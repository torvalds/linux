/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2019 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#ifndef __INC_HALMAC_BIT_8197F_H
#define __INC_HALMAC_BIT_8197F_H

#define CPU_OPT_WIDTH 0x1F

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_SYS_ISO_CTRL_8197F */
#define BIT_PWC_EV12V_8197F BIT(15)
#define BIT_PWC_EV25V_8197F BIT(14)
#define BIT_PA33V_EN_8197F BIT(13)
#define BIT_PA12V_EN_8197F BIT(12)
#define BIT_UA33V_EN_8197F BIT(11)
#define BIT_UA12V_EN_8197F BIT(10)
#define BIT_ISO_RFDIO_8197F BIT(9)
#define BIT_ISO_EB2CORE_8197F BIT(8)
#define BIT_ISO_DIOE_8197F BIT(7)
#define BIT_ISO_WLPON2PP_8197F BIT(6)
#define BIT_ISO_IP2MAC_WA2PP_8197F BIT(5)
#define BIT_ISO_PD2CORE_8197F BIT(4)
#define BIT_ISO_PA2PCIE_8197F BIT(3)
#define BIT_ISO_UD2CORE_8197F BIT(2)
#define BIT_ISO_UA2USB_8197F BIT(1)
#define BIT_ISO_WD2PP_8197F BIT(0)

/* 2 REG_SYS_FUNC_EN_8197F */
#define BIT_FEN_MREGEN_8197F BIT(15)
#define BIT_FEN_HWPDN_8197F BIT(14)
#define BIT_EN_25_1_8197F BIT(13)
#define BIT_FEN_ELDR_8197F BIT(12)
#define BIT_FEN_DCORE_8197F BIT(11)
#define BIT_FEN_CPUEN_8197F BIT(10)
#define BIT_FEN_DIOE_8197F BIT(9)
#define BIT_FEN_PCIED_8197F BIT(8)
#define BIT_FEN_PPLL_8197F BIT(7)
#define BIT_FEN_PCIEA_8197F BIT(6)
#define BIT_FEN_DIO_PCIE_8197F BIT(5)
#define BIT_FEN_USBD_8197F BIT(4)
#define BIT_FEN_UPLL_8197F BIT(3)
#define BIT_FEN_USBA_8197F BIT(2)
#define BIT_FEN_BB_GLB_RSTN_8197F BIT(1)
#define BIT_FEN_BBRSTB_8197F BIT(0)

/* 2 REG_SYS_PW_CTRL_8197F */
#define BIT_SOP_EABM_8197F BIT(31)
#define BIT_SOP_ACKF_8197F BIT(30)
#define BIT_SOP_ERCK_8197F BIT(29)
#define BIT_SOP_ESWR_8197F BIT(28)
#define BIT_SOP_PWMM_8197F BIT(27)
#define BIT_SOP_EECK_8197F BIT(26)
#define BIT_SOP_EXTL_8197F BIT(24)
#define BIT_SYM_OP_RING_12M_8197F BIT(22)
#define BIT_ROP_SWPR_8197F BIT(21)
#define BIT_DIS_HW_LPLDM_8197F BIT(20)
#define BIT_OPT_SWRST_WLMCU_8197F BIT(19)
#define BIT_RDY_SYSPWR_8197F BIT(17)
#define BIT_EN_WLON_8197F BIT(16)
#define BIT_APDM_HPDN_8197F BIT(15)
#define BIT_AFSM_PCIE_SUS_EN_8197F BIT(12)
#define BIT_AFSM_WLSUS_EN_8197F BIT(11)
#define BIT_APFM_SWLPS_8197F BIT(10)
#define BIT_APFM_OFFMAC_8197F BIT(9)
#define BIT_APFN_ONMAC_8197F BIT(8)
#define BIT_CHIP_PDN_EN_8197F BIT(7)
#define BIT_RDY_MACDIS_8197F BIT(6)
#define BIT_RING_CLK_12M_EN_8197F BIT(4)
#define BIT_PFM_WOWL_8197F BIT(3)
#define BIT_PFM_LDKP_8197F BIT(2)
#define BIT_WL_HCI_ALD_8197F BIT(1)
#define BIT_PFM_LDALL_8197F BIT(0)

/* 2 REG_SYS_CLK_CTRL_8197F */
#define BIT_LDO_DUMMY_8197F BIT(15)
#define BIT_CPU_CLK_EN_8197F BIT(14)
#define BIT_SYMREG_CLK_EN_8197F BIT(13)
#define BIT_HCI_CLK_EN_8197F BIT(12)
#define BIT_MAC_CLK_EN_8197F BIT(11)
#define BIT_SEC_CLK_EN_8197F BIT(10)
#define BIT_PHY_SSC_RSTB_8197F BIT(9)
#define BIT_EXT_32K_EN_8197F BIT(8)
#define BIT_WL_CLK_TEST_8197F BIT(7)
#define BIT_OP_SPS_PWM_EN_8197F BIT(6)
#define BIT_LOADER_CLK_EN_8197F BIT(5)
#define BIT_MACSLP_8197F BIT(4)
#define BIT_WAKEPAD_EN_8197F BIT(3)
#define BIT_ROMD16V_EN_8197F BIT(2)
#define BIT_CKANA12M_EN_8197F BIT(1)
#define BIT_CNTD16V_EN_8197F BIT(0)

/* 2 REG_SYS_EEPROM_CTRL_8197F */

#define BIT_SHIFT_VPDIDX_8197F 8
#define BIT_MASK_VPDIDX_8197F 0xff
#define BIT_VPDIDX_8197F(x)                                                    \
	(((x) & BIT_MASK_VPDIDX_8197F) << BIT_SHIFT_VPDIDX_8197F)
#define BITS_VPDIDX_8197F (BIT_MASK_VPDIDX_8197F << BIT_SHIFT_VPDIDX_8197F)
#define BIT_CLEAR_VPDIDX_8197F(x) ((x) & (~BITS_VPDIDX_8197F))
#define BIT_GET_VPDIDX_8197F(x)                                                \
	(((x) >> BIT_SHIFT_VPDIDX_8197F) & BIT_MASK_VPDIDX_8197F)
#define BIT_SET_VPDIDX_8197F(x, v)                                             \
	(BIT_CLEAR_VPDIDX_8197F(x) | BIT_VPDIDX_8197F(v))

#define BIT_SHIFT_EEM1_0_8197F 6
#define BIT_MASK_EEM1_0_8197F 0x3
#define BIT_EEM1_0_8197F(x)                                                    \
	(((x) & BIT_MASK_EEM1_0_8197F) << BIT_SHIFT_EEM1_0_8197F)
#define BITS_EEM1_0_8197F (BIT_MASK_EEM1_0_8197F << BIT_SHIFT_EEM1_0_8197F)
#define BIT_CLEAR_EEM1_0_8197F(x) ((x) & (~BITS_EEM1_0_8197F))
#define BIT_GET_EEM1_0_8197F(x)                                                \
	(((x) >> BIT_SHIFT_EEM1_0_8197F) & BIT_MASK_EEM1_0_8197F)
#define BIT_SET_EEM1_0_8197F(x, v)                                             \
	(BIT_CLEAR_EEM1_0_8197F(x) | BIT_EEM1_0_8197F(v))

#define BIT_AUTOLOAD_SUS_8197F BIT(5)
#define BIT_EERPOMSEL_8197F BIT(4)
#define BIT_EECS_V1_8197F BIT(3)
#define BIT_EESK_V1_8197F BIT(2)
#define BIT_EEDI_V1_8197F BIT(1)
#define BIT_EEDO_V1_8197F BIT(0)

/* 2 REG_EE_VPD_8197F */

#define BIT_SHIFT_VPD_DATA_8197F 0
#define BIT_MASK_VPD_DATA_8197F 0xffffffffL
#define BIT_VPD_DATA_8197F(x)                                                  \
	(((x) & BIT_MASK_VPD_DATA_8197F) << BIT_SHIFT_VPD_DATA_8197F)
#define BITS_VPD_DATA_8197F                                                    \
	(BIT_MASK_VPD_DATA_8197F << BIT_SHIFT_VPD_DATA_8197F)
#define BIT_CLEAR_VPD_DATA_8197F(x) ((x) & (~BITS_VPD_DATA_8197F))
#define BIT_GET_VPD_DATA_8197F(x)                                              \
	(((x) >> BIT_SHIFT_VPD_DATA_8197F) & BIT_MASK_VPD_DATA_8197F)
#define BIT_SET_VPD_DATA_8197F(x, v)                                           \
	(BIT_CLEAR_VPD_DATA_8197F(x) | BIT_VPD_DATA_8197F(v))

/* 2 REG_SYS_SWR_CTRL1_8197F */
#define BIT_SW18_C2_BIT0_8197F BIT(31)

#define BIT_SHIFT_SW18_C1_8197F 29
#define BIT_MASK_SW18_C1_8197F 0x3
#define BIT_SW18_C1_8197F(x)                                                   \
	(((x) & BIT_MASK_SW18_C1_8197F) << BIT_SHIFT_SW18_C1_8197F)
#define BITS_SW18_C1_8197F (BIT_MASK_SW18_C1_8197F << BIT_SHIFT_SW18_C1_8197F)
#define BIT_CLEAR_SW18_C1_8197F(x) ((x) & (~BITS_SW18_C1_8197F))
#define BIT_GET_SW18_C1_8197F(x)                                               \
	(((x) >> BIT_SHIFT_SW18_C1_8197F) & BIT_MASK_SW18_C1_8197F)
#define BIT_SET_SW18_C1_8197F(x, v)                                            \
	(BIT_CLEAR_SW18_C1_8197F(x) | BIT_SW18_C1_8197F(v))

#define BIT_SHIFT_REG_FREQ_L_8197F 25
#define BIT_MASK_REG_FREQ_L_8197F 0x7
#define BIT_REG_FREQ_L_8197F(x)                                                \
	(((x) & BIT_MASK_REG_FREQ_L_8197F) << BIT_SHIFT_REG_FREQ_L_8197F)
#define BITS_REG_FREQ_L_8197F                                                  \
	(BIT_MASK_REG_FREQ_L_8197F << BIT_SHIFT_REG_FREQ_L_8197F)
#define BIT_CLEAR_REG_FREQ_L_8197F(x) ((x) & (~BITS_REG_FREQ_L_8197F))
#define BIT_GET_REG_FREQ_L_8197F(x)                                            \
	(((x) >> BIT_SHIFT_REG_FREQ_L_8197F) & BIT_MASK_REG_FREQ_L_8197F)
#define BIT_SET_REG_FREQ_L_8197F(x, v)                                         \
	(BIT_CLEAR_REG_FREQ_L_8197F(x) | BIT_REG_FREQ_L_8197F(v))

#define BIT_REG_EN_DUTY_8197F BIT(24)

#define BIT_SHIFT_REG_MODE_8197F 22
#define BIT_MASK_REG_MODE_8197F 0x3
#define BIT_REG_MODE_8197F(x)                                                  \
	(((x) & BIT_MASK_REG_MODE_8197F) << BIT_SHIFT_REG_MODE_8197F)
#define BITS_REG_MODE_8197F                                                    \
	(BIT_MASK_REG_MODE_8197F << BIT_SHIFT_REG_MODE_8197F)
#define BIT_CLEAR_REG_MODE_8197F(x) ((x) & (~BITS_REG_MODE_8197F))
#define BIT_GET_REG_MODE_8197F(x)                                              \
	(((x) >> BIT_SHIFT_REG_MODE_8197F) & BIT_MASK_REG_MODE_8197F)
#define BIT_SET_REG_MODE_8197F(x, v)                                           \
	(BIT_CLEAR_REG_MODE_8197F(x) | BIT_REG_MODE_8197F(v))

#define BIT_REG_EN_SP_8197F BIT(21)
#define BIT_REG_AUTO_L_8197F BIT(20)
#define BIT_SW18_SELD_BIT0_8197F BIT(19)
#define BIT_SW18_POWOCP_8197F BIT(18)

#define BIT_SHIFT_SW18_OCP_8197F 15
#define BIT_MASK_SW18_OCP_8197F 0x7
#define BIT_SW18_OCP_8197F(x)                                                  \
	(((x) & BIT_MASK_SW18_OCP_8197F) << BIT_SHIFT_SW18_OCP_8197F)
#define BITS_SW18_OCP_8197F                                                    \
	(BIT_MASK_SW18_OCP_8197F << BIT_SHIFT_SW18_OCP_8197F)
#define BIT_CLEAR_SW18_OCP_8197F(x) ((x) & (~BITS_SW18_OCP_8197F))
#define BIT_GET_SW18_OCP_8197F(x)                                              \
	(((x) >> BIT_SHIFT_SW18_OCP_8197F) & BIT_MASK_SW18_OCP_8197F)
#define BIT_SET_SW18_OCP_8197F(x, v)                                           \
	(BIT_CLEAR_SW18_OCP_8197F(x) | BIT_SW18_OCP_8197F(v))

#define BIT_SHIFT_CF_L_BIT0_TO_1_8197F 13
#define BIT_MASK_CF_L_BIT0_TO_1_8197F 0x3
#define BIT_CF_L_BIT0_TO_1_8197F(x)                                            \
	(((x) & BIT_MASK_CF_L_BIT0_TO_1_8197F)                                 \
	 << BIT_SHIFT_CF_L_BIT0_TO_1_8197F)
#define BITS_CF_L_BIT0_TO_1_8197F                                              \
	(BIT_MASK_CF_L_BIT0_TO_1_8197F << BIT_SHIFT_CF_L_BIT0_TO_1_8197F)
#define BIT_CLEAR_CF_L_BIT0_TO_1_8197F(x) ((x) & (~BITS_CF_L_BIT0_TO_1_8197F))
#define BIT_GET_CF_L_BIT0_TO_1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_CF_L_BIT0_TO_1_8197F) &                             \
	 BIT_MASK_CF_L_BIT0_TO_1_8197F)
#define BIT_SET_CF_L_BIT0_TO_1_8197F(x, v)                                     \
	(BIT_CLEAR_CF_L_BIT0_TO_1_8197F(x) | BIT_CF_L_BIT0_TO_1_8197F(v))

#define BIT_SW18_FPWM_8197F BIT(11)
#define BIT_SW18_SWEN_8197F BIT(9)
#define BIT_SW18_LDEN_8197F BIT(8)
#define BIT_MAC_ID_EN_8197F BIT(7)
#define BIT_WL_CTRL_XTAL_CADJ_8197F BIT(6)
#define BIT_AFE_BGEN_8197F BIT(0)

/* 2 REG_SYS_SWR_CTRL2_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_SYS_SWR_CTRL3_8197F */
#define BIT_SPS18_OCP_DIS_8197F BIT(31)

#define BIT_SHIFT_SPS18_OCP_TH_8197F 16
#define BIT_MASK_SPS18_OCP_TH_8197F 0x7fff
#define BIT_SPS18_OCP_TH_8197F(x)                                              \
	(((x) & BIT_MASK_SPS18_OCP_TH_8197F) << BIT_SHIFT_SPS18_OCP_TH_8197F)
#define BITS_SPS18_OCP_TH_8197F                                                \
	(BIT_MASK_SPS18_OCP_TH_8197F << BIT_SHIFT_SPS18_OCP_TH_8197F)
#define BIT_CLEAR_SPS18_OCP_TH_8197F(x) ((x) & (~BITS_SPS18_OCP_TH_8197F))
#define BIT_GET_SPS18_OCP_TH_8197F(x)                                          \
	(((x) >> BIT_SHIFT_SPS18_OCP_TH_8197F) & BIT_MASK_SPS18_OCP_TH_8197F)
#define BIT_SET_SPS18_OCP_TH_8197F(x, v)                                       \
	(BIT_CLEAR_SPS18_OCP_TH_8197F(x) | BIT_SPS18_OCP_TH_8197F(v))

#define BIT_SHIFT_OCP_WINDOW_8197F 0
#define BIT_MASK_OCP_WINDOW_8197F 0xffff
#define BIT_OCP_WINDOW_8197F(x)                                                \
	(((x) & BIT_MASK_OCP_WINDOW_8197F) << BIT_SHIFT_OCP_WINDOW_8197F)
#define BITS_OCP_WINDOW_8197F                                                  \
	(BIT_MASK_OCP_WINDOW_8197F << BIT_SHIFT_OCP_WINDOW_8197F)
#define BIT_CLEAR_OCP_WINDOW_8197F(x) ((x) & (~BITS_OCP_WINDOW_8197F))
#define BIT_GET_OCP_WINDOW_8197F(x)                                            \
	(((x) >> BIT_SHIFT_OCP_WINDOW_8197F) & BIT_MASK_OCP_WINDOW_8197F)
#define BIT_SET_OCP_WINDOW_8197F(x, v)                                         \
	(BIT_CLEAR_OCP_WINDOW_8197F(x) | BIT_OCP_WINDOW_8197F(v))

/* 2 REG_RSV_CTRL_8197F */
#define BIT_HREG_DBG_8197F BIT(23)
#define BIT_WLMCUIOIF_8197F BIT(8)
#define BIT_LOCK_ALL_EN_8197F BIT(7)
#define BIT_R_DIS_PRST_8197F BIT(6)
#define BIT_WLOCK_1C_B6_8197F BIT(5)
#define BIT_WLOCK_40_8197F BIT(4)
#define BIT_WLOCK_08_8197F BIT(3)
#define BIT_WLOCK_04_8197F BIT(2)
#define BIT_WLOCK_00_8197F BIT(1)
#define BIT_WLOCK_ALL_8197F BIT(0)

/* 2 REG_RF0_CTRL_8197F */
#define BIT_RF0_SDMRSTB_8197F BIT(2)
#define BIT_RF0_RSTB_8197F BIT(1)
#define BIT_RF0_EN_8197F BIT(0)

/* 2 REG_AFE_LDO_CTRL_8197F */

#define BIT_SHIFT_LPLDH12_RSV_8197F 29
#define BIT_MASK_LPLDH12_RSV_8197F 0x7
#define BIT_LPLDH12_RSV_8197F(x)                                               \
	(((x) & BIT_MASK_LPLDH12_RSV_8197F) << BIT_SHIFT_LPLDH12_RSV_8197F)
#define BITS_LPLDH12_RSV_8197F                                                 \
	(BIT_MASK_LPLDH12_RSV_8197F << BIT_SHIFT_LPLDH12_RSV_8197F)
#define BIT_CLEAR_LPLDH12_RSV_8197F(x) ((x) & (~BITS_LPLDH12_RSV_8197F))
#define BIT_GET_LPLDH12_RSV_8197F(x)                                           \
	(((x) >> BIT_SHIFT_LPLDH12_RSV_8197F) & BIT_MASK_LPLDH12_RSV_8197F)
#define BIT_SET_LPLDH12_RSV_8197F(x, v)                                        \
	(BIT_CLEAR_LPLDH12_RSV_8197F(x) | BIT_LPLDH12_RSV_8197F(v))

#define BIT_LPLDH12_SLP_8197F BIT(28)

#define BIT_SHIFT_LPLDH12_VADJ_8197F 24
#define BIT_MASK_LPLDH12_VADJ_8197F 0xf
#define BIT_LPLDH12_VADJ_8197F(x)                                              \
	(((x) & BIT_MASK_LPLDH12_VADJ_8197F) << BIT_SHIFT_LPLDH12_VADJ_8197F)
#define BITS_LPLDH12_VADJ_8197F                                                \
	(BIT_MASK_LPLDH12_VADJ_8197F << BIT_SHIFT_LPLDH12_VADJ_8197F)
#define BIT_CLEAR_LPLDH12_VADJ_8197F(x) ((x) & (~BITS_LPLDH12_VADJ_8197F))
#define BIT_GET_LPLDH12_VADJ_8197F(x)                                          \
	(((x) >> BIT_SHIFT_LPLDH12_VADJ_8197F) & BIT_MASK_LPLDH12_VADJ_8197F)
#define BIT_SET_LPLDH12_VADJ_8197F(x, v)                                       \
	(BIT_CLEAR_LPLDH12_VADJ_8197F(x) | BIT_LPLDH12_VADJ_8197F(v))

#define BIT_LDH12_EN_8197F BIT(16)
#define BIT_POW_REGU_P1_8197F BIT(10)
#define BIT_LDOV12W_EN_8197F BIT(8)
#define BIT_EX_XTAL_DRV_DIGI_8197F BIT(7)
#define BIT_EX_XTAL_DRV_USB_8197F BIT(6)
#define BIT_EX_XTAL_DRV_AFE_8197F BIT(5)
#define BIT_EX_XTAL_DRV_RF2_8197F BIT(4)
#define BIT_EX_XTAL_DRV_RF1_8197F BIT(3)
#define BIT_POW_REGU_P0_8197F BIT(2)

/* 2 REG_NOT_VALID_8197F */
#define BIT_POW_PLL_LDO_8197F BIT(0)

/* 2 REG_AFE_CTRL1_8197F */
#define BIT_AGPIO_GPE_8197F BIT(31)

#define BIT_SHIFT_XTAL_CAP_XI_8197F 25
#define BIT_MASK_XTAL_CAP_XI_8197F 0x3f
#define BIT_XTAL_CAP_XI_8197F(x)                                               \
	(((x) & BIT_MASK_XTAL_CAP_XI_8197F) << BIT_SHIFT_XTAL_CAP_XI_8197F)
#define BITS_XTAL_CAP_XI_8197F                                                 \
	(BIT_MASK_XTAL_CAP_XI_8197F << BIT_SHIFT_XTAL_CAP_XI_8197F)
#define BIT_CLEAR_XTAL_CAP_XI_8197F(x) ((x) & (~BITS_XTAL_CAP_XI_8197F))
#define BIT_GET_XTAL_CAP_XI_8197F(x)                                           \
	(((x) >> BIT_SHIFT_XTAL_CAP_XI_8197F) & BIT_MASK_XTAL_CAP_XI_8197F)
#define BIT_SET_XTAL_CAP_XI_8197F(x, v)                                        \
	(BIT_CLEAR_XTAL_CAP_XI_8197F(x) | BIT_XTAL_CAP_XI_8197F(v))

#define BIT_SHIFT_XTAL_DRV_DIGI_8197F 23
#define BIT_MASK_XTAL_DRV_DIGI_8197F 0x3
#define BIT_XTAL_DRV_DIGI_8197F(x)                                             \
	(((x) & BIT_MASK_XTAL_DRV_DIGI_8197F) << BIT_SHIFT_XTAL_DRV_DIGI_8197F)
#define BITS_XTAL_DRV_DIGI_8197F                                               \
	(BIT_MASK_XTAL_DRV_DIGI_8197F << BIT_SHIFT_XTAL_DRV_DIGI_8197F)
#define BIT_CLEAR_XTAL_DRV_DIGI_8197F(x) ((x) & (~BITS_XTAL_DRV_DIGI_8197F))
#define BIT_GET_XTAL_DRV_DIGI_8197F(x)                                         \
	(((x) >> BIT_SHIFT_XTAL_DRV_DIGI_8197F) & BIT_MASK_XTAL_DRV_DIGI_8197F)
#define BIT_SET_XTAL_DRV_DIGI_8197F(x, v)                                      \
	(BIT_CLEAR_XTAL_DRV_DIGI_8197F(x) | BIT_XTAL_DRV_DIGI_8197F(v))

#define BIT_XTAL_DRV_USB_BIT1_8197F BIT(22)

#define BIT_SHIFT_MAC_CLK_SEL_8197F 20
#define BIT_MASK_MAC_CLK_SEL_8197F 0x3
#define BIT_MAC_CLK_SEL_8197F(x)                                               \
	(((x) & BIT_MASK_MAC_CLK_SEL_8197F) << BIT_SHIFT_MAC_CLK_SEL_8197F)
#define BITS_MAC_CLK_SEL_8197F                                                 \
	(BIT_MASK_MAC_CLK_SEL_8197F << BIT_SHIFT_MAC_CLK_SEL_8197F)
#define BIT_CLEAR_MAC_CLK_SEL_8197F(x) ((x) & (~BITS_MAC_CLK_SEL_8197F))
#define BIT_GET_MAC_CLK_SEL_8197F(x)                                           \
	(((x) >> BIT_SHIFT_MAC_CLK_SEL_8197F) & BIT_MASK_MAC_CLK_SEL_8197F)
#define BIT_SET_MAC_CLK_SEL_8197F(x, v)                                        \
	(BIT_CLEAR_MAC_CLK_SEL_8197F(x) | BIT_MAC_CLK_SEL_8197F(v))

#define BIT_XTAL_DRV_USB_BIT0_8197F BIT(19)

#define BIT_SHIFT_XTAL_DRV_AFE_8197F 17
#define BIT_MASK_XTAL_DRV_AFE_8197F 0x3
#define BIT_XTAL_DRV_AFE_8197F(x)                                              \
	(((x) & BIT_MASK_XTAL_DRV_AFE_8197F) << BIT_SHIFT_XTAL_DRV_AFE_8197F)
#define BITS_XTAL_DRV_AFE_8197F                                                \
	(BIT_MASK_XTAL_DRV_AFE_8197F << BIT_SHIFT_XTAL_DRV_AFE_8197F)
#define BIT_CLEAR_XTAL_DRV_AFE_8197F(x) ((x) & (~BITS_XTAL_DRV_AFE_8197F))
#define BIT_GET_XTAL_DRV_AFE_8197F(x)                                          \
	(((x) >> BIT_SHIFT_XTAL_DRV_AFE_8197F) & BIT_MASK_XTAL_DRV_AFE_8197F)
#define BIT_SET_XTAL_DRV_AFE_8197F(x, v)                                       \
	(BIT_CLEAR_XTAL_DRV_AFE_8197F(x) | BIT_XTAL_DRV_AFE_8197F(v))

#define BIT_SHIFT_XTAL_DRV_RF2_8197F 15
#define BIT_MASK_XTAL_DRV_RF2_8197F 0x3
#define BIT_XTAL_DRV_RF2_8197F(x)                                              \
	(((x) & BIT_MASK_XTAL_DRV_RF2_8197F) << BIT_SHIFT_XTAL_DRV_RF2_8197F)
#define BITS_XTAL_DRV_RF2_8197F                                                \
	(BIT_MASK_XTAL_DRV_RF2_8197F << BIT_SHIFT_XTAL_DRV_RF2_8197F)
#define BIT_CLEAR_XTAL_DRV_RF2_8197F(x) ((x) & (~BITS_XTAL_DRV_RF2_8197F))
#define BIT_GET_XTAL_DRV_RF2_8197F(x)                                          \
	(((x) >> BIT_SHIFT_XTAL_DRV_RF2_8197F) & BIT_MASK_XTAL_DRV_RF2_8197F)
#define BIT_SET_XTAL_DRV_RF2_8197F(x, v)                                       \
	(BIT_CLEAR_XTAL_DRV_RF2_8197F(x) | BIT_XTAL_DRV_RF2_8197F(v))

#define BIT_SHIFT_XTAL_DRV_RF1_8197F 13
#define BIT_MASK_XTAL_DRV_RF1_8197F 0x3
#define BIT_XTAL_DRV_RF1_8197F(x)                                              \
	(((x) & BIT_MASK_XTAL_DRV_RF1_8197F) << BIT_SHIFT_XTAL_DRV_RF1_8197F)
#define BITS_XTAL_DRV_RF1_8197F                                                \
	(BIT_MASK_XTAL_DRV_RF1_8197F << BIT_SHIFT_XTAL_DRV_RF1_8197F)
#define BIT_CLEAR_XTAL_DRV_RF1_8197F(x) ((x) & (~BITS_XTAL_DRV_RF1_8197F))
#define BIT_GET_XTAL_DRV_RF1_8197F(x)                                          \
	(((x) >> BIT_SHIFT_XTAL_DRV_RF1_8197F) & BIT_MASK_XTAL_DRV_RF1_8197F)
#define BIT_SET_XTAL_DRV_RF1_8197F(x, v)                                       \
	(BIT_CLEAR_XTAL_DRV_RF1_8197F(x) | BIT_XTAL_DRV_RF1_8197F(v))

#define BIT_XTAL_DELAY_DIGI_8197F BIT(12)
#define BIT_XTAL_DELAY_USB_8197F BIT(11)
#define BIT_XTAL_DELAY_AFE_8197F BIT(10)
#define BIT_XTAL_LP_V1_8197F BIT(9)
#define BIT_XTAL_GM_SEP_V1_8197F BIT(8)
#define BIT_XTAL_LDO_VREF_V1_8197F BIT(7)
#define BIT_XTAL_XQSEL_RF_8197F BIT(6)
#define BIT_XTAL_XQSEL_8197F BIT(5)

#define BIT_SHIFT_XTAL_GMN_V1_8197F 3
#define BIT_MASK_XTAL_GMN_V1_8197F 0x3
#define BIT_XTAL_GMN_V1_8197F(x)                                               \
	(((x) & BIT_MASK_XTAL_GMN_V1_8197F) << BIT_SHIFT_XTAL_GMN_V1_8197F)
#define BITS_XTAL_GMN_V1_8197F                                                 \
	(BIT_MASK_XTAL_GMN_V1_8197F << BIT_SHIFT_XTAL_GMN_V1_8197F)
#define BIT_CLEAR_XTAL_GMN_V1_8197F(x) ((x) & (~BITS_XTAL_GMN_V1_8197F))
#define BIT_GET_XTAL_GMN_V1_8197F(x)                                           \
	(((x) >> BIT_SHIFT_XTAL_GMN_V1_8197F) & BIT_MASK_XTAL_GMN_V1_8197F)
#define BIT_SET_XTAL_GMN_V1_8197F(x, v)                                        \
	(BIT_CLEAR_XTAL_GMN_V1_8197F(x) | BIT_XTAL_GMN_V1_8197F(v))

#define BIT_SHIFT_XTAL_GMP_V1_8197F 1
#define BIT_MASK_XTAL_GMP_V1_8197F 0x3
#define BIT_XTAL_GMP_V1_8197F(x)                                               \
	(((x) & BIT_MASK_XTAL_GMP_V1_8197F) << BIT_SHIFT_XTAL_GMP_V1_8197F)
#define BITS_XTAL_GMP_V1_8197F                                                 \
	(BIT_MASK_XTAL_GMP_V1_8197F << BIT_SHIFT_XTAL_GMP_V1_8197F)
#define BIT_CLEAR_XTAL_GMP_V1_8197F(x) ((x) & (~BITS_XTAL_GMP_V1_8197F))
#define BIT_GET_XTAL_GMP_V1_8197F(x)                                           \
	(((x) >> BIT_SHIFT_XTAL_GMP_V1_8197F) & BIT_MASK_XTAL_GMP_V1_8197F)
#define BIT_SET_XTAL_GMP_V1_8197F(x, v)                                        \
	(BIT_CLEAR_XTAL_GMP_V1_8197F(x) | BIT_XTAL_GMP_V1_8197F(v))

#define BIT_XTAL_EN_8197F BIT(0)

/* 2 REG_AFE_CTRL2_8197F */

#define BIT_SHIFT_RS_SET_V2_8197F 26
#define BIT_MASK_RS_SET_V2_8197F 0x7
#define BIT_RS_SET_V2_8197F(x)                                                 \
	(((x) & BIT_MASK_RS_SET_V2_8197F) << BIT_SHIFT_RS_SET_V2_8197F)
#define BITS_RS_SET_V2_8197F                                                   \
	(BIT_MASK_RS_SET_V2_8197F << BIT_SHIFT_RS_SET_V2_8197F)
#define BIT_CLEAR_RS_SET_V2_8197F(x) ((x) & (~BITS_RS_SET_V2_8197F))
#define BIT_GET_RS_SET_V2_8197F(x)                                             \
	(((x) >> BIT_SHIFT_RS_SET_V2_8197F) & BIT_MASK_RS_SET_V2_8197F)
#define BIT_SET_RS_SET_V2_8197F(x, v)                                          \
	(BIT_CLEAR_RS_SET_V2_8197F(x) | BIT_RS_SET_V2_8197F(v))

#define BIT_SHIFT_CP_BIAS_V2_8197F 18
#define BIT_MASK_CP_BIAS_V2_8197F 0x7
#define BIT_CP_BIAS_V2_8197F(x)                                                \
	(((x) & BIT_MASK_CP_BIAS_V2_8197F) << BIT_SHIFT_CP_BIAS_V2_8197F)
#define BITS_CP_BIAS_V2_8197F                                                  \
	(BIT_MASK_CP_BIAS_V2_8197F << BIT_SHIFT_CP_BIAS_V2_8197F)
#define BIT_CLEAR_CP_BIAS_V2_8197F(x) ((x) & (~BITS_CP_BIAS_V2_8197F))
#define BIT_GET_CP_BIAS_V2_8197F(x)                                            \
	(((x) >> BIT_SHIFT_CP_BIAS_V2_8197F) & BIT_MASK_CP_BIAS_V2_8197F)
#define BIT_SET_CP_BIAS_V2_8197F(x, v)                                         \
	(BIT_CLEAR_CP_BIAS_V2_8197F(x) | BIT_CP_BIAS_V2_8197F(v))

#define BIT_FREF_SEL_8197F BIT(16)

#define BIT_SHIFT_MCCO_V2_8197F 14
#define BIT_MASK_MCCO_V2_8197F 0x3
#define BIT_MCCO_V2_8197F(x)                                                   \
	(((x) & BIT_MASK_MCCO_V2_8197F) << BIT_SHIFT_MCCO_V2_8197F)
#define BITS_MCCO_V2_8197F (BIT_MASK_MCCO_V2_8197F << BIT_SHIFT_MCCO_V2_8197F)
#define BIT_CLEAR_MCCO_V2_8197F(x) ((x) & (~BITS_MCCO_V2_8197F))
#define BIT_GET_MCCO_V2_8197F(x)                                               \
	(((x) >> BIT_SHIFT_MCCO_V2_8197F) & BIT_MASK_MCCO_V2_8197F)
#define BIT_SET_MCCO_V2_8197F(x, v)                                            \
	(BIT_CLEAR_MCCO_V2_8197F(x) | BIT_MCCO_V2_8197F(v))

#define BIT_SHIFT_CK320_EN_8197F 12
#define BIT_MASK_CK320_EN_8197F 0x3
#define BIT_CK320_EN_8197F(x)                                                  \
	(((x) & BIT_MASK_CK320_EN_8197F) << BIT_SHIFT_CK320_EN_8197F)
#define BITS_CK320_EN_8197F                                                    \
	(BIT_MASK_CK320_EN_8197F << BIT_SHIFT_CK320_EN_8197F)
#define BIT_CLEAR_CK320_EN_8197F(x) ((x) & (~BITS_CK320_EN_8197F))
#define BIT_GET_CK320_EN_8197F(x)                                              \
	(((x) >> BIT_SHIFT_CK320_EN_8197F) & BIT_MASK_CK320_EN_8197F)
#define BIT_SET_CK320_EN_8197F(x, v)                                           \
	(BIT_CLEAR_CK320_EN_8197F(x) | BIT_CK320_EN_8197F(v))

#define BIT_AGPIO_GPO_8197F BIT(9)

#define BIT_SHIFT_AGPIO_DRV_8197F 7
#define BIT_MASK_AGPIO_DRV_8197F 0x3
#define BIT_AGPIO_DRV_8197F(x)                                                 \
	(((x) & BIT_MASK_AGPIO_DRV_8197F) << BIT_SHIFT_AGPIO_DRV_8197F)
#define BITS_AGPIO_DRV_8197F                                                   \
	(BIT_MASK_AGPIO_DRV_8197F << BIT_SHIFT_AGPIO_DRV_8197F)
#define BIT_CLEAR_AGPIO_DRV_8197F(x) ((x) & (~BITS_AGPIO_DRV_8197F))
#define BIT_GET_AGPIO_DRV_8197F(x)                                             \
	(((x) >> BIT_SHIFT_AGPIO_DRV_8197F) & BIT_MASK_AGPIO_DRV_8197F)
#define BIT_SET_AGPIO_DRV_8197F(x, v)                                          \
	(BIT_CLEAR_AGPIO_DRV_8197F(x) | BIT_AGPIO_DRV_8197F(v))

#define BIT_SHIFT_XTAL_CAP_XO_8197F 1
#define BIT_MASK_XTAL_CAP_XO_8197F 0x3f
#define BIT_XTAL_CAP_XO_8197F(x)                                               \
	(((x) & BIT_MASK_XTAL_CAP_XO_8197F) << BIT_SHIFT_XTAL_CAP_XO_8197F)
#define BITS_XTAL_CAP_XO_8197F                                                 \
	(BIT_MASK_XTAL_CAP_XO_8197F << BIT_SHIFT_XTAL_CAP_XO_8197F)
#define BIT_CLEAR_XTAL_CAP_XO_8197F(x) ((x) & (~BITS_XTAL_CAP_XO_8197F))
#define BIT_GET_XTAL_CAP_XO_8197F(x)                                           \
	(((x) >> BIT_SHIFT_XTAL_CAP_XO_8197F) & BIT_MASK_XTAL_CAP_XO_8197F)
#define BIT_SET_XTAL_CAP_XO_8197F(x, v)                                        \
	(BIT_CLEAR_XTAL_CAP_XO_8197F(x) | BIT_XTAL_CAP_XO_8197F(v))

#define BIT_POW_PLL_8197F BIT(0)

/* 2 REG_AFE_CTRL3_8197F */

#define BIT_SHIFT_PS_V2_8197F 7
#define BIT_MASK_PS_V2_8197F 0x7
#define BIT_PS_V2_8197F(x)                                                     \
	(((x) & BIT_MASK_PS_V2_8197F) << BIT_SHIFT_PS_V2_8197F)
#define BITS_PS_V2_8197F (BIT_MASK_PS_V2_8197F << BIT_SHIFT_PS_V2_8197F)
#define BIT_CLEAR_PS_V2_8197F(x) ((x) & (~BITS_PS_V2_8197F))
#define BIT_GET_PS_V2_8197F(x)                                                 \
	(((x) >> BIT_SHIFT_PS_V2_8197F) & BIT_MASK_PS_V2_8197F)
#define BIT_SET_PS_V2_8197F(x, v)                                              \
	(BIT_CLEAR_PS_V2_8197F(x) | BIT_PS_V2_8197F(v))

#define BIT_PSEN_8197F BIT(6)
#define BIT_DOGENB_8197F BIT(5)

/* 2 REG_EFUSE_CTRL_8197F */
#define BIT_EF_FLAG_8197F BIT(31)

#define BIT_SHIFT_EF_PGPD_8197F 28
#define BIT_MASK_EF_PGPD_8197F 0x7
#define BIT_EF_PGPD_8197F(x)                                                   \
	(((x) & BIT_MASK_EF_PGPD_8197F) << BIT_SHIFT_EF_PGPD_8197F)
#define BITS_EF_PGPD_8197F (BIT_MASK_EF_PGPD_8197F << BIT_SHIFT_EF_PGPD_8197F)
#define BIT_CLEAR_EF_PGPD_8197F(x) ((x) & (~BITS_EF_PGPD_8197F))
#define BIT_GET_EF_PGPD_8197F(x)                                               \
	(((x) >> BIT_SHIFT_EF_PGPD_8197F) & BIT_MASK_EF_PGPD_8197F)
#define BIT_SET_EF_PGPD_8197F(x, v)                                            \
	(BIT_CLEAR_EF_PGPD_8197F(x) | BIT_EF_PGPD_8197F(v))

#define BIT_SHIFT_EF_RDT_8197F 24
#define BIT_MASK_EF_RDT_8197F 0xf
#define BIT_EF_RDT_8197F(x)                                                    \
	(((x) & BIT_MASK_EF_RDT_8197F) << BIT_SHIFT_EF_RDT_8197F)
#define BITS_EF_RDT_8197F (BIT_MASK_EF_RDT_8197F << BIT_SHIFT_EF_RDT_8197F)
#define BIT_CLEAR_EF_RDT_8197F(x) ((x) & (~BITS_EF_RDT_8197F))
#define BIT_GET_EF_RDT_8197F(x)                                                \
	(((x) >> BIT_SHIFT_EF_RDT_8197F) & BIT_MASK_EF_RDT_8197F)
#define BIT_SET_EF_RDT_8197F(x, v)                                             \
	(BIT_CLEAR_EF_RDT_8197F(x) | BIT_EF_RDT_8197F(v))

#define BIT_SHIFT_EF_PGTS_8197F 20
#define BIT_MASK_EF_PGTS_8197F 0xf
#define BIT_EF_PGTS_8197F(x)                                                   \
	(((x) & BIT_MASK_EF_PGTS_8197F) << BIT_SHIFT_EF_PGTS_8197F)
#define BITS_EF_PGTS_8197F (BIT_MASK_EF_PGTS_8197F << BIT_SHIFT_EF_PGTS_8197F)
#define BIT_CLEAR_EF_PGTS_8197F(x) ((x) & (~BITS_EF_PGTS_8197F))
#define BIT_GET_EF_PGTS_8197F(x)                                               \
	(((x) >> BIT_SHIFT_EF_PGTS_8197F) & BIT_MASK_EF_PGTS_8197F)
#define BIT_SET_EF_PGTS_8197F(x, v)                                            \
	(BIT_CLEAR_EF_PGTS_8197F(x) | BIT_EF_PGTS_8197F(v))

#define BIT_EF_PDWN_8197F BIT(19)
#define BIT_EF_ALDEN_8197F BIT(18)

#define BIT_SHIFT_EF_ADDR_8197F 8
#define BIT_MASK_EF_ADDR_8197F 0x3ff
#define BIT_EF_ADDR_8197F(x)                                                   \
	(((x) & BIT_MASK_EF_ADDR_8197F) << BIT_SHIFT_EF_ADDR_8197F)
#define BITS_EF_ADDR_8197F (BIT_MASK_EF_ADDR_8197F << BIT_SHIFT_EF_ADDR_8197F)
#define BIT_CLEAR_EF_ADDR_8197F(x) ((x) & (~BITS_EF_ADDR_8197F))
#define BIT_GET_EF_ADDR_8197F(x)                                               \
	(((x) >> BIT_SHIFT_EF_ADDR_8197F) & BIT_MASK_EF_ADDR_8197F)
#define BIT_SET_EF_ADDR_8197F(x, v)                                            \
	(BIT_CLEAR_EF_ADDR_8197F(x) | BIT_EF_ADDR_8197F(v))

#define BIT_SHIFT_EF_DATA_8197F 0
#define BIT_MASK_EF_DATA_8197F 0xff
#define BIT_EF_DATA_8197F(x)                                                   \
	(((x) & BIT_MASK_EF_DATA_8197F) << BIT_SHIFT_EF_DATA_8197F)
#define BITS_EF_DATA_8197F (BIT_MASK_EF_DATA_8197F << BIT_SHIFT_EF_DATA_8197F)
#define BIT_CLEAR_EF_DATA_8197F(x) ((x) & (~BITS_EF_DATA_8197F))
#define BIT_GET_EF_DATA_8197F(x)                                               \
	(((x) >> BIT_SHIFT_EF_DATA_8197F) & BIT_MASK_EF_DATA_8197F)
#define BIT_SET_EF_DATA_8197F(x, v)                                            \
	(BIT_CLEAR_EF_DATA_8197F(x) | BIT_EF_DATA_8197F(v))

/* 2 REG_LDO_EFUSE_CTRL_8197F */
#define BIT_LDOE25_EN_8197F BIT(31)

#define BIT_SHIFT_LDOE25_V12ADJ_L_8197F 27
#define BIT_MASK_LDOE25_V12ADJ_L_8197F 0xf
#define BIT_LDOE25_V12ADJ_L_8197F(x)                                           \
	(((x) & BIT_MASK_LDOE25_V12ADJ_L_8197F)                                \
	 << BIT_SHIFT_LDOE25_V12ADJ_L_8197F)
#define BITS_LDOE25_V12ADJ_L_8197F                                             \
	(BIT_MASK_LDOE25_V12ADJ_L_8197F << BIT_SHIFT_LDOE25_V12ADJ_L_8197F)
#define BIT_CLEAR_LDOE25_V12ADJ_L_8197F(x) ((x) & (~BITS_LDOE25_V12ADJ_L_8197F))
#define BIT_GET_LDOE25_V12ADJ_L_8197F(x)                                       \
	(((x) >> BIT_SHIFT_LDOE25_V12ADJ_L_8197F) &                            \
	 BIT_MASK_LDOE25_V12ADJ_L_8197F)
#define BIT_SET_LDOE25_V12ADJ_L_8197F(x, v)                                    \
	(BIT_CLEAR_LDOE25_V12ADJ_L_8197F(x) | BIT_LDOE25_V12ADJ_L_8197F(v))

#define BIT_SHIFT_EF_SCAN_START_V1_8197F 16
#define BIT_MASK_EF_SCAN_START_V1_8197F 0x3ff
#define BIT_EF_SCAN_START_V1_8197F(x)                                          \
	(((x) & BIT_MASK_EF_SCAN_START_V1_8197F)                               \
	 << BIT_SHIFT_EF_SCAN_START_V1_8197F)
#define BITS_EF_SCAN_START_V1_8197F                                            \
	(BIT_MASK_EF_SCAN_START_V1_8197F << BIT_SHIFT_EF_SCAN_START_V1_8197F)
#define BIT_CLEAR_EF_SCAN_START_V1_8197F(x)                                    \
	((x) & (~BITS_EF_SCAN_START_V1_8197F))
#define BIT_GET_EF_SCAN_START_V1_8197F(x)                                      \
	(((x) >> BIT_SHIFT_EF_SCAN_START_V1_8197F) &                           \
	 BIT_MASK_EF_SCAN_START_V1_8197F)
#define BIT_SET_EF_SCAN_START_V1_8197F(x, v)                                   \
	(BIT_CLEAR_EF_SCAN_START_V1_8197F(x) | BIT_EF_SCAN_START_V1_8197F(v))

#define BIT_SHIFT_EF_SCAN_END_8197F 12
#define BIT_MASK_EF_SCAN_END_8197F 0xf
#define BIT_EF_SCAN_END_8197F(x)                                               \
	(((x) & BIT_MASK_EF_SCAN_END_8197F) << BIT_SHIFT_EF_SCAN_END_8197F)
#define BITS_EF_SCAN_END_8197F                                                 \
	(BIT_MASK_EF_SCAN_END_8197F << BIT_SHIFT_EF_SCAN_END_8197F)
#define BIT_CLEAR_EF_SCAN_END_8197F(x) ((x) & (~BITS_EF_SCAN_END_8197F))
#define BIT_GET_EF_SCAN_END_8197F(x)                                           \
	(((x) >> BIT_SHIFT_EF_SCAN_END_8197F) & BIT_MASK_EF_SCAN_END_8197F)
#define BIT_SET_EF_SCAN_END_8197F(x, v)                                        \
	(BIT_CLEAR_EF_SCAN_END_8197F(x) | BIT_EF_SCAN_END_8197F(v))

#define BIT_SHIFT_EF_CELL_SEL_8197F 8
#define BIT_MASK_EF_CELL_SEL_8197F 0x3
#define BIT_EF_CELL_SEL_8197F(x)                                               \
	(((x) & BIT_MASK_EF_CELL_SEL_8197F) << BIT_SHIFT_EF_CELL_SEL_8197F)
#define BITS_EF_CELL_SEL_8197F                                                 \
	(BIT_MASK_EF_CELL_SEL_8197F << BIT_SHIFT_EF_CELL_SEL_8197F)
#define BIT_CLEAR_EF_CELL_SEL_8197F(x) ((x) & (~BITS_EF_CELL_SEL_8197F))
#define BIT_GET_EF_CELL_SEL_8197F(x)                                           \
	(((x) >> BIT_SHIFT_EF_CELL_SEL_8197F) & BIT_MASK_EF_CELL_SEL_8197F)
#define BIT_SET_EF_CELL_SEL_8197F(x, v)                                        \
	(BIT_CLEAR_EF_CELL_SEL_8197F(x) | BIT_EF_CELL_SEL_8197F(v))

#define BIT_EF_TRPT_8197F BIT(7)

#define BIT_SHIFT_EF_TTHD_8197F 0
#define BIT_MASK_EF_TTHD_8197F 0x7f
#define BIT_EF_TTHD_8197F(x)                                                   \
	(((x) & BIT_MASK_EF_TTHD_8197F) << BIT_SHIFT_EF_TTHD_8197F)
#define BITS_EF_TTHD_8197F (BIT_MASK_EF_TTHD_8197F << BIT_SHIFT_EF_TTHD_8197F)
#define BIT_CLEAR_EF_TTHD_8197F(x) ((x) & (~BITS_EF_TTHD_8197F))
#define BIT_GET_EF_TTHD_8197F(x)                                               \
	(((x) >> BIT_SHIFT_EF_TTHD_8197F) & BIT_MASK_EF_TTHD_8197F)
#define BIT_SET_EF_TTHD_8197F(x, v)                                            \
	(BIT_CLEAR_EF_TTHD_8197F(x) | BIT_EF_TTHD_8197F(v))

/* 2 REG_PWR_OPTION_CTRL_8197F */

#define BIT_SHIFT_DBG_SEL_V1_8197F 16
#define BIT_MASK_DBG_SEL_V1_8197F 0xff
#define BIT_DBG_SEL_V1_8197F(x)                                                \
	(((x) & BIT_MASK_DBG_SEL_V1_8197F) << BIT_SHIFT_DBG_SEL_V1_8197F)
#define BITS_DBG_SEL_V1_8197F                                                  \
	(BIT_MASK_DBG_SEL_V1_8197F << BIT_SHIFT_DBG_SEL_V1_8197F)
#define BIT_CLEAR_DBG_SEL_V1_8197F(x) ((x) & (~BITS_DBG_SEL_V1_8197F))
#define BIT_GET_DBG_SEL_V1_8197F(x)                                            \
	(((x) >> BIT_SHIFT_DBG_SEL_V1_8197F) & BIT_MASK_DBG_SEL_V1_8197F)
#define BIT_SET_DBG_SEL_V1_8197F(x, v)                                         \
	(BIT_CLEAR_DBG_SEL_V1_8197F(x) | BIT_DBG_SEL_V1_8197F(v))

#define BIT_SHIFT_DBG_SEL_BYTE_8197F 14
#define BIT_MASK_DBG_SEL_BYTE_8197F 0x3
#define BIT_DBG_SEL_BYTE_8197F(x)                                              \
	(((x) & BIT_MASK_DBG_SEL_BYTE_8197F) << BIT_SHIFT_DBG_SEL_BYTE_8197F)
#define BITS_DBG_SEL_BYTE_8197F                                                \
	(BIT_MASK_DBG_SEL_BYTE_8197F << BIT_SHIFT_DBG_SEL_BYTE_8197F)
#define BIT_CLEAR_DBG_SEL_BYTE_8197F(x) ((x) & (~BITS_DBG_SEL_BYTE_8197F))
#define BIT_GET_DBG_SEL_BYTE_8197F(x)                                          \
	(((x) >> BIT_SHIFT_DBG_SEL_BYTE_8197F) & BIT_MASK_DBG_SEL_BYTE_8197F)
#define BIT_SET_DBG_SEL_BYTE_8197F(x, v)                                       \
	(BIT_CLEAR_DBG_SEL_BYTE_8197F(x) | BIT_DBG_SEL_BYTE_8197F(v))

#define BIT_SHIFT_STD_L1_V1_8197F 12
#define BIT_MASK_STD_L1_V1_8197F 0x3
#define BIT_STD_L1_V1_8197F(x)                                                 \
	(((x) & BIT_MASK_STD_L1_V1_8197F) << BIT_SHIFT_STD_L1_V1_8197F)
#define BITS_STD_L1_V1_8197F                                                   \
	(BIT_MASK_STD_L1_V1_8197F << BIT_SHIFT_STD_L1_V1_8197F)
#define BIT_CLEAR_STD_L1_V1_8197F(x) ((x) & (~BITS_STD_L1_V1_8197F))
#define BIT_GET_STD_L1_V1_8197F(x)                                             \
	(((x) >> BIT_SHIFT_STD_L1_V1_8197F) & BIT_MASK_STD_L1_V1_8197F)
#define BIT_SET_STD_L1_V1_8197F(x, v)                                          \
	(BIT_CLEAR_STD_L1_V1_8197F(x) | BIT_STD_L1_V1_8197F(v))

#define BIT_SYSON_DBG_PAD_E2_8197F BIT(11)
#define BIT_SYSON_LED_PAD_E2_8197F BIT(10)
#define BIT_SYSON_GPEE_PAD_E2_8197F BIT(9)
#define BIT_SYSON_PCI_PAD_E2_8197F BIT(8)
#define BIT_AUTO_SW_LDO_VOL_EN_8197F BIT(7)

#define BIT_SHIFT_SYSON_SPS0WWV_WT_8197F 4
#define BIT_MASK_SYSON_SPS0WWV_WT_8197F 0x3
#define BIT_SYSON_SPS0WWV_WT_8197F(x)                                          \
	(((x) & BIT_MASK_SYSON_SPS0WWV_WT_8197F)                               \
	 << BIT_SHIFT_SYSON_SPS0WWV_WT_8197F)
#define BITS_SYSON_SPS0WWV_WT_8197F                                            \
	(BIT_MASK_SYSON_SPS0WWV_WT_8197F << BIT_SHIFT_SYSON_SPS0WWV_WT_8197F)
#define BIT_CLEAR_SYSON_SPS0WWV_WT_8197F(x)                                    \
	((x) & (~BITS_SYSON_SPS0WWV_WT_8197F))
#define BIT_GET_SYSON_SPS0WWV_WT_8197F(x)                                      \
	(((x) >> BIT_SHIFT_SYSON_SPS0WWV_WT_8197F) &                           \
	 BIT_MASK_SYSON_SPS0WWV_WT_8197F)
#define BIT_SET_SYSON_SPS0WWV_WT_8197F(x, v)                                   \
	(BIT_CLEAR_SYSON_SPS0WWV_WT_8197F(x) | BIT_SYSON_SPS0WWV_WT_8197F(v))

#define BIT_SHIFT_SYSON_SPS0LDO_WT_8197F 2
#define BIT_MASK_SYSON_SPS0LDO_WT_8197F 0x3
#define BIT_SYSON_SPS0LDO_WT_8197F(x)                                          \
	(((x) & BIT_MASK_SYSON_SPS0LDO_WT_8197F)                               \
	 << BIT_SHIFT_SYSON_SPS0LDO_WT_8197F)
#define BITS_SYSON_SPS0LDO_WT_8197F                                            \
	(BIT_MASK_SYSON_SPS0LDO_WT_8197F << BIT_SHIFT_SYSON_SPS0LDO_WT_8197F)
#define BIT_CLEAR_SYSON_SPS0LDO_WT_8197F(x)                                    \
	((x) & (~BITS_SYSON_SPS0LDO_WT_8197F))
#define BIT_GET_SYSON_SPS0LDO_WT_8197F(x)                                      \
	(((x) >> BIT_SHIFT_SYSON_SPS0LDO_WT_8197F) &                           \
	 BIT_MASK_SYSON_SPS0LDO_WT_8197F)
#define BIT_SET_SYSON_SPS0LDO_WT_8197F(x, v)                                   \
	(BIT_CLEAR_SYSON_SPS0LDO_WT_8197F(x) | BIT_SYSON_SPS0LDO_WT_8197F(v))

#define BIT_SHIFT_SYSON_RCLK_SCALE_8197F 0
#define BIT_MASK_SYSON_RCLK_SCALE_8197F 0x3
#define BIT_SYSON_RCLK_SCALE_8197F(x)                                          \
	(((x) & BIT_MASK_SYSON_RCLK_SCALE_8197F)                               \
	 << BIT_SHIFT_SYSON_RCLK_SCALE_8197F)
#define BITS_SYSON_RCLK_SCALE_8197F                                            \
	(BIT_MASK_SYSON_RCLK_SCALE_8197F << BIT_SHIFT_SYSON_RCLK_SCALE_8197F)
#define BIT_CLEAR_SYSON_RCLK_SCALE_8197F(x)                                    \
	((x) & (~BITS_SYSON_RCLK_SCALE_8197F))
#define BIT_GET_SYSON_RCLK_SCALE_8197F(x)                                      \
	(((x) >> BIT_SHIFT_SYSON_RCLK_SCALE_8197F) &                           \
	 BIT_MASK_SYSON_RCLK_SCALE_8197F)
#define BIT_SET_SYSON_RCLK_SCALE_8197F(x, v)                                   \
	(BIT_CLEAR_SYSON_RCLK_SCALE_8197F(x) | BIT_SYSON_RCLK_SCALE_8197F(v))

/* 2 REG_CAL_TIMER_8197F */

#define BIT_SHIFT_MATCH_CNT_8197F 8
#define BIT_MASK_MATCH_CNT_8197F 0xff
#define BIT_MATCH_CNT_8197F(x)                                                 \
	(((x) & BIT_MASK_MATCH_CNT_8197F) << BIT_SHIFT_MATCH_CNT_8197F)
#define BITS_MATCH_CNT_8197F                                                   \
	(BIT_MASK_MATCH_CNT_8197F << BIT_SHIFT_MATCH_CNT_8197F)
#define BIT_CLEAR_MATCH_CNT_8197F(x) ((x) & (~BITS_MATCH_CNT_8197F))
#define BIT_GET_MATCH_CNT_8197F(x)                                             \
	(((x) >> BIT_SHIFT_MATCH_CNT_8197F) & BIT_MASK_MATCH_CNT_8197F)
#define BIT_SET_MATCH_CNT_8197F(x, v)                                          \
	(BIT_CLEAR_MATCH_CNT_8197F(x) | BIT_MATCH_CNT_8197F(v))

#define BIT_SHIFT_CAL_SCAL_8197F 0
#define BIT_MASK_CAL_SCAL_8197F 0xff
#define BIT_CAL_SCAL_8197F(x)                                                  \
	(((x) & BIT_MASK_CAL_SCAL_8197F) << BIT_SHIFT_CAL_SCAL_8197F)
#define BITS_CAL_SCAL_8197F                                                    \
	(BIT_MASK_CAL_SCAL_8197F << BIT_SHIFT_CAL_SCAL_8197F)
#define BIT_CLEAR_CAL_SCAL_8197F(x) ((x) & (~BITS_CAL_SCAL_8197F))
#define BIT_GET_CAL_SCAL_8197F(x)                                              \
	(((x) >> BIT_SHIFT_CAL_SCAL_8197F) & BIT_MASK_CAL_SCAL_8197F)
#define BIT_SET_CAL_SCAL_8197F(x, v)                                           \
	(BIT_CLEAR_CAL_SCAL_8197F(x) | BIT_CAL_SCAL_8197F(v))

/* 2 REG_ACLK_MON_8197F */

#define BIT_SHIFT_RCLK_MON_8197F 5
#define BIT_MASK_RCLK_MON_8197F 0x7ff
#define BIT_RCLK_MON_8197F(x)                                                  \
	(((x) & BIT_MASK_RCLK_MON_8197F) << BIT_SHIFT_RCLK_MON_8197F)
#define BITS_RCLK_MON_8197F                                                    \
	(BIT_MASK_RCLK_MON_8197F << BIT_SHIFT_RCLK_MON_8197F)
#define BIT_CLEAR_RCLK_MON_8197F(x) ((x) & (~BITS_RCLK_MON_8197F))
#define BIT_GET_RCLK_MON_8197F(x)                                              \
	(((x) >> BIT_SHIFT_RCLK_MON_8197F) & BIT_MASK_RCLK_MON_8197F)
#define BIT_SET_RCLK_MON_8197F(x, v)                                           \
	(BIT_CLEAR_RCLK_MON_8197F(x) | BIT_RCLK_MON_8197F(v))

#define BIT_CAL_EN_8197F BIT(4)

#define BIT_SHIFT_DPSTU_8197F 2
#define BIT_MASK_DPSTU_8197F 0x3
#define BIT_DPSTU_8197F(x)                                                     \
	(((x) & BIT_MASK_DPSTU_8197F) << BIT_SHIFT_DPSTU_8197F)
#define BITS_DPSTU_8197F (BIT_MASK_DPSTU_8197F << BIT_SHIFT_DPSTU_8197F)
#define BIT_CLEAR_DPSTU_8197F(x) ((x) & (~BITS_DPSTU_8197F))
#define BIT_GET_DPSTU_8197F(x)                                                 \
	(((x) >> BIT_SHIFT_DPSTU_8197F) & BIT_MASK_DPSTU_8197F)
#define BIT_SET_DPSTU_8197F(x, v)                                              \
	(BIT_CLEAR_DPSTU_8197F(x) | BIT_DPSTU_8197F(v))

#define BIT_SUS_16X_8197F BIT(1)

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_GPIO_MUXCFG_8197F */
#define BIT_SIC_LOWEST_PRIORITY_8197F BIT(28)

#define BIT_SHIFT_PIN_USECASE_8197F 24
#define BIT_MASK_PIN_USECASE_8197F 0xf
#define BIT_PIN_USECASE_8197F(x)                                               \
	(((x) & BIT_MASK_PIN_USECASE_8197F) << BIT_SHIFT_PIN_USECASE_8197F)
#define BITS_PIN_USECASE_8197F                                                 \
	(BIT_MASK_PIN_USECASE_8197F << BIT_SHIFT_PIN_USECASE_8197F)
#define BIT_CLEAR_PIN_USECASE_8197F(x) ((x) & (~BITS_PIN_USECASE_8197F))
#define BIT_GET_PIN_USECASE_8197F(x)                                           \
	(((x) >> BIT_SHIFT_PIN_USECASE_8197F) & BIT_MASK_PIN_USECASE_8197F)
#define BIT_SET_PIN_USECASE_8197F(x, v)                                        \
	(BIT_CLEAR_PIN_USECASE_8197F(x) | BIT_PIN_USECASE_8197F(v))

#define BIT_FSPI_EN_8197F BIT(19)
#define BIT_WL_RTS_EXT_32K_SEL_8197F BIT(18)
#define BIT_WLGP_SPI_EN_8197F BIT(16)
#define BIT_SIC_LBK_8197F BIT(15)
#define BIT_ENHTP_8197F BIT(14)
#define BIT_WLPHY_DBG_EN_8197F BIT(13)
#define BIT_ENSIC_8197F BIT(12)
#define BIT_SIC_SWRST_8197F BIT(11)
#define BIT_PO_WIFI_PTA_PINS_8197F BIT(10)
#define BIT_BTCOEX_MBOX_EN_8197F BIT(9)
#define BIT_ENUART_8197F BIT(8)

#define BIT_SHIFT_BTMODE_8197F 6
#define BIT_MASK_BTMODE_8197F 0x3
#define BIT_BTMODE_8197F(x)                                                    \
	(((x) & BIT_MASK_BTMODE_8197F) << BIT_SHIFT_BTMODE_8197F)
#define BITS_BTMODE_8197F (BIT_MASK_BTMODE_8197F << BIT_SHIFT_BTMODE_8197F)
#define BIT_CLEAR_BTMODE_8197F(x) ((x) & (~BITS_BTMODE_8197F))
#define BIT_GET_BTMODE_8197F(x)                                                \
	(((x) >> BIT_SHIFT_BTMODE_8197F) & BIT_MASK_BTMODE_8197F)
#define BIT_SET_BTMODE_8197F(x, v)                                             \
	(BIT_CLEAR_BTMODE_8197F(x) | BIT_BTMODE_8197F(v))

#define BIT_ENBT_8197F BIT(5)
#define BIT_EROM_EN_8197F BIT(4)
#define BIT_WLRFE_6_7_EN_8197F BIT(3)
#define BIT_WLRFE_4_5_EN_8197F BIT(2)

#define BIT_SHIFT_GPIOSEL_8197F 0
#define BIT_MASK_GPIOSEL_8197F 0x3
#define BIT_GPIOSEL_8197F(x)                                                   \
	(((x) & BIT_MASK_GPIOSEL_8197F) << BIT_SHIFT_GPIOSEL_8197F)
#define BITS_GPIOSEL_8197F (BIT_MASK_GPIOSEL_8197F << BIT_SHIFT_GPIOSEL_8197F)
#define BIT_CLEAR_GPIOSEL_8197F(x) ((x) & (~BITS_GPIOSEL_8197F))
#define BIT_GET_GPIOSEL_8197F(x)                                               \
	(((x) >> BIT_SHIFT_GPIOSEL_8197F) & BIT_MASK_GPIOSEL_8197F)
#define BIT_SET_GPIOSEL_8197F(x, v)                                            \
	(BIT_CLEAR_GPIOSEL_8197F(x) | BIT_GPIOSEL_8197F(v))

/* 2 REG_GPIO_PIN_CTRL_8197F */

#define BIT_SHIFT_GPIO_MOD_7_TO_0_8197F 24
#define BIT_MASK_GPIO_MOD_7_TO_0_8197F 0xff
#define BIT_GPIO_MOD_7_TO_0_8197F(x)                                           \
	(((x) & BIT_MASK_GPIO_MOD_7_TO_0_8197F)                                \
	 << BIT_SHIFT_GPIO_MOD_7_TO_0_8197F)
#define BITS_GPIO_MOD_7_TO_0_8197F                                             \
	(BIT_MASK_GPIO_MOD_7_TO_0_8197F << BIT_SHIFT_GPIO_MOD_7_TO_0_8197F)
#define BIT_CLEAR_GPIO_MOD_7_TO_0_8197F(x) ((x) & (~BITS_GPIO_MOD_7_TO_0_8197F))
#define BIT_GET_GPIO_MOD_7_TO_0_8197F(x)                                       \
	(((x) >> BIT_SHIFT_GPIO_MOD_7_TO_0_8197F) &                            \
	 BIT_MASK_GPIO_MOD_7_TO_0_8197F)
#define BIT_SET_GPIO_MOD_7_TO_0_8197F(x, v)                                    \
	(BIT_CLEAR_GPIO_MOD_7_TO_0_8197F(x) | BIT_GPIO_MOD_7_TO_0_8197F(v))

#define BIT_SHIFT_GPIO_IO_SEL_7_TO_0_8197F 16
#define BIT_MASK_GPIO_IO_SEL_7_TO_0_8197F 0xff
#define BIT_GPIO_IO_SEL_7_TO_0_8197F(x)                                        \
	(((x) & BIT_MASK_GPIO_IO_SEL_7_TO_0_8197F)                             \
	 << BIT_SHIFT_GPIO_IO_SEL_7_TO_0_8197F)
#define BITS_GPIO_IO_SEL_7_TO_0_8197F                                          \
	(BIT_MASK_GPIO_IO_SEL_7_TO_0_8197F                                     \
	 << BIT_SHIFT_GPIO_IO_SEL_7_TO_0_8197F)
#define BIT_CLEAR_GPIO_IO_SEL_7_TO_0_8197F(x)                                  \
	((x) & (~BITS_GPIO_IO_SEL_7_TO_0_8197F))
#define BIT_GET_GPIO_IO_SEL_7_TO_0_8197F(x)                                    \
	(((x) >> BIT_SHIFT_GPIO_IO_SEL_7_TO_0_8197F) &                         \
	 BIT_MASK_GPIO_IO_SEL_7_TO_0_8197F)
#define BIT_SET_GPIO_IO_SEL_7_TO_0_8197F(x, v)                                 \
	(BIT_CLEAR_GPIO_IO_SEL_7_TO_0_8197F(x) |                               \
	 BIT_GPIO_IO_SEL_7_TO_0_8197F(v))

#define BIT_SHIFT_GPIO_OUT_7_TO_0_8197F 8
#define BIT_MASK_GPIO_OUT_7_TO_0_8197F 0xff
#define BIT_GPIO_OUT_7_TO_0_8197F(x)                                           \
	(((x) & BIT_MASK_GPIO_OUT_7_TO_0_8197F)                                \
	 << BIT_SHIFT_GPIO_OUT_7_TO_0_8197F)
#define BITS_GPIO_OUT_7_TO_0_8197F                                             \
	(BIT_MASK_GPIO_OUT_7_TO_0_8197F << BIT_SHIFT_GPIO_OUT_7_TO_0_8197F)
#define BIT_CLEAR_GPIO_OUT_7_TO_0_8197F(x) ((x) & (~BITS_GPIO_OUT_7_TO_0_8197F))
#define BIT_GET_GPIO_OUT_7_TO_0_8197F(x)                                       \
	(((x) >> BIT_SHIFT_GPIO_OUT_7_TO_0_8197F) &                            \
	 BIT_MASK_GPIO_OUT_7_TO_0_8197F)
#define BIT_SET_GPIO_OUT_7_TO_0_8197F(x, v)                                    \
	(BIT_CLEAR_GPIO_OUT_7_TO_0_8197F(x) | BIT_GPIO_OUT_7_TO_0_8197F(v))

#define BIT_SHIFT_GPIO_IN_7_TO_0_8197F 0
#define BIT_MASK_GPIO_IN_7_TO_0_8197F 0xff
#define BIT_GPIO_IN_7_TO_0_8197F(x)                                            \
	(((x) & BIT_MASK_GPIO_IN_7_TO_0_8197F)                                 \
	 << BIT_SHIFT_GPIO_IN_7_TO_0_8197F)
#define BITS_GPIO_IN_7_TO_0_8197F                                              \
	(BIT_MASK_GPIO_IN_7_TO_0_8197F << BIT_SHIFT_GPIO_IN_7_TO_0_8197F)
#define BIT_CLEAR_GPIO_IN_7_TO_0_8197F(x) ((x) & (~BITS_GPIO_IN_7_TO_0_8197F))
#define BIT_GET_GPIO_IN_7_TO_0_8197F(x)                                        \
	(((x) >> BIT_SHIFT_GPIO_IN_7_TO_0_8197F) &                             \
	 BIT_MASK_GPIO_IN_7_TO_0_8197F)
#define BIT_SET_GPIO_IN_7_TO_0_8197F(x, v)                                     \
	(BIT_CLEAR_GPIO_IN_7_TO_0_8197F(x) | BIT_GPIO_IN_7_TO_0_8197F(v))

/* 2 REG_GPIO_INTM_8197F */

#define BIT_SHIFT_MUXDBG_SEL_8197F 30
#define BIT_MASK_MUXDBG_SEL_8197F 0x3
#define BIT_MUXDBG_SEL_8197F(x)                                                \
	(((x) & BIT_MASK_MUXDBG_SEL_8197F) << BIT_SHIFT_MUXDBG_SEL_8197F)
#define BITS_MUXDBG_SEL_8197F                                                  \
	(BIT_MASK_MUXDBG_SEL_8197F << BIT_SHIFT_MUXDBG_SEL_8197F)
#define BIT_CLEAR_MUXDBG_SEL_8197F(x) ((x) & (~BITS_MUXDBG_SEL_8197F))
#define BIT_GET_MUXDBG_SEL_8197F(x)                                            \
	(((x) >> BIT_SHIFT_MUXDBG_SEL_8197F) & BIT_MASK_MUXDBG_SEL_8197F)
#define BIT_SET_MUXDBG_SEL_8197F(x, v)                                         \
	(BIT_CLEAR_MUXDBG_SEL_8197F(x) | BIT_MUXDBG_SEL_8197F(v))

#define BIT_EXTWOL_SEL_8197F BIT(17)
#define BIT_EXTWOL_EN_8197F BIT(16)
#define BIT_GPIOF_INT_MD_8197F BIT(15)
#define BIT_GPIOE_INT_MD_8197F BIT(14)
#define BIT_GPIOD_INT_MD_8197F BIT(13)
#define BIT_GPIOC_INT_MD_8197F BIT(12)
#define BIT_GPIOB_INT_MD_8197F BIT(11)
#define BIT_GPIOA_INT_MD_8197F BIT(10)
#define BIT_GPIO9_INT_MD_8197F BIT(9)
#define BIT_GPIO8_INT_MD_8197F BIT(8)
#define BIT_GPIO7_INT_MD_8197F BIT(7)
#define BIT_GPIO6_INT_MD_8197F BIT(6)
#define BIT_GPIO5_INT_MD_8197F BIT(5)
#define BIT_GPIO4_INT_MD_8197F BIT(4)
#define BIT_GPIO3_INT_MD_8197F BIT(3)
#define BIT_GPIO2_INT_MD_8197F BIT(2)
#define BIT_GPIO1_INT_MD_8197F BIT(1)
#define BIT_GPIO0_INT_MD_8197F BIT(0)

/* 2 REG_LED_CFG_8197F */
#define BIT_LNAON_SEL_EN_8197F BIT(26)
#define BIT_PAPE_SEL_EN_8197F BIT(25)
#define BIT_DPDT_WLBT_SEL_8197F BIT(24)
#define BIT_DPDT_SEL_EN_8197F BIT(23)
#define BIT_LED2DIS_V1_8197F BIT(22)
#define BIT_LED2EN_8197F BIT(21)
#define BIT_LED2PL_8197F BIT(20)
#define BIT_LED2SV_8197F BIT(19)

#define BIT_SHIFT_LED2CM_8197F 16
#define BIT_MASK_LED2CM_8197F 0x7
#define BIT_LED2CM_8197F(x)                                                    \
	(((x) & BIT_MASK_LED2CM_8197F) << BIT_SHIFT_LED2CM_8197F)
#define BITS_LED2CM_8197F (BIT_MASK_LED2CM_8197F << BIT_SHIFT_LED2CM_8197F)
#define BIT_CLEAR_LED2CM_8197F(x) ((x) & (~BITS_LED2CM_8197F))
#define BIT_GET_LED2CM_8197F(x)                                                \
	(((x) >> BIT_SHIFT_LED2CM_8197F) & BIT_MASK_LED2CM_8197F)
#define BIT_SET_LED2CM_8197F(x, v)                                             \
	(BIT_CLEAR_LED2CM_8197F(x) | BIT_LED2CM_8197F(v))

#define BIT_LED1DIS_8197F BIT(15)
#define BIT_LED1PL_8197F BIT(12)
#define BIT_LED1SV_8197F BIT(11)

#define BIT_SHIFT_LED1CM_8197F 8
#define BIT_MASK_LED1CM_8197F 0x7
#define BIT_LED1CM_8197F(x)                                                    \
	(((x) & BIT_MASK_LED1CM_8197F) << BIT_SHIFT_LED1CM_8197F)
#define BITS_LED1CM_8197F (BIT_MASK_LED1CM_8197F << BIT_SHIFT_LED1CM_8197F)
#define BIT_CLEAR_LED1CM_8197F(x) ((x) & (~BITS_LED1CM_8197F))
#define BIT_GET_LED1CM_8197F(x)                                                \
	(((x) >> BIT_SHIFT_LED1CM_8197F) & BIT_MASK_LED1CM_8197F)
#define BIT_SET_LED1CM_8197F(x, v)                                             \
	(BIT_CLEAR_LED1CM_8197F(x) | BIT_LED1CM_8197F(v))

#define BIT_LED0DIS_8197F BIT(7)

#define BIT_SHIFT_AFE_LDO_SWR_CHECK_8197F 5
#define BIT_MASK_AFE_LDO_SWR_CHECK_8197F 0x3
#define BIT_AFE_LDO_SWR_CHECK_8197F(x)                                         \
	(((x) & BIT_MASK_AFE_LDO_SWR_CHECK_8197F)                              \
	 << BIT_SHIFT_AFE_LDO_SWR_CHECK_8197F)
#define BITS_AFE_LDO_SWR_CHECK_8197F                                           \
	(BIT_MASK_AFE_LDO_SWR_CHECK_8197F << BIT_SHIFT_AFE_LDO_SWR_CHECK_8197F)
#define BIT_CLEAR_AFE_LDO_SWR_CHECK_8197F(x)                                   \
	((x) & (~BITS_AFE_LDO_SWR_CHECK_8197F))
#define BIT_GET_AFE_LDO_SWR_CHECK_8197F(x)                                     \
	(((x) >> BIT_SHIFT_AFE_LDO_SWR_CHECK_8197F) &                          \
	 BIT_MASK_AFE_LDO_SWR_CHECK_8197F)
#define BIT_SET_AFE_LDO_SWR_CHECK_8197F(x, v)                                  \
	(BIT_CLEAR_AFE_LDO_SWR_CHECK_8197F(x) | BIT_AFE_LDO_SWR_CHECK_8197F(v))

#define BIT_LED0PL_8197F BIT(4)
#define BIT_LED0SV_8197F BIT(3)

#define BIT_SHIFT_LED0CM_8197F 0
#define BIT_MASK_LED0CM_8197F 0x7
#define BIT_LED0CM_8197F(x)                                                    \
	(((x) & BIT_MASK_LED0CM_8197F) << BIT_SHIFT_LED0CM_8197F)
#define BITS_LED0CM_8197F (BIT_MASK_LED0CM_8197F << BIT_SHIFT_LED0CM_8197F)
#define BIT_CLEAR_LED0CM_8197F(x) ((x) & (~BITS_LED0CM_8197F))
#define BIT_GET_LED0CM_8197F(x)                                                \
	(((x) >> BIT_SHIFT_LED0CM_8197F) & BIT_MASK_LED0CM_8197F)
#define BIT_SET_LED0CM_8197F(x, v)                                             \
	(BIT_CLEAR_LED0CM_8197F(x) | BIT_LED0CM_8197F(v))

/* 2 REG_FSIMR_8197F */
#define BIT_FS_PDNINT_EN_8197F BIT(31)
#define BIT_FS_SPS_OCP_INT_EN_8197F BIT(29)
#define BIT_FS_PWMERR_INT_EN_8197F BIT(28)
#define BIT_FS_GPIOF_INT_EN_8197F BIT(27)
#define BIT_FS_GPIOE_INT_EN_8197F BIT(26)
#define BIT_FS_GPIOD_INT_EN_8197F BIT(25)
#define BIT_FS_GPIOC_INT_EN_8197F BIT(24)
#define BIT_FS_GPIOB_INT_EN_8197F BIT(23)
#define BIT_FS_GPIOA_INT_EN_8197F BIT(22)
#define BIT_FS_GPIO9_INT_EN_8197F BIT(21)
#define BIT_FS_GPIO8_INT_EN_8197F BIT(20)
#define BIT_FS_GPIO7_INT_EN_8197F BIT(19)
#define BIT_FS_GPIO6_INT_EN_8197F BIT(18)
#define BIT_FS_GPIO5_INT_EN_8197F BIT(17)
#define BIT_FS_GPIO4_INT_EN_8197F BIT(16)
#define BIT_FS_GPIO3_INT_EN_8197F BIT(15)
#define BIT_FS_GPIO2_INT_EN_8197F BIT(14)
#define BIT_FS_GPIO1_INT_EN_8197F BIT(13)
#define BIT_FS_GPIO0_INT_EN_8197F BIT(12)
#define BIT_FS_HCI_SUS_EN_8197F BIT(11)
#define BIT_FS_HCI_RES_EN_8197F BIT(10)
#define BIT_FS_HCI_RESET_EN_8197F BIT(9)
#define BIT_AXI_EXCEPT_FINT_EN_8197F BIT(8)
#define BIT_FS_BTON_STS_UPDATE_MSK_EN_8197F BIT(7)
#define BIT_ACT2RECOVERY_INT_EN_V1_8197F BIT(6)
#define BIT_FS_TRPC_TO_INT_EN_8197F BIT(5)
#define BIT_FS_RPC_O_T_INT_EN_8197F BIT(4)
#define BIT_FS_32K_LEAVE_SETTING_MAK_8197F BIT(3)
#define BIT_FS_32K_ENTER_SETTING_MAK_8197F BIT(2)
#define BIT_FS_USB_LPMRSM_MSK_8197F BIT(1)
#define BIT_FS_USB_LPMINT_MSK_8197F BIT(0)

/* 2 REG_FSISR_8197F */
#define BIT_FS_PDNINT_8197F BIT(31)
#define BIT_FS_SPS_OCP_INT_8197F BIT(29)
#define BIT_FS_PWMERR_INT_8197F BIT(28)
#define BIT_FS_GPIOF_INT_8197F BIT(27)
#define BIT_FS_GPIOE_INT_8197F BIT(26)
#define BIT_FS_GPIOD_INT_8197F BIT(25)
#define BIT_FS_GPIOC_INT_8197F BIT(24)
#define BIT_FS_GPIOB_INT_8197F BIT(23)
#define BIT_FS_GPIOA_INT_8197F BIT(22)
#define BIT_FS_GPIO9_INT_8197F BIT(21)
#define BIT_FS_GPIO8_INT_8197F BIT(20)
#define BIT_FS_GPIO7_INT_8197F BIT(19)
#define BIT_FS_GPIO6_INT_8197F BIT(18)
#define BIT_FS_GPIO5_INT_8197F BIT(17)
#define BIT_FS_GPIO4_INT_8197F BIT(16)
#define BIT_FS_GPIO3_INT_8197F BIT(15)
#define BIT_FS_GPIO2_INT_8197F BIT(14)
#define BIT_FS_GPIO1_INT_8197F BIT(13)
#define BIT_FS_GPIO0_INT_8197F BIT(12)
#define BIT_FS_HCI_SUS_INT_8197F BIT(11)
#define BIT_FS_HCI_RES_INT_8197F BIT(10)
#define BIT_FS_HCI_RESET_INT_8197F BIT(9)
#define BIT_AXI_EXCEPT_FINT_8197F BIT(8)
#define BIT_FS_BTON_STS_UPDATE_INT_8197F BIT(7)
#define BIT_ACT2RECOVERY_INT_V1_8197F BIT(6)
#define BIT_FS_TRPC_TO_INT_INT_8197F BIT(5)
#define BIT_FS_RPC_O_T_INT_INT_8197F BIT(4)
#define BIT_FS_32K_LEAVE_SETTING_INT_8197F BIT(3)
#define BIT_FS_32K_ENTER_SETTING_INT_8197F BIT(2)
#define BIT_FS_USB_LPMRSM_INT_8197F BIT(1)
#define BIT_FS_USB_LPMINT_INT_8197F BIT(0)

/* 2 REG_HSIMR_8197F */
#define BIT_GPIOF_INT_EN_8197F BIT(31)
#define BIT_GPIOE_INT_EN_8197F BIT(30)
#define BIT_GPIOD_INT_EN_8197F BIT(29)
#define BIT_GPIOC_INT_EN_8197F BIT(28)
#define BIT_GPIOB_INT_EN_8197F BIT(27)
#define BIT_GPIOA_INT_EN_8197F BIT(26)
#define BIT_GPIO9_INT_EN_8197F BIT(25)
#define BIT_GPIO8_INT_EN_8197F BIT(24)
#define BIT_GPIO7_INT_EN_8197F BIT(23)
#define BIT_GPIO6_INT_EN_8197F BIT(22)
#define BIT_GPIO5_INT_EN_8197F BIT(21)
#define BIT_GPIO4_INT_EN_8197F BIT(20)
#define BIT_GPIO3_INT_EN_8197F BIT(19)
#define BIT_GPIO2_INT_EN_8197F BIT(18)
#define BIT_GPIO1_INT_EN_8197F BIT(17)
#define BIT_GPIO0_INT_EN_8197F BIT(16)
#define BIT_AXI_EXCEPT_HINT_EN_8197F BIT(9)
#define BIT_PDNINT_EN_V2_8197F BIT(8)
#define BIT_PDNINT_EN_V1_8197F BIT(7)
#define BIT_RON_INT_EN_V1_8197F BIT(6)
#define BIT_SPS_OCP_INT_EN_V1_8197F BIT(5)
#define BIT_GPIO15_0_INT_EN_V1_8197F BIT(0)

/* 2 REG_HSISR_8197F */
#define BIT_GPIOF_INT_8197F BIT(31)
#define BIT_GPIOE_INT_8197F BIT(30)
#define BIT_GPIOD_INT_8197F BIT(29)
#define BIT_GPIOC_INT_8197F BIT(28)
#define BIT_GPIOB_INT_8197F BIT(27)
#define BIT_GPIOA_INT_8197F BIT(26)
#define BIT_GPIO9_INT_8197F BIT(25)
#define BIT_GPIO8_INT_8197F BIT(24)
#define BIT_GPIO7_INT_8197F BIT(23)
#define BIT_GPIO6_INT_8197F BIT(22)
#define BIT_GPIO5_INT_8197F BIT(21)
#define BIT_GPIO4_INT_8197F BIT(20)
#define BIT_GPIO3_INT_8197F BIT(19)
#define BIT_GPIO2_INT_8197F BIT(18)
#define BIT_GPIO1_INT_8197F BIT(17)
#define BIT_GPIO0_INT_8197F BIT(16)
#define BIT_AXI_EXCEPT_HINT_8197F BIT(8)
#define BIT_PDNINT_V1_8197F BIT(7)
#define BIT_RON_INT_V1_8197F BIT(6)
#define BIT_SPS_OCP_INT_V1_8197F BIT(5)
#define BIT_GPIO15_0_INT_V1_8197F BIT(0)

/* 2 REG_GPIO_EXT_CTRL_8197F */

#define BIT_SHIFT_GPIO_MOD_15_TO_8_8197F 24
#define BIT_MASK_GPIO_MOD_15_TO_8_8197F 0xff
#define BIT_GPIO_MOD_15_TO_8_8197F(x)                                          \
	(((x) & BIT_MASK_GPIO_MOD_15_TO_8_8197F)                               \
	 << BIT_SHIFT_GPIO_MOD_15_TO_8_8197F)
#define BITS_GPIO_MOD_15_TO_8_8197F                                            \
	(BIT_MASK_GPIO_MOD_15_TO_8_8197F << BIT_SHIFT_GPIO_MOD_15_TO_8_8197F)
#define BIT_CLEAR_GPIO_MOD_15_TO_8_8197F(x)                                    \
	((x) & (~BITS_GPIO_MOD_15_TO_8_8197F))
#define BIT_GET_GPIO_MOD_15_TO_8_8197F(x)                                      \
	(((x) >> BIT_SHIFT_GPIO_MOD_15_TO_8_8197F) &                           \
	 BIT_MASK_GPIO_MOD_15_TO_8_8197F)
#define BIT_SET_GPIO_MOD_15_TO_8_8197F(x, v)                                   \
	(BIT_CLEAR_GPIO_MOD_15_TO_8_8197F(x) | BIT_GPIO_MOD_15_TO_8_8197F(v))

#define BIT_SHIFT_GPIO_IO_SEL_15_TO_8_8197F 16
#define BIT_MASK_GPIO_IO_SEL_15_TO_8_8197F 0xff
#define BIT_GPIO_IO_SEL_15_TO_8_8197F(x)                                       \
	(((x) & BIT_MASK_GPIO_IO_SEL_15_TO_8_8197F)                            \
	 << BIT_SHIFT_GPIO_IO_SEL_15_TO_8_8197F)
#define BITS_GPIO_IO_SEL_15_TO_8_8197F                                         \
	(BIT_MASK_GPIO_IO_SEL_15_TO_8_8197F                                    \
	 << BIT_SHIFT_GPIO_IO_SEL_15_TO_8_8197F)
#define BIT_CLEAR_GPIO_IO_SEL_15_TO_8_8197F(x)                                 \
	((x) & (~BITS_GPIO_IO_SEL_15_TO_8_8197F))
#define BIT_GET_GPIO_IO_SEL_15_TO_8_8197F(x)                                   \
	(((x) >> BIT_SHIFT_GPIO_IO_SEL_15_TO_8_8197F) &                        \
	 BIT_MASK_GPIO_IO_SEL_15_TO_8_8197F)
#define BIT_SET_GPIO_IO_SEL_15_TO_8_8197F(x, v)                                \
	(BIT_CLEAR_GPIO_IO_SEL_15_TO_8_8197F(x) |                              \
	 BIT_GPIO_IO_SEL_15_TO_8_8197F(v))

#define BIT_SHIFT_GPIO_OUT_15_TO_8_8197F 8
#define BIT_MASK_GPIO_OUT_15_TO_8_8197F 0xff
#define BIT_GPIO_OUT_15_TO_8_8197F(x)                                          \
	(((x) & BIT_MASK_GPIO_OUT_15_TO_8_8197F)                               \
	 << BIT_SHIFT_GPIO_OUT_15_TO_8_8197F)
#define BITS_GPIO_OUT_15_TO_8_8197F                                            \
	(BIT_MASK_GPIO_OUT_15_TO_8_8197F << BIT_SHIFT_GPIO_OUT_15_TO_8_8197F)
#define BIT_CLEAR_GPIO_OUT_15_TO_8_8197F(x)                                    \
	((x) & (~BITS_GPIO_OUT_15_TO_8_8197F))
#define BIT_GET_GPIO_OUT_15_TO_8_8197F(x)                                      \
	(((x) >> BIT_SHIFT_GPIO_OUT_15_TO_8_8197F) &                           \
	 BIT_MASK_GPIO_OUT_15_TO_8_8197F)
#define BIT_SET_GPIO_OUT_15_TO_8_8197F(x, v)                                   \
	(BIT_CLEAR_GPIO_OUT_15_TO_8_8197F(x) | BIT_GPIO_OUT_15_TO_8_8197F(v))

#define BIT_SHIFT_GPIO_IN_15_TO_8_8197F 0
#define BIT_MASK_GPIO_IN_15_TO_8_8197F 0xff
#define BIT_GPIO_IN_15_TO_8_8197F(x)                                           \
	(((x) & BIT_MASK_GPIO_IN_15_TO_8_8197F)                                \
	 << BIT_SHIFT_GPIO_IN_15_TO_8_8197F)
#define BITS_GPIO_IN_15_TO_8_8197F                                             \
	(BIT_MASK_GPIO_IN_15_TO_8_8197F << BIT_SHIFT_GPIO_IN_15_TO_8_8197F)
#define BIT_CLEAR_GPIO_IN_15_TO_8_8197F(x) ((x) & (~BITS_GPIO_IN_15_TO_8_8197F))
#define BIT_GET_GPIO_IN_15_TO_8_8197F(x)                                       \
	(((x) >> BIT_SHIFT_GPIO_IN_15_TO_8_8197F) &                            \
	 BIT_MASK_GPIO_IN_15_TO_8_8197F)
#define BIT_SET_GPIO_IN_15_TO_8_8197F(x, v)                                    \
	(BIT_CLEAR_GPIO_IN_15_TO_8_8197F(x) | BIT_GPIO_IN_15_TO_8_8197F(v))

/* 2 REG_PAD_CTRL1_8197F */
#define BIT_PAPE_WLBT_SEL_8197F BIT(29)
#define BIT_LNAON_WLBT_SEL_8197F BIT(28)
#define BIT_BTGP_GPG3_FEN_8197F BIT(26)
#define BIT_BTGP_GPG2_FEN_8197F BIT(25)
#define BIT_BTGP_JTAG_EN_8197F BIT(24)
#define BIT_XTAL_CLK_EXTARNAL_EN_8197F BIT(23)
#define BIT_BTGP_UART0_EN_8197F BIT(22)
#define BIT_BTGP_UART1_EN_8197F BIT(21)
#define BIT_BTGP_SPI_EN_8197F BIT(20)
#define BIT_BTGP_GPIO_E2_8197F BIT(19)
#define BIT_BTGP_GPIO_EN_8197F BIT(18)

#define BIT_SHIFT_BTGP_GPIO_SL_8197F 16
#define BIT_MASK_BTGP_GPIO_SL_8197F 0x3
#define BIT_BTGP_GPIO_SL_8197F(x)                                              \
	(((x) & BIT_MASK_BTGP_GPIO_SL_8197F) << BIT_SHIFT_BTGP_GPIO_SL_8197F)
#define BITS_BTGP_GPIO_SL_8197F                                                \
	(BIT_MASK_BTGP_GPIO_SL_8197F << BIT_SHIFT_BTGP_GPIO_SL_8197F)
#define BIT_CLEAR_BTGP_GPIO_SL_8197F(x) ((x) & (~BITS_BTGP_GPIO_SL_8197F))
#define BIT_GET_BTGP_GPIO_SL_8197F(x)                                          \
	(((x) >> BIT_SHIFT_BTGP_GPIO_SL_8197F) & BIT_MASK_BTGP_GPIO_SL_8197F)
#define BIT_SET_BTGP_GPIO_SL_8197F(x, v)                                       \
	(BIT_CLEAR_BTGP_GPIO_SL_8197F(x) | BIT_BTGP_GPIO_SL_8197F(v))

#define BIT_PAD_SDIO_SR_8197F BIT(14)
#define BIT_GPIO14_OUTPUT_PL_8197F BIT(13)
#define BIT_HOST_WAKE_PAD_PULL_EN_8197F BIT(12)
#define BIT_HOST_WAKE_PAD_SL_8197F BIT(11)
#define BIT_PAD_LNAON_SR_8197F BIT(10)
#define BIT_PAD_LNAON_E2_8197F BIT(9)
#define BIT_SW_LNAON_G_SEL_DATA_8197F BIT(8)
#define BIT_SW_LNAON_A_SEL_DATA_8197F BIT(7)
#define BIT_PAD_PAPE_SR_8197F BIT(6)
#define BIT_PAD_PAPE_E2_8197F BIT(5)
#define BIT_SW_PAPE_G_SEL_DATA_8197F BIT(4)
#define BIT_SW_PAPE_A_SEL_DATA_8197F BIT(3)
#define BIT_PAD_DPDT_SR_8197F BIT(2)
#define BIT_PAD_DPDT_PAD_E2_8197F BIT(1)
#define BIT_SW_DPDT_SEL_DATA_8197F BIT(0)

/* 2 REG_WL_BT_PWR_CTRL_8197F */
#define BIT_ISO_BD2PP_8197F BIT(31)
#define BIT_LDOV12B_EN_8197F BIT(30)
#define BIT_CKEN_BTGPS_8197F BIT(29)
#define BIT_FEN_BTGPS_8197F BIT(28)
#define BIT_BTCPU_BOOTSEL_8197F BIT(27)
#define BIT_SPI_SPEEDUP_8197F BIT(26)
#define BIT_DEVWAKE_PAD_TYPE_SEL_8197F BIT(24)
#define BIT_CLKREQ_PAD_TYPE_SEL_8197F BIT(23)
#define BIT_ISO_BTPON2PP_8197F BIT(22)
#define BIT_BT_HWROF_EN_8197F BIT(19)
#define BIT_BT_FUNC_EN_8197F BIT(18)
#define BIT_BT_HWPDN_SL_8197F BIT(17)
#define BIT_BT_DISN_EN_8197F BIT(16)
#define BIT_BT_PDN_PULL_EN_8197F BIT(15)
#define BIT_WL_PDN_PULL_EN_8197F BIT(14)
#define BIT_EXTERNAL_REQUEST_PL_8197F BIT(13)
#define BIT_GPIO0_2_3_PULL_LOW_EN_8197F BIT(12)
#define BIT_ISO_BA2PP_8197F BIT(11)
#define BIT_BT_AFE_LDO_EN_8197F BIT(10)
#define BIT_BT_AFE_PLL_EN_8197F BIT(9)
#define BIT_BT_DIG_CLK_EN_8197F BIT(8)
#define BIT_WL_DRV_EXIST_IDX_8197F BIT(5)
#define BIT_DOP_EHPAD_8197F BIT(4)
#define BIT_WL_HWROF_EN_8197F BIT(3)
#define BIT_WL_FUNC_EN_8197F BIT(2)
#define BIT_WL_HWPDN_SL_8197F BIT(1)
#define BIT_WL_HWPDN_EN_8197F BIT(0)

/* 2 REG_SDM_DEBUG_8197F */

#define BIT_SHIFT_WLCLK_PHASE_8197F 0
#define BIT_MASK_WLCLK_PHASE_8197F 0x1f
#define BIT_WLCLK_PHASE_8197F(x)                                               \
	(((x) & BIT_MASK_WLCLK_PHASE_8197F) << BIT_SHIFT_WLCLK_PHASE_8197F)
#define BITS_WLCLK_PHASE_8197F                                                 \
	(BIT_MASK_WLCLK_PHASE_8197F << BIT_SHIFT_WLCLK_PHASE_8197F)
#define BIT_CLEAR_WLCLK_PHASE_8197F(x) ((x) & (~BITS_WLCLK_PHASE_8197F))
#define BIT_GET_WLCLK_PHASE_8197F(x)                                           \
	(((x) >> BIT_SHIFT_WLCLK_PHASE_8197F) & BIT_MASK_WLCLK_PHASE_8197F)
#define BIT_SET_WLCLK_PHASE_8197F(x, v)                                        \
	(BIT_CLEAR_WLCLK_PHASE_8197F(x) | BIT_WLCLK_PHASE_8197F(v))

/* 2 REG_SYS_SDIO_CTRL_8197F */
#define BIT_DBG_GNT_WL_BT_8197F BIT(27)
#define BIT_LTE_MUX_CTRL_PATH_8197F BIT(26)
#define BIT_SDIO_INT_POLARITY_8197F BIT(19)
#define BIT_SDIO_INT_8197F BIT(18)
#define BIT_SDIO_OFF_EN_8197F BIT(17)
#define BIT_SDIO_ON_EN_8197F BIT(16)

/* 2 REG_HCI_OPT_CTRL_8197F */
#define BIT_USB_HOST_PWR_OFF_EN_8197F BIT(12)
#define BIT_SYM_LPS_BLOCK_EN_8197F BIT(11)
#define BIT_USB_LPM_ACT_EN_8197F BIT(10)
#define BIT_USB_LPM_NY_8197F BIT(9)
#define BIT_USB_SUS_DIS_8197F BIT(8)

#define BIT_SHIFT_SDIO_PAD_E_8197F 5
#define BIT_MASK_SDIO_PAD_E_8197F 0x7
#define BIT_SDIO_PAD_E_8197F(x)                                                \
	(((x) & BIT_MASK_SDIO_PAD_E_8197F) << BIT_SHIFT_SDIO_PAD_E_8197F)
#define BITS_SDIO_PAD_E_8197F                                                  \
	(BIT_MASK_SDIO_PAD_E_8197F << BIT_SHIFT_SDIO_PAD_E_8197F)
#define BIT_CLEAR_SDIO_PAD_E_8197F(x) ((x) & (~BITS_SDIO_PAD_E_8197F))
#define BIT_GET_SDIO_PAD_E_8197F(x)                                            \
	(((x) >> BIT_SHIFT_SDIO_PAD_E_8197F) & BIT_MASK_SDIO_PAD_E_8197F)
#define BIT_SET_SDIO_PAD_E_8197F(x, v)                                         \
	(BIT_CLEAR_SDIO_PAD_E_8197F(x) | BIT_SDIO_PAD_E_8197F(v))

#define BIT_USB_LPPLL_EN_8197F BIT(4)
#define BIT_ROP_SW15_8197F BIT(2)
#define BIT_PCI_CKRDY_OPT_8197F BIT(1)
#define BIT_PCI_VAUX_EN_8197F BIT(0)

/* 2 REG_AFE_CTRL4_8197F */
#define BIT_RF1_SDMRSTB_8197F BIT(26)
#define BIT_RF1_RSTB_8197F BIT(25)
#define BIT_RF1_EN_8197F BIT(24)

#define BIT_SHIFT_XTAL_LDO_8197F 20
#define BIT_MASK_XTAL_LDO_8197F 0x7
#define BIT_XTAL_LDO_8197F(x)                                                  \
	(((x) & BIT_MASK_XTAL_LDO_8197F) << BIT_SHIFT_XTAL_LDO_8197F)
#define BITS_XTAL_LDO_8197F                                                    \
	(BIT_MASK_XTAL_LDO_8197F << BIT_SHIFT_XTAL_LDO_8197F)
#define BIT_CLEAR_XTAL_LDO_8197F(x) ((x) & (~BITS_XTAL_LDO_8197F))
#define BIT_GET_XTAL_LDO_8197F(x)                                              \
	(((x) >> BIT_SHIFT_XTAL_LDO_8197F) & BIT_MASK_XTAL_LDO_8197F)
#define BIT_SET_XTAL_LDO_8197F(x, v)                                           \
	(BIT_CLEAR_XTAL_LDO_8197F(x) | BIT_XTAL_LDO_8197F(v))

#define BIT_ADC_CK_SYNC_EN_8197F BIT(16)

/* 2 REG_LDO_SWR_CTRL_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_MCUFW_CTRL_8197F */

#define BIT_SHIFT_RPWM_8197F 24
#define BIT_MASK_RPWM_8197F 0xff
#define BIT_RPWM_8197F(x) (((x) & BIT_MASK_RPWM_8197F) << BIT_SHIFT_RPWM_8197F)
#define BITS_RPWM_8197F (BIT_MASK_RPWM_8197F << BIT_SHIFT_RPWM_8197F)
#define BIT_CLEAR_RPWM_8197F(x) ((x) & (~BITS_RPWM_8197F))
#define BIT_GET_RPWM_8197F(x)                                                  \
	(((x) >> BIT_SHIFT_RPWM_8197F) & BIT_MASK_RPWM_8197F)
#define BIT_SET_RPWM_8197F(x, v) (BIT_CLEAR_RPWM_8197F(x) | BIT_RPWM_8197F(v))

#define BIT_CPRST_8197F BIT(23)
#define BIT_ANA_PORT_EN_8197F BIT(22)
#define BIT_MAC_PORT_EN_8197F BIT(21)
#define BIT_BOOT_FSPI_EN_8197F BIT(20)
#define BIT_ROM_DLEN_8197F BIT(19)

#define BIT_SHIFT_ROM_PGE_8197F 16
#define BIT_MASK_ROM_PGE_8197F 0x7
#define BIT_ROM_PGE_8197F(x)                                                   \
	(((x) & BIT_MASK_ROM_PGE_8197F) << BIT_SHIFT_ROM_PGE_8197F)
#define BITS_ROM_PGE_8197F (BIT_MASK_ROM_PGE_8197F << BIT_SHIFT_ROM_PGE_8197F)
#define BIT_CLEAR_ROM_PGE_8197F(x) ((x) & (~BITS_ROM_PGE_8197F))
#define BIT_GET_ROM_PGE_8197F(x)                                               \
	(((x) >> BIT_SHIFT_ROM_PGE_8197F) & BIT_MASK_ROM_PGE_8197F)
#define BIT_SET_ROM_PGE_8197F(x, v)                                            \
	(BIT_CLEAR_ROM_PGE_8197F(x) | BIT_ROM_PGE_8197F(v))

#define BIT_FW_INIT_RDY_8197F BIT(15)
#define BIT_FW_DW_RDY_8197F BIT(14)

#define BIT_SHIFT_CPU_CLK_SEL_8197F 12
#define BIT_MASK_CPU_CLK_SEL_8197F 0x3
#define BIT_CPU_CLK_SEL_8197F(x)                                               \
	(((x) & BIT_MASK_CPU_CLK_SEL_8197F) << BIT_SHIFT_CPU_CLK_SEL_8197F)
#define BITS_CPU_CLK_SEL_8197F                                                 \
	(BIT_MASK_CPU_CLK_SEL_8197F << BIT_SHIFT_CPU_CLK_SEL_8197F)
#define BIT_CLEAR_CPU_CLK_SEL_8197F(x) ((x) & (~BITS_CPU_CLK_SEL_8197F))
#define BIT_GET_CPU_CLK_SEL_8197F(x)                                           \
	(((x) >> BIT_SHIFT_CPU_CLK_SEL_8197F) & BIT_MASK_CPU_CLK_SEL_8197F)
#define BIT_SET_CPU_CLK_SEL_8197F(x, v)                                        \
	(BIT_CLEAR_CPU_CLK_SEL_8197F(x) | BIT_CPU_CLK_SEL_8197F(v))

#define BIT_CCLK_CHG_MASK_8197F BIT(11)
#define BIT_FW_INIT_RDY_V1_8197F BIT(10)
#define BIT_R_8051_SPD_8197F BIT(9)
#define BIT_MCU_CLK_EN_8197F BIT(8)
#define BIT_RAM_DL_SEL_8197F BIT(7)
#define BIT_WINTINI_RDY_8197F BIT(6)
#define BIT_RF_INIT_RDY_8197F BIT(5)
#define BIT_BB_INIT_RDY_8197F BIT(4)
#define BIT_MAC_INIT_RDY_8197F BIT(3)
#define BIT_MCU_FWDL_RDY_8197F BIT(1)
#define BIT_MCU_FWDL_EN_8197F BIT(0)

/* 2 REG_MCU_TST_CFG_8197F */

#define BIT_SHIFT_LBKTST_8197F 0
#define BIT_MASK_LBKTST_8197F 0xffff
#define BIT_LBKTST_8197F(x)                                                    \
	(((x) & BIT_MASK_LBKTST_8197F) << BIT_SHIFT_LBKTST_8197F)
#define BITS_LBKTST_8197F (BIT_MASK_LBKTST_8197F << BIT_SHIFT_LBKTST_8197F)
#define BIT_CLEAR_LBKTST_8197F(x) ((x) & (~BITS_LBKTST_8197F))
#define BIT_GET_LBKTST_8197F(x)                                                \
	(((x) >> BIT_SHIFT_LBKTST_8197F) & BIT_MASK_LBKTST_8197F)
#define BIT_SET_LBKTST_8197F(x, v)                                             \
	(BIT_CLEAR_LBKTST_8197F(x) | BIT_LBKTST_8197F(v))

/* 2 REG_HMEBOX_E0_E1_8197F */

#define BIT_SHIFT_HOST_MSG_E1_8197F 16
#define BIT_MASK_HOST_MSG_E1_8197F 0xffff
#define BIT_HOST_MSG_E1_8197F(x)                                               \
	(((x) & BIT_MASK_HOST_MSG_E1_8197F) << BIT_SHIFT_HOST_MSG_E1_8197F)
#define BITS_HOST_MSG_E1_8197F                                                 \
	(BIT_MASK_HOST_MSG_E1_8197F << BIT_SHIFT_HOST_MSG_E1_8197F)
#define BIT_CLEAR_HOST_MSG_E1_8197F(x) ((x) & (~BITS_HOST_MSG_E1_8197F))
#define BIT_GET_HOST_MSG_E1_8197F(x)                                           \
	(((x) >> BIT_SHIFT_HOST_MSG_E1_8197F) & BIT_MASK_HOST_MSG_E1_8197F)
#define BIT_SET_HOST_MSG_E1_8197F(x, v)                                        \
	(BIT_CLEAR_HOST_MSG_E1_8197F(x) | BIT_HOST_MSG_E1_8197F(v))

#define BIT_SHIFT_HOST_MSG_E0_8197F 0
#define BIT_MASK_HOST_MSG_E0_8197F 0xffff
#define BIT_HOST_MSG_E0_8197F(x)                                               \
	(((x) & BIT_MASK_HOST_MSG_E0_8197F) << BIT_SHIFT_HOST_MSG_E0_8197F)
#define BITS_HOST_MSG_E0_8197F                                                 \
	(BIT_MASK_HOST_MSG_E0_8197F << BIT_SHIFT_HOST_MSG_E0_8197F)
#define BIT_CLEAR_HOST_MSG_E0_8197F(x) ((x) & (~BITS_HOST_MSG_E0_8197F))
#define BIT_GET_HOST_MSG_E0_8197F(x)                                           \
	(((x) >> BIT_SHIFT_HOST_MSG_E0_8197F) & BIT_MASK_HOST_MSG_E0_8197F)
#define BIT_SET_HOST_MSG_E0_8197F(x, v)                                        \
	(BIT_CLEAR_HOST_MSG_E0_8197F(x) | BIT_HOST_MSG_E0_8197F(v))

/* 2 REG_HMEBOX_E2_E3_8197F */

#define BIT_SHIFT_HOST_MSG_E3_8197F 16
#define BIT_MASK_HOST_MSG_E3_8197F 0xffff
#define BIT_HOST_MSG_E3_8197F(x)                                               \
	(((x) & BIT_MASK_HOST_MSG_E3_8197F) << BIT_SHIFT_HOST_MSG_E3_8197F)
#define BITS_HOST_MSG_E3_8197F                                                 \
	(BIT_MASK_HOST_MSG_E3_8197F << BIT_SHIFT_HOST_MSG_E3_8197F)
#define BIT_CLEAR_HOST_MSG_E3_8197F(x) ((x) & (~BITS_HOST_MSG_E3_8197F))
#define BIT_GET_HOST_MSG_E3_8197F(x)                                           \
	(((x) >> BIT_SHIFT_HOST_MSG_E3_8197F) & BIT_MASK_HOST_MSG_E3_8197F)
#define BIT_SET_HOST_MSG_E3_8197F(x, v)                                        \
	(BIT_CLEAR_HOST_MSG_E3_8197F(x) | BIT_HOST_MSG_E3_8197F(v))

#define BIT_SHIFT_HOST_MSG_E2_8197F 0
#define BIT_MASK_HOST_MSG_E2_8197F 0xffff
#define BIT_HOST_MSG_E2_8197F(x)                                               \
	(((x) & BIT_MASK_HOST_MSG_E2_8197F) << BIT_SHIFT_HOST_MSG_E2_8197F)
#define BITS_HOST_MSG_E2_8197F                                                 \
	(BIT_MASK_HOST_MSG_E2_8197F << BIT_SHIFT_HOST_MSG_E2_8197F)
#define BIT_CLEAR_HOST_MSG_E2_8197F(x) ((x) & (~BITS_HOST_MSG_E2_8197F))
#define BIT_GET_HOST_MSG_E2_8197F(x)                                           \
	(((x) >> BIT_SHIFT_HOST_MSG_E2_8197F) & BIT_MASK_HOST_MSG_E2_8197F)
#define BIT_SET_HOST_MSG_E2_8197F(x, v)                                        \
	(BIT_CLEAR_HOST_MSG_E2_8197F(x) | BIT_HOST_MSG_E2_8197F(v))

/* 2 REG_WLLPS_CTRL_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_AFE_CTRL5_8197F */
#define BIT_BB_DBG_SEL_AFE_SDM_V3_8197F BIT(31)
#define BIT_ORDER_SDM_8197F BIT(30)
#define BIT_RFE_SEL_SDM_8197F BIT(29)

#define BIT_SHIFT_REF_SEL_8197F 25
#define BIT_MASK_REF_SEL_8197F 0xf
#define BIT_REF_SEL_8197F(x)                                                   \
	(((x) & BIT_MASK_REF_SEL_8197F) << BIT_SHIFT_REF_SEL_8197F)
#define BITS_REF_SEL_8197F (BIT_MASK_REF_SEL_8197F << BIT_SHIFT_REF_SEL_8197F)
#define BIT_CLEAR_REF_SEL_8197F(x) ((x) & (~BITS_REF_SEL_8197F))
#define BIT_GET_REF_SEL_8197F(x)                                               \
	(((x) >> BIT_SHIFT_REF_SEL_8197F) & BIT_MASK_REF_SEL_8197F)
#define BIT_SET_REF_SEL_8197F(x, v)                                            \
	(BIT_CLEAR_REF_SEL_8197F(x) | BIT_REF_SEL_8197F(v))

#define BIT_SHIFT_F0F_SDM_V2_8197F 12
#define BIT_MASK_F0F_SDM_V2_8197F 0x1fff
#define BIT_F0F_SDM_V2_8197F(x)                                                \
	(((x) & BIT_MASK_F0F_SDM_V2_8197F) << BIT_SHIFT_F0F_SDM_V2_8197F)
#define BITS_F0F_SDM_V2_8197F                                                  \
	(BIT_MASK_F0F_SDM_V2_8197F << BIT_SHIFT_F0F_SDM_V2_8197F)
#define BIT_CLEAR_F0F_SDM_V2_8197F(x) ((x) & (~BITS_F0F_SDM_V2_8197F))
#define BIT_GET_F0F_SDM_V2_8197F(x)                                            \
	(((x) >> BIT_SHIFT_F0F_SDM_V2_8197F) & BIT_MASK_F0F_SDM_V2_8197F)
#define BIT_SET_F0F_SDM_V2_8197F(x, v)                                         \
	(BIT_CLEAR_F0F_SDM_V2_8197F(x) | BIT_F0F_SDM_V2_8197F(v))

#define BIT_SHIFT_F0N_SDM_V2_8197F 9
#define BIT_MASK_F0N_SDM_V2_8197F 0x7
#define BIT_F0N_SDM_V2_8197F(x)                                                \
	(((x) & BIT_MASK_F0N_SDM_V2_8197F) << BIT_SHIFT_F0N_SDM_V2_8197F)
#define BITS_F0N_SDM_V2_8197F                                                  \
	(BIT_MASK_F0N_SDM_V2_8197F << BIT_SHIFT_F0N_SDM_V2_8197F)
#define BIT_CLEAR_F0N_SDM_V2_8197F(x) ((x) & (~BITS_F0N_SDM_V2_8197F))
#define BIT_GET_F0N_SDM_V2_8197F(x)                                            \
	(((x) >> BIT_SHIFT_F0N_SDM_V2_8197F) & BIT_MASK_F0N_SDM_V2_8197F)
#define BIT_SET_F0N_SDM_V2_8197F(x, v)                                         \
	(BIT_CLEAR_F0N_SDM_V2_8197F(x) | BIT_F0N_SDM_V2_8197F(v))

#define BIT_SHIFT_DIVN_SDM_V2_8197F 3
#define BIT_MASK_DIVN_SDM_V2_8197F 0x3f
#define BIT_DIVN_SDM_V2_8197F(x)                                               \
	(((x) & BIT_MASK_DIVN_SDM_V2_8197F) << BIT_SHIFT_DIVN_SDM_V2_8197F)
#define BITS_DIVN_SDM_V2_8197F                                                 \
	(BIT_MASK_DIVN_SDM_V2_8197F << BIT_SHIFT_DIVN_SDM_V2_8197F)
#define BIT_CLEAR_DIVN_SDM_V2_8197F(x) ((x) & (~BITS_DIVN_SDM_V2_8197F))
#define BIT_GET_DIVN_SDM_V2_8197F(x)                                           \
	(((x) >> BIT_SHIFT_DIVN_SDM_V2_8197F) & BIT_MASK_DIVN_SDM_V2_8197F)
#define BIT_SET_DIVN_SDM_V2_8197F(x, v)                                        \
	(BIT_CLEAR_DIVN_SDM_V2_8197F(x) | BIT_DIVN_SDM_V2_8197F(v))

#define BIT_SHIFT_DITHER_SDM_V2_8197F 0
#define BIT_MASK_DITHER_SDM_V2_8197F 0x7
#define BIT_DITHER_SDM_V2_8197F(x)                                             \
	(((x) & BIT_MASK_DITHER_SDM_V2_8197F) << BIT_SHIFT_DITHER_SDM_V2_8197F)
#define BITS_DITHER_SDM_V2_8197F                                               \
	(BIT_MASK_DITHER_SDM_V2_8197F << BIT_SHIFT_DITHER_SDM_V2_8197F)
#define BIT_CLEAR_DITHER_SDM_V2_8197F(x) ((x) & (~BITS_DITHER_SDM_V2_8197F))
#define BIT_GET_DITHER_SDM_V2_8197F(x)                                         \
	(((x) >> BIT_SHIFT_DITHER_SDM_V2_8197F) & BIT_MASK_DITHER_SDM_V2_8197F)
#define BIT_SET_DITHER_SDM_V2_8197F(x, v)                                      \
	(BIT_CLEAR_DITHER_SDM_V2_8197F(x) | BIT_DITHER_SDM_V2_8197F(v))

/* 2 REG_GPIO_DEBOUNCE_CTRL_8197F */
#define BIT_WLGP_DBC1EN_8197F BIT(15)

#define BIT_SHIFT_WLGP_DBC1_8197F 8
#define BIT_MASK_WLGP_DBC1_8197F 0xf
#define BIT_WLGP_DBC1_8197F(x)                                                 \
	(((x) & BIT_MASK_WLGP_DBC1_8197F) << BIT_SHIFT_WLGP_DBC1_8197F)
#define BITS_WLGP_DBC1_8197F                                                   \
	(BIT_MASK_WLGP_DBC1_8197F << BIT_SHIFT_WLGP_DBC1_8197F)
#define BIT_CLEAR_WLGP_DBC1_8197F(x) ((x) & (~BITS_WLGP_DBC1_8197F))
#define BIT_GET_WLGP_DBC1_8197F(x)                                             \
	(((x) >> BIT_SHIFT_WLGP_DBC1_8197F) & BIT_MASK_WLGP_DBC1_8197F)
#define BIT_SET_WLGP_DBC1_8197F(x, v)                                          \
	(BIT_CLEAR_WLGP_DBC1_8197F(x) | BIT_WLGP_DBC1_8197F(v))

#define BIT_WLGP_DBC0EN_8197F BIT(7)

#define BIT_SHIFT_WLGP_DBC0_8197F 0
#define BIT_MASK_WLGP_DBC0_8197F 0xf
#define BIT_WLGP_DBC0_8197F(x)                                                 \
	(((x) & BIT_MASK_WLGP_DBC0_8197F) << BIT_SHIFT_WLGP_DBC0_8197F)
#define BITS_WLGP_DBC0_8197F                                                   \
	(BIT_MASK_WLGP_DBC0_8197F << BIT_SHIFT_WLGP_DBC0_8197F)
#define BIT_CLEAR_WLGP_DBC0_8197F(x) ((x) & (~BITS_WLGP_DBC0_8197F))
#define BIT_GET_WLGP_DBC0_8197F(x)                                             \
	(((x) >> BIT_SHIFT_WLGP_DBC0_8197F) & BIT_MASK_WLGP_DBC0_8197F)
#define BIT_SET_WLGP_DBC0_8197F(x, v)                                          \
	(BIT_CLEAR_WLGP_DBC0_8197F(x) | BIT_WLGP_DBC0_8197F(v))

/* 2 REG_RPWM2_8197F */

#define BIT_SHIFT_RPWM2_8197F 16
#define BIT_MASK_RPWM2_8197F 0xffff
#define BIT_RPWM2_8197F(x)                                                     \
	(((x) & BIT_MASK_RPWM2_8197F) << BIT_SHIFT_RPWM2_8197F)
#define BITS_RPWM2_8197F (BIT_MASK_RPWM2_8197F << BIT_SHIFT_RPWM2_8197F)
#define BIT_CLEAR_RPWM2_8197F(x) ((x) & (~BITS_RPWM2_8197F))
#define BIT_GET_RPWM2_8197F(x)                                                 \
	(((x) >> BIT_SHIFT_RPWM2_8197F) & BIT_MASK_RPWM2_8197F)
#define BIT_SET_RPWM2_8197F(x, v)                                              \
	(BIT_CLEAR_RPWM2_8197F(x) | BIT_RPWM2_8197F(v))

/* 2 REG_SYSON_FSM_MON_8197F */

#define BIT_SHIFT_FSM_MON_SEL_8197F 24
#define BIT_MASK_FSM_MON_SEL_8197F 0x7
#define BIT_FSM_MON_SEL_8197F(x)                                               \
	(((x) & BIT_MASK_FSM_MON_SEL_8197F) << BIT_SHIFT_FSM_MON_SEL_8197F)
#define BITS_FSM_MON_SEL_8197F                                                 \
	(BIT_MASK_FSM_MON_SEL_8197F << BIT_SHIFT_FSM_MON_SEL_8197F)
#define BIT_CLEAR_FSM_MON_SEL_8197F(x) ((x) & (~BITS_FSM_MON_SEL_8197F))
#define BIT_GET_FSM_MON_SEL_8197F(x)                                           \
	(((x) >> BIT_SHIFT_FSM_MON_SEL_8197F) & BIT_MASK_FSM_MON_SEL_8197F)
#define BIT_SET_FSM_MON_SEL_8197F(x, v)                                        \
	(BIT_CLEAR_FSM_MON_SEL_8197F(x) | BIT_FSM_MON_SEL_8197F(v))

#define BIT_DOP_ELDO_8197F BIT(23)
#define BIT_FSM_MON_UPD_8197F BIT(15)

#define BIT_SHIFT_FSM_PAR_8197F 0
#define BIT_MASK_FSM_PAR_8197F 0x7fff
#define BIT_FSM_PAR_8197F(x)                                                   \
	(((x) & BIT_MASK_FSM_PAR_8197F) << BIT_SHIFT_FSM_PAR_8197F)
#define BITS_FSM_PAR_8197F (BIT_MASK_FSM_PAR_8197F << BIT_SHIFT_FSM_PAR_8197F)
#define BIT_CLEAR_FSM_PAR_8197F(x) ((x) & (~BITS_FSM_PAR_8197F))
#define BIT_GET_FSM_PAR_8197F(x)                                               \
	(((x) >> BIT_SHIFT_FSM_PAR_8197F) & BIT_MASK_FSM_PAR_8197F)
#define BIT_SET_FSM_PAR_8197F(x, v)                                            \
	(BIT_CLEAR_FSM_PAR_8197F(x) | BIT_FSM_PAR_8197F(v))

/* 2 REG_AFE_CTRL6_8197F */

#define BIT_SHIFT_TSFT_SEL_V1_8197F 0
#define BIT_MASK_TSFT_SEL_V1_8197F 0x7
#define BIT_TSFT_SEL_V1_8197F(x)                                               \
	(((x) & BIT_MASK_TSFT_SEL_V1_8197F) << BIT_SHIFT_TSFT_SEL_V1_8197F)
#define BITS_TSFT_SEL_V1_8197F                                                 \
	(BIT_MASK_TSFT_SEL_V1_8197F << BIT_SHIFT_TSFT_SEL_V1_8197F)
#define BIT_CLEAR_TSFT_SEL_V1_8197F(x) ((x) & (~BITS_TSFT_SEL_V1_8197F))
#define BIT_GET_TSFT_SEL_V1_8197F(x)                                           \
	(((x) >> BIT_SHIFT_TSFT_SEL_V1_8197F) & BIT_MASK_TSFT_SEL_V1_8197F)
#define BIT_SET_TSFT_SEL_V1_8197F(x, v)                                        \
	(BIT_CLEAR_TSFT_SEL_V1_8197F(x) | BIT_TSFT_SEL_V1_8197F(v))

/* 2 REG_PMC_DBG_CTRL1_8197F */
#define BIT_BT_INT_EN_8197F BIT(31)

#define BIT_SHIFT_RD_WR_WIFI_BT_INFO_8197F 16
#define BIT_MASK_RD_WR_WIFI_BT_INFO_8197F 0x7fff
#define BIT_RD_WR_WIFI_BT_INFO_8197F(x)                                        \
	(((x) & BIT_MASK_RD_WR_WIFI_BT_INFO_8197F)                             \
	 << BIT_SHIFT_RD_WR_WIFI_BT_INFO_8197F)
#define BITS_RD_WR_WIFI_BT_INFO_8197F                                          \
	(BIT_MASK_RD_WR_WIFI_BT_INFO_8197F                                     \
	 << BIT_SHIFT_RD_WR_WIFI_BT_INFO_8197F)
#define BIT_CLEAR_RD_WR_WIFI_BT_INFO_8197F(x)                                  \
	((x) & (~BITS_RD_WR_WIFI_BT_INFO_8197F))
#define BIT_GET_RD_WR_WIFI_BT_INFO_8197F(x)                                    \
	(((x) >> BIT_SHIFT_RD_WR_WIFI_BT_INFO_8197F) &                         \
	 BIT_MASK_RD_WR_WIFI_BT_INFO_8197F)
#define BIT_SET_RD_WR_WIFI_BT_INFO_8197F(x, v)                                 \
	(BIT_CLEAR_RD_WR_WIFI_BT_INFO_8197F(x) |                               \
	 BIT_RD_WR_WIFI_BT_INFO_8197F(v))

#define BIT_PMC_WR_OVF_8197F BIT(8)

#define BIT_SHIFT_WLPMC_ERRINT_8197F 0
#define BIT_MASK_WLPMC_ERRINT_8197F 0xff
#define BIT_WLPMC_ERRINT_8197F(x)                                              \
	(((x) & BIT_MASK_WLPMC_ERRINT_8197F) << BIT_SHIFT_WLPMC_ERRINT_8197F)
#define BITS_WLPMC_ERRINT_8197F                                                \
	(BIT_MASK_WLPMC_ERRINT_8197F << BIT_SHIFT_WLPMC_ERRINT_8197F)
#define BIT_CLEAR_WLPMC_ERRINT_8197F(x) ((x) & (~BITS_WLPMC_ERRINT_8197F))
#define BIT_GET_WLPMC_ERRINT_8197F(x)                                          \
	(((x) >> BIT_SHIFT_WLPMC_ERRINT_8197F) & BIT_MASK_WLPMC_ERRINT_8197F)
#define BIT_SET_WLPMC_ERRINT_8197F(x, v)                                       \
	(BIT_CLEAR_WLPMC_ERRINT_8197F(x) | BIT_WLPMC_ERRINT_8197F(v))

/* 2 REG_AFE_CTRL7_8197F */

#define BIT_SHIFT_SEL_V_8197F 30
#define BIT_MASK_SEL_V_8197F 0x3
#define BIT_SEL_V_8197F(x)                                                     \
	(((x) & BIT_MASK_SEL_V_8197F) << BIT_SHIFT_SEL_V_8197F)
#define BITS_SEL_V_8197F (BIT_MASK_SEL_V_8197F << BIT_SHIFT_SEL_V_8197F)
#define BIT_CLEAR_SEL_V_8197F(x) ((x) & (~BITS_SEL_V_8197F))
#define BIT_GET_SEL_V_8197F(x)                                                 \
	(((x) >> BIT_SHIFT_SEL_V_8197F) & BIT_MASK_SEL_V_8197F)
#define BIT_SET_SEL_V_8197F(x, v)                                              \
	(BIT_CLEAR_SEL_V_8197F(x) | BIT_SEL_V_8197F(v))

#define BIT_SEL_LDO_PC_8197F BIT(29)

#define BIT_SHIFT_CK_MON_SEL_V2_8197F 26
#define BIT_MASK_CK_MON_SEL_V2_8197F 0x7
#define BIT_CK_MON_SEL_V2_8197F(x)                                             \
	(((x) & BIT_MASK_CK_MON_SEL_V2_8197F) << BIT_SHIFT_CK_MON_SEL_V2_8197F)
#define BITS_CK_MON_SEL_V2_8197F                                               \
	(BIT_MASK_CK_MON_SEL_V2_8197F << BIT_SHIFT_CK_MON_SEL_V2_8197F)
#define BIT_CLEAR_CK_MON_SEL_V2_8197F(x) ((x) & (~BITS_CK_MON_SEL_V2_8197F))
#define BIT_GET_CK_MON_SEL_V2_8197F(x)                                         \
	(((x) >> BIT_SHIFT_CK_MON_SEL_V2_8197F) & BIT_MASK_CK_MON_SEL_V2_8197F)
#define BIT_SET_CK_MON_SEL_V2_8197F(x, v)                                      \
	(BIT_CLEAR_CK_MON_SEL_V2_8197F(x) | BIT_CK_MON_SEL_V2_8197F(v))

#define BIT_CK_MON_EN_8197F BIT(25)
#define BIT_FREF_EDGE_8197F BIT(24)
#define BIT_CK320M_EN_8197F BIT(23)
#define BIT_CK_5M_EN_8197F BIT(22)
#define BIT_TESTEN_8197F BIT(21)

/* 2 REG_HIMR0_8197F */
#define BIT_TIMEOUT_INTERRUPT2_MASK_8197F BIT(31)
#define BIT_TIMEOUT_INTERRUTP1_MASK_8197F BIT(30)
#define BIT_PSTIMEOUT_MSK_8197F BIT(29)
#define BIT_GTINT4_MSK_8197F BIT(28)
#define BIT_GTINT3_MSK_8197F BIT(27)
#define BIT_TXBCN0ERR_MSK_8197F BIT(26)
#define BIT_TXBCN0OK_MSK_8197F BIT(25)
#define BIT_TSF_BIT32_TOGGLE_MSK_8197F BIT(24)
#define BIT_BCNDMAINT0_MSK_8197F BIT(20)
#define BIT_BCNDERR0_MSK_8197F BIT(16)
#define BIT_HSISR_IND_ON_INT_MSK_8197F BIT(15)
#define BIT_HISR3_IND_INT_MSK_8197F BIT(14)
#define BIT_HISR2_IND_INT_MSK_8197F BIT(13)
#define BIT_CTWEND_MSK_8197F BIT(12)
#define BIT_HISR1_IND_MSK_8197F BIT(11)
#define BIT_C2HCMD_MSK_8197F BIT(10)
#define BIT_CPWM2_MSK_8197F BIT(9)
#define BIT_CPWM_MSK_8197F BIT(8)
#define BIT_HIGHDOK_MSK_8197F BIT(7)
#define BIT_MGTDOK_MSK_8197F BIT(6)
#define BIT_BKDOK_MSK_8197F BIT(5)
#define BIT_BEDOK_MSK_8197F BIT(4)
#define BIT_VIDOK_MSK_8197F BIT(3)
#define BIT_VODOK_MSK_8197F BIT(2)
#define BIT_RDU_MSK_8197F BIT(1)
#define BIT_RXOK_MSK_8197F BIT(0)

/* 2 REG_HISR0_8197F */
#define BIT_PSTIMEOUT2_8197F BIT(31)
#define BIT_PSTIMEOUT1_8197F BIT(30)
#define BIT_PSTIMEOUT_8197F BIT(29)
#define BIT_GTINT4_8197F BIT(28)
#define BIT_GTINT3_8197F BIT(27)
#define BIT_TXBCN0ERR_8197F BIT(26)
#define BIT_TXBCN0OK_8197F BIT(25)
#define BIT_TSF_BIT32_TOGGLE_8197F BIT(24)
#define BIT_BCNDMAINT0_8197F BIT(20)
#define BIT_BCNDERR0_8197F BIT(16)
#define BIT_HSISR_IND_ON_INT_8197F BIT(15)
#define BIT_HISR3_IND_INT_8197F BIT(14)
#define BIT_HISR2_IND_INT_8197F BIT(13)
#define BIT_CTWEND_8197F BIT(12)
#define BIT_HISR1_IND_INT_8197F BIT(11)
#define BIT_C2HCMD_8197F BIT(10)
#define BIT_CPWM2_8197F BIT(9)
#define BIT_CPWM_8197F BIT(8)
#define BIT_HIGHDOK_8197F BIT(7)
#define BIT_MGTDOK_8197F BIT(6)
#define BIT_BKDOK_8197F BIT(5)
#define BIT_BEDOK_8197F BIT(4)
#define BIT_VIDOK_8197F BIT(3)
#define BIT_VODOK_8197F BIT(2)
#define BIT_RDU_8197F BIT(1)
#define BIT_RXOK_8197F BIT(0)

/* 2 REG_HIMR1_8197F */
#define BIT_BTON_STS_UPDATE_MSK_8197F BIT(29)
#define BIT_MCU_ERR_MASK_8197F BIT(28)
#define BIT_BCNDMAINT7__MSK_8197F BIT(27)
#define BIT_BCNDMAINT6__MSK_8197F BIT(26)
#define BIT_BCNDMAINT5__MSK_8197F BIT(25)
#define BIT_BCNDMAINT4__MSK_8197F BIT(24)
#define BIT_BCNDMAINT3_MSK_8197F BIT(23)
#define BIT_BCNDMAINT2_MSK_8197F BIT(22)
#define BIT_BCNDMAINT1_MSK_8197F BIT(21)
#define BIT_BCNDERR7_MSK_8197F BIT(20)
#define BIT_BCNDERR6_MSK_8197F BIT(19)
#define BIT_BCNDERR5_MSK_8197F BIT(18)
#define BIT_BCNDERR4_MSK_8197F BIT(17)
#define BIT_BCNDERR3_MSK_8197F BIT(16)
#define BIT_BCNDERR2_MSK_8197F BIT(15)
#define BIT_BCNDERR1_MSK_8197F BIT(14)
#define BIT_ATIMEND_E_MSK_8197F BIT(13)
#define BIT_ATIMEND__MSK_8197F BIT(12)
#define BIT_TXERR_MSK_8197F BIT(11)
#define BIT_RXERR_MSK_8197F BIT(10)
#define BIT_TXFOVW_MSK_8197F BIT(9)
#define BIT_FOVW_MSK_8197F BIT(8)

/* 2 REG_HISR1_8197F */
#define BIT_BTON_STS_UPDATE_INT_8197F BIT(29)
#define BIT_MCU_ERR_8197F BIT(28)
#define BIT_BCNDMAINT7_8197F BIT(27)
#define BIT_BCNDMAINT6_8197F BIT(26)
#define BIT_BCNDMAINT5_8197F BIT(25)
#define BIT_BCNDMAINT4_8197F BIT(24)
#define BIT_BCNDMAINT3_8197F BIT(23)
#define BIT_BCNDMAINT2_8197F BIT(22)
#define BIT_BCNDMAINT1_8197F BIT(21)
#define BIT_BCNDERR7_8197F BIT(20)
#define BIT_BCNDERR6_8197F BIT(19)
#define BIT_BCNDERR5_8197F BIT(18)
#define BIT_BCNDERR4_8197F BIT(17)
#define BIT_BCNDERR3_8197F BIT(16)
#define BIT_BCNDERR2_8197F BIT(15)
#define BIT_BCNDERR1_8197F BIT(14)
#define BIT_ATIMEND_E_8197F BIT(13)
#define BIT_ATIMEND_8197F BIT(12)
#define BIT_TXERR_INT_8197F BIT(11)
#define BIT_RXERR_INT_8197F BIT(10)
#define BIT_TXFOVW_8197F BIT(9)
#define BIT_FOVW_8197F BIT(8)

/* 2 REG_DBG_PORT_SEL_8197F */

#define BIT_SHIFT_DEBUG_ST_8197F 0
#define BIT_MASK_DEBUG_ST_8197F 0xffffffffL
#define BIT_DEBUG_ST_8197F(x)                                                  \
	(((x) & BIT_MASK_DEBUG_ST_8197F) << BIT_SHIFT_DEBUG_ST_8197F)
#define BITS_DEBUG_ST_8197F                                                    \
	(BIT_MASK_DEBUG_ST_8197F << BIT_SHIFT_DEBUG_ST_8197F)
#define BIT_CLEAR_DEBUG_ST_8197F(x) ((x) & (~BITS_DEBUG_ST_8197F))
#define BIT_GET_DEBUG_ST_8197F(x)                                              \
	(((x) >> BIT_SHIFT_DEBUG_ST_8197F) & BIT_MASK_DEBUG_ST_8197F)
#define BIT_SET_DEBUG_ST_8197F(x, v)                                           \
	(BIT_CLEAR_DEBUG_ST_8197F(x) | BIT_DEBUG_ST_8197F(v))

/* 2 REG_PAD_CTRL2_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */
#define BIT_LD_B12V_EN_V1_8197F BIT(7)
#define BIT_EECS_IOSEL_V1_8197F BIT(6)
#define BIT_EECS_DATA_O_V1_8197F BIT(5)
#define BIT_EECS_DATA_I_V1_8197F BIT(4)
#define BIT_EESK_IOSEL_V1_8197F BIT(2)
#define BIT_EESK_DATA_O_V1_8197F BIT(1)
#define BIT_EESK_DATA_I_V1_8197F BIT(0)

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_PMC_DBG_CTRL2_8197F */

#define BIT_SHIFT_EFUSE_BURN_GNT_8197F 24
#define BIT_MASK_EFUSE_BURN_GNT_8197F 0xff
#define BIT_EFUSE_BURN_GNT_8197F(x)                                            \
	(((x) & BIT_MASK_EFUSE_BURN_GNT_8197F)                                 \
	 << BIT_SHIFT_EFUSE_BURN_GNT_8197F)
#define BITS_EFUSE_BURN_GNT_8197F                                              \
	(BIT_MASK_EFUSE_BURN_GNT_8197F << BIT_SHIFT_EFUSE_BURN_GNT_8197F)
#define BIT_CLEAR_EFUSE_BURN_GNT_8197F(x) ((x) & (~BITS_EFUSE_BURN_GNT_8197F))
#define BIT_GET_EFUSE_BURN_GNT_8197F(x)                                        \
	(((x) >> BIT_SHIFT_EFUSE_BURN_GNT_8197F) &                             \
	 BIT_MASK_EFUSE_BURN_GNT_8197F)
#define BIT_SET_EFUSE_BURN_GNT_8197F(x, v)                                     \
	(BIT_CLEAR_EFUSE_BURN_GNT_8197F(x) | BIT_EFUSE_BURN_GNT_8197F(v))

#define BIT_STOP_WL_PMC_8197F BIT(9)
#define BIT_STOP_SYM_PMC_8197F BIT(8)
#define BIT_REG_RST_WLPMC_8197F BIT(5)
#define BIT_REG_RST_PD12N_8197F BIT(4)
#define BIT_SYSON_DIS_WLREG_WRMSK_8197F BIT(3)
#define BIT_SYSON_DIS_PMCREG_WRMSK_8197F BIT(2)

#define BIT_SHIFT_SYSON_REG_ARB_8197F 0
#define BIT_MASK_SYSON_REG_ARB_8197F 0x3
#define BIT_SYSON_REG_ARB_8197F(x)                                             \
	(((x) & BIT_MASK_SYSON_REG_ARB_8197F) << BIT_SHIFT_SYSON_REG_ARB_8197F)
#define BITS_SYSON_REG_ARB_8197F                                               \
	(BIT_MASK_SYSON_REG_ARB_8197F << BIT_SHIFT_SYSON_REG_ARB_8197F)
#define BIT_CLEAR_SYSON_REG_ARB_8197F(x) ((x) & (~BITS_SYSON_REG_ARB_8197F))
#define BIT_GET_SYSON_REG_ARB_8197F(x)                                         \
	(((x) >> BIT_SHIFT_SYSON_REG_ARB_8197F) & BIT_MASK_SYSON_REG_ARB_8197F)
#define BIT_SET_SYSON_REG_ARB_8197F(x, v)                                      \
	(BIT_CLEAR_SYSON_REG_ARB_8197F(x) | BIT_SYSON_REG_ARB_8197F(v))

/* 2 REG_BIST_CTRL_8197F */
#define BIT_BIST_USB_DIS_8197F BIT(27)
#define BIT_BIST_PCI_DIS_8197F BIT(26)
#define BIT_BIST_BT_DIS_8197F BIT(25)
#define BIT_BIST_WL_DIS_8197F BIT(24)

#define BIT_SHIFT_BIST_RPT_SEL_8197F 16
#define BIT_MASK_BIST_RPT_SEL_8197F 0xf
#define BIT_BIST_RPT_SEL_8197F(x)                                              \
	(((x) & BIT_MASK_BIST_RPT_SEL_8197F) << BIT_SHIFT_BIST_RPT_SEL_8197F)
#define BITS_BIST_RPT_SEL_8197F                                                \
	(BIT_MASK_BIST_RPT_SEL_8197F << BIT_SHIFT_BIST_RPT_SEL_8197F)
#define BIT_CLEAR_BIST_RPT_SEL_8197F(x) ((x) & (~BITS_BIST_RPT_SEL_8197F))
#define BIT_GET_BIST_RPT_SEL_8197F(x)                                          \
	(((x) >> BIT_SHIFT_BIST_RPT_SEL_8197F) & BIT_MASK_BIST_RPT_SEL_8197F)
#define BIT_SET_BIST_RPT_SEL_8197F(x, v)                                       \
	(BIT_CLEAR_BIST_RPT_SEL_8197F(x) | BIT_BIST_RPT_SEL_8197F(v))

#define BIT_BIST_RESUME_PS_8197F BIT(4)
#define BIT_BIST_RESUME_8197F BIT(3)
#define BIT_BIST_NORMAL_8197F BIT(2)
#define BIT_BIST_RSTN_8197F BIT(1)
#define BIT_BIST_CLK_EN_8197F BIT(0)

/* 2 REG_BIST_RPT_8197F */

#define BIT_SHIFT_MBIST_REPORT_8197F 0
#define BIT_MASK_MBIST_REPORT_8197F 0xffffffffL
#define BIT_MBIST_REPORT_8197F(x)                                              \
	(((x) & BIT_MASK_MBIST_REPORT_8197F) << BIT_SHIFT_MBIST_REPORT_8197F)
#define BITS_MBIST_REPORT_8197F                                                \
	(BIT_MASK_MBIST_REPORT_8197F << BIT_SHIFT_MBIST_REPORT_8197F)
#define BIT_CLEAR_MBIST_REPORT_8197F(x) ((x) & (~BITS_MBIST_REPORT_8197F))
#define BIT_GET_MBIST_REPORT_8197F(x)                                          \
	(((x) >> BIT_SHIFT_MBIST_REPORT_8197F) & BIT_MASK_MBIST_REPORT_8197F)
#define BIT_SET_MBIST_REPORT_8197F(x, v)                                       \
	(BIT_CLEAR_MBIST_REPORT_8197F(x) | BIT_MBIST_REPORT_8197F(v))

/* 2 REG_MEM_CTRL_8197F */
#define BIT_UMEM_RME_8197F BIT(31)

#define BIT_SHIFT_BT_SPRAM_8197F 28
#define BIT_MASK_BT_SPRAM_8197F 0x3
#define BIT_BT_SPRAM_8197F(x)                                                  \
	(((x) & BIT_MASK_BT_SPRAM_8197F) << BIT_SHIFT_BT_SPRAM_8197F)
#define BITS_BT_SPRAM_8197F                                                    \
	(BIT_MASK_BT_SPRAM_8197F << BIT_SHIFT_BT_SPRAM_8197F)
#define BIT_CLEAR_BT_SPRAM_8197F(x) ((x) & (~BITS_BT_SPRAM_8197F))
#define BIT_GET_BT_SPRAM_8197F(x)                                              \
	(((x) >> BIT_SHIFT_BT_SPRAM_8197F) & BIT_MASK_BT_SPRAM_8197F)
#define BIT_SET_BT_SPRAM_8197F(x, v)                                           \
	(BIT_CLEAR_BT_SPRAM_8197F(x) | BIT_BT_SPRAM_8197F(v))

#define BIT_SHIFT_BT_ROM_8197F 24
#define BIT_MASK_BT_ROM_8197F 0xf
#define BIT_BT_ROM_8197F(x)                                                    \
	(((x) & BIT_MASK_BT_ROM_8197F) << BIT_SHIFT_BT_ROM_8197F)
#define BITS_BT_ROM_8197F (BIT_MASK_BT_ROM_8197F << BIT_SHIFT_BT_ROM_8197F)
#define BIT_CLEAR_BT_ROM_8197F(x) ((x) & (~BITS_BT_ROM_8197F))
#define BIT_GET_BT_ROM_8197F(x)                                                \
	(((x) >> BIT_SHIFT_BT_ROM_8197F) & BIT_MASK_BT_ROM_8197F)
#define BIT_SET_BT_ROM_8197F(x, v)                                             \
	(BIT_CLEAR_BT_ROM_8197F(x) | BIT_BT_ROM_8197F(v))

#define BIT_SHIFT_PCI_DPRAM_8197F 10
#define BIT_MASK_PCI_DPRAM_8197F 0x3
#define BIT_PCI_DPRAM_8197F(x)                                                 \
	(((x) & BIT_MASK_PCI_DPRAM_8197F) << BIT_SHIFT_PCI_DPRAM_8197F)
#define BITS_PCI_DPRAM_8197F                                                   \
	(BIT_MASK_PCI_DPRAM_8197F << BIT_SHIFT_PCI_DPRAM_8197F)
#define BIT_CLEAR_PCI_DPRAM_8197F(x) ((x) & (~BITS_PCI_DPRAM_8197F))
#define BIT_GET_PCI_DPRAM_8197F(x)                                             \
	(((x) >> BIT_SHIFT_PCI_DPRAM_8197F) & BIT_MASK_PCI_DPRAM_8197F)
#define BIT_SET_PCI_DPRAM_8197F(x, v)                                          \
	(BIT_CLEAR_PCI_DPRAM_8197F(x) | BIT_PCI_DPRAM_8197F(v))

#define BIT_SHIFT_PCI_SPRAM_8197F 8
#define BIT_MASK_PCI_SPRAM_8197F 0x3
#define BIT_PCI_SPRAM_8197F(x)                                                 \
	(((x) & BIT_MASK_PCI_SPRAM_8197F) << BIT_SHIFT_PCI_SPRAM_8197F)
#define BITS_PCI_SPRAM_8197F                                                   \
	(BIT_MASK_PCI_SPRAM_8197F << BIT_SHIFT_PCI_SPRAM_8197F)
#define BIT_CLEAR_PCI_SPRAM_8197F(x) ((x) & (~BITS_PCI_SPRAM_8197F))
#define BIT_GET_PCI_SPRAM_8197F(x)                                             \
	(((x) >> BIT_SHIFT_PCI_SPRAM_8197F) & BIT_MASK_PCI_SPRAM_8197F)
#define BIT_SET_PCI_SPRAM_8197F(x, v)                                          \
	(BIT_CLEAR_PCI_SPRAM_8197F(x) | BIT_PCI_SPRAM_8197F(v))

#define BIT_SHIFT_USB_SPRAM_8197F 6
#define BIT_MASK_USB_SPRAM_8197F 0x3
#define BIT_USB_SPRAM_8197F(x)                                                 \
	(((x) & BIT_MASK_USB_SPRAM_8197F) << BIT_SHIFT_USB_SPRAM_8197F)
#define BITS_USB_SPRAM_8197F                                                   \
	(BIT_MASK_USB_SPRAM_8197F << BIT_SHIFT_USB_SPRAM_8197F)
#define BIT_CLEAR_USB_SPRAM_8197F(x) ((x) & (~BITS_USB_SPRAM_8197F))
#define BIT_GET_USB_SPRAM_8197F(x)                                             \
	(((x) >> BIT_SHIFT_USB_SPRAM_8197F) & BIT_MASK_USB_SPRAM_8197F)
#define BIT_SET_USB_SPRAM_8197F(x, v)                                          \
	(BIT_CLEAR_USB_SPRAM_8197F(x) | BIT_USB_SPRAM_8197F(v))

#define BIT_SHIFT_USB_SPRF_8197F 4
#define BIT_MASK_USB_SPRF_8197F 0x3
#define BIT_USB_SPRF_8197F(x)                                                  \
	(((x) & BIT_MASK_USB_SPRF_8197F) << BIT_SHIFT_USB_SPRF_8197F)
#define BITS_USB_SPRF_8197F                                                    \
	(BIT_MASK_USB_SPRF_8197F << BIT_SHIFT_USB_SPRF_8197F)
#define BIT_CLEAR_USB_SPRF_8197F(x) ((x) & (~BITS_USB_SPRF_8197F))
#define BIT_GET_USB_SPRF_8197F(x)                                              \
	(((x) >> BIT_SHIFT_USB_SPRF_8197F) & BIT_MASK_USB_SPRF_8197F)
#define BIT_SET_USB_SPRF_8197F(x, v)                                           \
	(BIT_CLEAR_USB_SPRF_8197F(x) | BIT_USB_SPRF_8197F(v))

#define BIT_SHIFT_MCU_ROM_8197F 0
#define BIT_MASK_MCU_ROM_8197F 0xf
#define BIT_MCU_ROM_8197F(x)                                                   \
	(((x) & BIT_MASK_MCU_ROM_8197F) << BIT_SHIFT_MCU_ROM_8197F)
#define BITS_MCU_ROM_8197F (BIT_MASK_MCU_ROM_8197F << BIT_SHIFT_MCU_ROM_8197F)
#define BIT_CLEAR_MCU_ROM_8197F(x) ((x) & (~BITS_MCU_ROM_8197F))
#define BIT_GET_MCU_ROM_8197F(x)                                               \
	(((x) >> BIT_SHIFT_MCU_ROM_8197F) & BIT_MASK_MCU_ROM_8197F)
#define BIT_SET_MCU_ROM_8197F(x, v)                                            \
	(BIT_CLEAR_MCU_ROM_8197F(x) | BIT_MCU_ROM_8197F(v))

/* 2 REG_AFE_CTRL8_8197F */

#define BIT_SHIFT_BB_DBG_SEL_AFE_SDM_V4_8197F 26
#define BIT_MASK_BB_DBG_SEL_AFE_SDM_V4_8197F 0x7
#define BIT_BB_DBG_SEL_AFE_SDM_V4_8197F(x)                                     \
	(((x) & BIT_MASK_BB_DBG_SEL_AFE_SDM_V4_8197F)                          \
	 << BIT_SHIFT_BB_DBG_SEL_AFE_SDM_V4_8197F)
#define BITS_BB_DBG_SEL_AFE_SDM_V4_8197F                                       \
	(BIT_MASK_BB_DBG_SEL_AFE_SDM_V4_8197F                                  \
	 << BIT_SHIFT_BB_DBG_SEL_AFE_SDM_V4_8197F)
#define BIT_CLEAR_BB_DBG_SEL_AFE_SDM_V4_8197F(x)                               \
	((x) & (~BITS_BB_DBG_SEL_AFE_SDM_V4_8197F))
#define BIT_GET_BB_DBG_SEL_AFE_SDM_V4_8197F(x)                                 \
	(((x) >> BIT_SHIFT_BB_DBG_SEL_AFE_SDM_V4_8197F) &                      \
	 BIT_MASK_BB_DBG_SEL_AFE_SDM_V4_8197F)
#define BIT_SET_BB_DBG_SEL_AFE_SDM_V4_8197F(x, v)                              \
	(BIT_CLEAR_BB_DBG_SEL_AFE_SDM_V4_8197F(x) |                            \
	 BIT_BB_DBG_SEL_AFE_SDM_V4_8197F(v))

#define BIT_SYN_AGPIO_8197F BIT(20)

#define BIT_SHIFT_XTAL_SEL_TOK_V2_8197F 0
#define BIT_MASK_XTAL_SEL_TOK_V2_8197F 0x7
#define BIT_XTAL_SEL_TOK_V2_8197F(x)                                           \
	(((x) & BIT_MASK_XTAL_SEL_TOK_V2_8197F)                                \
	 << BIT_SHIFT_XTAL_SEL_TOK_V2_8197F)
#define BITS_XTAL_SEL_TOK_V2_8197F                                             \
	(BIT_MASK_XTAL_SEL_TOK_V2_8197F << BIT_SHIFT_XTAL_SEL_TOK_V2_8197F)
#define BIT_CLEAR_XTAL_SEL_TOK_V2_8197F(x) ((x) & (~BITS_XTAL_SEL_TOK_V2_8197F))
#define BIT_GET_XTAL_SEL_TOK_V2_8197F(x)                                       \
	(((x) >> BIT_SHIFT_XTAL_SEL_TOK_V2_8197F) &                            \
	 BIT_MASK_XTAL_SEL_TOK_V2_8197F)
#define BIT_SET_XTAL_SEL_TOK_V2_8197F(x, v)                                    \
	(BIT_CLEAR_XTAL_SEL_TOK_V2_8197F(x) | BIT_XTAL_SEL_TOK_V2_8197F(v))

/* 2 REG_USB_SIE_INTF_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_PCIE_MIO_INTF_8197F */
#define BIT_PCIE_MIO_BYIOREG_8197F BIT(13)
#define BIT_PCIE_MIO_RE_8197F BIT(12)

#define BIT_SHIFT_PCIE_MIO_WE_8197F 8
#define BIT_MASK_PCIE_MIO_WE_8197F 0xf
#define BIT_PCIE_MIO_WE_8197F(x)                                               \
	(((x) & BIT_MASK_PCIE_MIO_WE_8197F) << BIT_SHIFT_PCIE_MIO_WE_8197F)
#define BITS_PCIE_MIO_WE_8197F                                                 \
	(BIT_MASK_PCIE_MIO_WE_8197F << BIT_SHIFT_PCIE_MIO_WE_8197F)
#define BIT_CLEAR_PCIE_MIO_WE_8197F(x) ((x) & (~BITS_PCIE_MIO_WE_8197F))
#define BIT_GET_PCIE_MIO_WE_8197F(x)                                           \
	(((x) >> BIT_SHIFT_PCIE_MIO_WE_8197F) & BIT_MASK_PCIE_MIO_WE_8197F)
#define BIT_SET_PCIE_MIO_WE_8197F(x, v)                                        \
	(BIT_CLEAR_PCIE_MIO_WE_8197F(x) | BIT_PCIE_MIO_WE_8197F(v))

#define BIT_SHIFT_PCIE_MIO_ADDR_8197F 0
#define BIT_MASK_PCIE_MIO_ADDR_8197F 0xff
#define BIT_PCIE_MIO_ADDR_8197F(x)                                             \
	(((x) & BIT_MASK_PCIE_MIO_ADDR_8197F) << BIT_SHIFT_PCIE_MIO_ADDR_8197F)
#define BITS_PCIE_MIO_ADDR_8197F                                               \
	(BIT_MASK_PCIE_MIO_ADDR_8197F << BIT_SHIFT_PCIE_MIO_ADDR_8197F)
#define BIT_CLEAR_PCIE_MIO_ADDR_8197F(x) ((x) & (~BITS_PCIE_MIO_ADDR_8197F))
#define BIT_GET_PCIE_MIO_ADDR_8197F(x)                                         \
	(((x) >> BIT_SHIFT_PCIE_MIO_ADDR_8197F) & BIT_MASK_PCIE_MIO_ADDR_8197F)
#define BIT_SET_PCIE_MIO_ADDR_8197F(x, v)                                      \
	(BIT_CLEAR_PCIE_MIO_ADDR_8197F(x) | BIT_PCIE_MIO_ADDR_8197F(v))

/* 2 REG_PCIE_MIO_INTD_8197F */

#define BIT_SHIFT_PCIE_MIO_DATA_8197F 0
#define BIT_MASK_PCIE_MIO_DATA_8197F 0xffffffffL
#define BIT_PCIE_MIO_DATA_8197F(x)                                             \
	(((x) & BIT_MASK_PCIE_MIO_DATA_8197F) << BIT_SHIFT_PCIE_MIO_DATA_8197F)
#define BITS_PCIE_MIO_DATA_8197F                                               \
	(BIT_MASK_PCIE_MIO_DATA_8197F << BIT_SHIFT_PCIE_MIO_DATA_8197F)
#define BIT_CLEAR_PCIE_MIO_DATA_8197F(x) ((x) & (~BITS_PCIE_MIO_DATA_8197F))
#define BIT_GET_PCIE_MIO_DATA_8197F(x)                                         \
	(((x) >> BIT_SHIFT_PCIE_MIO_DATA_8197F) & BIT_MASK_PCIE_MIO_DATA_8197F)
#define BIT_SET_PCIE_MIO_DATA_8197F(x, v)                                      \
	(BIT_CLEAR_PCIE_MIO_DATA_8197F(x) | BIT_PCIE_MIO_DATA_8197F(v))

/* 2 REG_WLRF1_8197F */

/* 2 REG_SYS_CFG1_8197F */

#define BIT_SHIFT_TRP_ICFG_8197F 28
#define BIT_MASK_TRP_ICFG_8197F 0xf
#define BIT_TRP_ICFG_8197F(x)                                                  \
	(((x) & BIT_MASK_TRP_ICFG_8197F) << BIT_SHIFT_TRP_ICFG_8197F)
#define BITS_TRP_ICFG_8197F                                                    \
	(BIT_MASK_TRP_ICFG_8197F << BIT_SHIFT_TRP_ICFG_8197F)
#define BIT_CLEAR_TRP_ICFG_8197F(x) ((x) & (~BITS_TRP_ICFG_8197F))
#define BIT_GET_TRP_ICFG_8197F(x)                                              \
	(((x) >> BIT_SHIFT_TRP_ICFG_8197F) & BIT_MASK_TRP_ICFG_8197F)
#define BIT_SET_TRP_ICFG_8197F(x, v)                                           \
	(BIT_CLEAR_TRP_ICFG_8197F(x) | BIT_TRP_ICFG_8197F(v))

#define BIT_RF_TYPE_ID_8197F BIT(27)
#define BIT_BD_HCI_SEL_8197F BIT(26)
#define BIT_BD_PKG_SEL_8197F BIT(25)
#define BIT_SPSLDO_SEL_8197F BIT(24)
#define BIT_RTL_ID_8197F BIT(23)
#define BIT_PAD_HWPD_IDN_8197F BIT(22)
#define BIT_TESTMODE_8197F BIT(20)

#define BIT_SHIFT_VENDOR_ID_8197F 16
#define BIT_MASK_VENDOR_ID_8197F 0xf
#define BIT_VENDOR_ID_8197F(x)                                                 \
	(((x) & BIT_MASK_VENDOR_ID_8197F) << BIT_SHIFT_VENDOR_ID_8197F)
#define BITS_VENDOR_ID_8197F                                                   \
	(BIT_MASK_VENDOR_ID_8197F << BIT_SHIFT_VENDOR_ID_8197F)
#define BIT_CLEAR_VENDOR_ID_8197F(x) ((x) & (~BITS_VENDOR_ID_8197F))
#define BIT_GET_VENDOR_ID_8197F(x)                                             \
	(((x) >> BIT_SHIFT_VENDOR_ID_8197F) & BIT_MASK_VENDOR_ID_8197F)
#define BIT_SET_VENDOR_ID_8197F(x, v)                                          \
	(BIT_CLEAR_VENDOR_ID_8197F(x) | BIT_VENDOR_ID_8197F(v))

#define BIT_SHIFT_CHIP_VER_8197F 12
#define BIT_MASK_CHIP_VER_8197F 0xf
#define BIT_CHIP_VER_8197F(x)                                                  \
	(((x) & BIT_MASK_CHIP_VER_8197F) << BIT_SHIFT_CHIP_VER_8197F)
#define BITS_CHIP_VER_8197F                                                    \
	(BIT_MASK_CHIP_VER_8197F << BIT_SHIFT_CHIP_VER_8197F)
#define BIT_CLEAR_CHIP_VER_8197F(x) ((x) & (~BITS_CHIP_VER_8197F))
#define BIT_GET_CHIP_VER_8197F(x)                                              \
	(((x) >> BIT_SHIFT_CHIP_VER_8197F) & BIT_MASK_CHIP_VER_8197F)
#define BIT_SET_CHIP_VER_8197F(x, v)                                           \
	(BIT_CLEAR_CHIP_VER_8197F(x) | BIT_CHIP_VER_8197F(v))

#define BIT_BD_MAC1_8197F BIT(10)
#define BIT_BD_MAC2_8197F BIT(9)
#define BIT_SIC_IDLE_8197F BIT(8)
#define BIT_SW_OFFLOAD_EN_8197F BIT(7)
#define BIT_OCP_SHUTDN_8197F BIT(6)
#define BIT_V15_VLD_8197F BIT(5)
#define BIT_PCIRSTB_8197F BIT(4)
#define BIT_PCLK_VLD_8197F BIT(3)
#define BIT_UCLK_VLD_8197F BIT(2)
#define BIT_ACLK_VLD_8197F BIT(1)
#define BIT_XCLK_VLD_8197F BIT(0)

/* 2 REG_SYS_STATUS1_8197F */

#define BIT_SHIFT_RF_RL_ID_8197F 28
#define BIT_MASK_RF_RL_ID_8197F 0xf
#define BIT_RF_RL_ID_8197F(x)                                                  \
	(((x) & BIT_MASK_RF_RL_ID_8197F) << BIT_SHIFT_RF_RL_ID_8197F)
#define BITS_RF_RL_ID_8197F                                                    \
	(BIT_MASK_RF_RL_ID_8197F << BIT_SHIFT_RF_RL_ID_8197F)
#define BIT_CLEAR_RF_RL_ID_8197F(x) ((x) & (~BITS_RF_RL_ID_8197F))
#define BIT_GET_RF_RL_ID_8197F(x)                                              \
	(((x) >> BIT_SHIFT_RF_RL_ID_8197F) & BIT_MASK_RF_RL_ID_8197F)
#define BIT_SET_RF_RL_ID_8197F(x, v)                                           \
	(BIT_CLEAR_RF_RL_ID_8197F(x) | BIT_RF_RL_ID_8197F(v))

#define BIT_HPHY_ICFG_8197F BIT(19)

#define BIT_SHIFT_SEL_0XC0_8197F 16
#define BIT_MASK_SEL_0XC0_8197F 0x3
#define BIT_SEL_0XC0_8197F(x)                                                  \
	(((x) & BIT_MASK_SEL_0XC0_8197F) << BIT_SHIFT_SEL_0XC0_8197F)
#define BITS_SEL_0XC0_8197F                                                    \
	(BIT_MASK_SEL_0XC0_8197F << BIT_SHIFT_SEL_0XC0_8197F)
#define BIT_CLEAR_SEL_0XC0_8197F(x) ((x) & (~BITS_SEL_0XC0_8197F))
#define BIT_GET_SEL_0XC0_8197F(x)                                              \
	(((x) >> BIT_SHIFT_SEL_0XC0_8197F) & BIT_MASK_SEL_0XC0_8197F)
#define BIT_SET_SEL_0XC0_8197F(x, v)                                           \
	(BIT_CLEAR_SEL_0XC0_8197F(x) | BIT_SEL_0XC0_8197F(v))

#define BIT_USB_OPERATION_MODE_8197F BIT(10)
#define BIT_BT_PDN_8197F BIT(9)
#define BIT_AUTO_WLPON_8197F BIT(8)
#define BIT_WL_MODE_8197F BIT(7)
#define BIT_PKG_SEL_HCI_8197F BIT(6)

#define BIT_SHIFT_HCI_SEL_8197F 4
#define BIT_MASK_HCI_SEL_8197F 0x3
#define BIT_HCI_SEL_8197F(x)                                                   \
	(((x) & BIT_MASK_HCI_SEL_8197F) << BIT_SHIFT_HCI_SEL_8197F)
#define BITS_HCI_SEL_8197F (BIT_MASK_HCI_SEL_8197F << BIT_SHIFT_HCI_SEL_8197F)
#define BIT_CLEAR_HCI_SEL_8197F(x) ((x) & (~BITS_HCI_SEL_8197F))
#define BIT_GET_HCI_SEL_8197F(x)                                               \
	(((x) >> BIT_SHIFT_HCI_SEL_8197F) & BIT_MASK_HCI_SEL_8197F)
#define BIT_SET_HCI_SEL_8197F(x, v)                                            \
	(BIT_CLEAR_HCI_SEL_8197F(x) | BIT_HCI_SEL_8197F(v))

#define BIT_SHIFT_PAD_HCI_SEL_8197F 2
#define BIT_MASK_PAD_HCI_SEL_8197F 0x3
#define BIT_PAD_HCI_SEL_8197F(x)                                               \
	(((x) & BIT_MASK_PAD_HCI_SEL_8197F) << BIT_SHIFT_PAD_HCI_SEL_8197F)
#define BITS_PAD_HCI_SEL_8197F                                                 \
	(BIT_MASK_PAD_HCI_SEL_8197F << BIT_SHIFT_PAD_HCI_SEL_8197F)
#define BIT_CLEAR_PAD_HCI_SEL_8197F(x) ((x) & (~BITS_PAD_HCI_SEL_8197F))
#define BIT_GET_PAD_HCI_SEL_8197F(x)                                           \
	(((x) >> BIT_SHIFT_PAD_HCI_SEL_8197F) & BIT_MASK_PAD_HCI_SEL_8197F)
#define BIT_SET_PAD_HCI_SEL_8197F(x, v)                                        \
	(BIT_CLEAR_PAD_HCI_SEL_8197F(x) | BIT_PAD_HCI_SEL_8197F(v))

#define BIT_SHIFT_EFS_HCI_SEL_8197F 0
#define BIT_MASK_EFS_HCI_SEL_8197F 0x3
#define BIT_EFS_HCI_SEL_8197F(x)                                               \
	(((x) & BIT_MASK_EFS_HCI_SEL_8197F) << BIT_SHIFT_EFS_HCI_SEL_8197F)
#define BITS_EFS_HCI_SEL_8197F                                                 \
	(BIT_MASK_EFS_HCI_SEL_8197F << BIT_SHIFT_EFS_HCI_SEL_8197F)
#define BIT_CLEAR_EFS_HCI_SEL_8197F(x) ((x) & (~BITS_EFS_HCI_SEL_8197F))
#define BIT_GET_EFS_HCI_SEL_8197F(x)                                           \
	(((x) >> BIT_SHIFT_EFS_HCI_SEL_8197F) & BIT_MASK_EFS_HCI_SEL_8197F)
#define BIT_SET_EFS_HCI_SEL_8197F(x, v)                                        \
	(BIT_CLEAR_EFS_HCI_SEL_8197F(x) | BIT_EFS_HCI_SEL_8197F(v))

/* 2 REG_SYS_STATUS2_8197F */
#define BIT_SIO_ALDN_8197F BIT(19)
#define BIT_USB_ALDN_8197F BIT(18)
#define BIT_PCI_ALDN_8197F BIT(17)
#define BIT_SYS_ALDN_8197F BIT(16)

#define BIT_SHIFT_EPVID1_8197F 8
#define BIT_MASK_EPVID1_8197F 0xff
#define BIT_EPVID1_8197F(x)                                                    \
	(((x) & BIT_MASK_EPVID1_8197F) << BIT_SHIFT_EPVID1_8197F)
#define BITS_EPVID1_8197F (BIT_MASK_EPVID1_8197F << BIT_SHIFT_EPVID1_8197F)
#define BIT_CLEAR_EPVID1_8197F(x) ((x) & (~BITS_EPVID1_8197F))
#define BIT_GET_EPVID1_8197F(x)                                                \
	(((x) >> BIT_SHIFT_EPVID1_8197F) & BIT_MASK_EPVID1_8197F)
#define BIT_SET_EPVID1_8197F(x, v)                                             \
	(BIT_CLEAR_EPVID1_8197F(x) | BIT_EPVID1_8197F(v))

#define BIT_SHIFT_EPVID0_8197F 0
#define BIT_MASK_EPVID0_8197F 0xff
#define BIT_EPVID0_8197F(x)                                                    \
	(((x) & BIT_MASK_EPVID0_8197F) << BIT_SHIFT_EPVID0_8197F)
#define BITS_EPVID0_8197F (BIT_MASK_EPVID0_8197F << BIT_SHIFT_EPVID0_8197F)
#define BIT_CLEAR_EPVID0_8197F(x) ((x) & (~BITS_EPVID0_8197F))
#define BIT_GET_EPVID0_8197F(x)                                                \
	(((x) >> BIT_SHIFT_EPVID0_8197F) & BIT_MASK_EPVID0_8197F)
#define BIT_SET_EPVID0_8197F(x, v)                                             \
	(BIT_CLEAR_EPVID0_8197F(x) | BIT_EPVID0_8197F(v))

/* 2 REG_SYS_CFG2_8197F */

#define BIT_SHIFT_HW_ID_8197F 0
#define BIT_MASK_HW_ID_8197F 0xff
#define BIT_HW_ID_8197F(x)                                                     \
	(((x) & BIT_MASK_HW_ID_8197F) << BIT_SHIFT_HW_ID_8197F)
#define BITS_HW_ID_8197F (BIT_MASK_HW_ID_8197F << BIT_SHIFT_HW_ID_8197F)
#define BIT_CLEAR_HW_ID_8197F(x) ((x) & (~BITS_HW_ID_8197F))
#define BIT_GET_HW_ID_8197F(x)                                                 \
	(((x) >> BIT_SHIFT_HW_ID_8197F) & BIT_MASK_HW_ID_8197F)
#define BIT_SET_HW_ID_8197F(x, v)                                              \
	(BIT_CLEAR_HW_ID_8197F(x) | BIT_HW_ID_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_SYS_CFG3_8197F */

/* 2 REG_SYS_CFG4_8197F */

/* 2 REG_CPU_DMEM_CON_8197F */
#define BIT_ANA_PORT_IDLE_8197F BIT(18)
#define BIT_MAC_PORT_IDLE_8197F BIT(17)
#define BIT_WL_PLATFORM_RST_8197F BIT(16)
#define BIT_WL_SECURITY_CLK_8197F BIT(15)

#define BIT_SHIFT_CPU_DMEM_CON_8197F 0
#define BIT_MASK_CPU_DMEM_CON_8197F 0xff
#define BIT_CPU_DMEM_CON_8197F(x)                                              \
	(((x) & BIT_MASK_CPU_DMEM_CON_8197F) << BIT_SHIFT_CPU_DMEM_CON_8197F)
#define BITS_CPU_DMEM_CON_8197F                                                \
	(BIT_MASK_CPU_DMEM_CON_8197F << BIT_SHIFT_CPU_DMEM_CON_8197F)
#define BIT_CLEAR_CPU_DMEM_CON_8197F(x) ((x) & (~BITS_CPU_DMEM_CON_8197F))
#define BIT_GET_CPU_DMEM_CON_8197F(x)                                          \
	(((x) >> BIT_SHIFT_CPU_DMEM_CON_8197F) & BIT_MASK_CPU_DMEM_CON_8197F)
#define BIT_SET_CPU_DMEM_CON_8197F(x, v)                                       \
	(BIT_CLEAR_CPU_DMEM_CON_8197F(x) | BIT_CPU_DMEM_CON_8197F(v))

/* 2 REG_HIMR2_8197F */
#define BIT_BCNDMAINT_P4_MSK_8197F BIT(31)
#define BIT_BCNDMAINT_P3_MSK_8197F BIT(30)
#define BIT_BCNDMAINT_P2_MSK_8197F BIT(29)
#define BIT_BCNDMAINT_P1_MSK_8197F BIT(28)
#define BIT_ATIMEND7_MSK_8197F BIT(22)
#define BIT_ATIMEND6_MSK_8197F BIT(21)
#define BIT_ATIMEND5_MSK_8197F BIT(20)
#define BIT_ATIMEND4_MSK_8197F BIT(19)
#define BIT_ATIMEND3_MSK_8197F BIT(18)
#define BIT_ATIMEND2_MSK_8197F BIT(17)
#define BIT_ATIMEND1_MSK_8197F BIT(16)
#define BIT_TXBCN7OK_MSK_8197F BIT(14)
#define BIT_TXBCN6OK_MSK_8197F BIT(13)
#define BIT_TXBCN5OK_MSK_8197F BIT(12)
#define BIT_TXBCN4OK_MSK_8197F BIT(11)
#define BIT_TXBCN3OK_MSK_8197F BIT(10)
#define BIT_TXBCN2OK_MSK_8197F BIT(9)
#define BIT_TXBCN1OK_MSK_V1_8197F BIT(8)
#define BIT_TXBCN7ERR_MSK_8197F BIT(6)
#define BIT_TXBCN6ERR_MSK_8197F BIT(5)
#define BIT_TXBCN5ERR_MSK_8197F BIT(4)
#define BIT_TXBCN4ERR_MSK_8197F BIT(3)
#define BIT_TXBCN3ERR_MSK_8197F BIT(2)
#define BIT_TXBCN2ERR_MSK_8197F BIT(1)
#define BIT_TXBCN1ERR_MSK_V1_8197F BIT(0)

/* 2 REG_HISR2_8197F */
#define BIT_BCNDMAINT_P4_8197F BIT(31)
#define BIT_BCNDMAINT_P3_8197F BIT(30)
#define BIT_BCNDMAINT_P2_8197F BIT(29)
#define BIT_BCNDMAINT_P1_8197F BIT(28)
#define BIT_ATIMEND7_8197F BIT(22)
#define BIT_ATIMEND6_8197F BIT(21)
#define BIT_ATIMEND5_8197F BIT(20)
#define BIT_ATIMEND4_8197F BIT(19)
#define BIT_ATIMEND3_8197F BIT(18)
#define BIT_ATIMEND2_8197F BIT(17)
#define BIT_ATIMEND1_8197F BIT(16)
#define BIT_TXBCN7OK_8197F BIT(14)
#define BIT_TXBCN6OK_8197F BIT(13)
#define BIT_TXBCN5OK_8197F BIT(12)
#define BIT_TXBCN4OK_8197F BIT(11)
#define BIT_TXBCN3OK_8197F BIT(10)
#define BIT_TXBCN2OK_8197F BIT(9)
#define BIT_TXBCN1OK_8197F BIT(8)
#define BIT_TXBCN7ERR_8197F BIT(6)
#define BIT_TXBCN6ERR_8197F BIT(5)
#define BIT_TXBCN5ERR_8197F BIT(4)
#define BIT_TXBCN4ERR_8197F BIT(3)
#define BIT_TXBCN3ERR_8197F BIT(2)
#define BIT_TXBCN2ERR_8197F BIT(1)
#define BIT_TXBCN1ERR_8197F BIT(0)

/* 2 REG_HIMR3_8197F */
#define BIT_SETH2CDOK_MASK_8197F BIT(16)
#define BIT_H2C_CMD_FULL_MASK_8197F BIT(15)
#define BIT_PWR_INT_127_MASK_8197F BIT(14)
#define BIT_TXSHORTCUT_TXDESUPDATEOK_MASK_8197F BIT(13)
#define BIT_TXSHORTCUT_BKUPDATEOK_MASK_8197F BIT(12)
#define BIT_TXSHORTCUT_BEUPDATEOK_MASK_8197F BIT(11)
#define BIT_TXSHORTCUT_VIUPDATEOK_MAS_8197F BIT(10)
#define BIT_TXSHORTCUT_VOUPDATEOK_MASK_8197F BIT(9)
#define BIT_PWR_INT_127_MASK_V1_8197F BIT(8)
#define BIT_PWR_INT_126TO96_MASK_8197F BIT(7)
#define BIT_PWR_INT_95TO64_MASK_8197F BIT(6)
#define BIT_PWR_INT_63TO32_MASK_8197F BIT(5)
#define BIT_PWR_INT_31TO0_MASK_8197F BIT(4)
#define BIT_DDMA0_LP_INT_MSK_8197F BIT(1)
#define BIT_DDMA0_HP_INT_MSK_8197F BIT(0)

/* 2 REG_HISR3_8197F */
#define BIT_SETH2CDOK_8197F BIT(16)
#define BIT_H2C_CMD_FULL_8197F BIT(15)
#define BIT_PWR_INT_127_8197F BIT(14)
#define BIT_TXSHORTCUT_TXDESUPDATEOK_8197F BIT(13)
#define BIT_TXSHORTCUT_BKUPDATEOK_8197F BIT(12)
#define BIT_TXSHORTCUT_BEUPDATEOK_8197F BIT(11)
#define BIT_TXSHORTCUT_VIUPDATEOK_8197F BIT(10)
#define BIT_TXSHORTCUT_VOUPDATEOK_8197F BIT(9)
#define BIT_PWR_INT_127_V1_8197F BIT(8)
#define BIT_PWR_INT_126TO96_8197F BIT(7)
#define BIT_PWR_INT_95TO64_8197F BIT(6)
#define BIT_PWR_INT_63TO32_8197F BIT(5)
#define BIT_PWR_INT_31TO0_8197F BIT(4)
#define BIT_DDMA0_LP_INT_8197F BIT(1)
#define BIT_DDMA0_HP_INT_8197F BIT(0)

/* 2 REG_SW_MDIO_8197F */

/* 2 REG_SW_FLUSH_8197F */
#define BIT_FLUSH_HOLDN_EN_8197F BIT(25)
#define BIT_FLUSH_WR_EN_8197F BIT(24)
#define BIT_SW_FLASH_CONTROL_8197F BIT(23)
#define BIT_SW_FLASH_WEN_E_8197F BIT(19)
#define BIT_SW_FLASH_HOLDN_E_8197F BIT(18)
#define BIT_SW_FLASH_SO_E_8197F BIT(17)
#define BIT_SW_FLASH_SI_E_8197F BIT(16)
#define BIT_SW_FLASH_SK_O_8197F BIT(13)
#define BIT_SW_FLASH_CEN_O_8197F BIT(12)
#define BIT_SW_FLASH_WEN_O_8197F BIT(11)
#define BIT_SW_FLASH_HOLDN_O_8197F BIT(10)
#define BIT_SW_FLASH_SO_O_8197F BIT(9)
#define BIT_SW_FLASH_SI_O_8197F BIT(8)
#define BIT_SW_FLASH_WEN_I_8197F BIT(3)
#define BIT_SW_FLASH_HOLDN_I_8197F BIT(2)
#define BIT_SW_FLASH_SO_I_8197F BIT(1)
#define BIT_SW_FLASH_SI_I_8197F BIT(0)

/* 2 REG_DBG_GPIO_BMUX_8197F */

#define BIT_SHIFT_DBG_GPIO_BMUX_7_8197F 21
#define BIT_MASK_DBG_GPIO_BMUX_7_8197F 0x7
#define BIT_DBG_GPIO_BMUX_7_8197F(x)                                           \
	(((x) & BIT_MASK_DBG_GPIO_BMUX_7_8197F)                                \
	 << BIT_SHIFT_DBG_GPIO_BMUX_7_8197F)
#define BITS_DBG_GPIO_BMUX_7_8197F                                             \
	(BIT_MASK_DBG_GPIO_BMUX_7_8197F << BIT_SHIFT_DBG_GPIO_BMUX_7_8197F)
#define BIT_CLEAR_DBG_GPIO_BMUX_7_8197F(x) ((x) & (~BITS_DBG_GPIO_BMUX_7_8197F))
#define BIT_GET_DBG_GPIO_BMUX_7_8197F(x)                                       \
	(((x) >> BIT_SHIFT_DBG_GPIO_BMUX_7_8197F) &                            \
	 BIT_MASK_DBG_GPIO_BMUX_7_8197F)
#define BIT_SET_DBG_GPIO_BMUX_7_8197F(x, v)                                    \
	(BIT_CLEAR_DBG_GPIO_BMUX_7_8197F(x) | BIT_DBG_GPIO_BMUX_7_8197F(v))

#define BIT_SHIFT_DBG_GPIO_BMUX_6_8197F 18
#define BIT_MASK_DBG_GPIO_BMUX_6_8197F 0x7
#define BIT_DBG_GPIO_BMUX_6_8197F(x)                                           \
	(((x) & BIT_MASK_DBG_GPIO_BMUX_6_8197F)                                \
	 << BIT_SHIFT_DBG_GPIO_BMUX_6_8197F)
#define BITS_DBG_GPIO_BMUX_6_8197F                                             \
	(BIT_MASK_DBG_GPIO_BMUX_6_8197F << BIT_SHIFT_DBG_GPIO_BMUX_6_8197F)
#define BIT_CLEAR_DBG_GPIO_BMUX_6_8197F(x) ((x) & (~BITS_DBG_GPIO_BMUX_6_8197F))
#define BIT_GET_DBG_GPIO_BMUX_6_8197F(x)                                       \
	(((x) >> BIT_SHIFT_DBG_GPIO_BMUX_6_8197F) &                            \
	 BIT_MASK_DBG_GPIO_BMUX_6_8197F)
#define BIT_SET_DBG_GPIO_BMUX_6_8197F(x, v)                                    \
	(BIT_CLEAR_DBG_GPIO_BMUX_6_8197F(x) | BIT_DBG_GPIO_BMUX_6_8197F(v))

#define BIT_SHIFT_DBG_GPIO_BMUX_5_8197F 15
#define BIT_MASK_DBG_GPIO_BMUX_5_8197F 0x7
#define BIT_DBG_GPIO_BMUX_5_8197F(x)                                           \
	(((x) & BIT_MASK_DBG_GPIO_BMUX_5_8197F)                                \
	 << BIT_SHIFT_DBG_GPIO_BMUX_5_8197F)
#define BITS_DBG_GPIO_BMUX_5_8197F                                             \
	(BIT_MASK_DBG_GPIO_BMUX_5_8197F << BIT_SHIFT_DBG_GPIO_BMUX_5_8197F)
#define BIT_CLEAR_DBG_GPIO_BMUX_5_8197F(x) ((x) & (~BITS_DBG_GPIO_BMUX_5_8197F))
#define BIT_GET_DBG_GPIO_BMUX_5_8197F(x)                                       \
	(((x) >> BIT_SHIFT_DBG_GPIO_BMUX_5_8197F) &                            \
	 BIT_MASK_DBG_GPIO_BMUX_5_8197F)
#define BIT_SET_DBG_GPIO_BMUX_5_8197F(x, v)                                    \
	(BIT_CLEAR_DBG_GPIO_BMUX_5_8197F(x) | BIT_DBG_GPIO_BMUX_5_8197F(v))

#define BIT_SHIFT_DBG_GPIO_BMUX_4_8197F 12
#define BIT_MASK_DBG_GPIO_BMUX_4_8197F 0x7
#define BIT_DBG_GPIO_BMUX_4_8197F(x)                                           \
	(((x) & BIT_MASK_DBG_GPIO_BMUX_4_8197F)                                \
	 << BIT_SHIFT_DBG_GPIO_BMUX_4_8197F)
#define BITS_DBG_GPIO_BMUX_4_8197F                                             \
	(BIT_MASK_DBG_GPIO_BMUX_4_8197F << BIT_SHIFT_DBG_GPIO_BMUX_4_8197F)
#define BIT_CLEAR_DBG_GPIO_BMUX_4_8197F(x) ((x) & (~BITS_DBG_GPIO_BMUX_4_8197F))
#define BIT_GET_DBG_GPIO_BMUX_4_8197F(x)                                       \
	(((x) >> BIT_SHIFT_DBG_GPIO_BMUX_4_8197F) &                            \
	 BIT_MASK_DBG_GPIO_BMUX_4_8197F)
#define BIT_SET_DBG_GPIO_BMUX_4_8197F(x, v)                                    \
	(BIT_CLEAR_DBG_GPIO_BMUX_4_8197F(x) | BIT_DBG_GPIO_BMUX_4_8197F(v))

#define BIT_SHIFT_DBG_GPIO_BMUX_3_8197F 9
#define BIT_MASK_DBG_GPIO_BMUX_3_8197F 0x7
#define BIT_DBG_GPIO_BMUX_3_8197F(x)                                           \
	(((x) & BIT_MASK_DBG_GPIO_BMUX_3_8197F)                                \
	 << BIT_SHIFT_DBG_GPIO_BMUX_3_8197F)
#define BITS_DBG_GPIO_BMUX_3_8197F                                             \
	(BIT_MASK_DBG_GPIO_BMUX_3_8197F << BIT_SHIFT_DBG_GPIO_BMUX_3_8197F)
#define BIT_CLEAR_DBG_GPIO_BMUX_3_8197F(x) ((x) & (~BITS_DBG_GPIO_BMUX_3_8197F))
#define BIT_GET_DBG_GPIO_BMUX_3_8197F(x)                                       \
	(((x) >> BIT_SHIFT_DBG_GPIO_BMUX_3_8197F) &                            \
	 BIT_MASK_DBG_GPIO_BMUX_3_8197F)
#define BIT_SET_DBG_GPIO_BMUX_3_8197F(x, v)                                    \
	(BIT_CLEAR_DBG_GPIO_BMUX_3_8197F(x) | BIT_DBG_GPIO_BMUX_3_8197F(v))

#define BIT_SHIFT_DBG_GPIO_BMUX_2_8197F 6
#define BIT_MASK_DBG_GPIO_BMUX_2_8197F 0x7
#define BIT_DBG_GPIO_BMUX_2_8197F(x)                                           \
	(((x) & BIT_MASK_DBG_GPIO_BMUX_2_8197F)                                \
	 << BIT_SHIFT_DBG_GPIO_BMUX_2_8197F)
#define BITS_DBG_GPIO_BMUX_2_8197F                                             \
	(BIT_MASK_DBG_GPIO_BMUX_2_8197F << BIT_SHIFT_DBG_GPIO_BMUX_2_8197F)
#define BIT_CLEAR_DBG_GPIO_BMUX_2_8197F(x) ((x) & (~BITS_DBG_GPIO_BMUX_2_8197F))
#define BIT_GET_DBG_GPIO_BMUX_2_8197F(x)                                       \
	(((x) >> BIT_SHIFT_DBG_GPIO_BMUX_2_8197F) &                            \
	 BIT_MASK_DBG_GPIO_BMUX_2_8197F)
#define BIT_SET_DBG_GPIO_BMUX_2_8197F(x, v)                                    \
	(BIT_CLEAR_DBG_GPIO_BMUX_2_8197F(x) | BIT_DBG_GPIO_BMUX_2_8197F(v))

#define BIT_SHIFT_DBG_GPIO_BMUX_1_8197F 3
#define BIT_MASK_DBG_GPIO_BMUX_1_8197F 0x7
#define BIT_DBG_GPIO_BMUX_1_8197F(x)                                           \
	(((x) & BIT_MASK_DBG_GPIO_BMUX_1_8197F)                                \
	 << BIT_SHIFT_DBG_GPIO_BMUX_1_8197F)
#define BITS_DBG_GPIO_BMUX_1_8197F                                             \
	(BIT_MASK_DBG_GPIO_BMUX_1_8197F << BIT_SHIFT_DBG_GPIO_BMUX_1_8197F)
#define BIT_CLEAR_DBG_GPIO_BMUX_1_8197F(x) ((x) & (~BITS_DBG_GPIO_BMUX_1_8197F))
#define BIT_GET_DBG_GPIO_BMUX_1_8197F(x)                                       \
	(((x) >> BIT_SHIFT_DBG_GPIO_BMUX_1_8197F) &                            \
	 BIT_MASK_DBG_GPIO_BMUX_1_8197F)
#define BIT_SET_DBG_GPIO_BMUX_1_8197F(x, v)                                    \
	(BIT_CLEAR_DBG_GPIO_BMUX_1_8197F(x) | BIT_DBG_GPIO_BMUX_1_8197F(v))

#define BIT_SHIFT_DBG_GPIO_BMUX_0_8197F 0
#define BIT_MASK_DBG_GPIO_BMUX_0_8197F 0x7
#define BIT_DBG_GPIO_BMUX_0_8197F(x)                                           \
	(((x) & BIT_MASK_DBG_GPIO_BMUX_0_8197F)                                \
	 << BIT_SHIFT_DBG_GPIO_BMUX_0_8197F)
#define BITS_DBG_GPIO_BMUX_0_8197F                                             \
	(BIT_MASK_DBG_GPIO_BMUX_0_8197F << BIT_SHIFT_DBG_GPIO_BMUX_0_8197F)
#define BIT_CLEAR_DBG_GPIO_BMUX_0_8197F(x) ((x) & (~BITS_DBG_GPIO_BMUX_0_8197F))
#define BIT_GET_DBG_GPIO_BMUX_0_8197F(x)                                       \
	(((x) >> BIT_SHIFT_DBG_GPIO_BMUX_0_8197F) &                            \
	 BIT_MASK_DBG_GPIO_BMUX_0_8197F)
#define BIT_SET_DBG_GPIO_BMUX_0_8197F(x, v)                                    \
	(BIT_CLEAR_DBG_GPIO_BMUX_0_8197F(x) | BIT_DBG_GPIO_BMUX_0_8197F(v))

/* 2 REG_FPGA_TAG_8197F (NO USE IN ASIC) */

#define BIT_SHIFT_FPGA_TAG_8197F 0
#define BIT_MASK_FPGA_TAG_8197F 0xffffffffL
#define BIT_FPGA_TAG_8197F(x)                                                  \
	(((x) & BIT_MASK_FPGA_TAG_8197F) << BIT_SHIFT_FPGA_TAG_8197F)
#define BITS_FPGA_TAG_8197F                                                    \
	(BIT_MASK_FPGA_TAG_8197F << BIT_SHIFT_FPGA_TAG_8197F)
#define BIT_CLEAR_FPGA_TAG_8197F(x) ((x) & (~BITS_FPGA_TAG_8197F))
#define BIT_GET_FPGA_TAG_8197F(x)                                              \
	(((x) >> BIT_SHIFT_FPGA_TAG_8197F) & BIT_MASK_FPGA_TAG_8197F)
#define BIT_SET_FPGA_TAG_8197F(x, v)                                           \
	(BIT_CLEAR_FPGA_TAG_8197F(x) | BIT_FPGA_TAG_8197F(v))

/* 2 REG_WL_DSS_CTRL0_8197F */
#define BIT_WL_DSS_RSTN_8197F BIT(27)
#define BIT_WL_DSS_EN_CLK_8197F BIT(26)
#define BIT_WL_DSS_SPEED_EN_8197F BIT(25)

#define BIT_SHIFT_WL_DSS_COUNT_OUT_8197F 0
#define BIT_MASK_WL_DSS_COUNT_OUT_8197F 0xfffff
#define BIT_WL_DSS_COUNT_OUT_8197F(x)                                          \
	(((x) & BIT_MASK_WL_DSS_COUNT_OUT_8197F)                               \
	 << BIT_SHIFT_WL_DSS_COUNT_OUT_8197F)
#define BITS_WL_DSS_COUNT_OUT_8197F                                            \
	(BIT_MASK_WL_DSS_COUNT_OUT_8197F << BIT_SHIFT_WL_DSS_COUNT_OUT_8197F)
#define BIT_CLEAR_WL_DSS_COUNT_OUT_8197F(x)                                    \
	((x) & (~BITS_WL_DSS_COUNT_OUT_8197F))
#define BIT_GET_WL_DSS_COUNT_OUT_8197F(x)                                      \
	(((x) >> BIT_SHIFT_WL_DSS_COUNT_OUT_8197F) &                           \
	 BIT_MASK_WL_DSS_COUNT_OUT_8197F)
#define BIT_SET_WL_DSS_COUNT_OUT_8197F(x, v)                                   \
	(BIT_CLEAR_WL_DSS_COUNT_OUT_8197F(x) | BIT_WL_DSS_COUNT_OUT_8197F(v))

/* 2 REG_WL_DSS_CTRL1_8197F */
#define BIT_WL_DSS_RSTN_8197F BIT(27)
#define BIT_WL_DSS_EN_CLK_8197F BIT(26)
#define BIT_WL_DSS_SPEED_EN_8197F BIT(25)
#define BIT_WL_DSS_WIRE_SEL_8197F BIT(24)

#define BIT_SHIFT_WL_DSS_RO_SEL_8197F 20
#define BIT_MASK_WL_DSS_RO_SEL_8197F 0x7
#define BIT_WL_DSS_RO_SEL_8197F(x)                                             \
	(((x) & BIT_MASK_WL_DSS_RO_SEL_8197F) << BIT_SHIFT_WL_DSS_RO_SEL_8197F)
#define BITS_WL_DSS_RO_SEL_8197F                                               \
	(BIT_MASK_WL_DSS_RO_SEL_8197F << BIT_SHIFT_WL_DSS_RO_SEL_8197F)
#define BIT_CLEAR_WL_DSS_RO_SEL_8197F(x) ((x) & (~BITS_WL_DSS_RO_SEL_8197F))
#define BIT_GET_WL_DSS_RO_SEL_8197F(x)                                         \
	(((x) >> BIT_SHIFT_WL_DSS_RO_SEL_8197F) & BIT_MASK_WL_DSS_RO_SEL_8197F)
#define BIT_SET_WL_DSS_RO_SEL_8197F(x, v)                                      \
	(BIT_CLEAR_WL_DSS_RO_SEL_8197F(x) | BIT_WL_DSS_RO_SEL_8197F(v))

#define BIT_SHIFT_WL_DSS_DATA_IN_8197F 0
#define BIT_MASK_WL_DSS_DATA_IN_8197F 0xfffff
#define BIT_WL_DSS_DATA_IN_8197F(x)                                            \
	(((x) & BIT_MASK_WL_DSS_DATA_IN_8197F)                                 \
	 << BIT_SHIFT_WL_DSS_DATA_IN_8197F)
#define BITS_WL_DSS_DATA_IN_8197F                                              \
	(BIT_MASK_WL_DSS_DATA_IN_8197F << BIT_SHIFT_WL_DSS_DATA_IN_8197F)
#define BIT_CLEAR_WL_DSS_DATA_IN_8197F(x) ((x) & (~BITS_WL_DSS_DATA_IN_8197F))
#define BIT_GET_WL_DSS_DATA_IN_8197F(x)                                        \
	(((x) >> BIT_SHIFT_WL_DSS_DATA_IN_8197F) &                             \
	 BIT_MASK_WL_DSS_DATA_IN_8197F)
#define BIT_SET_WL_DSS_DATA_IN_8197F(x, v)                                     \
	(BIT_CLEAR_WL_DSS_DATA_IN_8197F(x) | BIT_WL_DSS_DATA_IN_8197F(v))

/* 2 REG_WL_DSS_STATUS1_8197F */
#define BIT_WL_DSS_READY_8197F BIT(21)
#define BIT_WL_DSS_WSORT_GO_8197F BIT(20)

#define BIT_SHIFT_WL_DSS_COUNT_OUT_8197F 0
#define BIT_MASK_WL_DSS_COUNT_OUT_8197F 0xfffff
#define BIT_WL_DSS_COUNT_OUT_8197F(x)                                          \
	(((x) & BIT_MASK_WL_DSS_COUNT_OUT_8197F)                               \
	 << BIT_SHIFT_WL_DSS_COUNT_OUT_8197F)
#define BITS_WL_DSS_COUNT_OUT_8197F                                            \
	(BIT_MASK_WL_DSS_COUNT_OUT_8197F << BIT_SHIFT_WL_DSS_COUNT_OUT_8197F)
#define BIT_CLEAR_WL_DSS_COUNT_OUT_8197F(x)                                    \
	((x) & (~BITS_WL_DSS_COUNT_OUT_8197F))
#define BIT_GET_WL_DSS_COUNT_OUT_8197F(x)                                      \
	(((x) >> BIT_SHIFT_WL_DSS_COUNT_OUT_8197F) &                           \
	 BIT_MASK_WL_DSS_COUNT_OUT_8197F)
#define BIT_SET_WL_DSS_COUNT_OUT_8197F(x, v)                                   \
	(BIT_CLEAR_WL_DSS_COUNT_OUT_8197F(x) | BIT_WL_DSS_COUNT_OUT_8197F(v))

/* 2 REG_FW_DBG0_8197F */

#define BIT_SHIFT_FW_DBG0_8197F 0
#define BIT_MASK_FW_DBG0_8197F 0xffffffffL
#define BIT_FW_DBG0_8197F(x)                                                   \
	(((x) & BIT_MASK_FW_DBG0_8197F) << BIT_SHIFT_FW_DBG0_8197F)
#define BITS_FW_DBG0_8197F (BIT_MASK_FW_DBG0_8197F << BIT_SHIFT_FW_DBG0_8197F)
#define BIT_CLEAR_FW_DBG0_8197F(x) ((x) & (~BITS_FW_DBG0_8197F))
#define BIT_GET_FW_DBG0_8197F(x)                                               \
	(((x) >> BIT_SHIFT_FW_DBG0_8197F) & BIT_MASK_FW_DBG0_8197F)
#define BIT_SET_FW_DBG0_8197F(x, v)                                            \
	(BIT_CLEAR_FW_DBG0_8197F(x) | BIT_FW_DBG0_8197F(v))

/* 2 REG_FW_DBG1_8197F */

#define BIT_SHIFT_FW_DBG1_8197F 0
#define BIT_MASK_FW_DBG1_8197F 0xffffffffL
#define BIT_FW_DBG1_8197F(x)                                                   \
	(((x) & BIT_MASK_FW_DBG1_8197F) << BIT_SHIFT_FW_DBG1_8197F)
#define BITS_FW_DBG1_8197F (BIT_MASK_FW_DBG1_8197F << BIT_SHIFT_FW_DBG1_8197F)
#define BIT_CLEAR_FW_DBG1_8197F(x) ((x) & (~BITS_FW_DBG1_8197F))
#define BIT_GET_FW_DBG1_8197F(x)                                               \
	(((x) >> BIT_SHIFT_FW_DBG1_8197F) & BIT_MASK_FW_DBG1_8197F)
#define BIT_SET_FW_DBG1_8197F(x, v)                                            \
	(BIT_CLEAR_FW_DBG1_8197F(x) | BIT_FW_DBG1_8197F(v))

/* 2 REG_FW_DBG2_8197F */

#define BIT_SHIFT_FW_DBG2_8197F 0
#define BIT_MASK_FW_DBG2_8197F 0xffffffffL
#define BIT_FW_DBG2_8197F(x)                                                   \
	(((x) & BIT_MASK_FW_DBG2_8197F) << BIT_SHIFT_FW_DBG2_8197F)
#define BITS_FW_DBG2_8197F (BIT_MASK_FW_DBG2_8197F << BIT_SHIFT_FW_DBG2_8197F)
#define BIT_CLEAR_FW_DBG2_8197F(x) ((x) & (~BITS_FW_DBG2_8197F))
#define BIT_GET_FW_DBG2_8197F(x)                                               \
	(((x) >> BIT_SHIFT_FW_DBG2_8197F) & BIT_MASK_FW_DBG2_8197F)
#define BIT_SET_FW_DBG2_8197F(x, v)                                            \
	(BIT_CLEAR_FW_DBG2_8197F(x) | BIT_FW_DBG2_8197F(v))

/* 2 REG_FW_DBG3_8197F */

#define BIT_SHIFT_FW_DBG3_8197F 0
#define BIT_MASK_FW_DBG3_8197F 0xffffffffL
#define BIT_FW_DBG3_8197F(x)                                                   \
	(((x) & BIT_MASK_FW_DBG3_8197F) << BIT_SHIFT_FW_DBG3_8197F)
#define BITS_FW_DBG3_8197F (BIT_MASK_FW_DBG3_8197F << BIT_SHIFT_FW_DBG3_8197F)
#define BIT_CLEAR_FW_DBG3_8197F(x) ((x) & (~BITS_FW_DBG3_8197F))
#define BIT_GET_FW_DBG3_8197F(x)                                               \
	(((x) >> BIT_SHIFT_FW_DBG3_8197F) & BIT_MASK_FW_DBG3_8197F)
#define BIT_SET_FW_DBG3_8197F(x, v)                                            \
	(BIT_CLEAR_FW_DBG3_8197F(x) | BIT_FW_DBG3_8197F(v))

/* 2 REG_FW_DBG4_8197F */

#define BIT_SHIFT_FW_DBG4_8197F 0
#define BIT_MASK_FW_DBG4_8197F 0xffffffffL
#define BIT_FW_DBG4_8197F(x)                                                   \
	(((x) & BIT_MASK_FW_DBG4_8197F) << BIT_SHIFT_FW_DBG4_8197F)
#define BITS_FW_DBG4_8197F (BIT_MASK_FW_DBG4_8197F << BIT_SHIFT_FW_DBG4_8197F)
#define BIT_CLEAR_FW_DBG4_8197F(x) ((x) & (~BITS_FW_DBG4_8197F))
#define BIT_GET_FW_DBG4_8197F(x)                                               \
	(((x) >> BIT_SHIFT_FW_DBG4_8197F) & BIT_MASK_FW_DBG4_8197F)
#define BIT_SET_FW_DBG4_8197F(x, v)                                            \
	(BIT_CLEAR_FW_DBG4_8197F(x) | BIT_FW_DBG4_8197F(v))

/* 2 REG_FW_DBG5_8197F */

#define BIT_SHIFT_FW_DBG5_8197F 0
#define BIT_MASK_FW_DBG5_8197F 0xffffffffL
#define BIT_FW_DBG5_8197F(x)                                                   \
	(((x) & BIT_MASK_FW_DBG5_8197F) << BIT_SHIFT_FW_DBG5_8197F)
#define BITS_FW_DBG5_8197F (BIT_MASK_FW_DBG5_8197F << BIT_SHIFT_FW_DBG5_8197F)
#define BIT_CLEAR_FW_DBG5_8197F(x) ((x) & (~BITS_FW_DBG5_8197F))
#define BIT_GET_FW_DBG5_8197F(x)                                               \
	(((x) >> BIT_SHIFT_FW_DBG5_8197F) & BIT_MASK_FW_DBG5_8197F)
#define BIT_SET_FW_DBG5_8197F(x, v)                                            \
	(BIT_CLEAR_FW_DBG5_8197F(x) | BIT_FW_DBG5_8197F(v))

/* 2 REG_FW_DBG6_8197F */

#define BIT_SHIFT_FW_DBG6_8197F 0
#define BIT_MASK_FW_DBG6_8197F 0xffffffffL
#define BIT_FW_DBG6_8197F(x)                                                   \
	(((x) & BIT_MASK_FW_DBG6_8197F) << BIT_SHIFT_FW_DBG6_8197F)
#define BITS_FW_DBG6_8197F (BIT_MASK_FW_DBG6_8197F << BIT_SHIFT_FW_DBG6_8197F)
#define BIT_CLEAR_FW_DBG6_8197F(x) ((x) & (~BITS_FW_DBG6_8197F))
#define BIT_GET_FW_DBG6_8197F(x)                                               \
	(((x) >> BIT_SHIFT_FW_DBG6_8197F) & BIT_MASK_FW_DBG6_8197F)
#define BIT_SET_FW_DBG6_8197F(x, v)                                            \
	(BIT_CLEAR_FW_DBG6_8197F(x) | BIT_FW_DBG6_8197F(v))

/* 2 REG_FW_DBG7_8197F */

#define BIT_SHIFT_FW_DBG7_8197F 0
#define BIT_MASK_FW_DBG7_8197F 0xffffffffL
#define BIT_FW_DBG7_8197F(x)                                                   \
	(((x) & BIT_MASK_FW_DBG7_8197F) << BIT_SHIFT_FW_DBG7_8197F)
#define BITS_FW_DBG7_8197F (BIT_MASK_FW_DBG7_8197F << BIT_SHIFT_FW_DBG7_8197F)
#define BIT_CLEAR_FW_DBG7_8197F(x) ((x) & (~BITS_FW_DBG7_8197F))
#define BIT_GET_FW_DBG7_8197F(x)                                               \
	(((x) >> BIT_SHIFT_FW_DBG7_8197F) & BIT_MASK_FW_DBG7_8197F)
#define BIT_SET_FW_DBG7_8197F(x, v)                                            \
	(BIT_CLEAR_FW_DBG7_8197F(x) | BIT_FW_DBG7_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_CR_8197F (ENABLE FUNCTION REGISTER) */
#define BIT_MACIO_TIMEOUT_EN_8197F BIT(29)

#define BIT_SHIFT_LBMODE_8197F 24
#define BIT_MASK_LBMODE_8197F 0x1f
#define BIT_LBMODE_8197F(x)                                                    \
	(((x) & BIT_MASK_LBMODE_8197F) << BIT_SHIFT_LBMODE_8197F)
#define BITS_LBMODE_8197F (BIT_MASK_LBMODE_8197F << BIT_SHIFT_LBMODE_8197F)
#define BIT_CLEAR_LBMODE_8197F(x) ((x) & (~BITS_LBMODE_8197F))
#define BIT_GET_LBMODE_8197F(x)                                                \
	(((x) >> BIT_SHIFT_LBMODE_8197F) & BIT_MASK_LBMODE_8197F)
#define BIT_SET_LBMODE_8197F(x, v)                                             \
	(BIT_CLEAR_LBMODE_8197F(x) | BIT_LBMODE_8197F(v))

#define BIT_SHIFT_NETYPE1_8197F 18
#define BIT_MASK_NETYPE1_8197F 0x3
#define BIT_NETYPE1_8197F(x)                                                   \
	(((x) & BIT_MASK_NETYPE1_8197F) << BIT_SHIFT_NETYPE1_8197F)
#define BITS_NETYPE1_8197F (BIT_MASK_NETYPE1_8197F << BIT_SHIFT_NETYPE1_8197F)
#define BIT_CLEAR_NETYPE1_8197F(x) ((x) & (~BITS_NETYPE1_8197F))
#define BIT_GET_NETYPE1_8197F(x)                                               \
	(((x) >> BIT_SHIFT_NETYPE1_8197F) & BIT_MASK_NETYPE1_8197F)
#define BIT_SET_NETYPE1_8197F(x, v)                                            \
	(BIT_CLEAR_NETYPE1_8197F(x) | BIT_NETYPE1_8197F(v))

#define BIT_SHIFT_NETYPE0_8197F 16
#define BIT_MASK_NETYPE0_8197F 0x3
#define BIT_NETYPE0_8197F(x)                                                   \
	(((x) & BIT_MASK_NETYPE0_8197F) << BIT_SHIFT_NETYPE0_8197F)
#define BITS_NETYPE0_8197F (BIT_MASK_NETYPE0_8197F << BIT_SHIFT_NETYPE0_8197F)
#define BIT_CLEAR_NETYPE0_8197F(x) ((x) & (~BITS_NETYPE0_8197F))
#define BIT_GET_NETYPE0_8197F(x)                                               \
	(((x) >> BIT_SHIFT_NETYPE0_8197F) & BIT_MASK_NETYPE0_8197F)
#define BIT_SET_NETYPE0_8197F(x, v)                                            \
	(BIT_CLEAR_NETYPE0_8197F(x) | BIT_NETYPE0_8197F(v))

#define BIT_STAT_FUNC_RST_8197F BIT(13)
#define BIT_I2C_MAILBOX_EN_8197F BIT(12)
#define BIT_SHCUT_EN_8197F BIT(11)
#define BIT_32K_CAL_TMR_EN_8197F BIT(10)
#define BIT_MAC_SEC_EN_8197F BIT(9)
#define BIT_ENSWBCN_8197F BIT(8)
#define BIT_MACRXEN_8197F BIT(7)
#define BIT_MACTXEN_8197F BIT(6)
#define BIT_SCHEDULE_EN_8197F BIT(5)
#define BIT_PROTOCOL_EN_8197F BIT(4)
#define BIT_RXDMA_EN_8197F BIT(3)
#define BIT_TXDMA_EN_8197F BIT(2)
#define BIT_HCI_RXDMA_EN_8197F BIT(1)
#define BIT_HCI_TXDMA_EN_8197F BIT(0)

/* 2 REG_TSF_CLK_STATE_8197F */
#define BIT_TSF_CLK_STABLE_8197F BIT(15)

/* 2 REG_TXDMA_PQ_MAP_8197F */

#define BIT_SHIFT_TXDMA_HIQ_MAP_8197F 14
#define BIT_MASK_TXDMA_HIQ_MAP_8197F 0x3
#define BIT_TXDMA_HIQ_MAP_8197F(x)                                             \
	(((x) & BIT_MASK_TXDMA_HIQ_MAP_8197F) << BIT_SHIFT_TXDMA_HIQ_MAP_8197F)
#define BITS_TXDMA_HIQ_MAP_8197F                                               \
	(BIT_MASK_TXDMA_HIQ_MAP_8197F << BIT_SHIFT_TXDMA_HIQ_MAP_8197F)
#define BIT_CLEAR_TXDMA_HIQ_MAP_8197F(x) ((x) & (~BITS_TXDMA_HIQ_MAP_8197F))
#define BIT_GET_TXDMA_HIQ_MAP_8197F(x)                                         \
	(((x) >> BIT_SHIFT_TXDMA_HIQ_MAP_8197F) & BIT_MASK_TXDMA_HIQ_MAP_8197F)
#define BIT_SET_TXDMA_HIQ_MAP_8197F(x, v)                                      \
	(BIT_CLEAR_TXDMA_HIQ_MAP_8197F(x) | BIT_TXDMA_HIQ_MAP_8197F(v))

#define BIT_SHIFT_TXDMA_MGQ_MAP_8197F 12
#define BIT_MASK_TXDMA_MGQ_MAP_8197F 0x3
#define BIT_TXDMA_MGQ_MAP_8197F(x)                                             \
	(((x) & BIT_MASK_TXDMA_MGQ_MAP_8197F) << BIT_SHIFT_TXDMA_MGQ_MAP_8197F)
#define BITS_TXDMA_MGQ_MAP_8197F                                               \
	(BIT_MASK_TXDMA_MGQ_MAP_8197F << BIT_SHIFT_TXDMA_MGQ_MAP_8197F)
#define BIT_CLEAR_TXDMA_MGQ_MAP_8197F(x) ((x) & (~BITS_TXDMA_MGQ_MAP_8197F))
#define BIT_GET_TXDMA_MGQ_MAP_8197F(x)                                         \
	(((x) >> BIT_SHIFT_TXDMA_MGQ_MAP_8197F) & BIT_MASK_TXDMA_MGQ_MAP_8197F)
#define BIT_SET_TXDMA_MGQ_MAP_8197F(x, v)                                      \
	(BIT_CLEAR_TXDMA_MGQ_MAP_8197F(x) | BIT_TXDMA_MGQ_MAP_8197F(v))

#define BIT_SHIFT_TXDMA_BKQ_MAP_8197F 10
#define BIT_MASK_TXDMA_BKQ_MAP_8197F 0x3
#define BIT_TXDMA_BKQ_MAP_8197F(x)                                             \
	(((x) & BIT_MASK_TXDMA_BKQ_MAP_8197F) << BIT_SHIFT_TXDMA_BKQ_MAP_8197F)
#define BITS_TXDMA_BKQ_MAP_8197F                                               \
	(BIT_MASK_TXDMA_BKQ_MAP_8197F << BIT_SHIFT_TXDMA_BKQ_MAP_8197F)
#define BIT_CLEAR_TXDMA_BKQ_MAP_8197F(x) ((x) & (~BITS_TXDMA_BKQ_MAP_8197F))
#define BIT_GET_TXDMA_BKQ_MAP_8197F(x)                                         \
	(((x) >> BIT_SHIFT_TXDMA_BKQ_MAP_8197F) & BIT_MASK_TXDMA_BKQ_MAP_8197F)
#define BIT_SET_TXDMA_BKQ_MAP_8197F(x, v)                                      \
	(BIT_CLEAR_TXDMA_BKQ_MAP_8197F(x) | BIT_TXDMA_BKQ_MAP_8197F(v))

#define BIT_SHIFT_TXDMA_BEQ_MAP_8197F 8
#define BIT_MASK_TXDMA_BEQ_MAP_8197F 0x3
#define BIT_TXDMA_BEQ_MAP_8197F(x)                                             \
	(((x) & BIT_MASK_TXDMA_BEQ_MAP_8197F) << BIT_SHIFT_TXDMA_BEQ_MAP_8197F)
#define BITS_TXDMA_BEQ_MAP_8197F                                               \
	(BIT_MASK_TXDMA_BEQ_MAP_8197F << BIT_SHIFT_TXDMA_BEQ_MAP_8197F)
#define BIT_CLEAR_TXDMA_BEQ_MAP_8197F(x) ((x) & (~BITS_TXDMA_BEQ_MAP_8197F))
#define BIT_GET_TXDMA_BEQ_MAP_8197F(x)                                         \
	(((x) >> BIT_SHIFT_TXDMA_BEQ_MAP_8197F) & BIT_MASK_TXDMA_BEQ_MAP_8197F)
#define BIT_SET_TXDMA_BEQ_MAP_8197F(x, v)                                      \
	(BIT_CLEAR_TXDMA_BEQ_MAP_8197F(x) | BIT_TXDMA_BEQ_MAP_8197F(v))

#define BIT_SHIFT_TXDMA_VIQ_MAP_8197F 6
#define BIT_MASK_TXDMA_VIQ_MAP_8197F 0x3
#define BIT_TXDMA_VIQ_MAP_8197F(x)                                             \
	(((x) & BIT_MASK_TXDMA_VIQ_MAP_8197F) << BIT_SHIFT_TXDMA_VIQ_MAP_8197F)
#define BITS_TXDMA_VIQ_MAP_8197F                                               \
	(BIT_MASK_TXDMA_VIQ_MAP_8197F << BIT_SHIFT_TXDMA_VIQ_MAP_8197F)
#define BIT_CLEAR_TXDMA_VIQ_MAP_8197F(x) ((x) & (~BITS_TXDMA_VIQ_MAP_8197F))
#define BIT_GET_TXDMA_VIQ_MAP_8197F(x)                                         \
	(((x) >> BIT_SHIFT_TXDMA_VIQ_MAP_8197F) & BIT_MASK_TXDMA_VIQ_MAP_8197F)
#define BIT_SET_TXDMA_VIQ_MAP_8197F(x, v)                                      \
	(BIT_CLEAR_TXDMA_VIQ_MAP_8197F(x) | BIT_TXDMA_VIQ_MAP_8197F(v))

#define BIT_SHIFT_TXDMA_VOQ_MAP_8197F 4
#define BIT_MASK_TXDMA_VOQ_MAP_8197F 0x3
#define BIT_TXDMA_VOQ_MAP_8197F(x)                                             \
	(((x) & BIT_MASK_TXDMA_VOQ_MAP_8197F) << BIT_SHIFT_TXDMA_VOQ_MAP_8197F)
#define BITS_TXDMA_VOQ_MAP_8197F                                               \
	(BIT_MASK_TXDMA_VOQ_MAP_8197F << BIT_SHIFT_TXDMA_VOQ_MAP_8197F)
#define BIT_CLEAR_TXDMA_VOQ_MAP_8197F(x) ((x) & (~BITS_TXDMA_VOQ_MAP_8197F))
#define BIT_GET_TXDMA_VOQ_MAP_8197F(x)                                         \
	(((x) >> BIT_SHIFT_TXDMA_VOQ_MAP_8197F) & BIT_MASK_TXDMA_VOQ_MAP_8197F)
#define BIT_SET_TXDMA_VOQ_MAP_8197F(x, v)                                      \
	(BIT_CLEAR_TXDMA_VOQ_MAP_8197F(x) | BIT_TXDMA_VOQ_MAP_8197F(v))

/* 2 REG_NOT_VALID_8197F */
#define BIT_RXDMA_AGG_EN_8197F BIT(2)
#define BIT_RXSHFT_EN_8197F BIT(1)
#define BIT_RXDMA_ARBBW_EN_8197F BIT(0)

/* 2 REG_TRXFF_BNDY_8197F */

#define BIT_SHIFT_RXFFOVFL_RSV_V2_8197F 8
#define BIT_MASK_RXFFOVFL_RSV_V2_8197F 0xf
#define BIT_RXFFOVFL_RSV_V2_8197F(x)                                           \
	(((x) & BIT_MASK_RXFFOVFL_RSV_V2_8197F)                                \
	 << BIT_SHIFT_RXFFOVFL_RSV_V2_8197F)
#define BITS_RXFFOVFL_RSV_V2_8197F                                             \
	(BIT_MASK_RXFFOVFL_RSV_V2_8197F << BIT_SHIFT_RXFFOVFL_RSV_V2_8197F)
#define BIT_CLEAR_RXFFOVFL_RSV_V2_8197F(x) ((x) & (~BITS_RXFFOVFL_RSV_V2_8197F))
#define BIT_GET_RXFFOVFL_RSV_V2_8197F(x)                                       \
	(((x) >> BIT_SHIFT_RXFFOVFL_RSV_V2_8197F) &                            \
	 BIT_MASK_RXFFOVFL_RSV_V2_8197F)
#define BIT_SET_RXFFOVFL_RSV_V2_8197F(x, v)                                    \
	(BIT_CLEAR_RXFFOVFL_RSV_V2_8197F(x) | BIT_RXFFOVFL_RSV_V2_8197F(v))

#define BIT_SHIFT_TXPKTBUF_PGBNDY_8197F 0
#define BIT_MASK_TXPKTBUF_PGBNDY_8197F 0xff
#define BIT_TXPKTBUF_PGBNDY_8197F(x)                                           \
	(((x) & BIT_MASK_TXPKTBUF_PGBNDY_8197F)                                \
	 << BIT_SHIFT_TXPKTBUF_PGBNDY_8197F)
#define BITS_TXPKTBUF_PGBNDY_8197F                                             \
	(BIT_MASK_TXPKTBUF_PGBNDY_8197F << BIT_SHIFT_TXPKTBUF_PGBNDY_8197F)
#define BIT_CLEAR_TXPKTBUF_PGBNDY_8197F(x) ((x) & (~BITS_TXPKTBUF_PGBNDY_8197F))
#define BIT_GET_TXPKTBUF_PGBNDY_8197F(x)                                       \
	(((x) >> BIT_SHIFT_TXPKTBUF_PGBNDY_8197F) &                            \
	 BIT_MASK_TXPKTBUF_PGBNDY_8197F)
#define BIT_SET_TXPKTBUF_PGBNDY_8197F(x, v)                                    \
	(BIT_CLEAR_TXPKTBUF_PGBNDY_8197F(x) | BIT_TXPKTBUF_PGBNDY_8197F(v))

/* 2 REG_PTA_I2C_MBOX_8197F */

/* 2 REG_NOT_VALID_8197F */

#define BIT_SHIFT_I2C_M_STATUS_8197F 8
#define BIT_MASK_I2C_M_STATUS_8197F 0xf
#define BIT_I2C_M_STATUS_8197F(x)                                              \
	(((x) & BIT_MASK_I2C_M_STATUS_8197F) << BIT_SHIFT_I2C_M_STATUS_8197F)
#define BITS_I2C_M_STATUS_8197F                                                \
	(BIT_MASK_I2C_M_STATUS_8197F << BIT_SHIFT_I2C_M_STATUS_8197F)
#define BIT_CLEAR_I2C_M_STATUS_8197F(x) ((x) & (~BITS_I2C_M_STATUS_8197F))
#define BIT_GET_I2C_M_STATUS_8197F(x)                                          \
	(((x) >> BIT_SHIFT_I2C_M_STATUS_8197F) & BIT_MASK_I2C_M_STATUS_8197F)
#define BIT_SET_I2C_M_STATUS_8197F(x, v)                                       \
	(BIT_CLEAR_I2C_M_STATUS_8197F(x) | BIT_I2C_M_STATUS_8197F(v))

#define BIT_SHIFT_I2C_M_BUS_GNT_FW_8197F 4
#define BIT_MASK_I2C_M_BUS_GNT_FW_8197F 0x7
#define BIT_I2C_M_BUS_GNT_FW_8197F(x)                                          \
	(((x) & BIT_MASK_I2C_M_BUS_GNT_FW_8197F)                               \
	 << BIT_SHIFT_I2C_M_BUS_GNT_FW_8197F)
#define BITS_I2C_M_BUS_GNT_FW_8197F                                            \
	(BIT_MASK_I2C_M_BUS_GNT_FW_8197F << BIT_SHIFT_I2C_M_BUS_GNT_FW_8197F)
#define BIT_CLEAR_I2C_M_BUS_GNT_FW_8197F(x)                                    \
	((x) & (~BITS_I2C_M_BUS_GNT_FW_8197F))
#define BIT_GET_I2C_M_BUS_GNT_FW_8197F(x)                                      \
	(((x) >> BIT_SHIFT_I2C_M_BUS_GNT_FW_8197F) &                           \
	 BIT_MASK_I2C_M_BUS_GNT_FW_8197F)
#define BIT_SET_I2C_M_BUS_GNT_FW_8197F(x, v)                                   \
	(BIT_CLEAR_I2C_M_BUS_GNT_FW_8197F(x) | BIT_I2C_M_BUS_GNT_FW_8197F(v))

#define BIT_I2C_M_GNT_FW_8197F BIT(3)

#define BIT_SHIFT_I2C_M_SPEED_8197F 1
#define BIT_MASK_I2C_M_SPEED_8197F 0x3
#define BIT_I2C_M_SPEED_8197F(x)                                               \
	(((x) & BIT_MASK_I2C_M_SPEED_8197F) << BIT_SHIFT_I2C_M_SPEED_8197F)
#define BITS_I2C_M_SPEED_8197F                                                 \
	(BIT_MASK_I2C_M_SPEED_8197F << BIT_SHIFT_I2C_M_SPEED_8197F)
#define BIT_CLEAR_I2C_M_SPEED_8197F(x) ((x) & (~BITS_I2C_M_SPEED_8197F))
#define BIT_GET_I2C_M_SPEED_8197F(x)                                           \
	(((x) >> BIT_SHIFT_I2C_M_SPEED_8197F) & BIT_MASK_I2C_M_SPEED_8197F)
#define BIT_SET_I2C_M_SPEED_8197F(x, v)                                        \
	(BIT_CLEAR_I2C_M_SPEED_8197F(x) | BIT_I2C_M_SPEED_8197F(v))

#define BIT_I2C_M_UNLOCK_8197F BIT(0)

/* 2 REG_RXFF_BNDY_8197F */

/* 2 REG_NOT_VALID_8197F */

#define BIT_SHIFT_RXFF0_BNDY_V2_8197F 0
#define BIT_MASK_RXFF0_BNDY_V2_8197F 0x3ffff
#define BIT_RXFF0_BNDY_V2_8197F(x)                                             \
	(((x) & BIT_MASK_RXFF0_BNDY_V2_8197F) << BIT_SHIFT_RXFF0_BNDY_V2_8197F)
#define BITS_RXFF0_BNDY_V2_8197F                                               \
	(BIT_MASK_RXFF0_BNDY_V2_8197F << BIT_SHIFT_RXFF0_BNDY_V2_8197F)
#define BIT_CLEAR_RXFF0_BNDY_V2_8197F(x) ((x) & (~BITS_RXFF0_BNDY_V2_8197F))
#define BIT_GET_RXFF0_BNDY_V2_8197F(x)                                         \
	(((x) >> BIT_SHIFT_RXFF0_BNDY_V2_8197F) & BIT_MASK_RXFF0_BNDY_V2_8197F)
#define BIT_SET_RXFF0_BNDY_V2_8197F(x, v)                                      \
	(BIT_CLEAR_RXFF0_BNDY_V2_8197F(x) | BIT_RXFF0_BNDY_V2_8197F(v))

/* 2 REG_FE1IMR_8197F */
#define BIT_BB_STOP_RX_INT_EN_8197F BIT(29)
#define BIT_FS_RXDMA2_DONE_INT_EN_8197F BIT(28)
#define BIT_FS_RXDONE3_INT_EN_8197F BIT(27)
#define BIT_FS_RXDONE2_INT_EN_8197F BIT(26)
#define BIT_FS_RX_BCN_P4_INT_EN_8197F BIT(25)
#define BIT_FS_RX_BCN_P3_INT_EN_8197F BIT(24)
#define BIT_FS_RX_BCN_P2_INT_EN_8197F BIT(23)
#define BIT_FS_RX_BCN_P1_INT_EN_8197F BIT(22)
#define BIT_FS_RX_BCN_P0_INT_EN_8197F BIT(21)
#define BIT_FS_RX_UMD0_INT_EN_8197F BIT(20)
#define BIT_FS_RX_UMD1_INT_EN_8197F BIT(19)
#define BIT_FS_RX_BMD0_INT_EN_8197F BIT(18)
#define BIT_FS_RX_BMD1_INT_EN_8197F BIT(17)
#define BIT_FS_RXDONE_INT_EN_8197F BIT(16)
#define BIT_FS_WWLAN_INT_EN_8197F BIT(15)
#define BIT_FS_SOUND_DONE_INT_EN_8197F BIT(14)
#define BIT_FS_LP_STBY_INT_EN_8197F BIT(13)
#define BIT_FS_TRL_MTR_INT_EN_8197F BIT(12)
#define BIT_FS_BF1_PRETO_INT_EN_8197F BIT(11)
#define BIT_FS_BF0_PRETO_INT_EN_8197F BIT(10)
#define BIT_FS_PTCL_RELEASE_MACID_INT_EN_8197F BIT(9)
#define BIT_FS_LTE_COEX_EN_8197F BIT(6)
#define BIT_FS_WLACTOFF_INT_EN_8197F BIT(5)
#define BIT_FS_WLACTON_INT_EN_8197F BIT(4)
#define BIT_FS_BTCMD_INT_EN_8197F BIT(3)
#define BIT_FS_REG_MAILBOX_TO_I2C_INT_EN_8197F BIT(2)
#define BIT_FS_TRPC_TO_INT_EN_V1_8197F BIT(1)
#define BIT_FS_RPC_O_T_INT_EN_V1_8197F BIT(0)

/* 2 REG_FE1ISR_8197F */
#define BIT_BB_STOP_RX_INT_8197F BIT(29)
#define BIT_FS_RXDMA2_DONE_INT_8197F BIT(28)
#define BIT_FS_RXDONE3_INT_8197F BIT(27)
#define BIT_FS_RXDONE2_INT_8197F BIT(26)
#define BIT_FS_RX_BCN_P4_INT_8197F BIT(25)
#define BIT_FS_RX_BCN_P3_INT_8197F BIT(24)
#define BIT_FS_RX_BCN_P2_INT_8197F BIT(23)
#define BIT_FS_RX_BCN_P1_INT_8197F BIT(22)
#define BIT_FS_RX_BCN_P0_INT_8197F BIT(21)
#define BIT_FS_RX_UMD0_INT_8197F BIT(20)
#define BIT_FS_RX_UMD1_INT_8197F BIT(19)
#define BIT_FS_RX_BMD0_INT_8197F BIT(18)
#define BIT_FS_RX_BMD1_INT_8197F BIT(17)
#define BIT_FS_RXDONE_INT_8197F BIT(16)
#define BIT_FS_WWLAN_INT_8197F BIT(15)
#define BIT_FS_SOUND_DONE_INT_8197F BIT(14)
#define BIT_FS_LP_STBY_INT_8197F BIT(13)
#define BIT_FS_TRL_MTR_INT_8197F BIT(12)
#define BIT_FS_BF1_PRETO_INT_8197F BIT(11)
#define BIT_FS_BF0_PRETO_INT_8197F BIT(10)
#define BIT_FS_PTCL_RELEASE_MACID_INT_8197F BIT(9)
#define BIT_FS_LTE_COEX_INT_8197F BIT(6)
#define BIT_FS_WLACTOFF_INT_8197F BIT(5)
#define BIT_FS_WLACTON_INT_8197F BIT(4)
#define BIT_FS_BCN_RX_INT_INT_8197F BIT(3)
#define BIT_FS_MAILBOX_TO_I2C_INT_8197F BIT(2)
#define BIT_FS_TRPC_TO_INT_8197F BIT(1)
#define BIT_FS_RPC_O_T_INT_8197F BIT(0)

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_CPWM_8197F */
#define BIT_CPWM_TOGGLING_8197F BIT(31)

#define BIT_SHIFT_CPWM_MOD_8197F 24
#define BIT_MASK_CPWM_MOD_8197F 0x7f
#define BIT_CPWM_MOD_8197F(x)                                                  \
	(((x) & BIT_MASK_CPWM_MOD_8197F) << BIT_SHIFT_CPWM_MOD_8197F)
#define BITS_CPWM_MOD_8197F                                                    \
	(BIT_MASK_CPWM_MOD_8197F << BIT_SHIFT_CPWM_MOD_8197F)
#define BIT_CLEAR_CPWM_MOD_8197F(x) ((x) & (~BITS_CPWM_MOD_8197F))
#define BIT_GET_CPWM_MOD_8197F(x)                                              \
	(((x) >> BIT_SHIFT_CPWM_MOD_8197F) & BIT_MASK_CPWM_MOD_8197F)
#define BIT_SET_CPWM_MOD_8197F(x, v)                                           \
	(BIT_CLEAR_CPWM_MOD_8197F(x) | BIT_CPWM_MOD_8197F(v))

/* 2 REG_FWIMR_8197F */
#define BIT_FS_TXBCNOK_MB7_INT_EN_8197F BIT(31)
#define BIT_FS_TXBCNOK_MB6_INT_EN_8197F BIT(30)
#define BIT_FS_TXBCNOK_MB5_INT_EN_8197F BIT(29)
#define BIT_FS_TXBCNOK_MB4_INT_EN_8197F BIT(28)
#define BIT_FS_TXBCNOK_MB3_INT_EN_8197F BIT(27)
#define BIT_FS_TXBCNOK_MB2_INT_EN_8197F BIT(26)
#define BIT_FS_TXBCNOK_MB1_INT_EN_8197F BIT(25)
#define BIT_FS_TXBCNOK_MB0_INT_EN_8197F BIT(24)
#define BIT_FS_TXBCNERR_MB7_INT_EN_8197F BIT(23)
#define BIT_FS_TXBCNERR_MB6_INT_EN_8197F BIT(22)
#define BIT_FS_TXBCNERR_MB5_INT_EN_8197F BIT(21)
#define BIT_FS_TXBCNERR_MB4_INT_EN_8197F BIT(20)
#define BIT_FS_TXBCNERR_MB3_INT_EN_8197F BIT(19)
#define BIT_FS_TXBCNERR_MB2_INT_EN_8197F BIT(18)
#define BIT_FS_TXBCNERR_MB1_INT_EN_8197F BIT(17)
#define BIT_FS_TXBCNERR_MB0_INT_EN_8197F BIT(16)
#define BIT_CPUMGN_POLLED_PKT_DONE_INT_EN_8197F BIT(15)
#define BIT_FS_MGNTQ_RPTR_RELEASE_INT_EN_8197F BIT(13)
#define BIT_FS_MGNTQFF_TO_INT_EN_8197F BIT(12)
#define BIT_FS_DDMA1_LP_INT_ENBIT_CPUMGN_POLLED_PKT_BUSY_ERR_INT_EN_8197F      \
	BIT(11)
#define BIT_FS_DDMA1_HP_INT_EN_8197F BIT(10)
#define BIT_FS_DDMA0_LP_INT_EN_8197F BIT(9)
#define BIT_FS_DDMA0_HP_INT_EN_8197F BIT(8)
#define BIT_FS_TRXRPT_INT_EN_8197F BIT(7)
#define BIT_FS_C2H_W_READY_INT_EN_8197F BIT(6)
#define BIT_FS_HRCV_INT_EN_8197F BIT(5)
#define BIT_FS_H2CCMD_INT_EN_8197F BIT(4)
#define BIT_FS_TXPKTIN_INT_EN_8197F BIT(3)
#define BIT_FS_ERRORHDL_INT_EN_8197F BIT(2)
#define BIT_FS_TXCCX_INT_EN_8197F BIT(1)
#define BIT_FS_TXCLOSE_INT_EN_8197F BIT(0)

/* 2 REG_FWISR_8197F */
#define BIT_FS_TXBCNOK_MB7_INT_8197F BIT(31)
#define BIT_FS_TXBCNOK_MB6_INT_8197F BIT(30)
#define BIT_FS_TXBCNOK_MB5_INT_8197F BIT(29)
#define BIT_FS_TXBCNOK_MB4_INT_8197F BIT(28)
#define BIT_FS_TXBCNOK_MB3_INT_8197F BIT(27)
#define BIT_FS_TXBCNOK_MB2_INT_8197F BIT(26)
#define BIT_FS_TXBCNOK_MB1_INT_8197F BIT(25)
#define BIT_FS_TXBCNOK_MB0_INT_8197F BIT(24)
#define BIT_FS_TXBCNERR_MB7_INT_8197F BIT(23)
#define BIT_FS_TXBCNERR_MB6_INT_8197F BIT(22)
#define BIT_FS_TXBCNERR_MB5_INT_8197F BIT(21)
#define BIT_FS_TXBCNERR_MB4_INT_8197F BIT(20)
#define BIT_FS_TXBCNERR_MB3_INT_8197F BIT(19)
#define BIT_FS_TXBCNERR_MB2_INT_8197F BIT(18)
#define BIT_FS_TXBCNERR_MB1_INT_8197F BIT(17)
#define BIT_FS_TXBCNERR_MB0_INT_8197F BIT(16)
#define BIT_CPUMGN_POLLED_PKT_DONE_INT_8197F BIT(15)
#define BIT_FS_MGNTQ_RPTR_RELEASE_INT_8197F BIT(13)
#define BIT_FS_MGNTQFF_TO_INT_8197F BIT(12)
#define BIT_FS_DDMA1_LP_INTBIT_CPUMGN_POLLED_PKT_BUSY_ERR_INT_8197F BIT(11)
#define BIT_FS_DDMA1_HP_INT_8197F BIT(10)
#define BIT_FS_DDMA0_LP_INT_8197F BIT(9)
#define BIT_FS_DDMA0_HP_INT_8197F BIT(8)
#define BIT_FS_TRXRPT_INT_8197F BIT(7)
#define BIT_FS_C2H_W_READY_INT_8197F BIT(6)
#define BIT_FS_HRCV_INT_8197F BIT(5)
#define BIT_FS_H2CCMD_INT_8197F BIT(4)
#define BIT_FS_TXPKTIN_INT_8197F BIT(3)
#define BIT_FS_ERRORHDL_INT_8197F BIT(2)
#define BIT_FS_TXCCX_INT_8197F BIT(1)
#define BIT_FS_TXCLOSE_INT_8197F BIT(0)

/* 2 REG_FTIMR_8197F */
#define BIT_PS_TIMER_C_EARLY_INT_EN_8197F BIT(23)
#define BIT_PS_TIMER_B_EARLY_INT_EN_8197F BIT(22)
#define BIT_PS_TIMER_A_EARLY_INT_EN_8197F BIT(21)
#define BIT_CPUMGQ_TX_TIMER_EARLY_INT_EN_8197F BIT(20)
#define BIT_PS_TIMER_C_INT_EN_8197F BIT(19)
#define BIT_PS_TIMER_B_INT_EN_8197F BIT(18)
#define BIT_PS_TIMER_A_INT_EN_8197F BIT(17)
#define BIT_CPUMGQ_TX_TIMER_INT_EN_8197F BIT(16)
#define BIT_FS_PS_TIMEOUT2_EN_8197F BIT(15)
#define BIT_FS_PS_TIMEOUT1_EN_8197F BIT(14)
#define BIT_FS_PS_TIMEOUT0_EN_8197F BIT(13)
#define BIT_FS_GTINT8_EN_8197F BIT(8)
#define BIT_FS_GTINT7_EN_8197F BIT(7)
#define BIT_FS_GTINT6_EN_8197F BIT(6)
#define BIT_FS_GTINT5_EN_8197F BIT(5)
#define BIT_FS_GTINT4_EN_8197F BIT(4)
#define BIT_FS_GTINT3_EN_8197F BIT(3)
#define BIT_FS_GTINT2_EN_8197F BIT(2)
#define BIT_FS_GTINT1_EN_8197F BIT(1)
#define BIT_FS_GTINT0_EN_8197F BIT(0)

/* 2 REG_FTISR_8197F */
#define BIT_PS_TIMER_C_EARLY__INT_8197F BIT(23)
#define BIT_PS_TIMER_B_EARLY__INT_8197F BIT(22)
#define BIT_PS_TIMER_A_EARLY__INT_8197F BIT(21)
#define BIT_CPUMGQ_TX_TIMER_EARLY_INT_8197F BIT(20)
#define BIT_PS_TIMER_C_INT_8197F BIT(19)
#define BIT_PS_TIMER_B_INT_8197F BIT(18)
#define BIT_PS_TIMER_A_INT_8197F BIT(17)
#define BIT_CPUMGQ_TX_TIMER_INT_8197F BIT(16)
#define BIT_FS_PS_TIMEOUT2_INT_8197F BIT(15)
#define BIT_FS_PS_TIMEOUT1_INT_8197F BIT(14)
#define BIT_FS_PS_TIMEOUT0_INT_8197F BIT(13)
#define BIT_FS_GTINT8_INT_8197F BIT(8)
#define BIT_FS_GTINT7_INT_8197F BIT(7)
#define BIT_FS_GTINT6_INT_8197F BIT(6)
#define BIT_FS_GTINT5_INT_8197F BIT(5)
#define BIT_FS_GTINT4_INT_8197F BIT(4)
#define BIT_FS_GTINT3_INT_8197F BIT(3)
#define BIT_FS_GTINT2_INT_8197F BIT(2)
#define BIT_FS_GTINT1_INT_8197F BIT(1)
#define BIT_FS_GTINT0_INT_8197F BIT(0)

/* 2 REG_PKTBUF_DBG_CTRL_8197F */

#define BIT_SHIFT_PKTBUF_WRITE_EN_8197F 24
#define BIT_MASK_PKTBUF_WRITE_EN_8197F 0xff
#define BIT_PKTBUF_WRITE_EN_8197F(x)                                           \
	(((x) & BIT_MASK_PKTBUF_WRITE_EN_8197F)                                \
	 << BIT_SHIFT_PKTBUF_WRITE_EN_8197F)
#define BITS_PKTBUF_WRITE_EN_8197F                                             \
	(BIT_MASK_PKTBUF_WRITE_EN_8197F << BIT_SHIFT_PKTBUF_WRITE_EN_8197F)
#define BIT_CLEAR_PKTBUF_WRITE_EN_8197F(x) ((x) & (~BITS_PKTBUF_WRITE_EN_8197F))
#define BIT_GET_PKTBUF_WRITE_EN_8197F(x)                                       \
	(((x) >> BIT_SHIFT_PKTBUF_WRITE_EN_8197F) &                            \
	 BIT_MASK_PKTBUF_WRITE_EN_8197F)
#define BIT_SET_PKTBUF_WRITE_EN_8197F(x, v)                                    \
	(BIT_CLEAR_PKTBUF_WRITE_EN_8197F(x) | BIT_PKTBUF_WRITE_EN_8197F(v))

#define BIT_TXRPTBUF_DBG_8197F BIT(23)

/* 2 REG_NOT_VALID_8197F */
#define BIT_TXPKTBUF_DBG_V2_8197F BIT(20)
#define BIT_RXPKTBUF_DBG_8197F BIT(16)

#define BIT_SHIFT_PKTBUF_DBG_ADDR_8197F 0
#define BIT_MASK_PKTBUF_DBG_ADDR_8197F 0x1fff
#define BIT_PKTBUF_DBG_ADDR_8197F(x)                                           \
	(((x) & BIT_MASK_PKTBUF_DBG_ADDR_8197F)                                \
	 << BIT_SHIFT_PKTBUF_DBG_ADDR_8197F)
#define BITS_PKTBUF_DBG_ADDR_8197F                                             \
	(BIT_MASK_PKTBUF_DBG_ADDR_8197F << BIT_SHIFT_PKTBUF_DBG_ADDR_8197F)
#define BIT_CLEAR_PKTBUF_DBG_ADDR_8197F(x) ((x) & (~BITS_PKTBUF_DBG_ADDR_8197F))
#define BIT_GET_PKTBUF_DBG_ADDR_8197F(x)                                       \
	(((x) >> BIT_SHIFT_PKTBUF_DBG_ADDR_8197F) &                            \
	 BIT_MASK_PKTBUF_DBG_ADDR_8197F)
#define BIT_SET_PKTBUF_DBG_ADDR_8197F(x, v)                                    \
	(BIT_CLEAR_PKTBUF_DBG_ADDR_8197F(x) | BIT_PKTBUF_DBG_ADDR_8197F(v))

/* 2 REG_PKTBUF_DBG_DATA_L_8197F */

#define BIT_SHIFT_PKTBUF_DBG_DATA_L_8197F 0
#define BIT_MASK_PKTBUF_DBG_DATA_L_8197F 0xffffffffL
#define BIT_PKTBUF_DBG_DATA_L_8197F(x)                                         \
	(((x) & BIT_MASK_PKTBUF_DBG_DATA_L_8197F)                              \
	 << BIT_SHIFT_PKTBUF_DBG_DATA_L_8197F)
#define BITS_PKTBUF_DBG_DATA_L_8197F                                           \
	(BIT_MASK_PKTBUF_DBG_DATA_L_8197F << BIT_SHIFT_PKTBUF_DBG_DATA_L_8197F)
#define BIT_CLEAR_PKTBUF_DBG_DATA_L_8197F(x)                                   \
	((x) & (~BITS_PKTBUF_DBG_DATA_L_8197F))
#define BIT_GET_PKTBUF_DBG_DATA_L_8197F(x)                                     \
	(((x) >> BIT_SHIFT_PKTBUF_DBG_DATA_L_8197F) &                          \
	 BIT_MASK_PKTBUF_DBG_DATA_L_8197F)
#define BIT_SET_PKTBUF_DBG_DATA_L_8197F(x, v)                                  \
	(BIT_CLEAR_PKTBUF_DBG_DATA_L_8197F(x) | BIT_PKTBUF_DBG_DATA_L_8197F(v))

/* 2 REG_PKTBUF_DBG_DATA_H_8197F */

#define BIT_SHIFT_PKTBUF_DBG_DATA_H_8197F 0
#define BIT_MASK_PKTBUF_DBG_DATA_H_8197F 0xffffffffL
#define BIT_PKTBUF_DBG_DATA_H_8197F(x)                                         \
	(((x) & BIT_MASK_PKTBUF_DBG_DATA_H_8197F)                              \
	 << BIT_SHIFT_PKTBUF_DBG_DATA_H_8197F)
#define BITS_PKTBUF_DBG_DATA_H_8197F                                           \
	(BIT_MASK_PKTBUF_DBG_DATA_H_8197F << BIT_SHIFT_PKTBUF_DBG_DATA_H_8197F)
#define BIT_CLEAR_PKTBUF_DBG_DATA_H_8197F(x)                                   \
	((x) & (~BITS_PKTBUF_DBG_DATA_H_8197F))
#define BIT_GET_PKTBUF_DBG_DATA_H_8197F(x)                                     \
	(((x) >> BIT_SHIFT_PKTBUF_DBG_DATA_H_8197F) &                          \
	 BIT_MASK_PKTBUF_DBG_DATA_H_8197F)
#define BIT_SET_PKTBUF_DBG_DATA_H_8197F(x, v)                                  \
	(BIT_CLEAR_PKTBUF_DBG_DATA_H_8197F(x) | BIT_PKTBUF_DBG_DATA_H_8197F(v))

/* 2 REG_CPWM2_8197F */

#define BIT_SHIFT_L0S_TO_RCVY_NUM_8197F 16
#define BIT_MASK_L0S_TO_RCVY_NUM_8197F 0xff
#define BIT_L0S_TO_RCVY_NUM_8197F(x)                                           \
	(((x) & BIT_MASK_L0S_TO_RCVY_NUM_8197F)                                \
	 << BIT_SHIFT_L0S_TO_RCVY_NUM_8197F)
#define BITS_L0S_TO_RCVY_NUM_8197F                                             \
	(BIT_MASK_L0S_TO_RCVY_NUM_8197F << BIT_SHIFT_L0S_TO_RCVY_NUM_8197F)
#define BIT_CLEAR_L0S_TO_RCVY_NUM_8197F(x) ((x) & (~BITS_L0S_TO_RCVY_NUM_8197F))
#define BIT_GET_L0S_TO_RCVY_NUM_8197F(x)                                       \
	(((x) >> BIT_SHIFT_L0S_TO_RCVY_NUM_8197F) &                            \
	 BIT_MASK_L0S_TO_RCVY_NUM_8197F)
#define BIT_SET_L0S_TO_RCVY_NUM_8197F(x, v)                                    \
	(BIT_CLEAR_L0S_TO_RCVY_NUM_8197F(x) | BIT_L0S_TO_RCVY_NUM_8197F(v))

#define BIT_CPWM2_TOGGLING_8197F BIT(15)

#define BIT_SHIFT_CPWM2_MOD_8197F 0
#define BIT_MASK_CPWM2_MOD_8197F 0x7fff
#define BIT_CPWM2_MOD_8197F(x)                                                 \
	(((x) & BIT_MASK_CPWM2_MOD_8197F) << BIT_SHIFT_CPWM2_MOD_8197F)
#define BITS_CPWM2_MOD_8197F                                                   \
	(BIT_MASK_CPWM2_MOD_8197F << BIT_SHIFT_CPWM2_MOD_8197F)
#define BIT_CLEAR_CPWM2_MOD_8197F(x) ((x) & (~BITS_CPWM2_MOD_8197F))
#define BIT_GET_CPWM2_MOD_8197F(x)                                             \
	(((x) >> BIT_SHIFT_CPWM2_MOD_8197F) & BIT_MASK_CPWM2_MOD_8197F)
#define BIT_SET_CPWM2_MOD_8197F(x, v)                                          \
	(BIT_CLEAR_CPWM2_MOD_8197F(x) | BIT_CPWM2_MOD_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_TC0_CTRL_8197F */
#define BIT_TC0INT_EN_8197F BIT(26)
#define BIT_TC0MODE_8197F BIT(25)
#define BIT_TC0EN_8197F BIT(24)

#define BIT_SHIFT_TC0DATA_8197F 0
#define BIT_MASK_TC0DATA_8197F 0xffffff
#define BIT_TC0DATA_8197F(x)                                                   \
	(((x) & BIT_MASK_TC0DATA_8197F) << BIT_SHIFT_TC0DATA_8197F)
#define BITS_TC0DATA_8197F (BIT_MASK_TC0DATA_8197F << BIT_SHIFT_TC0DATA_8197F)
#define BIT_CLEAR_TC0DATA_8197F(x) ((x) & (~BITS_TC0DATA_8197F))
#define BIT_GET_TC0DATA_8197F(x)                                               \
	(((x) >> BIT_SHIFT_TC0DATA_8197F) & BIT_MASK_TC0DATA_8197F)
#define BIT_SET_TC0DATA_8197F(x, v)                                            \
	(BIT_CLEAR_TC0DATA_8197F(x) | BIT_TC0DATA_8197F(v))

/* 2 REG_TC1_CTRL_8197F */
#define BIT_TC1INT_EN_8197F BIT(26)
#define BIT_TC1MODE_8197F BIT(25)
#define BIT_TC1EN_8197F BIT(24)

#define BIT_SHIFT_TC1DATA_8197F 0
#define BIT_MASK_TC1DATA_8197F 0xffffff
#define BIT_TC1DATA_8197F(x)                                                   \
	(((x) & BIT_MASK_TC1DATA_8197F) << BIT_SHIFT_TC1DATA_8197F)
#define BITS_TC1DATA_8197F (BIT_MASK_TC1DATA_8197F << BIT_SHIFT_TC1DATA_8197F)
#define BIT_CLEAR_TC1DATA_8197F(x) ((x) & (~BITS_TC1DATA_8197F))
#define BIT_GET_TC1DATA_8197F(x)                                               \
	(((x) >> BIT_SHIFT_TC1DATA_8197F) & BIT_MASK_TC1DATA_8197F)
#define BIT_SET_TC1DATA_8197F(x, v)                                            \
	(BIT_CLEAR_TC1DATA_8197F(x) | BIT_TC1DATA_8197F(v))

/* 2 REG_TC2_CTRL_8197F */
#define BIT_TC2INT_EN_8197F BIT(26)
#define BIT_TC2MODE_8197F BIT(25)
#define BIT_TC2EN_8197F BIT(24)

#define BIT_SHIFT_TC2DATA_8197F 0
#define BIT_MASK_TC2DATA_8197F 0xffffff
#define BIT_TC2DATA_8197F(x)                                                   \
	(((x) & BIT_MASK_TC2DATA_8197F) << BIT_SHIFT_TC2DATA_8197F)
#define BITS_TC2DATA_8197F (BIT_MASK_TC2DATA_8197F << BIT_SHIFT_TC2DATA_8197F)
#define BIT_CLEAR_TC2DATA_8197F(x) ((x) & (~BITS_TC2DATA_8197F))
#define BIT_GET_TC2DATA_8197F(x)                                               \
	(((x) >> BIT_SHIFT_TC2DATA_8197F) & BIT_MASK_TC2DATA_8197F)
#define BIT_SET_TC2DATA_8197F(x, v)                                            \
	(BIT_CLEAR_TC2DATA_8197F(x) | BIT_TC2DATA_8197F(v))

/* 2 REG_TC3_CTRL_8197F */
#define BIT_TC3INT_EN_8197F BIT(26)
#define BIT_TC3MODE_8197F BIT(25)
#define BIT_TC3EN_8197F BIT(24)

#define BIT_SHIFT_TC3DATA_8197F 0
#define BIT_MASK_TC3DATA_8197F 0xffffff
#define BIT_TC3DATA_8197F(x)                                                   \
	(((x) & BIT_MASK_TC3DATA_8197F) << BIT_SHIFT_TC3DATA_8197F)
#define BITS_TC3DATA_8197F (BIT_MASK_TC3DATA_8197F << BIT_SHIFT_TC3DATA_8197F)
#define BIT_CLEAR_TC3DATA_8197F(x) ((x) & (~BITS_TC3DATA_8197F))
#define BIT_GET_TC3DATA_8197F(x)                                               \
	(((x) >> BIT_SHIFT_TC3DATA_8197F) & BIT_MASK_TC3DATA_8197F)
#define BIT_SET_TC3DATA_8197F(x, v)                                            \
	(BIT_CLEAR_TC3DATA_8197F(x) | BIT_TC3DATA_8197F(v))

/* 2 REG_TC4_CTRL_8197F */
#define BIT_TC4INT_EN_8197F BIT(26)
#define BIT_TC4MODE_8197F BIT(25)
#define BIT_TC4EN_8197F BIT(24)

#define BIT_SHIFT_TC4DATA_8197F 0
#define BIT_MASK_TC4DATA_8197F 0xffffff
#define BIT_TC4DATA_8197F(x)                                                   \
	(((x) & BIT_MASK_TC4DATA_8197F) << BIT_SHIFT_TC4DATA_8197F)
#define BITS_TC4DATA_8197F (BIT_MASK_TC4DATA_8197F << BIT_SHIFT_TC4DATA_8197F)
#define BIT_CLEAR_TC4DATA_8197F(x) ((x) & (~BITS_TC4DATA_8197F))
#define BIT_GET_TC4DATA_8197F(x)                                               \
	(((x) >> BIT_SHIFT_TC4DATA_8197F) & BIT_MASK_TC4DATA_8197F)
#define BIT_SET_TC4DATA_8197F(x, v)                                            \
	(BIT_CLEAR_TC4DATA_8197F(x) | BIT_TC4DATA_8197F(v))

/* 2 REG_TCUNIT_BASE_8197F */

#define BIT_SHIFT_TCUNIT_BASE_8197F 0
#define BIT_MASK_TCUNIT_BASE_8197F 0x3fff
#define BIT_TCUNIT_BASE_8197F(x)                                               \
	(((x) & BIT_MASK_TCUNIT_BASE_8197F) << BIT_SHIFT_TCUNIT_BASE_8197F)
#define BITS_TCUNIT_BASE_8197F                                                 \
	(BIT_MASK_TCUNIT_BASE_8197F << BIT_SHIFT_TCUNIT_BASE_8197F)
#define BIT_CLEAR_TCUNIT_BASE_8197F(x) ((x) & (~BITS_TCUNIT_BASE_8197F))
#define BIT_GET_TCUNIT_BASE_8197F(x)                                           \
	(((x) >> BIT_SHIFT_TCUNIT_BASE_8197F) & BIT_MASK_TCUNIT_BASE_8197F)
#define BIT_SET_TCUNIT_BASE_8197F(x, v)                                        \
	(BIT_CLEAR_TCUNIT_BASE_8197F(x) | BIT_TCUNIT_BASE_8197F(v))

/* 2 REG_TC5_CTRL_8197F */
#define BIT_TC5INT_EN_8197F BIT(26)
#define BIT_TC5MODE_8197F BIT(25)
#define BIT_TC5EN_8197F BIT(24)

#define BIT_SHIFT_TC5DATA_8197F 0
#define BIT_MASK_TC5DATA_8197F 0xffffff
#define BIT_TC5DATA_8197F(x)                                                   \
	(((x) & BIT_MASK_TC5DATA_8197F) << BIT_SHIFT_TC5DATA_8197F)
#define BITS_TC5DATA_8197F (BIT_MASK_TC5DATA_8197F << BIT_SHIFT_TC5DATA_8197F)
#define BIT_CLEAR_TC5DATA_8197F(x) ((x) & (~BITS_TC5DATA_8197F))
#define BIT_GET_TC5DATA_8197F(x)                                               \
	(((x) >> BIT_SHIFT_TC5DATA_8197F) & BIT_MASK_TC5DATA_8197F)
#define BIT_SET_TC5DATA_8197F(x, v)                                            \
	(BIT_CLEAR_TC5DATA_8197F(x) | BIT_TC5DATA_8197F(v))

/* 2 REG_TC6_CTRL_8197F */
#define BIT_TC6INT_EN_8197F BIT(26)
#define BIT_TC6MODE_8197F BIT(25)
#define BIT_TC6EN_8197F BIT(24)

#define BIT_SHIFT_TC6DATA_8197F 0
#define BIT_MASK_TC6DATA_8197F 0xffffff
#define BIT_TC6DATA_8197F(x)                                                   \
	(((x) & BIT_MASK_TC6DATA_8197F) << BIT_SHIFT_TC6DATA_8197F)
#define BITS_TC6DATA_8197F (BIT_MASK_TC6DATA_8197F << BIT_SHIFT_TC6DATA_8197F)
#define BIT_CLEAR_TC6DATA_8197F(x) ((x) & (~BITS_TC6DATA_8197F))
#define BIT_GET_TC6DATA_8197F(x)                                               \
	(((x) >> BIT_SHIFT_TC6DATA_8197F) & BIT_MASK_TC6DATA_8197F)
#define BIT_SET_TC6DATA_8197F(x, v)                                            \
	(BIT_CLEAR_TC6DATA_8197F(x) | BIT_TC6DATA_8197F(v))

/* 2 REG_MBIST_FAIL_8197F */

#define BIT_SHIFT_8051_MBIST_FAIL_8197F 26
#define BIT_MASK_8051_MBIST_FAIL_8197F 0x7
#define BIT_8051_MBIST_FAIL_8197F(x)                                           \
	(((x) & BIT_MASK_8051_MBIST_FAIL_8197F)                                \
	 << BIT_SHIFT_8051_MBIST_FAIL_8197F)
#define BITS_8051_MBIST_FAIL_8197F                                             \
	(BIT_MASK_8051_MBIST_FAIL_8197F << BIT_SHIFT_8051_MBIST_FAIL_8197F)
#define BIT_CLEAR_8051_MBIST_FAIL_8197F(x) ((x) & (~BITS_8051_MBIST_FAIL_8197F))
#define BIT_GET_8051_MBIST_FAIL_8197F(x)                                       \
	(((x) >> BIT_SHIFT_8051_MBIST_FAIL_8197F) &                            \
	 BIT_MASK_8051_MBIST_FAIL_8197F)
#define BIT_SET_8051_MBIST_FAIL_8197F(x, v)                                    \
	(BIT_CLEAR_8051_MBIST_FAIL_8197F(x) | BIT_8051_MBIST_FAIL_8197F(v))

#define BIT_SHIFT_USB_MBIST_FAIL_8197F 24
#define BIT_MASK_USB_MBIST_FAIL_8197F 0x3
#define BIT_USB_MBIST_FAIL_8197F(x)                                            \
	(((x) & BIT_MASK_USB_MBIST_FAIL_8197F)                                 \
	 << BIT_SHIFT_USB_MBIST_FAIL_8197F)
#define BITS_USB_MBIST_FAIL_8197F                                              \
	(BIT_MASK_USB_MBIST_FAIL_8197F << BIT_SHIFT_USB_MBIST_FAIL_8197F)
#define BIT_CLEAR_USB_MBIST_FAIL_8197F(x) ((x) & (~BITS_USB_MBIST_FAIL_8197F))
#define BIT_GET_USB_MBIST_FAIL_8197F(x)                                        \
	(((x) >> BIT_SHIFT_USB_MBIST_FAIL_8197F) &                             \
	 BIT_MASK_USB_MBIST_FAIL_8197F)
#define BIT_SET_USB_MBIST_FAIL_8197F(x, v)                                     \
	(BIT_CLEAR_USB_MBIST_FAIL_8197F(x) | BIT_USB_MBIST_FAIL_8197F(v))

#define BIT_SHIFT_PCIE_MBIST_FAIL_8197F 16
#define BIT_MASK_PCIE_MBIST_FAIL_8197F 0x3f
#define BIT_PCIE_MBIST_FAIL_8197F(x)                                           \
	(((x) & BIT_MASK_PCIE_MBIST_FAIL_8197F)                                \
	 << BIT_SHIFT_PCIE_MBIST_FAIL_8197F)
#define BITS_PCIE_MBIST_FAIL_8197F                                             \
	(BIT_MASK_PCIE_MBIST_FAIL_8197F << BIT_SHIFT_PCIE_MBIST_FAIL_8197F)
#define BIT_CLEAR_PCIE_MBIST_FAIL_8197F(x) ((x) & (~BITS_PCIE_MBIST_FAIL_8197F))
#define BIT_GET_PCIE_MBIST_FAIL_8197F(x)                                       \
	(((x) >> BIT_SHIFT_PCIE_MBIST_FAIL_8197F) &                            \
	 BIT_MASK_PCIE_MBIST_FAIL_8197F)
#define BIT_SET_PCIE_MBIST_FAIL_8197F(x, v)                                    \
	(BIT_CLEAR_PCIE_MBIST_FAIL_8197F(x) | BIT_PCIE_MBIST_FAIL_8197F(v))

#define BIT_SHIFT_MAC_MBIST_FAIL_DRF_8197F 0
#define BIT_MASK_MAC_MBIST_FAIL_DRF_8197F 0x3ffff
#define BIT_MAC_MBIST_FAIL_DRF_8197F(x)                                        \
	(((x) & BIT_MASK_MAC_MBIST_FAIL_DRF_8197F)                             \
	 << BIT_SHIFT_MAC_MBIST_FAIL_DRF_8197F)
#define BITS_MAC_MBIST_FAIL_DRF_8197F                                          \
	(BIT_MASK_MAC_MBIST_FAIL_DRF_8197F                                     \
	 << BIT_SHIFT_MAC_MBIST_FAIL_DRF_8197F)
#define BIT_CLEAR_MAC_MBIST_FAIL_DRF_8197F(x)                                  \
	((x) & (~BITS_MAC_MBIST_FAIL_DRF_8197F))
#define BIT_GET_MAC_MBIST_FAIL_DRF_8197F(x)                                    \
	(((x) >> BIT_SHIFT_MAC_MBIST_FAIL_DRF_8197F) &                         \
	 BIT_MASK_MAC_MBIST_FAIL_DRF_8197F)
#define BIT_SET_MAC_MBIST_FAIL_DRF_8197F(x, v)                                 \
	(BIT_CLEAR_MAC_MBIST_FAIL_DRF_8197F(x) |                               \
	 BIT_MAC_MBIST_FAIL_DRF_8197F(v))

/* 2 REG_MBIST_START_PAUSE_8197F */

#define BIT_SHIFT_8051_MBIST_START_PAUSE_8197F 26
#define BIT_MASK_8051_MBIST_START_PAUSE_8197F 0x7
#define BIT_8051_MBIST_START_PAUSE_8197F(x)                                    \
	(((x) & BIT_MASK_8051_MBIST_START_PAUSE_8197F)                         \
	 << BIT_SHIFT_8051_MBIST_START_PAUSE_8197F)
#define BITS_8051_MBIST_START_PAUSE_8197F                                      \
	(BIT_MASK_8051_MBIST_START_PAUSE_8197F                                 \
	 << BIT_SHIFT_8051_MBIST_START_PAUSE_8197F)
#define BIT_CLEAR_8051_MBIST_START_PAUSE_8197F(x)                              \
	((x) & (~BITS_8051_MBIST_START_PAUSE_8197F))
#define BIT_GET_8051_MBIST_START_PAUSE_8197F(x)                                \
	(((x) >> BIT_SHIFT_8051_MBIST_START_PAUSE_8197F) &                     \
	 BIT_MASK_8051_MBIST_START_PAUSE_8197F)
#define BIT_SET_8051_MBIST_START_PAUSE_8197F(x, v)                             \
	(BIT_CLEAR_8051_MBIST_START_PAUSE_8197F(x) |                           \
	 BIT_8051_MBIST_START_PAUSE_8197F(v))

#define BIT_SHIFT_USB_MBIST_START_PAUSE_8197F 24
#define BIT_MASK_USB_MBIST_START_PAUSE_8197F 0x3
#define BIT_USB_MBIST_START_PAUSE_8197F(x)                                     \
	(((x) & BIT_MASK_USB_MBIST_START_PAUSE_8197F)                          \
	 << BIT_SHIFT_USB_MBIST_START_PAUSE_8197F)
#define BITS_USB_MBIST_START_PAUSE_8197F                                       \
	(BIT_MASK_USB_MBIST_START_PAUSE_8197F                                  \
	 << BIT_SHIFT_USB_MBIST_START_PAUSE_8197F)
#define BIT_CLEAR_USB_MBIST_START_PAUSE_8197F(x)                               \
	((x) & (~BITS_USB_MBIST_START_PAUSE_8197F))
#define BIT_GET_USB_MBIST_START_PAUSE_8197F(x)                                 \
	(((x) >> BIT_SHIFT_USB_MBIST_START_PAUSE_8197F) &                      \
	 BIT_MASK_USB_MBIST_START_PAUSE_8197F)
#define BIT_SET_USB_MBIST_START_PAUSE_8197F(x, v)                              \
	(BIT_CLEAR_USB_MBIST_START_PAUSE_8197F(x) |                            \
	 BIT_USB_MBIST_START_PAUSE_8197F(v))

#define BIT_SHIFT_PCIE_MBIST_START_PAUSE_8197F 16
#define BIT_MASK_PCIE_MBIST_START_PAUSE_8197F 0x3f
#define BIT_PCIE_MBIST_START_PAUSE_8197F(x)                                    \
	(((x) & BIT_MASK_PCIE_MBIST_START_PAUSE_8197F)                         \
	 << BIT_SHIFT_PCIE_MBIST_START_PAUSE_8197F)
#define BITS_PCIE_MBIST_START_PAUSE_8197F                                      \
	(BIT_MASK_PCIE_MBIST_START_PAUSE_8197F                                 \
	 << BIT_SHIFT_PCIE_MBIST_START_PAUSE_8197F)
#define BIT_CLEAR_PCIE_MBIST_START_PAUSE_8197F(x)                              \
	((x) & (~BITS_PCIE_MBIST_START_PAUSE_8197F))
#define BIT_GET_PCIE_MBIST_START_PAUSE_8197F(x)                                \
	(((x) >> BIT_SHIFT_PCIE_MBIST_START_PAUSE_8197F) &                     \
	 BIT_MASK_PCIE_MBIST_START_PAUSE_8197F)
#define BIT_SET_PCIE_MBIST_START_PAUSE_8197F(x, v)                             \
	(BIT_CLEAR_PCIE_MBIST_START_PAUSE_8197F(x) |                           \
	 BIT_PCIE_MBIST_START_PAUSE_8197F(v))

#define BIT_SHIFT_MAC_MBIST_START_PAUSE_V1_8197F 0
#define BIT_MASK_MAC_MBIST_START_PAUSE_V1_8197F 0x3ffff
#define BIT_MAC_MBIST_START_PAUSE_V1_8197F(x)                                  \
	(((x) & BIT_MASK_MAC_MBIST_START_PAUSE_V1_8197F)                       \
	 << BIT_SHIFT_MAC_MBIST_START_PAUSE_V1_8197F)
#define BITS_MAC_MBIST_START_PAUSE_V1_8197F                                    \
	(BIT_MASK_MAC_MBIST_START_PAUSE_V1_8197F                               \
	 << BIT_SHIFT_MAC_MBIST_START_PAUSE_V1_8197F)
#define BIT_CLEAR_MAC_MBIST_START_PAUSE_V1_8197F(x)                            \
	((x) & (~BITS_MAC_MBIST_START_PAUSE_V1_8197F))
#define BIT_GET_MAC_MBIST_START_PAUSE_V1_8197F(x)                              \
	(((x) >> BIT_SHIFT_MAC_MBIST_START_PAUSE_V1_8197F) &                   \
	 BIT_MASK_MAC_MBIST_START_PAUSE_V1_8197F)
#define BIT_SET_MAC_MBIST_START_PAUSE_V1_8197F(x, v)                           \
	(BIT_CLEAR_MAC_MBIST_START_PAUSE_V1_8197F(x) |                         \
	 BIT_MAC_MBIST_START_PAUSE_V1_8197F(v))

/* 2 REG_MBIST_DONE_8197F */

#define BIT_SHIFT_8051_MBIST_DONE_8197F 26
#define BIT_MASK_8051_MBIST_DONE_8197F 0x7
#define BIT_8051_MBIST_DONE_8197F(x)                                           \
	(((x) & BIT_MASK_8051_MBIST_DONE_8197F)                                \
	 << BIT_SHIFT_8051_MBIST_DONE_8197F)
#define BITS_8051_MBIST_DONE_8197F                                             \
	(BIT_MASK_8051_MBIST_DONE_8197F << BIT_SHIFT_8051_MBIST_DONE_8197F)
#define BIT_CLEAR_8051_MBIST_DONE_8197F(x) ((x) & (~BITS_8051_MBIST_DONE_8197F))
#define BIT_GET_8051_MBIST_DONE_8197F(x)                                       \
	(((x) >> BIT_SHIFT_8051_MBIST_DONE_8197F) &                            \
	 BIT_MASK_8051_MBIST_DONE_8197F)
#define BIT_SET_8051_MBIST_DONE_8197F(x, v)                                    \
	(BIT_CLEAR_8051_MBIST_DONE_8197F(x) | BIT_8051_MBIST_DONE_8197F(v))

#define BIT_SHIFT_USB_MBIST_DONE_8197F 24
#define BIT_MASK_USB_MBIST_DONE_8197F 0x3
#define BIT_USB_MBIST_DONE_8197F(x)                                            \
	(((x) & BIT_MASK_USB_MBIST_DONE_8197F)                                 \
	 << BIT_SHIFT_USB_MBIST_DONE_8197F)
#define BITS_USB_MBIST_DONE_8197F                                              \
	(BIT_MASK_USB_MBIST_DONE_8197F << BIT_SHIFT_USB_MBIST_DONE_8197F)
#define BIT_CLEAR_USB_MBIST_DONE_8197F(x) ((x) & (~BITS_USB_MBIST_DONE_8197F))
#define BIT_GET_USB_MBIST_DONE_8197F(x)                                        \
	(((x) >> BIT_SHIFT_USB_MBIST_DONE_8197F) &                             \
	 BIT_MASK_USB_MBIST_DONE_8197F)
#define BIT_SET_USB_MBIST_DONE_8197F(x, v)                                     \
	(BIT_CLEAR_USB_MBIST_DONE_8197F(x) | BIT_USB_MBIST_DONE_8197F(v))

#define BIT_SHIFT_PCIE_MBIST_DONE_8197F 16
#define BIT_MASK_PCIE_MBIST_DONE_8197F 0x3f
#define BIT_PCIE_MBIST_DONE_8197F(x)                                           \
	(((x) & BIT_MASK_PCIE_MBIST_DONE_8197F)                                \
	 << BIT_SHIFT_PCIE_MBIST_DONE_8197F)
#define BITS_PCIE_MBIST_DONE_8197F                                             \
	(BIT_MASK_PCIE_MBIST_DONE_8197F << BIT_SHIFT_PCIE_MBIST_DONE_8197F)
#define BIT_CLEAR_PCIE_MBIST_DONE_8197F(x) ((x) & (~BITS_PCIE_MBIST_DONE_8197F))
#define BIT_GET_PCIE_MBIST_DONE_8197F(x)                                       \
	(((x) >> BIT_SHIFT_PCIE_MBIST_DONE_8197F) &                            \
	 BIT_MASK_PCIE_MBIST_DONE_8197F)
#define BIT_SET_PCIE_MBIST_DONE_8197F(x, v)                                    \
	(BIT_CLEAR_PCIE_MBIST_DONE_8197F(x) | BIT_PCIE_MBIST_DONE_8197F(v))

#define BIT_SHIFT_MAC_MBIST_DONE_V1_8197F 0
#define BIT_MASK_MAC_MBIST_DONE_V1_8197F 0x3ffff
#define BIT_MAC_MBIST_DONE_V1_8197F(x)                                         \
	(((x) & BIT_MASK_MAC_MBIST_DONE_V1_8197F)                              \
	 << BIT_SHIFT_MAC_MBIST_DONE_V1_8197F)
#define BITS_MAC_MBIST_DONE_V1_8197F                                           \
	(BIT_MASK_MAC_MBIST_DONE_V1_8197F << BIT_SHIFT_MAC_MBIST_DONE_V1_8197F)
#define BIT_CLEAR_MAC_MBIST_DONE_V1_8197F(x)                                   \
	((x) & (~BITS_MAC_MBIST_DONE_V1_8197F))
#define BIT_GET_MAC_MBIST_DONE_V1_8197F(x)                                     \
	(((x) >> BIT_SHIFT_MAC_MBIST_DONE_V1_8197F) &                          \
	 BIT_MASK_MAC_MBIST_DONE_V1_8197F)
#define BIT_SET_MAC_MBIST_DONE_V1_8197F(x, v)                                  \
	(BIT_CLEAR_MAC_MBIST_DONE_V1_8197F(x) | BIT_MAC_MBIST_DONE_V1_8197F(v))

/* 2 REG_MBIST_FAIL_NRML_8197F */

#define BIT_SHIFT_MBIST_FAIL_NRML_V1_8197F 0
#define BIT_MASK_MBIST_FAIL_NRML_V1_8197F 0x3ffff
#define BIT_MBIST_FAIL_NRML_V1_8197F(x)                                        \
	(((x) & BIT_MASK_MBIST_FAIL_NRML_V1_8197F)                             \
	 << BIT_SHIFT_MBIST_FAIL_NRML_V1_8197F)
#define BITS_MBIST_FAIL_NRML_V1_8197F                                          \
	(BIT_MASK_MBIST_FAIL_NRML_V1_8197F                                     \
	 << BIT_SHIFT_MBIST_FAIL_NRML_V1_8197F)
#define BIT_CLEAR_MBIST_FAIL_NRML_V1_8197F(x)                                  \
	((x) & (~BITS_MBIST_FAIL_NRML_V1_8197F))
#define BIT_GET_MBIST_FAIL_NRML_V1_8197F(x)                                    \
	(((x) >> BIT_SHIFT_MBIST_FAIL_NRML_V1_8197F) &                         \
	 BIT_MASK_MBIST_FAIL_NRML_V1_8197F)
#define BIT_SET_MBIST_FAIL_NRML_V1_8197F(x, v)                                 \
	(BIT_CLEAR_MBIST_FAIL_NRML_V1_8197F(x) |                               \
	 BIT_MBIST_FAIL_NRML_V1_8197F(v))

/* 2 REG_AES_DECRPT_DATA_8197F */

#define BIT_SHIFT_IPS_CFG_ADDR_8197F 0
#define BIT_MASK_IPS_CFG_ADDR_8197F 0xff
#define BIT_IPS_CFG_ADDR_8197F(x)                                              \
	(((x) & BIT_MASK_IPS_CFG_ADDR_8197F) << BIT_SHIFT_IPS_CFG_ADDR_8197F)
#define BITS_IPS_CFG_ADDR_8197F                                                \
	(BIT_MASK_IPS_CFG_ADDR_8197F << BIT_SHIFT_IPS_CFG_ADDR_8197F)
#define BIT_CLEAR_IPS_CFG_ADDR_8197F(x) ((x) & (~BITS_IPS_CFG_ADDR_8197F))
#define BIT_GET_IPS_CFG_ADDR_8197F(x)                                          \
	(((x) >> BIT_SHIFT_IPS_CFG_ADDR_8197F) & BIT_MASK_IPS_CFG_ADDR_8197F)
#define BIT_SET_IPS_CFG_ADDR_8197F(x, v)                                       \
	(BIT_CLEAR_IPS_CFG_ADDR_8197F(x) | BIT_IPS_CFG_ADDR_8197F(v))

/* 2 REG_AES_DECRPT_CFG_8197F */

#define BIT_SHIFT_IPS_CFG_DATA_8197F 0
#define BIT_MASK_IPS_CFG_DATA_8197F 0xffffffffL
#define BIT_IPS_CFG_DATA_8197F(x)                                              \
	(((x) & BIT_MASK_IPS_CFG_DATA_8197F) << BIT_SHIFT_IPS_CFG_DATA_8197F)
#define BITS_IPS_CFG_DATA_8197F                                                \
	(BIT_MASK_IPS_CFG_DATA_8197F << BIT_SHIFT_IPS_CFG_DATA_8197F)
#define BIT_CLEAR_IPS_CFG_DATA_8197F(x) ((x) & (~BITS_IPS_CFG_DATA_8197F))
#define BIT_GET_IPS_CFG_DATA_8197F(x)                                          \
	(((x) >> BIT_SHIFT_IPS_CFG_DATA_8197F) & BIT_MASK_IPS_CFG_DATA_8197F)
#define BIT_SET_IPS_CFG_DATA_8197F(x, v)                                       \
	(BIT_CLEAR_IPS_CFG_DATA_8197F(x) | BIT_IPS_CFG_DATA_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_MACCLKFRQ_8197F */

#define BIT_SHIFT_MACCLK_FREQ_LOW32_8197F 0
#define BIT_MASK_MACCLK_FREQ_LOW32_8197F 0xffffffffL
#define BIT_MACCLK_FREQ_LOW32_8197F(x)                                         \
	(((x) & BIT_MASK_MACCLK_FREQ_LOW32_8197F)                              \
	 << BIT_SHIFT_MACCLK_FREQ_LOW32_8197F)
#define BITS_MACCLK_FREQ_LOW32_8197F                                           \
	(BIT_MASK_MACCLK_FREQ_LOW32_8197F << BIT_SHIFT_MACCLK_FREQ_LOW32_8197F)
#define BIT_CLEAR_MACCLK_FREQ_LOW32_8197F(x)                                   \
	((x) & (~BITS_MACCLK_FREQ_LOW32_8197F))
#define BIT_GET_MACCLK_FREQ_LOW32_8197F(x)                                     \
	(((x) >> BIT_SHIFT_MACCLK_FREQ_LOW32_8197F) &                          \
	 BIT_MASK_MACCLK_FREQ_LOW32_8197F)
#define BIT_SET_MACCLK_FREQ_LOW32_8197F(x, v)                                  \
	(BIT_CLEAR_MACCLK_FREQ_LOW32_8197F(x) | BIT_MACCLK_FREQ_LOW32_8197F(v))

/* 2 REG_TMETER_8197F */

#define BIT_SHIFT_MACCLK_FREQ_HIGH10_8197F 0
#define BIT_MASK_MACCLK_FREQ_HIGH10_8197F 0x3ff
#define BIT_MACCLK_FREQ_HIGH10_8197F(x)                                        \
	(((x) & BIT_MASK_MACCLK_FREQ_HIGH10_8197F)                             \
	 << BIT_SHIFT_MACCLK_FREQ_HIGH10_8197F)
#define BITS_MACCLK_FREQ_HIGH10_8197F                                          \
	(BIT_MASK_MACCLK_FREQ_HIGH10_8197F                                     \
	 << BIT_SHIFT_MACCLK_FREQ_HIGH10_8197F)
#define BIT_CLEAR_MACCLK_FREQ_HIGH10_8197F(x)                                  \
	((x) & (~BITS_MACCLK_FREQ_HIGH10_8197F))
#define BIT_GET_MACCLK_FREQ_HIGH10_8197F(x)                                    \
	(((x) >> BIT_SHIFT_MACCLK_FREQ_HIGH10_8197F) &                         \
	 BIT_MASK_MACCLK_FREQ_HIGH10_8197F)
#define BIT_SET_MACCLK_FREQ_HIGH10_8197F(x, v)                                 \
	(BIT_CLEAR_MACCLK_FREQ_HIGH10_8197F(x) |                               \
	 BIT_MACCLK_FREQ_HIGH10_8197F(v))

/* 2 REG_OSC_32K_CTRL_8197F */
#define BIT_32K_CLK_OUT_RDY_8197F BIT(12)

#define BIT_SHIFT_MONITOR_CYCLE_LOG2_8197F 8
#define BIT_MASK_MONITOR_CYCLE_LOG2_8197F 0xf
#define BIT_MONITOR_CYCLE_LOG2_8197F(x)                                        \
	(((x) & BIT_MASK_MONITOR_CYCLE_LOG2_8197F)                             \
	 << BIT_SHIFT_MONITOR_CYCLE_LOG2_8197F)
#define BITS_MONITOR_CYCLE_LOG2_8197F                                          \
	(BIT_MASK_MONITOR_CYCLE_LOG2_8197F                                     \
	 << BIT_SHIFT_MONITOR_CYCLE_LOG2_8197F)
#define BIT_CLEAR_MONITOR_CYCLE_LOG2_8197F(x)                                  \
	((x) & (~BITS_MONITOR_CYCLE_LOG2_8197F))
#define BIT_GET_MONITOR_CYCLE_LOG2_8197F(x)                                    \
	(((x) >> BIT_SHIFT_MONITOR_CYCLE_LOG2_8197F) &                         \
	 BIT_MASK_MONITOR_CYCLE_LOG2_8197F)
#define BIT_SET_MONITOR_CYCLE_LOG2_8197F(x, v)                                 \
	(BIT_CLEAR_MONITOR_CYCLE_LOG2_8197F(x) |                               \
	 BIT_MONITOR_CYCLE_LOG2_8197F(v))

/* 2 REG_32K_CAL_REG1_8197F */

#define BIT_SHIFT_FREQVALUE_UNREGCLK_8197F 8
#define BIT_MASK_FREQVALUE_UNREGCLK_8197F 0xffffff
#define BIT_FREQVALUE_UNREGCLK_8197F(x)                                        \
	(((x) & BIT_MASK_FREQVALUE_UNREGCLK_8197F)                             \
	 << BIT_SHIFT_FREQVALUE_UNREGCLK_8197F)
#define BITS_FREQVALUE_UNREGCLK_8197F                                          \
	(BIT_MASK_FREQVALUE_UNREGCLK_8197F                                     \
	 << BIT_SHIFT_FREQVALUE_UNREGCLK_8197F)
#define BIT_CLEAR_FREQVALUE_UNREGCLK_8197F(x)                                  \
	((x) & (~BITS_FREQVALUE_UNREGCLK_8197F))
#define BIT_GET_FREQVALUE_UNREGCLK_8197F(x)                                    \
	(((x) >> BIT_SHIFT_FREQVALUE_UNREGCLK_8197F) &                         \
	 BIT_MASK_FREQVALUE_UNREGCLK_8197F)
#define BIT_SET_FREQVALUE_UNREGCLK_8197F(x, v)                                 \
	(BIT_CLEAR_FREQVALUE_UNREGCLK_8197F(x) |                               \
	 BIT_FREQVALUE_UNREGCLK_8197F(v))

#define BIT_CAL32K_DBGMOD_8197F BIT(7)

#define BIT_SHIFT_NCO_THRS_8197F 0
#define BIT_MASK_NCO_THRS_8197F 0x7f
#define BIT_NCO_THRS_8197F(x)                                                  \
	(((x) & BIT_MASK_NCO_THRS_8197F) << BIT_SHIFT_NCO_THRS_8197F)
#define BITS_NCO_THRS_8197F                                                    \
	(BIT_MASK_NCO_THRS_8197F << BIT_SHIFT_NCO_THRS_8197F)
#define BIT_CLEAR_NCO_THRS_8197F(x) ((x) & (~BITS_NCO_THRS_8197F))
#define BIT_GET_NCO_THRS_8197F(x)                                              \
	(((x) >> BIT_SHIFT_NCO_THRS_8197F) & BIT_MASK_NCO_THRS_8197F)
#define BIT_SET_NCO_THRS_8197F(x, v)                                           \
	(BIT_CLEAR_NCO_THRS_8197F(x) | BIT_NCO_THRS_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_C2HEVT_8197F */

#define BIT_SHIFT_C2HEVT_MSG_8197F 0
#define BIT_MASK_C2HEVT_MSG_8197F 0xffffffffffffffffffffffffffffffffL
#define BIT_C2HEVT_MSG_8197F(x)                                                \
	(((x) & BIT_MASK_C2HEVT_MSG_8197F) << BIT_SHIFT_C2HEVT_MSG_8197F)
#define BITS_C2HEVT_MSG_8197F                                                  \
	(BIT_MASK_C2HEVT_MSG_8197F << BIT_SHIFT_C2HEVT_MSG_8197F)
#define BIT_CLEAR_C2HEVT_MSG_8197F(x) ((x) & (~BITS_C2HEVT_MSG_8197F))
#define BIT_GET_C2HEVT_MSG_8197F(x)                                            \
	(((x) >> BIT_SHIFT_C2HEVT_MSG_8197F) & BIT_MASK_C2HEVT_MSG_8197F)
#define BIT_SET_C2HEVT_MSG_8197F(x, v)                                         \
	(BIT_CLEAR_C2HEVT_MSG_8197F(x) | BIT_C2HEVT_MSG_8197F(v))

/* 2 REG_SW_DEFINED_PAGE1_8197F */

#define BIT_SHIFT_SW_DEFINED_PAGE1_8197F 0
#define BIT_MASK_SW_DEFINED_PAGE1_8197F 0xffffffffffffffffL
#define BIT_SW_DEFINED_PAGE1_8197F(x)                                          \
	(((x) & BIT_MASK_SW_DEFINED_PAGE1_8197F)                               \
	 << BIT_SHIFT_SW_DEFINED_PAGE1_8197F)
#define BITS_SW_DEFINED_PAGE1_8197F                                            \
	(BIT_MASK_SW_DEFINED_PAGE1_8197F << BIT_SHIFT_SW_DEFINED_PAGE1_8197F)
#define BIT_CLEAR_SW_DEFINED_PAGE1_8197F(x)                                    \
	((x) & (~BITS_SW_DEFINED_PAGE1_8197F))
#define BIT_GET_SW_DEFINED_PAGE1_8197F(x)                                      \
	(((x) >> BIT_SHIFT_SW_DEFINED_PAGE1_8197F) &                           \
	 BIT_MASK_SW_DEFINED_PAGE1_8197F)
#define BIT_SET_SW_DEFINED_PAGE1_8197F(x, v)                                   \
	(BIT_CLEAR_SW_DEFINED_PAGE1_8197F(x) | BIT_SW_DEFINED_PAGE1_8197F(v))

/* 2 REG_MCUTST_I_8197F */

#define BIT_SHIFT_MCUDMSG_I_8197F 0
#define BIT_MASK_MCUDMSG_I_8197F 0xffffffffL
#define BIT_MCUDMSG_I_8197F(x)                                                 \
	(((x) & BIT_MASK_MCUDMSG_I_8197F) << BIT_SHIFT_MCUDMSG_I_8197F)
#define BITS_MCUDMSG_I_8197F                                                   \
	(BIT_MASK_MCUDMSG_I_8197F << BIT_SHIFT_MCUDMSG_I_8197F)
#define BIT_CLEAR_MCUDMSG_I_8197F(x) ((x) & (~BITS_MCUDMSG_I_8197F))
#define BIT_GET_MCUDMSG_I_8197F(x)                                             \
	(((x) >> BIT_SHIFT_MCUDMSG_I_8197F) & BIT_MASK_MCUDMSG_I_8197F)
#define BIT_SET_MCUDMSG_I_8197F(x, v)                                          \
	(BIT_CLEAR_MCUDMSG_I_8197F(x) | BIT_MCUDMSG_I_8197F(v))

/* 2 REG_MCUTST_II_8197F */

#define BIT_SHIFT_MCUDMSG_II_8197F 0
#define BIT_MASK_MCUDMSG_II_8197F 0xffffffffL
#define BIT_MCUDMSG_II_8197F(x)                                                \
	(((x) & BIT_MASK_MCUDMSG_II_8197F) << BIT_SHIFT_MCUDMSG_II_8197F)
#define BITS_MCUDMSG_II_8197F                                                  \
	(BIT_MASK_MCUDMSG_II_8197F << BIT_SHIFT_MCUDMSG_II_8197F)
#define BIT_CLEAR_MCUDMSG_II_8197F(x) ((x) & (~BITS_MCUDMSG_II_8197F))
#define BIT_GET_MCUDMSG_II_8197F(x)                                            \
	(((x) >> BIT_SHIFT_MCUDMSG_II_8197F) & BIT_MASK_MCUDMSG_II_8197F)
#define BIT_SET_MCUDMSG_II_8197F(x, v)                                         \
	(BIT_CLEAR_MCUDMSG_II_8197F(x) | BIT_MCUDMSG_II_8197F(v))

/* 2 REG_FMETHR_8197F */
#define BIT_FMSG_INT_8197F BIT(31)

#define BIT_SHIFT_FW_MSG_8197F 0
#define BIT_MASK_FW_MSG_8197F 0xffffffffL
#define BIT_FW_MSG_8197F(x)                                                    \
	(((x) & BIT_MASK_FW_MSG_8197F) << BIT_SHIFT_FW_MSG_8197F)
#define BITS_FW_MSG_8197F (BIT_MASK_FW_MSG_8197F << BIT_SHIFT_FW_MSG_8197F)
#define BIT_CLEAR_FW_MSG_8197F(x) ((x) & (~BITS_FW_MSG_8197F))
#define BIT_GET_FW_MSG_8197F(x)                                                \
	(((x) >> BIT_SHIFT_FW_MSG_8197F) & BIT_MASK_FW_MSG_8197F)
#define BIT_SET_FW_MSG_8197F(x, v)                                             \
	(BIT_CLEAR_FW_MSG_8197F(x) | BIT_FW_MSG_8197F(v))

/* 2 REG_HMETFR_8197F */

#define BIT_SHIFT_HRCV_MSG_8197F 24
#define BIT_MASK_HRCV_MSG_8197F 0xff
#define BIT_HRCV_MSG_8197F(x)                                                  \
	(((x) & BIT_MASK_HRCV_MSG_8197F) << BIT_SHIFT_HRCV_MSG_8197F)
#define BITS_HRCV_MSG_8197F                                                    \
	(BIT_MASK_HRCV_MSG_8197F << BIT_SHIFT_HRCV_MSG_8197F)
#define BIT_CLEAR_HRCV_MSG_8197F(x) ((x) & (~BITS_HRCV_MSG_8197F))
#define BIT_GET_HRCV_MSG_8197F(x)                                              \
	(((x) >> BIT_SHIFT_HRCV_MSG_8197F) & BIT_MASK_HRCV_MSG_8197F)
#define BIT_SET_HRCV_MSG_8197F(x, v)                                           \
	(BIT_CLEAR_HRCV_MSG_8197F(x) | BIT_HRCV_MSG_8197F(v))

#define BIT_INT_BOX3_8197F BIT(3)
#define BIT_INT_BOX2_8197F BIT(2)
#define BIT_INT_BOX1_8197F BIT(1)
#define BIT_INT_BOX0_8197F BIT(0)

/* 2 REG_HMEBOX0_8197F */

#define BIT_SHIFT_HOST_MSG_0_8197F 0
#define BIT_MASK_HOST_MSG_0_8197F 0xffffffffL
#define BIT_HOST_MSG_0_8197F(x)                                                \
	(((x) & BIT_MASK_HOST_MSG_0_8197F) << BIT_SHIFT_HOST_MSG_0_8197F)
#define BITS_HOST_MSG_0_8197F                                                  \
	(BIT_MASK_HOST_MSG_0_8197F << BIT_SHIFT_HOST_MSG_0_8197F)
#define BIT_CLEAR_HOST_MSG_0_8197F(x) ((x) & (~BITS_HOST_MSG_0_8197F))
#define BIT_GET_HOST_MSG_0_8197F(x)                                            \
	(((x) >> BIT_SHIFT_HOST_MSG_0_8197F) & BIT_MASK_HOST_MSG_0_8197F)
#define BIT_SET_HOST_MSG_0_8197F(x, v)                                         \
	(BIT_CLEAR_HOST_MSG_0_8197F(x) | BIT_HOST_MSG_0_8197F(v))

/* 2 REG_HMEBOX1_8197F */

#define BIT_SHIFT_HOST_MSG_1_8197F 0
#define BIT_MASK_HOST_MSG_1_8197F 0xffffffffL
#define BIT_HOST_MSG_1_8197F(x)                                                \
	(((x) & BIT_MASK_HOST_MSG_1_8197F) << BIT_SHIFT_HOST_MSG_1_8197F)
#define BITS_HOST_MSG_1_8197F                                                  \
	(BIT_MASK_HOST_MSG_1_8197F << BIT_SHIFT_HOST_MSG_1_8197F)
#define BIT_CLEAR_HOST_MSG_1_8197F(x) ((x) & (~BITS_HOST_MSG_1_8197F))
#define BIT_GET_HOST_MSG_1_8197F(x)                                            \
	(((x) >> BIT_SHIFT_HOST_MSG_1_8197F) & BIT_MASK_HOST_MSG_1_8197F)
#define BIT_SET_HOST_MSG_1_8197F(x, v)                                         \
	(BIT_CLEAR_HOST_MSG_1_8197F(x) | BIT_HOST_MSG_1_8197F(v))

/* 2 REG_HMEBOX2_8197F */

#define BIT_SHIFT_HOST_MSG_2_8197F 0
#define BIT_MASK_HOST_MSG_2_8197F 0xffffffffL
#define BIT_HOST_MSG_2_8197F(x)                                                \
	(((x) & BIT_MASK_HOST_MSG_2_8197F) << BIT_SHIFT_HOST_MSG_2_8197F)
#define BITS_HOST_MSG_2_8197F                                                  \
	(BIT_MASK_HOST_MSG_2_8197F << BIT_SHIFT_HOST_MSG_2_8197F)
#define BIT_CLEAR_HOST_MSG_2_8197F(x) ((x) & (~BITS_HOST_MSG_2_8197F))
#define BIT_GET_HOST_MSG_2_8197F(x)                                            \
	(((x) >> BIT_SHIFT_HOST_MSG_2_8197F) & BIT_MASK_HOST_MSG_2_8197F)
#define BIT_SET_HOST_MSG_2_8197F(x, v)                                         \
	(BIT_CLEAR_HOST_MSG_2_8197F(x) | BIT_HOST_MSG_2_8197F(v))

/* 2 REG_HMEBOX3_8197F */

#define BIT_SHIFT_HOST_MSG_3_8197F 0
#define BIT_MASK_HOST_MSG_3_8197F 0xffffffffL
#define BIT_HOST_MSG_3_8197F(x)                                                \
	(((x) & BIT_MASK_HOST_MSG_3_8197F) << BIT_SHIFT_HOST_MSG_3_8197F)
#define BITS_HOST_MSG_3_8197F                                                  \
	(BIT_MASK_HOST_MSG_3_8197F << BIT_SHIFT_HOST_MSG_3_8197F)
#define BIT_CLEAR_HOST_MSG_3_8197F(x) ((x) & (~BITS_HOST_MSG_3_8197F))
#define BIT_GET_HOST_MSG_3_8197F(x)                                            \
	(((x) >> BIT_SHIFT_HOST_MSG_3_8197F) & BIT_MASK_HOST_MSG_3_8197F)
#define BIT_SET_HOST_MSG_3_8197F(x, v)                                         \
	(BIT_CLEAR_HOST_MSG_3_8197F(x) | BIT_HOST_MSG_3_8197F(v))

/* 2 REG_LLT_INIT_8197F */

#define BIT_SHIFT_LLTE_RWM_8197F 30
#define BIT_MASK_LLTE_RWM_8197F 0x3
#define BIT_LLTE_RWM_8197F(x)                                                  \
	(((x) & BIT_MASK_LLTE_RWM_8197F) << BIT_SHIFT_LLTE_RWM_8197F)
#define BITS_LLTE_RWM_8197F                                                    \
	(BIT_MASK_LLTE_RWM_8197F << BIT_SHIFT_LLTE_RWM_8197F)
#define BIT_CLEAR_LLTE_RWM_8197F(x) ((x) & (~BITS_LLTE_RWM_8197F))
#define BIT_GET_LLTE_RWM_8197F(x)                                              \
	(((x) >> BIT_SHIFT_LLTE_RWM_8197F) & BIT_MASK_LLTE_RWM_8197F)
#define BIT_SET_LLTE_RWM_8197F(x, v)                                           \
	(BIT_CLEAR_LLTE_RWM_8197F(x) | BIT_LLTE_RWM_8197F(v))

#define BIT_SHIFT_LLTINI_PDATA_V1_8197F 16
#define BIT_MASK_LLTINI_PDATA_V1_8197F 0xfff
#define BIT_LLTINI_PDATA_V1_8197F(x)                                           \
	(((x) & BIT_MASK_LLTINI_PDATA_V1_8197F)                                \
	 << BIT_SHIFT_LLTINI_PDATA_V1_8197F)
#define BITS_LLTINI_PDATA_V1_8197F                                             \
	(BIT_MASK_LLTINI_PDATA_V1_8197F << BIT_SHIFT_LLTINI_PDATA_V1_8197F)
#define BIT_CLEAR_LLTINI_PDATA_V1_8197F(x) ((x) & (~BITS_LLTINI_PDATA_V1_8197F))
#define BIT_GET_LLTINI_PDATA_V1_8197F(x)                                       \
	(((x) >> BIT_SHIFT_LLTINI_PDATA_V1_8197F) &                            \
	 BIT_MASK_LLTINI_PDATA_V1_8197F)
#define BIT_SET_LLTINI_PDATA_V1_8197F(x, v)                                    \
	(BIT_CLEAR_LLTINI_PDATA_V1_8197F(x) | BIT_LLTINI_PDATA_V1_8197F(v))

#define BIT_SHIFT_LLTINI_HDATA_V1_8197F 0
#define BIT_MASK_LLTINI_HDATA_V1_8197F 0xfff
#define BIT_LLTINI_HDATA_V1_8197F(x)                                           \
	(((x) & BIT_MASK_LLTINI_HDATA_V1_8197F)                                \
	 << BIT_SHIFT_LLTINI_HDATA_V1_8197F)
#define BITS_LLTINI_HDATA_V1_8197F                                             \
	(BIT_MASK_LLTINI_HDATA_V1_8197F << BIT_SHIFT_LLTINI_HDATA_V1_8197F)
#define BIT_CLEAR_LLTINI_HDATA_V1_8197F(x) ((x) & (~BITS_LLTINI_HDATA_V1_8197F))
#define BIT_GET_LLTINI_HDATA_V1_8197F(x)                                       \
	(((x) >> BIT_SHIFT_LLTINI_HDATA_V1_8197F) &                            \
	 BIT_MASK_LLTINI_HDATA_V1_8197F)
#define BIT_SET_LLTINI_HDATA_V1_8197F(x, v)                                    \
	(BIT_CLEAR_LLTINI_HDATA_V1_8197F(x) | BIT_LLTINI_HDATA_V1_8197F(v))

/* 2 REG_LLT_INIT_ADDR_8197F */

#define BIT_SHIFT_LLTINI_ADDR_V1_8197F 0
#define BIT_MASK_LLTINI_ADDR_V1_8197F 0xfff
#define BIT_LLTINI_ADDR_V1_8197F(x)                                            \
	(((x) & BIT_MASK_LLTINI_ADDR_V1_8197F)                                 \
	 << BIT_SHIFT_LLTINI_ADDR_V1_8197F)
#define BITS_LLTINI_ADDR_V1_8197F                                              \
	(BIT_MASK_LLTINI_ADDR_V1_8197F << BIT_SHIFT_LLTINI_ADDR_V1_8197F)
#define BIT_CLEAR_LLTINI_ADDR_V1_8197F(x) ((x) & (~BITS_LLTINI_ADDR_V1_8197F))
#define BIT_GET_LLTINI_ADDR_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_LLTINI_ADDR_V1_8197F) &                             \
	 BIT_MASK_LLTINI_ADDR_V1_8197F)
#define BIT_SET_LLTINI_ADDR_V1_8197F(x, v)                                     \
	(BIT_CLEAR_LLTINI_ADDR_V1_8197F(x) | BIT_LLTINI_ADDR_V1_8197F(v))

/* 2 REG_BB_ACCESS_CTRL_8197F */

#define BIT_SHIFT_BB_WRITE_READ_8197F 30
#define BIT_MASK_BB_WRITE_READ_8197F 0x3
#define BIT_BB_WRITE_READ_8197F(x)                                             \
	(((x) & BIT_MASK_BB_WRITE_READ_8197F) << BIT_SHIFT_BB_WRITE_READ_8197F)
#define BITS_BB_WRITE_READ_8197F                                               \
	(BIT_MASK_BB_WRITE_READ_8197F << BIT_SHIFT_BB_WRITE_READ_8197F)
#define BIT_CLEAR_BB_WRITE_READ_8197F(x) ((x) & (~BITS_BB_WRITE_READ_8197F))
#define BIT_GET_BB_WRITE_READ_8197F(x)                                         \
	(((x) >> BIT_SHIFT_BB_WRITE_READ_8197F) & BIT_MASK_BB_WRITE_READ_8197F)
#define BIT_SET_BB_WRITE_READ_8197F(x, v)                                      \
	(BIT_CLEAR_BB_WRITE_READ_8197F(x) | BIT_BB_WRITE_READ_8197F(v))

#define BIT_SHIFT_BB_WRITE_EN_V1_8197F 16
#define BIT_MASK_BB_WRITE_EN_V1_8197F 0xf
#define BIT_BB_WRITE_EN_V1_8197F(x)                                            \
	(((x) & BIT_MASK_BB_WRITE_EN_V1_8197F)                                 \
	 << BIT_SHIFT_BB_WRITE_EN_V1_8197F)
#define BITS_BB_WRITE_EN_V1_8197F                                              \
	(BIT_MASK_BB_WRITE_EN_V1_8197F << BIT_SHIFT_BB_WRITE_EN_V1_8197F)
#define BIT_CLEAR_BB_WRITE_EN_V1_8197F(x) ((x) & (~BITS_BB_WRITE_EN_V1_8197F))
#define BIT_GET_BB_WRITE_EN_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_BB_WRITE_EN_V1_8197F) &                             \
	 BIT_MASK_BB_WRITE_EN_V1_8197F)
#define BIT_SET_BB_WRITE_EN_V1_8197F(x, v)                                     \
	(BIT_CLEAR_BB_WRITE_EN_V1_8197F(x) | BIT_BB_WRITE_EN_V1_8197F(v))

#define BIT_SHIFT_BB_ADDR_V1_8197F 2
#define BIT_MASK_BB_ADDR_V1_8197F 0xfff
#define BIT_BB_ADDR_V1_8197F(x)                                                \
	(((x) & BIT_MASK_BB_ADDR_V1_8197F) << BIT_SHIFT_BB_ADDR_V1_8197F)
#define BITS_BB_ADDR_V1_8197F                                                  \
	(BIT_MASK_BB_ADDR_V1_8197F << BIT_SHIFT_BB_ADDR_V1_8197F)
#define BIT_CLEAR_BB_ADDR_V1_8197F(x) ((x) & (~BITS_BB_ADDR_V1_8197F))
#define BIT_GET_BB_ADDR_V1_8197F(x)                                            \
	(((x) >> BIT_SHIFT_BB_ADDR_V1_8197F) & BIT_MASK_BB_ADDR_V1_8197F)
#define BIT_SET_BB_ADDR_V1_8197F(x, v)                                         \
	(BIT_CLEAR_BB_ADDR_V1_8197F(x) | BIT_BB_ADDR_V1_8197F(v))

#define BIT_BB_ERRACC_8197F BIT(0)

/* 2 REG_BB_ACCESS_DATA_8197F */

#define BIT_SHIFT_BB_DATA_8197F 0
#define BIT_MASK_BB_DATA_8197F 0xffffffffL
#define BIT_BB_DATA_8197F(x)                                                   \
	(((x) & BIT_MASK_BB_DATA_8197F) << BIT_SHIFT_BB_DATA_8197F)
#define BITS_BB_DATA_8197F (BIT_MASK_BB_DATA_8197F << BIT_SHIFT_BB_DATA_8197F)
#define BIT_CLEAR_BB_DATA_8197F(x) ((x) & (~BITS_BB_DATA_8197F))
#define BIT_GET_BB_DATA_8197F(x)                                               \
	(((x) >> BIT_SHIFT_BB_DATA_8197F) & BIT_MASK_BB_DATA_8197F)
#define BIT_SET_BB_DATA_8197F(x, v)                                            \
	(BIT_CLEAR_BB_DATA_8197F(x) | BIT_BB_DATA_8197F(v))

/* 2 REG_HMEBOX_E0_8197F */

#define BIT_SHIFT_HMEBOX_E0_8197F 0
#define BIT_MASK_HMEBOX_E0_8197F 0xffffffffL
#define BIT_HMEBOX_E0_8197F(x)                                                 \
	(((x) & BIT_MASK_HMEBOX_E0_8197F) << BIT_SHIFT_HMEBOX_E0_8197F)
#define BITS_HMEBOX_E0_8197F                                                   \
	(BIT_MASK_HMEBOX_E0_8197F << BIT_SHIFT_HMEBOX_E0_8197F)
#define BIT_CLEAR_HMEBOX_E0_8197F(x) ((x) & (~BITS_HMEBOX_E0_8197F))
#define BIT_GET_HMEBOX_E0_8197F(x)                                             \
	(((x) >> BIT_SHIFT_HMEBOX_E0_8197F) & BIT_MASK_HMEBOX_E0_8197F)
#define BIT_SET_HMEBOX_E0_8197F(x, v)                                          \
	(BIT_CLEAR_HMEBOX_E0_8197F(x) | BIT_HMEBOX_E0_8197F(v))

/* 2 REG_HMEBOX_E1_8197F */

#define BIT_SHIFT_HMEBOX_E1_8197F 0
#define BIT_MASK_HMEBOX_E1_8197F 0xffffffffL
#define BIT_HMEBOX_E1_8197F(x)                                                 \
	(((x) & BIT_MASK_HMEBOX_E1_8197F) << BIT_SHIFT_HMEBOX_E1_8197F)
#define BITS_HMEBOX_E1_8197F                                                   \
	(BIT_MASK_HMEBOX_E1_8197F << BIT_SHIFT_HMEBOX_E1_8197F)
#define BIT_CLEAR_HMEBOX_E1_8197F(x) ((x) & (~BITS_HMEBOX_E1_8197F))
#define BIT_GET_HMEBOX_E1_8197F(x)                                             \
	(((x) >> BIT_SHIFT_HMEBOX_E1_8197F) & BIT_MASK_HMEBOX_E1_8197F)
#define BIT_SET_HMEBOX_E1_8197F(x, v)                                          \
	(BIT_CLEAR_HMEBOX_E1_8197F(x) | BIT_HMEBOX_E1_8197F(v))

/* 2 REG_HMEBOX_E2_8197F */

#define BIT_SHIFT_HMEBOX_E2_8197F 0
#define BIT_MASK_HMEBOX_E2_8197F 0xffffffffL
#define BIT_HMEBOX_E2_8197F(x)                                                 \
	(((x) & BIT_MASK_HMEBOX_E2_8197F) << BIT_SHIFT_HMEBOX_E2_8197F)
#define BITS_HMEBOX_E2_8197F                                                   \
	(BIT_MASK_HMEBOX_E2_8197F << BIT_SHIFT_HMEBOX_E2_8197F)
#define BIT_CLEAR_HMEBOX_E2_8197F(x) ((x) & (~BITS_HMEBOX_E2_8197F))
#define BIT_GET_HMEBOX_E2_8197F(x)                                             \
	(((x) >> BIT_SHIFT_HMEBOX_E2_8197F) & BIT_MASK_HMEBOX_E2_8197F)
#define BIT_SET_HMEBOX_E2_8197F(x, v)                                          \
	(BIT_CLEAR_HMEBOX_E2_8197F(x) | BIT_HMEBOX_E2_8197F(v))

/* 2 REG_HMEBOX_E3_8197F */

#define BIT_SHIFT_HMEBOX_E3_8197F 0
#define BIT_MASK_HMEBOX_E3_8197F 0xffffffffL
#define BIT_HMEBOX_E3_8197F(x)                                                 \
	(((x) & BIT_MASK_HMEBOX_E3_8197F) << BIT_SHIFT_HMEBOX_E3_8197F)
#define BITS_HMEBOX_E3_8197F                                                   \
	(BIT_MASK_HMEBOX_E3_8197F << BIT_SHIFT_HMEBOX_E3_8197F)
#define BIT_CLEAR_HMEBOX_E3_8197F(x) ((x) & (~BITS_HMEBOX_E3_8197F))
#define BIT_GET_HMEBOX_E3_8197F(x)                                             \
	(((x) >> BIT_SHIFT_HMEBOX_E3_8197F) & BIT_MASK_HMEBOX_E3_8197F)
#define BIT_SET_HMEBOX_E3_8197F(x, v)                                          \
	(BIT_CLEAR_HMEBOX_E3_8197F(x) | BIT_HMEBOX_E3_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_CR_EXT_8197F */

#define BIT_SHIFT_PHY_REQ_DELAY_8197F 24
#define BIT_MASK_PHY_REQ_DELAY_8197F 0xf
#define BIT_PHY_REQ_DELAY_8197F(x)                                             \
	(((x) & BIT_MASK_PHY_REQ_DELAY_8197F) << BIT_SHIFT_PHY_REQ_DELAY_8197F)
#define BITS_PHY_REQ_DELAY_8197F                                               \
	(BIT_MASK_PHY_REQ_DELAY_8197F << BIT_SHIFT_PHY_REQ_DELAY_8197F)
#define BIT_CLEAR_PHY_REQ_DELAY_8197F(x) ((x) & (~BITS_PHY_REQ_DELAY_8197F))
#define BIT_GET_PHY_REQ_DELAY_8197F(x)                                         \
	(((x) >> BIT_SHIFT_PHY_REQ_DELAY_8197F) & BIT_MASK_PHY_REQ_DELAY_8197F)
#define BIT_SET_PHY_REQ_DELAY_8197F(x, v)                                      \
	(BIT_CLEAR_PHY_REQ_DELAY_8197F(x) | BIT_PHY_REQ_DELAY_8197F(v))

#define BIT_SPD_DOWN_8197F BIT(16)

#define BIT_SHIFT_NETYPE4_8197F 4
#define BIT_MASK_NETYPE4_8197F 0x3
#define BIT_NETYPE4_8197F(x)                                                   \
	(((x) & BIT_MASK_NETYPE4_8197F) << BIT_SHIFT_NETYPE4_8197F)
#define BITS_NETYPE4_8197F (BIT_MASK_NETYPE4_8197F << BIT_SHIFT_NETYPE4_8197F)
#define BIT_CLEAR_NETYPE4_8197F(x) ((x) & (~BITS_NETYPE4_8197F))
#define BIT_GET_NETYPE4_8197F(x)                                               \
	(((x) >> BIT_SHIFT_NETYPE4_8197F) & BIT_MASK_NETYPE4_8197F)
#define BIT_SET_NETYPE4_8197F(x, v)                                            \
	(BIT_CLEAR_NETYPE4_8197F(x) | BIT_NETYPE4_8197F(v))

#define BIT_SHIFT_NETYPE3_8197F 2
#define BIT_MASK_NETYPE3_8197F 0x3
#define BIT_NETYPE3_8197F(x)                                                   \
	(((x) & BIT_MASK_NETYPE3_8197F) << BIT_SHIFT_NETYPE3_8197F)
#define BITS_NETYPE3_8197F (BIT_MASK_NETYPE3_8197F << BIT_SHIFT_NETYPE3_8197F)
#define BIT_CLEAR_NETYPE3_8197F(x) ((x) & (~BITS_NETYPE3_8197F))
#define BIT_GET_NETYPE3_8197F(x)                                               \
	(((x) >> BIT_SHIFT_NETYPE3_8197F) & BIT_MASK_NETYPE3_8197F)
#define BIT_SET_NETYPE3_8197F(x, v)                                            \
	(BIT_CLEAR_NETYPE3_8197F(x) | BIT_NETYPE3_8197F(v))

#define BIT_SHIFT_NETYPE2_8197F 0
#define BIT_MASK_NETYPE2_8197F 0x3
#define BIT_NETYPE2_8197F(x)                                                   \
	(((x) & BIT_MASK_NETYPE2_8197F) << BIT_SHIFT_NETYPE2_8197F)
#define BITS_NETYPE2_8197F (BIT_MASK_NETYPE2_8197F << BIT_SHIFT_NETYPE2_8197F)
#define BIT_CLEAR_NETYPE2_8197F(x) ((x) & (~BITS_NETYPE2_8197F))
#define BIT_GET_NETYPE2_8197F(x)                                               \
	(((x) >> BIT_SHIFT_NETYPE2_8197F) & BIT_MASK_NETYPE2_8197F)
#define BIT_SET_NETYPE2_8197F(x, v)                                            \
	(BIT_CLEAR_NETYPE2_8197F(x) | BIT_NETYPE2_8197F(v))

/* 2 REG_FWFF_8197F */

#define BIT_SHIFT_PKTNUM_TH_8197F 24
#define BIT_MASK_PKTNUM_TH_8197F 0xff
#define BIT_PKTNUM_TH_8197F(x)                                                 \
	(((x) & BIT_MASK_PKTNUM_TH_8197F) << BIT_SHIFT_PKTNUM_TH_8197F)
#define BITS_PKTNUM_TH_8197F                                                   \
	(BIT_MASK_PKTNUM_TH_8197F << BIT_SHIFT_PKTNUM_TH_8197F)
#define BIT_CLEAR_PKTNUM_TH_8197F(x) ((x) & (~BITS_PKTNUM_TH_8197F))
#define BIT_GET_PKTNUM_TH_8197F(x)                                             \
	(((x) >> BIT_SHIFT_PKTNUM_TH_8197F) & BIT_MASK_PKTNUM_TH_8197F)
#define BIT_SET_PKTNUM_TH_8197F(x, v)                                          \
	(BIT_CLEAR_PKTNUM_TH_8197F(x) | BIT_PKTNUM_TH_8197F(v))

#define BIT_SHIFT_TIMER_TH_8197F 16
#define BIT_MASK_TIMER_TH_8197F 0xff
#define BIT_TIMER_TH_8197F(x)                                                  \
	(((x) & BIT_MASK_TIMER_TH_8197F) << BIT_SHIFT_TIMER_TH_8197F)
#define BITS_TIMER_TH_8197F                                                    \
	(BIT_MASK_TIMER_TH_8197F << BIT_SHIFT_TIMER_TH_8197F)
#define BIT_CLEAR_TIMER_TH_8197F(x) ((x) & (~BITS_TIMER_TH_8197F))
#define BIT_GET_TIMER_TH_8197F(x)                                              \
	(((x) >> BIT_SHIFT_TIMER_TH_8197F) & BIT_MASK_TIMER_TH_8197F)
#define BIT_SET_TIMER_TH_8197F(x, v)                                           \
	(BIT_CLEAR_TIMER_TH_8197F(x) | BIT_TIMER_TH_8197F(v))

#define BIT_SHIFT_RXPKT1ENADDR_8197F 0
#define BIT_MASK_RXPKT1ENADDR_8197F 0xffff
#define BIT_RXPKT1ENADDR_8197F(x)                                              \
	(((x) & BIT_MASK_RXPKT1ENADDR_8197F) << BIT_SHIFT_RXPKT1ENADDR_8197F)
#define BITS_RXPKT1ENADDR_8197F                                                \
	(BIT_MASK_RXPKT1ENADDR_8197F << BIT_SHIFT_RXPKT1ENADDR_8197F)
#define BIT_CLEAR_RXPKT1ENADDR_8197F(x) ((x) & (~BITS_RXPKT1ENADDR_8197F))
#define BIT_GET_RXPKT1ENADDR_8197F(x)                                          \
	(((x) >> BIT_SHIFT_RXPKT1ENADDR_8197F) & BIT_MASK_RXPKT1ENADDR_8197F)
#define BIT_SET_RXPKT1ENADDR_8197F(x, v)                                       \
	(BIT_CLEAR_RXPKT1ENADDR_8197F(x) | BIT_RXPKT1ENADDR_8197F(v))

/* 2 REG_RXFF_PTR_V1_8197F */

/* 2 REG_NOT_VALID_8197F */

#define BIT_SHIFT_RXFF0_RDPTR_V2_8197F 0
#define BIT_MASK_RXFF0_RDPTR_V2_8197F 0x3ffff
#define BIT_RXFF0_RDPTR_V2_8197F(x)                                            \
	(((x) & BIT_MASK_RXFF0_RDPTR_V2_8197F)                                 \
	 << BIT_SHIFT_RXFF0_RDPTR_V2_8197F)
#define BITS_RXFF0_RDPTR_V2_8197F                                              \
	(BIT_MASK_RXFF0_RDPTR_V2_8197F << BIT_SHIFT_RXFF0_RDPTR_V2_8197F)
#define BIT_CLEAR_RXFF0_RDPTR_V2_8197F(x) ((x) & (~BITS_RXFF0_RDPTR_V2_8197F))
#define BIT_GET_RXFF0_RDPTR_V2_8197F(x)                                        \
	(((x) >> BIT_SHIFT_RXFF0_RDPTR_V2_8197F) &                             \
	 BIT_MASK_RXFF0_RDPTR_V2_8197F)
#define BIT_SET_RXFF0_RDPTR_V2_8197F(x, v)                                     \
	(BIT_CLEAR_RXFF0_RDPTR_V2_8197F(x) | BIT_RXFF0_RDPTR_V2_8197F(v))

/* 2 REG_RXFF_WTR_V1_8197F */

/* 2 REG_NOT_VALID_8197F */

#define BIT_SHIFT_RXFF0_WTPTR_V2_8197F 0
#define BIT_MASK_RXFF0_WTPTR_V2_8197F 0x3ffff
#define BIT_RXFF0_WTPTR_V2_8197F(x)                                            \
	(((x) & BIT_MASK_RXFF0_WTPTR_V2_8197F)                                 \
	 << BIT_SHIFT_RXFF0_WTPTR_V2_8197F)
#define BITS_RXFF0_WTPTR_V2_8197F                                              \
	(BIT_MASK_RXFF0_WTPTR_V2_8197F << BIT_SHIFT_RXFF0_WTPTR_V2_8197F)
#define BIT_CLEAR_RXFF0_WTPTR_V2_8197F(x) ((x) & (~BITS_RXFF0_WTPTR_V2_8197F))
#define BIT_GET_RXFF0_WTPTR_V2_8197F(x)                                        \
	(((x) >> BIT_SHIFT_RXFF0_WTPTR_V2_8197F) &                             \
	 BIT_MASK_RXFF0_WTPTR_V2_8197F)
#define BIT_SET_RXFF0_WTPTR_V2_8197F(x, v)                                     \
	(BIT_CLEAR_RXFF0_WTPTR_V2_8197F(x) | BIT_RXFF0_WTPTR_V2_8197F(v))

/* 2 REG_FE2IMR_8197F */
#define BIT_FS_TXSC_DESC_DONE_INT_EN_8197F BIT(28)
#define BIT_FS_TXSC_BKDONE_INT_EN_8197F BIT(27)
#define BIT_FS_TXSC_BEDONE_INT_EN_8197F BIT(26)
#define BIT_FS_TXSC_VIDONE_INT_EN_8197F BIT(25)
#define BIT_FS_TXSC_VODONE_INT_EN_8197F BIT(24)
#define BIT_FS_ATIM_MB7_INT_EN_8197F BIT(23)
#define BIT_FS_ATIM_MB6_INT_EN_8197F BIT(22)
#define BIT_FS_ATIM_MB5_INT_EN_8197F BIT(21)
#define BIT_FS_ATIM_MB4_INT_EN_8197F BIT(20)
#define BIT_FS_ATIM_MB3_INT_EN_8197F BIT(19)
#define BIT_FS_ATIM_MB2_INT_EN_8197F BIT(18)
#define BIT_FS_ATIM_MB1_INT_EN_8197F BIT(17)
#define BIT_FS_ATIM_MB0_INT_EN_8197F BIT(16)
#define BIT_FS_TBTT4INT_EN_8197F BIT(11)
#define BIT_FS_TBTT3INT_EN_8197F BIT(10)
#define BIT_FS_TBTT2INT_EN_8197F BIT(9)
#define BIT_FS_TBTT1INT_EN_8197F BIT(8)
#define BIT_FS_TBTT0_MB7INT_EN_8197F BIT(7)
#define BIT_FS_TBTT0_MB6INT_EN_8197F BIT(6)
#define BIT_FS_TBTT0_MB5INT_EN_8197F BIT(5)
#define BIT_FS_TBTT0_MB4INT_EN_8197F BIT(4)
#define BIT_FS_TBTT0_MB3INT_EN_8197F BIT(3)
#define BIT_FS_TBTT0_MB2INT_EN_8197F BIT(2)
#define BIT_FS_TBTT0_MB1INT_EN_8197F BIT(1)
#define BIT_FS_TBTT0_INT_EN_8197F BIT(0)

/* 2 REG_FE2ISR_8197F */
#define BIT_FS_TXSC_DESC_DONE_INT_8197F BIT(28)
#define BIT_FS_TXSC_BKDONE_INT_8197F BIT(27)
#define BIT_FS_TXSC_BEDONE_INT_8197F BIT(26)
#define BIT_FS_TXSC_VIDONE_INT_8197F BIT(25)
#define BIT_FS_TXSC_VODONE_INT_8197F BIT(24)
#define BIT_FS_ATIM_MB7_INT_8197F BIT(23)
#define BIT_FS_ATIM_MB6_INT_8197F BIT(22)
#define BIT_FS_ATIM_MB5_INT_8197F BIT(21)
#define BIT_FS_ATIM_MB4_INT_8197F BIT(20)
#define BIT_FS_ATIM_MB3_INT_8197F BIT(19)
#define BIT_FS_ATIM_MB2_INT_8197F BIT(18)
#define BIT_FS_ATIM_MB1_INT_8197F BIT(17)
#define BIT_FS_ATIM_MB0_INT_8197F BIT(16)
#define BIT_FS_TBTT4INT_8197F BIT(11)
#define BIT_FS_TBTT3INT_8197F BIT(10)
#define BIT_FS_TBTT2INT_8197F BIT(9)
#define BIT_FS_TBTT1INT_8197F BIT(8)
#define BIT_FS_TBTT0_MB7INT_8197F BIT(7)
#define BIT_FS_TBTT0_MB6INT_8197F BIT(6)
#define BIT_FS_TBTT0_MB5INT_8197F BIT(5)
#define BIT_FS_TBTT0_MB4INT_8197F BIT(4)
#define BIT_FS_TBTT0_MB3INT_8197F BIT(3)
#define BIT_FS_TBTT0_MB2INT_8197F BIT(2)
#define BIT_FS_TBTT0_MB1INT_8197F BIT(1)
#define BIT_FS_TBTT0_INT_8197F BIT(0)

/* 2 REG_FE3IMR_8197F */
#define BIT_FS_BCNELY4_AGGR_INT_EN_8197F BIT(31)
#define BIT_FS_BCNELY3_AGGR_INT_EN_8197F BIT(30)
#define BIT_FS_BCNELY2_AGGR_INT_EN_8197F BIT(29)
#define BIT_FS_BCNELY1_AGGR_INT_EN_8197F BIT(28)
#define BIT_FS_BCNDMA4_INT_EN_8197F BIT(27)
#define BIT_FS_BCNDMA3_INT_EN_8197F BIT(26)
#define BIT_FS_BCNDMA2_INT_EN_8197F BIT(25)
#define BIT_FS_BCNDMA1_INT_EN_8197F BIT(24)
#define BIT_FS_BCNDMA0_MB7_INT_EN_8197F BIT(23)
#define BIT_FS_BCNDMA0_MB6_INT_EN_8197F BIT(22)
#define BIT_FS_BCNDMA0_MB5_INT_EN_8197F BIT(21)
#define BIT_FS_BCNDMA0_MB4_INT_EN_8197F BIT(20)
#define BIT_FS_BCNDMA0_MB3_INT_EN_8197F BIT(19)
#define BIT_FS_BCNDMA0_MB2_INT_EN_8197F BIT(18)
#define BIT_FS_BCNDMA0_MB1_INT_EN_8197F BIT(17)
#define BIT_FS_BCNDMA0_INT_EN_8197F BIT(16)
#define BIT_FS_MTI_BCNIVLEAR_INT__EN_8197F BIT(15)
#define BIT_FS_BCNERLY4_INT_EN_8197F BIT(11)
#define BIT_FS_BCNERLY3_INT_EN_8197F BIT(10)
#define BIT_FS_BCNERLY2_INT_EN_8197F BIT(9)
#define BIT_FS_BCNERLY1_INT_EN_8197F BIT(8)
#define BIT_FS_BCNERLY0_MB7INT_EN_8197F BIT(7)
#define BIT_FS_BCNERLY0_MB6INT_EN_8197F BIT(6)
#define BIT_FS_BCNERLY0_MB5INT_EN_8197F BIT(5)
#define BIT_FS_BCNERLY0_MB4INT_EN_8197F BIT(4)
#define BIT_FS_BCNERLY0_MB3INT_EN_8197F BIT(3)
#define BIT_FS_BCNERLY0_MB2INT_EN_8197F BIT(2)
#define BIT_FS_BCNERLY0_MB1INT_EN_8197F BIT(1)
#define BIT_FS_BCNERLY0_INT_EN_8197F BIT(0)

/* 2 REG_FE3ISR_8197F */
#define BIT_FS_BCNELY4_AGGR_INT_8197F BIT(31)
#define BIT_FS_BCNELY3_AGGR_INT_8197F BIT(30)
#define BIT_FS_BCNELY2_AGGR_INT_8197F BIT(29)
#define BIT_FS_BCNELY1_AGGR_INT_8197F BIT(28)
#define BIT_FS_BCNDMA4_INT_8197F BIT(27)
#define BIT_FS_BCNDMA3_INT_8197F BIT(26)
#define BIT_FS_BCNDMA2_INT_8197F BIT(25)
#define BIT_FS_BCNDMA1_INT_8197F BIT(24)
#define BIT_FS_BCNDMA0_MB7_INT_8197F BIT(23)
#define BIT_FS_BCNDMA0_MB6_INT_8197F BIT(22)
#define BIT_FS_BCNDMA0_MB5_INT_8197F BIT(21)
#define BIT_FS_BCNDMA0_MB4_INT_8197F BIT(20)
#define BIT_FS_BCNDMA0_MB3_INT_8197F BIT(19)
#define BIT_FS_BCNDMA0_MB2_INT_8197F BIT(18)
#define BIT_FS_BCNDMA0_MB1_INT_8197F BIT(17)
#define BIT_FS_BCNDMA0_INT_8197F BIT(16)
#define BIT_FS_MTI_BCNIVLEAR_INT_8197F BIT(15)
#define BIT_FS_BCNERLY4_INT_8197F BIT(11)
#define BIT_FS_BCNERLY3_INT_8197F BIT(10)
#define BIT_FS_BCNERLY2_INT_8197F BIT(9)
#define BIT_FS_BCNERLY1_INT_8197F BIT(8)
#define BIT_FS_BCNERLY0_MB7INT_8197F BIT(7)
#define BIT_FS_BCNERLY0_MB6INT_8197F BIT(6)
#define BIT_FS_BCNERLY0_MB5INT_8197F BIT(5)
#define BIT_FS_BCNERLY0_MB4INT_8197F BIT(4)
#define BIT_FS_BCNERLY0_MB3INT_8197F BIT(3)
#define BIT_FS_BCNERLY0_MB2INT_8197F BIT(2)
#define BIT_FS_BCNERLY0_MB1INT_8197F BIT(1)
#define BIT_FS_BCNERLY0_INT_8197F BIT(0)

/* 2 REG_FE4IMR_8197F */
#define BIT_PORT4_PKTIN_INT_EN_8197F BIT(19)
#define BIT_PORT3_PKTIN_INT_EN_8197F BIT(18)
#define BIT_PORT2_PKTIN_INT_EN_8197F BIT(17)
#define BIT_PORT1_PKTIN_INT_EN_8197F BIT(16)
#define BIT_PORT4_RXUCMD0_OK_INT_EN_8197F BIT(15)
#define BIT_PORT4_RXUCMD1_OK_INT_EN_8197F BIT(14)
#define BIT_PORT4_RXBCMD0_OK_INT_EN_8197F BIT(13)
#define BIT_PORT4_RXBCMD1_OK_INT_EN_8197F BIT(12)
#define BIT_PORT3_RXUCMD0_OK_INT_EN_8197F BIT(11)
#define BIT_PORT3_RXUCMD1_OK_INT_EN_8197F BIT(10)
#define BIT_PORT3_RXBCMD0_OK_INT_EN_8197F BIT(9)
#define BIT_PORT3_RXBCMD1_OK_INT_EN_8197F BIT(8)
#define BIT_PORT2_RXUCMD0_OK_INT_EN_8197F BIT(7)
#define BIT_PORT2_RXUCMD1_OK_INT_EN_8197F BIT(6)
#define BIT_PORT2_RXBCMD0_OK_INT_EN_8197F BIT(5)
#define BIT_PORT2_RXBCMD1_OK_INT_EN_8197F BIT(4)
#define BIT_PORT1_RXUCMD0_OK_INT_EN_8197F BIT(3)
#define BIT_PORT1_RXUCMD1_OK_INT_EN_8197F BIT(2)
#define BIT_PORT1_RXBCMD0_OK_INT_EN_8197F BIT(1)
#define BIT_PORT1_RXBCMD1_OK_INT_EN_8197F BIT(0)

/* 2 REG_FE4ISR_8197F */
#define BIT_PORT4_PKTIN_INT_8197F BIT(19)
#define BIT_PORT3_PKTIN_INT_8197F BIT(18)
#define BIT_PORT2_PKTIN_INT_8197F BIT(17)
#define BIT_PORT1_PKTIN_INT_8197F BIT(16)
#define BIT_PORT4_RXUCMD0_OK_INT_8197F BIT(15)
#define BIT_PORT4_RXUCMD1_OK_INT_8197F BIT(14)
#define BIT_PORT4_RXBCMD0_OK_INT_8197F BIT(13)
#define BIT_PORT4_RXBCMD1_OK_INT_8197F BIT(12)
#define BIT_PORT3_RXUCMD0_OK_INT_8197F BIT(11)
#define BIT_PORT3_RXUCMD1_OK_INT_8197F BIT(10)
#define BIT_PORT3_RXBCMD0_OK_INT_8197F BIT(9)
#define BIT_PORT3_RXBCMD1_OK_INT_8197F BIT(8)
#define BIT_PORT2_RXUCMD0_OK_INT_8197F BIT(7)
#define BIT_PORT2_RXUCMD1_OK_INT_8197F BIT(6)
#define BIT_PORT2_RXBCMD0_OK_INT_8197F BIT(5)
#define BIT_PORT2_RXBCMD1_OK_INT_8197F BIT(4)
#define BIT_PORT1_RXUCMD0_OK_INT_8197F BIT(3)
#define BIT_PORT1_RXUCMD1_OK_INT_8197F BIT(2)
#define BIT_PORT1_RXBCMD0_OK_INT_8197F BIT(1)
#define BIT_PORT1_RXBCMD1_OK_INT_8197F BIT(0)

/* 2 REG_FT1IMR_8197F */
#define BIT__FT2ISR__IND_MSK_8197F BIT(30)
#define BIT_FTM_PTT_INT_EN_8197F BIT(29)
#define BIT_RXFTMREQ_INT_EN_8197F BIT(28)
#define BIT_RXFTM_INT_EN_8197F BIT(27)
#define BIT_TXFTM_INT_EN_8197F BIT(26)
#define BIT_FS_H2C_CMD_OK_INT_EN_8197F BIT(25)
#define BIT_FS_H2C_CMD_FULL_INT_EN_8197F BIT(24)
#define BIT_FS_MACID_PWRCHANGE5_INT_EN_8197F BIT(23)
#define BIT_FS_MACID_PWRCHANGE4_INT_EN_8197F BIT(22)
#define BIT_FS_MACID_PWRCHANGE3_INT_EN_8197F BIT(21)
#define BIT_FS_MACID_PWRCHANGE2_INT_EN_8197F BIT(20)
#define BIT_FS_MACID_PWRCHANGE1_INT_EN_8197F BIT(19)
#define BIT_FS_MACID_PWRCHANGE0_INT_EN_8197F BIT(18)
#define BIT_FS_CTWEND2_INT_EN_8197F BIT(17)
#define BIT_FS_CTWEND1_INT_EN_8197F BIT(16)
#define BIT_FS_CTWEND0_INT_EN_8197F BIT(15)
#define BIT_FS_TX_NULL1_INT_EN_8197F BIT(14)
#define BIT_FS_TX_NULL0_INT_EN_8197F BIT(13)
#define BIT_FS_TSF_BIT32_TOGGLE_EN_8197F BIT(12)
#define BIT_FS_P2P_RFON2_INT_EN_8197F BIT(11)
#define BIT_FS_P2P_RFOFF2_INT_EN_8197F BIT(10)
#define BIT_FS_P2P_RFON1_INT_EN_8197F BIT(9)
#define BIT_FS_P2P_RFOFF1_INT_EN_8197F BIT(8)
#define BIT_FS_P2P_RFON0_INT_EN_8197F BIT(7)
#define BIT_FS_P2P_RFOFF0_INT_EN_8197F BIT(6)
#define BIT_FS_RX_UAPSDMD1_EN_8197F BIT(5)
#define BIT_FS_RX_UAPSDMD0_EN_8197F BIT(4)
#define BIT_FS_TRIGGER_PKT_EN_8197F BIT(3)
#define BIT_FS_EOSP_INT_EN_8197F BIT(2)
#define BIT_FS_RPWM2_INT_EN_8197F BIT(1)
#define BIT_FS_RPWM_INT_EN_8197F BIT(0)

/* 2 REG_FT1ISR_8197F */
#define BIT__FT2ISR__IND_INT_8197F BIT(30)
#define BIT_FTM_PTT_INT_8197F BIT(29)
#define BIT_RXFTMREQ_INT_8197F BIT(28)
#define BIT_RXFTM_INT_8197F BIT(27)
#define BIT_TXFTM_INT_8197F BIT(26)
#define BIT_FS_H2C_CMD_OK_INT_8197F BIT(25)
#define BIT_FS_H2C_CMD_FULL_INT_8197F BIT(24)
#define BIT_FS_MACID_PWRCHANGE5_INT_8197F BIT(23)
#define BIT_FS_MACID_PWRCHANGE4_INT_8197F BIT(22)
#define BIT_FS_MACID_PWRCHANGE3_INT_8197F BIT(21)
#define BIT_FS_MACID_PWRCHANGE2_INT_8197F BIT(20)
#define BIT_FS_MACID_PWRCHANGE1_INT_8197F BIT(19)
#define BIT_FS_MACID_PWRCHANGE0_INT_8197F BIT(18)
#define BIT_FS_CTWEND2_INT_8197F BIT(17)
#define BIT_FS_CTWEND1_INT_8197F BIT(16)
#define BIT_FS_CTWEND0_INT_8197F BIT(15)
#define BIT_FS_TX_NULL1_INT_8197F BIT(14)
#define BIT_FS_TX_NULL0_INT_8197F BIT(13)
#define BIT_FS_TSF_BIT32_TOGGLE_INT_8197F BIT(12)
#define BIT_FS_P2P_RFON2_INT_8197F BIT(11)
#define BIT_FS_P2P_RFOFF2_INT_8197F BIT(10)
#define BIT_FS_P2P_RFON1_INT_8197F BIT(9)
#define BIT_FS_P2P_RFOFF1_INT_8197F BIT(8)
#define BIT_FS_P2P_RFON0_INT_8197F BIT(7)
#define BIT_FS_P2P_RFOFF0_INT_8197F BIT(6)
#define BIT_FS_RX_UAPSDMD1_INT_8197F BIT(5)
#define BIT_FS_RX_UAPSDMD0_INT_8197F BIT(4)
#define BIT_FS_TRIGGER_PKT_INT_8197F BIT(3)
#define BIT_FS_EOSP_INT_8197F BIT(2)
#define BIT_FS_RPWM2_INT_8197F BIT(1)
#define BIT_FS_RPWM_INT_8197F BIT(0)

/* 2 REG_SPWR0_8197F */

#define BIT_SHIFT_MID_31TO0_8197F 0
#define BIT_MASK_MID_31TO0_8197F 0xffffffffL
#define BIT_MID_31TO0_8197F(x)                                                 \
	(((x) & BIT_MASK_MID_31TO0_8197F) << BIT_SHIFT_MID_31TO0_8197F)
#define BITS_MID_31TO0_8197F                                                   \
	(BIT_MASK_MID_31TO0_8197F << BIT_SHIFT_MID_31TO0_8197F)
#define BIT_CLEAR_MID_31TO0_8197F(x) ((x) & (~BITS_MID_31TO0_8197F))
#define BIT_GET_MID_31TO0_8197F(x)                                             \
	(((x) >> BIT_SHIFT_MID_31TO0_8197F) & BIT_MASK_MID_31TO0_8197F)
#define BIT_SET_MID_31TO0_8197F(x, v)                                          \
	(BIT_CLEAR_MID_31TO0_8197F(x) | BIT_MID_31TO0_8197F(v))

/* 2 REG_SPWR1_8197F */

#define BIT_SHIFT_MID_63TO32_8197F 0
#define BIT_MASK_MID_63TO32_8197F 0xffffffffL
#define BIT_MID_63TO32_8197F(x)                                                \
	(((x) & BIT_MASK_MID_63TO32_8197F) << BIT_SHIFT_MID_63TO32_8197F)
#define BITS_MID_63TO32_8197F                                                  \
	(BIT_MASK_MID_63TO32_8197F << BIT_SHIFT_MID_63TO32_8197F)
#define BIT_CLEAR_MID_63TO32_8197F(x) ((x) & (~BITS_MID_63TO32_8197F))
#define BIT_GET_MID_63TO32_8197F(x)                                            \
	(((x) >> BIT_SHIFT_MID_63TO32_8197F) & BIT_MASK_MID_63TO32_8197F)
#define BIT_SET_MID_63TO32_8197F(x, v)                                         \
	(BIT_CLEAR_MID_63TO32_8197F(x) | BIT_MID_63TO32_8197F(v))

/* 2 REG_SPWR2_8197F */

#define BIT_SHIFT_MID_95O64_8197F 0
#define BIT_MASK_MID_95O64_8197F 0xffffffffL
#define BIT_MID_95O64_8197F(x)                                                 \
	(((x) & BIT_MASK_MID_95O64_8197F) << BIT_SHIFT_MID_95O64_8197F)
#define BITS_MID_95O64_8197F                                                   \
	(BIT_MASK_MID_95O64_8197F << BIT_SHIFT_MID_95O64_8197F)
#define BIT_CLEAR_MID_95O64_8197F(x) ((x) & (~BITS_MID_95O64_8197F))
#define BIT_GET_MID_95O64_8197F(x)                                             \
	(((x) >> BIT_SHIFT_MID_95O64_8197F) & BIT_MASK_MID_95O64_8197F)
#define BIT_SET_MID_95O64_8197F(x, v)                                          \
	(BIT_CLEAR_MID_95O64_8197F(x) | BIT_MID_95O64_8197F(v))

/* 2 REG_SPWR3_8197F */

#define BIT_SHIFT_MID_127TO96_8197F 0
#define BIT_MASK_MID_127TO96_8197F 0xffffffffL
#define BIT_MID_127TO96_8197F(x)                                               \
	(((x) & BIT_MASK_MID_127TO96_8197F) << BIT_SHIFT_MID_127TO96_8197F)
#define BITS_MID_127TO96_8197F                                                 \
	(BIT_MASK_MID_127TO96_8197F << BIT_SHIFT_MID_127TO96_8197F)
#define BIT_CLEAR_MID_127TO96_8197F(x) ((x) & (~BITS_MID_127TO96_8197F))
#define BIT_GET_MID_127TO96_8197F(x)                                           \
	(((x) >> BIT_SHIFT_MID_127TO96_8197F) & BIT_MASK_MID_127TO96_8197F)
#define BIT_SET_MID_127TO96_8197F(x, v)                                        \
	(BIT_CLEAR_MID_127TO96_8197F(x) | BIT_MID_127TO96_8197F(v))

/* 2 REG_POWSEQ_8197F */

#define BIT_SHIFT_SEQNUM_MID_8197F 16
#define BIT_MASK_SEQNUM_MID_8197F 0xffff
#define BIT_SEQNUM_MID_8197F(x)                                                \
	(((x) & BIT_MASK_SEQNUM_MID_8197F) << BIT_SHIFT_SEQNUM_MID_8197F)
#define BITS_SEQNUM_MID_8197F                                                  \
	(BIT_MASK_SEQNUM_MID_8197F << BIT_SHIFT_SEQNUM_MID_8197F)
#define BIT_CLEAR_SEQNUM_MID_8197F(x) ((x) & (~BITS_SEQNUM_MID_8197F))
#define BIT_GET_SEQNUM_MID_8197F(x)                                            \
	(((x) >> BIT_SHIFT_SEQNUM_MID_8197F) & BIT_MASK_SEQNUM_MID_8197F)
#define BIT_SET_SEQNUM_MID_8197F(x, v)                                         \
	(BIT_CLEAR_SEQNUM_MID_8197F(x) | BIT_SEQNUM_MID_8197F(v))

#define BIT_SHIFT_REF_MID_8197F 0
#define BIT_MASK_REF_MID_8197F 0x7f
#define BIT_REF_MID_8197F(x)                                                   \
	(((x) & BIT_MASK_REF_MID_8197F) << BIT_SHIFT_REF_MID_8197F)
#define BITS_REF_MID_8197F (BIT_MASK_REF_MID_8197F << BIT_SHIFT_REF_MID_8197F)
#define BIT_CLEAR_REF_MID_8197F(x) ((x) & (~BITS_REF_MID_8197F))
#define BIT_GET_REF_MID_8197F(x)                                               \
	(((x) >> BIT_SHIFT_REF_MID_8197F) & BIT_MASK_REF_MID_8197F)
#define BIT_SET_REF_MID_8197F(x, v)                                            \
	(BIT_CLEAR_REF_MID_8197F(x) | BIT_REF_MID_8197F(v))

/* 2 REG_TC7_CTRL_V1_8197F */
#define BIT_TC7INT_EN_8197F BIT(26)
#define BIT_TC7MODE_8197F BIT(25)
#define BIT_TC7EN_8197F BIT(24)

#define BIT_SHIFT_TC7DATA_8197F 0
#define BIT_MASK_TC7DATA_8197F 0xffffff
#define BIT_TC7DATA_8197F(x)                                                   \
	(((x) & BIT_MASK_TC7DATA_8197F) << BIT_SHIFT_TC7DATA_8197F)
#define BITS_TC7DATA_8197F (BIT_MASK_TC7DATA_8197F << BIT_SHIFT_TC7DATA_8197F)
#define BIT_CLEAR_TC7DATA_8197F(x) ((x) & (~BITS_TC7DATA_8197F))
#define BIT_GET_TC7DATA_8197F(x)                                               \
	(((x) >> BIT_SHIFT_TC7DATA_8197F) & BIT_MASK_TC7DATA_8197F)
#define BIT_SET_TC7DATA_8197F(x, v)                                            \
	(BIT_CLEAR_TC7DATA_8197F(x) | BIT_TC7DATA_8197F(v))

/* 2 REG_TC8_CTRL_V1_8197F */
#define BIT_TC8INT_EN_8197F BIT(26)
#define BIT_TC8MODE_8197F BIT(25)
#define BIT_TC8EN_8197F BIT(24)

#define BIT_SHIFT_TC8DATA_8197F 0
#define BIT_MASK_TC8DATA_8197F 0xffffff
#define BIT_TC8DATA_8197F(x)                                                   \
	(((x) & BIT_MASK_TC8DATA_8197F) << BIT_SHIFT_TC8DATA_8197F)
#define BITS_TC8DATA_8197F (BIT_MASK_TC8DATA_8197F << BIT_SHIFT_TC8DATA_8197F)
#define BIT_CLEAR_TC8DATA_8197F(x) ((x) & (~BITS_TC8DATA_8197F))
#define BIT_GET_TC8DATA_8197F(x)                                               \
	(((x) >> BIT_SHIFT_TC8DATA_8197F) & BIT_MASK_TC8DATA_8197F)
#define BIT_SET_TC8DATA_8197F(x, v)                                            \
	(BIT_CLEAR_TC8DATA_8197F(x) | BIT_TC8DATA_8197F(v))

/* 2 REG_RXBCN_TBTT_INTERVAL_PORT0TO3_8197F */

/* 2 REG_RXBCN_TBTT_INTERVAL_PORT4_8197F */

/* 2 REG_EXT_QUEUE_REG_8197F */

#define BIT_SHIFT_PCIE_PRIORITY_SEL_8197F 0
#define BIT_MASK_PCIE_PRIORITY_SEL_8197F 0x3
#define BIT_PCIE_PRIORITY_SEL_8197F(x)                                         \
	(((x) & BIT_MASK_PCIE_PRIORITY_SEL_8197F)                              \
	 << BIT_SHIFT_PCIE_PRIORITY_SEL_8197F)
#define BITS_PCIE_PRIORITY_SEL_8197F                                           \
	(BIT_MASK_PCIE_PRIORITY_SEL_8197F << BIT_SHIFT_PCIE_PRIORITY_SEL_8197F)
#define BIT_CLEAR_PCIE_PRIORITY_SEL_8197F(x)                                   \
	((x) & (~BITS_PCIE_PRIORITY_SEL_8197F))
#define BIT_GET_PCIE_PRIORITY_SEL_8197F(x)                                     \
	(((x) >> BIT_SHIFT_PCIE_PRIORITY_SEL_8197F) &                          \
	 BIT_MASK_PCIE_PRIORITY_SEL_8197F)
#define BIT_SET_PCIE_PRIORITY_SEL_8197F(x, v)                                  \
	(BIT_CLEAR_PCIE_PRIORITY_SEL_8197F(x) | BIT_PCIE_PRIORITY_SEL_8197F(v))

/* 2 REG_COUNTER_CONTROL_8197F */

#define BIT_SHIFT_COUNTER_BASE_8197F 16
#define BIT_MASK_COUNTER_BASE_8197F 0x1fff
#define BIT_COUNTER_BASE_8197F(x)                                              \
	(((x) & BIT_MASK_COUNTER_BASE_8197F) << BIT_SHIFT_COUNTER_BASE_8197F)
#define BITS_COUNTER_BASE_8197F                                                \
	(BIT_MASK_COUNTER_BASE_8197F << BIT_SHIFT_COUNTER_BASE_8197F)
#define BIT_CLEAR_COUNTER_BASE_8197F(x) ((x) & (~BITS_COUNTER_BASE_8197F))
#define BIT_GET_COUNTER_BASE_8197F(x)                                          \
	(((x) >> BIT_SHIFT_COUNTER_BASE_8197F) & BIT_MASK_COUNTER_BASE_8197F)
#define BIT_SET_COUNTER_BASE_8197F(x, v)                                       \
	(BIT_CLEAR_COUNTER_BASE_8197F(x) | BIT_COUNTER_BASE_8197F(v))

#define BIT_EN_RTS_REQ_8197F BIT(9)
#define BIT_EN_EDCA_REQ_8197F BIT(8)
#define BIT_EN_PTCL_REQ_8197F BIT(7)
#define BIT_EN_SCH_REQ_8197F BIT(6)
#define BIT_EN_USB_CNT_8197F BIT(5)
#define BIT_EN_PCIE_CNT_8197F BIT(4)
#define BIT_RQPN_CNT_8197F BIT(3)
#define BIT_RDE_CNT_8197F BIT(2)
#define BIT_TDE_CNT_8197F BIT(1)
#define BIT_DIS_CNT_8197F BIT(0)

/* 2 REG_COUNTER_TH_8197F */
#define BIT_CNT_ALL_MACID_8197F BIT(31)

#define BIT_SHIFT_CNT_MACID_8197F 24
#define BIT_MASK_CNT_MACID_8197F 0x7f
#define BIT_CNT_MACID_8197F(x)                                                 \
	(((x) & BIT_MASK_CNT_MACID_8197F) << BIT_SHIFT_CNT_MACID_8197F)
#define BITS_CNT_MACID_8197F                                                   \
	(BIT_MASK_CNT_MACID_8197F << BIT_SHIFT_CNT_MACID_8197F)
#define BIT_CLEAR_CNT_MACID_8197F(x) ((x) & (~BITS_CNT_MACID_8197F))
#define BIT_GET_CNT_MACID_8197F(x)                                             \
	(((x) >> BIT_SHIFT_CNT_MACID_8197F) & BIT_MASK_CNT_MACID_8197F)
#define BIT_SET_CNT_MACID_8197F(x, v)                                          \
	(BIT_CLEAR_CNT_MACID_8197F(x) | BIT_CNT_MACID_8197F(v))

#define BIT_SHIFT_AGG_VALUE2_8197F 16
#define BIT_MASK_AGG_VALUE2_8197F 0x7f
#define BIT_AGG_VALUE2_8197F(x)                                                \
	(((x) & BIT_MASK_AGG_VALUE2_8197F) << BIT_SHIFT_AGG_VALUE2_8197F)
#define BITS_AGG_VALUE2_8197F                                                  \
	(BIT_MASK_AGG_VALUE2_8197F << BIT_SHIFT_AGG_VALUE2_8197F)
#define BIT_CLEAR_AGG_VALUE2_8197F(x) ((x) & (~BITS_AGG_VALUE2_8197F))
#define BIT_GET_AGG_VALUE2_8197F(x)                                            \
	(((x) >> BIT_SHIFT_AGG_VALUE2_8197F) & BIT_MASK_AGG_VALUE2_8197F)
#define BIT_SET_AGG_VALUE2_8197F(x, v)                                         \
	(BIT_CLEAR_AGG_VALUE2_8197F(x) | BIT_AGG_VALUE2_8197F(v))

#define BIT_SHIFT_AGG_VALUE1_8197F 8
#define BIT_MASK_AGG_VALUE1_8197F 0x7f
#define BIT_AGG_VALUE1_8197F(x)                                                \
	(((x) & BIT_MASK_AGG_VALUE1_8197F) << BIT_SHIFT_AGG_VALUE1_8197F)
#define BITS_AGG_VALUE1_8197F                                                  \
	(BIT_MASK_AGG_VALUE1_8197F << BIT_SHIFT_AGG_VALUE1_8197F)
#define BIT_CLEAR_AGG_VALUE1_8197F(x) ((x) & (~BITS_AGG_VALUE1_8197F))
#define BIT_GET_AGG_VALUE1_8197F(x)                                            \
	(((x) >> BIT_SHIFT_AGG_VALUE1_8197F) & BIT_MASK_AGG_VALUE1_8197F)
#define BIT_SET_AGG_VALUE1_8197F(x, v)                                         \
	(BIT_CLEAR_AGG_VALUE1_8197F(x) | BIT_AGG_VALUE1_8197F(v))

#define BIT_SHIFT_AGG_VALUE0_8197F 0
#define BIT_MASK_AGG_VALUE0_8197F 0x7f
#define BIT_AGG_VALUE0_8197F(x)                                                \
	(((x) & BIT_MASK_AGG_VALUE0_8197F) << BIT_SHIFT_AGG_VALUE0_8197F)
#define BITS_AGG_VALUE0_8197F                                                  \
	(BIT_MASK_AGG_VALUE0_8197F << BIT_SHIFT_AGG_VALUE0_8197F)
#define BIT_CLEAR_AGG_VALUE0_8197F(x) ((x) & (~BITS_AGG_VALUE0_8197F))
#define BIT_GET_AGG_VALUE0_8197F(x)                                            \
	(((x) >> BIT_SHIFT_AGG_VALUE0_8197F) & BIT_MASK_AGG_VALUE0_8197F)
#define BIT_SET_AGG_VALUE0_8197F(x, v)                                         \
	(BIT_CLEAR_AGG_VALUE0_8197F(x) | BIT_AGG_VALUE0_8197F(v))

/* 2 REG_COUNTER_SET_8197F */
#define BIT_RTS_RST_8197F BIT(24)
#define BIT_PTCL_RST_8197F BIT(23)
#define BIT_SCH_RST_8197F BIT(22)
#define BIT_EDCA_RST_8197F BIT(21)
#define BIT_RQPN_RST_8197F BIT(20)
#define BIT_USB_RST_8197F BIT(19)
#define BIT_PCIE_RST_8197F BIT(18)
#define BIT_RXDMA_RST_8197F BIT(17)
#define BIT_TXDMA_RST_8197F BIT(16)
#define BIT_EN_RTS_START_8197F BIT(8)
#define BIT_EN_PTCL_START_8197F BIT(7)
#define BIT_EN_SCH_START_8197F BIT(6)
#define BIT_EN_EDCA_START_8197F BIT(5)
#define BIT_EN_RQPN_START_8197F BIT(4)
#define BIT_EN_USB_START_8197F BIT(3)
#define BIT_EN_PCIE_START_8197F BIT(2)
#define BIT_EN_RXDMA_START_8197F BIT(1)
#define BIT_EN_TXDMA_START_8197F BIT(0)

/* 2 REG_COUNTER_OVERFLOW_8197F */
#define BIT_RTS_OVF_8197F BIT(8)
#define BIT_PTCL_OVF_8197F BIT(7)
#define BIT_SCH_OVF_8197F BIT(6)
#define BIT_EDCA_OVF_8197F BIT(5)
#define BIT_RQPN_OVF_8197F BIT(4)
#define BIT_USB_OVF_8197F BIT(3)
#define BIT_PCIE_OVF_8197F BIT(2)
#define BIT_RXDMA_OVF_8197F BIT(1)
#define BIT_TXDMA_OVF_8197F BIT(0)

/* 2 REG_TDE_LEN_TH_8197F */

#define BIT_SHIFT_TXDMA_LEN_TH0_8197F 16
#define BIT_MASK_TXDMA_LEN_TH0_8197F 0xffff
#define BIT_TXDMA_LEN_TH0_8197F(x)                                             \
	(((x) & BIT_MASK_TXDMA_LEN_TH0_8197F) << BIT_SHIFT_TXDMA_LEN_TH0_8197F)
#define BITS_TXDMA_LEN_TH0_8197F                                               \
	(BIT_MASK_TXDMA_LEN_TH0_8197F << BIT_SHIFT_TXDMA_LEN_TH0_8197F)
#define BIT_CLEAR_TXDMA_LEN_TH0_8197F(x) ((x) & (~BITS_TXDMA_LEN_TH0_8197F))
#define BIT_GET_TXDMA_LEN_TH0_8197F(x)                                         \
	(((x) >> BIT_SHIFT_TXDMA_LEN_TH0_8197F) & BIT_MASK_TXDMA_LEN_TH0_8197F)
#define BIT_SET_TXDMA_LEN_TH0_8197F(x, v)                                      \
	(BIT_CLEAR_TXDMA_LEN_TH0_8197F(x) | BIT_TXDMA_LEN_TH0_8197F(v))

#define BIT_SHIFT_TXDMA_LEN_TH1_8197F 0
#define BIT_MASK_TXDMA_LEN_TH1_8197F 0xffff
#define BIT_TXDMA_LEN_TH1_8197F(x)                                             \
	(((x) & BIT_MASK_TXDMA_LEN_TH1_8197F) << BIT_SHIFT_TXDMA_LEN_TH1_8197F)
#define BITS_TXDMA_LEN_TH1_8197F                                               \
	(BIT_MASK_TXDMA_LEN_TH1_8197F << BIT_SHIFT_TXDMA_LEN_TH1_8197F)
#define BIT_CLEAR_TXDMA_LEN_TH1_8197F(x) ((x) & (~BITS_TXDMA_LEN_TH1_8197F))
#define BIT_GET_TXDMA_LEN_TH1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_TXDMA_LEN_TH1_8197F) & BIT_MASK_TXDMA_LEN_TH1_8197F)
#define BIT_SET_TXDMA_LEN_TH1_8197F(x, v)                                      \
	(BIT_CLEAR_TXDMA_LEN_TH1_8197F(x) | BIT_TXDMA_LEN_TH1_8197F(v))

/* 2 REG_RDE_LEN_TH_8197F */

#define BIT_SHIFT_RXDMA_LEN_TH0_8197F 16
#define BIT_MASK_RXDMA_LEN_TH0_8197F 0xffff
#define BIT_RXDMA_LEN_TH0_8197F(x)                                             \
	(((x) & BIT_MASK_RXDMA_LEN_TH0_8197F) << BIT_SHIFT_RXDMA_LEN_TH0_8197F)
#define BITS_RXDMA_LEN_TH0_8197F                                               \
	(BIT_MASK_RXDMA_LEN_TH0_8197F << BIT_SHIFT_RXDMA_LEN_TH0_8197F)
#define BIT_CLEAR_RXDMA_LEN_TH0_8197F(x) ((x) & (~BITS_RXDMA_LEN_TH0_8197F))
#define BIT_GET_RXDMA_LEN_TH0_8197F(x)                                         \
	(((x) >> BIT_SHIFT_RXDMA_LEN_TH0_8197F) & BIT_MASK_RXDMA_LEN_TH0_8197F)
#define BIT_SET_RXDMA_LEN_TH0_8197F(x, v)                                      \
	(BIT_CLEAR_RXDMA_LEN_TH0_8197F(x) | BIT_RXDMA_LEN_TH0_8197F(v))

#define BIT_SHIFT_RXDMA_LEN_TH1_8197F 0
#define BIT_MASK_RXDMA_LEN_TH1_8197F 0xffff
#define BIT_RXDMA_LEN_TH1_8197F(x)                                             \
	(((x) & BIT_MASK_RXDMA_LEN_TH1_8197F) << BIT_SHIFT_RXDMA_LEN_TH1_8197F)
#define BITS_RXDMA_LEN_TH1_8197F                                               \
	(BIT_MASK_RXDMA_LEN_TH1_8197F << BIT_SHIFT_RXDMA_LEN_TH1_8197F)
#define BIT_CLEAR_RXDMA_LEN_TH1_8197F(x) ((x) & (~BITS_RXDMA_LEN_TH1_8197F))
#define BIT_GET_RXDMA_LEN_TH1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_RXDMA_LEN_TH1_8197F) & BIT_MASK_RXDMA_LEN_TH1_8197F)
#define BIT_SET_RXDMA_LEN_TH1_8197F(x, v)                                      \
	(BIT_CLEAR_RXDMA_LEN_TH1_8197F(x) | BIT_RXDMA_LEN_TH1_8197F(v))

/* 2 REG_PCIE_EXEC_TIME_8197F */

#define BIT_SHIFT_COUNTER_INTERVAL_SEL_8197F 16
#define BIT_MASK_COUNTER_INTERVAL_SEL_8197F 0x3
#define BIT_COUNTER_INTERVAL_SEL_8197F(x)                                      \
	(((x) & BIT_MASK_COUNTER_INTERVAL_SEL_8197F)                           \
	 << BIT_SHIFT_COUNTER_INTERVAL_SEL_8197F)
#define BITS_COUNTER_INTERVAL_SEL_8197F                                        \
	(BIT_MASK_COUNTER_INTERVAL_SEL_8197F                                   \
	 << BIT_SHIFT_COUNTER_INTERVAL_SEL_8197F)
#define BIT_CLEAR_COUNTER_INTERVAL_SEL_8197F(x)                                \
	((x) & (~BITS_COUNTER_INTERVAL_SEL_8197F))
#define BIT_GET_COUNTER_INTERVAL_SEL_8197F(x)                                  \
	(((x) >> BIT_SHIFT_COUNTER_INTERVAL_SEL_8197F) &                       \
	 BIT_MASK_COUNTER_INTERVAL_SEL_8197F)
#define BIT_SET_COUNTER_INTERVAL_SEL_8197F(x, v)                               \
	(BIT_CLEAR_COUNTER_INTERVAL_SEL_8197F(x) |                             \
	 BIT_COUNTER_INTERVAL_SEL_8197F(v))

#define BIT_SHIFT_PCIE_TRANS_DATA_TH1_8197F 0
#define BIT_MASK_PCIE_TRANS_DATA_TH1_8197F 0xffff
#define BIT_PCIE_TRANS_DATA_TH1_8197F(x)                                       \
	(((x) & BIT_MASK_PCIE_TRANS_DATA_TH1_8197F)                            \
	 << BIT_SHIFT_PCIE_TRANS_DATA_TH1_8197F)
#define BITS_PCIE_TRANS_DATA_TH1_8197F                                         \
	(BIT_MASK_PCIE_TRANS_DATA_TH1_8197F                                    \
	 << BIT_SHIFT_PCIE_TRANS_DATA_TH1_8197F)
#define BIT_CLEAR_PCIE_TRANS_DATA_TH1_8197F(x)                                 \
	((x) & (~BITS_PCIE_TRANS_DATA_TH1_8197F))
#define BIT_GET_PCIE_TRANS_DATA_TH1_8197F(x)                                   \
	(((x) >> BIT_SHIFT_PCIE_TRANS_DATA_TH1_8197F) &                        \
	 BIT_MASK_PCIE_TRANS_DATA_TH1_8197F)
#define BIT_SET_PCIE_TRANS_DATA_TH1_8197F(x, v)                                \
	(BIT_CLEAR_PCIE_TRANS_DATA_TH1_8197F(x) |                              \
	 BIT_PCIE_TRANS_DATA_TH1_8197F(v))

/* 2 REG_FT2IMR_8197F */
#define BIT_PORT4_RX_UCMD1_UAPSD0_OK_INT_EN_8197F BIT(31)
#define BIT_PORT4_RX_UCMD0_UAPSD0_OK_INT_EN_8197F BIT(30)
#define BIT_PORT4_TRIPKT_OK_INT_EN_8197F BIT(29)
#define BIT_PORT4_RX_EOSP_OK_INT_EN_8197F BIT(28)
#define BIT_PORT3_RX_UCMD1_UAPSD0_OK_INT_EN_8197F BIT(27)
#define BIT_PORT3_RX_UCMD0_UAPSD0_OK_INT_EN_8197F BIT(26)
#define BIT_PORT3_TRIPKT_OK_INT_EN_8197F BIT(25)
#define BIT_PORT3_RX_EOSP_OK_INT_EN_8197F BIT(24)
#define BIT_PORT2_RX_UCMD1_UAPSD0_OK_INT_EN_8197F BIT(23)
#define BIT_PORT2_RX_UCMD0_UAPSD0_OK_INT_EN_8197F BIT(22)
#define BIT_PORT2_TRIPKT_OK_INT_EN_8197F BIT(21)
#define BIT_PORT2_RX_EOSP_OK_INT_EN_8197F BIT(20)
#define BIT_PORT1_RX_UCMD1_UAPSD0_OK_INT_EN_8197F BIT(19)
#define BIT_PORT1_RX_UCMD0_UAPSD0_OK_INT_EN_8197F BIT(18)
#define BIT_PORT1_TRIPKT_OK_INT_EN_8197F BIT(17)
#define BIT_PORT1_RX_EOSP_OK_INT_EN_8197F BIT(16)
#define BIT_NOA2_TSFT_BIT32_TOGGLE_INT_EN_8197F BIT(9)
#define BIT_NOA1_TSFT_BIT32_TOGGLE_INT_EN_8197F BIT(8)
#define BIT_PORT4_TX_NULL1_DONE_INT_EN_8197F BIT(7)
#define BIT_PORT4_TX_NULL0_DONE_INT_EN_8197F BIT(6)
#define BIT_PORT3_TX_NULL1_DONE_INT_EN_8197F BIT(5)
#define BIT_PORT3_TX_NULL0_DONE_INT_EN_8197F BIT(4)
#define BIT_PORT2_TX_NULL1_DONE_INT_EN_8197F BIT(3)
#define BIT_PORT2_TX_NULL0_DONE_INT_EN_8197F BIT(2)
#define BIT_PORT1_TX_NULL1_DONE_INT_EN_8197F BIT(1)
#define BIT_PORT1_TX_NULL0_DONE_INT_EN_8197F BIT(0)

/* 2 REG_FT2ISR_8197F */
#define BIT_PORT4_RX_UCMD1_UAPSD0_OK_INT_8197F BIT(31)
#define BIT_PORT4_RX_UCMD0_UAPSD0_OK_INT_8197F BIT(30)
#define BIT_PORT4_TRIPKT_OK_INT_8197F BIT(29)
#define BIT_PORT4_RX_EOSP_OK_INT_8197F BIT(28)
#define BIT_PORT3_RX_UCMD1_UAPSD0_OK_INT_8197F BIT(27)
#define BIT_PORT3_RX_UCMD0_UAPSD0_OK_INT_8197F BIT(26)
#define BIT_PORT3_TRIPKT_OK_INT_8197F BIT(25)
#define BIT_PORT3_RX_EOSP_OK_INT_8197F BIT(24)
#define BIT_PORT2_RX_UCMD1_UAPSD0_OK_INT_8197F BIT(23)
#define BIT_PORT2_RX_UCMD0_UAPSD0_OK_INT_8197F BIT(22)
#define BIT_PORT2_TRIPKT_OK_INT_8197F BIT(21)
#define BIT_PORT2_RX_EOSP_OK_INT_8197F BIT(20)
#define BIT_PORT1_RX_UCMD1_UAPSD0_OK_INT_8197F BIT(19)
#define BIT_PORT1_RX_UCMD0_UAPSD0_OK_INT_8197F BIT(18)
#define BIT_PORT1_TRIPKT_OK_INT_8197F BIT(17)
#define BIT_PORT1_RX_EOSP_OK_INT_8197F BIT(16)
#define BIT_NOA2_TSFT_BIT32_TOGGLE_INT_8197F BIT(9)
#define BIT_NOA1_TSFT_BIT32_TOGGLE_INT_8197F BIT(8)
#define BIT_PORT4_TX_NULL1_DONE_INT_8197F BIT(7)
#define BIT_PORT4_TX_NULL0_DONE_INT_8197F BIT(6)
#define BIT_PORT3_TX_NULL1_DONE_INT_8197F BIT(5)
#define BIT_PORT3_TX_NULL0_DONE_INT_8197F BIT(4)
#define BIT_PORT2_TX_NULL1_DONE_INT_8197F BIT(3)
#define BIT_PORT2_TX_NULL0_DONE_INT_8197F BIT(2)
#define BIT_PORT1_TX_NULL1_DONE_INT_8197F BIT(1)
#define BIT_PORT1_TX_NULL0_DONE_INT_8197F BIT(0)

/* 2 REG_MSG2_8197F */

#define BIT_SHIFT_FW_MSG2_8197F 0
#define BIT_MASK_FW_MSG2_8197F 0xffffffffL
#define BIT_FW_MSG2_8197F(x)                                                   \
	(((x) & BIT_MASK_FW_MSG2_8197F) << BIT_SHIFT_FW_MSG2_8197F)
#define BITS_FW_MSG2_8197F (BIT_MASK_FW_MSG2_8197F << BIT_SHIFT_FW_MSG2_8197F)
#define BIT_CLEAR_FW_MSG2_8197F(x) ((x) & (~BITS_FW_MSG2_8197F))
#define BIT_GET_FW_MSG2_8197F(x)                                               \
	(((x) >> BIT_SHIFT_FW_MSG2_8197F) & BIT_MASK_FW_MSG2_8197F)
#define BIT_SET_FW_MSG2_8197F(x, v)                                            \
	(BIT_CLEAR_FW_MSG2_8197F(x) | BIT_FW_MSG2_8197F(v))

/* 2 REG_MSG3_8197F */

#define BIT_SHIFT_FW_MSG3_8197F 0
#define BIT_MASK_FW_MSG3_8197F 0xffffffffL
#define BIT_FW_MSG3_8197F(x)                                                   \
	(((x) & BIT_MASK_FW_MSG3_8197F) << BIT_SHIFT_FW_MSG3_8197F)
#define BITS_FW_MSG3_8197F (BIT_MASK_FW_MSG3_8197F << BIT_SHIFT_FW_MSG3_8197F)
#define BIT_CLEAR_FW_MSG3_8197F(x) ((x) & (~BITS_FW_MSG3_8197F))
#define BIT_GET_FW_MSG3_8197F(x)                                               \
	(((x) >> BIT_SHIFT_FW_MSG3_8197F) & BIT_MASK_FW_MSG3_8197F)
#define BIT_SET_FW_MSG3_8197F(x, v)                                            \
	(BIT_CLEAR_FW_MSG3_8197F(x) | BIT_FW_MSG3_8197F(v))

/* 2 REG_MSG4_8197F */

#define BIT_SHIFT_FW_MSG4_8197F 0
#define BIT_MASK_FW_MSG4_8197F 0xffffffffL
#define BIT_FW_MSG4_8197F(x)                                                   \
	(((x) & BIT_MASK_FW_MSG4_8197F) << BIT_SHIFT_FW_MSG4_8197F)
#define BITS_FW_MSG4_8197F (BIT_MASK_FW_MSG4_8197F << BIT_SHIFT_FW_MSG4_8197F)
#define BIT_CLEAR_FW_MSG4_8197F(x) ((x) & (~BITS_FW_MSG4_8197F))
#define BIT_GET_FW_MSG4_8197F(x)                                               \
	(((x) >> BIT_SHIFT_FW_MSG4_8197F) & BIT_MASK_FW_MSG4_8197F)
#define BIT_SET_FW_MSG4_8197F(x, v)                                            \
	(BIT_CLEAR_FW_MSG4_8197F(x) | BIT_FW_MSG4_8197F(v))

/* 2 REG_MSG5_8197F */

#define BIT_SHIFT_FW_MSG5_8197F 0
#define BIT_MASK_FW_MSG5_8197F 0xffffffffL
#define BIT_FW_MSG5_8197F(x)                                                   \
	(((x) & BIT_MASK_FW_MSG5_8197F) << BIT_SHIFT_FW_MSG5_8197F)
#define BITS_FW_MSG5_8197F (BIT_MASK_FW_MSG5_8197F << BIT_SHIFT_FW_MSG5_8197F)
#define BIT_CLEAR_FW_MSG5_8197F(x) ((x) & (~BITS_FW_MSG5_8197F))
#define BIT_GET_FW_MSG5_8197F(x)                                               \
	(((x) >> BIT_SHIFT_FW_MSG5_8197F) & BIT_MASK_FW_MSG5_8197F)
#define BIT_SET_FW_MSG5_8197F(x, v)                                            \
	(BIT_CLEAR_FW_MSG5_8197F(x) | BIT_FW_MSG5_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_FIFOPAGE_CTRL_1_8197F */

#define BIT_SHIFT_TX_OQT_HE_FREE_SPACE_V1_8197F 16
#define BIT_MASK_TX_OQT_HE_FREE_SPACE_V1_8197F 0xff
#define BIT_TX_OQT_HE_FREE_SPACE_V1_8197F(x)                                   \
	(((x) & BIT_MASK_TX_OQT_HE_FREE_SPACE_V1_8197F)                        \
	 << BIT_SHIFT_TX_OQT_HE_FREE_SPACE_V1_8197F)
#define BITS_TX_OQT_HE_FREE_SPACE_V1_8197F                                     \
	(BIT_MASK_TX_OQT_HE_FREE_SPACE_V1_8197F                                \
	 << BIT_SHIFT_TX_OQT_HE_FREE_SPACE_V1_8197F)
#define BIT_CLEAR_TX_OQT_HE_FREE_SPACE_V1_8197F(x)                             \
	((x) & (~BITS_TX_OQT_HE_FREE_SPACE_V1_8197F))
#define BIT_GET_TX_OQT_HE_FREE_SPACE_V1_8197F(x)                               \
	(((x) >> BIT_SHIFT_TX_OQT_HE_FREE_SPACE_V1_8197F) &                    \
	 BIT_MASK_TX_OQT_HE_FREE_SPACE_V1_8197F)
#define BIT_SET_TX_OQT_HE_FREE_SPACE_V1_8197F(x, v)                            \
	(BIT_CLEAR_TX_OQT_HE_FREE_SPACE_V1_8197F(x) |                          \
	 BIT_TX_OQT_HE_FREE_SPACE_V1_8197F(v))

#define BIT_SHIFT_TX_OQT_NL_FREE_SPACE_V1_8197F 0
#define BIT_MASK_TX_OQT_NL_FREE_SPACE_V1_8197F 0xff
#define BIT_TX_OQT_NL_FREE_SPACE_V1_8197F(x)                                   \
	(((x) & BIT_MASK_TX_OQT_NL_FREE_SPACE_V1_8197F)                        \
	 << BIT_SHIFT_TX_OQT_NL_FREE_SPACE_V1_8197F)
#define BITS_TX_OQT_NL_FREE_SPACE_V1_8197F                                     \
	(BIT_MASK_TX_OQT_NL_FREE_SPACE_V1_8197F                                \
	 << BIT_SHIFT_TX_OQT_NL_FREE_SPACE_V1_8197F)
#define BIT_CLEAR_TX_OQT_NL_FREE_SPACE_V1_8197F(x)                             \
	((x) & (~BITS_TX_OQT_NL_FREE_SPACE_V1_8197F))
#define BIT_GET_TX_OQT_NL_FREE_SPACE_V1_8197F(x)                               \
	(((x) >> BIT_SHIFT_TX_OQT_NL_FREE_SPACE_V1_8197F) &                    \
	 BIT_MASK_TX_OQT_NL_FREE_SPACE_V1_8197F)
#define BIT_SET_TX_OQT_NL_FREE_SPACE_V1_8197F(x, v)                            \
	(BIT_CLEAR_TX_OQT_NL_FREE_SPACE_V1_8197F(x) |                          \
	 BIT_TX_OQT_NL_FREE_SPACE_V1_8197F(v))

/* 2 REG_FIFOPAGE_CTRL_2_8197F */
#define BIT_BCN_VALID_1_V1_8197F BIT(31)

#define BIT_SHIFT_BCN_HEAD_1_V1_8197F 16
#define BIT_MASK_BCN_HEAD_1_V1_8197F 0xfff
#define BIT_BCN_HEAD_1_V1_8197F(x)                                             \
	(((x) & BIT_MASK_BCN_HEAD_1_V1_8197F) << BIT_SHIFT_BCN_HEAD_1_V1_8197F)
#define BITS_BCN_HEAD_1_V1_8197F                                               \
	(BIT_MASK_BCN_HEAD_1_V1_8197F << BIT_SHIFT_BCN_HEAD_1_V1_8197F)
#define BIT_CLEAR_BCN_HEAD_1_V1_8197F(x) ((x) & (~BITS_BCN_HEAD_1_V1_8197F))
#define BIT_GET_BCN_HEAD_1_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_BCN_HEAD_1_V1_8197F) & BIT_MASK_BCN_HEAD_1_V1_8197F)
#define BIT_SET_BCN_HEAD_1_V1_8197F(x, v)                                      \
	(BIT_CLEAR_BCN_HEAD_1_V1_8197F(x) | BIT_BCN_HEAD_1_V1_8197F(v))

#define BIT_BCN_VALID_V1_8197F BIT(15)

#define BIT_SHIFT_BCN_HEAD_V1_8197F 0
#define BIT_MASK_BCN_HEAD_V1_8197F 0xfff
#define BIT_BCN_HEAD_V1_8197F(x)                                               \
	(((x) & BIT_MASK_BCN_HEAD_V1_8197F) << BIT_SHIFT_BCN_HEAD_V1_8197F)
#define BITS_BCN_HEAD_V1_8197F                                                 \
	(BIT_MASK_BCN_HEAD_V1_8197F << BIT_SHIFT_BCN_HEAD_V1_8197F)
#define BIT_CLEAR_BCN_HEAD_V1_8197F(x) ((x) & (~BITS_BCN_HEAD_V1_8197F))
#define BIT_GET_BCN_HEAD_V1_8197F(x)                                           \
	(((x) >> BIT_SHIFT_BCN_HEAD_V1_8197F) & BIT_MASK_BCN_HEAD_V1_8197F)
#define BIT_SET_BCN_HEAD_V1_8197F(x, v)                                        \
	(BIT_CLEAR_BCN_HEAD_V1_8197F(x) | BIT_BCN_HEAD_V1_8197F(v))

/* 2 REG_AUTO_LLT_V1_8197F */

#define BIT_SHIFT_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8197F 24
#define BIT_MASK_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8197F 0xff
#define BIT_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8197F(x)                            \
	(((x) & BIT_MASK_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8197F)                 \
	 << BIT_SHIFT_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8197F)
#define BITS_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8197F                              \
	(BIT_MASK_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8197F                         \
	 << BIT_SHIFT_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8197F)
#define BIT_CLEAR_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8197F(x)                      \
	((x) & (~BITS_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8197F))
#define BIT_GET_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8197F(x)                        \
	(((x) >> BIT_SHIFT_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8197F) &             \
	 BIT_MASK_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8197F)
#define BIT_SET_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8197F(x, v)                     \
	(BIT_CLEAR_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8197F(x) |                   \
	 BIT_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8197F(v))

#define BIT_SHIFT_LLT_FREE_PAGE_V1_8197F 8
#define BIT_MASK_LLT_FREE_PAGE_V1_8197F 0xffff
#define BIT_LLT_FREE_PAGE_V1_8197F(x)                                          \
	(((x) & BIT_MASK_LLT_FREE_PAGE_V1_8197F)                               \
	 << BIT_SHIFT_LLT_FREE_PAGE_V1_8197F)
#define BITS_LLT_FREE_PAGE_V1_8197F                                            \
	(BIT_MASK_LLT_FREE_PAGE_V1_8197F << BIT_SHIFT_LLT_FREE_PAGE_V1_8197F)
#define BIT_CLEAR_LLT_FREE_PAGE_V1_8197F(x)                                    \
	((x) & (~BITS_LLT_FREE_PAGE_V1_8197F))
#define BIT_GET_LLT_FREE_PAGE_V1_8197F(x)                                      \
	(((x) >> BIT_SHIFT_LLT_FREE_PAGE_V1_8197F) &                           \
	 BIT_MASK_LLT_FREE_PAGE_V1_8197F)
#define BIT_SET_LLT_FREE_PAGE_V1_8197F(x, v)                                   \
	(BIT_CLEAR_LLT_FREE_PAGE_V1_8197F(x) | BIT_LLT_FREE_PAGE_V1_8197F(v))

#define BIT_SHIFT_BLK_DESC_NUM_8197F 4
#define BIT_MASK_BLK_DESC_NUM_8197F 0xf
#define BIT_BLK_DESC_NUM_8197F(x)                                              \
	(((x) & BIT_MASK_BLK_DESC_NUM_8197F) << BIT_SHIFT_BLK_DESC_NUM_8197F)
#define BITS_BLK_DESC_NUM_8197F                                                \
	(BIT_MASK_BLK_DESC_NUM_8197F << BIT_SHIFT_BLK_DESC_NUM_8197F)
#define BIT_CLEAR_BLK_DESC_NUM_8197F(x) ((x) & (~BITS_BLK_DESC_NUM_8197F))
#define BIT_GET_BLK_DESC_NUM_8197F(x)                                          \
	(((x) >> BIT_SHIFT_BLK_DESC_NUM_8197F) & BIT_MASK_BLK_DESC_NUM_8197F)
#define BIT_SET_BLK_DESC_NUM_8197F(x, v)                                       \
	(BIT_CLEAR_BLK_DESC_NUM_8197F(x) | BIT_BLK_DESC_NUM_8197F(v))

#define BIT_R_BCN_HEAD_SEL_8197F BIT(3)
#define BIT_R_EN_BCN_SW_HEAD_SEL_8197F BIT(2)
#define BIT_LLT_DBG_SEL_8197F BIT(1)
#define BIT_AUTO_INIT_LLT_V1_8197F BIT(0)

/* 2 REG_TXDMA_OFFSET_CHK_8197F */
#define BIT_EM_CHKSUM_FIN_8197F BIT(31)
#define BIT_EMN_PCIE_DMA_MOD_8197F BIT(30)
#define BIT_EN_TXQUE_CLR_8197F BIT(29)
#define BIT_EN_PCIE_FIFO_MODE_8197F BIT(28)

#define BIT_SHIFT_PG_UNDER_TH_V1_8197F 16
#define BIT_MASK_PG_UNDER_TH_V1_8197F 0xfff
#define BIT_PG_UNDER_TH_V1_8197F(x)                                            \
	(((x) & BIT_MASK_PG_UNDER_TH_V1_8197F)                                 \
	 << BIT_SHIFT_PG_UNDER_TH_V1_8197F)
#define BITS_PG_UNDER_TH_V1_8197F                                              \
	(BIT_MASK_PG_UNDER_TH_V1_8197F << BIT_SHIFT_PG_UNDER_TH_V1_8197F)
#define BIT_CLEAR_PG_UNDER_TH_V1_8197F(x) ((x) & (~BITS_PG_UNDER_TH_V1_8197F))
#define BIT_GET_PG_UNDER_TH_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_PG_UNDER_TH_V1_8197F) &                             \
	 BIT_MASK_PG_UNDER_TH_V1_8197F)
#define BIT_SET_PG_UNDER_TH_V1_8197F(x, v)                                     \
	(BIT_CLEAR_PG_UNDER_TH_V1_8197F(x) | BIT_PG_UNDER_TH_V1_8197F(v))

#define BIT_EN_RESET_RESTORE_H2C_8197F BIT(15)
#define BIT_SDIO_TDE_FINISH_8197F BIT(14)
#define BIT_SDIO_TXDESC_CHKSUM_EN_8197F BIT(13)
#define BIT_RST_RDPTR_8197F BIT(12)
#define BIT_RST_WRPTR_8197F BIT(11)
#define BIT_CHK_PG_TH_EN_8197F BIT(10)
#define BIT_DROP_DATA_EN_8197F BIT(9)
#define BIT_CHECK_OFFSET_EN_8197F BIT(8)

#define BIT_SHIFT_CHECK_OFFSET_8197F 0
#define BIT_MASK_CHECK_OFFSET_8197F 0xff
#define BIT_CHECK_OFFSET_8197F(x)                                              \
	(((x) & BIT_MASK_CHECK_OFFSET_8197F) << BIT_SHIFT_CHECK_OFFSET_8197F)
#define BITS_CHECK_OFFSET_8197F                                                \
	(BIT_MASK_CHECK_OFFSET_8197F << BIT_SHIFT_CHECK_OFFSET_8197F)
#define BIT_CLEAR_CHECK_OFFSET_8197F(x) ((x) & (~BITS_CHECK_OFFSET_8197F))
#define BIT_GET_CHECK_OFFSET_8197F(x)                                          \
	(((x) >> BIT_SHIFT_CHECK_OFFSET_8197F) & BIT_MASK_CHECK_OFFSET_8197F)
#define BIT_SET_CHECK_OFFSET_8197F(x, v)                                       \
	(BIT_CLEAR_CHECK_OFFSET_8197F(x) | BIT_CHECK_OFFSET_8197F(v))

/* 2 REG_TXDMA_STATUS_8197F */
#define BIT_HI_OQT_UDN_8197F BIT(17)
#define BIT_HI_OQT_OVF_8197F BIT(16)
#define BIT_PAYLOAD_CHKSUM_ERR_8197F BIT(15)
#define BIT_PAYLOAD_UDN_8197F BIT(14)
#define BIT_PAYLOAD_OVF_8197F BIT(13)
#define BIT_DSC_CHKSUM_FAIL_8197F BIT(12)
#define BIT_UNKNOWN_QSEL_8197F BIT(11)
#define BIT_EP_QSEL_DIFF_8197F BIT(10)
#define BIT_TX_OFFS_UNMATCH_8197F BIT(9)
#define BIT_TXOQT_UDN_8197F BIT(8)
#define BIT_TXOQT_OVF_8197F BIT(7)
#define BIT_TXDMA_SFF_UDN_8197F BIT(6)
#define BIT_TXDMA_SFF_OVF_8197F BIT(5)
#define BIT_LLT_NULL_PG_8197F BIT(4)
#define BIT_PAGE_UDN_8197F BIT(3)
#define BIT_PAGE_OVF_8197F BIT(2)
#define BIT_TXFF_PG_UDN_8197F BIT(1)
#define BIT_TXFF_PG_OVF_8197F BIT(0)

/* 2 REG_TX_DMA_DBG_8197F */

/* 2 REG_TQPNT1_8197F */

#define BIT_SHIFT_HPQ_HIGH_TH_V1_8197F 16
#define BIT_MASK_HPQ_HIGH_TH_V1_8197F 0xfff
#define BIT_HPQ_HIGH_TH_V1_8197F(x)                                            \
	(((x) & BIT_MASK_HPQ_HIGH_TH_V1_8197F)                                 \
	 << BIT_SHIFT_HPQ_HIGH_TH_V1_8197F)
#define BITS_HPQ_HIGH_TH_V1_8197F                                              \
	(BIT_MASK_HPQ_HIGH_TH_V1_8197F << BIT_SHIFT_HPQ_HIGH_TH_V1_8197F)
#define BIT_CLEAR_HPQ_HIGH_TH_V1_8197F(x) ((x) & (~BITS_HPQ_HIGH_TH_V1_8197F))
#define BIT_GET_HPQ_HIGH_TH_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HPQ_HIGH_TH_V1_8197F) &                             \
	 BIT_MASK_HPQ_HIGH_TH_V1_8197F)
#define BIT_SET_HPQ_HIGH_TH_V1_8197F(x, v)                                     \
	(BIT_CLEAR_HPQ_HIGH_TH_V1_8197F(x) | BIT_HPQ_HIGH_TH_V1_8197F(v))

#define BIT_SHIFT_HPQ_LOW_TH_V1_8197F 0
#define BIT_MASK_HPQ_LOW_TH_V1_8197F 0xfff
#define BIT_HPQ_LOW_TH_V1_8197F(x)                                             \
	(((x) & BIT_MASK_HPQ_LOW_TH_V1_8197F) << BIT_SHIFT_HPQ_LOW_TH_V1_8197F)
#define BITS_HPQ_LOW_TH_V1_8197F                                               \
	(BIT_MASK_HPQ_LOW_TH_V1_8197F << BIT_SHIFT_HPQ_LOW_TH_V1_8197F)
#define BIT_CLEAR_HPQ_LOW_TH_V1_8197F(x) ((x) & (~BITS_HPQ_LOW_TH_V1_8197F))
#define BIT_GET_HPQ_LOW_TH_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_HPQ_LOW_TH_V1_8197F) & BIT_MASK_HPQ_LOW_TH_V1_8197F)
#define BIT_SET_HPQ_LOW_TH_V1_8197F(x, v)                                      \
	(BIT_CLEAR_HPQ_LOW_TH_V1_8197F(x) | BIT_HPQ_LOW_TH_V1_8197F(v))

/* 2 REG_TQPNT2_8197F */

#define BIT_SHIFT_NPQ_HIGH_TH_V1_8197F 16
#define BIT_MASK_NPQ_HIGH_TH_V1_8197F 0xfff
#define BIT_NPQ_HIGH_TH_V1_8197F(x)                                            \
	(((x) & BIT_MASK_NPQ_HIGH_TH_V1_8197F)                                 \
	 << BIT_SHIFT_NPQ_HIGH_TH_V1_8197F)
#define BITS_NPQ_HIGH_TH_V1_8197F                                              \
	(BIT_MASK_NPQ_HIGH_TH_V1_8197F << BIT_SHIFT_NPQ_HIGH_TH_V1_8197F)
#define BIT_CLEAR_NPQ_HIGH_TH_V1_8197F(x) ((x) & (~BITS_NPQ_HIGH_TH_V1_8197F))
#define BIT_GET_NPQ_HIGH_TH_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_NPQ_HIGH_TH_V1_8197F) &                             \
	 BIT_MASK_NPQ_HIGH_TH_V1_8197F)
#define BIT_SET_NPQ_HIGH_TH_V1_8197F(x, v)                                     \
	(BIT_CLEAR_NPQ_HIGH_TH_V1_8197F(x) | BIT_NPQ_HIGH_TH_V1_8197F(v))

#define BIT_SHIFT_NPQ_LOW_TH_V1_8197F 0
#define BIT_MASK_NPQ_LOW_TH_V1_8197F 0xfff
#define BIT_NPQ_LOW_TH_V1_8197F(x)                                             \
	(((x) & BIT_MASK_NPQ_LOW_TH_V1_8197F) << BIT_SHIFT_NPQ_LOW_TH_V1_8197F)
#define BITS_NPQ_LOW_TH_V1_8197F                                               \
	(BIT_MASK_NPQ_LOW_TH_V1_8197F << BIT_SHIFT_NPQ_LOW_TH_V1_8197F)
#define BIT_CLEAR_NPQ_LOW_TH_V1_8197F(x) ((x) & (~BITS_NPQ_LOW_TH_V1_8197F))
#define BIT_GET_NPQ_LOW_TH_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_NPQ_LOW_TH_V1_8197F) & BIT_MASK_NPQ_LOW_TH_V1_8197F)
#define BIT_SET_NPQ_LOW_TH_V1_8197F(x, v)                                      \
	(BIT_CLEAR_NPQ_LOW_TH_V1_8197F(x) | BIT_NPQ_LOW_TH_V1_8197F(v))

/* 2 REG_TQPNT3_8197F */

#define BIT_SHIFT_LPQ_HIGH_TH_V1_8197F 16
#define BIT_MASK_LPQ_HIGH_TH_V1_8197F 0xfff
#define BIT_LPQ_HIGH_TH_V1_8197F(x)                                            \
	(((x) & BIT_MASK_LPQ_HIGH_TH_V1_8197F)                                 \
	 << BIT_SHIFT_LPQ_HIGH_TH_V1_8197F)
#define BITS_LPQ_HIGH_TH_V1_8197F                                              \
	(BIT_MASK_LPQ_HIGH_TH_V1_8197F << BIT_SHIFT_LPQ_HIGH_TH_V1_8197F)
#define BIT_CLEAR_LPQ_HIGH_TH_V1_8197F(x) ((x) & (~BITS_LPQ_HIGH_TH_V1_8197F))
#define BIT_GET_LPQ_HIGH_TH_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_LPQ_HIGH_TH_V1_8197F) &                             \
	 BIT_MASK_LPQ_HIGH_TH_V1_8197F)
#define BIT_SET_LPQ_HIGH_TH_V1_8197F(x, v)                                     \
	(BIT_CLEAR_LPQ_HIGH_TH_V1_8197F(x) | BIT_LPQ_HIGH_TH_V1_8197F(v))

#define BIT_SHIFT_LPQ_LOW_TH_V1_8197F 0
#define BIT_MASK_LPQ_LOW_TH_V1_8197F 0xfff
#define BIT_LPQ_LOW_TH_V1_8197F(x)                                             \
	(((x) & BIT_MASK_LPQ_LOW_TH_V1_8197F) << BIT_SHIFT_LPQ_LOW_TH_V1_8197F)
#define BITS_LPQ_LOW_TH_V1_8197F                                               \
	(BIT_MASK_LPQ_LOW_TH_V1_8197F << BIT_SHIFT_LPQ_LOW_TH_V1_8197F)
#define BIT_CLEAR_LPQ_LOW_TH_V1_8197F(x) ((x) & (~BITS_LPQ_LOW_TH_V1_8197F))
#define BIT_GET_LPQ_LOW_TH_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_LPQ_LOW_TH_V1_8197F) & BIT_MASK_LPQ_LOW_TH_V1_8197F)
#define BIT_SET_LPQ_LOW_TH_V1_8197F(x, v)                                      \
	(BIT_CLEAR_LPQ_LOW_TH_V1_8197F(x) | BIT_LPQ_LOW_TH_V1_8197F(v))

/* 2 REG_TQPNT4_8197F */

#define BIT_SHIFT_EXQ_HIGH_TH_V1_8197F 16
#define BIT_MASK_EXQ_HIGH_TH_V1_8197F 0xfff
#define BIT_EXQ_HIGH_TH_V1_8197F(x)                                            \
	(((x) & BIT_MASK_EXQ_HIGH_TH_V1_8197F)                                 \
	 << BIT_SHIFT_EXQ_HIGH_TH_V1_8197F)
#define BITS_EXQ_HIGH_TH_V1_8197F                                              \
	(BIT_MASK_EXQ_HIGH_TH_V1_8197F << BIT_SHIFT_EXQ_HIGH_TH_V1_8197F)
#define BIT_CLEAR_EXQ_HIGH_TH_V1_8197F(x) ((x) & (~BITS_EXQ_HIGH_TH_V1_8197F))
#define BIT_GET_EXQ_HIGH_TH_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_EXQ_HIGH_TH_V1_8197F) &                             \
	 BIT_MASK_EXQ_HIGH_TH_V1_8197F)
#define BIT_SET_EXQ_HIGH_TH_V1_8197F(x, v)                                     \
	(BIT_CLEAR_EXQ_HIGH_TH_V1_8197F(x) | BIT_EXQ_HIGH_TH_V1_8197F(v))

#define BIT_SHIFT_EXQ_LOW_TH_V1_8197F 0
#define BIT_MASK_EXQ_LOW_TH_V1_8197F 0xfff
#define BIT_EXQ_LOW_TH_V1_8197F(x)                                             \
	(((x) & BIT_MASK_EXQ_LOW_TH_V1_8197F) << BIT_SHIFT_EXQ_LOW_TH_V1_8197F)
#define BITS_EXQ_LOW_TH_V1_8197F                                               \
	(BIT_MASK_EXQ_LOW_TH_V1_8197F << BIT_SHIFT_EXQ_LOW_TH_V1_8197F)
#define BIT_CLEAR_EXQ_LOW_TH_V1_8197F(x) ((x) & (~BITS_EXQ_LOW_TH_V1_8197F))
#define BIT_GET_EXQ_LOW_TH_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_EXQ_LOW_TH_V1_8197F) & BIT_MASK_EXQ_LOW_TH_V1_8197F)
#define BIT_SET_EXQ_LOW_TH_V1_8197F(x, v)                                      \
	(BIT_CLEAR_EXQ_LOW_TH_V1_8197F(x) | BIT_EXQ_LOW_TH_V1_8197F(v))

/* 2 REG_RQPN_CTRL_1_8197F */

#define BIT_SHIFT_TXPKTNUM_H_8197F 16
#define BIT_MASK_TXPKTNUM_H_8197F 0xffff
#define BIT_TXPKTNUM_H_8197F(x)                                                \
	(((x) & BIT_MASK_TXPKTNUM_H_8197F) << BIT_SHIFT_TXPKTNUM_H_8197F)
#define BITS_TXPKTNUM_H_8197F                                                  \
	(BIT_MASK_TXPKTNUM_H_8197F << BIT_SHIFT_TXPKTNUM_H_8197F)
#define BIT_CLEAR_TXPKTNUM_H_8197F(x) ((x) & (~BITS_TXPKTNUM_H_8197F))
#define BIT_GET_TXPKTNUM_H_8197F(x)                                            \
	(((x) >> BIT_SHIFT_TXPKTNUM_H_8197F) & BIT_MASK_TXPKTNUM_H_8197F)
#define BIT_SET_TXPKTNUM_H_8197F(x, v)                                         \
	(BIT_CLEAR_TXPKTNUM_H_8197F(x) | BIT_TXPKTNUM_H_8197F(v))

#define BIT_SHIFT_TXPKTNUM_H_V1_8197F 0
#define BIT_MASK_TXPKTNUM_H_V1_8197F 0xffff
#define BIT_TXPKTNUM_H_V1_8197F(x)                                             \
	(((x) & BIT_MASK_TXPKTNUM_H_V1_8197F) << BIT_SHIFT_TXPKTNUM_H_V1_8197F)
#define BITS_TXPKTNUM_H_V1_8197F                                               \
	(BIT_MASK_TXPKTNUM_H_V1_8197F << BIT_SHIFT_TXPKTNUM_H_V1_8197F)
#define BIT_CLEAR_TXPKTNUM_H_V1_8197F(x) ((x) & (~BITS_TXPKTNUM_H_V1_8197F))
#define BIT_GET_TXPKTNUM_H_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_TXPKTNUM_H_V1_8197F) & BIT_MASK_TXPKTNUM_H_V1_8197F)
#define BIT_SET_TXPKTNUM_H_V1_8197F(x, v)                                      \
	(BIT_CLEAR_TXPKTNUM_H_V1_8197F(x) | BIT_TXPKTNUM_H_V1_8197F(v))

/* 2 REG_RQPN_CTRL_2_8197F */
#define BIT_LD_RQPN_8197F BIT(31)
#define BIT_EXQ_PUBLIC_DIS_V1_8197F BIT(19)
#define BIT_NPQ_PUBLIC_DIS_V1_8197F BIT(18)
#define BIT_LPQ_PUBLIC_DIS_V1_8197F BIT(17)
#define BIT_HPQ_PUBLIC_DIS_V1_8197F BIT(16)

/* 2 REG_FIFOPAGE_INFO_1_8197F */

#define BIT_SHIFT_HPQ_AVAL_PG_V1_8197F 16
#define BIT_MASK_HPQ_AVAL_PG_V1_8197F 0xfff
#define BIT_HPQ_AVAL_PG_V1_8197F(x)                                            \
	(((x) & BIT_MASK_HPQ_AVAL_PG_V1_8197F)                                 \
	 << BIT_SHIFT_HPQ_AVAL_PG_V1_8197F)
#define BITS_HPQ_AVAL_PG_V1_8197F                                              \
	(BIT_MASK_HPQ_AVAL_PG_V1_8197F << BIT_SHIFT_HPQ_AVAL_PG_V1_8197F)
#define BIT_CLEAR_HPQ_AVAL_PG_V1_8197F(x) ((x) & (~BITS_HPQ_AVAL_PG_V1_8197F))
#define BIT_GET_HPQ_AVAL_PG_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HPQ_AVAL_PG_V1_8197F) &                             \
	 BIT_MASK_HPQ_AVAL_PG_V1_8197F)
#define BIT_SET_HPQ_AVAL_PG_V1_8197F(x, v)                                     \
	(BIT_CLEAR_HPQ_AVAL_PG_V1_8197F(x) | BIT_HPQ_AVAL_PG_V1_8197F(v))

#define BIT_SHIFT_HPQ_V1_8197F 0
#define BIT_MASK_HPQ_V1_8197F 0xfff
#define BIT_HPQ_V1_8197F(x)                                                    \
	(((x) & BIT_MASK_HPQ_V1_8197F) << BIT_SHIFT_HPQ_V1_8197F)
#define BITS_HPQ_V1_8197F (BIT_MASK_HPQ_V1_8197F << BIT_SHIFT_HPQ_V1_8197F)
#define BIT_CLEAR_HPQ_V1_8197F(x) ((x) & (~BITS_HPQ_V1_8197F))
#define BIT_GET_HPQ_V1_8197F(x)                                                \
	(((x) >> BIT_SHIFT_HPQ_V1_8197F) & BIT_MASK_HPQ_V1_8197F)
#define BIT_SET_HPQ_V1_8197F(x, v)                                             \
	(BIT_CLEAR_HPQ_V1_8197F(x) | BIT_HPQ_V1_8197F(v))

/* 2 REG_FIFOPAGE_INFO_2_8197F */

#define BIT_SHIFT_LPQ_AVAL_PG_V1_8197F 16
#define BIT_MASK_LPQ_AVAL_PG_V1_8197F 0xfff
#define BIT_LPQ_AVAL_PG_V1_8197F(x)                                            \
	(((x) & BIT_MASK_LPQ_AVAL_PG_V1_8197F)                                 \
	 << BIT_SHIFT_LPQ_AVAL_PG_V1_8197F)
#define BITS_LPQ_AVAL_PG_V1_8197F                                              \
	(BIT_MASK_LPQ_AVAL_PG_V1_8197F << BIT_SHIFT_LPQ_AVAL_PG_V1_8197F)
#define BIT_CLEAR_LPQ_AVAL_PG_V1_8197F(x) ((x) & (~BITS_LPQ_AVAL_PG_V1_8197F))
#define BIT_GET_LPQ_AVAL_PG_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_LPQ_AVAL_PG_V1_8197F) &                             \
	 BIT_MASK_LPQ_AVAL_PG_V1_8197F)
#define BIT_SET_LPQ_AVAL_PG_V1_8197F(x, v)                                     \
	(BIT_CLEAR_LPQ_AVAL_PG_V1_8197F(x) | BIT_LPQ_AVAL_PG_V1_8197F(v))

#define BIT_SHIFT_LPQ_V1_8197F 0
#define BIT_MASK_LPQ_V1_8197F 0xfff
#define BIT_LPQ_V1_8197F(x)                                                    \
	(((x) & BIT_MASK_LPQ_V1_8197F) << BIT_SHIFT_LPQ_V1_8197F)
#define BITS_LPQ_V1_8197F (BIT_MASK_LPQ_V1_8197F << BIT_SHIFT_LPQ_V1_8197F)
#define BIT_CLEAR_LPQ_V1_8197F(x) ((x) & (~BITS_LPQ_V1_8197F))
#define BIT_GET_LPQ_V1_8197F(x)                                                \
	(((x) >> BIT_SHIFT_LPQ_V1_8197F) & BIT_MASK_LPQ_V1_8197F)
#define BIT_SET_LPQ_V1_8197F(x, v)                                             \
	(BIT_CLEAR_LPQ_V1_8197F(x) | BIT_LPQ_V1_8197F(v))

/* 2 REG_FIFOPAGE_INFO_3_8197F */

#define BIT_SHIFT_NPQ_AVAL_PG_V1_8197F 16
#define BIT_MASK_NPQ_AVAL_PG_V1_8197F 0xfff
#define BIT_NPQ_AVAL_PG_V1_8197F(x)                                            \
	(((x) & BIT_MASK_NPQ_AVAL_PG_V1_8197F)                                 \
	 << BIT_SHIFT_NPQ_AVAL_PG_V1_8197F)
#define BITS_NPQ_AVAL_PG_V1_8197F                                              \
	(BIT_MASK_NPQ_AVAL_PG_V1_8197F << BIT_SHIFT_NPQ_AVAL_PG_V1_8197F)
#define BIT_CLEAR_NPQ_AVAL_PG_V1_8197F(x) ((x) & (~BITS_NPQ_AVAL_PG_V1_8197F))
#define BIT_GET_NPQ_AVAL_PG_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_NPQ_AVAL_PG_V1_8197F) &                             \
	 BIT_MASK_NPQ_AVAL_PG_V1_8197F)
#define BIT_SET_NPQ_AVAL_PG_V1_8197F(x, v)                                     \
	(BIT_CLEAR_NPQ_AVAL_PG_V1_8197F(x) | BIT_NPQ_AVAL_PG_V1_8197F(v))

#define BIT_SHIFT_NPQ_V1_8197F 0
#define BIT_MASK_NPQ_V1_8197F 0xfff
#define BIT_NPQ_V1_8197F(x)                                                    \
	(((x) & BIT_MASK_NPQ_V1_8197F) << BIT_SHIFT_NPQ_V1_8197F)
#define BITS_NPQ_V1_8197F (BIT_MASK_NPQ_V1_8197F << BIT_SHIFT_NPQ_V1_8197F)
#define BIT_CLEAR_NPQ_V1_8197F(x) ((x) & (~BITS_NPQ_V1_8197F))
#define BIT_GET_NPQ_V1_8197F(x)                                                \
	(((x) >> BIT_SHIFT_NPQ_V1_8197F) & BIT_MASK_NPQ_V1_8197F)
#define BIT_SET_NPQ_V1_8197F(x, v)                                             \
	(BIT_CLEAR_NPQ_V1_8197F(x) | BIT_NPQ_V1_8197F(v))

/* 2 REG_FIFOPAGE_INFO_4_8197F */

#define BIT_SHIFT_EXQ_AVAL_PG_V1_8197F 16
#define BIT_MASK_EXQ_AVAL_PG_V1_8197F 0xfff
#define BIT_EXQ_AVAL_PG_V1_8197F(x)                                            \
	(((x) & BIT_MASK_EXQ_AVAL_PG_V1_8197F)                                 \
	 << BIT_SHIFT_EXQ_AVAL_PG_V1_8197F)
#define BITS_EXQ_AVAL_PG_V1_8197F                                              \
	(BIT_MASK_EXQ_AVAL_PG_V1_8197F << BIT_SHIFT_EXQ_AVAL_PG_V1_8197F)
#define BIT_CLEAR_EXQ_AVAL_PG_V1_8197F(x) ((x) & (~BITS_EXQ_AVAL_PG_V1_8197F))
#define BIT_GET_EXQ_AVAL_PG_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_EXQ_AVAL_PG_V1_8197F) &                             \
	 BIT_MASK_EXQ_AVAL_PG_V1_8197F)
#define BIT_SET_EXQ_AVAL_PG_V1_8197F(x, v)                                     \
	(BIT_CLEAR_EXQ_AVAL_PG_V1_8197F(x) | BIT_EXQ_AVAL_PG_V1_8197F(v))

#define BIT_SHIFT_EXQ_V1_8197F 0
#define BIT_MASK_EXQ_V1_8197F 0xfff
#define BIT_EXQ_V1_8197F(x)                                                    \
	(((x) & BIT_MASK_EXQ_V1_8197F) << BIT_SHIFT_EXQ_V1_8197F)
#define BITS_EXQ_V1_8197F (BIT_MASK_EXQ_V1_8197F << BIT_SHIFT_EXQ_V1_8197F)
#define BIT_CLEAR_EXQ_V1_8197F(x) ((x) & (~BITS_EXQ_V1_8197F))
#define BIT_GET_EXQ_V1_8197F(x)                                                \
	(((x) >> BIT_SHIFT_EXQ_V1_8197F) & BIT_MASK_EXQ_V1_8197F)
#define BIT_SET_EXQ_V1_8197F(x, v)                                             \
	(BIT_CLEAR_EXQ_V1_8197F(x) | BIT_EXQ_V1_8197F(v))

/* 2 REG_FIFOPAGE_INFO_5_8197F */

#define BIT_SHIFT_PUBQ_AVAL_PG_V1_8197F 16
#define BIT_MASK_PUBQ_AVAL_PG_V1_8197F 0xfff
#define BIT_PUBQ_AVAL_PG_V1_8197F(x)                                           \
	(((x) & BIT_MASK_PUBQ_AVAL_PG_V1_8197F)                                \
	 << BIT_SHIFT_PUBQ_AVAL_PG_V1_8197F)
#define BITS_PUBQ_AVAL_PG_V1_8197F                                             \
	(BIT_MASK_PUBQ_AVAL_PG_V1_8197F << BIT_SHIFT_PUBQ_AVAL_PG_V1_8197F)
#define BIT_CLEAR_PUBQ_AVAL_PG_V1_8197F(x) ((x) & (~BITS_PUBQ_AVAL_PG_V1_8197F))
#define BIT_GET_PUBQ_AVAL_PG_V1_8197F(x)                                       \
	(((x) >> BIT_SHIFT_PUBQ_AVAL_PG_V1_8197F) &                            \
	 BIT_MASK_PUBQ_AVAL_PG_V1_8197F)
#define BIT_SET_PUBQ_AVAL_PG_V1_8197F(x, v)                                    \
	(BIT_CLEAR_PUBQ_AVAL_PG_V1_8197F(x) | BIT_PUBQ_AVAL_PG_V1_8197F(v))

#define BIT_SHIFT_PUBQ_V1_8197F 0
#define BIT_MASK_PUBQ_V1_8197F 0xfff
#define BIT_PUBQ_V1_8197F(x)                                                   \
	(((x) & BIT_MASK_PUBQ_V1_8197F) << BIT_SHIFT_PUBQ_V1_8197F)
#define BITS_PUBQ_V1_8197F (BIT_MASK_PUBQ_V1_8197F << BIT_SHIFT_PUBQ_V1_8197F)
#define BIT_CLEAR_PUBQ_V1_8197F(x) ((x) & (~BITS_PUBQ_V1_8197F))
#define BIT_GET_PUBQ_V1_8197F(x)                                               \
	(((x) >> BIT_SHIFT_PUBQ_V1_8197F) & BIT_MASK_PUBQ_V1_8197F)
#define BIT_SET_PUBQ_V1_8197F(x, v)                                            \
	(BIT_CLEAR_PUBQ_V1_8197F(x) | BIT_PUBQ_V1_8197F(v))

/* 2 REG_H2C_HEAD_8197F */

#define BIT_SHIFT_H2C_HEAD_8197F 0
#define BIT_MASK_H2C_HEAD_8197F 0x3ffff
#define BIT_H2C_HEAD_8197F(x)                                                  \
	(((x) & BIT_MASK_H2C_HEAD_8197F) << BIT_SHIFT_H2C_HEAD_8197F)
#define BITS_H2C_HEAD_8197F                                                    \
	(BIT_MASK_H2C_HEAD_8197F << BIT_SHIFT_H2C_HEAD_8197F)
#define BIT_CLEAR_H2C_HEAD_8197F(x) ((x) & (~BITS_H2C_HEAD_8197F))
#define BIT_GET_H2C_HEAD_8197F(x)                                              \
	(((x) >> BIT_SHIFT_H2C_HEAD_8197F) & BIT_MASK_H2C_HEAD_8197F)
#define BIT_SET_H2C_HEAD_8197F(x, v)                                           \
	(BIT_CLEAR_H2C_HEAD_8197F(x) | BIT_H2C_HEAD_8197F(v))

/* 2 REG_H2C_TAIL_8197F */

#define BIT_SHIFT_H2C_TAIL_8197F 0
#define BIT_MASK_H2C_TAIL_8197F 0x3ffff
#define BIT_H2C_TAIL_8197F(x)                                                  \
	(((x) & BIT_MASK_H2C_TAIL_8197F) << BIT_SHIFT_H2C_TAIL_8197F)
#define BITS_H2C_TAIL_8197F                                                    \
	(BIT_MASK_H2C_TAIL_8197F << BIT_SHIFT_H2C_TAIL_8197F)
#define BIT_CLEAR_H2C_TAIL_8197F(x) ((x) & (~BITS_H2C_TAIL_8197F))
#define BIT_GET_H2C_TAIL_8197F(x)                                              \
	(((x) >> BIT_SHIFT_H2C_TAIL_8197F) & BIT_MASK_H2C_TAIL_8197F)
#define BIT_SET_H2C_TAIL_8197F(x, v)                                           \
	(BIT_CLEAR_H2C_TAIL_8197F(x) | BIT_H2C_TAIL_8197F(v))

/* 2 REG_H2C_READ_ADDR_8197F */

#define BIT_SHIFT_H2C_READ_ADDR_8197F 0
#define BIT_MASK_H2C_READ_ADDR_8197F 0x3ffff
#define BIT_H2C_READ_ADDR_8197F(x)                                             \
	(((x) & BIT_MASK_H2C_READ_ADDR_8197F) << BIT_SHIFT_H2C_READ_ADDR_8197F)
#define BITS_H2C_READ_ADDR_8197F                                               \
	(BIT_MASK_H2C_READ_ADDR_8197F << BIT_SHIFT_H2C_READ_ADDR_8197F)
#define BIT_CLEAR_H2C_READ_ADDR_8197F(x) ((x) & (~BITS_H2C_READ_ADDR_8197F))
#define BIT_GET_H2C_READ_ADDR_8197F(x)                                         \
	(((x) >> BIT_SHIFT_H2C_READ_ADDR_8197F) & BIT_MASK_H2C_READ_ADDR_8197F)
#define BIT_SET_H2C_READ_ADDR_8197F(x, v)                                      \
	(BIT_CLEAR_H2C_READ_ADDR_8197F(x) | BIT_H2C_READ_ADDR_8197F(v))

/* 2 REG_H2C_WR_ADDR_8197F */

#define BIT_SHIFT_H2C_WR_ADDR_8197F 0
#define BIT_MASK_H2C_WR_ADDR_8197F 0x3ffff
#define BIT_H2C_WR_ADDR_8197F(x)                                               \
	(((x) & BIT_MASK_H2C_WR_ADDR_8197F) << BIT_SHIFT_H2C_WR_ADDR_8197F)
#define BITS_H2C_WR_ADDR_8197F                                                 \
	(BIT_MASK_H2C_WR_ADDR_8197F << BIT_SHIFT_H2C_WR_ADDR_8197F)
#define BIT_CLEAR_H2C_WR_ADDR_8197F(x) ((x) & (~BITS_H2C_WR_ADDR_8197F))
#define BIT_GET_H2C_WR_ADDR_8197F(x)                                           \
	(((x) >> BIT_SHIFT_H2C_WR_ADDR_8197F) & BIT_MASK_H2C_WR_ADDR_8197F)
#define BIT_SET_H2C_WR_ADDR_8197F(x, v)                                        \
	(BIT_CLEAR_H2C_WR_ADDR_8197F(x) | BIT_H2C_WR_ADDR_8197F(v))

/* 2 REG_H2C_INFO_8197F */
#define BIT_EXQ_EN_PUBLIC_LIMIT_8197F BIT(11)
#define BIT_NPQ_EN_PUBLIC_LIMIT_8197F BIT(10)
#define BIT_LPQ_EN_PUBLIC_LIMIT_8197F BIT(9)
#define BIT_HPQ_EN_PUBLIC_LIMIT_8197F BIT(8)
#define BIT_H2C_SPACE_VLD_8197F BIT(3)
#define BIT_H2C_WR_ADDR_RST_8197F BIT(2)

#define BIT_SHIFT_H2C_LEN_SEL_8197F 0
#define BIT_MASK_H2C_LEN_SEL_8197F 0x3
#define BIT_H2C_LEN_SEL_8197F(x)                                               \
	(((x) & BIT_MASK_H2C_LEN_SEL_8197F) << BIT_SHIFT_H2C_LEN_SEL_8197F)
#define BITS_H2C_LEN_SEL_8197F                                                 \
	(BIT_MASK_H2C_LEN_SEL_8197F << BIT_SHIFT_H2C_LEN_SEL_8197F)
#define BIT_CLEAR_H2C_LEN_SEL_8197F(x) ((x) & (~BITS_H2C_LEN_SEL_8197F))
#define BIT_GET_H2C_LEN_SEL_8197F(x)                                           \
	(((x) >> BIT_SHIFT_H2C_LEN_SEL_8197F) & BIT_MASK_H2C_LEN_SEL_8197F)
#define BIT_SET_H2C_LEN_SEL_8197F(x, v)                                        \
	(BIT_CLEAR_H2C_LEN_SEL_8197F(x) | BIT_H2C_LEN_SEL_8197F(v))

#define BIT_SHIFT_VI_PUB_LIMIT_8197F 16
#define BIT_MASK_VI_PUB_LIMIT_8197F 0xfff
#define BIT_VI_PUB_LIMIT_8197F(x)                                              \
	(((x) & BIT_MASK_VI_PUB_LIMIT_8197F) << BIT_SHIFT_VI_PUB_LIMIT_8197F)
#define BITS_VI_PUB_LIMIT_8197F                                                \
	(BIT_MASK_VI_PUB_LIMIT_8197F << BIT_SHIFT_VI_PUB_LIMIT_8197F)
#define BIT_CLEAR_VI_PUB_LIMIT_8197F(x) ((x) & (~BITS_VI_PUB_LIMIT_8197F))
#define BIT_GET_VI_PUB_LIMIT_8197F(x)                                          \
	(((x) >> BIT_SHIFT_VI_PUB_LIMIT_8197F) & BIT_MASK_VI_PUB_LIMIT_8197F)
#define BIT_SET_VI_PUB_LIMIT_8197F(x, v)                                       \
	(BIT_CLEAR_VI_PUB_LIMIT_8197F(x) | BIT_VI_PUB_LIMIT_8197F(v))

#define BIT_SHIFT_VO_PUB_LIMIT_8197F 0
#define BIT_MASK_VO_PUB_LIMIT_8197F 0xfff
#define BIT_VO_PUB_LIMIT_8197F(x)                                              \
	(((x) & BIT_MASK_VO_PUB_LIMIT_8197F) << BIT_SHIFT_VO_PUB_LIMIT_8197F)
#define BITS_VO_PUB_LIMIT_8197F                                                \
	(BIT_MASK_VO_PUB_LIMIT_8197F << BIT_SHIFT_VO_PUB_LIMIT_8197F)
#define BIT_CLEAR_VO_PUB_LIMIT_8197F(x) ((x) & (~BITS_VO_PUB_LIMIT_8197F))
#define BIT_GET_VO_PUB_LIMIT_8197F(x)                                          \
	(((x) >> BIT_SHIFT_VO_PUB_LIMIT_8197F) & BIT_MASK_VO_PUB_LIMIT_8197F)
#define BIT_SET_VO_PUB_LIMIT_8197F(x, v)                                       \
	(BIT_CLEAR_VO_PUB_LIMIT_8197F(x) | BIT_VO_PUB_LIMIT_8197F(v))

#define BIT_SHIFT_BK_PUB_LIMIT_8197F 16
#define BIT_MASK_BK_PUB_LIMIT_8197F 0xfff
#define BIT_BK_PUB_LIMIT_8197F(x)                                              \
	(((x) & BIT_MASK_BK_PUB_LIMIT_8197F) << BIT_SHIFT_BK_PUB_LIMIT_8197F)
#define BITS_BK_PUB_LIMIT_8197F                                                \
	(BIT_MASK_BK_PUB_LIMIT_8197F << BIT_SHIFT_BK_PUB_LIMIT_8197F)
#define BIT_CLEAR_BK_PUB_LIMIT_8197F(x) ((x) & (~BITS_BK_PUB_LIMIT_8197F))
#define BIT_GET_BK_PUB_LIMIT_8197F(x)                                          \
	(((x) >> BIT_SHIFT_BK_PUB_LIMIT_8197F) & BIT_MASK_BK_PUB_LIMIT_8197F)
#define BIT_SET_BK_PUB_LIMIT_8197F(x, v)                                       \
	(BIT_CLEAR_BK_PUB_LIMIT_8197F(x) | BIT_BK_PUB_LIMIT_8197F(v))

#define BIT_SHIFT_BE_PUB_LIMIT_8197F 0
#define BIT_MASK_BE_PUB_LIMIT_8197F 0xfff
#define BIT_BE_PUB_LIMIT_8197F(x)                                              \
	(((x) & BIT_MASK_BE_PUB_LIMIT_8197F) << BIT_SHIFT_BE_PUB_LIMIT_8197F)
#define BITS_BE_PUB_LIMIT_8197F                                                \
	(BIT_MASK_BE_PUB_LIMIT_8197F << BIT_SHIFT_BE_PUB_LIMIT_8197F)
#define BIT_CLEAR_BE_PUB_LIMIT_8197F(x) ((x) & (~BITS_BE_PUB_LIMIT_8197F))
#define BIT_GET_BE_PUB_LIMIT_8197F(x)                                          \
	(((x) >> BIT_SHIFT_BE_PUB_LIMIT_8197F) & BIT_MASK_BE_PUB_LIMIT_8197F)
#define BIT_SET_BE_PUB_LIMIT_8197F(x, v)                                       \
	(BIT_CLEAR_BE_PUB_LIMIT_8197F(x) | BIT_BE_PUB_LIMIT_8197F(v))

/* 2 REG_RXDMA_AGG_PG_TH_8197F */
#define BIT_DMA_STORE_MODE_8197F BIT(31)
#define BIT_EN_FW_ADD_8197F BIT(30)
#define BIT_EN_PRE_CALC_8197F BIT(29)
#define BIT_RXAGG_SW_EN_8197F BIT(28)

#define BIT_SHIFT_PKT_NUM_WOL_8197F 16
#define BIT_MASK_PKT_NUM_WOL_8197F 0xff
#define BIT_PKT_NUM_WOL_8197F(x)                                               \
	(((x) & BIT_MASK_PKT_NUM_WOL_8197F) << BIT_SHIFT_PKT_NUM_WOL_8197F)
#define BITS_PKT_NUM_WOL_8197F                                                 \
	(BIT_MASK_PKT_NUM_WOL_8197F << BIT_SHIFT_PKT_NUM_WOL_8197F)
#define BIT_CLEAR_PKT_NUM_WOL_8197F(x) ((x) & (~BITS_PKT_NUM_WOL_8197F))
#define BIT_GET_PKT_NUM_WOL_8197F(x)                                           \
	(((x) >> BIT_SHIFT_PKT_NUM_WOL_8197F) & BIT_MASK_PKT_NUM_WOL_8197F)
#define BIT_SET_PKT_NUM_WOL_8197F(x, v)                                        \
	(BIT_CLEAR_PKT_NUM_WOL_8197F(x) | BIT_PKT_NUM_WOL_8197F(v))

#define BIT_SHIFT_DMA_AGG_TO_V1_8197F 8
#define BIT_MASK_DMA_AGG_TO_V1_8197F 0xff
#define BIT_DMA_AGG_TO_V1_8197F(x)                                             \
	(((x) & BIT_MASK_DMA_AGG_TO_V1_8197F) << BIT_SHIFT_DMA_AGG_TO_V1_8197F)
#define BITS_DMA_AGG_TO_V1_8197F                                               \
	(BIT_MASK_DMA_AGG_TO_V1_8197F << BIT_SHIFT_DMA_AGG_TO_V1_8197F)
#define BIT_CLEAR_DMA_AGG_TO_V1_8197F(x) ((x) & (~BITS_DMA_AGG_TO_V1_8197F))
#define BIT_GET_DMA_AGG_TO_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_DMA_AGG_TO_V1_8197F) & BIT_MASK_DMA_AGG_TO_V1_8197F)
#define BIT_SET_DMA_AGG_TO_V1_8197F(x, v)                                      \
	(BIT_CLEAR_DMA_AGG_TO_V1_8197F(x) | BIT_DMA_AGG_TO_V1_8197F(v))

#define BIT_SHIFT_RXDMA_AGG_PG_TH_8197F 0
#define BIT_MASK_RXDMA_AGG_PG_TH_8197F 0xff
#define BIT_RXDMA_AGG_PG_TH_8197F(x)                                           \
	(((x) & BIT_MASK_RXDMA_AGG_PG_TH_8197F)                                \
	 << BIT_SHIFT_RXDMA_AGG_PG_TH_8197F)
#define BITS_RXDMA_AGG_PG_TH_8197F                                             \
	(BIT_MASK_RXDMA_AGG_PG_TH_8197F << BIT_SHIFT_RXDMA_AGG_PG_TH_8197F)
#define BIT_CLEAR_RXDMA_AGG_PG_TH_8197F(x) ((x) & (~BITS_RXDMA_AGG_PG_TH_8197F))
#define BIT_GET_RXDMA_AGG_PG_TH_8197F(x)                                       \
	(((x) >> BIT_SHIFT_RXDMA_AGG_PG_TH_8197F) &                            \
	 BIT_MASK_RXDMA_AGG_PG_TH_8197F)
#define BIT_SET_RXDMA_AGG_PG_TH_8197F(x, v)                                    \
	(BIT_CLEAR_RXDMA_AGG_PG_TH_8197F(x) | BIT_RXDMA_AGG_PG_TH_8197F(v))

/* 2 REG_RXPKT_NUM_8197F */

#define BIT_SHIFT_RXPKT_NUM_8197F 24
#define BIT_MASK_RXPKT_NUM_8197F 0xff
#define BIT_RXPKT_NUM_8197F(x)                                                 \
	(((x) & BIT_MASK_RXPKT_NUM_8197F) << BIT_SHIFT_RXPKT_NUM_8197F)
#define BITS_RXPKT_NUM_8197F                                                   \
	(BIT_MASK_RXPKT_NUM_8197F << BIT_SHIFT_RXPKT_NUM_8197F)
#define BIT_CLEAR_RXPKT_NUM_8197F(x) ((x) & (~BITS_RXPKT_NUM_8197F))
#define BIT_GET_RXPKT_NUM_8197F(x)                                             \
	(((x) >> BIT_SHIFT_RXPKT_NUM_8197F) & BIT_MASK_RXPKT_NUM_8197F)
#define BIT_SET_RXPKT_NUM_8197F(x, v)                                          \
	(BIT_CLEAR_RXPKT_NUM_8197F(x) | BIT_RXPKT_NUM_8197F(v))

#define BIT_SHIFT_FW_UPD_RDPTR19_TO_16_8197F 20
#define BIT_MASK_FW_UPD_RDPTR19_TO_16_8197F 0xf
#define BIT_FW_UPD_RDPTR19_TO_16_8197F(x)                                      \
	(((x) & BIT_MASK_FW_UPD_RDPTR19_TO_16_8197F)                           \
	 << BIT_SHIFT_FW_UPD_RDPTR19_TO_16_8197F)
#define BITS_FW_UPD_RDPTR19_TO_16_8197F                                        \
	(BIT_MASK_FW_UPD_RDPTR19_TO_16_8197F                                   \
	 << BIT_SHIFT_FW_UPD_RDPTR19_TO_16_8197F)
#define BIT_CLEAR_FW_UPD_RDPTR19_TO_16_8197F(x)                                \
	((x) & (~BITS_FW_UPD_RDPTR19_TO_16_8197F))
#define BIT_GET_FW_UPD_RDPTR19_TO_16_8197F(x)                                  \
	(((x) >> BIT_SHIFT_FW_UPD_RDPTR19_TO_16_8197F) &                       \
	 BIT_MASK_FW_UPD_RDPTR19_TO_16_8197F)
#define BIT_SET_FW_UPD_RDPTR19_TO_16_8197F(x, v)                               \
	(BIT_CLEAR_FW_UPD_RDPTR19_TO_16_8197F(x) |                             \
	 BIT_FW_UPD_RDPTR19_TO_16_8197F(v))

#define BIT_RXDMA_REQ_8197F BIT(19)
#define BIT_RW_RELEASE_EN_8197F BIT(18)
#define BIT_RXDMA_IDLE_8197F BIT(17)
#define BIT_RXPKT_RELEASE_POLL_8197F BIT(16)

#define BIT_SHIFT_FW_UPD_RDPTR_8197F 0
#define BIT_MASK_FW_UPD_RDPTR_8197F 0xffff
#define BIT_FW_UPD_RDPTR_8197F(x)                                              \
	(((x) & BIT_MASK_FW_UPD_RDPTR_8197F) << BIT_SHIFT_FW_UPD_RDPTR_8197F)
#define BITS_FW_UPD_RDPTR_8197F                                                \
	(BIT_MASK_FW_UPD_RDPTR_8197F << BIT_SHIFT_FW_UPD_RDPTR_8197F)
#define BIT_CLEAR_FW_UPD_RDPTR_8197F(x) ((x) & (~BITS_FW_UPD_RDPTR_8197F))
#define BIT_GET_FW_UPD_RDPTR_8197F(x)                                          \
	(((x) >> BIT_SHIFT_FW_UPD_RDPTR_8197F) & BIT_MASK_FW_UPD_RDPTR_8197F)
#define BIT_SET_FW_UPD_RDPTR_8197F(x, v)                                       \
	(BIT_CLEAR_FW_UPD_RDPTR_8197F(x) | BIT_FW_UPD_RDPTR_8197F(v))

/* 2 REG_RXDMA_STATUS_8197F */
#define BIT_FC2H_PKT_OVERFLOW_8197F BIT(8)
#define BIT_C2H_PKT_OVF_8197F BIT(7)
#define BIT_AGG_CONFGI_ISSUE_8197F BIT(6)
#define BIT_FW_POLL_ISSUE_8197F BIT(5)
#define BIT_RX_DATA_UDN_8197F BIT(4)
#define BIT_RX_SFF_UDN_8197F BIT(3)
#define BIT_RX_SFF_OVF_8197F BIT(2)
#define BIT_RXPKT_OVF_8197F BIT(0)

/* 2 REG_RXDMA_DPR_8197F */

#define BIT_SHIFT_RDE_DEBUG_8197F 0
#define BIT_MASK_RDE_DEBUG_8197F 0xffffffffL
#define BIT_RDE_DEBUG_8197F(x)                                                 \
	(((x) & BIT_MASK_RDE_DEBUG_8197F) << BIT_SHIFT_RDE_DEBUG_8197F)
#define BITS_RDE_DEBUG_8197F                                                   \
	(BIT_MASK_RDE_DEBUG_8197F << BIT_SHIFT_RDE_DEBUG_8197F)
#define BIT_CLEAR_RDE_DEBUG_8197F(x) ((x) & (~BITS_RDE_DEBUG_8197F))
#define BIT_GET_RDE_DEBUG_8197F(x)                                             \
	(((x) >> BIT_SHIFT_RDE_DEBUG_8197F) & BIT_MASK_RDE_DEBUG_8197F)
#define BIT_SET_RDE_DEBUG_8197F(x, v)                                          \
	(BIT_CLEAR_RDE_DEBUG_8197F(x) | BIT_RDE_DEBUG_8197F(v))

/* 2 REG_RXDMA_MODE_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */
#define BIT_EN_SPD_8197F BIT(6)

#define BIT_SHIFT_BURST_SIZE_8197F 4
#define BIT_MASK_BURST_SIZE_8197F 0x3
#define BIT_BURST_SIZE_8197F(x)                                                \
	(((x) & BIT_MASK_BURST_SIZE_8197F) << BIT_SHIFT_BURST_SIZE_8197F)
#define BITS_BURST_SIZE_8197F                                                  \
	(BIT_MASK_BURST_SIZE_8197F << BIT_SHIFT_BURST_SIZE_8197F)
#define BIT_CLEAR_BURST_SIZE_8197F(x) ((x) & (~BITS_BURST_SIZE_8197F))
#define BIT_GET_BURST_SIZE_8197F(x)                                            \
	(((x) >> BIT_SHIFT_BURST_SIZE_8197F) & BIT_MASK_BURST_SIZE_8197F)
#define BIT_SET_BURST_SIZE_8197F(x, v)                                         \
	(BIT_CLEAR_BURST_SIZE_8197F(x) | BIT_BURST_SIZE_8197F(v))

#define BIT_SHIFT_BURST_CNT_8197F 2
#define BIT_MASK_BURST_CNT_8197F 0x3
#define BIT_BURST_CNT_8197F(x)                                                 \
	(((x) & BIT_MASK_BURST_CNT_8197F) << BIT_SHIFT_BURST_CNT_8197F)
#define BITS_BURST_CNT_8197F                                                   \
	(BIT_MASK_BURST_CNT_8197F << BIT_SHIFT_BURST_CNT_8197F)
#define BIT_CLEAR_BURST_CNT_8197F(x) ((x) & (~BITS_BURST_CNT_8197F))
#define BIT_GET_BURST_CNT_8197F(x)                                             \
	(((x) >> BIT_SHIFT_BURST_CNT_8197F) & BIT_MASK_BURST_CNT_8197F)
#define BIT_SET_BURST_CNT_8197F(x, v)                                          \
	(BIT_CLEAR_BURST_CNT_8197F(x) | BIT_BURST_CNT_8197F(v))

#define BIT_DMA_MODE_8197F BIT(1)

/* 2 REG_C2H_PKT_8197F */

#define BIT_SHIFT_R_C2H_STR_ADDR_16_TO_19_8197F 24
#define BIT_MASK_R_C2H_STR_ADDR_16_TO_19_8197F 0xf
#define BIT_R_C2H_STR_ADDR_16_TO_19_8197F(x)                                   \
	(((x) & BIT_MASK_R_C2H_STR_ADDR_16_TO_19_8197F)                        \
	 << BIT_SHIFT_R_C2H_STR_ADDR_16_TO_19_8197F)
#define BITS_R_C2H_STR_ADDR_16_TO_19_8197F                                     \
	(BIT_MASK_R_C2H_STR_ADDR_16_TO_19_8197F                                \
	 << BIT_SHIFT_R_C2H_STR_ADDR_16_TO_19_8197F)
#define BIT_CLEAR_R_C2H_STR_ADDR_16_TO_19_8197F(x)                             \
	((x) & (~BITS_R_C2H_STR_ADDR_16_TO_19_8197F))
#define BIT_GET_R_C2H_STR_ADDR_16_TO_19_8197F(x)                               \
	(((x) >> BIT_SHIFT_R_C2H_STR_ADDR_16_TO_19_8197F) &                    \
	 BIT_MASK_R_C2H_STR_ADDR_16_TO_19_8197F)
#define BIT_SET_R_C2H_STR_ADDR_16_TO_19_8197F(x, v)                            \
	(BIT_CLEAR_R_C2H_STR_ADDR_16_TO_19_8197F(x) |                          \
	 BIT_R_C2H_STR_ADDR_16_TO_19_8197F(v))

#define BIT_R_C2H_PKT_REQ_8197F BIT(16)

#define BIT_SHIFT_R_C2H_STR_ADDR_8197F 0
#define BIT_MASK_R_C2H_STR_ADDR_8197F 0xffff
#define BIT_R_C2H_STR_ADDR_8197F(x)                                            \
	(((x) & BIT_MASK_R_C2H_STR_ADDR_8197F)                                 \
	 << BIT_SHIFT_R_C2H_STR_ADDR_8197F)
#define BITS_R_C2H_STR_ADDR_8197F                                              \
	(BIT_MASK_R_C2H_STR_ADDR_8197F << BIT_SHIFT_R_C2H_STR_ADDR_8197F)
#define BIT_CLEAR_R_C2H_STR_ADDR_8197F(x) ((x) & (~BITS_R_C2H_STR_ADDR_8197F))
#define BIT_GET_R_C2H_STR_ADDR_8197F(x)                                        \
	(((x) >> BIT_SHIFT_R_C2H_STR_ADDR_8197F) &                             \
	 BIT_MASK_R_C2H_STR_ADDR_8197F)
#define BIT_SET_R_C2H_STR_ADDR_8197F(x, v)                                     \
	(BIT_CLEAR_R_C2H_STR_ADDR_8197F(x) | BIT_R_C2H_STR_ADDR_8197F(v))

/* 2 REG_FWFF_C2H_8197F */

#define BIT_SHIFT_C2H_DMA_ADDR_8197F 0
#define BIT_MASK_C2H_DMA_ADDR_8197F 0x3ffff
#define BIT_C2H_DMA_ADDR_8197F(x)                                              \
	(((x) & BIT_MASK_C2H_DMA_ADDR_8197F) << BIT_SHIFT_C2H_DMA_ADDR_8197F)
#define BITS_C2H_DMA_ADDR_8197F                                                \
	(BIT_MASK_C2H_DMA_ADDR_8197F << BIT_SHIFT_C2H_DMA_ADDR_8197F)
#define BIT_CLEAR_C2H_DMA_ADDR_8197F(x) ((x) & (~BITS_C2H_DMA_ADDR_8197F))
#define BIT_GET_C2H_DMA_ADDR_8197F(x)                                          \
	(((x) >> BIT_SHIFT_C2H_DMA_ADDR_8197F) & BIT_MASK_C2H_DMA_ADDR_8197F)
#define BIT_SET_C2H_DMA_ADDR_8197F(x, v)                                       \
	(BIT_CLEAR_C2H_DMA_ADDR_8197F(x) | BIT_C2H_DMA_ADDR_8197F(v))

/* 2 REG_FWFF_CTRL_8197F */
#define BIT_FWFF_DMAPKT_REQ_8197F BIT(31)

#define BIT_SHIFT_FWFF_DMA_PKT_NUM_8197F 16
#define BIT_MASK_FWFF_DMA_PKT_NUM_8197F 0xff
#define BIT_FWFF_DMA_PKT_NUM_8197F(x)                                          \
	(((x) & BIT_MASK_FWFF_DMA_PKT_NUM_8197F)                               \
	 << BIT_SHIFT_FWFF_DMA_PKT_NUM_8197F)
#define BITS_FWFF_DMA_PKT_NUM_8197F                                            \
	(BIT_MASK_FWFF_DMA_PKT_NUM_8197F << BIT_SHIFT_FWFF_DMA_PKT_NUM_8197F)
#define BIT_CLEAR_FWFF_DMA_PKT_NUM_8197F(x)                                    \
	((x) & (~BITS_FWFF_DMA_PKT_NUM_8197F))
#define BIT_GET_FWFF_DMA_PKT_NUM_8197F(x)                                      \
	(((x) >> BIT_SHIFT_FWFF_DMA_PKT_NUM_8197F) &                           \
	 BIT_MASK_FWFF_DMA_PKT_NUM_8197F)
#define BIT_SET_FWFF_DMA_PKT_NUM_8197F(x, v)                                   \
	(BIT_CLEAR_FWFF_DMA_PKT_NUM_8197F(x) | BIT_FWFF_DMA_PKT_NUM_8197F(v))

#define BIT_SHIFT_FWFF_STR_ADDR_8197F 0
#define BIT_MASK_FWFF_STR_ADDR_8197F 0xffff
#define BIT_FWFF_STR_ADDR_8197F(x)                                             \
	(((x) & BIT_MASK_FWFF_STR_ADDR_8197F) << BIT_SHIFT_FWFF_STR_ADDR_8197F)
#define BITS_FWFF_STR_ADDR_8197F                                               \
	(BIT_MASK_FWFF_STR_ADDR_8197F << BIT_SHIFT_FWFF_STR_ADDR_8197F)
#define BIT_CLEAR_FWFF_STR_ADDR_8197F(x) ((x) & (~BITS_FWFF_STR_ADDR_8197F))
#define BIT_GET_FWFF_STR_ADDR_8197F(x)                                         \
	(((x) >> BIT_SHIFT_FWFF_STR_ADDR_8197F) & BIT_MASK_FWFF_STR_ADDR_8197F)
#define BIT_SET_FWFF_STR_ADDR_8197F(x, v)                                      \
	(BIT_CLEAR_FWFF_STR_ADDR_8197F(x) | BIT_FWFF_STR_ADDR_8197F(v))

/* 2 REG_FWFF_PKT_INFO_8197F */

#define BIT_SHIFT_FWFF_PKT_QUEUED_8197F 16
#define BIT_MASK_FWFF_PKT_QUEUED_8197F 0xff
#define BIT_FWFF_PKT_QUEUED_8197F(x)                                           \
	(((x) & BIT_MASK_FWFF_PKT_QUEUED_8197F)                                \
	 << BIT_SHIFT_FWFF_PKT_QUEUED_8197F)
#define BITS_FWFF_PKT_QUEUED_8197F                                             \
	(BIT_MASK_FWFF_PKT_QUEUED_8197F << BIT_SHIFT_FWFF_PKT_QUEUED_8197F)
#define BIT_CLEAR_FWFF_PKT_QUEUED_8197F(x) ((x) & (~BITS_FWFF_PKT_QUEUED_8197F))
#define BIT_GET_FWFF_PKT_QUEUED_8197F(x)                                       \
	(((x) >> BIT_SHIFT_FWFF_PKT_QUEUED_8197F) &                            \
	 BIT_MASK_FWFF_PKT_QUEUED_8197F)
#define BIT_SET_FWFF_PKT_QUEUED_8197F(x, v)                                    \
	(BIT_CLEAR_FWFF_PKT_QUEUED_8197F(x) | BIT_FWFF_PKT_QUEUED_8197F(v))

#define BIT_SHIFT_FWFF_PKT_STR_ADDR_8197F 0
#define BIT_MASK_FWFF_PKT_STR_ADDR_8197F 0xffff
#define BIT_FWFF_PKT_STR_ADDR_8197F(x)                                         \
	(((x) & BIT_MASK_FWFF_PKT_STR_ADDR_8197F)                              \
	 << BIT_SHIFT_FWFF_PKT_STR_ADDR_8197F)
#define BITS_FWFF_PKT_STR_ADDR_8197F                                           \
	(BIT_MASK_FWFF_PKT_STR_ADDR_8197F << BIT_SHIFT_FWFF_PKT_STR_ADDR_8197F)
#define BIT_CLEAR_FWFF_PKT_STR_ADDR_8197F(x)                                   \
	((x) & (~BITS_FWFF_PKT_STR_ADDR_8197F))
#define BIT_GET_FWFF_PKT_STR_ADDR_8197F(x)                                     \
	(((x) >> BIT_SHIFT_FWFF_PKT_STR_ADDR_8197F) &                          \
	 BIT_MASK_FWFF_PKT_STR_ADDR_8197F)
#define BIT_SET_FWFF_PKT_STR_ADDR_8197F(x, v)                                  \
	(BIT_CLEAR_FWFF_PKT_STR_ADDR_8197F(x) | BIT_FWFF_PKT_STR_ADDR_8197F(v))

/* 2 REG_FC2H_INFO_8197F */
#define BIT_FC2H_PKT_REQ_8197F BIT(16)

#define BIT_SHIFT_FC2H_STR_ADDR_8197F 0
#define BIT_MASK_FC2H_STR_ADDR_8197F 0xffff
#define BIT_FC2H_STR_ADDR_8197F(x)                                             \
	(((x) & BIT_MASK_FC2H_STR_ADDR_8197F) << BIT_SHIFT_FC2H_STR_ADDR_8197F)
#define BITS_FC2H_STR_ADDR_8197F                                               \
	(BIT_MASK_FC2H_STR_ADDR_8197F << BIT_SHIFT_FC2H_STR_ADDR_8197F)
#define BIT_CLEAR_FC2H_STR_ADDR_8197F(x) ((x) & (~BITS_FC2H_STR_ADDR_8197F))
#define BIT_GET_FC2H_STR_ADDR_8197F(x)                                         \
	(((x) >> BIT_SHIFT_FC2H_STR_ADDR_8197F) & BIT_MASK_FC2H_STR_ADDR_8197F)
#define BIT_SET_FC2H_STR_ADDR_8197F(x, v)                                      \
	(BIT_CLEAR_FC2H_STR_ADDR_8197F(x) | BIT_FC2H_STR_ADDR_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_DDMA_CH0SA_8197F */

#define BIT_SHIFT_DDMACH0_SA_8197F 0
#define BIT_MASK_DDMACH0_SA_8197F 0xffffffffL
#define BIT_DDMACH0_SA_8197F(x)                                                \
	(((x) & BIT_MASK_DDMACH0_SA_8197F) << BIT_SHIFT_DDMACH0_SA_8197F)
#define BITS_DDMACH0_SA_8197F                                                  \
	(BIT_MASK_DDMACH0_SA_8197F << BIT_SHIFT_DDMACH0_SA_8197F)
#define BIT_CLEAR_DDMACH0_SA_8197F(x) ((x) & (~BITS_DDMACH0_SA_8197F))
#define BIT_GET_DDMACH0_SA_8197F(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH0_SA_8197F) & BIT_MASK_DDMACH0_SA_8197F)
#define BIT_SET_DDMACH0_SA_8197F(x, v)                                         \
	(BIT_CLEAR_DDMACH0_SA_8197F(x) | BIT_DDMACH0_SA_8197F(v))

/* 2 REG_DDMA_CH0DA_8197F */

#define BIT_SHIFT_DDMACH0_DA_8197F 0
#define BIT_MASK_DDMACH0_DA_8197F 0xffffffffL
#define BIT_DDMACH0_DA_8197F(x)                                                \
	(((x) & BIT_MASK_DDMACH0_DA_8197F) << BIT_SHIFT_DDMACH0_DA_8197F)
#define BITS_DDMACH0_DA_8197F                                                  \
	(BIT_MASK_DDMACH0_DA_8197F << BIT_SHIFT_DDMACH0_DA_8197F)
#define BIT_CLEAR_DDMACH0_DA_8197F(x) ((x) & (~BITS_DDMACH0_DA_8197F))
#define BIT_GET_DDMACH0_DA_8197F(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH0_DA_8197F) & BIT_MASK_DDMACH0_DA_8197F)
#define BIT_SET_DDMACH0_DA_8197F(x, v)                                         \
	(BIT_CLEAR_DDMACH0_DA_8197F(x) | BIT_DDMACH0_DA_8197F(v))

/* 2 REG_DDMA_CH0CTRL_8197F */
#define BIT_DDMACH0_OWN_8197F BIT(31)
#define BIT_DDMACH0_CHKSUM_EN_8197F BIT(29)
#define BIT_DDMACH0_DA_W_DISABLE_8197F BIT(28)
#define BIT_DDMACH0_CHKSUM_STS_8197F BIT(27)
#define BIT_DDMACH0_DDMA_MODE_8197F BIT(26)
#define BIT_DDMACH0_RESET_CHKSUM_STS_8197F BIT(25)
#define BIT_DDMACH0_CHKSUM_CONT_8197F BIT(24)

#define BIT_SHIFT_DDMACH0_DLEN_8197F 0
#define BIT_MASK_DDMACH0_DLEN_8197F 0x3ffff
#define BIT_DDMACH0_DLEN_8197F(x)                                              \
	(((x) & BIT_MASK_DDMACH0_DLEN_8197F) << BIT_SHIFT_DDMACH0_DLEN_8197F)
#define BITS_DDMACH0_DLEN_8197F                                                \
	(BIT_MASK_DDMACH0_DLEN_8197F << BIT_SHIFT_DDMACH0_DLEN_8197F)
#define BIT_CLEAR_DDMACH0_DLEN_8197F(x) ((x) & (~BITS_DDMACH0_DLEN_8197F))
#define BIT_GET_DDMACH0_DLEN_8197F(x)                                          \
	(((x) >> BIT_SHIFT_DDMACH0_DLEN_8197F) & BIT_MASK_DDMACH0_DLEN_8197F)
#define BIT_SET_DDMACH0_DLEN_8197F(x, v)                                       \
	(BIT_CLEAR_DDMACH0_DLEN_8197F(x) | BIT_DDMACH0_DLEN_8197F(v))

/* 2 REG_DDMA_CH1SA_8197F */

#define BIT_SHIFT_DDMACH1_SA_8197F 0
#define BIT_MASK_DDMACH1_SA_8197F 0xffffffffL
#define BIT_DDMACH1_SA_8197F(x)                                                \
	(((x) & BIT_MASK_DDMACH1_SA_8197F) << BIT_SHIFT_DDMACH1_SA_8197F)
#define BITS_DDMACH1_SA_8197F                                                  \
	(BIT_MASK_DDMACH1_SA_8197F << BIT_SHIFT_DDMACH1_SA_8197F)
#define BIT_CLEAR_DDMACH1_SA_8197F(x) ((x) & (~BITS_DDMACH1_SA_8197F))
#define BIT_GET_DDMACH1_SA_8197F(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH1_SA_8197F) & BIT_MASK_DDMACH1_SA_8197F)
#define BIT_SET_DDMACH1_SA_8197F(x, v)                                         \
	(BIT_CLEAR_DDMACH1_SA_8197F(x) | BIT_DDMACH1_SA_8197F(v))

/* 2 REG_DDMA_CH1DA_8197F */

#define BIT_SHIFT_DDMACH1_DA_8197F 0
#define BIT_MASK_DDMACH1_DA_8197F 0xffffffffL
#define BIT_DDMACH1_DA_8197F(x)                                                \
	(((x) & BIT_MASK_DDMACH1_DA_8197F) << BIT_SHIFT_DDMACH1_DA_8197F)
#define BITS_DDMACH1_DA_8197F                                                  \
	(BIT_MASK_DDMACH1_DA_8197F << BIT_SHIFT_DDMACH1_DA_8197F)
#define BIT_CLEAR_DDMACH1_DA_8197F(x) ((x) & (~BITS_DDMACH1_DA_8197F))
#define BIT_GET_DDMACH1_DA_8197F(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH1_DA_8197F) & BIT_MASK_DDMACH1_DA_8197F)
#define BIT_SET_DDMACH1_DA_8197F(x, v)                                         \
	(BIT_CLEAR_DDMACH1_DA_8197F(x) | BIT_DDMACH1_DA_8197F(v))

/* 2 REG_DDMA_CH1CTRL_8197F */
#define BIT_DDMACH1_OWN_8197F BIT(31)
#define BIT_DDMACH1_CHKSUM_EN_8197F BIT(29)
#define BIT_DDMACH1_DA_W_DISABLE_8197F BIT(28)
#define BIT_DDMACH1_CHKSUM_STS_8197F BIT(27)
#define BIT_DDMACH1_DDMA_MODE_8197F BIT(26)
#define BIT_DDMACH1_RESET_CHKSUM_STS_8197F BIT(25)
#define BIT_DDMACH1_CHKSUM_CONT_8197F BIT(24)

#define BIT_SHIFT_DDMACH1_DLEN_8197F 0
#define BIT_MASK_DDMACH1_DLEN_8197F 0x3ffff
#define BIT_DDMACH1_DLEN_8197F(x)                                              \
	(((x) & BIT_MASK_DDMACH1_DLEN_8197F) << BIT_SHIFT_DDMACH1_DLEN_8197F)
#define BITS_DDMACH1_DLEN_8197F                                                \
	(BIT_MASK_DDMACH1_DLEN_8197F << BIT_SHIFT_DDMACH1_DLEN_8197F)
#define BIT_CLEAR_DDMACH1_DLEN_8197F(x) ((x) & (~BITS_DDMACH1_DLEN_8197F))
#define BIT_GET_DDMACH1_DLEN_8197F(x)                                          \
	(((x) >> BIT_SHIFT_DDMACH1_DLEN_8197F) & BIT_MASK_DDMACH1_DLEN_8197F)
#define BIT_SET_DDMACH1_DLEN_8197F(x, v)                                       \
	(BIT_CLEAR_DDMACH1_DLEN_8197F(x) | BIT_DDMACH1_DLEN_8197F(v))

/* 2 REG_DDMA_CH2SA_8197F */

#define BIT_SHIFT_DDMACH2_SA_8197F 0
#define BIT_MASK_DDMACH2_SA_8197F 0xffffffffL
#define BIT_DDMACH2_SA_8197F(x)                                                \
	(((x) & BIT_MASK_DDMACH2_SA_8197F) << BIT_SHIFT_DDMACH2_SA_8197F)
#define BITS_DDMACH2_SA_8197F                                                  \
	(BIT_MASK_DDMACH2_SA_8197F << BIT_SHIFT_DDMACH2_SA_8197F)
#define BIT_CLEAR_DDMACH2_SA_8197F(x) ((x) & (~BITS_DDMACH2_SA_8197F))
#define BIT_GET_DDMACH2_SA_8197F(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH2_SA_8197F) & BIT_MASK_DDMACH2_SA_8197F)
#define BIT_SET_DDMACH2_SA_8197F(x, v)                                         \
	(BIT_CLEAR_DDMACH2_SA_8197F(x) | BIT_DDMACH2_SA_8197F(v))

/* 2 REG_DDMA_CH2DA_8197F */

#define BIT_SHIFT_DDMACH2_DA_8197F 0
#define BIT_MASK_DDMACH2_DA_8197F 0xffffffffL
#define BIT_DDMACH2_DA_8197F(x)                                                \
	(((x) & BIT_MASK_DDMACH2_DA_8197F) << BIT_SHIFT_DDMACH2_DA_8197F)
#define BITS_DDMACH2_DA_8197F                                                  \
	(BIT_MASK_DDMACH2_DA_8197F << BIT_SHIFT_DDMACH2_DA_8197F)
#define BIT_CLEAR_DDMACH2_DA_8197F(x) ((x) & (~BITS_DDMACH2_DA_8197F))
#define BIT_GET_DDMACH2_DA_8197F(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH2_DA_8197F) & BIT_MASK_DDMACH2_DA_8197F)
#define BIT_SET_DDMACH2_DA_8197F(x, v)                                         \
	(BIT_CLEAR_DDMACH2_DA_8197F(x) | BIT_DDMACH2_DA_8197F(v))

/* 2 REG_DDMA_CH2CTRL_8197F */
#define BIT_DDMACH2_OWN_8197F BIT(31)
#define BIT_DDMACH2_CHKSUM_EN_8197F BIT(29)
#define BIT_DDMACH2_DA_W_DISABLE_8197F BIT(28)
#define BIT_DDMACH2_CHKSUM_STS_8197F BIT(27)
#define BIT_DDMACH2_DDMA_MODE_8197F BIT(26)
#define BIT_DDMACH2_RESET_CHKSUM_STS_8197F BIT(25)
#define BIT_DDMACH2_CHKSUM_CONT_8197F BIT(24)

#define BIT_SHIFT_DDMACH2_DLEN_8197F 0
#define BIT_MASK_DDMACH2_DLEN_8197F 0x3ffff
#define BIT_DDMACH2_DLEN_8197F(x)                                              \
	(((x) & BIT_MASK_DDMACH2_DLEN_8197F) << BIT_SHIFT_DDMACH2_DLEN_8197F)
#define BITS_DDMACH2_DLEN_8197F                                                \
	(BIT_MASK_DDMACH2_DLEN_8197F << BIT_SHIFT_DDMACH2_DLEN_8197F)
#define BIT_CLEAR_DDMACH2_DLEN_8197F(x) ((x) & (~BITS_DDMACH2_DLEN_8197F))
#define BIT_GET_DDMACH2_DLEN_8197F(x)                                          \
	(((x) >> BIT_SHIFT_DDMACH2_DLEN_8197F) & BIT_MASK_DDMACH2_DLEN_8197F)
#define BIT_SET_DDMACH2_DLEN_8197F(x, v)                                       \
	(BIT_CLEAR_DDMACH2_DLEN_8197F(x) | BIT_DDMACH2_DLEN_8197F(v))

/* 2 REG_DDMA_CH3SA_8197F */

#define BIT_SHIFT_DDMACH3_SA_8197F 0
#define BIT_MASK_DDMACH3_SA_8197F 0xffffffffL
#define BIT_DDMACH3_SA_8197F(x)                                                \
	(((x) & BIT_MASK_DDMACH3_SA_8197F) << BIT_SHIFT_DDMACH3_SA_8197F)
#define BITS_DDMACH3_SA_8197F                                                  \
	(BIT_MASK_DDMACH3_SA_8197F << BIT_SHIFT_DDMACH3_SA_8197F)
#define BIT_CLEAR_DDMACH3_SA_8197F(x) ((x) & (~BITS_DDMACH3_SA_8197F))
#define BIT_GET_DDMACH3_SA_8197F(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH3_SA_8197F) & BIT_MASK_DDMACH3_SA_8197F)
#define BIT_SET_DDMACH3_SA_8197F(x, v)                                         \
	(BIT_CLEAR_DDMACH3_SA_8197F(x) | BIT_DDMACH3_SA_8197F(v))

/* 2 REG_DDMA_CH3DA_8197F */

#define BIT_SHIFT_DDMACH3_DA_8197F 0
#define BIT_MASK_DDMACH3_DA_8197F 0xffffffffL
#define BIT_DDMACH3_DA_8197F(x)                                                \
	(((x) & BIT_MASK_DDMACH3_DA_8197F) << BIT_SHIFT_DDMACH3_DA_8197F)
#define BITS_DDMACH3_DA_8197F                                                  \
	(BIT_MASK_DDMACH3_DA_8197F << BIT_SHIFT_DDMACH3_DA_8197F)
#define BIT_CLEAR_DDMACH3_DA_8197F(x) ((x) & (~BITS_DDMACH3_DA_8197F))
#define BIT_GET_DDMACH3_DA_8197F(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH3_DA_8197F) & BIT_MASK_DDMACH3_DA_8197F)
#define BIT_SET_DDMACH3_DA_8197F(x, v)                                         \
	(BIT_CLEAR_DDMACH3_DA_8197F(x) | BIT_DDMACH3_DA_8197F(v))

/* 2 REG_DDMA_CH3CTRL_8197F */
#define BIT_DDMACH3_OWN_8197F BIT(31)
#define BIT_DDMACH3_CHKSUM_EN_8197F BIT(29)
#define BIT_DDMACH3_DA_W_DISABLE_8197F BIT(28)
#define BIT_DDMACH3_CHKSUM_STS_8197F BIT(27)
#define BIT_DDMACH3_DDMA_MODE_8197F BIT(26)
#define BIT_DDMACH3_RESET_CHKSUM_STS_8197F BIT(25)
#define BIT_DDMACH3_CHKSUM_CONT_8197F BIT(24)

#define BIT_SHIFT_DDMACH3_DLEN_8197F 0
#define BIT_MASK_DDMACH3_DLEN_8197F 0x3ffff
#define BIT_DDMACH3_DLEN_8197F(x)                                              \
	(((x) & BIT_MASK_DDMACH3_DLEN_8197F) << BIT_SHIFT_DDMACH3_DLEN_8197F)
#define BITS_DDMACH3_DLEN_8197F                                                \
	(BIT_MASK_DDMACH3_DLEN_8197F << BIT_SHIFT_DDMACH3_DLEN_8197F)
#define BIT_CLEAR_DDMACH3_DLEN_8197F(x) ((x) & (~BITS_DDMACH3_DLEN_8197F))
#define BIT_GET_DDMACH3_DLEN_8197F(x)                                          \
	(((x) >> BIT_SHIFT_DDMACH3_DLEN_8197F) & BIT_MASK_DDMACH3_DLEN_8197F)
#define BIT_SET_DDMACH3_DLEN_8197F(x, v)                                       \
	(BIT_CLEAR_DDMACH3_DLEN_8197F(x) | BIT_DDMACH3_DLEN_8197F(v))

/* 2 REG_DDMA_CH4SA_8197F */

#define BIT_SHIFT_DDMACH4_SA_8197F 0
#define BIT_MASK_DDMACH4_SA_8197F 0xffffffffL
#define BIT_DDMACH4_SA_8197F(x)                                                \
	(((x) & BIT_MASK_DDMACH4_SA_8197F) << BIT_SHIFT_DDMACH4_SA_8197F)
#define BITS_DDMACH4_SA_8197F                                                  \
	(BIT_MASK_DDMACH4_SA_8197F << BIT_SHIFT_DDMACH4_SA_8197F)
#define BIT_CLEAR_DDMACH4_SA_8197F(x) ((x) & (~BITS_DDMACH4_SA_8197F))
#define BIT_GET_DDMACH4_SA_8197F(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH4_SA_8197F) & BIT_MASK_DDMACH4_SA_8197F)
#define BIT_SET_DDMACH4_SA_8197F(x, v)                                         \
	(BIT_CLEAR_DDMACH4_SA_8197F(x) | BIT_DDMACH4_SA_8197F(v))

/* 2 REG_DDMA_CH4DA_8197F */

#define BIT_SHIFT_DDMACH4_DA_8197F 0
#define BIT_MASK_DDMACH4_DA_8197F 0xffffffffL
#define BIT_DDMACH4_DA_8197F(x)                                                \
	(((x) & BIT_MASK_DDMACH4_DA_8197F) << BIT_SHIFT_DDMACH4_DA_8197F)
#define BITS_DDMACH4_DA_8197F                                                  \
	(BIT_MASK_DDMACH4_DA_8197F << BIT_SHIFT_DDMACH4_DA_8197F)
#define BIT_CLEAR_DDMACH4_DA_8197F(x) ((x) & (~BITS_DDMACH4_DA_8197F))
#define BIT_GET_DDMACH4_DA_8197F(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH4_DA_8197F) & BIT_MASK_DDMACH4_DA_8197F)
#define BIT_SET_DDMACH4_DA_8197F(x, v)                                         \
	(BIT_CLEAR_DDMACH4_DA_8197F(x) | BIT_DDMACH4_DA_8197F(v))

/* 2 REG_DDMA_CH4CTRL_8197F */
#define BIT_DDMACH4_OWN_8197F BIT(31)
#define BIT_DDMACH4_CHKSUM_EN_8197F BIT(29)
#define BIT_DDMACH4_DA_W_DISABLE_8197F BIT(28)
#define BIT_DDMACH4_CHKSUM_STS_8197F BIT(27)
#define BIT_DDMACH4_DDMA_MODE_8197F BIT(26)
#define BIT_DDMACH4_RESET_CHKSUM_STS_8197F BIT(25)
#define BIT_DDMACH4_CHKSUM_CONT_8197F BIT(24)

#define BIT_SHIFT_DDMACH4_DLEN_8197F 0
#define BIT_MASK_DDMACH4_DLEN_8197F 0x3ffff
#define BIT_DDMACH4_DLEN_8197F(x)                                              \
	(((x) & BIT_MASK_DDMACH4_DLEN_8197F) << BIT_SHIFT_DDMACH4_DLEN_8197F)
#define BITS_DDMACH4_DLEN_8197F                                                \
	(BIT_MASK_DDMACH4_DLEN_8197F << BIT_SHIFT_DDMACH4_DLEN_8197F)
#define BIT_CLEAR_DDMACH4_DLEN_8197F(x) ((x) & (~BITS_DDMACH4_DLEN_8197F))
#define BIT_GET_DDMACH4_DLEN_8197F(x)                                          \
	(((x) >> BIT_SHIFT_DDMACH4_DLEN_8197F) & BIT_MASK_DDMACH4_DLEN_8197F)
#define BIT_SET_DDMACH4_DLEN_8197F(x, v)                                       \
	(BIT_CLEAR_DDMACH4_DLEN_8197F(x) | BIT_DDMACH4_DLEN_8197F(v))

/* 2 REG_DDMA_CH5SA_8197F */

#define BIT_SHIFT_DDMACH5_SA_8197F 0
#define BIT_MASK_DDMACH5_SA_8197F 0xffffffffL
#define BIT_DDMACH5_SA_8197F(x)                                                \
	(((x) & BIT_MASK_DDMACH5_SA_8197F) << BIT_SHIFT_DDMACH5_SA_8197F)
#define BITS_DDMACH5_SA_8197F                                                  \
	(BIT_MASK_DDMACH5_SA_8197F << BIT_SHIFT_DDMACH5_SA_8197F)
#define BIT_CLEAR_DDMACH5_SA_8197F(x) ((x) & (~BITS_DDMACH5_SA_8197F))
#define BIT_GET_DDMACH5_SA_8197F(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH5_SA_8197F) & BIT_MASK_DDMACH5_SA_8197F)
#define BIT_SET_DDMACH5_SA_8197F(x, v)                                         \
	(BIT_CLEAR_DDMACH5_SA_8197F(x) | BIT_DDMACH5_SA_8197F(v))

/* 2 REG_DDMA_CH5DA_8197F */

#define BIT_SHIFT_DDMACH5_DA_8197F 0
#define BIT_MASK_DDMACH5_DA_8197F 0xffffffffL
#define BIT_DDMACH5_DA_8197F(x)                                                \
	(((x) & BIT_MASK_DDMACH5_DA_8197F) << BIT_SHIFT_DDMACH5_DA_8197F)
#define BITS_DDMACH5_DA_8197F                                                  \
	(BIT_MASK_DDMACH5_DA_8197F << BIT_SHIFT_DDMACH5_DA_8197F)
#define BIT_CLEAR_DDMACH5_DA_8197F(x) ((x) & (~BITS_DDMACH5_DA_8197F))
#define BIT_GET_DDMACH5_DA_8197F(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH5_DA_8197F) & BIT_MASK_DDMACH5_DA_8197F)
#define BIT_SET_DDMACH5_DA_8197F(x, v)                                         \
	(BIT_CLEAR_DDMACH5_DA_8197F(x) | BIT_DDMACH5_DA_8197F(v))

/* 2 REG_REG_DDMA_CH5CTRL_8197F */
#define BIT_DDMACH5_OWN_8197F BIT(31)
#define BIT_DDMACH5_CHKSUM_EN_8197F BIT(29)
#define BIT_DDMACH5_DA_W_DISABLE_8197F BIT(28)
#define BIT_DDMACH5_CHKSUM_STS_8197F BIT(27)
#define BIT_DDMACH5_DDMA_MODE_8197F BIT(26)
#define BIT_DDMACH5_RESET_CHKSUM_STS_8197F BIT(25)
#define BIT_DDMACH5_CHKSUM_CONT_8197F BIT(24)

#define BIT_SHIFT_DDMACH5_DLEN_8197F 0
#define BIT_MASK_DDMACH5_DLEN_8197F 0x3ffff
#define BIT_DDMACH5_DLEN_8197F(x)                                              \
	(((x) & BIT_MASK_DDMACH5_DLEN_8197F) << BIT_SHIFT_DDMACH5_DLEN_8197F)
#define BITS_DDMACH5_DLEN_8197F                                                \
	(BIT_MASK_DDMACH5_DLEN_8197F << BIT_SHIFT_DDMACH5_DLEN_8197F)
#define BIT_CLEAR_DDMACH5_DLEN_8197F(x) ((x) & (~BITS_DDMACH5_DLEN_8197F))
#define BIT_GET_DDMACH5_DLEN_8197F(x)                                          \
	(((x) >> BIT_SHIFT_DDMACH5_DLEN_8197F) & BIT_MASK_DDMACH5_DLEN_8197F)
#define BIT_SET_DDMACH5_DLEN_8197F(x, v)                                       \
	(BIT_CLEAR_DDMACH5_DLEN_8197F(x) | BIT_DDMACH5_DLEN_8197F(v))

/* 2 REG_DDMA_INT_MSK_8197F */
#define BIT_DDMACH5_MSK_8197F BIT(5)
#define BIT_DDMACH4_MSK_8197F BIT(4)
#define BIT_DDMACH3_MSK_8197F BIT(3)
#define BIT_DDMACH2_MSK_8197F BIT(2)
#define BIT_DDMACH1_MSK_8197F BIT(1)
#define BIT_DDMACH0_MSK_8197F BIT(0)

/* 2 REG_DDMA_CHSTATUS_8197F */
#define BIT_DDMACH5_BUSY_8197F BIT(5)
#define BIT_DDMACH4_BUSY_8197F BIT(4)
#define BIT_DDMACH3_BUSY_8197F BIT(3)
#define BIT_DDMACH2_BUSY_8197F BIT(2)
#define BIT_DDMACH1_BUSY_8197F BIT(1)
#define BIT_DDMACH0_BUSY_8197F BIT(0)

/* 2 REG_DDMA_CHKSUM_8197F */

#define BIT_SHIFT_IDDMA0_CHKSUM_8197F 0
#define BIT_MASK_IDDMA0_CHKSUM_8197F 0xffff
#define BIT_IDDMA0_CHKSUM_8197F(x)                                             \
	(((x) & BIT_MASK_IDDMA0_CHKSUM_8197F) << BIT_SHIFT_IDDMA0_CHKSUM_8197F)
#define BITS_IDDMA0_CHKSUM_8197F                                               \
	(BIT_MASK_IDDMA0_CHKSUM_8197F << BIT_SHIFT_IDDMA0_CHKSUM_8197F)
#define BIT_CLEAR_IDDMA0_CHKSUM_8197F(x) ((x) & (~BITS_IDDMA0_CHKSUM_8197F))
#define BIT_GET_IDDMA0_CHKSUM_8197F(x)                                         \
	(((x) >> BIT_SHIFT_IDDMA0_CHKSUM_8197F) & BIT_MASK_IDDMA0_CHKSUM_8197F)
#define BIT_SET_IDDMA0_CHKSUM_8197F(x, v)                                      \
	(BIT_CLEAR_IDDMA0_CHKSUM_8197F(x) | BIT_IDDMA0_CHKSUM_8197F(v))

/* 2 REG_DDMA_MONITOR_8197F */
#define BIT_IDDMA0_PERMU_UNDERFLOW_8197F BIT(14)
#define BIT_IDDMA0_FIFO_UNDERFLOW_8197F BIT(13)
#define BIT_IDDMA0_FIFO_OVERFLOW_8197F BIT(12)
#define BIT_CH5_ERR_8197F BIT(5)
#define BIT_CH4_ERR_8197F BIT(4)
#define BIT_CH3_ERR_8197F BIT(3)
#define BIT_CH2_ERR_8197F BIT(2)
#define BIT_CH1_ERR_8197F BIT(1)
#define BIT_CH0_ERR_8197F BIT(0)

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_HCI_CTRL_8197F */
#define BIT_HCIIO_PERSTB_SEL_8197F BIT(31)

#define BIT_SHIFT_HCI_MAX_RXDMA_8197F 28
#define BIT_MASK_HCI_MAX_RXDMA_8197F 0x7
#define BIT_HCI_MAX_RXDMA_8197F(x)                                             \
	(((x) & BIT_MASK_HCI_MAX_RXDMA_8197F) << BIT_SHIFT_HCI_MAX_RXDMA_8197F)
#define BITS_HCI_MAX_RXDMA_8197F                                               \
	(BIT_MASK_HCI_MAX_RXDMA_8197F << BIT_SHIFT_HCI_MAX_RXDMA_8197F)
#define BIT_CLEAR_HCI_MAX_RXDMA_8197F(x) ((x) & (~BITS_HCI_MAX_RXDMA_8197F))
#define BIT_GET_HCI_MAX_RXDMA_8197F(x)                                         \
	(((x) >> BIT_SHIFT_HCI_MAX_RXDMA_8197F) & BIT_MASK_HCI_MAX_RXDMA_8197F)
#define BIT_SET_HCI_MAX_RXDMA_8197F(x, v)                                      \
	(BIT_CLEAR_HCI_MAX_RXDMA_8197F(x) | BIT_HCI_MAX_RXDMA_8197F(v))

#define BIT_MULRW_8197F BIT(27)

#define BIT_SHIFT_HCI_MAX_TXDMA_8197F 24
#define BIT_MASK_HCI_MAX_TXDMA_8197F 0x7
#define BIT_HCI_MAX_TXDMA_8197F(x)                                             \
	(((x) & BIT_MASK_HCI_MAX_TXDMA_8197F) << BIT_SHIFT_HCI_MAX_TXDMA_8197F)
#define BITS_HCI_MAX_TXDMA_8197F                                               \
	(BIT_MASK_HCI_MAX_TXDMA_8197F << BIT_SHIFT_HCI_MAX_TXDMA_8197F)
#define BIT_CLEAR_HCI_MAX_TXDMA_8197F(x) ((x) & (~BITS_HCI_MAX_TXDMA_8197F))
#define BIT_GET_HCI_MAX_TXDMA_8197F(x)                                         \
	(((x) >> BIT_SHIFT_HCI_MAX_TXDMA_8197F) & BIT_MASK_HCI_MAX_TXDMA_8197F)
#define BIT_SET_HCI_MAX_TXDMA_8197F(x, v)                                      \
	(BIT_CLEAR_HCI_MAX_TXDMA_8197F(x) | BIT_HCI_MAX_TXDMA_8197F(v))

#define BIT_EN_CPL_TIMEOUT_PS_8197F BIT(22)
#define BIT_REG_TXDMA_FAIL_PS_8197F BIT(21)
#define BIT_HCI_RST_TRXDMA_INTF_8197F BIT(20)
#define BIT_EN_HWENTR_L1_8197F BIT(19)
#define BIT_EN_ADV_CLKGATE_8197F BIT(18)
#define BIT_HCI_EN_SWENT_L23_8197F BIT(17)
#define BIT_HCI_EN_HWEXT_L1_8197F BIT(16)
#define BIT_RX_CLOSE_EN_8197F BIT(15)
#define BIT_STOP_BCNQ_8197F BIT(14)
#define BIT_STOP_MGQ_8197F BIT(13)
#define BIT_STOP_VOQ_8197F BIT(12)
#define BIT_STOP_VIQ_8197F BIT(11)
#define BIT_STOP_BEQ_8197F BIT(10)
#define BIT_STOP_BKQ_8197F BIT(9)
#define BIT_STOP_RXQ_8197F BIT(8)
#define BIT_STOP_HI7Q_8197F BIT(7)
#define BIT_STOP_HI6Q_8197F BIT(6)
#define BIT_STOP_HI5Q_8197F BIT(5)
#define BIT_STOP_HI4Q_8197F BIT(4)
#define BIT_STOP_HI3Q_8197F BIT(3)
#define BIT_STOP_HI2Q_8197F BIT(2)
#define BIT_STOP_HI1Q_8197F BIT(1)
#define BIT_STOP_HI0Q_8197F BIT(0)

/* 2 REG_INT_MIG_8197F */

#define BIT_SHIFT_TXTTIMER_MATCH_NUM_8197F 28
#define BIT_MASK_TXTTIMER_MATCH_NUM_8197F 0xf
#define BIT_TXTTIMER_MATCH_NUM_8197F(x)                                        \
	(((x) & BIT_MASK_TXTTIMER_MATCH_NUM_8197F)                             \
	 << BIT_SHIFT_TXTTIMER_MATCH_NUM_8197F)
#define BITS_TXTTIMER_MATCH_NUM_8197F                                          \
	(BIT_MASK_TXTTIMER_MATCH_NUM_8197F                                     \
	 << BIT_SHIFT_TXTTIMER_MATCH_NUM_8197F)
#define BIT_CLEAR_TXTTIMER_MATCH_NUM_8197F(x)                                  \
	((x) & (~BITS_TXTTIMER_MATCH_NUM_8197F))
#define BIT_GET_TXTTIMER_MATCH_NUM_8197F(x)                                    \
	(((x) >> BIT_SHIFT_TXTTIMER_MATCH_NUM_8197F) &                         \
	 BIT_MASK_TXTTIMER_MATCH_NUM_8197F)
#define BIT_SET_TXTTIMER_MATCH_NUM_8197F(x, v)                                 \
	(BIT_CLEAR_TXTTIMER_MATCH_NUM_8197F(x) |                               \
	 BIT_TXTTIMER_MATCH_NUM_8197F(v))

#define BIT_SHIFT_TXPKT_NUM_MATCH_8197F 24
#define BIT_MASK_TXPKT_NUM_MATCH_8197F 0xf
#define BIT_TXPKT_NUM_MATCH_8197F(x)                                           \
	(((x) & BIT_MASK_TXPKT_NUM_MATCH_8197F)                                \
	 << BIT_SHIFT_TXPKT_NUM_MATCH_8197F)
#define BITS_TXPKT_NUM_MATCH_8197F                                             \
	(BIT_MASK_TXPKT_NUM_MATCH_8197F << BIT_SHIFT_TXPKT_NUM_MATCH_8197F)
#define BIT_CLEAR_TXPKT_NUM_MATCH_8197F(x) ((x) & (~BITS_TXPKT_NUM_MATCH_8197F))
#define BIT_GET_TXPKT_NUM_MATCH_8197F(x)                                       \
	(((x) >> BIT_SHIFT_TXPKT_NUM_MATCH_8197F) &                            \
	 BIT_MASK_TXPKT_NUM_MATCH_8197F)
#define BIT_SET_TXPKT_NUM_MATCH_8197F(x, v)                                    \
	(BIT_CLEAR_TXPKT_NUM_MATCH_8197F(x) | BIT_TXPKT_NUM_MATCH_8197F(v))

#define BIT_SHIFT_RXTTIMER_MATCH_NUM_8197F 20
#define BIT_MASK_RXTTIMER_MATCH_NUM_8197F 0xf
#define BIT_RXTTIMER_MATCH_NUM_8197F(x)                                        \
	(((x) & BIT_MASK_RXTTIMER_MATCH_NUM_8197F)                             \
	 << BIT_SHIFT_RXTTIMER_MATCH_NUM_8197F)
#define BITS_RXTTIMER_MATCH_NUM_8197F                                          \
	(BIT_MASK_RXTTIMER_MATCH_NUM_8197F                                     \
	 << BIT_SHIFT_RXTTIMER_MATCH_NUM_8197F)
#define BIT_CLEAR_RXTTIMER_MATCH_NUM_8197F(x)                                  \
	((x) & (~BITS_RXTTIMER_MATCH_NUM_8197F))
#define BIT_GET_RXTTIMER_MATCH_NUM_8197F(x)                                    \
	(((x) >> BIT_SHIFT_RXTTIMER_MATCH_NUM_8197F) &                         \
	 BIT_MASK_RXTTIMER_MATCH_NUM_8197F)
#define BIT_SET_RXTTIMER_MATCH_NUM_8197F(x, v)                                 \
	(BIT_CLEAR_RXTTIMER_MATCH_NUM_8197F(x) |                               \
	 BIT_RXTTIMER_MATCH_NUM_8197F(v))

#define BIT_SHIFT_RXPKT_NUM_MATCH_8197F 16
#define BIT_MASK_RXPKT_NUM_MATCH_8197F 0xf
#define BIT_RXPKT_NUM_MATCH_8197F(x)                                           \
	(((x) & BIT_MASK_RXPKT_NUM_MATCH_8197F)                                \
	 << BIT_SHIFT_RXPKT_NUM_MATCH_8197F)
#define BITS_RXPKT_NUM_MATCH_8197F                                             \
	(BIT_MASK_RXPKT_NUM_MATCH_8197F << BIT_SHIFT_RXPKT_NUM_MATCH_8197F)
#define BIT_CLEAR_RXPKT_NUM_MATCH_8197F(x) ((x) & (~BITS_RXPKT_NUM_MATCH_8197F))
#define BIT_GET_RXPKT_NUM_MATCH_8197F(x)                                       \
	(((x) >> BIT_SHIFT_RXPKT_NUM_MATCH_8197F) &                            \
	 BIT_MASK_RXPKT_NUM_MATCH_8197F)
#define BIT_SET_RXPKT_NUM_MATCH_8197F(x, v)                                    \
	(BIT_CLEAR_RXPKT_NUM_MATCH_8197F(x) | BIT_RXPKT_NUM_MATCH_8197F(v))

#define BIT_SHIFT_MIGRATE_TIMER_8197F 0
#define BIT_MASK_MIGRATE_TIMER_8197F 0xffff
#define BIT_MIGRATE_TIMER_8197F(x)                                             \
	(((x) & BIT_MASK_MIGRATE_TIMER_8197F) << BIT_SHIFT_MIGRATE_TIMER_8197F)
#define BITS_MIGRATE_TIMER_8197F                                               \
	(BIT_MASK_MIGRATE_TIMER_8197F << BIT_SHIFT_MIGRATE_TIMER_8197F)
#define BIT_CLEAR_MIGRATE_TIMER_8197F(x) ((x) & (~BITS_MIGRATE_TIMER_8197F))
#define BIT_GET_MIGRATE_TIMER_8197F(x)                                         \
	(((x) >> BIT_SHIFT_MIGRATE_TIMER_8197F) & BIT_MASK_MIGRATE_TIMER_8197F)
#define BIT_SET_MIGRATE_TIMER_8197F(x, v)                                      \
	(BIT_CLEAR_MIGRATE_TIMER_8197F(x) | BIT_MIGRATE_TIMER_8197F(v))

/* 2 REG_BCNQ_TXBD_DESA_8197F */

#define BIT_SHIFT_BCNQ_TXBD_DESA_8197F 0
#define BIT_MASK_BCNQ_TXBD_DESA_8197F 0xffffffffffffffffL
#define BIT_BCNQ_TXBD_DESA_8197F(x)                                            \
	(((x) & BIT_MASK_BCNQ_TXBD_DESA_8197F)                                 \
	 << BIT_SHIFT_BCNQ_TXBD_DESA_8197F)
#define BITS_BCNQ_TXBD_DESA_8197F                                              \
	(BIT_MASK_BCNQ_TXBD_DESA_8197F << BIT_SHIFT_BCNQ_TXBD_DESA_8197F)
#define BIT_CLEAR_BCNQ_TXBD_DESA_8197F(x) ((x) & (~BITS_BCNQ_TXBD_DESA_8197F))
#define BIT_GET_BCNQ_TXBD_DESA_8197F(x)                                        \
	(((x) >> BIT_SHIFT_BCNQ_TXBD_DESA_8197F) &                             \
	 BIT_MASK_BCNQ_TXBD_DESA_8197F)
#define BIT_SET_BCNQ_TXBD_DESA_8197F(x, v)                                     \
	(BIT_CLEAR_BCNQ_TXBD_DESA_8197F(x) | BIT_BCNQ_TXBD_DESA_8197F(v))

/* 2 REG_MGQ_TXBD_DESA_8197F */

#define BIT_SHIFT_MGQ_TXBD_DESA_8197F 0
#define BIT_MASK_MGQ_TXBD_DESA_8197F 0xffffffffffffffffL
#define BIT_MGQ_TXBD_DESA_8197F(x)                                             \
	(((x) & BIT_MASK_MGQ_TXBD_DESA_8197F) << BIT_SHIFT_MGQ_TXBD_DESA_8197F)
#define BITS_MGQ_TXBD_DESA_8197F                                               \
	(BIT_MASK_MGQ_TXBD_DESA_8197F << BIT_SHIFT_MGQ_TXBD_DESA_8197F)
#define BIT_CLEAR_MGQ_TXBD_DESA_8197F(x) ((x) & (~BITS_MGQ_TXBD_DESA_8197F))
#define BIT_GET_MGQ_TXBD_DESA_8197F(x)                                         \
	(((x) >> BIT_SHIFT_MGQ_TXBD_DESA_8197F) & BIT_MASK_MGQ_TXBD_DESA_8197F)
#define BIT_SET_MGQ_TXBD_DESA_8197F(x, v)                                      \
	(BIT_CLEAR_MGQ_TXBD_DESA_8197F(x) | BIT_MGQ_TXBD_DESA_8197F(v))

/* 2 REG_VOQ_TXBD_DESA_8197F */

#define BIT_SHIFT_VOQ_TXBD_DESA_8197F 0
#define BIT_MASK_VOQ_TXBD_DESA_8197F 0xffffffffffffffffL
#define BIT_VOQ_TXBD_DESA_8197F(x)                                             \
	(((x) & BIT_MASK_VOQ_TXBD_DESA_8197F) << BIT_SHIFT_VOQ_TXBD_DESA_8197F)
#define BITS_VOQ_TXBD_DESA_8197F                                               \
	(BIT_MASK_VOQ_TXBD_DESA_8197F << BIT_SHIFT_VOQ_TXBD_DESA_8197F)
#define BIT_CLEAR_VOQ_TXBD_DESA_8197F(x) ((x) & (~BITS_VOQ_TXBD_DESA_8197F))
#define BIT_GET_VOQ_TXBD_DESA_8197F(x)                                         \
	(((x) >> BIT_SHIFT_VOQ_TXBD_DESA_8197F) & BIT_MASK_VOQ_TXBD_DESA_8197F)
#define BIT_SET_VOQ_TXBD_DESA_8197F(x, v)                                      \
	(BIT_CLEAR_VOQ_TXBD_DESA_8197F(x) | BIT_VOQ_TXBD_DESA_8197F(v))

/* 2 REG_VIQ_TXBD_DESA_8197F */

#define BIT_SHIFT_VIQ_TXBD_DESA_8197F 0
#define BIT_MASK_VIQ_TXBD_DESA_8197F 0xffffffffffffffffL
#define BIT_VIQ_TXBD_DESA_8197F(x)                                             \
	(((x) & BIT_MASK_VIQ_TXBD_DESA_8197F) << BIT_SHIFT_VIQ_TXBD_DESA_8197F)
#define BITS_VIQ_TXBD_DESA_8197F                                               \
	(BIT_MASK_VIQ_TXBD_DESA_8197F << BIT_SHIFT_VIQ_TXBD_DESA_8197F)
#define BIT_CLEAR_VIQ_TXBD_DESA_8197F(x) ((x) & (~BITS_VIQ_TXBD_DESA_8197F))
#define BIT_GET_VIQ_TXBD_DESA_8197F(x)                                         \
	(((x) >> BIT_SHIFT_VIQ_TXBD_DESA_8197F) & BIT_MASK_VIQ_TXBD_DESA_8197F)
#define BIT_SET_VIQ_TXBD_DESA_8197F(x, v)                                      \
	(BIT_CLEAR_VIQ_TXBD_DESA_8197F(x) | BIT_VIQ_TXBD_DESA_8197F(v))

/* 2 REG_BEQ_TXBD_DESA_8197F */

#define BIT_SHIFT_BEQ_TXBD_DESA_8197F 0
#define BIT_MASK_BEQ_TXBD_DESA_8197F 0xffffffffffffffffL
#define BIT_BEQ_TXBD_DESA_8197F(x)                                             \
	(((x) & BIT_MASK_BEQ_TXBD_DESA_8197F) << BIT_SHIFT_BEQ_TXBD_DESA_8197F)
#define BITS_BEQ_TXBD_DESA_8197F                                               \
	(BIT_MASK_BEQ_TXBD_DESA_8197F << BIT_SHIFT_BEQ_TXBD_DESA_8197F)
#define BIT_CLEAR_BEQ_TXBD_DESA_8197F(x) ((x) & (~BITS_BEQ_TXBD_DESA_8197F))
#define BIT_GET_BEQ_TXBD_DESA_8197F(x)                                         \
	(((x) >> BIT_SHIFT_BEQ_TXBD_DESA_8197F) & BIT_MASK_BEQ_TXBD_DESA_8197F)
#define BIT_SET_BEQ_TXBD_DESA_8197F(x, v)                                      \
	(BIT_CLEAR_BEQ_TXBD_DESA_8197F(x) | BIT_BEQ_TXBD_DESA_8197F(v))

/* 2 REG_BKQ_TXBD_DESA_8197F */

#define BIT_SHIFT_BKQ_TXBD_DESA_8197F 0
#define BIT_MASK_BKQ_TXBD_DESA_8197F 0xffffffffffffffffL
#define BIT_BKQ_TXBD_DESA_8197F(x)                                             \
	(((x) & BIT_MASK_BKQ_TXBD_DESA_8197F) << BIT_SHIFT_BKQ_TXBD_DESA_8197F)
#define BITS_BKQ_TXBD_DESA_8197F                                               \
	(BIT_MASK_BKQ_TXBD_DESA_8197F << BIT_SHIFT_BKQ_TXBD_DESA_8197F)
#define BIT_CLEAR_BKQ_TXBD_DESA_8197F(x) ((x) & (~BITS_BKQ_TXBD_DESA_8197F))
#define BIT_GET_BKQ_TXBD_DESA_8197F(x)                                         \
	(((x) >> BIT_SHIFT_BKQ_TXBD_DESA_8197F) & BIT_MASK_BKQ_TXBD_DESA_8197F)
#define BIT_SET_BKQ_TXBD_DESA_8197F(x, v)                                      \
	(BIT_CLEAR_BKQ_TXBD_DESA_8197F(x) | BIT_BKQ_TXBD_DESA_8197F(v))

/* 2 REG_RXQ_RXBD_DESA_8197F */

#define BIT_SHIFT_RXQ_RXBD_DESA_8197F 0
#define BIT_MASK_RXQ_RXBD_DESA_8197F 0xffffffffffffffffL
#define BIT_RXQ_RXBD_DESA_8197F(x)                                             \
	(((x) & BIT_MASK_RXQ_RXBD_DESA_8197F) << BIT_SHIFT_RXQ_RXBD_DESA_8197F)
#define BITS_RXQ_RXBD_DESA_8197F                                               \
	(BIT_MASK_RXQ_RXBD_DESA_8197F << BIT_SHIFT_RXQ_RXBD_DESA_8197F)
#define BIT_CLEAR_RXQ_RXBD_DESA_8197F(x) ((x) & (~BITS_RXQ_RXBD_DESA_8197F))
#define BIT_GET_RXQ_RXBD_DESA_8197F(x)                                         \
	(((x) >> BIT_SHIFT_RXQ_RXBD_DESA_8197F) & BIT_MASK_RXQ_RXBD_DESA_8197F)
#define BIT_SET_RXQ_RXBD_DESA_8197F(x, v)                                      \
	(BIT_CLEAR_RXQ_RXBD_DESA_8197F(x) | BIT_RXQ_RXBD_DESA_8197F(v))

/* 2 REG_HI0Q_TXBD_DESA_8197F */

#define BIT_SHIFT_HI0Q_TXBD_DESA_8197F 0
#define BIT_MASK_HI0Q_TXBD_DESA_8197F 0xffffffffffffffffL
#define BIT_HI0Q_TXBD_DESA_8197F(x)                                            \
	(((x) & BIT_MASK_HI0Q_TXBD_DESA_8197F)                                 \
	 << BIT_SHIFT_HI0Q_TXBD_DESA_8197F)
#define BITS_HI0Q_TXBD_DESA_8197F                                              \
	(BIT_MASK_HI0Q_TXBD_DESA_8197F << BIT_SHIFT_HI0Q_TXBD_DESA_8197F)
#define BIT_CLEAR_HI0Q_TXBD_DESA_8197F(x) ((x) & (~BITS_HI0Q_TXBD_DESA_8197F))
#define BIT_GET_HI0Q_TXBD_DESA_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HI0Q_TXBD_DESA_8197F) &                             \
	 BIT_MASK_HI0Q_TXBD_DESA_8197F)
#define BIT_SET_HI0Q_TXBD_DESA_8197F(x, v)                                     \
	(BIT_CLEAR_HI0Q_TXBD_DESA_8197F(x) | BIT_HI0Q_TXBD_DESA_8197F(v))

/* 2 REG_HI1Q_TXBD_DESA_8197F */

#define BIT_SHIFT_HI1Q_TXBD_DESA_8197F 0
#define BIT_MASK_HI1Q_TXBD_DESA_8197F 0xffffffffffffffffL
#define BIT_HI1Q_TXBD_DESA_8197F(x)                                            \
	(((x) & BIT_MASK_HI1Q_TXBD_DESA_8197F)                                 \
	 << BIT_SHIFT_HI1Q_TXBD_DESA_8197F)
#define BITS_HI1Q_TXBD_DESA_8197F                                              \
	(BIT_MASK_HI1Q_TXBD_DESA_8197F << BIT_SHIFT_HI1Q_TXBD_DESA_8197F)
#define BIT_CLEAR_HI1Q_TXBD_DESA_8197F(x) ((x) & (~BITS_HI1Q_TXBD_DESA_8197F))
#define BIT_GET_HI1Q_TXBD_DESA_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HI1Q_TXBD_DESA_8197F) &                             \
	 BIT_MASK_HI1Q_TXBD_DESA_8197F)
#define BIT_SET_HI1Q_TXBD_DESA_8197F(x, v)                                     \
	(BIT_CLEAR_HI1Q_TXBD_DESA_8197F(x) | BIT_HI1Q_TXBD_DESA_8197F(v))

/* 2 REG_HI2Q_TXBD_DESA_8197F */

#define BIT_SHIFT_HI2Q_TXBD_DESA_8197F 0
#define BIT_MASK_HI2Q_TXBD_DESA_8197F 0xffffffffffffffffL
#define BIT_HI2Q_TXBD_DESA_8197F(x)                                            \
	(((x) & BIT_MASK_HI2Q_TXBD_DESA_8197F)                                 \
	 << BIT_SHIFT_HI2Q_TXBD_DESA_8197F)
#define BITS_HI2Q_TXBD_DESA_8197F                                              \
	(BIT_MASK_HI2Q_TXBD_DESA_8197F << BIT_SHIFT_HI2Q_TXBD_DESA_8197F)
#define BIT_CLEAR_HI2Q_TXBD_DESA_8197F(x) ((x) & (~BITS_HI2Q_TXBD_DESA_8197F))
#define BIT_GET_HI2Q_TXBD_DESA_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HI2Q_TXBD_DESA_8197F) &                             \
	 BIT_MASK_HI2Q_TXBD_DESA_8197F)
#define BIT_SET_HI2Q_TXBD_DESA_8197F(x, v)                                     \
	(BIT_CLEAR_HI2Q_TXBD_DESA_8197F(x) | BIT_HI2Q_TXBD_DESA_8197F(v))

/* 2 REG_HI3Q_TXBD_DESA_8197F */

#define BIT_SHIFT_HI3Q_TXBD_DESA_8197F 0
#define BIT_MASK_HI3Q_TXBD_DESA_8197F 0xffffffffffffffffL
#define BIT_HI3Q_TXBD_DESA_8197F(x)                                            \
	(((x) & BIT_MASK_HI3Q_TXBD_DESA_8197F)                                 \
	 << BIT_SHIFT_HI3Q_TXBD_DESA_8197F)
#define BITS_HI3Q_TXBD_DESA_8197F                                              \
	(BIT_MASK_HI3Q_TXBD_DESA_8197F << BIT_SHIFT_HI3Q_TXBD_DESA_8197F)
#define BIT_CLEAR_HI3Q_TXBD_DESA_8197F(x) ((x) & (~BITS_HI3Q_TXBD_DESA_8197F))
#define BIT_GET_HI3Q_TXBD_DESA_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HI3Q_TXBD_DESA_8197F) &                             \
	 BIT_MASK_HI3Q_TXBD_DESA_8197F)
#define BIT_SET_HI3Q_TXBD_DESA_8197F(x, v)                                     \
	(BIT_CLEAR_HI3Q_TXBD_DESA_8197F(x) | BIT_HI3Q_TXBD_DESA_8197F(v))

/* 2 REG_HI4Q_TXBD_DESA_8197F */

#define BIT_SHIFT_HI4Q_TXBD_DESA_8197F 0
#define BIT_MASK_HI4Q_TXBD_DESA_8197F 0xffffffffffffffffL
#define BIT_HI4Q_TXBD_DESA_8197F(x)                                            \
	(((x) & BIT_MASK_HI4Q_TXBD_DESA_8197F)                                 \
	 << BIT_SHIFT_HI4Q_TXBD_DESA_8197F)
#define BITS_HI4Q_TXBD_DESA_8197F                                              \
	(BIT_MASK_HI4Q_TXBD_DESA_8197F << BIT_SHIFT_HI4Q_TXBD_DESA_8197F)
#define BIT_CLEAR_HI4Q_TXBD_DESA_8197F(x) ((x) & (~BITS_HI4Q_TXBD_DESA_8197F))
#define BIT_GET_HI4Q_TXBD_DESA_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HI4Q_TXBD_DESA_8197F) &                             \
	 BIT_MASK_HI4Q_TXBD_DESA_8197F)
#define BIT_SET_HI4Q_TXBD_DESA_8197F(x, v)                                     \
	(BIT_CLEAR_HI4Q_TXBD_DESA_8197F(x) | BIT_HI4Q_TXBD_DESA_8197F(v))

/* 2 REG_HI5Q_TXBD_DESA_8197F */

#define BIT_SHIFT_HI5Q_TXBD_DESA_8197F 0
#define BIT_MASK_HI5Q_TXBD_DESA_8197F 0xffffffffffffffffL
#define BIT_HI5Q_TXBD_DESA_8197F(x)                                            \
	(((x) & BIT_MASK_HI5Q_TXBD_DESA_8197F)                                 \
	 << BIT_SHIFT_HI5Q_TXBD_DESA_8197F)
#define BITS_HI5Q_TXBD_DESA_8197F                                              \
	(BIT_MASK_HI5Q_TXBD_DESA_8197F << BIT_SHIFT_HI5Q_TXBD_DESA_8197F)
#define BIT_CLEAR_HI5Q_TXBD_DESA_8197F(x) ((x) & (~BITS_HI5Q_TXBD_DESA_8197F))
#define BIT_GET_HI5Q_TXBD_DESA_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HI5Q_TXBD_DESA_8197F) &                             \
	 BIT_MASK_HI5Q_TXBD_DESA_8197F)
#define BIT_SET_HI5Q_TXBD_DESA_8197F(x, v)                                     \
	(BIT_CLEAR_HI5Q_TXBD_DESA_8197F(x) | BIT_HI5Q_TXBD_DESA_8197F(v))

/* 2 REG_HI6Q_TXBD_DESA_8197F */

#define BIT_SHIFT_HI6Q_TXBD_DESA_8197F 0
#define BIT_MASK_HI6Q_TXBD_DESA_8197F 0xffffffffffffffffL
#define BIT_HI6Q_TXBD_DESA_8197F(x)                                            \
	(((x) & BIT_MASK_HI6Q_TXBD_DESA_8197F)                                 \
	 << BIT_SHIFT_HI6Q_TXBD_DESA_8197F)
#define BITS_HI6Q_TXBD_DESA_8197F                                              \
	(BIT_MASK_HI6Q_TXBD_DESA_8197F << BIT_SHIFT_HI6Q_TXBD_DESA_8197F)
#define BIT_CLEAR_HI6Q_TXBD_DESA_8197F(x) ((x) & (~BITS_HI6Q_TXBD_DESA_8197F))
#define BIT_GET_HI6Q_TXBD_DESA_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HI6Q_TXBD_DESA_8197F) &                             \
	 BIT_MASK_HI6Q_TXBD_DESA_8197F)
#define BIT_SET_HI6Q_TXBD_DESA_8197F(x, v)                                     \
	(BIT_CLEAR_HI6Q_TXBD_DESA_8197F(x) | BIT_HI6Q_TXBD_DESA_8197F(v))

/* 2 REG_HI7Q_TXBD_DESA_8197F */

#define BIT_SHIFT_HI7Q_TXBD_DESA_8197F 0
#define BIT_MASK_HI7Q_TXBD_DESA_8197F 0xffffffffffffffffL
#define BIT_HI7Q_TXBD_DESA_8197F(x)                                            \
	(((x) & BIT_MASK_HI7Q_TXBD_DESA_8197F)                                 \
	 << BIT_SHIFT_HI7Q_TXBD_DESA_8197F)
#define BITS_HI7Q_TXBD_DESA_8197F                                              \
	(BIT_MASK_HI7Q_TXBD_DESA_8197F << BIT_SHIFT_HI7Q_TXBD_DESA_8197F)
#define BIT_CLEAR_HI7Q_TXBD_DESA_8197F(x) ((x) & (~BITS_HI7Q_TXBD_DESA_8197F))
#define BIT_GET_HI7Q_TXBD_DESA_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HI7Q_TXBD_DESA_8197F) &                             \
	 BIT_MASK_HI7Q_TXBD_DESA_8197F)
#define BIT_SET_HI7Q_TXBD_DESA_8197F(x, v)                                     \
	(BIT_CLEAR_HI7Q_TXBD_DESA_8197F(x) | BIT_HI7Q_TXBD_DESA_8197F(v))

/* 2 REG_MGQ_TXBD_NUM_8197F */
#define BIT_HCI_MGQ_FLAG_8197F BIT(14)

#define BIT_SHIFT_MGQ_DESC_MODE_8197F 12
#define BIT_MASK_MGQ_DESC_MODE_8197F 0x3
#define BIT_MGQ_DESC_MODE_8197F(x)                                             \
	(((x) & BIT_MASK_MGQ_DESC_MODE_8197F) << BIT_SHIFT_MGQ_DESC_MODE_8197F)
#define BITS_MGQ_DESC_MODE_8197F                                               \
	(BIT_MASK_MGQ_DESC_MODE_8197F << BIT_SHIFT_MGQ_DESC_MODE_8197F)
#define BIT_CLEAR_MGQ_DESC_MODE_8197F(x) ((x) & (~BITS_MGQ_DESC_MODE_8197F))
#define BIT_GET_MGQ_DESC_MODE_8197F(x)                                         \
	(((x) >> BIT_SHIFT_MGQ_DESC_MODE_8197F) & BIT_MASK_MGQ_DESC_MODE_8197F)
#define BIT_SET_MGQ_DESC_MODE_8197F(x, v)                                      \
	(BIT_CLEAR_MGQ_DESC_MODE_8197F(x) | BIT_MGQ_DESC_MODE_8197F(v))

#define BIT_SHIFT_MGQ_DESC_NUM_8197F 0
#define BIT_MASK_MGQ_DESC_NUM_8197F 0xfff
#define BIT_MGQ_DESC_NUM_8197F(x)                                              \
	(((x) & BIT_MASK_MGQ_DESC_NUM_8197F) << BIT_SHIFT_MGQ_DESC_NUM_8197F)
#define BITS_MGQ_DESC_NUM_8197F                                                \
	(BIT_MASK_MGQ_DESC_NUM_8197F << BIT_SHIFT_MGQ_DESC_NUM_8197F)
#define BIT_CLEAR_MGQ_DESC_NUM_8197F(x) ((x) & (~BITS_MGQ_DESC_NUM_8197F))
#define BIT_GET_MGQ_DESC_NUM_8197F(x)                                          \
	(((x) >> BIT_SHIFT_MGQ_DESC_NUM_8197F) & BIT_MASK_MGQ_DESC_NUM_8197F)
#define BIT_SET_MGQ_DESC_NUM_8197F(x, v)                                       \
	(BIT_CLEAR_MGQ_DESC_NUM_8197F(x) | BIT_MGQ_DESC_NUM_8197F(v))

/* 2 REG_RX_RXBD_NUM_8197F */
#define BIT_SYS_32_64_8197F BIT(15)

#define BIT_SHIFT_BCNQ_DESC_MODE_8197F 13
#define BIT_MASK_BCNQ_DESC_MODE_8197F 0x3
#define BIT_BCNQ_DESC_MODE_8197F(x)                                            \
	(((x) & BIT_MASK_BCNQ_DESC_MODE_8197F)                                 \
	 << BIT_SHIFT_BCNQ_DESC_MODE_8197F)
#define BITS_BCNQ_DESC_MODE_8197F                                              \
	(BIT_MASK_BCNQ_DESC_MODE_8197F << BIT_SHIFT_BCNQ_DESC_MODE_8197F)
#define BIT_CLEAR_BCNQ_DESC_MODE_8197F(x) ((x) & (~BITS_BCNQ_DESC_MODE_8197F))
#define BIT_GET_BCNQ_DESC_MODE_8197F(x)                                        \
	(((x) >> BIT_SHIFT_BCNQ_DESC_MODE_8197F) &                             \
	 BIT_MASK_BCNQ_DESC_MODE_8197F)
#define BIT_SET_BCNQ_DESC_MODE_8197F(x, v)                                     \
	(BIT_CLEAR_BCNQ_DESC_MODE_8197F(x) | BIT_BCNQ_DESC_MODE_8197F(v))

#define BIT_HCI_BCNQ_FLAG_8197F BIT(12)

#define BIT_SHIFT_RXQ_DESC_NUM_8197F 0
#define BIT_MASK_RXQ_DESC_NUM_8197F 0xfff
#define BIT_RXQ_DESC_NUM_8197F(x)                                              \
	(((x) & BIT_MASK_RXQ_DESC_NUM_8197F) << BIT_SHIFT_RXQ_DESC_NUM_8197F)
#define BITS_RXQ_DESC_NUM_8197F                                                \
	(BIT_MASK_RXQ_DESC_NUM_8197F << BIT_SHIFT_RXQ_DESC_NUM_8197F)
#define BIT_CLEAR_RXQ_DESC_NUM_8197F(x) ((x) & (~BITS_RXQ_DESC_NUM_8197F))
#define BIT_GET_RXQ_DESC_NUM_8197F(x)                                          \
	(((x) >> BIT_SHIFT_RXQ_DESC_NUM_8197F) & BIT_MASK_RXQ_DESC_NUM_8197F)
#define BIT_SET_RXQ_DESC_NUM_8197F(x, v)                                       \
	(BIT_CLEAR_RXQ_DESC_NUM_8197F(x) | BIT_RXQ_DESC_NUM_8197F(v))

/* 2 REG_VOQ_TXBD_NUM_8197F */
#define BIT_HCI_VOQ_FLAG_8197F BIT(14)

#define BIT_SHIFT_VOQ_DESC_MODE_8197F 12
#define BIT_MASK_VOQ_DESC_MODE_8197F 0x3
#define BIT_VOQ_DESC_MODE_8197F(x)                                             \
	(((x) & BIT_MASK_VOQ_DESC_MODE_8197F) << BIT_SHIFT_VOQ_DESC_MODE_8197F)
#define BITS_VOQ_DESC_MODE_8197F                                               \
	(BIT_MASK_VOQ_DESC_MODE_8197F << BIT_SHIFT_VOQ_DESC_MODE_8197F)
#define BIT_CLEAR_VOQ_DESC_MODE_8197F(x) ((x) & (~BITS_VOQ_DESC_MODE_8197F))
#define BIT_GET_VOQ_DESC_MODE_8197F(x)                                         \
	(((x) >> BIT_SHIFT_VOQ_DESC_MODE_8197F) & BIT_MASK_VOQ_DESC_MODE_8197F)
#define BIT_SET_VOQ_DESC_MODE_8197F(x, v)                                      \
	(BIT_CLEAR_VOQ_DESC_MODE_8197F(x) | BIT_VOQ_DESC_MODE_8197F(v))

#define BIT_SHIFT_VOQ_DESC_NUM_8197F 0
#define BIT_MASK_VOQ_DESC_NUM_8197F 0xfff
#define BIT_VOQ_DESC_NUM_8197F(x)                                              \
	(((x) & BIT_MASK_VOQ_DESC_NUM_8197F) << BIT_SHIFT_VOQ_DESC_NUM_8197F)
#define BITS_VOQ_DESC_NUM_8197F                                                \
	(BIT_MASK_VOQ_DESC_NUM_8197F << BIT_SHIFT_VOQ_DESC_NUM_8197F)
#define BIT_CLEAR_VOQ_DESC_NUM_8197F(x) ((x) & (~BITS_VOQ_DESC_NUM_8197F))
#define BIT_GET_VOQ_DESC_NUM_8197F(x)                                          \
	(((x) >> BIT_SHIFT_VOQ_DESC_NUM_8197F) & BIT_MASK_VOQ_DESC_NUM_8197F)
#define BIT_SET_VOQ_DESC_NUM_8197F(x, v)                                       \
	(BIT_CLEAR_VOQ_DESC_NUM_8197F(x) | BIT_VOQ_DESC_NUM_8197F(v))

/* 2 REG_VIQ_TXBD_NUM_8197F */
#define BIT_HCI_VIQ_FLAG_8197F BIT(14)

#define BIT_SHIFT_VIQ_DESC_MODE_8197F 12
#define BIT_MASK_VIQ_DESC_MODE_8197F 0x3
#define BIT_VIQ_DESC_MODE_8197F(x)                                             \
	(((x) & BIT_MASK_VIQ_DESC_MODE_8197F) << BIT_SHIFT_VIQ_DESC_MODE_8197F)
#define BITS_VIQ_DESC_MODE_8197F                                               \
	(BIT_MASK_VIQ_DESC_MODE_8197F << BIT_SHIFT_VIQ_DESC_MODE_8197F)
#define BIT_CLEAR_VIQ_DESC_MODE_8197F(x) ((x) & (~BITS_VIQ_DESC_MODE_8197F))
#define BIT_GET_VIQ_DESC_MODE_8197F(x)                                         \
	(((x) >> BIT_SHIFT_VIQ_DESC_MODE_8197F) & BIT_MASK_VIQ_DESC_MODE_8197F)
#define BIT_SET_VIQ_DESC_MODE_8197F(x, v)                                      \
	(BIT_CLEAR_VIQ_DESC_MODE_8197F(x) | BIT_VIQ_DESC_MODE_8197F(v))

#define BIT_SHIFT_VIQ_DESC_NUM_8197F 0
#define BIT_MASK_VIQ_DESC_NUM_8197F 0xfff
#define BIT_VIQ_DESC_NUM_8197F(x)                                              \
	(((x) & BIT_MASK_VIQ_DESC_NUM_8197F) << BIT_SHIFT_VIQ_DESC_NUM_8197F)
#define BITS_VIQ_DESC_NUM_8197F                                                \
	(BIT_MASK_VIQ_DESC_NUM_8197F << BIT_SHIFT_VIQ_DESC_NUM_8197F)
#define BIT_CLEAR_VIQ_DESC_NUM_8197F(x) ((x) & (~BITS_VIQ_DESC_NUM_8197F))
#define BIT_GET_VIQ_DESC_NUM_8197F(x)                                          \
	(((x) >> BIT_SHIFT_VIQ_DESC_NUM_8197F) & BIT_MASK_VIQ_DESC_NUM_8197F)
#define BIT_SET_VIQ_DESC_NUM_8197F(x, v)                                       \
	(BIT_CLEAR_VIQ_DESC_NUM_8197F(x) | BIT_VIQ_DESC_NUM_8197F(v))

/* 2 REG_BEQ_TXBD_NUM_8197F */
#define BIT_HCI_BEQ_FLAG_8197F BIT(14)

#define BIT_SHIFT_BEQ_DESC_MODE_8197F 12
#define BIT_MASK_BEQ_DESC_MODE_8197F 0x3
#define BIT_BEQ_DESC_MODE_8197F(x)                                             \
	(((x) & BIT_MASK_BEQ_DESC_MODE_8197F) << BIT_SHIFT_BEQ_DESC_MODE_8197F)
#define BITS_BEQ_DESC_MODE_8197F                                               \
	(BIT_MASK_BEQ_DESC_MODE_8197F << BIT_SHIFT_BEQ_DESC_MODE_8197F)
#define BIT_CLEAR_BEQ_DESC_MODE_8197F(x) ((x) & (~BITS_BEQ_DESC_MODE_8197F))
#define BIT_GET_BEQ_DESC_MODE_8197F(x)                                         \
	(((x) >> BIT_SHIFT_BEQ_DESC_MODE_8197F) & BIT_MASK_BEQ_DESC_MODE_8197F)
#define BIT_SET_BEQ_DESC_MODE_8197F(x, v)                                      \
	(BIT_CLEAR_BEQ_DESC_MODE_8197F(x) | BIT_BEQ_DESC_MODE_8197F(v))

#define BIT_SHIFT_BEQ_DESC_NUM_8197F 0
#define BIT_MASK_BEQ_DESC_NUM_8197F 0xfff
#define BIT_BEQ_DESC_NUM_8197F(x)                                              \
	(((x) & BIT_MASK_BEQ_DESC_NUM_8197F) << BIT_SHIFT_BEQ_DESC_NUM_8197F)
#define BITS_BEQ_DESC_NUM_8197F                                                \
	(BIT_MASK_BEQ_DESC_NUM_8197F << BIT_SHIFT_BEQ_DESC_NUM_8197F)
#define BIT_CLEAR_BEQ_DESC_NUM_8197F(x) ((x) & (~BITS_BEQ_DESC_NUM_8197F))
#define BIT_GET_BEQ_DESC_NUM_8197F(x)                                          \
	(((x) >> BIT_SHIFT_BEQ_DESC_NUM_8197F) & BIT_MASK_BEQ_DESC_NUM_8197F)
#define BIT_SET_BEQ_DESC_NUM_8197F(x, v)                                       \
	(BIT_CLEAR_BEQ_DESC_NUM_8197F(x) | BIT_BEQ_DESC_NUM_8197F(v))

/* 2 REG_BKQ_TXBD_NUM_8197F */
#define BIT_HCI_BKQ_FLAG_8197F BIT(14)

#define BIT_SHIFT_BKQ_DESC_MODE_8197F 12
#define BIT_MASK_BKQ_DESC_MODE_8197F 0x3
#define BIT_BKQ_DESC_MODE_8197F(x)                                             \
	(((x) & BIT_MASK_BKQ_DESC_MODE_8197F) << BIT_SHIFT_BKQ_DESC_MODE_8197F)
#define BITS_BKQ_DESC_MODE_8197F                                               \
	(BIT_MASK_BKQ_DESC_MODE_8197F << BIT_SHIFT_BKQ_DESC_MODE_8197F)
#define BIT_CLEAR_BKQ_DESC_MODE_8197F(x) ((x) & (~BITS_BKQ_DESC_MODE_8197F))
#define BIT_GET_BKQ_DESC_MODE_8197F(x)                                         \
	(((x) >> BIT_SHIFT_BKQ_DESC_MODE_8197F) & BIT_MASK_BKQ_DESC_MODE_8197F)
#define BIT_SET_BKQ_DESC_MODE_8197F(x, v)                                      \
	(BIT_CLEAR_BKQ_DESC_MODE_8197F(x) | BIT_BKQ_DESC_MODE_8197F(v))

#define BIT_SHIFT_BKQ_DESC_NUM_8197F 0
#define BIT_MASK_BKQ_DESC_NUM_8197F 0xfff
#define BIT_BKQ_DESC_NUM_8197F(x)                                              \
	(((x) & BIT_MASK_BKQ_DESC_NUM_8197F) << BIT_SHIFT_BKQ_DESC_NUM_8197F)
#define BITS_BKQ_DESC_NUM_8197F                                                \
	(BIT_MASK_BKQ_DESC_NUM_8197F << BIT_SHIFT_BKQ_DESC_NUM_8197F)
#define BIT_CLEAR_BKQ_DESC_NUM_8197F(x) ((x) & (~BITS_BKQ_DESC_NUM_8197F))
#define BIT_GET_BKQ_DESC_NUM_8197F(x)                                          \
	(((x) >> BIT_SHIFT_BKQ_DESC_NUM_8197F) & BIT_MASK_BKQ_DESC_NUM_8197F)
#define BIT_SET_BKQ_DESC_NUM_8197F(x, v)                                       \
	(BIT_CLEAR_BKQ_DESC_NUM_8197F(x) | BIT_BKQ_DESC_NUM_8197F(v))

/* 2 REG_HI0Q_TXBD_NUM_8197F */
#define BIT_HI0Q_FLAG_8197F BIT(14)

#define BIT_SHIFT_HI0Q_DESC_MODE_8197F 12
#define BIT_MASK_HI0Q_DESC_MODE_8197F 0x3
#define BIT_HI0Q_DESC_MODE_8197F(x)                                            \
	(((x) & BIT_MASK_HI0Q_DESC_MODE_8197F)                                 \
	 << BIT_SHIFT_HI0Q_DESC_MODE_8197F)
#define BITS_HI0Q_DESC_MODE_8197F                                              \
	(BIT_MASK_HI0Q_DESC_MODE_8197F << BIT_SHIFT_HI0Q_DESC_MODE_8197F)
#define BIT_CLEAR_HI0Q_DESC_MODE_8197F(x) ((x) & (~BITS_HI0Q_DESC_MODE_8197F))
#define BIT_GET_HI0Q_DESC_MODE_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HI0Q_DESC_MODE_8197F) &                             \
	 BIT_MASK_HI0Q_DESC_MODE_8197F)
#define BIT_SET_HI0Q_DESC_MODE_8197F(x, v)                                     \
	(BIT_CLEAR_HI0Q_DESC_MODE_8197F(x) | BIT_HI0Q_DESC_MODE_8197F(v))

#define BIT_SHIFT_HI0Q_DESC_NUM_8197F 0
#define BIT_MASK_HI0Q_DESC_NUM_8197F 0xfff
#define BIT_HI0Q_DESC_NUM_8197F(x)                                             \
	(((x) & BIT_MASK_HI0Q_DESC_NUM_8197F) << BIT_SHIFT_HI0Q_DESC_NUM_8197F)
#define BITS_HI0Q_DESC_NUM_8197F                                               \
	(BIT_MASK_HI0Q_DESC_NUM_8197F << BIT_SHIFT_HI0Q_DESC_NUM_8197F)
#define BIT_CLEAR_HI0Q_DESC_NUM_8197F(x) ((x) & (~BITS_HI0Q_DESC_NUM_8197F))
#define BIT_GET_HI0Q_DESC_NUM_8197F(x)                                         \
	(((x) >> BIT_SHIFT_HI0Q_DESC_NUM_8197F) & BIT_MASK_HI0Q_DESC_NUM_8197F)
#define BIT_SET_HI0Q_DESC_NUM_8197F(x, v)                                      \
	(BIT_CLEAR_HI0Q_DESC_NUM_8197F(x) | BIT_HI0Q_DESC_NUM_8197F(v))

/* 2 REG_HI1Q_TXBD_NUM_8197F */
#define BIT_HI1Q_FLAG_8197F BIT(14)

#define BIT_SHIFT_HI1Q_DESC_MODE_8197F 12
#define BIT_MASK_HI1Q_DESC_MODE_8197F 0x3
#define BIT_HI1Q_DESC_MODE_8197F(x)                                            \
	(((x) & BIT_MASK_HI1Q_DESC_MODE_8197F)                                 \
	 << BIT_SHIFT_HI1Q_DESC_MODE_8197F)
#define BITS_HI1Q_DESC_MODE_8197F                                              \
	(BIT_MASK_HI1Q_DESC_MODE_8197F << BIT_SHIFT_HI1Q_DESC_MODE_8197F)
#define BIT_CLEAR_HI1Q_DESC_MODE_8197F(x) ((x) & (~BITS_HI1Q_DESC_MODE_8197F))
#define BIT_GET_HI1Q_DESC_MODE_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HI1Q_DESC_MODE_8197F) &                             \
	 BIT_MASK_HI1Q_DESC_MODE_8197F)
#define BIT_SET_HI1Q_DESC_MODE_8197F(x, v)                                     \
	(BIT_CLEAR_HI1Q_DESC_MODE_8197F(x) | BIT_HI1Q_DESC_MODE_8197F(v))

#define BIT_SHIFT_HI1Q_DESC_NUM_8197F 0
#define BIT_MASK_HI1Q_DESC_NUM_8197F 0xfff
#define BIT_HI1Q_DESC_NUM_8197F(x)                                             \
	(((x) & BIT_MASK_HI1Q_DESC_NUM_8197F) << BIT_SHIFT_HI1Q_DESC_NUM_8197F)
#define BITS_HI1Q_DESC_NUM_8197F                                               \
	(BIT_MASK_HI1Q_DESC_NUM_8197F << BIT_SHIFT_HI1Q_DESC_NUM_8197F)
#define BIT_CLEAR_HI1Q_DESC_NUM_8197F(x) ((x) & (~BITS_HI1Q_DESC_NUM_8197F))
#define BIT_GET_HI1Q_DESC_NUM_8197F(x)                                         \
	(((x) >> BIT_SHIFT_HI1Q_DESC_NUM_8197F) & BIT_MASK_HI1Q_DESC_NUM_8197F)
#define BIT_SET_HI1Q_DESC_NUM_8197F(x, v)                                      \
	(BIT_CLEAR_HI1Q_DESC_NUM_8197F(x) | BIT_HI1Q_DESC_NUM_8197F(v))

/* 2 REG_HI2Q_TXBD_NUM_8197F */
#define BIT_HI2Q_FLAG_8197F BIT(14)

#define BIT_SHIFT_HI2Q_DESC_MODE_8197F 12
#define BIT_MASK_HI2Q_DESC_MODE_8197F 0x3
#define BIT_HI2Q_DESC_MODE_8197F(x)                                            \
	(((x) & BIT_MASK_HI2Q_DESC_MODE_8197F)                                 \
	 << BIT_SHIFT_HI2Q_DESC_MODE_8197F)
#define BITS_HI2Q_DESC_MODE_8197F                                              \
	(BIT_MASK_HI2Q_DESC_MODE_8197F << BIT_SHIFT_HI2Q_DESC_MODE_8197F)
#define BIT_CLEAR_HI2Q_DESC_MODE_8197F(x) ((x) & (~BITS_HI2Q_DESC_MODE_8197F))
#define BIT_GET_HI2Q_DESC_MODE_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HI2Q_DESC_MODE_8197F) &                             \
	 BIT_MASK_HI2Q_DESC_MODE_8197F)
#define BIT_SET_HI2Q_DESC_MODE_8197F(x, v)                                     \
	(BIT_CLEAR_HI2Q_DESC_MODE_8197F(x) | BIT_HI2Q_DESC_MODE_8197F(v))

#define BIT_SHIFT_HI2Q_DESC_NUM_8197F 0
#define BIT_MASK_HI2Q_DESC_NUM_8197F 0xfff
#define BIT_HI2Q_DESC_NUM_8197F(x)                                             \
	(((x) & BIT_MASK_HI2Q_DESC_NUM_8197F) << BIT_SHIFT_HI2Q_DESC_NUM_8197F)
#define BITS_HI2Q_DESC_NUM_8197F                                               \
	(BIT_MASK_HI2Q_DESC_NUM_8197F << BIT_SHIFT_HI2Q_DESC_NUM_8197F)
#define BIT_CLEAR_HI2Q_DESC_NUM_8197F(x) ((x) & (~BITS_HI2Q_DESC_NUM_8197F))
#define BIT_GET_HI2Q_DESC_NUM_8197F(x)                                         \
	(((x) >> BIT_SHIFT_HI2Q_DESC_NUM_8197F) & BIT_MASK_HI2Q_DESC_NUM_8197F)
#define BIT_SET_HI2Q_DESC_NUM_8197F(x, v)                                      \
	(BIT_CLEAR_HI2Q_DESC_NUM_8197F(x) | BIT_HI2Q_DESC_NUM_8197F(v))

/* 2 REG_HI3Q_TXBD_NUM_8197F */
#define BIT_HI3Q_FLAG_8197F BIT(14)

#define BIT_SHIFT_HI3Q_DESC_MODE_8197F 12
#define BIT_MASK_HI3Q_DESC_MODE_8197F 0x3
#define BIT_HI3Q_DESC_MODE_8197F(x)                                            \
	(((x) & BIT_MASK_HI3Q_DESC_MODE_8197F)                                 \
	 << BIT_SHIFT_HI3Q_DESC_MODE_8197F)
#define BITS_HI3Q_DESC_MODE_8197F                                              \
	(BIT_MASK_HI3Q_DESC_MODE_8197F << BIT_SHIFT_HI3Q_DESC_MODE_8197F)
#define BIT_CLEAR_HI3Q_DESC_MODE_8197F(x) ((x) & (~BITS_HI3Q_DESC_MODE_8197F))
#define BIT_GET_HI3Q_DESC_MODE_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HI3Q_DESC_MODE_8197F) &                             \
	 BIT_MASK_HI3Q_DESC_MODE_8197F)
#define BIT_SET_HI3Q_DESC_MODE_8197F(x, v)                                     \
	(BIT_CLEAR_HI3Q_DESC_MODE_8197F(x) | BIT_HI3Q_DESC_MODE_8197F(v))

#define BIT_SHIFT_HI3Q_DESC_NUM_8197F 0
#define BIT_MASK_HI3Q_DESC_NUM_8197F 0xfff
#define BIT_HI3Q_DESC_NUM_8197F(x)                                             \
	(((x) & BIT_MASK_HI3Q_DESC_NUM_8197F) << BIT_SHIFT_HI3Q_DESC_NUM_8197F)
#define BITS_HI3Q_DESC_NUM_8197F                                               \
	(BIT_MASK_HI3Q_DESC_NUM_8197F << BIT_SHIFT_HI3Q_DESC_NUM_8197F)
#define BIT_CLEAR_HI3Q_DESC_NUM_8197F(x) ((x) & (~BITS_HI3Q_DESC_NUM_8197F))
#define BIT_GET_HI3Q_DESC_NUM_8197F(x)                                         \
	(((x) >> BIT_SHIFT_HI3Q_DESC_NUM_8197F) & BIT_MASK_HI3Q_DESC_NUM_8197F)
#define BIT_SET_HI3Q_DESC_NUM_8197F(x, v)                                      \
	(BIT_CLEAR_HI3Q_DESC_NUM_8197F(x) | BIT_HI3Q_DESC_NUM_8197F(v))

/* 2 REG_HI4Q_TXBD_NUM_8197F */
#define BIT_HI4Q_FLAG_8197F BIT(14)

#define BIT_SHIFT_HI4Q_DESC_MODE_8197F 12
#define BIT_MASK_HI4Q_DESC_MODE_8197F 0x3
#define BIT_HI4Q_DESC_MODE_8197F(x)                                            \
	(((x) & BIT_MASK_HI4Q_DESC_MODE_8197F)                                 \
	 << BIT_SHIFT_HI4Q_DESC_MODE_8197F)
#define BITS_HI4Q_DESC_MODE_8197F                                              \
	(BIT_MASK_HI4Q_DESC_MODE_8197F << BIT_SHIFT_HI4Q_DESC_MODE_8197F)
#define BIT_CLEAR_HI4Q_DESC_MODE_8197F(x) ((x) & (~BITS_HI4Q_DESC_MODE_8197F))
#define BIT_GET_HI4Q_DESC_MODE_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HI4Q_DESC_MODE_8197F) &                             \
	 BIT_MASK_HI4Q_DESC_MODE_8197F)
#define BIT_SET_HI4Q_DESC_MODE_8197F(x, v)                                     \
	(BIT_CLEAR_HI4Q_DESC_MODE_8197F(x) | BIT_HI4Q_DESC_MODE_8197F(v))

#define BIT_SHIFT_HI4Q_DESC_NUM_8197F 0
#define BIT_MASK_HI4Q_DESC_NUM_8197F 0xfff
#define BIT_HI4Q_DESC_NUM_8197F(x)                                             \
	(((x) & BIT_MASK_HI4Q_DESC_NUM_8197F) << BIT_SHIFT_HI4Q_DESC_NUM_8197F)
#define BITS_HI4Q_DESC_NUM_8197F                                               \
	(BIT_MASK_HI4Q_DESC_NUM_8197F << BIT_SHIFT_HI4Q_DESC_NUM_8197F)
#define BIT_CLEAR_HI4Q_DESC_NUM_8197F(x) ((x) & (~BITS_HI4Q_DESC_NUM_8197F))
#define BIT_GET_HI4Q_DESC_NUM_8197F(x)                                         \
	(((x) >> BIT_SHIFT_HI4Q_DESC_NUM_8197F) & BIT_MASK_HI4Q_DESC_NUM_8197F)
#define BIT_SET_HI4Q_DESC_NUM_8197F(x, v)                                      \
	(BIT_CLEAR_HI4Q_DESC_NUM_8197F(x) | BIT_HI4Q_DESC_NUM_8197F(v))

/* 2 REG_HI5Q_TXBD_NUM_8197F */
#define BIT_HI5Q_FLAG_8197F BIT(14)

#define BIT_SHIFT_HI5Q_DESC_MODE_8197F 12
#define BIT_MASK_HI5Q_DESC_MODE_8197F 0x3
#define BIT_HI5Q_DESC_MODE_8197F(x)                                            \
	(((x) & BIT_MASK_HI5Q_DESC_MODE_8197F)                                 \
	 << BIT_SHIFT_HI5Q_DESC_MODE_8197F)
#define BITS_HI5Q_DESC_MODE_8197F                                              \
	(BIT_MASK_HI5Q_DESC_MODE_8197F << BIT_SHIFT_HI5Q_DESC_MODE_8197F)
#define BIT_CLEAR_HI5Q_DESC_MODE_8197F(x) ((x) & (~BITS_HI5Q_DESC_MODE_8197F))
#define BIT_GET_HI5Q_DESC_MODE_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HI5Q_DESC_MODE_8197F) &                             \
	 BIT_MASK_HI5Q_DESC_MODE_8197F)
#define BIT_SET_HI5Q_DESC_MODE_8197F(x, v)                                     \
	(BIT_CLEAR_HI5Q_DESC_MODE_8197F(x) | BIT_HI5Q_DESC_MODE_8197F(v))

#define BIT_SHIFT_HI5Q_DESC_NUM_8197F 0
#define BIT_MASK_HI5Q_DESC_NUM_8197F 0xfff
#define BIT_HI5Q_DESC_NUM_8197F(x)                                             \
	(((x) & BIT_MASK_HI5Q_DESC_NUM_8197F) << BIT_SHIFT_HI5Q_DESC_NUM_8197F)
#define BITS_HI5Q_DESC_NUM_8197F                                               \
	(BIT_MASK_HI5Q_DESC_NUM_8197F << BIT_SHIFT_HI5Q_DESC_NUM_8197F)
#define BIT_CLEAR_HI5Q_DESC_NUM_8197F(x) ((x) & (~BITS_HI5Q_DESC_NUM_8197F))
#define BIT_GET_HI5Q_DESC_NUM_8197F(x)                                         \
	(((x) >> BIT_SHIFT_HI5Q_DESC_NUM_8197F) & BIT_MASK_HI5Q_DESC_NUM_8197F)
#define BIT_SET_HI5Q_DESC_NUM_8197F(x, v)                                      \
	(BIT_CLEAR_HI5Q_DESC_NUM_8197F(x) | BIT_HI5Q_DESC_NUM_8197F(v))

/* 2 REG_HI6Q_TXBD_NUM_8197F */
#define BIT_HI6Q_FLAG_8197F BIT(14)

#define BIT_SHIFT_HI6Q_DESC_MODE_8197F 12
#define BIT_MASK_HI6Q_DESC_MODE_8197F 0x3
#define BIT_HI6Q_DESC_MODE_8197F(x)                                            \
	(((x) & BIT_MASK_HI6Q_DESC_MODE_8197F)                                 \
	 << BIT_SHIFT_HI6Q_DESC_MODE_8197F)
#define BITS_HI6Q_DESC_MODE_8197F                                              \
	(BIT_MASK_HI6Q_DESC_MODE_8197F << BIT_SHIFT_HI6Q_DESC_MODE_8197F)
#define BIT_CLEAR_HI6Q_DESC_MODE_8197F(x) ((x) & (~BITS_HI6Q_DESC_MODE_8197F))
#define BIT_GET_HI6Q_DESC_MODE_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HI6Q_DESC_MODE_8197F) &                             \
	 BIT_MASK_HI6Q_DESC_MODE_8197F)
#define BIT_SET_HI6Q_DESC_MODE_8197F(x, v)                                     \
	(BIT_CLEAR_HI6Q_DESC_MODE_8197F(x) | BIT_HI6Q_DESC_MODE_8197F(v))

#define BIT_SHIFT_HI6Q_DESC_NUM_8197F 0
#define BIT_MASK_HI6Q_DESC_NUM_8197F 0xfff
#define BIT_HI6Q_DESC_NUM_8197F(x)                                             \
	(((x) & BIT_MASK_HI6Q_DESC_NUM_8197F) << BIT_SHIFT_HI6Q_DESC_NUM_8197F)
#define BITS_HI6Q_DESC_NUM_8197F                                               \
	(BIT_MASK_HI6Q_DESC_NUM_8197F << BIT_SHIFT_HI6Q_DESC_NUM_8197F)
#define BIT_CLEAR_HI6Q_DESC_NUM_8197F(x) ((x) & (~BITS_HI6Q_DESC_NUM_8197F))
#define BIT_GET_HI6Q_DESC_NUM_8197F(x)                                         \
	(((x) >> BIT_SHIFT_HI6Q_DESC_NUM_8197F) & BIT_MASK_HI6Q_DESC_NUM_8197F)
#define BIT_SET_HI6Q_DESC_NUM_8197F(x, v)                                      \
	(BIT_CLEAR_HI6Q_DESC_NUM_8197F(x) | BIT_HI6Q_DESC_NUM_8197F(v))

/* 2 REG_HI7Q_TXBD_NUM_8197F */
#define BIT_HI7Q_FLAG_8197F BIT(14)

#define BIT_SHIFT_HI7Q_DESC_MODE_8197F 12
#define BIT_MASK_HI7Q_DESC_MODE_8197F 0x3
#define BIT_HI7Q_DESC_MODE_8197F(x)                                            \
	(((x) & BIT_MASK_HI7Q_DESC_MODE_8197F)                                 \
	 << BIT_SHIFT_HI7Q_DESC_MODE_8197F)
#define BITS_HI7Q_DESC_MODE_8197F                                              \
	(BIT_MASK_HI7Q_DESC_MODE_8197F << BIT_SHIFT_HI7Q_DESC_MODE_8197F)
#define BIT_CLEAR_HI7Q_DESC_MODE_8197F(x) ((x) & (~BITS_HI7Q_DESC_MODE_8197F))
#define BIT_GET_HI7Q_DESC_MODE_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HI7Q_DESC_MODE_8197F) &                             \
	 BIT_MASK_HI7Q_DESC_MODE_8197F)
#define BIT_SET_HI7Q_DESC_MODE_8197F(x, v)                                     \
	(BIT_CLEAR_HI7Q_DESC_MODE_8197F(x) | BIT_HI7Q_DESC_MODE_8197F(v))

#define BIT_SHIFT_HI7Q_DESC_NUM_8197F 0
#define BIT_MASK_HI7Q_DESC_NUM_8197F 0xfff
#define BIT_HI7Q_DESC_NUM_8197F(x)                                             \
	(((x) & BIT_MASK_HI7Q_DESC_NUM_8197F) << BIT_SHIFT_HI7Q_DESC_NUM_8197F)
#define BITS_HI7Q_DESC_NUM_8197F                                               \
	(BIT_MASK_HI7Q_DESC_NUM_8197F << BIT_SHIFT_HI7Q_DESC_NUM_8197F)
#define BIT_CLEAR_HI7Q_DESC_NUM_8197F(x) ((x) & (~BITS_HI7Q_DESC_NUM_8197F))
#define BIT_GET_HI7Q_DESC_NUM_8197F(x)                                         \
	(((x) >> BIT_SHIFT_HI7Q_DESC_NUM_8197F) & BIT_MASK_HI7Q_DESC_NUM_8197F)
#define BIT_SET_HI7Q_DESC_NUM_8197F(x, v)                                      \
	(BIT_CLEAR_HI7Q_DESC_NUM_8197F(x) | BIT_HI7Q_DESC_NUM_8197F(v))

/* 2 REG_TSFTIMER_HCI_8197F */

#define BIT_SHIFT_TSFT2_HCI_8197F 16
#define BIT_MASK_TSFT2_HCI_8197F 0xffff
#define BIT_TSFT2_HCI_8197F(x)                                                 \
	(((x) & BIT_MASK_TSFT2_HCI_8197F) << BIT_SHIFT_TSFT2_HCI_8197F)
#define BITS_TSFT2_HCI_8197F                                                   \
	(BIT_MASK_TSFT2_HCI_8197F << BIT_SHIFT_TSFT2_HCI_8197F)
#define BIT_CLEAR_TSFT2_HCI_8197F(x) ((x) & (~BITS_TSFT2_HCI_8197F))
#define BIT_GET_TSFT2_HCI_8197F(x)                                             \
	(((x) >> BIT_SHIFT_TSFT2_HCI_8197F) & BIT_MASK_TSFT2_HCI_8197F)
#define BIT_SET_TSFT2_HCI_8197F(x, v)                                          \
	(BIT_CLEAR_TSFT2_HCI_8197F(x) | BIT_TSFT2_HCI_8197F(v))

#define BIT_SHIFT_TSFT1_HCI_8197F 0
#define BIT_MASK_TSFT1_HCI_8197F 0xffff
#define BIT_TSFT1_HCI_8197F(x)                                                 \
	(((x) & BIT_MASK_TSFT1_HCI_8197F) << BIT_SHIFT_TSFT1_HCI_8197F)
#define BITS_TSFT1_HCI_8197F                                                   \
	(BIT_MASK_TSFT1_HCI_8197F << BIT_SHIFT_TSFT1_HCI_8197F)
#define BIT_CLEAR_TSFT1_HCI_8197F(x) ((x) & (~BITS_TSFT1_HCI_8197F))
#define BIT_GET_TSFT1_HCI_8197F(x)                                             \
	(((x) >> BIT_SHIFT_TSFT1_HCI_8197F) & BIT_MASK_TSFT1_HCI_8197F)
#define BIT_SET_TSFT1_HCI_8197F(x, v)                                          \
	(BIT_CLEAR_TSFT1_HCI_8197F(x) | BIT_TSFT1_HCI_8197F(v))

/* 2 REG_BD_RWPTR_CLR_8197F */
#define BIT_CLR_HI7Q_HW_IDX_8197F BIT(29)
#define BIT_CLR_HI6Q_HW_IDX_8197F BIT(28)
#define BIT_CLR_HI5Q_HW_IDX_8197F BIT(27)
#define BIT_CLR_HI4Q_HW_IDX_8197F BIT(26)
#define BIT_CLR_HI3Q_HW_IDX_8197F BIT(25)
#define BIT_CLR_HI2Q_HW_IDX_8197F BIT(24)
#define BIT_CLR_HI1Q_HW_IDX_8197F BIT(23)
#define BIT_CLR_HI0Q_HW_IDX_8197F BIT(22)
#define BIT_CLR_BKQ_HW_IDX_8197F BIT(21)
#define BIT_CLR_BEQ_HW_IDX_8197F BIT(20)
#define BIT_CLR_VIQ_HW_IDX_8197F BIT(19)
#define BIT_CLR_VOQ_HW_IDX_8197F BIT(18)
#define BIT_CLR_MGQ_HW_IDX_8197F BIT(17)
#define BIT_CLR_RXQ_HW_IDX_8197F BIT(16)
#define BIT_CLR_HI7Q_HOST_IDX_8197F BIT(13)
#define BIT_CLR_HI6Q_HOST_IDX_8197F BIT(12)
#define BIT_CLR_HI5Q_HOST_IDX_8197F BIT(11)
#define BIT_CLR_HI4Q_HOST_IDX_8197F BIT(10)
#define BIT_CLR_HI3Q_HOST_IDX_8197F BIT(9)
#define BIT_CLR_HI2Q_HOST_IDX_8197F BIT(8)
#define BIT_CLR_HI1Q_HOST_IDX_8197F BIT(7)
#define BIT_CLR_HI0Q_HOST_IDX_8197F BIT(6)
#define BIT_CLR_BKQ_HOST_IDX_8197F BIT(5)
#define BIT_CLR_BEQ_HOST_IDX_8197F BIT(4)
#define BIT_CLR_VIQ_HOST_IDX_8197F BIT(3)
#define BIT_CLR_VOQ_HOST_IDX_8197F BIT(2)
#define BIT_CLR_MGQ_HOST_IDX_8197F BIT(1)
#define BIT_CLR_RXQ_HOST_IDX_8197F BIT(0)

/* 2 REG_VOQ_TXBD_IDX_8197F */

#define BIT_SHIFT_VOQ_HW_IDX_8197F 16
#define BIT_MASK_VOQ_HW_IDX_8197F 0xfff
#define BIT_VOQ_HW_IDX_8197F(x)                                                \
	(((x) & BIT_MASK_VOQ_HW_IDX_8197F) << BIT_SHIFT_VOQ_HW_IDX_8197F)
#define BITS_VOQ_HW_IDX_8197F                                                  \
	(BIT_MASK_VOQ_HW_IDX_8197F << BIT_SHIFT_VOQ_HW_IDX_8197F)
#define BIT_CLEAR_VOQ_HW_IDX_8197F(x) ((x) & (~BITS_VOQ_HW_IDX_8197F))
#define BIT_GET_VOQ_HW_IDX_8197F(x)                                            \
	(((x) >> BIT_SHIFT_VOQ_HW_IDX_8197F) & BIT_MASK_VOQ_HW_IDX_8197F)
#define BIT_SET_VOQ_HW_IDX_8197F(x, v)                                         \
	(BIT_CLEAR_VOQ_HW_IDX_8197F(x) | BIT_VOQ_HW_IDX_8197F(v))

#define BIT_SHIFT_VOQ_HOST_IDX_8197F 0
#define BIT_MASK_VOQ_HOST_IDX_8197F 0xfff
#define BIT_VOQ_HOST_IDX_8197F(x)                                              \
	(((x) & BIT_MASK_VOQ_HOST_IDX_8197F) << BIT_SHIFT_VOQ_HOST_IDX_8197F)
#define BITS_VOQ_HOST_IDX_8197F                                                \
	(BIT_MASK_VOQ_HOST_IDX_8197F << BIT_SHIFT_VOQ_HOST_IDX_8197F)
#define BIT_CLEAR_VOQ_HOST_IDX_8197F(x) ((x) & (~BITS_VOQ_HOST_IDX_8197F))
#define BIT_GET_VOQ_HOST_IDX_8197F(x)                                          \
	(((x) >> BIT_SHIFT_VOQ_HOST_IDX_8197F) & BIT_MASK_VOQ_HOST_IDX_8197F)
#define BIT_SET_VOQ_HOST_IDX_8197F(x, v)                                       \
	(BIT_CLEAR_VOQ_HOST_IDX_8197F(x) | BIT_VOQ_HOST_IDX_8197F(v))

/* 2 REG_VIQ_TXBD_IDX_8197F */

#define BIT_SHIFT_VIQ_HW_IDX_8197F 16
#define BIT_MASK_VIQ_HW_IDX_8197F 0xfff
#define BIT_VIQ_HW_IDX_8197F(x)                                                \
	(((x) & BIT_MASK_VIQ_HW_IDX_8197F) << BIT_SHIFT_VIQ_HW_IDX_8197F)
#define BITS_VIQ_HW_IDX_8197F                                                  \
	(BIT_MASK_VIQ_HW_IDX_8197F << BIT_SHIFT_VIQ_HW_IDX_8197F)
#define BIT_CLEAR_VIQ_HW_IDX_8197F(x) ((x) & (~BITS_VIQ_HW_IDX_8197F))
#define BIT_GET_VIQ_HW_IDX_8197F(x)                                            \
	(((x) >> BIT_SHIFT_VIQ_HW_IDX_8197F) & BIT_MASK_VIQ_HW_IDX_8197F)
#define BIT_SET_VIQ_HW_IDX_8197F(x, v)                                         \
	(BIT_CLEAR_VIQ_HW_IDX_8197F(x) | BIT_VIQ_HW_IDX_8197F(v))

#define BIT_SHIFT_VIQ_HOST_IDX_8197F 0
#define BIT_MASK_VIQ_HOST_IDX_8197F 0xfff
#define BIT_VIQ_HOST_IDX_8197F(x)                                              \
	(((x) & BIT_MASK_VIQ_HOST_IDX_8197F) << BIT_SHIFT_VIQ_HOST_IDX_8197F)
#define BITS_VIQ_HOST_IDX_8197F                                                \
	(BIT_MASK_VIQ_HOST_IDX_8197F << BIT_SHIFT_VIQ_HOST_IDX_8197F)
#define BIT_CLEAR_VIQ_HOST_IDX_8197F(x) ((x) & (~BITS_VIQ_HOST_IDX_8197F))
#define BIT_GET_VIQ_HOST_IDX_8197F(x)                                          \
	(((x) >> BIT_SHIFT_VIQ_HOST_IDX_8197F) & BIT_MASK_VIQ_HOST_IDX_8197F)
#define BIT_SET_VIQ_HOST_IDX_8197F(x, v)                                       \
	(BIT_CLEAR_VIQ_HOST_IDX_8197F(x) | BIT_VIQ_HOST_IDX_8197F(v))

/* 2 REG_BEQ_TXBD_IDX_8197F */

#define BIT_SHIFT_BEQ_HW_IDX_8197F 16
#define BIT_MASK_BEQ_HW_IDX_8197F 0xfff
#define BIT_BEQ_HW_IDX_8197F(x)                                                \
	(((x) & BIT_MASK_BEQ_HW_IDX_8197F) << BIT_SHIFT_BEQ_HW_IDX_8197F)
#define BITS_BEQ_HW_IDX_8197F                                                  \
	(BIT_MASK_BEQ_HW_IDX_8197F << BIT_SHIFT_BEQ_HW_IDX_8197F)
#define BIT_CLEAR_BEQ_HW_IDX_8197F(x) ((x) & (~BITS_BEQ_HW_IDX_8197F))
#define BIT_GET_BEQ_HW_IDX_8197F(x)                                            \
	(((x) >> BIT_SHIFT_BEQ_HW_IDX_8197F) & BIT_MASK_BEQ_HW_IDX_8197F)
#define BIT_SET_BEQ_HW_IDX_8197F(x, v)                                         \
	(BIT_CLEAR_BEQ_HW_IDX_8197F(x) | BIT_BEQ_HW_IDX_8197F(v))

#define BIT_SHIFT_BEQ_HOST_IDX_8197F 0
#define BIT_MASK_BEQ_HOST_IDX_8197F 0xfff
#define BIT_BEQ_HOST_IDX_8197F(x)                                              \
	(((x) & BIT_MASK_BEQ_HOST_IDX_8197F) << BIT_SHIFT_BEQ_HOST_IDX_8197F)
#define BITS_BEQ_HOST_IDX_8197F                                                \
	(BIT_MASK_BEQ_HOST_IDX_8197F << BIT_SHIFT_BEQ_HOST_IDX_8197F)
#define BIT_CLEAR_BEQ_HOST_IDX_8197F(x) ((x) & (~BITS_BEQ_HOST_IDX_8197F))
#define BIT_GET_BEQ_HOST_IDX_8197F(x)                                          \
	(((x) >> BIT_SHIFT_BEQ_HOST_IDX_8197F) & BIT_MASK_BEQ_HOST_IDX_8197F)
#define BIT_SET_BEQ_HOST_IDX_8197F(x, v)                                       \
	(BIT_CLEAR_BEQ_HOST_IDX_8197F(x) | BIT_BEQ_HOST_IDX_8197F(v))

/* 2 REG_BKQ_TXBD_IDX_8197F */

#define BIT_SHIFT_BKQ_HW_IDX_8197F 16
#define BIT_MASK_BKQ_HW_IDX_8197F 0xfff
#define BIT_BKQ_HW_IDX_8197F(x)                                                \
	(((x) & BIT_MASK_BKQ_HW_IDX_8197F) << BIT_SHIFT_BKQ_HW_IDX_8197F)
#define BITS_BKQ_HW_IDX_8197F                                                  \
	(BIT_MASK_BKQ_HW_IDX_8197F << BIT_SHIFT_BKQ_HW_IDX_8197F)
#define BIT_CLEAR_BKQ_HW_IDX_8197F(x) ((x) & (~BITS_BKQ_HW_IDX_8197F))
#define BIT_GET_BKQ_HW_IDX_8197F(x)                                            \
	(((x) >> BIT_SHIFT_BKQ_HW_IDX_8197F) & BIT_MASK_BKQ_HW_IDX_8197F)
#define BIT_SET_BKQ_HW_IDX_8197F(x, v)                                         \
	(BIT_CLEAR_BKQ_HW_IDX_8197F(x) | BIT_BKQ_HW_IDX_8197F(v))

#define BIT_SHIFT_BKQ_HOST_IDX_8197F 0
#define BIT_MASK_BKQ_HOST_IDX_8197F 0xfff
#define BIT_BKQ_HOST_IDX_8197F(x)                                              \
	(((x) & BIT_MASK_BKQ_HOST_IDX_8197F) << BIT_SHIFT_BKQ_HOST_IDX_8197F)
#define BITS_BKQ_HOST_IDX_8197F                                                \
	(BIT_MASK_BKQ_HOST_IDX_8197F << BIT_SHIFT_BKQ_HOST_IDX_8197F)
#define BIT_CLEAR_BKQ_HOST_IDX_8197F(x) ((x) & (~BITS_BKQ_HOST_IDX_8197F))
#define BIT_GET_BKQ_HOST_IDX_8197F(x)                                          \
	(((x) >> BIT_SHIFT_BKQ_HOST_IDX_8197F) & BIT_MASK_BKQ_HOST_IDX_8197F)
#define BIT_SET_BKQ_HOST_IDX_8197F(x, v)                                       \
	(BIT_CLEAR_BKQ_HOST_IDX_8197F(x) | BIT_BKQ_HOST_IDX_8197F(v))

/* 2 REG_MGQ_TXBD_IDX_8197F */

#define BIT_SHIFT_MGQ_HW_IDX_8197F 16
#define BIT_MASK_MGQ_HW_IDX_8197F 0xfff
#define BIT_MGQ_HW_IDX_8197F(x)                                                \
	(((x) & BIT_MASK_MGQ_HW_IDX_8197F) << BIT_SHIFT_MGQ_HW_IDX_8197F)
#define BITS_MGQ_HW_IDX_8197F                                                  \
	(BIT_MASK_MGQ_HW_IDX_8197F << BIT_SHIFT_MGQ_HW_IDX_8197F)
#define BIT_CLEAR_MGQ_HW_IDX_8197F(x) ((x) & (~BITS_MGQ_HW_IDX_8197F))
#define BIT_GET_MGQ_HW_IDX_8197F(x)                                            \
	(((x) >> BIT_SHIFT_MGQ_HW_IDX_8197F) & BIT_MASK_MGQ_HW_IDX_8197F)
#define BIT_SET_MGQ_HW_IDX_8197F(x, v)                                         \
	(BIT_CLEAR_MGQ_HW_IDX_8197F(x) | BIT_MGQ_HW_IDX_8197F(v))

#define BIT_SHIFT_MGQ_HOST_IDX_8197F 0
#define BIT_MASK_MGQ_HOST_IDX_8197F 0xfff
#define BIT_MGQ_HOST_IDX_8197F(x)                                              \
	(((x) & BIT_MASK_MGQ_HOST_IDX_8197F) << BIT_SHIFT_MGQ_HOST_IDX_8197F)
#define BITS_MGQ_HOST_IDX_8197F                                                \
	(BIT_MASK_MGQ_HOST_IDX_8197F << BIT_SHIFT_MGQ_HOST_IDX_8197F)
#define BIT_CLEAR_MGQ_HOST_IDX_8197F(x) ((x) & (~BITS_MGQ_HOST_IDX_8197F))
#define BIT_GET_MGQ_HOST_IDX_8197F(x)                                          \
	(((x) >> BIT_SHIFT_MGQ_HOST_IDX_8197F) & BIT_MASK_MGQ_HOST_IDX_8197F)
#define BIT_SET_MGQ_HOST_IDX_8197F(x, v)                                       \
	(BIT_CLEAR_MGQ_HOST_IDX_8197F(x) | BIT_MGQ_HOST_IDX_8197F(v))

/* 2 REG_RXQ_RXBD_IDX_8197F */

#define BIT_SHIFT_RXQ_HW_IDX_8197F 16
#define BIT_MASK_RXQ_HW_IDX_8197F 0xfff
#define BIT_RXQ_HW_IDX_8197F(x)                                                \
	(((x) & BIT_MASK_RXQ_HW_IDX_8197F) << BIT_SHIFT_RXQ_HW_IDX_8197F)
#define BITS_RXQ_HW_IDX_8197F                                                  \
	(BIT_MASK_RXQ_HW_IDX_8197F << BIT_SHIFT_RXQ_HW_IDX_8197F)
#define BIT_CLEAR_RXQ_HW_IDX_8197F(x) ((x) & (~BITS_RXQ_HW_IDX_8197F))
#define BIT_GET_RXQ_HW_IDX_8197F(x)                                            \
	(((x) >> BIT_SHIFT_RXQ_HW_IDX_8197F) & BIT_MASK_RXQ_HW_IDX_8197F)
#define BIT_SET_RXQ_HW_IDX_8197F(x, v)                                         \
	(BIT_CLEAR_RXQ_HW_IDX_8197F(x) | BIT_RXQ_HW_IDX_8197F(v))

#define BIT_SHIFT_RXQ_HOST_IDX_8197F 0
#define BIT_MASK_RXQ_HOST_IDX_8197F 0xfff
#define BIT_RXQ_HOST_IDX_8197F(x)                                              \
	(((x) & BIT_MASK_RXQ_HOST_IDX_8197F) << BIT_SHIFT_RXQ_HOST_IDX_8197F)
#define BITS_RXQ_HOST_IDX_8197F                                                \
	(BIT_MASK_RXQ_HOST_IDX_8197F << BIT_SHIFT_RXQ_HOST_IDX_8197F)
#define BIT_CLEAR_RXQ_HOST_IDX_8197F(x) ((x) & (~BITS_RXQ_HOST_IDX_8197F))
#define BIT_GET_RXQ_HOST_IDX_8197F(x)                                          \
	(((x) >> BIT_SHIFT_RXQ_HOST_IDX_8197F) & BIT_MASK_RXQ_HOST_IDX_8197F)
#define BIT_SET_RXQ_HOST_IDX_8197F(x, v)                                       \
	(BIT_CLEAR_RXQ_HOST_IDX_8197F(x) | BIT_RXQ_HOST_IDX_8197F(v))

/* 2 REG_HI0Q_TXBD_IDX_8197F */

#define BIT_SHIFT_HI0Q_HW_IDX_8197F 16
#define BIT_MASK_HI0Q_HW_IDX_8197F 0xfff
#define BIT_HI0Q_HW_IDX_8197F(x)                                               \
	(((x) & BIT_MASK_HI0Q_HW_IDX_8197F) << BIT_SHIFT_HI0Q_HW_IDX_8197F)
#define BITS_HI0Q_HW_IDX_8197F                                                 \
	(BIT_MASK_HI0Q_HW_IDX_8197F << BIT_SHIFT_HI0Q_HW_IDX_8197F)
#define BIT_CLEAR_HI0Q_HW_IDX_8197F(x) ((x) & (~BITS_HI0Q_HW_IDX_8197F))
#define BIT_GET_HI0Q_HW_IDX_8197F(x)                                           \
	(((x) >> BIT_SHIFT_HI0Q_HW_IDX_8197F) & BIT_MASK_HI0Q_HW_IDX_8197F)
#define BIT_SET_HI0Q_HW_IDX_8197F(x, v)                                        \
	(BIT_CLEAR_HI0Q_HW_IDX_8197F(x) | BIT_HI0Q_HW_IDX_8197F(v))

#define BIT_SHIFT_HI0Q_HOST_IDX_8197F 0
#define BIT_MASK_HI0Q_HOST_IDX_8197F 0xfff
#define BIT_HI0Q_HOST_IDX_8197F(x)                                             \
	(((x) & BIT_MASK_HI0Q_HOST_IDX_8197F) << BIT_SHIFT_HI0Q_HOST_IDX_8197F)
#define BITS_HI0Q_HOST_IDX_8197F                                               \
	(BIT_MASK_HI0Q_HOST_IDX_8197F << BIT_SHIFT_HI0Q_HOST_IDX_8197F)
#define BIT_CLEAR_HI0Q_HOST_IDX_8197F(x) ((x) & (~BITS_HI0Q_HOST_IDX_8197F))
#define BIT_GET_HI0Q_HOST_IDX_8197F(x)                                         \
	(((x) >> BIT_SHIFT_HI0Q_HOST_IDX_8197F) & BIT_MASK_HI0Q_HOST_IDX_8197F)
#define BIT_SET_HI0Q_HOST_IDX_8197F(x, v)                                      \
	(BIT_CLEAR_HI0Q_HOST_IDX_8197F(x) | BIT_HI0Q_HOST_IDX_8197F(v))

/* 2 REG_HI1Q_TXBD_IDX_8197F */

#define BIT_SHIFT_HI1Q_HW_IDX_8197F 16
#define BIT_MASK_HI1Q_HW_IDX_8197F 0xfff
#define BIT_HI1Q_HW_IDX_8197F(x)                                               \
	(((x) & BIT_MASK_HI1Q_HW_IDX_8197F) << BIT_SHIFT_HI1Q_HW_IDX_8197F)
#define BITS_HI1Q_HW_IDX_8197F                                                 \
	(BIT_MASK_HI1Q_HW_IDX_8197F << BIT_SHIFT_HI1Q_HW_IDX_8197F)
#define BIT_CLEAR_HI1Q_HW_IDX_8197F(x) ((x) & (~BITS_HI1Q_HW_IDX_8197F))
#define BIT_GET_HI1Q_HW_IDX_8197F(x)                                           \
	(((x) >> BIT_SHIFT_HI1Q_HW_IDX_8197F) & BIT_MASK_HI1Q_HW_IDX_8197F)
#define BIT_SET_HI1Q_HW_IDX_8197F(x, v)                                        \
	(BIT_CLEAR_HI1Q_HW_IDX_8197F(x) | BIT_HI1Q_HW_IDX_8197F(v))

#define BIT_SHIFT_HI1Q_HOST_IDX_8197F 0
#define BIT_MASK_HI1Q_HOST_IDX_8197F 0xfff
#define BIT_HI1Q_HOST_IDX_8197F(x)                                             \
	(((x) & BIT_MASK_HI1Q_HOST_IDX_8197F) << BIT_SHIFT_HI1Q_HOST_IDX_8197F)
#define BITS_HI1Q_HOST_IDX_8197F                                               \
	(BIT_MASK_HI1Q_HOST_IDX_8197F << BIT_SHIFT_HI1Q_HOST_IDX_8197F)
#define BIT_CLEAR_HI1Q_HOST_IDX_8197F(x) ((x) & (~BITS_HI1Q_HOST_IDX_8197F))
#define BIT_GET_HI1Q_HOST_IDX_8197F(x)                                         \
	(((x) >> BIT_SHIFT_HI1Q_HOST_IDX_8197F) & BIT_MASK_HI1Q_HOST_IDX_8197F)
#define BIT_SET_HI1Q_HOST_IDX_8197F(x, v)                                      \
	(BIT_CLEAR_HI1Q_HOST_IDX_8197F(x) | BIT_HI1Q_HOST_IDX_8197F(v))

/* 2 REG_HI2Q_TXBD_IDX_8197F */

#define BIT_SHIFT_HI2Q_HW_IDX_8197F 16
#define BIT_MASK_HI2Q_HW_IDX_8197F 0xfff
#define BIT_HI2Q_HW_IDX_8197F(x)                                               \
	(((x) & BIT_MASK_HI2Q_HW_IDX_8197F) << BIT_SHIFT_HI2Q_HW_IDX_8197F)
#define BITS_HI2Q_HW_IDX_8197F                                                 \
	(BIT_MASK_HI2Q_HW_IDX_8197F << BIT_SHIFT_HI2Q_HW_IDX_8197F)
#define BIT_CLEAR_HI2Q_HW_IDX_8197F(x) ((x) & (~BITS_HI2Q_HW_IDX_8197F))
#define BIT_GET_HI2Q_HW_IDX_8197F(x)                                           \
	(((x) >> BIT_SHIFT_HI2Q_HW_IDX_8197F) & BIT_MASK_HI2Q_HW_IDX_8197F)
#define BIT_SET_HI2Q_HW_IDX_8197F(x, v)                                        \
	(BIT_CLEAR_HI2Q_HW_IDX_8197F(x) | BIT_HI2Q_HW_IDX_8197F(v))

#define BIT_SHIFT_HI2Q_HOST_IDX_8197F 0
#define BIT_MASK_HI2Q_HOST_IDX_8197F 0xfff
#define BIT_HI2Q_HOST_IDX_8197F(x)                                             \
	(((x) & BIT_MASK_HI2Q_HOST_IDX_8197F) << BIT_SHIFT_HI2Q_HOST_IDX_8197F)
#define BITS_HI2Q_HOST_IDX_8197F                                               \
	(BIT_MASK_HI2Q_HOST_IDX_8197F << BIT_SHIFT_HI2Q_HOST_IDX_8197F)
#define BIT_CLEAR_HI2Q_HOST_IDX_8197F(x) ((x) & (~BITS_HI2Q_HOST_IDX_8197F))
#define BIT_GET_HI2Q_HOST_IDX_8197F(x)                                         \
	(((x) >> BIT_SHIFT_HI2Q_HOST_IDX_8197F) & BIT_MASK_HI2Q_HOST_IDX_8197F)
#define BIT_SET_HI2Q_HOST_IDX_8197F(x, v)                                      \
	(BIT_CLEAR_HI2Q_HOST_IDX_8197F(x) | BIT_HI2Q_HOST_IDX_8197F(v))

/* 2 REG_HI3Q_TXBD_IDX_8197F */

#define BIT_SHIFT_HI3Q_HW_IDX_8197F 16
#define BIT_MASK_HI3Q_HW_IDX_8197F 0xfff
#define BIT_HI3Q_HW_IDX_8197F(x)                                               \
	(((x) & BIT_MASK_HI3Q_HW_IDX_8197F) << BIT_SHIFT_HI3Q_HW_IDX_8197F)
#define BITS_HI3Q_HW_IDX_8197F                                                 \
	(BIT_MASK_HI3Q_HW_IDX_8197F << BIT_SHIFT_HI3Q_HW_IDX_8197F)
#define BIT_CLEAR_HI3Q_HW_IDX_8197F(x) ((x) & (~BITS_HI3Q_HW_IDX_8197F))
#define BIT_GET_HI3Q_HW_IDX_8197F(x)                                           \
	(((x) >> BIT_SHIFT_HI3Q_HW_IDX_8197F) & BIT_MASK_HI3Q_HW_IDX_8197F)
#define BIT_SET_HI3Q_HW_IDX_8197F(x, v)                                        \
	(BIT_CLEAR_HI3Q_HW_IDX_8197F(x) | BIT_HI3Q_HW_IDX_8197F(v))

#define BIT_SHIFT_HI3Q_HOST_IDX_8197F 0
#define BIT_MASK_HI3Q_HOST_IDX_8197F 0xfff
#define BIT_HI3Q_HOST_IDX_8197F(x)                                             \
	(((x) & BIT_MASK_HI3Q_HOST_IDX_8197F) << BIT_SHIFT_HI3Q_HOST_IDX_8197F)
#define BITS_HI3Q_HOST_IDX_8197F                                               \
	(BIT_MASK_HI3Q_HOST_IDX_8197F << BIT_SHIFT_HI3Q_HOST_IDX_8197F)
#define BIT_CLEAR_HI3Q_HOST_IDX_8197F(x) ((x) & (~BITS_HI3Q_HOST_IDX_8197F))
#define BIT_GET_HI3Q_HOST_IDX_8197F(x)                                         \
	(((x) >> BIT_SHIFT_HI3Q_HOST_IDX_8197F) & BIT_MASK_HI3Q_HOST_IDX_8197F)
#define BIT_SET_HI3Q_HOST_IDX_8197F(x, v)                                      \
	(BIT_CLEAR_HI3Q_HOST_IDX_8197F(x) | BIT_HI3Q_HOST_IDX_8197F(v))

/* 2 REG_HI4Q_TXBD_IDX_8197F */

#define BIT_SHIFT_HI4Q_HW_IDX_8197F 16
#define BIT_MASK_HI4Q_HW_IDX_8197F 0xfff
#define BIT_HI4Q_HW_IDX_8197F(x)                                               \
	(((x) & BIT_MASK_HI4Q_HW_IDX_8197F) << BIT_SHIFT_HI4Q_HW_IDX_8197F)
#define BITS_HI4Q_HW_IDX_8197F                                                 \
	(BIT_MASK_HI4Q_HW_IDX_8197F << BIT_SHIFT_HI4Q_HW_IDX_8197F)
#define BIT_CLEAR_HI4Q_HW_IDX_8197F(x) ((x) & (~BITS_HI4Q_HW_IDX_8197F))
#define BIT_GET_HI4Q_HW_IDX_8197F(x)                                           \
	(((x) >> BIT_SHIFT_HI4Q_HW_IDX_8197F) & BIT_MASK_HI4Q_HW_IDX_8197F)
#define BIT_SET_HI4Q_HW_IDX_8197F(x, v)                                        \
	(BIT_CLEAR_HI4Q_HW_IDX_8197F(x) | BIT_HI4Q_HW_IDX_8197F(v))

#define BIT_SHIFT_HI4Q_HOST_IDX_8197F 0
#define BIT_MASK_HI4Q_HOST_IDX_8197F 0xfff
#define BIT_HI4Q_HOST_IDX_8197F(x)                                             \
	(((x) & BIT_MASK_HI4Q_HOST_IDX_8197F) << BIT_SHIFT_HI4Q_HOST_IDX_8197F)
#define BITS_HI4Q_HOST_IDX_8197F                                               \
	(BIT_MASK_HI4Q_HOST_IDX_8197F << BIT_SHIFT_HI4Q_HOST_IDX_8197F)
#define BIT_CLEAR_HI4Q_HOST_IDX_8197F(x) ((x) & (~BITS_HI4Q_HOST_IDX_8197F))
#define BIT_GET_HI4Q_HOST_IDX_8197F(x)                                         \
	(((x) >> BIT_SHIFT_HI4Q_HOST_IDX_8197F) & BIT_MASK_HI4Q_HOST_IDX_8197F)
#define BIT_SET_HI4Q_HOST_IDX_8197F(x, v)                                      \
	(BIT_CLEAR_HI4Q_HOST_IDX_8197F(x) | BIT_HI4Q_HOST_IDX_8197F(v))

/* 2 REG_HI5Q_TXBD_IDX_8197F */

#define BIT_SHIFT_HI5Q_HW_IDX_8197F 16
#define BIT_MASK_HI5Q_HW_IDX_8197F 0xfff
#define BIT_HI5Q_HW_IDX_8197F(x)                                               \
	(((x) & BIT_MASK_HI5Q_HW_IDX_8197F) << BIT_SHIFT_HI5Q_HW_IDX_8197F)
#define BITS_HI5Q_HW_IDX_8197F                                                 \
	(BIT_MASK_HI5Q_HW_IDX_8197F << BIT_SHIFT_HI5Q_HW_IDX_8197F)
#define BIT_CLEAR_HI5Q_HW_IDX_8197F(x) ((x) & (~BITS_HI5Q_HW_IDX_8197F))
#define BIT_GET_HI5Q_HW_IDX_8197F(x)                                           \
	(((x) >> BIT_SHIFT_HI5Q_HW_IDX_8197F) & BIT_MASK_HI5Q_HW_IDX_8197F)
#define BIT_SET_HI5Q_HW_IDX_8197F(x, v)                                        \
	(BIT_CLEAR_HI5Q_HW_IDX_8197F(x) | BIT_HI5Q_HW_IDX_8197F(v))

#define BIT_SHIFT_HI5Q_HOST_IDX_8197F 0
#define BIT_MASK_HI5Q_HOST_IDX_8197F 0xfff
#define BIT_HI5Q_HOST_IDX_8197F(x)                                             \
	(((x) & BIT_MASK_HI5Q_HOST_IDX_8197F) << BIT_SHIFT_HI5Q_HOST_IDX_8197F)
#define BITS_HI5Q_HOST_IDX_8197F                                               \
	(BIT_MASK_HI5Q_HOST_IDX_8197F << BIT_SHIFT_HI5Q_HOST_IDX_8197F)
#define BIT_CLEAR_HI5Q_HOST_IDX_8197F(x) ((x) & (~BITS_HI5Q_HOST_IDX_8197F))
#define BIT_GET_HI5Q_HOST_IDX_8197F(x)                                         \
	(((x) >> BIT_SHIFT_HI5Q_HOST_IDX_8197F) & BIT_MASK_HI5Q_HOST_IDX_8197F)
#define BIT_SET_HI5Q_HOST_IDX_8197F(x, v)                                      \
	(BIT_CLEAR_HI5Q_HOST_IDX_8197F(x) | BIT_HI5Q_HOST_IDX_8197F(v))

/* 2 REG_HI6Q_TXBD_IDX_8197F */

#define BIT_SHIFT_HI6Q_HW_IDX_8197F 16
#define BIT_MASK_HI6Q_HW_IDX_8197F 0xfff
#define BIT_HI6Q_HW_IDX_8197F(x)                                               \
	(((x) & BIT_MASK_HI6Q_HW_IDX_8197F) << BIT_SHIFT_HI6Q_HW_IDX_8197F)
#define BITS_HI6Q_HW_IDX_8197F                                                 \
	(BIT_MASK_HI6Q_HW_IDX_8197F << BIT_SHIFT_HI6Q_HW_IDX_8197F)
#define BIT_CLEAR_HI6Q_HW_IDX_8197F(x) ((x) & (~BITS_HI6Q_HW_IDX_8197F))
#define BIT_GET_HI6Q_HW_IDX_8197F(x)                                           \
	(((x) >> BIT_SHIFT_HI6Q_HW_IDX_8197F) & BIT_MASK_HI6Q_HW_IDX_8197F)
#define BIT_SET_HI6Q_HW_IDX_8197F(x, v)                                        \
	(BIT_CLEAR_HI6Q_HW_IDX_8197F(x) | BIT_HI6Q_HW_IDX_8197F(v))

#define BIT_SHIFT_HI6Q_HOST_IDX_8197F 0
#define BIT_MASK_HI6Q_HOST_IDX_8197F 0xfff
#define BIT_HI6Q_HOST_IDX_8197F(x)                                             \
	(((x) & BIT_MASK_HI6Q_HOST_IDX_8197F) << BIT_SHIFT_HI6Q_HOST_IDX_8197F)
#define BITS_HI6Q_HOST_IDX_8197F                                               \
	(BIT_MASK_HI6Q_HOST_IDX_8197F << BIT_SHIFT_HI6Q_HOST_IDX_8197F)
#define BIT_CLEAR_HI6Q_HOST_IDX_8197F(x) ((x) & (~BITS_HI6Q_HOST_IDX_8197F))
#define BIT_GET_HI6Q_HOST_IDX_8197F(x)                                         \
	(((x) >> BIT_SHIFT_HI6Q_HOST_IDX_8197F) & BIT_MASK_HI6Q_HOST_IDX_8197F)
#define BIT_SET_HI6Q_HOST_IDX_8197F(x, v)                                      \
	(BIT_CLEAR_HI6Q_HOST_IDX_8197F(x) | BIT_HI6Q_HOST_IDX_8197F(v))

/* 2 REG_HI7Q_TXBD_IDX_8197F */

#define BIT_SHIFT_HI7Q_HW_IDX_8197F 16
#define BIT_MASK_HI7Q_HW_IDX_8197F 0xfff
#define BIT_HI7Q_HW_IDX_8197F(x)                                               \
	(((x) & BIT_MASK_HI7Q_HW_IDX_8197F) << BIT_SHIFT_HI7Q_HW_IDX_8197F)
#define BITS_HI7Q_HW_IDX_8197F                                                 \
	(BIT_MASK_HI7Q_HW_IDX_8197F << BIT_SHIFT_HI7Q_HW_IDX_8197F)
#define BIT_CLEAR_HI7Q_HW_IDX_8197F(x) ((x) & (~BITS_HI7Q_HW_IDX_8197F))
#define BIT_GET_HI7Q_HW_IDX_8197F(x)                                           \
	(((x) >> BIT_SHIFT_HI7Q_HW_IDX_8197F) & BIT_MASK_HI7Q_HW_IDX_8197F)
#define BIT_SET_HI7Q_HW_IDX_8197F(x, v)                                        \
	(BIT_CLEAR_HI7Q_HW_IDX_8197F(x) | BIT_HI7Q_HW_IDX_8197F(v))

#define BIT_SHIFT_HI7Q_HOST_IDX_8197F 0
#define BIT_MASK_HI7Q_HOST_IDX_8197F 0xfff
#define BIT_HI7Q_HOST_IDX_8197F(x)                                             \
	(((x) & BIT_MASK_HI7Q_HOST_IDX_8197F) << BIT_SHIFT_HI7Q_HOST_IDX_8197F)
#define BITS_HI7Q_HOST_IDX_8197F                                               \
	(BIT_MASK_HI7Q_HOST_IDX_8197F << BIT_SHIFT_HI7Q_HOST_IDX_8197F)
#define BIT_CLEAR_HI7Q_HOST_IDX_8197F(x) ((x) & (~BITS_HI7Q_HOST_IDX_8197F))
#define BIT_GET_HI7Q_HOST_IDX_8197F(x)                                         \
	(((x) >> BIT_SHIFT_HI7Q_HOST_IDX_8197F) & BIT_MASK_HI7Q_HOST_IDX_8197F)
#define BIT_SET_HI7Q_HOST_IDX_8197F(x, v)                                      \
	(BIT_CLEAR_HI7Q_HOST_IDX_8197F(x) | BIT_HI7Q_HOST_IDX_8197F(v))

/* 2 REG_DBG_SEL_V1_8197F */

#define BIT_SHIFT_DBG_SEL_8197F 0
#define BIT_MASK_DBG_SEL_8197F 0xff
#define BIT_DBG_SEL_8197F(x)                                                   \
	(((x) & BIT_MASK_DBG_SEL_8197F) << BIT_SHIFT_DBG_SEL_8197F)
#define BITS_DBG_SEL_8197F (BIT_MASK_DBG_SEL_8197F << BIT_SHIFT_DBG_SEL_8197F)
#define BIT_CLEAR_DBG_SEL_8197F(x) ((x) & (~BITS_DBG_SEL_8197F))
#define BIT_GET_DBG_SEL_8197F(x)                                               \
	(((x) >> BIT_SHIFT_DBG_SEL_8197F) & BIT_MASK_DBG_SEL_8197F)
#define BIT_SET_DBG_SEL_8197F(x, v)                                            \
	(BIT_CLEAR_DBG_SEL_8197F(x) | BIT_DBG_SEL_8197F(v))

/* 2 REG_HCI_HRPWM1_V1_8197F */

#define BIT_SHIFT_HCI_HRPWM_8197F 0
#define BIT_MASK_HCI_HRPWM_8197F 0xff
#define BIT_HCI_HRPWM_8197F(x)                                                 \
	(((x) & BIT_MASK_HCI_HRPWM_8197F) << BIT_SHIFT_HCI_HRPWM_8197F)
#define BITS_HCI_HRPWM_8197F                                                   \
	(BIT_MASK_HCI_HRPWM_8197F << BIT_SHIFT_HCI_HRPWM_8197F)
#define BIT_CLEAR_HCI_HRPWM_8197F(x) ((x) & (~BITS_HCI_HRPWM_8197F))
#define BIT_GET_HCI_HRPWM_8197F(x)                                             \
	(((x) >> BIT_SHIFT_HCI_HRPWM_8197F) & BIT_MASK_HCI_HRPWM_8197F)
#define BIT_SET_HCI_HRPWM_8197F(x, v)                                          \
	(BIT_CLEAR_HCI_HRPWM_8197F(x) | BIT_HCI_HRPWM_8197F(v))

/* 2 REG_HCI_HCPWM1_V1_8197F */

#define BIT_SHIFT_HCI_HCPWM_8197F 0
#define BIT_MASK_HCI_HCPWM_8197F 0xff
#define BIT_HCI_HCPWM_8197F(x)                                                 \
	(((x) & BIT_MASK_HCI_HCPWM_8197F) << BIT_SHIFT_HCI_HCPWM_8197F)
#define BITS_HCI_HCPWM_8197F                                                   \
	(BIT_MASK_HCI_HCPWM_8197F << BIT_SHIFT_HCI_HCPWM_8197F)
#define BIT_CLEAR_HCI_HCPWM_8197F(x) ((x) & (~BITS_HCI_HCPWM_8197F))
#define BIT_GET_HCI_HCPWM_8197F(x)                                             \
	(((x) >> BIT_SHIFT_HCI_HCPWM_8197F) & BIT_MASK_HCI_HCPWM_8197F)
#define BIT_SET_HCI_HCPWM_8197F(x, v)                                          \
	(BIT_CLEAR_HCI_HCPWM_8197F(x) | BIT_HCI_HCPWM_8197F(v))

/* 2 REG_HCI_CTRL2_8197F */
#define BIT_DIS_TXDMA_PRE_8197F BIT(7)
#define BIT_DIS_RXDMA_PRE_8197F BIT(6)

#define BIT_SHIFT_HPS_CLKR_HCI_8197F 4
#define BIT_MASK_HPS_CLKR_HCI_8197F 0x3
#define BIT_HPS_CLKR_HCI_8197F(x)                                              \
	(((x) & BIT_MASK_HPS_CLKR_HCI_8197F) << BIT_SHIFT_HPS_CLKR_HCI_8197F)
#define BITS_HPS_CLKR_HCI_8197F                                                \
	(BIT_MASK_HPS_CLKR_HCI_8197F << BIT_SHIFT_HPS_CLKR_HCI_8197F)
#define BIT_CLEAR_HPS_CLKR_HCI_8197F(x) ((x) & (~BITS_HPS_CLKR_HCI_8197F))
#define BIT_GET_HPS_CLKR_HCI_8197F(x)                                          \
	(((x) >> BIT_SHIFT_HPS_CLKR_HCI_8197F) & BIT_MASK_HPS_CLKR_HCI_8197F)
#define BIT_SET_HPS_CLKR_HCI_8197F(x, v)                                       \
	(BIT_CLEAR_HPS_CLKR_HCI_8197F(x) | BIT_HPS_CLKR_HCI_8197F(v))

#define BIT_HCI_INT_8197F BIT(3)
#define BIT_TXFLAG_EXIT_L1_EN_8197F BIT(2)
#define BIT_EN_RXDMA_ALIGN_V1_8197F BIT(1)
#define BIT_EN_TXDMA_ALIGN_V1_8197F BIT(0)

/* 2 REG_HCI_HRPWM2_V1_8197F */

#define BIT_SHIFT_HCI_HRPWM2_8197F 0
#define BIT_MASK_HCI_HRPWM2_8197F 0xffff
#define BIT_HCI_HRPWM2_8197F(x)                                                \
	(((x) & BIT_MASK_HCI_HRPWM2_8197F) << BIT_SHIFT_HCI_HRPWM2_8197F)
#define BITS_HCI_HRPWM2_8197F                                                  \
	(BIT_MASK_HCI_HRPWM2_8197F << BIT_SHIFT_HCI_HRPWM2_8197F)
#define BIT_CLEAR_HCI_HRPWM2_8197F(x) ((x) & (~BITS_HCI_HRPWM2_8197F))
#define BIT_GET_HCI_HRPWM2_8197F(x)                                            \
	(((x) >> BIT_SHIFT_HCI_HRPWM2_8197F) & BIT_MASK_HCI_HRPWM2_8197F)
#define BIT_SET_HCI_HRPWM2_8197F(x, v)                                         \
	(BIT_CLEAR_HCI_HRPWM2_8197F(x) | BIT_HCI_HRPWM2_8197F(v))

/* 2 REG_HCI_HCPWM2_V1_8197F */

#define BIT_SHIFT_HCI_HCPWM2_8197F 0
#define BIT_MASK_HCI_HCPWM2_8197F 0xffff
#define BIT_HCI_HCPWM2_8197F(x)                                                \
	(((x) & BIT_MASK_HCI_HCPWM2_8197F) << BIT_SHIFT_HCI_HCPWM2_8197F)
#define BITS_HCI_HCPWM2_8197F                                                  \
	(BIT_MASK_HCI_HCPWM2_8197F << BIT_SHIFT_HCI_HCPWM2_8197F)
#define BIT_CLEAR_HCI_HCPWM2_8197F(x) ((x) & (~BITS_HCI_HCPWM2_8197F))
#define BIT_GET_HCI_HCPWM2_8197F(x)                                            \
	(((x) >> BIT_SHIFT_HCI_HCPWM2_8197F) & BIT_MASK_HCI_HCPWM2_8197F)
#define BIT_SET_HCI_HCPWM2_8197F(x, v)                                         \
	(BIT_CLEAR_HCI_HCPWM2_8197F(x) | BIT_HCI_HCPWM2_8197F(v))

/* 2 REG_HCI_H2C_MSG_V1_8197F */

#define BIT_SHIFT_DRV2FW_INFO_8197F 0
#define BIT_MASK_DRV2FW_INFO_8197F 0xffffffffL
#define BIT_DRV2FW_INFO_8197F(x)                                               \
	(((x) & BIT_MASK_DRV2FW_INFO_8197F) << BIT_SHIFT_DRV2FW_INFO_8197F)
#define BITS_DRV2FW_INFO_8197F                                                 \
	(BIT_MASK_DRV2FW_INFO_8197F << BIT_SHIFT_DRV2FW_INFO_8197F)
#define BIT_CLEAR_DRV2FW_INFO_8197F(x) ((x) & (~BITS_DRV2FW_INFO_8197F))
#define BIT_GET_DRV2FW_INFO_8197F(x)                                           \
	(((x) >> BIT_SHIFT_DRV2FW_INFO_8197F) & BIT_MASK_DRV2FW_INFO_8197F)
#define BIT_SET_DRV2FW_INFO_8197F(x, v)                                        \
	(BIT_CLEAR_DRV2FW_INFO_8197F(x) | BIT_DRV2FW_INFO_8197F(v))

/* 2 REG_HCI_C2H_MSG_V1_8197F */

#define BIT_SHIFT_HCI_C2H_MSG_8197F 0
#define BIT_MASK_HCI_C2H_MSG_8197F 0xffffffffL
#define BIT_HCI_C2H_MSG_8197F(x)                                               \
	(((x) & BIT_MASK_HCI_C2H_MSG_8197F) << BIT_SHIFT_HCI_C2H_MSG_8197F)
#define BITS_HCI_C2H_MSG_8197F                                                 \
	(BIT_MASK_HCI_C2H_MSG_8197F << BIT_SHIFT_HCI_C2H_MSG_8197F)
#define BIT_CLEAR_HCI_C2H_MSG_8197F(x) ((x) & (~BITS_HCI_C2H_MSG_8197F))
#define BIT_GET_HCI_C2H_MSG_8197F(x)                                           \
	(((x) >> BIT_SHIFT_HCI_C2H_MSG_8197F) & BIT_MASK_HCI_C2H_MSG_8197F)
#define BIT_SET_HCI_C2H_MSG_8197F(x, v)                                        \
	(BIT_CLEAR_HCI_C2H_MSG_8197F(x) | BIT_HCI_C2H_MSG_8197F(v))

/* 2 REG_DBI_WDATA_V1_8197F */

#define BIT_SHIFT_DBI_WDATA_8197F 0
#define BIT_MASK_DBI_WDATA_8197F 0xffffffffL
#define BIT_DBI_WDATA_8197F(x)                                                 \
	(((x) & BIT_MASK_DBI_WDATA_8197F) << BIT_SHIFT_DBI_WDATA_8197F)
#define BITS_DBI_WDATA_8197F                                                   \
	(BIT_MASK_DBI_WDATA_8197F << BIT_SHIFT_DBI_WDATA_8197F)
#define BIT_CLEAR_DBI_WDATA_8197F(x) ((x) & (~BITS_DBI_WDATA_8197F))
#define BIT_GET_DBI_WDATA_8197F(x)                                             \
	(((x) >> BIT_SHIFT_DBI_WDATA_8197F) & BIT_MASK_DBI_WDATA_8197F)
#define BIT_SET_DBI_WDATA_8197F(x, v)                                          \
	(BIT_CLEAR_DBI_WDATA_8197F(x) | BIT_DBI_WDATA_8197F(v))

/* 2 REG_DBI_RDATA_V1_8197F */

#define BIT_SHIFT_DBI_RDATA_8197F 0
#define BIT_MASK_DBI_RDATA_8197F 0xffffffffL
#define BIT_DBI_RDATA_8197F(x)                                                 \
	(((x) & BIT_MASK_DBI_RDATA_8197F) << BIT_SHIFT_DBI_RDATA_8197F)
#define BITS_DBI_RDATA_8197F                                                   \
	(BIT_MASK_DBI_RDATA_8197F << BIT_SHIFT_DBI_RDATA_8197F)
#define BIT_CLEAR_DBI_RDATA_8197F(x) ((x) & (~BITS_DBI_RDATA_8197F))
#define BIT_GET_DBI_RDATA_8197F(x)                                             \
	(((x) >> BIT_SHIFT_DBI_RDATA_8197F) & BIT_MASK_DBI_RDATA_8197F)
#define BIT_SET_DBI_RDATA_8197F(x, v)                                          \
	(BIT_CLEAR_DBI_RDATA_8197F(x) | BIT_DBI_RDATA_8197F(v))

/* 2 REG_STUCK_FLAG_V1_8197F */
#define BIT_EN_STUCK_DBG_8197F BIT(26)
#define BIT_RX_STUCK_8197F BIT(25)
#define BIT_TX_STUCK_8197F BIT(24)
#define BIT_DBI_RFLAG_8197F BIT(17)
#define BIT_DBI_WFLAG_8197F BIT(16)

#define BIT_SHIFT_DBI_WREN_8197F 12
#define BIT_MASK_DBI_WREN_8197F 0xf
#define BIT_DBI_WREN_8197F(x)                                                  \
	(((x) & BIT_MASK_DBI_WREN_8197F) << BIT_SHIFT_DBI_WREN_8197F)
#define BITS_DBI_WREN_8197F                                                    \
	(BIT_MASK_DBI_WREN_8197F << BIT_SHIFT_DBI_WREN_8197F)
#define BIT_CLEAR_DBI_WREN_8197F(x) ((x) & (~BITS_DBI_WREN_8197F))
#define BIT_GET_DBI_WREN_8197F(x)                                              \
	(((x) >> BIT_SHIFT_DBI_WREN_8197F) & BIT_MASK_DBI_WREN_8197F)
#define BIT_SET_DBI_WREN_8197F(x, v)                                           \
	(BIT_CLEAR_DBI_WREN_8197F(x) | BIT_DBI_WREN_8197F(v))

#define BIT_SHIFT_DBI_ADDR_8197F 0
#define BIT_MASK_DBI_ADDR_8197F 0xfff
#define BIT_DBI_ADDR_8197F(x)                                                  \
	(((x) & BIT_MASK_DBI_ADDR_8197F) << BIT_SHIFT_DBI_ADDR_8197F)
#define BITS_DBI_ADDR_8197F                                                    \
	(BIT_MASK_DBI_ADDR_8197F << BIT_SHIFT_DBI_ADDR_8197F)
#define BIT_CLEAR_DBI_ADDR_8197F(x) ((x) & (~BITS_DBI_ADDR_8197F))
#define BIT_GET_DBI_ADDR_8197F(x)                                              \
	(((x) >> BIT_SHIFT_DBI_ADDR_8197F) & BIT_MASK_DBI_ADDR_8197F)
#define BIT_SET_DBI_ADDR_8197F(x, v)                                           \
	(BIT_CLEAR_DBI_ADDR_8197F(x) | BIT_DBI_ADDR_8197F(v))

/* 2 REG_MDIO_V1_8197F */

#define BIT_SHIFT_MDIO_RDATA_8197F 16
#define BIT_MASK_MDIO_RDATA_8197F 0xffff
#define BIT_MDIO_RDATA_8197F(x)                                                \
	(((x) & BIT_MASK_MDIO_RDATA_8197F) << BIT_SHIFT_MDIO_RDATA_8197F)
#define BITS_MDIO_RDATA_8197F                                                  \
	(BIT_MASK_MDIO_RDATA_8197F << BIT_SHIFT_MDIO_RDATA_8197F)
#define BIT_CLEAR_MDIO_RDATA_8197F(x) ((x) & (~BITS_MDIO_RDATA_8197F))
#define BIT_GET_MDIO_RDATA_8197F(x)                                            \
	(((x) >> BIT_SHIFT_MDIO_RDATA_8197F) & BIT_MASK_MDIO_RDATA_8197F)
#define BIT_SET_MDIO_RDATA_8197F(x, v)                                         \
	(BIT_CLEAR_MDIO_RDATA_8197F(x) | BIT_MDIO_RDATA_8197F(v))

#define BIT_SHIFT_MDIO_WDATA_8197F 0
#define BIT_MASK_MDIO_WDATA_8197F 0xffff
#define BIT_MDIO_WDATA_8197F(x)                                                \
	(((x) & BIT_MASK_MDIO_WDATA_8197F) << BIT_SHIFT_MDIO_WDATA_8197F)
#define BITS_MDIO_WDATA_8197F                                                  \
	(BIT_MASK_MDIO_WDATA_8197F << BIT_SHIFT_MDIO_WDATA_8197F)
#define BIT_CLEAR_MDIO_WDATA_8197F(x) ((x) & (~BITS_MDIO_WDATA_8197F))
#define BIT_GET_MDIO_WDATA_8197F(x)                                            \
	(((x) >> BIT_SHIFT_MDIO_WDATA_8197F) & BIT_MASK_MDIO_WDATA_8197F)
#define BIT_SET_MDIO_WDATA_8197F(x, v)                                         \
	(BIT_CLEAR_MDIO_WDATA_8197F(x) | BIT_MDIO_WDATA_8197F(v))

/* 2 REG_WDT_CFG_8197F */

#define BIT_SHIFT_MDIO_PHY_ADDR_8197F 24
#define BIT_MASK_MDIO_PHY_ADDR_8197F 0x1f
#define BIT_MDIO_PHY_ADDR_8197F(x)                                             \
	(((x) & BIT_MASK_MDIO_PHY_ADDR_8197F) << BIT_SHIFT_MDIO_PHY_ADDR_8197F)
#define BITS_MDIO_PHY_ADDR_8197F                                               \
	(BIT_MASK_MDIO_PHY_ADDR_8197F << BIT_SHIFT_MDIO_PHY_ADDR_8197F)
#define BIT_CLEAR_MDIO_PHY_ADDR_8197F(x) ((x) & (~BITS_MDIO_PHY_ADDR_8197F))
#define BIT_GET_MDIO_PHY_ADDR_8197F(x)                                         \
	(((x) >> BIT_SHIFT_MDIO_PHY_ADDR_8197F) & BIT_MASK_MDIO_PHY_ADDR_8197F)
#define BIT_SET_MDIO_PHY_ADDR_8197F(x, v)                                      \
	(BIT_CLEAR_MDIO_PHY_ADDR_8197F(x) | BIT_MDIO_PHY_ADDR_8197F(v))

#define BIT_SHIFT_WATCH_DOG_RECORD_V1_8197F 10
#define BIT_MASK_WATCH_DOG_RECORD_V1_8197F 0x3fff
#define BIT_WATCH_DOG_RECORD_V1_8197F(x)                                       \
	(((x) & BIT_MASK_WATCH_DOG_RECORD_V1_8197F)                            \
	 << BIT_SHIFT_WATCH_DOG_RECORD_V1_8197F)
#define BITS_WATCH_DOG_RECORD_V1_8197F                                         \
	(BIT_MASK_WATCH_DOG_RECORD_V1_8197F                                    \
	 << BIT_SHIFT_WATCH_DOG_RECORD_V1_8197F)
#define BIT_CLEAR_WATCH_DOG_RECORD_V1_8197F(x)                                 \
	((x) & (~BITS_WATCH_DOG_RECORD_V1_8197F))
#define BIT_GET_WATCH_DOG_RECORD_V1_8197F(x)                                   \
	(((x) >> BIT_SHIFT_WATCH_DOG_RECORD_V1_8197F) &                        \
	 BIT_MASK_WATCH_DOG_RECORD_V1_8197F)
#define BIT_SET_WATCH_DOG_RECORD_V1_8197F(x, v)                                \
	(BIT_CLEAR_WATCH_DOG_RECORD_V1_8197F(x) |                              \
	 BIT_WATCH_DOG_RECORD_V1_8197F(v))

#define BIT_R_IO_TIMEOUT_FLAG_V1_8197F BIT(9)
#define BIT_EN_WATCH_DOG_V1_8197F BIT(8)
#define BIT_ECRC_EN_V1_8197F BIT(7)
#define BIT_MDIO_RFLAG_V1_8197F BIT(6)
#define BIT_MDIO_WFLAG_V1_8197F BIT(5)

#define BIT_SHIFT_MDIO_REG_ADDR_8197F 0
#define BIT_MASK_MDIO_REG_ADDR_8197F 0x1f
#define BIT_MDIO_REG_ADDR_8197F(x)                                             \
	(((x) & BIT_MASK_MDIO_REG_ADDR_8197F) << BIT_SHIFT_MDIO_REG_ADDR_8197F)
#define BITS_MDIO_REG_ADDR_8197F                                               \
	(BIT_MASK_MDIO_REG_ADDR_8197F << BIT_SHIFT_MDIO_REG_ADDR_8197F)
#define BIT_CLEAR_MDIO_REG_ADDR_8197F(x) ((x) & (~BITS_MDIO_REG_ADDR_8197F))
#define BIT_GET_MDIO_REG_ADDR_8197F(x)                                         \
	(((x) >> BIT_SHIFT_MDIO_REG_ADDR_8197F) & BIT_MASK_MDIO_REG_ADDR_8197F)
#define BIT_SET_MDIO_REG_ADDR_8197F(x, v)                                      \
	(BIT_CLEAR_MDIO_REG_ADDR_8197F(x) | BIT_MDIO_REG_ADDR_8197F(v))

/* 2 REG_HCI_MIX_CFG_8197F */
#define BIT_RXRST_BACKDOOR_8197F BIT(31)
#define BIT_TXRST_BACKDOOR_8197F BIT(30)
#define BIT_RXIDX_RSTB_8197F BIT(29)
#define BIT_TXIDX_RSTB_8197F BIT(28)
#define BIT_DROP_NEXT_RXPKT_8197F BIT(27)
#define BIT_SHORT_CORE_RST_SEL_8197F BIT(26)
#define BIT_EXCEPT_RESUME_EN_8197F BIT(25)
#define BIT_EXCEPT_RESUME_FLAG_8197F BIT(24)
#define BIT_ALIGN_MTU_8197F BIT(23)
#define BIT_HOST_GEN2_SUPPORT_8197F BIT(20)

#define BIT_SHIFT_TXDMA_ERR_FLAG_8197F 16
#define BIT_MASK_TXDMA_ERR_FLAG_8197F 0xf
#define BIT_TXDMA_ERR_FLAG_8197F(x)                                            \
	(((x) & BIT_MASK_TXDMA_ERR_FLAG_8197F)                                 \
	 << BIT_SHIFT_TXDMA_ERR_FLAG_8197F)
#define BITS_TXDMA_ERR_FLAG_8197F                                              \
	(BIT_MASK_TXDMA_ERR_FLAG_8197F << BIT_SHIFT_TXDMA_ERR_FLAG_8197F)
#define BIT_CLEAR_TXDMA_ERR_FLAG_8197F(x) ((x) & (~BITS_TXDMA_ERR_FLAG_8197F))
#define BIT_GET_TXDMA_ERR_FLAG_8197F(x)                                        \
	(((x) >> BIT_SHIFT_TXDMA_ERR_FLAG_8197F) &                             \
	 BIT_MASK_TXDMA_ERR_FLAG_8197F)
#define BIT_SET_TXDMA_ERR_FLAG_8197F(x, v)                                     \
	(BIT_CLEAR_TXDMA_ERR_FLAG_8197F(x) | BIT_TXDMA_ERR_FLAG_8197F(v))

#define BIT_SHIFT_EARLY_MODE_SEL_8197F 12
#define BIT_MASK_EARLY_MODE_SEL_8197F 0xf
#define BIT_EARLY_MODE_SEL_8197F(x)                                            \
	(((x) & BIT_MASK_EARLY_MODE_SEL_8197F)                                 \
	 << BIT_SHIFT_EARLY_MODE_SEL_8197F)
#define BITS_EARLY_MODE_SEL_8197F                                              \
	(BIT_MASK_EARLY_MODE_SEL_8197F << BIT_SHIFT_EARLY_MODE_SEL_8197F)
#define BIT_CLEAR_EARLY_MODE_SEL_8197F(x) ((x) & (~BITS_EARLY_MODE_SEL_8197F))
#define BIT_GET_EARLY_MODE_SEL_8197F(x)                                        \
	(((x) >> BIT_SHIFT_EARLY_MODE_SEL_8197F) &                             \
	 BIT_MASK_EARLY_MODE_SEL_8197F)
#define BIT_SET_EARLY_MODE_SEL_8197F(x, v)                                     \
	(BIT_CLEAR_EARLY_MODE_SEL_8197F(x) | BIT_EARLY_MODE_SEL_8197F(v))

#define BIT_EPHY_RX50_EN_8197F BIT(11)

#define BIT_SHIFT_MSI_TIMEOUT_ID_V1_8197F 8
#define BIT_MASK_MSI_TIMEOUT_ID_V1_8197F 0x7
#define BIT_MSI_TIMEOUT_ID_V1_8197F(x)                                         \
	(((x) & BIT_MASK_MSI_TIMEOUT_ID_V1_8197F)                              \
	 << BIT_SHIFT_MSI_TIMEOUT_ID_V1_8197F)
#define BITS_MSI_TIMEOUT_ID_V1_8197F                                           \
	(BIT_MASK_MSI_TIMEOUT_ID_V1_8197F << BIT_SHIFT_MSI_TIMEOUT_ID_V1_8197F)
#define BIT_CLEAR_MSI_TIMEOUT_ID_V1_8197F(x)                                   \
	((x) & (~BITS_MSI_TIMEOUT_ID_V1_8197F))
#define BIT_GET_MSI_TIMEOUT_ID_V1_8197F(x)                                     \
	(((x) >> BIT_SHIFT_MSI_TIMEOUT_ID_V1_8197F) &                          \
	 BIT_MASK_MSI_TIMEOUT_ID_V1_8197F)
#define BIT_SET_MSI_TIMEOUT_ID_V1_8197F(x, v)                                  \
	(BIT_CLEAR_MSI_TIMEOUT_ID_V1_8197F(x) | BIT_MSI_TIMEOUT_ID_V1_8197F(v))

#define BIT_RADDR_RD_8197F BIT(7)
#define BIT_EN_MUL_TAG_8197F BIT(6)
#define BIT_EN_EARLY_MODE_8197F BIT(5)
#define BIT_L0S_LINK_OFF_8197F BIT(4)
#define BIT_ACT_LINK_OFF_8197F BIT(3)

/* 2 REG_STC_INT_CS_8197F(HCI STATE CHANGE INTERRUPT CONTROL AND STATUS) */
#define BIT_STC_INT_EN_8197F BIT(31)

#define BIT_SHIFT_STC_INT_FLAG_8197F 16
#define BIT_MASK_STC_INT_FLAG_8197F 0xff
#define BIT_STC_INT_FLAG_8197F(x)                                              \
	(((x) & BIT_MASK_STC_INT_FLAG_8197F) << BIT_SHIFT_STC_INT_FLAG_8197F)
#define BITS_STC_INT_FLAG_8197F                                                \
	(BIT_MASK_STC_INT_FLAG_8197F << BIT_SHIFT_STC_INT_FLAG_8197F)
#define BIT_CLEAR_STC_INT_FLAG_8197F(x) ((x) & (~BITS_STC_INT_FLAG_8197F))
#define BIT_GET_STC_INT_FLAG_8197F(x)                                          \
	(((x) >> BIT_SHIFT_STC_INT_FLAG_8197F) & BIT_MASK_STC_INT_FLAG_8197F)
#define BIT_SET_STC_INT_FLAG_8197F(x, v)                                       \
	(BIT_CLEAR_STC_INT_FLAG_8197F(x) | BIT_STC_INT_FLAG_8197F(v))

#define BIT_SHIFT_STC_INT_IDX_8197F 8
#define BIT_MASK_STC_INT_IDX_8197F 0x7
#define BIT_STC_INT_IDX_8197F(x)                                               \
	(((x) & BIT_MASK_STC_INT_IDX_8197F) << BIT_SHIFT_STC_INT_IDX_8197F)
#define BITS_STC_INT_IDX_8197F                                                 \
	(BIT_MASK_STC_INT_IDX_8197F << BIT_SHIFT_STC_INT_IDX_8197F)
#define BIT_CLEAR_STC_INT_IDX_8197F(x) ((x) & (~BITS_STC_INT_IDX_8197F))
#define BIT_GET_STC_INT_IDX_8197F(x)                                           \
	(((x) >> BIT_SHIFT_STC_INT_IDX_8197F) & BIT_MASK_STC_INT_IDX_8197F)
#define BIT_SET_STC_INT_IDX_8197F(x, v)                                        \
	(BIT_CLEAR_STC_INT_IDX_8197F(x) | BIT_STC_INT_IDX_8197F(v))

#define BIT_SHIFT_STC_INT_REALTIME_CS_8197F 0
#define BIT_MASK_STC_INT_REALTIME_CS_8197F 0x3f
#define BIT_STC_INT_REALTIME_CS_8197F(x)                                       \
	(((x) & BIT_MASK_STC_INT_REALTIME_CS_8197F)                            \
	 << BIT_SHIFT_STC_INT_REALTIME_CS_8197F)
#define BITS_STC_INT_REALTIME_CS_8197F                                         \
	(BIT_MASK_STC_INT_REALTIME_CS_8197F                                    \
	 << BIT_SHIFT_STC_INT_REALTIME_CS_8197F)
#define BIT_CLEAR_STC_INT_REALTIME_CS_8197F(x)                                 \
	((x) & (~BITS_STC_INT_REALTIME_CS_8197F))
#define BIT_GET_STC_INT_REALTIME_CS_8197F(x)                                   \
	(((x) >> BIT_SHIFT_STC_INT_REALTIME_CS_8197F) &                        \
	 BIT_MASK_STC_INT_REALTIME_CS_8197F)
#define BIT_SET_STC_INT_REALTIME_CS_8197F(x, v)                                \
	(BIT_CLEAR_STC_INT_REALTIME_CS_8197F(x) |                              \
	 BIT_STC_INT_REALTIME_CS_8197F(v))

/* 2 REG_ST_INT_CFG_8197F(HCI STATE CHANGE INTERRUPT CONFIGURATION) */
#define BIT_STC_INT_GRP_EN_8197F BIT(31)

#define BIT_SHIFT_STC_INT_EXPECT_LS_8197F 8
#define BIT_MASK_STC_INT_EXPECT_LS_8197F 0x3f
#define BIT_STC_INT_EXPECT_LS_8197F(x)                                         \
	(((x) & BIT_MASK_STC_INT_EXPECT_LS_8197F)                              \
	 << BIT_SHIFT_STC_INT_EXPECT_LS_8197F)
#define BITS_STC_INT_EXPECT_LS_8197F                                           \
	(BIT_MASK_STC_INT_EXPECT_LS_8197F << BIT_SHIFT_STC_INT_EXPECT_LS_8197F)
#define BIT_CLEAR_STC_INT_EXPECT_LS_8197F(x)                                   \
	((x) & (~BITS_STC_INT_EXPECT_LS_8197F))
#define BIT_GET_STC_INT_EXPECT_LS_8197F(x)                                     \
	(((x) >> BIT_SHIFT_STC_INT_EXPECT_LS_8197F) &                          \
	 BIT_MASK_STC_INT_EXPECT_LS_8197F)
#define BIT_SET_STC_INT_EXPECT_LS_8197F(x, v)                                  \
	(BIT_CLEAR_STC_INT_EXPECT_LS_8197F(x) | BIT_STC_INT_EXPECT_LS_8197F(v))

#define BIT_SHIFT_STC_INT_EXPECT_CS_8197F 0
#define BIT_MASK_STC_INT_EXPECT_CS_8197F 0x3f
#define BIT_STC_INT_EXPECT_CS_8197F(x)                                         \
	(((x) & BIT_MASK_STC_INT_EXPECT_CS_8197F)                              \
	 << BIT_SHIFT_STC_INT_EXPECT_CS_8197F)
#define BITS_STC_INT_EXPECT_CS_8197F                                           \
	(BIT_MASK_STC_INT_EXPECT_CS_8197F << BIT_SHIFT_STC_INT_EXPECT_CS_8197F)
#define BIT_CLEAR_STC_INT_EXPECT_CS_8197F(x)                                   \
	((x) & (~BITS_STC_INT_EXPECT_CS_8197F))
#define BIT_GET_STC_INT_EXPECT_CS_8197F(x)                                     \
	(((x) >> BIT_SHIFT_STC_INT_EXPECT_CS_8197F) &                          \
	 BIT_MASK_STC_INT_EXPECT_CS_8197F)
#define BIT_SET_STC_INT_EXPECT_CS_8197F(x, v)                                  \
	(BIT_CLEAR_STC_INT_EXPECT_CS_8197F(x) | BIT_STC_INT_EXPECT_CS_8197F(v))

/* 2 REG_CMU_DLY_CTRL_8197F(HCI PHY CLOCK MGT UNIT DELAY CONTROL ) */
#define BIT_CMU_DLY_EN_8197F BIT(31)
#define BIT_CMU_DLY_MODE_8197F BIT(30)

#define BIT_SHIFT_CMU_DLY_PRE_DIV_8197F 0
#define BIT_MASK_CMU_DLY_PRE_DIV_8197F 0xff
#define BIT_CMU_DLY_PRE_DIV_8197F(x)                                           \
	(((x) & BIT_MASK_CMU_DLY_PRE_DIV_8197F)                                \
	 << BIT_SHIFT_CMU_DLY_PRE_DIV_8197F)
#define BITS_CMU_DLY_PRE_DIV_8197F                                             \
	(BIT_MASK_CMU_DLY_PRE_DIV_8197F << BIT_SHIFT_CMU_DLY_PRE_DIV_8197F)
#define BIT_CLEAR_CMU_DLY_PRE_DIV_8197F(x) ((x) & (~BITS_CMU_DLY_PRE_DIV_8197F))
#define BIT_GET_CMU_DLY_PRE_DIV_8197F(x)                                       \
	(((x) >> BIT_SHIFT_CMU_DLY_PRE_DIV_8197F) &                            \
	 BIT_MASK_CMU_DLY_PRE_DIV_8197F)
#define BIT_SET_CMU_DLY_PRE_DIV_8197F(x, v)                                    \
	(BIT_CLEAR_CMU_DLY_PRE_DIV_8197F(x) | BIT_CMU_DLY_PRE_DIV_8197F(v))

/* 2 REG_CMU_DLY_CFG_8197F(HCI PHY CLOCK MGT UNIT DELAY CONFIGURATION ) */

#define BIT_SHIFT_CMU_DLY_LTR_A2I_8197F 24
#define BIT_MASK_CMU_DLY_LTR_A2I_8197F 0xff
#define BIT_CMU_DLY_LTR_A2I_8197F(x)                                           \
	(((x) & BIT_MASK_CMU_DLY_LTR_A2I_8197F)                                \
	 << BIT_SHIFT_CMU_DLY_LTR_A2I_8197F)
#define BITS_CMU_DLY_LTR_A2I_8197F                                             \
	(BIT_MASK_CMU_DLY_LTR_A2I_8197F << BIT_SHIFT_CMU_DLY_LTR_A2I_8197F)
#define BIT_CLEAR_CMU_DLY_LTR_A2I_8197F(x) ((x) & (~BITS_CMU_DLY_LTR_A2I_8197F))
#define BIT_GET_CMU_DLY_LTR_A2I_8197F(x)                                       \
	(((x) >> BIT_SHIFT_CMU_DLY_LTR_A2I_8197F) &                            \
	 BIT_MASK_CMU_DLY_LTR_A2I_8197F)
#define BIT_SET_CMU_DLY_LTR_A2I_8197F(x, v)                                    \
	(BIT_CLEAR_CMU_DLY_LTR_A2I_8197F(x) | BIT_CMU_DLY_LTR_A2I_8197F(v))

#define BIT_SHIFT_CMU_DLY_LTR_I2A_8197F 16
#define BIT_MASK_CMU_DLY_LTR_I2A_8197F 0xff
#define BIT_CMU_DLY_LTR_I2A_8197F(x)                                           \
	(((x) & BIT_MASK_CMU_DLY_LTR_I2A_8197F)                                \
	 << BIT_SHIFT_CMU_DLY_LTR_I2A_8197F)
#define BITS_CMU_DLY_LTR_I2A_8197F                                             \
	(BIT_MASK_CMU_DLY_LTR_I2A_8197F << BIT_SHIFT_CMU_DLY_LTR_I2A_8197F)
#define BIT_CLEAR_CMU_DLY_LTR_I2A_8197F(x) ((x) & (~BITS_CMU_DLY_LTR_I2A_8197F))
#define BIT_GET_CMU_DLY_LTR_I2A_8197F(x)                                       \
	(((x) >> BIT_SHIFT_CMU_DLY_LTR_I2A_8197F) &                            \
	 BIT_MASK_CMU_DLY_LTR_I2A_8197F)
#define BIT_SET_CMU_DLY_LTR_I2A_8197F(x, v)                                    \
	(BIT_CLEAR_CMU_DLY_LTR_I2A_8197F(x) | BIT_CMU_DLY_LTR_I2A_8197F(v))

#define BIT_SHIFT_CMU_DLY_LTR_IDLE_8197F 8
#define BIT_MASK_CMU_DLY_LTR_IDLE_8197F 0xff
#define BIT_CMU_DLY_LTR_IDLE_8197F(x)                                          \
	(((x) & BIT_MASK_CMU_DLY_LTR_IDLE_8197F)                               \
	 << BIT_SHIFT_CMU_DLY_LTR_IDLE_8197F)
#define BITS_CMU_DLY_LTR_IDLE_8197F                                            \
	(BIT_MASK_CMU_DLY_LTR_IDLE_8197F << BIT_SHIFT_CMU_DLY_LTR_IDLE_8197F)
#define BIT_CLEAR_CMU_DLY_LTR_IDLE_8197F(x)                                    \
	((x) & (~BITS_CMU_DLY_LTR_IDLE_8197F))
#define BIT_GET_CMU_DLY_LTR_IDLE_8197F(x)                                      \
	(((x) >> BIT_SHIFT_CMU_DLY_LTR_IDLE_8197F) &                           \
	 BIT_MASK_CMU_DLY_LTR_IDLE_8197F)
#define BIT_SET_CMU_DLY_LTR_IDLE_8197F(x, v)                                   \
	(BIT_CLEAR_CMU_DLY_LTR_IDLE_8197F(x) | BIT_CMU_DLY_LTR_IDLE_8197F(v))

#define BIT_SHIFT_CMU_DLY_LTR_ACT_8197F 0
#define BIT_MASK_CMU_DLY_LTR_ACT_8197F 0xff
#define BIT_CMU_DLY_LTR_ACT_8197F(x)                                           \
	(((x) & BIT_MASK_CMU_DLY_LTR_ACT_8197F)                                \
	 << BIT_SHIFT_CMU_DLY_LTR_ACT_8197F)
#define BITS_CMU_DLY_LTR_ACT_8197F                                             \
	(BIT_MASK_CMU_DLY_LTR_ACT_8197F << BIT_SHIFT_CMU_DLY_LTR_ACT_8197F)
#define BIT_CLEAR_CMU_DLY_LTR_ACT_8197F(x) ((x) & (~BITS_CMU_DLY_LTR_ACT_8197F))
#define BIT_GET_CMU_DLY_LTR_ACT_8197F(x)                                       \
	(((x) >> BIT_SHIFT_CMU_DLY_LTR_ACT_8197F) &                            \
	 BIT_MASK_CMU_DLY_LTR_ACT_8197F)
#define BIT_SET_CMU_DLY_LTR_ACT_8197F(x, v)                                    \
	(BIT_CLEAR_CMU_DLY_LTR_ACT_8197F(x) | BIT_CMU_DLY_LTR_ACT_8197F(v))

/* 2 REG_H2CQ_TXBD_DESA_8197F */

#define BIT_SHIFT_H2CQ_TXBD_DESA_8197F 0
#define BIT_MASK_H2CQ_TXBD_DESA_8197F 0xffffffffffffffffL
#define BIT_H2CQ_TXBD_DESA_8197F(x)                                            \
	(((x) & BIT_MASK_H2CQ_TXBD_DESA_8197F)                                 \
	 << BIT_SHIFT_H2CQ_TXBD_DESA_8197F)
#define BITS_H2CQ_TXBD_DESA_8197F                                              \
	(BIT_MASK_H2CQ_TXBD_DESA_8197F << BIT_SHIFT_H2CQ_TXBD_DESA_8197F)
#define BIT_CLEAR_H2CQ_TXBD_DESA_8197F(x) ((x) & (~BITS_H2CQ_TXBD_DESA_8197F))
#define BIT_GET_H2CQ_TXBD_DESA_8197F(x)                                        \
	(((x) >> BIT_SHIFT_H2CQ_TXBD_DESA_8197F) &                             \
	 BIT_MASK_H2CQ_TXBD_DESA_8197F)
#define BIT_SET_H2CQ_TXBD_DESA_8197F(x, v)                                     \
	(BIT_CLEAR_H2CQ_TXBD_DESA_8197F(x) | BIT_H2CQ_TXBD_DESA_8197F(v))

/* 2 REG_H2CQ_TXBD_NUM_8197F */
#define BIT_HCI_H2CQ_FLAG_8197F BIT(14)

#define BIT_SHIFT_H2CQ_DESC_MODE_8197F 12
#define BIT_MASK_H2CQ_DESC_MODE_8197F 0x3
#define BIT_H2CQ_DESC_MODE_8197F(x)                                            \
	(((x) & BIT_MASK_H2CQ_DESC_MODE_8197F)                                 \
	 << BIT_SHIFT_H2CQ_DESC_MODE_8197F)
#define BITS_H2CQ_DESC_MODE_8197F                                              \
	(BIT_MASK_H2CQ_DESC_MODE_8197F << BIT_SHIFT_H2CQ_DESC_MODE_8197F)
#define BIT_CLEAR_H2CQ_DESC_MODE_8197F(x) ((x) & (~BITS_H2CQ_DESC_MODE_8197F))
#define BIT_GET_H2CQ_DESC_MODE_8197F(x)                                        \
	(((x) >> BIT_SHIFT_H2CQ_DESC_MODE_8197F) &                             \
	 BIT_MASK_H2CQ_DESC_MODE_8197F)
#define BIT_SET_H2CQ_DESC_MODE_8197F(x, v)                                     \
	(BIT_CLEAR_H2CQ_DESC_MODE_8197F(x) | BIT_H2CQ_DESC_MODE_8197F(v))

#define BIT_SHIFT_H2CQ_DESC_NUM_8197F 0
#define BIT_MASK_H2CQ_DESC_NUM_8197F 0xfff
#define BIT_H2CQ_DESC_NUM_8197F(x)                                             \
	(((x) & BIT_MASK_H2CQ_DESC_NUM_8197F) << BIT_SHIFT_H2CQ_DESC_NUM_8197F)
#define BITS_H2CQ_DESC_NUM_8197F                                               \
	(BIT_MASK_H2CQ_DESC_NUM_8197F << BIT_SHIFT_H2CQ_DESC_NUM_8197F)
#define BIT_CLEAR_H2CQ_DESC_NUM_8197F(x) ((x) & (~BITS_H2CQ_DESC_NUM_8197F))
#define BIT_GET_H2CQ_DESC_NUM_8197F(x)                                         \
	(((x) >> BIT_SHIFT_H2CQ_DESC_NUM_8197F) & BIT_MASK_H2CQ_DESC_NUM_8197F)
#define BIT_SET_H2CQ_DESC_NUM_8197F(x, v)                                      \
	(BIT_CLEAR_H2CQ_DESC_NUM_8197F(x) | BIT_H2CQ_DESC_NUM_8197F(v))

/* 2 REG_H2CQ_TXBD_IDX_8197F */

#define BIT_SHIFT_H2CQ_HW_IDX_8197F 16
#define BIT_MASK_H2CQ_HW_IDX_8197F 0xfff
#define BIT_H2CQ_HW_IDX_8197F(x)                                               \
	(((x) & BIT_MASK_H2CQ_HW_IDX_8197F) << BIT_SHIFT_H2CQ_HW_IDX_8197F)
#define BITS_H2CQ_HW_IDX_8197F                                                 \
	(BIT_MASK_H2CQ_HW_IDX_8197F << BIT_SHIFT_H2CQ_HW_IDX_8197F)
#define BIT_CLEAR_H2CQ_HW_IDX_8197F(x) ((x) & (~BITS_H2CQ_HW_IDX_8197F))
#define BIT_GET_H2CQ_HW_IDX_8197F(x)                                           \
	(((x) >> BIT_SHIFT_H2CQ_HW_IDX_8197F) & BIT_MASK_H2CQ_HW_IDX_8197F)
#define BIT_SET_H2CQ_HW_IDX_8197F(x, v)                                        \
	(BIT_CLEAR_H2CQ_HW_IDX_8197F(x) | BIT_H2CQ_HW_IDX_8197F(v))

#define BIT_SHIFT_H2CQ_HOST_IDX_8197F 0
#define BIT_MASK_H2CQ_HOST_IDX_8197F 0xfff
#define BIT_H2CQ_HOST_IDX_8197F(x)                                             \
	(((x) & BIT_MASK_H2CQ_HOST_IDX_8197F) << BIT_SHIFT_H2CQ_HOST_IDX_8197F)
#define BITS_H2CQ_HOST_IDX_8197F                                               \
	(BIT_MASK_H2CQ_HOST_IDX_8197F << BIT_SHIFT_H2CQ_HOST_IDX_8197F)
#define BIT_CLEAR_H2CQ_HOST_IDX_8197F(x) ((x) & (~BITS_H2CQ_HOST_IDX_8197F))
#define BIT_GET_H2CQ_HOST_IDX_8197F(x)                                         \
	(((x) >> BIT_SHIFT_H2CQ_HOST_IDX_8197F) & BIT_MASK_H2CQ_HOST_IDX_8197F)
#define BIT_SET_H2CQ_HOST_IDX_8197F(x, v)                                      \
	(BIT_CLEAR_H2CQ_HOST_IDX_8197F(x) | BIT_H2CQ_HOST_IDX_8197F(v))

/* 2 REG_H2CQ_CSR_8197F[31:0] (H2CQ CONTROL AND STATUS) */
#define BIT_H2CQ_FULL_8197F BIT(31)
#define BIT_CLR_H2CQ_HOST_IDX_8197F BIT(16)
#define BIT_CLR_H2CQ_HW_IDX_8197F BIT(8)
#define BIT_STOP_H2CQ_8197F BIT(0)

/* 2 REG_AXI_EXCEPT_CS_8197F[31:0]	(AXI EXCEPTION CONTROL AND STATUS) */
#define BIT_AXI_RXDMA_TIMEOUT_RE_8197F BIT(21)
#define BIT_AXI_TXDMA_TIMEOUT_RE_8197F BIT(20)
#define BIT_AXI_DECERR_W_RE_8197F BIT(19)
#define BIT_AXI_DECERR_R_RE_8197F BIT(18)
#define BIT_AXI_SLVERR_W_RE_8197F BIT(17)
#define BIT_AXI_SLVERR_R_RE_8197F BIT(16)
#define BIT_AXI_RXDMA_TIMEOUT_IE_8197F BIT(13)
#define BIT_AXI_TXDMA_TIMEOUT_IE_8197F BIT(12)
#define BIT_AXI_DECERR_W_IE_8197F BIT(11)
#define BIT_AXI_DECERR_R_IE_8197F BIT(10)
#define BIT_AXI_SLVERR_W_IE_8197F BIT(9)
#define BIT_AXI_SLVERR_R_IE_8197F BIT(8)
#define BIT_AXI_RXDMA_TIMEOUT_FLAG_8197F BIT(5)
#define BIT_AXI_TXDMA_TIMEOUT_FLAG_8197F BIT(4)
#define BIT_AXI_DECERR_W_FLAG_8197F BIT(3)
#define BIT_AXI_DECERR_R_FLAG_8197F BIT(2)
#define BIT_AXI_SLVERR_W_FLAG_8197F BIT(1)
#define BIT_AXI_SLVERR_R_FLAG_8197F BIT(0)

/* 2 REG_AXI_EXCEPT_TIME_8197F[31:0]	(AXI EXCEPTION TIME CONTROL) */

#define BIT_SHIFT_AXI_RECOVERY_TIME_8197F 24
#define BIT_MASK_AXI_RECOVERY_TIME_8197F 0xff
#define BIT_AXI_RECOVERY_TIME_8197F(x)                                         \
	(((x) & BIT_MASK_AXI_RECOVERY_TIME_8197F)                              \
	 << BIT_SHIFT_AXI_RECOVERY_TIME_8197F)
#define BITS_AXI_RECOVERY_TIME_8197F                                           \
	(BIT_MASK_AXI_RECOVERY_TIME_8197F << BIT_SHIFT_AXI_RECOVERY_TIME_8197F)
#define BIT_CLEAR_AXI_RECOVERY_TIME_8197F(x)                                   \
	((x) & (~BITS_AXI_RECOVERY_TIME_8197F))
#define BIT_GET_AXI_RECOVERY_TIME_8197F(x)                                     \
	(((x) >> BIT_SHIFT_AXI_RECOVERY_TIME_8197F) &                          \
	 BIT_MASK_AXI_RECOVERY_TIME_8197F)
#define BIT_SET_AXI_RECOVERY_TIME_8197F(x, v)                                  \
	(BIT_CLEAR_AXI_RECOVERY_TIME_8197F(x) | BIT_AXI_RECOVERY_TIME_8197F(v))

#define BIT_SHIFT_AXI_RXDMA_TIMEOUT_VAL_8197F 12
#define BIT_MASK_AXI_RXDMA_TIMEOUT_VAL_8197F 0xfff
#define BIT_AXI_RXDMA_TIMEOUT_VAL_8197F(x)                                     \
	(((x) & BIT_MASK_AXI_RXDMA_TIMEOUT_VAL_8197F)                          \
	 << BIT_SHIFT_AXI_RXDMA_TIMEOUT_VAL_8197F)
#define BITS_AXI_RXDMA_TIMEOUT_VAL_8197F                                       \
	(BIT_MASK_AXI_RXDMA_TIMEOUT_VAL_8197F                                  \
	 << BIT_SHIFT_AXI_RXDMA_TIMEOUT_VAL_8197F)
#define BIT_CLEAR_AXI_RXDMA_TIMEOUT_VAL_8197F(x)                               \
	((x) & (~BITS_AXI_RXDMA_TIMEOUT_VAL_8197F))
#define BIT_GET_AXI_RXDMA_TIMEOUT_VAL_8197F(x)                                 \
	(((x) >> BIT_SHIFT_AXI_RXDMA_TIMEOUT_VAL_8197F) &                      \
	 BIT_MASK_AXI_RXDMA_TIMEOUT_VAL_8197F)
#define BIT_SET_AXI_RXDMA_TIMEOUT_VAL_8197F(x, v)                              \
	(BIT_CLEAR_AXI_RXDMA_TIMEOUT_VAL_8197F(x) |                            \
	 BIT_AXI_RXDMA_TIMEOUT_VAL_8197F(v))

#define BIT_SHIFT_AXI_TXDMA_TIMEOUT_VAL_8197F 0
#define BIT_MASK_AXI_TXDMA_TIMEOUT_VAL_8197F 0xfff
#define BIT_AXI_TXDMA_TIMEOUT_VAL_8197F(x)                                     \
	(((x) & BIT_MASK_AXI_TXDMA_TIMEOUT_VAL_8197F)                          \
	 << BIT_SHIFT_AXI_TXDMA_TIMEOUT_VAL_8197F)
#define BITS_AXI_TXDMA_TIMEOUT_VAL_8197F                                       \
	(BIT_MASK_AXI_TXDMA_TIMEOUT_VAL_8197F                                  \
	 << BIT_SHIFT_AXI_TXDMA_TIMEOUT_VAL_8197F)
#define BIT_CLEAR_AXI_TXDMA_TIMEOUT_VAL_8197F(x)                               \
	((x) & (~BITS_AXI_TXDMA_TIMEOUT_VAL_8197F))
#define BIT_GET_AXI_TXDMA_TIMEOUT_VAL_8197F(x)                                 \
	(((x) >> BIT_SHIFT_AXI_TXDMA_TIMEOUT_VAL_8197F) &                      \
	 BIT_MASK_AXI_TXDMA_TIMEOUT_VAL_8197F)
#define BIT_SET_AXI_TXDMA_TIMEOUT_VAL_8197F(x, v)                              \
	(BIT_CLEAR_AXI_TXDMA_TIMEOUT_VAL_8197F(x) |                            \
	 BIT_AXI_TXDMA_TIMEOUT_VAL_8197F(v))

/* 2 REG_Q0_INFO_8197F */

#define BIT_SHIFT_QUEUEMACID_Q0_V1_8197F 25
#define BIT_MASK_QUEUEMACID_Q0_V1_8197F 0x7f
#define BIT_QUEUEMACID_Q0_V1_8197F(x)                                          \
	(((x) & BIT_MASK_QUEUEMACID_Q0_V1_8197F)                               \
	 << BIT_SHIFT_QUEUEMACID_Q0_V1_8197F)
#define BITS_QUEUEMACID_Q0_V1_8197F                                            \
	(BIT_MASK_QUEUEMACID_Q0_V1_8197F << BIT_SHIFT_QUEUEMACID_Q0_V1_8197F)
#define BIT_CLEAR_QUEUEMACID_Q0_V1_8197F(x)                                    \
	((x) & (~BITS_QUEUEMACID_Q0_V1_8197F))
#define BIT_GET_QUEUEMACID_Q0_V1_8197F(x)                                      \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q0_V1_8197F) &                           \
	 BIT_MASK_QUEUEMACID_Q0_V1_8197F)
#define BIT_SET_QUEUEMACID_Q0_V1_8197F(x, v)                                   \
	(BIT_CLEAR_QUEUEMACID_Q0_V1_8197F(x) | BIT_QUEUEMACID_Q0_V1_8197F(v))

#define BIT_SHIFT_QUEUEAC_Q0_V1_8197F 23
#define BIT_MASK_QUEUEAC_Q0_V1_8197F 0x3
#define BIT_QUEUEAC_Q0_V1_8197F(x)                                             \
	(((x) & BIT_MASK_QUEUEAC_Q0_V1_8197F) << BIT_SHIFT_QUEUEAC_Q0_V1_8197F)
#define BITS_QUEUEAC_Q0_V1_8197F                                               \
	(BIT_MASK_QUEUEAC_Q0_V1_8197F << BIT_SHIFT_QUEUEAC_Q0_V1_8197F)
#define BIT_CLEAR_QUEUEAC_Q0_V1_8197F(x) ((x) & (~BITS_QUEUEAC_Q0_V1_8197F))
#define BIT_GET_QUEUEAC_Q0_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_QUEUEAC_Q0_V1_8197F) & BIT_MASK_QUEUEAC_Q0_V1_8197F)
#define BIT_SET_QUEUEAC_Q0_V1_8197F(x, v)                                      \
	(BIT_CLEAR_QUEUEAC_Q0_V1_8197F(x) | BIT_QUEUEAC_Q0_V1_8197F(v))

#define BIT_TIDEMPTY_Q0_V1_8197F BIT(22)

#define BIT_SHIFT_TAIL_PKT_Q0_V2_8197F 11
#define BIT_MASK_TAIL_PKT_Q0_V2_8197F 0x7ff
#define BIT_TAIL_PKT_Q0_V2_8197F(x)                                            \
	(((x) & BIT_MASK_TAIL_PKT_Q0_V2_8197F)                                 \
	 << BIT_SHIFT_TAIL_PKT_Q0_V2_8197F)
#define BITS_TAIL_PKT_Q0_V2_8197F                                              \
	(BIT_MASK_TAIL_PKT_Q0_V2_8197F << BIT_SHIFT_TAIL_PKT_Q0_V2_8197F)
#define BIT_CLEAR_TAIL_PKT_Q0_V2_8197F(x) ((x) & (~BITS_TAIL_PKT_Q0_V2_8197F))
#define BIT_GET_TAIL_PKT_Q0_V2_8197F(x)                                        \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q0_V2_8197F) &                             \
	 BIT_MASK_TAIL_PKT_Q0_V2_8197F)
#define BIT_SET_TAIL_PKT_Q0_V2_8197F(x, v)                                     \
	(BIT_CLEAR_TAIL_PKT_Q0_V2_8197F(x) | BIT_TAIL_PKT_Q0_V2_8197F(v))

#define BIT_SHIFT_HEAD_PKT_Q0_V1_8197F 0
#define BIT_MASK_HEAD_PKT_Q0_V1_8197F 0x7ff
#define BIT_HEAD_PKT_Q0_V1_8197F(x)                                            \
	(((x) & BIT_MASK_HEAD_PKT_Q0_V1_8197F)                                 \
	 << BIT_SHIFT_HEAD_PKT_Q0_V1_8197F)
#define BITS_HEAD_PKT_Q0_V1_8197F                                              \
	(BIT_MASK_HEAD_PKT_Q0_V1_8197F << BIT_SHIFT_HEAD_PKT_Q0_V1_8197F)
#define BIT_CLEAR_HEAD_PKT_Q0_V1_8197F(x) ((x) & (~BITS_HEAD_PKT_Q0_V1_8197F))
#define BIT_GET_HEAD_PKT_Q0_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q0_V1_8197F) &                             \
	 BIT_MASK_HEAD_PKT_Q0_V1_8197F)
#define BIT_SET_HEAD_PKT_Q0_V1_8197F(x, v)                                     \
	(BIT_CLEAR_HEAD_PKT_Q0_V1_8197F(x) | BIT_HEAD_PKT_Q0_V1_8197F(v))

/* 2 REG_Q1_INFO_8197F */

#define BIT_SHIFT_QUEUEMACID_Q1_V1_8197F 25
#define BIT_MASK_QUEUEMACID_Q1_V1_8197F 0x7f
#define BIT_QUEUEMACID_Q1_V1_8197F(x)                                          \
	(((x) & BIT_MASK_QUEUEMACID_Q1_V1_8197F)                               \
	 << BIT_SHIFT_QUEUEMACID_Q1_V1_8197F)
#define BITS_QUEUEMACID_Q1_V1_8197F                                            \
	(BIT_MASK_QUEUEMACID_Q1_V1_8197F << BIT_SHIFT_QUEUEMACID_Q1_V1_8197F)
#define BIT_CLEAR_QUEUEMACID_Q1_V1_8197F(x)                                    \
	((x) & (~BITS_QUEUEMACID_Q1_V1_8197F))
#define BIT_GET_QUEUEMACID_Q1_V1_8197F(x)                                      \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q1_V1_8197F) &                           \
	 BIT_MASK_QUEUEMACID_Q1_V1_8197F)
#define BIT_SET_QUEUEMACID_Q1_V1_8197F(x, v)                                   \
	(BIT_CLEAR_QUEUEMACID_Q1_V1_8197F(x) | BIT_QUEUEMACID_Q1_V1_8197F(v))

#define BIT_SHIFT_QUEUEAC_Q1_V1_8197F 23
#define BIT_MASK_QUEUEAC_Q1_V1_8197F 0x3
#define BIT_QUEUEAC_Q1_V1_8197F(x)                                             \
	(((x) & BIT_MASK_QUEUEAC_Q1_V1_8197F) << BIT_SHIFT_QUEUEAC_Q1_V1_8197F)
#define BITS_QUEUEAC_Q1_V1_8197F                                               \
	(BIT_MASK_QUEUEAC_Q1_V1_8197F << BIT_SHIFT_QUEUEAC_Q1_V1_8197F)
#define BIT_CLEAR_QUEUEAC_Q1_V1_8197F(x) ((x) & (~BITS_QUEUEAC_Q1_V1_8197F))
#define BIT_GET_QUEUEAC_Q1_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_QUEUEAC_Q1_V1_8197F) & BIT_MASK_QUEUEAC_Q1_V1_8197F)
#define BIT_SET_QUEUEAC_Q1_V1_8197F(x, v)                                      \
	(BIT_CLEAR_QUEUEAC_Q1_V1_8197F(x) | BIT_QUEUEAC_Q1_V1_8197F(v))

#define BIT_TIDEMPTY_Q1_V1_8197F BIT(22)

#define BIT_SHIFT_TAIL_PKT_Q1_V2_8197F 11
#define BIT_MASK_TAIL_PKT_Q1_V2_8197F 0x7ff
#define BIT_TAIL_PKT_Q1_V2_8197F(x)                                            \
	(((x) & BIT_MASK_TAIL_PKT_Q1_V2_8197F)                                 \
	 << BIT_SHIFT_TAIL_PKT_Q1_V2_8197F)
#define BITS_TAIL_PKT_Q1_V2_8197F                                              \
	(BIT_MASK_TAIL_PKT_Q1_V2_8197F << BIT_SHIFT_TAIL_PKT_Q1_V2_8197F)
#define BIT_CLEAR_TAIL_PKT_Q1_V2_8197F(x) ((x) & (~BITS_TAIL_PKT_Q1_V2_8197F))
#define BIT_GET_TAIL_PKT_Q1_V2_8197F(x)                                        \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q1_V2_8197F) &                             \
	 BIT_MASK_TAIL_PKT_Q1_V2_8197F)
#define BIT_SET_TAIL_PKT_Q1_V2_8197F(x, v)                                     \
	(BIT_CLEAR_TAIL_PKT_Q1_V2_8197F(x) | BIT_TAIL_PKT_Q1_V2_8197F(v))

#define BIT_SHIFT_HEAD_PKT_Q1_V1_8197F 0
#define BIT_MASK_HEAD_PKT_Q1_V1_8197F 0x7ff
#define BIT_HEAD_PKT_Q1_V1_8197F(x)                                            \
	(((x) & BIT_MASK_HEAD_PKT_Q1_V1_8197F)                                 \
	 << BIT_SHIFT_HEAD_PKT_Q1_V1_8197F)
#define BITS_HEAD_PKT_Q1_V1_8197F                                              \
	(BIT_MASK_HEAD_PKT_Q1_V1_8197F << BIT_SHIFT_HEAD_PKT_Q1_V1_8197F)
#define BIT_CLEAR_HEAD_PKT_Q1_V1_8197F(x) ((x) & (~BITS_HEAD_PKT_Q1_V1_8197F))
#define BIT_GET_HEAD_PKT_Q1_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q1_V1_8197F) &                             \
	 BIT_MASK_HEAD_PKT_Q1_V1_8197F)
#define BIT_SET_HEAD_PKT_Q1_V1_8197F(x, v)                                     \
	(BIT_CLEAR_HEAD_PKT_Q1_V1_8197F(x) | BIT_HEAD_PKT_Q1_V1_8197F(v))

/* 2 REG_Q2_INFO_8197F */

#define BIT_SHIFT_QUEUEMACID_Q2_V1_8197F 25
#define BIT_MASK_QUEUEMACID_Q2_V1_8197F 0x7f
#define BIT_QUEUEMACID_Q2_V1_8197F(x)                                          \
	(((x) & BIT_MASK_QUEUEMACID_Q2_V1_8197F)                               \
	 << BIT_SHIFT_QUEUEMACID_Q2_V1_8197F)
#define BITS_QUEUEMACID_Q2_V1_8197F                                            \
	(BIT_MASK_QUEUEMACID_Q2_V1_8197F << BIT_SHIFT_QUEUEMACID_Q2_V1_8197F)
#define BIT_CLEAR_QUEUEMACID_Q2_V1_8197F(x)                                    \
	((x) & (~BITS_QUEUEMACID_Q2_V1_8197F))
#define BIT_GET_QUEUEMACID_Q2_V1_8197F(x)                                      \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q2_V1_8197F) &                           \
	 BIT_MASK_QUEUEMACID_Q2_V1_8197F)
#define BIT_SET_QUEUEMACID_Q2_V1_8197F(x, v)                                   \
	(BIT_CLEAR_QUEUEMACID_Q2_V1_8197F(x) | BIT_QUEUEMACID_Q2_V1_8197F(v))

#define BIT_SHIFT_QUEUEAC_Q2_V1_8197F 23
#define BIT_MASK_QUEUEAC_Q2_V1_8197F 0x3
#define BIT_QUEUEAC_Q2_V1_8197F(x)                                             \
	(((x) & BIT_MASK_QUEUEAC_Q2_V1_8197F) << BIT_SHIFT_QUEUEAC_Q2_V1_8197F)
#define BITS_QUEUEAC_Q2_V1_8197F                                               \
	(BIT_MASK_QUEUEAC_Q2_V1_8197F << BIT_SHIFT_QUEUEAC_Q2_V1_8197F)
#define BIT_CLEAR_QUEUEAC_Q2_V1_8197F(x) ((x) & (~BITS_QUEUEAC_Q2_V1_8197F))
#define BIT_GET_QUEUEAC_Q2_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_QUEUEAC_Q2_V1_8197F) & BIT_MASK_QUEUEAC_Q2_V1_8197F)
#define BIT_SET_QUEUEAC_Q2_V1_8197F(x, v)                                      \
	(BIT_CLEAR_QUEUEAC_Q2_V1_8197F(x) | BIT_QUEUEAC_Q2_V1_8197F(v))

#define BIT_TIDEMPTY_Q2_V1_8197F BIT(22)

#define BIT_SHIFT_TAIL_PKT_Q2_V2_8197F 11
#define BIT_MASK_TAIL_PKT_Q2_V2_8197F 0x7ff
#define BIT_TAIL_PKT_Q2_V2_8197F(x)                                            \
	(((x) & BIT_MASK_TAIL_PKT_Q2_V2_8197F)                                 \
	 << BIT_SHIFT_TAIL_PKT_Q2_V2_8197F)
#define BITS_TAIL_PKT_Q2_V2_8197F                                              \
	(BIT_MASK_TAIL_PKT_Q2_V2_8197F << BIT_SHIFT_TAIL_PKT_Q2_V2_8197F)
#define BIT_CLEAR_TAIL_PKT_Q2_V2_8197F(x) ((x) & (~BITS_TAIL_PKT_Q2_V2_8197F))
#define BIT_GET_TAIL_PKT_Q2_V2_8197F(x)                                        \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q2_V2_8197F) &                             \
	 BIT_MASK_TAIL_PKT_Q2_V2_8197F)
#define BIT_SET_TAIL_PKT_Q2_V2_8197F(x, v)                                     \
	(BIT_CLEAR_TAIL_PKT_Q2_V2_8197F(x) | BIT_TAIL_PKT_Q2_V2_8197F(v))

#define BIT_SHIFT_HEAD_PKT_Q2_V1_8197F 0
#define BIT_MASK_HEAD_PKT_Q2_V1_8197F 0x7ff
#define BIT_HEAD_PKT_Q2_V1_8197F(x)                                            \
	(((x) & BIT_MASK_HEAD_PKT_Q2_V1_8197F)                                 \
	 << BIT_SHIFT_HEAD_PKT_Q2_V1_8197F)
#define BITS_HEAD_PKT_Q2_V1_8197F                                              \
	(BIT_MASK_HEAD_PKT_Q2_V1_8197F << BIT_SHIFT_HEAD_PKT_Q2_V1_8197F)
#define BIT_CLEAR_HEAD_PKT_Q2_V1_8197F(x) ((x) & (~BITS_HEAD_PKT_Q2_V1_8197F))
#define BIT_GET_HEAD_PKT_Q2_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q2_V1_8197F) &                             \
	 BIT_MASK_HEAD_PKT_Q2_V1_8197F)
#define BIT_SET_HEAD_PKT_Q2_V1_8197F(x, v)                                     \
	(BIT_CLEAR_HEAD_PKT_Q2_V1_8197F(x) | BIT_HEAD_PKT_Q2_V1_8197F(v))

/* 2 REG_Q3_INFO_8197F */

#define BIT_SHIFT_QUEUEMACID_Q3_V1_8197F 25
#define BIT_MASK_QUEUEMACID_Q3_V1_8197F 0x7f
#define BIT_QUEUEMACID_Q3_V1_8197F(x)                                          \
	(((x) & BIT_MASK_QUEUEMACID_Q3_V1_8197F)                               \
	 << BIT_SHIFT_QUEUEMACID_Q3_V1_8197F)
#define BITS_QUEUEMACID_Q3_V1_8197F                                            \
	(BIT_MASK_QUEUEMACID_Q3_V1_8197F << BIT_SHIFT_QUEUEMACID_Q3_V1_8197F)
#define BIT_CLEAR_QUEUEMACID_Q3_V1_8197F(x)                                    \
	((x) & (~BITS_QUEUEMACID_Q3_V1_8197F))
#define BIT_GET_QUEUEMACID_Q3_V1_8197F(x)                                      \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q3_V1_8197F) &                           \
	 BIT_MASK_QUEUEMACID_Q3_V1_8197F)
#define BIT_SET_QUEUEMACID_Q3_V1_8197F(x, v)                                   \
	(BIT_CLEAR_QUEUEMACID_Q3_V1_8197F(x) | BIT_QUEUEMACID_Q3_V1_8197F(v))

#define BIT_SHIFT_QUEUEAC_Q3_V1_8197F 23
#define BIT_MASK_QUEUEAC_Q3_V1_8197F 0x3
#define BIT_QUEUEAC_Q3_V1_8197F(x)                                             \
	(((x) & BIT_MASK_QUEUEAC_Q3_V1_8197F) << BIT_SHIFT_QUEUEAC_Q3_V1_8197F)
#define BITS_QUEUEAC_Q3_V1_8197F                                               \
	(BIT_MASK_QUEUEAC_Q3_V1_8197F << BIT_SHIFT_QUEUEAC_Q3_V1_8197F)
#define BIT_CLEAR_QUEUEAC_Q3_V1_8197F(x) ((x) & (~BITS_QUEUEAC_Q3_V1_8197F))
#define BIT_GET_QUEUEAC_Q3_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_QUEUEAC_Q3_V1_8197F) & BIT_MASK_QUEUEAC_Q3_V1_8197F)
#define BIT_SET_QUEUEAC_Q3_V1_8197F(x, v)                                      \
	(BIT_CLEAR_QUEUEAC_Q3_V1_8197F(x) | BIT_QUEUEAC_Q3_V1_8197F(v))

#define BIT_TIDEMPTY_Q3_V1_8197F BIT(22)

#define BIT_SHIFT_TAIL_PKT_Q3_V2_8197F 11
#define BIT_MASK_TAIL_PKT_Q3_V2_8197F 0x7ff
#define BIT_TAIL_PKT_Q3_V2_8197F(x)                                            \
	(((x) & BIT_MASK_TAIL_PKT_Q3_V2_8197F)                                 \
	 << BIT_SHIFT_TAIL_PKT_Q3_V2_8197F)
#define BITS_TAIL_PKT_Q3_V2_8197F                                              \
	(BIT_MASK_TAIL_PKT_Q3_V2_8197F << BIT_SHIFT_TAIL_PKT_Q3_V2_8197F)
#define BIT_CLEAR_TAIL_PKT_Q3_V2_8197F(x) ((x) & (~BITS_TAIL_PKT_Q3_V2_8197F))
#define BIT_GET_TAIL_PKT_Q3_V2_8197F(x)                                        \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q3_V2_8197F) &                             \
	 BIT_MASK_TAIL_PKT_Q3_V2_8197F)
#define BIT_SET_TAIL_PKT_Q3_V2_8197F(x, v)                                     \
	(BIT_CLEAR_TAIL_PKT_Q3_V2_8197F(x) | BIT_TAIL_PKT_Q3_V2_8197F(v))

#define BIT_SHIFT_HEAD_PKT_Q3_V1_8197F 0
#define BIT_MASK_HEAD_PKT_Q3_V1_8197F 0x7ff
#define BIT_HEAD_PKT_Q3_V1_8197F(x)                                            \
	(((x) & BIT_MASK_HEAD_PKT_Q3_V1_8197F)                                 \
	 << BIT_SHIFT_HEAD_PKT_Q3_V1_8197F)
#define BITS_HEAD_PKT_Q3_V1_8197F                                              \
	(BIT_MASK_HEAD_PKT_Q3_V1_8197F << BIT_SHIFT_HEAD_PKT_Q3_V1_8197F)
#define BIT_CLEAR_HEAD_PKT_Q3_V1_8197F(x) ((x) & (~BITS_HEAD_PKT_Q3_V1_8197F))
#define BIT_GET_HEAD_PKT_Q3_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q3_V1_8197F) &                             \
	 BIT_MASK_HEAD_PKT_Q3_V1_8197F)
#define BIT_SET_HEAD_PKT_Q3_V1_8197F(x, v)                                     \
	(BIT_CLEAR_HEAD_PKT_Q3_V1_8197F(x) | BIT_HEAD_PKT_Q3_V1_8197F(v))

/* 2 REG_MGQ_INFO_8197F */

#define BIT_SHIFT_QUEUEMACID_MGQ_V1_8197F 25
#define BIT_MASK_QUEUEMACID_MGQ_V1_8197F 0x7f
#define BIT_QUEUEMACID_MGQ_V1_8197F(x)                                         \
	(((x) & BIT_MASK_QUEUEMACID_MGQ_V1_8197F)                              \
	 << BIT_SHIFT_QUEUEMACID_MGQ_V1_8197F)
#define BITS_QUEUEMACID_MGQ_V1_8197F                                           \
	(BIT_MASK_QUEUEMACID_MGQ_V1_8197F << BIT_SHIFT_QUEUEMACID_MGQ_V1_8197F)
#define BIT_CLEAR_QUEUEMACID_MGQ_V1_8197F(x)                                   \
	((x) & (~BITS_QUEUEMACID_MGQ_V1_8197F))
#define BIT_GET_QUEUEMACID_MGQ_V1_8197F(x)                                     \
	(((x) >> BIT_SHIFT_QUEUEMACID_MGQ_V1_8197F) &                          \
	 BIT_MASK_QUEUEMACID_MGQ_V1_8197F)
#define BIT_SET_QUEUEMACID_MGQ_V1_8197F(x, v)                                  \
	(BIT_CLEAR_QUEUEMACID_MGQ_V1_8197F(x) | BIT_QUEUEMACID_MGQ_V1_8197F(v))

#define BIT_SHIFT_QUEUEAC_MGQ_V1_8197F 23
#define BIT_MASK_QUEUEAC_MGQ_V1_8197F 0x3
#define BIT_QUEUEAC_MGQ_V1_8197F(x)                                            \
	(((x) & BIT_MASK_QUEUEAC_MGQ_V1_8197F)                                 \
	 << BIT_SHIFT_QUEUEAC_MGQ_V1_8197F)
#define BITS_QUEUEAC_MGQ_V1_8197F                                              \
	(BIT_MASK_QUEUEAC_MGQ_V1_8197F << BIT_SHIFT_QUEUEAC_MGQ_V1_8197F)
#define BIT_CLEAR_QUEUEAC_MGQ_V1_8197F(x) ((x) & (~BITS_QUEUEAC_MGQ_V1_8197F))
#define BIT_GET_QUEUEAC_MGQ_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_QUEUEAC_MGQ_V1_8197F) &                             \
	 BIT_MASK_QUEUEAC_MGQ_V1_8197F)
#define BIT_SET_QUEUEAC_MGQ_V1_8197F(x, v)                                     \
	(BIT_CLEAR_QUEUEAC_MGQ_V1_8197F(x) | BIT_QUEUEAC_MGQ_V1_8197F(v))

#define BIT_TIDEMPTY_MGQ_V1_8197F BIT(22)

#define BIT_SHIFT_TAIL_PKT_MGQ_V2_8197F 11
#define BIT_MASK_TAIL_PKT_MGQ_V2_8197F 0x7ff
#define BIT_TAIL_PKT_MGQ_V2_8197F(x)                                           \
	(((x) & BIT_MASK_TAIL_PKT_MGQ_V2_8197F)                                \
	 << BIT_SHIFT_TAIL_PKT_MGQ_V2_8197F)
#define BITS_TAIL_PKT_MGQ_V2_8197F                                             \
	(BIT_MASK_TAIL_PKT_MGQ_V2_8197F << BIT_SHIFT_TAIL_PKT_MGQ_V2_8197F)
#define BIT_CLEAR_TAIL_PKT_MGQ_V2_8197F(x) ((x) & (~BITS_TAIL_PKT_MGQ_V2_8197F))
#define BIT_GET_TAIL_PKT_MGQ_V2_8197F(x)                                       \
	(((x) >> BIT_SHIFT_TAIL_PKT_MGQ_V2_8197F) &                            \
	 BIT_MASK_TAIL_PKT_MGQ_V2_8197F)
#define BIT_SET_TAIL_PKT_MGQ_V2_8197F(x, v)                                    \
	(BIT_CLEAR_TAIL_PKT_MGQ_V2_8197F(x) | BIT_TAIL_PKT_MGQ_V2_8197F(v))

#define BIT_SHIFT_HEAD_PKT_MGQ_V1_8197F 0
#define BIT_MASK_HEAD_PKT_MGQ_V1_8197F 0x7ff
#define BIT_HEAD_PKT_MGQ_V1_8197F(x)                                           \
	(((x) & BIT_MASK_HEAD_PKT_MGQ_V1_8197F)                                \
	 << BIT_SHIFT_HEAD_PKT_MGQ_V1_8197F)
#define BITS_HEAD_PKT_MGQ_V1_8197F                                             \
	(BIT_MASK_HEAD_PKT_MGQ_V1_8197F << BIT_SHIFT_HEAD_PKT_MGQ_V1_8197F)
#define BIT_CLEAR_HEAD_PKT_MGQ_V1_8197F(x) ((x) & (~BITS_HEAD_PKT_MGQ_V1_8197F))
#define BIT_GET_HEAD_PKT_MGQ_V1_8197F(x)                                       \
	(((x) >> BIT_SHIFT_HEAD_PKT_MGQ_V1_8197F) &                            \
	 BIT_MASK_HEAD_PKT_MGQ_V1_8197F)
#define BIT_SET_HEAD_PKT_MGQ_V1_8197F(x, v)                                    \
	(BIT_CLEAR_HEAD_PKT_MGQ_V1_8197F(x) | BIT_HEAD_PKT_MGQ_V1_8197F(v))

/* 2 REG_HIQ_INFO_8197F */

#define BIT_SHIFT_QUEUEMACID_HIQ_V1_8197F 25
#define BIT_MASK_QUEUEMACID_HIQ_V1_8197F 0x7f
#define BIT_QUEUEMACID_HIQ_V1_8197F(x)                                         \
	(((x) & BIT_MASK_QUEUEMACID_HIQ_V1_8197F)                              \
	 << BIT_SHIFT_QUEUEMACID_HIQ_V1_8197F)
#define BITS_QUEUEMACID_HIQ_V1_8197F                                           \
	(BIT_MASK_QUEUEMACID_HIQ_V1_8197F << BIT_SHIFT_QUEUEMACID_HIQ_V1_8197F)
#define BIT_CLEAR_QUEUEMACID_HIQ_V1_8197F(x)                                   \
	((x) & (~BITS_QUEUEMACID_HIQ_V1_8197F))
#define BIT_GET_QUEUEMACID_HIQ_V1_8197F(x)                                     \
	(((x) >> BIT_SHIFT_QUEUEMACID_HIQ_V1_8197F) &                          \
	 BIT_MASK_QUEUEMACID_HIQ_V1_8197F)
#define BIT_SET_QUEUEMACID_HIQ_V1_8197F(x, v)                                  \
	(BIT_CLEAR_QUEUEMACID_HIQ_V1_8197F(x) | BIT_QUEUEMACID_HIQ_V1_8197F(v))

#define BIT_SHIFT_QUEUEAC_HIQ_V1_8197F 23
#define BIT_MASK_QUEUEAC_HIQ_V1_8197F 0x3
#define BIT_QUEUEAC_HIQ_V1_8197F(x)                                            \
	(((x) & BIT_MASK_QUEUEAC_HIQ_V1_8197F)                                 \
	 << BIT_SHIFT_QUEUEAC_HIQ_V1_8197F)
#define BITS_QUEUEAC_HIQ_V1_8197F                                              \
	(BIT_MASK_QUEUEAC_HIQ_V1_8197F << BIT_SHIFT_QUEUEAC_HIQ_V1_8197F)
#define BIT_CLEAR_QUEUEAC_HIQ_V1_8197F(x) ((x) & (~BITS_QUEUEAC_HIQ_V1_8197F))
#define BIT_GET_QUEUEAC_HIQ_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_QUEUEAC_HIQ_V1_8197F) &                             \
	 BIT_MASK_QUEUEAC_HIQ_V1_8197F)
#define BIT_SET_QUEUEAC_HIQ_V1_8197F(x, v)                                     \
	(BIT_CLEAR_QUEUEAC_HIQ_V1_8197F(x) | BIT_QUEUEAC_HIQ_V1_8197F(v))

#define BIT_TIDEMPTY_HIQ_V1_8197F BIT(22)

#define BIT_SHIFT_TAIL_PKT_HIQ_V2_8197F 11
#define BIT_MASK_TAIL_PKT_HIQ_V2_8197F 0x7ff
#define BIT_TAIL_PKT_HIQ_V2_8197F(x)                                           \
	(((x) & BIT_MASK_TAIL_PKT_HIQ_V2_8197F)                                \
	 << BIT_SHIFT_TAIL_PKT_HIQ_V2_8197F)
#define BITS_TAIL_PKT_HIQ_V2_8197F                                             \
	(BIT_MASK_TAIL_PKT_HIQ_V2_8197F << BIT_SHIFT_TAIL_PKT_HIQ_V2_8197F)
#define BIT_CLEAR_TAIL_PKT_HIQ_V2_8197F(x) ((x) & (~BITS_TAIL_PKT_HIQ_V2_8197F))
#define BIT_GET_TAIL_PKT_HIQ_V2_8197F(x)                                       \
	(((x) >> BIT_SHIFT_TAIL_PKT_HIQ_V2_8197F) &                            \
	 BIT_MASK_TAIL_PKT_HIQ_V2_8197F)
#define BIT_SET_TAIL_PKT_HIQ_V2_8197F(x, v)                                    \
	(BIT_CLEAR_TAIL_PKT_HIQ_V2_8197F(x) | BIT_TAIL_PKT_HIQ_V2_8197F(v))

#define BIT_SHIFT_HEAD_PKT_HIQ_V1_8197F 0
#define BIT_MASK_HEAD_PKT_HIQ_V1_8197F 0x7ff
#define BIT_HEAD_PKT_HIQ_V1_8197F(x)                                           \
	(((x) & BIT_MASK_HEAD_PKT_HIQ_V1_8197F)                                \
	 << BIT_SHIFT_HEAD_PKT_HIQ_V1_8197F)
#define BITS_HEAD_PKT_HIQ_V1_8197F                                             \
	(BIT_MASK_HEAD_PKT_HIQ_V1_8197F << BIT_SHIFT_HEAD_PKT_HIQ_V1_8197F)
#define BIT_CLEAR_HEAD_PKT_HIQ_V1_8197F(x) ((x) & (~BITS_HEAD_PKT_HIQ_V1_8197F))
#define BIT_GET_HEAD_PKT_HIQ_V1_8197F(x)                                       \
	(((x) >> BIT_SHIFT_HEAD_PKT_HIQ_V1_8197F) &                            \
	 BIT_MASK_HEAD_PKT_HIQ_V1_8197F)
#define BIT_SET_HEAD_PKT_HIQ_V1_8197F(x, v)                                    \
	(BIT_CLEAR_HEAD_PKT_HIQ_V1_8197F(x) | BIT_HEAD_PKT_HIQ_V1_8197F(v))

/* 2 REG_BCNQ_INFO_8197F */

#define BIT_SHIFT_BCNQ_HEAD_PG_V1_8197F 0
#define BIT_MASK_BCNQ_HEAD_PG_V1_8197F 0xfff
#define BIT_BCNQ_HEAD_PG_V1_8197F(x)                                           \
	(((x) & BIT_MASK_BCNQ_HEAD_PG_V1_8197F)                                \
	 << BIT_SHIFT_BCNQ_HEAD_PG_V1_8197F)
#define BITS_BCNQ_HEAD_PG_V1_8197F                                             \
	(BIT_MASK_BCNQ_HEAD_PG_V1_8197F << BIT_SHIFT_BCNQ_HEAD_PG_V1_8197F)
#define BIT_CLEAR_BCNQ_HEAD_PG_V1_8197F(x) ((x) & (~BITS_BCNQ_HEAD_PG_V1_8197F))
#define BIT_GET_BCNQ_HEAD_PG_V1_8197F(x)                                       \
	(((x) >> BIT_SHIFT_BCNQ_HEAD_PG_V1_8197F) &                            \
	 BIT_MASK_BCNQ_HEAD_PG_V1_8197F)
#define BIT_SET_BCNQ_HEAD_PG_V1_8197F(x, v)                                    \
	(BIT_CLEAR_BCNQ_HEAD_PG_V1_8197F(x) | BIT_BCNQ_HEAD_PG_V1_8197F(v))

/* 2 REG_TXPKT_EMPTY_8197F */
#define BIT_BCNQ_EMPTY_8197F BIT(11)
#define BIT_HQQ_EMPTY_8197F BIT(10)
#define BIT_MQQ_EMPTY_8197F BIT(9)
#define BIT_MGQ_CPU_EMPTY_8197F BIT(8)
#define BIT_AC7Q_EMPTY_8197F BIT(7)
#define BIT_AC6Q_EMPTY_8197F BIT(6)
#define BIT_AC5Q_EMPTY_8197F BIT(5)
#define BIT_AC4Q_EMPTY_8197F BIT(4)
#define BIT_AC3Q_EMPTY_8197F BIT(3)
#define BIT_AC2Q_EMPTY_8197F BIT(2)
#define BIT_AC1Q_EMPTY_8197F BIT(1)
#define BIT_AC0Q_EMPTY_8197F BIT(0)

/* 2 REG_CPU_MGQ_INFO_8197F */
#define BIT_BCN1_POLL_8197F BIT(30)
#define BIT_CPUMGT_POLL_8197F BIT(29)
#define BIT_BCN_POLL_8197F BIT(28)
#define BIT_CPUMGQ_FW_NUM_V1_8197F BIT(12)

#define BIT_SHIFT_FW_FREE_TAIL_V1_8197F 0
#define BIT_MASK_FW_FREE_TAIL_V1_8197F 0xfff
#define BIT_FW_FREE_TAIL_V1_8197F(x)                                           \
	(((x) & BIT_MASK_FW_FREE_TAIL_V1_8197F)                                \
	 << BIT_SHIFT_FW_FREE_TAIL_V1_8197F)
#define BITS_FW_FREE_TAIL_V1_8197F                                             \
	(BIT_MASK_FW_FREE_TAIL_V1_8197F << BIT_SHIFT_FW_FREE_TAIL_V1_8197F)
#define BIT_CLEAR_FW_FREE_TAIL_V1_8197F(x) ((x) & (~BITS_FW_FREE_TAIL_V1_8197F))
#define BIT_GET_FW_FREE_TAIL_V1_8197F(x)                                       \
	(((x) >> BIT_SHIFT_FW_FREE_TAIL_V1_8197F) &                            \
	 BIT_MASK_FW_FREE_TAIL_V1_8197F)
#define BIT_SET_FW_FREE_TAIL_V1_8197F(x, v)                                    \
	(BIT_CLEAR_FW_FREE_TAIL_V1_8197F(x) | BIT_FW_FREE_TAIL_V1_8197F(v))

/* 2 REG_FWHW_TXQ_CTRL_8197F */
#define BIT_RTS_LIMIT_IN_OFDM_8197F BIT(23)
#define BIT_EN_BCNQ_DL_8197F BIT(22)
#define BIT_EN_RD_RESP_NAV_BK_8197F BIT(21)
#define BIT_EN_WR_FREE_TAIL_8197F BIT(20)

#define BIT_SHIFT_EN_QUEUE_RPT_8197F 8
#define BIT_MASK_EN_QUEUE_RPT_8197F 0xff
#define BIT_EN_QUEUE_RPT_8197F(x)                                              \
	(((x) & BIT_MASK_EN_QUEUE_RPT_8197F) << BIT_SHIFT_EN_QUEUE_RPT_8197F)
#define BITS_EN_QUEUE_RPT_8197F                                                \
	(BIT_MASK_EN_QUEUE_RPT_8197F << BIT_SHIFT_EN_QUEUE_RPT_8197F)
#define BIT_CLEAR_EN_QUEUE_RPT_8197F(x) ((x) & (~BITS_EN_QUEUE_RPT_8197F))
#define BIT_GET_EN_QUEUE_RPT_8197F(x)                                          \
	(((x) >> BIT_SHIFT_EN_QUEUE_RPT_8197F) & BIT_MASK_EN_QUEUE_RPT_8197F)
#define BIT_SET_EN_QUEUE_RPT_8197F(x, v)                                       \
	(BIT_CLEAR_EN_QUEUE_RPT_8197F(x) | BIT_EN_QUEUE_RPT_8197F(v))

#define BIT_EN_RTY_BK_8197F BIT(7)
#define BIT_EN_USE_INI_RAT_8197F BIT(6)
#define BIT_EN_RTS_NAV_BK_8197F BIT(5)
#define BIT_DIS_SSN_CHECK_8197F BIT(4)
#define BIT_MACID_MATCH_RTS_8197F BIT(3)
#define BIT_EN_BCN_TRXRPT_V1_8197F BIT(2)
#define BIT_R_EN_FTMRPT_8197F BIT(1)
#define BIT_R_BMC_NAV_PROTECT_8197F BIT(0)

/* 2 REG_NOT_VALID_8197F */
#define BIT__R_EN_RTY_BK_COD_8197F BIT(2)

#define BIT_SHIFT__R_DATA_FALLBACK_SEL_8197F 0
#define BIT_MASK__R_DATA_FALLBACK_SEL_8197F 0x3
#define BIT__R_DATA_FALLBACK_SEL_8197F(x)                                      \
	(((x) & BIT_MASK__R_DATA_FALLBACK_SEL_8197F)                           \
	 << BIT_SHIFT__R_DATA_FALLBACK_SEL_8197F)
#define BITS__R_DATA_FALLBACK_SEL_8197F                                        \
	(BIT_MASK__R_DATA_FALLBACK_SEL_8197F                                   \
	 << BIT_SHIFT__R_DATA_FALLBACK_SEL_8197F)
#define BIT_CLEAR__R_DATA_FALLBACK_SEL_8197F(x)                                \
	((x) & (~BITS__R_DATA_FALLBACK_SEL_8197F))
#define BIT_GET__R_DATA_FALLBACK_SEL_8197F(x)                                  \
	(((x) >> BIT_SHIFT__R_DATA_FALLBACK_SEL_8197F) &                       \
	 BIT_MASK__R_DATA_FALLBACK_SEL_8197F)
#define BIT_SET__R_DATA_FALLBACK_SEL_8197F(x, v)                               \
	(BIT_CLEAR__R_DATA_FALLBACK_SEL_8197F(x) |                             \
	 BIT__R_DATA_FALLBACK_SEL_8197F(v))

/* 2 REG_BCNQ_BDNY_V1_8197F */

#define BIT_SHIFT_BCNQ_PGBNDY_V1_8197F 0
#define BIT_MASK_BCNQ_PGBNDY_V1_8197F 0xfff
#define BIT_BCNQ_PGBNDY_V1_8197F(x)                                            \
	(((x) & BIT_MASK_BCNQ_PGBNDY_V1_8197F)                                 \
	 << BIT_SHIFT_BCNQ_PGBNDY_V1_8197F)
#define BITS_BCNQ_PGBNDY_V1_8197F                                              \
	(BIT_MASK_BCNQ_PGBNDY_V1_8197F << BIT_SHIFT_BCNQ_PGBNDY_V1_8197F)
#define BIT_CLEAR_BCNQ_PGBNDY_V1_8197F(x) ((x) & (~BITS_BCNQ_PGBNDY_V1_8197F))
#define BIT_GET_BCNQ_PGBNDY_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_BCNQ_PGBNDY_V1_8197F) &                             \
	 BIT_MASK_BCNQ_PGBNDY_V1_8197F)
#define BIT_SET_BCNQ_PGBNDY_V1_8197F(x, v)                                     \
	(BIT_CLEAR_BCNQ_PGBNDY_V1_8197F(x) | BIT_BCNQ_PGBNDY_V1_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_LIFETIME_EN_8197F */
#define BIT_BT_INT_CPU_8197F BIT(7)
#define BIT_BT_INT_PTA_8197F BIT(6)
#define BIT_EN_CTRL_RTYBIT_8197F BIT(4)
#define BIT_LIFETIME_BK_EN_8197F BIT(3)
#define BIT_LIFETIME_BE_EN_8197F BIT(2)
#define BIT_LIFETIME_VI_EN_8197F BIT(1)
#define BIT_LIFETIME_VO_EN_8197F BIT(0)

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_SPEC_SIFS_8197F */

#define BIT_SHIFT_SPEC_SIFS_OFDM_PTCL_8197F 8
#define BIT_MASK_SPEC_SIFS_OFDM_PTCL_8197F 0xff
#define BIT_SPEC_SIFS_OFDM_PTCL_8197F(x)                                       \
	(((x) & BIT_MASK_SPEC_SIFS_OFDM_PTCL_8197F)                            \
	 << BIT_SHIFT_SPEC_SIFS_OFDM_PTCL_8197F)
#define BITS_SPEC_SIFS_OFDM_PTCL_8197F                                         \
	(BIT_MASK_SPEC_SIFS_OFDM_PTCL_8197F                                    \
	 << BIT_SHIFT_SPEC_SIFS_OFDM_PTCL_8197F)
#define BIT_CLEAR_SPEC_SIFS_OFDM_PTCL_8197F(x)                                 \
	((x) & (~BITS_SPEC_SIFS_OFDM_PTCL_8197F))
#define BIT_GET_SPEC_SIFS_OFDM_PTCL_8197F(x)                                   \
	(((x) >> BIT_SHIFT_SPEC_SIFS_OFDM_PTCL_8197F) &                        \
	 BIT_MASK_SPEC_SIFS_OFDM_PTCL_8197F)
#define BIT_SET_SPEC_SIFS_OFDM_PTCL_8197F(x, v)                                \
	(BIT_CLEAR_SPEC_SIFS_OFDM_PTCL_8197F(x) |                              \
	 BIT_SPEC_SIFS_OFDM_PTCL_8197F(v))

#define BIT_SHIFT_SPEC_SIFS_CCK_PTCL_8197F 0
#define BIT_MASK_SPEC_SIFS_CCK_PTCL_8197F 0xff
#define BIT_SPEC_SIFS_CCK_PTCL_8197F(x)                                        \
	(((x) & BIT_MASK_SPEC_SIFS_CCK_PTCL_8197F)                             \
	 << BIT_SHIFT_SPEC_SIFS_CCK_PTCL_8197F)
#define BITS_SPEC_SIFS_CCK_PTCL_8197F                                          \
	(BIT_MASK_SPEC_SIFS_CCK_PTCL_8197F                                     \
	 << BIT_SHIFT_SPEC_SIFS_CCK_PTCL_8197F)
#define BIT_CLEAR_SPEC_SIFS_CCK_PTCL_8197F(x)                                  \
	((x) & (~BITS_SPEC_SIFS_CCK_PTCL_8197F))
#define BIT_GET_SPEC_SIFS_CCK_PTCL_8197F(x)                                    \
	(((x) >> BIT_SHIFT_SPEC_SIFS_CCK_PTCL_8197F) &                         \
	 BIT_MASK_SPEC_SIFS_CCK_PTCL_8197F)
#define BIT_SET_SPEC_SIFS_CCK_PTCL_8197F(x, v)                                 \
	(BIT_CLEAR_SPEC_SIFS_CCK_PTCL_8197F(x) |                               \
	 BIT_SPEC_SIFS_CCK_PTCL_8197F(v))

/* 2 REG_RETRY_LIMIT_8197F */

#define BIT_SHIFT_SRL_8197F 8
#define BIT_MASK_SRL_8197F 0x3f
#define BIT_SRL_8197F(x) (((x) & BIT_MASK_SRL_8197F) << BIT_SHIFT_SRL_8197F)
#define BITS_SRL_8197F (BIT_MASK_SRL_8197F << BIT_SHIFT_SRL_8197F)
#define BIT_CLEAR_SRL_8197F(x) ((x) & (~BITS_SRL_8197F))
#define BIT_GET_SRL_8197F(x) (((x) >> BIT_SHIFT_SRL_8197F) & BIT_MASK_SRL_8197F)
#define BIT_SET_SRL_8197F(x, v) (BIT_CLEAR_SRL_8197F(x) | BIT_SRL_8197F(v))

#define BIT_SHIFT_LRL_8197F 0
#define BIT_MASK_LRL_8197F 0x3f
#define BIT_LRL_8197F(x) (((x) & BIT_MASK_LRL_8197F) << BIT_SHIFT_LRL_8197F)
#define BITS_LRL_8197F (BIT_MASK_LRL_8197F << BIT_SHIFT_LRL_8197F)
#define BIT_CLEAR_LRL_8197F(x) ((x) & (~BITS_LRL_8197F))
#define BIT_GET_LRL_8197F(x) (((x) >> BIT_SHIFT_LRL_8197F) & BIT_MASK_LRL_8197F)
#define BIT_SET_LRL_8197F(x, v) (BIT_CLEAR_LRL_8197F(x) | BIT_LRL_8197F(v))

/* 2 REG_TXBF_CTRL_8197F */
#define BIT_R_ENABLE_NDPA_8197F BIT(31)
#define BIT_USE_NDPA_PARAMETER_8197F BIT(30)
#define BIT_R_PROP_TXBF_8197F BIT(29)
#define BIT_R_EN_NDPA_INT_8197F BIT(28)
#define BIT_R_TXBF1_80M_8197F BIT(27)
#define BIT_R_TXBF1_40M_8197F BIT(26)
#define BIT_R_TXBF1_20M_8197F BIT(25)

#define BIT_SHIFT_R_TXBF1_AID_8197F 16
#define BIT_MASK_R_TXBF1_AID_8197F 0x1ff
#define BIT_R_TXBF1_AID_8197F(x)                                               \
	(((x) & BIT_MASK_R_TXBF1_AID_8197F) << BIT_SHIFT_R_TXBF1_AID_8197F)
#define BITS_R_TXBF1_AID_8197F                                                 \
	(BIT_MASK_R_TXBF1_AID_8197F << BIT_SHIFT_R_TXBF1_AID_8197F)
#define BIT_CLEAR_R_TXBF1_AID_8197F(x) ((x) & (~BITS_R_TXBF1_AID_8197F))
#define BIT_GET_R_TXBF1_AID_8197F(x)                                           \
	(((x) >> BIT_SHIFT_R_TXBF1_AID_8197F) & BIT_MASK_R_TXBF1_AID_8197F)
#define BIT_SET_R_TXBF1_AID_8197F(x, v)                                        \
	(BIT_CLEAR_R_TXBF1_AID_8197F(x) | BIT_R_TXBF1_AID_8197F(v))

#define BIT_DIS_NDP_BFEN_8197F BIT(15)
#define BIT_R_TXBCN_NOBLOCK_NDP_8197F BIT(14)
#define BIT_R_TXBF0_80M_8197F BIT(11)
#define BIT_R_TXBF0_40M_8197F BIT(10)
#define BIT_R_TXBF0_20M_8197F BIT(9)

#define BIT_SHIFT_R_TXBF0_AID_8197F 0
#define BIT_MASK_R_TXBF0_AID_8197F 0x1ff
#define BIT_R_TXBF0_AID_8197F(x)                                               \
	(((x) & BIT_MASK_R_TXBF0_AID_8197F) << BIT_SHIFT_R_TXBF0_AID_8197F)
#define BITS_R_TXBF0_AID_8197F                                                 \
	(BIT_MASK_R_TXBF0_AID_8197F << BIT_SHIFT_R_TXBF0_AID_8197F)
#define BIT_CLEAR_R_TXBF0_AID_8197F(x) ((x) & (~BITS_R_TXBF0_AID_8197F))
#define BIT_GET_R_TXBF0_AID_8197F(x)                                           \
	(((x) >> BIT_SHIFT_R_TXBF0_AID_8197F) & BIT_MASK_R_TXBF0_AID_8197F)
#define BIT_SET_R_TXBF0_AID_8197F(x, v)                                        \
	(BIT_CLEAR_R_TXBF0_AID_8197F(x) | BIT_R_TXBF0_AID_8197F(v))

/* 2 REG_DARFRC_8197F */

#define BIT_SHIFT_DARF_RC8_V2_8197F (56 & CPU_OPT_WIDTH)
#define BIT_MASK_DARF_RC8_V2_8197F 0x3f
#define BIT_DARF_RC8_V2_8197F(x)                                               \
	(((x) & BIT_MASK_DARF_RC8_V2_8197F) << BIT_SHIFT_DARF_RC8_V2_8197F)
#define BITS_DARF_RC8_V2_8197F                                                 \
	(BIT_MASK_DARF_RC8_V2_8197F << BIT_SHIFT_DARF_RC8_V2_8197F)
#define BIT_CLEAR_DARF_RC8_V2_8197F(x) ((x) & (~BITS_DARF_RC8_V2_8197F))
#define BIT_GET_DARF_RC8_V2_8197F(x)                                           \
	(((x) >> BIT_SHIFT_DARF_RC8_V2_8197F) & BIT_MASK_DARF_RC8_V2_8197F)
#define BIT_SET_DARF_RC8_V2_8197F(x, v)                                        \
	(BIT_CLEAR_DARF_RC8_V2_8197F(x) | BIT_DARF_RC8_V2_8197F(v))

#define BIT_SHIFT_DARF_RC7_V2_8197F (48 & CPU_OPT_WIDTH)
#define BIT_MASK_DARF_RC7_V2_8197F 0x3f
#define BIT_DARF_RC7_V2_8197F(x)                                               \
	(((x) & BIT_MASK_DARF_RC7_V2_8197F) << BIT_SHIFT_DARF_RC7_V2_8197F)
#define BITS_DARF_RC7_V2_8197F                                                 \
	(BIT_MASK_DARF_RC7_V2_8197F << BIT_SHIFT_DARF_RC7_V2_8197F)
#define BIT_CLEAR_DARF_RC7_V2_8197F(x) ((x) & (~BITS_DARF_RC7_V2_8197F))
#define BIT_GET_DARF_RC7_V2_8197F(x)                                           \
	(((x) >> BIT_SHIFT_DARF_RC7_V2_8197F) & BIT_MASK_DARF_RC7_V2_8197F)
#define BIT_SET_DARF_RC7_V2_8197F(x, v)                                        \
	(BIT_CLEAR_DARF_RC7_V2_8197F(x) | BIT_DARF_RC7_V2_8197F(v))

#define BIT_SHIFT_DARF_RC6_V2_8197F (40 & CPU_OPT_WIDTH)
#define BIT_MASK_DARF_RC6_V2_8197F 0x3f
#define BIT_DARF_RC6_V2_8197F(x)                                               \
	(((x) & BIT_MASK_DARF_RC6_V2_8197F) << BIT_SHIFT_DARF_RC6_V2_8197F)
#define BITS_DARF_RC6_V2_8197F                                                 \
	(BIT_MASK_DARF_RC6_V2_8197F << BIT_SHIFT_DARF_RC6_V2_8197F)
#define BIT_CLEAR_DARF_RC6_V2_8197F(x) ((x) & (~BITS_DARF_RC6_V2_8197F))
#define BIT_GET_DARF_RC6_V2_8197F(x)                                           \
	(((x) >> BIT_SHIFT_DARF_RC6_V2_8197F) & BIT_MASK_DARF_RC6_V2_8197F)
#define BIT_SET_DARF_RC6_V2_8197F(x, v)                                        \
	(BIT_CLEAR_DARF_RC6_V2_8197F(x) | BIT_DARF_RC6_V2_8197F(v))

#define BIT_SHIFT_DARF_RC5_V2_8197F (32 & CPU_OPT_WIDTH)
#define BIT_MASK_DARF_RC5_V2_8197F 0x3f
#define BIT_DARF_RC5_V2_8197F(x)                                               \
	(((x) & BIT_MASK_DARF_RC5_V2_8197F) << BIT_SHIFT_DARF_RC5_V2_8197F)
#define BITS_DARF_RC5_V2_8197F                                                 \
	(BIT_MASK_DARF_RC5_V2_8197F << BIT_SHIFT_DARF_RC5_V2_8197F)
#define BIT_CLEAR_DARF_RC5_V2_8197F(x) ((x) & (~BITS_DARF_RC5_V2_8197F))
#define BIT_GET_DARF_RC5_V2_8197F(x)                                           \
	(((x) >> BIT_SHIFT_DARF_RC5_V2_8197F) & BIT_MASK_DARF_RC5_V2_8197F)
#define BIT_SET_DARF_RC5_V2_8197F(x, v)                                        \
	(BIT_CLEAR_DARF_RC5_V2_8197F(x) | BIT_DARF_RC5_V2_8197F(v))

#define BIT_SHIFT_DARF_RC4_V1_8197F 24
#define BIT_MASK_DARF_RC4_V1_8197F 0x3f
#define BIT_DARF_RC4_V1_8197F(x)                                               \
	(((x) & BIT_MASK_DARF_RC4_V1_8197F) << BIT_SHIFT_DARF_RC4_V1_8197F)
#define BITS_DARF_RC4_V1_8197F                                                 \
	(BIT_MASK_DARF_RC4_V1_8197F << BIT_SHIFT_DARF_RC4_V1_8197F)
#define BIT_CLEAR_DARF_RC4_V1_8197F(x) ((x) & (~BITS_DARF_RC4_V1_8197F))
#define BIT_GET_DARF_RC4_V1_8197F(x)                                           \
	(((x) >> BIT_SHIFT_DARF_RC4_V1_8197F) & BIT_MASK_DARF_RC4_V1_8197F)
#define BIT_SET_DARF_RC4_V1_8197F(x, v)                                        \
	(BIT_CLEAR_DARF_RC4_V1_8197F(x) | BIT_DARF_RC4_V1_8197F(v))

#define BIT_SHIFT_DARF_RC3_V1_8197F 16
#define BIT_MASK_DARF_RC3_V1_8197F 0x3f
#define BIT_DARF_RC3_V1_8197F(x)                                               \
	(((x) & BIT_MASK_DARF_RC3_V1_8197F) << BIT_SHIFT_DARF_RC3_V1_8197F)
#define BITS_DARF_RC3_V1_8197F                                                 \
	(BIT_MASK_DARF_RC3_V1_8197F << BIT_SHIFT_DARF_RC3_V1_8197F)
#define BIT_CLEAR_DARF_RC3_V1_8197F(x) ((x) & (~BITS_DARF_RC3_V1_8197F))
#define BIT_GET_DARF_RC3_V1_8197F(x)                                           \
	(((x) >> BIT_SHIFT_DARF_RC3_V1_8197F) & BIT_MASK_DARF_RC3_V1_8197F)
#define BIT_SET_DARF_RC3_V1_8197F(x, v)                                        \
	(BIT_CLEAR_DARF_RC3_V1_8197F(x) | BIT_DARF_RC3_V1_8197F(v))

#define BIT_SHIFT_DARF_RC2_V1_8197F 8
#define BIT_MASK_DARF_RC2_V1_8197F 0x3f
#define BIT_DARF_RC2_V1_8197F(x)                                               \
	(((x) & BIT_MASK_DARF_RC2_V1_8197F) << BIT_SHIFT_DARF_RC2_V1_8197F)
#define BITS_DARF_RC2_V1_8197F                                                 \
	(BIT_MASK_DARF_RC2_V1_8197F << BIT_SHIFT_DARF_RC2_V1_8197F)
#define BIT_CLEAR_DARF_RC2_V1_8197F(x) ((x) & (~BITS_DARF_RC2_V1_8197F))
#define BIT_GET_DARF_RC2_V1_8197F(x)                                           \
	(((x) >> BIT_SHIFT_DARF_RC2_V1_8197F) & BIT_MASK_DARF_RC2_V1_8197F)
#define BIT_SET_DARF_RC2_V1_8197F(x, v)                                        \
	(BIT_CLEAR_DARF_RC2_V1_8197F(x) | BIT_DARF_RC2_V1_8197F(v))

#define BIT_SHIFT_DARF_RC1_V1_8197F 0
#define BIT_MASK_DARF_RC1_V1_8197F 0x3f
#define BIT_DARF_RC1_V1_8197F(x)                                               \
	(((x) & BIT_MASK_DARF_RC1_V1_8197F) << BIT_SHIFT_DARF_RC1_V1_8197F)
#define BITS_DARF_RC1_V1_8197F                                                 \
	(BIT_MASK_DARF_RC1_V1_8197F << BIT_SHIFT_DARF_RC1_V1_8197F)
#define BIT_CLEAR_DARF_RC1_V1_8197F(x) ((x) & (~BITS_DARF_RC1_V1_8197F))
#define BIT_GET_DARF_RC1_V1_8197F(x)                                           \
	(((x) >> BIT_SHIFT_DARF_RC1_V1_8197F) & BIT_MASK_DARF_RC1_V1_8197F)
#define BIT_SET_DARF_RC1_V1_8197F(x, v)                                        \
	(BIT_CLEAR_DARF_RC1_V1_8197F(x) | BIT_DARF_RC1_V1_8197F(v))

/* 2 REG_RARFRC_8197F */

#define BIT_SHIFT_RARF_RC8_8197F (56 & CPU_OPT_WIDTH)
#define BIT_MASK_RARF_RC8_8197F 0x1f
#define BIT_RARF_RC8_8197F(x)                                                  \
	(((x) & BIT_MASK_RARF_RC8_8197F) << BIT_SHIFT_RARF_RC8_8197F)
#define BITS_RARF_RC8_8197F                                                    \
	(BIT_MASK_RARF_RC8_8197F << BIT_SHIFT_RARF_RC8_8197F)
#define BIT_CLEAR_RARF_RC8_8197F(x) ((x) & (~BITS_RARF_RC8_8197F))
#define BIT_GET_RARF_RC8_8197F(x)                                              \
	(((x) >> BIT_SHIFT_RARF_RC8_8197F) & BIT_MASK_RARF_RC8_8197F)
#define BIT_SET_RARF_RC8_8197F(x, v)                                           \
	(BIT_CLEAR_RARF_RC8_8197F(x) | BIT_RARF_RC8_8197F(v))

#define BIT_SHIFT_RARF_RC7_8197F (48 & CPU_OPT_WIDTH)
#define BIT_MASK_RARF_RC7_8197F 0x1f
#define BIT_RARF_RC7_8197F(x)                                                  \
	(((x) & BIT_MASK_RARF_RC7_8197F) << BIT_SHIFT_RARF_RC7_8197F)
#define BITS_RARF_RC7_8197F                                                    \
	(BIT_MASK_RARF_RC7_8197F << BIT_SHIFT_RARF_RC7_8197F)
#define BIT_CLEAR_RARF_RC7_8197F(x) ((x) & (~BITS_RARF_RC7_8197F))
#define BIT_GET_RARF_RC7_8197F(x)                                              \
	(((x) >> BIT_SHIFT_RARF_RC7_8197F) & BIT_MASK_RARF_RC7_8197F)
#define BIT_SET_RARF_RC7_8197F(x, v)                                           \
	(BIT_CLEAR_RARF_RC7_8197F(x) | BIT_RARF_RC7_8197F(v))

#define BIT_SHIFT_RARF_RC6_8197F (40 & CPU_OPT_WIDTH)
#define BIT_MASK_RARF_RC6_8197F 0x1f
#define BIT_RARF_RC6_8197F(x)                                                  \
	(((x) & BIT_MASK_RARF_RC6_8197F) << BIT_SHIFT_RARF_RC6_8197F)
#define BITS_RARF_RC6_8197F                                                    \
	(BIT_MASK_RARF_RC6_8197F << BIT_SHIFT_RARF_RC6_8197F)
#define BIT_CLEAR_RARF_RC6_8197F(x) ((x) & (~BITS_RARF_RC6_8197F))
#define BIT_GET_RARF_RC6_8197F(x)                                              \
	(((x) >> BIT_SHIFT_RARF_RC6_8197F) & BIT_MASK_RARF_RC6_8197F)
#define BIT_SET_RARF_RC6_8197F(x, v)                                           \
	(BIT_CLEAR_RARF_RC6_8197F(x) | BIT_RARF_RC6_8197F(v))

#define BIT_SHIFT_RARF_RC5_8197F (32 & CPU_OPT_WIDTH)
#define BIT_MASK_RARF_RC5_8197F 0x1f
#define BIT_RARF_RC5_8197F(x)                                                  \
	(((x) & BIT_MASK_RARF_RC5_8197F) << BIT_SHIFT_RARF_RC5_8197F)
#define BITS_RARF_RC5_8197F                                                    \
	(BIT_MASK_RARF_RC5_8197F << BIT_SHIFT_RARF_RC5_8197F)
#define BIT_CLEAR_RARF_RC5_8197F(x) ((x) & (~BITS_RARF_RC5_8197F))
#define BIT_GET_RARF_RC5_8197F(x)                                              \
	(((x) >> BIT_SHIFT_RARF_RC5_8197F) & BIT_MASK_RARF_RC5_8197F)
#define BIT_SET_RARF_RC5_8197F(x, v)                                           \
	(BIT_CLEAR_RARF_RC5_8197F(x) | BIT_RARF_RC5_8197F(v))

#define BIT_SHIFT_RARF_RC4_8197F 24
#define BIT_MASK_RARF_RC4_8197F 0x1f
#define BIT_RARF_RC4_8197F(x)                                                  \
	(((x) & BIT_MASK_RARF_RC4_8197F) << BIT_SHIFT_RARF_RC4_8197F)
#define BITS_RARF_RC4_8197F                                                    \
	(BIT_MASK_RARF_RC4_8197F << BIT_SHIFT_RARF_RC4_8197F)
#define BIT_CLEAR_RARF_RC4_8197F(x) ((x) & (~BITS_RARF_RC4_8197F))
#define BIT_GET_RARF_RC4_8197F(x)                                              \
	(((x) >> BIT_SHIFT_RARF_RC4_8197F) & BIT_MASK_RARF_RC4_8197F)
#define BIT_SET_RARF_RC4_8197F(x, v)                                           \
	(BIT_CLEAR_RARF_RC4_8197F(x) | BIT_RARF_RC4_8197F(v))

#define BIT_SHIFT_RARF_RC3_8197F 16
#define BIT_MASK_RARF_RC3_8197F 0x1f
#define BIT_RARF_RC3_8197F(x)                                                  \
	(((x) & BIT_MASK_RARF_RC3_8197F) << BIT_SHIFT_RARF_RC3_8197F)
#define BITS_RARF_RC3_8197F                                                    \
	(BIT_MASK_RARF_RC3_8197F << BIT_SHIFT_RARF_RC3_8197F)
#define BIT_CLEAR_RARF_RC3_8197F(x) ((x) & (~BITS_RARF_RC3_8197F))
#define BIT_GET_RARF_RC3_8197F(x)                                              \
	(((x) >> BIT_SHIFT_RARF_RC3_8197F) & BIT_MASK_RARF_RC3_8197F)
#define BIT_SET_RARF_RC3_8197F(x, v)                                           \
	(BIT_CLEAR_RARF_RC3_8197F(x) | BIT_RARF_RC3_8197F(v))

#define BIT_SHIFT_RARF_RC2_8197F 8
#define BIT_MASK_RARF_RC2_8197F 0x1f
#define BIT_RARF_RC2_8197F(x)                                                  \
	(((x) & BIT_MASK_RARF_RC2_8197F) << BIT_SHIFT_RARF_RC2_8197F)
#define BITS_RARF_RC2_8197F                                                    \
	(BIT_MASK_RARF_RC2_8197F << BIT_SHIFT_RARF_RC2_8197F)
#define BIT_CLEAR_RARF_RC2_8197F(x) ((x) & (~BITS_RARF_RC2_8197F))
#define BIT_GET_RARF_RC2_8197F(x)                                              \
	(((x) >> BIT_SHIFT_RARF_RC2_8197F) & BIT_MASK_RARF_RC2_8197F)
#define BIT_SET_RARF_RC2_8197F(x, v)                                           \
	(BIT_CLEAR_RARF_RC2_8197F(x) | BIT_RARF_RC2_8197F(v))

#define BIT_SHIFT_RARF_RC1_8197F 0
#define BIT_MASK_RARF_RC1_8197F 0x1f
#define BIT_RARF_RC1_8197F(x)                                                  \
	(((x) & BIT_MASK_RARF_RC1_8197F) << BIT_SHIFT_RARF_RC1_8197F)
#define BITS_RARF_RC1_8197F                                                    \
	(BIT_MASK_RARF_RC1_8197F << BIT_SHIFT_RARF_RC1_8197F)
#define BIT_CLEAR_RARF_RC1_8197F(x) ((x) & (~BITS_RARF_RC1_8197F))
#define BIT_GET_RARF_RC1_8197F(x)                                              \
	(((x) >> BIT_SHIFT_RARF_RC1_8197F) & BIT_MASK_RARF_RC1_8197F)
#define BIT_SET_RARF_RC1_8197F(x, v)                                           \
	(BIT_CLEAR_RARF_RC1_8197F(x) | BIT_RARF_RC1_8197F(v))

/* 2 REG_RRSR_8197F */
#define BIT_EN_VHTBW_FALL_8197F BIT(31)
#define BIT_EN_HTBW_FALL_8197F BIT(30)

#define BIT_SHIFT_RRSR_RSC_8197F 21
#define BIT_MASK_RRSR_RSC_8197F 0x3
#define BIT_RRSR_RSC_8197F(x)                                                  \
	(((x) & BIT_MASK_RRSR_RSC_8197F) << BIT_SHIFT_RRSR_RSC_8197F)
#define BITS_RRSR_RSC_8197F                                                    \
	(BIT_MASK_RRSR_RSC_8197F << BIT_SHIFT_RRSR_RSC_8197F)
#define BIT_CLEAR_RRSR_RSC_8197F(x) ((x) & (~BITS_RRSR_RSC_8197F))
#define BIT_GET_RRSR_RSC_8197F(x)                                              \
	(((x) >> BIT_SHIFT_RRSR_RSC_8197F) & BIT_MASK_RRSR_RSC_8197F)
#define BIT_SET_RRSR_RSC_8197F(x, v)                                           \
	(BIT_CLEAR_RRSR_RSC_8197F(x) | BIT_RRSR_RSC_8197F(v))

#define BIT_RRSR_BW_8197F BIT(20)

#define BIT_SHIFT_RRSC_BITMAP_8197F 0
#define BIT_MASK_RRSC_BITMAP_8197F 0xfffff
#define BIT_RRSC_BITMAP_8197F(x)                                               \
	(((x) & BIT_MASK_RRSC_BITMAP_8197F) << BIT_SHIFT_RRSC_BITMAP_8197F)
#define BITS_RRSC_BITMAP_8197F                                                 \
	(BIT_MASK_RRSC_BITMAP_8197F << BIT_SHIFT_RRSC_BITMAP_8197F)
#define BIT_CLEAR_RRSC_BITMAP_8197F(x) ((x) & (~BITS_RRSC_BITMAP_8197F))
#define BIT_GET_RRSC_BITMAP_8197F(x)                                           \
	(((x) >> BIT_SHIFT_RRSC_BITMAP_8197F) & BIT_MASK_RRSC_BITMAP_8197F)
#define BIT_SET_RRSC_BITMAP_8197F(x, v)                                        \
	(BIT_CLEAR_RRSC_BITMAP_8197F(x) | BIT_RRSC_BITMAP_8197F(v))

/* 2 REG_ARFR0_8197F */

#define BIT_SHIFT_ARFR0_V1_8197F 0
#define BIT_MASK_ARFR0_V1_8197F 0xffffffffffffffffL
#define BIT_ARFR0_V1_8197F(x)                                                  \
	(((x) & BIT_MASK_ARFR0_V1_8197F) << BIT_SHIFT_ARFR0_V1_8197F)
#define BITS_ARFR0_V1_8197F                                                    \
	(BIT_MASK_ARFR0_V1_8197F << BIT_SHIFT_ARFR0_V1_8197F)
#define BIT_CLEAR_ARFR0_V1_8197F(x) ((x) & (~BITS_ARFR0_V1_8197F))
#define BIT_GET_ARFR0_V1_8197F(x)                                              \
	(((x) >> BIT_SHIFT_ARFR0_V1_8197F) & BIT_MASK_ARFR0_V1_8197F)
#define BIT_SET_ARFR0_V1_8197F(x, v)                                           \
	(BIT_CLEAR_ARFR0_V1_8197F(x) | BIT_ARFR0_V1_8197F(v))

/* 2 REG_ARFR1_V1_8197F */

#define BIT_SHIFT_ARFR1_V1_8197F 0
#define BIT_MASK_ARFR1_V1_8197F 0xffffffffffffffffL
#define BIT_ARFR1_V1_8197F(x)                                                  \
	(((x) & BIT_MASK_ARFR1_V1_8197F) << BIT_SHIFT_ARFR1_V1_8197F)
#define BITS_ARFR1_V1_8197F                                                    \
	(BIT_MASK_ARFR1_V1_8197F << BIT_SHIFT_ARFR1_V1_8197F)
#define BIT_CLEAR_ARFR1_V1_8197F(x) ((x) & (~BITS_ARFR1_V1_8197F))
#define BIT_GET_ARFR1_V1_8197F(x)                                              \
	(((x) >> BIT_SHIFT_ARFR1_V1_8197F) & BIT_MASK_ARFR1_V1_8197F)
#define BIT_SET_ARFR1_V1_8197F(x, v)                                           \
	(BIT_CLEAR_ARFR1_V1_8197F(x) | BIT_ARFR1_V1_8197F(v))

/* 2 REG_CCK_CHECK_8197F */
#define BIT_CHECK_CCK_EN_8197F BIT(7)
#define BIT_EN_BCN_PKT_REL_8197F BIT(6)
#define BIT_BCN_PORT_SEL_8197F BIT(5)
#define BIT_MOREDATA_BYPASS_8197F BIT(4)
#define BIT_EN_CLR_CMD_REL_BCN_PKT_8197F BIT(3)
#define BIT_R_EN_SET_MOREDATA_8197F BIT(2)
#define BIT__R_DIS_CLEAR_MACID_RELEASE_8197F BIT(1)
#define BIT__R_MACID_RELEASE_EN_8197F BIT(0)

/* 2 REG_AMPDU_MAX_TIME_V1_8197F */

#define BIT_SHIFT_AMPDU_MAX_TIME_8197F 0
#define BIT_MASK_AMPDU_MAX_TIME_8197F 0xff
#define BIT_AMPDU_MAX_TIME_8197F(x)                                            \
	(((x) & BIT_MASK_AMPDU_MAX_TIME_8197F)                                 \
	 << BIT_SHIFT_AMPDU_MAX_TIME_8197F)
#define BITS_AMPDU_MAX_TIME_8197F                                              \
	(BIT_MASK_AMPDU_MAX_TIME_8197F << BIT_SHIFT_AMPDU_MAX_TIME_8197F)
#define BIT_CLEAR_AMPDU_MAX_TIME_8197F(x) ((x) & (~BITS_AMPDU_MAX_TIME_8197F))
#define BIT_GET_AMPDU_MAX_TIME_8197F(x)                                        \
	(((x) >> BIT_SHIFT_AMPDU_MAX_TIME_8197F) &                             \
	 BIT_MASK_AMPDU_MAX_TIME_8197F)
#define BIT_SET_AMPDU_MAX_TIME_8197F(x, v)                                     \
	(BIT_CLEAR_AMPDU_MAX_TIME_8197F(x) | BIT_AMPDU_MAX_TIME_8197F(v))

/* 2 REG_BCNQ1_BDNY_V1_8197F */

#define BIT_SHIFT_BCNQ1_PGBNDY_V1_8197F 0
#define BIT_MASK_BCNQ1_PGBNDY_V1_8197F 0xfff
#define BIT_BCNQ1_PGBNDY_V1_8197F(x)                                           \
	(((x) & BIT_MASK_BCNQ1_PGBNDY_V1_8197F)                                \
	 << BIT_SHIFT_BCNQ1_PGBNDY_V1_8197F)
#define BITS_BCNQ1_PGBNDY_V1_8197F                                             \
	(BIT_MASK_BCNQ1_PGBNDY_V1_8197F << BIT_SHIFT_BCNQ1_PGBNDY_V1_8197F)
#define BIT_CLEAR_BCNQ1_PGBNDY_V1_8197F(x) ((x) & (~BITS_BCNQ1_PGBNDY_V1_8197F))
#define BIT_GET_BCNQ1_PGBNDY_V1_8197F(x)                                       \
	(((x) >> BIT_SHIFT_BCNQ1_PGBNDY_V1_8197F) &                            \
	 BIT_MASK_BCNQ1_PGBNDY_V1_8197F)
#define BIT_SET_BCNQ1_PGBNDY_V1_8197F(x, v)                                    \
	(BIT_CLEAR_BCNQ1_PGBNDY_V1_8197F(x) | BIT_BCNQ1_PGBNDY_V1_8197F(v))

/* 2 REG_AMPDU_MAX_LENGTH_8197F */

#define BIT_SHIFT_AMPDU_MAX_LENGTH_8197F 0
#define BIT_MASK_AMPDU_MAX_LENGTH_8197F 0xffffffffL
#define BIT_AMPDU_MAX_LENGTH_8197F(x)                                          \
	(((x) & BIT_MASK_AMPDU_MAX_LENGTH_8197F)                               \
	 << BIT_SHIFT_AMPDU_MAX_LENGTH_8197F)
#define BITS_AMPDU_MAX_LENGTH_8197F                                            \
	(BIT_MASK_AMPDU_MAX_LENGTH_8197F << BIT_SHIFT_AMPDU_MAX_LENGTH_8197F)
#define BIT_CLEAR_AMPDU_MAX_LENGTH_8197F(x)                                    \
	((x) & (~BITS_AMPDU_MAX_LENGTH_8197F))
#define BIT_GET_AMPDU_MAX_LENGTH_8197F(x)                                      \
	(((x) >> BIT_SHIFT_AMPDU_MAX_LENGTH_8197F) &                           \
	 BIT_MASK_AMPDU_MAX_LENGTH_8197F)
#define BIT_SET_AMPDU_MAX_LENGTH_8197F(x, v)                                   \
	(BIT_CLEAR_AMPDU_MAX_LENGTH_8197F(x) | BIT_AMPDU_MAX_LENGTH_8197F(v))

/* 2 REG_ACQ_STOP_8197F */
#define BIT_AC7Q_STOP_8197F BIT(7)
#define BIT_AC6Q_STOP_8197F BIT(6)
#define BIT_AC5Q_STOP_8197F BIT(5)
#define BIT_AC4Q_STOP_8197F BIT(4)
#define BIT_AC3Q_STOP_8197F BIT(3)
#define BIT_AC2Q_STOP_8197F BIT(2)
#define BIT_AC1Q_STOP_8197F BIT(1)
#define BIT_AC0Q_STOP_8197F BIT(0)

/* 2 REG_NDPA_RATE_8197F */

#define BIT_SHIFT_R_NDPA_RATE_V1_8197F 0
#define BIT_MASK_R_NDPA_RATE_V1_8197F 0xff
#define BIT_R_NDPA_RATE_V1_8197F(x)                                            \
	(((x) & BIT_MASK_R_NDPA_RATE_V1_8197F)                                 \
	 << BIT_SHIFT_R_NDPA_RATE_V1_8197F)
#define BITS_R_NDPA_RATE_V1_8197F                                              \
	(BIT_MASK_R_NDPA_RATE_V1_8197F << BIT_SHIFT_R_NDPA_RATE_V1_8197F)
#define BIT_CLEAR_R_NDPA_RATE_V1_8197F(x) ((x) & (~BITS_R_NDPA_RATE_V1_8197F))
#define BIT_GET_R_NDPA_RATE_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_R_NDPA_RATE_V1_8197F) &                             \
	 BIT_MASK_R_NDPA_RATE_V1_8197F)
#define BIT_SET_R_NDPA_RATE_V1_8197F(x, v)                                     \
	(BIT_CLEAR_R_NDPA_RATE_V1_8197F(x) | BIT_R_NDPA_RATE_V1_8197F(v))

/* 2 REG_TX_HANG_CTRL_8197F */
#define BIT_R_EN_GNT_BT_AWAKE_8197F BIT(3)
#define BIT_EN_EOF_V1_8197F BIT(2)
#define BIT_DIS_OQT_BLOCK_8197F BIT(1)
#define BIT_SEARCH_QUEUE_EN_8197F BIT(0)

/* 2 REG_NDPA_OPT_CTRL_8197F */
#define BIT_R_DIS_MACID_RELEASE_RTY_8197F BIT(5)

#define BIT_SHIFT_BW_SIGTA_8197F 3
#define BIT_MASK_BW_SIGTA_8197F 0x3
#define BIT_BW_SIGTA_8197F(x)                                                  \
	(((x) & BIT_MASK_BW_SIGTA_8197F) << BIT_SHIFT_BW_SIGTA_8197F)
#define BITS_BW_SIGTA_8197F                                                    \
	(BIT_MASK_BW_SIGTA_8197F << BIT_SHIFT_BW_SIGTA_8197F)
#define BIT_CLEAR_BW_SIGTA_8197F(x) ((x) & (~BITS_BW_SIGTA_8197F))
#define BIT_GET_BW_SIGTA_8197F(x)                                              \
	(((x) >> BIT_SHIFT_BW_SIGTA_8197F) & BIT_MASK_BW_SIGTA_8197F)
#define BIT_SET_BW_SIGTA_8197F(x, v)                                           \
	(BIT_CLEAR_BW_SIGTA_8197F(x) | BIT_BW_SIGTA_8197F(v))

#define BIT_EN_BAR_SIGTA_8197F BIT(2)

#define BIT_SHIFT_R_NDPA_BW_8197F 0
#define BIT_MASK_R_NDPA_BW_8197F 0x3
#define BIT_R_NDPA_BW_8197F(x)                                                 \
	(((x) & BIT_MASK_R_NDPA_BW_8197F) << BIT_SHIFT_R_NDPA_BW_8197F)
#define BITS_R_NDPA_BW_8197F                                                   \
	(BIT_MASK_R_NDPA_BW_8197F << BIT_SHIFT_R_NDPA_BW_8197F)
#define BIT_CLEAR_R_NDPA_BW_8197F(x) ((x) & (~BITS_R_NDPA_BW_8197F))
#define BIT_GET_R_NDPA_BW_8197F(x)                                             \
	(((x) >> BIT_SHIFT_R_NDPA_BW_8197F) & BIT_MASK_R_NDPA_BW_8197F)
#define BIT_SET_R_NDPA_BW_8197F(x, v)                                          \
	(BIT_CLEAR_R_NDPA_BW_8197F(x) | BIT_R_NDPA_BW_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_RD_RESP_PKT_TH_8197F */

#define BIT_SHIFT_RD_RESP_PKT_TH_V1_8197F 0
#define BIT_MASK_RD_RESP_PKT_TH_V1_8197F 0x3f
#define BIT_RD_RESP_PKT_TH_V1_8197F(x)                                         \
	(((x) & BIT_MASK_RD_RESP_PKT_TH_V1_8197F)                              \
	 << BIT_SHIFT_RD_RESP_PKT_TH_V1_8197F)
#define BITS_RD_RESP_PKT_TH_V1_8197F                                           \
	(BIT_MASK_RD_RESP_PKT_TH_V1_8197F << BIT_SHIFT_RD_RESP_PKT_TH_V1_8197F)
#define BIT_CLEAR_RD_RESP_PKT_TH_V1_8197F(x)                                   \
	((x) & (~BITS_RD_RESP_PKT_TH_V1_8197F))
#define BIT_GET_RD_RESP_PKT_TH_V1_8197F(x)                                     \
	(((x) >> BIT_SHIFT_RD_RESP_PKT_TH_V1_8197F) &                          \
	 BIT_MASK_RD_RESP_PKT_TH_V1_8197F)
#define BIT_SET_RD_RESP_PKT_TH_V1_8197F(x, v)                                  \
	(BIT_CLEAR_RD_RESP_PKT_TH_V1_8197F(x) | BIT_RD_RESP_PKT_TH_V1_8197F(v))

/* 2 REG_CMDQ_INFO_8197F */

#define BIT_SHIFT_PKT_NUM_8197F 23
#define BIT_MASK_PKT_NUM_8197F 0x1ff
#define BIT_PKT_NUM_8197F(x)                                                   \
	(((x) & BIT_MASK_PKT_NUM_8197F) << BIT_SHIFT_PKT_NUM_8197F)
#define BITS_PKT_NUM_8197F (BIT_MASK_PKT_NUM_8197F << BIT_SHIFT_PKT_NUM_8197F)
#define BIT_CLEAR_PKT_NUM_8197F(x) ((x) & (~BITS_PKT_NUM_8197F))
#define BIT_GET_PKT_NUM_8197F(x)                                               \
	(((x) >> BIT_SHIFT_PKT_NUM_8197F) & BIT_MASK_PKT_NUM_8197F)
#define BIT_SET_PKT_NUM_8197F(x, v)                                            \
	(BIT_CLEAR_PKT_NUM_8197F(x) | BIT_PKT_NUM_8197F(v))

#define BIT_TIDEMPTY_CMDQ_V1_8197F BIT(22)

#define BIT_SHIFT_TAIL_PKT_CMDQ_V2_8197F 11
#define BIT_MASK_TAIL_PKT_CMDQ_V2_8197F 0x7ff
#define BIT_TAIL_PKT_CMDQ_V2_8197F(x)                                          \
	(((x) & BIT_MASK_TAIL_PKT_CMDQ_V2_8197F)                               \
	 << BIT_SHIFT_TAIL_PKT_CMDQ_V2_8197F)
#define BITS_TAIL_PKT_CMDQ_V2_8197F                                            \
	(BIT_MASK_TAIL_PKT_CMDQ_V2_8197F << BIT_SHIFT_TAIL_PKT_CMDQ_V2_8197F)
#define BIT_CLEAR_TAIL_PKT_CMDQ_V2_8197F(x)                                    \
	((x) & (~BITS_TAIL_PKT_CMDQ_V2_8197F))
#define BIT_GET_TAIL_PKT_CMDQ_V2_8197F(x)                                      \
	(((x) >> BIT_SHIFT_TAIL_PKT_CMDQ_V2_8197F) &                           \
	 BIT_MASK_TAIL_PKT_CMDQ_V2_8197F)
#define BIT_SET_TAIL_PKT_CMDQ_V2_8197F(x, v)                                   \
	(BIT_CLEAR_TAIL_PKT_CMDQ_V2_8197F(x) | BIT_TAIL_PKT_CMDQ_V2_8197F(v))

#define BIT_SHIFT_HEAD_PKT_CMDQ_V1_8197F 0
#define BIT_MASK_HEAD_PKT_CMDQ_V1_8197F 0x7ff
#define BIT_HEAD_PKT_CMDQ_V1_8197F(x)                                          \
	(((x) & BIT_MASK_HEAD_PKT_CMDQ_V1_8197F)                               \
	 << BIT_SHIFT_HEAD_PKT_CMDQ_V1_8197F)
#define BITS_HEAD_PKT_CMDQ_V1_8197F                                            \
	(BIT_MASK_HEAD_PKT_CMDQ_V1_8197F << BIT_SHIFT_HEAD_PKT_CMDQ_V1_8197F)
#define BIT_CLEAR_HEAD_PKT_CMDQ_V1_8197F(x)                                    \
	((x) & (~BITS_HEAD_PKT_CMDQ_V1_8197F))
#define BIT_GET_HEAD_PKT_CMDQ_V1_8197F(x)                                      \
	(((x) >> BIT_SHIFT_HEAD_PKT_CMDQ_V1_8197F) &                           \
	 BIT_MASK_HEAD_PKT_CMDQ_V1_8197F)
#define BIT_SET_HEAD_PKT_CMDQ_V1_8197F(x, v)                                   \
	(BIT_CLEAR_HEAD_PKT_CMDQ_V1_8197F(x) | BIT_HEAD_PKT_CMDQ_V1_8197F(v))

/* 2 REG_Q4_INFO_8197F */

#define BIT_SHIFT_QUEUEMACID_Q4_V1_8197F 25
#define BIT_MASK_QUEUEMACID_Q4_V1_8197F 0x7f
#define BIT_QUEUEMACID_Q4_V1_8197F(x)                                          \
	(((x) & BIT_MASK_QUEUEMACID_Q4_V1_8197F)                               \
	 << BIT_SHIFT_QUEUEMACID_Q4_V1_8197F)
#define BITS_QUEUEMACID_Q4_V1_8197F                                            \
	(BIT_MASK_QUEUEMACID_Q4_V1_8197F << BIT_SHIFT_QUEUEMACID_Q4_V1_8197F)
#define BIT_CLEAR_QUEUEMACID_Q4_V1_8197F(x)                                    \
	((x) & (~BITS_QUEUEMACID_Q4_V1_8197F))
#define BIT_GET_QUEUEMACID_Q4_V1_8197F(x)                                      \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q4_V1_8197F) &                           \
	 BIT_MASK_QUEUEMACID_Q4_V1_8197F)
#define BIT_SET_QUEUEMACID_Q4_V1_8197F(x, v)                                   \
	(BIT_CLEAR_QUEUEMACID_Q4_V1_8197F(x) | BIT_QUEUEMACID_Q4_V1_8197F(v))

#define BIT_SHIFT_QUEUEAC_Q4_V1_8197F 23
#define BIT_MASK_QUEUEAC_Q4_V1_8197F 0x3
#define BIT_QUEUEAC_Q4_V1_8197F(x)                                             \
	(((x) & BIT_MASK_QUEUEAC_Q4_V1_8197F) << BIT_SHIFT_QUEUEAC_Q4_V1_8197F)
#define BITS_QUEUEAC_Q4_V1_8197F                                               \
	(BIT_MASK_QUEUEAC_Q4_V1_8197F << BIT_SHIFT_QUEUEAC_Q4_V1_8197F)
#define BIT_CLEAR_QUEUEAC_Q4_V1_8197F(x) ((x) & (~BITS_QUEUEAC_Q4_V1_8197F))
#define BIT_GET_QUEUEAC_Q4_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_QUEUEAC_Q4_V1_8197F) & BIT_MASK_QUEUEAC_Q4_V1_8197F)
#define BIT_SET_QUEUEAC_Q4_V1_8197F(x, v)                                      \
	(BIT_CLEAR_QUEUEAC_Q4_V1_8197F(x) | BIT_QUEUEAC_Q4_V1_8197F(v))

#define BIT_TIDEMPTY_Q4_V1_8197F BIT(22)

#define BIT_SHIFT_TAIL_PKT_Q4_V2_8197F 11
#define BIT_MASK_TAIL_PKT_Q4_V2_8197F 0x7ff
#define BIT_TAIL_PKT_Q4_V2_8197F(x)                                            \
	(((x) & BIT_MASK_TAIL_PKT_Q4_V2_8197F)                                 \
	 << BIT_SHIFT_TAIL_PKT_Q4_V2_8197F)
#define BITS_TAIL_PKT_Q4_V2_8197F                                              \
	(BIT_MASK_TAIL_PKT_Q4_V2_8197F << BIT_SHIFT_TAIL_PKT_Q4_V2_8197F)
#define BIT_CLEAR_TAIL_PKT_Q4_V2_8197F(x) ((x) & (~BITS_TAIL_PKT_Q4_V2_8197F))
#define BIT_GET_TAIL_PKT_Q4_V2_8197F(x)                                        \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q4_V2_8197F) &                             \
	 BIT_MASK_TAIL_PKT_Q4_V2_8197F)
#define BIT_SET_TAIL_PKT_Q4_V2_8197F(x, v)                                     \
	(BIT_CLEAR_TAIL_PKT_Q4_V2_8197F(x) | BIT_TAIL_PKT_Q4_V2_8197F(v))

#define BIT_SHIFT_HEAD_PKT_Q4_V1_8197F 0
#define BIT_MASK_HEAD_PKT_Q4_V1_8197F 0x7ff
#define BIT_HEAD_PKT_Q4_V1_8197F(x)                                            \
	(((x) & BIT_MASK_HEAD_PKT_Q4_V1_8197F)                                 \
	 << BIT_SHIFT_HEAD_PKT_Q4_V1_8197F)
#define BITS_HEAD_PKT_Q4_V1_8197F                                              \
	(BIT_MASK_HEAD_PKT_Q4_V1_8197F << BIT_SHIFT_HEAD_PKT_Q4_V1_8197F)
#define BIT_CLEAR_HEAD_PKT_Q4_V1_8197F(x) ((x) & (~BITS_HEAD_PKT_Q4_V1_8197F))
#define BIT_GET_HEAD_PKT_Q4_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q4_V1_8197F) &                             \
	 BIT_MASK_HEAD_PKT_Q4_V1_8197F)
#define BIT_SET_HEAD_PKT_Q4_V1_8197F(x, v)                                     \
	(BIT_CLEAR_HEAD_PKT_Q4_V1_8197F(x) | BIT_HEAD_PKT_Q4_V1_8197F(v))

/* 2 REG_Q5_INFO_8197F */

#define BIT_SHIFT_QUEUEMACID_Q5_V1_8197F 25
#define BIT_MASK_QUEUEMACID_Q5_V1_8197F 0x7f
#define BIT_QUEUEMACID_Q5_V1_8197F(x)                                          \
	(((x) & BIT_MASK_QUEUEMACID_Q5_V1_8197F)                               \
	 << BIT_SHIFT_QUEUEMACID_Q5_V1_8197F)
#define BITS_QUEUEMACID_Q5_V1_8197F                                            \
	(BIT_MASK_QUEUEMACID_Q5_V1_8197F << BIT_SHIFT_QUEUEMACID_Q5_V1_8197F)
#define BIT_CLEAR_QUEUEMACID_Q5_V1_8197F(x)                                    \
	((x) & (~BITS_QUEUEMACID_Q5_V1_8197F))
#define BIT_GET_QUEUEMACID_Q5_V1_8197F(x)                                      \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q5_V1_8197F) &                           \
	 BIT_MASK_QUEUEMACID_Q5_V1_8197F)
#define BIT_SET_QUEUEMACID_Q5_V1_8197F(x, v)                                   \
	(BIT_CLEAR_QUEUEMACID_Q5_V1_8197F(x) | BIT_QUEUEMACID_Q5_V1_8197F(v))

#define BIT_SHIFT_QUEUEAC_Q5_V1_8197F 23
#define BIT_MASK_QUEUEAC_Q5_V1_8197F 0x3
#define BIT_QUEUEAC_Q5_V1_8197F(x)                                             \
	(((x) & BIT_MASK_QUEUEAC_Q5_V1_8197F) << BIT_SHIFT_QUEUEAC_Q5_V1_8197F)
#define BITS_QUEUEAC_Q5_V1_8197F                                               \
	(BIT_MASK_QUEUEAC_Q5_V1_8197F << BIT_SHIFT_QUEUEAC_Q5_V1_8197F)
#define BIT_CLEAR_QUEUEAC_Q5_V1_8197F(x) ((x) & (~BITS_QUEUEAC_Q5_V1_8197F))
#define BIT_GET_QUEUEAC_Q5_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_QUEUEAC_Q5_V1_8197F) & BIT_MASK_QUEUEAC_Q5_V1_8197F)
#define BIT_SET_QUEUEAC_Q5_V1_8197F(x, v)                                      \
	(BIT_CLEAR_QUEUEAC_Q5_V1_8197F(x) | BIT_QUEUEAC_Q5_V1_8197F(v))

#define BIT_TIDEMPTY_Q5_V1_8197F BIT(22)

#define BIT_SHIFT_TAIL_PKT_Q5_V2_8197F 11
#define BIT_MASK_TAIL_PKT_Q5_V2_8197F 0x7ff
#define BIT_TAIL_PKT_Q5_V2_8197F(x)                                            \
	(((x) & BIT_MASK_TAIL_PKT_Q5_V2_8197F)                                 \
	 << BIT_SHIFT_TAIL_PKT_Q5_V2_8197F)
#define BITS_TAIL_PKT_Q5_V2_8197F                                              \
	(BIT_MASK_TAIL_PKT_Q5_V2_8197F << BIT_SHIFT_TAIL_PKT_Q5_V2_8197F)
#define BIT_CLEAR_TAIL_PKT_Q5_V2_8197F(x) ((x) & (~BITS_TAIL_PKT_Q5_V2_8197F))
#define BIT_GET_TAIL_PKT_Q5_V2_8197F(x)                                        \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q5_V2_8197F) &                             \
	 BIT_MASK_TAIL_PKT_Q5_V2_8197F)
#define BIT_SET_TAIL_PKT_Q5_V2_8197F(x, v)                                     \
	(BIT_CLEAR_TAIL_PKT_Q5_V2_8197F(x) | BIT_TAIL_PKT_Q5_V2_8197F(v))

#define BIT_SHIFT_HEAD_PKT_Q5_V1_8197F 0
#define BIT_MASK_HEAD_PKT_Q5_V1_8197F 0x7ff
#define BIT_HEAD_PKT_Q5_V1_8197F(x)                                            \
	(((x) & BIT_MASK_HEAD_PKT_Q5_V1_8197F)                                 \
	 << BIT_SHIFT_HEAD_PKT_Q5_V1_8197F)
#define BITS_HEAD_PKT_Q5_V1_8197F                                              \
	(BIT_MASK_HEAD_PKT_Q5_V1_8197F << BIT_SHIFT_HEAD_PKT_Q5_V1_8197F)
#define BIT_CLEAR_HEAD_PKT_Q5_V1_8197F(x) ((x) & (~BITS_HEAD_PKT_Q5_V1_8197F))
#define BIT_GET_HEAD_PKT_Q5_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q5_V1_8197F) &                             \
	 BIT_MASK_HEAD_PKT_Q5_V1_8197F)
#define BIT_SET_HEAD_PKT_Q5_V1_8197F(x, v)                                     \
	(BIT_CLEAR_HEAD_PKT_Q5_V1_8197F(x) | BIT_HEAD_PKT_Q5_V1_8197F(v))

/* 2 REG_Q6_INFO_8197F */

#define BIT_SHIFT_QUEUEMACID_Q6_V1_8197F 25
#define BIT_MASK_QUEUEMACID_Q6_V1_8197F 0x7f
#define BIT_QUEUEMACID_Q6_V1_8197F(x)                                          \
	(((x) & BIT_MASK_QUEUEMACID_Q6_V1_8197F)                               \
	 << BIT_SHIFT_QUEUEMACID_Q6_V1_8197F)
#define BITS_QUEUEMACID_Q6_V1_8197F                                            \
	(BIT_MASK_QUEUEMACID_Q6_V1_8197F << BIT_SHIFT_QUEUEMACID_Q6_V1_8197F)
#define BIT_CLEAR_QUEUEMACID_Q6_V1_8197F(x)                                    \
	((x) & (~BITS_QUEUEMACID_Q6_V1_8197F))
#define BIT_GET_QUEUEMACID_Q6_V1_8197F(x)                                      \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q6_V1_8197F) &                           \
	 BIT_MASK_QUEUEMACID_Q6_V1_8197F)
#define BIT_SET_QUEUEMACID_Q6_V1_8197F(x, v)                                   \
	(BIT_CLEAR_QUEUEMACID_Q6_V1_8197F(x) | BIT_QUEUEMACID_Q6_V1_8197F(v))

#define BIT_SHIFT_QUEUEAC_Q6_V1_8197F 23
#define BIT_MASK_QUEUEAC_Q6_V1_8197F 0x3
#define BIT_QUEUEAC_Q6_V1_8197F(x)                                             \
	(((x) & BIT_MASK_QUEUEAC_Q6_V1_8197F) << BIT_SHIFT_QUEUEAC_Q6_V1_8197F)
#define BITS_QUEUEAC_Q6_V1_8197F                                               \
	(BIT_MASK_QUEUEAC_Q6_V1_8197F << BIT_SHIFT_QUEUEAC_Q6_V1_8197F)
#define BIT_CLEAR_QUEUEAC_Q6_V1_8197F(x) ((x) & (~BITS_QUEUEAC_Q6_V1_8197F))
#define BIT_GET_QUEUEAC_Q6_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_QUEUEAC_Q6_V1_8197F) & BIT_MASK_QUEUEAC_Q6_V1_8197F)
#define BIT_SET_QUEUEAC_Q6_V1_8197F(x, v)                                      \
	(BIT_CLEAR_QUEUEAC_Q6_V1_8197F(x) | BIT_QUEUEAC_Q6_V1_8197F(v))

#define BIT_TIDEMPTY_Q6_V1_8197F BIT(22)

#define BIT_SHIFT_TAIL_PKT_Q6_V2_8197F 11
#define BIT_MASK_TAIL_PKT_Q6_V2_8197F 0x7ff
#define BIT_TAIL_PKT_Q6_V2_8197F(x)                                            \
	(((x) & BIT_MASK_TAIL_PKT_Q6_V2_8197F)                                 \
	 << BIT_SHIFT_TAIL_PKT_Q6_V2_8197F)
#define BITS_TAIL_PKT_Q6_V2_8197F                                              \
	(BIT_MASK_TAIL_PKT_Q6_V2_8197F << BIT_SHIFT_TAIL_PKT_Q6_V2_8197F)
#define BIT_CLEAR_TAIL_PKT_Q6_V2_8197F(x) ((x) & (~BITS_TAIL_PKT_Q6_V2_8197F))
#define BIT_GET_TAIL_PKT_Q6_V2_8197F(x)                                        \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q6_V2_8197F) &                             \
	 BIT_MASK_TAIL_PKT_Q6_V2_8197F)
#define BIT_SET_TAIL_PKT_Q6_V2_8197F(x, v)                                     \
	(BIT_CLEAR_TAIL_PKT_Q6_V2_8197F(x) | BIT_TAIL_PKT_Q6_V2_8197F(v))

#define BIT_SHIFT_HEAD_PKT_Q6_V1_8197F 0
#define BIT_MASK_HEAD_PKT_Q6_V1_8197F 0x7ff
#define BIT_HEAD_PKT_Q6_V1_8197F(x)                                            \
	(((x) & BIT_MASK_HEAD_PKT_Q6_V1_8197F)                                 \
	 << BIT_SHIFT_HEAD_PKT_Q6_V1_8197F)
#define BITS_HEAD_PKT_Q6_V1_8197F                                              \
	(BIT_MASK_HEAD_PKT_Q6_V1_8197F << BIT_SHIFT_HEAD_PKT_Q6_V1_8197F)
#define BIT_CLEAR_HEAD_PKT_Q6_V1_8197F(x) ((x) & (~BITS_HEAD_PKT_Q6_V1_8197F))
#define BIT_GET_HEAD_PKT_Q6_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q6_V1_8197F) &                             \
	 BIT_MASK_HEAD_PKT_Q6_V1_8197F)
#define BIT_SET_HEAD_PKT_Q6_V1_8197F(x, v)                                     \
	(BIT_CLEAR_HEAD_PKT_Q6_V1_8197F(x) | BIT_HEAD_PKT_Q6_V1_8197F(v))

/* 2 REG_Q7_INFO_8197F */

#define BIT_SHIFT_QUEUEMACID_Q7_V1_8197F 25
#define BIT_MASK_QUEUEMACID_Q7_V1_8197F 0x7f
#define BIT_QUEUEMACID_Q7_V1_8197F(x)                                          \
	(((x) & BIT_MASK_QUEUEMACID_Q7_V1_8197F)                               \
	 << BIT_SHIFT_QUEUEMACID_Q7_V1_8197F)
#define BITS_QUEUEMACID_Q7_V1_8197F                                            \
	(BIT_MASK_QUEUEMACID_Q7_V1_8197F << BIT_SHIFT_QUEUEMACID_Q7_V1_8197F)
#define BIT_CLEAR_QUEUEMACID_Q7_V1_8197F(x)                                    \
	((x) & (~BITS_QUEUEMACID_Q7_V1_8197F))
#define BIT_GET_QUEUEMACID_Q7_V1_8197F(x)                                      \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q7_V1_8197F) &                           \
	 BIT_MASK_QUEUEMACID_Q7_V1_8197F)
#define BIT_SET_QUEUEMACID_Q7_V1_8197F(x, v)                                   \
	(BIT_CLEAR_QUEUEMACID_Q7_V1_8197F(x) | BIT_QUEUEMACID_Q7_V1_8197F(v))

#define BIT_SHIFT_QUEUEAC_Q7_V1_8197F 23
#define BIT_MASK_QUEUEAC_Q7_V1_8197F 0x3
#define BIT_QUEUEAC_Q7_V1_8197F(x)                                             \
	(((x) & BIT_MASK_QUEUEAC_Q7_V1_8197F) << BIT_SHIFT_QUEUEAC_Q7_V1_8197F)
#define BITS_QUEUEAC_Q7_V1_8197F                                               \
	(BIT_MASK_QUEUEAC_Q7_V1_8197F << BIT_SHIFT_QUEUEAC_Q7_V1_8197F)
#define BIT_CLEAR_QUEUEAC_Q7_V1_8197F(x) ((x) & (~BITS_QUEUEAC_Q7_V1_8197F))
#define BIT_GET_QUEUEAC_Q7_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_QUEUEAC_Q7_V1_8197F) & BIT_MASK_QUEUEAC_Q7_V1_8197F)
#define BIT_SET_QUEUEAC_Q7_V1_8197F(x, v)                                      \
	(BIT_CLEAR_QUEUEAC_Q7_V1_8197F(x) | BIT_QUEUEAC_Q7_V1_8197F(v))

#define BIT_TIDEMPTY_Q7_V1_8197F BIT(22)

#define BIT_SHIFT_TAIL_PKT_Q7_V2_8197F 11
#define BIT_MASK_TAIL_PKT_Q7_V2_8197F 0x7ff
#define BIT_TAIL_PKT_Q7_V2_8197F(x)                                            \
	(((x) & BIT_MASK_TAIL_PKT_Q7_V2_8197F)                                 \
	 << BIT_SHIFT_TAIL_PKT_Q7_V2_8197F)
#define BITS_TAIL_PKT_Q7_V2_8197F                                              \
	(BIT_MASK_TAIL_PKT_Q7_V2_8197F << BIT_SHIFT_TAIL_PKT_Q7_V2_8197F)
#define BIT_CLEAR_TAIL_PKT_Q7_V2_8197F(x) ((x) & (~BITS_TAIL_PKT_Q7_V2_8197F))
#define BIT_GET_TAIL_PKT_Q7_V2_8197F(x)                                        \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q7_V2_8197F) &                             \
	 BIT_MASK_TAIL_PKT_Q7_V2_8197F)
#define BIT_SET_TAIL_PKT_Q7_V2_8197F(x, v)                                     \
	(BIT_CLEAR_TAIL_PKT_Q7_V2_8197F(x) | BIT_TAIL_PKT_Q7_V2_8197F(v))

#define BIT_SHIFT_HEAD_PKT_Q7_V1_8197F 0
#define BIT_MASK_HEAD_PKT_Q7_V1_8197F 0x7ff
#define BIT_HEAD_PKT_Q7_V1_8197F(x)                                            \
	(((x) & BIT_MASK_HEAD_PKT_Q7_V1_8197F)                                 \
	 << BIT_SHIFT_HEAD_PKT_Q7_V1_8197F)
#define BITS_HEAD_PKT_Q7_V1_8197F                                              \
	(BIT_MASK_HEAD_PKT_Q7_V1_8197F << BIT_SHIFT_HEAD_PKT_Q7_V1_8197F)
#define BIT_CLEAR_HEAD_PKT_Q7_V1_8197F(x) ((x) & (~BITS_HEAD_PKT_Q7_V1_8197F))
#define BIT_GET_HEAD_PKT_Q7_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q7_V1_8197F) &                             \
	 BIT_MASK_HEAD_PKT_Q7_V1_8197F)
#define BIT_SET_HEAD_PKT_Q7_V1_8197F(x, v)                                     \
	(BIT_CLEAR_HEAD_PKT_Q7_V1_8197F(x) | BIT_HEAD_PKT_Q7_V1_8197F(v))

/* 2 REG_WMAC_LBK_BUF_HD_V1_8197F */

#define BIT_SHIFT_WMAC_LBK_BUF_HEAD_V1_8197F 0
#define BIT_MASK_WMAC_LBK_BUF_HEAD_V1_8197F 0xfff
#define BIT_WMAC_LBK_BUF_HEAD_V1_8197F(x)                                      \
	(((x) & BIT_MASK_WMAC_LBK_BUF_HEAD_V1_8197F)                           \
	 << BIT_SHIFT_WMAC_LBK_BUF_HEAD_V1_8197F)
#define BITS_WMAC_LBK_BUF_HEAD_V1_8197F                                        \
	(BIT_MASK_WMAC_LBK_BUF_HEAD_V1_8197F                                   \
	 << BIT_SHIFT_WMAC_LBK_BUF_HEAD_V1_8197F)
#define BIT_CLEAR_WMAC_LBK_BUF_HEAD_V1_8197F(x)                                \
	((x) & (~BITS_WMAC_LBK_BUF_HEAD_V1_8197F))
#define BIT_GET_WMAC_LBK_BUF_HEAD_V1_8197F(x)                                  \
	(((x) >> BIT_SHIFT_WMAC_LBK_BUF_HEAD_V1_8197F) &                       \
	 BIT_MASK_WMAC_LBK_BUF_HEAD_V1_8197F)
#define BIT_SET_WMAC_LBK_BUF_HEAD_V1_8197F(x, v)                               \
	(BIT_CLEAR_WMAC_LBK_BUF_HEAD_V1_8197F(x) |                             \
	 BIT_WMAC_LBK_BUF_HEAD_V1_8197F(v))

/* 2 REG_MGQ_BDNY_V1_8197F */

#define BIT_SHIFT_MGQ_PGBNDY_V1_8197F 0
#define BIT_MASK_MGQ_PGBNDY_V1_8197F 0xfff
#define BIT_MGQ_PGBNDY_V1_8197F(x)                                             \
	(((x) & BIT_MASK_MGQ_PGBNDY_V1_8197F) << BIT_SHIFT_MGQ_PGBNDY_V1_8197F)
#define BITS_MGQ_PGBNDY_V1_8197F                                               \
	(BIT_MASK_MGQ_PGBNDY_V1_8197F << BIT_SHIFT_MGQ_PGBNDY_V1_8197F)
#define BIT_CLEAR_MGQ_PGBNDY_V1_8197F(x) ((x) & (~BITS_MGQ_PGBNDY_V1_8197F))
#define BIT_GET_MGQ_PGBNDY_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_MGQ_PGBNDY_V1_8197F) & BIT_MASK_MGQ_PGBNDY_V1_8197F)
#define BIT_SET_MGQ_PGBNDY_V1_8197F(x, v)                                      \
	(BIT_CLEAR_MGQ_PGBNDY_V1_8197F(x) | BIT_MGQ_PGBNDY_V1_8197F(v))

/* 2 REG_TXRPT_CTRL_8197F */

#define BIT_SHIFT_TRXRPT_TIMER_TH_8197F 24
#define BIT_MASK_TRXRPT_TIMER_TH_8197F 0xff
#define BIT_TRXRPT_TIMER_TH_8197F(x)                                           \
	(((x) & BIT_MASK_TRXRPT_TIMER_TH_8197F)                                \
	 << BIT_SHIFT_TRXRPT_TIMER_TH_8197F)
#define BITS_TRXRPT_TIMER_TH_8197F                                             \
	(BIT_MASK_TRXRPT_TIMER_TH_8197F << BIT_SHIFT_TRXRPT_TIMER_TH_8197F)
#define BIT_CLEAR_TRXRPT_TIMER_TH_8197F(x) ((x) & (~BITS_TRXRPT_TIMER_TH_8197F))
#define BIT_GET_TRXRPT_TIMER_TH_8197F(x)                                       \
	(((x) >> BIT_SHIFT_TRXRPT_TIMER_TH_8197F) &                            \
	 BIT_MASK_TRXRPT_TIMER_TH_8197F)
#define BIT_SET_TRXRPT_TIMER_TH_8197F(x, v)                                    \
	(BIT_CLEAR_TRXRPT_TIMER_TH_8197F(x) | BIT_TRXRPT_TIMER_TH_8197F(v))

#define BIT_SHIFT_TRXRPT_LEN_TH_8197F 16
#define BIT_MASK_TRXRPT_LEN_TH_8197F 0xff
#define BIT_TRXRPT_LEN_TH_8197F(x)                                             \
	(((x) & BIT_MASK_TRXRPT_LEN_TH_8197F) << BIT_SHIFT_TRXRPT_LEN_TH_8197F)
#define BITS_TRXRPT_LEN_TH_8197F                                               \
	(BIT_MASK_TRXRPT_LEN_TH_8197F << BIT_SHIFT_TRXRPT_LEN_TH_8197F)
#define BIT_CLEAR_TRXRPT_LEN_TH_8197F(x) ((x) & (~BITS_TRXRPT_LEN_TH_8197F))
#define BIT_GET_TRXRPT_LEN_TH_8197F(x)                                         \
	(((x) >> BIT_SHIFT_TRXRPT_LEN_TH_8197F) & BIT_MASK_TRXRPT_LEN_TH_8197F)
#define BIT_SET_TRXRPT_LEN_TH_8197F(x, v)                                      \
	(BIT_CLEAR_TRXRPT_LEN_TH_8197F(x) | BIT_TRXRPT_LEN_TH_8197F(v))

#define BIT_SHIFT_TRXRPT_READ_PTR_8197F 8
#define BIT_MASK_TRXRPT_READ_PTR_8197F 0xff
#define BIT_TRXRPT_READ_PTR_8197F(x)                                           \
	(((x) & BIT_MASK_TRXRPT_READ_PTR_8197F)                                \
	 << BIT_SHIFT_TRXRPT_READ_PTR_8197F)
#define BITS_TRXRPT_READ_PTR_8197F                                             \
	(BIT_MASK_TRXRPT_READ_PTR_8197F << BIT_SHIFT_TRXRPT_READ_PTR_8197F)
#define BIT_CLEAR_TRXRPT_READ_PTR_8197F(x) ((x) & (~BITS_TRXRPT_READ_PTR_8197F))
#define BIT_GET_TRXRPT_READ_PTR_8197F(x)                                       \
	(((x) >> BIT_SHIFT_TRXRPT_READ_PTR_8197F) &                            \
	 BIT_MASK_TRXRPT_READ_PTR_8197F)
#define BIT_SET_TRXRPT_READ_PTR_8197F(x, v)                                    \
	(BIT_CLEAR_TRXRPT_READ_PTR_8197F(x) | BIT_TRXRPT_READ_PTR_8197F(v))

#define BIT_SHIFT_TRXRPT_WRITE_PTR_8197F 0
#define BIT_MASK_TRXRPT_WRITE_PTR_8197F 0xff
#define BIT_TRXRPT_WRITE_PTR_8197F(x)                                          \
	(((x) & BIT_MASK_TRXRPT_WRITE_PTR_8197F)                               \
	 << BIT_SHIFT_TRXRPT_WRITE_PTR_8197F)
#define BITS_TRXRPT_WRITE_PTR_8197F                                            \
	(BIT_MASK_TRXRPT_WRITE_PTR_8197F << BIT_SHIFT_TRXRPT_WRITE_PTR_8197F)
#define BIT_CLEAR_TRXRPT_WRITE_PTR_8197F(x)                                    \
	((x) & (~BITS_TRXRPT_WRITE_PTR_8197F))
#define BIT_GET_TRXRPT_WRITE_PTR_8197F(x)                                      \
	(((x) >> BIT_SHIFT_TRXRPT_WRITE_PTR_8197F) &                           \
	 BIT_MASK_TRXRPT_WRITE_PTR_8197F)
#define BIT_SET_TRXRPT_WRITE_PTR_8197F(x, v)                                   \
	(BIT_CLEAR_TRXRPT_WRITE_PTR_8197F(x) | BIT_TRXRPT_WRITE_PTR_8197F(v))

/* 2 REG_INIRTS_RATE_SEL_8197F */
#define BIT_LEAG_RTS_BW_DUP_8197F BIT(5)

/* 2 REG_BASIC_CFEND_RATE_8197F */

#define BIT_SHIFT_BASIC_CFEND_RATE_8197F 0
#define BIT_MASK_BASIC_CFEND_RATE_8197F 0x1f
#define BIT_BASIC_CFEND_RATE_8197F(x)                                          \
	(((x) & BIT_MASK_BASIC_CFEND_RATE_8197F)                               \
	 << BIT_SHIFT_BASIC_CFEND_RATE_8197F)
#define BITS_BASIC_CFEND_RATE_8197F                                            \
	(BIT_MASK_BASIC_CFEND_RATE_8197F << BIT_SHIFT_BASIC_CFEND_RATE_8197F)
#define BIT_CLEAR_BASIC_CFEND_RATE_8197F(x)                                    \
	((x) & (~BITS_BASIC_CFEND_RATE_8197F))
#define BIT_GET_BASIC_CFEND_RATE_8197F(x)                                      \
	(((x) >> BIT_SHIFT_BASIC_CFEND_RATE_8197F) &                           \
	 BIT_MASK_BASIC_CFEND_RATE_8197F)
#define BIT_SET_BASIC_CFEND_RATE_8197F(x, v)                                   \
	(BIT_CLEAR_BASIC_CFEND_RATE_8197F(x) | BIT_BASIC_CFEND_RATE_8197F(v))

/* 2 REG_STBC_CFEND_RATE_8197F */

#define BIT_SHIFT_STBC_CFEND_RATE_8197F 0
#define BIT_MASK_STBC_CFEND_RATE_8197F 0x1f
#define BIT_STBC_CFEND_RATE_8197F(x)                                           \
	(((x) & BIT_MASK_STBC_CFEND_RATE_8197F)                                \
	 << BIT_SHIFT_STBC_CFEND_RATE_8197F)
#define BITS_STBC_CFEND_RATE_8197F                                             \
	(BIT_MASK_STBC_CFEND_RATE_8197F << BIT_SHIFT_STBC_CFEND_RATE_8197F)
#define BIT_CLEAR_STBC_CFEND_RATE_8197F(x) ((x) & (~BITS_STBC_CFEND_RATE_8197F))
#define BIT_GET_STBC_CFEND_RATE_8197F(x)                                       \
	(((x) >> BIT_SHIFT_STBC_CFEND_RATE_8197F) &                            \
	 BIT_MASK_STBC_CFEND_RATE_8197F)
#define BIT_SET_STBC_CFEND_RATE_8197F(x, v)                                    \
	(BIT_CLEAR_STBC_CFEND_RATE_8197F(x) | BIT_STBC_CFEND_RATE_8197F(v))

/* 2 REG_DATA_SC_8197F */

#define BIT_SHIFT_TXSC_40M_8197F 4
#define BIT_MASK_TXSC_40M_8197F 0xf
#define BIT_TXSC_40M_8197F(x)                                                  \
	(((x) & BIT_MASK_TXSC_40M_8197F) << BIT_SHIFT_TXSC_40M_8197F)
#define BITS_TXSC_40M_8197F                                                    \
	(BIT_MASK_TXSC_40M_8197F << BIT_SHIFT_TXSC_40M_8197F)
#define BIT_CLEAR_TXSC_40M_8197F(x) ((x) & (~BITS_TXSC_40M_8197F))
#define BIT_GET_TXSC_40M_8197F(x)                                              \
	(((x) >> BIT_SHIFT_TXSC_40M_8197F) & BIT_MASK_TXSC_40M_8197F)
#define BIT_SET_TXSC_40M_8197F(x, v)                                           \
	(BIT_CLEAR_TXSC_40M_8197F(x) | BIT_TXSC_40M_8197F(v))

#define BIT_SHIFT_TXSC_20M_8197F 0
#define BIT_MASK_TXSC_20M_8197F 0xf
#define BIT_TXSC_20M_8197F(x)                                                  \
	(((x) & BIT_MASK_TXSC_20M_8197F) << BIT_SHIFT_TXSC_20M_8197F)
#define BITS_TXSC_20M_8197F                                                    \
	(BIT_MASK_TXSC_20M_8197F << BIT_SHIFT_TXSC_20M_8197F)
#define BIT_CLEAR_TXSC_20M_8197F(x) ((x) & (~BITS_TXSC_20M_8197F))
#define BIT_GET_TXSC_20M_8197F(x)                                              \
	(((x) >> BIT_SHIFT_TXSC_20M_8197F) & BIT_MASK_TXSC_20M_8197F)
#define BIT_SET_TXSC_20M_8197F(x, v)                                           \
	(BIT_CLEAR_TXSC_20M_8197F(x) | BIT_TXSC_20M_8197F(v))

/* 2 REG_MACID_SLEEP3_8197F */

#define BIT_SHIFT_MACID127_96_PKTSLEEP_8197F 0
#define BIT_MASK_MACID127_96_PKTSLEEP_8197F 0xffffffffL
#define BIT_MACID127_96_PKTSLEEP_8197F(x)                                      \
	(((x) & BIT_MASK_MACID127_96_PKTSLEEP_8197F)                           \
	 << BIT_SHIFT_MACID127_96_PKTSLEEP_8197F)
#define BITS_MACID127_96_PKTSLEEP_8197F                                        \
	(BIT_MASK_MACID127_96_PKTSLEEP_8197F                                   \
	 << BIT_SHIFT_MACID127_96_PKTSLEEP_8197F)
#define BIT_CLEAR_MACID127_96_PKTSLEEP_8197F(x)                                \
	((x) & (~BITS_MACID127_96_PKTSLEEP_8197F))
#define BIT_GET_MACID127_96_PKTSLEEP_8197F(x)                                  \
	(((x) >> BIT_SHIFT_MACID127_96_PKTSLEEP_8197F) &                       \
	 BIT_MASK_MACID127_96_PKTSLEEP_8197F)
#define BIT_SET_MACID127_96_PKTSLEEP_8197F(x, v)                               \
	(BIT_CLEAR_MACID127_96_PKTSLEEP_8197F(x) |                             \
	 BIT_MACID127_96_PKTSLEEP_8197F(v))

/* 2 REG_MACID_SLEEP1_8197F */

#define BIT_SHIFT_MACID63_32_PKTSLEEP_8197F 0
#define BIT_MASK_MACID63_32_PKTSLEEP_8197F 0xffffffffL
#define BIT_MACID63_32_PKTSLEEP_8197F(x)                                       \
	(((x) & BIT_MASK_MACID63_32_PKTSLEEP_8197F)                            \
	 << BIT_SHIFT_MACID63_32_PKTSLEEP_8197F)
#define BITS_MACID63_32_PKTSLEEP_8197F                                         \
	(BIT_MASK_MACID63_32_PKTSLEEP_8197F                                    \
	 << BIT_SHIFT_MACID63_32_PKTSLEEP_8197F)
#define BIT_CLEAR_MACID63_32_PKTSLEEP_8197F(x)                                 \
	((x) & (~BITS_MACID63_32_PKTSLEEP_8197F))
#define BIT_GET_MACID63_32_PKTSLEEP_8197F(x)                                   \
	(((x) >> BIT_SHIFT_MACID63_32_PKTSLEEP_8197F) &                        \
	 BIT_MASK_MACID63_32_PKTSLEEP_8197F)
#define BIT_SET_MACID63_32_PKTSLEEP_8197F(x, v)                                \
	(BIT_CLEAR_MACID63_32_PKTSLEEP_8197F(x) |                              \
	 BIT_MACID63_32_PKTSLEEP_8197F(v))

/* 2 REG_ARFR2_V1_8197F */

#define BIT_SHIFT_ARFR2_V1_8197F 0
#define BIT_MASK_ARFR2_V1_8197F 0xffffffffffffffffL
#define BIT_ARFR2_V1_8197F(x)                                                  \
	(((x) & BIT_MASK_ARFR2_V1_8197F) << BIT_SHIFT_ARFR2_V1_8197F)
#define BITS_ARFR2_V1_8197F                                                    \
	(BIT_MASK_ARFR2_V1_8197F << BIT_SHIFT_ARFR2_V1_8197F)
#define BIT_CLEAR_ARFR2_V1_8197F(x) ((x) & (~BITS_ARFR2_V1_8197F))
#define BIT_GET_ARFR2_V1_8197F(x)                                              \
	(((x) >> BIT_SHIFT_ARFR2_V1_8197F) & BIT_MASK_ARFR2_V1_8197F)
#define BIT_SET_ARFR2_V1_8197F(x, v)                                           \
	(BIT_CLEAR_ARFR2_V1_8197F(x) | BIT_ARFR2_V1_8197F(v))

/* 2 REG_ARFR3_V1_8197F */

#define BIT_SHIFT_ARFR3_V1_8197F 0
#define BIT_MASK_ARFR3_V1_8197F 0xffffffffffffffffL
#define BIT_ARFR3_V1_8197F(x)                                                  \
	(((x) & BIT_MASK_ARFR3_V1_8197F) << BIT_SHIFT_ARFR3_V1_8197F)
#define BITS_ARFR3_V1_8197F                                                    \
	(BIT_MASK_ARFR3_V1_8197F << BIT_SHIFT_ARFR3_V1_8197F)
#define BIT_CLEAR_ARFR3_V1_8197F(x) ((x) & (~BITS_ARFR3_V1_8197F))
#define BIT_GET_ARFR3_V1_8197F(x)                                              \
	(((x) >> BIT_SHIFT_ARFR3_V1_8197F) & BIT_MASK_ARFR3_V1_8197F)
#define BIT_SET_ARFR3_V1_8197F(x, v)                                           \
	(BIT_CLEAR_ARFR3_V1_8197F(x) | BIT_ARFR3_V1_8197F(v))

/* 2 REG_ARFR4_8197F */

#define BIT_SHIFT_ARFR4_8197F 0
#define BIT_MASK_ARFR4_8197F 0xffffffffffffffffL
#define BIT_ARFR4_8197F(x)                                                     \
	(((x) & BIT_MASK_ARFR4_8197F) << BIT_SHIFT_ARFR4_8197F)
#define BITS_ARFR4_8197F (BIT_MASK_ARFR4_8197F << BIT_SHIFT_ARFR4_8197F)
#define BIT_CLEAR_ARFR4_8197F(x) ((x) & (~BITS_ARFR4_8197F))
#define BIT_GET_ARFR4_8197F(x)                                                 \
	(((x) >> BIT_SHIFT_ARFR4_8197F) & BIT_MASK_ARFR4_8197F)
#define BIT_SET_ARFR4_8197F(x, v)                                              \
	(BIT_CLEAR_ARFR4_8197F(x) | BIT_ARFR4_8197F(v))

/* 2 REG_ARFR5_8197F */

#define BIT_SHIFT_ARFR5_8197F 0
#define BIT_MASK_ARFR5_8197F 0xffffffffffffffffL
#define BIT_ARFR5_8197F(x)                                                     \
	(((x) & BIT_MASK_ARFR5_8197F) << BIT_SHIFT_ARFR5_8197F)
#define BITS_ARFR5_8197F (BIT_MASK_ARFR5_8197F << BIT_SHIFT_ARFR5_8197F)
#define BIT_CLEAR_ARFR5_8197F(x) ((x) & (~BITS_ARFR5_8197F))
#define BIT_GET_ARFR5_8197F(x)                                                 \
	(((x) >> BIT_SHIFT_ARFR5_8197F) & BIT_MASK_ARFR5_8197F)
#define BIT_SET_ARFR5_8197F(x, v)                                              \
	(BIT_CLEAR_ARFR5_8197F(x) | BIT_ARFR5_8197F(v))

/* 2 REG_TXRPT_START_OFFSET_8197F */
#define BIT_SHCUT_PARSE_DASA_8197F BIT(25)
#define BIT_SHCUT_BYPASS_8197F BIT(24)
#define BIT__R_RPTFIFO_1K_8197F BIT(16)

#define BIT_SHIFT_MACID_CTRL_OFFSET_8197F 8
#define BIT_MASK_MACID_CTRL_OFFSET_8197F 0xff
#define BIT_MACID_CTRL_OFFSET_8197F(x)                                         \
	(((x) & BIT_MASK_MACID_CTRL_OFFSET_8197F)                              \
	 << BIT_SHIFT_MACID_CTRL_OFFSET_8197F)
#define BITS_MACID_CTRL_OFFSET_8197F                                           \
	(BIT_MASK_MACID_CTRL_OFFSET_8197F << BIT_SHIFT_MACID_CTRL_OFFSET_8197F)
#define BIT_CLEAR_MACID_CTRL_OFFSET_8197F(x)                                   \
	((x) & (~BITS_MACID_CTRL_OFFSET_8197F))
#define BIT_GET_MACID_CTRL_OFFSET_8197F(x)                                     \
	(((x) >> BIT_SHIFT_MACID_CTRL_OFFSET_8197F) &                          \
	 BIT_MASK_MACID_CTRL_OFFSET_8197F)
#define BIT_SET_MACID_CTRL_OFFSET_8197F(x, v)                                  \
	(BIT_CLEAR_MACID_CTRL_OFFSET_8197F(x) | BIT_MACID_CTRL_OFFSET_8197F(v))

#define BIT_SHIFT_AMPDU_TXRPT_OFFSET_8197F 0
#define BIT_MASK_AMPDU_TXRPT_OFFSET_8197F 0xff
#define BIT_AMPDU_TXRPT_OFFSET_8197F(x)                                        \
	(((x) & BIT_MASK_AMPDU_TXRPT_OFFSET_8197F)                             \
	 << BIT_SHIFT_AMPDU_TXRPT_OFFSET_8197F)
#define BITS_AMPDU_TXRPT_OFFSET_8197F                                          \
	(BIT_MASK_AMPDU_TXRPT_OFFSET_8197F                                     \
	 << BIT_SHIFT_AMPDU_TXRPT_OFFSET_8197F)
#define BIT_CLEAR_AMPDU_TXRPT_OFFSET_8197F(x)                                  \
	((x) & (~BITS_AMPDU_TXRPT_OFFSET_8197F))
#define BIT_GET_AMPDU_TXRPT_OFFSET_8197F(x)                                    \
	(((x) >> BIT_SHIFT_AMPDU_TXRPT_OFFSET_8197F) &                         \
	 BIT_MASK_AMPDU_TXRPT_OFFSET_8197F)
#define BIT_SET_AMPDU_TXRPT_OFFSET_8197F(x, v)                                 \
	(BIT_CLEAR_AMPDU_TXRPT_OFFSET_8197F(x) |                               \
	 BIT_AMPDU_TXRPT_OFFSET_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_POWER_STAGE1_8197F */
#define BIT_PTA_WL_PRI_MASK_CPU_MGQ_8197F BIT(31)
#define BIT_PTA_WL_PRI_MASK_BCNQ_8197F BIT(30)
#define BIT_PTA_WL_PRI_MASK_HIQ_8197F BIT(29)
#define BIT_PTA_WL_PRI_MASK_MGQ_8197F BIT(28)
#define BIT_PTA_WL_PRI_MASK_BK_8197F BIT(27)
#define BIT_PTA_WL_PRI_MASK_BE_8197F BIT(26)
#define BIT_PTA_WL_PRI_MASK_VI_8197F BIT(25)
#define BIT_PTA_WL_PRI_MASK_VO_8197F BIT(24)

#define BIT_SHIFT_POWER_STAGE1_8197F 0
#define BIT_MASK_POWER_STAGE1_8197F 0xffffff
#define BIT_POWER_STAGE1_8197F(x)                                              \
	(((x) & BIT_MASK_POWER_STAGE1_8197F) << BIT_SHIFT_POWER_STAGE1_8197F)
#define BITS_POWER_STAGE1_8197F                                                \
	(BIT_MASK_POWER_STAGE1_8197F << BIT_SHIFT_POWER_STAGE1_8197F)
#define BIT_CLEAR_POWER_STAGE1_8197F(x) ((x) & (~BITS_POWER_STAGE1_8197F))
#define BIT_GET_POWER_STAGE1_8197F(x)                                          \
	(((x) >> BIT_SHIFT_POWER_STAGE1_8197F) & BIT_MASK_POWER_STAGE1_8197F)
#define BIT_SET_POWER_STAGE1_8197F(x, v)                                       \
	(BIT_CLEAR_POWER_STAGE1_8197F(x) | BIT_POWER_STAGE1_8197F(v))

/* 2 REG_POWER_STAGE2_8197F */
#define BIT__R_CTRL_PKT_POW_ADJ_8197F BIT(24)

#define BIT_SHIFT_POWER_STAGE2_8197F 0
#define BIT_MASK_POWER_STAGE2_8197F 0xffffff
#define BIT_POWER_STAGE2_8197F(x)                                              \
	(((x) & BIT_MASK_POWER_STAGE2_8197F) << BIT_SHIFT_POWER_STAGE2_8197F)
#define BITS_POWER_STAGE2_8197F                                                \
	(BIT_MASK_POWER_STAGE2_8197F << BIT_SHIFT_POWER_STAGE2_8197F)
#define BIT_CLEAR_POWER_STAGE2_8197F(x) ((x) & (~BITS_POWER_STAGE2_8197F))
#define BIT_GET_POWER_STAGE2_8197F(x)                                          \
	(((x) >> BIT_SHIFT_POWER_STAGE2_8197F) & BIT_MASK_POWER_STAGE2_8197F)
#define BIT_SET_POWER_STAGE2_8197F(x, v)                                       \
	(BIT_CLEAR_POWER_STAGE2_8197F(x) | BIT_POWER_STAGE2_8197F(v))

/* 2 REG_SW_AMPDU_BURST_MODE_CTRL_8197F */

#define BIT_SHIFT_PAD_NUM_THRES_8197F 24
#define BIT_MASK_PAD_NUM_THRES_8197F 0x3f
#define BIT_PAD_NUM_THRES_8197F(x)                                             \
	(((x) & BIT_MASK_PAD_NUM_THRES_8197F) << BIT_SHIFT_PAD_NUM_THRES_8197F)
#define BITS_PAD_NUM_THRES_8197F                                               \
	(BIT_MASK_PAD_NUM_THRES_8197F << BIT_SHIFT_PAD_NUM_THRES_8197F)
#define BIT_CLEAR_PAD_NUM_THRES_8197F(x) ((x) & (~BITS_PAD_NUM_THRES_8197F))
#define BIT_GET_PAD_NUM_THRES_8197F(x)                                         \
	(((x) >> BIT_SHIFT_PAD_NUM_THRES_8197F) & BIT_MASK_PAD_NUM_THRES_8197F)
#define BIT_SET_PAD_NUM_THRES_8197F(x, v)                                      \
	(BIT_CLEAR_PAD_NUM_THRES_8197F(x) | BIT_PAD_NUM_THRES_8197F(v))

#define BIT_R_DMA_THIS_QUEUE_BK_8197F BIT(23)
#define BIT_R_DMA_THIS_QUEUE_BE_8197F BIT(22)
#define BIT_R_DMA_THIS_QUEUE_VI_8197F BIT(21)
#define BIT_R_DMA_THIS_QUEUE_VO_8197F BIT(20)

#define BIT_SHIFT_R_TOTAL_LEN_TH_8197F 8
#define BIT_MASK_R_TOTAL_LEN_TH_8197F 0xfff
#define BIT_R_TOTAL_LEN_TH_8197F(x)                                            \
	(((x) & BIT_MASK_R_TOTAL_LEN_TH_8197F)                                 \
	 << BIT_SHIFT_R_TOTAL_LEN_TH_8197F)
#define BITS_R_TOTAL_LEN_TH_8197F                                              \
	(BIT_MASK_R_TOTAL_LEN_TH_8197F << BIT_SHIFT_R_TOTAL_LEN_TH_8197F)
#define BIT_CLEAR_R_TOTAL_LEN_TH_8197F(x) ((x) & (~BITS_R_TOTAL_LEN_TH_8197F))
#define BIT_GET_R_TOTAL_LEN_TH_8197F(x)                                        \
	(((x) >> BIT_SHIFT_R_TOTAL_LEN_TH_8197F) &                             \
	 BIT_MASK_R_TOTAL_LEN_TH_8197F)
#define BIT_SET_R_TOTAL_LEN_TH_8197F(x, v)                                     \
	(BIT_CLEAR_R_TOTAL_LEN_TH_8197F(x) | BIT_R_TOTAL_LEN_TH_8197F(v))

#define BIT_EN_NEW_EARLY_8197F BIT(7)
#define BIT_PRE_TX_CMD_8197F BIT(6)

#define BIT_SHIFT_NUM_SCL_EN_8197F 4
#define BIT_MASK_NUM_SCL_EN_8197F 0x3
#define BIT_NUM_SCL_EN_8197F(x)                                                \
	(((x) & BIT_MASK_NUM_SCL_EN_8197F) << BIT_SHIFT_NUM_SCL_EN_8197F)
#define BITS_NUM_SCL_EN_8197F                                                  \
	(BIT_MASK_NUM_SCL_EN_8197F << BIT_SHIFT_NUM_SCL_EN_8197F)
#define BIT_CLEAR_NUM_SCL_EN_8197F(x) ((x) & (~BITS_NUM_SCL_EN_8197F))
#define BIT_GET_NUM_SCL_EN_8197F(x)                                            \
	(((x) >> BIT_SHIFT_NUM_SCL_EN_8197F) & BIT_MASK_NUM_SCL_EN_8197F)
#define BIT_SET_NUM_SCL_EN_8197F(x, v)                                         \
	(BIT_CLEAR_NUM_SCL_EN_8197F(x) | BIT_NUM_SCL_EN_8197F(v))

#define BIT_BK_EN_8197F BIT(3)
#define BIT_BE_EN_8197F BIT(2)
#define BIT_VI_EN_8197F BIT(1)
#define BIT_VO_EN_8197F BIT(0)

/* 2 REG_PKT_LIFE_TIME_8197F */

#define BIT_SHIFT_PKT_LIFTIME_BEBK_8197F 16
#define BIT_MASK_PKT_LIFTIME_BEBK_8197F 0xffff
#define BIT_PKT_LIFTIME_BEBK_8197F(x)                                          \
	(((x) & BIT_MASK_PKT_LIFTIME_BEBK_8197F)                               \
	 << BIT_SHIFT_PKT_LIFTIME_BEBK_8197F)
#define BITS_PKT_LIFTIME_BEBK_8197F                                            \
	(BIT_MASK_PKT_LIFTIME_BEBK_8197F << BIT_SHIFT_PKT_LIFTIME_BEBK_8197F)
#define BIT_CLEAR_PKT_LIFTIME_BEBK_8197F(x)                                    \
	((x) & (~BITS_PKT_LIFTIME_BEBK_8197F))
#define BIT_GET_PKT_LIFTIME_BEBK_8197F(x)                                      \
	(((x) >> BIT_SHIFT_PKT_LIFTIME_BEBK_8197F) &                           \
	 BIT_MASK_PKT_LIFTIME_BEBK_8197F)
#define BIT_SET_PKT_LIFTIME_BEBK_8197F(x, v)                                   \
	(BIT_CLEAR_PKT_LIFTIME_BEBK_8197F(x) | BIT_PKT_LIFTIME_BEBK_8197F(v))

#define BIT_SHIFT_PKT_LIFTIME_VOVI_8197F 0
#define BIT_MASK_PKT_LIFTIME_VOVI_8197F 0xffff
#define BIT_PKT_LIFTIME_VOVI_8197F(x)                                          \
	(((x) & BIT_MASK_PKT_LIFTIME_VOVI_8197F)                               \
	 << BIT_SHIFT_PKT_LIFTIME_VOVI_8197F)
#define BITS_PKT_LIFTIME_VOVI_8197F                                            \
	(BIT_MASK_PKT_LIFTIME_VOVI_8197F << BIT_SHIFT_PKT_LIFTIME_VOVI_8197F)
#define BIT_CLEAR_PKT_LIFTIME_VOVI_8197F(x)                                    \
	((x) & (~BITS_PKT_LIFTIME_VOVI_8197F))
#define BIT_GET_PKT_LIFTIME_VOVI_8197F(x)                                      \
	(((x) >> BIT_SHIFT_PKT_LIFTIME_VOVI_8197F) &                           \
	 BIT_MASK_PKT_LIFTIME_VOVI_8197F)
#define BIT_SET_PKT_LIFTIME_VOVI_8197F(x, v)                                   \
	(BIT_CLEAR_PKT_LIFTIME_VOVI_8197F(x) | BIT_PKT_LIFTIME_VOVI_8197F(v))

/* 2 REG_STBC_SETTING_8197F */

#define BIT_SHIFT_CDEND_TXTIME_L_8197F 4
#define BIT_MASK_CDEND_TXTIME_L_8197F 0xf
#define BIT_CDEND_TXTIME_L_8197F(x)                                            \
	(((x) & BIT_MASK_CDEND_TXTIME_L_8197F)                                 \
	 << BIT_SHIFT_CDEND_TXTIME_L_8197F)
#define BITS_CDEND_TXTIME_L_8197F                                              \
	(BIT_MASK_CDEND_TXTIME_L_8197F << BIT_SHIFT_CDEND_TXTIME_L_8197F)
#define BIT_CLEAR_CDEND_TXTIME_L_8197F(x) ((x) & (~BITS_CDEND_TXTIME_L_8197F))
#define BIT_GET_CDEND_TXTIME_L_8197F(x)                                        \
	(((x) >> BIT_SHIFT_CDEND_TXTIME_L_8197F) &                             \
	 BIT_MASK_CDEND_TXTIME_L_8197F)
#define BIT_SET_CDEND_TXTIME_L_8197F(x, v)                                     \
	(BIT_CLEAR_CDEND_TXTIME_L_8197F(x) | BIT_CDEND_TXTIME_L_8197F(v))

#define BIT_SHIFT_NESS_8197F 2
#define BIT_MASK_NESS_8197F 0x3
#define BIT_NESS_8197F(x) (((x) & BIT_MASK_NESS_8197F) << BIT_SHIFT_NESS_8197F)
#define BITS_NESS_8197F (BIT_MASK_NESS_8197F << BIT_SHIFT_NESS_8197F)
#define BIT_CLEAR_NESS_8197F(x) ((x) & (~BITS_NESS_8197F))
#define BIT_GET_NESS_8197F(x)                                                  \
	(((x) >> BIT_SHIFT_NESS_8197F) & BIT_MASK_NESS_8197F)
#define BIT_SET_NESS_8197F(x, v) (BIT_CLEAR_NESS_8197F(x) | BIT_NESS_8197F(v))

#define BIT_SHIFT_STBC_CFEND_8197F 0
#define BIT_MASK_STBC_CFEND_8197F 0x3
#define BIT_STBC_CFEND_8197F(x)                                                \
	(((x) & BIT_MASK_STBC_CFEND_8197F) << BIT_SHIFT_STBC_CFEND_8197F)
#define BITS_STBC_CFEND_8197F                                                  \
	(BIT_MASK_STBC_CFEND_8197F << BIT_SHIFT_STBC_CFEND_8197F)
#define BIT_CLEAR_STBC_CFEND_8197F(x) ((x) & (~BITS_STBC_CFEND_8197F))
#define BIT_GET_STBC_CFEND_8197F(x)                                            \
	(((x) >> BIT_SHIFT_STBC_CFEND_8197F) & BIT_MASK_STBC_CFEND_8197F)
#define BIT_SET_STBC_CFEND_8197F(x, v)                                         \
	(BIT_CLEAR_STBC_CFEND_8197F(x) | BIT_STBC_CFEND_8197F(v))

/* 2 REG_STBC_SETTING2_8197F */

#define BIT_SHIFT_CDEND_TXTIME_H_8197F 0
#define BIT_MASK_CDEND_TXTIME_H_8197F 0x1f
#define BIT_CDEND_TXTIME_H_8197F(x)                                            \
	(((x) & BIT_MASK_CDEND_TXTIME_H_8197F)                                 \
	 << BIT_SHIFT_CDEND_TXTIME_H_8197F)
#define BITS_CDEND_TXTIME_H_8197F                                              \
	(BIT_MASK_CDEND_TXTIME_H_8197F << BIT_SHIFT_CDEND_TXTIME_H_8197F)
#define BIT_CLEAR_CDEND_TXTIME_H_8197F(x) ((x) & (~BITS_CDEND_TXTIME_H_8197F))
#define BIT_GET_CDEND_TXTIME_H_8197F(x)                                        \
	(((x) >> BIT_SHIFT_CDEND_TXTIME_H_8197F) &                             \
	 BIT_MASK_CDEND_TXTIME_H_8197F)
#define BIT_SET_CDEND_TXTIME_H_8197F(x, v)                                     \
	(BIT_CLEAR_CDEND_TXTIME_H_8197F(x) | BIT_CDEND_TXTIME_H_8197F(v))

/* 2 REG_QUEUE_CTRL_8197F */
#define BIT_PTA_EDCCA_EN_8197F BIT(5)
#define BIT_PTA_WL_TX_EN_8197F BIT(4)
#define BIT_R_USE_DATA_BW_8197F BIT(3)
#define BIT_TRI_PKT_INT_MODE1_8197F BIT(2)
#define BIT_TRI_PKT_INT_MODE0_8197F BIT(1)
#define BIT_ACQ_MODE_SEL_8197F BIT(0)

/* 2 REG_SINGLE_AMPDU_CTRL_8197F */
#define BIT_EN_SINGLE_APMDU_8197F BIT(7)

/* 2 REG_PROT_MODE_CTRL_8197F */

#define BIT_SHIFT_RTS_MAX_AGG_NUM_8197F 24
#define BIT_MASK_RTS_MAX_AGG_NUM_8197F 0x3f
#define BIT_RTS_MAX_AGG_NUM_8197F(x)                                           \
	(((x) & BIT_MASK_RTS_MAX_AGG_NUM_8197F)                                \
	 << BIT_SHIFT_RTS_MAX_AGG_NUM_8197F)
#define BITS_RTS_MAX_AGG_NUM_8197F                                             \
	(BIT_MASK_RTS_MAX_AGG_NUM_8197F << BIT_SHIFT_RTS_MAX_AGG_NUM_8197F)
#define BIT_CLEAR_RTS_MAX_AGG_NUM_8197F(x) ((x) & (~BITS_RTS_MAX_AGG_NUM_8197F))
#define BIT_GET_RTS_MAX_AGG_NUM_8197F(x)                                       \
	(((x) >> BIT_SHIFT_RTS_MAX_AGG_NUM_8197F) &                            \
	 BIT_MASK_RTS_MAX_AGG_NUM_8197F)
#define BIT_SET_RTS_MAX_AGG_NUM_8197F(x, v)                                    \
	(BIT_CLEAR_RTS_MAX_AGG_NUM_8197F(x) | BIT_RTS_MAX_AGG_NUM_8197F(v))

#define BIT_SHIFT_MAX_AGG_NUM_8197F 16
#define BIT_MASK_MAX_AGG_NUM_8197F 0x3f
#define BIT_MAX_AGG_NUM_8197F(x)                                               \
	(((x) & BIT_MASK_MAX_AGG_NUM_8197F) << BIT_SHIFT_MAX_AGG_NUM_8197F)
#define BITS_MAX_AGG_NUM_8197F                                                 \
	(BIT_MASK_MAX_AGG_NUM_8197F << BIT_SHIFT_MAX_AGG_NUM_8197F)
#define BIT_CLEAR_MAX_AGG_NUM_8197F(x) ((x) & (~BITS_MAX_AGG_NUM_8197F))
#define BIT_GET_MAX_AGG_NUM_8197F(x)                                           \
	(((x) >> BIT_SHIFT_MAX_AGG_NUM_8197F) & BIT_MASK_MAX_AGG_NUM_8197F)
#define BIT_SET_MAX_AGG_NUM_8197F(x, v)                                        \
	(BIT_CLEAR_MAX_AGG_NUM_8197F(x) | BIT_MAX_AGG_NUM_8197F(v))

#define BIT_SHIFT_RTS_TXTIME_TH_8197F 8
#define BIT_MASK_RTS_TXTIME_TH_8197F 0xff
#define BIT_RTS_TXTIME_TH_8197F(x)                                             \
	(((x) & BIT_MASK_RTS_TXTIME_TH_8197F) << BIT_SHIFT_RTS_TXTIME_TH_8197F)
#define BITS_RTS_TXTIME_TH_8197F                                               \
	(BIT_MASK_RTS_TXTIME_TH_8197F << BIT_SHIFT_RTS_TXTIME_TH_8197F)
#define BIT_CLEAR_RTS_TXTIME_TH_8197F(x) ((x) & (~BITS_RTS_TXTIME_TH_8197F))
#define BIT_GET_RTS_TXTIME_TH_8197F(x)                                         \
	(((x) >> BIT_SHIFT_RTS_TXTIME_TH_8197F) & BIT_MASK_RTS_TXTIME_TH_8197F)
#define BIT_SET_RTS_TXTIME_TH_8197F(x, v)                                      \
	(BIT_CLEAR_RTS_TXTIME_TH_8197F(x) | BIT_RTS_TXTIME_TH_8197F(v))

#define BIT_SHIFT_RTS_LEN_TH_8197F 0
#define BIT_MASK_RTS_LEN_TH_8197F 0xff
#define BIT_RTS_LEN_TH_8197F(x)                                                \
	(((x) & BIT_MASK_RTS_LEN_TH_8197F) << BIT_SHIFT_RTS_LEN_TH_8197F)
#define BITS_RTS_LEN_TH_8197F                                                  \
	(BIT_MASK_RTS_LEN_TH_8197F << BIT_SHIFT_RTS_LEN_TH_8197F)
#define BIT_CLEAR_RTS_LEN_TH_8197F(x) ((x) & (~BITS_RTS_LEN_TH_8197F))
#define BIT_GET_RTS_LEN_TH_8197F(x)                                            \
	(((x) >> BIT_SHIFT_RTS_LEN_TH_8197F) & BIT_MASK_RTS_LEN_TH_8197F)
#define BIT_SET_RTS_LEN_TH_8197F(x, v)                                         \
	(BIT_CLEAR_RTS_LEN_TH_8197F(x) | BIT_RTS_LEN_TH_8197F(v))

/* 2 REG_BAR_MODE_CTRL_8197F */

#define BIT_SHIFT_BAR_RTY_LMT_8197F 16
#define BIT_MASK_BAR_RTY_LMT_8197F 0x3
#define BIT_BAR_RTY_LMT_8197F(x)                                               \
	(((x) & BIT_MASK_BAR_RTY_LMT_8197F) << BIT_SHIFT_BAR_RTY_LMT_8197F)
#define BITS_BAR_RTY_LMT_8197F                                                 \
	(BIT_MASK_BAR_RTY_LMT_8197F << BIT_SHIFT_BAR_RTY_LMT_8197F)
#define BIT_CLEAR_BAR_RTY_LMT_8197F(x) ((x) & (~BITS_BAR_RTY_LMT_8197F))
#define BIT_GET_BAR_RTY_LMT_8197F(x)                                           \
	(((x) >> BIT_SHIFT_BAR_RTY_LMT_8197F) & BIT_MASK_BAR_RTY_LMT_8197F)
#define BIT_SET_BAR_RTY_LMT_8197F(x, v)                                        \
	(BIT_CLEAR_BAR_RTY_LMT_8197F(x) | BIT_BAR_RTY_LMT_8197F(v))

#define BIT_SHIFT_BAR_PKT_TXTIME_TH_8197F 8
#define BIT_MASK_BAR_PKT_TXTIME_TH_8197F 0xff
#define BIT_BAR_PKT_TXTIME_TH_8197F(x)                                         \
	(((x) & BIT_MASK_BAR_PKT_TXTIME_TH_8197F)                              \
	 << BIT_SHIFT_BAR_PKT_TXTIME_TH_8197F)
#define BITS_BAR_PKT_TXTIME_TH_8197F                                           \
	(BIT_MASK_BAR_PKT_TXTIME_TH_8197F << BIT_SHIFT_BAR_PKT_TXTIME_TH_8197F)
#define BIT_CLEAR_BAR_PKT_TXTIME_TH_8197F(x)                                   \
	((x) & (~BITS_BAR_PKT_TXTIME_TH_8197F))
#define BIT_GET_BAR_PKT_TXTIME_TH_8197F(x)                                     \
	(((x) >> BIT_SHIFT_BAR_PKT_TXTIME_TH_8197F) &                          \
	 BIT_MASK_BAR_PKT_TXTIME_TH_8197F)
#define BIT_SET_BAR_PKT_TXTIME_TH_8197F(x, v)                                  \
	(BIT_CLEAR_BAR_PKT_TXTIME_TH_8197F(x) | BIT_BAR_PKT_TXTIME_TH_8197F(v))

#define BIT_BAR_EN_V1_8197F BIT(6)

#define BIT_SHIFT_BAR_PKTNUM_TH_V1_8197F 0
#define BIT_MASK_BAR_PKTNUM_TH_V1_8197F 0x3f
#define BIT_BAR_PKTNUM_TH_V1_8197F(x)                                          \
	(((x) & BIT_MASK_BAR_PKTNUM_TH_V1_8197F)                               \
	 << BIT_SHIFT_BAR_PKTNUM_TH_V1_8197F)
#define BITS_BAR_PKTNUM_TH_V1_8197F                                            \
	(BIT_MASK_BAR_PKTNUM_TH_V1_8197F << BIT_SHIFT_BAR_PKTNUM_TH_V1_8197F)
#define BIT_CLEAR_BAR_PKTNUM_TH_V1_8197F(x)                                    \
	((x) & (~BITS_BAR_PKTNUM_TH_V1_8197F))
#define BIT_GET_BAR_PKTNUM_TH_V1_8197F(x)                                      \
	(((x) >> BIT_SHIFT_BAR_PKTNUM_TH_V1_8197F) &                           \
	 BIT_MASK_BAR_PKTNUM_TH_V1_8197F)
#define BIT_SET_BAR_PKTNUM_TH_V1_8197F(x, v)                                   \
	(BIT_CLEAR_BAR_PKTNUM_TH_V1_8197F(x) | BIT_BAR_PKTNUM_TH_V1_8197F(v))

/* 2 REG_RA_TRY_RATE_AGG_LMT_8197F */

#define BIT_SHIFT_RA_TRY_RATE_AGG_LMT_V1_8197F 0
#define BIT_MASK_RA_TRY_RATE_AGG_LMT_V1_8197F 0x3f
#define BIT_RA_TRY_RATE_AGG_LMT_V1_8197F(x)                                    \
	(((x) & BIT_MASK_RA_TRY_RATE_AGG_LMT_V1_8197F)                         \
	 << BIT_SHIFT_RA_TRY_RATE_AGG_LMT_V1_8197F)
#define BITS_RA_TRY_RATE_AGG_LMT_V1_8197F                                      \
	(BIT_MASK_RA_TRY_RATE_AGG_LMT_V1_8197F                                 \
	 << BIT_SHIFT_RA_TRY_RATE_AGG_LMT_V1_8197F)
#define BIT_CLEAR_RA_TRY_RATE_AGG_LMT_V1_8197F(x)                              \
	((x) & (~BITS_RA_TRY_RATE_AGG_LMT_V1_8197F))
#define BIT_GET_RA_TRY_RATE_AGG_LMT_V1_8197F(x)                                \
	(((x) >> BIT_SHIFT_RA_TRY_RATE_AGG_LMT_V1_8197F) &                     \
	 BIT_MASK_RA_TRY_RATE_AGG_LMT_V1_8197F)
#define BIT_SET_RA_TRY_RATE_AGG_LMT_V1_8197F(x, v)                             \
	(BIT_CLEAR_RA_TRY_RATE_AGG_LMT_V1_8197F(x) |                           \
	 BIT_RA_TRY_RATE_AGG_LMT_V1_8197F(v))

/* 2 REG_MACID_SLEEP2_8197F */

#define BIT_SHIFT_MACID95_64PKTSLEEP_8197F 0
#define BIT_MASK_MACID95_64PKTSLEEP_8197F 0xffffffffL
#define BIT_MACID95_64PKTSLEEP_8197F(x)                                        \
	(((x) & BIT_MASK_MACID95_64PKTSLEEP_8197F)                             \
	 << BIT_SHIFT_MACID95_64PKTSLEEP_8197F)
#define BITS_MACID95_64PKTSLEEP_8197F                                          \
	(BIT_MASK_MACID95_64PKTSLEEP_8197F                                     \
	 << BIT_SHIFT_MACID95_64PKTSLEEP_8197F)
#define BIT_CLEAR_MACID95_64PKTSLEEP_8197F(x)                                  \
	((x) & (~BITS_MACID95_64PKTSLEEP_8197F))
#define BIT_GET_MACID95_64PKTSLEEP_8197F(x)                                    \
	(((x) >> BIT_SHIFT_MACID95_64PKTSLEEP_8197F) &                         \
	 BIT_MASK_MACID95_64PKTSLEEP_8197F)
#define BIT_SET_MACID95_64PKTSLEEP_8197F(x, v)                                 \
	(BIT_CLEAR_MACID95_64PKTSLEEP_8197F(x) |                               \
	 BIT_MACID95_64PKTSLEEP_8197F(v))

/* 2 REG_MACID_SLEEP_8197F */

#define BIT_SHIFT_MACID31_0_PKTSLEEP_8197F 0
#define BIT_MASK_MACID31_0_PKTSLEEP_8197F 0xffffffffL
#define BIT_MACID31_0_PKTSLEEP_8197F(x)                                        \
	(((x) & BIT_MASK_MACID31_0_PKTSLEEP_8197F)                             \
	 << BIT_SHIFT_MACID31_0_PKTSLEEP_8197F)
#define BITS_MACID31_0_PKTSLEEP_8197F                                          \
	(BIT_MASK_MACID31_0_PKTSLEEP_8197F                                     \
	 << BIT_SHIFT_MACID31_0_PKTSLEEP_8197F)
#define BIT_CLEAR_MACID31_0_PKTSLEEP_8197F(x)                                  \
	((x) & (~BITS_MACID31_0_PKTSLEEP_8197F))
#define BIT_GET_MACID31_0_PKTSLEEP_8197F(x)                                    \
	(((x) >> BIT_SHIFT_MACID31_0_PKTSLEEP_8197F) &                         \
	 BIT_MASK_MACID31_0_PKTSLEEP_8197F)
#define BIT_SET_MACID31_0_PKTSLEEP_8197F(x, v)                                 \
	(BIT_CLEAR_MACID31_0_PKTSLEEP_8197F(x) |                               \
	 BIT_MACID31_0_PKTSLEEP_8197F(v))

/* 2 REG_HW_SEQ0_8197F */

#define BIT_SHIFT_HW_SSN_SEQ0_8197F 0
#define BIT_MASK_HW_SSN_SEQ0_8197F 0xfff
#define BIT_HW_SSN_SEQ0_8197F(x)                                               \
	(((x) & BIT_MASK_HW_SSN_SEQ0_8197F) << BIT_SHIFT_HW_SSN_SEQ0_8197F)
#define BITS_HW_SSN_SEQ0_8197F                                                 \
	(BIT_MASK_HW_SSN_SEQ0_8197F << BIT_SHIFT_HW_SSN_SEQ0_8197F)
#define BIT_CLEAR_HW_SSN_SEQ0_8197F(x) ((x) & (~BITS_HW_SSN_SEQ0_8197F))
#define BIT_GET_HW_SSN_SEQ0_8197F(x)                                           \
	(((x) >> BIT_SHIFT_HW_SSN_SEQ0_8197F) & BIT_MASK_HW_SSN_SEQ0_8197F)
#define BIT_SET_HW_SSN_SEQ0_8197F(x, v)                                        \
	(BIT_CLEAR_HW_SSN_SEQ0_8197F(x) | BIT_HW_SSN_SEQ0_8197F(v))

/* 2 REG_HW_SEQ1_8197F */

#define BIT_SHIFT_HW_SSN_SEQ1_8197F 0
#define BIT_MASK_HW_SSN_SEQ1_8197F 0xfff
#define BIT_HW_SSN_SEQ1_8197F(x)                                               \
	(((x) & BIT_MASK_HW_SSN_SEQ1_8197F) << BIT_SHIFT_HW_SSN_SEQ1_8197F)
#define BITS_HW_SSN_SEQ1_8197F                                                 \
	(BIT_MASK_HW_SSN_SEQ1_8197F << BIT_SHIFT_HW_SSN_SEQ1_8197F)
#define BIT_CLEAR_HW_SSN_SEQ1_8197F(x) ((x) & (~BITS_HW_SSN_SEQ1_8197F))
#define BIT_GET_HW_SSN_SEQ1_8197F(x)                                           \
	(((x) >> BIT_SHIFT_HW_SSN_SEQ1_8197F) & BIT_MASK_HW_SSN_SEQ1_8197F)
#define BIT_SET_HW_SSN_SEQ1_8197F(x, v)                                        \
	(BIT_CLEAR_HW_SSN_SEQ1_8197F(x) | BIT_HW_SSN_SEQ1_8197F(v))

/* 2 REG_HW_SEQ2_8197F */

#define BIT_SHIFT_HW_SSN_SEQ2_8197F 0
#define BIT_MASK_HW_SSN_SEQ2_8197F 0xfff
#define BIT_HW_SSN_SEQ2_8197F(x)                                               \
	(((x) & BIT_MASK_HW_SSN_SEQ2_8197F) << BIT_SHIFT_HW_SSN_SEQ2_8197F)
#define BITS_HW_SSN_SEQ2_8197F                                                 \
	(BIT_MASK_HW_SSN_SEQ2_8197F << BIT_SHIFT_HW_SSN_SEQ2_8197F)
#define BIT_CLEAR_HW_SSN_SEQ2_8197F(x) ((x) & (~BITS_HW_SSN_SEQ2_8197F))
#define BIT_GET_HW_SSN_SEQ2_8197F(x)                                           \
	(((x) >> BIT_SHIFT_HW_SSN_SEQ2_8197F) & BIT_MASK_HW_SSN_SEQ2_8197F)
#define BIT_SET_HW_SSN_SEQ2_8197F(x, v)                                        \
	(BIT_CLEAR_HW_SSN_SEQ2_8197F(x) | BIT_HW_SSN_SEQ2_8197F(v))

/* 2 REG_HW_SEQ3_8197F */

#define BIT_SHIFT_CSI_HWSSN_SEL_8197F 12
#define BIT_MASK_CSI_HWSSN_SEL_8197F 0x3
#define BIT_CSI_HWSSN_SEL_8197F(x)                                             \
	(((x) & BIT_MASK_CSI_HWSSN_SEL_8197F) << BIT_SHIFT_CSI_HWSSN_SEL_8197F)
#define BITS_CSI_HWSSN_SEL_8197F                                               \
	(BIT_MASK_CSI_HWSSN_SEL_8197F << BIT_SHIFT_CSI_HWSSN_SEL_8197F)
#define BIT_CLEAR_CSI_HWSSN_SEL_8197F(x) ((x) & (~BITS_CSI_HWSSN_SEL_8197F))
#define BIT_GET_CSI_HWSSN_SEL_8197F(x)                                         \
	(((x) >> BIT_SHIFT_CSI_HWSSN_SEL_8197F) & BIT_MASK_CSI_HWSSN_SEL_8197F)
#define BIT_SET_CSI_HWSSN_SEL_8197F(x, v)                                      \
	(BIT_CLEAR_CSI_HWSSN_SEL_8197F(x) | BIT_CSI_HWSSN_SEL_8197F(v))

#define BIT_SHIFT_HW_SSN_SEQ3_8197F 0
#define BIT_MASK_HW_SSN_SEQ3_8197F 0xfff
#define BIT_HW_SSN_SEQ3_8197F(x)                                               \
	(((x) & BIT_MASK_HW_SSN_SEQ3_8197F) << BIT_SHIFT_HW_SSN_SEQ3_8197F)
#define BITS_HW_SSN_SEQ3_8197F                                                 \
	(BIT_MASK_HW_SSN_SEQ3_8197F << BIT_SHIFT_HW_SSN_SEQ3_8197F)
#define BIT_CLEAR_HW_SSN_SEQ3_8197F(x) ((x) & (~BITS_HW_SSN_SEQ3_8197F))
#define BIT_GET_HW_SSN_SEQ3_8197F(x)                                           \
	(((x) >> BIT_SHIFT_HW_SSN_SEQ3_8197F) & BIT_MASK_HW_SSN_SEQ3_8197F)
#define BIT_SET_HW_SSN_SEQ3_8197F(x, v)                                        \
	(BIT_CLEAR_HW_SSN_SEQ3_8197F(x) | BIT_HW_SSN_SEQ3_8197F(v))

/* 2 REG_NULL_PKT_STATUS_V1_8197F */

#define BIT_SHIFT_PTCL_TOTAL_PG_V1_8197F 2
#define BIT_MASK_PTCL_TOTAL_PG_V1_8197F 0x1fff
#define BIT_PTCL_TOTAL_PG_V1_8197F(x)                                          \
	(((x) & BIT_MASK_PTCL_TOTAL_PG_V1_8197F)                               \
	 << BIT_SHIFT_PTCL_TOTAL_PG_V1_8197F)
#define BITS_PTCL_TOTAL_PG_V1_8197F                                            \
	(BIT_MASK_PTCL_TOTAL_PG_V1_8197F << BIT_SHIFT_PTCL_TOTAL_PG_V1_8197F)
#define BIT_CLEAR_PTCL_TOTAL_PG_V1_8197F(x)                                    \
	((x) & (~BITS_PTCL_TOTAL_PG_V1_8197F))
#define BIT_GET_PTCL_TOTAL_PG_V1_8197F(x)                                      \
	(((x) >> BIT_SHIFT_PTCL_TOTAL_PG_V1_8197F) &                           \
	 BIT_MASK_PTCL_TOTAL_PG_V1_8197F)
#define BIT_SET_PTCL_TOTAL_PG_V1_8197F(x, v)                                   \
	(BIT_CLEAR_PTCL_TOTAL_PG_V1_8197F(x) | BIT_PTCL_TOTAL_PG_V1_8197F(v))

#define BIT_TX_NULL_1_8197F BIT(1)
#define BIT_TX_NULL_0_8197F BIT(0)

/* 2 REG_PTCL_ERR_STATUS_8197F */
#define BIT_PTCL_RATE_TABLE_INVALID_8197F BIT(7)
#define BIT_FTM_T2R_ERROR_8197F BIT(6)
#define BIT_PTCL_ERR0_8197F BIT(5)
#define BIT_PTCL_ERR1_8197F BIT(4)
#define BIT_PTCL_ERR2_8197F BIT(3)
#define BIT_PTCL_ERR3_8197F BIT(2)
#define BIT_PTCL_ERR4_8197F BIT(1)
#define BIT_PTCL_ERR5_8197F BIT(0)

/* 2 REG_NULL_PKT_STATUS_EXTEND_8197F */
#define BIT_CLI3_TX_NULL_1_8197F BIT(7)
#define BIT_CLI3_TX_NULL_0_8197F BIT(6)
#define BIT_CLI2_TX_NULL_1_8197F BIT(5)
#define BIT_CLI2_TX_NULL_0_8197F BIT(4)
#define BIT_CLI1_TX_NULL_1_8197F BIT(3)
#define BIT_CLI1_TX_NULL_0_8197F BIT(2)
#define BIT_CLI0_TX_NULL_1_8197F BIT(1)
#define BIT_CLI0_TX_NULL_0_8197F BIT(0)

/* 2 REG_VIDEO_ENHANCEMENT_FUN_8197F */
#define BIT_VIDEO_JUST_DROP_8197F BIT(1)
#define BIT_VIDEO_ENHANCEMENT_FUN_EN_8197F BIT(0)

/* 2 REG_BT_POLLUTE_PKT_CNT_8197F */

#define BIT_SHIFT_BT_POLLUTE_PKT_CNT_8197F 0
#define BIT_MASK_BT_POLLUTE_PKT_CNT_8197F 0xffff
#define BIT_BT_POLLUTE_PKT_CNT_8197F(x)                                        \
	(((x) & BIT_MASK_BT_POLLUTE_PKT_CNT_8197F)                             \
	 << BIT_SHIFT_BT_POLLUTE_PKT_CNT_8197F)
#define BITS_BT_POLLUTE_PKT_CNT_8197F                                          \
	(BIT_MASK_BT_POLLUTE_PKT_CNT_8197F                                     \
	 << BIT_SHIFT_BT_POLLUTE_PKT_CNT_8197F)
#define BIT_CLEAR_BT_POLLUTE_PKT_CNT_8197F(x)                                  \
	((x) & (~BITS_BT_POLLUTE_PKT_CNT_8197F))
#define BIT_GET_BT_POLLUTE_PKT_CNT_8197F(x)                                    \
	(((x) >> BIT_SHIFT_BT_POLLUTE_PKT_CNT_8197F) &                         \
	 BIT_MASK_BT_POLLUTE_PKT_CNT_8197F)
#define BIT_SET_BT_POLLUTE_PKT_CNT_8197F(x, v)                                 \
	(BIT_CLEAR_BT_POLLUTE_PKT_CNT_8197F(x) |                               \
	 BIT_BT_POLLUTE_PKT_CNT_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_PTCL_DBG_8197F */

#define BIT_SHIFT_PTCL_DBG_8197F 0
#define BIT_MASK_PTCL_DBG_8197F 0xffffffffL
#define BIT_PTCL_DBG_8197F(x)                                                  \
	(((x) & BIT_MASK_PTCL_DBG_8197F) << BIT_SHIFT_PTCL_DBG_8197F)
#define BITS_PTCL_DBG_8197F                                                    \
	(BIT_MASK_PTCL_DBG_8197F << BIT_SHIFT_PTCL_DBG_8197F)
#define BIT_CLEAR_PTCL_DBG_8197F(x) ((x) & (~BITS_PTCL_DBG_8197F))
#define BIT_GET_PTCL_DBG_8197F(x)                                              \
	(((x) >> BIT_SHIFT_PTCL_DBG_8197F) & BIT_MASK_PTCL_DBG_8197F)
#define BIT_SET_PTCL_DBG_8197F(x, v)                                           \
	(BIT_CLEAR_PTCL_DBG_8197F(x) | BIT_PTCL_DBG_8197F(v))

/* 2 REG_TXOP_EXTRA_CTRL_8197F */
#define BIT_TXOP_EFFICIENCY_EN_8197F BIT(0)

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_CPUMGQ_TIMER_CTRL2_8197F */

#define BIT_SHIFT_TRI_HEAD_ADDR_8197F 16
#define BIT_MASK_TRI_HEAD_ADDR_8197F 0xfff
#define BIT_TRI_HEAD_ADDR_8197F(x)                                             \
	(((x) & BIT_MASK_TRI_HEAD_ADDR_8197F) << BIT_SHIFT_TRI_HEAD_ADDR_8197F)
#define BITS_TRI_HEAD_ADDR_8197F                                               \
	(BIT_MASK_TRI_HEAD_ADDR_8197F << BIT_SHIFT_TRI_HEAD_ADDR_8197F)
#define BIT_CLEAR_TRI_HEAD_ADDR_8197F(x) ((x) & (~BITS_TRI_HEAD_ADDR_8197F))
#define BIT_GET_TRI_HEAD_ADDR_8197F(x)                                         \
	(((x) >> BIT_SHIFT_TRI_HEAD_ADDR_8197F) & BIT_MASK_TRI_HEAD_ADDR_8197F)
#define BIT_SET_TRI_HEAD_ADDR_8197F(x, v)                                      \
	(BIT_CLEAR_TRI_HEAD_ADDR_8197F(x) | BIT_TRI_HEAD_ADDR_8197F(v))

#define BIT_DROP_TH_EN_8197F BIT(8)

#define BIT_SHIFT_DROP_TH_8197F 0
#define BIT_MASK_DROP_TH_8197F 0xff
#define BIT_DROP_TH_8197F(x)                                                   \
	(((x) & BIT_MASK_DROP_TH_8197F) << BIT_SHIFT_DROP_TH_8197F)
#define BITS_DROP_TH_8197F (BIT_MASK_DROP_TH_8197F << BIT_SHIFT_DROP_TH_8197F)
#define BIT_CLEAR_DROP_TH_8197F(x) ((x) & (~BITS_DROP_TH_8197F))
#define BIT_GET_DROP_TH_8197F(x)                                               \
	(((x) >> BIT_SHIFT_DROP_TH_8197F) & BIT_MASK_DROP_TH_8197F)
#define BIT_SET_DROP_TH_8197F(x, v)                                            \
	(BIT_CLEAR_DROP_TH_8197F(x) | BIT_DROP_TH_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_DUMMY_PAGE4_8197F */
#define BIT_MOREDATA_CTRL2_EN_V2_8197F BIT(19)
#define BIT_MOREDATA_CTRL1_EN_V2_8197F BIT(18)
#define BIT_PKTIN_MOREDATA_REPLACE_ENABLE_8197F BIT(16)

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_Q0_Q1_INFO_8197F */
#define BIT_QUEUE_MACID_AC_NOT_THE_SAME_8197F BIT(31)

#define BIT_SHIFT_GTAB_ID_8197F 28
#define BIT_MASK_GTAB_ID_8197F 0x7
#define BIT_GTAB_ID_8197F(x)                                                   \
	(((x) & BIT_MASK_GTAB_ID_8197F) << BIT_SHIFT_GTAB_ID_8197F)
#define BITS_GTAB_ID_8197F (BIT_MASK_GTAB_ID_8197F << BIT_SHIFT_GTAB_ID_8197F)
#define BIT_CLEAR_GTAB_ID_8197F(x) ((x) & (~BITS_GTAB_ID_8197F))
#define BIT_GET_GTAB_ID_8197F(x)                                               \
	(((x) >> BIT_SHIFT_GTAB_ID_8197F) & BIT_MASK_GTAB_ID_8197F)
#define BIT_SET_GTAB_ID_8197F(x, v)                                            \
	(BIT_CLEAR_GTAB_ID_8197F(x) | BIT_GTAB_ID_8197F(v))

#define BIT_SHIFT_AC1_PKT_INFO_8197F 16
#define BIT_MASK_AC1_PKT_INFO_8197F 0xfff
#define BIT_AC1_PKT_INFO_8197F(x)                                              \
	(((x) & BIT_MASK_AC1_PKT_INFO_8197F) << BIT_SHIFT_AC1_PKT_INFO_8197F)
#define BITS_AC1_PKT_INFO_8197F                                                \
	(BIT_MASK_AC1_PKT_INFO_8197F << BIT_SHIFT_AC1_PKT_INFO_8197F)
#define BIT_CLEAR_AC1_PKT_INFO_8197F(x) ((x) & (~BITS_AC1_PKT_INFO_8197F))
#define BIT_GET_AC1_PKT_INFO_8197F(x)                                          \
	(((x) >> BIT_SHIFT_AC1_PKT_INFO_8197F) & BIT_MASK_AC1_PKT_INFO_8197F)
#define BIT_SET_AC1_PKT_INFO_8197F(x, v)                                       \
	(BIT_CLEAR_AC1_PKT_INFO_8197F(x) | BIT_AC1_PKT_INFO_8197F(v))

#define BIT_QUEUE_MACID_AC_NOT_THE_SAME_V1_8197F BIT(15)

#define BIT_SHIFT_GTAB_ID_V1_8197F 12
#define BIT_MASK_GTAB_ID_V1_8197F 0x7
#define BIT_GTAB_ID_V1_8197F(x)                                                \
	(((x) & BIT_MASK_GTAB_ID_V1_8197F) << BIT_SHIFT_GTAB_ID_V1_8197F)
#define BITS_GTAB_ID_V1_8197F                                                  \
	(BIT_MASK_GTAB_ID_V1_8197F << BIT_SHIFT_GTAB_ID_V1_8197F)
#define BIT_CLEAR_GTAB_ID_V1_8197F(x) ((x) & (~BITS_GTAB_ID_V1_8197F))
#define BIT_GET_GTAB_ID_V1_8197F(x)                                            \
	(((x) >> BIT_SHIFT_GTAB_ID_V1_8197F) & BIT_MASK_GTAB_ID_V1_8197F)
#define BIT_SET_GTAB_ID_V1_8197F(x, v)                                         \
	(BIT_CLEAR_GTAB_ID_V1_8197F(x) | BIT_GTAB_ID_V1_8197F(v))

#define BIT_SHIFT_AC0_PKT_INFO_8197F 0
#define BIT_MASK_AC0_PKT_INFO_8197F 0xfff
#define BIT_AC0_PKT_INFO_8197F(x)                                              \
	(((x) & BIT_MASK_AC0_PKT_INFO_8197F) << BIT_SHIFT_AC0_PKT_INFO_8197F)
#define BITS_AC0_PKT_INFO_8197F                                                \
	(BIT_MASK_AC0_PKT_INFO_8197F << BIT_SHIFT_AC0_PKT_INFO_8197F)
#define BIT_CLEAR_AC0_PKT_INFO_8197F(x) ((x) & (~BITS_AC0_PKT_INFO_8197F))
#define BIT_GET_AC0_PKT_INFO_8197F(x)                                          \
	(((x) >> BIT_SHIFT_AC0_PKT_INFO_8197F) & BIT_MASK_AC0_PKT_INFO_8197F)
#define BIT_SET_AC0_PKT_INFO_8197F(x, v)                                       \
	(BIT_CLEAR_AC0_PKT_INFO_8197F(x) | BIT_AC0_PKT_INFO_8197F(v))

/* 2 REG_Q2_Q3_INFO_8197F */
#define BIT_QUEUE_MACID_AC_NOT_THE_SAME_8197F BIT(31)

#define BIT_SHIFT_GTAB_ID_8197F 28
#define BIT_MASK_GTAB_ID_8197F 0x7
#define BIT_GTAB_ID_8197F(x)                                                   \
	(((x) & BIT_MASK_GTAB_ID_8197F) << BIT_SHIFT_GTAB_ID_8197F)
#define BITS_GTAB_ID_8197F (BIT_MASK_GTAB_ID_8197F << BIT_SHIFT_GTAB_ID_8197F)
#define BIT_CLEAR_GTAB_ID_8197F(x) ((x) & (~BITS_GTAB_ID_8197F))
#define BIT_GET_GTAB_ID_8197F(x)                                               \
	(((x) >> BIT_SHIFT_GTAB_ID_8197F) & BIT_MASK_GTAB_ID_8197F)
#define BIT_SET_GTAB_ID_8197F(x, v)                                            \
	(BIT_CLEAR_GTAB_ID_8197F(x) | BIT_GTAB_ID_8197F(v))

#define BIT_SHIFT_AC3_PKT_INFO_8197F 16
#define BIT_MASK_AC3_PKT_INFO_8197F 0xfff
#define BIT_AC3_PKT_INFO_8197F(x)                                              \
	(((x) & BIT_MASK_AC3_PKT_INFO_8197F) << BIT_SHIFT_AC3_PKT_INFO_8197F)
#define BITS_AC3_PKT_INFO_8197F                                                \
	(BIT_MASK_AC3_PKT_INFO_8197F << BIT_SHIFT_AC3_PKT_INFO_8197F)
#define BIT_CLEAR_AC3_PKT_INFO_8197F(x) ((x) & (~BITS_AC3_PKT_INFO_8197F))
#define BIT_GET_AC3_PKT_INFO_8197F(x)                                          \
	(((x) >> BIT_SHIFT_AC3_PKT_INFO_8197F) & BIT_MASK_AC3_PKT_INFO_8197F)
#define BIT_SET_AC3_PKT_INFO_8197F(x, v)                                       \
	(BIT_CLEAR_AC3_PKT_INFO_8197F(x) | BIT_AC3_PKT_INFO_8197F(v))

#define BIT_QUEUE_MACID_AC_NOT_THE_SAME_V1_8197F BIT(15)

#define BIT_SHIFT_GTAB_ID_V1_8197F 12
#define BIT_MASK_GTAB_ID_V1_8197F 0x7
#define BIT_GTAB_ID_V1_8197F(x)                                                \
	(((x) & BIT_MASK_GTAB_ID_V1_8197F) << BIT_SHIFT_GTAB_ID_V1_8197F)
#define BITS_GTAB_ID_V1_8197F                                                  \
	(BIT_MASK_GTAB_ID_V1_8197F << BIT_SHIFT_GTAB_ID_V1_8197F)
#define BIT_CLEAR_GTAB_ID_V1_8197F(x) ((x) & (~BITS_GTAB_ID_V1_8197F))
#define BIT_GET_GTAB_ID_V1_8197F(x)                                            \
	(((x) >> BIT_SHIFT_GTAB_ID_V1_8197F) & BIT_MASK_GTAB_ID_V1_8197F)
#define BIT_SET_GTAB_ID_V1_8197F(x, v)                                         \
	(BIT_CLEAR_GTAB_ID_V1_8197F(x) | BIT_GTAB_ID_V1_8197F(v))

#define BIT_SHIFT_AC2_PKT_INFO_8197F 0
#define BIT_MASK_AC2_PKT_INFO_8197F 0xfff
#define BIT_AC2_PKT_INFO_8197F(x)                                              \
	(((x) & BIT_MASK_AC2_PKT_INFO_8197F) << BIT_SHIFT_AC2_PKT_INFO_8197F)
#define BITS_AC2_PKT_INFO_8197F                                                \
	(BIT_MASK_AC2_PKT_INFO_8197F << BIT_SHIFT_AC2_PKT_INFO_8197F)
#define BIT_CLEAR_AC2_PKT_INFO_8197F(x) ((x) & (~BITS_AC2_PKT_INFO_8197F))
#define BIT_GET_AC2_PKT_INFO_8197F(x)                                          \
	(((x) >> BIT_SHIFT_AC2_PKT_INFO_8197F) & BIT_MASK_AC2_PKT_INFO_8197F)
#define BIT_SET_AC2_PKT_INFO_8197F(x, v)                                       \
	(BIT_CLEAR_AC2_PKT_INFO_8197F(x) | BIT_AC2_PKT_INFO_8197F(v))

/* 2 REG_Q4_Q5_INFO_8197F */
#define BIT_QUEUE_MACID_AC_NOT_THE_SAME_8197F BIT(31)

#define BIT_SHIFT_GTAB_ID_8197F 28
#define BIT_MASK_GTAB_ID_8197F 0x7
#define BIT_GTAB_ID_8197F(x)                                                   \
	(((x) & BIT_MASK_GTAB_ID_8197F) << BIT_SHIFT_GTAB_ID_8197F)
#define BITS_GTAB_ID_8197F (BIT_MASK_GTAB_ID_8197F << BIT_SHIFT_GTAB_ID_8197F)
#define BIT_CLEAR_GTAB_ID_8197F(x) ((x) & (~BITS_GTAB_ID_8197F))
#define BIT_GET_GTAB_ID_8197F(x)                                               \
	(((x) >> BIT_SHIFT_GTAB_ID_8197F) & BIT_MASK_GTAB_ID_8197F)
#define BIT_SET_GTAB_ID_8197F(x, v)                                            \
	(BIT_CLEAR_GTAB_ID_8197F(x) | BIT_GTAB_ID_8197F(v))

#define BIT_SHIFT_AC5_PKT_INFO_8197F 16
#define BIT_MASK_AC5_PKT_INFO_8197F 0xfff
#define BIT_AC5_PKT_INFO_8197F(x)                                              \
	(((x) & BIT_MASK_AC5_PKT_INFO_8197F) << BIT_SHIFT_AC5_PKT_INFO_8197F)
#define BITS_AC5_PKT_INFO_8197F                                                \
	(BIT_MASK_AC5_PKT_INFO_8197F << BIT_SHIFT_AC5_PKT_INFO_8197F)
#define BIT_CLEAR_AC5_PKT_INFO_8197F(x) ((x) & (~BITS_AC5_PKT_INFO_8197F))
#define BIT_GET_AC5_PKT_INFO_8197F(x)                                          \
	(((x) >> BIT_SHIFT_AC5_PKT_INFO_8197F) & BIT_MASK_AC5_PKT_INFO_8197F)
#define BIT_SET_AC5_PKT_INFO_8197F(x, v)                                       \
	(BIT_CLEAR_AC5_PKT_INFO_8197F(x) | BIT_AC5_PKT_INFO_8197F(v))

#define BIT_QUEUE_MACID_AC_NOT_THE_SAME_V1_8197F BIT(15)

#define BIT_SHIFT_GTAB_ID_V1_8197F 12
#define BIT_MASK_GTAB_ID_V1_8197F 0x7
#define BIT_GTAB_ID_V1_8197F(x)                                                \
	(((x) & BIT_MASK_GTAB_ID_V1_8197F) << BIT_SHIFT_GTAB_ID_V1_8197F)
#define BITS_GTAB_ID_V1_8197F                                                  \
	(BIT_MASK_GTAB_ID_V1_8197F << BIT_SHIFT_GTAB_ID_V1_8197F)
#define BIT_CLEAR_GTAB_ID_V1_8197F(x) ((x) & (~BITS_GTAB_ID_V1_8197F))
#define BIT_GET_GTAB_ID_V1_8197F(x)                                            \
	(((x) >> BIT_SHIFT_GTAB_ID_V1_8197F) & BIT_MASK_GTAB_ID_V1_8197F)
#define BIT_SET_GTAB_ID_V1_8197F(x, v)                                         \
	(BIT_CLEAR_GTAB_ID_V1_8197F(x) | BIT_GTAB_ID_V1_8197F(v))

#define BIT_SHIFT_AC4_PKT_INFO_8197F 0
#define BIT_MASK_AC4_PKT_INFO_8197F 0xfff
#define BIT_AC4_PKT_INFO_8197F(x)                                              \
	(((x) & BIT_MASK_AC4_PKT_INFO_8197F) << BIT_SHIFT_AC4_PKT_INFO_8197F)
#define BITS_AC4_PKT_INFO_8197F                                                \
	(BIT_MASK_AC4_PKT_INFO_8197F << BIT_SHIFT_AC4_PKT_INFO_8197F)
#define BIT_CLEAR_AC4_PKT_INFO_8197F(x) ((x) & (~BITS_AC4_PKT_INFO_8197F))
#define BIT_GET_AC4_PKT_INFO_8197F(x)                                          \
	(((x) >> BIT_SHIFT_AC4_PKT_INFO_8197F) & BIT_MASK_AC4_PKT_INFO_8197F)
#define BIT_SET_AC4_PKT_INFO_8197F(x, v)                                       \
	(BIT_CLEAR_AC4_PKT_INFO_8197F(x) | BIT_AC4_PKT_INFO_8197F(v))

/* 2 REG_Q6_Q7_INFO_8197F */
#define BIT_QUEUE_MACID_AC_NOT_THE_SAME_8197F BIT(31)

#define BIT_SHIFT_GTAB_ID_8197F 28
#define BIT_MASK_GTAB_ID_8197F 0x7
#define BIT_GTAB_ID_8197F(x)                                                   \
	(((x) & BIT_MASK_GTAB_ID_8197F) << BIT_SHIFT_GTAB_ID_8197F)
#define BITS_GTAB_ID_8197F (BIT_MASK_GTAB_ID_8197F << BIT_SHIFT_GTAB_ID_8197F)
#define BIT_CLEAR_GTAB_ID_8197F(x) ((x) & (~BITS_GTAB_ID_8197F))
#define BIT_GET_GTAB_ID_8197F(x)                                               \
	(((x) >> BIT_SHIFT_GTAB_ID_8197F) & BIT_MASK_GTAB_ID_8197F)
#define BIT_SET_GTAB_ID_8197F(x, v)                                            \
	(BIT_CLEAR_GTAB_ID_8197F(x) | BIT_GTAB_ID_8197F(v))

#define BIT_SHIFT_AC7_PKT_INFO_8197F 16
#define BIT_MASK_AC7_PKT_INFO_8197F 0xfff
#define BIT_AC7_PKT_INFO_8197F(x)                                              \
	(((x) & BIT_MASK_AC7_PKT_INFO_8197F) << BIT_SHIFT_AC7_PKT_INFO_8197F)
#define BITS_AC7_PKT_INFO_8197F                                                \
	(BIT_MASK_AC7_PKT_INFO_8197F << BIT_SHIFT_AC7_PKT_INFO_8197F)
#define BIT_CLEAR_AC7_PKT_INFO_8197F(x) ((x) & (~BITS_AC7_PKT_INFO_8197F))
#define BIT_GET_AC7_PKT_INFO_8197F(x)                                          \
	(((x) >> BIT_SHIFT_AC7_PKT_INFO_8197F) & BIT_MASK_AC7_PKT_INFO_8197F)
#define BIT_SET_AC7_PKT_INFO_8197F(x, v)                                       \
	(BIT_CLEAR_AC7_PKT_INFO_8197F(x) | BIT_AC7_PKT_INFO_8197F(v))

#define BIT_QUEUE_MACID_AC_NOT_THE_SAME_V1_8197F BIT(15)

#define BIT_SHIFT_GTAB_ID_V1_8197F 12
#define BIT_MASK_GTAB_ID_V1_8197F 0x7
#define BIT_GTAB_ID_V1_8197F(x)                                                \
	(((x) & BIT_MASK_GTAB_ID_V1_8197F) << BIT_SHIFT_GTAB_ID_V1_8197F)
#define BITS_GTAB_ID_V1_8197F                                                  \
	(BIT_MASK_GTAB_ID_V1_8197F << BIT_SHIFT_GTAB_ID_V1_8197F)
#define BIT_CLEAR_GTAB_ID_V1_8197F(x) ((x) & (~BITS_GTAB_ID_V1_8197F))
#define BIT_GET_GTAB_ID_V1_8197F(x)                                            \
	(((x) >> BIT_SHIFT_GTAB_ID_V1_8197F) & BIT_MASK_GTAB_ID_V1_8197F)
#define BIT_SET_GTAB_ID_V1_8197F(x, v)                                         \
	(BIT_CLEAR_GTAB_ID_V1_8197F(x) | BIT_GTAB_ID_V1_8197F(v))

#define BIT_SHIFT_AC6_PKT_INFO_8197F 0
#define BIT_MASK_AC6_PKT_INFO_8197F 0xfff
#define BIT_AC6_PKT_INFO_8197F(x)                                              \
	(((x) & BIT_MASK_AC6_PKT_INFO_8197F) << BIT_SHIFT_AC6_PKT_INFO_8197F)
#define BITS_AC6_PKT_INFO_8197F                                                \
	(BIT_MASK_AC6_PKT_INFO_8197F << BIT_SHIFT_AC6_PKT_INFO_8197F)
#define BIT_CLEAR_AC6_PKT_INFO_8197F(x) ((x) & (~BITS_AC6_PKT_INFO_8197F))
#define BIT_GET_AC6_PKT_INFO_8197F(x)                                          \
	(((x) >> BIT_SHIFT_AC6_PKT_INFO_8197F) & BIT_MASK_AC6_PKT_INFO_8197F)
#define BIT_SET_AC6_PKT_INFO_8197F(x, v)                                       \
	(BIT_CLEAR_AC6_PKT_INFO_8197F(x) | BIT_AC6_PKT_INFO_8197F(v))

/* 2 REG_MGQ_HIQ_INFO_8197F */

#define BIT_SHIFT_HIQ_PKT_INFO_8197F 16
#define BIT_MASK_HIQ_PKT_INFO_8197F 0xfff
#define BIT_HIQ_PKT_INFO_8197F(x)                                              \
	(((x) & BIT_MASK_HIQ_PKT_INFO_8197F) << BIT_SHIFT_HIQ_PKT_INFO_8197F)
#define BITS_HIQ_PKT_INFO_8197F                                                \
	(BIT_MASK_HIQ_PKT_INFO_8197F << BIT_SHIFT_HIQ_PKT_INFO_8197F)
#define BIT_CLEAR_HIQ_PKT_INFO_8197F(x) ((x) & (~BITS_HIQ_PKT_INFO_8197F))
#define BIT_GET_HIQ_PKT_INFO_8197F(x)                                          \
	(((x) >> BIT_SHIFT_HIQ_PKT_INFO_8197F) & BIT_MASK_HIQ_PKT_INFO_8197F)
#define BIT_SET_HIQ_PKT_INFO_8197F(x, v)                                       \
	(BIT_CLEAR_HIQ_PKT_INFO_8197F(x) | BIT_HIQ_PKT_INFO_8197F(v))

#define BIT_SHIFT_MGQ_PKT_INFO_8197F 0
#define BIT_MASK_MGQ_PKT_INFO_8197F 0xfff
#define BIT_MGQ_PKT_INFO_8197F(x)                                              \
	(((x) & BIT_MASK_MGQ_PKT_INFO_8197F) << BIT_SHIFT_MGQ_PKT_INFO_8197F)
#define BITS_MGQ_PKT_INFO_8197F                                                \
	(BIT_MASK_MGQ_PKT_INFO_8197F << BIT_SHIFT_MGQ_PKT_INFO_8197F)
#define BIT_CLEAR_MGQ_PKT_INFO_8197F(x) ((x) & (~BITS_MGQ_PKT_INFO_8197F))
#define BIT_GET_MGQ_PKT_INFO_8197F(x)                                          \
	(((x) >> BIT_SHIFT_MGQ_PKT_INFO_8197F) & BIT_MASK_MGQ_PKT_INFO_8197F)
#define BIT_SET_MGQ_PKT_INFO_8197F(x, v)                                       \
	(BIT_CLEAR_MGQ_PKT_INFO_8197F(x) | BIT_MGQ_PKT_INFO_8197F(v))

/* 2 REG_CMDQ_BCNQ_INFO_8197F */

#define BIT_SHIFT_BCNQ_PKT_INFO_V1_8197F 16
#define BIT_MASK_BCNQ_PKT_INFO_V1_8197F 0xfff
#define BIT_BCNQ_PKT_INFO_V1_8197F(x)                                          \
	(((x) & BIT_MASK_BCNQ_PKT_INFO_V1_8197F)                               \
	 << BIT_SHIFT_BCNQ_PKT_INFO_V1_8197F)
#define BITS_BCNQ_PKT_INFO_V1_8197F                                            \
	(BIT_MASK_BCNQ_PKT_INFO_V1_8197F << BIT_SHIFT_BCNQ_PKT_INFO_V1_8197F)
#define BIT_CLEAR_BCNQ_PKT_INFO_V1_8197F(x)                                    \
	((x) & (~BITS_BCNQ_PKT_INFO_V1_8197F))
#define BIT_GET_BCNQ_PKT_INFO_V1_8197F(x)                                      \
	(((x) >> BIT_SHIFT_BCNQ_PKT_INFO_V1_8197F) &                           \
	 BIT_MASK_BCNQ_PKT_INFO_V1_8197F)
#define BIT_SET_BCNQ_PKT_INFO_V1_8197F(x, v)                                   \
	(BIT_CLEAR_BCNQ_PKT_INFO_V1_8197F(x) | BIT_BCNQ_PKT_INFO_V1_8197F(v))

#define BIT_SHIFT_CMDQ_PKT_INFO_V1_8197F 0
#define BIT_MASK_CMDQ_PKT_INFO_V1_8197F 0xfff
#define BIT_CMDQ_PKT_INFO_V1_8197F(x)                                          \
	(((x) & BIT_MASK_CMDQ_PKT_INFO_V1_8197F)                               \
	 << BIT_SHIFT_CMDQ_PKT_INFO_V1_8197F)
#define BITS_CMDQ_PKT_INFO_V1_8197F                                            \
	(BIT_MASK_CMDQ_PKT_INFO_V1_8197F << BIT_SHIFT_CMDQ_PKT_INFO_V1_8197F)
#define BIT_CLEAR_CMDQ_PKT_INFO_V1_8197F(x)                                    \
	((x) & (~BITS_CMDQ_PKT_INFO_V1_8197F))
#define BIT_GET_CMDQ_PKT_INFO_V1_8197F(x)                                      \
	(((x) >> BIT_SHIFT_CMDQ_PKT_INFO_V1_8197F) &                           \
	 BIT_MASK_CMDQ_PKT_INFO_V1_8197F)
#define BIT_SET_CMDQ_PKT_INFO_V1_8197F(x, v)                                   \
	(BIT_CLEAR_CMDQ_PKT_INFO_V1_8197F(x) | BIT_CMDQ_PKT_INFO_V1_8197F(v))

/* 2 REG_USEREG_SETTING_8197F */
#define BIT_NDPA_USEREG_8197F BIT(21)

#define BIT_SHIFT_RETRY_USEREG_8197F 19
#define BIT_MASK_RETRY_USEREG_8197F 0x3
#define BIT_RETRY_USEREG_8197F(x)                                              \
	(((x) & BIT_MASK_RETRY_USEREG_8197F) << BIT_SHIFT_RETRY_USEREG_8197F)
#define BITS_RETRY_USEREG_8197F                                                \
	(BIT_MASK_RETRY_USEREG_8197F << BIT_SHIFT_RETRY_USEREG_8197F)
#define BIT_CLEAR_RETRY_USEREG_8197F(x) ((x) & (~BITS_RETRY_USEREG_8197F))
#define BIT_GET_RETRY_USEREG_8197F(x)                                          \
	(((x) >> BIT_SHIFT_RETRY_USEREG_8197F) & BIT_MASK_RETRY_USEREG_8197F)
#define BIT_SET_RETRY_USEREG_8197F(x, v)                                       \
	(BIT_CLEAR_RETRY_USEREG_8197F(x) | BIT_RETRY_USEREG_8197F(v))

#define BIT_SHIFT_TRYPKT_USEREG_8197F 17
#define BIT_MASK_TRYPKT_USEREG_8197F 0x3
#define BIT_TRYPKT_USEREG_8197F(x)                                             \
	(((x) & BIT_MASK_TRYPKT_USEREG_8197F) << BIT_SHIFT_TRYPKT_USEREG_8197F)
#define BITS_TRYPKT_USEREG_8197F                                               \
	(BIT_MASK_TRYPKT_USEREG_8197F << BIT_SHIFT_TRYPKT_USEREG_8197F)
#define BIT_CLEAR_TRYPKT_USEREG_8197F(x) ((x) & (~BITS_TRYPKT_USEREG_8197F))
#define BIT_GET_TRYPKT_USEREG_8197F(x)                                         \
	(((x) >> BIT_SHIFT_TRYPKT_USEREG_8197F) & BIT_MASK_TRYPKT_USEREG_8197F)
#define BIT_SET_TRYPKT_USEREG_8197F(x, v)                                      \
	(BIT_CLEAR_TRYPKT_USEREG_8197F(x) | BIT_TRYPKT_USEREG_8197F(v))

#define BIT_CTLPKT_USEREG_8197F BIT(16)

/* 2 REG_AESIV_SETTING_8197F */

#define BIT_SHIFT_AESIV_OFFSET_8197F 0
#define BIT_MASK_AESIV_OFFSET_8197F 0xfff
#define BIT_AESIV_OFFSET_8197F(x)                                              \
	(((x) & BIT_MASK_AESIV_OFFSET_8197F) << BIT_SHIFT_AESIV_OFFSET_8197F)
#define BITS_AESIV_OFFSET_8197F                                                \
	(BIT_MASK_AESIV_OFFSET_8197F << BIT_SHIFT_AESIV_OFFSET_8197F)
#define BIT_CLEAR_AESIV_OFFSET_8197F(x) ((x) & (~BITS_AESIV_OFFSET_8197F))
#define BIT_GET_AESIV_OFFSET_8197F(x)                                          \
	(((x) >> BIT_SHIFT_AESIV_OFFSET_8197F) & BIT_MASK_AESIV_OFFSET_8197F)
#define BIT_SET_AESIV_OFFSET_8197F(x, v)                                       \
	(BIT_CLEAR_AESIV_OFFSET_8197F(x) | BIT_AESIV_OFFSET_8197F(v))

/* 2 REG_BF0_TIME_SETTING_8197F */
#define BIT_BF0_TIMER_SET_8197F BIT(31)
#define BIT_BF0_TIMER_CLR_8197F BIT(30)
#define BIT_BF0_UPDATE_EN_8197F BIT(29)
#define BIT_BF0_TIMER_EN_8197F BIT(28)

#define BIT_SHIFT_BF0_PRETIME_OVER_8197F 16
#define BIT_MASK_BF0_PRETIME_OVER_8197F 0xfff
#define BIT_BF0_PRETIME_OVER_8197F(x)                                          \
	(((x) & BIT_MASK_BF0_PRETIME_OVER_8197F)                               \
	 << BIT_SHIFT_BF0_PRETIME_OVER_8197F)
#define BITS_BF0_PRETIME_OVER_8197F                                            \
	(BIT_MASK_BF0_PRETIME_OVER_8197F << BIT_SHIFT_BF0_PRETIME_OVER_8197F)
#define BIT_CLEAR_BF0_PRETIME_OVER_8197F(x)                                    \
	((x) & (~BITS_BF0_PRETIME_OVER_8197F))
#define BIT_GET_BF0_PRETIME_OVER_8197F(x)                                      \
	(((x) >> BIT_SHIFT_BF0_PRETIME_OVER_8197F) &                           \
	 BIT_MASK_BF0_PRETIME_OVER_8197F)
#define BIT_SET_BF0_PRETIME_OVER_8197F(x, v)                                   \
	(BIT_CLEAR_BF0_PRETIME_OVER_8197F(x) | BIT_BF0_PRETIME_OVER_8197F(v))

#define BIT_SHIFT_BF0_LIFETIME_8197F 0
#define BIT_MASK_BF0_LIFETIME_8197F 0xffff
#define BIT_BF0_LIFETIME_8197F(x)                                              \
	(((x) & BIT_MASK_BF0_LIFETIME_8197F) << BIT_SHIFT_BF0_LIFETIME_8197F)
#define BITS_BF0_LIFETIME_8197F                                                \
	(BIT_MASK_BF0_LIFETIME_8197F << BIT_SHIFT_BF0_LIFETIME_8197F)
#define BIT_CLEAR_BF0_LIFETIME_8197F(x) ((x) & (~BITS_BF0_LIFETIME_8197F))
#define BIT_GET_BF0_LIFETIME_8197F(x)                                          \
	(((x) >> BIT_SHIFT_BF0_LIFETIME_8197F) & BIT_MASK_BF0_LIFETIME_8197F)
#define BIT_SET_BF0_LIFETIME_8197F(x, v)                                       \
	(BIT_CLEAR_BF0_LIFETIME_8197F(x) | BIT_BF0_LIFETIME_8197F(v))

/* 2 REG_BF1_TIME_SETTING_8197F */
#define BIT_BF1_TIMER_SET_8197F BIT(31)
#define BIT_BF1_TIMER_CLR_8197F BIT(30)
#define BIT_BF1_UPDATE_EN_8197F BIT(29)
#define BIT_BF1_TIMER_EN_8197F BIT(28)

#define BIT_SHIFT_BF1_PRETIME_OVER_8197F 16
#define BIT_MASK_BF1_PRETIME_OVER_8197F 0xfff
#define BIT_BF1_PRETIME_OVER_8197F(x)                                          \
	(((x) & BIT_MASK_BF1_PRETIME_OVER_8197F)                               \
	 << BIT_SHIFT_BF1_PRETIME_OVER_8197F)
#define BITS_BF1_PRETIME_OVER_8197F                                            \
	(BIT_MASK_BF1_PRETIME_OVER_8197F << BIT_SHIFT_BF1_PRETIME_OVER_8197F)
#define BIT_CLEAR_BF1_PRETIME_OVER_8197F(x)                                    \
	((x) & (~BITS_BF1_PRETIME_OVER_8197F))
#define BIT_GET_BF1_PRETIME_OVER_8197F(x)                                      \
	(((x) >> BIT_SHIFT_BF1_PRETIME_OVER_8197F) &                           \
	 BIT_MASK_BF1_PRETIME_OVER_8197F)
#define BIT_SET_BF1_PRETIME_OVER_8197F(x, v)                                   \
	(BIT_CLEAR_BF1_PRETIME_OVER_8197F(x) | BIT_BF1_PRETIME_OVER_8197F(v))

#define BIT_SHIFT_BF1_LIFETIME_8197F 0
#define BIT_MASK_BF1_LIFETIME_8197F 0xffff
#define BIT_BF1_LIFETIME_8197F(x)                                              \
	(((x) & BIT_MASK_BF1_LIFETIME_8197F) << BIT_SHIFT_BF1_LIFETIME_8197F)
#define BITS_BF1_LIFETIME_8197F                                                \
	(BIT_MASK_BF1_LIFETIME_8197F << BIT_SHIFT_BF1_LIFETIME_8197F)
#define BIT_CLEAR_BF1_LIFETIME_8197F(x) ((x) & (~BITS_BF1_LIFETIME_8197F))
#define BIT_GET_BF1_LIFETIME_8197F(x)                                          \
	(((x) >> BIT_SHIFT_BF1_LIFETIME_8197F) & BIT_MASK_BF1_LIFETIME_8197F)
#define BIT_SET_BF1_LIFETIME_8197F(x, v)                                       \
	(BIT_CLEAR_BF1_LIFETIME_8197F(x) | BIT_BF1_LIFETIME_8197F(v))

/* 2 REG_BF_TIMEOUT_EN_8197F */
#define BIT_EN_VHT_LDPC_8197F BIT(9)
#define BIT_EN_HT_LDPC_8197F BIT(8)
#define BIT_BF1_TIMEOUT_EN_8197F BIT(1)
#define BIT_BF0_TIMEOUT_EN_8197F BIT(0)

/* 2 REG_MACID_RELEASE0_8197F */

#define BIT_SHIFT_MACID31_0_RELEASE_8197F 0
#define BIT_MASK_MACID31_0_RELEASE_8197F 0xffffffffL
#define BIT_MACID31_0_RELEASE_8197F(x)                                         \
	(((x) & BIT_MASK_MACID31_0_RELEASE_8197F)                              \
	 << BIT_SHIFT_MACID31_0_RELEASE_8197F)
#define BITS_MACID31_0_RELEASE_8197F                                           \
	(BIT_MASK_MACID31_0_RELEASE_8197F << BIT_SHIFT_MACID31_0_RELEASE_8197F)
#define BIT_CLEAR_MACID31_0_RELEASE_8197F(x)                                   \
	((x) & (~BITS_MACID31_0_RELEASE_8197F))
#define BIT_GET_MACID31_0_RELEASE_8197F(x)                                     \
	(((x) >> BIT_SHIFT_MACID31_0_RELEASE_8197F) &                          \
	 BIT_MASK_MACID31_0_RELEASE_8197F)
#define BIT_SET_MACID31_0_RELEASE_8197F(x, v)                                  \
	(BIT_CLEAR_MACID31_0_RELEASE_8197F(x) | BIT_MACID31_0_RELEASE_8197F(v))

/* 2 REG_MACID_RELEASE1_8197F */

#define BIT_SHIFT_MACID63_32_RELEASE_8197F 0
#define BIT_MASK_MACID63_32_RELEASE_8197F 0xffffffffL
#define BIT_MACID63_32_RELEASE_8197F(x)                                        \
	(((x) & BIT_MASK_MACID63_32_RELEASE_8197F)                             \
	 << BIT_SHIFT_MACID63_32_RELEASE_8197F)
#define BITS_MACID63_32_RELEASE_8197F                                          \
	(BIT_MASK_MACID63_32_RELEASE_8197F                                     \
	 << BIT_SHIFT_MACID63_32_RELEASE_8197F)
#define BIT_CLEAR_MACID63_32_RELEASE_8197F(x)                                  \
	((x) & (~BITS_MACID63_32_RELEASE_8197F))
#define BIT_GET_MACID63_32_RELEASE_8197F(x)                                    \
	(((x) >> BIT_SHIFT_MACID63_32_RELEASE_8197F) &                         \
	 BIT_MASK_MACID63_32_RELEASE_8197F)
#define BIT_SET_MACID63_32_RELEASE_8197F(x, v)                                 \
	(BIT_CLEAR_MACID63_32_RELEASE_8197F(x) |                               \
	 BIT_MACID63_32_RELEASE_8197F(v))

/* 2 REG_MACID_RELEASE2_8197F */

#define BIT_SHIFT_MACID95_64_RELEASE_8197F 0
#define BIT_MASK_MACID95_64_RELEASE_8197F 0xffffffffL
#define BIT_MACID95_64_RELEASE_8197F(x)                                        \
	(((x) & BIT_MASK_MACID95_64_RELEASE_8197F)                             \
	 << BIT_SHIFT_MACID95_64_RELEASE_8197F)
#define BITS_MACID95_64_RELEASE_8197F                                          \
	(BIT_MASK_MACID95_64_RELEASE_8197F                                     \
	 << BIT_SHIFT_MACID95_64_RELEASE_8197F)
#define BIT_CLEAR_MACID95_64_RELEASE_8197F(x)                                  \
	((x) & (~BITS_MACID95_64_RELEASE_8197F))
#define BIT_GET_MACID95_64_RELEASE_8197F(x)                                    \
	(((x) >> BIT_SHIFT_MACID95_64_RELEASE_8197F) &                         \
	 BIT_MASK_MACID95_64_RELEASE_8197F)
#define BIT_SET_MACID95_64_RELEASE_8197F(x, v)                                 \
	(BIT_CLEAR_MACID95_64_RELEASE_8197F(x) |                               \
	 BIT_MACID95_64_RELEASE_8197F(v))

/* 2 REG_MACID_RELEASE3_8197F */

#define BIT_SHIFT_MACID127_96_RELEASE_8197F 0
#define BIT_MASK_MACID127_96_RELEASE_8197F 0xffffffffL
#define BIT_MACID127_96_RELEASE_8197F(x)                                       \
	(((x) & BIT_MASK_MACID127_96_RELEASE_8197F)                            \
	 << BIT_SHIFT_MACID127_96_RELEASE_8197F)
#define BITS_MACID127_96_RELEASE_8197F                                         \
	(BIT_MASK_MACID127_96_RELEASE_8197F                                    \
	 << BIT_SHIFT_MACID127_96_RELEASE_8197F)
#define BIT_CLEAR_MACID127_96_RELEASE_8197F(x)                                 \
	((x) & (~BITS_MACID127_96_RELEASE_8197F))
#define BIT_GET_MACID127_96_RELEASE_8197F(x)                                   \
	(((x) >> BIT_SHIFT_MACID127_96_RELEASE_8197F) &                        \
	 BIT_MASK_MACID127_96_RELEASE_8197F)
#define BIT_SET_MACID127_96_RELEASE_8197F(x, v)                                \
	(BIT_CLEAR_MACID127_96_RELEASE_8197F(x) |                              \
	 BIT_MACID127_96_RELEASE_8197F(v))

/* 2 REG_MACID_RELEASE_SETTING_8197F */
#define BIT_MACID_VALUE_8197F BIT(7)

#define BIT_SHIFT_MACID_OFFSET_8197F 0
#define BIT_MASK_MACID_OFFSET_8197F 0x7f
#define BIT_MACID_OFFSET_8197F(x)                                              \
	(((x) & BIT_MASK_MACID_OFFSET_8197F) << BIT_SHIFT_MACID_OFFSET_8197F)
#define BITS_MACID_OFFSET_8197F                                                \
	(BIT_MASK_MACID_OFFSET_8197F << BIT_SHIFT_MACID_OFFSET_8197F)
#define BIT_CLEAR_MACID_OFFSET_8197F(x) ((x) & (~BITS_MACID_OFFSET_8197F))
#define BIT_GET_MACID_OFFSET_8197F(x)                                          \
	(((x) >> BIT_SHIFT_MACID_OFFSET_8197F) & BIT_MASK_MACID_OFFSET_8197F)
#define BIT_SET_MACID_OFFSET_8197F(x, v)                                       \
	(BIT_CLEAR_MACID_OFFSET_8197F(x) | BIT_MACID_OFFSET_8197F(v))

/* 2 REG_FAST_EDCA_VOVI_SETTING_8197F */

#define BIT_SHIFT_VI_FAST_EDCA_TO_8197F 24
#define BIT_MASK_VI_FAST_EDCA_TO_8197F 0xff
#define BIT_VI_FAST_EDCA_TO_8197F(x)                                           \
	(((x) & BIT_MASK_VI_FAST_EDCA_TO_8197F)                                \
	 << BIT_SHIFT_VI_FAST_EDCA_TO_8197F)
#define BITS_VI_FAST_EDCA_TO_8197F                                             \
	(BIT_MASK_VI_FAST_EDCA_TO_8197F << BIT_SHIFT_VI_FAST_EDCA_TO_8197F)
#define BIT_CLEAR_VI_FAST_EDCA_TO_8197F(x) ((x) & (~BITS_VI_FAST_EDCA_TO_8197F))
#define BIT_GET_VI_FAST_EDCA_TO_8197F(x)                                       \
	(((x) >> BIT_SHIFT_VI_FAST_EDCA_TO_8197F) &                            \
	 BIT_MASK_VI_FAST_EDCA_TO_8197F)
#define BIT_SET_VI_FAST_EDCA_TO_8197F(x, v)                                    \
	(BIT_CLEAR_VI_FAST_EDCA_TO_8197F(x) | BIT_VI_FAST_EDCA_TO_8197F(v))

#define BIT_VI_THRESHOLD_SEL_8197F BIT(23)

#define BIT_SHIFT_VI_FAST_EDCA_PKT_TH_8197F 16
#define BIT_MASK_VI_FAST_EDCA_PKT_TH_8197F 0x7f
#define BIT_VI_FAST_EDCA_PKT_TH_8197F(x)                                       \
	(((x) & BIT_MASK_VI_FAST_EDCA_PKT_TH_8197F)                            \
	 << BIT_SHIFT_VI_FAST_EDCA_PKT_TH_8197F)
#define BITS_VI_FAST_EDCA_PKT_TH_8197F                                         \
	(BIT_MASK_VI_FAST_EDCA_PKT_TH_8197F                                    \
	 << BIT_SHIFT_VI_FAST_EDCA_PKT_TH_8197F)
#define BIT_CLEAR_VI_FAST_EDCA_PKT_TH_8197F(x)                                 \
	((x) & (~BITS_VI_FAST_EDCA_PKT_TH_8197F))
#define BIT_GET_VI_FAST_EDCA_PKT_TH_8197F(x)                                   \
	(((x) >> BIT_SHIFT_VI_FAST_EDCA_PKT_TH_8197F) &                        \
	 BIT_MASK_VI_FAST_EDCA_PKT_TH_8197F)
#define BIT_SET_VI_FAST_EDCA_PKT_TH_8197F(x, v)                                \
	(BIT_CLEAR_VI_FAST_EDCA_PKT_TH_8197F(x) |                              \
	 BIT_VI_FAST_EDCA_PKT_TH_8197F(v))

#define BIT_SHIFT_VO_FAST_EDCA_TO_8197F 8
#define BIT_MASK_VO_FAST_EDCA_TO_8197F 0xff
#define BIT_VO_FAST_EDCA_TO_8197F(x)                                           \
	(((x) & BIT_MASK_VO_FAST_EDCA_TO_8197F)                                \
	 << BIT_SHIFT_VO_FAST_EDCA_TO_8197F)
#define BITS_VO_FAST_EDCA_TO_8197F                                             \
	(BIT_MASK_VO_FAST_EDCA_TO_8197F << BIT_SHIFT_VO_FAST_EDCA_TO_8197F)
#define BIT_CLEAR_VO_FAST_EDCA_TO_8197F(x) ((x) & (~BITS_VO_FAST_EDCA_TO_8197F))
#define BIT_GET_VO_FAST_EDCA_TO_8197F(x)                                       \
	(((x) >> BIT_SHIFT_VO_FAST_EDCA_TO_8197F) &                            \
	 BIT_MASK_VO_FAST_EDCA_TO_8197F)
#define BIT_SET_VO_FAST_EDCA_TO_8197F(x, v)                                    \
	(BIT_CLEAR_VO_FAST_EDCA_TO_8197F(x) | BIT_VO_FAST_EDCA_TO_8197F(v))

#define BIT_VO_THRESHOLD_SEL_8197F BIT(7)

#define BIT_SHIFT_VO_FAST_EDCA_PKT_TH_8197F 0
#define BIT_MASK_VO_FAST_EDCA_PKT_TH_8197F 0x7f
#define BIT_VO_FAST_EDCA_PKT_TH_8197F(x)                                       \
	(((x) & BIT_MASK_VO_FAST_EDCA_PKT_TH_8197F)                            \
	 << BIT_SHIFT_VO_FAST_EDCA_PKT_TH_8197F)
#define BITS_VO_FAST_EDCA_PKT_TH_8197F                                         \
	(BIT_MASK_VO_FAST_EDCA_PKT_TH_8197F                                    \
	 << BIT_SHIFT_VO_FAST_EDCA_PKT_TH_8197F)
#define BIT_CLEAR_VO_FAST_EDCA_PKT_TH_8197F(x)                                 \
	((x) & (~BITS_VO_FAST_EDCA_PKT_TH_8197F))
#define BIT_GET_VO_FAST_EDCA_PKT_TH_8197F(x)                                   \
	(((x) >> BIT_SHIFT_VO_FAST_EDCA_PKT_TH_8197F) &                        \
	 BIT_MASK_VO_FAST_EDCA_PKT_TH_8197F)
#define BIT_SET_VO_FAST_EDCA_PKT_TH_8197F(x, v)                                \
	(BIT_CLEAR_VO_FAST_EDCA_PKT_TH_8197F(x) |                              \
	 BIT_VO_FAST_EDCA_PKT_TH_8197F(v))

/* 2 REG_FAST_EDCA_BEBK_SETTING_8197F */

#define BIT_SHIFT_BK_FAST_EDCA_TO_8197F 24
#define BIT_MASK_BK_FAST_EDCA_TO_8197F 0xff
#define BIT_BK_FAST_EDCA_TO_8197F(x)                                           \
	(((x) & BIT_MASK_BK_FAST_EDCA_TO_8197F)                                \
	 << BIT_SHIFT_BK_FAST_EDCA_TO_8197F)
#define BITS_BK_FAST_EDCA_TO_8197F                                             \
	(BIT_MASK_BK_FAST_EDCA_TO_8197F << BIT_SHIFT_BK_FAST_EDCA_TO_8197F)
#define BIT_CLEAR_BK_FAST_EDCA_TO_8197F(x) ((x) & (~BITS_BK_FAST_EDCA_TO_8197F))
#define BIT_GET_BK_FAST_EDCA_TO_8197F(x)                                       \
	(((x) >> BIT_SHIFT_BK_FAST_EDCA_TO_8197F) &                            \
	 BIT_MASK_BK_FAST_EDCA_TO_8197F)
#define BIT_SET_BK_FAST_EDCA_TO_8197F(x, v)                                    \
	(BIT_CLEAR_BK_FAST_EDCA_TO_8197F(x) | BIT_BK_FAST_EDCA_TO_8197F(v))

#define BIT_BK_THRESHOLD_SEL_8197F BIT(23)

#define BIT_SHIFT_BK_FAST_EDCA_PKT_TH_8197F 16
#define BIT_MASK_BK_FAST_EDCA_PKT_TH_8197F 0x7f
#define BIT_BK_FAST_EDCA_PKT_TH_8197F(x)                                       \
	(((x) & BIT_MASK_BK_FAST_EDCA_PKT_TH_8197F)                            \
	 << BIT_SHIFT_BK_FAST_EDCA_PKT_TH_8197F)
#define BITS_BK_FAST_EDCA_PKT_TH_8197F                                         \
	(BIT_MASK_BK_FAST_EDCA_PKT_TH_8197F                                    \
	 << BIT_SHIFT_BK_FAST_EDCA_PKT_TH_8197F)
#define BIT_CLEAR_BK_FAST_EDCA_PKT_TH_8197F(x)                                 \
	((x) & (~BITS_BK_FAST_EDCA_PKT_TH_8197F))
#define BIT_GET_BK_FAST_EDCA_PKT_TH_8197F(x)                                   \
	(((x) >> BIT_SHIFT_BK_FAST_EDCA_PKT_TH_8197F) &                        \
	 BIT_MASK_BK_FAST_EDCA_PKT_TH_8197F)
#define BIT_SET_BK_FAST_EDCA_PKT_TH_8197F(x, v)                                \
	(BIT_CLEAR_BK_FAST_EDCA_PKT_TH_8197F(x) |                              \
	 BIT_BK_FAST_EDCA_PKT_TH_8197F(v))

#define BIT_SHIFT_BE_FAST_EDCA_TO_8197F 8
#define BIT_MASK_BE_FAST_EDCA_TO_8197F 0xff
#define BIT_BE_FAST_EDCA_TO_8197F(x)                                           \
	(((x) & BIT_MASK_BE_FAST_EDCA_TO_8197F)                                \
	 << BIT_SHIFT_BE_FAST_EDCA_TO_8197F)
#define BITS_BE_FAST_EDCA_TO_8197F                                             \
	(BIT_MASK_BE_FAST_EDCA_TO_8197F << BIT_SHIFT_BE_FAST_EDCA_TO_8197F)
#define BIT_CLEAR_BE_FAST_EDCA_TO_8197F(x) ((x) & (~BITS_BE_FAST_EDCA_TO_8197F))
#define BIT_GET_BE_FAST_EDCA_TO_8197F(x)                                       \
	(((x) >> BIT_SHIFT_BE_FAST_EDCA_TO_8197F) &                            \
	 BIT_MASK_BE_FAST_EDCA_TO_8197F)
#define BIT_SET_BE_FAST_EDCA_TO_8197F(x, v)                                    \
	(BIT_CLEAR_BE_FAST_EDCA_TO_8197F(x) | BIT_BE_FAST_EDCA_TO_8197F(v))

#define BIT_BE_THRESHOLD_SEL_8197F BIT(7)

#define BIT_SHIFT_BE_FAST_EDCA_PKT_TH_8197F 0
#define BIT_MASK_BE_FAST_EDCA_PKT_TH_8197F 0x7f
#define BIT_BE_FAST_EDCA_PKT_TH_8197F(x)                                       \
	(((x) & BIT_MASK_BE_FAST_EDCA_PKT_TH_8197F)                            \
	 << BIT_SHIFT_BE_FAST_EDCA_PKT_TH_8197F)
#define BITS_BE_FAST_EDCA_PKT_TH_8197F                                         \
	(BIT_MASK_BE_FAST_EDCA_PKT_TH_8197F                                    \
	 << BIT_SHIFT_BE_FAST_EDCA_PKT_TH_8197F)
#define BIT_CLEAR_BE_FAST_EDCA_PKT_TH_8197F(x)                                 \
	((x) & (~BITS_BE_FAST_EDCA_PKT_TH_8197F))
#define BIT_GET_BE_FAST_EDCA_PKT_TH_8197F(x)                                   \
	(((x) >> BIT_SHIFT_BE_FAST_EDCA_PKT_TH_8197F) &                        \
	 BIT_MASK_BE_FAST_EDCA_PKT_TH_8197F)
#define BIT_SET_BE_FAST_EDCA_PKT_TH_8197F(x, v)                                \
	(BIT_CLEAR_BE_FAST_EDCA_PKT_TH_8197F(x) |                              \
	 BIT_BE_FAST_EDCA_PKT_TH_8197F(v))

/* 2 REG_MACID_DROP0_8197F */

#define BIT_SHIFT_MACID31_0_DROP_8197F 0
#define BIT_MASK_MACID31_0_DROP_8197F 0xffffffffL
#define BIT_MACID31_0_DROP_8197F(x)                                            \
	(((x) & BIT_MASK_MACID31_0_DROP_8197F)                                 \
	 << BIT_SHIFT_MACID31_0_DROP_8197F)
#define BITS_MACID31_0_DROP_8197F                                              \
	(BIT_MASK_MACID31_0_DROP_8197F << BIT_SHIFT_MACID31_0_DROP_8197F)
#define BIT_CLEAR_MACID31_0_DROP_8197F(x) ((x) & (~BITS_MACID31_0_DROP_8197F))
#define BIT_GET_MACID31_0_DROP_8197F(x)                                        \
	(((x) >> BIT_SHIFT_MACID31_0_DROP_8197F) &                             \
	 BIT_MASK_MACID31_0_DROP_8197F)
#define BIT_SET_MACID31_0_DROP_8197F(x, v)                                     \
	(BIT_CLEAR_MACID31_0_DROP_8197F(x) | BIT_MACID31_0_DROP_8197F(v))

/* 2 REG_MACID_DROP1_8197F */

#define BIT_SHIFT_MACID63_32_DROP_8197F 0
#define BIT_MASK_MACID63_32_DROP_8197F 0xffffffffL
#define BIT_MACID63_32_DROP_8197F(x)                                           \
	(((x) & BIT_MASK_MACID63_32_DROP_8197F)                                \
	 << BIT_SHIFT_MACID63_32_DROP_8197F)
#define BITS_MACID63_32_DROP_8197F                                             \
	(BIT_MASK_MACID63_32_DROP_8197F << BIT_SHIFT_MACID63_32_DROP_8197F)
#define BIT_CLEAR_MACID63_32_DROP_8197F(x) ((x) & (~BITS_MACID63_32_DROP_8197F))
#define BIT_GET_MACID63_32_DROP_8197F(x)                                       \
	(((x) >> BIT_SHIFT_MACID63_32_DROP_8197F) &                            \
	 BIT_MASK_MACID63_32_DROP_8197F)
#define BIT_SET_MACID63_32_DROP_8197F(x, v)                                    \
	(BIT_CLEAR_MACID63_32_DROP_8197F(x) | BIT_MACID63_32_DROP_8197F(v))

/* 2 REG_MACID_DROP2_8197F */

#define BIT_SHIFT_MACID95_64_DROP_8197F 0
#define BIT_MASK_MACID95_64_DROP_8197F 0xffffffffL
#define BIT_MACID95_64_DROP_8197F(x)                                           \
	(((x) & BIT_MASK_MACID95_64_DROP_8197F)                                \
	 << BIT_SHIFT_MACID95_64_DROP_8197F)
#define BITS_MACID95_64_DROP_8197F                                             \
	(BIT_MASK_MACID95_64_DROP_8197F << BIT_SHIFT_MACID95_64_DROP_8197F)
#define BIT_CLEAR_MACID95_64_DROP_8197F(x) ((x) & (~BITS_MACID95_64_DROP_8197F))
#define BIT_GET_MACID95_64_DROP_8197F(x)                                       \
	(((x) >> BIT_SHIFT_MACID95_64_DROP_8197F) &                            \
	 BIT_MASK_MACID95_64_DROP_8197F)
#define BIT_SET_MACID95_64_DROP_8197F(x, v)                                    \
	(BIT_CLEAR_MACID95_64_DROP_8197F(x) | BIT_MACID95_64_DROP_8197F(v))

/* 2 REG_MACID_DROP3_8197F */

#define BIT_SHIFT_MACID127_96_DROP_8197F 0
#define BIT_MASK_MACID127_96_DROP_8197F 0xffffffffL
#define BIT_MACID127_96_DROP_8197F(x)                                          \
	(((x) & BIT_MASK_MACID127_96_DROP_8197F)                               \
	 << BIT_SHIFT_MACID127_96_DROP_8197F)
#define BITS_MACID127_96_DROP_8197F                                            \
	(BIT_MASK_MACID127_96_DROP_8197F << BIT_SHIFT_MACID127_96_DROP_8197F)
#define BIT_CLEAR_MACID127_96_DROP_8197F(x)                                    \
	((x) & (~BITS_MACID127_96_DROP_8197F))
#define BIT_GET_MACID127_96_DROP_8197F(x)                                      \
	(((x) >> BIT_SHIFT_MACID127_96_DROP_8197F) &                           \
	 BIT_MASK_MACID127_96_DROP_8197F)
#define BIT_SET_MACID127_96_DROP_8197F(x, v)                                   \
	(BIT_CLEAR_MACID127_96_DROP_8197F(x) | BIT_MACID127_96_DROP_8197F(v))

/* 2 REG_R_MACID_RELEASE_SUCCESS_0_8197F */

#define BIT_SHIFT_R_MACID_RELEASE_SUCCESS_0_8197F 0
#define BIT_MASK_R_MACID_RELEASE_SUCCESS_0_8197F 0xffffffffL
#define BIT_R_MACID_RELEASE_SUCCESS_0_8197F(x)                                 \
	(((x) & BIT_MASK_R_MACID_RELEASE_SUCCESS_0_8197F)                      \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_0_8197F)
#define BITS_R_MACID_RELEASE_SUCCESS_0_8197F                                   \
	(BIT_MASK_R_MACID_RELEASE_SUCCESS_0_8197F                              \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_0_8197F)
#define BIT_CLEAR_R_MACID_RELEASE_SUCCESS_0_8197F(x)                           \
	((x) & (~BITS_R_MACID_RELEASE_SUCCESS_0_8197F))
#define BIT_GET_R_MACID_RELEASE_SUCCESS_0_8197F(x)                             \
	(((x) >> BIT_SHIFT_R_MACID_RELEASE_SUCCESS_0_8197F) &                  \
	 BIT_MASK_R_MACID_RELEASE_SUCCESS_0_8197F)
#define BIT_SET_R_MACID_RELEASE_SUCCESS_0_8197F(x, v)                          \
	(BIT_CLEAR_R_MACID_RELEASE_SUCCESS_0_8197F(x) |                        \
	 BIT_R_MACID_RELEASE_SUCCESS_0_8197F(v))

/* 2 REG_R_MACID_RELEASE_SUCCESS_1_8197F */

#define BIT_SHIFT_R_MACID_RELEASE_SUCCESS_1_8197F 0
#define BIT_MASK_R_MACID_RELEASE_SUCCESS_1_8197F 0xffffffffL
#define BIT_R_MACID_RELEASE_SUCCESS_1_8197F(x)                                 \
	(((x) & BIT_MASK_R_MACID_RELEASE_SUCCESS_1_8197F)                      \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_1_8197F)
#define BITS_R_MACID_RELEASE_SUCCESS_1_8197F                                   \
	(BIT_MASK_R_MACID_RELEASE_SUCCESS_1_8197F                              \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_1_8197F)
#define BIT_CLEAR_R_MACID_RELEASE_SUCCESS_1_8197F(x)                           \
	((x) & (~BITS_R_MACID_RELEASE_SUCCESS_1_8197F))
#define BIT_GET_R_MACID_RELEASE_SUCCESS_1_8197F(x)                             \
	(((x) >> BIT_SHIFT_R_MACID_RELEASE_SUCCESS_1_8197F) &                  \
	 BIT_MASK_R_MACID_RELEASE_SUCCESS_1_8197F)
#define BIT_SET_R_MACID_RELEASE_SUCCESS_1_8197F(x, v)                          \
	(BIT_CLEAR_R_MACID_RELEASE_SUCCESS_1_8197F(x) |                        \
	 BIT_R_MACID_RELEASE_SUCCESS_1_8197F(v))

/* 2 REG_R_MACID_RELEASE_SUCCESS_2_8197F */

#define BIT_SHIFT_R_MACID_RELEASE_SUCCESS_2_8197F 0
#define BIT_MASK_R_MACID_RELEASE_SUCCESS_2_8197F 0xffffffffL
#define BIT_R_MACID_RELEASE_SUCCESS_2_8197F(x)                                 \
	(((x) & BIT_MASK_R_MACID_RELEASE_SUCCESS_2_8197F)                      \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_2_8197F)
#define BITS_R_MACID_RELEASE_SUCCESS_2_8197F                                   \
	(BIT_MASK_R_MACID_RELEASE_SUCCESS_2_8197F                              \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_2_8197F)
#define BIT_CLEAR_R_MACID_RELEASE_SUCCESS_2_8197F(x)                           \
	((x) & (~BITS_R_MACID_RELEASE_SUCCESS_2_8197F))
#define BIT_GET_R_MACID_RELEASE_SUCCESS_2_8197F(x)                             \
	(((x) >> BIT_SHIFT_R_MACID_RELEASE_SUCCESS_2_8197F) &                  \
	 BIT_MASK_R_MACID_RELEASE_SUCCESS_2_8197F)
#define BIT_SET_R_MACID_RELEASE_SUCCESS_2_8197F(x, v)                          \
	(BIT_CLEAR_R_MACID_RELEASE_SUCCESS_2_8197F(x) |                        \
	 BIT_R_MACID_RELEASE_SUCCESS_2_8197F(v))

/* 2 REG_R_MACID_RELEASE_SUCCESS_3_8197F */

#define BIT_SHIFT_R_MACID_RELEASE_SUCCESS_3_8197F 0
#define BIT_MASK_R_MACID_RELEASE_SUCCESS_3_8197F 0xffffffffL
#define BIT_R_MACID_RELEASE_SUCCESS_3_8197F(x)                                 \
	(((x) & BIT_MASK_R_MACID_RELEASE_SUCCESS_3_8197F)                      \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_3_8197F)
#define BITS_R_MACID_RELEASE_SUCCESS_3_8197F                                   \
	(BIT_MASK_R_MACID_RELEASE_SUCCESS_3_8197F                              \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_3_8197F)
#define BIT_CLEAR_R_MACID_RELEASE_SUCCESS_3_8197F(x)                           \
	((x) & (~BITS_R_MACID_RELEASE_SUCCESS_3_8197F))
#define BIT_GET_R_MACID_RELEASE_SUCCESS_3_8197F(x)                             \
	(((x) >> BIT_SHIFT_R_MACID_RELEASE_SUCCESS_3_8197F) &                  \
	 BIT_MASK_R_MACID_RELEASE_SUCCESS_3_8197F)
#define BIT_SET_R_MACID_RELEASE_SUCCESS_3_8197F(x, v)                          \
	(BIT_CLEAR_R_MACID_RELEASE_SUCCESS_3_8197F(x) |                        \
	 BIT_R_MACID_RELEASE_SUCCESS_3_8197F(v))

/* 2 REG_MGG_FIFO_CRTL_8197F */
#define BIT_R_MGG_FIFO_EN_8197F BIT(31)

#define BIT_SHIFT_R_MGG_FIFO_PG_SIZE_8197F 28
#define BIT_MASK_R_MGG_FIFO_PG_SIZE_8197F 0x7
#define BIT_R_MGG_FIFO_PG_SIZE_8197F(x)                                        \
	(((x) & BIT_MASK_R_MGG_FIFO_PG_SIZE_8197F)                             \
	 << BIT_SHIFT_R_MGG_FIFO_PG_SIZE_8197F)
#define BITS_R_MGG_FIFO_PG_SIZE_8197F                                          \
	(BIT_MASK_R_MGG_FIFO_PG_SIZE_8197F                                     \
	 << BIT_SHIFT_R_MGG_FIFO_PG_SIZE_8197F)
#define BIT_CLEAR_R_MGG_FIFO_PG_SIZE_8197F(x)                                  \
	((x) & (~BITS_R_MGG_FIFO_PG_SIZE_8197F))
#define BIT_GET_R_MGG_FIFO_PG_SIZE_8197F(x)                                    \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_PG_SIZE_8197F) &                         \
	 BIT_MASK_R_MGG_FIFO_PG_SIZE_8197F)
#define BIT_SET_R_MGG_FIFO_PG_SIZE_8197F(x, v)                                 \
	(BIT_CLEAR_R_MGG_FIFO_PG_SIZE_8197F(x) |                               \
	 BIT_R_MGG_FIFO_PG_SIZE_8197F(v))

#define BIT_SHIFT_R_MGG_FIFO_START_PG_8197F 16
#define BIT_MASK_R_MGG_FIFO_START_PG_8197F 0xfff
#define BIT_R_MGG_FIFO_START_PG_8197F(x)                                       \
	(((x) & BIT_MASK_R_MGG_FIFO_START_PG_8197F)                            \
	 << BIT_SHIFT_R_MGG_FIFO_START_PG_8197F)
#define BITS_R_MGG_FIFO_START_PG_8197F                                         \
	(BIT_MASK_R_MGG_FIFO_START_PG_8197F                                    \
	 << BIT_SHIFT_R_MGG_FIFO_START_PG_8197F)
#define BIT_CLEAR_R_MGG_FIFO_START_PG_8197F(x)                                 \
	((x) & (~BITS_R_MGG_FIFO_START_PG_8197F))
#define BIT_GET_R_MGG_FIFO_START_PG_8197F(x)                                   \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_START_PG_8197F) &                        \
	 BIT_MASK_R_MGG_FIFO_START_PG_8197F)
#define BIT_SET_R_MGG_FIFO_START_PG_8197F(x, v)                                \
	(BIT_CLEAR_R_MGG_FIFO_START_PG_8197F(x) |                              \
	 BIT_R_MGG_FIFO_START_PG_8197F(v))

#define BIT_SHIFT_R_MGG_FIFO_SIZE_8197F 14
#define BIT_MASK_R_MGG_FIFO_SIZE_8197F 0x3
#define BIT_R_MGG_FIFO_SIZE_8197F(x)                                           \
	(((x) & BIT_MASK_R_MGG_FIFO_SIZE_8197F)                                \
	 << BIT_SHIFT_R_MGG_FIFO_SIZE_8197F)
#define BITS_R_MGG_FIFO_SIZE_8197F                                             \
	(BIT_MASK_R_MGG_FIFO_SIZE_8197F << BIT_SHIFT_R_MGG_FIFO_SIZE_8197F)
#define BIT_CLEAR_R_MGG_FIFO_SIZE_8197F(x) ((x) & (~BITS_R_MGG_FIFO_SIZE_8197F))
#define BIT_GET_R_MGG_FIFO_SIZE_8197F(x)                                       \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_SIZE_8197F) &                            \
	 BIT_MASK_R_MGG_FIFO_SIZE_8197F)
#define BIT_SET_R_MGG_FIFO_SIZE_8197F(x, v)                                    \
	(BIT_CLEAR_R_MGG_FIFO_SIZE_8197F(x) | BIT_R_MGG_FIFO_SIZE_8197F(v))

#define BIT_R_MGG_FIFO_PAUSE_8197F BIT(13)

#define BIT_SHIFT_R_MGG_FIFO_RPTR_8197F 8
#define BIT_MASK_R_MGG_FIFO_RPTR_8197F 0x1f
#define BIT_R_MGG_FIFO_RPTR_8197F(x)                                           \
	(((x) & BIT_MASK_R_MGG_FIFO_RPTR_8197F)                                \
	 << BIT_SHIFT_R_MGG_FIFO_RPTR_8197F)
#define BITS_R_MGG_FIFO_RPTR_8197F                                             \
	(BIT_MASK_R_MGG_FIFO_RPTR_8197F << BIT_SHIFT_R_MGG_FIFO_RPTR_8197F)
#define BIT_CLEAR_R_MGG_FIFO_RPTR_8197F(x) ((x) & (~BITS_R_MGG_FIFO_RPTR_8197F))
#define BIT_GET_R_MGG_FIFO_RPTR_8197F(x)                                       \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_RPTR_8197F) &                            \
	 BIT_MASK_R_MGG_FIFO_RPTR_8197F)
#define BIT_SET_R_MGG_FIFO_RPTR_8197F(x, v)                                    \
	(BIT_CLEAR_R_MGG_FIFO_RPTR_8197F(x) | BIT_R_MGG_FIFO_RPTR_8197F(v))

#define BIT_R_MGG_FIFO_OV_8197F BIT(7)
#define BIT_R_MGG_FIFO_WPTR_ERROR_8197F BIT(6)
#define BIT_R_EN_CPU_LIFETIME_8197F BIT(5)

#define BIT_SHIFT_R_MGG_FIFO_WPTR_8197F 0
#define BIT_MASK_R_MGG_FIFO_WPTR_8197F 0x1f
#define BIT_R_MGG_FIFO_WPTR_8197F(x)                                           \
	(((x) & BIT_MASK_R_MGG_FIFO_WPTR_8197F)                                \
	 << BIT_SHIFT_R_MGG_FIFO_WPTR_8197F)
#define BITS_R_MGG_FIFO_WPTR_8197F                                             \
	(BIT_MASK_R_MGG_FIFO_WPTR_8197F << BIT_SHIFT_R_MGG_FIFO_WPTR_8197F)
#define BIT_CLEAR_R_MGG_FIFO_WPTR_8197F(x) ((x) & (~BITS_R_MGG_FIFO_WPTR_8197F))
#define BIT_GET_R_MGG_FIFO_WPTR_8197F(x)                                       \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_WPTR_8197F) &                            \
	 BIT_MASK_R_MGG_FIFO_WPTR_8197F)
#define BIT_SET_R_MGG_FIFO_WPTR_8197F(x, v)                                    \
	(BIT_CLEAR_R_MGG_FIFO_WPTR_8197F(x) | BIT_R_MGG_FIFO_WPTR_8197F(v))

/* 2 REG_MGG_FIFO_INT_8197F */

#define BIT_SHIFT_R_MGG_FIFO_INT_FLAG_8197F 16
#define BIT_MASK_R_MGG_FIFO_INT_FLAG_8197F 0xffff
#define BIT_R_MGG_FIFO_INT_FLAG_8197F(x)                                       \
	(((x) & BIT_MASK_R_MGG_FIFO_INT_FLAG_8197F)                            \
	 << BIT_SHIFT_R_MGG_FIFO_INT_FLAG_8197F)
#define BITS_R_MGG_FIFO_INT_FLAG_8197F                                         \
	(BIT_MASK_R_MGG_FIFO_INT_FLAG_8197F                                    \
	 << BIT_SHIFT_R_MGG_FIFO_INT_FLAG_8197F)
#define BIT_CLEAR_R_MGG_FIFO_INT_FLAG_8197F(x)                                 \
	((x) & (~BITS_R_MGG_FIFO_INT_FLAG_8197F))
#define BIT_GET_R_MGG_FIFO_INT_FLAG_8197F(x)                                   \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_INT_FLAG_8197F) &                        \
	 BIT_MASK_R_MGG_FIFO_INT_FLAG_8197F)
#define BIT_SET_R_MGG_FIFO_INT_FLAG_8197F(x, v)                                \
	(BIT_CLEAR_R_MGG_FIFO_INT_FLAG_8197F(x) |                              \
	 BIT_R_MGG_FIFO_INT_FLAG_8197F(v))

#define BIT_SHIFT_R_MGG_FIFO_INT_MASK_8197F 0
#define BIT_MASK_R_MGG_FIFO_INT_MASK_8197F 0xffff
#define BIT_R_MGG_FIFO_INT_MASK_8197F(x)                                       \
	(((x) & BIT_MASK_R_MGG_FIFO_INT_MASK_8197F)                            \
	 << BIT_SHIFT_R_MGG_FIFO_INT_MASK_8197F)
#define BITS_R_MGG_FIFO_INT_MASK_8197F                                         \
	(BIT_MASK_R_MGG_FIFO_INT_MASK_8197F                                    \
	 << BIT_SHIFT_R_MGG_FIFO_INT_MASK_8197F)
#define BIT_CLEAR_R_MGG_FIFO_INT_MASK_8197F(x)                                 \
	((x) & (~BITS_R_MGG_FIFO_INT_MASK_8197F))
#define BIT_GET_R_MGG_FIFO_INT_MASK_8197F(x)                                   \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_INT_MASK_8197F) &                        \
	 BIT_MASK_R_MGG_FIFO_INT_MASK_8197F)
#define BIT_SET_R_MGG_FIFO_INT_MASK_8197F(x, v)                                \
	(BIT_CLEAR_R_MGG_FIFO_INT_MASK_8197F(x) |                              \
	 BIT_R_MGG_FIFO_INT_MASK_8197F(v))

/* 2 REG_MGG_FIFO_LIFETIME_8197F */

#define BIT_SHIFT_R_MGG_FIFO_LIFETIME_8197F 16
#define BIT_MASK_R_MGG_FIFO_LIFETIME_8197F 0xffff
#define BIT_R_MGG_FIFO_LIFETIME_8197F(x)                                       \
	(((x) & BIT_MASK_R_MGG_FIFO_LIFETIME_8197F)                            \
	 << BIT_SHIFT_R_MGG_FIFO_LIFETIME_8197F)
#define BITS_R_MGG_FIFO_LIFETIME_8197F                                         \
	(BIT_MASK_R_MGG_FIFO_LIFETIME_8197F                                    \
	 << BIT_SHIFT_R_MGG_FIFO_LIFETIME_8197F)
#define BIT_CLEAR_R_MGG_FIFO_LIFETIME_8197F(x)                                 \
	((x) & (~BITS_R_MGG_FIFO_LIFETIME_8197F))
#define BIT_GET_R_MGG_FIFO_LIFETIME_8197F(x)                                   \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_LIFETIME_8197F) &                        \
	 BIT_MASK_R_MGG_FIFO_LIFETIME_8197F)
#define BIT_SET_R_MGG_FIFO_LIFETIME_8197F(x, v)                                \
	(BIT_CLEAR_R_MGG_FIFO_LIFETIME_8197F(x) |                              \
	 BIT_R_MGG_FIFO_LIFETIME_8197F(v))

#define BIT_SHIFT_R_MGG_FIFO_VALID_MAP_8197F 0
#define BIT_MASK_R_MGG_FIFO_VALID_MAP_8197F 0xffff
#define BIT_R_MGG_FIFO_VALID_MAP_8197F(x)                                      \
	(((x) & BIT_MASK_R_MGG_FIFO_VALID_MAP_8197F)                           \
	 << BIT_SHIFT_R_MGG_FIFO_VALID_MAP_8197F)
#define BITS_R_MGG_FIFO_VALID_MAP_8197F                                        \
	(BIT_MASK_R_MGG_FIFO_VALID_MAP_8197F                                   \
	 << BIT_SHIFT_R_MGG_FIFO_VALID_MAP_8197F)
#define BIT_CLEAR_R_MGG_FIFO_VALID_MAP_8197F(x)                                \
	((x) & (~BITS_R_MGG_FIFO_VALID_MAP_8197F))
#define BIT_GET_R_MGG_FIFO_VALID_MAP_8197F(x)                                  \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_VALID_MAP_8197F) &                       \
	 BIT_MASK_R_MGG_FIFO_VALID_MAP_8197F)
#define BIT_SET_R_MGG_FIFO_VALID_MAP_8197F(x, v)                               \
	(BIT_CLEAR_R_MGG_FIFO_VALID_MAP_8197F(x) |                             \
	 BIT_R_MGG_FIFO_VALID_MAP_8197F(v))

/* 2 REG_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8197F */

#define BIT_SHIFT_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8197F 0
#define BIT_MASK_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8197F 0x7f
#define BIT_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8197F(x)                      \
	(((x) & BIT_MASK_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8197F)           \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8197F)
#define BITS_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8197F                        \
	(BIT_MASK_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8197F                   \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8197F)
#define BIT_CLEAR_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8197F(x)                \
	((x) & (~BITS_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8197F))
#define BIT_GET_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8197F(x)                  \
	(((x) >> BIT_SHIFT_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8197F) &       \
	 BIT_MASK_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8197F)
#define BIT_SET_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8197F(x, v)               \
	(BIT_CLEAR_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8197F(x) |             \
	 BIT_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8197F(v))

/* 2 REG_SHCUT_SETTING_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_SHCUT_LLC_ETH_TYPE0_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_SHCUT_LLC_ETH_TYPE1_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_SHCUT_LLC_OUI0_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_SHCUT_LLC_OUI1_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_SHCUT_LLC_OUI2_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_SHCUT_LLC_OUI3_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_CHNL_INFO_CTRL_8197F */
#define BIT_CHNL_REF_RXNAV_8197F BIT(7)
#define BIT_CHNL_REF_VBON_8197F BIT(6)
#define BIT_CHNL_REF_EDCCA_8197F BIT(5)
#define BIT_RST_CHNL_BUSY_8197F BIT(3)
#define BIT_RST_CHNL_IDLE_8197F BIT(2)
#define BIT_CHNL_INFO_RST_8197F BIT(1)
#define BIT_ATM_AIRTIME_EN_8197F BIT(0)

/* 2 REG_CHNL_IDLE_TIME_8197F */

#define BIT_SHIFT_CHNL_IDLE_TIME_8197F 0
#define BIT_MASK_CHNL_IDLE_TIME_8197F 0xffffffffL
#define BIT_CHNL_IDLE_TIME_8197F(x)                                            \
	(((x) & BIT_MASK_CHNL_IDLE_TIME_8197F)                                 \
	 << BIT_SHIFT_CHNL_IDLE_TIME_8197F)
#define BITS_CHNL_IDLE_TIME_8197F                                              \
	(BIT_MASK_CHNL_IDLE_TIME_8197F << BIT_SHIFT_CHNL_IDLE_TIME_8197F)
#define BIT_CLEAR_CHNL_IDLE_TIME_8197F(x) ((x) & (~BITS_CHNL_IDLE_TIME_8197F))
#define BIT_GET_CHNL_IDLE_TIME_8197F(x)                                        \
	(((x) >> BIT_SHIFT_CHNL_IDLE_TIME_8197F) &                             \
	 BIT_MASK_CHNL_IDLE_TIME_8197F)
#define BIT_SET_CHNL_IDLE_TIME_8197F(x, v)                                     \
	(BIT_CLEAR_CHNL_IDLE_TIME_8197F(x) | BIT_CHNL_IDLE_TIME_8197F(v))

/* 2 REG_CHNL_BUSY_TIME_8197F */

#define BIT_SHIFT_CHNL_BUSY_TIME_8197F 0
#define BIT_MASK_CHNL_BUSY_TIME_8197F 0xffffffffL
#define BIT_CHNL_BUSY_TIME_8197F(x)                                            \
	(((x) & BIT_MASK_CHNL_BUSY_TIME_8197F)                                 \
	 << BIT_SHIFT_CHNL_BUSY_TIME_8197F)
#define BITS_CHNL_BUSY_TIME_8197F                                              \
	(BIT_MASK_CHNL_BUSY_TIME_8197F << BIT_SHIFT_CHNL_BUSY_TIME_8197F)
#define BIT_CLEAR_CHNL_BUSY_TIME_8197F(x) ((x) & (~BITS_CHNL_BUSY_TIME_8197F))
#define BIT_GET_CHNL_BUSY_TIME_8197F(x)                                        \
	(((x) >> BIT_SHIFT_CHNL_BUSY_TIME_8197F) &                             \
	 BIT_MASK_CHNL_BUSY_TIME_8197F)
#define BIT_SET_CHNL_BUSY_TIME_8197F(x, v)                                     \
	(BIT_CLEAR_CHNL_BUSY_TIME_8197F(x) | BIT_CHNL_BUSY_TIME_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_EDCA_VO_PARAM_8197F */

#define BIT_SHIFT_TXOPLIMIT_8197F 16
#define BIT_MASK_TXOPLIMIT_8197F 0x7ff
#define BIT_TXOPLIMIT_8197F(x)                                                 \
	(((x) & BIT_MASK_TXOPLIMIT_8197F) << BIT_SHIFT_TXOPLIMIT_8197F)
#define BITS_TXOPLIMIT_8197F                                                   \
	(BIT_MASK_TXOPLIMIT_8197F << BIT_SHIFT_TXOPLIMIT_8197F)
#define BIT_CLEAR_TXOPLIMIT_8197F(x) ((x) & (~BITS_TXOPLIMIT_8197F))
#define BIT_GET_TXOPLIMIT_8197F(x)                                             \
	(((x) >> BIT_SHIFT_TXOPLIMIT_8197F) & BIT_MASK_TXOPLIMIT_8197F)
#define BIT_SET_TXOPLIMIT_8197F(x, v)                                          \
	(BIT_CLEAR_TXOPLIMIT_8197F(x) | BIT_TXOPLIMIT_8197F(v))

#define BIT_SHIFT_CW_8197F 8
#define BIT_MASK_CW_8197F 0xff
#define BIT_CW_8197F(x) (((x) & BIT_MASK_CW_8197F) << BIT_SHIFT_CW_8197F)
#define BITS_CW_8197F (BIT_MASK_CW_8197F << BIT_SHIFT_CW_8197F)
#define BIT_CLEAR_CW_8197F(x) ((x) & (~BITS_CW_8197F))
#define BIT_GET_CW_8197F(x) (((x) >> BIT_SHIFT_CW_8197F) & BIT_MASK_CW_8197F)
#define BIT_SET_CW_8197F(x, v) (BIT_CLEAR_CW_8197F(x) | BIT_CW_8197F(v))

#define BIT_SHIFT_AIFS_8197F 0
#define BIT_MASK_AIFS_8197F 0xff
#define BIT_AIFS_8197F(x) (((x) & BIT_MASK_AIFS_8197F) << BIT_SHIFT_AIFS_8197F)
#define BITS_AIFS_8197F (BIT_MASK_AIFS_8197F << BIT_SHIFT_AIFS_8197F)
#define BIT_CLEAR_AIFS_8197F(x) ((x) & (~BITS_AIFS_8197F))
#define BIT_GET_AIFS_8197F(x)                                                  \
	(((x) >> BIT_SHIFT_AIFS_8197F) & BIT_MASK_AIFS_8197F)
#define BIT_SET_AIFS_8197F(x, v) (BIT_CLEAR_AIFS_8197F(x) | BIT_AIFS_8197F(v))

/* 2 REG_EDCA_VI_PARAM_8197F */

/* 2 REG_NOT_VALID_8197F */

#define BIT_SHIFT_TXOPLIMIT_8197F 16
#define BIT_MASK_TXOPLIMIT_8197F 0x7ff
#define BIT_TXOPLIMIT_8197F(x)                                                 \
	(((x) & BIT_MASK_TXOPLIMIT_8197F) << BIT_SHIFT_TXOPLIMIT_8197F)
#define BITS_TXOPLIMIT_8197F                                                   \
	(BIT_MASK_TXOPLIMIT_8197F << BIT_SHIFT_TXOPLIMIT_8197F)
#define BIT_CLEAR_TXOPLIMIT_8197F(x) ((x) & (~BITS_TXOPLIMIT_8197F))
#define BIT_GET_TXOPLIMIT_8197F(x)                                             \
	(((x) >> BIT_SHIFT_TXOPLIMIT_8197F) & BIT_MASK_TXOPLIMIT_8197F)
#define BIT_SET_TXOPLIMIT_8197F(x, v)                                          \
	(BIT_CLEAR_TXOPLIMIT_8197F(x) | BIT_TXOPLIMIT_8197F(v))

#define BIT_SHIFT_CW_8197F 8
#define BIT_MASK_CW_8197F 0xff
#define BIT_CW_8197F(x) (((x) & BIT_MASK_CW_8197F) << BIT_SHIFT_CW_8197F)
#define BITS_CW_8197F (BIT_MASK_CW_8197F << BIT_SHIFT_CW_8197F)
#define BIT_CLEAR_CW_8197F(x) ((x) & (~BITS_CW_8197F))
#define BIT_GET_CW_8197F(x) (((x) >> BIT_SHIFT_CW_8197F) & BIT_MASK_CW_8197F)
#define BIT_SET_CW_8197F(x, v) (BIT_CLEAR_CW_8197F(x) | BIT_CW_8197F(v))

#define BIT_SHIFT_AIFS_8197F 0
#define BIT_MASK_AIFS_8197F 0xff
#define BIT_AIFS_8197F(x) (((x) & BIT_MASK_AIFS_8197F) << BIT_SHIFT_AIFS_8197F)
#define BITS_AIFS_8197F (BIT_MASK_AIFS_8197F << BIT_SHIFT_AIFS_8197F)
#define BIT_CLEAR_AIFS_8197F(x) ((x) & (~BITS_AIFS_8197F))
#define BIT_GET_AIFS_8197F(x)                                                  \
	(((x) >> BIT_SHIFT_AIFS_8197F) & BIT_MASK_AIFS_8197F)
#define BIT_SET_AIFS_8197F(x, v) (BIT_CLEAR_AIFS_8197F(x) | BIT_AIFS_8197F(v))

/* 2 REG_EDCA_BE_PARAM_8197F */

/* 2 REG_NOT_VALID_8197F */

#define BIT_SHIFT_TXOPLIMIT_8197F 16
#define BIT_MASK_TXOPLIMIT_8197F 0x7ff
#define BIT_TXOPLIMIT_8197F(x)                                                 \
	(((x) & BIT_MASK_TXOPLIMIT_8197F) << BIT_SHIFT_TXOPLIMIT_8197F)
#define BITS_TXOPLIMIT_8197F                                                   \
	(BIT_MASK_TXOPLIMIT_8197F << BIT_SHIFT_TXOPLIMIT_8197F)
#define BIT_CLEAR_TXOPLIMIT_8197F(x) ((x) & (~BITS_TXOPLIMIT_8197F))
#define BIT_GET_TXOPLIMIT_8197F(x)                                             \
	(((x) >> BIT_SHIFT_TXOPLIMIT_8197F) & BIT_MASK_TXOPLIMIT_8197F)
#define BIT_SET_TXOPLIMIT_8197F(x, v)                                          \
	(BIT_CLEAR_TXOPLIMIT_8197F(x) | BIT_TXOPLIMIT_8197F(v))

#define BIT_SHIFT_CW_8197F 8
#define BIT_MASK_CW_8197F 0xff
#define BIT_CW_8197F(x) (((x) & BIT_MASK_CW_8197F) << BIT_SHIFT_CW_8197F)
#define BITS_CW_8197F (BIT_MASK_CW_8197F << BIT_SHIFT_CW_8197F)
#define BIT_CLEAR_CW_8197F(x) ((x) & (~BITS_CW_8197F))
#define BIT_GET_CW_8197F(x) (((x) >> BIT_SHIFT_CW_8197F) & BIT_MASK_CW_8197F)
#define BIT_SET_CW_8197F(x, v) (BIT_CLEAR_CW_8197F(x) | BIT_CW_8197F(v))

#define BIT_SHIFT_AIFS_8197F 0
#define BIT_MASK_AIFS_8197F 0xff
#define BIT_AIFS_8197F(x) (((x) & BIT_MASK_AIFS_8197F) << BIT_SHIFT_AIFS_8197F)
#define BITS_AIFS_8197F (BIT_MASK_AIFS_8197F << BIT_SHIFT_AIFS_8197F)
#define BIT_CLEAR_AIFS_8197F(x) ((x) & (~BITS_AIFS_8197F))
#define BIT_GET_AIFS_8197F(x)                                                  \
	(((x) >> BIT_SHIFT_AIFS_8197F) & BIT_MASK_AIFS_8197F)
#define BIT_SET_AIFS_8197F(x, v) (BIT_CLEAR_AIFS_8197F(x) | BIT_AIFS_8197F(v))

/* 2 REG_EDCA_BK_PARAM_8197F */

/* 2 REG_NOT_VALID_8197F */

#define BIT_SHIFT_TXOPLIMIT_8197F 16
#define BIT_MASK_TXOPLIMIT_8197F 0x7ff
#define BIT_TXOPLIMIT_8197F(x)                                                 \
	(((x) & BIT_MASK_TXOPLIMIT_8197F) << BIT_SHIFT_TXOPLIMIT_8197F)
#define BITS_TXOPLIMIT_8197F                                                   \
	(BIT_MASK_TXOPLIMIT_8197F << BIT_SHIFT_TXOPLIMIT_8197F)
#define BIT_CLEAR_TXOPLIMIT_8197F(x) ((x) & (~BITS_TXOPLIMIT_8197F))
#define BIT_GET_TXOPLIMIT_8197F(x)                                             \
	(((x) >> BIT_SHIFT_TXOPLIMIT_8197F) & BIT_MASK_TXOPLIMIT_8197F)
#define BIT_SET_TXOPLIMIT_8197F(x, v)                                          \
	(BIT_CLEAR_TXOPLIMIT_8197F(x) | BIT_TXOPLIMIT_8197F(v))

#define BIT_SHIFT_CW_8197F 8
#define BIT_MASK_CW_8197F 0xff
#define BIT_CW_8197F(x) (((x) & BIT_MASK_CW_8197F) << BIT_SHIFT_CW_8197F)
#define BITS_CW_8197F (BIT_MASK_CW_8197F << BIT_SHIFT_CW_8197F)
#define BIT_CLEAR_CW_8197F(x) ((x) & (~BITS_CW_8197F))
#define BIT_GET_CW_8197F(x) (((x) >> BIT_SHIFT_CW_8197F) & BIT_MASK_CW_8197F)
#define BIT_SET_CW_8197F(x, v) (BIT_CLEAR_CW_8197F(x) | BIT_CW_8197F(v))

#define BIT_SHIFT_AIFS_8197F 0
#define BIT_MASK_AIFS_8197F 0xff
#define BIT_AIFS_8197F(x) (((x) & BIT_MASK_AIFS_8197F) << BIT_SHIFT_AIFS_8197F)
#define BITS_AIFS_8197F (BIT_MASK_AIFS_8197F << BIT_SHIFT_AIFS_8197F)
#define BIT_CLEAR_AIFS_8197F(x) ((x) & (~BITS_AIFS_8197F))
#define BIT_GET_AIFS_8197F(x)                                                  \
	(((x) >> BIT_SHIFT_AIFS_8197F) & BIT_MASK_AIFS_8197F)
#define BIT_SET_AIFS_8197F(x, v) (BIT_CLEAR_AIFS_8197F(x) | BIT_AIFS_8197F(v))

/* 2 REG_BCNTCFG_8197F */

#define BIT_SHIFT_BCNCW_MAX_8197F 12
#define BIT_MASK_BCNCW_MAX_8197F 0xf
#define BIT_BCNCW_MAX_8197F(x)                                                 \
	(((x) & BIT_MASK_BCNCW_MAX_8197F) << BIT_SHIFT_BCNCW_MAX_8197F)
#define BITS_BCNCW_MAX_8197F                                                   \
	(BIT_MASK_BCNCW_MAX_8197F << BIT_SHIFT_BCNCW_MAX_8197F)
#define BIT_CLEAR_BCNCW_MAX_8197F(x) ((x) & (~BITS_BCNCW_MAX_8197F))
#define BIT_GET_BCNCW_MAX_8197F(x)                                             \
	(((x) >> BIT_SHIFT_BCNCW_MAX_8197F) & BIT_MASK_BCNCW_MAX_8197F)
#define BIT_SET_BCNCW_MAX_8197F(x, v)                                          \
	(BIT_CLEAR_BCNCW_MAX_8197F(x) | BIT_BCNCW_MAX_8197F(v))

#define BIT_SHIFT_BCNCW_MIN_8197F 8
#define BIT_MASK_BCNCW_MIN_8197F 0xf
#define BIT_BCNCW_MIN_8197F(x)                                                 \
	(((x) & BIT_MASK_BCNCW_MIN_8197F) << BIT_SHIFT_BCNCW_MIN_8197F)
#define BITS_BCNCW_MIN_8197F                                                   \
	(BIT_MASK_BCNCW_MIN_8197F << BIT_SHIFT_BCNCW_MIN_8197F)
#define BIT_CLEAR_BCNCW_MIN_8197F(x) ((x) & (~BITS_BCNCW_MIN_8197F))
#define BIT_GET_BCNCW_MIN_8197F(x)                                             \
	(((x) >> BIT_SHIFT_BCNCW_MIN_8197F) & BIT_MASK_BCNCW_MIN_8197F)
#define BIT_SET_BCNCW_MIN_8197F(x, v)                                          \
	(BIT_CLEAR_BCNCW_MIN_8197F(x) | BIT_BCNCW_MIN_8197F(v))

#define BIT_SHIFT_BCNIFS_8197F 0
#define BIT_MASK_BCNIFS_8197F 0xff
#define BIT_BCNIFS_8197F(x)                                                    \
	(((x) & BIT_MASK_BCNIFS_8197F) << BIT_SHIFT_BCNIFS_8197F)
#define BITS_BCNIFS_8197F (BIT_MASK_BCNIFS_8197F << BIT_SHIFT_BCNIFS_8197F)
#define BIT_CLEAR_BCNIFS_8197F(x) ((x) & (~BITS_BCNIFS_8197F))
#define BIT_GET_BCNIFS_8197F(x)                                                \
	(((x) >> BIT_SHIFT_BCNIFS_8197F) & BIT_MASK_BCNIFS_8197F)
#define BIT_SET_BCNIFS_8197F(x, v)                                             \
	(BIT_CLEAR_BCNIFS_8197F(x) | BIT_BCNIFS_8197F(v))

/* 2 REG_PIFS_8197F */

#define BIT_SHIFT_PIFS_8197F 0
#define BIT_MASK_PIFS_8197F 0xff
#define BIT_PIFS_8197F(x) (((x) & BIT_MASK_PIFS_8197F) << BIT_SHIFT_PIFS_8197F)
#define BITS_PIFS_8197F (BIT_MASK_PIFS_8197F << BIT_SHIFT_PIFS_8197F)
#define BIT_CLEAR_PIFS_8197F(x) ((x) & (~BITS_PIFS_8197F))
#define BIT_GET_PIFS_8197F(x)                                                  \
	(((x) >> BIT_SHIFT_PIFS_8197F) & BIT_MASK_PIFS_8197F)
#define BIT_SET_PIFS_8197F(x, v) (BIT_CLEAR_PIFS_8197F(x) | BIT_PIFS_8197F(v))

/* 2 REG_RDG_PIFS_8197F */

#define BIT_SHIFT_RDG_PIFS_8197F 0
#define BIT_MASK_RDG_PIFS_8197F 0xff
#define BIT_RDG_PIFS_8197F(x)                                                  \
	(((x) & BIT_MASK_RDG_PIFS_8197F) << BIT_SHIFT_RDG_PIFS_8197F)
#define BITS_RDG_PIFS_8197F                                                    \
	(BIT_MASK_RDG_PIFS_8197F << BIT_SHIFT_RDG_PIFS_8197F)
#define BIT_CLEAR_RDG_PIFS_8197F(x) ((x) & (~BITS_RDG_PIFS_8197F))
#define BIT_GET_RDG_PIFS_8197F(x)                                              \
	(((x) >> BIT_SHIFT_RDG_PIFS_8197F) & BIT_MASK_RDG_PIFS_8197F)
#define BIT_SET_RDG_PIFS_8197F(x, v)                                           \
	(BIT_CLEAR_RDG_PIFS_8197F(x) | BIT_RDG_PIFS_8197F(v))

/* 2 REG_SIFS_8197F */

#define BIT_SHIFT_SIFS_OFDM_TRX_8197F 24
#define BIT_MASK_SIFS_OFDM_TRX_8197F 0xff
#define BIT_SIFS_OFDM_TRX_8197F(x)                                             \
	(((x) & BIT_MASK_SIFS_OFDM_TRX_8197F) << BIT_SHIFT_SIFS_OFDM_TRX_8197F)
#define BITS_SIFS_OFDM_TRX_8197F                                               \
	(BIT_MASK_SIFS_OFDM_TRX_8197F << BIT_SHIFT_SIFS_OFDM_TRX_8197F)
#define BIT_CLEAR_SIFS_OFDM_TRX_8197F(x) ((x) & (~BITS_SIFS_OFDM_TRX_8197F))
#define BIT_GET_SIFS_OFDM_TRX_8197F(x)                                         \
	(((x) >> BIT_SHIFT_SIFS_OFDM_TRX_8197F) & BIT_MASK_SIFS_OFDM_TRX_8197F)
#define BIT_SET_SIFS_OFDM_TRX_8197F(x, v)                                      \
	(BIT_CLEAR_SIFS_OFDM_TRX_8197F(x) | BIT_SIFS_OFDM_TRX_8197F(v))

#define BIT_SHIFT_SIFS_CCK_TRX_8197F 16
#define BIT_MASK_SIFS_CCK_TRX_8197F 0xff
#define BIT_SIFS_CCK_TRX_8197F(x)                                              \
	(((x) & BIT_MASK_SIFS_CCK_TRX_8197F) << BIT_SHIFT_SIFS_CCK_TRX_8197F)
#define BITS_SIFS_CCK_TRX_8197F                                                \
	(BIT_MASK_SIFS_CCK_TRX_8197F << BIT_SHIFT_SIFS_CCK_TRX_8197F)
#define BIT_CLEAR_SIFS_CCK_TRX_8197F(x) ((x) & (~BITS_SIFS_CCK_TRX_8197F))
#define BIT_GET_SIFS_CCK_TRX_8197F(x)                                          \
	(((x) >> BIT_SHIFT_SIFS_CCK_TRX_8197F) & BIT_MASK_SIFS_CCK_TRX_8197F)
#define BIT_SET_SIFS_CCK_TRX_8197F(x, v)                                       \
	(BIT_CLEAR_SIFS_CCK_TRX_8197F(x) | BIT_SIFS_CCK_TRX_8197F(v))

#define BIT_SHIFT_SIFS_OFDM_CTX_8197F 8
#define BIT_MASK_SIFS_OFDM_CTX_8197F 0xff
#define BIT_SIFS_OFDM_CTX_8197F(x)                                             \
	(((x) & BIT_MASK_SIFS_OFDM_CTX_8197F) << BIT_SHIFT_SIFS_OFDM_CTX_8197F)
#define BITS_SIFS_OFDM_CTX_8197F                                               \
	(BIT_MASK_SIFS_OFDM_CTX_8197F << BIT_SHIFT_SIFS_OFDM_CTX_8197F)
#define BIT_CLEAR_SIFS_OFDM_CTX_8197F(x) ((x) & (~BITS_SIFS_OFDM_CTX_8197F))
#define BIT_GET_SIFS_OFDM_CTX_8197F(x)                                         \
	(((x) >> BIT_SHIFT_SIFS_OFDM_CTX_8197F) & BIT_MASK_SIFS_OFDM_CTX_8197F)
#define BIT_SET_SIFS_OFDM_CTX_8197F(x, v)                                      \
	(BIT_CLEAR_SIFS_OFDM_CTX_8197F(x) | BIT_SIFS_OFDM_CTX_8197F(v))

#define BIT_SHIFT_SIFS_CCK_CTX_8197F 0
#define BIT_MASK_SIFS_CCK_CTX_8197F 0xff
#define BIT_SIFS_CCK_CTX_8197F(x)                                              \
	(((x) & BIT_MASK_SIFS_CCK_CTX_8197F) << BIT_SHIFT_SIFS_CCK_CTX_8197F)
#define BITS_SIFS_CCK_CTX_8197F                                                \
	(BIT_MASK_SIFS_CCK_CTX_8197F << BIT_SHIFT_SIFS_CCK_CTX_8197F)
#define BIT_CLEAR_SIFS_CCK_CTX_8197F(x) ((x) & (~BITS_SIFS_CCK_CTX_8197F))
#define BIT_GET_SIFS_CCK_CTX_8197F(x)                                          \
	(((x) >> BIT_SHIFT_SIFS_CCK_CTX_8197F) & BIT_MASK_SIFS_CCK_CTX_8197F)
#define BIT_SET_SIFS_CCK_CTX_8197F(x, v)                                       \
	(BIT_CLEAR_SIFS_CCK_CTX_8197F(x) | BIT_SIFS_CCK_CTX_8197F(v))

/* 2 REG_TSFTR_SYN_OFFSET_8197F */

#define BIT_SHIFT_TSFTR_SNC_OFFSET_8197F 0
#define BIT_MASK_TSFTR_SNC_OFFSET_8197F 0xffff
#define BIT_TSFTR_SNC_OFFSET_8197F(x)                                          \
	(((x) & BIT_MASK_TSFTR_SNC_OFFSET_8197F)                               \
	 << BIT_SHIFT_TSFTR_SNC_OFFSET_8197F)
#define BITS_TSFTR_SNC_OFFSET_8197F                                            \
	(BIT_MASK_TSFTR_SNC_OFFSET_8197F << BIT_SHIFT_TSFTR_SNC_OFFSET_8197F)
#define BIT_CLEAR_TSFTR_SNC_OFFSET_8197F(x)                                    \
	((x) & (~BITS_TSFTR_SNC_OFFSET_8197F))
#define BIT_GET_TSFTR_SNC_OFFSET_8197F(x)                                      \
	(((x) >> BIT_SHIFT_TSFTR_SNC_OFFSET_8197F) &                           \
	 BIT_MASK_TSFTR_SNC_OFFSET_8197F)
#define BIT_SET_TSFTR_SNC_OFFSET_8197F(x, v)                                   \
	(BIT_CLEAR_TSFTR_SNC_OFFSET_8197F(x) | BIT_TSFTR_SNC_OFFSET_8197F(v))

/* 2 REG_AGGR_BREAK_TIME_8197F */

#define BIT_SHIFT_AGGR_BK_TIME_8197F 0
#define BIT_MASK_AGGR_BK_TIME_8197F 0xff
#define BIT_AGGR_BK_TIME_8197F(x)                                              \
	(((x) & BIT_MASK_AGGR_BK_TIME_8197F) << BIT_SHIFT_AGGR_BK_TIME_8197F)
#define BITS_AGGR_BK_TIME_8197F                                                \
	(BIT_MASK_AGGR_BK_TIME_8197F << BIT_SHIFT_AGGR_BK_TIME_8197F)
#define BIT_CLEAR_AGGR_BK_TIME_8197F(x) ((x) & (~BITS_AGGR_BK_TIME_8197F))
#define BIT_GET_AGGR_BK_TIME_8197F(x)                                          \
	(((x) >> BIT_SHIFT_AGGR_BK_TIME_8197F) & BIT_MASK_AGGR_BK_TIME_8197F)
#define BIT_SET_AGGR_BK_TIME_8197F(x, v)                                       \
	(BIT_CLEAR_AGGR_BK_TIME_8197F(x) | BIT_AGGR_BK_TIME_8197F(v))

/* 2 REG_SLOT_8197F */

#define BIT_SHIFT_SLOT_8197F 0
#define BIT_MASK_SLOT_8197F 0xff
#define BIT_SLOT_8197F(x) (((x) & BIT_MASK_SLOT_8197F) << BIT_SHIFT_SLOT_8197F)
#define BITS_SLOT_8197F (BIT_MASK_SLOT_8197F << BIT_SHIFT_SLOT_8197F)
#define BIT_CLEAR_SLOT_8197F(x) ((x) & (~BITS_SLOT_8197F))
#define BIT_GET_SLOT_8197F(x)                                                  \
	(((x) >> BIT_SHIFT_SLOT_8197F) & BIT_MASK_SLOT_8197F)
#define BIT_SET_SLOT_8197F(x, v) (BIT_CLEAR_SLOT_8197F(x) | BIT_SLOT_8197F(v))

/* 2 REG_TX_PTCL_CTRL_8197F */
#define BIT_DIS_EDCCA_8197F BIT(15)
#define BIT_DIS_CCA_8197F BIT(14)
#define BIT_LSIG_TXOP_TXCMD_NAV_8197F BIT(13)
#define BIT_SIFS_BK_EN_8197F BIT(12)

#define BIT_SHIFT_TXQ_NAV_MSK_8197F 8
#define BIT_MASK_TXQ_NAV_MSK_8197F 0xf
#define BIT_TXQ_NAV_MSK_8197F(x)                                               \
	(((x) & BIT_MASK_TXQ_NAV_MSK_8197F) << BIT_SHIFT_TXQ_NAV_MSK_8197F)
#define BITS_TXQ_NAV_MSK_8197F                                                 \
	(BIT_MASK_TXQ_NAV_MSK_8197F << BIT_SHIFT_TXQ_NAV_MSK_8197F)
#define BIT_CLEAR_TXQ_NAV_MSK_8197F(x) ((x) & (~BITS_TXQ_NAV_MSK_8197F))
#define BIT_GET_TXQ_NAV_MSK_8197F(x)                                           \
	(((x) >> BIT_SHIFT_TXQ_NAV_MSK_8197F) & BIT_MASK_TXQ_NAV_MSK_8197F)
#define BIT_SET_TXQ_NAV_MSK_8197F(x, v)                                        \
	(BIT_CLEAR_TXQ_NAV_MSK_8197F(x) | BIT_TXQ_NAV_MSK_8197F(v))

#define BIT_DIS_CW_8197F BIT(7)
#define BIT_NAV_END_TXOP_8197F BIT(6)
#define BIT_RDG_END_TXOP_8197F BIT(5)
#define BIT_AC_INBCN_HOLD_8197F BIT(4)
#define BIT_MGTQ_TXOP_EN_8197F BIT(3)
#define BIT_MGTQ_RTSMF_EN_8197F BIT(2)
#define BIT_HIQ_RTSMF_EN_8197F BIT(1)
#define BIT_BCN_RTSMF_EN_8197F BIT(0)

/* 2 REG_TXPAUSE_8197F */
#define BIT_STOP_BCN_HI_MGT_8197F BIT(7)
#define BIT_MAC_STOPBCNQ_8197F BIT(6)
#define BIT_MAC_STOPHIQ_8197F BIT(5)
#define BIT_MAC_STOPMGQ_8197F BIT(4)
#define BIT_MAC_STOPBK_8197F BIT(3)
#define BIT_MAC_STOPBE_8197F BIT(2)
#define BIT_MAC_STOPVI_8197F BIT(1)
#define BIT_MAC_STOPVO_8197F BIT(0)

/* 2 REG_DIS_TXREQ_CLR_8197F */
#define BIT_DIS_BT_CCA_8197F BIT(7)
#define BIT_DIS_TXREQ_CLR_CPUMGQ_8197F BIT(6)
#define BIT_DIS_TXREQ_CLR_HI_8197F BIT(5)
#define BIT_DIS_TXREQ_CLR_MGQ_8197F BIT(4)
#define BIT_DIS_TXREQ_CLR_VO_8197F BIT(3)
#define BIT_DIS_TXREQ_CLR_VI_8197F BIT(2)
#define BIT_DIS_TXREQ_CLR_BE_8197F BIT(1)
#define BIT_DIS_TXREQ_CLR_BK_8197F BIT(0)

/* 2 REG_RD_CTRL_8197F */
#define BIT_EN_CLR_TXREQ_INCCA_8197F BIT(15)
#define BIT_DIS_TX_OVER_BCNQ_8197F BIT(14)
#define BIT_EN_BCNERR_INCCA_8197F BIT(13)
#define BIT_EN_BCNERR_INEDCCA_8197F BIT(12)
#define BIT_EDCCA_MSK_CNTDOWN_EN_8197F BIT(11)
#define BIT_DIS_TXOP_CFE_8197F BIT(10)
#define BIT_DIS_LSIG_CFE_8197F BIT(9)
#define BIT_DIS_STBC_CFE_8197F BIT(8)
#define BIT_BKQ_RD_INIT_EN_8197F BIT(7)
#define BIT_BEQ_RD_INIT_EN_8197F BIT(6)
#define BIT_VIQ_RD_INIT_EN_8197F BIT(5)
#define BIT_VOQ_RD_INIT_EN_8197F BIT(4)
#define BIT_BKQ_RD_RESP_EN_8197F BIT(3)
#define BIT_BEQ_RD_RESP_EN_8197F BIT(2)
#define BIT_VIQ_RD_RESP_EN_8197F BIT(1)
#define BIT_VOQ_RD_RESP_EN_8197F BIT(0)

/* 2 REG_MBSSID_CTRL_8197F */
#define BIT_MBID_BCNQ7_EN_8197F BIT(7)
#define BIT_MBID_BCNQ6_EN_8197F BIT(6)
#define BIT_MBID_BCNQ5_EN_8197F BIT(5)
#define BIT_MBID_BCNQ4_EN_8197F BIT(4)
#define BIT_MBID_BCNQ3_EN_8197F BIT(3)
#define BIT_MBID_BCNQ2_EN_8197F BIT(2)
#define BIT_MBID_BCNQ1_EN_8197F BIT(1)
#define BIT_MBID_BCNQ0_EN_8197F BIT(0)

/* 2 REG_P2PPS_CTRL_8197F */
#define BIT_P2P_CTW_ALLSTASLEEP_8197F BIT(7)
#define BIT_P2P_OFF_DISTX_EN_8197F BIT(6)
#define BIT_PWR_MGT_EN_8197F BIT(5)
#define BIT_P2P_NOA1_EN_8197F BIT(2)
#define BIT_P2P_NOA0_EN_8197F BIT(1)

/* 2 REG_PKT_LIFETIME_CTRL_8197F */
#define BIT_EN_TBTT_AREA_FOR_BB_8197F BIT(23)
#define BIT_EN_BKF_CLR_TXREQ_8197F BIT(22)
#define BIT_EN_TSFBIT32_RST_P2P_8197F BIT(21)
#define BIT_EN_BCN_TX_BTCCA_8197F BIT(20)
#define BIT_DIS_PKT_TX_ATIM_8197F BIT(19)
#define BIT_DIS_BCN_DIS_CTN_8197F BIT(18)
#define BIT_EN_NAVEND_RST_TXOP_8197F BIT(17)
#define BIT_EN_FILTER_CCA_8197F BIT(16)

#define BIT_SHIFT_CCA_FILTER_THRS_8197F 8
#define BIT_MASK_CCA_FILTER_THRS_8197F 0xff
#define BIT_CCA_FILTER_THRS_8197F(x)                                           \
	(((x) & BIT_MASK_CCA_FILTER_THRS_8197F)                                \
	 << BIT_SHIFT_CCA_FILTER_THRS_8197F)
#define BITS_CCA_FILTER_THRS_8197F                                             \
	(BIT_MASK_CCA_FILTER_THRS_8197F << BIT_SHIFT_CCA_FILTER_THRS_8197F)
#define BIT_CLEAR_CCA_FILTER_THRS_8197F(x) ((x) & (~BITS_CCA_FILTER_THRS_8197F))
#define BIT_GET_CCA_FILTER_THRS_8197F(x)                                       \
	(((x) >> BIT_SHIFT_CCA_FILTER_THRS_8197F) &                            \
	 BIT_MASK_CCA_FILTER_THRS_8197F)
#define BIT_SET_CCA_FILTER_THRS_8197F(x, v)                                    \
	(BIT_CLEAR_CCA_FILTER_THRS_8197F(x) | BIT_CCA_FILTER_THRS_8197F(v))

#define BIT_SHIFT_EDCCA_THRS_8197F 0
#define BIT_MASK_EDCCA_THRS_8197F 0xff
#define BIT_EDCCA_THRS_8197F(x)                                                \
	(((x) & BIT_MASK_EDCCA_THRS_8197F) << BIT_SHIFT_EDCCA_THRS_8197F)
#define BITS_EDCCA_THRS_8197F                                                  \
	(BIT_MASK_EDCCA_THRS_8197F << BIT_SHIFT_EDCCA_THRS_8197F)
#define BIT_CLEAR_EDCCA_THRS_8197F(x) ((x) & (~BITS_EDCCA_THRS_8197F))
#define BIT_GET_EDCCA_THRS_8197F(x)                                            \
	(((x) >> BIT_SHIFT_EDCCA_THRS_8197F) & BIT_MASK_EDCCA_THRS_8197F)
#define BIT_SET_EDCCA_THRS_8197F(x, v)                                         \
	(BIT_CLEAR_EDCCA_THRS_8197F(x) | BIT_EDCCA_THRS_8197F(v))

/* 2 REG_P2PPS_SPEC_STATE_8197F */
#define BIT_SPEC_POWER_STATE_8197F BIT(7)
#define BIT_SPEC_CTWINDOW_ON_8197F BIT(6)
#define BIT_SPEC_BEACON_AREA_ON_8197F BIT(5)
#define BIT_SPEC_CTWIN_EARLY_DISTX_8197F BIT(4)
#define BIT_SPEC_NOA1_OFF_PERIOD_8197F BIT(3)
#define BIT_SPEC_FORCE_DOZE1_8197F BIT(2)
#define BIT_SPEC_NOA0_OFF_PERIOD_8197F BIT(1)
#define BIT_SPEC_FORCE_DOZE0_8197F BIT(0)

/* 2 REG_NOT_VALID_8197F */

#define BIT_SHIFT_P2PON_DIS_TXTIME_8197F 0
#define BIT_MASK_P2PON_DIS_TXTIME_8197F 0xff
#define BIT_P2PON_DIS_TXTIME_8197F(x)                                          \
	(((x) & BIT_MASK_P2PON_DIS_TXTIME_8197F)                               \
	 << BIT_SHIFT_P2PON_DIS_TXTIME_8197F)
#define BITS_P2PON_DIS_TXTIME_8197F                                            \
	(BIT_MASK_P2PON_DIS_TXTIME_8197F << BIT_SHIFT_P2PON_DIS_TXTIME_8197F)
#define BIT_CLEAR_P2PON_DIS_TXTIME_8197F(x)                                    \
	((x) & (~BITS_P2PON_DIS_TXTIME_8197F))
#define BIT_GET_P2PON_DIS_TXTIME_8197F(x)                                      \
	(((x) >> BIT_SHIFT_P2PON_DIS_TXTIME_8197F) &                           \
	 BIT_MASK_P2PON_DIS_TXTIME_8197F)
#define BIT_SET_P2PON_DIS_TXTIME_8197F(x, v)                                   \
	(BIT_CLEAR_P2PON_DIS_TXTIME_8197F(x) | BIT_P2PON_DIS_TXTIME_8197F(v))

/* 2 REG_QUEUE_INCOL_THR_8197F */

#define BIT_SHIFT_BK_QUEUE_THR_8197F 24
#define BIT_MASK_BK_QUEUE_THR_8197F 0xff
#define BIT_BK_QUEUE_THR_8197F(x)                                              \
	(((x) & BIT_MASK_BK_QUEUE_THR_8197F) << BIT_SHIFT_BK_QUEUE_THR_8197F)
#define BITS_BK_QUEUE_THR_8197F                                                \
	(BIT_MASK_BK_QUEUE_THR_8197F << BIT_SHIFT_BK_QUEUE_THR_8197F)
#define BIT_CLEAR_BK_QUEUE_THR_8197F(x) ((x) & (~BITS_BK_QUEUE_THR_8197F))
#define BIT_GET_BK_QUEUE_THR_8197F(x)                                          \
	(((x) >> BIT_SHIFT_BK_QUEUE_THR_8197F) & BIT_MASK_BK_QUEUE_THR_8197F)
#define BIT_SET_BK_QUEUE_THR_8197F(x, v)                                       \
	(BIT_CLEAR_BK_QUEUE_THR_8197F(x) | BIT_BK_QUEUE_THR_8197F(v))

#define BIT_SHIFT_BE_QUEUE_THR_8197F 16
#define BIT_MASK_BE_QUEUE_THR_8197F 0xff
#define BIT_BE_QUEUE_THR_8197F(x)                                              \
	(((x) & BIT_MASK_BE_QUEUE_THR_8197F) << BIT_SHIFT_BE_QUEUE_THR_8197F)
#define BITS_BE_QUEUE_THR_8197F                                                \
	(BIT_MASK_BE_QUEUE_THR_8197F << BIT_SHIFT_BE_QUEUE_THR_8197F)
#define BIT_CLEAR_BE_QUEUE_THR_8197F(x) ((x) & (~BITS_BE_QUEUE_THR_8197F))
#define BIT_GET_BE_QUEUE_THR_8197F(x)                                          \
	(((x) >> BIT_SHIFT_BE_QUEUE_THR_8197F) & BIT_MASK_BE_QUEUE_THR_8197F)
#define BIT_SET_BE_QUEUE_THR_8197F(x, v)                                       \
	(BIT_CLEAR_BE_QUEUE_THR_8197F(x) | BIT_BE_QUEUE_THR_8197F(v))

#define BIT_SHIFT_VI_QUEUE_THR_8197F 8
#define BIT_MASK_VI_QUEUE_THR_8197F 0xff
#define BIT_VI_QUEUE_THR_8197F(x)                                              \
	(((x) & BIT_MASK_VI_QUEUE_THR_8197F) << BIT_SHIFT_VI_QUEUE_THR_8197F)
#define BITS_VI_QUEUE_THR_8197F                                                \
	(BIT_MASK_VI_QUEUE_THR_8197F << BIT_SHIFT_VI_QUEUE_THR_8197F)
#define BIT_CLEAR_VI_QUEUE_THR_8197F(x) ((x) & (~BITS_VI_QUEUE_THR_8197F))
#define BIT_GET_VI_QUEUE_THR_8197F(x)                                          \
	(((x) >> BIT_SHIFT_VI_QUEUE_THR_8197F) & BIT_MASK_VI_QUEUE_THR_8197F)
#define BIT_SET_VI_QUEUE_THR_8197F(x, v)                                       \
	(BIT_CLEAR_VI_QUEUE_THR_8197F(x) | BIT_VI_QUEUE_THR_8197F(v))

#define BIT_SHIFT_VO_QUEUE_THR_8197F 0
#define BIT_MASK_VO_QUEUE_THR_8197F 0xff
#define BIT_VO_QUEUE_THR_8197F(x)                                              \
	(((x) & BIT_MASK_VO_QUEUE_THR_8197F) << BIT_SHIFT_VO_QUEUE_THR_8197F)
#define BITS_VO_QUEUE_THR_8197F                                                \
	(BIT_MASK_VO_QUEUE_THR_8197F << BIT_SHIFT_VO_QUEUE_THR_8197F)
#define BIT_CLEAR_VO_QUEUE_THR_8197F(x) ((x) & (~BITS_VO_QUEUE_THR_8197F))
#define BIT_GET_VO_QUEUE_THR_8197F(x)                                          \
	(((x) >> BIT_SHIFT_VO_QUEUE_THR_8197F) & BIT_MASK_VO_QUEUE_THR_8197F)
#define BIT_SET_VO_QUEUE_THR_8197F(x, v)                                       \
	(BIT_CLEAR_VO_QUEUE_THR_8197F(x) | BIT_VO_QUEUE_THR_8197F(v))

/* 2 REG_QUEUE_INCOL_EN_8197F */
#define BIT_QUEUE_INCOL_EN_8197F BIT(16)

#define BIT_SHIFT_BK_TRIGGER_NUM_V1_8197F 12
#define BIT_MASK_BK_TRIGGER_NUM_V1_8197F 0xf
#define BIT_BK_TRIGGER_NUM_V1_8197F(x)                                         \
	(((x) & BIT_MASK_BK_TRIGGER_NUM_V1_8197F)                              \
	 << BIT_SHIFT_BK_TRIGGER_NUM_V1_8197F)
#define BITS_BK_TRIGGER_NUM_V1_8197F                                           \
	(BIT_MASK_BK_TRIGGER_NUM_V1_8197F << BIT_SHIFT_BK_TRIGGER_NUM_V1_8197F)
#define BIT_CLEAR_BK_TRIGGER_NUM_V1_8197F(x)                                   \
	((x) & (~BITS_BK_TRIGGER_NUM_V1_8197F))
#define BIT_GET_BK_TRIGGER_NUM_V1_8197F(x)                                     \
	(((x) >> BIT_SHIFT_BK_TRIGGER_NUM_V1_8197F) &                          \
	 BIT_MASK_BK_TRIGGER_NUM_V1_8197F)
#define BIT_SET_BK_TRIGGER_NUM_V1_8197F(x, v)                                  \
	(BIT_CLEAR_BK_TRIGGER_NUM_V1_8197F(x) | BIT_BK_TRIGGER_NUM_V1_8197F(v))

#define BIT_SHIFT_BE_TRIGGER_NUM_V1_8197F 8
#define BIT_MASK_BE_TRIGGER_NUM_V1_8197F 0xf
#define BIT_BE_TRIGGER_NUM_V1_8197F(x)                                         \
	(((x) & BIT_MASK_BE_TRIGGER_NUM_V1_8197F)                              \
	 << BIT_SHIFT_BE_TRIGGER_NUM_V1_8197F)
#define BITS_BE_TRIGGER_NUM_V1_8197F                                           \
	(BIT_MASK_BE_TRIGGER_NUM_V1_8197F << BIT_SHIFT_BE_TRIGGER_NUM_V1_8197F)
#define BIT_CLEAR_BE_TRIGGER_NUM_V1_8197F(x)                                   \
	((x) & (~BITS_BE_TRIGGER_NUM_V1_8197F))
#define BIT_GET_BE_TRIGGER_NUM_V1_8197F(x)                                     \
	(((x) >> BIT_SHIFT_BE_TRIGGER_NUM_V1_8197F) &                          \
	 BIT_MASK_BE_TRIGGER_NUM_V1_8197F)
#define BIT_SET_BE_TRIGGER_NUM_V1_8197F(x, v)                                  \
	(BIT_CLEAR_BE_TRIGGER_NUM_V1_8197F(x) | BIT_BE_TRIGGER_NUM_V1_8197F(v))

#define BIT_SHIFT_VI_TRIGGER_NUM_8197F 4
#define BIT_MASK_VI_TRIGGER_NUM_8197F 0xf
#define BIT_VI_TRIGGER_NUM_8197F(x)                                            \
	(((x) & BIT_MASK_VI_TRIGGER_NUM_8197F)                                 \
	 << BIT_SHIFT_VI_TRIGGER_NUM_8197F)
#define BITS_VI_TRIGGER_NUM_8197F                                              \
	(BIT_MASK_VI_TRIGGER_NUM_8197F << BIT_SHIFT_VI_TRIGGER_NUM_8197F)
#define BIT_CLEAR_VI_TRIGGER_NUM_8197F(x) ((x) & (~BITS_VI_TRIGGER_NUM_8197F))
#define BIT_GET_VI_TRIGGER_NUM_8197F(x)                                        \
	(((x) >> BIT_SHIFT_VI_TRIGGER_NUM_8197F) &                             \
	 BIT_MASK_VI_TRIGGER_NUM_8197F)
#define BIT_SET_VI_TRIGGER_NUM_8197F(x, v)                                     \
	(BIT_CLEAR_VI_TRIGGER_NUM_8197F(x) | BIT_VI_TRIGGER_NUM_8197F(v))

#define BIT_SHIFT_VO_TRIGGER_NUM_8197F 0
#define BIT_MASK_VO_TRIGGER_NUM_8197F 0xf
#define BIT_VO_TRIGGER_NUM_8197F(x)                                            \
	(((x) & BIT_MASK_VO_TRIGGER_NUM_8197F)                                 \
	 << BIT_SHIFT_VO_TRIGGER_NUM_8197F)
#define BITS_VO_TRIGGER_NUM_8197F                                              \
	(BIT_MASK_VO_TRIGGER_NUM_8197F << BIT_SHIFT_VO_TRIGGER_NUM_8197F)
#define BIT_CLEAR_VO_TRIGGER_NUM_8197F(x) ((x) & (~BITS_VO_TRIGGER_NUM_8197F))
#define BIT_GET_VO_TRIGGER_NUM_8197F(x)                                        \
	(((x) >> BIT_SHIFT_VO_TRIGGER_NUM_8197F) &                             \
	 BIT_MASK_VO_TRIGGER_NUM_8197F)
#define BIT_SET_VO_TRIGGER_NUM_8197F(x, v)                                     \
	(BIT_CLEAR_VO_TRIGGER_NUM_8197F(x) | BIT_VO_TRIGGER_NUM_8197F(v))

/* 2 REG_TBTT_PROHIBIT_8197F */

#define BIT_SHIFT_TBTT_HOLD_TIME_AP_8197F 8
#define BIT_MASK_TBTT_HOLD_TIME_AP_8197F 0xfff
#define BIT_TBTT_HOLD_TIME_AP_8197F(x)                                         \
	(((x) & BIT_MASK_TBTT_HOLD_TIME_AP_8197F)                              \
	 << BIT_SHIFT_TBTT_HOLD_TIME_AP_8197F)
#define BITS_TBTT_HOLD_TIME_AP_8197F                                           \
	(BIT_MASK_TBTT_HOLD_TIME_AP_8197F << BIT_SHIFT_TBTT_HOLD_TIME_AP_8197F)
#define BIT_CLEAR_TBTT_HOLD_TIME_AP_8197F(x)                                   \
	((x) & (~BITS_TBTT_HOLD_TIME_AP_8197F))
#define BIT_GET_TBTT_HOLD_TIME_AP_8197F(x)                                     \
	(((x) >> BIT_SHIFT_TBTT_HOLD_TIME_AP_8197F) &                          \
	 BIT_MASK_TBTT_HOLD_TIME_AP_8197F)
#define BIT_SET_TBTT_HOLD_TIME_AP_8197F(x, v)                                  \
	(BIT_CLEAR_TBTT_HOLD_TIME_AP_8197F(x) | BIT_TBTT_HOLD_TIME_AP_8197F(v))

#define BIT_SHIFT_TBTT_PROHIBIT_SETUP_8197F 0
#define BIT_MASK_TBTT_PROHIBIT_SETUP_8197F 0xf
#define BIT_TBTT_PROHIBIT_SETUP_8197F(x)                                       \
	(((x) & BIT_MASK_TBTT_PROHIBIT_SETUP_8197F)                            \
	 << BIT_SHIFT_TBTT_PROHIBIT_SETUP_8197F)
#define BITS_TBTT_PROHIBIT_SETUP_8197F                                         \
	(BIT_MASK_TBTT_PROHIBIT_SETUP_8197F                                    \
	 << BIT_SHIFT_TBTT_PROHIBIT_SETUP_8197F)
#define BIT_CLEAR_TBTT_PROHIBIT_SETUP_8197F(x)                                 \
	((x) & (~BITS_TBTT_PROHIBIT_SETUP_8197F))
#define BIT_GET_TBTT_PROHIBIT_SETUP_8197F(x)                                   \
	(((x) >> BIT_SHIFT_TBTT_PROHIBIT_SETUP_8197F) &                        \
	 BIT_MASK_TBTT_PROHIBIT_SETUP_8197F)
#define BIT_SET_TBTT_PROHIBIT_SETUP_8197F(x, v)                                \
	(BIT_CLEAR_TBTT_PROHIBIT_SETUP_8197F(x) |                              \
	 BIT_TBTT_PROHIBIT_SETUP_8197F(v))

/* 2 REG_P2PPS_STATE_8197F */
#define BIT_POWER_STATE_8197F BIT(7)
#define BIT_CTWINDOW_ON_8197F BIT(6)
#define BIT_BEACON_AREA_ON_8197F BIT(5)
#define BIT_CTWIN_EARLY_DISTX_8197F BIT(4)
#define BIT_NOA1_OFF_PERIOD_8197F BIT(3)
#define BIT_FORCE_DOZE1_8197F BIT(2)
#define BIT_NOA0_OFF_PERIOD_8197F BIT(1)
#define BIT_FORCE_DOZE0_8197F BIT(0)

/* 2 REG_RD_NAV_NXT_8197F */

#define BIT_SHIFT_RD_NAV_PROT_NXT_8197F 0
#define BIT_MASK_RD_NAV_PROT_NXT_8197F 0xffff
#define BIT_RD_NAV_PROT_NXT_8197F(x)                                           \
	(((x) & BIT_MASK_RD_NAV_PROT_NXT_8197F)                                \
	 << BIT_SHIFT_RD_NAV_PROT_NXT_8197F)
#define BITS_RD_NAV_PROT_NXT_8197F                                             \
	(BIT_MASK_RD_NAV_PROT_NXT_8197F << BIT_SHIFT_RD_NAV_PROT_NXT_8197F)
#define BIT_CLEAR_RD_NAV_PROT_NXT_8197F(x) ((x) & (~BITS_RD_NAV_PROT_NXT_8197F))
#define BIT_GET_RD_NAV_PROT_NXT_8197F(x)                                       \
	(((x) >> BIT_SHIFT_RD_NAV_PROT_NXT_8197F) &                            \
	 BIT_MASK_RD_NAV_PROT_NXT_8197F)
#define BIT_SET_RD_NAV_PROT_NXT_8197F(x, v)                                    \
	(BIT_CLEAR_RD_NAV_PROT_NXT_8197F(x) | BIT_RD_NAV_PROT_NXT_8197F(v))

/* 2 REG_NAV_PROT_LEN_8197F */

#define BIT_SHIFT_NAV_PROT_LEN_8197F 0
#define BIT_MASK_NAV_PROT_LEN_8197F 0xffff
#define BIT_NAV_PROT_LEN_8197F(x)                                              \
	(((x) & BIT_MASK_NAV_PROT_LEN_8197F) << BIT_SHIFT_NAV_PROT_LEN_8197F)
#define BITS_NAV_PROT_LEN_8197F                                                \
	(BIT_MASK_NAV_PROT_LEN_8197F << BIT_SHIFT_NAV_PROT_LEN_8197F)
#define BIT_CLEAR_NAV_PROT_LEN_8197F(x) ((x) & (~BITS_NAV_PROT_LEN_8197F))
#define BIT_GET_NAV_PROT_LEN_8197F(x)                                          \
	(((x) >> BIT_SHIFT_NAV_PROT_LEN_8197F) & BIT_MASK_NAV_PROT_LEN_8197F)
#define BIT_SET_NAV_PROT_LEN_8197F(x, v)                                       \
	(BIT_CLEAR_NAV_PROT_LEN_8197F(x) | BIT_NAV_PROT_LEN_8197F(v))

/* 2 REG_FTM_CTRL_8197F */

#define BIT_SHIFT_FTM_TSF_R2T_PORT_8197F 22
#define BIT_MASK_FTM_TSF_R2T_PORT_8197F 0x7
#define BIT_FTM_TSF_R2T_PORT_8197F(x)                                          \
	(((x) & BIT_MASK_FTM_TSF_R2T_PORT_8197F)                               \
	 << BIT_SHIFT_FTM_TSF_R2T_PORT_8197F)
#define BITS_FTM_TSF_R2T_PORT_8197F                                            \
	(BIT_MASK_FTM_TSF_R2T_PORT_8197F << BIT_SHIFT_FTM_TSF_R2T_PORT_8197F)
#define BIT_CLEAR_FTM_TSF_R2T_PORT_8197F(x)                                    \
	((x) & (~BITS_FTM_TSF_R2T_PORT_8197F))
#define BIT_GET_FTM_TSF_R2T_PORT_8197F(x)                                      \
	(((x) >> BIT_SHIFT_FTM_TSF_R2T_PORT_8197F) &                           \
	 BIT_MASK_FTM_TSF_R2T_PORT_8197F)
#define BIT_SET_FTM_TSF_R2T_PORT_8197F(x, v)                                   \
	(BIT_CLEAR_FTM_TSF_R2T_PORT_8197F(x) | BIT_FTM_TSF_R2T_PORT_8197F(v))

#define BIT_SHIFT_FTM_TSF_T2R_PORT_8197F 19
#define BIT_MASK_FTM_TSF_T2R_PORT_8197F 0x7
#define BIT_FTM_TSF_T2R_PORT_8197F(x)                                          \
	(((x) & BIT_MASK_FTM_TSF_T2R_PORT_8197F)                               \
	 << BIT_SHIFT_FTM_TSF_T2R_PORT_8197F)
#define BITS_FTM_TSF_T2R_PORT_8197F                                            \
	(BIT_MASK_FTM_TSF_T2R_PORT_8197F << BIT_SHIFT_FTM_TSF_T2R_PORT_8197F)
#define BIT_CLEAR_FTM_TSF_T2R_PORT_8197F(x)                                    \
	((x) & (~BITS_FTM_TSF_T2R_PORT_8197F))
#define BIT_GET_FTM_TSF_T2R_PORT_8197F(x)                                      \
	(((x) >> BIT_SHIFT_FTM_TSF_T2R_PORT_8197F) &                           \
	 BIT_MASK_FTM_TSF_T2R_PORT_8197F)
#define BIT_SET_FTM_TSF_T2R_PORT_8197F(x, v)                                   \
	(BIT_CLEAR_FTM_TSF_T2R_PORT_8197F(x) | BIT_FTM_TSF_T2R_PORT_8197F(v))

#define BIT_SHIFT_FTM_PTT_PORT_8197F 16
#define BIT_MASK_FTM_PTT_PORT_8197F 0x7
#define BIT_FTM_PTT_PORT_8197F(x)                                              \
	(((x) & BIT_MASK_FTM_PTT_PORT_8197F) << BIT_SHIFT_FTM_PTT_PORT_8197F)
#define BITS_FTM_PTT_PORT_8197F                                                \
	(BIT_MASK_FTM_PTT_PORT_8197F << BIT_SHIFT_FTM_PTT_PORT_8197F)
#define BIT_CLEAR_FTM_PTT_PORT_8197F(x) ((x) & (~BITS_FTM_PTT_PORT_8197F))
#define BIT_GET_FTM_PTT_PORT_8197F(x)                                          \
	(((x) >> BIT_SHIFT_FTM_PTT_PORT_8197F) & BIT_MASK_FTM_PTT_PORT_8197F)
#define BIT_SET_FTM_PTT_PORT_8197F(x, v)                                       \
	(BIT_CLEAR_FTM_PTT_PORT_8197F(x) | BIT_FTM_PTT_PORT_8197F(v))

#define BIT_SHIFT_FTM_PTT_8197F 0
#define BIT_MASK_FTM_PTT_8197F 0xffff
#define BIT_FTM_PTT_8197F(x)                                                   \
	(((x) & BIT_MASK_FTM_PTT_8197F) << BIT_SHIFT_FTM_PTT_8197F)
#define BITS_FTM_PTT_8197F (BIT_MASK_FTM_PTT_8197F << BIT_SHIFT_FTM_PTT_8197F)
#define BIT_CLEAR_FTM_PTT_8197F(x) ((x) & (~BITS_FTM_PTT_8197F))
#define BIT_GET_FTM_PTT_8197F(x)                                               \
	(((x) >> BIT_SHIFT_FTM_PTT_8197F) & BIT_MASK_FTM_PTT_8197F)
#define BIT_SET_FTM_PTT_8197F(x, v)                                            \
	(BIT_CLEAR_FTM_PTT_8197F(x) | BIT_FTM_PTT_8197F(v))

/* 2 REG_FTM_TSF_CNT_8197F */

#define BIT_SHIFT_FTM_TSF_R2T_8197F 16
#define BIT_MASK_FTM_TSF_R2T_8197F 0xffff
#define BIT_FTM_TSF_R2T_8197F(x)                                               \
	(((x) & BIT_MASK_FTM_TSF_R2T_8197F) << BIT_SHIFT_FTM_TSF_R2T_8197F)
#define BITS_FTM_TSF_R2T_8197F                                                 \
	(BIT_MASK_FTM_TSF_R2T_8197F << BIT_SHIFT_FTM_TSF_R2T_8197F)
#define BIT_CLEAR_FTM_TSF_R2T_8197F(x) ((x) & (~BITS_FTM_TSF_R2T_8197F))
#define BIT_GET_FTM_TSF_R2T_8197F(x)                                           \
	(((x) >> BIT_SHIFT_FTM_TSF_R2T_8197F) & BIT_MASK_FTM_TSF_R2T_8197F)
#define BIT_SET_FTM_TSF_R2T_8197F(x, v)                                        \
	(BIT_CLEAR_FTM_TSF_R2T_8197F(x) | BIT_FTM_TSF_R2T_8197F(v))

#define BIT_SHIFT_FTM_TSF_T2R_8197F 0
#define BIT_MASK_FTM_TSF_T2R_8197F 0xffff
#define BIT_FTM_TSF_T2R_8197F(x)                                               \
	(((x) & BIT_MASK_FTM_TSF_T2R_8197F) << BIT_SHIFT_FTM_TSF_T2R_8197F)
#define BITS_FTM_TSF_T2R_8197F                                                 \
	(BIT_MASK_FTM_TSF_T2R_8197F << BIT_SHIFT_FTM_TSF_T2R_8197F)
#define BIT_CLEAR_FTM_TSF_T2R_8197F(x) ((x) & (~BITS_FTM_TSF_T2R_8197F))
#define BIT_GET_FTM_TSF_T2R_8197F(x)                                           \
	(((x) >> BIT_SHIFT_FTM_TSF_T2R_8197F) & BIT_MASK_FTM_TSF_T2R_8197F)
#define BIT_SET_FTM_TSF_T2R_8197F(x, v)                                        \
	(BIT_CLEAR_FTM_TSF_T2R_8197F(x) | BIT_FTM_TSF_T2R_8197F(v))

/* 2 REG_BCN_CTRL_8197F */
#define BIT_DIS_RX_BSSID_FIT_8197F BIT(6)
#define BIT_P0_EN_TXBCN_RPT_8197F BIT(5)
#define BIT_DIS_TSF_UDT_8197F BIT(4)
#define BIT_EN_BCN_FUNCTION_8197F BIT(3)
#define BIT_P0_EN_RXBCN_RPT_8197F BIT(2)
#define BIT_EN_P2P_CTWINDOW_8197F BIT(1)
#define BIT_EN_P2P_BCNQ_AREA_8197F BIT(0)

/* 2 REG_BCN_CTRL_CLINT0_8197F */
#define BIT_CLI0_DIS_RX_BSSID_FIT_8197F BIT(6)
#define BIT_CLI0_DIS_TSF_UDT_8197F BIT(4)
#define BIT_CLI0_EN_BCN_FUNCTION_8197F BIT(3)
#define BIT_CLI0_EN_RXBCN_RPT_8197F BIT(2)
#define BIT_CLI0_ENP2P_CTWINDOW_8197F BIT(1)
#define BIT_CLI0_ENP2P_BCNQ_AREA_8197F BIT(0)

/* 2 REG_MBID_NUM_8197F */
#define BIT_EN_PRE_DL_BEACON_8197F BIT(3)

#define BIT_SHIFT_MBID_BCN_NUM_8197F 0
#define BIT_MASK_MBID_BCN_NUM_8197F 0x7
#define BIT_MBID_BCN_NUM_8197F(x)                                              \
	(((x) & BIT_MASK_MBID_BCN_NUM_8197F) << BIT_SHIFT_MBID_BCN_NUM_8197F)
#define BITS_MBID_BCN_NUM_8197F                                                \
	(BIT_MASK_MBID_BCN_NUM_8197F << BIT_SHIFT_MBID_BCN_NUM_8197F)
#define BIT_CLEAR_MBID_BCN_NUM_8197F(x) ((x) & (~BITS_MBID_BCN_NUM_8197F))
#define BIT_GET_MBID_BCN_NUM_8197F(x)                                          \
	(((x) >> BIT_SHIFT_MBID_BCN_NUM_8197F) & BIT_MASK_MBID_BCN_NUM_8197F)
#define BIT_SET_MBID_BCN_NUM_8197F(x, v)                                       \
	(BIT_CLEAR_MBID_BCN_NUM_8197F(x) | BIT_MBID_BCN_NUM_8197F(v))

/* 2 REG_DUAL_TSF_RST_8197F */
#define BIT_FREECNT_RST_8197F BIT(5)
#define BIT_TSFTR_CLI3_RST_8197F BIT(4)
#define BIT_TSFTR_CLI2_RST_8197F BIT(3)
#define BIT_TSFTR_CLI1_RST_8197F BIT(2)
#define BIT_TSFTR_CLI0_RST_8197F BIT(1)
#define BIT_TSFTR_RST_8197F BIT(0)

/* 2 REG_MBSSID_BCN_SPACE_8197F */

#define BIT_SHIFT_BCN_TIMER_SEL_FWRD_8197F 28
#define BIT_MASK_BCN_TIMER_SEL_FWRD_8197F 0x7
#define BIT_BCN_TIMER_SEL_FWRD_8197F(x)                                        \
	(((x) & BIT_MASK_BCN_TIMER_SEL_FWRD_8197F)                             \
	 << BIT_SHIFT_BCN_TIMER_SEL_FWRD_8197F)
#define BITS_BCN_TIMER_SEL_FWRD_8197F                                          \
	(BIT_MASK_BCN_TIMER_SEL_FWRD_8197F                                     \
	 << BIT_SHIFT_BCN_TIMER_SEL_FWRD_8197F)
#define BIT_CLEAR_BCN_TIMER_SEL_FWRD_8197F(x)                                  \
	((x) & (~BITS_BCN_TIMER_SEL_FWRD_8197F))
#define BIT_GET_BCN_TIMER_SEL_FWRD_8197F(x)                                    \
	(((x) >> BIT_SHIFT_BCN_TIMER_SEL_FWRD_8197F) &                         \
	 BIT_MASK_BCN_TIMER_SEL_FWRD_8197F)
#define BIT_SET_BCN_TIMER_SEL_FWRD_8197F(x, v)                                 \
	(BIT_CLEAR_BCN_TIMER_SEL_FWRD_8197F(x) |                               \
	 BIT_BCN_TIMER_SEL_FWRD_8197F(v))

#define BIT_SHIFT_BCN_SPACE_CLINT0_8197F 16
#define BIT_MASK_BCN_SPACE_CLINT0_8197F 0xfff
#define BIT_BCN_SPACE_CLINT0_8197F(x)                                          \
	(((x) & BIT_MASK_BCN_SPACE_CLINT0_8197F)                               \
	 << BIT_SHIFT_BCN_SPACE_CLINT0_8197F)
#define BITS_BCN_SPACE_CLINT0_8197F                                            \
	(BIT_MASK_BCN_SPACE_CLINT0_8197F << BIT_SHIFT_BCN_SPACE_CLINT0_8197F)
#define BIT_CLEAR_BCN_SPACE_CLINT0_8197F(x)                                    \
	((x) & (~BITS_BCN_SPACE_CLINT0_8197F))
#define BIT_GET_BCN_SPACE_CLINT0_8197F(x)                                      \
	(((x) >> BIT_SHIFT_BCN_SPACE_CLINT0_8197F) &                           \
	 BIT_MASK_BCN_SPACE_CLINT0_8197F)
#define BIT_SET_BCN_SPACE_CLINT0_8197F(x, v)                                   \
	(BIT_CLEAR_BCN_SPACE_CLINT0_8197F(x) | BIT_BCN_SPACE_CLINT0_8197F(v))

#define BIT_SHIFT_BCN_SPACE0_8197F 0
#define BIT_MASK_BCN_SPACE0_8197F 0xffff
#define BIT_BCN_SPACE0_8197F(x)                                                \
	(((x) & BIT_MASK_BCN_SPACE0_8197F) << BIT_SHIFT_BCN_SPACE0_8197F)
#define BITS_BCN_SPACE0_8197F                                                  \
	(BIT_MASK_BCN_SPACE0_8197F << BIT_SHIFT_BCN_SPACE0_8197F)
#define BIT_CLEAR_BCN_SPACE0_8197F(x) ((x) & (~BITS_BCN_SPACE0_8197F))
#define BIT_GET_BCN_SPACE0_8197F(x)                                            \
	(((x) >> BIT_SHIFT_BCN_SPACE0_8197F) & BIT_MASK_BCN_SPACE0_8197F)
#define BIT_SET_BCN_SPACE0_8197F(x, v)                                         \
	(BIT_CLEAR_BCN_SPACE0_8197F(x) | BIT_BCN_SPACE0_8197F(v))

/* 2 REG_DRVERLYINT_8197F */

#define BIT_SHIFT_DRVERLYITV_8197F 0
#define BIT_MASK_DRVERLYITV_8197F 0xff
#define BIT_DRVERLYITV_8197F(x)                                                \
	(((x) & BIT_MASK_DRVERLYITV_8197F) << BIT_SHIFT_DRVERLYITV_8197F)
#define BITS_DRVERLYITV_8197F                                                  \
	(BIT_MASK_DRVERLYITV_8197F << BIT_SHIFT_DRVERLYITV_8197F)
#define BIT_CLEAR_DRVERLYITV_8197F(x) ((x) & (~BITS_DRVERLYITV_8197F))
#define BIT_GET_DRVERLYITV_8197F(x)                                            \
	(((x) >> BIT_SHIFT_DRVERLYITV_8197F) & BIT_MASK_DRVERLYITV_8197F)
#define BIT_SET_DRVERLYITV_8197F(x, v)                                         \
	(BIT_CLEAR_DRVERLYITV_8197F(x) | BIT_DRVERLYITV_8197F(v))

/* 2 REG_BCNDMATIM_8197F */

#define BIT_SHIFT_BCNDMATIM_8197F 0
#define BIT_MASK_BCNDMATIM_8197F 0xff
#define BIT_BCNDMATIM_8197F(x)                                                 \
	(((x) & BIT_MASK_BCNDMATIM_8197F) << BIT_SHIFT_BCNDMATIM_8197F)
#define BITS_BCNDMATIM_8197F                                                   \
	(BIT_MASK_BCNDMATIM_8197F << BIT_SHIFT_BCNDMATIM_8197F)
#define BIT_CLEAR_BCNDMATIM_8197F(x) ((x) & (~BITS_BCNDMATIM_8197F))
#define BIT_GET_BCNDMATIM_8197F(x)                                             \
	(((x) >> BIT_SHIFT_BCNDMATIM_8197F) & BIT_MASK_BCNDMATIM_8197F)
#define BIT_SET_BCNDMATIM_8197F(x, v)                                          \
	(BIT_CLEAR_BCNDMATIM_8197F(x) | BIT_BCNDMATIM_8197F(v))

/* 2 REG_ATIMWND_8197F */

#define BIT_SHIFT_ATIMWND0_8197F 0
#define BIT_MASK_ATIMWND0_8197F 0xffff
#define BIT_ATIMWND0_8197F(x)                                                  \
	(((x) & BIT_MASK_ATIMWND0_8197F) << BIT_SHIFT_ATIMWND0_8197F)
#define BITS_ATIMWND0_8197F                                                    \
	(BIT_MASK_ATIMWND0_8197F << BIT_SHIFT_ATIMWND0_8197F)
#define BIT_CLEAR_ATIMWND0_8197F(x) ((x) & (~BITS_ATIMWND0_8197F))
#define BIT_GET_ATIMWND0_8197F(x)                                              \
	(((x) >> BIT_SHIFT_ATIMWND0_8197F) & BIT_MASK_ATIMWND0_8197F)
#define BIT_SET_ATIMWND0_8197F(x, v)                                           \
	(BIT_CLEAR_ATIMWND0_8197F(x) | BIT_ATIMWND0_8197F(v))

/* 2 REG_USTIME_TSF_8197F */

#define BIT_SHIFT_USTIME_TSF_V1_8197F 0
#define BIT_MASK_USTIME_TSF_V1_8197F 0xff
#define BIT_USTIME_TSF_V1_8197F(x)                                             \
	(((x) & BIT_MASK_USTIME_TSF_V1_8197F) << BIT_SHIFT_USTIME_TSF_V1_8197F)
#define BITS_USTIME_TSF_V1_8197F                                               \
	(BIT_MASK_USTIME_TSF_V1_8197F << BIT_SHIFT_USTIME_TSF_V1_8197F)
#define BIT_CLEAR_USTIME_TSF_V1_8197F(x) ((x) & (~BITS_USTIME_TSF_V1_8197F))
#define BIT_GET_USTIME_TSF_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_USTIME_TSF_V1_8197F) & BIT_MASK_USTIME_TSF_V1_8197F)
#define BIT_SET_USTIME_TSF_V1_8197F(x, v)                                      \
	(BIT_CLEAR_USTIME_TSF_V1_8197F(x) | BIT_USTIME_TSF_V1_8197F(v))

/* 2 REG_BCN_MAX_ERR_8197F */

#define BIT_SHIFT_BCN_MAX_ERR_8197F 0
#define BIT_MASK_BCN_MAX_ERR_8197F 0xff
#define BIT_BCN_MAX_ERR_8197F(x)                                               \
	(((x) & BIT_MASK_BCN_MAX_ERR_8197F) << BIT_SHIFT_BCN_MAX_ERR_8197F)
#define BITS_BCN_MAX_ERR_8197F                                                 \
	(BIT_MASK_BCN_MAX_ERR_8197F << BIT_SHIFT_BCN_MAX_ERR_8197F)
#define BIT_CLEAR_BCN_MAX_ERR_8197F(x) ((x) & (~BITS_BCN_MAX_ERR_8197F))
#define BIT_GET_BCN_MAX_ERR_8197F(x)                                           \
	(((x) >> BIT_SHIFT_BCN_MAX_ERR_8197F) & BIT_MASK_BCN_MAX_ERR_8197F)
#define BIT_SET_BCN_MAX_ERR_8197F(x, v)                                        \
	(BIT_CLEAR_BCN_MAX_ERR_8197F(x) | BIT_BCN_MAX_ERR_8197F(v))

/* 2 REG_RXTSF_OFFSET_CCK_8197F */

#define BIT_SHIFT_CCK_RXTSF_OFFSET_8197F 0
#define BIT_MASK_CCK_RXTSF_OFFSET_8197F 0xff
#define BIT_CCK_RXTSF_OFFSET_8197F(x)                                          \
	(((x) & BIT_MASK_CCK_RXTSF_OFFSET_8197F)                               \
	 << BIT_SHIFT_CCK_RXTSF_OFFSET_8197F)
#define BITS_CCK_RXTSF_OFFSET_8197F                                            \
	(BIT_MASK_CCK_RXTSF_OFFSET_8197F << BIT_SHIFT_CCK_RXTSF_OFFSET_8197F)
#define BIT_CLEAR_CCK_RXTSF_OFFSET_8197F(x)                                    \
	((x) & (~BITS_CCK_RXTSF_OFFSET_8197F))
#define BIT_GET_CCK_RXTSF_OFFSET_8197F(x)                                      \
	(((x) >> BIT_SHIFT_CCK_RXTSF_OFFSET_8197F) &                           \
	 BIT_MASK_CCK_RXTSF_OFFSET_8197F)
#define BIT_SET_CCK_RXTSF_OFFSET_8197F(x, v)                                   \
	(BIT_CLEAR_CCK_RXTSF_OFFSET_8197F(x) | BIT_CCK_RXTSF_OFFSET_8197F(v))

/* 2 REG_RXTSF_OFFSET_OFDM_8197F */

#define BIT_SHIFT_OFDM_RXTSF_OFFSET_8197F 0
#define BIT_MASK_OFDM_RXTSF_OFFSET_8197F 0xff
#define BIT_OFDM_RXTSF_OFFSET_8197F(x)                                         \
	(((x) & BIT_MASK_OFDM_RXTSF_OFFSET_8197F)                              \
	 << BIT_SHIFT_OFDM_RXTSF_OFFSET_8197F)
#define BITS_OFDM_RXTSF_OFFSET_8197F                                           \
	(BIT_MASK_OFDM_RXTSF_OFFSET_8197F << BIT_SHIFT_OFDM_RXTSF_OFFSET_8197F)
#define BIT_CLEAR_OFDM_RXTSF_OFFSET_8197F(x)                                   \
	((x) & (~BITS_OFDM_RXTSF_OFFSET_8197F))
#define BIT_GET_OFDM_RXTSF_OFFSET_8197F(x)                                     \
	(((x) >> BIT_SHIFT_OFDM_RXTSF_OFFSET_8197F) &                          \
	 BIT_MASK_OFDM_RXTSF_OFFSET_8197F)
#define BIT_SET_OFDM_RXTSF_OFFSET_8197F(x, v)                                  \
	(BIT_CLEAR_OFDM_RXTSF_OFFSET_8197F(x) | BIT_OFDM_RXTSF_OFFSET_8197F(v))

/* 2 REG_TSFTR_8197F */

#define BIT_SHIFT_TSF_TIMER_8197F 0
#define BIT_MASK_TSF_TIMER_8197F 0xffffffffffffffffL
#define BIT_TSF_TIMER_8197F(x)                                                 \
	(((x) & BIT_MASK_TSF_TIMER_8197F) << BIT_SHIFT_TSF_TIMER_8197F)
#define BITS_TSF_TIMER_8197F                                                   \
	(BIT_MASK_TSF_TIMER_8197F << BIT_SHIFT_TSF_TIMER_8197F)
#define BIT_CLEAR_TSF_TIMER_8197F(x) ((x) & (~BITS_TSF_TIMER_8197F))
#define BIT_GET_TSF_TIMER_8197F(x)                                             \
	(((x) >> BIT_SHIFT_TSF_TIMER_8197F) & BIT_MASK_TSF_TIMER_8197F)
#define BIT_SET_TSF_TIMER_8197F(x, v)                                          \
	(BIT_CLEAR_TSF_TIMER_8197F(x) | BIT_TSF_TIMER_8197F(v))

/* 2 REG_FREERUN_CNT_8197F */

#define BIT_SHIFT_FREERUN_CNT_8197F 0
#define BIT_MASK_FREERUN_CNT_8197F 0xffffffffffffffffL
#define BIT_FREERUN_CNT_8197F(x)                                               \
	(((x) & BIT_MASK_FREERUN_CNT_8197F) << BIT_SHIFT_FREERUN_CNT_8197F)
#define BITS_FREERUN_CNT_8197F                                                 \
	(BIT_MASK_FREERUN_CNT_8197F << BIT_SHIFT_FREERUN_CNT_8197F)
#define BIT_CLEAR_FREERUN_CNT_8197F(x) ((x) & (~BITS_FREERUN_CNT_8197F))
#define BIT_GET_FREERUN_CNT_8197F(x)                                           \
	(((x) >> BIT_SHIFT_FREERUN_CNT_8197F) & BIT_MASK_FREERUN_CNT_8197F)
#define BIT_SET_FREERUN_CNT_8197F(x, v)                                        \
	(BIT_CLEAR_FREERUN_CNT_8197F(x) | BIT_FREERUN_CNT_8197F(v))

/* 2 REG_ATIMWND1_8197F */

#define BIT_SHIFT_ATIMWND1_V1_8197F 0
#define BIT_MASK_ATIMWND1_V1_8197F 0xff
#define BIT_ATIMWND1_V1_8197F(x)                                               \
	(((x) & BIT_MASK_ATIMWND1_V1_8197F) << BIT_SHIFT_ATIMWND1_V1_8197F)
#define BITS_ATIMWND1_V1_8197F                                                 \
	(BIT_MASK_ATIMWND1_V1_8197F << BIT_SHIFT_ATIMWND1_V1_8197F)
#define BIT_CLEAR_ATIMWND1_V1_8197F(x) ((x) & (~BITS_ATIMWND1_V1_8197F))
#define BIT_GET_ATIMWND1_V1_8197F(x)                                           \
	(((x) >> BIT_SHIFT_ATIMWND1_V1_8197F) & BIT_MASK_ATIMWND1_V1_8197F)
#define BIT_SET_ATIMWND1_V1_8197F(x, v)                                        \
	(BIT_CLEAR_ATIMWND1_V1_8197F(x) | BIT_ATIMWND1_V1_8197F(v))

/* 2 REG_TBTT_PROHIBIT_INFRA_8197F */

#define BIT_SHIFT_TBTT_PROHIBIT_INFRA_8197F 0
#define BIT_MASK_TBTT_PROHIBIT_INFRA_8197F 0xff
#define BIT_TBTT_PROHIBIT_INFRA_8197F(x)                                       \
	(((x) & BIT_MASK_TBTT_PROHIBIT_INFRA_8197F)                            \
	 << BIT_SHIFT_TBTT_PROHIBIT_INFRA_8197F)
#define BITS_TBTT_PROHIBIT_INFRA_8197F                                         \
	(BIT_MASK_TBTT_PROHIBIT_INFRA_8197F                                    \
	 << BIT_SHIFT_TBTT_PROHIBIT_INFRA_8197F)
#define BIT_CLEAR_TBTT_PROHIBIT_INFRA_8197F(x)                                 \
	((x) & (~BITS_TBTT_PROHIBIT_INFRA_8197F))
#define BIT_GET_TBTT_PROHIBIT_INFRA_8197F(x)                                   \
	(((x) >> BIT_SHIFT_TBTT_PROHIBIT_INFRA_8197F) &                        \
	 BIT_MASK_TBTT_PROHIBIT_INFRA_8197F)
#define BIT_SET_TBTT_PROHIBIT_INFRA_8197F(x, v)                                \
	(BIT_CLEAR_TBTT_PROHIBIT_INFRA_8197F(x) |                              \
	 BIT_TBTT_PROHIBIT_INFRA_8197F(v))

/* 2 REG_CTWND_8197F */

#define BIT_SHIFT_CTWND_8197F 0
#define BIT_MASK_CTWND_8197F 0xff
#define BIT_CTWND_8197F(x)                                                     \
	(((x) & BIT_MASK_CTWND_8197F) << BIT_SHIFT_CTWND_8197F)
#define BITS_CTWND_8197F (BIT_MASK_CTWND_8197F << BIT_SHIFT_CTWND_8197F)
#define BIT_CLEAR_CTWND_8197F(x) ((x) & (~BITS_CTWND_8197F))
#define BIT_GET_CTWND_8197F(x)                                                 \
	(((x) >> BIT_SHIFT_CTWND_8197F) & BIT_MASK_CTWND_8197F)
#define BIT_SET_CTWND_8197F(x, v)                                              \
	(BIT_CLEAR_CTWND_8197F(x) | BIT_CTWND_8197F(v))

/* 2 REG_BCNIVLCUNT_8197F */

#define BIT_SHIFT_BCNIVLCUNT_8197F 0
#define BIT_MASK_BCNIVLCUNT_8197F 0x7f
#define BIT_BCNIVLCUNT_8197F(x)                                                \
	(((x) & BIT_MASK_BCNIVLCUNT_8197F) << BIT_SHIFT_BCNIVLCUNT_8197F)
#define BITS_BCNIVLCUNT_8197F                                                  \
	(BIT_MASK_BCNIVLCUNT_8197F << BIT_SHIFT_BCNIVLCUNT_8197F)
#define BIT_CLEAR_BCNIVLCUNT_8197F(x) ((x) & (~BITS_BCNIVLCUNT_8197F))
#define BIT_GET_BCNIVLCUNT_8197F(x)                                            \
	(((x) >> BIT_SHIFT_BCNIVLCUNT_8197F) & BIT_MASK_BCNIVLCUNT_8197F)
#define BIT_SET_BCNIVLCUNT_8197F(x, v)                                         \
	(BIT_CLEAR_BCNIVLCUNT_8197F(x) | BIT_BCNIVLCUNT_8197F(v))

/* 2 REG_BCNDROPCTRL_8197F */
#define BIT_BEACON_DROP_EN_8197F BIT(7)

#define BIT_SHIFT_BEACON_DROP_IVL_8197F 0
#define BIT_MASK_BEACON_DROP_IVL_8197F 0x7f
#define BIT_BEACON_DROP_IVL_8197F(x)                                           \
	(((x) & BIT_MASK_BEACON_DROP_IVL_8197F)                                \
	 << BIT_SHIFT_BEACON_DROP_IVL_8197F)
#define BITS_BEACON_DROP_IVL_8197F                                             \
	(BIT_MASK_BEACON_DROP_IVL_8197F << BIT_SHIFT_BEACON_DROP_IVL_8197F)
#define BIT_CLEAR_BEACON_DROP_IVL_8197F(x) ((x) & (~BITS_BEACON_DROP_IVL_8197F))
#define BIT_GET_BEACON_DROP_IVL_8197F(x)                                       \
	(((x) >> BIT_SHIFT_BEACON_DROP_IVL_8197F) &                            \
	 BIT_MASK_BEACON_DROP_IVL_8197F)
#define BIT_SET_BEACON_DROP_IVL_8197F(x, v)                                    \
	(BIT_CLEAR_BEACON_DROP_IVL_8197F(x) | BIT_BEACON_DROP_IVL_8197F(v))

/* 2 REG_HGQ_TIMEOUT_PERIOD_8197F */

#define BIT_SHIFT_HGQ_TIMEOUT_PERIOD_8197F 0
#define BIT_MASK_HGQ_TIMEOUT_PERIOD_8197F 0xff
#define BIT_HGQ_TIMEOUT_PERIOD_8197F(x)                                        \
	(((x) & BIT_MASK_HGQ_TIMEOUT_PERIOD_8197F)                             \
	 << BIT_SHIFT_HGQ_TIMEOUT_PERIOD_8197F)
#define BITS_HGQ_TIMEOUT_PERIOD_8197F                                          \
	(BIT_MASK_HGQ_TIMEOUT_PERIOD_8197F                                     \
	 << BIT_SHIFT_HGQ_TIMEOUT_PERIOD_8197F)
#define BIT_CLEAR_HGQ_TIMEOUT_PERIOD_8197F(x)                                  \
	((x) & (~BITS_HGQ_TIMEOUT_PERIOD_8197F))
#define BIT_GET_HGQ_TIMEOUT_PERIOD_8197F(x)                                    \
	(((x) >> BIT_SHIFT_HGQ_TIMEOUT_PERIOD_8197F) &                         \
	 BIT_MASK_HGQ_TIMEOUT_PERIOD_8197F)
#define BIT_SET_HGQ_TIMEOUT_PERIOD_8197F(x, v)                                 \
	(BIT_CLEAR_HGQ_TIMEOUT_PERIOD_8197F(x) |                               \
	 BIT_HGQ_TIMEOUT_PERIOD_8197F(v))

/* 2 REG_TXCMD_TIMEOUT_PERIOD_8197F */

#define BIT_SHIFT_TXCMD_TIMEOUT_PERIOD_8197F 0
#define BIT_MASK_TXCMD_TIMEOUT_PERIOD_8197F 0xff
#define BIT_TXCMD_TIMEOUT_PERIOD_8197F(x)                                      \
	(((x) & BIT_MASK_TXCMD_TIMEOUT_PERIOD_8197F)                           \
	 << BIT_SHIFT_TXCMD_TIMEOUT_PERIOD_8197F)
#define BITS_TXCMD_TIMEOUT_PERIOD_8197F                                        \
	(BIT_MASK_TXCMD_TIMEOUT_PERIOD_8197F                                   \
	 << BIT_SHIFT_TXCMD_TIMEOUT_PERIOD_8197F)
#define BIT_CLEAR_TXCMD_TIMEOUT_PERIOD_8197F(x)                                \
	((x) & (~BITS_TXCMD_TIMEOUT_PERIOD_8197F))
#define BIT_GET_TXCMD_TIMEOUT_PERIOD_8197F(x)                                  \
	(((x) >> BIT_SHIFT_TXCMD_TIMEOUT_PERIOD_8197F) &                       \
	 BIT_MASK_TXCMD_TIMEOUT_PERIOD_8197F)
#define BIT_SET_TXCMD_TIMEOUT_PERIOD_8197F(x, v)                               \
	(BIT_CLEAR_TXCMD_TIMEOUT_PERIOD_8197F(x) |                             \
	 BIT_TXCMD_TIMEOUT_PERIOD_8197F(v))

/* 2 REG_MISC_CTRL_8197F */
#define BIT_DIS_MARK_TSF_US_8197F BIT(7)
#define BIT_EN_TSFAUTO_SYNC_8197F BIT(6)
#define BIT_DIS_TRX_CAL_BCN_8197F BIT(5)
#define BIT_DIS_TX_CAL_TBTT_8197F BIT(4)
#define BIT_EN_FREECNT_8197F BIT(3)
#define BIT_BCN_AGGRESSION_8197F BIT(2)

#define BIT_SHIFT_DIS_SECONDARY_CCA_8197F 0
#define BIT_MASK_DIS_SECONDARY_CCA_8197F 0x3
#define BIT_DIS_SECONDARY_CCA_8197F(x)                                         \
	(((x) & BIT_MASK_DIS_SECONDARY_CCA_8197F)                              \
	 << BIT_SHIFT_DIS_SECONDARY_CCA_8197F)
#define BITS_DIS_SECONDARY_CCA_8197F                                           \
	(BIT_MASK_DIS_SECONDARY_CCA_8197F << BIT_SHIFT_DIS_SECONDARY_CCA_8197F)
#define BIT_CLEAR_DIS_SECONDARY_CCA_8197F(x)                                   \
	((x) & (~BITS_DIS_SECONDARY_CCA_8197F))
#define BIT_GET_DIS_SECONDARY_CCA_8197F(x)                                     \
	(((x) >> BIT_SHIFT_DIS_SECONDARY_CCA_8197F) &                          \
	 BIT_MASK_DIS_SECONDARY_CCA_8197F)
#define BIT_SET_DIS_SECONDARY_CCA_8197F(x, v)                                  \
	(BIT_CLEAR_DIS_SECONDARY_CCA_8197F(x) | BIT_DIS_SECONDARY_CCA_8197F(v))

/* 2 REG_BCN_CTRL_CLINT1_8197F */
#define BIT_CLI1_DIS_RX_BSSID_FIT_8197F BIT(6)
#define BIT_CLI1_DIS_TSF_UDT_8197F BIT(4)
#define BIT_CLI1_EN_BCN_FUNCTION_8197F BIT(3)
#define BIT_CLI1_EN_RXBCN_RPT_8197F BIT(2)
#define BIT_CLI1_ENP2P_CTWINDOW_8197F BIT(1)
#define BIT_CLI1_ENP2P_BCNQ_AREA_8197F BIT(0)

/* 2 REG_BCN_CTRL_CLINT2_8197F */
#define BIT_CLI2_DIS_RX_BSSID_FIT_8197F BIT(6)
#define BIT_CLI2_DIS_TSF_UDT_8197F BIT(4)
#define BIT_CLI2_EN_BCN_FUNCTION_8197F BIT(3)
#define BIT_CLI2_EN_RXBCN_RPT_8197F BIT(2)
#define BIT_CLI2_ENP2P_CTWINDOW_8197F BIT(1)
#define BIT_CLI2_ENP2P_BCNQ_AREA_8197F BIT(0)

/* 2 REG_BCN_CTRL_CLINT3_8197F */
#define BIT_CLI3_DIS_RX_BSSID_FIT_8197F BIT(6)
#define BIT_CLI3_DIS_TSF_UDT_8197F BIT(4)
#define BIT_CLI3_EN_BCN_FUNCTION_8197F BIT(3)
#define BIT_CLI3_EN_RXBCN_RPT_8197F BIT(2)
#define BIT_CLI3_ENP2P_CTWINDOW_8197F BIT(1)
#define BIT_CLI3_ENP2P_BCNQ_AREA_8197F BIT(0)

/* 2 REG_EXTEND_CTRL_8197F */
#define BIT_EN_TSFBIT32_RST_P2P2_8197F BIT(5)
#define BIT_EN_TSFBIT32_RST_P2P1_8197F BIT(4)

#define BIT_SHIFT_PORT_SEL_8197F 0
#define BIT_MASK_PORT_SEL_8197F 0x7
#define BIT_PORT_SEL_8197F(x)                                                  \
	(((x) & BIT_MASK_PORT_SEL_8197F) << BIT_SHIFT_PORT_SEL_8197F)
#define BITS_PORT_SEL_8197F                                                    \
	(BIT_MASK_PORT_SEL_8197F << BIT_SHIFT_PORT_SEL_8197F)
#define BIT_CLEAR_PORT_SEL_8197F(x) ((x) & (~BITS_PORT_SEL_8197F))
#define BIT_GET_PORT_SEL_8197F(x)                                              \
	(((x) >> BIT_SHIFT_PORT_SEL_8197F) & BIT_MASK_PORT_SEL_8197F)
#define BIT_SET_PORT_SEL_8197F(x, v)                                           \
	(BIT_CLEAR_PORT_SEL_8197F(x) | BIT_PORT_SEL_8197F(v))

/* 2 REG_P2PPS1_SPEC_STATE_8197F */
#define BIT_P2P1_SPEC_POWER_STATE_8197F BIT(7)
#define BIT_P2P1_SPEC_CTWINDOW_ON_8197F BIT(6)
#define BIT_P2P1_SPEC_BCN_AREA_ON_8197F BIT(5)
#define BIT_P2P1_SPEC_CTWIN_EARLY_DISTX_8197F BIT(4)
#define BIT_P2P1_SPEC_NOA1_OFF_PERIOD_8197F BIT(3)
#define BIT_P2P1_SPEC_FORCE_DOZE1_8197F BIT(2)
#define BIT_P2P1_SPEC_NOA0_OFF_PERIOD_8197F BIT(1)
#define BIT_P2P1_SPEC_FORCE_DOZE0_8197F BIT(0)

/* 2 REG_P2PPS1_STATE_8197F */
#define BIT_P2P1_POWER_STATE_8197F BIT(7)
#define BIT_P2P1_CTWINDOW_ON_8197F BIT(6)
#define BIT_P2P1_BEACON_AREA_ON_8197F BIT(5)
#define BIT_P2P1_CTWIN_EARLY_DISTX_8197F BIT(4)
#define BIT_P2P1_NOA1_OFF_PERIOD_8197F BIT(3)
#define BIT_P2P1_FORCE_DOZE1_8197F BIT(2)
#define BIT_P2P1_NOA0_OFF_PERIOD_8197F BIT(1)
#define BIT_P2P1_FORCE_DOZE0_8197F BIT(0)

/* 2 REG_P2PPS2_SPEC_STATE_8197F */
#define BIT_P2P2_SPEC_POWER_STATE_8197F BIT(7)
#define BIT_P2P2_SPEC_CTWINDOW_ON_8197F BIT(6)
#define BIT_P2P2_SPEC_BCN_AREA_ON_8197F BIT(5)
#define BIT_P2P2_SPEC_CTWIN_EARLY_DISTX_8197F BIT(4)
#define BIT_P2P2_SPEC_NOA1_OFF_PERIOD_8197F BIT(3)
#define BIT_P2P2_SPEC_FORCE_DOZE1_8197F BIT(2)
#define BIT_P2P2_SPEC_NOA0_OFF_PERIOD_8197F BIT(1)
#define BIT_P2P2_SPEC_FORCE_DOZE0_8197F BIT(0)

/* 2 REG_P2PPS2_STATE_8197F */
#define BIT_P2P2_POWER_STATE_8197F BIT(7)
#define BIT_P2P2_CTWINDOW_ON_8197F BIT(6)
#define BIT_P2P2_BEACON_AREA_ON_8197F BIT(5)
#define BIT_P2P2_CTWIN_EARLY_DISTX_8197F BIT(4)
#define BIT_P2P2_NOA1_OFF_PERIOD_8197F BIT(3)
#define BIT_P2P2_FORCE_DOZE1_8197F BIT(2)
#define BIT_P2P2_NOA0_OFF_PERIOD_8197F BIT(1)
#define BIT_P2P2_FORCE_DOZE0_8197F BIT(0)

/* 2 REG_PS_TIMER0_8197F */

#define BIT_SHIFT_PSTIMER0_INT_8197F 5
#define BIT_MASK_PSTIMER0_INT_8197F 0x7ffffff
#define BIT_PSTIMER0_INT_8197F(x)                                              \
	(((x) & BIT_MASK_PSTIMER0_INT_8197F) << BIT_SHIFT_PSTIMER0_INT_8197F)
#define BITS_PSTIMER0_INT_8197F                                                \
	(BIT_MASK_PSTIMER0_INT_8197F << BIT_SHIFT_PSTIMER0_INT_8197F)
#define BIT_CLEAR_PSTIMER0_INT_8197F(x) ((x) & (~BITS_PSTIMER0_INT_8197F))
#define BIT_GET_PSTIMER0_INT_8197F(x)                                          \
	(((x) >> BIT_SHIFT_PSTIMER0_INT_8197F) & BIT_MASK_PSTIMER0_INT_8197F)
#define BIT_SET_PSTIMER0_INT_8197F(x, v)                                       \
	(BIT_CLEAR_PSTIMER0_INT_8197F(x) | BIT_PSTIMER0_INT_8197F(v))

/* 2 REG_PS_TIMER1_8197F */

#define BIT_SHIFT_PSTIMER1_INT_8197F 5
#define BIT_MASK_PSTIMER1_INT_8197F 0x7ffffff
#define BIT_PSTIMER1_INT_8197F(x)                                              \
	(((x) & BIT_MASK_PSTIMER1_INT_8197F) << BIT_SHIFT_PSTIMER1_INT_8197F)
#define BITS_PSTIMER1_INT_8197F                                                \
	(BIT_MASK_PSTIMER1_INT_8197F << BIT_SHIFT_PSTIMER1_INT_8197F)
#define BIT_CLEAR_PSTIMER1_INT_8197F(x) ((x) & (~BITS_PSTIMER1_INT_8197F))
#define BIT_GET_PSTIMER1_INT_8197F(x)                                          \
	(((x) >> BIT_SHIFT_PSTIMER1_INT_8197F) & BIT_MASK_PSTIMER1_INT_8197F)
#define BIT_SET_PSTIMER1_INT_8197F(x, v)                                       \
	(BIT_CLEAR_PSTIMER1_INT_8197F(x) | BIT_PSTIMER1_INT_8197F(v))

/* 2 REG_PS_TIMER2_8197F */

#define BIT_SHIFT_PSTIMER2_INT_8197F 5
#define BIT_MASK_PSTIMER2_INT_8197F 0x7ffffff
#define BIT_PSTIMER2_INT_8197F(x)                                              \
	(((x) & BIT_MASK_PSTIMER2_INT_8197F) << BIT_SHIFT_PSTIMER2_INT_8197F)
#define BITS_PSTIMER2_INT_8197F                                                \
	(BIT_MASK_PSTIMER2_INT_8197F << BIT_SHIFT_PSTIMER2_INT_8197F)
#define BIT_CLEAR_PSTIMER2_INT_8197F(x) ((x) & (~BITS_PSTIMER2_INT_8197F))
#define BIT_GET_PSTIMER2_INT_8197F(x)                                          \
	(((x) >> BIT_SHIFT_PSTIMER2_INT_8197F) & BIT_MASK_PSTIMER2_INT_8197F)
#define BIT_SET_PSTIMER2_INT_8197F(x, v)                                       \
	(BIT_CLEAR_PSTIMER2_INT_8197F(x) | BIT_PSTIMER2_INT_8197F(v))

/* 2 REG_TBTT_CTN_AREA_8197F */

#define BIT_SHIFT_TBTT_CTN_AREA_8197F 0
#define BIT_MASK_TBTT_CTN_AREA_8197F 0xff
#define BIT_TBTT_CTN_AREA_8197F(x)                                             \
	(((x) & BIT_MASK_TBTT_CTN_AREA_8197F) << BIT_SHIFT_TBTT_CTN_AREA_8197F)
#define BITS_TBTT_CTN_AREA_8197F                                               \
	(BIT_MASK_TBTT_CTN_AREA_8197F << BIT_SHIFT_TBTT_CTN_AREA_8197F)
#define BIT_CLEAR_TBTT_CTN_AREA_8197F(x) ((x) & (~BITS_TBTT_CTN_AREA_8197F))
#define BIT_GET_TBTT_CTN_AREA_8197F(x)                                         \
	(((x) >> BIT_SHIFT_TBTT_CTN_AREA_8197F) & BIT_MASK_TBTT_CTN_AREA_8197F)
#define BIT_SET_TBTT_CTN_AREA_8197F(x, v)                                      \
	(BIT_CLEAR_TBTT_CTN_AREA_8197F(x) | BIT_TBTT_CTN_AREA_8197F(v))

/* 2 REG_FORCE_BCN_IFS_8197F */

#define BIT_SHIFT_FORCE_BCN_IFS_8197F 0
#define BIT_MASK_FORCE_BCN_IFS_8197F 0xff
#define BIT_FORCE_BCN_IFS_8197F(x)                                             \
	(((x) & BIT_MASK_FORCE_BCN_IFS_8197F) << BIT_SHIFT_FORCE_BCN_IFS_8197F)
#define BITS_FORCE_BCN_IFS_8197F                                               \
	(BIT_MASK_FORCE_BCN_IFS_8197F << BIT_SHIFT_FORCE_BCN_IFS_8197F)
#define BIT_CLEAR_FORCE_BCN_IFS_8197F(x) ((x) & (~BITS_FORCE_BCN_IFS_8197F))
#define BIT_GET_FORCE_BCN_IFS_8197F(x)                                         \
	(((x) >> BIT_SHIFT_FORCE_BCN_IFS_8197F) & BIT_MASK_FORCE_BCN_IFS_8197F)
#define BIT_SET_FORCE_BCN_IFS_8197F(x, v)                                      \
	(BIT_CLEAR_FORCE_BCN_IFS_8197F(x) | BIT_FORCE_BCN_IFS_8197F(v))

/* 2 REG_TXOP_MIN_8197F */
#define BIT_NAV_BLK_HGQ_8197F BIT(15)
#define BIT_NAV_BLK_MGQ_8197F BIT(14)

#define BIT_SHIFT_TXOP_MIN_8197F 0
#define BIT_MASK_TXOP_MIN_8197F 0x3fff
#define BIT_TXOP_MIN_8197F(x)                                                  \
	(((x) & BIT_MASK_TXOP_MIN_8197F) << BIT_SHIFT_TXOP_MIN_8197F)
#define BITS_TXOP_MIN_8197F                                                    \
	(BIT_MASK_TXOP_MIN_8197F << BIT_SHIFT_TXOP_MIN_8197F)
#define BIT_CLEAR_TXOP_MIN_8197F(x) ((x) & (~BITS_TXOP_MIN_8197F))
#define BIT_GET_TXOP_MIN_8197F(x)                                              \
	(((x) >> BIT_SHIFT_TXOP_MIN_8197F) & BIT_MASK_TXOP_MIN_8197F)
#define BIT_SET_TXOP_MIN_8197F(x, v)                                           \
	(BIT_CLEAR_TXOP_MIN_8197F(x) | BIT_TXOP_MIN_8197F(v))

/* 2 REG_PRE_BKF_TIME_8197F */

#define BIT_SHIFT_PRE_BKF_TIME_8197F 0
#define BIT_MASK_PRE_BKF_TIME_8197F 0xff
#define BIT_PRE_BKF_TIME_8197F(x)                                              \
	(((x) & BIT_MASK_PRE_BKF_TIME_8197F) << BIT_SHIFT_PRE_BKF_TIME_8197F)
#define BITS_PRE_BKF_TIME_8197F                                                \
	(BIT_MASK_PRE_BKF_TIME_8197F << BIT_SHIFT_PRE_BKF_TIME_8197F)
#define BIT_CLEAR_PRE_BKF_TIME_8197F(x) ((x) & (~BITS_PRE_BKF_TIME_8197F))
#define BIT_GET_PRE_BKF_TIME_8197F(x)                                          \
	(((x) >> BIT_SHIFT_PRE_BKF_TIME_8197F) & BIT_MASK_PRE_BKF_TIME_8197F)
#define BIT_SET_PRE_BKF_TIME_8197F(x, v)                                       \
	(BIT_CLEAR_PRE_BKF_TIME_8197F(x) | BIT_PRE_BKF_TIME_8197F(v))

/* 2 REG_CROSS_TXOP_CTRL_8197F */
#define BIT_DTIM_BYPASS_8197F BIT(2)
#define BIT_RTS_NAV_TXOP_8197F BIT(1)
#define BIT_NOT_CROSS_TXOP_8197F BIT(0)

/* 2 REG_TBTT_INT_SHIFT_CLI0_8197F */
#define BIT_TBTT_INT_SHIFT_DIR_CLI0_8197F BIT(7)

#define BIT_SHIFT_TBTT_INT_SHIFT_CLI0_8197F 0
#define BIT_MASK_TBTT_INT_SHIFT_CLI0_8197F 0x7f
#define BIT_TBTT_INT_SHIFT_CLI0_8197F(x)                                       \
	(((x) & BIT_MASK_TBTT_INT_SHIFT_CLI0_8197F)                            \
	 << BIT_SHIFT_TBTT_INT_SHIFT_CLI0_8197F)
#define BITS_TBTT_INT_SHIFT_CLI0_8197F                                         \
	(BIT_MASK_TBTT_INT_SHIFT_CLI0_8197F                                    \
	 << BIT_SHIFT_TBTT_INT_SHIFT_CLI0_8197F)
#define BIT_CLEAR_TBTT_INT_SHIFT_CLI0_8197F(x)                                 \
	((x) & (~BITS_TBTT_INT_SHIFT_CLI0_8197F))
#define BIT_GET_TBTT_INT_SHIFT_CLI0_8197F(x)                                   \
	(((x) >> BIT_SHIFT_TBTT_INT_SHIFT_CLI0_8197F) &                        \
	 BIT_MASK_TBTT_INT_SHIFT_CLI0_8197F)
#define BIT_SET_TBTT_INT_SHIFT_CLI0_8197F(x, v)                                \
	(BIT_CLEAR_TBTT_INT_SHIFT_CLI0_8197F(x) |                              \
	 BIT_TBTT_INT_SHIFT_CLI0_8197F(v))

/* 2 REG_TBTT_INT_SHIFT_CLI1_8197F */
#define BIT_TBTT_INT_SHIFT_DIR_CLI1_8197F BIT(7)

#define BIT_SHIFT_TBTT_INT_SHIFT_CLI1_8197F 0
#define BIT_MASK_TBTT_INT_SHIFT_CLI1_8197F 0x7f
#define BIT_TBTT_INT_SHIFT_CLI1_8197F(x)                                       \
	(((x) & BIT_MASK_TBTT_INT_SHIFT_CLI1_8197F)                            \
	 << BIT_SHIFT_TBTT_INT_SHIFT_CLI1_8197F)
#define BITS_TBTT_INT_SHIFT_CLI1_8197F                                         \
	(BIT_MASK_TBTT_INT_SHIFT_CLI1_8197F                                    \
	 << BIT_SHIFT_TBTT_INT_SHIFT_CLI1_8197F)
#define BIT_CLEAR_TBTT_INT_SHIFT_CLI1_8197F(x)                                 \
	((x) & (~BITS_TBTT_INT_SHIFT_CLI1_8197F))
#define BIT_GET_TBTT_INT_SHIFT_CLI1_8197F(x)                                   \
	(((x) >> BIT_SHIFT_TBTT_INT_SHIFT_CLI1_8197F) &                        \
	 BIT_MASK_TBTT_INT_SHIFT_CLI1_8197F)
#define BIT_SET_TBTT_INT_SHIFT_CLI1_8197F(x, v)                                \
	(BIT_CLEAR_TBTT_INT_SHIFT_CLI1_8197F(x) |                              \
	 BIT_TBTT_INT_SHIFT_CLI1_8197F(v))

/* 2 REG_TBTT_INT_SHIFT_CLI2_8197F */
#define BIT_TBTT_INT_SHIFT_DIR_CLI2_8197F BIT(7)

#define BIT_SHIFT_TBTT_INT_SHIFT_CLI2_8197F 0
#define BIT_MASK_TBTT_INT_SHIFT_CLI2_8197F 0x7f
#define BIT_TBTT_INT_SHIFT_CLI2_8197F(x)                                       \
	(((x) & BIT_MASK_TBTT_INT_SHIFT_CLI2_8197F)                            \
	 << BIT_SHIFT_TBTT_INT_SHIFT_CLI2_8197F)
#define BITS_TBTT_INT_SHIFT_CLI2_8197F                                         \
	(BIT_MASK_TBTT_INT_SHIFT_CLI2_8197F                                    \
	 << BIT_SHIFT_TBTT_INT_SHIFT_CLI2_8197F)
#define BIT_CLEAR_TBTT_INT_SHIFT_CLI2_8197F(x)                                 \
	((x) & (~BITS_TBTT_INT_SHIFT_CLI2_8197F))
#define BIT_GET_TBTT_INT_SHIFT_CLI2_8197F(x)                                   \
	(((x) >> BIT_SHIFT_TBTT_INT_SHIFT_CLI2_8197F) &                        \
	 BIT_MASK_TBTT_INT_SHIFT_CLI2_8197F)
#define BIT_SET_TBTT_INT_SHIFT_CLI2_8197F(x, v)                                \
	(BIT_CLEAR_TBTT_INT_SHIFT_CLI2_8197F(x) |                              \
	 BIT_TBTT_INT_SHIFT_CLI2_8197F(v))

/* 2 REG_TBTT_INT_SHIFT_CLI3_8197F */
#define BIT_TBTT_INT_SHIFT_DIR_CLI3_8197F BIT(7)

#define BIT_SHIFT_TBTT_INT_SHIFT_CLI3_8197F 0
#define BIT_MASK_TBTT_INT_SHIFT_CLI3_8197F 0x7f
#define BIT_TBTT_INT_SHIFT_CLI3_8197F(x)                                       \
	(((x) & BIT_MASK_TBTT_INT_SHIFT_CLI3_8197F)                            \
	 << BIT_SHIFT_TBTT_INT_SHIFT_CLI3_8197F)
#define BITS_TBTT_INT_SHIFT_CLI3_8197F                                         \
	(BIT_MASK_TBTT_INT_SHIFT_CLI3_8197F                                    \
	 << BIT_SHIFT_TBTT_INT_SHIFT_CLI3_8197F)
#define BIT_CLEAR_TBTT_INT_SHIFT_CLI3_8197F(x)                                 \
	((x) & (~BITS_TBTT_INT_SHIFT_CLI3_8197F))
#define BIT_GET_TBTT_INT_SHIFT_CLI3_8197F(x)                                   \
	(((x) >> BIT_SHIFT_TBTT_INT_SHIFT_CLI3_8197F) &                        \
	 BIT_MASK_TBTT_INT_SHIFT_CLI3_8197F)
#define BIT_SET_TBTT_INT_SHIFT_CLI3_8197F(x, v)                                \
	(BIT_CLEAR_TBTT_INT_SHIFT_CLI3_8197F(x) |                              \
	 BIT_TBTT_INT_SHIFT_CLI3_8197F(v))

/* 2 REG_TBTT_INT_SHIFT_ENABLE_8197F */
#define BIT_EN_TBTT_RTY_8197F BIT(1)
#define BIT_TBTT_INT_SHIFT_ENABLE_8197F BIT(0)

/* 2 REG_ATIMWND2_8197F */

#define BIT_SHIFT_ATIMWND2_8197F 0
#define BIT_MASK_ATIMWND2_8197F 0xff
#define BIT_ATIMWND2_8197F(x)                                                  \
	(((x) & BIT_MASK_ATIMWND2_8197F) << BIT_SHIFT_ATIMWND2_8197F)
#define BITS_ATIMWND2_8197F                                                    \
	(BIT_MASK_ATIMWND2_8197F << BIT_SHIFT_ATIMWND2_8197F)
#define BIT_CLEAR_ATIMWND2_8197F(x) ((x) & (~BITS_ATIMWND2_8197F))
#define BIT_GET_ATIMWND2_8197F(x)                                              \
	(((x) >> BIT_SHIFT_ATIMWND2_8197F) & BIT_MASK_ATIMWND2_8197F)
#define BIT_SET_ATIMWND2_8197F(x, v)                                           \
	(BIT_CLEAR_ATIMWND2_8197F(x) | BIT_ATIMWND2_8197F(v))

/* 2 REG_ATIMWND3_8197F */

#define BIT_SHIFT_ATIMWND3_8197F 0
#define BIT_MASK_ATIMWND3_8197F 0xff
#define BIT_ATIMWND3_8197F(x)                                                  \
	(((x) & BIT_MASK_ATIMWND3_8197F) << BIT_SHIFT_ATIMWND3_8197F)
#define BITS_ATIMWND3_8197F                                                    \
	(BIT_MASK_ATIMWND3_8197F << BIT_SHIFT_ATIMWND3_8197F)
#define BIT_CLEAR_ATIMWND3_8197F(x) ((x) & (~BITS_ATIMWND3_8197F))
#define BIT_GET_ATIMWND3_8197F(x)                                              \
	(((x) >> BIT_SHIFT_ATIMWND3_8197F) & BIT_MASK_ATIMWND3_8197F)
#define BIT_SET_ATIMWND3_8197F(x, v)                                           \
	(BIT_CLEAR_ATIMWND3_8197F(x) | BIT_ATIMWND3_8197F(v))

/* 2 REG_ATIMWND4_8197F */

#define BIT_SHIFT_ATIMWND4_8197F 0
#define BIT_MASK_ATIMWND4_8197F 0xff
#define BIT_ATIMWND4_8197F(x)                                                  \
	(((x) & BIT_MASK_ATIMWND4_8197F) << BIT_SHIFT_ATIMWND4_8197F)
#define BITS_ATIMWND4_8197F                                                    \
	(BIT_MASK_ATIMWND4_8197F << BIT_SHIFT_ATIMWND4_8197F)
#define BIT_CLEAR_ATIMWND4_8197F(x) ((x) & (~BITS_ATIMWND4_8197F))
#define BIT_GET_ATIMWND4_8197F(x)                                              \
	(((x) >> BIT_SHIFT_ATIMWND4_8197F) & BIT_MASK_ATIMWND4_8197F)
#define BIT_SET_ATIMWND4_8197F(x, v)                                           \
	(BIT_CLEAR_ATIMWND4_8197F(x) | BIT_ATIMWND4_8197F(v))

/* 2 REG_ATIMWND5_8197F */

#define BIT_SHIFT_ATIMWND5_8197F 0
#define BIT_MASK_ATIMWND5_8197F 0xff
#define BIT_ATIMWND5_8197F(x)                                                  \
	(((x) & BIT_MASK_ATIMWND5_8197F) << BIT_SHIFT_ATIMWND5_8197F)
#define BITS_ATIMWND5_8197F                                                    \
	(BIT_MASK_ATIMWND5_8197F << BIT_SHIFT_ATIMWND5_8197F)
#define BIT_CLEAR_ATIMWND5_8197F(x) ((x) & (~BITS_ATIMWND5_8197F))
#define BIT_GET_ATIMWND5_8197F(x)                                              \
	(((x) >> BIT_SHIFT_ATIMWND5_8197F) & BIT_MASK_ATIMWND5_8197F)
#define BIT_SET_ATIMWND5_8197F(x, v)                                           \
	(BIT_CLEAR_ATIMWND5_8197F(x) | BIT_ATIMWND5_8197F(v))

/* 2 REG_ATIMWND6_8197F */

#define BIT_SHIFT_ATIMWND6_8197F 0
#define BIT_MASK_ATIMWND6_8197F 0xff
#define BIT_ATIMWND6_8197F(x)                                                  \
	(((x) & BIT_MASK_ATIMWND6_8197F) << BIT_SHIFT_ATIMWND6_8197F)
#define BITS_ATIMWND6_8197F                                                    \
	(BIT_MASK_ATIMWND6_8197F << BIT_SHIFT_ATIMWND6_8197F)
#define BIT_CLEAR_ATIMWND6_8197F(x) ((x) & (~BITS_ATIMWND6_8197F))
#define BIT_GET_ATIMWND6_8197F(x)                                              \
	(((x) >> BIT_SHIFT_ATIMWND6_8197F) & BIT_MASK_ATIMWND6_8197F)
#define BIT_SET_ATIMWND6_8197F(x, v)                                           \
	(BIT_CLEAR_ATIMWND6_8197F(x) | BIT_ATIMWND6_8197F(v))

/* 2 REG_ATIMWND7_8197F */

#define BIT_SHIFT_ATIMWND7_8197F 0
#define BIT_MASK_ATIMWND7_8197F 0xff
#define BIT_ATIMWND7_8197F(x)                                                  \
	(((x) & BIT_MASK_ATIMWND7_8197F) << BIT_SHIFT_ATIMWND7_8197F)
#define BITS_ATIMWND7_8197F                                                    \
	(BIT_MASK_ATIMWND7_8197F << BIT_SHIFT_ATIMWND7_8197F)
#define BIT_CLEAR_ATIMWND7_8197F(x) ((x) & (~BITS_ATIMWND7_8197F))
#define BIT_GET_ATIMWND7_8197F(x)                                              \
	(((x) >> BIT_SHIFT_ATIMWND7_8197F) & BIT_MASK_ATIMWND7_8197F)
#define BIT_SET_ATIMWND7_8197F(x, v)                                           \
	(BIT_CLEAR_ATIMWND7_8197F(x) | BIT_ATIMWND7_8197F(v))

/* 2 REG_ATIMUGT_8197F */

#define BIT_SHIFT_ATIM_URGENT_8197F 0
#define BIT_MASK_ATIM_URGENT_8197F 0xff
#define BIT_ATIM_URGENT_8197F(x)                                               \
	(((x) & BIT_MASK_ATIM_URGENT_8197F) << BIT_SHIFT_ATIM_URGENT_8197F)
#define BITS_ATIM_URGENT_8197F                                                 \
	(BIT_MASK_ATIM_URGENT_8197F << BIT_SHIFT_ATIM_URGENT_8197F)
#define BIT_CLEAR_ATIM_URGENT_8197F(x) ((x) & (~BITS_ATIM_URGENT_8197F))
#define BIT_GET_ATIM_URGENT_8197F(x)                                           \
	(((x) >> BIT_SHIFT_ATIM_URGENT_8197F) & BIT_MASK_ATIM_URGENT_8197F)
#define BIT_SET_ATIM_URGENT_8197F(x, v)                                        \
	(BIT_CLEAR_ATIM_URGENT_8197F(x) | BIT_ATIM_URGENT_8197F(v))

/* 2 REG_HIQ_NO_LMT_EN_8197F */
#define BIT_HIQ_NO_LMT_EN_VAP7_8197F BIT(7)
#define BIT_HIQ_NO_LMT_EN_VAP6_8197F BIT(6)
#define BIT_HIQ_NO_LMT_EN_VAP5_8197F BIT(5)
#define BIT_HIQ_NO_LMT_EN_VAP4_8197F BIT(4)
#define BIT_HIQ_NO_LMT_EN_VAP3_8197F BIT(3)
#define BIT_HIQ_NO_LMT_EN_VAP2_8197F BIT(2)
#define BIT_HIQ_NO_LMT_EN_VAP1_8197F BIT(1)
#define BIT_HIQ_NO_LMT_EN_ROOT_8197F BIT(0)

/* 2 REG_DTIM_COUNTER_ROOT_8197F */

#define BIT_SHIFT_DTIM_COUNT_ROOT_8197F 0
#define BIT_MASK_DTIM_COUNT_ROOT_8197F 0xff
#define BIT_DTIM_COUNT_ROOT_8197F(x)                                           \
	(((x) & BIT_MASK_DTIM_COUNT_ROOT_8197F)                                \
	 << BIT_SHIFT_DTIM_COUNT_ROOT_8197F)
#define BITS_DTIM_COUNT_ROOT_8197F                                             \
	(BIT_MASK_DTIM_COUNT_ROOT_8197F << BIT_SHIFT_DTIM_COUNT_ROOT_8197F)
#define BIT_CLEAR_DTIM_COUNT_ROOT_8197F(x) ((x) & (~BITS_DTIM_COUNT_ROOT_8197F))
#define BIT_GET_DTIM_COUNT_ROOT_8197F(x)                                       \
	(((x) >> BIT_SHIFT_DTIM_COUNT_ROOT_8197F) &                            \
	 BIT_MASK_DTIM_COUNT_ROOT_8197F)
#define BIT_SET_DTIM_COUNT_ROOT_8197F(x, v)                                    \
	(BIT_CLEAR_DTIM_COUNT_ROOT_8197F(x) | BIT_DTIM_COUNT_ROOT_8197F(v))

/* 2 REG_DTIM_COUNTER_VAP1_8197F */

#define BIT_SHIFT_DTIM_COUNT_VAP1_8197F 0
#define BIT_MASK_DTIM_COUNT_VAP1_8197F 0xff
#define BIT_DTIM_COUNT_VAP1_8197F(x)                                           \
	(((x) & BIT_MASK_DTIM_COUNT_VAP1_8197F)                                \
	 << BIT_SHIFT_DTIM_COUNT_VAP1_8197F)
#define BITS_DTIM_COUNT_VAP1_8197F                                             \
	(BIT_MASK_DTIM_COUNT_VAP1_8197F << BIT_SHIFT_DTIM_COUNT_VAP1_8197F)
#define BIT_CLEAR_DTIM_COUNT_VAP1_8197F(x) ((x) & (~BITS_DTIM_COUNT_VAP1_8197F))
#define BIT_GET_DTIM_COUNT_VAP1_8197F(x)                                       \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP1_8197F) &                            \
	 BIT_MASK_DTIM_COUNT_VAP1_8197F)
#define BIT_SET_DTIM_COUNT_VAP1_8197F(x, v)                                    \
	(BIT_CLEAR_DTIM_COUNT_VAP1_8197F(x) | BIT_DTIM_COUNT_VAP1_8197F(v))

/* 2 REG_DTIM_COUNTER_VAP2_8197F */

#define BIT_SHIFT_DTIM_COUNT_VAP2_8197F 0
#define BIT_MASK_DTIM_COUNT_VAP2_8197F 0xff
#define BIT_DTIM_COUNT_VAP2_8197F(x)                                           \
	(((x) & BIT_MASK_DTIM_COUNT_VAP2_8197F)                                \
	 << BIT_SHIFT_DTIM_COUNT_VAP2_8197F)
#define BITS_DTIM_COUNT_VAP2_8197F                                             \
	(BIT_MASK_DTIM_COUNT_VAP2_8197F << BIT_SHIFT_DTIM_COUNT_VAP2_8197F)
#define BIT_CLEAR_DTIM_COUNT_VAP2_8197F(x) ((x) & (~BITS_DTIM_COUNT_VAP2_8197F))
#define BIT_GET_DTIM_COUNT_VAP2_8197F(x)                                       \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP2_8197F) &                            \
	 BIT_MASK_DTIM_COUNT_VAP2_8197F)
#define BIT_SET_DTIM_COUNT_VAP2_8197F(x, v)                                    \
	(BIT_CLEAR_DTIM_COUNT_VAP2_8197F(x) | BIT_DTIM_COUNT_VAP2_8197F(v))

/* 2 REG_DTIM_COUNTER_VAP3_8197F */

#define BIT_SHIFT_DTIM_COUNT_VAP3_8197F 0
#define BIT_MASK_DTIM_COUNT_VAP3_8197F 0xff
#define BIT_DTIM_COUNT_VAP3_8197F(x)                                           \
	(((x) & BIT_MASK_DTIM_COUNT_VAP3_8197F)                                \
	 << BIT_SHIFT_DTIM_COUNT_VAP3_8197F)
#define BITS_DTIM_COUNT_VAP3_8197F                                             \
	(BIT_MASK_DTIM_COUNT_VAP3_8197F << BIT_SHIFT_DTIM_COUNT_VAP3_8197F)
#define BIT_CLEAR_DTIM_COUNT_VAP3_8197F(x) ((x) & (~BITS_DTIM_COUNT_VAP3_8197F))
#define BIT_GET_DTIM_COUNT_VAP3_8197F(x)                                       \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP3_8197F) &                            \
	 BIT_MASK_DTIM_COUNT_VAP3_8197F)
#define BIT_SET_DTIM_COUNT_VAP3_8197F(x, v)                                    \
	(BIT_CLEAR_DTIM_COUNT_VAP3_8197F(x) | BIT_DTIM_COUNT_VAP3_8197F(v))

/* 2 REG_DTIM_COUNTER_VAP4_8197F */

#define BIT_SHIFT_DTIM_COUNT_VAP4_8197F 0
#define BIT_MASK_DTIM_COUNT_VAP4_8197F 0xff
#define BIT_DTIM_COUNT_VAP4_8197F(x)                                           \
	(((x) & BIT_MASK_DTIM_COUNT_VAP4_8197F)                                \
	 << BIT_SHIFT_DTIM_COUNT_VAP4_8197F)
#define BITS_DTIM_COUNT_VAP4_8197F                                             \
	(BIT_MASK_DTIM_COUNT_VAP4_8197F << BIT_SHIFT_DTIM_COUNT_VAP4_8197F)
#define BIT_CLEAR_DTIM_COUNT_VAP4_8197F(x) ((x) & (~BITS_DTIM_COUNT_VAP4_8197F))
#define BIT_GET_DTIM_COUNT_VAP4_8197F(x)                                       \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP4_8197F) &                            \
	 BIT_MASK_DTIM_COUNT_VAP4_8197F)
#define BIT_SET_DTIM_COUNT_VAP4_8197F(x, v)                                    \
	(BIT_CLEAR_DTIM_COUNT_VAP4_8197F(x) | BIT_DTIM_COUNT_VAP4_8197F(v))

/* 2 REG_DTIM_COUNTER_VAP5_8197F */

#define BIT_SHIFT_DTIM_COUNT_VAP5_8197F 0
#define BIT_MASK_DTIM_COUNT_VAP5_8197F 0xff
#define BIT_DTIM_COUNT_VAP5_8197F(x)                                           \
	(((x) & BIT_MASK_DTIM_COUNT_VAP5_8197F)                                \
	 << BIT_SHIFT_DTIM_COUNT_VAP5_8197F)
#define BITS_DTIM_COUNT_VAP5_8197F                                             \
	(BIT_MASK_DTIM_COUNT_VAP5_8197F << BIT_SHIFT_DTIM_COUNT_VAP5_8197F)
#define BIT_CLEAR_DTIM_COUNT_VAP5_8197F(x) ((x) & (~BITS_DTIM_COUNT_VAP5_8197F))
#define BIT_GET_DTIM_COUNT_VAP5_8197F(x)                                       \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP5_8197F) &                            \
	 BIT_MASK_DTIM_COUNT_VAP5_8197F)
#define BIT_SET_DTIM_COUNT_VAP5_8197F(x, v)                                    \
	(BIT_CLEAR_DTIM_COUNT_VAP5_8197F(x) | BIT_DTIM_COUNT_VAP5_8197F(v))

/* 2 REG_DTIM_COUNTER_VAP6_8197F */

#define BIT_SHIFT_DTIM_COUNT_VAP6_8197F 0
#define BIT_MASK_DTIM_COUNT_VAP6_8197F 0xff
#define BIT_DTIM_COUNT_VAP6_8197F(x)                                           \
	(((x) & BIT_MASK_DTIM_COUNT_VAP6_8197F)                                \
	 << BIT_SHIFT_DTIM_COUNT_VAP6_8197F)
#define BITS_DTIM_COUNT_VAP6_8197F                                             \
	(BIT_MASK_DTIM_COUNT_VAP6_8197F << BIT_SHIFT_DTIM_COUNT_VAP6_8197F)
#define BIT_CLEAR_DTIM_COUNT_VAP6_8197F(x) ((x) & (~BITS_DTIM_COUNT_VAP6_8197F))
#define BIT_GET_DTIM_COUNT_VAP6_8197F(x)                                       \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP6_8197F) &                            \
	 BIT_MASK_DTIM_COUNT_VAP6_8197F)
#define BIT_SET_DTIM_COUNT_VAP6_8197F(x, v)                                    \
	(BIT_CLEAR_DTIM_COUNT_VAP6_8197F(x) | BIT_DTIM_COUNT_VAP6_8197F(v))

/* 2 REG_DTIM_COUNTER_VAP7_8197F */

#define BIT_SHIFT_DTIM_COUNT_VAP7_8197F 0
#define BIT_MASK_DTIM_COUNT_VAP7_8197F 0xff
#define BIT_DTIM_COUNT_VAP7_8197F(x)                                           \
	(((x) & BIT_MASK_DTIM_COUNT_VAP7_8197F)                                \
	 << BIT_SHIFT_DTIM_COUNT_VAP7_8197F)
#define BITS_DTIM_COUNT_VAP7_8197F                                             \
	(BIT_MASK_DTIM_COUNT_VAP7_8197F << BIT_SHIFT_DTIM_COUNT_VAP7_8197F)
#define BIT_CLEAR_DTIM_COUNT_VAP7_8197F(x) ((x) & (~BITS_DTIM_COUNT_VAP7_8197F))
#define BIT_GET_DTIM_COUNT_VAP7_8197F(x)                                       \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP7_8197F) &                            \
	 BIT_MASK_DTIM_COUNT_VAP7_8197F)
#define BIT_SET_DTIM_COUNT_VAP7_8197F(x, v)                                    \
	(BIT_CLEAR_DTIM_COUNT_VAP7_8197F(x) | BIT_DTIM_COUNT_VAP7_8197F(v))

/* 2 REG_DIS_ATIM_8197F */
#define BIT_DIS_ATIM_VAP7_8197F BIT(7)
#define BIT_DIS_ATIM_VAP6_8197F BIT(6)
#define BIT_DIS_ATIM_VAP5_8197F BIT(5)
#define BIT_DIS_ATIM_VAP4_8197F BIT(4)
#define BIT_DIS_ATIM_VAP3_8197F BIT(3)
#define BIT_DIS_ATIM_VAP2_8197F BIT(2)
#define BIT_DIS_ATIM_VAP1_8197F BIT(1)
#define BIT_DIS_ATIM_ROOT_8197F BIT(0)

/* 2 REG_EARLY_128US_8197F */

#define BIT_SHIFT_TSFT_SEL_TIMER1_8197F 3
#define BIT_MASK_TSFT_SEL_TIMER1_8197F 0x7
#define BIT_TSFT_SEL_TIMER1_8197F(x)                                           \
	(((x) & BIT_MASK_TSFT_SEL_TIMER1_8197F)                                \
	 << BIT_SHIFT_TSFT_SEL_TIMER1_8197F)
#define BITS_TSFT_SEL_TIMER1_8197F                                             \
	(BIT_MASK_TSFT_SEL_TIMER1_8197F << BIT_SHIFT_TSFT_SEL_TIMER1_8197F)
#define BIT_CLEAR_TSFT_SEL_TIMER1_8197F(x) ((x) & (~BITS_TSFT_SEL_TIMER1_8197F))
#define BIT_GET_TSFT_SEL_TIMER1_8197F(x)                                       \
	(((x) >> BIT_SHIFT_TSFT_SEL_TIMER1_8197F) &                            \
	 BIT_MASK_TSFT_SEL_TIMER1_8197F)
#define BIT_SET_TSFT_SEL_TIMER1_8197F(x, v)                                    \
	(BIT_CLEAR_TSFT_SEL_TIMER1_8197F(x) | BIT_TSFT_SEL_TIMER1_8197F(v))

#define BIT_SHIFT_EARLY_128US_8197F 0
#define BIT_MASK_EARLY_128US_8197F 0x7
#define BIT_EARLY_128US_8197F(x)                                               \
	(((x) & BIT_MASK_EARLY_128US_8197F) << BIT_SHIFT_EARLY_128US_8197F)
#define BITS_EARLY_128US_8197F                                                 \
	(BIT_MASK_EARLY_128US_8197F << BIT_SHIFT_EARLY_128US_8197F)
#define BIT_CLEAR_EARLY_128US_8197F(x) ((x) & (~BITS_EARLY_128US_8197F))
#define BIT_GET_EARLY_128US_8197F(x)                                           \
	(((x) >> BIT_SHIFT_EARLY_128US_8197F) & BIT_MASK_EARLY_128US_8197F)
#define BIT_SET_EARLY_128US_8197F(x, v)                                        \
	(BIT_CLEAR_EARLY_128US_8197F(x) | BIT_EARLY_128US_8197F(v))

/* 2 REG_P2PPS1_CTRL_8197F */
#define BIT_P2P1_CTW_ALLSTASLEEP_8197F BIT(7)
#define BIT_P2P1_OFF_DISTX_EN_8197F BIT(6)
#define BIT_P2P1_PWR_MGT_EN_8197F BIT(5)
#define BIT_P2P1_NOA1_EN_8197F BIT(2)
#define BIT_P2P1_NOA0_EN_8197F BIT(1)

/* 2 REG_P2PPS2_CTRL_8197F */
#define BIT_P2P2_CTW_ALLSTASLEEP_8197F BIT(7)
#define BIT_P2P2_OFF_DISTX_EN_8197F BIT(6)
#define BIT_P2P2_PWR_MGT_EN_8197F BIT(5)
#define BIT_P2P2_NOA1_EN_8197F BIT(2)
#define BIT_P2P2_NOA0_EN_8197F BIT(1)

/* 2 REG_TIMER0_SRC_SEL_8197F */

#define BIT_SHIFT_SYNC_CLI_SEL_8197F 4
#define BIT_MASK_SYNC_CLI_SEL_8197F 0x7
#define BIT_SYNC_CLI_SEL_8197F(x)                                              \
	(((x) & BIT_MASK_SYNC_CLI_SEL_8197F) << BIT_SHIFT_SYNC_CLI_SEL_8197F)
#define BITS_SYNC_CLI_SEL_8197F                                                \
	(BIT_MASK_SYNC_CLI_SEL_8197F << BIT_SHIFT_SYNC_CLI_SEL_8197F)
#define BIT_CLEAR_SYNC_CLI_SEL_8197F(x) ((x) & (~BITS_SYNC_CLI_SEL_8197F))
#define BIT_GET_SYNC_CLI_SEL_8197F(x)                                          \
	(((x) >> BIT_SHIFT_SYNC_CLI_SEL_8197F) & BIT_MASK_SYNC_CLI_SEL_8197F)
#define BIT_SET_SYNC_CLI_SEL_8197F(x, v)                                       \
	(BIT_CLEAR_SYNC_CLI_SEL_8197F(x) | BIT_SYNC_CLI_SEL_8197F(v))

#define BIT_SHIFT_TSFT_SEL_TIMER0_8197F 0
#define BIT_MASK_TSFT_SEL_TIMER0_8197F 0x7
#define BIT_TSFT_SEL_TIMER0_8197F(x)                                           \
	(((x) & BIT_MASK_TSFT_SEL_TIMER0_8197F)                                \
	 << BIT_SHIFT_TSFT_SEL_TIMER0_8197F)
#define BITS_TSFT_SEL_TIMER0_8197F                                             \
	(BIT_MASK_TSFT_SEL_TIMER0_8197F << BIT_SHIFT_TSFT_SEL_TIMER0_8197F)
#define BIT_CLEAR_TSFT_SEL_TIMER0_8197F(x) ((x) & (~BITS_TSFT_SEL_TIMER0_8197F))
#define BIT_GET_TSFT_SEL_TIMER0_8197F(x)                                       \
	(((x) >> BIT_SHIFT_TSFT_SEL_TIMER0_8197F) &                            \
	 BIT_MASK_TSFT_SEL_TIMER0_8197F)
#define BIT_SET_TSFT_SEL_TIMER0_8197F(x, v)                                    \
	(BIT_CLEAR_TSFT_SEL_TIMER0_8197F(x) | BIT_TSFT_SEL_TIMER0_8197F(v))

/* 2 REG_NOA_UNIT_SEL_8197F */

#define BIT_SHIFT_NOA_UNIT2_SEL_8197F 8
#define BIT_MASK_NOA_UNIT2_SEL_8197F 0x7
#define BIT_NOA_UNIT2_SEL_8197F(x)                                             \
	(((x) & BIT_MASK_NOA_UNIT2_SEL_8197F) << BIT_SHIFT_NOA_UNIT2_SEL_8197F)
#define BITS_NOA_UNIT2_SEL_8197F                                               \
	(BIT_MASK_NOA_UNIT2_SEL_8197F << BIT_SHIFT_NOA_UNIT2_SEL_8197F)
#define BIT_CLEAR_NOA_UNIT2_SEL_8197F(x) ((x) & (~BITS_NOA_UNIT2_SEL_8197F))
#define BIT_GET_NOA_UNIT2_SEL_8197F(x)                                         \
	(((x) >> BIT_SHIFT_NOA_UNIT2_SEL_8197F) & BIT_MASK_NOA_UNIT2_SEL_8197F)
#define BIT_SET_NOA_UNIT2_SEL_8197F(x, v)                                      \
	(BIT_CLEAR_NOA_UNIT2_SEL_8197F(x) | BIT_NOA_UNIT2_SEL_8197F(v))

#define BIT_SHIFT_NOA_UNIT1_SEL_8197F 4
#define BIT_MASK_NOA_UNIT1_SEL_8197F 0x7
#define BIT_NOA_UNIT1_SEL_8197F(x)                                             \
	(((x) & BIT_MASK_NOA_UNIT1_SEL_8197F) << BIT_SHIFT_NOA_UNIT1_SEL_8197F)
#define BITS_NOA_UNIT1_SEL_8197F                                               \
	(BIT_MASK_NOA_UNIT1_SEL_8197F << BIT_SHIFT_NOA_UNIT1_SEL_8197F)
#define BIT_CLEAR_NOA_UNIT1_SEL_8197F(x) ((x) & (~BITS_NOA_UNIT1_SEL_8197F))
#define BIT_GET_NOA_UNIT1_SEL_8197F(x)                                         \
	(((x) >> BIT_SHIFT_NOA_UNIT1_SEL_8197F) & BIT_MASK_NOA_UNIT1_SEL_8197F)
#define BIT_SET_NOA_UNIT1_SEL_8197F(x, v)                                      \
	(BIT_CLEAR_NOA_UNIT1_SEL_8197F(x) | BIT_NOA_UNIT1_SEL_8197F(v))

#define BIT_SHIFT_NOA_UNIT0_SEL_8197F 0
#define BIT_MASK_NOA_UNIT0_SEL_8197F 0x7
#define BIT_NOA_UNIT0_SEL_8197F(x)                                             \
	(((x) & BIT_MASK_NOA_UNIT0_SEL_8197F) << BIT_SHIFT_NOA_UNIT0_SEL_8197F)
#define BITS_NOA_UNIT0_SEL_8197F                                               \
	(BIT_MASK_NOA_UNIT0_SEL_8197F << BIT_SHIFT_NOA_UNIT0_SEL_8197F)
#define BIT_CLEAR_NOA_UNIT0_SEL_8197F(x) ((x) & (~BITS_NOA_UNIT0_SEL_8197F))
#define BIT_GET_NOA_UNIT0_SEL_8197F(x)                                         \
	(((x) >> BIT_SHIFT_NOA_UNIT0_SEL_8197F) & BIT_MASK_NOA_UNIT0_SEL_8197F)
#define BIT_SET_NOA_UNIT0_SEL_8197F(x, v)                                      \
	(BIT_CLEAR_NOA_UNIT0_SEL_8197F(x) | BIT_NOA_UNIT0_SEL_8197F(v))

/* 2 REG_P2POFF_DIS_TXTIME_8197F */

#define BIT_SHIFT_P2POFF_DIS_TXTIME_8197F 0
#define BIT_MASK_P2POFF_DIS_TXTIME_8197F 0xff
#define BIT_P2POFF_DIS_TXTIME_8197F(x)                                         \
	(((x) & BIT_MASK_P2POFF_DIS_TXTIME_8197F)                              \
	 << BIT_SHIFT_P2POFF_DIS_TXTIME_8197F)
#define BITS_P2POFF_DIS_TXTIME_8197F                                           \
	(BIT_MASK_P2POFF_DIS_TXTIME_8197F << BIT_SHIFT_P2POFF_DIS_TXTIME_8197F)
#define BIT_CLEAR_P2POFF_DIS_TXTIME_8197F(x)                                   \
	((x) & (~BITS_P2POFF_DIS_TXTIME_8197F))
#define BIT_GET_P2POFF_DIS_TXTIME_8197F(x)                                     \
	(((x) >> BIT_SHIFT_P2POFF_DIS_TXTIME_8197F) &                          \
	 BIT_MASK_P2POFF_DIS_TXTIME_8197F)
#define BIT_SET_P2POFF_DIS_TXTIME_8197F(x, v)                                  \
	(BIT_CLEAR_P2POFF_DIS_TXTIME_8197F(x) | BIT_P2POFF_DIS_TXTIME_8197F(v))

/* 2 REG_MBSSID_BCN_SPACE2_8197F */

#define BIT_SHIFT_BCN_SPACE_CLINT2_8197F 16
#define BIT_MASK_BCN_SPACE_CLINT2_8197F 0xfff
#define BIT_BCN_SPACE_CLINT2_8197F(x)                                          \
	(((x) & BIT_MASK_BCN_SPACE_CLINT2_8197F)                               \
	 << BIT_SHIFT_BCN_SPACE_CLINT2_8197F)
#define BITS_BCN_SPACE_CLINT2_8197F                                            \
	(BIT_MASK_BCN_SPACE_CLINT2_8197F << BIT_SHIFT_BCN_SPACE_CLINT2_8197F)
#define BIT_CLEAR_BCN_SPACE_CLINT2_8197F(x)                                    \
	((x) & (~BITS_BCN_SPACE_CLINT2_8197F))
#define BIT_GET_BCN_SPACE_CLINT2_8197F(x)                                      \
	(((x) >> BIT_SHIFT_BCN_SPACE_CLINT2_8197F) &                           \
	 BIT_MASK_BCN_SPACE_CLINT2_8197F)
#define BIT_SET_BCN_SPACE_CLINT2_8197F(x, v)                                   \
	(BIT_CLEAR_BCN_SPACE_CLINT2_8197F(x) | BIT_BCN_SPACE_CLINT2_8197F(v))

#define BIT_SHIFT_BCN_SPACE_CLINT1_8197F 0
#define BIT_MASK_BCN_SPACE_CLINT1_8197F 0xfff
#define BIT_BCN_SPACE_CLINT1_8197F(x)                                          \
	(((x) & BIT_MASK_BCN_SPACE_CLINT1_8197F)                               \
	 << BIT_SHIFT_BCN_SPACE_CLINT1_8197F)
#define BITS_BCN_SPACE_CLINT1_8197F                                            \
	(BIT_MASK_BCN_SPACE_CLINT1_8197F << BIT_SHIFT_BCN_SPACE_CLINT1_8197F)
#define BIT_CLEAR_BCN_SPACE_CLINT1_8197F(x)                                    \
	((x) & (~BITS_BCN_SPACE_CLINT1_8197F))
#define BIT_GET_BCN_SPACE_CLINT1_8197F(x)                                      \
	(((x) >> BIT_SHIFT_BCN_SPACE_CLINT1_8197F) &                           \
	 BIT_MASK_BCN_SPACE_CLINT1_8197F)
#define BIT_SET_BCN_SPACE_CLINT1_8197F(x, v)                                   \
	(BIT_CLEAR_BCN_SPACE_CLINT1_8197F(x) | BIT_BCN_SPACE_CLINT1_8197F(v))

/* 2 REG_MBSSID_BCN_SPACE3_8197F */

#define BIT_SHIFT_SUB_BCN_SPACE_8197F 16
#define BIT_MASK_SUB_BCN_SPACE_8197F 0xff
#define BIT_SUB_BCN_SPACE_8197F(x)                                             \
	(((x) & BIT_MASK_SUB_BCN_SPACE_8197F) << BIT_SHIFT_SUB_BCN_SPACE_8197F)
#define BITS_SUB_BCN_SPACE_8197F                                               \
	(BIT_MASK_SUB_BCN_SPACE_8197F << BIT_SHIFT_SUB_BCN_SPACE_8197F)
#define BIT_CLEAR_SUB_BCN_SPACE_8197F(x) ((x) & (~BITS_SUB_BCN_SPACE_8197F))
#define BIT_GET_SUB_BCN_SPACE_8197F(x)                                         \
	(((x) >> BIT_SHIFT_SUB_BCN_SPACE_8197F) & BIT_MASK_SUB_BCN_SPACE_8197F)
#define BIT_SET_SUB_BCN_SPACE_8197F(x, v)                                      \
	(BIT_CLEAR_SUB_BCN_SPACE_8197F(x) | BIT_SUB_BCN_SPACE_8197F(v))

#define BIT_SHIFT_BCN_SPACE_CLINT3_8197F 0
#define BIT_MASK_BCN_SPACE_CLINT3_8197F 0xfff
#define BIT_BCN_SPACE_CLINT3_8197F(x)                                          \
	(((x) & BIT_MASK_BCN_SPACE_CLINT3_8197F)                               \
	 << BIT_SHIFT_BCN_SPACE_CLINT3_8197F)
#define BITS_BCN_SPACE_CLINT3_8197F                                            \
	(BIT_MASK_BCN_SPACE_CLINT3_8197F << BIT_SHIFT_BCN_SPACE_CLINT3_8197F)
#define BIT_CLEAR_BCN_SPACE_CLINT3_8197F(x)                                    \
	((x) & (~BITS_BCN_SPACE_CLINT3_8197F))
#define BIT_GET_BCN_SPACE_CLINT3_8197F(x)                                      \
	(((x) >> BIT_SHIFT_BCN_SPACE_CLINT3_8197F) &                           \
	 BIT_MASK_BCN_SPACE_CLINT3_8197F)
#define BIT_SET_BCN_SPACE_CLINT3_8197F(x, v)                                   \
	(BIT_CLEAR_BCN_SPACE_CLINT3_8197F(x) | BIT_BCN_SPACE_CLINT3_8197F(v))

/* 2 REG_ACMHWCTRL_8197F */
#define BIT_BEQ_ACM_STATUS_8197F BIT(7)
#define BIT_VIQ_ACM_STATUS_8197F BIT(6)
#define BIT_VOQ_ACM_STATUS_8197F BIT(5)
#define BIT_BEQ_ACM_EN_8197F BIT(3)
#define BIT_VIQ_ACM_EN_8197F BIT(2)
#define BIT_VOQ_ACM_EN_8197F BIT(1)
#define BIT_ACMHWEN_8197F BIT(0)

/* 2 REG_ACMRSTCTRL_8197F */
#define BIT_BE_ACM_RESET_USED_TIME_8197F BIT(2)
#define BIT_VI_ACM_RESET_USED_TIME_8197F BIT(1)
#define BIT_VO_ACM_RESET_USED_TIME_8197F BIT(0)

/* 2 REG_ACMAVG_8197F */

#define BIT_SHIFT_AVGPERIOD_8197F 0
#define BIT_MASK_AVGPERIOD_8197F 0xffff
#define BIT_AVGPERIOD_8197F(x)                                                 \
	(((x) & BIT_MASK_AVGPERIOD_8197F) << BIT_SHIFT_AVGPERIOD_8197F)
#define BITS_AVGPERIOD_8197F                                                   \
	(BIT_MASK_AVGPERIOD_8197F << BIT_SHIFT_AVGPERIOD_8197F)
#define BIT_CLEAR_AVGPERIOD_8197F(x) ((x) & (~BITS_AVGPERIOD_8197F))
#define BIT_GET_AVGPERIOD_8197F(x)                                             \
	(((x) >> BIT_SHIFT_AVGPERIOD_8197F) & BIT_MASK_AVGPERIOD_8197F)
#define BIT_SET_AVGPERIOD_8197F(x, v)                                          \
	(BIT_CLEAR_AVGPERIOD_8197F(x) | BIT_AVGPERIOD_8197F(v))

/* 2 REG_VO_ADMTIME_8197F */

#define BIT_SHIFT_VO_ADMITTED_TIME_8197F 0
#define BIT_MASK_VO_ADMITTED_TIME_8197F 0xffff
#define BIT_VO_ADMITTED_TIME_8197F(x)                                          \
	(((x) & BIT_MASK_VO_ADMITTED_TIME_8197F)                               \
	 << BIT_SHIFT_VO_ADMITTED_TIME_8197F)
#define BITS_VO_ADMITTED_TIME_8197F                                            \
	(BIT_MASK_VO_ADMITTED_TIME_8197F << BIT_SHIFT_VO_ADMITTED_TIME_8197F)
#define BIT_CLEAR_VO_ADMITTED_TIME_8197F(x)                                    \
	((x) & (~BITS_VO_ADMITTED_TIME_8197F))
#define BIT_GET_VO_ADMITTED_TIME_8197F(x)                                      \
	(((x) >> BIT_SHIFT_VO_ADMITTED_TIME_8197F) &                           \
	 BIT_MASK_VO_ADMITTED_TIME_8197F)
#define BIT_SET_VO_ADMITTED_TIME_8197F(x, v)                                   \
	(BIT_CLEAR_VO_ADMITTED_TIME_8197F(x) | BIT_VO_ADMITTED_TIME_8197F(v))

/* 2 REG_VI_ADMTIME_8197F */

#define BIT_SHIFT_VI_ADMITTED_TIME_8197F 0
#define BIT_MASK_VI_ADMITTED_TIME_8197F 0xffff
#define BIT_VI_ADMITTED_TIME_8197F(x)                                          \
	(((x) & BIT_MASK_VI_ADMITTED_TIME_8197F)                               \
	 << BIT_SHIFT_VI_ADMITTED_TIME_8197F)
#define BITS_VI_ADMITTED_TIME_8197F                                            \
	(BIT_MASK_VI_ADMITTED_TIME_8197F << BIT_SHIFT_VI_ADMITTED_TIME_8197F)
#define BIT_CLEAR_VI_ADMITTED_TIME_8197F(x)                                    \
	((x) & (~BITS_VI_ADMITTED_TIME_8197F))
#define BIT_GET_VI_ADMITTED_TIME_8197F(x)                                      \
	(((x) >> BIT_SHIFT_VI_ADMITTED_TIME_8197F) &                           \
	 BIT_MASK_VI_ADMITTED_TIME_8197F)
#define BIT_SET_VI_ADMITTED_TIME_8197F(x, v)                                   \
	(BIT_CLEAR_VI_ADMITTED_TIME_8197F(x) | BIT_VI_ADMITTED_TIME_8197F(v))

/* 2 REG_BE_ADMTIME_8197F */

#define BIT_SHIFT_BE_ADMITTED_TIME_8197F 0
#define BIT_MASK_BE_ADMITTED_TIME_8197F 0xffff
#define BIT_BE_ADMITTED_TIME_8197F(x)                                          \
	(((x) & BIT_MASK_BE_ADMITTED_TIME_8197F)                               \
	 << BIT_SHIFT_BE_ADMITTED_TIME_8197F)
#define BITS_BE_ADMITTED_TIME_8197F                                            \
	(BIT_MASK_BE_ADMITTED_TIME_8197F << BIT_SHIFT_BE_ADMITTED_TIME_8197F)
#define BIT_CLEAR_BE_ADMITTED_TIME_8197F(x)                                    \
	((x) & (~BITS_BE_ADMITTED_TIME_8197F))
#define BIT_GET_BE_ADMITTED_TIME_8197F(x)                                      \
	(((x) >> BIT_SHIFT_BE_ADMITTED_TIME_8197F) &                           \
	 BIT_MASK_BE_ADMITTED_TIME_8197F)
#define BIT_SET_BE_ADMITTED_TIME_8197F(x, v)                                   \
	(BIT_CLEAR_BE_ADMITTED_TIME_8197F(x) | BIT_BE_ADMITTED_TIME_8197F(v))

/* 2 REG_NOT_VALID_8197F */
#define BIT_CHANGE_POW_BCN_AREA_8197F BIT(9)

/* 2 REG_EDCA_RANDOM_GEN_8197F */

#define BIT_SHIFT_RANDOM_GEN_8197F 0
#define BIT_MASK_RANDOM_GEN_8197F 0xffffff
#define BIT_RANDOM_GEN_8197F(x)                                                \
	(((x) & BIT_MASK_RANDOM_GEN_8197F) << BIT_SHIFT_RANDOM_GEN_8197F)
#define BITS_RANDOM_GEN_8197F                                                  \
	(BIT_MASK_RANDOM_GEN_8197F << BIT_SHIFT_RANDOM_GEN_8197F)
#define BIT_CLEAR_RANDOM_GEN_8197F(x) ((x) & (~BITS_RANDOM_GEN_8197F))
#define BIT_GET_RANDOM_GEN_8197F(x)                                            \
	(((x) >> BIT_SHIFT_RANDOM_GEN_8197F) & BIT_MASK_RANDOM_GEN_8197F)
#define BIT_SET_RANDOM_GEN_8197F(x, v)                                         \
	(BIT_CLEAR_RANDOM_GEN_8197F(x) | BIT_RANDOM_GEN_8197F(v))

/* 2 REG_TXCMD_NOA_SEL_8197F */

#define BIT_SHIFT_NOA_SEL_V2_8197F 4
#define BIT_MASK_NOA_SEL_V2_8197F 0x7
#define BIT_NOA_SEL_V2_8197F(x)                                                \
	(((x) & BIT_MASK_NOA_SEL_V2_8197F) << BIT_SHIFT_NOA_SEL_V2_8197F)
#define BITS_NOA_SEL_V2_8197F                                                  \
	(BIT_MASK_NOA_SEL_V2_8197F << BIT_SHIFT_NOA_SEL_V2_8197F)
#define BIT_CLEAR_NOA_SEL_V2_8197F(x) ((x) & (~BITS_NOA_SEL_V2_8197F))
#define BIT_GET_NOA_SEL_V2_8197F(x)                                            \
	(((x) >> BIT_SHIFT_NOA_SEL_V2_8197F) & BIT_MASK_NOA_SEL_V2_8197F)
#define BIT_SET_NOA_SEL_V2_8197F(x, v)                                         \
	(BIT_CLEAR_NOA_SEL_V2_8197F(x) | BIT_NOA_SEL_V2_8197F(v))

#define BIT_SHIFT_TXCMD_SEG_SEL_8197F 0
#define BIT_MASK_TXCMD_SEG_SEL_8197F 0xf
#define BIT_TXCMD_SEG_SEL_8197F(x)                                             \
	(((x) & BIT_MASK_TXCMD_SEG_SEL_8197F) << BIT_SHIFT_TXCMD_SEG_SEL_8197F)
#define BITS_TXCMD_SEG_SEL_8197F                                               \
	(BIT_MASK_TXCMD_SEG_SEL_8197F << BIT_SHIFT_TXCMD_SEG_SEL_8197F)
#define BIT_CLEAR_TXCMD_SEG_SEL_8197F(x) ((x) & (~BITS_TXCMD_SEG_SEL_8197F))
#define BIT_GET_TXCMD_SEG_SEL_8197F(x)                                         \
	(((x) >> BIT_SHIFT_TXCMD_SEG_SEL_8197F) & BIT_MASK_TXCMD_SEG_SEL_8197F)
#define BIT_SET_TXCMD_SEG_SEL_8197F(x, v)                                      \
	(BIT_CLEAR_TXCMD_SEG_SEL_8197F(x) | BIT_TXCMD_SEG_SEL_8197F(v))

/* 2 REG_NOT_VALID_8197F */
#define BIT_BCNERR_CNT_EN_8197F BIT(20)

#define BIT_SHIFT_BCNERR_PORT_SEL_8197F 16
#define BIT_MASK_BCNERR_PORT_SEL_8197F 0x7
#define BIT_BCNERR_PORT_SEL_8197F(x)                                           \
	(((x) & BIT_MASK_BCNERR_PORT_SEL_8197F)                                \
	 << BIT_SHIFT_BCNERR_PORT_SEL_8197F)
#define BITS_BCNERR_PORT_SEL_8197F                                             \
	(BIT_MASK_BCNERR_PORT_SEL_8197F << BIT_SHIFT_BCNERR_PORT_SEL_8197F)
#define BIT_CLEAR_BCNERR_PORT_SEL_8197F(x) ((x) & (~BITS_BCNERR_PORT_SEL_8197F))
#define BIT_GET_BCNERR_PORT_SEL_8197F(x)                                       \
	(((x) >> BIT_SHIFT_BCNERR_PORT_SEL_8197F) &                            \
	 BIT_MASK_BCNERR_PORT_SEL_8197F)
#define BIT_SET_BCNERR_PORT_SEL_8197F(x, v)                                    \
	(BIT_CLEAR_BCNERR_PORT_SEL_8197F(x) | BIT_BCNERR_PORT_SEL_8197F(v))

#define BIT_SHIFT_TXPAUSE1_8197F 8
#define BIT_MASK_TXPAUSE1_8197F 0xff
#define BIT_TXPAUSE1_8197F(x)                                                  \
	(((x) & BIT_MASK_TXPAUSE1_8197F) << BIT_SHIFT_TXPAUSE1_8197F)
#define BITS_TXPAUSE1_8197F                                                    \
	(BIT_MASK_TXPAUSE1_8197F << BIT_SHIFT_TXPAUSE1_8197F)
#define BIT_CLEAR_TXPAUSE1_8197F(x) ((x) & (~BITS_TXPAUSE1_8197F))
#define BIT_GET_TXPAUSE1_8197F(x)                                              \
	(((x) >> BIT_SHIFT_TXPAUSE1_8197F) & BIT_MASK_TXPAUSE1_8197F)
#define BIT_SET_TXPAUSE1_8197F(x, v)                                           \
	(BIT_CLEAR_TXPAUSE1_8197F(x) | BIT_TXPAUSE1_8197F(v))

#define BIT_SHIFT_BW_CFG_8197F 0
#define BIT_MASK_BW_CFG_8197F 0x3
#define BIT_BW_CFG_8197F(x)                                                    \
	(((x) & BIT_MASK_BW_CFG_8197F) << BIT_SHIFT_BW_CFG_8197F)
#define BITS_BW_CFG_8197F (BIT_MASK_BW_CFG_8197F << BIT_SHIFT_BW_CFG_8197F)
#define BIT_CLEAR_BW_CFG_8197F(x) ((x) & (~BITS_BW_CFG_8197F))
#define BIT_GET_BW_CFG_8197F(x)                                                \
	(((x) >> BIT_SHIFT_BW_CFG_8197F) & BIT_MASK_BW_CFG_8197F)
#define BIT_SET_BW_CFG_8197F(x, v)                                             \
	(BIT_CLEAR_BW_CFG_8197F(x) | BIT_BW_CFG_8197F(v))

/* 2 REG_NOT_VALID_8197F */

#define BIT_SHIFT_RXBCN_TIMER_8197F 16
#define BIT_MASK_RXBCN_TIMER_8197F 0xffff
#define BIT_RXBCN_TIMER_8197F(x)                                               \
	(((x) & BIT_MASK_RXBCN_TIMER_8197F) << BIT_SHIFT_RXBCN_TIMER_8197F)
#define BITS_RXBCN_TIMER_8197F                                                 \
	(BIT_MASK_RXBCN_TIMER_8197F << BIT_SHIFT_RXBCN_TIMER_8197F)
#define BIT_CLEAR_RXBCN_TIMER_8197F(x) ((x) & (~BITS_RXBCN_TIMER_8197F))
#define BIT_GET_RXBCN_TIMER_8197F(x)                                           \
	(((x) >> BIT_SHIFT_RXBCN_TIMER_8197F) & BIT_MASK_RXBCN_TIMER_8197F)
#define BIT_SET_RXBCN_TIMER_8197F(x, v)                                        \
	(BIT_CLEAR_RXBCN_TIMER_8197F(x) | BIT_RXBCN_TIMER_8197F(v))

#define BIT_SHIFT_BCN_ELY_ADJ_8197F 0
#define BIT_MASK_BCN_ELY_ADJ_8197F 0xffff
#define BIT_BCN_ELY_ADJ_8197F(x)                                               \
	(((x) & BIT_MASK_BCN_ELY_ADJ_8197F) << BIT_SHIFT_BCN_ELY_ADJ_8197F)
#define BITS_BCN_ELY_ADJ_8197F                                                 \
	(BIT_MASK_BCN_ELY_ADJ_8197F << BIT_SHIFT_BCN_ELY_ADJ_8197F)
#define BIT_CLEAR_BCN_ELY_ADJ_8197F(x) ((x) & (~BITS_BCN_ELY_ADJ_8197F))
#define BIT_GET_BCN_ELY_ADJ_8197F(x)                                           \
	(((x) >> BIT_SHIFT_BCN_ELY_ADJ_8197F) & BIT_MASK_BCN_ELY_ADJ_8197F)
#define BIT_SET_BCN_ELY_ADJ_8197F(x, v)                                        \
	(BIT_CLEAR_BCN_ELY_ADJ_8197F(x) | BIT_BCN_ELY_ADJ_8197F(v))

/* 2 REG_NOT_VALID_8197F */

#define BIT_SHIFT_BCNERR_CNT_OTHERS_8197F 24
#define BIT_MASK_BCNERR_CNT_OTHERS_8197F 0xff
#define BIT_BCNERR_CNT_OTHERS_8197F(x)                                         \
	(((x) & BIT_MASK_BCNERR_CNT_OTHERS_8197F)                              \
	 << BIT_SHIFT_BCNERR_CNT_OTHERS_8197F)
#define BITS_BCNERR_CNT_OTHERS_8197F                                           \
	(BIT_MASK_BCNERR_CNT_OTHERS_8197F << BIT_SHIFT_BCNERR_CNT_OTHERS_8197F)
#define BIT_CLEAR_BCNERR_CNT_OTHERS_8197F(x)                                   \
	((x) & (~BITS_BCNERR_CNT_OTHERS_8197F))
#define BIT_GET_BCNERR_CNT_OTHERS_8197F(x)                                     \
	(((x) >> BIT_SHIFT_BCNERR_CNT_OTHERS_8197F) &                          \
	 BIT_MASK_BCNERR_CNT_OTHERS_8197F)
#define BIT_SET_BCNERR_CNT_OTHERS_8197F(x, v)                                  \
	(BIT_CLEAR_BCNERR_CNT_OTHERS_8197F(x) | BIT_BCNERR_CNT_OTHERS_8197F(v))

#define BIT_SHIFT_BCNERR_CNT_INVALID_8197F 16
#define BIT_MASK_BCNERR_CNT_INVALID_8197F 0xff
#define BIT_BCNERR_CNT_INVALID_8197F(x)                                        \
	(((x) & BIT_MASK_BCNERR_CNT_INVALID_8197F)                             \
	 << BIT_SHIFT_BCNERR_CNT_INVALID_8197F)
#define BITS_BCNERR_CNT_INVALID_8197F                                          \
	(BIT_MASK_BCNERR_CNT_INVALID_8197F                                     \
	 << BIT_SHIFT_BCNERR_CNT_INVALID_8197F)
#define BIT_CLEAR_BCNERR_CNT_INVALID_8197F(x)                                  \
	((x) & (~BITS_BCNERR_CNT_INVALID_8197F))
#define BIT_GET_BCNERR_CNT_INVALID_8197F(x)                                    \
	(((x) >> BIT_SHIFT_BCNERR_CNT_INVALID_8197F) &                         \
	 BIT_MASK_BCNERR_CNT_INVALID_8197F)
#define BIT_SET_BCNERR_CNT_INVALID_8197F(x, v)                                 \
	(BIT_CLEAR_BCNERR_CNT_INVALID_8197F(x) |                               \
	 BIT_BCNERR_CNT_INVALID_8197F(v))

#define BIT_SHIFT_BCNERR_CNT_MAC_8197F 8
#define BIT_MASK_BCNERR_CNT_MAC_8197F 0xff
#define BIT_BCNERR_CNT_MAC_8197F(x)                                            \
	(((x) & BIT_MASK_BCNERR_CNT_MAC_8197F)                                 \
	 << BIT_SHIFT_BCNERR_CNT_MAC_8197F)
#define BITS_BCNERR_CNT_MAC_8197F                                              \
	(BIT_MASK_BCNERR_CNT_MAC_8197F << BIT_SHIFT_BCNERR_CNT_MAC_8197F)
#define BIT_CLEAR_BCNERR_CNT_MAC_8197F(x) ((x) & (~BITS_BCNERR_CNT_MAC_8197F))
#define BIT_GET_BCNERR_CNT_MAC_8197F(x)                                        \
	(((x) >> BIT_SHIFT_BCNERR_CNT_MAC_8197F) &                             \
	 BIT_MASK_BCNERR_CNT_MAC_8197F)
#define BIT_SET_BCNERR_CNT_MAC_8197F(x, v)                                     \
	(BIT_CLEAR_BCNERR_CNT_MAC_8197F(x) | BIT_BCNERR_CNT_MAC_8197F(v))

#define BIT_SHIFT_BCNERR_CNT_CCA_8197F 0
#define BIT_MASK_BCNERR_CNT_CCA_8197F 0xff
#define BIT_BCNERR_CNT_CCA_8197F(x)                                            \
	(((x) & BIT_MASK_BCNERR_CNT_CCA_8197F)                                 \
	 << BIT_SHIFT_BCNERR_CNT_CCA_8197F)
#define BITS_BCNERR_CNT_CCA_8197F                                              \
	(BIT_MASK_BCNERR_CNT_CCA_8197F << BIT_SHIFT_BCNERR_CNT_CCA_8197F)
#define BIT_CLEAR_BCNERR_CNT_CCA_8197F(x) ((x) & (~BITS_BCNERR_CNT_CCA_8197F))
#define BIT_GET_BCNERR_CNT_CCA_8197F(x)                                        \
	(((x) >> BIT_SHIFT_BCNERR_CNT_CCA_8197F) &                             \
	 BIT_MASK_BCNERR_CNT_CCA_8197F)
#define BIT_SET_BCNERR_CNT_CCA_8197F(x, v)                                     \
	(BIT_CLEAR_BCNERR_CNT_CCA_8197F(x) | BIT_BCNERR_CNT_CCA_8197F(v))

/* 2 REG_NOA_PARAM_8197F */

#define BIT_SHIFT_NOA_COUNT_8197F (96 & CPU_OPT_WIDTH)
#define BIT_MASK_NOA_COUNT_8197F 0xff
#define BIT_NOA_COUNT_8197F(x)                                                 \
	(((x) & BIT_MASK_NOA_COUNT_8197F) << BIT_SHIFT_NOA_COUNT_8197F)
#define BITS_NOA_COUNT_8197F                                                   \
	(BIT_MASK_NOA_COUNT_8197F << BIT_SHIFT_NOA_COUNT_8197F)
#define BIT_CLEAR_NOA_COUNT_8197F(x) ((x) & (~BITS_NOA_COUNT_8197F))
#define BIT_GET_NOA_COUNT_8197F(x)                                             \
	(((x) >> BIT_SHIFT_NOA_COUNT_8197F) & BIT_MASK_NOA_COUNT_8197F)
#define BIT_SET_NOA_COUNT_8197F(x, v)                                          \
	(BIT_CLEAR_NOA_COUNT_8197F(x) | BIT_NOA_COUNT_8197F(v))

#define BIT_SHIFT_NOA_START_TIME_8197F (64 & CPU_OPT_WIDTH)
#define BIT_MASK_NOA_START_TIME_8197F 0xffffffffL
#define BIT_NOA_START_TIME_8197F(x)                                            \
	(((x) & BIT_MASK_NOA_START_TIME_8197F)                                 \
	 << BIT_SHIFT_NOA_START_TIME_8197F)
#define BITS_NOA_START_TIME_8197F                                              \
	(BIT_MASK_NOA_START_TIME_8197F << BIT_SHIFT_NOA_START_TIME_8197F)
#define BIT_CLEAR_NOA_START_TIME_8197F(x) ((x) & (~BITS_NOA_START_TIME_8197F))
#define BIT_GET_NOA_START_TIME_8197F(x)                                        \
	(((x) >> BIT_SHIFT_NOA_START_TIME_8197F) &                             \
	 BIT_MASK_NOA_START_TIME_8197F)
#define BIT_SET_NOA_START_TIME_8197F(x, v)                                     \
	(BIT_CLEAR_NOA_START_TIME_8197F(x) | BIT_NOA_START_TIME_8197F(v))

#define BIT_SHIFT_NOA_INTERVAL_8197F (32 & CPU_OPT_WIDTH)
#define BIT_MASK_NOA_INTERVAL_8197F 0xffffffffL
#define BIT_NOA_INTERVAL_8197F(x)                                              \
	(((x) & BIT_MASK_NOA_INTERVAL_8197F) << BIT_SHIFT_NOA_INTERVAL_8197F)
#define BITS_NOA_INTERVAL_8197F                                                \
	(BIT_MASK_NOA_INTERVAL_8197F << BIT_SHIFT_NOA_INTERVAL_8197F)
#define BIT_CLEAR_NOA_INTERVAL_8197F(x) ((x) & (~BITS_NOA_INTERVAL_8197F))
#define BIT_GET_NOA_INTERVAL_8197F(x)                                          \
	(((x) >> BIT_SHIFT_NOA_INTERVAL_8197F) & BIT_MASK_NOA_INTERVAL_8197F)
#define BIT_SET_NOA_INTERVAL_8197F(x, v)                                       \
	(BIT_CLEAR_NOA_INTERVAL_8197F(x) | BIT_NOA_INTERVAL_8197F(v))

#define BIT_SHIFT_NOA_DURATION_8197F 0
#define BIT_MASK_NOA_DURATION_8197F 0xffffffffL
#define BIT_NOA_DURATION_8197F(x)                                              \
	(((x) & BIT_MASK_NOA_DURATION_8197F) << BIT_SHIFT_NOA_DURATION_8197F)
#define BITS_NOA_DURATION_8197F                                                \
	(BIT_MASK_NOA_DURATION_8197F << BIT_SHIFT_NOA_DURATION_8197F)
#define BIT_CLEAR_NOA_DURATION_8197F(x) ((x) & (~BITS_NOA_DURATION_8197F))
#define BIT_GET_NOA_DURATION_8197F(x)                                          \
	(((x) >> BIT_SHIFT_NOA_DURATION_8197F) & BIT_MASK_NOA_DURATION_8197F)
#define BIT_SET_NOA_DURATION_8197F(x, v)                                       \
	(BIT_CLEAR_NOA_DURATION_8197F(x) | BIT_NOA_DURATION_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_P2P_RST_8197F */
#define BIT_P2P2_PWR_RST1_8197F BIT(5)
#define BIT_P2P2_PWR_RST0_8197F BIT(4)
#define BIT_P2P1_PWR_RST1_8197F BIT(3)
#define BIT_P2P1_PWR_RST0_8197F BIT(2)
#define BIT_P2P_PWR_RST1_V1_8197F BIT(1)
#define BIT_P2P_PWR_RST0_V1_8197F BIT(0)

/* 2 REG_SCHEDULER_RST_8197F */
#define BIT_SYNC_TSF_NOW_8197F BIT(2)
#define BIT_SYNC_CLI_8197F BIT(1)
#define BIT_SCHEDULER_RST_V1_8197F BIT(0)

/* 2 REG_SCH_TXCMD_8197F */

#define BIT_SHIFT_SCH_TXCMD_8197F 0
#define BIT_MASK_SCH_TXCMD_8197F 0xffffffffL
#define BIT_SCH_TXCMD_8197F(x)                                                 \
	(((x) & BIT_MASK_SCH_TXCMD_8197F) << BIT_SHIFT_SCH_TXCMD_8197F)
#define BITS_SCH_TXCMD_8197F                                                   \
	(BIT_MASK_SCH_TXCMD_8197F << BIT_SHIFT_SCH_TXCMD_8197F)
#define BIT_CLEAR_SCH_TXCMD_8197F(x) ((x) & (~BITS_SCH_TXCMD_8197F))
#define BIT_GET_SCH_TXCMD_8197F(x)                                             \
	(((x) >> BIT_SHIFT_SCH_TXCMD_8197F) & BIT_MASK_SCH_TXCMD_8197F)
#define BIT_SET_SCH_TXCMD_8197F(x, v)                                          \
	(BIT_CLEAR_SCH_TXCMD_8197F(x) | BIT_SCH_TXCMD_8197F(v))

/* 2 REG_PAGE5_DUMMY_8197F */

/* 2 REG_CPUMGQ_TX_TIMER_8197F */

#define BIT_SHIFT_CPUMGQ_TX_TIMER_V1_8197F 0
#define BIT_MASK_CPUMGQ_TX_TIMER_V1_8197F 0xffffffffL
#define BIT_CPUMGQ_TX_TIMER_V1_8197F(x)                                        \
	(((x) & BIT_MASK_CPUMGQ_TX_TIMER_V1_8197F)                             \
	 << BIT_SHIFT_CPUMGQ_TX_TIMER_V1_8197F)
#define BITS_CPUMGQ_TX_TIMER_V1_8197F                                          \
	(BIT_MASK_CPUMGQ_TX_TIMER_V1_8197F                                     \
	 << BIT_SHIFT_CPUMGQ_TX_TIMER_V1_8197F)
#define BIT_CLEAR_CPUMGQ_TX_TIMER_V1_8197F(x)                                  \
	((x) & (~BITS_CPUMGQ_TX_TIMER_V1_8197F))
#define BIT_GET_CPUMGQ_TX_TIMER_V1_8197F(x)                                    \
	(((x) >> BIT_SHIFT_CPUMGQ_TX_TIMER_V1_8197F) &                         \
	 BIT_MASK_CPUMGQ_TX_TIMER_V1_8197F)
#define BIT_SET_CPUMGQ_TX_TIMER_V1_8197F(x, v)                                 \
	(BIT_CLEAR_CPUMGQ_TX_TIMER_V1_8197F(x) |                               \
	 BIT_CPUMGQ_TX_TIMER_V1_8197F(v))

/* 2 REG_PS_TIMER_A_8197F */

#define BIT_SHIFT_PS_TIMER_A_V1_8197F 0
#define BIT_MASK_PS_TIMER_A_V1_8197F 0xffffffffL
#define BIT_PS_TIMER_A_V1_8197F(x)                                             \
	(((x) & BIT_MASK_PS_TIMER_A_V1_8197F) << BIT_SHIFT_PS_TIMER_A_V1_8197F)
#define BITS_PS_TIMER_A_V1_8197F                                               \
	(BIT_MASK_PS_TIMER_A_V1_8197F << BIT_SHIFT_PS_TIMER_A_V1_8197F)
#define BIT_CLEAR_PS_TIMER_A_V1_8197F(x) ((x) & (~BITS_PS_TIMER_A_V1_8197F))
#define BIT_GET_PS_TIMER_A_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_PS_TIMER_A_V1_8197F) & BIT_MASK_PS_TIMER_A_V1_8197F)
#define BIT_SET_PS_TIMER_A_V1_8197F(x, v)                                      \
	(BIT_CLEAR_PS_TIMER_A_V1_8197F(x) | BIT_PS_TIMER_A_V1_8197F(v))

/* 2 REG_PS_TIMER_B_8197F */

#define BIT_SHIFT_PS_TIMER_B_V1_8197F 0
#define BIT_MASK_PS_TIMER_B_V1_8197F 0xffffffffL
#define BIT_PS_TIMER_B_V1_8197F(x)                                             \
	(((x) & BIT_MASK_PS_TIMER_B_V1_8197F) << BIT_SHIFT_PS_TIMER_B_V1_8197F)
#define BITS_PS_TIMER_B_V1_8197F                                               \
	(BIT_MASK_PS_TIMER_B_V1_8197F << BIT_SHIFT_PS_TIMER_B_V1_8197F)
#define BIT_CLEAR_PS_TIMER_B_V1_8197F(x) ((x) & (~BITS_PS_TIMER_B_V1_8197F))
#define BIT_GET_PS_TIMER_B_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_PS_TIMER_B_V1_8197F) & BIT_MASK_PS_TIMER_B_V1_8197F)
#define BIT_SET_PS_TIMER_B_V1_8197F(x, v)                                      \
	(BIT_CLEAR_PS_TIMER_B_V1_8197F(x) | BIT_PS_TIMER_B_V1_8197F(v))

/* 2 REG_PS_TIMER_C_8197F */

#define BIT_SHIFT_PS_TIMER_C_V1_8197F 0
#define BIT_MASK_PS_TIMER_C_V1_8197F 0xffffffffL
#define BIT_PS_TIMER_C_V1_8197F(x)                                             \
	(((x) & BIT_MASK_PS_TIMER_C_V1_8197F) << BIT_SHIFT_PS_TIMER_C_V1_8197F)
#define BITS_PS_TIMER_C_V1_8197F                                               \
	(BIT_MASK_PS_TIMER_C_V1_8197F << BIT_SHIFT_PS_TIMER_C_V1_8197F)
#define BIT_CLEAR_PS_TIMER_C_V1_8197F(x) ((x) & (~BITS_PS_TIMER_C_V1_8197F))
#define BIT_GET_PS_TIMER_C_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_PS_TIMER_C_V1_8197F) & BIT_MASK_PS_TIMER_C_V1_8197F)
#define BIT_SET_PS_TIMER_C_V1_8197F(x, v)                                      \
	(BIT_CLEAR_PS_TIMER_C_V1_8197F(x) | BIT_PS_TIMER_C_V1_8197F(v))

/* 2 REG_PS_TIMER_ABC_CPUMGQ_TIMER_CRTL_8197F */
#define BIT_CPUMGQ_TIMER_EN_8197F BIT(31)
#define BIT_CPUMGQ_TX_EN_8197F BIT(28)

#define BIT_SHIFT_CPUMGQ_TIMER_TSF_SEL_8197F 24
#define BIT_MASK_CPUMGQ_TIMER_TSF_SEL_8197F 0x7
#define BIT_CPUMGQ_TIMER_TSF_SEL_8197F(x)                                      \
	(((x) & BIT_MASK_CPUMGQ_TIMER_TSF_SEL_8197F)                           \
	 << BIT_SHIFT_CPUMGQ_TIMER_TSF_SEL_8197F)
#define BITS_CPUMGQ_TIMER_TSF_SEL_8197F                                        \
	(BIT_MASK_CPUMGQ_TIMER_TSF_SEL_8197F                                   \
	 << BIT_SHIFT_CPUMGQ_TIMER_TSF_SEL_8197F)
#define BIT_CLEAR_CPUMGQ_TIMER_TSF_SEL_8197F(x)                                \
	((x) & (~BITS_CPUMGQ_TIMER_TSF_SEL_8197F))
#define BIT_GET_CPUMGQ_TIMER_TSF_SEL_8197F(x)                                  \
	(((x) >> BIT_SHIFT_CPUMGQ_TIMER_TSF_SEL_8197F) &                       \
	 BIT_MASK_CPUMGQ_TIMER_TSF_SEL_8197F)
#define BIT_SET_CPUMGQ_TIMER_TSF_SEL_8197F(x, v)                               \
	(BIT_CLEAR_CPUMGQ_TIMER_TSF_SEL_8197F(x) |                             \
	 BIT_CPUMGQ_TIMER_TSF_SEL_8197F(v))

#define BIT_PS_TIMER_C_EN_8197F BIT(23)

#define BIT_SHIFT_PS_TIMER_C_TSF_SEL_8197F 16
#define BIT_MASK_PS_TIMER_C_TSF_SEL_8197F 0x7
#define BIT_PS_TIMER_C_TSF_SEL_8197F(x)                                        \
	(((x) & BIT_MASK_PS_TIMER_C_TSF_SEL_8197F)                             \
	 << BIT_SHIFT_PS_TIMER_C_TSF_SEL_8197F)
#define BITS_PS_TIMER_C_TSF_SEL_8197F                                          \
	(BIT_MASK_PS_TIMER_C_TSF_SEL_8197F                                     \
	 << BIT_SHIFT_PS_TIMER_C_TSF_SEL_8197F)
#define BIT_CLEAR_PS_TIMER_C_TSF_SEL_8197F(x)                                  \
	((x) & (~BITS_PS_TIMER_C_TSF_SEL_8197F))
#define BIT_GET_PS_TIMER_C_TSF_SEL_8197F(x)                                    \
	(((x) >> BIT_SHIFT_PS_TIMER_C_TSF_SEL_8197F) &                         \
	 BIT_MASK_PS_TIMER_C_TSF_SEL_8197F)
#define BIT_SET_PS_TIMER_C_TSF_SEL_8197F(x, v)                                 \
	(BIT_CLEAR_PS_TIMER_C_TSF_SEL_8197F(x) |                               \
	 BIT_PS_TIMER_C_TSF_SEL_8197F(v))

#define BIT_PS_TIMER_B_EN_8197F BIT(15)

#define BIT_SHIFT_PS_TIMER_B_TSF_SEL_8197F 8
#define BIT_MASK_PS_TIMER_B_TSF_SEL_8197F 0x7
#define BIT_PS_TIMER_B_TSF_SEL_8197F(x)                                        \
	(((x) & BIT_MASK_PS_TIMER_B_TSF_SEL_8197F)                             \
	 << BIT_SHIFT_PS_TIMER_B_TSF_SEL_8197F)
#define BITS_PS_TIMER_B_TSF_SEL_8197F                                          \
	(BIT_MASK_PS_TIMER_B_TSF_SEL_8197F                                     \
	 << BIT_SHIFT_PS_TIMER_B_TSF_SEL_8197F)
#define BIT_CLEAR_PS_TIMER_B_TSF_SEL_8197F(x)                                  \
	((x) & (~BITS_PS_TIMER_B_TSF_SEL_8197F))
#define BIT_GET_PS_TIMER_B_TSF_SEL_8197F(x)                                    \
	(((x) >> BIT_SHIFT_PS_TIMER_B_TSF_SEL_8197F) &                         \
	 BIT_MASK_PS_TIMER_B_TSF_SEL_8197F)
#define BIT_SET_PS_TIMER_B_TSF_SEL_8197F(x, v)                                 \
	(BIT_CLEAR_PS_TIMER_B_TSF_SEL_8197F(x) |                               \
	 BIT_PS_TIMER_B_TSF_SEL_8197F(v))

#define BIT_PS_TIMER_A_EN_8197F BIT(7)

#define BIT_SHIFT_PS_TIMER_A_TSF_SEL_8197F 0
#define BIT_MASK_PS_TIMER_A_TSF_SEL_8197F 0x7
#define BIT_PS_TIMER_A_TSF_SEL_8197F(x)                                        \
	(((x) & BIT_MASK_PS_TIMER_A_TSF_SEL_8197F)                             \
	 << BIT_SHIFT_PS_TIMER_A_TSF_SEL_8197F)
#define BITS_PS_TIMER_A_TSF_SEL_8197F                                          \
	(BIT_MASK_PS_TIMER_A_TSF_SEL_8197F                                     \
	 << BIT_SHIFT_PS_TIMER_A_TSF_SEL_8197F)
#define BIT_CLEAR_PS_TIMER_A_TSF_SEL_8197F(x)                                  \
	((x) & (~BITS_PS_TIMER_A_TSF_SEL_8197F))
#define BIT_GET_PS_TIMER_A_TSF_SEL_8197F(x)                                    \
	(((x) >> BIT_SHIFT_PS_TIMER_A_TSF_SEL_8197F) &                         \
	 BIT_MASK_PS_TIMER_A_TSF_SEL_8197F)
#define BIT_SET_PS_TIMER_A_TSF_SEL_8197F(x, v)                                 \
	(BIT_CLEAR_PS_TIMER_A_TSF_SEL_8197F(x) |                               \
	 BIT_PS_TIMER_A_TSF_SEL_8197F(v))

/* 2 REG_CPUMGQ_TX_TIMER_EARLY_8197F */

#define BIT_SHIFT_CPUMGQ_TX_TIMER_EARLY_8197F 0
#define BIT_MASK_CPUMGQ_TX_TIMER_EARLY_8197F 0xff
#define BIT_CPUMGQ_TX_TIMER_EARLY_8197F(x)                                     \
	(((x) & BIT_MASK_CPUMGQ_TX_TIMER_EARLY_8197F)                          \
	 << BIT_SHIFT_CPUMGQ_TX_TIMER_EARLY_8197F)
#define BITS_CPUMGQ_TX_TIMER_EARLY_8197F                                       \
	(BIT_MASK_CPUMGQ_TX_TIMER_EARLY_8197F                                  \
	 << BIT_SHIFT_CPUMGQ_TX_TIMER_EARLY_8197F)
#define BIT_CLEAR_CPUMGQ_TX_TIMER_EARLY_8197F(x)                               \
	((x) & (~BITS_CPUMGQ_TX_TIMER_EARLY_8197F))
#define BIT_GET_CPUMGQ_TX_TIMER_EARLY_8197F(x)                                 \
	(((x) >> BIT_SHIFT_CPUMGQ_TX_TIMER_EARLY_8197F) &                      \
	 BIT_MASK_CPUMGQ_TX_TIMER_EARLY_8197F)
#define BIT_SET_CPUMGQ_TX_TIMER_EARLY_8197F(x, v)                              \
	(BIT_CLEAR_CPUMGQ_TX_TIMER_EARLY_8197F(x) |                            \
	 BIT_CPUMGQ_TX_TIMER_EARLY_8197F(v))

/* 2 REG_PS_TIMER_A_EARLY_8197F */

#define BIT_SHIFT_PS_TIMER_A_EARLY_8197F 0
#define BIT_MASK_PS_TIMER_A_EARLY_8197F 0xff
#define BIT_PS_TIMER_A_EARLY_8197F(x)                                          \
	(((x) & BIT_MASK_PS_TIMER_A_EARLY_8197F)                               \
	 << BIT_SHIFT_PS_TIMER_A_EARLY_8197F)
#define BITS_PS_TIMER_A_EARLY_8197F                                            \
	(BIT_MASK_PS_TIMER_A_EARLY_8197F << BIT_SHIFT_PS_TIMER_A_EARLY_8197F)
#define BIT_CLEAR_PS_TIMER_A_EARLY_8197F(x)                                    \
	((x) & (~BITS_PS_TIMER_A_EARLY_8197F))
#define BIT_GET_PS_TIMER_A_EARLY_8197F(x)                                      \
	(((x) >> BIT_SHIFT_PS_TIMER_A_EARLY_8197F) &                           \
	 BIT_MASK_PS_TIMER_A_EARLY_8197F)
#define BIT_SET_PS_TIMER_A_EARLY_8197F(x, v)                                   \
	(BIT_CLEAR_PS_TIMER_A_EARLY_8197F(x) | BIT_PS_TIMER_A_EARLY_8197F(v))

/* 2 REG_PS_TIMER_B_EARLY_8197F */

#define BIT_SHIFT_PS_TIMER_B_EARLY_8197F 0
#define BIT_MASK_PS_TIMER_B_EARLY_8197F 0xff
#define BIT_PS_TIMER_B_EARLY_8197F(x)                                          \
	(((x) & BIT_MASK_PS_TIMER_B_EARLY_8197F)                               \
	 << BIT_SHIFT_PS_TIMER_B_EARLY_8197F)
#define BITS_PS_TIMER_B_EARLY_8197F                                            \
	(BIT_MASK_PS_TIMER_B_EARLY_8197F << BIT_SHIFT_PS_TIMER_B_EARLY_8197F)
#define BIT_CLEAR_PS_TIMER_B_EARLY_8197F(x)                                    \
	((x) & (~BITS_PS_TIMER_B_EARLY_8197F))
#define BIT_GET_PS_TIMER_B_EARLY_8197F(x)                                      \
	(((x) >> BIT_SHIFT_PS_TIMER_B_EARLY_8197F) &                           \
	 BIT_MASK_PS_TIMER_B_EARLY_8197F)
#define BIT_SET_PS_TIMER_B_EARLY_8197F(x, v)                                   \
	(BIT_CLEAR_PS_TIMER_B_EARLY_8197F(x) | BIT_PS_TIMER_B_EARLY_8197F(v))

/* 2 REG_PS_TIMER_C_EARLY_8197F */

#define BIT_SHIFT_PS_TIMER_C_EARLY_8197F 0
#define BIT_MASK_PS_TIMER_C_EARLY_8197F 0xff
#define BIT_PS_TIMER_C_EARLY_8197F(x)                                          \
	(((x) & BIT_MASK_PS_TIMER_C_EARLY_8197F)                               \
	 << BIT_SHIFT_PS_TIMER_C_EARLY_8197F)
#define BITS_PS_TIMER_C_EARLY_8197F                                            \
	(BIT_MASK_PS_TIMER_C_EARLY_8197F << BIT_SHIFT_PS_TIMER_C_EARLY_8197F)
#define BIT_CLEAR_PS_TIMER_C_EARLY_8197F(x)                                    \
	((x) & (~BITS_PS_TIMER_C_EARLY_8197F))
#define BIT_GET_PS_TIMER_C_EARLY_8197F(x)                                      \
	(((x) >> BIT_SHIFT_PS_TIMER_C_EARLY_8197F) &                           \
	 BIT_MASK_PS_TIMER_C_EARLY_8197F)
#define BIT_SET_PS_TIMER_C_EARLY_8197F(x, v)                                   \
	(BIT_CLEAR_PS_TIMER_C_EARLY_8197F(x) | BIT_PS_TIMER_C_EARLY_8197F(v))

/* 2 REG_NOT_VALID_8197F */
#define BIT_STOP_CPUMGQ_8197F BIT(16)

#define BIT_SHIFT_CPUMGQ_PARAMETER_8197F 0
#define BIT_MASK_CPUMGQ_PARAMETER_8197F 0xffff
#define BIT_CPUMGQ_PARAMETER_8197F(x)                                          \
	(((x) & BIT_MASK_CPUMGQ_PARAMETER_8197F)                               \
	 << BIT_SHIFT_CPUMGQ_PARAMETER_8197F)
#define BITS_CPUMGQ_PARAMETER_8197F                                            \
	(BIT_MASK_CPUMGQ_PARAMETER_8197F << BIT_SHIFT_CPUMGQ_PARAMETER_8197F)
#define BIT_CLEAR_CPUMGQ_PARAMETER_8197F(x)                                    \
	((x) & (~BITS_CPUMGQ_PARAMETER_8197F))
#define BIT_GET_CPUMGQ_PARAMETER_8197F(x)                                      \
	(((x) >> BIT_SHIFT_CPUMGQ_PARAMETER_8197F) &                           \
	 BIT_MASK_CPUMGQ_PARAMETER_8197F)
#define BIT_SET_CPUMGQ_PARAMETER_8197F(x, v)                                   \
	(BIT_CLEAR_CPUMGQ_PARAMETER_8197F(x) | BIT_CPUMGQ_PARAMETER_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_BWOPMODE_8197F (BW OPERATION MODE REGISTER) */

/* 2 REG_WMAC_FWPKT_CR_8197F */
#define BIT_FWEN_8197F BIT(7)
#define BIT_PHYSTS_PKT_CTRL_8197F BIT(6)
#define BIT_APPHDR_MIDSRCH_FAIL_8197F BIT(4)
#define BIT_FWPARSING_EN_8197F BIT(3)

#define BIT_SHIFT_APPEND_MHDR_LEN_8197F 0
#define BIT_MASK_APPEND_MHDR_LEN_8197F 0x7
#define BIT_APPEND_MHDR_LEN_8197F(x)                                           \
	(((x) & BIT_MASK_APPEND_MHDR_LEN_8197F)                                \
	 << BIT_SHIFT_APPEND_MHDR_LEN_8197F)
#define BITS_APPEND_MHDR_LEN_8197F                                             \
	(BIT_MASK_APPEND_MHDR_LEN_8197F << BIT_SHIFT_APPEND_MHDR_LEN_8197F)
#define BIT_CLEAR_APPEND_MHDR_LEN_8197F(x) ((x) & (~BITS_APPEND_MHDR_LEN_8197F))
#define BIT_GET_APPEND_MHDR_LEN_8197F(x)                                       \
	(((x) >> BIT_SHIFT_APPEND_MHDR_LEN_8197F) &                            \
	 BIT_MASK_APPEND_MHDR_LEN_8197F)
#define BIT_SET_APPEND_MHDR_LEN_8197F(x, v)                                    \
	(BIT_CLEAR_APPEND_MHDR_LEN_8197F(x) | BIT_APPEND_MHDR_LEN_8197F(v))

/* 2 REG_WMAC_CR_8197F (WMAC CR AND APSD CONTROL REGISTER) */
#define BIT_APSDOFF_8197F BIT(6)
#define BIT_IC_MACPHY_M_8197F BIT(0)

/* 2 REG_TCR_8197F (TRANSMISSION CONFIGURATION REGISTER) */
#define BIT_WMAC_EN_RTS_ADDR_8197F BIT(31)
#define BIT_WMAC_DISABLE_CCK_8197F BIT(30)
#define BIT_WMAC_RAW_LEN_8197F BIT(29)
#define BIT_WMAC_NOTX_IN_RXNDP_8197F BIT(28)
#define BIT_WMAC_EN_EOF_8197F BIT(27)
#define BIT_WMAC_BF_SEL_8197F BIT(26)
#define BIT_WMAC_ANTMODE_SEL_8197F BIT(25)
#define BIT_WMAC_TCRPWRMGT_HWCTL_8197F BIT(24)
#define BIT_WMAC_SMOOTH_VAL_8197F BIT(23)
#define BIT_UNDERFLOWEN_CMPLEN_SEL_8197F BIT(21)
#define BIT_FETCH_MPDU_AFTER_WSEC_RDY_8197F BIT(20)
#define BIT_WMAC_TCR_EN_20MST_8197F BIT(19)
#define BIT_WMAC_DIS_SIGTA_8197F BIT(18)
#define BIT_WMAC_DIS_A2B0_8197F BIT(17)
#define BIT_WMAC_MSK_SIGBCRC_8197F BIT(16)
#define BIT_WMAC_TCR_ERRSTEN_3_8197F BIT(15)
#define BIT_WMAC_TCR_ERRSTEN_2_8197F BIT(14)
#define BIT_WMAC_TCR_ERRSTEN_1_8197F BIT(13)
#define BIT_WMAC_TCR_ERRSTEN_0_8197F BIT(12)
#define BIT_WMAC_TCR_TXSK_PERPKT_8197F BIT(11)
#define BIT_ICV_8197F BIT(10)
#define BIT_CFEND_FORMAT_8197F BIT(9)
#define BIT_CRC_8197F BIT(8)
#define BIT_PWRBIT_OW_EN_8197F BIT(7)
#define BIT_PWR_ST_8197F BIT(6)
#define BIT_WMAC_TCR_UPD_TIMIE_8197F BIT(5)
#define BIT_WMAC_TCR_UPD_HGQMD_8197F BIT(4)
#define BIT_VHTSIGA1_TXPS_8197F BIT(3)
#define BIT_PAD_SEL_8197F BIT(2)
#define BIT_DIS_GCLK_8197F BIT(1)

/* 2 REG_RCR_8197F (RECEIVE CONFIGURATION REGISTER) */
#define BIT_APP_FCS_8197F BIT(31)
#define BIT_APP_MIC_8197F BIT(30)
#define BIT_APP_ICV_8197F BIT(29)
#define BIT_APP_PHYSTS_8197F BIT(28)
#define BIT_APP_BASSN_8197F BIT(27)
#define BIT_VHT_DACK_8197F BIT(26)
#define BIT_TCPOFLD_EN_8197F BIT(25)
#define BIT_ENMBID_8197F BIT(24)
#define BIT_LSIGEN_8197F BIT(23)
#define BIT_MFBEN_8197F BIT(22)
#define BIT_DISCHKPPDLLEN_8197F BIT(21)
#define BIT_PKTCTL_DLEN_8197F BIT(20)
#define BIT_TIM_PARSER_EN_8197F BIT(18)
#define BIT_BC_MD_EN_8197F BIT(17)
#define BIT_UC_MD_EN_8197F BIT(16)
#define BIT_RXSK_PERPKT_8197F BIT(15)
#define BIT_HTC_LOC_CTRL_8197F BIT(14)
#define BIT_TA_BCN_8197F BIT(11)
#define BIT_DISDECMYPKT_8197F BIT(10)
#define BIT_AICV_8197F BIT(9)
#define BIT_ACRC32_8197F BIT(8)
#define BIT_CBSSID_BCN_8197F BIT(7)
#define BIT_CBSSID_DATA_8197F BIT(6)
#define BIT_APWRMGT_8197F BIT(5)
#define BIT_ADD3_8197F BIT(4)
#define BIT_AB_8197F BIT(3)
#define BIT_AM_8197F BIT(2)
#define BIT_APM_8197F BIT(1)
#define BIT_AAP_8197F BIT(0)

/* 2 REG_RX_DRVINFO_SZ_8197F (RX DRIVER INFO SIZE REGISTER) */
#define BIT_APP_PHYSTS_PER_SUBMPDU_8197F BIT(7)
#define BIT_APP_MH_SHIFT_VAL_8197F BIT(6)
#define BIT_WMAC_ENSHIFT_8197F BIT(5)

#define BIT_SHIFT_DRVINFO_SZ_V1_8197F 0
#define BIT_MASK_DRVINFO_SZ_V1_8197F 0xf
#define BIT_DRVINFO_SZ_V1_8197F(x)                                             \
	(((x) & BIT_MASK_DRVINFO_SZ_V1_8197F) << BIT_SHIFT_DRVINFO_SZ_V1_8197F)
#define BITS_DRVINFO_SZ_V1_8197F                                               \
	(BIT_MASK_DRVINFO_SZ_V1_8197F << BIT_SHIFT_DRVINFO_SZ_V1_8197F)
#define BIT_CLEAR_DRVINFO_SZ_V1_8197F(x) ((x) & (~BITS_DRVINFO_SZ_V1_8197F))
#define BIT_GET_DRVINFO_SZ_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_DRVINFO_SZ_V1_8197F) & BIT_MASK_DRVINFO_SZ_V1_8197F)
#define BIT_SET_DRVINFO_SZ_V1_8197F(x, v)                                      \
	(BIT_CLEAR_DRVINFO_SZ_V1_8197F(x) | BIT_DRVINFO_SZ_V1_8197F(v))

/* 2 REG_RX_DLK_TIME_8197F (RX DEADLOCK TIME REGISTER) */

#define BIT_SHIFT_RX_DLK_TIME_8197F 0
#define BIT_MASK_RX_DLK_TIME_8197F 0xff
#define BIT_RX_DLK_TIME_8197F(x)                                               \
	(((x) & BIT_MASK_RX_DLK_TIME_8197F) << BIT_SHIFT_RX_DLK_TIME_8197F)
#define BITS_RX_DLK_TIME_8197F                                                 \
	(BIT_MASK_RX_DLK_TIME_8197F << BIT_SHIFT_RX_DLK_TIME_8197F)
#define BIT_CLEAR_RX_DLK_TIME_8197F(x) ((x) & (~BITS_RX_DLK_TIME_8197F))
#define BIT_GET_RX_DLK_TIME_8197F(x)                                           \
	(((x) >> BIT_SHIFT_RX_DLK_TIME_8197F) & BIT_MASK_RX_DLK_TIME_8197F)
#define BIT_SET_RX_DLK_TIME_8197F(x, v)                                        \
	(BIT_CLEAR_RX_DLK_TIME_8197F(x) | BIT_RX_DLK_TIME_8197F(v))

/* 2 REG_RX_PKT_LIMIT_8197F (RX PACKET LENGTH LIMIT REGISTER) */

#define BIT_SHIFT_RXPKTLMT_8197F 0
#define BIT_MASK_RXPKTLMT_8197F 0x3f
#define BIT_RXPKTLMT_8197F(x)                                                  \
	(((x) & BIT_MASK_RXPKTLMT_8197F) << BIT_SHIFT_RXPKTLMT_8197F)
#define BITS_RXPKTLMT_8197F                                                    \
	(BIT_MASK_RXPKTLMT_8197F << BIT_SHIFT_RXPKTLMT_8197F)
#define BIT_CLEAR_RXPKTLMT_8197F(x) ((x) & (~BITS_RXPKTLMT_8197F))
#define BIT_GET_RXPKTLMT_8197F(x)                                              \
	(((x) >> BIT_SHIFT_RXPKTLMT_8197F) & BIT_MASK_RXPKTLMT_8197F)
#define BIT_SET_RXPKTLMT_8197F(x, v)                                           \
	(BIT_CLEAR_RXPKTLMT_8197F(x) | BIT_RXPKTLMT_8197F(v))

/* 2 REG_MACID_8197F (MAC ID REGISTER) */

#define BIT_SHIFT_MACID_8197F 0
#define BIT_MASK_MACID_8197F 0xffffffffffffL
#define BIT_MACID_8197F(x)                                                     \
	(((x) & BIT_MASK_MACID_8197F) << BIT_SHIFT_MACID_8197F)
#define BITS_MACID_8197F (BIT_MASK_MACID_8197F << BIT_SHIFT_MACID_8197F)
#define BIT_CLEAR_MACID_8197F(x) ((x) & (~BITS_MACID_8197F))
#define BIT_GET_MACID_8197F(x)                                                 \
	(((x) >> BIT_SHIFT_MACID_8197F) & BIT_MASK_MACID_8197F)
#define BIT_SET_MACID_8197F(x, v)                                              \
	(BIT_CLEAR_MACID_8197F(x) | BIT_MACID_8197F(v))

/* 2 REG_BSSID_8197F (BSSID REGISTER) */

#define BIT_SHIFT_BSSID_8197F 0
#define BIT_MASK_BSSID_8197F 0xffffffffffffL
#define BIT_BSSID_8197F(x)                                                     \
	(((x) & BIT_MASK_BSSID_8197F) << BIT_SHIFT_BSSID_8197F)
#define BITS_BSSID_8197F (BIT_MASK_BSSID_8197F << BIT_SHIFT_BSSID_8197F)
#define BIT_CLEAR_BSSID_8197F(x) ((x) & (~BITS_BSSID_8197F))
#define BIT_GET_BSSID_8197F(x)                                                 \
	(((x) >> BIT_SHIFT_BSSID_8197F) & BIT_MASK_BSSID_8197F)
#define BIT_SET_BSSID_8197F(x, v)                                              \
	(BIT_CLEAR_BSSID_8197F(x) | BIT_BSSID_8197F(v))

/* 2 REG_MAR_8197F (MULTICAST ADDRESS REGISTER) */

#define BIT_SHIFT_MAR_8197F 0
#define BIT_MASK_MAR_8197F 0xffffffffffffffffL
#define BIT_MAR_8197F(x) (((x) & BIT_MASK_MAR_8197F) << BIT_SHIFT_MAR_8197F)
#define BITS_MAR_8197F (BIT_MASK_MAR_8197F << BIT_SHIFT_MAR_8197F)
#define BIT_CLEAR_MAR_8197F(x) ((x) & (~BITS_MAR_8197F))
#define BIT_GET_MAR_8197F(x) (((x) >> BIT_SHIFT_MAR_8197F) & BIT_MASK_MAR_8197F)
#define BIT_SET_MAR_8197F(x, v) (BIT_CLEAR_MAR_8197F(x) | BIT_MAR_8197F(v))

/* 2 REG_MBIDCAMCFG_1_8197F (MBSSID CAM CONFIGURATION REGISTER) */

#define BIT_SHIFT_MBIDCAM_RWDATA_L_8197F 0
#define BIT_MASK_MBIDCAM_RWDATA_L_8197F 0xffffffffL
#define BIT_MBIDCAM_RWDATA_L_8197F(x)                                          \
	(((x) & BIT_MASK_MBIDCAM_RWDATA_L_8197F)                               \
	 << BIT_SHIFT_MBIDCAM_RWDATA_L_8197F)
#define BITS_MBIDCAM_RWDATA_L_8197F                                            \
	(BIT_MASK_MBIDCAM_RWDATA_L_8197F << BIT_SHIFT_MBIDCAM_RWDATA_L_8197F)
#define BIT_CLEAR_MBIDCAM_RWDATA_L_8197F(x)                                    \
	((x) & (~BITS_MBIDCAM_RWDATA_L_8197F))
#define BIT_GET_MBIDCAM_RWDATA_L_8197F(x)                                      \
	(((x) >> BIT_SHIFT_MBIDCAM_RWDATA_L_8197F) &                           \
	 BIT_MASK_MBIDCAM_RWDATA_L_8197F)
#define BIT_SET_MBIDCAM_RWDATA_L_8197F(x, v)                                   \
	(BIT_CLEAR_MBIDCAM_RWDATA_L_8197F(x) | BIT_MBIDCAM_RWDATA_L_8197F(v))

/* 2 REG_MBIDCAMCFG_2_8197F (MBSSID CAM CONFIGURATION REGISTER) */
#define BIT_MBIDCAM_POLL_8197F BIT(31)
#define BIT_MBIDCAM_WT_EN_8197F BIT(30)

#define BIT_SHIFT_MBIDCAM_ADDR_V1_8197F 24
#define BIT_MASK_MBIDCAM_ADDR_V1_8197F 0x3f
#define BIT_MBIDCAM_ADDR_V1_8197F(x)                                           \
	(((x) & BIT_MASK_MBIDCAM_ADDR_V1_8197F)                                \
	 << BIT_SHIFT_MBIDCAM_ADDR_V1_8197F)
#define BITS_MBIDCAM_ADDR_V1_8197F                                             \
	(BIT_MASK_MBIDCAM_ADDR_V1_8197F << BIT_SHIFT_MBIDCAM_ADDR_V1_8197F)
#define BIT_CLEAR_MBIDCAM_ADDR_V1_8197F(x) ((x) & (~BITS_MBIDCAM_ADDR_V1_8197F))
#define BIT_GET_MBIDCAM_ADDR_V1_8197F(x)                                       \
	(((x) >> BIT_SHIFT_MBIDCAM_ADDR_V1_8197F) &                            \
	 BIT_MASK_MBIDCAM_ADDR_V1_8197F)
#define BIT_SET_MBIDCAM_ADDR_V1_8197F(x, v)                                    \
	(BIT_CLEAR_MBIDCAM_ADDR_V1_8197F(x) | BIT_MBIDCAM_ADDR_V1_8197F(v))

#define BIT_MBIDCAM_VALID_8197F BIT(23)
#define BIT_LSIC_TXOP_EN_8197F BIT(17)
#define BIT_REPEAT_MODE_EN_8197F BIT(16)

#define BIT_SHIFT_MBIDCAM_RWDATA_H_8197F 0
#define BIT_MASK_MBIDCAM_RWDATA_H_8197F 0xffff
#define BIT_MBIDCAM_RWDATA_H_8197F(x)                                          \
	(((x) & BIT_MASK_MBIDCAM_RWDATA_H_8197F)                               \
	 << BIT_SHIFT_MBIDCAM_RWDATA_H_8197F)
#define BITS_MBIDCAM_RWDATA_H_8197F                                            \
	(BIT_MASK_MBIDCAM_RWDATA_H_8197F << BIT_SHIFT_MBIDCAM_RWDATA_H_8197F)
#define BIT_CLEAR_MBIDCAM_RWDATA_H_8197F(x)                                    \
	((x) & (~BITS_MBIDCAM_RWDATA_H_8197F))
#define BIT_GET_MBIDCAM_RWDATA_H_8197F(x)                                      \
	(((x) >> BIT_SHIFT_MBIDCAM_RWDATA_H_8197F) &                           \
	 BIT_MASK_MBIDCAM_RWDATA_H_8197F)
#define BIT_SET_MBIDCAM_RWDATA_H_8197F(x, v)                                   \
	(BIT_CLEAR_MBIDCAM_RWDATA_H_8197F(x) | BIT_MBIDCAM_RWDATA_H_8197F(v))

/* 2 REG_ZLD_NUM_8197F */

#define BIT_SHIFT_ZLD_NUM_8197F 0
#define BIT_MASK_ZLD_NUM_8197F 0xff
#define BIT_ZLD_NUM_8197F(x)                                                   \
	(((x) & BIT_MASK_ZLD_NUM_8197F) << BIT_SHIFT_ZLD_NUM_8197F)
#define BITS_ZLD_NUM_8197F (BIT_MASK_ZLD_NUM_8197F << BIT_SHIFT_ZLD_NUM_8197F)
#define BIT_CLEAR_ZLD_NUM_8197F(x) ((x) & (~BITS_ZLD_NUM_8197F))
#define BIT_GET_ZLD_NUM_8197F(x)                                               \
	(((x) >> BIT_SHIFT_ZLD_NUM_8197F) & BIT_MASK_ZLD_NUM_8197F)
#define BIT_SET_ZLD_NUM_8197F(x, v)                                            \
	(BIT_CLEAR_ZLD_NUM_8197F(x) | BIT_ZLD_NUM_8197F(v))

/* 2 REG_UDF_THSD_8197F */

#define BIT_SHIFT_UDF_THSD_8197F 0
#define BIT_MASK_UDF_THSD_8197F 0xff
#define BIT_UDF_THSD_8197F(x)                                                  \
	(((x) & BIT_MASK_UDF_THSD_8197F) << BIT_SHIFT_UDF_THSD_8197F)
#define BITS_UDF_THSD_8197F                                                    \
	(BIT_MASK_UDF_THSD_8197F << BIT_SHIFT_UDF_THSD_8197F)
#define BIT_CLEAR_UDF_THSD_8197F(x) ((x) & (~BITS_UDF_THSD_8197F))
#define BIT_GET_UDF_THSD_8197F(x)                                              \
	(((x) >> BIT_SHIFT_UDF_THSD_8197F) & BIT_MASK_UDF_THSD_8197F)
#define BIT_SET_UDF_THSD_8197F(x, v)                                           \
	(BIT_CLEAR_UDF_THSD_8197F(x) | BIT_UDF_THSD_8197F(v))

/* 2 REG_WMAC_TCR_TSFT_OFS_8197F */

#define BIT_SHIFT_WMAC_TCR_TSFT_OFS_8197F 0
#define BIT_MASK_WMAC_TCR_TSFT_OFS_8197F 0xffff
#define BIT_WMAC_TCR_TSFT_OFS_8197F(x)                                         \
	(((x) & BIT_MASK_WMAC_TCR_TSFT_OFS_8197F)                              \
	 << BIT_SHIFT_WMAC_TCR_TSFT_OFS_8197F)
#define BITS_WMAC_TCR_TSFT_OFS_8197F                                           \
	(BIT_MASK_WMAC_TCR_TSFT_OFS_8197F << BIT_SHIFT_WMAC_TCR_TSFT_OFS_8197F)
#define BIT_CLEAR_WMAC_TCR_TSFT_OFS_8197F(x)                                   \
	((x) & (~BITS_WMAC_TCR_TSFT_OFS_8197F))
#define BIT_GET_WMAC_TCR_TSFT_OFS_8197F(x)                                     \
	(((x) >> BIT_SHIFT_WMAC_TCR_TSFT_OFS_8197F) &                          \
	 BIT_MASK_WMAC_TCR_TSFT_OFS_8197F)
#define BIT_SET_WMAC_TCR_TSFT_OFS_8197F(x, v)                                  \
	(BIT_CLEAR_WMAC_TCR_TSFT_OFS_8197F(x) | BIT_WMAC_TCR_TSFT_OFS_8197F(v))

/* 2 REG_MCU_TEST_2_V1_8197F */

#define BIT_SHIFT_MCU_RSVD_2_V1_8197F 0
#define BIT_MASK_MCU_RSVD_2_V1_8197F 0xffff
#define BIT_MCU_RSVD_2_V1_8197F(x)                                             \
	(((x) & BIT_MASK_MCU_RSVD_2_V1_8197F) << BIT_SHIFT_MCU_RSVD_2_V1_8197F)
#define BITS_MCU_RSVD_2_V1_8197F                                               \
	(BIT_MASK_MCU_RSVD_2_V1_8197F << BIT_SHIFT_MCU_RSVD_2_V1_8197F)
#define BIT_CLEAR_MCU_RSVD_2_V1_8197F(x) ((x) & (~BITS_MCU_RSVD_2_V1_8197F))
#define BIT_GET_MCU_RSVD_2_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_MCU_RSVD_2_V1_8197F) & BIT_MASK_MCU_RSVD_2_V1_8197F)
#define BIT_SET_MCU_RSVD_2_V1_8197F(x, v)                                      \
	(BIT_CLEAR_MCU_RSVD_2_V1_8197F(x) | BIT_MCU_RSVD_2_V1_8197F(v))

/* 2 REG_WMAC_TXTIMEOUT_8197F */

#define BIT_SHIFT_WMAC_TXTIMEOUT_8197F 0
#define BIT_MASK_WMAC_TXTIMEOUT_8197F 0xff
#define BIT_WMAC_TXTIMEOUT_8197F(x)                                            \
	(((x) & BIT_MASK_WMAC_TXTIMEOUT_8197F)                                 \
	 << BIT_SHIFT_WMAC_TXTIMEOUT_8197F)
#define BITS_WMAC_TXTIMEOUT_8197F                                              \
	(BIT_MASK_WMAC_TXTIMEOUT_8197F << BIT_SHIFT_WMAC_TXTIMEOUT_8197F)
#define BIT_CLEAR_WMAC_TXTIMEOUT_8197F(x) ((x) & (~BITS_WMAC_TXTIMEOUT_8197F))
#define BIT_GET_WMAC_TXTIMEOUT_8197F(x)                                        \
	(((x) >> BIT_SHIFT_WMAC_TXTIMEOUT_8197F) &                             \
	 BIT_MASK_WMAC_TXTIMEOUT_8197F)
#define BIT_SET_WMAC_TXTIMEOUT_8197F(x, v)                                     \
	(BIT_CLEAR_WMAC_TXTIMEOUT_8197F(x) | BIT_WMAC_TXTIMEOUT_8197F(v))

/* 2 REG_STMP_THSD_8197F */

#define BIT_SHIFT_STMP_THSD_8197F 0
#define BIT_MASK_STMP_THSD_8197F 0xff
#define BIT_STMP_THSD_8197F(x)                                                 \
	(((x) & BIT_MASK_STMP_THSD_8197F) << BIT_SHIFT_STMP_THSD_8197F)
#define BITS_STMP_THSD_8197F                                                   \
	(BIT_MASK_STMP_THSD_8197F << BIT_SHIFT_STMP_THSD_8197F)
#define BIT_CLEAR_STMP_THSD_8197F(x) ((x) & (~BITS_STMP_THSD_8197F))
#define BIT_GET_STMP_THSD_8197F(x)                                             \
	(((x) >> BIT_SHIFT_STMP_THSD_8197F) & BIT_MASK_STMP_THSD_8197F)
#define BIT_SET_STMP_THSD_8197F(x, v)                                          \
	(BIT_CLEAR_STMP_THSD_8197F(x) | BIT_STMP_THSD_8197F(v))

/* 2 REG_MAC_SPEC_SIFS_8197F (SPECIFICATION SIFS REGISTER) */

#define BIT_SHIFT_SPEC_SIFS_OFDM_8197F 8
#define BIT_MASK_SPEC_SIFS_OFDM_8197F 0xff
#define BIT_SPEC_SIFS_OFDM_8197F(x)                                            \
	(((x) & BIT_MASK_SPEC_SIFS_OFDM_8197F)                                 \
	 << BIT_SHIFT_SPEC_SIFS_OFDM_8197F)
#define BITS_SPEC_SIFS_OFDM_8197F                                              \
	(BIT_MASK_SPEC_SIFS_OFDM_8197F << BIT_SHIFT_SPEC_SIFS_OFDM_8197F)
#define BIT_CLEAR_SPEC_SIFS_OFDM_8197F(x) ((x) & (~BITS_SPEC_SIFS_OFDM_8197F))
#define BIT_GET_SPEC_SIFS_OFDM_8197F(x)                                        \
	(((x) >> BIT_SHIFT_SPEC_SIFS_OFDM_8197F) &                             \
	 BIT_MASK_SPEC_SIFS_OFDM_8197F)
#define BIT_SET_SPEC_SIFS_OFDM_8197F(x, v)                                     \
	(BIT_CLEAR_SPEC_SIFS_OFDM_8197F(x) | BIT_SPEC_SIFS_OFDM_8197F(v))

#define BIT_SHIFT_SPEC_SIFS_CCK_8197F 0
#define BIT_MASK_SPEC_SIFS_CCK_8197F 0xff
#define BIT_SPEC_SIFS_CCK_8197F(x)                                             \
	(((x) & BIT_MASK_SPEC_SIFS_CCK_8197F) << BIT_SHIFT_SPEC_SIFS_CCK_8197F)
#define BITS_SPEC_SIFS_CCK_8197F                                               \
	(BIT_MASK_SPEC_SIFS_CCK_8197F << BIT_SHIFT_SPEC_SIFS_CCK_8197F)
#define BIT_CLEAR_SPEC_SIFS_CCK_8197F(x) ((x) & (~BITS_SPEC_SIFS_CCK_8197F))
#define BIT_GET_SPEC_SIFS_CCK_8197F(x)                                         \
	(((x) >> BIT_SHIFT_SPEC_SIFS_CCK_8197F) & BIT_MASK_SPEC_SIFS_CCK_8197F)
#define BIT_SET_SPEC_SIFS_CCK_8197F(x, v)                                      \
	(BIT_CLEAR_SPEC_SIFS_CCK_8197F(x) | BIT_SPEC_SIFS_CCK_8197F(v))

/* 2 REG_USTIME_EDCA_8197F (US TIME TUNING FOR EDCA REGISTER) */

#define BIT_SHIFT_USTIME_EDCA_8197F 0
#define BIT_MASK_USTIME_EDCA_8197F 0xff
#define BIT_USTIME_EDCA_8197F(x)                                               \
	(((x) & BIT_MASK_USTIME_EDCA_8197F) << BIT_SHIFT_USTIME_EDCA_8197F)
#define BITS_USTIME_EDCA_8197F                                                 \
	(BIT_MASK_USTIME_EDCA_8197F << BIT_SHIFT_USTIME_EDCA_8197F)
#define BIT_CLEAR_USTIME_EDCA_8197F(x) ((x) & (~BITS_USTIME_EDCA_8197F))
#define BIT_GET_USTIME_EDCA_8197F(x)                                           \
	(((x) >> BIT_SHIFT_USTIME_EDCA_8197F) & BIT_MASK_USTIME_EDCA_8197F)
#define BIT_SET_USTIME_EDCA_8197F(x, v)                                        \
	(BIT_CLEAR_USTIME_EDCA_8197F(x) | BIT_USTIME_EDCA_8197F(v))

/* 2 REG_RESP_SIFS_OFDM_8197F (RESPONSE SIFS FOR OFDM REGISTER) */

#define BIT_SHIFT_SIFS_R2T_OFDM_8197F 8
#define BIT_MASK_SIFS_R2T_OFDM_8197F 0xff
#define BIT_SIFS_R2T_OFDM_8197F(x)                                             \
	(((x) & BIT_MASK_SIFS_R2T_OFDM_8197F) << BIT_SHIFT_SIFS_R2T_OFDM_8197F)
#define BITS_SIFS_R2T_OFDM_8197F                                               \
	(BIT_MASK_SIFS_R2T_OFDM_8197F << BIT_SHIFT_SIFS_R2T_OFDM_8197F)
#define BIT_CLEAR_SIFS_R2T_OFDM_8197F(x) ((x) & (~BITS_SIFS_R2T_OFDM_8197F))
#define BIT_GET_SIFS_R2T_OFDM_8197F(x)                                         \
	(((x) >> BIT_SHIFT_SIFS_R2T_OFDM_8197F) & BIT_MASK_SIFS_R2T_OFDM_8197F)
#define BIT_SET_SIFS_R2T_OFDM_8197F(x, v)                                      \
	(BIT_CLEAR_SIFS_R2T_OFDM_8197F(x) | BIT_SIFS_R2T_OFDM_8197F(v))

#define BIT_SHIFT_SIFS_T2T_OFDM_8197F 0
#define BIT_MASK_SIFS_T2T_OFDM_8197F 0xff
#define BIT_SIFS_T2T_OFDM_8197F(x)                                             \
	(((x) & BIT_MASK_SIFS_T2T_OFDM_8197F) << BIT_SHIFT_SIFS_T2T_OFDM_8197F)
#define BITS_SIFS_T2T_OFDM_8197F                                               \
	(BIT_MASK_SIFS_T2T_OFDM_8197F << BIT_SHIFT_SIFS_T2T_OFDM_8197F)
#define BIT_CLEAR_SIFS_T2T_OFDM_8197F(x) ((x) & (~BITS_SIFS_T2T_OFDM_8197F))
#define BIT_GET_SIFS_T2T_OFDM_8197F(x)                                         \
	(((x) >> BIT_SHIFT_SIFS_T2T_OFDM_8197F) & BIT_MASK_SIFS_T2T_OFDM_8197F)
#define BIT_SET_SIFS_T2T_OFDM_8197F(x, v)                                      \
	(BIT_CLEAR_SIFS_T2T_OFDM_8197F(x) | BIT_SIFS_T2T_OFDM_8197F(v))

/* 2 REG_RESP_SIFS_CCK_8197F (RESPONSE SIFS FOR CCK REGISTER) */

#define BIT_SHIFT_SIFS_R2T_CCK_8197F 8
#define BIT_MASK_SIFS_R2T_CCK_8197F 0xff
#define BIT_SIFS_R2T_CCK_8197F(x)                                              \
	(((x) & BIT_MASK_SIFS_R2T_CCK_8197F) << BIT_SHIFT_SIFS_R2T_CCK_8197F)
#define BITS_SIFS_R2T_CCK_8197F                                                \
	(BIT_MASK_SIFS_R2T_CCK_8197F << BIT_SHIFT_SIFS_R2T_CCK_8197F)
#define BIT_CLEAR_SIFS_R2T_CCK_8197F(x) ((x) & (~BITS_SIFS_R2T_CCK_8197F))
#define BIT_GET_SIFS_R2T_CCK_8197F(x)                                          \
	(((x) >> BIT_SHIFT_SIFS_R2T_CCK_8197F) & BIT_MASK_SIFS_R2T_CCK_8197F)
#define BIT_SET_SIFS_R2T_CCK_8197F(x, v)                                       \
	(BIT_CLEAR_SIFS_R2T_CCK_8197F(x) | BIT_SIFS_R2T_CCK_8197F(v))

#define BIT_SHIFT_SIFS_T2T_CCK_8197F 0
#define BIT_MASK_SIFS_T2T_CCK_8197F 0xff
#define BIT_SIFS_T2T_CCK_8197F(x)                                              \
	(((x) & BIT_MASK_SIFS_T2T_CCK_8197F) << BIT_SHIFT_SIFS_T2T_CCK_8197F)
#define BITS_SIFS_T2T_CCK_8197F                                                \
	(BIT_MASK_SIFS_T2T_CCK_8197F << BIT_SHIFT_SIFS_T2T_CCK_8197F)
#define BIT_CLEAR_SIFS_T2T_CCK_8197F(x) ((x) & (~BITS_SIFS_T2T_CCK_8197F))
#define BIT_GET_SIFS_T2T_CCK_8197F(x)                                          \
	(((x) >> BIT_SHIFT_SIFS_T2T_CCK_8197F) & BIT_MASK_SIFS_T2T_CCK_8197F)
#define BIT_SET_SIFS_T2T_CCK_8197F(x, v)                                       \
	(BIT_CLEAR_SIFS_T2T_CCK_8197F(x) | BIT_SIFS_T2T_CCK_8197F(v))

/* 2 REG_EIFS_8197F (EIFS REGISTER) */

#define BIT_SHIFT_EIFS_8197F 0
#define BIT_MASK_EIFS_8197F 0xffff
#define BIT_EIFS_8197F(x) (((x) & BIT_MASK_EIFS_8197F) << BIT_SHIFT_EIFS_8197F)
#define BITS_EIFS_8197F (BIT_MASK_EIFS_8197F << BIT_SHIFT_EIFS_8197F)
#define BIT_CLEAR_EIFS_8197F(x) ((x) & (~BITS_EIFS_8197F))
#define BIT_GET_EIFS_8197F(x)                                                  \
	(((x) >> BIT_SHIFT_EIFS_8197F) & BIT_MASK_EIFS_8197F)
#define BIT_SET_EIFS_8197F(x, v) (BIT_CLEAR_EIFS_8197F(x) | BIT_EIFS_8197F(v))

/* 2 REG_CTS2TO_8197F (CTS2 TIMEOUT REGISTER) */

#define BIT_SHIFT_CTS2TO_8197F 0
#define BIT_MASK_CTS2TO_8197F 0xff
#define BIT_CTS2TO_8197F(x)                                                    \
	(((x) & BIT_MASK_CTS2TO_8197F) << BIT_SHIFT_CTS2TO_8197F)
#define BITS_CTS2TO_8197F (BIT_MASK_CTS2TO_8197F << BIT_SHIFT_CTS2TO_8197F)
#define BIT_CLEAR_CTS2TO_8197F(x) ((x) & (~BITS_CTS2TO_8197F))
#define BIT_GET_CTS2TO_8197F(x)                                                \
	(((x) >> BIT_SHIFT_CTS2TO_8197F) & BIT_MASK_CTS2TO_8197F)
#define BIT_SET_CTS2TO_8197F(x, v)                                             \
	(BIT_CLEAR_CTS2TO_8197F(x) | BIT_CTS2TO_8197F(v))

/* 2 REG_ACKTO_8197F (ACK TIMEOUT REGISTER) */

#define BIT_SHIFT_ACKTO_8197F 0
#define BIT_MASK_ACKTO_8197F 0xff
#define BIT_ACKTO_8197F(x)                                                     \
	(((x) & BIT_MASK_ACKTO_8197F) << BIT_SHIFT_ACKTO_8197F)
#define BITS_ACKTO_8197F (BIT_MASK_ACKTO_8197F << BIT_SHIFT_ACKTO_8197F)
#define BIT_CLEAR_ACKTO_8197F(x) ((x) & (~BITS_ACKTO_8197F))
#define BIT_GET_ACKTO_8197F(x)                                                 \
	(((x) >> BIT_SHIFT_ACKTO_8197F) & BIT_MASK_ACKTO_8197F)
#define BIT_SET_ACKTO_8197F(x, v)                                              \
	(BIT_CLEAR_ACKTO_8197F(x) | BIT_ACKTO_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NAV_CTRL_8197F (NAV CONTROL REGISTER) */

#define BIT_SHIFT_NAV_UPPER_8197F 16
#define BIT_MASK_NAV_UPPER_8197F 0xff
#define BIT_NAV_UPPER_8197F(x)                                                 \
	(((x) & BIT_MASK_NAV_UPPER_8197F) << BIT_SHIFT_NAV_UPPER_8197F)
#define BITS_NAV_UPPER_8197F                                                   \
	(BIT_MASK_NAV_UPPER_8197F << BIT_SHIFT_NAV_UPPER_8197F)
#define BIT_CLEAR_NAV_UPPER_8197F(x) ((x) & (~BITS_NAV_UPPER_8197F))
#define BIT_GET_NAV_UPPER_8197F(x)                                             \
	(((x) >> BIT_SHIFT_NAV_UPPER_8197F) & BIT_MASK_NAV_UPPER_8197F)
#define BIT_SET_NAV_UPPER_8197F(x, v)                                          \
	(BIT_CLEAR_NAV_UPPER_8197F(x) | BIT_NAV_UPPER_8197F(v))

#define BIT_SHIFT_RXMYRTS_NAV_8197F 8
#define BIT_MASK_RXMYRTS_NAV_8197F 0xf
#define BIT_RXMYRTS_NAV_8197F(x)                                               \
	(((x) & BIT_MASK_RXMYRTS_NAV_8197F) << BIT_SHIFT_RXMYRTS_NAV_8197F)
#define BITS_RXMYRTS_NAV_8197F                                                 \
	(BIT_MASK_RXMYRTS_NAV_8197F << BIT_SHIFT_RXMYRTS_NAV_8197F)
#define BIT_CLEAR_RXMYRTS_NAV_8197F(x) ((x) & (~BITS_RXMYRTS_NAV_8197F))
#define BIT_GET_RXMYRTS_NAV_8197F(x)                                           \
	(((x) >> BIT_SHIFT_RXMYRTS_NAV_8197F) & BIT_MASK_RXMYRTS_NAV_8197F)
#define BIT_SET_RXMYRTS_NAV_8197F(x, v)                                        \
	(BIT_CLEAR_RXMYRTS_NAV_8197F(x) | BIT_RXMYRTS_NAV_8197F(v))

#define BIT_SHIFT_RTSRST_8197F 0
#define BIT_MASK_RTSRST_8197F 0xff
#define BIT_RTSRST_8197F(x)                                                    \
	(((x) & BIT_MASK_RTSRST_8197F) << BIT_SHIFT_RTSRST_8197F)
#define BITS_RTSRST_8197F (BIT_MASK_RTSRST_8197F << BIT_SHIFT_RTSRST_8197F)
#define BIT_CLEAR_RTSRST_8197F(x) ((x) & (~BITS_RTSRST_8197F))
#define BIT_GET_RTSRST_8197F(x)                                                \
	(((x) >> BIT_SHIFT_RTSRST_8197F) & BIT_MASK_RTSRST_8197F)
#define BIT_SET_RTSRST_8197F(x, v)                                             \
	(BIT_CLEAR_RTSRST_8197F(x) | BIT_RTSRST_8197F(v))

/* 2 REG_BACAMCMD_8197F (BLOCK ACK CAM COMMAND REGISTER) */
#define BIT_BACAM_POLL_8197F BIT(31)
#define BIT_BACAM_RST_8197F BIT(17)
#define BIT_BACAM_RW_8197F BIT(16)

#define BIT_SHIFT_TXSBM_8197F 14
#define BIT_MASK_TXSBM_8197F 0x3
#define BIT_TXSBM_8197F(x)                                                     \
	(((x) & BIT_MASK_TXSBM_8197F) << BIT_SHIFT_TXSBM_8197F)
#define BITS_TXSBM_8197F (BIT_MASK_TXSBM_8197F << BIT_SHIFT_TXSBM_8197F)
#define BIT_CLEAR_TXSBM_8197F(x) ((x) & (~BITS_TXSBM_8197F))
#define BIT_GET_TXSBM_8197F(x)                                                 \
	(((x) >> BIT_SHIFT_TXSBM_8197F) & BIT_MASK_TXSBM_8197F)
#define BIT_SET_TXSBM_8197F(x, v)                                              \
	(BIT_CLEAR_TXSBM_8197F(x) | BIT_TXSBM_8197F(v))

#define BIT_SHIFT_BACAM_ADDR_8197F 0
#define BIT_MASK_BACAM_ADDR_8197F 0x3f
#define BIT_BACAM_ADDR_8197F(x)                                                \
	(((x) & BIT_MASK_BACAM_ADDR_8197F) << BIT_SHIFT_BACAM_ADDR_8197F)
#define BITS_BACAM_ADDR_8197F                                                  \
	(BIT_MASK_BACAM_ADDR_8197F << BIT_SHIFT_BACAM_ADDR_8197F)
#define BIT_CLEAR_BACAM_ADDR_8197F(x) ((x) & (~BITS_BACAM_ADDR_8197F))
#define BIT_GET_BACAM_ADDR_8197F(x)                                            \
	(((x) >> BIT_SHIFT_BACAM_ADDR_8197F) & BIT_MASK_BACAM_ADDR_8197F)
#define BIT_SET_BACAM_ADDR_8197F(x, v)                                         \
	(BIT_CLEAR_BACAM_ADDR_8197F(x) | BIT_BACAM_ADDR_8197F(v))

/* 2 REG_BACAMCONTENT_8197F (BLOCK ACK CAM CONTENT REGISTER) */

#define BIT_SHIFT_BA_CONTENT_H_8197F (32 & CPU_OPT_WIDTH)
#define BIT_MASK_BA_CONTENT_H_8197F 0xffffffffL
#define BIT_BA_CONTENT_H_8197F(x)                                              \
	(((x) & BIT_MASK_BA_CONTENT_H_8197F) << BIT_SHIFT_BA_CONTENT_H_8197F)
#define BITS_BA_CONTENT_H_8197F                                                \
	(BIT_MASK_BA_CONTENT_H_8197F << BIT_SHIFT_BA_CONTENT_H_8197F)
#define BIT_CLEAR_BA_CONTENT_H_8197F(x) ((x) & (~BITS_BA_CONTENT_H_8197F))
#define BIT_GET_BA_CONTENT_H_8197F(x)                                          \
	(((x) >> BIT_SHIFT_BA_CONTENT_H_8197F) & BIT_MASK_BA_CONTENT_H_8197F)
#define BIT_SET_BA_CONTENT_H_8197F(x, v)                                       \
	(BIT_CLEAR_BA_CONTENT_H_8197F(x) | BIT_BA_CONTENT_H_8197F(v))

#define BIT_SHIFT_BA_CONTENT_L_8197F 0
#define BIT_MASK_BA_CONTENT_L_8197F 0xffffffffL
#define BIT_BA_CONTENT_L_8197F(x)                                              \
	(((x) & BIT_MASK_BA_CONTENT_L_8197F) << BIT_SHIFT_BA_CONTENT_L_8197F)
#define BITS_BA_CONTENT_L_8197F                                                \
	(BIT_MASK_BA_CONTENT_L_8197F << BIT_SHIFT_BA_CONTENT_L_8197F)
#define BIT_CLEAR_BA_CONTENT_L_8197F(x) ((x) & (~BITS_BA_CONTENT_L_8197F))
#define BIT_GET_BA_CONTENT_L_8197F(x)                                          \
	(((x) >> BIT_SHIFT_BA_CONTENT_L_8197F) & BIT_MASK_BA_CONTENT_L_8197F)
#define BIT_SET_BA_CONTENT_L_8197F(x, v)                                       \
	(BIT_CLEAR_BA_CONTENT_L_8197F(x) | BIT_BA_CONTENT_L_8197F(v))

/* 2 REG_WMAC_BITMAP_CTL_8197F */
#define BIT_BITMAP_VO_8197F BIT(7)
#define BIT_BITMAP_VI_8197F BIT(6)
#define BIT_BITMAP_BE_8197F BIT(5)
#define BIT_BITMAP_BK_8197F BIT(4)

#define BIT_SHIFT_BITMAP_CONDITION_8197F 2
#define BIT_MASK_BITMAP_CONDITION_8197F 0x3
#define BIT_BITMAP_CONDITION_8197F(x)                                          \
	(((x) & BIT_MASK_BITMAP_CONDITION_8197F)                               \
	 << BIT_SHIFT_BITMAP_CONDITION_8197F)
#define BITS_BITMAP_CONDITION_8197F                                            \
	(BIT_MASK_BITMAP_CONDITION_8197F << BIT_SHIFT_BITMAP_CONDITION_8197F)
#define BIT_CLEAR_BITMAP_CONDITION_8197F(x)                                    \
	((x) & (~BITS_BITMAP_CONDITION_8197F))
#define BIT_GET_BITMAP_CONDITION_8197F(x)                                      \
	(((x) >> BIT_SHIFT_BITMAP_CONDITION_8197F) &                           \
	 BIT_MASK_BITMAP_CONDITION_8197F)
#define BIT_SET_BITMAP_CONDITION_8197F(x, v)                                   \
	(BIT_CLEAR_BITMAP_CONDITION_8197F(x) | BIT_BITMAP_CONDITION_8197F(v))

#define BIT_BITMAP_SSNBK_COUNTER_CLR_8197F BIT(1)
#define BIT_BITMAP_FORCE_8197F BIT(0)

/* 2 REG_NOT_VALID_8197F */

#define BIT_SHIFT_RXPKT_TYPE_8197F 2
#define BIT_MASK_RXPKT_TYPE_8197F 0x3f
#define BIT_RXPKT_TYPE_8197F(x)                                                \
	(((x) & BIT_MASK_RXPKT_TYPE_8197F) << BIT_SHIFT_RXPKT_TYPE_8197F)
#define BITS_RXPKT_TYPE_8197F                                                  \
	(BIT_MASK_RXPKT_TYPE_8197F << BIT_SHIFT_RXPKT_TYPE_8197F)
#define BIT_CLEAR_RXPKT_TYPE_8197F(x) ((x) & (~BITS_RXPKT_TYPE_8197F))
#define BIT_GET_RXPKT_TYPE_8197F(x)                                            \
	(((x) >> BIT_SHIFT_RXPKT_TYPE_8197F) & BIT_MASK_RXPKT_TYPE_8197F)
#define BIT_SET_RXPKT_TYPE_8197F(x, v)                                         \
	(BIT_CLEAR_RXPKT_TYPE_8197F(x) | BIT_RXPKT_TYPE_8197F(v))

#define BIT_TXACT_IND_8197F BIT(1)
#define BIT_RXACT_IND_8197F BIT(0)

/* 2 REG_WMAC_BACAM_RPMEN_8197F */

#define BIT_SHIFT_BITMAP_SSNBK_COUNTER_8197F 2
#define BIT_MASK_BITMAP_SSNBK_COUNTER_8197F 0x3f
#define BIT_BITMAP_SSNBK_COUNTER_8197F(x)                                      \
	(((x) & BIT_MASK_BITMAP_SSNBK_COUNTER_8197F)                           \
	 << BIT_SHIFT_BITMAP_SSNBK_COUNTER_8197F)
#define BITS_BITMAP_SSNBK_COUNTER_8197F                                        \
	(BIT_MASK_BITMAP_SSNBK_COUNTER_8197F                                   \
	 << BIT_SHIFT_BITMAP_SSNBK_COUNTER_8197F)
#define BIT_CLEAR_BITMAP_SSNBK_COUNTER_8197F(x)                                \
	((x) & (~BITS_BITMAP_SSNBK_COUNTER_8197F))
#define BIT_GET_BITMAP_SSNBK_COUNTER_8197F(x)                                  \
	(((x) >> BIT_SHIFT_BITMAP_SSNBK_COUNTER_8197F) &                       \
	 BIT_MASK_BITMAP_SSNBK_COUNTER_8197F)
#define BIT_SET_BITMAP_SSNBK_COUNTER_8197F(x, v)                               \
	(BIT_CLEAR_BITMAP_SSNBK_COUNTER_8197F(x) |                             \
	 BIT_BITMAP_SSNBK_COUNTER_8197F(v))

#define BIT_BITMAP_EN_8197F BIT(1)
#define BIT_WMAC_BACAM_RPMEN_8197F BIT(0)

/* 2 REG_LBDLY_8197F (LOOPBACK DELAY REGISTER) */

#define BIT_SHIFT_LBDLY_8197F 0
#define BIT_MASK_LBDLY_8197F 0x1f
#define BIT_LBDLY_8197F(x)                                                     \
	(((x) & BIT_MASK_LBDLY_8197F) << BIT_SHIFT_LBDLY_8197F)
#define BITS_LBDLY_8197F (BIT_MASK_LBDLY_8197F << BIT_SHIFT_LBDLY_8197F)
#define BIT_CLEAR_LBDLY_8197F(x) ((x) & (~BITS_LBDLY_8197F))
#define BIT_GET_LBDLY_8197F(x)                                                 \
	(((x) >> BIT_SHIFT_LBDLY_8197F) & BIT_MASK_LBDLY_8197F)
#define BIT_SET_LBDLY_8197F(x, v)                                              \
	(BIT_CLEAR_LBDLY_8197F(x) | BIT_LBDLY_8197F(v))

/* 2 REG_RXERR_RPT_8197F (RX ERROR REPORT REGISTER) */

#define BIT_SHIFT_RXERR_RPT_SEL_V1_3_0_8197F 28
#define BIT_MASK_RXERR_RPT_SEL_V1_3_0_8197F 0xf
#define BIT_RXERR_RPT_SEL_V1_3_0_8197F(x)                                      \
	(((x) & BIT_MASK_RXERR_RPT_SEL_V1_3_0_8197F)                           \
	 << BIT_SHIFT_RXERR_RPT_SEL_V1_3_0_8197F)
#define BITS_RXERR_RPT_SEL_V1_3_0_8197F                                        \
	(BIT_MASK_RXERR_RPT_SEL_V1_3_0_8197F                                   \
	 << BIT_SHIFT_RXERR_RPT_SEL_V1_3_0_8197F)
#define BIT_CLEAR_RXERR_RPT_SEL_V1_3_0_8197F(x)                                \
	((x) & (~BITS_RXERR_RPT_SEL_V1_3_0_8197F))
#define BIT_GET_RXERR_RPT_SEL_V1_3_0_8197F(x)                                  \
	(((x) >> BIT_SHIFT_RXERR_RPT_SEL_V1_3_0_8197F) &                       \
	 BIT_MASK_RXERR_RPT_SEL_V1_3_0_8197F)
#define BIT_SET_RXERR_RPT_SEL_V1_3_0_8197F(x, v)                               \
	(BIT_CLEAR_RXERR_RPT_SEL_V1_3_0_8197F(x) |                             \
	 BIT_RXERR_RPT_SEL_V1_3_0_8197F(v))

#define BIT_RXERR_RPT_RST_8197F BIT(27)
#define BIT_RXERR_RPT_SEL_V1_4_8197F BIT(26)

#define BIT_SHIFT_UD_SELECT_BSSID_2_1_8197F 24
#define BIT_MASK_UD_SELECT_BSSID_2_1_8197F 0x3
#define BIT_UD_SELECT_BSSID_2_1_8197F(x)                                       \
	(((x) & BIT_MASK_UD_SELECT_BSSID_2_1_8197F)                            \
	 << BIT_SHIFT_UD_SELECT_BSSID_2_1_8197F)
#define BITS_UD_SELECT_BSSID_2_1_8197F                                         \
	(BIT_MASK_UD_SELECT_BSSID_2_1_8197F                                    \
	 << BIT_SHIFT_UD_SELECT_BSSID_2_1_8197F)
#define BIT_CLEAR_UD_SELECT_BSSID_2_1_8197F(x)                                 \
	((x) & (~BITS_UD_SELECT_BSSID_2_1_8197F))
#define BIT_GET_UD_SELECT_BSSID_2_1_8197F(x)                                   \
	(((x) >> BIT_SHIFT_UD_SELECT_BSSID_2_1_8197F) &                        \
	 BIT_MASK_UD_SELECT_BSSID_2_1_8197F)
#define BIT_SET_UD_SELECT_BSSID_2_1_8197F(x, v)                                \
	(BIT_CLEAR_UD_SELECT_BSSID_2_1_8197F(x) |                              \
	 BIT_UD_SELECT_BSSID_2_1_8197F(v))

#define BIT_W1S_8197F BIT(23)
#define BIT_UD_SELECT_BSSID_0_8197F BIT(22)

#define BIT_SHIFT_UD_SUB_TYPE_8197F 18
#define BIT_MASK_UD_SUB_TYPE_8197F 0xf
#define BIT_UD_SUB_TYPE_8197F(x)                                               \
	(((x) & BIT_MASK_UD_SUB_TYPE_8197F) << BIT_SHIFT_UD_SUB_TYPE_8197F)
#define BITS_UD_SUB_TYPE_8197F                                                 \
	(BIT_MASK_UD_SUB_TYPE_8197F << BIT_SHIFT_UD_SUB_TYPE_8197F)
#define BIT_CLEAR_UD_SUB_TYPE_8197F(x) ((x) & (~BITS_UD_SUB_TYPE_8197F))
#define BIT_GET_UD_SUB_TYPE_8197F(x)                                           \
	(((x) >> BIT_SHIFT_UD_SUB_TYPE_8197F) & BIT_MASK_UD_SUB_TYPE_8197F)
#define BIT_SET_UD_SUB_TYPE_8197F(x, v)                                        \
	(BIT_CLEAR_UD_SUB_TYPE_8197F(x) | BIT_UD_SUB_TYPE_8197F(v))

#define BIT_SHIFT_UD_TYPE_8197F 16
#define BIT_MASK_UD_TYPE_8197F 0x3
#define BIT_UD_TYPE_8197F(x)                                                   \
	(((x) & BIT_MASK_UD_TYPE_8197F) << BIT_SHIFT_UD_TYPE_8197F)
#define BITS_UD_TYPE_8197F (BIT_MASK_UD_TYPE_8197F << BIT_SHIFT_UD_TYPE_8197F)
#define BIT_CLEAR_UD_TYPE_8197F(x) ((x) & (~BITS_UD_TYPE_8197F))
#define BIT_GET_UD_TYPE_8197F(x)                                               \
	(((x) >> BIT_SHIFT_UD_TYPE_8197F) & BIT_MASK_UD_TYPE_8197F)
#define BIT_SET_UD_TYPE_8197F(x, v)                                            \
	(BIT_CLEAR_UD_TYPE_8197F(x) | BIT_UD_TYPE_8197F(v))

#define BIT_SHIFT_RPT_COUNTER_8197F 0
#define BIT_MASK_RPT_COUNTER_8197F 0xffff
#define BIT_RPT_COUNTER_8197F(x)                                               \
	(((x) & BIT_MASK_RPT_COUNTER_8197F) << BIT_SHIFT_RPT_COUNTER_8197F)
#define BITS_RPT_COUNTER_8197F                                                 \
	(BIT_MASK_RPT_COUNTER_8197F << BIT_SHIFT_RPT_COUNTER_8197F)
#define BIT_CLEAR_RPT_COUNTER_8197F(x) ((x) & (~BITS_RPT_COUNTER_8197F))
#define BIT_GET_RPT_COUNTER_8197F(x)                                           \
	(((x) >> BIT_SHIFT_RPT_COUNTER_8197F) & BIT_MASK_RPT_COUNTER_8197F)
#define BIT_SET_RPT_COUNTER_8197F(x, v)                                        \
	(BIT_CLEAR_RPT_COUNTER_8197F(x) | BIT_RPT_COUNTER_8197F(v))

/* 2 REG_WMAC_TRXPTCL_CTL_8197F (WMAC TX/RX PROTOCOL CONTROL REGISTER) */

#define BIT_SHIFT_ACKBA_TYPSEL_8197F (60 & CPU_OPT_WIDTH)
#define BIT_MASK_ACKBA_TYPSEL_8197F 0xf
#define BIT_ACKBA_TYPSEL_8197F(x)                                              \
	(((x) & BIT_MASK_ACKBA_TYPSEL_8197F) << BIT_SHIFT_ACKBA_TYPSEL_8197F)
#define BITS_ACKBA_TYPSEL_8197F                                                \
	(BIT_MASK_ACKBA_TYPSEL_8197F << BIT_SHIFT_ACKBA_TYPSEL_8197F)
#define BIT_CLEAR_ACKBA_TYPSEL_8197F(x) ((x) & (~BITS_ACKBA_TYPSEL_8197F))
#define BIT_GET_ACKBA_TYPSEL_8197F(x)                                          \
	(((x) >> BIT_SHIFT_ACKBA_TYPSEL_8197F) & BIT_MASK_ACKBA_TYPSEL_8197F)
#define BIT_SET_ACKBA_TYPSEL_8197F(x, v)                                       \
	(BIT_CLEAR_ACKBA_TYPSEL_8197F(x) | BIT_ACKBA_TYPSEL_8197F(v))

#define BIT_SHIFT_ACKBA_ACKPCHK_8197F (56 & CPU_OPT_WIDTH)
#define BIT_MASK_ACKBA_ACKPCHK_8197F 0xf
#define BIT_ACKBA_ACKPCHK_8197F(x)                                             \
	(((x) & BIT_MASK_ACKBA_ACKPCHK_8197F) << BIT_SHIFT_ACKBA_ACKPCHK_8197F)
#define BITS_ACKBA_ACKPCHK_8197F                                               \
	(BIT_MASK_ACKBA_ACKPCHK_8197F << BIT_SHIFT_ACKBA_ACKPCHK_8197F)
#define BIT_CLEAR_ACKBA_ACKPCHK_8197F(x) ((x) & (~BITS_ACKBA_ACKPCHK_8197F))
#define BIT_GET_ACKBA_ACKPCHK_8197F(x)                                         \
	(((x) >> BIT_SHIFT_ACKBA_ACKPCHK_8197F) & BIT_MASK_ACKBA_ACKPCHK_8197F)
#define BIT_SET_ACKBA_ACKPCHK_8197F(x, v)                                      \
	(BIT_CLEAR_ACKBA_ACKPCHK_8197F(x) | BIT_ACKBA_ACKPCHK_8197F(v))

#define BIT_SHIFT_ACKBAR_TYPESEL_8197F (48 & CPU_OPT_WIDTH)
#define BIT_MASK_ACKBAR_TYPESEL_8197F 0xff
#define BIT_ACKBAR_TYPESEL_8197F(x)                                            \
	(((x) & BIT_MASK_ACKBAR_TYPESEL_8197F)                                 \
	 << BIT_SHIFT_ACKBAR_TYPESEL_8197F)
#define BITS_ACKBAR_TYPESEL_8197F                                              \
	(BIT_MASK_ACKBAR_TYPESEL_8197F << BIT_SHIFT_ACKBAR_TYPESEL_8197F)
#define BIT_CLEAR_ACKBAR_TYPESEL_8197F(x) ((x) & (~BITS_ACKBAR_TYPESEL_8197F))
#define BIT_GET_ACKBAR_TYPESEL_8197F(x)                                        \
	(((x) >> BIT_SHIFT_ACKBAR_TYPESEL_8197F) &                             \
	 BIT_MASK_ACKBAR_TYPESEL_8197F)
#define BIT_SET_ACKBAR_TYPESEL_8197F(x, v)                                     \
	(BIT_CLEAR_ACKBAR_TYPESEL_8197F(x) | BIT_ACKBAR_TYPESEL_8197F(v))

#define BIT_SHIFT_ACKBAR_ACKPCHK_8197F (44 & CPU_OPT_WIDTH)
#define BIT_MASK_ACKBAR_ACKPCHK_8197F 0xf
#define BIT_ACKBAR_ACKPCHK_8197F(x)                                            \
	(((x) & BIT_MASK_ACKBAR_ACKPCHK_8197F)                                 \
	 << BIT_SHIFT_ACKBAR_ACKPCHK_8197F)
#define BITS_ACKBAR_ACKPCHK_8197F                                              \
	(BIT_MASK_ACKBAR_ACKPCHK_8197F << BIT_SHIFT_ACKBAR_ACKPCHK_8197F)
#define BIT_CLEAR_ACKBAR_ACKPCHK_8197F(x) ((x) & (~BITS_ACKBAR_ACKPCHK_8197F))
#define BIT_GET_ACKBAR_ACKPCHK_8197F(x)                                        \
	(((x) >> BIT_SHIFT_ACKBAR_ACKPCHK_8197F) &                             \
	 BIT_MASK_ACKBAR_ACKPCHK_8197F)
#define BIT_SET_ACKBAR_ACKPCHK_8197F(x, v)                                     \
	(BIT_CLEAR_ACKBAR_ACKPCHK_8197F(x) | BIT_ACKBAR_ACKPCHK_8197F(v))

#define BIT_RXBA_IGNOREA2_8197F BIT(42)
#define BIT_EN_SAVE_ALL_TXOPADDR_8197F BIT(41)
#define BIT_EN_TXCTS_TO_TXOPOWNER_INRXNAV_8197F BIT(40)
#define BIT_DIS_TXBA_AMPDUFCSERR_8197F BIT(39)
#define BIT_DIS_TXBA_RXBARINFULL_8197F BIT(38)
#define BIT_DIS_TXCFE_INFULL_8197F BIT(37)
#define BIT_DIS_TXCTS_INFULL_8197F BIT(36)
#define BIT_EN_TXACKBA_IN_TX_RDG_8197F BIT(35)
#define BIT_EN_TXACKBA_IN_TXOP_8197F BIT(34)
#define BIT_EN_TXCTS_IN_RXNAV_8197F BIT(33)
#define BIT_EN_TXCTS_INTXOP_8197F BIT(32)
#define BIT_BLK_EDCA_BBSLP_8197F BIT(31)
#define BIT_BLK_EDCA_BBSBY_8197F BIT(30)
#define BIT_ACKTO_BLOCK_SCH_EN_8197F BIT(27)
#define BIT_EIFS_BLOCK_SCH_EN_8197F BIT(26)
#define BIT_PLCPCHK_RST_EIFS_8197F BIT(25)
#define BIT_CCA_RST_EIFS_8197F BIT(24)
#define BIT_DIS_UPD_MYRXPKTNAV_8197F BIT(23)
#define BIT_EARLY_TXBA_8197F BIT(22)

#define BIT_SHIFT_RESP_CHNBUSY_8197F 20
#define BIT_MASK_RESP_CHNBUSY_8197F 0x3
#define BIT_RESP_CHNBUSY_8197F(x)                                              \
	(((x) & BIT_MASK_RESP_CHNBUSY_8197F) << BIT_SHIFT_RESP_CHNBUSY_8197F)
#define BITS_RESP_CHNBUSY_8197F                                                \
	(BIT_MASK_RESP_CHNBUSY_8197F << BIT_SHIFT_RESP_CHNBUSY_8197F)
#define BIT_CLEAR_RESP_CHNBUSY_8197F(x) ((x) & (~BITS_RESP_CHNBUSY_8197F))
#define BIT_GET_RESP_CHNBUSY_8197F(x)                                          \
	(((x) >> BIT_SHIFT_RESP_CHNBUSY_8197F) & BIT_MASK_RESP_CHNBUSY_8197F)
#define BIT_SET_RESP_CHNBUSY_8197F(x, v)                                       \
	(BIT_CLEAR_RESP_CHNBUSY_8197F(x) | BIT_RESP_CHNBUSY_8197F(v))

#define BIT_RESP_DCTS_EN_8197F BIT(19)
#define BIT_RESP_DCFE_EN_8197F BIT(18)
#define BIT_RESP_SPLCPEN_8197F BIT(17)
#define BIT_RESP_SGIEN_8197F BIT(16)
#define BIT_RESP_LDPC_EN_8197F BIT(15)
#define BIT_DIS_RESP_ACKINCCA_8197F BIT(14)
#define BIT_DIS_RESP_CTSINCCA_8197F BIT(13)

#define BIT_SHIFT_R_WMAC_SECOND_CCA_TIMER_8197F 10
#define BIT_MASK_R_WMAC_SECOND_CCA_TIMER_8197F 0x7
#define BIT_R_WMAC_SECOND_CCA_TIMER_8197F(x)                                   \
	(((x) & BIT_MASK_R_WMAC_SECOND_CCA_TIMER_8197F)                        \
	 << BIT_SHIFT_R_WMAC_SECOND_CCA_TIMER_8197F)
#define BITS_R_WMAC_SECOND_CCA_TIMER_8197F                                     \
	(BIT_MASK_R_WMAC_SECOND_CCA_TIMER_8197F                                \
	 << BIT_SHIFT_R_WMAC_SECOND_CCA_TIMER_8197F)
#define BIT_CLEAR_R_WMAC_SECOND_CCA_TIMER_8197F(x)                             \
	((x) & (~BITS_R_WMAC_SECOND_CCA_TIMER_8197F))
#define BIT_GET_R_WMAC_SECOND_CCA_TIMER_8197F(x)                               \
	(((x) >> BIT_SHIFT_R_WMAC_SECOND_CCA_TIMER_8197F) &                    \
	 BIT_MASK_R_WMAC_SECOND_CCA_TIMER_8197F)
#define BIT_SET_R_WMAC_SECOND_CCA_TIMER_8197F(x, v)                            \
	(BIT_CLEAR_R_WMAC_SECOND_CCA_TIMER_8197F(x) |                          \
	 BIT_R_WMAC_SECOND_CCA_TIMER_8197F(v))

#define BIT_SHIFT_RFMOD_8197F 7
#define BIT_MASK_RFMOD_8197F 0x3
#define BIT_RFMOD_8197F(x)                                                     \
	(((x) & BIT_MASK_RFMOD_8197F) << BIT_SHIFT_RFMOD_8197F)
#define BITS_RFMOD_8197F (BIT_MASK_RFMOD_8197F << BIT_SHIFT_RFMOD_8197F)
#define BIT_CLEAR_RFMOD_8197F(x) ((x) & (~BITS_RFMOD_8197F))
#define BIT_GET_RFMOD_8197F(x)                                                 \
	(((x) >> BIT_SHIFT_RFMOD_8197F) & BIT_MASK_RFMOD_8197F)
#define BIT_SET_RFMOD_8197F(x, v)                                              \
	(BIT_CLEAR_RFMOD_8197F(x) | BIT_RFMOD_8197F(v))

#define BIT_SHIFT_RESP_CTS_DYNBW_SEL_8197F 5
#define BIT_MASK_RESP_CTS_DYNBW_SEL_8197F 0x3
#define BIT_RESP_CTS_DYNBW_SEL_8197F(x)                                        \
	(((x) & BIT_MASK_RESP_CTS_DYNBW_SEL_8197F)                             \
	 << BIT_SHIFT_RESP_CTS_DYNBW_SEL_8197F)
#define BITS_RESP_CTS_DYNBW_SEL_8197F                                          \
	(BIT_MASK_RESP_CTS_DYNBW_SEL_8197F                                     \
	 << BIT_SHIFT_RESP_CTS_DYNBW_SEL_8197F)
#define BIT_CLEAR_RESP_CTS_DYNBW_SEL_8197F(x)                                  \
	((x) & (~BITS_RESP_CTS_DYNBW_SEL_8197F))
#define BIT_GET_RESP_CTS_DYNBW_SEL_8197F(x)                                    \
	(((x) >> BIT_SHIFT_RESP_CTS_DYNBW_SEL_8197F) &                         \
	 BIT_MASK_RESP_CTS_DYNBW_SEL_8197F)
#define BIT_SET_RESP_CTS_DYNBW_SEL_8197F(x, v)                                 \
	(BIT_CLEAR_RESP_CTS_DYNBW_SEL_8197F(x) |                               \
	 BIT_RESP_CTS_DYNBW_SEL_8197F(v))

#define BIT_DLY_TX_WAIT_RXANTSEL_8197F BIT(4)
#define BIT_TXRESP_BY_RXANTSEL_8197F BIT(3)

#define BIT_SHIFT_ORIG_DCTS_CHK_8197F 0
#define BIT_MASK_ORIG_DCTS_CHK_8197F 0x3
#define BIT_ORIG_DCTS_CHK_8197F(x)                                             \
	(((x) & BIT_MASK_ORIG_DCTS_CHK_8197F) << BIT_SHIFT_ORIG_DCTS_CHK_8197F)
#define BITS_ORIG_DCTS_CHK_8197F                                               \
	(BIT_MASK_ORIG_DCTS_CHK_8197F << BIT_SHIFT_ORIG_DCTS_CHK_8197F)
#define BIT_CLEAR_ORIG_DCTS_CHK_8197F(x) ((x) & (~BITS_ORIG_DCTS_CHK_8197F))
#define BIT_GET_ORIG_DCTS_CHK_8197F(x)                                         \
	(((x) >> BIT_SHIFT_ORIG_DCTS_CHK_8197F) & BIT_MASK_ORIG_DCTS_CHK_8197F)
#define BIT_SET_ORIG_DCTS_CHK_8197F(x, v)                                      \
	(BIT_CLEAR_ORIG_DCTS_CHK_8197F(x) | BIT_ORIG_DCTS_CHK_8197F(v))

/* 2 REG_CAMCMD_8197F (CAM COMMAND REGISTER) */
#define BIT_SECCAM_POLLING_8197F BIT(31)
#define BIT_SECCAM_CLR_8197F BIT(30)
#define BIT_MFBCAM_CLR_8197F BIT(29)
#define BIT_SECCAM_WE_8197F BIT(16)

#define BIT_SHIFT_SECCAM_ADDR_V2_8197F 0
#define BIT_MASK_SECCAM_ADDR_V2_8197F 0x3ff
#define BIT_SECCAM_ADDR_V2_8197F(x)                                            \
	(((x) & BIT_MASK_SECCAM_ADDR_V2_8197F)                                 \
	 << BIT_SHIFT_SECCAM_ADDR_V2_8197F)
#define BITS_SECCAM_ADDR_V2_8197F                                              \
	(BIT_MASK_SECCAM_ADDR_V2_8197F << BIT_SHIFT_SECCAM_ADDR_V2_8197F)
#define BIT_CLEAR_SECCAM_ADDR_V2_8197F(x) ((x) & (~BITS_SECCAM_ADDR_V2_8197F))
#define BIT_GET_SECCAM_ADDR_V2_8197F(x)                                        \
	(((x) >> BIT_SHIFT_SECCAM_ADDR_V2_8197F) &                             \
	 BIT_MASK_SECCAM_ADDR_V2_8197F)
#define BIT_SET_SECCAM_ADDR_V2_8197F(x, v)                                     \
	(BIT_CLEAR_SECCAM_ADDR_V2_8197F(x) | BIT_SECCAM_ADDR_V2_8197F(v))

/* 2 REG_CAMWRITE_8197F (CAM WRITE REGISTER) */

#define BIT_SHIFT_CAMW_DATA_8197F 0
#define BIT_MASK_CAMW_DATA_8197F 0xffffffffL
#define BIT_CAMW_DATA_8197F(x)                                                 \
	(((x) & BIT_MASK_CAMW_DATA_8197F) << BIT_SHIFT_CAMW_DATA_8197F)
#define BITS_CAMW_DATA_8197F                                                   \
	(BIT_MASK_CAMW_DATA_8197F << BIT_SHIFT_CAMW_DATA_8197F)
#define BIT_CLEAR_CAMW_DATA_8197F(x) ((x) & (~BITS_CAMW_DATA_8197F))
#define BIT_GET_CAMW_DATA_8197F(x)                                             \
	(((x) >> BIT_SHIFT_CAMW_DATA_8197F) & BIT_MASK_CAMW_DATA_8197F)
#define BIT_SET_CAMW_DATA_8197F(x, v)                                          \
	(BIT_CLEAR_CAMW_DATA_8197F(x) | BIT_CAMW_DATA_8197F(v))

/* 2 REG_CAMREAD_8197F (CAM READ REGISTER) */

#define BIT_SHIFT_CAMR_DATA_8197F 0
#define BIT_MASK_CAMR_DATA_8197F 0xffffffffL
#define BIT_CAMR_DATA_8197F(x)                                                 \
	(((x) & BIT_MASK_CAMR_DATA_8197F) << BIT_SHIFT_CAMR_DATA_8197F)
#define BITS_CAMR_DATA_8197F                                                   \
	(BIT_MASK_CAMR_DATA_8197F << BIT_SHIFT_CAMR_DATA_8197F)
#define BIT_CLEAR_CAMR_DATA_8197F(x) ((x) & (~BITS_CAMR_DATA_8197F))
#define BIT_GET_CAMR_DATA_8197F(x)                                             \
	(((x) >> BIT_SHIFT_CAMR_DATA_8197F) & BIT_MASK_CAMR_DATA_8197F)
#define BIT_SET_CAMR_DATA_8197F(x, v)                                          \
	(BIT_CLEAR_CAMR_DATA_8197F(x) | BIT_CAMR_DATA_8197F(v))

/* 2 REG_CAMDBG_8197F (CAM DEBUG REGISTER) */
#define BIT_SECCAM_INFO_8197F BIT(31)
#define BIT_SEC_KEYFOUND_8197F BIT(15)

#define BIT_SHIFT_CAMDBG_SEC_TYPE_8197F 12
#define BIT_MASK_CAMDBG_SEC_TYPE_8197F 0x7
#define BIT_CAMDBG_SEC_TYPE_8197F(x)                                           \
	(((x) & BIT_MASK_CAMDBG_SEC_TYPE_8197F)                                \
	 << BIT_SHIFT_CAMDBG_SEC_TYPE_8197F)
#define BITS_CAMDBG_SEC_TYPE_8197F                                             \
	(BIT_MASK_CAMDBG_SEC_TYPE_8197F << BIT_SHIFT_CAMDBG_SEC_TYPE_8197F)
#define BIT_CLEAR_CAMDBG_SEC_TYPE_8197F(x) ((x) & (~BITS_CAMDBG_SEC_TYPE_8197F))
#define BIT_GET_CAMDBG_SEC_TYPE_8197F(x)                                       \
	(((x) >> BIT_SHIFT_CAMDBG_SEC_TYPE_8197F) &                            \
	 BIT_MASK_CAMDBG_SEC_TYPE_8197F)
#define BIT_SET_CAMDBG_SEC_TYPE_8197F(x, v)                                    \
	(BIT_CLEAR_CAMDBG_SEC_TYPE_8197F(x) | BIT_CAMDBG_SEC_TYPE_8197F(v))

#define BIT_CAMDBG_EXT_SEC_TYPE_8197F BIT(11)

#define BIT_SHIFT_CAMDBG_MIC_KEY_IDX_8197F 5
#define BIT_MASK_CAMDBG_MIC_KEY_IDX_8197F 0x1f
#define BIT_CAMDBG_MIC_KEY_IDX_8197F(x)                                        \
	(((x) & BIT_MASK_CAMDBG_MIC_KEY_IDX_8197F)                             \
	 << BIT_SHIFT_CAMDBG_MIC_KEY_IDX_8197F)
#define BITS_CAMDBG_MIC_KEY_IDX_8197F                                          \
	(BIT_MASK_CAMDBG_MIC_KEY_IDX_8197F                                     \
	 << BIT_SHIFT_CAMDBG_MIC_KEY_IDX_8197F)
#define BIT_CLEAR_CAMDBG_MIC_KEY_IDX_8197F(x)                                  \
	((x) & (~BITS_CAMDBG_MIC_KEY_IDX_8197F))
#define BIT_GET_CAMDBG_MIC_KEY_IDX_8197F(x)                                    \
	(((x) >> BIT_SHIFT_CAMDBG_MIC_KEY_IDX_8197F) &                         \
	 BIT_MASK_CAMDBG_MIC_KEY_IDX_8197F)
#define BIT_SET_CAMDBG_MIC_KEY_IDX_8197F(x, v)                                 \
	(BIT_CLEAR_CAMDBG_MIC_KEY_IDX_8197F(x) |                               \
	 BIT_CAMDBG_MIC_KEY_IDX_8197F(v))

#define BIT_SHIFT_CAMDBG_SEC_KEY_IDX_8197F 0
#define BIT_MASK_CAMDBG_SEC_KEY_IDX_8197F 0x1f
#define BIT_CAMDBG_SEC_KEY_IDX_8197F(x)                                        \
	(((x) & BIT_MASK_CAMDBG_SEC_KEY_IDX_8197F)                             \
	 << BIT_SHIFT_CAMDBG_SEC_KEY_IDX_8197F)
#define BITS_CAMDBG_SEC_KEY_IDX_8197F                                          \
	(BIT_MASK_CAMDBG_SEC_KEY_IDX_8197F                                     \
	 << BIT_SHIFT_CAMDBG_SEC_KEY_IDX_8197F)
#define BIT_CLEAR_CAMDBG_SEC_KEY_IDX_8197F(x)                                  \
	((x) & (~BITS_CAMDBG_SEC_KEY_IDX_8197F))
#define BIT_GET_CAMDBG_SEC_KEY_IDX_8197F(x)                                    \
	(((x) >> BIT_SHIFT_CAMDBG_SEC_KEY_IDX_8197F) &                         \
	 BIT_MASK_CAMDBG_SEC_KEY_IDX_8197F)
#define BIT_SET_CAMDBG_SEC_KEY_IDX_8197F(x, v)                                 \
	(BIT_CLEAR_CAMDBG_SEC_KEY_IDX_8197F(x) |                               \
	 BIT_CAMDBG_SEC_KEY_IDX_8197F(v))

/* 2 REG_RXFILTER_ACTION_1_8197F */

#define BIT_SHIFT_RXFILTER_ACTION_1_8197F 0
#define BIT_MASK_RXFILTER_ACTION_1_8197F 0xff
#define BIT_RXFILTER_ACTION_1_8197F(x)                                         \
	(((x) & BIT_MASK_RXFILTER_ACTION_1_8197F)                              \
	 << BIT_SHIFT_RXFILTER_ACTION_1_8197F)
#define BITS_RXFILTER_ACTION_1_8197F                                           \
	(BIT_MASK_RXFILTER_ACTION_1_8197F << BIT_SHIFT_RXFILTER_ACTION_1_8197F)
#define BIT_CLEAR_RXFILTER_ACTION_1_8197F(x)                                   \
	((x) & (~BITS_RXFILTER_ACTION_1_8197F))
#define BIT_GET_RXFILTER_ACTION_1_8197F(x)                                     \
	(((x) >> BIT_SHIFT_RXFILTER_ACTION_1_8197F) &                          \
	 BIT_MASK_RXFILTER_ACTION_1_8197F)
#define BIT_SET_RXFILTER_ACTION_1_8197F(x, v)                                  \
	(BIT_CLEAR_RXFILTER_ACTION_1_8197F(x) | BIT_RXFILTER_ACTION_1_8197F(v))

/* 2 REG_RXFILTER_CATEGORY_1_8197F */

#define BIT_SHIFT_RXFILTER_CATEGORY_1_8197F 0
#define BIT_MASK_RXFILTER_CATEGORY_1_8197F 0xff
#define BIT_RXFILTER_CATEGORY_1_8197F(x)                                       \
	(((x) & BIT_MASK_RXFILTER_CATEGORY_1_8197F)                            \
	 << BIT_SHIFT_RXFILTER_CATEGORY_1_8197F)
#define BITS_RXFILTER_CATEGORY_1_8197F                                         \
	(BIT_MASK_RXFILTER_CATEGORY_1_8197F                                    \
	 << BIT_SHIFT_RXFILTER_CATEGORY_1_8197F)
#define BIT_CLEAR_RXFILTER_CATEGORY_1_8197F(x)                                 \
	((x) & (~BITS_RXFILTER_CATEGORY_1_8197F))
#define BIT_GET_RXFILTER_CATEGORY_1_8197F(x)                                   \
	(((x) >> BIT_SHIFT_RXFILTER_CATEGORY_1_8197F) &                        \
	 BIT_MASK_RXFILTER_CATEGORY_1_8197F)
#define BIT_SET_RXFILTER_CATEGORY_1_8197F(x, v)                                \
	(BIT_CLEAR_RXFILTER_CATEGORY_1_8197F(x) |                              \
	 BIT_RXFILTER_CATEGORY_1_8197F(v))

/* 2 REG_SECCFG_8197F (SECURITY CONFIGURATION REGISTER) */
#define BIT_DIS_GCLK_WAPI_8197F BIT(15)
#define BIT_DIS_GCLK_AES_8197F BIT(14)
#define BIT_DIS_GCLK_TKIP_8197F BIT(13)
#define BIT_AES_SEL_QC_1_8197F BIT(12)
#define BIT_AES_SEL_QC_0_8197F BIT(11)
#define BIT_WMAC_CKECK_BMC_8197F BIT(9)
#define BIT_CHK_KEYID_8197F BIT(8)
#define BIT_RXBCUSEDK_8197F BIT(7)
#define BIT_TXBCUSEDK_8197F BIT(6)
#define BIT_NOSKMC_8197F BIT(5)
#define BIT_SKBYA2_8197F BIT(4)
#define BIT_RXDEC_8197F BIT(3)
#define BIT_TXENC_8197F BIT(2)
#define BIT_RXUHUSEDK_8197F BIT(1)
#define BIT_TXUHUSEDK_8197F BIT(0)

/* 2 REG_RXFILTER_ACTION_3_8197F */

#define BIT_SHIFT_RXFILTER_ACTION_3_8197F 0
#define BIT_MASK_RXFILTER_ACTION_3_8197F 0xff
#define BIT_RXFILTER_ACTION_3_8197F(x)                                         \
	(((x) & BIT_MASK_RXFILTER_ACTION_3_8197F)                              \
	 << BIT_SHIFT_RXFILTER_ACTION_3_8197F)
#define BITS_RXFILTER_ACTION_3_8197F                                           \
	(BIT_MASK_RXFILTER_ACTION_3_8197F << BIT_SHIFT_RXFILTER_ACTION_3_8197F)
#define BIT_CLEAR_RXFILTER_ACTION_3_8197F(x)                                   \
	((x) & (~BITS_RXFILTER_ACTION_3_8197F))
#define BIT_GET_RXFILTER_ACTION_3_8197F(x)                                     \
	(((x) >> BIT_SHIFT_RXFILTER_ACTION_3_8197F) &                          \
	 BIT_MASK_RXFILTER_ACTION_3_8197F)
#define BIT_SET_RXFILTER_ACTION_3_8197F(x, v)                                  \
	(BIT_CLEAR_RXFILTER_ACTION_3_8197F(x) | BIT_RXFILTER_ACTION_3_8197F(v))

/* 2 REG_RXFILTER_CATEGORY_3_8197F */

#define BIT_SHIFT_RXFILTER_CATEGORY_3_8197F 0
#define BIT_MASK_RXFILTER_CATEGORY_3_8197F 0xff
#define BIT_RXFILTER_CATEGORY_3_8197F(x)                                       \
	(((x) & BIT_MASK_RXFILTER_CATEGORY_3_8197F)                            \
	 << BIT_SHIFT_RXFILTER_CATEGORY_3_8197F)
#define BITS_RXFILTER_CATEGORY_3_8197F                                         \
	(BIT_MASK_RXFILTER_CATEGORY_3_8197F                                    \
	 << BIT_SHIFT_RXFILTER_CATEGORY_3_8197F)
#define BIT_CLEAR_RXFILTER_CATEGORY_3_8197F(x)                                 \
	((x) & (~BITS_RXFILTER_CATEGORY_3_8197F))
#define BIT_GET_RXFILTER_CATEGORY_3_8197F(x)                                   \
	(((x) >> BIT_SHIFT_RXFILTER_CATEGORY_3_8197F) &                        \
	 BIT_MASK_RXFILTER_CATEGORY_3_8197F)
#define BIT_SET_RXFILTER_CATEGORY_3_8197F(x, v)                                \
	(BIT_CLEAR_RXFILTER_CATEGORY_3_8197F(x) |                              \
	 BIT_RXFILTER_CATEGORY_3_8197F(v))

/* 2 REG_RXFILTER_ACTION_2_8197F */

#define BIT_SHIFT_RXFILTER_ACTION_2_8197F 0
#define BIT_MASK_RXFILTER_ACTION_2_8197F 0xff
#define BIT_RXFILTER_ACTION_2_8197F(x)                                         \
	(((x) & BIT_MASK_RXFILTER_ACTION_2_8197F)                              \
	 << BIT_SHIFT_RXFILTER_ACTION_2_8197F)
#define BITS_RXFILTER_ACTION_2_8197F                                           \
	(BIT_MASK_RXFILTER_ACTION_2_8197F << BIT_SHIFT_RXFILTER_ACTION_2_8197F)
#define BIT_CLEAR_RXFILTER_ACTION_2_8197F(x)                                   \
	((x) & (~BITS_RXFILTER_ACTION_2_8197F))
#define BIT_GET_RXFILTER_ACTION_2_8197F(x)                                     \
	(((x) >> BIT_SHIFT_RXFILTER_ACTION_2_8197F) &                          \
	 BIT_MASK_RXFILTER_ACTION_2_8197F)
#define BIT_SET_RXFILTER_ACTION_2_8197F(x, v)                                  \
	(BIT_CLEAR_RXFILTER_ACTION_2_8197F(x) | BIT_RXFILTER_ACTION_2_8197F(v))

/* 2 REG_RXFILTER_CATEGORY_2_8197F */

#define BIT_SHIFT_RXFILTER_CATEGORY_2_8197F 0
#define BIT_MASK_RXFILTER_CATEGORY_2_8197F 0xff
#define BIT_RXFILTER_CATEGORY_2_8197F(x)                                       \
	(((x) & BIT_MASK_RXFILTER_CATEGORY_2_8197F)                            \
	 << BIT_SHIFT_RXFILTER_CATEGORY_2_8197F)
#define BITS_RXFILTER_CATEGORY_2_8197F                                         \
	(BIT_MASK_RXFILTER_CATEGORY_2_8197F                                    \
	 << BIT_SHIFT_RXFILTER_CATEGORY_2_8197F)
#define BIT_CLEAR_RXFILTER_CATEGORY_2_8197F(x)                                 \
	((x) & (~BITS_RXFILTER_CATEGORY_2_8197F))
#define BIT_GET_RXFILTER_CATEGORY_2_8197F(x)                                   \
	(((x) >> BIT_SHIFT_RXFILTER_CATEGORY_2_8197F) &                        \
	 BIT_MASK_RXFILTER_CATEGORY_2_8197F)
#define BIT_SET_RXFILTER_CATEGORY_2_8197F(x, v)                                \
	(BIT_CLEAR_RXFILTER_CATEGORY_2_8197F(x) |                              \
	 BIT_RXFILTER_CATEGORY_2_8197F(v))

/* 2 REG_RXFLTMAP4_8197F (RX FILTER MAP GROUP 4) */
#define BIT_CTRLFLT15EN_FW_8197F BIT(15)
#define BIT_CTRLFLT14EN_FW_8197F BIT(14)
#define BIT_CTRLFLT13EN_FW_8197F BIT(13)
#define BIT_CTRLFLT12EN_FW_8197F BIT(12)
#define BIT_CTRLFLT11EN_FW_8197F BIT(11)
#define BIT_CTRLFLT10EN_FW_8197F BIT(10)
#define BIT_CTRLFLT9EN_FW_8197F BIT(9)
#define BIT_CTRLFLT8EN_FW_8197F BIT(8)
#define BIT_CTRLFLT7EN_FW_8197F BIT(7)
#define BIT_CTRLFLT6EN_FW_8197F BIT(6)
#define BIT_CTRLFLT5EN_FW_8197F BIT(5)
#define BIT_CTRLFLT4EN_FW_8197F BIT(4)
#define BIT_CTRLFLT3EN_FW_8197F BIT(3)
#define BIT_CTRLFLT2EN_FW_8197F BIT(2)
#define BIT_CTRLFLT1EN_FW_8197F BIT(1)
#define BIT_CTRLFLT0EN_FW_8197F BIT(0)

/* 2 REG_RXFLTMAP3_8197F (RX FILTER MAP GROUP 3) */
#define BIT_MGTFLT15EN_FW_8197F BIT(15)
#define BIT_MGTFLT14EN_FW_8197F BIT(14)
#define BIT_MGTFLT13EN_FW_8197F BIT(13)
#define BIT_MGTFLT12EN_FW_8197F BIT(12)
#define BIT_MGTFLT11EN_FW_8197F BIT(11)
#define BIT_MGTFLT10EN_FW_8197F BIT(10)
#define BIT_MGTFLT9EN_FW_8197F BIT(9)
#define BIT_MGTFLT8EN_FW_8197F BIT(8)
#define BIT_MGTFLT7EN_FW_8197F BIT(7)
#define BIT_MGTFLT6EN_FW_8197F BIT(6)
#define BIT_MGTFLT5EN_FW_8197F BIT(5)
#define BIT_MGTFLT4EN_FW_8197F BIT(4)
#define BIT_MGTFLT3EN_FW_8197F BIT(3)
#define BIT_MGTFLT2EN_FW_8197F BIT(2)
#define BIT_MGTFLT1EN_FW_8197F BIT(1)
#define BIT_MGTFLT0EN_FW_8197F BIT(0)

/* 2 REG_RXFLTMAP6_8197F (RX FILTER MAP GROUP 3) */
#define BIT_ACTIONFLT15EN_FW_8197F BIT(15)
#define BIT_ACTIONFLT14EN_FW_8197F BIT(14)
#define BIT_ACTIONFLT13EN_FW_8197F BIT(13)
#define BIT_ACTIONFLT12EN_FW_8197F BIT(12)
#define BIT_ACTIONFLT11EN_FW_8197F BIT(11)
#define BIT_ACTIONFLT10EN_FW_8197F BIT(10)
#define BIT_ACTIONFLT9EN_FW_8197F BIT(9)
#define BIT_ACTIONFLT8EN_FW_8197F BIT(8)
#define BIT_ACTIONFLT7EN_FW_8197F BIT(7)
#define BIT_ACTIONFLT6EN_FW_8197F BIT(6)
#define BIT_ACTIONFLT5EN_FW_8197F BIT(5)
#define BIT_ACTIONFLT4EN_FW_8197F BIT(4)
#define BIT_ACTIONFLT3EN_FW_8197F BIT(3)
#define BIT_ACTIONFLT2EN_FW_8197F BIT(2)
#define BIT_ACTIONFLT1EN_FW_8197F BIT(1)
#define BIT_ACTIONFLT0EN_FW_8197F BIT(0)

/* 2 REG_RXFLTMAP5_8197F (RX FILTER MAP GROUP 3) */
#define BIT_DATAFLT15EN_FW_8197F BIT(15)
#define BIT_DATAFLT14EN_FW_8197F BIT(14)
#define BIT_DATAFLT13EN_FW_8197F BIT(13)
#define BIT_DATAFLT12EN_FW_8197F BIT(12)
#define BIT_DATAFLT11EN_FW_8197F BIT(11)
#define BIT_DATAFLT10EN_FW_8197F BIT(10)
#define BIT_DATAFLT9EN_FW_8197F BIT(9)
#define BIT_DATAFLT8EN_FW_8197F BIT(8)
#define BIT_DATAFLT7EN_FW_8197F BIT(7)
#define BIT_DATAFLT6EN_FW_8197F BIT(6)
#define BIT_DATAFLT5EN_FW_8197F BIT(5)
#define BIT_DATAFLT4EN_FW_8197F BIT(4)
#define BIT_DATAFLT3EN_FW_8197F BIT(3)
#define BIT_DATAFLT2EN_FW_8197F BIT(2)
#define BIT_DATAFLT1EN_FW_8197F BIT(1)
#define BIT_DATAFLT0EN_FW_8197F BIT(0)

/* 2 REG_WMMPS_UAPSD_TID_8197F (WMM POWER SAVE UAPSD TID REGISTER) */
#define BIT_WMMPS_UAPSD_TID7_8197F BIT(7)
#define BIT_WMMPS_UAPSD_TID6_8197F BIT(6)
#define BIT_WMMPS_UAPSD_TID5_8197F BIT(5)
#define BIT_WMMPS_UAPSD_TID4_8197F BIT(4)
#define BIT_WMMPS_UAPSD_TID3_8197F BIT(3)
#define BIT_WMMPS_UAPSD_TID2_8197F BIT(2)
#define BIT_WMMPS_UAPSD_TID1_8197F BIT(1)
#define BIT_WMMPS_UAPSD_TID0_8197F BIT(0)

/* 2 REG_PS_RX_INFO_8197F (POWER SAVE RX INFORMATION REGISTER) */

#define BIT_SHIFT_PORTSEL__PS_RX_INFO_8197F 5
#define BIT_MASK_PORTSEL__PS_RX_INFO_8197F 0x7
#define BIT_PORTSEL__PS_RX_INFO_8197F(x)                                       \
	(((x) & BIT_MASK_PORTSEL__PS_RX_INFO_8197F)                            \
	 << BIT_SHIFT_PORTSEL__PS_RX_INFO_8197F)
#define BITS_PORTSEL__PS_RX_INFO_8197F                                         \
	(BIT_MASK_PORTSEL__PS_RX_INFO_8197F                                    \
	 << BIT_SHIFT_PORTSEL__PS_RX_INFO_8197F)
#define BIT_CLEAR_PORTSEL__PS_RX_INFO_8197F(x)                                 \
	((x) & (~BITS_PORTSEL__PS_RX_INFO_8197F))
#define BIT_GET_PORTSEL__PS_RX_INFO_8197F(x)                                   \
	(((x) >> BIT_SHIFT_PORTSEL__PS_RX_INFO_8197F) &                        \
	 BIT_MASK_PORTSEL__PS_RX_INFO_8197F)
#define BIT_SET_PORTSEL__PS_RX_INFO_8197F(x, v)                                \
	(BIT_CLEAR_PORTSEL__PS_RX_INFO_8197F(x) |                              \
	 BIT_PORTSEL__PS_RX_INFO_8197F(v))

#define BIT_RXCTRLIN0_8197F BIT(4)
#define BIT_RXMGTIN0_8197F BIT(3)
#define BIT_RXDATAIN2_8197F BIT(2)
#define BIT_RXDATAIN1_8197F BIT(1)
#define BIT_RXDATAIN0_8197F BIT(0)

/* 2 REG_NOT_VALID_8197F */
#define BIT_CHK_TSF_TA_8197F BIT(2)
#define BIT_CHK_TSF_CBSSID_8197F BIT(1)
#define BIT_CHK_TSF_EN_8197F BIT(0)

/* 2 REG_WOW_CTRL_8197F (WAKE ON WLAN CONTROL REGISTER) */

#define BIT_SHIFT_PSF_BSSIDSEL_B2B1_8197F 6
#define BIT_MASK_PSF_BSSIDSEL_B2B1_8197F 0x3
#define BIT_PSF_BSSIDSEL_B2B1_8197F(x)                                         \
	(((x) & BIT_MASK_PSF_BSSIDSEL_B2B1_8197F)                              \
	 << BIT_SHIFT_PSF_BSSIDSEL_B2B1_8197F)
#define BITS_PSF_BSSIDSEL_B2B1_8197F                                           \
	(BIT_MASK_PSF_BSSIDSEL_B2B1_8197F << BIT_SHIFT_PSF_BSSIDSEL_B2B1_8197F)
#define BIT_CLEAR_PSF_BSSIDSEL_B2B1_8197F(x)                                   \
	((x) & (~BITS_PSF_BSSIDSEL_B2B1_8197F))
#define BIT_GET_PSF_BSSIDSEL_B2B1_8197F(x)                                     \
	(((x) >> BIT_SHIFT_PSF_BSSIDSEL_B2B1_8197F) &                          \
	 BIT_MASK_PSF_BSSIDSEL_B2B1_8197F)
#define BIT_SET_PSF_BSSIDSEL_B2B1_8197F(x, v)                                  \
	(BIT_CLEAR_PSF_BSSIDSEL_B2B1_8197F(x) | BIT_PSF_BSSIDSEL_B2B1_8197F(v))

#define BIT_WOWHCI_8197F BIT(5)
#define BIT_PSF_BSSIDSEL_B0_8197F BIT(4)
#define BIT_UWF_8197F BIT(3)
#define BIT_MAGIC_8197F BIT(2)
#define BIT_WOWEN_8197F BIT(1)
#define BIT_FORCE_WAKEUP_8197F BIT(0)

/* 2 REG_LPNAV_CTRL_8197F (LOW POWER NAV CONTROL REGISTER) */
#define BIT_LPNAV_EN_8197F BIT(31)

#define BIT_SHIFT_LPNAV_EARLY_8197F 16
#define BIT_MASK_LPNAV_EARLY_8197F 0x7fff
#define BIT_LPNAV_EARLY_8197F(x)                                               \
	(((x) & BIT_MASK_LPNAV_EARLY_8197F) << BIT_SHIFT_LPNAV_EARLY_8197F)
#define BITS_LPNAV_EARLY_8197F                                                 \
	(BIT_MASK_LPNAV_EARLY_8197F << BIT_SHIFT_LPNAV_EARLY_8197F)
#define BIT_CLEAR_LPNAV_EARLY_8197F(x) ((x) & (~BITS_LPNAV_EARLY_8197F))
#define BIT_GET_LPNAV_EARLY_8197F(x)                                           \
	(((x) >> BIT_SHIFT_LPNAV_EARLY_8197F) & BIT_MASK_LPNAV_EARLY_8197F)
#define BIT_SET_LPNAV_EARLY_8197F(x, v)                                        \
	(BIT_CLEAR_LPNAV_EARLY_8197F(x) | BIT_LPNAV_EARLY_8197F(v))

#define BIT_SHIFT_LPNAV_TH_8197F 0
#define BIT_MASK_LPNAV_TH_8197F 0xffff
#define BIT_LPNAV_TH_8197F(x)                                                  \
	(((x) & BIT_MASK_LPNAV_TH_8197F) << BIT_SHIFT_LPNAV_TH_8197F)
#define BITS_LPNAV_TH_8197F                                                    \
	(BIT_MASK_LPNAV_TH_8197F << BIT_SHIFT_LPNAV_TH_8197F)
#define BIT_CLEAR_LPNAV_TH_8197F(x) ((x) & (~BITS_LPNAV_TH_8197F))
#define BIT_GET_LPNAV_TH_8197F(x)                                              \
	(((x) >> BIT_SHIFT_LPNAV_TH_8197F) & BIT_MASK_LPNAV_TH_8197F)
#define BIT_SET_LPNAV_TH_8197F(x, v)                                           \
	(BIT_CLEAR_LPNAV_TH_8197F(x) | BIT_LPNAV_TH_8197F(v))

/* 2 REG_WKFMCAM_CMD_8197F (WAKEUP FRAME CAM COMMAND REGISTER) */
#define BIT_WKFCAM_POLLING_V1_8197F BIT(31)
#define BIT_WKFCAM_CLR_V1_8197F BIT(30)
#define BIT_WKFCAM_WE_8197F BIT(16)

#define BIT_SHIFT_WKFCAM_ADDR_V2_8197F 8
#define BIT_MASK_WKFCAM_ADDR_V2_8197F 0xff
#define BIT_WKFCAM_ADDR_V2_8197F(x)                                            \
	(((x) & BIT_MASK_WKFCAM_ADDR_V2_8197F)                                 \
	 << BIT_SHIFT_WKFCAM_ADDR_V2_8197F)
#define BITS_WKFCAM_ADDR_V2_8197F                                              \
	(BIT_MASK_WKFCAM_ADDR_V2_8197F << BIT_SHIFT_WKFCAM_ADDR_V2_8197F)
#define BIT_CLEAR_WKFCAM_ADDR_V2_8197F(x) ((x) & (~BITS_WKFCAM_ADDR_V2_8197F))
#define BIT_GET_WKFCAM_ADDR_V2_8197F(x)                                        \
	(((x) >> BIT_SHIFT_WKFCAM_ADDR_V2_8197F) &                             \
	 BIT_MASK_WKFCAM_ADDR_V2_8197F)
#define BIT_SET_WKFCAM_ADDR_V2_8197F(x, v)                                     \
	(BIT_CLEAR_WKFCAM_ADDR_V2_8197F(x) | BIT_WKFCAM_ADDR_V2_8197F(v))

#define BIT_SHIFT_WKFCAM_CAM_NUM_V1_8197F 0
#define BIT_MASK_WKFCAM_CAM_NUM_V1_8197F 0xff
#define BIT_WKFCAM_CAM_NUM_V1_8197F(x)                                         \
	(((x) & BIT_MASK_WKFCAM_CAM_NUM_V1_8197F)                              \
	 << BIT_SHIFT_WKFCAM_CAM_NUM_V1_8197F)
#define BITS_WKFCAM_CAM_NUM_V1_8197F                                           \
	(BIT_MASK_WKFCAM_CAM_NUM_V1_8197F << BIT_SHIFT_WKFCAM_CAM_NUM_V1_8197F)
#define BIT_CLEAR_WKFCAM_CAM_NUM_V1_8197F(x)                                   \
	((x) & (~BITS_WKFCAM_CAM_NUM_V1_8197F))
#define BIT_GET_WKFCAM_CAM_NUM_V1_8197F(x)                                     \
	(((x) >> BIT_SHIFT_WKFCAM_CAM_NUM_V1_8197F) &                          \
	 BIT_MASK_WKFCAM_CAM_NUM_V1_8197F)
#define BIT_SET_WKFCAM_CAM_NUM_V1_8197F(x, v)                                  \
	(BIT_CLEAR_WKFCAM_CAM_NUM_V1_8197F(x) | BIT_WKFCAM_CAM_NUM_V1_8197F(v))

/* 2 REG_WKFMCAM_RWD_8197F (WAKEUP FRAME READ/WRITE DATA) */

#define BIT_SHIFT_WKFMCAM_RWD_8197F 0
#define BIT_MASK_WKFMCAM_RWD_8197F 0xffffffffL
#define BIT_WKFMCAM_RWD_8197F(x)                                               \
	(((x) & BIT_MASK_WKFMCAM_RWD_8197F) << BIT_SHIFT_WKFMCAM_RWD_8197F)
#define BITS_WKFMCAM_RWD_8197F                                                 \
	(BIT_MASK_WKFMCAM_RWD_8197F << BIT_SHIFT_WKFMCAM_RWD_8197F)
#define BIT_CLEAR_WKFMCAM_RWD_8197F(x) ((x) & (~BITS_WKFMCAM_RWD_8197F))
#define BIT_GET_WKFMCAM_RWD_8197F(x)                                           \
	(((x) >> BIT_SHIFT_WKFMCAM_RWD_8197F) & BIT_MASK_WKFMCAM_RWD_8197F)
#define BIT_SET_WKFMCAM_RWD_8197F(x, v)                                        \
	(BIT_CLEAR_WKFMCAM_RWD_8197F(x) | BIT_WKFMCAM_RWD_8197F(v))

/* 2 REG_RXFLTMAP1_8197F (RX FILTER MAP GROUP 1) */
#define BIT_CTRLFLT15EN_8197F BIT(15)
#define BIT_CTRLFLT14EN_8197F BIT(14)
#define BIT_CTRLFLT13EN_8197F BIT(13)
#define BIT_CTRLFLT12EN_8197F BIT(12)
#define BIT_CTRLFLT11EN_8197F BIT(11)
#define BIT_CTRLFLT10EN_8197F BIT(10)
#define BIT_CTRLFLT9EN_8197F BIT(9)
#define BIT_CTRLFLT8EN_8197F BIT(8)
#define BIT_CTRLFLT7EN_8197F BIT(7)
#define BIT_CTRLFLT6EN_8197F BIT(6)
#define BIT_CTRLFLT5EN_8197F BIT(5)
#define BIT_CTRLFLT4EN_8197F BIT(4)
#define BIT_CTRLFLT3EN_8197F BIT(3)
#define BIT_CTRLFLT2EN_8197F BIT(2)
#define BIT_CTRLFLT1EN_8197F BIT(1)
#define BIT_CTRLFLT0EN_8197F BIT(0)

/* 2 REG_RXFLTMAP0_8197F (RX FILTER MAP GROUP 0) */
#define BIT_MGTFLT15EN_8197F BIT(15)
#define BIT_MGTFLT14EN_8197F BIT(14)
#define BIT_MGTFLT13EN_8197F BIT(13)
#define BIT_MGTFLT12EN_8197F BIT(12)
#define BIT_MGTFLT11EN_8197F BIT(11)
#define BIT_MGTFLT10EN_8197F BIT(10)
#define BIT_MGTFLT9EN_8197F BIT(9)
#define BIT_MGTFLT8EN_8197F BIT(8)
#define BIT_MGTFLT7EN_8197F BIT(7)
#define BIT_MGTFLT6EN_8197F BIT(6)
#define BIT_MGTFLT5EN_8197F BIT(5)
#define BIT_MGTFLT4EN_8197F BIT(4)
#define BIT_MGTFLT3EN_8197F BIT(3)
#define BIT_MGTFLT2EN_8197F BIT(2)
#define BIT_MGTFLT1EN_8197F BIT(1)
#define BIT_MGTFLT0EN_8197F BIT(0)

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_RXFLTMAP_8197F (RX FILTER MAP GROUP 2) */
#define BIT_DATAFLT15EN_8197F BIT(15)
#define BIT_DATAFLT14EN_8197F BIT(14)
#define BIT_DATAFLT13EN_8197F BIT(13)
#define BIT_DATAFLT12EN_8197F BIT(12)
#define BIT_DATAFLT11EN_8197F BIT(11)
#define BIT_DATAFLT10EN_8197F BIT(10)
#define BIT_DATAFLT9EN_8197F BIT(9)
#define BIT_DATAFLT8EN_8197F BIT(8)
#define BIT_DATAFLT7EN_8197F BIT(7)
#define BIT_DATAFLT6EN_8197F BIT(6)
#define BIT_DATAFLT5EN_8197F BIT(5)
#define BIT_DATAFLT4EN_8197F BIT(4)
#define BIT_DATAFLT3EN_8197F BIT(3)
#define BIT_DATAFLT2EN_8197F BIT(2)
#define BIT_DATAFLT1EN_8197F BIT(1)
#define BIT_DATAFLT0EN_8197F BIT(0)

/* 2 REG_BCN_PSR_RPT_8197F (BEACON PARSER REPORT REGISTER) */

#define BIT_SHIFT_DTIM_CNT_8197F 24
#define BIT_MASK_DTIM_CNT_8197F 0xff
#define BIT_DTIM_CNT_8197F(x)                                                  \
	(((x) & BIT_MASK_DTIM_CNT_8197F) << BIT_SHIFT_DTIM_CNT_8197F)
#define BITS_DTIM_CNT_8197F                                                    \
	(BIT_MASK_DTIM_CNT_8197F << BIT_SHIFT_DTIM_CNT_8197F)
#define BIT_CLEAR_DTIM_CNT_8197F(x) ((x) & (~BITS_DTIM_CNT_8197F))
#define BIT_GET_DTIM_CNT_8197F(x)                                              \
	(((x) >> BIT_SHIFT_DTIM_CNT_8197F) & BIT_MASK_DTIM_CNT_8197F)
#define BIT_SET_DTIM_CNT_8197F(x, v)                                           \
	(BIT_CLEAR_DTIM_CNT_8197F(x) | BIT_DTIM_CNT_8197F(v))

#define BIT_SHIFT_DTIM_PERIOD_8197F 16
#define BIT_MASK_DTIM_PERIOD_8197F 0xff
#define BIT_DTIM_PERIOD_8197F(x)                                               \
	(((x) & BIT_MASK_DTIM_PERIOD_8197F) << BIT_SHIFT_DTIM_PERIOD_8197F)
#define BITS_DTIM_PERIOD_8197F                                                 \
	(BIT_MASK_DTIM_PERIOD_8197F << BIT_SHIFT_DTIM_PERIOD_8197F)
#define BIT_CLEAR_DTIM_PERIOD_8197F(x) ((x) & (~BITS_DTIM_PERIOD_8197F))
#define BIT_GET_DTIM_PERIOD_8197F(x)                                           \
	(((x) >> BIT_SHIFT_DTIM_PERIOD_8197F) & BIT_MASK_DTIM_PERIOD_8197F)
#define BIT_SET_DTIM_PERIOD_8197F(x, v)                                        \
	(BIT_CLEAR_DTIM_PERIOD_8197F(x) | BIT_DTIM_PERIOD_8197F(v))

#define BIT_DTIM_8197F BIT(15)
#define BIT_TIM_8197F BIT(14)

#define BIT_SHIFT_PS_AID_0_8197F 0
#define BIT_MASK_PS_AID_0_8197F 0x7ff
#define BIT_PS_AID_0_8197F(x)                                                  \
	(((x) & BIT_MASK_PS_AID_0_8197F) << BIT_SHIFT_PS_AID_0_8197F)
#define BITS_PS_AID_0_8197F                                                    \
	(BIT_MASK_PS_AID_0_8197F << BIT_SHIFT_PS_AID_0_8197F)
#define BIT_CLEAR_PS_AID_0_8197F(x) ((x) & (~BITS_PS_AID_0_8197F))
#define BIT_GET_PS_AID_0_8197F(x)                                              \
	(((x) >> BIT_SHIFT_PS_AID_0_8197F) & BIT_MASK_PS_AID_0_8197F)
#define BIT_SET_PS_AID_0_8197F(x, v)                                           \
	(BIT_CLEAR_PS_AID_0_8197F(x) | BIT_PS_AID_0_8197F(v))

/* 2 REG_NOT_VALID_8197F */
#define BIT_FLC_RPCT_V1_8197F BIT(7)
#define BIT_MODE_8197F BIT(6)

#define BIT_SHIFT_TRPCD_8197F 0
#define BIT_MASK_TRPCD_8197F 0x3f
#define BIT_TRPCD_8197F(x)                                                     \
	(((x) & BIT_MASK_TRPCD_8197F) << BIT_SHIFT_TRPCD_8197F)
#define BITS_TRPCD_8197F (BIT_MASK_TRPCD_8197F << BIT_SHIFT_TRPCD_8197F)
#define BIT_CLEAR_TRPCD_8197F(x) ((x) & (~BITS_TRPCD_8197F))
#define BIT_GET_TRPCD_8197F(x)                                                 \
	(((x) >> BIT_SHIFT_TRPCD_8197F) & BIT_MASK_TRPCD_8197F)
#define BIT_SET_TRPCD_8197F(x, v)                                              \
	(BIT_CLEAR_TRPCD_8197F(x) | BIT_TRPCD_8197F(v))

/* 2 REG_NOT_VALID_8197F */
#define BIT_CMF_8197F BIT(2)
#define BIT_CCF_8197F BIT(1)
#define BIT_CDF_8197F BIT(0)

/* 2 REG_NOT_VALID_8197F */

#define BIT_SHIFT_FLC_RPCT_8197F 0
#define BIT_MASK_FLC_RPCT_8197F 0xff
#define BIT_FLC_RPCT_8197F(x)                                                  \
	(((x) & BIT_MASK_FLC_RPCT_8197F) << BIT_SHIFT_FLC_RPCT_8197F)
#define BITS_FLC_RPCT_8197F                                                    \
	(BIT_MASK_FLC_RPCT_8197F << BIT_SHIFT_FLC_RPCT_8197F)
#define BIT_CLEAR_FLC_RPCT_8197F(x) ((x) & (~BITS_FLC_RPCT_8197F))
#define BIT_GET_FLC_RPCT_8197F(x)                                              \
	(((x) >> BIT_SHIFT_FLC_RPCT_8197F) & BIT_MASK_FLC_RPCT_8197F)
#define BIT_SET_FLC_RPCT_8197F(x, v)                                           \
	(BIT_CLEAR_FLC_RPCT_8197F(x) | BIT_FLC_RPCT_8197F(v))

/* 2 REG_NOT_VALID_8197F */

#define BIT_SHIFT_FLC_RPC_8197F 0
#define BIT_MASK_FLC_RPC_8197F 0xff
#define BIT_FLC_RPC_8197F(x)                                                   \
	(((x) & BIT_MASK_FLC_RPC_8197F) << BIT_SHIFT_FLC_RPC_8197F)
#define BITS_FLC_RPC_8197F (BIT_MASK_FLC_RPC_8197F << BIT_SHIFT_FLC_RPC_8197F)
#define BIT_CLEAR_FLC_RPC_8197F(x) ((x) & (~BITS_FLC_RPC_8197F))
#define BIT_GET_FLC_RPC_8197F(x)                                               \
	(((x) >> BIT_SHIFT_FLC_RPC_8197F) & BIT_MASK_FLC_RPC_8197F)
#define BIT_SET_FLC_RPC_8197F(x, v)                                            \
	(BIT_CLEAR_FLC_RPC_8197F(x) | BIT_FLC_RPC_8197F(v))

/* 2 REG_RXPKTMON_CTRL_8197F */

#define BIT_SHIFT_RXBKQPKT_SEQ_8197F 20
#define BIT_MASK_RXBKQPKT_SEQ_8197F 0xf
#define BIT_RXBKQPKT_SEQ_8197F(x)                                              \
	(((x) & BIT_MASK_RXBKQPKT_SEQ_8197F) << BIT_SHIFT_RXBKQPKT_SEQ_8197F)
#define BITS_RXBKQPKT_SEQ_8197F                                                \
	(BIT_MASK_RXBKQPKT_SEQ_8197F << BIT_SHIFT_RXBKQPKT_SEQ_8197F)
#define BIT_CLEAR_RXBKQPKT_SEQ_8197F(x) ((x) & (~BITS_RXBKQPKT_SEQ_8197F))
#define BIT_GET_RXBKQPKT_SEQ_8197F(x)                                          \
	(((x) >> BIT_SHIFT_RXBKQPKT_SEQ_8197F) & BIT_MASK_RXBKQPKT_SEQ_8197F)
#define BIT_SET_RXBKQPKT_SEQ_8197F(x, v)                                       \
	(BIT_CLEAR_RXBKQPKT_SEQ_8197F(x) | BIT_RXBKQPKT_SEQ_8197F(v))

#define BIT_SHIFT_RXBEQPKT_SEQ_8197F 16
#define BIT_MASK_RXBEQPKT_SEQ_8197F 0xf
#define BIT_RXBEQPKT_SEQ_8197F(x)                                              \
	(((x) & BIT_MASK_RXBEQPKT_SEQ_8197F) << BIT_SHIFT_RXBEQPKT_SEQ_8197F)
#define BITS_RXBEQPKT_SEQ_8197F                                                \
	(BIT_MASK_RXBEQPKT_SEQ_8197F << BIT_SHIFT_RXBEQPKT_SEQ_8197F)
#define BIT_CLEAR_RXBEQPKT_SEQ_8197F(x) ((x) & (~BITS_RXBEQPKT_SEQ_8197F))
#define BIT_GET_RXBEQPKT_SEQ_8197F(x)                                          \
	(((x) >> BIT_SHIFT_RXBEQPKT_SEQ_8197F) & BIT_MASK_RXBEQPKT_SEQ_8197F)
#define BIT_SET_RXBEQPKT_SEQ_8197F(x, v)                                       \
	(BIT_CLEAR_RXBEQPKT_SEQ_8197F(x) | BIT_RXBEQPKT_SEQ_8197F(v))

#define BIT_SHIFT_RXVIQPKT_SEQ_8197F 12
#define BIT_MASK_RXVIQPKT_SEQ_8197F 0xf
#define BIT_RXVIQPKT_SEQ_8197F(x)                                              \
	(((x) & BIT_MASK_RXVIQPKT_SEQ_8197F) << BIT_SHIFT_RXVIQPKT_SEQ_8197F)
#define BITS_RXVIQPKT_SEQ_8197F                                                \
	(BIT_MASK_RXVIQPKT_SEQ_8197F << BIT_SHIFT_RXVIQPKT_SEQ_8197F)
#define BIT_CLEAR_RXVIQPKT_SEQ_8197F(x) ((x) & (~BITS_RXVIQPKT_SEQ_8197F))
#define BIT_GET_RXVIQPKT_SEQ_8197F(x)                                          \
	(((x) >> BIT_SHIFT_RXVIQPKT_SEQ_8197F) & BIT_MASK_RXVIQPKT_SEQ_8197F)
#define BIT_SET_RXVIQPKT_SEQ_8197F(x, v)                                       \
	(BIT_CLEAR_RXVIQPKT_SEQ_8197F(x) | BIT_RXVIQPKT_SEQ_8197F(v))

#define BIT_SHIFT_RXVOQPKT_SEQ_8197F 8
#define BIT_MASK_RXVOQPKT_SEQ_8197F 0xf
#define BIT_RXVOQPKT_SEQ_8197F(x)                                              \
	(((x) & BIT_MASK_RXVOQPKT_SEQ_8197F) << BIT_SHIFT_RXVOQPKT_SEQ_8197F)
#define BITS_RXVOQPKT_SEQ_8197F                                                \
	(BIT_MASK_RXVOQPKT_SEQ_8197F << BIT_SHIFT_RXVOQPKT_SEQ_8197F)
#define BIT_CLEAR_RXVOQPKT_SEQ_8197F(x) ((x) & (~BITS_RXVOQPKT_SEQ_8197F))
#define BIT_GET_RXVOQPKT_SEQ_8197F(x)                                          \
	(((x) >> BIT_SHIFT_RXVOQPKT_SEQ_8197F) & BIT_MASK_RXVOQPKT_SEQ_8197F)
#define BIT_SET_RXVOQPKT_SEQ_8197F(x, v)                                       \
	(BIT_CLEAR_RXVOQPKT_SEQ_8197F(x) | BIT_RXVOQPKT_SEQ_8197F(v))

#define BIT_RXBKQPKT_ERR_8197F BIT(7)
#define BIT_RXBEQPKT_ERR_8197F BIT(6)
#define BIT_RXVIQPKT_ERR_8197F BIT(5)
#define BIT_RXVOQPKT_ERR_8197F BIT(4)
#define BIT_RXDMA_MON_EN_8197F BIT(2)
#define BIT_RXPKT_MON_RST_8197F BIT(1)
#define BIT_RXPKT_MON_EN_8197F BIT(0)

/* 2 REG_STATE_MON_8197F */

#define BIT_SHIFT_STATE_SEL_8197F 24
#define BIT_MASK_STATE_SEL_8197F 0x1f
#define BIT_STATE_SEL_8197F(x)                                                 \
	(((x) & BIT_MASK_STATE_SEL_8197F) << BIT_SHIFT_STATE_SEL_8197F)
#define BITS_STATE_SEL_8197F                                                   \
	(BIT_MASK_STATE_SEL_8197F << BIT_SHIFT_STATE_SEL_8197F)
#define BIT_CLEAR_STATE_SEL_8197F(x) ((x) & (~BITS_STATE_SEL_8197F))
#define BIT_GET_STATE_SEL_8197F(x)                                             \
	(((x) >> BIT_SHIFT_STATE_SEL_8197F) & BIT_MASK_STATE_SEL_8197F)
#define BIT_SET_STATE_SEL_8197F(x, v)                                          \
	(BIT_CLEAR_STATE_SEL_8197F(x) | BIT_STATE_SEL_8197F(v))

#define BIT_SHIFT_STATE_INFO_8197F 8
#define BIT_MASK_STATE_INFO_8197F 0xff
#define BIT_STATE_INFO_8197F(x)                                                \
	(((x) & BIT_MASK_STATE_INFO_8197F) << BIT_SHIFT_STATE_INFO_8197F)
#define BITS_STATE_INFO_8197F                                                  \
	(BIT_MASK_STATE_INFO_8197F << BIT_SHIFT_STATE_INFO_8197F)
#define BIT_CLEAR_STATE_INFO_8197F(x) ((x) & (~BITS_STATE_INFO_8197F))
#define BIT_GET_STATE_INFO_8197F(x)                                            \
	(((x) >> BIT_SHIFT_STATE_INFO_8197F) & BIT_MASK_STATE_INFO_8197F)
#define BIT_SET_STATE_INFO_8197F(x, v)                                         \
	(BIT_CLEAR_STATE_INFO_8197F(x) | BIT_STATE_INFO_8197F(v))

#define BIT_UPD_NXT_STATE_8197F BIT(7)

#define BIT_SHIFT_CUR_STATE_8197F 0
#define BIT_MASK_CUR_STATE_8197F 0x7f
#define BIT_CUR_STATE_8197F(x)                                                 \
	(((x) & BIT_MASK_CUR_STATE_8197F) << BIT_SHIFT_CUR_STATE_8197F)
#define BITS_CUR_STATE_8197F                                                   \
	(BIT_MASK_CUR_STATE_8197F << BIT_SHIFT_CUR_STATE_8197F)
#define BIT_CLEAR_CUR_STATE_8197F(x) ((x) & (~BITS_CUR_STATE_8197F))
#define BIT_GET_CUR_STATE_8197F(x)                                             \
	(((x) >> BIT_SHIFT_CUR_STATE_8197F) & BIT_MASK_CUR_STATE_8197F)
#define BIT_SET_CUR_STATE_8197F(x, v)                                          \
	(BIT_CLEAR_CUR_STATE_8197F(x) | BIT_CUR_STATE_8197F(v))

/* 2 REG_ERROR_MON_8197F */
#define BIT_MACRX_ERR_1_8197F BIT(17)
#define BIT_MACRX_ERR_0_8197F BIT(16)
#define BIT_MACTX_ERR_3_8197F BIT(3)
#define BIT_MACTX_ERR_2_8197F BIT(2)
#define BIT_MACTX_ERR_1_8197F BIT(1)
#define BIT_MACTX_ERR_0_8197F BIT(0)

/* 2 REG_SEARCH_MACID_8197F */
#define BIT_EN_TXRPTBUF_CLK_8197F BIT(31)

#define BIT_SHIFT_INFO_INDEX_OFFSET_8197F 16
#define BIT_MASK_INFO_INDEX_OFFSET_8197F 0x1fff
#define BIT_INFO_INDEX_OFFSET_8197F(x)                                         \
	(((x) & BIT_MASK_INFO_INDEX_OFFSET_8197F)                              \
	 << BIT_SHIFT_INFO_INDEX_OFFSET_8197F)
#define BITS_INFO_INDEX_OFFSET_8197F                                           \
	(BIT_MASK_INFO_INDEX_OFFSET_8197F << BIT_SHIFT_INFO_INDEX_OFFSET_8197F)
#define BIT_CLEAR_INFO_INDEX_OFFSET_8197F(x)                                   \
	((x) & (~BITS_INFO_INDEX_OFFSET_8197F))
#define BIT_GET_INFO_INDEX_OFFSET_8197F(x)                                     \
	(((x) >> BIT_SHIFT_INFO_INDEX_OFFSET_8197F) &                          \
	 BIT_MASK_INFO_INDEX_OFFSET_8197F)
#define BIT_SET_INFO_INDEX_OFFSET_8197F(x, v)                                  \
	(BIT_CLEAR_INFO_INDEX_OFFSET_8197F(x) | BIT_INFO_INDEX_OFFSET_8197F(v))

#define BIT_DIS_INFOSRCH_8197F BIT(14)
#define BIT_DISABLE_B0_8197F BIT(13)

#define BIT_SHIFT_INFO_ADDR_OFFSET_8197F 0
#define BIT_MASK_INFO_ADDR_OFFSET_8197F 0x1fff
#define BIT_INFO_ADDR_OFFSET_8197F(x)                                          \
	(((x) & BIT_MASK_INFO_ADDR_OFFSET_8197F)                               \
	 << BIT_SHIFT_INFO_ADDR_OFFSET_8197F)
#define BITS_INFO_ADDR_OFFSET_8197F                                            \
	(BIT_MASK_INFO_ADDR_OFFSET_8197F << BIT_SHIFT_INFO_ADDR_OFFSET_8197F)
#define BIT_CLEAR_INFO_ADDR_OFFSET_8197F(x)                                    \
	((x) & (~BITS_INFO_ADDR_OFFSET_8197F))
#define BIT_GET_INFO_ADDR_OFFSET_8197F(x)                                      \
	(((x) >> BIT_SHIFT_INFO_ADDR_OFFSET_8197F) &                           \
	 BIT_MASK_INFO_ADDR_OFFSET_8197F)
#define BIT_SET_INFO_ADDR_OFFSET_8197F(x, v)                                   \
	(BIT_CLEAR_INFO_ADDR_OFFSET_8197F(x) | BIT_INFO_ADDR_OFFSET_8197F(v))

/* 2 REG_BT_COEX_TABLE_8197F (BT-COEXISTENCE CONTROL REGISTER) */
#define BIT_PRI_MASK_RX_RESP_8197F BIT(126)
#define BIT_PRI_MASK_RXOFDM_8197F BIT(125)
#define BIT_PRI_MASK_RXCCK_8197F BIT(124)

#define BIT_SHIFT_PRI_MASK_TXAC_8197F (117 & CPU_OPT_WIDTH)
#define BIT_MASK_PRI_MASK_TXAC_8197F 0x7f
#define BIT_PRI_MASK_TXAC_8197F(x)                                             \
	(((x) & BIT_MASK_PRI_MASK_TXAC_8197F) << BIT_SHIFT_PRI_MASK_TXAC_8197F)
#define BITS_PRI_MASK_TXAC_8197F                                               \
	(BIT_MASK_PRI_MASK_TXAC_8197F << BIT_SHIFT_PRI_MASK_TXAC_8197F)
#define BIT_CLEAR_PRI_MASK_TXAC_8197F(x) ((x) & (~BITS_PRI_MASK_TXAC_8197F))
#define BIT_GET_PRI_MASK_TXAC_8197F(x)                                         \
	(((x) >> BIT_SHIFT_PRI_MASK_TXAC_8197F) & BIT_MASK_PRI_MASK_TXAC_8197F)
#define BIT_SET_PRI_MASK_TXAC_8197F(x, v)                                      \
	(BIT_CLEAR_PRI_MASK_TXAC_8197F(x) | BIT_PRI_MASK_TXAC_8197F(v))

#define BIT_SHIFT_PRI_MASK_NAV_8197F (109 & CPU_OPT_WIDTH)
#define BIT_MASK_PRI_MASK_NAV_8197F 0xff
#define BIT_PRI_MASK_NAV_8197F(x)                                              \
	(((x) & BIT_MASK_PRI_MASK_NAV_8197F) << BIT_SHIFT_PRI_MASK_NAV_8197F)
#define BITS_PRI_MASK_NAV_8197F                                                \
	(BIT_MASK_PRI_MASK_NAV_8197F << BIT_SHIFT_PRI_MASK_NAV_8197F)
#define BIT_CLEAR_PRI_MASK_NAV_8197F(x) ((x) & (~BITS_PRI_MASK_NAV_8197F))
#define BIT_GET_PRI_MASK_NAV_8197F(x)                                          \
	(((x) >> BIT_SHIFT_PRI_MASK_NAV_8197F) & BIT_MASK_PRI_MASK_NAV_8197F)
#define BIT_SET_PRI_MASK_NAV_8197F(x, v)                                       \
	(BIT_CLEAR_PRI_MASK_NAV_8197F(x) | BIT_PRI_MASK_NAV_8197F(v))

#define BIT_PRI_MASK_CCK_8197F BIT(108)
#define BIT_PRI_MASK_OFDM_8197F BIT(107)
#define BIT_PRI_MASK_RTY_8197F BIT(106)

#define BIT_SHIFT_PRI_MASK_NUM_8197F (102 & CPU_OPT_WIDTH)
#define BIT_MASK_PRI_MASK_NUM_8197F 0xf
#define BIT_PRI_MASK_NUM_8197F(x)                                              \
	(((x) & BIT_MASK_PRI_MASK_NUM_8197F) << BIT_SHIFT_PRI_MASK_NUM_8197F)
#define BITS_PRI_MASK_NUM_8197F                                                \
	(BIT_MASK_PRI_MASK_NUM_8197F << BIT_SHIFT_PRI_MASK_NUM_8197F)
#define BIT_CLEAR_PRI_MASK_NUM_8197F(x) ((x) & (~BITS_PRI_MASK_NUM_8197F))
#define BIT_GET_PRI_MASK_NUM_8197F(x)                                          \
	(((x) >> BIT_SHIFT_PRI_MASK_NUM_8197F) & BIT_MASK_PRI_MASK_NUM_8197F)
#define BIT_SET_PRI_MASK_NUM_8197F(x, v)                                       \
	(BIT_CLEAR_PRI_MASK_NUM_8197F(x) | BIT_PRI_MASK_NUM_8197F(v))

#define BIT_SHIFT_PRI_MASK_TYPE_8197F (98 & CPU_OPT_WIDTH)
#define BIT_MASK_PRI_MASK_TYPE_8197F 0xf
#define BIT_PRI_MASK_TYPE_8197F(x)                                             \
	(((x) & BIT_MASK_PRI_MASK_TYPE_8197F) << BIT_SHIFT_PRI_MASK_TYPE_8197F)
#define BITS_PRI_MASK_TYPE_8197F                                               \
	(BIT_MASK_PRI_MASK_TYPE_8197F << BIT_SHIFT_PRI_MASK_TYPE_8197F)
#define BIT_CLEAR_PRI_MASK_TYPE_8197F(x) ((x) & (~BITS_PRI_MASK_TYPE_8197F))
#define BIT_GET_PRI_MASK_TYPE_8197F(x)                                         \
	(((x) >> BIT_SHIFT_PRI_MASK_TYPE_8197F) & BIT_MASK_PRI_MASK_TYPE_8197F)
#define BIT_SET_PRI_MASK_TYPE_8197F(x, v)                                      \
	(BIT_CLEAR_PRI_MASK_TYPE_8197F(x) | BIT_PRI_MASK_TYPE_8197F(v))

#define BIT_OOB_8197F BIT(97)
#define BIT_ANT_SEL_8197F BIT(96)

#define BIT_SHIFT_BREAK_TABLE_2_8197F (80 & CPU_OPT_WIDTH)
#define BIT_MASK_BREAK_TABLE_2_8197F 0xffff
#define BIT_BREAK_TABLE_2_8197F(x)                                             \
	(((x) & BIT_MASK_BREAK_TABLE_2_8197F) << BIT_SHIFT_BREAK_TABLE_2_8197F)
#define BITS_BREAK_TABLE_2_8197F                                               \
	(BIT_MASK_BREAK_TABLE_2_8197F << BIT_SHIFT_BREAK_TABLE_2_8197F)
#define BIT_CLEAR_BREAK_TABLE_2_8197F(x) ((x) & (~BITS_BREAK_TABLE_2_8197F))
#define BIT_GET_BREAK_TABLE_2_8197F(x)                                         \
	(((x) >> BIT_SHIFT_BREAK_TABLE_2_8197F) & BIT_MASK_BREAK_TABLE_2_8197F)
#define BIT_SET_BREAK_TABLE_2_8197F(x, v)                                      \
	(BIT_CLEAR_BREAK_TABLE_2_8197F(x) | BIT_BREAK_TABLE_2_8197F(v))

#define BIT_SHIFT_BREAK_TABLE_1_8197F (64 & CPU_OPT_WIDTH)
#define BIT_MASK_BREAK_TABLE_1_8197F 0xffff
#define BIT_BREAK_TABLE_1_8197F(x)                                             \
	(((x) & BIT_MASK_BREAK_TABLE_1_8197F) << BIT_SHIFT_BREAK_TABLE_1_8197F)
#define BITS_BREAK_TABLE_1_8197F                                               \
	(BIT_MASK_BREAK_TABLE_1_8197F << BIT_SHIFT_BREAK_TABLE_1_8197F)
#define BIT_CLEAR_BREAK_TABLE_1_8197F(x) ((x) & (~BITS_BREAK_TABLE_1_8197F))
#define BIT_GET_BREAK_TABLE_1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_BREAK_TABLE_1_8197F) & BIT_MASK_BREAK_TABLE_1_8197F)
#define BIT_SET_BREAK_TABLE_1_8197F(x, v)                                      \
	(BIT_CLEAR_BREAK_TABLE_1_8197F(x) | BIT_BREAK_TABLE_1_8197F(v))

#define BIT_SHIFT_COEX_TABLE_2_8197F (32 & CPU_OPT_WIDTH)
#define BIT_MASK_COEX_TABLE_2_8197F 0xffffffffL
#define BIT_COEX_TABLE_2_8197F(x)                                              \
	(((x) & BIT_MASK_COEX_TABLE_2_8197F) << BIT_SHIFT_COEX_TABLE_2_8197F)
#define BITS_COEX_TABLE_2_8197F                                                \
	(BIT_MASK_COEX_TABLE_2_8197F << BIT_SHIFT_COEX_TABLE_2_8197F)
#define BIT_CLEAR_COEX_TABLE_2_8197F(x) ((x) & (~BITS_COEX_TABLE_2_8197F))
#define BIT_GET_COEX_TABLE_2_8197F(x)                                          \
	(((x) >> BIT_SHIFT_COEX_TABLE_2_8197F) & BIT_MASK_COEX_TABLE_2_8197F)
#define BIT_SET_COEX_TABLE_2_8197F(x, v)                                       \
	(BIT_CLEAR_COEX_TABLE_2_8197F(x) | BIT_COEX_TABLE_2_8197F(v))

#define BIT_SHIFT_COEX_TABLE_1_8197F 0
#define BIT_MASK_COEX_TABLE_1_8197F 0xffffffffL
#define BIT_COEX_TABLE_1_8197F(x)                                              \
	(((x) & BIT_MASK_COEX_TABLE_1_8197F) << BIT_SHIFT_COEX_TABLE_1_8197F)
#define BITS_COEX_TABLE_1_8197F                                                \
	(BIT_MASK_COEX_TABLE_1_8197F << BIT_SHIFT_COEX_TABLE_1_8197F)
#define BIT_CLEAR_COEX_TABLE_1_8197F(x) ((x) & (~BITS_COEX_TABLE_1_8197F))
#define BIT_GET_COEX_TABLE_1_8197F(x)                                          \
	(((x) >> BIT_SHIFT_COEX_TABLE_1_8197F) & BIT_MASK_COEX_TABLE_1_8197F)
#define BIT_SET_COEX_TABLE_1_8197F(x, v)                                       \
	(BIT_CLEAR_COEX_TABLE_1_8197F(x) | BIT_COEX_TABLE_1_8197F(v))

/* 2 REG_RXCMD_0_8197F */
#define BIT_RXCMD_EN_8197F BIT(31)

#define BIT_SHIFT_RXCMD_INFO_8197F 0
#define BIT_MASK_RXCMD_INFO_8197F 0x7fffffffL
#define BIT_RXCMD_INFO_8197F(x)                                                \
	(((x) & BIT_MASK_RXCMD_INFO_8197F) << BIT_SHIFT_RXCMD_INFO_8197F)
#define BITS_RXCMD_INFO_8197F                                                  \
	(BIT_MASK_RXCMD_INFO_8197F << BIT_SHIFT_RXCMD_INFO_8197F)
#define BIT_CLEAR_RXCMD_INFO_8197F(x) ((x) & (~BITS_RXCMD_INFO_8197F))
#define BIT_GET_RXCMD_INFO_8197F(x)                                            \
	(((x) >> BIT_SHIFT_RXCMD_INFO_8197F) & BIT_MASK_RXCMD_INFO_8197F)
#define BIT_SET_RXCMD_INFO_8197F(x, v)                                         \
	(BIT_CLEAR_RXCMD_INFO_8197F(x) | BIT_RXCMD_INFO_8197F(v))

/* 2 REG_RXCMD_1_8197F */

#define BIT_SHIFT_RXCMD_PRD_8197F 0
#define BIT_MASK_RXCMD_PRD_8197F 0xffff
#define BIT_RXCMD_PRD_8197F(x)                                                 \
	(((x) & BIT_MASK_RXCMD_PRD_8197F) << BIT_SHIFT_RXCMD_PRD_8197F)
#define BITS_RXCMD_PRD_8197F                                                   \
	(BIT_MASK_RXCMD_PRD_8197F << BIT_SHIFT_RXCMD_PRD_8197F)
#define BIT_CLEAR_RXCMD_PRD_8197F(x) ((x) & (~BITS_RXCMD_PRD_8197F))
#define BIT_GET_RXCMD_PRD_8197F(x)                                             \
	(((x) >> BIT_SHIFT_RXCMD_PRD_8197F) & BIT_MASK_RXCMD_PRD_8197F)
#define BIT_SET_RXCMD_PRD_8197F(x, v)                                          \
	(BIT_CLEAR_RXCMD_PRD_8197F(x) | BIT_RXCMD_PRD_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_WMAC_RESP_TXINFO_8197F (RESPONSE TXINFO REGISTER) */

#define BIT_SHIFT_WMAC_RESP_MFB_8197F 25
#define BIT_MASK_WMAC_RESP_MFB_8197F 0x7f
#define BIT_WMAC_RESP_MFB_8197F(x)                                             \
	(((x) & BIT_MASK_WMAC_RESP_MFB_8197F) << BIT_SHIFT_WMAC_RESP_MFB_8197F)
#define BITS_WMAC_RESP_MFB_8197F                                               \
	(BIT_MASK_WMAC_RESP_MFB_8197F << BIT_SHIFT_WMAC_RESP_MFB_8197F)
#define BIT_CLEAR_WMAC_RESP_MFB_8197F(x) ((x) & (~BITS_WMAC_RESP_MFB_8197F))
#define BIT_GET_WMAC_RESP_MFB_8197F(x)                                         \
	(((x) >> BIT_SHIFT_WMAC_RESP_MFB_8197F) & BIT_MASK_WMAC_RESP_MFB_8197F)
#define BIT_SET_WMAC_RESP_MFB_8197F(x, v)                                      \
	(BIT_CLEAR_WMAC_RESP_MFB_8197F(x) | BIT_WMAC_RESP_MFB_8197F(v))

#define BIT_SHIFT_WMAC_ANTINF_SEL_8197F 23
#define BIT_MASK_WMAC_ANTINF_SEL_8197F 0x3
#define BIT_WMAC_ANTINF_SEL_8197F(x)                                           \
	(((x) & BIT_MASK_WMAC_ANTINF_SEL_8197F)                                \
	 << BIT_SHIFT_WMAC_ANTINF_SEL_8197F)
#define BITS_WMAC_ANTINF_SEL_8197F                                             \
	(BIT_MASK_WMAC_ANTINF_SEL_8197F << BIT_SHIFT_WMAC_ANTINF_SEL_8197F)
#define BIT_CLEAR_WMAC_ANTINF_SEL_8197F(x) ((x) & (~BITS_WMAC_ANTINF_SEL_8197F))
#define BIT_GET_WMAC_ANTINF_SEL_8197F(x)                                       \
	(((x) >> BIT_SHIFT_WMAC_ANTINF_SEL_8197F) &                            \
	 BIT_MASK_WMAC_ANTINF_SEL_8197F)
#define BIT_SET_WMAC_ANTINF_SEL_8197F(x, v)                                    \
	(BIT_CLEAR_WMAC_ANTINF_SEL_8197F(x) | BIT_WMAC_ANTINF_SEL_8197F(v))

#define BIT_SHIFT_WMAC_ANTSEL_SEL_8197F 21
#define BIT_MASK_WMAC_ANTSEL_SEL_8197F 0x3
#define BIT_WMAC_ANTSEL_SEL_8197F(x)                                           \
	(((x) & BIT_MASK_WMAC_ANTSEL_SEL_8197F)                                \
	 << BIT_SHIFT_WMAC_ANTSEL_SEL_8197F)
#define BITS_WMAC_ANTSEL_SEL_8197F                                             \
	(BIT_MASK_WMAC_ANTSEL_SEL_8197F << BIT_SHIFT_WMAC_ANTSEL_SEL_8197F)
#define BIT_CLEAR_WMAC_ANTSEL_SEL_8197F(x) ((x) & (~BITS_WMAC_ANTSEL_SEL_8197F))
#define BIT_GET_WMAC_ANTSEL_SEL_8197F(x)                                       \
	(((x) >> BIT_SHIFT_WMAC_ANTSEL_SEL_8197F) &                            \
	 BIT_MASK_WMAC_ANTSEL_SEL_8197F)
#define BIT_SET_WMAC_ANTSEL_SEL_8197F(x, v)                                    \
	(BIT_CLEAR_WMAC_ANTSEL_SEL_8197F(x) | BIT_WMAC_ANTSEL_SEL_8197F(v))

#define BIT_SHIFT_R_WMAC_RESP_TXPOWER_8197F 18
#define BIT_MASK_R_WMAC_RESP_TXPOWER_8197F 0x7
#define BIT_R_WMAC_RESP_TXPOWER_8197F(x)                                       \
	(((x) & BIT_MASK_R_WMAC_RESP_TXPOWER_8197F)                            \
	 << BIT_SHIFT_R_WMAC_RESP_TXPOWER_8197F)
#define BITS_R_WMAC_RESP_TXPOWER_8197F                                         \
	(BIT_MASK_R_WMAC_RESP_TXPOWER_8197F                                    \
	 << BIT_SHIFT_R_WMAC_RESP_TXPOWER_8197F)
#define BIT_CLEAR_R_WMAC_RESP_TXPOWER_8197F(x)                                 \
	((x) & (~BITS_R_WMAC_RESP_TXPOWER_8197F))
#define BIT_GET_R_WMAC_RESP_TXPOWER_8197F(x)                                   \
	(((x) >> BIT_SHIFT_R_WMAC_RESP_TXPOWER_8197F) &                        \
	 BIT_MASK_R_WMAC_RESP_TXPOWER_8197F)
#define BIT_SET_R_WMAC_RESP_TXPOWER_8197F(x, v)                                \
	(BIT_CLEAR_R_WMAC_RESP_TXPOWER_8197F(x) |                              \
	 BIT_R_WMAC_RESP_TXPOWER_8197F(v))

#define BIT_SHIFT_WMAC_RESP_TXANT_8197F 0
#define BIT_MASK_WMAC_RESP_TXANT_8197F 0x3ffff
#define BIT_WMAC_RESP_TXANT_8197F(x)                                           \
	(((x) & BIT_MASK_WMAC_RESP_TXANT_8197F)                                \
	 << BIT_SHIFT_WMAC_RESP_TXANT_8197F)
#define BITS_WMAC_RESP_TXANT_8197F                                             \
	(BIT_MASK_WMAC_RESP_TXANT_8197F << BIT_SHIFT_WMAC_RESP_TXANT_8197F)
#define BIT_CLEAR_WMAC_RESP_TXANT_8197F(x) ((x) & (~BITS_WMAC_RESP_TXANT_8197F))
#define BIT_GET_WMAC_RESP_TXANT_8197F(x)                                       \
	(((x) >> BIT_SHIFT_WMAC_RESP_TXANT_8197F) &                            \
	 BIT_MASK_WMAC_RESP_TXANT_8197F)
#define BIT_SET_WMAC_RESP_TXANT_8197F(x, v)                                    \
	(BIT_CLEAR_WMAC_RESP_TXANT_8197F(x) | BIT_WMAC_RESP_TXANT_8197F(v))

/* 2 REG_BBPSF_CTRL_8197F */
#define BIT_CTL_IDLE_CLR_CSI_RPT_8197F BIT(31)
#define BIT_WMAC_USE_NDPARATE_8197F BIT(30)

#define BIT_SHIFT_WMAC_CSI_RATE_8197F 24
#define BIT_MASK_WMAC_CSI_RATE_8197F 0x3f
#define BIT_WMAC_CSI_RATE_8197F(x)                                             \
	(((x) & BIT_MASK_WMAC_CSI_RATE_8197F) << BIT_SHIFT_WMAC_CSI_RATE_8197F)
#define BITS_WMAC_CSI_RATE_8197F                                               \
	(BIT_MASK_WMAC_CSI_RATE_8197F << BIT_SHIFT_WMAC_CSI_RATE_8197F)
#define BIT_CLEAR_WMAC_CSI_RATE_8197F(x) ((x) & (~BITS_WMAC_CSI_RATE_8197F))
#define BIT_GET_WMAC_CSI_RATE_8197F(x)                                         \
	(((x) >> BIT_SHIFT_WMAC_CSI_RATE_8197F) & BIT_MASK_WMAC_CSI_RATE_8197F)
#define BIT_SET_WMAC_CSI_RATE_8197F(x, v)                                      \
	(BIT_CLEAR_WMAC_CSI_RATE_8197F(x) | BIT_WMAC_CSI_RATE_8197F(v))

#define BIT_SHIFT_WMAC_RESP_TXRATE_8197F 16
#define BIT_MASK_WMAC_RESP_TXRATE_8197F 0xff
#define BIT_WMAC_RESP_TXRATE_8197F(x)                                          \
	(((x) & BIT_MASK_WMAC_RESP_TXRATE_8197F)                               \
	 << BIT_SHIFT_WMAC_RESP_TXRATE_8197F)
#define BITS_WMAC_RESP_TXRATE_8197F                                            \
	(BIT_MASK_WMAC_RESP_TXRATE_8197F << BIT_SHIFT_WMAC_RESP_TXRATE_8197F)
#define BIT_CLEAR_WMAC_RESP_TXRATE_8197F(x)                                    \
	((x) & (~BITS_WMAC_RESP_TXRATE_8197F))
#define BIT_GET_WMAC_RESP_TXRATE_8197F(x)                                      \
	(((x) >> BIT_SHIFT_WMAC_RESP_TXRATE_8197F) &                           \
	 BIT_MASK_WMAC_RESP_TXRATE_8197F)
#define BIT_SET_WMAC_RESP_TXRATE_8197F(x, v)                                   \
	(BIT_CLEAR_WMAC_RESP_TXRATE_8197F(x) | BIT_WMAC_RESP_TXRATE_8197F(v))

#define BIT_BBPSF_MPDUCHKEN_8197F BIT(5)
#define BIT_BBPSF_MHCHKEN_8197F BIT(4)
#define BIT_BBPSF_ERRCHKEN_8197F BIT(3)

#define BIT_SHIFT_BBPSF_ERRTHR_8197F 0
#define BIT_MASK_BBPSF_ERRTHR_8197F 0x7
#define BIT_BBPSF_ERRTHR_8197F(x)                                              \
	(((x) & BIT_MASK_BBPSF_ERRTHR_8197F) << BIT_SHIFT_BBPSF_ERRTHR_8197F)
#define BITS_BBPSF_ERRTHR_8197F                                                \
	(BIT_MASK_BBPSF_ERRTHR_8197F << BIT_SHIFT_BBPSF_ERRTHR_8197F)
#define BIT_CLEAR_BBPSF_ERRTHR_8197F(x) ((x) & (~BITS_BBPSF_ERRTHR_8197F))
#define BIT_GET_BBPSF_ERRTHR_8197F(x)                                          \
	(((x) >> BIT_SHIFT_BBPSF_ERRTHR_8197F) & BIT_MASK_BBPSF_ERRTHR_8197F)
#define BIT_SET_BBPSF_ERRTHR_8197F(x, v)                                       \
	(BIT_CLEAR_BBPSF_ERRTHR_8197F(x) | BIT_BBPSF_ERRTHR_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_P2P_RX_BCN_NOA_8197F (P2P RX BEACON NOA REGISTER) */
#define BIT_NOA_PARSER_EN_8197F BIT(15)

#define BIT_SHIFT_BSSID_SEL_V1_8197F 12
#define BIT_MASK_BSSID_SEL_V1_8197F 0x7
#define BIT_BSSID_SEL_V1_8197F(x)                                              \
	(((x) & BIT_MASK_BSSID_SEL_V1_8197F) << BIT_SHIFT_BSSID_SEL_V1_8197F)
#define BITS_BSSID_SEL_V1_8197F                                                \
	(BIT_MASK_BSSID_SEL_V1_8197F << BIT_SHIFT_BSSID_SEL_V1_8197F)
#define BIT_CLEAR_BSSID_SEL_V1_8197F(x) ((x) & (~BITS_BSSID_SEL_V1_8197F))
#define BIT_GET_BSSID_SEL_V1_8197F(x)                                          \
	(((x) >> BIT_SHIFT_BSSID_SEL_V1_8197F) & BIT_MASK_BSSID_SEL_V1_8197F)
#define BIT_SET_BSSID_SEL_V1_8197F(x, v)                                       \
	(BIT_CLEAR_BSSID_SEL_V1_8197F(x) | BIT_BSSID_SEL_V1_8197F(v))

#define BIT_SHIFT_P2P_OUI_TYPE_8197F 0
#define BIT_MASK_P2P_OUI_TYPE_8197F 0xff
#define BIT_P2P_OUI_TYPE_8197F(x)                                              \
	(((x) & BIT_MASK_P2P_OUI_TYPE_8197F) << BIT_SHIFT_P2P_OUI_TYPE_8197F)
#define BITS_P2P_OUI_TYPE_8197F                                                \
	(BIT_MASK_P2P_OUI_TYPE_8197F << BIT_SHIFT_P2P_OUI_TYPE_8197F)
#define BIT_CLEAR_P2P_OUI_TYPE_8197F(x) ((x) & (~BITS_P2P_OUI_TYPE_8197F))
#define BIT_GET_P2P_OUI_TYPE_8197F(x)                                          \
	(((x) >> BIT_SHIFT_P2P_OUI_TYPE_8197F) & BIT_MASK_P2P_OUI_TYPE_8197F)
#define BIT_SET_P2P_OUI_TYPE_8197F(x, v)                                       \
	(BIT_CLEAR_P2P_OUI_TYPE_8197F(x) | BIT_P2P_OUI_TYPE_8197F(v))

/* 2 REG_ASSOCIATED_BFMER0_INFO_8197F (ASSOCIATED BEAMFORMER0 INFO REGISTER) */

#define BIT_SHIFT_R_WMAC_TXCSI_AID0_8197F (48 & CPU_OPT_WIDTH)
#define BIT_MASK_R_WMAC_TXCSI_AID0_8197F 0x1ff
#define BIT_R_WMAC_TXCSI_AID0_8197F(x)                                         \
	(((x) & BIT_MASK_R_WMAC_TXCSI_AID0_8197F)                              \
	 << BIT_SHIFT_R_WMAC_TXCSI_AID0_8197F)
#define BITS_R_WMAC_TXCSI_AID0_8197F                                           \
	(BIT_MASK_R_WMAC_TXCSI_AID0_8197F << BIT_SHIFT_R_WMAC_TXCSI_AID0_8197F)
#define BIT_CLEAR_R_WMAC_TXCSI_AID0_8197F(x)                                   \
	((x) & (~BITS_R_WMAC_TXCSI_AID0_8197F))
#define BIT_GET_R_WMAC_TXCSI_AID0_8197F(x)                                     \
	(((x) >> BIT_SHIFT_R_WMAC_TXCSI_AID0_8197F) &                          \
	 BIT_MASK_R_WMAC_TXCSI_AID0_8197F)
#define BIT_SET_R_WMAC_TXCSI_AID0_8197F(x, v)                                  \
	(BIT_CLEAR_R_WMAC_TXCSI_AID0_8197F(x) | BIT_R_WMAC_TXCSI_AID0_8197F(v))

#define BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R0_8197F 0
#define BIT_MASK_R_WMAC_SOUNDING_RXADD_R0_8197F 0xffffffffffffL
#define BIT_R_WMAC_SOUNDING_RXADD_R0_8197F(x)                                  \
	(((x) & BIT_MASK_R_WMAC_SOUNDING_RXADD_R0_8197F)                       \
	 << BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R0_8197F)
#define BITS_R_WMAC_SOUNDING_RXADD_R0_8197F                                    \
	(BIT_MASK_R_WMAC_SOUNDING_RXADD_R0_8197F                               \
	 << BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R0_8197F)
#define BIT_CLEAR_R_WMAC_SOUNDING_RXADD_R0_8197F(x)                            \
	((x) & (~BITS_R_WMAC_SOUNDING_RXADD_R0_8197F))
#define BIT_GET_R_WMAC_SOUNDING_RXADD_R0_8197F(x)                              \
	(((x) >> BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R0_8197F) &                   \
	 BIT_MASK_R_WMAC_SOUNDING_RXADD_R0_8197F)
#define BIT_SET_R_WMAC_SOUNDING_RXADD_R0_8197F(x, v)                           \
	(BIT_CLEAR_R_WMAC_SOUNDING_RXADD_R0_8197F(x) |                         \
	 BIT_R_WMAC_SOUNDING_RXADD_R0_8197F(v))

/* 2 REG_ASSOCIATED_BFMER1_INFO_8197F (ASSOCIATED BEAMFORMER1 INFO REGISTER) */

#define BIT_SHIFT_R_WMAC_TXCSI_AID1_8197F (48 & CPU_OPT_WIDTH)
#define BIT_MASK_R_WMAC_TXCSI_AID1_8197F 0x1ff
#define BIT_R_WMAC_TXCSI_AID1_8197F(x)                                         \
	(((x) & BIT_MASK_R_WMAC_TXCSI_AID1_8197F)                              \
	 << BIT_SHIFT_R_WMAC_TXCSI_AID1_8197F)
#define BITS_R_WMAC_TXCSI_AID1_8197F                                           \
	(BIT_MASK_R_WMAC_TXCSI_AID1_8197F << BIT_SHIFT_R_WMAC_TXCSI_AID1_8197F)
#define BIT_CLEAR_R_WMAC_TXCSI_AID1_8197F(x)                                   \
	((x) & (~BITS_R_WMAC_TXCSI_AID1_8197F))
#define BIT_GET_R_WMAC_TXCSI_AID1_8197F(x)                                     \
	(((x) >> BIT_SHIFT_R_WMAC_TXCSI_AID1_8197F) &                          \
	 BIT_MASK_R_WMAC_TXCSI_AID1_8197F)
#define BIT_SET_R_WMAC_TXCSI_AID1_8197F(x, v)                                  \
	(BIT_CLEAR_R_WMAC_TXCSI_AID1_8197F(x) | BIT_R_WMAC_TXCSI_AID1_8197F(v))

#define BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R1_8197F 0
#define BIT_MASK_R_WMAC_SOUNDING_RXADD_R1_8197F 0xffffffffffffL
#define BIT_R_WMAC_SOUNDING_RXADD_R1_8197F(x)                                  \
	(((x) & BIT_MASK_R_WMAC_SOUNDING_RXADD_R1_8197F)                       \
	 << BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R1_8197F)
#define BITS_R_WMAC_SOUNDING_RXADD_R1_8197F                                    \
	(BIT_MASK_R_WMAC_SOUNDING_RXADD_R1_8197F                               \
	 << BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R1_8197F)
#define BIT_CLEAR_R_WMAC_SOUNDING_RXADD_R1_8197F(x)                            \
	((x) & (~BITS_R_WMAC_SOUNDING_RXADD_R1_8197F))
#define BIT_GET_R_WMAC_SOUNDING_RXADD_R1_8197F(x)                              \
	(((x) >> BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R1_8197F) &                   \
	 BIT_MASK_R_WMAC_SOUNDING_RXADD_R1_8197F)
#define BIT_SET_R_WMAC_SOUNDING_RXADD_R1_8197F(x, v)                           \
	(BIT_CLEAR_R_WMAC_SOUNDING_RXADD_R1_8197F(x) |                         \
	 BIT_R_WMAC_SOUNDING_RXADD_R1_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_TX_CSI_RPT_PARAM_BW20_8197F (TX CSI REPORT PARAMETER_BW20 REGISTER) */

#define BIT_SHIFT_R_WMAC_BFINFO_20M_1_8197F 16
#define BIT_MASK_R_WMAC_BFINFO_20M_1_8197F 0xfff
#define BIT_R_WMAC_BFINFO_20M_1_8197F(x)                                       \
	(((x) & BIT_MASK_R_WMAC_BFINFO_20M_1_8197F)                            \
	 << BIT_SHIFT_R_WMAC_BFINFO_20M_1_8197F)
#define BITS_R_WMAC_BFINFO_20M_1_8197F                                         \
	(BIT_MASK_R_WMAC_BFINFO_20M_1_8197F                                    \
	 << BIT_SHIFT_R_WMAC_BFINFO_20M_1_8197F)
#define BIT_CLEAR_R_WMAC_BFINFO_20M_1_8197F(x)                                 \
	((x) & (~BITS_R_WMAC_BFINFO_20M_1_8197F))
#define BIT_GET_R_WMAC_BFINFO_20M_1_8197F(x)                                   \
	(((x) >> BIT_SHIFT_R_WMAC_BFINFO_20M_1_8197F) &                        \
	 BIT_MASK_R_WMAC_BFINFO_20M_1_8197F)
#define BIT_SET_R_WMAC_BFINFO_20M_1_8197F(x, v)                                \
	(BIT_CLEAR_R_WMAC_BFINFO_20M_1_8197F(x) |                              \
	 BIT_R_WMAC_BFINFO_20M_1_8197F(v))

#define BIT_SHIFT_R_WMAC_BFINFO_20M_0_8197F 0
#define BIT_MASK_R_WMAC_BFINFO_20M_0_8197F 0xfff
#define BIT_R_WMAC_BFINFO_20M_0_8197F(x)                                       \
	(((x) & BIT_MASK_R_WMAC_BFINFO_20M_0_8197F)                            \
	 << BIT_SHIFT_R_WMAC_BFINFO_20M_0_8197F)
#define BITS_R_WMAC_BFINFO_20M_0_8197F                                         \
	(BIT_MASK_R_WMAC_BFINFO_20M_0_8197F                                    \
	 << BIT_SHIFT_R_WMAC_BFINFO_20M_0_8197F)
#define BIT_CLEAR_R_WMAC_BFINFO_20M_0_8197F(x)                                 \
	((x) & (~BITS_R_WMAC_BFINFO_20M_0_8197F))
#define BIT_GET_R_WMAC_BFINFO_20M_0_8197F(x)                                   \
	(((x) >> BIT_SHIFT_R_WMAC_BFINFO_20M_0_8197F) &                        \
	 BIT_MASK_R_WMAC_BFINFO_20M_0_8197F)
#define BIT_SET_R_WMAC_BFINFO_20M_0_8197F(x, v)                                \
	(BIT_CLEAR_R_WMAC_BFINFO_20M_0_8197F(x) |                              \
	 BIT_R_WMAC_BFINFO_20M_0_8197F(v))

/* 2 REG_TX_CSI_RPT_PARAM_BW40_8197F (TX CSI REPORT PARAMETER_BW40 REGISTER) */

#define BIT_SHIFT_WMAC_RESP_ANTCD_8197F 0
#define BIT_MASK_WMAC_RESP_ANTCD_8197F 0xf
#define BIT_WMAC_RESP_ANTCD_8197F(x)                                           \
	(((x) & BIT_MASK_WMAC_RESP_ANTCD_8197F)                                \
	 << BIT_SHIFT_WMAC_RESP_ANTCD_8197F)
#define BITS_WMAC_RESP_ANTCD_8197F                                             \
	(BIT_MASK_WMAC_RESP_ANTCD_8197F << BIT_SHIFT_WMAC_RESP_ANTCD_8197F)
#define BIT_CLEAR_WMAC_RESP_ANTCD_8197F(x) ((x) & (~BITS_WMAC_RESP_ANTCD_8197F))
#define BIT_GET_WMAC_RESP_ANTCD_8197F(x)                                       \
	(((x) >> BIT_SHIFT_WMAC_RESP_ANTCD_8197F) &                            \
	 BIT_MASK_WMAC_RESP_ANTCD_8197F)
#define BIT_SET_WMAC_RESP_ANTCD_8197F(x, v)                                    \
	(BIT_CLEAR_WMAC_RESP_ANTCD_8197F(x) | BIT_WMAC_RESP_ANTCD_8197F(v))

/* 2 REG_TX_CSI_RPT_PARAM_BW80_8197F (TX CSI REPORT PARAMETER_BW80 REGISTER) */

/* 2 REG_BCN_PSR_RPT2_8197F (BEACON PARSER REPORT REGISTER2) */

#define BIT_SHIFT_DTIM_CNT2_8197F 24
#define BIT_MASK_DTIM_CNT2_8197F 0xff
#define BIT_DTIM_CNT2_8197F(x)                                                 \
	(((x) & BIT_MASK_DTIM_CNT2_8197F) << BIT_SHIFT_DTIM_CNT2_8197F)
#define BITS_DTIM_CNT2_8197F                                                   \
	(BIT_MASK_DTIM_CNT2_8197F << BIT_SHIFT_DTIM_CNT2_8197F)
#define BIT_CLEAR_DTIM_CNT2_8197F(x) ((x) & (~BITS_DTIM_CNT2_8197F))
#define BIT_GET_DTIM_CNT2_8197F(x)                                             \
	(((x) >> BIT_SHIFT_DTIM_CNT2_8197F) & BIT_MASK_DTIM_CNT2_8197F)
#define BIT_SET_DTIM_CNT2_8197F(x, v)                                          \
	(BIT_CLEAR_DTIM_CNT2_8197F(x) | BIT_DTIM_CNT2_8197F(v))

#define BIT_SHIFT_DTIM_PERIOD2_8197F 16
#define BIT_MASK_DTIM_PERIOD2_8197F 0xff
#define BIT_DTIM_PERIOD2_8197F(x)                                              \
	(((x) & BIT_MASK_DTIM_PERIOD2_8197F) << BIT_SHIFT_DTIM_PERIOD2_8197F)
#define BITS_DTIM_PERIOD2_8197F                                                \
	(BIT_MASK_DTIM_PERIOD2_8197F << BIT_SHIFT_DTIM_PERIOD2_8197F)
#define BIT_CLEAR_DTIM_PERIOD2_8197F(x) ((x) & (~BITS_DTIM_PERIOD2_8197F))
#define BIT_GET_DTIM_PERIOD2_8197F(x)                                          \
	(((x) >> BIT_SHIFT_DTIM_PERIOD2_8197F) & BIT_MASK_DTIM_PERIOD2_8197F)
#define BIT_SET_DTIM_PERIOD2_8197F(x, v)                                       \
	(BIT_CLEAR_DTIM_PERIOD2_8197F(x) | BIT_DTIM_PERIOD2_8197F(v))

#define BIT_DTIM2_8197F BIT(15)
#define BIT_TIM2_8197F BIT(14)

#define BIT_SHIFT_PS_AID_2_8197F 0
#define BIT_MASK_PS_AID_2_8197F 0x7ff
#define BIT_PS_AID_2_8197F(x)                                                  \
	(((x) & BIT_MASK_PS_AID_2_8197F) << BIT_SHIFT_PS_AID_2_8197F)
#define BITS_PS_AID_2_8197F                                                    \
	(BIT_MASK_PS_AID_2_8197F << BIT_SHIFT_PS_AID_2_8197F)
#define BIT_CLEAR_PS_AID_2_8197F(x) ((x) & (~BITS_PS_AID_2_8197F))
#define BIT_GET_PS_AID_2_8197F(x)                                              \
	(((x) >> BIT_SHIFT_PS_AID_2_8197F) & BIT_MASK_PS_AID_2_8197F)
#define BIT_SET_PS_AID_2_8197F(x, v)                                           \
	(BIT_CLEAR_PS_AID_2_8197F(x) | BIT_PS_AID_2_8197F(v))

/* 2 REG_BCN_PSR_RPT3_8197F (BEACON PARSER REPORT REGISTER3) */

#define BIT_SHIFT_DTIM_CNT3_8197F 24
#define BIT_MASK_DTIM_CNT3_8197F 0xff
#define BIT_DTIM_CNT3_8197F(x)                                                 \
	(((x) & BIT_MASK_DTIM_CNT3_8197F) << BIT_SHIFT_DTIM_CNT3_8197F)
#define BITS_DTIM_CNT3_8197F                                                   \
	(BIT_MASK_DTIM_CNT3_8197F << BIT_SHIFT_DTIM_CNT3_8197F)
#define BIT_CLEAR_DTIM_CNT3_8197F(x) ((x) & (~BITS_DTIM_CNT3_8197F))
#define BIT_GET_DTIM_CNT3_8197F(x)                                             \
	(((x) >> BIT_SHIFT_DTIM_CNT3_8197F) & BIT_MASK_DTIM_CNT3_8197F)
#define BIT_SET_DTIM_CNT3_8197F(x, v)                                          \
	(BIT_CLEAR_DTIM_CNT3_8197F(x) | BIT_DTIM_CNT3_8197F(v))

#define BIT_SHIFT_DTIM_PERIOD3_8197F 16
#define BIT_MASK_DTIM_PERIOD3_8197F 0xff
#define BIT_DTIM_PERIOD3_8197F(x)                                              \
	(((x) & BIT_MASK_DTIM_PERIOD3_8197F) << BIT_SHIFT_DTIM_PERIOD3_8197F)
#define BITS_DTIM_PERIOD3_8197F                                                \
	(BIT_MASK_DTIM_PERIOD3_8197F << BIT_SHIFT_DTIM_PERIOD3_8197F)
#define BIT_CLEAR_DTIM_PERIOD3_8197F(x) ((x) & (~BITS_DTIM_PERIOD3_8197F))
#define BIT_GET_DTIM_PERIOD3_8197F(x)                                          \
	(((x) >> BIT_SHIFT_DTIM_PERIOD3_8197F) & BIT_MASK_DTIM_PERIOD3_8197F)
#define BIT_SET_DTIM_PERIOD3_8197F(x, v)                                       \
	(BIT_CLEAR_DTIM_PERIOD3_8197F(x) | BIT_DTIM_PERIOD3_8197F(v))

#define BIT_DTIM3_8197F BIT(15)
#define BIT_TIM3_8197F BIT(14)

#define BIT_SHIFT_PS_AID_3_8197F 0
#define BIT_MASK_PS_AID_3_8197F 0x7ff
#define BIT_PS_AID_3_8197F(x)                                                  \
	(((x) & BIT_MASK_PS_AID_3_8197F) << BIT_SHIFT_PS_AID_3_8197F)
#define BITS_PS_AID_3_8197F                                                    \
	(BIT_MASK_PS_AID_3_8197F << BIT_SHIFT_PS_AID_3_8197F)
#define BIT_CLEAR_PS_AID_3_8197F(x) ((x) & (~BITS_PS_AID_3_8197F))
#define BIT_GET_PS_AID_3_8197F(x)                                              \
	(((x) >> BIT_SHIFT_PS_AID_3_8197F) & BIT_MASK_PS_AID_3_8197F)
#define BIT_SET_PS_AID_3_8197F(x, v)                                           \
	(BIT_CLEAR_PS_AID_3_8197F(x) | BIT_PS_AID_3_8197F(v))

/* 2 REG_BCN_PSR_RPT4_8197F (BEACON PARSER REPORT REGISTER4) */

#define BIT_SHIFT_DTIM_CNT4_8197F 24
#define BIT_MASK_DTIM_CNT4_8197F 0xff
#define BIT_DTIM_CNT4_8197F(x)                                                 \
	(((x) & BIT_MASK_DTIM_CNT4_8197F) << BIT_SHIFT_DTIM_CNT4_8197F)
#define BITS_DTIM_CNT4_8197F                                                   \
	(BIT_MASK_DTIM_CNT4_8197F << BIT_SHIFT_DTIM_CNT4_8197F)
#define BIT_CLEAR_DTIM_CNT4_8197F(x) ((x) & (~BITS_DTIM_CNT4_8197F))
#define BIT_GET_DTIM_CNT4_8197F(x)                                             \
	(((x) >> BIT_SHIFT_DTIM_CNT4_8197F) & BIT_MASK_DTIM_CNT4_8197F)
#define BIT_SET_DTIM_CNT4_8197F(x, v)                                          \
	(BIT_CLEAR_DTIM_CNT4_8197F(x) | BIT_DTIM_CNT4_8197F(v))

#define BIT_SHIFT_DTIM_PERIOD4_8197F 16
#define BIT_MASK_DTIM_PERIOD4_8197F 0xff
#define BIT_DTIM_PERIOD4_8197F(x)                                              \
	(((x) & BIT_MASK_DTIM_PERIOD4_8197F) << BIT_SHIFT_DTIM_PERIOD4_8197F)
#define BITS_DTIM_PERIOD4_8197F                                                \
	(BIT_MASK_DTIM_PERIOD4_8197F << BIT_SHIFT_DTIM_PERIOD4_8197F)
#define BIT_CLEAR_DTIM_PERIOD4_8197F(x) ((x) & (~BITS_DTIM_PERIOD4_8197F))
#define BIT_GET_DTIM_PERIOD4_8197F(x)                                          \
	(((x) >> BIT_SHIFT_DTIM_PERIOD4_8197F) & BIT_MASK_DTIM_PERIOD4_8197F)
#define BIT_SET_DTIM_PERIOD4_8197F(x, v)                                       \
	(BIT_CLEAR_DTIM_PERIOD4_8197F(x) | BIT_DTIM_PERIOD4_8197F(v))

#define BIT_DTIM4_8197F BIT(15)
#define BIT_TIM4_8197F BIT(14)

#define BIT_SHIFT_PS_AID_4_8197F 0
#define BIT_MASK_PS_AID_4_8197F 0x7ff
#define BIT_PS_AID_4_8197F(x)                                                  \
	(((x) & BIT_MASK_PS_AID_4_8197F) << BIT_SHIFT_PS_AID_4_8197F)
#define BITS_PS_AID_4_8197F                                                    \
	(BIT_MASK_PS_AID_4_8197F << BIT_SHIFT_PS_AID_4_8197F)
#define BIT_CLEAR_PS_AID_4_8197F(x) ((x) & (~BITS_PS_AID_4_8197F))
#define BIT_GET_PS_AID_4_8197F(x)                                              \
	(((x) >> BIT_SHIFT_PS_AID_4_8197F) & BIT_MASK_PS_AID_4_8197F)
#define BIT_SET_PS_AID_4_8197F(x, v)                                           \
	(BIT_CLEAR_PS_AID_4_8197F(x) | BIT_PS_AID_4_8197F(v))

/* 2 REG_A1_ADDR_MASK_8197F (A1 ADDR MASK REGISTER) */

#define BIT_SHIFT_A1_ADDR_MASK_8197F 0
#define BIT_MASK_A1_ADDR_MASK_8197F 0xffffffffL
#define BIT_A1_ADDR_MASK_8197F(x)                                              \
	(((x) & BIT_MASK_A1_ADDR_MASK_8197F) << BIT_SHIFT_A1_ADDR_MASK_8197F)
#define BITS_A1_ADDR_MASK_8197F                                                \
	(BIT_MASK_A1_ADDR_MASK_8197F << BIT_SHIFT_A1_ADDR_MASK_8197F)
#define BIT_CLEAR_A1_ADDR_MASK_8197F(x) ((x) & (~BITS_A1_ADDR_MASK_8197F))
#define BIT_GET_A1_ADDR_MASK_8197F(x)                                          \
	(((x) >> BIT_SHIFT_A1_ADDR_MASK_8197F) & BIT_MASK_A1_ADDR_MASK_8197F)
#define BIT_SET_A1_ADDR_MASK_8197F(x, v)                                       \
	(BIT_CLEAR_A1_ADDR_MASK_8197F(x) | BIT_A1_ADDR_MASK_8197F(v))

/* 2 REG_MACID2_8197F (MAC ID2 REGISTER) */

#define BIT_SHIFT_MACID2_8197F 0
#define BIT_MASK_MACID2_8197F 0xffffffffffffL
#define BIT_MACID2_8197F(x)                                                    \
	(((x) & BIT_MASK_MACID2_8197F) << BIT_SHIFT_MACID2_8197F)
#define BITS_MACID2_8197F (BIT_MASK_MACID2_8197F << BIT_SHIFT_MACID2_8197F)
#define BIT_CLEAR_MACID2_8197F(x) ((x) & (~BITS_MACID2_8197F))
#define BIT_GET_MACID2_8197F(x)                                                \
	(((x) >> BIT_SHIFT_MACID2_8197F) & BIT_MASK_MACID2_8197F)
#define BIT_SET_MACID2_8197F(x, v)                                             \
	(BIT_CLEAR_MACID2_8197F(x) | BIT_MACID2_8197F(v))

/* 2 REG_BSSID2_8197F (BSSID2 REGISTER) */

#define BIT_SHIFT_BSSID2_8197F 0
#define BIT_MASK_BSSID2_8197F 0xffffffffffffL
#define BIT_BSSID2_8197F(x)                                                    \
	(((x) & BIT_MASK_BSSID2_8197F) << BIT_SHIFT_BSSID2_8197F)
#define BITS_BSSID2_8197F (BIT_MASK_BSSID2_8197F << BIT_SHIFT_BSSID2_8197F)
#define BIT_CLEAR_BSSID2_8197F(x) ((x) & (~BITS_BSSID2_8197F))
#define BIT_GET_BSSID2_8197F(x)                                                \
	(((x) >> BIT_SHIFT_BSSID2_8197F) & BIT_MASK_BSSID2_8197F)
#define BIT_SET_BSSID2_8197F(x, v)                                             \
	(BIT_CLEAR_BSSID2_8197F(x) | BIT_BSSID2_8197F(v))

/* 2 REG_MACID3_8197F (MAC ID3 REGISTER) */

#define BIT_SHIFT_MACID3_8197F 0
#define BIT_MASK_MACID3_8197F 0xffffffffffffL
#define BIT_MACID3_8197F(x)                                                    \
	(((x) & BIT_MASK_MACID3_8197F) << BIT_SHIFT_MACID3_8197F)
#define BITS_MACID3_8197F (BIT_MASK_MACID3_8197F << BIT_SHIFT_MACID3_8197F)
#define BIT_CLEAR_MACID3_8197F(x) ((x) & (~BITS_MACID3_8197F))
#define BIT_GET_MACID3_8197F(x)                                                \
	(((x) >> BIT_SHIFT_MACID3_8197F) & BIT_MASK_MACID3_8197F)
#define BIT_SET_MACID3_8197F(x, v)                                             \
	(BIT_CLEAR_MACID3_8197F(x) | BIT_MACID3_8197F(v))

/* 2 REG_BSSID3_8197F (BSSID3 REGISTER) */

#define BIT_SHIFT_BSSID3_8197F 0
#define BIT_MASK_BSSID3_8197F 0xffffffffffffL
#define BIT_BSSID3_8197F(x)                                                    \
	(((x) & BIT_MASK_BSSID3_8197F) << BIT_SHIFT_BSSID3_8197F)
#define BITS_BSSID3_8197F (BIT_MASK_BSSID3_8197F << BIT_SHIFT_BSSID3_8197F)
#define BIT_CLEAR_BSSID3_8197F(x) ((x) & (~BITS_BSSID3_8197F))
#define BIT_GET_BSSID3_8197F(x)                                                \
	(((x) >> BIT_SHIFT_BSSID3_8197F) & BIT_MASK_BSSID3_8197F)
#define BIT_SET_BSSID3_8197F(x, v)                                             \
	(BIT_CLEAR_BSSID3_8197F(x) | BIT_BSSID3_8197F(v))

/* 2 REG_MACID4_8197F (MAC ID4 REGISTER) */

#define BIT_SHIFT_MACID4_8197F 0
#define BIT_MASK_MACID4_8197F 0xffffffffffffL
#define BIT_MACID4_8197F(x)                                                    \
	(((x) & BIT_MASK_MACID4_8197F) << BIT_SHIFT_MACID4_8197F)
#define BITS_MACID4_8197F (BIT_MASK_MACID4_8197F << BIT_SHIFT_MACID4_8197F)
#define BIT_CLEAR_MACID4_8197F(x) ((x) & (~BITS_MACID4_8197F))
#define BIT_GET_MACID4_8197F(x)                                                \
	(((x) >> BIT_SHIFT_MACID4_8197F) & BIT_MASK_MACID4_8197F)
#define BIT_SET_MACID4_8197F(x, v)                                             \
	(BIT_CLEAR_MACID4_8197F(x) | BIT_MACID4_8197F(v))

/* 2 REG_BSSID4_8197F (BSSID4 REGISTER) */

#define BIT_SHIFT_BSSID4_8197F 0
#define BIT_MASK_BSSID4_8197F 0xffffffffffffL
#define BIT_BSSID4_8197F(x)                                                    \
	(((x) & BIT_MASK_BSSID4_8197F) << BIT_SHIFT_BSSID4_8197F)
#define BITS_BSSID4_8197F (BIT_MASK_BSSID4_8197F << BIT_SHIFT_BSSID4_8197F)
#define BIT_CLEAR_BSSID4_8197F(x) ((x) & (~BITS_BSSID4_8197F))
#define BIT_GET_BSSID4_8197F(x)                                                \
	(((x) >> BIT_SHIFT_BSSID4_8197F) & BIT_MASK_BSSID4_8197F)
#define BIT_SET_BSSID4_8197F(x, v)                                             \
	(BIT_CLEAR_BSSID4_8197F(x) | BIT_BSSID4_8197F(v))

/* 2 REG_NOA_REPORT_8197F */

/* 2 REG_PWRBIT_SETTING_8197F */
#define BIT_CLI3_PWRBIT_OW_EN_8197F BIT(7)
#define BIT_CLI3_PWR_ST_8197F BIT(6)
#define BIT_CLI2_PWRBIT_OW_EN_8197F BIT(5)
#define BIT_CLI2_PWR_ST_8197F BIT(4)
#define BIT_CLI1_PWRBIT_OW_EN_8197F BIT(3)
#define BIT_CLI1_PWR_ST_8197F BIT(2)
#define BIT_CLI0_PWRBIT_OW_EN_8197F BIT(1)
#define BIT_CLI0_PWR_ST_8197F BIT(0)

/* 2 REG_WMAC_MU_BF_OPTION_8197F */
#define BIT_WMAC_RESP_NONSTA1_DIS_8197F BIT(7)
#define BIT_BIT_WMAC_TXMU_ACKPOLICY_EN_8197F BIT(6)

#define BIT_SHIFT_WMAC_TXMU_ACKPOLICY_8197F 4
#define BIT_MASK_WMAC_TXMU_ACKPOLICY_8197F 0x3
#define BIT_WMAC_TXMU_ACKPOLICY_8197F(x)                                       \
	(((x) & BIT_MASK_WMAC_TXMU_ACKPOLICY_8197F)                            \
	 << BIT_SHIFT_WMAC_TXMU_ACKPOLICY_8197F)
#define BITS_WMAC_TXMU_ACKPOLICY_8197F                                         \
	(BIT_MASK_WMAC_TXMU_ACKPOLICY_8197F                                    \
	 << BIT_SHIFT_WMAC_TXMU_ACKPOLICY_8197F)
#define BIT_CLEAR_WMAC_TXMU_ACKPOLICY_8197F(x)                                 \
	((x) & (~BITS_WMAC_TXMU_ACKPOLICY_8197F))
#define BIT_GET_WMAC_TXMU_ACKPOLICY_8197F(x)                                   \
	(((x) >> BIT_SHIFT_WMAC_TXMU_ACKPOLICY_8197F) &                        \
	 BIT_MASK_WMAC_TXMU_ACKPOLICY_8197F)
#define BIT_SET_WMAC_TXMU_ACKPOLICY_8197F(x, v)                                \
	(BIT_CLEAR_WMAC_TXMU_ACKPOLICY_8197F(x) |                              \
	 BIT_WMAC_TXMU_ACKPOLICY_8197F(v))

#define BIT_SHIFT_WMAC_MU_BFEE_PORT_SEL_8197F 1
#define BIT_MASK_WMAC_MU_BFEE_PORT_SEL_8197F 0x7
#define BIT_WMAC_MU_BFEE_PORT_SEL_8197F(x)                                     \
	(((x) & BIT_MASK_WMAC_MU_BFEE_PORT_SEL_8197F)                          \
	 << BIT_SHIFT_WMAC_MU_BFEE_PORT_SEL_8197F)
#define BITS_WMAC_MU_BFEE_PORT_SEL_8197F                                       \
	(BIT_MASK_WMAC_MU_BFEE_PORT_SEL_8197F                                  \
	 << BIT_SHIFT_WMAC_MU_BFEE_PORT_SEL_8197F)
#define BIT_CLEAR_WMAC_MU_BFEE_PORT_SEL_8197F(x)                               \
	((x) & (~BITS_WMAC_MU_BFEE_PORT_SEL_8197F))
#define BIT_GET_WMAC_MU_BFEE_PORT_SEL_8197F(x)                                 \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE_PORT_SEL_8197F) &                      \
	 BIT_MASK_WMAC_MU_BFEE_PORT_SEL_8197F)
#define BIT_SET_WMAC_MU_BFEE_PORT_SEL_8197F(x, v)                              \
	(BIT_CLEAR_WMAC_MU_BFEE_PORT_SEL_8197F(x) |                            \
	 BIT_WMAC_MU_BFEE_PORT_SEL_8197F(v))

#define BIT_WMAC_MU_BFEE_DIS_8197F BIT(0)

/* 2 REG_WMAC_PAUSE_BB_CLR_TH_8197F */

#define BIT_SHIFT_WMAC_PAUSE_BB_CLR_TH_8197F 0
#define BIT_MASK_WMAC_PAUSE_BB_CLR_TH_8197F 0xff
#define BIT_WMAC_PAUSE_BB_CLR_TH_8197F(x)                                      \
	(((x) & BIT_MASK_WMAC_PAUSE_BB_CLR_TH_8197F)                           \
	 << BIT_SHIFT_WMAC_PAUSE_BB_CLR_TH_8197F)
#define BITS_WMAC_PAUSE_BB_CLR_TH_8197F                                        \
	(BIT_MASK_WMAC_PAUSE_BB_CLR_TH_8197F                                   \
	 << BIT_SHIFT_WMAC_PAUSE_BB_CLR_TH_8197F)
#define BIT_CLEAR_WMAC_PAUSE_BB_CLR_TH_8197F(x)                                \
	((x) & (~BITS_WMAC_PAUSE_BB_CLR_TH_8197F))
#define BIT_GET_WMAC_PAUSE_BB_CLR_TH_8197F(x)                                  \
	(((x) >> BIT_SHIFT_WMAC_PAUSE_BB_CLR_TH_8197F) &                       \
	 BIT_MASK_WMAC_PAUSE_BB_CLR_TH_8197F)
#define BIT_SET_WMAC_PAUSE_BB_CLR_TH_8197F(x, v)                               \
	(BIT_CLEAR_WMAC_PAUSE_BB_CLR_TH_8197F(x) |                             \
	 BIT_WMAC_PAUSE_BB_CLR_TH_8197F(v))

/* 2 REG_WMAC_MU_ARB_8197F */
#define BIT_WMAC_ARB_HW_ADAPT_EN_8197F BIT(7)
#define BIT_WMAC_ARB_SW_EN_8197F BIT(6)

#define BIT_SHIFT_WMAC_ARB_SW_STATE_8197F 0
#define BIT_MASK_WMAC_ARB_SW_STATE_8197F 0x3f
#define BIT_WMAC_ARB_SW_STATE_8197F(x)                                         \
	(((x) & BIT_MASK_WMAC_ARB_SW_STATE_8197F)                              \
	 << BIT_SHIFT_WMAC_ARB_SW_STATE_8197F)
#define BITS_WMAC_ARB_SW_STATE_8197F                                           \
	(BIT_MASK_WMAC_ARB_SW_STATE_8197F << BIT_SHIFT_WMAC_ARB_SW_STATE_8197F)
#define BIT_CLEAR_WMAC_ARB_SW_STATE_8197F(x)                                   \
	((x) & (~BITS_WMAC_ARB_SW_STATE_8197F))
#define BIT_GET_WMAC_ARB_SW_STATE_8197F(x)                                     \
	(((x) >> BIT_SHIFT_WMAC_ARB_SW_STATE_8197F) &                          \
	 BIT_MASK_WMAC_ARB_SW_STATE_8197F)
#define BIT_SET_WMAC_ARB_SW_STATE_8197F(x, v)                                  \
	(BIT_CLEAR_WMAC_ARB_SW_STATE_8197F(x) | BIT_WMAC_ARB_SW_STATE_8197F(v))

/* 2 REG_WMAC_MU_OPTION_8197F */

#define BIT_SHIFT_WMAC_MU_DBGSEL_8197F 5
#define BIT_MASK_WMAC_MU_DBGSEL_8197F 0x3
#define BIT_WMAC_MU_DBGSEL_8197F(x)                                            \
	(((x) & BIT_MASK_WMAC_MU_DBGSEL_8197F)                                 \
	 << BIT_SHIFT_WMAC_MU_DBGSEL_8197F)
#define BITS_WMAC_MU_DBGSEL_8197F                                              \
	(BIT_MASK_WMAC_MU_DBGSEL_8197F << BIT_SHIFT_WMAC_MU_DBGSEL_8197F)
#define BIT_CLEAR_WMAC_MU_DBGSEL_8197F(x) ((x) & (~BITS_WMAC_MU_DBGSEL_8197F))
#define BIT_GET_WMAC_MU_DBGSEL_8197F(x)                                        \
	(((x) >> BIT_SHIFT_WMAC_MU_DBGSEL_8197F) &                             \
	 BIT_MASK_WMAC_MU_DBGSEL_8197F)
#define BIT_SET_WMAC_MU_DBGSEL_8197F(x, v)                                     \
	(BIT_CLEAR_WMAC_MU_DBGSEL_8197F(x) | BIT_WMAC_MU_DBGSEL_8197F(v))

#define BIT_SHIFT_WMAC_MU_CPRD_TIMEOUT_8197F 0
#define BIT_MASK_WMAC_MU_CPRD_TIMEOUT_8197F 0x1f
#define BIT_WMAC_MU_CPRD_TIMEOUT_8197F(x)                                      \
	(((x) & BIT_MASK_WMAC_MU_CPRD_TIMEOUT_8197F)                           \
	 << BIT_SHIFT_WMAC_MU_CPRD_TIMEOUT_8197F)
#define BITS_WMAC_MU_CPRD_TIMEOUT_8197F                                        \
	(BIT_MASK_WMAC_MU_CPRD_TIMEOUT_8197F                                   \
	 << BIT_SHIFT_WMAC_MU_CPRD_TIMEOUT_8197F)
#define BIT_CLEAR_WMAC_MU_CPRD_TIMEOUT_8197F(x)                                \
	((x) & (~BITS_WMAC_MU_CPRD_TIMEOUT_8197F))
#define BIT_GET_WMAC_MU_CPRD_TIMEOUT_8197F(x)                                  \
	(((x) >> BIT_SHIFT_WMAC_MU_CPRD_TIMEOUT_8197F) &                       \
	 BIT_MASK_WMAC_MU_CPRD_TIMEOUT_8197F)
#define BIT_SET_WMAC_MU_CPRD_TIMEOUT_8197F(x, v)                               \
	(BIT_CLEAR_WMAC_MU_CPRD_TIMEOUT_8197F(x) |                             \
	 BIT_WMAC_MU_CPRD_TIMEOUT_8197F(v))

/* 2 REG_WMAC_MU_BF_CTL_8197F */
#define BIT_WMAC_INVLD_BFPRT_CHK_8197F BIT(15)
#define BIT_WMAC_RETXBFRPTSEQ_UPD_8197F BIT(14)

#define BIT_SHIFT_WMAC_MU_BFRPTSEG_SEL_8197F 12
#define BIT_MASK_WMAC_MU_BFRPTSEG_SEL_8197F 0x3
#define BIT_WMAC_MU_BFRPTSEG_SEL_8197F(x)                                      \
	(((x) & BIT_MASK_WMAC_MU_BFRPTSEG_SEL_8197F)                           \
	 << BIT_SHIFT_WMAC_MU_BFRPTSEG_SEL_8197F)
#define BITS_WMAC_MU_BFRPTSEG_SEL_8197F                                        \
	(BIT_MASK_WMAC_MU_BFRPTSEG_SEL_8197F                                   \
	 << BIT_SHIFT_WMAC_MU_BFRPTSEG_SEL_8197F)
#define BIT_CLEAR_WMAC_MU_BFRPTSEG_SEL_8197F(x)                                \
	((x) & (~BITS_WMAC_MU_BFRPTSEG_SEL_8197F))
#define BIT_GET_WMAC_MU_BFRPTSEG_SEL_8197F(x)                                  \
	(((x) >> BIT_SHIFT_WMAC_MU_BFRPTSEG_SEL_8197F) &                       \
	 BIT_MASK_WMAC_MU_BFRPTSEG_SEL_8197F)
#define BIT_SET_WMAC_MU_BFRPTSEG_SEL_8197F(x, v)                               \
	(BIT_CLEAR_WMAC_MU_BFRPTSEG_SEL_8197F(x) |                             \
	 BIT_WMAC_MU_BFRPTSEG_SEL_8197F(v))

#define BIT_SHIFT_WMAC_MU_BF_MYAID_8197F 0
#define BIT_MASK_WMAC_MU_BF_MYAID_8197F 0xfff
#define BIT_WMAC_MU_BF_MYAID_8197F(x)                                          \
	(((x) & BIT_MASK_WMAC_MU_BF_MYAID_8197F)                               \
	 << BIT_SHIFT_WMAC_MU_BF_MYAID_8197F)
#define BITS_WMAC_MU_BF_MYAID_8197F                                            \
	(BIT_MASK_WMAC_MU_BF_MYAID_8197F << BIT_SHIFT_WMAC_MU_BF_MYAID_8197F)
#define BIT_CLEAR_WMAC_MU_BF_MYAID_8197F(x)                                    \
	((x) & (~BITS_WMAC_MU_BF_MYAID_8197F))
#define BIT_GET_WMAC_MU_BF_MYAID_8197F(x)                                      \
	(((x) >> BIT_SHIFT_WMAC_MU_BF_MYAID_8197F) &                           \
	 BIT_MASK_WMAC_MU_BF_MYAID_8197F)
#define BIT_SET_WMAC_MU_BF_MYAID_8197F(x, v)                                   \
	(BIT_CLEAR_WMAC_MU_BF_MYAID_8197F(x) | BIT_WMAC_MU_BF_MYAID_8197F(v))

/* 2 REG_WMAC_MU_BFRPT_PARA_8197F */

#define BIT_SHIFT_BFRPT_PARA_USERID_SEL_8197F 12
#define BIT_MASK_BFRPT_PARA_USERID_SEL_8197F 0x7
#define BIT_BFRPT_PARA_USERID_SEL_8197F(x)                                     \
	(((x) & BIT_MASK_BFRPT_PARA_USERID_SEL_8197F)                          \
	 << BIT_SHIFT_BFRPT_PARA_USERID_SEL_8197F)
#define BITS_BFRPT_PARA_USERID_SEL_8197F                                       \
	(BIT_MASK_BFRPT_PARA_USERID_SEL_8197F                                  \
	 << BIT_SHIFT_BFRPT_PARA_USERID_SEL_8197F)
#define BIT_CLEAR_BFRPT_PARA_USERID_SEL_8197F(x)                               \
	((x) & (~BITS_BFRPT_PARA_USERID_SEL_8197F))
#define BIT_GET_BFRPT_PARA_USERID_SEL_8197F(x)                                 \
	(((x) >> BIT_SHIFT_BFRPT_PARA_USERID_SEL_8197F) &                      \
	 BIT_MASK_BFRPT_PARA_USERID_SEL_8197F)
#define BIT_SET_BFRPT_PARA_USERID_SEL_8197F(x, v)                              \
	(BIT_CLEAR_BFRPT_PARA_USERID_SEL_8197F(x) |                            \
	 BIT_BFRPT_PARA_USERID_SEL_8197F(v))

#define BIT_SHIFT_BFRPT_PARA_8197F 0
#define BIT_MASK_BFRPT_PARA_8197F 0xfff
#define BIT_BFRPT_PARA_8197F(x)                                                \
	(((x) & BIT_MASK_BFRPT_PARA_8197F) << BIT_SHIFT_BFRPT_PARA_8197F)
#define BITS_BFRPT_PARA_8197F                                                  \
	(BIT_MASK_BFRPT_PARA_8197F << BIT_SHIFT_BFRPT_PARA_8197F)
#define BIT_CLEAR_BFRPT_PARA_8197F(x) ((x) & (~BITS_BFRPT_PARA_8197F))
#define BIT_GET_BFRPT_PARA_8197F(x)                                            \
	(((x) >> BIT_SHIFT_BFRPT_PARA_8197F) & BIT_MASK_BFRPT_PARA_8197F)
#define BIT_SET_BFRPT_PARA_8197F(x, v)                                         \
	(BIT_CLEAR_BFRPT_PARA_8197F(x) | BIT_BFRPT_PARA_8197F(v))

/* 2 REG_WMAC_ASSOCIATED_MU_BFMEE2_8197F */
#define BIT_STATUS_BFEE2_8197F BIT(10)
#define BIT_WMAC_MU_BFEE2_EN_8197F BIT(9)

#define BIT_SHIFT_WMAC_MU_BFEE2_AID_8197F 0
#define BIT_MASK_WMAC_MU_BFEE2_AID_8197F 0x1ff
#define BIT_WMAC_MU_BFEE2_AID_8197F(x)                                         \
	(((x) & BIT_MASK_WMAC_MU_BFEE2_AID_8197F)                              \
	 << BIT_SHIFT_WMAC_MU_BFEE2_AID_8197F)
#define BITS_WMAC_MU_BFEE2_AID_8197F                                           \
	(BIT_MASK_WMAC_MU_BFEE2_AID_8197F << BIT_SHIFT_WMAC_MU_BFEE2_AID_8197F)
#define BIT_CLEAR_WMAC_MU_BFEE2_AID_8197F(x)                                   \
	((x) & (~BITS_WMAC_MU_BFEE2_AID_8197F))
#define BIT_GET_WMAC_MU_BFEE2_AID_8197F(x)                                     \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE2_AID_8197F) &                          \
	 BIT_MASK_WMAC_MU_BFEE2_AID_8197F)
#define BIT_SET_WMAC_MU_BFEE2_AID_8197F(x, v)                                  \
	(BIT_CLEAR_WMAC_MU_BFEE2_AID_8197F(x) | BIT_WMAC_MU_BFEE2_AID_8197F(v))

/* 2 REG_WMAC_ASSOCIATED_MU_BFMEE3_8197F */
#define BIT_STATUS_BFEE3_8197F BIT(10)
#define BIT_WMAC_MU_BFEE3_EN_8197F BIT(9)

#define BIT_SHIFT_WMAC_MU_BFEE3_AID_8197F 0
#define BIT_MASK_WMAC_MU_BFEE3_AID_8197F 0x1ff
#define BIT_WMAC_MU_BFEE3_AID_8197F(x)                                         \
	(((x) & BIT_MASK_WMAC_MU_BFEE3_AID_8197F)                              \
	 << BIT_SHIFT_WMAC_MU_BFEE3_AID_8197F)
#define BITS_WMAC_MU_BFEE3_AID_8197F                                           \
	(BIT_MASK_WMAC_MU_BFEE3_AID_8197F << BIT_SHIFT_WMAC_MU_BFEE3_AID_8197F)
#define BIT_CLEAR_WMAC_MU_BFEE3_AID_8197F(x)                                   \
	((x) & (~BITS_WMAC_MU_BFEE3_AID_8197F))
#define BIT_GET_WMAC_MU_BFEE3_AID_8197F(x)                                     \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE3_AID_8197F) &                          \
	 BIT_MASK_WMAC_MU_BFEE3_AID_8197F)
#define BIT_SET_WMAC_MU_BFEE3_AID_8197F(x, v)                                  \
	(BIT_CLEAR_WMAC_MU_BFEE3_AID_8197F(x) | BIT_WMAC_MU_BFEE3_AID_8197F(v))

/* 2 REG_WMAC_ASSOCIATED_MU_BFMEE4_8197F */
#define BIT_STATUS_BFEE4_8197F BIT(10)
#define BIT_WMAC_MU_BFEE4_EN_8197F BIT(9)

#define BIT_SHIFT_WMAC_MU_BFEE4_AID_8197F 0
#define BIT_MASK_WMAC_MU_BFEE4_AID_8197F 0x1ff
#define BIT_WMAC_MU_BFEE4_AID_8197F(x)                                         \
	(((x) & BIT_MASK_WMAC_MU_BFEE4_AID_8197F)                              \
	 << BIT_SHIFT_WMAC_MU_BFEE4_AID_8197F)
#define BITS_WMAC_MU_BFEE4_AID_8197F                                           \
	(BIT_MASK_WMAC_MU_BFEE4_AID_8197F << BIT_SHIFT_WMAC_MU_BFEE4_AID_8197F)
#define BIT_CLEAR_WMAC_MU_BFEE4_AID_8197F(x)                                   \
	((x) & (~BITS_WMAC_MU_BFEE4_AID_8197F))
#define BIT_GET_WMAC_MU_BFEE4_AID_8197F(x)                                     \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE4_AID_8197F) &                          \
	 BIT_MASK_WMAC_MU_BFEE4_AID_8197F)
#define BIT_SET_WMAC_MU_BFEE4_AID_8197F(x, v)                                  \
	(BIT_CLEAR_WMAC_MU_BFEE4_AID_8197F(x) | BIT_WMAC_MU_BFEE4_AID_8197F(v))

/* 2 REG_WMAC_ASSOCIATED_MU_BFMEE5_8197F */
#define BIT_STATUS_BFEE5_8197F BIT(10)
#define BIT_WMAC_MU_BFEE5_EN_8197F BIT(9)

#define BIT_SHIFT_WMAC_MU_BFEE5_AID_8197F 0
#define BIT_MASK_WMAC_MU_BFEE5_AID_8197F 0x1ff
#define BIT_WMAC_MU_BFEE5_AID_8197F(x)                                         \
	(((x) & BIT_MASK_WMAC_MU_BFEE5_AID_8197F)                              \
	 << BIT_SHIFT_WMAC_MU_BFEE5_AID_8197F)
#define BITS_WMAC_MU_BFEE5_AID_8197F                                           \
	(BIT_MASK_WMAC_MU_BFEE5_AID_8197F << BIT_SHIFT_WMAC_MU_BFEE5_AID_8197F)
#define BIT_CLEAR_WMAC_MU_BFEE5_AID_8197F(x)                                   \
	((x) & (~BITS_WMAC_MU_BFEE5_AID_8197F))
#define BIT_GET_WMAC_MU_BFEE5_AID_8197F(x)                                     \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE5_AID_8197F) &                          \
	 BIT_MASK_WMAC_MU_BFEE5_AID_8197F)
#define BIT_SET_WMAC_MU_BFEE5_AID_8197F(x, v)                                  \
	(BIT_CLEAR_WMAC_MU_BFEE5_AID_8197F(x) | BIT_WMAC_MU_BFEE5_AID_8197F(v))

/* 2 REG_WMAC_ASSOCIATED_MU_BFMEE6_8197F */
#define BIT_STATUS_BFEE6_8197F BIT(10)
#define BIT_WMAC_MU_BFEE6_EN_8197F BIT(9)

#define BIT_SHIFT_WMAC_MU_BFEE6_AID_8197F 0
#define BIT_MASK_WMAC_MU_BFEE6_AID_8197F 0x1ff
#define BIT_WMAC_MU_BFEE6_AID_8197F(x)                                         \
	(((x) & BIT_MASK_WMAC_MU_BFEE6_AID_8197F)                              \
	 << BIT_SHIFT_WMAC_MU_BFEE6_AID_8197F)
#define BITS_WMAC_MU_BFEE6_AID_8197F                                           \
	(BIT_MASK_WMAC_MU_BFEE6_AID_8197F << BIT_SHIFT_WMAC_MU_BFEE6_AID_8197F)
#define BIT_CLEAR_WMAC_MU_BFEE6_AID_8197F(x)                                   \
	((x) & (~BITS_WMAC_MU_BFEE6_AID_8197F))
#define BIT_GET_WMAC_MU_BFEE6_AID_8197F(x)                                     \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE6_AID_8197F) &                          \
	 BIT_MASK_WMAC_MU_BFEE6_AID_8197F)
#define BIT_SET_WMAC_MU_BFEE6_AID_8197F(x, v)                                  \
	(BIT_CLEAR_WMAC_MU_BFEE6_AID_8197F(x) | BIT_WMAC_MU_BFEE6_AID_8197F(v))

/* 2 REG_WMAC_ASSOCIATED_MU_BFMEE7_8197F */
#define BIT_BIT_STATUS_BFEE4_8197F BIT(10)
#define BIT_WMAC_MU_BFEE7_EN_8197F BIT(9)

#define BIT_SHIFT_WMAC_MU_BFEE7_AID_8197F 0
#define BIT_MASK_WMAC_MU_BFEE7_AID_8197F 0x1ff
#define BIT_WMAC_MU_BFEE7_AID_8197F(x)                                         \
	(((x) & BIT_MASK_WMAC_MU_BFEE7_AID_8197F)                              \
	 << BIT_SHIFT_WMAC_MU_BFEE7_AID_8197F)
#define BITS_WMAC_MU_BFEE7_AID_8197F                                           \
	(BIT_MASK_WMAC_MU_BFEE7_AID_8197F << BIT_SHIFT_WMAC_MU_BFEE7_AID_8197F)
#define BIT_CLEAR_WMAC_MU_BFEE7_AID_8197F(x)                                   \
	((x) & (~BITS_WMAC_MU_BFEE7_AID_8197F))
#define BIT_GET_WMAC_MU_BFEE7_AID_8197F(x)                                     \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE7_AID_8197F) &                          \
	 BIT_MASK_WMAC_MU_BFEE7_AID_8197F)
#define BIT_SET_WMAC_MU_BFEE7_AID_8197F(x, v)                                  \
	(BIT_CLEAR_WMAC_MU_BFEE7_AID_8197F(x) | BIT_WMAC_MU_BFEE7_AID_8197F(v))

/* 2 REG_NOT_VALID_8197F */
#define BIT_RST_ALL_COUNTER_8197F BIT(31)

#define BIT_SHIFT_ABORT_RX_VBON_COUNTER_8197F 16
#define BIT_MASK_ABORT_RX_VBON_COUNTER_8197F 0xff
#define BIT_ABORT_RX_VBON_COUNTER_8197F(x)                                     \
	(((x) & BIT_MASK_ABORT_RX_VBON_COUNTER_8197F)                          \
	 << BIT_SHIFT_ABORT_RX_VBON_COUNTER_8197F)
#define BITS_ABORT_RX_VBON_COUNTER_8197F                                       \
	(BIT_MASK_ABORT_RX_VBON_COUNTER_8197F                                  \
	 << BIT_SHIFT_ABORT_RX_VBON_COUNTER_8197F)
#define BIT_CLEAR_ABORT_RX_VBON_COUNTER_8197F(x)                               \
	((x) & (~BITS_ABORT_RX_VBON_COUNTER_8197F))
#define BIT_GET_ABORT_RX_VBON_COUNTER_8197F(x)                                 \
	(((x) >> BIT_SHIFT_ABORT_RX_VBON_COUNTER_8197F) &                      \
	 BIT_MASK_ABORT_RX_VBON_COUNTER_8197F)
#define BIT_SET_ABORT_RX_VBON_COUNTER_8197F(x, v)                              \
	(BIT_CLEAR_ABORT_RX_VBON_COUNTER_8197F(x) |                            \
	 BIT_ABORT_RX_VBON_COUNTER_8197F(v))

#define BIT_SHIFT_ABORT_RX_RDRDY_COUNTER_8197F 8
#define BIT_MASK_ABORT_RX_RDRDY_COUNTER_8197F 0xff
#define BIT_ABORT_RX_RDRDY_COUNTER_8197F(x)                                    \
	(((x) & BIT_MASK_ABORT_RX_RDRDY_COUNTER_8197F)                         \
	 << BIT_SHIFT_ABORT_RX_RDRDY_COUNTER_8197F)
#define BITS_ABORT_RX_RDRDY_COUNTER_8197F                                      \
	(BIT_MASK_ABORT_RX_RDRDY_COUNTER_8197F                                 \
	 << BIT_SHIFT_ABORT_RX_RDRDY_COUNTER_8197F)
#define BIT_CLEAR_ABORT_RX_RDRDY_COUNTER_8197F(x)                              \
	((x) & (~BITS_ABORT_RX_RDRDY_COUNTER_8197F))
#define BIT_GET_ABORT_RX_RDRDY_COUNTER_8197F(x)                                \
	(((x) >> BIT_SHIFT_ABORT_RX_RDRDY_COUNTER_8197F) &                     \
	 BIT_MASK_ABORT_RX_RDRDY_COUNTER_8197F)
#define BIT_SET_ABORT_RX_RDRDY_COUNTER_8197F(x, v)                             \
	(BIT_CLEAR_ABORT_RX_RDRDY_COUNTER_8197F(x) |                           \
	 BIT_ABORT_RX_RDRDY_COUNTER_8197F(v))

#define BIT_SHIFT_VBON_EARLY_FALLING_COUNTER_8197F 0
#define BIT_MASK_VBON_EARLY_FALLING_COUNTER_8197F 0xff
#define BIT_VBON_EARLY_FALLING_COUNTER_8197F(x)                                \
	(((x) & BIT_MASK_VBON_EARLY_FALLING_COUNTER_8197F)                     \
	 << BIT_SHIFT_VBON_EARLY_FALLING_COUNTER_8197F)
#define BITS_VBON_EARLY_FALLING_COUNTER_8197F                                  \
	(BIT_MASK_VBON_EARLY_FALLING_COUNTER_8197F                             \
	 << BIT_SHIFT_VBON_EARLY_FALLING_COUNTER_8197F)
#define BIT_CLEAR_VBON_EARLY_FALLING_COUNTER_8197F(x)                          \
	((x) & (~BITS_VBON_EARLY_FALLING_COUNTER_8197F))
#define BIT_GET_VBON_EARLY_FALLING_COUNTER_8197F(x)                            \
	(((x) >> BIT_SHIFT_VBON_EARLY_FALLING_COUNTER_8197F) &                 \
	 BIT_MASK_VBON_EARLY_FALLING_COUNTER_8197F)
#define BIT_SET_VBON_EARLY_FALLING_COUNTER_8197F(x, v)                         \
	(BIT_CLEAR_VBON_EARLY_FALLING_COUNTER_8197F(x) |                       \
	 BIT_VBON_EARLY_FALLING_COUNTER_8197F(v))

/* 2 REG_NOT_VALID_8197F */
#define BIT_WMAC_PLCP_TRX_SEL_8197F BIT(31)

#define BIT_SHIFT_WMAC_PLCP_RDSIG_SEL_8197F 28
#define BIT_MASK_WMAC_PLCP_RDSIG_SEL_8197F 0x7
#define BIT_WMAC_PLCP_RDSIG_SEL_8197F(x)                                       \
	(((x) & BIT_MASK_WMAC_PLCP_RDSIG_SEL_8197F)                            \
	 << BIT_SHIFT_WMAC_PLCP_RDSIG_SEL_8197F)
#define BITS_WMAC_PLCP_RDSIG_SEL_8197F                                         \
	(BIT_MASK_WMAC_PLCP_RDSIG_SEL_8197F                                    \
	 << BIT_SHIFT_WMAC_PLCP_RDSIG_SEL_8197F)
#define BIT_CLEAR_WMAC_PLCP_RDSIG_SEL_8197F(x)                                 \
	((x) & (~BITS_WMAC_PLCP_RDSIG_SEL_8197F))
#define BIT_GET_WMAC_PLCP_RDSIG_SEL_8197F(x)                                   \
	(((x) >> BIT_SHIFT_WMAC_PLCP_RDSIG_SEL_8197F) &                        \
	 BIT_MASK_WMAC_PLCP_RDSIG_SEL_8197F)
#define BIT_SET_WMAC_PLCP_RDSIG_SEL_8197F(x, v)                                \
	(BIT_CLEAR_WMAC_PLCP_RDSIG_SEL_8197F(x) |                              \
	 BIT_WMAC_PLCP_RDSIG_SEL_8197F(v))

#define BIT_SHIFT_WMAC_RATE_IDX_8197F 24
#define BIT_MASK_WMAC_RATE_IDX_8197F 0xf
#define BIT_WMAC_RATE_IDX_8197F(x)                                             \
	(((x) & BIT_MASK_WMAC_RATE_IDX_8197F) << BIT_SHIFT_WMAC_RATE_IDX_8197F)
#define BITS_WMAC_RATE_IDX_8197F                                               \
	(BIT_MASK_WMAC_RATE_IDX_8197F << BIT_SHIFT_WMAC_RATE_IDX_8197F)
#define BIT_CLEAR_WMAC_RATE_IDX_8197F(x) ((x) & (~BITS_WMAC_RATE_IDX_8197F))
#define BIT_GET_WMAC_RATE_IDX_8197F(x)                                         \
	(((x) >> BIT_SHIFT_WMAC_RATE_IDX_8197F) & BIT_MASK_WMAC_RATE_IDX_8197F)
#define BIT_SET_WMAC_RATE_IDX_8197F(x, v)                                      \
	(BIT_CLEAR_WMAC_RATE_IDX_8197F(x) | BIT_WMAC_RATE_IDX_8197F(v))

#define BIT_SHIFT_WMAC_PLCP_RDSIG_8197F 0
#define BIT_MASK_WMAC_PLCP_RDSIG_8197F 0xffffff
#define BIT_WMAC_PLCP_RDSIG_8197F(x)                                           \
	(((x) & BIT_MASK_WMAC_PLCP_RDSIG_8197F)                                \
	 << BIT_SHIFT_WMAC_PLCP_RDSIG_8197F)
#define BITS_WMAC_PLCP_RDSIG_8197F                                             \
	(BIT_MASK_WMAC_PLCP_RDSIG_8197F << BIT_SHIFT_WMAC_PLCP_RDSIG_8197F)
#define BIT_CLEAR_WMAC_PLCP_RDSIG_8197F(x) ((x) & (~BITS_WMAC_PLCP_RDSIG_8197F))
#define BIT_GET_WMAC_PLCP_RDSIG_8197F(x)                                       \
	(((x) >> BIT_SHIFT_WMAC_PLCP_RDSIG_8197F) &                            \
	 BIT_MASK_WMAC_PLCP_RDSIG_8197F)
#define BIT_SET_WMAC_PLCP_RDSIG_8197F(x, v)                                    \
	(BIT_CLEAR_WMAC_PLCP_RDSIG_8197F(x) | BIT_WMAC_PLCP_RDSIG_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_TRANSMIT_ADDRSS_0_8197F (TA0 REGISTER) */

#define BIT_SHIFT_TA0_8197F 0
#define BIT_MASK_TA0_8197F 0xffffffffffffL
#define BIT_TA0_8197F(x) (((x) & BIT_MASK_TA0_8197F) << BIT_SHIFT_TA0_8197F)
#define BITS_TA0_8197F (BIT_MASK_TA0_8197F << BIT_SHIFT_TA0_8197F)
#define BIT_CLEAR_TA0_8197F(x) ((x) & (~BITS_TA0_8197F))
#define BIT_GET_TA0_8197F(x) (((x) >> BIT_SHIFT_TA0_8197F) & BIT_MASK_TA0_8197F)
#define BIT_SET_TA0_8197F(x, v) (BIT_CLEAR_TA0_8197F(x) | BIT_TA0_8197F(v))

/* 2 REG_TRANSMIT_ADDRSS_1_8197F (TA1 REGISTER) */

#define BIT_SHIFT_TA1_8197F 0
#define BIT_MASK_TA1_8197F 0xffffffffffffL
#define BIT_TA1_8197F(x) (((x) & BIT_MASK_TA1_8197F) << BIT_SHIFT_TA1_8197F)
#define BITS_TA1_8197F (BIT_MASK_TA1_8197F << BIT_SHIFT_TA1_8197F)
#define BIT_CLEAR_TA1_8197F(x) ((x) & (~BITS_TA1_8197F))
#define BIT_GET_TA1_8197F(x) (((x) >> BIT_SHIFT_TA1_8197F) & BIT_MASK_TA1_8197F)
#define BIT_SET_TA1_8197F(x, v) (BIT_CLEAR_TA1_8197F(x) | BIT_TA1_8197F(v))

/* 2 REG_TRANSMIT_ADDRSS_2_8197F (TA2 REGISTER) */

#define BIT_SHIFT_TA2_8197F 0
#define BIT_MASK_TA2_8197F 0xffffffffffffL
#define BIT_TA2_8197F(x) (((x) & BIT_MASK_TA2_8197F) << BIT_SHIFT_TA2_8197F)
#define BITS_TA2_8197F (BIT_MASK_TA2_8197F << BIT_SHIFT_TA2_8197F)
#define BIT_CLEAR_TA2_8197F(x) ((x) & (~BITS_TA2_8197F))
#define BIT_GET_TA2_8197F(x) (((x) >> BIT_SHIFT_TA2_8197F) & BIT_MASK_TA2_8197F)
#define BIT_SET_TA2_8197F(x, v) (BIT_CLEAR_TA2_8197F(x) | BIT_TA2_8197F(v))

/* 2 REG_TRANSMIT_ADDRSS_3_8197F (TA3 REGISTER) */

#define BIT_SHIFT_TA3_8197F 0
#define BIT_MASK_TA3_8197F 0xffffffffffffL
#define BIT_TA3_8197F(x) (((x) & BIT_MASK_TA3_8197F) << BIT_SHIFT_TA3_8197F)
#define BITS_TA3_8197F (BIT_MASK_TA3_8197F << BIT_SHIFT_TA3_8197F)
#define BIT_CLEAR_TA3_8197F(x) ((x) & (~BITS_TA3_8197F))
#define BIT_GET_TA3_8197F(x) (((x) >> BIT_SHIFT_TA3_8197F) & BIT_MASK_TA3_8197F)
#define BIT_SET_TA3_8197F(x, v) (BIT_CLEAR_TA3_8197F(x) | BIT_TA3_8197F(v))

/* 2 REG_TRANSMIT_ADDRSS_4_8197F (TA4 REGISTER) */

#define BIT_SHIFT_TA4_8197F 0
#define BIT_MASK_TA4_8197F 0xffffffffffffL
#define BIT_TA4_8197F(x) (((x) & BIT_MASK_TA4_8197F) << BIT_SHIFT_TA4_8197F)
#define BITS_TA4_8197F (BIT_MASK_TA4_8197F << BIT_SHIFT_TA4_8197F)
#define BIT_CLEAR_TA4_8197F(x) ((x) & (~BITS_TA4_8197F))
#define BIT_GET_TA4_8197F(x) (((x) >> BIT_SHIFT_TA4_8197F) & BIT_MASK_TA4_8197F)
#define BIT_SET_TA4_8197F(x, v) (BIT_CLEAR_TA4_8197F(x) | BIT_TA4_8197F(v))

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_MACID1_8197F */

#define BIT_SHIFT_MACID1_8197F 0
#define BIT_MASK_MACID1_8197F 0xffffffffffffL
#define BIT_MACID1_8197F(x)                                                    \
	(((x) & BIT_MASK_MACID1_8197F) << BIT_SHIFT_MACID1_8197F)
#define BITS_MACID1_8197F (BIT_MASK_MACID1_8197F << BIT_SHIFT_MACID1_8197F)
#define BIT_CLEAR_MACID1_8197F(x) ((x) & (~BITS_MACID1_8197F))
#define BIT_GET_MACID1_8197F(x)                                                \
	(((x) >> BIT_SHIFT_MACID1_8197F) & BIT_MASK_MACID1_8197F)
#define BIT_SET_MACID1_8197F(x, v)                                             \
	(BIT_CLEAR_MACID1_8197F(x) | BIT_MACID1_8197F(v))

/* 2 REG_BSSID1_8197F */

#define BIT_SHIFT_BSSID1_8197F 0
#define BIT_MASK_BSSID1_8197F 0xffffffffffffL
#define BIT_BSSID1_8197F(x)                                                    \
	(((x) & BIT_MASK_BSSID1_8197F) << BIT_SHIFT_BSSID1_8197F)
#define BITS_BSSID1_8197F (BIT_MASK_BSSID1_8197F << BIT_SHIFT_BSSID1_8197F)
#define BIT_CLEAR_BSSID1_8197F(x) ((x) & (~BITS_BSSID1_8197F))
#define BIT_GET_BSSID1_8197F(x)                                                \
	(((x) >> BIT_SHIFT_BSSID1_8197F) & BIT_MASK_BSSID1_8197F)
#define BIT_SET_BSSID1_8197F(x, v)                                             \
	(BIT_CLEAR_BSSID1_8197F(x) | BIT_BSSID1_8197F(v))

/* 2 REG_BCN_PSR_RPT1_8197F */

#define BIT_SHIFT_DTIM_CNT1_8197F 24
#define BIT_MASK_DTIM_CNT1_8197F 0xff
#define BIT_DTIM_CNT1_8197F(x)                                                 \
	(((x) & BIT_MASK_DTIM_CNT1_8197F) << BIT_SHIFT_DTIM_CNT1_8197F)
#define BITS_DTIM_CNT1_8197F                                                   \
	(BIT_MASK_DTIM_CNT1_8197F << BIT_SHIFT_DTIM_CNT1_8197F)
#define BIT_CLEAR_DTIM_CNT1_8197F(x) ((x) & (~BITS_DTIM_CNT1_8197F))
#define BIT_GET_DTIM_CNT1_8197F(x)                                             \
	(((x) >> BIT_SHIFT_DTIM_CNT1_8197F) & BIT_MASK_DTIM_CNT1_8197F)
#define BIT_SET_DTIM_CNT1_8197F(x, v)                                          \
	(BIT_CLEAR_DTIM_CNT1_8197F(x) | BIT_DTIM_CNT1_8197F(v))

#define BIT_SHIFT_DTIM_PERIOD1_8197F 16
#define BIT_MASK_DTIM_PERIOD1_8197F 0xff
#define BIT_DTIM_PERIOD1_8197F(x)                                              \
	(((x) & BIT_MASK_DTIM_PERIOD1_8197F) << BIT_SHIFT_DTIM_PERIOD1_8197F)
#define BITS_DTIM_PERIOD1_8197F                                                \
	(BIT_MASK_DTIM_PERIOD1_8197F << BIT_SHIFT_DTIM_PERIOD1_8197F)
#define BIT_CLEAR_DTIM_PERIOD1_8197F(x) ((x) & (~BITS_DTIM_PERIOD1_8197F))
#define BIT_GET_DTIM_PERIOD1_8197F(x)                                          \
	(((x) >> BIT_SHIFT_DTIM_PERIOD1_8197F) & BIT_MASK_DTIM_PERIOD1_8197F)
#define BIT_SET_DTIM_PERIOD1_8197F(x, v)                                       \
	(BIT_CLEAR_DTIM_PERIOD1_8197F(x) | BIT_DTIM_PERIOD1_8197F(v))

#define BIT_DTIM1_8197F BIT(15)
#define BIT_TIM1_8197F BIT(14)

#define BIT_SHIFT_PS_AID_1_8197F 0
#define BIT_MASK_PS_AID_1_8197F 0x7ff
#define BIT_PS_AID_1_8197F(x)                                                  \
	(((x) & BIT_MASK_PS_AID_1_8197F) << BIT_SHIFT_PS_AID_1_8197F)
#define BITS_PS_AID_1_8197F                                                    \
	(BIT_MASK_PS_AID_1_8197F << BIT_SHIFT_PS_AID_1_8197F)
#define BIT_CLEAR_PS_AID_1_8197F(x) ((x) & (~BITS_PS_AID_1_8197F))
#define BIT_GET_PS_AID_1_8197F(x)                                              \
	(((x) >> BIT_SHIFT_PS_AID_1_8197F) & BIT_MASK_PS_AID_1_8197F)
#define BIT_SET_PS_AID_1_8197F(x, v)                                           \
	(BIT_CLEAR_PS_AID_1_8197F(x) | BIT_PS_AID_1_8197F(v))

/* 2 REG_ASSOCIATED_BFMEE_SEL_8197F */
#define BIT_TXUSER_ID1_8197F BIT(25)

#define BIT_SHIFT_AID1_8197F 16
#define BIT_MASK_AID1_8197F 0x1ff
#define BIT_AID1_8197F(x) (((x) & BIT_MASK_AID1_8197F) << BIT_SHIFT_AID1_8197F)
#define BITS_AID1_8197F (BIT_MASK_AID1_8197F << BIT_SHIFT_AID1_8197F)
#define BIT_CLEAR_AID1_8197F(x) ((x) & (~BITS_AID1_8197F))
#define BIT_GET_AID1_8197F(x)                                                  \
	(((x) >> BIT_SHIFT_AID1_8197F) & BIT_MASK_AID1_8197F)
#define BIT_SET_AID1_8197F(x, v) (BIT_CLEAR_AID1_8197F(x) | BIT_AID1_8197F(v))

#define BIT_TXUSER_ID0_8197F BIT(9)

#define BIT_SHIFT_AID0_8197F 0
#define BIT_MASK_AID0_8197F 0x1ff
#define BIT_AID0_8197F(x) (((x) & BIT_MASK_AID0_8197F) << BIT_SHIFT_AID0_8197F)
#define BITS_AID0_8197F (BIT_MASK_AID0_8197F << BIT_SHIFT_AID0_8197F)
#define BIT_CLEAR_AID0_8197F(x) ((x) & (~BITS_AID0_8197F))
#define BIT_GET_AID0_8197F(x)                                                  \
	(((x) >> BIT_SHIFT_AID0_8197F) & BIT_MASK_AID0_8197F)
#define BIT_SET_AID0_8197F(x, v) (BIT_CLEAR_AID0_8197F(x) | BIT_AID0_8197F(v))

/* 2 REG_SND_PTCL_CTRL_8197F */

#define BIT_SHIFT_NDP_RX_STANDBY_TIMER_8197F 24
#define BIT_MASK_NDP_RX_STANDBY_TIMER_8197F 0xff
#define BIT_NDP_RX_STANDBY_TIMER_8197F(x)                                      \
	(((x) & BIT_MASK_NDP_RX_STANDBY_TIMER_8197F)                           \
	 << BIT_SHIFT_NDP_RX_STANDBY_TIMER_8197F)
#define BITS_NDP_RX_STANDBY_TIMER_8197F                                        \
	(BIT_MASK_NDP_RX_STANDBY_TIMER_8197F                                   \
	 << BIT_SHIFT_NDP_RX_STANDBY_TIMER_8197F)
#define BIT_CLEAR_NDP_RX_STANDBY_TIMER_8197F(x)                                \
	((x) & (~BITS_NDP_RX_STANDBY_TIMER_8197F))
#define BIT_GET_NDP_RX_STANDBY_TIMER_8197F(x)                                  \
	(((x) >> BIT_SHIFT_NDP_RX_STANDBY_TIMER_8197F) &                       \
	 BIT_MASK_NDP_RX_STANDBY_TIMER_8197F)
#define BIT_SET_NDP_RX_STANDBY_TIMER_8197F(x, v)                               \
	(BIT_CLEAR_NDP_RX_STANDBY_TIMER_8197F(x) |                             \
	 BIT_NDP_RX_STANDBY_TIMER_8197F(v))

#define BIT_SHIFT_CSI_RPT_OFFSET_HT_8197F 16
#define BIT_MASK_CSI_RPT_OFFSET_HT_8197F 0xff
#define BIT_CSI_RPT_OFFSET_HT_8197F(x)                                         \
	(((x) & BIT_MASK_CSI_RPT_OFFSET_HT_8197F)                              \
	 << BIT_SHIFT_CSI_RPT_OFFSET_HT_8197F)
#define BITS_CSI_RPT_OFFSET_HT_8197F                                           \
	(BIT_MASK_CSI_RPT_OFFSET_HT_8197F << BIT_SHIFT_CSI_RPT_OFFSET_HT_8197F)
#define BIT_CLEAR_CSI_RPT_OFFSET_HT_8197F(x)                                   \
	((x) & (~BITS_CSI_RPT_OFFSET_HT_8197F))
#define BIT_GET_CSI_RPT_OFFSET_HT_8197F(x)                                     \
	(((x) >> BIT_SHIFT_CSI_RPT_OFFSET_HT_8197F) &                          \
	 BIT_MASK_CSI_RPT_OFFSET_HT_8197F)
#define BIT_SET_CSI_RPT_OFFSET_HT_8197F(x, v)                                  \
	(BIT_CLEAR_CSI_RPT_OFFSET_HT_8197F(x) | BIT_CSI_RPT_OFFSET_HT_8197F(v))

#define BIT_SHIFT_CSI_RPT_OFFSET_VHT_8197F 8
#define BIT_MASK_CSI_RPT_OFFSET_VHT_8197F 0xff
#define BIT_CSI_RPT_OFFSET_VHT_8197F(x)                                        \
	(((x) & BIT_MASK_CSI_RPT_OFFSET_VHT_8197F)                             \
	 << BIT_SHIFT_CSI_RPT_OFFSET_VHT_8197F)
#define BITS_CSI_RPT_OFFSET_VHT_8197F                                          \
	(BIT_MASK_CSI_RPT_OFFSET_VHT_8197F                                     \
	 << BIT_SHIFT_CSI_RPT_OFFSET_VHT_8197F)
#define BIT_CLEAR_CSI_RPT_OFFSET_VHT_8197F(x)                                  \
	((x) & (~BITS_CSI_RPT_OFFSET_VHT_8197F))
#define BIT_GET_CSI_RPT_OFFSET_VHT_8197F(x)                                    \
	(((x) >> BIT_SHIFT_CSI_RPT_OFFSET_VHT_8197F) &                         \
	 BIT_MASK_CSI_RPT_OFFSET_VHT_8197F)
#define BIT_SET_CSI_RPT_OFFSET_VHT_8197F(x, v)                                 \
	(BIT_CLEAR_CSI_RPT_OFFSET_VHT_8197F(x) |                               \
	 BIT_CSI_RPT_OFFSET_VHT_8197F(v))

#define BIT_R_WMAC_USE_NSTS_8197F BIT(7)
#define BIT_R_DISABLE_CHECK_VHTSIGB_CRC_8197F BIT(6)
#define BIT_R_DISABLE_CHECK_VHTSIGA_CRC_8197F BIT(5)
#define BIT_R_WMAC_BFPARAM_SEL_8197F BIT(4)
#define BIT_R_WMAC_CSISEQ_SEL_8197F BIT(3)
#define BIT_R_WMAC_CSI_WITHHTC_EN_8197F BIT(2)
#define BIT_R_WMAC_HT_NDPA_EN_8197F BIT(1)
#define BIT_R_WMAC_VHT_NDPA_EN_8197F BIT(0)

/* 2 REG_RX_CSI_RPT_INFO_8197F */

/* 2 REG_NS_ARP_CTRL_8197F */
#define BIT_R_WMAC_NSARP_RSPEN_8197F BIT(15)
#define BIT_R_WMAC_NSARP_RARP_8197F BIT(9)
#define BIT_R_WMAC_NSARP_RIPV6_8197F BIT(8)

#define BIT_SHIFT_R_WMAC_NSARP_MODEN_8197F 6
#define BIT_MASK_R_WMAC_NSARP_MODEN_8197F 0x3
#define BIT_R_WMAC_NSARP_MODEN_8197F(x)                                        \
	(((x) & BIT_MASK_R_WMAC_NSARP_MODEN_8197F)                             \
	 << BIT_SHIFT_R_WMAC_NSARP_MODEN_8197F)
#define BITS_R_WMAC_NSARP_MODEN_8197F                                          \
	(BIT_MASK_R_WMAC_NSARP_MODEN_8197F                                     \
	 << BIT_SHIFT_R_WMAC_NSARP_MODEN_8197F)
#define BIT_CLEAR_R_WMAC_NSARP_MODEN_8197F(x)                                  \
	((x) & (~BITS_R_WMAC_NSARP_MODEN_8197F))
#define BIT_GET_R_WMAC_NSARP_MODEN_8197F(x)                                    \
	(((x) >> BIT_SHIFT_R_WMAC_NSARP_MODEN_8197F) &                         \
	 BIT_MASK_R_WMAC_NSARP_MODEN_8197F)
#define BIT_SET_R_WMAC_NSARP_MODEN_8197F(x, v)                                 \
	(BIT_CLEAR_R_WMAC_NSARP_MODEN_8197F(x) |                               \
	 BIT_R_WMAC_NSARP_MODEN_8197F(v))

#define BIT_SHIFT_R_WMAC_NSARP_RSPFTP_8197F 4
#define BIT_MASK_R_WMAC_NSARP_RSPFTP_8197F 0x3
#define BIT_R_WMAC_NSARP_RSPFTP_8197F(x)                                       \
	(((x) & BIT_MASK_R_WMAC_NSARP_RSPFTP_8197F)                            \
	 << BIT_SHIFT_R_WMAC_NSARP_RSPFTP_8197F)
#define BITS_R_WMAC_NSARP_RSPFTP_8197F                                         \
	(BIT_MASK_R_WMAC_NSARP_RSPFTP_8197F                                    \
	 << BIT_SHIFT_R_WMAC_NSARP_RSPFTP_8197F)
#define BIT_CLEAR_R_WMAC_NSARP_RSPFTP_8197F(x)                                 \
	((x) & (~BITS_R_WMAC_NSARP_RSPFTP_8197F))
#define BIT_GET_R_WMAC_NSARP_RSPFTP_8197F(x)                                   \
	(((x) >> BIT_SHIFT_R_WMAC_NSARP_RSPFTP_8197F) &                        \
	 BIT_MASK_R_WMAC_NSARP_RSPFTP_8197F)
#define BIT_SET_R_WMAC_NSARP_RSPFTP_8197F(x, v)                                \
	(BIT_CLEAR_R_WMAC_NSARP_RSPFTP_8197F(x) |                              \
	 BIT_R_WMAC_NSARP_RSPFTP_8197F(v))

#define BIT_SHIFT_R_WMAC_NSARP_RSPSEC_8197F 0
#define BIT_MASK_R_WMAC_NSARP_RSPSEC_8197F 0xf
#define BIT_R_WMAC_NSARP_RSPSEC_8197F(x)                                       \
	(((x) & BIT_MASK_R_WMAC_NSARP_RSPSEC_8197F)                            \
	 << BIT_SHIFT_R_WMAC_NSARP_RSPSEC_8197F)
#define BITS_R_WMAC_NSARP_RSPSEC_8197F                                         \
	(BIT_MASK_R_WMAC_NSARP_RSPSEC_8197F                                    \
	 << BIT_SHIFT_R_WMAC_NSARP_RSPSEC_8197F)
#define BIT_CLEAR_R_WMAC_NSARP_RSPSEC_8197F(x)                                 \
	((x) & (~BITS_R_WMAC_NSARP_RSPSEC_8197F))
#define BIT_GET_R_WMAC_NSARP_RSPSEC_8197F(x)                                   \
	(((x) >> BIT_SHIFT_R_WMAC_NSARP_RSPSEC_8197F) &                        \
	 BIT_MASK_R_WMAC_NSARP_RSPSEC_8197F)
#define BIT_SET_R_WMAC_NSARP_RSPSEC_8197F(x, v)                                \
	(BIT_CLEAR_R_WMAC_NSARP_RSPSEC_8197F(x) |                              \
	 BIT_R_WMAC_NSARP_RSPSEC_8197F(v))

/* 2 REG_NS_ARP_INFO_8197F */

/* 2 REG_BEAMFORMING_INFO_NSARP_V1_8197F */

/* 2 REG_BEAMFORMING_INFO_NSARP_8197F */

/* 2 REG_NOT_VALID_8197F */

/* 2 REG_RSVD_0X740_8197F */

/* 2 REG_WMAC_RTX_CTX_SUBTYPE_CFG_8197F */

#define BIT_SHIFT_R_WMAC_CTX_SUBTYPE_8197F 4
#define BIT_MASK_R_WMAC_CTX_SUBTYPE_8197F 0xf
#define BIT_R_WMAC_CTX_SUBTYPE_8197F(x)                                        \
	(((x) & BIT_MASK_R_WMAC_CTX_SUBTYPE_8197F)                             \
	 << BIT_SHIFT_R_WMAC_CTX_SUBTYPE_8197F)
#define BITS_R_WMAC_CTX_SUBTYPE_8197F                                          \
	(BIT_MASK_R_WMAC_CTX_SUBTYPE_8197F                                     \
	 << BIT_SHIFT_R_WMAC_CTX_SUBTYPE_8197F)
#define BIT_CLEAR_R_WMAC_CTX_SUBTYPE_8197F(x)                                  \
	((x) & (~BITS_R_WMAC_CTX_SUBTYPE_8197F))
#define BIT_GET_R_WMAC_CTX_SUBTYPE_8197F(x)                                    \
	(((x) >> BIT_SHIFT_R_WMAC_CTX_SUBTYPE_8197F) &                         \
	 BIT_MASK_R_WMAC_CTX_SUBTYPE_8197F)
#define BIT_SET_R_WMAC_CTX_SUBTYPE_8197F(x, v)                                 \
	(BIT_CLEAR_R_WMAC_CTX_SUBTYPE_8197F(x) |                               \
	 BIT_R_WMAC_CTX_SUBTYPE_8197F(v))

#define BIT_SHIFT_R_WMAC_RTX_SUBTYPE_8197F 0
#define BIT_MASK_R_WMAC_RTX_SUBTYPE_8197F 0xf
#define BIT_R_WMAC_RTX_SUBTYPE_8197F(x)                                        \
	(((x) & BIT_MASK_R_WMAC_RTX_SUBTYPE_8197F)                             \
	 << BIT_SHIFT_R_WMAC_RTX_SUBTYPE_8197F)
#define BITS_R_WMAC_RTX_SUBTYPE_8197F                                          \
	(BIT_MASK_R_WMAC_RTX_SUBTYPE_8197F                                     \
	 << BIT_SHIFT_R_WMAC_RTX_SUBTYPE_8197F)
#define BIT_CLEAR_R_WMAC_RTX_SUBTYPE_8197F(x)                                  \
	((x) & (~BITS_R_WMAC_RTX_SUBTYPE_8197F))
#define BIT_GET_R_WMAC_RTX_SUBTYPE_8197F(x)                                    \
	(((x) >> BIT_SHIFT_R_WMAC_RTX_SUBTYPE_8197F) &                         \
	 BIT_MASK_R_WMAC_RTX_SUBTYPE_8197F)
#define BIT_SET_R_WMAC_RTX_SUBTYPE_8197F(x, v)                                 \
	(BIT_CLEAR_R_WMAC_RTX_SUBTYPE_8197F(x) |                               \
	 BIT_R_WMAC_RTX_SUBTYPE_8197F(v))

/* 2 REG_WMAC_SWAES_CFG_8197F */

/* 2 REG_BT_COEX_V2_8197F */
#define BIT_GNT_BT_POLARITY_8197F BIT(12)
#define BIT_GNT_BT_BYPASS_PRIORITY_8197F BIT(8)

#define BIT_SHIFT_TIMER_8197F 0
#define BIT_MASK_TIMER_8197F 0xff
#define BIT_TIMER_8197F(x)                                                     \
	(((x) & BIT_MASK_TIMER_8197F) << BIT_SHIFT_TIMER_8197F)
#define BITS_TIMER_8197F (BIT_MASK_TIMER_8197F << BIT_SHIFT_TIMER_8197F)
#define BIT_CLEAR_TIMER_8197F(x) ((x) & (~BITS_TIMER_8197F))
#define BIT_GET_TIMER_8197F(x)                                                 \
	(((x) >> BIT_SHIFT_TIMER_8197F) & BIT_MASK_TIMER_8197F)
#define BIT_SET_TIMER_8197F(x, v)                                              \
	(BIT_CLEAR_TIMER_8197F(x) | BIT_TIMER_8197F(v))

/* 2 REG_BT_COEX_8197F */
#define BIT_R_GNT_BT_RFC_SW_8197F BIT(12)
#define BIT_R_GNT_BT_RFC_SW_EN_8197F BIT(11)
#define BIT_R_GNT_BT_BB_SW_8197F BIT(10)
#define BIT_R_GNT_BT_BB_SW_EN_8197F BIT(9)
#define BIT_R_BT_CNT_THREN_8197F BIT(8)

#define BIT_SHIFT_R_BT_CNT_THR_8197F 0
#define BIT_MASK_R_BT_CNT_THR_8197F 0xff
#define BIT_R_BT_CNT_THR_8197F(x)                                              \
	(((x) & BIT_MASK_R_BT_CNT_THR_8197F) << BIT_SHIFT_R_BT_CNT_THR_8197F)
#define BITS_R_BT_CNT_THR_8197F                                                \
	(BIT_MASK_R_BT_CNT_THR_8197F << BIT_SHIFT_R_BT_CNT_THR_8197F)
#define BIT_CLEAR_R_BT_CNT_THR_8197F(x) ((x) & (~BITS_R_BT_CNT_THR_8197F))
#define BIT_GET_R_BT_CNT_THR_8197F(x)                                          \
	(((x) >> BIT_SHIFT_R_BT_CNT_THR_8197F) & BIT_MASK_R_BT_CNT_THR_8197F)
#define BIT_SET_R_BT_CNT_THR_8197F(x, v)                                       \
	(BIT_CLEAR_R_BT_CNT_THR_8197F(x) | BIT_R_BT_CNT_THR_8197F(v))

/* 2 REG_WLAN_ACT_MASK_CTRL_8197F */
#define BIT_WLRX_TER_BY_CTL_8197F BIT(43)
#define BIT_WLRX_TER_BY_AD_8197F BIT(42)
#define BIT_ANT_DIVERSITY_SEL_8197F BIT(41)
#define BIT_ANTSEL_FOR_BT_CTRL_EN_8197F BIT(40)
#define BIT_WLACT_LOW_GNTWL_EN_8197F BIT(34)
#define BIT_WLACT_HIGH_GNTBT_EN_8197F BIT(33)

#define BIT_SHIFT_RXMYRTS_NAV_V1_8197F 8
#define BIT_MASK_RXMYRTS_NAV_V1_8197F 0xff
#define BIT_RXMYRTS_NAV_V1_8197F(x)                                            \
	(((x) & BIT_MASK_RXMYRTS_NAV_V1_8197F)                                 \
	 << BIT_SHIFT_RXMYRTS_NAV_V1_8197F)
#define BITS_RXMYRTS_NAV_V1_8197F                                              \
	(BIT_MASK_RXMYRTS_NAV_V1_8197F << BIT_SHIFT_RXMYRTS_NAV_V1_8197F)
#define BIT_CLEAR_RXMYRTS_NAV_V1_8197F(x) ((x) & (~BITS_RXMYRTS_NAV_V1_8197F))
#define BIT_GET_RXMYRTS_NAV_V1_8197F(x)                                        \
	(((x) >> BIT_SHIFT_RXMYRTS_NAV_V1_8197F) &                             \
	 BIT_MASK_RXMYRTS_NAV_V1_8197F)
#define BIT_SET_RXMYRTS_NAV_V1_8197F(x, v)                                     \
	(BIT_CLEAR_RXMYRTS_NAV_V1_8197F(x) | BIT_RXMYRTS_NAV_V1_8197F(v))

#define BIT_SHIFT_RTSRST_V1_8197F 0
#define BIT_MASK_RTSRST_V1_8197F 0xff
#define BIT_RTSRST_V1_8197F(x)                                                 \
	(((x) & BIT_MASK_RTSRST_V1_8197F) << BIT_SHIFT_RTSRST_V1_8197F)
#define BITS_RTSRST_V1_8197F                                                   \
	(BIT_MASK_RTSRST_V1_8197F << BIT_SHIFT_RTSRST_V1_8197F)
#define BIT_CLEAR_RTSRST_V1_8197F(x) ((x) & (~BITS_RTSRST_V1_8197F))
#define BIT_GET_RTSRST_V1_8197F(x)                                             \
	(((x) >> BIT_SHIFT_RTSRST_V1_8197F) & BIT_MASK_RTSRST_V1_8197F)
#define BIT_SET_RTSRST_V1_8197F(x, v)                                          \
	(BIT_CLEAR_RTSRST_V1_8197F(x) | BIT_RTSRST_V1_8197F(v))

/* 2 REG_BT_COEX_ENHANCED_INTR_CTRL_8197F */

#define BIT_SHIFT_BT_STAT_DELAY_8197F 12
#define BIT_MASK_BT_STAT_DELAY_8197F 0xf
#define BIT_BT_STAT_DELAY_8197F(x)                                             \
	(((x) & BIT_MASK_BT_STAT_DELAY_8197F) << BIT_SHIFT_BT_STAT_DELAY_8197F)
#define BITS_BT_STAT_DELAY_8197F                                               \
	(BIT_MASK_BT_STAT_DELAY_8197F << BIT_SHIFT_BT_STAT_DELAY_8197F)
#define BIT_CLEAR_BT_STAT_DELAY_8197F(x) ((x) & (~BITS_BT_STAT_DELAY_8197F))
#define BIT_GET_BT_STAT_DELAY_8197F(x)                                         \
	(((x) >> BIT_SHIFT_BT_STAT_DELAY_8197F) & BIT_MASK_BT_STAT_DELAY_8197F)
#define BIT_SET_BT_STAT_DELAY_8197F(x, v)                                      \
	(BIT_CLEAR_BT_STAT_DELAY_8197F(x) | BIT_BT_STAT_DELAY_8197F(v))

#define BIT_SHIFT_BT_TRX_INIT_DETECT_8197F 8
#define BIT_MASK_BT_TRX_INIT_DETECT_8197F 0xf
#define BIT_BT_TRX_INIT_DETECT_8197F(x)                                        \
	(((x) & BIT_MASK_BT_TRX_INIT_DETECT_8197F)                             \
	 << BIT_SHIFT_BT_TRX_INIT_DETECT_8197F)
#define BITS_BT_TRX_INIT_DETECT_8197F                                          \
	(BIT_MASK_BT_TRX_INIT_DETECT_8197F                                     \
	 << BIT_SHIFT_BT_TRX_INIT_DETECT_8197F)
#define BIT_CLEAR_BT_TRX_INIT_DETECT_8197F(x)                                  \
	((x) & (~BITS_BT_TRX_INIT_DETECT_8197F))
#define BIT_GET_BT_TRX_INIT_DETECT_8197F(x)                                    \
	(((x) >> BIT_SHIFT_BT_TRX_INIT_DETECT_8197F) &                         \
	 BIT_MASK_BT_TRX_INIT_DETECT_8197F)
#define BIT_SET_BT_TRX_INIT_DETECT_8197F(x, v)                                 \
	(BIT_CLEAR_BT_TRX_INIT_DETECT_8197F(x) |                               \
	 BIT_BT_TRX_INIT_DETECT_8197F(v))

#define BIT_SHIFT_BT_PRI_DETECT_TO_8197F 4
#define BIT_MASK_BT_PRI_DETECT_TO_8197F 0xf
#define BIT_BT_PRI_DETECT_TO_8197F(x)                                          \
	(((x) & BIT_MASK_BT_PRI_DETECT_TO_8197F)                               \
	 << BIT_SHIFT_BT_PRI_DETECT_TO_8197F)
#define BITS_BT_PRI_DETECT_TO_8197F                                            \
	(BIT_MASK_BT_PRI_DETECT_TO_8197F << BIT_SHIFT_BT_PRI_DETECT_TO_8197F)
#define BIT_CLEAR_BT_PRI_DETECT_TO_8197F(x)                                    \
	((x) & (~BITS_BT_PRI_DETECT_TO_8197F))
#define BIT_GET_BT_PRI_DETECT_TO_8197F(x)                                      \
	(((x) >> BIT_SHIFT_BT_PRI_DETECT_TO_8197F) &                           \
	 BIT_MASK_BT_PRI_DETECT_TO_8197F)
#define BIT_SET_BT_PRI_DETECT_TO_8197F(x, v)                                   \
	(BIT_CLEAR_BT_PRI_DETECT_TO_8197F(x) | BIT_BT_PRI_DETECT_TO_8197F(v))

#define BIT_R_GRANTALL_WLMASK_8197F BIT(3)
#define BIT_STATIS_BT_EN_8197F BIT(2)
#define BIT_WL_ACT_MASK_ENABLE_8197F BIT(1)
#define BIT_ENHANCED_BT_8197F BIT(0)

/* 2 REG_BT_ACT_STATISTICS_8197F */

#define BIT_SHIFT_STATIS_BT_LO_RX_8197F (48 & CPU_OPT_WIDTH)
#define BIT_MASK_STATIS_BT_LO_RX_8197F 0xffff
#define BIT_STATIS_BT_LO_RX_8197F(x)                                           \
	(((x) & BIT_MASK_STATIS_BT_LO_RX_8197F)                                \
	 << BIT_SHIFT_STATIS_BT_LO_RX_8197F)
#define BITS_STATIS_BT_LO_RX_8197F                                             \
	(BIT_MASK_STATIS_BT_LO_RX_8197F << BIT_SHIFT_STATIS_BT_LO_RX_8197F)
#define BIT_CLEAR_STATIS_BT_LO_RX_8197F(x) ((x) & (~BITS_STATIS_BT_LO_RX_8197F))
#define BIT_GET_STATIS_BT_LO_RX_8197F(x)                                       \
	(((x) >> BIT_SHIFT_STATIS_BT_LO_RX_8197F) &                            \
	 BIT_MASK_STATIS_BT_LO_RX_8197F)
#define BIT_SET_STATIS_BT_LO_RX_8197F(x, v)                                    \
	(BIT_CLEAR_STATIS_BT_LO_RX_8197F(x) | BIT_STATIS_BT_LO_RX_8197F(v))

#define BIT_SHIFT_STATIS_BT_LO_TX_8197F (32 & CPU_OPT_WIDTH)
#define BIT_MASK_STATIS_BT_LO_TX_8197F 0xffff
#define BIT_STATIS_BT_LO_TX_8197F(x)                                           \
	(((x) & BIT_MASK_STATIS_BT_LO_TX_8197F)                                \
	 << BIT_SHIFT_STATIS_BT_LO_TX_8197F)
#define BITS_STATIS_BT_LO_TX_8197F                                             \
	(BIT_MASK_STATIS_BT_LO_TX_8197F << BIT_SHIFT_STATIS_BT_LO_TX_8197F)
#define BIT_CLEAR_STATIS_BT_LO_TX_8197F(x) ((x) & (~BITS_STATIS_BT_LO_TX_8197F))
#define BIT_GET_STATIS_BT_LO_TX_8197F(x)                                       \
	(((x) >> BIT_SHIFT_STATIS_BT_LO_TX_8197F) &                            \
	 BIT_MASK_STATIS_BT_LO_TX_8197F)
#define BIT_SET_STATIS_BT_LO_TX_8197F(x, v)                                    \
	(BIT_CLEAR_STATIS_BT_LO_TX_8197F(x) | BIT_STATIS_BT_LO_TX_8197F(v))

#define BIT_SHIFT_STATIS_BT_HI_RX_8197F 16
#define BIT_MASK_STATIS_BT_HI_RX_8197F 0xffff
#define BIT_STATIS_BT_HI_RX_8197F(x)                                           \
	(((x) & BIT_MASK_STATIS_BT_HI_RX_8197F)                                \
	 << BIT_SHIFT_STATIS_BT_HI_RX_8197F)
#define BITS_STATIS_BT_HI_RX_8197F                                             \
	(BIT_MASK_STATIS_BT_HI_RX_8197F << BIT_SHIFT_STATIS_BT_HI_RX_8197F)
#define BIT_CLEAR_STATIS_BT_HI_RX_8197F(x) ((x) & (~BITS_STATIS_BT_HI_RX_8197F))
#define BIT_GET_STATIS_BT_HI_RX_8197F(x)                                       \
	(((x) >> BIT_SHIFT_STATIS_BT_HI_RX_8197F) &                            \
	 BIT_MASK_STATIS_BT_HI_RX_8197F)
#define BIT_SET_STATIS_BT_HI_RX_8197F(x, v)                                    \
	(BIT_CLEAR_STATIS_BT_HI_RX_8197F(x) | BIT_STATIS_BT_HI_RX_8197F(v))

#define BIT_SHIFT_STATIS_BT_HI_TX_8197F 0
#define BIT_MASK_STATIS_BT_HI_TX_8197F 0xffff
#define BIT_STATIS_BT_HI_TX_8197F(x)                                           \
	(((x) & BIT_MASK_STATIS_BT_HI_TX_8197F)                                \
	 << BIT_SHIFT_STATIS_BT_HI_TX_8197F)
#define BITS_STATIS_BT_HI_TX_8197F                                             \
	(BIT_MASK_STATIS_BT_HI_TX_8197F << BIT_SHIFT_STATIS_BT_HI_TX_8197F)
#define BIT_CLEAR_STATIS_BT_HI_TX_8197F(x) ((x) & (~BITS_STATIS_BT_HI_TX_8197F))
#define BIT_GET_STATIS_BT_HI_TX_8197F(x)                                       \
	(((x) >> BIT_SHIFT_STATIS_BT_HI_TX_8197F) &                            \
	 BIT_MASK_STATIS_BT_HI_TX_8197F)
#define BIT_SET_STATIS_BT_HI_TX_8197F(x, v)                                    \
	(BIT_CLEAR_STATIS_BT_HI_TX_8197F(x) | BIT_STATIS_BT_HI_TX_8197F(v))

/* 2 REG_BT_STATISTICS_CONTROL_REGISTER_8197F */

#define BIT_SHIFT_R_BT_CMD_RPT_8197F 16
#define BIT_MASK_R_BT_CMD_RPT_8197F 0xffff
#define BIT_R_BT_CMD_RPT_8197F(x)                                              \
	(((x) & BIT_MASK_R_BT_CMD_RPT_8197F) << BIT_SHIFT_R_BT_CMD_RPT_8197F)
#define BITS_R_BT_CMD_RPT_8197F                                                \
	(BIT_MASK_R_BT_CMD_RPT_8197F << BIT_SHIFT_R_BT_CMD_RPT_8197F)
#define BIT_CLEAR_R_BT_CMD_RPT_8197F(x) ((x) & (~BITS_R_BT_CMD_RPT_8197F))
#define BIT_GET_R_BT_CMD_RPT_8197F(x)                                          \
	(((x) >> BIT_SHIFT_R_BT_CMD_RPT_8197F) & BIT_MASK_R_BT_CMD_RPT_8197F)
#define BIT_SET_R_BT_CMD_RPT_8197F(x, v)                                       \
	(BIT_CLEAR_R_BT_CMD_RPT_8197F(x) | BIT_R_BT_CMD_RPT_8197F(v))

#define BIT_SHIFT_R_RPT_FROM_BT_8197F 8
#define BIT_MASK_R_RPT_FROM_BT_8197F 0xff
#define BIT_R_RPT_FROM_BT_8197F(x)                                             \
	(((x) & BIT_MASK_R_RPT_FROM_BT_8197F) << BIT_SHIFT_R_RPT_FROM_BT_8197F)
#define BITS_R_RPT_FROM_BT_8197F                                               \
	(BIT_MASK_R_RPT_FROM_BT_8197F << BIT_SHIFT_R_RPT_FROM_BT_8197F)
#define BIT_CLEAR_R_RPT_FROM_BT_8197F(x) ((x) & (~BITS_R_RPT_FROM_BT_8197F))
#define BIT_GET_R_RPT_FROM_BT_8197F(x)                                         \
	(((x) >> BIT_SHIFT_R_RPT_FROM_BT_8197F) & BIT_MASK_R_RPT_FROM_BT_8197F)
#define BIT_SET_R_RPT_FROM_BT_8197F(x, v)                                      \
	(BIT_CLEAR_R_RPT_FROM_BT_8197F(x) | BIT_R_RPT_FROM_BT_8197F(v))

#define BIT_SHIFT_BT_HID_ISR_SET_8197F 6
#define BIT_MASK_BT_HID_ISR_SET_8197F 0x3
#define BIT_BT_HID_ISR_SET_8197F(x)                                            \
	(((x) & BIT_MASK_BT_HID_ISR_SET_8197F)                                 \
	 << BIT_SHIFT_BT_HID_ISR_SET_8197F)
#define BITS_BT_HID_ISR_SET_8197F                                              \
	(BIT_MASK_BT_HID_ISR_SET_8197F << BIT_SHIFT_BT_HID_ISR_SET_8197F)
#define BIT_CLEAR_BT_HID_ISR_SET_8197F(x) ((x) & (~BITS_BT_HID_ISR_SET_8197F))
#define BIT_GET_BT_HID_ISR_SET_8197F(x)                                        \
	(((x) >> BIT_SHIFT_BT_HID_ISR_SET_8197F) &                             \
	 BIT_MASK_BT_HID_ISR_SET_8197F)
#define BIT_SET_BT_HID_ISR_SET_8197F(x, v)                                     \
	(BIT_CLEAR_BT_HID_ISR_SET_8197F(x) | BIT_BT_HID_ISR_SET_8197F(v))

#define BIT_TDMA_BT_START_NOTIFY_8197F BIT(5)
#define BIT_ENABLE_TDMA_FW_MODE_8197F BIT(4)
#define BIT_ENABLE_PTA_TDMA_MODE_8197F BIT(3)
#define BIT_ENABLE_COEXIST_TAB_IN_TDMA_8197F BIT(2)
#define BIT_GPIO2_GPIO3_EXANGE_OR_NO_BT_CCA_8197F BIT(1)
#define BIT_RTK_BT_ENABLE_8197F BIT(0)

/* 2 REG_BT_STATUS_REPORT_REGISTER_8197F */

#define BIT_SHIFT_BT_PROFILE_8197F 24
#define BIT_MASK_BT_PROFILE_8197F 0xff
#define BIT_BT_PROFILE_8197F(x)                                                \
	(((x) & BIT_MASK_BT_PROFILE_8197F) << BIT_SHIFT_BT_PROFILE_8197F)
#define BITS_BT_PROFILE_8197F                                                  \
	(BIT_MASK_BT_PROFILE_8197F << BIT_SHIFT_BT_PROFILE_8197F)
#define BIT_CLEAR_BT_PROFILE_8197F(x) ((x) & (~BITS_BT_PROFILE_8197F))
#define BIT_GET_BT_PROFILE_8197F(x)                                            \
	(((x) >> BIT_SHIFT_BT_PROFILE_8197F) & BIT_MASK_BT_PROFILE_8197F)
#define BIT_SET_BT_PROFILE_8197F(x, v)                                         \
	(BIT_CLEAR_BT_PROFILE_8197F(x) | BIT_BT_PROFILE_8197F(v))

#define BIT_SHIFT_BT_POWER_8197F 16
#define BIT_MASK_BT_POWER_8197F 0xff
#define BIT_BT_POWER_8197F(x)                                                  \
	(((x) & BIT_MASK_BT_POWER_8197F) << BIT_SHIFT_BT_POWER_8197F)
#define BITS_BT_POWER_8197F                                                    \
	(BIT_MASK_BT_POWER_8197F << BIT_SHIFT_BT_POWER_8197F)
#define BIT_CLEAR_BT_POWER_8197F(x) ((x) & (~BITS_BT_POWER_8197F))
#define BIT_GET_BT_POWER_8197F(x)                                              \
	(((x) >> BIT_SHIFT_BT_POWER_8197F) & BIT_MASK_BT_POWER_8197F)
#define BIT_SET_BT_POWER_8197F(x, v)                                           \
	(BIT_CLEAR_BT_POWER_8197F(x) | BIT_BT_POWER_8197F(v))

#define BIT_SHIFT_BT_PREDECT_STATUS_8197F 8
#define BIT_MASK_BT_PREDECT_STATUS_8197F 0xff
#define BIT_BT_PREDECT_STATUS_8197F(x)                                         \
	(((x) & BIT_MASK_BT_PREDECT_STATUS_8197F)                              \
	 << BIT_SHIFT_BT_PREDECT_STATUS_8197F)
#define BITS_BT_PREDECT_STATUS_8197F                                           \
	(BIT_MASK_BT_PREDECT_STATUS_8197F << BIT_SHIFT_BT_PREDECT_STATUS_8197F)
#define BIT_CLEAR_BT_PREDECT_STATUS_8197F(x)                                   \
	((x) & (~BITS_BT_PREDECT_STATUS_8197F))
#define BIT_GET_BT_PREDECT_STATUS_8197F(x)                                     \
	(((x) >> BIT_SHIFT_BT_PREDECT_STATUS_8197F) &                          \
	 BIT_MASK_BT_PREDECT_STATUS_8197F)
#define BIT_SET_BT_PREDECT_STATUS_8197F(x, v)                                  \
	(BIT_CLEAR_BT_PREDECT_STATUS_8197F(x) | BIT_BT_PREDECT_STATUS_8197F(v))

#define BIT_SHIFT_BT_CMD_INFO_8197F 0
#define BIT_MASK_BT_CMD_INFO_8197F 0xff
#define BIT_BT_CMD_INFO_8197F(x)                                               \
	(((x) & BIT_MASK_BT_CMD_INFO_8197F) << BIT_SHIFT_BT_CMD_INFO_8197F)
#define BITS_BT_CMD_INFO_8197F                                                 \
	(BIT_MASK_BT_CMD_INFO_8197F << BIT_SHIFT_BT_CMD_INFO_8197F)
#define BIT_CLEAR_BT_CMD_INFO_8197F(x) ((x) & (~BITS_BT_CMD_INFO_8197F))
#define BIT_GET_BT_CMD_INFO_8197F(x)                                           \
	(((x) >> BIT_SHIFT_BT_CMD_INFO_8197F) & BIT_MASK_BT_CMD_INFO_8197F)
#define BIT_SET_BT_CMD_INFO_8197F(x, v)                                        \
	(BIT_CLEAR_BT_CMD_INFO_8197F(x) | BIT_BT_CMD_INFO_8197F(v))

/* 2 REG_BT_INTERRUPT_CONTROL_REGISTER_8197F */
#define BIT_EN_MAC_NULL_PKT_NOTIFY_8197F BIT(31)
#define BIT_EN_WLAN_RPT_AND_BT_QUERY_8197F BIT(30)
#define BIT_EN_BT_STSTUS_RPT_8197F BIT(29)
#define BIT_EN_BT_POWER_8197F BIT(28)
#define BIT_EN_BT_CHANNEL_8197F BIT(27)
#define BIT_EN_BT_SLOT_CHANGE_8197F BIT(26)
#define BIT_EN_BT_PROFILE_OR_HID_8197F BIT(25)
#define BIT_WLAN_RPT_NOTIFY_8197F BIT(24)

#define BIT_SHIFT_WLAN_RPT_DATA_8197F 16
#define BIT_MASK_WLAN_RPT_DATA_8197F 0xff
#define BIT_WLAN_RPT_DATA_8197F(x)                                             \
	(((x) & BIT_MASK_WLAN_RPT_DATA_8197F) << BIT_SHIFT_WLAN_RPT_DATA_8197F)
#define BITS_WLAN_RPT_DATA_8197F                                               \
	(BIT_MASK_WLAN_RPT_DATA_8197F << BIT_SHIFT_WLAN_RPT_DATA_8197F)
#define BIT_CLEAR_WLAN_RPT_DATA_8197F(x) ((x) & (~BITS_WLAN_RPT_DATA_8197F))
#define BIT_GET_WLAN_RPT_DATA_8197F(x)                                         \
	(((x) >> BIT_SHIFT_WLAN_RPT_DATA_8197F) & BIT_MASK_WLAN_RPT_DATA_8197F)
#define BIT_SET_WLAN_RPT_DATA_8197F(x, v)                                      \
	(BIT_CLEAR_WLAN_RPT_DATA_8197F(x) | BIT_WLAN_RPT_DATA_8197F(v))

#define BIT_SHIFT_CMD_ID_8197F 8
#define BIT_MASK_CMD_ID_8197F 0xff
#define BIT_CMD_ID_8197F(x)                                                    \
	(((x) & BIT_MASK_CMD_ID_8197F) << BIT_SHIFT_CMD_ID_8197F)
#define BITS_CMD_ID_8197F (BIT_MASK_CMD_ID_8197F << BIT_SHIFT_CMD_ID_8197F)
#define BIT_CLEAR_CMD_ID_8197F(x) ((x) & (~BITS_CMD_ID_8197F))
#define BIT_GET_CMD_ID_8197F(x)                                                \
	(((x) >> BIT_SHIFT_CMD_ID_8197F) & BIT_MASK_CMD_ID_8197F)
#define BIT_SET_CMD_ID_8197F(x, v)                                             \
	(BIT_CLEAR_CMD_ID_8197F(x) | BIT_CMD_ID_8197F(v))

#define BIT_SHIFT_BT_DATA_8197F 0
#define BIT_MASK_BT_DATA_8197F 0xff
#define BIT_BT_DATA_8197F(x)                                                   \
	(((x) & BIT_MASK_BT_DATA_8197F) << BIT_SHIFT_BT_DATA_8197F)
#define BITS_BT_DATA_8197F (BIT_MASK_BT_DATA_8197F << BIT_SHIFT_BT_DATA_8197F)
#define BIT_CLEAR_BT_DATA_8197F(x) ((x) & (~BITS_BT_DATA_8197F))
#define BIT_GET_BT_DATA_8197F(x)                                               \
	(((x) >> BIT_SHIFT_BT_DATA_8197F) & BIT_MASK_BT_DATA_8197F)
#define BIT_SET_BT_DATA_8197F(x, v)                                            \
	(BIT_CLEAR_BT_DATA_8197F(x) | BIT_BT_DATA_8197F(v))

/* 2 REG_WLAN_REPORT_TIME_OUT_CONTROL_REGISTER_8197F */

#define BIT_SHIFT_WLAN_RPT_TO_8197F 0
#define BIT_MASK_WLAN_RPT_TO_8197F 0xff
#define BIT_WLAN_RPT_TO_8197F(x)                                               \
	(((x) & BIT_MASK_WLAN_RPT_TO_8197F) << BIT_SHIFT_WLAN_RPT_TO_8197F)
#define BITS_WLAN_RPT_TO_8197F                                                 \
	(BIT_MASK_WLAN_RPT_TO_8197F << BIT_SHIFT_WLAN_RPT_TO_8197F)
#define BIT_CLEAR_WLAN_RPT_TO_8197F(x) ((x) & (~BITS_WLAN_RPT_TO_8197F))
#define BIT_GET_WLAN_RPT_TO_8197F(x)                                           \
	(((x) >> BIT_SHIFT_WLAN_RPT_TO_8197F) & BIT_MASK_WLAN_RPT_TO_8197F)
#define BIT_SET_WLAN_RPT_TO_8197F(x, v)                                        \
	(BIT_CLEAR_WLAN_RPT_TO_8197F(x) | BIT_WLAN_RPT_TO_8197F(v))

/* 2 REG_BT_ISOLATION_TABLE_REGISTER_REGISTER_8197F */

#define BIT_SHIFT_ISOLATION_CHK_8197F 1
#define BIT_MASK_ISOLATION_CHK_8197F 0x7fffffffffffffffffffL
#define BIT_ISOLATION_CHK_8197F(x)                                             \
	(((x) & BIT_MASK_ISOLATION_CHK_8197F) << BIT_SHIFT_ISOLATION_CHK_8197F)
#define BITS_ISOLATION_CHK_8197F                                               \
	(BIT_MASK_ISOLATION_CHK_8197F << BIT_SHIFT_ISOLATION_CHK_8197F)
#define BIT_CLEAR_ISOLATION_CHK_8197F(x) ((x) & (~BITS_ISOLATION_CHK_8197F))
#define BIT_GET_ISOLATION_CHK_8197F(x)                                         \
	(((x) >> BIT_SHIFT_ISOLATION_CHK_8197F) & BIT_MASK_ISOLATION_CHK_8197F)
#define BIT_SET_ISOLATION_CHK_8197F(x, v)                                      \
	(BIT_CLEAR_ISOLATION_CHK_8197F(x) | BIT_ISOLATION_CHK_8197F(v))

#define BIT_ISOLATION_EN_8197F BIT(0)

/* 2 REG_BT_INTERRUPT_STATUS_REGISTER_8197F */
#define BIT_BT_HID_ISR_8197F BIT(7)
#define BIT_BT_QUERY_ISR_8197F BIT(6)
#define BIT_MAC_NULL_PKT_NOTIFY_ISR_8197F BIT(5)
#define BIT_WLAN_RPT_ISR_8197F BIT(4)
#define BIT_BT_POWER_ISR_8197F BIT(3)
#define BIT_BT_CHANNEL_ISR_8197F BIT(2)
#define BIT_BT_SLOT_CHANGE_ISR_8197F BIT(1)
#define BIT_BT_PROFILE_ISR_8197F BIT(0)

/* 2 REG_BT_TDMA_TIME_REGISTER_8197F */

#define BIT_SHIFT_BT_TIME_8197F 6
#define BIT_MASK_BT_TIME_8197F 0x3ffffff
#define BIT_BT_TIME_8197F(x)                                                   \
	(((x) & BIT_MASK_BT_TIME_8197F) << BIT_SHIFT_BT_TIME_8197F)
#define BITS_BT_TIME_8197F (BIT_MASK_BT_TIME_8197F << BIT_SHIFT_BT_TIME_8197F)
#define BIT_CLEAR_BT_TIME_8197F(x) ((x) & (~BITS_BT_TIME_8197F))
#define BIT_GET_BT_TIME_8197F(x)                                               \
	(((x) >> BIT_SHIFT_BT_TIME_8197F) & BIT_MASK_BT_TIME_8197F)
#define BIT_SET_BT_TIME_8197F(x, v)                                            \
	(BIT_CLEAR_BT_TIME_8197F(x) | BIT_BT_TIME_8197F(v))

#define BIT_SHIFT_BT_RPT_SAMPLE_RATE_8197F 0
#define BIT_MASK_BT_RPT_SAMPLE_RATE_8197F 0x3f
#define BIT_BT_RPT_SAMPLE_RATE_8197F(x)                                        \
	(((x) & BIT_MASK_BT_RPT_SAMPLE_RATE_8197F)                             \
	 << BIT_SHIFT_BT_RPT_SAMPLE_RATE_8197F)
#define BITS_BT_RPT_SAMPLE_RATE_8197F                                          \
	(BIT_MASK_BT_RPT_SAMPLE_RATE_8197F                                     \
	 << BIT_SHIFT_BT_RPT_SAMPLE_RATE_8197F)
#define BIT_CLEAR_BT_RPT_SAMPLE_RATE_8197F(x)                                  \
	((x) & (~BITS_BT_RPT_SAMPLE_RATE_8197F))
#define BIT_GET_BT_RPT_SAMPLE_RATE_8197F(x)                                    \
	(((x) >> BIT_SHIFT_BT_RPT_SAMPLE_RATE_8197F) &                         \
	 BIT_MASK_BT_RPT_SAMPLE_RATE_8197F)
#define BIT_SET_BT_RPT_SAMPLE_RATE_8197F(x, v)                                 \
	(BIT_CLEAR_BT_RPT_SAMPLE_RATE_8197F(x) |                               \
	 BIT_BT_RPT_SAMPLE_RATE_8197F(v))

/* 2 REG_BT_ACT_REGISTER_8197F */

#define BIT_SHIFT_BT_EISR_EN_8197F 16
#define BIT_MASK_BT_EISR_EN_8197F 0xff
#define BIT_BT_EISR_EN_8197F(x)                                                \
	(((x) & BIT_MASK_BT_EISR_EN_8197F) << BIT_SHIFT_BT_EISR_EN_8197F)
#define BITS_BT_EISR_EN_8197F                                                  \
	(BIT_MASK_BT_EISR_EN_8197F << BIT_SHIFT_BT_EISR_EN_8197F)
#define BIT_CLEAR_BT_EISR_EN_8197F(x) ((x) & (~BITS_BT_EISR_EN_8197F))
#define BIT_GET_BT_EISR_EN_8197F(x)                                            \
	(((x) >> BIT_SHIFT_BT_EISR_EN_8197F) & BIT_MASK_BT_EISR_EN_8197F)
#define BIT_SET_BT_EISR_EN_8197F(x, v)                                         \
	(BIT_CLEAR_BT_EISR_EN_8197F(x) | BIT_BT_EISR_EN_8197F(v))

#define BIT_BT_ACT_FALLING_ISR_8197F BIT(10)
#define BIT_BT_ACT_RISING_ISR_8197F BIT(9)
#define BIT_TDMA_TO_ISR_8197F BIT(8)

#define BIT_SHIFT_BT_CH_8197F 0
#define BIT_MASK_BT_CH_8197F 0xff
#define BIT_BT_CH_8197F(x)                                                     \
	(((x) & BIT_MASK_BT_CH_8197F) << BIT_SHIFT_BT_CH_8197F)
#define BITS_BT_CH_8197F (BIT_MASK_BT_CH_8197F << BIT_SHIFT_BT_CH_8197F)
#define BIT_CLEAR_BT_CH_8197F(x) ((x) & (~BITS_BT_CH_8197F))
#define BIT_GET_BT_CH_8197F(x)                                                 \
	(((x) >> BIT_SHIFT_BT_CH_8197F) & BIT_MASK_BT_CH_8197F)
#define BIT_SET_BT_CH_8197F(x, v)                                              \
	(BIT_CLEAR_BT_CH_8197F(x) | BIT_BT_CH_8197F(v))

/* 2 REG_OBFF_CTRL_BASIC_8197F */
#define BIT_OBFF_EN_V1_8197F BIT(31)

#define BIT_SHIFT_OBFF_STATE_V1_8197F 28
#define BIT_MASK_OBFF_STATE_V1_8197F 0x3
#define BIT_OBFF_STATE_V1_8197F(x)                                             \
	(((x) & BIT_MASK_OBFF_STATE_V1_8197F) << BIT_SHIFT_OBFF_STATE_V1_8197F)
#define BITS_OBFF_STATE_V1_8197F                                               \
	(BIT_MASK_OBFF_STATE_V1_8197F << BIT_SHIFT_OBFF_STATE_V1_8197F)
#define BIT_CLEAR_OBFF_STATE_V1_8197F(x) ((x) & (~BITS_OBFF_STATE_V1_8197F))
#define BIT_GET_OBFF_STATE_V1_8197F(x)                                         \
	(((x) >> BIT_SHIFT_OBFF_STATE_V1_8197F) & BIT_MASK_OBFF_STATE_V1_8197F)
#define BIT_SET_OBFF_STATE_V1_8197F(x, v)                                      \
	(BIT_CLEAR_OBFF_STATE_V1_8197F(x) | BIT_OBFF_STATE_V1_8197F(v))

#define BIT_OBFF_ACT_RXDMA_EN_8197F BIT(27)
#define BIT_OBFF_BLOCK_INT_EN_8197F BIT(26)
#define BIT_OBFF_AUTOACT_EN_8197F BIT(25)
#define BIT_OBFF_AUTOIDLE_EN_8197F BIT(24)

#define BIT_SHIFT_WAKE_MAX_PLS_8197F 20
#define BIT_MASK_WAKE_MAX_PLS_8197F 0x7
#define BIT_WAKE_MAX_PLS_8197F(x)                                              \
	(((x) & BIT_MASK_WAKE_MAX_PLS_8197F) << BIT_SHIFT_WAKE_MAX_PLS_8197F)
#define BITS_WAKE_MAX_PLS_8197F                                                \
	(BIT_MASK_WAKE_MAX_PLS_8197F << BIT_SHIFT_WAKE_MAX_PLS_8197F)
#define BIT_CLEAR_WAKE_MAX_PLS_8197F(x) ((x) & (~BITS_WAKE_MAX_PLS_8197F))
#define BIT_GET_WAKE_MAX_PLS_8197F(x)                                          \
	(((x) >> BIT_SHIFT_WAKE_MAX_PLS_8197F) & BIT_MASK_WAKE_MAX_PLS_8197F)
#define BIT_SET_WAKE_MAX_PLS_8197F(x, v)                                       \
	(BIT_CLEAR_WAKE_MAX_PLS_8197F(x) | BIT_WAKE_MAX_PLS_8197F(v))

#define BIT_SHIFT_WAKE_MIN_PLS_8197F 16
#define BIT_MASK_WAKE_MIN_PLS_8197F 0x7
#define BIT_WAKE_MIN_PLS_8197F(x)                                              \
	(((x) & BIT_MASK_WAKE_MIN_PLS_8197F) << BIT_SHIFT_WAKE_MIN_PLS_8197F)
#define BITS_WAKE_MIN_PLS_8197F                                                \
	(BIT_MASK_WAKE_MIN_PLS_8197F << BIT_SHIFT_WAKE_MIN_PLS_8197F)
#define BIT_CLEAR_WAKE_MIN_PLS_8197F(x) ((x) & (~BITS_WAKE_MIN_PLS_8197F))
#define BIT_GET_WAKE_MIN_PLS_8197F(x)                                          \
	(((x) >> BIT_SHIFT_WAKE_MIN_PLS_8197F) & BIT_MASK_WAKE_MIN_PLS_8197F)
#define BIT_SET_WAKE_MIN_PLS_8197F(x, v)                                       \
	(BIT_CLEAR_WAKE_MIN_PLS_8197F(x) | BIT_WAKE_MIN_PLS_8197F(v))

#define BIT_SHIFT_WAKE_MAX_F2F_8197F 12
#define BIT_MASK_WAKE_MAX_F2F_8197F 0x7
#define BIT_WAKE_MAX_F2F_8197F(x)                                              \
	(((x) & BIT_MASK_WAKE_MAX_F2F_8197F) << BIT_SHIFT_WAKE_MAX_F2F_8197F)
#define BITS_WAKE_MAX_F2F_8197F                                                \
	(BIT_MASK_WAKE_MAX_F2F_8197F << BIT_SHIFT_WAKE_MAX_F2F_8197F)
#define BIT_CLEAR_WAKE_MAX_F2F_8197F(x) ((x) & (~BITS_WAKE_MAX_F2F_8197F))
#define BIT_GET_WAKE_MAX_F2F_8197F(x)                                          \
	(((x) >> BIT_SHIFT_WAKE_MAX_F2F_8197F) & BIT_MASK_WAKE_MAX_F2F_8197F)
#define BIT_SET_WAKE_MAX_F2F_8197F(x, v)                                       \
	(BIT_CLEAR_WAKE_MAX_F2F_8197F(x) | BIT_WAKE_MAX_F2F_8197F(v))

#define BIT_SHIFT_WAKE_MIN_F2F_8197F 8
#define BIT_MASK_WAKE_MIN_F2F_8197F 0x7
#define BIT_WAKE_MIN_F2F_8197F(x)                                              \
	(((x) & BIT_MASK_WAKE_MIN_F2F_8197F) << BIT_SHIFT_WAKE_MIN_F2F_8197F)
#define BITS_WAKE_MIN_F2F_8197F                                                \
	(BIT_MASK_WAKE_MIN_F2F_8197F << BIT_SHIFT_WAKE_MIN_F2F_8197F)
#define BIT_CLEAR_WAKE_MIN_F2F_8197F(x) ((x) & (~BITS_WAKE_MIN_F2F_8197F))
#define BIT_GET_WAKE_MIN_F2F_8197F(x)                                          \
	(((x) >> BIT_SHIFT_WAKE_MIN_F2F_8197F) & BIT_MASK_WAKE_MIN_F2F_8197F)
#define BIT_SET_WAKE_MIN_F2F_8197F(x, v)                                       \
	(BIT_CLEAR_WAKE_MIN_F2F_8197F(x) | BIT_WAKE_MIN_F2F_8197F(v))

#define BIT_APP_CPU_ACT_V1_8197F BIT(3)
#define BIT_APP_OBFF_V1_8197F BIT(2)
#define BIT_APP_IDLE_V1_8197F BIT(1)
#define BIT_APP_INIT_V1_8197F BIT(0)

/* 2 REG_OBFF_CTRL2_TIMER_8197F */

#define BIT_SHIFT_RX_HIGH_TIMER_IDX_8197F 24
#define BIT_MASK_RX_HIGH_TIMER_IDX_8197F 0x7
#define BIT_RX_HIGH_TIMER_IDX_8197F(x)                                         \
	(((x) & BIT_MASK_RX_HIGH_TIMER_IDX_8197F)                              \
	 << BIT_SHIFT_RX_HIGH_TIMER_IDX_8197F)
#define BITS_RX_HIGH_TIMER_IDX_8197F                                           \
	(BIT_MASK_RX_HIGH_TIMER_IDX_8197F << BIT_SHIFT_RX_HIGH_TIMER_IDX_8197F)
#define BIT_CLEAR_RX_HIGH_TIMER_IDX_8197F(x)                                   \
	((x) & (~BITS_RX_HIGH_TIMER_IDX_8197F))
#define BIT_GET_RX_HIGH_TIMER_IDX_8197F(x)                                     \
	(((x) >> BIT_SHIFT_RX_HIGH_TIMER_IDX_8197F) &                          \
	 BIT_MASK_RX_HIGH_TIMER_IDX_8197F)
#define BIT_SET_RX_HIGH_TIMER_IDX_8197F(x, v)                                  \
	(BIT_CLEAR_RX_HIGH_TIMER_IDX_8197F(x) | BIT_RX_HIGH_TIMER_IDX_8197F(v))

#define BIT_SHIFT_RX_MED_TIMER_IDX_8197F 16
#define BIT_MASK_RX_MED_TIMER_IDX_8197F 0x7
#define BIT_RX_MED_TIMER_IDX_8197F(x)                                          \
	(((x) & BIT_MASK_RX_MED_TIMER_IDX_8197F)                               \
	 << BIT_SHIFT_RX_MED_TIMER_IDX_8197F)
#define BITS_RX_MED_TIMER_IDX_8197F                                            \
	(BIT_MASK_RX_MED_TIMER_IDX_8197F << BIT_SHIFT_RX_MED_TIMER_IDX_8197F)
#define BIT_CLEAR_RX_MED_TIMER_IDX_8197F(x)                                    \
	((x) & (~BITS_RX_MED_TIMER_IDX_8197F))
#define BIT_GET_RX_MED_TIMER_IDX_8197F(x)                                      \
	(((x) >> BIT_SHIFT_RX_MED_TIMER_IDX_8197F) &                           \
	 BIT_MASK_RX_MED_TIMER_IDX_8197F)
#define BIT_SET_RX_MED_TIMER_IDX_8197F(x, v)                                   \
	(BIT_CLEAR_RX_MED_TIMER_IDX_8197F(x) | BIT_RX_MED_TIMER_IDX_8197F(v))

#define BIT_SHIFT_RX_LOW_TIMER_IDX_8197F 8
#define BIT_MASK_RX_LOW_TIMER_IDX_8197F 0x7
#define BIT_RX_LOW_TIMER_IDX_8197F(x)                                          \
	(((x) & BIT_MASK_RX_LOW_TIMER_IDX_8197F)                               \
	 << BIT_SHIFT_RX_LOW_TIMER_IDX_8197F)
#define BITS_RX_LOW_TIMER_IDX_8197F                                            \
	(BIT_MASK_RX_LOW_TIMER_IDX_8197F << BIT_SHIFT_RX_LOW_TIMER_IDX_8197F)
#define BIT_CLEAR_RX_LOW_TIMER_IDX_8197F(x)                                    \
	((x) & (~BITS_RX_LOW_TIMER_IDX_8197F))
#define BIT_GET_RX_LOW_TIMER_IDX_8197F(x)                                      \
	(((x) >> BIT_SHIFT_RX_LOW_TIMER_IDX_8197F) &                           \
	 BIT_MASK_RX_LOW_TIMER_IDX_8197F)
#define BIT_SET_RX_LOW_TIMER_IDX_8197F(x, v)                                   \
	(BIT_CLEAR_RX_LOW_TIMER_IDX_8197F(x) | BIT_RX_LOW_TIMER_IDX_8197F(v))

#define BIT_SHIFT_OBFF_INT_TIMER_IDX_8197F 0
#define BIT_MASK_OBFF_INT_TIMER_IDX_8197F 0x7
#define BIT_OBFF_INT_TIMER_IDX_8197F(x)                                        \
	(((x) & BIT_MASK_OBFF_INT_TIMER_IDX_8197F)                             \
	 << BIT_SHIFT_OBFF_INT_TIMER_IDX_8197F)
#define BITS_OBFF_INT_TIMER_IDX_8197F                                          \
	(BIT_MASK_OBFF_INT_TIMER_IDX_8197F                                     \
	 << BIT_SHIFT_OBFF_INT_TIMER_IDX_8197F)
#define BIT_CLEAR_OBFF_INT_TIMER_IDX_8197F(x)                                  \
	((x) & (~BITS_OBFF_INT_TIMER_IDX_8197F))
#define BIT_GET_OBFF_INT_TIMER_IDX_8197F(x)                                    \
	(((x) >> BIT_SHIFT_OBFF_INT_TIMER_IDX_8197F) &                         \
	 BIT_MASK_OBFF_INT_TIMER_IDX_8197F)
#define BIT_SET_OBFF_INT_TIMER_IDX_8197F(x, v)                                 \
	(BIT_CLEAR_OBFF_INT_TIMER_IDX_8197F(x) |                               \
	 BIT_OBFF_INT_TIMER_IDX_8197F(v))

/* 2 REG_LTR_CTRL_BASIC_8197F */
#define BIT_LTR_EN_V1_8197F BIT(31)
#define BIT_LTR_HW_EN_V1_8197F BIT(30)
#define BIT_LRT_ACT_CTS_EN_8197F BIT(29)
#define BIT_LTR_ACT_RXPKT_EN_8197F BIT(28)
#define BIT_LTR_ACT_RXDMA_EN_8197F BIT(27)
#define BIT_LTR_IDLE_NO_SNOOP_8197F BIT(26)
#define BIT_SPDUP_MGTPKT_8197F BIT(25)
#define BIT_RX_AGG_EN_8197F BIT(24)
#define BIT_APP_LTR_ACT_8197F BIT(23)
#define BIT_APP_LTR_IDLE_8197F BIT(22)

#define BIT_SHIFT_HIGH_RATE_TRIG_SEL_8197F 20
#define BIT_MASK_HIGH_RATE_TRIG_SEL_8197F 0x3
#define BIT_HIGH_RATE_TRIG_SEL_8197F(x)                                        \
	(((x) & BIT_MASK_HIGH_RATE_TRIG_SEL_8197F)                             \
	 << BIT_SHIFT_HIGH_RATE_TRIG_SEL_8197F)
#define BITS_HIGH_RATE_TRIG_SEL_8197F                                          \
	(BIT_MASK_HIGH_RATE_TRIG_SEL_8197F                                     \
	 << BIT_SHIFT_HIGH_RATE_TRIG_SEL_8197F)
#define BIT_CLEAR_HIGH_RATE_TRIG_SEL_8197F(x)                                  \
	((x) & (~BITS_HIGH_RATE_TRIG_SEL_8197F))
#define BIT_GET_HIGH_RATE_TRIG_SEL_8197F(x)                                    \
	(((x) >> BIT_SHIFT_HIGH_RATE_TRIG_SEL_8197F) &                         \
	 BIT_MASK_HIGH_RATE_TRIG_SEL_8197F)
#define BIT_SET_HIGH_RATE_TRIG_SEL_8197F(x, v)                                 \
	(BIT_CLEAR_HIGH_RATE_TRIG_SEL_8197F(x) |                               \
	 BIT_HIGH_RATE_TRIG_SEL_8197F(v))

#define BIT_SHIFT_MED_RATE_TRIG_SEL_8197F 18
#define BIT_MASK_MED_RATE_TRIG_SEL_8197F 0x3
#define BIT_MED_RATE_TRIG_SEL_8197F(x)                                         \
	(((x) & BIT_MASK_MED_RATE_TRIG_SEL_8197F)                              \
	 << BIT_SHIFT_MED_RATE_TRIG_SEL_8197F)
#define BITS_MED_RATE_TRIG_SEL_8197F                                           \
	(BIT_MASK_MED_RATE_TRIG_SEL_8197F << BIT_SHIFT_MED_RATE_TRIG_SEL_8197F)
#define BIT_CLEAR_MED_RATE_TRIG_SEL_8197F(x)                                   \
	((x) & (~BITS_MED_RATE_TRIG_SEL_8197F))
#define BIT_GET_MED_RATE_TRIG_SEL_8197F(x)                                     \
	(((x) >> BIT_SHIFT_MED_RATE_TRIG_SEL_8197F) &                          \
	 BIT_MASK_MED_RATE_TRIG_SEL_8197F)
#define BIT_SET_MED_RATE_TRIG_SEL_8197F(x, v)                                  \
	(BIT_CLEAR_MED_RATE_TRIG_SEL_8197F(x) | BIT_MED_RATE_TRIG_SEL_8197F(v))

#define BIT_SHIFT_LOW_RATE_TRIG_SEL_8197F 16
#define BIT_MASK_LOW_RATE_TRIG_SEL_8197F 0x3
#define BIT_LOW_RATE_TRIG_SEL_8197F(x)                                         \
	(((x) & BIT_MASK_LOW_RATE_TRIG_SEL_8197F)                              \
	 << BIT_SHIFT_LOW_RATE_TRIG_SEL_8197F)
#define BITS_LOW_RATE_TRIG_SEL_8197F                                           \
	(BIT_MASK_LOW_RATE_TRIG_SEL_8197F << BIT_SHIFT_LOW_RATE_TRIG_SEL_8197F)
#define BIT_CLEAR_LOW_RATE_TRIG_SEL_8197F(x)                                   \
	((x) & (~BITS_LOW_RATE_TRIG_SEL_8197F))
#define BIT_GET_LOW_RATE_TRIG_SEL_8197F(x)                                     \
	(((x) >> BIT_SHIFT_LOW_RATE_TRIG_SEL_8197F) &                          \
	 BIT_MASK_LOW_RATE_TRIG_SEL_8197F)
#define BIT_SET_LOW_RATE_TRIG_SEL_8197F(x, v)                                  \
	(BIT_CLEAR_LOW_RATE_TRIG_SEL_8197F(x) | BIT_LOW_RATE_TRIG_SEL_8197F(v))

#define BIT_SHIFT_HIGH_RATE_BD_IDX_8197F 8
#define BIT_MASK_HIGH_RATE_BD_IDX_8197F 0x7f
#define BIT_HIGH_RATE_BD_IDX_8197F(x)                                          \
	(((x) & BIT_MASK_HIGH_RATE_BD_IDX_8197F)                               \
	 << BIT_SHIFT_HIGH_RATE_BD_IDX_8197F)
#define BITS_HIGH_RATE_BD_IDX_8197F                                            \
	(BIT_MASK_HIGH_RATE_BD_IDX_8197F << BIT_SHIFT_HIGH_RATE_BD_IDX_8197F)
#define BIT_CLEAR_HIGH_RATE_BD_IDX_8197F(x)                                    \
	((x) & (~BITS_HIGH_RATE_BD_IDX_8197F))
#define BIT_GET_HIGH_RATE_BD_IDX_8197F(x)                                      \
	(((x) >> BIT_SHIFT_HIGH_RATE_BD_IDX_8197F) &                           \
	 BIT_MASK_HIGH_RATE_BD_IDX_8197F)
#define BIT_SET_HIGH_RATE_BD_IDX_8197F(x, v)                                   \
	(BIT_CLEAR_HIGH_RATE_BD_IDX_8197F(x) | BIT_HIGH_RATE_BD_IDX_8197F(v))

#define BIT_SHIFT_LOW_RATE_BD_IDX_8197F 0
#define BIT_MASK_LOW_RATE_BD_IDX_8197F 0x7f
#define BIT_LOW_RATE_BD_IDX_8197F(x)                                           \
	(((x) & BIT_MASK_LOW_RATE_BD_IDX_8197F)                                \
	 << BIT_SHIFT_LOW_RATE_BD_IDX_8197F)
#define BITS_LOW_RATE_BD_IDX_8197F                                             \
	(BIT_MASK_LOW_RATE_BD_IDX_8197F << BIT_SHIFT_LOW_RATE_BD_IDX_8197F)
#define BIT_CLEAR_LOW_RATE_BD_IDX_8197F(x) ((x) & (~BITS_LOW_RATE_BD_IDX_8197F))
#define BIT_GET_LOW_RATE_BD_IDX_8197F(x)                                       \
	(((x) >> BIT_SHIFT_LOW_RATE_BD_IDX_8197F) &                            \
	 BIT_MASK_LOW_RATE_BD_IDX_8197F)
#define BIT_SET_LOW_RATE_BD_IDX_8197F(x, v)                                    \
	(BIT_CLEAR_LOW_RATE_BD_IDX_8197F(x) | BIT_LOW_RATE_BD_IDX_8197F(v))

/* 2 REG_LTR_CTRL2_TIMER_THRESHOLD_8197F */

#define BIT_SHIFT_RX_EMPTY_TIMER_IDX_8197F 24
#define BIT_MASK_RX_EMPTY_TIMER_IDX_8197F 0x7
#define BIT_RX_EMPTY_TIMER_IDX_8197F(x)                                        \
	(((x) & BIT_MASK_RX_EMPTY_TIMER_IDX_8197F)                             \
	 << BIT_SHIFT_RX_EMPTY_TIMER_IDX_8197F)
#define BITS_RX_EMPTY_TIMER_IDX_8197F                                          \
	(BIT_MASK_RX_EMPTY_TIMER_IDX_8197F                                     \
	 << BIT_SHIFT_RX_EMPTY_TIMER_IDX_8197F)
#define BIT_CLEAR_RX_EMPTY_TIMER_IDX_8197F(x)                                  \
	((x) & (~BITS_RX_EMPTY_TIMER_IDX_8197F))
#define BIT_GET_RX_EMPTY_TIMER_IDX_8197F(x)                                    \
	(((x) >> BIT_SHIFT_RX_EMPTY_TIMER_IDX_8197F) &                         \
	 BIT_MASK_RX_EMPTY_TIMER_IDX_8197F)
#define BIT_SET_RX_EMPTY_TIMER_IDX_8197F(x, v)                                 \
	(BIT_CLEAR_RX_EMPTY_TIMER_IDX_8197F(x) |                               \
	 BIT_RX_EMPTY_TIMER_IDX_8197F(v))

#define BIT_SHIFT_RX_AFULL_TH_IDX_8197F 20
#define BIT_MASK_RX_AFULL_TH_IDX_8197F 0x7
#define BIT_RX_AFULL_TH_IDX_8197F(x)                                           \
	(((x) & BIT_MASK_RX_AFULL_TH_IDX_8197F)                                \
	 << BIT_SHIFT_RX_AFULL_TH_IDX_8197F)
#define BITS_RX_AFULL_TH_IDX_8197F                                             \
	(BIT_MASK_RX_AFULL_TH_IDX_8197F << BIT_SHIFT_RX_AFULL_TH_IDX_8197F)
#define BIT_CLEAR_RX_AFULL_TH_IDX_8197F(x) ((x) & (~BITS_RX_AFULL_TH_IDX_8197F))
#define BIT_GET_RX_AFULL_TH_IDX_8197F(x)                                       \
	(((x) >> BIT_SHIFT_RX_AFULL_TH_IDX_8197F) &                            \
	 BIT_MASK_RX_AFULL_TH_IDX_8197F)
#define BIT_SET_RX_AFULL_TH_IDX_8197F(x, v)                                    \
	(BIT_CLEAR_RX_AFULL_TH_IDX_8197F(x) | BIT_RX_AFULL_TH_IDX_8197F(v))

#define BIT_SHIFT_RX_HIGH_TH_IDX_8197F 16
#define BIT_MASK_RX_HIGH_TH_IDX_8197F 0x7
#define BIT_RX_HIGH_TH_IDX_8197F(x)                                            \
	(((x) & BIT_MASK_RX_HIGH_TH_IDX_8197F)                                 \
	 << BIT_SHIFT_RX_HIGH_TH_IDX_8197F)
#define BITS_RX_HIGH_TH_IDX_8197F                                              \
	(BIT_MASK_RX_HIGH_TH_IDX_8197F << BIT_SHIFT_RX_HIGH_TH_IDX_8197F)
#define BIT_CLEAR_RX_HIGH_TH_IDX_8197F(x) ((x) & (~BITS_RX_HIGH_TH_IDX_8197F))
#define BIT_GET_RX_HIGH_TH_IDX_8197F(x)                                        \
	(((x) >> BIT_SHIFT_RX_HIGH_TH_IDX_8197F) &                             \
	 BIT_MASK_RX_HIGH_TH_IDX_8197F)
#define BIT_SET_RX_HIGH_TH_IDX_8197F(x, v)                                     \
	(BIT_CLEAR_RX_HIGH_TH_IDX_8197F(x) | BIT_RX_HIGH_TH_IDX_8197F(v))

#define BIT_SHIFT_RX_MED_TH_IDX_8197F 12
#define BIT_MASK_RX_MED_TH_IDX_8197F 0x7
#define BIT_RX_MED_TH_IDX_8197F(x)                                             \
	(((x) & BIT_MASK_RX_MED_TH_IDX_8197F) << BIT_SHIFT_RX_MED_TH_IDX_8197F)
#define BITS_RX_MED_TH_IDX_8197F                                               \
	(BIT_MASK_RX_MED_TH_IDX_8197F << BIT_SHIFT_RX_MED_TH_IDX_8197F)
#define BIT_CLEAR_RX_MED_TH_IDX_8197F(x) ((x) & (~BITS_RX_MED_TH_IDX_8197F))
#define BIT_GET_RX_MED_TH_IDX_8197F(x)                                         \
	(((x) >> BIT_SHIFT_RX_MED_TH_IDX_8197F) & BIT_MASK_RX_MED_TH_IDX_8197F)
#define BIT_SET_RX_MED_TH_IDX_8197F(x, v)                                      \
	(BIT_CLEAR_RX_MED_TH_IDX_8197F(x) | BIT_RX_MED_TH_IDX_8197F(v))

#define BIT_SHIFT_RX_LOW_TH_IDX_8197F 8
#define BIT_MASK_RX_LOW_TH_IDX_8197F 0x7
#define BIT_RX_LOW_TH_IDX_8197F(x)                                             \
	(((x) & BIT_MASK_RX_LOW_TH_IDX_8197F) << BIT_SHIFT_RX_LOW_TH_IDX_8197F)
#define BITS_RX_LOW_TH_IDX_8197F                                               \
	(BIT_MASK_RX_LOW_TH_IDX_8197F << BIT_SHIFT_RX_LOW_TH_IDX_8197F)
#define BIT_CLEAR_RX_LOW_TH_IDX_8197F(x) ((x) & (~BITS_RX_LOW_TH_IDX_8197F))
#define BIT_GET_RX_LOW_TH_IDX_8197F(x)                                         \
	(((x) >> BIT_SHIFT_RX_LOW_TH_IDX_8197F) & BIT_MASK_RX_LOW_TH_IDX_8197F)
#define BIT_SET_RX_LOW_TH_IDX_8197F(x, v)                                      \
	(BIT_CLEAR_RX_LOW_TH_IDX_8197F(x) | BIT_RX_LOW_TH_IDX_8197F(v))

#define BIT_SHIFT_LTR_SPACE_IDX_8197F 4
#define BIT_MASK_LTR_SPACE_IDX_8197F 0x3
#define BIT_LTR_SPACE_IDX_8197F(x)                                             \
	(((x) & BIT_MASK_LTR_SPACE_IDX_8197F) << BIT_SHIFT_LTR_SPACE_IDX_8197F)
#define BITS_LTR_SPACE_IDX_8197F                                               \
	(BIT_MASK_LTR_SPACE_IDX_8197F << BIT_SHIFT_LTR_SPACE_IDX_8197F)
#define BIT_CLEAR_LTR_SPACE_IDX_8197F(x) ((x) & (~BITS_LTR_SPACE_IDX_8197F))
#define BIT_GET_LTR_SPACE_IDX_8197F(x)                                         \
	(((x) >> BIT_SHIFT_LTR_SPACE_IDX_8197F) & BIT_MASK_LTR_SPACE_IDX_8197F)
#define BIT_SET_LTR_SPACE_IDX_8197F(x, v)                                      \
	(BIT_CLEAR_LTR_SPACE_IDX_8197F(x) | BIT_LTR_SPACE_IDX_8197F(v))

#define BIT_SHIFT_LTR_IDLE_TIMER_IDX_8197F 0
#define BIT_MASK_LTR_IDLE_TIMER_IDX_8197F 0x7
#define BIT_LTR_IDLE_TIMER_IDX_8197F(x)                                        \
	(((x) & BIT_MASK_LTR_IDLE_TIMER_IDX_8197F)                             \
	 << BIT_SHIFT_LTR_IDLE_TIMER_IDX_8197F)
#define BITS_LTR_IDLE_TIMER_IDX_8197F                                          \
	(BIT_MASK_LTR_IDLE_TIMER_IDX_8197F                                     \
	 << BIT_SHIFT_LTR_IDLE_TIMER_IDX_8197F)
#define BIT_CLEAR_LTR_IDLE_TIMER_IDX_8197F(x)                                  \
	((x) & (~BITS_LTR_IDLE_TIMER_IDX_8197F))
#define BIT_GET_LTR_IDLE_TIMER_IDX_8197F(x)                                    \
	(((x) >> BIT_SHIFT_LTR_IDLE_TIMER_IDX_8197F) &                         \
	 BIT_MASK_LTR_IDLE_TIMER_IDX_8197F)
#define BIT_SET_LTR_IDLE_TIMER_IDX_8197F(x, v)                                 \
	(BIT_CLEAR_LTR_IDLE_TIMER_IDX_8197F(x) |                               \
	 BIT_LTR_IDLE_TIMER_IDX_8197F(v))

/* 2 REG_LTR_IDLE_LATENCY_V1_8197F */

#define BIT_SHIFT_LTR_IDLE_L_8197F 0
#define BIT_MASK_LTR_IDLE_L_8197F 0xffffffffL
#define BIT_LTR_IDLE_L_8197F(x)                                                \
	(((x) & BIT_MASK_LTR_IDLE_L_8197F) << BIT_SHIFT_LTR_IDLE_L_8197F)
#define BITS_LTR_IDLE_L_8197F                                                  \
	(BIT_MASK_LTR_IDLE_L_8197F << BIT_SHIFT_LTR_IDLE_L_8197F)
#define BIT_CLEAR_LTR_IDLE_L_8197F(x) ((x) & (~BITS_LTR_IDLE_L_8197F))
#define BIT_GET_LTR_IDLE_L_8197F(x)                                            \
	(((x) >> BIT_SHIFT_LTR_IDLE_L_8197F) & BIT_MASK_LTR_IDLE_L_8197F)
#define BIT_SET_LTR_IDLE_L_8197F(x, v)                                         \
	(BIT_CLEAR_LTR_IDLE_L_8197F(x) | BIT_LTR_IDLE_L_8197F(v))

/* 2 REG_LTR_ACTIVE_LATENCY_V1_8197F */

#define BIT_SHIFT_LTR_ACT_L_8197F 0
#define BIT_MASK_LTR_ACT_L_8197F 0xffffffffL
#define BIT_LTR_ACT_L_8197F(x)                                                 \
	(((x) & BIT_MASK_LTR_ACT_L_8197F) << BIT_SHIFT_LTR_ACT_L_8197F)
#define BITS_LTR_ACT_L_8197F                                                   \
	(BIT_MASK_LTR_ACT_L_8197F << BIT_SHIFT_LTR_ACT_L_8197F)
#define BIT_CLEAR_LTR_ACT_L_8197F(x) ((x) & (~BITS_LTR_ACT_L_8197F))
#define BIT_GET_LTR_ACT_L_8197F(x)                                             \
	(((x) >> BIT_SHIFT_LTR_ACT_L_8197F) & BIT_MASK_LTR_ACT_L_8197F)
#define BIT_SET_LTR_ACT_L_8197F(x, v)                                          \
	(BIT_CLEAR_LTR_ACT_L_8197F(x) | BIT_LTR_ACT_L_8197F(v))

/* 2 REG_ANTENNA_TRAINING_CONTROL_REGISTER_8197F */
#define BIT_APPEND_MACID_IN_RESP_EN_8197F BIT(50)
#define BIT_ADDR2_MATCH_EN_8197F BIT(49)
#define BIT_ANTTRN_EN_8197F BIT(48)

#define BIT_SHIFT_TRAIN_STA_ADDR_8197F 0
#define BIT_MASK_TRAIN_STA_ADDR_8197F 0xffffffffffffL
#define BIT_TRAIN_STA_ADDR_8197F(x)                                            \
	(((x) & BIT_MASK_TRAIN_STA_ADDR_8197F)                                 \
	 << BIT_SHIFT_TRAIN_STA_ADDR_8197F)
#define BITS_TRAIN_STA_ADDR_8197F                                              \
	(BIT_MASK_TRAIN_STA_ADDR_8197F << BIT_SHIFT_TRAIN_STA_ADDR_8197F)
#define BIT_CLEAR_TRAIN_STA_ADDR_8197F(x) ((x) & (~BITS_TRAIN_STA_ADDR_8197F))
#define BIT_GET_TRAIN_STA_ADDR_8197F(x)                                        \
	(((x) >> BIT_SHIFT_TRAIN_STA_ADDR_8197F) &                             \
	 BIT_MASK_TRAIN_STA_ADDR_8197F)
#define BIT_SET_TRAIN_STA_ADDR_8197F(x, v)                                     \
	(BIT_CLEAR_TRAIN_STA_ADDR_8197F(x) | BIT_TRAIN_STA_ADDR_8197F(v))

/* 2 REG_RSVD_0X7B4_8197F */

/* 2 REG_WMAC_PKTCNT_RWD_8197F */

#define BIT_SHIFT_PKTCNT_BSSIDMAP_8197F 4
#define BIT_MASK_PKTCNT_BSSIDMAP_8197F 0xf
#define BIT_PKTCNT_BSSIDMAP_8197F(x)                                           \
	(((x) & BIT_MASK_PKTCNT_BSSIDMAP_8197F)                                \
	 << BIT_SHIFT_PKTCNT_BSSIDMAP_8197F)
#define BITS_PKTCNT_BSSIDMAP_8197F                                             \
	(BIT_MASK_PKTCNT_BSSIDMAP_8197F << BIT_SHIFT_PKTCNT_BSSIDMAP_8197F)
#define BIT_CLEAR_PKTCNT_BSSIDMAP_8197F(x) ((x) & (~BITS_PKTCNT_BSSIDMAP_8197F))
#define BIT_GET_PKTCNT_BSSIDMAP_8197F(x)                                       \
	(((x) >> BIT_SHIFT_PKTCNT_BSSIDMAP_8197F) &                            \
	 BIT_MASK_PKTCNT_BSSIDMAP_8197F)
#define BIT_SET_PKTCNT_BSSIDMAP_8197F(x, v)                                    \
	(BIT_CLEAR_PKTCNT_BSSIDMAP_8197F(x) | BIT_PKTCNT_BSSIDMAP_8197F(v))

#define BIT_PKTCNT_CNTRST_8197F BIT(1)
#define BIT_PKTCNT_CNTEN_8197F BIT(0)

/* 2 REG_WMAC_PKTCNT_CTRL_8197F */
#define BIT_WMAC_PKTCNT_TRST_8197F BIT(9)
#define BIT_WMAC_PKTCNT_FEN_8197F BIT(8)

#define BIT_SHIFT_WMAC_PKTCNT_CFGAD_8197F 0
#define BIT_MASK_WMAC_PKTCNT_CFGAD_8197F 0xff
#define BIT_WMAC_PKTCNT_CFGAD_8197F(x)                                         \
	(((x) & BIT_MASK_WMAC_PKTCNT_CFGAD_8197F)                              \
	 << BIT_SHIFT_WMAC_PKTCNT_CFGAD_8197F)
#define BITS_WMAC_PKTCNT_CFGAD_8197F                                           \
	(BIT_MASK_WMAC_PKTCNT_CFGAD_8197F << BIT_SHIFT_WMAC_PKTCNT_CFGAD_8197F)
#define BIT_CLEAR_WMAC_PKTCNT_CFGAD_8197F(x)                                   \
	((x) & (~BITS_WMAC_PKTCNT_CFGAD_8197F))
#define BIT_GET_WMAC_PKTCNT_CFGAD_8197F(x)                                     \
	(((x) >> BIT_SHIFT_WMAC_PKTCNT_CFGAD_8197F) &                          \
	 BIT_MASK_WMAC_PKTCNT_CFGAD_8197F)
#define BIT_SET_WMAC_PKTCNT_CFGAD_8197F(x, v)                                  \
	(BIT_CLEAR_WMAC_PKTCNT_CFGAD_8197F(x) | BIT_WMAC_PKTCNT_CFGAD_8197F(v))

/* 2 REG_IQ_DUMP_8197F */

#define BIT_SHIFT_R_WMAC_MATCH_REF_MAC_8197F (64 & CPU_OPT_WIDTH)
#define BIT_MASK_R_WMAC_MATCH_REF_MAC_8197F 0xffffffffL
#define BIT_R_WMAC_MATCH_REF_MAC_8197F(x)                                      \
	(((x) & BIT_MASK_R_WMAC_MATCH_REF_MAC_8197F)                           \
	 << BIT_SHIFT_R_WMAC_MATCH_REF_MAC_8197F)
#define BITS_R_WMAC_MATCH_REF_MAC_8197F                                        \
	(BIT_MASK_R_WMAC_MATCH_REF_MAC_8197F                                   \
	 << BIT_SHIFT_R_WMAC_MATCH_REF_MAC_8197F)
#define BIT_CLEAR_R_WMAC_MATCH_REF_MAC_8197F(x)                                \
	((x) & (~BITS_R_WMAC_MATCH_REF_MAC_8197F))
#define BIT_GET_R_WMAC_MATCH_REF_MAC_8197F(x)                                  \
	(((x) >> BIT_SHIFT_R_WMAC_MATCH_REF_MAC_8197F) &                       \
	 BIT_MASK_R_WMAC_MATCH_REF_MAC_8197F)
#define BIT_SET_R_WMAC_MATCH_REF_MAC_8197F(x, v)                               \
	(BIT_CLEAR_R_WMAC_MATCH_REF_MAC_8197F(x) |                             \
	 BIT_R_WMAC_MATCH_REF_MAC_8197F(v))

#define BIT_SHIFT_R_WMAC_MASK_LA_MAC_8197F (32 & CPU_OPT_WIDTH)
#define BIT_MASK_R_WMAC_MASK_LA_MAC_8197F 0xffffffffL
#define BIT_R_WMAC_MASK_LA_MAC_8197F(x)                                        \
	(((x) & BIT_MASK_R_WMAC_MASK_LA_MAC_8197F)                             \
	 << BIT_SHIFT_R_WMAC_MASK_LA_MAC_8197F)
#define BITS_R_WMAC_MASK_LA_MAC_8197F                                          \
	(BIT_MASK_R_WMAC_MASK_LA_MAC_8197F                                     \
	 << BIT_SHIFT_R_WMAC_MASK_LA_MAC_8197F)
#define BIT_CLEAR_R_WMAC_MASK_LA_MAC_8197F(x)                                  \
	((x) & (~BITS_R_WMAC_MASK_LA_MAC_8197F))
#define BIT_GET_R_WMAC_MASK_LA_MAC_8197F(x)                                    \
	(((x) >> BIT_SHIFT_R_WMAC_MASK_LA_MAC_8197F) &                         \
	 BIT_MASK_R_WMAC_MASK_LA_MAC_8197F)
#define BIT_SET_R_WMAC_MASK_LA_MAC_8197F(x, v)                                 \
	(BIT_CLEAR_R_WMAC_MASK_LA_MAC_8197F(x) |                               \
	 BIT_R_WMAC_MASK_LA_MAC_8197F(v))

#define BIT_SHIFT_DUMP_OK_ADDR_V1_8197F 15
#define BIT_MASK_DUMP_OK_ADDR_V1_8197F 0x1ffff
#define BIT_DUMP_OK_ADDR_V1_8197F(x)                                           \
	(((x) & BIT_MASK_DUMP_OK_ADDR_V1_8197F)                                \
	 << BIT_SHIFT_DUMP_OK_ADDR_V1_8197F)
#define BITS_DUMP_OK_ADDR_V1_8197F                                             \
	(BIT_MASK_DUMP_OK_ADDR_V1_8197F << BIT_SHIFT_DUMP_OK_ADDR_V1_8197F)
#define BIT_CLEAR_DUMP_OK_ADDR_V1_8197F(x) ((x) & (~BITS_DUMP_OK_ADDR_V1_8197F))
#define BIT_GET_DUMP_OK_ADDR_V1_8197F(x)                                       \
	(((x) >> BIT_SHIFT_DUMP_OK_ADDR_V1_8197F) &                            \
	 BIT_MASK_DUMP_OK_ADDR_V1_8197F)
#define BIT_SET_DUMP_OK_ADDR_V1_8197F(x, v)                                    \
	(BIT_CLEAR_DUMP_OK_ADDR_V1_8197F(x) | BIT_DUMP_OK_ADDR_V1_8197F(v))

#define BIT_SHIFT_R_TRIG_TIME_SEL_8197F 8
#define BIT_MASK_R_TRIG_TIME_SEL_8197F 0x7f
#define BIT_R_TRIG_TIME_SEL_8197F(x)                                           \
	(((x) & BIT_MASK_R_TRIG_TIME_SEL_8197F)                                \
	 << BIT_SHIFT_R_TRIG_TIME_SEL_8197F)
#define BITS_R_TRIG_TIME_SEL_8197F                                             \
	(BIT_MASK_R_TRIG_TIME_SEL_8197F << BIT_SHIFT_R_TRIG_TIME_SEL_8197F)
#define BIT_CLEAR_R_TRIG_TIME_SEL_8197F(x) ((x) & (~BITS_R_TRIG_TIME_SEL_8197F))
#define BIT_GET_R_TRIG_TIME_SEL_8197F(x)                                       \
	(((x) >> BIT_SHIFT_R_TRIG_TIME_SEL_8197F) &                            \
	 BIT_MASK_R_TRIG_TIME_SEL_8197F)
#define BIT_SET_R_TRIG_TIME_SEL_8197F(x, v)                                    \
	(BIT_CLEAR_R_TRIG_TIME_SEL_8197F(x) | BIT_R_TRIG_TIME_SEL_8197F(v))

#define BIT_SHIFT_R_MAC_TRIG_SEL_8197F 6
#define BIT_MASK_R_MAC_TRIG_SEL_8197F 0x3
#define BIT_R_MAC_TRIG_SEL_8197F(x)                                            \
	(((x) & BIT_MASK_R_MAC_TRIG_SEL_8197F)                                 \
	 << BIT_SHIFT_R_MAC_TRIG_SEL_8197F)
#define BITS_R_MAC_TRIG_SEL_8197F                                              \
	(BIT_MASK_R_MAC_TRIG_SEL_8197F << BIT_SHIFT_R_MAC_TRIG_SEL_8197F)
#define BIT_CLEAR_R_MAC_TRIG_SEL_8197F(x) ((x) & (~BITS_R_MAC_TRIG_SEL_8197F))
#define BIT_GET_R_MAC_TRIG_SEL_8197F(x)                                        \
	(((x) >> BIT_SHIFT_R_MAC_TRIG_SEL_8197F) &                             \
	 BIT_MASK_R_MAC_TRIG_SEL_8197F)
#define BIT_SET_R_MAC_TRIG_SEL_8197F(x, v)                                     \
	(BIT_CLEAR_R_MAC_TRIG_SEL_8197F(x) | BIT_R_MAC_TRIG_SEL_8197F(v))

#define BIT_MAC_TRIG_REG_8197F BIT(5)

#define BIT_SHIFT_R_LEVEL_PULSE_SEL_8197F 3
#define BIT_MASK_R_LEVEL_PULSE_SEL_8197F 0x3
#define BIT_R_LEVEL_PULSE_SEL_8197F(x)                                         \
	(((x) & BIT_MASK_R_LEVEL_PULSE_SEL_8197F)                              \
	 << BIT_SHIFT_R_LEVEL_PULSE_SEL_8197F)
#define BITS_R_LEVEL_PULSE_SEL_8197F                                           \
	(BIT_MASK_R_LEVEL_PULSE_SEL_8197F << BIT_SHIFT_R_LEVEL_PULSE_SEL_8197F)
#define BIT_CLEAR_R_LEVEL_PULSE_SEL_8197F(x)                                   \
	((x) & (~BITS_R_LEVEL_PULSE_SEL_8197F))
#define BIT_GET_R_LEVEL_PULSE_SEL_8197F(x)                                     \
	(((x) >> BIT_SHIFT_R_LEVEL_PULSE_SEL_8197F) &                          \
	 BIT_MASK_R_LEVEL_PULSE_SEL_8197F)
#define BIT_SET_R_LEVEL_PULSE_SEL_8197F(x, v)                                  \
	(BIT_CLEAR_R_LEVEL_PULSE_SEL_8197F(x) | BIT_R_LEVEL_PULSE_SEL_8197F(v))

#define BIT_EN_LA_MAC_8197F BIT(2)
#define BIT_R_EN_IQDUMP_8197F BIT(1)
#define BIT_R_IQDATA_DUMP_8197F BIT(0)

/* 2 REG_WMAC_FTM_CTL_8197F */
#define BIT_RXFTM_TXACK_SC_8197F BIT(6)
#define BIT_RXFTM_TXACK_BW_8197F BIT(5)
#define BIT_RXFTM_EN_8197F BIT(3)
#define BIT_RXFTMREQ_BYDRV_8197F BIT(2)
#define BIT_RXFTMREQ_EN_8197F BIT(1)
#define BIT_FTM_EN_8197F BIT(0)

/* 2 REG_IQ_DUMP_EXT_8197F */

#define BIT_SHIFT_R_TIME_UNIT_SEL_8197F 0
#define BIT_MASK_R_TIME_UNIT_SEL_8197F 0x7
#define BIT_R_TIME_UNIT_SEL_8197F(x)                                           \
	(((x) & BIT_MASK_R_TIME_UNIT_SEL_8197F)                                \
	 << BIT_SHIFT_R_TIME_UNIT_SEL_8197F)
#define BITS_R_TIME_UNIT_SEL_8197F                                             \
	(BIT_MASK_R_TIME_UNIT_SEL_8197F << BIT_SHIFT_R_TIME_UNIT_SEL_8197F)
#define BIT_CLEAR_R_TIME_UNIT_SEL_8197F(x) ((x) & (~BITS_R_TIME_UNIT_SEL_8197F))
#define BIT_GET_R_TIME_UNIT_SEL_8197F(x)                                       \
	(((x) >> BIT_SHIFT_R_TIME_UNIT_SEL_8197F) &                            \
	 BIT_MASK_R_TIME_UNIT_SEL_8197F)
#define BIT_SET_R_TIME_UNIT_SEL_8197F(x, v)                                    \
	(BIT_CLEAR_R_TIME_UNIT_SEL_8197F(x) | BIT_R_TIME_UNIT_SEL_8197F(v))

/* 2 REG_OFDM_CCK_LEN_MASK_8197F */

#define BIT_SHIFT_R_WMAC_RX_FIL_LEN_8197F (64 & CPU_OPT_WIDTH)
#define BIT_MASK_R_WMAC_RX_FIL_LEN_8197F 0xffff
#define BIT_R_WMAC_RX_FIL_LEN_8197F(x)                                         \
	(((x) & BIT_MASK_R_WMAC_RX_FIL_LEN_8197F)                              \
	 << BIT_SHIFT_R_WMAC_RX_FIL_LEN_8197F)
#define BITS_R_WMAC_RX_FIL_LEN_8197F                                           \
	(BIT_MASK_R_WMAC_RX_FIL_LEN_8197F << BIT_SHIFT_R_WMAC_RX_FIL_LEN_8197F)
#define BIT_CLEAR_R_WMAC_RX_FIL_LEN_8197F(x)                                   \
	((x) & (~BITS_R_WMAC_RX_FIL_LEN_8197F))
#define BIT_GET_R_WMAC_RX_FIL_LEN_8197F(x)                                     \
	(((x) >> BIT_SHIFT_R_WMAC_RX_FIL_LEN_8197F) &                          \
	 BIT_MASK_R_WMAC_RX_FIL_LEN_8197F)
#define BIT_SET_R_WMAC_RX_FIL_LEN_8197F(x, v)                                  \
	(BIT_CLEAR_R_WMAC_RX_FIL_LEN_8197F(x) | BIT_R_WMAC_RX_FIL_LEN_8197F(v))

#define BIT_SHIFT_R_WMAC_RXFIFO_FULL_TH_8197F (56 & CPU_OPT_WIDTH)
#define BIT_MASK_R_WMAC_RXFIFO_FULL_TH_8197F 0xff
#define BIT_R_WMAC_RXFIFO_FULL_TH_8197F(x)                                     \
	(((x) & BIT_MASK_R_WMAC_RXFIFO_FULL_TH_8197F)                          \
	 << BIT_SHIFT_R_WMAC_RXFIFO_FULL_TH_8197F)
#define BITS_R_WMAC_RXFIFO_FULL_TH_8197F                                       \
	(BIT_MASK_R_WMAC_RXFIFO_FULL_TH_8197F                                  \
	 << BIT_SHIFT_R_WMAC_RXFIFO_FULL_TH_8197F)
#define BIT_CLEAR_R_WMAC_RXFIFO_FULL_TH_8197F(x)                               \
	((x) & (~BITS_R_WMAC_RXFIFO_FULL_TH_8197F))
#define BIT_GET_R_WMAC_RXFIFO_FULL_TH_8197F(x)                                 \
	(((x) >> BIT_SHIFT_R_WMAC_RXFIFO_FULL_TH_8197F) &                      \
	 BIT_MASK_R_WMAC_RXFIFO_FULL_TH_8197F)
#define BIT_SET_R_WMAC_RXFIFO_FULL_TH_8197F(x, v)                              \
	(BIT_CLEAR_R_WMAC_RXFIFO_FULL_TH_8197F(x) |                            \
	 BIT_R_WMAC_RXFIFO_FULL_TH_8197F(v))

#define BIT_R_WMAC_RX_SYNCFIFO_SYNC_8197F BIT(55)
#define BIT_R_WMAC_RXRST_DLY_8197F BIT(54)
#define BIT_R_WMAC_SRCH_TXRPT_REF_DROP_8197F BIT(53)
#define BIT_R_WMAC_SRCH_TXRPT_UA1_8197F BIT(52)
#define BIT_R_WMAC_SRCH_TXRPT_TYPE_8197F BIT(51)
#define BIT_R_WMAC_NDP_RST_8197F BIT(50)
#define BIT_R_WMAC_POWINT_EN_8197F BIT(49)
#define BIT_R_WMAC_SRCH_TXRPT_PERPKT_8197F BIT(48)
#define BIT_R_WMAC_SRCH_TXRPT_MID_8197F BIT(47)
#define BIT_R_WMAC_PFIN_TOEN_8197F BIT(46)
#define BIT_R_WMAC_FIL_SECERR_8197F BIT(45)
#define BIT_R_WMAC_FIL_CTLPKTLEN_8197F BIT(44)
#define BIT_R_WMAC_FIL_FCTYPE_8197F BIT(43)
#define BIT_R_WMAC_FIL_FCPROVER_8197F BIT(42)
#define BIT_R_WMAC_PHYSTS_SNIF_8197F BIT(41)
#define BIT_R_WMAC_PHYSTS_PLCP_8197F BIT(40)
#define BIT_R_MAC_TCR_VBONF_RD_8197F BIT(39)
#define BIT_R_WMAC_TCR_MPAR_NDP_8197F BIT(38)
#define BIT_R_WMAC_NDP_FILTER_8197F BIT(37)
#define BIT_R_WMAC_RXLEN_SEL_8197F BIT(36)
#define BIT_R_WMAC_RXLEN_SEL1_8197F BIT(35)
#define BIT_R_OFDM_FILTER_8197F BIT(34)
#define BIT_R_WMAC_CHK_OFDM_LEN_8197F BIT(33)
#define BIT_R_WMAC_CHK_CCK_LEN_8197F BIT(32)

#define BIT_SHIFT_R_OFDM_LEN_8197F 26
#define BIT_MASK_R_OFDM_LEN_8197F 0x3f
#define BIT_R_OFDM_LEN_8197F(x)                                                \
	(((x) & BIT_MASK_R_OFDM_LEN_8197F) << BIT_SHIFT_R_OFDM_LEN_8197F)
#define BITS_R_OFDM_LEN_8197F                                                  \
	(BIT_MASK_R_OFDM_LEN_8197F << BIT_SHIFT_R_OFDM_LEN_8197F)
#define BIT_CLEAR_R_OFDM_LEN_8197F(x) ((x) & (~BITS_R_OFDM_LEN_8197F))
#define BIT_GET_R_OFDM_LEN_8197F(x)                                            \
	(((x) >> BIT_SHIFT_R_OFDM_LEN_8197F) & BIT_MASK_R_OFDM_LEN_8197F)
#define BIT_SET_R_OFDM_LEN_8197F(x, v)                                         \
	(BIT_CLEAR_R_OFDM_LEN_8197F(x) | BIT_R_OFDM_LEN_8197F(v))

#define BIT_SHIFT_R_CCK_LEN_8197F 0
#define BIT_MASK_R_CCK_LEN_8197F 0xffff
#define BIT_R_CCK_LEN_8197F(x)                                                 \
	(((x) & BIT_MASK_R_CCK_LEN_8197F) << BIT_SHIFT_R_CCK_LEN_8197F)
#define BITS_R_CCK_LEN_8197F                                                   \
	(BIT_MASK_R_CCK_LEN_8197F << BIT_SHIFT_R_CCK_LEN_8197F)
#define BIT_CLEAR_R_CCK_LEN_8197F(x) ((x) & (~BITS_R_CCK_LEN_8197F))
#define BIT_GET_R_CCK_LEN_8197F(x)                                             \
	(((x) >> BIT_SHIFT_R_CCK_LEN_8197F) & BIT_MASK_R_CCK_LEN_8197F)
#define BIT_SET_R_CCK_LEN_8197F(x, v)                                          \
	(BIT_CLEAR_R_CCK_LEN_8197F(x) | BIT_R_CCK_LEN_8197F(v))

/* 2 REG_RX_FILTER_FUNCTION_8197F */
#define BIT_R_WMAC_RXHANG_EN_8197F BIT(15)
#define BIT_R_WMAC_MHRDDY_LATCH_8197F BIT(14)
#define BIT_R_MHRDDY_CLR_8197F BIT(13)
#define BIT_R_RXPKTCTL_FSM_BASED_MPDURDY1_8197F BIT(12)
#define BIT_R_WMAC_DIS_VHT_PLCP_CHK_MU_8197F BIT(11)
#define BIT_R_CHK_DELIMIT_LEN_8197F BIT(10)
#define BIT_R_REAPTER_ADDR_MATCH_8197F BIT(9)
#define BIT_R_RXPKTCTL_FSM_BASED_MPDURDY_8197F BIT(8)
#define BIT_R_LATCH_MACHRDY_8197F BIT(7)
#define BIT_R_WMAC_RXFIL_REND_8197F BIT(6)
#define BIT_R_WMAC_MPDURDY_CLR_8197F BIT(5)
#define BIT_R_WMAC_CLRRXSEC_8197F BIT(4)
#define BIT_R_WMAC_RXFIL_RDEL_8197F BIT(3)
#define BIT_R_WMAC_RXFIL_FCSE_8197F BIT(2)
#define BIT_R_WMAC_RXFIL_MESH_DEL_8197F BIT(1)
#define BIT_R_WMAC_RXFIL_MASKM_8197F BIT(0)

/* 2 REG_NDP_SIG_8197F */

#define BIT_SHIFT_R_WMAC_TXNDP_SIGB_8197F 0
#define BIT_MASK_R_WMAC_TXNDP_SIGB_8197F 0x1fffff
#define BIT_R_WMAC_TXNDP_SIGB_8197F(x)                                         \
	(((x) & BIT_MASK_R_WMAC_TXNDP_SIGB_8197F)                              \
	 << BIT_SHIFT_R_WMAC_TXNDP_SIGB_8197F)
#define BITS_R_WMAC_TXNDP_SIGB_8197F                                           \
	(BIT_MASK_R_WMAC_TXNDP_SIGB_8197F << BIT_SHIFT_R_WMAC_TXNDP_SIGB_8197F)
#define BIT_CLEAR_R_WMAC_TXNDP_SIGB_8197F(x)                                   \
	((x) & (~BITS_R_WMAC_TXNDP_SIGB_8197F))
#define BIT_GET_R_WMAC_TXNDP_SIGB_8197F(x)                                     \
	(((x) >> BIT_SHIFT_R_WMAC_TXNDP_SIGB_8197F) &                          \
	 BIT_MASK_R_WMAC_TXNDP_SIGB_8197F)
#define BIT_SET_R_WMAC_TXNDP_SIGB_8197F(x, v)                                  \
	(BIT_CLEAR_R_WMAC_TXNDP_SIGB_8197F(x) | BIT_R_WMAC_TXNDP_SIGB_8197F(v))

/* 2 REG_TXCMD_INFO_FOR_RSP_PKT_8197F */

#define BIT_SHIFT_R_MAC_DEBUG_8197F (32 & CPU_OPT_WIDTH)
#define BIT_MASK_R_MAC_DEBUG_8197F 0xffffffffL
#define BIT_R_MAC_DEBUG_8197F(x)                                               \
	(((x) & BIT_MASK_R_MAC_DEBUG_8197F) << BIT_SHIFT_R_MAC_DEBUG_8197F)
#define BITS_R_MAC_DEBUG_8197F                                                 \
	(BIT_MASK_R_MAC_DEBUG_8197F << BIT_SHIFT_R_MAC_DEBUG_8197F)
#define BIT_CLEAR_R_MAC_DEBUG_8197F(x) ((x) & (~BITS_R_MAC_DEBUG_8197F))
#define BIT_GET_R_MAC_DEBUG_8197F(x)                                           \
	(((x) >> BIT_SHIFT_R_MAC_DEBUG_8197F) & BIT_MASK_R_MAC_DEBUG_8197F)
#define BIT_SET_R_MAC_DEBUG_8197F(x, v)                                        \
	(BIT_CLEAR_R_MAC_DEBUG_8197F(x) | BIT_R_MAC_DEBUG_8197F(v))

#define BIT_SHIFT_R_MAC_DBG_SHIFT_8197F 8
#define BIT_MASK_R_MAC_DBG_SHIFT_8197F 0x7
#define BIT_R_MAC_DBG_SHIFT_8197F(x)                                           \
	(((x) & BIT_MASK_R_MAC_DBG_SHIFT_8197F)                                \
	 << BIT_SHIFT_R_MAC_DBG_SHIFT_8197F)
#define BITS_R_MAC_DBG_SHIFT_8197F                                             \
	(BIT_MASK_R_MAC_DBG_SHIFT_8197F << BIT_SHIFT_R_MAC_DBG_SHIFT_8197F)
#define BIT_CLEAR_R_MAC_DBG_SHIFT_8197F(x) ((x) & (~BITS_R_MAC_DBG_SHIFT_8197F))
#define BIT_GET_R_MAC_DBG_SHIFT_8197F(x)                                       \
	(((x) >> BIT_SHIFT_R_MAC_DBG_SHIFT_8197F) &                            \
	 BIT_MASK_R_MAC_DBG_SHIFT_8197F)
#define BIT_SET_R_MAC_DBG_SHIFT_8197F(x, v)                                    \
	(BIT_CLEAR_R_MAC_DBG_SHIFT_8197F(x) | BIT_R_MAC_DBG_SHIFT_8197F(v))

#define BIT_SHIFT_R_MAC_DBG_SEL_8197F 0
#define BIT_MASK_R_MAC_DBG_SEL_8197F 0x3
#define BIT_R_MAC_DBG_SEL_8197F(x)                                             \
	(((x) & BIT_MASK_R_MAC_DBG_SEL_8197F) << BIT_SHIFT_R_MAC_DBG_SEL_8197F)
#define BITS_R_MAC_DBG_SEL_8197F                                               \
	(BIT_MASK_R_MAC_DBG_SEL_8197F << BIT_SHIFT_R_MAC_DBG_SEL_8197F)
#define BIT_CLEAR_R_MAC_DBG_SEL_8197F(x) ((x) & (~BITS_R_MAC_DBG_SEL_8197F))
#define BIT_GET_R_MAC_DBG_SEL_8197F(x)                                         \
	(((x) >> BIT_SHIFT_R_MAC_DBG_SEL_8197F) & BIT_MASK_R_MAC_DBG_SEL_8197F)
#define BIT_SET_R_MAC_DBG_SEL_8197F(x, v)                                      \
	(BIT_CLEAR_R_MAC_DBG_SEL_8197F(x) | BIT_R_MAC_DBG_SEL_8197F(v))

/* 2 REG_SEC_OPT_V2_8197F */
#define BIT_MASK_IV_8197F BIT(18)
#define BIT_EIVL_ENDIAN_8197F BIT(17)
#define BIT_EIVH_ENDIAN_8197F BIT(16)

#define BIT_SHIFT_BT_TIME_CNT_8197F 0
#define BIT_MASK_BT_TIME_CNT_8197F 0xff
#define BIT_BT_TIME_CNT_8197F(x)                                               \
	(((x) & BIT_MASK_BT_TIME_CNT_8197F) << BIT_SHIFT_BT_TIME_CNT_8197F)
#define BITS_BT_TIME_CNT_8197F                                                 \
	(BIT_MASK_BT_TIME_CNT_8197F << BIT_SHIFT_BT_TIME_CNT_8197F)
#define BIT_CLEAR_BT_TIME_CNT_8197F(x) ((x) & (~BITS_BT_TIME_CNT_8197F))
#define BIT_GET_BT_TIME_CNT_8197F(x)                                           \
	(((x) >> BIT_SHIFT_BT_TIME_CNT_8197F) & BIT_MASK_BT_TIME_CNT_8197F)
#define BIT_SET_BT_TIME_CNT_8197F(x, v)                                        \
	(BIT_CLEAR_BT_TIME_CNT_8197F(x) | BIT_BT_TIME_CNT_8197F(v))

/* 2 REG_RTS_ADDRESS_0_8197F */

/* 2 REG_RTS_ADDRESS_1_8197F */

/* 2 REG_WL2LTECOEX_INDIRECT_ACCESS_CTRL_V1_8197F */
#define BIT_LTECOEX_ACCESS_START_V1_8197F BIT(31)
#define BIT_LTECOEX_WRITE_MODE_V1_8197F BIT(30)
#define BIT_LTECOEX_READY_BIT_V1_8197F BIT(29)

#define BIT_SHIFT_WRITE_BYTE_EN_V1_8197F 16
#define BIT_MASK_WRITE_BYTE_EN_V1_8197F 0xf
#define BIT_WRITE_BYTE_EN_V1_8197F(x)                                          \
	(((x) & BIT_MASK_WRITE_BYTE_EN_V1_8197F)                               \
	 << BIT_SHIFT_WRITE_BYTE_EN_V1_8197F)
#define BITS_WRITE_BYTE_EN_V1_8197F                                            \
	(BIT_MASK_WRITE_BYTE_EN_V1_8197F << BIT_SHIFT_WRITE_BYTE_EN_V1_8197F)
#define BIT_CLEAR_WRITE_BYTE_EN_V1_8197F(x)                                    \
	((x) & (~BITS_WRITE_BYTE_EN_V1_8197F))
#define BIT_GET_WRITE_BYTE_EN_V1_8197F(x)                                      \
	(((x) >> BIT_SHIFT_WRITE_BYTE_EN_V1_8197F) &                           \
	 BIT_MASK_WRITE_BYTE_EN_V1_8197F)
#define BIT_SET_WRITE_BYTE_EN_V1_8197F(x, v)                                   \
	(BIT_CLEAR_WRITE_BYTE_EN_V1_8197F(x) | BIT_WRITE_BYTE_EN_V1_8197F(v))

#define BIT_SHIFT_LTECOEX_REG_ADDR_V1_8197F 0
#define BIT_MASK_LTECOEX_REG_ADDR_V1_8197F 0xffff
#define BIT_LTECOEX_REG_ADDR_V1_8197F(x)                                       \
	(((x) & BIT_MASK_LTECOEX_REG_ADDR_V1_8197F)                            \
	 << BIT_SHIFT_LTECOEX_REG_ADDR_V1_8197F)
#define BITS_LTECOEX_REG_ADDR_V1_8197F                                         \
	(BIT_MASK_LTECOEX_REG_ADDR_V1_8197F                                    \
	 << BIT_SHIFT_LTECOEX_REG_ADDR_V1_8197F)
#define BIT_CLEAR_LTECOEX_REG_ADDR_V1_8197F(x)                                 \
	((x) & (~BITS_LTECOEX_REG_ADDR_V1_8197F))
#define BIT_GET_LTECOEX_REG_ADDR_V1_8197F(x)                                   \
	(((x) >> BIT_SHIFT_LTECOEX_REG_ADDR_V1_8197F) &                        \
	 BIT_MASK_LTECOEX_REG_ADDR_V1_8197F)
#define BIT_SET_LTECOEX_REG_ADDR_V1_8197F(x, v)                                \
	(BIT_CLEAR_LTECOEX_REG_ADDR_V1_8197F(x) |                              \
	 BIT_LTECOEX_REG_ADDR_V1_8197F(v))

/* 2 REG_WL2LTECOEX_INDIRECT_ACCESS_WRITE_DATA_V1_8197F */

#define BIT_SHIFT_LTECOEX_W_DATA_V1_8197F 0
#define BIT_MASK_LTECOEX_W_DATA_V1_8197F 0xffffffffL
#define BIT_LTECOEX_W_DATA_V1_8197F(x)                                         \
	(((x) & BIT_MASK_LTECOEX_W_DATA_V1_8197F)                              \
	 << BIT_SHIFT_LTECOEX_W_DATA_V1_8197F)
#define BITS_LTECOEX_W_DATA_V1_8197F                                           \
	(BIT_MASK_LTECOEX_W_DATA_V1_8197F << BIT_SHIFT_LTECOEX_W_DATA_V1_8197F)
#define BIT_CLEAR_LTECOEX_W_DATA_V1_8197F(x)                                   \
	((x) & (~BITS_LTECOEX_W_DATA_V1_8197F))
#define BIT_GET_LTECOEX_W_DATA_V1_8197F(x)                                     \
	(((x) >> BIT_SHIFT_LTECOEX_W_DATA_V1_8197F) &                          \
	 BIT_MASK_LTECOEX_W_DATA_V1_8197F)
#define BIT_SET_LTECOEX_W_DATA_V1_8197F(x, v)                                  \
	(BIT_CLEAR_LTECOEX_W_DATA_V1_8197F(x) | BIT_LTECOEX_W_DATA_V1_8197F(v))

/* 2 REG_WL2LTECOEX_INDIRECT_ACCESS_READ_DATA_V1_8197F */

#define BIT_SHIFT_LTECOEX_R_DATA_V1_8197F 0
#define BIT_MASK_LTECOEX_R_DATA_V1_8197F 0xffffffffL
#define BIT_LTECOEX_R_DATA_V1_8197F(x)                                         \
	(((x) & BIT_MASK_LTECOEX_R_DATA_V1_8197F)                              \
	 << BIT_SHIFT_LTECOEX_R_DATA_V1_8197F)
#define BITS_LTECOEX_R_DATA_V1_8197F                                           \
	(BIT_MASK_LTECOEX_R_DATA_V1_8197F << BIT_SHIFT_LTECOEX_R_DATA_V1_8197F)
#define BIT_CLEAR_LTECOEX_R_DATA_V1_8197F(x)                                   \
	((x) & (~BITS_LTECOEX_R_DATA_V1_8197F))
#define BIT_GET_LTECOEX_R_DATA_V1_8197F(x)                                     \
	(((x) >> BIT_SHIFT_LTECOEX_R_DATA_V1_8197F) &                          \
	 BIT_MASK_LTECOEX_R_DATA_V1_8197F)
#define BIT_SET_LTECOEX_R_DATA_V1_8197F(x, v)                                  \
	(BIT_CLEAR_LTECOEX_R_DATA_V1_8197F(x) | BIT_LTECOEX_R_DATA_V1_8197F(v))

/* 2 REG_NOT_VALID_8197F */

#endif
