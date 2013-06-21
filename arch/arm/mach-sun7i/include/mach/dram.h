/*
 * arch/arm/mach-sun7i/include/mach/dram.h
 *
 * Copyright (C) 2012 - 2016 Reuuimlla Limited
 * Kevin <kevin@reuuimllatech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#ifndef __DRAM_H__
#define __DRAM_H__

#include <linux/kernel.h>

struct dram_para_t
{
    unsigned int    dram_baseaddr;
    unsigned int    dram_clk;
    unsigned int    dram_type;
    unsigned int    dram_rank_num;
    unsigned int    dram_chip_density;
    unsigned int    dram_io_width;
    unsigned int    dram_bus_width;
    unsigned int    dram_cas;
    unsigned int    dram_zq;
    unsigned int    dram_odt_en;
    unsigned int    dram_size;
    unsigned int    dram_tpr0;
    unsigned int    dram_tpr1;
    unsigned int    dram_tpr2;
    unsigned int    dram_tpr3;
    unsigned int    dram_tpr4;
    unsigned int    dram_tpr5;
    unsigned int    dram_emr1;
    unsigned int    dram_emr2;
    unsigned int    dram_emr3;
};

int dram_init(void);
int dram_exit(void);
int dram_get_size(void);
void dram_set_clock(int clk);
void dram_set_drive(void);
void dram_set_autorefresh_cycle(unsigned int clk);
int  dram_scan_readpipe(void);
void dram_enter_selfrefresh(void);
void dram_exit_selfrefresh(void);
void dram_enter_power_down(void);
void dram_exit_power_down(void);
void dram_hostport_on_off(unsigned int port_idx, unsigned int on);
unsigned int dram_hostport_check_ahb_fifo_status(unsigned int port_idx);
void dram_hostport_setup(unsigned int port, unsigned int prio, unsigned int wait_cycle, unsigned int cmd_num);
int dram_power_save_process(int standby_mode);
unsigned int dram_power_up_process(void);


#if defined(CONFIG_ARCH_SUN4I)
    #define HOST_PORT_SIZE 21
#elif defined(CONFIG_ARCH_SUN5I) || defined(CONFIG_ARCH_SUN7I)
    #define HOST_PORT_SIZE 14
#endif

#define DRAM_HOST_CFG_BASE  (SW_VA_DRAM_IO_BASE + 0x250)
#define DRAM_HOST_CFG_PORT  ((__dram_host_cfg_reg_t *)(DRAM_HOST_CFG_BASE + 4*port))

#define HOST_PORT_ATTR(_name)       \
{									\
	.attr = { .name = #_name,.mode = 0644 },    \
	.show =  _name##_show,          \
	.store = _name##_store,         \
}

typedef struct __DRAM_HOST_CFG_REG{
    unsigned int    AcsEn:1;    //bit0, host port access enable
    unsigned int    reserved0:1;    //bit1
    unsigned int    PrioLevel:2;    //bit2, host port poriority level
    unsigned int    WaitState:4;    //bit4, host port wait state
    unsigned int    CmdNum:8;       //bit8, host port command number
    unsigned int    reserved1:14;   //bit16
    unsigned int    WrCntEn:1;      //bit30, host port write counter enable
    unsigned int    RdCntEn:1;      //bit31, host port read counter enable
} __dram_host_cfg_reg_t;

typedef enum __DRAM_HOST_PORT{
    DRAM_HOST_CPU   = 16,
    DRAM_HOST_GPU   = 17,
    DRAM_HOST_BE    = 18,
    DRAM_HOST_FE    = 19,
    DRAM_HOST_CSI   = 20,
    DRAM_HOST_TSDM  = 21,
    DRAM_HOST_VE    = 22,
    DRAM_HOST_USB1  = 24,
    DRAM_HOST_NDMA  = 25,
    DRAM_HOST_ATH   = 26,
    DRAM_HOST_IEP   = 27,
    DRAM_HOST_SDHC  = 28,
    DRAM_HOST_DDMA  = 29,
    DRAM_HOST_GPS   = 30,
} __dram_host_port_e;


int dram_host_port_cmd_num_set(__dram_host_port_e port, unsigned int num);
int dram_host_port_cmd_num_get(__dram_host_port_e port);
int dram_host_port_wait_state_set(__dram_host_port_e port, unsigned int state);
int dram_host_port_wait_state_get(__dram_host_port_e port);
int dram_host_port_prio_level_set(__dram_host_port_e port, unsigned int level);
int dram_host_port_prio_level_get(__dram_host_port_e port);
int dram_host_port_acs_enable(__dram_host_port_e port);
int dram_host_port_acs_disable(__dram_host_port_e port);
int dram_host_port_acs_get(__dram_host_port_e port);

#endif  /* __DRAM_H__ */

