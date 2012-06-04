/*
 * arch/arm/mach-sun3i/clock/csp_ccmu/ccmu/ccm_sys_clk_status.c
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
#include "spec_ccm.h"

static struct{
    unsigned _1xIsOn        : 1;
    unsigned _4xIsOn        : 1;
    unsigned _8xIsOn        : 1;
    unsigned unUsedBits     : 29;
}_audioPllStatusFlag = {0};

static struct{
    unsigned _1xIsOn        : 1;
    unsigned _2xIsOn        : 1;
    unsigned unUsedBits     : 30;
}_videoPll0StatusFlag = {0}, _videoPll1StatusFlag = {0};
s32     CSP_CCM_set_sys_clk_status(CSP_CCM_sysClkNo_t sysClkNo, u8 onOrOff)
{
    if (sysClkNo >= CSP_CCM_SYS_CLK_TOTAL_NUM){
        return AW_CCMU_FAIL;
    }
    onOrOff &= 0x1;//mod 2

    switch (sysClkNo)
    {
    case CSP_CCM_SYS_CLK_HOSC:
        onOrOff ? HOSC_ENABEL : HOSC_DISABLE;
        break;

    case CSP_CCM_SYS_CLK_CORE_PLL:
        onOrOff ? CORE_PLL_ENABLE : CORE_PLL_DISABLE;
        break;

    case CSP_CCM_SYS_CLK_VE_PLL:
        onOrOff ? VE_PLL_ENABLE : VE_PLL_DISABLE;
        break;

    case CSP_CCM_SYS_CLK_AUDIO_PLL:
        _audioPllStatusFlag._1xIsOn = onOrOff;
        _CONDITION_BIT_SET(*((unsigned*)&_audioPllStatusFlag), CCM_AUDIO_HOSC_PLL_R, AUDIO_PLL_EN_BIT);
        break;
    case CSP_CCM_SYS_CLK_AUDIO_PLL_4X://///
        _audioPllStatusFlag._4xIsOn = onOrOff;
        _CONDITION_BIT_SET(*((unsigned*)&_audioPllStatusFlag), CCM_AUDIO_HOSC_PLL_R, AUDIO_PLL_EN_BIT);
        break;
    case CSP_CCM_SYS_CLK_AUDIO_PLL_8X://///
        _audioPllStatusFlag._8xIsOn = onOrOff;
        _CONDITION_BIT_SET(*((unsigned*)&_audioPllStatusFlag), CCM_AUDIO_HOSC_PLL_R, AUDIO_PLL_EN_BIT);
        break;

    case CSP_CCM_SYS_CLK_SDRAM_PLL:
        onOrOff ? SET_BIT(CCM_SDRAM_PLL_R, 11) : CLEAR_BIT(CCM_SDRAM_PLL_R, 11);
        break;

    case CSP_CCM_SYS_CLK_VIDEO_PLL0:
        _videoPll0StatusFlag._1xIsOn = onOrOff;
        _CONDITION_BIT_SET(*((unsigned*)&_videoPll0StatusFlag), CCM_VIDEO_PLL_R, VIDEO_PLL0_EN_BIT);
        break;
    case CSP_CCM_SYS_CLK_VIDEO_PLL0_2X:
        _videoPll0StatusFlag._2xIsOn = onOrOff;
        _CONDITION_BIT_SET(*((unsigned*)&_videoPll0StatusFlag), CCM_VIDEO_PLL_R, VIDEO_PLL0_EN_BIT);
        break;

    case CSP_CCM_SYS_CLK_VIDEO_PLL1:
        _videoPll1StatusFlag._1xIsOn = onOrOff;
        _CONDITION_BIT_SET(*((unsigned*)&_videoPll1StatusFlag), CCM_VIDEO_PLL_R, VIDEO_PLL1_EN_BIT);
        break;
    case CSP_CCM_SYS_CLK_VIDEO_PLL1_2X:
        _videoPll1StatusFlag._2xIsOn = onOrOff;
        _CONDITION_BIT_SET(*((unsigned*)&_videoPll1StatusFlag), CCM_VIDEO_PLL_R, VIDEO_PLL1_EN_BIT);
        break;

    case CSP_CCM_SYS_CLK_CPU:
    case CSP_CCM_SYS_CLK_AHB:
    case CSP_CCM_SYS_CLK_SDRAM:
    case CSP_CCM_SYS_CLK_APB:
        break;

    case CSP_CCM_SYS_CLK_TVENC_0:
    case CSP_CCM_SYS_CLK_TVENC_1:
        break;

    default:
        CCM_msg("Exception: unsolved clock[%d].\n", sysClkNo);
        return AW_CCMU_FAIL;
    }

    return AW_CCMU_OK;
}

s32  CSP_CCM_get_sys_clk_status(CSP_CCM_sysClkNo_t sysClkNo)
{
    switch (sysClkNo)
    {
    case CSP_CCM_SYS_CLK_HOSC:
        return HOSC_IS_ABLE;

    case CSP_CCM_SYS_CLK_VE_PLL:
        return VE_PLL_IS_ABLE;

    case CSP_CCM_SYS_CLK_AUDIO_PLL:
    case CSP_CCM_SYS_CLK_AUDIO_PLL_4X://///
    case CSP_CCM_SYS_CLK_AUDIO_PLL_8X://///
        return AUDIO_PLL_IS_ABLE;

    case CSP_CCM_SYS_CLK_SDRAM_PLL:
        return TEST_BIT(CCM_SDRAM_PLL_R, 11);

    case CSP_CCM_SYS_CLK_VIDEO_PLL0:
    case CSP_CCM_SYS_CLK_VIDEO_PLL0_2X:
        return TEST_BIT(CCM_VIDEO_PLL_R, 7);

    case CSP_CCM_SYS_CLK_VIDEO_PLL1:
    case CSP_CCM_SYS_CLK_VIDEO_PLL1_2X:
        return TEST_BIT(CCM_VIDEO_PLL_R, 23);

    case CSP_CCM_SYS_CLK_TVENC_0:
    case CSP_CCM_SYS_CLK_TVENC_1:
        return 1;//clock is on

    default:
        CCM_msg("Exception: get status of clock[%d] failed.\n", sysClkNo);
        return 0;
    }
}



