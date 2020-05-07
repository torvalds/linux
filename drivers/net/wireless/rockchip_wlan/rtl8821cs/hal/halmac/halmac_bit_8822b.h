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

#ifndef __INC_HALMAC_BIT_8822B_H
#define __INC_HALMAC_BIT_8822B_H

#define CPU_OPT_WIDTH 0x1F

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_SYS_ISO_CTRL_8822B */
#define BIT_PWC_EV12V_8822B BIT(15)
#define BIT_PWC_EV25V_8822B BIT(14)
#define BIT_PA33V_EN_8822B BIT(13)
#define BIT_PA12V_EN_8822B BIT(12)
#define BIT_UA33V_EN_8822B BIT(11)
#define BIT_UA12V_EN_8822B BIT(10)
#define BIT_ISO_RFDIO_8822B BIT(9)
#define BIT_ISO_EB2CORE_8822B BIT(8)
#define BIT_ISO_DIOE_8822B BIT(7)
#define BIT_ISO_WLPON2PP_8822B BIT(6)
#define BIT_ISO_IP2MAC_WA2PP_8822B BIT(5)
#define BIT_ISO_PD2CORE_8822B BIT(4)
#define BIT_ISO_PA2PCIE_8822B BIT(3)
#define BIT_ISO_UD2CORE_8822B BIT(2)
#define BIT_ISO_UA2USB_8822B BIT(1)
#define BIT_ISO_WD2PP_8822B BIT(0)

/* 2 REG_SYS_FUNC_EN_8822B */
#define BIT_FEN_MREGEN_8822B BIT(15)
#define BIT_FEN_HWPDN_8822B BIT(14)
#define BIT_EN_25_1_8822B BIT(13)
#define BIT_FEN_ELDR_8822B BIT(12)
#define BIT_FEN_DCORE_8822B BIT(11)
#define BIT_FEN_CPUEN_8822B BIT(10)
#define BIT_FEN_DIOE_8822B BIT(9)
#define BIT_FEN_PCIED_8822B BIT(8)
#define BIT_FEN_PPLL_8822B BIT(7)
#define BIT_FEN_PCIEA_8822B BIT(6)
#define BIT_FEN_DIO_PCIE_8822B BIT(5)
#define BIT_FEN_USBD_8822B BIT(4)
#define BIT_FEN_UPLL_8822B BIT(3)
#define BIT_FEN_USBA_8822B BIT(2)
#define BIT_FEN_BB_GLB_RSTN_8822B BIT(1)
#define BIT_FEN_BBRSTB_8822B BIT(0)

/* 2 REG_SYS_PW_CTRL_8822B */
#define BIT_SOP_EABM_8822B BIT(31)
#define BIT_SOP_ACKF_8822B BIT(30)
#define BIT_SOP_ERCK_8822B BIT(29)
#define BIT_SOP_ESWR_8822B BIT(28)
#define BIT_SOP_PWMM_8822B BIT(27)
#define BIT_SOP_EECK_8822B BIT(26)
#define BIT_SOP_EXTL_8822B BIT(24)
#define BIT_SYM_OP_RING_12M_8822B BIT(22)
#define BIT_ROP_SWPR_8822B BIT(21)
#define BIT_DIS_HW_LPLDM_8822B BIT(20)
#define BIT_OPT_SWRST_WLMCU_8822B BIT(19)
#define BIT_RDY_SYSPWR_8822B BIT(17)
#define BIT_EN_WLON_8822B BIT(16)
#define BIT_APDM_HPDN_8822B BIT(15)
#define BIT_AFSM_PCIE_SUS_EN_8822B BIT(12)
#define BIT_AFSM_WLSUS_EN_8822B BIT(11)
#define BIT_APFM_SWLPS_8822B BIT(10)
#define BIT_APFM_OFFMAC_8822B BIT(9)
#define BIT_APFN_ONMAC_8822B BIT(8)
#define BIT_CHIP_PDN_EN_8822B BIT(7)
#define BIT_RDY_MACDIS_8822B BIT(6)
#define BIT_RING_CLK_12M_EN_8822B BIT(4)
#define BIT_PFM_WOWL_8822B BIT(3)
#define BIT_PFM_LDKP_8822B BIT(2)
#define BIT_WL_HCI_ALD_8822B BIT(1)
#define BIT_PFM_LDALL_8822B BIT(0)

/* 2 REG_SYS_CLK_CTRL_8822B */
#define BIT_LDO_DUMMY_8822B BIT(15)
#define BIT_CPU_CLK_EN_8822B BIT(14)
#define BIT_SYMREG_CLK_EN_8822B BIT(13)
#define BIT_HCI_CLK_EN_8822B BIT(12)
#define BIT_MAC_CLK_EN_8822B BIT(11)
#define BIT_SEC_CLK_EN_8822B BIT(10)
#define BIT_PHY_SSC_RSTB_8822B BIT(9)
#define BIT_EXT_32K_EN_8822B BIT(8)
#define BIT_WL_CLK_TEST_8822B BIT(7)
#define BIT_OP_SPS_PWM_EN_8822B BIT(6)
#define BIT_LOADER_CLK_EN_8822B BIT(5)
#define BIT_MACSLP_8822B BIT(4)
#define BIT_WAKEPAD_EN_8822B BIT(3)
#define BIT_ROMD16V_EN_8822B BIT(2)
#define BIT_CKANA12M_EN_8822B BIT(1)
#define BIT_CNTD16V_EN_8822B BIT(0)

/* 2 REG_SYS_EEPROM_CTRL_8822B */

#define BIT_SHIFT_VPDIDX_8822B 8
#define BIT_MASK_VPDIDX_8822B 0xff
#define BIT_VPDIDX_8822B(x)                                                    \
	(((x) & BIT_MASK_VPDIDX_8822B) << BIT_SHIFT_VPDIDX_8822B)
#define BITS_VPDIDX_8822B (BIT_MASK_VPDIDX_8822B << BIT_SHIFT_VPDIDX_8822B)
#define BIT_CLEAR_VPDIDX_8822B(x) ((x) & (~BITS_VPDIDX_8822B))
#define BIT_GET_VPDIDX_8822B(x)                                                \
	(((x) >> BIT_SHIFT_VPDIDX_8822B) & BIT_MASK_VPDIDX_8822B)
#define BIT_SET_VPDIDX_8822B(x, v)                                             \
	(BIT_CLEAR_VPDIDX_8822B(x) | BIT_VPDIDX_8822B(v))

#define BIT_SHIFT_EEM1_0_8822B 6
#define BIT_MASK_EEM1_0_8822B 0x3
#define BIT_EEM1_0_8822B(x)                                                    \
	(((x) & BIT_MASK_EEM1_0_8822B) << BIT_SHIFT_EEM1_0_8822B)
#define BITS_EEM1_0_8822B (BIT_MASK_EEM1_0_8822B << BIT_SHIFT_EEM1_0_8822B)
#define BIT_CLEAR_EEM1_0_8822B(x) ((x) & (~BITS_EEM1_0_8822B))
#define BIT_GET_EEM1_0_8822B(x)                                                \
	(((x) >> BIT_SHIFT_EEM1_0_8822B) & BIT_MASK_EEM1_0_8822B)
#define BIT_SET_EEM1_0_8822B(x, v)                                             \
	(BIT_CLEAR_EEM1_0_8822B(x) | BIT_EEM1_0_8822B(v))

#define BIT_AUTOLOAD_SUS_8822B BIT(5)
#define BIT_EERPOMSEL_8822B BIT(4)
#define BIT_EECS_V1_8822B BIT(3)
#define BIT_EESK_V1_8822B BIT(2)
#define BIT_EEDI_V1_8822B BIT(1)
#define BIT_EEDO_V1_8822B BIT(0)

/* 2 REG_EE_VPD_8822B */

#define BIT_SHIFT_VPD_DATA_8822B 0
#define BIT_MASK_VPD_DATA_8822B 0xffffffffL
#define BIT_VPD_DATA_8822B(x)                                                  \
	(((x) & BIT_MASK_VPD_DATA_8822B) << BIT_SHIFT_VPD_DATA_8822B)
#define BITS_VPD_DATA_8822B                                                    \
	(BIT_MASK_VPD_DATA_8822B << BIT_SHIFT_VPD_DATA_8822B)
#define BIT_CLEAR_VPD_DATA_8822B(x) ((x) & (~BITS_VPD_DATA_8822B))
#define BIT_GET_VPD_DATA_8822B(x)                                              \
	(((x) >> BIT_SHIFT_VPD_DATA_8822B) & BIT_MASK_VPD_DATA_8822B)
#define BIT_SET_VPD_DATA_8822B(x, v)                                           \
	(BIT_CLEAR_VPD_DATA_8822B(x) | BIT_VPD_DATA_8822B(v))

/* 2 REG_SYS_SWR_CTRL1_8822B */
#define BIT_C2_L_BIT0_8822B BIT(31)

#define BIT_SHIFT_C1_L_8822B 29
#define BIT_MASK_C1_L_8822B 0x3
#define BIT_C1_L_8822B(x) (((x) & BIT_MASK_C1_L_8822B) << BIT_SHIFT_C1_L_8822B)
#define BITS_C1_L_8822B (BIT_MASK_C1_L_8822B << BIT_SHIFT_C1_L_8822B)
#define BIT_CLEAR_C1_L_8822B(x) ((x) & (~BITS_C1_L_8822B))
#define BIT_GET_C1_L_8822B(x)                                                  \
	(((x) >> BIT_SHIFT_C1_L_8822B) & BIT_MASK_C1_L_8822B)
#define BIT_SET_C1_L_8822B(x, v) (BIT_CLEAR_C1_L_8822B(x) | BIT_C1_L_8822B(v))

#define BIT_SHIFT_REG_FREQ_L_8822B 25
#define BIT_MASK_REG_FREQ_L_8822B 0x7
#define BIT_REG_FREQ_L_8822B(x)                                                \
	(((x) & BIT_MASK_REG_FREQ_L_8822B) << BIT_SHIFT_REG_FREQ_L_8822B)
#define BITS_REG_FREQ_L_8822B                                                  \
	(BIT_MASK_REG_FREQ_L_8822B << BIT_SHIFT_REG_FREQ_L_8822B)
#define BIT_CLEAR_REG_FREQ_L_8822B(x) ((x) & (~BITS_REG_FREQ_L_8822B))
#define BIT_GET_REG_FREQ_L_8822B(x)                                            \
	(((x) >> BIT_SHIFT_REG_FREQ_L_8822B) & BIT_MASK_REG_FREQ_L_8822B)
#define BIT_SET_REG_FREQ_L_8822B(x, v)                                         \
	(BIT_CLEAR_REG_FREQ_L_8822B(x) | BIT_REG_FREQ_L_8822B(v))

#define BIT_REG_EN_DUTY_8822B BIT(24)

#define BIT_SHIFT_REG_MODE_8822B 22
#define BIT_MASK_REG_MODE_8822B 0x3
#define BIT_REG_MODE_8822B(x)                                                  \
	(((x) & BIT_MASK_REG_MODE_8822B) << BIT_SHIFT_REG_MODE_8822B)
#define BITS_REG_MODE_8822B                                                    \
	(BIT_MASK_REG_MODE_8822B << BIT_SHIFT_REG_MODE_8822B)
#define BIT_CLEAR_REG_MODE_8822B(x) ((x) & (~BITS_REG_MODE_8822B))
#define BIT_GET_REG_MODE_8822B(x)                                              \
	(((x) >> BIT_SHIFT_REG_MODE_8822B) & BIT_MASK_REG_MODE_8822B)
#define BIT_SET_REG_MODE_8822B(x, v)                                           \
	(BIT_CLEAR_REG_MODE_8822B(x) | BIT_REG_MODE_8822B(v))

#define BIT_REG_EN_SP_8822B BIT(21)
#define BIT_REG_AUTO_L_8822B BIT(20)
#define BIT_SW18_SELD_BIT0_8822B BIT(19)
#define BIT_SW18_POWOCP_8822B BIT(18)

#define BIT_SHIFT_OCP_L1_8822B 15
#define BIT_MASK_OCP_L1_8822B 0x7
#define BIT_OCP_L1_8822B(x)                                                    \
	(((x) & BIT_MASK_OCP_L1_8822B) << BIT_SHIFT_OCP_L1_8822B)
#define BITS_OCP_L1_8822B (BIT_MASK_OCP_L1_8822B << BIT_SHIFT_OCP_L1_8822B)
#define BIT_CLEAR_OCP_L1_8822B(x) ((x) & (~BITS_OCP_L1_8822B))
#define BIT_GET_OCP_L1_8822B(x)                                                \
	(((x) >> BIT_SHIFT_OCP_L1_8822B) & BIT_MASK_OCP_L1_8822B)
#define BIT_SET_OCP_L1_8822B(x, v)                                             \
	(BIT_CLEAR_OCP_L1_8822B(x) | BIT_OCP_L1_8822B(v))

#define BIT_SHIFT_CF_L_8822B 13
#define BIT_MASK_CF_L_8822B 0x3
#define BIT_CF_L_8822B(x) (((x) & BIT_MASK_CF_L_8822B) << BIT_SHIFT_CF_L_8822B)
#define BITS_CF_L_8822B (BIT_MASK_CF_L_8822B << BIT_SHIFT_CF_L_8822B)
#define BIT_CLEAR_CF_L_8822B(x) ((x) & (~BITS_CF_L_8822B))
#define BIT_GET_CF_L_8822B(x)                                                  \
	(((x) >> BIT_SHIFT_CF_L_8822B) & BIT_MASK_CF_L_8822B)
#define BIT_SET_CF_L_8822B(x, v) (BIT_CLEAR_CF_L_8822B(x) | BIT_CF_L_8822B(v))

#define BIT_SW18_FPWM_8822B BIT(11)
#define BIT_SW18_SWEN_8822B BIT(9)
#define BIT_SW18_LDEN_8822B BIT(8)
#define BIT_MAC_ID_EN_8822B BIT(7)
#define BIT_AFE_BGEN_8822B BIT(0)

/* 2 REG_SYS_SWR_CTRL2_8822B */
#define BIT_POW_ZCD_L_8822B BIT(31)
#define BIT_AUTOZCD_L_8822B BIT(30)

#define BIT_SHIFT_REG_DELAY_8822B 28
#define BIT_MASK_REG_DELAY_8822B 0x3
#define BIT_REG_DELAY_8822B(x)                                                 \
	(((x) & BIT_MASK_REG_DELAY_8822B) << BIT_SHIFT_REG_DELAY_8822B)
#define BITS_REG_DELAY_8822B                                                   \
	(BIT_MASK_REG_DELAY_8822B << BIT_SHIFT_REG_DELAY_8822B)
#define BIT_CLEAR_REG_DELAY_8822B(x) ((x) & (~BITS_REG_DELAY_8822B))
#define BIT_GET_REG_DELAY_8822B(x)                                             \
	(((x) >> BIT_SHIFT_REG_DELAY_8822B) & BIT_MASK_REG_DELAY_8822B)
#define BIT_SET_REG_DELAY_8822B(x, v)                                          \
	(BIT_CLEAR_REG_DELAY_8822B(x) | BIT_REG_DELAY_8822B(v))

#define BIT_SHIFT_V15ADJ_L1_V1_8822B 24
#define BIT_MASK_V15ADJ_L1_V1_8822B 0x7
#define BIT_V15ADJ_L1_V1_8822B(x)                                              \
	(((x) & BIT_MASK_V15ADJ_L1_V1_8822B) << BIT_SHIFT_V15ADJ_L1_V1_8822B)
#define BITS_V15ADJ_L1_V1_8822B                                                \
	(BIT_MASK_V15ADJ_L1_V1_8822B << BIT_SHIFT_V15ADJ_L1_V1_8822B)
#define BIT_CLEAR_V15ADJ_L1_V1_8822B(x) ((x) & (~BITS_V15ADJ_L1_V1_8822B))
#define BIT_GET_V15ADJ_L1_V1_8822B(x)                                          \
	(((x) >> BIT_SHIFT_V15ADJ_L1_V1_8822B) & BIT_MASK_V15ADJ_L1_V1_8822B)
#define BIT_SET_V15ADJ_L1_V1_8822B(x, v)                                       \
	(BIT_CLEAR_V15ADJ_L1_V1_8822B(x) | BIT_V15ADJ_L1_V1_8822B(v))

#define BIT_SHIFT_VOL_L1_V1_8822B 20
#define BIT_MASK_VOL_L1_V1_8822B 0xf
#define BIT_VOL_L1_V1_8822B(x)                                                 \
	(((x) & BIT_MASK_VOL_L1_V1_8822B) << BIT_SHIFT_VOL_L1_V1_8822B)
#define BITS_VOL_L1_V1_8822B                                                   \
	(BIT_MASK_VOL_L1_V1_8822B << BIT_SHIFT_VOL_L1_V1_8822B)
#define BIT_CLEAR_VOL_L1_V1_8822B(x) ((x) & (~BITS_VOL_L1_V1_8822B))
#define BIT_GET_VOL_L1_V1_8822B(x)                                             \
	(((x) >> BIT_SHIFT_VOL_L1_V1_8822B) & BIT_MASK_VOL_L1_V1_8822B)
#define BIT_SET_VOL_L1_V1_8822B(x, v)                                          \
	(BIT_CLEAR_VOL_L1_V1_8822B(x) | BIT_VOL_L1_V1_8822B(v))

#define BIT_SHIFT_IN_L1_V1_8822B 17
#define BIT_MASK_IN_L1_V1_8822B 0x7
#define BIT_IN_L1_V1_8822B(x)                                                  \
	(((x) & BIT_MASK_IN_L1_V1_8822B) << BIT_SHIFT_IN_L1_V1_8822B)
#define BITS_IN_L1_V1_8822B                                                    \
	(BIT_MASK_IN_L1_V1_8822B << BIT_SHIFT_IN_L1_V1_8822B)
#define BIT_CLEAR_IN_L1_V1_8822B(x) ((x) & (~BITS_IN_L1_V1_8822B))
#define BIT_GET_IN_L1_V1_8822B(x)                                              \
	(((x) >> BIT_SHIFT_IN_L1_V1_8822B) & BIT_MASK_IN_L1_V1_8822B)
#define BIT_SET_IN_L1_V1_8822B(x, v)                                           \
	(BIT_CLEAR_IN_L1_V1_8822B(x) | BIT_IN_L1_V1_8822B(v))

#define BIT_SHIFT_TBOX_L1_8822B 15
#define BIT_MASK_TBOX_L1_8822B 0x3
#define BIT_TBOX_L1_8822B(x)                                                   \
	(((x) & BIT_MASK_TBOX_L1_8822B) << BIT_SHIFT_TBOX_L1_8822B)
#define BITS_TBOX_L1_8822B (BIT_MASK_TBOX_L1_8822B << BIT_SHIFT_TBOX_L1_8822B)
#define BIT_CLEAR_TBOX_L1_8822B(x) ((x) & (~BITS_TBOX_L1_8822B))
#define BIT_GET_TBOX_L1_8822B(x)                                               \
	(((x) >> BIT_SHIFT_TBOX_L1_8822B) & BIT_MASK_TBOX_L1_8822B)
#define BIT_SET_TBOX_L1_8822B(x, v)                                            \
	(BIT_CLEAR_TBOX_L1_8822B(x) | BIT_TBOX_L1_8822B(v))

#define BIT_SW18_SEL_8822B BIT(13)

/* 2 REG_NOT_VALID_8822B */
#define BIT_SW18_SD_8822B BIT(10)

#define BIT_SHIFT_R3_L_8822B 7
#define BIT_MASK_R3_L_8822B 0x3
#define BIT_R3_L_8822B(x) (((x) & BIT_MASK_R3_L_8822B) << BIT_SHIFT_R3_L_8822B)
#define BITS_R3_L_8822B (BIT_MASK_R3_L_8822B << BIT_SHIFT_R3_L_8822B)
#define BIT_CLEAR_R3_L_8822B(x) ((x) & (~BITS_R3_L_8822B))
#define BIT_GET_R3_L_8822B(x)                                                  \
	(((x) >> BIT_SHIFT_R3_L_8822B) & BIT_MASK_R3_L_8822B)
#define BIT_SET_R3_L_8822B(x, v) (BIT_CLEAR_R3_L_8822B(x) | BIT_R3_L_8822B(v))

#define BIT_SHIFT_SW18_R2_8822B 5
#define BIT_MASK_SW18_R2_8822B 0x3
#define BIT_SW18_R2_8822B(x)                                                   \
	(((x) & BIT_MASK_SW18_R2_8822B) << BIT_SHIFT_SW18_R2_8822B)
#define BITS_SW18_R2_8822B (BIT_MASK_SW18_R2_8822B << BIT_SHIFT_SW18_R2_8822B)
#define BIT_CLEAR_SW18_R2_8822B(x) ((x) & (~BITS_SW18_R2_8822B))
#define BIT_GET_SW18_R2_8822B(x)                                               \
	(((x) >> BIT_SHIFT_SW18_R2_8822B) & BIT_MASK_SW18_R2_8822B)
#define BIT_SET_SW18_R2_8822B(x, v)                                            \
	(BIT_CLEAR_SW18_R2_8822B(x) | BIT_SW18_R2_8822B(v))

#define BIT_SHIFT_SW18_R1_8822B 3
#define BIT_MASK_SW18_R1_8822B 0x3
#define BIT_SW18_R1_8822B(x)                                                   \
	(((x) & BIT_MASK_SW18_R1_8822B) << BIT_SHIFT_SW18_R1_8822B)
#define BITS_SW18_R1_8822B (BIT_MASK_SW18_R1_8822B << BIT_SHIFT_SW18_R1_8822B)
#define BIT_CLEAR_SW18_R1_8822B(x) ((x) & (~BITS_SW18_R1_8822B))
#define BIT_GET_SW18_R1_8822B(x)                                               \
	(((x) >> BIT_SHIFT_SW18_R1_8822B) & BIT_MASK_SW18_R1_8822B)
#define BIT_SET_SW18_R1_8822B(x, v)                                            \
	(BIT_CLEAR_SW18_R1_8822B(x) | BIT_SW18_R1_8822B(v))

#define BIT_SHIFT_C3_L_C3_8822B 1
#define BIT_MASK_C3_L_C3_8822B 0x3
#define BIT_C3_L_C3_8822B(x)                                                   \
	(((x) & BIT_MASK_C3_L_C3_8822B) << BIT_SHIFT_C3_L_C3_8822B)
#define BITS_C3_L_C3_8822B (BIT_MASK_C3_L_C3_8822B << BIT_SHIFT_C3_L_C3_8822B)
#define BIT_CLEAR_C3_L_C3_8822B(x) ((x) & (~BITS_C3_L_C3_8822B))
#define BIT_GET_C3_L_C3_8822B(x)                                               \
	(((x) >> BIT_SHIFT_C3_L_C3_8822B) & BIT_MASK_C3_L_C3_8822B)
#define BIT_SET_C3_L_C3_8822B(x, v)                                            \
	(BIT_CLEAR_C3_L_C3_8822B(x) | BIT_C3_L_C3_8822B(v))

#define BIT_C2_L_BIT1_8822B BIT(0)

/* 2 REG_SYS_SWR_CTRL3_8822B */
#define BIT_SPS18_OCP_DIS_8822B BIT(31)

#define BIT_SHIFT_SPS18_OCP_TH_8822B 16
#define BIT_MASK_SPS18_OCP_TH_8822B 0x7fff
#define BIT_SPS18_OCP_TH_8822B(x)                                              \
	(((x) & BIT_MASK_SPS18_OCP_TH_8822B) << BIT_SHIFT_SPS18_OCP_TH_8822B)
#define BITS_SPS18_OCP_TH_8822B                                                \
	(BIT_MASK_SPS18_OCP_TH_8822B << BIT_SHIFT_SPS18_OCP_TH_8822B)
#define BIT_CLEAR_SPS18_OCP_TH_8822B(x) ((x) & (~BITS_SPS18_OCP_TH_8822B))
#define BIT_GET_SPS18_OCP_TH_8822B(x)                                          \
	(((x) >> BIT_SHIFT_SPS18_OCP_TH_8822B) & BIT_MASK_SPS18_OCP_TH_8822B)
#define BIT_SET_SPS18_OCP_TH_8822B(x, v)                                       \
	(BIT_CLEAR_SPS18_OCP_TH_8822B(x) | BIT_SPS18_OCP_TH_8822B(v))

#define BIT_SHIFT_OCP_WINDOW_8822B 0
#define BIT_MASK_OCP_WINDOW_8822B 0xffff
#define BIT_OCP_WINDOW_8822B(x)                                                \
	(((x) & BIT_MASK_OCP_WINDOW_8822B) << BIT_SHIFT_OCP_WINDOW_8822B)
#define BITS_OCP_WINDOW_8822B                                                  \
	(BIT_MASK_OCP_WINDOW_8822B << BIT_SHIFT_OCP_WINDOW_8822B)
#define BIT_CLEAR_OCP_WINDOW_8822B(x) ((x) & (~BITS_OCP_WINDOW_8822B))
#define BIT_GET_OCP_WINDOW_8822B(x)                                            \
	(((x) >> BIT_SHIFT_OCP_WINDOW_8822B) & BIT_MASK_OCP_WINDOW_8822B)
#define BIT_SET_OCP_WINDOW_8822B(x, v)                                         \
	(BIT_CLEAR_OCP_WINDOW_8822B(x) | BIT_OCP_WINDOW_8822B(v))

/* 2 REG_RSV_CTRL_8822B */
#define BIT_HREG_DBG_8822B BIT(23)
#define BIT_WLMCUIOIF_8822B BIT(8)
#define BIT_LOCK_ALL_EN_8822B BIT(7)
#define BIT_R_DIS_PRST_8822B BIT(6)
#define BIT_WLOCK_1C_B6_8822B BIT(5)
#define BIT_WLOCK_40_8822B BIT(4)
#define BIT_WLOCK_08_8822B BIT(3)
#define BIT_WLOCK_04_8822B BIT(2)
#define BIT_WLOCK_00_8822B BIT(1)
#define BIT_WLOCK_ALL_8822B BIT(0)

/* 2 REG_RF_CTRL_8822B */
#define BIT_RF_SDMRSTB_8822B BIT(2)
#define BIT_RF_RSTB_8822B BIT(1)
#define BIT_RF_EN_8822B BIT(0)

/* 2 REG_AFE_LDO_CTRL_8822B */

#define BIT_SHIFT_LPLDH12_RSV_8822B 29
#define BIT_MASK_LPLDH12_RSV_8822B 0x7
#define BIT_LPLDH12_RSV_8822B(x)                                               \
	(((x) & BIT_MASK_LPLDH12_RSV_8822B) << BIT_SHIFT_LPLDH12_RSV_8822B)
#define BITS_LPLDH12_RSV_8822B                                                 \
	(BIT_MASK_LPLDH12_RSV_8822B << BIT_SHIFT_LPLDH12_RSV_8822B)
#define BIT_CLEAR_LPLDH12_RSV_8822B(x) ((x) & (~BITS_LPLDH12_RSV_8822B))
#define BIT_GET_LPLDH12_RSV_8822B(x)                                           \
	(((x) >> BIT_SHIFT_LPLDH12_RSV_8822B) & BIT_MASK_LPLDH12_RSV_8822B)
#define BIT_SET_LPLDH12_RSV_8822B(x, v)                                        \
	(BIT_CLEAR_LPLDH12_RSV_8822B(x) | BIT_LPLDH12_RSV_8822B(v))

#define BIT_LPLDH12_SLP_8822B BIT(28)

#define BIT_SHIFT_LPLDH12_VADJ_8822B 24
#define BIT_MASK_LPLDH12_VADJ_8822B 0xf
#define BIT_LPLDH12_VADJ_8822B(x)                                              \
	(((x) & BIT_MASK_LPLDH12_VADJ_8822B) << BIT_SHIFT_LPLDH12_VADJ_8822B)
#define BITS_LPLDH12_VADJ_8822B                                                \
	(BIT_MASK_LPLDH12_VADJ_8822B << BIT_SHIFT_LPLDH12_VADJ_8822B)
#define BIT_CLEAR_LPLDH12_VADJ_8822B(x) ((x) & (~BITS_LPLDH12_VADJ_8822B))
#define BIT_GET_LPLDH12_VADJ_8822B(x)                                          \
	(((x) >> BIT_SHIFT_LPLDH12_VADJ_8822B) & BIT_MASK_LPLDH12_VADJ_8822B)
#define BIT_SET_LPLDH12_VADJ_8822B(x, v)                                       \
	(BIT_CLEAR_LPLDH12_VADJ_8822B(x) | BIT_LPLDH12_VADJ_8822B(v))

#define BIT_LDH12_EN_8822B BIT(16)
#define BIT_WLBBOFF_BIG_PWC_EN_8822B BIT(14)
#define BIT_WLBBOFF_SMALL_PWC_EN_8822B BIT(13)
#define BIT_WLMACOFF_BIG_PWC_EN_8822B BIT(12)
#define BIT_WLPON_PWC_EN_8822B BIT(11)
#define BIT_POW_REGU_P1_8822B BIT(10)
#define BIT_LDOV12W_EN_8822B BIT(8)
#define BIT_EX_XTAL_DRV_DIGI_8822B BIT(7)
#define BIT_EX_XTAL_DRV_USB_8822B BIT(6)
#define BIT_EX_XTAL_DRV_AFE_8822B BIT(5)
#define BIT_EX_XTAL_DRV_RF2_8822B BIT(4)
#define BIT_EX_XTAL_DRV_RF1_8822B BIT(3)
#define BIT_POW_REGU_P0_8822B BIT(2)

/* 2 REG_NOT_VALID_8822B */
#define BIT_POW_PLL_LDO_8822B BIT(0)

/* 2 REG_AFE_CTRL1_8822B */
#define BIT_AGPIO_GPE_8822B BIT(31)

#define BIT_SHIFT_XTAL_CAP_XI_8822B 25
#define BIT_MASK_XTAL_CAP_XI_8822B 0x3f
#define BIT_XTAL_CAP_XI_8822B(x)                                               \
	(((x) & BIT_MASK_XTAL_CAP_XI_8822B) << BIT_SHIFT_XTAL_CAP_XI_8822B)
#define BITS_XTAL_CAP_XI_8822B                                                 \
	(BIT_MASK_XTAL_CAP_XI_8822B << BIT_SHIFT_XTAL_CAP_XI_8822B)
#define BIT_CLEAR_XTAL_CAP_XI_8822B(x) ((x) & (~BITS_XTAL_CAP_XI_8822B))
#define BIT_GET_XTAL_CAP_XI_8822B(x)                                           \
	(((x) >> BIT_SHIFT_XTAL_CAP_XI_8822B) & BIT_MASK_XTAL_CAP_XI_8822B)
#define BIT_SET_XTAL_CAP_XI_8822B(x, v)                                        \
	(BIT_CLEAR_XTAL_CAP_XI_8822B(x) | BIT_XTAL_CAP_XI_8822B(v))

#define BIT_SHIFT_XTAL_DRV_DIGI_8822B 23
#define BIT_MASK_XTAL_DRV_DIGI_8822B 0x3
#define BIT_XTAL_DRV_DIGI_8822B(x)                                             \
	(((x) & BIT_MASK_XTAL_DRV_DIGI_8822B) << BIT_SHIFT_XTAL_DRV_DIGI_8822B)
#define BITS_XTAL_DRV_DIGI_8822B                                               \
	(BIT_MASK_XTAL_DRV_DIGI_8822B << BIT_SHIFT_XTAL_DRV_DIGI_8822B)
#define BIT_CLEAR_XTAL_DRV_DIGI_8822B(x) ((x) & (~BITS_XTAL_DRV_DIGI_8822B))
#define BIT_GET_XTAL_DRV_DIGI_8822B(x)                                         \
	(((x) >> BIT_SHIFT_XTAL_DRV_DIGI_8822B) & BIT_MASK_XTAL_DRV_DIGI_8822B)
#define BIT_SET_XTAL_DRV_DIGI_8822B(x, v)                                      \
	(BIT_CLEAR_XTAL_DRV_DIGI_8822B(x) | BIT_XTAL_DRV_DIGI_8822B(v))

#define BIT_XTAL_DRV_USB_BIT1_8822B BIT(22)

#define BIT_SHIFT_MAC_CLK_SEL_8822B 20
#define BIT_MASK_MAC_CLK_SEL_8822B 0x3
#define BIT_MAC_CLK_SEL_8822B(x)                                               \
	(((x) & BIT_MASK_MAC_CLK_SEL_8822B) << BIT_SHIFT_MAC_CLK_SEL_8822B)
#define BITS_MAC_CLK_SEL_8822B                                                 \
	(BIT_MASK_MAC_CLK_SEL_8822B << BIT_SHIFT_MAC_CLK_SEL_8822B)
#define BIT_CLEAR_MAC_CLK_SEL_8822B(x) ((x) & (~BITS_MAC_CLK_SEL_8822B))
#define BIT_GET_MAC_CLK_SEL_8822B(x)                                           \
	(((x) >> BIT_SHIFT_MAC_CLK_SEL_8822B) & BIT_MASK_MAC_CLK_SEL_8822B)
#define BIT_SET_MAC_CLK_SEL_8822B(x, v)                                        \
	(BIT_CLEAR_MAC_CLK_SEL_8822B(x) | BIT_MAC_CLK_SEL_8822B(v))

#define BIT_XTAL_DRV_USB_BIT0_8822B BIT(19)

#define BIT_SHIFT_XTAL_DRV_AFE_8822B 17
#define BIT_MASK_XTAL_DRV_AFE_8822B 0x3
#define BIT_XTAL_DRV_AFE_8822B(x)                                              \
	(((x) & BIT_MASK_XTAL_DRV_AFE_8822B) << BIT_SHIFT_XTAL_DRV_AFE_8822B)
#define BITS_XTAL_DRV_AFE_8822B                                                \
	(BIT_MASK_XTAL_DRV_AFE_8822B << BIT_SHIFT_XTAL_DRV_AFE_8822B)
#define BIT_CLEAR_XTAL_DRV_AFE_8822B(x) ((x) & (~BITS_XTAL_DRV_AFE_8822B))
#define BIT_GET_XTAL_DRV_AFE_8822B(x)                                          \
	(((x) >> BIT_SHIFT_XTAL_DRV_AFE_8822B) & BIT_MASK_XTAL_DRV_AFE_8822B)
#define BIT_SET_XTAL_DRV_AFE_8822B(x, v)                                       \
	(BIT_CLEAR_XTAL_DRV_AFE_8822B(x) | BIT_XTAL_DRV_AFE_8822B(v))

#define BIT_SHIFT_XTAL_DRV_RF2_8822B 15
#define BIT_MASK_XTAL_DRV_RF2_8822B 0x3
#define BIT_XTAL_DRV_RF2_8822B(x)                                              \
	(((x) & BIT_MASK_XTAL_DRV_RF2_8822B) << BIT_SHIFT_XTAL_DRV_RF2_8822B)
#define BITS_XTAL_DRV_RF2_8822B                                                \
	(BIT_MASK_XTAL_DRV_RF2_8822B << BIT_SHIFT_XTAL_DRV_RF2_8822B)
#define BIT_CLEAR_XTAL_DRV_RF2_8822B(x) ((x) & (~BITS_XTAL_DRV_RF2_8822B))
#define BIT_GET_XTAL_DRV_RF2_8822B(x)                                          \
	(((x) >> BIT_SHIFT_XTAL_DRV_RF2_8822B) & BIT_MASK_XTAL_DRV_RF2_8822B)
#define BIT_SET_XTAL_DRV_RF2_8822B(x, v)                                       \
	(BIT_CLEAR_XTAL_DRV_RF2_8822B(x) | BIT_XTAL_DRV_RF2_8822B(v))

#define BIT_SHIFT_XTAL_DRV_RF1_8822B 13
#define BIT_MASK_XTAL_DRV_RF1_8822B 0x3
#define BIT_XTAL_DRV_RF1_8822B(x)                                              \
	(((x) & BIT_MASK_XTAL_DRV_RF1_8822B) << BIT_SHIFT_XTAL_DRV_RF1_8822B)
#define BITS_XTAL_DRV_RF1_8822B                                                \
	(BIT_MASK_XTAL_DRV_RF1_8822B << BIT_SHIFT_XTAL_DRV_RF1_8822B)
#define BIT_CLEAR_XTAL_DRV_RF1_8822B(x) ((x) & (~BITS_XTAL_DRV_RF1_8822B))
#define BIT_GET_XTAL_DRV_RF1_8822B(x)                                          \
	(((x) >> BIT_SHIFT_XTAL_DRV_RF1_8822B) & BIT_MASK_XTAL_DRV_RF1_8822B)
#define BIT_SET_XTAL_DRV_RF1_8822B(x, v)                                       \
	(BIT_CLEAR_XTAL_DRV_RF1_8822B(x) | BIT_XTAL_DRV_RF1_8822B(v))

#define BIT_XTAL_DELAY_DIGI_8822B BIT(12)
#define BIT_XTAL_DELAY_USB_8822B BIT(11)
#define BIT_XTAL_DELAY_AFE_8822B BIT(10)

#define BIT_SHIFT_XTAL_LDO_VREF_8822B 7
#define BIT_MASK_XTAL_LDO_VREF_8822B 0x7
#define BIT_XTAL_LDO_VREF_8822B(x)                                             \
	(((x) & BIT_MASK_XTAL_LDO_VREF_8822B) << BIT_SHIFT_XTAL_LDO_VREF_8822B)
#define BITS_XTAL_LDO_VREF_8822B                                               \
	(BIT_MASK_XTAL_LDO_VREF_8822B << BIT_SHIFT_XTAL_LDO_VREF_8822B)
#define BIT_CLEAR_XTAL_LDO_VREF_8822B(x) ((x) & (~BITS_XTAL_LDO_VREF_8822B))
#define BIT_GET_XTAL_LDO_VREF_8822B(x)                                         \
	(((x) >> BIT_SHIFT_XTAL_LDO_VREF_8822B) & BIT_MASK_XTAL_LDO_VREF_8822B)
#define BIT_SET_XTAL_LDO_VREF_8822B(x, v)                                      \
	(BIT_CLEAR_XTAL_LDO_VREF_8822B(x) | BIT_XTAL_LDO_VREF_8822B(v))

#define BIT_XTAL_XQSEL_RF_8822B BIT(6)
#define BIT_XTAL_XQSEL_8822B BIT(5)

#define BIT_SHIFT_XTAL_GMN_V2_8822B 3
#define BIT_MASK_XTAL_GMN_V2_8822B 0x3
#define BIT_XTAL_GMN_V2_8822B(x)                                               \
	(((x) & BIT_MASK_XTAL_GMN_V2_8822B) << BIT_SHIFT_XTAL_GMN_V2_8822B)
#define BITS_XTAL_GMN_V2_8822B                                                 \
	(BIT_MASK_XTAL_GMN_V2_8822B << BIT_SHIFT_XTAL_GMN_V2_8822B)
#define BIT_CLEAR_XTAL_GMN_V2_8822B(x) ((x) & (~BITS_XTAL_GMN_V2_8822B))
#define BIT_GET_XTAL_GMN_V2_8822B(x)                                           \
	(((x) >> BIT_SHIFT_XTAL_GMN_V2_8822B) & BIT_MASK_XTAL_GMN_V2_8822B)
#define BIT_SET_XTAL_GMN_V2_8822B(x, v)                                        \
	(BIT_CLEAR_XTAL_GMN_V2_8822B(x) | BIT_XTAL_GMN_V2_8822B(v))

#define BIT_SHIFT_XTAL_GMP_V2_8822B 1
#define BIT_MASK_XTAL_GMP_V2_8822B 0x3
#define BIT_XTAL_GMP_V2_8822B(x)                                               \
	(((x) & BIT_MASK_XTAL_GMP_V2_8822B) << BIT_SHIFT_XTAL_GMP_V2_8822B)
#define BITS_XTAL_GMP_V2_8822B                                                 \
	(BIT_MASK_XTAL_GMP_V2_8822B << BIT_SHIFT_XTAL_GMP_V2_8822B)
#define BIT_CLEAR_XTAL_GMP_V2_8822B(x) ((x) & (~BITS_XTAL_GMP_V2_8822B))
#define BIT_GET_XTAL_GMP_V2_8822B(x)                                           \
	(((x) >> BIT_SHIFT_XTAL_GMP_V2_8822B) & BIT_MASK_XTAL_GMP_V2_8822B)
#define BIT_SET_XTAL_GMP_V2_8822B(x, v)                                        \
	(BIT_CLEAR_XTAL_GMP_V2_8822B(x) | BIT_XTAL_GMP_V2_8822B(v))

#define BIT_XTAL_EN_8822B BIT(0)

/* 2 REG_AFE_CTRL2_8822B */

#define BIT_SHIFT_REG_C3_V4_8822B 30
#define BIT_MASK_REG_C3_V4_8822B 0x3
#define BIT_REG_C3_V4_8822B(x)                                                 \
	(((x) & BIT_MASK_REG_C3_V4_8822B) << BIT_SHIFT_REG_C3_V4_8822B)
#define BITS_REG_C3_V4_8822B                                                   \
	(BIT_MASK_REG_C3_V4_8822B << BIT_SHIFT_REG_C3_V4_8822B)
#define BIT_CLEAR_REG_C3_V4_8822B(x) ((x) & (~BITS_REG_C3_V4_8822B))
#define BIT_GET_REG_C3_V4_8822B(x)                                             \
	(((x) >> BIT_SHIFT_REG_C3_V4_8822B) & BIT_MASK_REG_C3_V4_8822B)
#define BIT_SET_REG_C3_V4_8822B(x, v)                                          \
	(BIT_CLEAR_REG_C3_V4_8822B(x) | BIT_REG_C3_V4_8822B(v))

#define BIT_REG_CP_BIT1_8822B BIT(29)

#define BIT_SHIFT_REG_RS_V4_8822B 26
#define BIT_MASK_REG_RS_V4_8822B 0x7
#define BIT_REG_RS_V4_8822B(x)                                                 \
	(((x) & BIT_MASK_REG_RS_V4_8822B) << BIT_SHIFT_REG_RS_V4_8822B)
#define BITS_REG_RS_V4_8822B                                                   \
	(BIT_MASK_REG_RS_V4_8822B << BIT_SHIFT_REG_RS_V4_8822B)
#define BIT_CLEAR_REG_RS_V4_8822B(x) ((x) & (~BITS_REG_RS_V4_8822B))
#define BIT_GET_REG_RS_V4_8822B(x)                                             \
	(((x) >> BIT_SHIFT_REG_RS_V4_8822B) & BIT_MASK_REG_RS_V4_8822B)
#define BIT_SET_REG_RS_V4_8822B(x, v)                                          \
	(BIT_CLEAR_REG_RS_V4_8822B(x) | BIT_REG_RS_V4_8822B(v))

#define BIT_SHIFT_REG__CS_8822B 24
#define BIT_MASK_REG__CS_8822B 0x3
#define BIT_REG__CS_8822B(x)                                                   \
	(((x) & BIT_MASK_REG__CS_8822B) << BIT_SHIFT_REG__CS_8822B)
#define BITS_REG__CS_8822B (BIT_MASK_REG__CS_8822B << BIT_SHIFT_REG__CS_8822B)
#define BIT_CLEAR_REG__CS_8822B(x) ((x) & (~BITS_REG__CS_8822B))
#define BIT_GET_REG__CS_8822B(x)                                               \
	(((x) >> BIT_SHIFT_REG__CS_8822B) & BIT_MASK_REG__CS_8822B)
#define BIT_SET_REG__CS_8822B(x, v)                                            \
	(BIT_CLEAR_REG__CS_8822B(x) | BIT_REG__CS_8822B(v))

#define BIT_SHIFT_REG_CP_OFFSET_8822B 21
#define BIT_MASK_REG_CP_OFFSET_8822B 0x7
#define BIT_REG_CP_OFFSET_8822B(x)                                             \
	(((x) & BIT_MASK_REG_CP_OFFSET_8822B) << BIT_SHIFT_REG_CP_OFFSET_8822B)
#define BITS_REG_CP_OFFSET_8822B                                               \
	(BIT_MASK_REG_CP_OFFSET_8822B << BIT_SHIFT_REG_CP_OFFSET_8822B)
#define BIT_CLEAR_REG_CP_OFFSET_8822B(x) ((x) & (~BITS_REG_CP_OFFSET_8822B))
#define BIT_GET_REG_CP_OFFSET_8822B(x)                                         \
	(((x) >> BIT_SHIFT_REG_CP_OFFSET_8822B) & BIT_MASK_REG_CP_OFFSET_8822B)
#define BIT_SET_REG_CP_OFFSET_8822B(x, v)                                      \
	(BIT_CLEAR_REG_CP_OFFSET_8822B(x) | BIT_REG_CP_OFFSET_8822B(v))

#define BIT_SHIFT_CP_BIAS_8822B 18
#define BIT_MASK_CP_BIAS_8822B 0x7
#define BIT_CP_BIAS_8822B(x)                                                   \
	(((x) & BIT_MASK_CP_BIAS_8822B) << BIT_SHIFT_CP_BIAS_8822B)
#define BITS_CP_BIAS_8822B (BIT_MASK_CP_BIAS_8822B << BIT_SHIFT_CP_BIAS_8822B)
#define BIT_CLEAR_CP_BIAS_8822B(x) ((x) & (~BITS_CP_BIAS_8822B))
#define BIT_GET_CP_BIAS_8822B(x)                                               \
	(((x) >> BIT_SHIFT_CP_BIAS_8822B) & BIT_MASK_CP_BIAS_8822B)
#define BIT_SET_CP_BIAS_8822B(x, v)                                            \
	(BIT_CLEAR_CP_BIAS_8822B(x) | BIT_CP_BIAS_8822B(v))

#define BIT_REG_IDOUBLE_V2_8822B BIT(17)
#define BIT_EN_SYN_8822B BIT(16)

#define BIT_SHIFT_MCCO_8822B 14
#define BIT_MASK_MCCO_8822B 0x3
#define BIT_MCCO_8822B(x) (((x) & BIT_MASK_MCCO_8822B) << BIT_SHIFT_MCCO_8822B)
#define BITS_MCCO_8822B (BIT_MASK_MCCO_8822B << BIT_SHIFT_MCCO_8822B)
#define BIT_CLEAR_MCCO_8822B(x) ((x) & (~BITS_MCCO_8822B))
#define BIT_GET_MCCO_8822B(x)                                                  \
	(((x) >> BIT_SHIFT_MCCO_8822B) & BIT_MASK_MCCO_8822B)
#define BIT_SET_MCCO_8822B(x, v) (BIT_CLEAR_MCCO_8822B(x) | BIT_MCCO_8822B(v))

#define BIT_SHIFT_REG_LDO_SEL_8822B 12
#define BIT_MASK_REG_LDO_SEL_8822B 0x3
#define BIT_REG_LDO_SEL_8822B(x)                                               \
	(((x) & BIT_MASK_REG_LDO_SEL_8822B) << BIT_SHIFT_REG_LDO_SEL_8822B)
#define BITS_REG_LDO_SEL_8822B                                                 \
	(BIT_MASK_REG_LDO_SEL_8822B << BIT_SHIFT_REG_LDO_SEL_8822B)
#define BIT_CLEAR_REG_LDO_SEL_8822B(x) ((x) & (~BITS_REG_LDO_SEL_8822B))
#define BIT_GET_REG_LDO_SEL_8822B(x)                                           \
	(((x) >> BIT_SHIFT_REG_LDO_SEL_8822B) & BIT_MASK_REG_LDO_SEL_8822B)
#define BIT_SET_REG_LDO_SEL_8822B(x, v)                                        \
	(BIT_CLEAR_REG_LDO_SEL_8822B(x) | BIT_REG_LDO_SEL_8822B(v))

#define BIT_REG_KVCO_V2_8822B BIT(10)
#define BIT_AGPIO_GPO_8822B BIT(9)

#define BIT_SHIFT_AGPIO_DRV_8822B 7
#define BIT_MASK_AGPIO_DRV_8822B 0x3
#define BIT_AGPIO_DRV_8822B(x)                                                 \
	(((x) & BIT_MASK_AGPIO_DRV_8822B) << BIT_SHIFT_AGPIO_DRV_8822B)
#define BITS_AGPIO_DRV_8822B                                                   \
	(BIT_MASK_AGPIO_DRV_8822B << BIT_SHIFT_AGPIO_DRV_8822B)
#define BIT_CLEAR_AGPIO_DRV_8822B(x) ((x) & (~BITS_AGPIO_DRV_8822B))
#define BIT_GET_AGPIO_DRV_8822B(x)                                             \
	(((x) >> BIT_SHIFT_AGPIO_DRV_8822B) & BIT_MASK_AGPIO_DRV_8822B)
#define BIT_SET_AGPIO_DRV_8822B(x, v)                                          \
	(BIT_CLEAR_AGPIO_DRV_8822B(x) | BIT_AGPIO_DRV_8822B(v))

#define BIT_SHIFT_XTAL_CAP_XO_8822B 1
#define BIT_MASK_XTAL_CAP_XO_8822B 0x3f
#define BIT_XTAL_CAP_XO_8822B(x)                                               \
	(((x) & BIT_MASK_XTAL_CAP_XO_8822B) << BIT_SHIFT_XTAL_CAP_XO_8822B)
#define BITS_XTAL_CAP_XO_8822B                                                 \
	(BIT_MASK_XTAL_CAP_XO_8822B << BIT_SHIFT_XTAL_CAP_XO_8822B)
#define BIT_CLEAR_XTAL_CAP_XO_8822B(x) ((x) & (~BITS_XTAL_CAP_XO_8822B))
#define BIT_GET_XTAL_CAP_XO_8822B(x)                                           \
	(((x) >> BIT_SHIFT_XTAL_CAP_XO_8822B) & BIT_MASK_XTAL_CAP_XO_8822B)
#define BIT_SET_XTAL_CAP_XO_8822B(x, v)                                        \
	(BIT_CLEAR_XTAL_CAP_XO_8822B(x) | BIT_XTAL_CAP_XO_8822B(v))

#define BIT_POW_PLL_8822B BIT(0)

/* 2 REG_AFE_CTRL3_8822B */

#define BIT_SHIFT_PS_8822B 7
#define BIT_MASK_PS_8822B 0x7
#define BIT_PS_8822B(x) (((x) & BIT_MASK_PS_8822B) << BIT_SHIFT_PS_8822B)
#define BITS_PS_8822B (BIT_MASK_PS_8822B << BIT_SHIFT_PS_8822B)
#define BIT_CLEAR_PS_8822B(x) ((x) & (~BITS_PS_8822B))
#define BIT_GET_PS_8822B(x) (((x) >> BIT_SHIFT_PS_8822B) & BIT_MASK_PS_8822B)
#define BIT_SET_PS_8822B(x, v) (BIT_CLEAR_PS_8822B(x) | BIT_PS_8822B(v))

#define BIT_PSEN_8822B BIT(6)
#define BIT_DOGENB_8822B BIT(5)
#define BIT_REG_MBIAS_8822B BIT(4)

#define BIT_SHIFT_REG_R3_V4_8822B 1
#define BIT_MASK_REG_R3_V4_8822B 0x7
#define BIT_REG_R3_V4_8822B(x)                                                 \
	(((x) & BIT_MASK_REG_R3_V4_8822B) << BIT_SHIFT_REG_R3_V4_8822B)
#define BITS_REG_R3_V4_8822B                                                   \
	(BIT_MASK_REG_R3_V4_8822B << BIT_SHIFT_REG_R3_V4_8822B)
#define BIT_CLEAR_REG_R3_V4_8822B(x) ((x) & (~BITS_REG_R3_V4_8822B))
#define BIT_GET_REG_R3_V4_8822B(x)                                             \
	(((x) >> BIT_SHIFT_REG_R3_V4_8822B) & BIT_MASK_REG_R3_V4_8822B)
#define BIT_SET_REG_R3_V4_8822B(x, v)                                          \
	(BIT_CLEAR_REG_R3_V4_8822B(x) | BIT_REG_R3_V4_8822B(v))

#define BIT_REG_CP_BIT0_8822B BIT(0)

/* 2 REG_EFUSE_CTRL_8822B */
#define BIT_EF_FLAG_8822B BIT(31)

#define BIT_SHIFT_EF_PGPD_8822B 28
#define BIT_MASK_EF_PGPD_8822B 0x7
#define BIT_EF_PGPD_8822B(x)                                                   \
	(((x) & BIT_MASK_EF_PGPD_8822B) << BIT_SHIFT_EF_PGPD_8822B)
#define BITS_EF_PGPD_8822B (BIT_MASK_EF_PGPD_8822B << BIT_SHIFT_EF_PGPD_8822B)
#define BIT_CLEAR_EF_PGPD_8822B(x) ((x) & (~BITS_EF_PGPD_8822B))
#define BIT_GET_EF_PGPD_8822B(x)                                               \
	(((x) >> BIT_SHIFT_EF_PGPD_8822B) & BIT_MASK_EF_PGPD_8822B)
#define BIT_SET_EF_PGPD_8822B(x, v)                                            \
	(BIT_CLEAR_EF_PGPD_8822B(x) | BIT_EF_PGPD_8822B(v))

#define BIT_SHIFT_EF_RDT_8822B 24
#define BIT_MASK_EF_RDT_8822B 0xf
#define BIT_EF_RDT_8822B(x)                                                    \
	(((x) & BIT_MASK_EF_RDT_8822B) << BIT_SHIFT_EF_RDT_8822B)
#define BITS_EF_RDT_8822B (BIT_MASK_EF_RDT_8822B << BIT_SHIFT_EF_RDT_8822B)
#define BIT_CLEAR_EF_RDT_8822B(x) ((x) & (~BITS_EF_RDT_8822B))
#define BIT_GET_EF_RDT_8822B(x)                                                \
	(((x) >> BIT_SHIFT_EF_RDT_8822B) & BIT_MASK_EF_RDT_8822B)
#define BIT_SET_EF_RDT_8822B(x, v)                                             \
	(BIT_CLEAR_EF_RDT_8822B(x) | BIT_EF_RDT_8822B(v))

#define BIT_SHIFT_EF_PGTS_8822B 20
#define BIT_MASK_EF_PGTS_8822B 0xf
#define BIT_EF_PGTS_8822B(x)                                                   \
	(((x) & BIT_MASK_EF_PGTS_8822B) << BIT_SHIFT_EF_PGTS_8822B)
#define BITS_EF_PGTS_8822B (BIT_MASK_EF_PGTS_8822B << BIT_SHIFT_EF_PGTS_8822B)
#define BIT_CLEAR_EF_PGTS_8822B(x) ((x) & (~BITS_EF_PGTS_8822B))
#define BIT_GET_EF_PGTS_8822B(x)                                               \
	(((x) >> BIT_SHIFT_EF_PGTS_8822B) & BIT_MASK_EF_PGTS_8822B)
#define BIT_SET_EF_PGTS_8822B(x, v)                                            \
	(BIT_CLEAR_EF_PGTS_8822B(x) | BIT_EF_PGTS_8822B(v))

#define BIT_EF_PDWN_8822B BIT(19)
#define BIT_EF_ALDEN_8822B BIT(18)

#define BIT_SHIFT_EF_ADDR_8822B 8
#define BIT_MASK_EF_ADDR_8822B 0x3ff
#define BIT_EF_ADDR_8822B(x)                                                   \
	(((x) & BIT_MASK_EF_ADDR_8822B) << BIT_SHIFT_EF_ADDR_8822B)
#define BITS_EF_ADDR_8822B (BIT_MASK_EF_ADDR_8822B << BIT_SHIFT_EF_ADDR_8822B)
#define BIT_CLEAR_EF_ADDR_8822B(x) ((x) & (~BITS_EF_ADDR_8822B))
#define BIT_GET_EF_ADDR_8822B(x)                                               \
	(((x) >> BIT_SHIFT_EF_ADDR_8822B) & BIT_MASK_EF_ADDR_8822B)
#define BIT_SET_EF_ADDR_8822B(x, v)                                            \
	(BIT_CLEAR_EF_ADDR_8822B(x) | BIT_EF_ADDR_8822B(v))

#define BIT_SHIFT_EF_DATA_8822B 0
#define BIT_MASK_EF_DATA_8822B 0xff
#define BIT_EF_DATA_8822B(x)                                                   \
	(((x) & BIT_MASK_EF_DATA_8822B) << BIT_SHIFT_EF_DATA_8822B)
#define BITS_EF_DATA_8822B (BIT_MASK_EF_DATA_8822B << BIT_SHIFT_EF_DATA_8822B)
#define BIT_CLEAR_EF_DATA_8822B(x) ((x) & (~BITS_EF_DATA_8822B))
#define BIT_GET_EF_DATA_8822B(x)                                               \
	(((x) >> BIT_SHIFT_EF_DATA_8822B) & BIT_MASK_EF_DATA_8822B)
#define BIT_SET_EF_DATA_8822B(x, v)                                            \
	(BIT_CLEAR_EF_DATA_8822B(x) | BIT_EF_DATA_8822B(v))

/* 2 REG_LDO_EFUSE_CTRL_8822B */
#define BIT_LDOE25_EN_8822B BIT(31)

#define BIT_SHIFT_LDOE25_V12ADJ_L_8822B 27
#define BIT_MASK_LDOE25_V12ADJ_L_8822B 0xf
#define BIT_LDOE25_V12ADJ_L_8822B(x)                                           \
	(((x) & BIT_MASK_LDOE25_V12ADJ_L_8822B)                                \
	 << BIT_SHIFT_LDOE25_V12ADJ_L_8822B)
#define BITS_LDOE25_V12ADJ_L_8822B                                             \
	(BIT_MASK_LDOE25_V12ADJ_L_8822B << BIT_SHIFT_LDOE25_V12ADJ_L_8822B)
#define BIT_CLEAR_LDOE25_V12ADJ_L_8822B(x) ((x) & (~BITS_LDOE25_V12ADJ_L_8822B))
#define BIT_GET_LDOE25_V12ADJ_L_8822B(x)                                       \
	(((x) >> BIT_SHIFT_LDOE25_V12ADJ_L_8822B) &                            \
	 BIT_MASK_LDOE25_V12ADJ_L_8822B)
#define BIT_SET_LDOE25_V12ADJ_L_8822B(x, v)                                    \
	(BIT_CLEAR_LDOE25_V12ADJ_L_8822B(x) | BIT_LDOE25_V12ADJ_L_8822B(v))

#define BIT_EF_CRES_SEL_8822B BIT(26)

#define BIT_SHIFT_EF_SCAN_START_V1_8822B 16
#define BIT_MASK_EF_SCAN_START_V1_8822B 0x3ff
#define BIT_EF_SCAN_START_V1_8822B(x)                                          \
	(((x) & BIT_MASK_EF_SCAN_START_V1_8822B)                               \
	 << BIT_SHIFT_EF_SCAN_START_V1_8822B)
#define BITS_EF_SCAN_START_V1_8822B                                            \
	(BIT_MASK_EF_SCAN_START_V1_8822B << BIT_SHIFT_EF_SCAN_START_V1_8822B)
#define BIT_CLEAR_EF_SCAN_START_V1_8822B(x)                                    \
	((x) & (~BITS_EF_SCAN_START_V1_8822B))
#define BIT_GET_EF_SCAN_START_V1_8822B(x)                                      \
	(((x) >> BIT_SHIFT_EF_SCAN_START_V1_8822B) &                           \
	 BIT_MASK_EF_SCAN_START_V1_8822B)
#define BIT_SET_EF_SCAN_START_V1_8822B(x, v)                                   \
	(BIT_CLEAR_EF_SCAN_START_V1_8822B(x) | BIT_EF_SCAN_START_V1_8822B(v))

#define BIT_SHIFT_EF_SCAN_END_8822B 12
#define BIT_MASK_EF_SCAN_END_8822B 0xf
#define BIT_EF_SCAN_END_8822B(x)                                               \
	(((x) & BIT_MASK_EF_SCAN_END_8822B) << BIT_SHIFT_EF_SCAN_END_8822B)
#define BITS_EF_SCAN_END_8822B                                                 \
	(BIT_MASK_EF_SCAN_END_8822B << BIT_SHIFT_EF_SCAN_END_8822B)
#define BIT_CLEAR_EF_SCAN_END_8822B(x) ((x) & (~BITS_EF_SCAN_END_8822B))
#define BIT_GET_EF_SCAN_END_8822B(x)                                           \
	(((x) >> BIT_SHIFT_EF_SCAN_END_8822B) & BIT_MASK_EF_SCAN_END_8822B)
#define BIT_SET_EF_SCAN_END_8822B(x, v)                                        \
	(BIT_CLEAR_EF_SCAN_END_8822B(x) | BIT_EF_SCAN_END_8822B(v))

#define BIT_EF_PD_DIS_8822B BIT(11)

#define BIT_SHIFT_EF_CELL_SEL_8822B 8
#define BIT_MASK_EF_CELL_SEL_8822B 0x3
#define BIT_EF_CELL_SEL_8822B(x)                                               \
	(((x) & BIT_MASK_EF_CELL_SEL_8822B) << BIT_SHIFT_EF_CELL_SEL_8822B)
#define BITS_EF_CELL_SEL_8822B                                                 \
	(BIT_MASK_EF_CELL_SEL_8822B << BIT_SHIFT_EF_CELL_SEL_8822B)
#define BIT_CLEAR_EF_CELL_SEL_8822B(x) ((x) & (~BITS_EF_CELL_SEL_8822B))
#define BIT_GET_EF_CELL_SEL_8822B(x)                                           \
	(((x) >> BIT_SHIFT_EF_CELL_SEL_8822B) & BIT_MASK_EF_CELL_SEL_8822B)
#define BIT_SET_EF_CELL_SEL_8822B(x, v)                                        \
	(BIT_CLEAR_EF_CELL_SEL_8822B(x) | BIT_EF_CELL_SEL_8822B(v))

#define BIT_EF_TRPT_8822B BIT(7)

#define BIT_SHIFT_EF_TTHD_8822B 0
#define BIT_MASK_EF_TTHD_8822B 0x7f
#define BIT_EF_TTHD_8822B(x)                                                   \
	(((x) & BIT_MASK_EF_TTHD_8822B) << BIT_SHIFT_EF_TTHD_8822B)
#define BITS_EF_TTHD_8822B (BIT_MASK_EF_TTHD_8822B << BIT_SHIFT_EF_TTHD_8822B)
#define BIT_CLEAR_EF_TTHD_8822B(x) ((x) & (~BITS_EF_TTHD_8822B))
#define BIT_GET_EF_TTHD_8822B(x)                                               \
	(((x) >> BIT_SHIFT_EF_TTHD_8822B) & BIT_MASK_EF_TTHD_8822B)
#define BIT_SET_EF_TTHD_8822B(x, v)                                            \
	(BIT_CLEAR_EF_TTHD_8822B(x) | BIT_EF_TTHD_8822B(v))

/* 2 REG_PWR_OPTION_CTRL_8822B */

#define BIT_SHIFT_DBG_SEL_V1_8822B 16
#define BIT_MASK_DBG_SEL_V1_8822B 0xff
#define BIT_DBG_SEL_V1_8822B(x)                                                \
	(((x) & BIT_MASK_DBG_SEL_V1_8822B) << BIT_SHIFT_DBG_SEL_V1_8822B)
#define BITS_DBG_SEL_V1_8822B                                                  \
	(BIT_MASK_DBG_SEL_V1_8822B << BIT_SHIFT_DBG_SEL_V1_8822B)
#define BIT_CLEAR_DBG_SEL_V1_8822B(x) ((x) & (~BITS_DBG_SEL_V1_8822B))
#define BIT_GET_DBG_SEL_V1_8822B(x)                                            \
	(((x) >> BIT_SHIFT_DBG_SEL_V1_8822B) & BIT_MASK_DBG_SEL_V1_8822B)
#define BIT_SET_DBG_SEL_V1_8822B(x, v)                                         \
	(BIT_CLEAR_DBG_SEL_V1_8822B(x) | BIT_DBG_SEL_V1_8822B(v))

#define BIT_SHIFT_DBG_SEL_BYTE_8822B 14
#define BIT_MASK_DBG_SEL_BYTE_8822B 0x3
#define BIT_DBG_SEL_BYTE_8822B(x)                                              \
	(((x) & BIT_MASK_DBG_SEL_BYTE_8822B) << BIT_SHIFT_DBG_SEL_BYTE_8822B)
#define BITS_DBG_SEL_BYTE_8822B                                                \
	(BIT_MASK_DBG_SEL_BYTE_8822B << BIT_SHIFT_DBG_SEL_BYTE_8822B)
#define BIT_CLEAR_DBG_SEL_BYTE_8822B(x) ((x) & (~BITS_DBG_SEL_BYTE_8822B))
#define BIT_GET_DBG_SEL_BYTE_8822B(x)                                          \
	(((x) >> BIT_SHIFT_DBG_SEL_BYTE_8822B) & BIT_MASK_DBG_SEL_BYTE_8822B)
#define BIT_SET_DBG_SEL_BYTE_8822B(x, v)                                       \
	(BIT_CLEAR_DBG_SEL_BYTE_8822B(x) | BIT_DBG_SEL_BYTE_8822B(v))

#define BIT_SHIFT_STD_L1_V1_8822B 12
#define BIT_MASK_STD_L1_V1_8822B 0x3
#define BIT_STD_L1_V1_8822B(x)                                                 \
	(((x) & BIT_MASK_STD_L1_V1_8822B) << BIT_SHIFT_STD_L1_V1_8822B)
#define BITS_STD_L1_V1_8822B                                                   \
	(BIT_MASK_STD_L1_V1_8822B << BIT_SHIFT_STD_L1_V1_8822B)
#define BIT_CLEAR_STD_L1_V1_8822B(x) ((x) & (~BITS_STD_L1_V1_8822B))
#define BIT_GET_STD_L1_V1_8822B(x)                                             \
	(((x) >> BIT_SHIFT_STD_L1_V1_8822B) & BIT_MASK_STD_L1_V1_8822B)
#define BIT_SET_STD_L1_V1_8822B(x, v)                                          \
	(BIT_CLEAR_STD_L1_V1_8822B(x) | BIT_STD_L1_V1_8822B(v))

#define BIT_SYSON_DBG_PAD_E2_8822B BIT(11)
#define BIT_SYSON_LED_PAD_E2_8822B BIT(10)
#define BIT_SYSON_GPEE_PAD_E2_8822B BIT(9)
#define BIT_SYSON_PCI_PAD_E2_8822B BIT(8)
#define BIT_AUTO_SW_LDO_VOL_EN_8822B BIT(7)

#define BIT_SHIFT_SYSON_SPS0WWV_WT_8822B 4
#define BIT_MASK_SYSON_SPS0WWV_WT_8822B 0x3
#define BIT_SYSON_SPS0WWV_WT_8822B(x)                                          \
	(((x) & BIT_MASK_SYSON_SPS0WWV_WT_8822B)                               \
	 << BIT_SHIFT_SYSON_SPS0WWV_WT_8822B)
#define BITS_SYSON_SPS0WWV_WT_8822B                                            \
	(BIT_MASK_SYSON_SPS0WWV_WT_8822B << BIT_SHIFT_SYSON_SPS0WWV_WT_8822B)
#define BIT_CLEAR_SYSON_SPS0WWV_WT_8822B(x)                                    \
	((x) & (~BITS_SYSON_SPS0WWV_WT_8822B))
#define BIT_GET_SYSON_SPS0WWV_WT_8822B(x)                                      \
	(((x) >> BIT_SHIFT_SYSON_SPS0WWV_WT_8822B) &                           \
	 BIT_MASK_SYSON_SPS0WWV_WT_8822B)
#define BIT_SET_SYSON_SPS0WWV_WT_8822B(x, v)                                   \
	(BIT_CLEAR_SYSON_SPS0WWV_WT_8822B(x) | BIT_SYSON_SPS0WWV_WT_8822B(v))

#define BIT_SHIFT_SYSON_SPS0LDO_WT_8822B 2
#define BIT_MASK_SYSON_SPS0LDO_WT_8822B 0x3
#define BIT_SYSON_SPS0LDO_WT_8822B(x)                                          \
	(((x) & BIT_MASK_SYSON_SPS0LDO_WT_8822B)                               \
	 << BIT_SHIFT_SYSON_SPS0LDO_WT_8822B)
#define BITS_SYSON_SPS0LDO_WT_8822B                                            \
	(BIT_MASK_SYSON_SPS0LDO_WT_8822B << BIT_SHIFT_SYSON_SPS0LDO_WT_8822B)
#define BIT_CLEAR_SYSON_SPS0LDO_WT_8822B(x)                                    \
	((x) & (~BITS_SYSON_SPS0LDO_WT_8822B))
#define BIT_GET_SYSON_SPS0LDO_WT_8822B(x)                                      \
	(((x) >> BIT_SHIFT_SYSON_SPS0LDO_WT_8822B) &                           \
	 BIT_MASK_SYSON_SPS0LDO_WT_8822B)
#define BIT_SET_SYSON_SPS0LDO_WT_8822B(x, v)                                   \
	(BIT_CLEAR_SYSON_SPS0LDO_WT_8822B(x) | BIT_SYSON_SPS0LDO_WT_8822B(v))

#define BIT_SHIFT_SYSON_RCLK_SCALE_8822B 0
#define BIT_MASK_SYSON_RCLK_SCALE_8822B 0x3
#define BIT_SYSON_RCLK_SCALE_8822B(x)                                          \
	(((x) & BIT_MASK_SYSON_RCLK_SCALE_8822B)                               \
	 << BIT_SHIFT_SYSON_RCLK_SCALE_8822B)
#define BITS_SYSON_RCLK_SCALE_8822B                                            \
	(BIT_MASK_SYSON_RCLK_SCALE_8822B << BIT_SHIFT_SYSON_RCLK_SCALE_8822B)
#define BIT_CLEAR_SYSON_RCLK_SCALE_8822B(x)                                    \
	((x) & (~BITS_SYSON_RCLK_SCALE_8822B))
#define BIT_GET_SYSON_RCLK_SCALE_8822B(x)                                      \
	(((x) >> BIT_SHIFT_SYSON_RCLK_SCALE_8822B) &                           \
	 BIT_MASK_SYSON_RCLK_SCALE_8822B)
#define BIT_SET_SYSON_RCLK_SCALE_8822B(x, v)                                   \
	(BIT_CLEAR_SYSON_RCLK_SCALE_8822B(x) | BIT_SYSON_RCLK_SCALE_8822B(v))

/* 2 REG_CAL_TIMER_8822B */

#define BIT_SHIFT_MATCH_CNT_8822B 8
#define BIT_MASK_MATCH_CNT_8822B 0xff
#define BIT_MATCH_CNT_8822B(x)                                                 \
	(((x) & BIT_MASK_MATCH_CNT_8822B) << BIT_SHIFT_MATCH_CNT_8822B)
#define BITS_MATCH_CNT_8822B                                                   \
	(BIT_MASK_MATCH_CNT_8822B << BIT_SHIFT_MATCH_CNT_8822B)
#define BIT_CLEAR_MATCH_CNT_8822B(x) ((x) & (~BITS_MATCH_CNT_8822B))
#define BIT_GET_MATCH_CNT_8822B(x)                                             \
	(((x) >> BIT_SHIFT_MATCH_CNT_8822B) & BIT_MASK_MATCH_CNT_8822B)
#define BIT_SET_MATCH_CNT_8822B(x, v)                                          \
	(BIT_CLEAR_MATCH_CNT_8822B(x) | BIT_MATCH_CNT_8822B(v))

#define BIT_SHIFT_CAL_SCAL_8822B 0
#define BIT_MASK_CAL_SCAL_8822B 0xff
#define BIT_CAL_SCAL_8822B(x)                                                  \
	(((x) & BIT_MASK_CAL_SCAL_8822B) << BIT_SHIFT_CAL_SCAL_8822B)
#define BITS_CAL_SCAL_8822B                                                    \
	(BIT_MASK_CAL_SCAL_8822B << BIT_SHIFT_CAL_SCAL_8822B)
#define BIT_CLEAR_CAL_SCAL_8822B(x) ((x) & (~BITS_CAL_SCAL_8822B))
#define BIT_GET_CAL_SCAL_8822B(x)                                              \
	(((x) >> BIT_SHIFT_CAL_SCAL_8822B) & BIT_MASK_CAL_SCAL_8822B)
#define BIT_SET_CAL_SCAL_8822B(x, v)                                           \
	(BIT_CLEAR_CAL_SCAL_8822B(x) | BIT_CAL_SCAL_8822B(v))

/* 2 REG_ACLK_MON_8822B */

#define BIT_SHIFT_RCLK_MON_8822B 5
#define BIT_MASK_RCLK_MON_8822B 0x7ff
#define BIT_RCLK_MON_8822B(x)                                                  \
	(((x) & BIT_MASK_RCLK_MON_8822B) << BIT_SHIFT_RCLK_MON_8822B)
#define BITS_RCLK_MON_8822B                                                    \
	(BIT_MASK_RCLK_MON_8822B << BIT_SHIFT_RCLK_MON_8822B)
#define BIT_CLEAR_RCLK_MON_8822B(x) ((x) & (~BITS_RCLK_MON_8822B))
#define BIT_GET_RCLK_MON_8822B(x)                                              \
	(((x) >> BIT_SHIFT_RCLK_MON_8822B) & BIT_MASK_RCLK_MON_8822B)
#define BIT_SET_RCLK_MON_8822B(x, v)                                           \
	(BIT_CLEAR_RCLK_MON_8822B(x) | BIT_RCLK_MON_8822B(v))

#define BIT_CAL_EN_8822B BIT(4)

#define BIT_SHIFT_DPSTU_8822B 2
#define BIT_MASK_DPSTU_8822B 0x3
#define BIT_DPSTU_8822B(x)                                                     \
	(((x) & BIT_MASK_DPSTU_8822B) << BIT_SHIFT_DPSTU_8822B)
#define BITS_DPSTU_8822B (BIT_MASK_DPSTU_8822B << BIT_SHIFT_DPSTU_8822B)
#define BIT_CLEAR_DPSTU_8822B(x) ((x) & (~BITS_DPSTU_8822B))
#define BIT_GET_DPSTU_8822B(x)                                                 \
	(((x) >> BIT_SHIFT_DPSTU_8822B) & BIT_MASK_DPSTU_8822B)
#define BIT_SET_DPSTU_8822B(x, v)                                              \
	(BIT_CLEAR_DPSTU_8822B(x) | BIT_DPSTU_8822B(v))

#define BIT_SUS_16X_8822B BIT(1)

/* 2 REG_GPIO_MUXCFG_8822B */
#define BIT_FSPI_EN_8822B BIT(19)
#define BIT_WL_RTS_EXT_32K_SEL_8822B BIT(18)
#define BIT_WLGP_SPI_EN_8822B BIT(16)
#define BIT_SIC_LBK_8822B BIT(15)
#define BIT_ENHTP_8822B BIT(14)
#define BIT_ENSIC_8822B BIT(12)
#define BIT_SIC_SWRST_8822B BIT(11)
#define BIT_PO_WIFI_PTA_PINS_8822B BIT(10)
#define BIT_PO_BT_PTA_PINS_8822B BIT(9)
#define BIT_ENUART_8822B BIT(8)

#define BIT_SHIFT_BTMODE_8822B 6
#define BIT_MASK_BTMODE_8822B 0x3
#define BIT_BTMODE_8822B(x)                                                    \
	(((x) & BIT_MASK_BTMODE_8822B) << BIT_SHIFT_BTMODE_8822B)
#define BITS_BTMODE_8822B (BIT_MASK_BTMODE_8822B << BIT_SHIFT_BTMODE_8822B)
#define BIT_CLEAR_BTMODE_8822B(x) ((x) & (~BITS_BTMODE_8822B))
#define BIT_GET_BTMODE_8822B(x)                                                \
	(((x) >> BIT_SHIFT_BTMODE_8822B) & BIT_MASK_BTMODE_8822B)
#define BIT_SET_BTMODE_8822B(x, v)                                             \
	(BIT_CLEAR_BTMODE_8822B(x) | BIT_BTMODE_8822B(v))

#define BIT_ENBT_8822B BIT(5)
#define BIT_EROM_EN_8822B BIT(4)
#define BIT_WLRFE_6_7_EN_8822B BIT(3)
#define BIT_WLRFE_4_5_EN_8822B BIT(2)

#define BIT_SHIFT_GPIOSEL_8822B 0
#define BIT_MASK_GPIOSEL_8822B 0x3
#define BIT_GPIOSEL_8822B(x)                                                   \
	(((x) & BIT_MASK_GPIOSEL_8822B) << BIT_SHIFT_GPIOSEL_8822B)
#define BITS_GPIOSEL_8822B (BIT_MASK_GPIOSEL_8822B << BIT_SHIFT_GPIOSEL_8822B)
#define BIT_CLEAR_GPIOSEL_8822B(x) ((x) & (~BITS_GPIOSEL_8822B))
#define BIT_GET_GPIOSEL_8822B(x)                                               \
	(((x) >> BIT_SHIFT_GPIOSEL_8822B) & BIT_MASK_GPIOSEL_8822B)
#define BIT_SET_GPIOSEL_8822B(x, v)                                            \
	(BIT_CLEAR_GPIOSEL_8822B(x) | BIT_GPIOSEL_8822B(v))

/* 2 REG_GPIO_PIN_CTRL_8822B */

#define BIT_SHIFT_GPIO_MOD_7_TO_0_8822B 24
#define BIT_MASK_GPIO_MOD_7_TO_0_8822B 0xff
#define BIT_GPIO_MOD_7_TO_0_8822B(x)                                           \
	(((x) & BIT_MASK_GPIO_MOD_7_TO_0_8822B)                                \
	 << BIT_SHIFT_GPIO_MOD_7_TO_0_8822B)
#define BITS_GPIO_MOD_7_TO_0_8822B                                             \
	(BIT_MASK_GPIO_MOD_7_TO_0_8822B << BIT_SHIFT_GPIO_MOD_7_TO_0_8822B)
#define BIT_CLEAR_GPIO_MOD_7_TO_0_8822B(x) ((x) & (~BITS_GPIO_MOD_7_TO_0_8822B))
#define BIT_GET_GPIO_MOD_7_TO_0_8822B(x)                                       \
	(((x) >> BIT_SHIFT_GPIO_MOD_7_TO_0_8822B) &                            \
	 BIT_MASK_GPIO_MOD_7_TO_0_8822B)
#define BIT_SET_GPIO_MOD_7_TO_0_8822B(x, v)                                    \
	(BIT_CLEAR_GPIO_MOD_7_TO_0_8822B(x) | BIT_GPIO_MOD_7_TO_0_8822B(v))

#define BIT_SHIFT_GPIO_IO_SEL_7_TO_0_8822B 16
#define BIT_MASK_GPIO_IO_SEL_7_TO_0_8822B 0xff
#define BIT_GPIO_IO_SEL_7_TO_0_8822B(x)                                        \
	(((x) & BIT_MASK_GPIO_IO_SEL_7_TO_0_8822B)                             \
	 << BIT_SHIFT_GPIO_IO_SEL_7_TO_0_8822B)
#define BITS_GPIO_IO_SEL_7_TO_0_8822B                                          \
	(BIT_MASK_GPIO_IO_SEL_7_TO_0_8822B                                     \
	 << BIT_SHIFT_GPIO_IO_SEL_7_TO_0_8822B)
#define BIT_CLEAR_GPIO_IO_SEL_7_TO_0_8822B(x)                                  \
	((x) & (~BITS_GPIO_IO_SEL_7_TO_0_8822B))
#define BIT_GET_GPIO_IO_SEL_7_TO_0_8822B(x)                                    \
	(((x) >> BIT_SHIFT_GPIO_IO_SEL_7_TO_0_8822B) &                         \
	 BIT_MASK_GPIO_IO_SEL_7_TO_0_8822B)
#define BIT_SET_GPIO_IO_SEL_7_TO_0_8822B(x, v)                                 \
	(BIT_CLEAR_GPIO_IO_SEL_7_TO_0_8822B(x) |                               \
	 BIT_GPIO_IO_SEL_7_TO_0_8822B(v))

#define BIT_SHIFT_GPIO_OUT_7_TO_0_8822B 8
#define BIT_MASK_GPIO_OUT_7_TO_0_8822B 0xff
#define BIT_GPIO_OUT_7_TO_0_8822B(x)                                           \
	(((x) & BIT_MASK_GPIO_OUT_7_TO_0_8822B)                                \
	 << BIT_SHIFT_GPIO_OUT_7_TO_0_8822B)
#define BITS_GPIO_OUT_7_TO_0_8822B                                             \
	(BIT_MASK_GPIO_OUT_7_TO_0_8822B << BIT_SHIFT_GPIO_OUT_7_TO_0_8822B)
#define BIT_CLEAR_GPIO_OUT_7_TO_0_8822B(x) ((x) & (~BITS_GPIO_OUT_7_TO_0_8822B))
#define BIT_GET_GPIO_OUT_7_TO_0_8822B(x)                                       \
	(((x) >> BIT_SHIFT_GPIO_OUT_7_TO_0_8822B) &                            \
	 BIT_MASK_GPIO_OUT_7_TO_0_8822B)
#define BIT_SET_GPIO_OUT_7_TO_0_8822B(x, v)                                    \
	(BIT_CLEAR_GPIO_OUT_7_TO_0_8822B(x) | BIT_GPIO_OUT_7_TO_0_8822B(v))

#define BIT_SHIFT_GPIO_IN_7_TO_0_8822B 0
#define BIT_MASK_GPIO_IN_7_TO_0_8822B 0xff
#define BIT_GPIO_IN_7_TO_0_8822B(x)                                            \
	(((x) & BIT_MASK_GPIO_IN_7_TO_0_8822B)                                 \
	 << BIT_SHIFT_GPIO_IN_7_TO_0_8822B)
#define BITS_GPIO_IN_7_TO_0_8822B                                              \
	(BIT_MASK_GPIO_IN_7_TO_0_8822B << BIT_SHIFT_GPIO_IN_7_TO_0_8822B)
#define BIT_CLEAR_GPIO_IN_7_TO_0_8822B(x) ((x) & (~BITS_GPIO_IN_7_TO_0_8822B))
#define BIT_GET_GPIO_IN_7_TO_0_8822B(x)                                        \
	(((x) >> BIT_SHIFT_GPIO_IN_7_TO_0_8822B) &                             \
	 BIT_MASK_GPIO_IN_7_TO_0_8822B)
#define BIT_SET_GPIO_IN_7_TO_0_8822B(x, v)                                     \
	(BIT_CLEAR_GPIO_IN_7_TO_0_8822B(x) | BIT_GPIO_IN_7_TO_0_8822B(v))

/* 2 REG_GPIO_INTM_8822B */

#define BIT_SHIFT_MUXDBG_SEL_8822B 30
#define BIT_MASK_MUXDBG_SEL_8822B 0x3
#define BIT_MUXDBG_SEL_8822B(x)                                                \
	(((x) & BIT_MASK_MUXDBG_SEL_8822B) << BIT_SHIFT_MUXDBG_SEL_8822B)
#define BITS_MUXDBG_SEL_8822B                                                  \
	(BIT_MASK_MUXDBG_SEL_8822B << BIT_SHIFT_MUXDBG_SEL_8822B)
#define BIT_CLEAR_MUXDBG_SEL_8822B(x) ((x) & (~BITS_MUXDBG_SEL_8822B))
#define BIT_GET_MUXDBG_SEL_8822B(x)                                            \
	(((x) >> BIT_SHIFT_MUXDBG_SEL_8822B) & BIT_MASK_MUXDBG_SEL_8822B)
#define BIT_SET_MUXDBG_SEL_8822B(x, v)                                         \
	(BIT_CLEAR_MUXDBG_SEL_8822B(x) | BIT_MUXDBG_SEL_8822B(v))

#define BIT_EXTWOL_SEL_8822B BIT(17)
#define BIT_EXTWOL_EN_8822B BIT(16)
#define BIT_GPIOF_INT_MD_8822B BIT(15)
#define BIT_GPIOE_INT_MD_8822B BIT(14)
#define BIT_GPIOD_INT_MD_8822B BIT(13)
#define BIT_GPIOF_INT_MD_8822B BIT(15)
#define BIT_GPIOE_INT_MD_8822B BIT(14)
#define BIT_GPIOD_INT_MD_8822B BIT(13)
#define BIT_GPIOC_INT_MD_8822B BIT(12)
#define BIT_GPIOB_INT_MD_8822B BIT(11)
#define BIT_GPIOA_INT_MD_8822B BIT(10)
#define BIT_GPIO9_INT_MD_8822B BIT(9)
#define BIT_GPIO8_INT_MD_8822B BIT(8)
#define BIT_GPIO7_INT_MD_8822B BIT(7)
#define BIT_GPIO6_INT_MD_8822B BIT(6)
#define BIT_GPIO5_INT_MD_8822B BIT(5)
#define BIT_GPIO4_INT_MD_8822B BIT(4)
#define BIT_GPIO3_INT_MD_8822B BIT(3)
#define BIT_GPIO2_INT_MD_8822B BIT(2)
#define BIT_GPIO1_INT_MD_8822B BIT(1)
#define BIT_GPIO0_INT_MD_8822B BIT(0)

/* 2 REG_LED_CFG_8822B */
#define BIT_GPIO3_WL_CTRL_EN_8822B BIT(27)
#define BIT_LNAON_SEL_EN_8822B BIT(26)
#define BIT_PAPE_SEL_EN_8822B BIT(25)
#define BIT_DPDT_WLBT_SEL_8822B BIT(24)
#define BIT_DPDT_SEL_EN_8822B BIT(23)
#define BIT_GPIO13_14_WL_CTRL_EN_8822B BIT(22)
#define BIT_GPIO13_14_WL_CTRL_EN_8822B BIT(22)
#define BIT_LED2DIS_8822B BIT(21)
#define BIT_LED2PL_8822B BIT(20)
#define BIT_LED2SV_8822B BIT(19)

#define BIT_SHIFT_LED2CM_8822B 16
#define BIT_MASK_LED2CM_8822B 0x7
#define BIT_LED2CM_8822B(x)                                                    \
	(((x) & BIT_MASK_LED2CM_8822B) << BIT_SHIFT_LED2CM_8822B)
#define BITS_LED2CM_8822B (BIT_MASK_LED2CM_8822B << BIT_SHIFT_LED2CM_8822B)
#define BIT_CLEAR_LED2CM_8822B(x) ((x) & (~BITS_LED2CM_8822B))
#define BIT_GET_LED2CM_8822B(x)                                                \
	(((x) >> BIT_SHIFT_LED2CM_8822B) & BIT_MASK_LED2CM_8822B)
#define BIT_SET_LED2CM_8822B(x, v)                                             \
	(BIT_CLEAR_LED2CM_8822B(x) | BIT_LED2CM_8822B(v))

#define BIT_LED1DIS_8822B BIT(15)
#define BIT_LED1PL_8822B BIT(12)
#define BIT_LED1SV_8822B BIT(11)

#define BIT_SHIFT_LED1CM_8822B 8
#define BIT_MASK_LED1CM_8822B 0x7
#define BIT_LED1CM_8822B(x)                                                    \
	(((x) & BIT_MASK_LED1CM_8822B) << BIT_SHIFT_LED1CM_8822B)
#define BITS_LED1CM_8822B (BIT_MASK_LED1CM_8822B << BIT_SHIFT_LED1CM_8822B)
#define BIT_CLEAR_LED1CM_8822B(x) ((x) & (~BITS_LED1CM_8822B))
#define BIT_GET_LED1CM_8822B(x)                                                \
	(((x) >> BIT_SHIFT_LED1CM_8822B) & BIT_MASK_LED1CM_8822B)
#define BIT_SET_LED1CM_8822B(x, v)                                             \
	(BIT_CLEAR_LED1CM_8822B(x) | BIT_LED1CM_8822B(v))

#define BIT_LED0DIS_8822B BIT(7)

#define BIT_SHIFT_AFE_LDO_SWR_CHECK_8822B 5
#define BIT_MASK_AFE_LDO_SWR_CHECK_8822B 0x3
#define BIT_AFE_LDO_SWR_CHECK_8822B(x)                                         \
	(((x) & BIT_MASK_AFE_LDO_SWR_CHECK_8822B)                              \
	 << BIT_SHIFT_AFE_LDO_SWR_CHECK_8822B)
#define BITS_AFE_LDO_SWR_CHECK_8822B                                           \
	(BIT_MASK_AFE_LDO_SWR_CHECK_8822B << BIT_SHIFT_AFE_LDO_SWR_CHECK_8822B)
#define BIT_CLEAR_AFE_LDO_SWR_CHECK_8822B(x)                                   \
	((x) & (~BITS_AFE_LDO_SWR_CHECK_8822B))
#define BIT_GET_AFE_LDO_SWR_CHECK_8822B(x)                                     \
	(((x) >> BIT_SHIFT_AFE_LDO_SWR_CHECK_8822B) &                          \
	 BIT_MASK_AFE_LDO_SWR_CHECK_8822B)
#define BIT_SET_AFE_LDO_SWR_CHECK_8822B(x, v)                                  \
	(BIT_CLEAR_AFE_LDO_SWR_CHECK_8822B(x) | BIT_AFE_LDO_SWR_CHECK_8822B(v))

#define BIT_LED0PL_8822B BIT(4)
#define BIT_LED0SV_8822B BIT(3)

#define BIT_SHIFT_LED0CM_8822B 0
#define BIT_MASK_LED0CM_8822B 0x7
#define BIT_LED0CM_8822B(x)                                                    \
	(((x) & BIT_MASK_LED0CM_8822B) << BIT_SHIFT_LED0CM_8822B)
#define BITS_LED0CM_8822B (BIT_MASK_LED0CM_8822B << BIT_SHIFT_LED0CM_8822B)
#define BIT_CLEAR_LED0CM_8822B(x) ((x) & (~BITS_LED0CM_8822B))
#define BIT_GET_LED0CM_8822B(x)                                                \
	(((x) >> BIT_SHIFT_LED0CM_8822B) & BIT_MASK_LED0CM_8822B)
#define BIT_SET_LED0CM_8822B(x, v)                                             \
	(BIT_CLEAR_LED0CM_8822B(x) | BIT_LED0CM_8822B(v))

/* 2 REG_FSIMR_8822B */
#define BIT_FS_PDNINT_EN_8822B BIT(31)
#define BIT_NFC_INT_PAD_EN_8822B BIT(30)
#define BIT_FS_SPS_OCP_INT_EN_8822B BIT(29)
#define BIT_FS_PWMERR_INT_EN_8822B BIT(28)
#define BIT_FS_GPIOF_INT_EN_8822B BIT(27)
#define BIT_FS_GPIOE_INT_EN_8822B BIT(26)
#define BIT_FS_GPIOD_INT_EN_8822B BIT(25)
#define BIT_FS_GPIOC_INT_EN_8822B BIT(24)
#define BIT_FS_GPIOB_INT_EN_8822B BIT(23)
#define BIT_FS_GPIOA_INT_EN_8822B BIT(22)
#define BIT_FS_GPIO9_INT_EN_8822B BIT(21)
#define BIT_FS_GPIO8_INT_EN_8822B BIT(20)
#define BIT_FS_GPIO7_INT_EN_8822B BIT(19)
#define BIT_FS_GPIO6_INT_EN_8822B BIT(18)
#define BIT_FS_GPIO5_INT_EN_8822B BIT(17)
#define BIT_FS_GPIO4_INT_EN_8822B BIT(16)
#define BIT_FS_GPIO3_INT_EN_8822B BIT(15)
#define BIT_FS_GPIO2_INT_EN_8822B BIT(14)
#define BIT_FS_GPIO1_INT_EN_8822B BIT(13)
#define BIT_FS_GPIO0_INT_EN_8822B BIT(12)
#define BIT_FS_HCI_SUS_EN_8822B BIT(11)
#define BIT_FS_HCI_RES_EN_8822B BIT(10)
#define BIT_FS_HCI_RESET_EN_8822B BIT(9)
#define BIT_FS_BTON_STS_UPDATE_MSK_EN_8822B BIT(7)
#define BIT_ACT2RECOVERY_INT_EN_V1_8822B BIT(6)
#define BIT_GEN1GEN2_SWITCH_8822B BIT(5)
#define BIT_HCI_TXDMA_REQ_HIMR_8822B BIT(4)
#define BIT_FS_32K_LEAVE_SETTING_MAK_8822B BIT(3)
#define BIT_FS_32K_ENTER_SETTING_MAK_8822B BIT(2)
#define BIT_FS_USB_LPMRSM_MSK_8822B BIT(1)
#define BIT_FS_USB_LPMINT_MSK_8822B BIT(0)

/* 2 REG_FSISR_8822B */
#define BIT_FS_PDNINT_8822B BIT(31)
#define BIT_FS_SPS_OCP_INT_8822B BIT(29)
#define BIT_FS_PWMERR_INT_8822B BIT(28)
#define BIT_FS_GPIOF_INT_8822B BIT(27)
#define BIT_FS_GPIOE_INT_8822B BIT(26)
#define BIT_FS_GPIOD_INT_8822B BIT(25)
#define BIT_FS_GPIOC_INT_8822B BIT(24)
#define BIT_FS_GPIOB_INT_8822B BIT(23)
#define BIT_FS_GPIOA_INT_8822B BIT(22)
#define BIT_FS_GPIO9_INT_8822B BIT(21)
#define BIT_FS_GPIO8_INT_8822B BIT(20)
#define BIT_FS_GPIO7_INT_8822B BIT(19)
#define BIT_FS_GPIO6_INT_8822B BIT(18)
#define BIT_FS_GPIO5_INT_8822B BIT(17)
#define BIT_FS_GPIO4_INT_8822B BIT(16)
#define BIT_FS_GPIO3_INT_8822B BIT(15)
#define BIT_FS_GPIO2_INT_8822B BIT(14)
#define BIT_FS_GPIO1_INT_8822B BIT(13)
#define BIT_FS_GPIO0_INT_8822B BIT(12)
#define BIT_FS_HCI_SUS_INT_8822B BIT(11)
#define BIT_FS_HCI_RES_INT_8822B BIT(10)
#define BIT_FS_HCI_RESET_INT_8822B BIT(9)
#define BIT_ACT2RECOVERY_8822B BIT(6)
#define BIT_GEN1GEN2_SWITCH_8822B BIT(5)
#define BIT_HCI_TXDMA_REQ_HISR_8822B BIT(4)
#define BIT_FS_32K_LEAVE_SETTING_INT_8822B BIT(3)
#define BIT_FS_32K_ENTER_SETTING_INT_8822B BIT(2)
#define BIT_FS_USB_LPMRSM_INT_8822B BIT(1)
#define BIT_FS_USB_LPMINT_INT_8822B BIT(0)

/* 2 REG_HSIMR_8822B */
#define BIT_GPIOF_INT_EN_8822B BIT(31)
#define BIT_GPIOE_INT_EN_8822B BIT(30)
#define BIT_GPIOD_INT_EN_8822B BIT(29)
#define BIT_GPIOC_INT_EN_8822B BIT(28)
#define BIT_GPIOB_INT_EN_8822B BIT(27)
#define BIT_GPIOA_INT_EN_8822B BIT(26)
#define BIT_GPIO9_INT_EN_8822B BIT(25)
#define BIT_GPIO8_INT_EN_8822B BIT(24)
#define BIT_GPIO7_INT_EN_8822B BIT(23)
#define BIT_GPIO6_INT_EN_8822B BIT(22)
#define BIT_GPIO5_INT_EN_8822B BIT(21)
#define BIT_GPIO4_INT_EN_8822B BIT(20)
#define BIT_GPIO3_INT_EN_8822B BIT(19)
#define BIT_GPIO2_INT_EN_V1_8822B BIT(18)
#define BIT_GPIO1_INT_EN_8822B BIT(17)
#define BIT_GPIO0_INT_EN_8822B BIT(16)
#define BIT_PDNINT_EN_8822B BIT(7)
#define BIT_RON_INT_EN_8822B BIT(6)
#define BIT_SPS_OCP_INT_EN_8822B BIT(5)
#define BIT_GPIO15_0_INT_EN_8822B BIT(0)

/* 2 REG_HSISR_8822B */
#define BIT_GPIOF_INT_8822B BIT(31)
#define BIT_GPIOE_INT_8822B BIT(30)
#define BIT_GPIOD_INT_8822B BIT(29)
#define BIT_GPIOC_INT_8822B BIT(28)
#define BIT_GPIOB_INT_8822B BIT(27)
#define BIT_GPIOA_INT_8822B BIT(26)
#define BIT_GPIO9_INT_8822B BIT(25)
#define BIT_GPIO8_INT_8822B BIT(24)
#define BIT_GPIO7_INT_8822B BIT(23)
#define BIT_GPIO6_INT_8822B BIT(22)
#define BIT_GPIO5_INT_8822B BIT(21)
#define BIT_GPIO4_INT_8822B BIT(20)
#define BIT_GPIO3_INT_8822B BIT(19)
#define BIT_GPIO2_INT_V1_8822B BIT(18)
#define BIT_GPIO1_INT_8822B BIT(17)
#define BIT_GPIO0_INT_8822B BIT(16)
#define BIT_PDNINT_8822B BIT(7)
#define BIT_RON_INT_8822B BIT(6)
#define BIT_SPS_OCP_INT_8822B BIT(5)
#define BIT_GPIO15_0_INT_8822B BIT(0)

/* 2 REG_GPIO_EXT_CTRL_8822B */

#define BIT_SHIFT_GPIO_MOD_15_TO_8_8822B 24
#define BIT_MASK_GPIO_MOD_15_TO_8_8822B 0xff
#define BIT_GPIO_MOD_15_TO_8_8822B(x)                                          \
	(((x) & BIT_MASK_GPIO_MOD_15_TO_8_8822B)                               \
	 << BIT_SHIFT_GPIO_MOD_15_TO_8_8822B)
#define BITS_GPIO_MOD_15_TO_8_8822B                                            \
	(BIT_MASK_GPIO_MOD_15_TO_8_8822B << BIT_SHIFT_GPIO_MOD_15_TO_8_8822B)
#define BIT_CLEAR_GPIO_MOD_15_TO_8_8822B(x)                                    \
	((x) & (~BITS_GPIO_MOD_15_TO_8_8822B))
#define BIT_GET_GPIO_MOD_15_TO_8_8822B(x)                                      \
	(((x) >> BIT_SHIFT_GPIO_MOD_15_TO_8_8822B) &                           \
	 BIT_MASK_GPIO_MOD_15_TO_8_8822B)
#define BIT_SET_GPIO_MOD_15_TO_8_8822B(x, v)                                   \
	(BIT_CLEAR_GPIO_MOD_15_TO_8_8822B(x) | BIT_GPIO_MOD_15_TO_8_8822B(v))

#define BIT_SHIFT_GPIO_IO_SEL_15_TO_8_8822B 16
#define BIT_MASK_GPIO_IO_SEL_15_TO_8_8822B 0xff
#define BIT_GPIO_IO_SEL_15_TO_8_8822B(x)                                       \
	(((x) & BIT_MASK_GPIO_IO_SEL_15_TO_8_8822B)                            \
	 << BIT_SHIFT_GPIO_IO_SEL_15_TO_8_8822B)
#define BITS_GPIO_IO_SEL_15_TO_8_8822B                                         \
	(BIT_MASK_GPIO_IO_SEL_15_TO_8_8822B                                    \
	 << BIT_SHIFT_GPIO_IO_SEL_15_TO_8_8822B)
#define BIT_CLEAR_GPIO_IO_SEL_15_TO_8_8822B(x)                                 \
	((x) & (~BITS_GPIO_IO_SEL_15_TO_8_8822B))
#define BIT_GET_GPIO_IO_SEL_15_TO_8_8822B(x)                                   \
	(((x) >> BIT_SHIFT_GPIO_IO_SEL_15_TO_8_8822B) &                        \
	 BIT_MASK_GPIO_IO_SEL_15_TO_8_8822B)
#define BIT_SET_GPIO_IO_SEL_15_TO_8_8822B(x, v)                                \
	(BIT_CLEAR_GPIO_IO_SEL_15_TO_8_8822B(x) |                              \
	 BIT_GPIO_IO_SEL_15_TO_8_8822B(v))

#define BIT_SHIFT_GPIO_OUT_15_TO_8_8822B 8
#define BIT_MASK_GPIO_OUT_15_TO_8_8822B 0xff
#define BIT_GPIO_OUT_15_TO_8_8822B(x)                                          \
	(((x) & BIT_MASK_GPIO_OUT_15_TO_8_8822B)                               \
	 << BIT_SHIFT_GPIO_OUT_15_TO_8_8822B)
#define BITS_GPIO_OUT_15_TO_8_8822B                                            \
	(BIT_MASK_GPIO_OUT_15_TO_8_8822B << BIT_SHIFT_GPIO_OUT_15_TO_8_8822B)
#define BIT_CLEAR_GPIO_OUT_15_TO_8_8822B(x)                                    \
	((x) & (~BITS_GPIO_OUT_15_TO_8_8822B))
#define BIT_GET_GPIO_OUT_15_TO_8_8822B(x)                                      \
	(((x) >> BIT_SHIFT_GPIO_OUT_15_TO_8_8822B) &                           \
	 BIT_MASK_GPIO_OUT_15_TO_8_8822B)
#define BIT_SET_GPIO_OUT_15_TO_8_8822B(x, v)                                   \
	(BIT_CLEAR_GPIO_OUT_15_TO_8_8822B(x) | BIT_GPIO_OUT_15_TO_8_8822B(v))

#define BIT_SHIFT_GPIO_IN_15_TO_8_8822B 0
#define BIT_MASK_GPIO_IN_15_TO_8_8822B 0xff
#define BIT_GPIO_IN_15_TO_8_8822B(x)                                           \
	(((x) & BIT_MASK_GPIO_IN_15_TO_8_8822B)                                \
	 << BIT_SHIFT_GPIO_IN_15_TO_8_8822B)
#define BITS_GPIO_IN_15_TO_8_8822B                                             \
	(BIT_MASK_GPIO_IN_15_TO_8_8822B << BIT_SHIFT_GPIO_IN_15_TO_8_8822B)
#define BIT_CLEAR_GPIO_IN_15_TO_8_8822B(x) ((x) & (~BITS_GPIO_IN_15_TO_8_8822B))
#define BIT_GET_GPIO_IN_15_TO_8_8822B(x)                                       \
	(((x) >> BIT_SHIFT_GPIO_IN_15_TO_8_8822B) &                            \
	 BIT_MASK_GPIO_IN_15_TO_8_8822B)
#define BIT_SET_GPIO_IN_15_TO_8_8822B(x, v)                                    \
	(BIT_CLEAR_GPIO_IN_15_TO_8_8822B(x) | BIT_GPIO_IN_15_TO_8_8822B(v))

/* 2 REG_PAD_CTRL1_8822B */
#define BIT_PAPE_WLBT_SEL_8822B BIT(29)
#define BIT_LNAON_WLBT_SEL_8822B BIT(28)
#define BIT_BTGP_GPG3_FEN_8822B BIT(26)
#define BIT_BTGP_GPG2_FEN_8822B BIT(25)
#define BIT_BTGP_JTAG_EN_8822B BIT(24)
#define BIT_XTAL_CLK_EXTARNAL_EN_8822B BIT(23)
#define BIT_BTGP_UART0_EN_8822B BIT(22)
#define BIT_BTGP_UART1_EN_8822B BIT(21)
#define BIT_BTGP_SPI_EN_8822B BIT(20)
#define BIT_BTGP_GPIO_E2_8822B BIT(19)
#define BIT_BTGP_GPIO_EN_8822B BIT(18)

#define BIT_SHIFT_BTGP_GPIO_SL_8822B 16
#define BIT_MASK_BTGP_GPIO_SL_8822B 0x3
#define BIT_BTGP_GPIO_SL_8822B(x)                                              \
	(((x) & BIT_MASK_BTGP_GPIO_SL_8822B) << BIT_SHIFT_BTGP_GPIO_SL_8822B)
#define BITS_BTGP_GPIO_SL_8822B                                                \
	(BIT_MASK_BTGP_GPIO_SL_8822B << BIT_SHIFT_BTGP_GPIO_SL_8822B)
#define BIT_CLEAR_BTGP_GPIO_SL_8822B(x) ((x) & (~BITS_BTGP_GPIO_SL_8822B))
#define BIT_GET_BTGP_GPIO_SL_8822B(x)                                          \
	(((x) >> BIT_SHIFT_BTGP_GPIO_SL_8822B) & BIT_MASK_BTGP_GPIO_SL_8822B)
#define BIT_SET_BTGP_GPIO_SL_8822B(x, v)                                       \
	(BIT_CLEAR_BTGP_GPIO_SL_8822B(x) | BIT_BTGP_GPIO_SL_8822B(v))

#define BIT_PAD_SDIO_SR_8822B BIT(14)
#define BIT_GPIO14_OUTPUT_PL_8822B BIT(13)
#define BIT_HOST_WAKE_PAD_PULL_EN_8822B BIT(12)
#define BIT_HOST_WAKE_PAD_SL_8822B BIT(11)
#define BIT_PAD_LNAON_SR_8822B BIT(10)
#define BIT_PAD_LNAON_E2_8822B BIT(9)
#define BIT_SW_LNAON_G_SEL_DATA_8822B BIT(8)
#define BIT_SW_LNAON_A_SEL_DATA_8822B BIT(7)
#define BIT_PAD_PAPE_SR_8822B BIT(6)
#define BIT_PAD_PAPE_E2_8822B BIT(5)
#define BIT_SW_PAPE_G_SEL_DATA_8822B BIT(4)
#define BIT_SW_PAPE_A_SEL_DATA_8822B BIT(3)
#define BIT_PAD_DPDT_SR_8822B BIT(2)
#define BIT_PAD_DPDT_PAD_E2_8822B BIT(1)
#define BIT_SW_DPDT_SEL_DATA_8822B BIT(0)

/* 2 REG_WL_BT_PWR_CTRL_8822B */
#define BIT_ISO_BD2PP_8822B BIT(31)
#define BIT_LDOV12B_EN_8822B BIT(30)
#define BIT_CKEN_BTGPS_8822B BIT(29)
#define BIT_FEN_BTGPS_8822B BIT(28)
#define BIT_BTCPU_BOOTSEL_8822B BIT(27)
#define BIT_SPI_SPEEDUP_8822B BIT(26)
#define BIT_DEVWAKE_PAD_TYPE_SEL_8822B BIT(24)
#define BIT_CLKREQ_PAD_TYPE_SEL_8822B BIT(23)
#define BIT_ISO_BTPON2PP_8822B BIT(22)
#define BIT_BT_HWROF_EN_8822B BIT(19)
#define BIT_BT_FUNC_EN_8822B BIT(18)
#define BIT_BT_HWPDN_SL_8822B BIT(17)
#define BIT_BT_DISN_EN_8822B BIT(16)
#define BIT_BT_PDN_PULL_EN_8822B BIT(15)
#define BIT_WL_PDN_PULL_EN_8822B BIT(14)
#define BIT_EXTERNAL_REQUEST_PL_8822B BIT(13)
#define BIT_GPIO0_2_3_PULL_LOW_EN_8822B BIT(12)
#define BIT_ISO_BA2PP_8822B BIT(11)
#define BIT_BT_AFE_LDO_EN_8822B BIT(10)
#define BIT_BT_AFE_PLL_EN_8822B BIT(9)
#define BIT_BT_DIG_CLK_EN_8822B BIT(8)
#define BIT_WL_DRV_EXIST_IDX_8822B BIT(5)
#define BIT_DOP_EHPAD_8822B BIT(4)
#define BIT_WL_HWROF_EN_8822B BIT(3)
#define BIT_WL_FUNC_EN_8822B BIT(2)
#define BIT_WL_HWPDN_SL_8822B BIT(1)
#define BIT_WL_HWPDN_EN_8822B BIT(0)

/* 2 REG_SDM_DEBUG_8822B */

#define BIT_SHIFT_WLCLK_PHASE_8822B 0
#define BIT_MASK_WLCLK_PHASE_8822B 0x1f
#define BIT_WLCLK_PHASE_8822B(x)                                               \
	(((x) & BIT_MASK_WLCLK_PHASE_8822B) << BIT_SHIFT_WLCLK_PHASE_8822B)
#define BITS_WLCLK_PHASE_8822B                                                 \
	(BIT_MASK_WLCLK_PHASE_8822B << BIT_SHIFT_WLCLK_PHASE_8822B)
#define BIT_CLEAR_WLCLK_PHASE_8822B(x) ((x) & (~BITS_WLCLK_PHASE_8822B))
#define BIT_GET_WLCLK_PHASE_8822B(x)                                           \
	(((x) >> BIT_SHIFT_WLCLK_PHASE_8822B) & BIT_MASK_WLCLK_PHASE_8822B)
#define BIT_SET_WLCLK_PHASE_8822B(x, v)                                        \
	(BIT_CLEAR_WLCLK_PHASE_8822B(x) | BIT_WLCLK_PHASE_8822B(v))

/* 2 REG_SYS_SDIO_CTRL_8822B */
#define BIT_DBG_GNT_WL_BT_8822B BIT(27)
#define BIT_LTE_MUX_CTRL_PATH_8822B BIT(26)
#define BIT_LTE_COEX_UART_8822B BIT(25)
#define BIT_3W_LTE_WL_GPIO_8822B BIT(24)
#define BIT_SDIO_INT_POLARITY_8822B BIT(19)
#define BIT_SDIO_INT_8822B BIT(18)
#define BIT_SDIO_OFF_EN_8822B BIT(17)
#define BIT_SDIO_ON_EN_8822B BIT(16)
#define BIT_PCIE_WAIT_TIMEOUT_EVENT_8822B BIT(10)
#define BIT_PCIE_WAIT_TIME_8822B BIT(9)
#define BIT_MPCIE_REFCLK_XTAL_SEL_8822B BIT(8)

#define BIT_SHIFT_SI_AUTHORIZATION_8822B 0
#define BIT_MASK_SI_AUTHORIZATION_8822B 0xff
#define BIT_SI_AUTHORIZATION_8822B(x)                                          \
	(((x) & BIT_MASK_SI_AUTHORIZATION_8822B)                               \
	 << BIT_SHIFT_SI_AUTHORIZATION_8822B)
#define BITS_SI_AUTHORIZATION_8822B                                            \
	(BIT_MASK_SI_AUTHORIZATION_8822B << BIT_SHIFT_SI_AUTHORIZATION_8822B)
#define BIT_CLEAR_SI_AUTHORIZATION_8822B(x)                                    \
	((x) & (~BITS_SI_AUTHORIZATION_8822B))
#define BIT_GET_SI_AUTHORIZATION_8822B(x)                                      \
	(((x) >> BIT_SHIFT_SI_AUTHORIZATION_8822B) &                           \
	 BIT_MASK_SI_AUTHORIZATION_8822B)
#define BIT_SET_SI_AUTHORIZATION_8822B(x, v)                                   \
	(BIT_CLEAR_SI_AUTHORIZATION_8822B(x) | BIT_SI_AUTHORIZATION_8822B(v))

/* 2 REG_HCI_OPT_CTRL_8822B */

#define BIT_SHIFT_TSFT_SEL_8822B 29
#define BIT_MASK_TSFT_SEL_8822B 0x7
#define BIT_TSFT_SEL_8822B(x)                                                  \
	(((x) & BIT_MASK_TSFT_SEL_8822B) << BIT_SHIFT_TSFT_SEL_8822B)
#define BITS_TSFT_SEL_8822B                                                    \
	(BIT_MASK_TSFT_SEL_8822B << BIT_SHIFT_TSFT_SEL_8822B)
#define BIT_CLEAR_TSFT_SEL_8822B(x) ((x) & (~BITS_TSFT_SEL_8822B))
#define BIT_GET_TSFT_SEL_8822B(x)                                              \
	(((x) >> BIT_SHIFT_TSFT_SEL_8822B) & BIT_MASK_TSFT_SEL_8822B)
#define BIT_SET_TSFT_SEL_8822B(x, v)                                           \
	(BIT_CLEAR_TSFT_SEL_8822B(x) | BIT_TSFT_SEL_8822B(v))

#define BIT_USB_HOST_PWR_OFF_EN_8822B BIT(12)
#define BIT_SYM_LPS_BLOCK_EN_8822B BIT(11)
#define BIT_USB_LPM_ACT_EN_8822B BIT(10)
#define BIT_USB_LPM_NY_8822B BIT(9)
#define BIT_USB_SUS_DIS_8822B BIT(8)

#define BIT_SHIFT_SDIO_PAD_E_8822B 5
#define BIT_MASK_SDIO_PAD_E_8822B 0x7
#define BIT_SDIO_PAD_E_8822B(x)                                                \
	(((x) & BIT_MASK_SDIO_PAD_E_8822B) << BIT_SHIFT_SDIO_PAD_E_8822B)
#define BITS_SDIO_PAD_E_8822B                                                  \
	(BIT_MASK_SDIO_PAD_E_8822B << BIT_SHIFT_SDIO_PAD_E_8822B)
#define BIT_CLEAR_SDIO_PAD_E_8822B(x) ((x) & (~BITS_SDIO_PAD_E_8822B))
#define BIT_GET_SDIO_PAD_E_8822B(x)                                            \
	(((x) >> BIT_SHIFT_SDIO_PAD_E_8822B) & BIT_MASK_SDIO_PAD_E_8822B)
#define BIT_SET_SDIO_PAD_E_8822B(x, v)                                         \
	(BIT_CLEAR_SDIO_PAD_E_8822B(x) | BIT_SDIO_PAD_E_8822B(v))

#define BIT_USB_LPPLL_EN_8822B BIT(4)
#define BIT_ROP_SW15_8822B BIT(2)
#define BIT_PCI_CKRDY_OPT_8822B BIT(1)
#define BIT_PCI_VAUX_EN_8822B BIT(0)

/* 2 REG_AFE_CTRL4_8822B */

/* 2 REG_LDO_SWR_CTRL_8822B */
#define BIT_ZCD_HW_AUTO_EN_8822B BIT(27)
#define BIT_ZCD_REGSEL_8822B BIT(26)

#define BIT_SHIFT_AUTO_ZCD_IN_CODE_8822B 21
#define BIT_MASK_AUTO_ZCD_IN_CODE_8822B 0x1f
#define BIT_AUTO_ZCD_IN_CODE_8822B(x)                                          \
	(((x) & BIT_MASK_AUTO_ZCD_IN_CODE_8822B)                               \
	 << BIT_SHIFT_AUTO_ZCD_IN_CODE_8822B)
#define BITS_AUTO_ZCD_IN_CODE_8822B                                            \
	(BIT_MASK_AUTO_ZCD_IN_CODE_8822B << BIT_SHIFT_AUTO_ZCD_IN_CODE_8822B)
#define BIT_CLEAR_AUTO_ZCD_IN_CODE_8822B(x)                                    \
	((x) & (~BITS_AUTO_ZCD_IN_CODE_8822B))
#define BIT_GET_AUTO_ZCD_IN_CODE_8822B(x)                                      \
	(((x) >> BIT_SHIFT_AUTO_ZCD_IN_CODE_8822B) &                           \
	 BIT_MASK_AUTO_ZCD_IN_CODE_8822B)
#define BIT_SET_AUTO_ZCD_IN_CODE_8822B(x, v)                                   \
	(BIT_CLEAR_AUTO_ZCD_IN_CODE_8822B(x) | BIT_AUTO_ZCD_IN_CODE_8822B(v))

#define BIT_SHIFT_ZCD_CODE_IN_L_8822B 16
#define BIT_MASK_ZCD_CODE_IN_L_8822B 0x1f
#define BIT_ZCD_CODE_IN_L_8822B(x)                                             \
	(((x) & BIT_MASK_ZCD_CODE_IN_L_8822B) << BIT_SHIFT_ZCD_CODE_IN_L_8822B)
#define BITS_ZCD_CODE_IN_L_8822B                                               \
	(BIT_MASK_ZCD_CODE_IN_L_8822B << BIT_SHIFT_ZCD_CODE_IN_L_8822B)
#define BIT_CLEAR_ZCD_CODE_IN_L_8822B(x) ((x) & (~BITS_ZCD_CODE_IN_L_8822B))
#define BIT_GET_ZCD_CODE_IN_L_8822B(x)                                         \
	(((x) >> BIT_SHIFT_ZCD_CODE_IN_L_8822B) & BIT_MASK_ZCD_CODE_IN_L_8822B)
#define BIT_SET_ZCD_CODE_IN_L_8822B(x, v)                                      \
	(BIT_CLEAR_ZCD_CODE_IN_L_8822B(x) | BIT_ZCD_CODE_IN_L_8822B(v))

#define BIT_SHIFT_LDO_HV5_DUMMY_8822B 14
#define BIT_MASK_LDO_HV5_DUMMY_8822B 0x3
#define BIT_LDO_HV5_DUMMY_8822B(x)                                             \
	(((x) & BIT_MASK_LDO_HV5_DUMMY_8822B) << BIT_SHIFT_LDO_HV5_DUMMY_8822B)
#define BITS_LDO_HV5_DUMMY_8822B                                               \
	(BIT_MASK_LDO_HV5_DUMMY_8822B << BIT_SHIFT_LDO_HV5_DUMMY_8822B)
#define BIT_CLEAR_LDO_HV5_DUMMY_8822B(x) ((x) & (~BITS_LDO_HV5_DUMMY_8822B))
#define BIT_GET_LDO_HV5_DUMMY_8822B(x)                                         \
	(((x) >> BIT_SHIFT_LDO_HV5_DUMMY_8822B) & BIT_MASK_LDO_HV5_DUMMY_8822B)
#define BIT_SET_LDO_HV5_DUMMY_8822B(x, v)                                      \
	(BIT_CLEAR_LDO_HV5_DUMMY_8822B(x) | BIT_LDO_HV5_DUMMY_8822B(v))

#define BIT_SHIFT_REG_VTUNE33_BIT0_TO_BIT1_8822B 12
#define BIT_MASK_REG_VTUNE33_BIT0_TO_BIT1_8822B 0x3
#define BIT_REG_VTUNE33_BIT0_TO_BIT1_8822B(x)                                  \
	(((x) & BIT_MASK_REG_VTUNE33_BIT0_TO_BIT1_8822B)                       \
	 << BIT_SHIFT_REG_VTUNE33_BIT0_TO_BIT1_8822B)
#define BITS_REG_VTUNE33_BIT0_TO_BIT1_8822B                                    \
	(BIT_MASK_REG_VTUNE33_BIT0_TO_BIT1_8822B                               \
	 << BIT_SHIFT_REG_VTUNE33_BIT0_TO_BIT1_8822B)
#define BIT_CLEAR_REG_VTUNE33_BIT0_TO_BIT1_8822B(x)                            \
	((x) & (~BITS_REG_VTUNE33_BIT0_TO_BIT1_8822B))
#define BIT_GET_REG_VTUNE33_BIT0_TO_BIT1_8822B(x)                              \
	(((x) >> BIT_SHIFT_REG_VTUNE33_BIT0_TO_BIT1_8822B) &                   \
	 BIT_MASK_REG_VTUNE33_BIT0_TO_BIT1_8822B)
#define BIT_SET_REG_VTUNE33_BIT0_TO_BIT1_8822B(x, v)                           \
	(BIT_CLEAR_REG_VTUNE33_BIT0_TO_BIT1_8822B(x) |                         \
	 BIT_REG_VTUNE33_BIT0_TO_BIT1_8822B(v))

#define BIT_SHIFT_REG_STANDBY33_BIT0_TO_BIT1_8822B 10
#define BIT_MASK_REG_STANDBY33_BIT0_TO_BIT1_8822B 0x3
#define BIT_REG_STANDBY33_BIT0_TO_BIT1_8822B(x)                                \
	(((x) & BIT_MASK_REG_STANDBY33_BIT0_TO_BIT1_8822B)                     \
	 << BIT_SHIFT_REG_STANDBY33_BIT0_TO_BIT1_8822B)
#define BITS_REG_STANDBY33_BIT0_TO_BIT1_8822B                                  \
	(BIT_MASK_REG_STANDBY33_BIT0_TO_BIT1_8822B                             \
	 << BIT_SHIFT_REG_STANDBY33_BIT0_TO_BIT1_8822B)
#define BIT_CLEAR_REG_STANDBY33_BIT0_TO_BIT1_8822B(x)                          \
	((x) & (~BITS_REG_STANDBY33_BIT0_TO_BIT1_8822B))
#define BIT_GET_REG_STANDBY33_BIT0_TO_BIT1_8822B(x)                            \
	(((x) >> BIT_SHIFT_REG_STANDBY33_BIT0_TO_BIT1_8822B) &                 \
	 BIT_MASK_REG_STANDBY33_BIT0_TO_BIT1_8822B)
#define BIT_SET_REG_STANDBY33_BIT0_TO_BIT1_8822B(x, v)                         \
	(BIT_CLEAR_REG_STANDBY33_BIT0_TO_BIT1_8822B(x) |                       \
	 BIT_REG_STANDBY33_BIT0_TO_BIT1_8822B(v))

#define BIT_SHIFT_REG_LOAD33_BIT0_TO_BIT1_8822B 8
#define BIT_MASK_REG_LOAD33_BIT0_TO_BIT1_8822B 0x3
#define BIT_REG_LOAD33_BIT0_TO_BIT1_8822B(x)                                   \
	(((x) & BIT_MASK_REG_LOAD33_BIT0_TO_BIT1_8822B)                        \
	 << BIT_SHIFT_REG_LOAD33_BIT0_TO_BIT1_8822B)
#define BITS_REG_LOAD33_BIT0_TO_BIT1_8822B                                     \
	(BIT_MASK_REG_LOAD33_BIT0_TO_BIT1_8822B                                \
	 << BIT_SHIFT_REG_LOAD33_BIT0_TO_BIT1_8822B)
#define BIT_CLEAR_REG_LOAD33_BIT0_TO_BIT1_8822B(x)                             \
	((x) & (~BITS_REG_LOAD33_BIT0_TO_BIT1_8822B))
#define BIT_GET_REG_LOAD33_BIT0_TO_BIT1_8822B(x)                               \
	(((x) >> BIT_SHIFT_REG_LOAD33_BIT0_TO_BIT1_8822B) &                    \
	 BIT_MASK_REG_LOAD33_BIT0_TO_BIT1_8822B)
#define BIT_SET_REG_LOAD33_BIT0_TO_BIT1_8822B(x, v)                            \
	(BIT_CLEAR_REG_LOAD33_BIT0_TO_BIT1_8822B(x) |                          \
	 BIT_REG_LOAD33_BIT0_TO_BIT1_8822B(v))

#define BIT_REG_BYPASS_L_8822B BIT(7)
#define BIT_REG_LDOF_L_8822B BIT(6)
#define BIT_REG_TYPE_L_V1_8822B BIT(5)
#define BIT_ARENB_L_8822B BIT(3)

#define BIT_SHIFT_CFC_L_8822B 1
#define BIT_MASK_CFC_L_8822B 0x3
#define BIT_CFC_L_8822B(x)                                                     \
	(((x) & BIT_MASK_CFC_L_8822B) << BIT_SHIFT_CFC_L_8822B)
#define BITS_CFC_L_8822B (BIT_MASK_CFC_L_8822B << BIT_SHIFT_CFC_L_8822B)
#define BIT_CLEAR_CFC_L_8822B(x) ((x) & (~BITS_CFC_L_8822B))
#define BIT_GET_CFC_L_8822B(x)                                                 \
	(((x) >> BIT_SHIFT_CFC_L_8822B) & BIT_MASK_CFC_L_8822B)
#define BIT_SET_CFC_L_8822B(x, v)                                              \
	(BIT_CLEAR_CFC_L_8822B(x) | BIT_CFC_L_8822B(v))

#define BIT_REG_OCPS_L_V1_8822B BIT(0)

/* 2 REG_MCUFW_CTRL_8822B */

#define BIT_SHIFT_RPWM_8822B 24
#define BIT_MASK_RPWM_8822B 0xff
#define BIT_RPWM_8822B(x) (((x) & BIT_MASK_RPWM_8822B) << BIT_SHIFT_RPWM_8822B)
#define BITS_RPWM_8822B (BIT_MASK_RPWM_8822B << BIT_SHIFT_RPWM_8822B)
#define BIT_CLEAR_RPWM_8822B(x) ((x) & (~BITS_RPWM_8822B))
#define BIT_GET_RPWM_8822B(x)                                                  \
	(((x) >> BIT_SHIFT_RPWM_8822B) & BIT_MASK_RPWM_8822B)
#define BIT_SET_RPWM_8822B(x, v) (BIT_CLEAR_RPWM_8822B(x) | BIT_RPWM_8822B(v))

#define BIT_ANA_PORT_EN_8822B BIT(22)
#define BIT_MAC_PORT_EN_8822B BIT(21)
#define BIT_BOOT_FSPI_EN_8822B BIT(20)
#define BIT_ROM_DLEN_8822B BIT(19)

#define BIT_SHIFT_ROM_PGE_8822B 16
#define BIT_MASK_ROM_PGE_8822B 0x7
#define BIT_ROM_PGE_8822B(x)                                                   \
	(((x) & BIT_MASK_ROM_PGE_8822B) << BIT_SHIFT_ROM_PGE_8822B)
#define BITS_ROM_PGE_8822B (BIT_MASK_ROM_PGE_8822B << BIT_SHIFT_ROM_PGE_8822B)
#define BIT_CLEAR_ROM_PGE_8822B(x) ((x) & (~BITS_ROM_PGE_8822B))
#define BIT_GET_ROM_PGE_8822B(x)                                               \
	(((x) >> BIT_SHIFT_ROM_PGE_8822B) & BIT_MASK_ROM_PGE_8822B)
#define BIT_SET_ROM_PGE_8822B(x, v)                                            \
	(BIT_CLEAR_ROM_PGE_8822B(x) | BIT_ROM_PGE_8822B(v))

#define BIT_FW_INIT_RDY_8822B BIT(15)
#define BIT_FW_DW_RDY_8822B BIT(14)

#define BIT_SHIFT_CPU_CLK_SEL_8822B 12
#define BIT_MASK_CPU_CLK_SEL_8822B 0x3
#define BIT_CPU_CLK_SEL_8822B(x)                                               \
	(((x) & BIT_MASK_CPU_CLK_SEL_8822B) << BIT_SHIFT_CPU_CLK_SEL_8822B)
#define BITS_CPU_CLK_SEL_8822B                                                 \
	(BIT_MASK_CPU_CLK_SEL_8822B << BIT_SHIFT_CPU_CLK_SEL_8822B)
#define BIT_CLEAR_CPU_CLK_SEL_8822B(x) ((x) & (~BITS_CPU_CLK_SEL_8822B))
#define BIT_GET_CPU_CLK_SEL_8822B(x)                                           \
	(((x) >> BIT_SHIFT_CPU_CLK_SEL_8822B) & BIT_MASK_CPU_CLK_SEL_8822B)
#define BIT_SET_CPU_CLK_SEL_8822B(x, v)                                        \
	(BIT_CLEAR_CPU_CLK_SEL_8822B(x) | BIT_CPU_CLK_SEL_8822B(v))

#define BIT_CCLK_CHG_MASK_8822B BIT(11)
#define BIT_EMEM__TXBUF_CHKSUM_OK_8822B BIT(10)
#define BIT_EMEM_TXBUF_DW_RDY_8822B BIT(9)
#define BIT_EMEM_CHKSUM_OK_8822B BIT(8)
#define BIT_EMEM_DW_OK_8822B BIT(7)
#define BIT_DMEM_CHKSUM_OK_8822B BIT(6)
#define BIT_DMEM_DW_OK_8822B BIT(5)
#define BIT_IMEM_CHKSUM_OK_8822B BIT(4)
#define BIT_IMEM_DW_OK_8822B BIT(3)
#define BIT_IMEM_BOOT_LOAD_CHKSUM_OK_8822B BIT(2)
#define BIT_IMEM_BOOT_LOAD_DW_OK_8822B BIT(1)
#define BIT_MCUFWDL_EN_8822B BIT(0)

/* 2 REG_MCU_TST_CFG_8822B */

#define BIT_SHIFT_C2H_MSG_8822B 0
#define BIT_MASK_C2H_MSG_8822B 0xffff
#define BIT_C2H_MSG_8822B(x)                                                   \
	(((x) & BIT_MASK_C2H_MSG_8822B) << BIT_SHIFT_C2H_MSG_8822B)
#define BITS_C2H_MSG_8822B (BIT_MASK_C2H_MSG_8822B << BIT_SHIFT_C2H_MSG_8822B)
#define BIT_CLEAR_C2H_MSG_8822B(x) ((x) & (~BITS_C2H_MSG_8822B))
#define BIT_GET_C2H_MSG_8822B(x)                                               \
	(((x) >> BIT_SHIFT_C2H_MSG_8822B) & BIT_MASK_C2H_MSG_8822B)
#define BIT_SET_C2H_MSG_8822B(x, v)                                            \
	(BIT_CLEAR_C2H_MSG_8822B(x) | BIT_C2H_MSG_8822B(v))

/* 2 REG_HMEBOX_E0_E1_8822B */

#define BIT_SHIFT_HOST_MSG_E1_8822B 16
#define BIT_MASK_HOST_MSG_E1_8822B 0xffff
#define BIT_HOST_MSG_E1_8822B(x)                                               \
	(((x) & BIT_MASK_HOST_MSG_E1_8822B) << BIT_SHIFT_HOST_MSG_E1_8822B)
#define BITS_HOST_MSG_E1_8822B                                                 \
	(BIT_MASK_HOST_MSG_E1_8822B << BIT_SHIFT_HOST_MSG_E1_8822B)
#define BIT_CLEAR_HOST_MSG_E1_8822B(x) ((x) & (~BITS_HOST_MSG_E1_8822B))
#define BIT_GET_HOST_MSG_E1_8822B(x)                                           \
	(((x) >> BIT_SHIFT_HOST_MSG_E1_8822B) & BIT_MASK_HOST_MSG_E1_8822B)
#define BIT_SET_HOST_MSG_E1_8822B(x, v)                                        \
	(BIT_CLEAR_HOST_MSG_E1_8822B(x) | BIT_HOST_MSG_E1_8822B(v))

#define BIT_SHIFT_HOST_MSG_E0_8822B 0
#define BIT_MASK_HOST_MSG_E0_8822B 0xffff
#define BIT_HOST_MSG_E0_8822B(x)                                               \
	(((x) & BIT_MASK_HOST_MSG_E0_8822B) << BIT_SHIFT_HOST_MSG_E0_8822B)
#define BITS_HOST_MSG_E0_8822B                                                 \
	(BIT_MASK_HOST_MSG_E0_8822B << BIT_SHIFT_HOST_MSG_E0_8822B)
#define BIT_CLEAR_HOST_MSG_E0_8822B(x) ((x) & (~BITS_HOST_MSG_E0_8822B))
#define BIT_GET_HOST_MSG_E0_8822B(x)                                           \
	(((x) >> BIT_SHIFT_HOST_MSG_E0_8822B) & BIT_MASK_HOST_MSG_E0_8822B)
#define BIT_SET_HOST_MSG_E0_8822B(x, v)                                        \
	(BIT_CLEAR_HOST_MSG_E0_8822B(x) | BIT_HOST_MSG_E0_8822B(v))

/* 2 REG_HMEBOX_E2_E3_8822B */

#define BIT_SHIFT_HOST_MSG_E3_8822B 16
#define BIT_MASK_HOST_MSG_E3_8822B 0xffff
#define BIT_HOST_MSG_E3_8822B(x)                                               \
	(((x) & BIT_MASK_HOST_MSG_E3_8822B) << BIT_SHIFT_HOST_MSG_E3_8822B)
#define BITS_HOST_MSG_E3_8822B                                                 \
	(BIT_MASK_HOST_MSG_E3_8822B << BIT_SHIFT_HOST_MSG_E3_8822B)
#define BIT_CLEAR_HOST_MSG_E3_8822B(x) ((x) & (~BITS_HOST_MSG_E3_8822B))
#define BIT_GET_HOST_MSG_E3_8822B(x)                                           \
	(((x) >> BIT_SHIFT_HOST_MSG_E3_8822B) & BIT_MASK_HOST_MSG_E3_8822B)
#define BIT_SET_HOST_MSG_E3_8822B(x, v)                                        \
	(BIT_CLEAR_HOST_MSG_E3_8822B(x) | BIT_HOST_MSG_E3_8822B(v))

#define BIT_SHIFT_HOST_MSG_E2_8822B 0
#define BIT_MASK_HOST_MSG_E2_8822B 0xffff
#define BIT_HOST_MSG_E2_8822B(x)                                               \
	(((x) & BIT_MASK_HOST_MSG_E2_8822B) << BIT_SHIFT_HOST_MSG_E2_8822B)
#define BITS_HOST_MSG_E2_8822B                                                 \
	(BIT_MASK_HOST_MSG_E2_8822B << BIT_SHIFT_HOST_MSG_E2_8822B)
#define BIT_CLEAR_HOST_MSG_E2_8822B(x) ((x) & (~BITS_HOST_MSG_E2_8822B))
#define BIT_GET_HOST_MSG_E2_8822B(x)                                           \
	(((x) >> BIT_SHIFT_HOST_MSG_E2_8822B) & BIT_MASK_HOST_MSG_E2_8822B)
#define BIT_SET_HOST_MSG_E2_8822B(x, v)                                        \
	(BIT_CLEAR_HOST_MSG_E2_8822B(x) | BIT_HOST_MSG_E2_8822B(v))

/* 2 REG_WLLPS_CTRL_8822B */
#define BIT_WLLPSOP_EABM_8822B BIT(31)
#define BIT_WLLPSOP_ACKF_8822B BIT(30)
#define BIT_WLLPSOP_DLDM_8822B BIT(29)
#define BIT_WLLPSOP_ESWR_8822B BIT(28)
#define BIT_WLLPSOP_PWMM_8822B BIT(27)
#define BIT_WLLPSOP_EECK_8822B BIT(26)
#define BIT_WLLPSOP_WLMACOFF_8822B BIT(25)
#define BIT_WLLPSOP_EXTAL_8822B BIT(24)
#define BIT_WL_SYNPON_VOLTSPDN_8822B BIT(23)
#define BIT_WLLPSOP_WLBBOFF_8822B BIT(22)
#define BIT_WLLPSOP_WLMEM_DS_8822B BIT(21)

#define BIT_SHIFT_LPLDH12_VADJ_STEP_DN_8822B 12
#define BIT_MASK_LPLDH12_VADJ_STEP_DN_8822B 0xf
#define BIT_LPLDH12_VADJ_STEP_DN_8822B(x)                                      \
	(((x) & BIT_MASK_LPLDH12_VADJ_STEP_DN_8822B)                           \
	 << BIT_SHIFT_LPLDH12_VADJ_STEP_DN_8822B)
#define BITS_LPLDH12_VADJ_STEP_DN_8822B                                        \
	(BIT_MASK_LPLDH12_VADJ_STEP_DN_8822B                                   \
	 << BIT_SHIFT_LPLDH12_VADJ_STEP_DN_8822B)
#define BIT_CLEAR_LPLDH12_VADJ_STEP_DN_8822B(x)                                \
	((x) & (~BITS_LPLDH12_VADJ_STEP_DN_8822B))
#define BIT_GET_LPLDH12_VADJ_STEP_DN_8822B(x)                                  \
	(((x) >> BIT_SHIFT_LPLDH12_VADJ_STEP_DN_8822B) &                       \
	 BIT_MASK_LPLDH12_VADJ_STEP_DN_8822B)
#define BIT_SET_LPLDH12_VADJ_STEP_DN_8822B(x, v)                               \
	(BIT_CLEAR_LPLDH12_VADJ_STEP_DN_8822B(x) |                             \
	 BIT_LPLDH12_VADJ_STEP_DN_8822B(v))

#define BIT_SHIFT_V15ADJ_L1_STEP_DN_8822B 8
#define BIT_MASK_V15ADJ_L1_STEP_DN_8822B 0x7
#define BIT_V15ADJ_L1_STEP_DN_8822B(x)                                         \
	(((x) & BIT_MASK_V15ADJ_L1_STEP_DN_8822B)                              \
	 << BIT_SHIFT_V15ADJ_L1_STEP_DN_8822B)
#define BITS_V15ADJ_L1_STEP_DN_8822B                                           \
	(BIT_MASK_V15ADJ_L1_STEP_DN_8822B << BIT_SHIFT_V15ADJ_L1_STEP_DN_8822B)
#define BIT_CLEAR_V15ADJ_L1_STEP_DN_8822B(x)                                   \
	((x) & (~BITS_V15ADJ_L1_STEP_DN_8822B))
#define BIT_GET_V15ADJ_L1_STEP_DN_8822B(x)                                     \
	(((x) >> BIT_SHIFT_V15ADJ_L1_STEP_DN_8822B) &                          \
	 BIT_MASK_V15ADJ_L1_STEP_DN_8822B)
#define BIT_SET_V15ADJ_L1_STEP_DN_8822B(x, v)                                  \
	(BIT_CLEAR_V15ADJ_L1_STEP_DN_8822B(x) | BIT_V15ADJ_L1_STEP_DN_8822B(v))

#define BIT_REGU_32K_CLK_EN_8822B BIT(1)
#define BIT_WL_LPS_EN_8822B BIT(0)

/* 2 REG_AFE_CTRL5_8822B */
#define BIT_BB_DBG_SEL_AFE_SDM_BIT0_8822B BIT(31)
#define BIT_ORDER_SDM_8822B BIT(30)
#define BIT_RFE_SEL_SDM_8822B BIT(29)

#define BIT_SHIFT_REF_SEL_8822B 25
#define BIT_MASK_REF_SEL_8822B 0xf
#define BIT_REF_SEL_8822B(x)                                                   \
	(((x) & BIT_MASK_REF_SEL_8822B) << BIT_SHIFT_REF_SEL_8822B)
#define BITS_REF_SEL_8822B (BIT_MASK_REF_SEL_8822B << BIT_SHIFT_REF_SEL_8822B)
#define BIT_CLEAR_REF_SEL_8822B(x) ((x) & (~BITS_REF_SEL_8822B))
#define BIT_GET_REF_SEL_8822B(x)                                               \
	(((x) >> BIT_SHIFT_REF_SEL_8822B) & BIT_MASK_REF_SEL_8822B)
#define BIT_SET_REF_SEL_8822B(x, v)                                            \
	(BIT_CLEAR_REF_SEL_8822B(x) | BIT_REF_SEL_8822B(v))

#define BIT_SHIFT_F0F_SDM_8822B 12
#define BIT_MASK_F0F_SDM_8822B 0x1fff
#define BIT_F0F_SDM_8822B(x)                                                   \
	(((x) & BIT_MASK_F0F_SDM_8822B) << BIT_SHIFT_F0F_SDM_8822B)
#define BITS_F0F_SDM_8822B (BIT_MASK_F0F_SDM_8822B << BIT_SHIFT_F0F_SDM_8822B)
#define BIT_CLEAR_F0F_SDM_8822B(x) ((x) & (~BITS_F0F_SDM_8822B))
#define BIT_GET_F0F_SDM_8822B(x)                                               \
	(((x) >> BIT_SHIFT_F0F_SDM_8822B) & BIT_MASK_F0F_SDM_8822B)
#define BIT_SET_F0F_SDM_8822B(x, v)                                            \
	(BIT_CLEAR_F0F_SDM_8822B(x) | BIT_F0F_SDM_8822B(v))

#define BIT_SHIFT_F0N_SDM_8822B 9
#define BIT_MASK_F0N_SDM_8822B 0x7
#define BIT_F0N_SDM_8822B(x)                                                   \
	(((x) & BIT_MASK_F0N_SDM_8822B) << BIT_SHIFT_F0N_SDM_8822B)
#define BITS_F0N_SDM_8822B (BIT_MASK_F0N_SDM_8822B << BIT_SHIFT_F0N_SDM_8822B)
#define BIT_CLEAR_F0N_SDM_8822B(x) ((x) & (~BITS_F0N_SDM_8822B))
#define BIT_GET_F0N_SDM_8822B(x)                                               \
	(((x) >> BIT_SHIFT_F0N_SDM_8822B) & BIT_MASK_F0N_SDM_8822B)
#define BIT_SET_F0N_SDM_8822B(x, v)                                            \
	(BIT_CLEAR_F0N_SDM_8822B(x) | BIT_F0N_SDM_8822B(v))

#define BIT_SHIFT_DIVN_SDM_8822B 3
#define BIT_MASK_DIVN_SDM_8822B 0x3f
#define BIT_DIVN_SDM_8822B(x)                                                  \
	(((x) & BIT_MASK_DIVN_SDM_8822B) << BIT_SHIFT_DIVN_SDM_8822B)
#define BITS_DIVN_SDM_8822B                                                    \
	(BIT_MASK_DIVN_SDM_8822B << BIT_SHIFT_DIVN_SDM_8822B)
#define BIT_CLEAR_DIVN_SDM_8822B(x) ((x) & (~BITS_DIVN_SDM_8822B))
#define BIT_GET_DIVN_SDM_8822B(x)                                              \
	(((x) >> BIT_SHIFT_DIVN_SDM_8822B) & BIT_MASK_DIVN_SDM_8822B)
#define BIT_SET_DIVN_SDM_8822B(x, v)                                           \
	(BIT_CLEAR_DIVN_SDM_8822B(x) | BIT_DIVN_SDM_8822B(v))

/* 2 REG_GPIO_DEBOUNCE_CTRL_8822B */
#define BIT_WLGP_DBC1EN_8822B BIT(15)

#define BIT_SHIFT_WLGP_DBC1_8822B 8
#define BIT_MASK_WLGP_DBC1_8822B 0xf
#define BIT_WLGP_DBC1_8822B(x)                                                 \
	(((x) & BIT_MASK_WLGP_DBC1_8822B) << BIT_SHIFT_WLGP_DBC1_8822B)
#define BITS_WLGP_DBC1_8822B                                                   \
	(BIT_MASK_WLGP_DBC1_8822B << BIT_SHIFT_WLGP_DBC1_8822B)
#define BIT_CLEAR_WLGP_DBC1_8822B(x) ((x) & (~BITS_WLGP_DBC1_8822B))
#define BIT_GET_WLGP_DBC1_8822B(x)                                             \
	(((x) >> BIT_SHIFT_WLGP_DBC1_8822B) & BIT_MASK_WLGP_DBC1_8822B)
#define BIT_SET_WLGP_DBC1_8822B(x, v)                                          \
	(BIT_CLEAR_WLGP_DBC1_8822B(x) | BIT_WLGP_DBC1_8822B(v))

#define BIT_WLGP_DBC0EN_8822B BIT(7)

#define BIT_SHIFT_WLGP_DBC0_8822B 0
#define BIT_MASK_WLGP_DBC0_8822B 0xf
#define BIT_WLGP_DBC0_8822B(x)                                                 \
	(((x) & BIT_MASK_WLGP_DBC0_8822B) << BIT_SHIFT_WLGP_DBC0_8822B)
#define BITS_WLGP_DBC0_8822B                                                   \
	(BIT_MASK_WLGP_DBC0_8822B << BIT_SHIFT_WLGP_DBC0_8822B)
#define BIT_CLEAR_WLGP_DBC0_8822B(x) ((x) & (~BITS_WLGP_DBC0_8822B))
#define BIT_GET_WLGP_DBC0_8822B(x)                                             \
	(((x) >> BIT_SHIFT_WLGP_DBC0_8822B) & BIT_MASK_WLGP_DBC0_8822B)
#define BIT_SET_WLGP_DBC0_8822B(x, v)                                          \
	(BIT_CLEAR_WLGP_DBC0_8822B(x) | BIT_WLGP_DBC0_8822B(v))

/* 2 REG_RPWM2_8822B */

#define BIT_SHIFT_RPWM2_8822B 16
#define BIT_MASK_RPWM2_8822B 0xffff
#define BIT_RPWM2_8822B(x)                                                     \
	(((x) & BIT_MASK_RPWM2_8822B) << BIT_SHIFT_RPWM2_8822B)
#define BITS_RPWM2_8822B (BIT_MASK_RPWM2_8822B << BIT_SHIFT_RPWM2_8822B)
#define BIT_CLEAR_RPWM2_8822B(x) ((x) & (~BITS_RPWM2_8822B))
#define BIT_GET_RPWM2_8822B(x)                                                 \
	(((x) >> BIT_SHIFT_RPWM2_8822B) & BIT_MASK_RPWM2_8822B)
#define BIT_SET_RPWM2_8822B(x, v)                                              \
	(BIT_CLEAR_RPWM2_8822B(x) | BIT_RPWM2_8822B(v))

/* 2 REG_SYSON_FSM_MON_8822B */

#define BIT_SHIFT_FSM_MON_SEL_8822B 24
#define BIT_MASK_FSM_MON_SEL_8822B 0x7
#define BIT_FSM_MON_SEL_8822B(x)                                               \
	(((x) & BIT_MASK_FSM_MON_SEL_8822B) << BIT_SHIFT_FSM_MON_SEL_8822B)
#define BITS_FSM_MON_SEL_8822B                                                 \
	(BIT_MASK_FSM_MON_SEL_8822B << BIT_SHIFT_FSM_MON_SEL_8822B)
#define BIT_CLEAR_FSM_MON_SEL_8822B(x) ((x) & (~BITS_FSM_MON_SEL_8822B))
#define BIT_GET_FSM_MON_SEL_8822B(x)                                           \
	(((x) >> BIT_SHIFT_FSM_MON_SEL_8822B) & BIT_MASK_FSM_MON_SEL_8822B)
#define BIT_SET_FSM_MON_SEL_8822B(x, v)                                        \
	(BIT_CLEAR_FSM_MON_SEL_8822B(x) | BIT_FSM_MON_SEL_8822B(v))

#define BIT_DOP_ELDO_8822B BIT(23)
#define BIT_FSM_MON_UPD_8822B BIT(15)

#define BIT_SHIFT_FSM_PAR_8822B 0
#define BIT_MASK_FSM_PAR_8822B 0x7fff
#define BIT_FSM_PAR_8822B(x)                                                   \
	(((x) & BIT_MASK_FSM_PAR_8822B) << BIT_SHIFT_FSM_PAR_8822B)
#define BITS_FSM_PAR_8822B (BIT_MASK_FSM_PAR_8822B << BIT_SHIFT_FSM_PAR_8822B)
#define BIT_CLEAR_FSM_PAR_8822B(x) ((x) & (~BITS_FSM_PAR_8822B))
#define BIT_GET_FSM_PAR_8822B(x)                                               \
	(((x) >> BIT_SHIFT_FSM_PAR_8822B) & BIT_MASK_FSM_PAR_8822B)
#define BIT_SET_FSM_PAR_8822B(x, v)                                            \
	(BIT_CLEAR_FSM_PAR_8822B(x) | BIT_FSM_PAR_8822B(v))

/* 2 REG_AFE_CTRL6_8822B */

#define BIT_SHIFT_BB_DBG_SEL_AFE_SDM_BIT3_1_8822B 0
#define BIT_MASK_BB_DBG_SEL_AFE_SDM_BIT3_1_8822B 0x7
#define BIT_BB_DBG_SEL_AFE_SDM_BIT3_1_8822B(x)                                 \
	(((x) & BIT_MASK_BB_DBG_SEL_AFE_SDM_BIT3_1_8822B)                      \
	 << BIT_SHIFT_BB_DBG_SEL_AFE_SDM_BIT3_1_8822B)
#define BITS_BB_DBG_SEL_AFE_SDM_BIT3_1_8822B                                   \
	(BIT_MASK_BB_DBG_SEL_AFE_SDM_BIT3_1_8822B                              \
	 << BIT_SHIFT_BB_DBG_SEL_AFE_SDM_BIT3_1_8822B)
#define BIT_CLEAR_BB_DBG_SEL_AFE_SDM_BIT3_1_8822B(x)                           \
	((x) & (~BITS_BB_DBG_SEL_AFE_SDM_BIT3_1_8822B))
#define BIT_GET_BB_DBG_SEL_AFE_SDM_BIT3_1_8822B(x)                             \
	(((x) >> BIT_SHIFT_BB_DBG_SEL_AFE_SDM_BIT3_1_8822B) &                  \
	 BIT_MASK_BB_DBG_SEL_AFE_SDM_BIT3_1_8822B)
#define BIT_SET_BB_DBG_SEL_AFE_SDM_BIT3_1_8822B(x, v)                          \
	(BIT_CLEAR_BB_DBG_SEL_AFE_SDM_BIT3_1_8822B(x) |                        \
	 BIT_BB_DBG_SEL_AFE_SDM_BIT3_1_8822B(v))

/* 2 REG_PMC_DBG_CTRL1_8822B */
#define BIT_BT_INT_EN_8822B BIT(31)

#define BIT_SHIFT_RD_WR_WIFI_BT_INFO_8822B 16
#define BIT_MASK_RD_WR_WIFI_BT_INFO_8822B 0x7fff
#define BIT_RD_WR_WIFI_BT_INFO_8822B(x)                                        \
	(((x) & BIT_MASK_RD_WR_WIFI_BT_INFO_8822B)                             \
	 << BIT_SHIFT_RD_WR_WIFI_BT_INFO_8822B)
#define BITS_RD_WR_WIFI_BT_INFO_8822B                                          \
	(BIT_MASK_RD_WR_WIFI_BT_INFO_8822B                                     \
	 << BIT_SHIFT_RD_WR_WIFI_BT_INFO_8822B)
#define BIT_CLEAR_RD_WR_WIFI_BT_INFO_8822B(x)                                  \
	((x) & (~BITS_RD_WR_WIFI_BT_INFO_8822B))
#define BIT_GET_RD_WR_WIFI_BT_INFO_8822B(x)                                    \
	(((x) >> BIT_SHIFT_RD_WR_WIFI_BT_INFO_8822B) &                         \
	 BIT_MASK_RD_WR_WIFI_BT_INFO_8822B)
#define BIT_SET_RD_WR_WIFI_BT_INFO_8822B(x, v)                                 \
	(BIT_CLEAR_RD_WR_WIFI_BT_INFO_8822B(x) |                               \
	 BIT_RD_WR_WIFI_BT_INFO_8822B(v))

#define BIT_PMC_WR_OVF_8822B BIT(8)

#define BIT_SHIFT_WLPMC_ERRINT_8822B 0
#define BIT_MASK_WLPMC_ERRINT_8822B 0xff
#define BIT_WLPMC_ERRINT_8822B(x)                                              \
	(((x) & BIT_MASK_WLPMC_ERRINT_8822B) << BIT_SHIFT_WLPMC_ERRINT_8822B)
#define BITS_WLPMC_ERRINT_8822B                                                \
	(BIT_MASK_WLPMC_ERRINT_8822B << BIT_SHIFT_WLPMC_ERRINT_8822B)
#define BIT_CLEAR_WLPMC_ERRINT_8822B(x) ((x) & (~BITS_WLPMC_ERRINT_8822B))
#define BIT_GET_WLPMC_ERRINT_8822B(x)                                          \
	(((x) >> BIT_SHIFT_WLPMC_ERRINT_8822B) & BIT_MASK_WLPMC_ERRINT_8822B)
#define BIT_SET_WLPMC_ERRINT_8822B(x, v)                                       \
	(BIT_CLEAR_WLPMC_ERRINT_8822B(x) | BIT_WLPMC_ERRINT_8822B(v))

/* 2 REG_AFE_CTRL7_8822B */

#define BIT_SHIFT_SEL_V_8822B 30
#define BIT_MASK_SEL_V_8822B 0x3
#define BIT_SEL_V_8822B(x)                                                     \
	(((x) & BIT_MASK_SEL_V_8822B) << BIT_SHIFT_SEL_V_8822B)
#define BITS_SEL_V_8822B (BIT_MASK_SEL_V_8822B << BIT_SHIFT_SEL_V_8822B)
#define BIT_CLEAR_SEL_V_8822B(x) ((x) & (~BITS_SEL_V_8822B))
#define BIT_GET_SEL_V_8822B(x)                                                 \
	(((x) >> BIT_SHIFT_SEL_V_8822B) & BIT_MASK_SEL_V_8822B)
#define BIT_SET_SEL_V_8822B(x, v)                                              \
	(BIT_CLEAR_SEL_V_8822B(x) | BIT_SEL_V_8822B(v))

#define BIT_SEL_LDO_PC_8822B BIT(29)

#define BIT_SHIFT_CK_MON_SEL_8822B 26
#define BIT_MASK_CK_MON_SEL_8822B 0x7
#define BIT_CK_MON_SEL_8822B(x)                                                \
	(((x) & BIT_MASK_CK_MON_SEL_8822B) << BIT_SHIFT_CK_MON_SEL_8822B)
#define BITS_CK_MON_SEL_8822B                                                  \
	(BIT_MASK_CK_MON_SEL_8822B << BIT_SHIFT_CK_MON_SEL_8822B)
#define BIT_CLEAR_CK_MON_SEL_8822B(x) ((x) & (~BITS_CK_MON_SEL_8822B))
#define BIT_GET_CK_MON_SEL_8822B(x)                                            \
	(((x) >> BIT_SHIFT_CK_MON_SEL_8822B) & BIT_MASK_CK_MON_SEL_8822B)
#define BIT_SET_CK_MON_SEL_8822B(x, v)                                         \
	(BIT_CLEAR_CK_MON_SEL_8822B(x) | BIT_CK_MON_SEL_8822B(v))

#define BIT_CK_MON_EN_8822B BIT(25)
#define BIT_FREF_EDGE_8822B BIT(24)
#define BIT_CK320M_EN_8822B BIT(23)
#define BIT_CK_5M_EN_8822B BIT(22)
#define BIT_TESTEN_8822B BIT(21)

/* 2 REG_HIMR0_8822B */
#define BIT_TIMEOUT_INTERRUPT2_MASK_8822B BIT(31)
#define BIT_TIMEOUT_INTERRUTP1_MASK_8822B BIT(30)
#define BIT_PSTIMEOUT_MSK_8822B BIT(29)
#define BIT_GTINT4_MSK_8822B BIT(28)
#define BIT_GTINT3_MSK_8822B BIT(27)
#define BIT_TXBCN0ERR_MSK_8822B BIT(26)
#define BIT_TXBCN0OK_MSK_8822B BIT(25)
#define BIT_TSF_BIT32_TOGGLE_MSK_8822B BIT(24)
#define BIT_BCNDMAINT0_MSK_8822B BIT(20)
#define BIT_BCNDERR0_MSK_8822B BIT(16)
#define BIT_HSISR_IND_ON_INT_MSK_8822B BIT(15)
#define BIT_HISR3_IND_INT_MSK_8822B BIT(14)
#define BIT_HISR2_IND_INT_MSK_8822B BIT(13)
#define BIT_HISR1_IND_MSK_8822B BIT(11)
#define BIT_C2HCMD_MSK_8822B BIT(10)
#define BIT_CPWM2_MSK_8822B BIT(9)
#define BIT_CPWM_MSK_8822B BIT(8)
#define BIT_HIGHDOK_MSK_8822B BIT(7)
#define BIT_MGTDOK_MSK_8822B BIT(6)
#define BIT_BKDOK_MSK_8822B BIT(5)
#define BIT_BEDOK_MSK_8822B BIT(4)
#define BIT_VIDOK_MSK_8822B BIT(3)
#define BIT_VODOK_MSK_8822B BIT(2)
#define BIT_RDU_MSK_8822B BIT(1)
#define BIT_RXOK_MSK_8822B BIT(0)

/* 2 REG_HISR0_8822B */
#define BIT_PSTIMEOUT2_8822B BIT(31)
#define BIT_PSTIMEOUT1_8822B BIT(30)
#define BIT_PSTIMEOUT_8822B BIT(29)
#define BIT_GTINT4_8822B BIT(28)
#define BIT_GTINT3_8822B BIT(27)
#define BIT_TXBCN0ERR_8822B BIT(26)
#define BIT_TXBCN0OK_8822B BIT(25)
#define BIT_TSF_BIT32_TOGGLE_8822B BIT(24)
#define BIT_BCNDMAINT0_8822B BIT(20)
#define BIT_BCNDERR0_8822B BIT(16)
#define BIT_HSISR_IND_ON_INT_8822B BIT(15)
#define BIT_HISR3_IND_INT_8822B BIT(14)
#define BIT_HISR2_IND_INT_8822B BIT(13)
#define BIT_HISR1_IND_INT_8822B BIT(11)
#define BIT_C2HCMD_8822B BIT(10)
#define BIT_CPWM2_8822B BIT(9)
#define BIT_CPWM_8822B BIT(8)
#define BIT_HIGHDOK_8822B BIT(7)
#define BIT_MGTDOK_8822B BIT(6)
#define BIT_BKDOK_8822B BIT(5)
#define BIT_BEDOK_8822B BIT(4)
#define BIT_VIDOK_8822B BIT(3)
#define BIT_VODOK_8822B BIT(2)
#define BIT_RDU_8822B BIT(1)
#define BIT_RXOK_8822B BIT(0)

/* 2 REG_HIMR1_8822B */
#define BIT_TXFIFO_TH_INT_8822B BIT(30)
#define BIT_BTON_STS_UPDATE_MASK_8822B BIT(29)
#define BIT_BCNDMAINT7__MSK_8822B BIT(27)
#define BIT_BCNDMAINT6__MSK_8822B BIT(26)
#define BIT_BCNDMAINT5__MSK_8822B BIT(25)
#define BIT_BCNDMAINT4__MSK_8822B BIT(24)
#define BIT_BCNDMAINT3_MSK_8822B BIT(23)
#define BIT_BCNDMAINT2_MSK_8822B BIT(22)
#define BIT_BCNDMAINT1_MSK_8822B BIT(21)
#define BIT_BCNDERR7_MSK_8822B BIT(20)
#define BIT_BCNDERR6_MSK_8822B BIT(19)
#define BIT_BCNDERR5_MSK_8822B BIT(18)
#define BIT_BCNDERR4_MSK_8822B BIT(17)
#define BIT_BCNDERR3_MSK_8822B BIT(16)
#define BIT_BCNDERR2_MSK_8822B BIT(15)
#define BIT_BCNDERR1_MSK_8822B BIT(14)
#define BIT_ATIMEND_E_V1_MSK_8822B BIT(12)
#define BIT_TXERR_MSK_8822B BIT(11)
#define BIT_RXERR_MSK_8822B BIT(10)
#define BIT_TXFOVW_MSK_8822B BIT(9)
#define BIT_FOVW_MSK_8822B BIT(8)
#define BIT_CPU_MGQ_TXDONE_MSK_8822B BIT(5)
#define BIT_PS_TIMER_C_MSK_8822B BIT(4)
#define BIT_PS_TIMER_B_MSK_8822B BIT(3)
#define BIT_PS_TIMER_A_MSK_8822B BIT(2)
#define BIT_CPUMGQ_TX_TIMER_MSK_8822B BIT(1)

/* 2 REG_HISR1_8822B */
#define BIT_TXFIFO_TH_INT_8822B BIT(30)
#define BIT_BTON_STS_UPDATE_INT_8822B BIT(29)
#define BIT_BCNDMAINT7_8822B BIT(27)
#define BIT_BCNDMAINT6_8822B BIT(26)
#define BIT_BCNDMAINT5_8822B BIT(25)
#define BIT_BCNDMAINT4_8822B BIT(24)
#define BIT_BCNDMAINT3_8822B BIT(23)
#define BIT_BCNDMAINT2_8822B BIT(22)
#define BIT_BCNDMAINT1_8822B BIT(21)
#define BIT_BCNDERR7_8822B BIT(20)
#define BIT_BCNDERR6_8822B BIT(19)
#define BIT_BCNDERR5_8822B BIT(18)
#define BIT_BCNDERR4_8822B BIT(17)
#define BIT_BCNDERR3_8822B BIT(16)
#define BIT_BCNDERR2_8822B BIT(15)
#define BIT_BCNDERR1_8822B BIT(14)
#define BIT_ATIMEND_E_V1_INT_8822B BIT(12)
#define BIT_TXERR_INT_8822B BIT(11)
#define BIT_RXERR_INT_8822B BIT(10)
#define BIT_TXFOVW_8822B BIT(9)
#define BIT_FOVW_8822B BIT(8)
#define BIT_CPU_MGQ_TXDONE_8822B BIT(5)
#define BIT_PS_TIMER_C_8822B BIT(4)
#define BIT_PS_TIMER_B_8822B BIT(3)
#define BIT_PS_TIMER_A_8822B BIT(2)
#define BIT_CPUMGQ_TX_TIMER_8822B BIT(1)

/* 2 REG_DBG_PORT_SEL_8822B */

#define BIT_SHIFT_DEBUG_ST_8822B 0
#define BIT_MASK_DEBUG_ST_8822B 0xffffffffL
#define BIT_DEBUG_ST_8822B(x)                                                  \
	(((x) & BIT_MASK_DEBUG_ST_8822B) << BIT_SHIFT_DEBUG_ST_8822B)
#define BITS_DEBUG_ST_8822B                                                    \
	(BIT_MASK_DEBUG_ST_8822B << BIT_SHIFT_DEBUG_ST_8822B)
#define BIT_CLEAR_DEBUG_ST_8822B(x) ((x) & (~BITS_DEBUG_ST_8822B))
#define BIT_GET_DEBUG_ST_8822B(x)                                              \
	(((x) >> BIT_SHIFT_DEBUG_ST_8822B) & BIT_MASK_DEBUG_ST_8822B)
#define BIT_SET_DEBUG_ST_8822B(x, v)                                           \
	(BIT_CLEAR_DEBUG_ST_8822B(x) | BIT_DEBUG_ST_8822B(v))

/* 2 REG_PAD_CTRL2_8822B */
#define BIT_USB3_USB2_TRANSITION_8822B BIT(20)

#define BIT_SHIFT_USB23_SW_MODE_V1_8822B 18
#define BIT_MASK_USB23_SW_MODE_V1_8822B 0x3
#define BIT_USB23_SW_MODE_V1_8822B(x)                                          \
	(((x) & BIT_MASK_USB23_SW_MODE_V1_8822B)                               \
	 << BIT_SHIFT_USB23_SW_MODE_V1_8822B)
#define BITS_USB23_SW_MODE_V1_8822B                                            \
	(BIT_MASK_USB23_SW_MODE_V1_8822B << BIT_SHIFT_USB23_SW_MODE_V1_8822B)
#define BIT_CLEAR_USB23_SW_MODE_V1_8822B(x)                                    \
	((x) & (~BITS_USB23_SW_MODE_V1_8822B))
#define BIT_GET_USB23_SW_MODE_V1_8822B(x)                                      \
	(((x) >> BIT_SHIFT_USB23_SW_MODE_V1_8822B) &                           \
	 BIT_MASK_USB23_SW_MODE_V1_8822B)
#define BIT_SET_USB23_SW_MODE_V1_8822B(x, v)                                   \
	(BIT_CLEAR_USB23_SW_MODE_V1_8822B(x) | BIT_USB23_SW_MODE_V1_8822B(v))

#define BIT_NO_PDN_CHIPOFF_V1_8822B BIT(17)
#define BIT_RSM_EN_V1_8822B BIT(16)

#define BIT_SHIFT_MATCH_CNT_8822B 8
#define BIT_MASK_MATCH_CNT_8822B 0xff
#define BIT_MATCH_CNT_8822B(x)                                                 \
	(((x) & BIT_MASK_MATCH_CNT_8822B) << BIT_SHIFT_MATCH_CNT_8822B)
#define BITS_MATCH_CNT_8822B                                                   \
	(BIT_MASK_MATCH_CNT_8822B << BIT_SHIFT_MATCH_CNT_8822B)
#define BIT_CLEAR_MATCH_CNT_8822B(x) ((x) & (~BITS_MATCH_CNT_8822B))
#define BIT_GET_MATCH_CNT_8822B(x)                                             \
	(((x) >> BIT_SHIFT_MATCH_CNT_8822B) & BIT_MASK_MATCH_CNT_8822B)
#define BIT_SET_MATCH_CNT_8822B(x, v)                                          \
	(BIT_CLEAR_MATCH_CNT_8822B(x) | BIT_MATCH_CNT_8822B(v))

#define BIT_LD_B12V_EN_8822B BIT(7)
#define BIT_EECS_IOSEL_V1_8822B BIT(6)
#define BIT_EECS_DATA_O_V1_8822B BIT(5)
#define BIT_EECS_DATA_I_V1_8822B BIT(4)
#define BIT_EESK_IOSEL_V1_8822B BIT(2)
#define BIT_EESK_DATA_O_V1_8822B BIT(1)
#define BIT_EESK_DATA_I_V1_8822B BIT(0)

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_PMC_DBG_CTRL2_8822B */

#define BIT_SHIFT_EFUSE_BURN_GNT_8822B 24
#define BIT_MASK_EFUSE_BURN_GNT_8822B 0xff
#define BIT_EFUSE_BURN_GNT_8822B(x)                                            \
	(((x) & BIT_MASK_EFUSE_BURN_GNT_8822B)                                 \
	 << BIT_SHIFT_EFUSE_BURN_GNT_8822B)
#define BITS_EFUSE_BURN_GNT_8822B                                              \
	(BIT_MASK_EFUSE_BURN_GNT_8822B << BIT_SHIFT_EFUSE_BURN_GNT_8822B)
#define BIT_CLEAR_EFUSE_BURN_GNT_8822B(x) ((x) & (~BITS_EFUSE_BURN_GNT_8822B))
#define BIT_GET_EFUSE_BURN_GNT_8822B(x)                                        \
	(((x) >> BIT_SHIFT_EFUSE_BURN_GNT_8822B) &                             \
	 BIT_MASK_EFUSE_BURN_GNT_8822B)
#define BIT_SET_EFUSE_BURN_GNT_8822B(x, v)                                     \
	(BIT_CLEAR_EFUSE_BURN_GNT_8822B(x) | BIT_EFUSE_BURN_GNT_8822B(v))

#define BIT_STOP_WL_PMC_8822B BIT(9)
#define BIT_STOP_SYM_PMC_8822B BIT(8)
#define BIT_REG_RST_WLPMC_8822B BIT(5)
#define BIT_REG_RST_PD12N_8822B BIT(4)
#define BIT_SYSON_DIS_WLREG_WRMSK_8822B BIT(3)
#define BIT_SYSON_DIS_PMCREG_WRMSK_8822B BIT(2)

#define BIT_SHIFT_SYSON_REG_ARB_8822B 0
#define BIT_MASK_SYSON_REG_ARB_8822B 0x3
#define BIT_SYSON_REG_ARB_8822B(x)                                             \
	(((x) & BIT_MASK_SYSON_REG_ARB_8822B) << BIT_SHIFT_SYSON_REG_ARB_8822B)
#define BITS_SYSON_REG_ARB_8822B                                               \
	(BIT_MASK_SYSON_REG_ARB_8822B << BIT_SHIFT_SYSON_REG_ARB_8822B)
#define BIT_CLEAR_SYSON_REG_ARB_8822B(x) ((x) & (~BITS_SYSON_REG_ARB_8822B))
#define BIT_GET_SYSON_REG_ARB_8822B(x)                                         \
	(((x) >> BIT_SHIFT_SYSON_REG_ARB_8822B) & BIT_MASK_SYSON_REG_ARB_8822B)
#define BIT_SET_SYSON_REG_ARB_8822B(x, v)                                      \
	(BIT_CLEAR_SYSON_REG_ARB_8822B(x) | BIT_SYSON_REG_ARB_8822B(v))

/* 2 REG_BIST_CTRL_8822B */
#define BIT_BIST_USB_DIS_8822B BIT(27)
#define BIT_BIST_PCI_DIS_8822B BIT(26)
#define BIT_BIST_BT_DIS_8822B BIT(25)
#define BIT_BIST_WL_DIS_8822B BIT(24)

#define BIT_SHIFT_BIST_RPT_SEL_8822B 16
#define BIT_MASK_BIST_RPT_SEL_8822B 0xf
#define BIT_BIST_RPT_SEL_8822B(x)                                              \
	(((x) & BIT_MASK_BIST_RPT_SEL_8822B) << BIT_SHIFT_BIST_RPT_SEL_8822B)
#define BITS_BIST_RPT_SEL_8822B                                                \
	(BIT_MASK_BIST_RPT_SEL_8822B << BIT_SHIFT_BIST_RPT_SEL_8822B)
#define BIT_CLEAR_BIST_RPT_SEL_8822B(x) ((x) & (~BITS_BIST_RPT_SEL_8822B))
#define BIT_GET_BIST_RPT_SEL_8822B(x)                                          \
	(((x) >> BIT_SHIFT_BIST_RPT_SEL_8822B) & BIT_MASK_BIST_RPT_SEL_8822B)
#define BIT_SET_BIST_RPT_SEL_8822B(x, v)                                       \
	(BIT_CLEAR_BIST_RPT_SEL_8822B(x) | BIT_BIST_RPT_SEL_8822B(v))

#define BIT_BIST_RESUME_PS_8822B BIT(4)
#define BIT_BIST_RESUME_8822B BIT(3)
#define BIT_BIST_NORMAL_8822B BIT(2)
#define BIT_BIST_RSTN_8822B BIT(1)
#define BIT_BIST_CLK_EN_8822B BIT(0)

/* 2 REG_BIST_RPT_8822B */

#define BIT_SHIFT_MBIST_REPORT_8822B 0
#define BIT_MASK_MBIST_REPORT_8822B 0xffffffffL
#define BIT_MBIST_REPORT_8822B(x)                                              \
	(((x) & BIT_MASK_MBIST_REPORT_8822B) << BIT_SHIFT_MBIST_REPORT_8822B)
#define BITS_MBIST_REPORT_8822B                                                \
	(BIT_MASK_MBIST_REPORT_8822B << BIT_SHIFT_MBIST_REPORT_8822B)
#define BIT_CLEAR_MBIST_REPORT_8822B(x) ((x) & (~BITS_MBIST_REPORT_8822B))
#define BIT_GET_MBIST_REPORT_8822B(x)                                          \
	(((x) >> BIT_SHIFT_MBIST_REPORT_8822B) & BIT_MASK_MBIST_REPORT_8822B)
#define BIT_SET_MBIST_REPORT_8822B(x, v)                                       \
	(BIT_CLEAR_MBIST_REPORT_8822B(x) | BIT_MBIST_REPORT_8822B(v))

/* 2 REG_MEM_CTRL_8822B */
#define BIT_UMEM_RME_8822B BIT(31)

#define BIT_SHIFT_BT_SPRAM_8822B 28
#define BIT_MASK_BT_SPRAM_8822B 0x3
#define BIT_BT_SPRAM_8822B(x)                                                  \
	(((x) & BIT_MASK_BT_SPRAM_8822B) << BIT_SHIFT_BT_SPRAM_8822B)
#define BITS_BT_SPRAM_8822B                                                    \
	(BIT_MASK_BT_SPRAM_8822B << BIT_SHIFT_BT_SPRAM_8822B)
#define BIT_CLEAR_BT_SPRAM_8822B(x) ((x) & (~BITS_BT_SPRAM_8822B))
#define BIT_GET_BT_SPRAM_8822B(x)                                              \
	(((x) >> BIT_SHIFT_BT_SPRAM_8822B) & BIT_MASK_BT_SPRAM_8822B)
#define BIT_SET_BT_SPRAM_8822B(x, v)                                           \
	(BIT_CLEAR_BT_SPRAM_8822B(x) | BIT_BT_SPRAM_8822B(v))

#define BIT_SHIFT_BT_ROM_8822B 24
#define BIT_MASK_BT_ROM_8822B 0xf
#define BIT_BT_ROM_8822B(x)                                                    \
	(((x) & BIT_MASK_BT_ROM_8822B) << BIT_SHIFT_BT_ROM_8822B)
#define BITS_BT_ROM_8822B (BIT_MASK_BT_ROM_8822B << BIT_SHIFT_BT_ROM_8822B)
#define BIT_CLEAR_BT_ROM_8822B(x) ((x) & (~BITS_BT_ROM_8822B))
#define BIT_GET_BT_ROM_8822B(x)                                                \
	(((x) >> BIT_SHIFT_BT_ROM_8822B) & BIT_MASK_BT_ROM_8822B)
#define BIT_SET_BT_ROM_8822B(x, v)                                             \
	(BIT_CLEAR_BT_ROM_8822B(x) | BIT_BT_ROM_8822B(v))

#define BIT_SHIFT_PCI_DPRAM_8822B 10
#define BIT_MASK_PCI_DPRAM_8822B 0x3
#define BIT_PCI_DPRAM_8822B(x)                                                 \
	(((x) & BIT_MASK_PCI_DPRAM_8822B) << BIT_SHIFT_PCI_DPRAM_8822B)
#define BITS_PCI_DPRAM_8822B                                                   \
	(BIT_MASK_PCI_DPRAM_8822B << BIT_SHIFT_PCI_DPRAM_8822B)
#define BIT_CLEAR_PCI_DPRAM_8822B(x) ((x) & (~BITS_PCI_DPRAM_8822B))
#define BIT_GET_PCI_DPRAM_8822B(x)                                             \
	(((x) >> BIT_SHIFT_PCI_DPRAM_8822B) & BIT_MASK_PCI_DPRAM_8822B)
#define BIT_SET_PCI_DPRAM_8822B(x, v)                                          \
	(BIT_CLEAR_PCI_DPRAM_8822B(x) | BIT_PCI_DPRAM_8822B(v))

#define BIT_SHIFT_PCI_SPRAM_8822B 8
#define BIT_MASK_PCI_SPRAM_8822B 0x3
#define BIT_PCI_SPRAM_8822B(x)                                                 \
	(((x) & BIT_MASK_PCI_SPRAM_8822B) << BIT_SHIFT_PCI_SPRAM_8822B)
#define BITS_PCI_SPRAM_8822B                                                   \
	(BIT_MASK_PCI_SPRAM_8822B << BIT_SHIFT_PCI_SPRAM_8822B)
#define BIT_CLEAR_PCI_SPRAM_8822B(x) ((x) & (~BITS_PCI_SPRAM_8822B))
#define BIT_GET_PCI_SPRAM_8822B(x)                                             \
	(((x) >> BIT_SHIFT_PCI_SPRAM_8822B) & BIT_MASK_PCI_SPRAM_8822B)
#define BIT_SET_PCI_SPRAM_8822B(x, v)                                          \
	(BIT_CLEAR_PCI_SPRAM_8822B(x) | BIT_PCI_SPRAM_8822B(v))

#define BIT_SHIFT_USB_SPRAM_8822B 6
#define BIT_MASK_USB_SPRAM_8822B 0x3
#define BIT_USB_SPRAM_8822B(x)                                                 \
	(((x) & BIT_MASK_USB_SPRAM_8822B) << BIT_SHIFT_USB_SPRAM_8822B)
#define BITS_USB_SPRAM_8822B                                                   \
	(BIT_MASK_USB_SPRAM_8822B << BIT_SHIFT_USB_SPRAM_8822B)
#define BIT_CLEAR_USB_SPRAM_8822B(x) ((x) & (~BITS_USB_SPRAM_8822B))
#define BIT_GET_USB_SPRAM_8822B(x)                                             \
	(((x) >> BIT_SHIFT_USB_SPRAM_8822B) & BIT_MASK_USB_SPRAM_8822B)
#define BIT_SET_USB_SPRAM_8822B(x, v)                                          \
	(BIT_CLEAR_USB_SPRAM_8822B(x) | BIT_USB_SPRAM_8822B(v))

#define BIT_SHIFT_USB_SPRF_8822B 4
#define BIT_MASK_USB_SPRF_8822B 0x3
#define BIT_USB_SPRF_8822B(x)                                                  \
	(((x) & BIT_MASK_USB_SPRF_8822B) << BIT_SHIFT_USB_SPRF_8822B)
#define BITS_USB_SPRF_8822B                                                    \
	(BIT_MASK_USB_SPRF_8822B << BIT_SHIFT_USB_SPRF_8822B)
#define BIT_CLEAR_USB_SPRF_8822B(x) ((x) & (~BITS_USB_SPRF_8822B))
#define BIT_GET_USB_SPRF_8822B(x)                                              \
	(((x) >> BIT_SHIFT_USB_SPRF_8822B) & BIT_MASK_USB_SPRF_8822B)
#define BIT_SET_USB_SPRF_8822B(x, v)                                           \
	(BIT_CLEAR_USB_SPRF_8822B(x) | BIT_USB_SPRF_8822B(v))

#define BIT_SHIFT_MCU_ROM_8822B 0
#define BIT_MASK_MCU_ROM_8822B 0xf
#define BIT_MCU_ROM_8822B(x)                                                   \
	(((x) & BIT_MASK_MCU_ROM_8822B) << BIT_SHIFT_MCU_ROM_8822B)
#define BITS_MCU_ROM_8822B (BIT_MASK_MCU_ROM_8822B << BIT_SHIFT_MCU_ROM_8822B)
#define BIT_CLEAR_MCU_ROM_8822B(x) ((x) & (~BITS_MCU_ROM_8822B))
#define BIT_GET_MCU_ROM_8822B(x)                                               \
	(((x) >> BIT_SHIFT_MCU_ROM_8822B) & BIT_MASK_MCU_ROM_8822B)
#define BIT_SET_MCU_ROM_8822B(x, v)                                            \
	(BIT_CLEAR_MCU_ROM_8822B(x) | BIT_MCU_ROM_8822B(v))

/* 2 REG_AFE_CTRL8_8822B */
#define BIT_SYN_AGPIO_8822B BIT(20)
#define BIT_XTAL_LP_8822B BIT(4)
#define BIT_XTAL_GM_SEP_8822B BIT(3)

#define BIT_SHIFT_XTAL_SEL_TOK_8822B 0
#define BIT_MASK_XTAL_SEL_TOK_8822B 0x7
#define BIT_XTAL_SEL_TOK_8822B(x)                                              \
	(((x) & BIT_MASK_XTAL_SEL_TOK_8822B) << BIT_SHIFT_XTAL_SEL_TOK_8822B)
#define BITS_XTAL_SEL_TOK_8822B                                                \
	(BIT_MASK_XTAL_SEL_TOK_8822B << BIT_SHIFT_XTAL_SEL_TOK_8822B)
#define BIT_CLEAR_XTAL_SEL_TOK_8822B(x) ((x) & (~BITS_XTAL_SEL_TOK_8822B))
#define BIT_GET_XTAL_SEL_TOK_8822B(x)                                          \
	(((x) >> BIT_SHIFT_XTAL_SEL_TOK_8822B) & BIT_MASK_XTAL_SEL_TOK_8822B)
#define BIT_SET_XTAL_SEL_TOK_8822B(x, v)                                       \
	(BIT_CLEAR_XTAL_SEL_TOK_8822B(x) | BIT_XTAL_SEL_TOK_8822B(v))

/* 2 REG_USB_SIE_INTF_8822B */
#define BIT_RD_SEL_8822B BIT(31)
#define BIT_USB_SIE_INTF_WE_V1_8822B BIT(30)
#define BIT_USB_SIE_INTF_BYIOREG_V1_8822B BIT(29)
#define BIT_USB_SIE_SELECT_8822B BIT(28)

#define BIT_SHIFT_USB_SIE_INTF_ADDR_V1_8822B 16
#define BIT_MASK_USB_SIE_INTF_ADDR_V1_8822B 0x1ff
#define BIT_USB_SIE_INTF_ADDR_V1_8822B(x)                                      \
	(((x) & BIT_MASK_USB_SIE_INTF_ADDR_V1_8822B)                           \
	 << BIT_SHIFT_USB_SIE_INTF_ADDR_V1_8822B)
#define BITS_USB_SIE_INTF_ADDR_V1_8822B                                        \
	(BIT_MASK_USB_SIE_INTF_ADDR_V1_8822B                                   \
	 << BIT_SHIFT_USB_SIE_INTF_ADDR_V1_8822B)
#define BIT_CLEAR_USB_SIE_INTF_ADDR_V1_8822B(x)                                \
	((x) & (~BITS_USB_SIE_INTF_ADDR_V1_8822B))
#define BIT_GET_USB_SIE_INTF_ADDR_V1_8822B(x)                                  \
	(((x) >> BIT_SHIFT_USB_SIE_INTF_ADDR_V1_8822B) &                       \
	 BIT_MASK_USB_SIE_INTF_ADDR_V1_8822B)
#define BIT_SET_USB_SIE_INTF_ADDR_V1_8822B(x, v)                               \
	(BIT_CLEAR_USB_SIE_INTF_ADDR_V1_8822B(x) |                             \
	 BIT_USB_SIE_INTF_ADDR_V1_8822B(v))

#define BIT_SHIFT_USB_SIE_INTF_RD_8822B 8
#define BIT_MASK_USB_SIE_INTF_RD_8822B 0xff
#define BIT_USB_SIE_INTF_RD_8822B(x)                                           \
	(((x) & BIT_MASK_USB_SIE_INTF_RD_8822B)                                \
	 << BIT_SHIFT_USB_SIE_INTF_RD_8822B)
#define BITS_USB_SIE_INTF_RD_8822B                                             \
	(BIT_MASK_USB_SIE_INTF_RD_8822B << BIT_SHIFT_USB_SIE_INTF_RD_8822B)
#define BIT_CLEAR_USB_SIE_INTF_RD_8822B(x) ((x) & (~BITS_USB_SIE_INTF_RD_8822B))
#define BIT_GET_USB_SIE_INTF_RD_8822B(x)                                       \
	(((x) >> BIT_SHIFT_USB_SIE_INTF_RD_8822B) &                            \
	 BIT_MASK_USB_SIE_INTF_RD_8822B)
#define BIT_SET_USB_SIE_INTF_RD_8822B(x, v)                                    \
	(BIT_CLEAR_USB_SIE_INTF_RD_8822B(x) | BIT_USB_SIE_INTF_RD_8822B(v))

#define BIT_SHIFT_USB_SIE_INTF_WD_8822B 0
#define BIT_MASK_USB_SIE_INTF_WD_8822B 0xff
#define BIT_USB_SIE_INTF_WD_8822B(x)                                           \
	(((x) & BIT_MASK_USB_SIE_INTF_WD_8822B)                                \
	 << BIT_SHIFT_USB_SIE_INTF_WD_8822B)
#define BITS_USB_SIE_INTF_WD_8822B                                             \
	(BIT_MASK_USB_SIE_INTF_WD_8822B << BIT_SHIFT_USB_SIE_INTF_WD_8822B)
#define BIT_CLEAR_USB_SIE_INTF_WD_8822B(x) ((x) & (~BITS_USB_SIE_INTF_WD_8822B))
#define BIT_GET_USB_SIE_INTF_WD_8822B(x)                                       \
	(((x) >> BIT_SHIFT_USB_SIE_INTF_WD_8822B) &                            \
	 BIT_MASK_USB_SIE_INTF_WD_8822B)
#define BIT_SET_USB_SIE_INTF_WD_8822B(x, v)                                    \
	(BIT_CLEAR_USB_SIE_INTF_WD_8822B(x) | BIT_USB_SIE_INTF_WD_8822B(v))

/* 2 REG_PCIE_MIO_INTF_8822B */
#define BIT_PCIE_MIO_BYIOREG_8822B BIT(13)
#define BIT_PCIE_MIO_RE_8822B BIT(12)

#define BIT_SHIFT_PCIE_MIO_WE_8822B 8
#define BIT_MASK_PCIE_MIO_WE_8822B 0xf
#define BIT_PCIE_MIO_WE_8822B(x)                                               \
	(((x) & BIT_MASK_PCIE_MIO_WE_8822B) << BIT_SHIFT_PCIE_MIO_WE_8822B)
#define BITS_PCIE_MIO_WE_8822B                                                 \
	(BIT_MASK_PCIE_MIO_WE_8822B << BIT_SHIFT_PCIE_MIO_WE_8822B)
#define BIT_CLEAR_PCIE_MIO_WE_8822B(x) ((x) & (~BITS_PCIE_MIO_WE_8822B))
#define BIT_GET_PCIE_MIO_WE_8822B(x)                                           \
	(((x) >> BIT_SHIFT_PCIE_MIO_WE_8822B) & BIT_MASK_PCIE_MIO_WE_8822B)
#define BIT_SET_PCIE_MIO_WE_8822B(x, v)                                        \
	(BIT_CLEAR_PCIE_MIO_WE_8822B(x) | BIT_PCIE_MIO_WE_8822B(v))

#define BIT_SHIFT_PCIE_MIO_ADDR_8822B 0
#define BIT_MASK_PCIE_MIO_ADDR_8822B 0xff
#define BIT_PCIE_MIO_ADDR_8822B(x)                                             \
	(((x) & BIT_MASK_PCIE_MIO_ADDR_8822B) << BIT_SHIFT_PCIE_MIO_ADDR_8822B)
#define BITS_PCIE_MIO_ADDR_8822B                                               \
	(BIT_MASK_PCIE_MIO_ADDR_8822B << BIT_SHIFT_PCIE_MIO_ADDR_8822B)
#define BIT_CLEAR_PCIE_MIO_ADDR_8822B(x) ((x) & (~BITS_PCIE_MIO_ADDR_8822B))
#define BIT_GET_PCIE_MIO_ADDR_8822B(x)                                         \
	(((x) >> BIT_SHIFT_PCIE_MIO_ADDR_8822B) & BIT_MASK_PCIE_MIO_ADDR_8822B)
#define BIT_SET_PCIE_MIO_ADDR_8822B(x, v)                                      \
	(BIT_CLEAR_PCIE_MIO_ADDR_8822B(x) | BIT_PCIE_MIO_ADDR_8822B(v))

/* 2 REG_PCIE_MIO_INTD_8822B */

#define BIT_SHIFT_PCIE_MIO_DATA_8822B 0
#define BIT_MASK_PCIE_MIO_DATA_8822B 0xffffffffL
#define BIT_PCIE_MIO_DATA_8822B(x)                                             \
	(((x) & BIT_MASK_PCIE_MIO_DATA_8822B) << BIT_SHIFT_PCIE_MIO_DATA_8822B)
#define BITS_PCIE_MIO_DATA_8822B                                               \
	(BIT_MASK_PCIE_MIO_DATA_8822B << BIT_SHIFT_PCIE_MIO_DATA_8822B)
#define BIT_CLEAR_PCIE_MIO_DATA_8822B(x) ((x) & (~BITS_PCIE_MIO_DATA_8822B))
#define BIT_GET_PCIE_MIO_DATA_8822B(x)                                         \
	(((x) >> BIT_SHIFT_PCIE_MIO_DATA_8822B) & BIT_MASK_PCIE_MIO_DATA_8822B)
#define BIT_SET_PCIE_MIO_DATA_8822B(x, v)                                      \
	(BIT_CLEAR_PCIE_MIO_DATA_8822B(x) | BIT_PCIE_MIO_DATA_8822B(v))

/* 2 REG_WLRF1_8822B */

#define BIT_SHIFT_WLRF1_CTRL_8822B 24
#define BIT_MASK_WLRF1_CTRL_8822B 0xff
#define BIT_WLRF1_CTRL_8822B(x)                                                \
	(((x) & BIT_MASK_WLRF1_CTRL_8822B) << BIT_SHIFT_WLRF1_CTRL_8822B)
#define BITS_WLRF1_CTRL_8822B                                                  \
	(BIT_MASK_WLRF1_CTRL_8822B << BIT_SHIFT_WLRF1_CTRL_8822B)
#define BIT_CLEAR_WLRF1_CTRL_8822B(x) ((x) & (~BITS_WLRF1_CTRL_8822B))
#define BIT_GET_WLRF1_CTRL_8822B(x)                                            \
	(((x) >> BIT_SHIFT_WLRF1_CTRL_8822B) & BIT_MASK_WLRF1_CTRL_8822B)
#define BIT_SET_WLRF1_CTRL_8822B(x, v)                                         \
	(BIT_CLEAR_WLRF1_CTRL_8822B(x) | BIT_WLRF1_CTRL_8822B(v))

/* 2 REG_SYS_CFG1_8822B */

#define BIT_SHIFT_TRP_ICFG_8822B 28
#define BIT_MASK_TRP_ICFG_8822B 0xf
#define BIT_TRP_ICFG_8822B(x)                                                  \
	(((x) & BIT_MASK_TRP_ICFG_8822B) << BIT_SHIFT_TRP_ICFG_8822B)
#define BITS_TRP_ICFG_8822B                                                    \
	(BIT_MASK_TRP_ICFG_8822B << BIT_SHIFT_TRP_ICFG_8822B)
#define BIT_CLEAR_TRP_ICFG_8822B(x) ((x) & (~BITS_TRP_ICFG_8822B))
#define BIT_GET_TRP_ICFG_8822B(x)                                              \
	(((x) >> BIT_SHIFT_TRP_ICFG_8822B) & BIT_MASK_TRP_ICFG_8822B)
#define BIT_SET_TRP_ICFG_8822B(x, v)                                           \
	(BIT_CLEAR_TRP_ICFG_8822B(x) | BIT_TRP_ICFG_8822B(v))

#define BIT_RF_TYPE_ID_8822B BIT(27)
#define BIT_BD_HCI_SEL_8822B BIT(26)
#define BIT_BD_PKG_SEL_8822B BIT(25)
#define BIT_SPSLDO_SEL_8822B BIT(24)
#define BIT_RTL_ID_8822B BIT(23)
#define BIT_PAD_HWPD_IDN_8822B BIT(22)
#define BIT_TESTMODE_8822B BIT(20)

#define BIT_SHIFT_VENDOR_ID_8822B 16
#define BIT_MASK_VENDOR_ID_8822B 0xf
#define BIT_VENDOR_ID_8822B(x)                                                 \
	(((x) & BIT_MASK_VENDOR_ID_8822B) << BIT_SHIFT_VENDOR_ID_8822B)
#define BITS_VENDOR_ID_8822B                                                   \
	(BIT_MASK_VENDOR_ID_8822B << BIT_SHIFT_VENDOR_ID_8822B)
#define BIT_CLEAR_VENDOR_ID_8822B(x) ((x) & (~BITS_VENDOR_ID_8822B))
#define BIT_GET_VENDOR_ID_8822B(x)                                             \
	(((x) >> BIT_SHIFT_VENDOR_ID_8822B) & BIT_MASK_VENDOR_ID_8822B)
#define BIT_SET_VENDOR_ID_8822B(x, v)                                          \
	(BIT_CLEAR_VENDOR_ID_8822B(x) | BIT_VENDOR_ID_8822B(v))

#define BIT_SHIFT_CHIP_VER_8822B 12
#define BIT_MASK_CHIP_VER_8822B 0xf
#define BIT_CHIP_VER_8822B(x)                                                  \
	(((x) & BIT_MASK_CHIP_VER_8822B) << BIT_SHIFT_CHIP_VER_8822B)
#define BITS_CHIP_VER_8822B                                                    \
	(BIT_MASK_CHIP_VER_8822B << BIT_SHIFT_CHIP_VER_8822B)
#define BIT_CLEAR_CHIP_VER_8822B(x) ((x) & (~BITS_CHIP_VER_8822B))
#define BIT_GET_CHIP_VER_8822B(x)                                              \
	(((x) >> BIT_SHIFT_CHIP_VER_8822B) & BIT_MASK_CHIP_VER_8822B)
#define BIT_SET_CHIP_VER_8822B(x, v)                                           \
	(BIT_CLEAR_CHIP_VER_8822B(x) | BIT_CHIP_VER_8822B(v))

#define BIT_BD_MAC3_8822B BIT(11)
#define BIT_BD_MAC1_8822B BIT(10)
#define BIT_BD_MAC2_8822B BIT(9)
#define BIT_SIC_IDLE_8822B BIT(8)
#define BIT_SW_OFFLOAD_EN_8822B BIT(7)
#define BIT_OCP_SHUTDN_8822B BIT(6)
#define BIT_V15_VLD_8822B BIT(5)
#define BIT_PCIRSTB_8822B BIT(4)
#define BIT_PCLK_VLD_8822B BIT(3)
#define BIT_UCLK_VLD_8822B BIT(2)
#define BIT_ACLK_VLD_8822B BIT(1)
#define BIT_XCLK_VLD_8822B BIT(0)

/* 2 REG_SYS_STATUS1_8822B */

#define BIT_SHIFT_RF_RL_ID_8822B 28
#define BIT_MASK_RF_RL_ID_8822B 0xf
#define BIT_RF_RL_ID_8822B(x)                                                  \
	(((x) & BIT_MASK_RF_RL_ID_8822B) << BIT_SHIFT_RF_RL_ID_8822B)
#define BITS_RF_RL_ID_8822B                                                    \
	(BIT_MASK_RF_RL_ID_8822B << BIT_SHIFT_RF_RL_ID_8822B)
#define BIT_CLEAR_RF_RL_ID_8822B(x) ((x) & (~BITS_RF_RL_ID_8822B))
#define BIT_GET_RF_RL_ID_8822B(x)                                              \
	(((x) >> BIT_SHIFT_RF_RL_ID_8822B) & BIT_MASK_RF_RL_ID_8822B)
#define BIT_SET_RF_RL_ID_8822B(x, v)                                           \
	(BIT_CLEAR_RF_RL_ID_8822B(x) | BIT_RF_RL_ID_8822B(v))

#define BIT_HPHY_ICFG_8822B BIT(19)

#define BIT_SHIFT_SEL_0XC0_8822B 16
#define BIT_MASK_SEL_0XC0_8822B 0x3
#define BIT_SEL_0XC0_8822B(x)                                                  \
	(((x) & BIT_MASK_SEL_0XC0_8822B) << BIT_SHIFT_SEL_0XC0_8822B)
#define BITS_SEL_0XC0_8822B                                                    \
	(BIT_MASK_SEL_0XC0_8822B << BIT_SHIFT_SEL_0XC0_8822B)
#define BIT_CLEAR_SEL_0XC0_8822B(x) ((x) & (~BITS_SEL_0XC0_8822B))
#define BIT_GET_SEL_0XC0_8822B(x)                                              \
	(((x) >> BIT_SHIFT_SEL_0XC0_8822B) & BIT_MASK_SEL_0XC0_8822B)
#define BIT_SET_SEL_0XC0_8822B(x, v)                                           \
	(BIT_CLEAR_SEL_0XC0_8822B(x) | BIT_SEL_0XC0_8822B(v))

#define BIT_SHIFT_HCI_SEL_V3_8822B 12
#define BIT_MASK_HCI_SEL_V3_8822B 0x7
#define BIT_HCI_SEL_V3_8822B(x)                                                \
	(((x) & BIT_MASK_HCI_SEL_V3_8822B) << BIT_SHIFT_HCI_SEL_V3_8822B)
#define BITS_HCI_SEL_V3_8822B                                                  \
	(BIT_MASK_HCI_SEL_V3_8822B << BIT_SHIFT_HCI_SEL_V3_8822B)
#define BIT_CLEAR_HCI_SEL_V3_8822B(x) ((x) & (~BITS_HCI_SEL_V3_8822B))
#define BIT_GET_HCI_SEL_V3_8822B(x)                                            \
	(((x) >> BIT_SHIFT_HCI_SEL_V3_8822B) & BIT_MASK_HCI_SEL_V3_8822B)
#define BIT_SET_HCI_SEL_V3_8822B(x, v)                                         \
	(BIT_CLEAR_HCI_SEL_V3_8822B(x) | BIT_HCI_SEL_V3_8822B(v))

#define BIT_USB_OPERATION_MODE_8822B BIT(10)
#define BIT_BT_PDN_8822B BIT(9)
#define BIT_AUTO_WLPON_8822B BIT(8)
#define BIT_WL_MODE_8822B BIT(7)
#define BIT_PKG_SEL_HCI_8822B BIT(6)

#define BIT_SHIFT_PAD_HCI_SEL_V1_8822B 3
#define BIT_MASK_PAD_HCI_SEL_V1_8822B 0x7
#define BIT_PAD_HCI_SEL_V1_8822B(x)                                            \
	(((x) & BIT_MASK_PAD_HCI_SEL_V1_8822B)                                 \
	 << BIT_SHIFT_PAD_HCI_SEL_V1_8822B)
#define BITS_PAD_HCI_SEL_V1_8822B                                              \
	(BIT_MASK_PAD_HCI_SEL_V1_8822B << BIT_SHIFT_PAD_HCI_SEL_V1_8822B)
#define BIT_CLEAR_PAD_HCI_SEL_V1_8822B(x) ((x) & (~BITS_PAD_HCI_SEL_V1_8822B))
#define BIT_GET_PAD_HCI_SEL_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_PAD_HCI_SEL_V1_8822B) &                             \
	 BIT_MASK_PAD_HCI_SEL_V1_8822B)
#define BIT_SET_PAD_HCI_SEL_V1_8822B(x, v)                                     \
	(BIT_CLEAR_PAD_HCI_SEL_V1_8822B(x) | BIT_PAD_HCI_SEL_V1_8822B(v))

#define BIT_SHIFT_EFS_HCI_SEL_V1_8822B 0
#define BIT_MASK_EFS_HCI_SEL_V1_8822B 0x7
#define BIT_EFS_HCI_SEL_V1_8822B(x)                                            \
	(((x) & BIT_MASK_EFS_HCI_SEL_V1_8822B)                                 \
	 << BIT_SHIFT_EFS_HCI_SEL_V1_8822B)
#define BITS_EFS_HCI_SEL_V1_8822B                                              \
	(BIT_MASK_EFS_HCI_SEL_V1_8822B << BIT_SHIFT_EFS_HCI_SEL_V1_8822B)
#define BIT_CLEAR_EFS_HCI_SEL_V1_8822B(x) ((x) & (~BITS_EFS_HCI_SEL_V1_8822B))
#define BIT_GET_EFS_HCI_SEL_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_EFS_HCI_SEL_V1_8822B) &                             \
	 BIT_MASK_EFS_HCI_SEL_V1_8822B)
#define BIT_SET_EFS_HCI_SEL_V1_8822B(x, v)                                     \
	(BIT_CLEAR_EFS_HCI_SEL_V1_8822B(x) | BIT_EFS_HCI_SEL_V1_8822B(v))

/* 2 REG_SYS_STATUS2_8822B */
#define BIT_SIO_ALDN_8822B BIT(19)
#define BIT_USB_ALDN_8822B BIT(18)
#define BIT_PCI_ALDN_8822B BIT(17)
#define BIT_SYS_ALDN_8822B BIT(16)

#define BIT_SHIFT_EPVID1_8822B 8
#define BIT_MASK_EPVID1_8822B 0xff
#define BIT_EPVID1_8822B(x)                                                    \
	(((x) & BIT_MASK_EPVID1_8822B) << BIT_SHIFT_EPVID1_8822B)
#define BITS_EPVID1_8822B (BIT_MASK_EPVID1_8822B << BIT_SHIFT_EPVID1_8822B)
#define BIT_CLEAR_EPVID1_8822B(x) ((x) & (~BITS_EPVID1_8822B))
#define BIT_GET_EPVID1_8822B(x)                                                \
	(((x) >> BIT_SHIFT_EPVID1_8822B) & BIT_MASK_EPVID1_8822B)
#define BIT_SET_EPVID1_8822B(x, v)                                             \
	(BIT_CLEAR_EPVID1_8822B(x) | BIT_EPVID1_8822B(v))

#define BIT_SHIFT_EPVID0_8822B 0
#define BIT_MASK_EPVID0_8822B 0xff
#define BIT_EPVID0_8822B(x)                                                    \
	(((x) & BIT_MASK_EPVID0_8822B) << BIT_SHIFT_EPVID0_8822B)
#define BITS_EPVID0_8822B (BIT_MASK_EPVID0_8822B << BIT_SHIFT_EPVID0_8822B)
#define BIT_CLEAR_EPVID0_8822B(x) ((x) & (~BITS_EPVID0_8822B))
#define BIT_GET_EPVID0_8822B(x)                                                \
	(((x) >> BIT_SHIFT_EPVID0_8822B) & BIT_MASK_EPVID0_8822B)
#define BIT_SET_EPVID0_8822B(x, v)                                             \
	(BIT_CLEAR_EPVID0_8822B(x) | BIT_EPVID0_8822B(v))

/* 2 REG_SYS_CFG2_8822B */
#define BIT_HCI_SEL_EMBEDDED_8822B BIT(8)

#define BIT_SHIFT_HW_ID_8822B 0
#define BIT_MASK_HW_ID_8822B 0xff
#define BIT_HW_ID_8822B(x)                                                     \
	(((x) & BIT_MASK_HW_ID_8822B) << BIT_SHIFT_HW_ID_8822B)
#define BITS_HW_ID_8822B (BIT_MASK_HW_ID_8822B << BIT_SHIFT_HW_ID_8822B)
#define BIT_CLEAR_HW_ID_8822B(x) ((x) & (~BITS_HW_ID_8822B))
#define BIT_GET_HW_ID_8822B(x)                                                 \
	(((x) >> BIT_SHIFT_HW_ID_8822B) & BIT_MASK_HW_ID_8822B)
#define BIT_SET_HW_ID_8822B(x, v)                                              \
	(BIT_CLEAR_HW_ID_8822B(x) | BIT_HW_ID_8822B(v))

/* 2 REG_SYS_CFG3_8822B */
#define BIT_PWC_MA33V_8822B BIT(15)
#define BIT_PWC_MA12V_8822B BIT(14)
#define BIT_PWC_MD12V_8822B BIT(13)
#define BIT_PWC_PD12V_8822B BIT(12)
#define BIT_PWC_UD12V_8822B BIT(11)
#define BIT_ISO_MA2MD_8822B BIT(1)
#define BIT_ISO_MD2PP_8822B BIT(0)

/* 2 REG_SYS_CFG4_8822B */

/* 2 REG_SYS_CFG5_8822B */
#define BIT_LPS_STATUS_8822B BIT(3)
#define BIT_HCI_TXDMA_BUSY_8822B BIT(2)
#define BIT_HCI_TXDMA_ALLOW_8822B BIT(1)
#define BIT_FW_CTRL_HCI_TXDMA_EN_8822B BIT(0)

/* 2 REG_CPU_DMEM_CON_8822B */
#define BIT_WDT_OPT_IOWRAPPER_8822B BIT(19)
#define BIT_ANA_PORT_IDLE_8822B BIT(18)
#define BIT_MAC_PORT_IDLE_8822B BIT(17)
#define BIT_WL_PLATFORM_RST_8822B BIT(16)
#define BIT_WL_SECURITY_CLK_8822B BIT(15)

#define BIT_SHIFT_CPU_DMEM_CON_8822B 0
#define BIT_MASK_CPU_DMEM_CON_8822B 0xff
#define BIT_CPU_DMEM_CON_8822B(x)                                              \
	(((x) & BIT_MASK_CPU_DMEM_CON_8822B) << BIT_SHIFT_CPU_DMEM_CON_8822B)
#define BITS_CPU_DMEM_CON_8822B                                                \
	(BIT_MASK_CPU_DMEM_CON_8822B << BIT_SHIFT_CPU_DMEM_CON_8822B)
#define BIT_CLEAR_CPU_DMEM_CON_8822B(x) ((x) & (~BITS_CPU_DMEM_CON_8822B))
#define BIT_GET_CPU_DMEM_CON_8822B(x)                                          \
	(((x) >> BIT_SHIFT_CPU_DMEM_CON_8822B) & BIT_MASK_CPU_DMEM_CON_8822B)
#define BIT_SET_CPU_DMEM_CON_8822B(x, v)                                       \
	(BIT_CLEAR_CPU_DMEM_CON_8822B(x) | BIT_CPU_DMEM_CON_8822B(v))

/* 2 REG_BOOT_REASON_8822B */

#define BIT_SHIFT_BOOT_REASON_V1_8822B 0
#define BIT_MASK_BOOT_REASON_V1_8822B 0x7
#define BIT_BOOT_REASON_V1_8822B(x)                                            \
	(((x) & BIT_MASK_BOOT_REASON_V1_8822B)                                 \
	 << BIT_SHIFT_BOOT_REASON_V1_8822B)
#define BITS_BOOT_REASON_V1_8822B                                              \
	(BIT_MASK_BOOT_REASON_V1_8822B << BIT_SHIFT_BOOT_REASON_V1_8822B)
#define BIT_CLEAR_BOOT_REASON_V1_8822B(x) ((x) & (~BITS_BOOT_REASON_V1_8822B))
#define BIT_GET_BOOT_REASON_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_BOOT_REASON_V1_8822B) &                             \
	 BIT_MASK_BOOT_REASON_V1_8822B)
#define BIT_SET_BOOT_REASON_V1_8822B(x, v)                                     \
	(BIT_CLEAR_BOOT_REASON_V1_8822B(x) | BIT_BOOT_REASON_V1_8822B(v))

/* 2 REG_NFCPAD_CTRL_8822B */
#define BIT_PAD_SHUTDW_8822B BIT(18)
#define BIT_SYSON_NFC_PAD_8822B BIT(17)
#define BIT_NFC_INT_PAD_CTRL_8822B BIT(16)
#define BIT_NFC_RFDIS_PAD_CTRL_8822B BIT(15)
#define BIT_NFC_CLK_PAD_CTRL_8822B BIT(14)
#define BIT_NFC_DATA_PAD_CTRL_8822B BIT(13)
#define BIT_NFC_PAD_PULL_CTRL_8822B BIT(12)

#define BIT_SHIFT_NFCPAD_IO_SEL_8822B 8
#define BIT_MASK_NFCPAD_IO_SEL_8822B 0xf
#define BIT_NFCPAD_IO_SEL_8822B(x)                                             \
	(((x) & BIT_MASK_NFCPAD_IO_SEL_8822B) << BIT_SHIFT_NFCPAD_IO_SEL_8822B)
#define BITS_NFCPAD_IO_SEL_8822B                                               \
	(BIT_MASK_NFCPAD_IO_SEL_8822B << BIT_SHIFT_NFCPAD_IO_SEL_8822B)
#define BIT_CLEAR_NFCPAD_IO_SEL_8822B(x) ((x) & (~BITS_NFCPAD_IO_SEL_8822B))
#define BIT_GET_NFCPAD_IO_SEL_8822B(x)                                         \
	(((x) >> BIT_SHIFT_NFCPAD_IO_SEL_8822B) & BIT_MASK_NFCPAD_IO_SEL_8822B)
#define BIT_SET_NFCPAD_IO_SEL_8822B(x, v)                                      \
	(BIT_CLEAR_NFCPAD_IO_SEL_8822B(x) | BIT_NFCPAD_IO_SEL_8822B(v))

#define BIT_SHIFT_NFCPAD_OUT_8822B 4
#define BIT_MASK_NFCPAD_OUT_8822B 0xf
#define BIT_NFCPAD_OUT_8822B(x)                                                \
	(((x) & BIT_MASK_NFCPAD_OUT_8822B) << BIT_SHIFT_NFCPAD_OUT_8822B)
#define BITS_NFCPAD_OUT_8822B                                                  \
	(BIT_MASK_NFCPAD_OUT_8822B << BIT_SHIFT_NFCPAD_OUT_8822B)
#define BIT_CLEAR_NFCPAD_OUT_8822B(x) ((x) & (~BITS_NFCPAD_OUT_8822B))
#define BIT_GET_NFCPAD_OUT_8822B(x)                                            \
	(((x) >> BIT_SHIFT_NFCPAD_OUT_8822B) & BIT_MASK_NFCPAD_OUT_8822B)
#define BIT_SET_NFCPAD_OUT_8822B(x, v)                                         \
	(BIT_CLEAR_NFCPAD_OUT_8822B(x) | BIT_NFCPAD_OUT_8822B(v))

#define BIT_SHIFT_NFCPAD_IN_8822B 0
#define BIT_MASK_NFCPAD_IN_8822B 0xf
#define BIT_NFCPAD_IN_8822B(x)                                                 \
	(((x) & BIT_MASK_NFCPAD_IN_8822B) << BIT_SHIFT_NFCPAD_IN_8822B)
#define BITS_NFCPAD_IN_8822B                                                   \
	(BIT_MASK_NFCPAD_IN_8822B << BIT_SHIFT_NFCPAD_IN_8822B)
#define BIT_CLEAR_NFCPAD_IN_8822B(x) ((x) & (~BITS_NFCPAD_IN_8822B))
#define BIT_GET_NFCPAD_IN_8822B(x)                                             \
	(((x) >> BIT_SHIFT_NFCPAD_IN_8822B) & BIT_MASK_NFCPAD_IN_8822B)
#define BIT_SET_NFCPAD_IN_8822B(x, v)                                          \
	(BIT_CLEAR_NFCPAD_IN_8822B(x) | BIT_NFCPAD_IN_8822B(v))

/* 2 REG_HIMR2_8822B */
#define BIT_BCNDMAINT_P4_MSK_8822B BIT(31)
#define BIT_BCNDMAINT_P3_MSK_8822B BIT(30)
#define BIT_BCNDMAINT_P2_MSK_8822B BIT(29)
#define BIT_BCNDMAINT_P1_MSK_8822B BIT(28)
#define BIT_ATIMEND7_MSK_8822B BIT(22)
#define BIT_ATIMEND6_MSK_8822B BIT(21)
#define BIT_ATIMEND5_MSK_8822B BIT(20)
#define BIT_ATIMEND4_MSK_8822B BIT(19)
#define BIT_ATIMEND3_MSK_8822B BIT(18)
#define BIT_ATIMEND2_MSK_8822B BIT(17)
#define BIT_ATIMEND1_MSK_8822B BIT(16)
#define BIT_TXBCN7OK_MSK_8822B BIT(14)
#define BIT_TXBCN6OK_MSK_8822B BIT(13)
#define BIT_TXBCN5OK_MSK_8822B BIT(12)
#define BIT_TXBCN4OK_MSK_8822B BIT(11)
#define BIT_TXBCN3OK_MSK_8822B BIT(10)
#define BIT_TXBCN2OK_MSK_8822B BIT(9)
#define BIT_TXBCN1OK_MSK_V1_8822B BIT(8)
#define BIT_TXBCN7ERR_MSK_8822B BIT(6)
#define BIT_TXBCN6ERR_MSK_8822B BIT(5)
#define BIT_TXBCN5ERR_MSK_8822B BIT(4)
#define BIT_TXBCN4ERR_MSK_8822B BIT(3)
#define BIT_TXBCN3ERR_MSK_8822B BIT(2)
#define BIT_TXBCN2ERR_MSK_8822B BIT(1)
#define BIT_TXBCN1ERR_MSK_V1_8822B BIT(0)

/* 2 REG_HISR2_8822B */
#define BIT_BCNDMAINT_P4_8822B BIT(31)
#define BIT_BCNDMAINT_P3_8822B BIT(30)
#define BIT_BCNDMAINT_P2_8822B BIT(29)
#define BIT_BCNDMAINT_P1_8822B BIT(28)
#define BIT_ATIMEND7_8822B BIT(22)
#define BIT_ATIMEND6_8822B BIT(21)
#define BIT_ATIMEND5_8822B BIT(20)
#define BIT_ATIMEND4_8822B BIT(19)
#define BIT_ATIMEND3_8822B BIT(18)
#define BIT_ATIMEND2_8822B BIT(17)
#define BIT_ATIMEND1_8822B BIT(16)
#define BIT_TXBCN7OK_8822B BIT(14)
#define BIT_TXBCN6OK_8822B BIT(13)
#define BIT_TXBCN5OK_8822B BIT(12)
#define BIT_TXBCN4OK_8822B BIT(11)
#define BIT_TXBCN3OK_8822B BIT(10)
#define BIT_TXBCN2OK_8822B BIT(9)
#define BIT_TXBCN1OK_8822B BIT(8)
#define BIT_TXBCN7ERR_8822B BIT(6)
#define BIT_TXBCN6ERR_8822B BIT(5)
#define BIT_TXBCN5ERR_8822B BIT(4)
#define BIT_TXBCN4ERR_8822B BIT(3)
#define BIT_TXBCN3ERR_8822B BIT(2)
#define BIT_TXBCN2ERR_8822B BIT(1)
#define BIT_TXBCN1ERR_8822B BIT(0)

/* 2 REG_HIMR3_8822B */
#define BIT_WDT_PLATFORM_INT_MSK_8822B BIT(18)
#define BIT_WDT_CPU_INT_MSK_8822B BIT(17)
#define BIT_SETH2CDOK_MASK_8822B BIT(16)
#define BIT_H2C_CMD_FULL_MASK_8822B BIT(15)
#define BIT_PWR_INT_127_MASK_8822B BIT(14)
#define BIT_TXSHORTCUT_TXDESUPDATEOK_MASK_8822B BIT(13)
#define BIT_TXSHORTCUT_BKUPDATEOK_MASK_8822B BIT(12)
#define BIT_TXSHORTCUT_BEUPDATEOK_MASK_8822B BIT(11)
#define BIT_TXSHORTCUT_VIUPDATEOK_MAS_8822B BIT(10)
#define BIT_TXSHORTCUT_VOUPDATEOK_MASK_8822B BIT(9)
#define BIT_PWR_INT_127_MASK_V1_8822B BIT(8)
#define BIT_PWR_INT_126TO96_MASK_8822B BIT(7)
#define BIT_PWR_INT_95TO64_MASK_8822B BIT(6)
#define BIT_PWR_INT_63TO32_MASK_8822B BIT(5)
#define BIT_PWR_INT_31TO0_MASK_8822B BIT(4)
#define BIT_DDMA0_LP_INT_MSK_8822B BIT(1)
#define BIT_DDMA0_HP_INT_MSK_8822B BIT(0)

/* 2 REG_HISR3_8822B */
#define BIT_WDT_PLATFORM_INT_8822B BIT(18)
#define BIT_WDT_CPU_INT_8822B BIT(17)
#define BIT_SETH2CDOK_8822B BIT(16)
#define BIT_H2C_CMD_FULL_8822B BIT(15)
#define BIT_PWR_INT_127_8822B BIT(14)
#define BIT_TXSHORTCUT_TXDESUPDATEOK_8822B BIT(13)
#define BIT_TXSHORTCUT_BKUPDATEOK_8822B BIT(12)
#define BIT_TXSHORTCUT_BEUPDATEOK_8822B BIT(11)
#define BIT_TXSHORTCUT_VIUPDATEOK_8822B BIT(10)
#define BIT_TXSHORTCUT_VOUPDATEOK_8822B BIT(9)
#define BIT_PWR_INT_127_V1_8822B BIT(8)
#define BIT_PWR_INT_126TO96_8822B BIT(7)
#define BIT_PWR_INT_95TO64_8822B BIT(6)
#define BIT_PWR_INT_63TO32_8822B BIT(5)
#define BIT_PWR_INT_31TO0_8822B BIT(4)
#define BIT_DDMA0_LP_INT_8822B BIT(1)
#define BIT_DDMA0_HP_INT_8822B BIT(0)

/* 2 REG_SW_MDIO_8822B */
#define BIT_DIS_TIMEOUT_IO_8822B BIT(24)

/* 2 REG_SW_FLUSH_8822B */
#define BIT_FLUSH_HOLDN_EN_8822B BIT(25)
#define BIT_FLUSH_WR_EN_8822B BIT(24)
#define BIT_SW_FLASH_CONTROL_8822B BIT(23)
#define BIT_SW_FLASH_WEN_E_8822B BIT(19)
#define BIT_SW_FLASH_HOLDN_E_8822B BIT(18)
#define BIT_SW_FLASH_SO_E_8822B BIT(17)
#define BIT_SW_FLASH_SI_E_8822B BIT(16)
#define BIT_SW_FLASH_SK_O_8822B BIT(13)
#define BIT_SW_FLASH_CEN_O_8822B BIT(12)
#define BIT_SW_FLASH_WEN_O_8822B BIT(11)
#define BIT_SW_FLASH_HOLDN_O_8822B BIT(10)
#define BIT_SW_FLASH_SO_O_8822B BIT(9)
#define BIT_SW_FLASH_SI_O_8822B BIT(8)
#define BIT_SW_FLASH_WEN_I_8822B BIT(3)
#define BIT_SW_FLASH_HOLDN_I_8822B BIT(2)
#define BIT_SW_FLASH_SO_I_8822B BIT(1)
#define BIT_SW_FLASH_SI_I_8822B BIT(0)

/* 2 REG_H2C_PKT_READADDR_8822B */

#define BIT_SHIFT_H2C_PKT_READADDR_8822B 0
#define BIT_MASK_H2C_PKT_READADDR_8822B 0x3ffff
#define BIT_H2C_PKT_READADDR_8822B(x)                                          \
	(((x) & BIT_MASK_H2C_PKT_READADDR_8822B)                               \
	 << BIT_SHIFT_H2C_PKT_READADDR_8822B)
#define BITS_H2C_PKT_READADDR_8822B                                            \
	(BIT_MASK_H2C_PKT_READADDR_8822B << BIT_SHIFT_H2C_PKT_READADDR_8822B)
#define BIT_CLEAR_H2C_PKT_READADDR_8822B(x)                                    \
	((x) & (~BITS_H2C_PKT_READADDR_8822B))
#define BIT_GET_H2C_PKT_READADDR_8822B(x)                                      \
	(((x) >> BIT_SHIFT_H2C_PKT_READADDR_8822B) &                           \
	 BIT_MASK_H2C_PKT_READADDR_8822B)
#define BIT_SET_H2C_PKT_READADDR_8822B(x, v)                                   \
	(BIT_CLEAR_H2C_PKT_READADDR_8822B(x) | BIT_H2C_PKT_READADDR_8822B(v))

/* 2 REG_H2C_PKT_WRITEADDR_8822B */

#define BIT_SHIFT_H2C_PKT_WRITEADDR_8822B 0
#define BIT_MASK_H2C_PKT_WRITEADDR_8822B 0x3ffff
#define BIT_H2C_PKT_WRITEADDR_8822B(x)                                         \
	(((x) & BIT_MASK_H2C_PKT_WRITEADDR_8822B)                              \
	 << BIT_SHIFT_H2C_PKT_WRITEADDR_8822B)
#define BITS_H2C_PKT_WRITEADDR_8822B                                           \
	(BIT_MASK_H2C_PKT_WRITEADDR_8822B << BIT_SHIFT_H2C_PKT_WRITEADDR_8822B)
#define BIT_CLEAR_H2C_PKT_WRITEADDR_8822B(x)                                   \
	((x) & (~BITS_H2C_PKT_WRITEADDR_8822B))
#define BIT_GET_H2C_PKT_WRITEADDR_8822B(x)                                     \
	(((x) >> BIT_SHIFT_H2C_PKT_WRITEADDR_8822B) &                          \
	 BIT_MASK_H2C_PKT_WRITEADDR_8822B)
#define BIT_SET_H2C_PKT_WRITEADDR_8822B(x, v)                                  \
	(BIT_CLEAR_H2C_PKT_WRITEADDR_8822B(x) | BIT_H2C_PKT_WRITEADDR_8822B(v))

/* 2 REG_MEM_PWR_CRTL_8822B */
#define BIT_MEM_BB_SD_8822B BIT(17)
#define BIT_MEM_BB_DS_8822B BIT(16)
#define BIT_MEM_BT_DS_8822B BIT(10)
#define BIT_MEM_SDIO_LS_8822B BIT(9)
#define BIT_MEM_SDIO_DS_8822B BIT(8)
#define BIT_MEM_USB_LS_8822B BIT(7)
#define BIT_MEM_USB_DS_8822B BIT(6)
#define BIT_MEM_PCI_LS_8822B BIT(5)
#define BIT_MEM_PCI_DS_8822B BIT(4)
#define BIT_MEM_WLMAC_LS_8822B BIT(3)
#define BIT_MEM_WLMAC_DS_8822B BIT(2)
#define BIT_MEM_WLMCU_LS_8822B BIT(1)
#define BIT_MEM_WLMCU_DS_8822B BIT(0)

/* 2 REG_FW_DBG0_8822B */

#define BIT_SHIFT_FW_DBG0_8822B 0
#define BIT_MASK_FW_DBG0_8822B 0xffffffffL
#define BIT_FW_DBG0_8822B(x)                                                   \
	(((x) & BIT_MASK_FW_DBG0_8822B) << BIT_SHIFT_FW_DBG0_8822B)
#define BITS_FW_DBG0_8822B (BIT_MASK_FW_DBG0_8822B << BIT_SHIFT_FW_DBG0_8822B)
#define BIT_CLEAR_FW_DBG0_8822B(x) ((x) & (~BITS_FW_DBG0_8822B))
#define BIT_GET_FW_DBG0_8822B(x)                                               \
	(((x) >> BIT_SHIFT_FW_DBG0_8822B) & BIT_MASK_FW_DBG0_8822B)
#define BIT_SET_FW_DBG0_8822B(x, v)                                            \
	(BIT_CLEAR_FW_DBG0_8822B(x) | BIT_FW_DBG0_8822B(v))

/* 2 REG_FW_DBG1_8822B */

#define BIT_SHIFT_FW_DBG1_8822B 0
#define BIT_MASK_FW_DBG1_8822B 0xffffffffL
#define BIT_FW_DBG1_8822B(x)                                                   \
	(((x) & BIT_MASK_FW_DBG1_8822B) << BIT_SHIFT_FW_DBG1_8822B)
#define BITS_FW_DBG1_8822B (BIT_MASK_FW_DBG1_8822B << BIT_SHIFT_FW_DBG1_8822B)
#define BIT_CLEAR_FW_DBG1_8822B(x) ((x) & (~BITS_FW_DBG1_8822B))
#define BIT_GET_FW_DBG1_8822B(x)                                               \
	(((x) >> BIT_SHIFT_FW_DBG1_8822B) & BIT_MASK_FW_DBG1_8822B)
#define BIT_SET_FW_DBG1_8822B(x, v)                                            \
	(BIT_CLEAR_FW_DBG1_8822B(x) | BIT_FW_DBG1_8822B(v))

/* 2 REG_FW_DBG2_8822B */

#define BIT_SHIFT_FW_DBG2_8822B 0
#define BIT_MASK_FW_DBG2_8822B 0xffffffffL
#define BIT_FW_DBG2_8822B(x)                                                   \
	(((x) & BIT_MASK_FW_DBG2_8822B) << BIT_SHIFT_FW_DBG2_8822B)
#define BITS_FW_DBG2_8822B (BIT_MASK_FW_DBG2_8822B << BIT_SHIFT_FW_DBG2_8822B)
#define BIT_CLEAR_FW_DBG2_8822B(x) ((x) & (~BITS_FW_DBG2_8822B))
#define BIT_GET_FW_DBG2_8822B(x)                                               \
	(((x) >> BIT_SHIFT_FW_DBG2_8822B) & BIT_MASK_FW_DBG2_8822B)
#define BIT_SET_FW_DBG2_8822B(x, v)                                            \
	(BIT_CLEAR_FW_DBG2_8822B(x) | BIT_FW_DBG2_8822B(v))

/* 2 REG_FW_DBG3_8822B */

#define BIT_SHIFT_FW_DBG3_8822B 0
#define BIT_MASK_FW_DBG3_8822B 0xffffffffL
#define BIT_FW_DBG3_8822B(x)                                                   \
	(((x) & BIT_MASK_FW_DBG3_8822B) << BIT_SHIFT_FW_DBG3_8822B)
#define BITS_FW_DBG3_8822B (BIT_MASK_FW_DBG3_8822B << BIT_SHIFT_FW_DBG3_8822B)
#define BIT_CLEAR_FW_DBG3_8822B(x) ((x) & (~BITS_FW_DBG3_8822B))
#define BIT_GET_FW_DBG3_8822B(x)                                               \
	(((x) >> BIT_SHIFT_FW_DBG3_8822B) & BIT_MASK_FW_DBG3_8822B)
#define BIT_SET_FW_DBG3_8822B(x, v)                                            \
	(BIT_CLEAR_FW_DBG3_8822B(x) | BIT_FW_DBG3_8822B(v))

/* 2 REG_FW_DBG4_8822B */

#define BIT_SHIFT_FW_DBG4_8822B 0
#define BIT_MASK_FW_DBG4_8822B 0xffffffffL
#define BIT_FW_DBG4_8822B(x)                                                   \
	(((x) & BIT_MASK_FW_DBG4_8822B) << BIT_SHIFT_FW_DBG4_8822B)
#define BITS_FW_DBG4_8822B (BIT_MASK_FW_DBG4_8822B << BIT_SHIFT_FW_DBG4_8822B)
#define BIT_CLEAR_FW_DBG4_8822B(x) ((x) & (~BITS_FW_DBG4_8822B))
#define BIT_GET_FW_DBG4_8822B(x)                                               \
	(((x) >> BIT_SHIFT_FW_DBG4_8822B) & BIT_MASK_FW_DBG4_8822B)
#define BIT_SET_FW_DBG4_8822B(x, v)                                            \
	(BIT_CLEAR_FW_DBG4_8822B(x) | BIT_FW_DBG4_8822B(v))

/* 2 REG_FW_DBG5_8822B */

#define BIT_SHIFT_FW_DBG5_8822B 0
#define BIT_MASK_FW_DBG5_8822B 0xffffffffL
#define BIT_FW_DBG5_8822B(x)                                                   \
	(((x) & BIT_MASK_FW_DBG5_8822B) << BIT_SHIFT_FW_DBG5_8822B)
#define BITS_FW_DBG5_8822B (BIT_MASK_FW_DBG5_8822B << BIT_SHIFT_FW_DBG5_8822B)
#define BIT_CLEAR_FW_DBG5_8822B(x) ((x) & (~BITS_FW_DBG5_8822B))
#define BIT_GET_FW_DBG5_8822B(x)                                               \
	(((x) >> BIT_SHIFT_FW_DBG5_8822B) & BIT_MASK_FW_DBG5_8822B)
#define BIT_SET_FW_DBG5_8822B(x, v)                                            \
	(BIT_CLEAR_FW_DBG5_8822B(x) | BIT_FW_DBG5_8822B(v))

/* 2 REG_FW_DBG6_8822B */

#define BIT_SHIFT_FW_DBG6_8822B 0
#define BIT_MASK_FW_DBG6_8822B 0xffffffffL
#define BIT_FW_DBG6_8822B(x)                                                   \
	(((x) & BIT_MASK_FW_DBG6_8822B) << BIT_SHIFT_FW_DBG6_8822B)
#define BITS_FW_DBG6_8822B (BIT_MASK_FW_DBG6_8822B << BIT_SHIFT_FW_DBG6_8822B)
#define BIT_CLEAR_FW_DBG6_8822B(x) ((x) & (~BITS_FW_DBG6_8822B))
#define BIT_GET_FW_DBG6_8822B(x)                                               \
	(((x) >> BIT_SHIFT_FW_DBG6_8822B) & BIT_MASK_FW_DBG6_8822B)
#define BIT_SET_FW_DBG6_8822B(x, v)                                            \
	(BIT_CLEAR_FW_DBG6_8822B(x) | BIT_FW_DBG6_8822B(v))

/* 2 REG_FW_DBG7_8822B */

#define BIT_SHIFT_FW_DBG7_8822B 0
#define BIT_MASK_FW_DBG7_8822B 0xffffffffL
#define BIT_FW_DBG7_8822B(x)                                                   \
	(((x) & BIT_MASK_FW_DBG7_8822B) << BIT_SHIFT_FW_DBG7_8822B)
#define BITS_FW_DBG7_8822B (BIT_MASK_FW_DBG7_8822B << BIT_SHIFT_FW_DBG7_8822B)
#define BIT_CLEAR_FW_DBG7_8822B(x) ((x) & (~BITS_FW_DBG7_8822B))
#define BIT_GET_FW_DBG7_8822B(x)                                               \
	(((x) >> BIT_SHIFT_FW_DBG7_8822B) & BIT_MASK_FW_DBG7_8822B)
#define BIT_SET_FW_DBG7_8822B(x, v)                                            \
	(BIT_CLEAR_FW_DBG7_8822B(x) | BIT_FW_DBG7_8822B(v))

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_CR_8822B */

#define BIT_SHIFT_LBMODE_8822B 24
#define BIT_MASK_LBMODE_8822B 0x1f
#define BIT_LBMODE_8822B(x)                                                    \
	(((x) & BIT_MASK_LBMODE_8822B) << BIT_SHIFT_LBMODE_8822B)
#define BITS_LBMODE_8822B (BIT_MASK_LBMODE_8822B << BIT_SHIFT_LBMODE_8822B)
#define BIT_CLEAR_LBMODE_8822B(x) ((x) & (~BITS_LBMODE_8822B))
#define BIT_GET_LBMODE_8822B(x)                                                \
	(((x) >> BIT_SHIFT_LBMODE_8822B) & BIT_MASK_LBMODE_8822B)
#define BIT_SET_LBMODE_8822B(x, v)                                             \
	(BIT_CLEAR_LBMODE_8822B(x) | BIT_LBMODE_8822B(v))

#define BIT_SHIFT_NETYPE1_8822B 18
#define BIT_MASK_NETYPE1_8822B 0x3
#define BIT_NETYPE1_8822B(x)                                                   \
	(((x) & BIT_MASK_NETYPE1_8822B) << BIT_SHIFT_NETYPE1_8822B)
#define BITS_NETYPE1_8822B (BIT_MASK_NETYPE1_8822B << BIT_SHIFT_NETYPE1_8822B)
#define BIT_CLEAR_NETYPE1_8822B(x) ((x) & (~BITS_NETYPE1_8822B))
#define BIT_GET_NETYPE1_8822B(x)                                               \
	(((x) >> BIT_SHIFT_NETYPE1_8822B) & BIT_MASK_NETYPE1_8822B)
#define BIT_SET_NETYPE1_8822B(x, v)                                            \
	(BIT_CLEAR_NETYPE1_8822B(x) | BIT_NETYPE1_8822B(v))

#define BIT_SHIFT_NETYPE0_8822B 16
#define BIT_MASK_NETYPE0_8822B 0x3
#define BIT_NETYPE0_8822B(x)                                                   \
	(((x) & BIT_MASK_NETYPE0_8822B) << BIT_SHIFT_NETYPE0_8822B)
#define BITS_NETYPE0_8822B (BIT_MASK_NETYPE0_8822B << BIT_SHIFT_NETYPE0_8822B)
#define BIT_CLEAR_NETYPE0_8822B(x) ((x) & (~BITS_NETYPE0_8822B))
#define BIT_GET_NETYPE0_8822B(x)                                               \
	(((x) >> BIT_SHIFT_NETYPE0_8822B) & BIT_MASK_NETYPE0_8822B)
#define BIT_SET_NETYPE0_8822B(x, v)                                            \
	(BIT_CLEAR_NETYPE0_8822B(x) | BIT_NETYPE0_8822B(v))

#define BIT_I2C_MAILBOX_EN_8822B BIT(12)
#define BIT_SHCUT_EN_8822B BIT(11)
#define BIT_32K_CAL_TMR_EN_8822B BIT(10)
#define BIT_MAC_SEC_EN_8822B BIT(9)
#define BIT_ENSWBCN_8822B BIT(8)
#define BIT_MACRXEN_8822B BIT(7)
#define BIT_MACTXEN_8822B BIT(6)
#define BIT_SCHEDULE_EN_8822B BIT(5)
#define BIT_PROTOCOL_EN_8822B BIT(4)
#define BIT_RXDMA_EN_8822B BIT(3)
#define BIT_TXDMA_EN_8822B BIT(2)
#define BIT_HCI_RXDMA_EN_8822B BIT(1)
#define BIT_HCI_TXDMA_EN_8822B BIT(0)

/* 2 REG_TSF_CLK_STATE_8822B */
#define BIT_TSF_CLK_STABLE_8822B BIT(15)

/* 2 REG_TXDMA_PQ_MAP_8822B */

#define BIT_SHIFT_TXDMA_HIQ_MAP_8822B 14
#define BIT_MASK_TXDMA_HIQ_MAP_8822B 0x3
#define BIT_TXDMA_HIQ_MAP_8822B(x)                                             \
	(((x) & BIT_MASK_TXDMA_HIQ_MAP_8822B) << BIT_SHIFT_TXDMA_HIQ_MAP_8822B)
#define BITS_TXDMA_HIQ_MAP_8822B                                               \
	(BIT_MASK_TXDMA_HIQ_MAP_8822B << BIT_SHIFT_TXDMA_HIQ_MAP_8822B)
#define BIT_CLEAR_TXDMA_HIQ_MAP_8822B(x) ((x) & (~BITS_TXDMA_HIQ_MAP_8822B))
#define BIT_GET_TXDMA_HIQ_MAP_8822B(x)                                         \
	(((x) >> BIT_SHIFT_TXDMA_HIQ_MAP_8822B) & BIT_MASK_TXDMA_HIQ_MAP_8822B)
#define BIT_SET_TXDMA_HIQ_MAP_8822B(x, v)                                      \
	(BIT_CLEAR_TXDMA_HIQ_MAP_8822B(x) | BIT_TXDMA_HIQ_MAP_8822B(v))

#define BIT_SHIFT_TXDMA_MGQ_MAP_8822B 12
#define BIT_MASK_TXDMA_MGQ_MAP_8822B 0x3
#define BIT_TXDMA_MGQ_MAP_8822B(x)                                             \
	(((x) & BIT_MASK_TXDMA_MGQ_MAP_8822B) << BIT_SHIFT_TXDMA_MGQ_MAP_8822B)
#define BITS_TXDMA_MGQ_MAP_8822B                                               \
	(BIT_MASK_TXDMA_MGQ_MAP_8822B << BIT_SHIFT_TXDMA_MGQ_MAP_8822B)
#define BIT_CLEAR_TXDMA_MGQ_MAP_8822B(x) ((x) & (~BITS_TXDMA_MGQ_MAP_8822B))
#define BIT_GET_TXDMA_MGQ_MAP_8822B(x)                                         \
	(((x) >> BIT_SHIFT_TXDMA_MGQ_MAP_8822B) & BIT_MASK_TXDMA_MGQ_MAP_8822B)
#define BIT_SET_TXDMA_MGQ_MAP_8822B(x, v)                                      \
	(BIT_CLEAR_TXDMA_MGQ_MAP_8822B(x) | BIT_TXDMA_MGQ_MAP_8822B(v))

#define BIT_SHIFT_TXDMA_BKQ_MAP_8822B 10
#define BIT_MASK_TXDMA_BKQ_MAP_8822B 0x3
#define BIT_TXDMA_BKQ_MAP_8822B(x)                                             \
	(((x) & BIT_MASK_TXDMA_BKQ_MAP_8822B) << BIT_SHIFT_TXDMA_BKQ_MAP_8822B)
#define BITS_TXDMA_BKQ_MAP_8822B                                               \
	(BIT_MASK_TXDMA_BKQ_MAP_8822B << BIT_SHIFT_TXDMA_BKQ_MAP_8822B)
#define BIT_CLEAR_TXDMA_BKQ_MAP_8822B(x) ((x) & (~BITS_TXDMA_BKQ_MAP_8822B))
#define BIT_GET_TXDMA_BKQ_MAP_8822B(x)                                         \
	(((x) >> BIT_SHIFT_TXDMA_BKQ_MAP_8822B) & BIT_MASK_TXDMA_BKQ_MAP_8822B)
#define BIT_SET_TXDMA_BKQ_MAP_8822B(x, v)                                      \
	(BIT_CLEAR_TXDMA_BKQ_MAP_8822B(x) | BIT_TXDMA_BKQ_MAP_8822B(v))

#define BIT_SHIFT_TXDMA_BEQ_MAP_8822B 8
#define BIT_MASK_TXDMA_BEQ_MAP_8822B 0x3
#define BIT_TXDMA_BEQ_MAP_8822B(x)                                             \
	(((x) & BIT_MASK_TXDMA_BEQ_MAP_8822B) << BIT_SHIFT_TXDMA_BEQ_MAP_8822B)
#define BITS_TXDMA_BEQ_MAP_8822B                                               \
	(BIT_MASK_TXDMA_BEQ_MAP_8822B << BIT_SHIFT_TXDMA_BEQ_MAP_8822B)
#define BIT_CLEAR_TXDMA_BEQ_MAP_8822B(x) ((x) & (~BITS_TXDMA_BEQ_MAP_8822B))
#define BIT_GET_TXDMA_BEQ_MAP_8822B(x)                                         \
	(((x) >> BIT_SHIFT_TXDMA_BEQ_MAP_8822B) & BIT_MASK_TXDMA_BEQ_MAP_8822B)
#define BIT_SET_TXDMA_BEQ_MAP_8822B(x, v)                                      \
	(BIT_CLEAR_TXDMA_BEQ_MAP_8822B(x) | BIT_TXDMA_BEQ_MAP_8822B(v))

#define BIT_SHIFT_TXDMA_VIQ_MAP_8822B 6
#define BIT_MASK_TXDMA_VIQ_MAP_8822B 0x3
#define BIT_TXDMA_VIQ_MAP_8822B(x)                                             \
	(((x) & BIT_MASK_TXDMA_VIQ_MAP_8822B) << BIT_SHIFT_TXDMA_VIQ_MAP_8822B)
#define BITS_TXDMA_VIQ_MAP_8822B                                               \
	(BIT_MASK_TXDMA_VIQ_MAP_8822B << BIT_SHIFT_TXDMA_VIQ_MAP_8822B)
#define BIT_CLEAR_TXDMA_VIQ_MAP_8822B(x) ((x) & (~BITS_TXDMA_VIQ_MAP_8822B))
#define BIT_GET_TXDMA_VIQ_MAP_8822B(x)                                         \
	(((x) >> BIT_SHIFT_TXDMA_VIQ_MAP_8822B) & BIT_MASK_TXDMA_VIQ_MAP_8822B)
#define BIT_SET_TXDMA_VIQ_MAP_8822B(x, v)                                      \
	(BIT_CLEAR_TXDMA_VIQ_MAP_8822B(x) | BIT_TXDMA_VIQ_MAP_8822B(v))

#define BIT_SHIFT_TXDMA_VOQ_MAP_8822B 4
#define BIT_MASK_TXDMA_VOQ_MAP_8822B 0x3
#define BIT_TXDMA_VOQ_MAP_8822B(x)                                             \
	(((x) & BIT_MASK_TXDMA_VOQ_MAP_8822B) << BIT_SHIFT_TXDMA_VOQ_MAP_8822B)
#define BITS_TXDMA_VOQ_MAP_8822B                                               \
	(BIT_MASK_TXDMA_VOQ_MAP_8822B << BIT_SHIFT_TXDMA_VOQ_MAP_8822B)
#define BIT_CLEAR_TXDMA_VOQ_MAP_8822B(x) ((x) & (~BITS_TXDMA_VOQ_MAP_8822B))
#define BIT_GET_TXDMA_VOQ_MAP_8822B(x)                                         \
	(((x) >> BIT_SHIFT_TXDMA_VOQ_MAP_8822B) & BIT_MASK_TXDMA_VOQ_MAP_8822B)
#define BIT_SET_TXDMA_VOQ_MAP_8822B(x, v)                                      \
	(BIT_CLEAR_TXDMA_VOQ_MAP_8822B(x) | BIT_TXDMA_VOQ_MAP_8822B(v))

#define BIT_RXDMA_AGG_EN_8822B BIT(2)
#define BIT_RXSHFT_EN_8822B BIT(1)
#define BIT_RXDMA_ARBBW_EN_8822B BIT(0)

/* 2 REG_TRXFF_BNDY_8822B */

#define BIT_SHIFT_RXFFOVFL_RSV_V2_8822B 8
#define BIT_MASK_RXFFOVFL_RSV_V2_8822B 0xf
#define BIT_RXFFOVFL_RSV_V2_8822B(x)                                           \
	(((x) & BIT_MASK_RXFFOVFL_RSV_V2_8822B)                                \
	 << BIT_SHIFT_RXFFOVFL_RSV_V2_8822B)
#define BITS_RXFFOVFL_RSV_V2_8822B                                             \
	(BIT_MASK_RXFFOVFL_RSV_V2_8822B << BIT_SHIFT_RXFFOVFL_RSV_V2_8822B)
#define BIT_CLEAR_RXFFOVFL_RSV_V2_8822B(x) ((x) & (~BITS_RXFFOVFL_RSV_V2_8822B))
#define BIT_GET_RXFFOVFL_RSV_V2_8822B(x)                                       \
	(((x) >> BIT_SHIFT_RXFFOVFL_RSV_V2_8822B) &                            \
	 BIT_MASK_RXFFOVFL_RSV_V2_8822B)
#define BIT_SET_RXFFOVFL_RSV_V2_8822B(x, v)                                    \
	(BIT_CLEAR_RXFFOVFL_RSV_V2_8822B(x) | BIT_RXFFOVFL_RSV_V2_8822B(v))

#define BIT_SHIFT_TXPKTBUF_PGBNDY_8822B 0
#define BIT_MASK_TXPKTBUF_PGBNDY_8822B 0xff
#define BIT_TXPKTBUF_PGBNDY_8822B(x)                                           \
	(((x) & BIT_MASK_TXPKTBUF_PGBNDY_8822B)                                \
	 << BIT_SHIFT_TXPKTBUF_PGBNDY_8822B)
#define BITS_TXPKTBUF_PGBNDY_8822B                                             \
	(BIT_MASK_TXPKTBUF_PGBNDY_8822B << BIT_SHIFT_TXPKTBUF_PGBNDY_8822B)
#define BIT_CLEAR_TXPKTBUF_PGBNDY_8822B(x) ((x) & (~BITS_TXPKTBUF_PGBNDY_8822B))
#define BIT_GET_TXPKTBUF_PGBNDY_8822B(x)                                       \
	(((x) >> BIT_SHIFT_TXPKTBUF_PGBNDY_8822B) &                            \
	 BIT_MASK_TXPKTBUF_PGBNDY_8822B)
#define BIT_SET_TXPKTBUF_PGBNDY_8822B(x, v)                                    \
	(BIT_CLEAR_TXPKTBUF_PGBNDY_8822B(x) | BIT_TXPKTBUF_PGBNDY_8822B(v))

/* 2 REG_PTA_I2C_MBOX_8822B */

/* 2 REG_NOT_VALID_8822B */

#define BIT_SHIFT_I2C_M_STATUS_8822B 8
#define BIT_MASK_I2C_M_STATUS_8822B 0xf
#define BIT_I2C_M_STATUS_8822B(x)                                              \
	(((x) & BIT_MASK_I2C_M_STATUS_8822B) << BIT_SHIFT_I2C_M_STATUS_8822B)
#define BITS_I2C_M_STATUS_8822B                                                \
	(BIT_MASK_I2C_M_STATUS_8822B << BIT_SHIFT_I2C_M_STATUS_8822B)
#define BIT_CLEAR_I2C_M_STATUS_8822B(x) ((x) & (~BITS_I2C_M_STATUS_8822B))
#define BIT_GET_I2C_M_STATUS_8822B(x)                                          \
	(((x) >> BIT_SHIFT_I2C_M_STATUS_8822B) & BIT_MASK_I2C_M_STATUS_8822B)
#define BIT_SET_I2C_M_STATUS_8822B(x, v)                                       \
	(BIT_CLEAR_I2C_M_STATUS_8822B(x) | BIT_I2C_M_STATUS_8822B(v))

#define BIT_SHIFT_I2C_M_BUS_GNT_FW_8822B 4
#define BIT_MASK_I2C_M_BUS_GNT_FW_8822B 0x7
#define BIT_I2C_M_BUS_GNT_FW_8822B(x)                                          \
	(((x) & BIT_MASK_I2C_M_BUS_GNT_FW_8822B)                               \
	 << BIT_SHIFT_I2C_M_BUS_GNT_FW_8822B)
#define BITS_I2C_M_BUS_GNT_FW_8822B                                            \
	(BIT_MASK_I2C_M_BUS_GNT_FW_8822B << BIT_SHIFT_I2C_M_BUS_GNT_FW_8822B)
#define BIT_CLEAR_I2C_M_BUS_GNT_FW_8822B(x)                                    \
	((x) & (~BITS_I2C_M_BUS_GNT_FW_8822B))
#define BIT_GET_I2C_M_BUS_GNT_FW_8822B(x)                                      \
	(((x) >> BIT_SHIFT_I2C_M_BUS_GNT_FW_8822B) &                           \
	 BIT_MASK_I2C_M_BUS_GNT_FW_8822B)
#define BIT_SET_I2C_M_BUS_GNT_FW_8822B(x, v)                                   \
	(BIT_CLEAR_I2C_M_BUS_GNT_FW_8822B(x) | BIT_I2C_M_BUS_GNT_FW_8822B(v))

#define BIT_I2C_M_GNT_FW_8822B BIT(3)

#define BIT_SHIFT_I2C_M_SPEED_8822B 1
#define BIT_MASK_I2C_M_SPEED_8822B 0x3
#define BIT_I2C_M_SPEED_8822B(x)                                               \
	(((x) & BIT_MASK_I2C_M_SPEED_8822B) << BIT_SHIFT_I2C_M_SPEED_8822B)
#define BITS_I2C_M_SPEED_8822B                                                 \
	(BIT_MASK_I2C_M_SPEED_8822B << BIT_SHIFT_I2C_M_SPEED_8822B)
#define BIT_CLEAR_I2C_M_SPEED_8822B(x) ((x) & (~BITS_I2C_M_SPEED_8822B))
#define BIT_GET_I2C_M_SPEED_8822B(x)                                           \
	(((x) >> BIT_SHIFT_I2C_M_SPEED_8822B) & BIT_MASK_I2C_M_SPEED_8822B)
#define BIT_SET_I2C_M_SPEED_8822B(x, v)                                        \
	(BIT_CLEAR_I2C_M_SPEED_8822B(x) | BIT_I2C_M_SPEED_8822B(v))

#define BIT_I2C_M_UNLOCK_8822B BIT(0)

/* 2 REG_RXFF_BNDY_8822B */

/* 2 REG_NOT_VALID_8822B */

#define BIT_SHIFT_RXFF0_BNDY_V2_8822B 0
#define BIT_MASK_RXFF0_BNDY_V2_8822B 0x3ffff
#define BIT_RXFF0_BNDY_V2_8822B(x)                                             \
	(((x) & BIT_MASK_RXFF0_BNDY_V2_8822B) << BIT_SHIFT_RXFF0_BNDY_V2_8822B)
#define BITS_RXFF0_BNDY_V2_8822B                                               \
	(BIT_MASK_RXFF0_BNDY_V2_8822B << BIT_SHIFT_RXFF0_BNDY_V2_8822B)
#define BIT_CLEAR_RXFF0_BNDY_V2_8822B(x) ((x) & (~BITS_RXFF0_BNDY_V2_8822B))
#define BIT_GET_RXFF0_BNDY_V2_8822B(x)                                         \
	(((x) >> BIT_SHIFT_RXFF0_BNDY_V2_8822B) & BIT_MASK_RXFF0_BNDY_V2_8822B)
#define BIT_SET_RXFF0_BNDY_V2_8822B(x, v)                                      \
	(BIT_CLEAR_RXFF0_BNDY_V2_8822B(x) | BIT_RXFF0_BNDY_V2_8822B(v))

/* 2 REG_FE1IMR_8822B */
#define BIT_FS_RXDMA2_DONE_INT_EN_8822B BIT(28)
#define BIT_FS_RXDONE3_INT_EN_8822B BIT(27)
#define BIT_FS_RXDONE2_INT_EN_8822B BIT(26)
#define BIT_FS_RX_BCN_P4_INT_EN_8822B BIT(25)
#define BIT_FS_RX_BCN_P3_INT_EN_8822B BIT(24)
#define BIT_FS_RX_BCN_P2_INT_EN_8822B BIT(23)
#define BIT_FS_RX_BCN_P1_INT_EN_8822B BIT(22)
#define BIT_FS_RX_BCN_P0_INT_EN_8822B BIT(21)
#define BIT_FS_RX_UMD0_INT_EN_8822B BIT(20)
#define BIT_FS_RX_UMD1_INT_EN_8822B BIT(19)
#define BIT_FS_RX_BMD0_INT_EN_8822B BIT(18)
#define BIT_FS_RX_BMD1_INT_EN_8822B BIT(17)
#define BIT_FS_RXDONE_INT_EN_8822B BIT(16)
#define BIT_FS_WWLAN_INT_EN_8822B BIT(15)
#define BIT_FS_SOUND_DONE_INT_EN_8822B BIT(14)
#define BIT_FS_LP_STBY_INT_EN_8822B BIT(13)
#define BIT_FS_TRL_MTR_INT_EN_8822B BIT(12)
#define BIT_FS_BF1_PRETO_INT_EN_8822B BIT(11)
#define BIT_FS_BF0_PRETO_INT_EN_8822B BIT(10)
#define BIT_FS_PTCL_RELEASE_MACID_INT_EN_8822B BIT(9)
#define BIT_FS_LTE_COEX_EN_8822B BIT(6)
#define BIT_FS_WLACTOFF_INT_EN_8822B BIT(5)
#define BIT_FS_WLACTON_INT_EN_8822B BIT(4)
#define BIT_FS_BTCMD_INT_EN_8822B BIT(3)
#define BIT_FS_REG_MAILBOX_TO_I2C_INT_EN_8822B BIT(2)
#define BIT_FS_TRPC_TO_INT_EN_V1_8822B BIT(1)
#define BIT_FS_RPC_O_T_INT_EN_V1_8822B BIT(0)

/* 2 REG_FE1ISR_8822B */
#define BIT_FS_RXDMA2_DONE_INT_8822B BIT(28)
#define BIT_FS_RXDONE3_INT_8822B BIT(27)
#define BIT_FS_RXDONE2_INT_8822B BIT(26)
#define BIT_FS_RX_BCN_P4_INT_8822B BIT(25)
#define BIT_FS_RX_BCN_P3_INT_8822B BIT(24)
#define BIT_FS_RX_BCN_P2_INT_8822B BIT(23)
#define BIT_FS_RX_BCN_P1_INT_8822B BIT(22)
#define BIT_FS_RX_BCN_P0_INT_8822B BIT(21)
#define BIT_FS_RX_UMD0_INT_8822B BIT(20)
#define BIT_FS_RX_UMD1_INT_8822B BIT(19)
#define BIT_FS_RX_BMD0_INT_8822B BIT(18)
#define BIT_FS_RX_BMD1_INT_8822B BIT(17)
#define BIT_FS_RXDONE_INT_8822B BIT(16)
#define BIT_FS_WWLAN_INT_8822B BIT(15)
#define BIT_FS_SOUND_DONE_INT_8822B BIT(14)
#define BIT_FS_LP_STBY_INT_8822B BIT(13)
#define BIT_FS_TRL_MTR_INT_8822B BIT(12)
#define BIT_FS_BF1_PRETO_INT_8822B BIT(11)
#define BIT_FS_BF0_PRETO_INT_8822B BIT(10)
#define BIT_FS_PTCL_RELEASE_MACID_INT_8822B BIT(9)
#define BIT_FS_LTE_COEX_INT_8822B BIT(6)
#define BIT_FS_WLACTOFF_INT_8822B BIT(5)
#define BIT_FS_WLACTON_INT_8822B BIT(4)
#define BIT_FS_BCN_RX_INT_INT_8822B BIT(3)
#define BIT_FS_MAILBOX_TO_I2C_INT_8822B BIT(2)
#define BIT_FS_TRPC_TO_INT_8822B BIT(1)
#define BIT_FS_RPC_O_T_INT_8822B BIT(0)

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_CPWM_8822B */
#define BIT_CPWM_TOGGLING_8822B BIT(31)

#define BIT_SHIFT_CPWM_MOD_8822B 24
#define BIT_MASK_CPWM_MOD_8822B 0x7f
#define BIT_CPWM_MOD_8822B(x)                                                  \
	(((x) & BIT_MASK_CPWM_MOD_8822B) << BIT_SHIFT_CPWM_MOD_8822B)
#define BITS_CPWM_MOD_8822B                                                    \
	(BIT_MASK_CPWM_MOD_8822B << BIT_SHIFT_CPWM_MOD_8822B)
#define BIT_CLEAR_CPWM_MOD_8822B(x) ((x) & (~BITS_CPWM_MOD_8822B))
#define BIT_GET_CPWM_MOD_8822B(x)                                              \
	(((x) >> BIT_SHIFT_CPWM_MOD_8822B) & BIT_MASK_CPWM_MOD_8822B)
#define BIT_SET_CPWM_MOD_8822B(x, v)                                           \
	(BIT_CLEAR_CPWM_MOD_8822B(x) | BIT_CPWM_MOD_8822B(v))

/* 2 REG_FWIMR_8822B */
#define BIT_FS_TXBCNOK_MB7_INT_EN_8822B BIT(31)
#define BIT_FS_TXBCNOK_MB6_INT_EN_8822B BIT(30)
#define BIT_FS_TXBCNOK_MB5_INT_EN_8822B BIT(29)
#define BIT_FS_TXBCNOK_MB4_INT_EN_8822B BIT(28)
#define BIT_FS_TXBCNOK_MB3_INT_EN_8822B BIT(27)
#define BIT_FS_TXBCNOK_MB2_INT_EN_8822B BIT(26)
#define BIT_FS_TXBCNOK_MB1_INT_EN_8822B BIT(25)
#define BIT_FS_TXBCNOK_MB0_INT_EN_8822B BIT(24)
#define BIT_FS_TXBCNERR_MB7_INT_EN_8822B BIT(23)
#define BIT_FS_TXBCNERR_MB6_INT_EN_8822B BIT(22)
#define BIT_FS_TXBCNERR_MB5_INT_EN_8822B BIT(21)
#define BIT_FS_TXBCNERR_MB4_INT_EN_8822B BIT(20)
#define BIT_FS_TXBCNERR_MB3_INT_EN_8822B BIT(19)
#define BIT_FS_TXBCNERR_MB2_INT_EN_8822B BIT(18)
#define BIT_FS_TXBCNERR_MB1_INT_EN_8822B BIT(17)
#define BIT_FS_TXBCNERR_MB0_INT_EN_8822B BIT(16)
#define BIT_CPU_MGQ_TXDONE_INT_EN_8822B BIT(15)
#define BIT_SIFS_OVERSPEC_INT_EN_8822B BIT(14)
#define BIT_FS_MGNTQ_RPTR_RELEASE_INT_EN_8822B BIT(13)
#define BIT_FS_MGNTQFF_TO_INT_EN_8822B BIT(12)
#define BIT_FS_CPUMGQ_ERR_INT_EN_8822B BIT(11)
#define BIT_FS_DDMA0_LP_INT_EN_8822B BIT(9)
#define BIT_FS_DDMA0_HP_INT_EN_8822B BIT(8)
#define BIT_FS_TRXRPT_INT_EN_8822B BIT(7)
#define BIT_FS_C2H_W_READY_INT_EN_8822B BIT(6)
#define BIT_FS_HRCV_INT_EN_8822B BIT(5)
#define BIT_FS_H2CCMD_INT_EN_8822B BIT(4)
#define BIT_FS_TXPKTIN_INT_EN_8822B BIT(3)
#define BIT_FS_ERRORHDL_INT_EN_8822B BIT(2)
#define BIT_FS_TXCCX_INT_EN_8822B BIT(1)
#define BIT_FS_TXCLOSE_INT_EN_8822B BIT(0)

/* 2 REG_FWISR_8822B */
#define BIT_FS_TXBCNOK_MB7_INT_8822B BIT(31)
#define BIT_FS_TXBCNOK_MB6_INT_8822B BIT(30)
#define BIT_FS_TXBCNOK_MB5_INT_8822B BIT(29)
#define BIT_FS_TXBCNOK_MB4_INT_8822B BIT(28)
#define BIT_FS_TXBCNOK_MB3_INT_8822B BIT(27)
#define BIT_FS_TXBCNOK_MB2_INT_8822B BIT(26)
#define BIT_FS_TXBCNOK_MB1_INT_8822B BIT(25)
#define BIT_FS_TXBCNOK_MB0_INT_8822B BIT(24)
#define BIT_FS_TXBCNERR_MB7_INT_8822B BIT(23)
#define BIT_FS_TXBCNERR_MB6_INT_8822B BIT(22)
#define BIT_FS_TXBCNERR_MB5_INT_8822B BIT(21)
#define BIT_FS_TXBCNERR_MB4_INT_8822B BIT(20)
#define BIT_FS_TXBCNERR_MB3_INT_8822B BIT(19)
#define BIT_FS_TXBCNERR_MB2_INT_8822B BIT(18)
#define BIT_FS_TXBCNERR_MB1_INT_8822B BIT(17)
#define BIT_FS_TXBCNERR_MB0_INT_8822B BIT(16)
#define BIT_CPU_MGQ_TXDONE_INT_8822B BIT(15)
#define BIT_SIFS_OVERSPEC_INT_8822B BIT(14)
#define BIT_FS_MGNTQ_RPTR_RELEASE_INT_8822B BIT(13)
#define BIT_FS_MGNTQFF_TO_INT_8822B BIT(12)
#define BIT_FS_CPUMGQ_ERR_INT_8822B BIT(11)
#define BIT_FS_DDMA0_LP_INT_8822B BIT(9)
#define BIT_FS_DDMA0_HP_INT_8822B BIT(8)
#define BIT_FS_TRXRPT_INT_8822B BIT(7)
#define BIT_FS_C2H_W_READY_INT_8822B BIT(6)
#define BIT_FS_HRCV_INT_8822B BIT(5)
#define BIT_FS_H2CCMD_INT_8822B BIT(4)
#define BIT_FS_TXPKTIN_INT_8822B BIT(3)
#define BIT_FS_ERRORHDL_INT_8822B BIT(2)
#define BIT_FS_TXCCX_INT_8822B BIT(1)
#define BIT_FS_TXCLOSE_INT_8822B BIT(0)

/* 2 REG_FTIMR_8822B */
#define BIT_PS_TIMER_C_EARLY_INT_EN_8822B BIT(23)
#define BIT_PS_TIMER_B_EARLY_INT_EN_8822B BIT(22)
#define BIT_PS_TIMER_A_EARLY_INT_EN_8822B BIT(21)
#define BIT_CPUMGQ_TX_TIMER_EARLY_INT_EN_8822B BIT(20)
#define BIT_PS_TIMER_C_INT_EN_8822B BIT(19)
#define BIT_PS_TIMER_B_INT_EN_8822B BIT(18)
#define BIT_PS_TIMER_A_INT_EN_8822B BIT(17)
#define BIT_CPUMGQ_TX_TIMER_INT_EN_8822B BIT(16)
#define BIT_FS_PS_TIMEOUT2_EN_8822B BIT(15)
#define BIT_FS_PS_TIMEOUT1_EN_8822B BIT(14)
#define BIT_FS_PS_TIMEOUT0_EN_8822B BIT(13)
#define BIT_FS_GTINT8_EN_8822B BIT(8)
#define BIT_FS_GTINT7_EN_8822B BIT(7)
#define BIT_FS_GTINT6_EN_8822B BIT(6)
#define BIT_FS_GTINT5_EN_8822B BIT(5)
#define BIT_FS_GTINT4_EN_8822B BIT(4)
#define BIT_FS_GTINT3_EN_8822B BIT(3)
#define BIT_FS_GTINT2_EN_8822B BIT(2)
#define BIT_FS_GTINT1_EN_8822B BIT(1)
#define BIT_FS_GTINT0_EN_8822B BIT(0)

/* 2 REG_FTISR_8822B */
#define BIT_PS_TIMER_C_EARLY__INT_8822B BIT(23)
#define BIT_PS_TIMER_B_EARLY__INT_8822B BIT(22)
#define BIT_PS_TIMER_A_EARLY__INT_8822B BIT(21)
#define BIT_CPUMGQ_TX_TIMER_EARLY_INT_8822B BIT(20)
#define BIT_PS_TIMER_C_INT_8822B BIT(19)
#define BIT_PS_TIMER_B_INT_8822B BIT(18)
#define BIT_PS_TIMER_A_INT_8822B BIT(17)
#define BIT_CPUMGQ_TX_TIMER_INT_8822B BIT(16)
#define BIT_FS_PS_TIMEOUT2_INT_8822B BIT(15)
#define BIT_FS_PS_TIMEOUT1_INT_8822B BIT(14)
#define BIT_FS_PS_TIMEOUT0_INT_8822B BIT(13)
#define BIT_FS_GTINT8_INT_8822B BIT(8)
#define BIT_FS_GTINT7_INT_8822B BIT(7)
#define BIT_FS_GTINT6_INT_8822B BIT(6)
#define BIT_FS_GTINT5_INT_8822B BIT(5)
#define BIT_FS_GTINT4_INT_8822B BIT(4)
#define BIT_FS_GTINT3_INT_8822B BIT(3)
#define BIT_FS_GTINT2_INT_8822B BIT(2)
#define BIT_FS_GTINT1_INT_8822B BIT(1)
#define BIT_FS_GTINT0_INT_8822B BIT(0)

/* 2 REG_PKTBUF_DBG_CTRL_8822B */

#define BIT_SHIFT_PKTBUF_WRITE_EN_8822B 24
#define BIT_MASK_PKTBUF_WRITE_EN_8822B 0xff
#define BIT_PKTBUF_WRITE_EN_8822B(x)                                           \
	(((x) & BIT_MASK_PKTBUF_WRITE_EN_8822B)                                \
	 << BIT_SHIFT_PKTBUF_WRITE_EN_8822B)
#define BITS_PKTBUF_WRITE_EN_8822B                                             \
	(BIT_MASK_PKTBUF_WRITE_EN_8822B << BIT_SHIFT_PKTBUF_WRITE_EN_8822B)
#define BIT_CLEAR_PKTBUF_WRITE_EN_8822B(x) ((x) & (~BITS_PKTBUF_WRITE_EN_8822B))
#define BIT_GET_PKTBUF_WRITE_EN_8822B(x)                                       \
	(((x) >> BIT_SHIFT_PKTBUF_WRITE_EN_8822B) &                            \
	 BIT_MASK_PKTBUF_WRITE_EN_8822B)
#define BIT_SET_PKTBUF_WRITE_EN_8822B(x, v)                                    \
	(BIT_CLEAR_PKTBUF_WRITE_EN_8822B(x) | BIT_PKTBUF_WRITE_EN_8822B(v))

#define BIT_TXRPTBUF_DBG_8822B BIT(23)

/* 2 REG_NOT_VALID_8822B */
#define BIT_TXPKTBUF_DBG_V2_8822B BIT(20)
#define BIT_RXPKTBUF_DBG_8822B BIT(16)

#define BIT_SHIFT_PKTBUF_DBG_ADDR_8822B 0
#define BIT_MASK_PKTBUF_DBG_ADDR_8822B 0x1fff
#define BIT_PKTBUF_DBG_ADDR_8822B(x)                                           \
	(((x) & BIT_MASK_PKTBUF_DBG_ADDR_8822B)                                \
	 << BIT_SHIFT_PKTBUF_DBG_ADDR_8822B)
#define BITS_PKTBUF_DBG_ADDR_8822B                                             \
	(BIT_MASK_PKTBUF_DBG_ADDR_8822B << BIT_SHIFT_PKTBUF_DBG_ADDR_8822B)
#define BIT_CLEAR_PKTBUF_DBG_ADDR_8822B(x) ((x) & (~BITS_PKTBUF_DBG_ADDR_8822B))
#define BIT_GET_PKTBUF_DBG_ADDR_8822B(x)                                       \
	(((x) >> BIT_SHIFT_PKTBUF_DBG_ADDR_8822B) &                            \
	 BIT_MASK_PKTBUF_DBG_ADDR_8822B)
#define BIT_SET_PKTBUF_DBG_ADDR_8822B(x, v)                                    \
	(BIT_CLEAR_PKTBUF_DBG_ADDR_8822B(x) | BIT_PKTBUF_DBG_ADDR_8822B(v))

/* 2 REG_PKTBUF_DBG_DATA_L_8822B */

#define BIT_SHIFT_PKTBUF_DBG_DATA_L_8822B 0
#define BIT_MASK_PKTBUF_DBG_DATA_L_8822B 0xffffffffL
#define BIT_PKTBUF_DBG_DATA_L_8822B(x)                                         \
	(((x) & BIT_MASK_PKTBUF_DBG_DATA_L_8822B)                              \
	 << BIT_SHIFT_PKTBUF_DBG_DATA_L_8822B)
#define BITS_PKTBUF_DBG_DATA_L_8822B                                           \
	(BIT_MASK_PKTBUF_DBG_DATA_L_8822B << BIT_SHIFT_PKTBUF_DBG_DATA_L_8822B)
#define BIT_CLEAR_PKTBUF_DBG_DATA_L_8822B(x)                                   \
	((x) & (~BITS_PKTBUF_DBG_DATA_L_8822B))
#define BIT_GET_PKTBUF_DBG_DATA_L_8822B(x)                                     \
	(((x) >> BIT_SHIFT_PKTBUF_DBG_DATA_L_8822B) &                          \
	 BIT_MASK_PKTBUF_DBG_DATA_L_8822B)
#define BIT_SET_PKTBUF_DBG_DATA_L_8822B(x, v)                                  \
	(BIT_CLEAR_PKTBUF_DBG_DATA_L_8822B(x) | BIT_PKTBUF_DBG_DATA_L_8822B(v))

/* 2 REG_PKTBUF_DBG_DATA_H_8822B */

#define BIT_SHIFT_PKTBUF_DBG_DATA_H_8822B 0
#define BIT_MASK_PKTBUF_DBG_DATA_H_8822B 0xffffffffL
#define BIT_PKTBUF_DBG_DATA_H_8822B(x)                                         \
	(((x) & BIT_MASK_PKTBUF_DBG_DATA_H_8822B)                              \
	 << BIT_SHIFT_PKTBUF_DBG_DATA_H_8822B)
#define BITS_PKTBUF_DBG_DATA_H_8822B                                           \
	(BIT_MASK_PKTBUF_DBG_DATA_H_8822B << BIT_SHIFT_PKTBUF_DBG_DATA_H_8822B)
#define BIT_CLEAR_PKTBUF_DBG_DATA_H_8822B(x)                                   \
	((x) & (~BITS_PKTBUF_DBG_DATA_H_8822B))
#define BIT_GET_PKTBUF_DBG_DATA_H_8822B(x)                                     \
	(((x) >> BIT_SHIFT_PKTBUF_DBG_DATA_H_8822B) &                          \
	 BIT_MASK_PKTBUF_DBG_DATA_H_8822B)
#define BIT_SET_PKTBUF_DBG_DATA_H_8822B(x, v)                                  \
	(BIT_CLEAR_PKTBUF_DBG_DATA_H_8822B(x) | BIT_PKTBUF_DBG_DATA_H_8822B(v))

/* 2 REG_CPWM2_8822B */

#define BIT_SHIFT_L0S_TO_RCVY_NUM_8822B 16
#define BIT_MASK_L0S_TO_RCVY_NUM_8822B 0xff
#define BIT_L0S_TO_RCVY_NUM_8822B(x)                                           \
	(((x) & BIT_MASK_L0S_TO_RCVY_NUM_8822B)                                \
	 << BIT_SHIFT_L0S_TO_RCVY_NUM_8822B)
#define BITS_L0S_TO_RCVY_NUM_8822B                                             \
	(BIT_MASK_L0S_TO_RCVY_NUM_8822B << BIT_SHIFT_L0S_TO_RCVY_NUM_8822B)
#define BIT_CLEAR_L0S_TO_RCVY_NUM_8822B(x) ((x) & (~BITS_L0S_TO_RCVY_NUM_8822B))
#define BIT_GET_L0S_TO_RCVY_NUM_8822B(x)                                       \
	(((x) >> BIT_SHIFT_L0S_TO_RCVY_NUM_8822B) &                            \
	 BIT_MASK_L0S_TO_RCVY_NUM_8822B)
#define BIT_SET_L0S_TO_RCVY_NUM_8822B(x, v)                                    \
	(BIT_CLEAR_L0S_TO_RCVY_NUM_8822B(x) | BIT_L0S_TO_RCVY_NUM_8822B(v))

#define BIT_CPWM2_TOGGLING_8822B BIT(15)

#define BIT_SHIFT_CPWM2_MOD_8822B 0
#define BIT_MASK_CPWM2_MOD_8822B 0x7fff
#define BIT_CPWM2_MOD_8822B(x)                                                 \
	(((x) & BIT_MASK_CPWM2_MOD_8822B) << BIT_SHIFT_CPWM2_MOD_8822B)
#define BITS_CPWM2_MOD_8822B                                                   \
	(BIT_MASK_CPWM2_MOD_8822B << BIT_SHIFT_CPWM2_MOD_8822B)
#define BIT_CLEAR_CPWM2_MOD_8822B(x) ((x) & (~BITS_CPWM2_MOD_8822B))
#define BIT_GET_CPWM2_MOD_8822B(x)                                             \
	(((x) >> BIT_SHIFT_CPWM2_MOD_8822B) & BIT_MASK_CPWM2_MOD_8822B)
#define BIT_SET_CPWM2_MOD_8822B(x, v)                                          \
	(BIT_CLEAR_CPWM2_MOD_8822B(x) | BIT_CPWM2_MOD_8822B(v))

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_TC0_CTRL_8822B */
#define BIT_TC0INT_EN_8822B BIT(26)
#define BIT_TC0MODE_8822B BIT(25)
#define BIT_TC0EN_8822B BIT(24)

#define BIT_SHIFT_TC0DATA_8822B 0
#define BIT_MASK_TC0DATA_8822B 0xffffff
#define BIT_TC0DATA_8822B(x)                                                   \
	(((x) & BIT_MASK_TC0DATA_8822B) << BIT_SHIFT_TC0DATA_8822B)
#define BITS_TC0DATA_8822B (BIT_MASK_TC0DATA_8822B << BIT_SHIFT_TC0DATA_8822B)
#define BIT_CLEAR_TC0DATA_8822B(x) ((x) & (~BITS_TC0DATA_8822B))
#define BIT_GET_TC0DATA_8822B(x)                                               \
	(((x) >> BIT_SHIFT_TC0DATA_8822B) & BIT_MASK_TC0DATA_8822B)
#define BIT_SET_TC0DATA_8822B(x, v)                                            \
	(BIT_CLEAR_TC0DATA_8822B(x) | BIT_TC0DATA_8822B(v))

/* 2 REG_TC1_CTRL_8822B */
#define BIT_TC1INT_EN_8822B BIT(26)
#define BIT_TC1MODE_8822B BIT(25)
#define BIT_TC1EN_8822B BIT(24)

#define BIT_SHIFT_TC1DATA_8822B 0
#define BIT_MASK_TC1DATA_8822B 0xffffff
#define BIT_TC1DATA_8822B(x)                                                   \
	(((x) & BIT_MASK_TC1DATA_8822B) << BIT_SHIFT_TC1DATA_8822B)
#define BITS_TC1DATA_8822B (BIT_MASK_TC1DATA_8822B << BIT_SHIFT_TC1DATA_8822B)
#define BIT_CLEAR_TC1DATA_8822B(x) ((x) & (~BITS_TC1DATA_8822B))
#define BIT_GET_TC1DATA_8822B(x)                                               \
	(((x) >> BIT_SHIFT_TC1DATA_8822B) & BIT_MASK_TC1DATA_8822B)
#define BIT_SET_TC1DATA_8822B(x, v)                                            \
	(BIT_CLEAR_TC1DATA_8822B(x) | BIT_TC1DATA_8822B(v))

/* 2 REG_TC2_CTRL_8822B */
#define BIT_TC2INT_EN_8822B BIT(26)
#define BIT_TC2MODE_8822B BIT(25)
#define BIT_TC2EN_8822B BIT(24)

#define BIT_SHIFT_TC2DATA_8822B 0
#define BIT_MASK_TC2DATA_8822B 0xffffff
#define BIT_TC2DATA_8822B(x)                                                   \
	(((x) & BIT_MASK_TC2DATA_8822B) << BIT_SHIFT_TC2DATA_8822B)
#define BITS_TC2DATA_8822B (BIT_MASK_TC2DATA_8822B << BIT_SHIFT_TC2DATA_8822B)
#define BIT_CLEAR_TC2DATA_8822B(x) ((x) & (~BITS_TC2DATA_8822B))
#define BIT_GET_TC2DATA_8822B(x)                                               \
	(((x) >> BIT_SHIFT_TC2DATA_8822B) & BIT_MASK_TC2DATA_8822B)
#define BIT_SET_TC2DATA_8822B(x, v)                                            \
	(BIT_CLEAR_TC2DATA_8822B(x) | BIT_TC2DATA_8822B(v))

/* 2 REG_TC3_CTRL_8822B */
#define BIT_TC3INT_EN_8822B BIT(26)
#define BIT_TC3MODE_8822B BIT(25)
#define BIT_TC3EN_8822B BIT(24)

#define BIT_SHIFT_TC3DATA_8822B 0
#define BIT_MASK_TC3DATA_8822B 0xffffff
#define BIT_TC3DATA_8822B(x)                                                   \
	(((x) & BIT_MASK_TC3DATA_8822B) << BIT_SHIFT_TC3DATA_8822B)
#define BITS_TC3DATA_8822B (BIT_MASK_TC3DATA_8822B << BIT_SHIFT_TC3DATA_8822B)
#define BIT_CLEAR_TC3DATA_8822B(x) ((x) & (~BITS_TC3DATA_8822B))
#define BIT_GET_TC3DATA_8822B(x)                                               \
	(((x) >> BIT_SHIFT_TC3DATA_8822B) & BIT_MASK_TC3DATA_8822B)
#define BIT_SET_TC3DATA_8822B(x, v)                                            \
	(BIT_CLEAR_TC3DATA_8822B(x) | BIT_TC3DATA_8822B(v))

/* 2 REG_TC4_CTRL_8822B */
#define BIT_TC4INT_EN_8822B BIT(26)
#define BIT_TC4MODE_8822B BIT(25)
#define BIT_TC4EN_8822B BIT(24)

#define BIT_SHIFT_TC4DATA_8822B 0
#define BIT_MASK_TC4DATA_8822B 0xffffff
#define BIT_TC4DATA_8822B(x)                                                   \
	(((x) & BIT_MASK_TC4DATA_8822B) << BIT_SHIFT_TC4DATA_8822B)
#define BITS_TC4DATA_8822B (BIT_MASK_TC4DATA_8822B << BIT_SHIFT_TC4DATA_8822B)
#define BIT_CLEAR_TC4DATA_8822B(x) ((x) & (~BITS_TC4DATA_8822B))
#define BIT_GET_TC4DATA_8822B(x)                                               \
	(((x) >> BIT_SHIFT_TC4DATA_8822B) & BIT_MASK_TC4DATA_8822B)
#define BIT_SET_TC4DATA_8822B(x, v)                                            \
	(BIT_CLEAR_TC4DATA_8822B(x) | BIT_TC4DATA_8822B(v))

/* 2 REG_TCUNIT_BASE_8822B */

#define BIT_SHIFT_TCUNIT_BASE_8822B 0
#define BIT_MASK_TCUNIT_BASE_8822B 0x3fff
#define BIT_TCUNIT_BASE_8822B(x)                                               \
	(((x) & BIT_MASK_TCUNIT_BASE_8822B) << BIT_SHIFT_TCUNIT_BASE_8822B)
#define BITS_TCUNIT_BASE_8822B                                                 \
	(BIT_MASK_TCUNIT_BASE_8822B << BIT_SHIFT_TCUNIT_BASE_8822B)
#define BIT_CLEAR_TCUNIT_BASE_8822B(x) ((x) & (~BITS_TCUNIT_BASE_8822B))
#define BIT_GET_TCUNIT_BASE_8822B(x)                                           \
	(((x) >> BIT_SHIFT_TCUNIT_BASE_8822B) & BIT_MASK_TCUNIT_BASE_8822B)
#define BIT_SET_TCUNIT_BASE_8822B(x, v)                                        \
	(BIT_CLEAR_TCUNIT_BASE_8822B(x) | BIT_TCUNIT_BASE_8822B(v))

/* 2 REG_TC5_CTRL_8822B */
#define BIT_TC5INT_EN_8822B BIT(26)
#define BIT_TC5MODE_8822B BIT(25)
#define BIT_TC5EN_8822B BIT(24)

#define BIT_SHIFT_TC5DATA_8822B 0
#define BIT_MASK_TC5DATA_8822B 0xffffff
#define BIT_TC5DATA_8822B(x)                                                   \
	(((x) & BIT_MASK_TC5DATA_8822B) << BIT_SHIFT_TC5DATA_8822B)
#define BITS_TC5DATA_8822B (BIT_MASK_TC5DATA_8822B << BIT_SHIFT_TC5DATA_8822B)
#define BIT_CLEAR_TC5DATA_8822B(x) ((x) & (~BITS_TC5DATA_8822B))
#define BIT_GET_TC5DATA_8822B(x)                                               \
	(((x) >> BIT_SHIFT_TC5DATA_8822B) & BIT_MASK_TC5DATA_8822B)
#define BIT_SET_TC5DATA_8822B(x, v)                                            \
	(BIT_CLEAR_TC5DATA_8822B(x) | BIT_TC5DATA_8822B(v))

/* 2 REG_TC6_CTRL_8822B */
#define BIT_TC6INT_EN_8822B BIT(26)
#define BIT_TC6MODE_8822B BIT(25)
#define BIT_TC6EN_8822B BIT(24)

#define BIT_SHIFT_TC6DATA_8822B 0
#define BIT_MASK_TC6DATA_8822B 0xffffff
#define BIT_TC6DATA_8822B(x)                                                   \
	(((x) & BIT_MASK_TC6DATA_8822B) << BIT_SHIFT_TC6DATA_8822B)
#define BITS_TC6DATA_8822B (BIT_MASK_TC6DATA_8822B << BIT_SHIFT_TC6DATA_8822B)
#define BIT_CLEAR_TC6DATA_8822B(x) ((x) & (~BITS_TC6DATA_8822B))
#define BIT_GET_TC6DATA_8822B(x)                                               \
	(((x) >> BIT_SHIFT_TC6DATA_8822B) & BIT_MASK_TC6DATA_8822B)
#define BIT_SET_TC6DATA_8822B(x, v)                                            \
	(BIT_CLEAR_TC6DATA_8822B(x) | BIT_TC6DATA_8822B(v))

/* 2 REG_MBIST_FAIL_8822B */

#define BIT_SHIFT_8051_MBIST_FAIL_8822B 26
#define BIT_MASK_8051_MBIST_FAIL_8822B 0x7
#define BIT_8051_MBIST_FAIL_8822B(x)                                           \
	(((x) & BIT_MASK_8051_MBIST_FAIL_8822B)                                \
	 << BIT_SHIFT_8051_MBIST_FAIL_8822B)
#define BITS_8051_MBIST_FAIL_8822B                                             \
	(BIT_MASK_8051_MBIST_FAIL_8822B << BIT_SHIFT_8051_MBIST_FAIL_8822B)
#define BIT_CLEAR_8051_MBIST_FAIL_8822B(x) ((x) & (~BITS_8051_MBIST_FAIL_8822B))
#define BIT_GET_8051_MBIST_FAIL_8822B(x)                                       \
	(((x) >> BIT_SHIFT_8051_MBIST_FAIL_8822B) &                            \
	 BIT_MASK_8051_MBIST_FAIL_8822B)
#define BIT_SET_8051_MBIST_FAIL_8822B(x, v)                                    \
	(BIT_CLEAR_8051_MBIST_FAIL_8822B(x) | BIT_8051_MBIST_FAIL_8822B(v))

#define BIT_SHIFT_USB_MBIST_FAIL_8822B 24
#define BIT_MASK_USB_MBIST_FAIL_8822B 0x3
#define BIT_USB_MBIST_FAIL_8822B(x)                                            \
	(((x) & BIT_MASK_USB_MBIST_FAIL_8822B)                                 \
	 << BIT_SHIFT_USB_MBIST_FAIL_8822B)
#define BITS_USB_MBIST_FAIL_8822B                                              \
	(BIT_MASK_USB_MBIST_FAIL_8822B << BIT_SHIFT_USB_MBIST_FAIL_8822B)
#define BIT_CLEAR_USB_MBIST_FAIL_8822B(x) ((x) & (~BITS_USB_MBIST_FAIL_8822B))
#define BIT_GET_USB_MBIST_FAIL_8822B(x)                                        \
	(((x) >> BIT_SHIFT_USB_MBIST_FAIL_8822B) &                             \
	 BIT_MASK_USB_MBIST_FAIL_8822B)
#define BIT_SET_USB_MBIST_FAIL_8822B(x, v)                                     \
	(BIT_CLEAR_USB_MBIST_FAIL_8822B(x) | BIT_USB_MBIST_FAIL_8822B(v))

#define BIT_SHIFT_PCIE_MBIST_FAIL_8822B 16
#define BIT_MASK_PCIE_MBIST_FAIL_8822B 0x3f
#define BIT_PCIE_MBIST_FAIL_8822B(x)                                           \
	(((x) & BIT_MASK_PCIE_MBIST_FAIL_8822B)                                \
	 << BIT_SHIFT_PCIE_MBIST_FAIL_8822B)
#define BITS_PCIE_MBIST_FAIL_8822B                                             \
	(BIT_MASK_PCIE_MBIST_FAIL_8822B << BIT_SHIFT_PCIE_MBIST_FAIL_8822B)
#define BIT_CLEAR_PCIE_MBIST_FAIL_8822B(x) ((x) & (~BITS_PCIE_MBIST_FAIL_8822B))
#define BIT_GET_PCIE_MBIST_FAIL_8822B(x)                                       \
	(((x) >> BIT_SHIFT_PCIE_MBIST_FAIL_8822B) &                            \
	 BIT_MASK_PCIE_MBIST_FAIL_8822B)
#define BIT_SET_PCIE_MBIST_FAIL_8822B(x, v)                                    \
	(BIT_CLEAR_PCIE_MBIST_FAIL_8822B(x) | BIT_PCIE_MBIST_FAIL_8822B(v))

#define BIT_SHIFT_MAC_MBIST_FAIL_8822B 0
#define BIT_MASK_MAC_MBIST_FAIL_8822B 0xfff
#define BIT_MAC_MBIST_FAIL_8822B(x)                                            \
	(((x) & BIT_MASK_MAC_MBIST_FAIL_8822B)                                 \
	 << BIT_SHIFT_MAC_MBIST_FAIL_8822B)
#define BITS_MAC_MBIST_FAIL_8822B                                              \
	(BIT_MASK_MAC_MBIST_FAIL_8822B << BIT_SHIFT_MAC_MBIST_FAIL_8822B)
#define BIT_CLEAR_MAC_MBIST_FAIL_8822B(x) ((x) & (~BITS_MAC_MBIST_FAIL_8822B))
#define BIT_GET_MAC_MBIST_FAIL_8822B(x)                                        \
	(((x) >> BIT_SHIFT_MAC_MBIST_FAIL_8822B) &                             \
	 BIT_MASK_MAC_MBIST_FAIL_8822B)
#define BIT_SET_MAC_MBIST_FAIL_8822B(x, v)                                     \
	(BIT_CLEAR_MAC_MBIST_FAIL_8822B(x) | BIT_MAC_MBIST_FAIL_8822B(v))

/* 2 REG_MBIST_START_PAUSE_8822B */

#define BIT_SHIFT_8051_MBIST_START_PAUSE_8822B 26
#define BIT_MASK_8051_MBIST_START_PAUSE_8822B 0x7
#define BIT_8051_MBIST_START_PAUSE_8822B(x)                                    \
	(((x) & BIT_MASK_8051_MBIST_START_PAUSE_8822B)                         \
	 << BIT_SHIFT_8051_MBIST_START_PAUSE_8822B)
#define BITS_8051_MBIST_START_PAUSE_8822B                                      \
	(BIT_MASK_8051_MBIST_START_PAUSE_8822B                                 \
	 << BIT_SHIFT_8051_MBIST_START_PAUSE_8822B)
#define BIT_CLEAR_8051_MBIST_START_PAUSE_8822B(x)                              \
	((x) & (~BITS_8051_MBIST_START_PAUSE_8822B))
#define BIT_GET_8051_MBIST_START_PAUSE_8822B(x)                                \
	(((x) >> BIT_SHIFT_8051_MBIST_START_PAUSE_8822B) &                     \
	 BIT_MASK_8051_MBIST_START_PAUSE_8822B)
#define BIT_SET_8051_MBIST_START_PAUSE_8822B(x, v)                             \
	(BIT_CLEAR_8051_MBIST_START_PAUSE_8822B(x) |                           \
	 BIT_8051_MBIST_START_PAUSE_8822B(v))

#define BIT_SHIFT_USB_MBIST_START_PAUSE_8822B 24
#define BIT_MASK_USB_MBIST_START_PAUSE_8822B 0x3
#define BIT_USB_MBIST_START_PAUSE_8822B(x)                                     \
	(((x) & BIT_MASK_USB_MBIST_START_PAUSE_8822B)                          \
	 << BIT_SHIFT_USB_MBIST_START_PAUSE_8822B)
#define BITS_USB_MBIST_START_PAUSE_8822B                                       \
	(BIT_MASK_USB_MBIST_START_PAUSE_8822B                                  \
	 << BIT_SHIFT_USB_MBIST_START_PAUSE_8822B)
#define BIT_CLEAR_USB_MBIST_START_PAUSE_8822B(x)                               \
	((x) & (~BITS_USB_MBIST_START_PAUSE_8822B))
#define BIT_GET_USB_MBIST_START_PAUSE_8822B(x)                                 \
	(((x) >> BIT_SHIFT_USB_MBIST_START_PAUSE_8822B) &                      \
	 BIT_MASK_USB_MBIST_START_PAUSE_8822B)
#define BIT_SET_USB_MBIST_START_PAUSE_8822B(x, v)                              \
	(BIT_CLEAR_USB_MBIST_START_PAUSE_8822B(x) |                            \
	 BIT_USB_MBIST_START_PAUSE_8822B(v))

#define BIT_SHIFT_PCIE_MBIST_START_PAUSE_8822B 16
#define BIT_MASK_PCIE_MBIST_START_PAUSE_8822B 0x3f
#define BIT_PCIE_MBIST_START_PAUSE_8822B(x)                                    \
	(((x) & BIT_MASK_PCIE_MBIST_START_PAUSE_8822B)                         \
	 << BIT_SHIFT_PCIE_MBIST_START_PAUSE_8822B)
#define BITS_PCIE_MBIST_START_PAUSE_8822B                                      \
	(BIT_MASK_PCIE_MBIST_START_PAUSE_8822B                                 \
	 << BIT_SHIFT_PCIE_MBIST_START_PAUSE_8822B)
#define BIT_CLEAR_PCIE_MBIST_START_PAUSE_8822B(x)                              \
	((x) & (~BITS_PCIE_MBIST_START_PAUSE_8822B))
#define BIT_GET_PCIE_MBIST_START_PAUSE_8822B(x)                                \
	(((x) >> BIT_SHIFT_PCIE_MBIST_START_PAUSE_8822B) &                     \
	 BIT_MASK_PCIE_MBIST_START_PAUSE_8822B)
#define BIT_SET_PCIE_MBIST_START_PAUSE_8822B(x, v)                             \
	(BIT_CLEAR_PCIE_MBIST_START_PAUSE_8822B(x) |                           \
	 BIT_PCIE_MBIST_START_PAUSE_8822B(v))

#define BIT_SHIFT_MAC_MBIST_START_PAUSE_8822B 0
#define BIT_MASK_MAC_MBIST_START_PAUSE_8822B 0xfff
#define BIT_MAC_MBIST_START_PAUSE_8822B(x)                                     \
	(((x) & BIT_MASK_MAC_MBIST_START_PAUSE_8822B)                          \
	 << BIT_SHIFT_MAC_MBIST_START_PAUSE_8822B)
#define BITS_MAC_MBIST_START_PAUSE_8822B                                       \
	(BIT_MASK_MAC_MBIST_START_PAUSE_8822B                                  \
	 << BIT_SHIFT_MAC_MBIST_START_PAUSE_8822B)
#define BIT_CLEAR_MAC_MBIST_START_PAUSE_8822B(x)                               \
	((x) & (~BITS_MAC_MBIST_START_PAUSE_8822B))
#define BIT_GET_MAC_MBIST_START_PAUSE_8822B(x)                                 \
	(((x) >> BIT_SHIFT_MAC_MBIST_START_PAUSE_8822B) &                      \
	 BIT_MASK_MAC_MBIST_START_PAUSE_8822B)
#define BIT_SET_MAC_MBIST_START_PAUSE_8822B(x, v)                              \
	(BIT_CLEAR_MAC_MBIST_START_PAUSE_8822B(x) |                            \
	 BIT_MAC_MBIST_START_PAUSE_8822B(v))

/* 2 REG_MBIST_DONE_8822B */

#define BIT_SHIFT_8051_MBIST_DONE_8822B 26
#define BIT_MASK_8051_MBIST_DONE_8822B 0x7
#define BIT_8051_MBIST_DONE_8822B(x)                                           \
	(((x) & BIT_MASK_8051_MBIST_DONE_8822B)                                \
	 << BIT_SHIFT_8051_MBIST_DONE_8822B)
#define BITS_8051_MBIST_DONE_8822B                                             \
	(BIT_MASK_8051_MBIST_DONE_8822B << BIT_SHIFT_8051_MBIST_DONE_8822B)
#define BIT_CLEAR_8051_MBIST_DONE_8822B(x) ((x) & (~BITS_8051_MBIST_DONE_8822B))
#define BIT_GET_8051_MBIST_DONE_8822B(x)                                       \
	(((x) >> BIT_SHIFT_8051_MBIST_DONE_8822B) &                            \
	 BIT_MASK_8051_MBIST_DONE_8822B)
#define BIT_SET_8051_MBIST_DONE_8822B(x, v)                                    \
	(BIT_CLEAR_8051_MBIST_DONE_8822B(x) | BIT_8051_MBIST_DONE_8822B(v))

#define BIT_SHIFT_USB_MBIST_DONE_8822B 24
#define BIT_MASK_USB_MBIST_DONE_8822B 0x3
#define BIT_USB_MBIST_DONE_8822B(x)                                            \
	(((x) & BIT_MASK_USB_MBIST_DONE_8822B)                                 \
	 << BIT_SHIFT_USB_MBIST_DONE_8822B)
#define BITS_USB_MBIST_DONE_8822B                                              \
	(BIT_MASK_USB_MBIST_DONE_8822B << BIT_SHIFT_USB_MBIST_DONE_8822B)
#define BIT_CLEAR_USB_MBIST_DONE_8822B(x) ((x) & (~BITS_USB_MBIST_DONE_8822B))
#define BIT_GET_USB_MBIST_DONE_8822B(x)                                        \
	(((x) >> BIT_SHIFT_USB_MBIST_DONE_8822B) &                             \
	 BIT_MASK_USB_MBIST_DONE_8822B)
#define BIT_SET_USB_MBIST_DONE_8822B(x, v)                                     \
	(BIT_CLEAR_USB_MBIST_DONE_8822B(x) | BIT_USB_MBIST_DONE_8822B(v))

#define BIT_SHIFT_PCIE_MBIST_DONE_8822B 16
#define BIT_MASK_PCIE_MBIST_DONE_8822B 0x3f
#define BIT_PCIE_MBIST_DONE_8822B(x)                                           \
	(((x) & BIT_MASK_PCIE_MBIST_DONE_8822B)                                \
	 << BIT_SHIFT_PCIE_MBIST_DONE_8822B)
#define BITS_PCIE_MBIST_DONE_8822B                                             \
	(BIT_MASK_PCIE_MBIST_DONE_8822B << BIT_SHIFT_PCIE_MBIST_DONE_8822B)
#define BIT_CLEAR_PCIE_MBIST_DONE_8822B(x) ((x) & (~BITS_PCIE_MBIST_DONE_8822B))
#define BIT_GET_PCIE_MBIST_DONE_8822B(x)                                       \
	(((x) >> BIT_SHIFT_PCIE_MBIST_DONE_8822B) &                            \
	 BIT_MASK_PCIE_MBIST_DONE_8822B)
#define BIT_SET_PCIE_MBIST_DONE_8822B(x, v)                                    \
	(BIT_CLEAR_PCIE_MBIST_DONE_8822B(x) | BIT_PCIE_MBIST_DONE_8822B(v))

#define BIT_SHIFT_MAC_MBIST_DONE_8822B 0
#define BIT_MASK_MAC_MBIST_DONE_8822B 0xfff
#define BIT_MAC_MBIST_DONE_8822B(x)                                            \
	(((x) & BIT_MASK_MAC_MBIST_DONE_8822B)                                 \
	 << BIT_SHIFT_MAC_MBIST_DONE_8822B)
#define BITS_MAC_MBIST_DONE_8822B                                              \
	(BIT_MASK_MAC_MBIST_DONE_8822B << BIT_SHIFT_MAC_MBIST_DONE_8822B)
#define BIT_CLEAR_MAC_MBIST_DONE_8822B(x) ((x) & (~BITS_MAC_MBIST_DONE_8822B))
#define BIT_GET_MAC_MBIST_DONE_8822B(x)                                        \
	(((x) >> BIT_SHIFT_MAC_MBIST_DONE_8822B) &                             \
	 BIT_MASK_MAC_MBIST_DONE_8822B)
#define BIT_SET_MAC_MBIST_DONE_8822B(x, v)                                     \
	(BIT_CLEAR_MAC_MBIST_DONE_8822B(x) | BIT_MAC_MBIST_DONE_8822B(v))

/* 2 REG_MBIST_FAIL_NRML_8822B */

#define BIT_SHIFT_MBIST_FAIL_NRML_8822B 0
#define BIT_MASK_MBIST_FAIL_NRML_8822B 0xffffffffL
#define BIT_MBIST_FAIL_NRML_8822B(x)                                           \
	(((x) & BIT_MASK_MBIST_FAIL_NRML_8822B)                                \
	 << BIT_SHIFT_MBIST_FAIL_NRML_8822B)
#define BITS_MBIST_FAIL_NRML_8822B                                             \
	(BIT_MASK_MBIST_FAIL_NRML_8822B << BIT_SHIFT_MBIST_FAIL_NRML_8822B)
#define BIT_CLEAR_MBIST_FAIL_NRML_8822B(x) ((x) & (~BITS_MBIST_FAIL_NRML_8822B))
#define BIT_GET_MBIST_FAIL_NRML_8822B(x)                                       \
	(((x) >> BIT_SHIFT_MBIST_FAIL_NRML_8822B) &                            \
	 BIT_MASK_MBIST_FAIL_NRML_8822B)
#define BIT_SET_MBIST_FAIL_NRML_8822B(x, v)                                    \
	(BIT_CLEAR_MBIST_FAIL_NRML_8822B(x) | BIT_MBIST_FAIL_NRML_8822B(v))

/* 2 REG_AES_DECRPT_DATA_8822B */

#define BIT_SHIFT_IPS_CFG_ADDR_8822B 0
#define BIT_MASK_IPS_CFG_ADDR_8822B 0xff
#define BIT_IPS_CFG_ADDR_8822B(x)                                              \
	(((x) & BIT_MASK_IPS_CFG_ADDR_8822B) << BIT_SHIFT_IPS_CFG_ADDR_8822B)
#define BITS_IPS_CFG_ADDR_8822B                                                \
	(BIT_MASK_IPS_CFG_ADDR_8822B << BIT_SHIFT_IPS_CFG_ADDR_8822B)
#define BIT_CLEAR_IPS_CFG_ADDR_8822B(x) ((x) & (~BITS_IPS_CFG_ADDR_8822B))
#define BIT_GET_IPS_CFG_ADDR_8822B(x)                                          \
	(((x) >> BIT_SHIFT_IPS_CFG_ADDR_8822B) & BIT_MASK_IPS_CFG_ADDR_8822B)
#define BIT_SET_IPS_CFG_ADDR_8822B(x, v)                                       \
	(BIT_CLEAR_IPS_CFG_ADDR_8822B(x) | BIT_IPS_CFG_ADDR_8822B(v))

/* 2 REG_AES_DECRPT_CFG_8822B */

#define BIT_SHIFT_IPS_CFG_DATA_8822B 0
#define BIT_MASK_IPS_CFG_DATA_8822B 0xffffffffL
#define BIT_IPS_CFG_DATA_8822B(x)                                              \
	(((x) & BIT_MASK_IPS_CFG_DATA_8822B) << BIT_SHIFT_IPS_CFG_DATA_8822B)
#define BITS_IPS_CFG_DATA_8822B                                                \
	(BIT_MASK_IPS_CFG_DATA_8822B << BIT_SHIFT_IPS_CFG_DATA_8822B)
#define BIT_CLEAR_IPS_CFG_DATA_8822B(x) ((x) & (~BITS_IPS_CFG_DATA_8822B))
#define BIT_GET_IPS_CFG_DATA_8822B(x)                                          \
	(((x) >> BIT_SHIFT_IPS_CFG_DATA_8822B) & BIT_MASK_IPS_CFG_DATA_8822B)
#define BIT_SET_IPS_CFG_DATA_8822B(x, v)                                       \
	(BIT_CLEAR_IPS_CFG_DATA_8822B(x) | BIT_IPS_CFG_DATA_8822B(v))

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_TMETER_8822B */
#define BIT_TEMP_VALID_8822B BIT(31)

#define BIT_SHIFT_TEMP_VALUE_8822B 24
#define BIT_MASK_TEMP_VALUE_8822B 0x3f
#define BIT_TEMP_VALUE_8822B(x)                                                \
	(((x) & BIT_MASK_TEMP_VALUE_8822B) << BIT_SHIFT_TEMP_VALUE_8822B)
#define BITS_TEMP_VALUE_8822B                                                  \
	(BIT_MASK_TEMP_VALUE_8822B << BIT_SHIFT_TEMP_VALUE_8822B)
#define BIT_CLEAR_TEMP_VALUE_8822B(x) ((x) & (~BITS_TEMP_VALUE_8822B))
#define BIT_GET_TEMP_VALUE_8822B(x)                                            \
	(((x) >> BIT_SHIFT_TEMP_VALUE_8822B) & BIT_MASK_TEMP_VALUE_8822B)
#define BIT_SET_TEMP_VALUE_8822B(x, v)                                         \
	(BIT_CLEAR_TEMP_VALUE_8822B(x) | BIT_TEMP_VALUE_8822B(v))

#define BIT_SHIFT_REG_TMETER_TIMER_8822B 8
#define BIT_MASK_REG_TMETER_TIMER_8822B 0xfff
#define BIT_REG_TMETER_TIMER_8822B(x)                                          \
	(((x) & BIT_MASK_REG_TMETER_TIMER_8822B)                               \
	 << BIT_SHIFT_REG_TMETER_TIMER_8822B)
#define BITS_REG_TMETER_TIMER_8822B                                            \
	(BIT_MASK_REG_TMETER_TIMER_8822B << BIT_SHIFT_REG_TMETER_TIMER_8822B)
#define BIT_CLEAR_REG_TMETER_TIMER_8822B(x)                                    \
	((x) & (~BITS_REG_TMETER_TIMER_8822B))
#define BIT_GET_REG_TMETER_TIMER_8822B(x)                                      \
	(((x) >> BIT_SHIFT_REG_TMETER_TIMER_8822B) &                           \
	 BIT_MASK_REG_TMETER_TIMER_8822B)
#define BIT_SET_REG_TMETER_TIMER_8822B(x, v)                                   \
	(BIT_CLEAR_REG_TMETER_TIMER_8822B(x) | BIT_REG_TMETER_TIMER_8822B(v))

#define BIT_SHIFT_REG_TEMP_DELTA_8822B 2
#define BIT_MASK_REG_TEMP_DELTA_8822B 0x3f
#define BIT_REG_TEMP_DELTA_8822B(x)                                            \
	(((x) & BIT_MASK_REG_TEMP_DELTA_8822B)                                 \
	 << BIT_SHIFT_REG_TEMP_DELTA_8822B)
#define BITS_REG_TEMP_DELTA_8822B                                              \
	(BIT_MASK_REG_TEMP_DELTA_8822B << BIT_SHIFT_REG_TEMP_DELTA_8822B)
#define BIT_CLEAR_REG_TEMP_DELTA_8822B(x) ((x) & (~BITS_REG_TEMP_DELTA_8822B))
#define BIT_GET_REG_TEMP_DELTA_8822B(x)                                        \
	(((x) >> BIT_SHIFT_REG_TEMP_DELTA_8822B) &                             \
	 BIT_MASK_REG_TEMP_DELTA_8822B)
#define BIT_SET_REG_TEMP_DELTA_8822B(x, v)                                     \
	(BIT_CLEAR_REG_TEMP_DELTA_8822B(x) | BIT_REG_TEMP_DELTA_8822B(v))

#define BIT_REG_TMETER_EN_8822B BIT(0)

/* 2 REG_OSC_32K_CTRL_8822B */

#define BIT_SHIFT_OSC_32K_CLKGEN_0_8822B 16
#define BIT_MASK_OSC_32K_CLKGEN_0_8822B 0xffff
#define BIT_OSC_32K_CLKGEN_0_8822B(x)                                          \
	(((x) & BIT_MASK_OSC_32K_CLKGEN_0_8822B)                               \
	 << BIT_SHIFT_OSC_32K_CLKGEN_0_8822B)
#define BITS_OSC_32K_CLKGEN_0_8822B                                            \
	(BIT_MASK_OSC_32K_CLKGEN_0_8822B << BIT_SHIFT_OSC_32K_CLKGEN_0_8822B)
#define BIT_CLEAR_OSC_32K_CLKGEN_0_8822B(x)                                    \
	((x) & (~BITS_OSC_32K_CLKGEN_0_8822B))
#define BIT_GET_OSC_32K_CLKGEN_0_8822B(x)                                      \
	(((x) >> BIT_SHIFT_OSC_32K_CLKGEN_0_8822B) &                           \
	 BIT_MASK_OSC_32K_CLKGEN_0_8822B)
#define BIT_SET_OSC_32K_CLKGEN_0_8822B(x, v)                                   \
	(BIT_CLEAR_OSC_32K_CLKGEN_0_8822B(x) | BIT_OSC_32K_CLKGEN_0_8822B(v))

#define BIT_SHIFT_OSC_32K_RES_COMP_8822B 4
#define BIT_MASK_OSC_32K_RES_COMP_8822B 0x3
#define BIT_OSC_32K_RES_COMP_8822B(x)                                          \
	(((x) & BIT_MASK_OSC_32K_RES_COMP_8822B)                               \
	 << BIT_SHIFT_OSC_32K_RES_COMP_8822B)
#define BITS_OSC_32K_RES_COMP_8822B                                            \
	(BIT_MASK_OSC_32K_RES_COMP_8822B << BIT_SHIFT_OSC_32K_RES_COMP_8822B)
#define BIT_CLEAR_OSC_32K_RES_COMP_8822B(x)                                    \
	((x) & (~BITS_OSC_32K_RES_COMP_8822B))
#define BIT_GET_OSC_32K_RES_COMP_8822B(x)                                      \
	(((x) >> BIT_SHIFT_OSC_32K_RES_COMP_8822B) &                           \
	 BIT_MASK_OSC_32K_RES_COMP_8822B)
#define BIT_SET_OSC_32K_RES_COMP_8822B(x, v)                                   \
	(BIT_CLEAR_OSC_32K_RES_COMP_8822B(x) | BIT_OSC_32K_RES_COMP_8822B(v))

#define BIT_OSC_32K_OUT_SEL_8822B BIT(3)
#define BIT_ISO_WL_2_OSC_32K_8822B BIT(1)
#define BIT_POW_CKGEN_8822B BIT(0)

/* 2 REG_32K_CAL_REG1_8822B */
#define BIT_CAL_32K_REG_WR_8822B BIT(31)
#define BIT_CAL_32K_DBG_SEL_8822B BIT(22)

#define BIT_SHIFT_CAL_32K_REG_ADDR_8822B 16
#define BIT_MASK_CAL_32K_REG_ADDR_8822B 0x3f
#define BIT_CAL_32K_REG_ADDR_8822B(x)                                          \
	(((x) & BIT_MASK_CAL_32K_REG_ADDR_8822B)                               \
	 << BIT_SHIFT_CAL_32K_REG_ADDR_8822B)
#define BITS_CAL_32K_REG_ADDR_8822B                                            \
	(BIT_MASK_CAL_32K_REG_ADDR_8822B << BIT_SHIFT_CAL_32K_REG_ADDR_8822B)
#define BIT_CLEAR_CAL_32K_REG_ADDR_8822B(x)                                    \
	((x) & (~BITS_CAL_32K_REG_ADDR_8822B))
#define BIT_GET_CAL_32K_REG_ADDR_8822B(x)                                      \
	(((x) >> BIT_SHIFT_CAL_32K_REG_ADDR_8822B) &                           \
	 BIT_MASK_CAL_32K_REG_ADDR_8822B)
#define BIT_SET_CAL_32K_REG_ADDR_8822B(x, v)                                   \
	(BIT_CLEAR_CAL_32K_REG_ADDR_8822B(x) | BIT_CAL_32K_REG_ADDR_8822B(v))

#define BIT_SHIFT_CAL_32K_REG_DATA_8822B 0
#define BIT_MASK_CAL_32K_REG_DATA_8822B 0xffff
#define BIT_CAL_32K_REG_DATA_8822B(x)                                          \
	(((x) & BIT_MASK_CAL_32K_REG_DATA_8822B)                               \
	 << BIT_SHIFT_CAL_32K_REG_DATA_8822B)
#define BITS_CAL_32K_REG_DATA_8822B                                            \
	(BIT_MASK_CAL_32K_REG_DATA_8822B << BIT_SHIFT_CAL_32K_REG_DATA_8822B)
#define BIT_CLEAR_CAL_32K_REG_DATA_8822B(x)                                    \
	((x) & (~BITS_CAL_32K_REG_DATA_8822B))
#define BIT_GET_CAL_32K_REG_DATA_8822B(x)                                      \
	(((x) >> BIT_SHIFT_CAL_32K_REG_DATA_8822B) &                           \
	 BIT_MASK_CAL_32K_REG_DATA_8822B)
#define BIT_SET_CAL_32K_REG_DATA_8822B(x, v)                                   \
	(BIT_CLEAR_CAL_32K_REG_DATA_8822B(x) | BIT_CAL_32K_REG_DATA_8822B(v))

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_C2HEVT_8822B */

#define BIT_SHIFT_C2HEVT_MSG_V1_8822B 0
#define BIT_MASK_C2HEVT_MSG_V1_8822B 0xffffffffL
#define BIT_C2HEVT_MSG_V1_8822B(x)                                             \
	(((x) & BIT_MASK_C2HEVT_MSG_V1_8822B) << BIT_SHIFT_C2HEVT_MSG_V1_8822B)
#define BITS_C2HEVT_MSG_V1_8822B                                               \
	(BIT_MASK_C2HEVT_MSG_V1_8822B << BIT_SHIFT_C2HEVT_MSG_V1_8822B)
#define BIT_CLEAR_C2HEVT_MSG_V1_8822B(x) ((x) & (~BITS_C2HEVT_MSG_V1_8822B))
#define BIT_GET_C2HEVT_MSG_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_C2HEVT_MSG_V1_8822B) & BIT_MASK_C2HEVT_MSG_V1_8822B)
#define BIT_SET_C2HEVT_MSG_V1_8822B(x, v)                                      \
	(BIT_CLEAR_C2HEVT_MSG_V1_8822B(x) | BIT_C2HEVT_MSG_V1_8822B(v))

/* 2 REG_C2HEVT_1_8822B */

#define BIT_SHIFT_C2HEVT_MSG_1_8822B 0
#define BIT_MASK_C2HEVT_MSG_1_8822B 0xffffffffL
#define BIT_C2HEVT_MSG_1_8822B(x)                                              \
	(((x) & BIT_MASK_C2HEVT_MSG_1_8822B) << BIT_SHIFT_C2HEVT_MSG_1_8822B)
#define BITS_C2HEVT_MSG_1_8822B                                                \
	(BIT_MASK_C2HEVT_MSG_1_8822B << BIT_SHIFT_C2HEVT_MSG_1_8822B)
#define BIT_CLEAR_C2HEVT_MSG_1_8822B(x) ((x) & (~BITS_C2HEVT_MSG_1_8822B))
#define BIT_GET_C2HEVT_MSG_1_8822B(x)                                          \
	(((x) >> BIT_SHIFT_C2HEVT_MSG_1_8822B) & BIT_MASK_C2HEVT_MSG_1_8822B)
#define BIT_SET_C2HEVT_MSG_1_8822B(x, v)                                       \
	(BIT_CLEAR_C2HEVT_MSG_1_8822B(x) | BIT_C2HEVT_MSG_1_8822B(v))

/* 2 REG_C2HEVT_2_8822B */

#define BIT_SHIFT_C2HEVT_MSG_2_8822B 0
#define BIT_MASK_C2HEVT_MSG_2_8822B 0xffffffffL
#define BIT_C2HEVT_MSG_2_8822B(x)                                              \
	(((x) & BIT_MASK_C2HEVT_MSG_2_8822B) << BIT_SHIFT_C2HEVT_MSG_2_8822B)
#define BITS_C2HEVT_MSG_2_8822B                                                \
	(BIT_MASK_C2HEVT_MSG_2_8822B << BIT_SHIFT_C2HEVT_MSG_2_8822B)
#define BIT_CLEAR_C2HEVT_MSG_2_8822B(x) ((x) & (~BITS_C2HEVT_MSG_2_8822B))
#define BIT_GET_C2HEVT_MSG_2_8822B(x)                                          \
	(((x) >> BIT_SHIFT_C2HEVT_MSG_2_8822B) & BIT_MASK_C2HEVT_MSG_2_8822B)
#define BIT_SET_C2HEVT_MSG_2_8822B(x, v)                                       \
	(BIT_CLEAR_C2HEVT_MSG_2_8822B(x) | BIT_C2HEVT_MSG_2_8822B(v))

/* 2 REG_C2HEVT_3_8822B */

#define BIT_SHIFT_C2HEVT_MSG_3_8822B 0
#define BIT_MASK_C2HEVT_MSG_3_8822B 0xffffffffL
#define BIT_C2HEVT_MSG_3_8822B(x)                                              \
	(((x) & BIT_MASK_C2HEVT_MSG_3_8822B) << BIT_SHIFT_C2HEVT_MSG_3_8822B)
#define BITS_C2HEVT_MSG_3_8822B                                                \
	(BIT_MASK_C2HEVT_MSG_3_8822B << BIT_SHIFT_C2HEVT_MSG_3_8822B)
#define BIT_CLEAR_C2HEVT_MSG_3_8822B(x) ((x) & (~BITS_C2HEVT_MSG_3_8822B))
#define BIT_GET_C2HEVT_MSG_3_8822B(x)                                          \
	(((x) >> BIT_SHIFT_C2HEVT_MSG_3_8822B) & BIT_MASK_C2HEVT_MSG_3_8822B)
#define BIT_SET_C2HEVT_MSG_3_8822B(x, v)                                       \
	(BIT_CLEAR_C2HEVT_MSG_3_8822B(x) | BIT_C2HEVT_MSG_3_8822B(v))

/* 2 REG_SW_DEFINED_PAGE1_8822B */

#define BIT_SHIFT_SW_DEFINED_PAGE1_8822B 0
#define BIT_MASK_SW_DEFINED_PAGE1_8822B 0xffffffffffffffffL
#define BIT_SW_DEFINED_PAGE1_8822B(x)                                          \
	(((x) & BIT_MASK_SW_DEFINED_PAGE1_8822B)                               \
	 << BIT_SHIFT_SW_DEFINED_PAGE1_8822B)
#define BITS_SW_DEFINED_PAGE1_8822B                                            \
	(BIT_MASK_SW_DEFINED_PAGE1_8822B << BIT_SHIFT_SW_DEFINED_PAGE1_8822B)
#define BIT_CLEAR_SW_DEFINED_PAGE1_8822B(x)                                    \
	((x) & (~BITS_SW_DEFINED_PAGE1_8822B))
#define BIT_GET_SW_DEFINED_PAGE1_8822B(x)                                      \
	(((x) >> BIT_SHIFT_SW_DEFINED_PAGE1_8822B) &                           \
	 BIT_MASK_SW_DEFINED_PAGE1_8822B)
#define BIT_SET_SW_DEFINED_PAGE1_8822B(x, v)                                   \
	(BIT_CLEAR_SW_DEFINED_PAGE1_8822B(x) | BIT_SW_DEFINED_PAGE1_8822B(v))

/* 2 REG_MCUTST_I_8822B */

#define BIT_SHIFT_MCUDMSG_I_8822B 0
#define BIT_MASK_MCUDMSG_I_8822B 0xffffffffL
#define BIT_MCUDMSG_I_8822B(x)                                                 \
	(((x) & BIT_MASK_MCUDMSG_I_8822B) << BIT_SHIFT_MCUDMSG_I_8822B)
#define BITS_MCUDMSG_I_8822B                                                   \
	(BIT_MASK_MCUDMSG_I_8822B << BIT_SHIFT_MCUDMSG_I_8822B)
#define BIT_CLEAR_MCUDMSG_I_8822B(x) ((x) & (~BITS_MCUDMSG_I_8822B))
#define BIT_GET_MCUDMSG_I_8822B(x)                                             \
	(((x) >> BIT_SHIFT_MCUDMSG_I_8822B) & BIT_MASK_MCUDMSG_I_8822B)
#define BIT_SET_MCUDMSG_I_8822B(x, v)                                          \
	(BIT_CLEAR_MCUDMSG_I_8822B(x) | BIT_MCUDMSG_I_8822B(v))

/* 2 REG_MCUTST_II_8822B */

#define BIT_SHIFT_MCUDMSG_II_8822B 0
#define BIT_MASK_MCUDMSG_II_8822B 0xffffffffL
#define BIT_MCUDMSG_II_8822B(x)                                                \
	(((x) & BIT_MASK_MCUDMSG_II_8822B) << BIT_SHIFT_MCUDMSG_II_8822B)
#define BITS_MCUDMSG_II_8822B                                                  \
	(BIT_MASK_MCUDMSG_II_8822B << BIT_SHIFT_MCUDMSG_II_8822B)
#define BIT_CLEAR_MCUDMSG_II_8822B(x) ((x) & (~BITS_MCUDMSG_II_8822B))
#define BIT_GET_MCUDMSG_II_8822B(x)                                            \
	(((x) >> BIT_SHIFT_MCUDMSG_II_8822B) & BIT_MASK_MCUDMSG_II_8822B)
#define BIT_SET_MCUDMSG_II_8822B(x, v)                                         \
	(BIT_CLEAR_MCUDMSG_II_8822B(x) | BIT_MCUDMSG_II_8822B(v))

/* 2 REG_FMETHR_8822B */
#define BIT_FMSG_INT_8822B BIT(31)

#define BIT_SHIFT_FW_MSG_8822B 0
#define BIT_MASK_FW_MSG_8822B 0xffffffffL
#define BIT_FW_MSG_8822B(x)                                                    \
	(((x) & BIT_MASK_FW_MSG_8822B) << BIT_SHIFT_FW_MSG_8822B)
#define BITS_FW_MSG_8822B (BIT_MASK_FW_MSG_8822B << BIT_SHIFT_FW_MSG_8822B)
#define BIT_CLEAR_FW_MSG_8822B(x) ((x) & (~BITS_FW_MSG_8822B))
#define BIT_GET_FW_MSG_8822B(x)                                                \
	(((x) >> BIT_SHIFT_FW_MSG_8822B) & BIT_MASK_FW_MSG_8822B)
#define BIT_SET_FW_MSG_8822B(x, v)                                             \
	(BIT_CLEAR_FW_MSG_8822B(x) | BIT_FW_MSG_8822B(v))

/* 2 REG_HMETFR_8822B */

#define BIT_SHIFT_HRCV_MSG_8822B 24
#define BIT_MASK_HRCV_MSG_8822B 0xff
#define BIT_HRCV_MSG_8822B(x)                                                  \
	(((x) & BIT_MASK_HRCV_MSG_8822B) << BIT_SHIFT_HRCV_MSG_8822B)
#define BITS_HRCV_MSG_8822B                                                    \
	(BIT_MASK_HRCV_MSG_8822B << BIT_SHIFT_HRCV_MSG_8822B)
#define BIT_CLEAR_HRCV_MSG_8822B(x) ((x) & (~BITS_HRCV_MSG_8822B))
#define BIT_GET_HRCV_MSG_8822B(x)                                              \
	(((x) >> BIT_SHIFT_HRCV_MSG_8822B) & BIT_MASK_HRCV_MSG_8822B)
#define BIT_SET_HRCV_MSG_8822B(x, v)                                           \
	(BIT_CLEAR_HRCV_MSG_8822B(x) | BIT_HRCV_MSG_8822B(v))

#define BIT_INT_BOX3_8822B BIT(3)
#define BIT_INT_BOX2_8822B BIT(2)
#define BIT_INT_BOX1_8822B BIT(1)
#define BIT_INT_BOX0_8822B BIT(0)

/* 2 REG_HMEBOX0_8822B */

#define BIT_SHIFT_HOST_MSG_0_8822B 0
#define BIT_MASK_HOST_MSG_0_8822B 0xffffffffL
#define BIT_HOST_MSG_0_8822B(x)                                                \
	(((x) & BIT_MASK_HOST_MSG_0_8822B) << BIT_SHIFT_HOST_MSG_0_8822B)
#define BITS_HOST_MSG_0_8822B                                                  \
	(BIT_MASK_HOST_MSG_0_8822B << BIT_SHIFT_HOST_MSG_0_8822B)
#define BIT_CLEAR_HOST_MSG_0_8822B(x) ((x) & (~BITS_HOST_MSG_0_8822B))
#define BIT_GET_HOST_MSG_0_8822B(x)                                            \
	(((x) >> BIT_SHIFT_HOST_MSG_0_8822B) & BIT_MASK_HOST_MSG_0_8822B)
#define BIT_SET_HOST_MSG_0_8822B(x, v)                                         \
	(BIT_CLEAR_HOST_MSG_0_8822B(x) | BIT_HOST_MSG_0_8822B(v))

/* 2 REG_HMEBOX1_8822B */

#define BIT_SHIFT_HOST_MSG_1_8822B 0
#define BIT_MASK_HOST_MSG_1_8822B 0xffffffffL
#define BIT_HOST_MSG_1_8822B(x)                                                \
	(((x) & BIT_MASK_HOST_MSG_1_8822B) << BIT_SHIFT_HOST_MSG_1_8822B)
#define BITS_HOST_MSG_1_8822B                                                  \
	(BIT_MASK_HOST_MSG_1_8822B << BIT_SHIFT_HOST_MSG_1_8822B)
#define BIT_CLEAR_HOST_MSG_1_8822B(x) ((x) & (~BITS_HOST_MSG_1_8822B))
#define BIT_GET_HOST_MSG_1_8822B(x)                                            \
	(((x) >> BIT_SHIFT_HOST_MSG_1_8822B) & BIT_MASK_HOST_MSG_1_8822B)
#define BIT_SET_HOST_MSG_1_8822B(x, v)                                         \
	(BIT_CLEAR_HOST_MSG_1_8822B(x) | BIT_HOST_MSG_1_8822B(v))

/* 2 REG_HMEBOX2_8822B */

#define BIT_SHIFT_HOST_MSG_2_8822B 0
#define BIT_MASK_HOST_MSG_2_8822B 0xffffffffL
#define BIT_HOST_MSG_2_8822B(x)                                                \
	(((x) & BIT_MASK_HOST_MSG_2_8822B) << BIT_SHIFT_HOST_MSG_2_8822B)
#define BITS_HOST_MSG_2_8822B                                                  \
	(BIT_MASK_HOST_MSG_2_8822B << BIT_SHIFT_HOST_MSG_2_8822B)
#define BIT_CLEAR_HOST_MSG_2_8822B(x) ((x) & (~BITS_HOST_MSG_2_8822B))
#define BIT_GET_HOST_MSG_2_8822B(x)                                            \
	(((x) >> BIT_SHIFT_HOST_MSG_2_8822B) & BIT_MASK_HOST_MSG_2_8822B)
#define BIT_SET_HOST_MSG_2_8822B(x, v)                                         \
	(BIT_CLEAR_HOST_MSG_2_8822B(x) | BIT_HOST_MSG_2_8822B(v))

/* 2 REG_HMEBOX3_8822B */

#define BIT_SHIFT_HOST_MSG_3_8822B 0
#define BIT_MASK_HOST_MSG_3_8822B 0xffffffffL
#define BIT_HOST_MSG_3_8822B(x)                                                \
	(((x) & BIT_MASK_HOST_MSG_3_8822B) << BIT_SHIFT_HOST_MSG_3_8822B)
#define BITS_HOST_MSG_3_8822B                                                  \
	(BIT_MASK_HOST_MSG_3_8822B << BIT_SHIFT_HOST_MSG_3_8822B)
#define BIT_CLEAR_HOST_MSG_3_8822B(x) ((x) & (~BITS_HOST_MSG_3_8822B))
#define BIT_GET_HOST_MSG_3_8822B(x)                                            \
	(((x) >> BIT_SHIFT_HOST_MSG_3_8822B) & BIT_MASK_HOST_MSG_3_8822B)
#define BIT_SET_HOST_MSG_3_8822B(x, v)                                         \
	(BIT_CLEAR_HOST_MSG_3_8822B(x) | BIT_HOST_MSG_3_8822B(v))

/* 2 REG_LLT_INIT_8822B */

#define BIT_SHIFT_LLTE_RWM_8822B 30
#define BIT_MASK_LLTE_RWM_8822B 0x3
#define BIT_LLTE_RWM_8822B(x)                                                  \
	(((x) & BIT_MASK_LLTE_RWM_8822B) << BIT_SHIFT_LLTE_RWM_8822B)
#define BITS_LLTE_RWM_8822B                                                    \
	(BIT_MASK_LLTE_RWM_8822B << BIT_SHIFT_LLTE_RWM_8822B)
#define BIT_CLEAR_LLTE_RWM_8822B(x) ((x) & (~BITS_LLTE_RWM_8822B))
#define BIT_GET_LLTE_RWM_8822B(x)                                              \
	(((x) >> BIT_SHIFT_LLTE_RWM_8822B) & BIT_MASK_LLTE_RWM_8822B)
#define BIT_SET_LLTE_RWM_8822B(x, v)                                           \
	(BIT_CLEAR_LLTE_RWM_8822B(x) | BIT_LLTE_RWM_8822B(v))

#define BIT_SHIFT_LLTINI_PDATA_V1_8822B 16
#define BIT_MASK_LLTINI_PDATA_V1_8822B 0xfff
#define BIT_LLTINI_PDATA_V1_8822B(x)                                           \
	(((x) & BIT_MASK_LLTINI_PDATA_V1_8822B)                                \
	 << BIT_SHIFT_LLTINI_PDATA_V1_8822B)
#define BITS_LLTINI_PDATA_V1_8822B                                             \
	(BIT_MASK_LLTINI_PDATA_V1_8822B << BIT_SHIFT_LLTINI_PDATA_V1_8822B)
#define BIT_CLEAR_LLTINI_PDATA_V1_8822B(x) ((x) & (~BITS_LLTINI_PDATA_V1_8822B))
#define BIT_GET_LLTINI_PDATA_V1_8822B(x)                                       \
	(((x) >> BIT_SHIFT_LLTINI_PDATA_V1_8822B) &                            \
	 BIT_MASK_LLTINI_PDATA_V1_8822B)
#define BIT_SET_LLTINI_PDATA_V1_8822B(x, v)                                    \
	(BIT_CLEAR_LLTINI_PDATA_V1_8822B(x) | BIT_LLTINI_PDATA_V1_8822B(v))

#define BIT_SHIFT_LLTINI_HDATA_V1_8822B 0
#define BIT_MASK_LLTINI_HDATA_V1_8822B 0xfff
#define BIT_LLTINI_HDATA_V1_8822B(x)                                           \
	(((x) & BIT_MASK_LLTINI_HDATA_V1_8822B)                                \
	 << BIT_SHIFT_LLTINI_HDATA_V1_8822B)
#define BITS_LLTINI_HDATA_V1_8822B                                             \
	(BIT_MASK_LLTINI_HDATA_V1_8822B << BIT_SHIFT_LLTINI_HDATA_V1_8822B)
#define BIT_CLEAR_LLTINI_HDATA_V1_8822B(x) ((x) & (~BITS_LLTINI_HDATA_V1_8822B))
#define BIT_GET_LLTINI_HDATA_V1_8822B(x)                                       \
	(((x) >> BIT_SHIFT_LLTINI_HDATA_V1_8822B) &                            \
	 BIT_MASK_LLTINI_HDATA_V1_8822B)
#define BIT_SET_LLTINI_HDATA_V1_8822B(x, v)                                    \
	(BIT_CLEAR_LLTINI_HDATA_V1_8822B(x) | BIT_LLTINI_HDATA_V1_8822B(v))

/* 2 REG_LLT_INIT_ADDR_8822B */

#define BIT_SHIFT_LLTINI_ADDR_V1_8822B 0
#define BIT_MASK_LLTINI_ADDR_V1_8822B 0xfff
#define BIT_LLTINI_ADDR_V1_8822B(x)                                            \
	(((x) & BIT_MASK_LLTINI_ADDR_V1_8822B)                                 \
	 << BIT_SHIFT_LLTINI_ADDR_V1_8822B)
#define BITS_LLTINI_ADDR_V1_8822B                                              \
	(BIT_MASK_LLTINI_ADDR_V1_8822B << BIT_SHIFT_LLTINI_ADDR_V1_8822B)
#define BIT_CLEAR_LLTINI_ADDR_V1_8822B(x) ((x) & (~BITS_LLTINI_ADDR_V1_8822B))
#define BIT_GET_LLTINI_ADDR_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_LLTINI_ADDR_V1_8822B) &                             \
	 BIT_MASK_LLTINI_ADDR_V1_8822B)
#define BIT_SET_LLTINI_ADDR_V1_8822B(x, v)                                     \
	(BIT_CLEAR_LLTINI_ADDR_V1_8822B(x) | BIT_LLTINI_ADDR_V1_8822B(v))

/* 2 REG_BB_ACCESS_CTRL_8822B */

#define BIT_SHIFT_BB_WRITE_READ_8822B 30
#define BIT_MASK_BB_WRITE_READ_8822B 0x3
#define BIT_BB_WRITE_READ_8822B(x)                                             \
	(((x) & BIT_MASK_BB_WRITE_READ_8822B) << BIT_SHIFT_BB_WRITE_READ_8822B)
#define BITS_BB_WRITE_READ_8822B                                               \
	(BIT_MASK_BB_WRITE_READ_8822B << BIT_SHIFT_BB_WRITE_READ_8822B)
#define BIT_CLEAR_BB_WRITE_READ_8822B(x) ((x) & (~BITS_BB_WRITE_READ_8822B))
#define BIT_GET_BB_WRITE_READ_8822B(x)                                         \
	(((x) >> BIT_SHIFT_BB_WRITE_READ_8822B) & BIT_MASK_BB_WRITE_READ_8822B)
#define BIT_SET_BB_WRITE_READ_8822B(x, v)                                      \
	(BIT_CLEAR_BB_WRITE_READ_8822B(x) | BIT_BB_WRITE_READ_8822B(v))

#define BIT_SHIFT_BB_WRITE_EN_8822B 12
#define BIT_MASK_BB_WRITE_EN_8822B 0xf
#define BIT_BB_WRITE_EN_8822B(x)                                               \
	(((x) & BIT_MASK_BB_WRITE_EN_8822B) << BIT_SHIFT_BB_WRITE_EN_8822B)
#define BITS_BB_WRITE_EN_8822B                                                 \
	(BIT_MASK_BB_WRITE_EN_8822B << BIT_SHIFT_BB_WRITE_EN_8822B)
#define BIT_CLEAR_BB_WRITE_EN_8822B(x) ((x) & (~BITS_BB_WRITE_EN_8822B))
#define BIT_GET_BB_WRITE_EN_8822B(x)                                           \
	(((x) >> BIT_SHIFT_BB_WRITE_EN_8822B) & BIT_MASK_BB_WRITE_EN_8822B)
#define BIT_SET_BB_WRITE_EN_8822B(x, v)                                        \
	(BIT_CLEAR_BB_WRITE_EN_8822B(x) | BIT_BB_WRITE_EN_8822B(v))

#define BIT_SHIFT_BB_ADDR_8822B 2
#define BIT_MASK_BB_ADDR_8822B 0x1ff
#define BIT_BB_ADDR_8822B(x)                                                   \
	(((x) & BIT_MASK_BB_ADDR_8822B) << BIT_SHIFT_BB_ADDR_8822B)
#define BITS_BB_ADDR_8822B (BIT_MASK_BB_ADDR_8822B << BIT_SHIFT_BB_ADDR_8822B)
#define BIT_CLEAR_BB_ADDR_8822B(x) ((x) & (~BITS_BB_ADDR_8822B))
#define BIT_GET_BB_ADDR_8822B(x)                                               \
	(((x) >> BIT_SHIFT_BB_ADDR_8822B) & BIT_MASK_BB_ADDR_8822B)
#define BIT_SET_BB_ADDR_8822B(x, v)                                            \
	(BIT_CLEAR_BB_ADDR_8822B(x) | BIT_BB_ADDR_8822B(v))

#define BIT_BB_ERRACC_8822B BIT(0)

/* 2 REG_BB_ACCESS_DATA_8822B */

#define BIT_SHIFT_BB_DATA_8822B 0
#define BIT_MASK_BB_DATA_8822B 0xffffffffL
#define BIT_BB_DATA_8822B(x)                                                   \
	(((x) & BIT_MASK_BB_DATA_8822B) << BIT_SHIFT_BB_DATA_8822B)
#define BITS_BB_DATA_8822B (BIT_MASK_BB_DATA_8822B << BIT_SHIFT_BB_DATA_8822B)
#define BIT_CLEAR_BB_DATA_8822B(x) ((x) & (~BITS_BB_DATA_8822B))
#define BIT_GET_BB_DATA_8822B(x)                                               \
	(((x) >> BIT_SHIFT_BB_DATA_8822B) & BIT_MASK_BB_DATA_8822B)
#define BIT_SET_BB_DATA_8822B(x, v)                                            \
	(BIT_CLEAR_BB_DATA_8822B(x) | BIT_BB_DATA_8822B(v))

/* 2 REG_HMEBOX_E0_8822B */

#define BIT_SHIFT_HMEBOX_E0_8822B 0
#define BIT_MASK_HMEBOX_E0_8822B 0xffffffffL
#define BIT_HMEBOX_E0_8822B(x)                                                 \
	(((x) & BIT_MASK_HMEBOX_E0_8822B) << BIT_SHIFT_HMEBOX_E0_8822B)
#define BITS_HMEBOX_E0_8822B                                                   \
	(BIT_MASK_HMEBOX_E0_8822B << BIT_SHIFT_HMEBOX_E0_8822B)
#define BIT_CLEAR_HMEBOX_E0_8822B(x) ((x) & (~BITS_HMEBOX_E0_8822B))
#define BIT_GET_HMEBOX_E0_8822B(x)                                             \
	(((x) >> BIT_SHIFT_HMEBOX_E0_8822B) & BIT_MASK_HMEBOX_E0_8822B)
#define BIT_SET_HMEBOX_E0_8822B(x, v)                                          \
	(BIT_CLEAR_HMEBOX_E0_8822B(x) | BIT_HMEBOX_E0_8822B(v))

/* 2 REG_HMEBOX_E1_8822B */

#define BIT_SHIFT_HMEBOX_E1_8822B 0
#define BIT_MASK_HMEBOX_E1_8822B 0xffffffffL
#define BIT_HMEBOX_E1_8822B(x)                                                 \
	(((x) & BIT_MASK_HMEBOX_E1_8822B) << BIT_SHIFT_HMEBOX_E1_8822B)
#define BITS_HMEBOX_E1_8822B                                                   \
	(BIT_MASK_HMEBOX_E1_8822B << BIT_SHIFT_HMEBOX_E1_8822B)
#define BIT_CLEAR_HMEBOX_E1_8822B(x) ((x) & (~BITS_HMEBOX_E1_8822B))
#define BIT_GET_HMEBOX_E1_8822B(x)                                             \
	(((x) >> BIT_SHIFT_HMEBOX_E1_8822B) & BIT_MASK_HMEBOX_E1_8822B)
#define BIT_SET_HMEBOX_E1_8822B(x, v)                                          \
	(BIT_CLEAR_HMEBOX_E1_8822B(x) | BIT_HMEBOX_E1_8822B(v))

/* 2 REG_HMEBOX_E2_8822B */

#define BIT_SHIFT_HMEBOX_E2_8822B 0
#define BIT_MASK_HMEBOX_E2_8822B 0xffffffffL
#define BIT_HMEBOX_E2_8822B(x)                                                 \
	(((x) & BIT_MASK_HMEBOX_E2_8822B) << BIT_SHIFT_HMEBOX_E2_8822B)
#define BITS_HMEBOX_E2_8822B                                                   \
	(BIT_MASK_HMEBOX_E2_8822B << BIT_SHIFT_HMEBOX_E2_8822B)
#define BIT_CLEAR_HMEBOX_E2_8822B(x) ((x) & (~BITS_HMEBOX_E2_8822B))
#define BIT_GET_HMEBOX_E2_8822B(x)                                             \
	(((x) >> BIT_SHIFT_HMEBOX_E2_8822B) & BIT_MASK_HMEBOX_E2_8822B)
#define BIT_SET_HMEBOX_E2_8822B(x, v)                                          \
	(BIT_CLEAR_HMEBOX_E2_8822B(x) | BIT_HMEBOX_E2_8822B(v))

/* 2 REG_HMEBOX_E3_8822B */

#define BIT_SHIFT_HMEBOX_E3_8822B 0
#define BIT_MASK_HMEBOX_E3_8822B 0xffffffffL
#define BIT_HMEBOX_E3_8822B(x)                                                 \
	(((x) & BIT_MASK_HMEBOX_E3_8822B) << BIT_SHIFT_HMEBOX_E3_8822B)
#define BITS_HMEBOX_E3_8822B                                                   \
	(BIT_MASK_HMEBOX_E3_8822B << BIT_SHIFT_HMEBOX_E3_8822B)
#define BIT_CLEAR_HMEBOX_E3_8822B(x) ((x) & (~BITS_HMEBOX_E3_8822B))
#define BIT_GET_HMEBOX_E3_8822B(x)                                             \
	(((x) >> BIT_SHIFT_HMEBOX_E3_8822B) & BIT_MASK_HMEBOX_E3_8822B)
#define BIT_SET_HMEBOX_E3_8822B(x, v)                                          \
	(BIT_CLEAR_HMEBOX_E3_8822B(x) | BIT_HMEBOX_E3_8822B(v))

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_CR_EXT_8822B */

#define BIT_SHIFT_PHY_REQ_DELAY_8822B 24
#define BIT_MASK_PHY_REQ_DELAY_8822B 0xf
#define BIT_PHY_REQ_DELAY_8822B(x)                                             \
	(((x) & BIT_MASK_PHY_REQ_DELAY_8822B) << BIT_SHIFT_PHY_REQ_DELAY_8822B)
#define BITS_PHY_REQ_DELAY_8822B                                               \
	(BIT_MASK_PHY_REQ_DELAY_8822B << BIT_SHIFT_PHY_REQ_DELAY_8822B)
#define BIT_CLEAR_PHY_REQ_DELAY_8822B(x) ((x) & (~BITS_PHY_REQ_DELAY_8822B))
#define BIT_GET_PHY_REQ_DELAY_8822B(x)                                         \
	(((x) >> BIT_SHIFT_PHY_REQ_DELAY_8822B) & BIT_MASK_PHY_REQ_DELAY_8822B)
#define BIT_SET_PHY_REQ_DELAY_8822B(x, v)                                      \
	(BIT_CLEAR_PHY_REQ_DELAY_8822B(x) | BIT_PHY_REQ_DELAY_8822B(v))

#define BIT_SPD_DOWN_8822B BIT(16)

#define BIT_SHIFT_NETYPE4_8822B 4
#define BIT_MASK_NETYPE4_8822B 0x3
#define BIT_NETYPE4_8822B(x)                                                   \
	(((x) & BIT_MASK_NETYPE4_8822B) << BIT_SHIFT_NETYPE4_8822B)
#define BITS_NETYPE4_8822B (BIT_MASK_NETYPE4_8822B << BIT_SHIFT_NETYPE4_8822B)
#define BIT_CLEAR_NETYPE4_8822B(x) ((x) & (~BITS_NETYPE4_8822B))
#define BIT_GET_NETYPE4_8822B(x)                                               \
	(((x) >> BIT_SHIFT_NETYPE4_8822B) & BIT_MASK_NETYPE4_8822B)
#define BIT_SET_NETYPE4_8822B(x, v)                                            \
	(BIT_CLEAR_NETYPE4_8822B(x) | BIT_NETYPE4_8822B(v))

#define BIT_SHIFT_NETYPE3_8822B 2
#define BIT_MASK_NETYPE3_8822B 0x3
#define BIT_NETYPE3_8822B(x)                                                   \
	(((x) & BIT_MASK_NETYPE3_8822B) << BIT_SHIFT_NETYPE3_8822B)
#define BITS_NETYPE3_8822B (BIT_MASK_NETYPE3_8822B << BIT_SHIFT_NETYPE3_8822B)
#define BIT_CLEAR_NETYPE3_8822B(x) ((x) & (~BITS_NETYPE3_8822B))
#define BIT_GET_NETYPE3_8822B(x)                                               \
	(((x) >> BIT_SHIFT_NETYPE3_8822B) & BIT_MASK_NETYPE3_8822B)
#define BIT_SET_NETYPE3_8822B(x, v)                                            \
	(BIT_CLEAR_NETYPE3_8822B(x) | BIT_NETYPE3_8822B(v))

#define BIT_SHIFT_NETYPE2_8822B 0
#define BIT_MASK_NETYPE2_8822B 0x3
#define BIT_NETYPE2_8822B(x)                                                   \
	(((x) & BIT_MASK_NETYPE2_8822B) << BIT_SHIFT_NETYPE2_8822B)
#define BITS_NETYPE2_8822B (BIT_MASK_NETYPE2_8822B << BIT_SHIFT_NETYPE2_8822B)
#define BIT_CLEAR_NETYPE2_8822B(x) ((x) & (~BITS_NETYPE2_8822B))
#define BIT_GET_NETYPE2_8822B(x)                                               \
	(((x) >> BIT_SHIFT_NETYPE2_8822B) & BIT_MASK_NETYPE2_8822B)
#define BIT_SET_NETYPE2_8822B(x, v)                                            \
	(BIT_CLEAR_NETYPE2_8822B(x) | BIT_NETYPE2_8822B(v))

/* 2 REG_FWFF_8822B */

#define BIT_SHIFT_PKTNUM_TH_V1_8822B 24
#define BIT_MASK_PKTNUM_TH_V1_8822B 0xff
#define BIT_PKTNUM_TH_V1_8822B(x)                                              \
	(((x) & BIT_MASK_PKTNUM_TH_V1_8822B) << BIT_SHIFT_PKTNUM_TH_V1_8822B)
#define BITS_PKTNUM_TH_V1_8822B                                                \
	(BIT_MASK_PKTNUM_TH_V1_8822B << BIT_SHIFT_PKTNUM_TH_V1_8822B)
#define BIT_CLEAR_PKTNUM_TH_V1_8822B(x) ((x) & (~BITS_PKTNUM_TH_V1_8822B))
#define BIT_GET_PKTNUM_TH_V1_8822B(x)                                          \
	(((x) >> BIT_SHIFT_PKTNUM_TH_V1_8822B) & BIT_MASK_PKTNUM_TH_V1_8822B)
#define BIT_SET_PKTNUM_TH_V1_8822B(x, v)                                       \
	(BIT_CLEAR_PKTNUM_TH_V1_8822B(x) | BIT_PKTNUM_TH_V1_8822B(v))

#define BIT_SHIFT_TIMER_TH_8822B 16
#define BIT_MASK_TIMER_TH_8822B 0xff
#define BIT_TIMER_TH_8822B(x)                                                  \
	(((x) & BIT_MASK_TIMER_TH_8822B) << BIT_SHIFT_TIMER_TH_8822B)
#define BITS_TIMER_TH_8822B                                                    \
	(BIT_MASK_TIMER_TH_8822B << BIT_SHIFT_TIMER_TH_8822B)
#define BIT_CLEAR_TIMER_TH_8822B(x) ((x) & (~BITS_TIMER_TH_8822B))
#define BIT_GET_TIMER_TH_8822B(x)                                              \
	(((x) >> BIT_SHIFT_TIMER_TH_8822B) & BIT_MASK_TIMER_TH_8822B)
#define BIT_SET_TIMER_TH_8822B(x, v)                                           \
	(BIT_CLEAR_TIMER_TH_8822B(x) | BIT_TIMER_TH_8822B(v))

#define BIT_SHIFT_RXPKT1ENADDR_8822B 0
#define BIT_MASK_RXPKT1ENADDR_8822B 0xffff
#define BIT_RXPKT1ENADDR_8822B(x)                                              \
	(((x) & BIT_MASK_RXPKT1ENADDR_8822B) << BIT_SHIFT_RXPKT1ENADDR_8822B)
#define BITS_RXPKT1ENADDR_8822B                                                \
	(BIT_MASK_RXPKT1ENADDR_8822B << BIT_SHIFT_RXPKT1ENADDR_8822B)
#define BIT_CLEAR_RXPKT1ENADDR_8822B(x) ((x) & (~BITS_RXPKT1ENADDR_8822B))
#define BIT_GET_RXPKT1ENADDR_8822B(x)                                          \
	(((x) >> BIT_SHIFT_RXPKT1ENADDR_8822B) & BIT_MASK_RXPKT1ENADDR_8822B)
#define BIT_SET_RXPKT1ENADDR_8822B(x, v)                                       \
	(BIT_CLEAR_RXPKT1ENADDR_8822B(x) | BIT_RXPKT1ENADDR_8822B(v))

/* 2 REG_RXFF_PTR_V1_8822B */

/* 2 REG_NOT_VALID_8822B */

#define BIT_SHIFT_RXFF0_RDPTR_V2_8822B 0
#define BIT_MASK_RXFF0_RDPTR_V2_8822B 0x3ffff
#define BIT_RXFF0_RDPTR_V2_8822B(x)                                            \
	(((x) & BIT_MASK_RXFF0_RDPTR_V2_8822B)                                 \
	 << BIT_SHIFT_RXFF0_RDPTR_V2_8822B)
#define BITS_RXFF0_RDPTR_V2_8822B                                              \
	(BIT_MASK_RXFF0_RDPTR_V2_8822B << BIT_SHIFT_RXFF0_RDPTR_V2_8822B)
#define BIT_CLEAR_RXFF0_RDPTR_V2_8822B(x) ((x) & (~BITS_RXFF0_RDPTR_V2_8822B))
#define BIT_GET_RXFF0_RDPTR_V2_8822B(x)                                        \
	(((x) >> BIT_SHIFT_RXFF0_RDPTR_V2_8822B) &                             \
	 BIT_MASK_RXFF0_RDPTR_V2_8822B)
#define BIT_SET_RXFF0_RDPTR_V2_8822B(x, v)                                     \
	(BIT_CLEAR_RXFF0_RDPTR_V2_8822B(x) | BIT_RXFF0_RDPTR_V2_8822B(v))

/* 2 REG_RXFF_WTR_V1_8822B */

/* 2 REG_NOT_VALID_8822B */

#define BIT_SHIFT_RXFF0_WTPTR_V2_8822B 0
#define BIT_MASK_RXFF0_WTPTR_V2_8822B 0x3ffff
#define BIT_RXFF0_WTPTR_V2_8822B(x)                                            \
	(((x) & BIT_MASK_RXFF0_WTPTR_V2_8822B)                                 \
	 << BIT_SHIFT_RXFF0_WTPTR_V2_8822B)
#define BITS_RXFF0_WTPTR_V2_8822B                                              \
	(BIT_MASK_RXFF0_WTPTR_V2_8822B << BIT_SHIFT_RXFF0_WTPTR_V2_8822B)
#define BIT_CLEAR_RXFF0_WTPTR_V2_8822B(x) ((x) & (~BITS_RXFF0_WTPTR_V2_8822B))
#define BIT_GET_RXFF0_WTPTR_V2_8822B(x)                                        \
	(((x) >> BIT_SHIFT_RXFF0_WTPTR_V2_8822B) &                             \
	 BIT_MASK_RXFF0_WTPTR_V2_8822B)
#define BIT_SET_RXFF0_WTPTR_V2_8822B(x, v)                                     \
	(BIT_CLEAR_RXFF0_WTPTR_V2_8822B(x) | BIT_RXFF0_WTPTR_V2_8822B(v))

/* 2 REG_FE2IMR_8822B */
#define BIT__FE4ISR__IND_MSK_8822B BIT(29)
#define BIT_FS_TXSC_DESC_DONE_INT_EN_8822B BIT(28)
#define BIT_FS_TXSC_BKDONE_INT_EN_8822B BIT(27)
#define BIT_FS_TXSC_BEDONE_INT_EN_8822B BIT(26)
#define BIT_FS_TXSC_VIDONE_INT_EN_8822B BIT(25)
#define BIT_FS_TXSC_VODONE_INT_EN_8822B BIT(24)
#define BIT_FS_ATIM_MB7_INT_EN_8822B BIT(23)
#define BIT_FS_ATIM_MB6_INT_EN_8822B BIT(22)
#define BIT_FS_ATIM_MB5_INT_EN_8822B BIT(21)
#define BIT_FS_ATIM_MB4_INT_EN_8822B BIT(20)
#define BIT_FS_ATIM_MB3_INT_EN_8822B BIT(19)
#define BIT_FS_ATIM_MB2_INT_EN_8822B BIT(18)
#define BIT_FS_ATIM_MB1_INT_EN_8822B BIT(17)
#define BIT_FS_ATIM_MB0_INT_EN_8822B BIT(16)
#define BIT_FS_TBTT4INT_EN_8822B BIT(11)
#define BIT_FS_TBTT3INT_EN_8822B BIT(10)
#define BIT_FS_TBTT2INT_EN_8822B BIT(9)
#define BIT_FS_TBTT1INT_EN_8822B BIT(8)
#define BIT_FS_TBTT0_MB7INT_EN_8822B BIT(7)
#define BIT_FS_TBTT0_MB6INT_EN_8822B BIT(6)
#define BIT_FS_TBTT0_MB5INT_EN_8822B BIT(5)
#define BIT_FS_TBTT0_MB4INT_EN_8822B BIT(4)
#define BIT_FS_TBTT0_MB3INT_EN_8822B BIT(3)
#define BIT_FS_TBTT0_MB2INT_EN_8822B BIT(2)
#define BIT_FS_TBTT0_MB1INT_EN_8822B BIT(1)
#define BIT_FS_TBTT0_INT_EN_8822B BIT(0)

/* 2 REG_FE2ISR_8822B */
#define BIT__FE4ISR__IND_INT_8822B BIT(29)
#define BIT_FS_TXSC_DESC_DONE_INT_8822B BIT(28)
#define BIT_FS_TXSC_BKDONE_INT_8822B BIT(27)
#define BIT_FS_TXSC_BEDONE_INT_8822B BIT(26)
#define BIT_FS_TXSC_VIDONE_INT_8822B BIT(25)
#define BIT_FS_TXSC_VODONE_INT_8822B BIT(24)
#define BIT_FS_ATIM_MB7_INT_8822B BIT(23)
#define BIT_FS_ATIM_MB6_INT_8822B BIT(22)
#define BIT_FS_ATIM_MB5_INT_8822B BIT(21)
#define BIT_FS_ATIM_MB4_INT_8822B BIT(20)
#define BIT_FS_ATIM_MB3_INT_8822B BIT(19)
#define BIT_FS_ATIM_MB2_INT_8822B BIT(18)
#define BIT_FS_ATIM_MB1_INT_8822B BIT(17)
#define BIT_FS_ATIM_MB0_INT_8822B BIT(16)
#define BIT_FS_TBTT4INT_8822B BIT(11)
#define BIT_FS_TBTT3INT_8822B BIT(10)
#define BIT_FS_TBTT2INT_8822B BIT(9)
#define BIT_FS_TBTT1INT_8822B BIT(8)
#define BIT_FS_TBTT0_MB7INT_8822B BIT(7)
#define BIT_FS_TBTT0_MB6INT_8822B BIT(6)
#define BIT_FS_TBTT0_MB5INT_8822B BIT(5)
#define BIT_FS_TBTT0_MB4INT_8822B BIT(4)
#define BIT_FS_TBTT0_MB3INT_8822B BIT(3)
#define BIT_FS_TBTT0_MB2INT_8822B BIT(2)
#define BIT_FS_TBTT0_MB1INT_8822B BIT(1)
#define BIT_FS_TBTT0_INT_8822B BIT(0)

/* 2 REG_FE3IMR_8822B */
#define BIT_FS_CLI3_MTI_BCNIVLEAR_INT__EN_8822B BIT(31)
#define BIT_FS_CLI2_MTI_BCNIVLEAR_INT__EN_8822B BIT(30)
#define BIT_FS_CLI1_MTI_BCNIVLEAR_INT__EN_8822B BIT(29)
#define BIT_FS_CLI0_MTI_BCNIVLEAR_INT__EN_8822B BIT(28)
#define BIT_FS_BCNDMA4_INT_EN_8822B BIT(27)
#define BIT_FS_BCNDMA3_INT_EN_8822B BIT(26)
#define BIT_FS_BCNDMA2_INT_EN_8822B BIT(25)
#define BIT_FS_BCNDMA1_INT_EN_8822B BIT(24)
#define BIT_FS_BCNDMA0_MB7_INT_EN_8822B BIT(23)
#define BIT_FS_BCNDMA0_MB6_INT_EN_8822B BIT(22)
#define BIT_FS_BCNDMA0_MB5_INT_EN_8822B BIT(21)
#define BIT_FS_BCNDMA0_MB4_INT_EN_8822B BIT(20)
#define BIT_FS_BCNDMA0_MB3_INT_EN_8822B BIT(19)
#define BIT_FS_BCNDMA0_MB2_INT_EN_8822B BIT(18)
#define BIT_FS_BCNDMA0_MB1_INT_EN_8822B BIT(17)
#define BIT_FS_BCNDMA0_INT_EN_8822B BIT(16)
#define BIT_FS_MTI_BCNIVLEAR_INT__EN_8822B BIT(15)
#define BIT_FS_BCNERLY4_INT_EN_8822B BIT(11)
#define BIT_FS_BCNERLY3_INT_EN_8822B BIT(10)
#define BIT_FS_BCNERLY2_INT_EN_8822B BIT(9)
#define BIT_FS_BCNERLY1_INT_EN_8822B BIT(8)
#define BIT_FS_BCNERLY0_MB7INT_EN_8822B BIT(7)
#define BIT_FS_BCNERLY0_MB6INT_EN_8822B BIT(6)
#define BIT_FS_BCNERLY0_MB5INT_EN_8822B BIT(5)
#define BIT_FS_BCNERLY0_MB4INT_EN_8822B BIT(4)
#define BIT_FS_BCNERLY0_MB3INT_EN_8822B BIT(3)
#define BIT_FS_BCNERLY0_MB2INT_EN_8822B BIT(2)
#define BIT_FS_BCNERLY0_MB1INT_EN_8822B BIT(1)
#define BIT_FS_BCNERLY0_INT_EN_8822B BIT(0)

/* 2 REG_FE3ISR_8822B */
#define BIT_FS_CLI3_MTI_BCNIVLEAR_INT_8822B BIT(31)
#define BIT_FS_CLI2_MTI_BCNIVLEAR_INT_8822B BIT(30)
#define BIT_FS_CLI1_MTI_BCNIVLEAR_INT_8822B BIT(29)
#define BIT_FS_CLI0_MTI_BCNIVLEAR_INT_8822B BIT(28)
#define BIT_FS_BCNDMA4_INT_8822B BIT(27)
#define BIT_FS_BCNDMA3_INT_8822B BIT(26)
#define BIT_FS_BCNDMA2_INT_8822B BIT(25)
#define BIT_FS_BCNDMA1_INT_8822B BIT(24)
#define BIT_FS_BCNDMA0_MB7_INT_8822B BIT(23)
#define BIT_FS_BCNDMA0_MB6_INT_8822B BIT(22)
#define BIT_FS_BCNDMA0_MB5_INT_8822B BIT(21)
#define BIT_FS_BCNDMA0_MB4_INT_8822B BIT(20)
#define BIT_FS_BCNDMA0_MB3_INT_8822B BIT(19)
#define BIT_FS_BCNDMA0_MB2_INT_8822B BIT(18)
#define BIT_FS_BCNDMA0_MB1_INT_8822B BIT(17)
#define BIT_FS_BCNDMA0_INT_8822B BIT(16)
#define BIT_FS_MTI_BCNIVLEAR_INT_8822B BIT(15)
#define BIT_FS_BCNERLY4_INT_8822B BIT(11)
#define BIT_FS_BCNERLY3_INT_8822B BIT(10)
#define BIT_FS_BCNERLY2_INT_8822B BIT(9)
#define BIT_FS_BCNERLY1_INT_8822B BIT(8)
#define BIT_FS_BCNERLY0_MB7INT_8822B BIT(7)
#define BIT_FS_BCNERLY0_MB6INT_8822B BIT(6)
#define BIT_FS_BCNERLY0_MB5INT_8822B BIT(5)
#define BIT_FS_BCNERLY0_MB4INT_8822B BIT(4)
#define BIT_FS_BCNERLY0_MB3INT_8822B BIT(3)
#define BIT_FS_BCNERLY0_MB2INT_8822B BIT(2)
#define BIT_FS_BCNERLY0_MB1INT_8822B BIT(1)
#define BIT_FS_BCNERLY0_INT_8822B BIT(0)

/* 2 REG_FE4IMR_8822B */
#define BIT_FS_CLI3_TXPKTIN_INT_EN_8822B BIT(19)
#define BIT_FS_CLI2_TXPKTIN_INT_EN_8822B BIT(18)
#define BIT_FS_CLI1_TXPKTIN_INT_EN_8822B BIT(17)
#define BIT_FS_CLI0_TXPKTIN_INT_EN_8822B BIT(16)
#define BIT_FS_CLI3_RX_UMD0_INT_EN_8822B BIT(15)
#define BIT_FS_CLI3_RX_UMD1_INT_EN_8822B BIT(14)
#define BIT_FS_CLI3_RX_BMD0_INT_EN_8822B BIT(13)
#define BIT_FS_CLI3_RX_BMD1_INT_EN_8822B BIT(12)
#define BIT_FS_CLI2_RX_UMD0_INT_EN_8822B BIT(11)
#define BIT_FS_CLI2_RX_UMD1_INT_EN_8822B BIT(10)
#define BIT_FS_CLI2_RX_BMD0_INT_EN_8822B BIT(9)
#define BIT_FS_CLI2_RX_BMD1_INT_EN_8822B BIT(8)
#define BIT_FS_CLI1_RX_UMD0_INT_EN_8822B BIT(7)
#define BIT_FS_CLI1_RX_UMD1_INT_EN_8822B BIT(6)
#define BIT_FS_CLI1_RX_BMD0_INT_EN_8822B BIT(5)
#define BIT_FS_CLI1_RX_BMD1_INT_EN_8822B BIT(4)
#define BIT_FS_CLI0_RX_UMD0_INT_EN_8822B BIT(3)
#define BIT_FS_CLI0_RX_UMD1_INT_EN_8822B BIT(2)
#define BIT_FS_CLI0_RX_BMD0_INT_EN_8822B BIT(1)
#define BIT_FS_CLI0_RX_BMD1_INT_EN_8822B BIT(0)

/* 2 REG_FE4ISR_8822B */
#define BIT_FS_CLI3_TXPKTIN_INT_8822B BIT(19)
#define BIT_FS_CLI2_TXPKTIN_INT_8822B BIT(18)
#define BIT_FS_CLI1_TXPKTIN_INT_8822B BIT(17)
#define BIT_FS_CLI0_TXPKTIN_INT_8822B BIT(16)
#define BIT_FS_CLI3_RX_UMD0_INT_8822B BIT(15)
#define BIT_FS_CLI3_RX_UMD1_INT_8822B BIT(14)
#define BIT_FS_CLI3_RX_BMD0_INT_8822B BIT(13)
#define BIT_FS_CLI3_RX_BMD1_INT_8822B BIT(12)
#define BIT_FS_CLI2_RX_UMD0_INT_8822B BIT(11)
#define BIT_FS_CLI2_RX_UMD1_INT_8822B BIT(10)
#define BIT_FS_CLI2_RX_BMD0_INT_8822B BIT(9)
#define BIT_FS_CLI2_RX_BMD1_INT_8822B BIT(8)
#define BIT_FS_CLI1_RX_UMD0_INT_8822B BIT(7)
#define BIT_FS_CLI1_RX_UMD1_INT_8822B BIT(6)
#define BIT_FS_CLI1_RX_BMD0_INT_8822B BIT(5)
#define BIT_FS_CLI1_RX_BMD1_INT_8822B BIT(4)
#define BIT_FS_CLI0_RX_UMD0_INT_8822B BIT(3)
#define BIT_FS_CLI0_RX_UMD1_INT_8822B BIT(2)
#define BIT_FS_CLI0_RX_BMD0_INT_8822B BIT(1)
#define BIT_FS_CLI0_RX_BMD1_INT_8822B BIT(0)

/* 2 REG_FT1IMR_8822B */
#define BIT__FT2ISR__IND_MSK_8822B BIT(30)
#define BIT_FTM_PTT_INT_EN_8822B BIT(29)
#define BIT_RXFTMREQ_INT_EN_8822B BIT(28)
#define BIT_RXFTM_INT_EN_8822B BIT(27)
#define BIT_TXFTM_INT_EN_8822B BIT(26)
#define BIT_FS_H2C_CMD_OK_INT_EN_8822B BIT(25)
#define BIT_FS_H2C_CMD_FULL_INT_EN_8822B BIT(24)
#define BIT_FS_MACID_PWRCHANGE5_INT_EN_8822B BIT(23)
#define BIT_FS_MACID_PWRCHANGE4_INT_EN_8822B BIT(22)
#define BIT_FS_MACID_PWRCHANGE3_INT_EN_8822B BIT(21)
#define BIT_FS_MACID_PWRCHANGE2_INT_EN_8822B BIT(20)
#define BIT_FS_MACID_PWRCHANGE1_INT_EN_8822B BIT(19)
#define BIT_FS_MACID_PWRCHANGE0_INT_EN_8822B BIT(18)
#define BIT_FS_CTWEND2_INT_EN_8822B BIT(17)
#define BIT_FS_CTWEND1_INT_EN_8822B BIT(16)
#define BIT_FS_CTWEND0_INT_EN_8822B BIT(15)
#define BIT_FS_TX_NULL1_INT_EN_8822B BIT(14)
#define BIT_FS_TX_NULL0_INT_EN_8822B BIT(13)
#define BIT_FS_TSF_BIT32_TOGGLE_EN_8822B BIT(12)
#define BIT_FS_P2P_RFON2_INT_EN_8822B BIT(11)
#define BIT_FS_P2P_RFOFF2_INT_EN_8822B BIT(10)
#define BIT_FS_P2P_RFON1_INT_EN_8822B BIT(9)
#define BIT_FS_P2P_RFOFF1_INT_EN_8822B BIT(8)
#define BIT_FS_P2P_RFON0_INT_EN_8822B BIT(7)
#define BIT_FS_P2P_RFOFF0_INT_EN_8822B BIT(6)
#define BIT_FS_RX_UAPSDMD1_EN_8822B BIT(5)
#define BIT_FS_RX_UAPSDMD0_EN_8822B BIT(4)
#define BIT_FS_TRIGGER_PKT_EN_8822B BIT(3)
#define BIT_FS_EOSP_INT_EN_8822B BIT(2)
#define BIT_FS_RPWM2_INT_EN_8822B BIT(1)
#define BIT_FS_RPWM_INT_EN_8822B BIT(0)

/* 2 REG_FT1ISR_8822B */
#define BIT__FT2ISR__IND_INT_8822B BIT(30)
#define BIT_FTM_PTT_INT_8822B BIT(29)
#define BIT_RXFTMREQ_INT_8822B BIT(28)
#define BIT_RXFTM_INT_8822B BIT(27)
#define BIT_TXFTM_INT_8822B BIT(26)
#define BIT_FS_H2C_CMD_OK_INT_8822B BIT(25)
#define BIT_FS_H2C_CMD_FULL_INT_8822B BIT(24)
#define BIT_FS_MACID_PWRCHANGE5_INT_8822B BIT(23)
#define BIT_FS_MACID_PWRCHANGE4_INT_8822B BIT(22)
#define BIT_FS_MACID_PWRCHANGE3_INT_8822B BIT(21)
#define BIT_FS_MACID_PWRCHANGE2_INT_8822B BIT(20)
#define BIT_FS_MACID_PWRCHANGE1_INT_8822B BIT(19)
#define BIT_FS_MACID_PWRCHANGE0_INT_8822B BIT(18)
#define BIT_FS_CTWEND2_INT_8822B BIT(17)
#define BIT_FS_CTWEND1_INT_8822B BIT(16)
#define BIT_FS_CTWEND0_INT_8822B BIT(15)
#define BIT_FS_TX_NULL1_INT_8822B BIT(14)
#define BIT_FS_TX_NULL0_INT_8822B BIT(13)
#define BIT_FS_TSF_BIT32_TOGGLE_INT_8822B BIT(12)
#define BIT_FS_P2P_RFON2_INT_8822B BIT(11)
#define BIT_FS_P2P_RFOFF2_INT_8822B BIT(10)
#define BIT_FS_P2P_RFON1_INT_8822B BIT(9)
#define BIT_FS_P2P_RFOFF1_INT_8822B BIT(8)
#define BIT_FS_P2P_RFON0_INT_8822B BIT(7)
#define BIT_FS_P2P_RFOFF0_INT_8822B BIT(6)
#define BIT_FS_RX_UAPSDMD1_INT_8822B BIT(5)
#define BIT_FS_RX_UAPSDMD0_INT_8822B BIT(4)
#define BIT_FS_TRIGGER_PKT_INT_8822B BIT(3)
#define BIT_FS_EOSP_INT_8822B BIT(2)
#define BIT_FS_RPWM2_INT_8822B BIT(1)
#define BIT_FS_RPWM_INT_8822B BIT(0)

/* 2 REG_SPWR0_8822B */

#define BIT_SHIFT_MID_31TO0_8822B 0
#define BIT_MASK_MID_31TO0_8822B 0xffffffffL
#define BIT_MID_31TO0_8822B(x)                                                 \
	(((x) & BIT_MASK_MID_31TO0_8822B) << BIT_SHIFT_MID_31TO0_8822B)
#define BITS_MID_31TO0_8822B                                                   \
	(BIT_MASK_MID_31TO0_8822B << BIT_SHIFT_MID_31TO0_8822B)
#define BIT_CLEAR_MID_31TO0_8822B(x) ((x) & (~BITS_MID_31TO0_8822B))
#define BIT_GET_MID_31TO0_8822B(x)                                             \
	(((x) >> BIT_SHIFT_MID_31TO0_8822B) & BIT_MASK_MID_31TO0_8822B)
#define BIT_SET_MID_31TO0_8822B(x, v)                                          \
	(BIT_CLEAR_MID_31TO0_8822B(x) | BIT_MID_31TO0_8822B(v))

/* 2 REG_SPWR1_8822B */

#define BIT_SHIFT_MID_63TO32_8822B 0
#define BIT_MASK_MID_63TO32_8822B 0xffffffffL
#define BIT_MID_63TO32_8822B(x)                                                \
	(((x) & BIT_MASK_MID_63TO32_8822B) << BIT_SHIFT_MID_63TO32_8822B)
#define BITS_MID_63TO32_8822B                                                  \
	(BIT_MASK_MID_63TO32_8822B << BIT_SHIFT_MID_63TO32_8822B)
#define BIT_CLEAR_MID_63TO32_8822B(x) ((x) & (~BITS_MID_63TO32_8822B))
#define BIT_GET_MID_63TO32_8822B(x)                                            \
	(((x) >> BIT_SHIFT_MID_63TO32_8822B) & BIT_MASK_MID_63TO32_8822B)
#define BIT_SET_MID_63TO32_8822B(x, v)                                         \
	(BIT_CLEAR_MID_63TO32_8822B(x) | BIT_MID_63TO32_8822B(v))

/* 2 REG_SPWR2_8822B */

#define BIT_SHIFT_MID_95O64_8822B 0
#define BIT_MASK_MID_95O64_8822B 0xffffffffL
#define BIT_MID_95O64_8822B(x)                                                 \
	(((x) & BIT_MASK_MID_95O64_8822B) << BIT_SHIFT_MID_95O64_8822B)
#define BITS_MID_95O64_8822B                                                   \
	(BIT_MASK_MID_95O64_8822B << BIT_SHIFT_MID_95O64_8822B)
#define BIT_CLEAR_MID_95O64_8822B(x) ((x) & (~BITS_MID_95O64_8822B))
#define BIT_GET_MID_95O64_8822B(x)                                             \
	(((x) >> BIT_SHIFT_MID_95O64_8822B) & BIT_MASK_MID_95O64_8822B)
#define BIT_SET_MID_95O64_8822B(x, v)                                          \
	(BIT_CLEAR_MID_95O64_8822B(x) | BIT_MID_95O64_8822B(v))

/* 2 REG_SPWR3_8822B */

#define BIT_SHIFT_MID_127TO96_8822B 0
#define BIT_MASK_MID_127TO96_8822B 0xffffffffL
#define BIT_MID_127TO96_8822B(x)                                               \
	(((x) & BIT_MASK_MID_127TO96_8822B) << BIT_SHIFT_MID_127TO96_8822B)
#define BITS_MID_127TO96_8822B                                                 \
	(BIT_MASK_MID_127TO96_8822B << BIT_SHIFT_MID_127TO96_8822B)
#define BIT_CLEAR_MID_127TO96_8822B(x) ((x) & (~BITS_MID_127TO96_8822B))
#define BIT_GET_MID_127TO96_8822B(x)                                           \
	(((x) >> BIT_SHIFT_MID_127TO96_8822B) & BIT_MASK_MID_127TO96_8822B)
#define BIT_SET_MID_127TO96_8822B(x, v)                                        \
	(BIT_CLEAR_MID_127TO96_8822B(x) | BIT_MID_127TO96_8822B(v))

/* 2 REG_POWSEQ_8822B */

#define BIT_SHIFT_SEQNUM_MID_8822B 16
#define BIT_MASK_SEQNUM_MID_8822B 0xffff
#define BIT_SEQNUM_MID_8822B(x)                                                \
	(((x) & BIT_MASK_SEQNUM_MID_8822B) << BIT_SHIFT_SEQNUM_MID_8822B)
#define BITS_SEQNUM_MID_8822B                                                  \
	(BIT_MASK_SEQNUM_MID_8822B << BIT_SHIFT_SEQNUM_MID_8822B)
#define BIT_CLEAR_SEQNUM_MID_8822B(x) ((x) & (~BITS_SEQNUM_MID_8822B))
#define BIT_GET_SEQNUM_MID_8822B(x)                                            \
	(((x) >> BIT_SHIFT_SEQNUM_MID_8822B) & BIT_MASK_SEQNUM_MID_8822B)
#define BIT_SET_SEQNUM_MID_8822B(x, v)                                         \
	(BIT_CLEAR_SEQNUM_MID_8822B(x) | BIT_SEQNUM_MID_8822B(v))

#define BIT_SHIFT_REF_MID_8822B 0
#define BIT_MASK_REF_MID_8822B 0x7f
#define BIT_REF_MID_8822B(x)                                                   \
	(((x) & BIT_MASK_REF_MID_8822B) << BIT_SHIFT_REF_MID_8822B)
#define BITS_REF_MID_8822B (BIT_MASK_REF_MID_8822B << BIT_SHIFT_REF_MID_8822B)
#define BIT_CLEAR_REF_MID_8822B(x) ((x) & (~BITS_REF_MID_8822B))
#define BIT_GET_REF_MID_8822B(x)                                               \
	(((x) >> BIT_SHIFT_REF_MID_8822B) & BIT_MASK_REF_MID_8822B)
#define BIT_SET_REF_MID_8822B(x, v)                                            \
	(BIT_CLEAR_REF_MID_8822B(x) | BIT_REF_MID_8822B(v))

/* 2 REG_TC7_CTRL_V1_8822B */
#define BIT_TC7INT_EN_8822B BIT(26)
#define BIT_TC7MODE_8822B BIT(25)
#define BIT_TC7EN_8822B BIT(24)

#define BIT_SHIFT_TC7DATA_8822B 0
#define BIT_MASK_TC7DATA_8822B 0xffffff
#define BIT_TC7DATA_8822B(x)                                                   \
	(((x) & BIT_MASK_TC7DATA_8822B) << BIT_SHIFT_TC7DATA_8822B)
#define BITS_TC7DATA_8822B (BIT_MASK_TC7DATA_8822B << BIT_SHIFT_TC7DATA_8822B)
#define BIT_CLEAR_TC7DATA_8822B(x) ((x) & (~BITS_TC7DATA_8822B))
#define BIT_GET_TC7DATA_8822B(x)                                               \
	(((x) >> BIT_SHIFT_TC7DATA_8822B) & BIT_MASK_TC7DATA_8822B)
#define BIT_SET_TC7DATA_8822B(x, v)                                            \
	(BIT_CLEAR_TC7DATA_8822B(x) | BIT_TC7DATA_8822B(v))

/* 2 REG_TC8_CTRL_V1_8822B */
#define BIT_TC8INT_EN_8822B BIT(26)
#define BIT_TC8MODE_8822B BIT(25)
#define BIT_TC8EN_8822B BIT(24)

#define BIT_SHIFT_TC8DATA_8822B 0
#define BIT_MASK_TC8DATA_8822B 0xffffff
#define BIT_TC8DATA_8822B(x)                                                   \
	(((x) & BIT_MASK_TC8DATA_8822B) << BIT_SHIFT_TC8DATA_8822B)
#define BITS_TC8DATA_8822B (BIT_MASK_TC8DATA_8822B << BIT_SHIFT_TC8DATA_8822B)
#define BIT_CLEAR_TC8DATA_8822B(x) ((x) & (~BITS_TC8DATA_8822B))
#define BIT_GET_TC8DATA_8822B(x)                                               \
	(((x) >> BIT_SHIFT_TC8DATA_8822B) & BIT_MASK_TC8DATA_8822B)
#define BIT_SET_TC8DATA_8822B(x, v)                                            \
	(BIT_CLEAR_TC8DATA_8822B(x) | BIT_TC8DATA_8822B(v))

/* 2 REG_FT2IMR_8822B */
#define BIT_FS_CLI3_RX_UAPSDMD1_EN_8822B BIT(31)
#define BIT_FS_CLI3_RX_UAPSDMD0_EN_8822B BIT(30)
#define BIT_FS_CLI3_TRIGGER_PKT_EN_8822B BIT(29)
#define BIT_FS_CLI3_EOSP_INT_EN_8822B BIT(28)
#define BIT_FS_CLI2_RX_UAPSDMD1_EN_8822B BIT(27)
#define BIT_FS_CLI2_RX_UAPSDMD0_EN_8822B BIT(26)
#define BIT_FS_CLI2_TRIGGER_PKT_EN_8822B BIT(25)
#define BIT_FS_CLI2_EOSP_INT_EN_8822B BIT(24)
#define BIT_FS_CLI1_RX_UAPSDMD1_EN_8822B BIT(23)
#define BIT_FS_CLI1_RX_UAPSDMD0_EN_8822B BIT(22)
#define BIT_FS_CLI1_TRIGGER_PKT_EN_8822B BIT(21)
#define BIT_FS_CLI1_EOSP_INT_EN_8822B BIT(20)
#define BIT_FS_CLI0_RX_UAPSDMD1_EN_8822B BIT(19)
#define BIT_FS_CLI0_RX_UAPSDMD0_EN_8822B BIT(18)
#define BIT_FS_CLI0_TRIGGER_PKT_EN_8822B BIT(17)
#define BIT_FS_CLI0_EOSP_INT_EN_8822B BIT(16)
#define BIT_FS_TSF_BIT32_TOGGLE_P2P2_EN_8822B BIT(9)
#define BIT_FS_TSF_BIT32_TOGGLE_P2P1_EN_8822B BIT(8)
#define BIT_FS_CLI3_TX_NULL1_INT_EN_8822B BIT(7)
#define BIT_FS_CLI3_TX_NULL0_INT_EN_8822B BIT(6)
#define BIT_FS_CLI2_TX_NULL1_INT_EN_8822B BIT(5)
#define BIT_FS_CLI2_TX_NULL0_INT_EN_8822B BIT(4)
#define BIT_FS_CLI1_TX_NULL1_INT_EN_8822B BIT(3)
#define BIT_FS_CLI1_TX_NULL0_INT_EN_8822B BIT(2)
#define BIT_FS_CLI0_TX_NULL1_INT_EN_8822B BIT(1)
#define BIT_FS_CLI0_TX_NULL0_INT_EN_8822B BIT(0)

/* 2 REG_FT2ISR_8822B */
#define BIT_FS_CLI3_RX_UAPSDMD1_INT_8822B BIT(31)
#define BIT_FS_CLI3_RX_UAPSDMD0_INT_8822B BIT(30)
#define BIT_FS_CLI3_TRIGGER_PKT_INT_8822B BIT(29)
#define BIT_FS_CLI3_EOSP_INT_8822B BIT(28)
#define BIT_FS_CLI2_RX_UAPSDMD1_INT_8822B BIT(27)
#define BIT_FS_CLI2_RX_UAPSDMD0_INT_8822B BIT(26)
#define BIT_FS_CLI2_TRIGGER_PKT_INT_8822B BIT(25)
#define BIT_FS_CLI2_EOSP_INT_8822B BIT(24)
#define BIT_FS_CLI1_RX_UAPSDMD1_INT_8822B BIT(23)
#define BIT_FS_CLI1_RX_UAPSDMD0_INT_8822B BIT(22)
#define BIT_FS_CLI1_TRIGGER_PKT_INT_8822B BIT(21)
#define BIT_FS_CLI1_EOSP_INT_8822B BIT(20)
#define BIT_FS_CLI0_RX_UAPSDMD1_INT_8822B BIT(19)
#define BIT_FS_CLI0_RX_UAPSDMD0_INT_8822B BIT(18)
#define BIT_FS_CLI0_TRIGGER_PKT_INT_8822B BIT(17)
#define BIT_FS_CLI0_EOSP_INT_8822B BIT(16)
#define BIT_FS_TSF_BIT32_TOGGLE_P2P2_INT_8822B BIT(9)
#define BIT_FS_TSF_BIT32_TOGGLE_P2P1_INT_8822B BIT(8)
#define BIT_FS_CLI3_TX_NULL1_INT_8822B BIT(7)
#define BIT_FS_CLI3_TX_NULL0_INT_8822B BIT(6)
#define BIT_FS_CLI2_TX_NULL1_INT_8822B BIT(5)
#define BIT_FS_CLI2_TX_NULL0_INT_8822B BIT(4)
#define BIT_FS_CLI1_TX_NULL1_INT_8822B BIT(3)
#define BIT_FS_CLI1_TX_NULL0_INT_8822B BIT(2)
#define BIT_FS_CLI0_TX_NULL1_INT_8822B BIT(1)
#define BIT_FS_CLI0_TX_NULL0_INT_8822B BIT(0)

/* 2 REG_MSG2_8822B */

#define BIT_SHIFT_FW_MSG2_8822B 0
#define BIT_MASK_FW_MSG2_8822B 0xffffffffL
#define BIT_FW_MSG2_8822B(x)                                                   \
	(((x) & BIT_MASK_FW_MSG2_8822B) << BIT_SHIFT_FW_MSG2_8822B)
#define BITS_FW_MSG2_8822B (BIT_MASK_FW_MSG2_8822B << BIT_SHIFT_FW_MSG2_8822B)
#define BIT_CLEAR_FW_MSG2_8822B(x) ((x) & (~BITS_FW_MSG2_8822B))
#define BIT_GET_FW_MSG2_8822B(x)                                               \
	(((x) >> BIT_SHIFT_FW_MSG2_8822B) & BIT_MASK_FW_MSG2_8822B)
#define BIT_SET_FW_MSG2_8822B(x, v)                                            \
	(BIT_CLEAR_FW_MSG2_8822B(x) | BIT_FW_MSG2_8822B(v))

/* 2 REG_MSG3_8822B */

#define BIT_SHIFT_FW_MSG3_8822B 0
#define BIT_MASK_FW_MSG3_8822B 0xffffffffL
#define BIT_FW_MSG3_8822B(x)                                                   \
	(((x) & BIT_MASK_FW_MSG3_8822B) << BIT_SHIFT_FW_MSG3_8822B)
#define BITS_FW_MSG3_8822B (BIT_MASK_FW_MSG3_8822B << BIT_SHIFT_FW_MSG3_8822B)
#define BIT_CLEAR_FW_MSG3_8822B(x) ((x) & (~BITS_FW_MSG3_8822B))
#define BIT_GET_FW_MSG3_8822B(x)                                               \
	(((x) >> BIT_SHIFT_FW_MSG3_8822B) & BIT_MASK_FW_MSG3_8822B)
#define BIT_SET_FW_MSG3_8822B(x, v)                                            \
	(BIT_CLEAR_FW_MSG3_8822B(x) | BIT_FW_MSG3_8822B(v))

/* 2 REG_MSG4_8822B */

#define BIT_SHIFT_FW_MSG4_8822B 0
#define BIT_MASK_FW_MSG4_8822B 0xffffffffL
#define BIT_FW_MSG4_8822B(x)                                                   \
	(((x) & BIT_MASK_FW_MSG4_8822B) << BIT_SHIFT_FW_MSG4_8822B)
#define BITS_FW_MSG4_8822B (BIT_MASK_FW_MSG4_8822B << BIT_SHIFT_FW_MSG4_8822B)
#define BIT_CLEAR_FW_MSG4_8822B(x) ((x) & (~BITS_FW_MSG4_8822B))
#define BIT_GET_FW_MSG4_8822B(x)                                               \
	(((x) >> BIT_SHIFT_FW_MSG4_8822B) & BIT_MASK_FW_MSG4_8822B)
#define BIT_SET_FW_MSG4_8822B(x, v)                                            \
	(BIT_CLEAR_FW_MSG4_8822B(x) | BIT_FW_MSG4_8822B(v))

/* 2 REG_MSG5_8822B */

#define BIT_SHIFT_FW_MSG5_8822B 0
#define BIT_MASK_FW_MSG5_8822B 0xffffffffL
#define BIT_FW_MSG5_8822B(x)                                                   \
	(((x) & BIT_MASK_FW_MSG5_8822B) << BIT_SHIFT_FW_MSG5_8822B)
#define BITS_FW_MSG5_8822B (BIT_MASK_FW_MSG5_8822B << BIT_SHIFT_FW_MSG5_8822B)
#define BIT_CLEAR_FW_MSG5_8822B(x) ((x) & (~BITS_FW_MSG5_8822B))
#define BIT_GET_FW_MSG5_8822B(x)                                               \
	(((x) >> BIT_SHIFT_FW_MSG5_8822B) & BIT_MASK_FW_MSG5_8822B)
#define BIT_SET_FW_MSG5_8822B(x, v)                                            \
	(BIT_CLEAR_FW_MSG5_8822B(x) | BIT_FW_MSG5_8822B(v))

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_FIFOPAGE_CTRL_1_8822B */

#define BIT_SHIFT_TX_OQT_HE_FREE_SPACE_V1_8822B 16
#define BIT_MASK_TX_OQT_HE_FREE_SPACE_V1_8822B 0xff
#define BIT_TX_OQT_HE_FREE_SPACE_V1_8822B(x)                                   \
	(((x) & BIT_MASK_TX_OQT_HE_FREE_SPACE_V1_8822B)                        \
	 << BIT_SHIFT_TX_OQT_HE_FREE_SPACE_V1_8822B)
#define BITS_TX_OQT_HE_FREE_SPACE_V1_8822B                                     \
	(BIT_MASK_TX_OQT_HE_FREE_SPACE_V1_8822B                                \
	 << BIT_SHIFT_TX_OQT_HE_FREE_SPACE_V1_8822B)
#define BIT_CLEAR_TX_OQT_HE_FREE_SPACE_V1_8822B(x)                             \
	((x) & (~BITS_TX_OQT_HE_FREE_SPACE_V1_8822B))
#define BIT_GET_TX_OQT_HE_FREE_SPACE_V1_8822B(x)                               \
	(((x) >> BIT_SHIFT_TX_OQT_HE_FREE_SPACE_V1_8822B) &                    \
	 BIT_MASK_TX_OQT_HE_FREE_SPACE_V1_8822B)
#define BIT_SET_TX_OQT_HE_FREE_SPACE_V1_8822B(x, v)                            \
	(BIT_CLEAR_TX_OQT_HE_FREE_SPACE_V1_8822B(x) |                          \
	 BIT_TX_OQT_HE_FREE_SPACE_V1_8822B(v))

#define BIT_SHIFT_TX_OQT_NL_FREE_SPACE_V1_8822B 0
#define BIT_MASK_TX_OQT_NL_FREE_SPACE_V1_8822B 0xff
#define BIT_TX_OQT_NL_FREE_SPACE_V1_8822B(x)                                   \
	(((x) & BIT_MASK_TX_OQT_NL_FREE_SPACE_V1_8822B)                        \
	 << BIT_SHIFT_TX_OQT_NL_FREE_SPACE_V1_8822B)
#define BITS_TX_OQT_NL_FREE_SPACE_V1_8822B                                     \
	(BIT_MASK_TX_OQT_NL_FREE_SPACE_V1_8822B                                \
	 << BIT_SHIFT_TX_OQT_NL_FREE_SPACE_V1_8822B)
#define BIT_CLEAR_TX_OQT_NL_FREE_SPACE_V1_8822B(x)                             \
	((x) & (~BITS_TX_OQT_NL_FREE_SPACE_V1_8822B))
#define BIT_GET_TX_OQT_NL_FREE_SPACE_V1_8822B(x)                               \
	(((x) >> BIT_SHIFT_TX_OQT_NL_FREE_SPACE_V1_8822B) &                    \
	 BIT_MASK_TX_OQT_NL_FREE_SPACE_V1_8822B)
#define BIT_SET_TX_OQT_NL_FREE_SPACE_V1_8822B(x, v)                            \
	(BIT_CLEAR_TX_OQT_NL_FREE_SPACE_V1_8822B(x) |                          \
	 BIT_TX_OQT_NL_FREE_SPACE_V1_8822B(v))

/* 2 REG_FIFOPAGE_CTRL_2_8822B */
#define BIT_BCN_VALID_1_V1_8822B BIT(31)

#define BIT_SHIFT_BCN_HEAD_1_V1_8822B 16
#define BIT_MASK_BCN_HEAD_1_V1_8822B 0xfff
#define BIT_BCN_HEAD_1_V1_8822B(x)                                             \
	(((x) & BIT_MASK_BCN_HEAD_1_V1_8822B) << BIT_SHIFT_BCN_HEAD_1_V1_8822B)
#define BITS_BCN_HEAD_1_V1_8822B                                               \
	(BIT_MASK_BCN_HEAD_1_V1_8822B << BIT_SHIFT_BCN_HEAD_1_V1_8822B)
#define BIT_CLEAR_BCN_HEAD_1_V1_8822B(x) ((x) & (~BITS_BCN_HEAD_1_V1_8822B))
#define BIT_GET_BCN_HEAD_1_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_BCN_HEAD_1_V1_8822B) & BIT_MASK_BCN_HEAD_1_V1_8822B)
#define BIT_SET_BCN_HEAD_1_V1_8822B(x, v)                                      \
	(BIT_CLEAR_BCN_HEAD_1_V1_8822B(x) | BIT_BCN_HEAD_1_V1_8822B(v))

#define BIT_BCN_VALID_V1_8822B BIT(15)

#define BIT_SHIFT_BCN_HEAD_V1_8822B 0
#define BIT_MASK_BCN_HEAD_V1_8822B 0xfff
#define BIT_BCN_HEAD_V1_8822B(x)                                               \
	(((x) & BIT_MASK_BCN_HEAD_V1_8822B) << BIT_SHIFT_BCN_HEAD_V1_8822B)
#define BITS_BCN_HEAD_V1_8822B                                                 \
	(BIT_MASK_BCN_HEAD_V1_8822B << BIT_SHIFT_BCN_HEAD_V1_8822B)
#define BIT_CLEAR_BCN_HEAD_V1_8822B(x) ((x) & (~BITS_BCN_HEAD_V1_8822B))
#define BIT_GET_BCN_HEAD_V1_8822B(x)                                           \
	(((x) >> BIT_SHIFT_BCN_HEAD_V1_8822B) & BIT_MASK_BCN_HEAD_V1_8822B)
#define BIT_SET_BCN_HEAD_V1_8822B(x, v)                                        \
	(BIT_CLEAR_BCN_HEAD_V1_8822B(x) | BIT_BCN_HEAD_V1_8822B(v))

/* 2 REG_AUTO_LLT_V1_8822B */

#define BIT_SHIFT_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8822B 24
#define BIT_MASK_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8822B 0xff
#define BIT_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8822B(x)                            \
	(((x) & BIT_MASK_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8822B)                 \
	 << BIT_SHIFT_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8822B)
#define BITS_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8822B                              \
	(BIT_MASK_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8822B                         \
	 << BIT_SHIFT_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8822B)
#define BIT_CLEAR_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8822B(x)                      \
	((x) & (~BITS_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8822B))
#define BIT_GET_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8822B(x)                        \
	(((x) >> BIT_SHIFT_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8822B) &             \
	 BIT_MASK_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8822B)
#define BIT_SET_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8822B(x, v)                     \
	(BIT_CLEAR_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8822B(x) |                   \
	 BIT_MAX_TX_PKT_FOR_USB_AND_SDIO_V1_8822B(v))

#define BIT_SHIFT_LLT_FREE_PAGE_V1_8822B 8
#define BIT_MASK_LLT_FREE_PAGE_V1_8822B 0xffff
#define BIT_LLT_FREE_PAGE_V1_8822B(x)                                          \
	(((x) & BIT_MASK_LLT_FREE_PAGE_V1_8822B)                               \
	 << BIT_SHIFT_LLT_FREE_PAGE_V1_8822B)
#define BITS_LLT_FREE_PAGE_V1_8822B                                            \
	(BIT_MASK_LLT_FREE_PAGE_V1_8822B << BIT_SHIFT_LLT_FREE_PAGE_V1_8822B)
#define BIT_CLEAR_LLT_FREE_PAGE_V1_8822B(x)                                    \
	((x) & (~BITS_LLT_FREE_PAGE_V1_8822B))
#define BIT_GET_LLT_FREE_PAGE_V1_8822B(x)                                      \
	(((x) >> BIT_SHIFT_LLT_FREE_PAGE_V1_8822B) &                           \
	 BIT_MASK_LLT_FREE_PAGE_V1_8822B)
#define BIT_SET_LLT_FREE_PAGE_V1_8822B(x, v)                                   \
	(BIT_CLEAR_LLT_FREE_PAGE_V1_8822B(x) | BIT_LLT_FREE_PAGE_V1_8822B(v))

#define BIT_SHIFT_BLK_DESC_NUM_8822B 4
#define BIT_MASK_BLK_DESC_NUM_8822B 0xf
#define BIT_BLK_DESC_NUM_8822B(x)                                              \
	(((x) & BIT_MASK_BLK_DESC_NUM_8822B) << BIT_SHIFT_BLK_DESC_NUM_8822B)
#define BITS_BLK_DESC_NUM_8822B                                                \
	(BIT_MASK_BLK_DESC_NUM_8822B << BIT_SHIFT_BLK_DESC_NUM_8822B)
#define BIT_CLEAR_BLK_DESC_NUM_8822B(x) ((x) & (~BITS_BLK_DESC_NUM_8822B))
#define BIT_GET_BLK_DESC_NUM_8822B(x)                                          \
	(((x) >> BIT_SHIFT_BLK_DESC_NUM_8822B) & BIT_MASK_BLK_DESC_NUM_8822B)
#define BIT_SET_BLK_DESC_NUM_8822B(x, v)                                       \
	(BIT_CLEAR_BLK_DESC_NUM_8822B(x) | BIT_BLK_DESC_NUM_8822B(v))

#define BIT_R_BCN_HEAD_SEL_8822B BIT(3)
#define BIT_R_EN_BCN_SW_HEAD_SEL_8822B BIT(2)
#define BIT_LLT_DBG_SEL_8822B BIT(1)
#define BIT_AUTO_INIT_LLT_V1_8822B BIT(0)

/* 2 REG_TXDMA_OFFSET_CHK_8822B */
#define BIT_EM_CHKSUM_FIN_8822B BIT(31)
#define BIT_EMN_PCIE_DMA_MOD_8822B BIT(30)
#define BIT_EN_TXQUE_CLR_8822B BIT(29)
#define BIT_EN_PCIE_FIFO_MODE_8822B BIT(28)

#define BIT_SHIFT_PG_UNDER_TH_V1_8822B 16
#define BIT_MASK_PG_UNDER_TH_V1_8822B 0xfff
#define BIT_PG_UNDER_TH_V1_8822B(x)                                            \
	(((x) & BIT_MASK_PG_UNDER_TH_V1_8822B)                                 \
	 << BIT_SHIFT_PG_UNDER_TH_V1_8822B)
#define BITS_PG_UNDER_TH_V1_8822B                                              \
	(BIT_MASK_PG_UNDER_TH_V1_8822B << BIT_SHIFT_PG_UNDER_TH_V1_8822B)
#define BIT_CLEAR_PG_UNDER_TH_V1_8822B(x) ((x) & (~BITS_PG_UNDER_TH_V1_8822B))
#define BIT_GET_PG_UNDER_TH_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_PG_UNDER_TH_V1_8822B) &                             \
	 BIT_MASK_PG_UNDER_TH_V1_8822B)
#define BIT_SET_PG_UNDER_TH_V1_8822B(x, v)                                     \
	(BIT_CLEAR_PG_UNDER_TH_V1_8822B(x) | BIT_PG_UNDER_TH_V1_8822B(v))

#define BIT_RESTORE_H2C_ADDRESS_8822B BIT(15)
#define BIT_SDIO_TXDESC_CHKSUM_EN_8822B BIT(13)
#define BIT_RST_RDPTR_8822B BIT(12)
#define BIT_RST_WRPTR_8822B BIT(11)
#define BIT_CHK_PG_TH_EN_8822B BIT(10)
#define BIT_DROP_DATA_EN_8822B BIT(9)
#define BIT_CHECK_OFFSET_EN_8822B BIT(8)

#define BIT_SHIFT_CHECK_OFFSET_8822B 0
#define BIT_MASK_CHECK_OFFSET_8822B 0xff
#define BIT_CHECK_OFFSET_8822B(x)                                              \
	(((x) & BIT_MASK_CHECK_OFFSET_8822B) << BIT_SHIFT_CHECK_OFFSET_8822B)
#define BITS_CHECK_OFFSET_8822B                                                \
	(BIT_MASK_CHECK_OFFSET_8822B << BIT_SHIFT_CHECK_OFFSET_8822B)
#define BIT_CLEAR_CHECK_OFFSET_8822B(x) ((x) & (~BITS_CHECK_OFFSET_8822B))
#define BIT_GET_CHECK_OFFSET_8822B(x)                                          \
	(((x) >> BIT_SHIFT_CHECK_OFFSET_8822B) & BIT_MASK_CHECK_OFFSET_8822B)
#define BIT_SET_CHECK_OFFSET_8822B(x, v)                                       \
	(BIT_CLEAR_CHECK_OFFSET_8822B(x) | BIT_CHECK_OFFSET_8822B(v))

/* 2 REG_TXDMA_STATUS_8822B */
#define BIT_HI_OQT_UDN_8822B BIT(17)
#define BIT_HI_OQT_OVF_8822B BIT(16)
#define BIT_PAYLOAD_CHKSUM_ERR_8822B BIT(15)
#define BIT_PAYLOAD_UDN_8822B BIT(14)
#define BIT_PAYLOAD_OVF_8822B BIT(13)
#define BIT_DSC_CHKSUM_FAIL_8822B BIT(12)
#define BIT_UNKNOWN_QSEL_8822B BIT(11)
#define BIT_EP_QSEL_DIFF_8822B BIT(10)
#define BIT_TX_OFFS_UNMATCH_8822B BIT(9)
#define BIT_TXOQT_UDN_8822B BIT(8)
#define BIT_TXOQT_OVF_8822B BIT(7)
#define BIT_TXDMA_SFF_UDN_8822B BIT(6)
#define BIT_TXDMA_SFF_OVF_8822B BIT(5)
#define BIT_LLT_NULL_PG_8822B BIT(4)
#define BIT_PAGE_UDN_8822B BIT(3)
#define BIT_PAGE_OVF_8822B BIT(2)
#define BIT_TXFF_PG_UDN_8822B BIT(1)
#define BIT_TXFF_PG_OVF_8822B BIT(0)

/* 2 REG_TX_DMA_DBG_8822B */

/* 2 REG_TQPNT1_8822B */

#define BIT_SHIFT_HPQ_HIGH_TH_V1_8822B 16
#define BIT_MASK_HPQ_HIGH_TH_V1_8822B 0xfff
#define BIT_HPQ_HIGH_TH_V1_8822B(x)                                            \
	(((x) & BIT_MASK_HPQ_HIGH_TH_V1_8822B)                                 \
	 << BIT_SHIFT_HPQ_HIGH_TH_V1_8822B)
#define BITS_HPQ_HIGH_TH_V1_8822B                                              \
	(BIT_MASK_HPQ_HIGH_TH_V1_8822B << BIT_SHIFT_HPQ_HIGH_TH_V1_8822B)
#define BIT_CLEAR_HPQ_HIGH_TH_V1_8822B(x) ((x) & (~BITS_HPQ_HIGH_TH_V1_8822B))
#define BIT_GET_HPQ_HIGH_TH_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HPQ_HIGH_TH_V1_8822B) &                             \
	 BIT_MASK_HPQ_HIGH_TH_V1_8822B)
#define BIT_SET_HPQ_HIGH_TH_V1_8822B(x, v)                                     \
	(BIT_CLEAR_HPQ_HIGH_TH_V1_8822B(x) | BIT_HPQ_HIGH_TH_V1_8822B(v))

#define BIT_SHIFT_HPQ_LOW_TH_V1_8822B 0
#define BIT_MASK_HPQ_LOW_TH_V1_8822B 0xfff
#define BIT_HPQ_LOW_TH_V1_8822B(x)                                             \
	(((x) & BIT_MASK_HPQ_LOW_TH_V1_8822B) << BIT_SHIFT_HPQ_LOW_TH_V1_8822B)
#define BITS_HPQ_LOW_TH_V1_8822B                                               \
	(BIT_MASK_HPQ_LOW_TH_V1_8822B << BIT_SHIFT_HPQ_LOW_TH_V1_8822B)
#define BIT_CLEAR_HPQ_LOW_TH_V1_8822B(x) ((x) & (~BITS_HPQ_LOW_TH_V1_8822B))
#define BIT_GET_HPQ_LOW_TH_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_HPQ_LOW_TH_V1_8822B) & BIT_MASK_HPQ_LOW_TH_V1_8822B)
#define BIT_SET_HPQ_LOW_TH_V1_8822B(x, v)                                      \
	(BIT_CLEAR_HPQ_LOW_TH_V1_8822B(x) | BIT_HPQ_LOW_TH_V1_8822B(v))

/* 2 REG_TQPNT2_8822B */

#define BIT_SHIFT_NPQ_HIGH_TH_V1_8822B 16
#define BIT_MASK_NPQ_HIGH_TH_V1_8822B 0xfff
#define BIT_NPQ_HIGH_TH_V1_8822B(x)                                            \
	(((x) & BIT_MASK_NPQ_HIGH_TH_V1_8822B)                                 \
	 << BIT_SHIFT_NPQ_HIGH_TH_V1_8822B)
#define BITS_NPQ_HIGH_TH_V1_8822B                                              \
	(BIT_MASK_NPQ_HIGH_TH_V1_8822B << BIT_SHIFT_NPQ_HIGH_TH_V1_8822B)
#define BIT_CLEAR_NPQ_HIGH_TH_V1_8822B(x) ((x) & (~BITS_NPQ_HIGH_TH_V1_8822B))
#define BIT_GET_NPQ_HIGH_TH_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_NPQ_HIGH_TH_V1_8822B) &                             \
	 BIT_MASK_NPQ_HIGH_TH_V1_8822B)
#define BIT_SET_NPQ_HIGH_TH_V1_8822B(x, v)                                     \
	(BIT_CLEAR_NPQ_HIGH_TH_V1_8822B(x) | BIT_NPQ_HIGH_TH_V1_8822B(v))

#define BIT_SHIFT_NPQ_LOW_TH_V1_8822B 0
#define BIT_MASK_NPQ_LOW_TH_V1_8822B 0xfff
#define BIT_NPQ_LOW_TH_V1_8822B(x)                                             \
	(((x) & BIT_MASK_NPQ_LOW_TH_V1_8822B) << BIT_SHIFT_NPQ_LOW_TH_V1_8822B)
#define BITS_NPQ_LOW_TH_V1_8822B                                               \
	(BIT_MASK_NPQ_LOW_TH_V1_8822B << BIT_SHIFT_NPQ_LOW_TH_V1_8822B)
#define BIT_CLEAR_NPQ_LOW_TH_V1_8822B(x) ((x) & (~BITS_NPQ_LOW_TH_V1_8822B))
#define BIT_GET_NPQ_LOW_TH_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_NPQ_LOW_TH_V1_8822B) & BIT_MASK_NPQ_LOW_TH_V1_8822B)
#define BIT_SET_NPQ_LOW_TH_V1_8822B(x, v)                                      \
	(BIT_CLEAR_NPQ_LOW_TH_V1_8822B(x) | BIT_NPQ_LOW_TH_V1_8822B(v))

/* 2 REG_TQPNT3_8822B */

#define BIT_SHIFT_LPQ_HIGH_TH_V1_8822B 16
#define BIT_MASK_LPQ_HIGH_TH_V1_8822B 0xfff
#define BIT_LPQ_HIGH_TH_V1_8822B(x)                                            \
	(((x) & BIT_MASK_LPQ_HIGH_TH_V1_8822B)                                 \
	 << BIT_SHIFT_LPQ_HIGH_TH_V1_8822B)
#define BITS_LPQ_HIGH_TH_V1_8822B                                              \
	(BIT_MASK_LPQ_HIGH_TH_V1_8822B << BIT_SHIFT_LPQ_HIGH_TH_V1_8822B)
#define BIT_CLEAR_LPQ_HIGH_TH_V1_8822B(x) ((x) & (~BITS_LPQ_HIGH_TH_V1_8822B))
#define BIT_GET_LPQ_HIGH_TH_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_LPQ_HIGH_TH_V1_8822B) &                             \
	 BIT_MASK_LPQ_HIGH_TH_V1_8822B)
#define BIT_SET_LPQ_HIGH_TH_V1_8822B(x, v)                                     \
	(BIT_CLEAR_LPQ_HIGH_TH_V1_8822B(x) | BIT_LPQ_HIGH_TH_V1_8822B(v))

#define BIT_SHIFT_LPQ_LOW_TH_V1_8822B 0
#define BIT_MASK_LPQ_LOW_TH_V1_8822B 0xfff
#define BIT_LPQ_LOW_TH_V1_8822B(x)                                             \
	(((x) & BIT_MASK_LPQ_LOW_TH_V1_8822B) << BIT_SHIFT_LPQ_LOW_TH_V1_8822B)
#define BITS_LPQ_LOW_TH_V1_8822B                                               \
	(BIT_MASK_LPQ_LOW_TH_V1_8822B << BIT_SHIFT_LPQ_LOW_TH_V1_8822B)
#define BIT_CLEAR_LPQ_LOW_TH_V1_8822B(x) ((x) & (~BITS_LPQ_LOW_TH_V1_8822B))
#define BIT_GET_LPQ_LOW_TH_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_LPQ_LOW_TH_V1_8822B) & BIT_MASK_LPQ_LOW_TH_V1_8822B)
#define BIT_SET_LPQ_LOW_TH_V1_8822B(x, v)                                      \
	(BIT_CLEAR_LPQ_LOW_TH_V1_8822B(x) | BIT_LPQ_LOW_TH_V1_8822B(v))

/* 2 REG_TQPNT4_8822B */

#define BIT_SHIFT_EXQ_HIGH_TH_V1_8822B 16
#define BIT_MASK_EXQ_HIGH_TH_V1_8822B 0xfff
#define BIT_EXQ_HIGH_TH_V1_8822B(x)                                            \
	(((x) & BIT_MASK_EXQ_HIGH_TH_V1_8822B)                                 \
	 << BIT_SHIFT_EXQ_HIGH_TH_V1_8822B)
#define BITS_EXQ_HIGH_TH_V1_8822B                                              \
	(BIT_MASK_EXQ_HIGH_TH_V1_8822B << BIT_SHIFT_EXQ_HIGH_TH_V1_8822B)
#define BIT_CLEAR_EXQ_HIGH_TH_V1_8822B(x) ((x) & (~BITS_EXQ_HIGH_TH_V1_8822B))
#define BIT_GET_EXQ_HIGH_TH_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_EXQ_HIGH_TH_V1_8822B) &                             \
	 BIT_MASK_EXQ_HIGH_TH_V1_8822B)
#define BIT_SET_EXQ_HIGH_TH_V1_8822B(x, v)                                     \
	(BIT_CLEAR_EXQ_HIGH_TH_V1_8822B(x) | BIT_EXQ_HIGH_TH_V1_8822B(v))

#define BIT_SHIFT_EXQ_LOW_TH_V1_8822B 0
#define BIT_MASK_EXQ_LOW_TH_V1_8822B 0xfff
#define BIT_EXQ_LOW_TH_V1_8822B(x)                                             \
	(((x) & BIT_MASK_EXQ_LOW_TH_V1_8822B) << BIT_SHIFT_EXQ_LOW_TH_V1_8822B)
#define BITS_EXQ_LOW_TH_V1_8822B                                               \
	(BIT_MASK_EXQ_LOW_TH_V1_8822B << BIT_SHIFT_EXQ_LOW_TH_V1_8822B)
#define BIT_CLEAR_EXQ_LOW_TH_V1_8822B(x) ((x) & (~BITS_EXQ_LOW_TH_V1_8822B))
#define BIT_GET_EXQ_LOW_TH_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_EXQ_LOW_TH_V1_8822B) & BIT_MASK_EXQ_LOW_TH_V1_8822B)
#define BIT_SET_EXQ_LOW_TH_V1_8822B(x, v)                                      \
	(BIT_CLEAR_EXQ_LOW_TH_V1_8822B(x) | BIT_EXQ_LOW_TH_V1_8822B(v))

/* 2 REG_RQPN_CTRL_1_8822B */

#define BIT_SHIFT_TXPKTNUM_H_8822B 16
#define BIT_MASK_TXPKTNUM_H_8822B 0xffff
#define BIT_TXPKTNUM_H_8822B(x)                                                \
	(((x) & BIT_MASK_TXPKTNUM_H_8822B) << BIT_SHIFT_TXPKTNUM_H_8822B)
#define BITS_TXPKTNUM_H_8822B                                                  \
	(BIT_MASK_TXPKTNUM_H_8822B << BIT_SHIFT_TXPKTNUM_H_8822B)
#define BIT_CLEAR_TXPKTNUM_H_8822B(x) ((x) & (~BITS_TXPKTNUM_H_8822B))
#define BIT_GET_TXPKTNUM_H_8822B(x)                                            \
	(((x) >> BIT_SHIFT_TXPKTNUM_H_8822B) & BIT_MASK_TXPKTNUM_H_8822B)
#define BIT_SET_TXPKTNUM_H_8822B(x, v)                                         \
	(BIT_CLEAR_TXPKTNUM_H_8822B(x) | BIT_TXPKTNUM_H_8822B(v))

#define BIT_SHIFT_TXPKTNUM_V2_8822B 0
#define BIT_MASK_TXPKTNUM_V2_8822B 0xffff
#define BIT_TXPKTNUM_V2_8822B(x)                                               \
	(((x) & BIT_MASK_TXPKTNUM_V2_8822B) << BIT_SHIFT_TXPKTNUM_V2_8822B)
#define BITS_TXPKTNUM_V2_8822B                                                 \
	(BIT_MASK_TXPKTNUM_V2_8822B << BIT_SHIFT_TXPKTNUM_V2_8822B)
#define BIT_CLEAR_TXPKTNUM_V2_8822B(x) ((x) & (~BITS_TXPKTNUM_V2_8822B))
#define BIT_GET_TXPKTNUM_V2_8822B(x)                                           \
	(((x) >> BIT_SHIFT_TXPKTNUM_V2_8822B) & BIT_MASK_TXPKTNUM_V2_8822B)
#define BIT_SET_TXPKTNUM_V2_8822B(x, v)                                        \
	(BIT_CLEAR_TXPKTNUM_V2_8822B(x) | BIT_TXPKTNUM_V2_8822B(v))

/* 2 REG_RQPN_CTRL_2_8822B */
#define BIT_LD_RQPN_8822B BIT(31)
#define BIT_EXQ_PUBLIC_DIS_V1_8822B BIT(19)
#define BIT_NPQ_PUBLIC_DIS_V1_8822B BIT(18)
#define BIT_LPQ_PUBLIC_DIS_V1_8822B BIT(17)
#define BIT_HPQ_PUBLIC_DIS_V1_8822B BIT(16)

/* 2 REG_FIFOPAGE_INFO_1_8822B */

#define BIT_SHIFT_HPQ_AVAL_PG_V1_8822B 16
#define BIT_MASK_HPQ_AVAL_PG_V1_8822B 0xfff
#define BIT_HPQ_AVAL_PG_V1_8822B(x)                                            \
	(((x) & BIT_MASK_HPQ_AVAL_PG_V1_8822B)                                 \
	 << BIT_SHIFT_HPQ_AVAL_PG_V1_8822B)
#define BITS_HPQ_AVAL_PG_V1_8822B                                              \
	(BIT_MASK_HPQ_AVAL_PG_V1_8822B << BIT_SHIFT_HPQ_AVAL_PG_V1_8822B)
#define BIT_CLEAR_HPQ_AVAL_PG_V1_8822B(x) ((x) & (~BITS_HPQ_AVAL_PG_V1_8822B))
#define BIT_GET_HPQ_AVAL_PG_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HPQ_AVAL_PG_V1_8822B) &                             \
	 BIT_MASK_HPQ_AVAL_PG_V1_8822B)
#define BIT_SET_HPQ_AVAL_PG_V1_8822B(x, v)                                     \
	(BIT_CLEAR_HPQ_AVAL_PG_V1_8822B(x) | BIT_HPQ_AVAL_PG_V1_8822B(v))

#define BIT_SHIFT_HPQ_V1_8822B 0
#define BIT_MASK_HPQ_V1_8822B 0xfff
#define BIT_HPQ_V1_8822B(x)                                                    \
	(((x) & BIT_MASK_HPQ_V1_8822B) << BIT_SHIFT_HPQ_V1_8822B)
#define BITS_HPQ_V1_8822B (BIT_MASK_HPQ_V1_8822B << BIT_SHIFT_HPQ_V1_8822B)
#define BIT_CLEAR_HPQ_V1_8822B(x) ((x) & (~BITS_HPQ_V1_8822B))
#define BIT_GET_HPQ_V1_8822B(x)                                                \
	(((x) >> BIT_SHIFT_HPQ_V1_8822B) & BIT_MASK_HPQ_V1_8822B)
#define BIT_SET_HPQ_V1_8822B(x, v)                                             \
	(BIT_CLEAR_HPQ_V1_8822B(x) | BIT_HPQ_V1_8822B(v))

/* 2 REG_FIFOPAGE_INFO_2_8822B */

#define BIT_SHIFT_LPQ_AVAL_PG_V1_8822B 16
#define BIT_MASK_LPQ_AVAL_PG_V1_8822B 0xfff
#define BIT_LPQ_AVAL_PG_V1_8822B(x)                                            \
	(((x) & BIT_MASK_LPQ_AVAL_PG_V1_8822B)                                 \
	 << BIT_SHIFT_LPQ_AVAL_PG_V1_8822B)
#define BITS_LPQ_AVAL_PG_V1_8822B                                              \
	(BIT_MASK_LPQ_AVAL_PG_V1_8822B << BIT_SHIFT_LPQ_AVAL_PG_V1_8822B)
#define BIT_CLEAR_LPQ_AVAL_PG_V1_8822B(x) ((x) & (~BITS_LPQ_AVAL_PG_V1_8822B))
#define BIT_GET_LPQ_AVAL_PG_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_LPQ_AVAL_PG_V1_8822B) &                             \
	 BIT_MASK_LPQ_AVAL_PG_V1_8822B)
#define BIT_SET_LPQ_AVAL_PG_V1_8822B(x, v)                                     \
	(BIT_CLEAR_LPQ_AVAL_PG_V1_8822B(x) | BIT_LPQ_AVAL_PG_V1_8822B(v))

#define BIT_SHIFT_LPQ_V1_8822B 0
#define BIT_MASK_LPQ_V1_8822B 0xfff
#define BIT_LPQ_V1_8822B(x)                                                    \
	(((x) & BIT_MASK_LPQ_V1_8822B) << BIT_SHIFT_LPQ_V1_8822B)
#define BITS_LPQ_V1_8822B (BIT_MASK_LPQ_V1_8822B << BIT_SHIFT_LPQ_V1_8822B)
#define BIT_CLEAR_LPQ_V1_8822B(x) ((x) & (~BITS_LPQ_V1_8822B))
#define BIT_GET_LPQ_V1_8822B(x)                                                \
	(((x) >> BIT_SHIFT_LPQ_V1_8822B) & BIT_MASK_LPQ_V1_8822B)
#define BIT_SET_LPQ_V1_8822B(x, v)                                             \
	(BIT_CLEAR_LPQ_V1_8822B(x) | BIT_LPQ_V1_8822B(v))

/* 2 REG_FIFOPAGE_INFO_3_8822B */

#define BIT_SHIFT_NPQ_AVAL_PG_V1_8822B 16
#define BIT_MASK_NPQ_AVAL_PG_V1_8822B 0xfff
#define BIT_NPQ_AVAL_PG_V1_8822B(x)                                            \
	(((x) & BIT_MASK_NPQ_AVAL_PG_V1_8822B)                                 \
	 << BIT_SHIFT_NPQ_AVAL_PG_V1_8822B)
#define BITS_NPQ_AVAL_PG_V1_8822B                                              \
	(BIT_MASK_NPQ_AVAL_PG_V1_8822B << BIT_SHIFT_NPQ_AVAL_PG_V1_8822B)
#define BIT_CLEAR_NPQ_AVAL_PG_V1_8822B(x) ((x) & (~BITS_NPQ_AVAL_PG_V1_8822B))
#define BIT_GET_NPQ_AVAL_PG_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_NPQ_AVAL_PG_V1_8822B) &                             \
	 BIT_MASK_NPQ_AVAL_PG_V1_8822B)
#define BIT_SET_NPQ_AVAL_PG_V1_8822B(x, v)                                     \
	(BIT_CLEAR_NPQ_AVAL_PG_V1_8822B(x) | BIT_NPQ_AVAL_PG_V1_8822B(v))

#define BIT_SHIFT_NPQ_V1_8822B 0
#define BIT_MASK_NPQ_V1_8822B 0xfff
#define BIT_NPQ_V1_8822B(x)                                                    \
	(((x) & BIT_MASK_NPQ_V1_8822B) << BIT_SHIFT_NPQ_V1_8822B)
#define BITS_NPQ_V1_8822B (BIT_MASK_NPQ_V1_8822B << BIT_SHIFT_NPQ_V1_8822B)
#define BIT_CLEAR_NPQ_V1_8822B(x) ((x) & (~BITS_NPQ_V1_8822B))
#define BIT_GET_NPQ_V1_8822B(x)                                                \
	(((x) >> BIT_SHIFT_NPQ_V1_8822B) & BIT_MASK_NPQ_V1_8822B)
#define BIT_SET_NPQ_V1_8822B(x, v)                                             \
	(BIT_CLEAR_NPQ_V1_8822B(x) | BIT_NPQ_V1_8822B(v))

/* 2 REG_FIFOPAGE_INFO_4_8822B */

#define BIT_SHIFT_EXQ_AVAL_PG_V1_8822B 16
#define BIT_MASK_EXQ_AVAL_PG_V1_8822B 0xfff
#define BIT_EXQ_AVAL_PG_V1_8822B(x)                                            \
	(((x) & BIT_MASK_EXQ_AVAL_PG_V1_8822B)                                 \
	 << BIT_SHIFT_EXQ_AVAL_PG_V1_8822B)
#define BITS_EXQ_AVAL_PG_V1_8822B                                              \
	(BIT_MASK_EXQ_AVAL_PG_V1_8822B << BIT_SHIFT_EXQ_AVAL_PG_V1_8822B)
#define BIT_CLEAR_EXQ_AVAL_PG_V1_8822B(x) ((x) & (~BITS_EXQ_AVAL_PG_V1_8822B))
#define BIT_GET_EXQ_AVAL_PG_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_EXQ_AVAL_PG_V1_8822B) &                             \
	 BIT_MASK_EXQ_AVAL_PG_V1_8822B)
#define BIT_SET_EXQ_AVAL_PG_V1_8822B(x, v)                                     \
	(BIT_CLEAR_EXQ_AVAL_PG_V1_8822B(x) | BIT_EXQ_AVAL_PG_V1_8822B(v))

#define BIT_SHIFT_EXQ_V1_8822B 0
#define BIT_MASK_EXQ_V1_8822B 0xfff
#define BIT_EXQ_V1_8822B(x)                                                    \
	(((x) & BIT_MASK_EXQ_V1_8822B) << BIT_SHIFT_EXQ_V1_8822B)
#define BITS_EXQ_V1_8822B (BIT_MASK_EXQ_V1_8822B << BIT_SHIFT_EXQ_V1_8822B)
#define BIT_CLEAR_EXQ_V1_8822B(x) ((x) & (~BITS_EXQ_V1_8822B))
#define BIT_GET_EXQ_V1_8822B(x)                                                \
	(((x) >> BIT_SHIFT_EXQ_V1_8822B) & BIT_MASK_EXQ_V1_8822B)
#define BIT_SET_EXQ_V1_8822B(x, v)                                             \
	(BIT_CLEAR_EXQ_V1_8822B(x) | BIT_EXQ_V1_8822B(v))

/* 2 REG_FIFOPAGE_INFO_5_8822B */

#define BIT_SHIFT_PUBQ_AVAL_PG_V1_8822B 16
#define BIT_MASK_PUBQ_AVAL_PG_V1_8822B 0xfff
#define BIT_PUBQ_AVAL_PG_V1_8822B(x)                                           \
	(((x) & BIT_MASK_PUBQ_AVAL_PG_V1_8822B)                                \
	 << BIT_SHIFT_PUBQ_AVAL_PG_V1_8822B)
#define BITS_PUBQ_AVAL_PG_V1_8822B                                             \
	(BIT_MASK_PUBQ_AVAL_PG_V1_8822B << BIT_SHIFT_PUBQ_AVAL_PG_V1_8822B)
#define BIT_CLEAR_PUBQ_AVAL_PG_V1_8822B(x) ((x) & (~BITS_PUBQ_AVAL_PG_V1_8822B))
#define BIT_GET_PUBQ_AVAL_PG_V1_8822B(x)                                       \
	(((x) >> BIT_SHIFT_PUBQ_AVAL_PG_V1_8822B) &                            \
	 BIT_MASK_PUBQ_AVAL_PG_V1_8822B)
#define BIT_SET_PUBQ_AVAL_PG_V1_8822B(x, v)                                    \
	(BIT_CLEAR_PUBQ_AVAL_PG_V1_8822B(x) | BIT_PUBQ_AVAL_PG_V1_8822B(v))

#define BIT_SHIFT_PUBQ_V1_8822B 0
#define BIT_MASK_PUBQ_V1_8822B 0xfff
#define BIT_PUBQ_V1_8822B(x)                                                   \
	(((x) & BIT_MASK_PUBQ_V1_8822B) << BIT_SHIFT_PUBQ_V1_8822B)
#define BITS_PUBQ_V1_8822B (BIT_MASK_PUBQ_V1_8822B << BIT_SHIFT_PUBQ_V1_8822B)
#define BIT_CLEAR_PUBQ_V1_8822B(x) ((x) & (~BITS_PUBQ_V1_8822B))
#define BIT_GET_PUBQ_V1_8822B(x)                                               \
	(((x) >> BIT_SHIFT_PUBQ_V1_8822B) & BIT_MASK_PUBQ_V1_8822B)
#define BIT_SET_PUBQ_V1_8822B(x, v)                                            \
	(BIT_CLEAR_PUBQ_V1_8822B(x) | BIT_PUBQ_V1_8822B(v))

/* 2 REG_H2C_HEAD_8822B */

#define BIT_SHIFT_H2C_HEAD_8822B 0
#define BIT_MASK_H2C_HEAD_8822B 0x3ffff
#define BIT_H2C_HEAD_8822B(x)                                                  \
	(((x) & BIT_MASK_H2C_HEAD_8822B) << BIT_SHIFT_H2C_HEAD_8822B)
#define BITS_H2C_HEAD_8822B                                                    \
	(BIT_MASK_H2C_HEAD_8822B << BIT_SHIFT_H2C_HEAD_8822B)
#define BIT_CLEAR_H2C_HEAD_8822B(x) ((x) & (~BITS_H2C_HEAD_8822B))
#define BIT_GET_H2C_HEAD_8822B(x)                                              \
	(((x) >> BIT_SHIFT_H2C_HEAD_8822B) & BIT_MASK_H2C_HEAD_8822B)
#define BIT_SET_H2C_HEAD_8822B(x, v)                                           \
	(BIT_CLEAR_H2C_HEAD_8822B(x) | BIT_H2C_HEAD_8822B(v))

/* 2 REG_H2C_TAIL_8822B */

#define BIT_SHIFT_H2C_TAIL_8822B 0
#define BIT_MASK_H2C_TAIL_8822B 0x3ffff
#define BIT_H2C_TAIL_8822B(x)                                                  \
	(((x) & BIT_MASK_H2C_TAIL_8822B) << BIT_SHIFT_H2C_TAIL_8822B)
#define BITS_H2C_TAIL_8822B                                                    \
	(BIT_MASK_H2C_TAIL_8822B << BIT_SHIFT_H2C_TAIL_8822B)
#define BIT_CLEAR_H2C_TAIL_8822B(x) ((x) & (~BITS_H2C_TAIL_8822B))
#define BIT_GET_H2C_TAIL_8822B(x)                                              \
	(((x) >> BIT_SHIFT_H2C_TAIL_8822B) & BIT_MASK_H2C_TAIL_8822B)
#define BIT_SET_H2C_TAIL_8822B(x, v)                                           \
	(BIT_CLEAR_H2C_TAIL_8822B(x) | BIT_H2C_TAIL_8822B(v))

/* 2 REG_H2C_READ_ADDR_8822B */

#define BIT_SHIFT_H2C_READ_ADDR_8822B 0
#define BIT_MASK_H2C_READ_ADDR_8822B 0x3ffff
#define BIT_H2C_READ_ADDR_8822B(x)                                             \
	(((x) & BIT_MASK_H2C_READ_ADDR_8822B) << BIT_SHIFT_H2C_READ_ADDR_8822B)
#define BITS_H2C_READ_ADDR_8822B                                               \
	(BIT_MASK_H2C_READ_ADDR_8822B << BIT_SHIFT_H2C_READ_ADDR_8822B)
#define BIT_CLEAR_H2C_READ_ADDR_8822B(x) ((x) & (~BITS_H2C_READ_ADDR_8822B))
#define BIT_GET_H2C_READ_ADDR_8822B(x)                                         \
	(((x) >> BIT_SHIFT_H2C_READ_ADDR_8822B) & BIT_MASK_H2C_READ_ADDR_8822B)
#define BIT_SET_H2C_READ_ADDR_8822B(x, v)                                      \
	(BIT_CLEAR_H2C_READ_ADDR_8822B(x) | BIT_H2C_READ_ADDR_8822B(v))

/* 2 REG_H2C_WR_ADDR_8822B */

#define BIT_SHIFT_H2C_WR_ADDR_8822B 0
#define BIT_MASK_H2C_WR_ADDR_8822B 0x3ffff
#define BIT_H2C_WR_ADDR_8822B(x)                                               \
	(((x) & BIT_MASK_H2C_WR_ADDR_8822B) << BIT_SHIFT_H2C_WR_ADDR_8822B)
#define BITS_H2C_WR_ADDR_8822B                                                 \
	(BIT_MASK_H2C_WR_ADDR_8822B << BIT_SHIFT_H2C_WR_ADDR_8822B)
#define BIT_CLEAR_H2C_WR_ADDR_8822B(x) ((x) & (~BITS_H2C_WR_ADDR_8822B))
#define BIT_GET_H2C_WR_ADDR_8822B(x)                                           \
	(((x) >> BIT_SHIFT_H2C_WR_ADDR_8822B) & BIT_MASK_H2C_WR_ADDR_8822B)
#define BIT_SET_H2C_WR_ADDR_8822B(x, v)                                        \
	(BIT_CLEAR_H2C_WR_ADDR_8822B(x) | BIT_H2C_WR_ADDR_8822B(v))

/* 2 REG_H2C_INFO_8822B */
#define BIT_H2C_SPACE_VLD_8822B BIT(3)
#define BIT_H2C_WR_ADDR_RST_8822B BIT(2)

#define BIT_SHIFT_H2C_LEN_SEL_8822B 0
#define BIT_MASK_H2C_LEN_SEL_8822B 0x3
#define BIT_H2C_LEN_SEL_8822B(x)                                               \
	(((x) & BIT_MASK_H2C_LEN_SEL_8822B) << BIT_SHIFT_H2C_LEN_SEL_8822B)
#define BITS_H2C_LEN_SEL_8822B                                                 \
	(BIT_MASK_H2C_LEN_SEL_8822B << BIT_SHIFT_H2C_LEN_SEL_8822B)
#define BIT_CLEAR_H2C_LEN_SEL_8822B(x) ((x) & (~BITS_H2C_LEN_SEL_8822B))
#define BIT_GET_H2C_LEN_SEL_8822B(x)                                           \
	(((x) >> BIT_SHIFT_H2C_LEN_SEL_8822B) & BIT_MASK_H2C_LEN_SEL_8822B)
#define BIT_SET_H2C_LEN_SEL_8822B(x, v)                                        \
	(BIT_CLEAR_H2C_LEN_SEL_8822B(x) | BIT_H2C_LEN_SEL_8822B(v))

/* 2 REG_RXDMA_AGG_PG_TH_8822B */
#define BIT_USB_RXDMA_AGG_EN_8822B BIT(31)
#define BIT_EN_PRE_CALC_8822B BIT(29)
#define BIT_RXAGG_SW_EN_8822B BIT(28)
#define BIT_RXAGG_SW_TRIG_8822B BIT(27)

#define BIT_SHIFT_PKT_NUM_WOL_8822B 16
#define BIT_MASK_PKT_NUM_WOL_8822B 0xff
#define BIT_PKT_NUM_WOL_8822B(x)                                               \
	(((x) & BIT_MASK_PKT_NUM_WOL_8822B) << BIT_SHIFT_PKT_NUM_WOL_8822B)
#define BITS_PKT_NUM_WOL_8822B                                                 \
	(BIT_MASK_PKT_NUM_WOL_8822B << BIT_SHIFT_PKT_NUM_WOL_8822B)
#define BIT_CLEAR_PKT_NUM_WOL_8822B(x) ((x) & (~BITS_PKT_NUM_WOL_8822B))
#define BIT_GET_PKT_NUM_WOL_8822B(x)                                           \
	(((x) >> BIT_SHIFT_PKT_NUM_WOL_8822B) & BIT_MASK_PKT_NUM_WOL_8822B)
#define BIT_SET_PKT_NUM_WOL_8822B(x, v)                                        \
	(BIT_CLEAR_PKT_NUM_WOL_8822B(x) | BIT_PKT_NUM_WOL_8822B(v))

#define BIT_SHIFT_DMA_AGG_TO_V1_8822B 8
#define BIT_MASK_DMA_AGG_TO_V1_8822B 0xff
#define BIT_DMA_AGG_TO_V1_8822B(x)                                             \
	(((x) & BIT_MASK_DMA_AGG_TO_V1_8822B) << BIT_SHIFT_DMA_AGG_TO_V1_8822B)
#define BITS_DMA_AGG_TO_V1_8822B                                               \
	(BIT_MASK_DMA_AGG_TO_V1_8822B << BIT_SHIFT_DMA_AGG_TO_V1_8822B)
#define BIT_CLEAR_DMA_AGG_TO_V1_8822B(x) ((x) & (~BITS_DMA_AGG_TO_V1_8822B))
#define BIT_GET_DMA_AGG_TO_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_DMA_AGG_TO_V1_8822B) & BIT_MASK_DMA_AGG_TO_V1_8822B)
#define BIT_SET_DMA_AGG_TO_V1_8822B(x, v)                                      \
	(BIT_CLEAR_DMA_AGG_TO_V1_8822B(x) | BIT_DMA_AGG_TO_V1_8822B(v))

#define BIT_SHIFT_RXDMA_AGG_PG_TH_8822B 0
#define BIT_MASK_RXDMA_AGG_PG_TH_8822B 0xff
#define BIT_RXDMA_AGG_PG_TH_8822B(x)                                           \
	(((x) & BIT_MASK_RXDMA_AGG_PG_TH_8822B)                                \
	 << BIT_SHIFT_RXDMA_AGG_PG_TH_8822B)
#define BITS_RXDMA_AGG_PG_TH_8822B                                             \
	(BIT_MASK_RXDMA_AGG_PG_TH_8822B << BIT_SHIFT_RXDMA_AGG_PG_TH_8822B)
#define BIT_CLEAR_RXDMA_AGG_PG_TH_8822B(x) ((x) & (~BITS_RXDMA_AGG_PG_TH_8822B))
#define BIT_GET_RXDMA_AGG_PG_TH_8822B(x)                                       \
	(((x) >> BIT_SHIFT_RXDMA_AGG_PG_TH_8822B) &                            \
	 BIT_MASK_RXDMA_AGG_PG_TH_8822B)
#define BIT_SET_RXDMA_AGG_PG_TH_8822B(x, v)                                    \
	(BIT_CLEAR_RXDMA_AGG_PG_TH_8822B(x) | BIT_RXDMA_AGG_PG_TH_8822B(v))

/* 2 REG_RXPKT_NUM_8822B */

#define BIT_SHIFT_RXPKT_NUM_8822B 24
#define BIT_MASK_RXPKT_NUM_8822B 0xff
#define BIT_RXPKT_NUM_8822B(x)                                                 \
	(((x) & BIT_MASK_RXPKT_NUM_8822B) << BIT_SHIFT_RXPKT_NUM_8822B)
#define BITS_RXPKT_NUM_8822B                                                   \
	(BIT_MASK_RXPKT_NUM_8822B << BIT_SHIFT_RXPKT_NUM_8822B)
#define BIT_CLEAR_RXPKT_NUM_8822B(x) ((x) & (~BITS_RXPKT_NUM_8822B))
#define BIT_GET_RXPKT_NUM_8822B(x)                                             \
	(((x) >> BIT_SHIFT_RXPKT_NUM_8822B) & BIT_MASK_RXPKT_NUM_8822B)
#define BIT_SET_RXPKT_NUM_8822B(x, v)                                          \
	(BIT_CLEAR_RXPKT_NUM_8822B(x) | BIT_RXPKT_NUM_8822B(v))

#define BIT_SHIFT_FW_UPD_RDPTR19_TO_16_8822B 20
#define BIT_MASK_FW_UPD_RDPTR19_TO_16_8822B 0xf
#define BIT_FW_UPD_RDPTR19_TO_16_8822B(x)                                      \
	(((x) & BIT_MASK_FW_UPD_RDPTR19_TO_16_8822B)                           \
	 << BIT_SHIFT_FW_UPD_RDPTR19_TO_16_8822B)
#define BITS_FW_UPD_RDPTR19_TO_16_8822B                                        \
	(BIT_MASK_FW_UPD_RDPTR19_TO_16_8822B                                   \
	 << BIT_SHIFT_FW_UPD_RDPTR19_TO_16_8822B)
#define BIT_CLEAR_FW_UPD_RDPTR19_TO_16_8822B(x)                                \
	((x) & (~BITS_FW_UPD_RDPTR19_TO_16_8822B))
#define BIT_GET_FW_UPD_RDPTR19_TO_16_8822B(x)                                  \
	(((x) >> BIT_SHIFT_FW_UPD_RDPTR19_TO_16_8822B) &                       \
	 BIT_MASK_FW_UPD_RDPTR19_TO_16_8822B)
#define BIT_SET_FW_UPD_RDPTR19_TO_16_8822B(x, v)                               \
	(BIT_CLEAR_FW_UPD_RDPTR19_TO_16_8822B(x) |                             \
	 BIT_FW_UPD_RDPTR19_TO_16_8822B(v))

#define BIT_RXDMA_REQ_8822B BIT(19)
#define BIT_RW_RELEASE_EN_8822B BIT(18)
#define BIT_RXDMA_IDLE_8822B BIT(17)
#define BIT_RXPKT_RELEASE_POLL_8822B BIT(16)

#define BIT_SHIFT_FW_UPD_RDPTR_8822B 0
#define BIT_MASK_FW_UPD_RDPTR_8822B 0xffff
#define BIT_FW_UPD_RDPTR_8822B(x)                                              \
	(((x) & BIT_MASK_FW_UPD_RDPTR_8822B) << BIT_SHIFT_FW_UPD_RDPTR_8822B)
#define BITS_FW_UPD_RDPTR_8822B                                                \
	(BIT_MASK_FW_UPD_RDPTR_8822B << BIT_SHIFT_FW_UPD_RDPTR_8822B)
#define BIT_CLEAR_FW_UPD_RDPTR_8822B(x) ((x) & (~BITS_FW_UPD_RDPTR_8822B))
#define BIT_GET_FW_UPD_RDPTR_8822B(x)                                          \
	(((x) >> BIT_SHIFT_FW_UPD_RDPTR_8822B) & BIT_MASK_FW_UPD_RDPTR_8822B)
#define BIT_SET_FW_UPD_RDPTR_8822B(x, v)                                       \
	(BIT_CLEAR_FW_UPD_RDPTR_8822B(x) | BIT_FW_UPD_RDPTR_8822B(v))

/* 2 REG_RXDMA_STATUS_8822B */
#define BIT_C2H_PKT_OVF_8822B BIT(7)
#define BIT_AGG_CONFGI_ISSUE_8822B BIT(6)
#define BIT_FW_POLL_ISSUE_8822B BIT(5)
#define BIT_RX_DATA_UDN_8822B BIT(4)
#define BIT_RX_SFF_UDN_8822B BIT(3)
#define BIT_RX_SFF_OVF_8822B BIT(2)
#define BIT_RXPKT_OVF_8822B BIT(0)

/* 2 REG_RXDMA_DPR_8822B */

#define BIT_SHIFT_RDE_DEBUG_8822B 0
#define BIT_MASK_RDE_DEBUG_8822B 0xffffffffL
#define BIT_RDE_DEBUG_8822B(x)                                                 \
	(((x) & BIT_MASK_RDE_DEBUG_8822B) << BIT_SHIFT_RDE_DEBUG_8822B)
#define BITS_RDE_DEBUG_8822B                                                   \
	(BIT_MASK_RDE_DEBUG_8822B << BIT_SHIFT_RDE_DEBUG_8822B)
#define BIT_CLEAR_RDE_DEBUG_8822B(x) ((x) & (~BITS_RDE_DEBUG_8822B))
#define BIT_GET_RDE_DEBUG_8822B(x)                                             \
	(((x) >> BIT_SHIFT_RDE_DEBUG_8822B) & BIT_MASK_RDE_DEBUG_8822B)
#define BIT_SET_RDE_DEBUG_8822B(x, v)                                          \
	(BIT_CLEAR_RDE_DEBUG_8822B(x) | BIT_RDE_DEBUG_8822B(v))

/* 2 REG_RXDMA_MODE_8822B */

#define BIT_SHIFT_PKTNUM_TH_V2_8822B 24
#define BIT_MASK_PKTNUM_TH_V2_8822B 0x1f
#define BIT_PKTNUM_TH_V2_8822B(x)                                              \
	(((x) & BIT_MASK_PKTNUM_TH_V2_8822B) << BIT_SHIFT_PKTNUM_TH_V2_8822B)
#define BITS_PKTNUM_TH_V2_8822B                                                \
	(BIT_MASK_PKTNUM_TH_V2_8822B << BIT_SHIFT_PKTNUM_TH_V2_8822B)
#define BIT_CLEAR_PKTNUM_TH_V2_8822B(x) ((x) & (~BITS_PKTNUM_TH_V2_8822B))
#define BIT_GET_PKTNUM_TH_V2_8822B(x)                                          \
	(((x) >> BIT_SHIFT_PKTNUM_TH_V2_8822B) & BIT_MASK_PKTNUM_TH_V2_8822B)
#define BIT_SET_PKTNUM_TH_V2_8822B(x, v)                                       \
	(BIT_CLEAR_PKTNUM_TH_V2_8822B(x) | BIT_PKTNUM_TH_V2_8822B(v))

#define BIT_TXBA_BREAK_USBAGG_8822B BIT(23)

#define BIT_SHIFT_PKTLEN_PARA_8822B 16
#define BIT_MASK_PKTLEN_PARA_8822B 0x7
#define BIT_PKTLEN_PARA_8822B(x)                                               \
	(((x) & BIT_MASK_PKTLEN_PARA_8822B) << BIT_SHIFT_PKTLEN_PARA_8822B)
#define BITS_PKTLEN_PARA_8822B                                                 \
	(BIT_MASK_PKTLEN_PARA_8822B << BIT_SHIFT_PKTLEN_PARA_8822B)
#define BIT_CLEAR_PKTLEN_PARA_8822B(x) ((x) & (~BITS_PKTLEN_PARA_8822B))
#define BIT_GET_PKTLEN_PARA_8822B(x)                                           \
	(((x) >> BIT_SHIFT_PKTLEN_PARA_8822B) & BIT_MASK_PKTLEN_PARA_8822B)
#define BIT_SET_PKTLEN_PARA_8822B(x, v)                                        \
	(BIT_CLEAR_PKTLEN_PARA_8822B(x) | BIT_PKTLEN_PARA_8822B(v))

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_NOT_VALID_8822B */

#define BIT_SHIFT_BURST_SIZE_8822B 4
#define BIT_MASK_BURST_SIZE_8822B 0x3
#define BIT_BURST_SIZE_8822B(x)                                                \
	(((x) & BIT_MASK_BURST_SIZE_8822B) << BIT_SHIFT_BURST_SIZE_8822B)
#define BITS_BURST_SIZE_8822B                                                  \
	(BIT_MASK_BURST_SIZE_8822B << BIT_SHIFT_BURST_SIZE_8822B)
#define BIT_CLEAR_BURST_SIZE_8822B(x) ((x) & (~BITS_BURST_SIZE_8822B))
#define BIT_GET_BURST_SIZE_8822B(x)                                            \
	(((x) >> BIT_SHIFT_BURST_SIZE_8822B) & BIT_MASK_BURST_SIZE_8822B)
#define BIT_SET_BURST_SIZE_8822B(x, v)                                         \
	(BIT_CLEAR_BURST_SIZE_8822B(x) | BIT_BURST_SIZE_8822B(v))

#define BIT_SHIFT_BURST_CNT_8822B 2
#define BIT_MASK_BURST_CNT_8822B 0x3
#define BIT_BURST_CNT_8822B(x)                                                 \
	(((x) & BIT_MASK_BURST_CNT_8822B) << BIT_SHIFT_BURST_CNT_8822B)
#define BITS_BURST_CNT_8822B                                                   \
	(BIT_MASK_BURST_CNT_8822B << BIT_SHIFT_BURST_CNT_8822B)
#define BIT_CLEAR_BURST_CNT_8822B(x) ((x) & (~BITS_BURST_CNT_8822B))
#define BIT_GET_BURST_CNT_8822B(x)                                             \
	(((x) >> BIT_SHIFT_BURST_CNT_8822B) & BIT_MASK_BURST_CNT_8822B)
#define BIT_SET_BURST_CNT_8822B(x, v)                                          \
	(BIT_CLEAR_BURST_CNT_8822B(x) | BIT_BURST_CNT_8822B(v))

#define BIT_DMA_MODE_8822B BIT(1)

/* 2 REG_C2H_PKT_8822B */

#define BIT_SHIFT_R_C2H_STR_ADDR_16_TO_19_8822B 24
#define BIT_MASK_R_C2H_STR_ADDR_16_TO_19_8822B 0xf
#define BIT_R_C2H_STR_ADDR_16_TO_19_8822B(x)                                   \
	(((x) & BIT_MASK_R_C2H_STR_ADDR_16_TO_19_8822B)                        \
	 << BIT_SHIFT_R_C2H_STR_ADDR_16_TO_19_8822B)
#define BITS_R_C2H_STR_ADDR_16_TO_19_8822B                                     \
	(BIT_MASK_R_C2H_STR_ADDR_16_TO_19_8822B                                \
	 << BIT_SHIFT_R_C2H_STR_ADDR_16_TO_19_8822B)
#define BIT_CLEAR_R_C2H_STR_ADDR_16_TO_19_8822B(x)                             \
	((x) & (~BITS_R_C2H_STR_ADDR_16_TO_19_8822B))
#define BIT_GET_R_C2H_STR_ADDR_16_TO_19_8822B(x)                               \
	(((x) >> BIT_SHIFT_R_C2H_STR_ADDR_16_TO_19_8822B) &                    \
	 BIT_MASK_R_C2H_STR_ADDR_16_TO_19_8822B)
#define BIT_SET_R_C2H_STR_ADDR_16_TO_19_8822B(x, v)                            \
	(BIT_CLEAR_R_C2H_STR_ADDR_16_TO_19_8822B(x) |                          \
	 BIT_R_C2H_STR_ADDR_16_TO_19_8822B(v))

#define BIT_R_C2H_PKT_REQ_8822B BIT(16)

#define BIT_SHIFT_R_C2H_STR_ADDR_8822B 0
#define BIT_MASK_R_C2H_STR_ADDR_8822B 0xffff
#define BIT_R_C2H_STR_ADDR_8822B(x)                                            \
	(((x) & BIT_MASK_R_C2H_STR_ADDR_8822B)                                 \
	 << BIT_SHIFT_R_C2H_STR_ADDR_8822B)
#define BITS_R_C2H_STR_ADDR_8822B                                              \
	(BIT_MASK_R_C2H_STR_ADDR_8822B << BIT_SHIFT_R_C2H_STR_ADDR_8822B)
#define BIT_CLEAR_R_C2H_STR_ADDR_8822B(x) ((x) & (~BITS_R_C2H_STR_ADDR_8822B))
#define BIT_GET_R_C2H_STR_ADDR_8822B(x)                                        \
	(((x) >> BIT_SHIFT_R_C2H_STR_ADDR_8822B) &                             \
	 BIT_MASK_R_C2H_STR_ADDR_8822B)
#define BIT_SET_R_C2H_STR_ADDR_8822B(x, v)                                     \
	(BIT_CLEAR_R_C2H_STR_ADDR_8822B(x) | BIT_R_C2H_STR_ADDR_8822B(v))

/* 2 REG_FWFF_C2H_8822B */

#define BIT_SHIFT_C2H_DMA_ADDR_8822B 0
#define BIT_MASK_C2H_DMA_ADDR_8822B 0x3ffff
#define BIT_C2H_DMA_ADDR_8822B(x)                                              \
	(((x) & BIT_MASK_C2H_DMA_ADDR_8822B) << BIT_SHIFT_C2H_DMA_ADDR_8822B)
#define BITS_C2H_DMA_ADDR_8822B                                                \
	(BIT_MASK_C2H_DMA_ADDR_8822B << BIT_SHIFT_C2H_DMA_ADDR_8822B)
#define BIT_CLEAR_C2H_DMA_ADDR_8822B(x) ((x) & (~BITS_C2H_DMA_ADDR_8822B))
#define BIT_GET_C2H_DMA_ADDR_8822B(x)                                          \
	(((x) >> BIT_SHIFT_C2H_DMA_ADDR_8822B) & BIT_MASK_C2H_DMA_ADDR_8822B)
#define BIT_SET_C2H_DMA_ADDR_8822B(x, v)                                       \
	(BIT_CLEAR_C2H_DMA_ADDR_8822B(x) | BIT_C2H_DMA_ADDR_8822B(v))

/* 2 REG_FWFF_CTRL_8822B */
#define BIT_FWFF_DMAPKT_REQ_8822B BIT(31)

#define BIT_SHIFT_FWFF_DMA_PKT_NUM_8822B 16
#define BIT_MASK_FWFF_DMA_PKT_NUM_8822B 0xff
#define BIT_FWFF_DMA_PKT_NUM_8822B(x)                                          \
	(((x) & BIT_MASK_FWFF_DMA_PKT_NUM_8822B)                               \
	 << BIT_SHIFT_FWFF_DMA_PKT_NUM_8822B)
#define BITS_FWFF_DMA_PKT_NUM_8822B                                            \
	(BIT_MASK_FWFF_DMA_PKT_NUM_8822B << BIT_SHIFT_FWFF_DMA_PKT_NUM_8822B)
#define BIT_CLEAR_FWFF_DMA_PKT_NUM_8822B(x)                                    \
	((x) & (~BITS_FWFF_DMA_PKT_NUM_8822B))
#define BIT_GET_FWFF_DMA_PKT_NUM_8822B(x)                                      \
	(((x) >> BIT_SHIFT_FWFF_DMA_PKT_NUM_8822B) &                           \
	 BIT_MASK_FWFF_DMA_PKT_NUM_8822B)
#define BIT_SET_FWFF_DMA_PKT_NUM_8822B(x, v)                                   \
	(BIT_CLEAR_FWFF_DMA_PKT_NUM_8822B(x) | BIT_FWFF_DMA_PKT_NUM_8822B(v))

#define BIT_SHIFT_FWFF_STR_ADDR_8822B 0
#define BIT_MASK_FWFF_STR_ADDR_8822B 0xffff
#define BIT_FWFF_STR_ADDR_8822B(x)                                             \
	(((x) & BIT_MASK_FWFF_STR_ADDR_8822B) << BIT_SHIFT_FWFF_STR_ADDR_8822B)
#define BITS_FWFF_STR_ADDR_8822B                                               \
	(BIT_MASK_FWFF_STR_ADDR_8822B << BIT_SHIFT_FWFF_STR_ADDR_8822B)
#define BIT_CLEAR_FWFF_STR_ADDR_8822B(x) ((x) & (~BITS_FWFF_STR_ADDR_8822B))
#define BIT_GET_FWFF_STR_ADDR_8822B(x)                                         \
	(((x) >> BIT_SHIFT_FWFF_STR_ADDR_8822B) & BIT_MASK_FWFF_STR_ADDR_8822B)
#define BIT_SET_FWFF_STR_ADDR_8822B(x, v)                                      \
	(BIT_CLEAR_FWFF_STR_ADDR_8822B(x) | BIT_FWFF_STR_ADDR_8822B(v))

/* 2 REG_FWFF_PKT_INFO_8822B */

#define BIT_SHIFT_FWFF_PKT_QUEUED_8822B 16
#define BIT_MASK_FWFF_PKT_QUEUED_8822B 0xff
#define BIT_FWFF_PKT_QUEUED_8822B(x)                                           \
	(((x) & BIT_MASK_FWFF_PKT_QUEUED_8822B)                                \
	 << BIT_SHIFT_FWFF_PKT_QUEUED_8822B)
#define BITS_FWFF_PKT_QUEUED_8822B                                             \
	(BIT_MASK_FWFF_PKT_QUEUED_8822B << BIT_SHIFT_FWFF_PKT_QUEUED_8822B)
#define BIT_CLEAR_FWFF_PKT_QUEUED_8822B(x) ((x) & (~BITS_FWFF_PKT_QUEUED_8822B))
#define BIT_GET_FWFF_PKT_QUEUED_8822B(x)                                       \
	(((x) >> BIT_SHIFT_FWFF_PKT_QUEUED_8822B) &                            \
	 BIT_MASK_FWFF_PKT_QUEUED_8822B)
#define BIT_SET_FWFF_PKT_QUEUED_8822B(x, v)                                    \
	(BIT_CLEAR_FWFF_PKT_QUEUED_8822B(x) | BIT_FWFF_PKT_QUEUED_8822B(v))

#define BIT_SHIFT_FWFF_PKT_STR_ADDR_8822B 0
#define BIT_MASK_FWFF_PKT_STR_ADDR_8822B 0xffff
#define BIT_FWFF_PKT_STR_ADDR_8822B(x)                                         \
	(((x) & BIT_MASK_FWFF_PKT_STR_ADDR_8822B)                              \
	 << BIT_SHIFT_FWFF_PKT_STR_ADDR_8822B)
#define BITS_FWFF_PKT_STR_ADDR_8822B                                           \
	(BIT_MASK_FWFF_PKT_STR_ADDR_8822B << BIT_SHIFT_FWFF_PKT_STR_ADDR_8822B)
#define BIT_CLEAR_FWFF_PKT_STR_ADDR_8822B(x)                                   \
	((x) & (~BITS_FWFF_PKT_STR_ADDR_8822B))
#define BIT_GET_FWFF_PKT_STR_ADDR_8822B(x)                                     \
	(((x) >> BIT_SHIFT_FWFF_PKT_STR_ADDR_8822B) &                          \
	 BIT_MASK_FWFF_PKT_STR_ADDR_8822B)
#define BIT_SET_FWFF_PKT_STR_ADDR_8822B(x, v)                                  \
	(BIT_CLEAR_FWFF_PKT_STR_ADDR_8822B(x) | BIT_FWFF_PKT_STR_ADDR_8822B(v))

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_DDMA_CH0SA_8822B */

#define BIT_SHIFT_DDMACH0_SA_8822B 0
#define BIT_MASK_DDMACH0_SA_8822B 0xffffffffL
#define BIT_DDMACH0_SA_8822B(x)                                                \
	(((x) & BIT_MASK_DDMACH0_SA_8822B) << BIT_SHIFT_DDMACH0_SA_8822B)
#define BITS_DDMACH0_SA_8822B                                                  \
	(BIT_MASK_DDMACH0_SA_8822B << BIT_SHIFT_DDMACH0_SA_8822B)
#define BIT_CLEAR_DDMACH0_SA_8822B(x) ((x) & (~BITS_DDMACH0_SA_8822B))
#define BIT_GET_DDMACH0_SA_8822B(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH0_SA_8822B) & BIT_MASK_DDMACH0_SA_8822B)
#define BIT_SET_DDMACH0_SA_8822B(x, v)                                         \
	(BIT_CLEAR_DDMACH0_SA_8822B(x) | BIT_DDMACH0_SA_8822B(v))

/* 2 REG_DDMA_CH0DA_8822B */

#define BIT_SHIFT_DDMACH0_DA_8822B 0
#define BIT_MASK_DDMACH0_DA_8822B 0xffffffffL
#define BIT_DDMACH0_DA_8822B(x)                                                \
	(((x) & BIT_MASK_DDMACH0_DA_8822B) << BIT_SHIFT_DDMACH0_DA_8822B)
#define BITS_DDMACH0_DA_8822B                                                  \
	(BIT_MASK_DDMACH0_DA_8822B << BIT_SHIFT_DDMACH0_DA_8822B)
#define BIT_CLEAR_DDMACH0_DA_8822B(x) ((x) & (~BITS_DDMACH0_DA_8822B))
#define BIT_GET_DDMACH0_DA_8822B(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH0_DA_8822B) & BIT_MASK_DDMACH0_DA_8822B)
#define BIT_SET_DDMACH0_DA_8822B(x, v)                                         \
	(BIT_CLEAR_DDMACH0_DA_8822B(x) | BIT_DDMACH0_DA_8822B(v))

/* 2 REG_DDMA_CH0CTRL_8822B */
#define BIT_DDMACH0_OWN_8822B BIT(31)
#define BIT_DDMACH0_IDMEM_ERR_8822B BIT(30)
#define BIT_DDMACH0_CHKSUM_EN_8822B BIT(29)
#define BIT_DDMACH0_DA_W_DISABLE_8822B BIT(28)
#define BIT_DDMACH0_CHKSUM_STS_8822B BIT(27)
#define BIT_DDMACH0_DDMA_MODE_8822B BIT(26)
#define BIT_DDMACH0_RESET_CHKSUM_STS_8822B BIT(25)
#define BIT_DDMACH0_CHKSUM_CONT_8822B BIT(24)

#define BIT_SHIFT_DDMACH0_DLEN_8822B 0
#define BIT_MASK_DDMACH0_DLEN_8822B 0x3ffff
#define BIT_DDMACH0_DLEN_8822B(x)                                              \
	(((x) & BIT_MASK_DDMACH0_DLEN_8822B) << BIT_SHIFT_DDMACH0_DLEN_8822B)
#define BITS_DDMACH0_DLEN_8822B                                                \
	(BIT_MASK_DDMACH0_DLEN_8822B << BIT_SHIFT_DDMACH0_DLEN_8822B)
#define BIT_CLEAR_DDMACH0_DLEN_8822B(x) ((x) & (~BITS_DDMACH0_DLEN_8822B))
#define BIT_GET_DDMACH0_DLEN_8822B(x)                                          \
	(((x) >> BIT_SHIFT_DDMACH0_DLEN_8822B) & BIT_MASK_DDMACH0_DLEN_8822B)
#define BIT_SET_DDMACH0_DLEN_8822B(x, v)                                       \
	(BIT_CLEAR_DDMACH0_DLEN_8822B(x) | BIT_DDMACH0_DLEN_8822B(v))

/* 2 REG_DDMA_CH1SA_8822B */

#define BIT_SHIFT_DDMACH1_SA_8822B 0
#define BIT_MASK_DDMACH1_SA_8822B 0xffffffffL
#define BIT_DDMACH1_SA_8822B(x)                                                \
	(((x) & BIT_MASK_DDMACH1_SA_8822B) << BIT_SHIFT_DDMACH1_SA_8822B)
#define BITS_DDMACH1_SA_8822B                                                  \
	(BIT_MASK_DDMACH1_SA_8822B << BIT_SHIFT_DDMACH1_SA_8822B)
#define BIT_CLEAR_DDMACH1_SA_8822B(x) ((x) & (~BITS_DDMACH1_SA_8822B))
#define BIT_GET_DDMACH1_SA_8822B(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH1_SA_8822B) & BIT_MASK_DDMACH1_SA_8822B)
#define BIT_SET_DDMACH1_SA_8822B(x, v)                                         \
	(BIT_CLEAR_DDMACH1_SA_8822B(x) | BIT_DDMACH1_SA_8822B(v))

/* 2 REG_DDMA_CH1DA_8822B */

#define BIT_SHIFT_DDMACH1_DA_8822B 0
#define BIT_MASK_DDMACH1_DA_8822B 0xffffffffL
#define BIT_DDMACH1_DA_8822B(x)                                                \
	(((x) & BIT_MASK_DDMACH1_DA_8822B) << BIT_SHIFT_DDMACH1_DA_8822B)
#define BITS_DDMACH1_DA_8822B                                                  \
	(BIT_MASK_DDMACH1_DA_8822B << BIT_SHIFT_DDMACH1_DA_8822B)
#define BIT_CLEAR_DDMACH1_DA_8822B(x) ((x) & (~BITS_DDMACH1_DA_8822B))
#define BIT_GET_DDMACH1_DA_8822B(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH1_DA_8822B) & BIT_MASK_DDMACH1_DA_8822B)
#define BIT_SET_DDMACH1_DA_8822B(x, v)                                         \
	(BIT_CLEAR_DDMACH1_DA_8822B(x) | BIT_DDMACH1_DA_8822B(v))

/* 2 REG_DDMA_CH1CTRL_8822B */
#define BIT_DDMACH1_OWN_8822B BIT(31)
#define BIT_DDMACH1_IDMEM_ERR_8822B BIT(30)
#define BIT_DDMACH1_CHKSUM_EN_8822B BIT(29)
#define BIT_DDMACH1_DA_W_DISABLE_8822B BIT(28)
#define BIT_DDMACH1_CHKSUM_STS_8822B BIT(27)
#define BIT_DDMACH1_DDMA_MODE_8822B BIT(26)
#define BIT_DDMACH1_RESET_CHKSUM_STS_8822B BIT(25)
#define BIT_DDMACH1_CHKSUM_CONT_8822B BIT(24)

#define BIT_SHIFT_DDMACH1_DLEN_8822B 0
#define BIT_MASK_DDMACH1_DLEN_8822B 0x3ffff
#define BIT_DDMACH1_DLEN_8822B(x)                                              \
	(((x) & BIT_MASK_DDMACH1_DLEN_8822B) << BIT_SHIFT_DDMACH1_DLEN_8822B)
#define BITS_DDMACH1_DLEN_8822B                                                \
	(BIT_MASK_DDMACH1_DLEN_8822B << BIT_SHIFT_DDMACH1_DLEN_8822B)
#define BIT_CLEAR_DDMACH1_DLEN_8822B(x) ((x) & (~BITS_DDMACH1_DLEN_8822B))
#define BIT_GET_DDMACH1_DLEN_8822B(x)                                          \
	(((x) >> BIT_SHIFT_DDMACH1_DLEN_8822B) & BIT_MASK_DDMACH1_DLEN_8822B)
#define BIT_SET_DDMACH1_DLEN_8822B(x, v)                                       \
	(BIT_CLEAR_DDMACH1_DLEN_8822B(x) | BIT_DDMACH1_DLEN_8822B(v))

/* 2 REG_DDMA_CH2SA_8822B */

#define BIT_SHIFT_DDMACH2_SA_8822B 0
#define BIT_MASK_DDMACH2_SA_8822B 0xffffffffL
#define BIT_DDMACH2_SA_8822B(x)                                                \
	(((x) & BIT_MASK_DDMACH2_SA_8822B) << BIT_SHIFT_DDMACH2_SA_8822B)
#define BITS_DDMACH2_SA_8822B                                                  \
	(BIT_MASK_DDMACH2_SA_8822B << BIT_SHIFT_DDMACH2_SA_8822B)
#define BIT_CLEAR_DDMACH2_SA_8822B(x) ((x) & (~BITS_DDMACH2_SA_8822B))
#define BIT_GET_DDMACH2_SA_8822B(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH2_SA_8822B) & BIT_MASK_DDMACH2_SA_8822B)
#define BIT_SET_DDMACH2_SA_8822B(x, v)                                         \
	(BIT_CLEAR_DDMACH2_SA_8822B(x) | BIT_DDMACH2_SA_8822B(v))

/* 2 REG_DDMA_CH2DA_8822B */

#define BIT_SHIFT_DDMACH2_DA_8822B 0
#define BIT_MASK_DDMACH2_DA_8822B 0xffffffffL
#define BIT_DDMACH2_DA_8822B(x)                                                \
	(((x) & BIT_MASK_DDMACH2_DA_8822B) << BIT_SHIFT_DDMACH2_DA_8822B)
#define BITS_DDMACH2_DA_8822B                                                  \
	(BIT_MASK_DDMACH2_DA_8822B << BIT_SHIFT_DDMACH2_DA_8822B)
#define BIT_CLEAR_DDMACH2_DA_8822B(x) ((x) & (~BITS_DDMACH2_DA_8822B))
#define BIT_GET_DDMACH2_DA_8822B(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH2_DA_8822B) & BIT_MASK_DDMACH2_DA_8822B)
#define BIT_SET_DDMACH2_DA_8822B(x, v)                                         \
	(BIT_CLEAR_DDMACH2_DA_8822B(x) | BIT_DDMACH2_DA_8822B(v))

/* 2 REG_DDMA_CH2CTRL_8822B */
#define BIT_DDMACH2_OWN_8822B BIT(31)
#define BIT_DDMACH2_IDMEM_ERR_8822B BIT(30)
#define BIT_DDMACH2_CHKSUM_EN_8822B BIT(29)
#define BIT_DDMACH2_DA_W_DISABLE_8822B BIT(28)
#define BIT_DDMACH2_CHKSUM_STS_8822B BIT(27)
#define BIT_DDMACH2_DDMA_MODE_8822B BIT(26)
#define BIT_DDMACH2_RESET_CHKSUM_STS_8822B BIT(25)
#define BIT_DDMACH2_CHKSUM_CONT_8822B BIT(24)

#define BIT_SHIFT_DDMACH2_DLEN_8822B 0
#define BIT_MASK_DDMACH2_DLEN_8822B 0x3ffff
#define BIT_DDMACH2_DLEN_8822B(x)                                              \
	(((x) & BIT_MASK_DDMACH2_DLEN_8822B) << BIT_SHIFT_DDMACH2_DLEN_8822B)
#define BITS_DDMACH2_DLEN_8822B                                                \
	(BIT_MASK_DDMACH2_DLEN_8822B << BIT_SHIFT_DDMACH2_DLEN_8822B)
#define BIT_CLEAR_DDMACH2_DLEN_8822B(x) ((x) & (~BITS_DDMACH2_DLEN_8822B))
#define BIT_GET_DDMACH2_DLEN_8822B(x)                                          \
	(((x) >> BIT_SHIFT_DDMACH2_DLEN_8822B) & BIT_MASK_DDMACH2_DLEN_8822B)
#define BIT_SET_DDMACH2_DLEN_8822B(x, v)                                       \
	(BIT_CLEAR_DDMACH2_DLEN_8822B(x) | BIT_DDMACH2_DLEN_8822B(v))

/* 2 REG_DDMA_CH3SA_8822B */

#define BIT_SHIFT_DDMACH3_SA_8822B 0
#define BIT_MASK_DDMACH3_SA_8822B 0xffffffffL
#define BIT_DDMACH3_SA_8822B(x)                                                \
	(((x) & BIT_MASK_DDMACH3_SA_8822B) << BIT_SHIFT_DDMACH3_SA_8822B)
#define BITS_DDMACH3_SA_8822B                                                  \
	(BIT_MASK_DDMACH3_SA_8822B << BIT_SHIFT_DDMACH3_SA_8822B)
#define BIT_CLEAR_DDMACH3_SA_8822B(x) ((x) & (~BITS_DDMACH3_SA_8822B))
#define BIT_GET_DDMACH3_SA_8822B(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH3_SA_8822B) & BIT_MASK_DDMACH3_SA_8822B)
#define BIT_SET_DDMACH3_SA_8822B(x, v)                                         \
	(BIT_CLEAR_DDMACH3_SA_8822B(x) | BIT_DDMACH3_SA_8822B(v))

/* 2 REG_DDMA_CH3DA_8822B */

#define BIT_SHIFT_DDMACH3_DA_8822B 0
#define BIT_MASK_DDMACH3_DA_8822B 0xffffffffL
#define BIT_DDMACH3_DA_8822B(x)                                                \
	(((x) & BIT_MASK_DDMACH3_DA_8822B) << BIT_SHIFT_DDMACH3_DA_8822B)
#define BITS_DDMACH3_DA_8822B                                                  \
	(BIT_MASK_DDMACH3_DA_8822B << BIT_SHIFT_DDMACH3_DA_8822B)
#define BIT_CLEAR_DDMACH3_DA_8822B(x) ((x) & (~BITS_DDMACH3_DA_8822B))
#define BIT_GET_DDMACH3_DA_8822B(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH3_DA_8822B) & BIT_MASK_DDMACH3_DA_8822B)
#define BIT_SET_DDMACH3_DA_8822B(x, v)                                         \
	(BIT_CLEAR_DDMACH3_DA_8822B(x) | BIT_DDMACH3_DA_8822B(v))

/* 2 REG_DDMA_CH3CTRL_8822B */
#define BIT_DDMACH3_OWN_8822B BIT(31)
#define BIT_DDMACH3_IDMEM_ERR_8822B BIT(30)
#define BIT_DDMACH3_CHKSUM_EN_8822B BIT(29)
#define BIT_DDMACH3_DA_W_DISABLE_8822B BIT(28)
#define BIT_DDMACH3_CHKSUM_STS_8822B BIT(27)
#define BIT_DDMACH3_DDMA_MODE_8822B BIT(26)
#define BIT_DDMACH3_RESET_CHKSUM_STS_8822B BIT(25)
#define BIT_DDMACH3_CHKSUM_CONT_8822B BIT(24)

#define BIT_SHIFT_DDMACH3_DLEN_8822B 0
#define BIT_MASK_DDMACH3_DLEN_8822B 0x3ffff
#define BIT_DDMACH3_DLEN_8822B(x)                                              \
	(((x) & BIT_MASK_DDMACH3_DLEN_8822B) << BIT_SHIFT_DDMACH3_DLEN_8822B)
#define BITS_DDMACH3_DLEN_8822B                                                \
	(BIT_MASK_DDMACH3_DLEN_8822B << BIT_SHIFT_DDMACH3_DLEN_8822B)
#define BIT_CLEAR_DDMACH3_DLEN_8822B(x) ((x) & (~BITS_DDMACH3_DLEN_8822B))
#define BIT_GET_DDMACH3_DLEN_8822B(x)                                          \
	(((x) >> BIT_SHIFT_DDMACH3_DLEN_8822B) & BIT_MASK_DDMACH3_DLEN_8822B)
#define BIT_SET_DDMACH3_DLEN_8822B(x, v)                                       \
	(BIT_CLEAR_DDMACH3_DLEN_8822B(x) | BIT_DDMACH3_DLEN_8822B(v))

/* 2 REG_DDMA_CH4SA_8822B */

#define BIT_SHIFT_DDMACH4_SA_8822B 0
#define BIT_MASK_DDMACH4_SA_8822B 0xffffffffL
#define BIT_DDMACH4_SA_8822B(x)                                                \
	(((x) & BIT_MASK_DDMACH4_SA_8822B) << BIT_SHIFT_DDMACH4_SA_8822B)
#define BITS_DDMACH4_SA_8822B                                                  \
	(BIT_MASK_DDMACH4_SA_8822B << BIT_SHIFT_DDMACH4_SA_8822B)
#define BIT_CLEAR_DDMACH4_SA_8822B(x) ((x) & (~BITS_DDMACH4_SA_8822B))
#define BIT_GET_DDMACH4_SA_8822B(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH4_SA_8822B) & BIT_MASK_DDMACH4_SA_8822B)
#define BIT_SET_DDMACH4_SA_8822B(x, v)                                         \
	(BIT_CLEAR_DDMACH4_SA_8822B(x) | BIT_DDMACH4_SA_8822B(v))

/* 2 REG_DDMA_CH4DA_8822B */

#define BIT_SHIFT_DDMACH4_DA_8822B 0
#define BIT_MASK_DDMACH4_DA_8822B 0xffffffffL
#define BIT_DDMACH4_DA_8822B(x)                                                \
	(((x) & BIT_MASK_DDMACH4_DA_8822B) << BIT_SHIFT_DDMACH4_DA_8822B)
#define BITS_DDMACH4_DA_8822B                                                  \
	(BIT_MASK_DDMACH4_DA_8822B << BIT_SHIFT_DDMACH4_DA_8822B)
#define BIT_CLEAR_DDMACH4_DA_8822B(x) ((x) & (~BITS_DDMACH4_DA_8822B))
#define BIT_GET_DDMACH4_DA_8822B(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH4_DA_8822B) & BIT_MASK_DDMACH4_DA_8822B)
#define BIT_SET_DDMACH4_DA_8822B(x, v)                                         \
	(BIT_CLEAR_DDMACH4_DA_8822B(x) | BIT_DDMACH4_DA_8822B(v))

/* 2 REG_DDMA_CH4CTRL_8822B */
#define BIT_DDMACH4_OWN_8822B BIT(31)
#define BIT_DDMACH4_IDMEM_ERR_8822B BIT(30)
#define BIT_DDMACH4_CHKSUM_EN_8822B BIT(29)
#define BIT_DDMACH4_DA_W_DISABLE_8822B BIT(28)
#define BIT_DDMACH4_CHKSUM_STS_8822B BIT(27)
#define BIT_DDMACH4_DDMA_MODE_8822B BIT(26)
#define BIT_DDMACH4_RESET_CHKSUM_STS_8822B BIT(25)
#define BIT_DDMACH4_CHKSUM_CONT_8822B BIT(24)

#define BIT_SHIFT_DDMACH4_DLEN_8822B 0
#define BIT_MASK_DDMACH4_DLEN_8822B 0x3ffff
#define BIT_DDMACH4_DLEN_8822B(x)                                              \
	(((x) & BIT_MASK_DDMACH4_DLEN_8822B) << BIT_SHIFT_DDMACH4_DLEN_8822B)
#define BITS_DDMACH4_DLEN_8822B                                                \
	(BIT_MASK_DDMACH4_DLEN_8822B << BIT_SHIFT_DDMACH4_DLEN_8822B)
#define BIT_CLEAR_DDMACH4_DLEN_8822B(x) ((x) & (~BITS_DDMACH4_DLEN_8822B))
#define BIT_GET_DDMACH4_DLEN_8822B(x)                                          \
	(((x) >> BIT_SHIFT_DDMACH4_DLEN_8822B) & BIT_MASK_DDMACH4_DLEN_8822B)
#define BIT_SET_DDMACH4_DLEN_8822B(x, v)                                       \
	(BIT_CLEAR_DDMACH4_DLEN_8822B(x) | BIT_DDMACH4_DLEN_8822B(v))

/* 2 REG_DDMA_CH5SA_8822B */

#define BIT_SHIFT_DDMACH5_SA_8822B 0
#define BIT_MASK_DDMACH5_SA_8822B 0xffffffffL
#define BIT_DDMACH5_SA_8822B(x)                                                \
	(((x) & BIT_MASK_DDMACH5_SA_8822B) << BIT_SHIFT_DDMACH5_SA_8822B)
#define BITS_DDMACH5_SA_8822B                                                  \
	(BIT_MASK_DDMACH5_SA_8822B << BIT_SHIFT_DDMACH5_SA_8822B)
#define BIT_CLEAR_DDMACH5_SA_8822B(x) ((x) & (~BITS_DDMACH5_SA_8822B))
#define BIT_GET_DDMACH5_SA_8822B(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH5_SA_8822B) & BIT_MASK_DDMACH5_SA_8822B)
#define BIT_SET_DDMACH5_SA_8822B(x, v)                                         \
	(BIT_CLEAR_DDMACH5_SA_8822B(x) | BIT_DDMACH5_SA_8822B(v))

/* 2 REG_DDMA_CH5DA_8822B */

#define BIT_SHIFT_DDMACH5_DA_8822B 0
#define BIT_MASK_DDMACH5_DA_8822B 0xffffffffL
#define BIT_DDMACH5_DA_8822B(x)                                                \
	(((x) & BIT_MASK_DDMACH5_DA_8822B) << BIT_SHIFT_DDMACH5_DA_8822B)
#define BITS_DDMACH5_DA_8822B                                                  \
	(BIT_MASK_DDMACH5_DA_8822B << BIT_SHIFT_DDMACH5_DA_8822B)
#define BIT_CLEAR_DDMACH5_DA_8822B(x) ((x) & (~BITS_DDMACH5_DA_8822B))
#define BIT_GET_DDMACH5_DA_8822B(x)                                            \
	(((x) >> BIT_SHIFT_DDMACH5_DA_8822B) & BIT_MASK_DDMACH5_DA_8822B)
#define BIT_SET_DDMACH5_DA_8822B(x, v)                                         \
	(BIT_CLEAR_DDMACH5_DA_8822B(x) | BIT_DDMACH5_DA_8822B(v))

/* 2 REG_REG_DDMA_CH5CTRL_8822B */
#define BIT_DDMACH5_OWN_8822B BIT(31)
#define BIT_DDMACH5_IDMEM_ERR_8822B BIT(30)
#define BIT_DDMACH5_CHKSUM_EN_8822B BIT(29)
#define BIT_DDMACH5_DA_W_DISABLE_8822B BIT(28)
#define BIT_DDMACH5_CHKSUM_STS_8822B BIT(27)
#define BIT_DDMACH5_DDMA_MODE_8822B BIT(26)
#define BIT_DDMACH5_RESET_CHKSUM_STS_8822B BIT(25)
#define BIT_DDMACH5_CHKSUM_CONT_8822B BIT(24)

#define BIT_SHIFT_DDMACH5_DLEN_8822B 0
#define BIT_MASK_DDMACH5_DLEN_8822B 0x3ffff
#define BIT_DDMACH5_DLEN_8822B(x)                                              \
	(((x) & BIT_MASK_DDMACH5_DLEN_8822B) << BIT_SHIFT_DDMACH5_DLEN_8822B)
#define BITS_DDMACH5_DLEN_8822B                                                \
	(BIT_MASK_DDMACH5_DLEN_8822B << BIT_SHIFT_DDMACH5_DLEN_8822B)
#define BIT_CLEAR_DDMACH5_DLEN_8822B(x) ((x) & (~BITS_DDMACH5_DLEN_8822B))
#define BIT_GET_DDMACH5_DLEN_8822B(x)                                          \
	(((x) >> BIT_SHIFT_DDMACH5_DLEN_8822B) & BIT_MASK_DDMACH5_DLEN_8822B)
#define BIT_SET_DDMACH5_DLEN_8822B(x, v)                                       \
	(BIT_CLEAR_DDMACH5_DLEN_8822B(x) | BIT_DDMACH5_DLEN_8822B(v))

/* 2 REG_DDMA_INT_MSK_8822B */
#define BIT_DDMACH5_MSK_8822B BIT(5)
#define BIT_DDMACH4_MSK_8822B BIT(4)
#define BIT_DDMACH3_MSK_8822B BIT(3)
#define BIT_DDMACH2_MSK_8822B BIT(2)
#define BIT_DDMACH1_MSK_8822B BIT(1)
#define BIT_DDMACH0_MSK_8822B BIT(0)

/* 2 REG_DDMA_CHSTATUS_8822B */
#define BIT_DDMACH5_BUSY_8822B BIT(5)
#define BIT_DDMACH4_BUSY_8822B BIT(4)
#define BIT_DDMACH3_BUSY_8822B BIT(3)
#define BIT_DDMACH2_BUSY_8822B BIT(2)
#define BIT_DDMACH1_BUSY_8822B BIT(1)
#define BIT_DDMACH0_BUSY_8822B BIT(0)

/* 2 REG_DDMA_CHKSUM_8822B */

#define BIT_SHIFT_IDDMA0_CHKSUM_8822B 0
#define BIT_MASK_IDDMA0_CHKSUM_8822B 0xffff
#define BIT_IDDMA0_CHKSUM_8822B(x)                                             \
	(((x) & BIT_MASK_IDDMA0_CHKSUM_8822B) << BIT_SHIFT_IDDMA0_CHKSUM_8822B)
#define BITS_IDDMA0_CHKSUM_8822B                                               \
	(BIT_MASK_IDDMA0_CHKSUM_8822B << BIT_SHIFT_IDDMA0_CHKSUM_8822B)
#define BIT_CLEAR_IDDMA0_CHKSUM_8822B(x) ((x) & (~BITS_IDDMA0_CHKSUM_8822B))
#define BIT_GET_IDDMA0_CHKSUM_8822B(x)                                         \
	(((x) >> BIT_SHIFT_IDDMA0_CHKSUM_8822B) & BIT_MASK_IDDMA0_CHKSUM_8822B)
#define BIT_SET_IDDMA0_CHKSUM_8822B(x, v)                                      \
	(BIT_CLEAR_IDDMA0_CHKSUM_8822B(x) | BIT_IDDMA0_CHKSUM_8822B(v))

/* 2 REG_DDMA_MONITOR_8822B */
#define BIT_IDDMA0_PERMU_UNDERFLOW_8822B BIT(14)
#define BIT_IDDMA0_FIFO_UNDERFLOW_8822B BIT(13)
#define BIT_IDDMA0_FIFO_OVERFLOW_8822B BIT(12)
#define BIT_CH5_ERR_8822B BIT(5)
#define BIT_CH4_ERR_8822B BIT(4)
#define BIT_CH3_ERR_8822B BIT(3)
#define BIT_CH2_ERR_8822B BIT(2)
#define BIT_CH1_ERR_8822B BIT(1)
#define BIT_CH0_ERR_8822B BIT(0)

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_PCIE_CTRL_8822B */
#define BIT_PCIEIO_PERSTB_SEL_8822B BIT(31)

#define BIT_SHIFT_PCIE_MAX_RXDMA_8822B 28
#define BIT_MASK_PCIE_MAX_RXDMA_8822B 0x7
#define BIT_PCIE_MAX_RXDMA_8822B(x)                                            \
	(((x) & BIT_MASK_PCIE_MAX_RXDMA_8822B)                                 \
	 << BIT_SHIFT_PCIE_MAX_RXDMA_8822B)
#define BITS_PCIE_MAX_RXDMA_8822B                                              \
	(BIT_MASK_PCIE_MAX_RXDMA_8822B << BIT_SHIFT_PCIE_MAX_RXDMA_8822B)
#define BIT_CLEAR_PCIE_MAX_RXDMA_8822B(x) ((x) & (~BITS_PCIE_MAX_RXDMA_8822B))
#define BIT_GET_PCIE_MAX_RXDMA_8822B(x)                                        \
	(((x) >> BIT_SHIFT_PCIE_MAX_RXDMA_8822B) &                             \
	 BIT_MASK_PCIE_MAX_RXDMA_8822B)
#define BIT_SET_PCIE_MAX_RXDMA_8822B(x, v)                                     \
	(BIT_CLEAR_PCIE_MAX_RXDMA_8822B(x) | BIT_PCIE_MAX_RXDMA_8822B(v))

#define BIT_MULRW_8822B BIT(27)

#define BIT_SHIFT_PCIE_MAX_TXDMA_8822B 24
#define BIT_MASK_PCIE_MAX_TXDMA_8822B 0x7
#define BIT_PCIE_MAX_TXDMA_8822B(x)                                            \
	(((x) & BIT_MASK_PCIE_MAX_TXDMA_8822B)                                 \
	 << BIT_SHIFT_PCIE_MAX_TXDMA_8822B)
#define BITS_PCIE_MAX_TXDMA_8822B                                              \
	(BIT_MASK_PCIE_MAX_TXDMA_8822B << BIT_SHIFT_PCIE_MAX_TXDMA_8822B)
#define BIT_CLEAR_PCIE_MAX_TXDMA_8822B(x) ((x) & (~BITS_PCIE_MAX_TXDMA_8822B))
#define BIT_GET_PCIE_MAX_TXDMA_8822B(x)                                        \
	(((x) >> BIT_SHIFT_PCIE_MAX_TXDMA_8822B) &                             \
	 BIT_MASK_PCIE_MAX_TXDMA_8822B)
#define BIT_SET_PCIE_MAX_TXDMA_8822B(x, v)                                     \
	(BIT_CLEAR_PCIE_MAX_TXDMA_8822B(x) | BIT_PCIE_MAX_TXDMA_8822B(v))

#define BIT_EN_CPL_TIMEOUT_PS_8822B BIT(22)
#define BIT_REG_TXDMA_FAIL_PS_8822B BIT(21)
#define BIT_PCIE_RST_TRXDMA_INTF_8822B BIT(20)
#define BIT_EN_HWENTR_L1_8822B BIT(19)
#define BIT_EN_ADV_CLKGATE_8822B BIT(18)
#define BIT_PCIE_EN_SWENT_L23_8822B BIT(17)
#define BIT_PCIE_EN_HWEXT_L1_8822B BIT(16)
#define BIT_RX_CLOSE_EN_8822B BIT(15)
#define BIT_STOP_BCNQ_8822B BIT(14)
#define BIT_STOP_MGQ_8822B BIT(13)
#define BIT_STOP_VOQ_8822B BIT(12)
#define BIT_STOP_VIQ_8822B BIT(11)
#define BIT_STOP_BEQ_8822B BIT(10)
#define BIT_STOP_BKQ_8822B BIT(9)
#define BIT_STOP_RXQ_8822B BIT(8)
#define BIT_STOP_HI7Q_8822B BIT(7)
#define BIT_STOP_HI6Q_8822B BIT(6)
#define BIT_STOP_HI5Q_8822B BIT(5)
#define BIT_STOP_HI4Q_8822B BIT(4)
#define BIT_STOP_HI3Q_8822B BIT(3)
#define BIT_STOP_HI2Q_8822B BIT(2)
#define BIT_STOP_HI1Q_8822B BIT(1)
#define BIT_STOP_HI0Q_8822B BIT(0)

/* 2 REG_INT_MIG_8822B */

#define BIT_SHIFT_TXTTIMER_MATCH_NUM_8822B 28
#define BIT_MASK_TXTTIMER_MATCH_NUM_8822B 0xf
#define BIT_TXTTIMER_MATCH_NUM_8822B(x)                                        \
	(((x) & BIT_MASK_TXTTIMER_MATCH_NUM_8822B)                             \
	 << BIT_SHIFT_TXTTIMER_MATCH_NUM_8822B)
#define BITS_TXTTIMER_MATCH_NUM_8822B                                          \
	(BIT_MASK_TXTTIMER_MATCH_NUM_8822B                                     \
	 << BIT_SHIFT_TXTTIMER_MATCH_NUM_8822B)
#define BIT_CLEAR_TXTTIMER_MATCH_NUM_8822B(x)                                  \
	((x) & (~BITS_TXTTIMER_MATCH_NUM_8822B))
#define BIT_GET_TXTTIMER_MATCH_NUM_8822B(x)                                    \
	(((x) >> BIT_SHIFT_TXTTIMER_MATCH_NUM_8822B) &                         \
	 BIT_MASK_TXTTIMER_MATCH_NUM_8822B)
#define BIT_SET_TXTTIMER_MATCH_NUM_8822B(x, v)                                 \
	(BIT_CLEAR_TXTTIMER_MATCH_NUM_8822B(x) |                               \
	 BIT_TXTTIMER_MATCH_NUM_8822B(v))

#define BIT_SHIFT_TXPKT_NUM_MATCH_8822B 24
#define BIT_MASK_TXPKT_NUM_MATCH_8822B 0xf
#define BIT_TXPKT_NUM_MATCH_8822B(x)                                           \
	(((x) & BIT_MASK_TXPKT_NUM_MATCH_8822B)                                \
	 << BIT_SHIFT_TXPKT_NUM_MATCH_8822B)
#define BITS_TXPKT_NUM_MATCH_8822B                                             \
	(BIT_MASK_TXPKT_NUM_MATCH_8822B << BIT_SHIFT_TXPKT_NUM_MATCH_8822B)
#define BIT_CLEAR_TXPKT_NUM_MATCH_8822B(x) ((x) & (~BITS_TXPKT_NUM_MATCH_8822B))
#define BIT_GET_TXPKT_NUM_MATCH_8822B(x)                                       \
	(((x) >> BIT_SHIFT_TXPKT_NUM_MATCH_8822B) &                            \
	 BIT_MASK_TXPKT_NUM_MATCH_8822B)
#define BIT_SET_TXPKT_NUM_MATCH_8822B(x, v)                                    \
	(BIT_CLEAR_TXPKT_NUM_MATCH_8822B(x) | BIT_TXPKT_NUM_MATCH_8822B(v))

#define BIT_SHIFT_RXTTIMER_MATCH_NUM_8822B 20
#define BIT_MASK_RXTTIMER_MATCH_NUM_8822B 0xf
#define BIT_RXTTIMER_MATCH_NUM_8822B(x)                                        \
	(((x) & BIT_MASK_RXTTIMER_MATCH_NUM_8822B)                             \
	 << BIT_SHIFT_RXTTIMER_MATCH_NUM_8822B)
#define BITS_RXTTIMER_MATCH_NUM_8822B                                          \
	(BIT_MASK_RXTTIMER_MATCH_NUM_8822B                                     \
	 << BIT_SHIFT_RXTTIMER_MATCH_NUM_8822B)
#define BIT_CLEAR_RXTTIMER_MATCH_NUM_8822B(x)                                  \
	((x) & (~BITS_RXTTIMER_MATCH_NUM_8822B))
#define BIT_GET_RXTTIMER_MATCH_NUM_8822B(x)                                    \
	(((x) >> BIT_SHIFT_RXTTIMER_MATCH_NUM_8822B) &                         \
	 BIT_MASK_RXTTIMER_MATCH_NUM_8822B)
#define BIT_SET_RXTTIMER_MATCH_NUM_8822B(x, v)                                 \
	(BIT_CLEAR_RXTTIMER_MATCH_NUM_8822B(x) |                               \
	 BIT_RXTTIMER_MATCH_NUM_8822B(v))

#define BIT_SHIFT_RXPKT_NUM_MATCH_8822B 16
#define BIT_MASK_RXPKT_NUM_MATCH_8822B 0xf
#define BIT_RXPKT_NUM_MATCH_8822B(x)                                           \
	(((x) & BIT_MASK_RXPKT_NUM_MATCH_8822B)                                \
	 << BIT_SHIFT_RXPKT_NUM_MATCH_8822B)
#define BITS_RXPKT_NUM_MATCH_8822B                                             \
	(BIT_MASK_RXPKT_NUM_MATCH_8822B << BIT_SHIFT_RXPKT_NUM_MATCH_8822B)
#define BIT_CLEAR_RXPKT_NUM_MATCH_8822B(x) ((x) & (~BITS_RXPKT_NUM_MATCH_8822B))
#define BIT_GET_RXPKT_NUM_MATCH_8822B(x)                                       \
	(((x) >> BIT_SHIFT_RXPKT_NUM_MATCH_8822B) &                            \
	 BIT_MASK_RXPKT_NUM_MATCH_8822B)
#define BIT_SET_RXPKT_NUM_MATCH_8822B(x, v)                                    \
	(BIT_CLEAR_RXPKT_NUM_MATCH_8822B(x) | BIT_RXPKT_NUM_MATCH_8822B(v))

#define BIT_SHIFT_MIGRATE_TIMER_8822B 0
#define BIT_MASK_MIGRATE_TIMER_8822B 0xffff
#define BIT_MIGRATE_TIMER_8822B(x)                                             \
	(((x) & BIT_MASK_MIGRATE_TIMER_8822B) << BIT_SHIFT_MIGRATE_TIMER_8822B)
#define BITS_MIGRATE_TIMER_8822B                                               \
	(BIT_MASK_MIGRATE_TIMER_8822B << BIT_SHIFT_MIGRATE_TIMER_8822B)
#define BIT_CLEAR_MIGRATE_TIMER_8822B(x) ((x) & (~BITS_MIGRATE_TIMER_8822B))
#define BIT_GET_MIGRATE_TIMER_8822B(x)                                         \
	(((x) >> BIT_SHIFT_MIGRATE_TIMER_8822B) & BIT_MASK_MIGRATE_TIMER_8822B)
#define BIT_SET_MIGRATE_TIMER_8822B(x, v)                                      \
	(BIT_CLEAR_MIGRATE_TIMER_8822B(x) | BIT_MIGRATE_TIMER_8822B(v))

/* 2 REG_BCNQ_TXBD_DESA_8822B */

#define BIT_SHIFT_BCNQ_TXBD_DESA_8822B 0
#define BIT_MASK_BCNQ_TXBD_DESA_8822B 0xffffffffffffffffL
#define BIT_BCNQ_TXBD_DESA_8822B(x)                                            \
	(((x) & BIT_MASK_BCNQ_TXBD_DESA_8822B)                                 \
	 << BIT_SHIFT_BCNQ_TXBD_DESA_8822B)
#define BITS_BCNQ_TXBD_DESA_8822B                                              \
	(BIT_MASK_BCNQ_TXBD_DESA_8822B << BIT_SHIFT_BCNQ_TXBD_DESA_8822B)
#define BIT_CLEAR_BCNQ_TXBD_DESA_8822B(x) ((x) & (~BITS_BCNQ_TXBD_DESA_8822B))
#define BIT_GET_BCNQ_TXBD_DESA_8822B(x)                                        \
	(((x) >> BIT_SHIFT_BCNQ_TXBD_DESA_8822B) &                             \
	 BIT_MASK_BCNQ_TXBD_DESA_8822B)
#define BIT_SET_BCNQ_TXBD_DESA_8822B(x, v)                                     \
	(BIT_CLEAR_BCNQ_TXBD_DESA_8822B(x) | BIT_BCNQ_TXBD_DESA_8822B(v))

/* 2 REG_MGQ_TXBD_DESA_8822B */

#define BIT_SHIFT_MGQ_TXBD_DESA_8822B 0
#define BIT_MASK_MGQ_TXBD_DESA_8822B 0xffffffffffffffffL
#define BIT_MGQ_TXBD_DESA_8822B(x)                                             \
	(((x) & BIT_MASK_MGQ_TXBD_DESA_8822B) << BIT_SHIFT_MGQ_TXBD_DESA_8822B)
#define BITS_MGQ_TXBD_DESA_8822B                                               \
	(BIT_MASK_MGQ_TXBD_DESA_8822B << BIT_SHIFT_MGQ_TXBD_DESA_8822B)
#define BIT_CLEAR_MGQ_TXBD_DESA_8822B(x) ((x) & (~BITS_MGQ_TXBD_DESA_8822B))
#define BIT_GET_MGQ_TXBD_DESA_8822B(x)                                         \
	(((x) >> BIT_SHIFT_MGQ_TXBD_DESA_8822B) & BIT_MASK_MGQ_TXBD_DESA_8822B)
#define BIT_SET_MGQ_TXBD_DESA_8822B(x, v)                                      \
	(BIT_CLEAR_MGQ_TXBD_DESA_8822B(x) | BIT_MGQ_TXBD_DESA_8822B(v))

/* 2 REG_VOQ_TXBD_DESA_8822B */

#define BIT_SHIFT_VOQ_TXBD_DESA_8822B 0
#define BIT_MASK_VOQ_TXBD_DESA_8822B 0xffffffffffffffffL
#define BIT_VOQ_TXBD_DESA_8822B(x)                                             \
	(((x) & BIT_MASK_VOQ_TXBD_DESA_8822B) << BIT_SHIFT_VOQ_TXBD_DESA_8822B)
#define BITS_VOQ_TXBD_DESA_8822B                                               \
	(BIT_MASK_VOQ_TXBD_DESA_8822B << BIT_SHIFT_VOQ_TXBD_DESA_8822B)
#define BIT_CLEAR_VOQ_TXBD_DESA_8822B(x) ((x) & (~BITS_VOQ_TXBD_DESA_8822B))
#define BIT_GET_VOQ_TXBD_DESA_8822B(x)                                         \
	(((x) >> BIT_SHIFT_VOQ_TXBD_DESA_8822B) & BIT_MASK_VOQ_TXBD_DESA_8822B)
#define BIT_SET_VOQ_TXBD_DESA_8822B(x, v)                                      \
	(BIT_CLEAR_VOQ_TXBD_DESA_8822B(x) | BIT_VOQ_TXBD_DESA_8822B(v))

/* 2 REG_VIQ_TXBD_DESA_8822B */

#define BIT_SHIFT_VIQ_TXBD_DESA_8822B 0
#define BIT_MASK_VIQ_TXBD_DESA_8822B 0xffffffffffffffffL
#define BIT_VIQ_TXBD_DESA_8822B(x)                                             \
	(((x) & BIT_MASK_VIQ_TXBD_DESA_8822B) << BIT_SHIFT_VIQ_TXBD_DESA_8822B)
#define BITS_VIQ_TXBD_DESA_8822B                                               \
	(BIT_MASK_VIQ_TXBD_DESA_8822B << BIT_SHIFT_VIQ_TXBD_DESA_8822B)
#define BIT_CLEAR_VIQ_TXBD_DESA_8822B(x) ((x) & (~BITS_VIQ_TXBD_DESA_8822B))
#define BIT_GET_VIQ_TXBD_DESA_8822B(x)                                         \
	(((x) >> BIT_SHIFT_VIQ_TXBD_DESA_8822B) & BIT_MASK_VIQ_TXBD_DESA_8822B)
#define BIT_SET_VIQ_TXBD_DESA_8822B(x, v)                                      \
	(BIT_CLEAR_VIQ_TXBD_DESA_8822B(x) | BIT_VIQ_TXBD_DESA_8822B(v))

/* 2 REG_BEQ_TXBD_DESA_8822B */

#define BIT_SHIFT_BEQ_TXBD_DESA_8822B 0
#define BIT_MASK_BEQ_TXBD_DESA_8822B 0xffffffffffffffffL
#define BIT_BEQ_TXBD_DESA_8822B(x)                                             \
	(((x) & BIT_MASK_BEQ_TXBD_DESA_8822B) << BIT_SHIFT_BEQ_TXBD_DESA_8822B)
#define BITS_BEQ_TXBD_DESA_8822B                                               \
	(BIT_MASK_BEQ_TXBD_DESA_8822B << BIT_SHIFT_BEQ_TXBD_DESA_8822B)
#define BIT_CLEAR_BEQ_TXBD_DESA_8822B(x) ((x) & (~BITS_BEQ_TXBD_DESA_8822B))
#define BIT_GET_BEQ_TXBD_DESA_8822B(x)                                         \
	(((x) >> BIT_SHIFT_BEQ_TXBD_DESA_8822B) & BIT_MASK_BEQ_TXBD_DESA_8822B)
#define BIT_SET_BEQ_TXBD_DESA_8822B(x, v)                                      \
	(BIT_CLEAR_BEQ_TXBD_DESA_8822B(x) | BIT_BEQ_TXBD_DESA_8822B(v))

/* 2 REG_BKQ_TXBD_DESA_8822B */

#define BIT_SHIFT_BKQ_TXBD_DESA_8822B 0
#define BIT_MASK_BKQ_TXBD_DESA_8822B 0xffffffffffffffffL
#define BIT_BKQ_TXBD_DESA_8822B(x)                                             \
	(((x) & BIT_MASK_BKQ_TXBD_DESA_8822B) << BIT_SHIFT_BKQ_TXBD_DESA_8822B)
#define BITS_BKQ_TXBD_DESA_8822B                                               \
	(BIT_MASK_BKQ_TXBD_DESA_8822B << BIT_SHIFT_BKQ_TXBD_DESA_8822B)
#define BIT_CLEAR_BKQ_TXBD_DESA_8822B(x) ((x) & (~BITS_BKQ_TXBD_DESA_8822B))
#define BIT_GET_BKQ_TXBD_DESA_8822B(x)                                         \
	(((x) >> BIT_SHIFT_BKQ_TXBD_DESA_8822B) & BIT_MASK_BKQ_TXBD_DESA_8822B)
#define BIT_SET_BKQ_TXBD_DESA_8822B(x, v)                                      \
	(BIT_CLEAR_BKQ_TXBD_DESA_8822B(x) | BIT_BKQ_TXBD_DESA_8822B(v))

/* 2 REG_RXQ_RXBD_DESA_8822B */

#define BIT_SHIFT_RXQ_RXBD_DESA_8822B 0
#define BIT_MASK_RXQ_RXBD_DESA_8822B 0xffffffffffffffffL
#define BIT_RXQ_RXBD_DESA_8822B(x)                                             \
	(((x) & BIT_MASK_RXQ_RXBD_DESA_8822B) << BIT_SHIFT_RXQ_RXBD_DESA_8822B)
#define BITS_RXQ_RXBD_DESA_8822B                                               \
	(BIT_MASK_RXQ_RXBD_DESA_8822B << BIT_SHIFT_RXQ_RXBD_DESA_8822B)
#define BIT_CLEAR_RXQ_RXBD_DESA_8822B(x) ((x) & (~BITS_RXQ_RXBD_DESA_8822B))
#define BIT_GET_RXQ_RXBD_DESA_8822B(x)                                         \
	(((x) >> BIT_SHIFT_RXQ_RXBD_DESA_8822B) & BIT_MASK_RXQ_RXBD_DESA_8822B)
#define BIT_SET_RXQ_RXBD_DESA_8822B(x, v)                                      \
	(BIT_CLEAR_RXQ_RXBD_DESA_8822B(x) | BIT_RXQ_RXBD_DESA_8822B(v))

/* 2 REG_HI0Q_TXBD_DESA_8822B */

#define BIT_SHIFT_HI0Q_TXBD_DESA_8822B 0
#define BIT_MASK_HI0Q_TXBD_DESA_8822B 0xffffffffffffffffL
#define BIT_HI0Q_TXBD_DESA_8822B(x)                                            \
	(((x) & BIT_MASK_HI0Q_TXBD_DESA_8822B)                                 \
	 << BIT_SHIFT_HI0Q_TXBD_DESA_8822B)
#define BITS_HI0Q_TXBD_DESA_8822B                                              \
	(BIT_MASK_HI0Q_TXBD_DESA_8822B << BIT_SHIFT_HI0Q_TXBD_DESA_8822B)
#define BIT_CLEAR_HI0Q_TXBD_DESA_8822B(x) ((x) & (~BITS_HI0Q_TXBD_DESA_8822B))
#define BIT_GET_HI0Q_TXBD_DESA_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HI0Q_TXBD_DESA_8822B) &                             \
	 BIT_MASK_HI0Q_TXBD_DESA_8822B)
#define BIT_SET_HI0Q_TXBD_DESA_8822B(x, v)                                     \
	(BIT_CLEAR_HI0Q_TXBD_DESA_8822B(x) | BIT_HI0Q_TXBD_DESA_8822B(v))

/* 2 REG_HI1Q_TXBD_DESA_8822B */

#define BIT_SHIFT_HI1Q_TXBD_DESA_8822B 0
#define BIT_MASK_HI1Q_TXBD_DESA_8822B 0xffffffffffffffffL
#define BIT_HI1Q_TXBD_DESA_8822B(x)                                            \
	(((x) & BIT_MASK_HI1Q_TXBD_DESA_8822B)                                 \
	 << BIT_SHIFT_HI1Q_TXBD_DESA_8822B)
#define BITS_HI1Q_TXBD_DESA_8822B                                              \
	(BIT_MASK_HI1Q_TXBD_DESA_8822B << BIT_SHIFT_HI1Q_TXBD_DESA_8822B)
#define BIT_CLEAR_HI1Q_TXBD_DESA_8822B(x) ((x) & (~BITS_HI1Q_TXBD_DESA_8822B))
#define BIT_GET_HI1Q_TXBD_DESA_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HI1Q_TXBD_DESA_8822B) &                             \
	 BIT_MASK_HI1Q_TXBD_DESA_8822B)
#define BIT_SET_HI1Q_TXBD_DESA_8822B(x, v)                                     \
	(BIT_CLEAR_HI1Q_TXBD_DESA_8822B(x) | BIT_HI1Q_TXBD_DESA_8822B(v))

/* 2 REG_HI2Q_TXBD_DESA_8822B */

#define BIT_SHIFT_HI2Q_TXBD_DESA_8822B 0
#define BIT_MASK_HI2Q_TXBD_DESA_8822B 0xffffffffffffffffL
#define BIT_HI2Q_TXBD_DESA_8822B(x)                                            \
	(((x) & BIT_MASK_HI2Q_TXBD_DESA_8822B)                                 \
	 << BIT_SHIFT_HI2Q_TXBD_DESA_8822B)
#define BITS_HI2Q_TXBD_DESA_8822B                                              \
	(BIT_MASK_HI2Q_TXBD_DESA_8822B << BIT_SHIFT_HI2Q_TXBD_DESA_8822B)
#define BIT_CLEAR_HI2Q_TXBD_DESA_8822B(x) ((x) & (~BITS_HI2Q_TXBD_DESA_8822B))
#define BIT_GET_HI2Q_TXBD_DESA_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HI2Q_TXBD_DESA_8822B) &                             \
	 BIT_MASK_HI2Q_TXBD_DESA_8822B)
#define BIT_SET_HI2Q_TXBD_DESA_8822B(x, v)                                     \
	(BIT_CLEAR_HI2Q_TXBD_DESA_8822B(x) | BIT_HI2Q_TXBD_DESA_8822B(v))

/* 2 REG_HI3Q_TXBD_DESA_8822B */

#define BIT_SHIFT_HI3Q_TXBD_DESA_8822B 0
#define BIT_MASK_HI3Q_TXBD_DESA_8822B 0xffffffffffffffffL
#define BIT_HI3Q_TXBD_DESA_8822B(x)                                            \
	(((x) & BIT_MASK_HI3Q_TXBD_DESA_8822B)                                 \
	 << BIT_SHIFT_HI3Q_TXBD_DESA_8822B)
#define BITS_HI3Q_TXBD_DESA_8822B                                              \
	(BIT_MASK_HI3Q_TXBD_DESA_8822B << BIT_SHIFT_HI3Q_TXBD_DESA_8822B)
#define BIT_CLEAR_HI3Q_TXBD_DESA_8822B(x) ((x) & (~BITS_HI3Q_TXBD_DESA_8822B))
#define BIT_GET_HI3Q_TXBD_DESA_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HI3Q_TXBD_DESA_8822B) &                             \
	 BIT_MASK_HI3Q_TXBD_DESA_8822B)
#define BIT_SET_HI3Q_TXBD_DESA_8822B(x, v)                                     \
	(BIT_CLEAR_HI3Q_TXBD_DESA_8822B(x) | BIT_HI3Q_TXBD_DESA_8822B(v))

/* 2 REG_HI4Q_TXBD_DESA_8822B */

#define BIT_SHIFT_HI4Q_TXBD_DESA_8822B 0
#define BIT_MASK_HI4Q_TXBD_DESA_8822B 0xffffffffffffffffL
#define BIT_HI4Q_TXBD_DESA_8822B(x)                                            \
	(((x) & BIT_MASK_HI4Q_TXBD_DESA_8822B)                                 \
	 << BIT_SHIFT_HI4Q_TXBD_DESA_8822B)
#define BITS_HI4Q_TXBD_DESA_8822B                                              \
	(BIT_MASK_HI4Q_TXBD_DESA_8822B << BIT_SHIFT_HI4Q_TXBD_DESA_8822B)
#define BIT_CLEAR_HI4Q_TXBD_DESA_8822B(x) ((x) & (~BITS_HI4Q_TXBD_DESA_8822B))
#define BIT_GET_HI4Q_TXBD_DESA_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HI4Q_TXBD_DESA_8822B) &                             \
	 BIT_MASK_HI4Q_TXBD_DESA_8822B)
#define BIT_SET_HI4Q_TXBD_DESA_8822B(x, v)                                     \
	(BIT_CLEAR_HI4Q_TXBD_DESA_8822B(x) | BIT_HI4Q_TXBD_DESA_8822B(v))

/* 2 REG_HI5Q_TXBD_DESA_8822B */

#define BIT_SHIFT_HI5Q_TXBD_DESA_8822B 0
#define BIT_MASK_HI5Q_TXBD_DESA_8822B 0xffffffffffffffffL
#define BIT_HI5Q_TXBD_DESA_8822B(x)                                            \
	(((x) & BIT_MASK_HI5Q_TXBD_DESA_8822B)                                 \
	 << BIT_SHIFT_HI5Q_TXBD_DESA_8822B)
#define BITS_HI5Q_TXBD_DESA_8822B                                              \
	(BIT_MASK_HI5Q_TXBD_DESA_8822B << BIT_SHIFT_HI5Q_TXBD_DESA_8822B)
#define BIT_CLEAR_HI5Q_TXBD_DESA_8822B(x) ((x) & (~BITS_HI5Q_TXBD_DESA_8822B))
#define BIT_GET_HI5Q_TXBD_DESA_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HI5Q_TXBD_DESA_8822B) &                             \
	 BIT_MASK_HI5Q_TXBD_DESA_8822B)
#define BIT_SET_HI5Q_TXBD_DESA_8822B(x, v)                                     \
	(BIT_CLEAR_HI5Q_TXBD_DESA_8822B(x) | BIT_HI5Q_TXBD_DESA_8822B(v))

/* 2 REG_HI6Q_TXBD_DESA_8822B */

#define BIT_SHIFT_HI6Q_TXBD_DESA_8822B 0
#define BIT_MASK_HI6Q_TXBD_DESA_8822B 0xffffffffffffffffL
#define BIT_HI6Q_TXBD_DESA_8822B(x)                                            \
	(((x) & BIT_MASK_HI6Q_TXBD_DESA_8822B)                                 \
	 << BIT_SHIFT_HI6Q_TXBD_DESA_8822B)
#define BITS_HI6Q_TXBD_DESA_8822B                                              \
	(BIT_MASK_HI6Q_TXBD_DESA_8822B << BIT_SHIFT_HI6Q_TXBD_DESA_8822B)
#define BIT_CLEAR_HI6Q_TXBD_DESA_8822B(x) ((x) & (~BITS_HI6Q_TXBD_DESA_8822B))
#define BIT_GET_HI6Q_TXBD_DESA_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HI6Q_TXBD_DESA_8822B) &                             \
	 BIT_MASK_HI6Q_TXBD_DESA_8822B)
#define BIT_SET_HI6Q_TXBD_DESA_8822B(x, v)                                     \
	(BIT_CLEAR_HI6Q_TXBD_DESA_8822B(x) | BIT_HI6Q_TXBD_DESA_8822B(v))

/* 2 REG_HI7Q_TXBD_DESA_8822B */

#define BIT_SHIFT_HI7Q_TXBD_DESA_8822B 0
#define BIT_MASK_HI7Q_TXBD_DESA_8822B 0xffffffffffffffffL
#define BIT_HI7Q_TXBD_DESA_8822B(x)                                            \
	(((x) & BIT_MASK_HI7Q_TXBD_DESA_8822B)                                 \
	 << BIT_SHIFT_HI7Q_TXBD_DESA_8822B)
#define BITS_HI7Q_TXBD_DESA_8822B                                              \
	(BIT_MASK_HI7Q_TXBD_DESA_8822B << BIT_SHIFT_HI7Q_TXBD_DESA_8822B)
#define BIT_CLEAR_HI7Q_TXBD_DESA_8822B(x) ((x) & (~BITS_HI7Q_TXBD_DESA_8822B))
#define BIT_GET_HI7Q_TXBD_DESA_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HI7Q_TXBD_DESA_8822B) &                             \
	 BIT_MASK_HI7Q_TXBD_DESA_8822B)
#define BIT_SET_HI7Q_TXBD_DESA_8822B(x, v)                                     \
	(BIT_CLEAR_HI7Q_TXBD_DESA_8822B(x) | BIT_HI7Q_TXBD_DESA_8822B(v))

/* 2 REG_MGQ_TXBD_NUM_8822B */
#define BIT_PCIE_MGQ_FLAG_8822B BIT(14)

#define BIT_SHIFT_MGQ_DESC_MODE_8822B 12
#define BIT_MASK_MGQ_DESC_MODE_8822B 0x3
#define BIT_MGQ_DESC_MODE_8822B(x)                                             \
	(((x) & BIT_MASK_MGQ_DESC_MODE_8822B) << BIT_SHIFT_MGQ_DESC_MODE_8822B)
#define BITS_MGQ_DESC_MODE_8822B                                               \
	(BIT_MASK_MGQ_DESC_MODE_8822B << BIT_SHIFT_MGQ_DESC_MODE_8822B)
#define BIT_CLEAR_MGQ_DESC_MODE_8822B(x) ((x) & (~BITS_MGQ_DESC_MODE_8822B))
#define BIT_GET_MGQ_DESC_MODE_8822B(x)                                         \
	(((x) >> BIT_SHIFT_MGQ_DESC_MODE_8822B) & BIT_MASK_MGQ_DESC_MODE_8822B)
#define BIT_SET_MGQ_DESC_MODE_8822B(x, v)                                      \
	(BIT_CLEAR_MGQ_DESC_MODE_8822B(x) | BIT_MGQ_DESC_MODE_8822B(v))

#define BIT_SHIFT_MGQ_DESC_NUM_8822B 0
#define BIT_MASK_MGQ_DESC_NUM_8822B 0xfff
#define BIT_MGQ_DESC_NUM_8822B(x)                                              \
	(((x) & BIT_MASK_MGQ_DESC_NUM_8822B) << BIT_SHIFT_MGQ_DESC_NUM_8822B)
#define BITS_MGQ_DESC_NUM_8822B                                                \
	(BIT_MASK_MGQ_DESC_NUM_8822B << BIT_SHIFT_MGQ_DESC_NUM_8822B)
#define BIT_CLEAR_MGQ_DESC_NUM_8822B(x) ((x) & (~BITS_MGQ_DESC_NUM_8822B))
#define BIT_GET_MGQ_DESC_NUM_8822B(x)                                          \
	(((x) >> BIT_SHIFT_MGQ_DESC_NUM_8822B) & BIT_MASK_MGQ_DESC_NUM_8822B)
#define BIT_SET_MGQ_DESC_NUM_8822B(x, v)                                       \
	(BIT_CLEAR_MGQ_DESC_NUM_8822B(x) | BIT_MGQ_DESC_NUM_8822B(v))

/* 2 REG_RX_RXBD_NUM_8822B */
#define BIT_SYS_32_64_8822B BIT(15)

#define BIT_SHIFT_BCNQ_DESC_MODE_8822B 13
#define BIT_MASK_BCNQ_DESC_MODE_8822B 0x3
#define BIT_BCNQ_DESC_MODE_8822B(x)                                            \
	(((x) & BIT_MASK_BCNQ_DESC_MODE_8822B)                                 \
	 << BIT_SHIFT_BCNQ_DESC_MODE_8822B)
#define BITS_BCNQ_DESC_MODE_8822B                                              \
	(BIT_MASK_BCNQ_DESC_MODE_8822B << BIT_SHIFT_BCNQ_DESC_MODE_8822B)
#define BIT_CLEAR_BCNQ_DESC_MODE_8822B(x) ((x) & (~BITS_BCNQ_DESC_MODE_8822B))
#define BIT_GET_BCNQ_DESC_MODE_8822B(x)                                        \
	(((x) >> BIT_SHIFT_BCNQ_DESC_MODE_8822B) &                             \
	 BIT_MASK_BCNQ_DESC_MODE_8822B)
#define BIT_SET_BCNQ_DESC_MODE_8822B(x, v)                                     \
	(BIT_CLEAR_BCNQ_DESC_MODE_8822B(x) | BIT_BCNQ_DESC_MODE_8822B(v))

#define BIT_PCIE_BCNQ_FLAG_8822B BIT(12)

#define BIT_SHIFT_RXQ_DESC_NUM_8822B 0
#define BIT_MASK_RXQ_DESC_NUM_8822B 0xfff
#define BIT_RXQ_DESC_NUM_8822B(x)                                              \
	(((x) & BIT_MASK_RXQ_DESC_NUM_8822B) << BIT_SHIFT_RXQ_DESC_NUM_8822B)
#define BITS_RXQ_DESC_NUM_8822B                                                \
	(BIT_MASK_RXQ_DESC_NUM_8822B << BIT_SHIFT_RXQ_DESC_NUM_8822B)
#define BIT_CLEAR_RXQ_DESC_NUM_8822B(x) ((x) & (~BITS_RXQ_DESC_NUM_8822B))
#define BIT_GET_RXQ_DESC_NUM_8822B(x)                                          \
	(((x) >> BIT_SHIFT_RXQ_DESC_NUM_8822B) & BIT_MASK_RXQ_DESC_NUM_8822B)
#define BIT_SET_RXQ_DESC_NUM_8822B(x, v)                                       \
	(BIT_CLEAR_RXQ_DESC_NUM_8822B(x) | BIT_RXQ_DESC_NUM_8822B(v))

/* 2 REG_VOQ_TXBD_NUM_8822B */
#define BIT_PCIE_VOQ_FLAG_8822B BIT(14)

#define BIT_SHIFT_VOQ_DESC_MODE_8822B 12
#define BIT_MASK_VOQ_DESC_MODE_8822B 0x3
#define BIT_VOQ_DESC_MODE_8822B(x)                                             \
	(((x) & BIT_MASK_VOQ_DESC_MODE_8822B) << BIT_SHIFT_VOQ_DESC_MODE_8822B)
#define BITS_VOQ_DESC_MODE_8822B                                               \
	(BIT_MASK_VOQ_DESC_MODE_8822B << BIT_SHIFT_VOQ_DESC_MODE_8822B)
#define BIT_CLEAR_VOQ_DESC_MODE_8822B(x) ((x) & (~BITS_VOQ_DESC_MODE_8822B))
#define BIT_GET_VOQ_DESC_MODE_8822B(x)                                         \
	(((x) >> BIT_SHIFT_VOQ_DESC_MODE_8822B) & BIT_MASK_VOQ_DESC_MODE_8822B)
#define BIT_SET_VOQ_DESC_MODE_8822B(x, v)                                      \
	(BIT_CLEAR_VOQ_DESC_MODE_8822B(x) | BIT_VOQ_DESC_MODE_8822B(v))

#define BIT_SHIFT_VOQ_DESC_NUM_8822B 0
#define BIT_MASK_VOQ_DESC_NUM_8822B 0xfff
#define BIT_VOQ_DESC_NUM_8822B(x)                                              \
	(((x) & BIT_MASK_VOQ_DESC_NUM_8822B) << BIT_SHIFT_VOQ_DESC_NUM_8822B)
#define BITS_VOQ_DESC_NUM_8822B                                                \
	(BIT_MASK_VOQ_DESC_NUM_8822B << BIT_SHIFT_VOQ_DESC_NUM_8822B)
#define BIT_CLEAR_VOQ_DESC_NUM_8822B(x) ((x) & (~BITS_VOQ_DESC_NUM_8822B))
#define BIT_GET_VOQ_DESC_NUM_8822B(x)                                          \
	(((x) >> BIT_SHIFT_VOQ_DESC_NUM_8822B) & BIT_MASK_VOQ_DESC_NUM_8822B)
#define BIT_SET_VOQ_DESC_NUM_8822B(x, v)                                       \
	(BIT_CLEAR_VOQ_DESC_NUM_8822B(x) | BIT_VOQ_DESC_NUM_8822B(v))

/* 2 REG_VIQ_TXBD_NUM_8822B */
#define BIT_PCIE_VIQ_FLAG_8822B BIT(14)

#define BIT_SHIFT_VIQ_DESC_MODE_8822B 12
#define BIT_MASK_VIQ_DESC_MODE_8822B 0x3
#define BIT_VIQ_DESC_MODE_8822B(x)                                             \
	(((x) & BIT_MASK_VIQ_DESC_MODE_8822B) << BIT_SHIFT_VIQ_DESC_MODE_8822B)
#define BITS_VIQ_DESC_MODE_8822B                                               \
	(BIT_MASK_VIQ_DESC_MODE_8822B << BIT_SHIFT_VIQ_DESC_MODE_8822B)
#define BIT_CLEAR_VIQ_DESC_MODE_8822B(x) ((x) & (~BITS_VIQ_DESC_MODE_8822B))
#define BIT_GET_VIQ_DESC_MODE_8822B(x)                                         \
	(((x) >> BIT_SHIFT_VIQ_DESC_MODE_8822B) & BIT_MASK_VIQ_DESC_MODE_8822B)
#define BIT_SET_VIQ_DESC_MODE_8822B(x, v)                                      \
	(BIT_CLEAR_VIQ_DESC_MODE_8822B(x) | BIT_VIQ_DESC_MODE_8822B(v))

#define BIT_SHIFT_VIQ_DESC_NUM_8822B 0
#define BIT_MASK_VIQ_DESC_NUM_8822B 0xfff
#define BIT_VIQ_DESC_NUM_8822B(x)                                              \
	(((x) & BIT_MASK_VIQ_DESC_NUM_8822B) << BIT_SHIFT_VIQ_DESC_NUM_8822B)
#define BITS_VIQ_DESC_NUM_8822B                                                \
	(BIT_MASK_VIQ_DESC_NUM_8822B << BIT_SHIFT_VIQ_DESC_NUM_8822B)
#define BIT_CLEAR_VIQ_DESC_NUM_8822B(x) ((x) & (~BITS_VIQ_DESC_NUM_8822B))
#define BIT_GET_VIQ_DESC_NUM_8822B(x)                                          \
	(((x) >> BIT_SHIFT_VIQ_DESC_NUM_8822B) & BIT_MASK_VIQ_DESC_NUM_8822B)
#define BIT_SET_VIQ_DESC_NUM_8822B(x, v)                                       \
	(BIT_CLEAR_VIQ_DESC_NUM_8822B(x) | BIT_VIQ_DESC_NUM_8822B(v))

/* 2 REG_BEQ_TXBD_NUM_8822B */
#define BIT_PCIE_BEQ_FLAG_8822B BIT(14)

#define BIT_SHIFT_BEQ_DESC_MODE_8822B 12
#define BIT_MASK_BEQ_DESC_MODE_8822B 0x3
#define BIT_BEQ_DESC_MODE_8822B(x)                                             \
	(((x) & BIT_MASK_BEQ_DESC_MODE_8822B) << BIT_SHIFT_BEQ_DESC_MODE_8822B)
#define BITS_BEQ_DESC_MODE_8822B                                               \
	(BIT_MASK_BEQ_DESC_MODE_8822B << BIT_SHIFT_BEQ_DESC_MODE_8822B)
#define BIT_CLEAR_BEQ_DESC_MODE_8822B(x) ((x) & (~BITS_BEQ_DESC_MODE_8822B))
#define BIT_GET_BEQ_DESC_MODE_8822B(x)                                         \
	(((x) >> BIT_SHIFT_BEQ_DESC_MODE_8822B) & BIT_MASK_BEQ_DESC_MODE_8822B)
#define BIT_SET_BEQ_DESC_MODE_8822B(x, v)                                      \
	(BIT_CLEAR_BEQ_DESC_MODE_8822B(x) | BIT_BEQ_DESC_MODE_8822B(v))

#define BIT_SHIFT_BEQ_DESC_NUM_8822B 0
#define BIT_MASK_BEQ_DESC_NUM_8822B 0xfff
#define BIT_BEQ_DESC_NUM_8822B(x)                                              \
	(((x) & BIT_MASK_BEQ_DESC_NUM_8822B) << BIT_SHIFT_BEQ_DESC_NUM_8822B)
#define BITS_BEQ_DESC_NUM_8822B                                                \
	(BIT_MASK_BEQ_DESC_NUM_8822B << BIT_SHIFT_BEQ_DESC_NUM_8822B)
#define BIT_CLEAR_BEQ_DESC_NUM_8822B(x) ((x) & (~BITS_BEQ_DESC_NUM_8822B))
#define BIT_GET_BEQ_DESC_NUM_8822B(x)                                          \
	(((x) >> BIT_SHIFT_BEQ_DESC_NUM_8822B) & BIT_MASK_BEQ_DESC_NUM_8822B)
#define BIT_SET_BEQ_DESC_NUM_8822B(x, v)                                       \
	(BIT_CLEAR_BEQ_DESC_NUM_8822B(x) | BIT_BEQ_DESC_NUM_8822B(v))

/* 2 REG_BKQ_TXBD_NUM_8822B */
#define BIT_PCIE_BKQ_FLAG_8822B BIT(14)

#define BIT_SHIFT_BKQ_DESC_MODE_8822B 12
#define BIT_MASK_BKQ_DESC_MODE_8822B 0x3
#define BIT_BKQ_DESC_MODE_8822B(x)                                             \
	(((x) & BIT_MASK_BKQ_DESC_MODE_8822B) << BIT_SHIFT_BKQ_DESC_MODE_8822B)
#define BITS_BKQ_DESC_MODE_8822B                                               \
	(BIT_MASK_BKQ_DESC_MODE_8822B << BIT_SHIFT_BKQ_DESC_MODE_8822B)
#define BIT_CLEAR_BKQ_DESC_MODE_8822B(x) ((x) & (~BITS_BKQ_DESC_MODE_8822B))
#define BIT_GET_BKQ_DESC_MODE_8822B(x)                                         \
	(((x) >> BIT_SHIFT_BKQ_DESC_MODE_8822B) & BIT_MASK_BKQ_DESC_MODE_8822B)
#define BIT_SET_BKQ_DESC_MODE_8822B(x, v)                                      \
	(BIT_CLEAR_BKQ_DESC_MODE_8822B(x) | BIT_BKQ_DESC_MODE_8822B(v))

#define BIT_SHIFT_BKQ_DESC_NUM_8822B 0
#define BIT_MASK_BKQ_DESC_NUM_8822B 0xfff
#define BIT_BKQ_DESC_NUM_8822B(x)                                              \
	(((x) & BIT_MASK_BKQ_DESC_NUM_8822B) << BIT_SHIFT_BKQ_DESC_NUM_8822B)
#define BITS_BKQ_DESC_NUM_8822B                                                \
	(BIT_MASK_BKQ_DESC_NUM_8822B << BIT_SHIFT_BKQ_DESC_NUM_8822B)
#define BIT_CLEAR_BKQ_DESC_NUM_8822B(x) ((x) & (~BITS_BKQ_DESC_NUM_8822B))
#define BIT_GET_BKQ_DESC_NUM_8822B(x)                                          \
	(((x) >> BIT_SHIFT_BKQ_DESC_NUM_8822B) & BIT_MASK_BKQ_DESC_NUM_8822B)
#define BIT_SET_BKQ_DESC_NUM_8822B(x, v)                                       \
	(BIT_CLEAR_BKQ_DESC_NUM_8822B(x) | BIT_BKQ_DESC_NUM_8822B(v))

/* 2 REG_HI0Q_TXBD_NUM_8822B */
#define BIT_HI0Q_FLAG_8822B BIT(14)

#define BIT_SHIFT_HI0Q_DESC_MODE_8822B 12
#define BIT_MASK_HI0Q_DESC_MODE_8822B 0x3
#define BIT_HI0Q_DESC_MODE_8822B(x)                                            \
	(((x) & BIT_MASK_HI0Q_DESC_MODE_8822B)                                 \
	 << BIT_SHIFT_HI0Q_DESC_MODE_8822B)
#define BITS_HI0Q_DESC_MODE_8822B                                              \
	(BIT_MASK_HI0Q_DESC_MODE_8822B << BIT_SHIFT_HI0Q_DESC_MODE_8822B)
#define BIT_CLEAR_HI0Q_DESC_MODE_8822B(x) ((x) & (~BITS_HI0Q_DESC_MODE_8822B))
#define BIT_GET_HI0Q_DESC_MODE_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HI0Q_DESC_MODE_8822B) &                             \
	 BIT_MASK_HI0Q_DESC_MODE_8822B)
#define BIT_SET_HI0Q_DESC_MODE_8822B(x, v)                                     \
	(BIT_CLEAR_HI0Q_DESC_MODE_8822B(x) | BIT_HI0Q_DESC_MODE_8822B(v))

#define BIT_SHIFT_HI0Q_DESC_NUM_8822B 0
#define BIT_MASK_HI0Q_DESC_NUM_8822B 0xfff
#define BIT_HI0Q_DESC_NUM_8822B(x)                                             \
	(((x) & BIT_MASK_HI0Q_DESC_NUM_8822B) << BIT_SHIFT_HI0Q_DESC_NUM_8822B)
#define BITS_HI0Q_DESC_NUM_8822B                                               \
	(BIT_MASK_HI0Q_DESC_NUM_8822B << BIT_SHIFT_HI0Q_DESC_NUM_8822B)
#define BIT_CLEAR_HI0Q_DESC_NUM_8822B(x) ((x) & (~BITS_HI0Q_DESC_NUM_8822B))
#define BIT_GET_HI0Q_DESC_NUM_8822B(x)                                         \
	(((x) >> BIT_SHIFT_HI0Q_DESC_NUM_8822B) & BIT_MASK_HI0Q_DESC_NUM_8822B)
#define BIT_SET_HI0Q_DESC_NUM_8822B(x, v)                                      \
	(BIT_CLEAR_HI0Q_DESC_NUM_8822B(x) | BIT_HI0Q_DESC_NUM_8822B(v))

/* 2 REG_HI1Q_TXBD_NUM_8822B */
#define BIT_HI1Q_FLAG_8822B BIT(14)

#define BIT_SHIFT_HI1Q_DESC_MODE_8822B 12
#define BIT_MASK_HI1Q_DESC_MODE_8822B 0x3
#define BIT_HI1Q_DESC_MODE_8822B(x)                                            \
	(((x) & BIT_MASK_HI1Q_DESC_MODE_8822B)                                 \
	 << BIT_SHIFT_HI1Q_DESC_MODE_8822B)
#define BITS_HI1Q_DESC_MODE_8822B                                              \
	(BIT_MASK_HI1Q_DESC_MODE_8822B << BIT_SHIFT_HI1Q_DESC_MODE_8822B)
#define BIT_CLEAR_HI1Q_DESC_MODE_8822B(x) ((x) & (~BITS_HI1Q_DESC_MODE_8822B))
#define BIT_GET_HI1Q_DESC_MODE_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HI1Q_DESC_MODE_8822B) &                             \
	 BIT_MASK_HI1Q_DESC_MODE_8822B)
#define BIT_SET_HI1Q_DESC_MODE_8822B(x, v)                                     \
	(BIT_CLEAR_HI1Q_DESC_MODE_8822B(x) | BIT_HI1Q_DESC_MODE_8822B(v))

#define BIT_SHIFT_HI1Q_DESC_NUM_8822B 0
#define BIT_MASK_HI1Q_DESC_NUM_8822B 0xfff
#define BIT_HI1Q_DESC_NUM_8822B(x)                                             \
	(((x) & BIT_MASK_HI1Q_DESC_NUM_8822B) << BIT_SHIFT_HI1Q_DESC_NUM_8822B)
#define BITS_HI1Q_DESC_NUM_8822B                                               \
	(BIT_MASK_HI1Q_DESC_NUM_8822B << BIT_SHIFT_HI1Q_DESC_NUM_8822B)
#define BIT_CLEAR_HI1Q_DESC_NUM_8822B(x) ((x) & (~BITS_HI1Q_DESC_NUM_8822B))
#define BIT_GET_HI1Q_DESC_NUM_8822B(x)                                         \
	(((x) >> BIT_SHIFT_HI1Q_DESC_NUM_8822B) & BIT_MASK_HI1Q_DESC_NUM_8822B)
#define BIT_SET_HI1Q_DESC_NUM_8822B(x, v)                                      \
	(BIT_CLEAR_HI1Q_DESC_NUM_8822B(x) | BIT_HI1Q_DESC_NUM_8822B(v))

/* 2 REG_HI2Q_TXBD_NUM_8822B */
#define BIT_HI2Q_FLAG_8822B BIT(14)

#define BIT_SHIFT_HI2Q_DESC_MODE_8822B 12
#define BIT_MASK_HI2Q_DESC_MODE_8822B 0x3
#define BIT_HI2Q_DESC_MODE_8822B(x)                                            \
	(((x) & BIT_MASK_HI2Q_DESC_MODE_8822B)                                 \
	 << BIT_SHIFT_HI2Q_DESC_MODE_8822B)
#define BITS_HI2Q_DESC_MODE_8822B                                              \
	(BIT_MASK_HI2Q_DESC_MODE_8822B << BIT_SHIFT_HI2Q_DESC_MODE_8822B)
#define BIT_CLEAR_HI2Q_DESC_MODE_8822B(x) ((x) & (~BITS_HI2Q_DESC_MODE_8822B))
#define BIT_GET_HI2Q_DESC_MODE_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HI2Q_DESC_MODE_8822B) &                             \
	 BIT_MASK_HI2Q_DESC_MODE_8822B)
#define BIT_SET_HI2Q_DESC_MODE_8822B(x, v)                                     \
	(BIT_CLEAR_HI2Q_DESC_MODE_8822B(x) | BIT_HI2Q_DESC_MODE_8822B(v))

#define BIT_SHIFT_HI2Q_DESC_NUM_8822B 0
#define BIT_MASK_HI2Q_DESC_NUM_8822B 0xfff
#define BIT_HI2Q_DESC_NUM_8822B(x)                                             \
	(((x) & BIT_MASK_HI2Q_DESC_NUM_8822B) << BIT_SHIFT_HI2Q_DESC_NUM_8822B)
#define BITS_HI2Q_DESC_NUM_8822B                                               \
	(BIT_MASK_HI2Q_DESC_NUM_8822B << BIT_SHIFT_HI2Q_DESC_NUM_8822B)
#define BIT_CLEAR_HI2Q_DESC_NUM_8822B(x) ((x) & (~BITS_HI2Q_DESC_NUM_8822B))
#define BIT_GET_HI2Q_DESC_NUM_8822B(x)                                         \
	(((x) >> BIT_SHIFT_HI2Q_DESC_NUM_8822B) & BIT_MASK_HI2Q_DESC_NUM_8822B)
#define BIT_SET_HI2Q_DESC_NUM_8822B(x, v)                                      \
	(BIT_CLEAR_HI2Q_DESC_NUM_8822B(x) | BIT_HI2Q_DESC_NUM_8822B(v))

/* 2 REG_HI3Q_TXBD_NUM_8822B */
#define BIT_HI3Q_FLAG_8822B BIT(14)

#define BIT_SHIFT_HI3Q_DESC_MODE_8822B 12
#define BIT_MASK_HI3Q_DESC_MODE_8822B 0x3
#define BIT_HI3Q_DESC_MODE_8822B(x)                                            \
	(((x) & BIT_MASK_HI3Q_DESC_MODE_8822B)                                 \
	 << BIT_SHIFT_HI3Q_DESC_MODE_8822B)
#define BITS_HI3Q_DESC_MODE_8822B                                              \
	(BIT_MASK_HI3Q_DESC_MODE_8822B << BIT_SHIFT_HI3Q_DESC_MODE_8822B)
#define BIT_CLEAR_HI3Q_DESC_MODE_8822B(x) ((x) & (~BITS_HI3Q_DESC_MODE_8822B))
#define BIT_GET_HI3Q_DESC_MODE_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HI3Q_DESC_MODE_8822B) &                             \
	 BIT_MASK_HI3Q_DESC_MODE_8822B)
#define BIT_SET_HI3Q_DESC_MODE_8822B(x, v)                                     \
	(BIT_CLEAR_HI3Q_DESC_MODE_8822B(x) | BIT_HI3Q_DESC_MODE_8822B(v))

#define BIT_SHIFT_HI3Q_DESC_NUM_8822B 0
#define BIT_MASK_HI3Q_DESC_NUM_8822B 0xfff
#define BIT_HI3Q_DESC_NUM_8822B(x)                                             \
	(((x) & BIT_MASK_HI3Q_DESC_NUM_8822B) << BIT_SHIFT_HI3Q_DESC_NUM_8822B)
#define BITS_HI3Q_DESC_NUM_8822B                                               \
	(BIT_MASK_HI3Q_DESC_NUM_8822B << BIT_SHIFT_HI3Q_DESC_NUM_8822B)
#define BIT_CLEAR_HI3Q_DESC_NUM_8822B(x) ((x) & (~BITS_HI3Q_DESC_NUM_8822B))
#define BIT_GET_HI3Q_DESC_NUM_8822B(x)                                         \
	(((x) >> BIT_SHIFT_HI3Q_DESC_NUM_8822B) & BIT_MASK_HI3Q_DESC_NUM_8822B)
#define BIT_SET_HI3Q_DESC_NUM_8822B(x, v)                                      \
	(BIT_CLEAR_HI3Q_DESC_NUM_8822B(x) | BIT_HI3Q_DESC_NUM_8822B(v))

/* 2 REG_HI4Q_TXBD_NUM_8822B */
#define BIT_HI4Q_FLAG_8822B BIT(14)

#define BIT_SHIFT_HI4Q_DESC_MODE_8822B 12
#define BIT_MASK_HI4Q_DESC_MODE_8822B 0x3
#define BIT_HI4Q_DESC_MODE_8822B(x)                                            \
	(((x) & BIT_MASK_HI4Q_DESC_MODE_8822B)                                 \
	 << BIT_SHIFT_HI4Q_DESC_MODE_8822B)
#define BITS_HI4Q_DESC_MODE_8822B                                              \
	(BIT_MASK_HI4Q_DESC_MODE_8822B << BIT_SHIFT_HI4Q_DESC_MODE_8822B)
#define BIT_CLEAR_HI4Q_DESC_MODE_8822B(x) ((x) & (~BITS_HI4Q_DESC_MODE_8822B))
#define BIT_GET_HI4Q_DESC_MODE_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HI4Q_DESC_MODE_8822B) &                             \
	 BIT_MASK_HI4Q_DESC_MODE_8822B)
#define BIT_SET_HI4Q_DESC_MODE_8822B(x, v)                                     \
	(BIT_CLEAR_HI4Q_DESC_MODE_8822B(x) | BIT_HI4Q_DESC_MODE_8822B(v))

#define BIT_SHIFT_HI4Q_DESC_NUM_8822B 0
#define BIT_MASK_HI4Q_DESC_NUM_8822B 0xfff
#define BIT_HI4Q_DESC_NUM_8822B(x)                                             \
	(((x) & BIT_MASK_HI4Q_DESC_NUM_8822B) << BIT_SHIFT_HI4Q_DESC_NUM_8822B)
#define BITS_HI4Q_DESC_NUM_8822B                                               \
	(BIT_MASK_HI4Q_DESC_NUM_8822B << BIT_SHIFT_HI4Q_DESC_NUM_8822B)
#define BIT_CLEAR_HI4Q_DESC_NUM_8822B(x) ((x) & (~BITS_HI4Q_DESC_NUM_8822B))
#define BIT_GET_HI4Q_DESC_NUM_8822B(x)                                         \
	(((x) >> BIT_SHIFT_HI4Q_DESC_NUM_8822B) & BIT_MASK_HI4Q_DESC_NUM_8822B)
#define BIT_SET_HI4Q_DESC_NUM_8822B(x, v)                                      \
	(BIT_CLEAR_HI4Q_DESC_NUM_8822B(x) | BIT_HI4Q_DESC_NUM_8822B(v))

/* 2 REG_HI5Q_TXBD_NUM_8822B */
#define BIT_HI5Q_FLAG_8822B BIT(14)

#define BIT_SHIFT_HI5Q_DESC_MODE_8822B 12
#define BIT_MASK_HI5Q_DESC_MODE_8822B 0x3
#define BIT_HI5Q_DESC_MODE_8822B(x)                                            \
	(((x) & BIT_MASK_HI5Q_DESC_MODE_8822B)                                 \
	 << BIT_SHIFT_HI5Q_DESC_MODE_8822B)
#define BITS_HI5Q_DESC_MODE_8822B                                              \
	(BIT_MASK_HI5Q_DESC_MODE_8822B << BIT_SHIFT_HI5Q_DESC_MODE_8822B)
#define BIT_CLEAR_HI5Q_DESC_MODE_8822B(x) ((x) & (~BITS_HI5Q_DESC_MODE_8822B))
#define BIT_GET_HI5Q_DESC_MODE_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HI5Q_DESC_MODE_8822B) &                             \
	 BIT_MASK_HI5Q_DESC_MODE_8822B)
#define BIT_SET_HI5Q_DESC_MODE_8822B(x, v)                                     \
	(BIT_CLEAR_HI5Q_DESC_MODE_8822B(x) | BIT_HI5Q_DESC_MODE_8822B(v))

#define BIT_SHIFT_HI5Q_DESC_NUM_8822B 0
#define BIT_MASK_HI5Q_DESC_NUM_8822B 0xfff
#define BIT_HI5Q_DESC_NUM_8822B(x)                                             \
	(((x) & BIT_MASK_HI5Q_DESC_NUM_8822B) << BIT_SHIFT_HI5Q_DESC_NUM_8822B)
#define BITS_HI5Q_DESC_NUM_8822B                                               \
	(BIT_MASK_HI5Q_DESC_NUM_8822B << BIT_SHIFT_HI5Q_DESC_NUM_8822B)
#define BIT_CLEAR_HI5Q_DESC_NUM_8822B(x) ((x) & (~BITS_HI5Q_DESC_NUM_8822B))
#define BIT_GET_HI5Q_DESC_NUM_8822B(x)                                         \
	(((x) >> BIT_SHIFT_HI5Q_DESC_NUM_8822B) & BIT_MASK_HI5Q_DESC_NUM_8822B)
#define BIT_SET_HI5Q_DESC_NUM_8822B(x, v)                                      \
	(BIT_CLEAR_HI5Q_DESC_NUM_8822B(x) | BIT_HI5Q_DESC_NUM_8822B(v))

/* 2 REG_HI6Q_TXBD_NUM_8822B */
#define BIT_HI6Q_FLAG_8822B BIT(14)

#define BIT_SHIFT_HI6Q_DESC_MODE_8822B 12
#define BIT_MASK_HI6Q_DESC_MODE_8822B 0x3
#define BIT_HI6Q_DESC_MODE_8822B(x)                                            \
	(((x) & BIT_MASK_HI6Q_DESC_MODE_8822B)                                 \
	 << BIT_SHIFT_HI6Q_DESC_MODE_8822B)
#define BITS_HI6Q_DESC_MODE_8822B                                              \
	(BIT_MASK_HI6Q_DESC_MODE_8822B << BIT_SHIFT_HI6Q_DESC_MODE_8822B)
#define BIT_CLEAR_HI6Q_DESC_MODE_8822B(x) ((x) & (~BITS_HI6Q_DESC_MODE_8822B))
#define BIT_GET_HI6Q_DESC_MODE_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HI6Q_DESC_MODE_8822B) &                             \
	 BIT_MASK_HI6Q_DESC_MODE_8822B)
#define BIT_SET_HI6Q_DESC_MODE_8822B(x, v)                                     \
	(BIT_CLEAR_HI6Q_DESC_MODE_8822B(x) | BIT_HI6Q_DESC_MODE_8822B(v))

#define BIT_SHIFT_HI6Q_DESC_NUM_8822B 0
#define BIT_MASK_HI6Q_DESC_NUM_8822B 0xfff
#define BIT_HI6Q_DESC_NUM_8822B(x)                                             \
	(((x) & BIT_MASK_HI6Q_DESC_NUM_8822B) << BIT_SHIFT_HI6Q_DESC_NUM_8822B)
#define BITS_HI6Q_DESC_NUM_8822B                                               \
	(BIT_MASK_HI6Q_DESC_NUM_8822B << BIT_SHIFT_HI6Q_DESC_NUM_8822B)
#define BIT_CLEAR_HI6Q_DESC_NUM_8822B(x) ((x) & (~BITS_HI6Q_DESC_NUM_8822B))
#define BIT_GET_HI6Q_DESC_NUM_8822B(x)                                         \
	(((x) >> BIT_SHIFT_HI6Q_DESC_NUM_8822B) & BIT_MASK_HI6Q_DESC_NUM_8822B)
#define BIT_SET_HI6Q_DESC_NUM_8822B(x, v)                                      \
	(BIT_CLEAR_HI6Q_DESC_NUM_8822B(x) | BIT_HI6Q_DESC_NUM_8822B(v))

/* 2 REG_HI7Q_TXBD_NUM_8822B */
#define BIT_HI7Q_FLAG_8822B BIT(14)

#define BIT_SHIFT_HI7Q_DESC_MODE_8822B 12
#define BIT_MASK_HI7Q_DESC_MODE_8822B 0x3
#define BIT_HI7Q_DESC_MODE_8822B(x)                                            \
	(((x) & BIT_MASK_HI7Q_DESC_MODE_8822B)                                 \
	 << BIT_SHIFT_HI7Q_DESC_MODE_8822B)
#define BITS_HI7Q_DESC_MODE_8822B                                              \
	(BIT_MASK_HI7Q_DESC_MODE_8822B << BIT_SHIFT_HI7Q_DESC_MODE_8822B)
#define BIT_CLEAR_HI7Q_DESC_MODE_8822B(x) ((x) & (~BITS_HI7Q_DESC_MODE_8822B))
#define BIT_GET_HI7Q_DESC_MODE_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HI7Q_DESC_MODE_8822B) &                             \
	 BIT_MASK_HI7Q_DESC_MODE_8822B)
#define BIT_SET_HI7Q_DESC_MODE_8822B(x, v)                                     \
	(BIT_CLEAR_HI7Q_DESC_MODE_8822B(x) | BIT_HI7Q_DESC_MODE_8822B(v))

#define BIT_SHIFT_HI7Q_DESC_NUM_8822B 0
#define BIT_MASK_HI7Q_DESC_NUM_8822B 0xfff
#define BIT_HI7Q_DESC_NUM_8822B(x)                                             \
	(((x) & BIT_MASK_HI7Q_DESC_NUM_8822B) << BIT_SHIFT_HI7Q_DESC_NUM_8822B)
#define BITS_HI7Q_DESC_NUM_8822B                                               \
	(BIT_MASK_HI7Q_DESC_NUM_8822B << BIT_SHIFT_HI7Q_DESC_NUM_8822B)
#define BIT_CLEAR_HI7Q_DESC_NUM_8822B(x) ((x) & (~BITS_HI7Q_DESC_NUM_8822B))
#define BIT_GET_HI7Q_DESC_NUM_8822B(x)                                         \
	(((x) >> BIT_SHIFT_HI7Q_DESC_NUM_8822B) & BIT_MASK_HI7Q_DESC_NUM_8822B)
#define BIT_SET_HI7Q_DESC_NUM_8822B(x, v)                                      \
	(BIT_CLEAR_HI7Q_DESC_NUM_8822B(x) | BIT_HI7Q_DESC_NUM_8822B(v))

/* 2 REG_TSFTIMER_HCI_8822B */

#define BIT_SHIFT_TSFT2_HCI_8822B 16
#define BIT_MASK_TSFT2_HCI_8822B 0xffff
#define BIT_TSFT2_HCI_8822B(x)                                                 \
	(((x) & BIT_MASK_TSFT2_HCI_8822B) << BIT_SHIFT_TSFT2_HCI_8822B)
#define BITS_TSFT2_HCI_8822B                                                   \
	(BIT_MASK_TSFT2_HCI_8822B << BIT_SHIFT_TSFT2_HCI_8822B)
#define BIT_CLEAR_TSFT2_HCI_8822B(x) ((x) & (~BITS_TSFT2_HCI_8822B))
#define BIT_GET_TSFT2_HCI_8822B(x)                                             \
	(((x) >> BIT_SHIFT_TSFT2_HCI_8822B) & BIT_MASK_TSFT2_HCI_8822B)
#define BIT_SET_TSFT2_HCI_8822B(x, v)                                          \
	(BIT_CLEAR_TSFT2_HCI_8822B(x) | BIT_TSFT2_HCI_8822B(v))

#define BIT_SHIFT_TSFT1_HCI_8822B 0
#define BIT_MASK_TSFT1_HCI_8822B 0xffff
#define BIT_TSFT1_HCI_8822B(x)                                                 \
	(((x) & BIT_MASK_TSFT1_HCI_8822B) << BIT_SHIFT_TSFT1_HCI_8822B)
#define BITS_TSFT1_HCI_8822B                                                   \
	(BIT_MASK_TSFT1_HCI_8822B << BIT_SHIFT_TSFT1_HCI_8822B)
#define BIT_CLEAR_TSFT1_HCI_8822B(x) ((x) & (~BITS_TSFT1_HCI_8822B))
#define BIT_GET_TSFT1_HCI_8822B(x)                                             \
	(((x) >> BIT_SHIFT_TSFT1_HCI_8822B) & BIT_MASK_TSFT1_HCI_8822B)
#define BIT_SET_TSFT1_HCI_8822B(x, v)                                          \
	(BIT_CLEAR_TSFT1_HCI_8822B(x) | BIT_TSFT1_HCI_8822B(v))

/* 2 REG_BD_RWPTR_CLR_8822B */
#define BIT_CLR_HI7Q_HW_IDX_8822B BIT(29)
#define BIT_CLR_HI6Q_HW_IDX_8822B BIT(28)
#define BIT_CLR_HI5Q_HW_IDX_8822B BIT(27)
#define BIT_CLR_HI4Q_HW_IDX_8822B BIT(26)
#define BIT_CLR_HI3Q_HW_IDX_8822B BIT(25)
#define BIT_CLR_HI2Q_HW_IDX_8822B BIT(24)
#define BIT_CLR_HI1Q_HW_IDX_8822B BIT(23)
#define BIT_CLR_HI0Q_HW_IDX_8822B BIT(22)
#define BIT_CLR_BKQ_HW_IDX_8822B BIT(21)
#define BIT_CLR_BEQ_HW_IDX_8822B BIT(20)
#define BIT_CLR_VIQ_HW_IDX_8822B BIT(19)
#define BIT_CLR_VOQ_HW_IDX_8822B BIT(18)
#define BIT_CLR_MGQ_HW_IDX_8822B BIT(17)
#define BIT_CLR_RXQ_HW_IDX_8822B BIT(16)
#define BIT_CLR_HI7Q_HOST_IDX_8822B BIT(13)
#define BIT_CLR_HI6Q_HOST_IDX_8822B BIT(12)
#define BIT_CLR_HI5Q_HOST_IDX_8822B BIT(11)
#define BIT_CLR_HI4Q_HOST_IDX_8822B BIT(10)
#define BIT_CLR_HI3Q_HOST_IDX_8822B BIT(9)
#define BIT_CLR_HI2Q_HOST_IDX_8822B BIT(8)
#define BIT_CLR_HI1Q_HOST_IDX_8822B BIT(7)
#define BIT_CLR_HI0Q_HOST_IDX_8822B BIT(6)
#define BIT_CLR_BKQ_HOST_IDX_8822B BIT(5)
#define BIT_CLR_BEQ_HOST_IDX_8822B BIT(4)
#define BIT_CLR_VIQ_HOST_IDX_8822B BIT(3)
#define BIT_CLR_VOQ_HOST_IDX_8822B BIT(2)
#define BIT_CLR_MGQ_HOST_IDX_8822B BIT(1)
#define BIT_CLR_RXQ_HOST_IDX_8822B BIT(0)

/* 2 REG_VOQ_TXBD_IDX_8822B */

#define BIT_SHIFT_VOQ_HW_IDX_8822B 16
#define BIT_MASK_VOQ_HW_IDX_8822B 0xfff
#define BIT_VOQ_HW_IDX_8822B(x)                                                \
	(((x) & BIT_MASK_VOQ_HW_IDX_8822B) << BIT_SHIFT_VOQ_HW_IDX_8822B)
#define BITS_VOQ_HW_IDX_8822B                                                  \
	(BIT_MASK_VOQ_HW_IDX_8822B << BIT_SHIFT_VOQ_HW_IDX_8822B)
#define BIT_CLEAR_VOQ_HW_IDX_8822B(x) ((x) & (~BITS_VOQ_HW_IDX_8822B))
#define BIT_GET_VOQ_HW_IDX_8822B(x)                                            \
	(((x) >> BIT_SHIFT_VOQ_HW_IDX_8822B) & BIT_MASK_VOQ_HW_IDX_8822B)
#define BIT_SET_VOQ_HW_IDX_8822B(x, v)                                         \
	(BIT_CLEAR_VOQ_HW_IDX_8822B(x) | BIT_VOQ_HW_IDX_8822B(v))

#define BIT_SHIFT_VOQ_HOST_IDX_8822B 0
#define BIT_MASK_VOQ_HOST_IDX_8822B 0xfff
#define BIT_VOQ_HOST_IDX_8822B(x)                                              \
	(((x) & BIT_MASK_VOQ_HOST_IDX_8822B) << BIT_SHIFT_VOQ_HOST_IDX_8822B)
#define BITS_VOQ_HOST_IDX_8822B                                                \
	(BIT_MASK_VOQ_HOST_IDX_8822B << BIT_SHIFT_VOQ_HOST_IDX_8822B)
#define BIT_CLEAR_VOQ_HOST_IDX_8822B(x) ((x) & (~BITS_VOQ_HOST_IDX_8822B))
#define BIT_GET_VOQ_HOST_IDX_8822B(x)                                          \
	(((x) >> BIT_SHIFT_VOQ_HOST_IDX_8822B) & BIT_MASK_VOQ_HOST_IDX_8822B)
#define BIT_SET_VOQ_HOST_IDX_8822B(x, v)                                       \
	(BIT_CLEAR_VOQ_HOST_IDX_8822B(x) | BIT_VOQ_HOST_IDX_8822B(v))

/* 2 REG_VIQ_TXBD_IDX_8822B */

#define BIT_SHIFT_VIQ_HW_IDX_8822B 16
#define BIT_MASK_VIQ_HW_IDX_8822B 0xfff
#define BIT_VIQ_HW_IDX_8822B(x)                                                \
	(((x) & BIT_MASK_VIQ_HW_IDX_8822B) << BIT_SHIFT_VIQ_HW_IDX_8822B)
#define BITS_VIQ_HW_IDX_8822B                                                  \
	(BIT_MASK_VIQ_HW_IDX_8822B << BIT_SHIFT_VIQ_HW_IDX_8822B)
#define BIT_CLEAR_VIQ_HW_IDX_8822B(x) ((x) & (~BITS_VIQ_HW_IDX_8822B))
#define BIT_GET_VIQ_HW_IDX_8822B(x)                                            \
	(((x) >> BIT_SHIFT_VIQ_HW_IDX_8822B) & BIT_MASK_VIQ_HW_IDX_8822B)
#define BIT_SET_VIQ_HW_IDX_8822B(x, v)                                         \
	(BIT_CLEAR_VIQ_HW_IDX_8822B(x) | BIT_VIQ_HW_IDX_8822B(v))

#define BIT_SHIFT_VIQ_HOST_IDX_8822B 0
#define BIT_MASK_VIQ_HOST_IDX_8822B 0xfff
#define BIT_VIQ_HOST_IDX_8822B(x)                                              \
	(((x) & BIT_MASK_VIQ_HOST_IDX_8822B) << BIT_SHIFT_VIQ_HOST_IDX_8822B)
#define BITS_VIQ_HOST_IDX_8822B                                                \
	(BIT_MASK_VIQ_HOST_IDX_8822B << BIT_SHIFT_VIQ_HOST_IDX_8822B)
#define BIT_CLEAR_VIQ_HOST_IDX_8822B(x) ((x) & (~BITS_VIQ_HOST_IDX_8822B))
#define BIT_GET_VIQ_HOST_IDX_8822B(x)                                          \
	(((x) >> BIT_SHIFT_VIQ_HOST_IDX_8822B) & BIT_MASK_VIQ_HOST_IDX_8822B)
#define BIT_SET_VIQ_HOST_IDX_8822B(x, v)                                       \
	(BIT_CLEAR_VIQ_HOST_IDX_8822B(x) | BIT_VIQ_HOST_IDX_8822B(v))

/* 2 REG_BEQ_TXBD_IDX_8822B */

#define BIT_SHIFT_BEQ_HW_IDX_8822B 16
#define BIT_MASK_BEQ_HW_IDX_8822B 0xfff
#define BIT_BEQ_HW_IDX_8822B(x)                                                \
	(((x) & BIT_MASK_BEQ_HW_IDX_8822B) << BIT_SHIFT_BEQ_HW_IDX_8822B)
#define BITS_BEQ_HW_IDX_8822B                                                  \
	(BIT_MASK_BEQ_HW_IDX_8822B << BIT_SHIFT_BEQ_HW_IDX_8822B)
#define BIT_CLEAR_BEQ_HW_IDX_8822B(x) ((x) & (~BITS_BEQ_HW_IDX_8822B))
#define BIT_GET_BEQ_HW_IDX_8822B(x)                                            \
	(((x) >> BIT_SHIFT_BEQ_HW_IDX_8822B) & BIT_MASK_BEQ_HW_IDX_8822B)
#define BIT_SET_BEQ_HW_IDX_8822B(x, v)                                         \
	(BIT_CLEAR_BEQ_HW_IDX_8822B(x) | BIT_BEQ_HW_IDX_8822B(v))

#define BIT_SHIFT_BEQ_HOST_IDX_8822B 0
#define BIT_MASK_BEQ_HOST_IDX_8822B 0xfff
#define BIT_BEQ_HOST_IDX_8822B(x)                                              \
	(((x) & BIT_MASK_BEQ_HOST_IDX_8822B) << BIT_SHIFT_BEQ_HOST_IDX_8822B)
#define BITS_BEQ_HOST_IDX_8822B                                                \
	(BIT_MASK_BEQ_HOST_IDX_8822B << BIT_SHIFT_BEQ_HOST_IDX_8822B)
#define BIT_CLEAR_BEQ_HOST_IDX_8822B(x) ((x) & (~BITS_BEQ_HOST_IDX_8822B))
#define BIT_GET_BEQ_HOST_IDX_8822B(x)                                          \
	(((x) >> BIT_SHIFT_BEQ_HOST_IDX_8822B) & BIT_MASK_BEQ_HOST_IDX_8822B)
#define BIT_SET_BEQ_HOST_IDX_8822B(x, v)                                       \
	(BIT_CLEAR_BEQ_HOST_IDX_8822B(x) | BIT_BEQ_HOST_IDX_8822B(v))

/* 2 REG_BKQ_TXBD_IDX_8822B */

#define BIT_SHIFT_BKQ_HW_IDX_8822B 16
#define BIT_MASK_BKQ_HW_IDX_8822B 0xfff
#define BIT_BKQ_HW_IDX_8822B(x)                                                \
	(((x) & BIT_MASK_BKQ_HW_IDX_8822B) << BIT_SHIFT_BKQ_HW_IDX_8822B)
#define BITS_BKQ_HW_IDX_8822B                                                  \
	(BIT_MASK_BKQ_HW_IDX_8822B << BIT_SHIFT_BKQ_HW_IDX_8822B)
#define BIT_CLEAR_BKQ_HW_IDX_8822B(x) ((x) & (~BITS_BKQ_HW_IDX_8822B))
#define BIT_GET_BKQ_HW_IDX_8822B(x)                                            \
	(((x) >> BIT_SHIFT_BKQ_HW_IDX_8822B) & BIT_MASK_BKQ_HW_IDX_8822B)
#define BIT_SET_BKQ_HW_IDX_8822B(x, v)                                         \
	(BIT_CLEAR_BKQ_HW_IDX_8822B(x) | BIT_BKQ_HW_IDX_8822B(v))

#define BIT_SHIFT_BKQ_HOST_IDX_8822B 0
#define BIT_MASK_BKQ_HOST_IDX_8822B 0xfff
#define BIT_BKQ_HOST_IDX_8822B(x)                                              \
	(((x) & BIT_MASK_BKQ_HOST_IDX_8822B) << BIT_SHIFT_BKQ_HOST_IDX_8822B)
#define BITS_BKQ_HOST_IDX_8822B                                                \
	(BIT_MASK_BKQ_HOST_IDX_8822B << BIT_SHIFT_BKQ_HOST_IDX_8822B)
#define BIT_CLEAR_BKQ_HOST_IDX_8822B(x) ((x) & (~BITS_BKQ_HOST_IDX_8822B))
#define BIT_GET_BKQ_HOST_IDX_8822B(x)                                          \
	(((x) >> BIT_SHIFT_BKQ_HOST_IDX_8822B) & BIT_MASK_BKQ_HOST_IDX_8822B)
#define BIT_SET_BKQ_HOST_IDX_8822B(x, v)                                       \
	(BIT_CLEAR_BKQ_HOST_IDX_8822B(x) | BIT_BKQ_HOST_IDX_8822B(v))

/* 2 REG_MGQ_TXBD_IDX_8822B */

#define BIT_SHIFT_MGQ_HW_IDX_8822B 16
#define BIT_MASK_MGQ_HW_IDX_8822B 0xfff
#define BIT_MGQ_HW_IDX_8822B(x)                                                \
	(((x) & BIT_MASK_MGQ_HW_IDX_8822B) << BIT_SHIFT_MGQ_HW_IDX_8822B)
#define BITS_MGQ_HW_IDX_8822B                                                  \
	(BIT_MASK_MGQ_HW_IDX_8822B << BIT_SHIFT_MGQ_HW_IDX_8822B)
#define BIT_CLEAR_MGQ_HW_IDX_8822B(x) ((x) & (~BITS_MGQ_HW_IDX_8822B))
#define BIT_GET_MGQ_HW_IDX_8822B(x)                                            \
	(((x) >> BIT_SHIFT_MGQ_HW_IDX_8822B) & BIT_MASK_MGQ_HW_IDX_8822B)
#define BIT_SET_MGQ_HW_IDX_8822B(x, v)                                         \
	(BIT_CLEAR_MGQ_HW_IDX_8822B(x) | BIT_MGQ_HW_IDX_8822B(v))

#define BIT_SHIFT_MGQ_HOST_IDX_8822B 0
#define BIT_MASK_MGQ_HOST_IDX_8822B 0xfff
#define BIT_MGQ_HOST_IDX_8822B(x)                                              \
	(((x) & BIT_MASK_MGQ_HOST_IDX_8822B) << BIT_SHIFT_MGQ_HOST_IDX_8822B)
#define BITS_MGQ_HOST_IDX_8822B                                                \
	(BIT_MASK_MGQ_HOST_IDX_8822B << BIT_SHIFT_MGQ_HOST_IDX_8822B)
#define BIT_CLEAR_MGQ_HOST_IDX_8822B(x) ((x) & (~BITS_MGQ_HOST_IDX_8822B))
#define BIT_GET_MGQ_HOST_IDX_8822B(x)                                          \
	(((x) >> BIT_SHIFT_MGQ_HOST_IDX_8822B) & BIT_MASK_MGQ_HOST_IDX_8822B)
#define BIT_SET_MGQ_HOST_IDX_8822B(x, v)                                       \
	(BIT_CLEAR_MGQ_HOST_IDX_8822B(x) | BIT_MGQ_HOST_IDX_8822B(v))

/* 2 REG_RXQ_RXBD_IDX_8822B */

#define BIT_SHIFT_RXQ_HW_IDX_8822B 16
#define BIT_MASK_RXQ_HW_IDX_8822B 0xfff
#define BIT_RXQ_HW_IDX_8822B(x)                                                \
	(((x) & BIT_MASK_RXQ_HW_IDX_8822B) << BIT_SHIFT_RXQ_HW_IDX_8822B)
#define BITS_RXQ_HW_IDX_8822B                                                  \
	(BIT_MASK_RXQ_HW_IDX_8822B << BIT_SHIFT_RXQ_HW_IDX_8822B)
#define BIT_CLEAR_RXQ_HW_IDX_8822B(x) ((x) & (~BITS_RXQ_HW_IDX_8822B))
#define BIT_GET_RXQ_HW_IDX_8822B(x)                                            \
	(((x) >> BIT_SHIFT_RXQ_HW_IDX_8822B) & BIT_MASK_RXQ_HW_IDX_8822B)
#define BIT_SET_RXQ_HW_IDX_8822B(x, v)                                         \
	(BIT_CLEAR_RXQ_HW_IDX_8822B(x) | BIT_RXQ_HW_IDX_8822B(v))

#define BIT_SHIFT_RXQ_HOST_IDX_8822B 0
#define BIT_MASK_RXQ_HOST_IDX_8822B 0xfff
#define BIT_RXQ_HOST_IDX_8822B(x)                                              \
	(((x) & BIT_MASK_RXQ_HOST_IDX_8822B) << BIT_SHIFT_RXQ_HOST_IDX_8822B)
#define BITS_RXQ_HOST_IDX_8822B                                                \
	(BIT_MASK_RXQ_HOST_IDX_8822B << BIT_SHIFT_RXQ_HOST_IDX_8822B)
#define BIT_CLEAR_RXQ_HOST_IDX_8822B(x) ((x) & (~BITS_RXQ_HOST_IDX_8822B))
#define BIT_GET_RXQ_HOST_IDX_8822B(x)                                          \
	(((x) >> BIT_SHIFT_RXQ_HOST_IDX_8822B) & BIT_MASK_RXQ_HOST_IDX_8822B)
#define BIT_SET_RXQ_HOST_IDX_8822B(x, v)                                       \
	(BIT_CLEAR_RXQ_HOST_IDX_8822B(x) | BIT_RXQ_HOST_IDX_8822B(v))

/* 2 REG_HI0Q_TXBD_IDX_8822B */

#define BIT_SHIFT_HI0Q_HW_IDX_8822B 16
#define BIT_MASK_HI0Q_HW_IDX_8822B 0xfff
#define BIT_HI0Q_HW_IDX_8822B(x)                                               \
	(((x) & BIT_MASK_HI0Q_HW_IDX_8822B) << BIT_SHIFT_HI0Q_HW_IDX_8822B)
#define BITS_HI0Q_HW_IDX_8822B                                                 \
	(BIT_MASK_HI0Q_HW_IDX_8822B << BIT_SHIFT_HI0Q_HW_IDX_8822B)
#define BIT_CLEAR_HI0Q_HW_IDX_8822B(x) ((x) & (~BITS_HI0Q_HW_IDX_8822B))
#define BIT_GET_HI0Q_HW_IDX_8822B(x)                                           \
	(((x) >> BIT_SHIFT_HI0Q_HW_IDX_8822B) & BIT_MASK_HI0Q_HW_IDX_8822B)
#define BIT_SET_HI0Q_HW_IDX_8822B(x, v)                                        \
	(BIT_CLEAR_HI0Q_HW_IDX_8822B(x) | BIT_HI0Q_HW_IDX_8822B(v))

#define BIT_SHIFT_HI0Q_HOST_IDX_8822B 0
#define BIT_MASK_HI0Q_HOST_IDX_8822B 0xfff
#define BIT_HI0Q_HOST_IDX_8822B(x)                                             \
	(((x) & BIT_MASK_HI0Q_HOST_IDX_8822B) << BIT_SHIFT_HI0Q_HOST_IDX_8822B)
#define BITS_HI0Q_HOST_IDX_8822B                                               \
	(BIT_MASK_HI0Q_HOST_IDX_8822B << BIT_SHIFT_HI0Q_HOST_IDX_8822B)
#define BIT_CLEAR_HI0Q_HOST_IDX_8822B(x) ((x) & (~BITS_HI0Q_HOST_IDX_8822B))
#define BIT_GET_HI0Q_HOST_IDX_8822B(x)                                         \
	(((x) >> BIT_SHIFT_HI0Q_HOST_IDX_8822B) & BIT_MASK_HI0Q_HOST_IDX_8822B)
#define BIT_SET_HI0Q_HOST_IDX_8822B(x, v)                                      \
	(BIT_CLEAR_HI0Q_HOST_IDX_8822B(x) | BIT_HI0Q_HOST_IDX_8822B(v))

/* 2 REG_HI1Q_TXBD_IDX_8822B */

#define BIT_SHIFT_HI1Q_HW_IDX_8822B 16
#define BIT_MASK_HI1Q_HW_IDX_8822B 0xfff
#define BIT_HI1Q_HW_IDX_8822B(x)                                               \
	(((x) & BIT_MASK_HI1Q_HW_IDX_8822B) << BIT_SHIFT_HI1Q_HW_IDX_8822B)
#define BITS_HI1Q_HW_IDX_8822B                                                 \
	(BIT_MASK_HI1Q_HW_IDX_8822B << BIT_SHIFT_HI1Q_HW_IDX_8822B)
#define BIT_CLEAR_HI1Q_HW_IDX_8822B(x) ((x) & (~BITS_HI1Q_HW_IDX_8822B))
#define BIT_GET_HI1Q_HW_IDX_8822B(x)                                           \
	(((x) >> BIT_SHIFT_HI1Q_HW_IDX_8822B) & BIT_MASK_HI1Q_HW_IDX_8822B)
#define BIT_SET_HI1Q_HW_IDX_8822B(x, v)                                        \
	(BIT_CLEAR_HI1Q_HW_IDX_8822B(x) | BIT_HI1Q_HW_IDX_8822B(v))

#define BIT_SHIFT_HI1Q_HOST_IDX_8822B 0
#define BIT_MASK_HI1Q_HOST_IDX_8822B 0xfff
#define BIT_HI1Q_HOST_IDX_8822B(x)                                             \
	(((x) & BIT_MASK_HI1Q_HOST_IDX_8822B) << BIT_SHIFT_HI1Q_HOST_IDX_8822B)
#define BITS_HI1Q_HOST_IDX_8822B                                               \
	(BIT_MASK_HI1Q_HOST_IDX_8822B << BIT_SHIFT_HI1Q_HOST_IDX_8822B)
#define BIT_CLEAR_HI1Q_HOST_IDX_8822B(x) ((x) & (~BITS_HI1Q_HOST_IDX_8822B))
#define BIT_GET_HI1Q_HOST_IDX_8822B(x)                                         \
	(((x) >> BIT_SHIFT_HI1Q_HOST_IDX_8822B) & BIT_MASK_HI1Q_HOST_IDX_8822B)
#define BIT_SET_HI1Q_HOST_IDX_8822B(x, v)                                      \
	(BIT_CLEAR_HI1Q_HOST_IDX_8822B(x) | BIT_HI1Q_HOST_IDX_8822B(v))

/* 2 REG_HI2Q_TXBD_IDX_8822B */

#define BIT_SHIFT_HI2Q_HW_IDX_8822B 16
#define BIT_MASK_HI2Q_HW_IDX_8822B 0xfff
#define BIT_HI2Q_HW_IDX_8822B(x)                                               \
	(((x) & BIT_MASK_HI2Q_HW_IDX_8822B) << BIT_SHIFT_HI2Q_HW_IDX_8822B)
#define BITS_HI2Q_HW_IDX_8822B                                                 \
	(BIT_MASK_HI2Q_HW_IDX_8822B << BIT_SHIFT_HI2Q_HW_IDX_8822B)
#define BIT_CLEAR_HI2Q_HW_IDX_8822B(x) ((x) & (~BITS_HI2Q_HW_IDX_8822B))
#define BIT_GET_HI2Q_HW_IDX_8822B(x)                                           \
	(((x) >> BIT_SHIFT_HI2Q_HW_IDX_8822B) & BIT_MASK_HI2Q_HW_IDX_8822B)
#define BIT_SET_HI2Q_HW_IDX_8822B(x, v)                                        \
	(BIT_CLEAR_HI2Q_HW_IDX_8822B(x) | BIT_HI2Q_HW_IDX_8822B(v))

#define BIT_SHIFT_HI2Q_HOST_IDX_8822B 0
#define BIT_MASK_HI2Q_HOST_IDX_8822B 0xfff
#define BIT_HI2Q_HOST_IDX_8822B(x)                                             \
	(((x) & BIT_MASK_HI2Q_HOST_IDX_8822B) << BIT_SHIFT_HI2Q_HOST_IDX_8822B)
#define BITS_HI2Q_HOST_IDX_8822B                                               \
	(BIT_MASK_HI2Q_HOST_IDX_8822B << BIT_SHIFT_HI2Q_HOST_IDX_8822B)
#define BIT_CLEAR_HI2Q_HOST_IDX_8822B(x) ((x) & (~BITS_HI2Q_HOST_IDX_8822B))
#define BIT_GET_HI2Q_HOST_IDX_8822B(x)                                         \
	(((x) >> BIT_SHIFT_HI2Q_HOST_IDX_8822B) & BIT_MASK_HI2Q_HOST_IDX_8822B)
#define BIT_SET_HI2Q_HOST_IDX_8822B(x, v)                                      \
	(BIT_CLEAR_HI2Q_HOST_IDX_8822B(x) | BIT_HI2Q_HOST_IDX_8822B(v))

/* 2 REG_HI3Q_TXBD_IDX_8822B */

#define BIT_SHIFT_HI3Q_HW_IDX_8822B 16
#define BIT_MASK_HI3Q_HW_IDX_8822B 0xfff
#define BIT_HI3Q_HW_IDX_8822B(x)                                               \
	(((x) & BIT_MASK_HI3Q_HW_IDX_8822B) << BIT_SHIFT_HI3Q_HW_IDX_8822B)
#define BITS_HI3Q_HW_IDX_8822B                                                 \
	(BIT_MASK_HI3Q_HW_IDX_8822B << BIT_SHIFT_HI3Q_HW_IDX_8822B)
#define BIT_CLEAR_HI3Q_HW_IDX_8822B(x) ((x) & (~BITS_HI3Q_HW_IDX_8822B))
#define BIT_GET_HI3Q_HW_IDX_8822B(x)                                           \
	(((x) >> BIT_SHIFT_HI3Q_HW_IDX_8822B) & BIT_MASK_HI3Q_HW_IDX_8822B)
#define BIT_SET_HI3Q_HW_IDX_8822B(x, v)                                        \
	(BIT_CLEAR_HI3Q_HW_IDX_8822B(x) | BIT_HI3Q_HW_IDX_8822B(v))

#define BIT_SHIFT_HI3Q_HOST_IDX_8822B 0
#define BIT_MASK_HI3Q_HOST_IDX_8822B 0xfff
#define BIT_HI3Q_HOST_IDX_8822B(x)                                             \
	(((x) & BIT_MASK_HI3Q_HOST_IDX_8822B) << BIT_SHIFT_HI3Q_HOST_IDX_8822B)
#define BITS_HI3Q_HOST_IDX_8822B                                               \
	(BIT_MASK_HI3Q_HOST_IDX_8822B << BIT_SHIFT_HI3Q_HOST_IDX_8822B)
#define BIT_CLEAR_HI3Q_HOST_IDX_8822B(x) ((x) & (~BITS_HI3Q_HOST_IDX_8822B))
#define BIT_GET_HI3Q_HOST_IDX_8822B(x)                                         \
	(((x) >> BIT_SHIFT_HI3Q_HOST_IDX_8822B) & BIT_MASK_HI3Q_HOST_IDX_8822B)
#define BIT_SET_HI3Q_HOST_IDX_8822B(x, v)                                      \
	(BIT_CLEAR_HI3Q_HOST_IDX_8822B(x) | BIT_HI3Q_HOST_IDX_8822B(v))

/* 2 REG_HI4Q_TXBD_IDX_8822B */

#define BIT_SHIFT_HI4Q_HW_IDX_8822B 16
#define BIT_MASK_HI4Q_HW_IDX_8822B 0xfff
#define BIT_HI4Q_HW_IDX_8822B(x)                                               \
	(((x) & BIT_MASK_HI4Q_HW_IDX_8822B) << BIT_SHIFT_HI4Q_HW_IDX_8822B)
#define BITS_HI4Q_HW_IDX_8822B                                                 \
	(BIT_MASK_HI4Q_HW_IDX_8822B << BIT_SHIFT_HI4Q_HW_IDX_8822B)
#define BIT_CLEAR_HI4Q_HW_IDX_8822B(x) ((x) & (~BITS_HI4Q_HW_IDX_8822B))
#define BIT_GET_HI4Q_HW_IDX_8822B(x)                                           \
	(((x) >> BIT_SHIFT_HI4Q_HW_IDX_8822B) & BIT_MASK_HI4Q_HW_IDX_8822B)
#define BIT_SET_HI4Q_HW_IDX_8822B(x, v)                                        \
	(BIT_CLEAR_HI4Q_HW_IDX_8822B(x) | BIT_HI4Q_HW_IDX_8822B(v))

#define BIT_SHIFT_HI4Q_HOST_IDX_8822B 0
#define BIT_MASK_HI4Q_HOST_IDX_8822B 0xfff
#define BIT_HI4Q_HOST_IDX_8822B(x)                                             \
	(((x) & BIT_MASK_HI4Q_HOST_IDX_8822B) << BIT_SHIFT_HI4Q_HOST_IDX_8822B)
#define BITS_HI4Q_HOST_IDX_8822B                                               \
	(BIT_MASK_HI4Q_HOST_IDX_8822B << BIT_SHIFT_HI4Q_HOST_IDX_8822B)
#define BIT_CLEAR_HI4Q_HOST_IDX_8822B(x) ((x) & (~BITS_HI4Q_HOST_IDX_8822B))
#define BIT_GET_HI4Q_HOST_IDX_8822B(x)                                         \
	(((x) >> BIT_SHIFT_HI4Q_HOST_IDX_8822B) & BIT_MASK_HI4Q_HOST_IDX_8822B)
#define BIT_SET_HI4Q_HOST_IDX_8822B(x, v)                                      \
	(BIT_CLEAR_HI4Q_HOST_IDX_8822B(x) | BIT_HI4Q_HOST_IDX_8822B(v))

/* 2 REG_HI5Q_TXBD_IDX_8822B */

#define BIT_SHIFT_HI5Q_HW_IDX_8822B 16
#define BIT_MASK_HI5Q_HW_IDX_8822B 0xfff
#define BIT_HI5Q_HW_IDX_8822B(x)                                               \
	(((x) & BIT_MASK_HI5Q_HW_IDX_8822B) << BIT_SHIFT_HI5Q_HW_IDX_8822B)
#define BITS_HI5Q_HW_IDX_8822B                                                 \
	(BIT_MASK_HI5Q_HW_IDX_8822B << BIT_SHIFT_HI5Q_HW_IDX_8822B)
#define BIT_CLEAR_HI5Q_HW_IDX_8822B(x) ((x) & (~BITS_HI5Q_HW_IDX_8822B))
#define BIT_GET_HI5Q_HW_IDX_8822B(x)                                           \
	(((x) >> BIT_SHIFT_HI5Q_HW_IDX_8822B) & BIT_MASK_HI5Q_HW_IDX_8822B)
#define BIT_SET_HI5Q_HW_IDX_8822B(x, v)                                        \
	(BIT_CLEAR_HI5Q_HW_IDX_8822B(x) | BIT_HI5Q_HW_IDX_8822B(v))

#define BIT_SHIFT_HI5Q_HOST_IDX_8822B 0
#define BIT_MASK_HI5Q_HOST_IDX_8822B 0xfff
#define BIT_HI5Q_HOST_IDX_8822B(x)                                             \
	(((x) & BIT_MASK_HI5Q_HOST_IDX_8822B) << BIT_SHIFT_HI5Q_HOST_IDX_8822B)
#define BITS_HI5Q_HOST_IDX_8822B                                               \
	(BIT_MASK_HI5Q_HOST_IDX_8822B << BIT_SHIFT_HI5Q_HOST_IDX_8822B)
#define BIT_CLEAR_HI5Q_HOST_IDX_8822B(x) ((x) & (~BITS_HI5Q_HOST_IDX_8822B))
#define BIT_GET_HI5Q_HOST_IDX_8822B(x)                                         \
	(((x) >> BIT_SHIFT_HI5Q_HOST_IDX_8822B) & BIT_MASK_HI5Q_HOST_IDX_8822B)
#define BIT_SET_HI5Q_HOST_IDX_8822B(x, v)                                      \
	(BIT_CLEAR_HI5Q_HOST_IDX_8822B(x) | BIT_HI5Q_HOST_IDX_8822B(v))

/* 2 REG_HI6Q_TXBD_IDX_8822B */

#define BIT_SHIFT_HI6Q_HW_IDX_8822B 16
#define BIT_MASK_HI6Q_HW_IDX_8822B 0xfff
#define BIT_HI6Q_HW_IDX_8822B(x)                                               \
	(((x) & BIT_MASK_HI6Q_HW_IDX_8822B) << BIT_SHIFT_HI6Q_HW_IDX_8822B)
#define BITS_HI6Q_HW_IDX_8822B                                                 \
	(BIT_MASK_HI6Q_HW_IDX_8822B << BIT_SHIFT_HI6Q_HW_IDX_8822B)
#define BIT_CLEAR_HI6Q_HW_IDX_8822B(x) ((x) & (~BITS_HI6Q_HW_IDX_8822B))
#define BIT_GET_HI6Q_HW_IDX_8822B(x)                                           \
	(((x) >> BIT_SHIFT_HI6Q_HW_IDX_8822B) & BIT_MASK_HI6Q_HW_IDX_8822B)
#define BIT_SET_HI6Q_HW_IDX_8822B(x, v)                                        \
	(BIT_CLEAR_HI6Q_HW_IDX_8822B(x) | BIT_HI6Q_HW_IDX_8822B(v))

#define BIT_SHIFT_HI6Q_HOST_IDX_8822B 0
#define BIT_MASK_HI6Q_HOST_IDX_8822B 0xfff
#define BIT_HI6Q_HOST_IDX_8822B(x)                                             \
	(((x) & BIT_MASK_HI6Q_HOST_IDX_8822B) << BIT_SHIFT_HI6Q_HOST_IDX_8822B)
#define BITS_HI6Q_HOST_IDX_8822B                                               \
	(BIT_MASK_HI6Q_HOST_IDX_8822B << BIT_SHIFT_HI6Q_HOST_IDX_8822B)
#define BIT_CLEAR_HI6Q_HOST_IDX_8822B(x) ((x) & (~BITS_HI6Q_HOST_IDX_8822B))
#define BIT_GET_HI6Q_HOST_IDX_8822B(x)                                         \
	(((x) >> BIT_SHIFT_HI6Q_HOST_IDX_8822B) & BIT_MASK_HI6Q_HOST_IDX_8822B)
#define BIT_SET_HI6Q_HOST_IDX_8822B(x, v)                                      \
	(BIT_CLEAR_HI6Q_HOST_IDX_8822B(x) | BIT_HI6Q_HOST_IDX_8822B(v))

/* 2 REG_HI7Q_TXBD_IDX_8822B */

#define BIT_SHIFT_HI7Q_HW_IDX_8822B 16
#define BIT_MASK_HI7Q_HW_IDX_8822B 0xfff
#define BIT_HI7Q_HW_IDX_8822B(x)                                               \
	(((x) & BIT_MASK_HI7Q_HW_IDX_8822B) << BIT_SHIFT_HI7Q_HW_IDX_8822B)
#define BITS_HI7Q_HW_IDX_8822B                                                 \
	(BIT_MASK_HI7Q_HW_IDX_8822B << BIT_SHIFT_HI7Q_HW_IDX_8822B)
#define BIT_CLEAR_HI7Q_HW_IDX_8822B(x) ((x) & (~BITS_HI7Q_HW_IDX_8822B))
#define BIT_GET_HI7Q_HW_IDX_8822B(x)                                           \
	(((x) >> BIT_SHIFT_HI7Q_HW_IDX_8822B) & BIT_MASK_HI7Q_HW_IDX_8822B)
#define BIT_SET_HI7Q_HW_IDX_8822B(x, v)                                        \
	(BIT_CLEAR_HI7Q_HW_IDX_8822B(x) | BIT_HI7Q_HW_IDX_8822B(v))

#define BIT_SHIFT_HI7Q_HOST_IDX_8822B 0
#define BIT_MASK_HI7Q_HOST_IDX_8822B 0xfff
#define BIT_HI7Q_HOST_IDX_8822B(x)                                             \
	(((x) & BIT_MASK_HI7Q_HOST_IDX_8822B) << BIT_SHIFT_HI7Q_HOST_IDX_8822B)
#define BITS_HI7Q_HOST_IDX_8822B                                               \
	(BIT_MASK_HI7Q_HOST_IDX_8822B << BIT_SHIFT_HI7Q_HOST_IDX_8822B)
#define BIT_CLEAR_HI7Q_HOST_IDX_8822B(x) ((x) & (~BITS_HI7Q_HOST_IDX_8822B))
#define BIT_GET_HI7Q_HOST_IDX_8822B(x)                                         \
	(((x) >> BIT_SHIFT_HI7Q_HOST_IDX_8822B) & BIT_MASK_HI7Q_HOST_IDX_8822B)
#define BIT_SET_HI7Q_HOST_IDX_8822B(x, v)                                      \
	(BIT_CLEAR_HI7Q_HOST_IDX_8822B(x) | BIT_HI7Q_HOST_IDX_8822B(v))

/* 2 REG_DBG_SEL_V1_8822B */

#define BIT_SHIFT_DBG_SEL_8822B 0
#define BIT_MASK_DBG_SEL_8822B 0xff
#define BIT_DBG_SEL_8822B(x)                                                   \
	(((x) & BIT_MASK_DBG_SEL_8822B) << BIT_SHIFT_DBG_SEL_8822B)
#define BITS_DBG_SEL_8822B (BIT_MASK_DBG_SEL_8822B << BIT_SHIFT_DBG_SEL_8822B)
#define BIT_CLEAR_DBG_SEL_8822B(x) ((x) & (~BITS_DBG_SEL_8822B))
#define BIT_GET_DBG_SEL_8822B(x)                                               \
	(((x) >> BIT_SHIFT_DBG_SEL_8822B) & BIT_MASK_DBG_SEL_8822B)
#define BIT_SET_DBG_SEL_8822B(x, v)                                            \
	(BIT_CLEAR_DBG_SEL_8822B(x) | BIT_DBG_SEL_8822B(v))

/* 2 REG_PCIE_HRPWM1_V1_8822B */

#define BIT_SHIFT_PCIE_HRPWM_8822B 0
#define BIT_MASK_PCIE_HRPWM_8822B 0xff
#define BIT_PCIE_HRPWM_8822B(x)                                                \
	(((x) & BIT_MASK_PCIE_HRPWM_8822B) << BIT_SHIFT_PCIE_HRPWM_8822B)
#define BITS_PCIE_HRPWM_8822B                                                  \
	(BIT_MASK_PCIE_HRPWM_8822B << BIT_SHIFT_PCIE_HRPWM_8822B)
#define BIT_CLEAR_PCIE_HRPWM_8822B(x) ((x) & (~BITS_PCIE_HRPWM_8822B))
#define BIT_GET_PCIE_HRPWM_8822B(x)                                            \
	(((x) >> BIT_SHIFT_PCIE_HRPWM_8822B) & BIT_MASK_PCIE_HRPWM_8822B)
#define BIT_SET_PCIE_HRPWM_8822B(x, v)                                         \
	(BIT_CLEAR_PCIE_HRPWM_8822B(x) | BIT_PCIE_HRPWM_8822B(v))

/* 2 REG_PCIE_HCPWM1_V1_8822B */

#define BIT_SHIFT_PCIE_HCPWM_8822B 0
#define BIT_MASK_PCIE_HCPWM_8822B 0xff
#define BIT_PCIE_HCPWM_8822B(x)                                                \
	(((x) & BIT_MASK_PCIE_HCPWM_8822B) << BIT_SHIFT_PCIE_HCPWM_8822B)
#define BITS_PCIE_HCPWM_8822B                                                  \
	(BIT_MASK_PCIE_HCPWM_8822B << BIT_SHIFT_PCIE_HCPWM_8822B)
#define BIT_CLEAR_PCIE_HCPWM_8822B(x) ((x) & (~BITS_PCIE_HCPWM_8822B))
#define BIT_GET_PCIE_HCPWM_8822B(x)                                            \
	(((x) >> BIT_SHIFT_PCIE_HCPWM_8822B) & BIT_MASK_PCIE_HCPWM_8822B)
#define BIT_SET_PCIE_HCPWM_8822B(x, v)                                         \
	(BIT_CLEAR_PCIE_HCPWM_8822B(x) | BIT_PCIE_HCPWM_8822B(v))

/* 2 REG_PCIE_CTRL2_8822B */
#define BIT_DIS_TXDMA_PRE_8822B BIT(7)
#define BIT_DIS_RXDMA_PRE_8822B BIT(6)

#define BIT_SHIFT_HPS_CLKR_PCIE_8822B 4
#define BIT_MASK_HPS_CLKR_PCIE_8822B 0x3
#define BIT_HPS_CLKR_PCIE_8822B(x)                                             \
	(((x) & BIT_MASK_HPS_CLKR_PCIE_8822B) << BIT_SHIFT_HPS_CLKR_PCIE_8822B)
#define BITS_HPS_CLKR_PCIE_8822B                                               \
	(BIT_MASK_HPS_CLKR_PCIE_8822B << BIT_SHIFT_HPS_CLKR_PCIE_8822B)
#define BIT_CLEAR_HPS_CLKR_PCIE_8822B(x) ((x) & (~BITS_HPS_CLKR_PCIE_8822B))
#define BIT_GET_HPS_CLKR_PCIE_8822B(x)                                         \
	(((x) >> BIT_SHIFT_HPS_CLKR_PCIE_8822B) & BIT_MASK_HPS_CLKR_PCIE_8822B)
#define BIT_SET_HPS_CLKR_PCIE_8822B(x, v)                                      \
	(BIT_CLEAR_HPS_CLKR_PCIE_8822B(x) | BIT_HPS_CLKR_PCIE_8822B(v))

#define BIT_PCIE_INT_8822B BIT(3)
#define BIT_TXFLAG_EXIT_L1_EN_8822B BIT(2)
#define BIT_EN_RXDMA_ALIGN_8822B BIT(1)
#define BIT_EN_TXDMA_ALIGN_8822B BIT(0)

/* 2 REG_PCIE_HRPWM2_V1_8822B */

#define BIT_SHIFT_PCIE_HRPWM2_8822B 0
#define BIT_MASK_PCIE_HRPWM2_8822B 0xffff
#define BIT_PCIE_HRPWM2_8822B(x)                                               \
	(((x) & BIT_MASK_PCIE_HRPWM2_8822B) << BIT_SHIFT_PCIE_HRPWM2_8822B)
#define BITS_PCIE_HRPWM2_8822B                                                 \
	(BIT_MASK_PCIE_HRPWM2_8822B << BIT_SHIFT_PCIE_HRPWM2_8822B)
#define BIT_CLEAR_PCIE_HRPWM2_8822B(x) ((x) & (~BITS_PCIE_HRPWM2_8822B))
#define BIT_GET_PCIE_HRPWM2_8822B(x)                                           \
	(((x) >> BIT_SHIFT_PCIE_HRPWM2_8822B) & BIT_MASK_PCIE_HRPWM2_8822B)
#define BIT_SET_PCIE_HRPWM2_8822B(x, v)                                        \
	(BIT_CLEAR_PCIE_HRPWM2_8822B(x) | BIT_PCIE_HRPWM2_8822B(v))

/* 2 REG_PCIE_HCPWM2_V1_8822B */

#define BIT_SHIFT_PCIE_HCPWM2_8822B 0
#define BIT_MASK_PCIE_HCPWM2_8822B 0xffff
#define BIT_PCIE_HCPWM2_8822B(x)                                               \
	(((x) & BIT_MASK_PCIE_HCPWM2_8822B) << BIT_SHIFT_PCIE_HCPWM2_8822B)
#define BITS_PCIE_HCPWM2_8822B                                                 \
	(BIT_MASK_PCIE_HCPWM2_8822B << BIT_SHIFT_PCIE_HCPWM2_8822B)
#define BIT_CLEAR_PCIE_HCPWM2_8822B(x) ((x) & (~BITS_PCIE_HCPWM2_8822B))
#define BIT_GET_PCIE_HCPWM2_8822B(x)                                           \
	(((x) >> BIT_SHIFT_PCIE_HCPWM2_8822B) & BIT_MASK_PCIE_HCPWM2_8822B)
#define BIT_SET_PCIE_HCPWM2_8822B(x, v)                                        \
	(BIT_CLEAR_PCIE_HCPWM2_8822B(x) | BIT_PCIE_HCPWM2_8822B(v))

/* 2 REG_PCIE_H2C_MSG_V1_8822B */

#define BIT_SHIFT_DRV2FW_INFO_8822B 0
#define BIT_MASK_DRV2FW_INFO_8822B 0xffffffffL
#define BIT_DRV2FW_INFO_8822B(x)                                               \
	(((x) & BIT_MASK_DRV2FW_INFO_8822B) << BIT_SHIFT_DRV2FW_INFO_8822B)
#define BITS_DRV2FW_INFO_8822B                                                 \
	(BIT_MASK_DRV2FW_INFO_8822B << BIT_SHIFT_DRV2FW_INFO_8822B)
#define BIT_CLEAR_DRV2FW_INFO_8822B(x) ((x) & (~BITS_DRV2FW_INFO_8822B))
#define BIT_GET_DRV2FW_INFO_8822B(x)                                           \
	(((x) >> BIT_SHIFT_DRV2FW_INFO_8822B) & BIT_MASK_DRV2FW_INFO_8822B)
#define BIT_SET_DRV2FW_INFO_8822B(x, v)                                        \
	(BIT_CLEAR_DRV2FW_INFO_8822B(x) | BIT_DRV2FW_INFO_8822B(v))

/* 2 REG_PCIE_C2H_MSG_V1_8822B */

#define BIT_SHIFT_HCI_PCIE_C2H_MSG_8822B 0
#define BIT_MASK_HCI_PCIE_C2H_MSG_8822B 0xffffffffL
#define BIT_HCI_PCIE_C2H_MSG_8822B(x)                                          \
	(((x) & BIT_MASK_HCI_PCIE_C2H_MSG_8822B)                               \
	 << BIT_SHIFT_HCI_PCIE_C2H_MSG_8822B)
#define BITS_HCI_PCIE_C2H_MSG_8822B                                            \
	(BIT_MASK_HCI_PCIE_C2H_MSG_8822B << BIT_SHIFT_HCI_PCIE_C2H_MSG_8822B)
#define BIT_CLEAR_HCI_PCIE_C2H_MSG_8822B(x)                                    \
	((x) & (~BITS_HCI_PCIE_C2H_MSG_8822B))
#define BIT_GET_HCI_PCIE_C2H_MSG_8822B(x)                                      \
	(((x) >> BIT_SHIFT_HCI_PCIE_C2H_MSG_8822B) &                           \
	 BIT_MASK_HCI_PCIE_C2H_MSG_8822B)
#define BIT_SET_HCI_PCIE_C2H_MSG_8822B(x, v)                                   \
	(BIT_CLEAR_HCI_PCIE_C2H_MSG_8822B(x) | BIT_HCI_PCIE_C2H_MSG_8822B(v))

/* 2 REG_DBI_WDATA_V1_8822B */

#define BIT_SHIFT_DBI_WDATA_8822B 0
#define BIT_MASK_DBI_WDATA_8822B 0xffffffffL
#define BIT_DBI_WDATA_8822B(x)                                                 \
	(((x) & BIT_MASK_DBI_WDATA_8822B) << BIT_SHIFT_DBI_WDATA_8822B)
#define BITS_DBI_WDATA_8822B                                                   \
	(BIT_MASK_DBI_WDATA_8822B << BIT_SHIFT_DBI_WDATA_8822B)
#define BIT_CLEAR_DBI_WDATA_8822B(x) ((x) & (~BITS_DBI_WDATA_8822B))
#define BIT_GET_DBI_WDATA_8822B(x)                                             \
	(((x) >> BIT_SHIFT_DBI_WDATA_8822B) & BIT_MASK_DBI_WDATA_8822B)
#define BIT_SET_DBI_WDATA_8822B(x, v)                                          \
	(BIT_CLEAR_DBI_WDATA_8822B(x) | BIT_DBI_WDATA_8822B(v))

/* 2 REG_DBI_RDATA_V1_8822B */

#define BIT_SHIFT_DBI_RDATA_8822B 0
#define BIT_MASK_DBI_RDATA_8822B 0xffffffffL
#define BIT_DBI_RDATA_8822B(x)                                                 \
	(((x) & BIT_MASK_DBI_RDATA_8822B) << BIT_SHIFT_DBI_RDATA_8822B)
#define BITS_DBI_RDATA_8822B                                                   \
	(BIT_MASK_DBI_RDATA_8822B << BIT_SHIFT_DBI_RDATA_8822B)
#define BIT_CLEAR_DBI_RDATA_8822B(x) ((x) & (~BITS_DBI_RDATA_8822B))
#define BIT_GET_DBI_RDATA_8822B(x)                                             \
	(((x) >> BIT_SHIFT_DBI_RDATA_8822B) & BIT_MASK_DBI_RDATA_8822B)
#define BIT_SET_DBI_RDATA_8822B(x, v)                                          \
	(BIT_CLEAR_DBI_RDATA_8822B(x) | BIT_DBI_RDATA_8822B(v))

/* 2 REG_DBI_FLAG_V1_8822B */
#define BIT_EN_STUCK_DBG_8822B BIT(26)
#define BIT_RX_STUCK_8822B BIT(25)
#define BIT_TX_STUCK_8822B BIT(24)
#define BIT_DBI_RFLAG_8822B BIT(17)
#define BIT_DBI_WFLAG_8822B BIT(16)

#define BIT_SHIFT_DBI_WREN_8822B 12
#define BIT_MASK_DBI_WREN_8822B 0xf
#define BIT_DBI_WREN_8822B(x)                                                  \
	(((x) & BIT_MASK_DBI_WREN_8822B) << BIT_SHIFT_DBI_WREN_8822B)
#define BITS_DBI_WREN_8822B                                                    \
	(BIT_MASK_DBI_WREN_8822B << BIT_SHIFT_DBI_WREN_8822B)
#define BIT_CLEAR_DBI_WREN_8822B(x) ((x) & (~BITS_DBI_WREN_8822B))
#define BIT_GET_DBI_WREN_8822B(x)                                              \
	(((x) >> BIT_SHIFT_DBI_WREN_8822B) & BIT_MASK_DBI_WREN_8822B)
#define BIT_SET_DBI_WREN_8822B(x, v)                                           \
	(BIT_CLEAR_DBI_WREN_8822B(x) | BIT_DBI_WREN_8822B(v))

#define BIT_SHIFT_DBI_ADDR_8822B 0
#define BIT_MASK_DBI_ADDR_8822B 0xfff
#define BIT_DBI_ADDR_8822B(x)                                                  \
	(((x) & BIT_MASK_DBI_ADDR_8822B) << BIT_SHIFT_DBI_ADDR_8822B)
#define BITS_DBI_ADDR_8822B                                                    \
	(BIT_MASK_DBI_ADDR_8822B << BIT_SHIFT_DBI_ADDR_8822B)
#define BIT_CLEAR_DBI_ADDR_8822B(x) ((x) & (~BITS_DBI_ADDR_8822B))
#define BIT_GET_DBI_ADDR_8822B(x)                                              \
	(((x) >> BIT_SHIFT_DBI_ADDR_8822B) & BIT_MASK_DBI_ADDR_8822B)
#define BIT_SET_DBI_ADDR_8822B(x, v)                                           \
	(BIT_CLEAR_DBI_ADDR_8822B(x) | BIT_DBI_ADDR_8822B(v))

/* 2 REG_MDIO_V1_8822B */

#define BIT_SHIFT_MDIO_RDATA_8822B 16
#define BIT_MASK_MDIO_RDATA_8822B 0xffff
#define BIT_MDIO_RDATA_8822B(x)                                                \
	(((x) & BIT_MASK_MDIO_RDATA_8822B) << BIT_SHIFT_MDIO_RDATA_8822B)
#define BITS_MDIO_RDATA_8822B                                                  \
	(BIT_MASK_MDIO_RDATA_8822B << BIT_SHIFT_MDIO_RDATA_8822B)
#define BIT_CLEAR_MDIO_RDATA_8822B(x) ((x) & (~BITS_MDIO_RDATA_8822B))
#define BIT_GET_MDIO_RDATA_8822B(x)                                            \
	(((x) >> BIT_SHIFT_MDIO_RDATA_8822B) & BIT_MASK_MDIO_RDATA_8822B)
#define BIT_SET_MDIO_RDATA_8822B(x, v)                                         \
	(BIT_CLEAR_MDIO_RDATA_8822B(x) | BIT_MDIO_RDATA_8822B(v))

#define BIT_SHIFT_MDIO_WDATA_8822B 0
#define BIT_MASK_MDIO_WDATA_8822B 0xffff
#define BIT_MDIO_WDATA_8822B(x)                                                \
	(((x) & BIT_MASK_MDIO_WDATA_8822B) << BIT_SHIFT_MDIO_WDATA_8822B)
#define BITS_MDIO_WDATA_8822B                                                  \
	(BIT_MASK_MDIO_WDATA_8822B << BIT_SHIFT_MDIO_WDATA_8822B)
#define BIT_CLEAR_MDIO_WDATA_8822B(x) ((x) & (~BITS_MDIO_WDATA_8822B))
#define BIT_GET_MDIO_WDATA_8822B(x)                                            \
	(((x) >> BIT_SHIFT_MDIO_WDATA_8822B) & BIT_MASK_MDIO_WDATA_8822B)
#define BIT_SET_MDIO_WDATA_8822B(x, v)                                         \
	(BIT_CLEAR_MDIO_WDATA_8822B(x) | BIT_MDIO_WDATA_8822B(v))

/* 2 REG_PCIE_MIX_CFG_8822B */

#define BIT_SHIFT_MDIO_PHY_ADDR_8822B 24
#define BIT_MASK_MDIO_PHY_ADDR_8822B 0x1f
#define BIT_MDIO_PHY_ADDR_8822B(x)                                             \
	(((x) & BIT_MASK_MDIO_PHY_ADDR_8822B) << BIT_SHIFT_MDIO_PHY_ADDR_8822B)
#define BITS_MDIO_PHY_ADDR_8822B                                               \
	(BIT_MASK_MDIO_PHY_ADDR_8822B << BIT_SHIFT_MDIO_PHY_ADDR_8822B)
#define BIT_CLEAR_MDIO_PHY_ADDR_8822B(x) ((x) & (~BITS_MDIO_PHY_ADDR_8822B))
#define BIT_GET_MDIO_PHY_ADDR_8822B(x)                                         \
	(((x) >> BIT_SHIFT_MDIO_PHY_ADDR_8822B) & BIT_MASK_MDIO_PHY_ADDR_8822B)
#define BIT_SET_MDIO_PHY_ADDR_8822B(x, v)                                      \
	(BIT_CLEAR_MDIO_PHY_ADDR_8822B(x) | BIT_MDIO_PHY_ADDR_8822B(v))

#define BIT_SHIFT_WATCH_DOG_RECORD_V1_8822B 10
#define BIT_MASK_WATCH_DOG_RECORD_V1_8822B 0x3fff
#define BIT_WATCH_DOG_RECORD_V1_8822B(x)                                       \
	(((x) & BIT_MASK_WATCH_DOG_RECORD_V1_8822B)                            \
	 << BIT_SHIFT_WATCH_DOG_RECORD_V1_8822B)
#define BITS_WATCH_DOG_RECORD_V1_8822B                                         \
	(BIT_MASK_WATCH_DOG_RECORD_V1_8822B                                    \
	 << BIT_SHIFT_WATCH_DOG_RECORD_V1_8822B)
#define BIT_CLEAR_WATCH_DOG_RECORD_V1_8822B(x)                                 \
	((x) & (~BITS_WATCH_DOG_RECORD_V1_8822B))
#define BIT_GET_WATCH_DOG_RECORD_V1_8822B(x)                                   \
	(((x) >> BIT_SHIFT_WATCH_DOG_RECORD_V1_8822B) &                        \
	 BIT_MASK_WATCH_DOG_RECORD_V1_8822B)
#define BIT_SET_WATCH_DOG_RECORD_V1_8822B(x, v)                                \
	(BIT_CLEAR_WATCH_DOG_RECORD_V1_8822B(x) |                              \
	 BIT_WATCH_DOG_RECORD_V1_8822B(v))

#define BIT_R_IO_TIMEOUT_FLAG_V1_8822B BIT(9)
#define BIT_EN_WATCH_DOG_8822B BIT(8)
#define BIT_ECRC_EN_V1_8822B BIT(7)
#define BIT_MDIO_RFLAG_V1_8822B BIT(6)
#define BIT_MDIO_WFLAG_V1_8822B BIT(5)

#define BIT_SHIFT_MDIO_REG_ADDR_V1_8822B 0
#define BIT_MASK_MDIO_REG_ADDR_V1_8822B 0x1f
#define BIT_MDIO_REG_ADDR_V1_8822B(x)                                          \
	(((x) & BIT_MASK_MDIO_REG_ADDR_V1_8822B)                               \
	 << BIT_SHIFT_MDIO_REG_ADDR_V1_8822B)
#define BITS_MDIO_REG_ADDR_V1_8822B                                            \
	(BIT_MASK_MDIO_REG_ADDR_V1_8822B << BIT_SHIFT_MDIO_REG_ADDR_V1_8822B)
#define BIT_CLEAR_MDIO_REG_ADDR_V1_8822B(x)                                    \
	((x) & (~BITS_MDIO_REG_ADDR_V1_8822B))
#define BIT_GET_MDIO_REG_ADDR_V1_8822B(x)                                      \
	(((x) >> BIT_SHIFT_MDIO_REG_ADDR_V1_8822B) &                           \
	 BIT_MASK_MDIO_REG_ADDR_V1_8822B)
#define BIT_SET_MDIO_REG_ADDR_V1_8822B(x, v)                                   \
	(BIT_CLEAR_MDIO_REG_ADDR_V1_8822B(x) | BIT_MDIO_REG_ADDR_V1_8822B(v))

/* 2 REG_HCI_MIX_CFG_8822B */
#define BIT_HOST_GEN2_SUPPORT_8822B BIT(20)

#define BIT_SHIFT_TXDMA_ERR_FLAG_8822B 16
#define BIT_MASK_TXDMA_ERR_FLAG_8822B 0xf
#define BIT_TXDMA_ERR_FLAG_8822B(x)                                            \
	(((x) & BIT_MASK_TXDMA_ERR_FLAG_8822B)                                 \
	 << BIT_SHIFT_TXDMA_ERR_FLAG_8822B)
#define BITS_TXDMA_ERR_FLAG_8822B                                              \
	(BIT_MASK_TXDMA_ERR_FLAG_8822B << BIT_SHIFT_TXDMA_ERR_FLAG_8822B)
#define BIT_CLEAR_TXDMA_ERR_FLAG_8822B(x) ((x) & (~BITS_TXDMA_ERR_FLAG_8822B))
#define BIT_GET_TXDMA_ERR_FLAG_8822B(x)                                        \
	(((x) >> BIT_SHIFT_TXDMA_ERR_FLAG_8822B) &                             \
	 BIT_MASK_TXDMA_ERR_FLAG_8822B)
#define BIT_SET_TXDMA_ERR_FLAG_8822B(x, v)                                     \
	(BIT_CLEAR_TXDMA_ERR_FLAG_8822B(x) | BIT_TXDMA_ERR_FLAG_8822B(v))

#define BIT_SHIFT_EARLY_MODE_SEL_8822B 12
#define BIT_MASK_EARLY_MODE_SEL_8822B 0xf
#define BIT_EARLY_MODE_SEL_8822B(x)                                            \
	(((x) & BIT_MASK_EARLY_MODE_SEL_8822B)                                 \
	 << BIT_SHIFT_EARLY_MODE_SEL_8822B)
#define BITS_EARLY_MODE_SEL_8822B                                              \
	(BIT_MASK_EARLY_MODE_SEL_8822B << BIT_SHIFT_EARLY_MODE_SEL_8822B)
#define BIT_CLEAR_EARLY_MODE_SEL_8822B(x) ((x) & (~BITS_EARLY_MODE_SEL_8822B))
#define BIT_GET_EARLY_MODE_SEL_8822B(x)                                        \
	(((x) >> BIT_SHIFT_EARLY_MODE_SEL_8822B) &                             \
	 BIT_MASK_EARLY_MODE_SEL_8822B)
#define BIT_SET_EARLY_MODE_SEL_8822B(x, v)                                     \
	(BIT_CLEAR_EARLY_MODE_SEL_8822B(x) | BIT_EARLY_MODE_SEL_8822B(v))

#define BIT_EPHY_RX50_EN_8822B BIT(11)

#define BIT_SHIFT_MSI_TIMEOUT_ID_V1_8822B 8
#define BIT_MASK_MSI_TIMEOUT_ID_V1_8822B 0x7
#define BIT_MSI_TIMEOUT_ID_V1_8822B(x)                                         \
	(((x) & BIT_MASK_MSI_TIMEOUT_ID_V1_8822B)                              \
	 << BIT_SHIFT_MSI_TIMEOUT_ID_V1_8822B)
#define BITS_MSI_TIMEOUT_ID_V1_8822B                                           \
	(BIT_MASK_MSI_TIMEOUT_ID_V1_8822B << BIT_SHIFT_MSI_TIMEOUT_ID_V1_8822B)
#define BIT_CLEAR_MSI_TIMEOUT_ID_V1_8822B(x)                                   \
	((x) & (~BITS_MSI_TIMEOUT_ID_V1_8822B))
#define BIT_GET_MSI_TIMEOUT_ID_V1_8822B(x)                                     \
	(((x) >> BIT_SHIFT_MSI_TIMEOUT_ID_V1_8822B) &                          \
	 BIT_MASK_MSI_TIMEOUT_ID_V1_8822B)
#define BIT_SET_MSI_TIMEOUT_ID_V1_8822B(x, v)                                  \
	(BIT_CLEAR_MSI_TIMEOUT_ID_V1_8822B(x) | BIT_MSI_TIMEOUT_ID_V1_8822B(v))

#define BIT_RADDR_RD_8822B BIT(7)
#define BIT_EN_MUL_TAG_8822B BIT(6)
#define BIT_EN_EARLY_MODE_8822B BIT(5)
#define BIT_L0S_LINK_OFF_8822B BIT(4)
#define BIT_ACT_LINK_OFF_8822B BIT(3)
#define BIT_EN_SLOW_MAC_TX_8822B BIT(2)
#define BIT_EN_SLOW_MAC_RX_8822B BIT(1)

/* 2 REG_STC_INT_CS_8822B(PCIE STATE CHANGE INTERRUPT CONTROL AND STATUS) */
#define BIT_STC_INT_EN_8822B BIT(31)

#define BIT_SHIFT_STC_INT_FLAG_8822B 16
#define BIT_MASK_STC_INT_FLAG_8822B 0xff
#define BIT_STC_INT_FLAG_8822B(x)                                              \
	(((x) & BIT_MASK_STC_INT_FLAG_8822B) << BIT_SHIFT_STC_INT_FLAG_8822B)
#define BITS_STC_INT_FLAG_8822B                                                \
	(BIT_MASK_STC_INT_FLAG_8822B << BIT_SHIFT_STC_INT_FLAG_8822B)
#define BIT_CLEAR_STC_INT_FLAG_8822B(x) ((x) & (~BITS_STC_INT_FLAG_8822B))
#define BIT_GET_STC_INT_FLAG_8822B(x)                                          \
	(((x) >> BIT_SHIFT_STC_INT_FLAG_8822B) & BIT_MASK_STC_INT_FLAG_8822B)
#define BIT_SET_STC_INT_FLAG_8822B(x, v)                                       \
	(BIT_CLEAR_STC_INT_FLAG_8822B(x) | BIT_STC_INT_FLAG_8822B(v))

#define BIT_SHIFT_STC_INT_IDX_8822B 8
#define BIT_MASK_STC_INT_IDX_8822B 0x7
#define BIT_STC_INT_IDX_8822B(x)                                               \
	(((x) & BIT_MASK_STC_INT_IDX_8822B) << BIT_SHIFT_STC_INT_IDX_8822B)
#define BITS_STC_INT_IDX_8822B                                                 \
	(BIT_MASK_STC_INT_IDX_8822B << BIT_SHIFT_STC_INT_IDX_8822B)
#define BIT_CLEAR_STC_INT_IDX_8822B(x) ((x) & (~BITS_STC_INT_IDX_8822B))
#define BIT_GET_STC_INT_IDX_8822B(x)                                           \
	(((x) >> BIT_SHIFT_STC_INT_IDX_8822B) & BIT_MASK_STC_INT_IDX_8822B)
#define BIT_SET_STC_INT_IDX_8822B(x, v)                                        \
	(BIT_CLEAR_STC_INT_IDX_8822B(x) | BIT_STC_INT_IDX_8822B(v))

#define BIT_SHIFT_STC_INT_REALTIME_CS_8822B 0
#define BIT_MASK_STC_INT_REALTIME_CS_8822B 0x3f
#define BIT_STC_INT_REALTIME_CS_8822B(x)                                       \
	(((x) & BIT_MASK_STC_INT_REALTIME_CS_8822B)                            \
	 << BIT_SHIFT_STC_INT_REALTIME_CS_8822B)
#define BITS_STC_INT_REALTIME_CS_8822B                                         \
	(BIT_MASK_STC_INT_REALTIME_CS_8822B                                    \
	 << BIT_SHIFT_STC_INT_REALTIME_CS_8822B)
#define BIT_CLEAR_STC_INT_REALTIME_CS_8822B(x)                                 \
	((x) & (~BITS_STC_INT_REALTIME_CS_8822B))
#define BIT_GET_STC_INT_REALTIME_CS_8822B(x)                                   \
	(((x) >> BIT_SHIFT_STC_INT_REALTIME_CS_8822B) &                        \
	 BIT_MASK_STC_INT_REALTIME_CS_8822B)
#define BIT_SET_STC_INT_REALTIME_CS_8822B(x, v)                                \
	(BIT_CLEAR_STC_INT_REALTIME_CS_8822B(x) |                              \
	 BIT_STC_INT_REALTIME_CS_8822B(v))

/* 2 REG_ST_INT_CFG_8822B(PCIE STATE CHANGE INTERRUPT CONFIGURATION) */
#define BIT_STC_INT_GRP_EN_8822B BIT(31)

#define BIT_SHIFT_STC_INT_EXPECT_LS_8822B 8
#define BIT_MASK_STC_INT_EXPECT_LS_8822B 0x3f
#define BIT_STC_INT_EXPECT_LS_8822B(x)                                         \
	(((x) & BIT_MASK_STC_INT_EXPECT_LS_8822B)                              \
	 << BIT_SHIFT_STC_INT_EXPECT_LS_8822B)
#define BITS_STC_INT_EXPECT_LS_8822B                                           \
	(BIT_MASK_STC_INT_EXPECT_LS_8822B << BIT_SHIFT_STC_INT_EXPECT_LS_8822B)
#define BIT_CLEAR_STC_INT_EXPECT_LS_8822B(x)                                   \
	((x) & (~BITS_STC_INT_EXPECT_LS_8822B))
#define BIT_GET_STC_INT_EXPECT_LS_8822B(x)                                     \
	(((x) >> BIT_SHIFT_STC_INT_EXPECT_LS_8822B) &                          \
	 BIT_MASK_STC_INT_EXPECT_LS_8822B)
#define BIT_SET_STC_INT_EXPECT_LS_8822B(x, v)                                  \
	(BIT_CLEAR_STC_INT_EXPECT_LS_8822B(x) | BIT_STC_INT_EXPECT_LS_8822B(v))

#define BIT_SHIFT_STC_INT_EXPECT_CS_8822B 0
#define BIT_MASK_STC_INT_EXPECT_CS_8822B 0x3f
#define BIT_STC_INT_EXPECT_CS_8822B(x)                                         \
	(((x) & BIT_MASK_STC_INT_EXPECT_CS_8822B)                              \
	 << BIT_SHIFT_STC_INT_EXPECT_CS_8822B)
#define BITS_STC_INT_EXPECT_CS_8822B                                           \
	(BIT_MASK_STC_INT_EXPECT_CS_8822B << BIT_SHIFT_STC_INT_EXPECT_CS_8822B)
#define BIT_CLEAR_STC_INT_EXPECT_CS_8822B(x)                                   \
	((x) & (~BITS_STC_INT_EXPECT_CS_8822B))
#define BIT_GET_STC_INT_EXPECT_CS_8822B(x)                                     \
	(((x) >> BIT_SHIFT_STC_INT_EXPECT_CS_8822B) &                          \
	 BIT_MASK_STC_INT_EXPECT_CS_8822B)
#define BIT_SET_STC_INT_EXPECT_CS_8822B(x, v)                                  \
	(BIT_CLEAR_STC_INT_EXPECT_CS_8822B(x) | BIT_STC_INT_EXPECT_CS_8822B(v))

/* 2 REG_CMU_DLY_CTRL_8822B(PCIE PHY CLOCK MGT UNIT DELAY CONTROL ) */
#define BIT_CMU_DLY_EN_8822B BIT(31)
#define BIT_CMU_DLY_MODE_8822B BIT(30)

#define BIT_SHIFT_CMU_DLY_PRE_DIV_8822B 0
#define BIT_MASK_CMU_DLY_PRE_DIV_8822B 0xff
#define BIT_CMU_DLY_PRE_DIV_8822B(x)                                           \
	(((x) & BIT_MASK_CMU_DLY_PRE_DIV_8822B)                                \
	 << BIT_SHIFT_CMU_DLY_PRE_DIV_8822B)
#define BITS_CMU_DLY_PRE_DIV_8822B                                             \
	(BIT_MASK_CMU_DLY_PRE_DIV_8822B << BIT_SHIFT_CMU_DLY_PRE_DIV_8822B)
#define BIT_CLEAR_CMU_DLY_PRE_DIV_8822B(x) ((x) & (~BITS_CMU_DLY_PRE_DIV_8822B))
#define BIT_GET_CMU_DLY_PRE_DIV_8822B(x)                                       \
	(((x) >> BIT_SHIFT_CMU_DLY_PRE_DIV_8822B) &                            \
	 BIT_MASK_CMU_DLY_PRE_DIV_8822B)
#define BIT_SET_CMU_DLY_PRE_DIV_8822B(x, v)                                    \
	(BIT_CLEAR_CMU_DLY_PRE_DIV_8822B(x) | BIT_CMU_DLY_PRE_DIV_8822B(v))

/* 2 REG_CMU_DLY_CFG_8822B(PCIE PHY CLOCK MGT UNIT DELAY CONFIGURATION ) */

#define BIT_SHIFT_CMU_DLY_LTR_A2I_8822B 24
#define BIT_MASK_CMU_DLY_LTR_A2I_8822B 0xff
#define BIT_CMU_DLY_LTR_A2I_8822B(x)                                           \
	(((x) & BIT_MASK_CMU_DLY_LTR_A2I_8822B)                                \
	 << BIT_SHIFT_CMU_DLY_LTR_A2I_8822B)
#define BITS_CMU_DLY_LTR_A2I_8822B                                             \
	(BIT_MASK_CMU_DLY_LTR_A2I_8822B << BIT_SHIFT_CMU_DLY_LTR_A2I_8822B)
#define BIT_CLEAR_CMU_DLY_LTR_A2I_8822B(x) ((x) & (~BITS_CMU_DLY_LTR_A2I_8822B))
#define BIT_GET_CMU_DLY_LTR_A2I_8822B(x)                                       \
	(((x) >> BIT_SHIFT_CMU_DLY_LTR_A2I_8822B) &                            \
	 BIT_MASK_CMU_DLY_LTR_A2I_8822B)
#define BIT_SET_CMU_DLY_LTR_A2I_8822B(x, v)                                    \
	(BIT_CLEAR_CMU_DLY_LTR_A2I_8822B(x) | BIT_CMU_DLY_LTR_A2I_8822B(v))

#define BIT_SHIFT_CMU_DLY_LTR_I2A_8822B 16
#define BIT_MASK_CMU_DLY_LTR_I2A_8822B 0xff
#define BIT_CMU_DLY_LTR_I2A_8822B(x)                                           \
	(((x) & BIT_MASK_CMU_DLY_LTR_I2A_8822B)                                \
	 << BIT_SHIFT_CMU_DLY_LTR_I2A_8822B)
#define BITS_CMU_DLY_LTR_I2A_8822B                                             \
	(BIT_MASK_CMU_DLY_LTR_I2A_8822B << BIT_SHIFT_CMU_DLY_LTR_I2A_8822B)
#define BIT_CLEAR_CMU_DLY_LTR_I2A_8822B(x) ((x) & (~BITS_CMU_DLY_LTR_I2A_8822B))
#define BIT_GET_CMU_DLY_LTR_I2A_8822B(x)                                       \
	(((x) >> BIT_SHIFT_CMU_DLY_LTR_I2A_8822B) &                            \
	 BIT_MASK_CMU_DLY_LTR_I2A_8822B)
#define BIT_SET_CMU_DLY_LTR_I2A_8822B(x, v)                                    \
	(BIT_CLEAR_CMU_DLY_LTR_I2A_8822B(x) | BIT_CMU_DLY_LTR_I2A_8822B(v))

#define BIT_SHIFT_CMU_DLY_LTR_IDLE_8822B 8
#define BIT_MASK_CMU_DLY_LTR_IDLE_8822B 0xff
#define BIT_CMU_DLY_LTR_IDLE_8822B(x)                                          \
	(((x) & BIT_MASK_CMU_DLY_LTR_IDLE_8822B)                               \
	 << BIT_SHIFT_CMU_DLY_LTR_IDLE_8822B)
#define BITS_CMU_DLY_LTR_IDLE_8822B                                            \
	(BIT_MASK_CMU_DLY_LTR_IDLE_8822B << BIT_SHIFT_CMU_DLY_LTR_IDLE_8822B)
#define BIT_CLEAR_CMU_DLY_LTR_IDLE_8822B(x)                                    \
	((x) & (~BITS_CMU_DLY_LTR_IDLE_8822B))
#define BIT_GET_CMU_DLY_LTR_IDLE_8822B(x)                                      \
	(((x) >> BIT_SHIFT_CMU_DLY_LTR_IDLE_8822B) &                           \
	 BIT_MASK_CMU_DLY_LTR_IDLE_8822B)
#define BIT_SET_CMU_DLY_LTR_IDLE_8822B(x, v)                                   \
	(BIT_CLEAR_CMU_DLY_LTR_IDLE_8822B(x) | BIT_CMU_DLY_LTR_IDLE_8822B(v))

#define BIT_SHIFT_CMU_DLY_LTR_ACT_8822B 0
#define BIT_MASK_CMU_DLY_LTR_ACT_8822B 0xff
#define BIT_CMU_DLY_LTR_ACT_8822B(x)                                           \
	(((x) & BIT_MASK_CMU_DLY_LTR_ACT_8822B)                                \
	 << BIT_SHIFT_CMU_DLY_LTR_ACT_8822B)
#define BITS_CMU_DLY_LTR_ACT_8822B                                             \
	(BIT_MASK_CMU_DLY_LTR_ACT_8822B << BIT_SHIFT_CMU_DLY_LTR_ACT_8822B)
#define BIT_CLEAR_CMU_DLY_LTR_ACT_8822B(x) ((x) & (~BITS_CMU_DLY_LTR_ACT_8822B))
#define BIT_GET_CMU_DLY_LTR_ACT_8822B(x)                                       \
	(((x) >> BIT_SHIFT_CMU_DLY_LTR_ACT_8822B) &                            \
	 BIT_MASK_CMU_DLY_LTR_ACT_8822B)
#define BIT_SET_CMU_DLY_LTR_ACT_8822B(x, v)                                    \
	(BIT_CLEAR_CMU_DLY_LTR_ACT_8822B(x) | BIT_CMU_DLY_LTR_ACT_8822B(v))

/* 2 REG_H2CQ_TXBD_DESA_8822B */

#define BIT_SHIFT_H2CQ_TXBD_DESA_8822B 0
#define BIT_MASK_H2CQ_TXBD_DESA_8822B 0xffffffffffffffffL
#define BIT_H2CQ_TXBD_DESA_8822B(x)                                            \
	(((x) & BIT_MASK_H2CQ_TXBD_DESA_8822B)                                 \
	 << BIT_SHIFT_H2CQ_TXBD_DESA_8822B)
#define BITS_H2CQ_TXBD_DESA_8822B                                              \
	(BIT_MASK_H2CQ_TXBD_DESA_8822B << BIT_SHIFT_H2CQ_TXBD_DESA_8822B)
#define BIT_CLEAR_H2CQ_TXBD_DESA_8822B(x) ((x) & (~BITS_H2CQ_TXBD_DESA_8822B))
#define BIT_GET_H2CQ_TXBD_DESA_8822B(x)                                        \
	(((x) >> BIT_SHIFT_H2CQ_TXBD_DESA_8822B) &                             \
	 BIT_MASK_H2CQ_TXBD_DESA_8822B)
#define BIT_SET_H2CQ_TXBD_DESA_8822B(x, v)                                     \
	(BIT_CLEAR_H2CQ_TXBD_DESA_8822B(x) | BIT_H2CQ_TXBD_DESA_8822B(v))

/* 2 REG_H2CQ_TXBD_NUM_8822B */
#define BIT_PCIE_H2CQ_FLAG_8822B BIT(14)

#define BIT_SHIFT_H2CQ_DESC_MODE_8822B 12
#define BIT_MASK_H2CQ_DESC_MODE_8822B 0x3
#define BIT_H2CQ_DESC_MODE_8822B(x)                                            \
	(((x) & BIT_MASK_H2CQ_DESC_MODE_8822B)                                 \
	 << BIT_SHIFT_H2CQ_DESC_MODE_8822B)
#define BITS_H2CQ_DESC_MODE_8822B                                              \
	(BIT_MASK_H2CQ_DESC_MODE_8822B << BIT_SHIFT_H2CQ_DESC_MODE_8822B)
#define BIT_CLEAR_H2CQ_DESC_MODE_8822B(x) ((x) & (~BITS_H2CQ_DESC_MODE_8822B))
#define BIT_GET_H2CQ_DESC_MODE_8822B(x)                                        \
	(((x) >> BIT_SHIFT_H2CQ_DESC_MODE_8822B) &                             \
	 BIT_MASK_H2CQ_DESC_MODE_8822B)
#define BIT_SET_H2CQ_DESC_MODE_8822B(x, v)                                     \
	(BIT_CLEAR_H2CQ_DESC_MODE_8822B(x) | BIT_H2CQ_DESC_MODE_8822B(v))

#define BIT_SHIFT_H2CQ_DESC_NUM_8822B 0
#define BIT_MASK_H2CQ_DESC_NUM_8822B 0xfff
#define BIT_H2CQ_DESC_NUM_8822B(x)                                             \
	(((x) & BIT_MASK_H2CQ_DESC_NUM_8822B) << BIT_SHIFT_H2CQ_DESC_NUM_8822B)
#define BITS_H2CQ_DESC_NUM_8822B                                               \
	(BIT_MASK_H2CQ_DESC_NUM_8822B << BIT_SHIFT_H2CQ_DESC_NUM_8822B)
#define BIT_CLEAR_H2CQ_DESC_NUM_8822B(x) ((x) & (~BITS_H2CQ_DESC_NUM_8822B))
#define BIT_GET_H2CQ_DESC_NUM_8822B(x)                                         \
	(((x) >> BIT_SHIFT_H2CQ_DESC_NUM_8822B) & BIT_MASK_H2CQ_DESC_NUM_8822B)
#define BIT_SET_H2CQ_DESC_NUM_8822B(x, v)                                      \
	(BIT_CLEAR_H2CQ_DESC_NUM_8822B(x) | BIT_H2CQ_DESC_NUM_8822B(v))

/* 2 REG_H2CQ_TXBD_IDX_8822B */

#define BIT_SHIFT_H2CQ_HW_IDX_8822B 16
#define BIT_MASK_H2CQ_HW_IDX_8822B 0xfff
#define BIT_H2CQ_HW_IDX_8822B(x)                                               \
	(((x) & BIT_MASK_H2CQ_HW_IDX_8822B) << BIT_SHIFT_H2CQ_HW_IDX_8822B)
#define BITS_H2CQ_HW_IDX_8822B                                                 \
	(BIT_MASK_H2CQ_HW_IDX_8822B << BIT_SHIFT_H2CQ_HW_IDX_8822B)
#define BIT_CLEAR_H2CQ_HW_IDX_8822B(x) ((x) & (~BITS_H2CQ_HW_IDX_8822B))
#define BIT_GET_H2CQ_HW_IDX_8822B(x)                                           \
	(((x) >> BIT_SHIFT_H2CQ_HW_IDX_8822B) & BIT_MASK_H2CQ_HW_IDX_8822B)
#define BIT_SET_H2CQ_HW_IDX_8822B(x, v)                                        \
	(BIT_CLEAR_H2CQ_HW_IDX_8822B(x) | BIT_H2CQ_HW_IDX_8822B(v))

#define BIT_SHIFT_H2CQ_HOST_IDX_8822B 0
#define BIT_MASK_H2CQ_HOST_IDX_8822B 0xfff
#define BIT_H2CQ_HOST_IDX_8822B(x)                                             \
	(((x) & BIT_MASK_H2CQ_HOST_IDX_8822B) << BIT_SHIFT_H2CQ_HOST_IDX_8822B)
#define BITS_H2CQ_HOST_IDX_8822B                                               \
	(BIT_MASK_H2CQ_HOST_IDX_8822B << BIT_SHIFT_H2CQ_HOST_IDX_8822B)
#define BIT_CLEAR_H2CQ_HOST_IDX_8822B(x) ((x) & (~BITS_H2CQ_HOST_IDX_8822B))
#define BIT_GET_H2CQ_HOST_IDX_8822B(x)                                         \
	(((x) >> BIT_SHIFT_H2CQ_HOST_IDX_8822B) & BIT_MASK_H2CQ_HOST_IDX_8822B)
#define BIT_SET_H2CQ_HOST_IDX_8822B(x, v)                                      \
	(BIT_CLEAR_H2CQ_HOST_IDX_8822B(x) | BIT_H2CQ_HOST_IDX_8822B(v))

/* 2 REG_H2CQ_CSR_8822B[31:0] (H2CQ CONTROL AND STATUS) */
#define BIT_H2CQ_FULL_8822B BIT(31)
#define BIT_CLR_H2CQ_HOST_IDX_8822B BIT(16)
#define BIT_CLR_H2CQ_HW_IDX_8822B BIT(8)
#define BIT_STOP_H2CQ_8822B BIT(0)

/* 2 REG_CHANGE_PCIE_SPEED_8822B */
#define BIT_CHANGE_PCIE_SPEED_8822B BIT(18)

#define BIT_SHIFT_GEN1_GEN2_8822B 16
#define BIT_MASK_GEN1_GEN2_8822B 0x3
#define BIT_GEN1_GEN2_8822B(x)                                                 \
	(((x) & BIT_MASK_GEN1_GEN2_8822B) << BIT_SHIFT_GEN1_GEN2_8822B)
#define BITS_GEN1_GEN2_8822B                                                   \
	(BIT_MASK_GEN1_GEN2_8822B << BIT_SHIFT_GEN1_GEN2_8822B)
#define BIT_CLEAR_GEN1_GEN2_8822B(x) ((x) & (~BITS_GEN1_GEN2_8822B))
#define BIT_GET_GEN1_GEN2_8822B(x)                                             \
	(((x) >> BIT_SHIFT_GEN1_GEN2_8822B) & BIT_MASK_GEN1_GEN2_8822B)
#define BIT_SET_GEN1_GEN2_8822B(x, v)                                          \
	(BIT_CLEAR_GEN1_GEN2_8822B(x) | BIT_GEN1_GEN2_8822B(v))

#define BIT_SHIFT_RXDMA_ERROR_COUNTER_8822B 8
#define BIT_MASK_RXDMA_ERROR_COUNTER_8822B 0xff
#define BIT_RXDMA_ERROR_COUNTER_8822B(x)                                       \
	(((x) & BIT_MASK_RXDMA_ERROR_COUNTER_8822B)                            \
	 << BIT_SHIFT_RXDMA_ERROR_COUNTER_8822B)
#define BITS_RXDMA_ERROR_COUNTER_8822B                                         \
	(BIT_MASK_RXDMA_ERROR_COUNTER_8822B                                    \
	 << BIT_SHIFT_RXDMA_ERROR_COUNTER_8822B)
#define BIT_CLEAR_RXDMA_ERROR_COUNTER_8822B(x)                                 \
	((x) & (~BITS_RXDMA_ERROR_COUNTER_8822B))
#define BIT_GET_RXDMA_ERROR_COUNTER_8822B(x)                                   \
	(((x) >> BIT_SHIFT_RXDMA_ERROR_COUNTER_8822B) &                        \
	 BIT_MASK_RXDMA_ERROR_COUNTER_8822B)
#define BIT_SET_RXDMA_ERROR_COUNTER_8822B(x, v)                                \
	(BIT_CLEAR_RXDMA_ERROR_COUNTER_8822B(x) |                              \
	 BIT_RXDMA_ERROR_COUNTER_8822B(v))

#define BIT_TXDMA_ERROR_HANDLE_STATUS_8822B BIT(7)
#define BIT_TXDMA_ERROR_PULSE_8822B BIT(6)
#define BIT_TXDMA_STUCK_ERROR_HANDLE_ENABLE_8822B BIT(5)
#define BIT_TXDMA_RETURN_ERROR_ENABLE_8822B BIT(4)
#define BIT_RXDMA_ERROR_HANDLE_STATUS_8822B BIT(3)

#define BIT_SHIFT_AUTO_HANG_RELEASE_8822B 0
#define BIT_MASK_AUTO_HANG_RELEASE_8822B 0x7
#define BIT_AUTO_HANG_RELEASE_8822B(x)                                         \
	(((x) & BIT_MASK_AUTO_HANG_RELEASE_8822B)                              \
	 << BIT_SHIFT_AUTO_HANG_RELEASE_8822B)
#define BITS_AUTO_HANG_RELEASE_8822B                                           \
	(BIT_MASK_AUTO_HANG_RELEASE_8822B << BIT_SHIFT_AUTO_HANG_RELEASE_8822B)
#define BIT_CLEAR_AUTO_HANG_RELEASE_8822B(x)                                   \
	((x) & (~BITS_AUTO_HANG_RELEASE_8822B))
#define BIT_GET_AUTO_HANG_RELEASE_8822B(x)                                     \
	(((x) >> BIT_SHIFT_AUTO_HANG_RELEASE_8822B) &                          \
	 BIT_MASK_AUTO_HANG_RELEASE_8822B)
#define BIT_SET_AUTO_HANG_RELEASE_8822B(x, v)                                  \
	(BIT_CLEAR_AUTO_HANG_RELEASE_8822B(x) | BIT_AUTO_HANG_RELEASE_8822B(v))

/* 2 REG_OLD_DEHANG_8822B */
#define BIT_OLD_DEHANG_8822B BIT(1)

/* 2 REG_Q0_INFO_8822B */

#define BIT_SHIFT_QUEUEMACID_Q0_V1_8822B 25
#define BIT_MASK_QUEUEMACID_Q0_V1_8822B 0x7f
#define BIT_QUEUEMACID_Q0_V1_8822B(x)                                          \
	(((x) & BIT_MASK_QUEUEMACID_Q0_V1_8822B)                               \
	 << BIT_SHIFT_QUEUEMACID_Q0_V1_8822B)
#define BITS_QUEUEMACID_Q0_V1_8822B                                            \
	(BIT_MASK_QUEUEMACID_Q0_V1_8822B << BIT_SHIFT_QUEUEMACID_Q0_V1_8822B)
#define BIT_CLEAR_QUEUEMACID_Q0_V1_8822B(x)                                    \
	((x) & (~BITS_QUEUEMACID_Q0_V1_8822B))
#define BIT_GET_QUEUEMACID_Q0_V1_8822B(x)                                      \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q0_V1_8822B) &                           \
	 BIT_MASK_QUEUEMACID_Q0_V1_8822B)
#define BIT_SET_QUEUEMACID_Q0_V1_8822B(x, v)                                   \
	(BIT_CLEAR_QUEUEMACID_Q0_V1_8822B(x) | BIT_QUEUEMACID_Q0_V1_8822B(v))

#define BIT_SHIFT_QUEUEAC_Q0_V1_8822B 23
#define BIT_MASK_QUEUEAC_Q0_V1_8822B 0x3
#define BIT_QUEUEAC_Q0_V1_8822B(x)                                             \
	(((x) & BIT_MASK_QUEUEAC_Q0_V1_8822B) << BIT_SHIFT_QUEUEAC_Q0_V1_8822B)
#define BITS_QUEUEAC_Q0_V1_8822B                                               \
	(BIT_MASK_QUEUEAC_Q0_V1_8822B << BIT_SHIFT_QUEUEAC_Q0_V1_8822B)
#define BIT_CLEAR_QUEUEAC_Q0_V1_8822B(x) ((x) & (~BITS_QUEUEAC_Q0_V1_8822B))
#define BIT_GET_QUEUEAC_Q0_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_QUEUEAC_Q0_V1_8822B) & BIT_MASK_QUEUEAC_Q0_V1_8822B)
#define BIT_SET_QUEUEAC_Q0_V1_8822B(x, v)                                      \
	(BIT_CLEAR_QUEUEAC_Q0_V1_8822B(x) | BIT_QUEUEAC_Q0_V1_8822B(v))

#define BIT_TIDEMPTY_Q0_V1_8822B BIT(22)

#define BIT_SHIFT_TAIL_PKT_Q0_V2_8822B 11
#define BIT_MASK_TAIL_PKT_Q0_V2_8822B 0x7ff
#define BIT_TAIL_PKT_Q0_V2_8822B(x)                                            \
	(((x) & BIT_MASK_TAIL_PKT_Q0_V2_8822B)                                 \
	 << BIT_SHIFT_TAIL_PKT_Q0_V2_8822B)
#define BITS_TAIL_PKT_Q0_V2_8822B                                              \
	(BIT_MASK_TAIL_PKT_Q0_V2_8822B << BIT_SHIFT_TAIL_PKT_Q0_V2_8822B)
#define BIT_CLEAR_TAIL_PKT_Q0_V2_8822B(x) ((x) & (~BITS_TAIL_PKT_Q0_V2_8822B))
#define BIT_GET_TAIL_PKT_Q0_V2_8822B(x)                                        \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q0_V2_8822B) &                             \
	 BIT_MASK_TAIL_PKT_Q0_V2_8822B)
#define BIT_SET_TAIL_PKT_Q0_V2_8822B(x, v)                                     \
	(BIT_CLEAR_TAIL_PKT_Q0_V2_8822B(x) | BIT_TAIL_PKT_Q0_V2_8822B(v))

#define BIT_SHIFT_HEAD_PKT_Q0_V1_8822B 0
#define BIT_MASK_HEAD_PKT_Q0_V1_8822B 0x7ff
#define BIT_HEAD_PKT_Q0_V1_8822B(x)                                            \
	(((x) & BIT_MASK_HEAD_PKT_Q0_V1_8822B)                                 \
	 << BIT_SHIFT_HEAD_PKT_Q0_V1_8822B)
#define BITS_HEAD_PKT_Q0_V1_8822B                                              \
	(BIT_MASK_HEAD_PKT_Q0_V1_8822B << BIT_SHIFT_HEAD_PKT_Q0_V1_8822B)
#define BIT_CLEAR_HEAD_PKT_Q0_V1_8822B(x) ((x) & (~BITS_HEAD_PKT_Q0_V1_8822B))
#define BIT_GET_HEAD_PKT_Q0_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q0_V1_8822B) &                             \
	 BIT_MASK_HEAD_PKT_Q0_V1_8822B)
#define BIT_SET_HEAD_PKT_Q0_V1_8822B(x, v)                                     \
	(BIT_CLEAR_HEAD_PKT_Q0_V1_8822B(x) | BIT_HEAD_PKT_Q0_V1_8822B(v))

/* 2 REG_Q1_INFO_8822B */

#define BIT_SHIFT_QUEUEMACID_Q1_V1_8822B 25
#define BIT_MASK_QUEUEMACID_Q1_V1_8822B 0x7f
#define BIT_QUEUEMACID_Q1_V1_8822B(x)                                          \
	(((x) & BIT_MASK_QUEUEMACID_Q1_V1_8822B)                               \
	 << BIT_SHIFT_QUEUEMACID_Q1_V1_8822B)
#define BITS_QUEUEMACID_Q1_V1_8822B                                            \
	(BIT_MASK_QUEUEMACID_Q1_V1_8822B << BIT_SHIFT_QUEUEMACID_Q1_V1_8822B)
#define BIT_CLEAR_QUEUEMACID_Q1_V1_8822B(x)                                    \
	((x) & (~BITS_QUEUEMACID_Q1_V1_8822B))
#define BIT_GET_QUEUEMACID_Q1_V1_8822B(x)                                      \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q1_V1_8822B) &                           \
	 BIT_MASK_QUEUEMACID_Q1_V1_8822B)
#define BIT_SET_QUEUEMACID_Q1_V1_8822B(x, v)                                   \
	(BIT_CLEAR_QUEUEMACID_Q1_V1_8822B(x) | BIT_QUEUEMACID_Q1_V1_8822B(v))

#define BIT_SHIFT_QUEUEAC_Q1_V1_8822B 23
#define BIT_MASK_QUEUEAC_Q1_V1_8822B 0x3
#define BIT_QUEUEAC_Q1_V1_8822B(x)                                             \
	(((x) & BIT_MASK_QUEUEAC_Q1_V1_8822B) << BIT_SHIFT_QUEUEAC_Q1_V1_8822B)
#define BITS_QUEUEAC_Q1_V1_8822B                                               \
	(BIT_MASK_QUEUEAC_Q1_V1_8822B << BIT_SHIFT_QUEUEAC_Q1_V1_8822B)
#define BIT_CLEAR_QUEUEAC_Q1_V1_8822B(x) ((x) & (~BITS_QUEUEAC_Q1_V1_8822B))
#define BIT_GET_QUEUEAC_Q1_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_QUEUEAC_Q1_V1_8822B) & BIT_MASK_QUEUEAC_Q1_V1_8822B)
#define BIT_SET_QUEUEAC_Q1_V1_8822B(x, v)                                      \
	(BIT_CLEAR_QUEUEAC_Q1_V1_8822B(x) | BIT_QUEUEAC_Q1_V1_8822B(v))

#define BIT_TIDEMPTY_Q1_V1_8822B BIT(22)

#define BIT_SHIFT_TAIL_PKT_Q1_V2_8822B 11
#define BIT_MASK_TAIL_PKT_Q1_V2_8822B 0x7ff
#define BIT_TAIL_PKT_Q1_V2_8822B(x)                                            \
	(((x) & BIT_MASK_TAIL_PKT_Q1_V2_8822B)                                 \
	 << BIT_SHIFT_TAIL_PKT_Q1_V2_8822B)
#define BITS_TAIL_PKT_Q1_V2_8822B                                              \
	(BIT_MASK_TAIL_PKT_Q1_V2_8822B << BIT_SHIFT_TAIL_PKT_Q1_V2_8822B)
#define BIT_CLEAR_TAIL_PKT_Q1_V2_8822B(x) ((x) & (~BITS_TAIL_PKT_Q1_V2_8822B))
#define BIT_GET_TAIL_PKT_Q1_V2_8822B(x)                                        \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q1_V2_8822B) &                             \
	 BIT_MASK_TAIL_PKT_Q1_V2_8822B)
#define BIT_SET_TAIL_PKT_Q1_V2_8822B(x, v)                                     \
	(BIT_CLEAR_TAIL_PKT_Q1_V2_8822B(x) | BIT_TAIL_PKT_Q1_V2_8822B(v))

#define BIT_SHIFT_HEAD_PKT_Q1_V1_8822B 0
#define BIT_MASK_HEAD_PKT_Q1_V1_8822B 0x7ff
#define BIT_HEAD_PKT_Q1_V1_8822B(x)                                            \
	(((x) & BIT_MASK_HEAD_PKT_Q1_V1_8822B)                                 \
	 << BIT_SHIFT_HEAD_PKT_Q1_V1_8822B)
#define BITS_HEAD_PKT_Q1_V1_8822B                                              \
	(BIT_MASK_HEAD_PKT_Q1_V1_8822B << BIT_SHIFT_HEAD_PKT_Q1_V1_8822B)
#define BIT_CLEAR_HEAD_PKT_Q1_V1_8822B(x) ((x) & (~BITS_HEAD_PKT_Q1_V1_8822B))
#define BIT_GET_HEAD_PKT_Q1_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q1_V1_8822B) &                             \
	 BIT_MASK_HEAD_PKT_Q1_V1_8822B)
#define BIT_SET_HEAD_PKT_Q1_V1_8822B(x, v)                                     \
	(BIT_CLEAR_HEAD_PKT_Q1_V1_8822B(x) | BIT_HEAD_PKT_Q1_V1_8822B(v))

/* 2 REG_Q2_INFO_8822B */

#define BIT_SHIFT_QUEUEMACID_Q2_V1_8822B 25
#define BIT_MASK_QUEUEMACID_Q2_V1_8822B 0x7f
#define BIT_QUEUEMACID_Q2_V1_8822B(x)                                          \
	(((x) & BIT_MASK_QUEUEMACID_Q2_V1_8822B)                               \
	 << BIT_SHIFT_QUEUEMACID_Q2_V1_8822B)
#define BITS_QUEUEMACID_Q2_V1_8822B                                            \
	(BIT_MASK_QUEUEMACID_Q2_V1_8822B << BIT_SHIFT_QUEUEMACID_Q2_V1_8822B)
#define BIT_CLEAR_QUEUEMACID_Q2_V1_8822B(x)                                    \
	((x) & (~BITS_QUEUEMACID_Q2_V1_8822B))
#define BIT_GET_QUEUEMACID_Q2_V1_8822B(x)                                      \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q2_V1_8822B) &                           \
	 BIT_MASK_QUEUEMACID_Q2_V1_8822B)
#define BIT_SET_QUEUEMACID_Q2_V1_8822B(x, v)                                   \
	(BIT_CLEAR_QUEUEMACID_Q2_V1_8822B(x) | BIT_QUEUEMACID_Q2_V1_8822B(v))

#define BIT_SHIFT_QUEUEAC_Q2_V1_8822B 23
#define BIT_MASK_QUEUEAC_Q2_V1_8822B 0x3
#define BIT_QUEUEAC_Q2_V1_8822B(x)                                             \
	(((x) & BIT_MASK_QUEUEAC_Q2_V1_8822B) << BIT_SHIFT_QUEUEAC_Q2_V1_8822B)
#define BITS_QUEUEAC_Q2_V1_8822B                                               \
	(BIT_MASK_QUEUEAC_Q2_V1_8822B << BIT_SHIFT_QUEUEAC_Q2_V1_8822B)
#define BIT_CLEAR_QUEUEAC_Q2_V1_8822B(x) ((x) & (~BITS_QUEUEAC_Q2_V1_8822B))
#define BIT_GET_QUEUEAC_Q2_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_QUEUEAC_Q2_V1_8822B) & BIT_MASK_QUEUEAC_Q2_V1_8822B)
#define BIT_SET_QUEUEAC_Q2_V1_8822B(x, v)                                      \
	(BIT_CLEAR_QUEUEAC_Q2_V1_8822B(x) | BIT_QUEUEAC_Q2_V1_8822B(v))

#define BIT_TIDEMPTY_Q2_V1_8822B BIT(22)

#define BIT_SHIFT_TAIL_PKT_Q2_V2_8822B 11
#define BIT_MASK_TAIL_PKT_Q2_V2_8822B 0x7ff
#define BIT_TAIL_PKT_Q2_V2_8822B(x)                                            \
	(((x) & BIT_MASK_TAIL_PKT_Q2_V2_8822B)                                 \
	 << BIT_SHIFT_TAIL_PKT_Q2_V2_8822B)
#define BITS_TAIL_PKT_Q2_V2_8822B                                              \
	(BIT_MASK_TAIL_PKT_Q2_V2_8822B << BIT_SHIFT_TAIL_PKT_Q2_V2_8822B)
#define BIT_CLEAR_TAIL_PKT_Q2_V2_8822B(x) ((x) & (~BITS_TAIL_PKT_Q2_V2_8822B))
#define BIT_GET_TAIL_PKT_Q2_V2_8822B(x)                                        \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q2_V2_8822B) &                             \
	 BIT_MASK_TAIL_PKT_Q2_V2_8822B)
#define BIT_SET_TAIL_PKT_Q2_V2_8822B(x, v)                                     \
	(BIT_CLEAR_TAIL_PKT_Q2_V2_8822B(x) | BIT_TAIL_PKT_Q2_V2_8822B(v))

#define BIT_SHIFT_HEAD_PKT_Q2_V1_8822B 0
#define BIT_MASK_HEAD_PKT_Q2_V1_8822B 0x7ff
#define BIT_HEAD_PKT_Q2_V1_8822B(x)                                            \
	(((x) & BIT_MASK_HEAD_PKT_Q2_V1_8822B)                                 \
	 << BIT_SHIFT_HEAD_PKT_Q2_V1_8822B)
#define BITS_HEAD_PKT_Q2_V1_8822B                                              \
	(BIT_MASK_HEAD_PKT_Q2_V1_8822B << BIT_SHIFT_HEAD_PKT_Q2_V1_8822B)
#define BIT_CLEAR_HEAD_PKT_Q2_V1_8822B(x) ((x) & (~BITS_HEAD_PKT_Q2_V1_8822B))
#define BIT_GET_HEAD_PKT_Q2_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q2_V1_8822B) &                             \
	 BIT_MASK_HEAD_PKT_Q2_V1_8822B)
#define BIT_SET_HEAD_PKT_Q2_V1_8822B(x, v)                                     \
	(BIT_CLEAR_HEAD_PKT_Q2_V1_8822B(x) | BIT_HEAD_PKT_Q2_V1_8822B(v))

/* 2 REG_Q3_INFO_8822B */

#define BIT_SHIFT_QUEUEMACID_Q3_V1_8822B 25
#define BIT_MASK_QUEUEMACID_Q3_V1_8822B 0x7f
#define BIT_QUEUEMACID_Q3_V1_8822B(x)                                          \
	(((x) & BIT_MASK_QUEUEMACID_Q3_V1_8822B)                               \
	 << BIT_SHIFT_QUEUEMACID_Q3_V1_8822B)
#define BITS_QUEUEMACID_Q3_V1_8822B                                            \
	(BIT_MASK_QUEUEMACID_Q3_V1_8822B << BIT_SHIFT_QUEUEMACID_Q3_V1_8822B)
#define BIT_CLEAR_QUEUEMACID_Q3_V1_8822B(x)                                    \
	((x) & (~BITS_QUEUEMACID_Q3_V1_8822B))
#define BIT_GET_QUEUEMACID_Q3_V1_8822B(x)                                      \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q3_V1_8822B) &                           \
	 BIT_MASK_QUEUEMACID_Q3_V1_8822B)
#define BIT_SET_QUEUEMACID_Q3_V1_8822B(x, v)                                   \
	(BIT_CLEAR_QUEUEMACID_Q3_V1_8822B(x) | BIT_QUEUEMACID_Q3_V1_8822B(v))

#define BIT_SHIFT_QUEUEAC_Q3_V1_8822B 23
#define BIT_MASK_QUEUEAC_Q3_V1_8822B 0x3
#define BIT_QUEUEAC_Q3_V1_8822B(x)                                             \
	(((x) & BIT_MASK_QUEUEAC_Q3_V1_8822B) << BIT_SHIFT_QUEUEAC_Q3_V1_8822B)
#define BITS_QUEUEAC_Q3_V1_8822B                                               \
	(BIT_MASK_QUEUEAC_Q3_V1_8822B << BIT_SHIFT_QUEUEAC_Q3_V1_8822B)
#define BIT_CLEAR_QUEUEAC_Q3_V1_8822B(x) ((x) & (~BITS_QUEUEAC_Q3_V1_8822B))
#define BIT_GET_QUEUEAC_Q3_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_QUEUEAC_Q3_V1_8822B) & BIT_MASK_QUEUEAC_Q3_V1_8822B)
#define BIT_SET_QUEUEAC_Q3_V1_8822B(x, v)                                      \
	(BIT_CLEAR_QUEUEAC_Q3_V1_8822B(x) | BIT_QUEUEAC_Q3_V1_8822B(v))

#define BIT_TIDEMPTY_Q3_V1_8822B BIT(22)

#define BIT_SHIFT_TAIL_PKT_Q3_V2_8822B 11
#define BIT_MASK_TAIL_PKT_Q3_V2_8822B 0x7ff
#define BIT_TAIL_PKT_Q3_V2_8822B(x)                                            \
	(((x) & BIT_MASK_TAIL_PKT_Q3_V2_8822B)                                 \
	 << BIT_SHIFT_TAIL_PKT_Q3_V2_8822B)
#define BITS_TAIL_PKT_Q3_V2_8822B                                              \
	(BIT_MASK_TAIL_PKT_Q3_V2_8822B << BIT_SHIFT_TAIL_PKT_Q3_V2_8822B)
#define BIT_CLEAR_TAIL_PKT_Q3_V2_8822B(x) ((x) & (~BITS_TAIL_PKT_Q3_V2_8822B))
#define BIT_GET_TAIL_PKT_Q3_V2_8822B(x)                                        \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q3_V2_8822B) &                             \
	 BIT_MASK_TAIL_PKT_Q3_V2_8822B)
#define BIT_SET_TAIL_PKT_Q3_V2_8822B(x, v)                                     \
	(BIT_CLEAR_TAIL_PKT_Q3_V2_8822B(x) | BIT_TAIL_PKT_Q3_V2_8822B(v))

#define BIT_SHIFT_HEAD_PKT_Q3_V1_8822B 0
#define BIT_MASK_HEAD_PKT_Q3_V1_8822B 0x7ff
#define BIT_HEAD_PKT_Q3_V1_8822B(x)                                            \
	(((x) & BIT_MASK_HEAD_PKT_Q3_V1_8822B)                                 \
	 << BIT_SHIFT_HEAD_PKT_Q3_V1_8822B)
#define BITS_HEAD_PKT_Q3_V1_8822B                                              \
	(BIT_MASK_HEAD_PKT_Q3_V1_8822B << BIT_SHIFT_HEAD_PKT_Q3_V1_8822B)
#define BIT_CLEAR_HEAD_PKT_Q3_V1_8822B(x) ((x) & (~BITS_HEAD_PKT_Q3_V1_8822B))
#define BIT_GET_HEAD_PKT_Q3_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q3_V1_8822B) &                             \
	 BIT_MASK_HEAD_PKT_Q3_V1_8822B)
#define BIT_SET_HEAD_PKT_Q3_V1_8822B(x, v)                                     \
	(BIT_CLEAR_HEAD_PKT_Q3_V1_8822B(x) | BIT_HEAD_PKT_Q3_V1_8822B(v))

/* 2 REG_MGQ_INFO_8822B */

#define BIT_SHIFT_QUEUEMACID_MGQ_V1_8822B 25
#define BIT_MASK_QUEUEMACID_MGQ_V1_8822B 0x7f
#define BIT_QUEUEMACID_MGQ_V1_8822B(x)                                         \
	(((x) & BIT_MASK_QUEUEMACID_MGQ_V1_8822B)                              \
	 << BIT_SHIFT_QUEUEMACID_MGQ_V1_8822B)
#define BITS_QUEUEMACID_MGQ_V1_8822B                                           \
	(BIT_MASK_QUEUEMACID_MGQ_V1_8822B << BIT_SHIFT_QUEUEMACID_MGQ_V1_8822B)
#define BIT_CLEAR_QUEUEMACID_MGQ_V1_8822B(x)                                   \
	((x) & (~BITS_QUEUEMACID_MGQ_V1_8822B))
#define BIT_GET_QUEUEMACID_MGQ_V1_8822B(x)                                     \
	(((x) >> BIT_SHIFT_QUEUEMACID_MGQ_V1_8822B) &                          \
	 BIT_MASK_QUEUEMACID_MGQ_V1_8822B)
#define BIT_SET_QUEUEMACID_MGQ_V1_8822B(x, v)                                  \
	(BIT_CLEAR_QUEUEMACID_MGQ_V1_8822B(x) | BIT_QUEUEMACID_MGQ_V1_8822B(v))

#define BIT_SHIFT_QUEUEAC_MGQ_V1_8822B 23
#define BIT_MASK_QUEUEAC_MGQ_V1_8822B 0x3
#define BIT_QUEUEAC_MGQ_V1_8822B(x)                                            \
	(((x) & BIT_MASK_QUEUEAC_MGQ_V1_8822B)                                 \
	 << BIT_SHIFT_QUEUEAC_MGQ_V1_8822B)
#define BITS_QUEUEAC_MGQ_V1_8822B                                              \
	(BIT_MASK_QUEUEAC_MGQ_V1_8822B << BIT_SHIFT_QUEUEAC_MGQ_V1_8822B)
#define BIT_CLEAR_QUEUEAC_MGQ_V1_8822B(x) ((x) & (~BITS_QUEUEAC_MGQ_V1_8822B))
#define BIT_GET_QUEUEAC_MGQ_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_QUEUEAC_MGQ_V1_8822B) &                             \
	 BIT_MASK_QUEUEAC_MGQ_V1_8822B)
#define BIT_SET_QUEUEAC_MGQ_V1_8822B(x, v)                                     \
	(BIT_CLEAR_QUEUEAC_MGQ_V1_8822B(x) | BIT_QUEUEAC_MGQ_V1_8822B(v))

#define BIT_TIDEMPTY_MGQ_V1_8822B BIT(22)

#define BIT_SHIFT_TAIL_PKT_MGQ_V2_8822B 11
#define BIT_MASK_TAIL_PKT_MGQ_V2_8822B 0x7ff
#define BIT_TAIL_PKT_MGQ_V2_8822B(x)                                           \
	(((x) & BIT_MASK_TAIL_PKT_MGQ_V2_8822B)                                \
	 << BIT_SHIFT_TAIL_PKT_MGQ_V2_8822B)
#define BITS_TAIL_PKT_MGQ_V2_8822B                                             \
	(BIT_MASK_TAIL_PKT_MGQ_V2_8822B << BIT_SHIFT_TAIL_PKT_MGQ_V2_8822B)
#define BIT_CLEAR_TAIL_PKT_MGQ_V2_8822B(x) ((x) & (~BITS_TAIL_PKT_MGQ_V2_8822B))
#define BIT_GET_TAIL_PKT_MGQ_V2_8822B(x)                                       \
	(((x) >> BIT_SHIFT_TAIL_PKT_MGQ_V2_8822B) &                            \
	 BIT_MASK_TAIL_PKT_MGQ_V2_8822B)
#define BIT_SET_TAIL_PKT_MGQ_V2_8822B(x, v)                                    \
	(BIT_CLEAR_TAIL_PKT_MGQ_V2_8822B(x) | BIT_TAIL_PKT_MGQ_V2_8822B(v))

#define BIT_SHIFT_HEAD_PKT_MGQ_V1_8822B 0
#define BIT_MASK_HEAD_PKT_MGQ_V1_8822B 0x7ff
#define BIT_HEAD_PKT_MGQ_V1_8822B(x)                                           \
	(((x) & BIT_MASK_HEAD_PKT_MGQ_V1_8822B)                                \
	 << BIT_SHIFT_HEAD_PKT_MGQ_V1_8822B)
#define BITS_HEAD_PKT_MGQ_V1_8822B                                             \
	(BIT_MASK_HEAD_PKT_MGQ_V1_8822B << BIT_SHIFT_HEAD_PKT_MGQ_V1_8822B)
#define BIT_CLEAR_HEAD_PKT_MGQ_V1_8822B(x) ((x) & (~BITS_HEAD_PKT_MGQ_V1_8822B))
#define BIT_GET_HEAD_PKT_MGQ_V1_8822B(x)                                       \
	(((x) >> BIT_SHIFT_HEAD_PKT_MGQ_V1_8822B) &                            \
	 BIT_MASK_HEAD_PKT_MGQ_V1_8822B)
#define BIT_SET_HEAD_PKT_MGQ_V1_8822B(x, v)                                    \
	(BIT_CLEAR_HEAD_PKT_MGQ_V1_8822B(x) | BIT_HEAD_PKT_MGQ_V1_8822B(v))

/* 2 REG_HIQ_INFO_8822B */

#define BIT_SHIFT_QUEUEMACID_HIQ_V1_8822B 25
#define BIT_MASK_QUEUEMACID_HIQ_V1_8822B 0x7f
#define BIT_QUEUEMACID_HIQ_V1_8822B(x)                                         \
	(((x) & BIT_MASK_QUEUEMACID_HIQ_V1_8822B)                              \
	 << BIT_SHIFT_QUEUEMACID_HIQ_V1_8822B)
#define BITS_QUEUEMACID_HIQ_V1_8822B                                           \
	(BIT_MASK_QUEUEMACID_HIQ_V1_8822B << BIT_SHIFT_QUEUEMACID_HIQ_V1_8822B)
#define BIT_CLEAR_QUEUEMACID_HIQ_V1_8822B(x)                                   \
	((x) & (~BITS_QUEUEMACID_HIQ_V1_8822B))
#define BIT_GET_QUEUEMACID_HIQ_V1_8822B(x)                                     \
	(((x) >> BIT_SHIFT_QUEUEMACID_HIQ_V1_8822B) &                          \
	 BIT_MASK_QUEUEMACID_HIQ_V1_8822B)
#define BIT_SET_QUEUEMACID_HIQ_V1_8822B(x, v)                                  \
	(BIT_CLEAR_QUEUEMACID_HIQ_V1_8822B(x) | BIT_QUEUEMACID_HIQ_V1_8822B(v))

#define BIT_SHIFT_QUEUEAC_HIQ_V1_8822B 23
#define BIT_MASK_QUEUEAC_HIQ_V1_8822B 0x3
#define BIT_QUEUEAC_HIQ_V1_8822B(x)                                            \
	(((x) & BIT_MASK_QUEUEAC_HIQ_V1_8822B)                                 \
	 << BIT_SHIFT_QUEUEAC_HIQ_V1_8822B)
#define BITS_QUEUEAC_HIQ_V1_8822B                                              \
	(BIT_MASK_QUEUEAC_HIQ_V1_8822B << BIT_SHIFT_QUEUEAC_HIQ_V1_8822B)
#define BIT_CLEAR_QUEUEAC_HIQ_V1_8822B(x) ((x) & (~BITS_QUEUEAC_HIQ_V1_8822B))
#define BIT_GET_QUEUEAC_HIQ_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_QUEUEAC_HIQ_V1_8822B) &                             \
	 BIT_MASK_QUEUEAC_HIQ_V1_8822B)
#define BIT_SET_QUEUEAC_HIQ_V1_8822B(x, v)                                     \
	(BIT_CLEAR_QUEUEAC_HIQ_V1_8822B(x) | BIT_QUEUEAC_HIQ_V1_8822B(v))

#define BIT_TIDEMPTY_HIQ_V1_8822B BIT(22)

#define BIT_SHIFT_TAIL_PKT_HIQ_V2_8822B 11
#define BIT_MASK_TAIL_PKT_HIQ_V2_8822B 0x7ff
#define BIT_TAIL_PKT_HIQ_V2_8822B(x)                                           \
	(((x) & BIT_MASK_TAIL_PKT_HIQ_V2_8822B)                                \
	 << BIT_SHIFT_TAIL_PKT_HIQ_V2_8822B)
#define BITS_TAIL_PKT_HIQ_V2_8822B                                             \
	(BIT_MASK_TAIL_PKT_HIQ_V2_8822B << BIT_SHIFT_TAIL_PKT_HIQ_V2_8822B)
#define BIT_CLEAR_TAIL_PKT_HIQ_V2_8822B(x) ((x) & (~BITS_TAIL_PKT_HIQ_V2_8822B))
#define BIT_GET_TAIL_PKT_HIQ_V2_8822B(x)                                       \
	(((x) >> BIT_SHIFT_TAIL_PKT_HIQ_V2_8822B) &                            \
	 BIT_MASK_TAIL_PKT_HIQ_V2_8822B)
#define BIT_SET_TAIL_PKT_HIQ_V2_8822B(x, v)                                    \
	(BIT_CLEAR_TAIL_PKT_HIQ_V2_8822B(x) | BIT_TAIL_PKT_HIQ_V2_8822B(v))

#define BIT_SHIFT_HEAD_PKT_HIQ_V1_8822B 0
#define BIT_MASK_HEAD_PKT_HIQ_V1_8822B 0x7ff
#define BIT_HEAD_PKT_HIQ_V1_8822B(x)                                           \
	(((x) & BIT_MASK_HEAD_PKT_HIQ_V1_8822B)                                \
	 << BIT_SHIFT_HEAD_PKT_HIQ_V1_8822B)
#define BITS_HEAD_PKT_HIQ_V1_8822B                                             \
	(BIT_MASK_HEAD_PKT_HIQ_V1_8822B << BIT_SHIFT_HEAD_PKT_HIQ_V1_8822B)
#define BIT_CLEAR_HEAD_PKT_HIQ_V1_8822B(x) ((x) & (~BITS_HEAD_PKT_HIQ_V1_8822B))
#define BIT_GET_HEAD_PKT_HIQ_V1_8822B(x)                                       \
	(((x) >> BIT_SHIFT_HEAD_PKT_HIQ_V1_8822B) &                            \
	 BIT_MASK_HEAD_PKT_HIQ_V1_8822B)
#define BIT_SET_HEAD_PKT_HIQ_V1_8822B(x, v)                                    \
	(BIT_CLEAR_HEAD_PKT_HIQ_V1_8822B(x) | BIT_HEAD_PKT_HIQ_V1_8822B(v))

/* 2 REG_BCNQ_INFO_8822B */

#define BIT_SHIFT_BCNQ_HEAD_PG_V1_8822B 0
#define BIT_MASK_BCNQ_HEAD_PG_V1_8822B 0xfff
#define BIT_BCNQ_HEAD_PG_V1_8822B(x)                                           \
	(((x) & BIT_MASK_BCNQ_HEAD_PG_V1_8822B)                                \
	 << BIT_SHIFT_BCNQ_HEAD_PG_V1_8822B)
#define BITS_BCNQ_HEAD_PG_V1_8822B                                             \
	(BIT_MASK_BCNQ_HEAD_PG_V1_8822B << BIT_SHIFT_BCNQ_HEAD_PG_V1_8822B)
#define BIT_CLEAR_BCNQ_HEAD_PG_V1_8822B(x) ((x) & (~BITS_BCNQ_HEAD_PG_V1_8822B))
#define BIT_GET_BCNQ_HEAD_PG_V1_8822B(x)                                       \
	(((x) >> BIT_SHIFT_BCNQ_HEAD_PG_V1_8822B) &                            \
	 BIT_MASK_BCNQ_HEAD_PG_V1_8822B)
#define BIT_SET_BCNQ_HEAD_PG_V1_8822B(x, v)                                    \
	(BIT_CLEAR_BCNQ_HEAD_PG_V1_8822B(x) | BIT_BCNQ_HEAD_PG_V1_8822B(v))

/* 2 REG_TXPKT_EMPTY_8822B */
#define BIT_BCNQ_EMPTY_8822B BIT(11)
#define BIT_HQQ_EMPTY_8822B BIT(10)
#define BIT_MQQ_EMPTY_8822B BIT(9)
#define BIT_MGQ_CPU_EMPTY_8822B BIT(8)
#define BIT_AC7Q_EMPTY_8822B BIT(7)
#define BIT_AC6Q_EMPTY_8822B BIT(6)
#define BIT_AC5Q_EMPTY_8822B BIT(5)
#define BIT_AC4Q_EMPTY_8822B BIT(4)
#define BIT_AC3Q_EMPTY_8822B BIT(3)
#define BIT_AC2Q_EMPTY_8822B BIT(2)
#define BIT_AC1Q_EMPTY_8822B BIT(1)
#define BIT_AC0Q_EMPTY_8822B BIT(0)

/* 2 REG_CPU_MGQ_INFO_8822B */
#define BIT_BCN1_POLL_8822B BIT(30)
#define BIT_CPUMGT_POLL_8822B BIT(29)
#define BIT_BCN_POLL_8822B BIT(28)
#define BIT_CPUMGQ_FW_NUM_V1_8822B BIT(12)

#define BIT_SHIFT_FW_FREE_TAIL_V1_8822B 0
#define BIT_MASK_FW_FREE_TAIL_V1_8822B 0xfff
#define BIT_FW_FREE_TAIL_V1_8822B(x)                                           \
	(((x) & BIT_MASK_FW_FREE_TAIL_V1_8822B)                                \
	 << BIT_SHIFT_FW_FREE_TAIL_V1_8822B)
#define BITS_FW_FREE_TAIL_V1_8822B                                             \
	(BIT_MASK_FW_FREE_TAIL_V1_8822B << BIT_SHIFT_FW_FREE_TAIL_V1_8822B)
#define BIT_CLEAR_FW_FREE_TAIL_V1_8822B(x) ((x) & (~BITS_FW_FREE_TAIL_V1_8822B))
#define BIT_GET_FW_FREE_TAIL_V1_8822B(x)                                       \
	(((x) >> BIT_SHIFT_FW_FREE_TAIL_V1_8822B) &                            \
	 BIT_MASK_FW_FREE_TAIL_V1_8822B)
#define BIT_SET_FW_FREE_TAIL_V1_8822B(x, v)                                    \
	(BIT_CLEAR_FW_FREE_TAIL_V1_8822B(x) | BIT_FW_FREE_TAIL_V1_8822B(v))

/* 2 REG_FWHW_TXQ_CTRL_8822B */
#define BIT_RTS_LIMIT_IN_OFDM_8822B BIT(23)
#define BIT_EN_BCNQ_DL_8822B BIT(22)
#define BIT_EN_RD_RESP_NAV_BK_8822B BIT(21)
#define BIT_EN_WR_FREE_TAIL_8822B BIT(20)

#define BIT_SHIFT_EN_QUEUE_RPT_8822B 8
#define BIT_MASK_EN_QUEUE_RPT_8822B 0xff
#define BIT_EN_QUEUE_RPT_8822B(x)                                              \
	(((x) & BIT_MASK_EN_QUEUE_RPT_8822B) << BIT_SHIFT_EN_QUEUE_RPT_8822B)
#define BITS_EN_QUEUE_RPT_8822B                                                \
	(BIT_MASK_EN_QUEUE_RPT_8822B << BIT_SHIFT_EN_QUEUE_RPT_8822B)
#define BIT_CLEAR_EN_QUEUE_RPT_8822B(x) ((x) & (~BITS_EN_QUEUE_RPT_8822B))
#define BIT_GET_EN_QUEUE_RPT_8822B(x)                                          \
	(((x) >> BIT_SHIFT_EN_QUEUE_RPT_8822B) & BIT_MASK_EN_QUEUE_RPT_8822B)
#define BIT_SET_EN_QUEUE_RPT_8822B(x, v)                                       \
	(BIT_CLEAR_EN_QUEUE_RPT_8822B(x) | BIT_EN_QUEUE_RPT_8822B(v))

#define BIT_EN_RTY_BK_8822B BIT(7)
#define BIT_EN_USE_INI_RAT_8822B BIT(6)
#define BIT_EN_RTS_NAV_BK_8822B BIT(5)
#define BIT_DIS_SSN_CHECK_8822B BIT(4)
#define BIT_MACID_MATCH_RTS_8822B BIT(3)
#define BIT_EN_BCN_TRXRPT_V1_8822B BIT(2)
#define BIT_EN_FTMACKRPT_8822B BIT(1)
#define BIT_EN_FTMRPT_8822B BIT(0)

/* 2 REG_DATAFB_SEL_8822B */

#define BIT_SHIFT__R_DATA_FALLBACK_SEL_8822B 0
#define BIT_MASK__R_DATA_FALLBACK_SEL_8822B 0x3
#define BIT__R_DATA_FALLBACK_SEL_8822B(x)                                      \
	(((x) & BIT_MASK__R_DATA_FALLBACK_SEL_8822B)                           \
	 << BIT_SHIFT__R_DATA_FALLBACK_SEL_8822B)
#define BITS__R_DATA_FALLBACK_SEL_8822B                                        \
	(BIT_MASK__R_DATA_FALLBACK_SEL_8822B                                   \
	 << BIT_SHIFT__R_DATA_FALLBACK_SEL_8822B)
#define BIT_CLEAR__R_DATA_FALLBACK_SEL_8822B(x)                                \
	((x) & (~BITS__R_DATA_FALLBACK_SEL_8822B))
#define BIT_GET__R_DATA_FALLBACK_SEL_8822B(x)                                  \
	(((x) >> BIT_SHIFT__R_DATA_FALLBACK_SEL_8822B) &                       \
	 BIT_MASK__R_DATA_FALLBACK_SEL_8822B)
#define BIT_SET__R_DATA_FALLBACK_SEL_8822B(x, v)                               \
	(BIT_CLEAR__R_DATA_FALLBACK_SEL_8822B(x) |                             \
	 BIT__R_DATA_FALLBACK_SEL_8822B(v))

/* 2 REG_BCNQ_BDNY_V1_8822B */

#define BIT_SHIFT_BCNQ_PGBNDY_V1_8822B 0
#define BIT_MASK_BCNQ_PGBNDY_V1_8822B 0xfff
#define BIT_BCNQ_PGBNDY_V1_8822B(x)                                            \
	(((x) & BIT_MASK_BCNQ_PGBNDY_V1_8822B)                                 \
	 << BIT_SHIFT_BCNQ_PGBNDY_V1_8822B)
#define BITS_BCNQ_PGBNDY_V1_8822B                                              \
	(BIT_MASK_BCNQ_PGBNDY_V1_8822B << BIT_SHIFT_BCNQ_PGBNDY_V1_8822B)
#define BIT_CLEAR_BCNQ_PGBNDY_V1_8822B(x) ((x) & (~BITS_BCNQ_PGBNDY_V1_8822B))
#define BIT_GET_BCNQ_PGBNDY_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_BCNQ_PGBNDY_V1_8822B) &                             \
	 BIT_MASK_BCNQ_PGBNDY_V1_8822B)
#define BIT_SET_BCNQ_PGBNDY_V1_8822B(x, v)                                     \
	(BIT_CLEAR_BCNQ_PGBNDY_V1_8822B(x) | BIT_BCNQ_PGBNDY_V1_8822B(v))

/* 2 REG_LIFETIME_EN_8822B */
#define BIT_BT_INT_CPU_8822B BIT(7)
#define BIT_BT_INT_PTA_8822B BIT(6)
#define BIT_EN_CTRL_RTYBIT_8822B BIT(4)
#define BIT_LIFETIME_BK_EN_8822B BIT(3)
#define BIT_LIFETIME_BE_EN_8822B BIT(2)
#define BIT_LIFETIME_VI_EN_8822B BIT(1)
#define BIT_LIFETIME_VO_EN_8822B BIT(0)

/* 2 REG_SPEC_SIFS_8822B */

#define BIT_SHIFT_SPEC_SIFS_OFDM_PTCL_8822B 8
#define BIT_MASK_SPEC_SIFS_OFDM_PTCL_8822B 0xff
#define BIT_SPEC_SIFS_OFDM_PTCL_8822B(x)                                       \
	(((x) & BIT_MASK_SPEC_SIFS_OFDM_PTCL_8822B)                            \
	 << BIT_SHIFT_SPEC_SIFS_OFDM_PTCL_8822B)
#define BITS_SPEC_SIFS_OFDM_PTCL_8822B                                         \
	(BIT_MASK_SPEC_SIFS_OFDM_PTCL_8822B                                    \
	 << BIT_SHIFT_SPEC_SIFS_OFDM_PTCL_8822B)
#define BIT_CLEAR_SPEC_SIFS_OFDM_PTCL_8822B(x)                                 \
	((x) & (~BITS_SPEC_SIFS_OFDM_PTCL_8822B))
#define BIT_GET_SPEC_SIFS_OFDM_PTCL_8822B(x)                                   \
	(((x) >> BIT_SHIFT_SPEC_SIFS_OFDM_PTCL_8822B) &                        \
	 BIT_MASK_SPEC_SIFS_OFDM_PTCL_8822B)
#define BIT_SET_SPEC_SIFS_OFDM_PTCL_8822B(x, v)                                \
	(BIT_CLEAR_SPEC_SIFS_OFDM_PTCL_8822B(x) |                              \
	 BIT_SPEC_SIFS_OFDM_PTCL_8822B(v))

#define BIT_SHIFT_SPEC_SIFS_CCK_PTCL_8822B 0
#define BIT_MASK_SPEC_SIFS_CCK_PTCL_8822B 0xff
#define BIT_SPEC_SIFS_CCK_PTCL_8822B(x)                                        \
	(((x) & BIT_MASK_SPEC_SIFS_CCK_PTCL_8822B)                             \
	 << BIT_SHIFT_SPEC_SIFS_CCK_PTCL_8822B)
#define BITS_SPEC_SIFS_CCK_PTCL_8822B                                          \
	(BIT_MASK_SPEC_SIFS_CCK_PTCL_8822B                                     \
	 << BIT_SHIFT_SPEC_SIFS_CCK_PTCL_8822B)
#define BIT_CLEAR_SPEC_SIFS_CCK_PTCL_8822B(x)                                  \
	((x) & (~BITS_SPEC_SIFS_CCK_PTCL_8822B))
#define BIT_GET_SPEC_SIFS_CCK_PTCL_8822B(x)                                    \
	(((x) >> BIT_SHIFT_SPEC_SIFS_CCK_PTCL_8822B) &                         \
	 BIT_MASK_SPEC_SIFS_CCK_PTCL_8822B)
#define BIT_SET_SPEC_SIFS_CCK_PTCL_8822B(x, v)                                 \
	(BIT_CLEAR_SPEC_SIFS_CCK_PTCL_8822B(x) |                               \
	 BIT_SPEC_SIFS_CCK_PTCL_8822B(v))

/* 2 REG_RETRY_LIMIT_8822B */

#define BIT_SHIFT_SRL_8822B 8
#define BIT_MASK_SRL_8822B 0x3f
#define BIT_SRL_8822B(x) (((x) & BIT_MASK_SRL_8822B) << BIT_SHIFT_SRL_8822B)
#define BITS_SRL_8822B (BIT_MASK_SRL_8822B << BIT_SHIFT_SRL_8822B)
#define BIT_CLEAR_SRL_8822B(x) ((x) & (~BITS_SRL_8822B))
#define BIT_GET_SRL_8822B(x) (((x) >> BIT_SHIFT_SRL_8822B) & BIT_MASK_SRL_8822B)
#define BIT_SET_SRL_8822B(x, v) (BIT_CLEAR_SRL_8822B(x) | BIT_SRL_8822B(v))

#define BIT_SHIFT_LRL_8822B 0
#define BIT_MASK_LRL_8822B 0x3f
#define BIT_LRL_8822B(x) (((x) & BIT_MASK_LRL_8822B) << BIT_SHIFT_LRL_8822B)
#define BITS_LRL_8822B (BIT_MASK_LRL_8822B << BIT_SHIFT_LRL_8822B)
#define BIT_CLEAR_LRL_8822B(x) ((x) & (~BITS_LRL_8822B))
#define BIT_GET_LRL_8822B(x) (((x) >> BIT_SHIFT_LRL_8822B) & BIT_MASK_LRL_8822B)
#define BIT_SET_LRL_8822B(x, v) (BIT_CLEAR_LRL_8822B(x) | BIT_LRL_8822B(v))

/* 2 REG_TXBF_CTRL_8822B */
#define BIT_R_ENABLE_NDPA_8822B BIT(31)
#define BIT_USE_NDPA_PARAMETER_8822B BIT(30)
#define BIT_R_PROP_TXBF_8822B BIT(29)
#define BIT_R_EN_NDPA_INT_8822B BIT(28)
#define BIT_R_TXBF1_80M_8822B BIT(27)
#define BIT_R_TXBF1_40M_8822B BIT(26)
#define BIT_R_TXBF1_20M_8822B BIT(25)

#define BIT_SHIFT_R_TXBF1_AID_8822B 16
#define BIT_MASK_R_TXBF1_AID_8822B 0x1ff
#define BIT_R_TXBF1_AID_8822B(x)                                               \
	(((x) & BIT_MASK_R_TXBF1_AID_8822B) << BIT_SHIFT_R_TXBF1_AID_8822B)
#define BITS_R_TXBF1_AID_8822B                                                 \
	(BIT_MASK_R_TXBF1_AID_8822B << BIT_SHIFT_R_TXBF1_AID_8822B)
#define BIT_CLEAR_R_TXBF1_AID_8822B(x) ((x) & (~BITS_R_TXBF1_AID_8822B))
#define BIT_GET_R_TXBF1_AID_8822B(x)                                           \
	(((x) >> BIT_SHIFT_R_TXBF1_AID_8822B) & BIT_MASK_R_TXBF1_AID_8822B)
#define BIT_SET_R_TXBF1_AID_8822B(x, v)                                        \
	(BIT_CLEAR_R_TXBF1_AID_8822B(x) | BIT_R_TXBF1_AID_8822B(v))

#define BIT_DIS_NDP_BFEN_8822B BIT(15)
#define BIT_R_TXBCN_NOBLOCK_NDP_8822B BIT(14)
#define BIT_R_TXBF0_80M_8822B BIT(11)
#define BIT_R_TXBF0_40M_8822B BIT(10)
#define BIT_R_TXBF0_20M_8822B BIT(9)

#define BIT_SHIFT_R_TXBF0_AID_8822B 0
#define BIT_MASK_R_TXBF0_AID_8822B 0x1ff
#define BIT_R_TXBF0_AID_8822B(x)                                               \
	(((x) & BIT_MASK_R_TXBF0_AID_8822B) << BIT_SHIFT_R_TXBF0_AID_8822B)
#define BITS_R_TXBF0_AID_8822B                                                 \
	(BIT_MASK_R_TXBF0_AID_8822B << BIT_SHIFT_R_TXBF0_AID_8822B)
#define BIT_CLEAR_R_TXBF0_AID_8822B(x) ((x) & (~BITS_R_TXBF0_AID_8822B))
#define BIT_GET_R_TXBF0_AID_8822B(x)                                           \
	(((x) >> BIT_SHIFT_R_TXBF0_AID_8822B) & BIT_MASK_R_TXBF0_AID_8822B)
#define BIT_SET_R_TXBF0_AID_8822B(x, v)                                        \
	(BIT_CLEAR_R_TXBF0_AID_8822B(x) | BIT_R_TXBF0_AID_8822B(v))

/* 2 REG_DARFRC_8822B */

#define BIT_SHIFT_DARF_RC8_8822B (56 & CPU_OPT_WIDTH)
#define BIT_MASK_DARF_RC8_8822B 0x1f
#define BIT_DARF_RC8_8822B(x)                                                  \
	(((x) & BIT_MASK_DARF_RC8_8822B) << BIT_SHIFT_DARF_RC8_8822B)
#define BITS_DARF_RC8_8822B                                                    \
	(BIT_MASK_DARF_RC8_8822B << BIT_SHIFT_DARF_RC8_8822B)
#define BIT_CLEAR_DARF_RC8_8822B(x) ((x) & (~BITS_DARF_RC8_8822B))
#define BIT_GET_DARF_RC8_8822B(x)                                              \
	(((x) >> BIT_SHIFT_DARF_RC8_8822B) & BIT_MASK_DARF_RC8_8822B)
#define BIT_SET_DARF_RC8_8822B(x, v)                                           \
	(BIT_CLEAR_DARF_RC8_8822B(x) | BIT_DARF_RC8_8822B(v))

#define BIT_SHIFT_DARF_RC7_8822B (48 & CPU_OPT_WIDTH)
#define BIT_MASK_DARF_RC7_8822B 0x1f
#define BIT_DARF_RC7_8822B(x)                                                  \
	(((x) & BIT_MASK_DARF_RC7_8822B) << BIT_SHIFT_DARF_RC7_8822B)
#define BITS_DARF_RC7_8822B                                                    \
	(BIT_MASK_DARF_RC7_8822B << BIT_SHIFT_DARF_RC7_8822B)
#define BIT_CLEAR_DARF_RC7_8822B(x) ((x) & (~BITS_DARF_RC7_8822B))
#define BIT_GET_DARF_RC7_8822B(x)                                              \
	(((x) >> BIT_SHIFT_DARF_RC7_8822B) & BIT_MASK_DARF_RC7_8822B)
#define BIT_SET_DARF_RC7_8822B(x, v)                                           \
	(BIT_CLEAR_DARF_RC7_8822B(x) | BIT_DARF_RC7_8822B(v))

#define BIT_SHIFT_DARF_RC6_8822B (40 & CPU_OPT_WIDTH)
#define BIT_MASK_DARF_RC6_8822B 0x1f
#define BIT_DARF_RC6_8822B(x)                                                  \
	(((x) & BIT_MASK_DARF_RC6_8822B) << BIT_SHIFT_DARF_RC6_8822B)
#define BITS_DARF_RC6_8822B                                                    \
	(BIT_MASK_DARF_RC6_8822B << BIT_SHIFT_DARF_RC6_8822B)
#define BIT_CLEAR_DARF_RC6_8822B(x) ((x) & (~BITS_DARF_RC6_8822B))
#define BIT_GET_DARF_RC6_8822B(x)                                              \
	(((x) >> BIT_SHIFT_DARF_RC6_8822B) & BIT_MASK_DARF_RC6_8822B)
#define BIT_SET_DARF_RC6_8822B(x, v)                                           \
	(BIT_CLEAR_DARF_RC6_8822B(x) | BIT_DARF_RC6_8822B(v))

#define BIT_SHIFT_DARF_RC5_8822B (32 & CPU_OPT_WIDTH)
#define BIT_MASK_DARF_RC5_8822B 0x1f
#define BIT_DARF_RC5_8822B(x)                                                  \
	(((x) & BIT_MASK_DARF_RC5_8822B) << BIT_SHIFT_DARF_RC5_8822B)
#define BITS_DARF_RC5_8822B                                                    \
	(BIT_MASK_DARF_RC5_8822B << BIT_SHIFT_DARF_RC5_8822B)
#define BIT_CLEAR_DARF_RC5_8822B(x) ((x) & (~BITS_DARF_RC5_8822B))
#define BIT_GET_DARF_RC5_8822B(x)                                              \
	(((x) >> BIT_SHIFT_DARF_RC5_8822B) & BIT_MASK_DARF_RC5_8822B)
#define BIT_SET_DARF_RC5_8822B(x, v)                                           \
	(BIT_CLEAR_DARF_RC5_8822B(x) | BIT_DARF_RC5_8822B(v))

#define BIT_SHIFT_DARF_RC4_8822B 24
#define BIT_MASK_DARF_RC4_8822B 0x1f
#define BIT_DARF_RC4_8822B(x)                                                  \
	(((x) & BIT_MASK_DARF_RC4_8822B) << BIT_SHIFT_DARF_RC4_8822B)
#define BITS_DARF_RC4_8822B                                                    \
	(BIT_MASK_DARF_RC4_8822B << BIT_SHIFT_DARF_RC4_8822B)
#define BIT_CLEAR_DARF_RC4_8822B(x) ((x) & (~BITS_DARF_RC4_8822B))
#define BIT_GET_DARF_RC4_8822B(x)                                              \
	(((x) >> BIT_SHIFT_DARF_RC4_8822B) & BIT_MASK_DARF_RC4_8822B)
#define BIT_SET_DARF_RC4_8822B(x, v)                                           \
	(BIT_CLEAR_DARF_RC4_8822B(x) | BIT_DARF_RC4_8822B(v))

#define BIT_SHIFT_DARF_RC3_8822B 16
#define BIT_MASK_DARF_RC3_8822B 0x1f
#define BIT_DARF_RC3_8822B(x)                                                  \
	(((x) & BIT_MASK_DARF_RC3_8822B) << BIT_SHIFT_DARF_RC3_8822B)
#define BITS_DARF_RC3_8822B                                                    \
	(BIT_MASK_DARF_RC3_8822B << BIT_SHIFT_DARF_RC3_8822B)
#define BIT_CLEAR_DARF_RC3_8822B(x) ((x) & (~BITS_DARF_RC3_8822B))
#define BIT_GET_DARF_RC3_8822B(x)                                              \
	(((x) >> BIT_SHIFT_DARF_RC3_8822B) & BIT_MASK_DARF_RC3_8822B)
#define BIT_SET_DARF_RC3_8822B(x, v)                                           \
	(BIT_CLEAR_DARF_RC3_8822B(x) | BIT_DARF_RC3_8822B(v))

#define BIT_SHIFT_DARF_RC2_8822B 8
#define BIT_MASK_DARF_RC2_8822B 0x1f
#define BIT_DARF_RC2_8822B(x)                                                  \
	(((x) & BIT_MASK_DARF_RC2_8822B) << BIT_SHIFT_DARF_RC2_8822B)
#define BITS_DARF_RC2_8822B                                                    \
	(BIT_MASK_DARF_RC2_8822B << BIT_SHIFT_DARF_RC2_8822B)
#define BIT_CLEAR_DARF_RC2_8822B(x) ((x) & (~BITS_DARF_RC2_8822B))
#define BIT_GET_DARF_RC2_8822B(x)                                              \
	(((x) >> BIT_SHIFT_DARF_RC2_8822B) & BIT_MASK_DARF_RC2_8822B)
#define BIT_SET_DARF_RC2_8822B(x, v)                                           \
	(BIT_CLEAR_DARF_RC2_8822B(x) | BIT_DARF_RC2_8822B(v))

#define BIT_SHIFT_DARF_RC1_8822B 0
#define BIT_MASK_DARF_RC1_8822B 0x1f
#define BIT_DARF_RC1_8822B(x)                                                  \
	(((x) & BIT_MASK_DARF_RC1_8822B) << BIT_SHIFT_DARF_RC1_8822B)
#define BITS_DARF_RC1_8822B                                                    \
	(BIT_MASK_DARF_RC1_8822B << BIT_SHIFT_DARF_RC1_8822B)
#define BIT_CLEAR_DARF_RC1_8822B(x) ((x) & (~BITS_DARF_RC1_8822B))
#define BIT_GET_DARF_RC1_8822B(x)                                              \
	(((x) >> BIT_SHIFT_DARF_RC1_8822B) & BIT_MASK_DARF_RC1_8822B)
#define BIT_SET_DARF_RC1_8822B(x, v)                                           \
	(BIT_CLEAR_DARF_RC1_8822B(x) | BIT_DARF_RC1_8822B(v))

/* 2 REG_RARFRC_8822B */

#define BIT_SHIFT_RARF_RC8_8822B (56 & CPU_OPT_WIDTH)
#define BIT_MASK_RARF_RC8_8822B 0x1f
#define BIT_RARF_RC8_8822B(x)                                                  \
	(((x) & BIT_MASK_RARF_RC8_8822B) << BIT_SHIFT_RARF_RC8_8822B)
#define BITS_RARF_RC8_8822B                                                    \
	(BIT_MASK_RARF_RC8_8822B << BIT_SHIFT_RARF_RC8_8822B)
#define BIT_CLEAR_RARF_RC8_8822B(x) ((x) & (~BITS_RARF_RC8_8822B))
#define BIT_GET_RARF_RC8_8822B(x)                                              \
	(((x) >> BIT_SHIFT_RARF_RC8_8822B) & BIT_MASK_RARF_RC8_8822B)
#define BIT_SET_RARF_RC8_8822B(x, v)                                           \
	(BIT_CLEAR_RARF_RC8_8822B(x) | BIT_RARF_RC8_8822B(v))

#define BIT_SHIFT_RARF_RC7_8822B (48 & CPU_OPT_WIDTH)
#define BIT_MASK_RARF_RC7_8822B 0x1f
#define BIT_RARF_RC7_8822B(x)                                                  \
	(((x) & BIT_MASK_RARF_RC7_8822B) << BIT_SHIFT_RARF_RC7_8822B)
#define BITS_RARF_RC7_8822B                                                    \
	(BIT_MASK_RARF_RC7_8822B << BIT_SHIFT_RARF_RC7_8822B)
#define BIT_CLEAR_RARF_RC7_8822B(x) ((x) & (~BITS_RARF_RC7_8822B))
#define BIT_GET_RARF_RC7_8822B(x)                                              \
	(((x) >> BIT_SHIFT_RARF_RC7_8822B) & BIT_MASK_RARF_RC7_8822B)
#define BIT_SET_RARF_RC7_8822B(x, v)                                           \
	(BIT_CLEAR_RARF_RC7_8822B(x) | BIT_RARF_RC7_8822B(v))

#define BIT_SHIFT_RARF_RC6_8822B (40 & CPU_OPT_WIDTH)
#define BIT_MASK_RARF_RC6_8822B 0x1f
#define BIT_RARF_RC6_8822B(x)                                                  \
	(((x) & BIT_MASK_RARF_RC6_8822B) << BIT_SHIFT_RARF_RC6_8822B)
#define BITS_RARF_RC6_8822B                                                    \
	(BIT_MASK_RARF_RC6_8822B << BIT_SHIFT_RARF_RC6_8822B)
#define BIT_CLEAR_RARF_RC6_8822B(x) ((x) & (~BITS_RARF_RC6_8822B))
#define BIT_GET_RARF_RC6_8822B(x)                                              \
	(((x) >> BIT_SHIFT_RARF_RC6_8822B) & BIT_MASK_RARF_RC6_8822B)
#define BIT_SET_RARF_RC6_8822B(x, v)                                           \
	(BIT_CLEAR_RARF_RC6_8822B(x) | BIT_RARF_RC6_8822B(v))

#define BIT_SHIFT_RARF_RC5_8822B (32 & CPU_OPT_WIDTH)
#define BIT_MASK_RARF_RC5_8822B 0x1f
#define BIT_RARF_RC5_8822B(x)                                                  \
	(((x) & BIT_MASK_RARF_RC5_8822B) << BIT_SHIFT_RARF_RC5_8822B)
#define BITS_RARF_RC5_8822B                                                    \
	(BIT_MASK_RARF_RC5_8822B << BIT_SHIFT_RARF_RC5_8822B)
#define BIT_CLEAR_RARF_RC5_8822B(x) ((x) & (~BITS_RARF_RC5_8822B))
#define BIT_GET_RARF_RC5_8822B(x)                                              \
	(((x) >> BIT_SHIFT_RARF_RC5_8822B) & BIT_MASK_RARF_RC5_8822B)
#define BIT_SET_RARF_RC5_8822B(x, v)                                           \
	(BIT_CLEAR_RARF_RC5_8822B(x) | BIT_RARF_RC5_8822B(v))

#define BIT_SHIFT_RARF_RC4_8822B 24
#define BIT_MASK_RARF_RC4_8822B 0x1f
#define BIT_RARF_RC4_8822B(x)                                                  \
	(((x) & BIT_MASK_RARF_RC4_8822B) << BIT_SHIFT_RARF_RC4_8822B)
#define BITS_RARF_RC4_8822B                                                    \
	(BIT_MASK_RARF_RC4_8822B << BIT_SHIFT_RARF_RC4_8822B)
#define BIT_CLEAR_RARF_RC4_8822B(x) ((x) & (~BITS_RARF_RC4_8822B))
#define BIT_GET_RARF_RC4_8822B(x)                                              \
	(((x) >> BIT_SHIFT_RARF_RC4_8822B) & BIT_MASK_RARF_RC4_8822B)
#define BIT_SET_RARF_RC4_8822B(x, v)                                           \
	(BIT_CLEAR_RARF_RC4_8822B(x) | BIT_RARF_RC4_8822B(v))

#define BIT_SHIFT_RARF_RC3_8822B 16
#define BIT_MASK_RARF_RC3_8822B 0x1f
#define BIT_RARF_RC3_8822B(x)                                                  \
	(((x) & BIT_MASK_RARF_RC3_8822B) << BIT_SHIFT_RARF_RC3_8822B)
#define BITS_RARF_RC3_8822B                                                    \
	(BIT_MASK_RARF_RC3_8822B << BIT_SHIFT_RARF_RC3_8822B)
#define BIT_CLEAR_RARF_RC3_8822B(x) ((x) & (~BITS_RARF_RC3_8822B))
#define BIT_GET_RARF_RC3_8822B(x)                                              \
	(((x) >> BIT_SHIFT_RARF_RC3_8822B) & BIT_MASK_RARF_RC3_8822B)
#define BIT_SET_RARF_RC3_8822B(x, v)                                           \
	(BIT_CLEAR_RARF_RC3_8822B(x) | BIT_RARF_RC3_8822B(v))

#define BIT_SHIFT_RARF_RC2_8822B 8
#define BIT_MASK_RARF_RC2_8822B 0x1f
#define BIT_RARF_RC2_8822B(x)                                                  \
	(((x) & BIT_MASK_RARF_RC2_8822B) << BIT_SHIFT_RARF_RC2_8822B)
#define BITS_RARF_RC2_8822B                                                    \
	(BIT_MASK_RARF_RC2_8822B << BIT_SHIFT_RARF_RC2_8822B)
#define BIT_CLEAR_RARF_RC2_8822B(x) ((x) & (~BITS_RARF_RC2_8822B))
#define BIT_GET_RARF_RC2_8822B(x)                                              \
	(((x) >> BIT_SHIFT_RARF_RC2_8822B) & BIT_MASK_RARF_RC2_8822B)
#define BIT_SET_RARF_RC2_8822B(x, v)                                           \
	(BIT_CLEAR_RARF_RC2_8822B(x) | BIT_RARF_RC2_8822B(v))

#define BIT_SHIFT_RARF_RC1_8822B 0
#define BIT_MASK_RARF_RC1_8822B 0x1f
#define BIT_RARF_RC1_8822B(x)                                                  \
	(((x) & BIT_MASK_RARF_RC1_8822B) << BIT_SHIFT_RARF_RC1_8822B)
#define BITS_RARF_RC1_8822B                                                    \
	(BIT_MASK_RARF_RC1_8822B << BIT_SHIFT_RARF_RC1_8822B)
#define BIT_CLEAR_RARF_RC1_8822B(x) ((x) & (~BITS_RARF_RC1_8822B))
#define BIT_GET_RARF_RC1_8822B(x)                                              \
	(((x) >> BIT_SHIFT_RARF_RC1_8822B) & BIT_MASK_RARF_RC1_8822B)
#define BIT_SET_RARF_RC1_8822B(x, v)                                           \
	(BIT_CLEAR_RARF_RC1_8822B(x) | BIT_RARF_RC1_8822B(v))

/* 2 REG_RRSR_8822B */

#define BIT_SHIFT_RRSR_RSC_8822B 21
#define BIT_MASK_RRSR_RSC_8822B 0x3
#define BIT_RRSR_RSC_8822B(x)                                                  \
	(((x) & BIT_MASK_RRSR_RSC_8822B) << BIT_SHIFT_RRSR_RSC_8822B)
#define BITS_RRSR_RSC_8822B                                                    \
	(BIT_MASK_RRSR_RSC_8822B << BIT_SHIFT_RRSR_RSC_8822B)
#define BIT_CLEAR_RRSR_RSC_8822B(x) ((x) & (~BITS_RRSR_RSC_8822B))
#define BIT_GET_RRSR_RSC_8822B(x)                                              \
	(((x) >> BIT_SHIFT_RRSR_RSC_8822B) & BIT_MASK_RRSR_RSC_8822B)
#define BIT_SET_RRSR_RSC_8822B(x, v)                                           \
	(BIT_CLEAR_RRSR_RSC_8822B(x) | BIT_RRSR_RSC_8822B(v))

#define BIT_RRSR_BW_8822B BIT(20)

#define BIT_SHIFT_RRSC_BITMAP_8822B 0
#define BIT_MASK_RRSC_BITMAP_8822B 0xfffff
#define BIT_RRSC_BITMAP_8822B(x)                                               \
	(((x) & BIT_MASK_RRSC_BITMAP_8822B) << BIT_SHIFT_RRSC_BITMAP_8822B)
#define BITS_RRSC_BITMAP_8822B                                                 \
	(BIT_MASK_RRSC_BITMAP_8822B << BIT_SHIFT_RRSC_BITMAP_8822B)
#define BIT_CLEAR_RRSC_BITMAP_8822B(x) ((x) & (~BITS_RRSC_BITMAP_8822B))
#define BIT_GET_RRSC_BITMAP_8822B(x)                                           \
	(((x) >> BIT_SHIFT_RRSC_BITMAP_8822B) & BIT_MASK_RRSC_BITMAP_8822B)
#define BIT_SET_RRSC_BITMAP_8822B(x, v)                                        \
	(BIT_CLEAR_RRSC_BITMAP_8822B(x) | BIT_RRSC_BITMAP_8822B(v))

/* 2 REG_ARFR0_8822B */

#define BIT_SHIFT_ARFR0_V1_8822B 0
#define BIT_MASK_ARFR0_V1_8822B 0xffffffffffffffffL
#define BIT_ARFR0_V1_8822B(x)                                                  \
	(((x) & BIT_MASK_ARFR0_V1_8822B) << BIT_SHIFT_ARFR0_V1_8822B)
#define BITS_ARFR0_V1_8822B                                                    \
	(BIT_MASK_ARFR0_V1_8822B << BIT_SHIFT_ARFR0_V1_8822B)
#define BIT_CLEAR_ARFR0_V1_8822B(x) ((x) & (~BITS_ARFR0_V1_8822B))
#define BIT_GET_ARFR0_V1_8822B(x)                                              \
	(((x) >> BIT_SHIFT_ARFR0_V1_8822B) & BIT_MASK_ARFR0_V1_8822B)
#define BIT_SET_ARFR0_V1_8822B(x, v)                                           \
	(BIT_CLEAR_ARFR0_V1_8822B(x) | BIT_ARFR0_V1_8822B(v))

/* 2 REG_ARFR1_V1_8822B */

#define BIT_SHIFT_ARFR1_V1_8822B 0
#define BIT_MASK_ARFR1_V1_8822B 0xffffffffffffffffL
#define BIT_ARFR1_V1_8822B(x)                                                  \
	(((x) & BIT_MASK_ARFR1_V1_8822B) << BIT_SHIFT_ARFR1_V1_8822B)
#define BITS_ARFR1_V1_8822B                                                    \
	(BIT_MASK_ARFR1_V1_8822B << BIT_SHIFT_ARFR1_V1_8822B)
#define BIT_CLEAR_ARFR1_V1_8822B(x) ((x) & (~BITS_ARFR1_V1_8822B))
#define BIT_GET_ARFR1_V1_8822B(x)                                              \
	(((x) >> BIT_SHIFT_ARFR1_V1_8822B) & BIT_MASK_ARFR1_V1_8822B)
#define BIT_SET_ARFR1_V1_8822B(x, v)                                           \
	(BIT_CLEAR_ARFR1_V1_8822B(x) | BIT_ARFR1_V1_8822B(v))

/* 2 REG_CCK_CHECK_8822B */
#define BIT_CHECK_CCK_EN_8822B BIT(7)
#define BIT_EN_BCN_PKT_REL_8822B BIT(6)
#define BIT_BCN_PORT_SEL_8822B BIT(5)
#define BIT_MOREDATA_BYPASS_8822B BIT(4)
#define BIT_EN_CLR_CMD_REL_BCN_PKT_8822B BIT(3)
#define BIT_R_EN_SET_MOREDATA_8822B BIT(2)
#define BIT__R_DIS_CLEAR_MACID_RELEASE_8822B BIT(1)
#define BIT__R_MACID_RELEASE_EN_8822B BIT(0)

/* 2 REG_AMPDU_MAX_TIME_V1_8822B */

#define BIT_SHIFT_AMPDU_MAX_TIME_8822B 0
#define BIT_MASK_AMPDU_MAX_TIME_8822B 0xff
#define BIT_AMPDU_MAX_TIME_8822B(x)                                            \
	(((x) & BIT_MASK_AMPDU_MAX_TIME_8822B)                                 \
	 << BIT_SHIFT_AMPDU_MAX_TIME_8822B)
#define BITS_AMPDU_MAX_TIME_8822B                                              \
	(BIT_MASK_AMPDU_MAX_TIME_8822B << BIT_SHIFT_AMPDU_MAX_TIME_8822B)
#define BIT_CLEAR_AMPDU_MAX_TIME_8822B(x) ((x) & (~BITS_AMPDU_MAX_TIME_8822B))
#define BIT_GET_AMPDU_MAX_TIME_8822B(x)                                        \
	(((x) >> BIT_SHIFT_AMPDU_MAX_TIME_8822B) &                             \
	 BIT_MASK_AMPDU_MAX_TIME_8822B)
#define BIT_SET_AMPDU_MAX_TIME_8822B(x, v)                                     \
	(BIT_CLEAR_AMPDU_MAX_TIME_8822B(x) | BIT_AMPDU_MAX_TIME_8822B(v))

/* 2 REG_BCNQ1_BDNY_V1_8822B */

#define BIT_SHIFT_BCNQ1_PGBNDY_V1_8822B 0
#define BIT_MASK_BCNQ1_PGBNDY_V1_8822B 0xfff
#define BIT_BCNQ1_PGBNDY_V1_8822B(x)                                           \
	(((x) & BIT_MASK_BCNQ1_PGBNDY_V1_8822B)                                \
	 << BIT_SHIFT_BCNQ1_PGBNDY_V1_8822B)
#define BITS_BCNQ1_PGBNDY_V1_8822B                                             \
	(BIT_MASK_BCNQ1_PGBNDY_V1_8822B << BIT_SHIFT_BCNQ1_PGBNDY_V1_8822B)
#define BIT_CLEAR_BCNQ1_PGBNDY_V1_8822B(x) ((x) & (~BITS_BCNQ1_PGBNDY_V1_8822B))
#define BIT_GET_BCNQ1_PGBNDY_V1_8822B(x)                                       \
	(((x) >> BIT_SHIFT_BCNQ1_PGBNDY_V1_8822B) &                            \
	 BIT_MASK_BCNQ1_PGBNDY_V1_8822B)
#define BIT_SET_BCNQ1_PGBNDY_V1_8822B(x, v)                                    \
	(BIT_CLEAR_BCNQ1_PGBNDY_V1_8822B(x) | BIT_BCNQ1_PGBNDY_V1_8822B(v))

/* 2 REG_AMPDU_MAX_LENGTH_8822B */

#define BIT_SHIFT_AMPDU_MAX_LENGTH_8822B 0
#define BIT_MASK_AMPDU_MAX_LENGTH_8822B 0xffffffffL
#define BIT_AMPDU_MAX_LENGTH_8822B(x)                                          \
	(((x) & BIT_MASK_AMPDU_MAX_LENGTH_8822B)                               \
	 << BIT_SHIFT_AMPDU_MAX_LENGTH_8822B)
#define BITS_AMPDU_MAX_LENGTH_8822B                                            \
	(BIT_MASK_AMPDU_MAX_LENGTH_8822B << BIT_SHIFT_AMPDU_MAX_LENGTH_8822B)
#define BIT_CLEAR_AMPDU_MAX_LENGTH_8822B(x)                                    \
	((x) & (~BITS_AMPDU_MAX_LENGTH_8822B))
#define BIT_GET_AMPDU_MAX_LENGTH_8822B(x)                                      \
	(((x) >> BIT_SHIFT_AMPDU_MAX_LENGTH_8822B) &                           \
	 BIT_MASK_AMPDU_MAX_LENGTH_8822B)
#define BIT_SET_AMPDU_MAX_LENGTH_8822B(x, v)                                   \
	(BIT_CLEAR_AMPDU_MAX_LENGTH_8822B(x) | BIT_AMPDU_MAX_LENGTH_8822B(v))

/* 2 REG_ACQ_STOP_8822B */
#define BIT_AC7Q_STOP_8822B BIT(7)
#define BIT_AC6Q_STOP_8822B BIT(6)
#define BIT_AC5Q_STOP_8822B BIT(5)
#define BIT_AC4Q_STOP_8822B BIT(4)
#define BIT_AC3Q_STOP_8822B BIT(3)
#define BIT_AC2Q_STOP_8822B BIT(2)
#define BIT_AC1Q_STOP_8822B BIT(1)
#define BIT_AC0Q_STOP_8822B BIT(0)

/* 2 REG_NDPA_RATE_8822B */

#define BIT_SHIFT_R_NDPA_RATE_V1_8822B 0
#define BIT_MASK_R_NDPA_RATE_V1_8822B 0xff
#define BIT_R_NDPA_RATE_V1_8822B(x)                                            \
	(((x) & BIT_MASK_R_NDPA_RATE_V1_8822B)                                 \
	 << BIT_SHIFT_R_NDPA_RATE_V1_8822B)
#define BITS_R_NDPA_RATE_V1_8822B                                              \
	(BIT_MASK_R_NDPA_RATE_V1_8822B << BIT_SHIFT_R_NDPA_RATE_V1_8822B)
#define BIT_CLEAR_R_NDPA_RATE_V1_8822B(x) ((x) & (~BITS_R_NDPA_RATE_V1_8822B))
#define BIT_GET_R_NDPA_RATE_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_R_NDPA_RATE_V1_8822B) &                             \
	 BIT_MASK_R_NDPA_RATE_V1_8822B)
#define BIT_SET_R_NDPA_RATE_V1_8822B(x, v)                                     \
	(BIT_CLEAR_R_NDPA_RATE_V1_8822B(x) | BIT_R_NDPA_RATE_V1_8822B(v))

/* 2 REG_TX_HANG_CTRL_8822B */
#define BIT_R_EN_GNT_BT_AWAKE_8822B BIT(3)
#define BIT_EN_EOF_V1_8822B BIT(2)
#define BIT_DIS_OQT_BLOCK_8822B BIT(1)
#define BIT_SEARCH_QUEUE_EN_8822B BIT(0)

/* 2 REG_NDPA_OPT_CTRL_8822B */
#define BIT_R_DIS_MACID_RELEASE_RTY_8822B BIT(5)

#define BIT_SHIFT_BW_SIGTA_8822B 3
#define BIT_MASK_BW_SIGTA_8822B 0x3
#define BIT_BW_SIGTA_8822B(x)                                                  \
	(((x) & BIT_MASK_BW_SIGTA_8822B) << BIT_SHIFT_BW_SIGTA_8822B)
#define BITS_BW_SIGTA_8822B                                                    \
	(BIT_MASK_BW_SIGTA_8822B << BIT_SHIFT_BW_SIGTA_8822B)
#define BIT_CLEAR_BW_SIGTA_8822B(x) ((x) & (~BITS_BW_SIGTA_8822B))
#define BIT_GET_BW_SIGTA_8822B(x)                                              \
	(((x) >> BIT_SHIFT_BW_SIGTA_8822B) & BIT_MASK_BW_SIGTA_8822B)
#define BIT_SET_BW_SIGTA_8822B(x, v)                                           \
	(BIT_CLEAR_BW_SIGTA_8822B(x) | BIT_BW_SIGTA_8822B(v))

#define BIT_EN_BAR_SIGTA_8822B BIT(2)

#define BIT_SHIFT_R_NDPA_BW_8822B 0
#define BIT_MASK_R_NDPA_BW_8822B 0x3
#define BIT_R_NDPA_BW_8822B(x)                                                 \
	(((x) & BIT_MASK_R_NDPA_BW_8822B) << BIT_SHIFT_R_NDPA_BW_8822B)
#define BITS_R_NDPA_BW_8822B                                                   \
	(BIT_MASK_R_NDPA_BW_8822B << BIT_SHIFT_R_NDPA_BW_8822B)
#define BIT_CLEAR_R_NDPA_BW_8822B(x) ((x) & (~BITS_R_NDPA_BW_8822B))
#define BIT_GET_R_NDPA_BW_8822B(x)                                             \
	(((x) >> BIT_SHIFT_R_NDPA_BW_8822B) & BIT_MASK_R_NDPA_BW_8822B)
#define BIT_SET_R_NDPA_BW_8822B(x, v)                                          \
	(BIT_CLEAR_R_NDPA_BW_8822B(x) | BIT_R_NDPA_BW_8822B(v))

/* 2 REG_RD_RESP_PKT_TH_8822B */

#define BIT_SHIFT_RD_RESP_PKT_TH_V1_8822B 0
#define BIT_MASK_RD_RESP_PKT_TH_V1_8822B 0x3f
#define BIT_RD_RESP_PKT_TH_V1_8822B(x)                                         \
	(((x) & BIT_MASK_RD_RESP_PKT_TH_V1_8822B)                              \
	 << BIT_SHIFT_RD_RESP_PKT_TH_V1_8822B)
#define BITS_RD_RESP_PKT_TH_V1_8822B                                           \
	(BIT_MASK_RD_RESP_PKT_TH_V1_8822B << BIT_SHIFT_RD_RESP_PKT_TH_V1_8822B)
#define BIT_CLEAR_RD_RESP_PKT_TH_V1_8822B(x)                                   \
	((x) & (~BITS_RD_RESP_PKT_TH_V1_8822B))
#define BIT_GET_RD_RESP_PKT_TH_V1_8822B(x)                                     \
	(((x) >> BIT_SHIFT_RD_RESP_PKT_TH_V1_8822B) &                          \
	 BIT_MASK_RD_RESP_PKT_TH_V1_8822B)
#define BIT_SET_RD_RESP_PKT_TH_V1_8822B(x, v)                                  \
	(BIT_CLEAR_RD_RESP_PKT_TH_V1_8822B(x) | BIT_RD_RESP_PKT_TH_V1_8822B(v))

/* 2 REG_CMDQ_INFO_8822B */

#define BIT_SHIFT_QUEUEMACID_CMDQ_V1_8822B 25
#define BIT_MASK_QUEUEMACID_CMDQ_V1_8822B 0x7f
#define BIT_QUEUEMACID_CMDQ_V1_8822B(x)                                        \
	(((x) & BIT_MASK_QUEUEMACID_CMDQ_V1_8822B)                             \
	 << BIT_SHIFT_QUEUEMACID_CMDQ_V1_8822B)
#define BITS_QUEUEMACID_CMDQ_V1_8822B                                          \
	(BIT_MASK_QUEUEMACID_CMDQ_V1_8822B                                     \
	 << BIT_SHIFT_QUEUEMACID_CMDQ_V1_8822B)
#define BIT_CLEAR_QUEUEMACID_CMDQ_V1_8822B(x)                                  \
	((x) & (~BITS_QUEUEMACID_CMDQ_V1_8822B))
#define BIT_GET_QUEUEMACID_CMDQ_V1_8822B(x)                                    \
	(((x) >> BIT_SHIFT_QUEUEMACID_CMDQ_V1_8822B) &                         \
	 BIT_MASK_QUEUEMACID_CMDQ_V1_8822B)
#define BIT_SET_QUEUEMACID_CMDQ_V1_8822B(x, v)                                 \
	(BIT_CLEAR_QUEUEMACID_CMDQ_V1_8822B(x) |                               \
	 BIT_QUEUEMACID_CMDQ_V1_8822B(v))

#define BIT_SHIFT_QUEUEAC_CMDQ_V1_8822B 23
#define BIT_MASK_QUEUEAC_CMDQ_V1_8822B 0x3
#define BIT_QUEUEAC_CMDQ_V1_8822B(x)                                           \
	(((x) & BIT_MASK_QUEUEAC_CMDQ_V1_8822B)                                \
	 << BIT_SHIFT_QUEUEAC_CMDQ_V1_8822B)
#define BITS_QUEUEAC_CMDQ_V1_8822B                                             \
	(BIT_MASK_QUEUEAC_CMDQ_V1_8822B << BIT_SHIFT_QUEUEAC_CMDQ_V1_8822B)
#define BIT_CLEAR_QUEUEAC_CMDQ_V1_8822B(x) ((x) & (~BITS_QUEUEAC_CMDQ_V1_8822B))
#define BIT_GET_QUEUEAC_CMDQ_V1_8822B(x)                                       \
	(((x) >> BIT_SHIFT_QUEUEAC_CMDQ_V1_8822B) &                            \
	 BIT_MASK_QUEUEAC_CMDQ_V1_8822B)
#define BIT_SET_QUEUEAC_CMDQ_V1_8822B(x, v)                                    \
	(BIT_CLEAR_QUEUEAC_CMDQ_V1_8822B(x) | BIT_QUEUEAC_CMDQ_V1_8822B(v))

#define BIT_TIDEMPTY_CMDQ_V1_8822B BIT(22)

#define BIT_SHIFT_TAIL_PKT_CMDQ_V2_8822B 11
#define BIT_MASK_TAIL_PKT_CMDQ_V2_8822B 0x7ff
#define BIT_TAIL_PKT_CMDQ_V2_8822B(x)                                          \
	(((x) & BIT_MASK_TAIL_PKT_CMDQ_V2_8822B)                               \
	 << BIT_SHIFT_TAIL_PKT_CMDQ_V2_8822B)
#define BITS_TAIL_PKT_CMDQ_V2_8822B                                            \
	(BIT_MASK_TAIL_PKT_CMDQ_V2_8822B << BIT_SHIFT_TAIL_PKT_CMDQ_V2_8822B)
#define BIT_CLEAR_TAIL_PKT_CMDQ_V2_8822B(x)                                    \
	((x) & (~BITS_TAIL_PKT_CMDQ_V2_8822B))
#define BIT_GET_TAIL_PKT_CMDQ_V2_8822B(x)                                      \
	(((x) >> BIT_SHIFT_TAIL_PKT_CMDQ_V2_8822B) &                           \
	 BIT_MASK_TAIL_PKT_CMDQ_V2_8822B)
#define BIT_SET_TAIL_PKT_CMDQ_V2_8822B(x, v)                                   \
	(BIT_CLEAR_TAIL_PKT_CMDQ_V2_8822B(x) | BIT_TAIL_PKT_CMDQ_V2_8822B(v))

#define BIT_SHIFT_HEAD_PKT_CMDQ_V1_8822B 0
#define BIT_MASK_HEAD_PKT_CMDQ_V1_8822B 0x7ff
#define BIT_HEAD_PKT_CMDQ_V1_8822B(x)                                          \
	(((x) & BIT_MASK_HEAD_PKT_CMDQ_V1_8822B)                               \
	 << BIT_SHIFT_HEAD_PKT_CMDQ_V1_8822B)
#define BITS_HEAD_PKT_CMDQ_V1_8822B                                            \
	(BIT_MASK_HEAD_PKT_CMDQ_V1_8822B << BIT_SHIFT_HEAD_PKT_CMDQ_V1_8822B)
#define BIT_CLEAR_HEAD_PKT_CMDQ_V1_8822B(x)                                    \
	((x) & (~BITS_HEAD_PKT_CMDQ_V1_8822B))
#define BIT_GET_HEAD_PKT_CMDQ_V1_8822B(x)                                      \
	(((x) >> BIT_SHIFT_HEAD_PKT_CMDQ_V1_8822B) &                           \
	 BIT_MASK_HEAD_PKT_CMDQ_V1_8822B)
#define BIT_SET_HEAD_PKT_CMDQ_V1_8822B(x, v)                                   \
	(BIT_CLEAR_HEAD_PKT_CMDQ_V1_8822B(x) | BIT_HEAD_PKT_CMDQ_V1_8822B(v))

/* 2 REG_Q4_INFO_8822B */

#define BIT_SHIFT_QUEUEMACID_Q4_V1_8822B 25
#define BIT_MASK_QUEUEMACID_Q4_V1_8822B 0x7f
#define BIT_QUEUEMACID_Q4_V1_8822B(x)                                          \
	(((x) & BIT_MASK_QUEUEMACID_Q4_V1_8822B)                               \
	 << BIT_SHIFT_QUEUEMACID_Q4_V1_8822B)
#define BITS_QUEUEMACID_Q4_V1_8822B                                            \
	(BIT_MASK_QUEUEMACID_Q4_V1_8822B << BIT_SHIFT_QUEUEMACID_Q4_V1_8822B)
#define BIT_CLEAR_QUEUEMACID_Q4_V1_8822B(x)                                    \
	((x) & (~BITS_QUEUEMACID_Q4_V1_8822B))
#define BIT_GET_QUEUEMACID_Q4_V1_8822B(x)                                      \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q4_V1_8822B) &                           \
	 BIT_MASK_QUEUEMACID_Q4_V1_8822B)
#define BIT_SET_QUEUEMACID_Q4_V1_8822B(x, v)                                   \
	(BIT_CLEAR_QUEUEMACID_Q4_V1_8822B(x) | BIT_QUEUEMACID_Q4_V1_8822B(v))

#define BIT_SHIFT_QUEUEAC_Q4_V1_8822B 23
#define BIT_MASK_QUEUEAC_Q4_V1_8822B 0x3
#define BIT_QUEUEAC_Q4_V1_8822B(x)                                             \
	(((x) & BIT_MASK_QUEUEAC_Q4_V1_8822B) << BIT_SHIFT_QUEUEAC_Q4_V1_8822B)
#define BITS_QUEUEAC_Q4_V1_8822B                                               \
	(BIT_MASK_QUEUEAC_Q4_V1_8822B << BIT_SHIFT_QUEUEAC_Q4_V1_8822B)
#define BIT_CLEAR_QUEUEAC_Q4_V1_8822B(x) ((x) & (~BITS_QUEUEAC_Q4_V1_8822B))
#define BIT_GET_QUEUEAC_Q4_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_QUEUEAC_Q4_V1_8822B) & BIT_MASK_QUEUEAC_Q4_V1_8822B)
#define BIT_SET_QUEUEAC_Q4_V1_8822B(x, v)                                      \
	(BIT_CLEAR_QUEUEAC_Q4_V1_8822B(x) | BIT_QUEUEAC_Q4_V1_8822B(v))

#define BIT_TIDEMPTY_Q4_V1_8822B BIT(22)

#define BIT_SHIFT_TAIL_PKT_Q4_V2_8822B 11
#define BIT_MASK_TAIL_PKT_Q4_V2_8822B 0x7ff
#define BIT_TAIL_PKT_Q4_V2_8822B(x)                                            \
	(((x) & BIT_MASK_TAIL_PKT_Q4_V2_8822B)                                 \
	 << BIT_SHIFT_TAIL_PKT_Q4_V2_8822B)
#define BITS_TAIL_PKT_Q4_V2_8822B                                              \
	(BIT_MASK_TAIL_PKT_Q4_V2_8822B << BIT_SHIFT_TAIL_PKT_Q4_V2_8822B)
#define BIT_CLEAR_TAIL_PKT_Q4_V2_8822B(x) ((x) & (~BITS_TAIL_PKT_Q4_V2_8822B))
#define BIT_GET_TAIL_PKT_Q4_V2_8822B(x)                                        \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q4_V2_8822B) &                             \
	 BIT_MASK_TAIL_PKT_Q4_V2_8822B)
#define BIT_SET_TAIL_PKT_Q4_V2_8822B(x, v)                                     \
	(BIT_CLEAR_TAIL_PKT_Q4_V2_8822B(x) | BIT_TAIL_PKT_Q4_V2_8822B(v))

#define BIT_SHIFT_HEAD_PKT_Q4_V1_8822B 0
#define BIT_MASK_HEAD_PKT_Q4_V1_8822B 0x7ff
#define BIT_HEAD_PKT_Q4_V1_8822B(x)                                            \
	(((x) & BIT_MASK_HEAD_PKT_Q4_V1_8822B)                                 \
	 << BIT_SHIFT_HEAD_PKT_Q4_V1_8822B)
#define BITS_HEAD_PKT_Q4_V1_8822B                                              \
	(BIT_MASK_HEAD_PKT_Q4_V1_8822B << BIT_SHIFT_HEAD_PKT_Q4_V1_8822B)
#define BIT_CLEAR_HEAD_PKT_Q4_V1_8822B(x) ((x) & (~BITS_HEAD_PKT_Q4_V1_8822B))
#define BIT_GET_HEAD_PKT_Q4_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q4_V1_8822B) &                             \
	 BIT_MASK_HEAD_PKT_Q4_V1_8822B)
#define BIT_SET_HEAD_PKT_Q4_V1_8822B(x, v)                                     \
	(BIT_CLEAR_HEAD_PKT_Q4_V1_8822B(x) | BIT_HEAD_PKT_Q4_V1_8822B(v))

/* 2 REG_Q5_INFO_8822B */

#define BIT_SHIFT_QUEUEMACID_Q5_V1_8822B 25
#define BIT_MASK_QUEUEMACID_Q5_V1_8822B 0x7f
#define BIT_QUEUEMACID_Q5_V1_8822B(x)                                          \
	(((x) & BIT_MASK_QUEUEMACID_Q5_V1_8822B)                               \
	 << BIT_SHIFT_QUEUEMACID_Q5_V1_8822B)
#define BITS_QUEUEMACID_Q5_V1_8822B                                            \
	(BIT_MASK_QUEUEMACID_Q5_V1_8822B << BIT_SHIFT_QUEUEMACID_Q5_V1_8822B)
#define BIT_CLEAR_QUEUEMACID_Q5_V1_8822B(x)                                    \
	((x) & (~BITS_QUEUEMACID_Q5_V1_8822B))
#define BIT_GET_QUEUEMACID_Q5_V1_8822B(x)                                      \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q5_V1_8822B) &                           \
	 BIT_MASK_QUEUEMACID_Q5_V1_8822B)
#define BIT_SET_QUEUEMACID_Q5_V1_8822B(x, v)                                   \
	(BIT_CLEAR_QUEUEMACID_Q5_V1_8822B(x) | BIT_QUEUEMACID_Q5_V1_8822B(v))

#define BIT_SHIFT_QUEUEAC_Q5_V1_8822B 23
#define BIT_MASK_QUEUEAC_Q5_V1_8822B 0x3
#define BIT_QUEUEAC_Q5_V1_8822B(x)                                             \
	(((x) & BIT_MASK_QUEUEAC_Q5_V1_8822B) << BIT_SHIFT_QUEUEAC_Q5_V1_8822B)
#define BITS_QUEUEAC_Q5_V1_8822B                                               \
	(BIT_MASK_QUEUEAC_Q5_V1_8822B << BIT_SHIFT_QUEUEAC_Q5_V1_8822B)
#define BIT_CLEAR_QUEUEAC_Q5_V1_8822B(x) ((x) & (~BITS_QUEUEAC_Q5_V1_8822B))
#define BIT_GET_QUEUEAC_Q5_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_QUEUEAC_Q5_V1_8822B) & BIT_MASK_QUEUEAC_Q5_V1_8822B)
#define BIT_SET_QUEUEAC_Q5_V1_8822B(x, v)                                      \
	(BIT_CLEAR_QUEUEAC_Q5_V1_8822B(x) | BIT_QUEUEAC_Q5_V1_8822B(v))

#define BIT_TIDEMPTY_Q5_V1_8822B BIT(22)

#define BIT_SHIFT_TAIL_PKT_Q5_V2_8822B 11
#define BIT_MASK_TAIL_PKT_Q5_V2_8822B 0x7ff
#define BIT_TAIL_PKT_Q5_V2_8822B(x)                                            \
	(((x) & BIT_MASK_TAIL_PKT_Q5_V2_8822B)                                 \
	 << BIT_SHIFT_TAIL_PKT_Q5_V2_8822B)
#define BITS_TAIL_PKT_Q5_V2_8822B                                              \
	(BIT_MASK_TAIL_PKT_Q5_V2_8822B << BIT_SHIFT_TAIL_PKT_Q5_V2_8822B)
#define BIT_CLEAR_TAIL_PKT_Q5_V2_8822B(x) ((x) & (~BITS_TAIL_PKT_Q5_V2_8822B))
#define BIT_GET_TAIL_PKT_Q5_V2_8822B(x)                                        \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q5_V2_8822B) &                             \
	 BIT_MASK_TAIL_PKT_Q5_V2_8822B)
#define BIT_SET_TAIL_PKT_Q5_V2_8822B(x, v)                                     \
	(BIT_CLEAR_TAIL_PKT_Q5_V2_8822B(x) | BIT_TAIL_PKT_Q5_V2_8822B(v))

#define BIT_SHIFT_HEAD_PKT_Q5_V1_8822B 0
#define BIT_MASK_HEAD_PKT_Q5_V1_8822B 0x7ff
#define BIT_HEAD_PKT_Q5_V1_8822B(x)                                            \
	(((x) & BIT_MASK_HEAD_PKT_Q5_V1_8822B)                                 \
	 << BIT_SHIFT_HEAD_PKT_Q5_V1_8822B)
#define BITS_HEAD_PKT_Q5_V1_8822B                                              \
	(BIT_MASK_HEAD_PKT_Q5_V1_8822B << BIT_SHIFT_HEAD_PKT_Q5_V1_8822B)
#define BIT_CLEAR_HEAD_PKT_Q5_V1_8822B(x) ((x) & (~BITS_HEAD_PKT_Q5_V1_8822B))
#define BIT_GET_HEAD_PKT_Q5_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q5_V1_8822B) &                             \
	 BIT_MASK_HEAD_PKT_Q5_V1_8822B)
#define BIT_SET_HEAD_PKT_Q5_V1_8822B(x, v)                                     \
	(BIT_CLEAR_HEAD_PKT_Q5_V1_8822B(x) | BIT_HEAD_PKT_Q5_V1_8822B(v))

/* 2 REG_Q6_INFO_8822B */

#define BIT_SHIFT_QUEUEMACID_Q6_V1_8822B 25
#define BIT_MASK_QUEUEMACID_Q6_V1_8822B 0x7f
#define BIT_QUEUEMACID_Q6_V1_8822B(x)                                          \
	(((x) & BIT_MASK_QUEUEMACID_Q6_V1_8822B)                               \
	 << BIT_SHIFT_QUEUEMACID_Q6_V1_8822B)
#define BITS_QUEUEMACID_Q6_V1_8822B                                            \
	(BIT_MASK_QUEUEMACID_Q6_V1_8822B << BIT_SHIFT_QUEUEMACID_Q6_V1_8822B)
#define BIT_CLEAR_QUEUEMACID_Q6_V1_8822B(x)                                    \
	((x) & (~BITS_QUEUEMACID_Q6_V1_8822B))
#define BIT_GET_QUEUEMACID_Q6_V1_8822B(x)                                      \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q6_V1_8822B) &                           \
	 BIT_MASK_QUEUEMACID_Q6_V1_8822B)
#define BIT_SET_QUEUEMACID_Q6_V1_8822B(x, v)                                   \
	(BIT_CLEAR_QUEUEMACID_Q6_V1_8822B(x) | BIT_QUEUEMACID_Q6_V1_8822B(v))

#define BIT_SHIFT_QUEUEAC_Q6_V1_8822B 23
#define BIT_MASK_QUEUEAC_Q6_V1_8822B 0x3
#define BIT_QUEUEAC_Q6_V1_8822B(x)                                             \
	(((x) & BIT_MASK_QUEUEAC_Q6_V1_8822B) << BIT_SHIFT_QUEUEAC_Q6_V1_8822B)
#define BITS_QUEUEAC_Q6_V1_8822B                                               \
	(BIT_MASK_QUEUEAC_Q6_V1_8822B << BIT_SHIFT_QUEUEAC_Q6_V1_8822B)
#define BIT_CLEAR_QUEUEAC_Q6_V1_8822B(x) ((x) & (~BITS_QUEUEAC_Q6_V1_8822B))
#define BIT_GET_QUEUEAC_Q6_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_QUEUEAC_Q6_V1_8822B) & BIT_MASK_QUEUEAC_Q6_V1_8822B)
#define BIT_SET_QUEUEAC_Q6_V1_8822B(x, v)                                      \
	(BIT_CLEAR_QUEUEAC_Q6_V1_8822B(x) | BIT_QUEUEAC_Q6_V1_8822B(v))

#define BIT_TIDEMPTY_Q6_V1_8822B BIT(22)

#define BIT_SHIFT_TAIL_PKT_Q6_V2_8822B 11
#define BIT_MASK_TAIL_PKT_Q6_V2_8822B 0x7ff
#define BIT_TAIL_PKT_Q6_V2_8822B(x)                                            \
	(((x) & BIT_MASK_TAIL_PKT_Q6_V2_8822B)                                 \
	 << BIT_SHIFT_TAIL_PKT_Q6_V2_8822B)
#define BITS_TAIL_PKT_Q6_V2_8822B                                              \
	(BIT_MASK_TAIL_PKT_Q6_V2_8822B << BIT_SHIFT_TAIL_PKT_Q6_V2_8822B)
#define BIT_CLEAR_TAIL_PKT_Q6_V2_8822B(x) ((x) & (~BITS_TAIL_PKT_Q6_V2_8822B))
#define BIT_GET_TAIL_PKT_Q6_V2_8822B(x)                                        \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q6_V2_8822B) &                             \
	 BIT_MASK_TAIL_PKT_Q6_V2_8822B)
#define BIT_SET_TAIL_PKT_Q6_V2_8822B(x, v)                                     \
	(BIT_CLEAR_TAIL_PKT_Q6_V2_8822B(x) | BIT_TAIL_PKT_Q6_V2_8822B(v))

#define BIT_SHIFT_HEAD_PKT_Q6_V1_8822B 0
#define BIT_MASK_HEAD_PKT_Q6_V1_8822B 0x7ff
#define BIT_HEAD_PKT_Q6_V1_8822B(x)                                            \
	(((x) & BIT_MASK_HEAD_PKT_Q6_V1_8822B)                                 \
	 << BIT_SHIFT_HEAD_PKT_Q6_V1_8822B)
#define BITS_HEAD_PKT_Q6_V1_8822B                                              \
	(BIT_MASK_HEAD_PKT_Q6_V1_8822B << BIT_SHIFT_HEAD_PKT_Q6_V1_8822B)
#define BIT_CLEAR_HEAD_PKT_Q6_V1_8822B(x) ((x) & (~BITS_HEAD_PKT_Q6_V1_8822B))
#define BIT_GET_HEAD_PKT_Q6_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q6_V1_8822B) &                             \
	 BIT_MASK_HEAD_PKT_Q6_V1_8822B)
#define BIT_SET_HEAD_PKT_Q6_V1_8822B(x, v)                                     \
	(BIT_CLEAR_HEAD_PKT_Q6_V1_8822B(x) | BIT_HEAD_PKT_Q6_V1_8822B(v))

/* 2 REG_Q7_INFO_8822B */

#define BIT_SHIFT_QUEUEMACID_Q7_V1_8822B 25
#define BIT_MASK_QUEUEMACID_Q7_V1_8822B 0x7f
#define BIT_QUEUEMACID_Q7_V1_8822B(x)                                          \
	(((x) & BIT_MASK_QUEUEMACID_Q7_V1_8822B)                               \
	 << BIT_SHIFT_QUEUEMACID_Q7_V1_8822B)
#define BITS_QUEUEMACID_Q7_V1_8822B                                            \
	(BIT_MASK_QUEUEMACID_Q7_V1_8822B << BIT_SHIFT_QUEUEMACID_Q7_V1_8822B)
#define BIT_CLEAR_QUEUEMACID_Q7_V1_8822B(x)                                    \
	((x) & (~BITS_QUEUEMACID_Q7_V1_8822B))
#define BIT_GET_QUEUEMACID_Q7_V1_8822B(x)                                      \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q7_V1_8822B) &                           \
	 BIT_MASK_QUEUEMACID_Q7_V1_8822B)
#define BIT_SET_QUEUEMACID_Q7_V1_8822B(x, v)                                   \
	(BIT_CLEAR_QUEUEMACID_Q7_V1_8822B(x) | BIT_QUEUEMACID_Q7_V1_8822B(v))

#define BIT_SHIFT_QUEUEAC_Q7_V1_8822B 23
#define BIT_MASK_QUEUEAC_Q7_V1_8822B 0x3
#define BIT_QUEUEAC_Q7_V1_8822B(x)                                             \
	(((x) & BIT_MASK_QUEUEAC_Q7_V1_8822B) << BIT_SHIFT_QUEUEAC_Q7_V1_8822B)
#define BITS_QUEUEAC_Q7_V1_8822B                                               \
	(BIT_MASK_QUEUEAC_Q7_V1_8822B << BIT_SHIFT_QUEUEAC_Q7_V1_8822B)
#define BIT_CLEAR_QUEUEAC_Q7_V1_8822B(x) ((x) & (~BITS_QUEUEAC_Q7_V1_8822B))
#define BIT_GET_QUEUEAC_Q7_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_QUEUEAC_Q7_V1_8822B) & BIT_MASK_QUEUEAC_Q7_V1_8822B)
#define BIT_SET_QUEUEAC_Q7_V1_8822B(x, v)                                      \
	(BIT_CLEAR_QUEUEAC_Q7_V1_8822B(x) | BIT_QUEUEAC_Q7_V1_8822B(v))

#define BIT_TIDEMPTY_Q7_V1_8822B BIT(22)

#define BIT_SHIFT_TAIL_PKT_Q7_V2_8822B 11
#define BIT_MASK_TAIL_PKT_Q7_V2_8822B 0x7ff
#define BIT_TAIL_PKT_Q7_V2_8822B(x)                                            \
	(((x) & BIT_MASK_TAIL_PKT_Q7_V2_8822B)                                 \
	 << BIT_SHIFT_TAIL_PKT_Q7_V2_8822B)
#define BITS_TAIL_PKT_Q7_V2_8822B                                              \
	(BIT_MASK_TAIL_PKT_Q7_V2_8822B << BIT_SHIFT_TAIL_PKT_Q7_V2_8822B)
#define BIT_CLEAR_TAIL_PKT_Q7_V2_8822B(x) ((x) & (~BITS_TAIL_PKT_Q7_V2_8822B))
#define BIT_GET_TAIL_PKT_Q7_V2_8822B(x)                                        \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q7_V2_8822B) &                             \
	 BIT_MASK_TAIL_PKT_Q7_V2_8822B)
#define BIT_SET_TAIL_PKT_Q7_V2_8822B(x, v)                                     \
	(BIT_CLEAR_TAIL_PKT_Q7_V2_8822B(x) | BIT_TAIL_PKT_Q7_V2_8822B(v))

#define BIT_SHIFT_HEAD_PKT_Q7_V1_8822B 0
#define BIT_MASK_HEAD_PKT_Q7_V1_8822B 0x7ff
#define BIT_HEAD_PKT_Q7_V1_8822B(x)                                            \
	(((x) & BIT_MASK_HEAD_PKT_Q7_V1_8822B)                                 \
	 << BIT_SHIFT_HEAD_PKT_Q7_V1_8822B)
#define BITS_HEAD_PKT_Q7_V1_8822B                                              \
	(BIT_MASK_HEAD_PKT_Q7_V1_8822B << BIT_SHIFT_HEAD_PKT_Q7_V1_8822B)
#define BIT_CLEAR_HEAD_PKT_Q7_V1_8822B(x) ((x) & (~BITS_HEAD_PKT_Q7_V1_8822B))
#define BIT_GET_HEAD_PKT_Q7_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q7_V1_8822B) &                             \
	 BIT_MASK_HEAD_PKT_Q7_V1_8822B)
#define BIT_SET_HEAD_PKT_Q7_V1_8822B(x, v)                                     \
	(BIT_CLEAR_HEAD_PKT_Q7_V1_8822B(x) | BIT_HEAD_PKT_Q7_V1_8822B(v))

/* 2 REG_WMAC_LBK_BUF_HD_V1_8822B */

#define BIT_SHIFT_WMAC_LBK_BUF_HEAD_V1_8822B 0
#define BIT_MASK_WMAC_LBK_BUF_HEAD_V1_8822B 0xfff
#define BIT_WMAC_LBK_BUF_HEAD_V1_8822B(x)                                      \
	(((x) & BIT_MASK_WMAC_LBK_BUF_HEAD_V1_8822B)                           \
	 << BIT_SHIFT_WMAC_LBK_BUF_HEAD_V1_8822B)
#define BITS_WMAC_LBK_BUF_HEAD_V1_8822B                                        \
	(BIT_MASK_WMAC_LBK_BUF_HEAD_V1_8822B                                   \
	 << BIT_SHIFT_WMAC_LBK_BUF_HEAD_V1_8822B)
#define BIT_CLEAR_WMAC_LBK_BUF_HEAD_V1_8822B(x)                                \
	((x) & (~BITS_WMAC_LBK_BUF_HEAD_V1_8822B))
#define BIT_GET_WMAC_LBK_BUF_HEAD_V1_8822B(x)                                  \
	(((x) >> BIT_SHIFT_WMAC_LBK_BUF_HEAD_V1_8822B) &                       \
	 BIT_MASK_WMAC_LBK_BUF_HEAD_V1_8822B)
#define BIT_SET_WMAC_LBK_BUF_HEAD_V1_8822B(x, v)                               \
	(BIT_CLEAR_WMAC_LBK_BUF_HEAD_V1_8822B(x) |                             \
	 BIT_WMAC_LBK_BUF_HEAD_V1_8822B(v))

/* 2 REG_MGQ_BDNY_V1_8822B */

#define BIT_SHIFT_MGQ_PGBNDY_V1_8822B 0
#define BIT_MASK_MGQ_PGBNDY_V1_8822B 0xfff
#define BIT_MGQ_PGBNDY_V1_8822B(x)                                             \
	(((x) & BIT_MASK_MGQ_PGBNDY_V1_8822B) << BIT_SHIFT_MGQ_PGBNDY_V1_8822B)
#define BITS_MGQ_PGBNDY_V1_8822B                                               \
	(BIT_MASK_MGQ_PGBNDY_V1_8822B << BIT_SHIFT_MGQ_PGBNDY_V1_8822B)
#define BIT_CLEAR_MGQ_PGBNDY_V1_8822B(x) ((x) & (~BITS_MGQ_PGBNDY_V1_8822B))
#define BIT_GET_MGQ_PGBNDY_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_MGQ_PGBNDY_V1_8822B) & BIT_MASK_MGQ_PGBNDY_V1_8822B)
#define BIT_SET_MGQ_PGBNDY_V1_8822B(x, v)                                      \
	(BIT_CLEAR_MGQ_PGBNDY_V1_8822B(x) | BIT_MGQ_PGBNDY_V1_8822B(v))

/* 2 REG_TXRPT_CTRL_8822B */

#define BIT_SHIFT_TRXRPT_TIMER_TH_8822B 24
#define BIT_MASK_TRXRPT_TIMER_TH_8822B 0xff
#define BIT_TRXRPT_TIMER_TH_8822B(x)                                           \
	(((x) & BIT_MASK_TRXRPT_TIMER_TH_8822B)                                \
	 << BIT_SHIFT_TRXRPT_TIMER_TH_8822B)
#define BITS_TRXRPT_TIMER_TH_8822B                                             \
	(BIT_MASK_TRXRPT_TIMER_TH_8822B << BIT_SHIFT_TRXRPT_TIMER_TH_8822B)
#define BIT_CLEAR_TRXRPT_TIMER_TH_8822B(x) ((x) & (~BITS_TRXRPT_TIMER_TH_8822B))
#define BIT_GET_TRXRPT_TIMER_TH_8822B(x)                                       \
	(((x) >> BIT_SHIFT_TRXRPT_TIMER_TH_8822B) &                            \
	 BIT_MASK_TRXRPT_TIMER_TH_8822B)
#define BIT_SET_TRXRPT_TIMER_TH_8822B(x, v)                                    \
	(BIT_CLEAR_TRXRPT_TIMER_TH_8822B(x) | BIT_TRXRPT_TIMER_TH_8822B(v))

#define BIT_SHIFT_TRXRPT_LEN_TH_8822B 16
#define BIT_MASK_TRXRPT_LEN_TH_8822B 0xff
#define BIT_TRXRPT_LEN_TH_8822B(x)                                             \
	(((x) & BIT_MASK_TRXRPT_LEN_TH_8822B) << BIT_SHIFT_TRXRPT_LEN_TH_8822B)
#define BITS_TRXRPT_LEN_TH_8822B                                               \
	(BIT_MASK_TRXRPT_LEN_TH_8822B << BIT_SHIFT_TRXRPT_LEN_TH_8822B)
#define BIT_CLEAR_TRXRPT_LEN_TH_8822B(x) ((x) & (~BITS_TRXRPT_LEN_TH_8822B))
#define BIT_GET_TRXRPT_LEN_TH_8822B(x)                                         \
	(((x) >> BIT_SHIFT_TRXRPT_LEN_TH_8822B) & BIT_MASK_TRXRPT_LEN_TH_8822B)
#define BIT_SET_TRXRPT_LEN_TH_8822B(x, v)                                      \
	(BIT_CLEAR_TRXRPT_LEN_TH_8822B(x) | BIT_TRXRPT_LEN_TH_8822B(v))

#define BIT_SHIFT_TRXRPT_READ_PTR_8822B 8
#define BIT_MASK_TRXRPT_READ_PTR_8822B 0xff
#define BIT_TRXRPT_READ_PTR_8822B(x)                                           \
	(((x) & BIT_MASK_TRXRPT_READ_PTR_8822B)                                \
	 << BIT_SHIFT_TRXRPT_READ_PTR_8822B)
#define BITS_TRXRPT_READ_PTR_8822B                                             \
	(BIT_MASK_TRXRPT_READ_PTR_8822B << BIT_SHIFT_TRXRPT_READ_PTR_8822B)
#define BIT_CLEAR_TRXRPT_READ_PTR_8822B(x) ((x) & (~BITS_TRXRPT_READ_PTR_8822B))
#define BIT_GET_TRXRPT_READ_PTR_8822B(x)                                       \
	(((x) >> BIT_SHIFT_TRXRPT_READ_PTR_8822B) &                            \
	 BIT_MASK_TRXRPT_READ_PTR_8822B)
#define BIT_SET_TRXRPT_READ_PTR_8822B(x, v)                                    \
	(BIT_CLEAR_TRXRPT_READ_PTR_8822B(x) | BIT_TRXRPT_READ_PTR_8822B(v))

#define BIT_SHIFT_TRXRPT_WRITE_PTR_8822B 0
#define BIT_MASK_TRXRPT_WRITE_PTR_8822B 0xff
#define BIT_TRXRPT_WRITE_PTR_8822B(x)                                          \
	(((x) & BIT_MASK_TRXRPT_WRITE_PTR_8822B)                               \
	 << BIT_SHIFT_TRXRPT_WRITE_PTR_8822B)
#define BITS_TRXRPT_WRITE_PTR_8822B                                            \
	(BIT_MASK_TRXRPT_WRITE_PTR_8822B << BIT_SHIFT_TRXRPT_WRITE_PTR_8822B)
#define BIT_CLEAR_TRXRPT_WRITE_PTR_8822B(x)                                    \
	((x) & (~BITS_TRXRPT_WRITE_PTR_8822B))
#define BIT_GET_TRXRPT_WRITE_PTR_8822B(x)                                      \
	(((x) >> BIT_SHIFT_TRXRPT_WRITE_PTR_8822B) &                           \
	 BIT_MASK_TRXRPT_WRITE_PTR_8822B)
#define BIT_SET_TRXRPT_WRITE_PTR_8822B(x, v)                                   \
	(BIT_CLEAR_TRXRPT_WRITE_PTR_8822B(x) | BIT_TRXRPT_WRITE_PTR_8822B(v))

/* 2 REG_INIRTS_RATE_SEL_8822B */
#define BIT_LEAG_RTS_BW_DUP_8822B BIT(5)

/* 2 REG_BASIC_CFEND_RATE_8822B */

#define BIT_SHIFT_BASIC_CFEND_RATE_8822B 0
#define BIT_MASK_BASIC_CFEND_RATE_8822B 0x1f
#define BIT_BASIC_CFEND_RATE_8822B(x)                                          \
	(((x) & BIT_MASK_BASIC_CFEND_RATE_8822B)                               \
	 << BIT_SHIFT_BASIC_CFEND_RATE_8822B)
#define BITS_BASIC_CFEND_RATE_8822B                                            \
	(BIT_MASK_BASIC_CFEND_RATE_8822B << BIT_SHIFT_BASIC_CFEND_RATE_8822B)
#define BIT_CLEAR_BASIC_CFEND_RATE_8822B(x)                                    \
	((x) & (~BITS_BASIC_CFEND_RATE_8822B))
#define BIT_GET_BASIC_CFEND_RATE_8822B(x)                                      \
	(((x) >> BIT_SHIFT_BASIC_CFEND_RATE_8822B) &                           \
	 BIT_MASK_BASIC_CFEND_RATE_8822B)
#define BIT_SET_BASIC_CFEND_RATE_8822B(x, v)                                   \
	(BIT_CLEAR_BASIC_CFEND_RATE_8822B(x) | BIT_BASIC_CFEND_RATE_8822B(v))

/* 2 REG_STBC_CFEND_RATE_8822B */

#define BIT_SHIFT_STBC_CFEND_RATE_8822B 0
#define BIT_MASK_STBC_CFEND_RATE_8822B 0x1f
#define BIT_STBC_CFEND_RATE_8822B(x)                                           \
	(((x) & BIT_MASK_STBC_CFEND_RATE_8822B)                                \
	 << BIT_SHIFT_STBC_CFEND_RATE_8822B)
#define BITS_STBC_CFEND_RATE_8822B                                             \
	(BIT_MASK_STBC_CFEND_RATE_8822B << BIT_SHIFT_STBC_CFEND_RATE_8822B)
#define BIT_CLEAR_STBC_CFEND_RATE_8822B(x) ((x) & (~BITS_STBC_CFEND_RATE_8822B))
#define BIT_GET_STBC_CFEND_RATE_8822B(x)                                       \
	(((x) >> BIT_SHIFT_STBC_CFEND_RATE_8822B) &                            \
	 BIT_MASK_STBC_CFEND_RATE_8822B)
#define BIT_SET_STBC_CFEND_RATE_8822B(x, v)                                    \
	(BIT_CLEAR_STBC_CFEND_RATE_8822B(x) | BIT_STBC_CFEND_RATE_8822B(v))

/* 2 REG_DATA_SC_8822B */

#define BIT_SHIFT_TXSC_40M_8822B 4
#define BIT_MASK_TXSC_40M_8822B 0xf
#define BIT_TXSC_40M_8822B(x)                                                  \
	(((x) & BIT_MASK_TXSC_40M_8822B) << BIT_SHIFT_TXSC_40M_8822B)
#define BITS_TXSC_40M_8822B                                                    \
	(BIT_MASK_TXSC_40M_8822B << BIT_SHIFT_TXSC_40M_8822B)
#define BIT_CLEAR_TXSC_40M_8822B(x) ((x) & (~BITS_TXSC_40M_8822B))
#define BIT_GET_TXSC_40M_8822B(x)                                              \
	(((x) >> BIT_SHIFT_TXSC_40M_8822B) & BIT_MASK_TXSC_40M_8822B)
#define BIT_SET_TXSC_40M_8822B(x, v)                                           \
	(BIT_CLEAR_TXSC_40M_8822B(x) | BIT_TXSC_40M_8822B(v))

#define BIT_SHIFT_TXSC_20M_8822B 0
#define BIT_MASK_TXSC_20M_8822B 0xf
#define BIT_TXSC_20M_8822B(x)                                                  \
	(((x) & BIT_MASK_TXSC_20M_8822B) << BIT_SHIFT_TXSC_20M_8822B)
#define BITS_TXSC_20M_8822B                                                    \
	(BIT_MASK_TXSC_20M_8822B << BIT_SHIFT_TXSC_20M_8822B)
#define BIT_CLEAR_TXSC_20M_8822B(x) ((x) & (~BITS_TXSC_20M_8822B))
#define BIT_GET_TXSC_20M_8822B(x)                                              \
	(((x) >> BIT_SHIFT_TXSC_20M_8822B) & BIT_MASK_TXSC_20M_8822B)
#define BIT_SET_TXSC_20M_8822B(x, v)                                           \
	(BIT_CLEAR_TXSC_20M_8822B(x) | BIT_TXSC_20M_8822B(v))

/* 2 REG_MACID_SLEEP3_8822B */

#define BIT_SHIFT_MACID127_96_PKTSLEEP_8822B 0
#define BIT_MASK_MACID127_96_PKTSLEEP_8822B 0xffffffffL
#define BIT_MACID127_96_PKTSLEEP_8822B(x)                                      \
	(((x) & BIT_MASK_MACID127_96_PKTSLEEP_8822B)                           \
	 << BIT_SHIFT_MACID127_96_PKTSLEEP_8822B)
#define BITS_MACID127_96_PKTSLEEP_8822B                                        \
	(BIT_MASK_MACID127_96_PKTSLEEP_8822B                                   \
	 << BIT_SHIFT_MACID127_96_PKTSLEEP_8822B)
#define BIT_CLEAR_MACID127_96_PKTSLEEP_8822B(x)                                \
	((x) & (~BITS_MACID127_96_PKTSLEEP_8822B))
#define BIT_GET_MACID127_96_PKTSLEEP_8822B(x)                                  \
	(((x) >> BIT_SHIFT_MACID127_96_PKTSLEEP_8822B) &                       \
	 BIT_MASK_MACID127_96_PKTSLEEP_8822B)
#define BIT_SET_MACID127_96_PKTSLEEP_8822B(x, v)                               \
	(BIT_CLEAR_MACID127_96_PKTSLEEP_8822B(x) |                             \
	 BIT_MACID127_96_PKTSLEEP_8822B(v))

/* 2 REG_MACID_SLEEP1_8822B */

#define BIT_SHIFT_MACID63_32_PKTSLEEP_8822B 0
#define BIT_MASK_MACID63_32_PKTSLEEP_8822B 0xffffffffL
#define BIT_MACID63_32_PKTSLEEP_8822B(x)                                       \
	(((x) & BIT_MASK_MACID63_32_PKTSLEEP_8822B)                            \
	 << BIT_SHIFT_MACID63_32_PKTSLEEP_8822B)
#define BITS_MACID63_32_PKTSLEEP_8822B                                         \
	(BIT_MASK_MACID63_32_PKTSLEEP_8822B                                    \
	 << BIT_SHIFT_MACID63_32_PKTSLEEP_8822B)
#define BIT_CLEAR_MACID63_32_PKTSLEEP_8822B(x)                                 \
	((x) & (~BITS_MACID63_32_PKTSLEEP_8822B))
#define BIT_GET_MACID63_32_PKTSLEEP_8822B(x)                                   \
	(((x) >> BIT_SHIFT_MACID63_32_PKTSLEEP_8822B) &                        \
	 BIT_MASK_MACID63_32_PKTSLEEP_8822B)
#define BIT_SET_MACID63_32_PKTSLEEP_8822B(x, v)                                \
	(BIT_CLEAR_MACID63_32_PKTSLEEP_8822B(x) |                              \
	 BIT_MACID63_32_PKTSLEEP_8822B(v))

/* 2 REG_ARFR2_V1_8822B */

#define BIT_SHIFT_ARFR2_V1_8822B 0
#define BIT_MASK_ARFR2_V1_8822B 0xffffffffffffffffL
#define BIT_ARFR2_V1_8822B(x)                                                  \
	(((x) & BIT_MASK_ARFR2_V1_8822B) << BIT_SHIFT_ARFR2_V1_8822B)
#define BITS_ARFR2_V1_8822B                                                    \
	(BIT_MASK_ARFR2_V1_8822B << BIT_SHIFT_ARFR2_V1_8822B)
#define BIT_CLEAR_ARFR2_V1_8822B(x) ((x) & (~BITS_ARFR2_V1_8822B))
#define BIT_GET_ARFR2_V1_8822B(x)                                              \
	(((x) >> BIT_SHIFT_ARFR2_V1_8822B) & BIT_MASK_ARFR2_V1_8822B)
#define BIT_SET_ARFR2_V1_8822B(x, v)                                           \
	(BIT_CLEAR_ARFR2_V1_8822B(x) | BIT_ARFR2_V1_8822B(v))

/* 2 REG_ARFR3_V1_8822B */

#define BIT_SHIFT_ARFR3_V1_8822B 0
#define BIT_MASK_ARFR3_V1_8822B 0xffffffffffffffffL
#define BIT_ARFR3_V1_8822B(x)                                                  \
	(((x) & BIT_MASK_ARFR3_V1_8822B) << BIT_SHIFT_ARFR3_V1_8822B)
#define BITS_ARFR3_V1_8822B                                                    \
	(BIT_MASK_ARFR3_V1_8822B << BIT_SHIFT_ARFR3_V1_8822B)
#define BIT_CLEAR_ARFR3_V1_8822B(x) ((x) & (~BITS_ARFR3_V1_8822B))
#define BIT_GET_ARFR3_V1_8822B(x)                                              \
	(((x) >> BIT_SHIFT_ARFR3_V1_8822B) & BIT_MASK_ARFR3_V1_8822B)
#define BIT_SET_ARFR3_V1_8822B(x, v)                                           \
	(BIT_CLEAR_ARFR3_V1_8822B(x) | BIT_ARFR3_V1_8822B(v))

/* 2 REG_ARFR4_8822B */

#define BIT_SHIFT_ARFR4_8822B 0
#define BIT_MASK_ARFR4_8822B 0xffffffffffffffffL
#define BIT_ARFR4_8822B(x)                                                     \
	(((x) & BIT_MASK_ARFR4_8822B) << BIT_SHIFT_ARFR4_8822B)
#define BITS_ARFR4_8822B (BIT_MASK_ARFR4_8822B << BIT_SHIFT_ARFR4_8822B)
#define BIT_CLEAR_ARFR4_8822B(x) ((x) & (~BITS_ARFR4_8822B))
#define BIT_GET_ARFR4_8822B(x)                                                 \
	(((x) >> BIT_SHIFT_ARFR4_8822B) & BIT_MASK_ARFR4_8822B)
#define BIT_SET_ARFR4_8822B(x, v)                                              \
	(BIT_CLEAR_ARFR4_8822B(x) | BIT_ARFR4_8822B(v))

/* 2 REG_ARFR5_8822B */

#define BIT_SHIFT_ARFR5_8822B 0
#define BIT_MASK_ARFR5_8822B 0xffffffffffffffffL
#define BIT_ARFR5_8822B(x)                                                     \
	(((x) & BIT_MASK_ARFR5_8822B) << BIT_SHIFT_ARFR5_8822B)
#define BITS_ARFR5_8822B (BIT_MASK_ARFR5_8822B << BIT_SHIFT_ARFR5_8822B)
#define BIT_CLEAR_ARFR5_8822B(x) ((x) & (~BITS_ARFR5_8822B))
#define BIT_GET_ARFR5_8822B(x)                                                 \
	(((x) >> BIT_SHIFT_ARFR5_8822B) & BIT_MASK_ARFR5_8822B)
#define BIT_SET_ARFR5_8822B(x, v)                                              \
	(BIT_CLEAR_ARFR5_8822B(x) | BIT_ARFR5_8822B(v))

/* 2 REG_TXRPT_START_OFFSET_8822B */

#define BIT_SHIFT_MACID_MURATE_OFFSET_8822B 24
#define BIT_MASK_MACID_MURATE_OFFSET_8822B 0xff
#define BIT_MACID_MURATE_OFFSET_8822B(x)                                       \
	(((x) & BIT_MASK_MACID_MURATE_OFFSET_8822B)                            \
	 << BIT_SHIFT_MACID_MURATE_OFFSET_8822B)
#define BITS_MACID_MURATE_OFFSET_8822B                                         \
	(BIT_MASK_MACID_MURATE_OFFSET_8822B                                    \
	 << BIT_SHIFT_MACID_MURATE_OFFSET_8822B)
#define BIT_CLEAR_MACID_MURATE_OFFSET_8822B(x)                                 \
	((x) & (~BITS_MACID_MURATE_OFFSET_8822B))
#define BIT_GET_MACID_MURATE_OFFSET_8822B(x)                                   \
	(((x) >> BIT_SHIFT_MACID_MURATE_OFFSET_8822B) &                        \
	 BIT_MASK_MACID_MURATE_OFFSET_8822B)
#define BIT_SET_MACID_MURATE_OFFSET_8822B(x, v)                                \
	(BIT_CLEAR_MACID_MURATE_OFFSET_8822B(x) |                              \
	 BIT_MACID_MURATE_OFFSET_8822B(v))

#define BIT_RPTFIFO_SIZE_OPT_8822B BIT(16)

#define BIT_SHIFT_MACID_CTRL_OFFSET_8822B 8
#define BIT_MASK_MACID_CTRL_OFFSET_8822B 0xff
#define BIT_MACID_CTRL_OFFSET_8822B(x)                                         \
	(((x) & BIT_MASK_MACID_CTRL_OFFSET_8822B)                              \
	 << BIT_SHIFT_MACID_CTRL_OFFSET_8822B)
#define BITS_MACID_CTRL_OFFSET_8822B                                           \
	(BIT_MASK_MACID_CTRL_OFFSET_8822B << BIT_SHIFT_MACID_CTRL_OFFSET_8822B)
#define BIT_CLEAR_MACID_CTRL_OFFSET_8822B(x)                                   \
	((x) & (~BITS_MACID_CTRL_OFFSET_8822B))
#define BIT_GET_MACID_CTRL_OFFSET_8822B(x)                                     \
	(((x) >> BIT_SHIFT_MACID_CTRL_OFFSET_8822B) &                          \
	 BIT_MASK_MACID_CTRL_OFFSET_8822B)
#define BIT_SET_MACID_CTRL_OFFSET_8822B(x, v)                                  \
	(BIT_CLEAR_MACID_CTRL_OFFSET_8822B(x) | BIT_MACID_CTRL_OFFSET_8822B(v))

#define BIT_SHIFT_AMPDU_TXRPT_OFFSET_8822B 0
#define BIT_MASK_AMPDU_TXRPT_OFFSET_8822B 0xff
#define BIT_AMPDU_TXRPT_OFFSET_8822B(x)                                        \
	(((x) & BIT_MASK_AMPDU_TXRPT_OFFSET_8822B)                             \
	 << BIT_SHIFT_AMPDU_TXRPT_OFFSET_8822B)
#define BITS_AMPDU_TXRPT_OFFSET_8822B                                          \
	(BIT_MASK_AMPDU_TXRPT_OFFSET_8822B                                     \
	 << BIT_SHIFT_AMPDU_TXRPT_OFFSET_8822B)
#define BIT_CLEAR_AMPDU_TXRPT_OFFSET_8822B(x)                                  \
	((x) & (~BITS_AMPDU_TXRPT_OFFSET_8822B))
#define BIT_GET_AMPDU_TXRPT_OFFSET_8822B(x)                                    \
	(((x) >> BIT_SHIFT_AMPDU_TXRPT_OFFSET_8822B) &                         \
	 BIT_MASK_AMPDU_TXRPT_OFFSET_8822B)
#define BIT_SET_AMPDU_TXRPT_OFFSET_8822B(x, v)                                 \
	(BIT_CLEAR_AMPDU_TXRPT_OFFSET_8822B(x) |                               \
	 BIT_AMPDU_TXRPT_OFFSET_8822B(v))

/* 2 REG_POWER_STAGE1_8822B */
#define BIT_PTA_WL_PRI_MASK_CPU_MGQ_8822B BIT(31)
#define BIT_PTA_WL_PRI_MASK_BCNQ_8822B BIT(30)
#define BIT_PTA_WL_PRI_MASK_HIQ_8822B BIT(29)
#define BIT_PTA_WL_PRI_MASK_MGQ_8822B BIT(28)
#define BIT_PTA_WL_PRI_MASK_BK_8822B BIT(27)
#define BIT_PTA_WL_PRI_MASK_BE_8822B BIT(26)
#define BIT_PTA_WL_PRI_MASK_VI_8822B BIT(25)
#define BIT_PTA_WL_PRI_MASK_VO_8822B BIT(24)

#define BIT_SHIFT_POWER_STAGE1_8822B 0
#define BIT_MASK_POWER_STAGE1_8822B 0xffffff
#define BIT_POWER_STAGE1_8822B(x)                                              \
	(((x) & BIT_MASK_POWER_STAGE1_8822B) << BIT_SHIFT_POWER_STAGE1_8822B)
#define BITS_POWER_STAGE1_8822B                                                \
	(BIT_MASK_POWER_STAGE1_8822B << BIT_SHIFT_POWER_STAGE1_8822B)
#define BIT_CLEAR_POWER_STAGE1_8822B(x) ((x) & (~BITS_POWER_STAGE1_8822B))
#define BIT_GET_POWER_STAGE1_8822B(x)                                          \
	(((x) >> BIT_SHIFT_POWER_STAGE1_8822B) & BIT_MASK_POWER_STAGE1_8822B)
#define BIT_SET_POWER_STAGE1_8822B(x, v)                                       \
	(BIT_CLEAR_POWER_STAGE1_8822B(x) | BIT_POWER_STAGE1_8822B(v))

/* 2 REG_POWER_STAGE2_8822B */
#define BIT__R_CTRL_PKT_POW_ADJ_8822B BIT(24)

#define BIT_SHIFT_POWER_STAGE2_8822B 0
#define BIT_MASK_POWER_STAGE2_8822B 0xffffff
#define BIT_POWER_STAGE2_8822B(x)                                              \
	(((x) & BIT_MASK_POWER_STAGE2_8822B) << BIT_SHIFT_POWER_STAGE2_8822B)
#define BITS_POWER_STAGE2_8822B                                                \
	(BIT_MASK_POWER_STAGE2_8822B << BIT_SHIFT_POWER_STAGE2_8822B)
#define BIT_CLEAR_POWER_STAGE2_8822B(x) ((x) & (~BITS_POWER_STAGE2_8822B))
#define BIT_GET_POWER_STAGE2_8822B(x)                                          \
	(((x) >> BIT_SHIFT_POWER_STAGE2_8822B) & BIT_MASK_POWER_STAGE2_8822B)
#define BIT_SET_POWER_STAGE2_8822B(x, v)                                       \
	(BIT_CLEAR_POWER_STAGE2_8822B(x) | BIT_POWER_STAGE2_8822B(v))

/* 2 REG_SW_AMPDU_BURST_MODE_CTRL_8822B */

#define BIT_SHIFT_PAD_NUM_THRES_8822B 24
#define BIT_MASK_PAD_NUM_THRES_8822B 0x3f
#define BIT_PAD_NUM_THRES_8822B(x)                                             \
	(((x) & BIT_MASK_PAD_NUM_THRES_8822B) << BIT_SHIFT_PAD_NUM_THRES_8822B)
#define BITS_PAD_NUM_THRES_8822B                                               \
	(BIT_MASK_PAD_NUM_THRES_8822B << BIT_SHIFT_PAD_NUM_THRES_8822B)
#define BIT_CLEAR_PAD_NUM_THRES_8822B(x) ((x) & (~BITS_PAD_NUM_THRES_8822B))
#define BIT_GET_PAD_NUM_THRES_8822B(x)                                         \
	(((x) >> BIT_SHIFT_PAD_NUM_THRES_8822B) & BIT_MASK_PAD_NUM_THRES_8822B)
#define BIT_SET_PAD_NUM_THRES_8822B(x, v)                                      \
	(BIT_CLEAR_PAD_NUM_THRES_8822B(x) | BIT_PAD_NUM_THRES_8822B(v))

#define BIT_R_DMA_THIS_QUEUE_BK_8822B BIT(23)
#define BIT_R_DMA_THIS_QUEUE_BE_8822B BIT(22)
#define BIT_R_DMA_THIS_QUEUE_VI_8822B BIT(21)
#define BIT_R_DMA_THIS_QUEUE_VO_8822B BIT(20)

#define BIT_SHIFT_R_TOTAL_LEN_TH_8822B 8
#define BIT_MASK_R_TOTAL_LEN_TH_8822B 0xfff
#define BIT_R_TOTAL_LEN_TH_8822B(x)                                            \
	(((x) & BIT_MASK_R_TOTAL_LEN_TH_8822B)                                 \
	 << BIT_SHIFT_R_TOTAL_LEN_TH_8822B)
#define BITS_R_TOTAL_LEN_TH_8822B                                              \
	(BIT_MASK_R_TOTAL_LEN_TH_8822B << BIT_SHIFT_R_TOTAL_LEN_TH_8822B)
#define BIT_CLEAR_R_TOTAL_LEN_TH_8822B(x) ((x) & (~BITS_R_TOTAL_LEN_TH_8822B))
#define BIT_GET_R_TOTAL_LEN_TH_8822B(x)                                        \
	(((x) >> BIT_SHIFT_R_TOTAL_LEN_TH_8822B) &                             \
	 BIT_MASK_R_TOTAL_LEN_TH_8822B)
#define BIT_SET_R_TOTAL_LEN_TH_8822B(x, v)                                     \
	(BIT_CLEAR_R_TOTAL_LEN_TH_8822B(x) | BIT_R_TOTAL_LEN_TH_8822B(v))

#define BIT_EN_NEW_EARLY_8822B BIT(7)
#define BIT_PRE_TX_CMD_8822B BIT(6)

#define BIT_SHIFT_NUM_SCL_EN_8822B 4
#define BIT_MASK_NUM_SCL_EN_8822B 0x3
#define BIT_NUM_SCL_EN_8822B(x)                                                \
	(((x) & BIT_MASK_NUM_SCL_EN_8822B) << BIT_SHIFT_NUM_SCL_EN_8822B)
#define BITS_NUM_SCL_EN_8822B                                                  \
	(BIT_MASK_NUM_SCL_EN_8822B << BIT_SHIFT_NUM_SCL_EN_8822B)
#define BIT_CLEAR_NUM_SCL_EN_8822B(x) ((x) & (~BITS_NUM_SCL_EN_8822B))
#define BIT_GET_NUM_SCL_EN_8822B(x)                                            \
	(((x) >> BIT_SHIFT_NUM_SCL_EN_8822B) & BIT_MASK_NUM_SCL_EN_8822B)
#define BIT_SET_NUM_SCL_EN_8822B(x, v)                                         \
	(BIT_CLEAR_NUM_SCL_EN_8822B(x) | BIT_NUM_SCL_EN_8822B(v))

#define BIT_BK_EN_8822B BIT(3)
#define BIT_BE_EN_8822B BIT(2)
#define BIT_VI_EN_8822B BIT(1)
#define BIT_VO_EN_8822B BIT(0)

/* 2 REG_PKT_LIFE_TIME_8822B */

#define BIT_SHIFT_PKT_LIFTIME_BEBK_8822B 16
#define BIT_MASK_PKT_LIFTIME_BEBK_8822B 0xffff
#define BIT_PKT_LIFTIME_BEBK_8822B(x)                                          \
	(((x) & BIT_MASK_PKT_LIFTIME_BEBK_8822B)                               \
	 << BIT_SHIFT_PKT_LIFTIME_BEBK_8822B)
#define BITS_PKT_LIFTIME_BEBK_8822B                                            \
	(BIT_MASK_PKT_LIFTIME_BEBK_8822B << BIT_SHIFT_PKT_LIFTIME_BEBK_8822B)
#define BIT_CLEAR_PKT_LIFTIME_BEBK_8822B(x)                                    \
	((x) & (~BITS_PKT_LIFTIME_BEBK_8822B))
#define BIT_GET_PKT_LIFTIME_BEBK_8822B(x)                                      \
	(((x) >> BIT_SHIFT_PKT_LIFTIME_BEBK_8822B) &                           \
	 BIT_MASK_PKT_LIFTIME_BEBK_8822B)
#define BIT_SET_PKT_LIFTIME_BEBK_8822B(x, v)                                   \
	(BIT_CLEAR_PKT_LIFTIME_BEBK_8822B(x) | BIT_PKT_LIFTIME_BEBK_8822B(v))

#define BIT_SHIFT_PKT_LIFTIME_VOVI_8822B 0
#define BIT_MASK_PKT_LIFTIME_VOVI_8822B 0xffff
#define BIT_PKT_LIFTIME_VOVI_8822B(x)                                          \
	(((x) & BIT_MASK_PKT_LIFTIME_VOVI_8822B)                               \
	 << BIT_SHIFT_PKT_LIFTIME_VOVI_8822B)
#define BITS_PKT_LIFTIME_VOVI_8822B                                            \
	(BIT_MASK_PKT_LIFTIME_VOVI_8822B << BIT_SHIFT_PKT_LIFTIME_VOVI_8822B)
#define BIT_CLEAR_PKT_LIFTIME_VOVI_8822B(x)                                    \
	((x) & (~BITS_PKT_LIFTIME_VOVI_8822B))
#define BIT_GET_PKT_LIFTIME_VOVI_8822B(x)                                      \
	(((x) >> BIT_SHIFT_PKT_LIFTIME_VOVI_8822B) &                           \
	 BIT_MASK_PKT_LIFTIME_VOVI_8822B)
#define BIT_SET_PKT_LIFTIME_VOVI_8822B(x, v)                                   \
	(BIT_CLEAR_PKT_LIFTIME_VOVI_8822B(x) | BIT_PKT_LIFTIME_VOVI_8822B(v))

/* 2 REG_STBC_SETTING_8822B */

#define BIT_SHIFT_CDEND_TXTIME_L_8822B 4
#define BIT_MASK_CDEND_TXTIME_L_8822B 0xf
#define BIT_CDEND_TXTIME_L_8822B(x)                                            \
	(((x) & BIT_MASK_CDEND_TXTIME_L_8822B)                                 \
	 << BIT_SHIFT_CDEND_TXTIME_L_8822B)
#define BITS_CDEND_TXTIME_L_8822B                                              \
	(BIT_MASK_CDEND_TXTIME_L_8822B << BIT_SHIFT_CDEND_TXTIME_L_8822B)
#define BIT_CLEAR_CDEND_TXTIME_L_8822B(x) ((x) & (~BITS_CDEND_TXTIME_L_8822B))
#define BIT_GET_CDEND_TXTIME_L_8822B(x)                                        \
	(((x) >> BIT_SHIFT_CDEND_TXTIME_L_8822B) &                             \
	 BIT_MASK_CDEND_TXTIME_L_8822B)
#define BIT_SET_CDEND_TXTIME_L_8822B(x, v)                                     \
	(BIT_CLEAR_CDEND_TXTIME_L_8822B(x) | BIT_CDEND_TXTIME_L_8822B(v))

#define BIT_SHIFT_NESS_8822B 2
#define BIT_MASK_NESS_8822B 0x3
#define BIT_NESS_8822B(x) (((x) & BIT_MASK_NESS_8822B) << BIT_SHIFT_NESS_8822B)
#define BITS_NESS_8822B (BIT_MASK_NESS_8822B << BIT_SHIFT_NESS_8822B)
#define BIT_CLEAR_NESS_8822B(x) ((x) & (~BITS_NESS_8822B))
#define BIT_GET_NESS_8822B(x)                                                  \
	(((x) >> BIT_SHIFT_NESS_8822B) & BIT_MASK_NESS_8822B)
#define BIT_SET_NESS_8822B(x, v) (BIT_CLEAR_NESS_8822B(x) | BIT_NESS_8822B(v))

#define BIT_SHIFT_STBC_CFEND_8822B 0
#define BIT_MASK_STBC_CFEND_8822B 0x3
#define BIT_STBC_CFEND_8822B(x)                                                \
	(((x) & BIT_MASK_STBC_CFEND_8822B) << BIT_SHIFT_STBC_CFEND_8822B)
#define BITS_STBC_CFEND_8822B                                                  \
	(BIT_MASK_STBC_CFEND_8822B << BIT_SHIFT_STBC_CFEND_8822B)
#define BIT_CLEAR_STBC_CFEND_8822B(x) ((x) & (~BITS_STBC_CFEND_8822B))
#define BIT_GET_STBC_CFEND_8822B(x)                                            \
	(((x) >> BIT_SHIFT_STBC_CFEND_8822B) & BIT_MASK_STBC_CFEND_8822B)
#define BIT_SET_STBC_CFEND_8822B(x, v)                                         \
	(BIT_CLEAR_STBC_CFEND_8822B(x) | BIT_STBC_CFEND_8822B(v))

/* 2 REG_STBC_SETTING2_8822B */

#define BIT_SHIFT_CDEND_TXTIME_H_8822B 0
#define BIT_MASK_CDEND_TXTIME_H_8822B 0x1f
#define BIT_CDEND_TXTIME_H_8822B(x)                                            \
	(((x) & BIT_MASK_CDEND_TXTIME_H_8822B)                                 \
	 << BIT_SHIFT_CDEND_TXTIME_H_8822B)
#define BITS_CDEND_TXTIME_H_8822B                                              \
	(BIT_MASK_CDEND_TXTIME_H_8822B << BIT_SHIFT_CDEND_TXTIME_H_8822B)
#define BIT_CLEAR_CDEND_TXTIME_H_8822B(x) ((x) & (~BITS_CDEND_TXTIME_H_8822B))
#define BIT_GET_CDEND_TXTIME_H_8822B(x)                                        \
	(((x) >> BIT_SHIFT_CDEND_TXTIME_H_8822B) &                             \
	 BIT_MASK_CDEND_TXTIME_H_8822B)
#define BIT_SET_CDEND_TXTIME_H_8822B(x, v)                                     \
	(BIT_CLEAR_CDEND_TXTIME_H_8822B(x) | BIT_CDEND_TXTIME_H_8822B(v))

/* 2 REG_QUEUE_CTRL_8822B */
#define BIT_PTA_EDCCA_EN_8822B BIT(5)
#define BIT_PTA_WL_TX_EN_8822B BIT(4)
#define BIT_R_USE_DATA_BW_8822B BIT(3)
#define BIT_TRI_PKT_INT_MODE1_8822B BIT(2)
#define BIT_TRI_PKT_INT_MODE0_8822B BIT(1)
#define BIT_ACQ_MODE_SEL_8822B BIT(0)

/* 2 REG_SINGLE_AMPDU_CTRL_8822B */
#define BIT_EN_SINGLE_APMDU_8822B BIT(7)

/* 2 REG_PROT_MODE_CTRL_8822B */

#define BIT_SHIFT_RTS_MAX_AGG_NUM_8822B 24
#define BIT_MASK_RTS_MAX_AGG_NUM_8822B 0x3f
#define BIT_RTS_MAX_AGG_NUM_8822B(x)                                           \
	(((x) & BIT_MASK_RTS_MAX_AGG_NUM_8822B)                                \
	 << BIT_SHIFT_RTS_MAX_AGG_NUM_8822B)
#define BITS_RTS_MAX_AGG_NUM_8822B                                             \
	(BIT_MASK_RTS_MAX_AGG_NUM_8822B << BIT_SHIFT_RTS_MAX_AGG_NUM_8822B)
#define BIT_CLEAR_RTS_MAX_AGG_NUM_8822B(x) ((x) & (~BITS_RTS_MAX_AGG_NUM_8822B))
#define BIT_GET_RTS_MAX_AGG_NUM_8822B(x)                                       \
	(((x) >> BIT_SHIFT_RTS_MAX_AGG_NUM_8822B) &                            \
	 BIT_MASK_RTS_MAX_AGG_NUM_8822B)
#define BIT_SET_RTS_MAX_AGG_NUM_8822B(x, v)                                    \
	(BIT_CLEAR_RTS_MAX_AGG_NUM_8822B(x) | BIT_RTS_MAX_AGG_NUM_8822B(v))

#define BIT_SHIFT_MAX_AGG_NUM_8822B 16
#define BIT_MASK_MAX_AGG_NUM_8822B 0x3f
#define BIT_MAX_AGG_NUM_8822B(x)                                               \
	(((x) & BIT_MASK_MAX_AGG_NUM_8822B) << BIT_SHIFT_MAX_AGG_NUM_8822B)
#define BITS_MAX_AGG_NUM_8822B                                                 \
	(BIT_MASK_MAX_AGG_NUM_8822B << BIT_SHIFT_MAX_AGG_NUM_8822B)
#define BIT_CLEAR_MAX_AGG_NUM_8822B(x) ((x) & (~BITS_MAX_AGG_NUM_8822B))
#define BIT_GET_MAX_AGG_NUM_8822B(x)                                           \
	(((x) >> BIT_SHIFT_MAX_AGG_NUM_8822B) & BIT_MASK_MAX_AGG_NUM_8822B)
#define BIT_SET_MAX_AGG_NUM_8822B(x, v)                                        \
	(BIT_CLEAR_MAX_AGG_NUM_8822B(x) | BIT_MAX_AGG_NUM_8822B(v))

#define BIT_SHIFT_RTS_TXTIME_TH_8822B 8
#define BIT_MASK_RTS_TXTIME_TH_8822B 0xff
#define BIT_RTS_TXTIME_TH_8822B(x)                                             \
	(((x) & BIT_MASK_RTS_TXTIME_TH_8822B) << BIT_SHIFT_RTS_TXTIME_TH_8822B)
#define BITS_RTS_TXTIME_TH_8822B                                               \
	(BIT_MASK_RTS_TXTIME_TH_8822B << BIT_SHIFT_RTS_TXTIME_TH_8822B)
#define BIT_CLEAR_RTS_TXTIME_TH_8822B(x) ((x) & (~BITS_RTS_TXTIME_TH_8822B))
#define BIT_GET_RTS_TXTIME_TH_8822B(x)                                         \
	(((x) >> BIT_SHIFT_RTS_TXTIME_TH_8822B) & BIT_MASK_RTS_TXTIME_TH_8822B)
#define BIT_SET_RTS_TXTIME_TH_8822B(x, v)                                      \
	(BIT_CLEAR_RTS_TXTIME_TH_8822B(x) | BIT_RTS_TXTIME_TH_8822B(v))

#define BIT_SHIFT_RTS_LEN_TH_8822B 0
#define BIT_MASK_RTS_LEN_TH_8822B 0xff
#define BIT_RTS_LEN_TH_8822B(x)                                                \
	(((x) & BIT_MASK_RTS_LEN_TH_8822B) << BIT_SHIFT_RTS_LEN_TH_8822B)
#define BITS_RTS_LEN_TH_8822B                                                  \
	(BIT_MASK_RTS_LEN_TH_8822B << BIT_SHIFT_RTS_LEN_TH_8822B)
#define BIT_CLEAR_RTS_LEN_TH_8822B(x) ((x) & (~BITS_RTS_LEN_TH_8822B))
#define BIT_GET_RTS_LEN_TH_8822B(x)                                            \
	(((x) >> BIT_SHIFT_RTS_LEN_TH_8822B) & BIT_MASK_RTS_LEN_TH_8822B)
#define BIT_SET_RTS_LEN_TH_8822B(x, v)                                         \
	(BIT_CLEAR_RTS_LEN_TH_8822B(x) | BIT_RTS_LEN_TH_8822B(v))

/* 2 REG_BAR_MODE_CTRL_8822B */

#define BIT_SHIFT_BAR_RTY_LMT_8822B 16
#define BIT_MASK_BAR_RTY_LMT_8822B 0x3
#define BIT_BAR_RTY_LMT_8822B(x)                                               \
	(((x) & BIT_MASK_BAR_RTY_LMT_8822B) << BIT_SHIFT_BAR_RTY_LMT_8822B)
#define BITS_BAR_RTY_LMT_8822B                                                 \
	(BIT_MASK_BAR_RTY_LMT_8822B << BIT_SHIFT_BAR_RTY_LMT_8822B)
#define BIT_CLEAR_BAR_RTY_LMT_8822B(x) ((x) & (~BITS_BAR_RTY_LMT_8822B))
#define BIT_GET_BAR_RTY_LMT_8822B(x)                                           \
	(((x) >> BIT_SHIFT_BAR_RTY_LMT_8822B) & BIT_MASK_BAR_RTY_LMT_8822B)
#define BIT_SET_BAR_RTY_LMT_8822B(x, v)                                        \
	(BIT_CLEAR_BAR_RTY_LMT_8822B(x) | BIT_BAR_RTY_LMT_8822B(v))

#define BIT_SHIFT_BAR_PKT_TXTIME_TH_8822B 8
#define BIT_MASK_BAR_PKT_TXTIME_TH_8822B 0xff
#define BIT_BAR_PKT_TXTIME_TH_8822B(x)                                         \
	(((x) & BIT_MASK_BAR_PKT_TXTIME_TH_8822B)                              \
	 << BIT_SHIFT_BAR_PKT_TXTIME_TH_8822B)
#define BITS_BAR_PKT_TXTIME_TH_8822B                                           \
	(BIT_MASK_BAR_PKT_TXTIME_TH_8822B << BIT_SHIFT_BAR_PKT_TXTIME_TH_8822B)
#define BIT_CLEAR_BAR_PKT_TXTIME_TH_8822B(x)                                   \
	((x) & (~BITS_BAR_PKT_TXTIME_TH_8822B))
#define BIT_GET_BAR_PKT_TXTIME_TH_8822B(x)                                     \
	(((x) >> BIT_SHIFT_BAR_PKT_TXTIME_TH_8822B) &                          \
	 BIT_MASK_BAR_PKT_TXTIME_TH_8822B)
#define BIT_SET_BAR_PKT_TXTIME_TH_8822B(x, v)                                  \
	(BIT_CLEAR_BAR_PKT_TXTIME_TH_8822B(x) | BIT_BAR_PKT_TXTIME_TH_8822B(v))

#define BIT_BAR_EN_V1_8822B BIT(6)

#define BIT_SHIFT_BAR_PKTNUM_TH_V1_8822B 0
#define BIT_MASK_BAR_PKTNUM_TH_V1_8822B 0x3f
#define BIT_BAR_PKTNUM_TH_V1_8822B(x)                                          \
	(((x) & BIT_MASK_BAR_PKTNUM_TH_V1_8822B)                               \
	 << BIT_SHIFT_BAR_PKTNUM_TH_V1_8822B)
#define BITS_BAR_PKTNUM_TH_V1_8822B                                            \
	(BIT_MASK_BAR_PKTNUM_TH_V1_8822B << BIT_SHIFT_BAR_PKTNUM_TH_V1_8822B)
#define BIT_CLEAR_BAR_PKTNUM_TH_V1_8822B(x)                                    \
	((x) & (~BITS_BAR_PKTNUM_TH_V1_8822B))
#define BIT_GET_BAR_PKTNUM_TH_V1_8822B(x)                                      \
	(((x) >> BIT_SHIFT_BAR_PKTNUM_TH_V1_8822B) &                           \
	 BIT_MASK_BAR_PKTNUM_TH_V1_8822B)
#define BIT_SET_BAR_PKTNUM_TH_V1_8822B(x, v)                                   \
	(BIT_CLEAR_BAR_PKTNUM_TH_V1_8822B(x) | BIT_BAR_PKTNUM_TH_V1_8822B(v))

/* 2 REG_RA_TRY_RATE_AGG_LMT_8822B */

#define BIT_SHIFT_RA_TRY_RATE_AGG_LMT_V1_8822B 0
#define BIT_MASK_RA_TRY_RATE_AGG_LMT_V1_8822B 0x3f
#define BIT_RA_TRY_RATE_AGG_LMT_V1_8822B(x)                                    \
	(((x) & BIT_MASK_RA_TRY_RATE_AGG_LMT_V1_8822B)                         \
	 << BIT_SHIFT_RA_TRY_RATE_AGG_LMT_V1_8822B)
#define BITS_RA_TRY_RATE_AGG_LMT_V1_8822B                                      \
	(BIT_MASK_RA_TRY_RATE_AGG_LMT_V1_8822B                                 \
	 << BIT_SHIFT_RA_TRY_RATE_AGG_LMT_V1_8822B)
#define BIT_CLEAR_RA_TRY_RATE_AGG_LMT_V1_8822B(x)                              \
	((x) & (~BITS_RA_TRY_RATE_AGG_LMT_V1_8822B))
#define BIT_GET_RA_TRY_RATE_AGG_LMT_V1_8822B(x)                                \
	(((x) >> BIT_SHIFT_RA_TRY_RATE_AGG_LMT_V1_8822B) &                     \
	 BIT_MASK_RA_TRY_RATE_AGG_LMT_V1_8822B)
#define BIT_SET_RA_TRY_RATE_AGG_LMT_V1_8822B(x, v)                             \
	(BIT_CLEAR_RA_TRY_RATE_AGG_LMT_V1_8822B(x) |                           \
	 BIT_RA_TRY_RATE_AGG_LMT_V1_8822B(v))

/* 2 REG_MACID_SLEEP2_8822B */

#define BIT_SHIFT_MACID95_64PKTSLEEP_8822B 0
#define BIT_MASK_MACID95_64PKTSLEEP_8822B 0xffffffffL
#define BIT_MACID95_64PKTSLEEP_8822B(x)                                        \
	(((x) & BIT_MASK_MACID95_64PKTSLEEP_8822B)                             \
	 << BIT_SHIFT_MACID95_64PKTSLEEP_8822B)
#define BITS_MACID95_64PKTSLEEP_8822B                                          \
	(BIT_MASK_MACID95_64PKTSLEEP_8822B                                     \
	 << BIT_SHIFT_MACID95_64PKTSLEEP_8822B)
#define BIT_CLEAR_MACID95_64PKTSLEEP_8822B(x)                                  \
	((x) & (~BITS_MACID95_64PKTSLEEP_8822B))
#define BIT_GET_MACID95_64PKTSLEEP_8822B(x)                                    \
	(((x) >> BIT_SHIFT_MACID95_64PKTSLEEP_8822B) &                         \
	 BIT_MASK_MACID95_64PKTSLEEP_8822B)
#define BIT_SET_MACID95_64PKTSLEEP_8822B(x, v)                                 \
	(BIT_CLEAR_MACID95_64PKTSLEEP_8822B(x) |                               \
	 BIT_MACID95_64PKTSLEEP_8822B(v))

/* 2 REG_MACID_SLEEP_8822B */

#define BIT_SHIFT_MACID31_0_PKTSLEEP_8822B 0
#define BIT_MASK_MACID31_0_PKTSLEEP_8822B 0xffffffffL
#define BIT_MACID31_0_PKTSLEEP_8822B(x)                                        \
	(((x) & BIT_MASK_MACID31_0_PKTSLEEP_8822B)                             \
	 << BIT_SHIFT_MACID31_0_PKTSLEEP_8822B)
#define BITS_MACID31_0_PKTSLEEP_8822B                                          \
	(BIT_MASK_MACID31_0_PKTSLEEP_8822B                                     \
	 << BIT_SHIFT_MACID31_0_PKTSLEEP_8822B)
#define BIT_CLEAR_MACID31_0_PKTSLEEP_8822B(x)                                  \
	((x) & (~BITS_MACID31_0_PKTSLEEP_8822B))
#define BIT_GET_MACID31_0_PKTSLEEP_8822B(x)                                    \
	(((x) >> BIT_SHIFT_MACID31_0_PKTSLEEP_8822B) &                         \
	 BIT_MASK_MACID31_0_PKTSLEEP_8822B)
#define BIT_SET_MACID31_0_PKTSLEEP_8822B(x, v)                                 \
	(BIT_CLEAR_MACID31_0_PKTSLEEP_8822B(x) |                               \
	 BIT_MACID31_0_PKTSLEEP_8822B(v))

/* 2 REG_HW_SEQ0_8822B */

#define BIT_SHIFT_HW_SSN_SEQ0_8822B 0
#define BIT_MASK_HW_SSN_SEQ0_8822B 0xfff
#define BIT_HW_SSN_SEQ0_8822B(x)                                               \
	(((x) & BIT_MASK_HW_SSN_SEQ0_8822B) << BIT_SHIFT_HW_SSN_SEQ0_8822B)
#define BITS_HW_SSN_SEQ0_8822B                                                 \
	(BIT_MASK_HW_SSN_SEQ0_8822B << BIT_SHIFT_HW_SSN_SEQ0_8822B)
#define BIT_CLEAR_HW_SSN_SEQ0_8822B(x) ((x) & (~BITS_HW_SSN_SEQ0_8822B))
#define BIT_GET_HW_SSN_SEQ0_8822B(x)                                           \
	(((x) >> BIT_SHIFT_HW_SSN_SEQ0_8822B) & BIT_MASK_HW_SSN_SEQ0_8822B)
#define BIT_SET_HW_SSN_SEQ0_8822B(x, v)                                        \
	(BIT_CLEAR_HW_SSN_SEQ0_8822B(x) | BIT_HW_SSN_SEQ0_8822B(v))

/* 2 REG_HW_SEQ1_8822B */

#define BIT_SHIFT_HW_SSN_SEQ1_8822B 0
#define BIT_MASK_HW_SSN_SEQ1_8822B 0xfff
#define BIT_HW_SSN_SEQ1_8822B(x)                                               \
	(((x) & BIT_MASK_HW_SSN_SEQ1_8822B) << BIT_SHIFT_HW_SSN_SEQ1_8822B)
#define BITS_HW_SSN_SEQ1_8822B                                                 \
	(BIT_MASK_HW_SSN_SEQ1_8822B << BIT_SHIFT_HW_SSN_SEQ1_8822B)
#define BIT_CLEAR_HW_SSN_SEQ1_8822B(x) ((x) & (~BITS_HW_SSN_SEQ1_8822B))
#define BIT_GET_HW_SSN_SEQ1_8822B(x)                                           \
	(((x) >> BIT_SHIFT_HW_SSN_SEQ1_8822B) & BIT_MASK_HW_SSN_SEQ1_8822B)
#define BIT_SET_HW_SSN_SEQ1_8822B(x, v)                                        \
	(BIT_CLEAR_HW_SSN_SEQ1_8822B(x) | BIT_HW_SSN_SEQ1_8822B(v))

/* 2 REG_HW_SEQ2_8822B */

#define BIT_SHIFT_HW_SSN_SEQ2_8822B 0
#define BIT_MASK_HW_SSN_SEQ2_8822B 0xfff
#define BIT_HW_SSN_SEQ2_8822B(x)                                               \
	(((x) & BIT_MASK_HW_SSN_SEQ2_8822B) << BIT_SHIFT_HW_SSN_SEQ2_8822B)
#define BITS_HW_SSN_SEQ2_8822B                                                 \
	(BIT_MASK_HW_SSN_SEQ2_8822B << BIT_SHIFT_HW_SSN_SEQ2_8822B)
#define BIT_CLEAR_HW_SSN_SEQ2_8822B(x) ((x) & (~BITS_HW_SSN_SEQ2_8822B))
#define BIT_GET_HW_SSN_SEQ2_8822B(x)                                           \
	(((x) >> BIT_SHIFT_HW_SSN_SEQ2_8822B) & BIT_MASK_HW_SSN_SEQ2_8822B)
#define BIT_SET_HW_SSN_SEQ2_8822B(x, v)                                        \
	(BIT_CLEAR_HW_SSN_SEQ2_8822B(x) | BIT_HW_SSN_SEQ2_8822B(v))

/* 2 REG_HW_SEQ3_8822B */

#define BIT_SHIFT_HW_SSN_SEQ3_8822B 0
#define BIT_MASK_HW_SSN_SEQ3_8822B 0xfff
#define BIT_HW_SSN_SEQ3_8822B(x)                                               \
	(((x) & BIT_MASK_HW_SSN_SEQ3_8822B) << BIT_SHIFT_HW_SSN_SEQ3_8822B)
#define BITS_HW_SSN_SEQ3_8822B                                                 \
	(BIT_MASK_HW_SSN_SEQ3_8822B << BIT_SHIFT_HW_SSN_SEQ3_8822B)
#define BIT_CLEAR_HW_SSN_SEQ3_8822B(x) ((x) & (~BITS_HW_SSN_SEQ3_8822B))
#define BIT_GET_HW_SSN_SEQ3_8822B(x)                                           \
	(((x) >> BIT_SHIFT_HW_SSN_SEQ3_8822B) & BIT_MASK_HW_SSN_SEQ3_8822B)
#define BIT_SET_HW_SSN_SEQ3_8822B(x, v)                                        \
	(BIT_CLEAR_HW_SSN_SEQ3_8822B(x) | BIT_HW_SSN_SEQ3_8822B(v))

/* 2 REG_NULL_PKT_STATUS_V1_8822B */

#define BIT_SHIFT_PTCL_TOTAL_PG_V2_8822B 2
#define BIT_MASK_PTCL_TOTAL_PG_V2_8822B 0x3fff
#define BIT_PTCL_TOTAL_PG_V2_8822B(x)                                          \
	(((x) & BIT_MASK_PTCL_TOTAL_PG_V2_8822B)                               \
	 << BIT_SHIFT_PTCL_TOTAL_PG_V2_8822B)
#define BITS_PTCL_TOTAL_PG_V2_8822B                                            \
	(BIT_MASK_PTCL_TOTAL_PG_V2_8822B << BIT_SHIFT_PTCL_TOTAL_PG_V2_8822B)
#define BIT_CLEAR_PTCL_TOTAL_PG_V2_8822B(x)                                    \
	((x) & (~BITS_PTCL_TOTAL_PG_V2_8822B))
#define BIT_GET_PTCL_TOTAL_PG_V2_8822B(x)                                      \
	(((x) >> BIT_SHIFT_PTCL_TOTAL_PG_V2_8822B) &                           \
	 BIT_MASK_PTCL_TOTAL_PG_V2_8822B)
#define BIT_SET_PTCL_TOTAL_PG_V2_8822B(x, v)                                   \
	(BIT_CLEAR_PTCL_TOTAL_PG_V2_8822B(x) | BIT_PTCL_TOTAL_PG_V2_8822B(v))

#define BIT_TX_NULL_1_8822B BIT(1)
#define BIT_TX_NULL_0_8822B BIT(0)

/* 2 REG_PTCL_ERR_STATUS_8822B */
#define BIT_PTCL_RATE_TABLE_INVALID_8822B BIT(7)
#define BIT_FTM_T2R_ERROR_8822B BIT(6)
#define BIT_PTCL_ERR0_8822B BIT(5)
#define BIT_PTCL_ERR1_8822B BIT(4)
#define BIT_PTCL_ERR2_8822B BIT(3)
#define BIT_PTCL_ERR3_8822B BIT(2)
#define BIT_PTCL_ERR4_8822B BIT(1)
#define BIT_PTCL_ERR5_8822B BIT(0)

/* 2 REG_NULL_PKT_STATUS_EXTEND_8822B */
#define BIT_CLI3_TX_NULL_1_8822B BIT(7)
#define BIT_CLI3_TX_NULL_0_8822B BIT(6)
#define BIT_CLI2_TX_NULL_1_8822B BIT(5)
#define BIT_CLI2_TX_NULL_0_8822B BIT(4)
#define BIT_CLI1_TX_NULL_1_8822B BIT(3)
#define BIT_CLI1_TX_NULL_0_8822B BIT(2)
#define BIT_CLI0_TX_NULL_1_8822B BIT(1)
#define BIT_CLI0_TX_NULL_0_8822B BIT(0)

/* 2 REG_VIDEO_ENHANCEMENT_FUN_8822B */
#define BIT_VIDEO_JUST_DROP_8822B BIT(1)
#define BIT_VIDEO_ENHANCEMENT_FUN_EN_8822B BIT(0)

/* 2 REG_BT_POLLUTE_PKT_CNT_8822B */

#define BIT_SHIFT_BT_POLLUTE_PKT_CNT_8822B 0
#define BIT_MASK_BT_POLLUTE_PKT_CNT_8822B 0xffff
#define BIT_BT_POLLUTE_PKT_CNT_8822B(x)                                        \
	(((x) & BIT_MASK_BT_POLLUTE_PKT_CNT_8822B)                             \
	 << BIT_SHIFT_BT_POLLUTE_PKT_CNT_8822B)
#define BITS_BT_POLLUTE_PKT_CNT_8822B                                          \
	(BIT_MASK_BT_POLLUTE_PKT_CNT_8822B                                     \
	 << BIT_SHIFT_BT_POLLUTE_PKT_CNT_8822B)
#define BIT_CLEAR_BT_POLLUTE_PKT_CNT_8822B(x)                                  \
	((x) & (~BITS_BT_POLLUTE_PKT_CNT_8822B))
#define BIT_GET_BT_POLLUTE_PKT_CNT_8822B(x)                                    \
	(((x) >> BIT_SHIFT_BT_POLLUTE_PKT_CNT_8822B) &                         \
	 BIT_MASK_BT_POLLUTE_PKT_CNT_8822B)
#define BIT_SET_BT_POLLUTE_PKT_CNT_8822B(x, v)                                 \
	(BIT_CLEAR_BT_POLLUTE_PKT_CNT_8822B(x) |                               \
	 BIT_BT_POLLUTE_PKT_CNT_8822B(v))

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_PTCL_DBG_8822B */

#define BIT_SHIFT_PTCL_DBG_8822B 0
#define BIT_MASK_PTCL_DBG_8822B 0xffffffffL
#define BIT_PTCL_DBG_8822B(x)                                                  \
	(((x) & BIT_MASK_PTCL_DBG_8822B) << BIT_SHIFT_PTCL_DBG_8822B)
#define BITS_PTCL_DBG_8822B                                                    \
	(BIT_MASK_PTCL_DBG_8822B << BIT_SHIFT_PTCL_DBG_8822B)
#define BIT_CLEAR_PTCL_DBG_8822B(x) ((x) & (~BITS_PTCL_DBG_8822B))
#define BIT_GET_PTCL_DBG_8822B(x)                                              \
	(((x) >> BIT_SHIFT_PTCL_DBG_8822B) & BIT_MASK_PTCL_DBG_8822B)
#define BIT_SET_PTCL_DBG_8822B(x, v)                                           \
	(BIT_CLEAR_PTCL_DBG_8822B(x) | BIT_PTCL_DBG_8822B(v))

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_CPUMGQ_TIMER_CTRL2_8822B */

#define BIT_SHIFT_TRI_HEAD_ADDR_8822B 16
#define BIT_MASK_TRI_HEAD_ADDR_8822B 0xfff
#define BIT_TRI_HEAD_ADDR_8822B(x)                                             \
	(((x) & BIT_MASK_TRI_HEAD_ADDR_8822B) << BIT_SHIFT_TRI_HEAD_ADDR_8822B)
#define BITS_TRI_HEAD_ADDR_8822B                                               \
	(BIT_MASK_TRI_HEAD_ADDR_8822B << BIT_SHIFT_TRI_HEAD_ADDR_8822B)
#define BIT_CLEAR_TRI_HEAD_ADDR_8822B(x) ((x) & (~BITS_TRI_HEAD_ADDR_8822B))
#define BIT_GET_TRI_HEAD_ADDR_8822B(x)                                         \
	(((x) >> BIT_SHIFT_TRI_HEAD_ADDR_8822B) & BIT_MASK_TRI_HEAD_ADDR_8822B)
#define BIT_SET_TRI_HEAD_ADDR_8822B(x, v)                                      \
	(BIT_CLEAR_TRI_HEAD_ADDR_8822B(x) | BIT_TRI_HEAD_ADDR_8822B(v))

#define BIT_DROP_TH_EN_8822B BIT(8)

#define BIT_SHIFT_DROP_TH_8822B 0
#define BIT_MASK_DROP_TH_8822B 0xff
#define BIT_DROP_TH_8822B(x)                                                   \
	(((x) & BIT_MASK_DROP_TH_8822B) << BIT_SHIFT_DROP_TH_8822B)
#define BITS_DROP_TH_8822B (BIT_MASK_DROP_TH_8822B << BIT_SHIFT_DROP_TH_8822B)
#define BIT_CLEAR_DROP_TH_8822B(x) ((x) & (~BITS_DROP_TH_8822B))
#define BIT_GET_DROP_TH_8822B(x)                                               \
	(((x) >> BIT_SHIFT_DROP_TH_8822B) & BIT_MASK_DROP_TH_8822B)
#define BIT_SET_DROP_TH_8822B(x, v)                                            \
	(BIT_CLEAR_DROP_TH_8822B(x) | BIT_DROP_TH_8822B(v))

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_DUMMY_PAGE4_V1_8822B */
#define BIT_BCN_EN_EXTHWSEQ_8822B BIT(1)
#define BIT_BCN_EN_HWSEQ_8822B BIT(0)

/* 2 REG_MOREDATA_8822B */
#define BIT_MOREDATA_CTRL2_EN_V1_8822B BIT(3)
#define BIT_MOREDATA_CTRL1_EN_V1_8822B BIT(2)
#define BIT_PKTIN_MOREDATA_REPLACE_ENABLE_V1_8822B BIT(0)

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_Q0_Q1_INFO_8822B */
#define BIT_QUEUE_MACID_AC_NOT_THE_SAME_8822B BIT(31)

#define BIT_SHIFT_GTAB_ID_8822B 28
#define BIT_MASK_GTAB_ID_8822B 0x7
#define BIT_GTAB_ID_8822B(x)                                                   \
	(((x) & BIT_MASK_GTAB_ID_8822B) << BIT_SHIFT_GTAB_ID_8822B)
#define BITS_GTAB_ID_8822B (BIT_MASK_GTAB_ID_8822B << BIT_SHIFT_GTAB_ID_8822B)
#define BIT_CLEAR_GTAB_ID_8822B(x) ((x) & (~BITS_GTAB_ID_8822B))
#define BIT_GET_GTAB_ID_8822B(x)                                               \
	(((x) >> BIT_SHIFT_GTAB_ID_8822B) & BIT_MASK_GTAB_ID_8822B)
#define BIT_SET_GTAB_ID_8822B(x, v)                                            \
	(BIT_CLEAR_GTAB_ID_8822B(x) | BIT_GTAB_ID_8822B(v))

#define BIT_SHIFT_AC1_PKT_INFO_8822B 16
#define BIT_MASK_AC1_PKT_INFO_8822B 0xfff
#define BIT_AC1_PKT_INFO_8822B(x)                                              \
	(((x) & BIT_MASK_AC1_PKT_INFO_8822B) << BIT_SHIFT_AC1_PKT_INFO_8822B)
#define BITS_AC1_PKT_INFO_8822B                                                \
	(BIT_MASK_AC1_PKT_INFO_8822B << BIT_SHIFT_AC1_PKT_INFO_8822B)
#define BIT_CLEAR_AC1_PKT_INFO_8822B(x) ((x) & (~BITS_AC1_PKT_INFO_8822B))
#define BIT_GET_AC1_PKT_INFO_8822B(x)                                          \
	(((x) >> BIT_SHIFT_AC1_PKT_INFO_8822B) & BIT_MASK_AC1_PKT_INFO_8822B)
#define BIT_SET_AC1_PKT_INFO_8822B(x, v)                                       \
	(BIT_CLEAR_AC1_PKT_INFO_8822B(x) | BIT_AC1_PKT_INFO_8822B(v))

#define BIT_QUEUE_MACID_AC_NOT_THE_SAME_V1_8822B BIT(15)

#define BIT_SHIFT_GTAB_ID_V1_8822B 12
#define BIT_MASK_GTAB_ID_V1_8822B 0x7
#define BIT_GTAB_ID_V1_8822B(x)                                                \
	(((x) & BIT_MASK_GTAB_ID_V1_8822B) << BIT_SHIFT_GTAB_ID_V1_8822B)
#define BITS_GTAB_ID_V1_8822B                                                  \
	(BIT_MASK_GTAB_ID_V1_8822B << BIT_SHIFT_GTAB_ID_V1_8822B)
#define BIT_CLEAR_GTAB_ID_V1_8822B(x) ((x) & (~BITS_GTAB_ID_V1_8822B))
#define BIT_GET_GTAB_ID_V1_8822B(x)                                            \
	(((x) >> BIT_SHIFT_GTAB_ID_V1_8822B) & BIT_MASK_GTAB_ID_V1_8822B)
#define BIT_SET_GTAB_ID_V1_8822B(x, v)                                         \
	(BIT_CLEAR_GTAB_ID_V1_8822B(x) | BIT_GTAB_ID_V1_8822B(v))

#define BIT_SHIFT_AC0_PKT_INFO_8822B 0
#define BIT_MASK_AC0_PKT_INFO_8822B 0xfff
#define BIT_AC0_PKT_INFO_8822B(x)                                              \
	(((x) & BIT_MASK_AC0_PKT_INFO_8822B) << BIT_SHIFT_AC0_PKT_INFO_8822B)
#define BITS_AC0_PKT_INFO_8822B                                                \
	(BIT_MASK_AC0_PKT_INFO_8822B << BIT_SHIFT_AC0_PKT_INFO_8822B)
#define BIT_CLEAR_AC0_PKT_INFO_8822B(x) ((x) & (~BITS_AC0_PKT_INFO_8822B))
#define BIT_GET_AC0_PKT_INFO_8822B(x)                                          \
	(((x) >> BIT_SHIFT_AC0_PKT_INFO_8822B) & BIT_MASK_AC0_PKT_INFO_8822B)
#define BIT_SET_AC0_PKT_INFO_8822B(x, v)                                       \
	(BIT_CLEAR_AC0_PKT_INFO_8822B(x) | BIT_AC0_PKT_INFO_8822B(v))

/* 2 REG_Q2_Q3_INFO_8822B */
#define BIT_QUEUE_MACID_AC_NOT_THE_SAME_8822B BIT(31)

#define BIT_SHIFT_GTAB_ID_8822B 28
#define BIT_MASK_GTAB_ID_8822B 0x7
#define BIT_GTAB_ID_8822B(x)                                                   \
	(((x) & BIT_MASK_GTAB_ID_8822B) << BIT_SHIFT_GTAB_ID_8822B)
#define BITS_GTAB_ID_8822B (BIT_MASK_GTAB_ID_8822B << BIT_SHIFT_GTAB_ID_8822B)
#define BIT_CLEAR_GTAB_ID_8822B(x) ((x) & (~BITS_GTAB_ID_8822B))
#define BIT_GET_GTAB_ID_8822B(x)                                               \
	(((x) >> BIT_SHIFT_GTAB_ID_8822B) & BIT_MASK_GTAB_ID_8822B)
#define BIT_SET_GTAB_ID_8822B(x, v)                                            \
	(BIT_CLEAR_GTAB_ID_8822B(x) | BIT_GTAB_ID_8822B(v))

#define BIT_SHIFT_AC3_PKT_INFO_8822B 16
#define BIT_MASK_AC3_PKT_INFO_8822B 0xfff
#define BIT_AC3_PKT_INFO_8822B(x)                                              \
	(((x) & BIT_MASK_AC3_PKT_INFO_8822B) << BIT_SHIFT_AC3_PKT_INFO_8822B)
#define BITS_AC3_PKT_INFO_8822B                                                \
	(BIT_MASK_AC3_PKT_INFO_8822B << BIT_SHIFT_AC3_PKT_INFO_8822B)
#define BIT_CLEAR_AC3_PKT_INFO_8822B(x) ((x) & (~BITS_AC3_PKT_INFO_8822B))
#define BIT_GET_AC3_PKT_INFO_8822B(x)                                          \
	(((x) >> BIT_SHIFT_AC3_PKT_INFO_8822B) & BIT_MASK_AC3_PKT_INFO_8822B)
#define BIT_SET_AC3_PKT_INFO_8822B(x, v)                                       \
	(BIT_CLEAR_AC3_PKT_INFO_8822B(x) | BIT_AC3_PKT_INFO_8822B(v))

#define BIT_QUEUE_MACID_AC_NOT_THE_SAME_V1_8822B BIT(15)

#define BIT_SHIFT_GTAB_ID_V1_8822B 12
#define BIT_MASK_GTAB_ID_V1_8822B 0x7
#define BIT_GTAB_ID_V1_8822B(x)                                                \
	(((x) & BIT_MASK_GTAB_ID_V1_8822B) << BIT_SHIFT_GTAB_ID_V1_8822B)
#define BITS_GTAB_ID_V1_8822B                                                  \
	(BIT_MASK_GTAB_ID_V1_8822B << BIT_SHIFT_GTAB_ID_V1_8822B)
#define BIT_CLEAR_GTAB_ID_V1_8822B(x) ((x) & (~BITS_GTAB_ID_V1_8822B))
#define BIT_GET_GTAB_ID_V1_8822B(x)                                            \
	(((x) >> BIT_SHIFT_GTAB_ID_V1_8822B) & BIT_MASK_GTAB_ID_V1_8822B)
#define BIT_SET_GTAB_ID_V1_8822B(x, v)                                         \
	(BIT_CLEAR_GTAB_ID_V1_8822B(x) | BIT_GTAB_ID_V1_8822B(v))

#define BIT_SHIFT_AC2_PKT_INFO_8822B 0
#define BIT_MASK_AC2_PKT_INFO_8822B 0xfff
#define BIT_AC2_PKT_INFO_8822B(x)                                              \
	(((x) & BIT_MASK_AC2_PKT_INFO_8822B) << BIT_SHIFT_AC2_PKT_INFO_8822B)
#define BITS_AC2_PKT_INFO_8822B                                                \
	(BIT_MASK_AC2_PKT_INFO_8822B << BIT_SHIFT_AC2_PKT_INFO_8822B)
#define BIT_CLEAR_AC2_PKT_INFO_8822B(x) ((x) & (~BITS_AC2_PKT_INFO_8822B))
#define BIT_GET_AC2_PKT_INFO_8822B(x)                                          \
	(((x) >> BIT_SHIFT_AC2_PKT_INFO_8822B) & BIT_MASK_AC2_PKT_INFO_8822B)
#define BIT_SET_AC2_PKT_INFO_8822B(x, v)                                       \
	(BIT_CLEAR_AC2_PKT_INFO_8822B(x) | BIT_AC2_PKT_INFO_8822B(v))

/* 2 REG_Q4_Q5_INFO_8822B */
#define BIT_QUEUE_MACID_AC_NOT_THE_SAME_8822B BIT(31)

#define BIT_SHIFT_GTAB_ID_8822B 28
#define BIT_MASK_GTAB_ID_8822B 0x7
#define BIT_GTAB_ID_8822B(x)                                                   \
	(((x) & BIT_MASK_GTAB_ID_8822B) << BIT_SHIFT_GTAB_ID_8822B)
#define BITS_GTAB_ID_8822B (BIT_MASK_GTAB_ID_8822B << BIT_SHIFT_GTAB_ID_8822B)
#define BIT_CLEAR_GTAB_ID_8822B(x) ((x) & (~BITS_GTAB_ID_8822B))
#define BIT_GET_GTAB_ID_8822B(x)                                               \
	(((x) >> BIT_SHIFT_GTAB_ID_8822B) & BIT_MASK_GTAB_ID_8822B)
#define BIT_SET_GTAB_ID_8822B(x, v)                                            \
	(BIT_CLEAR_GTAB_ID_8822B(x) | BIT_GTAB_ID_8822B(v))

#define BIT_SHIFT_AC5_PKT_INFO_8822B 16
#define BIT_MASK_AC5_PKT_INFO_8822B 0xfff
#define BIT_AC5_PKT_INFO_8822B(x)                                              \
	(((x) & BIT_MASK_AC5_PKT_INFO_8822B) << BIT_SHIFT_AC5_PKT_INFO_8822B)
#define BITS_AC5_PKT_INFO_8822B                                                \
	(BIT_MASK_AC5_PKT_INFO_8822B << BIT_SHIFT_AC5_PKT_INFO_8822B)
#define BIT_CLEAR_AC5_PKT_INFO_8822B(x) ((x) & (~BITS_AC5_PKT_INFO_8822B))
#define BIT_GET_AC5_PKT_INFO_8822B(x)                                          \
	(((x) >> BIT_SHIFT_AC5_PKT_INFO_8822B) & BIT_MASK_AC5_PKT_INFO_8822B)
#define BIT_SET_AC5_PKT_INFO_8822B(x, v)                                       \
	(BIT_CLEAR_AC5_PKT_INFO_8822B(x) | BIT_AC5_PKT_INFO_8822B(v))

#define BIT_QUEUE_MACID_AC_NOT_THE_SAME_V1_8822B BIT(15)

#define BIT_SHIFT_GTAB_ID_V1_8822B 12
#define BIT_MASK_GTAB_ID_V1_8822B 0x7
#define BIT_GTAB_ID_V1_8822B(x)                                                \
	(((x) & BIT_MASK_GTAB_ID_V1_8822B) << BIT_SHIFT_GTAB_ID_V1_8822B)
#define BITS_GTAB_ID_V1_8822B                                                  \
	(BIT_MASK_GTAB_ID_V1_8822B << BIT_SHIFT_GTAB_ID_V1_8822B)
#define BIT_CLEAR_GTAB_ID_V1_8822B(x) ((x) & (~BITS_GTAB_ID_V1_8822B))
#define BIT_GET_GTAB_ID_V1_8822B(x)                                            \
	(((x) >> BIT_SHIFT_GTAB_ID_V1_8822B) & BIT_MASK_GTAB_ID_V1_8822B)
#define BIT_SET_GTAB_ID_V1_8822B(x, v)                                         \
	(BIT_CLEAR_GTAB_ID_V1_8822B(x) | BIT_GTAB_ID_V1_8822B(v))

#define BIT_SHIFT_AC4_PKT_INFO_8822B 0
#define BIT_MASK_AC4_PKT_INFO_8822B 0xfff
#define BIT_AC4_PKT_INFO_8822B(x)                                              \
	(((x) & BIT_MASK_AC4_PKT_INFO_8822B) << BIT_SHIFT_AC4_PKT_INFO_8822B)
#define BITS_AC4_PKT_INFO_8822B                                                \
	(BIT_MASK_AC4_PKT_INFO_8822B << BIT_SHIFT_AC4_PKT_INFO_8822B)
#define BIT_CLEAR_AC4_PKT_INFO_8822B(x) ((x) & (~BITS_AC4_PKT_INFO_8822B))
#define BIT_GET_AC4_PKT_INFO_8822B(x)                                          \
	(((x) >> BIT_SHIFT_AC4_PKT_INFO_8822B) & BIT_MASK_AC4_PKT_INFO_8822B)
#define BIT_SET_AC4_PKT_INFO_8822B(x, v)                                       \
	(BIT_CLEAR_AC4_PKT_INFO_8822B(x) | BIT_AC4_PKT_INFO_8822B(v))

/* 2 REG_Q6_Q7_INFO_8822B */
#define BIT_QUEUE_MACID_AC_NOT_THE_SAME_8822B BIT(31)

#define BIT_SHIFT_GTAB_ID_8822B 28
#define BIT_MASK_GTAB_ID_8822B 0x7
#define BIT_GTAB_ID_8822B(x)                                                   \
	(((x) & BIT_MASK_GTAB_ID_8822B) << BIT_SHIFT_GTAB_ID_8822B)
#define BITS_GTAB_ID_8822B (BIT_MASK_GTAB_ID_8822B << BIT_SHIFT_GTAB_ID_8822B)
#define BIT_CLEAR_GTAB_ID_8822B(x) ((x) & (~BITS_GTAB_ID_8822B))
#define BIT_GET_GTAB_ID_8822B(x)                                               \
	(((x) >> BIT_SHIFT_GTAB_ID_8822B) & BIT_MASK_GTAB_ID_8822B)
#define BIT_SET_GTAB_ID_8822B(x, v)                                            \
	(BIT_CLEAR_GTAB_ID_8822B(x) | BIT_GTAB_ID_8822B(v))

#define BIT_SHIFT_AC7_PKT_INFO_8822B 16
#define BIT_MASK_AC7_PKT_INFO_8822B 0xfff
#define BIT_AC7_PKT_INFO_8822B(x)                                              \
	(((x) & BIT_MASK_AC7_PKT_INFO_8822B) << BIT_SHIFT_AC7_PKT_INFO_8822B)
#define BITS_AC7_PKT_INFO_8822B                                                \
	(BIT_MASK_AC7_PKT_INFO_8822B << BIT_SHIFT_AC7_PKT_INFO_8822B)
#define BIT_CLEAR_AC7_PKT_INFO_8822B(x) ((x) & (~BITS_AC7_PKT_INFO_8822B))
#define BIT_GET_AC7_PKT_INFO_8822B(x)                                          \
	(((x) >> BIT_SHIFT_AC7_PKT_INFO_8822B) & BIT_MASK_AC7_PKT_INFO_8822B)
#define BIT_SET_AC7_PKT_INFO_8822B(x, v)                                       \
	(BIT_CLEAR_AC7_PKT_INFO_8822B(x) | BIT_AC7_PKT_INFO_8822B(v))

#define BIT_QUEUE_MACID_AC_NOT_THE_SAME_V1_8822B BIT(15)

#define BIT_SHIFT_GTAB_ID_V1_8822B 12
#define BIT_MASK_GTAB_ID_V1_8822B 0x7
#define BIT_GTAB_ID_V1_8822B(x)                                                \
	(((x) & BIT_MASK_GTAB_ID_V1_8822B) << BIT_SHIFT_GTAB_ID_V1_8822B)
#define BITS_GTAB_ID_V1_8822B                                                  \
	(BIT_MASK_GTAB_ID_V1_8822B << BIT_SHIFT_GTAB_ID_V1_8822B)
#define BIT_CLEAR_GTAB_ID_V1_8822B(x) ((x) & (~BITS_GTAB_ID_V1_8822B))
#define BIT_GET_GTAB_ID_V1_8822B(x)                                            \
	(((x) >> BIT_SHIFT_GTAB_ID_V1_8822B) & BIT_MASK_GTAB_ID_V1_8822B)
#define BIT_SET_GTAB_ID_V1_8822B(x, v)                                         \
	(BIT_CLEAR_GTAB_ID_V1_8822B(x) | BIT_GTAB_ID_V1_8822B(v))

#define BIT_SHIFT_AC6_PKT_INFO_8822B 0
#define BIT_MASK_AC6_PKT_INFO_8822B 0xfff
#define BIT_AC6_PKT_INFO_8822B(x)                                              \
	(((x) & BIT_MASK_AC6_PKT_INFO_8822B) << BIT_SHIFT_AC6_PKT_INFO_8822B)
#define BITS_AC6_PKT_INFO_8822B                                                \
	(BIT_MASK_AC6_PKT_INFO_8822B << BIT_SHIFT_AC6_PKT_INFO_8822B)
#define BIT_CLEAR_AC6_PKT_INFO_8822B(x) ((x) & (~BITS_AC6_PKT_INFO_8822B))
#define BIT_GET_AC6_PKT_INFO_8822B(x)                                          \
	(((x) >> BIT_SHIFT_AC6_PKT_INFO_8822B) & BIT_MASK_AC6_PKT_INFO_8822B)
#define BIT_SET_AC6_PKT_INFO_8822B(x, v)                                       \
	(BIT_CLEAR_AC6_PKT_INFO_8822B(x) | BIT_AC6_PKT_INFO_8822B(v))

/* 2 REG_MGQ_HIQ_INFO_8822B */

#define BIT_SHIFT_HIQ_PKT_INFO_8822B 16
#define BIT_MASK_HIQ_PKT_INFO_8822B 0xfff
#define BIT_HIQ_PKT_INFO_8822B(x)                                              \
	(((x) & BIT_MASK_HIQ_PKT_INFO_8822B) << BIT_SHIFT_HIQ_PKT_INFO_8822B)
#define BITS_HIQ_PKT_INFO_8822B                                                \
	(BIT_MASK_HIQ_PKT_INFO_8822B << BIT_SHIFT_HIQ_PKT_INFO_8822B)
#define BIT_CLEAR_HIQ_PKT_INFO_8822B(x) ((x) & (~BITS_HIQ_PKT_INFO_8822B))
#define BIT_GET_HIQ_PKT_INFO_8822B(x)                                          \
	(((x) >> BIT_SHIFT_HIQ_PKT_INFO_8822B) & BIT_MASK_HIQ_PKT_INFO_8822B)
#define BIT_SET_HIQ_PKT_INFO_8822B(x, v)                                       \
	(BIT_CLEAR_HIQ_PKT_INFO_8822B(x) | BIT_HIQ_PKT_INFO_8822B(v))

#define BIT_SHIFT_MGQ_PKT_INFO_8822B 0
#define BIT_MASK_MGQ_PKT_INFO_8822B 0xfff
#define BIT_MGQ_PKT_INFO_8822B(x)                                              \
	(((x) & BIT_MASK_MGQ_PKT_INFO_8822B) << BIT_SHIFT_MGQ_PKT_INFO_8822B)
#define BITS_MGQ_PKT_INFO_8822B                                                \
	(BIT_MASK_MGQ_PKT_INFO_8822B << BIT_SHIFT_MGQ_PKT_INFO_8822B)
#define BIT_CLEAR_MGQ_PKT_INFO_8822B(x) ((x) & (~BITS_MGQ_PKT_INFO_8822B))
#define BIT_GET_MGQ_PKT_INFO_8822B(x)                                          \
	(((x) >> BIT_SHIFT_MGQ_PKT_INFO_8822B) & BIT_MASK_MGQ_PKT_INFO_8822B)
#define BIT_SET_MGQ_PKT_INFO_8822B(x, v)                                       \
	(BIT_CLEAR_MGQ_PKT_INFO_8822B(x) | BIT_MGQ_PKT_INFO_8822B(v))

/* 2 REG_CMDQ_BCNQ_INFO_8822B */

#define BIT_SHIFT_CMDQ_PKT_INFO_8822B 16
#define BIT_MASK_CMDQ_PKT_INFO_8822B 0xfff
#define BIT_CMDQ_PKT_INFO_8822B(x)                                             \
	(((x) & BIT_MASK_CMDQ_PKT_INFO_8822B) << BIT_SHIFT_CMDQ_PKT_INFO_8822B)
#define BITS_CMDQ_PKT_INFO_8822B                                               \
	(BIT_MASK_CMDQ_PKT_INFO_8822B << BIT_SHIFT_CMDQ_PKT_INFO_8822B)
#define BIT_CLEAR_CMDQ_PKT_INFO_8822B(x) ((x) & (~BITS_CMDQ_PKT_INFO_8822B))
#define BIT_GET_CMDQ_PKT_INFO_8822B(x)                                         \
	(((x) >> BIT_SHIFT_CMDQ_PKT_INFO_8822B) & BIT_MASK_CMDQ_PKT_INFO_8822B)
#define BIT_SET_CMDQ_PKT_INFO_8822B(x, v)                                      \
	(BIT_CLEAR_CMDQ_PKT_INFO_8822B(x) | BIT_CMDQ_PKT_INFO_8822B(v))

#define BIT_SHIFT_BCNQ_PKT_INFO_8822B 0
#define BIT_MASK_BCNQ_PKT_INFO_8822B 0xfff
#define BIT_BCNQ_PKT_INFO_8822B(x)                                             \
	(((x) & BIT_MASK_BCNQ_PKT_INFO_8822B) << BIT_SHIFT_BCNQ_PKT_INFO_8822B)
#define BITS_BCNQ_PKT_INFO_8822B                                               \
	(BIT_MASK_BCNQ_PKT_INFO_8822B << BIT_SHIFT_BCNQ_PKT_INFO_8822B)
#define BIT_CLEAR_BCNQ_PKT_INFO_8822B(x) ((x) & (~BITS_BCNQ_PKT_INFO_8822B))
#define BIT_GET_BCNQ_PKT_INFO_8822B(x)                                         \
	(((x) >> BIT_SHIFT_BCNQ_PKT_INFO_8822B) & BIT_MASK_BCNQ_PKT_INFO_8822B)
#define BIT_SET_BCNQ_PKT_INFO_8822B(x, v)                                      \
	(BIT_CLEAR_BCNQ_PKT_INFO_8822B(x) | BIT_BCNQ_PKT_INFO_8822B(v))

/* 2 REG_USEREG_SETTING_8822B */
#define BIT_NDPA_USEREG_8822B BIT(21)

#define BIT_SHIFT_RETRY_USEREG_8822B 19
#define BIT_MASK_RETRY_USEREG_8822B 0x3
#define BIT_RETRY_USEREG_8822B(x)                                              \
	(((x) & BIT_MASK_RETRY_USEREG_8822B) << BIT_SHIFT_RETRY_USEREG_8822B)
#define BITS_RETRY_USEREG_8822B                                                \
	(BIT_MASK_RETRY_USEREG_8822B << BIT_SHIFT_RETRY_USEREG_8822B)
#define BIT_CLEAR_RETRY_USEREG_8822B(x) ((x) & (~BITS_RETRY_USEREG_8822B))
#define BIT_GET_RETRY_USEREG_8822B(x)                                          \
	(((x) >> BIT_SHIFT_RETRY_USEREG_8822B) & BIT_MASK_RETRY_USEREG_8822B)
#define BIT_SET_RETRY_USEREG_8822B(x, v)                                       \
	(BIT_CLEAR_RETRY_USEREG_8822B(x) | BIT_RETRY_USEREG_8822B(v))

#define BIT_SHIFT_TRYPKT_USEREG_8822B 17
#define BIT_MASK_TRYPKT_USEREG_8822B 0x3
#define BIT_TRYPKT_USEREG_8822B(x)                                             \
	(((x) & BIT_MASK_TRYPKT_USEREG_8822B) << BIT_SHIFT_TRYPKT_USEREG_8822B)
#define BITS_TRYPKT_USEREG_8822B                                               \
	(BIT_MASK_TRYPKT_USEREG_8822B << BIT_SHIFT_TRYPKT_USEREG_8822B)
#define BIT_CLEAR_TRYPKT_USEREG_8822B(x) ((x) & (~BITS_TRYPKT_USEREG_8822B))
#define BIT_GET_TRYPKT_USEREG_8822B(x)                                         \
	(((x) >> BIT_SHIFT_TRYPKT_USEREG_8822B) & BIT_MASK_TRYPKT_USEREG_8822B)
#define BIT_SET_TRYPKT_USEREG_8822B(x, v)                                      \
	(BIT_CLEAR_TRYPKT_USEREG_8822B(x) | BIT_TRYPKT_USEREG_8822B(v))

#define BIT_CTLPKT_USEREG_8822B BIT(16)

/* 2 REG_AESIV_SETTING_8822B */

#define BIT_SHIFT_AESIV_OFFSET_8822B 0
#define BIT_MASK_AESIV_OFFSET_8822B 0xfff
#define BIT_AESIV_OFFSET_8822B(x)                                              \
	(((x) & BIT_MASK_AESIV_OFFSET_8822B) << BIT_SHIFT_AESIV_OFFSET_8822B)
#define BITS_AESIV_OFFSET_8822B                                                \
	(BIT_MASK_AESIV_OFFSET_8822B << BIT_SHIFT_AESIV_OFFSET_8822B)
#define BIT_CLEAR_AESIV_OFFSET_8822B(x) ((x) & (~BITS_AESIV_OFFSET_8822B))
#define BIT_GET_AESIV_OFFSET_8822B(x)                                          \
	(((x) >> BIT_SHIFT_AESIV_OFFSET_8822B) & BIT_MASK_AESIV_OFFSET_8822B)
#define BIT_SET_AESIV_OFFSET_8822B(x, v)                                       \
	(BIT_CLEAR_AESIV_OFFSET_8822B(x) | BIT_AESIV_OFFSET_8822B(v))

/* 2 REG_BF0_TIME_SETTING_8822B */
#define BIT_BF0_TIMER_SET_8822B BIT(31)
#define BIT_BF0_TIMER_CLR_8822B BIT(30)
#define BIT_BF0_UPDATE_EN_8822B BIT(29)
#define BIT_BF0_TIMER_EN_8822B BIT(28)

#define BIT_SHIFT_BF0_PRETIME_OVER_8822B 16
#define BIT_MASK_BF0_PRETIME_OVER_8822B 0xfff
#define BIT_BF0_PRETIME_OVER_8822B(x)                                          \
	(((x) & BIT_MASK_BF0_PRETIME_OVER_8822B)                               \
	 << BIT_SHIFT_BF0_PRETIME_OVER_8822B)
#define BITS_BF0_PRETIME_OVER_8822B                                            \
	(BIT_MASK_BF0_PRETIME_OVER_8822B << BIT_SHIFT_BF0_PRETIME_OVER_8822B)
#define BIT_CLEAR_BF0_PRETIME_OVER_8822B(x)                                    \
	((x) & (~BITS_BF0_PRETIME_OVER_8822B))
#define BIT_GET_BF0_PRETIME_OVER_8822B(x)                                      \
	(((x) >> BIT_SHIFT_BF0_PRETIME_OVER_8822B) &                           \
	 BIT_MASK_BF0_PRETIME_OVER_8822B)
#define BIT_SET_BF0_PRETIME_OVER_8822B(x, v)                                   \
	(BIT_CLEAR_BF0_PRETIME_OVER_8822B(x) | BIT_BF0_PRETIME_OVER_8822B(v))

#define BIT_SHIFT_BF0_LIFETIME_8822B 0
#define BIT_MASK_BF0_LIFETIME_8822B 0xffff
#define BIT_BF0_LIFETIME_8822B(x)                                              \
	(((x) & BIT_MASK_BF0_LIFETIME_8822B) << BIT_SHIFT_BF0_LIFETIME_8822B)
#define BITS_BF0_LIFETIME_8822B                                                \
	(BIT_MASK_BF0_LIFETIME_8822B << BIT_SHIFT_BF0_LIFETIME_8822B)
#define BIT_CLEAR_BF0_LIFETIME_8822B(x) ((x) & (~BITS_BF0_LIFETIME_8822B))
#define BIT_GET_BF0_LIFETIME_8822B(x)                                          \
	(((x) >> BIT_SHIFT_BF0_LIFETIME_8822B) & BIT_MASK_BF0_LIFETIME_8822B)
#define BIT_SET_BF0_LIFETIME_8822B(x, v)                                       \
	(BIT_CLEAR_BF0_LIFETIME_8822B(x) | BIT_BF0_LIFETIME_8822B(v))

/* 2 REG_BF1_TIME_SETTING_8822B */
#define BIT_BF1_TIMER_SET_8822B BIT(31)
#define BIT_BF1_TIMER_CLR_8822B BIT(30)
#define BIT_BF1_UPDATE_EN_8822B BIT(29)
#define BIT_BF1_TIMER_EN_8822B BIT(28)

#define BIT_SHIFT_BF1_PRETIME_OVER_8822B 16
#define BIT_MASK_BF1_PRETIME_OVER_8822B 0xfff
#define BIT_BF1_PRETIME_OVER_8822B(x)                                          \
	(((x) & BIT_MASK_BF1_PRETIME_OVER_8822B)                               \
	 << BIT_SHIFT_BF1_PRETIME_OVER_8822B)
#define BITS_BF1_PRETIME_OVER_8822B                                            \
	(BIT_MASK_BF1_PRETIME_OVER_8822B << BIT_SHIFT_BF1_PRETIME_OVER_8822B)
#define BIT_CLEAR_BF1_PRETIME_OVER_8822B(x)                                    \
	((x) & (~BITS_BF1_PRETIME_OVER_8822B))
#define BIT_GET_BF1_PRETIME_OVER_8822B(x)                                      \
	(((x) >> BIT_SHIFT_BF1_PRETIME_OVER_8822B) &                           \
	 BIT_MASK_BF1_PRETIME_OVER_8822B)
#define BIT_SET_BF1_PRETIME_OVER_8822B(x, v)                                   \
	(BIT_CLEAR_BF1_PRETIME_OVER_8822B(x) | BIT_BF1_PRETIME_OVER_8822B(v))

#define BIT_SHIFT_BF1_LIFETIME_8822B 0
#define BIT_MASK_BF1_LIFETIME_8822B 0xffff
#define BIT_BF1_LIFETIME_8822B(x)                                              \
	(((x) & BIT_MASK_BF1_LIFETIME_8822B) << BIT_SHIFT_BF1_LIFETIME_8822B)
#define BITS_BF1_LIFETIME_8822B                                                \
	(BIT_MASK_BF1_LIFETIME_8822B << BIT_SHIFT_BF1_LIFETIME_8822B)
#define BIT_CLEAR_BF1_LIFETIME_8822B(x) ((x) & (~BITS_BF1_LIFETIME_8822B))
#define BIT_GET_BF1_LIFETIME_8822B(x)                                          \
	(((x) >> BIT_SHIFT_BF1_LIFETIME_8822B) & BIT_MASK_BF1_LIFETIME_8822B)
#define BIT_SET_BF1_LIFETIME_8822B(x, v)                                       \
	(BIT_CLEAR_BF1_LIFETIME_8822B(x) | BIT_BF1_LIFETIME_8822B(v))

/* 2 REG_BF_TIMEOUT_EN_8822B */
#define BIT_EN_VHT_LDPC_8822B BIT(9)
#define BIT_EN_HT_LDPC_8822B BIT(8)
#define BIT_BF1_TIMEOUT_EN_8822B BIT(1)
#define BIT_BF0_TIMEOUT_EN_8822B BIT(0)

/* 2 REG_MACID_RELEASE0_8822B */

#define BIT_SHIFT_MACID31_0_RELEASE_8822B 0
#define BIT_MASK_MACID31_0_RELEASE_8822B 0xffffffffL
#define BIT_MACID31_0_RELEASE_8822B(x)                                         \
	(((x) & BIT_MASK_MACID31_0_RELEASE_8822B)                              \
	 << BIT_SHIFT_MACID31_0_RELEASE_8822B)
#define BITS_MACID31_0_RELEASE_8822B                                           \
	(BIT_MASK_MACID31_0_RELEASE_8822B << BIT_SHIFT_MACID31_0_RELEASE_8822B)
#define BIT_CLEAR_MACID31_0_RELEASE_8822B(x)                                   \
	((x) & (~BITS_MACID31_0_RELEASE_8822B))
#define BIT_GET_MACID31_0_RELEASE_8822B(x)                                     \
	(((x) >> BIT_SHIFT_MACID31_0_RELEASE_8822B) &                          \
	 BIT_MASK_MACID31_0_RELEASE_8822B)
#define BIT_SET_MACID31_0_RELEASE_8822B(x, v)                                  \
	(BIT_CLEAR_MACID31_0_RELEASE_8822B(x) | BIT_MACID31_0_RELEASE_8822B(v))

/* 2 REG_MACID_RELEASE1_8822B */

#define BIT_SHIFT_MACID63_32_RELEASE_8822B 0
#define BIT_MASK_MACID63_32_RELEASE_8822B 0xffffffffL
#define BIT_MACID63_32_RELEASE_8822B(x)                                        \
	(((x) & BIT_MASK_MACID63_32_RELEASE_8822B)                             \
	 << BIT_SHIFT_MACID63_32_RELEASE_8822B)
#define BITS_MACID63_32_RELEASE_8822B                                          \
	(BIT_MASK_MACID63_32_RELEASE_8822B                                     \
	 << BIT_SHIFT_MACID63_32_RELEASE_8822B)
#define BIT_CLEAR_MACID63_32_RELEASE_8822B(x)                                  \
	((x) & (~BITS_MACID63_32_RELEASE_8822B))
#define BIT_GET_MACID63_32_RELEASE_8822B(x)                                    \
	(((x) >> BIT_SHIFT_MACID63_32_RELEASE_8822B) &                         \
	 BIT_MASK_MACID63_32_RELEASE_8822B)
#define BIT_SET_MACID63_32_RELEASE_8822B(x, v)                                 \
	(BIT_CLEAR_MACID63_32_RELEASE_8822B(x) |                               \
	 BIT_MACID63_32_RELEASE_8822B(v))

/* 2 REG_MACID_RELEASE2_8822B */

#define BIT_SHIFT_MACID95_64_RELEASE_8822B 0
#define BIT_MASK_MACID95_64_RELEASE_8822B 0xffffffffL
#define BIT_MACID95_64_RELEASE_8822B(x)                                        \
	(((x) & BIT_MASK_MACID95_64_RELEASE_8822B)                             \
	 << BIT_SHIFT_MACID95_64_RELEASE_8822B)
#define BITS_MACID95_64_RELEASE_8822B                                          \
	(BIT_MASK_MACID95_64_RELEASE_8822B                                     \
	 << BIT_SHIFT_MACID95_64_RELEASE_8822B)
#define BIT_CLEAR_MACID95_64_RELEASE_8822B(x)                                  \
	((x) & (~BITS_MACID95_64_RELEASE_8822B))
#define BIT_GET_MACID95_64_RELEASE_8822B(x)                                    \
	(((x) >> BIT_SHIFT_MACID95_64_RELEASE_8822B) &                         \
	 BIT_MASK_MACID95_64_RELEASE_8822B)
#define BIT_SET_MACID95_64_RELEASE_8822B(x, v)                                 \
	(BIT_CLEAR_MACID95_64_RELEASE_8822B(x) |                               \
	 BIT_MACID95_64_RELEASE_8822B(v))

/* 2 REG_MACID_RELEASE3_8822B */

#define BIT_SHIFT_MACID127_96_RELEASE_8822B 0
#define BIT_MASK_MACID127_96_RELEASE_8822B 0xffffffffL
#define BIT_MACID127_96_RELEASE_8822B(x)                                       \
	(((x) & BIT_MASK_MACID127_96_RELEASE_8822B)                            \
	 << BIT_SHIFT_MACID127_96_RELEASE_8822B)
#define BITS_MACID127_96_RELEASE_8822B                                         \
	(BIT_MASK_MACID127_96_RELEASE_8822B                                    \
	 << BIT_SHIFT_MACID127_96_RELEASE_8822B)
#define BIT_CLEAR_MACID127_96_RELEASE_8822B(x)                                 \
	((x) & (~BITS_MACID127_96_RELEASE_8822B))
#define BIT_GET_MACID127_96_RELEASE_8822B(x)                                   \
	(((x) >> BIT_SHIFT_MACID127_96_RELEASE_8822B) &                        \
	 BIT_MASK_MACID127_96_RELEASE_8822B)
#define BIT_SET_MACID127_96_RELEASE_8822B(x, v)                                \
	(BIT_CLEAR_MACID127_96_RELEASE_8822B(x) |                              \
	 BIT_MACID127_96_RELEASE_8822B(v))

/* 2 REG_MACID_RELEASE_SETTING_8822B */
#define BIT_MACID_VALUE_8822B BIT(7)

#define BIT_SHIFT_MACID_OFFSET_8822B 0
#define BIT_MASK_MACID_OFFSET_8822B 0x7f
#define BIT_MACID_OFFSET_8822B(x)                                              \
	(((x) & BIT_MASK_MACID_OFFSET_8822B) << BIT_SHIFT_MACID_OFFSET_8822B)
#define BITS_MACID_OFFSET_8822B                                                \
	(BIT_MASK_MACID_OFFSET_8822B << BIT_SHIFT_MACID_OFFSET_8822B)
#define BIT_CLEAR_MACID_OFFSET_8822B(x) ((x) & (~BITS_MACID_OFFSET_8822B))
#define BIT_GET_MACID_OFFSET_8822B(x)                                          \
	(((x) >> BIT_SHIFT_MACID_OFFSET_8822B) & BIT_MASK_MACID_OFFSET_8822B)
#define BIT_SET_MACID_OFFSET_8822B(x, v)                                       \
	(BIT_CLEAR_MACID_OFFSET_8822B(x) | BIT_MACID_OFFSET_8822B(v))

/* 2 REG_FAST_EDCA_VOVI_SETTING_8822B */

#define BIT_SHIFT_VI_FAST_EDCA_TO_8822B 24
#define BIT_MASK_VI_FAST_EDCA_TO_8822B 0xff
#define BIT_VI_FAST_EDCA_TO_8822B(x)                                           \
	(((x) & BIT_MASK_VI_FAST_EDCA_TO_8822B)                                \
	 << BIT_SHIFT_VI_FAST_EDCA_TO_8822B)
#define BITS_VI_FAST_EDCA_TO_8822B                                             \
	(BIT_MASK_VI_FAST_EDCA_TO_8822B << BIT_SHIFT_VI_FAST_EDCA_TO_8822B)
#define BIT_CLEAR_VI_FAST_EDCA_TO_8822B(x) ((x) & (~BITS_VI_FAST_EDCA_TO_8822B))
#define BIT_GET_VI_FAST_EDCA_TO_8822B(x)                                       \
	(((x) >> BIT_SHIFT_VI_FAST_EDCA_TO_8822B) &                            \
	 BIT_MASK_VI_FAST_EDCA_TO_8822B)
#define BIT_SET_VI_FAST_EDCA_TO_8822B(x, v)                                    \
	(BIT_CLEAR_VI_FAST_EDCA_TO_8822B(x) | BIT_VI_FAST_EDCA_TO_8822B(v))

#define BIT_VI_THRESHOLD_SEL_8822B BIT(23)

#define BIT_SHIFT_VI_FAST_EDCA_PKT_TH_8822B 16
#define BIT_MASK_VI_FAST_EDCA_PKT_TH_8822B 0x7f
#define BIT_VI_FAST_EDCA_PKT_TH_8822B(x)                                       \
	(((x) & BIT_MASK_VI_FAST_EDCA_PKT_TH_8822B)                            \
	 << BIT_SHIFT_VI_FAST_EDCA_PKT_TH_8822B)
#define BITS_VI_FAST_EDCA_PKT_TH_8822B                                         \
	(BIT_MASK_VI_FAST_EDCA_PKT_TH_8822B                                    \
	 << BIT_SHIFT_VI_FAST_EDCA_PKT_TH_8822B)
#define BIT_CLEAR_VI_FAST_EDCA_PKT_TH_8822B(x)                                 \
	((x) & (~BITS_VI_FAST_EDCA_PKT_TH_8822B))
#define BIT_GET_VI_FAST_EDCA_PKT_TH_8822B(x)                                   \
	(((x) >> BIT_SHIFT_VI_FAST_EDCA_PKT_TH_8822B) &                        \
	 BIT_MASK_VI_FAST_EDCA_PKT_TH_8822B)
#define BIT_SET_VI_FAST_EDCA_PKT_TH_8822B(x, v)                                \
	(BIT_CLEAR_VI_FAST_EDCA_PKT_TH_8822B(x) |                              \
	 BIT_VI_FAST_EDCA_PKT_TH_8822B(v))

#define BIT_SHIFT_VO_FAST_EDCA_TO_8822B 8
#define BIT_MASK_VO_FAST_EDCA_TO_8822B 0xff
#define BIT_VO_FAST_EDCA_TO_8822B(x)                                           \
	(((x) & BIT_MASK_VO_FAST_EDCA_TO_8822B)                                \
	 << BIT_SHIFT_VO_FAST_EDCA_TO_8822B)
#define BITS_VO_FAST_EDCA_TO_8822B                                             \
	(BIT_MASK_VO_FAST_EDCA_TO_8822B << BIT_SHIFT_VO_FAST_EDCA_TO_8822B)
#define BIT_CLEAR_VO_FAST_EDCA_TO_8822B(x) ((x) & (~BITS_VO_FAST_EDCA_TO_8822B))
#define BIT_GET_VO_FAST_EDCA_TO_8822B(x)                                       \
	(((x) >> BIT_SHIFT_VO_FAST_EDCA_TO_8822B) &                            \
	 BIT_MASK_VO_FAST_EDCA_TO_8822B)
#define BIT_SET_VO_FAST_EDCA_TO_8822B(x, v)                                    \
	(BIT_CLEAR_VO_FAST_EDCA_TO_8822B(x) | BIT_VO_FAST_EDCA_TO_8822B(v))

#define BIT_VO_THRESHOLD_SEL_8822B BIT(7)

#define BIT_SHIFT_VO_FAST_EDCA_PKT_TH_8822B 0
#define BIT_MASK_VO_FAST_EDCA_PKT_TH_8822B 0x7f
#define BIT_VO_FAST_EDCA_PKT_TH_8822B(x)                                       \
	(((x) & BIT_MASK_VO_FAST_EDCA_PKT_TH_8822B)                            \
	 << BIT_SHIFT_VO_FAST_EDCA_PKT_TH_8822B)
#define BITS_VO_FAST_EDCA_PKT_TH_8822B                                         \
	(BIT_MASK_VO_FAST_EDCA_PKT_TH_8822B                                    \
	 << BIT_SHIFT_VO_FAST_EDCA_PKT_TH_8822B)
#define BIT_CLEAR_VO_FAST_EDCA_PKT_TH_8822B(x)                                 \
	((x) & (~BITS_VO_FAST_EDCA_PKT_TH_8822B))
#define BIT_GET_VO_FAST_EDCA_PKT_TH_8822B(x)                                   \
	(((x) >> BIT_SHIFT_VO_FAST_EDCA_PKT_TH_8822B) &                        \
	 BIT_MASK_VO_FAST_EDCA_PKT_TH_8822B)
#define BIT_SET_VO_FAST_EDCA_PKT_TH_8822B(x, v)                                \
	(BIT_CLEAR_VO_FAST_EDCA_PKT_TH_8822B(x) |                              \
	 BIT_VO_FAST_EDCA_PKT_TH_8822B(v))

/* 2 REG_FAST_EDCA_BEBK_SETTING_8822B */

#define BIT_SHIFT_BK_FAST_EDCA_TO_8822B 24
#define BIT_MASK_BK_FAST_EDCA_TO_8822B 0xff
#define BIT_BK_FAST_EDCA_TO_8822B(x)                                           \
	(((x) & BIT_MASK_BK_FAST_EDCA_TO_8822B)                                \
	 << BIT_SHIFT_BK_FAST_EDCA_TO_8822B)
#define BITS_BK_FAST_EDCA_TO_8822B                                             \
	(BIT_MASK_BK_FAST_EDCA_TO_8822B << BIT_SHIFT_BK_FAST_EDCA_TO_8822B)
#define BIT_CLEAR_BK_FAST_EDCA_TO_8822B(x) ((x) & (~BITS_BK_FAST_EDCA_TO_8822B))
#define BIT_GET_BK_FAST_EDCA_TO_8822B(x)                                       \
	(((x) >> BIT_SHIFT_BK_FAST_EDCA_TO_8822B) &                            \
	 BIT_MASK_BK_FAST_EDCA_TO_8822B)
#define BIT_SET_BK_FAST_EDCA_TO_8822B(x, v)                                    \
	(BIT_CLEAR_BK_FAST_EDCA_TO_8822B(x) | BIT_BK_FAST_EDCA_TO_8822B(v))

#define BIT_BK_THRESHOLD_SEL_8822B BIT(23)

#define BIT_SHIFT_BK_FAST_EDCA_PKT_TH_8822B 16
#define BIT_MASK_BK_FAST_EDCA_PKT_TH_8822B 0x7f
#define BIT_BK_FAST_EDCA_PKT_TH_8822B(x)                                       \
	(((x) & BIT_MASK_BK_FAST_EDCA_PKT_TH_8822B)                            \
	 << BIT_SHIFT_BK_FAST_EDCA_PKT_TH_8822B)
#define BITS_BK_FAST_EDCA_PKT_TH_8822B                                         \
	(BIT_MASK_BK_FAST_EDCA_PKT_TH_8822B                                    \
	 << BIT_SHIFT_BK_FAST_EDCA_PKT_TH_8822B)
#define BIT_CLEAR_BK_FAST_EDCA_PKT_TH_8822B(x)                                 \
	((x) & (~BITS_BK_FAST_EDCA_PKT_TH_8822B))
#define BIT_GET_BK_FAST_EDCA_PKT_TH_8822B(x)                                   \
	(((x) >> BIT_SHIFT_BK_FAST_EDCA_PKT_TH_8822B) &                        \
	 BIT_MASK_BK_FAST_EDCA_PKT_TH_8822B)
#define BIT_SET_BK_FAST_EDCA_PKT_TH_8822B(x, v)                                \
	(BIT_CLEAR_BK_FAST_EDCA_PKT_TH_8822B(x) |                              \
	 BIT_BK_FAST_EDCA_PKT_TH_8822B(v))

#define BIT_SHIFT_BE_FAST_EDCA_TO_8822B 8
#define BIT_MASK_BE_FAST_EDCA_TO_8822B 0xff
#define BIT_BE_FAST_EDCA_TO_8822B(x)                                           \
	(((x) & BIT_MASK_BE_FAST_EDCA_TO_8822B)                                \
	 << BIT_SHIFT_BE_FAST_EDCA_TO_8822B)
#define BITS_BE_FAST_EDCA_TO_8822B                                             \
	(BIT_MASK_BE_FAST_EDCA_TO_8822B << BIT_SHIFT_BE_FAST_EDCA_TO_8822B)
#define BIT_CLEAR_BE_FAST_EDCA_TO_8822B(x) ((x) & (~BITS_BE_FAST_EDCA_TO_8822B))
#define BIT_GET_BE_FAST_EDCA_TO_8822B(x)                                       \
	(((x) >> BIT_SHIFT_BE_FAST_EDCA_TO_8822B) &                            \
	 BIT_MASK_BE_FAST_EDCA_TO_8822B)
#define BIT_SET_BE_FAST_EDCA_TO_8822B(x, v)                                    \
	(BIT_CLEAR_BE_FAST_EDCA_TO_8822B(x) | BIT_BE_FAST_EDCA_TO_8822B(v))

#define BIT_BE_THRESHOLD_SEL_8822B BIT(7)

#define BIT_SHIFT_BE_FAST_EDCA_PKT_TH_8822B 0
#define BIT_MASK_BE_FAST_EDCA_PKT_TH_8822B 0x7f
#define BIT_BE_FAST_EDCA_PKT_TH_8822B(x)                                       \
	(((x) & BIT_MASK_BE_FAST_EDCA_PKT_TH_8822B)                            \
	 << BIT_SHIFT_BE_FAST_EDCA_PKT_TH_8822B)
#define BITS_BE_FAST_EDCA_PKT_TH_8822B                                         \
	(BIT_MASK_BE_FAST_EDCA_PKT_TH_8822B                                    \
	 << BIT_SHIFT_BE_FAST_EDCA_PKT_TH_8822B)
#define BIT_CLEAR_BE_FAST_EDCA_PKT_TH_8822B(x)                                 \
	((x) & (~BITS_BE_FAST_EDCA_PKT_TH_8822B))
#define BIT_GET_BE_FAST_EDCA_PKT_TH_8822B(x)                                   \
	(((x) >> BIT_SHIFT_BE_FAST_EDCA_PKT_TH_8822B) &                        \
	 BIT_MASK_BE_FAST_EDCA_PKT_TH_8822B)
#define BIT_SET_BE_FAST_EDCA_PKT_TH_8822B(x, v)                                \
	(BIT_CLEAR_BE_FAST_EDCA_PKT_TH_8822B(x) |                              \
	 BIT_BE_FAST_EDCA_PKT_TH_8822B(v))

/* 2 REG_MACID_DROP0_8822B */

#define BIT_SHIFT_MACID31_0_DROP_8822B 0
#define BIT_MASK_MACID31_0_DROP_8822B 0xffffffffL
#define BIT_MACID31_0_DROP_8822B(x)                                            \
	(((x) & BIT_MASK_MACID31_0_DROP_8822B)                                 \
	 << BIT_SHIFT_MACID31_0_DROP_8822B)
#define BITS_MACID31_0_DROP_8822B                                              \
	(BIT_MASK_MACID31_0_DROP_8822B << BIT_SHIFT_MACID31_0_DROP_8822B)
#define BIT_CLEAR_MACID31_0_DROP_8822B(x) ((x) & (~BITS_MACID31_0_DROP_8822B))
#define BIT_GET_MACID31_0_DROP_8822B(x)                                        \
	(((x) >> BIT_SHIFT_MACID31_0_DROP_8822B) &                             \
	 BIT_MASK_MACID31_0_DROP_8822B)
#define BIT_SET_MACID31_0_DROP_8822B(x, v)                                     \
	(BIT_CLEAR_MACID31_0_DROP_8822B(x) | BIT_MACID31_0_DROP_8822B(v))

/* 2 REG_MACID_DROP1_8822B */

#define BIT_SHIFT_MACID63_32_DROP_8822B 0
#define BIT_MASK_MACID63_32_DROP_8822B 0xffffffffL
#define BIT_MACID63_32_DROP_8822B(x)                                           \
	(((x) & BIT_MASK_MACID63_32_DROP_8822B)                                \
	 << BIT_SHIFT_MACID63_32_DROP_8822B)
#define BITS_MACID63_32_DROP_8822B                                             \
	(BIT_MASK_MACID63_32_DROP_8822B << BIT_SHIFT_MACID63_32_DROP_8822B)
#define BIT_CLEAR_MACID63_32_DROP_8822B(x) ((x) & (~BITS_MACID63_32_DROP_8822B))
#define BIT_GET_MACID63_32_DROP_8822B(x)                                       \
	(((x) >> BIT_SHIFT_MACID63_32_DROP_8822B) &                            \
	 BIT_MASK_MACID63_32_DROP_8822B)
#define BIT_SET_MACID63_32_DROP_8822B(x, v)                                    \
	(BIT_CLEAR_MACID63_32_DROP_8822B(x) | BIT_MACID63_32_DROP_8822B(v))

/* 2 REG_MACID_DROP2_8822B */

#define BIT_SHIFT_MACID95_64_DROP_8822B 0
#define BIT_MASK_MACID95_64_DROP_8822B 0xffffffffL
#define BIT_MACID95_64_DROP_8822B(x)                                           \
	(((x) & BIT_MASK_MACID95_64_DROP_8822B)                                \
	 << BIT_SHIFT_MACID95_64_DROP_8822B)
#define BITS_MACID95_64_DROP_8822B                                             \
	(BIT_MASK_MACID95_64_DROP_8822B << BIT_SHIFT_MACID95_64_DROP_8822B)
#define BIT_CLEAR_MACID95_64_DROP_8822B(x) ((x) & (~BITS_MACID95_64_DROP_8822B))
#define BIT_GET_MACID95_64_DROP_8822B(x)                                       \
	(((x) >> BIT_SHIFT_MACID95_64_DROP_8822B) &                            \
	 BIT_MASK_MACID95_64_DROP_8822B)
#define BIT_SET_MACID95_64_DROP_8822B(x, v)                                    \
	(BIT_CLEAR_MACID95_64_DROP_8822B(x) | BIT_MACID95_64_DROP_8822B(v))

/* 2 REG_MACID_DROP3_8822B */

#define BIT_SHIFT_MACID127_96_DROP_8822B 0
#define BIT_MASK_MACID127_96_DROP_8822B 0xffffffffL
#define BIT_MACID127_96_DROP_8822B(x)                                          \
	(((x) & BIT_MASK_MACID127_96_DROP_8822B)                               \
	 << BIT_SHIFT_MACID127_96_DROP_8822B)
#define BITS_MACID127_96_DROP_8822B                                            \
	(BIT_MASK_MACID127_96_DROP_8822B << BIT_SHIFT_MACID127_96_DROP_8822B)
#define BIT_CLEAR_MACID127_96_DROP_8822B(x)                                    \
	((x) & (~BITS_MACID127_96_DROP_8822B))
#define BIT_GET_MACID127_96_DROP_8822B(x)                                      \
	(((x) >> BIT_SHIFT_MACID127_96_DROP_8822B) &                           \
	 BIT_MASK_MACID127_96_DROP_8822B)
#define BIT_SET_MACID127_96_DROP_8822B(x, v)                                   \
	(BIT_CLEAR_MACID127_96_DROP_8822B(x) | BIT_MACID127_96_DROP_8822B(v))

/* 2 REG_R_MACID_RELEASE_SUCCESS_0_8822B */

#define BIT_SHIFT_R_MACID_RELEASE_SUCCESS_0_8822B 0
#define BIT_MASK_R_MACID_RELEASE_SUCCESS_0_8822B 0xffffffffL
#define BIT_R_MACID_RELEASE_SUCCESS_0_8822B(x)                                 \
	(((x) & BIT_MASK_R_MACID_RELEASE_SUCCESS_0_8822B)                      \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_0_8822B)
#define BITS_R_MACID_RELEASE_SUCCESS_0_8822B                                   \
	(BIT_MASK_R_MACID_RELEASE_SUCCESS_0_8822B                              \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_0_8822B)
#define BIT_CLEAR_R_MACID_RELEASE_SUCCESS_0_8822B(x)                           \
	((x) & (~BITS_R_MACID_RELEASE_SUCCESS_0_8822B))
#define BIT_GET_R_MACID_RELEASE_SUCCESS_0_8822B(x)                             \
	(((x) >> BIT_SHIFT_R_MACID_RELEASE_SUCCESS_0_8822B) &                  \
	 BIT_MASK_R_MACID_RELEASE_SUCCESS_0_8822B)
#define BIT_SET_R_MACID_RELEASE_SUCCESS_0_8822B(x, v)                          \
	(BIT_CLEAR_R_MACID_RELEASE_SUCCESS_0_8822B(x) |                        \
	 BIT_R_MACID_RELEASE_SUCCESS_0_8822B(v))

/* 2 REG_R_MACID_RELEASE_SUCCESS_1_8822B */

#define BIT_SHIFT_R_MACID_RELEASE_SUCCESS_1_8822B 0
#define BIT_MASK_R_MACID_RELEASE_SUCCESS_1_8822B 0xffffffffL
#define BIT_R_MACID_RELEASE_SUCCESS_1_8822B(x)                                 \
	(((x) & BIT_MASK_R_MACID_RELEASE_SUCCESS_1_8822B)                      \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_1_8822B)
#define BITS_R_MACID_RELEASE_SUCCESS_1_8822B                                   \
	(BIT_MASK_R_MACID_RELEASE_SUCCESS_1_8822B                              \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_1_8822B)
#define BIT_CLEAR_R_MACID_RELEASE_SUCCESS_1_8822B(x)                           \
	((x) & (~BITS_R_MACID_RELEASE_SUCCESS_1_8822B))
#define BIT_GET_R_MACID_RELEASE_SUCCESS_1_8822B(x)                             \
	(((x) >> BIT_SHIFT_R_MACID_RELEASE_SUCCESS_1_8822B) &                  \
	 BIT_MASK_R_MACID_RELEASE_SUCCESS_1_8822B)
#define BIT_SET_R_MACID_RELEASE_SUCCESS_1_8822B(x, v)                          \
	(BIT_CLEAR_R_MACID_RELEASE_SUCCESS_1_8822B(x) |                        \
	 BIT_R_MACID_RELEASE_SUCCESS_1_8822B(v))

/* 2 REG_R_MACID_RELEASE_SUCCESS_2_8822B */

#define BIT_SHIFT_R_MACID_RELEASE_SUCCESS_2_8822B 0
#define BIT_MASK_R_MACID_RELEASE_SUCCESS_2_8822B 0xffffffffL
#define BIT_R_MACID_RELEASE_SUCCESS_2_8822B(x)                                 \
	(((x) & BIT_MASK_R_MACID_RELEASE_SUCCESS_2_8822B)                      \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_2_8822B)
#define BITS_R_MACID_RELEASE_SUCCESS_2_8822B                                   \
	(BIT_MASK_R_MACID_RELEASE_SUCCESS_2_8822B                              \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_2_8822B)
#define BIT_CLEAR_R_MACID_RELEASE_SUCCESS_2_8822B(x)                           \
	((x) & (~BITS_R_MACID_RELEASE_SUCCESS_2_8822B))
#define BIT_GET_R_MACID_RELEASE_SUCCESS_2_8822B(x)                             \
	(((x) >> BIT_SHIFT_R_MACID_RELEASE_SUCCESS_2_8822B) &                  \
	 BIT_MASK_R_MACID_RELEASE_SUCCESS_2_8822B)
#define BIT_SET_R_MACID_RELEASE_SUCCESS_2_8822B(x, v)                          \
	(BIT_CLEAR_R_MACID_RELEASE_SUCCESS_2_8822B(x) |                        \
	 BIT_R_MACID_RELEASE_SUCCESS_2_8822B(v))

/* 2 REG_R_MACID_RELEASE_SUCCESS_3_8822B */

#define BIT_SHIFT_R_MACID_RELEASE_SUCCESS_3_8822B 0
#define BIT_MASK_R_MACID_RELEASE_SUCCESS_3_8822B 0xffffffffL
#define BIT_R_MACID_RELEASE_SUCCESS_3_8822B(x)                                 \
	(((x) & BIT_MASK_R_MACID_RELEASE_SUCCESS_3_8822B)                      \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_3_8822B)
#define BITS_R_MACID_RELEASE_SUCCESS_3_8822B                                   \
	(BIT_MASK_R_MACID_RELEASE_SUCCESS_3_8822B                              \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_3_8822B)
#define BIT_CLEAR_R_MACID_RELEASE_SUCCESS_3_8822B(x)                           \
	((x) & (~BITS_R_MACID_RELEASE_SUCCESS_3_8822B))
#define BIT_GET_R_MACID_RELEASE_SUCCESS_3_8822B(x)                             \
	(((x) >> BIT_SHIFT_R_MACID_RELEASE_SUCCESS_3_8822B) &                  \
	 BIT_MASK_R_MACID_RELEASE_SUCCESS_3_8822B)
#define BIT_SET_R_MACID_RELEASE_SUCCESS_3_8822B(x, v)                          \
	(BIT_CLEAR_R_MACID_RELEASE_SUCCESS_3_8822B(x) |                        \
	 BIT_R_MACID_RELEASE_SUCCESS_3_8822B(v))

/* 2 REG_MGG_FIFO_CRTL_8822B */
#define BIT_R_MGG_FIFO_EN_8822B BIT(31)

#define BIT_SHIFT_R_MGG_FIFO_PG_SIZE_8822B 28
#define BIT_MASK_R_MGG_FIFO_PG_SIZE_8822B 0x7
#define BIT_R_MGG_FIFO_PG_SIZE_8822B(x)                                        \
	(((x) & BIT_MASK_R_MGG_FIFO_PG_SIZE_8822B)                             \
	 << BIT_SHIFT_R_MGG_FIFO_PG_SIZE_8822B)
#define BITS_R_MGG_FIFO_PG_SIZE_8822B                                          \
	(BIT_MASK_R_MGG_FIFO_PG_SIZE_8822B                                     \
	 << BIT_SHIFT_R_MGG_FIFO_PG_SIZE_8822B)
#define BIT_CLEAR_R_MGG_FIFO_PG_SIZE_8822B(x)                                  \
	((x) & (~BITS_R_MGG_FIFO_PG_SIZE_8822B))
#define BIT_GET_R_MGG_FIFO_PG_SIZE_8822B(x)                                    \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_PG_SIZE_8822B) &                         \
	 BIT_MASK_R_MGG_FIFO_PG_SIZE_8822B)
#define BIT_SET_R_MGG_FIFO_PG_SIZE_8822B(x, v)                                 \
	(BIT_CLEAR_R_MGG_FIFO_PG_SIZE_8822B(x) |                               \
	 BIT_R_MGG_FIFO_PG_SIZE_8822B(v))

#define BIT_SHIFT_R_MGG_FIFO_START_PG_8822B 16
#define BIT_MASK_R_MGG_FIFO_START_PG_8822B 0xfff
#define BIT_R_MGG_FIFO_START_PG_8822B(x)                                       \
	(((x) & BIT_MASK_R_MGG_FIFO_START_PG_8822B)                            \
	 << BIT_SHIFT_R_MGG_FIFO_START_PG_8822B)
#define BITS_R_MGG_FIFO_START_PG_8822B                                         \
	(BIT_MASK_R_MGG_FIFO_START_PG_8822B                                    \
	 << BIT_SHIFT_R_MGG_FIFO_START_PG_8822B)
#define BIT_CLEAR_R_MGG_FIFO_START_PG_8822B(x)                                 \
	((x) & (~BITS_R_MGG_FIFO_START_PG_8822B))
#define BIT_GET_R_MGG_FIFO_START_PG_8822B(x)                                   \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_START_PG_8822B) &                        \
	 BIT_MASK_R_MGG_FIFO_START_PG_8822B)
#define BIT_SET_R_MGG_FIFO_START_PG_8822B(x, v)                                \
	(BIT_CLEAR_R_MGG_FIFO_START_PG_8822B(x) |                              \
	 BIT_R_MGG_FIFO_START_PG_8822B(v))

#define BIT_SHIFT_R_MGG_FIFO_SIZE_8822B 14
#define BIT_MASK_R_MGG_FIFO_SIZE_8822B 0x3
#define BIT_R_MGG_FIFO_SIZE_8822B(x)                                           \
	(((x) & BIT_MASK_R_MGG_FIFO_SIZE_8822B)                                \
	 << BIT_SHIFT_R_MGG_FIFO_SIZE_8822B)
#define BITS_R_MGG_FIFO_SIZE_8822B                                             \
	(BIT_MASK_R_MGG_FIFO_SIZE_8822B << BIT_SHIFT_R_MGG_FIFO_SIZE_8822B)
#define BIT_CLEAR_R_MGG_FIFO_SIZE_8822B(x) ((x) & (~BITS_R_MGG_FIFO_SIZE_8822B))
#define BIT_GET_R_MGG_FIFO_SIZE_8822B(x)                                       \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_SIZE_8822B) &                            \
	 BIT_MASK_R_MGG_FIFO_SIZE_8822B)
#define BIT_SET_R_MGG_FIFO_SIZE_8822B(x, v)                                    \
	(BIT_CLEAR_R_MGG_FIFO_SIZE_8822B(x) | BIT_R_MGG_FIFO_SIZE_8822B(v))

#define BIT_R_MGG_FIFO_PAUSE_8822B BIT(13)

#define BIT_SHIFT_R_MGG_FIFO_RPTR_8822B 8
#define BIT_MASK_R_MGG_FIFO_RPTR_8822B 0x1f
#define BIT_R_MGG_FIFO_RPTR_8822B(x)                                           \
	(((x) & BIT_MASK_R_MGG_FIFO_RPTR_8822B)                                \
	 << BIT_SHIFT_R_MGG_FIFO_RPTR_8822B)
#define BITS_R_MGG_FIFO_RPTR_8822B                                             \
	(BIT_MASK_R_MGG_FIFO_RPTR_8822B << BIT_SHIFT_R_MGG_FIFO_RPTR_8822B)
#define BIT_CLEAR_R_MGG_FIFO_RPTR_8822B(x) ((x) & (~BITS_R_MGG_FIFO_RPTR_8822B))
#define BIT_GET_R_MGG_FIFO_RPTR_8822B(x)                                       \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_RPTR_8822B) &                            \
	 BIT_MASK_R_MGG_FIFO_RPTR_8822B)
#define BIT_SET_R_MGG_FIFO_RPTR_8822B(x, v)                                    \
	(BIT_CLEAR_R_MGG_FIFO_RPTR_8822B(x) | BIT_R_MGG_FIFO_RPTR_8822B(v))

#define BIT_R_MGG_FIFO_OV_8822B BIT(7)
#define BIT_R_MGG_FIFO_WPTR_ERROR_8822B BIT(6)
#define BIT_R_EN_CPU_LIFETIME_8822B BIT(5)

#define BIT_SHIFT_R_MGG_FIFO_WPTR_8822B 0
#define BIT_MASK_R_MGG_FIFO_WPTR_8822B 0x1f
#define BIT_R_MGG_FIFO_WPTR_8822B(x)                                           \
	(((x) & BIT_MASK_R_MGG_FIFO_WPTR_8822B)                                \
	 << BIT_SHIFT_R_MGG_FIFO_WPTR_8822B)
#define BITS_R_MGG_FIFO_WPTR_8822B                                             \
	(BIT_MASK_R_MGG_FIFO_WPTR_8822B << BIT_SHIFT_R_MGG_FIFO_WPTR_8822B)
#define BIT_CLEAR_R_MGG_FIFO_WPTR_8822B(x) ((x) & (~BITS_R_MGG_FIFO_WPTR_8822B))
#define BIT_GET_R_MGG_FIFO_WPTR_8822B(x)                                       \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_WPTR_8822B) &                            \
	 BIT_MASK_R_MGG_FIFO_WPTR_8822B)
#define BIT_SET_R_MGG_FIFO_WPTR_8822B(x, v)                                    \
	(BIT_CLEAR_R_MGG_FIFO_WPTR_8822B(x) | BIT_R_MGG_FIFO_WPTR_8822B(v))

/* 2 REG_MGG_FIFO_INT_8822B */

#define BIT_SHIFT_R_MGG_FIFO_INT_FLAG_8822B 16
#define BIT_MASK_R_MGG_FIFO_INT_FLAG_8822B 0xffff
#define BIT_R_MGG_FIFO_INT_FLAG_8822B(x)                                       \
	(((x) & BIT_MASK_R_MGG_FIFO_INT_FLAG_8822B)                            \
	 << BIT_SHIFT_R_MGG_FIFO_INT_FLAG_8822B)
#define BITS_R_MGG_FIFO_INT_FLAG_8822B                                         \
	(BIT_MASK_R_MGG_FIFO_INT_FLAG_8822B                                    \
	 << BIT_SHIFT_R_MGG_FIFO_INT_FLAG_8822B)
#define BIT_CLEAR_R_MGG_FIFO_INT_FLAG_8822B(x)                                 \
	((x) & (~BITS_R_MGG_FIFO_INT_FLAG_8822B))
#define BIT_GET_R_MGG_FIFO_INT_FLAG_8822B(x)                                   \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_INT_FLAG_8822B) &                        \
	 BIT_MASK_R_MGG_FIFO_INT_FLAG_8822B)
#define BIT_SET_R_MGG_FIFO_INT_FLAG_8822B(x, v)                                \
	(BIT_CLEAR_R_MGG_FIFO_INT_FLAG_8822B(x) |                              \
	 BIT_R_MGG_FIFO_INT_FLAG_8822B(v))

#define BIT_SHIFT_R_MGG_FIFO_INT_MASK_8822B 0
#define BIT_MASK_R_MGG_FIFO_INT_MASK_8822B 0xffff
#define BIT_R_MGG_FIFO_INT_MASK_8822B(x)                                       \
	(((x) & BIT_MASK_R_MGG_FIFO_INT_MASK_8822B)                            \
	 << BIT_SHIFT_R_MGG_FIFO_INT_MASK_8822B)
#define BITS_R_MGG_FIFO_INT_MASK_8822B                                         \
	(BIT_MASK_R_MGG_FIFO_INT_MASK_8822B                                    \
	 << BIT_SHIFT_R_MGG_FIFO_INT_MASK_8822B)
#define BIT_CLEAR_R_MGG_FIFO_INT_MASK_8822B(x)                                 \
	((x) & (~BITS_R_MGG_FIFO_INT_MASK_8822B))
#define BIT_GET_R_MGG_FIFO_INT_MASK_8822B(x)                                   \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_INT_MASK_8822B) &                        \
	 BIT_MASK_R_MGG_FIFO_INT_MASK_8822B)
#define BIT_SET_R_MGG_FIFO_INT_MASK_8822B(x, v)                                \
	(BIT_CLEAR_R_MGG_FIFO_INT_MASK_8822B(x) |                              \
	 BIT_R_MGG_FIFO_INT_MASK_8822B(v))

/* 2 REG_MGG_FIFO_LIFETIME_8822B */

#define BIT_SHIFT_R_MGG_FIFO_LIFETIME_8822B 16
#define BIT_MASK_R_MGG_FIFO_LIFETIME_8822B 0xffff
#define BIT_R_MGG_FIFO_LIFETIME_8822B(x)                                       \
	(((x) & BIT_MASK_R_MGG_FIFO_LIFETIME_8822B)                            \
	 << BIT_SHIFT_R_MGG_FIFO_LIFETIME_8822B)
#define BITS_R_MGG_FIFO_LIFETIME_8822B                                         \
	(BIT_MASK_R_MGG_FIFO_LIFETIME_8822B                                    \
	 << BIT_SHIFT_R_MGG_FIFO_LIFETIME_8822B)
#define BIT_CLEAR_R_MGG_FIFO_LIFETIME_8822B(x)                                 \
	((x) & (~BITS_R_MGG_FIFO_LIFETIME_8822B))
#define BIT_GET_R_MGG_FIFO_LIFETIME_8822B(x)                                   \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_LIFETIME_8822B) &                        \
	 BIT_MASK_R_MGG_FIFO_LIFETIME_8822B)
#define BIT_SET_R_MGG_FIFO_LIFETIME_8822B(x, v)                                \
	(BIT_CLEAR_R_MGG_FIFO_LIFETIME_8822B(x) |                              \
	 BIT_R_MGG_FIFO_LIFETIME_8822B(v))

#define BIT_SHIFT_R_MGG_FIFO_VALID_MAP_8822B 0
#define BIT_MASK_R_MGG_FIFO_VALID_MAP_8822B 0xffff
#define BIT_R_MGG_FIFO_VALID_MAP_8822B(x)                                      \
	(((x) & BIT_MASK_R_MGG_FIFO_VALID_MAP_8822B)                           \
	 << BIT_SHIFT_R_MGG_FIFO_VALID_MAP_8822B)
#define BITS_R_MGG_FIFO_VALID_MAP_8822B                                        \
	(BIT_MASK_R_MGG_FIFO_VALID_MAP_8822B                                   \
	 << BIT_SHIFT_R_MGG_FIFO_VALID_MAP_8822B)
#define BIT_CLEAR_R_MGG_FIFO_VALID_MAP_8822B(x)                                \
	((x) & (~BITS_R_MGG_FIFO_VALID_MAP_8822B))
#define BIT_GET_R_MGG_FIFO_VALID_MAP_8822B(x)                                  \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_VALID_MAP_8822B) &                       \
	 BIT_MASK_R_MGG_FIFO_VALID_MAP_8822B)
#define BIT_SET_R_MGG_FIFO_VALID_MAP_8822B(x, v)                               \
	(BIT_CLEAR_R_MGG_FIFO_VALID_MAP_8822B(x) |                             \
	 BIT_R_MGG_FIFO_VALID_MAP_8822B(v))

/* 2 REG_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8822B */

#define BIT_SHIFT_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8822B 0
#define BIT_MASK_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8822B 0x7f
#define BIT_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8822B(x)                      \
	(((x) & BIT_MASK_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8822B)           \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8822B)
#define BITS_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8822B                        \
	(BIT_MASK_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8822B                   \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8822B)
#define BIT_CLEAR_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8822B(x)                \
	((x) & (~BITS_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8822B))
#define BIT_GET_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8822B(x)                  \
	(((x) >> BIT_SHIFT_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8822B) &       \
	 BIT_MASK_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8822B)
#define BIT_SET_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8822B(x, v)               \
	(BIT_CLEAR_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8822B(x) |             \
	 BIT_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET_8822B(v))

/* 2 REG_SHCUT_SETTING_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_SHCUT_LLC_ETH_TYPE0_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_SHCUT_LLC_ETH_TYPE1_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_SHCUT_LLC_OUI0_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_SHCUT_LLC_OUI1_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_SHCUT_LLC_OUI2_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_SHCUT_LLC_OUI3_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_MU_TX_CTL_8822B */
#define BIT_R_EN_REVERS_GTAB_8822B BIT(6)

#define BIT_SHIFT_R_MU_TABLE_VALID_8822B 0
#define BIT_MASK_R_MU_TABLE_VALID_8822B 0x3f
#define BIT_R_MU_TABLE_VALID_8822B(x)                                          \
	(((x) & BIT_MASK_R_MU_TABLE_VALID_8822B)                               \
	 << BIT_SHIFT_R_MU_TABLE_VALID_8822B)
#define BITS_R_MU_TABLE_VALID_8822B                                            \
	(BIT_MASK_R_MU_TABLE_VALID_8822B << BIT_SHIFT_R_MU_TABLE_VALID_8822B)
#define BIT_CLEAR_R_MU_TABLE_VALID_8822B(x)                                    \
	((x) & (~BITS_R_MU_TABLE_VALID_8822B))
#define BIT_GET_R_MU_TABLE_VALID_8822B(x)                                      \
	(((x) >> BIT_SHIFT_R_MU_TABLE_VALID_8822B) &                           \
	 BIT_MASK_R_MU_TABLE_VALID_8822B)
#define BIT_SET_R_MU_TABLE_VALID_8822B(x, v)                                   \
	(BIT_CLEAR_R_MU_TABLE_VALID_8822B(x) | BIT_R_MU_TABLE_VALID_8822B(v))

/* 2 REG_MU_STA_GID_VLD_8822B */

/* 2 REG_NOT_VALID_8822B */

#define BIT_SHIFT_R_MU_STA_GTAB_VALID_8822B 0
#define BIT_MASK_R_MU_STA_GTAB_VALID_8822B 0xffffffffL
#define BIT_R_MU_STA_GTAB_VALID_8822B(x)                                       \
	(((x) & BIT_MASK_R_MU_STA_GTAB_VALID_8822B)                            \
	 << BIT_SHIFT_R_MU_STA_GTAB_VALID_8822B)
#define BITS_R_MU_STA_GTAB_VALID_8822B                                         \
	(BIT_MASK_R_MU_STA_GTAB_VALID_8822B                                    \
	 << BIT_SHIFT_R_MU_STA_GTAB_VALID_8822B)
#define BIT_CLEAR_R_MU_STA_GTAB_VALID_8822B(x)                                 \
	((x) & (~BITS_R_MU_STA_GTAB_VALID_8822B))
#define BIT_GET_R_MU_STA_GTAB_VALID_8822B(x)                                   \
	(((x) >> BIT_SHIFT_R_MU_STA_GTAB_VALID_8822B) &                        \
	 BIT_MASK_R_MU_STA_GTAB_VALID_8822B)
#define BIT_SET_R_MU_STA_GTAB_VALID_8822B(x, v)                                \
	(BIT_CLEAR_R_MU_STA_GTAB_VALID_8822B(x) |                              \
	 BIT_R_MU_STA_GTAB_VALID_8822B(v))

#define BIT_SHIFT_R_MU_STA_GTAB_VALID_8822B 0
#define BIT_MASK_R_MU_STA_GTAB_VALID_8822B 0xffffffffL
#define BIT_R_MU_STA_GTAB_VALID_8822B(x)                                       \
	(((x) & BIT_MASK_R_MU_STA_GTAB_VALID_8822B)                            \
	 << BIT_SHIFT_R_MU_STA_GTAB_VALID_8822B)
#define BITS_R_MU_STA_GTAB_VALID_8822B                                         \
	(BIT_MASK_R_MU_STA_GTAB_VALID_8822B                                    \
	 << BIT_SHIFT_R_MU_STA_GTAB_VALID_8822B)
#define BIT_CLEAR_R_MU_STA_GTAB_VALID_8822B(x)                                 \
	((x) & (~BITS_R_MU_STA_GTAB_VALID_8822B))
#define BIT_GET_R_MU_STA_GTAB_VALID_8822B(x)                                   \
	(((x) >> BIT_SHIFT_R_MU_STA_GTAB_VALID_8822B) &                        \
	 BIT_MASK_R_MU_STA_GTAB_VALID_8822B)
#define BIT_SET_R_MU_STA_GTAB_VALID_8822B(x, v)                                \
	(BIT_CLEAR_R_MU_STA_GTAB_VALID_8822B(x) |                              \
	 BIT_R_MU_STA_GTAB_VALID_8822B(v))

/* 2 REG_MU_STA_USER_POS_INFO_8822B */

/* 2 REG_NOT_VALID_8822B */

#define BIT_SHIFT_R_MU_STA_GTAB_POSITION_8822B 0
#define BIT_MASK_R_MU_STA_GTAB_POSITION_8822B 0xffffffffffffffffL
#define BIT_R_MU_STA_GTAB_POSITION_8822B(x)                                    \
	(((x) & BIT_MASK_R_MU_STA_GTAB_POSITION_8822B)                         \
	 << BIT_SHIFT_R_MU_STA_GTAB_POSITION_8822B)
#define BITS_R_MU_STA_GTAB_POSITION_8822B                                      \
	(BIT_MASK_R_MU_STA_GTAB_POSITION_8822B                                 \
	 << BIT_SHIFT_R_MU_STA_GTAB_POSITION_8822B)
#define BIT_CLEAR_R_MU_STA_GTAB_POSITION_8822B(x)                              \
	((x) & (~BITS_R_MU_STA_GTAB_POSITION_8822B))
#define BIT_GET_R_MU_STA_GTAB_POSITION_8822B(x)                                \
	(((x) >> BIT_SHIFT_R_MU_STA_GTAB_POSITION_8822B) &                     \
	 BIT_MASK_R_MU_STA_GTAB_POSITION_8822B)
#define BIT_SET_R_MU_STA_GTAB_POSITION_8822B(x, v)                             \
	(BIT_CLEAR_R_MU_STA_GTAB_POSITION_8822B(x) |                           \
	 BIT_R_MU_STA_GTAB_POSITION_8822B(v))

#define BIT_SHIFT_R_MU_STA_GTAB_POSITION_8822B 0
#define BIT_MASK_R_MU_STA_GTAB_POSITION_8822B 0xffffffffffffffffL
#define BIT_R_MU_STA_GTAB_POSITION_8822B(x)                                    \
	(((x) & BIT_MASK_R_MU_STA_GTAB_POSITION_8822B)                         \
	 << BIT_SHIFT_R_MU_STA_GTAB_POSITION_8822B)
#define BITS_R_MU_STA_GTAB_POSITION_8822B                                      \
	(BIT_MASK_R_MU_STA_GTAB_POSITION_8822B                                 \
	 << BIT_SHIFT_R_MU_STA_GTAB_POSITION_8822B)
#define BIT_CLEAR_R_MU_STA_GTAB_POSITION_8822B(x)                              \
	((x) & (~BITS_R_MU_STA_GTAB_POSITION_8822B))
#define BIT_GET_R_MU_STA_GTAB_POSITION_8822B(x)                                \
	(((x) >> BIT_SHIFT_R_MU_STA_GTAB_POSITION_8822B) &                     \
	 BIT_MASK_R_MU_STA_GTAB_POSITION_8822B)
#define BIT_SET_R_MU_STA_GTAB_POSITION_8822B(x, v)                             \
	(BIT_CLEAR_R_MU_STA_GTAB_POSITION_8822B(x) |                           \
	 BIT_R_MU_STA_GTAB_POSITION_8822B(v))

/* 2 REG_MU_TRX_DBG_CNT_8822B */
#define BIT_MU_DNGCNT_RST_8822B BIT(20)

#define BIT_SHIFT_MU_DBGCNT_SEL_8822B 16
#define BIT_MASK_MU_DBGCNT_SEL_8822B 0xf
#define BIT_MU_DBGCNT_SEL_8822B(x)                                             \
	(((x) & BIT_MASK_MU_DBGCNT_SEL_8822B) << BIT_SHIFT_MU_DBGCNT_SEL_8822B)
#define BITS_MU_DBGCNT_SEL_8822B                                               \
	(BIT_MASK_MU_DBGCNT_SEL_8822B << BIT_SHIFT_MU_DBGCNT_SEL_8822B)
#define BIT_CLEAR_MU_DBGCNT_SEL_8822B(x) ((x) & (~BITS_MU_DBGCNT_SEL_8822B))
#define BIT_GET_MU_DBGCNT_SEL_8822B(x)                                         \
	(((x) >> BIT_SHIFT_MU_DBGCNT_SEL_8822B) & BIT_MASK_MU_DBGCNT_SEL_8822B)
#define BIT_SET_MU_DBGCNT_SEL_8822B(x, v)                                      \
	(BIT_CLEAR_MU_DBGCNT_SEL_8822B(x) | BIT_MU_DBGCNT_SEL_8822B(v))

#define BIT_SHIFT_MU_DNGCNT_8822B 0
#define BIT_MASK_MU_DNGCNT_8822B 0xffff
#define BIT_MU_DNGCNT_8822B(x)                                                 \
	(((x) & BIT_MASK_MU_DNGCNT_8822B) << BIT_SHIFT_MU_DNGCNT_8822B)
#define BITS_MU_DNGCNT_8822B                                                   \
	(BIT_MASK_MU_DNGCNT_8822B << BIT_SHIFT_MU_DNGCNT_8822B)
#define BIT_CLEAR_MU_DNGCNT_8822B(x) ((x) & (~BITS_MU_DNGCNT_8822B))
#define BIT_GET_MU_DNGCNT_8822B(x)                                             \
	(((x) >> BIT_SHIFT_MU_DNGCNT_8822B) & BIT_MASK_MU_DNGCNT_8822B)
#define BIT_SET_MU_DNGCNT_8822B(x, v)                                          \
	(BIT_CLEAR_MU_DNGCNT_8822B(x) | BIT_MU_DNGCNT_8822B(v))

/* 2 REG_MU_TX_CTL_8822B */
#define BIT_R_EN_REVERS_GTAB_8822B BIT(6)

#define BIT_SHIFT_R_MU_TABLE_VALID_8822B 0
#define BIT_MASK_R_MU_TABLE_VALID_8822B 0x3f
#define BIT_R_MU_TABLE_VALID_8822B(x)                                          \
	(((x) & BIT_MASK_R_MU_TABLE_VALID_8822B)                               \
	 << BIT_SHIFT_R_MU_TABLE_VALID_8822B)
#define BITS_R_MU_TABLE_VALID_8822B                                            \
	(BIT_MASK_R_MU_TABLE_VALID_8822B << BIT_SHIFT_R_MU_TABLE_VALID_8822B)
#define BIT_CLEAR_R_MU_TABLE_VALID_8822B(x)                                    \
	((x) & (~BITS_R_MU_TABLE_VALID_8822B))
#define BIT_GET_R_MU_TABLE_VALID_8822B(x)                                      \
	(((x) >> BIT_SHIFT_R_MU_TABLE_VALID_8822B) &                           \
	 BIT_MASK_R_MU_TABLE_VALID_8822B)
#define BIT_SET_R_MU_TABLE_VALID_8822B(x, v)                                   \
	(BIT_CLEAR_R_MU_TABLE_VALID_8822B(x) | BIT_R_MU_TABLE_VALID_8822B(v))

/* 2 REG_MU_STA_GID_VLD_8822B */

/* 2 REG_NOT_VALID_8822B */

#define BIT_SHIFT_R_MU_STA_GTAB_VALID_8822B 0
#define BIT_MASK_R_MU_STA_GTAB_VALID_8822B 0xffffffffL
#define BIT_R_MU_STA_GTAB_VALID_8822B(x)                                       \
	(((x) & BIT_MASK_R_MU_STA_GTAB_VALID_8822B)                            \
	 << BIT_SHIFT_R_MU_STA_GTAB_VALID_8822B)
#define BITS_R_MU_STA_GTAB_VALID_8822B                                         \
	(BIT_MASK_R_MU_STA_GTAB_VALID_8822B                                    \
	 << BIT_SHIFT_R_MU_STA_GTAB_VALID_8822B)
#define BIT_CLEAR_R_MU_STA_GTAB_VALID_8822B(x)                                 \
	((x) & (~BITS_R_MU_STA_GTAB_VALID_8822B))
#define BIT_GET_R_MU_STA_GTAB_VALID_8822B(x)                                   \
	(((x) >> BIT_SHIFT_R_MU_STA_GTAB_VALID_8822B) &                        \
	 BIT_MASK_R_MU_STA_GTAB_VALID_8822B)
#define BIT_SET_R_MU_STA_GTAB_VALID_8822B(x, v)                                \
	(BIT_CLEAR_R_MU_STA_GTAB_VALID_8822B(x) |                              \
	 BIT_R_MU_STA_GTAB_VALID_8822B(v))

#define BIT_SHIFT_R_MU_STA_GTAB_VALID_8822B 0
#define BIT_MASK_R_MU_STA_GTAB_VALID_8822B 0xffffffffL
#define BIT_R_MU_STA_GTAB_VALID_8822B(x)                                       \
	(((x) & BIT_MASK_R_MU_STA_GTAB_VALID_8822B)                            \
	 << BIT_SHIFT_R_MU_STA_GTAB_VALID_8822B)
#define BITS_R_MU_STA_GTAB_VALID_8822B                                         \
	(BIT_MASK_R_MU_STA_GTAB_VALID_8822B                                    \
	 << BIT_SHIFT_R_MU_STA_GTAB_VALID_8822B)
#define BIT_CLEAR_R_MU_STA_GTAB_VALID_8822B(x)                                 \
	((x) & (~BITS_R_MU_STA_GTAB_VALID_8822B))
#define BIT_GET_R_MU_STA_GTAB_VALID_8822B(x)                                   \
	(((x) >> BIT_SHIFT_R_MU_STA_GTAB_VALID_8822B) &                        \
	 BIT_MASK_R_MU_STA_GTAB_VALID_8822B)
#define BIT_SET_R_MU_STA_GTAB_VALID_8822B(x, v)                                \
	(BIT_CLEAR_R_MU_STA_GTAB_VALID_8822B(x) |                              \
	 BIT_R_MU_STA_GTAB_VALID_8822B(v))

/* 2 REG_MU_STA_USER_POS_INFO_8822B */

/* 2 REG_NOT_VALID_8822B */

#define BIT_SHIFT_R_MU_STA_GTAB_POSITION_8822B 0
#define BIT_MASK_R_MU_STA_GTAB_POSITION_8822B 0xffffffffffffffffL
#define BIT_R_MU_STA_GTAB_POSITION_8822B(x)                                    \
	(((x) & BIT_MASK_R_MU_STA_GTAB_POSITION_8822B)                         \
	 << BIT_SHIFT_R_MU_STA_GTAB_POSITION_8822B)
#define BITS_R_MU_STA_GTAB_POSITION_8822B                                      \
	(BIT_MASK_R_MU_STA_GTAB_POSITION_8822B                                 \
	 << BIT_SHIFT_R_MU_STA_GTAB_POSITION_8822B)
#define BIT_CLEAR_R_MU_STA_GTAB_POSITION_8822B(x)                              \
	((x) & (~BITS_R_MU_STA_GTAB_POSITION_8822B))
#define BIT_GET_R_MU_STA_GTAB_POSITION_8822B(x)                                \
	(((x) >> BIT_SHIFT_R_MU_STA_GTAB_POSITION_8822B) &                     \
	 BIT_MASK_R_MU_STA_GTAB_POSITION_8822B)
#define BIT_SET_R_MU_STA_GTAB_POSITION_8822B(x, v)                             \
	(BIT_CLEAR_R_MU_STA_GTAB_POSITION_8822B(x) |                           \
	 BIT_R_MU_STA_GTAB_POSITION_8822B(v))

#define BIT_SHIFT_R_MU_STA_GTAB_POSITION_8822B 0
#define BIT_MASK_R_MU_STA_GTAB_POSITION_8822B 0xffffffffffffffffL
#define BIT_R_MU_STA_GTAB_POSITION_8822B(x)                                    \
	(((x) & BIT_MASK_R_MU_STA_GTAB_POSITION_8822B)                         \
	 << BIT_SHIFT_R_MU_STA_GTAB_POSITION_8822B)
#define BITS_R_MU_STA_GTAB_POSITION_8822B                                      \
	(BIT_MASK_R_MU_STA_GTAB_POSITION_8822B                                 \
	 << BIT_SHIFT_R_MU_STA_GTAB_POSITION_8822B)
#define BIT_CLEAR_R_MU_STA_GTAB_POSITION_8822B(x)                              \
	((x) & (~BITS_R_MU_STA_GTAB_POSITION_8822B))
#define BIT_GET_R_MU_STA_GTAB_POSITION_8822B(x)                                \
	(((x) >> BIT_SHIFT_R_MU_STA_GTAB_POSITION_8822B) &                     \
	 BIT_MASK_R_MU_STA_GTAB_POSITION_8822B)
#define BIT_SET_R_MU_STA_GTAB_POSITION_8822B(x, v)                             \
	(BIT_CLEAR_R_MU_STA_GTAB_POSITION_8822B(x) |                           \
	 BIT_R_MU_STA_GTAB_POSITION_8822B(v))

/* 2 REG_MU_TRX_DBG_CNT_8822B */
#define BIT_MU_DNGCNT_RST_8822B BIT(20)

#define BIT_SHIFT_MU_DBGCNT_SEL_8822B 16
#define BIT_MASK_MU_DBGCNT_SEL_8822B 0xf
#define BIT_MU_DBGCNT_SEL_8822B(x)                                             \
	(((x) & BIT_MASK_MU_DBGCNT_SEL_8822B) << BIT_SHIFT_MU_DBGCNT_SEL_8822B)
#define BITS_MU_DBGCNT_SEL_8822B                                               \
	(BIT_MASK_MU_DBGCNT_SEL_8822B << BIT_SHIFT_MU_DBGCNT_SEL_8822B)
#define BIT_CLEAR_MU_DBGCNT_SEL_8822B(x) ((x) & (~BITS_MU_DBGCNT_SEL_8822B))
#define BIT_GET_MU_DBGCNT_SEL_8822B(x)                                         \
	(((x) >> BIT_SHIFT_MU_DBGCNT_SEL_8822B) & BIT_MASK_MU_DBGCNT_SEL_8822B)
#define BIT_SET_MU_DBGCNT_SEL_8822B(x, v)                                      \
	(BIT_CLEAR_MU_DBGCNT_SEL_8822B(x) | BIT_MU_DBGCNT_SEL_8822B(v))

#define BIT_SHIFT_MU_DNGCNT_8822B 0
#define BIT_MASK_MU_DNGCNT_8822B 0xffff
#define BIT_MU_DNGCNT_8822B(x)                                                 \
	(((x) & BIT_MASK_MU_DNGCNT_8822B) << BIT_SHIFT_MU_DNGCNT_8822B)
#define BITS_MU_DNGCNT_8822B                                                   \
	(BIT_MASK_MU_DNGCNT_8822B << BIT_SHIFT_MU_DNGCNT_8822B)
#define BIT_CLEAR_MU_DNGCNT_8822B(x) ((x) & (~BITS_MU_DNGCNT_8822B))
#define BIT_GET_MU_DNGCNT_8822B(x)                                             \
	(((x) >> BIT_SHIFT_MU_DNGCNT_8822B) & BIT_MASK_MU_DNGCNT_8822B)
#define BIT_SET_MU_DNGCNT_8822B(x, v)                                          \
	(BIT_CLEAR_MU_DNGCNT_8822B(x) | BIT_MU_DNGCNT_8822B(v))

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_EDCA_VO_PARAM_8822B */

#define BIT_SHIFT_TXOPLIMIT_8822B 16
#define BIT_MASK_TXOPLIMIT_8822B 0x7ff
#define BIT_TXOPLIMIT_8822B(x)                                                 \
	(((x) & BIT_MASK_TXOPLIMIT_8822B) << BIT_SHIFT_TXOPLIMIT_8822B)
#define BITS_TXOPLIMIT_8822B                                                   \
	(BIT_MASK_TXOPLIMIT_8822B << BIT_SHIFT_TXOPLIMIT_8822B)
#define BIT_CLEAR_TXOPLIMIT_8822B(x) ((x) & (~BITS_TXOPLIMIT_8822B))
#define BIT_GET_TXOPLIMIT_8822B(x)                                             \
	(((x) >> BIT_SHIFT_TXOPLIMIT_8822B) & BIT_MASK_TXOPLIMIT_8822B)
#define BIT_SET_TXOPLIMIT_8822B(x, v)                                          \
	(BIT_CLEAR_TXOPLIMIT_8822B(x) | BIT_TXOPLIMIT_8822B(v))

#define BIT_SHIFT_CW_8822B 8
#define BIT_MASK_CW_8822B 0xff
#define BIT_CW_8822B(x) (((x) & BIT_MASK_CW_8822B) << BIT_SHIFT_CW_8822B)
#define BITS_CW_8822B (BIT_MASK_CW_8822B << BIT_SHIFT_CW_8822B)
#define BIT_CLEAR_CW_8822B(x) ((x) & (~BITS_CW_8822B))
#define BIT_GET_CW_8822B(x) (((x) >> BIT_SHIFT_CW_8822B) & BIT_MASK_CW_8822B)
#define BIT_SET_CW_8822B(x, v) (BIT_CLEAR_CW_8822B(x) | BIT_CW_8822B(v))

#define BIT_SHIFT_AIFS_8822B 0
#define BIT_MASK_AIFS_8822B 0xff
#define BIT_AIFS_8822B(x) (((x) & BIT_MASK_AIFS_8822B) << BIT_SHIFT_AIFS_8822B)
#define BITS_AIFS_8822B (BIT_MASK_AIFS_8822B << BIT_SHIFT_AIFS_8822B)
#define BIT_CLEAR_AIFS_8822B(x) ((x) & (~BITS_AIFS_8822B))
#define BIT_GET_AIFS_8822B(x)                                                  \
	(((x) >> BIT_SHIFT_AIFS_8822B) & BIT_MASK_AIFS_8822B)
#define BIT_SET_AIFS_8822B(x, v) (BIT_CLEAR_AIFS_8822B(x) | BIT_AIFS_8822B(v))

/* 2 REG_EDCA_VI_PARAM_8822B */

/* 2 REG_NOT_VALID_8822B */

#define BIT_SHIFT_TXOPLIMIT_8822B 16
#define BIT_MASK_TXOPLIMIT_8822B 0x7ff
#define BIT_TXOPLIMIT_8822B(x)                                                 \
	(((x) & BIT_MASK_TXOPLIMIT_8822B) << BIT_SHIFT_TXOPLIMIT_8822B)
#define BITS_TXOPLIMIT_8822B                                                   \
	(BIT_MASK_TXOPLIMIT_8822B << BIT_SHIFT_TXOPLIMIT_8822B)
#define BIT_CLEAR_TXOPLIMIT_8822B(x) ((x) & (~BITS_TXOPLIMIT_8822B))
#define BIT_GET_TXOPLIMIT_8822B(x)                                             \
	(((x) >> BIT_SHIFT_TXOPLIMIT_8822B) & BIT_MASK_TXOPLIMIT_8822B)
#define BIT_SET_TXOPLIMIT_8822B(x, v)                                          \
	(BIT_CLEAR_TXOPLIMIT_8822B(x) | BIT_TXOPLIMIT_8822B(v))

#define BIT_SHIFT_CW_8822B 8
#define BIT_MASK_CW_8822B 0xff
#define BIT_CW_8822B(x) (((x) & BIT_MASK_CW_8822B) << BIT_SHIFT_CW_8822B)
#define BITS_CW_8822B (BIT_MASK_CW_8822B << BIT_SHIFT_CW_8822B)
#define BIT_CLEAR_CW_8822B(x) ((x) & (~BITS_CW_8822B))
#define BIT_GET_CW_8822B(x) (((x) >> BIT_SHIFT_CW_8822B) & BIT_MASK_CW_8822B)
#define BIT_SET_CW_8822B(x, v) (BIT_CLEAR_CW_8822B(x) | BIT_CW_8822B(v))

#define BIT_SHIFT_AIFS_8822B 0
#define BIT_MASK_AIFS_8822B 0xff
#define BIT_AIFS_8822B(x) (((x) & BIT_MASK_AIFS_8822B) << BIT_SHIFT_AIFS_8822B)
#define BITS_AIFS_8822B (BIT_MASK_AIFS_8822B << BIT_SHIFT_AIFS_8822B)
#define BIT_CLEAR_AIFS_8822B(x) ((x) & (~BITS_AIFS_8822B))
#define BIT_GET_AIFS_8822B(x)                                                  \
	(((x) >> BIT_SHIFT_AIFS_8822B) & BIT_MASK_AIFS_8822B)
#define BIT_SET_AIFS_8822B(x, v) (BIT_CLEAR_AIFS_8822B(x) | BIT_AIFS_8822B(v))

/* 2 REG_EDCA_BE_PARAM_8822B */

/* 2 REG_NOT_VALID_8822B */

#define BIT_SHIFT_TXOPLIMIT_8822B 16
#define BIT_MASK_TXOPLIMIT_8822B 0x7ff
#define BIT_TXOPLIMIT_8822B(x)                                                 \
	(((x) & BIT_MASK_TXOPLIMIT_8822B) << BIT_SHIFT_TXOPLIMIT_8822B)
#define BITS_TXOPLIMIT_8822B                                                   \
	(BIT_MASK_TXOPLIMIT_8822B << BIT_SHIFT_TXOPLIMIT_8822B)
#define BIT_CLEAR_TXOPLIMIT_8822B(x) ((x) & (~BITS_TXOPLIMIT_8822B))
#define BIT_GET_TXOPLIMIT_8822B(x)                                             \
	(((x) >> BIT_SHIFT_TXOPLIMIT_8822B) & BIT_MASK_TXOPLIMIT_8822B)
#define BIT_SET_TXOPLIMIT_8822B(x, v)                                          \
	(BIT_CLEAR_TXOPLIMIT_8822B(x) | BIT_TXOPLIMIT_8822B(v))

#define BIT_SHIFT_CW_8822B 8
#define BIT_MASK_CW_8822B 0xff
#define BIT_CW_8822B(x) (((x) & BIT_MASK_CW_8822B) << BIT_SHIFT_CW_8822B)
#define BITS_CW_8822B (BIT_MASK_CW_8822B << BIT_SHIFT_CW_8822B)
#define BIT_CLEAR_CW_8822B(x) ((x) & (~BITS_CW_8822B))
#define BIT_GET_CW_8822B(x) (((x) >> BIT_SHIFT_CW_8822B) & BIT_MASK_CW_8822B)
#define BIT_SET_CW_8822B(x, v) (BIT_CLEAR_CW_8822B(x) | BIT_CW_8822B(v))

#define BIT_SHIFT_AIFS_8822B 0
#define BIT_MASK_AIFS_8822B 0xff
#define BIT_AIFS_8822B(x) (((x) & BIT_MASK_AIFS_8822B) << BIT_SHIFT_AIFS_8822B)
#define BITS_AIFS_8822B (BIT_MASK_AIFS_8822B << BIT_SHIFT_AIFS_8822B)
#define BIT_CLEAR_AIFS_8822B(x) ((x) & (~BITS_AIFS_8822B))
#define BIT_GET_AIFS_8822B(x)                                                  \
	(((x) >> BIT_SHIFT_AIFS_8822B) & BIT_MASK_AIFS_8822B)
#define BIT_SET_AIFS_8822B(x, v) (BIT_CLEAR_AIFS_8822B(x) | BIT_AIFS_8822B(v))

/* 2 REG_EDCA_BK_PARAM_8822B */

/* 2 REG_NOT_VALID_8822B */

#define BIT_SHIFT_TXOPLIMIT_8822B 16
#define BIT_MASK_TXOPLIMIT_8822B 0x7ff
#define BIT_TXOPLIMIT_8822B(x)                                                 \
	(((x) & BIT_MASK_TXOPLIMIT_8822B) << BIT_SHIFT_TXOPLIMIT_8822B)
#define BITS_TXOPLIMIT_8822B                                                   \
	(BIT_MASK_TXOPLIMIT_8822B << BIT_SHIFT_TXOPLIMIT_8822B)
#define BIT_CLEAR_TXOPLIMIT_8822B(x) ((x) & (~BITS_TXOPLIMIT_8822B))
#define BIT_GET_TXOPLIMIT_8822B(x)                                             \
	(((x) >> BIT_SHIFT_TXOPLIMIT_8822B) & BIT_MASK_TXOPLIMIT_8822B)
#define BIT_SET_TXOPLIMIT_8822B(x, v)                                          \
	(BIT_CLEAR_TXOPLIMIT_8822B(x) | BIT_TXOPLIMIT_8822B(v))

#define BIT_SHIFT_CW_8822B 8
#define BIT_MASK_CW_8822B 0xff
#define BIT_CW_8822B(x) (((x) & BIT_MASK_CW_8822B) << BIT_SHIFT_CW_8822B)
#define BITS_CW_8822B (BIT_MASK_CW_8822B << BIT_SHIFT_CW_8822B)
#define BIT_CLEAR_CW_8822B(x) ((x) & (~BITS_CW_8822B))
#define BIT_GET_CW_8822B(x) (((x) >> BIT_SHIFT_CW_8822B) & BIT_MASK_CW_8822B)
#define BIT_SET_CW_8822B(x, v) (BIT_CLEAR_CW_8822B(x) | BIT_CW_8822B(v))

#define BIT_SHIFT_AIFS_8822B 0
#define BIT_MASK_AIFS_8822B 0xff
#define BIT_AIFS_8822B(x) (((x) & BIT_MASK_AIFS_8822B) << BIT_SHIFT_AIFS_8822B)
#define BITS_AIFS_8822B (BIT_MASK_AIFS_8822B << BIT_SHIFT_AIFS_8822B)
#define BIT_CLEAR_AIFS_8822B(x) ((x) & (~BITS_AIFS_8822B))
#define BIT_GET_AIFS_8822B(x)                                                  \
	(((x) >> BIT_SHIFT_AIFS_8822B) & BIT_MASK_AIFS_8822B)
#define BIT_SET_AIFS_8822B(x, v) (BIT_CLEAR_AIFS_8822B(x) | BIT_AIFS_8822B(v))

/* 2 REG_BCNTCFG_8822B */

#define BIT_SHIFT_BCNCW_MAX_8822B 12
#define BIT_MASK_BCNCW_MAX_8822B 0xf
#define BIT_BCNCW_MAX_8822B(x)                                                 \
	(((x) & BIT_MASK_BCNCW_MAX_8822B) << BIT_SHIFT_BCNCW_MAX_8822B)
#define BITS_BCNCW_MAX_8822B                                                   \
	(BIT_MASK_BCNCW_MAX_8822B << BIT_SHIFT_BCNCW_MAX_8822B)
#define BIT_CLEAR_BCNCW_MAX_8822B(x) ((x) & (~BITS_BCNCW_MAX_8822B))
#define BIT_GET_BCNCW_MAX_8822B(x)                                             \
	(((x) >> BIT_SHIFT_BCNCW_MAX_8822B) & BIT_MASK_BCNCW_MAX_8822B)
#define BIT_SET_BCNCW_MAX_8822B(x, v)                                          \
	(BIT_CLEAR_BCNCW_MAX_8822B(x) | BIT_BCNCW_MAX_8822B(v))

#define BIT_SHIFT_BCNCW_MIN_8822B 8
#define BIT_MASK_BCNCW_MIN_8822B 0xf
#define BIT_BCNCW_MIN_8822B(x)                                                 \
	(((x) & BIT_MASK_BCNCW_MIN_8822B) << BIT_SHIFT_BCNCW_MIN_8822B)
#define BITS_BCNCW_MIN_8822B                                                   \
	(BIT_MASK_BCNCW_MIN_8822B << BIT_SHIFT_BCNCW_MIN_8822B)
#define BIT_CLEAR_BCNCW_MIN_8822B(x) ((x) & (~BITS_BCNCW_MIN_8822B))
#define BIT_GET_BCNCW_MIN_8822B(x)                                             \
	(((x) >> BIT_SHIFT_BCNCW_MIN_8822B) & BIT_MASK_BCNCW_MIN_8822B)
#define BIT_SET_BCNCW_MIN_8822B(x, v)                                          \
	(BIT_CLEAR_BCNCW_MIN_8822B(x) | BIT_BCNCW_MIN_8822B(v))

#define BIT_SHIFT_BCNIFS_8822B 0
#define BIT_MASK_BCNIFS_8822B 0xff
#define BIT_BCNIFS_8822B(x)                                                    \
	(((x) & BIT_MASK_BCNIFS_8822B) << BIT_SHIFT_BCNIFS_8822B)
#define BITS_BCNIFS_8822B (BIT_MASK_BCNIFS_8822B << BIT_SHIFT_BCNIFS_8822B)
#define BIT_CLEAR_BCNIFS_8822B(x) ((x) & (~BITS_BCNIFS_8822B))
#define BIT_GET_BCNIFS_8822B(x)                                                \
	(((x) >> BIT_SHIFT_BCNIFS_8822B) & BIT_MASK_BCNIFS_8822B)
#define BIT_SET_BCNIFS_8822B(x, v)                                             \
	(BIT_CLEAR_BCNIFS_8822B(x) | BIT_BCNIFS_8822B(v))

/* 2 REG_PIFS_8822B */

#define BIT_SHIFT_PIFS_8822B 0
#define BIT_MASK_PIFS_8822B 0xff
#define BIT_PIFS_8822B(x) (((x) & BIT_MASK_PIFS_8822B) << BIT_SHIFT_PIFS_8822B)
#define BITS_PIFS_8822B (BIT_MASK_PIFS_8822B << BIT_SHIFT_PIFS_8822B)
#define BIT_CLEAR_PIFS_8822B(x) ((x) & (~BITS_PIFS_8822B))
#define BIT_GET_PIFS_8822B(x)                                                  \
	(((x) >> BIT_SHIFT_PIFS_8822B) & BIT_MASK_PIFS_8822B)
#define BIT_SET_PIFS_8822B(x, v) (BIT_CLEAR_PIFS_8822B(x) | BIT_PIFS_8822B(v))

/* 2 REG_RDG_PIFS_8822B */

#define BIT_SHIFT_RDG_PIFS_8822B 0
#define BIT_MASK_RDG_PIFS_8822B 0xff
#define BIT_RDG_PIFS_8822B(x)                                                  \
	(((x) & BIT_MASK_RDG_PIFS_8822B) << BIT_SHIFT_RDG_PIFS_8822B)
#define BITS_RDG_PIFS_8822B                                                    \
	(BIT_MASK_RDG_PIFS_8822B << BIT_SHIFT_RDG_PIFS_8822B)
#define BIT_CLEAR_RDG_PIFS_8822B(x) ((x) & (~BITS_RDG_PIFS_8822B))
#define BIT_GET_RDG_PIFS_8822B(x)                                              \
	(((x) >> BIT_SHIFT_RDG_PIFS_8822B) & BIT_MASK_RDG_PIFS_8822B)
#define BIT_SET_RDG_PIFS_8822B(x, v)                                           \
	(BIT_CLEAR_RDG_PIFS_8822B(x) | BIT_RDG_PIFS_8822B(v))

/* 2 REG_SIFS_8822B */

#define BIT_SHIFT_SIFS_OFDM_TRX_8822B 24
#define BIT_MASK_SIFS_OFDM_TRX_8822B 0xff
#define BIT_SIFS_OFDM_TRX_8822B(x)                                             \
	(((x) & BIT_MASK_SIFS_OFDM_TRX_8822B) << BIT_SHIFT_SIFS_OFDM_TRX_8822B)
#define BITS_SIFS_OFDM_TRX_8822B                                               \
	(BIT_MASK_SIFS_OFDM_TRX_8822B << BIT_SHIFT_SIFS_OFDM_TRX_8822B)
#define BIT_CLEAR_SIFS_OFDM_TRX_8822B(x) ((x) & (~BITS_SIFS_OFDM_TRX_8822B))
#define BIT_GET_SIFS_OFDM_TRX_8822B(x)                                         \
	(((x) >> BIT_SHIFT_SIFS_OFDM_TRX_8822B) & BIT_MASK_SIFS_OFDM_TRX_8822B)
#define BIT_SET_SIFS_OFDM_TRX_8822B(x, v)                                      \
	(BIT_CLEAR_SIFS_OFDM_TRX_8822B(x) | BIT_SIFS_OFDM_TRX_8822B(v))

#define BIT_SHIFT_SIFS_CCK_TRX_8822B 16
#define BIT_MASK_SIFS_CCK_TRX_8822B 0xff
#define BIT_SIFS_CCK_TRX_8822B(x)                                              \
	(((x) & BIT_MASK_SIFS_CCK_TRX_8822B) << BIT_SHIFT_SIFS_CCK_TRX_8822B)
#define BITS_SIFS_CCK_TRX_8822B                                                \
	(BIT_MASK_SIFS_CCK_TRX_8822B << BIT_SHIFT_SIFS_CCK_TRX_8822B)
#define BIT_CLEAR_SIFS_CCK_TRX_8822B(x) ((x) & (~BITS_SIFS_CCK_TRX_8822B))
#define BIT_GET_SIFS_CCK_TRX_8822B(x)                                          \
	(((x) >> BIT_SHIFT_SIFS_CCK_TRX_8822B) & BIT_MASK_SIFS_CCK_TRX_8822B)
#define BIT_SET_SIFS_CCK_TRX_8822B(x, v)                                       \
	(BIT_CLEAR_SIFS_CCK_TRX_8822B(x) | BIT_SIFS_CCK_TRX_8822B(v))

#define BIT_SHIFT_SIFS_OFDM_CTX_8822B 8
#define BIT_MASK_SIFS_OFDM_CTX_8822B 0xff
#define BIT_SIFS_OFDM_CTX_8822B(x)                                             \
	(((x) & BIT_MASK_SIFS_OFDM_CTX_8822B) << BIT_SHIFT_SIFS_OFDM_CTX_8822B)
#define BITS_SIFS_OFDM_CTX_8822B                                               \
	(BIT_MASK_SIFS_OFDM_CTX_8822B << BIT_SHIFT_SIFS_OFDM_CTX_8822B)
#define BIT_CLEAR_SIFS_OFDM_CTX_8822B(x) ((x) & (~BITS_SIFS_OFDM_CTX_8822B))
#define BIT_GET_SIFS_OFDM_CTX_8822B(x)                                         \
	(((x) >> BIT_SHIFT_SIFS_OFDM_CTX_8822B) & BIT_MASK_SIFS_OFDM_CTX_8822B)
#define BIT_SET_SIFS_OFDM_CTX_8822B(x, v)                                      \
	(BIT_CLEAR_SIFS_OFDM_CTX_8822B(x) | BIT_SIFS_OFDM_CTX_8822B(v))

#define BIT_SHIFT_SIFS_CCK_CTX_8822B 0
#define BIT_MASK_SIFS_CCK_CTX_8822B 0xff
#define BIT_SIFS_CCK_CTX_8822B(x)                                              \
	(((x) & BIT_MASK_SIFS_CCK_CTX_8822B) << BIT_SHIFT_SIFS_CCK_CTX_8822B)
#define BITS_SIFS_CCK_CTX_8822B                                                \
	(BIT_MASK_SIFS_CCK_CTX_8822B << BIT_SHIFT_SIFS_CCK_CTX_8822B)
#define BIT_CLEAR_SIFS_CCK_CTX_8822B(x) ((x) & (~BITS_SIFS_CCK_CTX_8822B))
#define BIT_GET_SIFS_CCK_CTX_8822B(x)                                          \
	(((x) >> BIT_SHIFT_SIFS_CCK_CTX_8822B) & BIT_MASK_SIFS_CCK_CTX_8822B)
#define BIT_SET_SIFS_CCK_CTX_8822B(x, v)                                       \
	(BIT_CLEAR_SIFS_CCK_CTX_8822B(x) | BIT_SIFS_CCK_CTX_8822B(v))

/* 2 REG_TSFTR_SYN_OFFSET_8822B */

#define BIT_SHIFT_TSFTR_SNC_OFFSET_8822B 0
#define BIT_MASK_TSFTR_SNC_OFFSET_8822B 0xffff
#define BIT_TSFTR_SNC_OFFSET_8822B(x)                                          \
	(((x) & BIT_MASK_TSFTR_SNC_OFFSET_8822B)                               \
	 << BIT_SHIFT_TSFTR_SNC_OFFSET_8822B)
#define BITS_TSFTR_SNC_OFFSET_8822B                                            \
	(BIT_MASK_TSFTR_SNC_OFFSET_8822B << BIT_SHIFT_TSFTR_SNC_OFFSET_8822B)
#define BIT_CLEAR_TSFTR_SNC_OFFSET_8822B(x)                                    \
	((x) & (~BITS_TSFTR_SNC_OFFSET_8822B))
#define BIT_GET_TSFTR_SNC_OFFSET_8822B(x)                                      \
	(((x) >> BIT_SHIFT_TSFTR_SNC_OFFSET_8822B) &                           \
	 BIT_MASK_TSFTR_SNC_OFFSET_8822B)
#define BIT_SET_TSFTR_SNC_OFFSET_8822B(x, v)                                   \
	(BIT_CLEAR_TSFTR_SNC_OFFSET_8822B(x) | BIT_TSFTR_SNC_OFFSET_8822B(v))

/* 2 REG_AGGR_BREAK_TIME_8822B */

#define BIT_SHIFT_AGGR_BK_TIME_8822B 0
#define BIT_MASK_AGGR_BK_TIME_8822B 0xff
#define BIT_AGGR_BK_TIME_8822B(x)                                              \
	(((x) & BIT_MASK_AGGR_BK_TIME_8822B) << BIT_SHIFT_AGGR_BK_TIME_8822B)
#define BITS_AGGR_BK_TIME_8822B                                                \
	(BIT_MASK_AGGR_BK_TIME_8822B << BIT_SHIFT_AGGR_BK_TIME_8822B)
#define BIT_CLEAR_AGGR_BK_TIME_8822B(x) ((x) & (~BITS_AGGR_BK_TIME_8822B))
#define BIT_GET_AGGR_BK_TIME_8822B(x)                                          \
	(((x) >> BIT_SHIFT_AGGR_BK_TIME_8822B) & BIT_MASK_AGGR_BK_TIME_8822B)
#define BIT_SET_AGGR_BK_TIME_8822B(x, v)                                       \
	(BIT_CLEAR_AGGR_BK_TIME_8822B(x) | BIT_AGGR_BK_TIME_8822B(v))

/* 2 REG_SLOT_8822B */

#define BIT_SHIFT_SLOT_8822B 0
#define BIT_MASK_SLOT_8822B 0xff
#define BIT_SLOT_8822B(x) (((x) & BIT_MASK_SLOT_8822B) << BIT_SHIFT_SLOT_8822B)
#define BITS_SLOT_8822B (BIT_MASK_SLOT_8822B << BIT_SHIFT_SLOT_8822B)
#define BIT_CLEAR_SLOT_8822B(x) ((x) & (~BITS_SLOT_8822B))
#define BIT_GET_SLOT_8822B(x)                                                  \
	(((x) >> BIT_SHIFT_SLOT_8822B) & BIT_MASK_SLOT_8822B)
#define BIT_SET_SLOT_8822B(x, v) (BIT_CLEAR_SLOT_8822B(x) | BIT_SLOT_8822B(v))

/* 2 REG_TX_PTCL_CTRL_8822B */
#define BIT_DIS_EDCCA_8822B BIT(15)
#define BIT_DIS_CCA_8822B BIT(14)
#define BIT_LSIG_TXOP_TXCMD_NAV_8822B BIT(13)
#define BIT_SIFS_BK_EN_8822B BIT(12)

#define BIT_SHIFT_TXQ_NAV_MSK_8822B 8
#define BIT_MASK_TXQ_NAV_MSK_8822B 0xf
#define BIT_TXQ_NAV_MSK_8822B(x)                                               \
	(((x) & BIT_MASK_TXQ_NAV_MSK_8822B) << BIT_SHIFT_TXQ_NAV_MSK_8822B)
#define BITS_TXQ_NAV_MSK_8822B                                                 \
	(BIT_MASK_TXQ_NAV_MSK_8822B << BIT_SHIFT_TXQ_NAV_MSK_8822B)
#define BIT_CLEAR_TXQ_NAV_MSK_8822B(x) ((x) & (~BITS_TXQ_NAV_MSK_8822B))
#define BIT_GET_TXQ_NAV_MSK_8822B(x)                                           \
	(((x) >> BIT_SHIFT_TXQ_NAV_MSK_8822B) & BIT_MASK_TXQ_NAV_MSK_8822B)
#define BIT_SET_TXQ_NAV_MSK_8822B(x, v)                                        \
	(BIT_CLEAR_TXQ_NAV_MSK_8822B(x) | BIT_TXQ_NAV_MSK_8822B(v))

#define BIT_DIS_CW_8822B BIT(7)
#define BIT_NAV_END_TXOP_8822B BIT(6)
#define BIT_RDG_END_TXOP_8822B BIT(5)
#define BIT_AC_INBCN_HOLD_8822B BIT(4)
#define BIT_MGTQ_TXOP_EN_8822B BIT(3)
#define BIT_MGTQ_RTSMF_EN_8822B BIT(2)
#define BIT_HIQ_RTSMF_EN_8822B BIT(1)
#define BIT_BCN_RTSMF_EN_8822B BIT(0)

/* 2 REG_TXPAUSE_8822B */
#define BIT_STOP_BCN_HI_MGT_8822B BIT(7)
#define BIT_MAC_STOPBCNQ_8822B BIT(6)
#define BIT_MAC_STOPHIQ_8822B BIT(5)
#define BIT_MAC_STOPMGQ_8822B BIT(4)
#define BIT_MAC_STOPBK_8822B BIT(3)
#define BIT_MAC_STOPBE_8822B BIT(2)
#define BIT_MAC_STOPVI_8822B BIT(1)
#define BIT_MAC_STOPVO_8822B BIT(0)

/* 2 REG_DIS_TXREQ_CLR_8822B */
#define BIT_DIS_BT_CCA_8822B BIT(7)
#define BIT_DIS_TXREQ_CLR_HI_8822B BIT(5)
#define BIT_DIS_TXREQ_CLR_MGQ_8822B BIT(4)
#define BIT_DIS_TXREQ_CLR_VO_8822B BIT(3)
#define BIT_DIS_TXREQ_CLR_VI_8822B BIT(2)
#define BIT_DIS_TXREQ_CLR_BE_8822B BIT(1)
#define BIT_DIS_TXREQ_CLR_BK_8822B BIT(0)

/* 2 REG_RD_CTRL_8822B */
#define BIT_EN_CLR_TXREQ_INCCA_8822B BIT(15)
#define BIT_DIS_TX_OVER_BCNQ_8822B BIT(14)
#define BIT_EN_BCNERR_INCCCA_8822B BIT(13)
#define BIT_EDCCA_MSK_CNTDOWN_EN_8822B BIT(11)
#define BIT_DIS_TXOP_CFE_8822B BIT(10)
#define BIT_DIS_LSIG_CFE_8822B BIT(9)
#define BIT_DIS_STBC_CFE_8822B BIT(8)
#define BIT_BKQ_RD_INIT_EN_8822B BIT(7)
#define BIT_BEQ_RD_INIT_EN_8822B BIT(6)
#define BIT_VIQ_RD_INIT_EN_8822B BIT(5)
#define BIT_VOQ_RD_INIT_EN_8822B BIT(4)
#define BIT_BKQ_RD_RESP_EN_8822B BIT(3)
#define BIT_BEQ_RD_RESP_EN_8822B BIT(2)
#define BIT_VIQ_RD_RESP_EN_8822B BIT(1)
#define BIT_VOQ_RD_RESP_EN_8822B BIT(0)

/* 2 REG_MBSSID_CTRL_8822B */
#define BIT_MBID_BCNQ7_EN_8822B BIT(7)
#define BIT_MBID_BCNQ6_EN_8822B BIT(6)
#define BIT_MBID_BCNQ5_EN_8822B BIT(5)
#define BIT_MBID_BCNQ4_EN_8822B BIT(4)
#define BIT_MBID_BCNQ3_EN_8822B BIT(3)
#define BIT_MBID_BCNQ2_EN_8822B BIT(2)
#define BIT_MBID_BCNQ1_EN_8822B BIT(1)
#define BIT_MBID_BCNQ0_EN_8822B BIT(0)

/* 2 REG_P2PPS_CTRL_8822B */
#define BIT_P2P_CTW_ALLSTASLEEP_8822B BIT(7)
#define BIT_P2P_OFF_DISTX_EN_8822B BIT(6)
#define BIT_PWR_MGT_EN_8822B BIT(5)
#define BIT_P2P_NOA1_EN_8822B BIT(2)
#define BIT_P2P_NOA0_EN_8822B BIT(1)

/* 2 REG_PKT_LIFETIME_CTRL_8822B */
#define BIT_EN_P2P_CTWND1_8822B BIT(23)
#define BIT_EN_BKF_CLR_TXREQ_8822B BIT(22)
#define BIT_EN_TSFBIT32_RST_P2P_8822B BIT(21)
#define BIT_EN_BCN_TX_BTCCA_8822B BIT(20)
#define BIT_DIS_PKT_TX_ATIM_8822B BIT(19)
#define BIT_DIS_BCN_DIS_CTN_8822B BIT(18)
#define BIT_EN_NAVEND_RST_TXOP_8822B BIT(17)
#define BIT_EN_FILTER_CCA_8822B BIT(16)

#define BIT_SHIFT_CCA_FILTER_THRS_8822B 8
#define BIT_MASK_CCA_FILTER_THRS_8822B 0xff
#define BIT_CCA_FILTER_THRS_8822B(x)                                           \
	(((x) & BIT_MASK_CCA_FILTER_THRS_8822B)                                \
	 << BIT_SHIFT_CCA_FILTER_THRS_8822B)
#define BITS_CCA_FILTER_THRS_8822B                                             \
	(BIT_MASK_CCA_FILTER_THRS_8822B << BIT_SHIFT_CCA_FILTER_THRS_8822B)
#define BIT_CLEAR_CCA_FILTER_THRS_8822B(x) ((x) & (~BITS_CCA_FILTER_THRS_8822B))
#define BIT_GET_CCA_FILTER_THRS_8822B(x)                                       \
	(((x) >> BIT_SHIFT_CCA_FILTER_THRS_8822B) &                            \
	 BIT_MASK_CCA_FILTER_THRS_8822B)
#define BIT_SET_CCA_FILTER_THRS_8822B(x, v)                                    \
	(BIT_CLEAR_CCA_FILTER_THRS_8822B(x) | BIT_CCA_FILTER_THRS_8822B(v))

#define BIT_SHIFT_EDCCA_THRS_8822B 0
#define BIT_MASK_EDCCA_THRS_8822B 0xff
#define BIT_EDCCA_THRS_8822B(x)                                                \
	(((x) & BIT_MASK_EDCCA_THRS_8822B) << BIT_SHIFT_EDCCA_THRS_8822B)
#define BITS_EDCCA_THRS_8822B                                                  \
	(BIT_MASK_EDCCA_THRS_8822B << BIT_SHIFT_EDCCA_THRS_8822B)
#define BIT_CLEAR_EDCCA_THRS_8822B(x) ((x) & (~BITS_EDCCA_THRS_8822B))
#define BIT_GET_EDCCA_THRS_8822B(x)                                            \
	(((x) >> BIT_SHIFT_EDCCA_THRS_8822B) & BIT_MASK_EDCCA_THRS_8822B)
#define BIT_SET_EDCCA_THRS_8822B(x, v)                                         \
	(BIT_CLEAR_EDCCA_THRS_8822B(x) | BIT_EDCCA_THRS_8822B(v))

/* 2 REG_P2PPS_SPEC_STATE_8822B */
#define BIT_SPEC_POWER_STATE_8822B BIT(7)
#define BIT_SPEC_CTWINDOW_ON_8822B BIT(6)
#define BIT_SPEC_BEACON_AREA_ON_8822B BIT(5)
#define BIT_SPEC_CTWIN_EARLY_DISTX_8822B BIT(4)
#define BIT_SPEC_NOA1_OFF_PERIOD_8822B BIT(3)
#define BIT_SPEC_FORCE_DOZE1_8822B BIT(2)
#define BIT_SPEC_NOA0_OFF_PERIOD_8822B BIT(1)
#define BIT_SPEC_FORCE_DOZE0_8822B BIT(0)

/* 2 REG_TXOP_LIMIT_CTRL_8822B */

#define BIT_SHIFT_TXOP_TBTT_CNT_8822B 24
#define BIT_MASK_TXOP_TBTT_CNT_8822B 0xff
#define BIT_TXOP_TBTT_CNT_8822B(x)                                             \
	(((x) & BIT_MASK_TXOP_TBTT_CNT_8822B) << BIT_SHIFT_TXOP_TBTT_CNT_8822B)
#define BITS_TXOP_TBTT_CNT_8822B                                               \
	(BIT_MASK_TXOP_TBTT_CNT_8822B << BIT_SHIFT_TXOP_TBTT_CNT_8822B)
#define BIT_CLEAR_TXOP_TBTT_CNT_8822B(x) ((x) & (~BITS_TXOP_TBTT_CNT_8822B))
#define BIT_GET_TXOP_TBTT_CNT_8822B(x)                                         \
	(((x) >> BIT_SHIFT_TXOP_TBTT_CNT_8822B) & BIT_MASK_TXOP_TBTT_CNT_8822B)
#define BIT_SET_TXOP_TBTT_CNT_8822B(x, v)                                      \
	(BIT_CLEAR_TXOP_TBTT_CNT_8822B(x) | BIT_TXOP_TBTT_CNT_8822B(v))

#define BIT_SHIFT_TXOP_TBTT_CNT_SEL_8822B 20
#define BIT_MASK_TXOP_TBTT_CNT_SEL_8822B 0xf
#define BIT_TXOP_TBTT_CNT_SEL_8822B(x)                                         \
	(((x) & BIT_MASK_TXOP_TBTT_CNT_SEL_8822B)                              \
	 << BIT_SHIFT_TXOP_TBTT_CNT_SEL_8822B)
#define BITS_TXOP_TBTT_CNT_SEL_8822B                                           \
	(BIT_MASK_TXOP_TBTT_CNT_SEL_8822B << BIT_SHIFT_TXOP_TBTT_CNT_SEL_8822B)
#define BIT_CLEAR_TXOP_TBTT_CNT_SEL_8822B(x)                                   \
	((x) & (~BITS_TXOP_TBTT_CNT_SEL_8822B))
#define BIT_GET_TXOP_TBTT_CNT_SEL_8822B(x)                                     \
	(((x) >> BIT_SHIFT_TXOP_TBTT_CNT_SEL_8822B) &                          \
	 BIT_MASK_TXOP_TBTT_CNT_SEL_8822B)
#define BIT_SET_TXOP_TBTT_CNT_SEL_8822B(x, v)                                  \
	(BIT_CLEAR_TXOP_TBTT_CNT_SEL_8822B(x) | BIT_TXOP_TBTT_CNT_SEL_8822B(v))

#define BIT_SHIFT_TXOP_LMT_EN_8822B 16
#define BIT_MASK_TXOP_LMT_EN_8822B 0xf
#define BIT_TXOP_LMT_EN_8822B(x)                                               \
	(((x) & BIT_MASK_TXOP_LMT_EN_8822B) << BIT_SHIFT_TXOP_LMT_EN_8822B)
#define BITS_TXOP_LMT_EN_8822B                                                 \
	(BIT_MASK_TXOP_LMT_EN_8822B << BIT_SHIFT_TXOP_LMT_EN_8822B)
#define BIT_CLEAR_TXOP_LMT_EN_8822B(x) ((x) & (~BITS_TXOP_LMT_EN_8822B))
#define BIT_GET_TXOP_LMT_EN_8822B(x)                                           \
	(((x) >> BIT_SHIFT_TXOP_LMT_EN_8822B) & BIT_MASK_TXOP_LMT_EN_8822B)
#define BIT_SET_TXOP_LMT_EN_8822B(x, v)                                        \
	(BIT_CLEAR_TXOP_LMT_EN_8822B(x) | BIT_TXOP_LMT_EN_8822B(v))

#define BIT_SHIFT_TXOP_LMT_TX_TIME_8822B 8
#define BIT_MASK_TXOP_LMT_TX_TIME_8822B 0xff
#define BIT_TXOP_LMT_TX_TIME_8822B(x)                                          \
	(((x) & BIT_MASK_TXOP_LMT_TX_TIME_8822B)                               \
	 << BIT_SHIFT_TXOP_LMT_TX_TIME_8822B)
#define BITS_TXOP_LMT_TX_TIME_8822B                                            \
	(BIT_MASK_TXOP_LMT_TX_TIME_8822B << BIT_SHIFT_TXOP_LMT_TX_TIME_8822B)
#define BIT_CLEAR_TXOP_LMT_TX_TIME_8822B(x)                                    \
	((x) & (~BITS_TXOP_LMT_TX_TIME_8822B))
#define BIT_GET_TXOP_LMT_TX_TIME_8822B(x)                                      \
	(((x) >> BIT_SHIFT_TXOP_LMT_TX_TIME_8822B) &                           \
	 BIT_MASK_TXOP_LMT_TX_TIME_8822B)
#define BIT_SET_TXOP_LMT_TX_TIME_8822B(x, v)                                   \
	(BIT_CLEAR_TXOP_LMT_TX_TIME_8822B(x) | BIT_TXOP_LMT_TX_TIME_8822B(v))

#define BIT_TXOP_CNT_TRIGGER_RESET_8822B BIT(7)

#define BIT_SHIFT_TXOP_LMT_PKT_NUM_8822B 0
#define BIT_MASK_TXOP_LMT_PKT_NUM_8822B 0x3f
#define BIT_TXOP_LMT_PKT_NUM_8822B(x)                                          \
	(((x) & BIT_MASK_TXOP_LMT_PKT_NUM_8822B)                               \
	 << BIT_SHIFT_TXOP_LMT_PKT_NUM_8822B)
#define BITS_TXOP_LMT_PKT_NUM_8822B                                            \
	(BIT_MASK_TXOP_LMT_PKT_NUM_8822B << BIT_SHIFT_TXOP_LMT_PKT_NUM_8822B)
#define BIT_CLEAR_TXOP_LMT_PKT_NUM_8822B(x)                                    \
	((x) & (~BITS_TXOP_LMT_PKT_NUM_8822B))
#define BIT_GET_TXOP_LMT_PKT_NUM_8822B(x)                                      \
	(((x) >> BIT_SHIFT_TXOP_LMT_PKT_NUM_8822B) &                           \
	 BIT_MASK_TXOP_LMT_PKT_NUM_8822B)
#define BIT_SET_TXOP_LMT_PKT_NUM_8822B(x, v)                                   \
	(BIT_CLEAR_TXOP_LMT_PKT_NUM_8822B(x) | BIT_TXOP_LMT_PKT_NUM_8822B(v))

/* 2 REG_BAR_TX_CTRL_8822B */

/* 2 REG_P2PON_DIS_TXTIME_8822B */

#define BIT_SHIFT_P2PON_DIS_TXTIME_8822B 0
#define BIT_MASK_P2PON_DIS_TXTIME_8822B 0xff
#define BIT_P2PON_DIS_TXTIME_8822B(x)                                          \
	(((x) & BIT_MASK_P2PON_DIS_TXTIME_8822B)                               \
	 << BIT_SHIFT_P2PON_DIS_TXTIME_8822B)
#define BITS_P2PON_DIS_TXTIME_8822B                                            \
	(BIT_MASK_P2PON_DIS_TXTIME_8822B << BIT_SHIFT_P2PON_DIS_TXTIME_8822B)
#define BIT_CLEAR_P2PON_DIS_TXTIME_8822B(x)                                    \
	((x) & (~BITS_P2PON_DIS_TXTIME_8822B))
#define BIT_GET_P2PON_DIS_TXTIME_8822B(x)                                      \
	(((x) >> BIT_SHIFT_P2PON_DIS_TXTIME_8822B) &                           \
	 BIT_MASK_P2PON_DIS_TXTIME_8822B)
#define BIT_SET_P2PON_DIS_TXTIME_8822B(x, v)                                   \
	(BIT_CLEAR_P2PON_DIS_TXTIME_8822B(x) | BIT_P2PON_DIS_TXTIME_8822B(v))

/* 2 REG_QUEUE_INCOL_THR_8822B */

#define BIT_SHIFT_BK_QUEUE_THR_8822B 24
#define BIT_MASK_BK_QUEUE_THR_8822B 0xff
#define BIT_BK_QUEUE_THR_8822B(x)                                              \
	(((x) & BIT_MASK_BK_QUEUE_THR_8822B) << BIT_SHIFT_BK_QUEUE_THR_8822B)
#define BITS_BK_QUEUE_THR_8822B                                                \
	(BIT_MASK_BK_QUEUE_THR_8822B << BIT_SHIFT_BK_QUEUE_THR_8822B)
#define BIT_CLEAR_BK_QUEUE_THR_8822B(x) ((x) & (~BITS_BK_QUEUE_THR_8822B))
#define BIT_GET_BK_QUEUE_THR_8822B(x)                                          \
	(((x) >> BIT_SHIFT_BK_QUEUE_THR_8822B) & BIT_MASK_BK_QUEUE_THR_8822B)
#define BIT_SET_BK_QUEUE_THR_8822B(x, v)                                       \
	(BIT_CLEAR_BK_QUEUE_THR_8822B(x) | BIT_BK_QUEUE_THR_8822B(v))

#define BIT_SHIFT_BE_QUEUE_THR_8822B 16
#define BIT_MASK_BE_QUEUE_THR_8822B 0xff
#define BIT_BE_QUEUE_THR_8822B(x)                                              \
	(((x) & BIT_MASK_BE_QUEUE_THR_8822B) << BIT_SHIFT_BE_QUEUE_THR_8822B)
#define BITS_BE_QUEUE_THR_8822B                                                \
	(BIT_MASK_BE_QUEUE_THR_8822B << BIT_SHIFT_BE_QUEUE_THR_8822B)
#define BIT_CLEAR_BE_QUEUE_THR_8822B(x) ((x) & (~BITS_BE_QUEUE_THR_8822B))
#define BIT_GET_BE_QUEUE_THR_8822B(x)                                          \
	(((x) >> BIT_SHIFT_BE_QUEUE_THR_8822B) & BIT_MASK_BE_QUEUE_THR_8822B)
#define BIT_SET_BE_QUEUE_THR_8822B(x, v)                                       \
	(BIT_CLEAR_BE_QUEUE_THR_8822B(x) | BIT_BE_QUEUE_THR_8822B(v))

#define BIT_SHIFT_VI_QUEUE_THR_8822B 8
#define BIT_MASK_VI_QUEUE_THR_8822B 0xff
#define BIT_VI_QUEUE_THR_8822B(x)                                              \
	(((x) & BIT_MASK_VI_QUEUE_THR_8822B) << BIT_SHIFT_VI_QUEUE_THR_8822B)
#define BITS_VI_QUEUE_THR_8822B                                                \
	(BIT_MASK_VI_QUEUE_THR_8822B << BIT_SHIFT_VI_QUEUE_THR_8822B)
#define BIT_CLEAR_VI_QUEUE_THR_8822B(x) ((x) & (~BITS_VI_QUEUE_THR_8822B))
#define BIT_GET_VI_QUEUE_THR_8822B(x)                                          \
	(((x) >> BIT_SHIFT_VI_QUEUE_THR_8822B) & BIT_MASK_VI_QUEUE_THR_8822B)
#define BIT_SET_VI_QUEUE_THR_8822B(x, v)                                       \
	(BIT_CLEAR_VI_QUEUE_THR_8822B(x) | BIT_VI_QUEUE_THR_8822B(v))

#define BIT_SHIFT_VO_QUEUE_THR_8822B 0
#define BIT_MASK_VO_QUEUE_THR_8822B 0xff
#define BIT_VO_QUEUE_THR_8822B(x)                                              \
	(((x) & BIT_MASK_VO_QUEUE_THR_8822B) << BIT_SHIFT_VO_QUEUE_THR_8822B)
#define BITS_VO_QUEUE_THR_8822B                                                \
	(BIT_MASK_VO_QUEUE_THR_8822B << BIT_SHIFT_VO_QUEUE_THR_8822B)
#define BIT_CLEAR_VO_QUEUE_THR_8822B(x) ((x) & (~BITS_VO_QUEUE_THR_8822B))
#define BIT_GET_VO_QUEUE_THR_8822B(x)                                          \
	(((x) >> BIT_SHIFT_VO_QUEUE_THR_8822B) & BIT_MASK_VO_QUEUE_THR_8822B)
#define BIT_SET_VO_QUEUE_THR_8822B(x, v)                                       \
	(BIT_CLEAR_VO_QUEUE_THR_8822B(x) | BIT_VO_QUEUE_THR_8822B(v))

/* 2 REG_QUEUE_INCOL_EN_8822B */
#define BIT_QUEUE_INCOL_EN_8822B BIT(16)

#define BIT_SHIFT_BE_TRIGGER_NUM_8822B 12
#define BIT_MASK_BE_TRIGGER_NUM_8822B 0xf
#define BIT_BE_TRIGGER_NUM_8822B(x)                                            \
	(((x) & BIT_MASK_BE_TRIGGER_NUM_8822B)                                 \
	 << BIT_SHIFT_BE_TRIGGER_NUM_8822B)
#define BITS_BE_TRIGGER_NUM_8822B                                              \
	(BIT_MASK_BE_TRIGGER_NUM_8822B << BIT_SHIFT_BE_TRIGGER_NUM_8822B)
#define BIT_CLEAR_BE_TRIGGER_NUM_8822B(x) ((x) & (~BITS_BE_TRIGGER_NUM_8822B))
#define BIT_GET_BE_TRIGGER_NUM_8822B(x)                                        \
	(((x) >> BIT_SHIFT_BE_TRIGGER_NUM_8822B) &                             \
	 BIT_MASK_BE_TRIGGER_NUM_8822B)
#define BIT_SET_BE_TRIGGER_NUM_8822B(x, v)                                     \
	(BIT_CLEAR_BE_TRIGGER_NUM_8822B(x) | BIT_BE_TRIGGER_NUM_8822B(v))

#define BIT_SHIFT_BK_TRIGGER_NUM_8822B 8
#define BIT_MASK_BK_TRIGGER_NUM_8822B 0xf
#define BIT_BK_TRIGGER_NUM_8822B(x)                                            \
	(((x) & BIT_MASK_BK_TRIGGER_NUM_8822B)                                 \
	 << BIT_SHIFT_BK_TRIGGER_NUM_8822B)
#define BITS_BK_TRIGGER_NUM_8822B                                              \
	(BIT_MASK_BK_TRIGGER_NUM_8822B << BIT_SHIFT_BK_TRIGGER_NUM_8822B)
#define BIT_CLEAR_BK_TRIGGER_NUM_8822B(x) ((x) & (~BITS_BK_TRIGGER_NUM_8822B))
#define BIT_GET_BK_TRIGGER_NUM_8822B(x)                                        \
	(((x) >> BIT_SHIFT_BK_TRIGGER_NUM_8822B) &                             \
	 BIT_MASK_BK_TRIGGER_NUM_8822B)
#define BIT_SET_BK_TRIGGER_NUM_8822B(x, v)                                     \
	(BIT_CLEAR_BK_TRIGGER_NUM_8822B(x) | BIT_BK_TRIGGER_NUM_8822B(v))

#define BIT_SHIFT_VI_TRIGGER_NUM_8822B 4
#define BIT_MASK_VI_TRIGGER_NUM_8822B 0xf
#define BIT_VI_TRIGGER_NUM_8822B(x)                                            \
	(((x) & BIT_MASK_VI_TRIGGER_NUM_8822B)                                 \
	 << BIT_SHIFT_VI_TRIGGER_NUM_8822B)
#define BITS_VI_TRIGGER_NUM_8822B                                              \
	(BIT_MASK_VI_TRIGGER_NUM_8822B << BIT_SHIFT_VI_TRIGGER_NUM_8822B)
#define BIT_CLEAR_VI_TRIGGER_NUM_8822B(x) ((x) & (~BITS_VI_TRIGGER_NUM_8822B))
#define BIT_GET_VI_TRIGGER_NUM_8822B(x)                                        \
	(((x) >> BIT_SHIFT_VI_TRIGGER_NUM_8822B) &                             \
	 BIT_MASK_VI_TRIGGER_NUM_8822B)
#define BIT_SET_VI_TRIGGER_NUM_8822B(x, v)                                     \
	(BIT_CLEAR_VI_TRIGGER_NUM_8822B(x) | BIT_VI_TRIGGER_NUM_8822B(v))

#define BIT_SHIFT_VO_TRIGGER_NUM_8822B 0
#define BIT_MASK_VO_TRIGGER_NUM_8822B 0xf
#define BIT_VO_TRIGGER_NUM_8822B(x)                                            \
	(((x) & BIT_MASK_VO_TRIGGER_NUM_8822B)                                 \
	 << BIT_SHIFT_VO_TRIGGER_NUM_8822B)
#define BITS_VO_TRIGGER_NUM_8822B                                              \
	(BIT_MASK_VO_TRIGGER_NUM_8822B << BIT_SHIFT_VO_TRIGGER_NUM_8822B)
#define BIT_CLEAR_VO_TRIGGER_NUM_8822B(x) ((x) & (~BITS_VO_TRIGGER_NUM_8822B))
#define BIT_GET_VO_TRIGGER_NUM_8822B(x)                                        \
	(((x) >> BIT_SHIFT_VO_TRIGGER_NUM_8822B) &                             \
	 BIT_MASK_VO_TRIGGER_NUM_8822B)
#define BIT_SET_VO_TRIGGER_NUM_8822B(x, v)                                     \
	(BIT_CLEAR_VO_TRIGGER_NUM_8822B(x) | BIT_VO_TRIGGER_NUM_8822B(v))

/* 2 REG_TBTT_PROHIBIT_8822B */

#define BIT_SHIFT_TBTT_HOLD_TIME_AP_8822B 8
#define BIT_MASK_TBTT_HOLD_TIME_AP_8822B 0xfff
#define BIT_TBTT_HOLD_TIME_AP_8822B(x)                                         \
	(((x) & BIT_MASK_TBTT_HOLD_TIME_AP_8822B)                              \
	 << BIT_SHIFT_TBTT_HOLD_TIME_AP_8822B)
#define BITS_TBTT_HOLD_TIME_AP_8822B                                           \
	(BIT_MASK_TBTT_HOLD_TIME_AP_8822B << BIT_SHIFT_TBTT_HOLD_TIME_AP_8822B)
#define BIT_CLEAR_TBTT_HOLD_TIME_AP_8822B(x)                                   \
	((x) & (~BITS_TBTT_HOLD_TIME_AP_8822B))
#define BIT_GET_TBTT_HOLD_TIME_AP_8822B(x)                                     \
	(((x) >> BIT_SHIFT_TBTT_HOLD_TIME_AP_8822B) &                          \
	 BIT_MASK_TBTT_HOLD_TIME_AP_8822B)
#define BIT_SET_TBTT_HOLD_TIME_AP_8822B(x, v)                                  \
	(BIT_CLEAR_TBTT_HOLD_TIME_AP_8822B(x) | BIT_TBTT_HOLD_TIME_AP_8822B(v))

#define BIT_SHIFT_TBTT_PROHIBIT_SETUP_8822B 0
#define BIT_MASK_TBTT_PROHIBIT_SETUP_8822B 0xf
#define BIT_TBTT_PROHIBIT_SETUP_8822B(x)                                       \
	(((x) & BIT_MASK_TBTT_PROHIBIT_SETUP_8822B)                            \
	 << BIT_SHIFT_TBTT_PROHIBIT_SETUP_8822B)
#define BITS_TBTT_PROHIBIT_SETUP_8822B                                         \
	(BIT_MASK_TBTT_PROHIBIT_SETUP_8822B                                    \
	 << BIT_SHIFT_TBTT_PROHIBIT_SETUP_8822B)
#define BIT_CLEAR_TBTT_PROHIBIT_SETUP_8822B(x)                                 \
	((x) & (~BITS_TBTT_PROHIBIT_SETUP_8822B))
#define BIT_GET_TBTT_PROHIBIT_SETUP_8822B(x)                                   \
	(((x) >> BIT_SHIFT_TBTT_PROHIBIT_SETUP_8822B) &                        \
	 BIT_MASK_TBTT_PROHIBIT_SETUP_8822B)
#define BIT_SET_TBTT_PROHIBIT_SETUP_8822B(x, v)                                \
	(BIT_CLEAR_TBTT_PROHIBIT_SETUP_8822B(x) |                              \
	 BIT_TBTT_PROHIBIT_SETUP_8822B(v))

/* 2 REG_P2PPS_STATE_8822B */
#define BIT_POWER_STATE_8822B BIT(7)
#define BIT_CTWINDOW_ON_8822B BIT(6)
#define BIT_BEACON_AREA_ON_8822B BIT(5)
#define BIT_CTWIN_EARLY_DISTX_8822B BIT(4)
#define BIT_NOA1_OFF_PERIOD_8822B BIT(3)
#define BIT_FORCE_DOZE1_8822B BIT(2)
#define BIT_NOA0_OFF_PERIOD_8822B BIT(1)
#define BIT_FORCE_DOZE0_8822B BIT(0)

/* 2 REG_RD_NAV_NXT_8822B */

#define BIT_SHIFT_RD_NAV_PROT_NXT_8822B 0
#define BIT_MASK_RD_NAV_PROT_NXT_8822B 0xffff
#define BIT_RD_NAV_PROT_NXT_8822B(x)                                           \
	(((x) & BIT_MASK_RD_NAV_PROT_NXT_8822B)                                \
	 << BIT_SHIFT_RD_NAV_PROT_NXT_8822B)
#define BITS_RD_NAV_PROT_NXT_8822B                                             \
	(BIT_MASK_RD_NAV_PROT_NXT_8822B << BIT_SHIFT_RD_NAV_PROT_NXT_8822B)
#define BIT_CLEAR_RD_NAV_PROT_NXT_8822B(x) ((x) & (~BITS_RD_NAV_PROT_NXT_8822B))
#define BIT_GET_RD_NAV_PROT_NXT_8822B(x)                                       \
	(((x) >> BIT_SHIFT_RD_NAV_PROT_NXT_8822B) &                            \
	 BIT_MASK_RD_NAV_PROT_NXT_8822B)
#define BIT_SET_RD_NAV_PROT_NXT_8822B(x, v)                                    \
	(BIT_CLEAR_RD_NAV_PROT_NXT_8822B(x) | BIT_RD_NAV_PROT_NXT_8822B(v))

/* 2 REG_NAV_PROT_LEN_8822B */

#define BIT_SHIFT_NAV_PROT_LEN_8822B 0
#define BIT_MASK_NAV_PROT_LEN_8822B 0xffff
#define BIT_NAV_PROT_LEN_8822B(x)                                              \
	(((x) & BIT_MASK_NAV_PROT_LEN_8822B) << BIT_SHIFT_NAV_PROT_LEN_8822B)
#define BITS_NAV_PROT_LEN_8822B                                                \
	(BIT_MASK_NAV_PROT_LEN_8822B << BIT_SHIFT_NAV_PROT_LEN_8822B)
#define BIT_CLEAR_NAV_PROT_LEN_8822B(x) ((x) & (~BITS_NAV_PROT_LEN_8822B))
#define BIT_GET_NAV_PROT_LEN_8822B(x)                                          \
	(((x) >> BIT_SHIFT_NAV_PROT_LEN_8822B) & BIT_MASK_NAV_PROT_LEN_8822B)
#define BIT_SET_NAV_PROT_LEN_8822B(x, v)                                       \
	(BIT_CLEAR_NAV_PROT_LEN_8822B(x) | BIT_NAV_PROT_LEN_8822B(v))

/* 2 REG_BCN_CTRL_8822B */
#define BIT_DIS_RX_BSSID_FIT_8822B BIT(6)
#define BIT_P0_EN_TXBCN_RPT_8822B BIT(5)
#define BIT_DIS_TSF_UDT_8822B BIT(4)
#define BIT_EN_BCN_FUNCTION_8822B BIT(3)
#define BIT_P0_EN_RXBCN_RPT_8822B BIT(2)
#define BIT_EN_P2P_CTWINDOW_8822B BIT(1)
#define BIT_EN_P2P_BCNQ_AREA_8822B BIT(0)

/* 2 REG_BCN_CTRL_CLINT0_8822B */
#define BIT_CLI0_DIS_RX_BSSID_FIT_8822B BIT(6)
#define BIT_CLI0_DIS_TSF_UDT_8822B BIT(4)
#define BIT_CLI0_EN_BCN_FUNCTION_8822B BIT(3)
#define BIT_CLI0_EN_RXBCN_RPT_8822B BIT(2)
#define BIT_CLI0_ENP2P_CTWINDOW_8822B BIT(1)
#define BIT_CLI0_ENP2P_BCNQ_AREA_8822B BIT(0)

/* 2 REG_MBID_NUM_8822B */
#define BIT_EN_PRE_DL_BEACON_8822B BIT(3)

#define BIT_SHIFT_MBID_BCN_NUM_8822B 0
#define BIT_MASK_MBID_BCN_NUM_8822B 0x7
#define BIT_MBID_BCN_NUM_8822B(x)                                              \
	(((x) & BIT_MASK_MBID_BCN_NUM_8822B) << BIT_SHIFT_MBID_BCN_NUM_8822B)
#define BITS_MBID_BCN_NUM_8822B                                                \
	(BIT_MASK_MBID_BCN_NUM_8822B << BIT_SHIFT_MBID_BCN_NUM_8822B)
#define BIT_CLEAR_MBID_BCN_NUM_8822B(x) ((x) & (~BITS_MBID_BCN_NUM_8822B))
#define BIT_GET_MBID_BCN_NUM_8822B(x)                                          \
	(((x) >> BIT_SHIFT_MBID_BCN_NUM_8822B) & BIT_MASK_MBID_BCN_NUM_8822B)
#define BIT_SET_MBID_BCN_NUM_8822B(x, v)                                       \
	(BIT_CLEAR_MBID_BCN_NUM_8822B(x) | BIT_MBID_BCN_NUM_8822B(v))

/* 2 REG_DUAL_TSF_RST_8822B */
#define BIT_FREECNT_RST_8822B BIT(5)
#define BIT_TSFTR_CLI3_RST_8822B BIT(4)
#define BIT_TSFTR_CLI2_RST_8822B BIT(3)
#define BIT_TSFTR_CLI1_RST_8822B BIT(2)
#define BIT_TSFTR_CLI0_RST_8822B BIT(1)
#define BIT_TSFTR_RST_8822B BIT(0)

/* 2 REG_MBSSID_BCN_SPACE_8822B */

#define BIT_SHIFT_BCN_TIMER_SEL_FWRD_8822B 28
#define BIT_MASK_BCN_TIMER_SEL_FWRD_8822B 0x7
#define BIT_BCN_TIMER_SEL_FWRD_8822B(x)                                        \
	(((x) & BIT_MASK_BCN_TIMER_SEL_FWRD_8822B)                             \
	 << BIT_SHIFT_BCN_TIMER_SEL_FWRD_8822B)
#define BITS_BCN_TIMER_SEL_FWRD_8822B                                          \
	(BIT_MASK_BCN_TIMER_SEL_FWRD_8822B                                     \
	 << BIT_SHIFT_BCN_TIMER_SEL_FWRD_8822B)
#define BIT_CLEAR_BCN_TIMER_SEL_FWRD_8822B(x)                                  \
	((x) & (~BITS_BCN_TIMER_SEL_FWRD_8822B))
#define BIT_GET_BCN_TIMER_SEL_FWRD_8822B(x)                                    \
	(((x) >> BIT_SHIFT_BCN_TIMER_SEL_FWRD_8822B) &                         \
	 BIT_MASK_BCN_TIMER_SEL_FWRD_8822B)
#define BIT_SET_BCN_TIMER_SEL_FWRD_8822B(x, v)                                 \
	(BIT_CLEAR_BCN_TIMER_SEL_FWRD_8822B(x) |                               \
	 BIT_BCN_TIMER_SEL_FWRD_8822B(v))

#define BIT_SHIFT_BCN_SPACE_CLINT0_8822B 16
#define BIT_MASK_BCN_SPACE_CLINT0_8822B 0xfff
#define BIT_BCN_SPACE_CLINT0_8822B(x)                                          \
	(((x) & BIT_MASK_BCN_SPACE_CLINT0_8822B)                               \
	 << BIT_SHIFT_BCN_SPACE_CLINT0_8822B)
#define BITS_BCN_SPACE_CLINT0_8822B                                            \
	(BIT_MASK_BCN_SPACE_CLINT0_8822B << BIT_SHIFT_BCN_SPACE_CLINT0_8822B)
#define BIT_CLEAR_BCN_SPACE_CLINT0_8822B(x)                                    \
	((x) & (~BITS_BCN_SPACE_CLINT0_8822B))
#define BIT_GET_BCN_SPACE_CLINT0_8822B(x)                                      \
	(((x) >> BIT_SHIFT_BCN_SPACE_CLINT0_8822B) &                           \
	 BIT_MASK_BCN_SPACE_CLINT0_8822B)
#define BIT_SET_BCN_SPACE_CLINT0_8822B(x, v)                                   \
	(BIT_CLEAR_BCN_SPACE_CLINT0_8822B(x) | BIT_BCN_SPACE_CLINT0_8822B(v))

#define BIT_SHIFT_BCN_SPACE0_8822B 0
#define BIT_MASK_BCN_SPACE0_8822B 0xffff
#define BIT_BCN_SPACE0_8822B(x)                                                \
	(((x) & BIT_MASK_BCN_SPACE0_8822B) << BIT_SHIFT_BCN_SPACE0_8822B)
#define BITS_BCN_SPACE0_8822B                                                  \
	(BIT_MASK_BCN_SPACE0_8822B << BIT_SHIFT_BCN_SPACE0_8822B)
#define BIT_CLEAR_BCN_SPACE0_8822B(x) ((x) & (~BITS_BCN_SPACE0_8822B))
#define BIT_GET_BCN_SPACE0_8822B(x)                                            \
	(((x) >> BIT_SHIFT_BCN_SPACE0_8822B) & BIT_MASK_BCN_SPACE0_8822B)
#define BIT_SET_BCN_SPACE0_8822B(x, v)                                         \
	(BIT_CLEAR_BCN_SPACE0_8822B(x) | BIT_BCN_SPACE0_8822B(v))

/* 2 REG_DRVERLYINT_8822B */

#define BIT_SHIFT_DRVERLYITV_8822B 0
#define BIT_MASK_DRVERLYITV_8822B 0xff
#define BIT_DRVERLYITV_8822B(x)                                                \
	(((x) & BIT_MASK_DRVERLYITV_8822B) << BIT_SHIFT_DRVERLYITV_8822B)
#define BITS_DRVERLYITV_8822B                                                  \
	(BIT_MASK_DRVERLYITV_8822B << BIT_SHIFT_DRVERLYITV_8822B)
#define BIT_CLEAR_DRVERLYITV_8822B(x) ((x) & (~BITS_DRVERLYITV_8822B))
#define BIT_GET_DRVERLYITV_8822B(x)                                            \
	(((x) >> BIT_SHIFT_DRVERLYITV_8822B) & BIT_MASK_DRVERLYITV_8822B)
#define BIT_SET_DRVERLYITV_8822B(x, v)                                         \
	(BIT_CLEAR_DRVERLYITV_8822B(x) | BIT_DRVERLYITV_8822B(v))

/* 2 REG_BCNDMATIM_8822B */

#define BIT_SHIFT_BCNDMATIM_8822B 0
#define BIT_MASK_BCNDMATIM_8822B 0xff
#define BIT_BCNDMATIM_8822B(x)                                                 \
	(((x) & BIT_MASK_BCNDMATIM_8822B) << BIT_SHIFT_BCNDMATIM_8822B)
#define BITS_BCNDMATIM_8822B                                                   \
	(BIT_MASK_BCNDMATIM_8822B << BIT_SHIFT_BCNDMATIM_8822B)
#define BIT_CLEAR_BCNDMATIM_8822B(x) ((x) & (~BITS_BCNDMATIM_8822B))
#define BIT_GET_BCNDMATIM_8822B(x)                                             \
	(((x) >> BIT_SHIFT_BCNDMATIM_8822B) & BIT_MASK_BCNDMATIM_8822B)
#define BIT_SET_BCNDMATIM_8822B(x, v)                                          \
	(BIT_CLEAR_BCNDMATIM_8822B(x) | BIT_BCNDMATIM_8822B(v))

/* 2 REG_ATIMWND_8822B */

#define BIT_SHIFT_ATIMWND0_8822B 0
#define BIT_MASK_ATIMWND0_8822B 0xffff
#define BIT_ATIMWND0_8822B(x)                                                  \
	(((x) & BIT_MASK_ATIMWND0_8822B) << BIT_SHIFT_ATIMWND0_8822B)
#define BITS_ATIMWND0_8822B                                                    \
	(BIT_MASK_ATIMWND0_8822B << BIT_SHIFT_ATIMWND0_8822B)
#define BIT_CLEAR_ATIMWND0_8822B(x) ((x) & (~BITS_ATIMWND0_8822B))
#define BIT_GET_ATIMWND0_8822B(x)                                              \
	(((x) >> BIT_SHIFT_ATIMWND0_8822B) & BIT_MASK_ATIMWND0_8822B)
#define BIT_SET_ATIMWND0_8822B(x, v)                                           \
	(BIT_CLEAR_ATIMWND0_8822B(x) | BIT_ATIMWND0_8822B(v))

/* 2 REG_USTIME_TSF_8822B */

#define BIT_SHIFT_USTIME_TSF_V1_8822B 0
#define BIT_MASK_USTIME_TSF_V1_8822B 0xff
#define BIT_USTIME_TSF_V1_8822B(x)                                             \
	(((x) & BIT_MASK_USTIME_TSF_V1_8822B) << BIT_SHIFT_USTIME_TSF_V1_8822B)
#define BITS_USTIME_TSF_V1_8822B                                               \
	(BIT_MASK_USTIME_TSF_V1_8822B << BIT_SHIFT_USTIME_TSF_V1_8822B)
#define BIT_CLEAR_USTIME_TSF_V1_8822B(x) ((x) & (~BITS_USTIME_TSF_V1_8822B))
#define BIT_GET_USTIME_TSF_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_USTIME_TSF_V1_8822B) & BIT_MASK_USTIME_TSF_V1_8822B)
#define BIT_SET_USTIME_TSF_V1_8822B(x, v)                                      \
	(BIT_CLEAR_USTIME_TSF_V1_8822B(x) | BIT_USTIME_TSF_V1_8822B(v))

/* 2 REG_BCN_MAX_ERR_8822B */

#define BIT_SHIFT_BCN_MAX_ERR_8822B 0
#define BIT_MASK_BCN_MAX_ERR_8822B 0xff
#define BIT_BCN_MAX_ERR_8822B(x)                                               \
	(((x) & BIT_MASK_BCN_MAX_ERR_8822B) << BIT_SHIFT_BCN_MAX_ERR_8822B)
#define BITS_BCN_MAX_ERR_8822B                                                 \
	(BIT_MASK_BCN_MAX_ERR_8822B << BIT_SHIFT_BCN_MAX_ERR_8822B)
#define BIT_CLEAR_BCN_MAX_ERR_8822B(x) ((x) & (~BITS_BCN_MAX_ERR_8822B))
#define BIT_GET_BCN_MAX_ERR_8822B(x)                                           \
	(((x) >> BIT_SHIFT_BCN_MAX_ERR_8822B) & BIT_MASK_BCN_MAX_ERR_8822B)
#define BIT_SET_BCN_MAX_ERR_8822B(x, v)                                        \
	(BIT_CLEAR_BCN_MAX_ERR_8822B(x) | BIT_BCN_MAX_ERR_8822B(v))

/* 2 REG_RXTSF_OFFSET_CCK_8822B */

#define BIT_SHIFT_CCK_RXTSF_OFFSET_8822B 0
#define BIT_MASK_CCK_RXTSF_OFFSET_8822B 0xff
#define BIT_CCK_RXTSF_OFFSET_8822B(x)                                          \
	(((x) & BIT_MASK_CCK_RXTSF_OFFSET_8822B)                               \
	 << BIT_SHIFT_CCK_RXTSF_OFFSET_8822B)
#define BITS_CCK_RXTSF_OFFSET_8822B                                            \
	(BIT_MASK_CCK_RXTSF_OFFSET_8822B << BIT_SHIFT_CCK_RXTSF_OFFSET_8822B)
#define BIT_CLEAR_CCK_RXTSF_OFFSET_8822B(x)                                    \
	((x) & (~BITS_CCK_RXTSF_OFFSET_8822B))
#define BIT_GET_CCK_RXTSF_OFFSET_8822B(x)                                      \
	(((x) >> BIT_SHIFT_CCK_RXTSF_OFFSET_8822B) &                           \
	 BIT_MASK_CCK_RXTSF_OFFSET_8822B)
#define BIT_SET_CCK_RXTSF_OFFSET_8822B(x, v)                                   \
	(BIT_CLEAR_CCK_RXTSF_OFFSET_8822B(x) | BIT_CCK_RXTSF_OFFSET_8822B(v))

/* 2 REG_RXTSF_OFFSET_OFDM_8822B */

#define BIT_SHIFT_OFDM_RXTSF_OFFSET_8822B 0
#define BIT_MASK_OFDM_RXTSF_OFFSET_8822B 0xff
#define BIT_OFDM_RXTSF_OFFSET_8822B(x)                                         \
	(((x) & BIT_MASK_OFDM_RXTSF_OFFSET_8822B)                              \
	 << BIT_SHIFT_OFDM_RXTSF_OFFSET_8822B)
#define BITS_OFDM_RXTSF_OFFSET_8822B                                           \
	(BIT_MASK_OFDM_RXTSF_OFFSET_8822B << BIT_SHIFT_OFDM_RXTSF_OFFSET_8822B)
#define BIT_CLEAR_OFDM_RXTSF_OFFSET_8822B(x)                                   \
	((x) & (~BITS_OFDM_RXTSF_OFFSET_8822B))
#define BIT_GET_OFDM_RXTSF_OFFSET_8822B(x)                                     \
	(((x) >> BIT_SHIFT_OFDM_RXTSF_OFFSET_8822B) &                          \
	 BIT_MASK_OFDM_RXTSF_OFFSET_8822B)
#define BIT_SET_OFDM_RXTSF_OFFSET_8822B(x, v)                                  \
	(BIT_CLEAR_OFDM_RXTSF_OFFSET_8822B(x) | BIT_OFDM_RXTSF_OFFSET_8822B(v))

/* 2 REG_TSFTR_8822B */

#define BIT_SHIFT_TSF_TIMER_8822B 0
#define BIT_MASK_TSF_TIMER_8822B 0xffffffffffffffffL
#define BIT_TSF_TIMER_8822B(x)                                                 \
	(((x) & BIT_MASK_TSF_TIMER_8822B) << BIT_SHIFT_TSF_TIMER_8822B)
#define BITS_TSF_TIMER_8822B                                                   \
	(BIT_MASK_TSF_TIMER_8822B << BIT_SHIFT_TSF_TIMER_8822B)
#define BIT_CLEAR_TSF_TIMER_8822B(x) ((x) & (~BITS_TSF_TIMER_8822B))
#define BIT_GET_TSF_TIMER_8822B(x)                                             \
	(((x) >> BIT_SHIFT_TSF_TIMER_8822B) & BIT_MASK_TSF_TIMER_8822B)
#define BIT_SET_TSF_TIMER_8822B(x, v)                                          \
	(BIT_CLEAR_TSF_TIMER_8822B(x) | BIT_TSF_TIMER_8822B(v))

/* 2 REG_FREERUN_CNT_8822B */

#define BIT_SHIFT_FREERUN_CNT_8822B 0
#define BIT_MASK_FREERUN_CNT_8822B 0xffffffffffffffffL
#define BIT_FREERUN_CNT_8822B(x)                                               \
	(((x) & BIT_MASK_FREERUN_CNT_8822B) << BIT_SHIFT_FREERUN_CNT_8822B)
#define BITS_FREERUN_CNT_8822B                                                 \
	(BIT_MASK_FREERUN_CNT_8822B << BIT_SHIFT_FREERUN_CNT_8822B)
#define BIT_CLEAR_FREERUN_CNT_8822B(x) ((x) & (~BITS_FREERUN_CNT_8822B))
#define BIT_GET_FREERUN_CNT_8822B(x)                                           \
	(((x) >> BIT_SHIFT_FREERUN_CNT_8822B) & BIT_MASK_FREERUN_CNT_8822B)
#define BIT_SET_FREERUN_CNT_8822B(x, v)                                        \
	(BIT_CLEAR_FREERUN_CNT_8822B(x) | BIT_FREERUN_CNT_8822B(v))

/* 2 REG_ATIMWND1_V1_8822B */

#define BIT_SHIFT_ATIMWND1_V1_8822B 0
#define BIT_MASK_ATIMWND1_V1_8822B 0xff
#define BIT_ATIMWND1_V1_8822B(x)                                               \
	(((x) & BIT_MASK_ATIMWND1_V1_8822B) << BIT_SHIFT_ATIMWND1_V1_8822B)
#define BITS_ATIMWND1_V1_8822B                                                 \
	(BIT_MASK_ATIMWND1_V1_8822B << BIT_SHIFT_ATIMWND1_V1_8822B)
#define BIT_CLEAR_ATIMWND1_V1_8822B(x) ((x) & (~BITS_ATIMWND1_V1_8822B))
#define BIT_GET_ATIMWND1_V1_8822B(x)                                           \
	(((x) >> BIT_SHIFT_ATIMWND1_V1_8822B) & BIT_MASK_ATIMWND1_V1_8822B)
#define BIT_SET_ATIMWND1_V1_8822B(x, v)                                        \
	(BIT_CLEAR_ATIMWND1_V1_8822B(x) | BIT_ATIMWND1_V1_8822B(v))

/* 2 REG_TBTT_PROHIBIT_INFRA_8822B */

#define BIT_SHIFT_TBTT_PROHIBIT_INFRA_8822B 0
#define BIT_MASK_TBTT_PROHIBIT_INFRA_8822B 0xff
#define BIT_TBTT_PROHIBIT_INFRA_8822B(x)                                       \
	(((x) & BIT_MASK_TBTT_PROHIBIT_INFRA_8822B)                            \
	 << BIT_SHIFT_TBTT_PROHIBIT_INFRA_8822B)
#define BITS_TBTT_PROHIBIT_INFRA_8822B                                         \
	(BIT_MASK_TBTT_PROHIBIT_INFRA_8822B                                    \
	 << BIT_SHIFT_TBTT_PROHIBIT_INFRA_8822B)
#define BIT_CLEAR_TBTT_PROHIBIT_INFRA_8822B(x)                                 \
	((x) & (~BITS_TBTT_PROHIBIT_INFRA_8822B))
#define BIT_GET_TBTT_PROHIBIT_INFRA_8822B(x)                                   \
	(((x) >> BIT_SHIFT_TBTT_PROHIBIT_INFRA_8822B) &                        \
	 BIT_MASK_TBTT_PROHIBIT_INFRA_8822B)
#define BIT_SET_TBTT_PROHIBIT_INFRA_8822B(x, v)                                \
	(BIT_CLEAR_TBTT_PROHIBIT_INFRA_8822B(x) |                              \
	 BIT_TBTT_PROHIBIT_INFRA_8822B(v))

/* 2 REG_CTWND_8822B */

#define BIT_SHIFT_CTWND_8822B 0
#define BIT_MASK_CTWND_8822B 0xff
#define BIT_CTWND_8822B(x)                                                     \
	(((x) & BIT_MASK_CTWND_8822B) << BIT_SHIFT_CTWND_8822B)
#define BITS_CTWND_8822B (BIT_MASK_CTWND_8822B << BIT_SHIFT_CTWND_8822B)
#define BIT_CLEAR_CTWND_8822B(x) ((x) & (~BITS_CTWND_8822B))
#define BIT_GET_CTWND_8822B(x)                                                 \
	(((x) >> BIT_SHIFT_CTWND_8822B) & BIT_MASK_CTWND_8822B)
#define BIT_SET_CTWND_8822B(x, v)                                              \
	(BIT_CLEAR_CTWND_8822B(x) | BIT_CTWND_8822B(v))

/* 2 REG_BCNIVLCUNT_8822B */

#define BIT_SHIFT_BCNIVLCUNT_8822B 0
#define BIT_MASK_BCNIVLCUNT_8822B 0x7f
#define BIT_BCNIVLCUNT_8822B(x)                                                \
	(((x) & BIT_MASK_BCNIVLCUNT_8822B) << BIT_SHIFT_BCNIVLCUNT_8822B)
#define BITS_BCNIVLCUNT_8822B                                                  \
	(BIT_MASK_BCNIVLCUNT_8822B << BIT_SHIFT_BCNIVLCUNT_8822B)
#define BIT_CLEAR_BCNIVLCUNT_8822B(x) ((x) & (~BITS_BCNIVLCUNT_8822B))
#define BIT_GET_BCNIVLCUNT_8822B(x)                                            \
	(((x) >> BIT_SHIFT_BCNIVLCUNT_8822B) & BIT_MASK_BCNIVLCUNT_8822B)
#define BIT_SET_BCNIVLCUNT_8822B(x, v)                                         \
	(BIT_CLEAR_BCNIVLCUNT_8822B(x) | BIT_BCNIVLCUNT_8822B(v))

/* 2 REG_BCNDROPCTRL_8822B */
#define BIT_BEACON_DROP_EN_8822B BIT(7)

#define BIT_SHIFT_BEACON_DROP_IVL_8822B 0
#define BIT_MASK_BEACON_DROP_IVL_8822B 0x7f
#define BIT_BEACON_DROP_IVL_8822B(x)                                           \
	(((x) & BIT_MASK_BEACON_DROP_IVL_8822B)                                \
	 << BIT_SHIFT_BEACON_DROP_IVL_8822B)
#define BITS_BEACON_DROP_IVL_8822B                                             \
	(BIT_MASK_BEACON_DROP_IVL_8822B << BIT_SHIFT_BEACON_DROP_IVL_8822B)
#define BIT_CLEAR_BEACON_DROP_IVL_8822B(x) ((x) & (~BITS_BEACON_DROP_IVL_8822B))
#define BIT_GET_BEACON_DROP_IVL_8822B(x)                                       \
	(((x) >> BIT_SHIFT_BEACON_DROP_IVL_8822B) &                            \
	 BIT_MASK_BEACON_DROP_IVL_8822B)
#define BIT_SET_BEACON_DROP_IVL_8822B(x, v)                                    \
	(BIT_CLEAR_BEACON_DROP_IVL_8822B(x) | BIT_BEACON_DROP_IVL_8822B(v))

/* 2 REG_HGQ_TIMEOUT_PERIOD_8822B */

#define BIT_SHIFT_HGQ_TIMEOUT_PERIOD_8822B 0
#define BIT_MASK_HGQ_TIMEOUT_PERIOD_8822B 0xff
#define BIT_HGQ_TIMEOUT_PERIOD_8822B(x)                                        \
	(((x) & BIT_MASK_HGQ_TIMEOUT_PERIOD_8822B)                             \
	 << BIT_SHIFT_HGQ_TIMEOUT_PERIOD_8822B)
#define BITS_HGQ_TIMEOUT_PERIOD_8822B                                          \
	(BIT_MASK_HGQ_TIMEOUT_PERIOD_8822B                                     \
	 << BIT_SHIFT_HGQ_TIMEOUT_PERIOD_8822B)
#define BIT_CLEAR_HGQ_TIMEOUT_PERIOD_8822B(x)                                  \
	((x) & (~BITS_HGQ_TIMEOUT_PERIOD_8822B))
#define BIT_GET_HGQ_TIMEOUT_PERIOD_8822B(x)                                    \
	(((x) >> BIT_SHIFT_HGQ_TIMEOUT_PERIOD_8822B) &                         \
	 BIT_MASK_HGQ_TIMEOUT_PERIOD_8822B)
#define BIT_SET_HGQ_TIMEOUT_PERIOD_8822B(x, v)                                 \
	(BIT_CLEAR_HGQ_TIMEOUT_PERIOD_8822B(x) |                               \
	 BIT_HGQ_TIMEOUT_PERIOD_8822B(v))

/* 2 REG_TXCMD_TIMEOUT_PERIOD_8822B */

#define BIT_SHIFT_TXCMD_TIMEOUT_PERIOD_8822B 0
#define BIT_MASK_TXCMD_TIMEOUT_PERIOD_8822B 0xff
#define BIT_TXCMD_TIMEOUT_PERIOD_8822B(x)                                      \
	(((x) & BIT_MASK_TXCMD_TIMEOUT_PERIOD_8822B)                           \
	 << BIT_SHIFT_TXCMD_TIMEOUT_PERIOD_8822B)
#define BITS_TXCMD_TIMEOUT_PERIOD_8822B                                        \
	(BIT_MASK_TXCMD_TIMEOUT_PERIOD_8822B                                   \
	 << BIT_SHIFT_TXCMD_TIMEOUT_PERIOD_8822B)
#define BIT_CLEAR_TXCMD_TIMEOUT_PERIOD_8822B(x)                                \
	((x) & (~BITS_TXCMD_TIMEOUT_PERIOD_8822B))
#define BIT_GET_TXCMD_TIMEOUT_PERIOD_8822B(x)                                  \
	(((x) >> BIT_SHIFT_TXCMD_TIMEOUT_PERIOD_8822B) &                       \
	 BIT_MASK_TXCMD_TIMEOUT_PERIOD_8822B)
#define BIT_SET_TXCMD_TIMEOUT_PERIOD_8822B(x, v)                               \
	(BIT_CLEAR_TXCMD_TIMEOUT_PERIOD_8822B(x) |                             \
	 BIT_TXCMD_TIMEOUT_PERIOD_8822B(v))

/* 2 REG_MISC_CTRL_8822B */
#define BIT_AUTO_SYNC_BY_TBTT_8822B BIT(6)
#define BIT_DIS_TRX_CAL_BCN_8822B BIT(5)
#define BIT_DIS_TX_CAL_TBTT_8822B BIT(4)
#define BIT_EN_FREECNT_8822B BIT(3)
#define BIT_BCN_AGGRESSION_8822B BIT(2)

#define BIT_SHIFT_DIS_SECONDARY_CCA_8822B 0
#define BIT_MASK_DIS_SECONDARY_CCA_8822B 0x3
#define BIT_DIS_SECONDARY_CCA_8822B(x)                                         \
	(((x) & BIT_MASK_DIS_SECONDARY_CCA_8822B)                              \
	 << BIT_SHIFT_DIS_SECONDARY_CCA_8822B)
#define BITS_DIS_SECONDARY_CCA_8822B                                           \
	(BIT_MASK_DIS_SECONDARY_CCA_8822B << BIT_SHIFT_DIS_SECONDARY_CCA_8822B)
#define BIT_CLEAR_DIS_SECONDARY_CCA_8822B(x)                                   \
	((x) & (~BITS_DIS_SECONDARY_CCA_8822B))
#define BIT_GET_DIS_SECONDARY_CCA_8822B(x)                                     \
	(((x) >> BIT_SHIFT_DIS_SECONDARY_CCA_8822B) &                          \
	 BIT_MASK_DIS_SECONDARY_CCA_8822B)
#define BIT_SET_DIS_SECONDARY_CCA_8822B(x, v)                                  \
	(BIT_CLEAR_DIS_SECONDARY_CCA_8822B(x) | BIT_DIS_SECONDARY_CCA_8822B(v))

/* 2 REG_BCN_CTRL_CLINT1_8822B */
#define BIT_CLI1_DIS_RX_BSSID_FIT_8822B BIT(6)
#define BIT_CLI1_DIS_TSF_UDT_8822B BIT(4)
#define BIT_CLI1_EN_BCN_FUNCTION_8822B BIT(3)
#define BIT_CLI1_EN_RXBCN_RPT_8822B BIT(2)
#define BIT_CLI1_ENP2P_CTWINDOW_8822B BIT(1)
#define BIT_CLI1_ENP2P_BCNQ_AREA_8822B BIT(0)

/* 2 REG_BCN_CTRL_CLINT2_8822B */
#define BIT_CLI2_DIS_RX_BSSID_FIT_8822B BIT(6)
#define BIT_CLI2_DIS_TSF_UDT_8822B BIT(4)
#define BIT_CLI2_EN_BCN_FUNCTION_8822B BIT(3)
#define BIT_CLI2_EN_RXBCN_RPT_8822B BIT(2)
#define BIT_CLI2_ENP2P_CTWINDOW_8822B BIT(1)
#define BIT_CLI2_ENP2P_BCNQ_AREA_8822B BIT(0)

/* 2 REG_BCN_CTRL_CLINT3_8822B */
#define BIT_CLI3_DIS_RX_BSSID_FIT_8822B BIT(6)
#define BIT_CLI3_DIS_TSF_UDT_8822B BIT(4)
#define BIT_CLI3_EN_BCN_FUNCTION_8822B BIT(3)
#define BIT_CLI3_EN_RXBCN_RPT_8822B BIT(2)
#define BIT_CLI3_ENP2P_CTWINDOW_8822B BIT(1)
#define BIT_CLI3_ENP2P_BCNQ_AREA_8822B BIT(0)

/* 2 REG_EXTEND_CTRL_8822B */
#define BIT_EN_TSFBIT32_RST_P2P2_8822B BIT(5)
#define BIT_EN_TSFBIT32_RST_P2P1_8822B BIT(4)

#define BIT_SHIFT_PORT_SEL_8822B 0
#define BIT_MASK_PORT_SEL_8822B 0x7
#define BIT_PORT_SEL_8822B(x)                                                  \
	(((x) & BIT_MASK_PORT_SEL_8822B) << BIT_SHIFT_PORT_SEL_8822B)
#define BITS_PORT_SEL_8822B                                                    \
	(BIT_MASK_PORT_SEL_8822B << BIT_SHIFT_PORT_SEL_8822B)
#define BIT_CLEAR_PORT_SEL_8822B(x) ((x) & (~BITS_PORT_SEL_8822B))
#define BIT_GET_PORT_SEL_8822B(x)                                              \
	(((x) >> BIT_SHIFT_PORT_SEL_8822B) & BIT_MASK_PORT_SEL_8822B)
#define BIT_SET_PORT_SEL_8822B(x, v)                                           \
	(BIT_CLEAR_PORT_SEL_8822B(x) | BIT_PORT_SEL_8822B(v))

/* 2 REG_P2PPS1_SPEC_STATE_8822B */
#define BIT_P2P1_SPEC_POWER_STATE_8822B BIT(7)
#define BIT_P2P1_SPEC_CTWINDOW_ON_8822B BIT(6)
#define BIT_P2P1_SPEC_BCN_AREA_ON_8822B BIT(5)
#define BIT_P2P1_SPEC_CTWIN_EARLY_DISTX_8822B BIT(4)
#define BIT_P2P1_SPEC_NOA1_OFF_PERIOD_8822B BIT(3)
#define BIT_P2P1_SPEC_FORCE_DOZE1_8822B BIT(2)
#define BIT_P2P1_SPEC_NOA0_OFF_PERIOD_8822B BIT(1)
#define BIT_P2P1_SPEC_FORCE_DOZE0_8822B BIT(0)

/* 2 REG_P2PPS1_STATE_8822B */
#define BIT_P2P1_POWER_STATE_8822B BIT(7)
#define BIT_P2P1_CTWINDOW_ON_8822B BIT(6)
#define BIT_P2P1_BEACON_AREA_ON_8822B BIT(5)
#define BIT_P2P1_CTWIN_EARLY_DISTX_8822B BIT(4)
#define BIT_P2P1_NOA1_OFF_PERIOD_8822B BIT(3)
#define BIT_P2P1_FORCE_DOZE1_8822B BIT(2)
#define BIT_P2P1_NOA0_OFF_PERIOD_8822B BIT(1)
#define BIT_P2P1_FORCE_DOZE0_8822B BIT(0)

/* 2 REG_P2PPS2_SPEC_STATE_8822B */
#define BIT_P2P2_SPEC_POWER_STATE_8822B BIT(7)
#define BIT_P2P2_SPEC_CTWINDOW_ON_8822B BIT(6)
#define BIT_P2P2_SPEC_BCN_AREA_ON_8822B BIT(5)
#define BIT_P2P2_SPEC_CTWIN_EARLY_DISTX_8822B BIT(4)
#define BIT_P2P2_SPEC_NOA1_OFF_PERIOD_8822B BIT(3)
#define BIT_P2P2_SPEC_FORCE_DOZE1_8822B BIT(2)
#define BIT_P2P2_SPEC_NOA0_OFF_PERIOD_8822B BIT(1)
#define BIT_P2P2_SPEC_FORCE_DOZE0_8822B BIT(0)

/* 2 REG_P2PPS2_STATE_8822B */
#define BIT_P2P2_POWER_STATE_8822B BIT(7)
#define BIT_P2P2_CTWINDOW_ON_8822B BIT(6)
#define BIT_P2P2_BEACON_AREA_ON_8822B BIT(5)
#define BIT_P2P2_CTWIN_EARLY_DISTX_8822B BIT(4)
#define BIT_P2P2_NOA1_OFF_PERIOD_8822B BIT(3)
#define BIT_P2P2_FORCE_DOZE1_8822B BIT(2)
#define BIT_P2P2_NOA0_OFF_PERIOD_8822B BIT(1)
#define BIT_P2P2_FORCE_DOZE0_8822B BIT(0)

/* 2 REG_PS_TIMER0_8822B */

#define BIT_SHIFT_PSTIMER0_INT_8822B 5
#define BIT_MASK_PSTIMER0_INT_8822B 0x7ffffff
#define BIT_PSTIMER0_INT_8822B(x)                                              \
	(((x) & BIT_MASK_PSTIMER0_INT_8822B) << BIT_SHIFT_PSTIMER0_INT_8822B)
#define BITS_PSTIMER0_INT_8822B                                                \
	(BIT_MASK_PSTIMER0_INT_8822B << BIT_SHIFT_PSTIMER0_INT_8822B)
#define BIT_CLEAR_PSTIMER0_INT_8822B(x) ((x) & (~BITS_PSTIMER0_INT_8822B))
#define BIT_GET_PSTIMER0_INT_8822B(x)                                          \
	(((x) >> BIT_SHIFT_PSTIMER0_INT_8822B) & BIT_MASK_PSTIMER0_INT_8822B)
#define BIT_SET_PSTIMER0_INT_8822B(x, v)                                       \
	(BIT_CLEAR_PSTIMER0_INT_8822B(x) | BIT_PSTIMER0_INT_8822B(v))

/* 2 REG_PS_TIMER1_8822B */

#define BIT_SHIFT_PSTIMER1_INT_8822B 5
#define BIT_MASK_PSTIMER1_INT_8822B 0x7ffffff
#define BIT_PSTIMER1_INT_8822B(x)                                              \
	(((x) & BIT_MASK_PSTIMER1_INT_8822B) << BIT_SHIFT_PSTIMER1_INT_8822B)
#define BITS_PSTIMER1_INT_8822B                                                \
	(BIT_MASK_PSTIMER1_INT_8822B << BIT_SHIFT_PSTIMER1_INT_8822B)
#define BIT_CLEAR_PSTIMER1_INT_8822B(x) ((x) & (~BITS_PSTIMER1_INT_8822B))
#define BIT_GET_PSTIMER1_INT_8822B(x)                                          \
	(((x) >> BIT_SHIFT_PSTIMER1_INT_8822B) & BIT_MASK_PSTIMER1_INT_8822B)
#define BIT_SET_PSTIMER1_INT_8822B(x, v)                                       \
	(BIT_CLEAR_PSTIMER1_INT_8822B(x) | BIT_PSTIMER1_INT_8822B(v))

/* 2 REG_PS_TIMER2_8822B */

#define BIT_SHIFT_PSTIMER2_INT_8822B 5
#define BIT_MASK_PSTIMER2_INT_8822B 0x7ffffff
#define BIT_PSTIMER2_INT_8822B(x)                                              \
	(((x) & BIT_MASK_PSTIMER2_INT_8822B) << BIT_SHIFT_PSTIMER2_INT_8822B)
#define BITS_PSTIMER2_INT_8822B                                                \
	(BIT_MASK_PSTIMER2_INT_8822B << BIT_SHIFT_PSTIMER2_INT_8822B)
#define BIT_CLEAR_PSTIMER2_INT_8822B(x) ((x) & (~BITS_PSTIMER2_INT_8822B))
#define BIT_GET_PSTIMER2_INT_8822B(x)                                          \
	(((x) >> BIT_SHIFT_PSTIMER2_INT_8822B) & BIT_MASK_PSTIMER2_INT_8822B)
#define BIT_SET_PSTIMER2_INT_8822B(x, v)                                       \
	(BIT_CLEAR_PSTIMER2_INT_8822B(x) | BIT_PSTIMER2_INT_8822B(v))

/* 2 REG_TBTT_CTN_AREA_8822B */

#define BIT_SHIFT_TBTT_CTN_AREA_8822B 0
#define BIT_MASK_TBTT_CTN_AREA_8822B 0xff
#define BIT_TBTT_CTN_AREA_8822B(x)                                             \
	(((x) & BIT_MASK_TBTT_CTN_AREA_8822B) << BIT_SHIFT_TBTT_CTN_AREA_8822B)
#define BITS_TBTT_CTN_AREA_8822B                                               \
	(BIT_MASK_TBTT_CTN_AREA_8822B << BIT_SHIFT_TBTT_CTN_AREA_8822B)
#define BIT_CLEAR_TBTT_CTN_AREA_8822B(x) ((x) & (~BITS_TBTT_CTN_AREA_8822B))
#define BIT_GET_TBTT_CTN_AREA_8822B(x)                                         \
	(((x) >> BIT_SHIFT_TBTT_CTN_AREA_8822B) & BIT_MASK_TBTT_CTN_AREA_8822B)
#define BIT_SET_TBTT_CTN_AREA_8822B(x, v)                                      \
	(BIT_CLEAR_TBTT_CTN_AREA_8822B(x) | BIT_TBTT_CTN_AREA_8822B(v))

/* 2 REG_FORCE_BCN_IFS_8822B */

#define BIT_SHIFT_FORCE_BCN_IFS_8822B 0
#define BIT_MASK_FORCE_BCN_IFS_8822B 0xff
#define BIT_FORCE_BCN_IFS_8822B(x)                                             \
	(((x) & BIT_MASK_FORCE_BCN_IFS_8822B) << BIT_SHIFT_FORCE_BCN_IFS_8822B)
#define BITS_FORCE_BCN_IFS_8822B                                               \
	(BIT_MASK_FORCE_BCN_IFS_8822B << BIT_SHIFT_FORCE_BCN_IFS_8822B)
#define BIT_CLEAR_FORCE_BCN_IFS_8822B(x) ((x) & (~BITS_FORCE_BCN_IFS_8822B))
#define BIT_GET_FORCE_BCN_IFS_8822B(x)                                         \
	(((x) >> BIT_SHIFT_FORCE_BCN_IFS_8822B) & BIT_MASK_FORCE_BCN_IFS_8822B)
#define BIT_SET_FORCE_BCN_IFS_8822B(x, v)                                      \
	(BIT_CLEAR_FORCE_BCN_IFS_8822B(x) | BIT_FORCE_BCN_IFS_8822B(v))

/* 2 REG_TXOP_MIN_8822B */

#define BIT_SHIFT_TXOP_MIN_8822B 0
#define BIT_MASK_TXOP_MIN_8822B 0x3fff
#define BIT_TXOP_MIN_8822B(x)                                                  \
	(((x) & BIT_MASK_TXOP_MIN_8822B) << BIT_SHIFT_TXOP_MIN_8822B)
#define BITS_TXOP_MIN_8822B                                                    \
	(BIT_MASK_TXOP_MIN_8822B << BIT_SHIFT_TXOP_MIN_8822B)
#define BIT_CLEAR_TXOP_MIN_8822B(x) ((x) & (~BITS_TXOP_MIN_8822B))
#define BIT_GET_TXOP_MIN_8822B(x)                                              \
	(((x) >> BIT_SHIFT_TXOP_MIN_8822B) & BIT_MASK_TXOP_MIN_8822B)
#define BIT_SET_TXOP_MIN_8822B(x, v)                                           \
	(BIT_CLEAR_TXOP_MIN_8822B(x) | BIT_TXOP_MIN_8822B(v))

/* 2 REG_PRE_BKF_TIME_8822B */

#define BIT_SHIFT_PRE_BKF_TIME_8822B 0
#define BIT_MASK_PRE_BKF_TIME_8822B 0xff
#define BIT_PRE_BKF_TIME_8822B(x)                                              \
	(((x) & BIT_MASK_PRE_BKF_TIME_8822B) << BIT_SHIFT_PRE_BKF_TIME_8822B)
#define BITS_PRE_BKF_TIME_8822B                                                \
	(BIT_MASK_PRE_BKF_TIME_8822B << BIT_SHIFT_PRE_BKF_TIME_8822B)
#define BIT_CLEAR_PRE_BKF_TIME_8822B(x) ((x) & (~BITS_PRE_BKF_TIME_8822B))
#define BIT_GET_PRE_BKF_TIME_8822B(x)                                          \
	(((x) >> BIT_SHIFT_PRE_BKF_TIME_8822B) & BIT_MASK_PRE_BKF_TIME_8822B)
#define BIT_SET_PRE_BKF_TIME_8822B(x, v)                                       \
	(BIT_CLEAR_PRE_BKF_TIME_8822B(x) | BIT_PRE_BKF_TIME_8822B(v))

/* 2 REG_CROSS_TXOP_CTRL_8822B */
#define BIT_DTIM_BYPASS_8822B BIT(2)
#define BIT_RTS_NAV_TXOP_8822B BIT(1)
#define BIT_NOT_CROSS_TXOP_8822B BIT(0)

/* 2 REG_ATIMWND2_8822B */

#define BIT_SHIFT_ATIMWND2_8822B 0
#define BIT_MASK_ATIMWND2_8822B 0xff
#define BIT_ATIMWND2_8822B(x)                                                  \
	(((x) & BIT_MASK_ATIMWND2_8822B) << BIT_SHIFT_ATIMWND2_8822B)
#define BITS_ATIMWND2_8822B                                                    \
	(BIT_MASK_ATIMWND2_8822B << BIT_SHIFT_ATIMWND2_8822B)
#define BIT_CLEAR_ATIMWND2_8822B(x) ((x) & (~BITS_ATIMWND2_8822B))
#define BIT_GET_ATIMWND2_8822B(x)                                              \
	(((x) >> BIT_SHIFT_ATIMWND2_8822B) & BIT_MASK_ATIMWND2_8822B)
#define BIT_SET_ATIMWND2_8822B(x, v)                                           \
	(BIT_CLEAR_ATIMWND2_8822B(x) | BIT_ATIMWND2_8822B(v))

/* 2 REG_ATIMWND3_8822B */

#define BIT_SHIFT_ATIMWND3_8822B 0
#define BIT_MASK_ATIMWND3_8822B 0xff
#define BIT_ATIMWND3_8822B(x)                                                  \
	(((x) & BIT_MASK_ATIMWND3_8822B) << BIT_SHIFT_ATIMWND3_8822B)
#define BITS_ATIMWND3_8822B                                                    \
	(BIT_MASK_ATIMWND3_8822B << BIT_SHIFT_ATIMWND3_8822B)
#define BIT_CLEAR_ATIMWND3_8822B(x) ((x) & (~BITS_ATIMWND3_8822B))
#define BIT_GET_ATIMWND3_8822B(x)                                              \
	(((x) >> BIT_SHIFT_ATIMWND3_8822B) & BIT_MASK_ATIMWND3_8822B)
#define BIT_SET_ATIMWND3_8822B(x, v)                                           \
	(BIT_CLEAR_ATIMWND3_8822B(x) | BIT_ATIMWND3_8822B(v))

/* 2 REG_ATIMWND4_8822B */

#define BIT_SHIFT_ATIMWND4_8822B 0
#define BIT_MASK_ATIMWND4_8822B 0xff
#define BIT_ATIMWND4_8822B(x)                                                  \
	(((x) & BIT_MASK_ATIMWND4_8822B) << BIT_SHIFT_ATIMWND4_8822B)
#define BITS_ATIMWND4_8822B                                                    \
	(BIT_MASK_ATIMWND4_8822B << BIT_SHIFT_ATIMWND4_8822B)
#define BIT_CLEAR_ATIMWND4_8822B(x) ((x) & (~BITS_ATIMWND4_8822B))
#define BIT_GET_ATIMWND4_8822B(x)                                              \
	(((x) >> BIT_SHIFT_ATIMWND4_8822B) & BIT_MASK_ATIMWND4_8822B)
#define BIT_SET_ATIMWND4_8822B(x, v)                                           \
	(BIT_CLEAR_ATIMWND4_8822B(x) | BIT_ATIMWND4_8822B(v))

/* 2 REG_ATIMWND5_8822B */

#define BIT_SHIFT_ATIMWND5_8822B 0
#define BIT_MASK_ATIMWND5_8822B 0xff
#define BIT_ATIMWND5_8822B(x)                                                  \
	(((x) & BIT_MASK_ATIMWND5_8822B) << BIT_SHIFT_ATIMWND5_8822B)
#define BITS_ATIMWND5_8822B                                                    \
	(BIT_MASK_ATIMWND5_8822B << BIT_SHIFT_ATIMWND5_8822B)
#define BIT_CLEAR_ATIMWND5_8822B(x) ((x) & (~BITS_ATIMWND5_8822B))
#define BIT_GET_ATIMWND5_8822B(x)                                              \
	(((x) >> BIT_SHIFT_ATIMWND5_8822B) & BIT_MASK_ATIMWND5_8822B)
#define BIT_SET_ATIMWND5_8822B(x, v)                                           \
	(BIT_CLEAR_ATIMWND5_8822B(x) | BIT_ATIMWND5_8822B(v))

/* 2 REG_ATIMWND6_8822B */

#define BIT_SHIFT_ATIMWND6_8822B 0
#define BIT_MASK_ATIMWND6_8822B 0xff
#define BIT_ATIMWND6_8822B(x)                                                  \
	(((x) & BIT_MASK_ATIMWND6_8822B) << BIT_SHIFT_ATIMWND6_8822B)
#define BITS_ATIMWND6_8822B                                                    \
	(BIT_MASK_ATIMWND6_8822B << BIT_SHIFT_ATIMWND6_8822B)
#define BIT_CLEAR_ATIMWND6_8822B(x) ((x) & (~BITS_ATIMWND6_8822B))
#define BIT_GET_ATIMWND6_8822B(x)                                              \
	(((x) >> BIT_SHIFT_ATIMWND6_8822B) & BIT_MASK_ATIMWND6_8822B)
#define BIT_SET_ATIMWND6_8822B(x, v)                                           \
	(BIT_CLEAR_ATIMWND6_8822B(x) | BIT_ATIMWND6_8822B(v))

/* 2 REG_ATIMWND7_8822B */

#define BIT_SHIFT_ATIMWND7_8822B 0
#define BIT_MASK_ATIMWND7_8822B 0xff
#define BIT_ATIMWND7_8822B(x)                                                  \
	(((x) & BIT_MASK_ATIMWND7_8822B) << BIT_SHIFT_ATIMWND7_8822B)
#define BITS_ATIMWND7_8822B                                                    \
	(BIT_MASK_ATIMWND7_8822B << BIT_SHIFT_ATIMWND7_8822B)
#define BIT_CLEAR_ATIMWND7_8822B(x) ((x) & (~BITS_ATIMWND7_8822B))
#define BIT_GET_ATIMWND7_8822B(x)                                              \
	(((x) >> BIT_SHIFT_ATIMWND7_8822B) & BIT_MASK_ATIMWND7_8822B)
#define BIT_SET_ATIMWND7_8822B(x, v)                                           \
	(BIT_CLEAR_ATIMWND7_8822B(x) | BIT_ATIMWND7_8822B(v))

/* 2 REG_ATIMUGT_8822B */

#define BIT_SHIFT_ATIM_URGENT_8822B 0
#define BIT_MASK_ATIM_URGENT_8822B 0xff
#define BIT_ATIM_URGENT_8822B(x)                                               \
	(((x) & BIT_MASK_ATIM_URGENT_8822B) << BIT_SHIFT_ATIM_URGENT_8822B)
#define BITS_ATIM_URGENT_8822B                                                 \
	(BIT_MASK_ATIM_URGENT_8822B << BIT_SHIFT_ATIM_URGENT_8822B)
#define BIT_CLEAR_ATIM_URGENT_8822B(x) ((x) & (~BITS_ATIM_URGENT_8822B))
#define BIT_GET_ATIM_URGENT_8822B(x)                                           \
	(((x) >> BIT_SHIFT_ATIM_URGENT_8822B) & BIT_MASK_ATIM_URGENT_8822B)
#define BIT_SET_ATIM_URGENT_8822B(x, v)                                        \
	(BIT_CLEAR_ATIM_URGENT_8822B(x) | BIT_ATIM_URGENT_8822B(v))

/* 2 REG_HIQ_NO_LMT_EN_8822B */
#define BIT_HIQ_NO_LMT_EN_VAP7_8822B BIT(7)
#define BIT_HIQ_NO_LMT_EN_VAP6_8822B BIT(6)
#define BIT_HIQ_NO_LMT_EN_VAP5_8822B BIT(5)
#define BIT_HIQ_NO_LMT_EN_VAP4_8822B BIT(4)
#define BIT_HIQ_NO_LMT_EN_VAP3_8822B BIT(3)
#define BIT_HIQ_NO_LMT_EN_VAP2_8822B BIT(2)
#define BIT_HIQ_NO_LMT_EN_VAP1_8822B BIT(1)
#define BIT_HIQ_NO_LMT_EN_ROOT_8822B BIT(0)

/* 2 REG_DTIM_COUNTER_ROOT_8822B */

#define BIT_SHIFT_DTIM_COUNT_ROOT_8822B 0
#define BIT_MASK_DTIM_COUNT_ROOT_8822B 0xff
#define BIT_DTIM_COUNT_ROOT_8822B(x)                                           \
	(((x) & BIT_MASK_DTIM_COUNT_ROOT_8822B)                                \
	 << BIT_SHIFT_DTIM_COUNT_ROOT_8822B)
#define BITS_DTIM_COUNT_ROOT_8822B                                             \
	(BIT_MASK_DTIM_COUNT_ROOT_8822B << BIT_SHIFT_DTIM_COUNT_ROOT_8822B)
#define BIT_CLEAR_DTIM_COUNT_ROOT_8822B(x) ((x) & (~BITS_DTIM_COUNT_ROOT_8822B))
#define BIT_GET_DTIM_COUNT_ROOT_8822B(x)                                       \
	(((x) >> BIT_SHIFT_DTIM_COUNT_ROOT_8822B) &                            \
	 BIT_MASK_DTIM_COUNT_ROOT_8822B)
#define BIT_SET_DTIM_COUNT_ROOT_8822B(x, v)                                    \
	(BIT_CLEAR_DTIM_COUNT_ROOT_8822B(x) | BIT_DTIM_COUNT_ROOT_8822B(v))

/* 2 REG_DTIM_COUNTER_VAP1_8822B */

#define BIT_SHIFT_DTIM_COUNT_VAP1_8822B 0
#define BIT_MASK_DTIM_COUNT_VAP1_8822B 0xff
#define BIT_DTIM_COUNT_VAP1_8822B(x)                                           \
	(((x) & BIT_MASK_DTIM_COUNT_VAP1_8822B)                                \
	 << BIT_SHIFT_DTIM_COUNT_VAP1_8822B)
#define BITS_DTIM_COUNT_VAP1_8822B                                             \
	(BIT_MASK_DTIM_COUNT_VAP1_8822B << BIT_SHIFT_DTIM_COUNT_VAP1_8822B)
#define BIT_CLEAR_DTIM_COUNT_VAP1_8822B(x) ((x) & (~BITS_DTIM_COUNT_VAP1_8822B))
#define BIT_GET_DTIM_COUNT_VAP1_8822B(x)                                       \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP1_8822B) &                            \
	 BIT_MASK_DTIM_COUNT_VAP1_8822B)
#define BIT_SET_DTIM_COUNT_VAP1_8822B(x, v)                                    \
	(BIT_CLEAR_DTIM_COUNT_VAP1_8822B(x) | BIT_DTIM_COUNT_VAP1_8822B(v))

/* 2 REG_DTIM_COUNTER_VAP2_8822B */

#define BIT_SHIFT_DTIM_COUNT_VAP2_8822B 0
#define BIT_MASK_DTIM_COUNT_VAP2_8822B 0xff
#define BIT_DTIM_COUNT_VAP2_8822B(x)                                           \
	(((x) & BIT_MASK_DTIM_COUNT_VAP2_8822B)                                \
	 << BIT_SHIFT_DTIM_COUNT_VAP2_8822B)
#define BITS_DTIM_COUNT_VAP2_8822B                                             \
	(BIT_MASK_DTIM_COUNT_VAP2_8822B << BIT_SHIFT_DTIM_COUNT_VAP2_8822B)
#define BIT_CLEAR_DTIM_COUNT_VAP2_8822B(x) ((x) & (~BITS_DTIM_COUNT_VAP2_8822B))
#define BIT_GET_DTIM_COUNT_VAP2_8822B(x)                                       \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP2_8822B) &                            \
	 BIT_MASK_DTIM_COUNT_VAP2_8822B)
#define BIT_SET_DTIM_COUNT_VAP2_8822B(x, v)                                    \
	(BIT_CLEAR_DTIM_COUNT_VAP2_8822B(x) | BIT_DTIM_COUNT_VAP2_8822B(v))

/* 2 REG_DTIM_COUNTER_VAP3_8822B */

#define BIT_SHIFT_DTIM_COUNT_VAP3_8822B 0
#define BIT_MASK_DTIM_COUNT_VAP3_8822B 0xff
#define BIT_DTIM_COUNT_VAP3_8822B(x)                                           \
	(((x) & BIT_MASK_DTIM_COUNT_VAP3_8822B)                                \
	 << BIT_SHIFT_DTIM_COUNT_VAP3_8822B)
#define BITS_DTIM_COUNT_VAP3_8822B                                             \
	(BIT_MASK_DTIM_COUNT_VAP3_8822B << BIT_SHIFT_DTIM_COUNT_VAP3_8822B)
#define BIT_CLEAR_DTIM_COUNT_VAP3_8822B(x) ((x) & (~BITS_DTIM_COUNT_VAP3_8822B))
#define BIT_GET_DTIM_COUNT_VAP3_8822B(x)                                       \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP3_8822B) &                            \
	 BIT_MASK_DTIM_COUNT_VAP3_8822B)
#define BIT_SET_DTIM_COUNT_VAP3_8822B(x, v)                                    \
	(BIT_CLEAR_DTIM_COUNT_VAP3_8822B(x) | BIT_DTIM_COUNT_VAP3_8822B(v))

/* 2 REG_DTIM_COUNTER_VAP4_8822B */

#define BIT_SHIFT_DTIM_COUNT_VAP4_8822B 0
#define BIT_MASK_DTIM_COUNT_VAP4_8822B 0xff
#define BIT_DTIM_COUNT_VAP4_8822B(x)                                           \
	(((x) & BIT_MASK_DTIM_COUNT_VAP4_8822B)                                \
	 << BIT_SHIFT_DTIM_COUNT_VAP4_8822B)
#define BITS_DTIM_COUNT_VAP4_8822B                                             \
	(BIT_MASK_DTIM_COUNT_VAP4_8822B << BIT_SHIFT_DTIM_COUNT_VAP4_8822B)
#define BIT_CLEAR_DTIM_COUNT_VAP4_8822B(x) ((x) & (~BITS_DTIM_COUNT_VAP4_8822B))
#define BIT_GET_DTIM_COUNT_VAP4_8822B(x)                                       \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP4_8822B) &                            \
	 BIT_MASK_DTIM_COUNT_VAP4_8822B)
#define BIT_SET_DTIM_COUNT_VAP4_8822B(x, v)                                    \
	(BIT_CLEAR_DTIM_COUNT_VAP4_8822B(x) | BIT_DTIM_COUNT_VAP4_8822B(v))

/* 2 REG_DTIM_COUNTER_VAP5_8822B */

#define BIT_SHIFT_DTIM_COUNT_VAP5_8822B 0
#define BIT_MASK_DTIM_COUNT_VAP5_8822B 0xff
#define BIT_DTIM_COUNT_VAP5_8822B(x)                                           \
	(((x) & BIT_MASK_DTIM_COUNT_VAP5_8822B)                                \
	 << BIT_SHIFT_DTIM_COUNT_VAP5_8822B)
#define BITS_DTIM_COUNT_VAP5_8822B                                             \
	(BIT_MASK_DTIM_COUNT_VAP5_8822B << BIT_SHIFT_DTIM_COUNT_VAP5_8822B)
#define BIT_CLEAR_DTIM_COUNT_VAP5_8822B(x) ((x) & (~BITS_DTIM_COUNT_VAP5_8822B))
#define BIT_GET_DTIM_COUNT_VAP5_8822B(x)                                       \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP5_8822B) &                            \
	 BIT_MASK_DTIM_COUNT_VAP5_8822B)
#define BIT_SET_DTIM_COUNT_VAP5_8822B(x, v)                                    \
	(BIT_CLEAR_DTIM_COUNT_VAP5_8822B(x) | BIT_DTIM_COUNT_VAP5_8822B(v))

/* 2 REG_DTIM_COUNTER_VAP6_8822B */

#define BIT_SHIFT_DTIM_COUNT_VAP6_8822B 0
#define BIT_MASK_DTIM_COUNT_VAP6_8822B 0xff
#define BIT_DTIM_COUNT_VAP6_8822B(x)                                           \
	(((x) & BIT_MASK_DTIM_COUNT_VAP6_8822B)                                \
	 << BIT_SHIFT_DTIM_COUNT_VAP6_8822B)
#define BITS_DTIM_COUNT_VAP6_8822B                                             \
	(BIT_MASK_DTIM_COUNT_VAP6_8822B << BIT_SHIFT_DTIM_COUNT_VAP6_8822B)
#define BIT_CLEAR_DTIM_COUNT_VAP6_8822B(x) ((x) & (~BITS_DTIM_COUNT_VAP6_8822B))
#define BIT_GET_DTIM_COUNT_VAP6_8822B(x)                                       \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP6_8822B) &                            \
	 BIT_MASK_DTIM_COUNT_VAP6_8822B)
#define BIT_SET_DTIM_COUNT_VAP6_8822B(x, v)                                    \
	(BIT_CLEAR_DTIM_COUNT_VAP6_8822B(x) | BIT_DTIM_COUNT_VAP6_8822B(v))

/* 2 REG_DTIM_COUNTER_VAP7_8822B */

#define BIT_SHIFT_DTIM_COUNT_VAP7_8822B 0
#define BIT_MASK_DTIM_COUNT_VAP7_8822B 0xff
#define BIT_DTIM_COUNT_VAP7_8822B(x)                                           \
	(((x) & BIT_MASK_DTIM_COUNT_VAP7_8822B)                                \
	 << BIT_SHIFT_DTIM_COUNT_VAP7_8822B)
#define BITS_DTIM_COUNT_VAP7_8822B                                             \
	(BIT_MASK_DTIM_COUNT_VAP7_8822B << BIT_SHIFT_DTIM_COUNT_VAP7_8822B)
#define BIT_CLEAR_DTIM_COUNT_VAP7_8822B(x) ((x) & (~BITS_DTIM_COUNT_VAP7_8822B))
#define BIT_GET_DTIM_COUNT_VAP7_8822B(x)                                       \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP7_8822B) &                            \
	 BIT_MASK_DTIM_COUNT_VAP7_8822B)
#define BIT_SET_DTIM_COUNT_VAP7_8822B(x, v)                                    \
	(BIT_CLEAR_DTIM_COUNT_VAP7_8822B(x) | BIT_DTIM_COUNT_VAP7_8822B(v))

/* 2 REG_DIS_ATIM_8822B */
#define BIT_DIS_ATIM_VAP7_8822B BIT(7)
#define BIT_DIS_ATIM_VAP6_8822B BIT(6)
#define BIT_DIS_ATIM_VAP5_8822B BIT(5)
#define BIT_DIS_ATIM_VAP4_8822B BIT(4)
#define BIT_DIS_ATIM_VAP3_8822B BIT(3)
#define BIT_DIS_ATIM_VAP2_8822B BIT(2)
#define BIT_DIS_ATIM_VAP1_8822B BIT(1)
#define BIT_DIS_ATIM_ROOT_8822B BIT(0)

/* 2 REG_EARLY_128US_8822B */

#define BIT_SHIFT_TSFT_SEL_TIMER1_8822B 3
#define BIT_MASK_TSFT_SEL_TIMER1_8822B 0x7
#define BIT_TSFT_SEL_TIMER1_8822B(x)                                           \
	(((x) & BIT_MASK_TSFT_SEL_TIMER1_8822B)                                \
	 << BIT_SHIFT_TSFT_SEL_TIMER1_8822B)
#define BITS_TSFT_SEL_TIMER1_8822B                                             \
	(BIT_MASK_TSFT_SEL_TIMER1_8822B << BIT_SHIFT_TSFT_SEL_TIMER1_8822B)
#define BIT_CLEAR_TSFT_SEL_TIMER1_8822B(x) ((x) & (~BITS_TSFT_SEL_TIMER1_8822B))
#define BIT_GET_TSFT_SEL_TIMER1_8822B(x)                                       \
	(((x) >> BIT_SHIFT_TSFT_SEL_TIMER1_8822B) &                            \
	 BIT_MASK_TSFT_SEL_TIMER1_8822B)
#define BIT_SET_TSFT_SEL_TIMER1_8822B(x, v)                                    \
	(BIT_CLEAR_TSFT_SEL_TIMER1_8822B(x) | BIT_TSFT_SEL_TIMER1_8822B(v))

#define BIT_SHIFT_EARLY_128US_8822B 0
#define BIT_MASK_EARLY_128US_8822B 0x7
#define BIT_EARLY_128US_8822B(x)                                               \
	(((x) & BIT_MASK_EARLY_128US_8822B) << BIT_SHIFT_EARLY_128US_8822B)
#define BITS_EARLY_128US_8822B                                                 \
	(BIT_MASK_EARLY_128US_8822B << BIT_SHIFT_EARLY_128US_8822B)
#define BIT_CLEAR_EARLY_128US_8822B(x) ((x) & (~BITS_EARLY_128US_8822B))
#define BIT_GET_EARLY_128US_8822B(x)                                           \
	(((x) >> BIT_SHIFT_EARLY_128US_8822B) & BIT_MASK_EARLY_128US_8822B)
#define BIT_SET_EARLY_128US_8822B(x, v)                                        \
	(BIT_CLEAR_EARLY_128US_8822B(x) | BIT_EARLY_128US_8822B(v))

/* 2 REG_P2PPS1_CTRL_8822B */
#define BIT_P2P1_CTW_ALLSTASLEEP_8822B BIT(7)
#define BIT_P2P1_OFF_DISTX_EN_8822B BIT(6)
#define BIT_P2P1_PWR_MGT_EN_8822B BIT(5)
#define BIT_P2P1_NOA1_EN_8822B BIT(2)
#define BIT_P2P1_NOA0_EN_8822B BIT(1)

/* 2 REG_P2PPS2_CTRL_8822B */
#define BIT_P2P2_CTW_ALLSTASLEEP_8822B BIT(7)
#define BIT_P2P2_OFF_DISTX_EN_8822B BIT(6)
#define BIT_P2P2_PWR_MGT_EN_8822B BIT(5)
#define BIT_P2P2_NOA1_EN_8822B BIT(2)
#define BIT_P2P2_NOA0_EN_8822B BIT(1)

/* 2 REG_TIMER0_SRC_SEL_8822B */

#define BIT_SHIFT_SYNC_CLI_SEL_8822B 4
#define BIT_MASK_SYNC_CLI_SEL_8822B 0x7
#define BIT_SYNC_CLI_SEL_8822B(x)                                              \
	(((x) & BIT_MASK_SYNC_CLI_SEL_8822B) << BIT_SHIFT_SYNC_CLI_SEL_8822B)
#define BITS_SYNC_CLI_SEL_8822B                                                \
	(BIT_MASK_SYNC_CLI_SEL_8822B << BIT_SHIFT_SYNC_CLI_SEL_8822B)
#define BIT_CLEAR_SYNC_CLI_SEL_8822B(x) ((x) & (~BITS_SYNC_CLI_SEL_8822B))
#define BIT_GET_SYNC_CLI_SEL_8822B(x)                                          \
	(((x) >> BIT_SHIFT_SYNC_CLI_SEL_8822B) & BIT_MASK_SYNC_CLI_SEL_8822B)
#define BIT_SET_SYNC_CLI_SEL_8822B(x, v)                                       \
	(BIT_CLEAR_SYNC_CLI_SEL_8822B(x) | BIT_SYNC_CLI_SEL_8822B(v))

#define BIT_SHIFT_TSFT_SEL_TIMER0_8822B 0
#define BIT_MASK_TSFT_SEL_TIMER0_8822B 0x7
#define BIT_TSFT_SEL_TIMER0_8822B(x)                                           \
	(((x) & BIT_MASK_TSFT_SEL_TIMER0_8822B)                                \
	 << BIT_SHIFT_TSFT_SEL_TIMER0_8822B)
#define BITS_TSFT_SEL_TIMER0_8822B                                             \
	(BIT_MASK_TSFT_SEL_TIMER0_8822B << BIT_SHIFT_TSFT_SEL_TIMER0_8822B)
#define BIT_CLEAR_TSFT_SEL_TIMER0_8822B(x) ((x) & (~BITS_TSFT_SEL_TIMER0_8822B))
#define BIT_GET_TSFT_SEL_TIMER0_8822B(x)                                       \
	(((x) >> BIT_SHIFT_TSFT_SEL_TIMER0_8822B) &                            \
	 BIT_MASK_TSFT_SEL_TIMER0_8822B)
#define BIT_SET_TSFT_SEL_TIMER0_8822B(x, v)                                    \
	(BIT_CLEAR_TSFT_SEL_TIMER0_8822B(x) | BIT_TSFT_SEL_TIMER0_8822B(v))

/* 2 REG_NOA_UNIT_SEL_8822B */

#define BIT_SHIFT_NOA_UNIT2_SEL_8822B 8
#define BIT_MASK_NOA_UNIT2_SEL_8822B 0x7
#define BIT_NOA_UNIT2_SEL_8822B(x)                                             \
	(((x) & BIT_MASK_NOA_UNIT2_SEL_8822B) << BIT_SHIFT_NOA_UNIT2_SEL_8822B)
#define BITS_NOA_UNIT2_SEL_8822B                                               \
	(BIT_MASK_NOA_UNIT2_SEL_8822B << BIT_SHIFT_NOA_UNIT2_SEL_8822B)
#define BIT_CLEAR_NOA_UNIT2_SEL_8822B(x) ((x) & (~BITS_NOA_UNIT2_SEL_8822B))
#define BIT_GET_NOA_UNIT2_SEL_8822B(x)                                         \
	(((x) >> BIT_SHIFT_NOA_UNIT2_SEL_8822B) & BIT_MASK_NOA_UNIT2_SEL_8822B)
#define BIT_SET_NOA_UNIT2_SEL_8822B(x, v)                                      \
	(BIT_CLEAR_NOA_UNIT2_SEL_8822B(x) | BIT_NOA_UNIT2_SEL_8822B(v))

#define BIT_SHIFT_NOA_UNIT1_SEL_8822B 4
#define BIT_MASK_NOA_UNIT1_SEL_8822B 0x7
#define BIT_NOA_UNIT1_SEL_8822B(x)                                             \
	(((x) & BIT_MASK_NOA_UNIT1_SEL_8822B) << BIT_SHIFT_NOA_UNIT1_SEL_8822B)
#define BITS_NOA_UNIT1_SEL_8822B                                               \
	(BIT_MASK_NOA_UNIT1_SEL_8822B << BIT_SHIFT_NOA_UNIT1_SEL_8822B)
#define BIT_CLEAR_NOA_UNIT1_SEL_8822B(x) ((x) & (~BITS_NOA_UNIT1_SEL_8822B))
#define BIT_GET_NOA_UNIT1_SEL_8822B(x)                                         \
	(((x) >> BIT_SHIFT_NOA_UNIT1_SEL_8822B) & BIT_MASK_NOA_UNIT1_SEL_8822B)
#define BIT_SET_NOA_UNIT1_SEL_8822B(x, v)                                      \
	(BIT_CLEAR_NOA_UNIT1_SEL_8822B(x) | BIT_NOA_UNIT1_SEL_8822B(v))

#define BIT_SHIFT_NOA_UNIT0_SEL_8822B 0
#define BIT_MASK_NOA_UNIT0_SEL_8822B 0x7
#define BIT_NOA_UNIT0_SEL_8822B(x)                                             \
	(((x) & BIT_MASK_NOA_UNIT0_SEL_8822B) << BIT_SHIFT_NOA_UNIT0_SEL_8822B)
#define BITS_NOA_UNIT0_SEL_8822B                                               \
	(BIT_MASK_NOA_UNIT0_SEL_8822B << BIT_SHIFT_NOA_UNIT0_SEL_8822B)
#define BIT_CLEAR_NOA_UNIT0_SEL_8822B(x) ((x) & (~BITS_NOA_UNIT0_SEL_8822B))
#define BIT_GET_NOA_UNIT0_SEL_8822B(x)                                         \
	(((x) >> BIT_SHIFT_NOA_UNIT0_SEL_8822B) & BIT_MASK_NOA_UNIT0_SEL_8822B)
#define BIT_SET_NOA_UNIT0_SEL_8822B(x, v)                                      \
	(BIT_CLEAR_NOA_UNIT0_SEL_8822B(x) | BIT_NOA_UNIT0_SEL_8822B(v))

/* 2 REG_P2POFF_DIS_TXTIME_8822B */

#define BIT_SHIFT_P2POFF_DIS_TXTIME_8822B 0
#define BIT_MASK_P2POFF_DIS_TXTIME_8822B 0xff
#define BIT_P2POFF_DIS_TXTIME_8822B(x)                                         \
	(((x) & BIT_MASK_P2POFF_DIS_TXTIME_8822B)                              \
	 << BIT_SHIFT_P2POFF_DIS_TXTIME_8822B)
#define BITS_P2POFF_DIS_TXTIME_8822B                                           \
	(BIT_MASK_P2POFF_DIS_TXTIME_8822B << BIT_SHIFT_P2POFF_DIS_TXTIME_8822B)
#define BIT_CLEAR_P2POFF_DIS_TXTIME_8822B(x)                                   \
	((x) & (~BITS_P2POFF_DIS_TXTIME_8822B))
#define BIT_GET_P2POFF_DIS_TXTIME_8822B(x)                                     \
	(((x) >> BIT_SHIFT_P2POFF_DIS_TXTIME_8822B) &                          \
	 BIT_MASK_P2POFF_DIS_TXTIME_8822B)
#define BIT_SET_P2POFF_DIS_TXTIME_8822B(x, v)                                  \
	(BIT_CLEAR_P2POFF_DIS_TXTIME_8822B(x) | BIT_P2POFF_DIS_TXTIME_8822B(v))

/* 2 REG_MBSSID_BCN_SPACE2_8822B */

#define BIT_SHIFT_BCN_SPACE_CLINT2_8822B 16
#define BIT_MASK_BCN_SPACE_CLINT2_8822B 0xfff
#define BIT_BCN_SPACE_CLINT2_8822B(x)                                          \
	(((x) & BIT_MASK_BCN_SPACE_CLINT2_8822B)                               \
	 << BIT_SHIFT_BCN_SPACE_CLINT2_8822B)
#define BITS_BCN_SPACE_CLINT2_8822B                                            \
	(BIT_MASK_BCN_SPACE_CLINT2_8822B << BIT_SHIFT_BCN_SPACE_CLINT2_8822B)
#define BIT_CLEAR_BCN_SPACE_CLINT2_8822B(x)                                    \
	((x) & (~BITS_BCN_SPACE_CLINT2_8822B))
#define BIT_GET_BCN_SPACE_CLINT2_8822B(x)                                      \
	(((x) >> BIT_SHIFT_BCN_SPACE_CLINT2_8822B) &                           \
	 BIT_MASK_BCN_SPACE_CLINT2_8822B)
#define BIT_SET_BCN_SPACE_CLINT2_8822B(x, v)                                   \
	(BIT_CLEAR_BCN_SPACE_CLINT2_8822B(x) | BIT_BCN_SPACE_CLINT2_8822B(v))

#define BIT_SHIFT_BCN_SPACE_CLINT1_8822B 0
#define BIT_MASK_BCN_SPACE_CLINT1_8822B 0xfff
#define BIT_BCN_SPACE_CLINT1_8822B(x)                                          \
	(((x) & BIT_MASK_BCN_SPACE_CLINT1_8822B)                               \
	 << BIT_SHIFT_BCN_SPACE_CLINT1_8822B)
#define BITS_BCN_SPACE_CLINT1_8822B                                            \
	(BIT_MASK_BCN_SPACE_CLINT1_8822B << BIT_SHIFT_BCN_SPACE_CLINT1_8822B)
#define BIT_CLEAR_BCN_SPACE_CLINT1_8822B(x)                                    \
	((x) & (~BITS_BCN_SPACE_CLINT1_8822B))
#define BIT_GET_BCN_SPACE_CLINT1_8822B(x)                                      \
	(((x) >> BIT_SHIFT_BCN_SPACE_CLINT1_8822B) &                           \
	 BIT_MASK_BCN_SPACE_CLINT1_8822B)
#define BIT_SET_BCN_SPACE_CLINT1_8822B(x, v)                                   \
	(BIT_CLEAR_BCN_SPACE_CLINT1_8822B(x) | BIT_BCN_SPACE_CLINT1_8822B(v))

/* 2 REG_MBSSID_BCN_SPACE3_8822B */

#define BIT_SHIFT_SUB_BCN_SPACE_8822B 16
#define BIT_MASK_SUB_BCN_SPACE_8822B 0xff
#define BIT_SUB_BCN_SPACE_8822B(x)                                             \
	(((x) & BIT_MASK_SUB_BCN_SPACE_8822B) << BIT_SHIFT_SUB_BCN_SPACE_8822B)
#define BITS_SUB_BCN_SPACE_8822B                                               \
	(BIT_MASK_SUB_BCN_SPACE_8822B << BIT_SHIFT_SUB_BCN_SPACE_8822B)
#define BIT_CLEAR_SUB_BCN_SPACE_8822B(x) ((x) & (~BITS_SUB_BCN_SPACE_8822B))
#define BIT_GET_SUB_BCN_SPACE_8822B(x)                                         \
	(((x) >> BIT_SHIFT_SUB_BCN_SPACE_8822B) & BIT_MASK_SUB_BCN_SPACE_8822B)
#define BIT_SET_SUB_BCN_SPACE_8822B(x, v)                                      \
	(BIT_CLEAR_SUB_BCN_SPACE_8822B(x) | BIT_SUB_BCN_SPACE_8822B(v))

#define BIT_SHIFT_BCN_SPACE_CLINT3_8822B 0
#define BIT_MASK_BCN_SPACE_CLINT3_8822B 0xfff
#define BIT_BCN_SPACE_CLINT3_8822B(x)                                          \
	(((x) & BIT_MASK_BCN_SPACE_CLINT3_8822B)                               \
	 << BIT_SHIFT_BCN_SPACE_CLINT3_8822B)
#define BITS_BCN_SPACE_CLINT3_8822B                                            \
	(BIT_MASK_BCN_SPACE_CLINT3_8822B << BIT_SHIFT_BCN_SPACE_CLINT3_8822B)
#define BIT_CLEAR_BCN_SPACE_CLINT3_8822B(x)                                    \
	((x) & (~BITS_BCN_SPACE_CLINT3_8822B))
#define BIT_GET_BCN_SPACE_CLINT3_8822B(x)                                      \
	(((x) >> BIT_SHIFT_BCN_SPACE_CLINT3_8822B) &                           \
	 BIT_MASK_BCN_SPACE_CLINT3_8822B)
#define BIT_SET_BCN_SPACE_CLINT3_8822B(x, v)                                   \
	(BIT_CLEAR_BCN_SPACE_CLINT3_8822B(x) | BIT_BCN_SPACE_CLINT3_8822B(v))

/* 2 REG_ACMHWCTRL_8822B */
#define BIT_BEQ_ACM_STATUS_8822B BIT(7)
#define BIT_VIQ_ACM_STATUS_8822B BIT(6)
#define BIT_VOQ_ACM_STATUS_8822B BIT(5)
#define BIT_BEQ_ACM_EN_8822B BIT(3)
#define BIT_VIQ_ACM_EN_8822B BIT(2)
#define BIT_VOQ_ACM_EN_8822B BIT(1)
#define BIT_ACMHWEN_8822B BIT(0)

/* 2 REG_ACMRSTCTRL_8822B */
#define BIT_BE_ACM_RESET_USED_TIME_8822B BIT(2)
#define BIT_VI_ACM_RESET_USED_TIME_8822B BIT(1)
#define BIT_VO_ACM_RESET_USED_TIME_8822B BIT(0)

/* 2 REG_ACMAVG_8822B */

#define BIT_SHIFT_AVGPERIOD_8822B 0
#define BIT_MASK_AVGPERIOD_8822B 0xffff
#define BIT_AVGPERIOD_8822B(x)                                                 \
	(((x) & BIT_MASK_AVGPERIOD_8822B) << BIT_SHIFT_AVGPERIOD_8822B)
#define BITS_AVGPERIOD_8822B                                                   \
	(BIT_MASK_AVGPERIOD_8822B << BIT_SHIFT_AVGPERIOD_8822B)
#define BIT_CLEAR_AVGPERIOD_8822B(x) ((x) & (~BITS_AVGPERIOD_8822B))
#define BIT_GET_AVGPERIOD_8822B(x)                                             \
	(((x) >> BIT_SHIFT_AVGPERIOD_8822B) & BIT_MASK_AVGPERIOD_8822B)
#define BIT_SET_AVGPERIOD_8822B(x, v)                                          \
	(BIT_CLEAR_AVGPERIOD_8822B(x) | BIT_AVGPERIOD_8822B(v))

/* 2 REG_VO_ADMTIME_8822B */

#define BIT_SHIFT_VO_ADMITTED_TIME_8822B 0
#define BIT_MASK_VO_ADMITTED_TIME_8822B 0xffff
#define BIT_VO_ADMITTED_TIME_8822B(x)                                          \
	(((x) & BIT_MASK_VO_ADMITTED_TIME_8822B)                               \
	 << BIT_SHIFT_VO_ADMITTED_TIME_8822B)
#define BITS_VO_ADMITTED_TIME_8822B                                            \
	(BIT_MASK_VO_ADMITTED_TIME_8822B << BIT_SHIFT_VO_ADMITTED_TIME_8822B)
#define BIT_CLEAR_VO_ADMITTED_TIME_8822B(x)                                    \
	((x) & (~BITS_VO_ADMITTED_TIME_8822B))
#define BIT_GET_VO_ADMITTED_TIME_8822B(x)                                      \
	(((x) >> BIT_SHIFT_VO_ADMITTED_TIME_8822B) &                           \
	 BIT_MASK_VO_ADMITTED_TIME_8822B)
#define BIT_SET_VO_ADMITTED_TIME_8822B(x, v)                                   \
	(BIT_CLEAR_VO_ADMITTED_TIME_8822B(x) | BIT_VO_ADMITTED_TIME_8822B(v))

/* 2 REG_VI_ADMTIME_8822B */

#define BIT_SHIFT_VI_ADMITTED_TIME_8822B 0
#define BIT_MASK_VI_ADMITTED_TIME_8822B 0xffff
#define BIT_VI_ADMITTED_TIME_8822B(x)                                          \
	(((x) & BIT_MASK_VI_ADMITTED_TIME_8822B)                               \
	 << BIT_SHIFT_VI_ADMITTED_TIME_8822B)
#define BITS_VI_ADMITTED_TIME_8822B                                            \
	(BIT_MASK_VI_ADMITTED_TIME_8822B << BIT_SHIFT_VI_ADMITTED_TIME_8822B)
#define BIT_CLEAR_VI_ADMITTED_TIME_8822B(x)                                    \
	((x) & (~BITS_VI_ADMITTED_TIME_8822B))
#define BIT_GET_VI_ADMITTED_TIME_8822B(x)                                      \
	(((x) >> BIT_SHIFT_VI_ADMITTED_TIME_8822B) &                           \
	 BIT_MASK_VI_ADMITTED_TIME_8822B)
#define BIT_SET_VI_ADMITTED_TIME_8822B(x, v)                                   \
	(BIT_CLEAR_VI_ADMITTED_TIME_8822B(x) | BIT_VI_ADMITTED_TIME_8822B(v))

/* 2 REG_BE_ADMTIME_8822B */

#define BIT_SHIFT_BE_ADMITTED_TIME_8822B 0
#define BIT_MASK_BE_ADMITTED_TIME_8822B 0xffff
#define BIT_BE_ADMITTED_TIME_8822B(x)                                          \
	(((x) & BIT_MASK_BE_ADMITTED_TIME_8822B)                               \
	 << BIT_SHIFT_BE_ADMITTED_TIME_8822B)
#define BITS_BE_ADMITTED_TIME_8822B                                            \
	(BIT_MASK_BE_ADMITTED_TIME_8822B << BIT_SHIFT_BE_ADMITTED_TIME_8822B)
#define BIT_CLEAR_BE_ADMITTED_TIME_8822B(x)                                    \
	((x) & (~BITS_BE_ADMITTED_TIME_8822B))
#define BIT_GET_BE_ADMITTED_TIME_8822B(x)                                      \
	(((x) >> BIT_SHIFT_BE_ADMITTED_TIME_8822B) &                           \
	 BIT_MASK_BE_ADMITTED_TIME_8822B)
#define BIT_SET_BE_ADMITTED_TIME_8822B(x, v)                                   \
	(BIT_CLEAR_BE_ADMITTED_TIME_8822B(x) | BIT_BE_ADMITTED_TIME_8822B(v))

/* 2 REG_EDCA_RANDOM_GEN_8822B */

#define BIT_SHIFT_RANDOM_GEN_8822B 0
#define BIT_MASK_RANDOM_GEN_8822B 0xffffff
#define BIT_RANDOM_GEN_8822B(x)                                                \
	(((x) & BIT_MASK_RANDOM_GEN_8822B) << BIT_SHIFT_RANDOM_GEN_8822B)
#define BITS_RANDOM_GEN_8822B                                                  \
	(BIT_MASK_RANDOM_GEN_8822B << BIT_SHIFT_RANDOM_GEN_8822B)
#define BIT_CLEAR_RANDOM_GEN_8822B(x) ((x) & (~BITS_RANDOM_GEN_8822B))
#define BIT_GET_RANDOM_GEN_8822B(x)                                            \
	(((x) >> BIT_SHIFT_RANDOM_GEN_8822B) & BIT_MASK_RANDOM_GEN_8822B)
#define BIT_SET_RANDOM_GEN_8822B(x, v)                                         \
	(BIT_CLEAR_RANDOM_GEN_8822B(x) | BIT_RANDOM_GEN_8822B(v))

/* 2 REG_TXCMD_NOA_SEL_8822B */

#define BIT_SHIFT_NOA_SEL_V2_8822B 4
#define BIT_MASK_NOA_SEL_V2_8822B 0x7
#define BIT_NOA_SEL_V2_8822B(x)                                                \
	(((x) & BIT_MASK_NOA_SEL_V2_8822B) << BIT_SHIFT_NOA_SEL_V2_8822B)
#define BITS_NOA_SEL_V2_8822B                                                  \
	(BIT_MASK_NOA_SEL_V2_8822B << BIT_SHIFT_NOA_SEL_V2_8822B)
#define BIT_CLEAR_NOA_SEL_V2_8822B(x) ((x) & (~BITS_NOA_SEL_V2_8822B))
#define BIT_GET_NOA_SEL_V2_8822B(x)                                            \
	(((x) >> BIT_SHIFT_NOA_SEL_V2_8822B) & BIT_MASK_NOA_SEL_V2_8822B)
#define BIT_SET_NOA_SEL_V2_8822B(x, v)                                         \
	(BIT_CLEAR_NOA_SEL_V2_8822B(x) | BIT_NOA_SEL_V2_8822B(v))

#define BIT_SHIFT_TXCMD_SEG_SEL_8822B 0
#define BIT_MASK_TXCMD_SEG_SEL_8822B 0xf
#define BIT_TXCMD_SEG_SEL_8822B(x)                                             \
	(((x) & BIT_MASK_TXCMD_SEG_SEL_8822B) << BIT_SHIFT_TXCMD_SEG_SEL_8822B)
#define BITS_TXCMD_SEG_SEL_8822B                                               \
	(BIT_MASK_TXCMD_SEG_SEL_8822B << BIT_SHIFT_TXCMD_SEG_SEL_8822B)
#define BIT_CLEAR_TXCMD_SEG_SEL_8822B(x) ((x) & (~BITS_TXCMD_SEG_SEL_8822B))
#define BIT_GET_TXCMD_SEG_SEL_8822B(x)                                         \
	(((x) >> BIT_SHIFT_TXCMD_SEG_SEL_8822B) & BIT_MASK_TXCMD_SEG_SEL_8822B)
#define BIT_SET_TXCMD_SEG_SEL_8822B(x, v)                                      \
	(BIT_CLEAR_TXCMD_SEG_SEL_8822B(x) | BIT_TXCMD_SEG_SEL_8822B(v))

/* 2 REG_NOA_PARAM_8822B */

#define BIT_SHIFT_NOA_COUNT_8822B (96 & CPU_OPT_WIDTH)
#define BIT_MASK_NOA_COUNT_8822B 0xff
#define BIT_NOA_COUNT_8822B(x)                                                 \
	(((x) & BIT_MASK_NOA_COUNT_8822B) << BIT_SHIFT_NOA_COUNT_8822B)
#define BITS_NOA_COUNT_8822B                                                   \
	(BIT_MASK_NOA_COUNT_8822B << BIT_SHIFT_NOA_COUNT_8822B)
#define BIT_CLEAR_NOA_COUNT_8822B(x) ((x) & (~BITS_NOA_COUNT_8822B))
#define BIT_GET_NOA_COUNT_8822B(x)                                             \
	(((x) >> BIT_SHIFT_NOA_COUNT_8822B) & BIT_MASK_NOA_COUNT_8822B)
#define BIT_SET_NOA_COUNT_8822B(x, v)                                          \
	(BIT_CLEAR_NOA_COUNT_8822B(x) | BIT_NOA_COUNT_8822B(v))

#define BIT_SHIFT_NOA_START_TIME_8822B (64 & CPU_OPT_WIDTH)
#define BIT_MASK_NOA_START_TIME_8822B 0xffffffffL
#define BIT_NOA_START_TIME_8822B(x)                                            \
	(((x) & BIT_MASK_NOA_START_TIME_8822B)                                 \
	 << BIT_SHIFT_NOA_START_TIME_8822B)
#define BITS_NOA_START_TIME_8822B                                              \
	(BIT_MASK_NOA_START_TIME_8822B << BIT_SHIFT_NOA_START_TIME_8822B)
#define BIT_CLEAR_NOA_START_TIME_8822B(x) ((x) & (~BITS_NOA_START_TIME_8822B))
#define BIT_GET_NOA_START_TIME_8822B(x)                                        \
	(((x) >> BIT_SHIFT_NOA_START_TIME_8822B) &                             \
	 BIT_MASK_NOA_START_TIME_8822B)
#define BIT_SET_NOA_START_TIME_8822B(x, v)                                     \
	(BIT_CLEAR_NOA_START_TIME_8822B(x) | BIT_NOA_START_TIME_8822B(v))

#define BIT_SHIFT_NOA_INTERVAL_8822B (32 & CPU_OPT_WIDTH)
#define BIT_MASK_NOA_INTERVAL_8822B 0xffffffffL
#define BIT_NOA_INTERVAL_8822B(x)                                              \
	(((x) & BIT_MASK_NOA_INTERVAL_8822B) << BIT_SHIFT_NOA_INTERVAL_8822B)
#define BITS_NOA_INTERVAL_8822B                                                \
	(BIT_MASK_NOA_INTERVAL_8822B << BIT_SHIFT_NOA_INTERVAL_8822B)
#define BIT_CLEAR_NOA_INTERVAL_8822B(x) ((x) & (~BITS_NOA_INTERVAL_8822B))
#define BIT_GET_NOA_INTERVAL_8822B(x)                                          \
	(((x) >> BIT_SHIFT_NOA_INTERVAL_8822B) & BIT_MASK_NOA_INTERVAL_8822B)
#define BIT_SET_NOA_INTERVAL_8822B(x, v)                                       \
	(BIT_CLEAR_NOA_INTERVAL_8822B(x) | BIT_NOA_INTERVAL_8822B(v))

#define BIT_SHIFT_NOA_DURATION_8822B 0
#define BIT_MASK_NOA_DURATION_8822B 0xffffffffL
#define BIT_NOA_DURATION_8822B(x)                                              \
	(((x) & BIT_MASK_NOA_DURATION_8822B) << BIT_SHIFT_NOA_DURATION_8822B)
#define BITS_NOA_DURATION_8822B                                                \
	(BIT_MASK_NOA_DURATION_8822B << BIT_SHIFT_NOA_DURATION_8822B)
#define BIT_CLEAR_NOA_DURATION_8822B(x) ((x) & (~BITS_NOA_DURATION_8822B))
#define BIT_GET_NOA_DURATION_8822B(x)                                          \
	(((x) >> BIT_SHIFT_NOA_DURATION_8822B) & BIT_MASK_NOA_DURATION_8822B)
#define BIT_SET_NOA_DURATION_8822B(x, v)                                       \
	(BIT_CLEAR_NOA_DURATION_8822B(x) | BIT_NOA_DURATION_8822B(v))

/* 2 REG_P2P_RST_8822B */
#define BIT_P2P2_PWR_RST1_8822B BIT(5)
#define BIT_P2P2_PWR_RST0_8822B BIT(4)
#define BIT_P2P1_PWR_RST1_8822B BIT(3)
#define BIT_P2P1_PWR_RST0_8822B BIT(2)
#define BIT_P2P_PWR_RST1_V1_8822B BIT(1)
#define BIT_P2P_PWR_RST0_V1_8822B BIT(0)

/* 2 REG_SCHEDULER_RST_8822B */
#define BIT_SYNC_CLI_ONCE_RIGHT_NOW_8822B BIT(2)
#define BIT_SYNC_CLI_ONCE_BY_TBTT_8822B BIT(1)
#define BIT_SCHEDULER_RST_V1_8822B BIT(0)

/* 2 REG_SCH_TXCMD_8822B */

#define BIT_SHIFT_SCH_TXCMD_8822B 0
#define BIT_MASK_SCH_TXCMD_8822B 0xffffffffL
#define BIT_SCH_TXCMD_8822B(x)                                                 \
	(((x) & BIT_MASK_SCH_TXCMD_8822B) << BIT_SHIFT_SCH_TXCMD_8822B)
#define BITS_SCH_TXCMD_8822B                                                   \
	(BIT_MASK_SCH_TXCMD_8822B << BIT_SHIFT_SCH_TXCMD_8822B)
#define BIT_CLEAR_SCH_TXCMD_8822B(x) ((x) & (~BITS_SCH_TXCMD_8822B))
#define BIT_GET_SCH_TXCMD_8822B(x)                                             \
	(((x) >> BIT_SHIFT_SCH_TXCMD_8822B) & BIT_MASK_SCH_TXCMD_8822B)
#define BIT_SET_SCH_TXCMD_8822B(x, v)                                          \
	(BIT_CLEAR_SCH_TXCMD_8822B(x) | BIT_SCH_TXCMD_8822B(v))

/* 2 REG_PAGE5_DUMMY_8822B */

/* 2 REG_CPUMGQ_TX_TIMER_8822B */

#define BIT_SHIFT_CPUMGQ_TX_TIMER_V1_8822B 0
#define BIT_MASK_CPUMGQ_TX_TIMER_V1_8822B 0xffffffffL
#define BIT_CPUMGQ_TX_TIMER_V1_8822B(x)                                        \
	(((x) & BIT_MASK_CPUMGQ_TX_TIMER_V1_8822B)                             \
	 << BIT_SHIFT_CPUMGQ_TX_TIMER_V1_8822B)
#define BITS_CPUMGQ_TX_TIMER_V1_8822B                                          \
	(BIT_MASK_CPUMGQ_TX_TIMER_V1_8822B                                     \
	 << BIT_SHIFT_CPUMGQ_TX_TIMER_V1_8822B)
#define BIT_CLEAR_CPUMGQ_TX_TIMER_V1_8822B(x)                                  \
	((x) & (~BITS_CPUMGQ_TX_TIMER_V1_8822B))
#define BIT_GET_CPUMGQ_TX_TIMER_V1_8822B(x)                                    \
	(((x) >> BIT_SHIFT_CPUMGQ_TX_TIMER_V1_8822B) &                         \
	 BIT_MASK_CPUMGQ_TX_TIMER_V1_8822B)
#define BIT_SET_CPUMGQ_TX_TIMER_V1_8822B(x, v)                                 \
	(BIT_CLEAR_CPUMGQ_TX_TIMER_V1_8822B(x) |                               \
	 BIT_CPUMGQ_TX_TIMER_V1_8822B(v))

/* 2 REG_PS_TIMER_A_8822B */

#define BIT_SHIFT_PS_TIMER_A_V1_8822B 0
#define BIT_MASK_PS_TIMER_A_V1_8822B 0xffffffffL
#define BIT_PS_TIMER_A_V1_8822B(x)                                             \
	(((x) & BIT_MASK_PS_TIMER_A_V1_8822B) << BIT_SHIFT_PS_TIMER_A_V1_8822B)
#define BITS_PS_TIMER_A_V1_8822B                                               \
	(BIT_MASK_PS_TIMER_A_V1_8822B << BIT_SHIFT_PS_TIMER_A_V1_8822B)
#define BIT_CLEAR_PS_TIMER_A_V1_8822B(x) ((x) & (~BITS_PS_TIMER_A_V1_8822B))
#define BIT_GET_PS_TIMER_A_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_PS_TIMER_A_V1_8822B) & BIT_MASK_PS_TIMER_A_V1_8822B)
#define BIT_SET_PS_TIMER_A_V1_8822B(x, v)                                      \
	(BIT_CLEAR_PS_TIMER_A_V1_8822B(x) | BIT_PS_TIMER_A_V1_8822B(v))

/* 2 REG_PS_TIMER_B_8822B */

#define BIT_SHIFT_PS_TIMER_B_V1_8822B 0
#define BIT_MASK_PS_TIMER_B_V1_8822B 0xffffffffL
#define BIT_PS_TIMER_B_V1_8822B(x)                                             \
	(((x) & BIT_MASK_PS_TIMER_B_V1_8822B) << BIT_SHIFT_PS_TIMER_B_V1_8822B)
#define BITS_PS_TIMER_B_V1_8822B                                               \
	(BIT_MASK_PS_TIMER_B_V1_8822B << BIT_SHIFT_PS_TIMER_B_V1_8822B)
#define BIT_CLEAR_PS_TIMER_B_V1_8822B(x) ((x) & (~BITS_PS_TIMER_B_V1_8822B))
#define BIT_GET_PS_TIMER_B_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_PS_TIMER_B_V1_8822B) & BIT_MASK_PS_TIMER_B_V1_8822B)
#define BIT_SET_PS_TIMER_B_V1_8822B(x, v)                                      \
	(BIT_CLEAR_PS_TIMER_B_V1_8822B(x) | BIT_PS_TIMER_B_V1_8822B(v))

/* 2 REG_PS_TIMER_C_8822B */

#define BIT_SHIFT_PS_TIMER_C_V1_8822B 0
#define BIT_MASK_PS_TIMER_C_V1_8822B 0xffffffffL
#define BIT_PS_TIMER_C_V1_8822B(x)                                             \
	(((x) & BIT_MASK_PS_TIMER_C_V1_8822B) << BIT_SHIFT_PS_TIMER_C_V1_8822B)
#define BITS_PS_TIMER_C_V1_8822B                                               \
	(BIT_MASK_PS_TIMER_C_V1_8822B << BIT_SHIFT_PS_TIMER_C_V1_8822B)
#define BIT_CLEAR_PS_TIMER_C_V1_8822B(x) ((x) & (~BITS_PS_TIMER_C_V1_8822B))
#define BIT_GET_PS_TIMER_C_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_PS_TIMER_C_V1_8822B) & BIT_MASK_PS_TIMER_C_V1_8822B)
#define BIT_SET_PS_TIMER_C_V1_8822B(x, v)                                      \
	(BIT_CLEAR_PS_TIMER_C_V1_8822B(x) | BIT_PS_TIMER_C_V1_8822B(v))

/* 2 REG_PS_TIMER_ABC_CPUMGQ_TIMER_CRTL_8822B */
#define BIT_CPUMGQ_TIMER_EN_8822B BIT(31)
#define BIT_CPUMGQ_TX_EN_8822B BIT(28)

#define BIT_SHIFT_CPUMGQ_TIMER_TSF_SEL_8822B 24
#define BIT_MASK_CPUMGQ_TIMER_TSF_SEL_8822B 0x7
#define BIT_CPUMGQ_TIMER_TSF_SEL_8822B(x)                                      \
	(((x) & BIT_MASK_CPUMGQ_TIMER_TSF_SEL_8822B)                           \
	 << BIT_SHIFT_CPUMGQ_TIMER_TSF_SEL_8822B)
#define BITS_CPUMGQ_TIMER_TSF_SEL_8822B                                        \
	(BIT_MASK_CPUMGQ_TIMER_TSF_SEL_8822B                                   \
	 << BIT_SHIFT_CPUMGQ_TIMER_TSF_SEL_8822B)
#define BIT_CLEAR_CPUMGQ_TIMER_TSF_SEL_8822B(x)                                \
	((x) & (~BITS_CPUMGQ_TIMER_TSF_SEL_8822B))
#define BIT_GET_CPUMGQ_TIMER_TSF_SEL_8822B(x)                                  \
	(((x) >> BIT_SHIFT_CPUMGQ_TIMER_TSF_SEL_8822B) &                       \
	 BIT_MASK_CPUMGQ_TIMER_TSF_SEL_8822B)
#define BIT_SET_CPUMGQ_TIMER_TSF_SEL_8822B(x, v)                               \
	(BIT_CLEAR_CPUMGQ_TIMER_TSF_SEL_8822B(x) |                             \
	 BIT_CPUMGQ_TIMER_TSF_SEL_8822B(v))

#define BIT_PS_TIMER_C_EN_8822B BIT(23)

#define BIT_SHIFT_PS_TIMER_C_TSF_SEL_8822B 16
#define BIT_MASK_PS_TIMER_C_TSF_SEL_8822B 0x7
#define BIT_PS_TIMER_C_TSF_SEL_8822B(x)                                        \
	(((x) & BIT_MASK_PS_TIMER_C_TSF_SEL_8822B)                             \
	 << BIT_SHIFT_PS_TIMER_C_TSF_SEL_8822B)
#define BITS_PS_TIMER_C_TSF_SEL_8822B                                          \
	(BIT_MASK_PS_TIMER_C_TSF_SEL_8822B                                     \
	 << BIT_SHIFT_PS_TIMER_C_TSF_SEL_8822B)
#define BIT_CLEAR_PS_TIMER_C_TSF_SEL_8822B(x)                                  \
	((x) & (~BITS_PS_TIMER_C_TSF_SEL_8822B))
#define BIT_GET_PS_TIMER_C_TSF_SEL_8822B(x)                                    \
	(((x) >> BIT_SHIFT_PS_TIMER_C_TSF_SEL_8822B) &                         \
	 BIT_MASK_PS_TIMER_C_TSF_SEL_8822B)
#define BIT_SET_PS_TIMER_C_TSF_SEL_8822B(x, v)                                 \
	(BIT_CLEAR_PS_TIMER_C_TSF_SEL_8822B(x) |                               \
	 BIT_PS_TIMER_C_TSF_SEL_8822B(v))

#define BIT_PS_TIMER_B_EN_8822B BIT(15)

#define BIT_SHIFT_PS_TIMER_B_TSF_SEL_8822B 8
#define BIT_MASK_PS_TIMER_B_TSF_SEL_8822B 0x7
#define BIT_PS_TIMER_B_TSF_SEL_8822B(x)                                        \
	(((x) & BIT_MASK_PS_TIMER_B_TSF_SEL_8822B)                             \
	 << BIT_SHIFT_PS_TIMER_B_TSF_SEL_8822B)
#define BITS_PS_TIMER_B_TSF_SEL_8822B                                          \
	(BIT_MASK_PS_TIMER_B_TSF_SEL_8822B                                     \
	 << BIT_SHIFT_PS_TIMER_B_TSF_SEL_8822B)
#define BIT_CLEAR_PS_TIMER_B_TSF_SEL_8822B(x)                                  \
	((x) & (~BITS_PS_TIMER_B_TSF_SEL_8822B))
#define BIT_GET_PS_TIMER_B_TSF_SEL_8822B(x)                                    \
	(((x) >> BIT_SHIFT_PS_TIMER_B_TSF_SEL_8822B) &                         \
	 BIT_MASK_PS_TIMER_B_TSF_SEL_8822B)
#define BIT_SET_PS_TIMER_B_TSF_SEL_8822B(x, v)                                 \
	(BIT_CLEAR_PS_TIMER_B_TSF_SEL_8822B(x) |                               \
	 BIT_PS_TIMER_B_TSF_SEL_8822B(v))

#define BIT_PS_TIMER_A_EN_8822B BIT(7)

#define BIT_SHIFT_PS_TIMER_A_TSF_SEL_8822B 0
#define BIT_MASK_PS_TIMER_A_TSF_SEL_8822B 0x7
#define BIT_PS_TIMER_A_TSF_SEL_8822B(x)                                        \
	(((x) & BIT_MASK_PS_TIMER_A_TSF_SEL_8822B)                             \
	 << BIT_SHIFT_PS_TIMER_A_TSF_SEL_8822B)
#define BITS_PS_TIMER_A_TSF_SEL_8822B                                          \
	(BIT_MASK_PS_TIMER_A_TSF_SEL_8822B                                     \
	 << BIT_SHIFT_PS_TIMER_A_TSF_SEL_8822B)
#define BIT_CLEAR_PS_TIMER_A_TSF_SEL_8822B(x)                                  \
	((x) & (~BITS_PS_TIMER_A_TSF_SEL_8822B))
#define BIT_GET_PS_TIMER_A_TSF_SEL_8822B(x)                                    \
	(((x) >> BIT_SHIFT_PS_TIMER_A_TSF_SEL_8822B) &                         \
	 BIT_MASK_PS_TIMER_A_TSF_SEL_8822B)
#define BIT_SET_PS_TIMER_A_TSF_SEL_8822B(x, v)                                 \
	(BIT_CLEAR_PS_TIMER_A_TSF_SEL_8822B(x) |                               \
	 BIT_PS_TIMER_A_TSF_SEL_8822B(v))

/* 2 REG_CPUMGQ_TX_TIMER_EARLY_8822B */

#define BIT_SHIFT_CPUMGQ_TX_TIMER_EARLY_8822B 0
#define BIT_MASK_CPUMGQ_TX_TIMER_EARLY_8822B 0xff
#define BIT_CPUMGQ_TX_TIMER_EARLY_8822B(x)                                     \
	(((x) & BIT_MASK_CPUMGQ_TX_TIMER_EARLY_8822B)                          \
	 << BIT_SHIFT_CPUMGQ_TX_TIMER_EARLY_8822B)
#define BITS_CPUMGQ_TX_TIMER_EARLY_8822B                                       \
	(BIT_MASK_CPUMGQ_TX_TIMER_EARLY_8822B                                  \
	 << BIT_SHIFT_CPUMGQ_TX_TIMER_EARLY_8822B)
#define BIT_CLEAR_CPUMGQ_TX_TIMER_EARLY_8822B(x)                               \
	((x) & (~BITS_CPUMGQ_TX_TIMER_EARLY_8822B))
#define BIT_GET_CPUMGQ_TX_TIMER_EARLY_8822B(x)                                 \
	(((x) >> BIT_SHIFT_CPUMGQ_TX_TIMER_EARLY_8822B) &                      \
	 BIT_MASK_CPUMGQ_TX_TIMER_EARLY_8822B)
#define BIT_SET_CPUMGQ_TX_TIMER_EARLY_8822B(x, v)                              \
	(BIT_CLEAR_CPUMGQ_TX_TIMER_EARLY_8822B(x) |                            \
	 BIT_CPUMGQ_TX_TIMER_EARLY_8822B(v))

/* 2 REG_PS_TIMER_A_EARLY_8822B */

#define BIT_SHIFT_PS_TIMER_A_EARLY_8822B 0
#define BIT_MASK_PS_TIMER_A_EARLY_8822B 0xff
#define BIT_PS_TIMER_A_EARLY_8822B(x)                                          \
	(((x) & BIT_MASK_PS_TIMER_A_EARLY_8822B)                               \
	 << BIT_SHIFT_PS_TIMER_A_EARLY_8822B)
#define BITS_PS_TIMER_A_EARLY_8822B                                            \
	(BIT_MASK_PS_TIMER_A_EARLY_8822B << BIT_SHIFT_PS_TIMER_A_EARLY_8822B)
#define BIT_CLEAR_PS_TIMER_A_EARLY_8822B(x)                                    \
	((x) & (~BITS_PS_TIMER_A_EARLY_8822B))
#define BIT_GET_PS_TIMER_A_EARLY_8822B(x)                                      \
	(((x) >> BIT_SHIFT_PS_TIMER_A_EARLY_8822B) &                           \
	 BIT_MASK_PS_TIMER_A_EARLY_8822B)
#define BIT_SET_PS_TIMER_A_EARLY_8822B(x, v)                                   \
	(BIT_CLEAR_PS_TIMER_A_EARLY_8822B(x) | BIT_PS_TIMER_A_EARLY_8822B(v))

/* 2 REG_PS_TIMER_B_EARLY_8822B */

#define BIT_SHIFT_PS_TIMER_B_EARLY_8822B 0
#define BIT_MASK_PS_TIMER_B_EARLY_8822B 0xff
#define BIT_PS_TIMER_B_EARLY_8822B(x)                                          \
	(((x) & BIT_MASK_PS_TIMER_B_EARLY_8822B)                               \
	 << BIT_SHIFT_PS_TIMER_B_EARLY_8822B)
#define BITS_PS_TIMER_B_EARLY_8822B                                            \
	(BIT_MASK_PS_TIMER_B_EARLY_8822B << BIT_SHIFT_PS_TIMER_B_EARLY_8822B)
#define BIT_CLEAR_PS_TIMER_B_EARLY_8822B(x)                                    \
	((x) & (~BITS_PS_TIMER_B_EARLY_8822B))
#define BIT_GET_PS_TIMER_B_EARLY_8822B(x)                                      \
	(((x) >> BIT_SHIFT_PS_TIMER_B_EARLY_8822B) &                           \
	 BIT_MASK_PS_TIMER_B_EARLY_8822B)
#define BIT_SET_PS_TIMER_B_EARLY_8822B(x, v)                                   \
	(BIT_CLEAR_PS_TIMER_B_EARLY_8822B(x) | BIT_PS_TIMER_B_EARLY_8822B(v))

/* 2 REG_PS_TIMER_C_EARLY_8822B */

#define BIT_SHIFT_PS_TIMER_C_EARLY_8822B 0
#define BIT_MASK_PS_TIMER_C_EARLY_8822B 0xff
#define BIT_PS_TIMER_C_EARLY_8822B(x)                                          \
	(((x) & BIT_MASK_PS_TIMER_C_EARLY_8822B)                               \
	 << BIT_SHIFT_PS_TIMER_C_EARLY_8822B)
#define BITS_PS_TIMER_C_EARLY_8822B                                            \
	(BIT_MASK_PS_TIMER_C_EARLY_8822B << BIT_SHIFT_PS_TIMER_C_EARLY_8822B)
#define BIT_CLEAR_PS_TIMER_C_EARLY_8822B(x)                                    \
	((x) & (~BITS_PS_TIMER_C_EARLY_8822B))
#define BIT_GET_PS_TIMER_C_EARLY_8822B(x)                                      \
	(((x) >> BIT_SHIFT_PS_TIMER_C_EARLY_8822B) &                           \
	 BIT_MASK_PS_TIMER_C_EARLY_8822B)
#define BIT_SET_PS_TIMER_C_EARLY_8822B(x, v)                                   \
	(BIT_CLEAR_PS_TIMER_C_EARLY_8822B(x) | BIT_PS_TIMER_C_EARLY_8822B(v))

/* 2 REG_CPUMGQ_PARAMETER_8822B */

/* 2 REG_NOT_VALID_8822B */
#define BIT_MAC_STOP_CPUMGQ_8822B BIT(16)

#define BIT_SHIFT_CW_8822B 8
#define BIT_MASK_CW_8822B 0xff
#define BIT_CW_8822B(x) (((x) & BIT_MASK_CW_8822B) << BIT_SHIFT_CW_8822B)
#define BITS_CW_8822B (BIT_MASK_CW_8822B << BIT_SHIFT_CW_8822B)
#define BIT_CLEAR_CW_8822B(x) ((x) & (~BITS_CW_8822B))
#define BIT_GET_CW_8822B(x) (((x) >> BIT_SHIFT_CW_8822B) & BIT_MASK_CW_8822B)
#define BIT_SET_CW_8822B(x, v) (BIT_CLEAR_CW_8822B(x) | BIT_CW_8822B(v))

#define BIT_SHIFT_AIFS_8822B 0
#define BIT_MASK_AIFS_8822B 0xff
#define BIT_AIFS_8822B(x) (((x) & BIT_MASK_AIFS_8822B) << BIT_SHIFT_AIFS_8822B)
#define BITS_AIFS_8822B (BIT_MASK_AIFS_8822B << BIT_SHIFT_AIFS_8822B)
#define BIT_CLEAR_AIFS_8822B(x) ((x) & (~BITS_AIFS_8822B))
#define BIT_GET_AIFS_8822B(x)                                                  \
	(((x) >> BIT_SHIFT_AIFS_8822B) & BIT_MASK_AIFS_8822B)
#define BIT_SET_AIFS_8822B(x, v) (BIT_CLEAR_AIFS_8822B(x) | BIT_AIFS_8822B(v))

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_BWOPMODE_8822B (BW OPERATION MODE REGISTER) */

/* 2 REG_WMAC_FWPKT_CR_8822B */
#define BIT_FWEN_8822B BIT(7)
#define BIT_PHYSTS_PKT_CTRL_8822B BIT(6)
#define BIT_APPHDR_MIDSRCH_FAIL_8822B BIT(4)
#define BIT_FWPARSING_EN_8822B BIT(3)

#define BIT_SHIFT_APPEND_MHDR_LEN_8822B 0
#define BIT_MASK_APPEND_MHDR_LEN_8822B 0x7
#define BIT_APPEND_MHDR_LEN_8822B(x)                                           \
	(((x) & BIT_MASK_APPEND_MHDR_LEN_8822B)                                \
	 << BIT_SHIFT_APPEND_MHDR_LEN_8822B)
#define BITS_APPEND_MHDR_LEN_8822B                                             \
	(BIT_MASK_APPEND_MHDR_LEN_8822B << BIT_SHIFT_APPEND_MHDR_LEN_8822B)
#define BIT_CLEAR_APPEND_MHDR_LEN_8822B(x) ((x) & (~BITS_APPEND_MHDR_LEN_8822B))
#define BIT_GET_APPEND_MHDR_LEN_8822B(x)                                       \
	(((x) >> BIT_SHIFT_APPEND_MHDR_LEN_8822B) &                            \
	 BIT_MASK_APPEND_MHDR_LEN_8822B)
#define BIT_SET_APPEND_MHDR_LEN_8822B(x, v)                                    \
	(BIT_CLEAR_APPEND_MHDR_LEN_8822B(x) | BIT_APPEND_MHDR_LEN_8822B(v))

/* 2 REG_WMAC_CR_8822B (WMAC CR AND APSD CONTROL REGISTER) */
#define BIT_IC_MACPHY_M_8822B BIT(0)

/* 2 REG_TCR_8822B (TRANSMISSION CONFIGURATION REGISTER) */
#define BIT_WMAC_EN_RTS_ADDR_8822B BIT(31)
#define BIT_WMAC_DISABLE_CCK_8822B BIT(30)
#define BIT_WMAC_RAW_LEN_8822B BIT(29)
#define BIT_WMAC_NOTX_IN_RXNDP_8822B BIT(28)
#define BIT_WMAC_EN_EOF_8822B BIT(27)
#define BIT_WMAC_BF_SEL_8822B BIT(26)
#define BIT_WMAC_ANTMODE_SEL_8822B BIT(25)
#define BIT_WMAC_TCRPWRMGT_HWCTL_8822B BIT(24)
#define BIT_WMAC_SMOOTH_VAL_8822B BIT(23)
#define BIT_FETCH_MPDU_AFTER_WSEC_RDY_8822B BIT(20)
#define BIT_WMAC_TCR_EN_20MST_8822B BIT(19)
#define BIT_WMAC_DIS_SIGTA_8822B BIT(18)
#define BIT_WMAC_DIS_A2B0_8822B BIT(17)
#define BIT_WMAC_MSK_SIGBCRC_8822B BIT(16)
#define BIT_WMAC_TCR_ERRSTEN_3_8822B BIT(15)
#define BIT_WMAC_TCR_ERRSTEN_2_8822B BIT(14)
#define BIT_WMAC_TCR_ERRSTEN_1_8822B BIT(13)
#define BIT_WMAC_TCR_ERRSTEN_0_8822B BIT(12)
#define BIT_WMAC_TCR_TXSK_PERPKT_8822B BIT(11)
#define BIT_ICV_8822B BIT(10)
#define BIT_CFEND_FORMAT_8822B BIT(9)
#define BIT_CRC_8822B BIT(8)
#define BIT_PWRBIT_OW_EN_8822B BIT(7)
#define BIT_PWR_ST_8822B BIT(6)
#define BIT_WMAC_TCR_UPD_TIMIE_8822B BIT(5)
#define BIT_WMAC_TCR_UPD_HGQMD_8822B BIT(4)
#define BIT_VHTSIGA1_TXPS_8822B BIT(3)
#define BIT_PAD_SEL_8822B BIT(2)
#define BIT_DIS_GCLK_8822B BIT(1)

/* 2 REG_RCR_8822B (RECEIVE CONFIGURATION REGISTER) */
#define BIT_APP_FCS_8822B BIT(31)
#define BIT_APP_MIC_8822B BIT(30)
#define BIT_APP_ICV_8822B BIT(29)
#define BIT_APP_PHYSTS_8822B BIT(28)
#define BIT_APP_BASSN_8822B BIT(27)
#define BIT_VHT_DACK_8822B BIT(26)
#define BIT_TCPOFLD_EN_8822B BIT(25)
#define BIT_ENMBID_8822B BIT(24)
#define BIT_LSIGEN_8822B BIT(23)
#define BIT_MFBEN_8822B BIT(22)
#define BIT_DISCHKPPDLLEN_8822B BIT(21)
#define BIT_PKTCTL_DLEN_8822B BIT(20)
#define BIT_TIM_PARSER_EN_8822B BIT(18)
#define BIT_BC_MD_EN_8822B BIT(17)
#define BIT_UC_MD_EN_8822B BIT(16)
#define BIT_RXSK_PERPKT_8822B BIT(15)
#define BIT_HTC_LOC_CTRL_8822B BIT(14)
#define BIT_RPFM_CAM_ENABLE_8822B BIT(12)
#define BIT_TA_BCN_8822B BIT(11)
#define BIT_DISDECMYPKT_8822B BIT(10)
#define BIT_AICV_8822B BIT(9)
#define BIT_ACRC32_8822B BIT(8)
#define BIT_CBSSID_BCN_8822B BIT(7)
#define BIT_CBSSID_DATA_8822B BIT(6)
#define BIT_APWRMGT_8822B BIT(5)
#define BIT_ADD3_8822B BIT(4)
#define BIT_AB_8822B BIT(3)
#define BIT_AM_8822B BIT(2)
#define BIT_APM_8822B BIT(1)
#define BIT_AAP_8822B BIT(0)

/* 2 REG_RX_DRVINFO_SZ_8822B (RX DRIVER INFO SIZE REGISTER) */
#define BIT_PHYSTS_PER_PKT_MODE_8822B BIT(7)

#define BIT_SHIFT_DRVINFO_SZ_V1_8822B 0
#define BIT_MASK_DRVINFO_SZ_V1_8822B 0xf
#define BIT_DRVINFO_SZ_V1_8822B(x)                                             \
	(((x) & BIT_MASK_DRVINFO_SZ_V1_8822B) << BIT_SHIFT_DRVINFO_SZ_V1_8822B)
#define BITS_DRVINFO_SZ_V1_8822B                                               \
	(BIT_MASK_DRVINFO_SZ_V1_8822B << BIT_SHIFT_DRVINFO_SZ_V1_8822B)
#define BIT_CLEAR_DRVINFO_SZ_V1_8822B(x) ((x) & (~BITS_DRVINFO_SZ_V1_8822B))
#define BIT_GET_DRVINFO_SZ_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_DRVINFO_SZ_V1_8822B) & BIT_MASK_DRVINFO_SZ_V1_8822B)
#define BIT_SET_DRVINFO_SZ_V1_8822B(x, v)                                      \
	(BIT_CLEAR_DRVINFO_SZ_V1_8822B(x) | BIT_DRVINFO_SZ_V1_8822B(v))

/* 2 REG_RX_DLK_TIME_8822B (RX DEADLOCK TIME REGISTER) */

#define BIT_SHIFT_RX_DLK_TIME_8822B 0
#define BIT_MASK_RX_DLK_TIME_8822B 0xff
#define BIT_RX_DLK_TIME_8822B(x)                                               \
	(((x) & BIT_MASK_RX_DLK_TIME_8822B) << BIT_SHIFT_RX_DLK_TIME_8822B)
#define BITS_RX_DLK_TIME_8822B                                                 \
	(BIT_MASK_RX_DLK_TIME_8822B << BIT_SHIFT_RX_DLK_TIME_8822B)
#define BIT_CLEAR_RX_DLK_TIME_8822B(x) ((x) & (~BITS_RX_DLK_TIME_8822B))
#define BIT_GET_RX_DLK_TIME_8822B(x)                                           \
	(((x) >> BIT_SHIFT_RX_DLK_TIME_8822B) & BIT_MASK_RX_DLK_TIME_8822B)
#define BIT_SET_RX_DLK_TIME_8822B(x, v)                                        \
	(BIT_CLEAR_RX_DLK_TIME_8822B(x) | BIT_RX_DLK_TIME_8822B(v))

/* 2 REG_RX_PKT_LIMIT_8822B (RX PACKET LENGTH LIMIT REGISTER) */

#define BIT_SHIFT_RXPKTLMT_8822B 0
#define BIT_MASK_RXPKTLMT_8822B 0x3f
#define BIT_RXPKTLMT_8822B(x)                                                  \
	(((x) & BIT_MASK_RXPKTLMT_8822B) << BIT_SHIFT_RXPKTLMT_8822B)
#define BITS_RXPKTLMT_8822B                                                    \
	(BIT_MASK_RXPKTLMT_8822B << BIT_SHIFT_RXPKTLMT_8822B)
#define BIT_CLEAR_RXPKTLMT_8822B(x) ((x) & (~BITS_RXPKTLMT_8822B))
#define BIT_GET_RXPKTLMT_8822B(x)                                              \
	(((x) >> BIT_SHIFT_RXPKTLMT_8822B) & BIT_MASK_RXPKTLMT_8822B)
#define BIT_SET_RXPKTLMT_8822B(x, v)                                           \
	(BIT_CLEAR_RXPKTLMT_8822B(x) | BIT_RXPKTLMT_8822B(v))

/* 2 REG_MACID_8822B (MAC ID REGISTER) */

#define BIT_SHIFT_MACID_8822B 0
#define BIT_MASK_MACID_8822B 0xffffffffffffL
#define BIT_MACID_8822B(x)                                                     \
	(((x) & BIT_MASK_MACID_8822B) << BIT_SHIFT_MACID_8822B)
#define BITS_MACID_8822B (BIT_MASK_MACID_8822B << BIT_SHIFT_MACID_8822B)
#define BIT_CLEAR_MACID_8822B(x) ((x) & (~BITS_MACID_8822B))
#define BIT_GET_MACID_8822B(x)                                                 \
	(((x) >> BIT_SHIFT_MACID_8822B) & BIT_MASK_MACID_8822B)
#define BIT_SET_MACID_8822B(x, v)                                              \
	(BIT_CLEAR_MACID_8822B(x) | BIT_MACID_8822B(v))

/* 2 REG_BSSID_8822B (BSSID REGISTER) */

#define BIT_SHIFT_BSSID_8822B 0
#define BIT_MASK_BSSID_8822B 0xffffffffffffL
#define BIT_BSSID_8822B(x)                                                     \
	(((x) & BIT_MASK_BSSID_8822B) << BIT_SHIFT_BSSID_8822B)
#define BITS_BSSID_8822B (BIT_MASK_BSSID_8822B << BIT_SHIFT_BSSID_8822B)
#define BIT_CLEAR_BSSID_8822B(x) ((x) & (~BITS_BSSID_8822B))
#define BIT_GET_BSSID_8822B(x)                                                 \
	(((x) >> BIT_SHIFT_BSSID_8822B) & BIT_MASK_BSSID_8822B)
#define BIT_SET_BSSID_8822B(x, v)                                              \
	(BIT_CLEAR_BSSID_8822B(x) | BIT_BSSID_8822B(v))

/* 2 REG_MAR_8822B (MULTICAST ADDRESS REGISTER) */

#define BIT_SHIFT_MAR_8822B 0
#define BIT_MASK_MAR_8822B 0xffffffffffffffffL
#define BIT_MAR_8822B(x) (((x) & BIT_MASK_MAR_8822B) << BIT_SHIFT_MAR_8822B)
#define BITS_MAR_8822B (BIT_MASK_MAR_8822B << BIT_SHIFT_MAR_8822B)
#define BIT_CLEAR_MAR_8822B(x) ((x) & (~BITS_MAR_8822B))
#define BIT_GET_MAR_8822B(x) (((x) >> BIT_SHIFT_MAR_8822B) & BIT_MASK_MAR_8822B)
#define BIT_SET_MAR_8822B(x, v) (BIT_CLEAR_MAR_8822B(x) | BIT_MAR_8822B(v))

/* 2 REG_MBIDCAMCFG_1_8822B (MBSSID CAM CONFIGURATION REGISTER) */

#define BIT_SHIFT_MBIDCAM_RWDATA_L_8822B 0
#define BIT_MASK_MBIDCAM_RWDATA_L_8822B 0xffffffffL
#define BIT_MBIDCAM_RWDATA_L_8822B(x)                                          \
	(((x) & BIT_MASK_MBIDCAM_RWDATA_L_8822B)                               \
	 << BIT_SHIFT_MBIDCAM_RWDATA_L_8822B)
#define BITS_MBIDCAM_RWDATA_L_8822B                                            \
	(BIT_MASK_MBIDCAM_RWDATA_L_8822B << BIT_SHIFT_MBIDCAM_RWDATA_L_8822B)
#define BIT_CLEAR_MBIDCAM_RWDATA_L_8822B(x)                                    \
	((x) & (~BITS_MBIDCAM_RWDATA_L_8822B))
#define BIT_GET_MBIDCAM_RWDATA_L_8822B(x)                                      \
	(((x) >> BIT_SHIFT_MBIDCAM_RWDATA_L_8822B) &                           \
	 BIT_MASK_MBIDCAM_RWDATA_L_8822B)
#define BIT_SET_MBIDCAM_RWDATA_L_8822B(x, v)                                   \
	(BIT_CLEAR_MBIDCAM_RWDATA_L_8822B(x) | BIT_MBIDCAM_RWDATA_L_8822B(v))

/* 2 REG_MBIDCAMCFG_2_8822B (MBSSID CAM CONFIGURATION REGISTER) */
#define BIT_MBIDCAM_POLL_8822B BIT(31)
#define BIT_MBIDCAM_WT_EN_8822B BIT(30)

#define BIT_SHIFT_MBIDCAM_ADDR_8822B 24
#define BIT_MASK_MBIDCAM_ADDR_8822B 0x1f
#define BIT_MBIDCAM_ADDR_8822B(x)                                              \
	(((x) & BIT_MASK_MBIDCAM_ADDR_8822B) << BIT_SHIFT_MBIDCAM_ADDR_8822B)
#define BITS_MBIDCAM_ADDR_8822B                                                \
	(BIT_MASK_MBIDCAM_ADDR_8822B << BIT_SHIFT_MBIDCAM_ADDR_8822B)
#define BIT_CLEAR_MBIDCAM_ADDR_8822B(x) ((x) & (~BITS_MBIDCAM_ADDR_8822B))
#define BIT_GET_MBIDCAM_ADDR_8822B(x)                                          \
	(((x) >> BIT_SHIFT_MBIDCAM_ADDR_8822B) & BIT_MASK_MBIDCAM_ADDR_8822B)
#define BIT_SET_MBIDCAM_ADDR_8822B(x, v)                                       \
	(BIT_CLEAR_MBIDCAM_ADDR_8822B(x) | BIT_MBIDCAM_ADDR_8822B(v))

#define BIT_MBIDCAM_VALID_8822B BIT(23)
#define BIT_LSIC_TXOP_EN_8822B BIT(17)
#define BIT_CTS_EN_8822B BIT(16)

#define BIT_SHIFT_MBIDCAM_RWDATA_H_8822B 0
#define BIT_MASK_MBIDCAM_RWDATA_H_8822B 0xffff
#define BIT_MBIDCAM_RWDATA_H_8822B(x)                                          \
	(((x) & BIT_MASK_MBIDCAM_RWDATA_H_8822B)                               \
	 << BIT_SHIFT_MBIDCAM_RWDATA_H_8822B)
#define BITS_MBIDCAM_RWDATA_H_8822B                                            \
	(BIT_MASK_MBIDCAM_RWDATA_H_8822B << BIT_SHIFT_MBIDCAM_RWDATA_H_8822B)
#define BIT_CLEAR_MBIDCAM_RWDATA_H_8822B(x)                                    \
	((x) & (~BITS_MBIDCAM_RWDATA_H_8822B))
#define BIT_GET_MBIDCAM_RWDATA_H_8822B(x)                                      \
	(((x) >> BIT_SHIFT_MBIDCAM_RWDATA_H_8822B) &                           \
	 BIT_MASK_MBIDCAM_RWDATA_H_8822B)
#define BIT_SET_MBIDCAM_RWDATA_H_8822B(x, v)                                   \
	(BIT_CLEAR_MBIDCAM_RWDATA_H_8822B(x) | BIT_MBIDCAM_RWDATA_H_8822B(v))

/* 2 REG_ZLD_NUM_8822B */

#define BIT_SHIFT_ZLD_NUM_8822B 0
#define BIT_MASK_ZLD_NUM_8822B 0xff
#define BIT_ZLD_NUM_8822B(x)                                                   \
	(((x) & BIT_MASK_ZLD_NUM_8822B) << BIT_SHIFT_ZLD_NUM_8822B)
#define BITS_ZLD_NUM_8822B (BIT_MASK_ZLD_NUM_8822B << BIT_SHIFT_ZLD_NUM_8822B)
#define BIT_CLEAR_ZLD_NUM_8822B(x) ((x) & (~BITS_ZLD_NUM_8822B))
#define BIT_GET_ZLD_NUM_8822B(x)                                               \
	(((x) >> BIT_SHIFT_ZLD_NUM_8822B) & BIT_MASK_ZLD_NUM_8822B)
#define BIT_SET_ZLD_NUM_8822B(x, v)                                            \
	(BIT_CLEAR_ZLD_NUM_8822B(x) | BIT_ZLD_NUM_8822B(v))

/* 2 REG_UDF_THSD_8822B */

#define BIT_SHIFT_UDF_THSD_8822B 0
#define BIT_MASK_UDF_THSD_8822B 0xff
#define BIT_UDF_THSD_8822B(x)                                                  \
	(((x) & BIT_MASK_UDF_THSD_8822B) << BIT_SHIFT_UDF_THSD_8822B)
#define BITS_UDF_THSD_8822B                                                    \
	(BIT_MASK_UDF_THSD_8822B << BIT_SHIFT_UDF_THSD_8822B)
#define BIT_CLEAR_UDF_THSD_8822B(x) ((x) & (~BITS_UDF_THSD_8822B))
#define BIT_GET_UDF_THSD_8822B(x)                                              \
	(((x) >> BIT_SHIFT_UDF_THSD_8822B) & BIT_MASK_UDF_THSD_8822B)
#define BIT_SET_UDF_THSD_8822B(x, v)                                           \
	(BIT_CLEAR_UDF_THSD_8822B(x) | BIT_UDF_THSD_8822B(v))

/* 2 REG_WMAC_TCR_TSFT_OFS_8822B */

#define BIT_SHIFT_WMAC_TCR_TSFT_OFS_8822B 0
#define BIT_MASK_WMAC_TCR_TSFT_OFS_8822B 0xffff
#define BIT_WMAC_TCR_TSFT_OFS_8822B(x)                                         \
	(((x) & BIT_MASK_WMAC_TCR_TSFT_OFS_8822B)                              \
	 << BIT_SHIFT_WMAC_TCR_TSFT_OFS_8822B)
#define BITS_WMAC_TCR_TSFT_OFS_8822B                                           \
	(BIT_MASK_WMAC_TCR_TSFT_OFS_8822B << BIT_SHIFT_WMAC_TCR_TSFT_OFS_8822B)
#define BIT_CLEAR_WMAC_TCR_TSFT_OFS_8822B(x)                                   \
	((x) & (~BITS_WMAC_TCR_TSFT_OFS_8822B))
#define BIT_GET_WMAC_TCR_TSFT_OFS_8822B(x)                                     \
	(((x) >> BIT_SHIFT_WMAC_TCR_TSFT_OFS_8822B) &                          \
	 BIT_MASK_WMAC_TCR_TSFT_OFS_8822B)
#define BIT_SET_WMAC_TCR_TSFT_OFS_8822B(x, v)                                  \
	(BIT_CLEAR_WMAC_TCR_TSFT_OFS_8822B(x) | BIT_WMAC_TCR_TSFT_OFS_8822B(v))

/* 2 REG_MCU_TEST_2_V1_8822B */

#define BIT_SHIFT_MCU_RSVD_2_V1_8822B 0
#define BIT_MASK_MCU_RSVD_2_V1_8822B 0xffff
#define BIT_MCU_RSVD_2_V1_8822B(x)                                             \
	(((x) & BIT_MASK_MCU_RSVD_2_V1_8822B) << BIT_SHIFT_MCU_RSVD_2_V1_8822B)
#define BITS_MCU_RSVD_2_V1_8822B                                               \
	(BIT_MASK_MCU_RSVD_2_V1_8822B << BIT_SHIFT_MCU_RSVD_2_V1_8822B)
#define BIT_CLEAR_MCU_RSVD_2_V1_8822B(x) ((x) & (~BITS_MCU_RSVD_2_V1_8822B))
#define BIT_GET_MCU_RSVD_2_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_MCU_RSVD_2_V1_8822B) & BIT_MASK_MCU_RSVD_2_V1_8822B)
#define BIT_SET_MCU_RSVD_2_V1_8822B(x, v)                                      \
	(BIT_CLEAR_MCU_RSVD_2_V1_8822B(x) | BIT_MCU_RSVD_2_V1_8822B(v))

/* 2 REG_WMAC_TXTIMEOUT_8822B */

#define BIT_SHIFT_WMAC_TXTIMEOUT_8822B 0
#define BIT_MASK_WMAC_TXTIMEOUT_8822B 0xff
#define BIT_WMAC_TXTIMEOUT_8822B(x)                                            \
	(((x) & BIT_MASK_WMAC_TXTIMEOUT_8822B)                                 \
	 << BIT_SHIFT_WMAC_TXTIMEOUT_8822B)
#define BITS_WMAC_TXTIMEOUT_8822B                                              \
	(BIT_MASK_WMAC_TXTIMEOUT_8822B << BIT_SHIFT_WMAC_TXTIMEOUT_8822B)
#define BIT_CLEAR_WMAC_TXTIMEOUT_8822B(x) ((x) & (~BITS_WMAC_TXTIMEOUT_8822B))
#define BIT_GET_WMAC_TXTIMEOUT_8822B(x)                                        \
	(((x) >> BIT_SHIFT_WMAC_TXTIMEOUT_8822B) &                             \
	 BIT_MASK_WMAC_TXTIMEOUT_8822B)
#define BIT_SET_WMAC_TXTIMEOUT_8822B(x, v)                                     \
	(BIT_CLEAR_WMAC_TXTIMEOUT_8822B(x) | BIT_WMAC_TXTIMEOUT_8822B(v))

/* 2 REG_STMP_THSD_8822B */

#define BIT_SHIFT_STMP_THSD_8822B 0
#define BIT_MASK_STMP_THSD_8822B 0xff
#define BIT_STMP_THSD_8822B(x)                                                 \
	(((x) & BIT_MASK_STMP_THSD_8822B) << BIT_SHIFT_STMP_THSD_8822B)
#define BITS_STMP_THSD_8822B                                                   \
	(BIT_MASK_STMP_THSD_8822B << BIT_SHIFT_STMP_THSD_8822B)
#define BIT_CLEAR_STMP_THSD_8822B(x) ((x) & (~BITS_STMP_THSD_8822B))
#define BIT_GET_STMP_THSD_8822B(x)                                             \
	(((x) >> BIT_SHIFT_STMP_THSD_8822B) & BIT_MASK_STMP_THSD_8822B)
#define BIT_SET_STMP_THSD_8822B(x, v)                                          \
	(BIT_CLEAR_STMP_THSD_8822B(x) | BIT_STMP_THSD_8822B(v))

/* 2 REG_MAC_SPEC_SIFS_8822B (SPECIFICATION SIFS REGISTER) */

#define BIT_SHIFT_SPEC_SIFS_OFDM_8822B 8
#define BIT_MASK_SPEC_SIFS_OFDM_8822B 0xff
#define BIT_SPEC_SIFS_OFDM_8822B(x)                                            \
	(((x) & BIT_MASK_SPEC_SIFS_OFDM_8822B)                                 \
	 << BIT_SHIFT_SPEC_SIFS_OFDM_8822B)
#define BITS_SPEC_SIFS_OFDM_8822B                                              \
	(BIT_MASK_SPEC_SIFS_OFDM_8822B << BIT_SHIFT_SPEC_SIFS_OFDM_8822B)
#define BIT_CLEAR_SPEC_SIFS_OFDM_8822B(x) ((x) & (~BITS_SPEC_SIFS_OFDM_8822B))
#define BIT_GET_SPEC_SIFS_OFDM_8822B(x)                                        \
	(((x) >> BIT_SHIFT_SPEC_SIFS_OFDM_8822B) &                             \
	 BIT_MASK_SPEC_SIFS_OFDM_8822B)
#define BIT_SET_SPEC_SIFS_OFDM_8822B(x, v)                                     \
	(BIT_CLEAR_SPEC_SIFS_OFDM_8822B(x) | BIT_SPEC_SIFS_OFDM_8822B(v))

#define BIT_SHIFT_SPEC_SIFS_CCK_8822B 0
#define BIT_MASK_SPEC_SIFS_CCK_8822B 0xff
#define BIT_SPEC_SIFS_CCK_8822B(x)                                             \
	(((x) & BIT_MASK_SPEC_SIFS_CCK_8822B) << BIT_SHIFT_SPEC_SIFS_CCK_8822B)
#define BITS_SPEC_SIFS_CCK_8822B                                               \
	(BIT_MASK_SPEC_SIFS_CCK_8822B << BIT_SHIFT_SPEC_SIFS_CCK_8822B)
#define BIT_CLEAR_SPEC_SIFS_CCK_8822B(x) ((x) & (~BITS_SPEC_SIFS_CCK_8822B))
#define BIT_GET_SPEC_SIFS_CCK_8822B(x)                                         \
	(((x) >> BIT_SHIFT_SPEC_SIFS_CCK_8822B) & BIT_MASK_SPEC_SIFS_CCK_8822B)
#define BIT_SET_SPEC_SIFS_CCK_8822B(x, v)                                      \
	(BIT_CLEAR_SPEC_SIFS_CCK_8822B(x) | BIT_SPEC_SIFS_CCK_8822B(v))

/* 2 REG_USTIME_EDCA_8822B (US TIME TUNING FOR EDCA REGISTER) */

#define BIT_SHIFT_USTIME_EDCA_V1_8822B 0
#define BIT_MASK_USTIME_EDCA_V1_8822B 0x1ff
#define BIT_USTIME_EDCA_V1_8822B(x)                                            \
	(((x) & BIT_MASK_USTIME_EDCA_V1_8822B)                                 \
	 << BIT_SHIFT_USTIME_EDCA_V1_8822B)
#define BITS_USTIME_EDCA_V1_8822B                                              \
	(BIT_MASK_USTIME_EDCA_V1_8822B << BIT_SHIFT_USTIME_EDCA_V1_8822B)
#define BIT_CLEAR_USTIME_EDCA_V1_8822B(x) ((x) & (~BITS_USTIME_EDCA_V1_8822B))
#define BIT_GET_USTIME_EDCA_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_USTIME_EDCA_V1_8822B) &                             \
	 BIT_MASK_USTIME_EDCA_V1_8822B)
#define BIT_SET_USTIME_EDCA_V1_8822B(x, v)                                     \
	(BIT_CLEAR_USTIME_EDCA_V1_8822B(x) | BIT_USTIME_EDCA_V1_8822B(v))

/* 2 REG_RESP_SIFS_OFDM_8822B (RESPONSE SIFS FOR OFDM REGISTER) */

#define BIT_SHIFT_SIFS_R2T_OFDM_8822B 8
#define BIT_MASK_SIFS_R2T_OFDM_8822B 0xff
#define BIT_SIFS_R2T_OFDM_8822B(x)                                             \
	(((x) & BIT_MASK_SIFS_R2T_OFDM_8822B) << BIT_SHIFT_SIFS_R2T_OFDM_8822B)
#define BITS_SIFS_R2T_OFDM_8822B                                               \
	(BIT_MASK_SIFS_R2T_OFDM_8822B << BIT_SHIFT_SIFS_R2T_OFDM_8822B)
#define BIT_CLEAR_SIFS_R2T_OFDM_8822B(x) ((x) & (~BITS_SIFS_R2T_OFDM_8822B))
#define BIT_GET_SIFS_R2T_OFDM_8822B(x)                                         \
	(((x) >> BIT_SHIFT_SIFS_R2T_OFDM_8822B) & BIT_MASK_SIFS_R2T_OFDM_8822B)
#define BIT_SET_SIFS_R2T_OFDM_8822B(x, v)                                      \
	(BIT_CLEAR_SIFS_R2T_OFDM_8822B(x) | BIT_SIFS_R2T_OFDM_8822B(v))

#define BIT_SHIFT_SIFS_T2T_OFDM_8822B 0
#define BIT_MASK_SIFS_T2T_OFDM_8822B 0xff
#define BIT_SIFS_T2T_OFDM_8822B(x)                                             \
	(((x) & BIT_MASK_SIFS_T2T_OFDM_8822B) << BIT_SHIFT_SIFS_T2T_OFDM_8822B)
#define BITS_SIFS_T2T_OFDM_8822B                                               \
	(BIT_MASK_SIFS_T2T_OFDM_8822B << BIT_SHIFT_SIFS_T2T_OFDM_8822B)
#define BIT_CLEAR_SIFS_T2T_OFDM_8822B(x) ((x) & (~BITS_SIFS_T2T_OFDM_8822B))
#define BIT_GET_SIFS_T2T_OFDM_8822B(x)                                         \
	(((x) >> BIT_SHIFT_SIFS_T2T_OFDM_8822B) & BIT_MASK_SIFS_T2T_OFDM_8822B)
#define BIT_SET_SIFS_T2T_OFDM_8822B(x, v)                                      \
	(BIT_CLEAR_SIFS_T2T_OFDM_8822B(x) | BIT_SIFS_T2T_OFDM_8822B(v))

/* 2 REG_RESP_SIFS_CCK_8822B (RESPONSE SIFS FOR CCK REGISTER) */

#define BIT_SHIFT_SIFS_R2T_CCK_8822B 8
#define BIT_MASK_SIFS_R2T_CCK_8822B 0xff
#define BIT_SIFS_R2T_CCK_8822B(x)                                              \
	(((x) & BIT_MASK_SIFS_R2T_CCK_8822B) << BIT_SHIFT_SIFS_R2T_CCK_8822B)
#define BITS_SIFS_R2T_CCK_8822B                                                \
	(BIT_MASK_SIFS_R2T_CCK_8822B << BIT_SHIFT_SIFS_R2T_CCK_8822B)
#define BIT_CLEAR_SIFS_R2T_CCK_8822B(x) ((x) & (~BITS_SIFS_R2T_CCK_8822B))
#define BIT_GET_SIFS_R2T_CCK_8822B(x)                                          \
	(((x) >> BIT_SHIFT_SIFS_R2T_CCK_8822B) & BIT_MASK_SIFS_R2T_CCK_8822B)
#define BIT_SET_SIFS_R2T_CCK_8822B(x, v)                                       \
	(BIT_CLEAR_SIFS_R2T_CCK_8822B(x) | BIT_SIFS_R2T_CCK_8822B(v))

#define BIT_SHIFT_SIFS_T2T_CCK_8822B 0
#define BIT_MASK_SIFS_T2T_CCK_8822B 0xff
#define BIT_SIFS_T2T_CCK_8822B(x)                                              \
	(((x) & BIT_MASK_SIFS_T2T_CCK_8822B) << BIT_SHIFT_SIFS_T2T_CCK_8822B)
#define BITS_SIFS_T2T_CCK_8822B                                                \
	(BIT_MASK_SIFS_T2T_CCK_8822B << BIT_SHIFT_SIFS_T2T_CCK_8822B)
#define BIT_CLEAR_SIFS_T2T_CCK_8822B(x) ((x) & (~BITS_SIFS_T2T_CCK_8822B))
#define BIT_GET_SIFS_T2T_CCK_8822B(x)                                          \
	(((x) >> BIT_SHIFT_SIFS_T2T_CCK_8822B) & BIT_MASK_SIFS_T2T_CCK_8822B)
#define BIT_SET_SIFS_T2T_CCK_8822B(x, v)                                       \
	(BIT_CLEAR_SIFS_T2T_CCK_8822B(x) | BIT_SIFS_T2T_CCK_8822B(v))

/* 2 REG_EIFS_8822B (EIFS REGISTER) */

#define BIT_SHIFT_EIFS_8822B 0
#define BIT_MASK_EIFS_8822B 0xffff
#define BIT_EIFS_8822B(x) (((x) & BIT_MASK_EIFS_8822B) << BIT_SHIFT_EIFS_8822B)
#define BITS_EIFS_8822B (BIT_MASK_EIFS_8822B << BIT_SHIFT_EIFS_8822B)
#define BIT_CLEAR_EIFS_8822B(x) ((x) & (~BITS_EIFS_8822B))
#define BIT_GET_EIFS_8822B(x)                                                  \
	(((x) >> BIT_SHIFT_EIFS_8822B) & BIT_MASK_EIFS_8822B)
#define BIT_SET_EIFS_8822B(x, v) (BIT_CLEAR_EIFS_8822B(x) | BIT_EIFS_8822B(v))

/* 2 REG_CTS2TO_8822B (CTS2 TIMEOUT REGISTER) */

#define BIT_SHIFT_CTS2TO_8822B 0
#define BIT_MASK_CTS2TO_8822B 0xff
#define BIT_CTS2TO_8822B(x)                                                    \
	(((x) & BIT_MASK_CTS2TO_8822B) << BIT_SHIFT_CTS2TO_8822B)
#define BITS_CTS2TO_8822B (BIT_MASK_CTS2TO_8822B << BIT_SHIFT_CTS2TO_8822B)
#define BIT_CLEAR_CTS2TO_8822B(x) ((x) & (~BITS_CTS2TO_8822B))
#define BIT_GET_CTS2TO_8822B(x)                                                \
	(((x) >> BIT_SHIFT_CTS2TO_8822B) & BIT_MASK_CTS2TO_8822B)
#define BIT_SET_CTS2TO_8822B(x, v)                                             \
	(BIT_CLEAR_CTS2TO_8822B(x) | BIT_CTS2TO_8822B(v))

/* 2 REG_ACKTO_8822B (ACK TIMEOUT REGISTER) */

#define BIT_SHIFT_ACKTO_8822B 0
#define BIT_MASK_ACKTO_8822B 0xff
#define BIT_ACKTO_8822B(x)                                                     \
	(((x) & BIT_MASK_ACKTO_8822B) << BIT_SHIFT_ACKTO_8822B)
#define BITS_ACKTO_8822B (BIT_MASK_ACKTO_8822B << BIT_SHIFT_ACKTO_8822B)
#define BIT_CLEAR_ACKTO_8822B(x) ((x) & (~BITS_ACKTO_8822B))
#define BIT_GET_ACKTO_8822B(x)                                                 \
	(((x) >> BIT_SHIFT_ACKTO_8822B) & BIT_MASK_ACKTO_8822B)
#define BIT_SET_ACKTO_8822B(x, v)                                              \
	(BIT_CLEAR_ACKTO_8822B(x) | BIT_ACKTO_8822B(v))

/* 2 REG_NAV_CTRL_8822B (NAV CONTROL REGISTER) */

#define BIT_SHIFT_NAV_UPPER_8822B 16
#define BIT_MASK_NAV_UPPER_8822B 0xff
#define BIT_NAV_UPPER_8822B(x)                                                 \
	(((x) & BIT_MASK_NAV_UPPER_8822B) << BIT_SHIFT_NAV_UPPER_8822B)
#define BITS_NAV_UPPER_8822B                                                   \
	(BIT_MASK_NAV_UPPER_8822B << BIT_SHIFT_NAV_UPPER_8822B)
#define BIT_CLEAR_NAV_UPPER_8822B(x) ((x) & (~BITS_NAV_UPPER_8822B))
#define BIT_GET_NAV_UPPER_8822B(x)                                             \
	(((x) >> BIT_SHIFT_NAV_UPPER_8822B) & BIT_MASK_NAV_UPPER_8822B)
#define BIT_SET_NAV_UPPER_8822B(x, v)                                          \
	(BIT_CLEAR_NAV_UPPER_8822B(x) | BIT_NAV_UPPER_8822B(v))

#define BIT_SHIFT_RXMYRTS_NAV_8822B 8
#define BIT_MASK_RXMYRTS_NAV_8822B 0xf
#define BIT_RXMYRTS_NAV_8822B(x)                                               \
	(((x) & BIT_MASK_RXMYRTS_NAV_8822B) << BIT_SHIFT_RXMYRTS_NAV_8822B)
#define BITS_RXMYRTS_NAV_8822B                                                 \
	(BIT_MASK_RXMYRTS_NAV_8822B << BIT_SHIFT_RXMYRTS_NAV_8822B)
#define BIT_CLEAR_RXMYRTS_NAV_8822B(x) ((x) & (~BITS_RXMYRTS_NAV_8822B))
#define BIT_GET_RXMYRTS_NAV_8822B(x)                                           \
	(((x) >> BIT_SHIFT_RXMYRTS_NAV_8822B) & BIT_MASK_RXMYRTS_NAV_8822B)
#define BIT_SET_RXMYRTS_NAV_8822B(x, v)                                        \
	(BIT_CLEAR_RXMYRTS_NAV_8822B(x) | BIT_RXMYRTS_NAV_8822B(v))

#define BIT_SHIFT_RTSRST_8822B 0
#define BIT_MASK_RTSRST_8822B 0xff
#define BIT_RTSRST_8822B(x)                                                    \
	(((x) & BIT_MASK_RTSRST_8822B) << BIT_SHIFT_RTSRST_8822B)
#define BITS_RTSRST_8822B (BIT_MASK_RTSRST_8822B << BIT_SHIFT_RTSRST_8822B)
#define BIT_CLEAR_RTSRST_8822B(x) ((x) & (~BITS_RTSRST_8822B))
#define BIT_GET_RTSRST_8822B(x)                                                \
	(((x) >> BIT_SHIFT_RTSRST_8822B) & BIT_MASK_RTSRST_8822B)
#define BIT_SET_RTSRST_8822B(x, v)                                             \
	(BIT_CLEAR_RTSRST_8822B(x) | BIT_RTSRST_8822B(v))

/* 2 REG_BACAMCMD_8822B (BLOCK ACK CAM COMMAND REGISTER) */
#define BIT_BACAM_POLL_8822B BIT(31)
#define BIT_BACAM_RST_8822B BIT(17)
#define BIT_BACAM_RW_8822B BIT(16)

#define BIT_SHIFT_TXSBM_8822B 14
#define BIT_MASK_TXSBM_8822B 0x3
#define BIT_TXSBM_8822B(x)                                                     \
	(((x) & BIT_MASK_TXSBM_8822B) << BIT_SHIFT_TXSBM_8822B)
#define BITS_TXSBM_8822B (BIT_MASK_TXSBM_8822B << BIT_SHIFT_TXSBM_8822B)
#define BIT_CLEAR_TXSBM_8822B(x) ((x) & (~BITS_TXSBM_8822B))
#define BIT_GET_TXSBM_8822B(x)                                                 \
	(((x) >> BIT_SHIFT_TXSBM_8822B) & BIT_MASK_TXSBM_8822B)
#define BIT_SET_TXSBM_8822B(x, v)                                              \
	(BIT_CLEAR_TXSBM_8822B(x) | BIT_TXSBM_8822B(v))

#define BIT_SHIFT_BACAM_ADDR_8822B 0
#define BIT_MASK_BACAM_ADDR_8822B 0x3f
#define BIT_BACAM_ADDR_8822B(x)                                                \
	(((x) & BIT_MASK_BACAM_ADDR_8822B) << BIT_SHIFT_BACAM_ADDR_8822B)
#define BITS_BACAM_ADDR_8822B                                                  \
	(BIT_MASK_BACAM_ADDR_8822B << BIT_SHIFT_BACAM_ADDR_8822B)
#define BIT_CLEAR_BACAM_ADDR_8822B(x) ((x) & (~BITS_BACAM_ADDR_8822B))
#define BIT_GET_BACAM_ADDR_8822B(x)                                            \
	(((x) >> BIT_SHIFT_BACAM_ADDR_8822B) & BIT_MASK_BACAM_ADDR_8822B)
#define BIT_SET_BACAM_ADDR_8822B(x, v)                                         \
	(BIT_CLEAR_BACAM_ADDR_8822B(x) | BIT_BACAM_ADDR_8822B(v))

/* 2 REG_BACAMCONTENT_8822B (BLOCK ACK CAM CONTENT REGISTER) */

#define BIT_SHIFT_BA_CONTENT_H_8822B (32 & CPU_OPT_WIDTH)
#define BIT_MASK_BA_CONTENT_H_8822B 0xffffffffL
#define BIT_BA_CONTENT_H_8822B(x)                                              \
	(((x) & BIT_MASK_BA_CONTENT_H_8822B) << BIT_SHIFT_BA_CONTENT_H_8822B)
#define BITS_BA_CONTENT_H_8822B                                                \
	(BIT_MASK_BA_CONTENT_H_8822B << BIT_SHIFT_BA_CONTENT_H_8822B)
#define BIT_CLEAR_BA_CONTENT_H_8822B(x) ((x) & (~BITS_BA_CONTENT_H_8822B))
#define BIT_GET_BA_CONTENT_H_8822B(x)                                          \
	(((x) >> BIT_SHIFT_BA_CONTENT_H_8822B) & BIT_MASK_BA_CONTENT_H_8822B)
#define BIT_SET_BA_CONTENT_H_8822B(x, v)                                       \
	(BIT_CLEAR_BA_CONTENT_H_8822B(x) | BIT_BA_CONTENT_H_8822B(v))

#define BIT_SHIFT_BA_CONTENT_L_8822B 0
#define BIT_MASK_BA_CONTENT_L_8822B 0xffffffffL
#define BIT_BA_CONTENT_L_8822B(x)                                              \
	(((x) & BIT_MASK_BA_CONTENT_L_8822B) << BIT_SHIFT_BA_CONTENT_L_8822B)
#define BITS_BA_CONTENT_L_8822B                                                \
	(BIT_MASK_BA_CONTENT_L_8822B << BIT_SHIFT_BA_CONTENT_L_8822B)
#define BIT_CLEAR_BA_CONTENT_L_8822B(x) ((x) & (~BITS_BA_CONTENT_L_8822B))
#define BIT_GET_BA_CONTENT_L_8822B(x)                                          \
	(((x) >> BIT_SHIFT_BA_CONTENT_L_8822B) & BIT_MASK_BA_CONTENT_L_8822B)
#define BIT_SET_BA_CONTENT_L_8822B(x, v)                                       \
	(BIT_CLEAR_BA_CONTENT_L_8822B(x) | BIT_BA_CONTENT_L_8822B(v))

/* 2 REG_WMAC_BITMAP_CTL_8822B */
#define BIT_BITMAP_VO_8822B BIT(7)
#define BIT_BITMAP_VI_8822B BIT(6)
#define BIT_BITMAP_BE_8822B BIT(5)
#define BIT_BITMAP_BK_8822B BIT(4)

#define BIT_SHIFT_BITMAP_CONDITION_8822B 2
#define BIT_MASK_BITMAP_CONDITION_8822B 0x3
#define BIT_BITMAP_CONDITION_8822B(x)                                          \
	(((x) & BIT_MASK_BITMAP_CONDITION_8822B)                               \
	 << BIT_SHIFT_BITMAP_CONDITION_8822B)
#define BITS_BITMAP_CONDITION_8822B                                            \
	(BIT_MASK_BITMAP_CONDITION_8822B << BIT_SHIFT_BITMAP_CONDITION_8822B)
#define BIT_CLEAR_BITMAP_CONDITION_8822B(x)                                    \
	((x) & (~BITS_BITMAP_CONDITION_8822B))
#define BIT_GET_BITMAP_CONDITION_8822B(x)                                      \
	(((x) >> BIT_SHIFT_BITMAP_CONDITION_8822B) &                           \
	 BIT_MASK_BITMAP_CONDITION_8822B)
#define BIT_SET_BITMAP_CONDITION_8822B(x, v)                                   \
	(BIT_CLEAR_BITMAP_CONDITION_8822B(x) | BIT_BITMAP_CONDITION_8822B(v))

#define BIT_BITMAP_SSNBK_COUNTER_CLR_8822B BIT(1)
#define BIT_BITMAP_FORCE_8822B BIT(0)

/* 2 REG_TX_RX_8822B STATUS */

#define BIT_SHIFT_RXPKT_TYPE_8822B 2
#define BIT_MASK_RXPKT_TYPE_8822B 0x3f
#define BIT_RXPKT_TYPE_8822B(x)                                                \
	(((x) & BIT_MASK_RXPKT_TYPE_8822B) << BIT_SHIFT_RXPKT_TYPE_8822B)
#define BITS_RXPKT_TYPE_8822B                                                  \
	(BIT_MASK_RXPKT_TYPE_8822B << BIT_SHIFT_RXPKT_TYPE_8822B)
#define BIT_CLEAR_RXPKT_TYPE_8822B(x) ((x) & (~BITS_RXPKT_TYPE_8822B))
#define BIT_GET_RXPKT_TYPE_8822B(x)                                            \
	(((x) >> BIT_SHIFT_RXPKT_TYPE_8822B) & BIT_MASK_RXPKT_TYPE_8822B)
#define BIT_SET_RXPKT_TYPE_8822B(x, v)                                         \
	(BIT_CLEAR_RXPKT_TYPE_8822B(x) | BIT_RXPKT_TYPE_8822B(v))

#define BIT_TXACT_IND_8822B BIT(1)
#define BIT_RXACT_IND_8822B BIT(0)

/* 2 REG_WMAC_BACAM_RPMEN_8822B */

#define BIT_SHIFT_BITMAP_SSNBK_COUNTER_8822B 2
#define BIT_MASK_BITMAP_SSNBK_COUNTER_8822B 0x3f
#define BIT_BITMAP_SSNBK_COUNTER_8822B(x)                                      \
	(((x) & BIT_MASK_BITMAP_SSNBK_COUNTER_8822B)                           \
	 << BIT_SHIFT_BITMAP_SSNBK_COUNTER_8822B)
#define BITS_BITMAP_SSNBK_COUNTER_8822B                                        \
	(BIT_MASK_BITMAP_SSNBK_COUNTER_8822B                                   \
	 << BIT_SHIFT_BITMAP_SSNBK_COUNTER_8822B)
#define BIT_CLEAR_BITMAP_SSNBK_COUNTER_8822B(x)                                \
	((x) & (~BITS_BITMAP_SSNBK_COUNTER_8822B))
#define BIT_GET_BITMAP_SSNBK_COUNTER_8822B(x)                                  \
	(((x) >> BIT_SHIFT_BITMAP_SSNBK_COUNTER_8822B) &                       \
	 BIT_MASK_BITMAP_SSNBK_COUNTER_8822B)
#define BIT_SET_BITMAP_SSNBK_COUNTER_8822B(x, v)                               \
	(BIT_CLEAR_BITMAP_SSNBK_COUNTER_8822B(x) |                             \
	 BIT_BITMAP_SSNBK_COUNTER_8822B(v))

#define BIT_BITMAP_EN_8822B BIT(1)
#define BIT_WMAC_BACAM_RPMEN_8822B BIT(0)

/* 2 REG_LBDLY_8822B (LOOPBACK DELAY REGISTER) */

#define BIT_SHIFT_LBDLY_8822B 0
#define BIT_MASK_LBDLY_8822B 0x1f
#define BIT_LBDLY_8822B(x)                                                     \
	(((x) & BIT_MASK_LBDLY_8822B) << BIT_SHIFT_LBDLY_8822B)
#define BITS_LBDLY_8822B (BIT_MASK_LBDLY_8822B << BIT_SHIFT_LBDLY_8822B)
#define BIT_CLEAR_LBDLY_8822B(x) ((x) & (~BITS_LBDLY_8822B))
#define BIT_GET_LBDLY_8822B(x)                                                 \
	(((x) >> BIT_SHIFT_LBDLY_8822B) & BIT_MASK_LBDLY_8822B)
#define BIT_SET_LBDLY_8822B(x, v)                                              \
	(BIT_CLEAR_LBDLY_8822B(x) | BIT_LBDLY_8822B(v))

/* 2 REG_RXERR_RPT_8822B (RX ERROR REPORT REGISTER) */

#define BIT_SHIFT_RXERR_RPT_SEL_V1_3_0_8822B 28
#define BIT_MASK_RXERR_RPT_SEL_V1_3_0_8822B 0xf
#define BIT_RXERR_RPT_SEL_V1_3_0_8822B(x)                                      \
	(((x) & BIT_MASK_RXERR_RPT_SEL_V1_3_0_8822B)                           \
	 << BIT_SHIFT_RXERR_RPT_SEL_V1_3_0_8822B)
#define BITS_RXERR_RPT_SEL_V1_3_0_8822B                                        \
	(BIT_MASK_RXERR_RPT_SEL_V1_3_0_8822B                                   \
	 << BIT_SHIFT_RXERR_RPT_SEL_V1_3_0_8822B)
#define BIT_CLEAR_RXERR_RPT_SEL_V1_3_0_8822B(x)                                \
	((x) & (~BITS_RXERR_RPT_SEL_V1_3_0_8822B))
#define BIT_GET_RXERR_RPT_SEL_V1_3_0_8822B(x)                                  \
	(((x) >> BIT_SHIFT_RXERR_RPT_SEL_V1_3_0_8822B) &                       \
	 BIT_MASK_RXERR_RPT_SEL_V1_3_0_8822B)
#define BIT_SET_RXERR_RPT_SEL_V1_3_0_8822B(x, v)                               \
	(BIT_CLEAR_RXERR_RPT_SEL_V1_3_0_8822B(x) |                             \
	 BIT_RXERR_RPT_SEL_V1_3_0_8822B(v))

#define BIT_RXERR_RPT_RST_8822B BIT(27)
#define BIT_RXERR_RPT_SEL_V1_4_8822B BIT(26)
#define BIT_W1S_8822B BIT(23)
#define BIT_UD_SELECT_BSSID_8822B BIT(22)

#define BIT_SHIFT_UD_SUB_TYPE_8822B 18
#define BIT_MASK_UD_SUB_TYPE_8822B 0xf
#define BIT_UD_SUB_TYPE_8822B(x)                                               \
	(((x) & BIT_MASK_UD_SUB_TYPE_8822B) << BIT_SHIFT_UD_SUB_TYPE_8822B)
#define BITS_UD_SUB_TYPE_8822B                                                 \
	(BIT_MASK_UD_SUB_TYPE_8822B << BIT_SHIFT_UD_SUB_TYPE_8822B)
#define BIT_CLEAR_UD_SUB_TYPE_8822B(x) ((x) & (~BITS_UD_SUB_TYPE_8822B))
#define BIT_GET_UD_SUB_TYPE_8822B(x)                                           \
	(((x) >> BIT_SHIFT_UD_SUB_TYPE_8822B) & BIT_MASK_UD_SUB_TYPE_8822B)
#define BIT_SET_UD_SUB_TYPE_8822B(x, v)                                        \
	(BIT_CLEAR_UD_SUB_TYPE_8822B(x) | BIT_UD_SUB_TYPE_8822B(v))

#define BIT_SHIFT_UD_TYPE_8822B 16
#define BIT_MASK_UD_TYPE_8822B 0x3
#define BIT_UD_TYPE_8822B(x)                                                   \
	(((x) & BIT_MASK_UD_TYPE_8822B) << BIT_SHIFT_UD_TYPE_8822B)
#define BITS_UD_TYPE_8822B (BIT_MASK_UD_TYPE_8822B << BIT_SHIFT_UD_TYPE_8822B)
#define BIT_CLEAR_UD_TYPE_8822B(x) ((x) & (~BITS_UD_TYPE_8822B))
#define BIT_GET_UD_TYPE_8822B(x)                                               \
	(((x) >> BIT_SHIFT_UD_TYPE_8822B) & BIT_MASK_UD_TYPE_8822B)
#define BIT_SET_UD_TYPE_8822B(x, v)                                            \
	(BIT_CLEAR_UD_TYPE_8822B(x) | BIT_UD_TYPE_8822B(v))

#define BIT_SHIFT_RPT_COUNTER_8822B 0
#define BIT_MASK_RPT_COUNTER_8822B 0xffff
#define BIT_RPT_COUNTER_8822B(x)                                               \
	(((x) & BIT_MASK_RPT_COUNTER_8822B) << BIT_SHIFT_RPT_COUNTER_8822B)
#define BITS_RPT_COUNTER_8822B                                                 \
	(BIT_MASK_RPT_COUNTER_8822B << BIT_SHIFT_RPT_COUNTER_8822B)
#define BIT_CLEAR_RPT_COUNTER_8822B(x) ((x) & (~BITS_RPT_COUNTER_8822B))
#define BIT_GET_RPT_COUNTER_8822B(x)                                           \
	(((x) >> BIT_SHIFT_RPT_COUNTER_8822B) & BIT_MASK_RPT_COUNTER_8822B)
#define BIT_SET_RPT_COUNTER_8822B(x, v)                                        \
	(BIT_CLEAR_RPT_COUNTER_8822B(x) | BIT_RPT_COUNTER_8822B(v))

/* 2 REG_WMAC_TRXPTCL_CTL_8822B (WMAC TX/RX PROTOCOL CONTROL REGISTER) */

#define BIT_SHIFT_ACKBA_TYPSEL_8822B (60 & CPU_OPT_WIDTH)
#define BIT_MASK_ACKBA_TYPSEL_8822B 0xf
#define BIT_ACKBA_TYPSEL_8822B(x)                                              \
	(((x) & BIT_MASK_ACKBA_TYPSEL_8822B) << BIT_SHIFT_ACKBA_TYPSEL_8822B)
#define BITS_ACKBA_TYPSEL_8822B                                                \
	(BIT_MASK_ACKBA_TYPSEL_8822B << BIT_SHIFT_ACKBA_TYPSEL_8822B)
#define BIT_CLEAR_ACKBA_TYPSEL_8822B(x) ((x) & (~BITS_ACKBA_TYPSEL_8822B))
#define BIT_GET_ACKBA_TYPSEL_8822B(x)                                          \
	(((x) >> BIT_SHIFT_ACKBA_TYPSEL_8822B) & BIT_MASK_ACKBA_TYPSEL_8822B)
#define BIT_SET_ACKBA_TYPSEL_8822B(x, v)                                       \
	(BIT_CLEAR_ACKBA_TYPSEL_8822B(x) | BIT_ACKBA_TYPSEL_8822B(v))

#define BIT_SHIFT_ACKBA_ACKPCHK_8822B (56 & CPU_OPT_WIDTH)
#define BIT_MASK_ACKBA_ACKPCHK_8822B 0xf
#define BIT_ACKBA_ACKPCHK_8822B(x)                                             \
	(((x) & BIT_MASK_ACKBA_ACKPCHK_8822B) << BIT_SHIFT_ACKBA_ACKPCHK_8822B)
#define BITS_ACKBA_ACKPCHK_8822B                                               \
	(BIT_MASK_ACKBA_ACKPCHK_8822B << BIT_SHIFT_ACKBA_ACKPCHK_8822B)
#define BIT_CLEAR_ACKBA_ACKPCHK_8822B(x) ((x) & (~BITS_ACKBA_ACKPCHK_8822B))
#define BIT_GET_ACKBA_ACKPCHK_8822B(x)                                         \
	(((x) >> BIT_SHIFT_ACKBA_ACKPCHK_8822B) & BIT_MASK_ACKBA_ACKPCHK_8822B)
#define BIT_SET_ACKBA_ACKPCHK_8822B(x, v)                                      \
	(BIT_CLEAR_ACKBA_ACKPCHK_8822B(x) | BIT_ACKBA_ACKPCHK_8822B(v))

#define BIT_SHIFT_ACKBAR_TYPESEL_8822B (48 & CPU_OPT_WIDTH)
#define BIT_MASK_ACKBAR_TYPESEL_8822B 0xff
#define BIT_ACKBAR_TYPESEL_8822B(x)                                            \
	(((x) & BIT_MASK_ACKBAR_TYPESEL_8822B)                                 \
	 << BIT_SHIFT_ACKBAR_TYPESEL_8822B)
#define BITS_ACKBAR_TYPESEL_8822B                                              \
	(BIT_MASK_ACKBAR_TYPESEL_8822B << BIT_SHIFT_ACKBAR_TYPESEL_8822B)
#define BIT_CLEAR_ACKBAR_TYPESEL_8822B(x) ((x) & (~BITS_ACKBAR_TYPESEL_8822B))
#define BIT_GET_ACKBAR_TYPESEL_8822B(x)                                        \
	(((x) >> BIT_SHIFT_ACKBAR_TYPESEL_8822B) &                             \
	 BIT_MASK_ACKBAR_TYPESEL_8822B)
#define BIT_SET_ACKBAR_TYPESEL_8822B(x, v)                                     \
	(BIT_CLEAR_ACKBAR_TYPESEL_8822B(x) | BIT_ACKBAR_TYPESEL_8822B(v))

#define BIT_SHIFT_ACKBAR_ACKPCHK_8822B (44 & CPU_OPT_WIDTH)
#define BIT_MASK_ACKBAR_ACKPCHK_8822B 0xf
#define BIT_ACKBAR_ACKPCHK_8822B(x)                                            \
	(((x) & BIT_MASK_ACKBAR_ACKPCHK_8822B)                                 \
	 << BIT_SHIFT_ACKBAR_ACKPCHK_8822B)
#define BITS_ACKBAR_ACKPCHK_8822B                                              \
	(BIT_MASK_ACKBAR_ACKPCHK_8822B << BIT_SHIFT_ACKBAR_ACKPCHK_8822B)
#define BIT_CLEAR_ACKBAR_ACKPCHK_8822B(x) ((x) & (~BITS_ACKBAR_ACKPCHK_8822B))
#define BIT_GET_ACKBAR_ACKPCHK_8822B(x)                                        \
	(((x) >> BIT_SHIFT_ACKBAR_ACKPCHK_8822B) &                             \
	 BIT_MASK_ACKBAR_ACKPCHK_8822B)
#define BIT_SET_ACKBAR_ACKPCHK_8822B(x, v)                                     \
	(BIT_CLEAR_ACKBAR_ACKPCHK_8822B(x) | BIT_ACKBAR_ACKPCHK_8822B(v))

#define BIT_RXBA_IGNOREA2_8822B BIT(42)
#define BIT_EN_SAVE_ALL_TXOPADDR_8822B BIT(41)
#define BIT_EN_TXCTS_TO_TXOPOWNER_INRXNAV_8822B BIT(40)
#define BIT_DIS_TXBA_AMPDUFCSERR_8822B BIT(39)
#define BIT_DIS_TXBA_RXBARINFULL_8822B BIT(38)
#define BIT_DIS_TXCFE_INFULL_8822B BIT(37)
#define BIT_DIS_TXCTS_INFULL_8822B BIT(36)
#define BIT_EN_TXACKBA_IN_TX_RDG_8822B BIT(35)
#define BIT_EN_TXACKBA_IN_TXOP_8822B BIT(34)
#define BIT_EN_TXCTS_IN_RXNAV_8822B BIT(33)
#define BIT_EN_TXCTS_INTXOP_8822B BIT(32)
#define BIT_BLK_EDCA_BBSLP_8822B BIT(31)
#define BIT_BLK_EDCA_BBSBY_8822B BIT(30)
#define BIT_ACKTO_BLOCK_SCH_EN_8822B BIT(27)
#define BIT_EIFS_BLOCK_SCH_EN_8822B BIT(26)
#define BIT_PLCPCHK_RST_EIFS_8822B BIT(25)
#define BIT_CCA_RST_EIFS_8822B BIT(24)
#define BIT_DIS_UPD_MYRXPKTNAV_8822B BIT(23)
#define BIT_EARLY_TXBA_8822B BIT(22)

#define BIT_SHIFT_RESP_CHNBUSY_8822B 20
#define BIT_MASK_RESP_CHNBUSY_8822B 0x3
#define BIT_RESP_CHNBUSY_8822B(x)                                              \
	(((x) & BIT_MASK_RESP_CHNBUSY_8822B) << BIT_SHIFT_RESP_CHNBUSY_8822B)
#define BITS_RESP_CHNBUSY_8822B                                                \
	(BIT_MASK_RESP_CHNBUSY_8822B << BIT_SHIFT_RESP_CHNBUSY_8822B)
#define BIT_CLEAR_RESP_CHNBUSY_8822B(x) ((x) & (~BITS_RESP_CHNBUSY_8822B))
#define BIT_GET_RESP_CHNBUSY_8822B(x)                                          \
	(((x) >> BIT_SHIFT_RESP_CHNBUSY_8822B) & BIT_MASK_RESP_CHNBUSY_8822B)
#define BIT_SET_RESP_CHNBUSY_8822B(x, v)                                       \
	(BIT_CLEAR_RESP_CHNBUSY_8822B(x) | BIT_RESP_CHNBUSY_8822B(v))

#define BIT_RESP_DCTS_EN_8822B BIT(19)
#define BIT_RESP_DCFE_EN_8822B BIT(18)
#define BIT_RESP_SPLCPEN_8822B BIT(17)
#define BIT_RESP_SGIEN_8822B BIT(16)
#define BIT_RESP_LDPC_EN_8822B BIT(15)
#define BIT_DIS_RESP_ACKINCCA_8822B BIT(14)
#define BIT_DIS_RESP_CTSINCCA_8822B BIT(13)

#define BIT_SHIFT_R_WMAC_SECOND_CCA_TIMER_8822B 10
#define BIT_MASK_R_WMAC_SECOND_CCA_TIMER_8822B 0x7
#define BIT_R_WMAC_SECOND_CCA_TIMER_8822B(x)                                   \
	(((x) & BIT_MASK_R_WMAC_SECOND_CCA_TIMER_8822B)                        \
	 << BIT_SHIFT_R_WMAC_SECOND_CCA_TIMER_8822B)
#define BITS_R_WMAC_SECOND_CCA_TIMER_8822B                                     \
	(BIT_MASK_R_WMAC_SECOND_CCA_TIMER_8822B                                \
	 << BIT_SHIFT_R_WMAC_SECOND_CCA_TIMER_8822B)
#define BIT_CLEAR_R_WMAC_SECOND_CCA_TIMER_8822B(x)                             \
	((x) & (~BITS_R_WMAC_SECOND_CCA_TIMER_8822B))
#define BIT_GET_R_WMAC_SECOND_CCA_TIMER_8822B(x)                               \
	(((x) >> BIT_SHIFT_R_WMAC_SECOND_CCA_TIMER_8822B) &                    \
	 BIT_MASK_R_WMAC_SECOND_CCA_TIMER_8822B)
#define BIT_SET_R_WMAC_SECOND_CCA_TIMER_8822B(x, v)                            \
	(BIT_CLEAR_R_WMAC_SECOND_CCA_TIMER_8822B(x) |                          \
	 BIT_R_WMAC_SECOND_CCA_TIMER_8822B(v))

#define BIT_SHIFT_RFMOD_8822B 7
#define BIT_MASK_RFMOD_8822B 0x3
#define BIT_RFMOD_8822B(x)                                                     \
	(((x) & BIT_MASK_RFMOD_8822B) << BIT_SHIFT_RFMOD_8822B)
#define BITS_RFMOD_8822B (BIT_MASK_RFMOD_8822B << BIT_SHIFT_RFMOD_8822B)
#define BIT_CLEAR_RFMOD_8822B(x) ((x) & (~BITS_RFMOD_8822B))
#define BIT_GET_RFMOD_8822B(x)                                                 \
	(((x) >> BIT_SHIFT_RFMOD_8822B) & BIT_MASK_RFMOD_8822B)
#define BIT_SET_RFMOD_8822B(x, v)                                              \
	(BIT_CLEAR_RFMOD_8822B(x) | BIT_RFMOD_8822B(v))

#define BIT_SHIFT_RESP_CTS_DYNBW_SEL_8822B 5
#define BIT_MASK_RESP_CTS_DYNBW_SEL_8822B 0x3
#define BIT_RESP_CTS_DYNBW_SEL_8822B(x)                                        \
	(((x) & BIT_MASK_RESP_CTS_DYNBW_SEL_8822B)                             \
	 << BIT_SHIFT_RESP_CTS_DYNBW_SEL_8822B)
#define BITS_RESP_CTS_DYNBW_SEL_8822B                                          \
	(BIT_MASK_RESP_CTS_DYNBW_SEL_8822B                                     \
	 << BIT_SHIFT_RESP_CTS_DYNBW_SEL_8822B)
#define BIT_CLEAR_RESP_CTS_DYNBW_SEL_8822B(x)                                  \
	((x) & (~BITS_RESP_CTS_DYNBW_SEL_8822B))
#define BIT_GET_RESP_CTS_DYNBW_SEL_8822B(x)                                    \
	(((x) >> BIT_SHIFT_RESP_CTS_DYNBW_SEL_8822B) &                         \
	 BIT_MASK_RESP_CTS_DYNBW_SEL_8822B)
#define BIT_SET_RESP_CTS_DYNBW_SEL_8822B(x, v)                                 \
	(BIT_CLEAR_RESP_CTS_DYNBW_SEL_8822B(x) |                               \
	 BIT_RESP_CTS_DYNBW_SEL_8822B(v))

#define BIT_DLY_TX_WAIT_RXANTSEL_8822B BIT(4)
#define BIT_TXRESP_BY_RXANTSEL_8822B BIT(3)

#define BIT_SHIFT_ORIG_DCTS_CHK_8822B 0
#define BIT_MASK_ORIG_DCTS_CHK_8822B 0x3
#define BIT_ORIG_DCTS_CHK_8822B(x)                                             \
	(((x) & BIT_MASK_ORIG_DCTS_CHK_8822B) << BIT_SHIFT_ORIG_DCTS_CHK_8822B)
#define BITS_ORIG_DCTS_CHK_8822B                                               \
	(BIT_MASK_ORIG_DCTS_CHK_8822B << BIT_SHIFT_ORIG_DCTS_CHK_8822B)
#define BIT_CLEAR_ORIG_DCTS_CHK_8822B(x) ((x) & (~BITS_ORIG_DCTS_CHK_8822B))
#define BIT_GET_ORIG_DCTS_CHK_8822B(x)                                         \
	(((x) >> BIT_SHIFT_ORIG_DCTS_CHK_8822B) & BIT_MASK_ORIG_DCTS_CHK_8822B)
#define BIT_SET_ORIG_DCTS_CHK_8822B(x, v)                                      \
	(BIT_CLEAR_ORIG_DCTS_CHK_8822B(x) | BIT_ORIG_DCTS_CHK_8822B(v))

/* 2 REG_CAMCMD_8822B (CAM COMMAND REGISTER) */
#define BIT_SECCAM_POLLING_8822B BIT(31)
#define BIT_SECCAM_CLR_8822B BIT(30)
#define BIT_MFBCAM_CLR_8822B BIT(29)
#define BIT_SECCAM_WE_8822B BIT(16)

#define BIT_SHIFT_SECCAM_ADDR_V2_8822B 0
#define BIT_MASK_SECCAM_ADDR_V2_8822B 0x3ff
#define BIT_SECCAM_ADDR_V2_8822B(x)                                            \
	(((x) & BIT_MASK_SECCAM_ADDR_V2_8822B)                                 \
	 << BIT_SHIFT_SECCAM_ADDR_V2_8822B)
#define BITS_SECCAM_ADDR_V2_8822B                                              \
	(BIT_MASK_SECCAM_ADDR_V2_8822B << BIT_SHIFT_SECCAM_ADDR_V2_8822B)
#define BIT_CLEAR_SECCAM_ADDR_V2_8822B(x) ((x) & (~BITS_SECCAM_ADDR_V2_8822B))
#define BIT_GET_SECCAM_ADDR_V2_8822B(x)                                        \
	(((x) >> BIT_SHIFT_SECCAM_ADDR_V2_8822B) &                             \
	 BIT_MASK_SECCAM_ADDR_V2_8822B)
#define BIT_SET_SECCAM_ADDR_V2_8822B(x, v)                                     \
	(BIT_CLEAR_SECCAM_ADDR_V2_8822B(x) | BIT_SECCAM_ADDR_V2_8822B(v))

/* 2 REG_CAMWRITE_8822B (CAM WRITE REGISTER) */

#define BIT_SHIFT_CAMW_DATA_8822B 0
#define BIT_MASK_CAMW_DATA_8822B 0xffffffffL
#define BIT_CAMW_DATA_8822B(x)                                                 \
	(((x) & BIT_MASK_CAMW_DATA_8822B) << BIT_SHIFT_CAMW_DATA_8822B)
#define BITS_CAMW_DATA_8822B                                                   \
	(BIT_MASK_CAMW_DATA_8822B << BIT_SHIFT_CAMW_DATA_8822B)
#define BIT_CLEAR_CAMW_DATA_8822B(x) ((x) & (~BITS_CAMW_DATA_8822B))
#define BIT_GET_CAMW_DATA_8822B(x)                                             \
	(((x) >> BIT_SHIFT_CAMW_DATA_8822B) & BIT_MASK_CAMW_DATA_8822B)
#define BIT_SET_CAMW_DATA_8822B(x, v)                                          \
	(BIT_CLEAR_CAMW_DATA_8822B(x) | BIT_CAMW_DATA_8822B(v))

/* 2 REG_CAMREAD_8822B (CAM READ REGISTER) */

#define BIT_SHIFT_CAMR_DATA_8822B 0
#define BIT_MASK_CAMR_DATA_8822B 0xffffffffL
#define BIT_CAMR_DATA_8822B(x)                                                 \
	(((x) & BIT_MASK_CAMR_DATA_8822B) << BIT_SHIFT_CAMR_DATA_8822B)
#define BITS_CAMR_DATA_8822B                                                   \
	(BIT_MASK_CAMR_DATA_8822B << BIT_SHIFT_CAMR_DATA_8822B)
#define BIT_CLEAR_CAMR_DATA_8822B(x) ((x) & (~BITS_CAMR_DATA_8822B))
#define BIT_GET_CAMR_DATA_8822B(x)                                             \
	(((x) >> BIT_SHIFT_CAMR_DATA_8822B) & BIT_MASK_CAMR_DATA_8822B)
#define BIT_SET_CAMR_DATA_8822B(x, v)                                          \
	(BIT_CLEAR_CAMR_DATA_8822B(x) | BIT_CAMR_DATA_8822B(v))

/* 2 REG_CAMDBG_8822B (CAM DEBUG REGISTER) */
#define BIT_SECCAM_INFO_8822B BIT(31)
#define BIT_SEC_KEYFOUND_8822B BIT(15)

#define BIT_SHIFT_CAMDBG_SEC_TYPE_8822B 12
#define BIT_MASK_CAMDBG_SEC_TYPE_8822B 0x7
#define BIT_CAMDBG_SEC_TYPE_8822B(x)                                           \
	(((x) & BIT_MASK_CAMDBG_SEC_TYPE_8822B)                                \
	 << BIT_SHIFT_CAMDBG_SEC_TYPE_8822B)
#define BITS_CAMDBG_SEC_TYPE_8822B                                             \
	(BIT_MASK_CAMDBG_SEC_TYPE_8822B << BIT_SHIFT_CAMDBG_SEC_TYPE_8822B)
#define BIT_CLEAR_CAMDBG_SEC_TYPE_8822B(x) ((x) & (~BITS_CAMDBG_SEC_TYPE_8822B))
#define BIT_GET_CAMDBG_SEC_TYPE_8822B(x)                                       \
	(((x) >> BIT_SHIFT_CAMDBG_SEC_TYPE_8822B) &                            \
	 BIT_MASK_CAMDBG_SEC_TYPE_8822B)
#define BIT_SET_CAMDBG_SEC_TYPE_8822B(x, v)                                    \
	(BIT_CLEAR_CAMDBG_SEC_TYPE_8822B(x) | BIT_CAMDBG_SEC_TYPE_8822B(v))

#define BIT_CAMDBG_EXT_SECTYPE_8822B BIT(11)

#define BIT_SHIFT_CAMDBG_MIC_KEY_IDX_8822B 5
#define BIT_MASK_CAMDBG_MIC_KEY_IDX_8822B 0x1f
#define BIT_CAMDBG_MIC_KEY_IDX_8822B(x)                                        \
	(((x) & BIT_MASK_CAMDBG_MIC_KEY_IDX_8822B)                             \
	 << BIT_SHIFT_CAMDBG_MIC_KEY_IDX_8822B)
#define BITS_CAMDBG_MIC_KEY_IDX_8822B                                          \
	(BIT_MASK_CAMDBG_MIC_KEY_IDX_8822B                                     \
	 << BIT_SHIFT_CAMDBG_MIC_KEY_IDX_8822B)
#define BIT_CLEAR_CAMDBG_MIC_KEY_IDX_8822B(x)                                  \
	((x) & (~BITS_CAMDBG_MIC_KEY_IDX_8822B))
#define BIT_GET_CAMDBG_MIC_KEY_IDX_8822B(x)                                    \
	(((x) >> BIT_SHIFT_CAMDBG_MIC_KEY_IDX_8822B) &                         \
	 BIT_MASK_CAMDBG_MIC_KEY_IDX_8822B)
#define BIT_SET_CAMDBG_MIC_KEY_IDX_8822B(x, v)                                 \
	(BIT_CLEAR_CAMDBG_MIC_KEY_IDX_8822B(x) |                               \
	 BIT_CAMDBG_MIC_KEY_IDX_8822B(v))

#define BIT_SHIFT_CAMDBG_SEC_KEY_IDX_8822B 0
#define BIT_MASK_CAMDBG_SEC_KEY_IDX_8822B 0x1f
#define BIT_CAMDBG_SEC_KEY_IDX_8822B(x)                                        \
	(((x) & BIT_MASK_CAMDBG_SEC_KEY_IDX_8822B)                             \
	 << BIT_SHIFT_CAMDBG_SEC_KEY_IDX_8822B)
#define BITS_CAMDBG_SEC_KEY_IDX_8822B                                          \
	(BIT_MASK_CAMDBG_SEC_KEY_IDX_8822B                                     \
	 << BIT_SHIFT_CAMDBG_SEC_KEY_IDX_8822B)
#define BIT_CLEAR_CAMDBG_SEC_KEY_IDX_8822B(x)                                  \
	((x) & (~BITS_CAMDBG_SEC_KEY_IDX_8822B))
#define BIT_GET_CAMDBG_SEC_KEY_IDX_8822B(x)                                    \
	(((x) >> BIT_SHIFT_CAMDBG_SEC_KEY_IDX_8822B) &                         \
	 BIT_MASK_CAMDBG_SEC_KEY_IDX_8822B)
#define BIT_SET_CAMDBG_SEC_KEY_IDX_8822B(x, v)                                 \
	(BIT_CLEAR_CAMDBG_SEC_KEY_IDX_8822B(x) |                               \
	 BIT_CAMDBG_SEC_KEY_IDX_8822B(v))

/* 2 REG_RXFILTER_ACTION_1_8822B */

#define BIT_SHIFT_RXFILTER_ACTION_1_8822B 0
#define BIT_MASK_RXFILTER_ACTION_1_8822B 0xff
#define BIT_RXFILTER_ACTION_1_8822B(x)                                         \
	(((x) & BIT_MASK_RXFILTER_ACTION_1_8822B)                              \
	 << BIT_SHIFT_RXFILTER_ACTION_1_8822B)
#define BITS_RXFILTER_ACTION_1_8822B                                           \
	(BIT_MASK_RXFILTER_ACTION_1_8822B << BIT_SHIFT_RXFILTER_ACTION_1_8822B)
#define BIT_CLEAR_RXFILTER_ACTION_1_8822B(x)                                   \
	((x) & (~BITS_RXFILTER_ACTION_1_8822B))
#define BIT_GET_RXFILTER_ACTION_1_8822B(x)                                     \
	(((x) >> BIT_SHIFT_RXFILTER_ACTION_1_8822B) &                          \
	 BIT_MASK_RXFILTER_ACTION_1_8822B)
#define BIT_SET_RXFILTER_ACTION_1_8822B(x, v)                                  \
	(BIT_CLEAR_RXFILTER_ACTION_1_8822B(x) | BIT_RXFILTER_ACTION_1_8822B(v))

/* 2 REG_RXFILTER_CATEGORY_1_8822B */

#define BIT_SHIFT_RXFILTER_CATEGORY_1_8822B 0
#define BIT_MASK_RXFILTER_CATEGORY_1_8822B 0xff
#define BIT_RXFILTER_CATEGORY_1_8822B(x)                                       \
	(((x) & BIT_MASK_RXFILTER_CATEGORY_1_8822B)                            \
	 << BIT_SHIFT_RXFILTER_CATEGORY_1_8822B)
#define BITS_RXFILTER_CATEGORY_1_8822B                                         \
	(BIT_MASK_RXFILTER_CATEGORY_1_8822B                                    \
	 << BIT_SHIFT_RXFILTER_CATEGORY_1_8822B)
#define BIT_CLEAR_RXFILTER_CATEGORY_1_8822B(x)                                 \
	((x) & (~BITS_RXFILTER_CATEGORY_1_8822B))
#define BIT_GET_RXFILTER_CATEGORY_1_8822B(x)                                   \
	(((x) >> BIT_SHIFT_RXFILTER_CATEGORY_1_8822B) &                        \
	 BIT_MASK_RXFILTER_CATEGORY_1_8822B)
#define BIT_SET_RXFILTER_CATEGORY_1_8822B(x, v)                                \
	(BIT_CLEAR_RXFILTER_CATEGORY_1_8822B(x) |                              \
	 BIT_RXFILTER_CATEGORY_1_8822B(v))

/* 2 REG_SECCFG_8822B (SECURITY CONFIGURATION REGISTER) */
#define BIT_DIS_GCLK_WAPI_8822B BIT(15)
#define BIT_DIS_GCLK_AES_8822B BIT(14)
#define BIT_DIS_GCLK_TKIP_8822B BIT(13)
#define BIT_AES_SEL_QC_1_8822B BIT(12)
#define BIT_AES_SEL_QC_0_8822B BIT(11)
#define BIT_CHK_BMC_8822B BIT(9)
#define BIT_CHK_KEYID_8822B BIT(8)
#define BIT_RXBCUSEDK_8822B BIT(7)
#define BIT_TXBCUSEDK_8822B BIT(6)
#define BIT_NOSKMC_8822B BIT(5)
#define BIT_SKBYA2_8822B BIT(4)
#define BIT_RXDEC_8822B BIT(3)
#define BIT_TXENC_8822B BIT(2)
#define BIT_RXUHUSEDK_8822B BIT(1)
#define BIT_TXUHUSEDK_8822B BIT(0)

/* 2 REG_RXFILTER_ACTION_3_8822B */

#define BIT_SHIFT_RXFILTER_ACTION_3_8822B 0
#define BIT_MASK_RXFILTER_ACTION_3_8822B 0xff
#define BIT_RXFILTER_ACTION_3_8822B(x)                                         \
	(((x) & BIT_MASK_RXFILTER_ACTION_3_8822B)                              \
	 << BIT_SHIFT_RXFILTER_ACTION_3_8822B)
#define BITS_RXFILTER_ACTION_3_8822B                                           \
	(BIT_MASK_RXFILTER_ACTION_3_8822B << BIT_SHIFT_RXFILTER_ACTION_3_8822B)
#define BIT_CLEAR_RXFILTER_ACTION_3_8822B(x)                                   \
	((x) & (~BITS_RXFILTER_ACTION_3_8822B))
#define BIT_GET_RXFILTER_ACTION_3_8822B(x)                                     \
	(((x) >> BIT_SHIFT_RXFILTER_ACTION_3_8822B) &                          \
	 BIT_MASK_RXFILTER_ACTION_3_8822B)
#define BIT_SET_RXFILTER_ACTION_3_8822B(x, v)                                  \
	(BIT_CLEAR_RXFILTER_ACTION_3_8822B(x) | BIT_RXFILTER_ACTION_3_8822B(v))

/* 2 REG_RXFILTER_CATEGORY_3_8822B */

#define BIT_SHIFT_RXFILTER_CATEGORY_3_8822B 0
#define BIT_MASK_RXFILTER_CATEGORY_3_8822B 0xff
#define BIT_RXFILTER_CATEGORY_3_8822B(x)                                       \
	(((x) & BIT_MASK_RXFILTER_CATEGORY_3_8822B)                            \
	 << BIT_SHIFT_RXFILTER_CATEGORY_3_8822B)
#define BITS_RXFILTER_CATEGORY_3_8822B                                         \
	(BIT_MASK_RXFILTER_CATEGORY_3_8822B                                    \
	 << BIT_SHIFT_RXFILTER_CATEGORY_3_8822B)
#define BIT_CLEAR_RXFILTER_CATEGORY_3_8822B(x)                                 \
	((x) & (~BITS_RXFILTER_CATEGORY_3_8822B))
#define BIT_GET_RXFILTER_CATEGORY_3_8822B(x)                                   \
	(((x) >> BIT_SHIFT_RXFILTER_CATEGORY_3_8822B) &                        \
	 BIT_MASK_RXFILTER_CATEGORY_3_8822B)
#define BIT_SET_RXFILTER_CATEGORY_3_8822B(x, v)                                \
	(BIT_CLEAR_RXFILTER_CATEGORY_3_8822B(x) |                              \
	 BIT_RXFILTER_CATEGORY_3_8822B(v))

/* 2 REG_RXFILTER_ACTION_2_8822B */

#define BIT_SHIFT_RXFILTER_ACTION_2_8822B 0
#define BIT_MASK_RXFILTER_ACTION_2_8822B 0xff
#define BIT_RXFILTER_ACTION_2_8822B(x)                                         \
	(((x) & BIT_MASK_RXFILTER_ACTION_2_8822B)                              \
	 << BIT_SHIFT_RXFILTER_ACTION_2_8822B)
#define BITS_RXFILTER_ACTION_2_8822B                                           \
	(BIT_MASK_RXFILTER_ACTION_2_8822B << BIT_SHIFT_RXFILTER_ACTION_2_8822B)
#define BIT_CLEAR_RXFILTER_ACTION_2_8822B(x)                                   \
	((x) & (~BITS_RXFILTER_ACTION_2_8822B))
#define BIT_GET_RXFILTER_ACTION_2_8822B(x)                                     \
	(((x) >> BIT_SHIFT_RXFILTER_ACTION_2_8822B) &                          \
	 BIT_MASK_RXFILTER_ACTION_2_8822B)
#define BIT_SET_RXFILTER_ACTION_2_8822B(x, v)                                  \
	(BIT_CLEAR_RXFILTER_ACTION_2_8822B(x) | BIT_RXFILTER_ACTION_2_8822B(v))

/* 2 REG_RXFILTER_CATEGORY_2_8822B */

#define BIT_SHIFT_RXFILTER_CATEGORY_2_8822B 0
#define BIT_MASK_RXFILTER_CATEGORY_2_8822B 0xff
#define BIT_RXFILTER_CATEGORY_2_8822B(x)                                       \
	(((x) & BIT_MASK_RXFILTER_CATEGORY_2_8822B)                            \
	 << BIT_SHIFT_RXFILTER_CATEGORY_2_8822B)
#define BITS_RXFILTER_CATEGORY_2_8822B                                         \
	(BIT_MASK_RXFILTER_CATEGORY_2_8822B                                    \
	 << BIT_SHIFT_RXFILTER_CATEGORY_2_8822B)
#define BIT_CLEAR_RXFILTER_CATEGORY_2_8822B(x)                                 \
	((x) & (~BITS_RXFILTER_CATEGORY_2_8822B))
#define BIT_GET_RXFILTER_CATEGORY_2_8822B(x)                                   \
	(((x) >> BIT_SHIFT_RXFILTER_CATEGORY_2_8822B) &                        \
	 BIT_MASK_RXFILTER_CATEGORY_2_8822B)
#define BIT_SET_RXFILTER_CATEGORY_2_8822B(x, v)                                \
	(BIT_CLEAR_RXFILTER_CATEGORY_2_8822B(x) |                              \
	 BIT_RXFILTER_CATEGORY_2_8822B(v))

/* 2 REG_RXFLTMAP4_8822B (RX FILTER MAP GROUP 4) */
#define BIT_CTRLFLT15EN_FW_8822B BIT(15)
#define BIT_CTRLFLT14EN_FW_8822B BIT(14)
#define BIT_CTRLFLT13EN_FW_8822B BIT(13)
#define BIT_CTRLFLT12EN_FW_8822B BIT(12)
#define BIT_CTRLFLT11EN_FW_8822B BIT(11)
#define BIT_CTRLFLT10EN_FW_8822B BIT(10)
#define BIT_CTRLFLT9EN_FW_8822B BIT(9)
#define BIT_CTRLFLT8EN_FW_8822B BIT(8)
#define BIT_CTRLFLT7EN_FW_8822B BIT(7)
#define BIT_CTRLFLT6EN_FW_8822B BIT(6)
#define BIT_CTRLFLT5EN_FW_8822B BIT(5)
#define BIT_CTRLFLT4EN_FW_8822B BIT(4)
#define BIT_CTRLFLT3EN_FW_8822B BIT(3)
#define BIT_CTRLFLT2EN_FW_8822B BIT(2)
#define BIT_CTRLFLT1EN_FW_8822B BIT(1)
#define BIT_CTRLFLT0EN_FW_8822B BIT(0)

/* 2 REG_RXFLTMAP3_8822B (RX FILTER MAP GROUP 3) */
#define BIT_MGTFLT15EN_FW_8822B BIT(15)
#define BIT_MGTFLT14EN_FW_8822B BIT(14)
#define BIT_MGTFLT13EN_FW_8822B BIT(13)
#define BIT_MGTFLT12EN_FW_8822B BIT(12)
#define BIT_MGTFLT11EN_FW_8822B BIT(11)
#define BIT_MGTFLT10EN_FW_8822B BIT(10)
#define BIT_MGTFLT9EN_FW_8822B BIT(9)
#define BIT_MGTFLT8EN_FW_8822B BIT(8)
#define BIT_MGTFLT7EN_FW_8822B BIT(7)
#define BIT_MGTFLT6EN_FW_8822B BIT(6)
#define BIT_MGTFLT5EN_FW_8822B BIT(5)
#define BIT_MGTFLT4EN_FW_8822B BIT(4)
#define BIT_MGTFLT3EN_FW_8822B BIT(3)
#define BIT_MGTFLT2EN_FW_8822B BIT(2)
#define BIT_MGTFLT1EN_FW_8822B BIT(1)
#define BIT_MGTFLT0EN_FW_8822B BIT(0)

/* 2 REG_RXFLTMAP6_8822B (RX FILTER MAP GROUP 6) */
#define BIT_ACTIONFLT15EN_FW_8822B BIT(15)
#define BIT_ACTIONFLT14EN_FW_8822B BIT(14)
#define BIT_ACTIONFLT13EN_FW_8822B BIT(13)
#define BIT_ACTIONFLT12EN_FW_8822B BIT(12)
#define BIT_ACTIONFLT11EN_FW_8822B BIT(11)
#define BIT_ACTIONFLT10EN_FW_8822B BIT(10)
#define BIT_ACTIONFLT9EN_FW_8822B BIT(9)
#define BIT_ACTIONFLT8EN_FW_8822B BIT(8)
#define BIT_ACTIONFLT7EN_FW_8822B BIT(7)
#define BIT_ACTIONFLT6EN_FW_8822B BIT(6)
#define BIT_ACTIONFLT5EN_FW_8822B BIT(5)
#define BIT_ACTIONFLT4EN_FW_8822B BIT(4)
#define BIT_ACTIONFLT3EN_FW_8822B BIT(3)
#define BIT_ACTIONFLT2EN_FW_8822B BIT(2)
#define BIT_ACTIONFLT1EN_FW_8822B BIT(1)
#define BIT_ACTIONFLT0EN_FW_8822B BIT(0)

/* 2 REG_RXFLTMAP5_8822B (RX FILTER MAP GROUP 5) */
#define BIT_DATAFLT15EN_FW_8822B BIT(15)
#define BIT_DATAFLT14EN_FW_8822B BIT(14)
#define BIT_DATAFLT13EN_FW_8822B BIT(13)
#define BIT_DATAFLT12EN_FW_8822B BIT(12)
#define BIT_DATAFLT11EN_FW_8822B BIT(11)
#define BIT_DATAFLT10EN_FW_8822B BIT(10)
#define BIT_DATAFLT9EN_FW_8822B BIT(9)
#define BIT_DATAFLT8EN_FW_8822B BIT(8)
#define BIT_DATAFLT7EN_FW_8822B BIT(7)
#define BIT_DATAFLT6EN_FW_8822B BIT(6)
#define BIT_DATAFLT5EN_FW_8822B BIT(5)
#define BIT_DATAFLT4EN_FW_8822B BIT(4)
#define BIT_DATAFLT3EN_FW_8822B BIT(3)
#define BIT_DATAFLT2EN_FW_8822B BIT(2)
#define BIT_DATAFLT1EN_FW_8822B BIT(1)
#define BIT_DATAFLT0EN_FW_8822B BIT(0)

/* 2 REG_WMMPS_UAPSD_TID_8822B (WMM POWER SAVE UAPSD TID REGISTER) */
#define BIT_WMMPS_UAPSD_TID7_8822B BIT(7)
#define BIT_WMMPS_UAPSD_TID6_8822B BIT(6)
#define BIT_WMMPS_UAPSD_TID5_8822B BIT(5)
#define BIT_WMMPS_UAPSD_TID4_8822B BIT(4)
#define BIT_WMMPS_UAPSD_TID3_8822B BIT(3)
#define BIT_WMMPS_UAPSD_TID2_8822B BIT(2)
#define BIT_WMMPS_UAPSD_TID1_8822B BIT(1)
#define BIT_WMMPS_UAPSD_TID0_8822B BIT(0)

/* 2 REG_PS_RX_INFO_8822B (POWER SAVE RX INFORMATION REGISTER) */

#define BIT_SHIFT_PORTSEL__PS_RX_INFO_8822B 5
#define BIT_MASK_PORTSEL__PS_RX_INFO_8822B 0x7
#define BIT_PORTSEL__PS_RX_INFO_8822B(x)                                       \
	(((x) & BIT_MASK_PORTSEL__PS_RX_INFO_8822B)                            \
	 << BIT_SHIFT_PORTSEL__PS_RX_INFO_8822B)
#define BITS_PORTSEL__PS_RX_INFO_8822B                                         \
	(BIT_MASK_PORTSEL__PS_RX_INFO_8822B                                    \
	 << BIT_SHIFT_PORTSEL__PS_RX_INFO_8822B)
#define BIT_CLEAR_PORTSEL__PS_RX_INFO_8822B(x)                                 \
	((x) & (~BITS_PORTSEL__PS_RX_INFO_8822B))
#define BIT_GET_PORTSEL__PS_RX_INFO_8822B(x)                                   \
	(((x) >> BIT_SHIFT_PORTSEL__PS_RX_INFO_8822B) &                        \
	 BIT_MASK_PORTSEL__PS_RX_INFO_8822B)
#define BIT_SET_PORTSEL__PS_RX_INFO_8822B(x, v)                                \
	(BIT_CLEAR_PORTSEL__PS_RX_INFO_8822B(x) |                              \
	 BIT_PORTSEL__PS_RX_INFO_8822B(v))

#define BIT_RXCTRLIN0_8822B BIT(4)
#define BIT_RXMGTIN0_8822B BIT(3)
#define BIT_RXDATAIN2_8822B BIT(2)
#define BIT_RXDATAIN1_8822B BIT(1)
#define BIT_RXDATAIN0_8822B BIT(0)

/* 2 REG_NAN_RX_TSF_FILTER_8822B(NAN_RX_TSF_ADDRESS_FILTER) */
#define BIT_CHK_TSF_TA_8822B BIT(2)
#define BIT_CHK_TSF_CBSSID_8822B BIT(1)
#define BIT_CHK_TSF_EN_8822B BIT(0)

/* 2 REG_WOW_CTRL_8822B (WAKE ON WLAN CONTROL REGISTER) */

#define BIT_SHIFT_PSF_BSSIDSEL_B2B1_8822B 6
#define BIT_MASK_PSF_BSSIDSEL_B2B1_8822B 0x3
#define BIT_PSF_BSSIDSEL_B2B1_8822B(x)                                         \
	(((x) & BIT_MASK_PSF_BSSIDSEL_B2B1_8822B)                              \
	 << BIT_SHIFT_PSF_BSSIDSEL_B2B1_8822B)
#define BITS_PSF_BSSIDSEL_B2B1_8822B                                           \
	(BIT_MASK_PSF_BSSIDSEL_B2B1_8822B << BIT_SHIFT_PSF_BSSIDSEL_B2B1_8822B)
#define BIT_CLEAR_PSF_BSSIDSEL_B2B1_8822B(x)                                   \
	((x) & (~BITS_PSF_BSSIDSEL_B2B1_8822B))
#define BIT_GET_PSF_BSSIDSEL_B2B1_8822B(x)                                     \
	(((x) >> BIT_SHIFT_PSF_BSSIDSEL_B2B1_8822B) &                          \
	 BIT_MASK_PSF_BSSIDSEL_B2B1_8822B)
#define BIT_SET_PSF_BSSIDSEL_B2B1_8822B(x, v)                                  \
	(BIT_CLEAR_PSF_BSSIDSEL_B2B1_8822B(x) | BIT_PSF_BSSIDSEL_B2B1_8822B(v))

#define BIT_WOWHCI_8822B BIT(5)
#define BIT_PSF_BSSIDSEL_B0_8822B BIT(4)
#define BIT_UWF_8822B BIT(3)
#define BIT_MAGIC_8822B BIT(2)
#define BIT_WOWEN_8822B BIT(1)
#define BIT_FORCE_WAKEUP_8822B BIT(0)

/* 2 REG_LPNAV_CTRL_8822B (LOW POWER NAV CONTROL REGISTER) */
#define BIT_LPNAV_EN_8822B BIT(31)

#define BIT_SHIFT_LPNAV_EARLY_8822B 16
#define BIT_MASK_LPNAV_EARLY_8822B 0x7fff
#define BIT_LPNAV_EARLY_8822B(x)                                               \
	(((x) & BIT_MASK_LPNAV_EARLY_8822B) << BIT_SHIFT_LPNAV_EARLY_8822B)
#define BITS_LPNAV_EARLY_8822B                                                 \
	(BIT_MASK_LPNAV_EARLY_8822B << BIT_SHIFT_LPNAV_EARLY_8822B)
#define BIT_CLEAR_LPNAV_EARLY_8822B(x) ((x) & (~BITS_LPNAV_EARLY_8822B))
#define BIT_GET_LPNAV_EARLY_8822B(x)                                           \
	(((x) >> BIT_SHIFT_LPNAV_EARLY_8822B) & BIT_MASK_LPNAV_EARLY_8822B)
#define BIT_SET_LPNAV_EARLY_8822B(x, v)                                        \
	(BIT_CLEAR_LPNAV_EARLY_8822B(x) | BIT_LPNAV_EARLY_8822B(v))

#define BIT_SHIFT_LPNAV_TH_8822B 0
#define BIT_MASK_LPNAV_TH_8822B 0xffff
#define BIT_LPNAV_TH_8822B(x)                                                  \
	(((x) & BIT_MASK_LPNAV_TH_8822B) << BIT_SHIFT_LPNAV_TH_8822B)
#define BITS_LPNAV_TH_8822B                                                    \
	(BIT_MASK_LPNAV_TH_8822B << BIT_SHIFT_LPNAV_TH_8822B)
#define BIT_CLEAR_LPNAV_TH_8822B(x) ((x) & (~BITS_LPNAV_TH_8822B))
#define BIT_GET_LPNAV_TH_8822B(x)                                              \
	(((x) >> BIT_SHIFT_LPNAV_TH_8822B) & BIT_MASK_LPNAV_TH_8822B)
#define BIT_SET_LPNAV_TH_8822B(x, v)                                           \
	(BIT_CLEAR_LPNAV_TH_8822B(x) | BIT_LPNAV_TH_8822B(v))

/* 2 REG_WKFMCAM_CMD_8822B (WAKEUP FRAME CAM COMMAND REGISTER) */
#define BIT_WKFCAM_POLLING_V1_8822B BIT(31)
#define BIT_WKFCAM_CLR_V1_8822B BIT(30)
#define BIT_WKFCAM_WE_8822B BIT(16)

#define BIT_SHIFT_WKFCAM_ADDR_V2_8822B 8
#define BIT_MASK_WKFCAM_ADDR_V2_8822B 0xff
#define BIT_WKFCAM_ADDR_V2_8822B(x)                                            \
	(((x) & BIT_MASK_WKFCAM_ADDR_V2_8822B)                                 \
	 << BIT_SHIFT_WKFCAM_ADDR_V2_8822B)
#define BITS_WKFCAM_ADDR_V2_8822B                                              \
	(BIT_MASK_WKFCAM_ADDR_V2_8822B << BIT_SHIFT_WKFCAM_ADDR_V2_8822B)
#define BIT_CLEAR_WKFCAM_ADDR_V2_8822B(x) ((x) & (~BITS_WKFCAM_ADDR_V2_8822B))
#define BIT_GET_WKFCAM_ADDR_V2_8822B(x)                                        \
	(((x) >> BIT_SHIFT_WKFCAM_ADDR_V2_8822B) &                             \
	 BIT_MASK_WKFCAM_ADDR_V2_8822B)
#define BIT_SET_WKFCAM_ADDR_V2_8822B(x, v)                                     \
	(BIT_CLEAR_WKFCAM_ADDR_V2_8822B(x) | BIT_WKFCAM_ADDR_V2_8822B(v))

#define BIT_SHIFT_WKFCAM_CAM_NUM_V1_8822B 0
#define BIT_MASK_WKFCAM_CAM_NUM_V1_8822B 0xff
#define BIT_WKFCAM_CAM_NUM_V1_8822B(x)                                         \
	(((x) & BIT_MASK_WKFCAM_CAM_NUM_V1_8822B)                              \
	 << BIT_SHIFT_WKFCAM_CAM_NUM_V1_8822B)
#define BITS_WKFCAM_CAM_NUM_V1_8822B                                           \
	(BIT_MASK_WKFCAM_CAM_NUM_V1_8822B << BIT_SHIFT_WKFCAM_CAM_NUM_V1_8822B)
#define BIT_CLEAR_WKFCAM_CAM_NUM_V1_8822B(x)                                   \
	((x) & (~BITS_WKFCAM_CAM_NUM_V1_8822B))
#define BIT_GET_WKFCAM_CAM_NUM_V1_8822B(x)                                     \
	(((x) >> BIT_SHIFT_WKFCAM_CAM_NUM_V1_8822B) &                          \
	 BIT_MASK_WKFCAM_CAM_NUM_V1_8822B)
#define BIT_SET_WKFCAM_CAM_NUM_V1_8822B(x, v)                                  \
	(BIT_CLEAR_WKFCAM_CAM_NUM_V1_8822B(x) | BIT_WKFCAM_CAM_NUM_V1_8822B(v))

/* 2 REG_WKFMCAM_RWD_8822B (WAKEUP FRAME READ/WRITE DATA) */

#define BIT_SHIFT_WKFMCAM_RWD_8822B 0
#define BIT_MASK_WKFMCAM_RWD_8822B 0xffffffffL
#define BIT_WKFMCAM_RWD_8822B(x)                                               \
	(((x) & BIT_MASK_WKFMCAM_RWD_8822B) << BIT_SHIFT_WKFMCAM_RWD_8822B)
#define BITS_WKFMCAM_RWD_8822B                                                 \
	(BIT_MASK_WKFMCAM_RWD_8822B << BIT_SHIFT_WKFMCAM_RWD_8822B)
#define BIT_CLEAR_WKFMCAM_RWD_8822B(x) ((x) & (~BITS_WKFMCAM_RWD_8822B))
#define BIT_GET_WKFMCAM_RWD_8822B(x)                                           \
	(((x) >> BIT_SHIFT_WKFMCAM_RWD_8822B) & BIT_MASK_WKFMCAM_RWD_8822B)
#define BIT_SET_WKFMCAM_RWD_8822B(x, v)                                        \
	(BIT_CLEAR_WKFMCAM_RWD_8822B(x) | BIT_WKFMCAM_RWD_8822B(v))

/* 2 REG_RXFLTMAP1_8822B (RX FILTER MAP GROUP 1) */
#define BIT_CTRLFLT15EN_8822B BIT(15)
#define BIT_CTRLFLT14EN_8822B BIT(14)
#define BIT_CTRLFLT13EN_8822B BIT(13)
#define BIT_CTRLFLT12EN_8822B BIT(12)
#define BIT_CTRLFLT11EN_8822B BIT(11)
#define BIT_CTRLFLT10EN_8822B BIT(10)
#define BIT_CTRLFLT9EN_8822B BIT(9)
#define BIT_CTRLFLT8EN_8822B BIT(8)
#define BIT_CTRLFLT7EN_8822B BIT(7)
#define BIT_CTRLFLT6EN_8822B BIT(6)
#define BIT_CTRLFLT5EN_8822B BIT(5)
#define BIT_CTRLFLT4EN_8822B BIT(4)
#define BIT_CTRLFLT3EN_8822B BIT(3)
#define BIT_CTRLFLT2EN_8822B BIT(2)
#define BIT_CTRLFLT1EN_8822B BIT(1)
#define BIT_CTRLFLT0EN_8822B BIT(0)

/* 2 REG_RXFLTMAP0_8822B (RX FILTER MAP GROUP 0) */
#define BIT_MGTFLT15EN_8822B BIT(15)
#define BIT_MGTFLT14EN_8822B BIT(14)
#define BIT_MGTFLT13EN_8822B BIT(13)
#define BIT_MGTFLT12EN_8822B BIT(12)
#define BIT_MGTFLT11EN_8822B BIT(11)
#define BIT_MGTFLT10EN_8822B BIT(10)
#define BIT_MGTFLT9EN_8822B BIT(9)
#define BIT_MGTFLT8EN_8822B BIT(8)
#define BIT_MGTFLT7EN_8822B BIT(7)
#define BIT_MGTFLT6EN_8822B BIT(6)
#define BIT_MGTFLT5EN_8822B BIT(5)
#define BIT_MGTFLT4EN_8822B BIT(4)
#define BIT_MGTFLT3EN_8822B BIT(3)
#define BIT_MGTFLT2EN_8822B BIT(2)
#define BIT_MGTFLT1EN_8822B BIT(1)
#define BIT_MGTFLT0EN_8822B BIT(0)

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_RXFLTMAP2_8822B (RX FILTER MAP GROUP 2) */
#define BIT_DATAFLT15EN_8822B BIT(15)
#define BIT_DATAFLT14EN_8822B BIT(14)
#define BIT_DATAFLT13EN_8822B BIT(13)
#define BIT_DATAFLT12EN_8822B BIT(12)
#define BIT_DATAFLT11EN_8822B BIT(11)
#define BIT_DATAFLT10EN_8822B BIT(10)
#define BIT_DATAFLT9EN_8822B BIT(9)
#define BIT_DATAFLT8EN_8822B BIT(8)
#define BIT_DATAFLT7EN_8822B BIT(7)
#define BIT_DATAFLT6EN_8822B BIT(6)
#define BIT_DATAFLT5EN_8822B BIT(5)
#define BIT_DATAFLT4EN_8822B BIT(4)
#define BIT_DATAFLT3EN_8822B BIT(3)
#define BIT_DATAFLT2EN_8822B BIT(2)
#define BIT_DATAFLT1EN_8822B BIT(1)
#define BIT_DATAFLT0EN_8822B BIT(0)

/* 2 REG_BCN_PSR_RPT_8822B (BEACON PARSER REPORT REGISTER) */

#define BIT_SHIFT_DTIM_CNT_8822B 24
#define BIT_MASK_DTIM_CNT_8822B 0xff
#define BIT_DTIM_CNT_8822B(x)                                                  \
	(((x) & BIT_MASK_DTIM_CNT_8822B) << BIT_SHIFT_DTIM_CNT_8822B)
#define BITS_DTIM_CNT_8822B                                                    \
	(BIT_MASK_DTIM_CNT_8822B << BIT_SHIFT_DTIM_CNT_8822B)
#define BIT_CLEAR_DTIM_CNT_8822B(x) ((x) & (~BITS_DTIM_CNT_8822B))
#define BIT_GET_DTIM_CNT_8822B(x)                                              \
	(((x) >> BIT_SHIFT_DTIM_CNT_8822B) & BIT_MASK_DTIM_CNT_8822B)
#define BIT_SET_DTIM_CNT_8822B(x, v)                                           \
	(BIT_CLEAR_DTIM_CNT_8822B(x) | BIT_DTIM_CNT_8822B(v))

#define BIT_SHIFT_DTIM_PERIOD_8822B 16
#define BIT_MASK_DTIM_PERIOD_8822B 0xff
#define BIT_DTIM_PERIOD_8822B(x)                                               \
	(((x) & BIT_MASK_DTIM_PERIOD_8822B) << BIT_SHIFT_DTIM_PERIOD_8822B)
#define BITS_DTIM_PERIOD_8822B                                                 \
	(BIT_MASK_DTIM_PERIOD_8822B << BIT_SHIFT_DTIM_PERIOD_8822B)
#define BIT_CLEAR_DTIM_PERIOD_8822B(x) ((x) & (~BITS_DTIM_PERIOD_8822B))
#define BIT_GET_DTIM_PERIOD_8822B(x)                                           \
	(((x) >> BIT_SHIFT_DTIM_PERIOD_8822B) & BIT_MASK_DTIM_PERIOD_8822B)
#define BIT_SET_DTIM_PERIOD_8822B(x, v)                                        \
	(BIT_CLEAR_DTIM_PERIOD_8822B(x) | BIT_DTIM_PERIOD_8822B(v))

#define BIT_DTIM_8822B BIT(15)
#define BIT_TIM_8822B BIT(14)

#define BIT_SHIFT_PS_AID_0_8822B 0
#define BIT_MASK_PS_AID_0_8822B 0x7ff
#define BIT_PS_AID_0_8822B(x)                                                  \
	(((x) & BIT_MASK_PS_AID_0_8822B) << BIT_SHIFT_PS_AID_0_8822B)
#define BITS_PS_AID_0_8822B                                                    \
	(BIT_MASK_PS_AID_0_8822B << BIT_SHIFT_PS_AID_0_8822B)
#define BIT_CLEAR_PS_AID_0_8822B(x) ((x) & (~BITS_PS_AID_0_8822B))
#define BIT_GET_PS_AID_0_8822B(x)                                              \
	(((x) >> BIT_SHIFT_PS_AID_0_8822B) & BIT_MASK_PS_AID_0_8822B)
#define BIT_SET_PS_AID_0_8822B(x, v)                                           \
	(BIT_CLEAR_PS_AID_0_8822B(x) | BIT_PS_AID_0_8822B(v))

/* 2 REG_FLC_TRPC_8822B (TIMER OF FLC_RPC) */
#define BIT_FLC_RPCT_V1_8822B BIT(7)
#define BIT_MODE_8822B BIT(6)

#define BIT_SHIFT_TRPCD_8822B 0
#define BIT_MASK_TRPCD_8822B 0x3f
#define BIT_TRPCD_8822B(x)                                                     \
	(((x) & BIT_MASK_TRPCD_8822B) << BIT_SHIFT_TRPCD_8822B)
#define BITS_TRPCD_8822B (BIT_MASK_TRPCD_8822B << BIT_SHIFT_TRPCD_8822B)
#define BIT_CLEAR_TRPCD_8822B(x) ((x) & (~BITS_TRPCD_8822B))
#define BIT_GET_TRPCD_8822B(x)                                                 \
	(((x) >> BIT_SHIFT_TRPCD_8822B) & BIT_MASK_TRPCD_8822B)
#define BIT_SET_TRPCD_8822B(x, v)                                              \
	(BIT_CLEAR_TRPCD_8822B(x) | BIT_TRPCD_8822B(v))

/* 2 REG_FLC_PTS_8822B (PKT TYPE SELECTION OF FLC_RPC T) */
#define BIT_CMF_8822B BIT(2)
#define BIT_CCF_8822B BIT(1)
#define BIT_CDF_8822B BIT(0)

/* 2 REG_FLC_RPCT_8822B (FLC_RPC THRESHOLD) */

#define BIT_SHIFT_FLC_RPCT_8822B 0
#define BIT_MASK_FLC_RPCT_8822B 0xff
#define BIT_FLC_RPCT_8822B(x)                                                  \
	(((x) & BIT_MASK_FLC_RPCT_8822B) << BIT_SHIFT_FLC_RPCT_8822B)
#define BITS_FLC_RPCT_8822B                                                    \
	(BIT_MASK_FLC_RPCT_8822B << BIT_SHIFT_FLC_RPCT_8822B)
#define BIT_CLEAR_FLC_RPCT_8822B(x) ((x) & (~BITS_FLC_RPCT_8822B))
#define BIT_GET_FLC_RPCT_8822B(x)                                              \
	(((x) >> BIT_SHIFT_FLC_RPCT_8822B) & BIT_MASK_FLC_RPCT_8822B)
#define BIT_SET_FLC_RPCT_8822B(x, v)                                           \
	(BIT_CLEAR_FLC_RPCT_8822B(x) | BIT_FLC_RPCT_8822B(v))

/* 2 REG_FLC_RPC_8822B (FW LPS CONDITION -- RX PKT COUNTER) */

#define BIT_SHIFT_FLC_RPC_8822B 0
#define BIT_MASK_FLC_RPC_8822B 0xff
#define BIT_FLC_RPC_8822B(x)                                                   \
	(((x) & BIT_MASK_FLC_RPC_8822B) << BIT_SHIFT_FLC_RPC_8822B)
#define BITS_FLC_RPC_8822B (BIT_MASK_FLC_RPC_8822B << BIT_SHIFT_FLC_RPC_8822B)
#define BIT_CLEAR_FLC_RPC_8822B(x) ((x) & (~BITS_FLC_RPC_8822B))
#define BIT_GET_FLC_RPC_8822B(x)                                               \
	(((x) >> BIT_SHIFT_FLC_RPC_8822B) & BIT_MASK_FLC_RPC_8822B)
#define BIT_SET_FLC_RPC_8822B(x, v)                                            \
	(BIT_CLEAR_FLC_RPC_8822B(x) | BIT_FLC_RPC_8822B(v))

/* 2 REG_RXPKTMON_CTRL_8822B */

#define BIT_SHIFT_RXBKQPKT_SEQ_8822B 20
#define BIT_MASK_RXBKQPKT_SEQ_8822B 0xf
#define BIT_RXBKQPKT_SEQ_8822B(x)                                              \
	(((x) & BIT_MASK_RXBKQPKT_SEQ_8822B) << BIT_SHIFT_RXBKQPKT_SEQ_8822B)
#define BITS_RXBKQPKT_SEQ_8822B                                                \
	(BIT_MASK_RXBKQPKT_SEQ_8822B << BIT_SHIFT_RXBKQPKT_SEQ_8822B)
#define BIT_CLEAR_RXBKQPKT_SEQ_8822B(x) ((x) & (~BITS_RXBKQPKT_SEQ_8822B))
#define BIT_GET_RXBKQPKT_SEQ_8822B(x)                                          \
	(((x) >> BIT_SHIFT_RXBKQPKT_SEQ_8822B) & BIT_MASK_RXBKQPKT_SEQ_8822B)
#define BIT_SET_RXBKQPKT_SEQ_8822B(x, v)                                       \
	(BIT_CLEAR_RXBKQPKT_SEQ_8822B(x) | BIT_RXBKQPKT_SEQ_8822B(v))

#define BIT_SHIFT_RXBEQPKT_SEQ_8822B 16
#define BIT_MASK_RXBEQPKT_SEQ_8822B 0xf
#define BIT_RXBEQPKT_SEQ_8822B(x)                                              \
	(((x) & BIT_MASK_RXBEQPKT_SEQ_8822B) << BIT_SHIFT_RXBEQPKT_SEQ_8822B)
#define BITS_RXBEQPKT_SEQ_8822B                                                \
	(BIT_MASK_RXBEQPKT_SEQ_8822B << BIT_SHIFT_RXBEQPKT_SEQ_8822B)
#define BIT_CLEAR_RXBEQPKT_SEQ_8822B(x) ((x) & (~BITS_RXBEQPKT_SEQ_8822B))
#define BIT_GET_RXBEQPKT_SEQ_8822B(x)                                          \
	(((x) >> BIT_SHIFT_RXBEQPKT_SEQ_8822B) & BIT_MASK_RXBEQPKT_SEQ_8822B)
#define BIT_SET_RXBEQPKT_SEQ_8822B(x, v)                                       \
	(BIT_CLEAR_RXBEQPKT_SEQ_8822B(x) | BIT_RXBEQPKT_SEQ_8822B(v))

#define BIT_SHIFT_RXVIQPKT_SEQ_8822B 12
#define BIT_MASK_RXVIQPKT_SEQ_8822B 0xf
#define BIT_RXVIQPKT_SEQ_8822B(x)                                              \
	(((x) & BIT_MASK_RXVIQPKT_SEQ_8822B) << BIT_SHIFT_RXVIQPKT_SEQ_8822B)
#define BITS_RXVIQPKT_SEQ_8822B                                                \
	(BIT_MASK_RXVIQPKT_SEQ_8822B << BIT_SHIFT_RXVIQPKT_SEQ_8822B)
#define BIT_CLEAR_RXVIQPKT_SEQ_8822B(x) ((x) & (~BITS_RXVIQPKT_SEQ_8822B))
#define BIT_GET_RXVIQPKT_SEQ_8822B(x)                                          \
	(((x) >> BIT_SHIFT_RXVIQPKT_SEQ_8822B) & BIT_MASK_RXVIQPKT_SEQ_8822B)
#define BIT_SET_RXVIQPKT_SEQ_8822B(x, v)                                       \
	(BIT_CLEAR_RXVIQPKT_SEQ_8822B(x) | BIT_RXVIQPKT_SEQ_8822B(v))

#define BIT_SHIFT_RXVOQPKT_SEQ_8822B 8
#define BIT_MASK_RXVOQPKT_SEQ_8822B 0xf
#define BIT_RXVOQPKT_SEQ_8822B(x)                                              \
	(((x) & BIT_MASK_RXVOQPKT_SEQ_8822B) << BIT_SHIFT_RXVOQPKT_SEQ_8822B)
#define BITS_RXVOQPKT_SEQ_8822B                                                \
	(BIT_MASK_RXVOQPKT_SEQ_8822B << BIT_SHIFT_RXVOQPKT_SEQ_8822B)
#define BIT_CLEAR_RXVOQPKT_SEQ_8822B(x) ((x) & (~BITS_RXVOQPKT_SEQ_8822B))
#define BIT_GET_RXVOQPKT_SEQ_8822B(x)                                          \
	(((x) >> BIT_SHIFT_RXVOQPKT_SEQ_8822B) & BIT_MASK_RXVOQPKT_SEQ_8822B)
#define BIT_SET_RXVOQPKT_SEQ_8822B(x, v)                                       \
	(BIT_CLEAR_RXVOQPKT_SEQ_8822B(x) | BIT_RXVOQPKT_SEQ_8822B(v))

#define BIT_RXBKQPKT_ERR_8822B BIT(7)
#define BIT_RXBEQPKT_ERR_8822B BIT(6)
#define BIT_RXVIQPKT_ERR_8822B BIT(5)
#define BIT_RXVOQPKT_ERR_8822B BIT(4)
#define BIT_RXDMA_MON_EN_8822B BIT(2)
#define BIT_RXPKT_MON_RST_8822B BIT(1)
#define BIT_RXPKT_MON_EN_8822B BIT(0)

/* 2 REG_STATE_MON_8822B */

#define BIT_SHIFT_STATE_SEL_8822B 24
#define BIT_MASK_STATE_SEL_8822B 0x1f
#define BIT_STATE_SEL_8822B(x)                                                 \
	(((x) & BIT_MASK_STATE_SEL_8822B) << BIT_SHIFT_STATE_SEL_8822B)
#define BITS_STATE_SEL_8822B                                                   \
	(BIT_MASK_STATE_SEL_8822B << BIT_SHIFT_STATE_SEL_8822B)
#define BIT_CLEAR_STATE_SEL_8822B(x) ((x) & (~BITS_STATE_SEL_8822B))
#define BIT_GET_STATE_SEL_8822B(x)                                             \
	(((x) >> BIT_SHIFT_STATE_SEL_8822B) & BIT_MASK_STATE_SEL_8822B)
#define BIT_SET_STATE_SEL_8822B(x, v)                                          \
	(BIT_CLEAR_STATE_SEL_8822B(x) | BIT_STATE_SEL_8822B(v))

#define BIT_SHIFT_STATE_INFO_8822B 8
#define BIT_MASK_STATE_INFO_8822B 0xff
#define BIT_STATE_INFO_8822B(x)                                                \
	(((x) & BIT_MASK_STATE_INFO_8822B) << BIT_SHIFT_STATE_INFO_8822B)
#define BITS_STATE_INFO_8822B                                                  \
	(BIT_MASK_STATE_INFO_8822B << BIT_SHIFT_STATE_INFO_8822B)
#define BIT_CLEAR_STATE_INFO_8822B(x) ((x) & (~BITS_STATE_INFO_8822B))
#define BIT_GET_STATE_INFO_8822B(x)                                            \
	(((x) >> BIT_SHIFT_STATE_INFO_8822B) & BIT_MASK_STATE_INFO_8822B)
#define BIT_SET_STATE_INFO_8822B(x, v)                                         \
	(BIT_CLEAR_STATE_INFO_8822B(x) | BIT_STATE_INFO_8822B(v))

#define BIT_UPD_NXT_STATE_8822B BIT(7)

#define BIT_SHIFT_CUR_STATE_8822B 0
#define BIT_MASK_CUR_STATE_8822B 0x7f
#define BIT_CUR_STATE_8822B(x)                                                 \
	(((x) & BIT_MASK_CUR_STATE_8822B) << BIT_SHIFT_CUR_STATE_8822B)
#define BITS_CUR_STATE_8822B                                                   \
	(BIT_MASK_CUR_STATE_8822B << BIT_SHIFT_CUR_STATE_8822B)
#define BIT_CLEAR_CUR_STATE_8822B(x) ((x) & (~BITS_CUR_STATE_8822B))
#define BIT_GET_CUR_STATE_8822B(x)                                             \
	(((x) >> BIT_SHIFT_CUR_STATE_8822B) & BIT_MASK_CUR_STATE_8822B)
#define BIT_SET_CUR_STATE_8822B(x, v)                                          \
	(BIT_CLEAR_CUR_STATE_8822B(x) | BIT_CUR_STATE_8822B(v))

/* 2 REG_ERROR_MON_8822B */
#define BIT_MACRX_ERR_1_8822B BIT(17)
#define BIT_MACRX_ERR_0_8822B BIT(16)
#define BIT_MACTX_ERR_3_8822B BIT(3)
#define BIT_MACTX_ERR_2_8822B BIT(2)
#define BIT_MACTX_ERR_1_8822B BIT(1)
#define BIT_MACTX_ERR_0_8822B BIT(0)

/* 2 REG_SEARCH_MACID_8822B */
#define BIT_EN_TXRPTBUF_CLK_8822B BIT(31)

#define BIT_SHIFT_INFO_INDEX_OFFSET_8822B 16
#define BIT_MASK_INFO_INDEX_OFFSET_8822B 0x1fff
#define BIT_INFO_INDEX_OFFSET_8822B(x)                                         \
	(((x) & BIT_MASK_INFO_INDEX_OFFSET_8822B)                              \
	 << BIT_SHIFT_INFO_INDEX_OFFSET_8822B)
#define BITS_INFO_INDEX_OFFSET_8822B                                           \
	(BIT_MASK_INFO_INDEX_OFFSET_8822B << BIT_SHIFT_INFO_INDEX_OFFSET_8822B)
#define BIT_CLEAR_INFO_INDEX_OFFSET_8822B(x)                                   \
	((x) & (~BITS_INFO_INDEX_OFFSET_8822B))
#define BIT_GET_INFO_INDEX_OFFSET_8822B(x)                                     \
	(((x) >> BIT_SHIFT_INFO_INDEX_OFFSET_8822B) &                          \
	 BIT_MASK_INFO_INDEX_OFFSET_8822B)
#define BIT_SET_INFO_INDEX_OFFSET_8822B(x, v)                                  \
	(BIT_CLEAR_INFO_INDEX_OFFSET_8822B(x) | BIT_INFO_INDEX_OFFSET_8822B(v))

#define BIT_WMAC_SRCH_FIFOFULL_8822B BIT(15)
#define BIT_DIS_INFOSRCH_8822B BIT(14)
#define BIT_DISABLE_B0_8822B BIT(13)

#define BIT_SHIFT_INFO_ADDR_OFFSET_8822B 0
#define BIT_MASK_INFO_ADDR_OFFSET_8822B 0x1fff
#define BIT_INFO_ADDR_OFFSET_8822B(x)                                          \
	(((x) & BIT_MASK_INFO_ADDR_OFFSET_8822B)                               \
	 << BIT_SHIFT_INFO_ADDR_OFFSET_8822B)
#define BITS_INFO_ADDR_OFFSET_8822B                                            \
	(BIT_MASK_INFO_ADDR_OFFSET_8822B << BIT_SHIFT_INFO_ADDR_OFFSET_8822B)
#define BIT_CLEAR_INFO_ADDR_OFFSET_8822B(x)                                    \
	((x) & (~BITS_INFO_ADDR_OFFSET_8822B))
#define BIT_GET_INFO_ADDR_OFFSET_8822B(x)                                      \
	(((x) >> BIT_SHIFT_INFO_ADDR_OFFSET_8822B) &                           \
	 BIT_MASK_INFO_ADDR_OFFSET_8822B)
#define BIT_SET_INFO_ADDR_OFFSET_8822B(x, v)                                   \
	(BIT_CLEAR_INFO_ADDR_OFFSET_8822B(x) | BIT_INFO_ADDR_OFFSET_8822B(v))

/* 2 REG_BT_COEX_TABLE_8822B (BT-COEXISTENCE CONTROL REGISTER) */
#define BIT_PRI_MASK_RX_RESP_8822B BIT(126)
#define BIT_PRI_MASK_RXOFDM_8822B BIT(125)
#define BIT_PRI_MASK_RXCCK_8822B BIT(124)

#define BIT_SHIFT_PRI_MASK_TXAC_8822B (117 & CPU_OPT_WIDTH)
#define BIT_MASK_PRI_MASK_TXAC_8822B 0x7f
#define BIT_PRI_MASK_TXAC_8822B(x)                                             \
	(((x) & BIT_MASK_PRI_MASK_TXAC_8822B) << BIT_SHIFT_PRI_MASK_TXAC_8822B)
#define BITS_PRI_MASK_TXAC_8822B                                               \
	(BIT_MASK_PRI_MASK_TXAC_8822B << BIT_SHIFT_PRI_MASK_TXAC_8822B)
#define BIT_CLEAR_PRI_MASK_TXAC_8822B(x) ((x) & (~BITS_PRI_MASK_TXAC_8822B))
#define BIT_GET_PRI_MASK_TXAC_8822B(x)                                         \
	(((x) >> BIT_SHIFT_PRI_MASK_TXAC_8822B) & BIT_MASK_PRI_MASK_TXAC_8822B)
#define BIT_SET_PRI_MASK_TXAC_8822B(x, v)                                      \
	(BIT_CLEAR_PRI_MASK_TXAC_8822B(x) | BIT_PRI_MASK_TXAC_8822B(v))

#define BIT_SHIFT_PRI_MASK_NAV_8822B (109 & CPU_OPT_WIDTH)
#define BIT_MASK_PRI_MASK_NAV_8822B 0xff
#define BIT_PRI_MASK_NAV_8822B(x)                                              \
	(((x) & BIT_MASK_PRI_MASK_NAV_8822B) << BIT_SHIFT_PRI_MASK_NAV_8822B)
#define BITS_PRI_MASK_NAV_8822B                                                \
	(BIT_MASK_PRI_MASK_NAV_8822B << BIT_SHIFT_PRI_MASK_NAV_8822B)
#define BIT_CLEAR_PRI_MASK_NAV_8822B(x) ((x) & (~BITS_PRI_MASK_NAV_8822B))
#define BIT_GET_PRI_MASK_NAV_8822B(x)                                          \
	(((x) >> BIT_SHIFT_PRI_MASK_NAV_8822B) & BIT_MASK_PRI_MASK_NAV_8822B)
#define BIT_SET_PRI_MASK_NAV_8822B(x, v)                                       \
	(BIT_CLEAR_PRI_MASK_NAV_8822B(x) | BIT_PRI_MASK_NAV_8822B(v))

#define BIT_PRI_MASK_CCK_8822B BIT(108)
#define BIT_PRI_MASK_OFDM_8822B BIT(107)
#define BIT_PRI_MASK_RTY_8822B BIT(106)

#define BIT_SHIFT_PRI_MASK_NUM_8822B (102 & CPU_OPT_WIDTH)
#define BIT_MASK_PRI_MASK_NUM_8822B 0xf
#define BIT_PRI_MASK_NUM_8822B(x)                                              \
	(((x) & BIT_MASK_PRI_MASK_NUM_8822B) << BIT_SHIFT_PRI_MASK_NUM_8822B)
#define BITS_PRI_MASK_NUM_8822B                                                \
	(BIT_MASK_PRI_MASK_NUM_8822B << BIT_SHIFT_PRI_MASK_NUM_8822B)
#define BIT_CLEAR_PRI_MASK_NUM_8822B(x) ((x) & (~BITS_PRI_MASK_NUM_8822B))
#define BIT_GET_PRI_MASK_NUM_8822B(x)                                          \
	(((x) >> BIT_SHIFT_PRI_MASK_NUM_8822B) & BIT_MASK_PRI_MASK_NUM_8822B)
#define BIT_SET_PRI_MASK_NUM_8822B(x, v)                                       \
	(BIT_CLEAR_PRI_MASK_NUM_8822B(x) | BIT_PRI_MASK_NUM_8822B(v))

#define BIT_SHIFT_PRI_MASK_TYPE_8822B (98 & CPU_OPT_WIDTH)
#define BIT_MASK_PRI_MASK_TYPE_8822B 0xf
#define BIT_PRI_MASK_TYPE_8822B(x)                                             \
	(((x) & BIT_MASK_PRI_MASK_TYPE_8822B) << BIT_SHIFT_PRI_MASK_TYPE_8822B)
#define BITS_PRI_MASK_TYPE_8822B                                               \
	(BIT_MASK_PRI_MASK_TYPE_8822B << BIT_SHIFT_PRI_MASK_TYPE_8822B)
#define BIT_CLEAR_PRI_MASK_TYPE_8822B(x) ((x) & (~BITS_PRI_MASK_TYPE_8822B))
#define BIT_GET_PRI_MASK_TYPE_8822B(x)                                         \
	(((x) >> BIT_SHIFT_PRI_MASK_TYPE_8822B) & BIT_MASK_PRI_MASK_TYPE_8822B)
#define BIT_SET_PRI_MASK_TYPE_8822B(x, v)                                      \
	(BIT_CLEAR_PRI_MASK_TYPE_8822B(x) | BIT_PRI_MASK_TYPE_8822B(v))

#define BIT_OOB_8822B BIT(97)
#define BIT_ANT_SEL_8822B BIT(96)

#define BIT_SHIFT_BREAK_TABLE_2_8822B (80 & CPU_OPT_WIDTH)
#define BIT_MASK_BREAK_TABLE_2_8822B 0xffff
#define BIT_BREAK_TABLE_2_8822B(x)                                             \
	(((x) & BIT_MASK_BREAK_TABLE_2_8822B) << BIT_SHIFT_BREAK_TABLE_2_8822B)
#define BITS_BREAK_TABLE_2_8822B                                               \
	(BIT_MASK_BREAK_TABLE_2_8822B << BIT_SHIFT_BREAK_TABLE_2_8822B)
#define BIT_CLEAR_BREAK_TABLE_2_8822B(x) ((x) & (~BITS_BREAK_TABLE_2_8822B))
#define BIT_GET_BREAK_TABLE_2_8822B(x)                                         \
	(((x) >> BIT_SHIFT_BREAK_TABLE_2_8822B) & BIT_MASK_BREAK_TABLE_2_8822B)
#define BIT_SET_BREAK_TABLE_2_8822B(x, v)                                      \
	(BIT_CLEAR_BREAK_TABLE_2_8822B(x) | BIT_BREAK_TABLE_2_8822B(v))

#define BIT_SHIFT_BREAK_TABLE_1_8822B (64 & CPU_OPT_WIDTH)
#define BIT_MASK_BREAK_TABLE_1_8822B 0xffff
#define BIT_BREAK_TABLE_1_8822B(x)                                             \
	(((x) & BIT_MASK_BREAK_TABLE_1_8822B) << BIT_SHIFT_BREAK_TABLE_1_8822B)
#define BITS_BREAK_TABLE_1_8822B                                               \
	(BIT_MASK_BREAK_TABLE_1_8822B << BIT_SHIFT_BREAK_TABLE_1_8822B)
#define BIT_CLEAR_BREAK_TABLE_1_8822B(x) ((x) & (~BITS_BREAK_TABLE_1_8822B))
#define BIT_GET_BREAK_TABLE_1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_BREAK_TABLE_1_8822B) & BIT_MASK_BREAK_TABLE_1_8822B)
#define BIT_SET_BREAK_TABLE_1_8822B(x, v)                                      \
	(BIT_CLEAR_BREAK_TABLE_1_8822B(x) | BIT_BREAK_TABLE_1_8822B(v))

#define BIT_SHIFT_COEX_TABLE_2_8822B (32 & CPU_OPT_WIDTH)
#define BIT_MASK_COEX_TABLE_2_8822B 0xffffffffL
#define BIT_COEX_TABLE_2_8822B(x)                                              \
	(((x) & BIT_MASK_COEX_TABLE_2_8822B) << BIT_SHIFT_COEX_TABLE_2_8822B)
#define BITS_COEX_TABLE_2_8822B                                                \
	(BIT_MASK_COEX_TABLE_2_8822B << BIT_SHIFT_COEX_TABLE_2_8822B)
#define BIT_CLEAR_COEX_TABLE_2_8822B(x) ((x) & (~BITS_COEX_TABLE_2_8822B))
#define BIT_GET_COEX_TABLE_2_8822B(x)                                          \
	(((x) >> BIT_SHIFT_COEX_TABLE_2_8822B) & BIT_MASK_COEX_TABLE_2_8822B)
#define BIT_SET_COEX_TABLE_2_8822B(x, v)                                       \
	(BIT_CLEAR_COEX_TABLE_2_8822B(x) | BIT_COEX_TABLE_2_8822B(v))

#define BIT_SHIFT_COEX_TABLE_1_8822B 0
#define BIT_MASK_COEX_TABLE_1_8822B 0xffffffffL
#define BIT_COEX_TABLE_1_8822B(x)                                              \
	(((x) & BIT_MASK_COEX_TABLE_1_8822B) << BIT_SHIFT_COEX_TABLE_1_8822B)
#define BITS_COEX_TABLE_1_8822B                                                \
	(BIT_MASK_COEX_TABLE_1_8822B << BIT_SHIFT_COEX_TABLE_1_8822B)
#define BIT_CLEAR_COEX_TABLE_1_8822B(x) ((x) & (~BITS_COEX_TABLE_1_8822B))
#define BIT_GET_COEX_TABLE_1_8822B(x)                                          \
	(((x) >> BIT_SHIFT_COEX_TABLE_1_8822B) & BIT_MASK_COEX_TABLE_1_8822B)
#define BIT_SET_COEX_TABLE_1_8822B(x, v)                                       \
	(BIT_CLEAR_COEX_TABLE_1_8822B(x) | BIT_COEX_TABLE_1_8822B(v))

/* 2 REG_RXCMD_0_8822B */
#define BIT_RXCMD_EN_8822B BIT(31)

#define BIT_SHIFT_RXCMD_INFO_8822B 0
#define BIT_MASK_RXCMD_INFO_8822B 0x7fffffffL
#define BIT_RXCMD_INFO_8822B(x)                                                \
	(((x) & BIT_MASK_RXCMD_INFO_8822B) << BIT_SHIFT_RXCMD_INFO_8822B)
#define BITS_RXCMD_INFO_8822B                                                  \
	(BIT_MASK_RXCMD_INFO_8822B << BIT_SHIFT_RXCMD_INFO_8822B)
#define BIT_CLEAR_RXCMD_INFO_8822B(x) ((x) & (~BITS_RXCMD_INFO_8822B))
#define BIT_GET_RXCMD_INFO_8822B(x)                                            \
	(((x) >> BIT_SHIFT_RXCMD_INFO_8822B) & BIT_MASK_RXCMD_INFO_8822B)
#define BIT_SET_RXCMD_INFO_8822B(x, v)                                         \
	(BIT_CLEAR_RXCMD_INFO_8822B(x) | BIT_RXCMD_INFO_8822B(v))

/* 2 REG_RXCMD_1_8822B */

#define BIT_SHIFT_RXCMD_PRD_8822B 0
#define BIT_MASK_RXCMD_PRD_8822B 0xffff
#define BIT_RXCMD_PRD_8822B(x)                                                 \
	(((x) & BIT_MASK_RXCMD_PRD_8822B) << BIT_SHIFT_RXCMD_PRD_8822B)
#define BITS_RXCMD_PRD_8822B                                                   \
	(BIT_MASK_RXCMD_PRD_8822B << BIT_SHIFT_RXCMD_PRD_8822B)
#define BIT_CLEAR_RXCMD_PRD_8822B(x) ((x) & (~BITS_RXCMD_PRD_8822B))
#define BIT_GET_RXCMD_PRD_8822B(x)                                             \
	(((x) >> BIT_SHIFT_RXCMD_PRD_8822B) & BIT_MASK_RXCMD_PRD_8822B)
#define BIT_SET_RXCMD_PRD_8822B(x, v)                                          \
	(BIT_CLEAR_RXCMD_PRD_8822B(x) | BIT_RXCMD_PRD_8822B(v))

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_WMAC_RESP_TXINFO_8822B (RESPONSE TXINFO REGISTER) */

#define BIT_SHIFT_WMAC_RESP_MFB_8822B 25
#define BIT_MASK_WMAC_RESP_MFB_8822B 0x7f
#define BIT_WMAC_RESP_MFB_8822B(x)                                             \
	(((x) & BIT_MASK_WMAC_RESP_MFB_8822B) << BIT_SHIFT_WMAC_RESP_MFB_8822B)
#define BITS_WMAC_RESP_MFB_8822B                                               \
	(BIT_MASK_WMAC_RESP_MFB_8822B << BIT_SHIFT_WMAC_RESP_MFB_8822B)
#define BIT_CLEAR_WMAC_RESP_MFB_8822B(x) ((x) & (~BITS_WMAC_RESP_MFB_8822B))
#define BIT_GET_WMAC_RESP_MFB_8822B(x)                                         \
	(((x) >> BIT_SHIFT_WMAC_RESP_MFB_8822B) & BIT_MASK_WMAC_RESP_MFB_8822B)
#define BIT_SET_WMAC_RESP_MFB_8822B(x, v)                                      \
	(BIT_CLEAR_WMAC_RESP_MFB_8822B(x) | BIT_WMAC_RESP_MFB_8822B(v))

#define BIT_SHIFT_WMAC_ANTINF_SEL_8822B 23
#define BIT_MASK_WMAC_ANTINF_SEL_8822B 0x3
#define BIT_WMAC_ANTINF_SEL_8822B(x)                                           \
	(((x) & BIT_MASK_WMAC_ANTINF_SEL_8822B)                                \
	 << BIT_SHIFT_WMAC_ANTINF_SEL_8822B)
#define BITS_WMAC_ANTINF_SEL_8822B                                             \
	(BIT_MASK_WMAC_ANTINF_SEL_8822B << BIT_SHIFT_WMAC_ANTINF_SEL_8822B)
#define BIT_CLEAR_WMAC_ANTINF_SEL_8822B(x) ((x) & (~BITS_WMAC_ANTINF_SEL_8822B))
#define BIT_GET_WMAC_ANTINF_SEL_8822B(x)                                       \
	(((x) >> BIT_SHIFT_WMAC_ANTINF_SEL_8822B) &                            \
	 BIT_MASK_WMAC_ANTINF_SEL_8822B)
#define BIT_SET_WMAC_ANTINF_SEL_8822B(x, v)                                    \
	(BIT_CLEAR_WMAC_ANTINF_SEL_8822B(x) | BIT_WMAC_ANTINF_SEL_8822B(v))

#define BIT_SHIFT_WMAC_ANTSEL_SEL_8822B 21
#define BIT_MASK_WMAC_ANTSEL_SEL_8822B 0x3
#define BIT_WMAC_ANTSEL_SEL_8822B(x)                                           \
	(((x) & BIT_MASK_WMAC_ANTSEL_SEL_8822B)                                \
	 << BIT_SHIFT_WMAC_ANTSEL_SEL_8822B)
#define BITS_WMAC_ANTSEL_SEL_8822B                                             \
	(BIT_MASK_WMAC_ANTSEL_SEL_8822B << BIT_SHIFT_WMAC_ANTSEL_SEL_8822B)
#define BIT_CLEAR_WMAC_ANTSEL_SEL_8822B(x) ((x) & (~BITS_WMAC_ANTSEL_SEL_8822B))
#define BIT_GET_WMAC_ANTSEL_SEL_8822B(x)                                       \
	(((x) >> BIT_SHIFT_WMAC_ANTSEL_SEL_8822B) &                            \
	 BIT_MASK_WMAC_ANTSEL_SEL_8822B)
#define BIT_SET_WMAC_ANTSEL_SEL_8822B(x, v)                                    \
	(BIT_CLEAR_WMAC_ANTSEL_SEL_8822B(x) | BIT_WMAC_ANTSEL_SEL_8822B(v))

#define BIT_SHIFT_R_WMAC_RESP_TXPOWER_8822B 18
#define BIT_MASK_R_WMAC_RESP_TXPOWER_8822B 0x7
#define BIT_R_WMAC_RESP_TXPOWER_8822B(x)                                       \
	(((x) & BIT_MASK_R_WMAC_RESP_TXPOWER_8822B)                            \
	 << BIT_SHIFT_R_WMAC_RESP_TXPOWER_8822B)
#define BITS_R_WMAC_RESP_TXPOWER_8822B                                         \
	(BIT_MASK_R_WMAC_RESP_TXPOWER_8822B                                    \
	 << BIT_SHIFT_R_WMAC_RESP_TXPOWER_8822B)
#define BIT_CLEAR_R_WMAC_RESP_TXPOWER_8822B(x)                                 \
	((x) & (~BITS_R_WMAC_RESP_TXPOWER_8822B))
#define BIT_GET_R_WMAC_RESP_TXPOWER_8822B(x)                                   \
	(((x) >> BIT_SHIFT_R_WMAC_RESP_TXPOWER_8822B) &                        \
	 BIT_MASK_R_WMAC_RESP_TXPOWER_8822B)
#define BIT_SET_R_WMAC_RESP_TXPOWER_8822B(x, v)                                \
	(BIT_CLEAR_R_WMAC_RESP_TXPOWER_8822B(x) |                              \
	 BIT_R_WMAC_RESP_TXPOWER_8822B(v))

#define BIT_SHIFT_WMAC_RESP_TXANT_8822B 0
#define BIT_MASK_WMAC_RESP_TXANT_8822B 0x3ffff
#define BIT_WMAC_RESP_TXANT_8822B(x)                                           \
	(((x) & BIT_MASK_WMAC_RESP_TXANT_8822B)                                \
	 << BIT_SHIFT_WMAC_RESP_TXANT_8822B)
#define BITS_WMAC_RESP_TXANT_8822B                                             \
	(BIT_MASK_WMAC_RESP_TXANT_8822B << BIT_SHIFT_WMAC_RESP_TXANT_8822B)
#define BIT_CLEAR_WMAC_RESP_TXANT_8822B(x) ((x) & (~BITS_WMAC_RESP_TXANT_8822B))
#define BIT_GET_WMAC_RESP_TXANT_8822B(x)                                       \
	(((x) >> BIT_SHIFT_WMAC_RESP_TXANT_8822B) &                            \
	 BIT_MASK_WMAC_RESP_TXANT_8822B)
#define BIT_SET_WMAC_RESP_TXANT_8822B(x, v)                                    \
	(BIT_CLEAR_WMAC_RESP_TXANT_8822B(x) | BIT_WMAC_RESP_TXANT_8822B(v))

/* 2 REG_BBPSF_CTRL_8822B */
#define BIT_CTL_IDLE_CLR_CSI_RPT_8822B BIT(31)
#define BIT_WMAC_USE_NDPARATE_8822B BIT(30)

#define BIT_SHIFT_WMAC_CSI_RATE_8822B 24
#define BIT_MASK_WMAC_CSI_RATE_8822B 0x3f
#define BIT_WMAC_CSI_RATE_8822B(x)                                             \
	(((x) & BIT_MASK_WMAC_CSI_RATE_8822B) << BIT_SHIFT_WMAC_CSI_RATE_8822B)
#define BITS_WMAC_CSI_RATE_8822B                                               \
	(BIT_MASK_WMAC_CSI_RATE_8822B << BIT_SHIFT_WMAC_CSI_RATE_8822B)
#define BIT_CLEAR_WMAC_CSI_RATE_8822B(x) ((x) & (~BITS_WMAC_CSI_RATE_8822B))
#define BIT_GET_WMAC_CSI_RATE_8822B(x)                                         \
	(((x) >> BIT_SHIFT_WMAC_CSI_RATE_8822B) & BIT_MASK_WMAC_CSI_RATE_8822B)
#define BIT_SET_WMAC_CSI_RATE_8822B(x, v)                                      \
	(BIT_CLEAR_WMAC_CSI_RATE_8822B(x) | BIT_WMAC_CSI_RATE_8822B(v))

#define BIT_SHIFT_WMAC_RESP_TXRATE_8822B 16
#define BIT_MASK_WMAC_RESP_TXRATE_8822B 0xff
#define BIT_WMAC_RESP_TXRATE_8822B(x)                                          \
	(((x) & BIT_MASK_WMAC_RESP_TXRATE_8822B)                               \
	 << BIT_SHIFT_WMAC_RESP_TXRATE_8822B)
#define BITS_WMAC_RESP_TXRATE_8822B                                            \
	(BIT_MASK_WMAC_RESP_TXRATE_8822B << BIT_SHIFT_WMAC_RESP_TXRATE_8822B)
#define BIT_CLEAR_WMAC_RESP_TXRATE_8822B(x)                                    \
	((x) & (~BITS_WMAC_RESP_TXRATE_8822B))
#define BIT_GET_WMAC_RESP_TXRATE_8822B(x)                                      \
	(((x) >> BIT_SHIFT_WMAC_RESP_TXRATE_8822B) &                           \
	 BIT_MASK_WMAC_RESP_TXRATE_8822B)
#define BIT_SET_WMAC_RESP_TXRATE_8822B(x, v)                                   \
	(BIT_CLEAR_WMAC_RESP_TXRATE_8822B(x) | BIT_WMAC_RESP_TXRATE_8822B(v))

#define BIT_BBPSF_MPDUCHKEN_8822B BIT(5)
#define BIT_BBPSF_MHCHKEN_8822B BIT(4)
#define BIT_BBPSF_ERRCHKEN_8822B BIT(3)

#define BIT_SHIFT_BBPSF_ERRTHR_8822B 0
#define BIT_MASK_BBPSF_ERRTHR_8822B 0x7
#define BIT_BBPSF_ERRTHR_8822B(x)                                              \
	(((x) & BIT_MASK_BBPSF_ERRTHR_8822B) << BIT_SHIFT_BBPSF_ERRTHR_8822B)
#define BITS_BBPSF_ERRTHR_8822B                                                \
	(BIT_MASK_BBPSF_ERRTHR_8822B << BIT_SHIFT_BBPSF_ERRTHR_8822B)
#define BIT_CLEAR_BBPSF_ERRTHR_8822B(x) ((x) & (~BITS_BBPSF_ERRTHR_8822B))
#define BIT_GET_BBPSF_ERRTHR_8822B(x)                                          \
	(((x) >> BIT_SHIFT_BBPSF_ERRTHR_8822B) & BIT_MASK_BBPSF_ERRTHR_8822B)
#define BIT_SET_BBPSF_ERRTHR_8822B(x, v)                                       \
	(BIT_CLEAR_BBPSF_ERRTHR_8822B(x) | BIT_BBPSF_ERRTHR_8822B(v))

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_P2P_RX_BCN_NOA_8822B (P2P RX BEACON NOA REGISTER) */
#define BIT_NOA_PARSER_EN_8822B BIT(15)
#define BIT_BSSID_SEL_8822B BIT(14)

#define BIT_SHIFT_P2P_OUI_TYPE_8822B 0
#define BIT_MASK_P2P_OUI_TYPE_8822B 0xff
#define BIT_P2P_OUI_TYPE_8822B(x)                                              \
	(((x) & BIT_MASK_P2P_OUI_TYPE_8822B) << BIT_SHIFT_P2P_OUI_TYPE_8822B)
#define BITS_P2P_OUI_TYPE_8822B                                                \
	(BIT_MASK_P2P_OUI_TYPE_8822B << BIT_SHIFT_P2P_OUI_TYPE_8822B)
#define BIT_CLEAR_P2P_OUI_TYPE_8822B(x) ((x) & (~BITS_P2P_OUI_TYPE_8822B))
#define BIT_GET_P2P_OUI_TYPE_8822B(x)                                          \
	(((x) >> BIT_SHIFT_P2P_OUI_TYPE_8822B) & BIT_MASK_P2P_OUI_TYPE_8822B)
#define BIT_SET_P2P_OUI_TYPE_8822B(x, v)                                       \
	(BIT_CLEAR_P2P_OUI_TYPE_8822B(x) | BIT_P2P_OUI_TYPE_8822B(v))

/* 2 REG_ASSOCIATED_BFMER0_INFO_8822B (ASSOCIATED BEAMFORMER0 INFO REGISTER) */

#define BIT_SHIFT_R_WMAC_TXCSI_AID0_8822B (48 & CPU_OPT_WIDTH)
#define BIT_MASK_R_WMAC_TXCSI_AID0_8822B 0x1ff
#define BIT_R_WMAC_TXCSI_AID0_8822B(x)                                         \
	(((x) & BIT_MASK_R_WMAC_TXCSI_AID0_8822B)                              \
	 << BIT_SHIFT_R_WMAC_TXCSI_AID0_8822B)
#define BITS_R_WMAC_TXCSI_AID0_8822B                                           \
	(BIT_MASK_R_WMAC_TXCSI_AID0_8822B << BIT_SHIFT_R_WMAC_TXCSI_AID0_8822B)
#define BIT_CLEAR_R_WMAC_TXCSI_AID0_8822B(x)                                   \
	((x) & (~BITS_R_WMAC_TXCSI_AID0_8822B))
#define BIT_GET_R_WMAC_TXCSI_AID0_8822B(x)                                     \
	(((x) >> BIT_SHIFT_R_WMAC_TXCSI_AID0_8822B) &                          \
	 BIT_MASK_R_WMAC_TXCSI_AID0_8822B)
#define BIT_SET_R_WMAC_TXCSI_AID0_8822B(x, v)                                  \
	(BIT_CLEAR_R_WMAC_TXCSI_AID0_8822B(x) | BIT_R_WMAC_TXCSI_AID0_8822B(v))

#define BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R0_8822B 0
#define BIT_MASK_R_WMAC_SOUNDING_RXADD_R0_8822B 0xffffffffffffL
#define BIT_R_WMAC_SOUNDING_RXADD_R0_8822B(x)                                  \
	(((x) & BIT_MASK_R_WMAC_SOUNDING_RXADD_R0_8822B)                       \
	 << BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R0_8822B)
#define BITS_R_WMAC_SOUNDING_RXADD_R0_8822B                                    \
	(BIT_MASK_R_WMAC_SOUNDING_RXADD_R0_8822B                               \
	 << BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R0_8822B)
#define BIT_CLEAR_R_WMAC_SOUNDING_RXADD_R0_8822B(x)                            \
	((x) & (~BITS_R_WMAC_SOUNDING_RXADD_R0_8822B))
#define BIT_GET_R_WMAC_SOUNDING_RXADD_R0_8822B(x)                              \
	(((x) >> BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R0_8822B) &                   \
	 BIT_MASK_R_WMAC_SOUNDING_RXADD_R0_8822B)
#define BIT_SET_R_WMAC_SOUNDING_RXADD_R0_8822B(x, v)                           \
	(BIT_CLEAR_R_WMAC_SOUNDING_RXADD_R0_8822B(x) |                         \
	 BIT_R_WMAC_SOUNDING_RXADD_R0_8822B(v))

/* 2 REG_ASSOCIATED_BFMER1_INFO_8822B (ASSOCIATED BEAMFORMER1 INFO REGISTER) */

#define BIT_SHIFT_R_WMAC_TXCSI_AID1_8822B (48 & CPU_OPT_WIDTH)
#define BIT_MASK_R_WMAC_TXCSI_AID1_8822B 0x1ff
#define BIT_R_WMAC_TXCSI_AID1_8822B(x)                                         \
	(((x) & BIT_MASK_R_WMAC_TXCSI_AID1_8822B)                              \
	 << BIT_SHIFT_R_WMAC_TXCSI_AID1_8822B)
#define BITS_R_WMAC_TXCSI_AID1_8822B                                           \
	(BIT_MASK_R_WMAC_TXCSI_AID1_8822B << BIT_SHIFT_R_WMAC_TXCSI_AID1_8822B)
#define BIT_CLEAR_R_WMAC_TXCSI_AID1_8822B(x)                                   \
	((x) & (~BITS_R_WMAC_TXCSI_AID1_8822B))
#define BIT_GET_R_WMAC_TXCSI_AID1_8822B(x)                                     \
	(((x) >> BIT_SHIFT_R_WMAC_TXCSI_AID1_8822B) &                          \
	 BIT_MASK_R_WMAC_TXCSI_AID1_8822B)
#define BIT_SET_R_WMAC_TXCSI_AID1_8822B(x, v)                                  \
	(BIT_CLEAR_R_WMAC_TXCSI_AID1_8822B(x) | BIT_R_WMAC_TXCSI_AID1_8822B(v))

#define BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R1_8822B 0
#define BIT_MASK_R_WMAC_SOUNDING_RXADD_R1_8822B 0xffffffffffffL
#define BIT_R_WMAC_SOUNDING_RXADD_R1_8822B(x)                                  \
	(((x) & BIT_MASK_R_WMAC_SOUNDING_RXADD_R1_8822B)                       \
	 << BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R1_8822B)
#define BITS_R_WMAC_SOUNDING_RXADD_R1_8822B                                    \
	(BIT_MASK_R_WMAC_SOUNDING_RXADD_R1_8822B                               \
	 << BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R1_8822B)
#define BIT_CLEAR_R_WMAC_SOUNDING_RXADD_R1_8822B(x)                            \
	((x) & (~BITS_R_WMAC_SOUNDING_RXADD_R1_8822B))
#define BIT_GET_R_WMAC_SOUNDING_RXADD_R1_8822B(x)                              \
	(((x) >> BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R1_8822B) &                   \
	 BIT_MASK_R_WMAC_SOUNDING_RXADD_R1_8822B)
#define BIT_SET_R_WMAC_SOUNDING_RXADD_R1_8822B(x, v)                           \
	(BIT_CLEAR_R_WMAC_SOUNDING_RXADD_R1_8822B(x) |                         \
	 BIT_R_WMAC_SOUNDING_RXADD_R1_8822B(v))

/* 2 REG_TX_CSI_RPT_PARAM_BW20_8822B (TX CSI REPORT PARAMETER REGISTER) */

#define BIT_SHIFT_R_WMAC_BFINFO_20M_1_8822B 16
#define BIT_MASK_R_WMAC_BFINFO_20M_1_8822B 0xfff
#define BIT_R_WMAC_BFINFO_20M_1_8822B(x)                                       \
	(((x) & BIT_MASK_R_WMAC_BFINFO_20M_1_8822B)                            \
	 << BIT_SHIFT_R_WMAC_BFINFO_20M_1_8822B)
#define BITS_R_WMAC_BFINFO_20M_1_8822B                                         \
	(BIT_MASK_R_WMAC_BFINFO_20M_1_8822B                                    \
	 << BIT_SHIFT_R_WMAC_BFINFO_20M_1_8822B)
#define BIT_CLEAR_R_WMAC_BFINFO_20M_1_8822B(x)                                 \
	((x) & (~BITS_R_WMAC_BFINFO_20M_1_8822B))
#define BIT_GET_R_WMAC_BFINFO_20M_1_8822B(x)                                   \
	(((x) >> BIT_SHIFT_R_WMAC_BFINFO_20M_1_8822B) &                        \
	 BIT_MASK_R_WMAC_BFINFO_20M_1_8822B)
#define BIT_SET_R_WMAC_BFINFO_20M_1_8822B(x, v)                                \
	(BIT_CLEAR_R_WMAC_BFINFO_20M_1_8822B(x) |                              \
	 BIT_R_WMAC_BFINFO_20M_1_8822B(v))

#define BIT_SHIFT_R_WMAC_BFINFO_20M_0_8822B 0
#define BIT_MASK_R_WMAC_BFINFO_20M_0_8822B 0xfff
#define BIT_R_WMAC_BFINFO_20M_0_8822B(x)                                       \
	(((x) & BIT_MASK_R_WMAC_BFINFO_20M_0_8822B)                            \
	 << BIT_SHIFT_R_WMAC_BFINFO_20M_0_8822B)
#define BITS_R_WMAC_BFINFO_20M_0_8822B                                         \
	(BIT_MASK_R_WMAC_BFINFO_20M_0_8822B                                    \
	 << BIT_SHIFT_R_WMAC_BFINFO_20M_0_8822B)
#define BIT_CLEAR_R_WMAC_BFINFO_20M_0_8822B(x)                                 \
	((x) & (~BITS_R_WMAC_BFINFO_20M_0_8822B))
#define BIT_GET_R_WMAC_BFINFO_20M_0_8822B(x)                                   \
	(((x) >> BIT_SHIFT_R_WMAC_BFINFO_20M_0_8822B) &                        \
	 BIT_MASK_R_WMAC_BFINFO_20M_0_8822B)
#define BIT_SET_R_WMAC_BFINFO_20M_0_8822B(x, v)                                \
	(BIT_CLEAR_R_WMAC_BFINFO_20M_0_8822B(x) |                              \
	 BIT_R_WMAC_BFINFO_20M_0_8822B(v))

/* 2 REG_TX_CSI_RPT_PARAM_BW40_8822B (TX CSI REPORT PARAMETER_BW40 REGISTER) */

#define BIT_SHIFT_WMAC_RESP_ANTCD_8822B 0
#define BIT_MASK_WMAC_RESP_ANTCD_8822B 0xf
#define BIT_WMAC_RESP_ANTCD_8822B(x)                                           \
	(((x) & BIT_MASK_WMAC_RESP_ANTCD_8822B)                                \
	 << BIT_SHIFT_WMAC_RESP_ANTCD_8822B)
#define BITS_WMAC_RESP_ANTCD_8822B                                             \
	(BIT_MASK_WMAC_RESP_ANTCD_8822B << BIT_SHIFT_WMAC_RESP_ANTCD_8822B)
#define BIT_CLEAR_WMAC_RESP_ANTCD_8822B(x) ((x) & (~BITS_WMAC_RESP_ANTCD_8822B))
#define BIT_GET_WMAC_RESP_ANTCD_8822B(x)                                       \
	(((x) >> BIT_SHIFT_WMAC_RESP_ANTCD_8822B) &                            \
	 BIT_MASK_WMAC_RESP_ANTCD_8822B)
#define BIT_SET_WMAC_RESP_ANTCD_8822B(x, v)                                    \
	(BIT_CLEAR_WMAC_RESP_ANTCD_8822B(x) | BIT_WMAC_RESP_ANTCD_8822B(v))

/* 2 REG_TX_CSI_RPT_PARAM_BW80_8822B (TX CSI REPORT PARAMETER_BW80 REGISTER) */

/* 2 REG_BCN_PSR_RPT2_8822B (BEACON PARSER REPORT REGISTER2) */

#define BIT_SHIFT_DTIM_CNT2_8822B 24
#define BIT_MASK_DTIM_CNT2_8822B 0xff
#define BIT_DTIM_CNT2_8822B(x)                                                 \
	(((x) & BIT_MASK_DTIM_CNT2_8822B) << BIT_SHIFT_DTIM_CNT2_8822B)
#define BITS_DTIM_CNT2_8822B                                                   \
	(BIT_MASK_DTIM_CNT2_8822B << BIT_SHIFT_DTIM_CNT2_8822B)
#define BIT_CLEAR_DTIM_CNT2_8822B(x) ((x) & (~BITS_DTIM_CNT2_8822B))
#define BIT_GET_DTIM_CNT2_8822B(x)                                             \
	(((x) >> BIT_SHIFT_DTIM_CNT2_8822B) & BIT_MASK_DTIM_CNT2_8822B)
#define BIT_SET_DTIM_CNT2_8822B(x, v)                                          \
	(BIT_CLEAR_DTIM_CNT2_8822B(x) | BIT_DTIM_CNT2_8822B(v))

#define BIT_SHIFT_DTIM_PERIOD2_8822B 16
#define BIT_MASK_DTIM_PERIOD2_8822B 0xff
#define BIT_DTIM_PERIOD2_8822B(x)                                              \
	(((x) & BIT_MASK_DTIM_PERIOD2_8822B) << BIT_SHIFT_DTIM_PERIOD2_8822B)
#define BITS_DTIM_PERIOD2_8822B                                                \
	(BIT_MASK_DTIM_PERIOD2_8822B << BIT_SHIFT_DTIM_PERIOD2_8822B)
#define BIT_CLEAR_DTIM_PERIOD2_8822B(x) ((x) & (~BITS_DTIM_PERIOD2_8822B))
#define BIT_GET_DTIM_PERIOD2_8822B(x)                                          \
	(((x) >> BIT_SHIFT_DTIM_PERIOD2_8822B) & BIT_MASK_DTIM_PERIOD2_8822B)
#define BIT_SET_DTIM_PERIOD2_8822B(x, v)                                       \
	(BIT_CLEAR_DTIM_PERIOD2_8822B(x) | BIT_DTIM_PERIOD2_8822B(v))

#define BIT_DTIM2_8822B BIT(15)
#define BIT_TIM2_8822B BIT(14)

#define BIT_SHIFT_PS_AID_2_8822B 0
#define BIT_MASK_PS_AID_2_8822B 0x7ff
#define BIT_PS_AID_2_8822B(x)                                                  \
	(((x) & BIT_MASK_PS_AID_2_8822B) << BIT_SHIFT_PS_AID_2_8822B)
#define BITS_PS_AID_2_8822B                                                    \
	(BIT_MASK_PS_AID_2_8822B << BIT_SHIFT_PS_AID_2_8822B)
#define BIT_CLEAR_PS_AID_2_8822B(x) ((x) & (~BITS_PS_AID_2_8822B))
#define BIT_GET_PS_AID_2_8822B(x)                                              \
	(((x) >> BIT_SHIFT_PS_AID_2_8822B) & BIT_MASK_PS_AID_2_8822B)
#define BIT_SET_PS_AID_2_8822B(x, v)                                           \
	(BIT_CLEAR_PS_AID_2_8822B(x) | BIT_PS_AID_2_8822B(v))

/* 2 REG_BCN_PSR_RPT3_8822B (BEACON PARSER REPORT REGISTER3) */

#define BIT_SHIFT_DTIM_CNT3_8822B 24
#define BIT_MASK_DTIM_CNT3_8822B 0xff
#define BIT_DTIM_CNT3_8822B(x)                                                 \
	(((x) & BIT_MASK_DTIM_CNT3_8822B) << BIT_SHIFT_DTIM_CNT3_8822B)
#define BITS_DTIM_CNT3_8822B                                                   \
	(BIT_MASK_DTIM_CNT3_8822B << BIT_SHIFT_DTIM_CNT3_8822B)
#define BIT_CLEAR_DTIM_CNT3_8822B(x) ((x) & (~BITS_DTIM_CNT3_8822B))
#define BIT_GET_DTIM_CNT3_8822B(x)                                             \
	(((x) >> BIT_SHIFT_DTIM_CNT3_8822B) & BIT_MASK_DTIM_CNT3_8822B)
#define BIT_SET_DTIM_CNT3_8822B(x, v)                                          \
	(BIT_CLEAR_DTIM_CNT3_8822B(x) | BIT_DTIM_CNT3_8822B(v))

#define BIT_SHIFT_DTIM_PERIOD3_8822B 16
#define BIT_MASK_DTIM_PERIOD3_8822B 0xff
#define BIT_DTIM_PERIOD3_8822B(x)                                              \
	(((x) & BIT_MASK_DTIM_PERIOD3_8822B) << BIT_SHIFT_DTIM_PERIOD3_8822B)
#define BITS_DTIM_PERIOD3_8822B                                                \
	(BIT_MASK_DTIM_PERIOD3_8822B << BIT_SHIFT_DTIM_PERIOD3_8822B)
#define BIT_CLEAR_DTIM_PERIOD3_8822B(x) ((x) & (~BITS_DTIM_PERIOD3_8822B))
#define BIT_GET_DTIM_PERIOD3_8822B(x)                                          \
	(((x) >> BIT_SHIFT_DTIM_PERIOD3_8822B) & BIT_MASK_DTIM_PERIOD3_8822B)
#define BIT_SET_DTIM_PERIOD3_8822B(x, v)                                       \
	(BIT_CLEAR_DTIM_PERIOD3_8822B(x) | BIT_DTIM_PERIOD3_8822B(v))

#define BIT_DTIM3_8822B BIT(15)
#define BIT_TIM3_8822B BIT(14)

#define BIT_SHIFT_PS_AID_3_8822B 0
#define BIT_MASK_PS_AID_3_8822B 0x7ff
#define BIT_PS_AID_3_8822B(x)                                                  \
	(((x) & BIT_MASK_PS_AID_3_8822B) << BIT_SHIFT_PS_AID_3_8822B)
#define BITS_PS_AID_3_8822B                                                    \
	(BIT_MASK_PS_AID_3_8822B << BIT_SHIFT_PS_AID_3_8822B)
#define BIT_CLEAR_PS_AID_3_8822B(x) ((x) & (~BITS_PS_AID_3_8822B))
#define BIT_GET_PS_AID_3_8822B(x)                                              \
	(((x) >> BIT_SHIFT_PS_AID_3_8822B) & BIT_MASK_PS_AID_3_8822B)
#define BIT_SET_PS_AID_3_8822B(x, v)                                           \
	(BIT_CLEAR_PS_AID_3_8822B(x) | BIT_PS_AID_3_8822B(v))

/* 2 REG_BCN_PSR_RPT4_8822B (BEACON PARSER REPORT REGISTER4) */

#define BIT_SHIFT_DTIM_CNT4_8822B 24
#define BIT_MASK_DTIM_CNT4_8822B 0xff
#define BIT_DTIM_CNT4_8822B(x)                                                 \
	(((x) & BIT_MASK_DTIM_CNT4_8822B) << BIT_SHIFT_DTIM_CNT4_8822B)
#define BITS_DTIM_CNT4_8822B                                                   \
	(BIT_MASK_DTIM_CNT4_8822B << BIT_SHIFT_DTIM_CNT4_8822B)
#define BIT_CLEAR_DTIM_CNT4_8822B(x) ((x) & (~BITS_DTIM_CNT4_8822B))
#define BIT_GET_DTIM_CNT4_8822B(x)                                             \
	(((x) >> BIT_SHIFT_DTIM_CNT4_8822B) & BIT_MASK_DTIM_CNT4_8822B)
#define BIT_SET_DTIM_CNT4_8822B(x, v)                                          \
	(BIT_CLEAR_DTIM_CNT4_8822B(x) | BIT_DTIM_CNT4_8822B(v))

#define BIT_SHIFT_DTIM_PERIOD4_8822B 16
#define BIT_MASK_DTIM_PERIOD4_8822B 0xff
#define BIT_DTIM_PERIOD4_8822B(x)                                              \
	(((x) & BIT_MASK_DTIM_PERIOD4_8822B) << BIT_SHIFT_DTIM_PERIOD4_8822B)
#define BITS_DTIM_PERIOD4_8822B                                                \
	(BIT_MASK_DTIM_PERIOD4_8822B << BIT_SHIFT_DTIM_PERIOD4_8822B)
#define BIT_CLEAR_DTIM_PERIOD4_8822B(x) ((x) & (~BITS_DTIM_PERIOD4_8822B))
#define BIT_GET_DTIM_PERIOD4_8822B(x)                                          \
	(((x) >> BIT_SHIFT_DTIM_PERIOD4_8822B) & BIT_MASK_DTIM_PERIOD4_8822B)
#define BIT_SET_DTIM_PERIOD4_8822B(x, v)                                       \
	(BIT_CLEAR_DTIM_PERIOD4_8822B(x) | BIT_DTIM_PERIOD4_8822B(v))

#define BIT_DTIM4_8822B BIT(15)
#define BIT_TIM4_8822B BIT(14)

#define BIT_SHIFT_PS_AID_4_8822B 0
#define BIT_MASK_PS_AID_4_8822B 0x7ff
#define BIT_PS_AID_4_8822B(x)                                                  \
	(((x) & BIT_MASK_PS_AID_4_8822B) << BIT_SHIFT_PS_AID_4_8822B)
#define BITS_PS_AID_4_8822B                                                    \
	(BIT_MASK_PS_AID_4_8822B << BIT_SHIFT_PS_AID_4_8822B)
#define BIT_CLEAR_PS_AID_4_8822B(x) ((x) & (~BITS_PS_AID_4_8822B))
#define BIT_GET_PS_AID_4_8822B(x)                                              \
	(((x) >> BIT_SHIFT_PS_AID_4_8822B) & BIT_MASK_PS_AID_4_8822B)
#define BIT_SET_PS_AID_4_8822B(x, v)                                           \
	(BIT_CLEAR_PS_AID_4_8822B(x) | BIT_PS_AID_4_8822B(v))

/* 2 REG_A1_ADDR_MASK_8822B (A1 ADDR MASK REGISTER) */

#define BIT_SHIFT_A1_ADDR_MASK_8822B 0
#define BIT_MASK_A1_ADDR_MASK_8822B 0xffffffffL
#define BIT_A1_ADDR_MASK_8822B(x)                                              \
	(((x) & BIT_MASK_A1_ADDR_MASK_8822B) << BIT_SHIFT_A1_ADDR_MASK_8822B)
#define BITS_A1_ADDR_MASK_8822B                                                \
	(BIT_MASK_A1_ADDR_MASK_8822B << BIT_SHIFT_A1_ADDR_MASK_8822B)
#define BIT_CLEAR_A1_ADDR_MASK_8822B(x) ((x) & (~BITS_A1_ADDR_MASK_8822B))
#define BIT_GET_A1_ADDR_MASK_8822B(x)                                          \
	(((x) >> BIT_SHIFT_A1_ADDR_MASK_8822B) & BIT_MASK_A1_ADDR_MASK_8822B)
#define BIT_SET_A1_ADDR_MASK_8822B(x, v)                                       \
	(BIT_CLEAR_A1_ADDR_MASK_8822B(x) | BIT_A1_ADDR_MASK_8822B(v))

/* 2 REG_MACID2_8822B (MAC ID2 REGISTER) */

#define BIT_SHIFT_MACID2_8822B 0
#define BIT_MASK_MACID2_8822B 0xffffffffffffL
#define BIT_MACID2_8822B(x)                                                    \
	(((x) & BIT_MASK_MACID2_8822B) << BIT_SHIFT_MACID2_8822B)
#define BITS_MACID2_8822B (BIT_MASK_MACID2_8822B << BIT_SHIFT_MACID2_8822B)
#define BIT_CLEAR_MACID2_8822B(x) ((x) & (~BITS_MACID2_8822B))
#define BIT_GET_MACID2_8822B(x)                                                \
	(((x) >> BIT_SHIFT_MACID2_8822B) & BIT_MASK_MACID2_8822B)
#define BIT_SET_MACID2_8822B(x, v)                                             \
	(BIT_CLEAR_MACID2_8822B(x) | BIT_MACID2_8822B(v))

/* 2 REG_BSSID2_8822B (BSSID2 REGISTER) */

#define BIT_SHIFT_BSSID2_8822B 0
#define BIT_MASK_BSSID2_8822B 0xffffffffffffL
#define BIT_BSSID2_8822B(x)                                                    \
	(((x) & BIT_MASK_BSSID2_8822B) << BIT_SHIFT_BSSID2_8822B)
#define BITS_BSSID2_8822B (BIT_MASK_BSSID2_8822B << BIT_SHIFT_BSSID2_8822B)
#define BIT_CLEAR_BSSID2_8822B(x) ((x) & (~BITS_BSSID2_8822B))
#define BIT_GET_BSSID2_8822B(x)                                                \
	(((x) >> BIT_SHIFT_BSSID2_8822B) & BIT_MASK_BSSID2_8822B)
#define BIT_SET_BSSID2_8822B(x, v)                                             \
	(BIT_CLEAR_BSSID2_8822B(x) | BIT_BSSID2_8822B(v))

/* 2 REG_MACID3_8822B (MAC ID3 REGISTER) */

#define BIT_SHIFT_MACID3_8822B 0
#define BIT_MASK_MACID3_8822B 0xffffffffffffL
#define BIT_MACID3_8822B(x)                                                    \
	(((x) & BIT_MASK_MACID3_8822B) << BIT_SHIFT_MACID3_8822B)
#define BITS_MACID3_8822B (BIT_MASK_MACID3_8822B << BIT_SHIFT_MACID3_8822B)
#define BIT_CLEAR_MACID3_8822B(x) ((x) & (~BITS_MACID3_8822B))
#define BIT_GET_MACID3_8822B(x)                                                \
	(((x) >> BIT_SHIFT_MACID3_8822B) & BIT_MASK_MACID3_8822B)
#define BIT_SET_MACID3_8822B(x, v)                                             \
	(BIT_CLEAR_MACID3_8822B(x) | BIT_MACID3_8822B(v))

/* 2 REG_BSSID3_8822B (BSSID3 REGISTER) */

#define BIT_SHIFT_BSSID3_8822B 0
#define BIT_MASK_BSSID3_8822B 0xffffffffffffL
#define BIT_BSSID3_8822B(x)                                                    \
	(((x) & BIT_MASK_BSSID3_8822B) << BIT_SHIFT_BSSID3_8822B)
#define BITS_BSSID3_8822B (BIT_MASK_BSSID3_8822B << BIT_SHIFT_BSSID3_8822B)
#define BIT_CLEAR_BSSID3_8822B(x) ((x) & (~BITS_BSSID3_8822B))
#define BIT_GET_BSSID3_8822B(x)                                                \
	(((x) >> BIT_SHIFT_BSSID3_8822B) & BIT_MASK_BSSID3_8822B)
#define BIT_SET_BSSID3_8822B(x, v)                                             \
	(BIT_CLEAR_BSSID3_8822B(x) | BIT_BSSID3_8822B(v))

/* 2 REG_MACID4_8822B (MAC ID4 REGISTER) */

#define BIT_SHIFT_MACID4_8822B 0
#define BIT_MASK_MACID4_8822B 0xffffffffffffL
#define BIT_MACID4_8822B(x)                                                    \
	(((x) & BIT_MASK_MACID4_8822B) << BIT_SHIFT_MACID4_8822B)
#define BITS_MACID4_8822B (BIT_MASK_MACID4_8822B << BIT_SHIFT_MACID4_8822B)
#define BIT_CLEAR_MACID4_8822B(x) ((x) & (~BITS_MACID4_8822B))
#define BIT_GET_MACID4_8822B(x)                                                \
	(((x) >> BIT_SHIFT_MACID4_8822B) & BIT_MASK_MACID4_8822B)
#define BIT_SET_MACID4_8822B(x, v)                                             \
	(BIT_CLEAR_MACID4_8822B(x) | BIT_MACID4_8822B(v))

/* 2 REG_BSSID4_8822B (BSSID4 REGISTER) */

#define BIT_SHIFT_BSSID4_8822B 0
#define BIT_MASK_BSSID4_8822B 0xffffffffffffL
#define BIT_BSSID4_8822B(x)                                                    \
	(((x) & BIT_MASK_BSSID4_8822B) << BIT_SHIFT_BSSID4_8822B)
#define BITS_BSSID4_8822B (BIT_MASK_BSSID4_8822B << BIT_SHIFT_BSSID4_8822B)
#define BIT_CLEAR_BSSID4_8822B(x) ((x) & (~BITS_BSSID4_8822B))
#define BIT_GET_BSSID4_8822B(x)                                                \
	(((x) >> BIT_SHIFT_BSSID4_8822B) & BIT_MASK_BSSID4_8822B)
#define BIT_SET_BSSID4_8822B(x, v)                                             \
	(BIT_CLEAR_BSSID4_8822B(x) | BIT_BSSID4_8822B(v))

/* 2 REG_NOA_REPORT_8822B */

/* 2 REG_PWRBIT_SETTING_8822B */
#define BIT_CLI3_PWRBIT_OW_EN_8822B BIT(7)
#define BIT_CLI3_PWR_ST_8822B BIT(6)
#define BIT_CLI2_PWRBIT_OW_EN_8822B BIT(5)
#define BIT_CLI2_PWR_ST_8822B BIT(4)
#define BIT_CLI1_PWRBIT_OW_EN_8822B BIT(3)
#define BIT_CLI1_PWR_ST_8822B BIT(2)
#define BIT_CLI0_PWRBIT_OW_EN_8822B BIT(1)
#define BIT_CLI0_PWR_ST_8822B BIT(0)

/* 2 REG_WMAC_MU_BF_OPTION_8822B */
#define BIT_WMAC_RESP_NONSTA1_DIS_8822B BIT(7)
#define BIT_BIT_WMAC_TXMU_ACKPOLICY_EN_8822B BIT(6)

#define BIT_SHIFT_WMAC_TXMU_ACKPOLICY_8822B 4
#define BIT_MASK_WMAC_TXMU_ACKPOLICY_8822B 0x3
#define BIT_WMAC_TXMU_ACKPOLICY_8822B(x)                                       \
	(((x) & BIT_MASK_WMAC_TXMU_ACKPOLICY_8822B)                            \
	 << BIT_SHIFT_WMAC_TXMU_ACKPOLICY_8822B)
#define BITS_WMAC_TXMU_ACKPOLICY_8822B                                         \
	(BIT_MASK_WMAC_TXMU_ACKPOLICY_8822B                                    \
	 << BIT_SHIFT_WMAC_TXMU_ACKPOLICY_8822B)
#define BIT_CLEAR_WMAC_TXMU_ACKPOLICY_8822B(x)                                 \
	((x) & (~BITS_WMAC_TXMU_ACKPOLICY_8822B))
#define BIT_GET_WMAC_TXMU_ACKPOLICY_8822B(x)                                   \
	(((x) >> BIT_SHIFT_WMAC_TXMU_ACKPOLICY_8822B) &                        \
	 BIT_MASK_WMAC_TXMU_ACKPOLICY_8822B)
#define BIT_SET_WMAC_TXMU_ACKPOLICY_8822B(x, v)                                \
	(BIT_CLEAR_WMAC_TXMU_ACKPOLICY_8822B(x) |                              \
	 BIT_WMAC_TXMU_ACKPOLICY_8822B(v))

#define BIT_SHIFT_WMAC_MU_BFEE_PORT_SEL_8822B 1
#define BIT_MASK_WMAC_MU_BFEE_PORT_SEL_8822B 0x7
#define BIT_WMAC_MU_BFEE_PORT_SEL_8822B(x)                                     \
	(((x) & BIT_MASK_WMAC_MU_BFEE_PORT_SEL_8822B)                          \
	 << BIT_SHIFT_WMAC_MU_BFEE_PORT_SEL_8822B)
#define BITS_WMAC_MU_BFEE_PORT_SEL_8822B                                       \
	(BIT_MASK_WMAC_MU_BFEE_PORT_SEL_8822B                                  \
	 << BIT_SHIFT_WMAC_MU_BFEE_PORT_SEL_8822B)
#define BIT_CLEAR_WMAC_MU_BFEE_PORT_SEL_8822B(x)                               \
	((x) & (~BITS_WMAC_MU_BFEE_PORT_SEL_8822B))
#define BIT_GET_WMAC_MU_BFEE_PORT_SEL_8822B(x)                                 \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE_PORT_SEL_8822B) &                      \
	 BIT_MASK_WMAC_MU_BFEE_PORT_SEL_8822B)
#define BIT_SET_WMAC_MU_BFEE_PORT_SEL_8822B(x, v)                              \
	(BIT_CLEAR_WMAC_MU_BFEE_PORT_SEL_8822B(x) |                            \
	 BIT_WMAC_MU_BFEE_PORT_SEL_8822B(v))

#define BIT_WMAC_MU_BFEE_DIS_8822B BIT(0)

/* 2 REG_NOT_VALID_8822B */

#define BIT_SHIFT_WMAC_PAUSE_BB_CLR_TH_8822B 0
#define BIT_MASK_WMAC_PAUSE_BB_CLR_TH_8822B 0xff
#define BIT_WMAC_PAUSE_BB_CLR_TH_8822B(x)                                      \
	(((x) & BIT_MASK_WMAC_PAUSE_BB_CLR_TH_8822B)                           \
	 << BIT_SHIFT_WMAC_PAUSE_BB_CLR_TH_8822B)
#define BITS_WMAC_PAUSE_BB_CLR_TH_8822B                                        \
	(BIT_MASK_WMAC_PAUSE_BB_CLR_TH_8822B                                   \
	 << BIT_SHIFT_WMAC_PAUSE_BB_CLR_TH_8822B)
#define BIT_CLEAR_WMAC_PAUSE_BB_CLR_TH_8822B(x)                                \
	((x) & (~BITS_WMAC_PAUSE_BB_CLR_TH_8822B))
#define BIT_GET_WMAC_PAUSE_BB_CLR_TH_8822B(x)                                  \
	(((x) >> BIT_SHIFT_WMAC_PAUSE_BB_CLR_TH_8822B) &                       \
	 BIT_MASK_WMAC_PAUSE_BB_CLR_TH_8822B)
#define BIT_SET_WMAC_PAUSE_BB_CLR_TH_8822B(x, v)                               \
	(BIT_CLEAR_WMAC_PAUSE_BB_CLR_TH_8822B(x) |                             \
	 BIT_WMAC_PAUSE_BB_CLR_TH_8822B(v))

/* 2 REG_WMAC_MU_ARB_8822B */
#define BIT_WMAC_ARB_HW_ADAPT_EN_8822B BIT(7)
#define BIT_WMAC_ARB_SW_EN_8822B BIT(6)

#define BIT_SHIFT_WMAC_ARB_SW_STATE_8822B 0
#define BIT_MASK_WMAC_ARB_SW_STATE_8822B 0x3f
#define BIT_WMAC_ARB_SW_STATE_8822B(x)                                         \
	(((x) & BIT_MASK_WMAC_ARB_SW_STATE_8822B)                              \
	 << BIT_SHIFT_WMAC_ARB_SW_STATE_8822B)
#define BITS_WMAC_ARB_SW_STATE_8822B                                           \
	(BIT_MASK_WMAC_ARB_SW_STATE_8822B << BIT_SHIFT_WMAC_ARB_SW_STATE_8822B)
#define BIT_CLEAR_WMAC_ARB_SW_STATE_8822B(x)                                   \
	((x) & (~BITS_WMAC_ARB_SW_STATE_8822B))
#define BIT_GET_WMAC_ARB_SW_STATE_8822B(x)                                     \
	(((x) >> BIT_SHIFT_WMAC_ARB_SW_STATE_8822B) &                          \
	 BIT_MASK_WMAC_ARB_SW_STATE_8822B)
#define BIT_SET_WMAC_ARB_SW_STATE_8822B(x, v)                                  \
	(BIT_CLEAR_WMAC_ARB_SW_STATE_8822B(x) | BIT_WMAC_ARB_SW_STATE_8822B(v))

/* 2 REG_WMAC_MU_OPTION_8822B */

#define BIT_SHIFT_WMAC_MU_DBGSEL_8822B 5
#define BIT_MASK_WMAC_MU_DBGSEL_8822B 0x3
#define BIT_WMAC_MU_DBGSEL_8822B(x)                                            \
	(((x) & BIT_MASK_WMAC_MU_DBGSEL_8822B)                                 \
	 << BIT_SHIFT_WMAC_MU_DBGSEL_8822B)
#define BITS_WMAC_MU_DBGSEL_8822B                                              \
	(BIT_MASK_WMAC_MU_DBGSEL_8822B << BIT_SHIFT_WMAC_MU_DBGSEL_8822B)
#define BIT_CLEAR_WMAC_MU_DBGSEL_8822B(x) ((x) & (~BITS_WMAC_MU_DBGSEL_8822B))
#define BIT_GET_WMAC_MU_DBGSEL_8822B(x)                                        \
	(((x) >> BIT_SHIFT_WMAC_MU_DBGSEL_8822B) &                             \
	 BIT_MASK_WMAC_MU_DBGSEL_8822B)
#define BIT_SET_WMAC_MU_DBGSEL_8822B(x, v)                                     \
	(BIT_CLEAR_WMAC_MU_DBGSEL_8822B(x) | BIT_WMAC_MU_DBGSEL_8822B(v))

#define BIT_SHIFT_WMAC_MU_CPRD_TIMEOUT_8822B 0
#define BIT_MASK_WMAC_MU_CPRD_TIMEOUT_8822B 0x1f
#define BIT_WMAC_MU_CPRD_TIMEOUT_8822B(x)                                      \
	(((x) & BIT_MASK_WMAC_MU_CPRD_TIMEOUT_8822B)                           \
	 << BIT_SHIFT_WMAC_MU_CPRD_TIMEOUT_8822B)
#define BITS_WMAC_MU_CPRD_TIMEOUT_8822B                                        \
	(BIT_MASK_WMAC_MU_CPRD_TIMEOUT_8822B                                   \
	 << BIT_SHIFT_WMAC_MU_CPRD_TIMEOUT_8822B)
#define BIT_CLEAR_WMAC_MU_CPRD_TIMEOUT_8822B(x)                                \
	((x) & (~BITS_WMAC_MU_CPRD_TIMEOUT_8822B))
#define BIT_GET_WMAC_MU_CPRD_TIMEOUT_8822B(x)                                  \
	(((x) >> BIT_SHIFT_WMAC_MU_CPRD_TIMEOUT_8822B) &                       \
	 BIT_MASK_WMAC_MU_CPRD_TIMEOUT_8822B)
#define BIT_SET_WMAC_MU_CPRD_TIMEOUT_8822B(x, v)                               \
	(BIT_CLEAR_WMAC_MU_CPRD_TIMEOUT_8822B(x) |                             \
	 BIT_WMAC_MU_CPRD_TIMEOUT_8822B(v))

/* 2 REG_WMAC_MU_BF_CTL_8822B */
#define BIT_WMAC_INVLD_BFPRT_CHK_8822B BIT(15)
#define BIT_WMAC_RETXBFRPTSEQ_UPD_8822B BIT(14)

#define BIT_SHIFT_WMAC_MU_BFRPTSEG_SEL_8822B 12
#define BIT_MASK_WMAC_MU_BFRPTSEG_SEL_8822B 0x3
#define BIT_WMAC_MU_BFRPTSEG_SEL_8822B(x)                                      \
	(((x) & BIT_MASK_WMAC_MU_BFRPTSEG_SEL_8822B)                           \
	 << BIT_SHIFT_WMAC_MU_BFRPTSEG_SEL_8822B)
#define BITS_WMAC_MU_BFRPTSEG_SEL_8822B                                        \
	(BIT_MASK_WMAC_MU_BFRPTSEG_SEL_8822B                                   \
	 << BIT_SHIFT_WMAC_MU_BFRPTSEG_SEL_8822B)
#define BIT_CLEAR_WMAC_MU_BFRPTSEG_SEL_8822B(x)                                \
	((x) & (~BITS_WMAC_MU_BFRPTSEG_SEL_8822B))
#define BIT_GET_WMAC_MU_BFRPTSEG_SEL_8822B(x)                                  \
	(((x) >> BIT_SHIFT_WMAC_MU_BFRPTSEG_SEL_8822B) &                       \
	 BIT_MASK_WMAC_MU_BFRPTSEG_SEL_8822B)
#define BIT_SET_WMAC_MU_BFRPTSEG_SEL_8822B(x, v)                               \
	(BIT_CLEAR_WMAC_MU_BFRPTSEG_SEL_8822B(x) |                             \
	 BIT_WMAC_MU_BFRPTSEG_SEL_8822B(v))

#define BIT_SHIFT_WMAC_MU_BF_MYAID_8822B 0
#define BIT_MASK_WMAC_MU_BF_MYAID_8822B 0xfff
#define BIT_WMAC_MU_BF_MYAID_8822B(x)                                          \
	(((x) & BIT_MASK_WMAC_MU_BF_MYAID_8822B)                               \
	 << BIT_SHIFT_WMAC_MU_BF_MYAID_8822B)
#define BITS_WMAC_MU_BF_MYAID_8822B                                            \
	(BIT_MASK_WMAC_MU_BF_MYAID_8822B << BIT_SHIFT_WMAC_MU_BF_MYAID_8822B)
#define BIT_CLEAR_WMAC_MU_BF_MYAID_8822B(x)                                    \
	((x) & (~BITS_WMAC_MU_BF_MYAID_8822B))
#define BIT_GET_WMAC_MU_BF_MYAID_8822B(x)                                      \
	(((x) >> BIT_SHIFT_WMAC_MU_BF_MYAID_8822B) &                           \
	 BIT_MASK_WMAC_MU_BF_MYAID_8822B)
#define BIT_SET_WMAC_MU_BF_MYAID_8822B(x, v)                                   \
	(BIT_CLEAR_WMAC_MU_BF_MYAID_8822B(x) | BIT_WMAC_MU_BF_MYAID_8822B(v))

/* 2 REG_WMAC_MU_BFRPT_PARA_8822B */

#define BIT_SHIFT_BIT_BFRPT_PARA_USERID_SEL_8822B 12
#define BIT_MASK_BIT_BFRPT_PARA_USERID_SEL_8822B 0x7
#define BIT_BIT_BFRPT_PARA_USERID_SEL_8822B(x)                                 \
	(((x) & BIT_MASK_BIT_BFRPT_PARA_USERID_SEL_8822B)                      \
	 << BIT_SHIFT_BIT_BFRPT_PARA_USERID_SEL_8822B)
#define BITS_BIT_BFRPT_PARA_USERID_SEL_8822B                                   \
	(BIT_MASK_BIT_BFRPT_PARA_USERID_SEL_8822B                              \
	 << BIT_SHIFT_BIT_BFRPT_PARA_USERID_SEL_8822B)
#define BIT_CLEAR_BIT_BFRPT_PARA_USERID_SEL_8822B(x)                           \
	((x) & (~BITS_BIT_BFRPT_PARA_USERID_SEL_8822B))
#define BIT_GET_BIT_BFRPT_PARA_USERID_SEL_8822B(x)                             \
	(((x) >> BIT_SHIFT_BIT_BFRPT_PARA_USERID_SEL_8822B) &                  \
	 BIT_MASK_BIT_BFRPT_PARA_USERID_SEL_8822B)
#define BIT_SET_BIT_BFRPT_PARA_USERID_SEL_8822B(x, v)                          \
	(BIT_CLEAR_BIT_BFRPT_PARA_USERID_SEL_8822B(x) |                        \
	 BIT_BIT_BFRPT_PARA_USERID_SEL_8822B(v))

#define BIT_SHIFT_BFRPT_PARA_8822B 0
#define BIT_MASK_BFRPT_PARA_8822B 0xfff
#define BIT_BFRPT_PARA_8822B(x)                                                \
	(((x) & BIT_MASK_BFRPT_PARA_8822B) << BIT_SHIFT_BFRPT_PARA_8822B)
#define BITS_BFRPT_PARA_8822B                                                  \
	(BIT_MASK_BFRPT_PARA_8822B << BIT_SHIFT_BFRPT_PARA_8822B)
#define BIT_CLEAR_BFRPT_PARA_8822B(x) ((x) & (~BITS_BFRPT_PARA_8822B))
#define BIT_GET_BFRPT_PARA_8822B(x)                                            \
	(((x) >> BIT_SHIFT_BFRPT_PARA_8822B) & BIT_MASK_BFRPT_PARA_8822B)
#define BIT_SET_BFRPT_PARA_8822B(x, v)                                         \
	(BIT_CLEAR_BFRPT_PARA_8822B(x) | BIT_BFRPT_PARA_8822B(v))

/* 2 REG_WMAC_ASSOCIATED_MU_BFMEE2_8822B */
#define BIT_STATUS_BFEE2_8822B BIT(10)
#define BIT_WMAC_MU_BFEE2_EN_8822B BIT(9)

#define BIT_SHIFT_WMAC_MU_BFEE2_AID_8822B 0
#define BIT_MASK_WMAC_MU_BFEE2_AID_8822B 0x1ff
#define BIT_WMAC_MU_BFEE2_AID_8822B(x)                                         \
	(((x) & BIT_MASK_WMAC_MU_BFEE2_AID_8822B)                              \
	 << BIT_SHIFT_WMAC_MU_BFEE2_AID_8822B)
#define BITS_WMAC_MU_BFEE2_AID_8822B                                           \
	(BIT_MASK_WMAC_MU_BFEE2_AID_8822B << BIT_SHIFT_WMAC_MU_BFEE2_AID_8822B)
#define BIT_CLEAR_WMAC_MU_BFEE2_AID_8822B(x)                                   \
	((x) & (~BITS_WMAC_MU_BFEE2_AID_8822B))
#define BIT_GET_WMAC_MU_BFEE2_AID_8822B(x)                                     \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE2_AID_8822B) &                          \
	 BIT_MASK_WMAC_MU_BFEE2_AID_8822B)
#define BIT_SET_WMAC_MU_BFEE2_AID_8822B(x, v)                                  \
	(BIT_CLEAR_WMAC_MU_BFEE2_AID_8822B(x) | BIT_WMAC_MU_BFEE2_AID_8822B(v))

/* 2 REG_WMAC_ASSOCIATED_MU_BFMEE3_8822B */
#define BIT_STATUS_BFEE3_8822B BIT(10)
#define BIT_WMAC_MU_BFEE3_EN_8822B BIT(9)

#define BIT_SHIFT_WMAC_MU_BFEE3_AID_8822B 0
#define BIT_MASK_WMAC_MU_BFEE3_AID_8822B 0x1ff
#define BIT_WMAC_MU_BFEE3_AID_8822B(x)                                         \
	(((x) & BIT_MASK_WMAC_MU_BFEE3_AID_8822B)                              \
	 << BIT_SHIFT_WMAC_MU_BFEE3_AID_8822B)
#define BITS_WMAC_MU_BFEE3_AID_8822B                                           \
	(BIT_MASK_WMAC_MU_BFEE3_AID_8822B << BIT_SHIFT_WMAC_MU_BFEE3_AID_8822B)
#define BIT_CLEAR_WMAC_MU_BFEE3_AID_8822B(x)                                   \
	((x) & (~BITS_WMAC_MU_BFEE3_AID_8822B))
#define BIT_GET_WMAC_MU_BFEE3_AID_8822B(x)                                     \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE3_AID_8822B) &                          \
	 BIT_MASK_WMAC_MU_BFEE3_AID_8822B)
#define BIT_SET_WMAC_MU_BFEE3_AID_8822B(x, v)                                  \
	(BIT_CLEAR_WMAC_MU_BFEE3_AID_8822B(x) | BIT_WMAC_MU_BFEE3_AID_8822B(v))

/* 2 REG_WMAC_ASSOCIATED_MU_BFMEE4_8822B */
#define BIT_STATUS_BFEE4_8822B BIT(10)
#define BIT_WMAC_MU_BFEE4_EN_8822B BIT(9)

#define BIT_SHIFT_WMAC_MU_BFEE4_AID_8822B 0
#define BIT_MASK_WMAC_MU_BFEE4_AID_8822B 0x1ff
#define BIT_WMAC_MU_BFEE4_AID_8822B(x)                                         \
	(((x) & BIT_MASK_WMAC_MU_BFEE4_AID_8822B)                              \
	 << BIT_SHIFT_WMAC_MU_BFEE4_AID_8822B)
#define BITS_WMAC_MU_BFEE4_AID_8822B                                           \
	(BIT_MASK_WMAC_MU_BFEE4_AID_8822B << BIT_SHIFT_WMAC_MU_BFEE4_AID_8822B)
#define BIT_CLEAR_WMAC_MU_BFEE4_AID_8822B(x)                                   \
	((x) & (~BITS_WMAC_MU_BFEE4_AID_8822B))
#define BIT_GET_WMAC_MU_BFEE4_AID_8822B(x)                                     \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE4_AID_8822B) &                          \
	 BIT_MASK_WMAC_MU_BFEE4_AID_8822B)
#define BIT_SET_WMAC_MU_BFEE4_AID_8822B(x, v)                                  \
	(BIT_CLEAR_WMAC_MU_BFEE4_AID_8822B(x) | BIT_WMAC_MU_BFEE4_AID_8822B(v))

/* 2 REG_WMAC_ASSOCIATED_MU_BFMEE5_8822B */
#define BIT_STATUS_BFEE5_8822B BIT(10)
#define BIT_WMAC_MU_BFEE5_EN_8822B BIT(9)

#define BIT_SHIFT_WMAC_MU_BFEE5_AID_8822B 0
#define BIT_MASK_WMAC_MU_BFEE5_AID_8822B 0x1ff
#define BIT_WMAC_MU_BFEE5_AID_8822B(x)                                         \
	(((x) & BIT_MASK_WMAC_MU_BFEE5_AID_8822B)                              \
	 << BIT_SHIFT_WMAC_MU_BFEE5_AID_8822B)
#define BITS_WMAC_MU_BFEE5_AID_8822B                                           \
	(BIT_MASK_WMAC_MU_BFEE5_AID_8822B << BIT_SHIFT_WMAC_MU_BFEE5_AID_8822B)
#define BIT_CLEAR_WMAC_MU_BFEE5_AID_8822B(x)                                   \
	((x) & (~BITS_WMAC_MU_BFEE5_AID_8822B))
#define BIT_GET_WMAC_MU_BFEE5_AID_8822B(x)                                     \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE5_AID_8822B) &                          \
	 BIT_MASK_WMAC_MU_BFEE5_AID_8822B)
#define BIT_SET_WMAC_MU_BFEE5_AID_8822B(x, v)                                  \
	(BIT_CLEAR_WMAC_MU_BFEE5_AID_8822B(x) | BIT_WMAC_MU_BFEE5_AID_8822B(v))

/* 2 REG_WMAC_ASSOCIATED_MU_BFMEE6_8822B */
#define BIT_STATUS_BFEE6_8822B BIT(10)
#define BIT_WMAC_MU_BFEE6_EN_8822B BIT(9)

#define BIT_SHIFT_WMAC_MU_BFEE6_AID_8822B 0
#define BIT_MASK_WMAC_MU_BFEE6_AID_8822B 0x1ff
#define BIT_WMAC_MU_BFEE6_AID_8822B(x)                                         \
	(((x) & BIT_MASK_WMAC_MU_BFEE6_AID_8822B)                              \
	 << BIT_SHIFT_WMAC_MU_BFEE6_AID_8822B)
#define BITS_WMAC_MU_BFEE6_AID_8822B                                           \
	(BIT_MASK_WMAC_MU_BFEE6_AID_8822B << BIT_SHIFT_WMAC_MU_BFEE6_AID_8822B)
#define BIT_CLEAR_WMAC_MU_BFEE6_AID_8822B(x)                                   \
	((x) & (~BITS_WMAC_MU_BFEE6_AID_8822B))
#define BIT_GET_WMAC_MU_BFEE6_AID_8822B(x)                                     \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE6_AID_8822B) &                          \
	 BIT_MASK_WMAC_MU_BFEE6_AID_8822B)
#define BIT_SET_WMAC_MU_BFEE6_AID_8822B(x, v)                                  \
	(BIT_CLEAR_WMAC_MU_BFEE6_AID_8822B(x) | BIT_WMAC_MU_BFEE6_AID_8822B(v))

/* 2 REG_WMAC_ASSOCIATED_MU_BFMEE7_8822B */
#define BIT_STATUS_BFEE7_8822B BIT(10)
#define BIT_WMAC_MU_BFEE7_EN_8822B BIT(9)

#define BIT_SHIFT_WMAC_MU_BFEE7_AID_8822B 0
#define BIT_MASK_WMAC_MU_BFEE7_AID_8822B 0x1ff
#define BIT_WMAC_MU_BFEE7_AID_8822B(x)                                         \
	(((x) & BIT_MASK_WMAC_MU_BFEE7_AID_8822B)                              \
	 << BIT_SHIFT_WMAC_MU_BFEE7_AID_8822B)
#define BITS_WMAC_MU_BFEE7_AID_8822B                                           \
	(BIT_MASK_WMAC_MU_BFEE7_AID_8822B << BIT_SHIFT_WMAC_MU_BFEE7_AID_8822B)
#define BIT_CLEAR_WMAC_MU_BFEE7_AID_8822B(x)                                   \
	((x) & (~BITS_WMAC_MU_BFEE7_AID_8822B))
#define BIT_GET_WMAC_MU_BFEE7_AID_8822B(x)                                     \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE7_AID_8822B) &                          \
	 BIT_MASK_WMAC_MU_BFEE7_AID_8822B)
#define BIT_SET_WMAC_MU_BFEE7_AID_8822B(x, v)                                  \
	(BIT_CLEAR_WMAC_MU_BFEE7_AID_8822B(x) | BIT_WMAC_MU_BFEE7_AID_8822B(v))

/* 2 REG_NOT_VALID_8822B */
#define BIT_RST_ALL_COUNTER_8822B BIT(31)

#define BIT_SHIFT_ABORT_RX_VBON_COUNTER_8822B 16
#define BIT_MASK_ABORT_RX_VBON_COUNTER_8822B 0xff
#define BIT_ABORT_RX_VBON_COUNTER_8822B(x)                                     \
	(((x) & BIT_MASK_ABORT_RX_VBON_COUNTER_8822B)                          \
	 << BIT_SHIFT_ABORT_RX_VBON_COUNTER_8822B)
#define BITS_ABORT_RX_VBON_COUNTER_8822B                                       \
	(BIT_MASK_ABORT_RX_VBON_COUNTER_8822B                                  \
	 << BIT_SHIFT_ABORT_RX_VBON_COUNTER_8822B)
#define BIT_CLEAR_ABORT_RX_VBON_COUNTER_8822B(x)                               \
	((x) & (~BITS_ABORT_RX_VBON_COUNTER_8822B))
#define BIT_GET_ABORT_RX_VBON_COUNTER_8822B(x)                                 \
	(((x) >> BIT_SHIFT_ABORT_RX_VBON_COUNTER_8822B) &                      \
	 BIT_MASK_ABORT_RX_VBON_COUNTER_8822B)
#define BIT_SET_ABORT_RX_VBON_COUNTER_8822B(x, v)                              \
	(BIT_CLEAR_ABORT_RX_VBON_COUNTER_8822B(x) |                            \
	 BIT_ABORT_RX_VBON_COUNTER_8822B(v))

#define BIT_SHIFT_ABORT_RX_RDRDY_COUNTER_8822B 8
#define BIT_MASK_ABORT_RX_RDRDY_COUNTER_8822B 0xff
#define BIT_ABORT_RX_RDRDY_COUNTER_8822B(x)                                    \
	(((x) & BIT_MASK_ABORT_RX_RDRDY_COUNTER_8822B)                         \
	 << BIT_SHIFT_ABORT_RX_RDRDY_COUNTER_8822B)
#define BITS_ABORT_RX_RDRDY_COUNTER_8822B                                      \
	(BIT_MASK_ABORT_RX_RDRDY_COUNTER_8822B                                 \
	 << BIT_SHIFT_ABORT_RX_RDRDY_COUNTER_8822B)
#define BIT_CLEAR_ABORT_RX_RDRDY_COUNTER_8822B(x)                              \
	((x) & (~BITS_ABORT_RX_RDRDY_COUNTER_8822B))
#define BIT_GET_ABORT_RX_RDRDY_COUNTER_8822B(x)                                \
	(((x) >> BIT_SHIFT_ABORT_RX_RDRDY_COUNTER_8822B) &                     \
	 BIT_MASK_ABORT_RX_RDRDY_COUNTER_8822B)
#define BIT_SET_ABORT_RX_RDRDY_COUNTER_8822B(x, v)                             \
	(BIT_CLEAR_ABORT_RX_RDRDY_COUNTER_8822B(x) |                           \
	 BIT_ABORT_RX_RDRDY_COUNTER_8822B(v))

#define BIT_SHIFT_VBON_EARLY_FALLING_COUNTER_8822B 0
#define BIT_MASK_VBON_EARLY_FALLING_COUNTER_8822B 0xff
#define BIT_VBON_EARLY_FALLING_COUNTER_8822B(x)                                \
	(((x) & BIT_MASK_VBON_EARLY_FALLING_COUNTER_8822B)                     \
	 << BIT_SHIFT_VBON_EARLY_FALLING_COUNTER_8822B)
#define BITS_VBON_EARLY_FALLING_COUNTER_8822B                                  \
	(BIT_MASK_VBON_EARLY_FALLING_COUNTER_8822B                             \
	 << BIT_SHIFT_VBON_EARLY_FALLING_COUNTER_8822B)
#define BIT_CLEAR_VBON_EARLY_FALLING_COUNTER_8822B(x)                          \
	((x) & (~BITS_VBON_EARLY_FALLING_COUNTER_8822B))
#define BIT_GET_VBON_EARLY_FALLING_COUNTER_8822B(x)                            \
	(((x) >> BIT_SHIFT_VBON_EARLY_FALLING_COUNTER_8822B) &                 \
	 BIT_MASK_VBON_EARLY_FALLING_COUNTER_8822B)
#define BIT_SET_VBON_EARLY_FALLING_COUNTER_8822B(x, v)                         \
	(BIT_CLEAR_VBON_EARLY_FALLING_COUNTER_8822B(x) |                       \
	 BIT_VBON_EARLY_FALLING_COUNTER_8822B(v))

/* 2 REG_NOT_VALID_8822B */
#define BIT_WMAC_PLCP_TRX_SEL_8822B BIT(31)

#define BIT_SHIFT_WMAC_PLCP_RDSIG_SEL_8822B 28
#define BIT_MASK_WMAC_PLCP_RDSIG_SEL_8822B 0x7
#define BIT_WMAC_PLCP_RDSIG_SEL_8822B(x)                                       \
	(((x) & BIT_MASK_WMAC_PLCP_RDSIG_SEL_8822B)                            \
	 << BIT_SHIFT_WMAC_PLCP_RDSIG_SEL_8822B)
#define BITS_WMAC_PLCP_RDSIG_SEL_8822B                                         \
	(BIT_MASK_WMAC_PLCP_RDSIG_SEL_8822B                                    \
	 << BIT_SHIFT_WMAC_PLCP_RDSIG_SEL_8822B)
#define BIT_CLEAR_WMAC_PLCP_RDSIG_SEL_8822B(x)                                 \
	((x) & (~BITS_WMAC_PLCP_RDSIG_SEL_8822B))
#define BIT_GET_WMAC_PLCP_RDSIG_SEL_8822B(x)                                   \
	(((x) >> BIT_SHIFT_WMAC_PLCP_RDSIG_SEL_8822B) &                        \
	 BIT_MASK_WMAC_PLCP_RDSIG_SEL_8822B)
#define BIT_SET_WMAC_PLCP_RDSIG_SEL_8822B(x, v)                                \
	(BIT_CLEAR_WMAC_PLCP_RDSIG_SEL_8822B(x) |                              \
	 BIT_WMAC_PLCP_RDSIG_SEL_8822B(v))

#define BIT_SHIFT_WMAC_RATE_IDX_8822B 24
#define BIT_MASK_WMAC_RATE_IDX_8822B 0xf
#define BIT_WMAC_RATE_IDX_8822B(x)                                             \
	(((x) & BIT_MASK_WMAC_RATE_IDX_8822B) << BIT_SHIFT_WMAC_RATE_IDX_8822B)
#define BITS_WMAC_RATE_IDX_8822B                                               \
	(BIT_MASK_WMAC_RATE_IDX_8822B << BIT_SHIFT_WMAC_RATE_IDX_8822B)
#define BIT_CLEAR_WMAC_RATE_IDX_8822B(x) ((x) & (~BITS_WMAC_RATE_IDX_8822B))
#define BIT_GET_WMAC_RATE_IDX_8822B(x)                                         \
	(((x) >> BIT_SHIFT_WMAC_RATE_IDX_8822B) & BIT_MASK_WMAC_RATE_IDX_8822B)
#define BIT_SET_WMAC_RATE_IDX_8822B(x, v)                                      \
	(BIT_CLEAR_WMAC_RATE_IDX_8822B(x) | BIT_WMAC_RATE_IDX_8822B(v))

#define BIT_SHIFT_WMAC_PLCP_RDSIG_8822B 0
#define BIT_MASK_WMAC_PLCP_RDSIG_8822B 0xffffff
#define BIT_WMAC_PLCP_RDSIG_8822B(x)                                           \
	(((x) & BIT_MASK_WMAC_PLCP_RDSIG_8822B)                                \
	 << BIT_SHIFT_WMAC_PLCP_RDSIG_8822B)
#define BITS_WMAC_PLCP_RDSIG_8822B                                             \
	(BIT_MASK_WMAC_PLCP_RDSIG_8822B << BIT_SHIFT_WMAC_PLCP_RDSIG_8822B)
#define BIT_CLEAR_WMAC_PLCP_RDSIG_8822B(x) ((x) & (~BITS_WMAC_PLCP_RDSIG_8822B))
#define BIT_GET_WMAC_PLCP_RDSIG_8822B(x)                                       \
	(((x) >> BIT_SHIFT_WMAC_PLCP_RDSIG_8822B) &                            \
	 BIT_MASK_WMAC_PLCP_RDSIG_8822B)
#define BIT_SET_WMAC_PLCP_RDSIG_8822B(x, v)                                    \
	(BIT_CLEAR_WMAC_PLCP_RDSIG_8822B(x) | BIT_WMAC_PLCP_RDSIG_8822B(v))

/* 2 REG_NOT_VALID_8822B */
#define BIT_WMAC_MUTX_IDX_8822B BIT(24)

#define BIT_SHIFT_WMAC_PLCP_RDSIG_8822B 0
#define BIT_MASK_WMAC_PLCP_RDSIG_8822B 0xffffff
#define BIT_WMAC_PLCP_RDSIG_8822B(x)                                           \
	(((x) & BIT_MASK_WMAC_PLCP_RDSIG_8822B)                                \
	 << BIT_SHIFT_WMAC_PLCP_RDSIG_8822B)
#define BITS_WMAC_PLCP_RDSIG_8822B                                             \
	(BIT_MASK_WMAC_PLCP_RDSIG_8822B << BIT_SHIFT_WMAC_PLCP_RDSIG_8822B)
#define BIT_CLEAR_WMAC_PLCP_RDSIG_8822B(x) ((x) & (~BITS_WMAC_PLCP_RDSIG_8822B))
#define BIT_GET_WMAC_PLCP_RDSIG_8822B(x)                                       \
	(((x) >> BIT_SHIFT_WMAC_PLCP_RDSIG_8822B) &                            \
	 BIT_MASK_WMAC_PLCP_RDSIG_8822B)
#define BIT_SET_WMAC_PLCP_RDSIG_8822B(x, v)                                    \
	(BIT_CLEAR_WMAC_PLCP_RDSIG_8822B(x) | BIT_WMAC_PLCP_RDSIG_8822B(v))

/* 2 REG_TRANSMIT_ADDRSS_0_8822B (TA0 REGISTER) */

#define BIT_SHIFT_TA0_8822B 0
#define BIT_MASK_TA0_8822B 0xffffffffffffL
#define BIT_TA0_8822B(x) (((x) & BIT_MASK_TA0_8822B) << BIT_SHIFT_TA0_8822B)
#define BITS_TA0_8822B (BIT_MASK_TA0_8822B << BIT_SHIFT_TA0_8822B)
#define BIT_CLEAR_TA0_8822B(x) ((x) & (~BITS_TA0_8822B))
#define BIT_GET_TA0_8822B(x) (((x) >> BIT_SHIFT_TA0_8822B) & BIT_MASK_TA0_8822B)
#define BIT_SET_TA0_8822B(x, v) (BIT_CLEAR_TA0_8822B(x) | BIT_TA0_8822B(v))

/* 2 REG_TRANSMIT_ADDRSS_1_8822B (TA1 REGISTER) */

#define BIT_SHIFT_TA1_8822B 0
#define BIT_MASK_TA1_8822B 0xffffffffffffL
#define BIT_TA1_8822B(x) (((x) & BIT_MASK_TA1_8822B) << BIT_SHIFT_TA1_8822B)
#define BITS_TA1_8822B (BIT_MASK_TA1_8822B << BIT_SHIFT_TA1_8822B)
#define BIT_CLEAR_TA1_8822B(x) ((x) & (~BITS_TA1_8822B))
#define BIT_GET_TA1_8822B(x) (((x) >> BIT_SHIFT_TA1_8822B) & BIT_MASK_TA1_8822B)
#define BIT_SET_TA1_8822B(x, v) (BIT_CLEAR_TA1_8822B(x) | BIT_TA1_8822B(v))

/* 2 REG_TRANSMIT_ADDRSS_2_8822B (TA2 REGISTER) */

#define BIT_SHIFT_TA2_8822B 0
#define BIT_MASK_TA2_8822B 0xffffffffffffL
#define BIT_TA2_8822B(x) (((x) & BIT_MASK_TA2_8822B) << BIT_SHIFT_TA2_8822B)
#define BITS_TA2_8822B (BIT_MASK_TA2_8822B << BIT_SHIFT_TA2_8822B)
#define BIT_CLEAR_TA2_8822B(x) ((x) & (~BITS_TA2_8822B))
#define BIT_GET_TA2_8822B(x) (((x) >> BIT_SHIFT_TA2_8822B) & BIT_MASK_TA2_8822B)
#define BIT_SET_TA2_8822B(x, v) (BIT_CLEAR_TA2_8822B(x) | BIT_TA2_8822B(v))

/* 2 REG_TRANSMIT_ADDRSS_3_8822B (TA3 REGISTER) */

#define BIT_SHIFT_TA3_8822B 0
#define BIT_MASK_TA3_8822B 0xffffffffffffL
#define BIT_TA3_8822B(x) (((x) & BIT_MASK_TA3_8822B) << BIT_SHIFT_TA3_8822B)
#define BITS_TA3_8822B (BIT_MASK_TA3_8822B << BIT_SHIFT_TA3_8822B)
#define BIT_CLEAR_TA3_8822B(x) ((x) & (~BITS_TA3_8822B))
#define BIT_GET_TA3_8822B(x) (((x) >> BIT_SHIFT_TA3_8822B) & BIT_MASK_TA3_8822B)
#define BIT_SET_TA3_8822B(x, v) (BIT_CLEAR_TA3_8822B(x) | BIT_TA3_8822B(v))

/* 2 REG_TRANSMIT_ADDRSS_4_8822B (TA4 REGISTER) */

#define BIT_SHIFT_TA4_8822B 0
#define BIT_MASK_TA4_8822B 0xffffffffffffL
#define BIT_TA4_8822B(x) (((x) & BIT_MASK_TA4_8822B) << BIT_SHIFT_TA4_8822B)
#define BITS_TA4_8822B (BIT_MASK_TA4_8822B << BIT_SHIFT_TA4_8822B)
#define BIT_CLEAR_TA4_8822B(x) ((x) & (~BITS_TA4_8822B))
#define BIT_GET_TA4_8822B(x) (((x) >> BIT_SHIFT_TA4_8822B) & BIT_MASK_TA4_8822B)
#define BIT_SET_TA4_8822B(x, v) (BIT_CLEAR_TA4_8822B(x) | BIT_TA4_8822B(v))

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_MACID1_8822B */

#define BIT_SHIFT_MACID1_8822B 0
#define BIT_MASK_MACID1_8822B 0xffffffffffffL
#define BIT_MACID1_8822B(x)                                                    \
	(((x) & BIT_MASK_MACID1_8822B) << BIT_SHIFT_MACID1_8822B)
#define BITS_MACID1_8822B (BIT_MASK_MACID1_8822B << BIT_SHIFT_MACID1_8822B)
#define BIT_CLEAR_MACID1_8822B(x) ((x) & (~BITS_MACID1_8822B))
#define BIT_GET_MACID1_8822B(x)                                                \
	(((x) >> BIT_SHIFT_MACID1_8822B) & BIT_MASK_MACID1_8822B)
#define BIT_SET_MACID1_8822B(x, v)                                             \
	(BIT_CLEAR_MACID1_8822B(x) | BIT_MACID1_8822B(v))

/* 2 REG_BSSID1_8822B */

#define BIT_SHIFT_BSSID1_8822B 0
#define BIT_MASK_BSSID1_8822B 0xffffffffffffL
#define BIT_BSSID1_8822B(x)                                                    \
	(((x) & BIT_MASK_BSSID1_8822B) << BIT_SHIFT_BSSID1_8822B)
#define BITS_BSSID1_8822B (BIT_MASK_BSSID1_8822B << BIT_SHIFT_BSSID1_8822B)
#define BIT_CLEAR_BSSID1_8822B(x) ((x) & (~BITS_BSSID1_8822B))
#define BIT_GET_BSSID1_8822B(x)                                                \
	(((x) >> BIT_SHIFT_BSSID1_8822B) & BIT_MASK_BSSID1_8822B)
#define BIT_SET_BSSID1_8822B(x, v)                                             \
	(BIT_CLEAR_BSSID1_8822B(x) | BIT_BSSID1_8822B(v))

/* 2 REG_BCN_PSR_RPT1_8822B */

#define BIT_SHIFT_DTIM_CNT1_8822B 24
#define BIT_MASK_DTIM_CNT1_8822B 0xff
#define BIT_DTIM_CNT1_8822B(x)                                                 \
	(((x) & BIT_MASK_DTIM_CNT1_8822B) << BIT_SHIFT_DTIM_CNT1_8822B)
#define BITS_DTIM_CNT1_8822B                                                   \
	(BIT_MASK_DTIM_CNT1_8822B << BIT_SHIFT_DTIM_CNT1_8822B)
#define BIT_CLEAR_DTIM_CNT1_8822B(x) ((x) & (~BITS_DTIM_CNT1_8822B))
#define BIT_GET_DTIM_CNT1_8822B(x)                                             \
	(((x) >> BIT_SHIFT_DTIM_CNT1_8822B) & BIT_MASK_DTIM_CNT1_8822B)
#define BIT_SET_DTIM_CNT1_8822B(x, v)                                          \
	(BIT_CLEAR_DTIM_CNT1_8822B(x) | BIT_DTIM_CNT1_8822B(v))

#define BIT_SHIFT_DTIM_PERIOD1_8822B 16
#define BIT_MASK_DTIM_PERIOD1_8822B 0xff
#define BIT_DTIM_PERIOD1_8822B(x)                                              \
	(((x) & BIT_MASK_DTIM_PERIOD1_8822B) << BIT_SHIFT_DTIM_PERIOD1_8822B)
#define BITS_DTIM_PERIOD1_8822B                                                \
	(BIT_MASK_DTIM_PERIOD1_8822B << BIT_SHIFT_DTIM_PERIOD1_8822B)
#define BIT_CLEAR_DTIM_PERIOD1_8822B(x) ((x) & (~BITS_DTIM_PERIOD1_8822B))
#define BIT_GET_DTIM_PERIOD1_8822B(x)                                          \
	(((x) >> BIT_SHIFT_DTIM_PERIOD1_8822B) & BIT_MASK_DTIM_PERIOD1_8822B)
#define BIT_SET_DTIM_PERIOD1_8822B(x, v)                                       \
	(BIT_CLEAR_DTIM_PERIOD1_8822B(x) | BIT_DTIM_PERIOD1_8822B(v))

#define BIT_DTIM1_8822B BIT(15)
#define BIT_TIM1_8822B BIT(14)

#define BIT_SHIFT_PS_AID_1_8822B 0
#define BIT_MASK_PS_AID_1_8822B 0x7ff
#define BIT_PS_AID_1_8822B(x)                                                  \
	(((x) & BIT_MASK_PS_AID_1_8822B) << BIT_SHIFT_PS_AID_1_8822B)
#define BITS_PS_AID_1_8822B                                                    \
	(BIT_MASK_PS_AID_1_8822B << BIT_SHIFT_PS_AID_1_8822B)
#define BIT_CLEAR_PS_AID_1_8822B(x) ((x) & (~BITS_PS_AID_1_8822B))
#define BIT_GET_PS_AID_1_8822B(x)                                              \
	(((x) >> BIT_SHIFT_PS_AID_1_8822B) & BIT_MASK_PS_AID_1_8822B)
#define BIT_SET_PS_AID_1_8822B(x, v)                                           \
	(BIT_CLEAR_PS_AID_1_8822B(x) | BIT_PS_AID_1_8822B(v))

/* 2 REG_ASSOCIATED_BFMEE_SEL_8822B */
#define BIT_TXUSER_ID1_8822B BIT(25)

#define BIT_SHIFT_AID1_8822B 16
#define BIT_MASK_AID1_8822B 0x1ff
#define BIT_AID1_8822B(x) (((x) & BIT_MASK_AID1_8822B) << BIT_SHIFT_AID1_8822B)
#define BITS_AID1_8822B (BIT_MASK_AID1_8822B << BIT_SHIFT_AID1_8822B)
#define BIT_CLEAR_AID1_8822B(x) ((x) & (~BITS_AID1_8822B))
#define BIT_GET_AID1_8822B(x)                                                  \
	(((x) >> BIT_SHIFT_AID1_8822B) & BIT_MASK_AID1_8822B)
#define BIT_SET_AID1_8822B(x, v) (BIT_CLEAR_AID1_8822B(x) | BIT_AID1_8822B(v))

#define BIT_TXUSER_ID0_8822B BIT(9)

#define BIT_SHIFT_AID0_8822B 0
#define BIT_MASK_AID0_8822B 0x1ff
#define BIT_AID0_8822B(x) (((x) & BIT_MASK_AID0_8822B) << BIT_SHIFT_AID0_8822B)
#define BITS_AID0_8822B (BIT_MASK_AID0_8822B << BIT_SHIFT_AID0_8822B)
#define BIT_CLEAR_AID0_8822B(x) ((x) & (~BITS_AID0_8822B))
#define BIT_GET_AID0_8822B(x)                                                  \
	(((x) >> BIT_SHIFT_AID0_8822B) & BIT_MASK_AID0_8822B)
#define BIT_SET_AID0_8822B(x, v) (BIT_CLEAR_AID0_8822B(x) | BIT_AID0_8822B(v))

/* 2 REG_SND_PTCL_CTRL_8822B */

#define BIT_SHIFT_NDP_RX_STANDBY_TIMER_8822B 24
#define BIT_MASK_NDP_RX_STANDBY_TIMER_8822B 0xff
#define BIT_NDP_RX_STANDBY_TIMER_8822B(x)                                      \
	(((x) & BIT_MASK_NDP_RX_STANDBY_TIMER_8822B)                           \
	 << BIT_SHIFT_NDP_RX_STANDBY_TIMER_8822B)
#define BITS_NDP_RX_STANDBY_TIMER_8822B                                        \
	(BIT_MASK_NDP_RX_STANDBY_TIMER_8822B                                   \
	 << BIT_SHIFT_NDP_RX_STANDBY_TIMER_8822B)
#define BIT_CLEAR_NDP_RX_STANDBY_TIMER_8822B(x)                                \
	((x) & (~BITS_NDP_RX_STANDBY_TIMER_8822B))
#define BIT_GET_NDP_RX_STANDBY_TIMER_8822B(x)                                  \
	(((x) >> BIT_SHIFT_NDP_RX_STANDBY_TIMER_8822B) &                       \
	 BIT_MASK_NDP_RX_STANDBY_TIMER_8822B)
#define BIT_SET_NDP_RX_STANDBY_TIMER_8822B(x, v)                               \
	(BIT_CLEAR_NDP_RX_STANDBY_TIMER_8822B(x) |                             \
	 BIT_NDP_RX_STANDBY_TIMER_8822B(v))

#define BIT_SHIFT_CSI_RPT_OFFSET_HT_V1_8822B 16
#define BIT_MASK_CSI_RPT_OFFSET_HT_V1_8822B 0x3f
#define BIT_CSI_RPT_OFFSET_HT_V1_8822B(x)                                      \
	(((x) & BIT_MASK_CSI_RPT_OFFSET_HT_V1_8822B)                           \
	 << BIT_SHIFT_CSI_RPT_OFFSET_HT_V1_8822B)
#define BITS_CSI_RPT_OFFSET_HT_V1_8822B                                        \
	(BIT_MASK_CSI_RPT_OFFSET_HT_V1_8822B                                   \
	 << BIT_SHIFT_CSI_RPT_OFFSET_HT_V1_8822B)
#define BIT_CLEAR_CSI_RPT_OFFSET_HT_V1_8822B(x)                                \
	((x) & (~BITS_CSI_RPT_OFFSET_HT_V1_8822B))
#define BIT_GET_CSI_RPT_OFFSET_HT_V1_8822B(x)                                  \
	(((x) >> BIT_SHIFT_CSI_RPT_OFFSET_HT_V1_8822B) &                       \
	 BIT_MASK_CSI_RPT_OFFSET_HT_V1_8822B)
#define BIT_SET_CSI_RPT_OFFSET_HT_V1_8822B(x, v)                               \
	(BIT_CLEAR_CSI_RPT_OFFSET_HT_V1_8822B(x) |                             \
	 BIT_CSI_RPT_OFFSET_HT_V1_8822B(v))

#define BIT_VHTNDP_RPTPOLL_CSI_STR_OFFSET_SEL_8822B BIT(15)
#define BIT_NDPVLD_POS_RST_FFPTR_DIS_8822B BIT(14)

#define BIT_SHIFT_R_CSI_RPT_OFFSET_VHT_V1_8822B 8
#define BIT_MASK_R_CSI_RPT_OFFSET_VHT_V1_8822B 0x3f
#define BIT_R_CSI_RPT_OFFSET_VHT_V1_8822B(x)                                   \
	(((x) & BIT_MASK_R_CSI_RPT_OFFSET_VHT_V1_8822B)                        \
	 << BIT_SHIFT_R_CSI_RPT_OFFSET_VHT_V1_8822B)
#define BITS_R_CSI_RPT_OFFSET_VHT_V1_8822B                                     \
	(BIT_MASK_R_CSI_RPT_OFFSET_VHT_V1_8822B                                \
	 << BIT_SHIFT_R_CSI_RPT_OFFSET_VHT_V1_8822B)
#define BIT_CLEAR_R_CSI_RPT_OFFSET_VHT_V1_8822B(x)                             \
	((x) & (~BITS_R_CSI_RPT_OFFSET_VHT_V1_8822B))
#define BIT_GET_R_CSI_RPT_OFFSET_VHT_V1_8822B(x)                               \
	(((x) >> BIT_SHIFT_R_CSI_RPT_OFFSET_VHT_V1_8822B) &                    \
	 BIT_MASK_R_CSI_RPT_OFFSET_VHT_V1_8822B)
#define BIT_SET_R_CSI_RPT_OFFSET_VHT_V1_8822B(x, v)                            \
	(BIT_CLEAR_R_CSI_RPT_OFFSET_VHT_V1_8822B(x) |                          \
	 BIT_R_CSI_RPT_OFFSET_VHT_V1_8822B(v))

#define BIT_R_WMAC_USE_NSTS_8822B BIT(7)
#define BIT_R_DISABLE_CHECK_VHTSIGB_CRC_8822B BIT(6)
#define BIT_R_DISABLE_CHECK_VHTSIGA_CRC_8822B BIT(5)
#define BIT_R_WMAC_BFPARAM_SEL_8822B BIT(4)
#define BIT_R_WMAC_CSISEQ_SEL_8822B BIT(3)
#define BIT_R_WMAC_CSI_WITHHTC_EN_8822B BIT(2)
#define BIT_R_WMAC_HT_NDPA_EN_8822B BIT(1)
#define BIT_R_WMAC_VHT_NDPA_EN_8822B BIT(0)

/* 2 REG_RX_CSI_RPT_INFO_8822B */

/* 2 REG_NS_ARP_CTRL_8822B */
#define BIT_R_WMAC_NSARP_RSPEN_8822B BIT(15)
#define BIT_R_WMAC_NSARP_RARP_8822B BIT(9)
#define BIT_R_WMAC_NSARP_RIPV6_8822B BIT(8)

#define BIT_SHIFT_R_WMAC_NSARP_MODEN_8822B 6
#define BIT_MASK_R_WMAC_NSARP_MODEN_8822B 0x3
#define BIT_R_WMAC_NSARP_MODEN_8822B(x)                                        \
	(((x) & BIT_MASK_R_WMAC_NSARP_MODEN_8822B)                             \
	 << BIT_SHIFT_R_WMAC_NSARP_MODEN_8822B)
#define BITS_R_WMAC_NSARP_MODEN_8822B                                          \
	(BIT_MASK_R_WMAC_NSARP_MODEN_8822B                                     \
	 << BIT_SHIFT_R_WMAC_NSARP_MODEN_8822B)
#define BIT_CLEAR_R_WMAC_NSARP_MODEN_8822B(x)                                  \
	((x) & (~BITS_R_WMAC_NSARP_MODEN_8822B))
#define BIT_GET_R_WMAC_NSARP_MODEN_8822B(x)                                    \
	(((x) >> BIT_SHIFT_R_WMAC_NSARP_MODEN_8822B) &                         \
	 BIT_MASK_R_WMAC_NSARP_MODEN_8822B)
#define BIT_SET_R_WMAC_NSARP_MODEN_8822B(x, v)                                 \
	(BIT_CLEAR_R_WMAC_NSARP_MODEN_8822B(x) |                               \
	 BIT_R_WMAC_NSARP_MODEN_8822B(v))

#define BIT_SHIFT_R_WMAC_NSARP_RSPFTP_8822B 4
#define BIT_MASK_R_WMAC_NSARP_RSPFTP_8822B 0x3
#define BIT_R_WMAC_NSARP_RSPFTP_8822B(x)                                       \
	(((x) & BIT_MASK_R_WMAC_NSARP_RSPFTP_8822B)                            \
	 << BIT_SHIFT_R_WMAC_NSARP_RSPFTP_8822B)
#define BITS_R_WMAC_NSARP_RSPFTP_8822B                                         \
	(BIT_MASK_R_WMAC_NSARP_RSPFTP_8822B                                    \
	 << BIT_SHIFT_R_WMAC_NSARP_RSPFTP_8822B)
#define BIT_CLEAR_R_WMAC_NSARP_RSPFTP_8822B(x)                                 \
	((x) & (~BITS_R_WMAC_NSARP_RSPFTP_8822B))
#define BIT_GET_R_WMAC_NSARP_RSPFTP_8822B(x)                                   \
	(((x) >> BIT_SHIFT_R_WMAC_NSARP_RSPFTP_8822B) &                        \
	 BIT_MASK_R_WMAC_NSARP_RSPFTP_8822B)
#define BIT_SET_R_WMAC_NSARP_RSPFTP_8822B(x, v)                                \
	(BIT_CLEAR_R_WMAC_NSARP_RSPFTP_8822B(x) |                              \
	 BIT_R_WMAC_NSARP_RSPFTP_8822B(v))

#define BIT_SHIFT_R_WMAC_NSARP_RSPSEC_8822B 0
#define BIT_MASK_R_WMAC_NSARP_RSPSEC_8822B 0xf
#define BIT_R_WMAC_NSARP_RSPSEC_8822B(x)                                       \
	(((x) & BIT_MASK_R_WMAC_NSARP_RSPSEC_8822B)                            \
	 << BIT_SHIFT_R_WMAC_NSARP_RSPSEC_8822B)
#define BITS_R_WMAC_NSARP_RSPSEC_8822B                                         \
	(BIT_MASK_R_WMAC_NSARP_RSPSEC_8822B                                    \
	 << BIT_SHIFT_R_WMAC_NSARP_RSPSEC_8822B)
#define BIT_CLEAR_R_WMAC_NSARP_RSPSEC_8822B(x)                                 \
	((x) & (~BITS_R_WMAC_NSARP_RSPSEC_8822B))
#define BIT_GET_R_WMAC_NSARP_RSPSEC_8822B(x)                                   \
	(((x) >> BIT_SHIFT_R_WMAC_NSARP_RSPSEC_8822B) &                        \
	 BIT_MASK_R_WMAC_NSARP_RSPSEC_8822B)
#define BIT_SET_R_WMAC_NSARP_RSPSEC_8822B(x, v)                                \
	(BIT_CLEAR_R_WMAC_NSARP_RSPSEC_8822B(x) |                              \
	 BIT_R_WMAC_NSARP_RSPSEC_8822B(v))

/* 2 REG_NS_ARP_INFO_8822B */
#define BIT_REQ_IS_MCNS_8822B BIT(23)
#define BIT_REQ_IS_UCNS_8822B BIT(22)
#define BIT_REQ_IS_USNS_8822B BIT(21)
#define BIT_REQ_IS_ARP_8822B BIT(20)
#define BIT_EXPRSP_MH_WITHQC_8822B BIT(19)

#define BIT_SHIFT_EXPRSP_SECTYPE_8822B 16
#define BIT_MASK_EXPRSP_SECTYPE_8822B 0x7
#define BIT_EXPRSP_SECTYPE_8822B(x)                                            \
	(((x) & BIT_MASK_EXPRSP_SECTYPE_8822B)                                 \
	 << BIT_SHIFT_EXPRSP_SECTYPE_8822B)
#define BITS_EXPRSP_SECTYPE_8822B                                              \
	(BIT_MASK_EXPRSP_SECTYPE_8822B << BIT_SHIFT_EXPRSP_SECTYPE_8822B)
#define BIT_CLEAR_EXPRSP_SECTYPE_8822B(x) ((x) & (~BITS_EXPRSP_SECTYPE_8822B))
#define BIT_GET_EXPRSP_SECTYPE_8822B(x)                                        \
	(((x) >> BIT_SHIFT_EXPRSP_SECTYPE_8822B) &                             \
	 BIT_MASK_EXPRSP_SECTYPE_8822B)
#define BIT_SET_EXPRSP_SECTYPE_8822B(x, v)                                     \
	(BIT_CLEAR_EXPRSP_SECTYPE_8822B(x) | BIT_EXPRSP_SECTYPE_8822B(v))

#define BIT_SHIFT_EXPRSP_CHKSM_7_TO_0_8822B 8
#define BIT_MASK_EXPRSP_CHKSM_7_TO_0_8822B 0xff
#define BIT_EXPRSP_CHKSM_7_TO_0_8822B(x)                                       \
	(((x) & BIT_MASK_EXPRSP_CHKSM_7_TO_0_8822B)                            \
	 << BIT_SHIFT_EXPRSP_CHKSM_7_TO_0_8822B)
#define BITS_EXPRSP_CHKSM_7_TO_0_8822B                                         \
	(BIT_MASK_EXPRSP_CHKSM_7_TO_0_8822B                                    \
	 << BIT_SHIFT_EXPRSP_CHKSM_7_TO_0_8822B)
#define BIT_CLEAR_EXPRSP_CHKSM_7_TO_0_8822B(x)                                 \
	((x) & (~BITS_EXPRSP_CHKSM_7_TO_0_8822B))
#define BIT_GET_EXPRSP_CHKSM_7_TO_0_8822B(x)                                   \
	(((x) >> BIT_SHIFT_EXPRSP_CHKSM_7_TO_0_8822B) &                        \
	 BIT_MASK_EXPRSP_CHKSM_7_TO_0_8822B)
#define BIT_SET_EXPRSP_CHKSM_7_TO_0_8822B(x, v)                                \
	(BIT_CLEAR_EXPRSP_CHKSM_7_TO_0_8822B(x) |                              \
	 BIT_EXPRSP_CHKSM_7_TO_0_8822B(v))

#define BIT_SHIFT_EXPRSP_CHKSM_15_TO_8_8822B 0
#define BIT_MASK_EXPRSP_CHKSM_15_TO_8_8822B 0xff
#define BIT_EXPRSP_CHKSM_15_TO_8_8822B(x)                                      \
	(((x) & BIT_MASK_EXPRSP_CHKSM_15_TO_8_8822B)                           \
	 << BIT_SHIFT_EXPRSP_CHKSM_15_TO_8_8822B)
#define BITS_EXPRSP_CHKSM_15_TO_8_8822B                                        \
	(BIT_MASK_EXPRSP_CHKSM_15_TO_8_8822B                                   \
	 << BIT_SHIFT_EXPRSP_CHKSM_15_TO_8_8822B)
#define BIT_CLEAR_EXPRSP_CHKSM_15_TO_8_8822B(x)                                \
	((x) & (~BITS_EXPRSP_CHKSM_15_TO_8_8822B))
#define BIT_GET_EXPRSP_CHKSM_15_TO_8_8822B(x)                                  \
	(((x) >> BIT_SHIFT_EXPRSP_CHKSM_15_TO_8_8822B) &                       \
	 BIT_MASK_EXPRSP_CHKSM_15_TO_8_8822B)
#define BIT_SET_EXPRSP_CHKSM_15_TO_8_8822B(x, v)                               \
	(BIT_CLEAR_EXPRSP_CHKSM_15_TO_8_8822B(x) |                             \
	 BIT_EXPRSP_CHKSM_15_TO_8_8822B(v))

/* 2 REG_BEAMFORMING_INFO_NSARP_V1_8822B */

#define BIT_SHIFT_WMAC_ARPIP_8822B 0
#define BIT_MASK_WMAC_ARPIP_8822B 0xffffffffL
#define BIT_WMAC_ARPIP_8822B(x)                                                \
	(((x) & BIT_MASK_WMAC_ARPIP_8822B) << BIT_SHIFT_WMAC_ARPIP_8822B)
#define BITS_WMAC_ARPIP_8822B                                                  \
	(BIT_MASK_WMAC_ARPIP_8822B << BIT_SHIFT_WMAC_ARPIP_8822B)
#define BIT_CLEAR_WMAC_ARPIP_8822B(x) ((x) & (~BITS_WMAC_ARPIP_8822B))
#define BIT_GET_WMAC_ARPIP_8822B(x)                                            \
	(((x) >> BIT_SHIFT_WMAC_ARPIP_8822B) & BIT_MASK_WMAC_ARPIP_8822B)
#define BIT_SET_WMAC_ARPIP_8822B(x, v)                                         \
	(BIT_CLEAR_WMAC_ARPIP_8822B(x) | BIT_WMAC_ARPIP_8822B(v))

/* 2 REG_BEAMFORMING_INFO_NSARP_8822B */

#define BIT_SHIFT_BEAMFORMING_INFO_8822B 0
#define BIT_MASK_BEAMFORMING_INFO_8822B 0xffffffffL
#define BIT_BEAMFORMING_INFO_8822B(x)                                          \
	(((x) & BIT_MASK_BEAMFORMING_INFO_8822B)                               \
	 << BIT_SHIFT_BEAMFORMING_INFO_8822B)
#define BITS_BEAMFORMING_INFO_8822B                                            \
	(BIT_MASK_BEAMFORMING_INFO_8822B << BIT_SHIFT_BEAMFORMING_INFO_8822B)
#define BIT_CLEAR_BEAMFORMING_INFO_8822B(x)                                    \
	((x) & (~BITS_BEAMFORMING_INFO_8822B))
#define BIT_GET_BEAMFORMING_INFO_8822B(x)                                      \
	(((x) >> BIT_SHIFT_BEAMFORMING_INFO_8822B) &                           \
	 BIT_MASK_BEAMFORMING_INFO_8822B)
#define BIT_SET_BEAMFORMING_INFO_8822B(x, v)                                   \
	(BIT_CLEAR_BEAMFORMING_INFO_8822B(x) | BIT_BEAMFORMING_INFO_8822B(v))

/* 2 REG_NOT_VALID_8822B */

#define BIT_SHIFT_R_WMAC_IPV6_MYIPAD_8822B 0
#define BIT_MASK_R_WMAC_IPV6_MYIPAD_8822B 0xffffffffffffffffffffffffffffffffL
#define BIT_R_WMAC_IPV6_MYIPAD_8822B(x)                                        \
	(((x) & BIT_MASK_R_WMAC_IPV6_MYIPAD_8822B)                             \
	 << BIT_SHIFT_R_WMAC_IPV6_MYIPAD_8822B)
#define BITS_R_WMAC_IPV6_MYIPAD_8822B                                          \
	(BIT_MASK_R_WMAC_IPV6_MYIPAD_8822B                                     \
	 << BIT_SHIFT_R_WMAC_IPV6_MYIPAD_8822B)
#define BIT_CLEAR_R_WMAC_IPV6_MYIPAD_8822B(x)                                  \
	((x) & (~BITS_R_WMAC_IPV6_MYIPAD_8822B))
#define BIT_GET_R_WMAC_IPV6_MYIPAD_8822B(x)                                    \
	(((x) >> BIT_SHIFT_R_WMAC_IPV6_MYIPAD_8822B) &                         \
	 BIT_MASK_R_WMAC_IPV6_MYIPAD_8822B)
#define BIT_SET_R_WMAC_IPV6_MYIPAD_8822B(x, v)                                 \
	(BIT_CLEAR_R_WMAC_IPV6_MYIPAD_8822B(x) |                               \
	 BIT_R_WMAC_IPV6_MYIPAD_8822B(v))

/* 2 REG_RSVD_0X740_8822B */

/* 2 REG_WMAC_RTX_CTX_SUBTYPE_CFG_8822B */

#define BIT_SHIFT_R_WMAC_CTX_SUBTYPE_8822B 4
#define BIT_MASK_R_WMAC_CTX_SUBTYPE_8822B 0xf
#define BIT_R_WMAC_CTX_SUBTYPE_8822B(x)                                        \
	(((x) & BIT_MASK_R_WMAC_CTX_SUBTYPE_8822B)                             \
	 << BIT_SHIFT_R_WMAC_CTX_SUBTYPE_8822B)
#define BITS_R_WMAC_CTX_SUBTYPE_8822B                                          \
	(BIT_MASK_R_WMAC_CTX_SUBTYPE_8822B                                     \
	 << BIT_SHIFT_R_WMAC_CTX_SUBTYPE_8822B)
#define BIT_CLEAR_R_WMAC_CTX_SUBTYPE_8822B(x)                                  \
	((x) & (~BITS_R_WMAC_CTX_SUBTYPE_8822B))
#define BIT_GET_R_WMAC_CTX_SUBTYPE_8822B(x)                                    \
	(((x) >> BIT_SHIFT_R_WMAC_CTX_SUBTYPE_8822B) &                         \
	 BIT_MASK_R_WMAC_CTX_SUBTYPE_8822B)
#define BIT_SET_R_WMAC_CTX_SUBTYPE_8822B(x, v)                                 \
	(BIT_CLEAR_R_WMAC_CTX_SUBTYPE_8822B(x) |                               \
	 BIT_R_WMAC_CTX_SUBTYPE_8822B(v))

#define BIT_SHIFT_R_WMAC_RTX_SUBTYPE_8822B 0
#define BIT_MASK_R_WMAC_RTX_SUBTYPE_8822B 0xf
#define BIT_R_WMAC_RTX_SUBTYPE_8822B(x)                                        \
	(((x) & BIT_MASK_R_WMAC_RTX_SUBTYPE_8822B)                             \
	 << BIT_SHIFT_R_WMAC_RTX_SUBTYPE_8822B)
#define BITS_R_WMAC_RTX_SUBTYPE_8822B                                          \
	(BIT_MASK_R_WMAC_RTX_SUBTYPE_8822B                                     \
	 << BIT_SHIFT_R_WMAC_RTX_SUBTYPE_8822B)
#define BIT_CLEAR_R_WMAC_RTX_SUBTYPE_8822B(x)                                  \
	((x) & (~BITS_R_WMAC_RTX_SUBTYPE_8822B))
#define BIT_GET_R_WMAC_RTX_SUBTYPE_8822B(x)                                    \
	(((x) >> BIT_SHIFT_R_WMAC_RTX_SUBTYPE_8822B) &                         \
	 BIT_MASK_R_WMAC_RTX_SUBTYPE_8822B)
#define BIT_SET_R_WMAC_RTX_SUBTYPE_8822B(x, v)                                 \
	(BIT_CLEAR_R_WMAC_RTX_SUBTYPE_8822B(x) |                               \
	 BIT_R_WMAC_RTX_SUBTYPE_8822B(v))

/* 2 REG_WMAC_SWAES_CFG_8822B */

/* 2 REG_BT_COEX_V2_8822B */
#define BIT_GNT_BT_POLARITY_8822B BIT(12)
#define BIT_GNT_BT_BYPASS_PRIORITY_8822B BIT(8)

#define BIT_SHIFT_TIMER_8822B 0
#define BIT_MASK_TIMER_8822B 0xff
#define BIT_TIMER_8822B(x)                                                     \
	(((x) & BIT_MASK_TIMER_8822B) << BIT_SHIFT_TIMER_8822B)
#define BITS_TIMER_8822B (BIT_MASK_TIMER_8822B << BIT_SHIFT_TIMER_8822B)
#define BIT_CLEAR_TIMER_8822B(x) ((x) & (~BITS_TIMER_8822B))
#define BIT_GET_TIMER_8822B(x)                                                 \
	(((x) >> BIT_SHIFT_TIMER_8822B) & BIT_MASK_TIMER_8822B)
#define BIT_SET_TIMER_8822B(x, v)                                              \
	(BIT_CLEAR_TIMER_8822B(x) | BIT_TIMER_8822B(v))

/* 2 REG_BT_COEX_8822B */
#define BIT_R_GNT_BT_RFC_SW_8822B BIT(12)
#define BIT_R_GNT_BT_RFC_SW_EN_8822B BIT(11)
#define BIT_R_GNT_BT_BB_SW_8822B BIT(10)
#define BIT_R_GNT_BT_BB_SW_EN_8822B BIT(9)
#define BIT_R_BT_CNT_THREN_8822B BIT(8)

#define BIT_SHIFT_R_BT_CNT_THR_8822B 0
#define BIT_MASK_R_BT_CNT_THR_8822B 0xff
#define BIT_R_BT_CNT_THR_8822B(x)                                              \
	(((x) & BIT_MASK_R_BT_CNT_THR_8822B) << BIT_SHIFT_R_BT_CNT_THR_8822B)
#define BITS_R_BT_CNT_THR_8822B                                                \
	(BIT_MASK_R_BT_CNT_THR_8822B << BIT_SHIFT_R_BT_CNT_THR_8822B)
#define BIT_CLEAR_R_BT_CNT_THR_8822B(x) ((x) & (~BITS_R_BT_CNT_THR_8822B))
#define BIT_GET_R_BT_CNT_THR_8822B(x)                                          \
	(((x) >> BIT_SHIFT_R_BT_CNT_THR_8822B) & BIT_MASK_R_BT_CNT_THR_8822B)
#define BIT_SET_R_BT_CNT_THR_8822B(x, v)                                       \
	(BIT_CLEAR_R_BT_CNT_THR_8822B(x) | BIT_R_BT_CNT_THR_8822B(v))

/* 2 REG_WLAN_ACT_MASK_CTRL_8822B */
#define BIT_WLRX_TER_BY_CTL_8822B BIT(43)
#define BIT_WLRX_TER_BY_AD_8822B BIT(42)
#define BIT_ANT_DIVERSITY_SEL_8822B BIT(41)
#define BIT_ANTSEL_FOR_BT_CTRL_EN_8822B BIT(40)
#define BIT_WLACT_LOW_GNTWL_EN_8822B BIT(34)
#define BIT_WLACT_HIGH_GNTBT_EN_8822B BIT(33)
#define BIT_NAV_UPPER_V1_8822B BIT(32)

#define BIT_SHIFT_RXMYRTS_NAV_V1_8822B 8
#define BIT_MASK_RXMYRTS_NAV_V1_8822B 0xff
#define BIT_RXMYRTS_NAV_V1_8822B(x)                                            \
	(((x) & BIT_MASK_RXMYRTS_NAV_V1_8822B)                                 \
	 << BIT_SHIFT_RXMYRTS_NAV_V1_8822B)
#define BITS_RXMYRTS_NAV_V1_8822B                                              \
	(BIT_MASK_RXMYRTS_NAV_V1_8822B << BIT_SHIFT_RXMYRTS_NAV_V1_8822B)
#define BIT_CLEAR_RXMYRTS_NAV_V1_8822B(x) ((x) & (~BITS_RXMYRTS_NAV_V1_8822B))
#define BIT_GET_RXMYRTS_NAV_V1_8822B(x)                                        \
	(((x) >> BIT_SHIFT_RXMYRTS_NAV_V1_8822B) &                             \
	 BIT_MASK_RXMYRTS_NAV_V1_8822B)
#define BIT_SET_RXMYRTS_NAV_V1_8822B(x, v)                                     \
	(BIT_CLEAR_RXMYRTS_NAV_V1_8822B(x) | BIT_RXMYRTS_NAV_V1_8822B(v))

#define BIT_SHIFT_RTSRST_V1_8822B 0
#define BIT_MASK_RTSRST_V1_8822B 0xff
#define BIT_RTSRST_V1_8822B(x)                                                 \
	(((x) & BIT_MASK_RTSRST_V1_8822B) << BIT_SHIFT_RTSRST_V1_8822B)
#define BITS_RTSRST_V1_8822B                                                   \
	(BIT_MASK_RTSRST_V1_8822B << BIT_SHIFT_RTSRST_V1_8822B)
#define BIT_CLEAR_RTSRST_V1_8822B(x) ((x) & (~BITS_RTSRST_V1_8822B))
#define BIT_GET_RTSRST_V1_8822B(x)                                             \
	(((x) >> BIT_SHIFT_RTSRST_V1_8822B) & BIT_MASK_RTSRST_V1_8822B)
#define BIT_SET_RTSRST_V1_8822B(x, v)                                          \
	(BIT_CLEAR_RTSRST_V1_8822B(x) | BIT_RTSRST_V1_8822B(v))

/* 2 REG_BT_COEX_ENHANCED_INTR_CTRL_8822B */

#define BIT_SHIFT_BT_STAT_DELAY_8822B 12
#define BIT_MASK_BT_STAT_DELAY_8822B 0xf
#define BIT_BT_STAT_DELAY_8822B(x)                                             \
	(((x) & BIT_MASK_BT_STAT_DELAY_8822B) << BIT_SHIFT_BT_STAT_DELAY_8822B)
#define BITS_BT_STAT_DELAY_8822B                                               \
	(BIT_MASK_BT_STAT_DELAY_8822B << BIT_SHIFT_BT_STAT_DELAY_8822B)
#define BIT_CLEAR_BT_STAT_DELAY_8822B(x) ((x) & (~BITS_BT_STAT_DELAY_8822B))
#define BIT_GET_BT_STAT_DELAY_8822B(x)                                         \
	(((x) >> BIT_SHIFT_BT_STAT_DELAY_8822B) & BIT_MASK_BT_STAT_DELAY_8822B)
#define BIT_SET_BT_STAT_DELAY_8822B(x, v)                                      \
	(BIT_CLEAR_BT_STAT_DELAY_8822B(x) | BIT_BT_STAT_DELAY_8822B(v))

#define BIT_SHIFT_BT_TRX_INIT_DETECT_8822B 8
#define BIT_MASK_BT_TRX_INIT_DETECT_8822B 0xf
#define BIT_BT_TRX_INIT_DETECT_8822B(x)                                        \
	(((x) & BIT_MASK_BT_TRX_INIT_DETECT_8822B)                             \
	 << BIT_SHIFT_BT_TRX_INIT_DETECT_8822B)
#define BITS_BT_TRX_INIT_DETECT_8822B                                          \
	(BIT_MASK_BT_TRX_INIT_DETECT_8822B                                     \
	 << BIT_SHIFT_BT_TRX_INIT_DETECT_8822B)
#define BIT_CLEAR_BT_TRX_INIT_DETECT_8822B(x)                                  \
	((x) & (~BITS_BT_TRX_INIT_DETECT_8822B))
#define BIT_GET_BT_TRX_INIT_DETECT_8822B(x)                                    \
	(((x) >> BIT_SHIFT_BT_TRX_INIT_DETECT_8822B) &                         \
	 BIT_MASK_BT_TRX_INIT_DETECT_8822B)
#define BIT_SET_BT_TRX_INIT_DETECT_8822B(x, v)                                 \
	(BIT_CLEAR_BT_TRX_INIT_DETECT_8822B(x) |                               \
	 BIT_BT_TRX_INIT_DETECT_8822B(v))

#define BIT_SHIFT_BT_PRI_DETECT_TO_8822B 4
#define BIT_MASK_BT_PRI_DETECT_TO_8822B 0xf
#define BIT_BT_PRI_DETECT_TO_8822B(x)                                          \
	(((x) & BIT_MASK_BT_PRI_DETECT_TO_8822B)                               \
	 << BIT_SHIFT_BT_PRI_DETECT_TO_8822B)
#define BITS_BT_PRI_DETECT_TO_8822B                                            \
	(BIT_MASK_BT_PRI_DETECT_TO_8822B << BIT_SHIFT_BT_PRI_DETECT_TO_8822B)
#define BIT_CLEAR_BT_PRI_DETECT_TO_8822B(x)                                    \
	((x) & (~BITS_BT_PRI_DETECT_TO_8822B))
#define BIT_GET_BT_PRI_DETECT_TO_8822B(x)                                      \
	(((x) >> BIT_SHIFT_BT_PRI_DETECT_TO_8822B) &                           \
	 BIT_MASK_BT_PRI_DETECT_TO_8822B)
#define BIT_SET_BT_PRI_DETECT_TO_8822B(x, v)                                   \
	(BIT_CLEAR_BT_PRI_DETECT_TO_8822B(x) | BIT_BT_PRI_DETECT_TO_8822B(v))

#define BIT_R_GRANTALL_WLMASK_8822B BIT(3)
#define BIT_STATIS_BT_EN_8822B BIT(2)
#define BIT_WL_ACT_MASK_ENABLE_8822B BIT(1)
#define BIT_ENHANCED_BT_8822B BIT(0)

/* 2 REG_BT_ACT_STATISTICS_8822B */

#define BIT_SHIFT_STATIS_BT_LO_RX_8822B (48 & CPU_OPT_WIDTH)
#define BIT_MASK_STATIS_BT_LO_RX_8822B 0xffff
#define BIT_STATIS_BT_LO_RX_8822B(x)                                           \
	(((x) & BIT_MASK_STATIS_BT_LO_RX_8822B)                                \
	 << BIT_SHIFT_STATIS_BT_LO_RX_8822B)
#define BITS_STATIS_BT_LO_RX_8822B                                             \
	(BIT_MASK_STATIS_BT_LO_RX_8822B << BIT_SHIFT_STATIS_BT_LO_RX_8822B)
#define BIT_CLEAR_STATIS_BT_LO_RX_8822B(x) ((x) & (~BITS_STATIS_BT_LO_RX_8822B))
#define BIT_GET_STATIS_BT_LO_RX_8822B(x)                                       \
	(((x) >> BIT_SHIFT_STATIS_BT_LO_RX_8822B) &                            \
	 BIT_MASK_STATIS_BT_LO_RX_8822B)
#define BIT_SET_STATIS_BT_LO_RX_8822B(x, v)                                    \
	(BIT_CLEAR_STATIS_BT_LO_RX_8822B(x) | BIT_STATIS_BT_LO_RX_8822B(v))

#define BIT_SHIFT_STATIS_BT_LO_TX_8822B (32 & CPU_OPT_WIDTH)
#define BIT_MASK_STATIS_BT_LO_TX_8822B 0xffff
#define BIT_STATIS_BT_LO_TX_8822B(x)                                           \
	(((x) & BIT_MASK_STATIS_BT_LO_TX_8822B)                                \
	 << BIT_SHIFT_STATIS_BT_LO_TX_8822B)
#define BITS_STATIS_BT_LO_TX_8822B                                             \
	(BIT_MASK_STATIS_BT_LO_TX_8822B << BIT_SHIFT_STATIS_BT_LO_TX_8822B)
#define BIT_CLEAR_STATIS_BT_LO_TX_8822B(x) ((x) & (~BITS_STATIS_BT_LO_TX_8822B))
#define BIT_GET_STATIS_BT_LO_TX_8822B(x)                                       \
	(((x) >> BIT_SHIFT_STATIS_BT_LO_TX_8822B) &                            \
	 BIT_MASK_STATIS_BT_LO_TX_8822B)
#define BIT_SET_STATIS_BT_LO_TX_8822B(x, v)                                    \
	(BIT_CLEAR_STATIS_BT_LO_TX_8822B(x) | BIT_STATIS_BT_LO_TX_8822B(v))

#define BIT_SHIFT_STATIS_BT_HI_RX_8822B 16
#define BIT_MASK_STATIS_BT_HI_RX_8822B 0xffff
#define BIT_STATIS_BT_HI_RX_8822B(x)                                           \
	(((x) & BIT_MASK_STATIS_BT_HI_RX_8822B)                                \
	 << BIT_SHIFT_STATIS_BT_HI_RX_8822B)
#define BITS_STATIS_BT_HI_RX_8822B                                             \
	(BIT_MASK_STATIS_BT_HI_RX_8822B << BIT_SHIFT_STATIS_BT_HI_RX_8822B)
#define BIT_CLEAR_STATIS_BT_HI_RX_8822B(x) ((x) & (~BITS_STATIS_BT_HI_RX_8822B))
#define BIT_GET_STATIS_BT_HI_RX_8822B(x)                                       \
	(((x) >> BIT_SHIFT_STATIS_BT_HI_RX_8822B) &                            \
	 BIT_MASK_STATIS_BT_HI_RX_8822B)
#define BIT_SET_STATIS_BT_HI_RX_8822B(x, v)                                    \
	(BIT_CLEAR_STATIS_BT_HI_RX_8822B(x) | BIT_STATIS_BT_HI_RX_8822B(v))

#define BIT_SHIFT_STATIS_BT_HI_TX_8822B 0
#define BIT_MASK_STATIS_BT_HI_TX_8822B 0xffff
#define BIT_STATIS_BT_HI_TX_8822B(x)                                           \
	(((x) & BIT_MASK_STATIS_BT_HI_TX_8822B)                                \
	 << BIT_SHIFT_STATIS_BT_HI_TX_8822B)
#define BITS_STATIS_BT_HI_TX_8822B                                             \
	(BIT_MASK_STATIS_BT_HI_TX_8822B << BIT_SHIFT_STATIS_BT_HI_TX_8822B)
#define BIT_CLEAR_STATIS_BT_HI_TX_8822B(x) ((x) & (~BITS_STATIS_BT_HI_TX_8822B))
#define BIT_GET_STATIS_BT_HI_TX_8822B(x)                                       \
	(((x) >> BIT_SHIFT_STATIS_BT_HI_TX_8822B) &                            \
	 BIT_MASK_STATIS_BT_HI_TX_8822B)
#define BIT_SET_STATIS_BT_HI_TX_8822B(x, v)                                    \
	(BIT_CLEAR_STATIS_BT_HI_TX_8822B(x) | BIT_STATIS_BT_HI_TX_8822B(v))

/* 2 REG_BT_STATISTICS_CONTROL_REGISTER_8822B */

#define BIT_SHIFT_R_BT_CMD_RPT_8822B 16
#define BIT_MASK_R_BT_CMD_RPT_8822B 0xffff
#define BIT_R_BT_CMD_RPT_8822B(x)                                              \
	(((x) & BIT_MASK_R_BT_CMD_RPT_8822B) << BIT_SHIFT_R_BT_CMD_RPT_8822B)
#define BITS_R_BT_CMD_RPT_8822B                                                \
	(BIT_MASK_R_BT_CMD_RPT_8822B << BIT_SHIFT_R_BT_CMD_RPT_8822B)
#define BIT_CLEAR_R_BT_CMD_RPT_8822B(x) ((x) & (~BITS_R_BT_CMD_RPT_8822B))
#define BIT_GET_R_BT_CMD_RPT_8822B(x)                                          \
	(((x) >> BIT_SHIFT_R_BT_CMD_RPT_8822B) & BIT_MASK_R_BT_CMD_RPT_8822B)
#define BIT_SET_R_BT_CMD_RPT_8822B(x, v)                                       \
	(BIT_CLEAR_R_BT_CMD_RPT_8822B(x) | BIT_R_BT_CMD_RPT_8822B(v))

#define BIT_SHIFT_R_RPT_FROM_BT_8822B 8
#define BIT_MASK_R_RPT_FROM_BT_8822B 0xff
#define BIT_R_RPT_FROM_BT_8822B(x)                                             \
	(((x) & BIT_MASK_R_RPT_FROM_BT_8822B) << BIT_SHIFT_R_RPT_FROM_BT_8822B)
#define BITS_R_RPT_FROM_BT_8822B                                               \
	(BIT_MASK_R_RPT_FROM_BT_8822B << BIT_SHIFT_R_RPT_FROM_BT_8822B)
#define BIT_CLEAR_R_RPT_FROM_BT_8822B(x) ((x) & (~BITS_R_RPT_FROM_BT_8822B))
#define BIT_GET_R_RPT_FROM_BT_8822B(x)                                         \
	(((x) >> BIT_SHIFT_R_RPT_FROM_BT_8822B) & BIT_MASK_R_RPT_FROM_BT_8822B)
#define BIT_SET_R_RPT_FROM_BT_8822B(x, v)                                      \
	(BIT_CLEAR_R_RPT_FROM_BT_8822B(x) | BIT_R_RPT_FROM_BT_8822B(v))

#define BIT_SHIFT_BT_HID_ISR_SET_8822B 6
#define BIT_MASK_BT_HID_ISR_SET_8822B 0x3
#define BIT_BT_HID_ISR_SET_8822B(x)                                            \
	(((x) & BIT_MASK_BT_HID_ISR_SET_8822B)                                 \
	 << BIT_SHIFT_BT_HID_ISR_SET_8822B)
#define BITS_BT_HID_ISR_SET_8822B                                              \
	(BIT_MASK_BT_HID_ISR_SET_8822B << BIT_SHIFT_BT_HID_ISR_SET_8822B)
#define BIT_CLEAR_BT_HID_ISR_SET_8822B(x) ((x) & (~BITS_BT_HID_ISR_SET_8822B))
#define BIT_GET_BT_HID_ISR_SET_8822B(x)                                        \
	(((x) >> BIT_SHIFT_BT_HID_ISR_SET_8822B) &                             \
	 BIT_MASK_BT_HID_ISR_SET_8822B)
#define BIT_SET_BT_HID_ISR_SET_8822B(x, v)                                     \
	(BIT_CLEAR_BT_HID_ISR_SET_8822B(x) | BIT_BT_HID_ISR_SET_8822B(v))

#define BIT_TDMA_BT_START_NOTIFY_8822B BIT(5)
#define BIT_ENABLE_TDMA_FW_MODE_8822B BIT(4)
#define BIT_ENABLE_PTA_TDMA_MODE_8822B BIT(3)
#define BIT_ENABLE_COEXIST_TAB_IN_TDMA_8822B BIT(2)
#define BIT_GPIO2_GPIO3_EXANGE_OR_NO_BT_CCA_8822B BIT(1)
#define BIT_RTK_BT_ENABLE_8822B BIT(0)

/* 2 REG_BT_STATUS_REPORT_REGISTER_8822B */

#define BIT_SHIFT_BT_PROFILE_8822B 24
#define BIT_MASK_BT_PROFILE_8822B 0xff
#define BIT_BT_PROFILE_8822B(x)                                                \
	(((x) & BIT_MASK_BT_PROFILE_8822B) << BIT_SHIFT_BT_PROFILE_8822B)
#define BITS_BT_PROFILE_8822B                                                  \
	(BIT_MASK_BT_PROFILE_8822B << BIT_SHIFT_BT_PROFILE_8822B)
#define BIT_CLEAR_BT_PROFILE_8822B(x) ((x) & (~BITS_BT_PROFILE_8822B))
#define BIT_GET_BT_PROFILE_8822B(x)                                            \
	(((x) >> BIT_SHIFT_BT_PROFILE_8822B) & BIT_MASK_BT_PROFILE_8822B)
#define BIT_SET_BT_PROFILE_8822B(x, v)                                         \
	(BIT_CLEAR_BT_PROFILE_8822B(x) | BIT_BT_PROFILE_8822B(v))

#define BIT_SHIFT_BT_POWER_8822B 16
#define BIT_MASK_BT_POWER_8822B 0xff
#define BIT_BT_POWER_8822B(x)                                                  \
	(((x) & BIT_MASK_BT_POWER_8822B) << BIT_SHIFT_BT_POWER_8822B)
#define BITS_BT_POWER_8822B                                                    \
	(BIT_MASK_BT_POWER_8822B << BIT_SHIFT_BT_POWER_8822B)
#define BIT_CLEAR_BT_POWER_8822B(x) ((x) & (~BITS_BT_POWER_8822B))
#define BIT_GET_BT_POWER_8822B(x)                                              \
	(((x) >> BIT_SHIFT_BT_POWER_8822B) & BIT_MASK_BT_POWER_8822B)
#define BIT_SET_BT_POWER_8822B(x, v)                                           \
	(BIT_CLEAR_BT_POWER_8822B(x) | BIT_BT_POWER_8822B(v))

#define BIT_SHIFT_BT_PREDECT_STATUS_8822B 8
#define BIT_MASK_BT_PREDECT_STATUS_8822B 0xff
#define BIT_BT_PREDECT_STATUS_8822B(x)                                         \
	(((x) & BIT_MASK_BT_PREDECT_STATUS_8822B)                              \
	 << BIT_SHIFT_BT_PREDECT_STATUS_8822B)
#define BITS_BT_PREDECT_STATUS_8822B                                           \
	(BIT_MASK_BT_PREDECT_STATUS_8822B << BIT_SHIFT_BT_PREDECT_STATUS_8822B)
#define BIT_CLEAR_BT_PREDECT_STATUS_8822B(x)                                   \
	((x) & (~BITS_BT_PREDECT_STATUS_8822B))
#define BIT_GET_BT_PREDECT_STATUS_8822B(x)                                     \
	(((x) >> BIT_SHIFT_BT_PREDECT_STATUS_8822B) &                          \
	 BIT_MASK_BT_PREDECT_STATUS_8822B)
#define BIT_SET_BT_PREDECT_STATUS_8822B(x, v)                                  \
	(BIT_CLEAR_BT_PREDECT_STATUS_8822B(x) | BIT_BT_PREDECT_STATUS_8822B(v))

#define BIT_SHIFT_BT_CMD_INFO_8822B 0
#define BIT_MASK_BT_CMD_INFO_8822B 0xff
#define BIT_BT_CMD_INFO_8822B(x)                                               \
	(((x) & BIT_MASK_BT_CMD_INFO_8822B) << BIT_SHIFT_BT_CMD_INFO_8822B)
#define BITS_BT_CMD_INFO_8822B                                                 \
	(BIT_MASK_BT_CMD_INFO_8822B << BIT_SHIFT_BT_CMD_INFO_8822B)
#define BIT_CLEAR_BT_CMD_INFO_8822B(x) ((x) & (~BITS_BT_CMD_INFO_8822B))
#define BIT_GET_BT_CMD_INFO_8822B(x)                                           \
	(((x) >> BIT_SHIFT_BT_CMD_INFO_8822B) & BIT_MASK_BT_CMD_INFO_8822B)
#define BIT_SET_BT_CMD_INFO_8822B(x, v)                                        \
	(BIT_CLEAR_BT_CMD_INFO_8822B(x) | BIT_BT_CMD_INFO_8822B(v))

/* 2 REG_BT_INTERRUPT_CONTROL_REGISTER_8822B */
#define BIT_EN_MAC_NULL_PKT_NOTIFY_8822B BIT(31)
#define BIT_EN_WLAN_RPT_AND_BT_QUERY_8822B BIT(30)
#define BIT_EN_BT_STSTUS_RPT_8822B BIT(29)
#define BIT_EN_BT_POWER_8822B BIT(28)
#define BIT_EN_BT_CHANNEL_8822B BIT(27)
#define BIT_EN_BT_SLOT_CHANGE_8822B BIT(26)
#define BIT_EN_BT_PROFILE_OR_HID_8822B BIT(25)
#define BIT_WLAN_RPT_NOTIFY_8822B BIT(24)

#define BIT_SHIFT_WLAN_RPT_DATA_8822B 16
#define BIT_MASK_WLAN_RPT_DATA_8822B 0xff
#define BIT_WLAN_RPT_DATA_8822B(x)                                             \
	(((x) & BIT_MASK_WLAN_RPT_DATA_8822B) << BIT_SHIFT_WLAN_RPT_DATA_8822B)
#define BITS_WLAN_RPT_DATA_8822B                                               \
	(BIT_MASK_WLAN_RPT_DATA_8822B << BIT_SHIFT_WLAN_RPT_DATA_8822B)
#define BIT_CLEAR_WLAN_RPT_DATA_8822B(x) ((x) & (~BITS_WLAN_RPT_DATA_8822B))
#define BIT_GET_WLAN_RPT_DATA_8822B(x)                                         \
	(((x) >> BIT_SHIFT_WLAN_RPT_DATA_8822B) & BIT_MASK_WLAN_RPT_DATA_8822B)
#define BIT_SET_WLAN_RPT_DATA_8822B(x, v)                                      \
	(BIT_CLEAR_WLAN_RPT_DATA_8822B(x) | BIT_WLAN_RPT_DATA_8822B(v))

#define BIT_SHIFT_CMD_ID_8822B 8
#define BIT_MASK_CMD_ID_8822B 0xff
#define BIT_CMD_ID_8822B(x)                                                    \
	(((x) & BIT_MASK_CMD_ID_8822B) << BIT_SHIFT_CMD_ID_8822B)
#define BITS_CMD_ID_8822B (BIT_MASK_CMD_ID_8822B << BIT_SHIFT_CMD_ID_8822B)
#define BIT_CLEAR_CMD_ID_8822B(x) ((x) & (~BITS_CMD_ID_8822B))
#define BIT_GET_CMD_ID_8822B(x)                                                \
	(((x) >> BIT_SHIFT_CMD_ID_8822B) & BIT_MASK_CMD_ID_8822B)
#define BIT_SET_CMD_ID_8822B(x, v)                                             \
	(BIT_CLEAR_CMD_ID_8822B(x) | BIT_CMD_ID_8822B(v))

#define BIT_SHIFT_BT_DATA_8822B 0
#define BIT_MASK_BT_DATA_8822B 0xff
#define BIT_BT_DATA_8822B(x)                                                   \
	(((x) & BIT_MASK_BT_DATA_8822B) << BIT_SHIFT_BT_DATA_8822B)
#define BITS_BT_DATA_8822B (BIT_MASK_BT_DATA_8822B << BIT_SHIFT_BT_DATA_8822B)
#define BIT_CLEAR_BT_DATA_8822B(x) ((x) & (~BITS_BT_DATA_8822B))
#define BIT_GET_BT_DATA_8822B(x)                                               \
	(((x) >> BIT_SHIFT_BT_DATA_8822B) & BIT_MASK_BT_DATA_8822B)
#define BIT_SET_BT_DATA_8822B(x, v)                                            \
	(BIT_CLEAR_BT_DATA_8822B(x) | BIT_BT_DATA_8822B(v))

/* 2 REG_WLAN_REPORT_TIME_OUT_CONTROL_REGISTER_8822B */

#define BIT_SHIFT_WLAN_RPT_TO_8822B 0
#define BIT_MASK_WLAN_RPT_TO_8822B 0xff
#define BIT_WLAN_RPT_TO_8822B(x)                                               \
	(((x) & BIT_MASK_WLAN_RPT_TO_8822B) << BIT_SHIFT_WLAN_RPT_TO_8822B)
#define BITS_WLAN_RPT_TO_8822B                                                 \
	(BIT_MASK_WLAN_RPT_TO_8822B << BIT_SHIFT_WLAN_RPT_TO_8822B)
#define BIT_CLEAR_WLAN_RPT_TO_8822B(x) ((x) & (~BITS_WLAN_RPT_TO_8822B))
#define BIT_GET_WLAN_RPT_TO_8822B(x)                                           \
	(((x) >> BIT_SHIFT_WLAN_RPT_TO_8822B) & BIT_MASK_WLAN_RPT_TO_8822B)
#define BIT_SET_WLAN_RPT_TO_8822B(x, v)                                        \
	(BIT_CLEAR_WLAN_RPT_TO_8822B(x) | BIT_WLAN_RPT_TO_8822B(v))

/* 2 REG_BT_ISOLATION_TABLE_REGISTER_REGISTER_8822B */

#define BIT_SHIFT_ISOLATION_CHK_8822B 1
#define BIT_MASK_ISOLATION_CHK_8822B 0x7fffffffffffffffffffL
#define BIT_ISOLATION_CHK_8822B(x)                                             \
	(((x) & BIT_MASK_ISOLATION_CHK_8822B) << BIT_SHIFT_ISOLATION_CHK_8822B)
#define BITS_ISOLATION_CHK_8822B                                               \
	(BIT_MASK_ISOLATION_CHK_8822B << BIT_SHIFT_ISOLATION_CHK_8822B)
#define BIT_CLEAR_ISOLATION_CHK_8822B(x) ((x) & (~BITS_ISOLATION_CHK_8822B))
#define BIT_GET_ISOLATION_CHK_8822B(x)                                         \
	(((x) >> BIT_SHIFT_ISOLATION_CHK_8822B) & BIT_MASK_ISOLATION_CHK_8822B)
#define BIT_SET_ISOLATION_CHK_8822B(x, v)                                      \
	(BIT_CLEAR_ISOLATION_CHK_8822B(x) | BIT_ISOLATION_CHK_8822B(v))

#define BIT_ISOLATION_EN_8822B BIT(0)

/* 2 REG_BT_INTERRUPT_STATUS_REGISTER_8822B */
#define BIT_BT_HID_ISR_8822B BIT(7)
#define BIT_BT_QUERY_ISR_8822B BIT(6)
#define BIT_MAC_NULL_PKT_NOTIFY_ISR_8822B BIT(5)
#define BIT_WLAN_RPT_ISR_8822B BIT(4)
#define BIT_BT_POWER_ISR_8822B BIT(3)
#define BIT_BT_CHANNEL_ISR_8822B BIT(2)
#define BIT_BT_SLOT_CHANGE_ISR_8822B BIT(1)
#define BIT_BT_PROFILE_ISR_8822B BIT(0)

/* 2 REG_BT_TDMA_TIME_REGISTER_8822B */

#define BIT_SHIFT_BT_TIME_8822B 6
#define BIT_MASK_BT_TIME_8822B 0x3ffffff
#define BIT_BT_TIME_8822B(x)                                                   \
	(((x) & BIT_MASK_BT_TIME_8822B) << BIT_SHIFT_BT_TIME_8822B)
#define BITS_BT_TIME_8822B (BIT_MASK_BT_TIME_8822B << BIT_SHIFT_BT_TIME_8822B)
#define BIT_CLEAR_BT_TIME_8822B(x) ((x) & (~BITS_BT_TIME_8822B))
#define BIT_GET_BT_TIME_8822B(x)                                               \
	(((x) >> BIT_SHIFT_BT_TIME_8822B) & BIT_MASK_BT_TIME_8822B)
#define BIT_SET_BT_TIME_8822B(x, v)                                            \
	(BIT_CLEAR_BT_TIME_8822B(x) | BIT_BT_TIME_8822B(v))

#define BIT_SHIFT_BT_RPT_SAMPLE_RATE_8822B 0
#define BIT_MASK_BT_RPT_SAMPLE_RATE_8822B 0x3f
#define BIT_BT_RPT_SAMPLE_RATE_8822B(x)                                        \
	(((x) & BIT_MASK_BT_RPT_SAMPLE_RATE_8822B)                             \
	 << BIT_SHIFT_BT_RPT_SAMPLE_RATE_8822B)
#define BITS_BT_RPT_SAMPLE_RATE_8822B                                          \
	(BIT_MASK_BT_RPT_SAMPLE_RATE_8822B                                     \
	 << BIT_SHIFT_BT_RPT_SAMPLE_RATE_8822B)
#define BIT_CLEAR_BT_RPT_SAMPLE_RATE_8822B(x)                                  \
	((x) & (~BITS_BT_RPT_SAMPLE_RATE_8822B))
#define BIT_GET_BT_RPT_SAMPLE_RATE_8822B(x)                                    \
	(((x) >> BIT_SHIFT_BT_RPT_SAMPLE_RATE_8822B) &                         \
	 BIT_MASK_BT_RPT_SAMPLE_RATE_8822B)
#define BIT_SET_BT_RPT_SAMPLE_RATE_8822B(x, v)                                 \
	(BIT_CLEAR_BT_RPT_SAMPLE_RATE_8822B(x) |                               \
	 BIT_BT_RPT_SAMPLE_RATE_8822B(v))

/* 2 REG_BT_ACT_REGISTER_8822B */

#define BIT_SHIFT_BT_EISR_EN_8822B 16
#define BIT_MASK_BT_EISR_EN_8822B 0xff
#define BIT_BT_EISR_EN_8822B(x)                                                \
	(((x) & BIT_MASK_BT_EISR_EN_8822B) << BIT_SHIFT_BT_EISR_EN_8822B)
#define BITS_BT_EISR_EN_8822B                                                  \
	(BIT_MASK_BT_EISR_EN_8822B << BIT_SHIFT_BT_EISR_EN_8822B)
#define BIT_CLEAR_BT_EISR_EN_8822B(x) ((x) & (~BITS_BT_EISR_EN_8822B))
#define BIT_GET_BT_EISR_EN_8822B(x)                                            \
	(((x) >> BIT_SHIFT_BT_EISR_EN_8822B) & BIT_MASK_BT_EISR_EN_8822B)
#define BIT_SET_BT_EISR_EN_8822B(x, v)                                         \
	(BIT_CLEAR_BT_EISR_EN_8822B(x) | BIT_BT_EISR_EN_8822B(v))

#define BIT_BT_ACT_FALLING_ISR_8822B BIT(10)
#define BIT_BT_ACT_RISING_ISR_8822B BIT(9)
#define BIT_TDMA_TO_ISR_8822B BIT(8)

#define BIT_SHIFT_BT_CH_8822B 0
#define BIT_MASK_BT_CH_8822B 0xff
#define BIT_BT_CH_8822B(x)                                                     \
	(((x) & BIT_MASK_BT_CH_8822B) << BIT_SHIFT_BT_CH_8822B)
#define BITS_BT_CH_8822B (BIT_MASK_BT_CH_8822B << BIT_SHIFT_BT_CH_8822B)
#define BIT_CLEAR_BT_CH_8822B(x) ((x) & (~BITS_BT_CH_8822B))
#define BIT_GET_BT_CH_8822B(x)                                                 \
	(((x) >> BIT_SHIFT_BT_CH_8822B) & BIT_MASK_BT_CH_8822B)
#define BIT_SET_BT_CH_8822B(x, v)                                              \
	(BIT_CLEAR_BT_CH_8822B(x) | BIT_BT_CH_8822B(v))

/* 2 REG_OBFF_CTRL_BASIC_8822B */
#define BIT_OBFF_EN_V1_8822B BIT(31)

#define BIT_SHIFT_OBFF_STATE_V1_8822B 28
#define BIT_MASK_OBFF_STATE_V1_8822B 0x3
#define BIT_OBFF_STATE_V1_8822B(x)                                             \
	(((x) & BIT_MASK_OBFF_STATE_V1_8822B) << BIT_SHIFT_OBFF_STATE_V1_8822B)
#define BITS_OBFF_STATE_V1_8822B                                               \
	(BIT_MASK_OBFF_STATE_V1_8822B << BIT_SHIFT_OBFF_STATE_V1_8822B)
#define BIT_CLEAR_OBFF_STATE_V1_8822B(x) ((x) & (~BITS_OBFF_STATE_V1_8822B))
#define BIT_GET_OBFF_STATE_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_OBFF_STATE_V1_8822B) & BIT_MASK_OBFF_STATE_V1_8822B)
#define BIT_SET_OBFF_STATE_V1_8822B(x, v)                                      \
	(BIT_CLEAR_OBFF_STATE_V1_8822B(x) | BIT_OBFF_STATE_V1_8822B(v))

#define BIT_OBFF_ACT_RXDMA_EN_8822B BIT(27)
#define BIT_OBFF_BLOCK_INT_EN_8822B BIT(26)
#define BIT_OBFF_AUTOACT_EN_8822B BIT(25)
#define BIT_OBFF_AUTOIDLE_EN_8822B BIT(24)

#define BIT_SHIFT_WAKE_MAX_PLS_8822B 20
#define BIT_MASK_WAKE_MAX_PLS_8822B 0x7
#define BIT_WAKE_MAX_PLS_8822B(x)                                              \
	(((x) & BIT_MASK_WAKE_MAX_PLS_8822B) << BIT_SHIFT_WAKE_MAX_PLS_8822B)
#define BITS_WAKE_MAX_PLS_8822B                                                \
	(BIT_MASK_WAKE_MAX_PLS_8822B << BIT_SHIFT_WAKE_MAX_PLS_8822B)
#define BIT_CLEAR_WAKE_MAX_PLS_8822B(x) ((x) & (~BITS_WAKE_MAX_PLS_8822B))
#define BIT_GET_WAKE_MAX_PLS_8822B(x)                                          \
	(((x) >> BIT_SHIFT_WAKE_MAX_PLS_8822B) & BIT_MASK_WAKE_MAX_PLS_8822B)
#define BIT_SET_WAKE_MAX_PLS_8822B(x, v)                                       \
	(BIT_CLEAR_WAKE_MAX_PLS_8822B(x) | BIT_WAKE_MAX_PLS_8822B(v))

#define BIT_SHIFT_WAKE_MIN_PLS_8822B 16
#define BIT_MASK_WAKE_MIN_PLS_8822B 0x7
#define BIT_WAKE_MIN_PLS_8822B(x)                                              \
	(((x) & BIT_MASK_WAKE_MIN_PLS_8822B) << BIT_SHIFT_WAKE_MIN_PLS_8822B)
#define BITS_WAKE_MIN_PLS_8822B                                                \
	(BIT_MASK_WAKE_MIN_PLS_8822B << BIT_SHIFT_WAKE_MIN_PLS_8822B)
#define BIT_CLEAR_WAKE_MIN_PLS_8822B(x) ((x) & (~BITS_WAKE_MIN_PLS_8822B))
#define BIT_GET_WAKE_MIN_PLS_8822B(x)                                          \
	(((x) >> BIT_SHIFT_WAKE_MIN_PLS_8822B) & BIT_MASK_WAKE_MIN_PLS_8822B)
#define BIT_SET_WAKE_MIN_PLS_8822B(x, v)                                       \
	(BIT_CLEAR_WAKE_MIN_PLS_8822B(x) | BIT_WAKE_MIN_PLS_8822B(v))

#define BIT_SHIFT_WAKE_MAX_F2F_8822B 12
#define BIT_MASK_WAKE_MAX_F2F_8822B 0x7
#define BIT_WAKE_MAX_F2F_8822B(x)                                              \
	(((x) & BIT_MASK_WAKE_MAX_F2F_8822B) << BIT_SHIFT_WAKE_MAX_F2F_8822B)
#define BITS_WAKE_MAX_F2F_8822B                                                \
	(BIT_MASK_WAKE_MAX_F2F_8822B << BIT_SHIFT_WAKE_MAX_F2F_8822B)
#define BIT_CLEAR_WAKE_MAX_F2F_8822B(x) ((x) & (~BITS_WAKE_MAX_F2F_8822B))
#define BIT_GET_WAKE_MAX_F2F_8822B(x)                                          \
	(((x) >> BIT_SHIFT_WAKE_MAX_F2F_8822B) & BIT_MASK_WAKE_MAX_F2F_8822B)
#define BIT_SET_WAKE_MAX_F2F_8822B(x, v)                                       \
	(BIT_CLEAR_WAKE_MAX_F2F_8822B(x) | BIT_WAKE_MAX_F2F_8822B(v))

#define BIT_SHIFT_WAKE_MIN_F2F_8822B 8
#define BIT_MASK_WAKE_MIN_F2F_8822B 0x7
#define BIT_WAKE_MIN_F2F_8822B(x)                                              \
	(((x) & BIT_MASK_WAKE_MIN_F2F_8822B) << BIT_SHIFT_WAKE_MIN_F2F_8822B)
#define BITS_WAKE_MIN_F2F_8822B                                                \
	(BIT_MASK_WAKE_MIN_F2F_8822B << BIT_SHIFT_WAKE_MIN_F2F_8822B)
#define BIT_CLEAR_WAKE_MIN_F2F_8822B(x) ((x) & (~BITS_WAKE_MIN_F2F_8822B))
#define BIT_GET_WAKE_MIN_F2F_8822B(x)                                          \
	(((x) >> BIT_SHIFT_WAKE_MIN_F2F_8822B) & BIT_MASK_WAKE_MIN_F2F_8822B)
#define BIT_SET_WAKE_MIN_F2F_8822B(x, v)                                       \
	(BIT_CLEAR_WAKE_MIN_F2F_8822B(x) | BIT_WAKE_MIN_F2F_8822B(v))

#define BIT_APP_CPU_ACT_V1_8822B BIT(3)
#define BIT_APP_OBFF_V1_8822B BIT(2)
#define BIT_APP_IDLE_V1_8822B BIT(1)
#define BIT_APP_INIT_V1_8822B BIT(0)

/* 2 REG_OBFF_CTRL2_TIMER_8822B */

#define BIT_SHIFT_RX_HIGH_TIMER_IDX_8822B 24
#define BIT_MASK_RX_HIGH_TIMER_IDX_8822B 0x7
#define BIT_RX_HIGH_TIMER_IDX_8822B(x)                                         \
	(((x) & BIT_MASK_RX_HIGH_TIMER_IDX_8822B)                              \
	 << BIT_SHIFT_RX_HIGH_TIMER_IDX_8822B)
#define BITS_RX_HIGH_TIMER_IDX_8822B                                           \
	(BIT_MASK_RX_HIGH_TIMER_IDX_8822B << BIT_SHIFT_RX_HIGH_TIMER_IDX_8822B)
#define BIT_CLEAR_RX_HIGH_TIMER_IDX_8822B(x)                                   \
	((x) & (~BITS_RX_HIGH_TIMER_IDX_8822B))
#define BIT_GET_RX_HIGH_TIMER_IDX_8822B(x)                                     \
	(((x) >> BIT_SHIFT_RX_HIGH_TIMER_IDX_8822B) &                          \
	 BIT_MASK_RX_HIGH_TIMER_IDX_8822B)
#define BIT_SET_RX_HIGH_TIMER_IDX_8822B(x, v)                                  \
	(BIT_CLEAR_RX_HIGH_TIMER_IDX_8822B(x) | BIT_RX_HIGH_TIMER_IDX_8822B(v))

#define BIT_SHIFT_RX_MED_TIMER_IDX_8822B 16
#define BIT_MASK_RX_MED_TIMER_IDX_8822B 0x7
#define BIT_RX_MED_TIMER_IDX_8822B(x)                                          \
	(((x) & BIT_MASK_RX_MED_TIMER_IDX_8822B)                               \
	 << BIT_SHIFT_RX_MED_TIMER_IDX_8822B)
#define BITS_RX_MED_TIMER_IDX_8822B                                            \
	(BIT_MASK_RX_MED_TIMER_IDX_8822B << BIT_SHIFT_RX_MED_TIMER_IDX_8822B)
#define BIT_CLEAR_RX_MED_TIMER_IDX_8822B(x)                                    \
	((x) & (~BITS_RX_MED_TIMER_IDX_8822B))
#define BIT_GET_RX_MED_TIMER_IDX_8822B(x)                                      \
	(((x) >> BIT_SHIFT_RX_MED_TIMER_IDX_8822B) &                           \
	 BIT_MASK_RX_MED_TIMER_IDX_8822B)
#define BIT_SET_RX_MED_TIMER_IDX_8822B(x, v)                                   \
	(BIT_CLEAR_RX_MED_TIMER_IDX_8822B(x) | BIT_RX_MED_TIMER_IDX_8822B(v))

#define BIT_SHIFT_RX_LOW_TIMER_IDX_8822B 8
#define BIT_MASK_RX_LOW_TIMER_IDX_8822B 0x7
#define BIT_RX_LOW_TIMER_IDX_8822B(x)                                          \
	(((x) & BIT_MASK_RX_LOW_TIMER_IDX_8822B)                               \
	 << BIT_SHIFT_RX_LOW_TIMER_IDX_8822B)
#define BITS_RX_LOW_TIMER_IDX_8822B                                            \
	(BIT_MASK_RX_LOW_TIMER_IDX_8822B << BIT_SHIFT_RX_LOW_TIMER_IDX_8822B)
#define BIT_CLEAR_RX_LOW_TIMER_IDX_8822B(x)                                    \
	((x) & (~BITS_RX_LOW_TIMER_IDX_8822B))
#define BIT_GET_RX_LOW_TIMER_IDX_8822B(x)                                      \
	(((x) >> BIT_SHIFT_RX_LOW_TIMER_IDX_8822B) &                           \
	 BIT_MASK_RX_LOW_TIMER_IDX_8822B)
#define BIT_SET_RX_LOW_TIMER_IDX_8822B(x, v)                                   \
	(BIT_CLEAR_RX_LOW_TIMER_IDX_8822B(x) | BIT_RX_LOW_TIMER_IDX_8822B(v))

#define BIT_SHIFT_OBFF_INT_TIMER_IDX_8822B 0
#define BIT_MASK_OBFF_INT_TIMER_IDX_8822B 0x7
#define BIT_OBFF_INT_TIMER_IDX_8822B(x)                                        \
	(((x) & BIT_MASK_OBFF_INT_TIMER_IDX_8822B)                             \
	 << BIT_SHIFT_OBFF_INT_TIMER_IDX_8822B)
#define BITS_OBFF_INT_TIMER_IDX_8822B                                          \
	(BIT_MASK_OBFF_INT_TIMER_IDX_8822B                                     \
	 << BIT_SHIFT_OBFF_INT_TIMER_IDX_8822B)
#define BIT_CLEAR_OBFF_INT_TIMER_IDX_8822B(x)                                  \
	((x) & (~BITS_OBFF_INT_TIMER_IDX_8822B))
#define BIT_GET_OBFF_INT_TIMER_IDX_8822B(x)                                    \
	(((x) >> BIT_SHIFT_OBFF_INT_TIMER_IDX_8822B) &                         \
	 BIT_MASK_OBFF_INT_TIMER_IDX_8822B)
#define BIT_SET_OBFF_INT_TIMER_IDX_8822B(x, v)                                 \
	(BIT_CLEAR_OBFF_INT_TIMER_IDX_8822B(x) |                               \
	 BIT_OBFF_INT_TIMER_IDX_8822B(v))

/* 2 REG_LTR_CTRL_BASIC_8822B */
#define BIT_LTR_EN_V1_8822B BIT(31)
#define BIT_LTR_HW_EN_V1_8822B BIT(30)
#define BIT_LRT_ACT_CTS_EN_8822B BIT(29)
#define BIT_LTR_ACT_RXPKT_EN_8822B BIT(28)
#define BIT_LTR_ACT_RXDMA_EN_8822B BIT(27)
#define BIT_LTR_IDLE_NO_SNOOP_8822B BIT(26)
#define BIT_SPDUP_MGTPKT_8822B BIT(25)
#define BIT_RX_AGG_EN_8822B BIT(24)
#define BIT_APP_LTR_ACT_8822B BIT(23)
#define BIT_APP_LTR_IDLE_8822B BIT(22)

#define BIT_SHIFT_HIGH_RATE_TRIG_SEL_8822B 20
#define BIT_MASK_HIGH_RATE_TRIG_SEL_8822B 0x3
#define BIT_HIGH_RATE_TRIG_SEL_8822B(x)                                        \
	(((x) & BIT_MASK_HIGH_RATE_TRIG_SEL_8822B)                             \
	 << BIT_SHIFT_HIGH_RATE_TRIG_SEL_8822B)
#define BITS_HIGH_RATE_TRIG_SEL_8822B                                          \
	(BIT_MASK_HIGH_RATE_TRIG_SEL_8822B                                     \
	 << BIT_SHIFT_HIGH_RATE_TRIG_SEL_8822B)
#define BIT_CLEAR_HIGH_RATE_TRIG_SEL_8822B(x)                                  \
	((x) & (~BITS_HIGH_RATE_TRIG_SEL_8822B))
#define BIT_GET_HIGH_RATE_TRIG_SEL_8822B(x)                                    \
	(((x) >> BIT_SHIFT_HIGH_RATE_TRIG_SEL_8822B) &                         \
	 BIT_MASK_HIGH_RATE_TRIG_SEL_8822B)
#define BIT_SET_HIGH_RATE_TRIG_SEL_8822B(x, v)                                 \
	(BIT_CLEAR_HIGH_RATE_TRIG_SEL_8822B(x) |                               \
	 BIT_HIGH_RATE_TRIG_SEL_8822B(v))

#define BIT_SHIFT_MED_RATE_TRIG_SEL_8822B 18
#define BIT_MASK_MED_RATE_TRIG_SEL_8822B 0x3
#define BIT_MED_RATE_TRIG_SEL_8822B(x)                                         \
	(((x) & BIT_MASK_MED_RATE_TRIG_SEL_8822B)                              \
	 << BIT_SHIFT_MED_RATE_TRIG_SEL_8822B)
#define BITS_MED_RATE_TRIG_SEL_8822B                                           \
	(BIT_MASK_MED_RATE_TRIG_SEL_8822B << BIT_SHIFT_MED_RATE_TRIG_SEL_8822B)
#define BIT_CLEAR_MED_RATE_TRIG_SEL_8822B(x)                                   \
	((x) & (~BITS_MED_RATE_TRIG_SEL_8822B))
#define BIT_GET_MED_RATE_TRIG_SEL_8822B(x)                                     \
	(((x) >> BIT_SHIFT_MED_RATE_TRIG_SEL_8822B) &                          \
	 BIT_MASK_MED_RATE_TRIG_SEL_8822B)
#define BIT_SET_MED_RATE_TRIG_SEL_8822B(x, v)                                  \
	(BIT_CLEAR_MED_RATE_TRIG_SEL_8822B(x) | BIT_MED_RATE_TRIG_SEL_8822B(v))

#define BIT_SHIFT_LOW_RATE_TRIG_SEL_8822B 16
#define BIT_MASK_LOW_RATE_TRIG_SEL_8822B 0x3
#define BIT_LOW_RATE_TRIG_SEL_8822B(x)                                         \
	(((x) & BIT_MASK_LOW_RATE_TRIG_SEL_8822B)                              \
	 << BIT_SHIFT_LOW_RATE_TRIG_SEL_8822B)
#define BITS_LOW_RATE_TRIG_SEL_8822B                                           \
	(BIT_MASK_LOW_RATE_TRIG_SEL_8822B << BIT_SHIFT_LOW_RATE_TRIG_SEL_8822B)
#define BIT_CLEAR_LOW_RATE_TRIG_SEL_8822B(x)                                   \
	((x) & (~BITS_LOW_RATE_TRIG_SEL_8822B))
#define BIT_GET_LOW_RATE_TRIG_SEL_8822B(x)                                     \
	(((x) >> BIT_SHIFT_LOW_RATE_TRIG_SEL_8822B) &                          \
	 BIT_MASK_LOW_RATE_TRIG_SEL_8822B)
#define BIT_SET_LOW_RATE_TRIG_SEL_8822B(x, v)                                  \
	(BIT_CLEAR_LOW_RATE_TRIG_SEL_8822B(x) | BIT_LOW_RATE_TRIG_SEL_8822B(v))

#define BIT_SHIFT_HIGH_RATE_BD_IDX_8822B 8
#define BIT_MASK_HIGH_RATE_BD_IDX_8822B 0x7f
#define BIT_HIGH_RATE_BD_IDX_8822B(x)                                          \
	(((x) & BIT_MASK_HIGH_RATE_BD_IDX_8822B)                               \
	 << BIT_SHIFT_HIGH_RATE_BD_IDX_8822B)
#define BITS_HIGH_RATE_BD_IDX_8822B                                            \
	(BIT_MASK_HIGH_RATE_BD_IDX_8822B << BIT_SHIFT_HIGH_RATE_BD_IDX_8822B)
#define BIT_CLEAR_HIGH_RATE_BD_IDX_8822B(x)                                    \
	((x) & (~BITS_HIGH_RATE_BD_IDX_8822B))
#define BIT_GET_HIGH_RATE_BD_IDX_8822B(x)                                      \
	(((x) >> BIT_SHIFT_HIGH_RATE_BD_IDX_8822B) &                           \
	 BIT_MASK_HIGH_RATE_BD_IDX_8822B)
#define BIT_SET_HIGH_RATE_BD_IDX_8822B(x, v)                                   \
	(BIT_CLEAR_HIGH_RATE_BD_IDX_8822B(x) | BIT_HIGH_RATE_BD_IDX_8822B(v))

#define BIT_SHIFT_LOW_RATE_BD_IDX_8822B 0
#define BIT_MASK_LOW_RATE_BD_IDX_8822B 0x7f
#define BIT_LOW_RATE_BD_IDX_8822B(x)                                           \
	(((x) & BIT_MASK_LOW_RATE_BD_IDX_8822B)                                \
	 << BIT_SHIFT_LOW_RATE_BD_IDX_8822B)
#define BITS_LOW_RATE_BD_IDX_8822B                                             \
	(BIT_MASK_LOW_RATE_BD_IDX_8822B << BIT_SHIFT_LOW_RATE_BD_IDX_8822B)
#define BIT_CLEAR_LOW_RATE_BD_IDX_8822B(x) ((x) & (~BITS_LOW_RATE_BD_IDX_8822B))
#define BIT_GET_LOW_RATE_BD_IDX_8822B(x)                                       \
	(((x) >> BIT_SHIFT_LOW_RATE_BD_IDX_8822B) &                            \
	 BIT_MASK_LOW_RATE_BD_IDX_8822B)
#define BIT_SET_LOW_RATE_BD_IDX_8822B(x, v)                                    \
	(BIT_CLEAR_LOW_RATE_BD_IDX_8822B(x) | BIT_LOW_RATE_BD_IDX_8822B(v))

/* 2 REG_LTR_CTRL2_TIMER_THRESHOLD_8822B */

#define BIT_SHIFT_RX_EMPTY_TIMER_IDX_8822B 24
#define BIT_MASK_RX_EMPTY_TIMER_IDX_8822B 0x7
#define BIT_RX_EMPTY_TIMER_IDX_8822B(x)                                        \
	(((x) & BIT_MASK_RX_EMPTY_TIMER_IDX_8822B)                             \
	 << BIT_SHIFT_RX_EMPTY_TIMER_IDX_8822B)
#define BITS_RX_EMPTY_TIMER_IDX_8822B                                          \
	(BIT_MASK_RX_EMPTY_TIMER_IDX_8822B                                     \
	 << BIT_SHIFT_RX_EMPTY_TIMER_IDX_8822B)
#define BIT_CLEAR_RX_EMPTY_TIMER_IDX_8822B(x)                                  \
	((x) & (~BITS_RX_EMPTY_TIMER_IDX_8822B))
#define BIT_GET_RX_EMPTY_TIMER_IDX_8822B(x)                                    \
	(((x) >> BIT_SHIFT_RX_EMPTY_TIMER_IDX_8822B) &                         \
	 BIT_MASK_RX_EMPTY_TIMER_IDX_8822B)
#define BIT_SET_RX_EMPTY_TIMER_IDX_8822B(x, v)                                 \
	(BIT_CLEAR_RX_EMPTY_TIMER_IDX_8822B(x) |                               \
	 BIT_RX_EMPTY_TIMER_IDX_8822B(v))

#define BIT_SHIFT_RX_AFULL_TH_IDX_8822B 20
#define BIT_MASK_RX_AFULL_TH_IDX_8822B 0x7
#define BIT_RX_AFULL_TH_IDX_8822B(x)                                           \
	(((x) & BIT_MASK_RX_AFULL_TH_IDX_8822B)                                \
	 << BIT_SHIFT_RX_AFULL_TH_IDX_8822B)
#define BITS_RX_AFULL_TH_IDX_8822B                                             \
	(BIT_MASK_RX_AFULL_TH_IDX_8822B << BIT_SHIFT_RX_AFULL_TH_IDX_8822B)
#define BIT_CLEAR_RX_AFULL_TH_IDX_8822B(x) ((x) & (~BITS_RX_AFULL_TH_IDX_8822B))
#define BIT_GET_RX_AFULL_TH_IDX_8822B(x)                                       \
	(((x) >> BIT_SHIFT_RX_AFULL_TH_IDX_8822B) &                            \
	 BIT_MASK_RX_AFULL_TH_IDX_8822B)
#define BIT_SET_RX_AFULL_TH_IDX_8822B(x, v)                                    \
	(BIT_CLEAR_RX_AFULL_TH_IDX_8822B(x) | BIT_RX_AFULL_TH_IDX_8822B(v))

#define BIT_SHIFT_RX_HIGH_TH_IDX_8822B 16
#define BIT_MASK_RX_HIGH_TH_IDX_8822B 0x7
#define BIT_RX_HIGH_TH_IDX_8822B(x)                                            \
	(((x) & BIT_MASK_RX_HIGH_TH_IDX_8822B)                                 \
	 << BIT_SHIFT_RX_HIGH_TH_IDX_8822B)
#define BITS_RX_HIGH_TH_IDX_8822B                                              \
	(BIT_MASK_RX_HIGH_TH_IDX_8822B << BIT_SHIFT_RX_HIGH_TH_IDX_8822B)
#define BIT_CLEAR_RX_HIGH_TH_IDX_8822B(x) ((x) & (~BITS_RX_HIGH_TH_IDX_8822B))
#define BIT_GET_RX_HIGH_TH_IDX_8822B(x)                                        \
	(((x) >> BIT_SHIFT_RX_HIGH_TH_IDX_8822B) &                             \
	 BIT_MASK_RX_HIGH_TH_IDX_8822B)
#define BIT_SET_RX_HIGH_TH_IDX_8822B(x, v)                                     \
	(BIT_CLEAR_RX_HIGH_TH_IDX_8822B(x) | BIT_RX_HIGH_TH_IDX_8822B(v))

#define BIT_SHIFT_RX_MED_TH_IDX_8822B 12
#define BIT_MASK_RX_MED_TH_IDX_8822B 0x7
#define BIT_RX_MED_TH_IDX_8822B(x)                                             \
	(((x) & BIT_MASK_RX_MED_TH_IDX_8822B) << BIT_SHIFT_RX_MED_TH_IDX_8822B)
#define BITS_RX_MED_TH_IDX_8822B                                               \
	(BIT_MASK_RX_MED_TH_IDX_8822B << BIT_SHIFT_RX_MED_TH_IDX_8822B)
#define BIT_CLEAR_RX_MED_TH_IDX_8822B(x) ((x) & (~BITS_RX_MED_TH_IDX_8822B))
#define BIT_GET_RX_MED_TH_IDX_8822B(x)                                         \
	(((x) >> BIT_SHIFT_RX_MED_TH_IDX_8822B) & BIT_MASK_RX_MED_TH_IDX_8822B)
#define BIT_SET_RX_MED_TH_IDX_8822B(x, v)                                      \
	(BIT_CLEAR_RX_MED_TH_IDX_8822B(x) | BIT_RX_MED_TH_IDX_8822B(v))

#define BIT_SHIFT_RX_LOW_TH_IDX_8822B 8
#define BIT_MASK_RX_LOW_TH_IDX_8822B 0x7
#define BIT_RX_LOW_TH_IDX_8822B(x)                                             \
	(((x) & BIT_MASK_RX_LOW_TH_IDX_8822B) << BIT_SHIFT_RX_LOW_TH_IDX_8822B)
#define BITS_RX_LOW_TH_IDX_8822B                                               \
	(BIT_MASK_RX_LOW_TH_IDX_8822B << BIT_SHIFT_RX_LOW_TH_IDX_8822B)
#define BIT_CLEAR_RX_LOW_TH_IDX_8822B(x) ((x) & (~BITS_RX_LOW_TH_IDX_8822B))
#define BIT_GET_RX_LOW_TH_IDX_8822B(x)                                         \
	(((x) >> BIT_SHIFT_RX_LOW_TH_IDX_8822B) & BIT_MASK_RX_LOW_TH_IDX_8822B)
#define BIT_SET_RX_LOW_TH_IDX_8822B(x, v)                                      \
	(BIT_CLEAR_RX_LOW_TH_IDX_8822B(x) | BIT_RX_LOW_TH_IDX_8822B(v))

#define BIT_SHIFT_LTR_SPACE_IDX_8822B 4
#define BIT_MASK_LTR_SPACE_IDX_8822B 0x3
#define BIT_LTR_SPACE_IDX_8822B(x)                                             \
	(((x) & BIT_MASK_LTR_SPACE_IDX_8822B) << BIT_SHIFT_LTR_SPACE_IDX_8822B)
#define BITS_LTR_SPACE_IDX_8822B                                               \
	(BIT_MASK_LTR_SPACE_IDX_8822B << BIT_SHIFT_LTR_SPACE_IDX_8822B)
#define BIT_CLEAR_LTR_SPACE_IDX_8822B(x) ((x) & (~BITS_LTR_SPACE_IDX_8822B))
#define BIT_GET_LTR_SPACE_IDX_8822B(x)                                         \
	(((x) >> BIT_SHIFT_LTR_SPACE_IDX_8822B) & BIT_MASK_LTR_SPACE_IDX_8822B)
#define BIT_SET_LTR_SPACE_IDX_8822B(x, v)                                      \
	(BIT_CLEAR_LTR_SPACE_IDX_8822B(x) | BIT_LTR_SPACE_IDX_8822B(v))

#define BIT_SHIFT_LTR_IDLE_TIMER_IDX_8822B 0
#define BIT_MASK_LTR_IDLE_TIMER_IDX_8822B 0x7
#define BIT_LTR_IDLE_TIMER_IDX_8822B(x)                                        \
	(((x) & BIT_MASK_LTR_IDLE_TIMER_IDX_8822B)                             \
	 << BIT_SHIFT_LTR_IDLE_TIMER_IDX_8822B)
#define BITS_LTR_IDLE_TIMER_IDX_8822B                                          \
	(BIT_MASK_LTR_IDLE_TIMER_IDX_8822B                                     \
	 << BIT_SHIFT_LTR_IDLE_TIMER_IDX_8822B)
#define BIT_CLEAR_LTR_IDLE_TIMER_IDX_8822B(x)                                  \
	((x) & (~BITS_LTR_IDLE_TIMER_IDX_8822B))
#define BIT_GET_LTR_IDLE_TIMER_IDX_8822B(x)                                    \
	(((x) >> BIT_SHIFT_LTR_IDLE_TIMER_IDX_8822B) &                         \
	 BIT_MASK_LTR_IDLE_TIMER_IDX_8822B)
#define BIT_SET_LTR_IDLE_TIMER_IDX_8822B(x, v)                                 \
	(BIT_CLEAR_LTR_IDLE_TIMER_IDX_8822B(x) |                               \
	 BIT_LTR_IDLE_TIMER_IDX_8822B(v))

/* 2 REG_LTR_IDLE_LATENCY_V1_8822B */

#define BIT_SHIFT_LTR_IDLE_L_8822B 0
#define BIT_MASK_LTR_IDLE_L_8822B 0xffffffffL
#define BIT_LTR_IDLE_L_8822B(x)                                                \
	(((x) & BIT_MASK_LTR_IDLE_L_8822B) << BIT_SHIFT_LTR_IDLE_L_8822B)
#define BITS_LTR_IDLE_L_8822B                                                  \
	(BIT_MASK_LTR_IDLE_L_8822B << BIT_SHIFT_LTR_IDLE_L_8822B)
#define BIT_CLEAR_LTR_IDLE_L_8822B(x) ((x) & (~BITS_LTR_IDLE_L_8822B))
#define BIT_GET_LTR_IDLE_L_8822B(x)                                            \
	(((x) >> BIT_SHIFT_LTR_IDLE_L_8822B) & BIT_MASK_LTR_IDLE_L_8822B)
#define BIT_SET_LTR_IDLE_L_8822B(x, v)                                         \
	(BIT_CLEAR_LTR_IDLE_L_8822B(x) | BIT_LTR_IDLE_L_8822B(v))

/* 2 REG_LTR_ACTIVE_LATENCY_V1_8822B */

#define BIT_SHIFT_LTR_ACT_L_8822B 0
#define BIT_MASK_LTR_ACT_L_8822B 0xffffffffL
#define BIT_LTR_ACT_L_8822B(x)                                                 \
	(((x) & BIT_MASK_LTR_ACT_L_8822B) << BIT_SHIFT_LTR_ACT_L_8822B)
#define BITS_LTR_ACT_L_8822B                                                   \
	(BIT_MASK_LTR_ACT_L_8822B << BIT_SHIFT_LTR_ACT_L_8822B)
#define BIT_CLEAR_LTR_ACT_L_8822B(x) ((x) & (~BITS_LTR_ACT_L_8822B))
#define BIT_GET_LTR_ACT_L_8822B(x)                                             \
	(((x) >> BIT_SHIFT_LTR_ACT_L_8822B) & BIT_MASK_LTR_ACT_L_8822B)
#define BIT_SET_LTR_ACT_L_8822B(x, v)                                          \
	(BIT_CLEAR_LTR_ACT_L_8822B(x) | BIT_LTR_ACT_L_8822B(v))

/* 2 REG_ANTENNA_TRAINING_CONTROL_REGISTER_8822B */
#define BIT_APPEND_MACID_IN_RESP_EN_8822B BIT(50)
#define BIT_ADDR2_MATCH_EN_8822B BIT(49)
#define BIT_ANTTRN_EN_8822B BIT(48)

#define BIT_SHIFT_TRAIN_STA_ADDR_8822B 0
#define BIT_MASK_TRAIN_STA_ADDR_8822B 0xffffffffffffL
#define BIT_TRAIN_STA_ADDR_8822B(x)                                            \
	(((x) & BIT_MASK_TRAIN_STA_ADDR_8822B)                                 \
	 << BIT_SHIFT_TRAIN_STA_ADDR_8822B)
#define BITS_TRAIN_STA_ADDR_8822B                                              \
	(BIT_MASK_TRAIN_STA_ADDR_8822B << BIT_SHIFT_TRAIN_STA_ADDR_8822B)
#define BIT_CLEAR_TRAIN_STA_ADDR_8822B(x) ((x) & (~BITS_TRAIN_STA_ADDR_8822B))
#define BIT_GET_TRAIN_STA_ADDR_8822B(x)                                        \
	(((x) >> BIT_SHIFT_TRAIN_STA_ADDR_8822B) &                             \
	 BIT_MASK_TRAIN_STA_ADDR_8822B)
#define BIT_SET_TRAIN_STA_ADDR_8822B(x, v)                                     \
	(BIT_CLEAR_TRAIN_STA_ADDR_8822B(x) | BIT_TRAIN_STA_ADDR_8822B(v))

/* 2 REG_RSVD_0X7B4_8822B */

/* 2 REG_WMAC_PKTCNT_RWD_8822B */

#define BIT_SHIFT_PKTCNT_BSSIDMAP_8822B 4
#define BIT_MASK_PKTCNT_BSSIDMAP_8822B 0xf
#define BIT_PKTCNT_BSSIDMAP_8822B(x)                                           \
	(((x) & BIT_MASK_PKTCNT_BSSIDMAP_8822B)                                \
	 << BIT_SHIFT_PKTCNT_BSSIDMAP_8822B)
#define BITS_PKTCNT_BSSIDMAP_8822B                                             \
	(BIT_MASK_PKTCNT_BSSIDMAP_8822B << BIT_SHIFT_PKTCNT_BSSIDMAP_8822B)
#define BIT_CLEAR_PKTCNT_BSSIDMAP_8822B(x) ((x) & (~BITS_PKTCNT_BSSIDMAP_8822B))
#define BIT_GET_PKTCNT_BSSIDMAP_8822B(x)                                       \
	(((x) >> BIT_SHIFT_PKTCNT_BSSIDMAP_8822B) &                            \
	 BIT_MASK_PKTCNT_BSSIDMAP_8822B)
#define BIT_SET_PKTCNT_BSSIDMAP_8822B(x, v)                                    \
	(BIT_CLEAR_PKTCNT_BSSIDMAP_8822B(x) | BIT_PKTCNT_BSSIDMAP_8822B(v))

#define BIT_PKTCNT_CNTRST_8822B BIT(1)
#define BIT_PKTCNT_CNTEN_8822B BIT(0)

/* 2 REG_WMAC_PKTCNT_CTRL_8822B */
#define BIT_WMAC_PKTCNT_TRST_8822B BIT(9)
#define BIT_WMAC_PKTCNT_FEN_8822B BIT(8)

#define BIT_SHIFT_WMAC_PKTCNT_CFGAD_8822B 0
#define BIT_MASK_WMAC_PKTCNT_CFGAD_8822B 0xff
#define BIT_WMAC_PKTCNT_CFGAD_8822B(x)                                         \
	(((x) & BIT_MASK_WMAC_PKTCNT_CFGAD_8822B)                              \
	 << BIT_SHIFT_WMAC_PKTCNT_CFGAD_8822B)
#define BITS_WMAC_PKTCNT_CFGAD_8822B                                           \
	(BIT_MASK_WMAC_PKTCNT_CFGAD_8822B << BIT_SHIFT_WMAC_PKTCNT_CFGAD_8822B)
#define BIT_CLEAR_WMAC_PKTCNT_CFGAD_8822B(x)                                   \
	((x) & (~BITS_WMAC_PKTCNT_CFGAD_8822B))
#define BIT_GET_WMAC_PKTCNT_CFGAD_8822B(x)                                     \
	(((x) >> BIT_SHIFT_WMAC_PKTCNT_CFGAD_8822B) &                          \
	 BIT_MASK_WMAC_PKTCNT_CFGAD_8822B)
#define BIT_SET_WMAC_PKTCNT_CFGAD_8822B(x, v)                                  \
	(BIT_CLEAR_WMAC_PKTCNT_CFGAD_8822B(x) | BIT_WMAC_PKTCNT_CFGAD_8822B(v))

/* 2 REG_IQ_DUMP_8822B */

#define BIT_SHIFT_R_WMAC_MATCH_REF_MAC_8822B (64 & CPU_OPT_WIDTH)
#define BIT_MASK_R_WMAC_MATCH_REF_MAC_8822B 0xffffffffL
#define BIT_R_WMAC_MATCH_REF_MAC_8822B(x)                                      \
	(((x) & BIT_MASK_R_WMAC_MATCH_REF_MAC_8822B)                           \
	 << BIT_SHIFT_R_WMAC_MATCH_REF_MAC_8822B)
#define BITS_R_WMAC_MATCH_REF_MAC_8822B                                        \
	(BIT_MASK_R_WMAC_MATCH_REF_MAC_8822B                                   \
	 << BIT_SHIFT_R_WMAC_MATCH_REF_MAC_8822B)
#define BIT_CLEAR_R_WMAC_MATCH_REF_MAC_8822B(x)                                \
	((x) & (~BITS_R_WMAC_MATCH_REF_MAC_8822B))
#define BIT_GET_R_WMAC_MATCH_REF_MAC_8822B(x)                                  \
	(((x) >> BIT_SHIFT_R_WMAC_MATCH_REF_MAC_8822B) &                       \
	 BIT_MASK_R_WMAC_MATCH_REF_MAC_8822B)
#define BIT_SET_R_WMAC_MATCH_REF_MAC_8822B(x, v)                               \
	(BIT_CLEAR_R_WMAC_MATCH_REF_MAC_8822B(x) |                             \
	 BIT_R_WMAC_MATCH_REF_MAC_8822B(v))

#define BIT_SHIFT_R_WMAC_MASK_LA_MAC_8822B (32 & CPU_OPT_WIDTH)
#define BIT_MASK_R_WMAC_MASK_LA_MAC_8822B 0xffffffffL
#define BIT_R_WMAC_MASK_LA_MAC_8822B(x)                                        \
	(((x) & BIT_MASK_R_WMAC_MASK_LA_MAC_8822B)                             \
	 << BIT_SHIFT_R_WMAC_MASK_LA_MAC_8822B)
#define BITS_R_WMAC_MASK_LA_MAC_8822B                                          \
	(BIT_MASK_R_WMAC_MASK_LA_MAC_8822B                                     \
	 << BIT_SHIFT_R_WMAC_MASK_LA_MAC_8822B)
#define BIT_CLEAR_R_WMAC_MASK_LA_MAC_8822B(x)                                  \
	((x) & (~BITS_R_WMAC_MASK_LA_MAC_8822B))
#define BIT_GET_R_WMAC_MASK_LA_MAC_8822B(x)                                    \
	(((x) >> BIT_SHIFT_R_WMAC_MASK_LA_MAC_8822B) &                         \
	 BIT_MASK_R_WMAC_MASK_LA_MAC_8822B)
#define BIT_SET_R_WMAC_MASK_LA_MAC_8822B(x, v)                                 \
	(BIT_CLEAR_R_WMAC_MASK_LA_MAC_8822B(x) |                               \
	 BIT_R_WMAC_MASK_LA_MAC_8822B(v))

#define BIT_SHIFT_DUMP_OK_ADDR_8822B 16
#define BIT_MASK_DUMP_OK_ADDR_8822B 0xffff
#define BIT_DUMP_OK_ADDR_8822B(x)                                              \
	(((x) & BIT_MASK_DUMP_OK_ADDR_8822B) << BIT_SHIFT_DUMP_OK_ADDR_8822B)
#define BITS_DUMP_OK_ADDR_8822B                                                \
	(BIT_MASK_DUMP_OK_ADDR_8822B << BIT_SHIFT_DUMP_OK_ADDR_8822B)
#define BIT_CLEAR_DUMP_OK_ADDR_8822B(x) ((x) & (~BITS_DUMP_OK_ADDR_8822B))
#define BIT_GET_DUMP_OK_ADDR_8822B(x)                                          \
	(((x) >> BIT_SHIFT_DUMP_OK_ADDR_8822B) & BIT_MASK_DUMP_OK_ADDR_8822B)
#define BIT_SET_DUMP_OK_ADDR_8822B(x, v)                                       \
	(BIT_CLEAR_DUMP_OK_ADDR_8822B(x) | BIT_DUMP_OK_ADDR_8822B(v))

#define BIT_SHIFT_R_TRIG_TIME_SEL_8822B 8
#define BIT_MASK_R_TRIG_TIME_SEL_8822B 0x7f
#define BIT_R_TRIG_TIME_SEL_8822B(x)                                           \
	(((x) & BIT_MASK_R_TRIG_TIME_SEL_8822B)                                \
	 << BIT_SHIFT_R_TRIG_TIME_SEL_8822B)
#define BITS_R_TRIG_TIME_SEL_8822B                                             \
	(BIT_MASK_R_TRIG_TIME_SEL_8822B << BIT_SHIFT_R_TRIG_TIME_SEL_8822B)
#define BIT_CLEAR_R_TRIG_TIME_SEL_8822B(x) ((x) & (~BITS_R_TRIG_TIME_SEL_8822B))
#define BIT_GET_R_TRIG_TIME_SEL_8822B(x)                                       \
	(((x) >> BIT_SHIFT_R_TRIG_TIME_SEL_8822B) &                            \
	 BIT_MASK_R_TRIG_TIME_SEL_8822B)
#define BIT_SET_R_TRIG_TIME_SEL_8822B(x, v)                                    \
	(BIT_CLEAR_R_TRIG_TIME_SEL_8822B(x) | BIT_R_TRIG_TIME_SEL_8822B(v))

#define BIT_SHIFT_R_MAC_TRIG_SEL_8822B 6
#define BIT_MASK_R_MAC_TRIG_SEL_8822B 0x3
#define BIT_R_MAC_TRIG_SEL_8822B(x)                                            \
	(((x) & BIT_MASK_R_MAC_TRIG_SEL_8822B)                                 \
	 << BIT_SHIFT_R_MAC_TRIG_SEL_8822B)
#define BITS_R_MAC_TRIG_SEL_8822B                                              \
	(BIT_MASK_R_MAC_TRIG_SEL_8822B << BIT_SHIFT_R_MAC_TRIG_SEL_8822B)
#define BIT_CLEAR_R_MAC_TRIG_SEL_8822B(x) ((x) & (~BITS_R_MAC_TRIG_SEL_8822B))
#define BIT_GET_R_MAC_TRIG_SEL_8822B(x)                                        \
	(((x) >> BIT_SHIFT_R_MAC_TRIG_SEL_8822B) &                             \
	 BIT_MASK_R_MAC_TRIG_SEL_8822B)
#define BIT_SET_R_MAC_TRIG_SEL_8822B(x, v)                                     \
	(BIT_CLEAR_R_MAC_TRIG_SEL_8822B(x) | BIT_R_MAC_TRIG_SEL_8822B(v))

#define BIT_MAC_TRIG_REG_8822B BIT(5)

#define BIT_SHIFT_R_LEVEL_PULSE_SEL_8822B 3
#define BIT_MASK_R_LEVEL_PULSE_SEL_8822B 0x3
#define BIT_R_LEVEL_PULSE_SEL_8822B(x)                                         \
	(((x) & BIT_MASK_R_LEVEL_PULSE_SEL_8822B)                              \
	 << BIT_SHIFT_R_LEVEL_PULSE_SEL_8822B)
#define BITS_R_LEVEL_PULSE_SEL_8822B                                           \
	(BIT_MASK_R_LEVEL_PULSE_SEL_8822B << BIT_SHIFT_R_LEVEL_PULSE_SEL_8822B)
#define BIT_CLEAR_R_LEVEL_PULSE_SEL_8822B(x)                                   \
	((x) & (~BITS_R_LEVEL_PULSE_SEL_8822B))
#define BIT_GET_R_LEVEL_PULSE_SEL_8822B(x)                                     \
	(((x) >> BIT_SHIFT_R_LEVEL_PULSE_SEL_8822B) &                          \
	 BIT_MASK_R_LEVEL_PULSE_SEL_8822B)
#define BIT_SET_R_LEVEL_PULSE_SEL_8822B(x, v)                                  \
	(BIT_CLEAR_R_LEVEL_PULSE_SEL_8822B(x) | BIT_R_LEVEL_PULSE_SEL_8822B(v))

#define BIT_EN_LA_MAC_8822B BIT(2)
#define BIT_R_EN_IQDUMP_8822B BIT(1)
#define BIT_R_IQDATA_DUMP_8822B BIT(0)

/* 2 REG_WMAC_FTM_CTL_8822B */
#define BIT_RXFTM_TXACK_SC_8822B BIT(6)
#define BIT_RXFTM_TXACK_BW_8822B BIT(5)
#define BIT_RXFTM_EN_8822B BIT(3)
#define BIT_RXFTMREQ_BYDRV_8822B BIT(2)
#define BIT_RXFTMREQ_EN_8822B BIT(1)
#define BIT_FTM_EN_8822B BIT(0)

/* 2 REG_WMAC_IQ_MDPK_FUNC_8822B */

/* 2 REG_WMAC_OPTION_FUNCTION_8822B */

#define BIT_SHIFT_R_WMAC_RX_FIL_LEN_8822B (64 & CPU_OPT_WIDTH)
#define BIT_MASK_R_WMAC_RX_FIL_LEN_8822B 0xffff
#define BIT_R_WMAC_RX_FIL_LEN_8822B(x)                                         \
	(((x) & BIT_MASK_R_WMAC_RX_FIL_LEN_8822B)                              \
	 << BIT_SHIFT_R_WMAC_RX_FIL_LEN_8822B)
#define BITS_R_WMAC_RX_FIL_LEN_8822B                                           \
	(BIT_MASK_R_WMAC_RX_FIL_LEN_8822B << BIT_SHIFT_R_WMAC_RX_FIL_LEN_8822B)
#define BIT_CLEAR_R_WMAC_RX_FIL_LEN_8822B(x)                                   \
	((x) & (~BITS_R_WMAC_RX_FIL_LEN_8822B))
#define BIT_GET_R_WMAC_RX_FIL_LEN_8822B(x)                                     \
	(((x) >> BIT_SHIFT_R_WMAC_RX_FIL_LEN_8822B) &                          \
	 BIT_MASK_R_WMAC_RX_FIL_LEN_8822B)
#define BIT_SET_R_WMAC_RX_FIL_LEN_8822B(x, v)                                  \
	(BIT_CLEAR_R_WMAC_RX_FIL_LEN_8822B(x) | BIT_R_WMAC_RX_FIL_LEN_8822B(v))

#define BIT_SHIFT_R_WMAC_RXFIFO_FULL_TH_8822B (56 & CPU_OPT_WIDTH)
#define BIT_MASK_R_WMAC_RXFIFO_FULL_TH_8822B 0xff
#define BIT_R_WMAC_RXFIFO_FULL_TH_8822B(x)                                     \
	(((x) & BIT_MASK_R_WMAC_RXFIFO_FULL_TH_8822B)                          \
	 << BIT_SHIFT_R_WMAC_RXFIFO_FULL_TH_8822B)
#define BITS_R_WMAC_RXFIFO_FULL_TH_8822B                                       \
	(BIT_MASK_R_WMAC_RXFIFO_FULL_TH_8822B                                  \
	 << BIT_SHIFT_R_WMAC_RXFIFO_FULL_TH_8822B)
#define BIT_CLEAR_R_WMAC_RXFIFO_FULL_TH_8822B(x)                               \
	((x) & (~BITS_R_WMAC_RXFIFO_FULL_TH_8822B))
#define BIT_GET_R_WMAC_RXFIFO_FULL_TH_8822B(x)                                 \
	(((x) >> BIT_SHIFT_R_WMAC_RXFIFO_FULL_TH_8822B) &                      \
	 BIT_MASK_R_WMAC_RXFIFO_FULL_TH_8822B)
#define BIT_SET_R_WMAC_RXFIFO_FULL_TH_8822B(x, v)                              \
	(BIT_CLEAR_R_WMAC_RXFIFO_FULL_TH_8822B(x) |                            \
	 BIT_R_WMAC_RXFIFO_FULL_TH_8822B(v))

#define BIT_R_WMAC_RX_SYNCFIFO_SYNC_8822B BIT(55)
#define BIT_R_WMAC_RXRST_DLY_8822B BIT(54)
#define BIT_R_WMAC_SRCH_TXRPT_REF_DROP_8822B BIT(53)
#define BIT_R_WMAC_SRCH_TXRPT_UA1_8822B BIT(52)
#define BIT_R_WMAC_SRCH_TXRPT_TYPE_8822B BIT(51)
#define BIT_R_WMAC_NDP_RST_8822B BIT(50)
#define BIT_R_WMAC_POWINT_EN_8822B BIT(49)
#define BIT_R_WMAC_SRCH_TXRPT_PERPKT_8822B BIT(48)
#define BIT_R_WMAC_SRCH_TXRPT_MID_8822B BIT(47)
#define BIT_R_WMAC_PFIN_TOEN_8822B BIT(46)
#define BIT_R_WMAC_FIL_SECERR_8822B BIT(45)
#define BIT_R_WMAC_FIL_CTLPKTLEN_8822B BIT(44)
#define BIT_R_WMAC_FIL_FCTYPE_8822B BIT(43)
#define BIT_R_WMAC_FIL_FCPROVER_8822B BIT(42)
#define BIT_R_WMAC_PHYSTS_SNIF_8822B BIT(41)
#define BIT_R_WMAC_PHYSTS_PLCP_8822B BIT(40)
#define BIT_R_MAC_TCR_VBONF_RD_8822B BIT(39)
#define BIT_R_WMAC_TCR_MPAR_NDP_8822B BIT(38)
#define BIT_R_WMAC_NDP_FILTER_8822B BIT(37)
#define BIT_R_WMAC_RXLEN_SEL_8822B BIT(36)
#define BIT_R_WMAC_RXLEN_SEL1_8822B BIT(35)
#define BIT_R_OFDM_FILTER_8822B BIT(34)
#define BIT_R_WMAC_CHK_OFDM_LEN_8822B BIT(33)
#define BIT_R_WMAC_CHK_CCK_LEN_8822B BIT(32)

#define BIT_SHIFT_R_OFDM_LEN_8822B 26
#define BIT_MASK_R_OFDM_LEN_8822B 0x3f
#define BIT_R_OFDM_LEN_8822B(x)                                                \
	(((x) & BIT_MASK_R_OFDM_LEN_8822B) << BIT_SHIFT_R_OFDM_LEN_8822B)
#define BITS_R_OFDM_LEN_8822B                                                  \
	(BIT_MASK_R_OFDM_LEN_8822B << BIT_SHIFT_R_OFDM_LEN_8822B)
#define BIT_CLEAR_R_OFDM_LEN_8822B(x) ((x) & (~BITS_R_OFDM_LEN_8822B))
#define BIT_GET_R_OFDM_LEN_8822B(x)                                            \
	(((x) >> BIT_SHIFT_R_OFDM_LEN_8822B) & BIT_MASK_R_OFDM_LEN_8822B)
#define BIT_SET_R_OFDM_LEN_8822B(x, v)                                         \
	(BIT_CLEAR_R_OFDM_LEN_8822B(x) | BIT_R_OFDM_LEN_8822B(v))

#define BIT_SHIFT_R_CCK_LEN_8822B 0
#define BIT_MASK_R_CCK_LEN_8822B 0xffff
#define BIT_R_CCK_LEN_8822B(x)                                                 \
	(((x) & BIT_MASK_R_CCK_LEN_8822B) << BIT_SHIFT_R_CCK_LEN_8822B)
#define BITS_R_CCK_LEN_8822B                                                   \
	(BIT_MASK_R_CCK_LEN_8822B << BIT_SHIFT_R_CCK_LEN_8822B)
#define BIT_CLEAR_R_CCK_LEN_8822B(x) ((x) & (~BITS_R_CCK_LEN_8822B))
#define BIT_GET_R_CCK_LEN_8822B(x)                                             \
	(((x) >> BIT_SHIFT_R_CCK_LEN_8822B) & BIT_MASK_R_CCK_LEN_8822B)
#define BIT_SET_R_CCK_LEN_8822B(x, v)                                          \
	(BIT_CLEAR_R_CCK_LEN_8822B(x) | BIT_R_CCK_LEN_8822B(v))

/* 2 REG_RX_FILTER_FUNCTION_8822B */
#define BIT_R_WMAC_MHRDDY_LATCH_8822B BIT(14)
#define BIT_R_WMAC_MHRDDY_CLR_8822B BIT(13)
#define BIT_R_RXPKTCTL_FSM_BASED_MPDURDY1_8822B BIT(12)
#define BIT_WMAC_DIS_VHT_PLCP_CHK_MU_8822B BIT(11)
#define BIT_R_CHK_DELIMIT_LEN_8822B BIT(10)
#define BIT_R_REAPTER_ADDR_MATCH_8822B BIT(9)
#define BIT_R_RXPKTCTL_FSM_BASED_MPDURDY_8822B BIT(8)
#define BIT_R_LATCH_MACHRDY_8822B BIT(7)
#define BIT_R_WMAC_RXFIL_REND_8822B BIT(6)
#define BIT_R_WMAC_MPDURDY_CLR_8822B BIT(5)
#define BIT_R_WMAC_CLRRXSEC_8822B BIT(4)
#define BIT_R_WMAC_RXFIL_RDEL_8822B BIT(3)
#define BIT_R_WMAC_RXFIL_FCSE_8822B BIT(2)
#define BIT_R_WMAC_RXFIL_MESH_DEL_8822B BIT(1)
#define BIT_R_WMAC_RXFIL_MASKM_8822B BIT(0)

/* 2 REG_NDP_SIG_8822B */

#define BIT_SHIFT_R_WMAC_TXNDP_SIGB_8822B 0
#define BIT_MASK_R_WMAC_TXNDP_SIGB_8822B 0x1fffff
#define BIT_R_WMAC_TXNDP_SIGB_8822B(x)                                         \
	(((x) & BIT_MASK_R_WMAC_TXNDP_SIGB_8822B)                              \
	 << BIT_SHIFT_R_WMAC_TXNDP_SIGB_8822B)
#define BITS_R_WMAC_TXNDP_SIGB_8822B                                           \
	(BIT_MASK_R_WMAC_TXNDP_SIGB_8822B << BIT_SHIFT_R_WMAC_TXNDP_SIGB_8822B)
#define BIT_CLEAR_R_WMAC_TXNDP_SIGB_8822B(x)                                   \
	((x) & (~BITS_R_WMAC_TXNDP_SIGB_8822B))
#define BIT_GET_R_WMAC_TXNDP_SIGB_8822B(x)                                     \
	(((x) >> BIT_SHIFT_R_WMAC_TXNDP_SIGB_8822B) &                          \
	 BIT_MASK_R_WMAC_TXNDP_SIGB_8822B)
#define BIT_SET_R_WMAC_TXNDP_SIGB_8822B(x, v)                                  \
	(BIT_CLEAR_R_WMAC_TXNDP_SIGB_8822B(x) | BIT_R_WMAC_TXNDP_SIGB_8822B(v))

/* 2 REG_TXCMD_INFO_FOR_RSP_PKT_8822B */

#define BIT_SHIFT_R_MAC_DEBUG_8822B (32 & CPU_OPT_WIDTH)
#define BIT_MASK_R_MAC_DEBUG_8822B 0xffffffffL
#define BIT_R_MAC_DEBUG_8822B(x)                                               \
	(((x) & BIT_MASK_R_MAC_DEBUG_8822B) << BIT_SHIFT_R_MAC_DEBUG_8822B)
#define BITS_R_MAC_DEBUG_8822B                                                 \
	(BIT_MASK_R_MAC_DEBUG_8822B << BIT_SHIFT_R_MAC_DEBUG_8822B)
#define BIT_CLEAR_R_MAC_DEBUG_8822B(x) ((x) & (~BITS_R_MAC_DEBUG_8822B))
#define BIT_GET_R_MAC_DEBUG_8822B(x)                                           \
	(((x) >> BIT_SHIFT_R_MAC_DEBUG_8822B) & BIT_MASK_R_MAC_DEBUG_8822B)
#define BIT_SET_R_MAC_DEBUG_8822B(x, v)                                        \
	(BIT_CLEAR_R_MAC_DEBUG_8822B(x) | BIT_R_MAC_DEBUG_8822B(v))

#define BIT_SHIFT_R_MAC_DBG_SHIFT_8822B 8
#define BIT_MASK_R_MAC_DBG_SHIFT_8822B 0x7
#define BIT_R_MAC_DBG_SHIFT_8822B(x)                                           \
	(((x) & BIT_MASK_R_MAC_DBG_SHIFT_8822B)                                \
	 << BIT_SHIFT_R_MAC_DBG_SHIFT_8822B)
#define BITS_R_MAC_DBG_SHIFT_8822B                                             \
	(BIT_MASK_R_MAC_DBG_SHIFT_8822B << BIT_SHIFT_R_MAC_DBG_SHIFT_8822B)
#define BIT_CLEAR_R_MAC_DBG_SHIFT_8822B(x) ((x) & (~BITS_R_MAC_DBG_SHIFT_8822B))
#define BIT_GET_R_MAC_DBG_SHIFT_8822B(x)                                       \
	(((x) >> BIT_SHIFT_R_MAC_DBG_SHIFT_8822B) &                            \
	 BIT_MASK_R_MAC_DBG_SHIFT_8822B)
#define BIT_SET_R_MAC_DBG_SHIFT_8822B(x, v)                                    \
	(BIT_CLEAR_R_MAC_DBG_SHIFT_8822B(x) | BIT_R_MAC_DBG_SHIFT_8822B(v))

#define BIT_SHIFT_R_MAC_DBG_SEL_8822B 0
#define BIT_MASK_R_MAC_DBG_SEL_8822B 0x3
#define BIT_R_MAC_DBG_SEL_8822B(x)                                             \
	(((x) & BIT_MASK_R_MAC_DBG_SEL_8822B) << BIT_SHIFT_R_MAC_DBG_SEL_8822B)
#define BITS_R_MAC_DBG_SEL_8822B                                               \
	(BIT_MASK_R_MAC_DBG_SEL_8822B << BIT_SHIFT_R_MAC_DBG_SEL_8822B)
#define BIT_CLEAR_R_MAC_DBG_SEL_8822B(x) ((x) & (~BITS_R_MAC_DBG_SEL_8822B))
#define BIT_GET_R_MAC_DBG_SEL_8822B(x)                                         \
	(((x) >> BIT_SHIFT_R_MAC_DBG_SEL_8822B) & BIT_MASK_R_MAC_DBG_SEL_8822B)
#define BIT_SET_R_MAC_DBG_SEL_8822B(x, v)                                      \
	(BIT_CLEAR_R_MAC_DBG_SEL_8822B(x) | BIT_R_MAC_DBG_SEL_8822B(v))

/* 2 REG_RTS_ADDRESS_0_8822B */

/* 2 REG_RTS_ADDRESS_1_8822B */

/* 2 REG_RPFM_MAP1_8822B */
#define BIT_DATA_RPFM15EN_8822B BIT(15)
#define BIT_DATA_RPFM14EN_8822B BIT(14)
#define BIT_DATA_RPFM13EN_8822B BIT(13)
#define BIT_DATA_RPFM12EN_8822B BIT(12)
#define BIT_DATA_RPFM11EN_8822B BIT(11)
#define BIT_DATA_RPFM10EN_8822B BIT(10)
#define BIT_DATA_RPFM9EN_8822B BIT(9)
#define BIT_DATA_RPFM8EN_8822B BIT(8)
#define BIT_DATA_RPFM7EN_8822B BIT(7)
#define BIT_DATA_RPFM6EN_8822B BIT(6)
#define BIT_DATA_RPFM5EN_8822B BIT(5)
#define BIT_DATA_RPFM4EN_8822B BIT(4)
#define BIT_DATA_RPFM3EN_8822B BIT(3)
#define BIT_DATA_RPFM2EN_8822B BIT(2)
#define BIT_DATA_RPFM1EN_8822B BIT(1)
#define BIT_DATA_RPFM0EN_8822B BIT(0)

/* 2 REG_WL2LTECOEX_INDIRECT_ACCESS_CTRL_V1_8822B */
#define BIT_LTECOEX_ACCESS_START_V1_8822B BIT(31)
#define BIT_LTECOEX_WRITE_MODE_V1_8822B BIT(30)
#define BIT_LTECOEX_READY_BIT_V1_8822B BIT(29)

#define BIT_SHIFT_WRITE_BYTE_EN_V1_8822B 16
#define BIT_MASK_WRITE_BYTE_EN_V1_8822B 0xf
#define BIT_WRITE_BYTE_EN_V1_8822B(x)                                          \
	(((x) & BIT_MASK_WRITE_BYTE_EN_V1_8822B)                               \
	 << BIT_SHIFT_WRITE_BYTE_EN_V1_8822B)
#define BITS_WRITE_BYTE_EN_V1_8822B                                            \
	(BIT_MASK_WRITE_BYTE_EN_V1_8822B << BIT_SHIFT_WRITE_BYTE_EN_V1_8822B)
#define BIT_CLEAR_WRITE_BYTE_EN_V1_8822B(x)                                    \
	((x) & (~BITS_WRITE_BYTE_EN_V1_8822B))
#define BIT_GET_WRITE_BYTE_EN_V1_8822B(x)                                      \
	(((x) >> BIT_SHIFT_WRITE_BYTE_EN_V1_8822B) &                           \
	 BIT_MASK_WRITE_BYTE_EN_V1_8822B)
#define BIT_SET_WRITE_BYTE_EN_V1_8822B(x, v)                                   \
	(BIT_CLEAR_WRITE_BYTE_EN_V1_8822B(x) | BIT_WRITE_BYTE_EN_V1_8822B(v))

#define BIT_SHIFT_LTECOEX_REG_ADDR_V1_8822B 0
#define BIT_MASK_LTECOEX_REG_ADDR_V1_8822B 0xffff
#define BIT_LTECOEX_REG_ADDR_V1_8822B(x)                                       \
	(((x) & BIT_MASK_LTECOEX_REG_ADDR_V1_8822B)                            \
	 << BIT_SHIFT_LTECOEX_REG_ADDR_V1_8822B)
#define BITS_LTECOEX_REG_ADDR_V1_8822B                                         \
	(BIT_MASK_LTECOEX_REG_ADDR_V1_8822B                                    \
	 << BIT_SHIFT_LTECOEX_REG_ADDR_V1_8822B)
#define BIT_CLEAR_LTECOEX_REG_ADDR_V1_8822B(x)                                 \
	((x) & (~BITS_LTECOEX_REG_ADDR_V1_8822B))
#define BIT_GET_LTECOEX_REG_ADDR_V1_8822B(x)                                   \
	(((x) >> BIT_SHIFT_LTECOEX_REG_ADDR_V1_8822B) &                        \
	 BIT_MASK_LTECOEX_REG_ADDR_V1_8822B)
#define BIT_SET_LTECOEX_REG_ADDR_V1_8822B(x, v)                                \
	(BIT_CLEAR_LTECOEX_REG_ADDR_V1_8822B(x) |                              \
	 BIT_LTECOEX_REG_ADDR_V1_8822B(v))

/* 2 REG_WL2LTECOEX_INDIRECT_ACCESS_WRITE_DATA_V1_8822B */

#define BIT_SHIFT_LTECOEX_W_DATA_V1_8822B 0
#define BIT_MASK_LTECOEX_W_DATA_V1_8822B 0xffffffffL
#define BIT_LTECOEX_W_DATA_V1_8822B(x)                                         \
	(((x) & BIT_MASK_LTECOEX_W_DATA_V1_8822B)                              \
	 << BIT_SHIFT_LTECOEX_W_DATA_V1_8822B)
#define BITS_LTECOEX_W_DATA_V1_8822B                                           \
	(BIT_MASK_LTECOEX_W_DATA_V1_8822B << BIT_SHIFT_LTECOEX_W_DATA_V1_8822B)
#define BIT_CLEAR_LTECOEX_W_DATA_V1_8822B(x)                                   \
	((x) & (~BITS_LTECOEX_W_DATA_V1_8822B))
#define BIT_GET_LTECOEX_W_DATA_V1_8822B(x)                                     \
	(((x) >> BIT_SHIFT_LTECOEX_W_DATA_V1_8822B) &                          \
	 BIT_MASK_LTECOEX_W_DATA_V1_8822B)
#define BIT_SET_LTECOEX_W_DATA_V1_8822B(x, v)                                  \
	(BIT_CLEAR_LTECOEX_W_DATA_V1_8822B(x) | BIT_LTECOEX_W_DATA_V1_8822B(v))

/* 2 REG_WL2LTECOEX_INDIRECT_ACCESS_READ_DATA_V1_8822B */

#define BIT_SHIFT_LTECOEX_R_DATA_V1_8822B 0
#define BIT_MASK_LTECOEX_R_DATA_V1_8822B 0xffffffffL
#define BIT_LTECOEX_R_DATA_V1_8822B(x)                                         \
	(((x) & BIT_MASK_LTECOEX_R_DATA_V1_8822B)                              \
	 << BIT_SHIFT_LTECOEX_R_DATA_V1_8822B)
#define BITS_LTECOEX_R_DATA_V1_8822B                                           \
	(BIT_MASK_LTECOEX_R_DATA_V1_8822B << BIT_SHIFT_LTECOEX_R_DATA_V1_8822B)
#define BIT_CLEAR_LTECOEX_R_DATA_V1_8822B(x)                                   \
	((x) & (~BITS_LTECOEX_R_DATA_V1_8822B))
#define BIT_GET_LTECOEX_R_DATA_V1_8822B(x)                                     \
	(((x) >> BIT_SHIFT_LTECOEX_R_DATA_V1_8822B) &                          \
	 BIT_MASK_LTECOEX_R_DATA_V1_8822B)
#define BIT_SET_LTECOEX_R_DATA_V1_8822B(x, v)                                  \
	(BIT_CLEAR_LTECOEX_R_DATA_V1_8822B(x) | BIT_LTECOEX_R_DATA_V1_8822B(v))

/* 2 REG_NOT_VALID_8822B */

/* 2 REG_SDIO_TX_CTRL_8822B */

#define BIT_SHIFT_SDIO_INT_TIMEOUT_8822B 16
#define BIT_MASK_SDIO_INT_TIMEOUT_8822B 0xffff
#define BIT_SDIO_INT_TIMEOUT_8822B(x)                                          \
	(((x) & BIT_MASK_SDIO_INT_TIMEOUT_8822B)                               \
	 << BIT_SHIFT_SDIO_INT_TIMEOUT_8822B)
#define BITS_SDIO_INT_TIMEOUT_8822B                                            \
	(BIT_MASK_SDIO_INT_TIMEOUT_8822B << BIT_SHIFT_SDIO_INT_TIMEOUT_8822B)
#define BIT_CLEAR_SDIO_INT_TIMEOUT_8822B(x)                                    \
	((x) & (~BITS_SDIO_INT_TIMEOUT_8822B))
#define BIT_GET_SDIO_INT_TIMEOUT_8822B(x)                                      \
	(((x) >> BIT_SHIFT_SDIO_INT_TIMEOUT_8822B) &                           \
	 BIT_MASK_SDIO_INT_TIMEOUT_8822B)
#define BIT_SET_SDIO_INT_TIMEOUT_8822B(x, v)                                   \
	(BIT_CLEAR_SDIO_INT_TIMEOUT_8822B(x) | BIT_SDIO_INT_TIMEOUT_8822B(v))

#define BIT_IO_ERR_STATUS_8822B BIT(15)
#define BIT_REPLY_ERRCRC_IN_DATA_8822B BIT(9)
#define BIT_EN_CMD53_OVERLAP_8822B BIT(8)
#define BIT_REPLY_ERR_IN_R5_8822B BIT(7)
#define BIT_R18A_EN_8822B BIT(6)
#define BIT_SDIO_CMD_FORCE_VLD_8822B BIT(5)
#define BIT_INIT_CMD_EN_8822B BIT(4)
#define BIT_EN_RXDMA_MASK_INT_8822B BIT(2)
#define BIT_EN_MASK_TIMER_8822B BIT(1)
#define BIT_CMD_ERR_STOP_INT_EN_8822B BIT(0)

/* 2 REG_SDIO_HIMR_8822B */
#define BIT_SDIO_CRCERR_MSK_8822B BIT(31)
#define BIT_SDIO_HSISR3_IND_MSK_8822B BIT(30)
#define BIT_SDIO_HSISR2_IND_MSK_8822B BIT(29)
#define BIT_SDIO_HEISR_IND_MSK_8822B BIT(28)
#define BIT_SDIO_CTWEND_MSK_8822B BIT(27)
#define BIT_SDIO_ATIMEND_E_MSK_8822B BIT(26)
#define BIT_SDIIO_ATIMEND_MSK_8822B BIT(25)
#define BIT_SDIO_OCPINT_MSK_8822B BIT(24)
#define BIT_SDIO_PSTIMEOUT_MSK_8822B BIT(23)
#define BIT_SDIO_GTINT4_MSK_8822B BIT(22)
#define BIT_SDIO_GTINT3_MSK_8822B BIT(21)
#define BIT_SDIO_HSISR_IND_MSK_8822B BIT(20)
#define BIT_SDIO_CPWM2_MSK_8822B BIT(19)
#define BIT_SDIO_CPWM1_MSK_8822B BIT(18)
#define BIT_SDIO_C2HCMD_INT_MSK_8822B BIT(17)
#define BIT_SDIO_BCNERLY_INT_MSK_8822B BIT(16)
#define BIT_SDIO_TXBCNERR_MSK_8822B BIT(7)
#define BIT_SDIO_TXBCNOK_MSK_8822B BIT(6)
#define BIT_SDIO_RXFOVW_MSK_8822B BIT(5)
#define BIT_SDIO_TXFOVW_MSK_8822B BIT(4)
#define BIT_SDIO_RXERR_MSK_8822B BIT(3)
#define BIT_SDIO_TXERR_MSK_8822B BIT(2)
#define BIT_SDIO_AVAL_MSK_8822B BIT(1)
#define BIT_RX_REQUEST_MSK_8822B BIT(0)

/* 2 REG_SDIO_HISR_8822B */
#define BIT_SDIO_CRCERR_8822B BIT(31)
#define BIT_SDIO_HSISR3_IND_8822B BIT(30)
#define BIT_SDIO_HSISR2_IND_8822B BIT(29)
#define BIT_SDIO_HEISR_IND_8822B BIT(28)
#define BIT_SDIO_CTWEND_8822B BIT(27)
#define BIT_SDIO_ATIMEND_E_8822B BIT(26)
#define BIT_SDIO_ATIMEND_8822B BIT(25)
#define BIT_SDIO_OCPINT_8822B BIT(24)
#define BIT_SDIO_PSTIMEOUT_8822B BIT(23)
#define BIT_SDIO_GTINT4_8822B BIT(22)
#define BIT_SDIO_GTINT3_8822B BIT(21)
#define BIT_SDIO_HSISR_IND_8822B BIT(20)
#define BIT_SDIO_CPWM2_8822B BIT(19)
#define BIT_SDIO_CPWM1_8822B BIT(18)
#define BIT_SDIO_C2HCMD_INT_8822B BIT(17)
#define BIT_SDIO_BCNERLY_INT_8822B BIT(16)
#define BIT_SDIO_TXBCNERR_8822B BIT(7)
#define BIT_SDIO_TXBCNOK_8822B BIT(6)
#define BIT_SDIO_RXFOVW_8822B BIT(5)
#define BIT_SDIO_TXFOVW_8822B BIT(4)
#define BIT_SDIO_RXERR_8822B BIT(3)
#define BIT_SDIO_TXERR_8822B BIT(2)
#define BIT_SDIO_AVAL_8822B BIT(1)
#define BIT_RX_REQUEST_8822B BIT(0)

/* 2 REG_SDIO_RX_REQ_LEN_8822B */

#define BIT_SHIFT_RX_REQ_LEN_V1_8822B 0
#define BIT_MASK_RX_REQ_LEN_V1_8822B 0x3ffff
#define BIT_RX_REQ_LEN_V1_8822B(x)                                             \
	(((x) & BIT_MASK_RX_REQ_LEN_V1_8822B) << BIT_SHIFT_RX_REQ_LEN_V1_8822B)
#define BITS_RX_REQ_LEN_V1_8822B                                               \
	(BIT_MASK_RX_REQ_LEN_V1_8822B << BIT_SHIFT_RX_REQ_LEN_V1_8822B)
#define BIT_CLEAR_RX_REQ_LEN_V1_8822B(x) ((x) & (~BITS_RX_REQ_LEN_V1_8822B))
#define BIT_GET_RX_REQ_LEN_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_RX_REQ_LEN_V1_8822B) & BIT_MASK_RX_REQ_LEN_V1_8822B)
#define BIT_SET_RX_REQ_LEN_V1_8822B(x, v)                                      \
	(BIT_CLEAR_RX_REQ_LEN_V1_8822B(x) | BIT_RX_REQ_LEN_V1_8822B(v))

/* 2 REG_SDIO_FREE_TXPG_SEQ_V1_8822B */

#define BIT_SHIFT_FREE_TXPG_SEQ_8822B 0
#define BIT_MASK_FREE_TXPG_SEQ_8822B 0xff
#define BIT_FREE_TXPG_SEQ_8822B(x)                                             \
	(((x) & BIT_MASK_FREE_TXPG_SEQ_8822B) << BIT_SHIFT_FREE_TXPG_SEQ_8822B)
#define BITS_FREE_TXPG_SEQ_8822B                                               \
	(BIT_MASK_FREE_TXPG_SEQ_8822B << BIT_SHIFT_FREE_TXPG_SEQ_8822B)
#define BIT_CLEAR_FREE_TXPG_SEQ_8822B(x) ((x) & (~BITS_FREE_TXPG_SEQ_8822B))
#define BIT_GET_FREE_TXPG_SEQ_8822B(x)                                         \
	(((x) >> BIT_SHIFT_FREE_TXPG_SEQ_8822B) & BIT_MASK_FREE_TXPG_SEQ_8822B)
#define BIT_SET_FREE_TXPG_SEQ_8822B(x, v)                                      \
	(BIT_CLEAR_FREE_TXPG_SEQ_8822B(x) | BIT_FREE_TXPG_SEQ_8822B(v))

/* 2 REG_SDIO_FREE_TXPG_8822B */

#define BIT_SHIFT_MID_FREEPG_V1_8822B 16
#define BIT_MASK_MID_FREEPG_V1_8822B 0xfff
#define BIT_MID_FREEPG_V1_8822B(x)                                             \
	(((x) & BIT_MASK_MID_FREEPG_V1_8822B) << BIT_SHIFT_MID_FREEPG_V1_8822B)
#define BITS_MID_FREEPG_V1_8822B                                               \
	(BIT_MASK_MID_FREEPG_V1_8822B << BIT_SHIFT_MID_FREEPG_V1_8822B)
#define BIT_CLEAR_MID_FREEPG_V1_8822B(x) ((x) & (~BITS_MID_FREEPG_V1_8822B))
#define BIT_GET_MID_FREEPG_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_MID_FREEPG_V1_8822B) & BIT_MASK_MID_FREEPG_V1_8822B)
#define BIT_SET_MID_FREEPG_V1_8822B(x, v)                                      \
	(BIT_CLEAR_MID_FREEPG_V1_8822B(x) | BIT_MID_FREEPG_V1_8822B(v))

#define BIT_SHIFT_HIQ_FREEPG_V1_8822B 0
#define BIT_MASK_HIQ_FREEPG_V1_8822B 0xfff
#define BIT_HIQ_FREEPG_V1_8822B(x)                                             \
	(((x) & BIT_MASK_HIQ_FREEPG_V1_8822B) << BIT_SHIFT_HIQ_FREEPG_V1_8822B)
#define BITS_HIQ_FREEPG_V1_8822B                                               \
	(BIT_MASK_HIQ_FREEPG_V1_8822B << BIT_SHIFT_HIQ_FREEPG_V1_8822B)
#define BIT_CLEAR_HIQ_FREEPG_V1_8822B(x) ((x) & (~BITS_HIQ_FREEPG_V1_8822B))
#define BIT_GET_HIQ_FREEPG_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_HIQ_FREEPG_V1_8822B) & BIT_MASK_HIQ_FREEPG_V1_8822B)
#define BIT_SET_HIQ_FREEPG_V1_8822B(x, v)                                      \
	(BIT_CLEAR_HIQ_FREEPG_V1_8822B(x) | BIT_HIQ_FREEPG_V1_8822B(v))

/* 2 REG_SDIO_FREE_TXPG2_8822B */

#define BIT_SHIFT_PUB_FREEPG_V1_8822B 16
#define BIT_MASK_PUB_FREEPG_V1_8822B 0xfff
#define BIT_PUB_FREEPG_V1_8822B(x)                                             \
	(((x) & BIT_MASK_PUB_FREEPG_V1_8822B) << BIT_SHIFT_PUB_FREEPG_V1_8822B)
#define BITS_PUB_FREEPG_V1_8822B                                               \
	(BIT_MASK_PUB_FREEPG_V1_8822B << BIT_SHIFT_PUB_FREEPG_V1_8822B)
#define BIT_CLEAR_PUB_FREEPG_V1_8822B(x) ((x) & (~BITS_PUB_FREEPG_V1_8822B))
#define BIT_GET_PUB_FREEPG_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_PUB_FREEPG_V1_8822B) & BIT_MASK_PUB_FREEPG_V1_8822B)
#define BIT_SET_PUB_FREEPG_V1_8822B(x, v)                                      \
	(BIT_CLEAR_PUB_FREEPG_V1_8822B(x) | BIT_PUB_FREEPG_V1_8822B(v))

#define BIT_SHIFT_LOW_FREEPG_V1_8822B 0
#define BIT_MASK_LOW_FREEPG_V1_8822B 0xfff
#define BIT_LOW_FREEPG_V1_8822B(x)                                             \
	(((x) & BIT_MASK_LOW_FREEPG_V1_8822B) << BIT_SHIFT_LOW_FREEPG_V1_8822B)
#define BITS_LOW_FREEPG_V1_8822B                                               \
	(BIT_MASK_LOW_FREEPG_V1_8822B << BIT_SHIFT_LOW_FREEPG_V1_8822B)
#define BIT_CLEAR_LOW_FREEPG_V1_8822B(x) ((x) & (~BITS_LOW_FREEPG_V1_8822B))
#define BIT_GET_LOW_FREEPG_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_LOW_FREEPG_V1_8822B) & BIT_MASK_LOW_FREEPG_V1_8822B)
#define BIT_SET_LOW_FREEPG_V1_8822B(x, v)                                      \
	(BIT_CLEAR_LOW_FREEPG_V1_8822B(x) | BIT_LOW_FREEPG_V1_8822B(v))

/* 2 REG_SDIO_OQT_FREE_TXPG_V1_8822B */

#define BIT_SHIFT_NOAC_OQT_FREEPG_V1_8822B 24
#define BIT_MASK_NOAC_OQT_FREEPG_V1_8822B 0xff
#define BIT_NOAC_OQT_FREEPG_V1_8822B(x)                                        \
	(((x) & BIT_MASK_NOAC_OQT_FREEPG_V1_8822B)                             \
	 << BIT_SHIFT_NOAC_OQT_FREEPG_V1_8822B)
#define BITS_NOAC_OQT_FREEPG_V1_8822B                                          \
	(BIT_MASK_NOAC_OQT_FREEPG_V1_8822B                                     \
	 << BIT_SHIFT_NOAC_OQT_FREEPG_V1_8822B)
#define BIT_CLEAR_NOAC_OQT_FREEPG_V1_8822B(x)                                  \
	((x) & (~BITS_NOAC_OQT_FREEPG_V1_8822B))
#define BIT_GET_NOAC_OQT_FREEPG_V1_8822B(x)                                    \
	(((x) >> BIT_SHIFT_NOAC_OQT_FREEPG_V1_8822B) &                         \
	 BIT_MASK_NOAC_OQT_FREEPG_V1_8822B)
#define BIT_SET_NOAC_OQT_FREEPG_V1_8822B(x, v)                                 \
	(BIT_CLEAR_NOAC_OQT_FREEPG_V1_8822B(x) |                               \
	 BIT_NOAC_OQT_FREEPG_V1_8822B(v))

#define BIT_SHIFT_AC_OQT_FREEPG_V1_8822B 16
#define BIT_MASK_AC_OQT_FREEPG_V1_8822B 0xff
#define BIT_AC_OQT_FREEPG_V1_8822B(x)                                          \
	(((x) & BIT_MASK_AC_OQT_FREEPG_V1_8822B)                               \
	 << BIT_SHIFT_AC_OQT_FREEPG_V1_8822B)
#define BITS_AC_OQT_FREEPG_V1_8822B                                            \
	(BIT_MASK_AC_OQT_FREEPG_V1_8822B << BIT_SHIFT_AC_OQT_FREEPG_V1_8822B)
#define BIT_CLEAR_AC_OQT_FREEPG_V1_8822B(x)                                    \
	((x) & (~BITS_AC_OQT_FREEPG_V1_8822B))
#define BIT_GET_AC_OQT_FREEPG_V1_8822B(x)                                      \
	(((x) >> BIT_SHIFT_AC_OQT_FREEPG_V1_8822B) &                           \
	 BIT_MASK_AC_OQT_FREEPG_V1_8822B)
#define BIT_SET_AC_OQT_FREEPG_V1_8822B(x, v)                                   \
	(BIT_CLEAR_AC_OQT_FREEPG_V1_8822B(x) | BIT_AC_OQT_FREEPG_V1_8822B(v))

#define BIT_SHIFT_EXQ_FREEPG_V1_8822B 0
#define BIT_MASK_EXQ_FREEPG_V1_8822B 0xfff
#define BIT_EXQ_FREEPG_V1_8822B(x)                                             \
	(((x) & BIT_MASK_EXQ_FREEPG_V1_8822B) << BIT_SHIFT_EXQ_FREEPG_V1_8822B)
#define BITS_EXQ_FREEPG_V1_8822B                                               \
	(BIT_MASK_EXQ_FREEPG_V1_8822B << BIT_SHIFT_EXQ_FREEPG_V1_8822B)
#define BIT_CLEAR_EXQ_FREEPG_V1_8822B(x) ((x) & (~BITS_EXQ_FREEPG_V1_8822B))
#define BIT_GET_EXQ_FREEPG_V1_8822B(x)                                         \
	(((x) >> BIT_SHIFT_EXQ_FREEPG_V1_8822B) & BIT_MASK_EXQ_FREEPG_V1_8822B)
#define BIT_SET_EXQ_FREEPG_V1_8822B(x, v)                                      \
	(BIT_CLEAR_EXQ_FREEPG_V1_8822B(x) | BIT_EXQ_FREEPG_V1_8822B(v))

/* 2 REG_SDIO_HTSFR_INFO_8822B */

#define BIT_SHIFT_HTSFR1_8822B 16
#define BIT_MASK_HTSFR1_8822B 0xffff
#define BIT_HTSFR1_8822B(x)                                                    \
	(((x) & BIT_MASK_HTSFR1_8822B) << BIT_SHIFT_HTSFR1_8822B)
#define BITS_HTSFR1_8822B (BIT_MASK_HTSFR1_8822B << BIT_SHIFT_HTSFR1_8822B)
#define BIT_CLEAR_HTSFR1_8822B(x) ((x) & (~BITS_HTSFR1_8822B))
#define BIT_GET_HTSFR1_8822B(x)                                                \
	(((x) >> BIT_SHIFT_HTSFR1_8822B) & BIT_MASK_HTSFR1_8822B)
#define BIT_SET_HTSFR1_8822B(x, v)                                             \
	(BIT_CLEAR_HTSFR1_8822B(x) | BIT_HTSFR1_8822B(v))

#define BIT_SHIFT_HTSFR0_8822B 0
#define BIT_MASK_HTSFR0_8822B 0xffff
#define BIT_HTSFR0_8822B(x)                                                    \
	(((x) & BIT_MASK_HTSFR0_8822B) << BIT_SHIFT_HTSFR0_8822B)
#define BITS_HTSFR0_8822B (BIT_MASK_HTSFR0_8822B << BIT_SHIFT_HTSFR0_8822B)
#define BIT_CLEAR_HTSFR0_8822B(x) ((x) & (~BITS_HTSFR0_8822B))
#define BIT_GET_HTSFR0_8822B(x)                                                \
	(((x) >> BIT_SHIFT_HTSFR0_8822B) & BIT_MASK_HTSFR0_8822B)
#define BIT_SET_HTSFR0_8822B(x, v)                                             \
	(BIT_CLEAR_HTSFR0_8822B(x) | BIT_HTSFR0_8822B(v))

/* 2 REG_SDIO_HCPWM1_V2_8822B */
#define BIT_TOGGLE_8822B BIT(7)
#define BIT_CUR_PS_8822B BIT(0)

/* 2 REG_SDIO_HCPWM2_V2_8822B */

/* 2 REG_SDIO_INDIRECT_REG_CFG_8822B */
#define BIT_INDIRECT_REG_RDY_8822B BIT(20)
#define BIT_INDIRECT_REG_R_8822B BIT(19)
#define BIT_INDIRECT_REG_W_8822B BIT(18)

#define BIT_SHIFT_INDIRECT_REG_SIZE_8822B 16
#define BIT_MASK_INDIRECT_REG_SIZE_8822B 0x3
#define BIT_INDIRECT_REG_SIZE_8822B(x)                                         \
	(((x) & BIT_MASK_INDIRECT_REG_SIZE_8822B)                              \
	 << BIT_SHIFT_INDIRECT_REG_SIZE_8822B)
#define BITS_INDIRECT_REG_SIZE_8822B                                           \
	(BIT_MASK_INDIRECT_REG_SIZE_8822B << BIT_SHIFT_INDIRECT_REG_SIZE_8822B)
#define BIT_CLEAR_INDIRECT_REG_SIZE_8822B(x)                                   \
	((x) & (~BITS_INDIRECT_REG_SIZE_8822B))
#define BIT_GET_INDIRECT_REG_SIZE_8822B(x)                                     \
	(((x) >> BIT_SHIFT_INDIRECT_REG_SIZE_8822B) &                          \
	 BIT_MASK_INDIRECT_REG_SIZE_8822B)
#define BIT_SET_INDIRECT_REG_SIZE_8822B(x, v)                                  \
	(BIT_CLEAR_INDIRECT_REG_SIZE_8822B(x) | BIT_INDIRECT_REG_SIZE_8822B(v))

#define BIT_SHIFT_INDIRECT_REG_ADDR_8822B 0
#define BIT_MASK_INDIRECT_REG_ADDR_8822B 0xffff
#define BIT_INDIRECT_REG_ADDR_8822B(x)                                         \
	(((x) & BIT_MASK_INDIRECT_REG_ADDR_8822B)                              \
	 << BIT_SHIFT_INDIRECT_REG_ADDR_8822B)
#define BITS_INDIRECT_REG_ADDR_8822B                                           \
	(BIT_MASK_INDIRECT_REG_ADDR_8822B << BIT_SHIFT_INDIRECT_REG_ADDR_8822B)
#define BIT_CLEAR_INDIRECT_REG_ADDR_8822B(x)                                   \
	((x) & (~BITS_INDIRECT_REG_ADDR_8822B))
#define BIT_GET_INDIRECT_REG_ADDR_8822B(x)                                     \
	(((x) >> BIT_SHIFT_INDIRECT_REG_ADDR_8822B) &                          \
	 BIT_MASK_INDIRECT_REG_ADDR_8822B)
#define BIT_SET_INDIRECT_REG_ADDR_8822B(x, v)                                  \
	(BIT_CLEAR_INDIRECT_REG_ADDR_8822B(x) | BIT_INDIRECT_REG_ADDR_8822B(v))

/* 2 REG_SDIO_INDIRECT_REG_DATA_8822B */

#define BIT_SHIFT_INDIRECT_REG_DATA_8822B 0
#define BIT_MASK_INDIRECT_REG_DATA_8822B 0xffffffffL
#define BIT_INDIRECT_REG_DATA_8822B(x)                                         \
	(((x) & BIT_MASK_INDIRECT_REG_DATA_8822B)                              \
	 << BIT_SHIFT_INDIRECT_REG_DATA_8822B)
#define BITS_INDIRECT_REG_DATA_8822B                                           \
	(BIT_MASK_INDIRECT_REG_DATA_8822B << BIT_SHIFT_INDIRECT_REG_DATA_8822B)
#define BIT_CLEAR_INDIRECT_REG_DATA_8822B(x)                                   \
	((x) & (~BITS_INDIRECT_REG_DATA_8822B))
#define BIT_GET_INDIRECT_REG_DATA_8822B(x)                                     \
	(((x) >> BIT_SHIFT_INDIRECT_REG_DATA_8822B) &                          \
	 BIT_MASK_INDIRECT_REG_DATA_8822B)
#define BIT_SET_INDIRECT_REG_DATA_8822B(x, v)                                  \
	(BIT_CLEAR_INDIRECT_REG_DATA_8822B(x) | BIT_INDIRECT_REG_DATA_8822B(v))

/* 2 REG_SDIO_H2C_8822B */

#define BIT_SHIFT_SDIO_H2C_MSG_8822B 0
#define BIT_MASK_SDIO_H2C_MSG_8822B 0xffffffffL
#define BIT_SDIO_H2C_MSG_8822B(x)                                              \
	(((x) & BIT_MASK_SDIO_H2C_MSG_8822B) << BIT_SHIFT_SDIO_H2C_MSG_8822B)
#define BITS_SDIO_H2C_MSG_8822B                                                \
	(BIT_MASK_SDIO_H2C_MSG_8822B << BIT_SHIFT_SDIO_H2C_MSG_8822B)
#define BIT_CLEAR_SDIO_H2C_MSG_8822B(x) ((x) & (~BITS_SDIO_H2C_MSG_8822B))
#define BIT_GET_SDIO_H2C_MSG_8822B(x)                                          \
	(((x) >> BIT_SHIFT_SDIO_H2C_MSG_8822B) & BIT_MASK_SDIO_H2C_MSG_8822B)
#define BIT_SET_SDIO_H2C_MSG_8822B(x, v)                                       \
	(BIT_CLEAR_SDIO_H2C_MSG_8822B(x) | BIT_SDIO_H2C_MSG_8822B(v))

/* 2 REG_SDIO_C2H_8822B */

#define BIT_SHIFT_SDIO_C2H_MSG_8822B 0
#define BIT_MASK_SDIO_C2H_MSG_8822B 0xffffffffL
#define BIT_SDIO_C2H_MSG_8822B(x)                                              \
	(((x) & BIT_MASK_SDIO_C2H_MSG_8822B) << BIT_SHIFT_SDIO_C2H_MSG_8822B)
#define BITS_SDIO_C2H_MSG_8822B                                                \
	(BIT_MASK_SDIO_C2H_MSG_8822B << BIT_SHIFT_SDIO_C2H_MSG_8822B)
#define BIT_CLEAR_SDIO_C2H_MSG_8822B(x) ((x) & (~BITS_SDIO_C2H_MSG_8822B))
#define BIT_GET_SDIO_C2H_MSG_8822B(x)                                          \
	(((x) >> BIT_SHIFT_SDIO_C2H_MSG_8822B) & BIT_MASK_SDIO_C2H_MSG_8822B)
#define BIT_SET_SDIO_C2H_MSG_8822B(x, v)                                       \
	(BIT_CLEAR_SDIO_C2H_MSG_8822B(x) | BIT_SDIO_C2H_MSG_8822B(v))

/* 2 REG_SDIO_HRPWM1_8822B */
#define BIT_TOGGLE_8822B BIT(7)
#define BIT_ACK_8822B BIT(6)
#define BIT_REQ_PS_8822B BIT(0)

/* 2 REG_SDIO_HRPWM2_8822B */

/* 2 REG_SDIO_HPS_CLKR_8822B */

/* 2 REG_SDIO_BUS_CTRL_8822B */
#define BIT_PAD_CLK_XHGE_EN_8822B BIT(3)
#define BIT_INTER_CLK_EN_8822B BIT(2)
#define BIT_EN_RPT_TXCRC_8822B BIT(1)
#define BIT_DIS_RXDMA_STS_8822B BIT(0)

/* 2 REG_SDIO_HSUS_CTRL_8822B */
#define BIT_INTR_CTRL_8822B BIT(4)
#define BIT_SDIO_VOLTAGE_8822B BIT(3)
#define BIT_BYPASS_INIT_8822B BIT(2)
#define BIT_HCI_RESUME_RDY_8822B BIT(1)
#define BIT_HCI_SUS_REQ_8822B BIT(0)

/* 2 REG_SDIO_RESPONSE_TIMER_8822B */

#define BIT_SHIFT_CMDIN_2RESP_TIMER_8822B 0
#define BIT_MASK_CMDIN_2RESP_TIMER_8822B 0xffff
#define BIT_CMDIN_2RESP_TIMER_8822B(x)                                         \
	(((x) & BIT_MASK_CMDIN_2RESP_TIMER_8822B)                              \
	 << BIT_SHIFT_CMDIN_2RESP_TIMER_8822B)
#define BITS_CMDIN_2RESP_TIMER_8822B                                           \
	(BIT_MASK_CMDIN_2RESP_TIMER_8822B << BIT_SHIFT_CMDIN_2RESP_TIMER_8822B)
#define BIT_CLEAR_CMDIN_2RESP_TIMER_8822B(x)                                   \
	((x) & (~BITS_CMDIN_2RESP_TIMER_8822B))
#define BIT_GET_CMDIN_2RESP_TIMER_8822B(x)                                     \
	(((x) >> BIT_SHIFT_CMDIN_2RESP_TIMER_8822B) &                          \
	 BIT_MASK_CMDIN_2RESP_TIMER_8822B)
#define BIT_SET_CMDIN_2RESP_TIMER_8822B(x, v)                                  \
	(BIT_CLEAR_CMDIN_2RESP_TIMER_8822B(x) | BIT_CMDIN_2RESP_TIMER_8822B(v))

/* 2 REG_SDIO_CMD_CRC_8822B */

#define BIT_SHIFT_SDIO_CMD_CRC_V1_8822B 0
#define BIT_MASK_SDIO_CMD_CRC_V1_8822B 0xff
#define BIT_SDIO_CMD_CRC_V1_8822B(x)                                           \
	(((x) & BIT_MASK_SDIO_CMD_CRC_V1_8822B)                                \
	 << BIT_SHIFT_SDIO_CMD_CRC_V1_8822B)
#define BITS_SDIO_CMD_CRC_V1_8822B                                             \
	(BIT_MASK_SDIO_CMD_CRC_V1_8822B << BIT_SHIFT_SDIO_CMD_CRC_V1_8822B)
#define BIT_CLEAR_SDIO_CMD_CRC_V1_8822B(x) ((x) & (~BITS_SDIO_CMD_CRC_V1_8822B))
#define BIT_GET_SDIO_CMD_CRC_V1_8822B(x)                                       \
	(((x) >> BIT_SHIFT_SDIO_CMD_CRC_V1_8822B) &                            \
	 BIT_MASK_SDIO_CMD_CRC_V1_8822B)
#define BIT_SET_SDIO_CMD_CRC_V1_8822B(x, v)                                    \
	(BIT_CLEAR_SDIO_CMD_CRC_V1_8822B(x) | BIT_SDIO_CMD_CRC_V1_8822B(v))

/* 2 REG_SDIO_HSISR_8822B */
#define BIT_DRV_WLAN_INT_CLR_8822B BIT(1)
#define BIT_DRV_WLAN_INT_8822B BIT(0)

/* 2 REG_SDIO_ERR_RPT_8822B */
#define BIT_HR_FF_OVF_8822B BIT(6)
#define BIT_HR_FF_UDN_8822B BIT(5)
#define BIT_TXDMA_BUSY_ERR_8822B BIT(4)
#define BIT_TXDMA_VLD_ERR_8822B BIT(3)
#define BIT_QSEL_UNKNOWN_ERR_8822B BIT(2)
#define BIT_QSEL_MIS_ERR_8822B BIT(1)
#define BIT_SDIO_OVERRD_ERR_8822B BIT(0)

/* 2 REG_SDIO_CMD_ERRCNT_8822B */

#define BIT_SHIFT_CMD_CRC_ERR_CNT_8822B 0
#define BIT_MASK_CMD_CRC_ERR_CNT_8822B 0xff
#define BIT_CMD_CRC_ERR_CNT_8822B(x)                                           \
	(((x) & BIT_MASK_CMD_CRC_ERR_CNT_8822B)                                \
	 << BIT_SHIFT_CMD_CRC_ERR_CNT_8822B)
#define BITS_CMD_CRC_ERR_CNT_8822B                                             \
	(BIT_MASK_CMD_CRC_ERR_CNT_8822B << BIT_SHIFT_CMD_CRC_ERR_CNT_8822B)
#define BIT_CLEAR_CMD_CRC_ERR_CNT_8822B(x) ((x) & (~BITS_CMD_CRC_ERR_CNT_8822B))
#define BIT_GET_CMD_CRC_ERR_CNT_8822B(x)                                       \
	(((x) >> BIT_SHIFT_CMD_CRC_ERR_CNT_8822B) &                            \
	 BIT_MASK_CMD_CRC_ERR_CNT_8822B)
#define BIT_SET_CMD_CRC_ERR_CNT_8822B(x, v)                                    \
	(BIT_CLEAR_CMD_CRC_ERR_CNT_8822B(x) | BIT_CMD_CRC_ERR_CNT_8822B(v))

/* 2 REG_SDIO_DATA_ERRCNT_8822B */

#define BIT_SHIFT_DATA_CRC_ERR_CNT_8822B 0
#define BIT_MASK_DATA_CRC_ERR_CNT_8822B 0xff
#define BIT_DATA_CRC_ERR_CNT_8822B(x)                                          \
	(((x) & BIT_MASK_DATA_CRC_ERR_CNT_8822B)                               \
	 << BIT_SHIFT_DATA_CRC_ERR_CNT_8822B)
#define BITS_DATA_CRC_ERR_CNT_8822B                                            \
	(BIT_MASK_DATA_CRC_ERR_CNT_8822B << BIT_SHIFT_DATA_CRC_ERR_CNT_8822B)
#define BIT_CLEAR_DATA_CRC_ERR_CNT_8822B(x)                                    \
	((x) & (~BITS_DATA_CRC_ERR_CNT_8822B))
#define BIT_GET_DATA_CRC_ERR_CNT_8822B(x)                                      \
	(((x) >> BIT_SHIFT_DATA_CRC_ERR_CNT_8822B) &                           \
	 BIT_MASK_DATA_CRC_ERR_CNT_8822B)
#define BIT_SET_DATA_CRC_ERR_CNT_8822B(x, v)                                   \
	(BIT_CLEAR_DATA_CRC_ERR_CNT_8822B(x) | BIT_DATA_CRC_ERR_CNT_8822B(v))

/* 2 REG_SDIO_CMD_ERR_CONTENT_8822B */

#define BIT_SHIFT_SDIO_CMD_ERR_CONTENT_8822B 0
#define BIT_MASK_SDIO_CMD_ERR_CONTENT_8822B 0xffffffffffL
#define BIT_SDIO_CMD_ERR_CONTENT_8822B(x)                                      \
	(((x) & BIT_MASK_SDIO_CMD_ERR_CONTENT_8822B)                           \
	 << BIT_SHIFT_SDIO_CMD_ERR_CONTENT_8822B)
#define BITS_SDIO_CMD_ERR_CONTENT_8822B                                        \
	(BIT_MASK_SDIO_CMD_ERR_CONTENT_8822B                                   \
	 << BIT_SHIFT_SDIO_CMD_ERR_CONTENT_8822B)
#define BIT_CLEAR_SDIO_CMD_ERR_CONTENT_8822B(x)                                \
	((x) & (~BITS_SDIO_CMD_ERR_CONTENT_8822B))
#define BIT_GET_SDIO_CMD_ERR_CONTENT_8822B(x)                                  \
	(((x) >> BIT_SHIFT_SDIO_CMD_ERR_CONTENT_8822B) &                       \
	 BIT_MASK_SDIO_CMD_ERR_CONTENT_8822B)
#define BIT_SET_SDIO_CMD_ERR_CONTENT_8822B(x, v)                               \
	(BIT_CLEAR_SDIO_CMD_ERR_CONTENT_8822B(x) |                             \
	 BIT_SDIO_CMD_ERR_CONTENT_8822B(v))

/* 2 REG_SDIO_CRC_ERR_IDX_8822B */
#define BIT_D3_CRC_ERR_8822B BIT(4)
#define BIT_D2_CRC_ERR_8822B BIT(3)
#define BIT_D1_CRC_ERR_8822B BIT(2)
#define BIT_D0_CRC_ERR_8822B BIT(1)
#define BIT_CMD_CRC_ERR_8822B BIT(0)

/* 2 REG_SDIO_DATA_CRC_8822B */

#define BIT_SHIFT_SDIO_DATA_CRC_8822B 0
#define BIT_MASK_SDIO_DATA_CRC_8822B 0xffff
#define BIT_SDIO_DATA_CRC_8822B(x)                                             \
	(((x) & BIT_MASK_SDIO_DATA_CRC_8822B) << BIT_SHIFT_SDIO_DATA_CRC_8822B)
#define BITS_SDIO_DATA_CRC_8822B                                               \
	(BIT_MASK_SDIO_DATA_CRC_8822B << BIT_SHIFT_SDIO_DATA_CRC_8822B)
#define BIT_CLEAR_SDIO_DATA_CRC_8822B(x) ((x) & (~BITS_SDIO_DATA_CRC_8822B))
#define BIT_GET_SDIO_DATA_CRC_8822B(x)                                         \
	(((x) >> BIT_SHIFT_SDIO_DATA_CRC_8822B) & BIT_MASK_SDIO_DATA_CRC_8822B)
#define BIT_SET_SDIO_DATA_CRC_8822B(x, v)                                      \
	(BIT_CLEAR_SDIO_DATA_CRC_8822B(x) | BIT_SDIO_DATA_CRC_8822B(v))

/* 2 REG_SDIO_DATA_REPLY_TIME_8822B */

#define BIT_SHIFT_SDIO_DATA_REPLY_TIME_8822B 0
#define BIT_MASK_SDIO_DATA_REPLY_TIME_8822B 0x7
#define BIT_SDIO_DATA_REPLY_TIME_8822B(x)                                      \
	(((x) & BIT_MASK_SDIO_DATA_REPLY_TIME_8822B)                           \
	 << BIT_SHIFT_SDIO_DATA_REPLY_TIME_8822B)
#define BITS_SDIO_DATA_REPLY_TIME_8822B                                        \
	(BIT_MASK_SDIO_DATA_REPLY_TIME_8822B                                   \
	 << BIT_SHIFT_SDIO_DATA_REPLY_TIME_8822B)
#define BIT_CLEAR_SDIO_DATA_REPLY_TIME_8822B(x)                                \
	((x) & (~BITS_SDIO_DATA_REPLY_TIME_8822B))
#define BIT_GET_SDIO_DATA_REPLY_TIME_8822B(x)                                  \
	(((x) >> BIT_SHIFT_SDIO_DATA_REPLY_TIME_8822B) &                       \
	 BIT_MASK_SDIO_DATA_REPLY_TIME_8822B)
#define BIT_SET_SDIO_DATA_REPLY_TIME_8822B(x, v)                               \
	(BIT_CLEAR_SDIO_DATA_REPLY_TIME_8822B(x) |                             \
	 BIT_SDIO_DATA_REPLY_TIME_8822B(v))

#endif
