/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : dram.h
* By      : kevin.z
* Version : v1.0
* Date    : 2011-6-2 14:54
* Descript: dram interface
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/

#ifndef __AW_DRAM_H__
#define __AW_DRAM_H__

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
void dram_power_save_process(void);
unsigned int dram_power_up_process(void);

#endif  /* __AW_DRAM_H__ */

