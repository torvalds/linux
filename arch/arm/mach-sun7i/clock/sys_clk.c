/*
 * arch/arm/mach-sun7i/clock/ccmu/ccm_sys_clk.c
 * (c) Copyright 2010-2015
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * James Deng <csjamesdeng@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <mach/includes.h>
#include <asm/delay.h>
#include "ccm_i.h"

/*
 * Get parent clock for system clock.
 *
 * @id:     system clock id.
 *
 */
static __aw_ccu_clk_id_e sys_clk_get_parent(__aw_ccu_clk_id_e id)
{
    switch (id) {
        case AW_SYS_CLK_PLL2X8: {
            return AW_SYS_CLK_PLL2;
        }
        case AW_SYS_CLK_PLL3X2: {
            return AW_SYS_CLK_PLL3;
        }
        case AW_SYS_CLK_PLL7X2: {
            return AW_SYS_CLK_PLL7;
        }
        case AW_SYS_CLK_PLL5M:
        case AW_SYS_CLK_PLL5P: {
            return AW_SYS_CLK_PLL5;
        }
        case AW_SYS_CLK_CPU: {
            switch (aw_ccu_reg->SysClkDiv.AC327ClkSrc) {
                case 0:
                    return AW_SYS_CLK_LOSC;
                case 1:
                    return AW_SYS_CLK_HOSC;
                case 2:
                    return AW_SYS_CLK_PLL1;
                case 3:
                    return AW_SYS_CLK_PLL6;
                default:
                    return AW_SYS_CLK_NONE;
            }
        }
        case AW_SYS_CLK_AXI: {
            return AW_SYS_CLK_CPU;
        }
        case AW_SYS_CLK_ATB: {
            return AW_SYS_CLK_CPU;
        }
        case AW_SYS_CLK_AHB: {
            switch (aw_ccu_reg->SysClkDiv.AHBClkSrc) {
                case 0:
                    return AW_SYS_CLK_AXI;
                case 1:
                    return AW_SYS_CLK_PLL62;
                case 2:
                    return AW_SYS_CLK_PLL6;
                default:
                    aw_ccu_reg->SysClkDiv.AHBClkSrc = 0;
                    return AW_SYS_CLK_AXI;
            }
        }
        case AW_SYS_CLK_APB0: {
            return AW_SYS_CLK_AHB;
        }
        case AW_SYS_CLK_APB1: {
            switch (aw_ccu_reg->Apb1ClkDiv.ClkSrc) {
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
        default: {
            return AW_SYS_CLK_NONE;
        }
    }
}

/*
 * Get system clock on/off status.
 *
 * @id:     system clock id.
 *
 */
static __aw_ccu_clk_onff_e sys_clk_get_status(__aw_ccu_clk_id_e id)
{
    switch (id) {
        case AW_SYS_CLK_LOSC:
            return AW_CCU_CLK_ON;
        case AW_SYS_CLK_HOSC:
            return aw_ccu_reg->HoscCtl.OSC24MEn ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;

        case AW_SYS_CLK_PLL1:
            return aw_ccu_reg->Pll1Ctl.PLLEn ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_SYS_CLK_PLL2:
        case AW_SYS_CLK_PLL2X8:
            return aw_ccu_reg->Pll2Ctl.PLLEn ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_SYS_CLK_PLL3:
        case AW_SYS_CLK_PLL3X2:
            return aw_ccu_reg->Pll3Ctl.PLLEn ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_SYS_CLK_PLL4:
            return aw_ccu_reg->Pll4Ctl.PLLEn ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_SYS_CLK_PLL5:
        case AW_SYS_CLK_PLL5M:
        case AW_SYS_CLK_PLL5P:
            return aw_ccu_reg->Pll5Ctl.PLLEn ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_SYS_CLK_PLL6:
        case AW_SYS_CLK_PLL6M:
        case AW_SYS_CLK_PLL62:
            return aw_ccu_reg->Pll6Ctl.PLLEn ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_SYS_CLK_PLL7:
        case AW_SYS_CLK_PLL7X2:
            return aw_ccu_reg->Pll7Ctl.PLLEn ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_SYS_CLK_PLL8:
            return aw_ccu_reg->Pll8Ctl.PLLEn ? AW_CCU_CLK_ON : AW_CCU_CLK_OFF;
        case AW_SYS_CLK_CPU:
        case AW_SYS_CLK_AXI:
        case AW_SYS_CLK_ATB:
        case AW_SYS_CLK_AHB:
            return sys_clk_get_status(sys_clk_get_parent(id));
        case AW_SYS_CLK_APB0:
        case AW_SYS_CLK_APB1:
        default:
            return AW_CCU_CLK_ON;
    }
}

/*
 * Get clock rate for system clock.
 *
 * @id:     system clock id.
 *
 */
static __u64 sys_clk_get_rate(__aw_ccu_clk_id_e id)
{
    switch (id) {
        case AW_SYS_CLK_NONE: {
            return 1;
        }

        case AW_SYS_CLK_LOSC: {
            return 32768;
        }
        case AW_SYS_CLK_HOSC: {
            return 24000000;
        }
        case AW_SYS_CLK_PLL1: {
            return ccu_clk_uldiv(((__s64)24000000 * aw_ccu_reg->Pll1Ctl.FactorN * (aw_ccu_reg->Pll1Ctl.FactorK + 1)   \
                                  >> aw_ccu_reg->Pll1Ctl.PLLDivP), (aw_ccu_reg->Pll1Ctl.FactorM + 1));
        }
        case AW_SYS_CLK_PLL2: {
            __u32   tmpReg;

            /*  FactorN=79, PreDiv=21, PostDiv=4, output=24*79/21/4=22.571mhz, 44.1k series fs
                FactorN=86, PreDiv=21, PostDiv=4, output=24*86/21/4=24.571mhz, 48k series fs */
            tmpReg = *(volatile __u32 *)&aw_ccu_reg->Pll2Ctl;
            if (((tmpReg >> 8) & 0x7f) == 79) {
                return 22579200; /* 22.571mhz ~= 22579200, 22579200*2/1024=44100 */
            } else if (((tmpReg >> 8) & 0x7f) == 86) {
                return 24576000; /* 24.571mhz ~= 24576000, 24576000*2/1024=48000 */
            } else {
                /* set audio pll to default value 24576000 */
                tmpReg &= ~((0x1f << 0) | (0x7f << 8) | (0x0f << 26));
                tmpReg |= (21 << 0) | (86 << 8) | (4 << 26);
                *(volatile __u32 *)&aw_ccu_reg->Pll2Ctl = tmpReg;
                return 24576000;
            }
        }
        case AW_SYS_CLK_PLL2X8: {
            if (sys_clk_get_rate(AW_SYS_CLK_PLL2) == 24576000) {
                return 24576000 * 18; /* why not 24576000 * 8? */
            } else {
                return 22579200 * 20; /* why not 22579200 * 8? */
            }
        }
        case AW_SYS_CLK_PLL3: {
            __s64   tmp_rate;

            if (!aw_ccu_reg->Pll3Ctl.ModeSel) {
                if (aw_ccu_reg->Pll3Ctl.FracSet) {
                    return 297000000;
                } else {
                    return 270000000;
                }
            } else {
                tmp_rate = 3000000 * aw_ccu_reg->Pll3Ctl.FactorM;
                /* skip 270M and 297M */
                if (tmp_rate == 270000000) {
                    return 273000000;
                } else if (tmp_rate == 297000000) {
                    return 300000000;
                }

                return tmp_rate;
            }
        }
        case AW_SYS_CLK_PLL3X2: {
            return sys_clk_get_rate(AW_SYS_CLK_PLL3) << 1;
        }

        case AW_SYS_CLK_PLL4: {
            return (__s64)24000000 * aw_ccu_reg->Pll4Ctl.FactorN * (aw_ccu_reg->Pll4Ctl.FactorK + 1);
        }
        case AW_SYS_CLK_PLL5: {
            return (__s64)24000000 * aw_ccu_reg->Pll5Ctl.FactorN * (aw_ccu_reg->Pll5Ctl.FactorK + 1);
        }
        case AW_SYS_CLK_PLL5M: {
            return ccu_clk_uldiv(sys_clk_get_rate(AW_SYS_CLK_PLL5), (aw_ccu_reg->Pll5Ctl.FactorM + 1));
        }
        case AW_SYS_CLK_PLL5P: {
            return sys_clk_get_rate(AW_SYS_CLK_PLL5) >> aw_ccu_reg->Pll5Ctl.FactorP;
        }
        case AW_SYS_CLK_PLL6: {
            return (__s64)24000000 * aw_ccu_reg->Pll6Ctl.FactorN * (aw_ccu_reg->Pll6Ctl.FactorK + 1) >> 1;
        }
        case AW_SYS_CLK_PLL6M: {
            return ccu_clk_uldiv((__s64)24000000 * aw_ccu_reg->Pll6Ctl.FactorN * (aw_ccu_reg->Pll6Ctl.FactorK + 1),   \
                                 (aw_ccu_reg->Pll6Ctl.FactorM + 1) * 6);
        }
        case AW_SYS_CLK_PLL62: {
            return (__s64)24000000 * aw_ccu_reg->Pll6Ctl.FactorN * (aw_ccu_reg->Pll6Ctl.FactorK + 1) >> 2;
        }
        case AW_SYS_CLK_PLL6X2: {
            return (__s64)24000000 * aw_ccu_reg->Pll6Ctl.FactorN * (aw_ccu_reg->Pll6Ctl.FactorK + 1);
        }
        case AW_SYS_CLK_PLL7: {
            if (!aw_ccu_reg->Pll7Ctl.ModeSel) {
                if (aw_ccu_reg->Pll7Ctl.FracSet) {
                    return 297000000;
                } else {
                    return 270000000;
                }
            } else {
                return (__s64)3000000 * aw_ccu_reg->Pll7Ctl.FactorM;
            }
        }
        case AW_SYS_CLK_PLL7X2: {
            return sys_clk_get_rate(AW_SYS_CLK_PLL7) << 1;
        }
        case AW_SYS_CLK_PLL8: {
            return (__s64)24000000 * aw_ccu_reg->Pll8Ctl.FactorN * (aw_ccu_reg->Pll8Ctl.FactorK + 1);
        }
        case AW_SYS_CLK_CPU: {
            __u32   tmpCpuRate;
            switch (aw_ccu_reg->SysClkDiv.AC327ClkSrc) {
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
        case AW_SYS_CLK_AXI: {
            return ccu_clk_uldiv(sys_clk_get_rate(AW_SYS_CLK_CPU), (aw_ccu_reg->SysClkDiv.AXIClkDiv + 1));
        }
        case AW_SYS_CLK_ATB: {
            __u32   div;
            switch (aw_ccu_reg->SysClkDiv.AtbApbClkDiv) {
                case 0:
                    div = 1;
                    break;
                case 1:
                    div = 2;
                    break;
                default:
                    div = 4;
            }
            return ccu_clk_uldiv(sys_clk_get_rate(AW_SYS_CLK_CPU), div);
        }
        case AW_SYS_CLK_AHB: {
            return sys_clk_get_rate(sys_clk_get_parent(AW_SYS_CLK_AHB)) >> aw_ccu_reg->SysClkDiv.AHBClkDiv;
        }
        case AW_SYS_CLK_APB0: {
            __s32   tmpShift = aw_ccu_reg->SysClkDiv.APB0ClkDiv;

            return sys_clk_get_rate(AW_SYS_CLK_AHB) >> (tmpShift ? tmpShift : 1);
        }
        case AW_SYS_CLK_APB1: {
            __s64   tmpApb1Rate;
            switch (aw_ccu_reg->Apb1ClkDiv.ClkSrc) {
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
        default: {
            return 0;
        }
    }
}

/*
 * Set parent clock id for system clock.
 *
 * @id:     system clock id whose parent need to be set.
 * @parent: parent id.
 *
 */
static __s32 sys_clk_set_parent(__aw_ccu_clk_id_e id, __aw_ccu_clk_id_e parent)
{
    switch (id) {
        case AW_SYS_CLK_PLL2X8:
            return (parent == AW_SYS_CLK_PLL2) ? 0 : -1;

        case AW_SYS_CLK_PLL3X2:
            return (parent == AW_SYS_CLK_PLL3) ? 0 : -1;

        case AW_SYS_CLK_PLL5M:
        case AW_SYS_CLK_PLL5P:
            return (parent == AW_SYS_CLK_PLL5) ? 0 : -1;

        case AW_SYS_CLK_PLL6M:
        case AW_SYS_CLK_PLL62:
            return (parent == AW_SYS_CLK_PLL6) ? 0 : -1;

        case AW_SYS_CLK_PLL7X2:
            return (parent == AW_SYS_CLK_PLL7) ? 0 : -1;

        case AW_SYS_CLK_CPU: {
            switch (parent) {
                case AW_SYS_CLK_LOSC:
                    aw_ccu_reg->SysClkDiv.AC327ClkSrc = 0;
                    break;
                case AW_SYS_CLK_HOSC:
                    aw_ccu_reg->SysClkDiv.AC327ClkSrc = 1;
                    break;
                case AW_SYS_CLK_PLL1:
                    aw_ccu_reg->SysClkDiv.AC327ClkSrc = 2;
                    break;
                default:
                    return -1;
            }
            return 0;
        }
        case AW_SYS_CLK_AXI:
            return (parent == AW_SYS_CLK_CPU) ? 0 : -1;
        case AW_SYS_CLK_ATB:
            return (parent == AW_SYS_CLK_CPU) ? 0 : -1;
        case AW_SYS_CLK_AHB: {
            switch (parent) {
                case AW_SYS_CLK_AXI:
                    aw_ccu_reg->SysClkDiv.AHBClkSrc = 0;
                    break;
                case AW_SYS_CLK_PLL62:
                    aw_ccu_reg->SysClkDiv.AHBClkSrc = 1;
                    break;
                case AW_SYS_CLK_PLL6:
                    aw_ccu_reg->SysClkDiv.AHBClkSrc = 2;
                    break;
                default:
                    return -1;
            }
            return 0;
        }
        case AW_SYS_CLK_APB0:
            return (parent == AW_SYS_CLK_AHB) ? 0 : -1;
        case AW_SYS_CLK_APB1: {
            switch (parent) {
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
        case AW_SYS_CLK_PLL8: {
            return (parent == AW_SYS_CLK_NONE) ? 0 : -1;
        }

        default: {
            return -1;
        }
    }
}

/*
 * Set on/off status for system clock.
 *
 * @id:     System clock id.
 * @status: AW_CCU_CLK_OFF/AW_CCU_CLK_ON.
 *
 */
static __s32 sys_clk_set_status(__aw_ccu_clk_id_e id, __aw_ccu_clk_onff_e status)
{
    switch (id) {
        case AW_SYS_CLK_LOSC:
            return 0;
        case AW_SYS_CLK_HOSC:
            aw_ccu_reg->HoscCtl.OSC24MEn = (status == AW_CCU_CLK_ON) ? 1 : 0;
            return 0;
        case AW_SYS_CLK_PLL1:
            aw_ccu_reg->Pll1Ctl.PLLEn = (status == AW_CCU_CLK_ON) ? 1 : 0;
            return 0;
        case AW_SYS_CLK_PLL2:
            aw_ccu_reg->Pll2Ctl.PLLEn = (status == AW_CCU_CLK_ON) ? 1 : 0;
            return 0;
        case AW_SYS_CLK_PLL2X8: {
            if (status && !aw_ccu_reg->Pll2Ctl.PLLEn) {
                return -1;
            } else {
                return 0;
            }
        }
        case AW_SYS_CLK_PLL3:
            aw_ccu_reg->Pll3Ctl.PLLEn = (status == AW_CCU_CLK_ON) ? 1 : 0;
            return 0;
        case AW_SYS_CLK_PLL3X2: {
            if (status && !aw_ccu_reg->Pll3Ctl.PLLEn) {
                return -1;
            } else {
                return 0;
            }
        }
        case AW_SYS_CLK_PLL4:
            aw_ccu_reg->Pll4Ctl.PLLEn = (status == AW_CCU_CLK_ON) ? 1 : 0;
            return 0;
        case AW_SYS_CLK_PLL5:
            aw_ccu_reg->Pll5Ctl.PLLEn = (status == AW_CCU_CLK_ON) ? 1 : 0;
            return 0;
        case AW_SYS_CLK_PLL5M:
        case AW_SYS_CLK_PLL5P: {
            if (status && !aw_ccu_reg->Pll5Ctl.PLLEn) {
                return -1;
            } else {
                return 0;
            }
        }
        case AW_SYS_CLK_PLL6:
            aw_ccu_reg->Pll6Ctl.PLLEn = (status == AW_CCU_CLK_ON) ? 1 : 0;
            return 0;
        case AW_SYS_CLK_PLL6M:
        case AW_SYS_CLK_PLL62: {
            if (status && !aw_ccu_reg->Pll6Ctl.PLLEn) {
                return -1;
            } else {
                return 0;
            }
        }
        case AW_SYS_CLK_PLL7:
            aw_ccu_reg->Pll7Ctl.PLLEn = (status == AW_CCU_CLK_ON) ? 1 : 0;
            return 0;
        case AW_SYS_CLK_PLL7X2: {
            if (status && !aw_ccu_reg->Pll7Ctl.PLLEn) {
                return -1;
            } else {
                return 0;
            }
        }
        case AW_SYS_CLK_PLL8:
            aw_ccu_reg->Pll8Ctl.PLLEn = (status == AW_CCU_CLK_ON) ? 1 : 0;
            return 0;
        case AW_SYS_CLK_CPU:
        case AW_SYS_CLK_AXI:
        case AW_SYS_CLK_ATB:
        case AW_SYS_CLK_AHB:
        case AW_SYS_CLK_APB0:
        case AW_SYS_CLK_APB1:
            return 0;

        default: {
            return -1;
        }
    }
}

/*
 * Set clock rate for system clock.
 *
 * @id:     system clock id
 * @rate:   clock rate for system clock
 *
 */
static int sys_clk_set_rate(__aw_ccu_clk_id_e id, __u64 rate)
{
    switch (id) {
        case AW_SYS_CLK_LOSC:
            return (rate == 32768) ? 0 : -1;

        case AW_SYS_CLK_HOSC:
            return (rate == 24000000) ? 0 : -1;
        case AW_SYS_CLK_PLL1: {
            struct core_pll_factor_t    factor;
            __ccmu_pll1_core_reg0000_t  tmp_pll;

            /* the setting of pll1 must be called by cpu-freq driver, and adjust pll step by step */
            ccm_clk_get_pll_para(&factor, rate);
            /* set factor */
            tmp_pll = aw_ccu_reg->Pll1Ctl;
            if (tmp_pll.PLLDivP < factor.FactorP) {
                tmp_pll.PLLDivP = factor.FactorP;
                aw_ccu_reg->Pll1Ctl = tmp_pll;
                __delay(rate >> 20);

                tmp_pll.FactorN = factor.FactorN;
                tmp_pll.FactorK = factor.FactorK;
                tmp_pll.FactorM = factor.FactorM;
                aw_ccu_reg->Pll1Ctl = tmp_pll;
                /* delay 500us for pll be stably */
                __delay((rate >> 20) * 500);
            } else if (tmp_pll.PLLDivP == factor.FactorP) {
                tmp_pll.FactorN = factor.FactorN;
                tmp_pll.FactorK = factor.FactorK;
                tmp_pll.FactorM = factor.FactorM;
                tmp_pll.PLLDivP = factor.FactorP;
                aw_ccu_reg->Pll1Ctl = tmp_pll;
                /* delay 500us for pll be stably */
                __delay((rate >> 20) * 500);
            } else {
                tmp_pll.FactorN = factor.FactorN;
                tmp_pll.FactorK = factor.FactorK;
                tmp_pll.FactorM = factor.FactorM;
                aw_ccu_reg->Pll1Ctl = tmp_pll;
                /* delay 500us for pll be stably */
                __delay((rate >> 20) * 500);

                tmp_pll.PLLDivP = factor.FactorP;
                aw_ccu_reg->Pll1Ctl = tmp_pll;
                __delay(rate >> 20);
            }

            return 0;
        }
        case AW_SYS_CLK_PLL2: {
            if (rate == 22579200) {
                /* FactorN=79, PreDiv=21, PostDiv=4,
                   output=24*79/21/4=22.571mhz, 44.1k series fs */
                __u32   tmpReg;

                tmpReg = *(volatile __u32 *)&aw_ccu_reg->Pll2Ctl;
                tmpReg &= ~((0x1f << 0) | (0x7f << 8) | (0x0f << 26));
                tmpReg |= (21 << 0) | (79 << 8) | (4 << 26);
                *(volatile __u32 *)&aw_ccu_reg->Pll2Ctl = tmpReg;
            } else if (rate == 24576000) {
                /* FactorN=86, PreDiv=21, PostDiv=4,
                   output=24*86/21/4=24.571mhz, 48k series fs   */
                __u32   tmpReg;

                tmpReg = *(volatile __u32 *)&aw_ccu_reg->Pll2Ctl;
                tmpReg &= ~((0x1f << 0) | (0x7f << 8) | (0x0f << 26));
                tmpReg |= (21 << 0) | (86 << 8) | (4 << 26);
                *(volatile __u32 *)&aw_ccu_reg->Pll2Ctl = tmpReg;
            } else {
                return -1;
            }
            return 0;
        }
        case AW_SYS_CLK_PLL2X8: {
            if ((sys_clk_get_rate(AW_SYS_CLK_PLL2) == 24576000) && (rate == 24576000 * 18)) {
                return 0;
            } else if ((sys_clk_get_rate(AW_SYS_CLK_PLL2) == 22579200) && (rate == 24576000 * 20)) {
                return 0;
            }

            return -1;
        }
        case AW_SYS_CLK_PLL3: {
            if ((rate < 9 * 3000000) || (rate > (127 * 3000000))) {
                CCU_ERR("rate (%llu) is invalid when set PLL3 rate\n", rate);
                return -1;
            }

            if (rate == 270000000) {
                aw_ccu_reg->Pll3Ctl.ModeSel = 0;
                aw_ccu_reg->Pll3Ctl.FracSet = 0;
            } else if (rate == 297000000) {
                aw_ccu_reg->Pll3Ctl.ModeSel = 0;
                aw_ccu_reg->Pll3Ctl.FracSet = 1;
            } else {
                aw_ccu_reg->Pll3Ctl.ModeSel = 1;
                aw_ccu_reg->Pll3Ctl.FactorM = ccu_clk_uldiv(rate, 3000000);
            }
            return 0;
        }
        case AW_SYS_CLK_PLL3X2: {
            if (rate == (sys_clk_get_rate(AW_SYS_CLK_PLL3) << 1)) {
                return 0;
            }
            return -1;
        }
        case AW_SYS_CLK_PLL4: {
            __s32   tmpFactorN, tmpFactorK;

            if (rate <= 600000000)
                tmpFactorK = 0;
            else if (rate <= 1200000000)
                tmpFactorK = 1;
            else {
                CCU_ERR("rate (%llu) is invaid for PLL4\n", rate);
                return -1;
            }

            tmpFactorN = ccu_clk_uldiv(rate, ((tmpFactorK + 1) * 24000000));
            if (tmpFactorN > 31) {
                CCU_ERR("rate (%llu) is invaid for PLL4\n", rate);
                return -1;
            }

            aw_ccu_reg->Pll4Ctl.FactorN = tmpFactorN;
            aw_ccu_reg->Pll4Ctl.FactorK = tmpFactorK;

            return 0;
        }
        case AW_SYS_CLK_PLL5: {
            __s32   tmpFactorN, tmpFactorK;

            if (rate < 240000000) {
                CCU_ERR("rate (%llu) is invalid when set PLL5 rate\n", rate);
                return -1;
            }

            if (rate < 480000000) {
                tmpFactorK = 0;
            } else if (rate < 960000000) {
                tmpFactorK = 1;
            } else if (rate < 2000000000) {
                tmpFactorK = 3;
            } else {
                CCU_ERR("rate (%llu) is invaid for PLL5\n", rate);
                return -1;
            }

            tmpFactorN = ccu_clk_uldiv(rate, ((tmpFactorK + 1) * 24000000));
            if (tmpFactorN > 31) {
                CCU_ERR("rate (%llu) is invaid for PLL5\n", rate);
                return -1;
            }

            aw_ccu_reg->Pll5Ctl.FactorN = tmpFactorN;
            aw_ccu_reg->Pll5Ctl.FactorK = tmpFactorK;

            return 0;
        }
        case AW_SYS_CLK_PLL5M: {
            __u32   tmpFactorM;
            __s64   tmpPLL5;

            tmpPLL5 = sys_clk_get_rate(AW_SYS_CLK_PLL5);
            if ((rate > tmpPLL5) || (tmpPLL5 > rate * 4)) {
                CCU_ERR("PLL5 (%llu) rate is invalid for PLL5M(%lld)!\n", tmpPLL5, rate);
                return -1;
            }

            tmpFactorM = ccu_clk_uldiv(tmpPLL5, rate);
            aw_ccu_reg->Pll5Ctl.FactorM = tmpFactorM - 1;

            return 0;
        }
        case AW_SYS_CLK_PLL5P: {
            __s32   tmpFactorP = -1;
            __s64   tmpPLL5 = sys_clk_get_rate(AW_SYS_CLK_PLL5);

            if ((rate > tmpPLL5) || (tmpPLL5 > rate * 8)) {
                CCU_ERR("PLL5 (%llu) rate is invalid for PLL5P(%lld)!\n", tmpPLL5, rate);
                return -1;
            }

            do {
                rate <<= 1;
                tmpFactorP++;
            } while (rate < tmpPLL5);
            aw_ccu_reg->Pll5Ctl.FactorP = tmpFactorP;

            return 0;
        }
        case AW_SYS_CLK_PLL6: {
            __s32   tmpFactorN, tmpFactorK;

            if (rate <= 600000000)
                tmpFactorK = 1;
            else if (rate <= 1200000000)
                tmpFactorK = 2;
            else {
                CCU_ERR("rate (%llu) is invaid for PLL6\n", rate);
                return -1;
            }

            tmpFactorN = ccu_clk_uldiv(rate, ((tmpFactorK + 1) * 24000000) >> 1);
            if (tmpFactorN > 31) {
                CCU_ERR("rate (%llu) is invaid for PLL6\n", rate);
                return -1;
            }

            aw_ccu_reg->Pll6Ctl.FactorN = tmpFactorN;
            aw_ccu_reg->Pll6Ctl.FactorK = tmpFactorK;

            return 0;
        }
        case AW_SYS_CLK_PLL6M: {
            __s64   tmpPLL6 = sys_clk_get_rate(AW_SYS_CLK_PLL6);
            __s32   tmpFactorM = ccu_clk_uldiv(tmpPLL6, rate * 6);

            tmpFactorM = tmpFactorM ? tmpFactorM : 1;
            aw_ccu_reg->Pll6Ctl.FactorM = tmpFactorM - 1;
            return 0;
        }
        case AW_SYS_CLK_PLL62: {
            return 0;
        }
        case AW_SYS_CLK_PLL6X2: {
            return 0;
        }
        case AW_SYS_CLK_PLL7: {
            if ((rate < 9 * 3000000) || (rate > (127 * 3000000))) {
                CCU_ERR("rate (%llu) is invalid when set PLL7 rate\n", rate);
                return -1;
            }

            if (rate == 270000000) {
                aw_ccu_reg->Pll7Ctl.ModeSel = 0;
                aw_ccu_reg->Pll7Ctl.FracSet = 0;
            } else if (rate == 297000000) {
                aw_ccu_reg->Pll7Ctl.ModeSel = 0;
                aw_ccu_reg->Pll7Ctl.FracSet = 1;
            } else {
                aw_ccu_reg->Pll7Ctl.ModeSel = 1;
                aw_ccu_reg->Pll7Ctl.FactorM = ccu_clk_uldiv(rate, 3000000);
            }
            return 0;
        }
        case AW_SYS_CLK_PLL7X2: {
            if (rate == (sys_clk_get_rate(AW_SYS_CLK_PLL7) << 1)) {
                return 0;
            }
            return -1;
        }
        case AW_SYS_CLK_PLL8: {
            __s32   tmpFactorN, tmpFactorK;

            if (rate <= 600000000)
                tmpFactorK = 0;
            else if (rate <= 1200000000)
                tmpFactorK = 1;
            else {
                CCU_ERR("rate (%llu) is invaid for PLL8\n", rate);
                return -1;
            }

            tmpFactorN = ccu_clk_uldiv(rate, ((tmpFactorK + 1) * 24000000));
            if (tmpFactorN > 31) {
                CCU_ERR("rate (%llu) is invaid for PLL8\n", rate);
                return -1;
            }

            aw_ccu_reg->Pll8Ctl.FactorN = tmpFactorN;
            aw_ccu_reg->Pll8Ctl.FactorK = tmpFactorK;

            return 0;
        }
        case AW_SYS_CLK_CPU: {
            __s64   tmpRate = sys_clk_get_rate(sys_clk_get_parent(AW_SYS_CLK_CPU));

            if (rate != tmpRate) {
                CCU_ERR("rate (%llu) is invalid when set cpu rate (%lld)\n", rate, tmpRate);
                CCU_ERR("0xf1c20000 = 0x%x\n", *(volatile __u32 *)0xf1c20000);
                return -1;
            } else {
                return 0;
            }
        }
        case AW_SYS_CLK_AXI: {
            __s32   tmpDiv = ccu_clk_uldiv(sys_clk_get_rate(AW_SYS_CLK_CPU), rate);
            if (tmpDiv > 4) {
                CCU_ERR("rate (%llu) is invalid when set axi rate\n", rate);
                return -1;
            }
            tmpDiv = tmpDiv ? (tmpDiv - 1) : 0;
            aw_ccu_reg->SysClkDiv.AXIClkDiv = tmpDiv;

            return 0;
        }
        case AW_SYS_CLK_ATB: {
            __s32   tmpDiv = ccu_clk_uldiv(sys_clk_get_rate(AW_SYS_CLK_CPU), rate);
            if (tmpDiv > 4) {
                CCU_ERR("rate (%llu) is invalid when set atb rate\n", rate);
                return -1;
            } else if ((tmpDiv == 4) || (tmpDiv == 3)) {
                aw_ccu_reg->SysClkDiv.AtbApbClkDiv = 2;
            } else if (tmpDiv == 2) {
                aw_ccu_reg->SysClkDiv.AtbApbClkDiv = 1;
            } else {
                aw_ccu_reg->SysClkDiv.AtbApbClkDiv = 0;
            }

            return 0;
        }
        case AW_SYS_CLK_AHB: {
            __s32   tmpVal = -1, tmpDiv = ccu_clk_uldiv(sys_clk_get_rate(sys_clk_get_parent(AW_SYS_CLK_AHB)), rate);

            if (tmpDiv > 8) {
                CCU_ERR("rate (%llu) is invalid for set AHB rate\n", rate);
                return -1;
            }

            do {
                tmpDiv >>= 1;
                tmpVal++;
            } while (tmpDiv);
            aw_ccu_reg->SysClkDiv.AHBClkDiv = tmpVal;

            return 0;
        }
        case AW_SYS_CLK_APB0: {
            __s32   tmpVal = -1, tmpDiv = ccu_clk_uldiv(sys_clk_get_rate(AW_SYS_CLK_AHB), rate);

            if (tmpDiv > 8) {
                CCU_ERR("rate (%llu) is invalid for set APB0 rate\n", rate);
                return -1;
            }

            do {
                tmpDiv >>= 1;
                tmpVal++;
            } while (tmpDiv);
            aw_ccu_reg->SysClkDiv.APB0ClkDiv = tmpVal;

            return 0;
        }
        case AW_SYS_CLK_APB1: {
            __s64   tmpRate = sys_clk_get_rate(sys_clk_get_parent(AW_SYS_CLK_APB1));
            __s32   tmpDivP, tmpDivM;

            if (tmpRate < rate) {
                CCU_ERR("rate (%llu) is invalid for set APB1 rate, parent is (%llu)\n", rate, tmpRate);
                return -1;
            }

            tmpRate = ccu_clk_uldiv(tmpRate, rate);
            if (tmpRate <= 4) {
                tmpDivP = 0;
                tmpDivM = tmpRate - 1;
            } else if (tmpRate <= 8) {
                tmpDivP = 1;
                tmpDivM = (tmpRate >> 1) - 1;
            } else if (tmpRate <= 16) {
                tmpDivP = 2;
                tmpDivM = (tmpRate >> 2) - 1;
            } else if (tmpRate <= 32) {
                tmpDivP = 3;
                tmpDivM = (tmpRate >> 3) - 1;
            } else {
                CCU_ERR("rate (%llu) is invalid for set APB1 rate\n", rate);
                return -1;
            }

            aw_ccu_reg->Apb1ClkDiv.PreDiv = tmpDivP;
            aw_ccu_reg->Apb1ClkDiv.ClkDiv = tmpDivM;

            return 0;
        }
        default: {
            return -1;
        }
    }
}

__clk_ops_t sys_clk_ops = {
    .set_status = sys_clk_set_status,
    .get_status = sys_clk_get_status,
    .set_parent = sys_clk_set_parent,
    .get_parent = sys_clk_get_parent,
    .get_rate = sys_clk_get_rate,
    .set_rate = sys_clk_set_rate,
    .round_rate = 0,
    .set_reset  = 0,
    .get_reset  = 0,
};
