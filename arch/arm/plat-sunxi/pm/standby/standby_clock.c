/*
 * arch/arm/plat-sunxi/pm/standby/standby_clock.c
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

#include "standby_i.h"

static __ccmu_reg_list_t   *CmuReg;;
static __u32    ccu_reg_back[7];
__u32   cpu_ms_loopcnt;

/*
*********************************************************************************************************
*                           standby_clk_init
*
*Description: ccu init for platform standby
*
*Arguments  : none
*
*Return     : result,
*
*Notes      :
*
*********************************************************************************************************
*/
__s32 standby_clk_init(void)
{
    CmuReg = (__ccmu_reg_list_t *)SW_VA_CCM_IO_BASE;

    /* backup pll registers */
    ccu_reg_back[0] = *(volatile __u32 *)&CmuReg->Pll1Ctl;
    ccu_reg_back[1] = *(volatile __u32 *)&CmuReg->Pll2Ctl;
    ccu_reg_back[2] = *(volatile __u32 *)&CmuReg->Pll3Ctl;
    ccu_reg_back[3] = *(volatile __u32 *)&CmuReg->Pll4Ctl;
    ccu_reg_back[4] = *(volatile __u32 *)&CmuReg->Pll5Ctl;
    ccu_reg_back[5] = *(volatile __u32 *)&CmuReg->Pll6Ctl;
    ccu_reg_back[6] = *(volatile __u32 *)&CmuReg->Pll7Ctl;

    /* cpu frequency is 60mhz now */
    cpu_ms_loopcnt = 3000;

    return 0;
}


/*
*********************************************************************************************************
*                           standby_clk_exit
*
*Description: ccu exit for platform standby
*
*Arguments  : none
*
*Return     : result,
*
*Notes      :
*
*********************************************************************************************************
*/
__s32 standby_clk_exit(void)
{
    /* restore pll registers */
    *(volatile __u32 *)&CmuReg->Pll1Ctl = ccu_reg_back[0];
    *(volatile __u32 *)&CmuReg->Pll2Ctl = ccu_reg_back[1];
    *(volatile __u32 *)&CmuReg->Pll3Ctl = ccu_reg_back[2];
    *(volatile __u32 *)&CmuReg->Pll4Ctl = ccu_reg_back[3];
    *(volatile __u32 *)&CmuReg->Pll5Ctl = ccu_reg_back[4];
    *(volatile __u32 *)&CmuReg->Pll6Ctl = ccu_reg_back[5];
    *(volatile __u32 *)&CmuReg->Pll7Ctl = ccu_reg_back[6];

    return 0;
}


/*
*********************************************************************************************************
*                                     standby_clk_core2losc
*
* Description: switch core clock to 32k low osc.
*
* Arguments  : none
*
* Returns    : 0;
*********************************************************************************************************
*/
__s32 standby_clk_core2losc(void)
{
    CmuReg->SysClkDiv.AC328ClkSrc = 0;
    /* cpu frequency is 32k hz */
    cpu_ms_loopcnt = 1;

    return 0;
}


/*
*********************************************************************************************************
*                                     standby_clk_core2hosc
*
* Description: switch core clock to 24M high osc.
*
* Arguments  : none
*
* Returns    : 0;
*********************************************************************************************************
*/
__s32 standby_clk_core2hosc(void)
{
    CmuReg->SysClkDiv.AC328ClkSrc = 1;
    /* cpu frequency is 24M hz */
    cpu_ms_loopcnt = 600;

    return 0;
}


/*
*********************************************************************************************************
*                                     standby_clk_core2pll
*
* Description: switch core clock to core pll.
*
* Arguments  : none
*
* Returns    : 0;
*********************************************************************************************************
*/
__s32 standby_clk_core2pll(void)
{
    CmuReg->SysClkDiv.AC328ClkSrc = 2;
    /* cpu frequency is 60M hz */
    cpu_ms_loopcnt = 2000;

    return 0;
}


/*
*********************************************************************************************************
*                                     standby_clk_plldisable
*
* Description: disable dram pll.
*
* Arguments  : none
*
* Returns    : 0;
*********************************************************************************************************
*/
__s32 standby_clk_plldisable(void)
{
    CmuReg->Pll1Ctl.PLLEn = 0;
    CmuReg->Pll2Ctl.PLLEn = 0;
    CmuReg->Pll3Ctl.PLLEn = 0;
    CmuReg->Pll4Ctl.PLLEn = 0;
    CmuReg->Pll5Ctl.PLLEn = 0;
    CmuReg->Pll6Ctl.PLLEn = 0;
    CmuReg->Pll7Ctl.PLLEn = 0;

    return 0;
}


/*
*********************************************************************************************************
*                                     standby_clk_pllenable
*
* Description: enable dram pll.
*
* Arguments  : none
*
* Returns    : 0;
*********************************************************************************************************
*/
__s32 standby_clk_pllenable(void)
{
    CmuReg->Pll1Ctl.PLLEn = 1;
    CmuReg->Pll2Ctl.PLLEn = 1;
    CmuReg->Pll3Ctl.PLLEn = 1;
    CmuReg->Pll4Ctl.PLLEn = 1;
    CmuReg->Pll5Ctl.PLLEn = 1;
    CmuReg->Pll6Ctl.PLLEn = 1;
    CmuReg->Pll7Ctl.PLLEn = 1;

    return 0;
}


/*
*********************************************************************************************************
*                                     standby_clk_hoscdisable
*
* Description: disable HOSC.
*
* Arguments  : none
*
* Returns    : 0;
*********************************************************************************************************
*/
__s32 standby_clk_hoscdisable(void)
{
    CmuReg->HoscCtl.OSC24MEn = 0;
    return 0;
}


/*
*********************************************************************************************************
*                                     standby_clk_hoscenable
*
* Description: enable HOSC.
*
* Arguments  : none
*
* Returns    : 0;
*********************************************************************************************************
*/
__s32 standby_clk_hoscenable(void)
{
    CmuReg->HoscCtl.OSC24MEn = 1;
    return 0;
}


/*
*********************************************************************************************************
*                                     standby_clk_ldodisable
*
* Description: disable LDO.
*
* Arguments  : none
*
* Returns    : 0;
*********************************************************************************************************
*/
__s32 standby_clk_ldodisable(void)
{
    CmuReg->HoscCtl.KeyField = 0x538;
    CmuReg->HoscCtl.LDOEn = 0;
    CmuReg->Pll5Ctl.LDO2En = 0;
    CmuReg->HoscCtl.KeyField = 0x00;
    return 0;
}


/*
*********************************************************************************************************
*                                     standby_clk_ldoenable
*
* Description: enable LDO.
*
* Arguments  : none
*
* Returns    : 0;
*********************************************************************************************************
*/
__s32 standby_clk_ldoenable(void)
{
    CmuReg->HoscCtl.KeyField = 0x538;
    CmuReg->HoscCtl.LDOEn = 1;
    CmuReg->Pll5Ctl.LDO2En = 1;
    CmuReg->HoscCtl.KeyField = 0x00;
    return 0;
}


/*
*********************************************************************************************************
*                                     standby_clk_setdiv
*
* Description: switch core clock to 32k low osc.
*
* Arguments  : none
*
* Returns    : 0;
*********************************************************************************************************
*/
__s32 standby_clk_setdiv(struct sun4i_clk_div_t  *clk_div)
{
    if(!clk_div)
    {
        return -1;
    }

    CmuReg->SysClkDiv.AXIClkDiv = clk_div->axi_div;
    CmuReg->SysClkDiv.AHBClkDiv = clk_div->ahb_div;
    CmuReg->SysClkDiv.APB0ClkDiv = clk_div->apb_div;

    return 0;
}


/*
*********************************************************************************************************
*                                     standby_clk_getdiv
*
* Description: switch core clock to 32k low osc.
*
* Arguments  : none
*
* Returns    : 0;
*********************************************************************************************************
*/
__s32 standby_clk_getdiv(struct sun4i_clk_div_t  *clk_div)
{
    if(!clk_div)
    {
        return -1;
    }

    clk_div->axi_div = CmuReg->SysClkDiv.AXIClkDiv;
    clk_div->ahb_div = CmuReg->SysClkDiv.AHBClkDiv;
    clk_div->apb_div = CmuReg->SysClkDiv.APB0ClkDiv;

    return 0;
}


/*
*********************************************************************************************************
*                                     standby_clk_dramgating
*
* Description: gating dram clock.
*
* Arguments  : onoff    dram clock gating on or off;
*
* Returns    : 0;
*********************************************************************************************************
*/
void standby_clk_dramgating(int onoff)
{
    if(onoff) {
        CmuReg->Pll5Ctl.OutputEn = 1;
    }
    else {
        CmuReg->Pll5Ctl.OutputEn = 0;
    }
}


/*
*********************************************************************************************************
*                                     standby_clk_apb2losc
*
* Description: switch apb1 clock to 32k low osc.
*
* Arguments  : none
*
* Returns    : 0;
*********************************************************************************************************
*/
__s32 standby_clk_apb2losc(void)
{
    CmuReg->Apb1ClkDiv.ClkSrc = 2;
    return 0;
}


/*
*********************************************************************************************************
*                                     standby_clk_apb2hosc
*
* Description: switch apb1 clock to 24M hosc.
*
* Arguments  : none
*
* Returns    : 0;
*********************************************************************************************************
*/
__s32 standby_clk_apb2hosc(void)
{
    CmuReg->Apb1ClkDiv.ClkSrc = 0;
    return 0;
}
