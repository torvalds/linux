/*
 * arch/arm/mach-sun4i/clock/ccmu/ccm_sys_clk.c
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <mach/clock.h>
#include <mach/system.h>
#include <asm/delay.h>
#include "ccm_i.h"


#define make_sys_clk_inf(clk_id, clk_name)    {.id = clk_id, .name = clk_name,}



static __aw_ccu_clk_t aw_ccu_sys_clk[] =
{
    make_sys_clk_inf(AW_SYS_CLK_NONE,   "sclk_none"     ),
    make_sys_clk_inf(AW_SYS_CLK_LOSC,   "losc"          ),
    make_sys_clk_inf(AW_SYS_CLK_HOSC,   "hosc"          ),
    make_sys_clk_inf(AW_SYS_CLK_PLL1,   "core_pll"      ),
    make_sys_clk_inf(AW_SYS_CLK_PLL2,   "audio_pll"     ),
    make_sys_clk_inf(AW_SYS_CLK_PLL2X8, "audio_pllx8"   ),
    make_sys_clk_inf(AW_SYS_CLK_PLL3,   "video_pll0"    ),
    make_sys_clk_inf(AW_SYS_CLK_PLL3X2, "video_pll0x2"  ),
    make_sys_clk_inf(AW_SYS_CLK_PLL4,   "ve_pll"        ),
    make_sys_clk_inf(AW_SYS_CLK_PLL5,   "sdram_pll"     ),
    make_sys_clk_inf(AW_SYS_CLK_PLL5M,  "sdram_pll_m"   ),
    make_sys_clk_inf(AW_SYS_CLK_PLL5P,  "sdram_pll_p"   ),
    make_sys_clk_inf(AW_SYS_CLK_PLL6,   "sata_pll"      ),
    make_sys_clk_inf(AW_SYS_CLK_PLL7,   "video_pll1"    ),
    make_sys_clk_inf(AW_SYS_CLK_PLL7X2, "video_pll1x2"  ),
    make_sys_clk_inf(AW_SYS_CLK_200M,   "200m_pll"      ),
    make_sys_clk_inf(AW_SYS_CLK_CPU,    "cpu"           ),
    make_sys_clk_inf(AW_SYS_CLK_AXI,    "axi"           ),
    make_sys_clk_inf(AW_SYS_CLK_AHB,    "ahb"           ),
    make_sys_clk_inf(AW_SYS_CLK_APB0,   "apb"           ),
    make_sys_clk_inf(AW_SYS_CLK_APB1,   "apb1"          ),
    make_sys_clk_inf(AW_SYS_CLK_PLL6M,  "sata_pll_m"    ),
    make_sys_clk_inf(AW_SYS_CLK_PLL62,  "sata_pll_2"    ),
};


/*
*********************************************************************************************************
*                           sys_clk_get_parent
*
*Description: get parent clock for system clock;
*
*Arguments  : id    system clock id;
*
*Return     : parent id;
*
*Notes      :
*
*********************************************************************************************************
*/
static __aw_ccu_sys_clk_e sys_clk_get_parent(__aw_ccu_sys_clk_e id)
{
    switch(id)
    {
        case AW_SYS_CLK_PLL2X8:
        {
            return AW_SYS_CLK_PLL2;
        }
        case AW_SYS_CLK_PLL3X2:
        {
            return AW_SYS_CLK_PLL3;
        }
        case AW_SYS_CLK_PLL7X2:
        {
            return AW_SYS_CLK_PLL7;
        }
        case AW_SYS_CLK_PLL5M:
        case AW_SYS_CLK_PLL5P:
        {
            return AW_SYS_CLK_PLL5;
        }
        case AW_SYS_CLK_200M:
        {
            return AW_SYS_CLK_PLL62;
        }
        case AW_SYS_CLK_CPU:
        {
            switch(aw_ccu_reg->SysClkDiv.AC328ClkSrc)
            {
                case 0:
                    return AW_SYS_CLK_LOSC;
                case 1:
                    return AW_SYS_CLK_HOSC;
                case 2:
                    return AW_SYS_CLK_PLL1;
                case 3:
                default:
                    return AW_SYS_CLK_200M;
            }
        }
        case AW_SYS_CLK_AXI:
        {
            return AW_SYS_CLK_CPU;
        }
        case AW_SYS_CLK_AHB:
        {
            return AW_SYS_CLK_AXI;
        }
        case AW_SYS_CLK_APB0:
        {
            return AW_SYS_CLK_AHB;
        }
        case AW_SYS_CLK_APB1:
        {
            switch(aw_ccu_reg->Apb1ClkDiv.ClkSrc)
            {
                case 0:
                    return AW_SYS_CLK_HOSC;
                case 1:
                    return AW_SYS_CLK_PLL62;
                case 2:
                    return AW_SYS_CLK_LOSC;
                case 3:
                default:
                    return AW_SYS_CLK_NONE;
            }
        }
        default:
        {
            return AW_SYS_CLK_NONE;
        }
    }
}


/*
*********************************************************************************************************
*                           sys_clk_get_status
*
*Description: get system clock on/off status.
*
*Arguments  : id    system clock id;
*
*Return     : system clock status;
*               0, clock is off;
*              !0, clock is on;
*
*Notes      :
*
*********************************************************************************************************
*/
static __aw_ccu_clk_onff_e sys_clk_get_status(__aw_ccu_sys_clk_e id)
{
    switch(id)
    {
        case AW_SYS_CLK_LOSC:
            return AW_CCU_CLK_ON;
        case AW_SYS_CLK_HOSC:
            return aw_ccu_reg->HoscCtl.OSC24MEn? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;

        case AW_SYS_CLK_PLL1:
            return aw_ccu_reg->Pll1Ctl.PLLEn? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_SYS_CLK_PLL2:
        case AW_SYS_CLK_PLL2X8:
            return aw_ccu_reg->Pll2Ctl.PLLEn? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_SYS_CLK_PLL3:
        case AW_SYS_CLK_PLL3X2:
            return aw_ccu_reg->Pll3Ctl.PLLEn? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_SYS_CLK_PLL4:
            return aw_ccu_reg->Pll4Ctl.PLLEn? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_SYS_CLK_PLL5:
        case AW_SYS_CLK_PLL5M:
        case AW_SYS_CLK_PLL5P:
            return aw_ccu_reg->Pll5Ctl.PLLEn? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_SYS_CLK_PLL6:
        case AW_SYS_CLK_PLL6M:
        case AW_SYS_CLK_PLL62:
            return aw_ccu_reg->Pll6Ctl.PLLEn? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_SYS_CLK_PLL7:
        case AW_SYS_CLK_PLL7X2:
            return aw_ccu_reg->Pll7Ctl.PLLEn? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_SYS_CLK_200M:
            return aw_ccu_reg->Pll6Ctl.PLLEn? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;

        case AW_SYS_CLK_CPU:
        case AW_SYS_CLK_AXI:
        case AW_SYS_CLK_AHB:
        case AW_SYS_CLK_APB0:
        case AW_SYS_CLK_APB1:
        default:
            return AW_CCU_CLK_ON;
    }
}


/*
*********************************************************************************************************
*                           sys_clk_get_rate
*
*Description: get clock rate for system clock;
*
*Arguments  : id    system clock id;
*
*Return     : clock rate;
*
*Notes      :
*
*********************************************************************************************************
*/
static __s64 sys_clk_get_rate(__aw_ccu_sys_clk_e id)
{
    switch(id)
    {
        case AW_SYS_CLK_NONE:
        {
            return 1;
        }

        case AW_SYS_CLK_LOSC:
        {
            return 32768;
        }
        case AW_SYS_CLK_HOSC:
        {
            return 24000000;
        }
        case AW_SYS_CLK_PLL1:
        {
            return ccu_clk_uldiv(((__s64)24000000*aw_ccu_reg->Pll1Ctl.FactorN*(aw_ccu_reg->Pll1Ctl.FactorK + 1)   \
                >> aw_ccu_reg->Pll1Ctl.PLLDivP), (aw_ccu_reg->Pll1Ctl.FactorM + 1));
        }
        case AW_SYS_CLK_PLL2:
        {
            enum sw_ic_ver  chip_ver = sw_get_ic_ver();

            if (chip_ver == SUNXI_VER_A10A) {
                /* chip is version A */
                if((aw_ccu_reg->Pll2Ctl.VCOBias == 10) &&(aw_ccu_reg->Pll2Ctl.FactorN == 94))
                {
                    return 22579200;
                }
                else if((aw_ccu_reg->Pll2Ctl.VCOBias == 9) &&(aw_ccu_reg->Pll2Ctl.FactorN == 83))
                {
                    return 24576000;
                }
                else
                {
                    /* set audio pll to default value 24576000 */
                    aw_ccu_reg->Pll2Ctl.VCOBias = 9;
                    aw_ccu_reg->Pll2Ctl.FactorN = 83;
                    return 24576000;
                }
            } else if ((chip_ver == SUNXI_VER_A10B) || (chip_ver == SUNXI_VER_A10C)) {
                /* chip is version B:
                    FactorN=79, PreDiv=21, PostDiv=4, output=24*79/21/4=22.571mhz, 44.1k series fs
                    FactorN=86, PreDiv=21, PostDiv=4, output=24*86/21/4=24.571mhz, 48k series fs */

                __u32   tmpReg;

                tmpReg = *(volatile __u32 *)&aw_ccu_reg->Pll2Ctl;
                if(((tmpReg>>8) & 0x7f) == 79)
                {
                    return 22579200;
                }
                else if(((tmpReg>>8) & 0x7f) == 86)
                {
                    return 24576000;
                }
                else
                {
                    /* set audio pll to default value 24576000 */
                    tmpReg &= ~((0x1f<<0)|(0x7f<<8)|(0x0f<<26));
                    tmpReg |= (21<<0)|(86<<8)|(4<<26);
                    *(volatile __u32 *)&aw_ccu_reg->Pll2Ctl = tmpReg;
                    return 24576000;
                }
            }
            else {
                /* chip version is unknown */
                return 24576000;
            }
        }
        case AW_SYS_CLK_PLL2X8:
        {
            if(sys_clk_get_rate(AW_SYS_CLK_PLL2) == 24576000)
            {
                return 24576000 * 18;
            }
            else
            {
                return 22579200 * 20;
            }
        }
        case AW_SYS_CLK_PLL3:
        {
            __s64   tmp_rate;

            if(!aw_ccu_reg->Pll3Ctl.ModeSel)
            {
                if(aw_ccu_reg->Pll3Ctl.FracSet)
                {
                    return 297000000;
                }
                else
                {
                    return 270000000;
                }
            }
            else
            {
                tmp_rate = 3000000*aw_ccu_reg->Pll3Ctl.FactorM;
                /* skip 270M and 297M */
                if(tmp_rate == 270000000)
                {
                    return 273000000;
                }
                else if(tmp_rate == 297000000)
                {
                    return 300000000;
                }

                return tmp_rate;
            }
        }
        case AW_SYS_CLK_PLL3X2:
        {
            return sys_clk_get_rate(AW_SYS_CLK_PLL3) << 1;
        }

        case AW_SYS_CLK_PLL4:
        {
            #if(USE_PLL6M_REPLACE_PLL4)
            if (SUNXI_VER_A10C == sw_get_ic_ver())
                return sys_clk_get_rate(AW_SYS_CLK_PLL6);
            #endif

            return ccu_clk_uldiv(((__s64)24000000*aw_ccu_reg->Pll4Ctl.FactorN * (aw_ccu_reg->Pll4Ctl.FactorK + 1)   \
                >> aw_ccu_reg->Pll4Ctl.FactorP), (aw_ccu_reg->Pll4Ctl.FactorM + 1));
        }
        case AW_SYS_CLK_PLL5:
        {
            return (__s64)24000000*aw_ccu_reg->Pll5Ctl.FactorN * (aw_ccu_reg->Pll5Ctl.FactorK + 1);
        }
        case AW_SYS_CLK_PLL5M:
        {
            return ccu_clk_uldiv(sys_clk_get_rate(AW_SYS_CLK_PLL5), (aw_ccu_reg->Pll5Ctl.FactorM + 1));
        }
        case AW_SYS_CLK_PLL5P:
        {
            return sys_clk_get_rate(AW_SYS_CLK_PLL5) >> aw_ccu_reg->Pll5Ctl.FactorP;
        }
        case AW_SYS_CLK_PLL6:
        {
            return (__s64)24000000*aw_ccu_reg->Pll6Ctl.FactorN*(aw_ccu_reg->Pll6Ctl.FactorK + 1);
        }
        case AW_SYS_CLK_PLL6M:
        {
            return ccu_clk_uldiv((__s64)24000000*aw_ccu_reg->Pll6Ctl.FactorN*(aw_ccu_reg->Pll6Ctl.FactorK + 1),   \
                (aw_ccu_reg->Pll6Ctl.FactorM + 1) * 6);
        }
        case AW_SYS_CLK_PLL62:
        {
            return (__s64)24000000*aw_ccu_reg->Pll6Ctl.FactorN*(aw_ccu_reg->Pll6Ctl.FactorK + 1)>>1;
        }
        case AW_SYS_CLK_PLL7:
        {
            if(!aw_ccu_reg->Pll7Ctl.ModeSel)
            {
                if(aw_ccu_reg->Pll7Ctl.FracSet)
                {
                    return 297000000;
                }
                else
                {
                    return 270000000;
                }
            }
            else
            {
                return (__s64)3000000*aw_ccu_reg->Pll7Ctl.FactorM;
            }
        }
        case AW_SYS_CLK_PLL7X2:
        {
            return sys_clk_get_rate(AW_SYS_CLK_PLL7) << 1;
        }
        case AW_SYS_CLK_200M:
        {
            return 200000000;
        }
        case AW_SYS_CLK_CPU:
        {
            __u32   tmpCpuRate;
            switch(aw_ccu_reg->SysClkDiv.AC328ClkSrc)
            {
                case 0:
                    tmpCpuRate = 32768;
                    break;
                case 1:
                    tmpCpuRate = 24000000;
                    break;
                case 2:
                    tmpCpuRate = sys_clk_get_rate(AW_SYS_CLK_PLL1);
                    break;
                case 3:
                default:
                    tmpCpuRate = 200000000;
                    break;
            }
            return tmpCpuRate;
        }
        case AW_SYS_CLK_AXI:
        {
            return ccu_clk_uldiv(sys_clk_get_rate(AW_SYS_CLK_CPU), (aw_ccu_reg->SysClkDiv.AXIClkDiv + 1));
        }
        case AW_SYS_CLK_AHB:
        {
            return sys_clk_get_rate(AW_SYS_CLK_AXI) >> aw_ccu_reg->SysClkDiv.AHBClkDiv;
        }
        case AW_SYS_CLK_APB0:
        {
            __s32   tmpShift = aw_ccu_reg->SysClkDiv.APB0ClkDiv;

            return sys_clk_get_rate(AW_SYS_CLK_AHB) >> (tmpShift? tmpShift : 1);
        }
        case AW_SYS_CLK_APB1:
        {
            __s64   tmpApb1Rate;
            switch(aw_ccu_reg->Apb1ClkDiv.ClkSrc)
            {
                case 0:
                    tmpApb1Rate = 24000000;
                    break;
                case 1:
                    tmpApb1Rate = sys_clk_get_rate(AW_SYS_CLK_PLL62);
                    break;
                case 2:
                    tmpApb1Rate = 32768;
                    break;
                default:
                    tmpApb1Rate = 0;
                    break;
            }
            return ccu_clk_uldiv((tmpApb1Rate >> aw_ccu_reg->Apb1ClkDiv.PreDiv), (aw_ccu_reg->Apb1ClkDiv.ClkDiv + 1));
        }
        default:
        {
            return 0;
        }
    }
}


/*
*********************************************************************************************************
*                           sys_clk_set_parent
*
*Description: set parent clock id for system clock;
*
*Arguments  : id        system clock id whose parent need be set;
*             parent    parent id to be set;
*
*Return     : result,
*               0,  set parent successed;
*              !0,  set parent failed;
*
*Notes      :
*
*********************************************************************************************************
*/
static __s32 sys_clk_set_parent(__aw_ccu_sys_clk_e id, __aw_ccu_sys_clk_e parent)
{
    switch(id)
    {
        case AW_SYS_CLK_PLL2X8:
            return (parent == AW_SYS_CLK_PLL2)? 0 : -1;

        case AW_SYS_CLK_PLL3X2:
            return (parent == AW_SYS_CLK_PLL3)? 0 : -1;

        case AW_SYS_CLK_PLL5M:
        case AW_SYS_CLK_PLL5P:
            return (parent == AW_SYS_CLK_PLL5)? 0 : -1;

        case AW_SYS_CLK_PLL6M:
        case AW_SYS_CLK_PLL62:
            return (parent == AW_SYS_CLK_PLL6)? 0 : -1;

        case AW_SYS_CLK_PLL7X2:
            return (parent == AW_SYS_CLK_PLL7)? 0 : -1;

        case AW_SYS_CLK_200M:
            return (parent == AW_SYS_CLK_PLL62)? 0 : -1;

        case AW_SYS_CLK_CPU:
        {
            switch(parent)
            {
                case AW_SYS_CLK_LOSC:
                    aw_ccu_reg->SysClkDiv.AC328ClkSrc = 0;
                    break;
                case AW_SYS_CLK_HOSC:
                    aw_ccu_reg->SysClkDiv.AC328ClkSrc = 1;
                    break;
                case AW_SYS_CLK_PLL1:
                    aw_ccu_reg->SysClkDiv.AC328ClkSrc = 2;
                    break;
                case AW_SYS_CLK_200M:
                    aw_ccu_reg->SysClkDiv.AC328ClkSrc = 3;
                    break;
                default:
                    return -1;
            }
            return 0;
        }
        case AW_SYS_CLK_AXI:
            return (parent == AW_SYS_CLK_CPU)? 0 : -1;
        case AW_SYS_CLK_AHB:
            return (parent == AW_SYS_CLK_AXI)? 0 : -1;
        case AW_SYS_CLK_APB0:
            return (parent == AW_SYS_CLK_AHB)? 0 : -1;
        case AW_SYS_CLK_APB1:
        {
            switch(parent)
            {
                case AW_SYS_CLK_LOSC:
                    aw_ccu_reg->Apb1ClkDiv.ClkSrc = 2;
                    break;
                case AW_SYS_CLK_HOSC:
                    aw_ccu_reg->Apb1ClkDiv.ClkSrc = 0;
                    break;
                case AW_SYS_CLK_PLL62:
                    aw_ccu_reg->Apb1ClkDiv.ClkSrc = 1;
                    break;
                default:
                    return -1;
            }
            return 0;
        }

        case AW_SYS_CLK_LOSC:
        case AW_SYS_CLK_HOSC:
        case AW_SYS_CLK_PLL1:
        case AW_SYS_CLK_PLL2:
        case AW_SYS_CLK_PLL3:
        case AW_SYS_CLK_PLL4:
        case AW_SYS_CLK_PLL5:
        case AW_SYS_CLK_PLL6:
        case AW_SYS_CLK_PLL7:
        {
            return (parent == AW_SYS_CLK_NONE)? 0 : -1;
        }

        default:
        {
            return -1;
        }
    }
}


/*
*********************************************************************************************************
*                           sys_clk_set_status
*
*Description: set on/off status for system clock;
*
*Arguments  : id        system clock id;
*             status    on/off status;
*                           AW_CCU_CLK_OFF - off
*                           AW_CCU_CLK_ON - on
*
*Return     : result;
*               0,  set status successed;
*              !0,  set status failed;
*
*Notes      :
*
*********************************************************************************************************
*/
static __s32 sys_clk_set_status(__aw_ccu_sys_clk_e id, __aw_ccu_clk_onff_e status)
{
    switch(id)
    {
        case AW_SYS_CLK_LOSC:
            return 0;
        case AW_SYS_CLK_HOSC:
            aw_ccu_reg->HoscCtl.OSC24MEn = (status == AW_CCU_CLK_ON)? 1 : 0;
            return 0;
        case AW_SYS_CLK_PLL1:
            aw_ccu_reg->Pll1Ctl.PLLEn = (status == AW_CCU_CLK_ON)? 1 : 0;
            return 0;
        case AW_SYS_CLK_PLL2:
            aw_ccu_reg->Pll2Ctl.PLLEn = (status == AW_CCU_CLK_ON)? 1 : 0;
            return 0;
        case AW_SYS_CLK_PLL2X8:
        {
            if(status && !aw_ccu_reg->Pll2Ctl.PLLEn)
            {
                return -1;
            }
            else
            {
                return 0;
            }
        }
        case AW_SYS_CLK_PLL3:
            aw_ccu_reg->Pll3Ctl.PLLEn = (status == AW_CCU_CLK_ON)? 1 : 0;
            return 0;
        case AW_SYS_CLK_PLL3X2:
        {
            if(status && !aw_ccu_reg->Pll3Ctl.PLLEn)
            {
                return -1;
            }
            else
            {
                return 0;
            }
        }
        case AW_SYS_CLK_PLL4:
            aw_ccu_reg->Pll4Ctl.PLLEn = (status == AW_CCU_CLK_ON)? 1 : 0;
            return 0;
        case AW_SYS_CLK_PLL5:
            aw_ccu_reg->Pll5Ctl.PLLEn = (status == AW_CCU_CLK_ON)? 1 : 0;
            return 0;
        case AW_SYS_CLK_PLL5M:
        case AW_SYS_CLK_PLL5P:
        {
            if(status && !aw_ccu_reg->Pll5Ctl.PLLEn)
            {
                return -1;
            }
            else
            {
                return 0;
            }
        }
        case AW_SYS_CLK_PLL6:
            aw_ccu_reg->Pll6Ctl.PLLEn = (status == AW_CCU_CLK_ON)? 1 : 0;
            return 0;
        case AW_SYS_CLK_PLL6M:
        case AW_SYS_CLK_PLL62:
        {
            if(status && !aw_ccu_reg->Pll6Ctl.PLLEn)
            {
                return -1;
            }
            else
            {
                return 0;
            }
        }
        case AW_SYS_CLK_PLL7:
            aw_ccu_reg->Pll7Ctl.PLLEn = (status == AW_CCU_CLK_ON)? 1 : 0;
            return 0;
        case AW_SYS_CLK_PLL7X2:
        {
            if(status && !aw_ccu_reg->Pll7Ctl.PLLEn)
            {
                return -1;
            }
            else
            {
                return 0;
            }
        }
        case AW_SYS_CLK_200M:
        {
            if(status && !aw_ccu_reg->Pll6Ctl.PLLEn)
            {
                return -1;
            }
            else
            {
                return 0;
            }
        }
        case AW_SYS_CLK_CPU:
        case AW_SYS_CLK_AXI:
        case AW_SYS_CLK_AHB:
        case AW_SYS_CLK_APB0:
        case AW_SYS_CLK_APB1:
            return 0;

        default:
        {
            return -1;
        }
    }
}


/*
*********************************************************************************************************
*                           sys_clk_set_rate
*
*Description: set clock rate for system clock;
*
*Arguments  : id    system clock id;
*             rate  clock rate for system clock;
*
*Return     : result,
*               0,  set system clock rate successed;
*              !0,  set system clock rate failed;
*
*Notes      :
*
*********************************************************************************************************
*/
static __s32 sys_clk_set_rate(__aw_ccu_sys_clk_e id, __s64 rate)
{
    switch(id)
    {
        case AW_SYS_CLK_LOSC:
            return (rate == 32768)? 0 : -1;

        case AW_SYS_CLK_HOSC:
            return (rate == 24000000)? 0 : -1;
        case AW_SYS_CLK_PLL1:
        {
            struct core_pll_factor_t    factor;
            __ccmu_pll1_core_reg0000_t  tmp_pll;

            /* the setting of pll1 must be called by cpu-freq driver, and adjust pll step by step */
            ccm_clk_get_pll_para(&factor, rate);
            /* set factor */
            tmp_pll = aw_ccu_reg->Pll1Ctl;
            tmp_pll.FactorN = factor.FactorN;
            tmp_pll.FactorK = factor.FactorK;
            tmp_pll.FactorM = factor.FactorM;
            tmp_pll.PLLDivP = factor.FactorP;
            aw_ccu_reg->Pll1Ctl = tmp_pll;
            /* delay 500us for pll be stably */
            __delay((rate >> 20) * 500 / 2);

            return 0;
        }
        case AW_SYS_CLK_PLL2:
        {
            enum sw_ic_ver  chip_ver = sw_get_ic_ver();

            if(rate == 22579200)
            {
                if (chip_ver == SUNXI_VER_A10A) {
                    /* chip is version A */
                    aw_ccu_reg->Pll2Ctl.VCOBias = 10;
                    aw_ccu_reg->Pll2Ctl.FactorN = 94;
                }
                else if ((chip_ver == SUNXI_VER_A10B) || (chip_ver == SUNXI_VER_A10C)) {
                    /* chip is version B, FactorN=79, PreDiv=21, PostDiv=4,
                       output=24*79/21/4=22.571mhz, 44.1k series fs     */
                    __u32   tmpReg;

                    tmpReg = *(volatile __u32 *)&aw_ccu_reg->Pll2Ctl;
                    tmpReg &= ~((0x1f<<0)|(0x7f<<8)|(0x0f<<26));
                    tmpReg |= (21<<0)|(79<<8)|(4<<26);
                    *(volatile __u32 *)&aw_ccu_reg->Pll2Ctl = tmpReg;
                }
                else{
                    printk("[ccu] chip version is unknown!\n");
                    return -1;
                }
            }
            else if(rate == 24576000)
            {
                if (chip_ver == SUNXI_VER_A10A) {
                    /* chip is version A */
                    aw_ccu_reg->Pll2Ctl.VCOBias = 9;
                    aw_ccu_reg->Pll2Ctl.FactorN = 83;
                } else if ((chip_ver == SUNXI_VER_A10B) || (chip_ver == SUNXI_VER_A10C)) {
                    /* chip is version B, FactorN=86, PreDiv=21, PostDiv=4,
                       output=24*86/21/4=24.571mhz, 48k series fs       */
                    __u32   tmpReg;

                    tmpReg = *(volatile __u32 *)&aw_ccu_reg->Pll2Ctl;
                    tmpReg &= ~((0x1f<<0)|(0x7f<<8)|(0x0f<<26));
                   tmpReg |= (21<<0)|(86<<8)|(4<<26);
                    *(volatile __u32 *)&aw_ccu_reg->Pll2Ctl = tmpReg;
                }
                else{
                    printk("[ccu] chip version is unknown!\n");
                    return -1;
                }
            }
            else
            {
                return -1;
            }
            return 0;
        }
        case AW_SYS_CLK_PLL2X8:
        {
            if((sys_clk_get_rate(AW_SYS_CLK_PLL2) == 24576000) && (rate == 24576000 * 18))
            {
                return 0;
            }
            else if((sys_clk_get_rate(AW_SYS_CLK_PLL2) == 22579200) && (rate == 24576000 * 20))

            {
                return 0;
            }

            return -1;
        }
        case AW_SYS_CLK_PLL3:
        {
            if((rate < 9*3000000) || (rate > (127*3000000)))
            {
                CCU_ERR("Rate(%lld) is invalid when set PLL3 rate!\n", rate);
                return -1;
            }

            if(rate == 270000000)
            {
                aw_ccu_reg->Pll3Ctl.ModeSel = 0;
                aw_ccu_reg->Pll3Ctl.FracSet = 0;
            }
            else if(rate == 297000000)
            {
                aw_ccu_reg->Pll3Ctl.ModeSel = 0;
                aw_ccu_reg->Pll3Ctl.FracSet = 1;
            }
            else
            {
                aw_ccu_reg->Pll3Ctl.ModeSel = 1;
                aw_ccu_reg->Pll3Ctl.FactorM = ccu_clk_uldiv(rate + (3000000 - 1), 3000000);
            }
            return 0;
        }
        case AW_SYS_CLK_PLL3X2:
        {
            if(rate == (sys_clk_get_rate(AW_SYS_CLK_PLL3) << 1))
            {
                return 0;
            }
            return -1;
        }
        case AW_SYS_CLK_PLL4:
        {
            struct core_pll_factor_t    factor;
            __u32   tmpDly = ccu_clk_uldiv(sys_clk_get_rate(AW_SYS_CLK_CPU), 1000000) * 200;

            #if(USE_PLL6M_REPLACE_PLL4)
            if (SUNXI_VER_A10C == sw_get_ic_ver()) {
                CCU_ERR("PLL4 clock rate should not be set!\n");
                return -1;
            }
            #endif

            ccm_clk_get_pll_para(&factor, rate);

            /* set the correct parameter for PLL */
            aw_ccu_reg->Pll4Ctl.FactorN = factor.FactorN;
            aw_ccu_reg->Pll4Ctl.FactorK = factor.FactorK;
            aw_ccu_reg->Pll4Ctl.FactorM = factor.FactorM;
            aw_ccu_reg->Pll4Ctl.FactorP = factor.FactorP;
            /* delay 200us for pll be stably */
            __delay(tmpDly);

            return 0;
        }
        case AW_SYS_CLK_PLL5:
        {
            __s32   tmpFactorN, tmpFactorK;

            if(rate < 240000000)
            {
                CCU_ERR("Rate(%lld) is invalid when set PLL5 rate!\n", rate);
                return -1;
            }

            if(rate < 480000000)
            {
                tmpFactorK = 0;
            }
            else if(rate < 960000000)
            {
                tmpFactorK = 1;
            }
            else if(rate < 2000000000)
            {
                tmpFactorK = 3;
            }
            else
            {
                CCU_ERR("Rate (%lld) is invaid for PLL5!\n", rate);
                return -1;
            }

            tmpFactorN = ccu_clk_uldiv(rate + (((tmpFactorK+1) * 24000000) - 1), ((tmpFactorK+1) * 24000000));
            if(tmpFactorN > 31)
            {
                CCU_ERR("Rate (%lld) is invaid for PLL5!\n", rate);
                return -1;
            }

            aw_ccu_reg->Pll5Ctl.FactorN = tmpFactorN;
            aw_ccu_reg->Pll5Ctl.FactorK = tmpFactorK;

            return 0;
        }
        case AW_SYS_CLK_PLL5M:
        {
            __u32   tmpFactorM;
            __s64   tmpPLL5;

            tmpPLL5 = sys_clk_get_rate(AW_SYS_CLK_PLL5);
            if((rate > tmpPLL5) || (tmpPLL5 > rate*4))
            {
                CCU_ERR("PLL5(%lld) rate is invalid for PLL5M(%lld)!\n", tmpPLL5, rate);
                return -1;
            }

            tmpFactorM = ccu_clk_uldiv(tmpPLL5 + (rate - 1), rate);
            aw_ccu_reg->Pll5Ctl.FactorM = tmpFactorM-1;

            return 0;
        }
        case AW_SYS_CLK_PLL5P:
        {
            __s32   tmpFactorP = -1;
            __s64   tmpPLL5 = sys_clk_get_rate(AW_SYS_CLK_PLL5);

            if((rate > tmpPLL5) || (tmpPLL5 > rate*8))
            {
                CCU_ERR("PLL5(%lld) rate is invalid for PLL5P(%lld)!\n", tmpPLL5, rate);
                return -1;
            }

            do{rate <<= 1; tmpFactorP++;}while(rate < tmpPLL5);
            aw_ccu_reg->Pll5Ctl.FactorP = tmpFactorP;

            return 0;
        }
        case AW_SYS_CLK_PLL6:
        {
            __s32   tmpFactorN, tmpFactorK;

            if(rate <= 600000000)
                tmpFactorK = 0;
            else if(rate <= 1200000000)
                tmpFactorK = 1;
            else
            {
                CCU_ERR("Rate (%lld) is invaid for PLL6!\n", rate);
                return -1;
            }

            tmpFactorN = ccu_clk_uldiv(rate, ((tmpFactorK+1) * 24000000));
            if(tmpFactorN > 31)
            {
                CCU_ERR("Rate (%lld) is invaid for PLL6!\n", rate);
                return -1;
            }

            aw_ccu_reg->Pll6Ctl.FactorN = tmpFactorN;
            aw_ccu_reg->Pll6Ctl.FactorK = tmpFactorK;

            return 0;
        }
        case AW_SYS_CLK_PLL6M:
        {
            __s64   tmpPLL6 = sys_clk_get_rate(AW_SYS_CLK_PLL6);
            __s32   tmpFactorM = ccu_clk_uldiv(tmpPLL6, rate*6);

            tmpFactorM = tmpFactorM ? tmpFactorM : 1;
            aw_ccu_reg->Pll6Ctl.FactorM = tmpFactorM - 1;
            return 0;
        }
        case AW_SYS_CLK_PLL62:
        {
            /* rate of AW_SYS_CLK_PLL62 is always the half of the rate of AW_SYS_CLK_PLL6 */
            return 0;
        }
        case AW_SYS_CLK_PLL7:
        {
            if((rate < 9*3000000) || (rate > (127*3000000)))
            {
                CCU_ERR("Rate(%lld) is invalid when set PLL7 rate!\n", rate);
                return -1;
            }

            if(rate == 270000000)
            {
                aw_ccu_reg->Pll7Ctl.ModeSel = 0;
                aw_ccu_reg->Pll7Ctl.FracSet = 0;
            }
            else if(rate == 297000000)
            {
                aw_ccu_reg->Pll7Ctl.ModeSel = 0;
                aw_ccu_reg->Pll7Ctl.FracSet = 1;
            }
            else
            {
                aw_ccu_reg->Pll7Ctl.ModeSel = 1;
                aw_ccu_reg->Pll7Ctl.FactorM = ccu_clk_uldiv(rate + (3000000 - 1), 3000000);
            }
            return 0;
        }
        case AW_SYS_CLK_PLL7X2:
        {
            if(rate == (sys_clk_get_rate(AW_SYS_CLK_PLL7) << 1))
            {
                return 0;
            }
            return -1;
        }
        case AW_SYS_CLK_200M:
        {
            if(rate == 200000000)
            {
                return 0;
            }
            else
            {
                return -1;
            }
        }
        case AW_SYS_CLK_CPU:
        {
            __s64   tmpRate = sys_clk_get_rate(sys_clk_get_parent(AW_SYS_CLK_CPU));

            if(rate != tmpRate)
            {
                CCU_ERR("Rate(%lld) is invalid when set cpu rate(%lld)\n", rate, tmpRate);
                CCU_ERR("0xf1c20000 = 0x%x\n", *(volatile __u32 *)0xf1c20000);
                return -1;
            }
            else
            {
                return 0;
            }
        }
        case AW_SYS_CLK_AXI:
        {
            __s32   tmpDiv = ccu_clk_uldiv(sys_clk_get_rate(AW_SYS_CLK_CPU) + (rate - 1), rate);
            if(tmpDiv > 4)
            {
                CCU_ERR("Rate(%lld) is invalid when set axi rate\n", rate);
                return -1;
            }
            tmpDiv = tmpDiv? (tmpDiv-1) : 0;
            aw_ccu_reg->SysClkDiv.AXIClkDiv = tmpDiv;

            return 0;
        }
        case AW_SYS_CLK_AHB:
        {
            __s32   tmpVal = -1, tmpDiv = ccu_clk_uldiv(sys_clk_get_rate(AW_SYS_CLK_AXI) + (rate - 1), rate);

            if(tmpDiv > 8)
            {
                CCU_ERR("Rate(%lld) is invalid for set AHB rate!\n", rate);
                return -1;
            }

            do{tmpDiv >>= 1; tmpVal++;}while(tmpDiv);
            aw_ccu_reg->SysClkDiv.AHBClkDiv = tmpVal;

            return 0;
        }
        case AW_SYS_CLK_APB0:
        {
            __s32   tmpVal = -1, tmpDiv = ccu_clk_uldiv(sys_clk_get_rate(AW_SYS_CLK_AHB) + (rate - 1), rate);

            if(tmpDiv > 8)
            {
                CCU_ERR("Rate(%lld) is invalid for set APB0 rate!\n", rate);
                return -1;
            }

            do{tmpDiv >>= 1; tmpVal++;}while(tmpDiv);
            aw_ccu_reg->SysClkDiv.APB0ClkDiv = tmpVal;

            return 0;
        }
        case AW_SYS_CLK_APB1:
        {
            __s64   tmpRate = sys_clk_get_rate(sys_clk_get_parent(AW_SYS_CLK_APB1));
            __s32   tmpDivP, tmpDivM;

            if(tmpRate < rate)
            {
                CCU_ERR(" Rate (%lld) is invalid for set APB1 rate, parent is (%lld)!\n", rate, tmpRate);
                return -1;
            }

            tmpRate = ccu_clk_uldiv(tmpRate + (rate - 1), rate);
            if(tmpRate <= 4)
            {
                tmpDivP = 0;
                tmpDivM = tmpRate - 1;
            }
            else if(tmpRate <= 8)
            {
                tmpDivP = 1;
                tmpDivM = (tmpRate>>1) - 1;
            }
            else if(tmpRate <= 16)
            {
                tmpDivP = 2;
                tmpDivM = (tmpRate>>2) - 1;
            }
            else if(tmpRate <= 32)
            {
                tmpDivP = 3;
                tmpDivM = (tmpRate>>3) - 1;
            }
            else
            {
                CCU_ERR("Rate(%lld) is invalid for set APB1 rate!\n", rate);
                return -1;
            }

            aw_ccu_reg->Apb1ClkDiv.PreDiv = tmpDivP;
            aw_ccu_reg->Apb1ClkDiv.ClkDiv = tmpDivM;

            return 0;
        }
        default:
        {
            CCU_ERR("clock id(%d) is invaid when set rate!\n", (__s32)id);
            return -1;
        }
    }
}


/*
*********************************************************************************************************
*                           aw_ccu_get_sys_clk_cnt
*
*Description: get the count of the system clock.
*
*Arguments  : none
*
*Return     : count of system clock;
*
*Notes      :
*
*********************************************************************************************************
*/
__s32 aw_ccu_get_sys_clk_cnt(void)
{
    return (__u32)AW_SYS_CLK_CNT;
}


/*
*********************************************************************************************************
*                           aw_ccu_get_sys_clk
*
*Description: get system clock information by clock id.
*
*Arguments  : id    system clock id;
*
*Return     : system clock handle, return NULL if get clock information failed;
*
*Notes      :
*
*********************************************************************************************************
*/
__aw_ccu_clk_t *aw_ccu_get_sys_clk(__aw_ccu_sys_clk_e id)
{
    __s32   tmpIdx = (__u32)id;

    /* check if clock id is valid   */
    if((id < AW_SYS_CLK_NONE) || (id >= AW_SYS_CLK_CNT))
    {
        CCU_ERR("ID is invalid when get system clock information!\n");
        return NULL;
    }

    /* query system clock information from hardware */
    aw_ccu_sys_clk[tmpIdx].parent = sys_clk_get_parent(id);
    aw_ccu_sys_clk[tmpIdx].onoff  = sys_clk_get_status(id);
    aw_ccu_sys_clk[tmpIdx].rate   = sys_clk_get_rate(id);
    aw_ccu_sys_clk[tmpIdx].hash   = ccu_clk_calc_hash(aw_ccu_sys_clk[tmpIdx].name);

    return &aw_ccu_sys_clk[tmpIdx];
}


/*
*********************************************************************************************************
*                           aw_ccu_set_sys_clk
*
*Description: set system clock parameters;
*
*Arguments  : clk   handle of system clock;
*
*Return     : error type.
*
*Notes      :
*
*********************************************************************************************************
*/
__aw_ccu_err_e aw_ccu_set_sys_clk(__aw_ccu_clk_t *clk)
{
    __aw_ccu_clk_t  tmpClk;

    if(!clk)
    {
        CCU_ERR("Clock handle is NULL when set system clock!\n");
        return AW_CCU_ERR_PARA_NULL;
    }

    /* backup old parameter */
    tmpClk.parent = sys_clk_get_parent(clk->id);
    tmpClk.rate   = sys_clk_get_rate(clk->id);
    tmpClk.onoff  = sys_clk_get_status(clk->id);

    /* try to set new parameter */
    if(tmpClk.parent != clk->parent)
    {
        /* update parent */
        if(sys_clk_set_parent(clk->id, clk->parent))
        {
            CCU_ERR("try to set %s parent to %s failed!\n", clk->name, aw_ccu_sys_clk[clk->parent].name);
            goto _restore_clk_pare;
        }

        /* update clock parent */
        clk->parent = sys_clk_get_parent(clk->id);
        /* update clock rate */
        clk->rate = sys_clk_get_rate(clk->id);
        /* update clock status */
        clk->onoff = sys_clk_get_status(clk->id);
        return AW_CCU_ERR_NONE;
    }

    if(tmpClk.rate != clk->rate)
    {
        /* update clock rate */
        if(sys_clk_set_rate(clk->id, clk->rate))
        {
            CCU_ERR("try to set %s rate to %lld failed!\n", clk->name, clk->rate);
            goto _restore_clk_pare;
        }
        /* update clock rate */
        clk->rate = sys_clk_get_rate(clk->id);
        /* update clock status */
        clk->onoff = sys_clk_get_status(clk->id);
        return AW_CCU_ERR_NONE;
    }

    if(tmpClk.onoff != clk->onoff)
    {
        /* update clock rate */
        if(sys_clk_set_status(clk->id, clk->onoff))
        {
            CCU_ERR("try to set %s status to %d failed!\n", clk->name, clk->onoff);
            goto _restore_clk_pare;
        }
        /* update clock status */
        clk->onoff = sys_clk_get_status(clk->id);
        return AW_CCU_ERR_NONE;
    }

    /* nothing is need be updated */
    return AW_CCU_ERR_NONE;

    _restore_clk_pare:

    /* restore clock parameter  */
    sys_clk_set_parent(clk->id, tmpClk.parent);
    sys_clk_set_rate(clk->id, tmpClk.rate);
    sys_clk_set_status(clk->id, tmpClk.onoff);

    /* update clock manager paremter */
    clk->parent = tmpClk.parent;
    clk->onoff  = tmpClk.onoff;
    clk->rate   = tmpClk.rate;

    return AW_CCU_ERR_PARA_INVALID;
}

