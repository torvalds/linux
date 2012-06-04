/*
 * arch/arm/mach-sun4i/clock/ccmu/ccm_i.h
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

#ifndef __AW_CCMU_I_H__
#define __AW_CCMU_I_H__

#include <linux/kernel.h>
#include <mach/ccmu_regs.h>
#include <mach/system.h>
#include <asm/io.h>

extern __ccmu_reg_list_t   *aw_ccu_reg;


#undef CCU_DBG
#undef CCU_ERR
#if (1)
    #define CCU_DBG(format,args...)   printk("[ccmu] "format,##args)
    #define CCU_ERR(format,args...)   printk("[ccmu] "format,##args)
#else
    #define CCU_DBG(...)
    #define CCU_ERR(...)
#endif


struct core_pll_factor_t {
    __u8    FactorN;
    __u8    FactorK;
    __u8    FactorM;
    __u8    FactorP;
};

extern int ccm_clk_get_pll_para(struct core_pll_factor_t *factor, __u64 rate);

#endif /* #ifndef __AW_CCMU_I_H__ */

