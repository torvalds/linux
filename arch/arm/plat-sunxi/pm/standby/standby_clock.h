/*
 * arch/arm/plat-sunxi/pm/standby/standby_clock.h
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

#ifndef __STANDBY_CLOCK_H__
#define __STANDBY_CLOCK_H__

#include "standby_cfg.h"
#include <mach/ccmu_regs.h>

struct sun4i_clk_div_t {
    __u32   cpu_div:4;      /* division of cpu clock, divide core_pll */
    __u32   axi_div:4;      /* division of axi clock, divide cpu clock*/
    __u32   ahb_div:4;      /* division of ahb clock, divide axi clock*/
    __u32   apb_div:4;      /* division of apb clock, divide ahb clock*/
    __u32   reserved:16;
};

__s32 standby_clk_init(void);
__s32 standby_clk_exit(void);
__s32 standby_clk_core2losc(void);
__s32 standby_clk_core2hosc(void);
__s32 standby_clk_core2pll(void);
__s32 standby_clk_plldisable(void);
__s32 standby_clk_pllenable(void);
__s32 standby_clk_hoscdisable(void);
__s32 standby_clk_hoscenable(void);
__s32 standby_clk_ldodisable(void);
__s32 standby_clk_ldoenable(void);
__s32 standby_clk_setdiv(struct sun4i_clk_div_t  *clk_div);
__s32 standby_clk_getdiv(struct sun4i_clk_div_t  *clk_div);
void standby_clk_dramgating(int onoff);
__s32 standby_clk_apb2losc(void);
__s32 standby_clk_apb2hosc(void);

extern __u32   cpu_ms_loopcnt;

#endif  /* __STANDBY_CLOCK_H__ */
