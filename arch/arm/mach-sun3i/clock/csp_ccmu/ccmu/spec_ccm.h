/*
 * arch/arm/mach-sun3i/clock/csp_ccmu/ccmu/spec_ccm.h
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

#ifndef _SPEC_CSP_CCMU_H_
#define _SPEC_CSP_CCMU_H_

#include "ccm_i.h"
#include "my_bits_ops.h"


#define CCM_REG_OFFSET(n)              ( 0X00 + (n)*4U )

#define CCM_O_CORE_VE_PLL_R             CCM_REG_OFFSET(0)
#define CCM_O_AUDIO_HOSC_PLL_R          CCM_REG_OFFSET(1)
#define CCM_O_AHB_APB_CFG_R             CCM_REG_OFFSET(2)
#define CCM_O_AHB_GATE_R                CCM_REG_OFFSET(3)
#define CCM_O_APB_GATE_R                CCM_REG_OFFSET(4)
#define CCM_O_NFC_MSC_CLK_R             CCM_REG_OFFSET(5)
#define CCM_O_SDC01_CLK_R               CCM_REG_OFFSET(6)
#define CCM_O_SDC23_CLK_R               CCM_REG_OFFSET(7)
#define CCM_O_SDRAM_PLL_R               CCM_REG_OFFSET(8)
#define CCM_O_DE_CLK_R                  CCM_REG_OFFSET(9)
#define CCM_O_VE_CLK_R                  CCM_REG_OFFSET(10)
#define CCM_O_CSI_CLK_R                 CCM_REG_OFFSET(11)
#define CCM_O_VIDEO_PLL_R               CCM_REG_OFFSET(12)
#define CCM_O_IR_CLK_R                  CCM_REG_OFFSET(13)
#define CCM_O_AUDIO_CLK_R               CCM_REG_OFFSET(14)
#define CCM_O_TS_SS_CLK_R               CCM_REG_OFFSET(15)
#define CCM_O_AVS_USB_CLK_R             CCM_REG_OFFSET(16)
#define CCM_O_ATA_CLK_R                 CCM_REG_OFFSET(17)
#define CCM_O_DE_MIX_CLK_R              CCM_REG_OFFSET(18)
#define CCM_O_MISC_CLK_R                CCM_REG_OFFSET(19)
#define CCM_O_CORE_PLL_DBG_R            CCM_REG_OFFSET(20)
#define CCM_O_TVENC_CLK_R               CCM_REG_OFFSET(21)
#define CCM_O_TCON_CLK_R                CCM_REG_OFFSET(22)

#define CCM_O_BSP_STA_R                 0X60
#define CCM_O_SWP_R                     0XD4
#define CCM_O_PWN_CTL_R                 0XE0
#define CCM_O_PWN_CH0_SETUP_R           0XE4
#define CCM_O_PWN_CH1_SETUP_R           0XE8

//typedef volatile unsigned int *   RegAddr_t ;
typedef volatile unsigned int*   RegAddr_t ;
#define CCM_REG(_ofst)                ( *(RegAddr_t)( (_ofst) + CCM_MOD_VIR_BASE ) )

#define CCM_CORE_VE_PLL_R             CCM_REG(CCM_O_CORE_VE_PLL_R)
#define CCM_AUDIO_HOSC_PLL_R          CCM_REG(CCM_O_AUDIO_HOSC_PLL_R)
#define CCM_AHB_APB_RATIO_CFG_R       CCM_REG(CCM_O_AHB_APB_CFG_R)
#define CCM_AHB_GATE_R                CCM_REG(CCM_O_AHB_GATE_R)
#define CCM_APB_GATE_R                CCM_REG(CCM_O_APB_GATE_R)
#define CCM_NFC_MSC_CLK_R             CCM_REG(CCM_O_NFC_MSC_CLK_R)
#define CCM_SDC01_CLK_R               CCM_REG(CCM_O_SDC01_CLK_R)
#define CCM_SDC23_CLK_R               CCM_REG(CCM_O_SDC23_CLK_R)
#define CCM_SDRAM_PLL_R               CCM_REG(CCM_O_SDRAM_PLL_R)
#define CCM_DE_CLK_R                  CCM_REG(CCM_O_DE_CLK_R)
#define CCM_VE_CLK_R                  CCM_REG(CCM_O_VE_CLK_R)
#define CCM_CSI_CLK_R                 CCM_REG(CCM_O_CSI_CLK_R)
#define CCM_VIDEO_PLL_R               CCM_REG(CCM_O_VIDEO_PLL_R)
#define CCM_IR_CLK_R                  CCM_REG(CCM_O_IR_CLK_R)
#define CCM_AUDIO_CLK_R               CCM_REG(CCM_O_AUDIO_CLK_R)
#define CCM_TS_SS_CLK_R               CCM_REG(CCM_O_TS_SS_CLK_R)
#define CCM_AVS_USB_CLK_R             CCM_REG(CCM_O_AVS_USB_CLK_R)
#define CCM_ATA_CLK_R                 CCM_REG(CCM_O_ATA_CLK_R)
#define CCM_DE_MIX_CLK_R              CCM_REG(CCM_O_DE_MIX_CLK_R)
#define CCM_MISC_CLK_R                CCM_REG(CCM_O_MISC_CLK_R)
#define CCM_CORE_PLL_DBG_R            CCM_REG(CCM_O_CORE_PLL_DBG_R)
#define CCM_TVENC_CLK_R               CCM_REG(CCM_O_TVENC_CLK_R)
#define CCM_TCON_CLK_R                CCM_REG(CCM_O_TCON_CLK_R)
#define CCM_BSP_STA_R                 CCM_REG(CCM_O_BSP_STA_R)
#define CCM_SYS_WAKEU_PENDING_R       CCM_REG(CCM_O_SWP_R)
#define CCM_PWN_CTL_R                 CCM_REG(CCM_O_PWN_CTL_R)
#define CCM_PWN_CH0_SETUP_R           CCM_REG(CCM_O_PWN_CH0_SETUP_R)
#define CCM_PWN_CH1_SETUP_R           CCM_REG(CCM_O_PWN_CH1_SETUP_R)

/************************************************************************/
/* Core and VE PLL                      */
/************************************************************************/
#define CORE_PLL_ENABLE                 SET_BIT(CCM_CORE_VE_PLL_R, 29)
#define CORE_PLL_DISABLE                CLEAR_BIT(CCM_CORE_VE_PLL_R, 29)
#define CORE_PLL_IS_ABLE                TEST_BIT(CCM_CORE_VE_PLL_R, 29)

#define CORE_PLL_SET_BIAS_CURRENT(_val)      SET_BITS(CCM_CORE_VE_PLL_R, 27, 2, (_val))
#define CORE_PLL_GET_BIAS_CURRENT       GET_BITS_VAL(CCM_CORE_VE_PLL_R, 27, 2)

#define CORE_PLL_OUTPUT_BYPASS_ENABLE       SET_BIT(CCM_CORE_VE_PLL_R, 26)
#define CORE_PLL_OUTPUT_BYPASS_DISABLE      CLEAR_BIT(CCM_CORE_VE_PLL_R, 26)
#define CORE_PLL_OUTPUT_BYPASS_IS_ABLE      TEST_BIT(CCM_CORE_VE_PLL_R, 26)

#define CORE_PLL_SET_FACTOR(_val)             SET_BITS(CCM_CORE_VE_PLL_R, 16, 7, (_val))
#define CORE_PLL_GET_FACTOR                   GET_BITS_VAL(CCM_CORE_VE_PLL_R, 16, 7)

#define VE_PLL_ENABLE                       SET_BIT(CCM_CORE_VE_PLL_R, 15)
#define VE_PLL_DISABLE                      CLEAR_BIT(CCM_CORE_VE_PLL_R, 15)
#define VE_PLL_IS_ABLE                      TEST_BIT(CCM_CORE_VE_PLL_R, 15)

#define VE_PLL_SET_BIAS_CURRENT(_val)       SET_BITS(CCM_CORE_VE_PLL_R, 13, 2, (_val))
#define VE_PLL_GET_BIAS_CURRENT             GET_BITS_VAL(CCM_CORE_VE_PLL_R, 13, 2)

#define VE_PLL_OUTPUT_BYPASS_ENABLE         SET_BIT(CCM_CORE_VE_PLL_R, 12)
#define VE_PLL_OUTPUT_BYPASS_DISABLE        CLEAR_BIT(CCM_CORE_VE_PLL_R, 12)
#define VE_PLL_OUTPUT_BYPASS_IS_ABLE        TEST_BIT(CCM_CORE_VE_PLL_R, 12)

#define VE_PLL_SET_FACTOR(_val)             SET_BITS(CCM_CORE_VE_PLL_R, 0, 7, (_val))
#define VE_PLL_GET_FACTOR                   GET_BITS_VAL(CCM_CORE_VE_PLL_R, 0, 7)

/************************************************************************/
/* Audio PLL and HOSC config Reg                      */
/************************************************************************/
#define AUDIO_PLL_IS_ABLE           TEST_BIT(CCM_AUDIO_HOSC_PLL_R, 29)
#define AUDIO_PLL_EN_BIT            (29U)
#define VIDEO_PLL0_EN_BIT           (7U)
#define VIDEO_PLL1_EN_BIT           (23U)

#define AUDIO_PLL_SET_BIAS_CURRENT(_val)    SET_BITS(CCM_AUDIO_HOSC_PLL_R, 27, 2, (_val))
#define AUDIO_PLL_GET_BIAS_CURRENT          GET_BITS_VAL(CCM_AUDIO_HOSC_PLL_R, 27, 2)

#define FREQ_AUDIO_PLL_HIGH  (24576*1000)
#define FREQ_AUDIO_PLL_LOW   (22579200)
#define AUDIO_PLL_FREQ_SEL_HIGH    SET_BIT(CCM_AUDIO_HOSC_PLL_R, 26)
#define AUDIO_PLL_FREQ_SEL_LOW     CLEAR_BIT(CCM_AUDIO_HOSC_PLL_R, 26)
#define AUDIO_PLL_FREQ_IS_HIGH     TEST_BIT(CCM_AUDIO_HOSC_PLL_R, 26)

#define LDO_ENABLE          SET_BIT(CCM_AUDIO_HOSC_PLL_R, 15)
#define LDO_DISABLE         CLEAR_BIT(CCM_AUDIO_HOSC_PLL_R, 15)
#define LDO_IS_ABLE         TEST_BIT(CCM_AUDIO_HOSC_PLL_R, 15)

#define HOSC_ENABEL         SET_BIT(CCM_AUDIO_HOSC_PLL_R, 0)
#define HOSC_DISABLE        CLEAR_BIT(CCM_AUDIO_HOSC_PLL_R, 0)
#define HOSC_IS_ABLE        TEST_BIT(CCM_AUDIO_HOSC_PLL_R, 0)

/************************************************************************/
/* AHB and APB Clock Ratio Configure Register                      */
/************************************************************************/
#define _CPU_CLK_SRC_LOSC       0U
#define _CPU_CLK_SRC_HOSC       1U
#define _CPU_CLK_SRC_CORE_PLL   2U
#define _CPU_CLK_SRC_AUDIO_PLL_4X  3u
#define CPU_CLK_SRC_SEL(_val)   SET_BITS(CCM_AHB_APB_RATIO_CFG_R, 9, 2, (_val))
#define CPU_CLK_SRC_GET         GET_BITS_VAL(CCM_AHB_APB_RATIO_CFG_R, 9, 2) ///==_CPU_CLK_SRC_AUDIO_PLL

#define _APB_DIV_RATIO_2    1U
#define _APB_DIV_RATIO_4    2U
#define _APB_DIV_RATIO_8    3U
#define APB_CLK_RATIO_SEL(_val)     SET_BITS(CCM_AHB_APB_RATIO_CFG_R, 6, 2, (_val))
#define APB_CLK_RATIO_GET           GET_BITS_VAL(CCM_AHB_APB_RATIO_CFG_R, 6, 2)

#define _AHB_CLK_DIV_RATIO_1    0U
#define _AHB_CLK_DIV_RATIO_2    1U
#define _AHB_CLK_DIV_RATIO_4    2U
#define _AHB_CLK_DIV_RATIO_8    3U
#define AHB_CLK_DIV_RATIO_SET(_val)     SET_BITS(CCM_AHB_APB_RATIO_CFG_R, 3, 2, (_val))
#define AHB_CLK_DIV_RATIO_GET           2<<( GET_BITS_VAL(CCM_AHB_APB_RATIO_CFG_R, 3, 2) )

#define _CPU_CLK_DIV_RATIO_1    0U
#define _CPU_CLK_DIV_RATIO_2    1U
#define _CPU_CLK_DIV_RATIO_2_   2U
#define _CPU_CLK_DIV_RATIO_4    3U
#define CPU_CLK_DIV_RATIO_SET(_val)     SET_BITS(CCM_AHB_APB_RATIO_CFG_R, (_val))

#define USB_PHY2_RST_INVAL_BIT      2U
#define USB_PHY1_RST_INVAL_BIT      1U
#define USB_PHY0_RST_INVAL_BIT      0U

#define DE_IMAGE1_RST_INVAL_BIT     (19U)
#define DE_IMAGE0_RST_INVAL_BIT     (18U)
#define DE_SCALE1_RST_INVAL_BIT     (17U)
#define DE_SCALE0_RST_INVAL_BIT     (16U)


#endif //#ifndef _SPEC_CSP_CCMU_H_


