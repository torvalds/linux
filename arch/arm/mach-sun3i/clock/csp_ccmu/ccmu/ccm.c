/*
 * arch/arm/mach-sun3i/clock/csp_ccmu/ccmu/ccm.c
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

#include "ccm_i.h"


u32 g_virMemBaseCcm = 0;


#define _CCM_PHY_BASE   0X01C20000
#define _CCM_MEM_SZ     0X1000

__s32 CSP_CCM_init(void)
{

    g_virMemBaseCcm = (u32)ioremap(_CCM_PHY_BASE, _CCM_MEM_SZ);
    if(!g_virMemBaseCcm){
        return AW_CCMU_FAIL;
    }

    return AW_CCMU_OK;
}

__s32 CSP_CCM_exit( void )
{
    return AW_CCMU_OK;
}

u32 CSP_CCM_get_sys_clk_total_num( void )
{
    return (u32)CSP_CCM_SYS_CLK_TOTAL_NUM;
}

u32 CSP_CCM_get_mod_clk_total_num( void )
{
    return (u32)(CSP_CCM_MOD_CLK_TOTAL_NUM);
}

