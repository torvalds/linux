/*
 * arch/arm/mach-sun4i/include/mach/dram.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Kevin Zhang <kevin@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
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

