/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        newbie Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, kevin.z China
*                                             All Rights Reserved
*
* File    : super_clock.h
* By      : kevin.z
* Version : v1.0
* Date    : 2011-5-31 21:05
* Descript:
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#ifndef __SUPER_CLOCK_H__
#define __SUPER_CLOCK_H__

#include "super_cfg.h"
#include <mach/ccmu.h>



void mem_clk_dramgating(int onoff);
void mem_clk_dramgating_nommu(int onoff);
__s32 mem_clk_init(void);
__s32 mem_clk_plldisable(void);
__s32 mem_clk_pllenable(void);
__s32 mem_clk_set_pll_factor(struct pll_factor_t *pll_factor);
__s32 mem_clk_get_pll_factor(struct pll_factor_t *pll_factor);
__s32 mem_clk_setdiv(struct clk_div_t  *clk_div);
__s32 mem_clk_getdiv(struct clk_div_t  *clk_div);

extern __u32   cpu_ms_loopcnt;

#endif  /* __SUPER_CLOCK_H__ */

