#include "verilated.h"
#include "verilated_vcd_c.h"
#include "Vtop_axi_wrapper.h"

bool running = true;
#undef assert
void assert(bool expr) {
    if (!expr) {
        running = false;
        printf("soc_simulator assert failed!\n");
    }
}

#include "axi4.hpp"
#include "axi4_mem.hpp"
#include "axi4_xbar.hpp"
#include "mmio_mem.hpp"
#include "uartlite.hpp"

#include <iostream>
#include <termios.h>
#include <unistd.h>
#include <thread>
#include <csignal>
#include <sstream>

void connect_wire(axi4_ptr <32,64,4> &mmio_ptr, Vtop_axi_wrapper *top) {
    // connect
    // mmio
    // aw   
    mmio_ptr.awaddr     = &(top->MAXI_awaddr);
    mmio_ptr.awburst    = &(top->MAXI_awburst);
    mmio_ptr.awid       = &(top->MAXI_awid);
    mmio_ptr.awlen      = &(top->MAXI_awlen);
    mmio_ptr.awready    = &(top->MAXI_awready);
    mmio_ptr.awsize     = &(top->MAXI_awsize);
    mmio_ptr.awvalid    = &(top->MAXI_awvalid);
    // w
    mmio_ptr.wdata      = &(top->MAXI_wdata);
    mmio_ptr.wlast      = &(top->MAXI_wlast);
    mmio_ptr.wready     = &(top->MAXI_wready);
    mmio_ptr.wstrb      = &(top->MAXI_wstrb);
    mmio_ptr.wvalid     = &(top->MAXI_wvalid);
    // b
    mmio_ptr.bid        = &(top->MAXI_bid);
    mmio_ptr.bready     = &(top->MAXI_bready);
    mmio_ptr.bresp      = &(top->MAXI_bresp);
    mmio_ptr.bvalid     = &(top->MAXI_bvalid);
    // ar
    mmio_ptr.araddr     = &(top->MAXI_araddr);
    mmio_ptr.arburst    = &(top->MAXI_arburst);
    mmio_ptr.arid       = &(top->MAXI_arid);
    mmio_ptr.arlen      = &(top->MAXI_arlen);
    mmio_ptr.arready    = &(top->MAXI_arready);
    mmio_ptr.arsize     = &(top->MAXI_arsize);
    mmio_ptr.arvalid    = &(top->MAXI_arvalid);
    // r
    mmio_ptr.rdata      = &(top->MAXI_rdata);
    mmio_ptr.rid        = &(top->MAXI_rid);
    mmio_ptr.rlast      = &(top->MAXI_rlast);
    mmio_ptr.rready     = &(top->MAXI_rready);
    mmio_ptr.rresp      = &(top->MAXI_rresp);
    mmio_ptr.rvalid     = &(top->MAXI_rvalid);
}

bool trace_on = false;
long sim_time = 1e3;

void uart_input(uartlite &uart) {
    termios tmp;
    tcgetattr(STDIN_FILENO,&tmp);
    tmp.c_lflag &=(~ICANON & ~ECHO);
    tcsetattr(STDIN_FILENO,TCSANOW,&tmp);
    while (running) {
        char c = getchar();
        if (c == 10) c = 13; // convert lf to cr
        uart.putc(c);
    }
}

void workbench_run(Vtop_axi_wrapper *top, axi4_ref <32,64,4> &mmio_ref) {
    axi4     <32,64,4> mmio_sigs;
    axi4_ref <32,64,4> mmio_sigs_ref(mmio_sigs);
    axi4_xbar<32,64,4> mmio;

    // setup boot ram
    mmio_mem boot_ram(262144*4, "../soft/start.bin");
    assert(mmio.add_dev(0x60000000,0x100000,&boot_ram));

    // setup uart
    uartlite uart;
    std::thread *uart_input_thread = new std::thread(uart_input,std::ref(uart));
    assert(mmio.add_dev(0x60100000,0x10000,&uart));
    
    // connect Vcd for trace
    VerilatedVcdC vcd;
    if (trace_on) {
        top->trace(&vcd,0);
        vcd.open("trace.vcd");
    }

    uint64_t rst_ticks = 10;
    uint64_t ticks = 0;
    while (!Verilated::gotFinish() && sim_time > 0 && running) {
        if (rst_ticks  > 0) {
            top->reset = 1;
            rst_ticks --;
        }
        else top->reset = 0;
        top->clock = !top->clock;
        if (top->clock && !top->reset) mmio_sigs.update_input(mmio_ref);
        top->eval();
        if (top->clock && !top->reset) {
            mmio.beat(mmio_sigs_ref);
            mmio_sigs.update_output(mmio_ref);
            if (uart.exist_tx()) {
                printf("%c",uart.getc());
                fflush(stdout);
            }
        }
        if (trace_on) {
            vcd.dump(ticks);
            sim_time --;
        }
        ticks ++;
    }
    if (trace_on) vcd.close();
    top->final();
    pthread_kill(uart_input_thread->native_handle(),SIGKILL);
}

int main(int argc, char** argv, char** env) {
    Verilated::commandArgs(argc, argv);

    std::signal(SIGINT, [](int) {
        running = false;
    });

    for (int i=1;i<argc;i++) {
        if (strcmp(argv[i],"-trace") == 0) {
            trace_on = true;
            if (i+1 < argc) {
                sscanf(argv[++i],"%lu",&sim_time);
            }
        }
    }

    Verilated::traceEverOn(trace_on);

    enum {NOP, WORKBENCH} run_mode = WORKBENCH;
    
    // setup soc
    Vtop_axi_wrapper *top = new Vtop_axi_wrapper;
    axi4_ptr <32,64,4> mmio_ptr;

    connect_wire(mmio_ptr,top);
    assert(mmio_ptr.check());
    
    axi4_ref <32,64,4> mmio_ref(mmio_ptr);

    switch (run_mode) {
        case WORKBENCH:
            workbench_run(top, mmio_ref);
            break;
        default:
            printf("Unknown running mode.\n");
            exit(-ENOENT);
    }

    return 0;
}
