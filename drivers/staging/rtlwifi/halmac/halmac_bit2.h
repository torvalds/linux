/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef __RTL_WLAN_BITDEF_H__
#define __RTL_WLAN_BITDEF_H__

/*-------------------------Modification Log-----------------------------------
 *	Base on MAC_Register.doc SVN391
 *-------------------------Modification Log-----------------------------------
 */

/*--------------------------Include File--------------------------------------*/
/*--------------------------Include File--------------------------------------*/

/* 3 ============Programming guide Start===================== */
/*
 *	1. For all bit define, it should be prefixed by "BIT_"
 *	2. For all bit mask, it should be prefixed by "BIT_MASK_"
 *	3. For all bit shift, it should be prefixed by "BIT_SHIFT_"
 *	4. For other case, prefix is not needed
 *
 * Example:
 * #define BIT_SHIFT_MAX_TXDMA		16
 * #define BIT_MASK_MAX_TXDMA		0x7
 * #define BIT_MAX_TXDMA(x)		\
 *			(((x) & BIT_MASK_MAX_TXDMA) << BIT_SHIFT_MAX_TXDMA)
 * #define BIT_GET_MAX_TXDMA(x)		\
 *			(((x) >> BIT_SHIFT_MAX_TXDMA) & BIT_MASK_MAX_TXDMA)
 *
 */
/* 3 ============Programming guide End===================== */

#define CPU_OPT_WIDTH 0x1F

#define BIT_SHIFT_WATCH_DOG_RECORD_V1 10
#define BIT_MASK_WATCH_DOG_RECORD_V1 0x3fff
#define BIT_WATCH_DOG_RECORD_V1(x)                                             \
	(((x) & BIT_MASK_WATCH_DOG_RECORD_V1) << BIT_SHIFT_WATCH_DOG_RECORD_V1)
#define BIT_GET_WATCH_DOG_RECORD_V1(x)                                         \
	(((x) >> BIT_SHIFT_WATCH_DOG_RECORD_V1) & BIT_MASK_WATCH_DOG_RECORD_V1)

#define BIT_R_IO_TIMEOUT_FLAG_V1 BIT(9)

#define BIT_ISO_MD2PP BIT(0)

#define BIT_SHIFT_R_WMAC_IPV6_MYIPAD 0
#define BIT_MASK_R_WMAC_IPV6_MYIPAD 0xffffffffffffffffffffffffffffffffL
#define BIT_R_WMAC_IPV6_MYIPAD(x)                                              \
	(((x) & BIT_MASK_R_WMAC_IPV6_MYIPAD) << BIT_SHIFT_R_WMAC_IPV6_MYIPAD)
#define BIT_GET_R_WMAC_IPV6_MYIPAD(x)                                          \
	(((x) >> BIT_SHIFT_R_WMAC_IPV6_MYIPAD) & BIT_MASK_R_WMAC_IPV6_MYIPAD)

/* 2 REG_SDIO_TX_CTRL			(Offset 0x10250000) */

#define BIT_SHIFT_SDIO_INT_TIMEOUT 16
#define BIT_MASK_SDIO_INT_TIMEOUT 0xffff
#define BIT_SDIO_INT_TIMEOUT(x)                                                \
	(((x) & BIT_MASK_SDIO_INT_TIMEOUT) << BIT_SHIFT_SDIO_INT_TIMEOUT)
#define BIT_GET_SDIO_INT_TIMEOUT(x)                                            \
	(((x) >> BIT_SHIFT_SDIO_INT_TIMEOUT) & BIT_MASK_SDIO_INT_TIMEOUT)

/* 2 REG_SYS_ISO_CTRL			(Offset 0x0000) */

#define BIT_PWC_EV12V BIT(15)

/* 2 REG_SDIO_TX_CTRL			(Offset 0x10250000) */

#define BIT_IO_ERR_STATUS BIT(15)

/* 2 REG_SYS_ISO_CTRL			(Offset 0x0000) */

#define BIT_PWC_EV25V BIT(14)

/* 2 REG_SYS_ISO_CTRL			(Offset 0x0000) */

#define BIT_PA33V_EN BIT(13)
#define BIT_PA12V_EN BIT(12)

/* 2 REG_SYS_ISO_CTRL			(Offset 0x0000) */

#define BIT_UA33V_EN BIT(11)
#define BIT_UA12V_EN BIT(10)

/* 2 REG_SYS_ISO_CTRL			(Offset 0x0000) */

#define BIT_ISO_RFDIO BIT(9)

/* 2 REG_SDIO_TX_CTRL			(Offset 0x10250000) */

#define BIT_REPLY_ERRCRC_IN_DATA BIT(9)

/* 2 REG_SYS_ISO_CTRL			(Offset 0x0000) */

#define BIT_ISO_EB2CORE BIT(8)

/* 2 REG_SDIO_TX_CTRL			(Offset 0x10250000) */

#define BIT_EN_CMD53_OVERLAP BIT(8)

/* 2 REG_SYS_ISO_CTRL			(Offset 0x0000) */

#define BIT_ISO_DIOE BIT(7)

/* 2 REG_SDIO_TX_CTRL			(Offset 0x10250000) */

#define BIT_REPLY_ERR_IN_R5 BIT(7)

/* 2 REG_SYS_ISO_CTRL			(Offset 0x0000) */

#define BIT_ISO_WLPON2PP BIT(6)

/* 2 REG_SDIO_TX_CTRL			(Offset 0x10250000) */

#define BIT_R18A_EN BIT(6)

/* 2 REG_SYS_ISO_CTRL			(Offset 0x0000) */

#define BIT_ISO_IP2MAC_WA2PP BIT(5)

/* 2 REG_SDIO_TX_CTRL			(Offset 0x10250000) */

#define BIT_INIT_CMD_EN BIT(5)

/* 2 REG_SYS_ISO_CTRL			(Offset 0x0000) */

#define BIT_ISO_PD2CORE BIT(4)

/* 2 REG_SYS_ISO_CTRL			(Offset 0x0000) */

#define BIT_ISO_PA2PCIE BIT(3)

/* 2 REG_SYS_ISO_CTRL			(Offset 0x0000) */

#define BIT_ISO_UD2CORE BIT(2)

/* 2 REG_SDIO_TX_CTRL			(Offset 0x10250000) */

#define BIT_EN_RXDMA_MASK_INT BIT(2)

/* 2 REG_SYS_ISO_CTRL			(Offset 0x0000) */

#define BIT_ISO_UA2USB BIT(1)

/* 2 REG_SDIO_TX_CTRL			(Offset 0x10250000) */

#define BIT_EN_MASK_TIMER BIT(1)

/* 2 REG_SYS_ISO_CTRL			(Offset 0x0000) */

#define BIT_ISO_WD2PP BIT(0)

/* 2 REG_SDIO_TX_CTRL			(Offset 0x10250000) */

#define BIT_CMD_ERR_STOP_INT_EN BIT(0)

/* 2 REG_SYS_FUNC_EN				(Offset 0x0002) */

#define BIT_FEN_MREGEN BIT(15)
#define BIT_FEN_HWPDN BIT(14)

/* 2 REG_SYS_FUNC_EN				(Offset 0x0002) */

#define BIT_EN_25_1 BIT(13)

/* 2 REG_SYS_FUNC_EN				(Offset 0x0002) */

#define BIT_FEN_ELDR BIT(12)
#define BIT_FEN_DCORE BIT(11)
#define BIT_FEN_CPUEN BIT(10)
#define BIT_FEN_DIOE BIT(9)
#define BIT_FEN_PCIED BIT(8)
#define BIT_FEN_PPLL BIT(7)
#define BIT_FEN_PCIEA BIT(6)
#define BIT_FEN_DIO_PCIE BIT(5)
#define BIT_FEN_USBD BIT(4)
#define BIT_FEN_UPLL BIT(3)
#define BIT_FEN_USBA BIT(2)

/* 2 REG_SYS_FUNC_EN				(Offset 0x0002) */

#define BIT_FEN_BB_GLB_RSTN BIT(1)
#define BIT_FEN_BBRSTB BIT(0)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_SOP_EABM BIT(31)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_SOP_ACKF BIT(30)
#define BIT_SOP_ERCK BIT(29)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_SOP_ESWR BIT(28)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_SOP_PWMM BIT(27)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_SOP_EECK BIT(26)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_SOP_EXTL BIT(24)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_SYM_OP_RING_12M BIT(22)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_ROP_SWPR BIT(21)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_DIS_HW_LPLDM BIT(20)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_OPT_SWRST_WLMCU BIT(19)
#define BIT_RDY_SYSPWR BIT(17)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_EN_WLON BIT(16)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_APDM_HPDN BIT(15)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_AFSM_PCIE_SUS_EN BIT(12)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_AFSM_WLSUS_EN BIT(11)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_APFM_SWLPS BIT(10)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_APFM_OFFMAC BIT(9)
#define BIT_APFN_ONMAC BIT(8)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_CHIP_PDN_EN BIT(7)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_RDY_MACDIS BIT(6)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_RING_CLK_12M_EN BIT(4)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_PFM_WOWL BIT(3)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_PFM_LDKP BIT(2)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_WL_HCI_ALD BIT(1)

/* 2 REG_SYS_PW_CTRL				(Offset 0x0004) */

#define BIT_PFM_LDALL BIT(0)

/* 2 REG_SYS_CLK_CTRL			(Offset 0x0008) */

#define BIT_LDO_DUMMY BIT(15)

/* 2 REG_SYS_CLK_CTRL			(Offset 0x0008) */

#define BIT_CPU_CLK_EN BIT(14)

/* 2 REG_SYS_CLK_CTRL			(Offset 0x0008) */

#define BIT_SYMREG_CLK_EN BIT(13)

/* 2 REG_SYS_CLK_CTRL			(Offset 0x0008) */

#define BIT_HCI_CLK_EN BIT(12)

/* 2 REG_SYS_CLK_CTRL			(Offset 0x0008) */

#define BIT_MAC_CLK_EN BIT(11)
#define BIT_SEC_CLK_EN BIT(10)

/* 2 REG_SYS_CLK_CTRL			(Offset 0x0008) */

#define BIT_PHY_SSC_RSTB BIT(9)
#define BIT_EXT_32K_EN BIT(8)

/* 2 REG_SYS_CLK_CTRL			(Offset 0x0008) */

#define BIT_WL_CLK_TEST BIT(7)
#define BIT_OP_SPS_PWM_EN BIT(6)

/* 2 REG_SYS_CLK_CTRL			(Offset 0x0008) */

#define BIT_LOADER_CLK_EN BIT(5)
#define BIT_MACSLP BIT(4)
#define BIT_WAKEPAD_EN BIT(3)
#define BIT_ROMD16V_EN BIT(2)

/* 2 REG_SYS_CLK_CTRL			(Offset 0x0008) */

#define BIT_CKANA12M_EN BIT(1)

/* 2 REG_SYS_CLK_CTRL			(Offset 0x0008) */

#define BIT_CNTD16V_EN BIT(0)

/* 2 REG_SYS_EEPROM_CTRL			(Offset 0x000A) */

#define BIT_SHIFT_VPDIDX 8
#define BIT_MASK_VPDIDX 0xff
#define BIT_VPDIDX(x) (((x) & BIT_MASK_VPDIDX) << BIT_SHIFT_VPDIDX)
#define BIT_GET_VPDIDX(x) (((x) >> BIT_SHIFT_VPDIDX) & BIT_MASK_VPDIDX)

#define BIT_SHIFT_EEM1_0 6
#define BIT_MASK_EEM1_0 0x3
#define BIT_EEM1_0(x) (((x) & BIT_MASK_EEM1_0) << BIT_SHIFT_EEM1_0)
#define BIT_GET_EEM1_0(x) (((x) >> BIT_SHIFT_EEM1_0) & BIT_MASK_EEM1_0)

#define BIT_AUTOLOAD_SUS BIT(5)

/* 2 REG_SYS_EEPROM_CTRL			(Offset 0x000A) */

#define BIT_EERPOMSEL BIT(4)

/* 2 REG_SYS_EEPROM_CTRL			(Offset 0x000A) */

#define BIT_EECS_V1 BIT(3)
#define BIT_EESK_V1 BIT(2)
#define BIT_EEDI_V1 BIT(1)
#define BIT_EEDO_V1 BIT(0)

/* 2 REG_EE_VPD				(Offset 0x000C) */

#define BIT_SHIFT_VPD_DATA 0
#define BIT_MASK_VPD_DATA 0xffffffffL
#define BIT_VPD_DATA(x) (((x) & BIT_MASK_VPD_DATA) << BIT_SHIFT_VPD_DATA)
#define BIT_GET_VPD_DATA(x) (((x) >> BIT_SHIFT_VPD_DATA) & BIT_MASK_VPD_DATA)

/* 2 REG_SYS_SWR_CTRL1			(Offset 0x0010) */

#define BIT_C2_L_BIT0 BIT(31)

/* 2 REG_SYS_SWR_CTRL1			(Offset 0x0010) */

#define BIT_SHIFT_C1_L 29
#define BIT_MASK_C1_L 0x3
#define BIT_C1_L(x) (((x) & BIT_MASK_C1_L) << BIT_SHIFT_C1_L)
#define BIT_GET_C1_L(x) (((x) >> BIT_SHIFT_C1_L) & BIT_MASK_C1_L)

/* 2 REG_SYS_SWR_CTRL1			(Offset 0x0010) */

#define BIT_SHIFT_REG_FREQ_L 25
#define BIT_MASK_REG_FREQ_L 0x7
#define BIT_REG_FREQ_L(x) (((x) & BIT_MASK_REG_FREQ_L) << BIT_SHIFT_REG_FREQ_L)
#define BIT_GET_REG_FREQ_L(x)                                                  \
	(((x) >> BIT_SHIFT_REG_FREQ_L) & BIT_MASK_REG_FREQ_L)

#define BIT_REG_EN_DUTY BIT(24)

/* 2 REG_SYS_SWR_CTRL1			(Offset 0x0010) */

#define BIT_SHIFT_REG_MODE 22
#define BIT_MASK_REG_MODE 0x3
#define BIT_REG_MODE(x) (((x) & BIT_MASK_REG_MODE) << BIT_SHIFT_REG_MODE)
#define BIT_GET_REG_MODE(x) (((x) >> BIT_SHIFT_REG_MODE) & BIT_MASK_REG_MODE)

/* 2 REG_SYS_SWR_CTRL1			(Offset 0x0010) */

#define BIT_REG_EN_SP BIT(21)
#define BIT_REG_AUTO_L BIT(20)

/* 2 REG_SYS_SWR_CTRL1			(Offset 0x0010) */

#define BIT_SW18_SELD_BIT0 BIT(19)

/* 2 REG_SYS_SWR_CTRL1			(Offset 0x0010) */

#define BIT_SW18_POWOCP BIT(18)

/* 2 REG_SYS_SWR_CTRL1			(Offset 0x0010) */

#define BIT_SHIFT_OCP_L1 15
#define BIT_MASK_OCP_L1 0x7
#define BIT_OCP_L1(x) (((x) & BIT_MASK_OCP_L1) << BIT_SHIFT_OCP_L1)
#define BIT_GET_OCP_L1(x) (((x) >> BIT_SHIFT_OCP_L1) & BIT_MASK_OCP_L1)

/* 2 REG_SYS_SWR_CTRL1			(Offset 0x0010) */

#define BIT_SHIFT_CF_L 13
#define BIT_MASK_CF_L 0x3
#define BIT_CF_L(x) (((x) & BIT_MASK_CF_L) << BIT_SHIFT_CF_L)
#define BIT_GET_CF_L(x) (((x) >> BIT_SHIFT_CF_L) & BIT_MASK_CF_L)

/* 2 REG_SYS_SWR_CTRL1			(Offset 0x0010) */

#define BIT_SW18_FPWM BIT(11)

/* 2 REG_SYS_SWR_CTRL1			(Offset 0x0010) */

#define BIT_SW18_SWEN BIT(9)
#define BIT_SW18_LDEN BIT(8)
#define BIT_MAC_ID_EN BIT(7)

/* 2 REG_SYS_SWR_CTRL1			(Offset 0x0010) */

#define BIT_AFE_BGEN BIT(0)

/* 2 REG_SYS_SWR_CTRL2			(Offset 0x0014) */

#define BIT_POW_ZCD_L BIT(31)

/* 2 REG_SDIO_HIMR				(Offset 0x10250014) */

#define BIT_SDIO_CRCERR_MSK BIT(31)

/* 2 REG_SYS_SWR_CTRL2			(Offset 0x0014) */

#define BIT_AUTOZCD_L BIT(30)
#define BIT_SDIO_HSISR3_IND_MSK BIT(30)
#define BIT_SDIO_HSISR2_IND_MSK BIT(29)

/* 2 REG_SYS_SWR_CTRL2			(Offset 0x0014) */

#define BIT_SHIFT_REG_DELAY 28
#define BIT_MASK_REG_DELAY 0x3
#define BIT_REG_DELAY(x) (((x) & BIT_MASK_REG_DELAY) << BIT_SHIFT_REG_DELAY)
#define BIT_GET_REG_DELAY(x) (((x) >> BIT_SHIFT_REG_DELAY) & BIT_MASK_REG_DELAY)

/* 2 REG_SDIO_HIMR				(Offset 0x10250014) */

#define BIT_SDIO_HEISR_IND_MSK BIT(28)

/* 2 REG_SDIO_HIMR				(Offset 0x10250014) */

#define BIT_SDIO_CTWEND_MSK BIT(27)
#define BIT_SDIO_ATIMEND_E_MSK BIT(26)

/* 2 REG_SDIO_HIMR				(Offset 0x10250014) */

#define BIT_SDIIO_ATIMEND_MSK BIT(25)

/* 2 REG_SDIO_HIMR				(Offset 0x10250014) */

#define BIT_SDIO_OCPINT_MSK BIT(24)

/* 2 REG_SYS_SWR_CTRL2			(Offset 0x0014) */

#define BIT_SHIFT_V15ADJ_L1_V1 24
#define BIT_MASK_V15ADJ_L1_V1 0x7
#define BIT_V15ADJ_L1_V1(x)                                                    \
	(((x) & BIT_MASK_V15ADJ_L1_V1) << BIT_SHIFT_V15ADJ_L1_V1)
#define BIT_GET_V15ADJ_L1_V1(x)                                                \
	(((x) >> BIT_SHIFT_V15ADJ_L1_V1) & BIT_MASK_V15ADJ_L1_V1)

/* 2 REG_SDIO_HIMR				(Offset 0x10250014) */

#define BIT_SDIO_PSTIMEOUT_MSK BIT(23)

/* 2 REG_SDIO_HIMR				(Offset 0x10250014) */

#define BIT_SDIO_GTINT4_MSK BIT(22)

/* 2 REG_SDIO_HIMR				(Offset 0x10250014) */

#define BIT_SDIO_GTINT3_MSK BIT(21)

/* 2 REG_SDIO_HIMR				(Offset 0x10250014) */

#define BIT_SDIO_HSISR_IND_MSK BIT(20)

/* 2 REG_SYS_SWR_CTRL2			(Offset 0x0014) */

#define BIT_SHIFT_VOL_L1_V1 20
#define BIT_MASK_VOL_L1_V1 0xf
#define BIT_VOL_L1_V1(x) (((x) & BIT_MASK_VOL_L1_V1) << BIT_SHIFT_VOL_L1_V1)
#define BIT_GET_VOL_L1_V1(x) (((x) >> BIT_SHIFT_VOL_L1_V1) & BIT_MASK_VOL_L1_V1)

/* 2 REG_SDIO_HIMR				(Offset 0x10250014) */

#define BIT_SDIO_CPWM2_MSK BIT(19)

/* 2 REG_SDIO_HIMR				(Offset 0x10250014) */

#define BIT_SDIO_CPWM1_MSK BIT(18)

/* 2 REG_SDIO_HIMR				(Offset 0x10250014) */

#define BIT_SDIO_C2HCMD_INT_MSK BIT(17)

/* 2 REG_SYS_SWR_CTRL2			(Offset 0x0014) */

#define BIT_SHIFT_IN_L1_V1 17
#define BIT_MASK_IN_L1_V1 0x7
#define BIT_IN_L1_V1(x) (((x) & BIT_MASK_IN_L1_V1) << BIT_SHIFT_IN_L1_V1)
#define BIT_GET_IN_L1_V1(x) (((x) >> BIT_SHIFT_IN_L1_V1) & BIT_MASK_IN_L1_V1)

/* 2 REG_SDIO_HIMR				(Offset 0x10250014) */

#define BIT_SDIO_BCNERLY_INT_MSK BIT(16)

/* 2 REG_SYS_SWR_CTRL2			(Offset 0x0014) */

#define BIT_SHIFT_TBOX_L1 15
#define BIT_MASK_TBOX_L1 0x3
#define BIT_TBOX_L1(x) (((x) & BIT_MASK_TBOX_L1) << BIT_SHIFT_TBOX_L1)
#define BIT_GET_TBOX_L1(x) (((x) >> BIT_SHIFT_TBOX_L1) & BIT_MASK_TBOX_L1)

/* 2 REG_SYS_SWR_CTRL2			(Offset 0x0014) */

#define BIT_SW18_SEL BIT(13)

/* 2 REG_SYS_SWR_CTRL2			(Offset 0x0014) */

#define BIT_SW18_SD BIT(10)

/* 2 REG_SDIO_HIMR				(Offset 0x10250014) */

#define BIT_SDIO_TXBCNERR_MSK BIT(7)

/* 2 REG_SYS_SWR_CTRL2			(Offset 0x0014) */

#define BIT_SHIFT_R3_L 7
#define BIT_MASK_R3_L 0x3
#define BIT_R3_L(x) (((x) & BIT_MASK_R3_L) << BIT_SHIFT_R3_L)
#define BIT_GET_R3_L(x) (((x) >> BIT_SHIFT_R3_L) & BIT_MASK_R3_L)

/* 2 REG_SDIO_HIMR				(Offset 0x10250014) */

#define BIT_SDIO_TXBCNOK_MSK BIT(6)

/* 2 REG_SYS_SWR_CTRL2			(Offset 0x0014) */

#define BIT_SHIFT_SW18_R2 5
#define BIT_MASK_SW18_R2 0x3
#define BIT_SW18_R2(x) (((x) & BIT_MASK_SW18_R2) << BIT_SHIFT_SW18_R2)
#define BIT_GET_SW18_R2(x) (((x) >> BIT_SHIFT_SW18_R2) & BIT_MASK_SW18_R2)

/* 2 REG_SDIO_HIMR				(Offset 0x10250014) */

#define BIT_SDIO_RXFOVW_MSK BIT(5)
#define BIT_SDIO_TXFOVW_MSK BIT(4)

/* 2 REG_SYS_SWR_CTRL2			(Offset 0x0014) */

#define BIT_SHIFT_SW18_R1 3
#define BIT_MASK_SW18_R1 0x3
#define BIT_SW18_R1(x) (((x) & BIT_MASK_SW18_R1) << BIT_SHIFT_SW18_R1)
#define BIT_GET_SW18_R1(x) (((x) >> BIT_SHIFT_SW18_R1) & BIT_MASK_SW18_R1)

/* 2 REG_SDIO_HIMR				(Offset 0x10250014) */

#define BIT_SDIO_RXERR_MSK BIT(3)
#define BIT_SDIO_TXERR_MSK BIT(2)

/* 2 REG_SDIO_HIMR				(Offset 0x10250014) */

#define BIT_SDIO_AVAL_MSK BIT(1)

/* 2 REG_SYS_SWR_CTRL2			(Offset 0x0014) */

#define BIT_SHIFT_C3_L_C3 1
#define BIT_MASK_C3_L_C3 0x3
#define BIT_C3_L_C3(x) (((x) & BIT_MASK_C3_L_C3) << BIT_SHIFT_C3_L_C3)
#define BIT_GET_C3_L_C3(x) (((x) >> BIT_SHIFT_C3_L_C3) & BIT_MASK_C3_L_C3)

/* 2 REG_SDIO_HIMR				(Offset 0x10250014) */

#define BIT_RX_REQUEST_MSK BIT(0)

/* 2 REG_SYS_SWR_CTRL2			(Offset 0x0014) */

#define BIT_C2_L_BIT1 BIT(0)

/* 2 REG_SYS_SWR_CTRL3			(Offset 0x0018) */

#define BIT_SPS18_OCP_DIS BIT(31)

/* 2 REG_SDIO_HISR				(Offset 0x10250018) */

#define BIT_SDIO_CRCERR BIT(31)

/* 2 REG_SDIO_HISR				(Offset 0x10250018) */

#define BIT_SDIO_HSISR3_IND BIT(30)
#define BIT_SDIO_HSISR2_IND BIT(29)
#define BIT_SDIO_HEISR_IND BIT(28)

/* 2 REG_SDIO_HISR				(Offset 0x10250018) */

#define BIT_SDIO_CTWEND BIT(27)
#define BIT_SDIO_ATIMEND_E BIT(26)
#define BIT_SDIO_ATIMEND BIT(25)
#define BIT_SDIO_OCPINT BIT(24)
#define BIT_SDIO_PSTIMEOUT BIT(23)
#define BIT_SDIO_GTINT4 BIT(22)
#define BIT_SDIO_GTINT3 BIT(21)
#define BIT_SDIO_HSISR_IND BIT(20)
#define BIT_SDIO_CPWM2 BIT(19)
#define BIT_SDIO_CPWM1 BIT(18)
#define BIT_SDIO_C2HCMD_INT BIT(17)

/* 2 REG_SYS_SWR_CTRL3			(Offset 0x0018) */

#define BIT_SHIFT_SPS18_OCP_TH 16
#define BIT_MASK_SPS18_OCP_TH 0x7fff
#define BIT_SPS18_OCP_TH(x)                                                    \
	(((x) & BIT_MASK_SPS18_OCP_TH) << BIT_SHIFT_SPS18_OCP_TH)
#define BIT_GET_SPS18_OCP_TH(x)                                                \
	(((x) >> BIT_SHIFT_SPS18_OCP_TH) & BIT_MASK_SPS18_OCP_TH)

/* 2 REG_SDIO_HISR				(Offset 0x10250018) */

#define BIT_SDIO_BCNERLY_INT BIT(16)
#define BIT_SDIO_TXBCNERR BIT(7)
#define BIT_SDIO_TXBCNOK BIT(6)
#define BIT_SDIO_RXFOVW BIT(5)
#define BIT_SDIO_TXFOVW BIT(4)
#define BIT_SDIO_RXERR BIT(3)
#define BIT_SDIO_TXERR BIT(2)
#define BIT_SDIO_AVAL BIT(1)

/* 2 REG_SYS_SWR_CTRL3			(Offset 0x0018) */

#define BIT_SHIFT_OCP_WINDOW 0
#define BIT_MASK_OCP_WINDOW 0xffff
#define BIT_OCP_WINDOW(x) (((x) & BIT_MASK_OCP_WINDOW) << BIT_SHIFT_OCP_WINDOW)
#define BIT_GET_OCP_WINDOW(x)                                                  \
	(((x) >> BIT_SHIFT_OCP_WINDOW) & BIT_MASK_OCP_WINDOW)

/* 2 REG_SDIO_HISR				(Offset 0x10250018) */

#define BIT_RX_REQUEST BIT(0)

/* 2 REG_RSV_CTRL				(Offset 0x001C) */

#define BIT_HREG_DBG BIT(23)

/* 2 REG_RSV_CTRL				(Offset 0x001C) */

#define BIT_WLMCUIOIF BIT(8)

/* 2 REG_RSV_CTRL				(Offset 0x001C) */

#define BIT_LOCK_ALL_EN BIT(7)

/* 2 REG_RSV_CTRL				(Offset 0x001C) */

#define BIT_R_DIS_PRST BIT(6)

/* 2 REG_RSV_CTRL				(Offset 0x001C) */

#define BIT_WLOCK_1C_B6 BIT(5)

/* 2 REG_RSV_CTRL				(Offset 0x001C) */

#define BIT_WLOCK_40 BIT(4)
#define BIT_WLOCK_08 BIT(3)
#define BIT_WLOCK_04 BIT(2)
#define BIT_WLOCK_00 BIT(1)
#define BIT_WLOCK_ALL BIT(0)

/* 2 REG_SDIO_RX_REQ_LEN			(Offset 0x1025001C) */

#define BIT_SHIFT_RX_REQ_LEN_V1 0
#define BIT_MASK_RX_REQ_LEN_V1 0x3ffff
#define BIT_RX_REQ_LEN_V1(x)                                                   \
	(((x) & BIT_MASK_RX_REQ_LEN_V1) << BIT_SHIFT_RX_REQ_LEN_V1)
#define BIT_GET_RX_REQ_LEN_V1(x)                                               \
	(((x) >> BIT_SHIFT_RX_REQ_LEN_V1) & BIT_MASK_RX_REQ_LEN_V1)

/* 2 REG_RF_CTRL				(Offset 0x001F) */

#define BIT_RF_SDMRSTB BIT(2)

/* 2 REG_RF_CTRL				(Offset 0x001F) */

#define BIT_RF_RSTB BIT(1)

/* 2 REG_RF_CTRL				(Offset 0x001F) */

#define BIT_RF_EN BIT(0)

/* 2 REG_SDIO_FREE_TXPG_SEQ_V1		(Offset 0x1025001F) */

#define BIT_SHIFT_FREE_TXPG_SEQ 0
#define BIT_MASK_FREE_TXPG_SEQ 0xff
#define BIT_FREE_TXPG_SEQ(x)                                                   \
	(((x) & BIT_MASK_FREE_TXPG_SEQ) << BIT_SHIFT_FREE_TXPG_SEQ)
#define BIT_GET_FREE_TXPG_SEQ(x)                                               \
	(((x) >> BIT_SHIFT_FREE_TXPG_SEQ) & BIT_MASK_FREE_TXPG_SEQ)

/* 2 REG_AFE_LDO_CTRL			(Offset 0x0020) */

#define BIT_SHIFT_LPLDH12_RSV 29
#define BIT_MASK_LPLDH12_RSV 0x7
#define BIT_LPLDH12_RSV(x)                                                     \
	(((x) & BIT_MASK_LPLDH12_RSV) << BIT_SHIFT_LPLDH12_RSV)
#define BIT_GET_LPLDH12_RSV(x)                                                 \
	(((x) >> BIT_SHIFT_LPLDH12_RSV) & BIT_MASK_LPLDH12_RSV)

/* 2 REG_AFE_LDO_CTRL			(Offset 0x0020) */

#define BIT_LPLDH12_SLP BIT(28)

#define BIT_SHIFT_LPLDH12_VADJ 24
#define BIT_MASK_LPLDH12_VADJ 0xf
#define BIT_LPLDH12_VADJ(x)                                                    \
	(((x) & BIT_MASK_LPLDH12_VADJ) << BIT_SHIFT_LPLDH12_VADJ)
#define BIT_GET_LPLDH12_VADJ(x)                                                \
	(((x) >> BIT_SHIFT_LPLDH12_VADJ) & BIT_MASK_LPLDH12_VADJ)

/* 2 REG_AFE_LDO_CTRL			(Offset 0x0020) */

#define BIT_LDH12_EN BIT(16)

/* 2 REG_SDIO_FREE_TXPG			(Offset 0x10250020) */

#define BIT_SHIFT_MID_FREEPG_V1 16
#define BIT_MASK_MID_FREEPG_V1 0xfff
#define BIT_MID_FREEPG_V1(x)                                                   \
	(((x) & BIT_MASK_MID_FREEPG_V1) << BIT_SHIFT_MID_FREEPG_V1)
#define BIT_GET_MID_FREEPG_V1(x)                                               \
	(((x) >> BIT_SHIFT_MID_FREEPG_V1) & BIT_MASK_MID_FREEPG_V1)

/* 2 REG_AFE_LDO_CTRL			(Offset 0x0020) */

#define BIT_WLBBOFF_BIG_PWC_EN BIT(14)
#define BIT_WLBBOFF_SMALL_PWC_EN BIT(13)
#define BIT_WLMACOFF_BIG_PWC_EN BIT(12)
#define BIT_WLPON_PWC_EN BIT(11)

/* 2 REG_AFE_LDO_CTRL			(Offset 0x0020) */

#define BIT_POW_REGU_P1 BIT(10)

/* 2 REG_AFE_LDO_CTRL			(Offset 0x0020) */

#define BIT_LDOV12W_EN BIT(8)

/* 2 REG_AFE_LDO_CTRL			(Offset 0x0020) */

#define BIT_EX_XTAL_DRV_DIGI BIT(7)
#define BIT_EX_XTAL_DRV_USB BIT(6)
#define BIT_EX_XTAL_DRV_AFE BIT(5)

/* 2 REG_AFE_LDO_CTRL			(Offset 0x0020) */

#define BIT_EX_XTAL_DRV_RF2 BIT(4)

/* 2 REG_AFE_LDO_CTRL			(Offset 0x0020) */

#define BIT_EX_XTAL_DRV_RF1 BIT(3)
#define BIT_POW_REGU_P0 BIT(2)

/* 2 REG_AFE_LDO_CTRL			(Offset 0x0020) */

#define BIT_POW_PLL_LDO BIT(0)

/* 2 REG_SDIO_FREE_TXPG			(Offset 0x10250020) */

#define BIT_SHIFT_HIQ_FREEPG_V1 0
#define BIT_MASK_HIQ_FREEPG_V1 0xfff
#define BIT_HIQ_FREEPG_V1(x)                                                   \
	(((x) & BIT_MASK_HIQ_FREEPG_V1) << BIT_SHIFT_HIQ_FREEPG_V1)
#define BIT_GET_HIQ_FREEPG_V1(x)                                               \
	(((x) >> BIT_SHIFT_HIQ_FREEPG_V1) & BIT_MASK_HIQ_FREEPG_V1)

/* 2 REG_AFE_CTRL1				(Offset 0x0024) */

#define BIT_AGPIO_GPE BIT(31)

/* 2 REG_AFE_CTRL1				(Offset 0x0024) */

#define BIT_SHIFT_XTAL_CAP_XI 25
#define BIT_MASK_XTAL_CAP_XI 0x3f
#define BIT_XTAL_CAP_XI(x)                                                     \
	(((x) & BIT_MASK_XTAL_CAP_XI) << BIT_SHIFT_XTAL_CAP_XI)
#define BIT_GET_XTAL_CAP_XI(x)                                                 \
	(((x) >> BIT_SHIFT_XTAL_CAP_XI) & BIT_MASK_XTAL_CAP_XI)

/* 2 REG_AFE_CTRL1				(Offset 0x0024) */

#define BIT_SHIFT_XTAL_DRV_DIGI 23
#define BIT_MASK_XTAL_DRV_DIGI 0x3
#define BIT_XTAL_DRV_DIGI(x)                                                   \
	(((x) & BIT_MASK_XTAL_DRV_DIGI) << BIT_SHIFT_XTAL_DRV_DIGI)
#define BIT_GET_XTAL_DRV_DIGI(x)                                               \
	(((x) >> BIT_SHIFT_XTAL_DRV_DIGI) & BIT_MASK_XTAL_DRV_DIGI)

#define BIT_XTAL_DRV_USB_BIT1 BIT(22)

/* 2 REG_AFE_CTRL1				(Offset 0x0024) */

#define BIT_SHIFT_MAC_CLK_SEL 20
#define BIT_MASK_MAC_CLK_SEL 0x3
#define BIT_MAC_CLK_SEL(x)                                                     \
	(((x) & BIT_MASK_MAC_CLK_SEL) << BIT_SHIFT_MAC_CLK_SEL)
#define BIT_GET_MAC_CLK_SEL(x)                                                 \
	(((x) >> BIT_SHIFT_MAC_CLK_SEL) & BIT_MASK_MAC_CLK_SEL)

/* 2 REG_AFE_CTRL1				(Offset 0x0024) */

#define BIT_XTAL_DRV_USB_BIT0 BIT(19)

/* 2 REG_AFE_CTRL1				(Offset 0x0024) */

#define BIT_SHIFT_XTAL_DRV_AFE 17
#define BIT_MASK_XTAL_DRV_AFE 0x3
#define BIT_XTAL_DRV_AFE(x)                                                    \
	(((x) & BIT_MASK_XTAL_DRV_AFE) << BIT_SHIFT_XTAL_DRV_AFE)
#define BIT_GET_XTAL_DRV_AFE(x)                                                \
	(((x) >> BIT_SHIFT_XTAL_DRV_AFE) & BIT_MASK_XTAL_DRV_AFE)

/* 2 REG_SDIO_FREE_TXPG2			(Offset 0x10250024) */

#define BIT_SHIFT_PUB_FREEPG_V1 16
#define BIT_MASK_PUB_FREEPG_V1 0xfff
#define BIT_PUB_FREEPG_V1(x)                                                   \
	(((x) & BIT_MASK_PUB_FREEPG_V1) << BIT_SHIFT_PUB_FREEPG_V1)
#define BIT_GET_PUB_FREEPG_V1(x)                                               \
	(((x) >> BIT_SHIFT_PUB_FREEPG_V1) & BIT_MASK_PUB_FREEPG_V1)

/* 2 REG_AFE_CTRL1				(Offset 0x0024) */

#define BIT_SHIFT_XTAL_DRV_RF2 15
#define BIT_MASK_XTAL_DRV_RF2 0x3
#define BIT_XTAL_DRV_RF2(x)                                                    \
	(((x) & BIT_MASK_XTAL_DRV_RF2) << BIT_SHIFT_XTAL_DRV_RF2)
#define BIT_GET_XTAL_DRV_RF2(x)                                                \
	(((x) >> BIT_SHIFT_XTAL_DRV_RF2) & BIT_MASK_XTAL_DRV_RF2)

/* 2 REG_AFE_CTRL1				(Offset 0x0024) */

#define BIT_SHIFT_XTAL_DRV_RF1 13
#define BIT_MASK_XTAL_DRV_RF1 0x3
#define BIT_XTAL_DRV_RF1(x)                                                    \
	(((x) & BIT_MASK_XTAL_DRV_RF1) << BIT_SHIFT_XTAL_DRV_RF1)
#define BIT_GET_XTAL_DRV_RF1(x)                                                \
	(((x) >> BIT_SHIFT_XTAL_DRV_RF1) & BIT_MASK_XTAL_DRV_RF1)

/* 2 REG_AFE_CTRL1				(Offset 0x0024) */

#define BIT_XTAL_DELAY_DIGI BIT(12)

/* 2 REG_AFE_CTRL1				(Offset 0x0024) */

#define BIT_XTAL_DELAY_USB BIT(11)
#define BIT_XTAL_DELAY_AFE BIT(10)

/* 2 REG_AFE_CTRL1				(Offset 0x0024) */

#define BIT_SHIFT_XTAL_LDO_VREF 7
#define BIT_MASK_XTAL_LDO_VREF 0x7
#define BIT_XTAL_LDO_VREF(x)                                                   \
	(((x) & BIT_MASK_XTAL_LDO_VREF) << BIT_SHIFT_XTAL_LDO_VREF)
#define BIT_GET_XTAL_LDO_VREF(x)                                               \
	(((x) >> BIT_SHIFT_XTAL_LDO_VREF) & BIT_MASK_XTAL_LDO_VREF)

/* 2 REG_AFE_CTRL1				(Offset 0x0024) */

#define BIT_XTAL_XQSEL_RF BIT(6)
#define BIT_XTAL_XQSEL BIT(5)

/* 2 REG_AFE_CTRL1				(Offset 0x0024) */

#define BIT_SHIFT_XTAL_GMN_V2 3
#define BIT_MASK_XTAL_GMN_V2 0x3
#define BIT_XTAL_GMN_V2(x)                                                     \
	(((x) & BIT_MASK_XTAL_GMN_V2) << BIT_SHIFT_XTAL_GMN_V2)
#define BIT_GET_XTAL_GMN_V2(x)                                                 \
	(((x) >> BIT_SHIFT_XTAL_GMN_V2) & BIT_MASK_XTAL_GMN_V2)

/* 2 REG_AFE_CTRL1				(Offset 0x0024) */

#define BIT_SHIFT_XTAL_GMP_V2 1
#define BIT_MASK_XTAL_GMP_V2 0x3
#define BIT_XTAL_GMP_V2(x)                                                     \
	(((x) & BIT_MASK_XTAL_GMP_V2) << BIT_SHIFT_XTAL_GMP_V2)
#define BIT_GET_XTAL_GMP_V2(x)                                                 \
	(((x) >> BIT_SHIFT_XTAL_GMP_V2) & BIT_MASK_XTAL_GMP_V2)

/* 2 REG_AFE_CTRL1				(Offset 0x0024) */

#define BIT_XTAL_EN BIT(0)

/* 2 REG_SDIO_FREE_TXPG2			(Offset 0x10250024) */

#define BIT_SHIFT_LOW_FREEPG_V1 0
#define BIT_MASK_LOW_FREEPG_V1 0xfff
#define BIT_LOW_FREEPG_V1(x)                                                   \
	(((x) & BIT_MASK_LOW_FREEPG_V1) << BIT_SHIFT_LOW_FREEPG_V1)
#define BIT_GET_LOW_FREEPG_V1(x)                                               \
	(((x) >> BIT_SHIFT_LOW_FREEPG_V1) & BIT_MASK_LOW_FREEPG_V1)

/* 2 REG_AFE_CTRL2				(Offset 0x0028) */

#define BIT_SHIFT_REG_C3_V4 30
#define BIT_MASK_REG_C3_V4 0x3
#define BIT_REG_C3_V4(x) (((x) & BIT_MASK_REG_C3_V4) << BIT_SHIFT_REG_C3_V4)
#define BIT_GET_REG_C3_V4(x) (((x) >> BIT_SHIFT_REG_C3_V4) & BIT_MASK_REG_C3_V4)

#define BIT_REG_CP_BIT1 BIT(29)

/* 2 REG_AFE_CTRL2				(Offset 0x0028) */

#define BIT_SHIFT_REG_RS_V4 26
#define BIT_MASK_REG_RS_V4 0x7
#define BIT_REG_RS_V4(x) (((x) & BIT_MASK_REG_RS_V4) << BIT_SHIFT_REG_RS_V4)
#define BIT_GET_REG_RS_V4(x) (((x) >> BIT_SHIFT_REG_RS_V4) & BIT_MASK_REG_RS_V4)

/* 2 REG_SDIO_OQT_FREE_TXPG_V1		(Offset 0x10250028) */

#define BIT_SHIFT_NOAC_OQT_FREEPG_V1 24
#define BIT_MASK_NOAC_OQT_FREEPG_V1 0xff
#define BIT_NOAC_OQT_FREEPG_V1(x)                                              \
	(((x) & BIT_MASK_NOAC_OQT_FREEPG_V1) << BIT_SHIFT_NOAC_OQT_FREEPG_V1)
#define BIT_GET_NOAC_OQT_FREEPG_V1(x)                                          \
	(((x) >> BIT_SHIFT_NOAC_OQT_FREEPG_V1) & BIT_MASK_NOAC_OQT_FREEPG_V1)

/* 2 REG_AFE_CTRL2				(Offset 0x0028) */

#define BIT_SHIFT_REG__CS 24
#define BIT_MASK_REG__CS 0x3
#define BIT_REG__CS(x) (((x) & BIT_MASK_REG__CS) << BIT_SHIFT_REG__CS)
#define BIT_GET_REG__CS(x) (((x) >> BIT_SHIFT_REG__CS) & BIT_MASK_REG__CS)

/* 2 REG_AFE_CTRL2				(Offset 0x0028) */

#define BIT_SHIFT_REG_CP_OFFSET 21
#define BIT_MASK_REG_CP_OFFSET 0x7
#define BIT_REG_CP_OFFSET(x)                                                   \
	(((x) & BIT_MASK_REG_CP_OFFSET) << BIT_SHIFT_REG_CP_OFFSET)
#define BIT_GET_REG_CP_OFFSET(x)                                               \
	(((x) >> BIT_SHIFT_REG_CP_OFFSET) & BIT_MASK_REG_CP_OFFSET)

/* 2 REG_AFE_CTRL2				(Offset 0x0028) */

#define BIT_SHIFT_CP_BIAS 18
#define BIT_MASK_CP_BIAS 0x7
#define BIT_CP_BIAS(x) (((x) & BIT_MASK_CP_BIAS) << BIT_SHIFT_CP_BIAS)
#define BIT_GET_CP_BIAS(x) (((x) >> BIT_SHIFT_CP_BIAS) & BIT_MASK_CP_BIAS)

/* 2 REG_AFE_CTRL2				(Offset 0x0028) */

#define BIT_REG_IDOUBLE_V2 BIT(17)

/* 2 REG_AFE_CTRL2				(Offset 0x0028) */

#define BIT_EN_SYN BIT(16)

#define BIT_SHIFT_AC_OQT_FREEPG_V1 16
#define BIT_MASK_AC_OQT_FREEPG_V1 0xff
#define BIT_AC_OQT_FREEPG_V1(x)                                                \
	(((x) & BIT_MASK_AC_OQT_FREEPG_V1) << BIT_SHIFT_AC_OQT_FREEPG_V1)
#define BIT_GET_AC_OQT_FREEPG_V1(x)                                            \
	(((x) >> BIT_SHIFT_AC_OQT_FREEPG_V1) & BIT_MASK_AC_OQT_FREEPG_V1)

/* 2 REG_AFE_CTRL2				(Offset 0x0028) */

#define BIT_SHIFT_MCCO 14
#define BIT_MASK_MCCO 0x3
#define BIT_MCCO(x) (((x) & BIT_MASK_MCCO) << BIT_SHIFT_MCCO)
#define BIT_GET_MCCO(x) (((x) >> BIT_SHIFT_MCCO) & BIT_MASK_MCCO)

/* 2 REG_AFE_CTRL2				(Offset 0x0028) */

#define BIT_SHIFT_REG_LDO_SEL 12
#define BIT_MASK_REG_LDO_SEL 0x3
#define BIT_REG_LDO_SEL(x)                                                     \
	(((x) & BIT_MASK_REG_LDO_SEL) << BIT_SHIFT_REG_LDO_SEL)
#define BIT_GET_REG_LDO_SEL(x)                                                 \
	(((x) >> BIT_SHIFT_REG_LDO_SEL) & BIT_MASK_REG_LDO_SEL)

#define BIT_REG_KVCO_V2 BIT(10)

/* 2 REG_AFE_CTRL2				(Offset 0x0028) */

#define BIT_AGPIO_GPO BIT(9)

/* 2 REG_AFE_CTRL2				(Offset 0x0028) */

#define BIT_SHIFT_AGPIO_DRV 7
#define BIT_MASK_AGPIO_DRV 0x3
#define BIT_AGPIO_DRV(x) (((x) & BIT_MASK_AGPIO_DRV) << BIT_SHIFT_AGPIO_DRV)
#define BIT_GET_AGPIO_DRV(x) (((x) >> BIT_SHIFT_AGPIO_DRV) & BIT_MASK_AGPIO_DRV)

/* 2 REG_AFE_CTRL2				(Offset 0x0028) */

#define BIT_SHIFT_XTAL_CAP_XO 1
#define BIT_MASK_XTAL_CAP_XO 0x3f
#define BIT_XTAL_CAP_XO(x)                                                     \
	(((x) & BIT_MASK_XTAL_CAP_XO) << BIT_SHIFT_XTAL_CAP_XO)
#define BIT_GET_XTAL_CAP_XO(x)                                                 \
	(((x) >> BIT_SHIFT_XTAL_CAP_XO) & BIT_MASK_XTAL_CAP_XO)

/* 2 REG_AFE_CTRL2				(Offset 0x0028) */

#define BIT_POW_PLL BIT(0)

/* 2 REG_SDIO_OQT_FREE_TXPG_V1		(Offset 0x10250028) */

#define BIT_SHIFT_EXQ_FREEPG_V1 0
#define BIT_MASK_EXQ_FREEPG_V1 0xfff
#define BIT_EXQ_FREEPG_V1(x)                                                   \
	(((x) & BIT_MASK_EXQ_FREEPG_V1) << BIT_SHIFT_EXQ_FREEPG_V1)
#define BIT_GET_EXQ_FREEPG_V1(x)                                               \
	(((x) >> BIT_SHIFT_EXQ_FREEPG_V1) & BIT_MASK_EXQ_FREEPG_V1)

/* 2 REG_AFE_CTRL3				(Offset 0x002C) */

#define BIT_SHIFT_PS 7
#define BIT_MASK_PS 0x7
#define BIT_PS(x) (((x) & BIT_MASK_PS) << BIT_SHIFT_PS)
#define BIT_GET_PS(x) (((x) >> BIT_SHIFT_PS) & BIT_MASK_PS)

/* 2 REG_AFE_CTRL3				(Offset 0x002C) */

#define BIT_PSEN BIT(6)
#define BIT_DOGENB BIT(5)

/* 2 REG_AFE_CTRL3				(Offset 0x002C) */

#define BIT_REG_MBIAS BIT(4)

/* 2 REG_AFE_CTRL3				(Offset 0x002C) */

#define BIT_SHIFT_REG_R3_V4 1
#define BIT_MASK_REG_R3_V4 0x7
#define BIT_REG_R3_V4(x) (((x) & BIT_MASK_REG_R3_V4) << BIT_SHIFT_REG_R3_V4)
#define BIT_GET_REG_R3_V4(x) (((x) >> BIT_SHIFT_REG_R3_V4) & BIT_MASK_REG_R3_V4)

/* 2 REG_AFE_CTRL3				(Offset 0x002C) */

#define BIT_REG_CP_BIT0 BIT(0)

/* 2 REG_EFUSE_CTRL				(Offset 0x0030) */

#define BIT_EF_FLAG BIT(31)

#define BIT_SHIFT_EF_PGPD 28
#define BIT_MASK_EF_PGPD 0x7
#define BIT_EF_PGPD(x) (((x) & BIT_MASK_EF_PGPD) << BIT_SHIFT_EF_PGPD)
#define BIT_GET_EF_PGPD(x) (((x) >> BIT_SHIFT_EF_PGPD) & BIT_MASK_EF_PGPD)

#define BIT_SHIFT_EF_RDT 24
#define BIT_MASK_EF_RDT 0xf
#define BIT_EF_RDT(x) (((x) & BIT_MASK_EF_RDT) << BIT_SHIFT_EF_RDT)
#define BIT_GET_EF_RDT(x) (((x) >> BIT_SHIFT_EF_RDT) & BIT_MASK_EF_RDT)

#define BIT_SHIFT_EF_PGTS 20
#define BIT_MASK_EF_PGTS 0xf
#define BIT_EF_PGTS(x) (((x) & BIT_MASK_EF_PGTS) << BIT_SHIFT_EF_PGTS)
#define BIT_GET_EF_PGTS(x) (((x) >> BIT_SHIFT_EF_PGTS) & BIT_MASK_EF_PGTS)

/* 2 REG_EFUSE_CTRL				(Offset 0x0030) */

#define BIT_EF_PDWN BIT(19)

/* 2 REG_EFUSE_CTRL				(Offset 0x0030) */

#define BIT_EF_ALDEN BIT(18)

/* 2 REG_SDIO_HTSFR_INFO			(Offset 0x10250030) */

#define BIT_SHIFT_HTSFR1 16
#define BIT_MASK_HTSFR1 0xffff
#define BIT_HTSFR1(x) (((x) & BIT_MASK_HTSFR1) << BIT_SHIFT_HTSFR1)
#define BIT_GET_HTSFR1(x) (((x) >> BIT_SHIFT_HTSFR1) & BIT_MASK_HTSFR1)

/* 2 REG_EFUSE_CTRL				(Offset 0x0030) */

#define BIT_SHIFT_EF_ADDR 8
#define BIT_MASK_EF_ADDR 0x3ff
#define BIT_EF_ADDR(x) (((x) & BIT_MASK_EF_ADDR) << BIT_SHIFT_EF_ADDR)
#define BIT_GET_EF_ADDR(x) (((x) >> BIT_SHIFT_EF_ADDR) & BIT_MASK_EF_ADDR)

#define BIT_SHIFT_EF_DATA 0
#define BIT_MASK_EF_DATA 0xff
#define BIT_EF_DATA(x) (((x) & BIT_MASK_EF_DATA) << BIT_SHIFT_EF_DATA)
#define BIT_GET_EF_DATA(x) (((x) >> BIT_SHIFT_EF_DATA) & BIT_MASK_EF_DATA)

/* 2 REG_SDIO_HTSFR_INFO			(Offset 0x10250030) */

#define BIT_SHIFT_HTSFR0 0
#define BIT_MASK_HTSFR0 0xffff
#define BIT_HTSFR0(x) (((x) & BIT_MASK_HTSFR0) << BIT_SHIFT_HTSFR0)
#define BIT_GET_HTSFR0(x) (((x) >> BIT_SHIFT_HTSFR0) & BIT_MASK_HTSFR0)

/* 2 REG_LDO_EFUSE_CTRL			(Offset 0x0034) */

#define BIT_LDOE25_EN BIT(31)

/* 2 REG_LDO_EFUSE_CTRL			(Offset 0x0034) */

#define BIT_SHIFT_LDOE25_V12ADJ_L 27
#define BIT_MASK_LDOE25_V12ADJ_L 0xf
#define BIT_LDOE25_V12ADJ_L(x)                                                 \
	(((x) & BIT_MASK_LDOE25_V12ADJ_L) << BIT_SHIFT_LDOE25_V12ADJ_L)
#define BIT_GET_LDOE25_V12ADJ_L(x)                                             \
	(((x) >> BIT_SHIFT_LDOE25_V12ADJ_L) & BIT_MASK_LDOE25_V12ADJ_L)

/* 2 REG_LDO_EFUSE_CTRL			(Offset 0x0034) */

#define BIT_EF_CRES_SEL BIT(26)

/* 2 REG_LDO_EFUSE_CTRL			(Offset 0x0034) */

#define BIT_SHIFT_EF_SCAN_START_V1 16
#define BIT_MASK_EF_SCAN_START_V1 0x3ff
#define BIT_EF_SCAN_START_V1(x)                                                \
	(((x) & BIT_MASK_EF_SCAN_START_V1) << BIT_SHIFT_EF_SCAN_START_V1)
#define BIT_GET_EF_SCAN_START_V1(x)                                            \
	(((x) >> BIT_SHIFT_EF_SCAN_START_V1) & BIT_MASK_EF_SCAN_START_V1)

/* 2 REG_LDO_EFUSE_CTRL			(Offset 0x0034) */

#define BIT_SHIFT_EF_SCAN_END 12
#define BIT_MASK_EF_SCAN_END 0xf
#define BIT_EF_SCAN_END(x)                                                     \
	(((x) & BIT_MASK_EF_SCAN_END) << BIT_SHIFT_EF_SCAN_END)
#define BIT_GET_EF_SCAN_END(x)                                                 \
	(((x) >> BIT_SHIFT_EF_SCAN_END) & BIT_MASK_EF_SCAN_END)

/* 2 REG_LDO_EFUSE_CTRL			(Offset 0x0034) */

#define BIT_EF_PD_DIS BIT(11)

/* 2 REG_LDO_EFUSE_CTRL			(Offset 0x0034) */

#define BIT_SHIFT_EF_CELL_SEL 8
#define BIT_MASK_EF_CELL_SEL 0x3
#define BIT_EF_CELL_SEL(x)                                                     \
	(((x) & BIT_MASK_EF_CELL_SEL) << BIT_SHIFT_EF_CELL_SEL)
#define BIT_GET_EF_CELL_SEL(x)                                                 \
	(((x) >> BIT_SHIFT_EF_CELL_SEL) & BIT_MASK_EF_CELL_SEL)

/* 2 REG_LDO_EFUSE_CTRL			(Offset 0x0034) */

#define BIT_EF_TRPT BIT(7)

#define BIT_SHIFT_EF_TTHD 0
#define BIT_MASK_EF_TTHD 0x7f
#define BIT_EF_TTHD(x) (((x) & BIT_MASK_EF_TTHD) << BIT_SHIFT_EF_TTHD)
#define BIT_GET_EF_TTHD(x) (((x) >> BIT_SHIFT_EF_TTHD) & BIT_MASK_EF_TTHD)

/* 2 REG_PWR_OPTION_CTRL			(Offset 0x0038) */

#define BIT_SHIFT_DBG_SEL_V1 16
#define BIT_MASK_DBG_SEL_V1 0xff
#define BIT_DBG_SEL_V1(x) (((x) & BIT_MASK_DBG_SEL_V1) << BIT_SHIFT_DBG_SEL_V1)
#define BIT_GET_DBG_SEL_V1(x)                                                  \
	(((x) >> BIT_SHIFT_DBG_SEL_V1) & BIT_MASK_DBG_SEL_V1)

/* 2 REG_PWR_OPTION_CTRL			(Offset 0x0038) */

#define BIT_SHIFT_DBG_SEL_BYTE 14
#define BIT_MASK_DBG_SEL_BYTE 0x3
#define BIT_DBG_SEL_BYTE(x)                                                    \
	(((x) & BIT_MASK_DBG_SEL_BYTE) << BIT_SHIFT_DBG_SEL_BYTE)
#define BIT_GET_DBG_SEL_BYTE(x)                                                \
	(((x) >> BIT_SHIFT_DBG_SEL_BYTE) & BIT_MASK_DBG_SEL_BYTE)

/* 2 REG_PWR_OPTION_CTRL			(Offset 0x0038) */

#define BIT_SHIFT_STD_L1_V1 12
#define BIT_MASK_STD_L1_V1 0x3
#define BIT_STD_L1_V1(x) (((x) & BIT_MASK_STD_L1_V1) << BIT_SHIFT_STD_L1_V1)
#define BIT_GET_STD_L1_V1(x) (((x) >> BIT_SHIFT_STD_L1_V1) & BIT_MASK_STD_L1_V1)

/* 2 REG_PWR_OPTION_CTRL			(Offset 0x0038) */

#define BIT_SYSON_DBG_PAD_E2 BIT(11)

/* 2 REG_PWR_OPTION_CTRL			(Offset 0x0038) */

#define BIT_SYSON_LED_PAD_E2 BIT(10)

/* 2 REG_PWR_OPTION_CTRL			(Offset 0x0038) */

#define BIT_SYSON_GPEE_PAD_E2 BIT(9)

/* 2 REG_PWR_OPTION_CTRL			(Offset 0x0038) */

#define BIT_SYSON_PCI_PAD_E2 BIT(8)

#define BIT_SHIFT_MATCH_CNT 8
#define BIT_MASK_MATCH_CNT 0xff
#define BIT_MATCH_CNT(x) (((x) & BIT_MASK_MATCH_CNT) << BIT_SHIFT_MATCH_CNT)
#define BIT_GET_MATCH_CNT(x) (((x) >> BIT_SHIFT_MATCH_CNT) & BIT_MASK_MATCH_CNT)

/* 2 REG_PWR_OPTION_CTRL			(Offset 0x0038) */

#define BIT_AUTO_SW_LDO_VOL_EN BIT(7)

/* 2 REG_PWR_OPTION_CTRL			(Offset 0x0038) */

#define BIT_SHIFT_SYSON_SPS0WWV_WT 4
#define BIT_MASK_SYSON_SPS0WWV_WT 0x3
#define BIT_SYSON_SPS0WWV_WT(x)                                                \
	(((x) & BIT_MASK_SYSON_SPS0WWV_WT) << BIT_SHIFT_SYSON_SPS0WWV_WT)
#define BIT_GET_SYSON_SPS0WWV_WT(x)                                            \
	(((x) >> BIT_SHIFT_SYSON_SPS0WWV_WT) & BIT_MASK_SYSON_SPS0WWV_WT)

/* 2 REG_PWR_OPTION_CTRL			(Offset 0x0038) */

#define BIT_SHIFT_SYSON_SPS0LDO_WT 2
#define BIT_MASK_SYSON_SPS0LDO_WT 0x3
#define BIT_SYSON_SPS0LDO_WT(x)                                                \
	(((x) & BIT_MASK_SYSON_SPS0LDO_WT) << BIT_SHIFT_SYSON_SPS0LDO_WT)
#define BIT_GET_SYSON_SPS0LDO_WT(x)                                            \
	(((x) >> BIT_SHIFT_SYSON_SPS0LDO_WT) & BIT_MASK_SYSON_SPS0LDO_WT)

/* 2 REG_PWR_OPTION_CTRL			(Offset 0x0038) */

#define BIT_SHIFT_SYSON_RCLK_SCALE 0
#define BIT_MASK_SYSON_RCLK_SCALE 0x3
#define BIT_SYSON_RCLK_SCALE(x)                                                \
	(((x) & BIT_MASK_SYSON_RCLK_SCALE) << BIT_SHIFT_SYSON_RCLK_SCALE)
#define BIT_GET_SYSON_RCLK_SCALE(x)                                            \
	(((x) >> BIT_SHIFT_SYSON_RCLK_SCALE) & BIT_MASK_SYSON_RCLK_SCALE)

/* 2 REG_SDIO_HCPWM1_V2			(Offset 0x10250038) */

#define BIT_SYS_CLK BIT(0)

/* 2 REG_CAL_TIMER				(Offset 0x003C) */

#define BIT_SHIFT_CAL_SCAL 0
#define BIT_MASK_CAL_SCAL 0xff
#define BIT_CAL_SCAL(x) (((x) & BIT_MASK_CAL_SCAL) << BIT_SHIFT_CAL_SCAL)
#define BIT_GET_CAL_SCAL(x) (((x) >> BIT_SHIFT_CAL_SCAL) & BIT_MASK_CAL_SCAL)

/* 2 REG_ACLK_MON				(Offset 0x003E) */

#define BIT_SHIFT_RCLK_MON 5
#define BIT_MASK_RCLK_MON 0x7ff
#define BIT_RCLK_MON(x) (((x) & BIT_MASK_RCLK_MON) << BIT_SHIFT_RCLK_MON)
#define BIT_GET_RCLK_MON(x) (((x) >> BIT_SHIFT_RCLK_MON) & BIT_MASK_RCLK_MON)

#define BIT_CAL_EN BIT(4)

#define BIT_SHIFT_DPSTU 2
#define BIT_MASK_DPSTU 0x3
#define BIT_DPSTU(x) (((x) & BIT_MASK_DPSTU) << BIT_SHIFT_DPSTU)
#define BIT_GET_DPSTU(x) (((x) >> BIT_SHIFT_DPSTU) & BIT_MASK_DPSTU)

#define BIT_SUS_16X BIT(1)

/* 2 REG_SDIO_INDIRECT_REG_CFG		(Offset 0x10250040) */

#define BIT_INDIRECT_REG_RDY BIT(20)

/* 2 REG_GPIO_MUXCFG				(Offset 0x0040) */

#define BIT_FSPI_EN BIT(19)

/* 2 REG_SDIO_INDIRECT_REG_CFG		(Offset 0x10250040) */

#define BIT_INDIRECT_REG_R BIT(19)

/* 2 REG_GPIO_MUXCFG				(Offset 0x0040) */

#define BIT_WL_RTS_EXT_32K_SEL BIT(18)

/* 2 REG_SDIO_INDIRECT_REG_CFG		(Offset 0x10250040) */

#define BIT_INDIRECT_REG_W BIT(18)

/* 2 REG_GPIO_MUXCFG				(Offset 0x0040) */

#define BIT_WLGP_SPI_EN BIT(16)

/* 2 REG_SDIO_INDIRECT_REG_CFG		(Offset 0x10250040) */

#define BIT_SHIFT_INDIRECT_REG_SIZE 16
#define BIT_MASK_INDIRECT_REG_SIZE 0x3
#define BIT_INDIRECT_REG_SIZE(x)                                               \
	(((x) & BIT_MASK_INDIRECT_REG_SIZE) << BIT_SHIFT_INDIRECT_REG_SIZE)
#define BIT_GET_INDIRECT_REG_SIZE(x)                                           \
	(((x) >> BIT_SHIFT_INDIRECT_REG_SIZE) & BIT_MASK_INDIRECT_REG_SIZE)

/* 2 REG_GPIO_MUXCFG				(Offset 0x0040) */

#define BIT_SIC_LBK BIT(15)
#define BIT_ENHTP BIT(14)

/* 2 REG_GPIO_MUXCFG				(Offset 0x0040) */

#define BIT_ENSIC BIT(12)
#define BIT_SIC_SWRST BIT(11)

/* 2 REG_GPIO_MUXCFG				(Offset 0x0040) */

#define BIT_PO_WIFI_PTA_PINS BIT(10)

/* 2 REG_GPIO_MUXCFG				(Offset 0x0040) */

#define BIT_PO_BT_PTA_PINS BIT(9)

/* 2 REG_GPIO_MUXCFG				(Offset 0x0040) */

#define BIT_ENUART BIT(8)

#define BIT_SHIFT_BTMODE 6
#define BIT_MASK_BTMODE 0x3
#define BIT_BTMODE(x) (((x) & BIT_MASK_BTMODE) << BIT_SHIFT_BTMODE)
#define BIT_GET_BTMODE(x) (((x) >> BIT_SHIFT_BTMODE) & BIT_MASK_BTMODE)

#define BIT_ENBT BIT(5)
#define BIT_EROM_EN BIT(4)

/* 2 REG_GPIO_MUXCFG				(Offset 0x0040) */

#define BIT_WLRFE_6_7_EN BIT(3)

/* 2 REG_GPIO_MUXCFG				(Offset 0x0040) */

#define BIT_WLRFE_4_5_EN BIT(2)

/* 2 REG_GPIO_MUXCFG				(Offset 0x0040) */

#define BIT_SHIFT_GPIOSEL 0
#define BIT_MASK_GPIOSEL 0x3
#define BIT_GPIOSEL(x) (((x) & BIT_MASK_GPIOSEL) << BIT_SHIFT_GPIOSEL)
#define BIT_GET_GPIOSEL(x) (((x) >> BIT_SHIFT_GPIOSEL) & BIT_MASK_GPIOSEL)

/* 2 REG_SDIO_INDIRECT_REG_CFG		(Offset 0x10250040) */

#define BIT_SHIFT_INDIRECT_REG_ADDR 0
#define BIT_MASK_INDIRECT_REG_ADDR 0xffff
#define BIT_INDIRECT_REG_ADDR(x)                                               \
	(((x) & BIT_MASK_INDIRECT_REG_ADDR) << BIT_SHIFT_INDIRECT_REG_ADDR)
#define BIT_GET_INDIRECT_REG_ADDR(x)                                           \
	(((x) >> BIT_SHIFT_INDIRECT_REG_ADDR) & BIT_MASK_INDIRECT_REG_ADDR)

/* 2 REG_GPIO_PIN_CTRL			(Offset 0x0044) */

#define BIT_SHIFT_GPIO_MOD_7_TO_0 24
#define BIT_MASK_GPIO_MOD_7_TO_0 0xff
#define BIT_GPIO_MOD_7_TO_0(x)                                                 \
	(((x) & BIT_MASK_GPIO_MOD_7_TO_0) << BIT_SHIFT_GPIO_MOD_7_TO_0)
#define BIT_GET_GPIO_MOD_7_TO_0(x)                                             \
	(((x) >> BIT_SHIFT_GPIO_MOD_7_TO_0) & BIT_MASK_GPIO_MOD_7_TO_0)

#define BIT_SHIFT_GPIO_IO_SEL_7_TO_0 16
#define BIT_MASK_GPIO_IO_SEL_7_TO_0 0xff
#define BIT_GPIO_IO_SEL_7_TO_0(x)                                              \
	(((x) & BIT_MASK_GPIO_IO_SEL_7_TO_0) << BIT_SHIFT_GPIO_IO_SEL_7_TO_0)
#define BIT_GET_GPIO_IO_SEL_7_TO_0(x)                                          \
	(((x) >> BIT_SHIFT_GPIO_IO_SEL_7_TO_0) & BIT_MASK_GPIO_IO_SEL_7_TO_0)

#define BIT_SHIFT_GPIO_OUT_7_TO_0 8
#define BIT_MASK_GPIO_OUT_7_TO_0 0xff
#define BIT_GPIO_OUT_7_TO_0(x)                                                 \
	(((x) & BIT_MASK_GPIO_OUT_7_TO_0) << BIT_SHIFT_GPIO_OUT_7_TO_0)
#define BIT_GET_GPIO_OUT_7_TO_0(x)                                             \
	(((x) >> BIT_SHIFT_GPIO_OUT_7_TO_0) & BIT_MASK_GPIO_OUT_7_TO_0)

#define BIT_SHIFT_GPIO_IN_7_TO_0 0
#define BIT_MASK_GPIO_IN_7_TO_0 0xff
#define BIT_GPIO_IN_7_TO_0(x)                                                  \
	(((x) & BIT_MASK_GPIO_IN_7_TO_0) << BIT_SHIFT_GPIO_IN_7_TO_0)
#define BIT_GET_GPIO_IN_7_TO_0(x)                                              \
	(((x) >> BIT_SHIFT_GPIO_IN_7_TO_0) & BIT_MASK_GPIO_IN_7_TO_0)

/* 2 REG_SDIO_INDIRECT_REG_DATA		(Offset 0x10250044) */

#define BIT_SHIFT_INDIRECT_REG_DATA 0
#define BIT_MASK_INDIRECT_REG_DATA 0xffffffffL
#define BIT_INDIRECT_REG_DATA(x)                                               \
	(((x) & BIT_MASK_INDIRECT_REG_DATA) << BIT_SHIFT_INDIRECT_REG_DATA)
#define BIT_GET_INDIRECT_REG_DATA(x)                                           \
	(((x) >> BIT_SHIFT_INDIRECT_REG_DATA) & BIT_MASK_INDIRECT_REG_DATA)

/* 2 REG_GPIO_INTM				(Offset 0x0048) */

#define BIT_SHIFT_MUXDBG_SEL 30
#define BIT_MASK_MUXDBG_SEL 0x3
#define BIT_MUXDBG_SEL(x) (((x) & BIT_MASK_MUXDBG_SEL) << BIT_SHIFT_MUXDBG_SEL)
#define BIT_GET_MUXDBG_SEL(x)                                                  \
	(((x) >> BIT_SHIFT_MUXDBG_SEL) & BIT_MASK_MUXDBG_SEL)

/* 2 REG_GPIO_INTM				(Offset 0x0048) */

#define BIT_EXTWOL_SEL BIT(17)

/* 2 REG_GPIO_INTM				(Offset 0x0048) */

#define BIT_EXTWOL_EN BIT(16)

/* 2 REG_GPIO_INTM				(Offset 0x0048) */

#define BIT_GPIOF_INT_MD BIT(15)
#define BIT_GPIOE_INT_MD BIT(14)
#define BIT_GPIOD_INT_MD BIT(13)
#define BIT_GPIOC_INT_MD BIT(12)
#define BIT_GPIOB_INT_MD BIT(11)
#define BIT_GPIOA_INT_MD BIT(10)
#define BIT_GPIO9_INT_MD BIT(9)
#define BIT_GPIO8_INT_MD BIT(8)
#define BIT_GPIO7_INT_MD BIT(7)
#define BIT_GPIO6_INT_MD BIT(6)
#define BIT_GPIO5_INT_MD BIT(5)
#define BIT_GPIO4_INT_MD BIT(4)
#define BIT_GPIO3_INT_MD BIT(3)
#define BIT_GPIO2_INT_MD BIT(2)
#define BIT_GPIO1_INT_MD BIT(1)
#define BIT_GPIO0_INT_MD BIT(0)

/* 2 REG_LED_CFG				(Offset 0x004C) */

#define BIT_GPIO3_WL_CTRL_EN BIT(27)

/* 2 REG_LED_CFG				(Offset 0x004C) */

#define BIT_LNAON_SEL_EN BIT(26)

/* 2 REG_LED_CFG				(Offset 0x004C) */

#define BIT_PAPE_SEL_EN BIT(25)

/* 2 REG_LED_CFG				(Offset 0x004C) */

#define BIT_DPDT_WLBT_SEL BIT(24)

/* 2 REG_LED_CFG				(Offset 0x004C) */

#define BIT_DPDT_SEL_EN BIT(23)

/* 2 REG_LED_CFG				(Offset 0x004C) */

#define BIT_GPIO13_14_WL_CTRL_EN BIT(22)

/* 2 REG_LED_CFG				(Offset 0x004C) */

#define BIT_LED2DIS BIT(21)

/* 2 REG_LED_CFG				(Offset 0x004C) */

#define BIT_LED2PL BIT(20)
#define BIT_LED2SV BIT(19)

#define BIT_SHIFT_LED2CM 16
#define BIT_MASK_LED2CM 0x7
#define BIT_LED2CM(x) (((x) & BIT_MASK_LED2CM) << BIT_SHIFT_LED2CM)
#define BIT_GET_LED2CM(x) (((x) >> BIT_SHIFT_LED2CM) & BIT_MASK_LED2CM)

#define BIT_LED1DIS BIT(15)
#define BIT_LED1PL BIT(12)
#define BIT_LED1SV BIT(11)

#define BIT_SHIFT_LED1CM 8
#define BIT_MASK_LED1CM 0x7
#define BIT_LED1CM(x) (((x) & BIT_MASK_LED1CM) << BIT_SHIFT_LED1CM)
#define BIT_GET_LED1CM(x) (((x) >> BIT_SHIFT_LED1CM) & BIT_MASK_LED1CM)

#define BIT_LED0DIS BIT(7)

/* 2 REG_LED_CFG				(Offset 0x004C) */

#define BIT_SHIFT_AFE_LDO_SWR_CHECK 5
#define BIT_MASK_AFE_LDO_SWR_CHECK 0x3
#define BIT_AFE_LDO_SWR_CHECK(x)                                               \
	(((x) & BIT_MASK_AFE_LDO_SWR_CHECK) << BIT_SHIFT_AFE_LDO_SWR_CHECK)
#define BIT_GET_AFE_LDO_SWR_CHECK(x)                                           \
	(((x) >> BIT_SHIFT_AFE_LDO_SWR_CHECK) & BIT_MASK_AFE_LDO_SWR_CHECK)

/* 2 REG_LED_CFG				(Offset 0x004C) */

#define BIT_LED0PL BIT(4)
#define BIT_LED0SV BIT(3)

#define BIT_SHIFT_LED0CM 0
#define BIT_MASK_LED0CM 0x7
#define BIT_LED0CM(x) (((x) & BIT_MASK_LED0CM) << BIT_SHIFT_LED0CM)
#define BIT_GET_LED0CM(x) (((x) >> BIT_SHIFT_LED0CM) & BIT_MASK_LED0CM)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_PDNINT_EN BIT(31)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_NFC_INT_PAD_EN BIT(30)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_SPS_OCP_INT_EN BIT(29)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_PWMERR_INT_EN BIT(28)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_GPIOF_INT_EN BIT(27)
#define BIT_FS_GPIOE_INT_EN BIT(26)
#define BIT_FS_GPIOD_INT_EN BIT(25)
#define BIT_FS_GPIOC_INT_EN BIT(24)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_GPIOB_INT_EN BIT(23)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_GPIOA_INT_EN BIT(22)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_GPIO9_INT_EN BIT(21)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_GPIO8_INT_EN BIT(20)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_GPIO7_INT_EN BIT(19)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_GPIO6_INT_EN BIT(18)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_GPIO5_INT_EN BIT(17)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_GPIO4_INT_EN BIT(16)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_GPIO3_INT_EN BIT(15)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_GPIO2_INT_EN BIT(14)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_GPIO1_INT_EN BIT(13)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_GPIO0_INT_EN BIT(12)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_HCI_SUS_EN BIT(11)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_HCI_RES_EN BIT(10)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_HCI_RESET_EN BIT(9)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_BTON_STS_UPDATE_MSK_EN BIT(7)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_ACT2RECOVERY_INT_EN_V1 BIT(6)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_GEN1GEN2_SWITCH BIT(5)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_HCI_TXDMA_REQ_HIMR BIT(4)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_32K_LEAVE_SETTING_MAK BIT(3)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_32K_ENTER_SETTING_MAK BIT(2)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_USB_LPMRSM_MSK BIT(1)

/* 2 REG_FSIMR				(Offset 0x0050) */

#define BIT_FS_USB_LPMINT_MSK BIT(0)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_PDNINT BIT(31)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_SPS_OCP_INT BIT(29)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_PWMERR_INT BIT(28)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_GPIOF_INT BIT(27)
#define BIT_FS_GPIOE_INT BIT(26)
#define BIT_FS_GPIOD_INT BIT(25)
#define BIT_FS_GPIOC_INT BIT(24)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_GPIOB_INT BIT(23)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_GPIOA_INT BIT(22)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_GPIO9_INT BIT(21)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_GPIO8_INT BIT(20)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_GPIO7_INT BIT(19)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_GPIO6_INT BIT(18)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_GPIO5_INT BIT(17)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_GPIO4_INT BIT(16)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_GPIO3_INT BIT(15)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_GPIO2_INT BIT(14)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_GPIO1_INT BIT(13)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_GPIO0_INT BIT(12)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_HCI_SUS_INT BIT(11)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_HCI_RES_INT BIT(10)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_HCI_RESET_INT BIT(9)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_ACT2RECOVERY BIT(6)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_HCI_TXDMA_REQ_HISR BIT(4)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_32K_LEAVE_SETTING_INT BIT(3)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_32K_ENTER_SETTING_INT BIT(2)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_USB_LPMRSM_INT BIT(1)

/* 2 REG_FSISR				(Offset 0x0054) */

#define BIT_FS_USB_LPMINT_INT BIT(0)

/* 2 REG_HSIMR				(Offset 0x0058) */

#define BIT_GPIOF_INT_EN BIT(31)
#define BIT_GPIOE_INT_EN BIT(30)
#define BIT_GPIOD_INT_EN BIT(29)
#define BIT_GPIOC_INT_EN BIT(28)
#define BIT_GPIOB_INT_EN BIT(27)
#define BIT_GPIOA_INT_EN BIT(26)
#define BIT_GPIO9_INT_EN BIT(25)
#define BIT_GPIO8_INT_EN BIT(24)
#define BIT_GPIO7_INT_EN BIT(23)
#define BIT_GPIO6_INT_EN BIT(22)
#define BIT_GPIO5_INT_EN BIT(21)
#define BIT_GPIO4_INT_EN BIT(20)
#define BIT_GPIO3_INT_EN BIT(19)

/* 2 REG_HSIMR				(Offset 0x0058) */

#define BIT_GPIO1_INT_EN BIT(17)
#define BIT_GPIO0_INT_EN BIT(16)

/* 2 REG_HSIMR				(Offset 0x0058) */

#define BIT_GPIO2_INT_EN_V1 BIT(16)

/* 2 REG_HSIMR				(Offset 0x0058) */

#define BIT_PDNINT_EN BIT(7)

/* 2 REG_HSIMR				(Offset 0x0058) */

#define BIT_RON_INT_EN BIT(6)

/* 2 REG_HSIMR				(Offset 0x0058) */

#define BIT_SPS_OCP_INT_EN BIT(5)

/* 2 REG_HSIMR				(Offset 0x0058) */

#define BIT_GPIO15_0_INT_EN BIT(0)

/* 2 REG_HSISR				(Offset 0x005C) */

#define BIT_GPIOF_INT BIT(31)
#define BIT_GPIOE_INT BIT(30)
#define BIT_GPIOD_INT BIT(29)
#define BIT_GPIOC_INT BIT(28)
#define BIT_GPIOB_INT BIT(27)
#define BIT_GPIOA_INT BIT(26)
#define BIT_GPIO9_INT BIT(25)
#define BIT_GPIO8_INT BIT(24)
#define BIT_GPIO7_INT BIT(23)

/* 2 REG_HSISR				(Offset 0x005C) */

#define BIT_GPIO6_INT BIT(22)
#define BIT_GPIO5_INT BIT(21)
#define BIT_GPIO4_INT BIT(20)
#define BIT_GPIO3_INT BIT(19)

/* 2 REG_HSISR				(Offset 0x005C) */

#define BIT_GPIO1_INT BIT(17)
#define BIT_GPIO0_INT BIT(16)

/* 2 REG_HSISR				(Offset 0x005C) */

#define BIT_GPIO2_INT_V1 BIT(16)

/* 2 REG_HSISR				(Offset 0x005C) */

#define BIT_PDNINT BIT(7)

/* 2 REG_HSISR				(Offset 0x005C) */

#define BIT_RON_INT BIT(6)

/* 2 REG_HSISR				(Offset 0x005C) */

#define BIT_SPS_OCP_INT BIT(5)

/* 2 REG_HSISR				(Offset 0x005C) */

#define BIT_GPIO15_0_INT BIT(0)
#define BIT_MCUFWDL_EN BIT(0)

/* 2 REG_GPIO_EXT_CTRL			(Offset 0x0060) */

#define BIT_SHIFT_GPIO_MOD_15_TO_8 24
#define BIT_MASK_GPIO_MOD_15_TO_8 0xff
#define BIT_GPIO_MOD_15_TO_8(x)                                                \
	(((x) & BIT_MASK_GPIO_MOD_15_TO_8) << BIT_SHIFT_GPIO_MOD_15_TO_8)
#define BIT_GET_GPIO_MOD_15_TO_8(x)                                            \
	(((x) >> BIT_SHIFT_GPIO_MOD_15_TO_8) & BIT_MASK_GPIO_MOD_15_TO_8)

#define BIT_SHIFT_GPIO_IO_SEL_15_TO_8 16
#define BIT_MASK_GPIO_IO_SEL_15_TO_8 0xff
#define BIT_GPIO_IO_SEL_15_TO_8(x)                                             \
	(((x) & BIT_MASK_GPIO_IO_SEL_15_TO_8) << BIT_SHIFT_GPIO_IO_SEL_15_TO_8)
#define BIT_GET_GPIO_IO_SEL_15_TO_8(x)                                         \
	(((x) >> BIT_SHIFT_GPIO_IO_SEL_15_TO_8) & BIT_MASK_GPIO_IO_SEL_15_TO_8)

#define BIT_SHIFT_GPIO_OUT_15_TO_8 8
#define BIT_MASK_GPIO_OUT_15_TO_8 0xff
#define BIT_GPIO_OUT_15_TO_8(x)                                                \
	(((x) & BIT_MASK_GPIO_OUT_15_TO_8) << BIT_SHIFT_GPIO_OUT_15_TO_8)
#define BIT_GET_GPIO_OUT_15_TO_8(x)                                            \
	(((x) >> BIT_SHIFT_GPIO_OUT_15_TO_8) & BIT_MASK_GPIO_OUT_15_TO_8)

#define BIT_SHIFT_GPIO_IN_15_TO_8 0
#define BIT_MASK_GPIO_IN_15_TO_8 0xff
#define BIT_GPIO_IN_15_TO_8(x)                                                 \
	(((x) & BIT_MASK_GPIO_IN_15_TO_8) << BIT_SHIFT_GPIO_IN_15_TO_8)
#define BIT_GET_GPIO_IN_15_TO_8(x)                                             \
	(((x) >> BIT_SHIFT_GPIO_IN_15_TO_8) & BIT_MASK_GPIO_IN_15_TO_8)

/* 2 REG_SDIO_H2C				(Offset 0x10250060) */

#define BIT_SHIFT_SDIO_H2C_MSG 0
#define BIT_MASK_SDIO_H2C_MSG 0xffffffffL
#define BIT_SDIO_H2C_MSG(x)                                                    \
	(((x) & BIT_MASK_SDIO_H2C_MSG) << BIT_SHIFT_SDIO_H2C_MSG)
#define BIT_GET_SDIO_H2C_MSG(x)                                                \
	(((x) >> BIT_SHIFT_SDIO_H2C_MSG) & BIT_MASK_SDIO_H2C_MSG)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_PAPE_WLBT_SEL BIT(29)
#define BIT_LNAON_WLBT_SEL BIT(28)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_BTGP_GPG3_FEN BIT(26)
#define BIT_BTGP_GPG2_FEN BIT(25)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_BTGP_JTAG_EN BIT(24)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_XTAL_CLK_EXTARNAL_EN BIT(23)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_BTGP_UART0_EN BIT(22)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_BTGP_UART1_EN BIT(21)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_BTGP_SPI_EN BIT(20)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_BTGP_GPIO_E2 BIT(19)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_BTGP_GPIO_EN BIT(18)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_SHIFT_BTGP_GPIO_SL 16
#define BIT_MASK_BTGP_GPIO_SL 0x3
#define BIT_BTGP_GPIO_SL(x)                                                    \
	(((x) & BIT_MASK_BTGP_GPIO_SL) << BIT_SHIFT_BTGP_GPIO_SL)
#define BIT_GET_BTGP_GPIO_SL(x)                                                \
	(((x) >> BIT_SHIFT_BTGP_GPIO_SL) & BIT_MASK_BTGP_GPIO_SL)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_PAD_SDIO_SR BIT(14)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_GPIO14_OUTPUT_PL BIT(13)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_HOST_WAKE_PAD_PULL_EN BIT(12)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_HOST_WAKE_PAD_SL BIT(11)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_PAD_LNAON_SR BIT(10)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_PAD_LNAON_E2 BIT(9)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_SW_LNAON_G_SEL_DATA BIT(8)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_SW_LNAON_A_SEL_DATA BIT(7)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_PAD_PAPE_SR BIT(6)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_PAD_PAPE_E2 BIT(5)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_SW_PAPE_G_SEL_DATA BIT(4)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_SW_PAPE_A_SEL_DATA BIT(3)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_PAD_DPDT_SR BIT(2)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_PAD_DPDT_PAD_E2 BIT(1)

/* 2 REG_PAD_CTRL1				(Offset 0x0064) */

#define BIT_SW_DPDT_SEL_DATA BIT(0)

/* 2 REG_SDIO_C2H				(Offset 0x10250064) */

#define BIT_SHIFT_SDIO_C2H_MSG 0
#define BIT_MASK_SDIO_C2H_MSG 0xffffffffL
#define BIT_SDIO_C2H_MSG(x)                                                    \
	(((x) & BIT_MASK_SDIO_C2H_MSG) << BIT_SHIFT_SDIO_C2H_MSG)
#define BIT_GET_SDIO_C2H_MSG(x)                                                \
	(((x) >> BIT_SHIFT_SDIO_C2H_MSG) & BIT_MASK_SDIO_C2H_MSG)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_ISO_BD2PP BIT(31)
#define BIT_LDOV12B_EN BIT(30)
#define BIT_CKEN_BTGPS BIT(29)
#define BIT_FEN_BTGPS BIT(28)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_MULRW BIT(27)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_BTCPU_BOOTSEL BIT(27)
#define BIT_SPI_SPEEDUP BIT(26)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_DEVWAKE_PAD_TYPE_SEL BIT(24)
#define BIT_CLKREQ_PAD_TYPE_SEL BIT(23)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_EN_CPL_TIMEOUT_PS BIT(22)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_ISO_BTPON2PP BIT(22)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_REG_TXDMA_FAIL_PS BIT(21)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_EN_HWENTR_L1 BIT(19)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_BT_HWROF_EN BIT(19)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_EN_ADV_CLKGATE BIT(18)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_BT_FUNC_EN BIT(18)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_BT_HWPDN_SL BIT(17)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_BT_DISN_EN BIT(16)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_BT_PDN_PULL_EN BIT(15)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_WL_PDN_PULL_EN BIT(14)
#define BIT_EXTERNAL_REQUEST_PL BIT(13)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_GPIO0_2_3_PULL_LOW_EN BIT(12)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_ISO_BA2PP BIT(11)
#define BIT_BT_AFE_LDO_EN BIT(10)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_BT_AFE_PLL_EN BIT(9)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_BT_DIG_CLK_EN BIT(8)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_WL_DRV_EXIST_IDX BIT(5)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_DOP_EHPAD BIT(4)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_WL_HWROF_EN BIT(3)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_WL_FUNC_EN BIT(2)

/* 2 REG_WL_BT_PWR_CTRL			(Offset 0x0068) */

#define BIT_WL_HWPDN_SL BIT(1)
#define BIT_WL_HWPDN_EN BIT(0)

/* 2 REG_SDM_DEBUG				(Offset 0x006C) */

#define BIT_SHIFT_WLCLK_PHASE 0
#define BIT_MASK_WLCLK_PHASE 0x1f
#define BIT_WLCLK_PHASE(x)                                                     \
	(((x) & BIT_MASK_WLCLK_PHASE) << BIT_SHIFT_WLCLK_PHASE)
#define BIT_GET_WLCLK_PHASE(x)                                                 \
	(((x) >> BIT_SHIFT_WLCLK_PHASE) & BIT_MASK_WLCLK_PHASE)

/* 2 REG_SYS_SDIO_CTRL			(Offset 0x0070) */

#define BIT_DBG_GNT_WL_BT BIT(27)

/* 2 REG_SYS_SDIO_CTRL			(Offset 0x0070) */

#define BIT_LTE_MUX_CTRL_PATH BIT(26)

/* 2 REG_SYS_SDIO_CTRL			(Offset 0x0070) */

#define BIT_LTE_COEX_UART BIT(25)

/* 2 REG_SYS_SDIO_CTRL			(Offset 0x0070) */

#define BIT_3W_LTE_WL_GPIO BIT(24)

/* 2 REG_SYS_SDIO_CTRL			(Offset 0x0070) */

#define BIT_SDIO_INT_POLARITY BIT(19)
#define BIT_SDIO_INT BIT(18)

/* 2 REG_SYS_SDIO_CTRL			(Offset 0x0070) */

#define BIT_SDIO_OFF_EN BIT(17)

/* 2 REG_SYS_SDIO_CTRL			(Offset 0x0070) */

#define BIT_SDIO_ON_EN BIT(16)

/* 2 REG_SYS_SDIO_CTRL			(Offset 0x0070) */

#define BIT_PCIE_WAIT_TIMEOUT_EVENT BIT(10)
#define BIT_PCIE_WAIT_TIME BIT(9)

/* 2 REG_SYS_SDIO_CTRL			(Offset 0x0070) */

#define BIT_MPCIE_REFCLK_XTAL_SEL BIT(8)

/* 2 REG_HCI_OPT_CTRL			(Offset 0x0074) */

#define BIT_SHIFT_TSFT_SEL 29
#define BIT_MASK_TSFT_SEL 0x7
#define BIT_TSFT_SEL(x) (((x) & BIT_MASK_TSFT_SEL) << BIT_SHIFT_TSFT_SEL)
#define BIT_GET_TSFT_SEL(x) (((x) >> BIT_SHIFT_TSFT_SEL) & BIT_MASK_TSFT_SEL)

/* 2 REG_HCI_OPT_CTRL			(Offset 0x0074) */

#define BIT_SHIFT_RPWM 24
#define BIT_MASK_RPWM 0xff
#define BIT_RPWM(x) (((x) & BIT_MASK_RPWM) << BIT_SHIFT_RPWM)
#define BIT_GET_RPWM(x) (((x) >> BIT_SHIFT_RPWM) & BIT_MASK_RPWM)

#define BIT_ROM_DLEN BIT(19)

#define BIT_SHIFT_ROM_PGE 16
#define BIT_MASK_ROM_PGE 0x7
#define BIT_ROM_PGE(x) (((x) & BIT_MASK_ROM_PGE) << BIT_SHIFT_ROM_PGE)
#define BIT_GET_ROM_PGE(x) (((x) >> BIT_SHIFT_ROM_PGE) & BIT_MASK_ROM_PGE)

/* 2 REG_HCI_OPT_CTRL			(Offset 0x0074) */

#define BIT_USB_HOST_PWR_OFF_EN BIT(12)

/* 2 REG_HCI_OPT_CTRL			(Offset 0x0074) */

#define BIT_SYM_LPS_BLOCK_EN BIT(11)

/* 2 REG_HCI_OPT_CTRL			(Offset 0x0074) */

#define BIT_USB_LPM_ACT_EN BIT(10)

/* 2 REG_HCI_OPT_CTRL			(Offset 0x0074) */

#define BIT_USB_LPM_NY BIT(9)

/* 2 REG_HCI_OPT_CTRL			(Offset 0x0074) */

#define BIT_USB_SUS_DIS BIT(8)

/* 2 REG_HCI_OPT_CTRL			(Offset 0x0074) */

#define BIT_SHIFT_SDIO_PAD_E 5
#define BIT_MASK_SDIO_PAD_E 0x7
#define BIT_SDIO_PAD_E(x) (((x) & BIT_MASK_SDIO_PAD_E) << BIT_SHIFT_SDIO_PAD_E)
#define BIT_GET_SDIO_PAD_E(x)                                                  \
	(((x) >> BIT_SHIFT_SDIO_PAD_E) & BIT_MASK_SDIO_PAD_E)

/* 2 REG_HCI_OPT_CTRL			(Offset 0x0074) */

#define BIT_USB_LPPLL_EN BIT(4)

/* 2 REG_HCI_OPT_CTRL			(Offset 0x0074) */

#define BIT_ROP_SW15 BIT(2)

/* 2 REG_HCI_OPT_CTRL			(Offset 0x0074) */

#define BIT_PCI_CKRDY_OPT BIT(1)

/* 2 REG_HCI_OPT_CTRL			(Offset 0x0074) */

#define BIT_PCI_VAUX_EN BIT(0)

/* 2 REG_LDO_SWR_CTRL			(Offset 0x007C) */

#define BIT_ZCD_HW_AUTO_EN BIT(27)
#define BIT_ZCD_REGSEL BIT(26)

/* 2 REG_LDO_SWR_CTRL			(Offset 0x007C) */

#define BIT_SHIFT_AUTO_ZCD_IN_CODE 21
#define BIT_MASK_AUTO_ZCD_IN_CODE 0x1f
#define BIT_AUTO_ZCD_IN_CODE(x)                                                \
	(((x) & BIT_MASK_AUTO_ZCD_IN_CODE) << BIT_SHIFT_AUTO_ZCD_IN_CODE)
#define BIT_GET_AUTO_ZCD_IN_CODE(x)                                            \
	(((x) >> BIT_SHIFT_AUTO_ZCD_IN_CODE) & BIT_MASK_AUTO_ZCD_IN_CODE)

/* 2 REG_LDO_SWR_CTRL			(Offset 0x007C) */

#define BIT_SHIFT_ZCD_CODE_IN_L 16
#define BIT_MASK_ZCD_CODE_IN_L 0x1f
#define BIT_ZCD_CODE_IN_L(x)                                                   \
	(((x) & BIT_MASK_ZCD_CODE_IN_L) << BIT_SHIFT_ZCD_CODE_IN_L)
#define BIT_GET_ZCD_CODE_IN_L(x)                                               \
	(((x) >> BIT_SHIFT_ZCD_CODE_IN_L) & BIT_MASK_ZCD_CODE_IN_L)

/* 2 REG_LDO_SWR_CTRL			(Offset 0x007C) */

#define BIT_SHIFT_LDO_HV5_DUMMY 14
#define BIT_MASK_LDO_HV5_DUMMY 0x3
#define BIT_LDO_HV5_DUMMY(x)                                                   \
	(((x) & BIT_MASK_LDO_HV5_DUMMY) << BIT_SHIFT_LDO_HV5_DUMMY)
#define BIT_GET_LDO_HV5_DUMMY(x)                                               \
	(((x) >> BIT_SHIFT_LDO_HV5_DUMMY) & BIT_MASK_LDO_HV5_DUMMY)

/* 2 REG_LDO_SWR_CTRL			(Offset 0x007C) */

#define BIT_SHIFT_REG_VTUNE33_BIT0_TO_BIT1 12
#define BIT_MASK_REG_VTUNE33_BIT0_TO_BIT1 0x3
#define BIT_REG_VTUNE33_BIT0_TO_BIT1(x)                                        \
	(((x) & BIT_MASK_REG_VTUNE33_BIT0_TO_BIT1)                             \
	 << BIT_SHIFT_REG_VTUNE33_BIT0_TO_BIT1)
#define BIT_GET_REG_VTUNE33_BIT0_TO_BIT1(x)                                    \
	(((x) >> BIT_SHIFT_REG_VTUNE33_BIT0_TO_BIT1) &                         \
	 BIT_MASK_REG_VTUNE33_BIT0_TO_BIT1)

/* 2 REG_LDO_SWR_CTRL			(Offset 0x007C) */

#define BIT_SHIFT_REG_STANDBY33_BIT0_TO_BIT1 10
#define BIT_MASK_REG_STANDBY33_BIT0_TO_BIT1 0x3
#define BIT_REG_STANDBY33_BIT0_TO_BIT1(x)                                      \
	(((x) & BIT_MASK_REG_STANDBY33_BIT0_TO_BIT1)                           \
	 << BIT_SHIFT_REG_STANDBY33_BIT0_TO_BIT1)
#define BIT_GET_REG_STANDBY33_BIT0_TO_BIT1(x)                                  \
	(((x) >> BIT_SHIFT_REG_STANDBY33_BIT0_TO_BIT1) &                       \
	 BIT_MASK_REG_STANDBY33_BIT0_TO_BIT1)

/* 2 REG_LDO_SWR_CTRL			(Offset 0x007C) */

#define BIT_SHIFT_REG_LOAD33_BIT0_TO_BIT1 8
#define BIT_MASK_REG_LOAD33_BIT0_TO_BIT1 0x3
#define BIT_REG_LOAD33_BIT0_TO_BIT1(x)                                         \
	(((x) & BIT_MASK_REG_LOAD33_BIT0_TO_BIT1)                              \
	 << BIT_SHIFT_REG_LOAD33_BIT0_TO_BIT1)
#define BIT_GET_REG_LOAD33_BIT0_TO_BIT1(x)                                     \
	(((x) >> BIT_SHIFT_REG_LOAD33_BIT0_TO_BIT1) &                          \
	 BIT_MASK_REG_LOAD33_BIT0_TO_BIT1)

/* 2 REG_LDO_SWR_CTRL			(Offset 0x007C) */

#define BIT_REG_BYPASS_L BIT(7)

/* 2 REG_LDO_SWR_CTRL			(Offset 0x007C) */

#define BIT_REG_LDOF_L BIT(6)

/* 2 REG_LDO_SWR_CTRL			(Offset 0x007C) */

#define BIT_REG_TYPE_L_V1 BIT(5)

/* 2 REG_LDO_SWR_CTRL			(Offset 0x007C) */

#define BIT_ARENB_L BIT(3)

/* 2 REG_LDO_SWR_CTRL			(Offset 0x007C) */

#define BIT_SHIFT_CFC_L 1
#define BIT_MASK_CFC_L 0x3
#define BIT_CFC_L(x) (((x) & BIT_MASK_CFC_L) << BIT_SHIFT_CFC_L)
#define BIT_GET_CFC_L(x) (((x) >> BIT_SHIFT_CFC_L) & BIT_MASK_CFC_L)

/* 2 REG_LDO_SWR_CTRL			(Offset 0x007C) */

#define BIT_REG_OCPS_L_V1 BIT(0)

/* 2 REG_MCUFW_CTRL				(Offset 0x0080) */

#define BIT_ANA_PORT_EN BIT(22)
#define BIT_MAC_PORT_EN BIT(21)
#define BIT_BOOT_FSPI_EN BIT(20)
#define BIT_FW_INIT_RDY BIT(15)
#define BIT_FW_DW_RDY BIT(14)

/* 2 REG_MCUFW_CTRL				(Offset 0x0080) */

#define BIT_SHIFT_CPU_CLK_SEL 12
#define BIT_MASK_CPU_CLK_SEL 0x3
#define BIT_CPU_CLK_SEL(x)                                                     \
	(((x) & BIT_MASK_CPU_CLK_SEL) << BIT_SHIFT_CPU_CLK_SEL)
#define BIT_GET_CPU_CLK_SEL(x)                                                 \
	(((x) >> BIT_SHIFT_CPU_CLK_SEL) & BIT_MASK_CPU_CLK_SEL)

/* 2 REG_MCUFW_CTRL				(Offset 0x0080) */

#define BIT_CCLK_CHG_MASK BIT(11)

/* 2 REG_MCUFW_CTRL				(Offset 0x0080) */

#define BIT_EMEM__TXBUF_CHKSUM_OK BIT(10)

/* 2 REG_MCUFW_CTRL				(Offset 0x0080) */

#define BIT_EMEM_TXBUF_DW_RDY BIT(9)

/* 2 REG_MCUFW_CTRL				(Offset 0x0080) */

#define BIT_EMEM_CHKSUM_OK BIT(8)
#define BIT_EMEM_DW_OK BIT(7)
#define BIT_TOGGLING BIT(7)
#define BIT_DMEM_CHKSUM_OK BIT(6)
#define BIT_ACK BIT(6)

/* 2 REG_MCUFW_CTRL				(Offset 0x0080) */

#define BIT_DMEM_DW_OK BIT(5)

/* 2 REG_MCUFW_CTRL				(Offset 0x0080) */

#define BIT_IMEM_CHKSUM_OK BIT(4)

/* 2 REG_MCUFW_CTRL				(Offset 0x0080) */

#define BIT_IMEM_DW_OK BIT(3)

/* 2 REG_MCUFW_CTRL				(Offset 0x0080) */

#define BIT_IMEM_BOOT_LOAD_CHKSUM_OK BIT(2)

/* 2 REG_MCUFW_CTRL				(Offset 0x0080) */

#define BIT_IMEM_BOOT_LOAD_DW_OK BIT(1)

/* 2 REG_SDIO_HRPWM1				(Offset 0x10250080) */

#define BIT_32K_PERMISSION BIT(0)

/* 2 REG_MCU_TST_CFG				(Offset 0x0084) */

#define BIT_SHIFT_LBKTST 0
#define BIT_MASK_LBKTST 0xffff
#define BIT_LBKTST(x) (((x) & BIT_MASK_LBKTST) << BIT_SHIFT_LBKTST)
#define BIT_GET_LBKTST(x) (((x) >> BIT_SHIFT_LBKTST) & BIT_MASK_LBKTST)

/* 2 REG_SDIO_BUS_CTRL			(Offset 0x10250085) */

#define BIT_PAD_CLK_XHGE_EN BIT(3)
#define BIT_INTER_CLK_EN BIT(2)
#define BIT_EN_RPT_TXCRC BIT(1)
#define BIT_DIS_RXDMA_STS BIT(0)

/* 2 REG_SDIO_HSUS_CTRL			(Offset 0x10250086) */

#define BIT_INTR_CTRL BIT(4)
#define BIT_SDIO_VOLTAGE BIT(3)
#define BIT_BYPASS_INIT BIT(2)

/* 2 REG_SDIO_HSUS_CTRL			(Offset 0x10250086) */

#define BIT_HCI_RESUME_RDY BIT(1)
#define BIT_HCI_SUS_REQ BIT(0)

/* 2 REG_HMEBOX_E0_E1			(Offset 0x0088) */

#define BIT_SHIFT_HOST_MSG_E1 16
#define BIT_MASK_HOST_MSG_E1 0xffff
#define BIT_HOST_MSG_E1(x)                                                     \
	(((x) & BIT_MASK_HOST_MSG_E1) << BIT_SHIFT_HOST_MSG_E1)
#define BIT_GET_HOST_MSG_E1(x)                                                 \
	(((x) >> BIT_SHIFT_HOST_MSG_E1) & BIT_MASK_HOST_MSG_E1)

#define BIT_SHIFT_HOST_MSG_E0 0
#define BIT_MASK_HOST_MSG_E0 0xffff
#define BIT_HOST_MSG_E0(x)                                                     \
	(((x) & BIT_MASK_HOST_MSG_E0) << BIT_SHIFT_HOST_MSG_E0)
#define BIT_GET_HOST_MSG_E0(x)                                                 \
	(((x) >> BIT_SHIFT_HOST_MSG_E0) & BIT_MASK_HOST_MSG_E0)

/* 2 REG_SDIO_RESPONSE_TIMER			(Offset 0x10250088) */

#define BIT_SHIFT_CMDIN_2RESP_TIMER 0
#define BIT_MASK_CMDIN_2RESP_TIMER 0xffff
#define BIT_CMDIN_2RESP_TIMER(x)                                               \
	(((x) & BIT_MASK_CMDIN_2RESP_TIMER) << BIT_SHIFT_CMDIN_2RESP_TIMER)
#define BIT_GET_CMDIN_2RESP_TIMER(x)                                           \
	(((x) >> BIT_SHIFT_CMDIN_2RESP_TIMER) & BIT_MASK_CMDIN_2RESP_TIMER)

/* 2 REG_SDIO_CMD_CRC			(Offset 0x1025008A) */

#define BIT_SHIFT_SDIO_CMD_CRC_V1 0
#define BIT_MASK_SDIO_CMD_CRC_V1 0xff
#define BIT_SDIO_CMD_CRC_V1(x)                                                 \
	(((x) & BIT_MASK_SDIO_CMD_CRC_V1) << BIT_SHIFT_SDIO_CMD_CRC_V1)
#define BIT_GET_SDIO_CMD_CRC_V1(x)                                             \
	(((x) >> BIT_SHIFT_SDIO_CMD_CRC_V1) & BIT_MASK_SDIO_CMD_CRC_V1)

/* 2 REG_HMEBOX_E2_E3			(Offset 0x008C) */

#define BIT_SHIFT_HOST_MSG_E3 16
#define BIT_MASK_HOST_MSG_E3 0xffff
#define BIT_HOST_MSG_E3(x)                                                     \
	(((x) & BIT_MASK_HOST_MSG_E3) << BIT_SHIFT_HOST_MSG_E3)
#define BIT_GET_HOST_MSG_E3(x)                                                 \
	(((x) >> BIT_SHIFT_HOST_MSG_E3) & BIT_MASK_HOST_MSG_E3)

#define BIT_SHIFT_HOST_MSG_E2 0
#define BIT_MASK_HOST_MSG_E2 0xffff
#define BIT_HOST_MSG_E2(x)                                                     \
	(((x) & BIT_MASK_HOST_MSG_E2) << BIT_SHIFT_HOST_MSG_E2)
#define BIT_GET_HOST_MSG_E2(x)                                                 \
	(((x) >> BIT_SHIFT_HOST_MSG_E2) & BIT_MASK_HOST_MSG_E2)

/* 2 REG_WLLPS_CTRL				(Offset 0x0090) */

#define BIT_WLLPSOP_EABM BIT(31)

/* 2 REG_WLLPS_CTRL				(Offset 0x0090) */

#define BIT_WLLPSOP_ACKF BIT(30)

/* 2 REG_WLLPS_CTRL				(Offset 0x0090) */

#define BIT_WLLPSOP_DLDM BIT(29)

/* 2 REG_WLLPS_CTRL				(Offset 0x0090) */

#define BIT_WLLPSOP_ESWR BIT(28)

/* 2 REG_WLLPS_CTRL				(Offset 0x0090) */

#define BIT_WLLPSOP_PWMM BIT(27)
#define BIT_WLLPSOP_EECK BIT(26)

/* 2 REG_WLLPS_CTRL				(Offset 0x0090) */

#define BIT_WLLPSOP_WLMACOFF BIT(25)

/* 2 REG_WLLPS_CTRL				(Offset 0x0090) */

#define BIT_WLLPSOP_EXTAL BIT(24)

/* 2 REG_WLLPS_CTRL				(Offset 0x0090) */

#define BIT_WL_SYNPON_VOLTSPDN BIT(23)

/* 2 REG_WLLPS_CTRL				(Offset 0x0090) */

#define BIT_WLLPSOP_WLBBOFF BIT(22)

/* 2 REG_WLLPS_CTRL				(Offset 0x0090) */

#define BIT_WLLPSOP_WLMEM_DS BIT(21)

/* 2 REG_WLLPS_CTRL				(Offset 0x0090) */

#define BIT_SHIFT_LPLDH12_VADJ_STEP_DN 12
#define BIT_MASK_LPLDH12_VADJ_STEP_DN 0xf
#define BIT_LPLDH12_VADJ_STEP_DN(x)                                            \
	(((x) & BIT_MASK_LPLDH12_VADJ_STEP_DN)                                 \
	 << BIT_SHIFT_LPLDH12_VADJ_STEP_DN)
#define BIT_GET_LPLDH12_VADJ_STEP_DN(x)                                        \
	(((x) >> BIT_SHIFT_LPLDH12_VADJ_STEP_DN) &                             \
	 BIT_MASK_LPLDH12_VADJ_STEP_DN)

/* 2 REG_WLLPS_CTRL				(Offset 0x0090) */

#define BIT_SHIFT_V15ADJ_L1_STEP_DN 8
#define BIT_MASK_V15ADJ_L1_STEP_DN 0x7
#define BIT_V15ADJ_L1_STEP_DN(x)                                               \
	(((x) & BIT_MASK_V15ADJ_L1_STEP_DN) << BIT_SHIFT_V15ADJ_L1_STEP_DN)
#define BIT_GET_V15ADJ_L1_STEP_DN(x)                                           \
	(((x) >> BIT_SHIFT_V15ADJ_L1_STEP_DN) & BIT_MASK_V15ADJ_L1_STEP_DN)

#define BIT_REGU_32K_CLK_EN BIT(1)
#define BIT_DRV_WLAN_INT_CLR BIT(1)

/* 2 REG_WLLPS_CTRL				(Offset 0x0090) */

#define BIT_WL_LPS_EN BIT(0)

/* 2 REG_SDIO_HSISR				(Offset 0x10250090) */

#define BIT_DRV_WLAN_INT BIT(0)

/* 2 REG_SDIO_HSIMR				(Offset 0x10250091) */

#define BIT_HISR_MASK BIT(0)

/* 2 REG_AFE_CTRL5				(Offset 0x0094) */

#define BIT_BB_DBG_SEL_AFE_SDM_BIT0 BIT(31)

/* 2 REG_AFE_CTRL5				(Offset 0x0094) */

#define BIT_ORDER_SDM BIT(30)
#define BIT_RFE_SEL_SDM BIT(29)

#define BIT_SHIFT_REF_SEL 25
#define BIT_MASK_REF_SEL 0xf
#define BIT_REF_SEL(x) (((x) & BIT_MASK_REF_SEL) << BIT_SHIFT_REF_SEL)
#define BIT_GET_REF_SEL(x) (((x) >> BIT_SHIFT_REF_SEL) & BIT_MASK_REF_SEL)

/* 2 REG_AFE_CTRL5				(Offset 0x0094) */

#define BIT_SHIFT_F0F_SDM 12
#define BIT_MASK_F0F_SDM 0x1fff
#define BIT_F0F_SDM(x) (((x) & BIT_MASK_F0F_SDM) << BIT_SHIFT_F0F_SDM)
#define BIT_GET_F0F_SDM(x) (((x) >> BIT_SHIFT_F0F_SDM) & BIT_MASK_F0F_SDM)

/* 2 REG_AFE_CTRL5				(Offset 0x0094) */

#define BIT_SHIFT_F0N_SDM 9
#define BIT_MASK_F0N_SDM 0x7
#define BIT_F0N_SDM(x) (((x) & BIT_MASK_F0N_SDM) << BIT_SHIFT_F0N_SDM)
#define BIT_GET_F0N_SDM(x) (((x) >> BIT_SHIFT_F0N_SDM) & BIT_MASK_F0N_SDM)

/* 2 REG_AFE_CTRL5				(Offset 0x0094) */

#define BIT_SHIFT_DIVN_SDM 3
#define BIT_MASK_DIVN_SDM 0x3f
#define BIT_DIVN_SDM(x) (((x) & BIT_MASK_DIVN_SDM) << BIT_SHIFT_DIVN_SDM)
#define BIT_GET_DIVN_SDM(x) (((x) >> BIT_SHIFT_DIVN_SDM) & BIT_MASK_DIVN_SDM)

/* 2 REG_GPIO_DEBOUNCE_CTRL			(Offset 0x0098) */

#define BIT_WLGP_DBC1EN BIT(15)

#define BIT_SHIFT_WLGP_DBC1 8
#define BIT_MASK_WLGP_DBC1 0xf
#define BIT_WLGP_DBC1(x) (((x) & BIT_MASK_WLGP_DBC1) << BIT_SHIFT_WLGP_DBC1)
#define BIT_GET_WLGP_DBC1(x) (((x) >> BIT_SHIFT_WLGP_DBC1) & BIT_MASK_WLGP_DBC1)

#define BIT_WLGP_DBC0EN BIT(7)

#define BIT_SHIFT_WLGP_DBC0 0
#define BIT_MASK_WLGP_DBC0 0xf
#define BIT_WLGP_DBC0(x) (((x) & BIT_MASK_WLGP_DBC0) << BIT_SHIFT_WLGP_DBC0)
#define BIT_GET_WLGP_DBC0(x) (((x) >> BIT_SHIFT_WLGP_DBC0) & BIT_MASK_WLGP_DBC0)

/* 2 REG_RPWM2				(Offset 0x009C) */

#define BIT_SHIFT_RPWM2 16
#define BIT_MASK_RPWM2 0xffff
#define BIT_RPWM2(x) (((x) & BIT_MASK_RPWM2) << BIT_SHIFT_RPWM2)
#define BIT_GET_RPWM2(x) (((x) >> BIT_SHIFT_RPWM2) & BIT_MASK_RPWM2)

/* 2 REG_SYSON_FSM_MON			(Offset 0x00A0) */

#define BIT_SHIFT_FSM_MON_SEL 24
#define BIT_MASK_FSM_MON_SEL 0x7
#define BIT_FSM_MON_SEL(x)                                                     \
	(((x) & BIT_MASK_FSM_MON_SEL) << BIT_SHIFT_FSM_MON_SEL)
#define BIT_GET_FSM_MON_SEL(x)                                                 \
	(((x) >> BIT_SHIFT_FSM_MON_SEL) & BIT_MASK_FSM_MON_SEL)

#define BIT_DOP_ELDO BIT(23)
#define BIT_FSM_MON_UPD BIT(15)

#define BIT_SHIFT_FSM_PAR 0
#define BIT_MASK_FSM_PAR 0x7fff
#define BIT_FSM_PAR(x) (((x) & BIT_MASK_FSM_PAR) << BIT_SHIFT_FSM_PAR)
#define BIT_GET_FSM_PAR(x) (((x) >> BIT_SHIFT_FSM_PAR) & BIT_MASK_FSM_PAR)

/* 2 REG_AFE_CTRL6				(Offset 0x00A4) */

#define BIT_SHIFT_BB_DBG_SEL_AFE_SDM_BIT3_1 0
#define BIT_MASK_BB_DBG_SEL_AFE_SDM_BIT3_1 0x7
#define BIT_BB_DBG_SEL_AFE_SDM_BIT3_1(x)                                       \
	(((x) & BIT_MASK_BB_DBG_SEL_AFE_SDM_BIT3_1)                            \
	 << BIT_SHIFT_BB_DBG_SEL_AFE_SDM_BIT3_1)
#define BIT_GET_BB_DBG_SEL_AFE_SDM_BIT3_1(x)                                   \
	(((x) >> BIT_SHIFT_BB_DBG_SEL_AFE_SDM_BIT3_1) &                        \
	 BIT_MASK_BB_DBG_SEL_AFE_SDM_BIT3_1)

/* 2 REG_PMC_DBG_CTRL1			(Offset 0x00A8) */

#define BIT_BT_INT_EN BIT(31)

#define BIT_SHIFT_RD_WR_WIFI_BT_INFO 16
#define BIT_MASK_RD_WR_WIFI_BT_INFO 0x7fff
#define BIT_RD_WR_WIFI_BT_INFO(x)                                              \
	(((x) & BIT_MASK_RD_WR_WIFI_BT_INFO) << BIT_SHIFT_RD_WR_WIFI_BT_INFO)
#define BIT_GET_RD_WR_WIFI_BT_INFO(x)                                          \
	(((x) >> BIT_SHIFT_RD_WR_WIFI_BT_INFO) & BIT_MASK_RD_WR_WIFI_BT_INFO)

/* 2 REG_PMC_DBG_CTRL1			(Offset 0x00A8) */

#define BIT_PMC_WR_OVF BIT(8)

#define BIT_SHIFT_WLPMC_ERRINT 0
#define BIT_MASK_WLPMC_ERRINT 0xff
#define BIT_WLPMC_ERRINT(x)                                                    \
	(((x) & BIT_MASK_WLPMC_ERRINT) << BIT_SHIFT_WLPMC_ERRINT)
#define BIT_GET_WLPMC_ERRINT(x)                                                \
	(((x) >> BIT_SHIFT_WLPMC_ERRINT) & BIT_MASK_WLPMC_ERRINT)

/* 2 REG_AFE_CTRL7				(Offset 0x00AC) */

#define BIT_SHIFT_SEL_V 30
#define BIT_MASK_SEL_V 0x3
#define BIT_SEL_V(x) (((x) & BIT_MASK_SEL_V) << BIT_SHIFT_SEL_V)
#define BIT_GET_SEL_V(x) (((x) >> BIT_SHIFT_SEL_V) & BIT_MASK_SEL_V)

/* 2 REG_AFE_CTRL7				(Offset 0x00AC) */

#define BIT_TXFIFO_TH_INT BIT(30)

/* 2 REG_AFE_CTRL7				(Offset 0x00AC) */

#define BIT_SEL_LDO_PC BIT(29)

/* 2 REG_AFE_CTRL7				(Offset 0x00AC) */

#define BIT_SHIFT_CK_MON_SEL 26
#define BIT_MASK_CK_MON_SEL 0x7
#define BIT_CK_MON_SEL(x) (((x) & BIT_MASK_CK_MON_SEL) << BIT_SHIFT_CK_MON_SEL)
#define BIT_GET_CK_MON_SEL(x)                                                  \
	(((x) >> BIT_SHIFT_CK_MON_SEL) & BIT_MASK_CK_MON_SEL)

/* 2 REG_AFE_CTRL7				(Offset 0x00AC) */

#define BIT_CK_MON_EN BIT(25)
#define BIT_FREF_EDGE BIT(24)
#define BIT_CK320M_EN BIT(23)
#define BIT_CK_5M_EN BIT(22)
#define BIT_TESTEN BIT(21)

/* 2 REG_HIMR0				(Offset 0x00B0) */

#define BIT_TIMEOUT_INTERRUPT2_MASK BIT(31)
#define BIT_TIMEOUT_INTERRUTP1_MASK BIT(30)
#define BIT_PSTIMEOUT_MSK BIT(29)
#define BIT_GTINT4_MSK BIT(28)
#define BIT_GTINT3_MSK BIT(27)
#define BIT_TXBCN0ERR_MSK BIT(26)
#define BIT_TXBCN0OK_MSK BIT(25)
#define BIT_TSF_BIT32_TOGGLE_MSK BIT(24)
#define BIT_BCNDMAINT0_MSK BIT(20)
#define BIT_BCNDERR0_MSK BIT(16)
#define BIT_HSISR_IND_ON_INT_MSK BIT(15)

/* 2 REG_HIMR0				(Offset 0x00B0) */

#define BIT_BCNDMAINT_E_MSK BIT(14)

/* 2 REG_HIMR0				(Offset 0x00B0) */

#define BIT_CTWEND_MSK BIT(12)
#define BIT_HISR1_IND_MSK BIT(11)

/* 2 REG_HIMR0				(Offset 0x00B0) */

#define BIT_C2HCMD_MSK BIT(10)
#define BIT_CPWM2_MSK BIT(9)
#define BIT_CPWM_MSK BIT(8)
#define BIT_HIGHDOK_MSK BIT(7)
#define BIT_MGTDOK_MSK BIT(6)
#define BIT_BKDOK_MSK BIT(5)
#define BIT_BEDOK_MSK BIT(4)
#define BIT_VIDOK_MSK BIT(3)
#define BIT_VODOK_MSK BIT(2)
#define BIT_RDU_MSK BIT(1)
#define BIT_RXOK_MSK BIT(0)

/* 2 REG_HISR0				(Offset 0x00B4) */

#define BIT_TIMEOUT_INTERRUPT2 BIT(31)

/* 2 REG_HISR0				(Offset 0x00B4) */

#define BIT_TIMEOUT_INTERRUTP1 BIT(30)

/* 2 REG_HISR0				(Offset 0x00B4) */

#define BIT_PSTIMEOUT BIT(29)
#define BIT_GTINT4 BIT(28)
#define BIT_GTINT3 BIT(27)
#define BIT_TXBCN0ERR BIT(26)
#define BIT_TXBCN0OK BIT(25)
#define BIT_TSF_BIT32_TOGGLE BIT(24)
#define BIT_BCNDMAINT0 BIT(20)
#define BIT_BCNDERR0 BIT(16)
#define BIT_HSISR_IND_ON_INT BIT(15)

/* 2 REG_HISR0				(Offset 0x00B4) */

#define BIT_BCNDMAINT_E BIT(14)

/* 2 REG_HISR0				(Offset 0x00B4) */

#define BIT_CTWEND BIT(12)

/* 2 REG_HISR0				(Offset 0x00B4) */

#define BIT_HISR1_IND_INT BIT(11)
#define BIT_C2HCMD BIT(10)
#define BIT_CPWM2 BIT(9)
#define BIT_CPWM BIT(8)
#define BIT_HIGHDOK BIT(7)
#define BIT_MGTDOK BIT(6)
#define BIT_BKDOK BIT(5)
#define BIT_BEDOK BIT(4)
#define BIT_VIDOK BIT(3)
#define BIT_VODOK BIT(2)
#define BIT_RDU BIT(1)
#define BIT_RXOK BIT(0)

/* 2 REG_HIMR1				(Offset 0x00B8) */

#define BIT_BTON_STS_UPDATE_MASK BIT(29)

/* 2 REG_HIMR1				(Offset 0x00B8) */

#define BIT_MCU_ERR_MASK BIT(28)

/* 2 REG_HIMR1				(Offset 0x00B8) */

#define BIT_BCNDMAINT7__MSK BIT(27)

/* 2 REG_HIMR1				(Offset 0x00B8) */

#define BIT_BCNDMAINT6__MSK BIT(26)

/* 2 REG_HIMR1				(Offset 0x00B8) */

#define BIT_BCNDMAINT5__MSK BIT(25)

/* 2 REG_HIMR1				(Offset 0x00B8) */

#define BIT_BCNDMAINT4__MSK BIT(24)

/* 2 REG_HIMR1				(Offset 0x00B8) */

#define BIT_BCNDMAINT3_MSK BIT(23)
#define BIT_BCNDMAINT2_MSK BIT(22)
#define BIT_BCNDMAINT1_MSK BIT(21)
#define BIT_BCNDERR7_MSK BIT(20)
#define BIT_BCNDERR6_MSK BIT(19)
#define BIT_BCNDERR5_MSK BIT(18)
#define BIT_BCNDERR4_MSK BIT(17)
#define BIT_BCNDERR3_MSK BIT(16)
#define BIT_BCNDERR2_MSK BIT(15)
#define BIT_BCNDERR1_MSK BIT(14)

/* 2 REG_HIMR1				(Offset 0x00B8) */

#define BIT_ATIMEND_E_MSK BIT(13)

/* 2 REG_HIMR1				(Offset 0x00B8) */

#define BIT_ATIMEND__MSK BIT(12)

/* 2 REG_HIMR1				(Offset 0x00B8) */

#define BIT_TXERR_MSK BIT(11)
#define BIT_RXERR_MSK BIT(10)
#define BIT_TXFOVW_MSK BIT(9)
#define BIT_FOVW_MSK BIT(8)

/* 2 REG_HIMR1				(Offset 0x00B8) */

#define BIT_CPU_MGQ_TXDONE_MSK BIT(5)
#define BIT_PS_TIMER_C_MSK BIT(4)
#define BIT_PS_TIMER_B_MSK BIT(3)
#define BIT_PS_TIMER_A_MSK BIT(2)
#define BIT_CPUMGQ_TX_TIMER_MSK BIT(1)

/* 2 REG_HISR1				(Offset 0x00BC) */

#define BIT_BTON_STS_UPDATE_INT BIT(29)

/* 2 REG_HISR1				(Offset 0x00BC) */

#define BIT_MCU_ERR BIT(28)

/* 2 REG_HISR1				(Offset 0x00BC) */

#define BIT_BCNDMAINT7 BIT(27)
#define BIT_BCNDMAINT6 BIT(26)
#define BIT_BCNDMAINT5 BIT(25)
#define BIT_BCNDMAINT4 BIT(24)
#define BIT_BCNDMAINT3 BIT(23)
#define BIT_BCNDMAINT2 BIT(22)
#define BIT_BCNDMAINT1 BIT(21)
#define BIT_BCNDERR7 BIT(20)
#define BIT_BCNDERR6 BIT(19)
#define BIT_BCNDERR5 BIT(18)
#define BIT_BCNDERR4 BIT(17)
#define BIT_BCNDERR3 BIT(16)
#define BIT_BCNDERR2 BIT(15)
#define BIT_BCNDERR1 BIT(14)

/* 2 REG_HISR1				(Offset 0x00BC) */

#define BIT_ATIMEND_E BIT(13)

/* 2 REG_HISR1				(Offset 0x00BC) */

#define BIT_ATIMEND BIT(12)
#define BIT_TXERR_INT BIT(11)
#define BIT_RXERR_INT BIT(10)
#define BIT_TXFOVW BIT(9)
#define BIT_FOVW BIT(8)

/* 2 REG_HISR1				(Offset 0x00BC) */

#define BIT_CPU_MGQ_TXDONE BIT(5)
#define BIT_PS_TIMER_C BIT(4)
#define BIT_PS_TIMER_B BIT(3)
#define BIT_PS_TIMER_A BIT(2)
#define BIT_CPUMGQ_TX_TIMER BIT(1)

/* 2 REG_SDIO_ERR_RPT			(Offset 0x102500C0) */

#define BIT_HR_FF_OVF BIT(6)
#define BIT_HR_FF_UDN BIT(5)
#define BIT_TXDMA_BUSY_ERR BIT(4)
#define BIT_TXDMA_VLD_ERR BIT(3)
#define BIT_QSEL_UNKNOWN_ERR BIT(2)
#define BIT_QSEL_MIS_ERR BIT(1)

/* 2 REG_DBG_PORT_SEL			(Offset 0x00C0) */

#define BIT_SHIFT_DEBUG_ST 0
#define BIT_MASK_DEBUG_ST 0xffffffffL
#define BIT_DEBUG_ST(x) (((x) & BIT_MASK_DEBUG_ST) << BIT_SHIFT_DEBUG_ST)
#define BIT_GET_DEBUG_ST(x) (((x) >> BIT_SHIFT_DEBUG_ST) & BIT_MASK_DEBUG_ST)

/* 2 REG_SDIO_ERR_RPT			(Offset 0x102500C0) */

#define BIT_SDIO_OVERRD_ERR BIT(0)

/* 2 REG_SDIO_CMD_ERRCNT			(Offset 0x102500C1) */

#define BIT_SHIFT_CMD_CRC_ERR_CNT 0
#define BIT_MASK_CMD_CRC_ERR_CNT 0xff
#define BIT_CMD_CRC_ERR_CNT(x)                                                 \
	(((x) & BIT_MASK_CMD_CRC_ERR_CNT) << BIT_SHIFT_CMD_CRC_ERR_CNT)
#define BIT_GET_CMD_CRC_ERR_CNT(x)                                             \
	(((x) >> BIT_SHIFT_CMD_CRC_ERR_CNT) & BIT_MASK_CMD_CRC_ERR_CNT)

/* 2 REG_SDIO_DATA_ERRCNT			(Offset 0x102500C2) */

#define BIT_SHIFT_DATA_CRC_ERR_CNT 0
#define BIT_MASK_DATA_CRC_ERR_CNT 0xff
#define BIT_DATA_CRC_ERR_CNT(x)                                                \
	(((x) & BIT_MASK_DATA_CRC_ERR_CNT) << BIT_SHIFT_DATA_CRC_ERR_CNT)
#define BIT_GET_DATA_CRC_ERR_CNT(x)                                            \
	(((x) >> BIT_SHIFT_DATA_CRC_ERR_CNT) & BIT_MASK_DATA_CRC_ERR_CNT)

/* 2 REG_PAD_CTRL2				(Offset 0x00C4) */

#define BIT_USB3_USB2_TRANSITION BIT(20)

/* 2 REG_PAD_CTRL2				(Offset 0x00C4) */

#define BIT_SHIFT_USB23_SW_MODE_V1 18
#define BIT_MASK_USB23_SW_MODE_V1 0x3
#define BIT_USB23_SW_MODE_V1(x)                                                \
	(((x) & BIT_MASK_USB23_SW_MODE_V1) << BIT_SHIFT_USB23_SW_MODE_V1)
#define BIT_GET_USB23_SW_MODE_V1(x)                                            \
	(((x) >> BIT_SHIFT_USB23_SW_MODE_V1) & BIT_MASK_USB23_SW_MODE_V1)

/* 2 REG_PAD_CTRL2				(Offset 0x00C4) */

#define BIT_NO_PDN_CHIPOFF_V1 BIT(17)

/* 2 REG_PAD_CTRL2				(Offset 0x00C4) */

#define BIT_RSM_EN_V1 BIT(16)

/* 2 REG_PAD_CTRL2				(Offset 0x00C4) */

#define BIT_LD_B12V_EN BIT(7)

/* 2 REG_PAD_CTRL2				(Offset 0x00C4) */

#define BIT_EECS_IOSEL_V1 BIT(6)

/* 2 REG_PAD_CTRL2				(Offset 0x00C4) */

#define BIT_EECS_DATA_O_V1 BIT(5)

/* 2 REG_PAD_CTRL2				(Offset 0x00C4) */

#define BIT_EECS_DATA_I_V1 BIT(4)

/* 2 REG_PAD_CTRL2				(Offset 0x00C4) */

#define BIT_EESK_IOSEL_V1 BIT(2)

/* 2 REG_PAD_CTRL2				(Offset 0x00C4) */

#define BIT_EESK_DATA_O_V1 BIT(1)

/* 2 REG_PAD_CTRL2				(Offset 0x00C4) */

#define BIT_EESK_DATA_I_V1 BIT(0)

/* 2 REG_SDIO_CMD_ERR_CONTENT		(Offset 0x102500C4) */

#define BIT_SHIFT_SDIO_CMD_ERR_CONTENT 0
#define BIT_MASK_SDIO_CMD_ERR_CONTENT 0xffffffffffL
#define BIT_SDIO_CMD_ERR_CONTENT(x)                                            \
	(((x) & BIT_MASK_SDIO_CMD_ERR_CONTENT)                                 \
	 << BIT_SHIFT_SDIO_CMD_ERR_CONTENT)
#define BIT_GET_SDIO_CMD_ERR_CONTENT(x)                                        \
	(((x) >> BIT_SHIFT_SDIO_CMD_ERR_CONTENT) &                             \
	 BIT_MASK_SDIO_CMD_ERR_CONTENT)

/* 2 REG_SDIO_CRC_ERR_IDX			(Offset 0x102500C9) */

#define BIT_D3_CRC_ERR BIT(4)
#define BIT_D2_CRC_ERR BIT(3)
#define BIT_D1_CRC_ERR BIT(2)
#define BIT_D0_CRC_ERR BIT(1)
#define BIT_CMD_CRC_ERR BIT(0)

/* 2 REG_SDIO_DATA_CRC			(Offset 0x102500CA) */

#define BIT_SHIFT_SDIO_DATA_CRC 0
#define BIT_MASK_SDIO_DATA_CRC 0xff
#define BIT_SDIO_DATA_CRC(x)                                                   \
	(((x) & BIT_MASK_SDIO_DATA_CRC) << BIT_SHIFT_SDIO_DATA_CRC)
#define BIT_GET_SDIO_DATA_CRC(x)                                               \
	(((x) >> BIT_SHIFT_SDIO_DATA_CRC) & BIT_MASK_SDIO_DATA_CRC)

/* 2 REG_SDIO_DATA_REPLY_TIME		(Offset 0x102500CB) */

#define BIT_SHIFT_SDIO_DATA_REPLY_TIME 0
#define BIT_MASK_SDIO_DATA_REPLY_TIME 0x7
#define BIT_SDIO_DATA_REPLY_TIME(x)                                            \
	(((x) & BIT_MASK_SDIO_DATA_REPLY_TIME)                                 \
	 << BIT_SHIFT_SDIO_DATA_REPLY_TIME)
#define BIT_GET_SDIO_DATA_REPLY_TIME(x)                                        \
	(((x) >> BIT_SHIFT_SDIO_DATA_REPLY_TIME) &                             \
	 BIT_MASK_SDIO_DATA_REPLY_TIME)

/* 2 REG_PMC_DBG_CTRL2			(Offset 0x00CC) */

#define BIT_SHIFT_EFUSE_BURN_GNT 24
#define BIT_MASK_EFUSE_BURN_GNT 0xff
#define BIT_EFUSE_BURN_GNT(x)                                                  \
	(((x) & BIT_MASK_EFUSE_BURN_GNT) << BIT_SHIFT_EFUSE_BURN_GNT)
#define BIT_GET_EFUSE_BURN_GNT(x)                                              \
	(((x) >> BIT_SHIFT_EFUSE_BURN_GNT) & BIT_MASK_EFUSE_BURN_GNT)

/* 2 REG_PMC_DBG_CTRL2			(Offset 0x00CC) */

#define BIT_STOP_WL_PMC BIT(9)
#define BIT_STOP_SYM_PMC BIT(8)

/* 2 REG_PMC_DBG_CTRL2			(Offset 0x00CC) */

#define BIT_REG_RST_WLPMC BIT(5)
#define BIT_REG_RST_PD12N BIT(4)
#define BIT_SYSON_DIS_WLREG_WRMSK BIT(3)
#define BIT_SYSON_DIS_PMCREG_WRMSK BIT(2)

#define BIT_SHIFT_SYSON_REG_ARB 0
#define BIT_MASK_SYSON_REG_ARB 0x3
#define BIT_SYSON_REG_ARB(x)                                                   \
	(((x) & BIT_MASK_SYSON_REG_ARB) << BIT_SHIFT_SYSON_REG_ARB)
#define BIT_GET_SYSON_REG_ARB(x)                                               \
	(((x) >> BIT_SHIFT_SYSON_REG_ARB) & BIT_MASK_SYSON_REG_ARB)

/* 2 REG_BIST_CTRL				(Offset 0x00D0) */

#define BIT_BIST_USB_DIS BIT(27)

/* 2 REG_BIST_CTRL				(Offset 0x00D0) */

#define BIT_BIST_PCI_DIS BIT(26)

/* 2 REG_BIST_CTRL				(Offset 0x00D0) */

#define BIT_BIST_BT_DIS BIT(25)

/* 2 REG_BIST_CTRL				(Offset 0x00D0) */

#define BIT_BIST_WL_DIS BIT(24)

/* 2 REG_BIST_CTRL				(Offset 0x00D0) */

#define BIT_SHIFT_BIST_RPT_SEL 16
#define BIT_MASK_BIST_RPT_SEL 0xf
#define BIT_BIST_RPT_SEL(x)                                                    \
	(((x) & BIT_MASK_BIST_RPT_SEL) << BIT_SHIFT_BIST_RPT_SEL)
#define BIT_GET_BIST_RPT_SEL(x)                                                \
	(((x) >> BIT_SHIFT_BIST_RPT_SEL) & BIT_MASK_BIST_RPT_SEL)

/* 2 REG_BIST_CTRL				(Offset 0x00D0) */

#define BIT_BIST_RESUME_PS BIT(4)

/* 2 REG_BIST_CTRL				(Offset 0x00D0) */

#define BIT_BIST_RESUME BIT(3)
#define BIT_BIST_NORMAL BIT(2)

/* 2 REG_BIST_CTRL				(Offset 0x00D0) */

#define BIT_BIST_RSTN BIT(1)
#define BIT_BIST_CLK_EN BIT(0)

/* 2 REG_BIST_RPT				(Offset 0x00D4) */

#define BIT_SHIFT_MBIST_REPORT 0
#define BIT_MASK_MBIST_REPORT 0xffffffffL
#define BIT_MBIST_REPORT(x)                                                    \
	(((x) & BIT_MASK_MBIST_REPORT) << BIT_SHIFT_MBIST_REPORT)
#define BIT_GET_MBIST_REPORT(x)                                                \
	(((x) >> BIT_SHIFT_MBIST_REPORT) & BIT_MASK_MBIST_REPORT)

/* 2 REG_MEM_CTRL				(Offset 0x00D8) */

#define BIT_UMEM_RME BIT(31)

/* 2 REG_MEM_CTRL				(Offset 0x00D8) */

#define BIT_SHIFT_BT_SPRAM 28
#define BIT_MASK_BT_SPRAM 0x3
#define BIT_BT_SPRAM(x) (((x) & BIT_MASK_BT_SPRAM) << BIT_SHIFT_BT_SPRAM)
#define BIT_GET_BT_SPRAM(x) (((x) >> BIT_SHIFT_BT_SPRAM) & BIT_MASK_BT_SPRAM)

/* 2 REG_MEM_CTRL				(Offset 0x00D8) */

#define BIT_SHIFT_BT_ROM 24
#define BIT_MASK_BT_ROM 0xf
#define BIT_BT_ROM(x) (((x) & BIT_MASK_BT_ROM) << BIT_SHIFT_BT_ROM)
#define BIT_GET_BT_ROM(x) (((x) >> BIT_SHIFT_BT_ROM) & BIT_MASK_BT_ROM)

#define BIT_SHIFT_PCI_DPRAM 10
#define BIT_MASK_PCI_DPRAM 0x3
#define BIT_PCI_DPRAM(x) (((x) & BIT_MASK_PCI_DPRAM) << BIT_SHIFT_PCI_DPRAM)
#define BIT_GET_PCI_DPRAM(x) (((x) >> BIT_SHIFT_PCI_DPRAM) & BIT_MASK_PCI_DPRAM)

/* 2 REG_MEM_CTRL				(Offset 0x00D8) */

#define BIT_SHIFT_PCI_SPRAM 8
#define BIT_MASK_PCI_SPRAM 0x3
#define BIT_PCI_SPRAM(x) (((x) & BIT_MASK_PCI_SPRAM) << BIT_SHIFT_PCI_SPRAM)
#define BIT_GET_PCI_SPRAM(x) (((x) >> BIT_SHIFT_PCI_SPRAM) & BIT_MASK_PCI_SPRAM)

#define BIT_SHIFT_USB_SPRAM 6
#define BIT_MASK_USB_SPRAM 0x3
#define BIT_USB_SPRAM(x) (((x) & BIT_MASK_USB_SPRAM) << BIT_SHIFT_USB_SPRAM)
#define BIT_GET_USB_SPRAM(x) (((x) >> BIT_SHIFT_USB_SPRAM) & BIT_MASK_USB_SPRAM)

/* 2 REG_MEM_CTRL				(Offset 0x00D8) */

#define BIT_SHIFT_USB_SPRF 4
#define BIT_MASK_USB_SPRF 0x3
#define BIT_USB_SPRF(x) (((x) & BIT_MASK_USB_SPRF) << BIT_SHIFT_USB_SPRF)
#define BIT_GET_USB_SPRF(x) (((x) >> BIT_SHIFT_USB_SPRF) & BIT_MASK_USB_SPRF)

/* 2 REG_MEM_CTRL				(Offset 0x00D8) */

#define BIT_SHIFT_MCU_ROM 0
#define BIT_MASK_MCU_ROM 0xf
#define BIT_MCU_ROM(x) (((x) & BIT_MASK_MCU_ROM) << BIT_SHIFT_MCU_ROM)
#define BIT_GET_MCU_ROM(x) (((x) >> BIT_SHIFT_MCU_ROM) & BIT_MASK_MCU_ROM)

/* 2 REG_AFE_CTRL8				(Offset 0x00DC) */

#define BIT_SYN_AGPIO BIT(20)

/* 2 REG_AFE_CTRL8				(Offset 0x00DC) */

#define BIT_XTAL_LP BIT(4)
#define BIT_XTAL_GM_SEP BIT(3)

/* 2 REG_AFE_CTRL8				(Offset 0x00DC) */

#define BIT_SHIFT_XTAL_SEL_TOK 0
#define BIT_MASK_XTAL_SEL_TOK 0x7
#define BIT_XTAL_SEL_TOK(x)                                                    \
	(((x) & BIT_MASK_XTAL_SEL_TOK) << BIT_SHIFT_XTAL_SEL_TOK)
#define BIT_GET_XTAL_SEL_TOK(x)                                                \
	(((x) >> BIT_SHIFT_XTAL_SEL_TOK) & BIT_MASK_XTAL_SEL_TOK)

/* 2 REG_USB_SIE_INTF			(Offset 0x00E0) */

#define BIT_RD_SEL BIT(31)

/* 2 REG_USB_SIE_INTF			(Offset 0x00E0) */

#define BIT_USB_SIE_INTF_WE_V1 BIT(30)
#define BIT_USB_SIE_INTF_BYIOREG_V1 BIT(29)
#define BIT_USB_SIE_SELECT BIT(28)

/* 2 REG_USB_SIE_INTF			(Offset 0x00E0) */

#define BIT_SHIFT_USB_SIE_INTF_ADDR_V1 16
#define BIT_MASK_USB_SIE_INTF_ADDR_V1 0x1ff
#define BIT_USB_SIE_INTF_ADDR_V1(x)                                            \
	(((x) & BIT_MASK_USB_SIE_INTF_ADDR_V1)                                 \
	 << BIT_SHIFT_USB_SIE_INTF_ADDR_V1)
#define BIT_GET_USB_SIE_INTF_ADDR_V1(x)                                        \
	(((x) >> BIT_SHIFT_USB_SIE_INTF_ADDR_V1) &                             \
	 BIT_MASK_USB_SIE_INTF_ADDR_V1)

/* 2 REG_USB_SIE_INTF			(Offset 0x00E0) */

#define BIT_SHIFT_USB_SIE_INTF_RD 8
#define BIT_MASK_USB_SIE_INTF_RD 0xff
#define BIT_USB_SIE_INTF_RD(x)                                                 \
	(((x) & BIT_MASK_USB_SIE_INTF_RD) << BIT_SHIFT_USB_SIE_INTF_RD)
#define BIT_GET_USB_SIE_INTF_RD(x)                                             \
	(((x) >> BIT_SHIFT_USB_SIE_INTF_RD) & BIT_MASK_USB_SIE_INTF_RD)

#define BIT_SHIFT_USB_SIE_INTF_WD 0
#define BIT_MASK_USB_SIE_INTF_WD 0xff
#define BIT_USB_SIE_INTF_WD(x)                                                 \
	(((x) & BIT_MASK_USB_SIE_INTF_WD) << BIT_SHIFT_USB_SIE_INTF_WD)
#define BIT_GET_USB_SIE_INTF_WD(x)                                             \
	(((x) >> BIT_SHIFT_USB_SIE_INTF_WD) & BIT_MASK_USB_SIE_INTF_WD)

/* 2 REG_PCIE_MIO_INTF			(Offset 0x00E4) */

#define BIT_PCIE_MIO_BYIOREG BIT(13)
#define BIT_PCIE_MIO_RE BIT(12)

#define BIT_SHIFT_PCIE_MIO_WE 8
#define BIT_MASK_PCIE_MIO_WE 0xf
#define BIT_PCIE_MIO_WE(x)                                                     \
	(((x) & BIT_MASK_PCIE_MIO_WE) << BIT_SHIFT_PCIE_MIO_WE)
#define BIT_GET_PCIE_MIO_WE(x)                                                 \
	(((x) >> BIT_SHIFT_PCIE_MIO_WE) & BIT_MASK_PCIE_MIO_WE)

#define BIT_SHIFT_PCIE_MIO_ADDR 0
#define BIT_MASK_PCIE_MIO_ADDR 0xff
#define BIT_PCIE_MIO_ADDR(x)                                                   \
	(((x) & BIT_MASK_PCIE_MIO_ADDR) << BIT_SHIFT_PCIE_MIO_ADDR)
#define BIT_GET_PCIE_MIO_ADDR(x)                                               \
	(((x) >> BIT_SHIFT_PCIE_MIO_ADDR) & BIT_MASK_PCIE_MIO_ADDR)

/* 2 REG_PCIE_MIO_INTD			(Offset 0x00E8) */

#define BIT_SHIFT_PCIE_MIO_DATA 0
#define BIT_MASK_PCIE_MIO_DATA 0xffffffffL
#define BIT_PCIE_MIO_DATA(x)                                                   \
	(((x) & BIT_MASK_PCIE_MIO_DATA) << BIT_SHIFT_PCIE_MIO_DATA)
#define BIT_GET_PCIE_MIO_DATA(x)                                               \
	(((x) >> BIT_SHIFT_PCIE_MIO_DATA) & BIT_MASK_PCIE_MIO_DATA)

/* 2 REG_WLRF1				(Offset 0x00EC) */

#define BIT_SHIFT_WLRF1_CTRL 24
#define BIT_MASK_WLRF1_CTRL 0xff
#define BIT_WLRF1_CTRL(x) (((x) & BIT_MASK_WLRF1_CTRL) << BIT_SHIFT_WLRF1_CTRL)
#define BIT_GET_WLRF1_CTRL(x)                                                  \
	(((x) >> BIT_SHIFT_WLRF1_CTRL) & BIT_MASK_WLRF1_CTRL)

/* 2 REG_SYS_CFG1				(Offset 0x00F0) */

#define BIT_SHIFT_TRP_ICFG 28
#define BIT_MASK_TRP_ICFG 0xf
#define BIT_TRP_ICFG(x) (((x) & BIT_MASK_TRP_ICFG) << BIT_SHIFT_TRP_ICFG)
#define BIT_GET_TRP_ICFG(x) (((x) >> BIT_SHIFT_TRP_ICFG) & BIT_MASK_TRP_ICFG)

/* 2 REG_SYS_CFG1				(Offset 0x00F0) */

#define BIT_RF_TYPE_ID BIT(27)
#define BIT_BD_HCI_SEL BIT(26)

/* 2 REG_SYS_CFG1				(Offset 0x00F0) */

#define BIT_BD_PKG_SEL BIT(25)

/* 2 REG_SYS_CFG1				(Offset 0x00F0) */

#define BIT_SPSLDO_SEL BIT(24)

/* 2 REG_SYS_CFG1				(Offset 0x00F0) */

#define BIT_RTL_ID BIT(23)
#define BIT_PAD_HWPD_IDN BIT(22)

/* 2 REG_SYS_CFG1				(Offset 0x00F0) */

#define BIT_TESTMODE BIT(20)

/* 2 REG_SYS_CFG1				(Offset 0x00F0) */

#define BIT_SHIFT_VENDOR_ID 16
#define BIT_MASK_VENDOR_ID 0xf
#define BIT_VENDOR_ID(x) (((x) & BIT_MASK_VENDOR_ID) << BIT_SHIFT_VENDOR_ID)
#define BIT_GET_VENDOR_ID(x) (((x) >> BIT_SHIFT_VENDOR_ID) & BIT_MASK_VENDOR_ID)

/* 2 REG_SYS_CFG1				(Offset 0x00F0) */

#define BIT_SHIFT_CHIP_VER 12
#define BIT_MASK_CHIP_VER 0xf
#define BIT_CHIP_VER(x) (((x) & BIT_MASK_CHIP_VER) << BIT_SHIFT_CHIP_VER)
#define BIT_GET_CHIP_VER(x) (((x) >> BIT_SHIFT_CHIP_VER) & BIT_MASK_CHIP_VER)

/* 2 REG_SYS_CFG1				(Offset 0x00F0) */

#define BIT_BD_MAC3 BIT(11)

/* 2 REG_SYS_CFG1				(Offset 0x00F0) */

#define BIT_BD_MAC1 BIT(10)
#define BIT_BD_MAC2 BIT(9)
#define BIT_SIC_IDLE BIT(8)
#define BIT_SW_OFFLOAD_EN BIT(7)

/* 2 REG_SYS_CFG1				(Offset 0x00F0) */

#define BIT_OCP_SHUTDN BIT(6)

/* 2 REG_SYS_CFG1				(Offset 0x00F0) */

#define BIT_V15_VLD BIT(5)

/* 2 REG_SYS_CFG1				(Offset 0x00F0) */

#define BIT_PCIRSTB BIT(4)

/* 2 REG_SYS_CFG1				(Offset 0x00F0) */

#define BIT_PCLK_VLD BIT(3)

/* 2 REG_SYS_CFG1				(Offset 0x00F0) */

#define BIT_UCLK_VLD BIT(2)

/* 2 REG_SYS_CFG1				(Offset 0x00F0) */

#define BIT_ACLK_VLD BIT(1)

/* 2 REG_SYS_CFG1				(Offset 0x00F0) */

#define BIT_XCLK_VLD BIT(0)

/* 2 REG_SYS_STATUS1				(Offset 0x00F4) */

#define BIT_SHIFT_RF_RL_ID 28
#define BIT_MASK_RF_RL_ID 0xf
#define BIT_RF_RL_ID(x) (((x) & BIT_MASK_RF_RL_ID) << BIT_SHIFT_RF_RL_ID)
#define BIT_GET_RF_RL_ID(x) (((x) >> BIT_SHIFT_RF_RL_ID) & BIT_MASK_RF_RL_ID)

/* 2 REG_SYS_STATUS1				(Offset 0x00F4) */

#define BIT_HPHY_ICFG BIT(19)

/* 2 REG_SYS_STATUS1				(Offset 0x00F4) */

#define BIT_SHIFT_SEL_0XC0 16
#define BIT_MASK_SEL_0XC0 0x3
#define BIT_SEL_0XC0(x) (((x) & BIT_MASK_SEL_0XC0) << BIT_SHIFT_SEL_0XC0)
#define BIT_GET_SEL_0XC0(x) (((x) >> BIT_SHIFT_SEL_0XC0) & BIT_MASK_SEL_0XC0)

/* 2 REG_SYS_STATUS1				(Offset 0x00F4) */

#define BIT_SHIFT_HCI_SEL_V3 12
#define BIT_MASK_HCI_SEL_V3 0x7
#define BIT_HCI_SEL_V3(x) (((x) & BIT_MASK_HCI_SEL_V3) << BIT_SHIFT_HCI_SEL_V3)
#define BIT_GET_HCI_SEL_V3(x)                                                  \
	(((x) >> BIT_SHIFT_HCI_SEL_V3) & BIT_MASK_HCI_SEL_V3)

/* 2 REG_SYS_STATUS1				(Offset 0x00F4) */

#define BIT_USB_OPERATION_MODE BIT(10)
#define BIT_BT_PDN BIT(9)
#define BIT_AUTO_WLPON BIT(8)

/* 2 REG_SYS_STATUS1				(Offset 0x00F4) */

#define BIT_WL_MODE BIT(7)

/* 2 REG_SYS_STATUS1				(Offset 0x00F4) */

#define BIT_PKG_SEL_HCI BIT(6)

/* 2 REG_SYS_STATUS1				(Offset 0x00F4) */

#define BIT_SHIFT_PAD_HCI_SEL_V1 3
#define BIT_MASK_PAD_HCI_SEL_V1 0x7
#define BIT_PAD_HCI_SEL_V1(x)                                                  \
	(((x) & BIT_MASK_PAD_HCI_SEL_V1) << BIT_SHIFT_PAD_HCI_SEL_V1)
#define BIT_GET_PAD_HCI_SEL_V1(x)                                              \
	(((x) >> BIT_SHIFT_PAD_HCI_SEL_V1) & BIT_MASK_PAD_HCI_SEL_V1)

/* 2 REG_SYS_STATUS1				(Offset 0x00F4) */

#define BIT_SHIFT_EFS_HCI_SEL_V1 0
#define BIT_MASK_EFS_HCI_SEL_V1 0x7
#define BIT_EFS_HCI_SEL_V1(x)                                                  \
	(((x) & BIT_MASK_EFS_HCI_SEL_V1) << BIT_SHIFT_EFS_HCI_SEL_V1)
#define BIT_GET_EFS_HCI_SEL_V1(x)                                              \
	(((x) >> BIT_SHIFT_EFS_HCI_SEL_V1) & BIT_MASK_EFS_HCI_SEL_V1)

/* 2 REG_SYS_STATUS2				(Offset 0x00F8) */

#define BIT_SIO_ALDN BIT(19)
#define BIT_USB_ALDN BIT(18)
#define BIT_PCI_ALDN BIT(17)
#define BIT_SYS_ALDN BIT(16)

#define BIT_SHIFT_EPVID1 8
#define BIT_MASK_EPVID1 0xff
#define BIT_EPVID1(x) (((x) & BIT_MASK_EPVID1) << BIT_SHIFT_EPVID1)
#define BIT_GET_EPVID1(x) (((x) >> BIT_SHIFT_EPVID1) & BIT_MASK_EPVID1)

#define BIT_SHIFT_EPVID0 0
#define BIT_MASK_EPVID0 0xff
#define BIT_EPVID0(x) (((x) & BIT_MASK_EPVID0) << BIT_SHIFT_EPVID0)
#define BIT_GET_EPVID0(x) (((x) >> BIT_SHIFT_EPVID0) & BIT_MASK_EPVID0)

/* 2 REG_SYS_CFG2				(Offset 0x00FC) */

#define BIT_HCI_SEL_EMBEDDED BIT(8)

/* 2 REG_SYS_CFG2				(Offset 0x00FC) */

#define BIT_SHIFT_HW_ID 0
#define BIT_MASK_HW_ID 0xff
#define BIT_HW_ID(x) (((x) & BIT_MASK_HW_ID) << BIT_SHIFT_HW_ID)
#define BIT_GET_HW_ID(x) (((x) >> BIT_SHIFT_HW_ID) & BIT_MASK_HW_ID)

/* 2 REG_CR					(Offset 0x0100) */

#define BIT_SHIFT_LBMODE 24
#define BIT_MASK_LBMODE 0x1f
#define BIT_LBMODE(x) (((x) & BIT_MASK_LBMODE) << BIT_SHIFT_LBMODE)
#define BIT_GET_LBMODE(x) (((x) >> BIT_SHIFT_LBMODE) & BIT_MASK_LBMODE)

#define BIT_SHIFT_NETYPE1 18
#define BIT_MASK_NETYPE1 0x3
#define BIT_NETYPE1(x) (((x) & BIT_MASK_NETYPE1) << BIT_SHIFT_NETYPE1)
#define BIT_GET_NETYPE1(x) (((x) >> BIT_SHIFT_NETYPE1) & BIT_MASK_NETYPE1)

#define BIT_SHIFT_NETYPE0 16
#define BIT_MASK_NETYPE0 0x3
#define BIT_NETYPE0(x) (((x) & BIT_MASK_NETYPE0) << BIT_SHIFT_NETYPE0)
#define BIT_GET_NETYPE0(x) (((x) >> BIT_SHIFT_NETYPE0) & BIT_MASK_NETYPE0)

/* 2 REG_CR					(Offset 0x0100) */

#define BIT_I2C_MAILBOX_EN BIT(12)
#define BIT_SHCUT_EN BIT(11)

/* 2 REG_CR					(Offset 0x0100) */

#define BIT_32K_CAL_TMR_EN BIT(10)
#define BIT_MAC_SEC_EN BIT(9)
#define BIT_ENSWBCN BIT(8)
#define BIT_MACRXEN BIT(7)
#define BIT_MACTXEN BIT(6)
#define BIT_SCHEDULE_EN BIT(5)
#define BIT_PROTOCOL_EN BIT(4)
#define BIT_RXDMA_EN BIT(3)
#define BIT_TXDMA_EN BIT(2)
#define BIT_HCI_RXDMA_EN BIT(1)
#define BIT_HCI_TXDMA_EN BIT(0)

/* 2 REG_PKT_BUFF_ACCESS_CTRL		(Offset 0x0106) */

#define BIT_SHIFT_PKT_BUFF_ACCESS_CTRL 0
#define BIT_MASK_PKT_BUFF_ACCESS_CTRL 0xff
#define BIT_PKT_BUFF_ACCESS_CTRL(x)                                            \
	(((x) & BIT_MASK_PKT_BUFF_ACCESS_CTRL)                                 \
	 << BIT_SHIFT_PKT_BUFF_ACCESS_CTRL)
#define BIT_GET_PKT_BUFF_ACCESS_CTRL(x)                                        \
	(((x) >> BIT_SHIFT_PKT_BUFF_ACCESS_CTRL) &                             \
	 BIT_MASK_PKT_BUFF_ACCESS_CTRL)

/* 2 REG_TSF_CLK_STATE			(Offset 0x0108) */

#define BIT_TSF_CLK_STABLE BIT(15)

#define BIT_SHIFT_I2C_M_BUS_GNT_FW 4
#define BIT_MASK_I2C_M_BUS_GNT_FW 0x7
#define BIT_I2C_M_BUS_GNT_FW(x)                                                \
	(((x) & BIT_MASK_I2C_M_BUS_GNT_FW) << BIT_SHIFT_I2C_M_BUS_GNT_FW)
#define BIT_GET_I2C_M_BUS_GNT_FW(x)                                            \
	(((x) >> BIT_SHIFT_I2C_M_BUS_GNT_FW) & BIT_MASK_I2C_M_BUS_GNT_FW)

#define BIT_I2C_M_GNT_FW BIT(3)

#define BIT_SHIFT_I2C_M_SPEED 1
#define BIT_MASK_I2C_M_SPEED 0x3
#define BIT_I2C_M_SPEED(x)                                                     \
	(((x) & BIT_MASK_I2C_M_SPEED) << BIT_SHIFT_I2C_M_SPEED)
#define BIT_GET_I2C_M_SPEED(x)                                                 \
	(((x) >> BIT_SHIFT_I2C_M_SPEED) & BIT_MASK_I2C_M_SPEED)

#define BIT_I2C_M_UNLOCK BIT(0)

/* 2 REG_TXDMA_PQ_MAP			(Offset 0x010C) */

#define BIT_SHIFT_TXDMA_HIQ_MAP 14
#define BIT_MASK_TXDMA_HIQ_MAP 0x3
#define BIT_TXDMA_HIQ_MAP(x)                                                   \
	(((x) & BIT_MASK_TXDMA_HIQ_MAP) << BIT_SHIFT_TXDMA_HIQ_MAP)
#define BIT_GET_TXDMA_HIQ_MAP(x)                                               \
	(((x) >> BIT_SHIFT_TXDMA_HIQ_MAP) & BIT_MASK_TXDMA_HIQ_MAP)

#define BIT_SHIFT_TXDMA_MGQ_MAP 12
#define BIT_MASK_TXDMA_MGQ_MAP 0x3
#define BIT_TXDMA_MGQ_MAP(x)                                                   \
	(((x) & BIT_MASK_TXDMA_MGQ_MAP) << BIT_SHIFT_TXDMA_MGQ_MAP)
#define BIT_GET_TXDMA_MGQ_MAP(x)                                               \
	(((x) >> BIT_SHIFT_TXDMA_MGQ_MAP) & BIT_MASK_TXDMA_MGQ_MAP)

#define BIT_SHIFT_TXDMA_BKQ_MAP 10
#define BIT_MASK_TXDMA_BKQ_MAP 0x3
#define BIT_TXDMA_BKQ_MAP(x)                                                   \
	(((x) & BIT_MASK_TXDMA_BKQ_MAP) << BIT_SHIFT_TXDMA_BKQ_MAP)
#define BIT_GET_TXDMA_BKQ_MAP(x)                                               \
	(((x) >> BIT_SHIFT_TXDMA_BKQ_MAP) & BIT_MASK_TXDMA_BKQ_MAP)

#define BIT_SHIFT_TXDMA_BEQ_MAP 8
#define BIT_MASK_TXDMA_BEQ_MAP 0x3
#define BIT_TXDMA_BEQ_MAP(x)                                                   \
	(((x) & BIT_MASK_TXDMA_BEQ_MAP) << BIT_SHIFT_TXDMA_BEQ_MAP)
#define BIT_GET_TXDMA_BEQ_MAP(x)                                               \
	(((x) >> BIT_SHIFT_TXDMA_BEQ_MAP) & BIT_MASK_TXDMA_BEQ_MAP)

#define BIT_SHIFT_TXDMA_VIQ_MAP 6
#define BIT_MASK_TXDMA_VIQ_MAP 0x3
#define BIT_TXDMA_VIQ_MAP(x)                                                   \
	(((x) & BIT_MASK_TXDMA_VIQ_MAP) << BIT_SHIFT_TXDMA_VIQ_MAP)
#define BIT_GET_TXDMA_VIQ_MAP(x)                                               \
	(((x) >> BIT_SHIFT_TXDMA_VIQ_MAP) & BIT_MASK_TXDMA_VIQ_MAP)

#define BIT_SHIFT_TXDMA_VOQ_MAP 4
#define BIT_MASK_TXDMA_VOQ_MAP 0x3
#define BIT_TXDMA_VOQ_MAP(x)                                                   \
	(((x) & BIT_MASK_TXDMA_VOQ_MAP) << BIT_SHIFT_TXDMA_VOQ_MAP)
#define BIT_GET_TXDMA_VOQ_MAP(x)                                               \
	(((x) >> BIT_SHIFT_TXDMA_VOQ_MAP) & BIT_MASK_TXDMA_VOQ_MAP)

#define BIT_RXDMA_AGG_EN BIT(2)
#define BIT_RXSHFT_EN BIT(1)
#define BIT_RXDMA_ARBBW_EN BIT(0)

/* 2 REG_TRXFF_BNDY				(Offset 0x0114) */

#define BIT_SHIFT_RXFFOVFL_RSV_V2 8
#define BIT_MASK_RXFFOVFL_RSV_V2 0xf
#define BIT_RXFFOVFL_RSV_V2(x)                                                 \
	(((x) & BIT_MASK_RXFFOVFL_RSV_V2) << BIT_SHIFT_RXFFOVFL_RSV_V2)
#define BIT_GET_RXFFOVFL_RSV_V2(x)                                             \
	(((x) >> BIT_SHIFT_RXFFOVFL_RSV_V2) & BIT_MASK_RXFFOVFL_RSV_V2)

/* 2 REG_TRXFF_BNDY				(Offset 0x0114) */

#define BIT_SHIFT_TXPKTBUF_PGBNDY 0
#define BIT_MASK_TXPKTBUF_PGBNDY 0xff
#define BIT_TXPKTBUF_PGBNDY(x)                                                 \
	(((x) & BIT_MASK_TXPKTBUF_PGBNDY) << BIT_SHIFT_TXPKTBUF_PGBNDY)
#define BIT_GET_TXPKTBUF_PGBNDY(x)                                             \
	(((x) >> BIT_SHIFT_TXPKTBUF_PGBNDY) & BIT_MASK_TXPKTBUF_PGBNDY)

/* 2 REG_TRXFF_BNDY				(Offset 0x0114) */

#define BIT_SHIFT_RXFF0_BNDY_V2 0
#define BIT_MASK_RXFF0_BNDY_V2 0x3ffff
#define BIT_RXFF0_BNDY_V2(x)                                                   \
	(((x) & BIT_MASK_RXFF0_BNDY_V2) << BIT_SHIFT_RXFF0_BNDY_V2)
#define BIT_GET_RXFF0_BNDY_V2(x)                                               \
	(((x) >> BIT_SHIFT_RXFF0_BNDY_V2) & BIT_MASK_RXFF0_BNDY_V2)

#define BIT_SHIFT_RXFF0_RDPTR_V2 0
#define BIT_MASK_RXFF0_RDPTR_V2 0x3ffff
#define BIT_RXFF0_RDPTR_V2(x)                                                  \
	(((x) & BIT_MASK_RXFF0_RDPTR_V2) << BIT_SHIFT_RXFF0_RDPTR_V2)
#define BIT_GET_RXFF0_RDPTR_V2(x)                                              \
	(((x) >> BIT_SHIFT_RXFF0_RDPTR_V2) & BIT_MASK_RXFF0_RDPTR_V2)

#define BIT_SHIFT_RXFF0_WTPTR_V2 0
#define BIT_MASK_RXFF0_WTPTR_V2 0x3ffff
#define BIT_RXFF0_WTPTR_V2(x)                                                  \
	(((x) & BIT_MASK_RXFF0_WTPTR_V2) << BIT_SHIFT_RXFF0_WTPTR_V2)
#define BIT_GET_RXFF0_WTPTR_V2(x)                                              \
	(((x) >> BIT_SHIFT_RXFF0_WTPTR_V2) & BIT_MASK_RXFF0_WTPTR_V2)

/* 2 REG_PTA_I2C_MBOX			(Offset 0x0118) */

#define BIT_SHIFT_I2C_M_STATUS 8
#define BIT_MASK_I2C_M_STATUS 0xf
#define BIT_I2C_M_STATUS(x)                                                    \
	(((x) & BIT_MASK_I2C_M_STATUS) << BIT_SHIFT_I2C_M_STATUS)
#define BIT_GET_I2C_M_STATUS(x)                                                \
	(((x) >> BIT_SHIFT_I2C_M_STATUS) & BIT_MASK_I2C_M_STATUS)

/* 2 REG_FE1IMR				(Offset 0x0120) */

#define BIT_FS_RXDMA2_DONE_INT_EN BIT(28)
#define BIT_FS_RXDONE3_INT_EN BIT(27)
#define BIT_FS_RXDONE2_INT_EN BIT(26)
#define BIT_FS_RX_BCN_P4_INT_EN BIT(25)
#define BIT_FS_RX_BCN_P3_INT_EN BIT(24)
#define BIT_FS_RX_BCN_P2_INT_EN BIT(23)
#define BIT_FS_RX_BCN_P1_INT_EN BIT(22)
#define BIT_FS_RX_BCN_P0_INT_EN BIT(21)
#define BIT_FS_RX_UMD0_INT_EN BIT(20)
#define BIT_FS_RX_UMD1_INT_EN BIT(19)
#define BIT_FS_RX_BMD0_INT_EN BIT(18)
#define BIT_FS_RX_BMD1_INT_EN BIT(17)
#define BIT_FS_RXDONE_INT_EN BIT(16)
#define BIT_FS_WWLAN_INT_EN BIT(15)
#define BIT_FS_SOUND_DONE_INT_EN BIT(14)
#define BIT_FS_LP_STBY_INT_EN BIT(13)
#define BIT_FS_TRL_MTR_INT_EN BIT(12)
#define BIT_FS_BF1_PRETO_INT_EN BIT(11)
#define BIT_FS_BF0_PRETO_INT_EN BIT(10)
#define BIT_FS_PTCL_RELEASE_MACID_INT_EN BIT(9)

/* 2 REG_FE1IMR				(Offset 0x0120) */

#define BIT_FS_LTE_COEX_EN BIT(6)

/* 2 REG_FE1IMR				(Offset 0x0120) */

#define BIT_FS_WLACTOFF_INT_EN BIT(5)
#define BIT_FS_WLACTON_INT_EN BIT(4)
#define BIT_FS_BTCMD_INT_EN BIT(3)

/* 2 REG_FE1IMR				(Offset 0x0120) */

#define BIT_FS_REG_MAILBOX_TO_I2C_INT_EN BIT(2)

/* 2 REG_FE1IMR				(Offset 0x0120) */

#define BIT_FS_TRPC_TO_INT_EN_V1 BIT(1)

/* 2 REG_FE1IMR				(Offset 0x0120) */

#define BIT_FS_RPC_O_T_INT_EN_V1 BIT(0)

/* 2 REG_FE1ISR				(Offset 0x0124) */

#define BIT_FS_RXDMA2_DONE_INT BIT(28)
#define BIT_FS_RXDONE3_INT BIT(27)
#define BIT_FS_RXDONE2_INT BIT(26)
#define BIT_FS_RX_BCN_P4_INT BIT(25)
#define BIT_FS_RX_BCN_P3_INT BIT(24)
#define BIT_FS_RX_BCN_P2_INT BIT(23)
#define BIT_FS_RX_BCN_P1_INT BIT(22)
#define BIT_FS_RX_BCN_P0_INT BIT(21)
#define BIT_FS_RX_UMD0_INT BIT(20)
#define BIT_FS_RX_UMD1_INT BIT(19)
#define BIT_FS_RX_BMD0_INT BIT(18)
#define BIT_FS_RX_BMD1_INT BIT(17)
#define BIT_FS_RXDONE_INT BIT(16)
#define BIT_FS_WWLAN_INT BIT(15)
#define BIT_FS_SOUND_DONE_INT BIT(14)
#define BIT_FS_LP_STBY_INT BIT(13)
#define BIT_FS_TRL_MTR_INT BIT(12)
#define BIT_FS_BF1_PRETO_INT BIT(11)
#define BIT_FS_BF0_PRETO_INT BIT(10)
#define BIT_FS_PTCL_RELEASE_MACID_INT BIT(9)

/* 2 REG_FE1ISR				(Offset 0x0124) */

#define BIT_FS_LTE_COEX_INT BIT(6)

/* 2 REG_FE1ISR				(Offset 0x0124) */

#define BIT_FS_WLACTOFF_INT BIT(5)
#define BIT_FS_WLACTON_INT BIT(4)
#define BIT_FS_BCN_RX_INT_INT BIT(3)

/* 2 REG_FE1ISR				(Offset 0x0124) */

#define BIT_FS_MAILBOX_TO_I2C_INT BIT(2)

/* 2 REG_FE1ISR				(Offset 0x0124) */

#define BIT_FS_TRPC_TO_INT BIT(1)

/* 2 REG_FE1ISR				(Offset 0x0124) */

#define BIT_FS_RPC_O_T_INT BIT(0)

/* 2 REG_CPWM				(Offset 0x012C) */

#define BIT_CPWM_TOGGLING BIT(31)

#define BIT_SHIFT_CPWM_MOD 24
#define BIT_MASK_CPWM_MOD 0x7f
#define BIT_CPWM_MOD(x) (((x) & BIT_MASK_CPWM_MOD) << BIT_SHIFT_CPWM_MOD)
#define BIT_GET_CPWM_MOD(x) (((x) >> BIT_SHIFT_CPWM_MOD) & BIT_MASK_CPWM_MOD)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_TXBCNOK_MB7_INT_EN BIT(31)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_TXBCNOK_MB6_INT_EN BIT(30)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_TXBCNOK_MB5_INT_EN BIT(29)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_TXBCNOK_MB4_INT_EN BIT(28)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_TXBCNOK_MB3_INT_EN BIT(27)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_TXBCNOK_MB2_INT_EN BIT(26)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_TXBCNOK_MB1_INT_EN BIT(25)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_TXBCNOK_MB0_INT_EN BIT(24)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_TXBCNERR_MB7_INT_EN BIT(23)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_TXBCNERR_MB6_INT_EN BIT(22)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_TXBCNERR_MB5_INT_EN BIT(21)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_TXBCNERR_MB4_INT_EN BIT(20)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_TXBCNERR_MB3_INT_EN BIT(19)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_TXBCNERR_MB2_INT_EN BIT(18)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_TXBCNERR_MB1_INT_EN BIT(17)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_TXBCNERR_MB0_INT_EN BIT(16)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_CPU_MGQ_TXDONE_INT_EN BIT(15)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_SIFS_OVERSPEC_INT_EN BIT(14)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_MGNTQ_RPTR_RELEASE_INT_EN BIT(13)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_MGNTQFF_TO_INT_EN BIT(12)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_DDMA1_LP_INT_EN BIT(11)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_DDMA1_HP_INT_EN BIT(10)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_DDMA0_LP_INT_EN BIT(9)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_DDMA0_HP_INT_EN BIT(8)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_TRXRPT_INT_EN BIT(7)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_C2H_W_READY_INT_EN BIT(6)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_HRCV_INT_EN BIT(5)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_H2CCMD_INT_EN BIT(4)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_TXPKTIN_INT_EN BIT(3)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_ERRORHDL_INT_EN BIT(2)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_TXCCX_INT_EN BIT(1)

/* 2 REG_FWIMR				(Offset 0x0130) */

#define BIT_FS_TXCLOSE_INT_EN BIT(0)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_TXBCNOK_MB7_INT BIT(31)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_TXBCNOK_MB6_INT BIT(30)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_TXBCNOK_MB5_INT BIT(29)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_TXBCNOK_MB4_INT BIT(28)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_TXBCNOK_MB3_INT BIT(27)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_TXBCNOK_MB2_INT BIT(26)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_TXBCNOK_MB1_INT BIT(25)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_TXBCNOK_MB0_INT BIT(24)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_TXBCNERR_MB7_INT BIT(23)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_TXBCNERR_MB6_INT BIT(22)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_TXBCNERR_MB5_INT BIT(21)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_TXBCNERR_MB4_INT BIT(20)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_TXBCNERR_MB3_INT BIT(19)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_TXBCNERR_MB2_INT BIT(18)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_TXBCNERR_MB1_INT BIT(17)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_TXBCNERR_MB0_INT BIT(16)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_CPU_MGQ_TXDONE_INT BIT(15)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_SIFS_OVERSPEC_INT BIT(14)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_MGNTQ_RPTR_RELEASE_INT BIT(13)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_MGNTQFF_TO_INT BIT(12)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_DDMA1_LP_INT BIT(11)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_DDMA1_HP_INT BIT(10)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_DDMA0_LP_INT BIT(9)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_DDMA0_HP_INT BIT(8)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_TRXRPT_INT BIT(7)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_C2H_W_READY_INT BIT(6)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_HRCV_INT BIT(5)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_H2CCMD_INT BIT(4)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_TXPKTIN_INT BIT(3)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_ERRORHDL_INT BIT(2)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_TXCCX_INT BIT(1)

/* 2 REG_FWISR				(Offset 0x0134) */

#define BIT_FS_TXCLOSE_INT BIT(0)

/* 2 REG_FTIMR				(Offset 0x0138) */

#define BIT_PS_TIMER_C_EARLY_INT_EN BIT(23)

/* 2 REG_FTIMR				(Offset 0x0138) */

#define BIT_PS_TIMER_B_EARLY_INT_EN BIT(22)

/* 2 REG_FTIMR				(Offset 0x0138) */

#define BIT_PS_TIMER_A_EARLY_INT_EN BIT(21)

/* 2 REG_FTIMR				(Offset 0x0138) */

#define BIT_CPUMGQ_TX_TIMER_EARLY_INT_EN BIT(20)

/* 2 REG_FTIMR				(Offset 0x0138) */

#define BIT_PS_TIMER_C_INT_EN BIT(19)

/* 2 REG_FTIMR				(Offset 0x0138) */

#define BIT_PS_TIMER_B_INT_EN BIT(18)

/* 2 REG_FTIMR				(Offset 0x0138) */

#define BIT_PS_TIMER_A_INT_EN BIT(17)

/* 2 REG_FTIMR				(Offset 0x0138) */

#define BIT_CPUMGQ_TX_TIMER_INT_EN BIT(16)

/* 2 REG_FTIMR				(Offset 0x0138) */

#define BIT_FS_PS_TIMEOUT2_EN BIT(15)

/* 2 REG_FTIMR				(Offset 0x0138) */

#define BIT_FS_PS_TIMEOUT1_EN BIT(14)

/* 2 REG_FTIMR				(Offset 0x0138) */

#define BIT_FS_PS_TIMEOUT0_EN BIT(13)

/* 2 REG_FTIMR				(Offset 0x0138) */

#define BIT_FS_GTINT8_EN BIT(8)

/* 2 REG_FTIMR				(Offset 0x0138) */

#define BIT_FS_GTINT7_EN BIT(7)

/* 2 REG_FTIMR				(Offset 0x0138) */

#define BIT_FS_GTINT6_EN BIT(6)

/* 2 REG_FTIMR				(Offset 0x0138) */

#define BIT_FS_GTINT5_EN BIT(5)

/* 2 REG_FTIMR				(Offset 0x0138) */

#define BIT_FS_GTINT4_EN BIT(4)

/* 2 REG_FTIMR				(Offset 0x0138) */

#define BIT_FS_GTINT3_EN BIT(3)

/* 2 REG_FTIMR				(Offset 0x0138) */

#define BIT_FS_GTINT2_EN BIT(2)

/* 2 REG_FTIMR				(Offset 0x0138) */

#define BIT_FS_GTINT1_EN BIT(1)

/* 2 REG_FTIMR				(Offset 0x0138) */

#define BIT_FS_GTINT0_EN BIT(0)

/* 2 REG_FTISR				(Offset 0x013C) */

#define BIT_PS_TIMER_C_EARLY__INT BIT(23)

/* 2 REG_FTISR				(Offset 0x013C) */

#define BIT_PS_TIMER_B_EARLY__INT BIT(22)

/* 2 REG_FTISR				(Offset 0x013C) */

#define BIT_PS_TIMER_A_EARLY__INT BIT(21)

/* 2 REG_FTISR				(Offset 0x013C) */

#define BIT_CPUMGQ_TX_TIMER_EARLY_INT BIT(20)

/* 2 REG_FTISR				(Offset 0x013C) */

#define BIT_PS_TIMER_C_INT BIT(19)

/* 2 REG_FTISR				(Offset 0x013C) */

#define BIT_PS_TIMER_B_INT BIT(18)

/* 2 REG_FTISR				(Offset 0x013C) */

#define BIT_PS_TIMER_A_INT BIT(17)

/* 2 REG_FTISR				(Offset 0x013C) */

#define BIT_CPUMGQ_TX_TIMER_INT BIT(16)

/* 2 REG_FTISR				(Offset 0x013C) */

#define BIT_FS_PS_TIMEOUT2_INT BIT(15)

/* 2 REG_FTISR				(Offset 0x013C) */

#define BIT_FS_PS_TIMEOUT1_INT BIT(14)

/* 2 REG_FTISR				(Offset 0x013C) */

#define BIT_FS_PS_TIMEOUT0_INT BIT(13)

/* 2 REG_FTISR				(Offset 0x013C) */

#define BIT_FS_GTINT8_INT BIT(8)

/* 2 REG_FTISR				(Offset 0x013C) */

#define BIT_FS_GTINT7_INT BIT(7)

/* 2 REG_FTISR				(Offset 0x013C) */

#define BIT_FS_GTINT6_INT BIT(6)

/* 2 REG_FTISR				(Offset 0x013C) */

#define BIT_FS_GTINT5_INT BIT(5)

/* 2 REG_FTISR				(Offset 0x013C) */

#define BIT_FS_GTINT4_INT BIT(4)

/* 2 REG_FTISR				(Offset 0x013C) */

#define BIT_FS_GTINT3_INT BIT(3)

/* 2 REG_FTISR				(Offset 0x013C) */

#define BIT_FS_GTINT2_INT BIT(2)

/* 2 REG_FTISR				(Offset 0x013C) */

#define BIT_FS_GTINT1_INT BIT(1)

/* 2 REG_FTISR				(Offset 0x013C) */

#define BIT_FS_GTINT0_INT BIT(0)

/* 2 REG_PKTBUF_DBG_CTRL			(Offset 0x0140) */

#define BIT_SHIFT_PKTBUF_WRITE_EN 24
#define BIT_MASK_PKTBUF_WRITE_EN 0xff
#define BIT_PKTBUF_WRITE_EN(x)                                                 \
	(((x) & BIT_MASK_PKTBUF_WRITE_EN) << BIT_SHIFT_PKTBUF_WRITE_EN)
#define BIT_GET_PKTBUF_WRITE_EN(x)                                             \
	(((x) >> BIT_SHIFT_PKTBUF_WRITE_EN) & BIT_MASK_PKTBUF_WRITE_EN)

/* 2 REG_PKTBUF_DBG_CTRL			(Offset 0x0140) */

#define BIT_TXRPTBUF_DBG BIT(23)

/* 2 REG_PKTBUF_DBG_CTRL			(Offset 0x0140) */

#define BIT_TXPKTBUF_DBG_V2 BIT(20)

/* 2 REG_PKTBUF_DBG_CTRL			(Offset 0x0140) */

#define BIT_RXPKTBUF_DBG BIT(16)

/* 2 REG_PKTBUF_DBG_CTRL			(Offset 0x0140) */

#define BIT_SHIFT_PKTBUF_DBG_ADDR 0
#define BIT_MASK_PKTBUF_DBG_ADDR 0x1fff
#define BIT_PKTBUF_DBG_ADDR(x)                                                 \
	(((x) & BIT_MASK_PKTBUF_DBG_ADDR) << BIT_SHIFT_PKTBUF_DBG_ADDR)
#define BIT_GET_PKTBUF_DBG_ADDR(x)                                             \
	(((x) >> BIT_SHIFT_PKTBUF_DBG_ADDR) & BIT_MASK_PKTBUF_DBG_ADDR)

/* 2 REG_PKTBUF_DBG_DATA_L			(Offset 0x0144) */

#define BIT_SHIFT_PKTBUF_DBG_DATA_L 0
#define BIT_MASK_PKTBUF_DBG_DATA_L 0xffffffffL
#define BIT_PKTBUF_DBG_DATA_L(x)                                               \
	(((x) & BIT_MASK_PKTBUF_DBG_DATA_L) << BIT_SHIFT_PKTBUF_DBG_DATA_L)
#define BIT_GET_PKTBUF_DBG_DATA_L(x)                                           \
	(((x) >> BIT_SHIFT_PKTBUF_DBG_DATA_L) & BIT_MASK_PKTBUF_DBG_DATA_L)

/* 2 REG_PKTBUF_DBG_DATA_H			(Offset 0x0148) */

#define BIT_SHIFT_PKTBUF_DBG_DATA_H 0
#define BIT_MASK_PKTBUF_DBG_DATA_H 0xffffffffL
#define BIT_PKTBUF_DBG_DATA_H(x)                                               \
	(((x) & BIT_MASK_PKTBUF_DBG_DATA_H) << BIT_SHIFT_PKTBUF_DBG_DATA_H)
#define BIT_GET_PKTBUF_DBG_DATA_H(x)                                           \
	(((x) >> BIT_SHIFT_PKTBUF_DBG_DATA_H) & BIT_MASK_PKTBUF_DBG_DATA_H)

/* 2 REG_CPWM2				(Offset 0x014C) */

#define BIT_SHIFT_L0S_TO_RCVY_NUM 16
#define BIT_MASK_L0S_TO_RCVY_NUM 0xff
#define BIT_L0S_TO_RCVY_NUM(x)                                                 \
	(((x) & BIT_MASK_L0S_TO_RCVY_NUM) << BIT_SHIFT_L0S_TO_RCVY_NUM)
#define BIT_GET_L0S_TO_RCVY_NUM(x)                                             \
	(((x) >> BIT_SHIFT_L0S_TO_RCVY_NUM) & BIT_MASK_L0S_TO_RCVY_NUM)

#define BIT_CPWM2_TOGGLING BIT(15)

#define BIT_SHIFT_CPWM2_MOD 0
#define BIT_MASK_CPWM2_MOD 0x7fff
#define BIT_CPWM2_MOD(x) (((x) & BIT_MASK_CPWM2_MOD) << BIT_SHIFT_CPWM2_MOD)
#define BIT_GET_CPWM2_MOD(x) (((x) >> BIT_SHIFT_CPWM2_MOD) & BIT_MASK_CPWM2_MOD)

/* 2 REG_TC0_CTRL				(Offset 0x0150) */

#define BIT_TC0INT_EN BIT(26)
#define BIT_TC0MODE BIT(25)
#define BIT_TC0EN BIT(24)

#define BIT_SHIFT_TC0DATA 0
#define BIT_MASK_TC0DATA 0xffffff
#define BIT_TC0DATA(x) (((x) & BIT_MASK_TC0DATA) << BIT_SHIFT_TC0DATA)
#define BIT_GET_TC0DATA(x) (((x) >> BIT_SHIFT_TC0DATA) & BIT_MASK_TC0DATA)

/* 2 REG_TC1_CTRL				(Offset 0x0154) */

#define BIT_TC1INT_EN BIT(26)
#define BIT_TC1MODE BIT(25)
#define BIT_TC1EN BIT(24)

#define BIT_SHIFT_TC1DATA 0
#define BIT_MASK_TC1DATA 0xffffff
#define BIT_TC1DATA(x) (((x) & BIT_MASK_TC1DATA) << BIT_SHIFT_TC1DATA)
#define BIT_GET_TC1DATA(x) (((x) >> BIT_SHIFT_TC1DATA) & BIT_MASK_TC1DATA)

/* 2 REG_TC2_CTRL				(Offset 0x0158) */

#define BIT_TC2INT_EN BIT(26)
#define BIT_TC2MODE BIT(25)
#define BIT_TC2EN BIT(24)

#define BIT_SHIFT_TC2DATA 0
#define BIT_MASK_TC2DATA 0xffffff
#define BIT_TC2DATA(x) (((x) & BIT_MASK_TC2DATA) << BIT_SHIFT_TC2DATA)
#define BIT_GET_TC2DATA(x) (((x) >> BIT_SHIFT_TC2DATA) & BIT_MASK_TC2DATA)

/* 2 REG_TC3_CTRL				(Offset 0x015C) */

#define BIT_TC3INT_EN BIT(26)
#define BIT_TC3MODE BIT(25)
#define BIT_TC3EN BIT(24)

#define BIT_SHIFT_TC3DATA 0
#define BIT_MASK_TC3DATA 0xffffff
#define BIT_TC3DATA(x) (((x) & BIT_MASK_TC3DATA) << BIT_SHIFT_TC3DATA)
#define BIT_GET_TC3DATA(x) (((x) >> BIT_SHIFT_TC3DATA) & BIT_MASK_TC3DATA)

/* 2 REG_TC4_CTRL				(Offset 0x0160) */

#define BIT_TC4INT_EN BIT(26)
#define BIT_TC4MODE BIT(25)
#define BIT_TC4EN BIT(24)

#define BIT_SHIFT_TC4DATA 0
#define BIT_MASK_TC4DATA 0xffffff
#define BIT_TC4DATA(x) (((x) & BIT_MASK_TC4DATA) << BIT_SHIFT_TC4DATA)
#define BIT_GET_TC4DATA(x) (((x) >> BIT_SHIFT_TC4DATA) & BIT_MASK_TC4DATA)

/* 2 REG_TCUNIT_BASE				(Offset 0x0164) */

#define BIT_SHIFT_TCUNIT_BASE 0
#define BIT_MASK_TCUNIT_BASE 0x3fff
#define BIT_TCUNIT_BASE(x)                                                     \
	(((x) & BIT_MASK_TCUNIT_BASE) << BIT_SHIFT_TCUNIT_BASE)
#define BIT_GET_TCUNIT_BASE(x)                                                 \
	(((x) >> BIT_SHIFT_TCUNIT_BASE) & BIT_MASK_TCUNIT_BASE)

/* 2 REG_TC5_CTRL				(Offset 0x0168) */

#define BIT_TC5INT_EN BIT(26)

/* 2 REG_TC5_CTRL				(Offset 0x0168) */

#define BIT_TC5MODE BIT(25)
#define BIT_TC5EN BIT(24)

#define BIT_SHIFT_TC5DATA 0
#define BIT_MASK_TC5DATA 0xffffff
#define BIT_TC5DATA(x) (((x) & BIT_MASK_TC5DATA) << BIT_SHIFT_TC5DATA)
#define BIT_GET_TC5DATA(x) (((x) >> BIT_SHIFT_TC5DATA) & BIT_MASK_TC5DATA)

/* 2 REG_TC6_CTRL				(Offset 0x016C) */

#define BIT_TC6INT_EN BIT(26)

/* 2 REG_TC6_CTRL				(Offset 0x016C) */

#define BIT_TC6MODE BIT(25)
#define BIT_TC6EN BIT(24)

#define BIT_SHIFT_TC6DATA 0
#define BIT_MASK_TC6DATA 0xffffff
#define BIT_TC6DATA(x) (((x) & BIT_MASK_TC6DATA) << BIT_SHIFT_TC6DATA)
#define BIT_GET_TC6DATA(x) (((x) >> BIT_SHIFT_TC6DATA) & BIT_MASK_TC6DATA)

/* 2 REG_MBIST_FAIL				(Offset 0x0170) */

#define BIT_SHIFT_8051_MBIST_FAIL 26
#define BIT_MASK_8051_MBIST_FAIL 0x7
#define BIT_8051_MBIST_FAIL(x)                                                 \
	(((x) & BIT_MASK_8051_MBIST_FAIL) << BIT_SHIFT_8051_MBIST_FAIL)
#define BIT_GET_8051_MBIST_FAIL(x)                                             \
	(((x) >> BIT_SHIFT_8051_MBIST_FAIL) & BIT_MASK_8051_MBIST_FAIL)

#define BIT_SHIFT_USB_MBIST_FAIL 24
#define BIT_MASK_USB_MBIST_FAIL 0x3
#define BIT_USB_MBIST_FAIL(x)                                                  \
	(((x) & BIT_MASK_USB_MBIST_FAIL) << BIT_SHIFT_USB_MBIST_FAIL)
#define BIT_GET_USB_MBIST_FAIL(x)                                              \
	(((x) >> BIT_SHIFT_USB_MBIST_FAIL) & BIT_MASK_USB_MBIST_FAIL)

#define BIT_SHIFT_PCIE_MBIST_FAIL 16
#define BIT_MASK_PCIE_MBIST_FAIL 0x3f
#define BIT_PCIE_MBIST_FAIL(x)                                                 \
	(((x) & BIT_MASK_PCIE_MBIST_FAIL) << BIT_SHIFT_PCIE_MBIST_FAIL)
#define BIT_GET_PCIE_MBIST_FAIL(x)                                             \
	(((x) >> BIT_SHIFT_PCIE_MBIST_FAIL) & BIT_MASK_PCIE_MBIST_FAIL)

/* 2 REG_MBIST_FAIL				(Offset 0x0170) */

#define BIT_SHIFT_MAC_MBIST_FAIL 0
#define BIT_MASK_MAC_MBIST_FAIL 0xfff
#define BIT_MAC_MBIST_FAIL(x)                                                  \
	(((x) & BIT_MASK_MAC_MBIST_FAIL) << BIT_SHIFT_MAC_MBIST_FAIL)
#define BIT_GET_MAC_MBIST_FAIL(x)                                              \
	(((x) >> BIT_SHIFT_MAC_MBIST_FAIL) & BIT_MASK_MAC_MBIST_FAIL)

/* 2 REG_MBIST_START_PAUSE			(Offset 0x0174) */

#define BIT_SHIFT_8051_MBIST_START_PAUSE 26
#define BIT_MASK_8051_MBIST_START_PAUSE 0x7
#define BIT_8051_MBIST_START_PAUSE(x)                                          \
	(((x) & BIT_MASK_8051_MBIST_START_PAUSE)                               \
	 << BIT_SHIFT_8051_MBIST_START_PAUSE)
#define BIT_GET_8051_MBIST_START_PAUSE(x)                                      \
	(((x) >> BIT_SHIFT_8051_MBIST_START_PAUSE) &                           \
	 BIT_MASK_8051_MBIST_START_PAUSE)

#define BIT_SHIFT_USB_MBIST_START_PAUSE 24
#define BIT_MASK_USB_MBIST_START_PAUSE 0x3
#define BIT_USB_MBIST_START_PAUSE(x)                                           \
	(((x) & BIT_MASK_USB_MBIST_START_PAUSE)                                \
	 << BIT_SHIFT_USB_MBIST_START_PAUSE)
#define BIT_GET_USB_MBIST_START_PAUSE(x)                                       \
	(((x) >> BIT_SHIFT_USB_MBIST_START_PAUSE) &                            \
	 BIT_MASK_USB_MBIST_START_PAUSE)

#define BIT_SHIFT_PCIE_MBIST_START_PAUSE 16
#define BIT_MASK_PCIE_MBIST_START_PAUSE 0x3f
#define BIT_PCIE_MBIST_START_PAUSE(x)                                          \
	(((x) & BIT_MASK_PCIE_MBIST_START_PAUSE)                               \
	 << BIT_SHIFT_PCIE_MBIST_START_PAUSE)
#define BIT_GET_PCIE_MBIST_START_PAUSE(x)                                      \
	(((x) >> BIT_SHIFT_PCIE_MBIST_START_PAUSE) &                           \
	 BIT_MASK_PCIE_MBIST_START_PAUSE)

/* 2 REG_MBIST_START_PAUSE			(Offset 0x0174) */

#define BIT_SHIFT_MAC_MBIST_START_PAUSE 0
#define BIT_MASK_MAC_MBIST_START_PAUSE 0xfff
#define BIT_MAC_MBIST_START_PAUSE(x)                                           \
	(((x) & BIT_MASK_MAC_MBIST_START_PAUSE)                                \
	 << BIT_SHIFT_MAC_MBIST_START_PAUSE)
#define BIT_GET_MAC_MBIST_START_PAUSE(x)                                       \
	(((x) >> BIT_SHIFT_MAC_MBIST_START_PAUSE) &                            \
	 BIT_MASK_MAC_MBIST_START_PAUSE)

/* 2 REG_MBIST_DONE				(Offset 0x0178) */

#define BIT_SHIFT_8051_MBIST_DONE 26
#define BIT_MASK_8051_MBIST_DONE 0x7
#define BIT_8051_MBIST_DONE(x)                                                 \
	(((x) & BIT_MASK_8051_MBIST_DONE) << BIT_SHIFT_8051_MBIST_DONE)
#define BIT_GET_8051_MBIST_DONE(x)                                             \
	(((x) >> BIT_SHIFT_8051_MBIST_DONE) & BIT_MASK_8051_MBIST_DONE)

#define BIT_SHIFT_USB_MBIST_DONE 24
#define BIT_MASK_USB_MBIST_DONE 0x3
#define BIT_USB_MBIST_DONE(x)                                                  \
	(((x) & BIT_MASK_USB_MBIST_DONE) << BIT_SHIFT_USB_MBIST_DONE)
#define BIT_GET_USB_MBIST_DONE(x)                                              \
	(((x) >> BIT_SHIFT_USB_MBIST_DONE) & BIT_MASK_USB_MBIST_DONE)

#define BIT_SHIFT_PCIE_MBIST_DONE 16
#define BIT_MASK_PCIE_MBIST_DONE 0x3f
#define BIT_PCIE_MBIST_DONE(x)                                                 \
	(((x) & BIT_MASK_PCIE_MBIST_DONE) << BIT_SHIFT_PCIE_MBIST_DONE)
#define BIT_GET_PCIE_MBIST_DONE(x)                                             \
	(((x) >> BIT_SHIFT_PCIE_MBIST_DONE) & BIT_MASK_PCIE_MBIST_DONE)

/* 2 REG_MBIST_DONE				(Offset 0x0178) */

#define BIT_SHIFT_MAC_MBIST_DONE 0
#define BIT_MASK_MAC_MBIST_DONE 0xfff
#define BIT_MAC_MBIST_DONE(x)                                                  \
	(((x) & BIT_MASK_MAC_MBIST_DONE) << BIT_SHIFT_MAC_MBIST_DONE)
#define BIT_GET_MAC_MBIST_DONE(x)                                              \
	(((x) >> BIT_SHIFT_MAC_MBIST_DONE) & BIT_MASK_MAC_MBIST_DONE)

/* 2 REG_MBIST_FAIL_NRML			(Offset 0x017C) */

#define BIT_SHIFT_MBIST_FAIL_NRML 0
#define BIT_MASK_MBIST_FAIL_NRML 0xffffffffL
#define BIT_MBIST_FAIL_NRML(x)                                                 \
	(((x) & BIT_MASK_MBIST_FAIL_NRML) << BIT_SHIFT_MBIST_FAIL_NRML)
#define BIT_GET_MBIST_FAIL_NRML(x)                                             \
	(((x) >> BIT_SHIFT_MBIST_FAIL_NRML) & BIT_MASK_MBIST_FAIL_NRML)

/* 2 REG_AES_DECRPT_DATA			(Offset 0x0180) */

#define BIT_SHIFT_IPS_CFG_ADDR 0
#define BIT_MASK_IPS_CFG_ADDR 0xff
#define BIT_IPS_CFG_ADDR(x)                                                    \
	(((x) & BIT_MASK_IPS_CFG_ADDR) << BIT_SHIFT_IPS_CFG_ADDR)
#define BIT_GET_IPS_CFG_ADDR(x)                                                \
	(((x) >> BIT_SHIFT_IPS_CFG_ADDR) & BIT_MASK_IPS_CFG_ADDR)

/* 2 REG_AES_DECRPT_CFG			(Offset 0x0184) */

#define BIT_SHIFT_IPS_CFG_DATA 0
#define BIT_MASK_IPS_CFG_DATA 0xffffffffL
#define BIT_IPS_CFG_DATA(x)                                                    \
	(((x) & BIT_MASK_IPS_CFG_DATA) << BIT_SHIFT_IPS_CFG_DATA)
#define BIT_GET_IPS_CFG_DATA(x)                                                \
	(((x) >> BIT_SHIFT_IPS_CFG_DATA) & BIT_MASK_IPS_CFG_DATA)

/* 2 REG_TMETER				(Offset 0x0190) */

#define BIT_TEMP_VALID BIT(31)

#define BIT_SHIFT_TEMP_VALUE 24
#define BIT_MASK_TEMP_VALUE 0x3f
#define BIT_TEMP_VALUE(x) (((x) & BIT_MASK_TEMP_VALUE) << BIT_SHIFT_TEMP_VALUE)
#define BIT_GET_TEMP_VALUE(x)                                                  \
	(((x) >> BIT_SHIFT_TEMP_VALUE) & BIT_MASK_TEMP_VALUE)

#define BIT_SHIFT_REG_TMETER_TIMER 8
#define BIT_MASK_REG_TMETER_TIMER 0xfff
#define BIT_REG_TMETER_TIMER(x)                                                \
	(((x) & BIT_MASK_REG_TMETER_TIMER) << BIT_SHIFT_REG_TMETER_TIMER)
#define BIT_GET_REG_TMETER_TIMER(x)                                            \
	(((x) >> BIT_SHIFT_REG_TMETER_TIMER) & BIT_MASK_REG_TMETER_TIMER)

#define BIT_SHIFT_REG_TEMP_DELTA 2
#define BIT_MASK_REG_TEMP_DELTA 0x3f
#define BIT_REG_TEMP_DELTA(x)                                                  \
	(((x) & BIT_MASK_REG_TEMP_DELTA) << BIT_SHIFT_REG_TEMP_DELTA)
#define BIT_GET_REG_TEMP_DELTA(x)                                              \
	(((x) >> BIT_SHIFT_REG_TEMP_DELTA) & BIT_MASK_REG_TEMP_DELTA)

#define BIT_REG_TMETER_EN BIT(0)

/* 2 REG_OSC_32K_CTRL			(Offset 0x0194) */

#define BIT_SHIFT_OSC_32K_CLKGEN_0 16
#define BIT_MASK_OSC_32K_CLKGEN_0 0xffff
#define BIT_OSC_32K_CLKGEN_0(x)                                                \
	(((x) & BIT_MASK_OSC_32K_CLKGEN_0) << BIT_SHIFT_OSC_32K_CLKGEN_0)
#define BIT_GET_OSC_32K_CLKGEN_0(x)                                            \
	(((x) >> BIT_SHIFT_OSC_32K_CLKGEN_0) & BIT_MASK_OSC_32K_CLKGEN_0)

/* 2 REG_OSC_32K_CTRL			(Offset 0x0194) */

#define BIT_SHIFT_OSC_32K_RES_COMP 4
#define BIT_MASK_OSC_32K_RES_COMP 0x3
#define BIT_OSC_32K_RES_COMP(x)                                                \
	(((x) & BIT_MASK_OSC_32K_RES_COMP) << BIT_SHIFT_OSC_32K_RES_COMP)
#define BIT_GET_OSC_32K_RES_COMP(x)                                            \
	(((x) >> BIT_SHIFT_OSC_32K_RES_COMP) & BIT_MASK_OSC_32K_RES_COMP)

#define BIT_OSC_32K_OUT_SEL BIT(3)

/* 2 REG_OSC_32K_CTRL			(Offset 0x0194) */

#define BIT_ISO_WL_2_OSC_32K BIT(1)

/* 2 REG_OSC_32K_CTRL			(Offset 0x0194) */

#define BIT_POW_CKGEN BIT(0)

/* 2 REG_32K_CAL_REG1			(Offset 0x0198) */

#define BIT_CAL_32K_REG_WR BIT(31)
#define BIT_CAL_32K_DBG_SEL BIT(22)

#define BIT_SHIFT_CAL_32K_REG_ADDR 16
#define BIT_MASK_CAL_32K_REG_ADDR 0x3f
#define BIT_CAL_32K_REG_ADDR(x)                                                \
	(((x) & BIT_MASK_CAL_32K_REG_ADDR) << BIT_SHIFT_CAL_32K_REG_ADDR)
#define BIT_GET_CAL_32K_REG_ADDR(x)                                            \
	(((x) >> BIT_SHIFT_CAL_32K_REG_ADDR) & BIT_MASK_CAL_32K_REG_ADDR)

/* 2 REG_32K_CAL_REG1			(Offset 0x0198) */

#define BIT_SHIFT_CAL_32K_REG_DATA 0
#define BIT_MASK_CAL_32K_REG_DATA 0xffff
#define BIT_CAL_32K_REG_DATA(x)                                                \
	(((x) & BIT_MASK_CAL_32K_REG_DATA) << BIT_SHIFT_CAL_32K_REG_DATA)
#define BIT_GET_CAL_32K_REG_DATA(x)                                            \
	(((x) >> BIT_SHIFT_CAL_32K_REG_DATA) & BIT_MASK_CAL_32K_REG_DATA)

/* 2 REG_C2HEVT				(Offset 0x01A0) */

#define BIT_SHIFT_C2HEVT_MSG 0
#define BIT_MASK_C2HEVT_MSG 0xffffffffffffffffffffffffffffffffL
#define BIT_C2HEVT_MSG(x) (((x) & BIT_MASK_C2HEVT_MSG) << BIT_SHIFT_C2HEVT_MSG)
#define BIT_GET_C2HEVT_MSG(x)                                                  \
	(((x) >> BIT_SHIFT_C2HEVT_MSG) & BIT_MASK_C2HEVT_MSG)

/* 2 REG_SW_DEFINED_PAGE1			(Offset 0x01B8) */

#define BIT_SHIFT_SW_DEFINED_PAGE1 0
#define BIT_MASK_SW_DEFINED_PAGE1 0xffffffffffffffffL
#define BIT_SW_DEFINED_PAGE1(x)                                                \
	(((x) & BIT_MASK_SW_DEFINED_PAGE1) << BIT_SHIFT_SW_DEFINED_PAGE1)
#define BIT_GET_SW_DEFINED_PAGE1(x)                                            \
	(((x) >> BIT_SHIFT_SW_DEFINED_PAGE1) & BIT_MASK_SW_DEFINED_PAGE1)

/* 2 REG_MCUTST_I				(Offset 0x01C0) */

#define BIT_SHIFT_MCUDMSG_I 0
#define BIT_MASK_MCUDMSG_I 0xffffffffL
#define BIT_MCUDMSG_I(x) (((x) & BIT_MASK_MCUDMSG_I) << BIT_SHIFT_MCUDMSG_I)
#define BIT_GET_MCUDMSG_I(x) (((x) >> BIT_SHIFT_MCUDMSG_I) & BIT_MASK_MCUDMSG_I)

/* 2 REG_MCUTST_II				(Offset 0x01C4) */

#define BIT_SHIFT_MCUDMSG_II 0
#define BIT_MASK_MCUDMSG_II 0xffffffffL
#define BIT_MCUDMSG_II(x) (((x) & BIT_MASK_MCUDMSG_II) << BIT_SHIFT_MCUDMSG_II)
#define BIT_GET_MCUDMSG_II(x)                                                  \
	(((x) >> BIT_SHIFT_MCUDMSG_II) & BIT_MASK_MCUDMSG_II)

/* 2 REG_FMETHR				(Offset 0x01C8) */

#define BIT_FMSG_INT BIT(31)

#define BIT_SHIFT_FW_MSG 0
#define BIT_MASK_FW_MSG 0xffffffffL
#define BIT_FW_MSG(x) (((x) & BIT_MASK_FW_MSG) << BIT_SHIFT_FW_MSG)
#define BIT_GET_FW_MSG(x) (((x) >> BIT_SHIFT_FW_MSG) & BIT_MASK_FW_MSG)

/* 2 REG_HMETFR				(Offset 0x01CC) */

#define BIT_SHIFT_HRCV_MSG 24
#define BIT_MASK_HRCV_MSG 0xff
#define BIT_HRCV_MSG(x) (((x) & BIT_MASK_HRCV_MSG) << BIT_SHIFT_HRCV_MSG)
#define BIT_GET_HRCV_MSG(x) (((x) >> BIT_SHIFT_HRCV_MSG) & BIT_MASK_HRCV_MSG)

#define BIT_INT_BOX3 BIT(3)
#define BIT_INT_BOX2 BIT(2)
#define BIT_INT_BOX1 BIT(1)
#define BIT_INT_BOX0 BIT(0)

/* 2 REG_HMEBOX0				(Offset 0x01D0) */

#define BIT_SHIFT_HOST_MSG_0 0
#define BIT_MASK_HOST_MSG_0 0xffffffffL
#define BIT_HOST_MSG_0(x) (((x) & BIT_MASK_HOST_MSG_0) << BIT_SHIFT_HOST_MSG_0)
#define BIT_GET_HOST_MSG_0(x)                                                  \
	(((x) >> BIT_SHIFT_HOST_MSG_0) & BIT_MASK_HOST_MSG_0)

/* 2 REG_HMEBOX1				(Offset 0x01D4) */

#define BIT_SHIFT_HOST_MSG_1 0
#define BIT_MASK_HOST_MSG_1 0xffffffffL
#define BIT_HOST_MSG_1(x) (((x) & BIT_MASK_HOST_MSG_1) << BIT_SHIFT_HOST_MSG_1)
#define BIT_GET_HOST_MSG_1(x)                                                  \
	(((x) >> BIT_SHIFT_HOST_MSG_1) & BIT_MASK_HOST_MSG_1)

/* 2 REG_HMEBOX2				(Offset 0x01D8) */

#define BIT_SHIFT_HOST_MSG_2 0
#define BIT_MASK_HOST_MSG_2 0xffffffffL
#define BIT_HOST_MSG_2(x) (((x) & BIT_MASK_HOST_MSG_2) << BIT_SHIFT_HOST_MSG_2)
#define BIT_GET_HOST_MSG_2(x)                                                  \
	(((x) >> BIT_SHIFT_HOST_MSG_2) & BIT_MASK_HOST_MSG_2)

/* 2 REG_HMEBOX3				(Offset 0x01DC) */

#define BIT_SHIFT_HOST_MSG_3 0
#define BIT_MASK_HOST_MSG_3 0xffffffffL
#define BIT_HOST_MSG_3(x) (((x) & BIT_MASK_HOST_MSG_3) << BIT_SHIFT_HOST_MSG_3)
#define BIT_GET_HOST_MSG_3(x)                                                  \
	(((x) >> BIT_SHIFT_HOST_MSG_3) & BIT_MASK_HOST_MSG_3)

/* 2 REG_LLT_INIT				(Offset 0x01E0) */

#define BIT_SHIFT_LLTE_RWM 30
#define BIT_MASK_LLTE_RWM 0x3
#define BIT_LLTE_RWM(x) (((x) & BIT_MASK_LLTE_RWM) << BIT_SHIFT_LLTE_RWM)
#define BIT_GET_LLTE_RWM(x) (((x) >> BIT_SHIFT_LLTE_RWM) & BIT_MASK_LLTE_RWM)

/* 2 REG_LLT_INIT				(Offset 0x01E0) */

#define BIT_SHIFT_LLTINI_PDATA_V1 16
#define BIT_MASK_LLTINI_PDATA_V1 0xfff
#define BIT_LLTINI_PDATA_V1(x)                                                 \
	(((x) & BIT_MASK_LLTINI_PDATA_V1) << BIT_SHIFT_LLTINI_PDATA_V1)
#define BIT_GET_LLTINI_PDATA_V1(x)                                             \
	(((x) >> BIT_SHIFT_LLTINI_PDATA_V1) & BIT_MASK_LLTINI_PDATA_V1)

/* 2 REG_LLT_INIT				(Offset 0x01E0) */

#define BIT_SHIFT_LLTINI_HDATA_V1 0
#define BIT_MASK_LLTINI_HDATA_V1 0xfff
#define BIT_LLTINI_HDATA_V1(x)                                                 \
	(((x) & BIT_MASK_LLTINI_HDATA_V1) << BIT_SHIFT_LLTINI_HDATA_V1)
#define BIT_GET_LLTINI_HDATA_V1(x)                                             \
	(((x) >> BIT_SHIFT_LLTINI_HDATA_V1) & BIT_MASK_LLTINI_HDATA_V1)

/* 2 REG_LLT_INIT_ADDR			(Offset 0x01E4) */

#define BIT_SHIFT_LLTINI_ADDR_V1 0
#define BIT_MASK_LLTINI_ADDR_V1 0xfff
#define BIT_LLTINI_ADDR_V1(x)                                                  \
	(((x) & BIT_MASK_LLTINI_ADDR_V1) << BIT_SHIFT_LLTINI_ADDR_V1)
#define BIT_GET_LLTINI_ADDR_V1(x)                                              \
	(((x) >> BIT_SHIFT_LLTINI_ADDR_V1) & BIT_MASK_LLTINI_ADDR_V1)

/* 2 REG_BB_ACCESS_CTRL			(Offset 0x01E8) */

#define BIT_SHIFT_BB_WRITE_READ 30
#define BIT_MASK_BB_WRITE_READ 0x3
#define BIT_BB_WRITE_READ(x)                                                   \
	(((x) & BIT_MASK_BB_WRITE_READ) << BIT_SHIFT_BB_WRITE_READ)
#define BIT_GET_BB_WRITE_READ(x)                                               \
	(((x) >> BIT_SHIFT_BB_WRITE_READ) & BIT_MASK_BB_WRITE_READ)

/* 2 REG_BB_ACCESS_CTRL			(Offset 0x01E8) */

#define BIT_SHIFT_BB_WRITE_EN 12
#define BIT_MASK_BB_WRITE_EN 0xf
#define BIT_BB_WRITE_EN(x)                                                     \
	(((x) & BIT_MASK_BB_WRITE_EN) << BIT_SHIFT_BB_WRITE_EN)
#define BIT_GET_BB_WRITE_EN(x)                                                 \
	(((x) >> BIT_SHIFT_BB_WRITE_EN) & BIT_MASK_BB_WRITE_EN)

#define BIT_SHIFT_BB_ADDR 2
#define BIT_MASK_BB_ADDR 0x1ff
#define BIT_BB_ADDR(x) (((x) & BIT_MASK_BB_ADDR) << BIT_SHIFT_BB_ADDR)
#define BIT_GET_BB_ADDR(x) (((x) >> BIT_SHIFT_BB_ADDR) & BIT_MASK_BB_ADDR)

/* 2 REG_BB_ACCESS_CTRL			(Offset 0x01E8) */

#define BIT_BB_ERRACC BIT(0)

/* 2 REG_BB_ACCESS_DATA			(Offset 0x01EC) */

#define BIT_SHIFT_BB_DATA 0
#define BIT_MASK_BB_DATA 0xffffffffL
#define BIT_BB_DATA(x) (((x) & BIT_MASK_BB_DATA) << BIT_SHIFT_BB_DATA)
#define BIT_GET_BB_DATA(x) (((x) >> BIT_SHIFT_BB_DATA) & BIT_MASK_BB_DATA)

/* 2 REG_HMEBOX_E0				(Offset 0x01F0) */

#define BIT_SHIFT_HMEBOX_E0 0
#define BIT_MASK_HMEBOX_E0 0xffffffffL
#define BIT_HMEBOX_E0(x) (((x) & BIT_MASK_HMEBOX_E0) << BIT_SHIFT_HMEBOX_E0)
#define BIT_GET_HMEBOX_E0(x) (((x) >> BIT_SHIFT_HMEBOX_E0) & BIT_MASK_HMEBOX_E0)

/* 2 REG_HMEBOX_E1				(Offset 0x01F4) */

#define BIT_SHIFT_HMEBOX_E1 0
#define BIT_MASK_HMEBOX_E1 0xffffffffL
#define BIT_HMEBOX_E1(x) (((x) & BIT_MASK_HMEBOX_E1) << BIT_SHIFT_HMEBOX_E1)
#define BIT_GET_HMEBOX_E1(x) (((x) >> BIT_SHIFT_HMEBOX_E1) & BIT_MASK_HMEBOX_E1)

/* 2 REG_HMEBOX_E2				(Offset 0x01F8) */

#define BIT_SHIFT_HMEBOX_E2 0
#define BIT_MASK_HMEBOX_E2 0xffffffffL
#define BIT_HMEBOX_E2(x) (((x) & BIT_MASK_HMEBOX_E2) << BIT_SHIFT_HMEBOX_E2)
#define BIT_GET_HMEBOX_E2(x) (((x) >> BIT_SHIFT_HMEBOX_E2) & BIT_MASK_HMEBOX_E2)

/* 2 REG_HMEBOX_E3				(Offset 0x01FC) */

#define BIT_LD_RQPN BIT(31)

#define BIT_SHIFT_HMEBOX_E3 0
#define BIT_MASK_HMEBOX_E3 0xffffffffL
#define BIT_HMEBOX_E3(x) (((x) & BIT_MASK_HMEBOX_E3) << BIT_SHIFT_HMEBOX_E3)
#define BIT_GET_HMEBOX_E3(x) (((x) >> BIT_SHIFT_HMEBOX_E3) & BIT_MASK_HMEBOX_E3)

/* 2 REG_FIFOPAGE_CTRL_1			(Offset 0x0200) */

#define BIT_SHIFT_TX_OQT_HE_FREE_SPACE_V1 16
#define BIT_MASK_TX_OQT_HE_FREE_SPACE_V1 0xff
#define BIT_TX_OQT_HE_FREE_SPACE_V1(x)                                         \
	(((x) & BIT_MASK_TX_OQT_HE_FREE_SPACE_V1)                              \
	 << BIT_SHIFT_TX_OQT_HE_FREE_SPACE_V1)
#define BIT_GET_TX_OQT_HE_FREE_SPACE_V1(x)                                     \
	(((x) >> BIT_SHIFT_TX_OQT_HE_FREE_SPACE_V1) &                          \
	 BIT_MASK_TX_OQT_HE_FREE_SPACE_V1)

/* 2 REG_FIFOPAGE_CTRL_1			(Offset 0x0200) */

#define BIT_SHIFT_TX_OQT_NL_FREE_SPACE_V1 0
#define BIT_MASK_TX_OQT_NL_FREE_SPACE_V1 0xff
#define BIT_TX_OQT_NL_FREE_SPACE_V1(x)                                         \
	(((x) & BIT_MASK_TX_OQT_NL_FREE_SPACE_V1)                              \
	 << BIT_SHIFT_TX_OQT_NL_FREE_SPACE_V1)
#define BIT_GET_TX_OQT_NL_FREE_SPACE_V1(x)                                     \
	(((x) >> BIT_SHIFT_TX_OQT_NL_FREE_SPACE_V1) &                          \
	 BIT_MASK_TX_OQT_NL_FREE_SPACE_V1)

/* 2 REG_FIFOPAGE_CTRL_2			(Offset 0x0204) */

#define BIT_BCN_VALID_1_V1 BIT(31)

/* 2 REG_FIFOPAGE_CTRL_2			(Offset 0x0204) */

#define BIT_SHIFT_BCN_HEAD_1_V1 16
#define BIT_MASK_BCN_HEAD_1_V1 0xfff
#define BIT_BCN_HEAD_1_V1(x)                                                   \
	(((x) & BIT_MASK_BCN_HEAD_1_V1) << BIT_SHIFT_BCN_HEAD_1_V1)
#define BIT_GET_BCN_HEAD_1_V1(x)                                               \
	(((x) >> BIT_SHIFT_BCN_HEAD_1_V1) & BIT_MASK_BCN_HEAD_1_V1)

#define BIT_BCN_VALID_V1 BIT(15)

/* 2 REG_FIFOPAGE_CTRL_2			(Offset 0x0204) */

#define BIT_SHIFT_BCN_HEAD_V1 0
#define BIT_MASK_BCN_HEAD_V1 0xfff
#define BIT_BCN_HEAD_V1(x)                                                     \
	(((x) & BIT_MASK_BCN_HEAD_V1) << BIT_SHIFT_BCN_HEAD_V1)
#define BIT_GET_BCN_HEAD_V1(x)                                                 \
	(((x) >> BIT_SHIFT_BCN_HEAD_V1) & BIT_MASK_BCN_HEAD_V1)

/* 2 REG_AUTO_LLT_V1				(Offset 0x0208) */

#define BIT_SHIFT_MAX_TX_PKT_FOR_USB_AND_SDIO_V1 24
#define BIT_MASK_MAX_TX_PKT_FOR_USB_AND_SDIO_V1 0xff
#define BIT_MAX_TX_PKT_FOR_USB_AND_SDIO_V1(x)                                  \
	(((x) & BIT_MASK_MAX_TX_PKT_FOR_USB_AND_SDIO_V1)                       \
	 << BIT_SHIFT_MAX_TX_PKT_FOR_USB_AND_SDIO_V1)
#define BIT_GET_MAX_TX_PKT_FOR_USB_AND_SDIO_V1(x)                              \
	(((x) >> BIT_SHIFT_MAX_TX_PKT_FOR_USB_AND_SDIO_V1) &                   \
	 BIT_MASK_MAX_TX_PKT_FOR_USB_AND_SDIO_V1)

/* 2 REG_AUTO_LLT_V1				(Offset 0x0208) */

#define BIT_SHIFT_LLT_FREE_PAGE_V1 8
#define BIT_MASK_LLT_FREE_PAGE_V1 0xffff
#define BIT_LLT_FREE_PAGE_V1(x)                                                \
	(((x) & BIT_MASK_LLT_FREE_PAGE_V1) << BIT_SHIFT_LLT_FREE_PAGE_V1)
#define BIT_GET_LLT_FREE_PAGE_V1(x)                                            \
	(((x) >> BIT_SHIFT_LLT_FREE_PAGE_V1) & BIT_MASK_LLT_FREE_PAGE_V1)

/* 2 REG_DWBCN0_CTRL				(Offset 0x0208) */

#define BIT_SHIFT_BLK_DESC_NUM 4
#define BIT_MASK_BLK_DESC_NUM 0xf
#define BIT_BLK_DESC_NUM(x)                                                    \
	(((x) & BIT_MASK_BLK_DESC_NUM) << BIT_SHIFT_BLK_DESC_NUM)
#define BIT_GET_BLK_DESC_NUM(x)                                                \
	(((x) >> BIT_SHIFT_BLK_DESC_NUM) & BIT_MASK_BLK_DESC_NUM)

/* 2 REG_AUTO_LLT_V1				(Offset 0x0208) */

#define BIT_R_BCN_HEAD_SEL BIT(3)
#define BIT_R_EN_BCN_SW_HEAD_SEL BIT(2)
#define BIT_LLT_DBG_SEL BIT(1)
#define BIT_AUTO_INIT_LLT_V1 BIT(0)

/* 2 REG_TXDMA_OFFSET_CHK			(Offset 0x020C) */

#define BIT_EM_CHKSUM_FIN BIT(31)
#define BIT_EMN_PCIE_DMA_MOD BIT(30)

/* 2 REG_TXDMA_OFFSET_CHK			(Offset 0x020C) */

#define BIT_EN_TXQUE_CLR BIT(29)
#define BIT_EN_PCIE_FIFO_MODE BIT(28)

/* 2 REG_TXDMA_OFFSET_CHK			(Offset 0x020C) */

#define BIT_SHIFT_PG_UNDER_TH_V1 16
#define BIT_MASK_PG_UNDER_TH_V1 0xfff
#define BIT_PG_UNDER_TH_V1(x)                                                  \
	(((x) & BIT_MASK_PG_UNDER_TH_V1) << BIT_SHIFT_PG_UNDER_TH_V1)
#define BIT_GET_PG_UNDER_TH_V1(x)                                              \
	(((x) >> BIT_SHIFT_PG_UNDER_TH_V1) & BIT_MASK_PG_UNDER_TH_V1)

/* 2 REG_TXDMA_OFFSET_CHK			(Offset 0x020C) */

#define BIT_RESTORE_H2C_ADDRESS BIT(15)

/* 2 REG_TXDMA_OFFSET_CHK			(Offset 0x020C) */

#define BIT_SDIO_TXDESC_CHKSUM_EN BIT(13)
#define BIT_RST_RDPTR BIT(12)
#define BIT_RST_WRPTR BIT(11)
#define BIT_CHK_PG_TH_EN BIT(10)
#define BIT_DROP_DATA_EN BIT(9)
#define BIT_CHECK_OFFSET_EN BIT(8)

#define BIT_SHIFT_CHECK_OFFSET 0
#define BIT_MASK_CHECK_OFFSET 0xff
#define BIT_CHECK_OFFSET(x)                                                    \
	(((x) & BIT_MASK_CHECK_OFFSET) << BIT_SHIFT_CHECK_OFFSET)
#define BIT_GET_CHECK_OFFSET(x)                                                \
	(((x) >> BIT_SHIFT_CHECK_OFFSET) & BIT_MASK_CHECK_OFFSET)

/* 2 REG_TXDMA_STATUS			(Offset 0x0210) */

#define BIT_HI_OQT_UDN BIT(17)
#define BIT_HI_OQT_OVF BIT(16)
#define BIT_PAYLOAD_CHKSUM_ERR BIT(15)
#define BIT_PAYLOAD_UDN BIT(14)
#define BIT_PAYLOAD_OVF BIT(13)
#define BIT_DSC_CHKSUM_FAIL BIT(12)
#define BIT_UNKNOWN_QSEL BIT(11)
#define BIT_EP_QSEL_DIFF BIT(10)
#define BIT_TX_OFFS_UNMATCH BIT(9)
#define BIT_TXOQT_UDN BIT(8)
#define BIT_TXOQT_OVF BIT(7)
#define BIT_TXDMA_SFF_UDN BIT(6)
#define BIT_TXDMA_SFF_OVF BIT(5)
#define BIT_LLT_NULL_PG BIT(4)
#define BIT_PAGE_UDN BIT(3)
#define BIT_PAGE_OVF BIT(2)
#define BIT_TXFF_PG_UDN BIT(1)
#define BIT_TXFF_PG_OVF BIT(0)

/* 2 REG_TQPNT1				(Offset 0x0218) */

#define BIT_SHIFT_HPQ_HIGH_TH_V1 16
#define BIT_MASK_HPQ_HIGH_TH_V1 0xfff
#define BIT_HPQ_HIGH_TH_V1(x)                                                  \
	(((x) & BIT_MASK_HPQ_HIGH_TH_V1) << BIT_SHIFT_HPQ_HIGH_TH_V1)
#define BIT_GET_HPQ_HIGH_TH_V1(x)                                              \
	(((x) >> BIT_SHIFT_HPQ_HIGH_TH_V1) & BIT_MASK_HPQ_HIGH_TH_V1)

/* 2 REG_TQPNT1				(Offset 0x0218) */

#define BIT_SHIFT_HPQ_LOW_TH_V1 0
#define BIT_MASK_HPQ_LOW_TH_V1 0xfff
#define BIT_HPQ_LOW_TH_V1(x)                                                   \
	(((x) & BIT_MASK_HPQ_LOW_TH_V1) << BIT_SHIFT_HPQ_LOW_TH_V1)
#define BIT_GET_HPQ_LOW_TH_V1(x)                                               \
	(((x) >> BIT_SHIFT_HPQ_LOW_TH_V1) & BIT_MASK_HPQ_LOW_TH_V1)

/* 2 REG_TQPNT2				(Offset 0x021C) */

#define BIT_SHIFT_NPQ_HIGH_TH_V1 16
#define BIT_MASK_NPQ_HIGH_TH_V1 0xfff
#define BIT_NPQ_HIGH_TH_V1(x)                                                  \
	(((x) & BIT_MASK_NPQ_HIGH_TH_V1) << BIT_SHIFT_NPQ_HIGH_TH_V1)
#define BIT_GET_NPQ_HIGH_TH_V1(x)                                              \
	(((x) >> BIT_SHIFT_NPQ_HIGH_TH_V1) & BIT_MASK_NPQ_HIGH_TH_V1)

/* 2 REG_TQPNT2				(Offset 0x021C) */

#define BIT_SHIFT_NPQ_LOW_TH_V1 0
#define BIT_MASK_NPQ_LOW_TH_V1 0xfff
#define BIT_NPQ_LOW_TH_V1(x)                                                   \
	(((x) & BIT_MASK_NPQ_LOW_TH_V1) << BIT_SHIFT_NPQ_LOW_TH_V1)
#define BIT_GET_NPQ_LOW_TH_V1(x)                                               \
	(((x) >> BIT_SHIFT_NPQ_LOW_TH_V1) & BIT_MASK_NPQ_LOW_TH_V1)

/* 2 REG_TQPNT3				(Offset 0x0220) */

#define BIT_SHIFT_LPQ_HIGH_TH_V1 16
#define BIT_MASK_LPQ_HIGH_TH_V1 0xfff
#define BIT_LPQ_HIGH_TH_V1(x)                                                  \
	(((x) & BIT_MASK_LPQ_HIGH_TH_V1) << BIT_SHIFT_LPQ_HIGH_TH_V1)
#define BIT_GET_LPQ_HIGH_TH_V1(x)                                              \
	(((x) >> BIT_SHIFT_LPQ_HIGH_TH_V1) & BIT_MASK_LPQ_HIGH_TH_V1)

/* 2 REG_TQPNT3				(Offset 0x0220) */

#define BIT_SHIFT_LPQ_LOW_TH_V1 0
#define BIT_MASK_LPQ_LOW_TH_V1 0xfff
#define BIT_LPQ_LOW_TH_V1(x)                                                   \
	(((x) & BIT_MASK_LPQ_LOW_TH_V1) << BIT_SHIFT_LPQ_LOW_TH_V1)
#define BIT_GET_LPQ_LOW_TH_V1(x)                                               \
	(((x) >> BIT_SHIFT_LPQ_LOW_TH_V1) & BIT_MASK_LPQ_LOW_TH_V1)

/* 2 REG_TQPNT4				(Offset 0x0224) */

#define BIT_SHIFT_EXQ_HIGH_TH_V1 16
#define BIT_MASK_EXQ_HIGH_TH_V1 0xfff
#define BIT_EXQ_HIGH_TH_V1(x)                                                  \
	(((x) & BIT_MASK_EXQ_HIGH_TH_V1) << BIT_SHIFT_EXQ_HIGH_TH_V1)
#define BIT_GET_EXQ_HIGH_TH_V1(x)                                              \
	(((x) >> BIT_SHIFT_EXQ_HIGH_TH_V1) & BIT_MASK_EXQ_HIGH_TH_V1)

/* 2 REG_TQPNT4				(Offset 0x0224) */

#define BIT_SHIFT_EXQ_LOW_TH_V1 0
#define BIT_MASK_EXQ_LOW_TH_V1 0xfff
#define BIT_EXQ_LOW_TH_V1(x)                                                   \
	(((x) & BIT_MASK_EXQ_LOW_TH_V1) << BIT_SHIFT_EXQ_LOW_TH_V1)
#define BIT_GET_EXQ_LOW_TH_V1(x)                                               \
	(((x) >> BIT_SHIFT_EXQ_LOW_TH_V1) & BIT_MASK_EXQ_LOW_TH_V1)

/* 2 REG_RQPN_CTRL_1				(Offset 0x0228) */

#define BIT_SHIFT_TXPKTNUM_H 16
#define BIT_MASK_TXPKTNUM_H 0xffff
#define BIT_TXPKTNUM_H(x) (((x) & BIT_MASK_TXPKTNUM_H) << BIT_SHIFT_TXPKTNUM_H)
#define BIT_GET_TXPKTNUM_H(x)                                                  \
	(((x) >> BIT_SHIFT_TXPKTNUM_H) & BIT_MASK_TXPKTNUM_H)

/* 2 REG_RQPN_CTRL_1				(Offset 0x0228) */

#define BIT_SHIFT_TXPKTNUM_V2 0
#define BIT_MASK_TXPKTNUM_V2 0xffff
#define BIT_TXPKTNUM_V2(x)                                                     \
	(((x) & BIT_MASK_TXPKTNUM_V2) << BIT_SHIFT_TXPKTNUM_V2)
#define BIT_GET_TXPKTNUM_V2(x)                                                 \
	(((x) >> BIT_SHIFT_TXPKTNUM_V2) & BIT_MASK_TXPKTNUM_V2)

/* 2 REG_RQPN_CTRL_2				(Offset 0x022C) */

#define BIT_EXQ_PUBLIC_DIS_V1 BIT(19)
#define BIT_NPQ_PUBLIC_DIS_V1 BIT(18)
#define BIT_LPQ_PUBLIC_DIS_V1 BIT(17)
#define BIT_HPQ_PUBLIC_DIS_V1 BIT(16)

/* 2 REG_FIFOPAGE_INFO_1			(Offset 0x0230) */

#define BIT_SHIFT_HPQ_AVAL_PG_V1 16
#define BIT_MASK_HPQ_AVAL_PG_V1 0xfff
#define BIT_HPQ_AVAL_PG_V1(x)                                                  \
	(((x) & BIT_MASK_HPQ_AVAL_PG_V1) << BIT_SHIFT_HPQ_AVAL_PG_V1)
#define BIT_GET_HPQ_AVAL_PG_V1(x)                                              \
	(((x) >> BIT_SHIFT_HPQ_AVAL_PG_V1) & BIT_MASK_HPQ_AVAL_PG_V1)

#define BIT_SHIFT_HPQ_V1 0
#define BIT_MASK_HPQ_V1 0xfff
#define BIT_HPQ_V1(x) (((x) & BIT_MASK_HPQ_V1) << BIT_SHIFT_HPQ_V1)
#define BIT_GET_HPQ_V1(x) (((x) >> BIT_SHIFT_HPQ_V1) & BIT_MASK_HPQ_V1)

/* 2 REG_FIFOPAGE_INFO_2			(Offset 0x0234) */

#define BIT_SHIFT_LPQ_AVAL_PG_V1 16
#define BIT_MASK_LPQ_AVAL_PG_V1 0xfff
#define BIT_LPQ_AVAL_PG_V1(x)                                                  \
	(((x) & BIT_MASK_LPQ_AVAL_PG_V1) << BIT_SHIFT_LPQ_AVAL_PG_V1)
#define BIT_GET_LPQ_AVAL_PG_V1(x)                                              \
	(((x) >> BIT_SHIFT_LPQ_AVAL_PG_V1) & BIT_MASK_LPQ_AVAL_PG_V1)

#define BIT_SHIFT_LPQ_V1 0
#define BIT_MASK_LPQ_V1 0xfff
#define BIT_LPQ_V1(x) (((x) & BIT_MASK_LPQ_V1) << BIT_SHIFT_LPQ_V1)
#define BIT_GET_LPQ_V1(x) (((x) >> BIT_SHIFT_LPQ_V1) & BIT_MASK_LPQ_V1)

/* 2 REG_FIFOPAGE_INFO_3			(Offset 0x0238) */

#define BIT_SHIFT_NPQ_AVAL_PG_V1 16
#define BIT_MASK_NPQ_AVAL_PG_V1 0xfff
#define BIT_NPQ_AVAL_PG_V1(x)                                                  \
	(((x) & BIT_MASK_NPQ_AVAL_PG_V1) << BIT_SHIFT_NPQ_AVAL_PG_V1)
#define BIT_GET_NPQ_AVAL_PG_V1(x)                                              \
	(((x) >> BIT_SHIFT_NPQ_AVAL_PG_V1) & BIT_MASK_NPQ_AVAL_PG_V1)

/* 2 REG_FIFOPAGE_INFO_3			(Offset 0x0238) */

#define BIT_SHIFT_NPQ_V1 0
#define BIT_MASK_NPQ_V1 0xfff
#define BIT_NPQ_V1(x) (((x) & BIT_MASK_NPQ_V1) << BIT_SHIFT_NPQ_V1)
#define BIT_GET_NPQ_V1(x) (((x) >> BIT_SHIFT_NPQ_V1) & BIT_MASK_NPQ_V1)

/* 2 REG_FIFOPAGE_INFO_4			(Offset 0x023C) */

#define BIT_SHIFT_EXQ_AVAL_PG_V1 16
#define BIT_MASK_EXQ_AVAL_PG_V1 0xfff
#define BIT_EXQ_AVAL_PG_V1(x)                                                  \
	(((x) & BIT_MASK_EXQ_AVAL_PG_V1) << BIT_SHIFT_EXQ_AVAL_PG_V1)
#define BIT_GET_EXQ_AVAL_PG_V1(x)                                              \
	(((x) >> BIT_SHIFT_EXQ_AVAL_PG_V1) & BIT_MASK_EXQ_AVAL_PG_V1)

#define BIT_SHIFT_EXQ_V1 0
#define BIT_MASK_EXQ_V1 0xfff
#define BIT_EXQ_V1(x) (((x) & BIT_MASK_EXQ_V1) << BIT_SHIFT_EXQ_V1)
#define BIT_GET_EXQ_V1(x) (((x) >> BIT_SHIFT_EXQ_V1) & BIT_MASK_EXQ_V1)

/* 2 REG_FIFOPAGE_INFO_5			(Offset 0x0240) */

#define BIT_SHIFT_PUBQ_AVAL_PG_V1 16
#define BIT_MASK_PUBQ_AVAL_PG_V1 0xfff
#define BIT_PUBQ_AVAL_PG_V1(x)                                                 \
	(((x) & BIT_MASK_PUBQ_AVAL_PG_V1) << BIT_SHIFT_PUBQ_AVAL_PG_V1)
#define BIT_GET_PUBQ_AVAL_PG_V1(x)                                             \
	(((x) >> BIT_SHIFT_PUBQ_AVAL_PG_V1) & BIT_MASK_PUBQ_AVAL_PG_V1)

#define BIT_SHIFT_PUBQ_V1 0
#define BIT_MASK_PUBQ_V1 0xfff
#define BIT_PUBQ_V1(x) (((x) & BIT_MASK_PUBQ_V1) << BIT_SHIFT_PUBQ_V1)
#define BIT_GET_PUBQ_V1(x) (((x) >> BIT_SHIFT_PUBQ_V1) & BIT_MASK_PUBQ_V1)

/* 2 REG_H2C_HEAD				(Offset 0x0244) */

#define BIT_SHIFT_H2C_HEAD 0
#define BIT_MASK_H2C_HEAD 0x3ffff
#define BIT_H2C_HEAD(x) (((x) & BIT_MASK_H2C_HEAD) << BIT_SHIFT_H2C_HEAD)
#define BIT_GET_H2C_HEAD(x) (((x) >> BIT_SHIFT_H2C_HEAD) & BIT_MASK_H2C_HEAD)

/* 2 REG_H2C_TAIL				(Offset 0x0248) */

#define BIT_SHIFT_H2C_TAIL 0
#define BIT_MASK_H2C_TAIL 0x3ffff
#define BIT_H2C_TAIL(x) (((x) & BIT_MASK_H2C_TAIL) << BIT_SHIFT_H2C_TAIL)
#define BIT_GET_H2C_TAIL(x) (((x) >> BIT_SHIFT_H2C_TAIL) & BIT_MASK_H2C_TAIL)

/* 2 REG_H2C_READ_ADDR			(Offset 0x024C) */

#define BIT_SHIFT_H2C_READ_ADDR 0
#define BIT_MASK_H2C_READ_ADDR 0x3ffff
#define BIT_H2C_READ_ADDR(x)                                                   \
	(((x) & BIT_MASK_H2C_READ_ADDR) << BIT_SHIFT_H2C_READ_ADDR)
#define BIT_GET_H2C_READ_ADDR(x)                                               \
	(((x) >> BIT_SHIFT_H2C_READ_ADDR) & BIT_MASK_H2C_READ_ADDR)

/* 2 REG_H2C_WR_ADDR				(Offset 0x0250) */

#define BIT_SHIFT_H2C_WR_ADDR 0
#define BIT_MASK_H2C_WR_ADDR 0x3ffff
#define BIT_H2C_WR_ADDR(x)                                                     \
	(((x) & BIT_MASK_H2C_WR_ADDR) << BIT_SHIFT_H2C_WR_ADDR)
#define BIT_GET_H2C_WR_ADDR(x)                                                 \
	(((x) >> BIT_SHIFT_H2C_WR_ADDR) & BIT_MASK_H2C_WR_ADDR)

/* 2 REG_H2C_INFO				(Offset 0x0254) */

#define BIT_H2C_SPACE_VLD BIT(3)
#define BIT_H2C_WR_ADDR_RST BIT(2)

#define BIT_SHIFT_H2C_LEN_SEL 0
#define BIT_MASK_H2C_LEN_SEL 0x3
#define BIT_H2C_LEN_SEL(x)                                                     \
	(((x) & BIT_MASK_H2C_LEN_SEL) << BIT_SHIFT_H2C_LEN_SEL)
#define BIT_GET_H2C_LEN_SEL(x)                                                 \
	(((x) >> BIT_SHIFT_H2C_LEN_SEL) & BIT_MASK_H2C_LEN_SEL)

/* 2 REG_RXDMA_AGG_PG_TH			(Offset 0x0280) */

#define BIT_SHIFT_RXDMA_AGG_OLD_MOD 24
#define BIT_MASK_RXDMA_AGG_OLD_MOD 0xff
#define BIT_RXDMA_AGG_OLD_MOD(x)                                               \
	(((x) & BIT_MASK_RXDMA_AGG_OLD_MOD) << BIT_SHIFT_RXDMA_AGG_OLD_MOD)
#define BIT_GET_RXDMA_AGG_OLD_MOD(x)                                           \
	(((x) >> BIT_SHIFT_RXDMA_AGG_OLD_MOD) & BIT_MASK_RXDMA_AGG_OLD_MOD)

/* 2 REG_RXDMA_AGG_PG_TH			(Offset 0x0280) */

#define BIT_SHIFT_PKT_NUM_WOL 16
#define BIT_MASK_PKT_NUM_WOL 0xff
#define BIT_PKT_NUM_WOL(x)                                                     \
	(((x) & BIT_MASK_PKT_NUM_WOL) << BIT_SHIFT_PKT_NUM_WOL)
#define BIT_GET_PKT_NUM_WOL(x)                                                 \
	(((x) >> BIT_SHIFT_PKT_NUM_WOL) & BIT_MASK_PKT_NUM_WOL)

/* 2 REG_RXDMA_AGG_PG_TH			(Offset 0x0280) */

#define BIT_SHIFT_DMA_AGG_TO 8
#define BIT_MASK_DMA_AGG_TO 0xf
#define BIT_DMA_AGG_TO(x) (((x) & BIT_MASK_DMA_AGG_TO) << BIT_SHIFT_DMA_AGG_TO)
#define BIT_GET_DMA_AGG_TO(x)                                                  \
	(((x) >> BIT_SHIFT_DMA_AGG_TO) & BIT_MASK_DMA_AGG_TO)

/* 2 REG_RXDMA_AGG_PG_TH			(Offset 0x0280) */

#define BIT_SHIFT_RXDMA_AGG_PG_TH_V1 0
#define BIT_MASK_RXDMA_AGG_PG_TH_V1 0xf
#define BIT_RXDMA_AGG_PG_TH_V1(x)                                              \
	(((x) & BIT_MASK_RXDMA_AGG_PG_TH_V1) << BIT_SHIFT_RXDMA_AGG_PG_TH_V1)
#define BIT_GET_RXDMA_AGG_PG_TH_V1(x)                                          \
	(((x) >> BIT_SHIFT_RXDMA_AGG_PG_TH_V1) & BIT_MASK_RXDMA_AGG_PG_TH_V1)

/* 2 REG_RXPKT_NUM				(Offset 0x0284) */

#define BIT_SHIFT_RXPKT_NUM 24
#define BIT_MASK_RXPKT_NUM 0xff
#define BIT_RXPKT_NUM(x) (((x) & BIT_MASK_RXPKT_NUM) << BIT_SHIFT_RXPKT_NUM)
#define BIT_GET_RXPKT_NUM(x) (((x) >> BIT_SHIFT_RXPKT_NUM) & BIT_MASK_RXPKT_NUM)

/* 2 REG_RXPKT_NUM				(Offset 0x0284) */

#define BIT_SHIFT_FW_UPD_RDPTR19_TO_16 20
#define BIT_MASK_FW_UPD_RDPTR19_TO_16 0xf
#define BIT_FW_UPD_RDPTR19_TO_16(x)                                            \
	(((x) & BIT_MASK_FW_UPD_RDPTR19_TO_16)                                 \
	 << BIT_SHIFT_FW_UPD_RDPTR19_TO_16)
#define BIT_GET_FW_UPD_RDPTR19_TO_16(x)                                        \
	(((x) >> BIT_SHIFT_FW_UPD_RDPTR19_TO_16) &                             \
	 BIT_MASK_FW_UPD_RDPTR19_TO_16)

/* 2 REG_RXPKT_NUM				(Offset 0x0284) */

#define BIT_RXDMA_REQ BIT(19)
#define BIT_RW_RELEASE_EN BIT(18)
#define BIT_RXDMA_IDLE BIT(17)
#define BIT_RXPKT_RELEASE_POLL BIT(16)

#define BIT_SHIFT_FW_UPD_RDPTR 0
#define BIT_MASK_FW_UPD_RDPTR 0xffff
#define BIT_FW_UPD_RDPTR(x)                                                    \
	(((x) & BIT_MASK_FW_UPD_RDPTR) << BIT_SHIFT_FW_UPD_RDPTR)
#define BIT_GET_FW_UPD_RDPTR(x)                                                \
	(((x) >> BIT_SHIFT_FW_UPD_RDPTR) & BIT_MASK_FW_UPD_RDPTR)

/* 2 REG_RXDMA_STATUS			(Offset 0x0288) */

#define BIT_C2H_PKT_OVF BIT(7)

/* 2 REG_RXDMA_STATUS			(Offset 0x0288) */

#define BIT_AGG_CONFGI_ISSUE BIT(6)

/* 2 REG_RXDMA_STATUS			(Offset 0x0288) */

#define BIT_FW_POLL_ISSUE BIT(5)
#define BIT_RX_DATA_UDN BIT(4)
#define BIT_RX_SFF_UDN BIT(3)
#define BIT_RX_SFF_OVF BIT(2)

/* 2 REG_RXDMA_STATUS			(Offset 0x0288) */

#define BIT_RXPKT_OVF BIT(0)

/* 2 REG_RXDMA_DPR				(Offset 0x028C) */

#define BIT_SHIFT_RDE_DEBUG 0
#define BIT_MASK_RDE_DEBUG 0xffffffffL
#define BIT_RDE_DEBUG(x) (((x) & BIT_MASK_RDE_DEBUG) << BIT_SHIFT_RDE_DEBUG)
#define BIT_GET_RDE_DEBUG(x) (((x) >> BIT_SHIFT_RDE_DEBUG) & BIT_MASK_RDE_DEBUG)

/* 2 REG_RXDMA_MODE				(Offset 0x0290) */

#define BIT_SHIFT_PKTNUM_TH_V2 24
#define BIT_MASK_PKTNUM_TH_V2 0x1f
#define BIT_PKTNUM_TH_V2(x)                                                    \
	(((x) & BIT_MASK_PKTNUM_TH_V2) << BIT_SHIFT_PKTNUM_TH_V2)
#define BIT_GET_PKTNUM_TH_V2(x)                                                \
	(((x) >> BIT_SHIFT_PKTNUM_TH_V2) & BIT_MASK_PKTNUM_TH_V2)

#define BIT_TXBA_BREAK_USBAGG BIT(23)

#define BIT_SHIFT_PKTLEN_PARA 16
#define BIT_MASK_PKTLEN_PARA 0x7
#define BIT_PKTLEN_PARA(x)                                                     \
	(((x) & BIT_MASK_PKTLEN_PARA) << BIT_SHIFT_PKTLEN_PARA)
#define BIT_GET_PKTLEN_PARA(x)                                                 \
	(((x) >> BIT_SHIFT_PKTLEN_PARA) & BIT_MASK_PKTLEN_PARA)

/* 2 REG_RXDMA_MODE				(Offset 0x0290) */

#define BIT_SHIFT_BURST_SIZE 4
#define BIT_MASK_BURST_SIZE 0x3
#define BIT_BURST_SIZE(x) (((x) & BIT_MASK_BURST_SIZE) << BIT_SHIFT_BURST_SIZE)
#define BIT_GET_BURST_SIZE(x)                                                  \
	(((x) >> BIT_SHIFT_BURST_SIZE) & BIT_MASK_BURST_SIZE)

#define BIT_SHIFT_BURST_CNT 2
#define BIT_MASK_BURST_CNT 0x3
#define BIT_BURST_CNT(x) (((x) & BIT_MASK_BURST_CNT) << BIT_SHIFT_BURST_CNT)
#define BIT_GET_BURST_CNT(x) (((x) >> BIT_SHIFT_BURST_CNT) & BIT_MASK_BURST_CNT)

/* 2 REG_RXDMA_MODE				(Offset 0x0290) */

#define BIT_DMA_MODE BIT(1)

/* 2 REG_C2H_PKT				(Offset 0x0294) */

#define BIT_SHIFT_R_C2H_STR_ADDR_16_TO_19 24
#define BIT_MASK_R_C2H_STR_ADDR_16_TO_19 0xf
#define BIT_R_C2H_STR_ADDR_16_TO_19(x)                                         \
	(((x) & BIT_MASK_R_C2H_STR_ADDR_16_TO_19)                              \
	 << BIT_SHIFT_R_C2H_STR_ADDR_16_TO_19)
#define BIT_GET_R_C2H_STR_ADDR_16_TO_19(x)                                     \
	(((x) >> BIT_SHIFT_R_C2H_STR_ADDR_16_TO_19) &                          \
	 BIT_MASK_R_C2H_STR_ADDR_16_TO_19)

#define BIT_SHIFT_MDIO_PHY_ADDR 24
#define BIT_MASK_MDIO_PHY_ADDR 0x1f
#define BIT_MDIO_PHY_ADDR(x)                                                   \
	(((x) & BIT_MASK_MDIO_PHY_ADDR) << BIT_SHIFT_MDIO_PHY_ADDR)
#define BIT_GET_MDIO_PHY_ADDR(x)                                               \
	(((x) >> BIT_SHIFT_MDIO_PHY_ADDR) & BIT_MASK_MDIO_PHY_ADDR)

/* 2 REG_C2H_PKT				(Offset 0x0294) */

#define BIT_R_C2H_PKT_REQ BIT(16)
#define BIT_RX_CLOSE_EN BIT(15)
#define BIT_STOP_BCNQ BIT(14)
#define BIT_STOP_MGQ BIT(13)
#define BIT_STOP_VOQ BIT(12)
#define BIT_STOP_VIQ BIT(11)
#define BIT_STOP_BEQ BIT(10)
#define BIT_STOP_BKQ BIT(9)
#define BIT_STOP_RXQ BIT(8)
#define BIT_STOP_HI7Q BIT(7)
#define BIT_STOP_HI6Q BIT(6)
#define BIT_STOP_HI5Q BIT(5)
#define BIT_STOP_HI4Q BIT(4)
#define BIT_STOP_HI3Q BIT(3)
#define BIT_STOP_HI2Q BIT(2)
#define BIT_STOP_HI1Q BIT(1)

#define BIT_SHIFT_R_C2H_STR_ADDR 0
#define BIT_MASK_R_C2H_STR_ADDR 0xffff
#define BIT_R_C2H_STR_ADDR(x)                                                  \
	(((x) & BIT_MASK_R_C2H_STR_ADDR) << BIT_SHIFT_R_C2H_STR_ADDR)
#define BIT_GET_R_C2H_STR_ADDR(x)                                              \
	(((x) >> BIT_SHIFT_R_C2H_STR_ADDR) & BIT_MASK_R_C2H_STR_ADDR)

#define BIT_STOP_HI0Q BIT(0)

/* 2 REG_FWFF_C2H				(Offset 0x0298) */

#define BIT_SHIFT_C2H_DMA_ADDR 0
#define BIT_MASK_C2H_DMA_ADDR 0x3ffff
#define BIT_C2H_DMA_ADDR(x)                                                    \
	(((x) & BIT_MASK_C2H_DMA_ADDR) << BIT_SHIFT_C2H_DMA_ADDR)
#define BIT_GET_C2H_DMA_ADDR(x)                                                \
	(((x) >> BIT_SHIFT_C2H_DMA_ADDR) & BIT_MASK_C2H_DMA_ADDR)

/* 2 REG_FWFF_CTRL				(Offset 0x029C) */

#define BIT_FWFF_DMAPKT_REQ BIT(31)

#define BIT_SHIFT_FWFF_DMA_PKT_NUM 16
#define BIT_MASK_FWFF_DMA_PKT_NUM 0xff
#define BIT_FWFF_DMA_PKT_NUM(x)                                                \
	(((x) & BIT_MASK_FWFF_DMA_PKT_NUM) << BIT_SHIFT_FWFF_DMA_PKT_NUM)
#define BIT_GET_FWFF_DMA_PKT_NUM(x)                                            \
	(((x) >> BIT_SHIFT_FWFF_DMA_PKT_NUM) & BIT_MASK_FWFF_DMA_PKT_NUM)

#define BIT_SHIFT_FWFF_STR_ADDR 0
#define BIT_MASK_FWFF_STR_ADDR 0xffff
#define BIT_FWFF_STR_ADDR(x)                                                   \
	(((x) & BIT_MASK_FWFF_STR_ADDR) << BIT_SHIFT_FWFF_STR_ADDR)
#define BIT_GET_FWFF_STR_ADDR(x)                                               \
	(((x) >> BIT_SHIFT_FWFF_STR_ADDR) & BIT_MASK_FWFF_STR_ADDR)

/* 2 REG_FWFF_PKT_INFO			(Offset 0x02A0) */

#define BIT_SHIFT_FWFF_PKT_QUEUED 16
#define BIT_MASK_FWFF_PKT_QUEUED 0xff
#define BIT_FWFF_PKT_QUEUED(x)                                                 \
	(((x) & BIT_MASK_FWFF_PKT_QUEUED) << BIT_SHIFT_FWFF_PKT_QUEUED)
#define BIT_GET_FWFF_PKT_QUEUED(x)                                             \
	(((x) >> BIT_SHIFT_FWFF_PKT_QUEUED) & BIT_MASK_FWFF_PKT_QUEUED)

/* 2 REG_FWFF_PKT_INFO			(Offset 0x02A0) */

#define BIT_SHIFT_FWFF_PKT_STR_ADDR 0
#define BIT_MASK_FWFF_PKT_STR_ADDR 0xffff
#define BIT_FWFF_PKT_STR_ADDR(x)                                               \
	(((x) & BIT_MASK_FWFF_PKT_STR_ADDR) << BIT_SHIFT_FWFF_PKT_STR_ADDR)
#define BIT_GET_FWFF_PKT_STR_ADDR(x)                                           \
	(((x) >> BIT_SHIFT_FWFF_PKT_STR_ADDR) & BIT_MASK_FWFF_PKT_STR_ADDR)

/* 2 REG_PCIE_CTRL				(Offset 0x0300) */

#define BIT_PCIEIO_PERSTB_SEL BIT(31)

/* 2 REG_PCIE_CTRL				(Offset 0x0300) */

#define BIT_SHIFT_PCIE_MAX_RXDMA 28
#define BIT_MASK_PCIE_MAX_RXDMA 0x7
#define BIT_PCIE_MAX_RXDMA(x)                                                  \
	(((x) & BIT_MASK_PCIE_MAX_RXDMA) << BIT_SHIFT_PCIE_MAX_RXDMA)
#define BIT_GET_PCIE_MAX_RXDMA(x)                                              \
	(((x) >> BIT_SHIFT_PCIE_MAX_RXDMA) & BIT_MASK_PCIE_MAX_RXDMA)

/* 2 REG_PCIE_CTRL				(Offset 0x0300) */

#define BIT_SHIFT_PCIE_MAX_TXDMA 24
#define BIT_MASK_PCIE_MAX_TXDMA 0x7
#define BIT_PCIE_MAX_TXDMA(x)                                                  \
	(((x) & BIT_MASK_PCIE_MAX_TXDMA) << BIT_SHIFT_PCIE_MAX_TXDMA)
#define BIT_GET_PCIE_MAX_TXDMA(x)                                              \
	(((x) >> BIT_SHIFT_PCIE_MAX_TXDMA) & BIT_MASK_PCIE_MAX_TXDMA)

/* 2 REG_PCIE_CTRL				(Offset 0x0300) */

#define BIT_PCIE_RST_TRXDMA_INTF BIT(20)

/* 2 REG_PCIE_CTRL				(Offset 0x0300) */

#define BIT_PCIE_EN_SWENT_L23 BIT(17)

/* 2 REG_PCIE_CTRL				(Offset 0x0300) */

#define BIT_PCIE_EN_HWEXT_L1 BIT(16)

/* 2 REG_INT_MIG				(Offset 0x0304) */

#define BIT_SHIFT_TXTTIMER_MATCH_NUM 28
#define BIT_MASK_TXTTIMER_MATCH_NUM 0xf
#define BIT_TXTTIMER_MATCH_NUM(x)                                              \
	(((x) & BIT_MASK_TXTTIMER_MATCH_NUM) << BIT_SHIFT_TXTTIMER_MATCH_NUM)
#define BIT_GET_TXTTIMER_MATCH_NUM(x)                                          \
	(((x) >> BIT_SHIFT_TXTTIMER_MATCH_NUM) & BIT_MASK_TXTTIMER_MATCH_NUM)

#define BIT_SHIFT_TXPKT_NUM_MATCH 24
#define BIT_MASK_TXPKT_NUM_MATCH 0xf
#define BIT_TXPKT_NUM_MATCH(x)                                                 \
	(((x) & BIT_MASK_TXPKT_NUM_MATCH) << BIT_SHIFT_TXPKT_NUM_MATCH)
#define BIT_GET_TXPKT_NUM_MATCH(x)                                             \
	(((x) >> BIT_SHIFT_TXPKT_NUM_MATCH) & BIT_MASK_TXPKT_NUM_MATCH)

#define BIT_SHIFT_RXTTIMER_MATCH_NUM 20
#define BIT_MASK_RXTTIMER_MATCH_NUM 0xf
#define BIT_RXTTIMER_MATCH_NUM(x)                                              \
	(((x) & BIT_MASK_RXTTIMER_MATCH_NUM) << BIT_SHIFT_RXTTIMER_MATCH_NUM)
#define BIT_GET_RXTTIMER_MATCH_NUM(x)                                          \
	(((x) >> BIT_SHIFT_RXTTIMER_MATCH_NUM) & BIT_MASK_RXTTIMER_MATCH_NUM)

#define BIT_SHIFT_RXPKT_NUM_MATCH 16
#define BIT_MASK_RXPKT_NUM_MATCH 0xf
#define BIT_RXPKT_NUM_MATCH(x)                                                 \
	(((x) & BIT_MASK_RXPKT_NUM_MATCH) << BIT_SHIFT_RXPKT_NUM_MATCH)
#define BIT_GET_RXPKT_NUM_MATCH(x)                                             \
	(((x) >> BIT_SHIFT_RXPKT_NUM_MATCH) & BIT_MASK_RXPKT_NUM_MATCH)

#define BIT_SHIFT_MIGRATE_TIMER 0
#define BIT_MASK_MIGRATE_TIMER 0xffff
#define BIT_MIGRATE_TIMER(x)                                                   \
	(((x) & BIT_MASK_MIGRATE_TIMER) << BIT_SHIFT_MIGRATE_TIMER)
#define BIT_GET_MIGRATE_TIMER(x)                                               \
	(((x) >> BIT_SHIFT_MIGRATE_TIMER) & BIT_MASK_MIGRATE_TIMER)

/* 2 REG_BCNQ_TXBD_DESA			(Offset 0x0308) */

#define BIT_SHIFT_BCNQ_TXBD_DESA 0
#define BIT_MASK_BCNQ_TXBD_DESA 0xffffffffffffffffL
#define BIT_BCNQ_TXBD_DESA(x)                                                  \
	(((x) & BIT_MASK_BCNQ_TXBD_DESA) << BIT_SHIFT_BCNQ_TXBD_DESA)
#define BIT_GET_BCNQ_TXBD_DESA(x)                                              \
	(((x) >> BIT_SHIFT_BCNQ_TXBD_DESA) & BIT_MASK_BCNQ_TXBD_DESA)

/* 2 REG_MGQ_TXBD_DESA			(Offset 0x0310) */

#define BIT_SHIFT_MGQ_TXBD_DESA 0
#define BIT_MASK_MGQ_TXBD_DESA 0xffffffffffffffffL
#define BIT_MGQ_TXBD_DESA(x)                                                   \
	(((x) & BIT_MASK_MGQ_TXBD_DESA) << BIT_SHIFT_MGQ_TXBD_DESA)
#define BIT_GET_MGQ_TXBD_DESA(x)                                               \
	(((x) >> BIT_SHIFT_MGQ_TXBD_DESA) & BIT_MASK_MGQ_TXBD_DESA)

/* 2 REG_VOQ_TXBD_DESA			(Offset 0x0318) */

#define BIT_SHIFT_VOQ_TXBD_DESA 0
#define BIT_MASK_VOQ_TXBD_DESA 0xffffffffffffffffL
#define BIT_VOQ_TXBD_DESA(x)                                                   \
	(((x) & BIT_MASK_VOQ_TXBD_DESA) << BIT_SHIFT_VOQ_TXBD_DESA)
#define BIT_GET_VOQ_TXBD_DESA(x)                                               \
	(((x) >> BIT_SHIFT_VOQ_TXBD_DESA) & BIT_MASK_VOQ_TXBD_DESA)

/* 2 REG_VIQ_TXBD_DESA			(Offset 0x0320) */

#define BIT_SHIFT_VIQ_TXBD_DESA 0
#define BIT_MASK_VIQ_TXBD_DESA 0xffffffffffffffffL
#define BIT_VIQ_TXBD_DESA(x)                                                   \
	(((x) & BIT_MASK_VIQ_TXBD_DESA) << BIT_SHIFT_VIQ_TXBD_DESA)
#define BIT_GET_VIQ_TXBD_DESA(x)                                               \
	(((x) >> BIT_SHIFT_VIQ_TXBD_DESA) & BIT_MASK_VIQ_TXBD_DESA)

/* 2 REG_BEQ_TXBD_DESA			(Offset 0x0328) */

#define BIT_SHIFT_BEQ_TXBD_DESA 0
#define BIT_MASK_BEQ_TXBD_DESA 0xffffffffffffffffL
#define BIT_BEQ_TXBD_DESA(x)                                                   \
	(((x) & BIT_MASK_BEQ_TXBD_DESA) << BIT_SHIFT_BEQ_TXBD_DESA)
#define BIT_GET_BEQ_TXBD_DESA(x)                                               \
	(((x) >> BIT_SHIFT_BEQ_TXBD_DESA) & BIT_MASK_BEQ_TXBD_DESA)

/* 2 REG_BKQ_TXBD_DESA			(Offset 0x0330) */

#define BIT_SHIFT_BKQ_TXBD_DESA 0
#define BIT_MASK_BKQ_TXBD_DESA 0xffffffffffffffffL
#define BIT_BKQ_TXBD_DESA(x)                                                   \
	(((x) & BIT_MASK_BKQ_TXBD_DESA) << BIT_SHIFT_BKQ_TXBD_DESA)
#define BIT_GET_BKQ_TXBD_DESA(x)                                               \
	(((x) >> BIT_SHIFT_BKQ_TXBD_DESA) & BIT_MASK_BKQ_TXBD_DESA)

/* 2 REG_RXQ_RXBD_DESA			(Offset 0x0338) */

#define BIT_SHIFT_RXQ_RXBD_DESA 0
#define BIT_MASK_RXQ_RXBD_DESA 0xffffffffffffffffL
#define BIT_RXQ_RXBD_DESA(x)                                                   \
	(((x) & BIT_MASK_RXQ_RXBD_DESA) << BIT_SHIFT_RXQ_RXBD_DESA)
#define BIT_GET_RXQ_RXBD_DESA(x)                                               \
	(((x) >> BIT_SHIFT_RXQ_RXBD_DESA) & BIT_MASK_RXQ_RXBD_DESA)

/* 2 REG_HI0Q_TXBD_DESA			(Offset 0x0340) */

#define BIT_SHIFT_HI0Q_TXBD_DESA 0
#define BIT_MASK_HI0Q_TXBD_DESA 0xffffffffffffffffL
#define BIT_HI0Q_TXBD_DESA(x)                                                  \
	(((x) & BIT_MASK_HI0Q_TXBD_DESA) << BIT_SHIFT_HI0Q_TXBD_DESA)
#define BIT_GET_HI0Q_TXBD_DESA(x)                                              \
	(((x) >> BIT_SHIFT_HI0Q_TXBD_DESA) & BIT_MASK_HI0Q_TXBD_DESA)

/* 2 REG_HI1Q_TXBD_DESA			(Offset 0x0348) */

#define BIT_SHIFT_HI1Q_TXBD_DESA 0
#define BIT_MASK_HI1Q_TXBD_DESA 0xffffffffffffffffL
#define BIT_HI1Q_TXBD_DESA(x)                                                  \
	(((x) & BIT_MASK_HI1Q_TXBD_DESA) << BIT_SHIFT_HI1Q_TXBD_DESA)
#define BIT_GET_HI1Q_TXBD_DESA(x)                                              \
	(((x) >> BIT_SHIFT_HI1Q_TXBD_DESA) & BIT_MASK_HI1Q_TXBD_DESA)

/* 2 REG_HI2Q_TXBD_DESA			(Offset 0x0350) */

#define BIT_SHIFT_HI2Q_TXBD_DESA 0
#define BIT_MASK_HI2Q_TXBD_DESA 0xffffffffffffffffL
#define BIT_HI2Q_TXBD_DESA(x)                                                  \
	(((x) & BIT_MASK_HI2Q_TXBD_DESA) << BIT_SHIFT_HI2Q_TXBD_DESA)
#define BIT_GET_HI2Q_TXBD_DESA(x)                                              \
	(((x) >> BIT_SHIFT_HI2Q_TXBD_DESA) & BIT_MASK_HI2Q_TXBD_DESA)

/* 2 REG_HI3Q_TXBD_DESA			(Offset 0x0358) */

#define BIT_SHIFT_HI3Q_TXBD_DESA 0
#define BIT_MASK_HI3Q_TXBD_DESA 0xffffffffffffffffL
#define BIT_HI3Q_TXBD_DESA(x)                                                  \
	(((x) & BIT_MASK_HI3Q_TXBD_DESA) << BIT_SHIFT_HI3Q_TXBD_DESA)
#define BIT_GET_HI3Q_TXBD_DESA(x)                                              \
	(((x) >> BIT_SHIFT_HI3Q_TXBD_DESA) & BIT_MASK_HI3Q_TXBD_DESA)

/* 2 REG_HI4Q_TXBD_DESA			(Offset 0x0360) */

#define BIT_SHIFT_HI4Q_TXBD_DESA 0
#define BIT_MASK_HI4Q_TXBD_DESA 0xffffffffffffffffL
#define BIT_HI4Q_TXBD_DESA(x)                                                  \
	(((x) & BIT_MASK_HI4Q_TXBD_DESA) << BIT_SHIFT_HI4Q_TXBD_DESA)
#define BIT_GET_HI4Q_TXBD_DESA(x)                                              \
	(((x) >> BIT_SHIFT_HI4Q_TXBD_DESA) & BIT_MASK_HI4Q_TXBD_DESA)

/* 2 REG_HI5Q_TXBD_DESA			(Offset 0x0368) */

#define BIT_SHIFT_HI5Q_TXBD_DESA 0
#define BIT_MASK_HI5Q_TXBD_DESA 0xffffffffffffffffL
#define BIT_HI5Q_TXBD_DESA(x)                                                  \
	(((x) & BIT_MASK_HI5Q_TXBD_DESA) << BIT_SHIFT_HI5Q_TXBD_DESA)
#define BIT_GET_HI5Q_TXBD_DESA(x)                                              \
	(((x) >> BIT_SHIFT_HI5Q_TXBD_DESA) & BIT_MASK_HI5Q_TXBD_DESA)

/* 2 REG_HI6Q_TXBD_DESA			(Offset 0x0370) */

#define BIT_SHIFT_HI6Q_TXBD_DESA 0
#define BIT_MASK_HI6Q_TXBD_DESA 0xffffffffffffffffL
#define BIT_HI6Q_TXBD_DESA(x)                                                  \
	(((x) & BIT_MASK_HI6Q_TXBD_DESA) << BIT_SHIFT_HI6Q_TXBD_DESA)
#define BIT_GET_HI6Q_TXBD_DESA(x)                                              \
	(((x) >> BIT_SHIFT_HI6Q_TXBD_DESA) & BIT_MASK_HI6Q_TXBD_DESA)

/* 2 REG_HI7Q_TXBD_DESA			(Offset 0x0378) */

#define BIT_SHIFT_HI7Q_TXBD_DESA 0
#define BIT_MASK_HI7Q_TXBD_DESA 0xffffffffffffffffL
#define BIT_HI7Q_TXBD_DESA(x)                                                  \
	(((x) & BIT_MASK_HI7Q_TXBD_DESA) << BIT_SHIFT_HI7Q_TXBD_DESA)
#define BIT_GET_HI7Q_TXBD_DESA(x)                                              \
	(((x) >> BIT_SHIFT_HI7Q_TXBD_DESA) & BIT_MASK_HI7Q_TXBD_DESA)

/* 2 REG_MGQ_TXBD_NUM			(Offset 0x0380) */

#define BIT_PCIE_MGQ_FLAG BIT(14)

/* 2 REG_MGQ_TXBD_NUM			(Offset 0x0380) */

#define BIT_SHIFT_MGQ_DESC_MODE 12
#define BIT_MASK_MGQ_DESC_MODE 0x3
#define BIT_MGQ_DESC_MODE(x)                                                   \
	(((x) & BIT_MASK_MGQ_DESC_MODE) << BIT_SHIFT_MGQ_DESC_MODE)
#define BIT_GET_MGQ_DESC_MODE(x)                                               \
	(((x) >> BIT_SHIFT_MGQ_DESC_MODE) & BIT_MASK_MGQ_DESC_MODE)

#define BIT_SHIFT_MGQ_DESC_NUM 0
#define BIT_MASK_MGQ_DESC_NUM 0xfff
#define BIT_MGQ_DESC_NUM(x)                                                    \
	(((x) & BIT_MASK_MGQ_DESC_NUM) << BIT_SHIFT_MGQ_DESC_NUM)
#define BIT_GET_MGQ_DESC_NUM(x)                                                \
	(((x) >> BIT_SHIFT_MGQ_DESC_NUM) & BIT_MASK_MGQ_DESC_NUM)

/* 2 REG_RX_RXBD_NUM				(Offset 0x0382) */

#define BIT_SYS_32_64 BIT(15)

#define BIT_SHIFT_BCNQ_DESC_MODE 13
#define BIT_MASK_BCNQ_DESC_MODE 0x3
#define BIT_BCNQ_DESC_MODE(x)                                                  \
	(((x) & BIT_MASK_BCNQ_DESC_MODE) << BIT_SHIFT_BCNQ_DESC_MODE)
#define BIT_GET_BCNQ_DESC_MODE(x)                                              \
	(((x) >> BIT_SHIFT_BCNQ_DESC_MODE) & BIT_MASK_BCNQ_DESC_MODE)

/* 2 REG_RX_RXBD_NUM				(Offset 0x0382) */

#define BIT_PCIE_BCNQ_FLAG BIT(12)

/* 2 REG_RX_RXBD_NUM				(Offset 0x0382) */

#define BIT_SHIFT_RXQ_DESC_NUM 0
#define BIT_MASK_RXQ_DESC_NUM 0xfff
#define BIT_RXQ_DESC_NUM(x)                                                    \
	(((x) & BIT_MASK_RXQ_DESC_NUM) << BIT_SHIFT_RXQ_DESC_NUM)
#define BIT_GET_RXQ_DESC_NUM(x)                                                \
	(((x) >> BIT_SHIFT_RXQ_DESC_NUM) & BIT_MASK_RXQ_DESC_NUM)

/* 2 REG_VOQ_TXBD_NUM			(Offset 0x0384) */

#define BIT_PCIE_VOQ_FLAG BIT(14)

/* 2 REG_VOQ_TXBD_NUM			(Offset 0x0384) */

#define BIT_SHIFT_VOQ_DESC_MODE 12
#define BIT_MASK_VOQ_DESC_MODE 0x3
#define BIT_VOQ_DESC_MODE(x)                                                   \
	(((x) & BIT_MASK_VOQ_DESC_MODE) << BIT_SHIFT_VOQ_DESC_MODE)
#define BIT_GET_VOQ_DESC_MODE(x)                                               \
	(((x) >> BIT_SHIFT_VOQ_DESC_MODE) & BIT_MASK_VOQ_DESC_MODE)

#define BIT_SHIFT_VOQ_DESC_NUM 0
#define BIT_MASK_VOQ_DESC_NUM 0xfff
#define BIT_VOQ_DESC_NUM(x)                                                    \
	(((x) & BIT_MASK_VOQ_DESC_NUM) << BIT_SHIFT_VOQ_DESC_NUM)
#define BIT_GET_VOQ_DESC_NUM(x)                                                \
	(((x) >> BIT_SHIFT_VOQ_DESC_NUM) & BIT_MASK_VOQ_DESC_NUM)

/* 2 REG_VIQ_TXBD_NUM			(Offset 0x0386) */

#define BIT_PCIE_VIQ_FLAG BIT(14)

/* 2 REG_VIQ_TXBD_NUM			(Offset 0x0386) */

#define BIT_SHIFT_VIQ_DESC_MODE 12
#define BIT_MASK_VIQ_DESC_MODE 0x3
#define BIT_VIQ_DESC_MODE(x)                                                   \
	(((x) & BIT_MASK_VIQ_DESC_MODE) << BIT_SHIFT_VIQ_DESC_MODE)
#define BIT_GET_VIQ_DESC_MODE(x)                                               \
	(((x) >> BIT_SHIFT_VIQ_DESC_MODE) & BIT_MASK_VIQ_DESC_MODE)

#define BIT_SHIFT_VIQ_DESC_NUM 0
#define BIT_MASK_VIQ_DESC_NUM 0xfff
#define BIT_VIQ_DESC_NUM(x)                                                    \
	(((x) & BIT_MASK_VIQ_DESC_NUM) << BIT_SHIFT_VIQ_DESC_NUM)
#define BIT_GET_VIQ_DESC_NUM(x)                                                \
	(((x) >> BIT_SHIFT_VIQ_DESC_NUM) & BIT_MASK_VIQ_DESC_NUM)

/* 2 REG_BEQ_TXBD_NUM			(Offset 0x0388) */

#define BIT_PCIE_BEQ_FLAG BIT(14)

/* 2 REG_BEQ_TXBD_NUM			(Offset 0x0388) */

#define BIT_SHIFT_BEQ_DESC_MODE 12
#define BIT_MASK_BEQ_DESC_MODE 0x3
#define BIT_BEQ_DESC_MODE(x)                                                   \
	(((x) & BIT_MASK_BEQ_DESC_MODE) << BIT_SHIFT_BEQ_DESC_MODE)
#define BIT_GET_BEQ_DESC_MODE(x)                                               \
	(((x) >> BIT_SHIFT_BEQ_DESC_MODE) & BIT_MASK_BEQ_DESC_MODE)

#define BIT_SHIFT_BEQ_DESC_NUM 0
#define BIT_MASK_BEQ_DESC_NUM 0xfff
#define BIT_BEQ_DESC_NUM(x)                                                    \
	(((x) & BIT_MASK_BEQ_DESC_NUM) << BIT_SHIFT_BEQ_DESC_NUM)
#define BIT_GET_BEQ_DESC_NUM(x)                                                \
	(((x) >> BIT_SHIFT_BEQ_DESC_NUM) & BIT_MASK_BEQ_DESC_NUM)

/* 2 REG_BKQ_TXBD_NUM			(Offset 0x038A) */

#define BIT_PCIE_BKQ_FLAG BIT(14)

/* 2 REG_BKQ_TXBD_NUM			(Offset 0x038A) */

#define BIT_SHIFT_BKQ_DESC_MODE 12
#define BIT_MASK_BKQ_DESC_MODE 0x3
#define BIT_BKQ_DESC_MODE(x)                                                   \
	(((x) & BIT_MASK_BKQ_DESC_MODE) << BIT_SHIFT_BKQ_DESC_MODE)
#define BIT_GET_BKQ_DESC_MODE(x)                                               \
	(((x) >> BIT_SHIFT_BKQ_DESC_MODE) & BIT_MASK_BKQ_DESC_MODE)

#define BIT_SHIFT_BKQ_DESC_NUM 0
#define BIT_MASK_BKQ_DESC_NUM 0xfff
#define BIT_BKQ_DESC_NUM(x)                                                    \
	(((x) & BIT_MASK_BKQ_DESC_NUM) << BIT_SHIFT_BKQ_DESC_NUM)
#define BIT_GET_BKQ_DESC_NUM(x)                                                \
	(((x) >> BIT_SHIFT_BKQ_DESC_NUM) & BIT_MASK_BKQ_DESC_NUM)

/* 2 REG_HI0Q_TXBD_NUM			(Offset 0x038C) */

#define BIT_HI0Q_FLAG BIT(14)

#define BIT_SHIFT_HI0Q_DESC_MODE 12
#define BIT_MASK_HI0Q_DESC_MODE 0x3
#define BIT_HI0Q_DESC_MODE(x)                                                  \
	(((x) & BIT_MASK_HI0Q_DESC_MODE) << BIT_SHIFT_HI0Q_DESC_MODE)
#define BIT_GET_HI0Q_DESC_MODE(x)                                              \
	(((x) >> BIT_SHIFT_HI0Q_DESC_MODE) & BIT_MASK_HI0Q_DESC_MODE)

#define BIT_SHIFT_HI0Q_DESC_NUM 0
#define BIT_MASK_HI0Q_DESC_NUM 0xfff
#define BIT_HI0Q_DESC_NUM(x)                                                   \
	(((x) & BIT_MASK_HI0Q_DESC_NUM) << BIT_SHIFT_HI0Q_DESC_NUM)
#define BIT_GET_HI0Q_DESC_NUM(x)                                               \
	(((x) >> BIT_SHIFT_HI0Q_DESC_NUM) & BIT_MASK_HI0Q_DESC_NUM)

/* 2 REG_HI1Q_TXBD_NUM			(Offset 0x038E) */

#define BIT_HI1Q_FLAG BIT(14)

#define BIT_SHIFT_HI1Q_DESC_MODE 12
#define BIT_MASK_HI1Q_DESC_MODE 0x3
#define BIT_HI1Q_DESC_MODE(x)                                                  \
	(((x) & BIT_MASK_HI1Q_DESC_MODE) << BIT_SHIFT_HI1Q_DESC_MODE)
#define BIT_GET_HI1Q_DESC_MODE(x)                                              \
	(((x) >> BIT_SHIFT_HI1Q_DESC_MODE) & BIT_MASK_HI1Q_DESC_MODE)

#define BIT_SHIFT_HI1Q_DESC_NUM 0
#define BIT_MASK_HI1Q_DESC_NUM 0xfff
#define BIT_HI1Q_DESC_NUM(x)                                                   \
	(((x) & BIT_MASK_HI1Q_DESC_NUM) << BIT_SHIFT_HI1Q_DESC_NUM)
#define BIT_GET_HI1Q_DESC_NUM(x)                                               \
	(((x) >> BIT_SHIFT_HI1Q_DESC_NUM) & BIT_MASK_HI1Q_DESC_NUM)

/* 2 REG_HI2Q_TXBD_NUM			(Offset 0x0390) */

#define BIT_HI2Q_FLAG BIT(14)

#define BIT_SHIFT_HI2Q_DESC_MODE 12
#define BIT_MASK_HI2Q_DESC_MODE 0x3
#define BIT_HI2Q_DESC_MODE(x)                                                  \
	(((x) & BIT_MASK_HI2Q_DESC_MODE) << BIT_SHIFT_HI2Q_DESC_MODE)
#define BIT_GET_HI2Q_DESC_MODE(x)                                              \
	(((x) >> BIT_SHIFT_HI2Q_DESC_MODE) & BIT_MASK_HI2Q_DESC_MODE)

#define BIT_SHIFT_HI2Q_DESC_NUM 0
#define BIT_MASK_HI2Q_DESC_NUM 0xfff
#define BIT_HI2Q_DESC_NUM(x)                                                   \
	(((x) & BIT_MASK_HI2Q_DESC_NUM) << BIT_SHIFT_HI2Q_DESC_NUM)
#define BIT_GET_HI2Q_DESC_NUM(x)                                               \
	(((x) >> BIT_SHIFT_HI2Q_DESC_NUM) & BIT_MASK_HI2Q_DESC_NUM)

/* 2 REG_HI3Q_TXBD_NUM			(Offset 0x0392) */

#define BIT_HI3Q_FLAG BIT(14)

#define BIT_SHIFT_HI3Q_DESC_MODE 12
#define BIT_MASK_HI3Q_DESC_MODE 0x3
#define BIT_HI3Q_DESC_MODE(x)                                                  \
	(((x) & BIT_MASK_HI3Q_DESC_MODE) << BIT_SHIFT_HI3Q_DESC_MODE)
#define BIT_GET_HI3Q_DESC_MODE(x)                                              \
	(((x) >> BIT_SHIFT_HI3Q_DESC_MODE) & BIT_MASK_HI3Q_DESC_MODE)

#define BIT_SHIFT_HI3Q_DESC_NUM 0
#define BIT_MASK_HI3Q_DESC_NUM 0xfff
#define BIT_HI3Q_DESC_NUM(x)                                                   \
	(((x) & BIT_MASK_HI3Q_DESC_NUM) << BIT_SHIFT_HI3Q_DESC_NUM)
#define BIT_GET_HI3Q_DESC_NUM(x)                                               \
	(((x) >> BIT_SHIFT_HI3Q_DESC_NUM) & BIT_MASK_HI3Q_DESC_NUM)

/* 2 REG_HI4Q_TXBD_NUM			(Offset 0x0394) */

#define BIT_HI4Q_FLAG BIT(14)

#define BIT_SHIFT_HI4Q_DESC_MODE 12
#define BIT_MASK_HI4Q_DESC_MODE 0x3
#define BIT_HI4Q_DESC_MODE(x)                                                  \
	(((x) & BIT_MASK_HI4Q_DESC_MODE) << BIT_SHIFT_HI4Q_DESC_MODE)
#define BIT_GET_HI4Q_DESC_MODE(x)                                              \
	(((x) >> BIT_SHIFT_HI4Q_DESC_MODE) & BIT_MASK_HI4Q_DESC_MODE)

#define BIT_SHIFT_HI4Q_DESC_NUM 0
#define BIT_MASK_HI4Q_DESC_NUM 0xfff
#define BIT_HI4Q_DESC_NUM(x)                                                   \
	(((x) & BIT_MASK_HI4Q_DESC_NUM) << BIT_SHIFT_HI4Q_DESC_NUM)
#define BIT_GET_HI4Q_DESC_NUM(x)                                               \
	(((x) >> BIT_SHIFT_HI4Q_DESC_NUM) & BIT_MASK_HI4Q_DESC_NUM)

/* 2 REG_HI5Q_TXBD_NUM			(Offset 0x0396) */

#define BIT_HI5Q_FLAG BIT(14)

#define BIT_SHIFT_HI5Q_DESC_MODE 12
#define BIT_MASK_HI5Q_DESC_MODE 0x3
#define BIT_HI5Q_DESC_MODE(x)                                                  \
	(((x) & BIT_MASK_HI5Q_DESC_MODE) << BIT_SHIFT_HI5Q_DESC_MODE)
#define BIT_GET_HI5Q_DESC_MODE(x)                                              \
	(((x) >> BIT_SHIFT_HI5Q_DESC_MODE) & BIT_MASK_HI5Q_DESC_MODE)

#define BIT_SHIFT_HI5Q_DESC_NUM 0
#define BIT_MASK_HI5Q_DESC_NUM 0xfff
#define BIT_HI5Q_DESC_NUM(x)                                                   \
	(((x) & BIT_MASK_HI5Q_DESC_NUM) << BIT_SHIFT_HI5Q_DESC_NUM)
#define BIT_GET_HI5Q_DESC_NUM(x)                                               \
	(((x) >> BIT_SHIFT_HI5Q_DESC_NUM) & BIT_MASK_HI5Q_DESC_NUM)

/* 2 REG_HI6Q_TXBD_NUM			(Offset 0x0398) */

#define BIT_HI6Q_FLAG BIT(14)

#define BIT_SHIFT_HI6Q_DESC_MODE 12
#define BIT_MASK_HI6Q_DESC_MODE 0x3
#define BIT_HI6Q_DESC_MODE(x)                                                  \
	(((x) & BIT_MASK_HI6Q_DESC_MODE) << BIT_SHIFT_HI6Q_DESC_MODE)
#define BIT_GET_HI6Q_DESC_MODE(x)                                              \
	(((x) >> BIT_SHIFT_HI6Q_DESC_MODE) & BIT_MASK_HI6Q_DESC_MODE)

#define BIT_SHIFT_HI6Q_DESC_NUM 0
#define BIT_MASK_HI6Q_DESC_NUM 0xfff
#define BIT_HI6Q_DESC_NUM(x)                                                   \
	(((x) & BIT_MASK_HI6Q_DESC_NUM) << BIT_SHIFT_HI6Q_DESC_NUM)
#define BIT_GET_HI6Q_DESC_NUM(x)                                               \
	(((x) >> BIT_SHIFT_HI6Q_DESC_NUM) & BIT_MASK_HI6Q_DESC_NUM)

/* 2 REG_HI7Q_TXBD_NUM			(Offset 0x039A) */

#define BIT_HI7Q_FLAG BIT(14)

#define BIT_SHIFT_HI7Q_DESC_MODE 12
#define BIT_MASK_HI7Q_DESC_MODE 0x3
#define BIT_HI7Q_DESC_MODE(x)                                                  \
	(((x) & BIT_MASK_HI7Q_DESC_MODE) << BIT_SHIFT_HI7Q_DESC_MODE)
#define BIT_GET_HI7Q_DESC_MODE(x)                                              \
	(((x) >> BIT_SHIFT_HI7Q_DESC_MODE) & BIT_MASK_HI7Q_DESC_MODE)

#define BIT_SHIFT_HI7Q_DESC_NUM 0
#define BIT_MASK_HI7Q_DESC_NUM 0xfff
#define BIT_HI7Q_DESC_NUM(x)                                                   \
	(((x) & BIT_MASK_HI7Q_DESC_NUM) << BIT_SHIFT_HI7Q_DESC_NUM)
#define BIT_GET_HI7Q_DESC_NUM(x)                                               \
	(((x) >> BIT_SHIFT_HI7Q_DESC_NUM) & BIT_MASK_HI7Q_DESC_NUM)

/* 2 REG_BD_RWPTR_CLR			(Offset 0x039C) */

#define BIT_CLR_HI7Q_HW_IDX BIT(29)
#define BIT_CLR_HI6Q_HW_IDX BIT(28)
#define BIT_CLR_HI5Q_HW_IDX BIT(27)
#define BIT_CLR_HI4Q_HW_IDX BIT(26)
#define BIT_CLR_HI3Q_HW_IDX BIT(25)
#define BIT_CLR_HI2Q_HW_IDX BIT(24)
#define BIT_CLR_HI1Q_HW_IDX BIT(23)

/* 2 REG_BD_RWPTR_CLR			(Offset 0x039C) */

#define BIT_CLR_HI0Q_HW_IDX BIT(22)

/* 2 REG_BD_RWPTR_CLR			(Offset 0x039C) */

#define BIT_CLR_BKQ_HW_IDX BIT(21)

/* 2 REG_BD_RWPTR_CLR			(Offset 0x039C) */

#define BIT_CLR_BEQ_HW_IDX BIT(20)

/* 2 REG_BD_RWPTR_CLR			(Offset 0x039C) */

#define BIT_CLR_VIQ_HW_IDX BIT(19)

/* 2 REG_BD_RWPTR_CLR			(Offset 0x039C) */

#define BIT_CLR_VOQ_HW_IDX BIT(18)

/* 2 REG_BD_RWPTR_CLR			(Offset 0x039C) */

#define BIT_CLR_MGQ_HW_IDX BIT(17)

/* 2 REG_TSFTIMER_HCI			(Offset 0x039C) */

#define BIT_SHIFT_TSFT2_HCI 16
#define BIT_MASK_TSFT2_HCI 0xffff
#define BIT_TSFT2_HCI(x) (((x) & BIT_MASK_TSFT2_HCI) << BIT_SHIFT_TSFT2_HCI)
#define BIT_GET_TSFT2_HCI(x) (((x) >> BIT_SHIFT_TSFT2_HCI) & BIT_MASK_TSFT2_HCI)

#define BIT_CLR_RXQ_HW_IDX BIT(16)

/* 2 REG_BD_RWPTR_CLR			(Offset 0x039C) */

#define BIT_CLR_HI7Q_HOST_IDX BIT(13)

/* 2 REG_BD_RWPTR_CLR			(Offset 0x039C) */

#define BIT_CLR_HI6Q_HOST_IDX BIT(12)

/* 2 REG_BD_RWPTR_CLR			(Offset 0x039C) */

#define BIT_CLR_HI5Q_HOST_IDX BIT(11)

/* 2 REG_BD_RWPTR_CLR			(Offset 0x039C) */

#define BIT_CLR_HI4Q_HOST_IDX BIT(10)

/* 2 REG_BD_RWPTR_CLR			(Offset 0x039C) */

#define BIT_CLR_HI3Q_HOST_IDX BIT(9)

/* 2 REG_BD_RWPTR_CLR			(Offset 0x039C) */

#define BIT_CLR_HI2Q_HOST_IDX BIT(8)

/* 2 REG_BD_RWPTR_CLR			(Offset 0x039C) */

#define BIT_CLR_HI1Q_HOST_IDX BIT(7)
#define BIT_CLR_HI0Q_HOST_IDX BIT(6)

/* 2 REG_BD_RWPTR_CLR			(Offset 0x039C) */

#define BIT_CLR_BKQ_HOST_IDX BIT(5)

/* 2 REG_BD_RWPTR_CLR			(Offset 0x039C) */

#define BIT_CLR_BEQ_HOST_IDX BIT(4)

/* 2 REG_BD_RWPTR_CLR			(Offset 0x039C) */

#define BIT_CLR_VIQ_HOST_IDX BIT(3)

/* 2 REG_BD_RWPTR_CLR			(Offset 0x039C) */

#define BIT_CLR_VOQ_HOST_IDX BIT(2)

/* 2 REG_BD_RWPTR_CLR			(Offset 0x039C) */

#define BIT_CLR_MGQ_HOST_IDX BIT(1)

/* 2 REG_TSFTIMER_HCI			(Offset 0x039C) */

#define BIT_SHIFT_TSFT1_HCI 0
#define BIT_MASK_TSFT1_HCI 0xffff
#define BIT_TSFT1_HCI(x) (((x) & BIT_MASK_TSFT1_HCI) << BIT_SHIFT_TSFT1_HCI)
#define BIT_GET_TSFT1_HCI(x) (((x) >> BIT_SHIFT_TSFT1_HCI) & BIT_MASK_TSFT1_HCI)

#define BIT_CLR_RXQ_HOST_IDX BIT(0)

/* 2 REG_VOQ_TXBD_IDX			(Offset 0x03A0) */

#define BIT_SHIFT_VOQ_HW_IDX 16
#define BIT_MASK_VOQ_HW_IDX 0xfff
#define BIT_VOQ_HW_IDX(x) (((x) & BIT_MASK_VOQ_HW_IDX) << BIT_SHIFT_VOQ_HW_IDX)
#define BIT_GET_VOQ_HW_IDX(x)                                                  \
	(((x) >> BIT_SHIFT_VOQ_HW_IDX) & BIT_MASK_VOQ_HW_IDX)

#define BIT_SHIFT_VOQ_HOST_IDX 0
#define BIT_MASK_VOQ_HOST_IDX 0xfff
#define BIT_VOQ_HOST_IDX(x)                                                    \
	(((x) & BIT_MASK_VOQ_HOST_IDX) << BIT_SHIFT_VOQ_HOST_IDX)
#define BIT_GET_VOQ_HOST_IDX(x)                                                \
	(((x) >> BIT_SHIFT_VOQ_HOST_IDX) & BIT_MASK_VOQ_HOST_IDX)

/* 2 REG_VIQ_TXBD_IDX			(Offset 0x03A4) */

#define BIT_SHIFT_VIQ_HW_IDX 16
#define BIT_MASK_VIQ_HW_IDX 0xfff
#define BIT_VIQ_HW_IDX(x) (((x) & BIT_MASK_VIQ_HW_IDX) << BIT_SHIFT_VIQ_HW_IDX)
#define BIT_GET_VIQ_HW_IDX(x)                                                  \
	(((x) >> BIT_SHIFT_VIQ_HW_IDX) & BIT_MASK_VIQ_HW_IDX)

#define BIT_SHIFT_VIQ_HOST_IDX 0
#define BIT_MASK_VIQ_HOST_IDX 0xfff
#define BIT_VIQ_HOST_IDX(x)                                                    \
	(((x) & BIT_MASK_VIQ_HOST_IDX) << BIT_SHIFT_VIQ_HOST_IDX)
#define BIT_GET_VIQ_HOST_IDX(x)                                                \
	(((x) >> BIT_SHIFT_VIQ_HOST_IDX) & BIT_MASK_VIQ_HOST_IDX)

/* 2 REG_BEQ_TXBD_IDX			(Offset 0x03A8) */

#define BIT_SHIFT_BEQ_HW_IDX 16
#define BIT_MASK_BEQ_HW_IDX 0xfff
#define BIT_BEQ_HW_IDX(x) (((x) & BIT_MASK_BEQ_HW_IDX) << BIT_SHIFT_BEQ_HW_IDX)
#define BIT_GET_BEQ_HW_IDX(x)                                                  \
	(((x) >> BIT_SHIFT_BEQ_HW_IDX) & BIT_MASK_BEQ_HW_IDX)

#define BIT_SHIFT_BEQ_HOST_IDX 0
#define BIT_MASK_BEQ_HOST_IDX 0xfff
#define BIT_BEQ_HOST_IDX(x)                                                    \
	(((x) & BIT_MASK_BEQ_HOST_IDX) << BIT_SHIFT_BEQ_HOST_IDX)
#define BIT_GET_BEQ_HOST_IDX(x)                                                \
	(((x) >> BIT_SHIFT_BEQ_HOST_IDX) & BIT_MASK_BEQ_HOST_IDX)

/* 2 REG_BKQ_TXBD_IDX			(Offset 0x03AC) */

#define BIT_SHIFT_BKQ_HW_IDX 16
#define BIT_MASK_BKQ_HW_IDX 0xfff
#define BIT_BKQ_HW_IDX(x) (((x) & BIT_MASK_BKQ_HW_IDX) << BIT_SHIFT_BKQ_HW_IDX)
#define BIT_GET_BKQ_HW_IDX(x)                                                  \
	(((x) >> BIT_SHIFT_BKQ_HW_IDX) & BIT_MASK_BKQ_HW_IDX)

#define BIT_SHIFT_BKQ_HOST_IDX 0
#define BIT_MASK_BKQ_HOST_IDX 0xfff
#define BIT_BKQ_HOST_IDX(x)                                                    \
	(((x) & BIT_MASK_BKQ_HOST_IDX) << BIT_SHIFT_BKQ_HOST_IDX)
#define BIT_GET_BKQ_HOST_IDX(x)                                                \
	(((x) >> BIT_SHIFT_BKQ_HOST_IDX) & BIT_MASK_BKQ_HOST_IDX)

/* 2 REG_MGQ_TXBD_IDX			(Offset 0x03B0) */

#define BIT_SHIFT_MGQ_HW_IDX 16
#define BIT_MASK_MGQ_HW_IDX 0xfff
#define BIT_MGQ_HW_IDX(x) (((x) & BIT_MASK_MGQ_HW_IDX) << BIT_SHIFT_MGQ_HW_IDX)
#define BIT_GET_MGQ_HW_IDX(x)                                                  \
	(((x) >> BIT_SHIFT_MGQ_HW_IDX) & BIT_MASK_MGQ_HW_IDX)

#define BIT_SHIFT_MGQ_HOST_IDX 0
#define BIT_MASK_MGQ_HOST_IDX 0xfff
#define BIT_MGQ_HOST_IDX(x)                                                    \
	(((x) & BIT_MASK_MGQ_HOST_IDX) << BIT_SHIFT_MGQ_HOST_IDX)
#define BIT_GET_MGQ_HOST_IDX(x)                                                \
	(((x) >> BIT_SHIFT_MGQ_HOST_IDX) & BIT_MASK_MGQ_HOST_IDX)

/* 2 REG_RXQ_RXBD_IDX			(Offset 0x03B4) */

#define BIT_SHIFT_RXQ_HW_IDX 16
#define BIT_MASK_RXQ_HW_IDX 0xfff
#define BIT_RXQ_HW_IDX(x) (((x) & BIT_MASK_RXQ_HW_IDX) << BIT_SHIFT_RXQ_HW_IDX)
#define BIT_GET_RXQ_HW_IDX(x)                                                  \
	(((x) >> BIT_SHIFT_RXQ_HW_IDX) & BIT_MASK_RXQ_HW_IDX)

#define BIT_SHIFT_RXQ_HOST_IDX 0
#define BIT_MASK_RXQ_HOST_IDX 0xfff
#define BIT_RXQ_HOST_IDX(x)                                                    \
	(((x) & BIT_MASK_RXQ_HOST_IDX) << BIT_SHIFT_RXQ_HOST_IDX)
#define BIT_GET_RXQ_HOST_IDX(x)                                                \
	(((x) >> BIT_SHIFT_RXQ_HOST_IDX) & BIT_MASK_RXQ_HOST_IDX)

/* 2 REG_HI0Q_TXBD_IDX			(Offset 0x03B8) */

#define BIT_SHIFT_HI0Q_HW_IDX 16
#define BIT_MASK_HI0Q_HW_IDX 0xfff
#define BIT_HI0Q_HW_IDX(x)                                                     \
	(((x) & BIT_MASK_HI0Q_HW_IDX) << BIT_SHIFT_HI0Q_HW_IDX)
#define BIT_GET_HI0Q_HW_IDX(x)                                                 \
	(((x) >> BIT_SHIFT_HI0Q_HW_IDX) & BIT_MASK_HI0Q_HW_IDX)

#define BIT_SHIFT_HI0Q_HOST_IDX 0
#define BIT_MASK_HI0Q_HOST_IDX 0xfff
#define BIT_HI0Q_HOST_IDX(x)                                                   \
	(((x) & BIT_MASK_HI0Q_HOST_IDX) << BIT_SHIFT_HI0Q_HOST_IDX)
#define BIT_GET_HI0Q_HOST_IDX(x)                                               \
	(((x) >> BIT_SHIFT_HI0Q_HOST_IDX) & BIT_MASK_HI0Q_HOST_IDX)

/* 2 REG_HI1Q_TXBD_IDX			(Offset 0x03BC) */

#define BIT_SHIFT_HI1Q_HW_IDX 16
#define BIT_MASK_HI1Q_HW_IDX 0xfff
#define BIT_HI1Q_HW_IDX(x)                                                     \
	(((x) & BIT_MASK_HI1Q_HW_IDX) << BIT_SHIFT_HI1Q_HW_IDX)
#define BIT_GET_HI1Q_HW_IDX(x)                                                 \
	(((x) >> BIT_SHIFT_HI1Q_HW_IDX) & BIT_MASK_HI1Q_HW_IDX)

#define BIT_SHIFT_HI1Q_HOST_IDX 0
#define BIT_MASK_HI1Q_HOST_IDX 0xfff
#define BIT_HI1Q_HOST_IDX(x)                                                   \
	(((x) & BIT_MASK_HI1Q_HOST_IDX) << BIT_SHIFT_HI1Q_HOST_IDX)
#define BIT_GET_HI1Q_HOST_IDX(x)                                               \
	(((x) >> BIT_SHIFT_HI1Q_HOST_IDX) & BIT_MASK_HI1Q_HOST_IDX)

/* 2 REG_HI2Q_TXBD_IDX			(Offset 0x03C0) */

#define BIT_SHIFT_HI2Q_HW_IDX 16
#define BIT_MASK_HI2Q_HW_IDX 0xfff
#define BIT_HI2Q_HW_IDX(x)                                                     \
	(((x) & BIT_MASK_HI2Q_HW_IDX) << BIT_SHIFT_HI2Q_HW_IDX)
#define BIT_GET_HI2Q_HW_IDX(x)                                                 \
	(((x) >> BIT_SHIFT_HI2Q_HW_IDX) & BIT_MASK_HI2Q_HW_IDX)

#define BIT_SHIFT_HI2Q_HOST_IDX 0
#define BIT_MASK_HI2Q_HOST_IDX 0xfff
#define BIT_HI2Q_HOST_IDX(x)                                                   \
	(((x) & BIT_MASK_HI2Q_HOST_IDX) << BIT_SHIFT_HI2Q_HOST_IDX)
#define BIT_GET_HI2Q_HOST_IDX(x)                                               \
	(((x) >> BIT_SHIFT_HI2Q_HOST_IDX) & BIT_MASK_HI2Q_HOST_IDX)

/* 2 REG_HI3Q_TXBD_IDX			(Offset 0x03C4) */

#define BIT_SHIFT_HI3Q_HW_IDX 16
#define BIT_MASK_HI3Q_HW_IDX 0xfff
#define BIT_HI3Q_HW_IDX(x)                                                     \
	(((x) & BIT_MASK_HI3Q_HW_IDX) << BIT_SHIFT_HI3Q_HW_IDX)
#define BIT_GET_HI3Q_HW_IDX(x)                                                 \
	(((x) >> BIT_SHIFT_HI3Q_HW_IDX) & BIT_MASK_HI3Q_HW_IDX)

#define BIT_SHIFT_HI3Q_HOST_IDX 0
#define BIT_MASK_HI3Q_HOST_IDX 0xfff
#define BIT_HI3Q_HOST_IDX(x)                                                   \
	(((x) & BIT_MASK_HI3Q_HOST_IDX) << BIT_SHIFT_HI3Q_HOST_IDX)
#define BIT_GET_HI3Q_HOST_IDX(x)                                               \
	(((x) >> BIT_SHIFT_HI3Q_HOST_IDX) & BIT_MASK_HI3Q_HOST_IDX)

/* 2 REG_HI4Q_TXBD_IDX			(Offset 0x03C8) */

#define BIT_SHIFT_HI4Q_HW_IDX 16
#define BIT_MASK_HI4Q_HW_IDX 0xfff
#define BIT_HI4Q_HW_IDX(x)                                                     \
	(((x) & BIT_MASK_HI4Q_HW_IDX) << BIT_SHIFT_HI4Q_HW_IDX)
#define BIT_GET_HI4Q_HW_IDX(x)                                                 \
	(((x) >> BIT_SHIFT_HI4Q_HW_IDX) & BIT_MASK_HI4Q_HW_IDX)

#define BIT_SHIFT_HI4Q_HOST_IDX 0
#define BIT_MASK_HI4Q_HOST_IDX 0xfff
#define BIT_HI4Q_HOST_IDX(x)                                                   \
	(((x) & BIT_MASK_HI4Q_HOST_IDX) << BIT_SHIFT_HI4Q_HOST_IDX)
#define BIT_GET_HI4Q_HOST_IDX(x)                                               \
	(((x) >> BIT_SHIFT_HI4Q_HOST_IDX) & BIT_MASK_HI4Q_HOST_IDX)

/* 2 REG_HI5Q_TXBD_IDX			(Offset 0x03CC) */

#define BIT_SHIFT_HI5Q_HW_IDX 16
#define BIT_MASK_HI5Q_HW_IDX 0xfff
#define BIT_HI5Q_HW_IDX(x)                                                     \
	(((x) & BIT_MASK_HI5Q_HW_IDX) << BIT_SHIFT_HI5Q_HW_IDX)
#define BIT_GET_HI5Q_HW_IDX(x)                                                 \
	(((x) >> BIT_SHIFT_HI5Q_HW_IDX) & BIT_MASK_HI5Q_HW_IDX)

#define BIT_SHIFT_HI5Q_HOST_IDX 0
#define BIT_MASK_HI5Q_HOST_IDX 0xfff
#define BIT_HI5Q_HOST_IDX(x)                                                   \
	(((x) & BIT_MASK_HI5Q_HOST_IDX) << BIT_SHIFT_HI5Q_HOST_IDX)
#define BIT_GET_HI5Q_HOST_IDX(x)                                               \
	(((x) >> BIT_SHIFT_HI5Q_HOST_IDX) & BIT_MASK_HI5Q_HOST_IDX)

/* 2 REG_HI6Q_TXBD_IDX			(Offset 0x03D0) */

#define BIT_SHIFT_HI6Q_HW_IDX 16
#define BIT_MASK_HI6Q_HW_IDX 0xfff
#define BIT_HI6Q_HW_IDX(x)                                                     \
	(((x) & BIT_MASK_HI6Q_HW_IDX) << BIT_SHIFT_HI6Q_HW_IDX)
#define BIT_GET_HI6Q_HW_IDX(x)                                                 \
	(((x) >> BIT_SHIFT_HI6Q_HW_IDX) & BIT_MASK_HI6Q_HW_IDX)

#define BIT_SHIFT_HI6Q_HOST_IDX 0
#define BIT_MASK_HI6Q_HOST_IDX 0xfff
#define BIT_HI6Q_HOST_IDX(x)                                                   \
	(((x) & BIT_MASK_HI6Q_HOST_IDX) << BIT_SHIFT_HI6Q_HOST_IDX)
#define BIT_GET_HI6Q_HOST_IDX(x)                                               \
	(((x) >> BIT_SHIFT_HI6Q_HOST_IDX) & BIT_MASK_HI6Q_HOST_IDX)

/* 2 REG_HI7Q_TXBD_IDX			(Offset 0x03D4) */

#define BIT_SHIFT_HI7Q_HW_IDX 16
#define BIT_MASK_HI7Q_HW_IDX 0xfff
#define BIT_HI7Q_HW_IDX(x)                                                     \
	(((x) & BIT_MASK_HI7Q_HW_IDX) << BIT_SHIFT_HI7Q_HW_IDX)
#define BIT_GET_HI7Q_HW_IDX(x)                                                 \
	(((x) >> BIT_SHIFT_HI7Q_HW_IDX) & BIT_MASK_HI7Q_HW_IDX)

#define BIT_SHIFT_HI7Q_HOST_IDX 0
#define BIT_MASK_HI7Q_HOST_IDX 0xfff
#define BIT_HI7Q_HOST_IDX(x)                                                   \
	(((x) & BIT_MASK_HI7Q_HOST_IDX) << BIT_SHIFT_HI7Q_HOST_IDX)
#define BIT_GET_HI7Q_HOST_IDX(x)                                               \
	(((x) >> BIT_SHIFT_HI7Q_HOST_IDX) & BIT_MASK_HI7Q_HOST_IDX)

/* 2 REG_DBG_SEL_V1				(Offset 0x03D8) */

#define BIT_DIS_TXDMA_PRE BIT(7)
#define BIT_DIS_RXDMA_PRE BIT(6)
#define BIT_TXFLAG_EXIT_L1_EN BIT(2)

#define BIT_SHIFT_DBG_SEL 0
#define BIT_MASK_DBG_SEL 0xff
#define BIT_DBG_SEL(x) (((x) & BIT_MASK_DBG_SEL) << BIT_SHIFT_DBG_SEL)
#define BIT_GET_DBG_SEL(x) (((x) >> BIT_SHIFT_DBG_SEL) & BIT_MASK_DBG_SEL)

/* 2 REG_PCIE_HRPWM1_V1			(Offset 0x03D9) */

#define BIT_SHIFT_PCIE_HRPWM 0
#define BIT_MASK_PCIE_HRPWM 0xff
#define BIT_PCIE_HRPWM(x) (((x) & BIT_MASK_PCIE_HRPWM) << BIT_SHIFT_PCIE_HRPWM)
#define BIT_GET_PCIE_HRPWM(x)                                                  \
	(((x) >> BIT_SHIFT_PCIE_HRPWM) & BIT_MASK_PCIE_HRPWM)

/* 2 REG_PCIE_HCPWM1_V1			(Offset 0x03DA) */

#define BIT_SHIFT_PCIE_HCPWM 0
#define BIT_MASK_PCIE_HCPWM 0xff
#define BIT_PCIE_HCPWM(x) (((x) & BIT_MASK_PCIE_HCPWM) << BIT_SHIFT_PCIE_HCPWM)
#define BIT_GET_PCIE_HCPWM(x)                                                  \
	(((x) >> BIT_SHIFT_PCIE_HCPWM) & BIT_MASK_PCIE_HCPWM)

/* 2 REG_PCIE_CTRL2				(Offset 0x03DB) */

#define BIT_SHIFT_HPS_CLKR_PCIE 4
#define BIT_MASK_HPS_CLKR_PCIE 0x3
#define BIT_HPS_CLKR_PCIE(x)                                                   \
	(((x) & BIT_MASK_HPS_CLKR_PCIE) << BIT_SHIFT_HPS_CLKR_PCIE)
#define BIT_GET_HPS_CLKR_PCIE(x)                                               \
	(((x) >> BIT_SHIFT_HPS_CLKR_PCIE) & BIT_MASK_HPS_CLKR_PCIE)

/* 2 REG_PCIE_CTRL2				(Offset 0x03DB) */

#define BIT_PCIE_INT BIT(3)

/* 2 REG_PCIE_CTRL2				(Offset 0x03DB) */

#define BIT_EN_RXDMA_ALIGN BIT(1)
#define BIT_EN_TXDMA_ALIGN BIT(0)

/* 2 REG_PCIE_HRPWM2_V1			(Offset 0x03DC) */

#define BIT_SHIFT_PCIE_HRPWM2 0
#define BIT_MASK_PCIE_HRPWM2 0xffff
#define BIT_PCIE_HRPWM2(x)                                                     \
	(((x) & BIT_MASK_PCIE_HRPWM2) << BIT_SHIFT_PCIE_HRPWM2)
#define BIT_GET_PCIE_HRPWM2(x)                                                 \
	(((x) >> BIT_SHIFT_PCIE_HRPWM2) & BIT_MASK_PCIE_HRPWM2)

/* 2 REG_PCIE_HCPWM2_V1			(Offset 0x03DE) */

#define BIT_SHIFT_PCIE_HCPWM2 0
#define BIT_MASK_PCIE_HCPWM2 0xffff
#define BIT_PCIE_HCPWM2(x)                                                     \
	(((x) & BIT_MASK_PCIE_HCPWM2) << BIT_SHIFT_PCIE_HCPWM2)
#define BIT_GET_PCIE_HCPWM2(x)                                                 \
	(((x) >> BIT_SHIFT_PCIE_HCPWM2) & BIT_MASK_PCIE_HCPWM2)

/* 2 REG_PCIE_H2C_MSG_V1			(Offset 0x03E0) */

#define BIT_SHIFT_DRV2FW_INFO 0
#define BIT_MASK_DRV2FW_INFO 0xffffffffL
#define BIT_DRV2FW_INFO(x)                                                     \
	(((x) & BIT_MASK_DRV2FW_INFO) << BIT_SHIFT_DRV2FW_INFO)
#define BIT_GET_DRV2FW_INFO(x)                                                 \
	(((x) >> BIT_SHIFT_DRV2FW_INFO) & BIT_MASK_DRV2FW_INFO)

/* 2 REG_PCIE_C2H_MSG_V1			(Offset 0x03E4) */

#define BIT_SHIFT_HCI_PCIE_C2H_MSG 0
#define BIT_MASK_HCI_PCIE_C2H_MSG 0xffffffffL
#define BIT_HCI_PCIE_C2H_MSG(x)                                                \
	(((x) & BIT_MASK_HCI_PCIE_C2H_MSG) << BIT_SHIFT_HCI_PCIE_C2H_MSG)
#define BIT_GET_HCI_PCIE_C2H_MSG(x)                                            \
	(((x) >> BIT_SHIFT_HCI_PCIE_C2H_MSG) & BIT_MASK_HCI_PCIE_C2H_MSG)

/* 2 REG_DBI_WDATA_V1			(Offset 0x03E8) */

#define BIT_SHIFT_DBI_WDATA 0
#define BIT_MASK_DBI_WDATA 0xffffffffL
#define BIT_DBI_WDATA(x) (((x) & BIT_MASK_DBI_WDATA) << BIT_SHIFT_DBI_WDATA)
#define BIT_GET_DBI_WDATA(x) (((x) >> BIT_SHIFT_DBI_WDATA) & BIT_MASK_DBI_WDATA)

/* 2 REG_DBI_RDATA_V1			(Offset 0x03EC) */

#define BIT_SHIFT_DBI_RDATA 0
#define BIT_MASK_DBI_RDATA 0xffffffffL
#define BIT_DBI_RDATA(x) (((x) & BIT_MASK_DBI_RDATA) << BIT_SHIFT_DBI_RDATA)
#define BIT_GET_DBI_RDATA(x) (((x) >> BIT_SHIFT_DBI_RDATA) & BIT_MASK_DBI_RDATA)

/* 2 REG_DBI_FLAG_V1				(Offset 0x03F0) */

#define BIT_EN_STUCK_DBG BIT(26)
#define BIT_RX_STUCK BIT(25)
#define BIT_TX_STUCK BIT(24)
#define BIT_DBI_RFLAG BIT(17)
#define BIT_DBI_WFLAG BIT(16)

#define BIT_SHIFT_DBI_WREN 12
#define BIT_MASK_DBI_WREN 0xf
#define BIT_DBI_WREN(x) (((x) & BIT_MASK_DBI_WREN) << BIT_SHIFT_DBI_WREN)
#define BIT_GET_DBI_WREN(x) (((x) >> BIT_SHIFT_DBI_WREN) & BIT_MASK_DBI_WREN)

#define BIT_SHIFT_DBI_ADDR 0
#define BIT_MASK_DBI_ADDR 0xfff
#define BIT_DBI_ADDR(x) (((x) & BIT_MASK_DBI_ADDR) << BIT_SHIFT_DBI_ADDR)
#define BIT_GET_DBI_ADDR(x) (((x) >> BIT_SHIFT_DBI_ADDR) & BIT_MASK_DBI_ADDR)

/* 2 REG_MDIO_V1				(Offset 0x03F4) */

#define BIT_SHIFT_MDIO_RDATA 16
#define BIT_MASK_MDIO_RDATA 0xffff
#define BIT_MDIO_RDATA(x) (((x) & BIT_MASK_MDIO_RDATA) << BIT_SHIFT_MDIO_RDATA)
#define BIT_GET_MDIO_RDATA(x)                                                  \
	(((x) >> BIT_SHIFT_MDIO_RDATA) & BIT_MASK_MDIO_RDATA)

#define BIT_SHIFT_MDIO_WDATA 0
#define BIT_MASK_MDIO_WDATA 0xffff
#define BIT_MDIO_WDATA(x) (((x) & BIT_MASK_MDIO_WDATA) << BIT_SHIFT_MDIO_WDATA)
#define BIT_GET_MDIO_WDATA(x)                                                  \
	(((x) >> BIT_SHIFT_MDIO_WDATA) & BIT_MASK_MDIO_WDATA)

/* 2 REG_PCIE_MIX_CFG			(Offset 0x03F8) */

#define BIT_EN_WATCH_DOG BIT(8)

/* 2 REG_PCIE_MIX_CFG			(Offset 0x03F8) */

#define BIT_SHIFT_MDIO_REG_ADDR_V1 0
#define BIT_MASK_MDIO_REG_ADDR_V1 0x1f
#define BIT_MDIO_REG_ADDR_V1(x)                                                \
	(((x) & BIT_MASK_MDIO_REG_ADDR_V1) << BIT_SHIFT_MDIO_REG_ADDR_V1)
#define BIT_GET_MDIO_REG_ADDR_V1(x)                                            \
	(((x) >> BIT_SHIFT_MDIO_REG_ADDR_V1) & BIT_MASK_MDIO_REG_ADDR_V1)

/* 2 REG_HCI_MIX_CFG				(Offset 0x03FC) */

#define BIT_HOST_GEN2_SUPPORT BIT(20)

#define BIT_SHIFT_TXDMA_ERR_FLAG 16
#define BIT_MASK_TXDMA_ERR_FLAG 0xf
#define BIT_TXDMA_ERR_FLAG(x)                                                  \
	(((x) & BIT_MASK_TXDMA_ERR_FLAG) << BIT_SHIFT_TXDMA_ERR_FLAG)
#define BIT_GET_TXDMA_ERR_FLAG(x)                                              \
	(((x) >> BIT_SHIFT_TXDMA_ERR_FLAG) & BIT_MASK_TXDMA_ERR_FLAG)

#define BIT_SHIFT_EARLY_MODE_SEL 12
#define BIT_MASK_EARLY_MODE_SEL 0xf
#define BIT_EARLY_MODE_SEL(x)                                                  \
	(((x) & BIT_MASK_EARLY_MODE_SEL) << BIT_SHIFT_EARLY_MODE_SEL)
#define BIT_GET_EARLY_MODE_SEL(x)                                              \
	(((x) >> BIT_SHIFT_EARLY_MODE_SEL) & BIT_MASK_EARLY_MODE_SEL)

#define BIT_EPHY_RX50_EN BIT(11)

#define BIT_SHIFT_MSI_TIMEOUT_ID_V1 8
#define BIT_MASK_MSI_TIMEOUT_ID_V1 0x7
#define BIT_MSI_TIMEOUT_ID_V1(x)                                               \
	(((x) & BIT_MASK_MSI_TIMEOUT_ID_V1) << BIT_SHIFT_MSI_TIMEOUT_ID_V1)
#define BIT_GET_MSI_TIMEOUT_ID_V1(x)                                           \
	(((x) >> BIT_SHIFT_MSI_TIMEOUT_ID_V1) & BIT_MASK_MSI_TIMEOUT_ID_V1)

#define BIT_RADDR_RD BIT(7)
#define BIT_EN_MUL_TAG BIT(6)
#define BIT_EN_EARLY_MODE BIT(5)
#define BIT_L0S_LINK_OFF BIT(4)
#define BIT_ACT_LINK_OFF BIT(3)

/* 2 REG_HCI_MIX_CFG				(Offset 0x03FC) */

#define BIT_EN_SLOW_MAC_TX BIT(2)
#define BIT_EN_SLOW_MAC_RX BIT(1)

/* 2 REG_Q0_INFO				(Offset 0x0400) */

#define BIT_SHIFT_QUEUEMACID_Q0_V1 25
#define BIT_MASK_QUEUEMACID_Q0_V1 0x7f
#define BIT_QUEUEMACID_Q0_V1(x)                                                \
	(((x) & BIT_MASK_QUEUEMACID_Q0_V1) << BIT_SHIFT_QUEUEMACID_Q0_V1)
#define BIT_GET_QUEUEMACID_Q0_V1(x)                                            \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q0_V1) & BIT_MASK_QUEUEMACID_Q0_V1)

#define BIT_SHIFT_QUEUEAC_Q0_V1 23
#define BIT_MASK_QUEUEAC_Q0_V1 0x3
#define BIT_QUEUEAC_Q0_V1(x)                                                   \
	(((x) & BIT_MASK_QUEUEAC_Q0_V1) << BIT_SHIFT_QUEUEAC_Q0_V1)
#define BIT_GET_QUEUEAC_Q0_V1(x)                                               \
	(((x) >> BIT_SHIFT_QUEUEAC_Q0_V1) & BIT_MASK_QUEUEAC_Q0_V1)

/* 2 REG_Q0_INFO				(Offset 0x0400) */

#define BIT_TIDEMPTY_Q0_V1 BIT(22)

/* 2 REG_Q0_INFO				(Offset 0x0400) */

#define BIT_SHIFT_TAIL_PKT_Q0_V2 11
#define BIT_MASK_TAIL_PKT_Q0_V2 0x7ff
#define BIT_TAIL_PKT_Q0_V2(x)                                                  \
	(((x) & BIT_MASK_TAIL_PKT_Q0_V2) << BIT_SHIFT_TAIL_PKT_Q0_V2)
#define BIT_GET_TAIL_PKT_Q0_V2(x)                                              \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q0_V2) & BIT_MASK_TAIL_PKT_Q0_V2)

/* 2 REG_Q0_INFO				(Offset 0x0400) */

#define BIT_SHIFT_HEAD_PKT_Q0_V1 0
#define BIT_MASK_HEAD_PKT_Q0_V1 0x7ff
#define BIT_HEAD_PKT_Q0_V1(x)                                                  \
	(((x) & BIT_MASK_HEAD_PKT_Q0_V1) << BIT_SHIFT_HEAD_PKT_Q0_V1)
#define BIT_GET_HEAD_PKT_Q0_V1(x)                                              \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q0_V1) & BIT_MASK_HEAD_PKT_Q0_V1)

/* 2 REG_Q1_INFO				(Offset 0x0404) */

#define BIT_SHIFT_QUEUEMACID_Q1_V1 25
#define BIT_MASK_QUEUEMACID_Q1_V1 0x7f
#define BIT_QUEUEMACID_Q1_V1(x)                                                \
	(((x) & BIT_MASK_QUEUEMACID_Q1_V1) << BIT_SHIFT_QUEUEMACID_Q1_V1)
#define BIT_GET_QUEUEMACID_Q1_V1(x)                                            \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q1_V1) & BIT_MASK_QUEUEMACID_Q1_V1)

#define BIT_SHIFT_QUEUEAC_Q1_V1 23
#define BIT_MASK_QUEUEAC_Q1_V1 0x3
#define BIT_QUEUEAC_Q1_V1(x)                                                   \
	(((x) & BIT_MASK_QUEUEAC_Q1_V1) << BIT_SHIFT_QUEUEAC_Q1_V1)
#define BIT_GET_QUEUEAC_Q1_V1(x)                                               \
	(((x) >> BIT_SHIFT_QUEUEAC_Q1_V1) & BIT_MASK_QUEUEAC_Q1_V1)

/* 2 REG_Q1_INFO				(Offset 0x0404) */

#define BIT_TIDEMPTY_Q1_V1 BIT(22)

/* 2 REG_Q1_INFO				(Offset 0x0404) */

#define BIT_SHIFT_TAIL_PKT_Q1_V2 11
#define BIT_MASK_TAIL_PKT_Q1_V2 0x7ff
#define BIT_TAIL_PKT_Q1_V2(x)                                                  \
	(((x) & BIT_MASK_TAIL_PKT_Q1_V2) << BIT_SHIFT_TAIL_PKT_Q1_V2)
#define BIT_GET_TAIL_PKT_Q1_V2(x)                                              \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q1_V2) & BIT_MASK_TAIL_PKT_Q1_V2)

/* 2 REG_Q1_INFO				(Offset 0x0404) */

#define BIT_SHIFT_HEAD_PKT_Q1_V1 0
#define BIT_MASK_HEAD_PKT_Q1_V1 0x7ff
#define BIT_HEAD_PKT_Q1_V1(x)                                                  \
	(((x) & BIT_MASK_HEAD_PKT_Q1_V1) << BIT_SHIFT_HEAD_PKT_Q1_V1)
#define BIT_GET_HEAD_PKT_Q1_V1(x)                                              \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q1_V1) & BIT_MASK_HEAD_PKT_Q1_V1)

/* 2 REG_Q2_INFO				(Offset 0x0408) */

#define BIT_SHIFT_QUEUEMACID_Q2_V1 25
#define BIT_MASK_QUEUEMACID_Q2_V1 0x7f
#define BIT_QUEUEMACID_Q2_V1(x)                                                \
	(((x) & BIT_MASK_QUEUEMACID_Q2_V1) << BIT_SHIFT_QUEUEMACID_Q2_V1)
#define BIT_GET_QUEUEMACID_Q2_V1(x)                                            \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q2_V1) & BIT_MASK_QUEUEMACID_Q2_V1)

#define BIT_SHIFT_QUEUEAC_Q2_V1 23
#define BIT_MASK_QUEUEAC_Q2_V1 0x3
#define BIT_QUEUEAC_Q2_V1(x)                                                   \
	(((x) & BIT_MASK_QUEUEAC_Q2_V1) << BIT_SHIFT_QUEUEAC_Q2_V1)
#define BIT_GET_QUEUEAC_Q2_V1(x)                                               \
	(((x) >> BIT_SHIFT_QUEUEAC_Q2_V1) & BIT_MASK_QUEUEAC_Q2_V1)

/* 2 REG_Q2_INFO				(Offset 0x0408) */

#define BIT_TIDEMPTY_Q2_V1 BIT(22)

/* 2 REG_Q2_INFO				(Offset 0x0408) */

#define BIT_SHIFT_TAIL_PKT_Q2_V2 11
#define BIT_MASK_TAIL_PKT_Q2_V2 0x7ff
#define BIT_TAIL_PKT_Q2_V2(x)                                                  \
	(((x) & BIT_MASK_TAIL_PKT_Q2_V2) << BIT_SHIFT_TAIL_PKT_Q2_V2)
#define BIT_GET_TAIL_PKT_Q2_V2(x)                                              \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q2_V2) & BIT_MASK_TAIL_PKT_Q2_V2)

/* 2 REG_Q2_INFO				(Offset 0x0408) */

#define BIT_SHIFT_HEAD_PKT_Q2_V1 0
#define BIT_MASK_HEAD_PKT_Q2_V1 0x7ff
#define BIT_HEAD_PKT_Q2_V1(x)                                                  \
	(((x) & BIT_MASK_HEAD_PKT_Q2_V1) << BIT_SHIFT_HEAD_PKT_Q2_V1)
#define BIT_GET_HEAD_PKT_Q2_V1(x)                                              \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q2_V1) & BIT_MASK_HEAD_PKT_Q2_V1)

/* 2 REG_Q3_INFO				(Offset 0x040C) */

#define BIT_SHIFT_QUEUEMACID_Q3_V1 25
#define BIT_MASK_QUEUEMACID_Q3_V1 0x7f
#define BIT_QUEUEMACID_Q3_V1(x)                                                \
	(((x) & BIT_MASK_QUEUEMACID_Q3_V1) << BIT_SHIFT_QUEUEMACID_Q3_V1)
#define BIT_GET_QUEUEMACID_Q3_V1(x)                                            \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q3_V1) & BIT_MASK_QUEUEMACID_Q3_V1)

#define BIT_SHIFT_QUEUEAC_Q3_V1 23
#define BIT_MASK_QUEUEAC_Q3_V1 0x3
#define BIT_QUEUEAC_Q3_V1(x)                                                   \
	(((x) & BIT_MASK_QUEUEAC_Q3_V1) << BIT_SHIFT_QUEUEAC_Q3_V1)
#define BIT_GET_QUEUEAC_Q3_V1(x)                                               \
	(((x) >> BIT_SHIFT_QUEUEAC_Q3_V1) & BIT_MASK_QUEUEAC_Q3_V1)

/* 2 REG_Q3_INFO				(Offset 0x040C) */

#define BIT_TIDEMPTY_Q3_V1 BIT(22)

/* 2 REG_Q3_INFO				(Offset 0x040C) */

#define BIT_SHIFT_TAIL_PKT_Q3_V2 11
#define BIT_MASK_TAIL_PKT_Q3_V2 0x7ff
#define BIT_TAIL_PKT_Q3_V2(x)                                                  \
	(((x) & BIT_MASK_TAIL_PKT_Q3_V2) << BIT_SHIFT_TAIL_PKT_Q3_V2)
#define BIT_GET_TAIL_PKT_Q3_V2(x)                                              \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q3_V2) & BIT_MASK_TAIL_PKT_Q3_V2)

/* 2 REG_Q3_INFO				(Offset 0x040C) */

#define BIT_SHIFT_HEAD_PKT_Q3_V1 0
#define BIT_MASK_HEAD_PKT_Q3_V1 0x7ff
#define BIT_HEAD_PKT_Q3_V1(x)                                                  \
	(((x) & BIT_MASK_HEAD_PKT_Q3_V1) << BIT_SHIFT_HEAD_PKT_Q3_V1)
#define BIT_GET_HEAD_PKT_Q3_V1(x)                                              \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q3_V1) & BIT_MASK_HEAD_PKT_Q3_V1)

/* 2 REG_MGQ_INFO				(Offset 0x0410) */

#define BIT_SHIFT_QUEUEMACID_MGQ_V1 25
#define BIT_MASK_QUEUEMACID_MGQ_V1 0x7f
#define BIT_QUEUEMACID_MGQ_V1(x)                                               \
	(((x) & BIT_MASK_QUEUEMACID_MGQ_V1) << BIT_SHIFT_QUEUEMACID_MGQ_V1)
#define BIT_GET_QUEUEMACID_MGQ_V1(x)                                           \
	(((x) >> BIT_SHIFT_QUEUEMACID_MGQ_V1) & BIT_MASK_QUEUEMACID_MGQ_V1)

#define BIT_SHIFT_QUEUEAC_MGQ_V1 23
#define BIT_MASK_QUEUEAC_MGQ_V1 0x3
#define BIT_QUEUEAC_MGQ_V1(x)                                                  \
	(((x) & BIT_MASK_QUEUEAC_MGQ_V1) << BIT_SHIFT_QUEUEAC_MGQ_V1)
#define BIT_GET_QUEUEAC_MGQ_V1(x)                                              \
	(((x) >> BIT_SHIFT_QUEUEAC_MGQ_V1) & BIT_MASK_QUEUEAC_MGQ_V1)

/* 2 REG_MGQ_INFO				(Offset 0x0410) */

#define BIT_TIDEMPTY_MGQ_V1 BIT(22)

/* 2 REG_MGQ_INFO				(Offset 0x0410) */

#define BIT_SHIFT_TAIL_PKT_MGQ_V2 11
#define BIT_MASK_TAIL_PKT_MGQ_V2 0x7ff
#define BIT_TAIL_PKT_MGQ_V2(x)                                                 \
	(((x) & BIT_MASK_TAIL_PKT_MGQ_V2) << BIT_SHIFT_TAIL_PKT_MGQ_V2)
#define BIT_GET_TAIL_PKT_MGQ_V2(x)                                             \
	(((x) >> BIT_SHIFT_TAIL_PKT_MGQ_V2) & BIT_MASK_TAIL_PKT_MGQ_V2)

/* 2 REG_MGQ_INFO				(Offset 0x0410) */

#define BIT_SHIFT_HEAD_PKT_MGQ_V1 0
#define BIT_MASK_HEAD_PKT_MGQ_V1 0x7ff
#define BIT_HEAD_PKT_MGQ_V1(x)                                                 \
	(((x) & BIT_MASK_HEAD_PKT_MGQ_V1) << BIT_SHIFT_HEAD_PKT_MGQ_V1)
#define BIT_GET_HEAD_PKT_MGQ_V1(x)                                             \
	(((x) >> BIT_SHIFT_HEAD_PKT_MGQ_V1) & BIT_MASK_HEAD_PKT_MGQ_V1)

/* 2 REG_HIQ_INFO				(Offset 0x0414) */

#define BIT_SHIFT_QUEUEMACID_HIQ_V1 25
#define BIT_MASK_QUEUEMACID_HIQ_V1 0x7f
#define BIT_QUEUEMACID_HIQ_V1(x)                                               \
	(((x) & BIT_MASK_QUEUEMACID_HIQ_V1) << BIT_SHIFT_QUEUEMACID_HIQ_V1)
#define BIT_GET_QUEUEMACID_HIQ_V1(x)                                           \
	(((x) >> BIT_SHIFT_QUEUEMACID_HIQ_V1) & BIT_MASK_QUEUEMACID_HIQ_V1)

#define BIT_SHIFT_QUEUEAC_HIQ_V1 23
#define BIT_MASK_QUEUEAC_HIQ_V1 0x3
#define BIT_QUEUEAC_HIQ_V1(x)                                                  \
	(((x) & BIT_MASK_QUEUEAC_HIQ_V1) << BIT_SHIFT_QUEUEAC_HIQ_V1)
#define BIT_GET_QUEUEAC_HIQ_V1(x)                                              \
	(((x) >> BIT_SHIFT_QUEUEAC_HIQ_V1) & BIT_MASK_QUEUEAC_HIQ_V1)

/* 2 REG_HIQ_INFO				(Offset 0x0414) */

#define BIT_TIDEMPTY_HIQ_V1 BIT(22)

/* 2 REG_HIQ_INFO				(Offset 0x0414) */

#define BIT_SHIFT_TAIL_PKT_HIQ_V2 11
#define BIT_MASK_TAIL_PKT_HIQ_V2 0x7ff
#define BIT_TAIL_PKT_HIQ_V2(x)                                                 \
	(((x) & BIT_MASK_TAIL_PKT_HIQ_V2) << BIT_SHIFT_TAIL_PKT_HIQ_V2)
#define BIT_GET_TAIL_PKT_HIQ_V2(x)                                             \
	(((x) >> BIT_SHIFT_TAIL_PKT_HIQ_V2) & BIT_MASK_TAIL_PKT_HIQ_V2)

/* 2 REG_HIQ_INFO				(Offset 0x0414) */

#define BIT_SHIFT_HEAD_PKT_HIQ_V1 0
#define BIT_MASK_HEAD_PKT_HIQ_V1 0x7ff
#define BIT_HEAD_PKT_HIQ_V1(x)                                                 \
	(((x) & BIT_MASK_HEAD_PKT_HIQ_V1) << BIT_SHIFT_HEAD_PKT_HIQ_V1)
#define BIT_GET_HEAD_PKT_HIQ_V1(x)                                             \
	(((x) >> BIT_SHIFT_HEAD_PKT_HIQ_V1) & BIT_MASK_HEAD_PKT_HIQ_V1)

/* 2 REG_BCNQ_INFO				(Offset 0x0418) */

#define BIT_SHIFT_BCNQ_HEAD_PG_V1 0
#define BIT_MASK_BCNQ_HEAD_PG_V1 0xfff
#define BIT_BCNQ_HEAD_PG_V1(x)                                                 \
	(((x) & BIT_MASK_BCNQ_HEAD_PG_V1) << BIT_SHIFT_BCNQ_HEAD_PG_V1)
#define BIT_GET_BCNQ_HEAD_PG_V1(x)                                             \
	(((x) >> BIT_SHIFT_BCNQ_HEAD_PG_V1) & BIT_MASK_BCNQ_HEAD_PG_V1)

/* 2 REG_TXPKT_EMPTY				(Offset 0x041A) */

#define BIT_BCNQ_EMPTY BIT(11)
#define BIT_HQQ_EMPTY BIT(10)
#define BIT_MQQ_EMPTY BIT(9)
#define BIT_MGQ_CPU_EMPTY BIT(8)
#define BIT_AC7Q_EMPTY BIT(7)
#define BIT_AC6Q_EMPTY BIT(6)
#define BIT_AC5Q_EMPTY BIT(5)
#define BIT_AC4Q_EMPTY BIT(4)
#define BIT_AC3Q_EMPTY BIT(3)
#define BIT_AC2Q_EMPTY BIT(2)
#define BIT_AC1Q_EMPTY BIT(1)
#define BIT_AC0Q_EMPTY BIT(0)

/* 2 REG_CPU_MGQ_INFO			(Offset 0x041C) */

#define BIT_BCN1_POLL BIT(30)

/* 2 REG_CPU_MGQ_INFO			(Offset 0x041C) */

#define BIT_CPUMGT_POLL BIT(29)
#define BIT_BCN_POLL BIT(28)

/* 2 REG_CPU_MGQ_INFO			(Offset 0x041C) */

#define BIT_CPUMGQ_FW_NUM_V1 BIT(12)

/* 2 REG_CPU_MGQ_INFO			(Offset 0x041C) */

#define BIT_SHIFT_FW_FREE_TAIL_V1 0
#define BIT_MASK_FW_FREE_TAIL_V1 0xfff
#define BIT_FW_FREE_TAIL_V1(x)                                                 \
	(((x) & BIT_MASK_FW_FREE_TAIL_V1) << BIT_SHIFT_FW_FREE_TAIL_V1)
#define BIT_GET_FW_FREE_TAIL_V1(x)                                             \
	(((x) >> BIT_SHIFT_FW_FREE_TAIL_V1) & BIT_MASK_FW_FREE_TAIL_V1)

/* 2 REG_FWHW_TXQ_CTRL			(Offset 0x0420) */

#define BIT_RTS_LIMIT_IN_OFDM BIT(23)
#define BIT_EN_BCNQ_DL BIT(22)
#define BIT_EN_RD_RESP_NAV_BK BIT(21)
#define BIT_EN_WR_FREE_TAIL BIT(20)

#define BIT_SHIFT_EN_QUEUE_RPT 8
#define BIT_MASK_EN_QUEUE_RPT 0xff
#define BIT_EN_QUEUE_RPT(x)                                                    \
	(((x) & BIT_MASK_EN_QUEUE_RPT) << BIT_SHIFT_EN_QUEUE_RPT)
#define BIT_GET_EN_QUEUE_RPT(x)                                                \
	(((x) >> BIT_SHIFT_EN_QUEUE_RPT) & BIT_MASK_EN_QUEUE_RPT)

#define BIT_EN_RTY_BK BIT(7)
#define BIT_EN_USE_INI_RAT BIT(6)
#define BIT_EN_RTS_NAV_BK BIT(5)
#define BIT_DIS_SSN_CHECK BIT(4)
#define BIT_MACID_MATCH_RTS BIT(3)

/* 2 REG_FWHW_TXQ_CTRL			(Offset 0x0420) */

#define BIT_EN_BCN_TRXRPT_V1 BIT(2)

/* 2 REG_FWHW_TXQ_CTRL			(Offset 0x0420) */

#define BIT_EN_FTMACKRPT BIT(1)

/* 2 REG_FWHW_TXQ_CTRL			(Offset 0x0420) */

#define BIT_EN_FTMRPT BIT(0)

/* 2 REG_DATAFB_SEL				(Offset 0x0423) */

#define BIT__R_EN_RTY_BK_COD BIT(2)

/* 2 REG_DATAFB_SEL				(Offset 0x0423) */

#define BIT_SHIFT__R_DATA_FALLBACK_SEL 0
#define BIT_MASK__R_DATA_FALLBACK_SEL 0x3
#define BIT__R_DATA_FALLBACK_SEL(x)                                            \
	(((x) & BIT_MASK__R_DATA_FALLBACK_SEL)                                 \
	 << BIT_SHIFT__R_DATA_FALLBACK_SEL)
#define BIT_GET__R_DATA_FALLBACK_SEL(x)                                        \
	(((x) >> BIT_SHIFT__R_DATA_FALLBACK_SEL) &                             \
	 BIT_MASK__R_DATA_FALLBACK_SEL)

/* 2 REG_BCNQ_BDNY_V1			(Offset 0x0424) */

#define BIT_SHIFT_BCNQ_PGBNDY_V1 0
#define BIT_MASK_BCNQ_PGBNDY_V1 0xfff
#define BIT_BCNQ_PGBNDY_V1(x)                                                  \
	(((x) & BIT_MASK_BCNQ_PGBNDY_V1) << BIT_SHIFT_BCNQ_PGBNDY_V1)
#define BIT_GET_BCNQ_PGBNDY_V1(x)                                              \
	(((x) >> BIT_SHIFT_BCNQ_PGBNDY_V1) & BIT_MASK_BCNQ_PGBNDY_V1)

/* 2 REG_LIFETIME_EN				(Offset 0x0426) */

#define BIT_BT_INT_CPU BIT(7)
#define BIT_BT_INT_PTA BIT(6)

/* 2 REG_LIFETIME_EN				(Offset 0x0426) */

#define BIT_EN_CTRL_RTYBIT BIT(4)

/* 2 REG_LIFETIME_EN				(Offset 0x0426) */

#define BIT_LIFETIME_BK_EN BIT(3)
#define BIT_LIFETIME_BE_EN BIT(2)
#define BIT_LIFETIME_VI_EN BIT(1)
#define BIT_LIFETIME_VO_EN BIT(0)

/* 2 REG_SPEC_SIFS				(Offset 0x0428) */

#define BIT_SHIFT_SPEC_SIFS_OFDM_PTCL 8
#define BIT_MASK_SPEC_SIFS_OFDM_PTCL 0xff
#define BIT_SPEC_SIFS_OFDM_PTCL(x)                                             \
	(((x) & BIT_MASK_SPEC_SIFS_OFDM_PTCL) << BIT_SHIFT_SPEC_SIFS_OFDM_PTCL)
#define BIT_GET_SPEC_SIFS_OFDM_PTCL(x)                                         \
	(((x) >> BIT_SHIFT_SPEC_SIFS_OFDM_PTCL) & BIT_MASK_SPEC_SIFS_OFDM_PTCL)

#define BIT_SHIFT_SPEC_SIFS_CCK_PTCL 0
#define BIT_MASK_SPEC_SIFS_CCK_PTCL 0xff
#define BIT_SPEC_SIFS_CCK_PTCL(x)                                              \
	(((x) & BIT_MASK_SPEC_SIFS_CCK_PTCL) << BIT_SHIFT_SPEC_SIFS_CCK_PTCL)
#define BIT_GET_SPEC_SIFS_CCK_PTCL(x)                                          \
	(((x) >> BIT_SHIFT_SPEC_SIFS_CCK_PTCL) & BIT_MASK_SPEC_SIFS_CCK_PTCL)

/* 2 REG_RETRY_LIMIT				(Offset 0x042A) */

#define BIT_SHIFT_SRL 8
#define BIT_MASK_SRL 0x3f
#define BIT_SRL(x) (((x) & BIT_MASK_SRL) << BIT_SHIFT_SRL)
#define BIT_GET_SRL(x) (((x) >> BIT_SHIFT_SRL) & BIT_MASK_SRL)

#define BIT_SHIFT_LRL 0
#define BIT_MASK_LRL 0x3f
#define BIT_LRL(x) (((x) & BIT_MASK_LRL) << BIT_SHIFT_LRL)
#define BIT_GET_LRL(x) (((x) >> BIT_SHIFT_LRL) & BIT_MASK_LRL)

/* 2 REG_TXBF_CTRL				(Offset 0x042C) */

#define BIT_R_ENABLE_NDPA BIT(31)
#define BIT_USE_NDPA_PARAMETER BIT(30)
#define BIT_R_PROP_TXBF BIT(29)
#define BIT_R_EN_NDPA_INT BIT(28)
#define BIT_R_TXBF1_80M BIT(27)
#define BIT_R_TXBF1_40M BIT(26)
#define BIT_R_TXBF1_20M BIT(25)

#define BIT_SHIFT_R_TXBF1_AID 16
#define BIT_MASK_R_TXBF1_AID 0x1ff
#define BIT_R_TXBF1_AID(x)                                                     \
	(((x) & BIT_MASK_R_TXBF1_AID) << BIT_SHIFT_R_TXBF1_AID)
#define BIT_GET_R_TXBF1_AID(x)                                                 \
	(((x) >> BIT_SHIFT_R_TXBF1_AID) & BIT_MASK_R_TXBF1_AID)

/* 2 REG_TXBF_CTRL				(Offset 0x042C) */

#define BIT_DIS_NDP_BFEN BIT(15)

/* 2 REG_TXBF_CTRL				(Offset 0x042C) */

#define BIT_R_TXBCN_NOBLOCK_NDP BIT(14)

/* 2 REG_TXBF_CTRL				(Offset 0x042C) */

#define BIT_R_TXBF0_80M BIT(11)
#define BIT_R_TXBF0_40M BIT(10)
#define BIT_R_TXBF0_20M BIT(9)

#define BIT_SHIFT_R_TXBF0_AID 0
#define BIT_MASK_R_TXBF0_AID 0x1ff
#define BIT_R_TXBF0_AID(x)                                                     \
	(((x) & BIT_MASK_R_TXBF0_AID) << BIT_SHIFT_R_TXBF0_AID)
#define BIT_GET_R_TXBF0_AID(x)                                                 \
	(((x) >> BIT_SHIFT_R_TXBF0_AID) & BIT_MASK_R_TXBF0_AID)

/* 2 REG_DARFRC				(Offset 0x0430) */

#define BIT_SHIFT_DARF_RC8 (56 & CPU_OPT_WIDTH)
#define BIT_MASK_DARF_RC8 0x1f
#define BIT_DARF_RC8(x) (((x) & BIT_MASK_DARF_RC8) << BIT_SHIFT_DARF_RC8)
#define BIT_GET_DARF_RC8(x) (((x) >> BIT_SHIFT_DARF_RC8) & BIT_MASK_DARF_RC8)

#define BIT_SHIFT_DARF_RC7 (48 & CPU_OPT_WIDTH)
#define BIT_MASK_DARF_RC7 0x1f
#define BIT_DARF_RC7(x) (((x) & BIT_MASK_DARF_RC7) << BIT_SHIFT_DARF_RC7)
#define BIT_GET_DARF_RC7(x) (((x) >> BIT_SHIFT_DARF_RC7) & BIT_MASK_DARF_RC7)

#define BIT_SHIFT_DARF_RC6 (40 & CPU_OPT_WIDTH)
#define BIT_MASK_DARF_RC6 0x1f
#define BIT_DARF_RC6(x) (((x) & BIT_MASK_DARF_RC6) << BIT_SHIFT_DARF_RC6)
#define BIT_GET_DARF_RC6(x) (((x) >> BIT_SHIFT_DARF_RC6) & BIT_MASK_DARF_RC6)

#define BIT_SHIFT_DARF_RC5 (32 & CPU_OPT_WIDTH)
#define BIT_MASK_DARF_RC5 0x1f
#define BIT_DARF_RC5(x) (((x) & BIT_MASK_DARF_RC5) << BIT_SHIFT_DARF_RC5)
#define BIT_GET_DARF_RC5(x) (((x) >> BIT_SHIFT_DARF_RC5) & BIT_MASK_DARF_RC5)

#define BIT_SHIFT_DARF_RC4 24
#define BIT_MASK_DARF_RC4 0x1f
#define BIT_DARF_RC4(x) (((x) & BIT_MASK_DARF_RC4) << BIT_SHIFT_DARF_RC4)
#define BIT_GET_DARF_RC4(x) (((x) >> BIT_SHIFT_DARF_RC4) & BIT_MASK_DARF_RC4)

#define BIT_SHIFT_DARF_RC3 16
#define BIT_MASK_DARF_RC3 0x1f
#define BIT_DARF_RC3(x) (((x) & BIT_MASK_DARF_RC3) << BIT_SHIFT_DARF_RC3)
#define BIT_GET_DARF_RC3(x) (((x) >> BIT_SHIFT_DARF_RC3) & BIT_MASK_DARF_RC3)

#define BIT_SHIFT_DARF_RC2 8
#define BIT_MASK_DARF_RC2 0x1f
#define BIT_DARF_RC2(x) (((x) & BIT_MASK_DARF_RC2) << BIT_SHIFT_DARF_RC2)
#define BIT_GET_DARF_RC2(x) (((x) >> BIT_SHIFT_DARF_RC2) & BIT_MASK_DARF_RC2)

#define BIT_SHIFT_DARF_RC1 0
#define BIT_MASK_DARF_RC1 0x1f
#define BIT_DARF_RC1(x) (((x) & BIT_MASK_DARF_RC1) << BIT_SHIFT_DARF_RC1)
#define BIT_GET_DARF_RC1(x) (((x) >> BIT_SHIFT_DARF_RC1) & BIT_MASK_DARF_RC1)

/* 2 REG_RARFRC				(Offset 0x0438) */

#define BIT_SHIFT_RARF_RC8 (56 & CPU_OPT_WIDTH)
#define BIT_MASK_RARF_RC8 0x1f
#define BIT_RARF_RC8(x) (((x) & BIT_MASK_RARF_RC8) << BIT_SHIFT_RARF_RC8)
#define BIT_GET_RARF_RC8(x) (((x) >> BIT_SHIFT_RARF_RC8) & BIT_MASK_RARF_RC8)

#define BIT_SHIFT_RARF_RC7 (48 & CPU_OPT_WIDTH)
#define BIT_MASK_RARF_RC7 0x1f
#define BIT_RARF_RC7(x) (((x) & BIT_MASK_RARF_RC7) << BIT_SHIFT_RARF_RC7)
#define BIT_GET_RARF_RC7(x) (((x) >> BIT_SHIFT_RARF_RC7) & BIT_MASK_RARF_RC7)

#define BIT_SHIFT_RARF_RC6 (40 & CPU_OPT_WIDTH)
#define BIT_MASK_RARF_RC6 0x1f
#define BIT_RARF_RC6(x) (((x) & BIT_MASK_RARF_RC6) << BIT_SHIFT_RARF_RC6)
#define BIT_GET_RARF_RC6(x) (((x) >> BIT_SHIFT_RARF_RC6) & BIT_MASK_RARF_RC6)

#define BIT_SHIFT_RARF_RC5 (32 & CPU_OPT_WIDTH)
#define BIT_MASK_RARF_RC5 0x1f
#define BIT_RARF_RC5(x) (((x) & BIT_MASK_RARF_RC5) << BIT_SHIFT_RARF_RC5)
#define BIT_GET_RARF_RC5(x) (((x) >> BIT_SHIFT_RARF_RC5) & BIT_MASK_RARF_RC5)

#define BIT_SHIFT_RARF_RC4 24
#define BIT_MASK_RARF_RC4 0x1f
#define BIT_RARF_RC4(x) (((x) & BIT_MASK_RARF_RC4) << BIT_SHIFT_RARF_RC4)
#define BIT_GET_RARF_RC4(x) (((x) >> BIT_SHIFT_RARF_RC4) & BIT_MASK_RARF_RC4)

#define BIT_SHIFT_RARF_RC3 16
#define BIT_MASK_RARF_RC3 0x1f
#define BIT_RARF_RC3(x) (((x) & BIT_MASK_RARF_RC3) << BIT_SHIFT_RARF_RC3)
#define BIT_GET_RARF_RC3(x) (((x) >> BIT_SHIFT_RARF_RC3) & BIT_MASK_RARF_RC3)

#define BIT_SHIFT_RARF_RC2 8
#define BIT_MASK_RARF_RC2 0x1f
#define BIT_RARF_RC2(x) (((x) & BIT_MASK_RARF_RC2) << BIT_SHIFT_RARF_RC2)
#define BIT_GET_RARF_RC2(x) (((x) >> BIT_SHIFT_RARF_RC2) & BIT_MASK_RARF_RC2)

#define BIT_SHIFT_RARF_RC1 0
#define BIT_MASK_RARF_RC1 0x1f
#define BIT_RARF_RC1(x) (((x) & BIT_MASK_RARF_RC1) << BIT_SHIFT_RARF_RC1)
#define BIT_GET_RARF_RC1(x) (((x) >> BIT_SHIFT_RARF_RC1) & BIT_MASK_RARF_RC1)

/* 2 REG_RRSR				(Offset 0x0440) */

#define BIT_SHIFT_RRSR_RSC 21
#define BIT_MASK_RRSR_RSC 0x3
#define BIT_RRSR_RSC(x) (((x) & BIT_MASK_RRSR_RSC) << BIT_SHIFT_RRSR_RSC)
#define BIT_GET_RRSR_RSC(x) (((x) >> BIT_SHIFT_RRSR_RSC) & BIT_MASK_RRSR_RSC)

#define BIT_RRSR_BW BIT(20)

#define BIT_SHIFT_RRSC_BITMAP 0
#define BIT_MASK_RRSC_BITMAP 0xfffff
#define BIT_RRSC_BITMAP(x)                                                     \
	(((x) & BIT_MASK_RRSC_BITMAP) << BIT_SHIFT_RRSC_BITMAP)
#define BIT_GET_RRSC_BITMAP(x)                                                 \
	(((x) >> BIT_SHIFT_RRSC_BITMAP) & BIT_MASK_RRSC_BITMAP)

/* 2 REG_ARFR0				(Offset 0x0444) */

#define BIT_SHIFT_ARFR0_V1 0
#define BIT_MASK_ARFR0_V1 0xffffffffffffffffL
#define BIT_ARFR0_V1(x) (((x) & BIT_MASK_ARFR0_V1) << BIT_SHIFT_ARFR0_V1)
#define BIT_GET_ARFR0_V1(x) (((x) >> BIT_SHIFT_ARFR0_V1) & BIT_MASK_ARFR0_V1)

/* 2 REG_ARFR1_V1				(Offset 0x044C) */

#define BIT_SHIFT_ARFR1_V1 0
#define BIT_MASK_ARFR1_V1 0xffffffffffffffffL
#define BIT_ARFR1_V1(x) (((x) & BIT_MASK_ARFR1_V1) << BIT_SHIFT_ARFR1_V1)
#define BIT_GET_ARFR1_V1(x) (((x) >> BIT_SHIFT_ARFR1_V1) & BIT_MASK_ARFR1_V1)

/* 2 REG_CCK_CHECK				(Offset 0x0454) */

#define BIT_CHECK_CCK_EN BIT(7)
#define BIT_EN_BCN_PKT_REL BIT(6)
#define BIT_BCN_PORT_SEL BIT(5)
#define BIT_MOREDATA_BYPASS BIT(4)
#define BIT_EN_CLR_CMD_REL_BCN_PKT BIT(3)

/* 2 REG_CCK_CHECK				(Offset 0x0454) */

#define BIT_R_EN_SET_MOREDATA BIT(2)
#define BIT__R_DIS_CLEAR_MACID_RELEASE BIT(1)
#define BIT__R_MACID_RELEASE_EN BIT(0)

/* 2 REG_AMPDU_MAX_TIME			(Offset 0x0456) */

#define BIT_SHIFT_AMPDU_MAX_TIME 0
#define BIT_MASK_AMPDU_MAX_TIME 0xff
#define BIT_AMPDU_MAX_TIME(x)                                                  \
	(((x) & BIT_MASK_AMPDU_MAX_TIME) << BIT_SHIFT_AMPDU_MAX_TIME)
#define BIT_GET_AMPDU_MAX_TIME(x)                                              \
	(((x) >> BIT_SHIFT_AMPDU_MAX_TIME) & BIT_MASK_AMPDU_MAX_TIME)

/* 2 REG_BCNQ1_BDNY_V1			(Offset 0x0456) */

#define BIT_SHIFT_BCNQ1_PGBNDY_V1 0
#define BIT_MASK_BCNQ1_PGBNDY_V1 0xfff
#define BIT_BCNQ1_PGBNDY_V1(x)                                                 \
	(((x) & BIT_MASK_BCNQ1_PGBNDY_V1) << BIT_SHIFT_BCNQ1_PGBNDY_V1)
#define BIT_GET_BCNQ1_PGBNDY_V1(x)                                             \
	(((x) >> BIT_SHIFT_BCNQ1_PGBNDY_V1) & BIT_MASK_BCNQ1_PGBNDY_V1)

/* 2 REG_AMPDU_MAX_LENGTH			(Offset 0x0458) */

#define BIT_SHIFT_AMPDU_MAX_LENGTH 0
#define BIT_MASK_AMPDU_MAX_LENGTH 0xffffffffL
#define BIT_AMPDU_MAX_LENGTH(x)                                                \
	(((x) & BIT_MASK_AMPDU_MAX_LENGTH) << BIT_SHIFT_AMPDU_MAX_LENGTH)
#define BIT_GET_AMPDU_MAX_LENGTH(x)                                            \
	(((x) >> BIT_SHIFT_AMPDU_MAX_LENGTH) & BIT_MASK_AMPDU_MAX_LENGTH)

/* 2 REG_ACQ_STOP				(Offset 0x045C) */

#define BIT_AC7Q_STOP BIT(7)
#define BIT_AC6Q_STOP BIT(6)
#define BIT_AC5Q_STOP BIT(5)
#define BIT_AC4Q_STOP BIT(4)
#define BIT_AC3Q_STOP BIT(3)
#define BIT_AC2Q_STOP BIT(2)
#define BIT_AC1Q_STOP BIT(1)
#define BIT_AC0Q_STOP BIT(0)

/* 2 REG_NDPA_RATE				(Offset 0x045D) */

#define BIT_SHIFT_R_NDPA_RATE_V1 0
#define BIT_MASK_R_NDPA_RATE_V1 0xff
#define BIT_R_NDPA_RATE_V1(x)                                                  \
	(((x) & BIT_MASK_R_NDPA_RATE_V1) << BIT_SHIFT_R_NDPA_RATE_V1)
#define BIT_GET_R_NDPA_RATE_V1(x)                                              \
	(((x) >> BIT_SHIFT_R_NDPA_RATE_V1) & BIT_MASK_R_NDPA_RATE_V1)

/* 2 REG_TX_HANG_CTRL			(Offset 0x045E) */

#define BIT_R_EN_GNT_BT_AWAKE BIT(3)

/* 2 REG_TX_HANG_CTRL			(Offset 0x045E) */

#define BIT_EN_EOF_V1 BIT(2)

/* 2 REG_TX_HANG_CTRL			(Offset 0x045E) */

#define BIT_DIS_OQT_BLOCK BIT(1)
#define BIT_SEARCH_QUEUE_EN BIT(0)

/* 2 REG_NDPA_OPT_CTRL			(Offset 0x045F) */

#define BIT_R_DIS_MACID_RELEASE_RTY BIT(5)

/* 2 REG_NDPA_OPT_CTRL			(Offset 0x045F) */

#define BIT_SHIFT_BW_SIGTA 3
#define BIT_MASK_BW_SIGTA 0x3
#define BIT_BW_SIGTA(x) (((x) & BIT_MASK_BW_SIGTA) << BIT_SHIFT_BW_SIGTA)
#define BIT_GET_BW_SIGTA(x) (((x) >> BIT_SHIFT_BW_SIGTA) & BIT_MASK_BW_SIGTA)

/* 2 REG_NDPA_OPT_CTRL			(Offset 0x045F) */

#define BIT_EN_BAR_SIGTA BIT(2)

/* 2 REG_NDPA_OPT_CTRL			(Offset 0x045F) */

#define BIT_SHIFT_R_NDPA_BW 0
#define BIT_MASK_R_NDPA_BW 0x3
#define BIT_R_NDPA_BW(x) (((x) & BIT_MASK_R_NDPA_BW) << BIT_SHIFT_R_NDPA_BW)
#define BIT_GET_R_NDPA_BW(x) (((x) >> BIT_SHIFT_R_NDPA_BW) & BIT_MASK_R_NDPA_BW)

/* 2 REG_RD_RESP_PKT_TH			(Offset 0x0463) */

#define BIT_SHIFT_RD_RESP_PKT_TH_V1 0
#define BIT_MASK_RD_RESP_PKT_TH_V1 0x3f
#define BIT_RD_RESP_PKT_TH_V1(x)                                               \
	(((x) & BIT_MASK_RD_RESP_PKT_TH_V1) << BIT_SHIFT_RD_RESP_PKT_TH_V1)
#define BIT_GET_RD_RESP_PKT_TH_V1(x)                                           \
	(((x) >> BIT_SHIFT_RD_RESP_PKT_TH_V1) & BIT_MASK_RD_RESP_PKT_TH_V1)

/* 2 REG_CMDQ_INFO				(Offset 0x0464) */

#define BIT_SHIFT_QUEUEMACID_CMDQ_V1 25
#define BIT_MASK_QUEUEMACID_CMDQ_V1 0x7f
#define BIT_QUEUEMACID_CMDQ_V1(x)                                              \
	(((x) & BIT_MASK_QUEUEMACID_CMDQ_V1) << BIT_SHIFT_QUEUEMACID_CMDQ_V1)
#define BIT_GET_QUEUEMACID_CMDQ_V1(x)                                          \
	(((x) >> BIT_SHIFT_QUEUEMACID_CMDQ_V1) & BIT_MASK_QUEUEMACID_CMDQ_V1)

/* 2 REG_CMDQ_INFO				(Offset 0x0464) */

#define BIT_SHIFT_QUEUEAC_CMDQ_V1 23
#define BIT_MASK_QUEUEAC_CMDQ_V1 0x3
#define BIT_QUEUEAC_CMDQ_V1(x)                                                 \
	(((x) & BIT_MASK_QUEUEAC_CMDQ_V1) << BIT_SHIFT_QUEUEAC_CMDQ_V1)
#define BIT_GET_QUEUEAC_CMDQ_V1(x)                                             \
	(((x) >> BIT_SHIFT_QUEUEAC_CMDQ_V1) & BIT_MASK_QUEUEAC_CMDQ_V1)

/* 2 REG_CMDQ_INFO				(Offset 0x0464) */

#define BIT_TIDEMPTY_CMDQ_V1 BIT(22)

/* 2 REG_CMDQ_INFO				(Offset 0x0464) */

#define BIT_SHIFT_TAIL_PKT_CMDQ_V2 11
#define BIT_MASK_TAIL_PKT_CMDQ_V2 0x7ff
#define BIT_TAIL_PKT_CMDQ_V2(x)                                                \
	(((x) & BIT_MASK_TAIL_PKT_CMDQ_V2) << BIT_SHIFT_TAIL_PKT_CMDQ_V2)
#define BIT_GET_TAIL_PKT_CMDQ_V2(x)                                            \
	(((x) >> BIT_SHIFT_TAIL_PKT_CMDQ_V2) & BIT_MASK_TAIL_PKT_CMDQ_V2)

/* 2 REG_CMDQ_INFO				(Offset 0x0464) */

#define BIT_SHIFT_HEAD_PKT_CMDQ_V1 0
#define BIT_MASK_HEAD_PKT_CMDQ_V1 0x7ff
#define BIT_HEAD_PKT_CMDQ_V1(x)                                                \
	(((x) & BIT_MASK_HEAD_PKT_CMDQ_V1) << BIT_SHIFT_HEAD_PKT_CMDQ_V1)
#define BIT_GET_HEAD_PKT_CMDQ_V1(x)                                            \
	(((x) >> BIT_SHIFT_HEAD_PKT_CMDQ_V1) & BIT_MASK_HEAD_PKT_CMDQ_V1)

/* 2 REG_Q4_INFO				(Offset 0x0468) */

#define BIT_SHIFT_QUEUEMACID_Q4_V1 25
#define BIT_MASK_QUEUEMACID_Q4_V1 0x7f
#define BIT_QUEUEMACID_Q4_V1(x)                                                \
	(((x) & BIT_MASK_QUEUEMACID_Q4_V1) << BIT_SHIFT_QUEUEMACID_Q4_V1)
#define BIT_GET_QUEUEMACID_Q4_V1(x)                                            \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q4_V1) & BIT_MASK_QUEUEMACID_Q4_V1)

#define BIT_SHIFT_QUEUEAC_Q4_V1 23
#define BIT_MASK_QUEUEAC_Q4_V1 0x3
#define BIT_QUEUEAC_Q4_V1(x)                                                   \
	(((x) & BIT_MASK_QUEUEAC_Q4_V1) << BIT_SHIFT_QUEUEAC_Q4_V1)
#define BIT_GET_QUEUEAC_Q4_V1(x)                                               \
	(((x) >> BIT_SHIFT_QUEUEAC_Q4_V1) & BIT_MASK_QUEUEAC_Q4_V1)

/* 2 REG_Q4_INFO				(Offset 0x0468) */

#define BIT_TIDEMPTY_Q4_V1 BIT(22)

/* 2 REG_Q4_INFO				(Offset 0x0468) */

#define BIT_SHIFT_TAIL_PKT_Q4_V2 11
#define BIT_MASK_TAIL_PKT_Q4_V2 0x7ff
#define BIT_TAIL_PKT_Q4_V2(x)                                                  \
	(((x) & BIT_MASK_TAIL_PKT_Q4_V2) << BIT_SHIFT_TAIL_PKT_Q4_V2)
#define BIT_GET_TAIL_PKT_Q4_V2(x)                                              \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q4_V2) & BIT_MASK_TAIL_PKT_Q4_V2)

/* 2 REG_Q4_INFO				(Offset 0x0468) */

#define BIT_SHIFT_HEAD_PKT_Q4_V1 0
#define BIT_MASK_HEAD_PKT_Q4_V1 0x7ff
#define BIT_HEAD_PKT_Q4_V1(x)                                                  \
	(((x) & BIT_MASK_HEAD_PKT_Q4_V1) << BIT_SHIFT_HEAD_PKT_Q4_V1)
#define BIT_GET_HEAD_PKT_Q4_V1(x)                                              \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q4_V1) & BIT_MASK_HEAD_PKT_Q4_V1)

/* 2 REG_Q5_INFO				(Offset 0x046C) */

#define BIT_SHIFT_QUEUEMACID_Q5_V1 25
#define BIT_MASK_QUEUEMACID_Q5_V1 0x7f
#define BIT_QUEUEMACID_Q5_V1(x)                                                \
	(((x) & BIT_MASK_QUEUEMACID_Q5_V1) << BIT_SHIFT_QUEUEMACID_Q5_V1)
#define BIT_GET_QUEUEMACID_Q5_V1(x)                                            \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q5_V1) & BIT_MASK_QUEUEMACID_Q5_V1)

#define BIT_SHIFT_QUEUEAC_Q5_V1 23
#define BIT_MASK_QUEUEAC_Q5_V1 0x3
#define BIT_QUEUEAC_Q5_V1(x)                                                   \
	(((x) & BIT_MASK_QUEUEAC_Q5_V1) << BIT_SHIFT_QUEUEAC_Q5_V1)
#define BIT_GET_QUEUEAC_Q5_V1(x)                                               \
	(((x) >> BIT_SHIFT_QUEUEAC_Q5_V1) & BIT_MASK_QUEUEAC_Q5_V1)

/* 2 REG_Q5_INFO				(Offset 0x046C) */

#define BIT_TIDEMPTY_Q5_V1 BIT(22)

/* 2 REG_Q5_INFO				(Offset 0x046C) */

#define BIT_SHIFT_TAIL_PKT_Q5_V2 11
#define BIT_MASK_TAIL_PKT_Q5_V2 0x7ff
#define BIT_TAIL_PKT_Q5_V2(x)                                                  \
	(((x) & BIT_MASK_TAIL_PKT_Q5_V2) << BIT_SHIFT_TAIL_PKT_Q5_V2)
#define BIT_GET_TAIL_PKT_Q5_V2(x)                                              \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q5_V2) & BIT_MASK_TAIL_PKT_Q5_V2)

/* 2 REG_Q5_INFO				(Offset 0x046C) */

#define BIT_SHIFT_HEAD_PKT_Q5_V1 0
#define BIT_MASK_HEAD_PKT_Q5_V1 0x7ff
#define BIT_HEAD_PKT_Q5_V1(x)                                                  \
	(((x) & BIT_MASK_HEAD_PKT_Q5_V1) << BIT_SHIFT_HEAD_PKT_Q5_V1)
#define BIT_GET_HEAD_PKT_Q5_V1(x)                                              \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q5_V1) & BIT_MASK_HEAD_PKT_Q5_V1)

/* 2 REG_Q6_INFO				(Offset 0x0470) */

#define BIT_SHIFT_QUEUEMACID_Q6_V1 25
#define BIT_MASK_QUEUEMACID_Q6_V1 0x7f
#define BIT_QUEUEMACID_Q6_V1(x)                                                \
	(((x) & BIT_MASK_QUEUEMACID_Q6_V1) << BIT_SHIFT_QUEUEMACID_Q6_V1)
#define BIT_GET_QUEUEMACID_Q6_V1(x)                                            \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q6_V1) & BIT_MASK_QUEUEMACID_Q6_V1)

#define BIT_SHIFT_QUEUEAC_Q6_V1 23
#define BIT_MASK_QUEUEAC_Q6_V1 0x3
#define BIT_QUEUEAC_Q6_V1(x)                                                   \
	(((x) & BIT_MASK_QUEUEAC_Q6_V1) << BIT_SHIFT_QUEUEAC_Q6_V1)
#define BIT_GET_QUEUEAC_Q6_V1(x)                                               \
	(((x) >> BIT_SHIFT_QUEUEAC_Q6_V1) & BIT_MASK_QUEUEAC_Q6_V1)

/* 2 REG_Q6_INFO				(Offset 0x0470) */

#define BIT_TIDEMPTY_Q6_V1 BIT(22)

/* 2 REG_Q6_INFO				(Offset 0x0470) */

#define BIT_SHIFT_TAIL_PKT_Q6_V2 11
#define BIT_MASK_TAIL_PKT_Q6_V2 0x7ff
#define BIT_TAIL_PKT_Q6_V2(x)                                                  \
	(((x) & BIT_MASK_TAIL_PKT_Q6_V2) << BIT_SHIFT_TAIL_PKT_Q6_V2)
#define BIT_GET_TAIL_PKT_Q6_V2(x)                                              \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q6_V2) & BIT_MASK_TAIL_PKT_Q6_V2)

/* 2 REG_Q6_INFO				(Offset 0x0470) */

#define BIT_SHIFT_HEAD_PKT_Q6_V1 0
#define BIT_MASK_HEAD_PKT_Q6_V1 0x7ff
#define BIT_HEAD_PKT_Q6_V1(x)                                                  \
	(((x) & BIT_MASK_HEAD_PKT_Q6_V1) << BIT_SHIFT_HEAD_PKT_Q6_V1)
#define BIT_GET_HEAD_PKT_Q6_V1(x)                                              \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q6_V1) & BIT_MASK_HEAD_PKT_Q6_V1)

/* 2 REG_Q7_INFO				(Offset 0x0474) */

#define BIT_SHIFT_QUEUEMACID_Q7_V1 25
#define BIT_MASK_QUEUEMACID_Q7_V1 0x7f
#define BIT_QUEUEMACID_Q7_V1(x)                                                \
	(((x) & BIT_MASK_QUEUEMACID_Q7_V1) << BIT_SHIFT_QUEUEMACID_Q7_V1)
#define BIT_GET_QUEUEMACID_Q7_V1(x)                                            \
	(((x) >> BIT_SHIFT_QUEUEMACID_Q7_V1) & BIT_MASK_QUEUEMACID_Q7_V1)

#define BIT_SHIFT_QUEUEAC_Q7_V1 23
#define BIT_MASK_QUEUEAC_Q7_V1 0x3
#define BIT_QUEUEAC_Q7_V1(x)                                                   \
	(((x) & BIT_MASK_QUEUEAC_Q7_V1) << BIT_SHIFT_QUEUEAC_Q7_V1)
#define BIT_GET_QUEUEAC_Q7_V1(x)                                               \
	(((x) >> BIT_SHIFT_QUEUEAC_Q7_V1) & BIT_MASK_QUEUEAC_Q7_V1)

/* 2 REG_Q7_INFO				(Offset 0x0474) */

#define BIT_TIDEMPTY_Q7_V1 BIT(22)

/* 2 REG_Q7_INFO				(Offset 0x0474) */

#define BIT_SHIFT_TAIL_PKT_Q7_V2 11
#define BIT_MASK_TAIL_PKT_Q7_V2 0x7ff
#define BIT_TAIL_PKT_Q7_V2(x)                                                  \
	(((x) & BIT_MASK_TAIL_PKT_Q7_V2) << BIT_SHIFT_TAIL_PKT_Q7_V2)
#define BIT_GET_TAIL_PKT_Q7_V2(x)                                              \
	(((x) >> BIT_SHIFT_TAIL_PKT_Q7_V2) & BIT_MASK_TAIL_PKT_Q7_V2)

/* 2 REG_Q7_INFO				(Offset 0x0474) */

#define BIT_SHIFT_HEAD_PKT_Q7_V1 0
#define BIT_MASK_HEAD_PKT_Q7_V1 0x7ff
#define BIT_HEAD_PKT_Q7_V1(x)                                                  \
	(((x) & BIT_MASK_HEAD_PKT_Q7_V1) << BIT_SHIFT_HEAD_PKT_Q7_V1)
#define BIT_GET_HEAD_PKT_Q7_V1(x)                                              \
	(((x) >> BIT_SHIFT_HEAD_PKT_Q7_V1) & BIT_MASK_HEAD_PKT_Q7_V1)

/* 2 REG_WMAC_LBK_BUF_HD_V1			(Offset 0x0478) */

#define BIT_SHIFT_WMAC_LBK_BUF_HEAD_V1 0
#define BIT_MASK_WMAC_LBK_BUF_HEAD_V1 0xfff
#define BIT_WMAC_LBK_BUF_HEAD_V1(x)                                            \
	(((x) & BIT_MASK_WMAC_LBK_BUF_HEAD_V1)                                 \
	 << BIT_SHIFT_WMAC_LBK_BUF_HEAD_V1)
#define BIT_GET_WMAC_LBK_BUF_HEAD_V1(x)                                        \
	(((x) >> BIT_SHIFT_WMAC_LBK_BUF_HEAD_V1) &                             \
	 BIT_MASK_WMAC_LBK_BUF_HEAD_V1)

/* 2 REG_MGQ_BDNY_V1				(Offset 0x047A) */

#define BIT_SHIFT_MGQ_PGBNDY_V1 0
#define BIT_MASK_MGQ_PGBNDY_V1 0xfff
#define BIT_MGQ_PGBNDY_V1(x)                                                   \
	(((x) & BIT_MASK_MGQ_PGBNDY_V1) << BIT_SHIFT_MGQ_PGBNDY_V1)
#define BIT_GET_MGQ_PGBNDY_V1(x)                                               \
	(((x) >> BIT_SHIFT_MGQ_PGBNDY_V1) & BIT_MASK_MGQ_PGBNDY_V1)

/* 2 REG_TXRPT_CTRL				(Offset 0x047C) */

#define BIT_SHIFT_TRXRPT_TIMER_TH 24
#define BIT_MASK_TRXRPT_TIMER_TH 0xff
#define BIT_TRXRPT_TIMER_TH(x)                                                 \
	(((x) & BIT_MASK_TRXRPT_TIMER_TH) << BIT_SHIFT_TRXRPT_TIMER_TH)
#define BIT_GET_TRXRPT_TIMER_TH(x)                                             \
	(((x) >> BIT_SHIFT_TRXRPT_TIMER_TH) & BIT_MASK_TRXRPT_TIMER_TH)

/* 2 REG_TXRPT_CTRL				(Offset 0x047C) */

#define BIT_SHIFT_TRXRPT_LEN_TH 16
#define BIT_MASK_TRXRPT_LEN_TH 0xff
#define BIT_TRXRPT_LEN_TH(x)                                                   \
	(((x) & BIT_MASK_TRXRPT_LEN_TH) << BIT_SHIFT_TRXRPT_LEN_TH)
#define BIT_GET_TRXRPT_LEN_TH(x)                                               \
	(((x) >> BIT_SHIFT_TRXRPT_LEN_TH) & BIT_MASK_TRXRPT_LEN_TH)

/* 2 REG_TXRPT_CTRL				(Offset 0x047C) */

#define BIT_SHIFT_TRXRPT_READ_PTR 8
#define BIT_MASK_TRXRPT_READ_PTR 0xff
#define BIT_TRXRPT_READ_PTR(x)                                                 \
	(((x) & BIT_MASK_TRXRPT_READ_PTR) << BIT_SHIFT_TRXRPT_READ_PTR)
#define BIT_GET_TRXRPT_READ_PTR(x)                                             \
	(((x) >> BIT_SHIFT_TRXRPT_READ_PTR) & BIT_MASK_TRXRPT_READ_PTR)

/* 2 REG_TXRPT_CTRL				(Offset 0x047C) */

#define BIT_SHIFT_TRXRPT_WRITE_PTR 0
#define BIT_MASK_TRXRPT_WRITE_PTR 0xff
#define BIT_TRXRPT_WRITE_PTR(x)                                                \
	(((x) & BIT_MASK_TRXRPT_WRITE_PTR) << BIT_SHIFT_TRXRPT_WRITE_PTR)
#define BIT_GET_TRXRPT_WRITE_PTR(x)                                            \
	(((x) >> BIT_SHIFT_TRXRPT_WRITE_PTR) & BIT_MASK_TRXRPT_WRITE_PTR)

/* 2 REG_INIRTS_RATE_SEL			(Offset 0x0480) */

#define BIT_LEAG_RTS_BW_DUP BIT(5)

/* 2 REG_BASIC_CFEND_RATE			(Offset 0x0481) */

#define BIT_SHIFT_BASIC_CFEND_RATE 0
#define BIT_MASK_BASIC_CFEND_RATE 0x1f
#define BIT_BASIC_CFEND_RATE(x)                                                \
	(((x) & BIT_MASK_BASIC_CFEND_RATE) << BIT_SHIFT_BASIC_CFEND_RATE)
#define BIT_GET_BASIC_CFEND_RATE(x)                                            \
	(((x) >> BIT_SHIFT_BASIC_CFEND_RATE) & BIT_MASK_BASIC_CFEND_RATE)

/* 2 REG_STBC_CFEND_RATE			(Offset 0x0482) */

#define BIT_SHIFT_STBC_CFEND_RATE 0
#define BIT_MASK_STBC_CFEND_RATE 0x1f
#define BIT_STBC_CFEND_RATE(x)                                                 \
	(((x) & BIT_MASK_STBC_CFEND_RATE) << BIT_SHIFT_STBC_CFEND_RATE)
#define BIT_GET_STBC_CFEND_RATE(x)                                             \
	(((x) >> BIT_SHIFT_STBC_CFEND_RATE) & BIT_MASK_STBC_CFEND_RATE)

/* 2 REG_DATA_SC				(Offset 0x0483) */

#define BIT_SHIFT_TXSC_40M 4
#define BIT_MASK_TXSC_40M 0xf
#define BIT_TXSC_40M(x) (((x) & BIT_MASK_TXSC_40M) << BIT_SHIFT_TXSC_40M)
#define BIT_GET_TXSC_40M(x) (((x) >> BIT_SHIFT_TXSC_40M) & BIT_MASK_TXSC_40M)

#define BIT_SHIFT_TXSC_20M 0
#define BIT_MASK_TXSC_20M 0xf
#define BIT_TXSC_20M(x) (((x) & BIT_MASK_TXSC_20M) << BIT_SHIFT_TXSC_20M)
#define BIT_GET_TXSC_20M(x) (((x) >> BIT_SHIFT_TXSC_20M) & BIT_MASK_TXSC_20M)

/* 2 REG_MACID_SLEEP3			(Offset 0x0484) */

#define BIT_SHIFT_MACID127_96_PKTSLEEP 0
#define BIT_MASK_MACID127_96_PKTSLEEP 0xffffffffL
#define BIT_MACID127_96_PKTSLEEP(x)                                            \
	(((x) & BIT_MASK_MACID127_96_PKTSLEEP)                                 \
	 << BIT_SHIFT_MACID127_96_PKTSLEEP)
#define BIT_GET_MACID127_96_PKTSLEEP(x)                                        \
	(((x) >> BIT_SHIFT_MACID127_96_PKTSLEEP) &                             \
	 BIT_MASK_MACID127_96_PKTSLEEP)

/* 2 REG_MACID_SLEEP1			(Offset 0x0488) */

#define BIT_SHIFT_MACID63_32_PKTSLEEP 0
#define BIT_MASK_MACID63_32_PKTSLEEP 0xffffffffL
#define BIT_MACID63_32_PKTSLEEP(x)                                             \
	(((x) & BIT_MASK_MACID63_32_PKTSLEEP) << BIT_SHIFT_MACID63_32_PKTSLEEP)
#define BIT_GET_MACID63_32_PKTSLEEP(x)                                         \
	(((x) >> BIT_SHIFT_MACID63_32_PKTSLEEP) & BIT_MASK_MACID63_32_PKTSLEEP)

/* 2 REG_ARFR2_V1				(Offset 0x048C) */

#define BIT_SHIFT_ARFR2_V1 0
#define BIT_MASK_ARFR2_V1 0xffffffffffffffffL
#define BIT_ARFR2_V1(x) (((x) & BIT_MASK_ARFR2_V1) << BIT_SHIFT_ARFR2_V1)
#define BIT_GET_ARFR2_V1(x) (((x) >> BIT_SHIFT_ARFR2_V1) & BIT_MASK_ARFR2_V1)

/* 2 REG_ARFR3_V1				(Offset 0x0494) */

#define BIT_SHIFT_ARFR3_V1 0
#define BIT_MASK_ARFR3_V1 0xffffffffffffffffL
#define BIT_ARFR3_V1(x) (((x) & BIT_MASK_ARFR3_V1) << BIT_SHIFT_ARFR3_V1)
#define BIT_GET_ARFR3_V1(x) (((x) >> BIT_SHIFT_ARFR3_V1) & BIT_MASK_ARFR3_V1)

/* 2 REG_ARFR4				(Offset 0x049C) */

#define BIT_SHIFT_ARFR4 0
#define BIT_MASK_ARFR4 0xffffffffffffffffL
#define BIT_ARFR4(x) (((x) & BIT_MASK_ARFR4) << BIT_SHIFT_ARFR4)
#define BIT_GET_ARFR4(x) (((x) >> BIT_SHIFT_ARFR4) & BIT_MASK_ARFR4)

/* 2 REG_ARFR5				(Offset 0x04A4) */

#define BIT_SHIFT_ARFR5 0
#define BIT_MASK_ARFR5 0xffffffffffffffffL
#define BIT_ARFR5(x) (((x) & BIT_MASK_ARFR5) << BIT_SHIFT_ARFR5)
#define BIT_GET_ARFR5(x) (((x) >> BIT_SHIFT_ARFR5) & BIT_MASK_ARFR5)

/* 2 REG_TXRPT_START_OFFSET			(Offset 0x04AC) */

#define BIT_SHIFT_MACID_MURATE_OFFSET 24
#define BIT_MASK_MACID_MURATE_OFFSET 0xff
#define BIT_MACID_MURATE_OFFSET(x)                                             \
	(((x) & BIT_MASK_MACID_MURATE_OFFSET) << BIT_SHIFT_MACID_MURATE_OFFSET)
#define BIT_GET_MACID_MURATE_OFFSET(x)                                         \
	(((x) >> BIT_SHIFT_MACID_MURATE_OFFSET) & BIT_MASK_MACID_MURATE_OFFSET)

/* 2 REG_TXRPT_START_OFFSET			(Offset 0x04AC) */

#define BIT_RPTFIFO_SIZE_OPT BIT(16)

/* 2 REG_TXRPT_START_OFFSET			(Offset 0x04AC) */

#define BIT_SHIFT_MACID_CTRL_OFFSET 8
#define BIT_MASK_MACID_CTRL_OFFSET 0xff
#define BIT_MACID_CTRL_OFFSET(x)                                               \
	(((x) & BIT_MASK_MACID_CTRL_OFFSET) << BIT_SHIFT_MACID_CTRL_OFFSET)
#define BIT_GET_MACID_CTRL_OFFSET(x)                                           \
	(((x) >> BIT_SHIFT_MACID_CTRL_OFFSET) & BIT_MASK_MACID_CTRL_OFFSET)

/* 2 REG_TXRPT_START_OFFSET			(Offset 0x04AC) */

#define BIT_SHIFT_AMPDU_TXRPT_OFFSET 0
#define BIT_MASK_AMPDU_TXRPT_OFFSET 0xff
#define BIT_AMPDU_TXRPT_OFFSET(x)                                              \
	(((x) & BIT_MASK_AMPDU_TXRPT_OFFSET) << BIT_SHIFT_AMPDU_TXRPT_OFFSET)
#define BIT_GET_AMPDU_TXRPT_OFFSET(x)                                          \
	(((x) >> BIT_SHIFT_AMPDU_TXRPT_OFFSET) & BIT_MASK_AMPDU_TXRPT_OFFSET)

/* 2 REG_POWER_STAGE1			(Offset 0x04B4) */

#define BIT_PTA_WL_PRI_MASK_CPU_MGQ BIT(31)
#define BIT_PTA_WL_PRI_MASK_BCNQ BIT(30)
#define BIT_PTA_WL_PRI_MASK_HIQ BIT(29)
#define BIT_PTA_WL_PRI_MASK_MGQ BIT(28)
#define BIT_PTA_WL_PRI_MASK_BK BIT(27)
#define BIT_PTA_WL_PRI_MASK_BE BIT(26)
#define BIT_PTA_WL_PRI_MASK_VI BIT(25)
#define BIT_PTA_WL_PRI_MASK_VO BIT(24)

/* 2 REG_POWER_STAGE1			(Offset 0x04B4) */

#define BIT_SHIFT_POWER_STAGE1 0
#define BIT_MASK_POWER_STAGE1 0xffffff
#define BIT_POWER_STAGE1(x)                                                    \
	(((x) & BIT_MASK_POWER_STAGE1) << BIT_SHIFT_POWER_STAGE1)
#define BIT_GET_POWER_STAGE1(x)                                                \
	(((x) >> BIT_SHIFT_POWER_STAGE1) & BIT_MASK_POWER_STAGE1)

/* 2 REG_POWER_STAGE2			(Offset 0x04B8) */

#define BIT__R_CTRL_PKT_POW_ADJ BIT(24)

/* 2 REG_POWER_STAGE2			(Offset 0x04B8) */

#define BIT_SHIFT_POWER_STAGE2 0
#define BIT_MASK_POWER_STAGE2 0xffffff
#define BIT_POWER_STAGE2(x)                                                    \
	(((x) & BIT_MASK_POWER_STAGE2) << BIT_SHIFT_POWER_STAGE2)
#define BIT_GET_POWER_STAGE2(x)                                                \
	(((x) >> BIT_SHIFT_POWER_STAGE2) & BIT_MASK_POWER_STAGE2)

/* 2 REG_SW_AMPDU_BURST_MODE_CTRL		(Offset 0x04BC) */

#define BIT_SHIFT_PAD_NUM_THRES 24
#define BIT_MASK_PAD_NUM_THRES 0x3f
#define BIT_PAD_NUM_THRES(x)                                                   \
	(((x) & BIT_MASK_PAD_NUM_THRES) << BIT_SHIFT_PAD_NUM_THRES)
#define BIT_GET_PAD_NUM_THRES(x)                                               \
	(((x) >> BIT_SHIFT_PAD_NUM_THRES) & BIT_MASK_PAD_NUM_THRES)

/* 2 REG_SW_AMPDU_BURST_MODE_CTRL		(Offset 0x04BC) */

#define BIT_R_DMA_THIS_QUEUE_BK BIT(23)
#define BIT_R_DMA_THIS_QUEUE_BE BIT(22)
#define BIT_R_DMA_THIS_QUEUE_VI BIT(21)
#define BIT_R_DMA_THIS_QUEUE_VO BIT(20)

#define BIT_SHIFT_R_TOTAL_LEN_TH 8
#define BIT_MASK_R_TOTAL_LEN_TH 0xfff
#define BIT_R_TOTAL_LEN_TH(x)                                                  \
	(((x) & BIT_MASK_R_TOTAL_LEN_TH) << BIT_SHIFT_R_TOTAL_LEN_TH)
#define BIT_GET_R_TOTAL_LEN_TH(x)                                              \
	(((x) >> BIT_SHIFT_R_TOTAL_LEN_TH) & BIT_MASK_R_TOTAL_LEN_TH)

/* 2 REG_SW_AMPDU_BURST_MODE_CTRL		(Offset 0x04BC) */

#define BIT_EN_NEW_EARLY BIT(7)

/* 2 REG_SW_AMPDU_BURST_MODE_CTRL		(Offset 0x04BC) */

#define BIT_PRE_TX_CMD BIT(6)

#define BIT_SHIFT_NUM_SCL_EN 4
#define BIT_MASK_NUM_SCL_EN 0x3
#define BIT_NUM_SCL_EN(x) (((x) & BIT_MASK_NUM_SCL_EN) << BIT_SHIFT_NUM_SCL_EN)
#define BIT_GET_NUM_SCL_EN(x)                                                  \
	(((x) >> BIT_SHIFT_NUM_SCL_EN) & BIT_MASK_NUM_SCL_EN)

#define BIT_BK_EN BIT(3)
#define BIT_BE_EN BIT(2)
#define BIT_VI_EN BIT(1)
#define BIT_VO_EN BIT(0)

/* 2 REG_PKT_LIFE_TIME			(Offset 0x04C0) */

#define BIT_SHIFT_PKT_LIFTIME_BEBK 16
#define BIT_MASK_PKT_LIFTIME_BEBK 0xffff
#define BIT_PKT_LIFTIME_BEBK(x)                                                \
	(((x) & BIT_MASK_PKT_LIFTIME_BEBK) << BIT_SHIFT_PKT_LIFTIME_BEBK)
#define BIT_GET_PKT_LIFTIME_BEBK(x)                                            \
	(((x) >> BIT_SHIFT_PKT_LIFTIME_BEBK) & BIT_MASK_PKT_LIFTIME_BEBK)

#define BIT_SHIFT_PKT_LIFTIME_VOVI 0
#define BIT_MASK_PKT_LIFTIME_VOVI 0xffff
#define BIT_PKT_LIFTIME_VOVI(x)                                                \
	(((x) & BIT_MASK_PKT_LIFTIME_VOVI) << BIT_SHIFT_PKT_LIFTIME_VOVI)
#define BIT_GET_PKT_LIFTIME_VOVI(x)                                            \
	(((x) >> BIT_SHIFT_PKT_LIFTIME_VOVI) & BIT_MASK_PKT_LIFTIME_VOVI)

/* 2 REG_STBC_SETTING			(Offset 0x04C4) */

#define BIT_SHIFT_CDEND_TXTIME_L 4
#define BIT_MASK_CDEND_TXTIME_L 0xf
#define BIT_CDEND_TXTIME_L(x)                                                  \
	(((x) & BIT_MASK_CDEND_TXTIME_L) << BIT_SHIFT_CDEND_TXTIME_L)
#define BIT_GET_CDEND_TXTIME_L(x)                                              \
	(((x) >> BIT_SHIFT_CDEND_TXTIME_L) & BIT_MASK_CDEND_TXTIME_L)

#define BIT_SHIFT_NESS 2
#define BIT_MASK_NESS 0x3
#define BIT_NESS(x) (((x) & BIT_MASK_NESS) << BIT_SHIFT_NESS)
#define BIT_GET_NESS(x) (((x) >> BIT_SHIFT_NESS) & BIT_MASK_NESS)

#define BIT_SHIFT_STBC_CFEND 0
#define BIT_MASK_STBC_CFEND 0x3
#define BIT_STBC_CFEND(x) (((x) & BIT_MASK_STBC_CFEND) << BIT_SHIFT_STBC_CFEND)
#define BIT_GET_STBC_CFEND(x)                                                  \
	(((x) >> BIT_SHIFT_STBC_CFEND) & BIT_MASK_STBC_CFEND)

/* 2 REG_STBC_SETTING2			(Offset 0x04C5) */

#define BIT_SHIFT_CDEND_TXTIME_H 0
#define BIT_MASK_CDEND_TXTIME_H 0x1f
#define BIT_CDEND_TXTIME_H(x)                                                  \
	(((x) & BIT_MASK_CDEND_TXTIME_H) << BIT_SHIFT_CDEND_TXTIME_H)
#define BIT_GET_CDEND_TXTIME_H(x)                                              \
	(((x) >> BIT_SHIFT_CDEND_TXTIME_H) & BIT_MASK_CDEND_TXTIME_H)

/* 2 REG_QUEUE_CTRL				(Offset 0x04C6) */

#define BIT_PTA_EDCCA_EN BIT(5)
#define BIT_PTA_WL_TX_EN BIT(4)

/* 2 REG_QUEUE_CTRL				(Offset 0x04C6) */

#define BIT_R_USE_DATA_BW BIT(3)
#define BIT_TRI_PKT_INT_MODE1 BIT(2)
#define BIT_TRI_PKT_INT_MODE0 BIT(1)
#define BIT_ACQ_MODE_SEL BIT(0)

/* 2 REG_SINGLE_AMPDU_CTRL			(Offset 0x04C7) */

#define BIT_EN_SINGLE_APMDU BIT(7)

/* 2 REG_PROT_MODE_CTRL			(Offset 0x04C8) */

#define BIT_SHIFT_RTS_MAX_AGG_NUM 24
#define BIT_MASK_RTS_MAX_AGG_NUM 0x3f
#define BIT_RTS_MAX_AGG_NUM(x)                                                 \
	(((x) & BIT_MASK_RTS_MAX_AGG_NUM) << BIT_SHIFT_RTS_MAX_AGG_NUM)
#define BIT_GET_RTS_MAX_AGG_NUM(x)                                             \
	(((x) >> BIT_SHIFT_RTS_MAX_AGG_NUM) & BIT_MASK_RTS_MAX_AGG_NUM)

#define BIT_SHIFT_MAX_AGG_NUM 16
#define BIT_MASK_MAX_AGG_NUM 0x3f
#define BIT_MAX_AGG_NUM(x)                                                     \
	(((x) & BIT_MASK_MAX_AGG_NUM) << BIT_SHIFT_MAX_AGG_NUM)
#define BIT_GET_MAX_AGG_NUM(x)                                                 \
	(((x) >> BIT_SHIFT_MAX_AGG_NUM) & BIT_MASK_MAX_AGG_NUM)

#define BIT_SHIFT_RTS_TXTIME_TH 8
#define BIT_MASK_RTS_TXTIME_TH 0xff
#define BIT_RTS_TXTIME_TH(x)                                                   \
	(((x) & BIT_MASK_RTS_TXTIME_TH) << BIT_SHIFT_RTS_TXTIME_TH)
#define BIT_GET_RTS_TXTIME_TH(x)                                               \
	(((x) >> BIT_SHIFT_RTS_TXTIME_TH) & BIT_MASK_RTS_TXTIME_TH)

#define BIT_SHIFT_RTS_LEN_TH 0
#define BIT_MASK_RTS_LEN_TH 0xff
#define BIT_RTS_LEN_TH(x) (((x) & BIT_MASK_RTS_LEN_TH) << BIT_SHIFT_RTS_LEN_TH)
#define BIT_GET_RTS_LEN_TH(x)                                                  \
	(((x) >> BIT_SHIFT_RTS_LEN_TH) & BIT_MASK_RTS_LEN_TH)

/* 2 REG_BAR_MODE_CTRL			(Offset 0x04CC) */

#define BIT_SHIFT_BAR_RTY_LMT 16
#define BIT_MASK_BAR_RTY_LMT 0x3
#define BIT_BAR_RTY_LMT(x)                                                     \
	(((x) & BIT_MASK_BAR_RTY_LMT) << BIT_SHIFT_BAR_RTY_LMT)
#define BIT_GET_BAR_RTY_LMT(x)                                                 \
	(((x) >> BIT_SHIFT_BAR_RTY_LMT) & BIT_MASK_BAR_RTY_LMT)

#define BIT_SHIFT_BAR_PKT_TXTIME_TH 8
#define BIT_MASK_BAR_PKT_TXTIME_TH 0xff
#define BIT_BAR_PKT_TXTIME_TH(x)                                               \
	(((x) & BIT_MASK_BAR_PKT_TXTIME_TH) << BIT_SHIFT_BAR_PKT_TXTIME_TH)
#define BIT_GET_BAR_PKT_TXTIME_TH(x)                                           \
	(((x) >> BIT_SHIFT_BAR_PKT_TXTIME_TH) & BIT_MASK_BAR_PKT_TXTIME_TH)

#define BIT_BAR_EN_V1 BIT(6)

#define BIT_SHIFT_BAR_PKTNUM_TH_V1 0
#define BIT_MASK_BAR_PKTNUM_TH_V1 0x3f
#define BIT_BAR_PKTNUM_TH_V1(x)                                                \
	(((x) & BIT_MASK_BAR_PKTNUM_TH_V1) << BIT_SHIFT_BAR_PKTNUM_TH_V1)
#define BIT_GET_BAR_PKTNUM_TH_V1(x)                                            \
	(((x) >> BIT_SHIFT_BAR_PKTNUM_TH_V1) & BIT_MASK_BAR_PKTNUM_TH_V1)

/* 2 REG_RA_TRY_RATE_AGG_LMT			(Offset 0x04CF) */

#define BIT_SHIFT_RA_TRY_RATE_AGG_LMT_V1 0
#define BIT_MASK_RA_TRY_RATE_AGG_LMT_V1 0x3f
#define BIT_RA_TRY_RATE_AGG_LMT_V1(x)                                          \
	(((x) & BIT_MASK_RA_TRY_RATE_AGG_LMT_V1)                               \
	 << BIT_SHIFT_RA_TRY_RATE_AGG_LMT_V1)
#define BIT_GET_RA_TRY_RATE_AGG_LMT_V1(x)                                      \
	(((x) >> BIT_SHIFT_RA_TRY_RATE_AGG_LMT_V1) &                           \
	 BIT_MASK_RA_TRY_RATE_AGG_LMT_V1)

/* 2 REG_MACID_SLEEP2			(Offset 0x04D0) */

#define BIT_SHIFT_MACID95_64PKTSLEEP 0
#define BIT_MASK_MACID95_64PKTSLEEP 0xffffffffL
#define BIT_MACID95_64PKTSLEEP(x)                                              \
	(((x) & BIT_MASK_MACID95_64PKTSLEEP) << BIT_SHIFT_MACID95_64PKTSLEEP)
#define BIT_GET_MACID95_64PKTSLEEP(x)                                          \
	(((x) >> BIT_SHIFT_MACID95_64PKTSLEEP) & BIT_MASK_MACID95_64PKTSLEEP)

/* 2 REG_MACID_SLEEP				(Offset 0x04D4) */

#define BIT_SHIFT_MACID31_0_PKTSLEEP 0
#define BIT_MASK_MACID31_0_PKTSLEEP 0xffffffffL
#define BIT_MACID31_0_PKTSLEEP(x)                                              \
	(((x) & BIT_MASK_MACID31_0_PKTSLEEP) << BIT_SHIFT_MACID31_0_PKTSLEEP)
#define BIT_GET_MACID31_0_PKTSLEEP(x)                                          \
	(((x) >> BIT_SHIFT_MACID31_0_PKTSLEEP) & BIT_MASK_MACID31_0_PKTSLEEP)

/* 2 REG_HW_SEQ0				(Offset 0x04D8) */

#define BIT_SHIFT_HW_SSN_SEQ0 0
#define BIT_MASK_HW_SSN_SEQ0 0xfff
#define BIT_HW_SSN_SEQ0(x)                                                     \
	(((x) & BIT_MASK_HW_SSN_SEQ0) << BIT_SHIFT_HW_SSN_SEQ0)
#define BIT_GET_HW_SSN_SEQ0(x)                                                 \
	(((x) >> BIT_SHIFT_HW_SSN_SEQ0) & BIT_MASK_HW_SSN_SEQ0)

/* 2 REG_HW_SEQ1				(Offset 0x04DA) */

#define BIT_SHIFT_HW_SSN_SEQ1 0
#define BIT_MASK_HW_SSN_SEQ1 0xfff
#define BIT_HW_SSN_SEQ1(x)                                                     \
	(((x) & BIT_MASK_HW_SSN_SEQ1) << BIT_SHIFT_HW_SSN_SEQ1)
#define BIT_GET_HW_SSN_SEQ1(x)                                                 \
	(((x) >> BIT_SHIFT_HW_SSN_SEQ1) & BIT_MASK_HW_SSN_SEQ1)

/* 2 REG_HW_SEQ2				(Offset 0x04DC) */

#define BIT_SHIFT_HW_SSN_SEQ2 0
#define BIT_MASK_HW_SSN_SEQ2 0xfff
#define BIT_HW_SSN_SEQ2(x)                                                     \
	(((x) & BIT_MASK_HW_SSN_SEQ2) << BIT_SHIFT_HW_SSN_SEQ2)
#define BIT_GET_HW_SSN_SEQ2(x)                                                 \
	(((x) >> BIT_SHIFT_HW_SSN_SEQ2) & BIT_MASK_HW_SSN_SEQ2)

/* 2 REG_HW_SEQ3				(Offset 0x04DE) */

#define BIT_SHIFT_HW_SSN_SEQ3 0
#define BIT_MASK_HW_SSN_SEQ3 0xfff
#define BIT_HW_SSN_SEQ3(x)                                                     \
	(((x) & BIT_MASK_HW_SSN_SEQ3) << BIT_SHIFT_HW_SSN_SEQ3)
#define BIT_GET_HW_SSN_SEQ3(x)                                                 \
	(((x) >> BIT_SHIFT_HW_SSN_SEQ3) & BIT_MASK_HW_SSN_SEQ3)

/* 2 REG_NULL_PKT_STATUS_V1			(Offset 0x04E0) */

#define BIT_SHIFT_PTCL_TOTAL_PG_V2 2
#define BIT_MASK_PTCL_TOTAL_PG_V2 0x3fff
#define BIT_PTCL_TOTAL_PG_V2(x)                                                \
	(((x) & BIT_MASK_PTCL_TOTAL_PG_V2) << BIT_SHIFT_PTCL_TOTAL_PG_V2)
#define BIT_GET_PTCL_TOTAL_PG_V2(x)                                            \
	(((x) >> BIT_SHIFT_PTCL_TOTAL_PG_V2) & BIT_MASK_PTCL_TOTAL_PG_V2)

/* 2 REG_NULL_PKT_STATUS			(Offset 0x04E0) */

#define BIT_TX_NULL_1 BIT(1)
#define BIT_TX_NULL_0 BIT(0)

/* 2 REG_PTCL_ERR_STATUS			(Offset 0x04E2) */

#define BIT_PTCL_RATE_TABLE_INVALID BIT(7)
#define BIT_FTM_T2R_ERROR BIT(6)

/* 2 REG_PTCL_ERR_STATUS			(Offset 0x04E2) */

#define BIT_PTCL_ERR0 BIT(5)
#define BIT_PTCL_ERR1 BIT(4)
#define BIT_PTCL_ERR2 BIT(3)
#define BIT_PTCL_ERR3 BIT(2)
#define BIT_PTCL_ERR4 BIT(1)
#define BIT_PTCL_ERR5 BIT(0)

/* 2 REG_NULL_PKT_STATUS_EXTEND		(Offset 0x04E3) */

#define BIT_CLI3_TX_NULL_1 BIT(7)
#define BIT_CLI3_TX_NULL_0 BIT(6)
#define BIT_CLI2_TX_NULL_1 BIT(5)
#define BIT_CLI2_TX_NULL_0 BIT(4)
#define BIT_CLI1_TX_NULL_1 BIT(3)
#define BIT_CLI1_TX_NULL_0 BIT(2)
#define BIT_CLI0_TX_NULL_1 BIT(1)

/* 2 REG_NULL_PKT_STATUS_EXTEND		(Offset 0x04E3) */

#define BIT_CLI0_TX_NULL_0 BIT(0)

/* 2 REG_VIDEO_ENHANCEMENT_FUN		(Offset 0x04E4) */

#define BIT_VIDEO_JUST_DROP BIT(1)
#define BIT_VIDEO_ENHANCEMENT_FUN_EN BIT(0)

/* 2 REG_BT_POLLUTE_PKT_CNT			(Offset 0x04E8) */

#define BIT_SHIFT_BT_POLLUTE_PKT_CNT 0
#define BIT_MASK_BT_POLLUTE_PKT_CNT 0xffff
#define BIT_BT_POLLUTE_PKT_CNT(x)                                              \
	(((x) & BIT_MASK_BT_POLLUTE_PKT_CNT) << BIT_SHIFT_BT_POLLUTE_PKT_CNT)
#define BIT_GET_BT_POLLUTE_PKT_CNT(x)                                          \
	(((x) >> BIT_SHIFT_BT_POLLUTE_PKT_CNT) & BIT_MASK_BT_POLLUTE_PKT_CNT)

/* 2 REG_PTCL_DBG				(Offset 0x04EC) */

#define BIT_SHIFT_PTCL_DBG 0
#define BIT_MASK_PTCL_DBG 0xffffffffL
#define BIT_PTCL_DBG(x) (((x) & BIT_MASK_PTCL_DBG) << BIT_SHIFT_PTCL_DBG)
#define BIT_GET_PTCL_DBG(x) (((x) >> BIT_SHIFT_PTCL_DBG) & BIT_MASK_PTCL_DBG)

/* 2 REG_CPUMGQ_TIMER_CTRL2			(Offset 0x04F4) */

#define BIT_QUEUE_MACID_AC_NOT_THE_SAME BIT(31)

#define BIT_SHIFT_GTAB_ID 28
#define BIT_MASK_GTAB_ID 0x7
#define BIT_GTAB_ID(x) (((x) & BIT_MASK_GTAB_ID) << BIT_SHIFT_GTAB_ID)
#define BIT_GET_GTAB_ID(x) (((x) >> BIT_SHIFT_GTAB_ID) & BIT_MASK_GTAB_ID)

#define BIT_SHIFT_TRI_HEAD_ADDR 16
#define BIT_MASK_TRI_HEAD_ADDR 0xfff
#define BIT_TRI_HEAD_ADDR(x)                                                   \
	(((x) & BIT_MASK_TRI_HEAD_ADDR) << BIT_SHIFT_TRI_HEAD_ADDR)
#define BIT_GET_TRI_HEAD_ADDR(x)                                               \
	(((x) >> BIT_SHIFT_TRI_HEAD_ADDR) & BIT_MASK_TRI_HEAD_ADDR)

#define BIT_QUEUE_MACID_AC_NOT_THE_SAME_V1 BIT(15)

#define BIT_SHIFT_GTAB_ID_V1 12
#define BIT_MASK_GTAB_ID_V1 0x7
#define BIT_GTAB_ID_V1(x) (((x) & BIT_MASK_GTAB_ID_V1) << BIT_SHIFT_GTAB_ID_V1)
#define BIT_GET_GTAB_ID_V1(x)                                                  \
	(((x) >> BIT_SHIFT_GTAB_ID_V1) & BIT_MASK_GTAB_ID_V1)

#define BIT_DROP_TH_EN BIT(8)

#define BIT_SHIFT_DROP_TH 0
#define BIT_MASK_DROP_TH 0xff
#define BIT_DROP_TH(x) (((x) & BIT_MASK_DROP_TH) << BIT_SHIFT_DROP_TH)
#define BIT_GET_DROP_TH(x) (((x) >> BIT_SHIFT_DROP_TH) & BIT_MASK_DROP_TH)

/* 2 REG_DUMMY_PAGE4_V1			(Offset 0x04FC) */

#define BIT_BCN_EN_EXTHWSEQ BIT(1)
#define BIT_BCN_EN_HWSEQ BIT(0)

/* 2 REG_MOREDATA				(Offset 0x04FE) */

#define BIT_MOREDATA_CTRL2_EN_V1 BIT(3)
#define BIT_MOREDATA_CTRL1_EN_V1 BIT(2)
#define BIT_PKTIN_MOREDATA_REPLACE_ENABLE_V1 BIT(0)

/* 2 REG_EDCA_VO_PARAM			(Offset 0x0500) */

#define BIT_SHIFT_TXOPLIMIT 16
#define BIT_MASK_TXOPLIMIT 0x7ff
#define BIT_TXOPLIMIT(x) (((x) & BIT_MASK_TXOPLIMIT) << BIT_SHIFT_TXOPLIMIT)
#define BIT_GET_TXOPLIMIT(x) (((x) >> BIT_SHIFT_TXOPLIMIT) & BIT_MASK_TXOPLIMIT)

#define BIT_SHIFT_CW 8
#define BIT_MASK_CW 0xff
#define BIT_CW(x) (((x) & BIT_MASK_CW) << BIT_SHIFT_CW)
#define BIT_GET_CW(x) (((x) >> BIT_SHIFT_CW) & BIT_MASK_CW)

#define BIT_SHIFT_AIFS 0
#define BIT_MASK_AIFS 0xff
#define BIT_AIFS(x) (((x) & BIT_MASK_AIFS) << BIT_SHIFT_AIFS)
#define BIT_GET_AIFS(x) (((x) >> BIT_SHIFT_AIFS) & BIT_MASK_AIFS)

/* 2 REG_BCNTCFG				(Offset 0x0510) */

#define BIT_SHIFT_BCNCW_MAX 12
#define BIT_MASK_BCNCW_MAX 0xf
#define BIT_BCNCW_MAX(x) (((x) & BIT_MASK_BCNCW_MAX) << BIT_SHIFT_BCNCW_MAX)
#define BIT_GET_BCNCW_MAX(x) (((x) >> BIT_SHIFT_BCNCW_MAX) & BIT_MASK_BCNCW_MAX)

#define BIT_SHIFT_BCNCW_MIN 8
#define BIT_MASK_BCNCW_MIN 0xf
#define BIT_BCNCW_MIN(x) (((x) & BIT_MASK_BCNCW_MIN) << BIT_SHIFT_BCNCW_MIN)
#define BIT_GET_BCNCW_MIN(x) (((x) >> BIT_SHIFT_BCNCW_MIN) & BIT_MASK_BCNCW_MIN)

#define BIT_SHIFT_BCNIFS 0
#define BIT_MASK_BCNIFS 0xff
#define BIT_BCNIFS(x) (((x) & BIT_MASK_BCNIFS) << BIT_SHIFT_BCNIFS)
#define BIT_GET_BCNIFS(x) (((x) >> BIT_SHIFT_BCNIFS) & BIT_MASK_BCNIFS)

/* 2 REG_PIFS				(Offset 0x0512) */

#define BIT_SHIFT_PIFS 0
#define BIT_MASK_PIFS 0xff
#define BIT_PIFS(x) (((x) & BIT_MASK_PIFS) << BIT_SHIFT_PIFS)
#define BIT_GET_PIFS(x) (((x) >> BIT_SHIFT_PIFS) & BIT_MASK_PIFS)

/* 2 REG_RDG_PIFS				(Offset 0x0513) */

#define BIT_SHIFT_RDG_PIFS 0
#define BIT_MASK_RDG_PIFS 0xff
#define BIT_RDG_PIFS(x) (((x) & BIT_MASK_RDG_PIFS) << BIT_SHIFT_RDG_PIFS)
#define BIT_GET_RDG_PIFS(x) (((x) >> BIT_SHIFT_RDG_PIFS) & BIT_MASK_RDG_PIFS)

/* 2 REG_SIFS				(Offset 0x0514) */

#define BIT_SHIFT_SIFS_OFDM_TRX 24
#define BIT_MASK_SIFS_OFDM_TRX 0xff
#define BIT_SIFS_OFDM_TRX(x)                                                   \
	(((x) & BIT_MASK_SIFS_OFDM_TRX) << BIT_SHIFT_SIFS_OFDM_TRX)
#define BIT_GET_SIFS_OFDM_TRX(x)                                               \
	(((x) >> BIT_SHIFT_SIFS_OFDM_TRX) & BIT_MASK_SIFS_OFDM_TRX)

#define BIT_SHIFT_SIFS_CCK_TRX 16
#define BIT_MASK_SIFS_CCK_TRX 0xff
#define BIT_SIFS_CCK_TRX(x)                                                    \
	(((x) & BIT_MASK_SIFS_CCK_TRX) << BIT_SHIFT_SIFS_CCK_TRX)
#define BIT_GET_SIFS_CCK_TRX(x)                                                \
	(((x) >> BIT_SHIFT_SIFS_CCK_TRX) & BIT_MASK_SIFS_CCK_TRX)

#define BIT_SHIFT_SIFS_OFDM_CTX 8
#define BIT_MASK_SIFS_OFDM_CTX 0xff
#define BIT_SIFS_OFDM_CTX(x)                                                   \
	(((x) & BIT_MASK_SIFS_OFDM_CTX) << BIT_SHIFT_SIFS_OFDM_CTX)
#define BIT_GET_SIFS_OFDM_CTX(x)                                               \
	(((x) >> BIT_SHIFT_SIFS_OFDM_CTX) & BIT_MASK_SIFS_OFDM_CTX)

#define BIT_SHIFT_SIFS_CCK_CTX 0
#define BIT_MASK_SIFS_CCK_CTX 0xff
#define BIT_SIFS_CCK_CTX(x)                                                    \
	(((x) & BIT_MASK_SIFS_CCK_CTX) << BIT_SHIFT_SIFS_CCK_CTX)
#define BIT_GET_SIFS_CCK_CTX(x)                                                \
	(((x) >> BIT_SHIFT_SIFS_CCK_CTX) & BIT_MASK_SIFS_CCK_CTX)

/* 2 REG_TSFTR_SYN_OFFSET			(Offset 0x0518) */

#define BIT_SHIFT_TSFTR_SNC_OFFSET 0
#define BIT_MASK_TSFTR_SNC_OFFSET 0xffff
#define BIT_TSFTR_SNC_OFFSET(x)                                                \
	(((x) & BIT_MASK_TSFTR_SNC_OFFSET) << BIT_SHIFT_TSFTR_SNC_OFFSET)
#define BIT_GET_TSFTR_SNC_OFFSET(x)                                            \
	(((x) >> BIT_SHIFT_TSFTR_SNC_OFFSET) & BIT_MASK_TSFTR_SNC_OFFSET)

/* 2 REG_AGGR_BREAK_TIME			(Offset 0x051A) */

#define BIT_SHIFT_AGGR_BK_TIME 0
#define BIT_MASK_AGGR_BK_TIME 0xff
#define BIT_AGGR_BK_TIME(x)                                                    \
	(((x) & BIT_MASK_AGGR_BK_TIME) << BIT_SHIFT_AGGR_BK_TIME)
#define BIT_GET_AGGR_BK_TIME(x)                                                \
	(((x) >> BIT_SHIFT_AGGR_BK_TIME) & BIT_MASK_AGGR_BK_TIME)

/* 2 REG_SLOT				(Offset 0x051B) */

#define BIT_SHIFT_SLOT 0
#define BIT_MASK_SLOT 0xff
#define BIT_SLOT(x) (((x) & BIT_MASK_SLOT) << BIT_SHIFT_SLOT)
#define BIT_GET_SLOT(x) (((x) >> BIT_SHIFT_SLOT) & BIT_MASK_SLOT)

/* 2 REG_TX_PTCL_CTRL			(Offset 0x0520) */

#define BIT_DIS_EDCCA BIT(15)
#define BIT_DIS_CCA BIT(14)
#define BIT_LSIG_TXOP_TXCMD_NAV BIT(13)
#define BIT_SIFS_BK_EN BIT(12)

#define BIT_SHIFT_TXQ_NAV_MSK 8
#define BIT_MASK_TXQ_NAV_MSK 0xf
#define BIT_TXQ_NAV_MSK(x)                                                     \
	(((x) & BIT_MASK_TXQ_NAV_MSK) << BIT_SHIFT_TXQ_NAV_MSK)
#define BIT_GET_TXQ_NAV_MSK(x)                                                 \
	(((x) >> BIT_SHIFT_TXQ_NAV_MSK) & BIT_MASK_TXQ_NAV_MSK)

#define BIT_DIS_CW BIT(7)
#define BIT_NAV_END_TXOP BIT(6)
#define BIT_RDG_END_TXOP BIT(5)
#define BIT_AC_INBCN_HOLD BIT(4)
#define BIT_MGTQ_TXOP_EN BIT(3)
#define BIT_MGTQ_RTSMF_EN BIT(2)
#define BIT_HIQ_RTSMF_EN BIT(1)
#define BIT_BCN_RTSMF_EN BIT(0)

/* 2 REG_TXPAUSE				(Offset 0x0522) */

#define BIT_STOP_BCN_HI_MGT BIT(7)
#define BIT_MAC_STOPBCNQ BIT(6)
#define BIT_MAC_STOPHIQ BIT(5)
#define BIT_MAC_STOPMGQ BIT(4)
#define BIT_MAC_STOPBK BIT(3)
#define BIT_MAC_STOPBE BIT(2)
#define BIT_MAC_STOPVI BIT(1)
#define BIT_MAC_STOPVO BIT(0)

/* 2 REG_DIS_TXREQ_CLR			(Offset 0x0523) */

#define BIT_DIS_BT_CCA BIT(7)

/* 2 REG_DIS_TXREQ_CLR			(Offset 0x0523) */

#define BIT_DIS_TXREQ_CLR_HI BIT(5)
#define BIT_DIS_TXREQ_CLR_MGQ BIT(4)
#define BIT_DIS_TXREQ_CLR_VO BIT(3)
#define BIT_DIS_TXREQ_CLR_VI BIT(2)
#define BIT_DIS_TXREQ_CLR_BE BIT(1)
#define BIT_DIS_TXREQ_CLR_BK BIT(0)

/* 2 REG_RD_CTRL				(Offset 0x0524) */

#define BIT_EN_CLR_TXREQ_INCCA BIT(15)
#define BIT_DIS_TX_OVER_BCNQ BIT(14)

/* 2 REG_RD_CTRL				(Offset 0x0524) */

#define BIT_EN_BCNERR_INCCCA BIT(13)

/* 2 REG_RD_CTRL				(Offset 0x0524) */

#define BIT_EDCCA_MSK_CNTDOWN_EN BIT(11)
#define BIT_DIS_TXOP_CFE BIT(10)
#define BIT_DIS_LSIG_CFE BIT(9)
#define BIT_DIS_STBC_CFE BIT(8)
#define BIT_BKQ_RD_INIT_EN BIT(7)
#define BIT_BEQ_RD_INIT_EN BIT(6)
#define BIT_VIQ_RD_INIT_EN BIT(5)
#define BIT_VOQ_RD_INIT_EN BIT(4)
#define BIT_BKQ_RD_RESP_EN BIT(3)
#define BIT_BEQ_RD_RESP_EN BIT(2)
#define BIT_VIQ_RD_RESP_EN BIT(1)
#define BIT_VOQ_RD_RESP_EN BIT(0)

/* 2 REG_MBSSID_CTRL				(Offset 0x0526) */

#define BIT_MBID_BCNQ7_EN BIT(7)
#define BIT_MBID_BCNQ6_EN BIT(6)
#define BIT_MBID_BCNQ5_EN BIT(5)
#define BIT_MBID_BCNQ4_EN BIT(4)
#define BIT_MBID_BCNQ3_EN BIT(3)
#define BIT_MBID_BCNQ2_EN BIT(2)
#define BIT_MBID_BCNQ1_EN BIT(1)
#define BIT_MBID_BCNQ0_EN BIT(0)

/* 2 REG_P2PPS_CTRL				(Offset 0x0527) */

#define BIT_P2P_CTW_ALLSTASLEEP BIT(7)
#define BIT_P2P_OFF_DISTX_EN BIT(6)
#define BIT_PWR_MGT_EN BIT(5)

/* 2 REG_P2PPS_CTRL				(Offset 0x0527) */

#define BIT_P2P_NOA1_EN BIT(2)
#define BIT_P2P_NOA0_EN BIT(1)

/* 2 REG_PKT_LIFETIME_CTRL			(Offset 0x0528) */

#define BIT_EN_P2P_CTWND1 BIT(23)

/* 2 REG_PKT_LIFETIME_CTRL			(Offset 0x0528) */

#define BIT_EN_BKF_CLR_TXREQ BIT(22)
#define BIT_EN_TSFBIT32_RST_P2P BIT(21)
#define BIT_EN_BCN_TX_BTCCA BIT(20)
#define BIT_DIS_PKT_TX_ATIM BIT(19)
#define BIT_DIS_BCN_DIS_CTN BIT(18)
#define BIT_EN_NAVEND_RST_TXOP BIT(17)
#define BIT_EN_FILTER_CCA BIT(16)

#define BIT_SHIFT_CCA_FILTER_THRS 8
#define BIT_MASK_CCA_FILTER_THRS 0xff
#define BIT_CCA_FILTER_THRS(x)                                                 \
	(((x) & BIT_MASK_CCA_FILTER_THRS) << BIT_SHIFT_CCA_FILTER_THRS)
#define BIT_GET_CCA_FILTER_THRS(x)                                             \
	(((x) >> BIT_SHIFT_CCA_FILTER_THRS) & BIT_MASK_CCA_FILTER_THRS)

#define BIT_SHIFT_EDCCA_THRS 0
#define BIT_MASK_EDCCA_THRS 0xff
#define BIT_EDCCA_THRS(x) (((x) & BIT_MASK_EDCCA_THRS) << BIT_SHIFT_EDCCA_THRS)
#define BIT_GET_EDCCA_THRS(x)                                                  \
	(((x) >> BIT_SHIFT_EDCCA_THRS) & BIT_MASK_EDCCA_THRS)

/* 2 REG_P2PPS_SPEC_STATE			(Offset 0x052B) */

#define BIT_SPEC_POWER_STATE BIT(7)
#define BIT_SPEC_CTWINDOW_ON BIT(6)
#define BIT_SPEC_BEACON_AREA_ON BIT(5)
#define BIT_SPEC_CTWIN_EARLY_DISTX BIT(4)
#define BIT_SPEC_NOA1_OFF_PERIOD BIT(3)
#define BIT_SPEC_FORCE_DOZE1 BIT(2)
#define BIT_SPEC_NOA0_OFF_PERIOD BIT(1)
#define BIT_SPEC_FORCE_DOZE0 BIT(0)

/* 2 REG_QUEUE_INCOL_THR			(Offset 0x0538) */

#define BIT_SHIFT_BK_QUEUE_THR 24
#define BIT_MASK_BK_QUEUE_THR 0xff
#define BIT_BK_QUEUE_THR(x)                                                    \
	(((x) & BIT_MASK_BK_QUEUE_THR) << BIT_SHIFT_BK_QUEUE_THR)
#define BIT_GET_BK_QUEUE_THR(x)                                                \
	(((x) >> BIT_SHIFT_BK_QUEUE_THR) & BIT_MASK_BK_QUEUE_THR)

#define BIT_SHIFT_BE_QUEUE_THR 16
#define BIT_MASK_BE_QUEUE_THR 0xff
#define BIT_BE_QUEUE_THR(x)                                                    \
	(((x) & BIT_MASK_BE_QUEUE_THR) << BIT_SHIFT_BE_QUEUE_THR)
#define BIT_GET_BE_QUEUE_THR(x)                                                \
	(((x) >> BIT_SHIFT_BE_QUEUE_THR) & BIT_MASK_BE_QUEUE_THR)

#define BIT_SHIFT_VI_QUEUE_THR 8
#define BIT_MASK_VI_QUEUE_THR 0xff
#define BIT_VI_QUEUE_THR(x)                                                    \
	(((x) & BIT_MASK_VI_QUEUE_THR) << BIT_SHIFT_VI_QUEUE_THR)
#define BIT_GET_VI_QUEUE_THR(x)                                                \
	(((x) >> BIT_SHIFT_VI_QUEUE_THR) & BIT_MASK_VI_QUEUE_THR)

#define BIT_SHIFT_VO_QUEUE_THR 0
#define BIT_MASK_VO_QUEUE_THR 0xff
#define BIT_VO_QUEUE_THR(x)                                                    \
	(((x) & BIT_MASK_VO_QUEUE_THR) << BIT_SHIFT_VO_QUEUE_THR)
#define BIT_GET_VO_QUEUE_THR(x)                                                \
	(((x) >> BIT_SHIFT_VO_QUEUE_THR) & BIT_MASK_VO_QUEUE_THR)

/* 2 REG_QUEUE_INCOL_EN			(Offset 0x053C) */

#define BIT_QUEUE_INCOL_EN BIT(16)

/* 2 REG_QUEUE_INCOL_EN			(Offset 0x053C) */

#define BIT_SHIFT_BE_TRIGGER_NUM 12
#define BIT_MASK_BE_TRIGGER_NUM 0xf
#define BIT_BE_TRIGGER_NUM(x)                                                  \
	(((x) & BIT_MASK_BE_TRIGGER_NUM) << BIT_SHIFT_BE_TRIGGER_NUM)
#define BIT_GET_BE_TRIGGER_NUM(x)                                              \
	(((x) >> BIT_SHIFT_BE_TRIGGER_NUM) & BIT_MASK_BE_TRIGGER_NUM)

/* 2 REG_QUEUE_INCOL_EN			(Offset 0x053C) */

#define BIT_SHIFT_BK_TRIGGER_NUM 8
#define BIT_MASK_BK_TRIGGER_NUM 0xf
#define BIT_BK_TRIGGER_NUM(x)                                                  \
	(((x) & BIT_MASK_BK_TRIGGER_NUM) << BIT_SHIFT_BK_TRIGGER_NUM)
#define BIT_GET_BK_TRIGGER_NUM(x)                                              \
	(((x) >> BIT_SHIFT_BK_TRIGGER_NUM) & BIT_MASK_BK_TRIGGER_NUM)

/* 2 REG_QUEUE_INCOL_EN			(Offset 0x053C) */

#define BIT_SHIFT_VI_TRIGGER_NUM 4
#define BIT_MASK_VI_TRIGGER_NUM 0xf
#define BIT_VI_TRIGGER_NUM(x)                                                  \
	(((x) & BIT_MASK_VI_TRIGGER_NUM) << BIT_SHIFT_VI_TRIGGER_NUM)
#define BIT_GET_VI_TRIGGER_NUM(x)                                              \
	(((x) >> BIT_SHIFT_VI_TRIGGER_NUM) & BIT_MASK_VI_TRIGGER_NUM)

#define BIT_SHIFT_VO_TRIGGER_NUM 0
#define BIT_MASK_VO_TRIGGER_NUM 0xf
#define BIT_VO_TRIGGER_NUM(x)                                                  \
	(((x) & BIT_MASK_VO_TRIGGER_NUM) << BIT_SHIFT_VO_TRIGGER_NUM)
#define BIT_GET_VO_TRIGGER_NUM(x)                                              \
	(((x) >> BIT_SHIFT_VO_TRIGGER_NUM) & BIT_MASK_VO_TRIGGER_NUM)

/* 2 REG_TBTT_PROHIBIT			(Offset 0x0540) */

#define BIT_SHIFT_TBTT_HOLD_TIME_AP 8
#define BIT_MASK_TBTT_HOLD_TIME_AP 0xfff
#define BIT_TBTT_HOLD_TIME_AP(x)                                               \
	(((x) & BIT_MASK_TBTT_HOLD_TIME_AP) << BIT_SHIFT_TBTT_HOLD_TIME_AP)
#define BIT_GET_TBTT_HOLD_TIME_AP(x)                                           \
	(((x) >> BIT_SHIFT_TBTT_HOLD_TIME_AP) & BIT_MASK_TBTT_HOLD_TIME_AP)

/* 2 REG_TBTT_PROHIBIT			(Offset 0x0540) */

#define BIT_SHIFT_TBTT_PROHIBIT_SETUP 0
#define BIT_MASK_TBTT_PROHIBIT_SETUP 0xf
#define BIT_TBTT_PROHIBIT_SETUP(x)                                             \
	(((x) & BIT_MASK_TBTT_PROHIBIT_SETUP) << BIT_SHIFT_TBTT_PROHIBIT_SETUP)
#define BIT_GET_TBTT_PROHIBIT_SETUP(x)                                         \
	(((x) >> BIT_SHIFT_TBTT_PROHIBIT_SETUP) & BIT_MASK_TBTT_PROHIBIT_SETUP)

/* 2 REG_P2PPS_STATE				(Offset 0x0543) */

#define BIT_POWER_STATE BIT(7)
#define BIT_CTWINDOW_ON BIT(6)
#define BIT_BEACON_AREA_ON BIT(5)
#define BIT_CTWIN_EARLY_DISTX BIT(4)
#define BIT_NOA1_OFF_PERIOD BIT(3)
#define BIT_FORCE_DOZE1 BIT(2)
#define BIT_NOA0_OFF_PERIOD BIT(1)
#define BIT_FORCE_DOZE0 BIT(0)

/* 2 REG_RD_NAV_NXT				(Offset 0x0544) */

#define BIT_SHIFT_RD_NAV_PROT_NXT 0
#define BIT_MASK_RD_NAV_PROT_NXT 0xffff
#define BIT_RD_NAV_PROT_NXT(x)                                                 \
	(((x) & BIT_MASK_RD_NAV_PROT_NXT) << BIT_SHIFT_RD_NAV_PROT_NXT)
#define BIT_GET_RD_NAV_PROT_NXT(x)                                             \
	(((x) >> BIT_SHIFT_RD_NAV_PROT_NXT) & BIT_MASK_RD_NAV_PROT_NXT)

/* 2 REG_NAV_PROT_LEN			(Offset 0x0546) */

#define BIT_SHIFT_NAV_PROT_LEN 0
#define BIT_MASK_NAV_PROT_LEN 0xffff
#define BIT_NAV_PROT_LEN(x)                                                    \
	(((x) & BIT_MASK_NAV_PROT_LEN) << BIT_SHIFT_NAV_PROT_LEN)
#define BIT_GET_NAV_PROT_LEN(x)                                                \
	(((x) >> BIT_SHIFT_NAV_PROT_LEN) & BIT_MASK_NAV_PROT_LEN)

/* 2 REG_BCN_CTRL				(Offset 0x0550) */

#define BIT_DIS_RX_BSSID_FIT BIT(6)

/* 2 REG_BCN_CTRL				(Offset 0x0550) */

#define BIT_P0_EN_TXBCN_RPT BIT(5)

/* 2 REG_BCN_CTRL				(Offset 0x0550) */

#define BIT_DIS_TSF_UDT BIT(4)
#define BIT_EN_BCN_FUNCTION BIT(3)

/* 2 REG_BCN_CTRL				(Offset 0x0550) */

#define BIT_P0_EN_RXBCN_RPT BIT(2)

/* 2 REG_BCN_CTRL				(Offset 0x0550) */

#define BIT_EN_P2P_CTWINDOW BIT(1)
#define BIT_EN_P2P_BCNQ_AREA BIT(0)

/* 2 REG_BCN_CTRL_CLINT0			(Offset 0x0551) */

#define BIT_CLI0_DIS_RX_BSSID_FIT BIT(6)

/* 2 REG_BCN_CTRL_CLINT0			(Offset 0x0551) */

#define BIT_CLI0_DIS_TSF_UDT BIT(4)

/* 2 REG_BCN_CTRL_CLINT0			(Offset 0x0551) */

#define BIT_CLI0_EN_BCN_FUNCTION BIT(3)

/* 2 REG_BCN_CTRL_CLINT0			(Offset 0x0551) */

#define BIT_CLI0_EN_RXBCN_RPT BIT(2)

/* 2 REG_BCN_CTRL_CLINT0			(Offset 0x0551) */

#define BIT_CLI0_ENP2P_CTWINDOW BIT(1)
#define BIT_CLI0_ENP2P_BCNQ_AREA BIT(0)

/* 2 REG_MBID_NUM				(Offset 0x0552) */

#define BIT_EN_PRE_DL_BEACON BIT(3)

#define BIT_SHIFT_MBID_BCN_NUM 0
#define BIT_MASK_MBID_BCN_NUM 0x7
#define BIT_MBID_BCN_NUM(x)                                                    \
	(((x) & BIT_MASK_MBID_BCN_NUM) << BIT_SHIFT_MBID_BCN_NUM)
#define BIT_GET_MBID_BCN_NUM(x)                                                \
	(((x) >> BIT_SHIFT_MBID_BCN_NUM) & BIT_MASK_MBID_BCN_NUM)

/* 2 REG_DUAL_TSF_RST			(Offset 0x0553) */

#define BIT_FREECNT_RST BIT(5)

/* 2 REG_DUAL_TSF_RST			(Offset 0x0553) */

#define BIT_TSFTR_CLI3_RST BIT(4)

/* 2 REG_DUAL_TSF_RST			(Offset 0x0553) */

#define BIT_TSFTR_CLI2_RST BIT(3)

/* 2 REG_DUAL_TSF_RST			(Offset 0x0553) */

#define BIT_TSFTR_CLI1_RST BIT(2)

/* 2 REG_DUAL_TSF_RST			(Offset 0x0553) */

#define BIT_TSFTR_CLI0_RST BIT(1)

/* 2 REG_DUAL_TSF_RST			(Offset 0x0553) */

#define BIT_TSFTR_RST BIT(0)

/* 2 REG_MBSSID_BCN_SPACE			(Offset 0x0554) */

#define BIT_SHIFT_BCN_TIMER_SEL_FWRD 28
#define BIT_MASK_BCN_TIMER_SEL_FWRD 0x7
#define BIT_BCN_TIMER_SEL_FWRD(x)                                              \
	(((x) & BIT_MASK_BCN_TIMER_SEL_FWRD) << BIT_SHIFT_BCN_TIMER_SEL_FWRD)
#define BIT_GET_BCN_TIMER_SEL_FWRD(x)                                          \
	(((x) >> BIT_SHIFT_BCN_TIMER_SEL_FWRD) & BIT_MASK_BCN_TIMER_SEL_FWRD)

/* 2 REG_MBSSID_BCN_SPACE			(Offset 0x0554) */

#define BIT_SHIFT_BCN_SPACE_CLINT0 16
#define BIT_MASK_BCN_SPACE_CLINT0 0xfff
#define BIT_BCN_SPACE_CLINT0(x)                                                \
	(((x) & BIT_MASK_BCN_SPACE_CLINT0) << BIT_SHIFT_BCN_SPACE_CLINT0)
#define BIT_GET_BCN_SPACE_CLINT0(x)                                            \
	(((x) >> BIT_SHIFT_BCN_SPACE_CLINT0) & BIT_MASK_BCN_SPACE_CLINT0)

/* 2 REG_MBSSID_BCN_SPACE			(Offset 0x0554) */

#define BIT_SHIFT_BCN_SPACE0 0
#define BIT_MASK_BCN_SPACE0 0xffff
#define BIT_BCN_SPACE0(x) (((x) & BIT_MASK_BCN_SPACE0) << BIT_SHIFT_BCN_SPACE0)
#define BIT_GET_BCN_SPACE0(x)                                                  \
	(((x) >> BIT_SHIFT_BCN_SPACE0) & BIT_MASK_BCN_SPACE0)

/* 2 REG_DRVERLYINT				(Offset 0x0558) */

#define BIT_SHIFT_DRVERLYITV 0
#define BIT_MASK_DRVERLYITV 0xff
#define BIT_DRVERLYITV(x) (((x) & BIT_MASK_DRVERLYITV) << BIT_SHIFT_DRVERLYITV)
#define BIT_GET_DRVERLYITV(x)                                                  \
	(((x) >> BIT_SHIFT_DRVERLYITV) & BIT_MASK_DRVERLYITV)

/* 2 REG_BCNDMATIM				(Offset 0x0559) */

#define BIT_SHIFT_BCNDMATIM 0
#define BIT_MASK_BCNDMATIM 0xff
#define BIT_BCNDMATIM(x) (((x) & BIT_MASK_BCNDMATIM) << BIT_SHIFT_BCNDMATIM)
#define BIT_GET_BCNDMATIM(x) (((x) >> BIT_SHIFT_BCNDMATIM) & BIT_MASK_BCNDMATIM)

/* 2 REG_ATIMWND				(Offset 0x055A) */

#define BIT_SHIFT_ATIMWND0 0
#define BIT_MASK_ATIMWND0 0xffff
#define BIT_ATIMWND0(x) (((x) & BIT_MASK_ATIMWND0) << BIT_SHIFT_ATIMWND0)
#define BIT_GET_ATIMWND0(x) (((x) >> BIT_SHIFT_ATIMWND0) & BIT_MASK_ATIMWND0)

/* 2 REG_USTIME_TSF				(Offset 0x055C) */

#define BIT_SHIFT_USTIME_TSF_V1 0
#define BIT_MASK_USTIME_TSF_V1 0xff
#define BIT_USTIME_TSF_V1(x)                                                   \
	(((x) & BIT_MASK_USTIME_TSF_V1) << BIT_SHIFT_USTIME_TSF_V1)
#define BIT_GET_USTIME_TSF_V1(x)                                               \
	(((x) >> BIT_SHIFT_USTIME_TSF_V1) & BIT_MASK_USTIME_TSF_V1)

/* 2 REG_BCN_MAX_ERR				(Offset 0x055D) */

#define BIT_SHIFT_BCN_MAX_ERR 0
#define BIT_MASK_BCN_MAX_ERR 0xff
#define BIT_BCN_MAX_ERR(x)                                                     \
	(((x) & BIT_MASK_BCN_MAX_ERR) << BIT_SHIFT_BCN_MAX_ERR)
#define BIT_GET_BCN_MAX_ERR(x)                                                 \
	(((x) >> BIT_SHIFT_BCN_MAX_ERR) & BIT_MASK_BCN_MAX_ERR)

/* 2 REG_RXTSF_OFFSET_CCK			(Offset 0x055E) */

#define BIT_SHIFT_CCK_RXTSF_OFFSET 0
#define BIT_MASK_CCK_RXTSF_OFFSET 0xff
#define BIT_CCK_RXTSF_OFFSET(x)                                                \
	(((x) & BIT_MASK_CCK_RXTSF_OFFSET) << BIT_SHIFT_CCK_RXTSF_OFFSET)
#define BIT_GET_CCK_RXTSF_OFFSET(x)                                            \
	(((x) >> BIT_SHIFT_CCK_RXTSF_OFFSET) & BIT_MASK_CCK_RXTSF_OFFSET)

/* 2 REG_RXTSF_OFFSET_OFDM			(Offset 0x055F) */

#define BIT_SHIFT_OFDM_RXTSF_OFFSET 0
#define BIT_MASK_OFDM_RXTSF_OFFSET 0xff
#define BIT_OFDM_RXTSF_OFFSET(x)                                               \
	(((x) & BIT_MASK_OFDM_RXTSF_OFFSET) << BIT_SHIFT_OFDM_RXTSF_OFFSET)
#define BIT_GET_OFDM_RXTSF_OFFSET(x)                                           \
	(((x) >> BIT_SHIFT_OFDM_RXTSF_OFFSET) & BIT_MASK_OFDM_RXTSF_OFFSET)

/* 2 REG_TSFTR				(Offset 0x0560) */

#define BIT_SHIFT_TSF_TIMER 0
#define BIT_MASK_TSF_TIMER 0xffffffffffffffffL
#define BIT_TSF_TIMER(x) (((x) & BIT_MASK_TSF_TIMER) << BIT_SHIFT_TSF_TIMER)
#define BIT_GET_TSF_TIMER(x) (((x) >> BIT_SHIFT_TSF_TIMER) & BIT_MASK_TSF_TIMER)

/* 2 REG_FREERUN_CNT				(Offset 0x0568) */

#define BIT_SHIFT_FREERUN_CNT 0
#define BIT_MASK_FREERUN_CNT 0xffffffffffffffffL
#define BIT_FREERUN_CNT(x)                                                     \
	(((x) & BIT_MASK_FREERUN_CNT) << BIT_SHIFT_FREERUN_CNT)
#define BIT_GET_FREERUN_CNT(x)                                                 \
	(((x) >> BIT_SHIFT_FREERUN_CNT) & BIT_MASK_FREERUN_CNT)

/* 2 REG_ATIMWND1_V1				(Offset 0x0570) */

#define BIT_SHIFT_ATIMWND1_V1 0
#define BIT_MASK_ATIMWND1_V1 0xff
#define BIT_ATIMWND1_V1(x)                                                     \
	(((x) & BIT_MASK_ATIMWND1_V1) << BIT_SHIFT_ATIMWND1_V1)
#define BIT_GET_ATIMWND1_V1(x)                                                 \
	(((x) >> BIT_SHIFT_ATIMWND1_V1) & BIT_MASK_ATIMWND1_V1)

/* 2 REG_TBTT_PROHIBIT_INFRA			(Offset 0x0571) */

#define BIT_SHIFT_TBTT_PROHIBIT_INFRA 0
#define BIT_MASK_TBTT_PROHIBIT_INFRA 0xff
#define BIT_TBTT_PROHIBIT_INFRA(x)                                             \
	(((x) & BIT_MASK_TBTT_PROHIBIT_INFRA) << BIT_SHIFT_TBTT_PROHIBIT_INFRA)
#define BIT_GET_TBTT_PROHIBIT_INFRA(x)                                         \
	(((x) >> BIT_SHIFT_TBTT_PROHIBIT_INFRA) & BIT_MASK_TBTT_PROHIBIT_INFRA)

/* 2 REG_CTWND				(Offset 0x0572) */

#define BIT_SHIFT_CTWND 0
#define BIT_MASK_CTWND 0xff
#define BIT_CTWND(x) (((x) & BIT_MASK_CTWND) << BIT_SHIFT_CTWND)
#define BIT_GET_CTWND(x) (((x) >> BIT_SHIFT_CTWND) & BIT_MASK_CTWND)

/* 2 REG_BCNIVLCUNT				(Offset 0x0573) */

#define BIT_SHIFT_BCNIVLCUNT 0
#define BIT_MASK_BCNIVLCUNT 0x7f
#define BIT_BCNIVLCUNT(x) (((x) & BIT_MASK_BCNIVLCUNT) << BIT_SHIFT_BCNIVLCUNT)
#define BIT_GET_BCNIVLCUNT(x)                                                  \
	(((x) >> BIT_SHIFT_BCNIVLCUNT) & BIT_MASK_BCNIVLCUNT)

/* 2 REG_BCNDROPCTRL				(Offset 0x0574) */

#define BIT_BEACON_DROP_EN BIT(7)

#define BIT_SHIFT_BEACON_DROP_IVL 0
#define BIT_MASK_BEACON_DROP_IVL 0x7f
#define BIT_BEACON_DROP_IVL(x)                                                 \
	(((x) & BIT_MASK_BEACON_DROP_IVL) << BIT_SHIFT_BEACON_DROP_IVL)
#define BIT_GET_BEACON_DROP_IVL(x)                                             \
	(((x) >> BIT_SHIFT_BEACON_DROP_IVL) & BIT_MASK_BEACON_DROP_IVL)

/* 2 REG_HGQ_TIMEOUT_PERIOD			(Offset 0x0575) */

#define BIT_SHIFT_HGQ_TIMEOUT_PERIOD 0
#define BIT_MASK_HGQ_TIMEOUT_PERIOD 0xff
#define BIT_HGQ_TIMEOUT_PERIOD(x)                                              \
	(((x) & BIT_MASK_HGQ_TIMEOUT_PERIOD) << BIT_SHIFT_HGQ_TIMEOUT_PERIOD)
#define BIT_GET_HGQ_TIMEOUT_PERIOD(x)                                          \
	(((x) >> BIT_SHIFT_HGQ_TIMEOUT_PERIOD) & BIT_MASK_HGQ_TIMEOUT_PERIOD)

/* 2 REG_TXCMD_TIMEOUT_PERIOD		(Offset 0x0576) */

#define BIT_SHIFT_TXCMD_TIMEOUT_PERIOD 0
#define BIT_MASK_TXCMD_TIMEOUT_PERIOD 0xff
#define BIT_TXCMD_TIMEOUT_PERIOD(x)                                            \
	(((x) & BIT_MASK_TXCMD_TIMEOUT_PERIOD)                                 \
	 << BIT_SHIFT_TXCMD_TIMEOUT_PERIOD)
#define BIT_GET_TXCMD_TIMEOUT_PERIOD(x)                                        \
	(((x) >> BIT_SHIFT_TXCMD_TIMEOUT_PERIOD) &                             \
	 BIT_MASK_TXCMD_TIMEOUT_PERIOD)

/* 2 REG_MISC_CTRL				(Offset 0x0577) */

#define BIT_DIS_TRX_CAL_BCN BIT(5)
#define BIT_DIS_TX_CAL_TBTT BIT(4)
#define BIT_EN_FREECNT BIT(3)
#define BIT_BCN_AGGRESSION BIT(2)

#define BIT_SHIFT_DIS_SECONDARY_CCA 0
#define BIT_MASK_DIS_SECONDARY_CCA 0x3
#define BIT_DIS_SECONDARY_CCA(x)                                               \
	(((x) & BIT_MASK_DIS_SECONDARY_CCA) << BIT_SHIFT_DIS_SECONDARY_CCA)
#define BIT_GET_DIS_SECONDARY_CCA(x)                                           \
	(((x) >> BIT_SHIFT_DIS_SECONDARY_CCA) & BIT_MASK_DIS_SECONDARY_CCA)

/* 2 REG_BCN_CTRL_CLINT1			(Offset 0x0578) */

#define BIT_CLI1_DIS_RX_BSSID_FIT BIT(6)
#define BIT_CLI1_DIS_TSF_UDT BIT(4)
#define BIT_CLI1_EN_BCN_FUNCTION BIT(3)

/* 2 REG_BCN_CTRL_CLINT1			(Offset 0x0578) */

#define BIT_CLI1_EN_RXBCN_RPT BIT(2)

/* 2 REG_BCN_CTRL_CLINT1			(Offset 0x0578) */

#define BIT_CLI1_ENP2P_CTWINDOW BIT(1)
#define BIT_CLI1_ENP2P_BCNQ_AREA BIT(0)

/* 2 REG_BCN_CTRL_CLINT2			(Offset 0x0579) */

#define BIT_CLI2_DIS_RX_BSSID_FIT BIT(6)
#define BIT_CLI2_DIS_TSF_UDT BIT(4)
#define BIT_CLI2_EN_BCN_FUNCTION BIT(3)

/* 2 REG_BCN_CTRL_CLINT2			(Offset 0x0579) */

#define BIT_CLI2_EN_RXBCN_RPT BIT(2)

/* 2 REG_BCN_CTRL_CLINT2			(Offset 0x0579) */

#define BIT_CLI2_ENP2P_CTWINDOW BIT(1)
#define BIT_CLI2_ENP2P_BCNQ_AREA BIT(0)

/* 2 REG_BCN_CTRL_CLINT3			(Offset 0x057A) */

#define BIT_CLI3_DIS_RX_BSSID_FIT BIT(6)
#define BIT_CLI3_DIS_TSF_UDT BIT(4)
#define BIT_CLI3_EN_BCN_FUNCTION BIT(3)

/* 2 REG_BCN_CTRL_CLINT3			(Offset 0x057A) */

#define BIT_CLI3_EN_RXBCN_RPT BIT(2)

/* 2 REG_BCN_CTRL_CLINT3			(Offset 0x057A) */

#define BIT_CLI3_ENP2P_CTWINDOW BIT(1)
#define BIT_CLI3_ENP2P_BCNQ_AREA BIT(0)

/* 2 REG_EXTEND_CTRL				(Offset 0x057B) */

#define BIT_EN_TSFBIT32_RST_P2P2 BIT(5)
#define BIT_EN_TSFBIT32_RST_P2P1 BIT(4)

#define BIT_SHIFT_PORT_SEL 0
#define BIT_MASK_PORT_SEL 0x7
#define BIT_PORT_SEL(x) (((x) & BIT_MASK_PORT_SEL) << BIT_SHIFT_PORT_SEL)
#define BIT_GET_PORT_SEL(x) (((x) >> BIT_SHIFT_PORT_SEL) & BIT_MASK_PORT_SEL)

/* 2 REG_P2PPS1_SPEC_STATE			(Offset 0x057C) */

#define BIT_P2P1_SPEC_POWER_STATE BIT(7)
#define BIT_P2P1_SPEC_CTWINDOW_ON BIT(6)
#define BIT_P2P1_SPEC_BCN_AREA_ON BIT(5)
#define BIT_P2P1_SPEC_CTWIN_EARLY_DISTX BIT(4)
#define BIT_P2P1_SPEC_NOA1_OFF_PERIOD BIT(3)
#define BIT_P2P1_SPEC_FORCE_DOZE1 BIT(2)
#define BIT_P2P1_SPEC_NOA0_OFF_PERIOD BIT(1)
#define BIT_P2P1_SPEC_FORCE_DOZE0 BIT(0)

/* 2 REG_P2PPS1_STATE			(Offset 0x057D) */

#define BIT_P2P1_POWER_STATE BIT(7)
#define BIT_P2P1_CTWINDOW_ON BIT(6)
#define BIT_P2P1_BEACON_AREA_ON BIT(5)
#define BIT_P2P1_CTWIN_EARLY_DISTX BIT(4)
#define BIT_P2P1_NOA1_OFF_PERIOD BIT(3)
#define BIT_P2P1_FORCE_DOZE1 BIT(2)
#define BIT_P2P1_NOA0_OFF_PERIOD BIT(1)
#define BIT_P2P1_FORCE_DOZE0 BIT(0)

/* 2 REG_P2PPS2_SPEC_STATE			(Offset 0x057E) */

#define BIT_P2P2_SPEC_POWER_STATE BIT(7)
#define BIT_P2P2_SPEC_CTWINDOW_ON BIT(6)
#define BIT_P2P2_SPEC_BCN_AREA_ON BIT(5)
#define BIT_P2P2_SPEC_CTWIN_EARLY_DISTX BIT(4)
#define BIT_P2P2_SPEC_NOA1_OFF_PERIOD BIT(3)
#define BIT_P2P2_SPEC_FORCE_DOZE1 BIT(2)
#define BIT_P2P2_SPEC_NOA0_OFF_PERIOD BIT(1)
#define BIT_P2P2_SPEC_FORCE_DOZE0 BIT(0)

/* 2 REG_P2PPS2_STATE			(Offset 0x057F) */

#define BIT_P2P2_POWER_STATE BIT(7)
#define BIT_P2P2_CTWINDOW_ON BIT(6)
#define BIT_P2P2_BEACON_AREA_ON BIT(5)
#define BIT_P2P2_CTWIN_EARLY_DISTX BIT(4)
#define BIT_P2P2_NOA1_OFF_PERIOD BIT(3)
#define BIT_P2P2_FORCE_DOZE1 BIT(2)
#define BIT_P2P2_NOA0_OFF_PERIOD BIT(1)
#define BIT_P2P2_FORCE_DOZE0 BIT(0)

/* 2 REG_PS_TIMER0				(Offset 0x0580) */

#define BIT_SHIFT_PSTIMER0_INT 5
#define BIT_MASK_PSTIMER0_INT 0x7ffffff
#define BIT_PSTIMER0_INT(x)                                                    \
	(((x) & BIT_MASK_PSTIMER0_INT) << BIT_SHIFT_PSTIMER0_INT)
#define BIT_GET_PSTIMER0_INT(x)                                                \
	(((x) >> BIT_SHIFT_PSTIMER0_INT) & BIT_MASK_PSTIMER0_INT)

/* 2 REG_PS_TIMER1				(Offset 0x0584) */

#define BIT_SHIFT_PSTIMER1_INT 5
#define BIT_MASK_PSTIMER1_INT 0x7ffffff
#define BIT_PSTIMER1_INT(x)                                                    \
	(((x) & BIT_MASK_PSTIMER1_INT) << BIT_SHIFT_PSTIMER1_INT)
#define BIT_GET_PSTIMER1_INT(x)                                                \
	(((x) >> BIT_SHIFT_PSTIMER1_INT) & BIT_MASK_PSTIMER1_INT)

/* 2 REG_PS_TIMER2				(Offset 0x0588) */

#define BIT_SHIFT_PSTIMER2_INT 5
#define BIT_MASK_PSTIMER2_INT 0x7ffffff
#define BIT_PSTIMER2_INT(x)                                                    \
	(((x) & BIT_MASK_PSTIMER2_INT) << BIT_SHIFT_PSTIMER2_INT)
#define BIT_GET_PSTIMER2_INT(x)                                                \
	(((x) >> BIT_SHIFT_PSTIMER2_INT) & BIT_MASK_PSTIMER2_INT)

/* 2 REG_TBTT_CTN_AREA			(Offset 0x058C) */

#define BIT_SHIFT_TBTT_CTN_AREA 0
#define BIT_MASK_TBTT_CTN_AREA 0xff
#define BIT_TBTT_CTN_AREA(x)                                                   \
	(((x) & BIT_MASK_TBTT_CTN_AREA) << BIT_SHIFT_TBTT_CTN_AREA)
#define BIT_GET_TBTT_CTN_AREA(x)                                               \
	(((x) >> BIT_SHIFT_TBTT_CTN_AREA) & BIT_MASK_TBTT_CTN_AREA)

/* 2 REG_FORCE_BCN_IFS			(Offset 0x058E) */

#define BIT_SHIFT_FORCE_BCN_IFS 0
#define BIT_MASK_FORCE_BCN_IFS 0xff
#define BIT_FORCE_BCN_IFS(x)                                                   \
	(((x) & BIT_MASK_FORCE_BCN_IFS) << BIT_SHIFT_FORCE_BCN_IFS)
#define BIT_GET_FORCE_BCN_IFS(x)                                               \
	(((x) >> BIT_SHIFT_FORCE_BCN_IFS) & BIT_MASK_FORCE_BCN_IFS)

/* 2 REG_TXOP_MIN				(Offset 0x0590) */

#define BIT_SHIFT_TXOP_MIN 0
#define BIT_MASK_TXOP_MIN 0x3fff
#define BIT_TXOP_MIN(x) (((x) & BIT_MASK_TXOP_MIN) << BIT_SHIFT_TXOP_MIN)
#define BIT_GET_TXOP_MIN(x) (((x) >> BIT_SHIFT_TXOP_MIN) & BIT_MASK_TXOP_MIN)

/* 2 REG_PRE_BKF_TIME			(Offset 0x0592) */

#define BIT_SHIFT_PRE_BKF_TIME 0
#define BIT_MASK_PRE_BKF_TIME 0xff
#define BIT_PRE_BKF_TIME(x)                                                    \
	(((x) & BIT_MASK_PRE_BKF_TIME) << BIT_SHIFT_PRE_BKF_TIME)
#define BIT_GET_PRE_BKF_TIME(x)                                                \
	(((x) >> BIT_SHIFT_PRE_BKF_TIME) & BIT_MASK_PRE_BKF_TIME)

/* 2 REG_CROSS_TXOP_CTRL			(Offset 0x0593) */

#define BIT_DTIM_BYPASS BIT(2)
#define BIT_RTS_NAV_TXOP BIT(1)
#define BIT_NOT_CROSS_TXOP BIT(0)

/* 2 REG_ATIMWND2				(Offset 0x05A0) */

#define BIT_SHIFT_ATIMWND2 0
#define BIT_MASK_ATIMWND2 0xff
#define BIT_ATIMWND2(x) (((x) & BIT_MASK_ATIMWND2) << BIT_SHIFT_ATIMWND2)
#define BIT_GET_ATIMWND2(x) (((x) >> BIT_SHIFT_ATIMWND2) & BIT_MASK_ATIMWND2)

/* 2 REG_ATIMWND3				(Offset 0x05A1) */

#define BIT_SHIFT_ATIMWND3 0
#define BIT_MASK_ATIMWND3 0xff
#define BIT_ATIMWND3(x) (((x) & BIT_MASK_ATIMWND3) << BIT_SHIFT_ATIMWND3)
#define BIT_GET_ATIMWND3(x) (((x) >> BIT_SHIFT_ATIMWND3) & BIT_MASK_ATIMWND3)

/* 2 REG_ATIMWND4				(Offset 0x05A2) */

#define BIT_SHIFT_ATIMWND4 0
#define BIT_MASK_ATIMWND4 0xff
#define BIT_ATIMWND4(x) (((x) & BIT_MASK_ATIMWND4) << BIT_SHIFT_ATIMWND4)
#define BIT_GET_ATIMWND4(x) (((x) >> BIT_SHIFT_ATIMWND4) & BIT_MASK_ATIMWND4)

/* 2 REG_ATIMWND5				(Offset 0x05A3) */

#define BIT_SHIFT_ATIMWND5 0
#define BIT_MASK_ATIMWND5 0xff
#define BIT_ATIMWND5(x) (((x) & BIT_MASK_ATIMWND5) << BIT_SHIFT_ATIMWND5)
#define BIT_GET_ATIMWND5(x) (((x) >> BIT_SHIFT_ATIMWND5) & BIT_MASK_ATIMWND5)

/* 2 REG_ATIMWND6				(Offset 0x05A4) */

#define BIT_SHIFT_ATIMWND6 0
#define BIT_MASK_ATIMWND6 0xff
#define BIT_ATIMWND6(x) (((x) & BIT_MASK_ATIMWND6) << BIT_SHIFT_ATIMWND6)
#define BIT_GET_ATIMWND6(x) (((x) >> BIT_SHIFT_ATIMWND6) & BIT_MASK_ATIMWND6)

/* 2 REG_ATIMWND7				(Offset 0x05A5) */

#define BIT_SHIFT_ATIMWND7 0
#define BIT_MASK_ATIMWND7 0xff
#define BIT_ATIMWND7(x) (((x) & BIT_MASK_ATIMWND7) << BIT_SHIFT_ATIMWND7)
#define BIT_GET_ATIMWND7(x) (((x) >> BIT_SHIFT_ATIMWND7) & BIT_MASK_ATIMWND7)

/* 2 REG_ATIMUGT				(Offset 0x05A6) */

#define BIT_SHIFT_ATIM_URGENT 0
#define BIT_MASK_ATIM_URGENT 0xff
#define BIT_ATIM_URGENT(x)                                                     \
	(((x) & BIT_MASK_ATIM_URGENT) << BIT_SHIFT_ATIM_URGENT)
#define BIT_GET_ATIM_URGENT(x)                                                 \
	(((x) >> BIT_SHIFT_ATIM_URGENT) & BIT_MASK_ATIM_URGENT)

/* 2 REG_HIQ_NO_LMT_EN			(Offset 0x05A7) */

#define BIT_HIQ_NO_LMT_EN_VAP7 BIT(7)
#define BIT_HIQ_NO_LMT_EN_VAP6 BIT(6)
#define BIT_HIQ_NO_LMT_EN_VAP5 BIT(5)
#define BIT_HIQ_NO_LMT_EN_VAP4 BIT(4)
#define BIT_HIQ_NO_LMT_EN_VAP3 BIT(3)
#define BIT_HIQ_NO_LMT_EN_VAP2 BIT(2)
#define BIT_HIQ_NO_LMT_EN_VAP1 BIT(1)
#define BIT_HIQ_NO_LMT_EN_ROOT BIT(0)

/* 2 REG_DTIM_COUNTER_ROOT			(Offset 0x05A8) */

#define BIT_SHIFT_DTIM_COUNT_ROOT 0
#define BIT_MASK_DTIM_COUNT_ROOT 0xff
#define BIT_DTIM_COUNT_ROOT(x)                                                 \
	(((x) & BIT_MASK_DTIM_COUNT_ROOT) << BIT_SHIFT_DTIM_COUNT_ROOT)
#define BIT_GET_DTIM_COUNT_ROOT(x)                                             \
	(((x) >> BIT_SHIFT_DTIM_COUNT_ROOT) & BIT_MASK_DTIM_COUNT_ROOT)

/* 2 REG_DTIM_COUNTER_VAP1			(Offset 0x05A9) */

#define BIT_SHIFT_DTIM_COUNT_VAP1 0
#define BIT_MASK_DTIM_COUNT_VAP1 0xff
#define BIT_DTIM_COUNT_VAP1(x)                                                 \
	(((x) & BIT_MASK_DTIM_COUNT_VAP1) << BIT_SHIFT_DTIM_COUNT_VAP1)
#define BIT_GET_DTIM_COUNT_VAP1(x)                                             \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP1) & BIT_MASK_DTIM_COUNT_VAP1)

/* 2 REG_DTIM_COUNTER_VAP2			(Offset 0x05AA) */

#define BIT_SHIFT_DTIM_COUNT_VAP2 0
#define BIT_MASK_DTIM_COUNT_VAP2 0xff
#define BIT_DTIM_COUNT_VAP2(x)                                                 \
	(((x) & BIT_MASK_DTIM_COUNT_VAP2) << BIT_SHIFT_DTIM_COUNT_VAP2)
#define BIT_GET_DTIM_COUNT_VAP2(x)                                             \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP2) & BIT_MASK_DTIM_COUNT_VAP2)

/* 2 REG_DTIM_COUNTER_VAP3			(Offset 0x05AB) */

#define BIT_SHIFT_DTIM_COUNT_VAP3 0
#define BIT_MASK_DTIM_COUNT_VAP3 0xff
#define BIT_DTIM_COUNT_VAP3(x)                                                 \
	(((x) & BIT_MASK_DTIM_COUNT_VAP3) << BIT_SHIFT_DTIM_COUNT_VAP3)
#define BIT_GET_DTIM_COUNT_VAP3(x)                                             \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP3) & BIT_MASK_DTIM_COUNT_VAP3)

/* 2 REG_DTIM_COUNTER_VAP4			(Offset 0x05AC) */

#define BIT_SHIFT_DTIM_COUNT_VAP4 0
#define BIT_MASK_DTIM_COUNT_VAP4 0xff
#define BIT_DTIM_COUNT_VAP4(x)                                                 \
	(((x) & BIT_MASK_DTIM_COUNT_VAP4) << BIT_SHIFT_DTIM_COUNT_VAP4)
#define BIT_GET_DTIM_COUNT_VAP4(x)                                             \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP4) & BIT_MASK_DTIM_COUNT_VAP4)

/* 2 REG_DTIM_COUNTER_VAP5			(Offset 0x05AD) */

#define BIT_SHIFT_DTIM_COUNT_VAP5 0
#define BIT_MASK_DTIM_COUNT_VAP5 0xff
#define BIT_DTIM_COUNT_VAP5(x)                                                 \
	(((x) & BIT_MASK_DTIM_COUNT_VAP5) << BIT_SHIFT_DTIM_COUNT_VAP5)
#define BIT_GET_DTIM_COUNT_VAP5(x)                                             \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP5) & BIT_MASK_DTIM_COUNT_VAP5)

/* 2 REG_DTIM_COUNTER_VAP6			(Offset 0x05AE) */

#define BIT_SHIFT_DTIM_COUNT_VAP6 0
#define BIT_MASK_DTIM_COUNT_VAP6 0xff
#define BIT_DTIM_COUNT_VAP6(x)                                                 \
	(((x) & BIT_MASK_DTIM_COUNT_VAP6) << BIT_SHIFT_DTIM_COUNT_VAP6)
#define BIT_GET_DTIM_COUNT_VAP6(x)                                             \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP6) & BIT_MASK_DTIM_COUNT_VAP6)

/* 2 REG_DTIM_COUNTER_VAP7			(Offset 0x05AF) */

#define BIT_SHIFT_DTIM_COUNT_VAP7 0
#define BIT_MASK_DTIM_COUNT_VAP7 0xff
#define BIT_DTIM_COUNT_VAP7(x)                                                 \
	(((x) & BIT_MASK_DTIM_COUNT_VAP7) << BIT_SHIFT_DTIM_COUNT_VAP7)
#define BIT_GET_DTIM_COUNT_VAP7(x)                                             \
	(((x) >> BIT_SHIFT_DTIM_COUNT_VAP7) & BIT_MASK_DTIM_COUNT_VAP7)

/* 2 REG_DIS_ATIM				(Offset 0x05B0) */

#define BIT_DIS_ATIM_VAP7 BIT(7)
#define BIT_DIS_ATIM_VAP6 BIT(6)
#define BIT_DIS_ATIM_VAP5 BIT(5)
#define BIT_DIS_ATIM_VAP4 BIT(4)
#define BIT_DIS_ATIM_VAP3 BIT(3)
#define BIT_DIS_ATIM_VAP2 BIT(2)
#define BIT_DIS_ATIM_VAP1 BIT(1)
#define BIT_DIS_ATIM_ROOT BIT(0)

/* 2 REG_EARLY_128US				(Offset 0x05B1) */

#define BIT_SHIFT_TSFT_SEL_TIMER1 3
#define BIT_MASK_TSFT_SEL_TIMER1 0x7
#define BIT_TSFT_SEL_TIMER1(x)                                                 \
	(((x) & BIT_MASK_TSFT_SEL_TIMER1) << BIT_SHIFT_TSFT_SEL_TIMER1)
#define BIT_GET_TSFT_SEL_TIMER1(x)                                             \
	(((x) >> BIT_SHIFT_TSFT_SEL_TIMER1) & BIT_MASK_TSFT_SEL_TIMER1)

#define BIT_SHIFT_EARLY_128US 0
#define BIT_MASK_EARLY_128US 0x7
#define BIT_EARLY_128US(x)                                                     \
	(((x) & BIT_MASK_EARLY_128US) << BIT_SHIFT_EARLY_128US)
#define BIT_GET_EARLY_128US(x)                                                 \
	(((x) >> BIT_SHIFT_EARLY_128US) & BIT_MASK_EARLY_128US)

/* 2 REG_P2PPS1_CTRL				(Offset 0x05B2) */

#define BIT_P2P1_CTW_ALLSTASLEEP BIT(7)
#define BIT_P2P1_OFF_DISTX_EN BIT(6)
#define BIT_P2P1_PWR_MGT_EN BIT(5)
#define BIT_P2P1_NOA1_EN BIT(2)
#define BIT_P2P1_NOA0_EN BIT(1)

/* 2 REG_P2PPS2_CTRL				(Offset 0x05B3) */

#define BIT_P2P2_CTW_ALLSTASLEEP BIT(7)
#define BIT_P2P2_OFF_DISTX_EN BIT(6)
#define BIT_P2P2_PWR_MGT_EN BIT(5)
#define BIT_P2P2_NOA1_EN BIT(2)
#define BIT_P2P2_NOA0_EN BIT(1)

/* 2 REG_TIMER0_SRC_SEL			(Offset 0x05B4) */

#define BIT_SHIFT_SYNC_CLI_SEL 4
#define BIT_MASK_SYNC_CLI_SEL 0x7
#define BIT_SYNC_CLI_SEL(x)                                                    \
	(((x) & BIT_MASK_SYNC_CLI_SEL) << BIT_SHIFT_SYNC_CLI_SEL)
#define BIT_GET_SYNC_CLI_SEL(x)                                                \
	(((x) >> BIT_SHIFT_SYNC_CLI_SEL) & BIT_MASK_SYNC_CLI_SEL)

#define BIT_SHIFT_TSFT_SEL_TIMER0 0
#define BIT_MASK_TSFT_SEL_TIMER0 0x7
#define BIT_TSFT_SEL_TIMER0(x)                                                 \
	(((x) & BIT_MASK_TSFT_SEL_TIMER0) << BIT_SHIFT_TSFT_SEL_TIMER0)
#define BIT_GET_TSFT_SEL_TIMER0(x)                                             \
	(((x) >> BIT_SHIFT_TSFT_SEL_TIMER0) & BIT_MASK_TSFT_SEL_TIMER0)

/* 2 REG_NOA_UNIT_SEL			(Offset 0x05B5) */

#define BIT_SHIFT_NOA_UNIT2_SEL 8
#define BIT_MASK_NOA_UNIT2_SEL 0x7
#define BIT_NOA_UNIT2_SEL(x)                                                   \
	(((x) & BIT_MASK_NOA_UNIT2_SEL) << BIT_SHIFT_NOA_UNIT2_SEL)
#define BIT_GET_NOA_UNIT2_SEL(x)                                               \
	(((x) >> BIT_SHIFT_NOA_UNIT2_SEL) & BIT_MASK_NOA_UNIT2_SEL)

#define BIT_SHIFT_NOA_UNIT1_SEL 4
#define BIT_MASK_NOA_UNIT1_SEL 0x7
#define BIT_NOA_UNIT1_SEL(x)                                                   \
	(((x) & BIT_MASK_NOA_UNIT1_SEL) << BIT_SHIFT_NOA_UNIT1_SEL)
#define BIT_GET_NOA_UNIT1_SEL(x)                                               \
	(((x) >> BIT_SHIFT_NOA_UNIT1_SEL) & BIT_MASK_NOA_UNIT1_SEL)

#define BIT_SHIFT_NOA_UNIT0_SEL 0
#define BIT_MASK_NOA_UNIT0_SEL 0x7
#define BIT_NOA_UNIT0_SEL(x)                                                   \
	(((x) & BIT_MASK_NOA_UNIT0_SEL) << BIT_SHIFT_NOA_UNIT0_SEL)
#define BIT_GET_NOA_UNIT0_SEL(x)                                               \
	(((x) >> BIT_SHIFT_NOA_UNIT0_SEL) & BIT_MASK_NOA_UNIT0_SEL)

/* 2 REG_P2POFF_DIS_TXTIME			(Offset 0x05B7) */

#define BIT_SHIFT_P2POFF_DIS_TXTIME 0
#define BIT_MASK_P2POFF_DIS_TXTIME 0xff
#define BIT_P2POFF_DIS_TXTIME(x)                                               \
	(((x) & BIT_MASK_P2POFF_DIS_TXTIME) << BIT_SHIFT_P2POFF_DIS_TXTIME)
#define BIT_GET_P2POFF_DIS_TXTIME(x)                                           \
	(((x) >> BIT_SHIFT_P2POFF_DIS_TXTIME) & BIT_MASK_P2POFF_DIS_TXTIME)

/* 2 REG_MBSSID_BCN_SPACE2			(Offset 0x05B8) */

#define BIT_SHIFT_BCN_SPACE_CLINT2 16
#define BIT_MASK_BCN_SPACE_CLINT2 0xfff
#define BIT_BCN_SPACE_CLINT2(x)                                                \
	(((x) & BIT_MASK_BCN_SPACE_CLINT2) << BIT_SHIFT_BCN_SPACE_CLINT2)
#define BIT_GET_BCN_SPACE_CLINT2(x)                                            \
	(((x) >> BIT_SHIFT_BCN_SPACE_CLINT2) & BIT_MASK_BCN_SPACE_CLINT2)

#define BIT_SHIFT_BCN_SPACE_CLINT1 0
#define BIT_MASK_BCN_SPACE_CLINT1 0xfff
#define BIT_BCN_SPACE_CLINT1(x)                                                \
	(((x) & BIT_MASK_BCN_SPACE_CLINT1) << BIT_SHIFT_BCN_SPACE_CLINT1)
#define BIT_GET_BCN_SPACE_CLINT1(x)                                            \
	(((x) >> BIT_SHIFT_BCN_SPACE_CLINT1) & BIT_MASK_BCN_SPACE_CLINT1)

/* 2 REG_MBSSID_BCN_SPACE3			(Offset 0x05BC) */

#define BIT_SHIFT_SUB_BCN_SPACE 16
#define BIT_MASK_SUB_BCN_SPACE 0xff
#define BIT_SUB_BCN_SPACE(x)                                                   \
	(((x) & BIT_MASK_SUB_BCN_SPACE) << BIT_SHIFT_SUB_BCN_SPACE)
#define BIT_GET_SUB_BCN_SPACE(x)                                               \
	(((x) >> BIT_SHIFT_SUB_BCN_SPACE) & BIT_MASK_SUB_BCN_SPACE)

/* 2 REG_MBSSID_BCN_SPACE3			(Offset 0x05BC) */

#define BIT_SHIFT_BCN_SPACE_CLINT3 0
#define BIT_MASK_BCN_SPACE_CLINT3 0xfff
#define BIT_BCN_SPACE_CLINT3(x)                                                \
	(((x) & BIT_MASK_BCN_SPACE_CLINT3) << BIT_SHIFT_BCN_SPACE_CLINT3)
#define BIT_GET_BCN_SPACE_CLINT3(x)                                            \
	(((x) >> BIT_SHIFT_BCN_SPACE_CLINT3) & BIT_MASK_BCN_SPACE_CLINT3)

/* 2 REG_ACMHWCTRL				(Offset 0x05C0) */

#define BIT_BEQ_ACM_STATUS BIT(7)
#define BIT_VIQ_ACM_STATUS BIT(6)
#define BIT_VOQ_ACM_STATUS BIT(5)
#define BIT_BEQ_ACM_EN BIT(3)
#define BIT_VIQ_ACM_EN BIT(2)
#define BIT_VOQ_ACM_EN BIT(1)
#define BIT_ACMHWEN BIT(0)

/* 2 REG_ACMRSTCTRL				(Offset 0x05C1) */

#define BIT_BE_ACM_RESET_USED_TIME BIT(2)
#define BIT_VI_ACM_RESET_USED_TIME BIT(1)
#define BIT_VO_ACM_RESET_USED_TIME BIT(0)

/* 2 REG_ACMAVG				(Offset 0x05C2) */

#define BIT_SHIFT_AVGPERIOD 0
#define BIT_MASK_AVGPERIOD 0xffff
#define BIT_AVGPERIOD(x) (((x) & BIT_MASK_AVGPERIOD) << BIT_SHIFT_AVGPERIOD)
#define BIT_GET_AVGPERIOD(x) (((x) >> BIT_SHIFT_AVGPERIOD) & BIT_MASK_AVGPERIOD)

/* 2 REG_VO_ADMTIME				(Offset 0x05C4) */

#define BIT_SHIFT_VO_ADMITTED_TIME 0
#define BIT_MASK_VO_ADMITTED_TIME 0xffff
#define BIT_VO_ADMITTED_TIME(x)                                                \
	(((x) & BIT_MASK_VO_ADMITTED_TIME) << BIT_SHIFT_VO_ADMITTED_TIME)
#define BIT_GET_VO_ADMITTED_TIME(x)                                            \
	(((x) >> BIT_SHIFT_VO_ADMITTED_TIME) & BIT_MASK_VO_ADMITTED_TIME)

/* 2 REG_VI_ADMTIME				(Offset 0x05C6) */

#define BIT_SHIFT_VI_ADMITTED_TIME 0
#define BIT_MASK_VI_ADMITTED_TIME 0xffff
#define BIT_VI_ADMITTED_TIME(x)                                                \
	(((x) & BIT_MASK_VI_ADMITTED_TIME) << BIT_SHIFT_VI_ADMITTED_TIME)
#define BIT_GET_VI_ADMITTED_TIME(x)                                            \
	(((x) >> BIT_SHIFT_VI_ADMITTED_TIME) & BIT_MASK_VI_ADMITTED_TIME)

/* 2 REG_BE_ADMTIME				(Offset 0x05C8) */

#define BIT_SHIFT_BE_ADMITTED_TIME 0
#define BIT_MASK_BE_ADMITTED_TIME 0xffff
#define BIT_BE_ADMITTED_TIME(x)                                                \
	(((x) & BIT_MASK_BE_ADMITTED_TIME) << BIT_SHIFT_BE_ADMITTED_TIME)
#define BIT_GET_BE_ADMITTED_TIME(x)                                            \
	(((x) >> BIT_SHIFT_BE_ADMITTED_TIME) & BIT_MASK_BE_ADMITTED_TIME)

/* 2 REG_EDCA_RANDOM_GEN			(Offset 0x05CC) */

#define BIT_SHIFT_RANDOM_GEN 0
#define BIT_MASK_RANDOM_GEN 0xffffff
#define BIT_RANDOM_GEN(x) (((x) & BIT_MASK_RANDOM_GEN) << BIT_SHIFT_RANDOM_GEN)
#define BIT_GET_RANDOM_GEN(x)                                                  \
	(((x) >> BIT_SHIFT_RANDOM_GEN) & BIT_MASK_RANDOM_GEN)

/* 2 REG_TXCMD_NOA_SEL			(Offset 0x05CF) */

#define BIT_SHIFT_NOA_SEL 4
#define BIT_MASK_NOA_SEL 0x7
#define BIT_NOA_SEL(x) (((x) & BIT_MASK_NOA_SEL) << BIT_SHIFT_NOA_SEL)
#define BIT_GET_NOA_SEL(x) (((x) >> BIT_SHIFT_NOA_SEL) & BIT_MASK_NOA_SEL)

/* 2 REG_TXCMD_NOA_SEL			(Offset 0x05CF) */

#define BIT_SHIFT_TXCMD_SEG_SEL 0
#define BIT_MASK_TXCMD_SEG_SEL 0xf
#define BIT_TXCMD_SEG_SEL(x)                                                   \
	(((x) & BIT_MASK_TXCMD_SEG_SEL) << BIT_SHIFT_TXCMD_SEG_SEL)
#define BIT_GET_TXCMD_SEG_SEL(x)                                               \
	(((x) >> BIT_SHIFT_TXCMD_SEG_SEL) & BIT_MASK_TXCMD_SEG_SEL)

/* 2 REG_NOA_PARAM				(Offset 0x05E0) */

#define BIT_SHIFT_NOA_COUNT (96 & CPU_OPT_WIDTH)
#define BIT_MASK_NOA_COUNT 0xff
#define BIT_NOA_COUNT(x) (((x) & BIT_MASK_NOA_COUNT) << BIT_SHIFT_NOA_COUNT)
#define BIT_GET_NOA_COUNT(x) (((x) >> BIT_SHIFT_NOA_COUNT) & BIT_MASK_NOA_COUNT)

#define BIT_SHIFT_NOA_START_TIME (64 & CPU_OPT_WIDTH)
#define BIT_MASK_NOA_START_TIME 0xffffffffL
#define BIT_NOA_START_TIME(x)                                                  \
	(((x) & BIT_MASK_NOA_START_TIME) << BIT_SHIFT_NOA_START_TIME)
#define BIT_GET_NOA_START_TIME(x)                                              \
	(((x) >> BIT_SHIFT_NOA_START_TIME) & BIT_MASK_NOA_START_TIME)

#define BIT_SHIFT_NOA_INTERVAL (32 & CPU_OPT_WIDTH)
#define BIT_MASK_NOA_INTERVAL 0xffffffffL
#define BIT_NOA_INTERVAL(x)                                                    \
	(((x) & BIT_MASK_NOA_INTERVAL) << BIT_SHIFT_NOA_INTERVAL)
#define BIT_GET_NOA_INTERVAL(x)                                                \
	(((x) >> BIT_SHIFT_NOA_INTERVAL) & BIT_MASK_NOA_INTERVAL)

#define BIT_SHIFT_NOA_DURATION 0
#define BIT_MASK_NOA_DURATION 0xffffffffL
#define BIT_NOA_DURATION(x)                                                    \
	(((x) & BIT_MASK_NOA_DURATION) << BIT_SHIFT_NOA_DURATION)
#define BIT_GET_NOA_DURATION(x)                                                \
	(((x) >> BIT_SHIFT_NOA_DURATION) & BIT_MASK_NOA_DURATION)

/* 2 REG_P2P_RST				(Offset 0x05F0) */

#define BIT_P2P2_PWR_RST1 BIT(5)
#define BIT_P2P2_PWR_RST0 BIT(4)
#define BIT_P2P1_PWR_RST1 BIT(3)
#define BIT_P2P1_PWR_RST0 BIT(2)
#define BIT_P2P_PWR_RST1_V1 BIT(1)
#define BIT_P2P_PWR_RST0_V1 BIT(0)

/* 2 REG_SCHEDULER_RST			(Offset 0x05F1) */

#define BIT_SYNC_CLI BIT(1)
#define BIT_SCHEDULER_RST_V1 BIT(0)

/* 2 REG_SCH_TXCMD				(Offset 0x05F8) */

#define BIT_SHIFT_SCH_TXCMD 0
#define BIT_MASK_SCH_TXCMD 0xffffffffL
#define BIT_SCH_TXCMD(x) (((x) & BIT_MASK_SCH_TXCMD) << BIT_SHIFT_SCH_TXCMD)
#define BIT_GET_SCH_TXCMD(x) (((x) >> BIT_SHIFT_SCH_TXCMD) & BIT_MASK_SCH_TXCMD)

/* 2 REG_WMAC_CR				(Offset 0x0600) */

#define BIT_IC_MACPHY_M BIT(0)

/* 2 REG_WMAC_FWPKT_CR			(Offset 0x0601) */

#define BIT_FWEN BIT(7)

/* 2 REG_WMAC_FWPKT_CR			(Offset 0x0601) */

#define BIT_PHYSTS_PKT_CTRL BIT(6)

/* 2 REG_WMAC_FWPKT_CR			(Offset 0x0601) */

#define BIT_APPHDR_MIDSRCH_FAIL BIT(4)
#define BIT_FWPARSING_EN BIT(3)

#define BIT_SHIFT_APPEND_MHDR_LEN 0
#define BIT_MASK_APPEND_MHDR_LEN 0x7
#define BIT_APPEND_MHDR_LEN(x)                                                 \
	(((x) & BIT_MASK_APPEND_MHDR_LEN) << BIT_SHIFT_APPEND_MHDR_LEN)
#define BIT_GET_APPEND_MHDR_LEN(x)                                             \
	(((x) >> BIT_SHIFT_APPEND_MHDR_LEN) & BIT_MASK_APPEND_MHDR_LEN)

/* 2 REG_TCR					(Offset 0x0604) */

#define BIT_WMAC_EN_RTS_ADDR BIT(31)
#define BIT_WMAC_DISABLE_CCK BIT(30)
#define BIT_WMAC_RAW_LEN BIT(29)
#define BIT_WMAC_NOTX_IN_RXNDP BIT(28)
#define BIT_WMAC_EN_EOF BIT(27)
#define BIT_WMAC_BF_SEL BIT(26)
#define BIT_WMAC_ANTMODE_SEL BIT(25)

/* 2 REG_TCR					(Offset 0x0604) */

#define BIT_WMAC_TCRPWRMGT_HWCTL BIT(24)

/* 2 REG_TCR					(Offset 0x0604) */

#define BIT_WMAC_SMOOTH_VAL BIT(23)

/* 2 REG_TCR					(Offset 0x0604) */

#define BIT_FETCH_MPDU_AFTER_WSEC_RDY BIT(20)

/* 2 REG_TCR					(Offset 0x0604) */

#define BIT_WMAC_TCR_EN_20MST BIT(19)
#define BIT_WMAC_DIS_SIGTA BIT(18)
#define BIT_WMAC_DIS_A2B0 BIT(17)
#define BIT_WMAC_MSK_SIGBCRC BIT(16)

/* 2 REG_TCR					(Offset 0x0604) */

#define BIT_WMAC_TCR_ERRSTEN_3 BIT(15)
#define BIT_WMAC_TCR_ERRSTEN_2 BIT(14)
#define BIT_WMAC_TCR_ERRSTEN_1 BIT(13)
#define BIT_WMAC_TCR_ERRSTEN_0 BIT(12)
#define BIT_WMAC_TCR_TXSK_PERPKT BIT(11)
#define BIT_ICV BIT(10)
#define BIT_CFEND_FORMAT BIT(9)
#define BIT_CRC BIT(8)
#define BIT_PWRBIT_OW_EN BIT(7)
#define BIT_PWR_ST BIT(6)
#define BIT_WMAC_TCR_UPD_TIMIE BIT(5)
#define BIT_WMAC_TCR_UPD_HGQMD BIT(4)

/* 2 REG_TCR					(Offset 0x0604) */

#define BIT_VHTSIGA1_TXPS BIT(3)

/* 2 REG_TCR					(Offset 0x0604) */

#define BIT_PAD_SEL BIT(2)
#define BIT_DIS_GCLK BIT(1)

/* 2 REG_RCR					(Offset 0x0608) */

#define BIT_APP_FCS BIT(31)
#define BIT_APP_MIC BIT(30)
#define BIT_APP_ICV BIT(29)
#define BIT_APP_PHYSTS BIT(28)
#define BIT_APP_BASSN BIT(27)

/* 2 REG_RCR					(Offset 0x0608) */

#define BIT_VHT_DACK BIT(26)

/* 2 REG_RCR					(Offset 0x0608) */

#define BIT_TCPOFLD_EN BIT(25)
#define BIT_ENMBID BIT(24)
#define BIT_LSIGEN BIT(23)
#define BIT_MFBEN BIT(22)
#define BIT_DISCHKPPDLLEN BIT(21)
#define BIT_PKTCTL_DLEN BIT(20)
#define BIT_TIM_PARSER_EN BIT(18)
#define BIT_BC_MD_EN BIT(17)
#define BIT_UC_MD_EN BIT(16)
#define BIT_RXSK_PERPKT BIT(15)
#define BIT_HTC_LOC_CTRL BIT(14)

/* 2 REG_RCR					(Offset 0x0608) */

#define BIT_RPFM_CAM_ENABLE BIT(12)

/* 2 REG_RCR					(Offset 0x0608) */

#define BIT_TA_BCN BIT(11)

/* 2 REG_RCR					(Offset 0x0608) */

#define BIT_DISDECMYPKT BIT(10)
#define BIT_AICV BIT(9)
#define BIT_ACRC32 BIT(8)
#define BIT_CBSSID_BCN BIT(7)
#define BIT_CBSSID_DATA BIT(6)
#define BIT_APWRMGT BIT(5)
#define BIT_ADD3 BIT(4)
#define BIT_AB BIT(3)
#define BIT_AM BIT(2)
#define BIT_APM BIT(1)
#define BIT_AAP BIT(0)

/* 2 REG_RX_PKT_LIMIT			(Offset 0x060C) */

#define BIT_SHIFT_RXPKTLMT 0
#define BIT_MASK_RXPKTLMT 0x3f
#define BIT_RXPKTLMT(x) (((x) & BIT_MASK_RXPKTLMT) << BIT_SHIFT_RXPKTLMT)
#define BIT_GET_RXPKTLMT(x) (((x) >> BIT_SHIFT_RXPKTLMT) & BIT_MASK_RXPKTLMT)

/* 2 REG_RX_DLK_TIME				(Offset 0x060D) */

#define BIT_SHIFT_RX_DLK_TIME 0
#define BIT_MASK_RX_DLK_TIME 0xff
#define BIT_RX_DLK_TIME(x)                                                     \
	(((x) & BIT_MASK_RX_DLK_TIME) << BIT_SHIFT_RX_DLK_TIME)
#define BIT_GET_RX_DLK_TIME(x)                                                 \
	(((x) >> BIT_SHIFT_RX_DLK_TIME) & BIT_MASK_RX_DLK_TIME)

/* 2 REG_RX_DRVINFO_SZ			(Offset 0x060F) */

#define BIT_DATA_RPFM15EN BIT(15)
#define BIT_DATA_RPFM14EN BIT(14)
#define BIT_DATA_RPFM13EN BIT(13)
#define BIT_DATA_RPFM12EN BIT(12)
#define BIT_DATA_RPFM11EN BIT(11)
#define BIT_DATA_RPFM10EN BIT(10)
#define BIT_DATA_RPFM9EN BIT(9)
#define BIT_DATA_RPFM8EN BIT(8)

/* 2 REG_RX_DRVINFO_SZ			(Offset 0x060F) */

#define BIT_PHYSTS_PER_PKT_MODE BIT(7)
#define BIT_DATA_RPFM7EN BIT(7)

/* 2 REG_RX_DRVINFO_SZ			(Offset 0x060F) */

#define BIT_DATA_RPFM6EN BIT(6)

/* 2 REG_RX_DRVINFO_SZ			(Offset 0x060F) */

#define BIT_DATA_RPFM5EN BIT(5)
#define BIT_DATA_RPFM4EN BIT(4)
#define BIT_DATA_RPFM3EN BIT(3)
#define BIT_DATA_RPFM2EN BIT(2)
#define BIT_DATA_RPFM1EN BIT(1)

/* 2 REG_RX_DRVINFO_SZ			(Offset 0x060F) */

#define BIT_SHIFT_DRVINFO_SZ_V1 0
#define BIT_MASK_DRVINFO_SZ_V1 0xf
#define BIT_DRVINFO_SZ_V1(x)                                                   \
	(((x) & BIT_MASK_DRVINFO_SZ_V1) << BIT_SHIFT_DRVINFO_SZ_V1)
#define BIT_GET_DRVINFO_SZ_V1(x)                                               \
	(((x) >> BIT_SHIFT_DRVINFO_SZ_V1) & BIT_MASK_DRVINFO_SZ_V1)

/* 2 REG_RX_DRVINFO_SZ			(Offset 0x060F) */

#define BIT_DATA_RPFM0EN BIT(0)

/* 2 REG_MACID				(Offset 0x0610) */

#define BIT_SHIFT_MACID 0
#define BIT_MASK_MACID 0xffffffffffffL
#define BIT_MACID(x) (((x) & BIT_MASK_MACID) << BIT_SHIFT_MACID)
#define BIT_GET_MACID(x) (((x) >> BIT_SHIFT_MACID) & BIT_MASK_MACID)

/* 2 REG_BSSID				(Offset 0x0618) */

#define BIT_SHIFT_BSSID 0
#define BIT_MASK_BSSID 0xffffffffffffL
#define BIT_BSSID(x) (((x) & BIT_MASK_BSSID) << BIT_SHIFT_BSSID)
#define BIT_GET_BSSID(x) (((x) >> BIT_SHIFT_BSSID) & BIT_MASK_BSSID)

/* 2 REG_MAR					(Offset 0x0620) */

#define BIT_SHIFT_MAR 0
#define BIT_MASK_MAR 0xffffffffffffffffL
#define BIT_MAR(x) (((x) & BIT_MASK_MAR) << BIT_SHIFT_MAR)
#define BIT_GET_MAR(x) (((x) >> BIT_SHIFT_MAR) & BIT_MASK_MAR)

/* 2 REG_MBIDCAMCFG_1			(Offset 0x0628) */

#define BIT_SHIFT_MBIDCAM_RWDATA_L 0
#define BIT_MASK_MBIDCAM_RWDATA_L 0xffffffffL
#define BIT_MBIDCAM_RWDATA_L(x)                                                \
	(((x) & BIT_MASK_MBIDCAM_RWDATA_L) << BIT_SHIFT_MBIDCAM_RWDATA_L)
#define BIT_GET_MBIDCAM_RWDATA_L(x)                                            \
	(((x) >> BIT_SHIFT_MBIDCAM_RWDATA_L) & BIT_MASK_MBIDCAM_RWDATA_L)

/* 2 REG_MBIDCAMCFG_2			(Offset 0x062C) */

#define BIT_MBIDCAM_POLL BIT(31)
#define BIT_MBIDCAM_WT_EN BIT(30)

#define BIT_SHIFT_MBIDCAM_ADDR 24
#define BIT_MASK_MBIDCAM_ADDR 0x1f
#define BIT_MBIDCAM_ADDR(x)                                                    \
	(((x) & BIT_MASK_MBIDCAM_ADDR) << BIT_SHIFT_MBIDCAM_ADDR)
#define BIT_GET_MBIDCAM_ADDR(x)                                                \
	(((x) >> BIT_SHIFT_MBIDCAM_ADDR) & BIT_MASK_MBIDCAM_ADDR)

#define BIT_MBIDCAM_VALID BIT(23)
#define BIT_LSIC_TXOP_EN BIT(17)

/* 2 REG_MBIDCAMCFG_2			(Offset 0x062C) */

#define BIT_CTS_EN BIT(16)

/* 2 REG_MBIDCAMCFG_2			(Offset 0x062C) */

#define BIT_SHIFT_MBIDCAM_RWDATA_H 0
#define BIT_MASK_MBIDCAM_RWDATA_H 0xffff
#define BIT_MBIDCAM_RWDATA_H(x)                                                \
	(((x) & BIT_MASK_MBIDCAM_RWDATA_H) << BIT_SHIFT_MBIDCAM_RWDATA_H)
#define BIT_GET_MBIDCAM_RWDATA_H(x)                                            \
	(((x) >> BIT_SHIFT_MBIDCAM_RWDATA_H) & BIT_MASK_MBIDCAM_RWDATA_H)

/* 2 REG_WMAC_TCR_TSFT_OFS			(Offset 0x0630) */

#define BIT_SHIFT_WMAC_TCR_TSFT_OFS 0
#define BIT_MASK_WMAC_TCR_TSFT_OFS 0xffff
#define BIT_WMAC_TCR_TSFT_OFS(x)                                               \
	(((x) & BIT_MASK_WMAC_TCR_TSFT_OFS) << BIT_SHIFT_WMAC_TCR_TSFT_OFS)
#define BIT_GET_WMAC_TCR_TSFT_OFS(x)                                           \
	(((x) >> BIT_SHIFT_WMAC_TCR_TSFT_OFS) & BIT_MASK_WMAC_TCR_TSFT_OFS)

/* 2 REG_UDF_THSD				(Offset 0x0632) */

#define BIT_SHIFT_UDF_THSD 0
#define BIT_MASK_UDF_THSD 0xff
#define BIT_UDF_THSD(x) (((x) & BIT_MASK_UDF_THSD) << BIT_SHIFT_UDF_THSD)
#define BIT_GET_UDF_THSD(x) (((x) >> BIT_SHIFT_UDF_THSD) & BIT_MASK_UDF_THSD)

/* 2 REG_ZLD_NUM				(Offset 0x0633) */

#define BIT_SHIFT_ZLD_NUM 0
#define BIT_MASK_ZLD_NUM 0xff
#define BIT_ZLD_NUM(x) (((x) & BIT_MASK_ZLD_NUM) << BIT_SHIFT_ZLD_NUM)
#define BIT_GET_ZLD_NUM(x) (((x) >> BIT_SHIFT_ZLD_NUM) & BIT_MASK_ZLD_NUM)

/* 2 REG_STMP_THSD				(Offset 0x0634) */

#define BIT_SHIFT_STMP_THSD 0
#define BIT_MASK_STMP_THSD 0xff
#define BIT_STMP_THSD(x) (((x) & BIT_MASK_STMP_THSD) << BIT_SHIFT_STMP_THSD)
#define BIT_GET_STMP_THSD(x) (((x) >> BIT_SHIFT_STMP_THSD) & BIT_MASK_STMP_THSD)

/* 2 REG_WMAC_TXTIMEOUT			(Offset 0x0635) */

#define BIT_SHIFT_WMAC_TXTIMEOUT 0
#define BIT_MASK_WMAC_TXTIMEOUT 0xff
#define BIT_WMAC_TXTIMEOUT(x)                                                  \
	(((x) & BIT_MASK_WMAC_TXTIMEOUT) << BIT_SHIFT_WMAC_TXTIMEOUT)
#define BIT_GET_WMAC_TXTIMEOUT(x)                                              \
	(((x) >> BIT_SHIFT_WMAC_TXTIMEOUT) & BIT_MASK_WMAC_TXTIMEOUT)

/* 2 REG_MCU_TEST_2_V1			(Offset 0x0636) */

#define BIT_SHIFT_MCU_RSVD_2_V1 0
#define BIT_MASK_MCU_RSVD_2_V1 0xffff
#define BIT_MCU_RSVD_2_V1(x)                                                   \
	(((x) & BIT_MASK_MCU_RSVD_2_V1) << BIT_SHIFT_MCU_RSVD_2_V1)
#define BIT_GET_MCU_RSVD_2_V1(x)                                               \
	(((x) >> BIT_SHIFT_MCU_RSVD_2_V1) & BIT_MASK_MCU_RSVD_2_V1)

/* 2 REG_USTIME_EDCA				(Offset 0x0638) */

#define BIT_SHIFT_USTIME_EDCA_V1 0
#define BIT_MASK_USTIME_EDCA_V1 0x1ff
#define BIT_USTIME_EDCA_V1(x)                                                  \
	(((x) & BIT_MASK_USTIME_EDCA_V1) << BIT_SHIFT_USTIME_EDCA_V1)
#define BIT_GET_USTIME_EDCA_V1(x)                                              \
	(((x) >> BIT_SHIFT_USTIME_EDCA_V1) & BIT_MASK_USTIME_EDCA_V1)

/* 2 REG_MAC_SPEC_SIFS			(Offset 0x063A) */

#define BIT_SHIFT_SPEC_SIFS_OFDM 8
#define BIT_MASK_SPEC_SIFS_OFDM 0xff
#define BIT_SPEC_SIFS_OFDM(x)                                                  \
	(((x) & BIT_MASK_SPEC_SIFS_OFDM) << BIT_SHIFT_SPEC_SIFS_OFDM)
#define BIT_GET_SPEC_SIFS_OFDM(x)                                              \
	(((x) >> BIT_SHIFT_SPEC_SIFS_OFDM) & BIT_MASK_SPEC_SIFS_OFDM)

#define BIT_SHIFT_SPEC_SIFS_CCK 0
#define BIT_MASK_SPEC_SIFS_CCK 0xff
#define BIT_SPEC_SIFS_CCK(x)                                                   \
	(((x) & BIT_MASK_SPEC_SIFS_CCK) << BIT_SHIFT_SPEC_SIFS_CCK)
#define BIT_GET_SPEC_SIFS_CCK(x)                                               \
	(((x) >> BIT_SHIFT_SPEC_SIFS_CCK) & BIT_MASK_SPEC_SIFS_CCK)

/* 2 REG_RESP_SIFS_CCK			(Offset 0x063C) */

#define BIT_SHIFT_SIFS_R2T_CCK 8
#define BIT_MASK_SIFS_R2T_CCK 0xff
#define BIT_SIFS_R2T_CCK(x)                                                    \
	(((x) & BIT_MASK_SIFS_R2T_CCK) << BIT_SHIFT_SIFS_R2T_CCK)
#define BIT_GET_SIFS_R2T_CCK(x)                                                \
	(((x) >> BIT_SHIFT_SIFS_R2T_CCK) & BIT_MASK_SIFS_R2T_CCK)

#define BIT_SHIFT_SIFS_T2T_CCK 0
#define BIT_MASK_SIFS_T2T_CCK 0xff
#define BIT_SIFS_T2T_CCK(x)                                                    \
	(((x) & BIT_MASK_SIFS_T2T_CCK) << BIT_SHIFT_SIFS_T2T_CCK)
#define BIT_GET_SIFS_T2T_CCK(x)                                                \
	(((x) >> BIT_SHIFT_SIFS_T2T_CCK) & BIT_MASK_SIFS_T2T_CCK)

/* 2 REG_RESP_SIFS_OFDM			(Offset 0x063E) */

#define BIT_SHIFT_SIFS_R2T_OFDM 8
#define BIT_MASK_SIFS_R2T_OFDM 0xff
#define BIT_SIFS_R2T_OFDM(x)                                                   \
	(((x) & BIT_MASK_SIFS_R2T_OFDM) << BIT_SHIFT_SIFS_R2T_OFDM)
#define BIT_GET_SIFS_R2T_OFDM(x)                                               \
	(((x) >> BIT_SHIFT_SIFS_R2T_OFDM) & BIT_MASK_SIFS_R2T_OFDM)

#define BIT_SHIFT_SIFS_T2T_OFDM 0
#define BIT_MASK_SIFS_T2T_OFDM 0xff
#define BIT_SIFS_T2T_OFDM(x)                                                   \
	(((x) & BIT_MASK_SIFS_T2T_OFDM) << BIT_SHIFT_SIFS_T2T_OFDM)
#define BIT_GET_SIFS_T2T_OFDM(x)                                               \
	(((x) >> BIT_SHIFT_SIFS_T2T_OFDM) & BIT_MASK_SIFS_T2T_OFDM)

/* 2 REG_ACKTO				(Offset 0x0640) */

#define BIT_SHIFT_ACKTO 0
#define BIT_MASK_ACKTO 0xff
#define BIT_ACKTO(x) (((x) & BIT_MASK_ACKTO) << BIT_SHIFT_ACKTO)
#define BIT_GET_ACKTO(x) (((x) >> BIT_SHIFT_ACKTO) & BIT_MASK_ACKTO)

/* 2 REG_CTS2TO				(Offset 0x0641) */

#define BIT_SHIFT_CTS2TO 0
#define BIT_MASK_CTS2TO 0xff
#define BIT_CTS2TO(x) (((x) & BIT_MASK_CTS2TO) << BIT_SHIFT_CTS2TO)
#define BIT_GET_CTS2TO(x) (((x) >> BIT_SHIFT_CTS2TO) & BIT_MASK_CTS2TO)

/* 2 REG_EIFS				(Offset 0x0642) */

#define BIT_SHIFT_EIFS 0
#define BIT_MASK_EIFS 0xffff
#define BIT_EIFS(x) (((x) & BIT_MASK_EIFS) << BIT_SHIFT_EIFS)
#define BIT_GET_EIFS(x) (((x) >> BIT_SHIFT_EIFS) & BIT_MASK_EIFS)

/* 2 REG_NAV_CTRL				(Offset 0x0650) */

#define BIT_SHIFT_NAV_UPPER 16
#define BIT_MASK_NAV_UPPER 0xff
#define BIT_NAV_UPPER(x) (((x) & BIT_MASK_NAV_UPPER) << BIT_SHIFT_NAV_UPPER)
#define BIT_GET_NAV_UPPER(x) (((x) >> BIT_SHIFT_NAV_UPPER) & BIT_MASK_NAV_UPPER)

#define BIT_SHIFT_RXMYRTS_NAV 8
#define BIT_MASK_RXMYRTS_NAV 0xf
#define BIT_RXMYRTS_NAV(x)                                                     \
	(((x) & BIT_MASK_RXMYRTS_NAV) << BIT_SHIFT_RXMYRTS_NAV)
#define BIT_GET_RXMYRTS_NAV(x)                                                 \
	(((x) >> BIT_SHIFT_RXMYRTS_NAV) & BIT_MASK_RXMYRTS_NAV)

#define BIT_SHIFT_RTSRST 0
#define BIT_MASK_RTSRST 0xff
#define BIT_RTSRST(x) (((x) & BIT_MASK_RTSRST) << BIT_SHIFT_RTSRST)
#define BIT_GET_RTSRST(x) (((x) >> BIT_SHIFT_RTSRST) & BIT_MASK_RTSRST)

/* 2 REG_BACAMCMD				(Offset 0x0654) */

#define BIT_BACAM_POLL BIT(31)
#define BIT_BACAM_RST BIT(17)
#define BIT_BACAM_RW BIT(16)

#define BIT_SHIFT_TXSBM 14
#define BIT_MASK_TXSBM 0x3
#define BIT_TXSBM(x) (((x) & BIT_MASK_TXSBM) << BIT_SHIFT_TXSBM)
#define BIT_GET_TXSBM(x) (((x) >> BIT_SHIFT_TXSBM) & BIT_MASK_TXSBM)

#define BIT_SHIFT_BACAM_ADDR 0
#define BIT_MASK_BACAM_ADDR 0x3f
#define BIT_BACAM_ADDR(x) (((x) & BIT_MASK_BACAM_ADDR) << BIT_SHIFT_BACAM_ADDR)
#define BIT_GET_BACAM_ADDR(x)                                                  \
	(((x) >> BIT_SHIFT_BACAM_ADDR) & BIT_MASK_BACAM_ADDR)

/* 2 REG_BACAMCONTENT			(Offset 0x0658) */

#define BIT_SHIFT_BA_CONTENT_H (32 & CPU_OPT_WIDTH)
#define BIT_MASK_BA_CONTENT_H 0xffffffffL
#define BIT_BA_CONTENT_H(x)                                                    \
	(((x) & BIT_MASK_BA_CONTENT_H) << BIT_SHIFT_BA_CONTENT_H)
#define BIT_GET_BA_CONTENT_H(x)                                                \
	(((x) >> BIT_SHIFT_BA_CONTENT_H) & BIT_MASK_BA_CONTENT_H)

#define BIT_SHIFT_BA_CONTENT_L 0
#define BIT_MASK_BA_CONTENT_L 0xffffffffL
#define BIT_BA_CONTENT_L(x)                                                    \
	(((x) & BIT_MASK_BA_CONTENT_L) << BIT_SHIFT_BA_CONTENT_L)
#define BIT_GET_BA_CONTENT_L(x)                                                \
	(((x) >> BIT_SHIFT_BA_CONTENT_L) & BIT_MASK_BA_CONTENT_L)

/* 2 REG_LBDLY				(Offset 0x0660) */

#define BIT_SHIFT_LBDLY 0
#define BIT_MASK_LBDLY 0x1f
#define BIT_LBDLY(x) (((x) & BIT_MASK_LBDLY) << BIT_SHIFT_LBDLY)
#define BIT_GET_LBDLY(x) (((x) >> BIT_SHIFT_LBDLY) & BIT_MASK_LBDLY)

/* 2 REG_WMAC_BACAM_RPMEN			(Offset 0x0661) */

#define BIT_SHIFT_BITMAP_SSNBK_COUNTER 2
#define BIT_MASK_BITMAP_SSNBK_COUNTER 0x3f
#define BIT_BITMAP_SSNBK_COUNTER(x)                                            \
	(((x) & BIT_MASK_BITMAP_SSNBK_COUNTER)                                 \
	 << BIT_SHIFT_BITMAP_SSNBK_COUNTER)
#define BIT_GET_BITMAP_SSNBK_COUNTER(x)                                        \
	(((x) >> BIT_SHIFT_BITMAP_SSNBK_COUNTER) &                             \
	 BIT_MASK_BITMAP_SSNBK_COUNTER)

#define BIT_BITMAP_EN BIT(1)

/* 2 REG_WMAC_BACAM_RPMEN			(Offset 0x0661) */

#define BIT_WMAC_BACAM_RPMEN BIT(0)

/* 2 REG_TX_RX				(Offset 0x0662) */

#define BIT_SHIFT_RXPKT_TYPE 2
#define BIT_MASK_RXPKT_TYPE 0x3f
#define BIT_RXPKT_TYPE(x) (((x) & BIT_MASK_RXPKT_TYPE) << BIT_SHIFT_RXPKT_TYPE)
#define BIT_GET_RXPKT_TYPE(x)                                                  \
	(((x) >> BIT_SHIFT_RXPKT_TYPE) & BIT_MASK_RXPKT_TYPE)

#define BIT_TXACT_IND BIT(1)
#define BIT_RXACT_IND BIT(0)

/* 2 REG_WMAC_BITMAP_CTL			(Offset 0x0663) */

#define BIT_BITMAP_VO BIT(7)
#define BIT_BITMAP_VI BIT(6)
#define BIT_BITMAP_BE BIT(5)
#define BIT_BITMAP_BK BIT(4)

#define BIT_SHIFT_BITMAP_CONDITION 2
#define BIT_MASK_BITMAP_CONDITION 0x3
#define BIT_BITMAP_CONDITION(x)                                                \
	(((x) & BIT_MASK_BITMAP_CONDITION) << BIT_SHIFT_BITMAP_CONDITION)
#define BIT_GET_BITMAP_CONDITION(x)                                            \
	(((x) >> BIT_SHIFT_BITMAP_CONDITION) & BIT_MASK_BITMAP_CONDITION)

#define BIT_BITMAP_SSNBK_COUNTER_CLR BIT(1)
#define BIT_BITMAP_FORCE BIT(0)

/* 2 REG_RXERR_RPT				(Offset 0x0664) */

#define BIT_SHIFT_RXERR_RPT_SEL_V1_3_0 28
#define BIT_MASK_RXERR_RPT_SEL_V1_3_0 0xf
#define BIT_RXERR_RPT_SEL_V1_3_0(x)                                            \
	(((x) & BIT_MASK_RXERR_RPT_SEL_V1_3_0)                                 \
	 << BIT_SHIFT_RXERR_RPT_SEL_V1_3_0)
#define BIT_GET_RXERR_RPT_SEL_V1_3_0(x)                                        \
	(((x) >> BIT_SHIFT_RXERR_RPT_SEL_V1_3_0) &                             \
	 BIT_MASK_RXERR_RPT_SEL_V1_3_0)

/* 2 REG_RXERR_RPT				(Offset 0x0664) */

#define BIT_RXERR_RPT_RST BIT(27)

/* 2 REG_RXERR_RPT				(Offset 0x0664) */

#define BIT_RXERR_RPT_SEL_V1_4 BIT(26)

/* 2 REG_RXERR_RPT				(Offset 0x0664) */

#define BIT_W1S BIT(23)

/* 2 REG_RXERR_RPT				(Offset 0x0664) */

#define BIT_UD_SELECT_BSSID BIT(22)

/* 2 REG_RXERR_RPT				(Offset 0x0664) */

#define BIT_SHIFT_UD_SUB_TYPE 18
#define BIT_MASK_UD_SUB_TYPE 0xf
#define BIT_UD_SUB_TYPE(x)                                                     \
	(((x) & BIT_MASK_UD_SUB_TYPE) << BIT_SHIFT_UD_SUB_TYPE)
#define BIT_GET_UD_SUB_TYPE(x)                                                 \
	(((x) >> BIT_SHIFT_UD_SUB_TYPE) & BIT_MASK_UD_SUB_TYPE)

#define BIT_SHIFT_UD_TYPE 16
#define BIT_MASK_UD_TYPE 0x3
#define BIT_UD_TYPE(x) (((x) & BIT_MASK_UD_TYPE) << BIT_SHIFT_UD_TYPE)
#define BIT_GET_UD_TYPE(x) (((x) >> BIT_SHIFT_UD_TYPE) & BIT_MASK_UD_TYPE)

#define BIT_SHIFT_RPT_COUNTER 0
#define BIT_MASK_RPT_COUNTER 0xffff
#define BIT_RPT_COUNTER(x)                                                     \
	(((x) & BIT_MASK_RPT_COUNTER) << BIT_SHIFT_RPT_COUNTER)
#define BIT_GET_RPT_COUNTER(x)                                                 \
	(((x) >> BIT_SHIFT_RPT_COUNTER) & BIT_MASK_RPT_COUNTER)

/* 2 REG_WMAC_TRXPTCL_CTL			(Offset 0x0668) */

#define BIT_SHIFT_ACKBA_TYPSEL (60 & CPU_OPT_WIDTH)
#define BIT_MASK_ACKBA_TYPSEL 0xf
#define BIT_ACKBA_TYPSEL(x)                                                    \
	(((x) & BIT_MASK_ACKBA_TYPSEL) << BIT_SHIFT_ACKBA_TYPSEL)
#define BIT_GET_ACKBA_TYPSEL(x)                                                \
	(((x) >> BIT_SHIFT_ACKBA_TYPSEL) & BIT_MASK_ACKBA_TYPSEL)

#define BIT_SHIFT_ACKBA_ACKPCHK (56 & CPU_OPT_WIDTH)
#define BIT_MASK_ACKBA_ACKPCHK 0xf
#define BIT_ACKBA_ACKPCHK(x)                                                   \
	(((x) & BIT_MASK_ACKBA_ACKPCHK) << BIT_SHIFT_ACKBA_ACKPCHK)
#define BIT_GET_ACKBA_ACKPCHK(x)                                               \
	(((x) >> BIT_SHIFT_ACKBA_ACKPCHK) & BIT_MASK_ACKBA_ACKPCHK)

#define BIT_SHIFT_ACKBAR_TYPESEL (48 & CPU_OPT_WIDTH)
#define BIT_MASK_ACKBAR_TYPESEL 0xff
#define BIT_ACKBAR_TYPESEL(x)                                                  \
	(((x) & BIT_MASK_ACKBAR_TYPESEL) << BIT_SHIFT_ACKBAR_TYPESEL)
#define BIT_GET_ACKBAR_TYPESEL(x)                                              \
	(((x) >> BIT_SHIFT_ACKBAR_TYPESEL) & BIT_MASK_ACKBAR_TYPESEL)

#define BIT_SHIFT_ACKBAR_ACKPCHK (44 & CPU_OPT_WIDTH)
#define BIT_MASK_ACKBAR_ACKPCHK 0xf
#define BIT_ACKBAR_ACKPCHK(x)                                                  \
	(((x) & BIT_MASK_ACKBAR_ACKPCHK) << BIT_SHIFT_ACKBAR_ACKPCHK)
#define BIT_GET_ACKBAR_ACKPCHK(x)                                              \
	(((x) >> BIT_SHIFT_ACKBAR_ACKPCHK) & BIT_MASK_ACKBAR_ACKPCHK)

/* 2 REG_WMAC_TRXPTCL_CTL			(Offset 0x0668) */

#define BIT_RXBA_IGNOREA2 BIT(42)
#define BIT_EN_SAVE_ALL_TXOPADDR BIT(41)
#define BIT_EN_TXCTS_TO_TXOPOWNER_INRXNAV BIT(40)

/* 2 REG_WMAC_TRXPTCL_CTL			(Offset 0x0668) */

#define BIT_DIS_TXBA_AMPDUFCSERR BIT(39)
#define BIT_DIS_TXBA_RXBARINFULL BIT(38)
#define BIT_DIS_TXCFE_INFULL BIT(37)
#define BIT_DIS_TXCTS_INFULL BIT(36)
#define BIT_EN_TXACKBA_IN_TX_RDG BIT(35)
#define BIT_EN_TXACKBA_IN_TXOP BIT(34)
#define BIT_EN_TXCTS_IN_RXNAV BIT(33)
#define BIT_EN_TXCTS_INTXOP BIT(32)
#define BIT_BLK_EDCA_BBSLP BIT(31)
#define BIT_BLK_EDCA_BBSBY BIT(30)
#define BIT_ACKTO_BLOCK_SCH_EN BIT(27)
#define BIT_EIFS_BLOCK_SCH_EN BIT(26)
#define BIT_PLCPCHK_RST_EIFS BIT(25)
#define BIT_CCA_RST_EIFS BIT(24)
#define BIT_DIS_UPD_MYRXPKTNAV BIT(23)
#define BIT_EARLY_TXBA BIT(22)

#define BIT_SHIFT_RESP_CHNBUSY 20
#define BIT_MASK_RESP_CHNBUSY 0x3
#define BIT_RESP_CHNBUSY(x)                                                    \
	(((x) & BIT_MASK_RESP_CHNBUSY) << BIT_SHIFT_RESP_CHNBUSY)
#define BIT_GET_RESP_CHNBUSY(x)                                                \
	(((x) >> BIT_SHIFT_RESP_CHNBUSY) & BIT_MASK_RESP_CHNBUSY)

#define BIT_RESP_DCTS_EN BIT(19)
#define BIT_RESP_DCFE_EN BIT(18)
#define BIT_RESP_SPLCPEN BIT(17)
#define BIT_RESP_SGIEN BIT(16)

/* 2 REG_WMAC_TRXPTCL_CTL			(Offset 0x0668) */

#define BIT_RESP_LDPC_EN BIT(15)
#define BIT_DIS_RESP_ACKINCCA BIT(14)
#define BIT_DIS_RESP_CTSINCCA BIT(13)

/* 2 REG_WMAC_TRXPTCL_CTL			(Offset 0x0668) */

#define BIT_SHIFT_R_WMAC_SECOND_CCA_TIMER 10
#define BIT_MASK_R_WMAC_SECOND_CCA_TIMER 0x7
#define BIT_R_WMAC_SECOND_CCA_TIMER(x)                                         \
	(((x) & BIT_MASK_R_WMAC_SECOND_CCA_TIMER)                              \
	 << BIT_SHIFT_R_WMAC_SECOND_CCA_TIMER)
#define BIT_GET_R_WMAC_SECOND_CCA_TIMER(x)                                     \
	(((x) >> BIT_SHIFT_R_WMAC_SECOND_CCA_TIMER) &                          \
	 BIT_MASK_R_WMAC_SECOND_CCA_TIMER)

/* 2 REG_WMAC_TRXPTCL_CTL			(Offset 0x0668) */

#define BIT_SHIFT_RFMOD 7
#define BIT_MASK_RFMOD 0x3
#define BIT_RFMOD(x) (((x) & BIT_MASK_RFMOD) << BIT_SHIFT_RFMOD)
#define BIT_GET_RFMOD(x) (((x) >> BIT_SHIFT_RFMOD) & BIT_MASK_RFMOD)

/* 2 REG_WMAC_TRXPTCL_CTL			(Offset 0x0668) */

#define BIT_SHIFT_RESP_CTS_DYNBW_SEL 5
#define BIT_MASK_RESP_CTS_DYNBW_SEL 0x3
#define BIT_RESP_CTS_DYNBW_SEL(x)                                              \
	(((x) & BIT_MASK_RESP_CTS_DYNBW_SEL) << BIT_SHIFT_RESP_CTS_DYNBW_SEL)
#define BIT_GET_RESP_CTS_DYNBW_SEL(x)                                          \
	(((x) >> BIT_SHIFT_RESP_CTS_DYNBW_SEL) & BIT_MASK_RESP_CTS_DYNBW_SEL)

/* 2 REG_WMAC_TRXPTCL_CTL			(Offset 0x0668) */

#define BIT_DLY_TX_WAIT_RXANTSEL BIT(4)

/* 2 REG_WMAC_TRXPTCL_CTL			(Offset 0x0668) */

#define BIT_TXRESP_BY_RXANTSEL BIT(3)

/* 2 REG_WMAC_TRXPTCL_CTL			(Offset 0x0668) */

#define BIT_SHIFT_ORIG_DCTS_CHK 0
#define BIT_MASK_ORIG_DCTS_CHK 0x3
#define BIT_ORIG_DCTS_CHK(x)                                                   \
	(((x) & BIT_MASK_ORIG_DCTS_CHK) << BIT_SHIFT_ORIG_DCTS_CHK)
#define BIT_GET_ORIG_DCTS_CHK(x)                                               \
	(((x) >> BIT_SHIFT_ORIG_DCTS_CHK) & BIT_MASK_ORIG_DCTS_CHK)

/* 2 REG_CAMCMD				(Offset 0x0670) */

#define BIT_SECCAM_POLLING BIT(31)
#define BIT_SECCAM_CLR BIT(30)
#define BIT_MFBCAM_CLR BIT(29)

/* 2 REG_CAMCMD				(Offset 0x0670) */

#define BIT_SECCAM_WE BIT(16)

/* 2 REG_CAMCMD				(Offset 0x0670) */

#define BIT_SHIFT_SECCAM_ADDR_V2 0
#define BIT_MASK_SECCAM_ADDR_V2 0x3ff
#define BIT_SECCAM_ADDR_V2(x)                                                  \
	(((x) & BIT_MASK_SECCAM_ADDR_V2) << BIT_SHIFT_SECCAM_ADDR_V2)
#define BIT_GET_SECCAM_ADDR_V2(x)                                              \
	(((x) >> BIT_SHIFT_SECCAM_ADDR_V2) & BIT_MASK_SECCAM_ADDR_V2)

/* 2 REG_CAMWRITE				(Offset 0x0674) */

#define BIT_SHIFT_CAMW_DATA 0
#define BIT_MASK_CAMW_DATA 0xffffffffL
#define BIT_CAMW_DATA(x) (((x) & BIT_MASK_CAMW_DATA) << BIT_SHIFT_CAMW_DATA)
#define BIT_GET_CAMW_DATA(x) (((x) >> BIT_SHIFT_CAMW_DATA) & BIT_MASK_CAMW_DATA)

/* 2 REG_CAMREAD				(Offset 0x0678) */

#define BIT_SHIFT_CAMR_DATA 0
#define BIT_MASK_CAMR_DATA 0xffffffffL
#define BIT_CAMR_DATA(x) (((x) & BIT_MASK_CAMR_DATA) << BIT_SHIFT_CAMR_DATA)
#define BIT_GET_CAMR_DATA(x) (((x) >> BIT_SHIFT_CAMR_DATA) & BIT_MASK_CAMR_DATA)

/* 2 REG_CAMDBG				(Offset 0x067C) */

#define BIT_SECCAM_INFO BIT(31)
#define BIT_SEC_KEYFOUND BIT(15)

#define BIT_SHIFT_CAMDBG_SEC_TYPE 12
#define BIT_MASK_CAMDBG_SEC_TYPE 0x7
#define BIT_CAMDBG_SEC_TYPE(x)                                                 \
	(((x) & BIT_MASK_CAMDBG_SEC_TYPE) << BIT_SHIFT_CAMDBG_SEC_TYPE)
#define BIT_GET_CAMDBG_SEC_TYPE(x)                                             \
	(((x) >> BIT_SHIFT_CAMDBG_SEC_TYPE) & BIT_MASK_CAMDBG_SEC_TYPE)

/* 2 REG_CAMDBG				(Offset 0x067C) */

#define BIT_CAMDBG_EXT_SECTYPE BIT(11)

/* 2 REG_CAMDBG				(Offset 0x067C) */

#define BIT_SHIFT_CAMDBG_MIC_KEY_IDX 5
#define BIT_MASK_CAMDBG_MIC_KEY_IDX 0x1f
#define BIT_CAMDBG_MIC_KEY_IDX(x)                                              \
	(((x) & BIT_MASK_CAMDBG_MIC_KEY_IDX) << BIT_SHIFT_CAMDBG_MIC_KEY_IDX)
#define BIT_GET_CAMDBG_MIC_KEY_IDX(x)                                          \
	(((x) >> BIT_SHIFT_CAMDBG_MIC_KEY_IDX) & BIT_MASK_CAMDBG_MIC_KEY_IDX)

#define BIT_SHIFT_CAMDBG_SEC_KEY_IDX 0
#define BIT_MASK_CAMDBG_SEC_KEY_IDX 0x1f
#define BIT_CAMDBG_SEC_KEY_IDX(x)                                              \
	(((x) & BIT_MASK_CAMDBG_SEC_KEY_IDX) << BIT_SHIFT_CAMDBG_SEC_KEY_IDX)
#define BIT_GET_CAMDBG_SEC_KEY_IDX(x)                                          \
	(((x) >> BIT_SHIFT_CAMDBG_SEC_KEY_IDX) & BIT_MASK_CAMDBG_SEC_KEY_IDX)

/* 2 REG_SECCFG				(Offset 0x0680) */

#define BIT_DIS_GCLK_WAPI BIT(15)
#define BIT_DIS_GCLK_AES BIT(14)
#define BIT_DIS_GCLK_TKIP BIT(13)

/* 2 REG_SECCFG				(Offset 0x0680) */

#define BIT_AES_SEL_QC_1 BIT(12)
#define BIT_AES_SEL_QC_0 BIT(11)

/* 2 REG_SECCFG				(Offset 0x0680) */

#define BIT_CHK_BMC BIT(9)

/* 2 REG_SECCFG				(Offset 0x0680) */

#define BIT_CHK_KEYID BIT(8)
#define BIT_RXBCUSEDK BIT(7)
#define BIT_TXBCUSEDK BIT(6)
#define BIT_NOSKMC BIT(5)
#define BIT_SKBYA2 BIT(4)
#define BIT_RXDEC BIT(3)
#define BIT_TXENC BIT(2)
#define BIT_RXUHUSEDK BIT(1)
#define BIT_TXUHUSEDK BIT(0)

/* 2 REG_RXFILTER_CATEGORY_1			(Offset 0x0682) */

#define BIT_SHIFT_RXFILTER_CATEGORY_1 0
#define BIT_MASK_RXFILTER_CATEGORY_1 0xff
#define BIT_RXFILTER_CATEGORY_1(x)                                             \
	(((x) & BIT_MASK_RXFILTER_CATEGORY_1) << BIT_SHIFT_RXFILTER_CATEGORY_1)
#define BIT_GET_RXFILTER_CATEGORY_1(x)                                         \
	(((x) >> BIT_SHIFT_RXFILTER_CATEGORY_1) & BIT_MASK_RXFILTER_CATEGORY_1)

/* 2 REG_RXFILTER_ACTION_1			(Offset 0x0683) */

#define BIT_SHIFT_RXFILTER_ACTION_1 0
#define BIT_MASK_RXFILTER_ACTION_1 0xff
#define BIT_RXFILTER_ACTION_1(x)                                               \
	(((x) & BIT_MASK_RXFILTER_ACTION_1) << BIT_SHIFT_RXFILTER_ACTION_1)
#define BIT_GET_RXFILTER_ACTION_1(x)                                           \
	(((x) >> BIT_SHIFT_RXFILTER_ACTION_1) & BIT_MASK_RXFILTER_ACTION_1)

/* 2 REG_RXFILTER_CATEGORY_2			(Offset 0x0684) */

#define BIT_SHIFT_RXFILTER_CATEGORY_2 0
#define BIT_MASK_RXFILTER_CATEGORY_2 0xff
#define BIT_RXFILTER_CATEGORY_2(x)                                             \
	(((x) & BIT_MASK_RXFILTER_CATEGORY_2) << BIT_SHIFT_RXFILTER_CATEGORY_2)
#define BIT_GET_RXFILTER_CATEGORY_2(x)                                         \
	(((x) >> BIT_SHIFT_RXFILTER_CATEGORY_2) & BIT_MASK_RXFILTER_CATEGORY_2)

/* 2 REG_RXFILTER_ACTION_2			(Offset 0x0685) */

#define BIT_SHIFT_RXFILTER_ACTION_2 0
#define BIT_MASK_RXFILTER_ACTION_2 0xff
#define BIT_RXFILTER_ACTION_2(x)                                               \
	(((x) & BIT_MASK_RXFILTER_ACTION_2) << BIT_SHIFT_RXFILTER_ACTION_2)
#define BIT_GET_RXFILTER_ACTION_2(x)                                           \
	(((x) >> BIT_SHIFT_RXFILTER_ACTION_2) & BIT_MASK_RXFILTER_ACTION_2)

/* 2 REG_RXFILTER_CATEGORY_3			(Offset 0x0686) */

#define BIT_SHIFT_RXFILTER_CATEGORY_3 0
#define BIT_MASK_RXFILTER_CATEGORY_3 0xff
#define BIT_RXFILTER_CATEGORY_3(x)                                             \
	(((x) & BIT_MASK_RXFILTER_CATEGORY_3) << BIT_SHIFT_RXFILTER_CATEGORY_3)
#define BIT_GET_RXFILTER_CATEGORY_3(x)                                         \
	(((x) >> BIT_SHIFT_RXFILTER_CATEGORY_3) & BIT_MASK_RXFILTER_CATEGORY_3)

/* 2 REG_RXFILTER_ACTION_3			(Offset 0x0687) */

#define BIT_SHIFT_RXFILTER_ACTION_3 0
#define BIT_MASK_RXFILTER_ACTION_3 0xff
#define BIT_RXFILTER_ACTION_3(x)                                               \
	(((x) & BIT_MASK_RXFILTER_ACTION_3) << BIT_SHIFT_RXFILTER_ACTION_3)
#define BIT_GET_RXFILTER_ACTION_3(x)                                           \
	(((x) >> BIT_SHIFT_RXFILTER_ACTION_3) & BIT_MASK_RXFILTER_ACTION_3)

/* 2 REG_RXFLTMAP3				(Offset 0x0688) */

#define BIT_MGTFLT15EN_FW BIT(15)
#define BIT_MGTFLT14EN_FW BIT(14)
#define BIT_MGTFLT13EN_FW BIT(13)
#define BIT_MGTFLT12EN_FW BIT(12)
#define BIT_MGTFLT11EN_FW BIT(11)
#define BIT_MGTFLT10EN_FW BIT(10)
#define BIT_MGTFLT9EN_FW BIT(9)
#define BIT_MGTFLT8EN_FW BIT(8)
#define BIT_MGTFLT7EN_FW BIT(7)
#define BIT_MGTFLT6EN_FW BIT(6)
#define BIT_MGTFLT5EN_FW BIT(5)
#define BIT_MGTFLT4EN_FW BIT(4)
#define BIT_MGTFLT3EN_FW BIT(3)
#define BIT_MGTFLT2EN_FW BIT(2)
#define BIT_MGTFLT1EN_FW BIT(1)
#define BIT_MGTFLT0EN_FW BIT(0)

/* 2 REG_RXFLTMAP4				(Offset 0x068A) */

#define BIT_CTRLFLT15EN_FW BIT(15)
#define BIT_CTRLFLT14EN_FW BIT(14)
#define BIT_CTRLFLT13EN_FW BIT(13)
#define BIT_CTRLFLT12EN_FW BIT(12)
#define BIT_CTRLFLT11EN_FW BIT(11)
#define BIT_CTRLFLT10EN_FW BIT(10)
#define BIT_CTRLFLT9EN_FW BIT(9)
#define BIT_CTRLFLT8EN_FW BIT(8)
#define BIT_CTRLFLT7EN_FW BIT(7)
#define BIT_CTRLFLT6EN_FW BIT(6)
#define BIT_CTRLFLT5EN_FW BIT(5)
#define BIT_CTRLFLT4EN_FW BIT(4)
#define BIT_CTRLFLT3EN_FW BIT(3)
#define BIT_CTRLFLT2EN_FW BIT(2)
#define BIT_CTRLFLT1EN_FW BIT(1)
#define BIT_CTRLFLT0EN_FW BIT(0)

/* 2 REG_RXFLTMAP5				(Offset 0x068C) */

#define BIT_DATAFLT15EN_FW BIT(15)
#define BIT_DATAFLT14EN_FW BIT(14)
#define BIT_DATAFLT13EN_FW BIT(13)
#define BIT_DATAFLT12EN_FW BIT(12)
#define BIT_DATAFLT11EN_FW BIT(11)
#define BIT_DATAFLT10EN_FW BIT(10)
#define BIT_DATAFLT9EN_FW BIT(9)
#define BIT_DATAFLT8EN_FW BIT(8)
#define BIT_DATAFLT7EN_FW BIT(7)
#define BIT_DATAFLT6EN_FW BIT(6)
#define BIT_DATAFLT5EN_FW BIT(5)
#define BIT_DATAFLT4EN_FW BIT(4)
#define BIT_DATAFLT3EN_FW BIT(3)
#define BIT_DATAFLT2EN_FW BIT(2)
#define BIT_DATAFLT1EN_FW BIT(1)
#define BIT_DATAFLT0EN_FW BIT(0)

/* 2 REG_RXFLTMAP6				(Offset 0x068E) */

#define BIT_ACTIONFLT15EN_FW BIT(15)
#define BIT_ACTIONFLT14EN_FW BIT(14)
#define BIT_ACTIONFLT13EN_FW BIT(13)
#define BIT_ACTIONFLT12EN_FW BIT(12)
#define BIT_ACTIONFLT11EN_FW BIT(11)
#define BIT_ACTIONFLT10EN_FW BIT(10)
#define BIT_ACTIONFLT9EN_FW BIT(9)
#define BIT_ACTIONFLT8EN_FW BIT(8)
#define BIT_ACTIONFLT7EN_FW BIT(7)
#define BIT_ACTIONFLT6EN_FW BIT(6)
#define BIT_ACTIONFLT5EN_FW BIT(5)
#define BIT_ACTIONFLT4EN_FW BIT(4)
#define BIT_ACTIONFLT3EN_FW BIT(3)
#define BIT_ACTIONFLT2EN_FW BIT(2)
#define BIT_ACTIONFLT1EN_FW BIT(1)
#define BIT_ACTIONFLT0EN_FW BIT(0)

/* 2 REG_WOW_CTRL				(Offset 0x0690) */

#define BIT_SHIFT_PSF_BSSIDSEL_B2B1 6
#define BIT_MASK_PSF_BSSIDSEL_B2B1 0x3
#define BIT_PSF_BSSIDSEL_B2B1(x)                                               \
	(((x) & BIT_MASK_PSF_BSSIDSEL_B2B1) << BIT_SHIFT_PSF_BSSIDSEL_B2B1)
#define BIT_GET_PSF_BSSIDSEL_B2B1(x)                                           \
	(((x) >> BIT_SHIFT_PSF_BSSIDSEL_B2B1) & BIT_MASK_PSF_BSSIDSEL_B2B1)

/* 2 REG_WOW_CTRL				(Offset 0x0690) */

#define BIT_WOWHCI BIT(5)

/* 2 REG_WOW_CTRL				(Offset 0x0690) */

#define BIT_PSF_BSSIDSEL_B0 BIT(4)

/* 2 REG_WOW_CTRL				(Offset 0x0690) */

#define BIT_UWF BIT(3)
#define BIT_MAGIC BIT(2)
#define BIT_WOWEN BIT(1)
#define BIT_FORCE_WAKEUP BIT(0)

/* 2 REG_NAN_RX_TSF_FILTER			(Offset 0x0691) */

#define BIT_CHK_TSF_TA BIT(2)
#define BIT_CHK_TSF_CBSSID BIT(1)
#define BIT_CHK_TSF_EN BIT(0)

/* 2 REG_PS_RX_INFO				(Offset 0x0692) */

#define BIT_SHIFT_PORTSEL__PS_RX_INFO 5
#define BIT_MASK_PORTSEL__PS_RX_INFO 0x7
#define BIT_PORTSEL__PS_RX_INFO(x)                                             \
	(((x) & BIT_MASK_PORTSEL__PS_RX_INFO) << BIT_SHIFT_PORTSEL__PS_RX_INFO)
#define BIT_GET_PORTSEL__PS_RX_INFO(x)                                         \
	(((x) >> BIT_SHIFT_PORTSEL__PS_RX_INFO) & BIT_MASK_PORTSEL__PS_RX_INFO)

/* 2 REG_PS_RX_INFO				(Offset 0x0692) */

#define BIT_RXCTRLIN0 BIT(4)
#define BIT_RXMGTIN0 BIT(3)
#define BIT_RXDATAIN2 BIT(2)
#define BIT_RXDATAIN1 BIT(1)
#define BIT_RXDATAIN0 BIT(0)

/* 2 REG_WMMPS_UAPSD_TID			(Offset 0x0693) */

#define BIT_WMMPS_UAPSD_TID7 BIT(7)
#define BIT_WMMPS_UAPSD_TID6 BIT(6)
#define BIT_WMMPS_UAPSD_TID5 BIT(5)
#define BIT_WMMPS_UAPSD_TID4 BIT(4)
#define BIT_WMMPS_UAPSD_TID3 BIT(3)
#define BIT_WMMPS_UAPSD_TID2 BIT(2)
#define BIT_WMMPS_UAPSD_TID1 BIT(1)
#define BIT_WMMPS_UAPSD_TID0 BIT(0)

/* 2 REG_LPNAV_CTRL				(Offset 0x0694) */

#define BIT_LPNAV_EN BIT(31)

#define BIT_SHIFT_LPNAV_EARLY 16
#define BIT_MASK_LPNAV_EARLY 0x7fff
#define BIT_LPNAV_EARLY(x)                                                     \
	(((x) & BIT_MASK_LPNAV_EARLY) << BIT_SHIFT_LPNAV_EARLY)
#define BIT_GET_LPNAV_EARLY(x)                                                 \
	(((x) >> BIT_SHIFT_LPNAV_EARLY) & BIT_MASK_LPNAV_EARLY)

#define BIT_SHIFT_LPNAV_TH 0
#define BIT_MASK_LPNAV_TH 0xffff
#define BIT_LPNAV_TH(x) (((x) & BIT_MASK_LPNAV_TH) << BIT_SHIFT_LPNAV_TH)
#define BIT_GET_LPNAV_TH(x) (((x) >> BIT_SHIFT_LPNAV_TH) & BIT_MASK_LPNAV_TH)

/* 2 REG_WKFMCAM_CMD				(Offset 0x0698) */

#define BIT_WKFCAM_POLLING_V1 BIT(31)
#define BIT_WKFCAM_CLR_V1 BIT(30)

/* 2 REG_WKFMCAM_CMD				(Offset 0x0698) */

#define BIT_WKFCAM_WE BIT(16)

/* 2 REG_WKFMCAM_CMD				(Offset 0x0698) */

#define BIT_SHIFT_WKFCAM_ADDR_V2 8
#define BIT_MASK_WKFCAM_ADDR_V2 0xff
#define BIT_WKFCAM_ADDR_V2(x)                                                  \
	(((x) & BIT_MASK_WKFCAM_ADDR_V2) << BIT_SHIFT_WKFCAM_ADDR_V2)
#define BIT_GET_WKFCAM_ADDR_V2(x)                                              \
	(((x) >> BIT_SHIFT_WKFCAM_ADDR_V2) & BIT_MASK_WKFCAM_ADDR_V2)

#define BIT_SHIFT_WKFCAM_CAM_NUM_V1 0
#define BIT_MASK_WKFCAM_CAM_NUM_V1 0xff
#define BIT_WKFCAM_CAM_NUM_V1(x)                                               \
	(((x) & BIT_MASK_WKFCAM_CAM_NUM_V1) << BIT_SHIFT_WKFCAM_CAM_NUM_V1)
#define BIT_GET_WKFCAM_CAM_NUM_V1(x)                                           \
	(((x) >> BIT_SHIFT_WKFCAM_CAM_NUM_V1) & BIT_MASK_WKFCAM_CAM_NUM_V1)

/* 2 REG_WKFMCAM_RWD				(Offset 0x069C) */

#define BIT_SHIFT_WKFMCAM_RWD 0
#define BIT_MASK_WKFMCAM_RWD 0xffffffffL
#define BIT_WKFMCAM_RWD(x)                                                     \
	(((x) & BIT_MASK_WKFMCAM_RWD) << BIT_SHIFT_WKFMCAM_RWD)
#define BIT_GET_WKFMCAM_RWD(x)                                                 \
	(((x) >> BIT_SHIFT_WKFMCAM_RWD) & BIT_MASK_WKFMCAM_RWD)

/* 2 REG_RXFLTMAP0				(Offset 0x06A0) */

#define BIT_MGTFLT15EN BIT(15)
#define BIT_MGTFLT14EN BIT(14)

/* 2 REG_RXFLTMAP0				(Offset 0x06A0) */

#define BIT_MGTFLT13EN BIT(13)
#define BIT_MGTFLT12EN BIT(12)
#define BIT_MGTFLT11EN BIT(11)
#define BIT_MGTFLT10EN BIT(10)
#define BIT_MGTFLT9EN BIT(9)
#define BIT_MGTFLT8EN BIT(8)

/* 2 REG_RXFLTMAP0				(Offset 0x06A0) */

#define BIT_MGTFLT7EN BIT(7)
#define BIT_MGTFLT6EN BIT(6)

/* 2 REG_RXFLTMAP0				(Offset 0x06A0) */

#define BIT_MGTFLT5EN BIT(5)
#define BIT_MGTFLT4EN BIT(4)
#define BIT_MGTFLT3EN BIT(3)
#define BIT_MGTFLT2EN BIT(2)
#define BIT_MGTFLT1EN BIT(1)
#define BIT_MGTFLT0EN BIT(0)

/* 2 REG_RXFLTMAP1				(Offset 0x06A2) */

#define BIT_CTRLFLT15EN BIT(15)
#define BIT_CTRLFLT14EN BIT(14)
#define BIT_CTRLFLT13EN BIT(13)
#define BIT_CTRLFLT12EN BIT(12)
#define BIT_CTRLFLT11EN BIT(11)
#define BIT_CTRLFLT10EN BIT(10)
#define BIT_CTRLFLT9EN BIT(9)
#define BIT_CTRLFLT8EN BIT(8)
#define BIT_CTRLFLT7EN BIT(7)
#define BIT_CTRLFLT6EN BIT(6)

/* 2 REG_RXFLTMAP1				(Offset 0x06A2) */

#define BIT_CTRLFLT5EN BIT(5)
#define BIT_CTRLFLT4EN BIT(4)
#define BIT_CTRLFLT3EN BIT(3)
#define BIT_CTRLFLT2EN BIT(2)
#define BIT_CTRLFLT1EN BIT(1)
#define BIT_CTRLFLT0EN BIT(0)

/* 2 REG_RXFLTMAP				(Offset 0x06A4) */

#define BIT_DATAFLT15EN BIT(15)
#define BIT_DATAFLT14EN BIT(14)
#define BIT_DATAFLT13EN BIT(13)
#define BIT_DATAFLT12EN BIT(12)
#define BIT_DATAFLT11EN BIT(11)
#define BIT_DATAFLT10EN BIT(10)
#define BIT_DATAFLT9EN BIT(9)
#define BIT_DATAFLT8EN BIT(8)
#define BIT_DATAFLT7EN BIT(7)
#define BIT_DATAFLT6EN BIT(6)
#define BIT_DATAFLT5EN BIT(5)
#define BIT_DATAFLT4EN BIT(4)
#define BIT_DATAFLT3EN BIT(3)
#define BIT_DATAFLT2EN BIT(2)
#define BIT_DATAFLT1EN BIT(1)
#define BIT_DATAFLT0EN BIT(0)

/* 2 REG_BCN_PSR_RPT				(Offset 0x06A8) */

#define BIT_SHIFT_DTIM_CNT 24
#define BIT_MASK_DTIM_CNT 0xff
#define BIT_DTIM_CNT(x) (((x) & BIT_MASK_DTIM_CNT) << BIT_SHIFT_DTIM_CNT)
#define BIT_GET_DTIM_CNT(x) (((x) >> BIT_SHIFT_DTIM_CNT) & BIT_MASK_DTIM_CNT)

#define BIT_SHIFT_DTIM_PERIOD 16
#define BIT_MASK_DTIM_PERIOD 0xff
#define BIT_DTIM_PERIOD(x)                                                     \
	(((x) & BIT_MASK_DTIM_PERIOD) << BIT_SHIFT_DTIM_PERIOD)
#define BIT_GET_DTIM_PERIOD(x)                                                 \
	(((x) >> BIT_SHIFT_DTIM_PERIOD) & BIT_MASK_DTIM_PERIOD)

#define BIT_DTIM BIT(15)
#define BIT_TIM BIT(14)

#define BIT_SHIFT_PS_AID_0 0
#define BIT_MASK_PS_AID_0 0x7ff
#define BIT_PS_AID_0(x) (((x) & BIT_MASK_PS_AID_0) << BIT_SHIFT_PS_AID_0)
#define BIT_GET_PS_AID_0(x) (((x) >> BIT_SHIFT_PS_AID_0) & BIT_MASK_PS_AID_0)

/* 2 REG_FLC_RPC				(Offset 0x06AC) */

#define BIT_SHIFT_FLC_RPC 0
#define BIT_MASK_FLC_RPC 0xff
#define BIT_FLC_RPC(x) (((x) & BIT_MASK_FLC_RPC) << BIT_SHIFT_FLC_RPC)
#define BIT_GET_FLC_RPC(x) (((x) >> BIT_SHIFT_FLC_RPC) & BIT_MASK_FLC_RPC)

/* 2 REG_FLC_RPCT				(Offset 0x06AD) */

#define BIT_SHIFT_FLC_RPCT 0
#define BIT_MASK_FLC_RPCT 0xff
#define BIT_FLC_RPCT(x) (((x) & BIT_MASK_FLC_RPCT) << BIT_SHIFT_FLC_RPCT)
#define BIT_GET_FLC_RPCT(x) (((x) >> BIT_SHIFT_FLC_RPCT) & BIT_MASK_FLC_RPCT)

/* 2 REG_FLC_PTS				(Offset 0x06AE) */

#define BIT_CMF BIT(2)
#define BIT_CCF BIT(1)
#define BIT_CDF BIT(0)

/* 2 REG_FLC_TRPC				(Offset 0x06AF) */

#define BIT_FLC_RPCT_V1 BIT(7)
#define BIT_MODE BIT(6)

#define BIT_SHIFT_TRPCD 0
#define BIT_MASK_TRPCD 0x3f
#define BIT_TRPCD(x) (((x) & BIT_MASK_TRPCD) << BIT_SHIFT_TRPCD)
#define BIT_GET_TRPCD(x) (((x) >> BIT_SHIFT_TRPCD) & BIT_MASK_TRPCD)

/* 2 REG_RXPKTMON_CTRL			(Offset 0x06B0) */

#define BIT_SHIFT_RXBKQPKT_SEQ 20
#define BIT_MASK_RXBKQPKT_SEQ 0xf
#define BIT_RXBKQPKT_SEQ(x)                                                    \
	(((x) & BIT_MASK_RXBKQPKT_SEQ) << BIT_SHIFT_RXBKQPKT_SEQ)
#define BIT_GET_RXBKQPKT_SEQ(x)                                                \
	(((x) >> BIT_SHIFT_RXBKQPKT_SEQ) & BIT_MASK_RXBKQPKT_SEQ)

#define BIT_SHIFT_RXBEQPKT_SEQ 16
#define BIT_MASK_RXBEQPKT_SEQ 0xf
#define BIT_RXBEQPKT_SEQ(x)                                                    \
	(((x) & BIT_MASK_RXBEQPKT_SEQ) << BIT_SHIFT_RXBEQPKT_SEQ)
#define BIT_GET_RXBEQPKT_SEQ(x)                                                \
	(((x) >> BIT_SHIFT_RXBEQPKT_SEQ) & BIT_MASK_RXBEQPKT_SEQ)

#define BIT_SHIFT_RXVIQPKT_SEQ 12
#define BIT_MASK_RXVIQPKT_SEQ 0xf
#define BIT_RXVIQPKT_SEQ(x)                                                    \
	(((x) & BIT_MASK_RXVIQPKT_SEQ) << BIT_SHIFT_RXVIQPKT_SEQ)
#define BIT_GET_RXVIQPKT_SEQ(x)                                                \
	(((x) >> BIT_SHIFT_RXVIQPKT_SEQ) & BIT_MASK_RXVIQPKT_SEQ)

#define BIT_SHIFT_RXVOQPKT_SEQ 8
#define BIT_MASK_RXVOQPKT_SEQ 0xf
#define BIT_RXVOQPKT_SEQ(x)                                                    \
	(((x) & BIT_MASK_RXVOQPKT_SEQ) << BIT_SHIFT_RXVOQPKT_SEQ)
#define BIT_GET_RXVOQPKT_SEQ(x)                                                \
	(((x) >> BIT_SHIFT_RXVOQPKT_SEQ) & BIT_MASK_RXVOQPKT_SEQ)

#define BIT_RXBKQPKT_ERR BIT(7)
#define BIT_RXBEQPKT_ERR BIT(6)
#define BIT_RXVIQPKT_ERR BIT(5)
#define BIT_RXVOQPKT_ERR BIT(4)
#define BIT_RXDMA_MON_EN BIT(2)
#define BIT_RXPKT_MON_RST BIT(1)
#define BIT_RXPKT_MON_EN BIT(0)

/* 2 REG_STATE_MON				(Offset 0x06B4) */

#define BIT_SHIFT_STATE_SEL 24
#define BIT_MASK_STATE_SEL 0x1f
#define BIT_STATE_SEL(x) (((x) & BIT_MASK_STATE_SEL) << BIT_SHIFT_STATE_SEL)
#define BIT_GET_STATE_SEL(x) (((x) >> BIT_SHIFT_STATE_SEL) & BIT_MASK_STATE_SEL)

#define BIT_SHIFT_STATE_INFO 8
#define BIT_MASK_STATE_INFO 0xff
#define BIT_STATE_INFO(x) (((x) & BIT_MASK_STATE_INFO) << BIT_SHIFT_STATE_INFO)
#define BIT_GET_STATE_INFO(x)                                                  \
	(((x) >> BIT_SHIFT_STATE_INFO) & BIT_MASK_STATE_INFO)

#define BIT_UPD_NXT_STATE BIT(7)

/* 2 REG_STATE_MON				(Offset 0x06B4) */

#define BIT_SHIFT_CUR_STATE 0
#define BIT_MASK_CUR_STATE 0x7f
#define BIT_CUR_STATE(x) (((x) & BIT_MASK_CUR_STATE) << BIT_SHIFT_CUR_STATE)
#define BIT_GET_CUR_STATE(x) (((x) >> BIT_SHIFT_CUR_STATE) & BIT_MASK_CUR_STATE)

/* 2 REG_ERROR_MON				(Offset 0x06B8) */

#define BIT_MACRX_ERR_1 BIT(17)
#define BIT_MACRX_ERR_0 BIT(16)
#define BIT_MACTX_ERR_3 BIT(3)
#define BIT_MACTX_ERR_2 BIT(2)
#define BIT_MACTX_ERR_1 BIT(1)
#define BIT_MACTX_ERR_0 BIT(0)

/* 2 REG_SEARCH_MACID			(Offset 0x06BC) */

#define BIT_EN_TXRPTBUF_CLK BIT(31)

#define BIT_SHIFT_INFO_INDEX_OFFSET 16
#define BIT_MASK_INFO_INDEX_OFFSET 0x1fff
#define BIT_INFO_INDEX_OFFSET(x)                                               \
	(((x) & BIT_MASK_INFO_INDEX_OFFSET) << BIT_SHIFT_INFO_INDEX_OFFSET)
#define BIT_GET_INFO_INDEX_OFFSET(x)                                           \
	(((x) >> BIT_SHIFT_INFO_INDEX_OFFSET) & BIT_MASK_INFO_INDEX_OFFSET)

/* 2 REG_SEARCH_MACID			(Offset 0x06BC) */

#define BIT_WMAC_SRCH_FIFOFULL BIT(15)

/* 2 REG_SEARCH_MACID			(Offset 0x06BC) */

#define BIT_DIS_INFOSRCH BIT(14)
#define BIT_DISABLE_B0 BIT(13)

#define BIT_SHIFT_INFO_ADDR_OFFSET 0
#define BIT_MASK_INFO_ADDR_OFFSET 0x1fff
#define BIT_INFO_ADDR_OFFSET(x)                                                \
	(((x) & BIT_MASK_INFO_ADDR_OFFSET) << BIT_SHIFT_INFO_ADDR_OFFSET)
#define BIT_GET_INFO_ADDR_OFFSET(x)                                            \
	(((x) >> BIT_SHIFT_INFO_ADDR_OFFSET) & BIT_MASK_INFO_ADDR_OFFSET)

/* 2 REG_BT_COEX_TABLE			(Offset 0x06C0) */

#define BIT_PRI_MASK_RX_RESP BIT(126)
#define BIT_PRI_MASK_RXOFDM BIT(125)
#define BIT_PRI_MASK_RXCCK BIT(124)

#define BIT_SHIFT_PRI_MASK_TXAC (117 & CPU_OPT_WIDTH)
#define BIT_MASK_PRI_MASK_TXAC 0x7f
#define BIT_PRI_MASK_TXAC(x)                                                   \
	(((x) & BIT_MASK_PRI_MASK_TXAC) << BIT_SHIFT_PRI_MASK_TXAC)
#define BIT_GET_PRI_MASK_TXAC(x)                                               \
	(((x) >> BIT_SHIFT_PRI_MASK_TXAC) & BIT_MASK_PRI_MASK_TXAC)

#define BIT_SHIFT_PRI_MASK_NAV (109 & CPU_OPT_WIDTH)
#define BIT_MASK_PRI_MASK_NAV 0xff
#define BIT_PRI_MASK_NAV(x)                                                    \
	(((x) & BIT_MASK_PRI_MASK_NAV) << BIT_SHIFT_PRI_MASK_NAV)
#define BIT_GET_PRI_MASK_NAV(x)                                                \
	(((x) >> BIT_SHIFT_PRI_MASK_NAV) & BIT_MASK_PRI_MASK_NAV)

#define BIT_PRI_MASK_CCK BIT(108)
#define BIT_PRI_MASK_OFDM BIT(107)
#define BIT_PRI_MASK_RTY BIT(106)

#define BIT_SHIFT_PRI_MASK_NUM (102 & CPU_OPT_WIDTH)
#define BIT_MASK_PRI_MASK_NUM 0xf
#define BIT_PRI_MASK_NUM(x)                                                    \
	(((x) & BIT_MASK_PRI_MASK_NUM) << BIT_SHIFT_PRI_MASK_NUM)
#define BIT_GET_PRI_MASK_NUM(x)                                                \
	(((x) >> BIT_SHIFT_PRI_MASK_NUM) & BIT_MASK_PRI_MASK_NUM)

#define BIT_SHIFT_PRI_MASK_TYPE (98 & CPU_OPT_WIDTH)
#define BIT_MASK_PRI_MASK_TYPE 0xf
#define BIT_PRI_MASK_TYPE(x)                                                   \
	(((x) & BIT_MASK_PRI_MASK_TYPE) << BIT_SHIFT_PRI_MASK_TYPE)
#define BIT_GET_PRI_MASK_TYPE(x)                                               \
	(((x) >> BIT_SHIFT_PRI_MASK_TYPE) & BIT_MASK_PRI_MASK_TYPE)

#define BIT_OOB BIT(97)
#define BIT_ANT_SEL BIT(96)

#define BIT_SHIFT_BREAK_TABLE_2 (80 & CPU_OPT_WIDTH)
#define BIT_MASK_BREAK_TABLE_2 0xffff
#define BIT_BREAK_TABLE_2(x)                                                   \
	(((x) & BIT_MASK_BREAK_TABLE_2) << BIT_SHIFT_BREAK_TABLE_2)
#define BIT_GET_BREAK_TABLE_2(x)                                               \
	(((x) >> BIT_SHIFT_BREAK_TABLE_2) & BIT_MASK_BREAK_TABLE_2)

#define BIT_SHIFT_BREAK_TABLE_1 (64 & CPU_OPT_WIDTH)
#define BIT_MASK_BREAK_TABLE_1 0xffff
#define BIT_BREAK_TABLE_1(x)                                                   \
	(((x) & BIT_MASK_BREAK_TABLE_1) << BIT_SHIFT_BREAK_TABLE_1)
#define BIT_GET_BREAK_TABLE_1(x)                                               \
	(((x) >> BIT_SHIFT_BREAK_TABLE_1) & BIT_MASK_BREAK_TABLE_1)

#define BIT_SHIFT_COEX_TABLE_2 (32 & CPU_OPT_WIDTH)
#define BIT_MASK_COEX_TABLE_2 0xffffffffL
#define BIT_COEX_TABLE_2(x)                                                    \
	(((x) & BIT_MASK_COEX_TABLE_2) << BIT_SHIFT_COEX_TABLE_2)
#define BIT_GET_COEX_TABLE_2(x)                                                \
	(((x) >> BIT_SHIFT_COEX_TABLE_2) & BIT_MASK_COEX_TABLE_2)

#define BIT_SHIFT_COEX_TABLE_1 0
#define BIT_MASK_COEX_TABLE_1 0xffffffffL
#define BIT_COEX_TABLE_1(x)                                                    \
	(((x) & BIT_MASK_COEX_TABLE_1) << BIT_SHIFT_COEX_TABLE_1)
#define BIT_GET_COEX_TABLE_1(x)                                                \
	(((x) >> BIT_SHIFT_COEX_TABLE_1) & BIT_MASK_COEX_TABLE_1)

/* 2 REG_RXCMD_0				(Offset 0x06D0) */

#define BIT_RXCMD_EN BIT(31)

#define BIT_SHIFT_RXCMD_INFO 0
#define BIT_MASK_RXCMD_INFO 0x7fffffffL
#define BIT_RXCMD_INFO(x) (((x) & BIT_MASK_RXCMD_INFO) << BIT_SHIFT_RXCMD_INFO)
#define BIT_GET_RXCMD_INFO(x)                                                  \
	(((x) >> BIT_SHIFT_RXCMD_INFO) & BIT_MASK_RXCMD_INFO)

/* 2 REG_RXCMD_1				(Offset 0x06D4) */

#define BIT_SHIFT_RXCMD_PRD 0
#define BIT_MASK_RXCMD_PRD 0xffff
#define BIT_RXCMD_PRD(x) (((x) & BIT_MASK_RXCMD_PRD) << BIT_SHIFT_RXCMD_PRD)
#define BIT_GET_RXCMD_PRD(x) (((x) >> BIT_SHIFT_RXCMD_PRD) & BIT_MASK_RXCMD_PRD)

/* 2 REG_WMAC_RESP_TXINFO			(Offset 0x06D8) */

#define BIT_SHIFT_WMAC_RESP_MFB 25
#define BIT_MASK_WMAC_RESP_MFB 0x7f
#define BIT_WMAC_RESP_MFB(x)                                                   \
	(((x) & BIT_MASK_WMAC_RESP_MFB) << BIT_SHIFT_WMAC_RESP_MFB)
#define BIT_GET_WMAC_RESP_MFB(x)                                               \
	(((x) >> BIT_SHIFT_WMAC_RESP_MFB) & BIT_MASK_WMAC_RESP_MFB)

#define BIT_SHIFT_WMAC_ANTINF_SEL 23
#define BIT_MASK_WMAC_ANTINF_SEL 0x3
#define BIT_WMAC_ANTINF_SEL(x)                                                 \
	(((x) & BIT_MASK_WMAC_ANTINF_SEL) << BIT_SHIFT_WMAC_ANTINF_SEL)
#define BIT_GET_WMAC_ANTINF_SEL(x)                                             \
	(((x) >> BIT_SHIFT_WMAC_ANTINF_SEL) & BIT_MASK_WMAC_ANTINF_SEL)

#define BIT_SHIFT_WMAC_ANTSEL_SEL 21
#define BIT_MASK_WMAC_ANTSEL_SEL 0x3
#define BIT_WMAC_ANTSEL_SEL(x)                                                 \
	(((x) & BIT_MASK_WMAC_ANTSEL_SEL) << BIT_SHIFT_WMAC_ANTSEL_SEL)
#define BIT_GET_WMAC_ANTSEL_SEL(x)                                             \
	(((x) >> BIT_SHIFT_WMAC_ANTSEL_SEL) & BIT_MASK_WMAC_ANTSEL_SEL)

/* 2 REG_WMAC_RESP_TXINFO			(Offset 0x06D8) */

#define BIT_SHIFT_R_WMAC_RESP_TXPOWER 18
#define BIT_MASK_R_WMAC_RESP_TXPOWER 0x7
#define BIT_R_WMAC_RESP_TXPOWER(x)                                             \
	(((x) & BIT_MASK_R_WMAC_RESP_TXPOWER) << BIT_SHIFT_R_WMAC_RESP_TXPOWER)
#define BIT_GET_R_WMAC_RESP_TXPOWER(x)                                         \
	(((x) >> BIT_SHIFT_R_WMAC_RESP_TXPOWER) & BIT_MASK_R_WMAC_RESP_TXPOWER)

/* 2 REG_WMAC_RESP_TXINFO			(Offset 0x06D8) */

#define BIT_SHIFT_WMAC_RESP_TXANT 0
#define BIT_MASK_WMAC_RESP_TXANT 0x3ffff
#define BIT_WMAC_RESP_TXANT(x)                                                 \
	(((x) & BIT_MASK_WMAC_RESP_TXANT) << BIT_SHIFT_WMAC_RESP_TXANT)
#define BIT_GET_WMAC_RESP_TXANT(x)                                             \
	(((x) >> BIT_SHIFT_WMAC_RESP_TXANT) & BIT_MASK_WMAC_RESP_TXANT)

/* 2 REG_BBPSF_CTRL				(Offset 0x06DC) */

#define BIT_CTL_IDLE_CLR_CSI_RPT BIT(31)

/* 2 REG_BBPSF_CTRL				(Offset 0x06DC) */

#define BIT_WMAC_USE_NDPARATE BIT(30)

#define BIT_SHIFT_WMAC_CSI_RATE 24
#define BIT_MASK_WMAC_CSI_RATE 0x3f
#define BIT_WMAC_CSI_RATE(x)                                                   \
	(((x) & BIT_MASK_WMAC_CSI_RATE) << BIT_SHIFT_WMAC_CSI_RATE)
#define BIT_GET_WMAC_CSI_RATE(x)                                               \
	(((x) >> BIT_SHIFT_WMAC_CSI_RATE) & BIT_MASK_WMAC_CSI_RATE)

#define BIT_SHIFT_WMAC_RESP_TXRATE 16
#define BIT_MASK_WMAC_RESP_TXRATE 0xff
#define BIT_WMAC_RESP_TXRATE(x)                                                \
	(((x) & BIT_MASK_WMAC_RESP_TXRATE) << BIT_SHIFT_WMAC_RESP_TXRATE)
#define BIT_GET_WMAC_RESP_TXRATE(x)                                            \
	(((x) >> BIT_SHIFT_WMAC_RESP_TXRATE) & BIT_MASK_WMAC_RESP_TXRATE)

/* 2 REG_BBPSF_CTRL				(Offset 0x06DC) */

#define BIT_BBPSF_MPDUCHKEN BIT(5)

/* 2 REG_BBPSF_CTRL				(Offset 0x06DC) */

#define BIT_BBPSF_MHCHKEN BIT(4)
#define BIT_BBPSF_ERRCHKEN BIT(3)

#define BIT_SHIFT_BBPSF_ERRTHR 0
#define BIT_MASK_BBPSF_ERRTHR 0x7
#define BIT_BBPSF_ERRTHR(x)                                                    \
	(((x) & BIT_MASK_BBPSF_ERRTHR) << BIT_SHIFT_BBPSF_ERRTHR)
#define BIT_GET_BBPSF_ERRTHR(x)                                                \
	(((x) >> BIT_SHIFT_BBPSF_ERRTHR) & BIT_MASK_BBPSF_ERRTHR)

/* 2 REG_P2P_RX_BCN_NOA			(Offset 0x06E0) */

#define BIT_NOA_PARSER_EN BIT(15)

/* 2 REG_P2P_RX_BCN_NOA			(Offset 0x06E0) */

#define BIT_BSSID_SEL BIT(14)

/* 2 REG_P2P_RX_BCN_NOA			(Offset 0x06E0) */

#define BIT_SHIFT_P2P_OUI_TYPE 0
#define BIT_MASK_P2P_OUI_TYPE 0xff
#define BIT_P2P_OUI_TYPE(x)                                                    \
	(((x) & BIT_MASK_P2P_OUI_TYPE) << BIT_SHIFT_P2P_OUI_TYPE)
#define BIT_GET_P2P_OUI_TYPE(x)                                                \
	(((x) >> BIT_SHIFT_P2P_OUI_TYPE) & BIT_MASK_P2P_OUI_TYPE)

/* 2 REG_ASSOCIATED_BFMER0_INFO		(Offset 0x06E4) */

#define BIT_SHIFT_R_WMAC_TXCSI_AID0 (48 & CPU_OPT_WIDTH)
#define BIT_MASK_R_WMAC_TXCSI_AID0 0x1ff
#define BIT_R_WMAC_TXCSI_AID0(x)                                               \
	(((x) & BIT_MASK_R_WMAC_TXCSI_AID0) << BIT_SHIFT_R_WMAC_TXCSI_AID0)
#define BIT_GET_R_WMAC_TXCSI_AID0(x)                                           \
	(((x) >> BIT_SHIFT_R_WMAC_TXCSI_AID0) & BIT_MASK_R_WMAC_TXCSI_AID0)

#define BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R0 0
#define BIT_MASK_R_WMAC_SOUNDING_RXADD_R0 0xffffffffffffL
#define BIT_R_WMAC_SOUNDING_RXADD_R0(x)                                        \
	(((x) & BIT_MASK_R_WMAC_SOUNDING_RXADD_R0)                             \
	 << BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R0)
#define BIT_GET_R_WMAC_SOUNDING_RXADD_R0(x)                                    \
	(((x) >> BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R0) &                         \
	 BIT_MASK_R_WMAC_SOUNDING_RXADD_R0)

/* 2 REG_ASSOCIATED_BFMER1_INFO		(Offset 0x06EC) */

#define BIT_SHIFT_R_WMAC_TXCSI_AID1 (48 & CPU_OPT_WIDTH)
#define BIT_MASK_R_WMAC_TXCSI_AID1 0x1ff
#define BIT_R_WMAC_TXCSI_AID1(x)                                               \
	(((x) & BIT_MASK_R_WMAC_TXCSI_AID1) << BIT_SHIFT_R_WMAC_TXCSI_AID1)
#define BIT_GET_R_WMAC_TXCSI_AID1(x)                                           \
	(((x) >> BIT_SHIFT_R_WMAC_TXCSI_AID1) & BIT_MASK_R_WMAC_TXCSI_AID1)

#define BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R1 0
#define BIT_MASK_R_WMAC_SOUNDING_RXADD_R1 0xffffffffffffL
#define BIT_R_WMAC_SOUNDING_RXADD_R1(x)                                        \
	(((x) & BIT_MASK_R_WMAC_SOUNDING_RXADD_R1)                             \
	 << BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R1)
#define BIT_GET_R_WMAC_SOUNDING_RXADD_R1(x)                                    \
	(((x) >> BIT_SHIFT_R_WMAC_SOUNDING_RXADD_R1) &                         \
	 BIT_MASK_R_WMAC_SOUNDING_RXADD_R1)

/* 2 REG_TX_CSI_RPT_PARAM_BW20		(Offset 0x06F4) */

#define BIT_SHIFT_R_WMAC_BFINFO_20M_1 16
#define BIT_MASK_R_WMAC_BFINFO_20M_1 0xfff
#define BIT_R_WMAC_BFINFO_20M_1(x)                                             \
	(((x) & BIT_MASK_R_WMAC_BFINFO_20M_1) << BIT_SHIFT_R_WMAC_BFINFO_20M_1)
#define BIT_GET_R_WMAC_BFINFO_20M_1(x)                                         \
	(((x) >> BIT_SHIFT_R_WMAC_BFINFO_20M_1) & BIT_MASK_R_WMAC_BFINFO_20M_1)

#define BIT_SHIFT_R_WMAC_BFINFO_20M_0 0
#define BIT_MASK_R_WMAC_BFINFO_20M_0 0xfff
#define BIT_R_WMAC_BFINFO_20M_0(x)                                             \
	(((x) & BIT_MASK_R_WMAC_BFINFO_20M_0) << BIT_SHIFT_R_WMAC_BFINFO_20M_0)
#define BIT_GET_R_WMAC_BFINFO_20M_0(x)                                         \
	(((x) >> BIT_SHIFT_R_WMAC_BFINFO_20M_0) & BIT_MASK_R_WMAC_BFINFO_20M_0)

/* 2 REG_TX_CSI_RPT_PARAM_BW40		(Offset 0x06F8) */

#define BIT_SHIFT_WMAC_RESP_ANTCD 0
#define BIT_MASK_WMAC_RESP_ANTCD 0xf
#define BIT_WMAC_RESP_ANTCD(x)                                                 \
	(((x) & BIT_MASK_WMAC_RESP_ANTCD) << BIT_SHIFT_WMAC_RESP_ANTCD)
#define BIT_GET_WMAC_RESP_ANTCD(x)                                             \
	(((x) >> BIT_SHIFT_WMAC_RESP_ANTCD) & BIT_MASK_WMAC_RESP_ANTCD)

/* 2 REG_MACID1				(Offset 0x0700) */

#define BIT_SHIFT_MACID1 0
#define BIT_MASK_MACID1 0xffffffffffffL
#define BIT_MACID1(x) (((x) & BIT_MASK_MACID1) << BIT_SHIFT_MACID1)
#define BIT_GET_MACID1(x) (((x) >> BIT_SHIFT_MACID1) & BIT_MASK_MACID1)

/* 2 REG_BSSID1				(Offset 0x0708) */

#define BIT_SHIFT_BSSID1 0
#define BIT_MASK_BSSID1 0xffffffffffffL
#define BIT_BSSID1(x) (((x) & BIT_MASK_BSSID1) << BIT_SHIFT_BSSID1)
#define BIT_GET_BSSID1(x) (((x) >> BIT_SHIFT_BSSID1) & BIT_MASK_BSSID1)

/* 2 REG_BCN_PSR_RPT1			(Offset 0x0710) */

#define BIT_SHIFT_DTIM_CNT1 24
#define BIT_MASK_DTIM_CNT1 0xff
#define BIT_DTIM_CNT1(x) (((x) & BIT_MASK_DTIM_CNT1) << BIT_SHIFT_DTIM_CNT1)
#define BIT_GET_DTIM_CNT1(x) (((x) >> BIT_SHIFT_DTIM_CNT1) & BIT_MASK_DTIM_CNT1)

#define BIT_SHIFT_DTIM_PERIOD1 16
#define BIT_MASK_DTIM_PERIOD1 0xff
#define BIT_DTIM_PERIOD1(x)                                                    \
	(((x) & BIT_MASK_DTIM_PERIOD1) << BIT_SHIFT_DTIM_PERIOD1)
#define BIT_GET_DTIM_PERIOD1(x)                                                \
	(((x) >> BIT_SHIFT_DTIM_PERIOD1) & BIT_MASK_DTIM_PERIOD1)

#define BIT_DTIM1 BIT(15)
#define BIT_TIM1 BIT(14)

#define BIT_SHIFT_PS_AID_1 0
#define BIT_MASK_PS_AID_1 0x7ff
#define BIT_PS_AID_1(x) (((x) & BIT_MASK_PS_AID_1) << BIT_SHIFT_PS_AID_1)
#define BIT_GET_PS_AID_1(x) (((x) >> BIT_SHIFT_PS_AID_1) & BIT_MASK_PS_AID_1)

/* 2 REG_ASSOCIATED_BFMEE_SEL		(Offset 0x0714) */

#define BIT_TXUSER_ID1 BIT(25)

#define BIT_SHIFT_AID1 16
#define BIT_MASK_AID1 0x1ff
#define BIT_AID1(x) (((x) & BIT_MASK_AID1) << BIT_SHIFT_AID1)
#define BIT_GET_AID1(x) (((x) >> BIT_SHIFT_AID1) & BIT_MASK_AID1)

#define BIT_TXUSER_ID0 BIT(9)

#define BIT_SHIFT_AID0 0
#define BIT_MASK_AID0 0x1ff
#define BIT_AID0(x) (((x) & BIT_MASK_AID0) << BIT_SHIFT_AID0)
#define BIT_GET_AID0(x) (((x) >> BIT_SHIFT_AID0) & BIT_MASK_AID0)

/* 2 REG_SND_PTCL_CTRL			(Offset 0x0718) */

#define BIT_SHIFT_NDP_RX_STANDBY_TIMER 24
#define BIT_MASK_NDP_RX_STANDBY_TIMER 0xff
#define BIT_NDP_RX_STANDBY_TIMER(x)                                            \
	(((x) & BIT_MASK_NDP_RX_STANDBY_TIMER)                                 \
	 << BIT_SHIFT_NDP_RX_STANDBY_TIMER)
#define BIT_GET_NDP_RX_STANDBY_TIMER(x)                                        \
	(((x) >> BIT_SHIFT_NDP_RX_STANDBY_TIMER) &                             \
	 BIT_MASK_NDP_RX_STANDBY_TIMER)

#define BIT_SHIFT_CSI_RPT_OFFSET_HT 16
#define BIT_MASK_CSI_RPT_OFFSET_HT 0xff
#define BIT_CSI_RPT_OFFSET_HT(x)                                               \
	(((x) & BIT_MASK_CSI_RPT_OFFSET_HT) << BIT_SHIFT_CSI_RPT_OFFSET_HT)
#define BIT_GET_CSI_RPT_OFFSET_HT(x)                                           \
	(((x) >> BIT_SHIFT_CSI_RPT_OFFSET_HT) & BIT_MASK_CSI_RPT_OFFSET_HT)

/* 2 REG_SND_PTCL_CTRL			(Offset 0x0718) */

#define BIT_SHIFT_R_WMAC_VHT_CATEGORY 8
#define BIT_MASK_R_WMAC_VHT_CATEGORY 0xff
#define BIT_R_WMAC_VHT_CATEGORY(x)                                             \
	(((x) & BIT_MASK_R_WMAC_VHT_CATEGORY) << BIT_SHIFT_R_WMAC_VHT_CATEGORY)
#define BIT_GET_R_WMAC_VHT_CATEGORY(x)                                         \
	(((x) >> BIT_SHIFT_R_WMAC_VHT_CATEGORY) & BIT_MASK_R_WMAC_VHT_CATEGORY)

/* 2 REG_SND_PTCL_CTRL			(Offset 0x0718) */

#define BIT_R_WMAC_USE_NSTS BIT(7)
#define BIT_R_DISABLE_CHECK_VHTSIGB_CRC BIT(6)
#define BIT_R_DISABLE_CHECK_VHTSIGA_CRC BIT(5)
#define BIT_R_WMAC_BFPARAM_SEL BIT(4)
#define BIT_R_WMAC_CSISEQ_SEL BIT(3)
#define BIT_R_WMAC_CSI_WITHHTC_EN BIT(2)
#define BIT_R_WMAC_HT_NDPA_EN BIT(1)
#define BIT_R_WMAC_VHT_NDPA_EN BIT(0)

/* 2 REG_NS_ARP_CTRL				(Offset 0x0720) */

#define BIT_R_WMAC_NSARP_RSPEN BIT(15)
#define BIT_R_WMAC_NSARP_RARP BIT(9)
#define BIT_R_WMAC_NSARP_RIPV6 BIT(8)

#define BIT_SHIFT_R_WMAC_NSARP_MODEN 6
#define BIT_MASK_R_WMAC_NSARP_MODEN 0x3
#define BIT_R_WMAC_NSARP_MODEN(x)                                              \
	(((x) & BIT_MASK_R_WMAC_NSARP_MODEN) << BIT_SHIFT_R_WMAC_NSARP_MODEN)
#define BIT_GET_R_WMAC_NSARP_MODEN(x)                                          \
	(((x) >> BIT_SHIFT_R_WMAC_NSARP_MODEN) & BIT_MASK_R_WMAC_NSARP_MODEN)

#define BIT_SHIFT_R_WMAC_NSARP_RSPFTP 4
#define BIT_MASK_R_WMAC_NSARP_RSPFTP 0x3
#define BIT_R_WMAC_NSARP_RSPFTP(x)                                             \
	(((x) & BIT_MASK_R_WMAC_NSARP_RSPFTP) << BIT_SHIFT_R_WMAC_NSARP_RSPFTP)
#define BIT_GET_R_WMAC_NSARP_RSPFTP(x)                                         \
	(((x) >> BIT_SHIFT_R_WMAC_NSARP_RSPFTP) & BIT_MASK_R_WMAC_NSARP_RSPFTP)

#define BIT_SHIFT_R_WMAC_NSARP_RSPSEC 0
#define BIT_MASK_R_WMAC_NSARP_RSPSEC 0xf
#define BIT_R_WMAC_NSARP_RSPSEC(x)                                             \
	(((x) & BIT_MASK_R_WMAC_NSARP_RSPSEC) << BIT_SHIFT_R_WMAC_NSARP_RSPSEC)
#define BIT_GET_R_WMAC_NSARP_RSPSEC(x)                                         \
	(((x) >> BIT_SHIFT_R_WMAC_NSARP_RSPSEC) & BIT_MASK_R_WMAC_NSARP_RSPSEC)

/* 2 REG_NS_ARP_INFO				(Offset 0x0724) */

#define BIT_REQ_IS_MCNS BIT(23)
#define BIT_REQ_IS_UCNS BIT(22)
#define BIT_REQ_IS_USNS BIT(21)
#define BIT_REQ_IS_ARP BIT(20)
#define BIT_EXPRSP_MH_WITHQC BIT(19)

#define BIT_SHIFT_EXPRSP_SECTYPE 16
#define BIT_MASK_EXPRSP_SECTYPE 0x7
#define BIT_EXPRSP_SECTYPE(x)                                                  \
	(((x) & BIT_MASK_EXPRSP_SECTYPE) << BIT_SHIFT_EXPRSP_SECTYPE)
#define BIT_GET_EXPRSP_SECTYPE(x)                                              \
	(((x) >> BIT_SHIFT_EXPRSP_SECTYPE) & BIT_MASK_EXPRSP_SECTYPE)

#define BIT_SHIFT_EXPRSP_CHKSM_7_TO_0 8
#define BIT_MASK_EXPRSP_CHKSM_7_TO_0 0xff
#define BIT_EXPRSP_CHKSM_7_TO_0(x)                                             \
	(((x) & BIT_MASK_EXPRSP_CHKSM_7_TO_0) << BIT_SHIFT_EXPRSP_CHKSM_7_TO_0)
#define BIT_GET_EXPRSP_CHKSM_7_TO_0(x)                                         \
	(((x) >> BIT_SHIFT_EXPRSP_CHKSM_7_TO_0) & BIT_MASK_EXPRSP_CHKSM_7_TO_0)

#define BIT_SHIFT_EXPRSP_CHKSM_15_TO_8 0
#define BIT_MASK_EXPRSP_CHKSM_15_TO_8 0xff
#define BIT_EXPRSP_CHKSM_15_TO_8(x)                                            \
	(((x) & BIT_MASK_EXPRSP_CHKSM_15_TO_8)                                 \
	 << BIT_SHIFT_EXPRSP_CHKSM_15_TO_8)
#define BIT_GET_EXPRSP_CHKSM_15_TO_8(x)                                        \
	(((x) >> BIT_SHIFT_EXPRSP_CHKSM_15_TO_8) &                             \
	 BIT_MASK_EXPRSP_CHKSM_15_TO_8)

/* 2 REG_BEAMFORMING_INFO_NSARP_V1		(Offset 0x0728) */

#define BIT_SHIFT_WMAC_ARPIP 0
#define BIT_MASK_WMAC_ARPIP 0xffffffffL
#define BIT_WMAC_ARPIP(x) (((x) & BIT_MASK_WMAC_ARPIP) << BIT_SHIFT_WMAC_ARPIP)
#define BIT_GET_WMAC_ARPIP(x)                                                  \
	(((x) >> BIT_SHIFT_WMAC_ARPIP) & BIT_MASK_WMAC_ARPIP)

/* 2 REG_BEAMFORMING_INFO_NSARP		(Offset 0x072C) */

#define BIT_SHIFT_BEAMFORMING_INFO 0
#define BIT_MASK_BEAMFORMING_INFO 0xffffffffL
#define BIT_BEAMFORMING_INFO(x)                                                \
	(((x) & BIT_MASK_BEAMFORMING_INFO) << BIT_SHIFT_BEAMFORMING_INFO)
#define BIT_GET_BEAMFORMING_INFO(x)                                            \
	(((x) >> BIT_SHIFT_BEAMFORMING_INFO) & BIT_MASK_BEAMFORMING_INFO)

/* 2 REG_WMAC_RTX_CTX_SUBTYPE_CFG		(Offset 0x0750) */

#define BIT_SHIFT_R_WMAC_CTX_SUBTYPE 4
#define BIT_MASK_R_WMAC_CTX_SUBTYPE 0xf
#define BIT_R_WMAC_CTX_SUBTYPE(x)                                              \
	(((x) & BIT_MASK_R_WMAC_CTX_SUBTYPE) << BIT_SHIFT_R_WMAC_CTX_SUBTYPE)
#define BIT_GET_R_WMAC_CTX_SUBTYPE(x)                                          \
	(((x) >> BIT_SHIFT_R_WMAC_CTX_SUBTYPE) & BIT_MASK_R_WMAC_CTX_SUBTYPE)

#define BIT_SHIFT_R_WMAC_RTX_SUBTYPE 0
#define BIT_MASK_R_WMAC_RTX_SUBTYPE 0xf
#define BIT_R_WMAC_RTX_SUBTYPE(x)                                              \
	(((x) & BIT_MASK_R_WMAC_RTX_SUBTYPE) << BIT_SHIFT_R_WMAC_RTX_SUBTYPE)
#define BIT_GET_R_WMAC_RTX_SUBTYPE(x)                                          \
	(((x) >> BIT_SHIFT_R_WMAC_RTX_SUBTYPE) & BIT_MASK_R_WMAC_RTX_SUBTYPE)

/* 2 REG_BT_COEX_V2				(Offset 0x0762) */

#define BIT_GNT_BT_POLARITY BIT(12)
#define BIT_GNT_BT_BYPASS_PRIORITY BIT(8)

#define BIT_SHIFT_TIMER 0
#define BIT_MASK_TIMER 0xff
#define BIT_TIMER(x) (((x) & BIT_MASK_TIMER) << BIT_SHIFT_TIMER)
#define BIT_GET_TIMER(x) (((x) >> BIT_SHIFT_TIMER) & BIT_MASK_TIMER)

/* 2 REG_BT_COEX				(Offset 0x0764) */

#define BIT_R_GNT_BT_RFC_SW BIT(12)
#define BIT_R_GNT_BT_RFC_SW_EN BIT(11)
#define BIT_R_GNT_BT_BB_SW BIT(10)
#define BIT_R_GNT_BT_BB_SW_EN BIT(9)
#define BIT_R_BT_CNT_THREN BIT(8)

#define BIT_SHIFT_R_BT_CNT_THR 0
#define BIT_MASK_R_BT_CNT_THR 0xff
#define BIT_R_BT_CNT_THR(x)                                                    \
	(((x) & BIT_MASK_R_BT_CNT_THR) << BIT_SHIFT_R_BT_CNT_THR)
#define BIT_GET_R_BT_CNT_THR(x)                                                \
	(((x) >> BIT_SHIFT_R_BT_CNT_THR) & BIT_MASK_R_BT_CNT_THR)

/* 2 REG_WLAN_ACT_MASK_CTRL			(Offset 0x0768) */

#define BIT_WLRX_TER_BY_CTL BIT(43)
#define BIT_WLRX_TER_BY_AD BIT(42)
#define BIT_ANT_DIVERSITY_SEL BIT(41)
#define BIT_ANTSEL_FOR_BT_CTRL_EN BIT(40)
#define BIT_WLACT_LOW_GNTWL_EN BIT(34)
#define BIT_WLACT_HIGH_GNTBT_EN BIT(33)

/* 2 REG_WLAN_ACT_MASK_CTRL			(Offset 0x0768) */

#define BIT_NAV_UPPER_V1 BIT(32)

/* 2 REG_WLAN_ACT_MASK_CTRL			(Offset 0x0768) */

#define BIT_SHIFT_RXMYRTS_NAV_V1 8
#define BIT_MASK_RXMYRTS_NAV_V1 0xff
#define BIT_RXMYRTS_NAV_V1(x)                                                  \
	(((x) & BIT_MASK_RXMYRTS_NAV_V1) << BIT_SHIFT_RXMYRTS_NAV_V1)
#define BIT_GET_RXMYRTS_NAV_V1(x)                                              \
	(((x) >> BIT_SHIFT_RXMYRTS_NAV_V1) & BIT_MASK_RXMYRTS_NAV_V1)

#define BIT_SHIFT_RTSRST_V1 0
#define BIT_MASK_RTSRST_V1 0xff
#define BIT_RTSRST_V1(x) (((x) & BIT_MASK_RTSRST_V1) << BIT_SHIFT_RTSRST_V1)
#define BIT_GET_RTSRST_V1(x) (((x) >> BIT_SHIFT_RTSRST_V1) & BIT_MASK_RTSRST_V1)

/* 2 REG_BT_COEX_ENHANCED_INTR_CTRL		(Offset 0x076E) */

#define BIT_SHIFT_BT_STAT_DELAY 12
#define BIT_MASK_BT_STAT_DELAY 0xf
#define BIT_BT_STAT_DELAY(x)                                                   \
	(((x) & BIT_MASK_BT_STAT_DELAY) << BIT_SHIFT_BT_STAT_DELAY)
#define BIT_GET_BT_STAT_DELAY(x)                                               \
	(((x) >> BIT_SHIFT_BT_STAT_DELAY) & BIT_MASK_BT_STAT_DELAY)

#define BIT_SHIFT_BT_TRX_INIT_DETECT 8
#define BIT_MASK_BT_TRX_INIT_DETECT 0xf
#define BIT_BT_TRX_INIT_DETECT(x)                                              \
	(((x) & BIT_MASK_BT_TRX_INIT_DETECT) << BIT_SHIFT_BT_TRX_INIT_DETECT)
#define BIT_GET_BT_TRX_INIT_DETECT(x)                                          \
	(((x) >> BIT_SHIFT_BT_TRX_INIT_DETECT) & BIT_MASK_BT_TRX_INIT_DETECT)

#define BIT_SHIFT_BT_PRI_DETECT_TO 4
#define BIT_MASK_BT_PRI_DETECT_TO 0xf
#define BIT_BT_PRI_DETECT_TO(x)                                                \
	(((x) & BIT_MASK_BT_PRI_DETECT_TO) << BIT_SHIFT_BT_PRI_DETECT_TO)
#define BIT_GET_BT_PRI_DETECT_TO(x)                                            \
	(((x) >> BIT_SHIFT_BT_PRI_DETECT_TO) & BIT_MASK_BT_PRI_DETECT_TO)

#define BIT_R_GRANTALL_WLMASK BIT(3)
#define BIT_STATIS_BT_EN BIT(2)
#define BIT_WL_ACT_MASK_ENABLE BIT(1)
#define BIT_ENHANCED_BT BIT(0)

/* 2 REG_BT_ACT_STATISTICS			(Offset 0x0770) */

#define BIT_SHIFT_STATIS_BT_LO_RX (48 & CPU_OPT_WIDTH)
#define BIT_MASK_STATIS_BT_LO_RX 0xffff
#define BIT_STATIS_BT_LO_RX(x)                                                 \
	(((x) & BIT_MASK_STATIS_BT_LO_RX) << BIT_SHIFT_STATIS_BT_LO_RX)
#define BIT_GET_STATIS_BT_LO_RX(x)                                             \
	(((x) >> BIT_SHIFT_STATIS_BT_LO_RX) & BIT_MASK_STATIS_BT_LO_RX)

#define BIT_SHIFT_STATIS_BT_LO_TX (32 & CPU_OPT_WIDTH)
#define BIT_MASK_STATIS_BT_LO_TX 0xffff
#define BIT_STATIS_BT_LO_TX(x)                                                 \
	(((x) & BIT_MASK_STATIS_BT_LO_TX) << BIT_SHIFT_STATIS_BT_LO_TX)
#define BIT_GET_STATIS_BT_LO_TX(x)                                             \
	(((x) >> BIT_SHIFT_STATIS_BT_LO_TX) & BIT_MASK_STATIS_BT_LO_TX)

/* 2 REG_BT_ACT_STATISTICS			(Offset 0x0770) */

#define BIT_SHIFT_STATIS_BT_HI_RX 16
#define BIT_MASK_STATIS_BT_HI_RX 0xffff
#define BIT_STATIS_BT_HI_RX(x)                                                 \
	(((x) & BIT_MASK_STATIS_BT_HI_RX) << BIT_SHIFT_STATIS_BT_HI_RX)
#define BIT_GET_STATIS_BT_HI_RX(x)                                             \
	(((x) >> BIT_SHIFT_STATIS_BT_HI_RX) & BIT_MASK_STATIS_BT_HI_RX)

#define BIT_SHIFT_STATIS_BT_HI_TX 0
#define BIT_MASK_STATIS_BT_HI_TX 0xffff
#define BIT_STATIS_BT_HI_TX(x)                                                 \
	(((x) & BIT_MASK_STATIS_BT_HI_TX) << BIT_SHIFT_STATIS_BT_HI_TX)
#define BIT_GET_STATIS_BT_HI_TX(x)                                             \
	(((x) >> BIT_SHIFT_STATIS_BT_HI_TX) & BIT_MASK_STATIS_BT_HI_TX)

/* 2 REG_BT_STATISTICS_CONTROL_REGISTER	(Offset 0x0778) */

#define BIT_SHIFT_R_BT_CMD_RPT 16
#define BIT_MASK_R_BT_CMD_RPT 0xffff
#define BIT_R_BT_CMD_RPT(x)                                                    \
	(((x) & BIT_MASK_R_BT_CMD_RPT) << BIT_SHIFT_R_BT_CMD_RPT)
#define BIT_GET_R_BT_CMD_RPT(x)                                                \
	(((x) >> BIT_SHIFT_R_BT_CMD_RPT) & BIT_MASK_R_BT_CMD_RPT)

#define BIT_SHIFT_R_RPT_FROM_BT 8
#define BIT_MASK_R_RPT_FROM_BT 0xff
#define BIT_R_RPT_FROM_BT(x)                                                   \
	(((x) & BIT_MASK_R_RPT_FROM_BT) << BIT_SHIFT_R_RPT_FROM_BT)
#define BIT_GET_R_RPT_FROM_BT(x)                                               \
	(((x) >> BIT_SHIFT_R_RPT_FROM_BT) & BIT_MASK_R_RPT_FROM_BT)

#define BIT_SHIFT_BT_HID_ISR_SET 6
#define BIT_MASK_BT_HID_ISR_SET 0x3
#define BIT_BT_HID_ISR_SET(x)                                                  \
	(((x) & BIT_MASK_BT_HID_ISR_SET) << BIT_SHIFT_BT_HID_ISR_SET)
#define BIT_GET_BT_HID_ISR_SET(x)                                              \
	(((x) >> BIT_SHIFT_BT_HID_ISR_SET) & BIT_MASK_BT_HID_ISR_SET)

#define BIT_TDMA_BT_START_NOTIFY BIT(5)
#define BIT_ENABLE_TDMA_FW_MODE BIT(4)
#define BIT_ENABLE_PTA_TDMA_MODE BIT(3)
#define BIT_ENABLE_COEXIST_TAB_IN_TDMA BIT(2)
#define BIT_GPIO2_GPIO3_EXANGE_OR_NO_BT_CCA BIT(1)
#define BIT_RTK_BT_ENABLE BIT(0)

/* 2 REG_BT_STATUS_REPORT_REGISTER		(Offset 0x077C) */

#define BIT_SHIFT_BT_PROFILE 24
#define BIT_MASK_BT_PROFILE 0xff
#define BIT_BT_PROFILE(x) (((x) & BIT_MASK_BT_PROFILE) << BIT_SHIFT_BT_PROFILE)
#define BIT_GET_BT_PROFILE(x)                                                  \
	(((x) >> BIT_SHIFT_BT_PROFILE) & BIT_MASK_BT_PROFILE)

#define BIT_SHIFT_BT_POWER 16
#define BIT_MASK_BT_POWER 0xff
#define BIT_BT_POWER(x) (((x) & BIT_MASK_BT_POWER) << BIT_SHIFT_BT_POWER)
#define BIT_GET_BT_POWER(x) (((x) >> BIT_SHIFT_BT_POWER) & BIT_MASK_BT_POWER)

#define BIT_SHIFT_BT_PREDECT_STATUS 8
#define BIT_MASK_BT_PREDECT_STATUS 0xff
#define BIT_BT_PREDECT_STATUS(x)                                               \
	(((x) & BIT_MASK_BT_PREDECT_STATUS) << BIT_SHIFT_BT_PREDECT_STATUS)
#define BIT_GET_BT_PREDECT_STATUS(x)                                           \
	(((x) >> BIT_SHIFT_BT_PREDECT_STATUS) & BIT_MASK_BT_PREDECT_STATUS)

#define BIT_SHIFT_BT_CMD_INFO 0
#define BIT_MASK_BT_CMD_INFO 0xff
#define BIT_BT_CMD_INFO(x)                                                     \
	(((x) & BIT_MASK_BT_CMD_INFO) << BIT_SHIFT_BT_CMD_INFO)
#define BIT_GET_BT_CMD_INFO(x)                                                 \
	(((x) >> BIT_SHIFT_BT_CMD_INFO) & BIT_MASK_BT_CMD_INFO)

/* 2 REG_BT_INTERRUPT_CONTROL_REGISTER	(Offset 0x0780) */

#define BIT_EN_MAC_NULL_PKT_NOTIFY BIT(31)
#define BIT_EN_WLAN_RPT_AND_BT_QUERY BIT(30)
#define BIT_EN_BT_STSTUS_RPT BIT(29)
#define BIT_EN_BT_POWER BIT(28)
#define BIT_EN_BT_CHANNEL BIT(27)
#define BIT_EN_BT_SLOT_CHANGE BIT(26)
#define BIT_EN_BT_PROFILE_OR_HID BIT(25)
#define BIT_WLAN_RPT_NOTIFY BIT(24)

#define BIT_SHIFT_WLAN_RPT_DATA 16
#define BIT_MASK_WLAN_RPT_DATA 0xff
#define BIT_WLAN_RPT_DATA(x)                                                   \
	(((x) & BIT_MASK_WLAN_RPT_DATA) << BIT_SHIFT_WLAN_RPT_DATA)
#define BIT_GET_WLAN_RPT_DATA(x)                                               \
	(((x) >> BIT_SHIFT_WLAN_RPT_DATA) & BIT_MASK_WLAN_RPT_DATA)

#define BIT_SHIFT_CMD_ID 8
#define BIT_MASK_CMD_ID 0xff
#define BIT_CMD_ID(x) (((x) & BIT_MASK_CMD_ID) << BIT_SHIFT_CMD_ID)
#define BIT_GET_CMD_ID(x) (((x) >> BIT_SHIFT_CMD_ID) & BIT_MASK_CMD_ID)

#define BIT_SHIFT_BT_DATA 0
#define BIT_MASK_BT_DATA 0xff
#define BIT_BT_DATA(x) (((x) & BIT_MASK_BT_DATA) << BIT_SHIFT_BT_DATA)
#define BIT_GET_BT_DATA(x) (((x) >> BIT_SHIFT_BT_DATA) & BIT_MASK_BT_DATA)

/* 2 REG_WLAN_REPORT_TIME_OUT_CONTROL_REGISTER (Offset 0x0784) */

#define BIT_SHIFT_WLAN_RPT_TO 0
#define BIT_MASK_WLAN_RPT_TO 0xff
#define BIT_WLAN_RPT_TO(x)                                                     \
	(((x) & BIT_MASK_WLAN_RPT_TO) << BIT_SHIFT_WLAN_RPT_TO)
#define BIT_GET_WLAN_RPT_TO(x)                                                 \
	(((x) >> BIT_SHIFT_WLAN_RPT_TO) & BIT_MASK_WLAN_RPT_TO)

/* 2 REG_BT_ISOLATION_TABLE_REGISTER_REGISTER (Offset 0x0785) */

#define BIT_SHIFT_ISOLATION_CHK 1
#define BIT_MASK_ISOLATION_CHK 0x7fffffffffffffffffffL
#define BIT_ISOLATION_CHK(x)                                                   \
	(((x) & BIT_MASK_ISOLATION_CHK) << BIT_SHIFT_ISOLATION_CHK)
#define BIT_GET_ISOLATION_CHK(x)                                               \
	(((x) >> BIT_SHIFT_ISOLATION_CHK) & BIT_MASK_ISOLATION_CHK)

/* 2 REG_BT_ISOLATION_TABLE_REGISTER_REGISTER (Offset 0x0785) */

#define BIT_ISOLATION_EN BIT(0)

/* 2 REG_BT_INTERRUPT_STATUS_REGISTER	(Offset 0x078F) */

#define BIT_BT_HID_ISR BIT(7)
#define BIT_BT_QUERY_ISR BIT(6)
#define BIT_MAC_NULL_PKT_NOTIFY_ISR BIT(5)
#define BIT_WLAN_RPT_ISR BIT(4)
#define BIT_BT_POWER_ISR BIT(3)
#define BIT_BT_CHANNEL_ISR BIT(2)
#define BIT_BT_SLOT_CHANGE_ISR BIT(1)
#define BIT_BT_PROFILE_ISR BIT(0)

/* 2 REG_BT_TDMA_TIME_REGISTER		(Offset 0x0790) */

#define BIT_SHIFT_BT_TIME 6
#define BIT_MASK_BT_TIME 0x3ffffff
#define BIT_BT_TIME(x) (((x) & BIT_MASK_BT_TIME) << BIT_SHIFT_BT_TIME)
#define BIT_GET_BT_TIME(x) (((x) >> BIT_SHIFT_BT_TIME) & BIT_MASK_BT_TIME)

#define BIT_SHIFT_BT_RPT_SAMPLE_RATE 0
#define BIT_MASK_BT_RPT_SAMPLE_RATE 0x3f
#define BIT_BT_RPT_SAMPLE_RATE(x)                                              \
	(((x) & BIT_MASK_BT_RPT_SAMPLE_RATE) << BIT_SHIFT_BT_RPT_SAMPLE_RATE)
#define BIT_GET_BT_RPT_SAMPLE_RATE(x)                                          \
	(((x) >> BIT_SHIFT_BT_RPT_SAMPLE_RATE) & BIT_MASK_BT_RPT_SAMPLE_RATE)

/* 2 REG_BT_ACT_REGISTER			(Offset 0x0794) */

#define BIT_SHIFT_BT_EISR_EN 16
#define BIT_MASK_BT_EISR_EN 0xff
#define BIT_BT_EISR_EN(x) (((x) & BIT_MASK_BT_EISR_EN) << BIT_SHIFT_BT_EISR_EN)
#define BIT_GET_BT_EISR_EN(x)                                                  \
	(((x) >> BIT_SHIFT_BT_EISR_EN) & BIT_MASK_BT_EISR_EN)

#define BIT_BT_ACT_FALLING_ISR BIT(10)
#define BIT_BT_ACT_RISING_ISR BIT(9)
#define BIT_TDMA_TO_ISR BIT(8)

#define BIT_SHIFT_BT_CH 0
#define BIT_MASK_BT_CH 0xff
#define BIT_BT_CH(x) (((x) & BIT_MASK_BT_CH) << BIT_SHIFT_BT_CH)
#define BIT_GET_BT_CH(x) (((x) >> BIT_SHIFT_BT_CH) & BIT_MASK_BT_CH)

/* 2 REG_OBFF_CTRL_BASIC			(Offset 0x0798) */

#define BIT_OBFF_EN_V1 BIT(31)

#define BIT_SHIFT_OBFF_STATE_V1 28
#define BIT_MASK_OBFF_STATE_V1 0x3
#define BIT_OBFF_STATE_V1(x)                                                   \
	(((x) & BIT_MASK_OBFF_STATE_V1) << BIT_SHIFT_OBFF_STATE_V1)
#define BIT_GET_OBFF_STATE_V1(x)                                               \
	(((x) >> BIT_SHIFT_OBFF_STATE_V1) & BIT_MASK_OBFF_STATE_V1)

#define BIT_OBFF_ACT_RXDMA_EN BIT(27)
#define BIT_OBFF_BLOCK_INT_EN BIT(26)
#define BIT_OBFF_AUTOACT_EN BIT(25)
#define BIT_OBFF_AUTOIDLE_EN BIT(24)

#define BIT_SHIFT_WAKE_MAX_PLS 20
#define BIT_MASK_WAKE_MAX_PLS 0x7
#define BIT_WAKE_MAX_PLS(x)                                                    \
	(((x) & BIT_MASK_WAKE_MAX_PLS) << BIT_SHIFT_WAKE_MAX_PLS)
#define BIT_GET_WAKE_MAX_PLS(x)                                                \
	(((x) >> BIT_SHIFT_WAKE_MAX_PLS) & BIT_MASK_WAKE_MAX_PLS)

#define BIT_SHIFT_WAKE_MIN_PLS 16
#define BIT_MASK_WAKE_MIN_PLS 0x7
#define BIT_WAKE_MIN_PLS(x)                                                    \
	(((x) & BIT_MASK_WAKE_MIN_PLS) << BIT_SHIFT_WAKE_MIN_PLS)
#define BIT_GET_WAKE_MIN_PLS(x)                                                \
	(((x) >> BIT_SHIFT_WAKE_MIN_PLS) & BIT_MASK_WAKE_MIN_PLS)

#define BIT_SHIFT_WAKE_MAX_F2F 12
#define BIT_MASK_WAKE_MAX_F2F 0x7
#define BIT_WAKE_MAX_F2F(x)                                                    \
	(((x) & BIT_MASK_WAKE_MAX_F2F) << BIT_SHIFT_WAKE_MAX_F2F)
#define BIT_GET_WAKE_MAX_F2F(x)                                                \
	(((x) >> BIT_SHIFT_WAKE_MAX_F2F) & BIT_MASK_WAKE_MAX_F2F)

#define BIT_SHIFT_WAKE_MIN_F2F 8
#define BIT_MASK_WAKE_MIN_F2F 0x7
#define BIT_WAKE_MIN_F2F(x)                                                    \
	(((x) & BIT_MASK_WAKE_MIN_F2F) << BIT_SHIFT_WAKE_MIN_F2F)
#define BIT_GET_WAKE_MIN_F2F(x)                                                \
	(((x) >> BIT_SHIFT_WAKE_MIN_F2F) & BIT_MASK_WAKE_MIN_F2F)

#define BIT_APP_CPU_ACT_V1 BIT(3)
#define BIT_APP_OBFF_V1 BIT(2)
#define BIT_APP_IDLE_V1 BIT(1)
#define BIT_APP_INIT_V1 BIT(0)

/* 2 REG_OBFF_CTRL2_TIMER			(Offset 0x079C) */

#define BIT_SHIFT_RX_HIGH_TIMER_IDX 24
#define BIT_MASK_RX_HIGH_TIMER_IDX 0x7
#define BIT_RX_HIGH_TIMER_IDX(x)                                               \
	(((x) & BIT_MASK_RX_HIGH_TIMER_IDX) << BIT_SHIFT_RX_HIGH_TIMER_IDX)
#define BIT_GET_RX_HIGH_TIMER_IDX(x)                                           \
	(((x) >> BIT_SHIFT_RX_HIGH_TIMER_IDX) & BIT_MASK_RX_HIGH_TIMER_IDX)

#define BIT_SHIFT_RX_MED_TIMER_IDX 16
#define BIT_MASK_RX_MED_TIMER_IDX 0x7
#define BIT_RX_MED_TIMER_IDX(x)                                                \
	(((x) & BIT_MASK_RX_MED_TIMER_IDX) << BIT_SHIFT_RX_MED_TIMER_IDX)
#define BIT_GET_RX_MED_TIMER_IDX(x)                                            \
	(((x) >> BIT_SHIFT_RX_MED_TIMER_IDX) & BIT_MASK_RX_MED_TIMER_IDX)

#define BIT_SHIFT_RX_LOW_TIMER_IDX 8
#define BIT_MASK_RX_LOW_TIMER_IDX 0x7
#define BIT_RX_LOW_TIMER_IDX(x)                                                \
	(((x) & BIT_MASK_RX_LOW_TIMER_IDX) << BIT_SHIFT_RX_LOW_TIMER_IDX)
#define BIT_GET_RX_LOW_TIMER_IDX(x)                                            \
	(((x) >> BIT_SHIFT_RX_LOW_TIMER_IDX) & BIT_MASK_RX_LOW_TIMER_IDX)

#define BIT_SHIFT_OBFF_INT_TIMER_IDX 0
#define BIT_MASK_OBFF_INT_TIMER_IDX 0x7
#define BIT_OBFF_INT_TIMER_IDX(x)                                              \
	(((x) & BIT_MASK_OBFF_INT_TIMER_IDX) << BIT_SHIFT_OBFF_INT_TIMER_IDX)
#define BIT_GET_OBFF_INT_TIMER_IDX(x)                                          \
	(((x) >> BIT_SHIFT_OBFF_INT_TIMER_IDX) & BIT_MASK_OBFF_INT_TIMER_IDX)

/* 2 REG_LTR_CTRL_BASIC			(Offset 0x07A0) */

#define BIT_LTR_EN_V1 BIT(31)
#define BIT_LTR_HW_EN_V1 BIT(30)
#define BIT_LRT_ACT_CTS_EN BIT(29)
#define BIT_LTR_ACT_RXPKT_EN BIT(28)
#define BIT_LTR_ACT_RXDMA_EN BIT(27)
#define BIT_LTR_IDLE_NO_SNOOP BIT(26)
#define BIT_SPDUP_MGTPKT BIT(25)
#define BIT_RX_AGG_EN BIT(24)
#define BIT_APP_LTR_ACT BIT(23)
#define BIT_APP_LTR_IDLE BIT(22)

#define BIT_SHIFT_HIGH_RATE_TRIG_SEL 20
#define BIT_MASK_HIGH_RATE_TRIG_SEL 0x3
#define BIT_HIGH_RATE_TRIG_SEL(x)                                              \
	(((x) & BIT_MASK_HIGH_RATE_TRIG_SEL) << BIT_SHIFT_HIGH_RATE_TRIG_SEL)
#define BIT_GET_HIGH_RATE_TRIG_SEL(x)                                          \
	(((x) >> BIT_SHIFT_HIGH_RATE_TRIG_SEL) & BIT_MASK_HIGH_RATE_TRIG_SEL)

#define BIT_SHIFT_MED_RATE_TRIG_SEL 18
#define BIT_MASK_MED_RATE_TRIG_SEL 0x3
#define BIT_MED_RATE_TRIG_SEL(x)                                               \
	(((x) & BIT_MASK_MED_RATE_TRIG_SEL) << BIT_SHIFT_MED_RATE_TRIG_SEL)
#define BIT_GET_MED_RATE_TRIG_SEL(x)                                           \
	(((x) >> BIT_SHIFT_MED_RATE_TRIG_SEL) & BIT_MASK_MED_RATE_TRIG_SEL)

#define BIT_SHIFT_LOW_RATE_TRIG_SEL 16
#define BIT_MASK_LOW_RATE_TRIG_SEL 0x3
#define BIT_LOW_RATE_TRIG_SEL(x)                                               \
	(((x) & BIT_MASK_LOW_RATE_TRIG_SEL) << BIT_SHIFT_LOW_RATE_TRIG_SEL)
#define BIT_GET_LOW_RATE_TRIG_SEL(x)                                           \
	(((x) >> BIT_SHIFT_LOW_RATE_TRIG_SEL) & BIT_MASK_LOW_RATE_TRIG_SEL)

#define BIT_SHIFT_HIGH_RATE_BD_IDX 8
#define BIT_MASK_HIGH_RATE_BD_IDX 0x7f
#define BIT_HIGH_RATE_BD_IDX(x)                                                \
	(((x) & BIT_MASK_HIGH_RATE_BD_IDX) << BIT_SHIFT_HIGH_RATE_BD_IDX)
#define BIT_GET_HIGH_RATE_BD_IDX(x)                                            \
	(((x) >> BIT_SHIFT_HIGH_RATE_BD_IDX) & BIT_MASK_HIGH_RATE_BD_IDX)

#define BIT_SHIFT_LOW_RATE_BD_IDX 0
#define BIT_MASK_LOW_RATE_BD_IDX 0x7f
#define BIT_LOW_RATE_BD_IDX(x)                                                 \
	(((x) & BIT_MASK_LOW_RATE_BD_IDX) << BIT_SHIFT_LOW_RATE_BD_IDX)
#define BIT_GET_LOW_RATE_BD_IDX(x)                                             \
	(((x) >> BIT_SHIFT_LOW_RATE_BD_IDX) & BIT_MASK_LOW_RATE_BD_IDX)

/* 2 REG_LTR_CTRL2_TIMER_THRESHOLD		(Offset 0x07A4) */

#define BIT_SHIFT_RX_EMPTY_TIMER_IDX 24
#define BIT_MASK_RX_EMPTY_TIMER_IDX 0x7
#define BIT_RX_EMPTY_TIMER_IDX(x)                                              \
	(((x) & BIT_MASK_RX_EMPTY_TIMER_IDX) << BIT_SHIFT_RX_EMPTY_TIMER_IDX)
#define BIT_GET_RX_EMPTY_TIMER_IDX(x)                                          \
	(((x) >> BIT_SHIFT_RX_EMPTY_TIMER_IDX) & BIT_MASK_RX_EMPTY_TIMER_IDX)

#define BIT_SHIFT_RX_AFULL_TH_IDX 20
#define BIT_MASK_RX_AFULL_TH_IDX 0x7
#define BIT_RX_AFULL_TH_IDX(x)                                                 \
	(((x) & BIT_MASK_RX_AFULL_TH_IDX) << BIT_SHIFT_RX_AFULL_TH_IDX)
#define BIT_GET_RX_AFULL_TH_IDX(x)                                             \
	(((x) >> BIT_SHIFT_RX_AFULL_TH_IDX) & BIT_MASK_RX_AFULL_TH_IDX)

#define BIT_SHIFT_RX_HIGH_TH_IDX 16
#define BIT_MASK_RX_HIGH_TH_IDX 0x7
#define BIT_RX_HIGH_TH_IDX(x)                                                  \
	(((x) & BIT_MASK_RX_HIGH_TH_IDX) << BIT_SHIFT_RX_HIGH_TH_IDX)
#define BIT_GET_RX_HIGH_TH_IDX(x)                                              \
	(((x) >> BIT_SHIFT_RX_HIGH_TH_IDX) & BIT_MASK_RX_HIGH_TH_IDX)

#define BIT_SHIFT_RX_MED_TH_IDX 12
#define BIT_MASK_RX_MED_TH_IDX 0x7
#define BIT_RX_MED_TH_IDX(x)                                                   \
	(((x) & BIT_MASK_RX_MED_TH_IDX) << BIT_SHIFT_RX_MED_TH_IDX)
#define BIT_GET_RX_MED_TH_IDX(x)                                               \
	(((x) >> BIT_SHIFT_RX_MED_TH_IDX) & BIT_MASK_RX_MED_TH_IDX)

#define BIT_SHIFT_RX_LOW_TH_IDX 8
#define BIT_MASK_RX_LOW_TH_IDX 0x7
#define BIT_RX_LOW_TH_IDX(x)                                                   \
	(((x) & BIT_MASK_RX_LOW_TH_IDX) << BIT_SHIFT_RX_LOW_TH_IDX)
#define BIT_GET_RX_LOW_TH_IDX(x)                                               \
	(((x) >> BIT_SHIFT_RX_LOW_TH_IDX) & BIT_MASK_RX_LOW_TH_IDX)

#define BIT_SHIFT_LTR_SPACE_IDX 4
#define BIT_MASK_LTR_SPACE_IDX 0x3
#define BIT_LTR_SPACE_IDX(x)                                                   \
	(((x) & BIT_MASK_LTR_SPACE_IDX) << BIT_SHIFT_LTR_SPACE_IDX)
#define BIT_GET_LTR_SPACE_IDX(x)                                               \
	(((x) >> BIT_SHIFT_LTR_SPACE_IDX) & BIT_MASK_LTR_SPACE_IDX)

#define BIT_SHIFT_LTR_IDLE_TIMER_IDX 0
#define BIT_MASK_LTR_IDLE_TIMER_IDX 0x7
#define BIT_LTR_IDLE_TIMER_IDX(x)                                              \
	(((x) & BIT_MASK_LTR_IDLE_TIMER_IDX) << BIT_SHIFT_LTR_IDLE_TIMER_IDX)
#define BIT_GET_LTR_IDLE_TIMER_IDX(x)                                          \
	(((x) >> BIT_SHIFT_LTR_IDLE_TIMER_IDX) & BIT_MASK_LTR_IDLE_TIMER_IDX)

/* 2 REG_LTR_IDLE_LATENCY_V1			(Offset 0x07A8) */

#define BIT_SHIFT_LTR_IDLE_L 0
#define BIT_MASK_LTR_IDLE_L 0xffffffffL
#define BIT_LTR_IDLE_L(x) (((x) & BIT_MASK_LTR_IDLE_L) << BIT_SHIFT_LTR_IDLE_L)
#define BIT_GET_LTR_IDLE_L(x)                                                  \
	(((x) >> BIT_SHIFT_LTR_IDLE_L) & BIT_MASK_LTR_IDLE_L)

/* 2 REG_LTR_ACTIVE_LATENCY_V1		(Offset 0x07AC) */

#define BIT_SHIFT_LTR_ACT_L 0
#define BIT_MASK_LTR_ACT_L 0xffffffffL
#define BIT_LTR_ACT_L(x) (((x) & BIT_MASK_LTR_ACT_L) << BIT_SHIFT_LTR_ACT_L)
#define BIT_GET_LTR_ACT_L(x) (((x) >> BIT_SHIFT_LTR_ACT_L) & BIT_MASK_LTR_ACT_L)

/* 2 REG_ANTENNA_TRAINING_CONTROL_REGISTER	(Offset 0x07B0) */

#define BIT_APPEND_MACID_IN_RESP_EN BIT(50)
#define BIT_ADDR2_MATCH_EN BIT(49)
#define BIT_ANTTRN_EN BIT(48)

#define BIT_SHIFT_TRAIN_STA_ADDR 0
#define BIT_MASK_TRAIN_STA_ADDR 0xffffffffffffL
#define BIT_TRAIN_STA_ADDR(x)                                                  \
	(((x) & BIT_MASK_TRAIN_STA_ADDR) << BIT_SHIFT_TRAIN_STA_ADDR)
#define BIT_GET_TRAIN_STA_ADDR(x)                                              \
	(((x) >> BIT_SHIFT_TRAIN_STA_ADDR) & BIT_MASK_TRAIN_STA_ADDR)

/* 2 REG_WMAC_PKTCNT_RWD			(Offset 0x07B8) */

#define BIT_SHIFT_PKTCNT_BSSIDMAP 4
#define BIT_MASK_PKTCNT_BSSIDMAP 0xf
#define BIT_PKTCNT_BSSIDMAP(x)                                                 \
	(((x) & BIT_MASK_PKTCNT_BSSIDMAP) << BIT_SHIFT_PKTCNT_BSSIDMAP)
#define BIT_GET_PKTCNT_BSSIDMAP(x)                                             \
	(((x) >> BIT_SHIFT_PKTCNT_BSSIDMAP) & BIT_MASK_PKTCNT_BSSIDMAP)

#define BIT_PKTCNT_CNTRST BIT(1)
#define BIT_PKTCNT_CNTEN BIT(0)

/* 2 REG_WMAC_PKTCNT_CTRL			(Offset 0x07BC) */

#define BIT_WMAC_PKTCNT_TRST BIT(9)
#define BIT_WMAC_PKTCNT_FEN BIT(8)

#define BIT_SHIFT_WMAC_PKTCNT_CFGAD 0
#define BIT_MASK_WMAC_PKTCNT_CFGAD 0xff
#define BIT_WMAC_PKTCNT_CFGAD(x)                                               \
	(((x) & BIT_MASK_WMAC_PKTCNT_CFGAD) << BIT_SHIFT_WMAC_PKTCNT_CFGAD)
#define BIT_GET_WMAC_PKTCNT_CFGAD(x)                                           \
	(((x) >> BIT_SHIFT_WMAC_PKTCNT_CFGAD) & BIT_MASK_WMAC_PKTCNT_CFGAD)

/* 2 REG_IQ_DUMP				(Offset 0x07C0) */

#define BIT_SHIFT_R_WMAC_MATCH_REF_MAC (64 & CPU_OPT_WIDTH)
#define BIT_MASK_R_WMAC_MATCH_REF_MAC 0xffffffffL
#define BIT_R_WMAC_MATCH_REF_MAC(x)                                            \
	(((x) & BIT_MASK_R_WMAC_MATCH_REF_MAC)                                 \
	 << BIT_SHIFT_R_WMAC_MATCH_REF_MAC)
#define BIT_GET_R_WMAC_MATCH_REF_MAC(x)                                        \
	(((x) >> BIT_SHIFT_R_WMAC_MATCH_REF_MAC) &                             \
	 BIT_MASK_R_WMAC_MATCH_REF_MAC)

#define BIT_SHIFT_R_WMAC_RX_FIL_LEN (64 & CPU_OPT_WIDTH)
#define BIT_MASK_R_WMAC_RX_FIL_LEN 0xffff
#define BIT_R_WMAC_RX_FIL_LEN(x)                                               \
	(((x) & BIT_MASK_R_WMAC_RX_FIL_LEN) << BIT_SHIFT_R_WMAC_RX_FIL_LEN)
#define BIT_GET_R_WMAC_RX_FIL_LEN(x)                                           \
	(((x) >> BIT_SHIFT_R_WMAC_RX_FIL_LEN) & BIT_MASK_R_WMAC_RX_FIL_LEN)

#define BIT_SHIFT_R_WMAC_RXFIFO_FULL_TH (56 & CPU_OPT_WIDTH)
#define BIT_MASK_R_WMAC_RXFIFO_FULL_TH 0xff
#define BIT_R_WMAC_RXFIFO_FULL_TH(x)                                           \
	(((x) & BIT_MASK_R_WMAC_RXFIFO_FULL_TH)                                \
	 << BIT_SHIFT_R_WMAC_RXFIFO_FULL_TH)
#define BIT_GET_R_WMAC_RXFIFO_FULL_TH(x)                                       \
	(((x) >> BIT_SHIFT_R_WMAC_RXFIFO_FULL_TH) &                            \
	 BIT_MASK_R_WMAC_RXFIFO_FULL_TH)

#define BIT_R_WMAC_SRCH_TXRPT_TYPE BIT(51)
#define BIT_R_WMAC_NDP_RST BIT(50)
#define BIT_R_WMAC_POWINT_EN BIT(49)
#define BIT_R_WMAC_SRCH_TXRPT_PERPKT BIT(48)
#define BIT_R_WMAC_SRCH_TXRPT_MID BIT(47)
#define BIT_R_WMAC_PFIN_TOEN BIT(46)
#define BIT_R_WMAC_FIL_SECERR BIT(45)
#define BIT_R_WMAC_FIL_CTLPKTLEN BIT(44)
#define BIT_R_WMAC_FIL_FCTYPE BIT(43)
#define BIT_R_WMAC_FIL_FCPROVER BIT(42)
#define BIT_R_WMAC_PHYSTS_SNIF BIT(41)
#define BIT_R_WMAC_PHYSTS_PLCP BIT(40)
#define BIT_R_MAC_TCR_VBONF_RD BIT(39)
#define BIT_R_WMAC_TCR_MPAR_NDP BIT(38)
#define BIT_R_WMAC_NDP_FILTER BIT(37)
#define BIT_R_WMAC_RXLEN_SEL BIT(36)
#define BIT_R_WMAC_RXLEN_SEL1 BIT(35)
#define BIT_R_OFDM_FILTER BIT(34)
#define BIT_R_WMAC_CHK_OFDM_LEN BIT(33)

#define BIT_SHIFT_R_WMAC_MASK_LA_MAC (32 & CPU_OPT_WIDTH)
#define BIT_MASK_R_WMAC_MASK_LA_MAC 0xffffffffL
#define BIT_R_WMAC_MASK_LA_MAC(x)                                              \
	(((x) & BIT_MASK_R_WMAC_MASK_LA_MAC) << BIT_SHIFT_R_WMAC_MASK_LA_MAC)
#define BIT_GET_R_WMAC_MASK_LA_MAC(x)                                          \
	(((x) >> BIT_SHIFT_R_WMAC_MASK_LA_MAC) & BIT_MASK_R_WMAC_MASK_LA_MAC)

#define BIT_R_WMAC_CHK_CCK_LEN BIT(32)

/* 2 REG_IQ_DUMP				(Offset 0x07C0) */

#define BIT_SHIFT_R_OFDM_LEN 26
#define BIT_MASK_R_OFDM_LEN 0x3f
#define BIT_R_OFDM_LEN(x) (((x) & BIT_MASK_R_OFDM_LEN) << BIT_SHIFT_R_OFDM_LEN)
#define BIT_GET_R_OFDM_LEN(x)                                                  \
	(((x) >> BIT_SHIFT_R_OFDM_LEN) & BIT_MASK_R_OFDM_LEN)

#define BIT_SHIFT_DUMP_OK_ADDR 15
#define BIT_MASK_DUMP_OK_ADDR 0x1ffff
#define BIT_DUMP_OK_ADDR(x)                                                    \
	(((x) & BIT_MASK_DUMP_OK_ADDR) << BIT_SHIFT_DUMP_OK_ADDR)
#define BIT_GET_DUMP_OK_ADDR(x)                                                \
	(((x) >> BIT_SHIFT_DUMP_OK_ADDR) & BIT_MASK_DUMP_OK_ADDR)

#define BIT_SHIFT_R_TRIG_TIME_SEL 8
#define BIT_MASK_R_TRIG_TIME_SEL 0x7f
#define BIT_R_TRIG_TIME_SEL(x)                                                 \
	(((x) & BIT_MASK_R_TRIG_TIME_SEL) << BIT_SHIFT_R_TRIG_TIME_SEL)
#define BIT_GET_R_TRIG_TIME_SEL(x)                                             \
	(((x) >> BIT_SHIFT_R_TRIG_TIME_SEL) & BIT_MASK_R_TRIG_TIME_SEL)

#define BIT_SHIFT_R_MAC_TRIG_SEL 6
#define BIT_MASK_R_MAC_TRIG_SEL 0x3
#define BIT_R_MAC_TRIG_SEL(x)                                                  \
	(((x) & BIT_MASK_R_MAC_TRIG_SEL) << BIT_SHIFT_R_MAC_TRIG_SEL)
#define BIT_GET_R_MAC_TRIG_SEL(x)                                              \
	(((x) >> BIT_SHIFT_R_MAC_TRIG_SEL) & BIT_MASK_R_MAC_TRIG_SEL)

#define BIT_MAC_TRIG_REG BIT(5)

#define BIT_SHIFT_R_LEVEL_PULSE_SEL 3
#define BIT_MASK_R_LEVEL_PULSE_SEL 0x3
#define BIT_R_LEVEL_PULSE_SEL(x)                                               \
	(((x) & BIT_MASK_R_LEVEL_PULSE_SEL) << BIT_SHIFT_R_LEVEL_PULSE_SEL)
#define BIT_GET_R_LEVEL_PULSE_SEL(x)                                           \
	(((x) >> BIT_SHIFT_R_LEVEL_PULSE_SEL) & BIT_MASK_R_LEVEL_PULSE_SEL)

#define BIT_EN_LA_MAC BIT(2)
#define BIT_R_EN_IQDUMP BIT(1)
#define BIT_R_IQDATA_DUMP BIT(0)

#define BIT_SHIFT_R_CCK_LEN 0
#define BIT_MASK_R_CCK_LEN 0xffff
#define BIT_R_CCK_LEN(x) (((x) & BIT_MASK_R_CCK_LEN) << BIT_SHIFT_R_CCK_LEN)
#define BIT_GET_R_CCK_LEN(x) (((x) >> BIT_SHIFT_R_CCK_LEN) & BIT_MASK_R_CCK_LEN)

/* 2 REG_WMAC_FTM_CTL			(Offset 0x07CC) */

#define BIT_RXFTM_TXACK_SC BIT(6)
#define BIT_RXFTM_TXACK_BW BIT(5)
#define BIT_RXFTM_EN BIT(3)
#define BIT_RXFTMREQ_BYDRV BIT(2)
#define BIT_RXFTMREQ_EN BIT(1)
#define BIT_FTM_EN BIT(0)

/* 2 REG_RX_FILTER_FUNCTION			(Offset 0x07DA) */

#define BIT_R_WMAC_MHRDDY_LATCH BIT(14)

/* 2 REG_RX_FILTER_FUNCTION			(Offset 0x07DA) */

#define BIT_R_WMAC_MHRDDY_CLR BIT(13)

/* 2 REG_RX_FILTER_FUNCTION			(Offset 0x07DA) */

#define BIT_R_RXPKTCTL_FSM_BASED_MPDURDY1 BIT(12)

/* 2 REG_RX_FILTER_FUNCTION			(Offset 0x07DA) */

#define BIT_WMAC_DIS_VHT_PLCP_CHK_MU BIT(11)

/* 2 REG_RX_FILTER_FUNCTION			(Offset 0x07DA) */

#define BIT_R_CHK_DELIMIT_LEN BIT(10)
#define BIT_R_REAPTER_ADDR_MATCH BIT(9)
#define BIT_R_RXPKTCTL_FSM_BASED_MPDURDY BIT(8)
#define BIT_R_LATCH_MACHRDY BIT(7)
#define BIT_R_WMAC_RXFIL_REND BIT(6)
#define BIT_R_WMAC_MPDURDY_CLR BIT(5)
#define BIT_R_WMAC_CLRRXSEC BIT(4)
#define BIT_R_WMAC_RXFIL_RDEL BIT(3)
#define BIT_R_WMAC_RXFIL_FCSE BIT(2)
#define BIT_R_WMAC_RXFIL_MESH_DEL BIT(1)
#define BIT_R_WMAC_RXFIL_MASKM BIT(0)

/* 2 REG_NDP_SIG				(Offset 0x07E0) */

#define BIT_SHIFT_R_WMAC_TXNDP_SIGB 0
#define BIT_MASK_R_WMAC_TXNDP_SIGB 0x1fffff
#define BIT_R_WMAC_TXNDP_SIGB(x)                                               \
	(((x) & BIT_MASK_R_WMAC_TXNDP_SIGB) << BIT_SHIFT_R_WMAC_TXNDP_SIGB)
#define BIT_GET_R_WMAC_TXNDP_SIGB(x)                                           \
	(((x) >> BIT_SHIFT_R_WMAC_TXNDP_SIGB) & BIT_MASK_R_WMAC_TXNDP_SIGB)

/* 2 REG_TXCMD_INFO_FOR_RSP_PKT		(Offset 0x07E4) */

#define BIT_SHIFT_R_MAC_DEBUG (32 & CPU_OPT_WIDTH)
#define BIT_MASK_R_MAC_DEBUG 0xffffffffL
#define BIT_R_MAC_DEBUG(x)                                                     \
	(((x) & BIT_MASK_R_MAC_DEBUG) << BIT_SHIFT_R_MAC_DEBUG)
#define BIT_GET_R_MAC_DEBUG(x)                                                 \
	(((x) >> BIT_SHIFT_R_MAC_DEBUG) & BIT_MASK_R_MAC_DEBUG)

/* 2 REG_TXCMD_INFO_FOR_RSP_PKT		(Offset 0x07E4) */

#define BIT_SHIFT_R_MAC_DBG_SHIFT 8
#define BIT_MASK_R_MAC_DBG_SHIFT 0x7
#define BIT_R_MAC_DBG_SHIFT(x)                                                 \
	(((x) & BIT_MASK_R_MAC_DBG_SHIFT) << BIT_SHIFT_R_MAC_DBG_SHIFT)
#define BIT_GET_R_MAC_DBG_SHIFT(x)                                             \
	(((x) >> BIT_SHIFT_R_MAC_DBG_SHIFT) & BIT_MASK_R_MAC_DBG_SHIFT)

#define BIT_SHIFT_R_MAC_DBG_SEL 0
#define BIT_MASK_R_MAC_DBG_SEL 0x3
#define BIT_R_MAC_DBG_SEL(x)                                                   \
	(((x) & BIT_MASK_R_MAC_DBG_SEL) << BIT_SHIFT_R_MAC_DBG_SEL)
#define BIT_GET_R_MAC_DBG_SEL(x)                                               \
	(((x) >> BIT_SHIFT_R_MAC_DBG_SEL) & BIT_MASK_R_MAC_DBG_SEL)

/* 2 REG_SYS_CFG3				(Offset 0x1000) */

#define BIT_PWC_MA33V BIT(15)

/* 2 REG_SYS_CFG3				(Offset 0x1000) */

#define BIT_PWC_MA12V BIT(14)
#define BIT_PWC_MD12V BIT(13)
#define BIT_PWC_PD12V BIT(12)
#define BIT_PWC_UD12V BIT(11)
#define BIT_ISO_MA2MD BIT(1)

/* 2 REG_SYS_CFG5				(Offset 0x1070) */

#define BIT_LPS_STATUS BIT(3)
#define BIT_HCI_TXDMA_BUSY BIT(2)
#define BIT_HCI_TXDMA_ALLOW BIT(1)
#define BIT_FW_CTRL_HCI_TXDMA_EN BIT(0)

/* 2 REG_CPU_DMEM_CON			(Offset 0x1080) */

#define BIT_WDT_OPT_IOWRAPPER BIT(19)

/* 2 REG_CPU_DMEM_CON			(Offset 0x1080) */

#define BIT_ANA_PORT_IDLE BIT(18)
#define BIT_MAC_PORT_IDLE BIT(17)
#define BIT_WL_PLATFORM_RST BIT(16)
#define BIT_WL_SECURITY_CLK BIT(15)

/* 2 REG_CPU_DMEM_CON			(Offset 0x1080) */

#define BIT_SHIFT_CPU_DMEM_CON 0
#define BIT_MASK_CPU_DMEM_CON 0xff
#define BIT_CPU_DMEM_CON(x)                                                    \
	(((x) & BIT_MASK_CPU_DMEM_CON) << BIT_SHIFT_CPU_DMEM_CON)
#define BIT_GET_CPU_DMEM_CON(x)                                                \
	(((x) >> BIT_SHIFT_CPU_DMEM_CON) & BIT_MASK_CPU_DMEM_CON)

/* 2 REG_BOOT_REASON				(Offset 0x1088) */

#define BIT_SHIFT_BOOT_REASON 0
#define BIT_MASK_BOOT_REASON 0x7
#define BIT_BOOT_REASON(x)                                                     \
	(((x) & BIT_MASK_BOOT_REASON) << BIT_SHIFT_BOOT_REASON)
#define BIT_GET_BOOT_REASON(x)                                                 \
	(((x) >> BIT_SHIFT_BOOT_REASON) & BIT_MASK_BOOT_REASON)

/* 2 REG_NFCPAD_CTRL				(Offset 0x10A8) */

#define BIT_PAD_SHUTDW BIT(18)
#define BIT_SYSON_NFC_PAD BIT(17)
#define BIT_NFC_INT_PAD_CTRL BIT(16)
#define BIT_NFC_RFDIS_PAD_CTRL BIT(15)
#define BIT_NFC_CLK_PAD_CTRL BIT(14)
#define BIT_NFC_DATA_PAD_CTRL BIT(13)
#define BIT_NFC_PAD_PULL_CTRL BIT(12)

#define BIT_SHIFT_NFCPAD_IO_SEL 8
#define BIT_MASK_NFCPAD_IO_SEL 0xf
#define BIT_NFCPAD_IO_SEL(x)                                                   \
	(((x) & BIT_MASK_NFCPAD_IO_SEL) << BIT_SHIFT_NFCPAD_IO_SEL)
#define BIT_GET_NFCPAD_IO_SEL(x)                                               \
	(((x) >> BIT_SHIFT_NFCPAD_IO_SEL) & BIT_MASK_NFCPAD_IO_SEL)

#define BIT_SHIFT_NFCPAD_OUT 4
#define BIT_MASK_NFCPAD_OUT 0xf
#define BIT_NFCPAD_OUT(x) (((x) & BIT_MASK_NFCPAD_OUT) << BIT_SHIFT_NFCPAD_OUT)
#define BIT_GET_NFCPAD_OUT(x)                                                  \
	(((x) >> BIT_SHIFT_NFCPAD_OUT) & BIT_MASK_NFCPAD_OUT)

#define BIT_SHIFT_NFCPAD_IN 0
#define BIT_MASK_NFCPAD_IN 0xf
#define BIT_NFCPAD_IN(x) (((x) & BIT_MASK_NFCPAD_IN) << BIT_SHIFT_NFCPAD_IN)
#define BIT_GET_NFCPAD_IN(x) (((x) >> BIT_SHIFT_NFCPAD_IN) & BIT_MASK_NFCPAD_IN)

/* 2 REG_HIMR2				(Offset 0x10B0) */

#define BIT_BCNDMAINT_P4_MSK BIT(31)
#define BIT_BCNDMAINT_P3_MSK BIT(30)
#define BIT_BCNDMAINT_P2_MSK BIT(29)
#define BIT_BCNDMAINT_P1_MSK BIT(28)
#define BIT_ATIMEND7_MSK BIT(22)
#define BIT_ATIMEND6_MSK BIT(21)
#define BIT_ATIMEND5_MSK BIT(20)
#define BIT_ATIMEND4_MSK BIT(19)
#define BIT_ATIMEND3_MSK BIT(18)
#define BIT_ATIMEND2_MSK BIT(17)
#define BIT_ATIMEND1_MSK BIT(16)
#define BIT_TXBCN7OK_MSK BIT(14)
#define BIT_TXBCN6OK_MSK BIT(13)
#define BIT_TXBCN5OK_MSK BIT(12)
#define BIT_TXBCN4OK_MSK BIT(11)
#define BIT_TXBCN3OK_MSK BIT(10)
#define BIT_TXBCN2OK_MSK BIT(9)
#define BIT_TXBCN1OK_MSK_V1 BIT(8)
#define BIT_TXBCN7ERR_MSK BIT(6)
#define BIT_TXBCN6ERR_MSK BIT(5)
#define BIT_TXBCN5ERR_MSK BIT(4)
#define BIT_TXBCN4ERR_MSK BIT(3)
#define BIT_TXBCN3ERR_MSK BIT(2)
#define BIT_TXBCN2ERR_MSK BIT(1)
#define BIT_TXBCN1ERR_MSK_V1 BIT(0)

/* 2 REG_HISR2				(Offset 0x10B4) */

#define BIT_BCNDMAINT_P4 BIT(31)
#define BIT_BCNDMAINT_P3 BIT(30)
#define BIT_BCNDMAINT_P2 BIT(29)
#define BIT_BCNDMAINT_P1 BIT(28)
#define BIT_ATIMEND7 BIT(22)
#define BIT_ATIMEND6 BIT(21)
#define BIT_ATIMEND5 BIT(20)
#define BIT_ATIMEND4 BIT(19)
#define BIT_ATIMEND3 BIT(18)
#define BIT_ATIMEND2 BIT(17)
#define BIT_ATIMEND1 BIT(16)
#define BIT_TXBCN7OK BIT(14)
#define BIT_TXBCN6OK BIT(13)
#define BIT_TXBCN5OK BIT(12)
#define BIT_TXBCN4OK BIT(11)
#define BIT_TXBCN3OK BIT(10)
#define BIT_TXBCN2OK BIT(9)
#define BIT_TXBCN1OK BIT(8)
#define BIT_TXBCN7ERR BIT(6)
#define BIT_TXBCN6ERR BIT(5)
#define BIT_TXBCN5ERR BIT(4)
#define BIT_TXBCN4ERR BIT(3)
#define BIT_TXBCN3ERR BIT(2)
#define BIT_TXBCN2ERR BIT(1)
#define BIT_TXBCN1ERR BIT(0)

/* 2 REG_HIMR3				(Offset 0x10B8) */

#define BIT_WDT_PLATFORM_INT_MSK BIT(18)
#define BIT_WDT_CPU_INT_MSK BIT(17)

/* 2 REG_HIMR3				(Offset 0x10B8) */

#define BIT_SETH2CDOK_MASK BIT(16)
#define BIT_H2C_CMD_FULL_MASK BIT(15)
#define BIT_PWR_INT_127_MASK BIT(14)
#define BIT_TXSHORTCUT_TXDESUPDATEOK_MASK BIT(13)
#define BIT_TXSHORTCUT_BKUPDATEOK_MASK BIT(12)
#define BIT_TXSHORTCUT_BEUPDATEOK_MASK BIT(11)
#define BIT_TXSHORTCUT_VIUPDATEOK_MAS BIT(10)
#define BIT_TXSHORTCUT_VOUPDATEOK_MASK BIT(9)
#define BIT_PWR_INT_127_MASK_V1 BIT(8)
#define BIT_PWR_INT_126TO96_MASK BIT(7)
#define BIT_PWR_INT_95TO64_MASK BIT(6)
#define BIT_PWR_INT_63TO32_MASK BIT(5)
#define BIT_PWR_INT_31TO0_MASK BIT(4)
#define BIT_DDMA0_LP_INT_MSK BIT(1)
#define BIT_DDMA0_HP_INT_MSK BIT(0)

/* 2 REG_HISR3				(Offset 0x10BC) */

#define BIT_WDT_PLATFORM_INT BIT(18)
#define BIT_WDT_CPU_INT BIT(17)

/* 2 REG_HISR3				(Offset 0x10BC) */

#define BIT_SETH2CDOK BIT(16)
#define BIT_H2C_CMD_FULL BIT(15)
#define BIT_PWR_INT_127 BIT(14)
#define BIT_TXSHORTCUT_TXDESUPDATEOK BIT(13)
#define BIT_TXSHORTCUT_BKUPDATEOK BIT(12)
#define BIT_TXSHORTCUT_BEUPDATEOK BIT(11)
#define BIT_TXSHORTCUT_VIUPDATEOK BIT(10)
#define BIT_TXSHORTCUT_VOUPDATEOK BIT(9)
#define BIT_PWR_INT_127_V1 BIT(8)
#define BIT_PWR_INT_126TO96 BIT(7)
#define BIT_PWR_INT_95TO64 BIT(6)
#define BIT_PWR_INT_63TO32 BIT(5)
#define BIT_PWR_INT_31TO0 BIT(4)
#define BIT_DDMA0_LP_INT BIT(1)
#define BIT_DDMA0_HP_INT BIT(0)

/* 2 REG_SW_MDIO				(Offset 0x10C0) */

#define BIT_DIS_TIMEOUT_IO BIT(24)

/* 2 REG_SW_FLUSH				(Offset 0x10C4) */

#define BIT_FLUSH_HOLDN_EN BIT(25)
#define BIT_FLUSH_WR_EN BIT(24)
#define BIT_SW_FLASH_CONTROL BIT(23)
#define BIT_SW_FLASH_WEN_E BIT(19)
#define BIT_SW_FLASH_HOLDN_E BIT(18)
#define BIT_SW_FLASH_SO_E BIT(17)
#define BIT_SW_FLASH_SI_E BIT(16)
#define BIT_SW_FLASH_SK_O BIT(13)
#define BIT_SW_FLASH_CEN_O BIT(12)
#define BIT_SW_FLASH_WEN_O BIT(11)
#define BIT_SW_FLASH_HOLDN_O BIT(10)
#define BIT_SW_FLASH_SO_O BIT(9)
#define BIT_SW_FLASH_SI_O BIT(8)
#define BIT_SW_FLASH_WEN_I BIT(3)
#define BIT_SW_FLASH_HOLDN_I BIT(2)
#define BIT_SW_FLASH_SO_I BIT(1)
#define BIT_SW_FLASH_SI_I BIT(0)

/* 2 REG_H2C_PKT_READADDR			(Offset 0x10D0) */

#define BIT_SHIFT_H2C_PKT_READADDR 0
#define BIT_MASK_H2C_PKT_READADDR 0x3ffff
#define BIT_H2C_PKT_READADDR(x)                                                \
	(((x) & BIT_MASK_H2C_PKT_READADDR) << BIT_SHIFT_H2C_PKT_READADDR)
#define BIT_GET_H2C_PKT_READADDR(x)                                            \
	(((x) >> BIT_SHIFT_H2C_PKT_READADDR) & BIT_MASK_H2C_PKT_READADDR)

/* 2 REG_H2C_PKT_WRITEADDR			(Offset 0x10D4) */

#define BIT_SHIFT_H2C_PKT_WRITEADDR 0
#define BIT_MASK_H2C_PKT_WRITEADDR 0x3ffff
#define BIT_H2C_PKT_WRITEADDR(x)                                               \
	(((x) & BIT_MASK_H2C_PKT_WRITEADDR) << BIT_SHIFT_H2C_PKT_WRITEADDR)
#define BIT_GET_H2C_PKT_WRITEADDR(x)                                           \
	(((x) >> BIT_SHIFT_H2C_PKT_WRITEADDR) & BIT_MASK_H2C_PKT_WRITEADDR)

/* 2 REG_MEM_PWR_CRTL			(Offset 0x10D8) */

#define BIT_MEM_BB_SD BIT(17)
#define BIT_MEM_BB_DS BIT(16)
#define BIT_MEM_BT_DS BIT(10)
#define BIT_MEM_SDIO_LS BIT(9)
#define BIT_MEM_SDIO_DS BIT(8)
#define BIT_MEM_USB_LS BIT(7)
#define BIT_MEM_USB_DS BIT(6)
#define BIT_MEM_PCI_LS BIT(5)
#define BIT_MEM_PCI_DS BIT(4)
#define BIT_MEM_WLMAC_LS BIT(3)
#define BIT_MEM_WLMAC_DS BIT(2)
#define BIT_MEM_WLMCU_LS BIT(1)

/* 2 REG_MEM_PWR_CRTL			(Offset 0x10D8) */

#define BIT_MEM_WLMCU_DS BIT(0)

/* 2 REG_FW_DBG0				(Offset 0x10E0) */

#define BIT_SHIFT_FW_DBG0 0
#define BIT_MASK_FW_DBG0 0xffffffffL
#define BIT_FW_DBG0(x) (((x) & BIT_MASK_FW_DBG0) << BIT_SHIFT_FW_DBG0)
#define BIT_GET_FW_DBG0(x) (((x) >> BIT_SHIFT_FW_DBG0) & BIT_MASK_FW_DBG0)

/* 2 REG_FW_DBG1				(Offset 0x10E4) */

#define BIT_SHIFT_FW_DBG1 0
#define BIT_MASK_FW_DBG1 0xffffffffL
#define BIT_FW_DBG1(x) (((x) & BIT_MASK_FW_DBG1) << BIT_SHIFT_FW_DBG1)
#define BIT_GET_FW_DBG1(x) (((x) >> BIT_SHIFT_FW_DBG1) & BIT_MASK_FW_DBG1)

/* 2 REG_FW_DBG2				(Offset 0x10E8) */

#define BIT_SHIFT_FW_DBG2 0
#define BIT_MASK_FW_DBG2 0xffffffffL
#define BIT_FW_DBG2(x) (((x) & BIT_MASK_FW_DBG2) << BIT_SHIFT_FW_DBG2)
#define BIT_GET_FW_DBG2(x) (((x) >> BIT_SHIFT_FW_DBG2) & BIT_MASK_FW_DBG2)

/* 2 REG_FW_DBG3				(Offset 0x10EC) */

#define BIT_SHIFT_FW_DBG3 0
#define BIT_MASK_FW_DBG3 0xffffffffL
#define BIT_FW_DBG3(x) (((x) & BIT_MASK_FW_DBG3) << BIT_SHIFT_FW_DBG3)
#define BIT_GET_FW_DBG3(x) (((x) >> BIT_SHIFT_FW_DBG3) & BIT_MASK_FW_DBG3)

/* 2 REG_FW_DBG4				(Offset 0x10F0) */

#define BIT_SHIFT_FW_DBG4 0
#define BIT_MASK_FW_DBG4 0xffffffffL
#define BIT_FW_DBG4(x) (((x) & BIT_MASK_FW_DBG4) << BIT_SHIFT_FW_DBG4)
#define BIT_GET_FW_DBG4(x) (((x) >> BIT_SHIFT_FW_DBG4) & BIT_MASK_FW_DBG4)

/* 2 REG_FW_DBG5				(Offset 0x10F4) */

#define BIT_SHIFT_FW_DBG5 0
#define BIT_MASK_FW_DBG5 0xffffffffL
#define BIT_FW_DBG5(x) (((x) & BIT_MASK_FW_DBG5) << BIT_SHIFT_FW_DBG5)
#define BIT_GET_FW_DBG5(x) (((x) >> BIT_SHIFT_FW_DBG5) & BIT_MASK_FW_DBG5)

/* 2 REG_FW_DBG6				(Offset 0x10F8) */

#define BIT_SHIFT_FW_DBG6 0
#define BIT_MASK_FW_DBG6 0xffffffffL
#define BIT_FW_DBG6(x) (((x) & BIT_MASK_FW_DBG6) << BIT_SHIFT_FW_DBG6)
#define BIT_GET_FW_DBG6(x) (((x) >> BIT_SHIFT_FW_DBG6) & BIT_MASK_FW_DBG6)

/* 2 REG_FW_DBG7				(Offset 0x10FC) */

#define BIT_SHIFT_FW_DBG7 0
#define BIT_MASK_FW_DBG7 0xffffffffL
#define BIT_FW_DBG7(x) (((x) & BIT_MASK_FW_DBG7) << BIT_SHIFT_FW_DBG7)
#define BIT_GET_FW_DBG7(x) (((x) >> BIT_SHIFT_FW_DBG7) & BIT_MASK_FW_DBG7)

/* 2 REG_CR_EXT				(Offset 0x1100) */

#define BIT_SHIFT_PHY_REQ_DELAY 24
#define BIT_MASK_PHY_REQ_DELAY 0xf
#define BIT_PHY_REQ_DELAY(x)                                                   \
	(((x) & BIT_MASK_PHY_REQ_DELAY) << BIT_SHIFT_PHY_REQ_DELAY)
#define BIT_GET_PHY_REQ_DELAY(x)                                               \
	(((x) >> BIT_SHIFT_PHY_REQ_DELAY) & BIT_MASK_PHY_REQ_DELAY)

#define BIT_SPD_DOWN BIT(16)

#define BIT_SHIFT_NETYPE4 4
#define BIT_MASK_NETYPE4 0x3
#define BIT_NETYPE4(x) (((x) & BIT_MASK_NETYPE4) << BIT_SHIFT_NETYPE4)
#define BIT_GET_NETYPE4(x) (((x) >> BIT_SHIFT_NETYPE4) & BIT_MASK_NETYPE4)

#define BIT_SHIFT_NETYPE3 2
#define BIT_MASK_NETYPE3 0x3
#define BIT_NETYPE3(x) (((x) & BIT_MASK_NETYPE3) << BIT_SHIFT_NETYPE3)
#define BIT_GET_NETYPE3(x) (((x) >> BIT_SHIFT_NETYPE3) & BIT_MASK_NETYPE3)

#define BIT_SHIFT_NETYPE2 0
#define BIT_MASK_NETYPE2 0x3
#define BIT_NETYPE2(x) (((x) & BIT_MASK_NETYPE2) << BIT_SHIFT_NETYPE2)
#define BIT_GET_NETYPE2(x) (((x) >> BIT_SHIFT_NETYPE2) & BIT_MASK_NETYPE2)

/* 2 REG_FWFF				(Offset 0x1114) */

#define BIT_SHIFT_PKTNUM_TH_V1 24
#define BIT_MASK_PKTNUM_TH_V1 0xff
#define BIT_PKTNUM_TH_V1(x)                                                    \
	(((x) & BIT_MASK_PKTNUM_TH_V1) << BIT_SHIFT_PKTNUM_TH_V1)
#define BIT_GET_PKTNUM_TH_V1(x)                                                \
	(((x) >> BIT_SHIFT_PKTNUM_TH_V1) & BIT_MASK_PKTNUM_TH_V1)

/* 2 REG_FWFF				(Offset 0x1114) */

#define BIT_SHIFT_TIMER_TH 16
#define BIT_MASK_TIMER_TH 0xff
#define BIT_TIMER_TH(x) (((x) & BIT_MASK_TIMER_TH) << BIT_SHIFT_TIMER_TH)
#define BIT_GET_TIMER_TH(x) (((x) >> BIT_SHIFT_TIMER_TH) & BIT_MASK_TIMER_TH)

/* 2 REG_FWFF				(Offset 0x1114) */

#define BIT_SHIFT_RXPKT1ENADDR 0
#define BIT_MASK_RXPKT1ENADDR 0xffff
#define BIT_RXPKT1ENADDR(x)                                                    \
	(((x) & BIT_MASK_RXPKT1ENADDR) << BIT_SHIFT_RXPKT1ENADDR)
#define BIT_GET_RXPKT1ENADDR(x)                                                \
	(((x) >> BIT_SHIFT_RXPKT1ENADDR) & BIT_MASK_RXPKT1ENADDR)

/* 2 REG_FE2IMR				(Offset 0x1120) */

#define BIT__FE4ISR__IND_MSK BIT(29)

/* 2 REG_FE2IMR				(Offset 0x1120) */

#define BIT_FS_TXSC_DESC_DONE_INT_EN BIT(28)
#define BIT_FS_TXSC_BKDONE_INT_EN BIT(27)
#define BIT_FS_TXSC_BEDONE_INT_EN BIT(26)
#define BIT_FS_TXSC_VIDONE_INT_EN BIT(25)
#define BIT_FS_TXSC_VODONE_INT_EN BIT(24)

/* 2 REG_FE2IMR				(Offset 0x1120) */

#define BIT_FS_ATIM_MB7_INT_EN BIT(23)
#define BIT_FS_ATIM_MB6_INT_EN BIT(22)
#define BIT_FS_ATIM_MB5_INT_EN BIT(21)
#define BIT_FS_ATIM_MB4_INT_EN BIT(20)
#define BIT_FS_ATIM_MB3_INT_EN BIT(19)
#define BIT_FS_ATIM_MB2_INT_EN BIT(18)
#define BIT_FS_ATIM_MB1_INT_EN BIT(17)
#define BIT_FS_ATIM_MB0_INT_EN BIT(16)
#define BIT_FS_TBTT4INT_EN BIT(11)
#define BIT_FS_TBTT3INT_EN BIT(10)
#define BIT_FS_TBTT2INT_EN BIT(9)
#define BIT_FS_TBTT1INT_EN BIT(8)
#define BIT_FS_TBTT0_MB7INT_EN BIT(7)
#define BIT_FS_TBTT0_MB6INT_EN BIT(6)
#define BIT_FS_TBTT0_MB5INT_EN BIT(5)
#define BIT_FS_TBTT0_MB4INT_EN BIT(4)
#define BIT_FS_TBTT0_MB3INT_EN BIT(3)
#define BIT_FS_TBTT0_MB2INT_EN BIT(2)
#define BIT_FS_TBTT0_MB1INT_EN BIT(1)
#define BIT_FS_TBTT0_INT_EN BIT(0)

/* 2 REG_FE2ISR				(Offset 0x1124) */

#define BIT__FE4ISR__IND_INT BIT(29)

/* 2 REG_FE2ISR				(Offset 0x1124) */

#define BIT_FS_TXSC_DESC_DONE_INT BIT(28)
#define BIT_FS_TXSC_BKDONE_INT BIT(27)
#define BIT_FS_TXSC_BEDONE_INT BIT(26)
#define BIT_FS_TXSC_VIDONE_INT BIT(25)
#define BIT_FS_TXSC_VODONE_INT BIT(24)

/* 2 REG_FE2ISR				(Offset 0x1124) */

#define BIT_FS_ATIM_MB7_INT BIT(23)
#define BIT_FS_ATIM_MB6_INT BIT(22)
#define BIT_FS_ATIM_MB5_INT BIT(21)
#define BIT_FS_ATIM_MB4_INT BIT(20)
#define BIT_FS_ATIM_MB3_INT BIT(19)
#define BIT_FS_ATIM_MB2_INT BIT(18)
#define BIT_FS_ATIM_MB1_INT BIT(17)
#define BIT_FS_ATIM_MB0_INT BIT(16)
#define BIT_FS_TBTT4INT BIT(11)
#define BIT_FS_TBTT3INT BIT(10)
#define BIT_FS_TBTT2INT BIT(9)
#define BIT_FS_TBTT1INT BIT(8)
#define BIT_FS_TBTT0_MB7INT BIT(7)
#define BIT_FS_TBTT0_MB6INT BIT(6)
#define BIT_FS_TBTT0_MB5INT BIT(5)
#define BIT_FS_TBTT0_MB4INT BIT(4)
#define BIT_FS_TBTT0_MB3INT BIT(3)
#define BIT_FS_TBTT0_MB2INT BIT(2)
#define BIT_FS_TBTT0_MB1INT BIT(1)
#define BIT_FS_TBTT0_INT BIT(0)

/* 2 REG_FE3IMR				(Offset 0x1128) */

#define BIT_FS_CLI3_MTI_BCNIVLEAR_INT__EN BIT(31)

/* 2 REG_FE3IMR				(Offset 0x1128) */

#define BIT_FS_CLI2_MTI_BCNIVLEAR_INT__EN BIT(30)

/* 2 REG_FE3IMR				(Offset 0x1128) */

#define BIT_FS_CLI1_MTI_BCNIVLEAR_INT__EN BIT(29)

/* 2 REG_FE3IMR				(Offset 0x1128) */

#define BIT_FS_CLI0_MTI_BCNIVLEAR_INT__EN BIT(28)

/* 2 REG_FE3IMR				(Offset 0x1128) */

#define BIT_FS_BCNDMA4_INT_EN BIT(27)
#define BIT_FS_BCNDMA3_INT_EN BIT(26)
#define BIT_FS_BCNDMA2_INT_EN BIT(25)
#define BIT_FS_BCNDMA1_INT_EN BIT(24)
#define BIT_FS_BCNDMA0_MB7_INT_EN BIT(23)
#define BIT_FS_BCNDMA0_MB6_INT_EN BIT(22)
#define BIT_FS_BCNDMA0_MB5_INT_EN BIT(21)
#define BIT_FS_BCNDMA0_MB4_INT_EN BIT(20)
#define BIT_FS_BCNDMA0_MB3_INT_EN BIT(19)
#define BIT_FS_BCNDMA0_MB2_INT_EN BIT(18)
#define BIT_FS_BCNDMA0_MB1_INT_EN BIT(17)
#define BIT_FS_BCNDMA0_INT_EN BIT(16)
#define BIT_FS_MTI_BCNIVLEAR_INT__EN BIT(15)
#define BIT_FS_BCNERLY4_INT_EN BIT(11)
#define BIT_FS_BCNERLY3_INT_EN BIT(10)
#define BIT_FS_BCNERLY2_INT_EN BIT(9)
#define BIT_FS_BCNERLY1_INT_EN BIT(8)
#define BIT_FS_BCNERLY0_MB7INT_EN BIT(7)
#define BIT_FS_BCNERLY0_MB6INT_EN BIT(6)
#define BIT_FS_BCNERLY0_MB5INT_EN BIT(5)
#define BIT_FS_BCNERLY0_MB4INT_EN BIT(4)
#define BIT_FS_BCNERLY0_MB3INT_EN BIT(3)
#define BIT_FS_BCNERLY0_MB2INT_EN BIT(2)
#define BIT_FS_BCNERLY0_MB1INT_EN BIT(1)
#define BIT_FS_BCNERLY0_INT_EN BIT(0)

/* 2 REG_FE3ISR				(Offset 0x112C) */

#define BIT_FS_CLI3_MTI_BCNIVLEAR_INT BIT(31)

/* 2 REG_FE3ISR				(Offset 0x112C) */

#define BIT_FS_CLI2_MTI_BCNIVLEAR_INT BIT(30)

/* 2 REG_FE3ISR				(Offset 0x112C) */

#define BIT_FS_CLI1_MTI_BCNIVLEAR_INT BIT(29)

/* 2 REG_FE3ISR				(Offset 0x112C) */

#define BIT_FS_CLI0_MTI_BCNIVLEAR_INT BIT(28)

/* 2 REG_FE3ISR				(Offset 0x112C) */

#define BIT_FS_BCNDMA4_INT BIT(27)
#define BIT_FS_BCNDMA3_INT BIT(26)
#define BIT_FS_BCNDMA2_INT BIT(25)
#define BIT_FS_BCNDMA1_INT BIT(24)
#define BIT_FS_BCNDMA0_MB7_INT BIT(23)
#define BIT_FS_BCNDMA0_MB6_INT BIT(22)
#define BIT_FS_BCNDMA0_MB5_INT BIT(21)
#define BIT_FS_BCNDMA0_MB4_INT BIT(20)
#define BIT_FS_BCNDMA0_MB3_INT BIT(19)
#define BIT_FS_BCNDMA0_MB2_INT BIT(18)
#define BIT_FS_BCNDMA0_MB1_INT BIT(17)
#define BIT_FS_BCNDMA0_INT BIT(16)
#define BIT_FS_MTI_BCNIVLEAR_INT BIT(15)
#define BIT_FS_BCNERLY4_INT BIT(11)
#define BIT_FS_BCNERLY3_INT BIT(10)
#define BIT_FS_BCNERLY2_INT BIT(9)
#define BIT_FS_BCNERLY1_INT BIT(8)
#define BIT_FS_BCNERLY0_MB7INT BIT(7)
#define BIT_FS_BCNERLY0_MB6INT BIT(6)
#define BIT_FS_BCNERLY0_MB5INT BIT(5)
#define BIT_FS_BCNERLY0_MB4INT BIT(4)
#define BIT_FS_BCNERLY0_MB3INT BIT(3)
#define BIT_FS_BCNERLY0_MB2INT BIT(2)
#define BIT_FS_BCNERLY0_MB1INT BIT(1)
#define BIT_FS_BCNERLY0_INT BIT(0)

/* 2 REG_FE4IMR				(Offset 0x1130) */

#define BIT_FS_CLI3_TXPKTIN_INT_EN BIT(19)

/* 2 REG_FE4IMR				(Offset 0x1130) */

#define BIT_FS_CLI2_TXPKTIN_INT_EN BIT(18)

/* 2 REG_FE4IMR				(Offset 0x1130) */

#define BIT_FS_CLI1_TXPKTIN_INT_EN BIT(17)

/* 2 REG_FE4IMR				(Offset 0x1130) */

#define BIT_FS_CLI0_TXPKTIN_INT_EN BIT(16)

/* 2 REG_FE4IMR				(Offset 0x1130) */

#define BIT_FS_CLI3_RX_UMD0_INT_EN BIT(15)

/* 2 REG_FE4IMR				(Offset 0x1130) */

#define BIT_FS_CLI3_RX_UMD1_INT_EN BIT(14)

/* 2 REG_FE4IMR				(Offset 0x1130) */

#define BIT_FS_CLI3_RX_BMD0_INT_EN BIT(13)

/* 2 REG_FE4IMR				(Offset 0x1130) */

#define BIT_FS_CLI3_RX_BMD1_INT_EN BIT(12)

/* 2 REG_FE4IMR				(Offset 0x1130) */

#define BIT_FS_CLI2_RX_UMD0_INT_EN BIT(11)

/* 2 REG_FE4IMR				(Offset 0x1130) */

#define BIT_FS_CLI2_RX_UMD1_INT_EN BIT(10)

/* 2 REG_FE4IMR				(Offset 0x1130) */

#define BIT_FS_CLI2_RX_BMD0_INT_EN BIT(9)

/* 2 REG_FE4IMR				(Offset 0x1130) */

#define BIT_FS_CLI2_RX_BMD1_INT_EN BIT(8)

/* 2 REG_FE4IMR				(Offset 0x1130) */

#define BIT_FS_CLI1_RX_UMD0_INT_EN BIT(7)

/* 2 REG_FE4IMR				(Offset 0x1130) */

#define BIT_FS_CLI1_RX_UMD1_INT_EN BIT(6)

/* 2 REG_FE4IMR				(Offset 0x1130) */

#define BIT_FS_CLI1_RX_BMD0_INT_EN BIT(5)

/* 2 REG_FE4IMR				(Offset 0x1130) */

#define BIT_FS_CLI1_RX_BMD1_INT_EN BIT(4)

/* 2 REG_FE4IMR				(Offset 0x1130) */

#define BIT_FS_CLI0_RX_UMD0_INT_EN BIT(3)

/* 2 REG_FE4IMR				(Offset 0x1130) */

#define BIT_FS_CLI0_RX_UMD1_INT_EN BIT(2)

/* 2 REG_FE4IMR				(Offset 0x1130) */

#define BIT_FS_CLI0_RX_BMD0_INT_EN BIT(1)

/* 2 REG_FE4IMR				(Offset 0x1130) */

#define BIT_FS_CLI0_RX_BMD1_INT_EN BIT(0)

/* 2 REG_FE4ISR				(Offset 0x1134) */

#define BIT_FS_CLI3_TXPKTIN_INT BIT(19)

/* 2 REG_FE4ISR				(Offset 0x1134) */

#define BIT_FS_CLI2_TXPKTIN_INT BIT(18)

/* 2 REG_FE4ISR				(Offset 0x1134) */

#define BIT_FS_CLI1_TXPKTIN_INT BIT(17)

/* 2 REG_FE4ISR				(Offset 0x1134) */

#define BIT_FS_CLI0_TXPKTIN_INT BIT(16)

/* 2 REG_FE4ISR				(Offset 0x1134) */

#define BIT_FS_CLI3_RX_UMD0_INT BIT(15)

/* 2 REG_FE4ISR				(Offset 0x1134) */

#define BIT_FS_CLI3_RX_UMD1_INT BIT(14)

/* 2 REG_FE4ISR				(Offset 0x1134) */

#define BIT_FS_CLI3_RX_BMD0_INT BIT(13)

/* 2 REG_FE4ISR				(Offset 0x1134) */

#define BIT_FS_CLI3_RX_BMD1_INT BIT(12)

/* 2 REG_FE4ISR				(Offset 0x1134) */

#define BIT_FS_CLI2_RX_UMD0_INT BIT(11)

/* 2 REG_FE4ISR				(Offset 0x1134) */

#define BIT_FS_CLI2_RX_UMD1_INT BIT(10)

/* 2 REG_FE4ISR				(Offset 0x1134) */

#define BIT_FS_CLI2_RX_BMD0_INT BIT(9)

/* 2 REG_FE4ISR				(Offset 0x1134) */

#define BIT_FS_CLI2_RX_BMD1_INT BIT(8)

/* 2 REG_FE4ISR				(Offset 0x1134) */

#define BIT_FS_CLI1_RX_UMD0_INT BIT(7)

/* 2 REG_FE4ISR				(Offset 0x1134) */

#define BIT_FS_CLI1_RX_UMD1_INT BIT(6)

/* 2 REG_FE4ISR				(Offset 0x1134) */

#define BIT_FS_CLI1_RX_BMD0_INT BIT(5)

/* 2 REG_FE4ISR				(Offset 0x1134) */

#define BIT_FS_CLI1_RX_BMD1_INT BIT(4)

/* 2 REG_FE4ISR				(Offset 0x1134) */

#define BIT_FS_CLI0_RX_UMD0_INT BIT(3)

/* 2 REG_FE4ISR				(Offset 0x1134) */

#define BIT_FS_CLI0_RX_UMD1_INT BIT(2)

/* 2 REG_FE4ISR				(Offset 0x1134) */

#define BIT_FS_CLI0_RX_BMD0_INT BIT(1)

/* 2 REG_FE4ISR				(Offset 0x1134) */

#define BIT_FS_CLI0_RX_BMD1_INT BIT(0)

/* 2 REG_FT1IMR				(Offset 0x1138) */

#define BIT__FT2ISR__IND_MSK BIT(30)
#define BIT_FTM_PTT_INT_EN BIT(29)
#define BIT_RXFTMREQ_INT_EN BIT(28)
#define BIT_RXFTM_INT_EN BIT(27)
#define BIT_TXFTM_INT_EN BIT(26)

/* 2 REG_FT1IMR				(Offset 0x1138) */

#define BIT_FS_H2C_CMD_OK_INT_EN BIT(25)
#define BIT_FS_H2C_CMD_FULL_INT_EN BIT(24)

/* 2 REG_FT1IMR				(Offset 0x1138) */

#define BIT_FS_MACID_PWRCHANGE5_INT_EN BIT(23)
#define BIT_FS_MACID_PWRCHANGE4_INT_EN BIT(22)
#define BIT_FS_MACID_PWRCHANGE3_INT_EN BIT(21)
#define BIT_FS_MACID_PWRCHANGE2_INT_EN BIT(20)
#define BIT_FS_MACID_PWRCHANGE1_INT_EN BIT(19)
#define BIT_FS_MACID_PWRCHANGE0_INT_EN BIT(18)
#define BIT_FS_CTWEND2_INT_EN BIT(17)
#define BIT_FS_CTWEND1_INT_EN BIT(16)
#define BIT_FS_CTWEND0_INT_EN BIT(15)
#define BIT_FS_TX_NULL1_INT_EN BIT(14)
#define BIT_FS_TX_NULL0_INT_EN BIT(13)
#define BIT_FS_TSF_BIT32_TOGGLE_EN BIT(12)
#define BIT_FS_P2P_RFON2_INT_EN BIT(11)
#define BIT_FS_P2P_RFOFF2_INT_EN BIT(10)
#define BIT_FS_P2P_RFON1_INT_EN BIT(9)
#define BIT_FS_P2P_RFOFF1_INT_EN BIT(8)
#define BIT_FS_P2P_RFON0_INT_EN BIT(7)
#define BIT_FS_P2P_RFOFF0_INT_EN BIT(6)
#define BIT_FS_RX_UAPSDMD1_EN BIT(5)
#define BIT_FS_RX_UAPSDMD0_EN BIT(4)
#define BIT_FS_TRIGGER_PKT_EN BIT(3)
#define BIT_FS_EOSP_INT_EN BIT(2)
#define BIT_FS_RPWM2_INT_EN BIT(1)
#define BIT_FS_RPWM_INT_EN BIT(0)

/* 2 REG_FT1ISR				(Offset 0x113C) */

#define BIT__FT2ISR__IND_INT BIT(30)
#define BIT_FTM_PTT_INT BIT(29)
#define BIT_RXFTMREQ_INT BIT(28)
#define BIT_RXFTM_INT BIT(27)
#define BIT_TXFTM_INT BIT(26)

/* 2 REG_FT1ISR				(Offset 0x113C) */

#define BIT_FS_H2C_CMD_OK_INT BIT(25)
#define BIT_FS_H2C_CMD_FULL_INT BIT(24)

/* 2 REG_FT1ISR				(Offset 0x113C) */

#define BIT_FS_MACID_PWRCHANGE5_INT BIT(23)
#define BIT_FS_MACID_PWRCHANGE4_INT BIT(22)
#define BIT_FS_MACID_PWRCHANGE3_INT BIT(21)
#define BIT_FS_MACID_PWRCHANGE2_INT BIT(20)
#define BIT_FS_MACID_PWRCHANGE1_INT BIT(19)
#define BIT_FS_MACID_PWRCHANGE0_INT BIT(18)
#define BIT_FS_CTWEND2_INT BIT(17)
#define BIT_FS_CTWEND1_INT BIT(16)
#define BIT_FS_CTWEND0_INT BIT(15)
#define BIT_FS_TX_NULL1_INT BIT(14)
#define BIT_FS_TX_NULL0_INT BIT(13)
#define BIT_FS_TSF_BIT32_TOGGLE_INT BIT(12)
#define BIT_FS_P2P_RFON2_INT BIT(11)
#define BIT_FS_P2P_RFOFF2_INT BIT(10)
#define BIT_FS_P2P_RFON1_INT BIT(9)
#define BIT_FS_P2P_RFOFF1_INT BIT(8)
#define BIT_FS_P2P_RFON0_INT BIT(7)
#define BIT_FS_P2P_RFOFF0_INT BIT(6)
#define BIT_FS_RX_UAPSDMD1_INT BIT(5)
#define BIT_FS_RX_UAPSDMD0_INT BIT(4)
#define BIT_FS_TRIGGER_PKT_INT BIT(3)
#define BIT_FS_EOSP_INT BIT(2)
#define BIT_FS_RPWM2_INT BIT(1)
#define BIT_FS_RPWM_INT BIT(0)

/* 2 REG_SPWR0				(Offset 0x1140) */

#define BIT_SHIFT_MID_31TO0 0
#define BIT_MASK_MID_31TO0 0xffffffffL
#define BIT_MID_31TO0(x) (((x) & BIT_MASK_MID_31TO0) << BIT_SHIFT_MID_31TO0)
#define BIT_GET_MID_31TO0(x) (((x) >> BIT_SHIFT_MID_31TO0) & BIT_MASK_MID_31TO0)

/* 2 REG_SPWR1				(Offset 0x1144) */

#define BIT_SHIFT_MID_63TO32 0
#define BIT_MASK_MID_63TO32 0xffffffffL
#define BIT_MID_63TO32(x) (((x) & BIT_MASK_MID_63TO32) << BIT_SHIFT_MID_63TO32)
#define BIT_GET_MID_63TO32(x)                                                  \
	(((x) >> BIT_SHIFT_MID_63TO32) & BIT_MASK_MID_63TO32)

/* 2 REG_SPWR2				(Offset 0x1148) */

#define BIT_SHIFT_MID_95O64 0
#define BIT_MASK_MID_95O64 0xffffffffL
#define BIT_MID_95O64(x) (((x) & BIT_MASK_MID_95O64) << BIT_SHIFT_MID_95O64)
#define BIT_GET_MID_95O64(x) (((x) >> BIT_SHIFT_MID_95O64) & BIT_MASK_MID_95O64)

/* 2 REG_SPWR3				(Offset 0x114C) */

#define BIT_SHIFT_MID_127TO96 0
#define BIT_MASK_MID_127TO96 0xffffffffL
#define BIT_MID_127TO96(x)                                                     \
	(((x) & BIT_MASK_MID_127TO96) << BIT_SHIFT_MID_127TO96)
#define BIT_GET_MID_127TO96(x)                                                 \
	(((x) >> BIT_SHIFT_MID_127TO96) & BIT_MASK_MID_127TO96)

/* 2 REG_POWSEQ				(Offset 0x1150) */

#define BIT_SHIFT_SEQNUM_MID 16
#define BIT_MASK_SEQNUM_MID 0xffff
#define BIT_SEQNUM_MID(x) (((x) & BIT_MASK_SEQNUM_MID) << BIT_SHIFT_SEQNUM_MID)
#define BIT_GET_SEQNUM_MID(x)                                                  \
	(((x) >> BIT_SHIFT_SEQNUM_MID) & BIT_MASK_SEQNUM_MID)

#define BIT_SHIFT_REF_MID 0
#define BIT_MASK_REF_MID 0x7f
#define BIT_REF_MID(x) (((x) & BIT_MASK_REF_MID) << BIT_SHIFT_REF_MID)
#define BIT_GET_REF_MID(x) (((x) >> BIT_SHIFT_REF_MID) & BIT_MASK_REF_MID)

/* 2 REG_TC7_CTRL_V1				(Offset 0x1158) */

#define BIT_TC7INT_EN BIT(26)
#define BIT_TC7MODE BIT(25)
#define BIT_TC7EN BIT(24)

#define BIT_SHIFT_TC7DATA 0
#define BIT_MASK_TC7DATA 0xffffff
#define BIT_TC7DATA(x) (((x) & BIT_MASK_TC7DATA) << BIT_SHIFT_TC7DATA)
#define BIT_GET_TC7DATA(x) (((x) >> BIT_SHIFT_TC7DATA) & BIT_MASK_TC7DATA)

/* 2 REG_TC8_CTRL_V1				(Offset 0x115C) */

#define BIT_TC8INT_EN BIT(26)
#define BIT_TC8MODE BIT(25)
#define BIT_TC8EN BIT(24)

#define BIT_SHIFT_TC8DATA 0
#define BIT_MASK_TC8DATA 0xffffff
#define BIT_TC8DATA(x) (((x) & BIT_MASK_TC8DATA) << BIT_SHIFT_TC8DATA)
#define BIT_GET_TC8DATA(x) (((x) >> BIT_SHIFT_TC8DATA) & BIT_MASK_TC8DATA)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI3_RX_UAPSDMD1_EN BIT(31)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI3_RX_UAPSDMD0_EN BIT(30)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI3_TRIGGER_PKT_EN BIT(29)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI3_EOSP_INT_EN BIT(28)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI2_RX_UAPSDMD1_EN BIT(27)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI2_RX_UAPSDMD0_EN BIT(26)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI2_TRIGGER_PKT_EN BIT(25)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI2_EOSP_INT_EN BIT(24)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI1_RX_UAPSDMD1_EN BIT(23)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI1_RX_UAPSDMD0_EN BIT(22)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI1_TRIGGER_PKT_EN BIT(21)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI1_EOSP_INT_EN BIT(20)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI0_RX_UAPSDMD1_EN BIT(19)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI0_RX_UAPSDMD0_EN BIT(18)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI0_TRIGGER_PKT_EN BIT(17)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI0_EOSP_INT_EN BIT(16)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_TSF_BIT32_TOGGLE_P2P2_EN BIT(9)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_TSF_BIT32_TOGGLE_P2P1_EN BIT(8)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI3_TX_NULL1_INT_EN BIT(7)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI3_TX_NULL0_INT_EN BIT(6)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI2_TX_NULL1_INT_EN BIT(5)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI2_TX_NULL0_INT_EN BIT(4)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI1_TX_NULL1_INT_EN BIT(3)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI1_TX_NULL0_INT_EN BIT(2)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI0_TX_NULL1_INT_EN BIT(1)

/* 2 REG_FT2IMR				(Offset 0x11E0) */

#define BIT_FS_CLI0_TX_NULL0_INT_EN BIT(0)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI3_RX_UAPSDMD1_INT BIT(31)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI3_RX_UAPSDMD0_INT BIT(30)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI3_TRIGGER_PKT_INT BIT(29)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI3_EOSP_INT BIT(28)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI2_RX_UAPSDMD1_INT BIT(27)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI2_RX_UAPSDMD0_INT BIT(26)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI2_TRIGGER_PKT_INT BIT(25)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI2_EOSP_INT BIT(24)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI1_RX_UAPSDMD1_INT BIT(23)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI1_RX_UAPSDMD0_INT BIT(22)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI1_TRIGGER_PKT_INT BIT(21)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI1_EOSP_INT BIT(20)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI0_RX_UAPSDMD1_INT BIT(19)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI0_RX_UAPSDMD0_INT BIT(18)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI0_TRIGGER_PKT_INT BIT(17)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI0_EOSP_INT BIT(16)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_TSF_BIT32_TOGGLE_P2P2_INT BIT(9)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_TSF_BIT32_TOGGLE_P2P1_INT BIT(8)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI3_TX_NULL1_INT BIT(7)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI3_TX_NULL0_INT BIT(6)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI2_TX_NULL1_INT BIT(5)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI2_TX_NULL0_INT BIT(4)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI1_TX_NULL1_INT BIT(3)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI1_TX_NULL0_INT BIT(2)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI0_TX_NULL1_INT BIT(1)

/* 2 REG_FT2ISR				(Offset 0x11E4) */

#define BIT_FS_CLI0_TX_NULL0_INT BIT(0)

/* 2 REG_MSG2				(Offset 0x11F0) */

#define BIT_SHIFT_FW_MSG2 0
#define BIT_MASK_FW_MSG2 0xffffffffL
#define BIT_FW_MSG2(x) (((x) & BIT_MASK_FW_MSG2) << BIT_SHIFT_FW_MSG2)
#define BIT_GET_FW_MSG2(x) (((x) >> BIT_SHIFT_FW_MSG2) & BIT_MASK_FW_MSG2)

/* 2 REG_MSG3				(Offset 0x11F4) */

#define BIT_SHIFT_FW_MSG3 0
#define BIT_MASK_FW_MSG3 0xffffffffL
#define BIT_FW_MSG3(x) (((x) & BIT_MASK_FW_MSG3) << BIT_SHIFT_FW_MSG3)
#define BIT_GET_FW_MSG3(x) (((x) >> BIT_SHIFT_FW_MSG3) & BIT_MASK_FW_MSG3)

/* 2 REG_MSG4				(Offset 0x11F8) */

#define BIT_SHIFT_FW_MSG4 0
#define BIT_MASK_FW_MSG4 0xffffffffL
#define BIT_FW_MSG4(x) (((x) & BIT_MASK_FW_MSG4) << BIT_SHIFT_FW_MSG4)
#define BIT_GET_FW_MSG4(x) (((x) >> BIT_SHIFT_FW_MSG4) & BIT_MASK_FW_MSG4)

/* 2 REG_MSG5				(Offset 0x11FC) */

#define BIT_SHIFT_FW_MSG5 0
#define BIT_MASK_FW_MSG5 0xffffffffL
#define BIT_FW_MSG5(x) (((x) & BIT_MASK_FW_MSG5) << BIT_SHIFT_FW_MSG5)
#define BIT_GET_FW_MSG5(x) (((x) >> BIT_SHIFT_FW_MSG5) & BIT_MASK_FW_MSG5)

/* 2 REG_DDMA_CH0SA				(Offset 0x1200) */

#define BIT_SHIFT_DDMACH0_SA 0
#define BIT_MASK_DDMACH0_SA 0xffffffffL
#define BIT_DDMACH0_SA(x) (((x) & BIT_MASK_DDMACH0_SA) << BIT_SHIFT_DDMACH0_SA)
#define BIT_GET_DDMACH0_SA(x)                                                  \
	(((x) >> BIT_SHIFT_DDMACH0_SA) & BIT_MASK_DDMACH0_SA)

/* 2 REG_DDMA_CH0DA				(Offset 0x1204) */

#define BIT_SHIFT_DDMACH0_DA 0
#define BIT_MASK_DDMACH0_DA 0xffffffffL
#define BIT_DDMACH0_DA(x) (((x) & BIT_MASK_DDMACH0_DA) << BIT_SHIFT_DDMACH0_DA)
#define BIT_GET_DDMACH0_DA(x)                                                  \
	(((x) >> BIT_SHIFT_DDMACH0_DA) & BIT_MASK_DDMACH0_DA)

/* 2 REG_DDMA_CH0CTRL			(Offset 0x1208) */

#define BIT_DDMACH0_OWN BIT(31)
#define BIT_DDMACH0_CHKSUM_EN BIT(29)
#define BIT_DDMACH0_DA_W_DISABLE BIT(28)
#define BIT_DDMACH0_CHKSUM_STS BIT(27)
#define BIT_DDMACH0_DDMA_MODE BIT(26)
#define BIT_DDMACH0_RESET_CHKSUM_STS BIT(25)
#define BIT_DDMACH0_CHKSUM_CONT BIT(24)

#define BIT_SHIFT_DDMACH0_DLEN 0
#define BIT_MASK_DDMACH0_DLEN 0x3ffff
#define BIT_DDMACH0_DLEN(x)                                                    \
	(((x) & BIT_MASK_DDMACH0_DLEN) << BIT_SHIFT_DDMACH0_DLEN)
#define BIT_GET_DDMACH0_DLEN(x)                                                \
	(((x) >> BIT_SHIFT_DDMACH0_DLEN) & BIT_MASK_DDMACH0_DLEN)

/* 2 REG_DDMA_CH1SA				(Offset 0x1210) */

#define BIT_SHIFT_DDMACH1_SA 0
#define BIT_MASK_DDMACH1_SA 0xffffffffL
#define BIT_DDMACH1_SA(x) (((x) & BIT_MASK_DDMACH1_SA) << BIT_SHIFT_DDMACH1_SA)
#define BIT_GET_DDMACH1_SA(x)                                                  \
	(((x) >> BIT_SHIFT_DDMACH1_SA) & BIT_MASK_DDMACH1_SA)

/* 2 REG_DDMA_CH1DA				(Offset 0x1214) */

#define BIT_SHIFT_DDMACH1_DA 0
#define BIT_MASK_DDMACH1_DA 0xffffffffL
#define BIT_DDMACH1_DA(x) (((x) & BIT_MASK_DDMACH1_DA) << BIT_SHIFT_DDMACH1_DA)
#define BIT_GET_DDMACH1_DA(x)                                                  \
	(((x) >> BIT_SHIFT_DDMACH1_DA) & BIT_MASK_DDMACH1_DA)

/* 2 REG_DDMA_CH1CTRL			(Offset 0x1218) */

#define BIT_DDMACH1_OWN BIT(31)
#define BIT_DDMACH1_CHKSUM_EN BIT(29)
#define BIT_DDMACH1_DA_W_DISABLE BIT(28)
#define BIT_DDMACH1_CHKSUM_STS BIT(27)
#define BIT_DDMACH1_DDMA_MODE BIT(26)
#define BIT_DDMACH1_RESET_CHKSUM_STS BIT(25)
#define BIT_DDMACH1_CHKSUM_CONT BIT(24)

#define BIT_SHIFT_DDMACH1_DLEN 0
#define BIT_MASK_DDMACH1_DLEN 0x3ffff
#define BIT_DDMACH1_DLEN(x)                                                    \
	(((x) & BIT_MASK_DDMACH1_DLEN) << BIT_SHIFT_DDMACH1_DLEN)
#define BIT_GET_DDMACH1_DLEN(x)                                                \
	(((x) >> BIT_SHIFT_DDMACH1_DLEN) & BIT_MASK_DDMACH1_DLEN)

/* 2 REG_DDMA_CH2SA				(Offset 0x1220) */

#define BIT_SHIFT_DDMACH2_SA 0
#define BIT_MASK_DDMACH2_SA 0xffffffffL
#define BIT_DDMACH2_SA(x) (((x) & BIT_MASK_DDMACH2_SA) << BIT_SHIFT_DDMACH2_SA)
#define BIT_GET_DDMACH2_SA(x)                                                  \
	(((x) >> BIT_SHIFT_DDMACH2_SA) & BIT_MASK_DDMACH2_SA)

/* 2 REG_DDMA_CH2DA				(Offset 0x1224) */

#define BIT_SHIFT_DDMACH2_DA 0
#define BIT_MASK_DDMACH2_DA 0xffffffffL
#define BIT_DDMACH2_DA(x) (((x) & BIT_MASK_DDMACH2_DA) << BIT_SHIFT_DDMACH2_DA)
#define BIT_GET_DDMACH2_DA(x)                                                  \
	(((x) >> BIT_SHIFT_DDMACH2_DA) & BIT_MASK_DDMACH2_DA)

/* 2 REG_DDMA_CH2CTRL			(Offset 0x1228) */

#define BIT_DDMACH2_OWN BIT(31)
#define BIT_DDMACH2_CHKSUM_EN BIT(29)
#define BIT_DDMACH2_DA_W_DISABLE BIT(28)
#define BIT_DDMACH2_CHKSUM_STS BIT(27)
#define BIT_DDMACH2_DDMA_MODE BIT(26)
#define BIT_DDMACH2_RESET_CHKSUM_STS BIT(25)
#define BIT_DDMACH2_CHKSUM_CONT BIT(24)

#define BIT_SHIFT_DDMACH2_DLEN 0
#define BIT_MASK_DDMACH2_DLEN 0x3ffff
#define BIT_DDMACH2_DLEN(x)                                                    \
	(((x) & BIT_MASK_DDMACH2_DLEN) << BIT_SHIFT_DDMACH2_DLEN)
#define BIT_GET_DDMACH2_DLEN(x)                                                \
	(((x) >> BIT_SHIFT_DDMACH2_DLEN) & BIT_MASK_DDMACH2_DLEN)

/* 2 REG_DDMA_CH3SA				(Offset 0x1230) */

#define BIT_SHIFT_DDMACH3_SA 0
#define BIT_MASK_DDMACH3_SA 0xffffffffL
#define BIT_DDMACH3_SA(x) (((x) & BIT_MASK_DDMACH3_SA) << BIT_SHIFT_DDMACH3_SA)
#define BIT_GET_DDMACH3_SA(x)                                                  \
	(((x) >> BIT_SHIFT_DDMACH3_SA) & BIT_MASK_DDMACH3_SA)

/* 2 REG_DDMA_CH3DA				(Offset 0x1234) */

#define BIT_SHIFT_DDMACH3_DA 0
#define BIT_MASK_DDMACH3_DA 0xffffffffL
#define BIT_DDMACH3_DA(x) (((x) & BIT_MASK_DDMACH3_DA) << BIT_SHIFT_DDMACH3_DA)
#define BIT_GET_DDMACH3_DA(x)                                                  \
	(((x) >> BIT_SHIFT_DDMACH3_DA) & BIT_MASK_DDMACH3_DA)

/* 2 REG_DDMA_CH3CTRL			(Offset 0x1238) */

#define BIT_DDMACH3_OWN BIT(31)
#define BIT_DDMACH3_CHKSUM_EN BIT(29)
#define BIT_DDMACH3_DA_W_DISABLE BIT(28)
#define BIT_DDMACH3_CHKSUM_STS BIT(27)
#define BIT_DDMACH3_DDMA_MODE BIT(26)
#define BIT_DDMACH3_RESET_CHKSUM_STS BIT(25)
#define BIT_DDMACH3_CHKSUM_CONT BIT(24)

#define BIT_SHIFT_DDMACH3_DLEN 0
#define BIT_MASK_DDMACH3_DLEN 0x3ffff
#define BIT_DDMACH3_DLEN(x)                                                    \
	(((x) & BIT_MASK_DDMACH3_DLEN) << BIT_SHIFT_DDMACH3_DLEN)
#define BIT_GET_DDMACH3_DLEN(x)                                                \
	(((x) >> BIT_SHIFT_DDMACH3_DLEN) & BIT_MASK_DDMACH3_DLEN)

/* 2 REG_DDMA_CH4SA				(Offset 0x1240) */

#define BIT_SHIFT_DDMACH4_SA 0
#define BIT_MASK_DDMACH4_SA 0xffffffffL
#define BIT_DDMACH4_SA(x) (((x) & BIT_MASK_DDMACH4_SA) << BIT_SHIFT_DDMACH4_SA)
#define BIT_GET_DDMACH4_SA(x)                                                  \
	(((x) >> BIT_SHIFT_DDMACH4_SA) & BIT_MASK_DDMACH4_SA)

/* 2 REG_DDMA_CH4DA				(Offset 0x1244) */

#define BIT_SHIFT_DDMACH4_DA 0
#define BIT_MASK_DDMACH4_DA 0xffffffffL
#define BIT_DDMACH4_DA(x) (((x) & BIT_MASK_DDMACH4_DA) << BIT_SHIFT_DDMACH4_DA)
#define BIT_GET_DDMACH4_DA(x)                                                  \
	(((x) >> BIT_SHIFT_DDMACH4_DA) & BIT_MASK_DDMACH4_DA)

/* 2 REG_DDMA_CH4CTRL			(Offset 0x1248) */

#define BIT_DDMACH4_OWN BIT(31)
#define BIT_DDMACH4_CHKSUM_EN BIT(29)
#define BIT_DDMACH4_DA_W_DISABLE BIT(28)
#define BIT_DDMACH4_CHKSUM_STS BIT(27)
#define BIT_DDMACH4_DDMA_MODE BIT(26)
#define BIT_DDMACH4_RESET_CHKSUM_STS BIT(25)
#define BIT_DDMACH4_CHKSUM_CONT BIT(24)

#define BIT_SHIFT_DDMACH4_DLEN 0
#define BIT_MASK_DDMACH4_DLEN 0x3ffff
#define BIT_DDMACH4_DLEN(x)                                                    \
	(((x) & BIT_MASK_DDMACH4_DLEN) << BIT_SHIFT_DDMACH4_DLEN)
#define BIT_GET_DDMACH4_DLEN(x)                                                \
	(((x) >> BIT_SHIFT_DDMACH4_DLEN) & BIT_MASK_DDMACH4_DLEN)

/* 2 REG_DDMA_CH5SA				(Offset 0x1250) */

#define BIT_SHIFT_DDMACH5_SA 0
#define BIT_MASK_DDMACH5_SA 0xffffffffL
#define BIT_DDMACH5_SA(x) (((x) & BIT_MASK_DDMACH5_SA) << BIT_SHIFT_DDMACH5_SA)
#define BIT_GET_DDMACH5_SA(x)                                                  \
	(((x) >> BIT_SHIFT_DDMACH5_SA) & BIT_MASK_DDMACH5_SA)

/* 2 REG_DDMA_CH5DA				(Offset 0x1254) */

#define BIT_DDMACH5_OWN BIT(31)
#define BIT_DDMACH5_CHKSUM_EN BIT(29)
#define BIT_DDMACH5_DA_W_DISABLE BIT(28)
#define BIT_DDMACH5_CHKSUM_STS BIT(27)
#define BIT_DDMACH5_DDMA_MODE BIT(26)
#define BIT_DDMACH5_RESET_CHKSUM_STS BIT(25)
#define BIT_DDMACH5_CHKSUM_CONT BIT(24)

#define BIT_SHIFT_DDMACH5_DA 0
#define BIT_MASK_DDMACH5_DA 0xffffffffL
#define BIT_DDMACH5_DA(x) (((x) & BIT_MASK_DDMACH5_DA) << BIT_SHIFT_DDMACH5_DA)
#define BIT_GET_DDMACH5_DA(x)                                                  \
	(((x) >> BIT_SHIFT_DDMACH5_DA) & BIT_MASK_DDMACH5_DA)

#define BIT_SHIFT_DDMACH5_DLEN 0
#define BIT_MASK_DDMACH5_DLEN 0x3ffff
#define BIT_DDMACH5_DLEN(x)                                                    \
	(((x) & BIT_MASK_DDMACH5_DLEN) << BIT_SHIFT_DDMACH5_DLEN)
#define BIT_GET_DDMACH5_DLEN(x)                                                \
	(((x) >> BIT_SHIFT_DDMACH5_DLEN) & BIT_MASK_DDMACH5_DLEN)

/* 2 REG_DDMA_INT_MSK			(Offset 0x12E0) */

#define BIT_DDMACH5_MSK BIT(5)
#define BIT_DDMACH4_MSK BIT(4)
#define BIT_DDMACH3_MSK BIT(3)
#define BIT_DDMACH2_MSK BIT(2)
#define BIT_DDMACH1_MSK BIT(1)
#define BIT_DDMACH0_MSK BIT(0)

/* 2 REG_DDMA_CHSTATUS			(Offset 0x12E8) */

#define BIT_DDMACH5_BUSY BIT(5)
#define BIT_DDMACH4_BUSY BIT(4)
#define BIT_DDMACH3_BUSY BIT(3)
#define BIT_DDMACH2_BUSY BIT(2)
#define BIT_DDMACH1_BUSY BIT(1)
#define BIT_DDMACH0_BUSY BIT(0)

/* 2 REG_DDMA_CHKSUM				(Offset 0x12F0) */

#define BIT_SHIFT_IDDMA0_CHKSUM 0
#define BIT_MASK_IDDMA0_CHKSUM 0xffff
#define BIT_IDDMA0_CHKSUM(x)                                                   \
	(((x) & BIT_MASK_IDDMA0_CHKSUM) << BIT_SHIFT_IDDMA0_CHKSUM)
#define BIT_GET_IDDMA0_CHKSUM(x)                                               \
	(((x) >> BIT_SHIFT_IDDMA0_CHKSUM) & BIT_MASK_IDDMA0_CHKSUM)

/* 2 REG_DDMA_MONITOR			(Offset 0x12FC) */

#define BIT_IDDMA0_PERMU_UNDERFLOW BIT(14)
#define BIT_IDDMA0_FIFO_UNDERFLOW BIT(13)
#define BIT_IDDMA0_FIFO_OVERFLOW BIT(12)
#define BIT_ECRC_EN_V1 BIT(7)
#define BIT_MDIO_RFLAG_V1 BIT(6)
#define BIT_CH5_ERR BIT(5)
#define BIT_MDIO_WFLAG_V1 BIT(5)
#define BIT_CH4_ERR BIT(4)
#define BIT_CH3_ERR BIT(3)
#define BIT_CH2_ERR BIT(2)
#define BIT_CH1_ERR BIT(1)
#define BIT_CH0_ERR BIT(0)

/* 2 REG_STC_INT_CS				(Offset 0x1300) */

#define BIT_STC_INT_EN BIT(31)

#define BIT_SHIFT_STC_INT_FLAG 16
#define BIT_MASK_STC_INT_FLAG 0xff
#define BIT_STC_INT_FLAG(x)                                                    \
	(((x) & BIT_MASK_STC_INT_FLAG) << BIT_SHIFT_STC_INT_FLAG)
#define BIT_GET_STC_INT_FLAG(x)                                                \
	(((x) >> BIT_SHIFT_STC_INT_FLAG) & BIT_MASK_STC_INT_FLAG)

#define BIT_SHIFT_STC_INT_IDX 8
#define BIT_MASK_STC_INT_IDX 0x7
#define BIT_STC_INT_IDX(x)                                                     \
	(((x) & BIT_MASK_STC_INT_IDX) << BIT_SHIFT_STC_INT_IDX)
#define BIT_GET_STC_INT_IDX(x)                                                 \
	(((x) >> BIT_SHIFT_STC_INT_IDX) & BIT_MASK_STC_INT_IDX)

#define BIT_SHIFT_STC_INT_REALTIME_CS 0
#define BIT_MASK_STC_INT_REALTIME_CS 0x3f
#define BIT_STC_INT_REALTIME_CS(x)                                             \
	(((x) & BIT_MASK_STC_INT_REALTIME_CS) << BIT_SHIFT_STC_INT_REALTIME_CS)
#define BIT_GET_STC_INT_REALTIME_CS(x)                                         \
	(((x) >> BIT_SHIFT_STC_INT_REALTIME_CS) & BIT_MASK_STC_INT_REALTIME_CS)

/* 2 REG_ST_INT_CFG				(Offset 0x1304) */

#define BIT_STC_INT_GRP_EN BIT(31)

#define BIT_SHIFT_STC_INT_EXPECT_LS 8
#define BIT_MASK_STC_INT_EXPECT_LS 0x3f
#define BIT_STC_INT_EXPECT_LS(x)                                               \
	(((x) & BIT_MASK_STC_INT_EXPECT_LS) << BIT_SHIFT_STC_INT_EXPECT_LS)
#define BIT_GET_STC_INT_EXPECT_LS(x)                                           \
	(((x) >> BIT_SHIFT_STC_INT_EXPECT_LS) & BIT_MASK_STC_INT_EXPECT_LS)

#define BIT_SHIFT_STC_INT_EXPECT_CS 0
#define BIT_MASK_STC_INT_EXPECT_CS 0x3f
#define BIT_STC_INT_EXPECT_CS(x)                                               \
	(((x) & BIT_MASK_STC_INT_EXPECT_CS) << BIT_SHIFT_STC_INT_EXPECT_CS)
#define BIT_GET_STC_INT_EXPECT_CS(x)                                           \
	(((x) >> BIT_SHIFT_STC_INT_EXPECT_CS) & BIT_MASK_STC_INT_EXPECT_CS)

/* 2 REG_CMU_DLY_CTRL			(Offset 0x1310) */

#define BIT_CMU_DLY_EN BIT(31)
#define BIT_CMU_DLY_MODE BIT(30)

#define BIT_SHIFT_CMU_DLY_PRE_DIV 0
#define BIT_MASK_CMU_DLY_PRE_DIV 0xff
#define BIT_CMU_DLY_PRE_DIV(x)                                                 \
	(((x) & BIT_MASK_CMU_DLY_PRE_DIV) << BIT_SHIFT_CMU_DLY_PRE_DIV)
#define BIT_GET_CMU_DLY_PRE_DIV(x)                                             \
	(((x) >> BIT_SHIFT_CMU_DLY_PRE_DIV) & BIT_MASK_CMU_DLY_PRE_DIV)

/* 2 REG_CMU_DLY_CFG				(Offset 0x1314) */

#define BIT_SHIFT_CMU_DLY_LTR_A2I 24
#define BIT_MASK_CMU_DLY_LTR_A2I 0xff
#define BIT_CMU_DLY_LTR_A2I(x)                                                 \
	(((x) & BIT_MASK_CMU_DLY_LTR_A2I) << BIT_SHIFT_CMU_DLY_LTR_A2I)
#define BIT_GET_CMU_DLY_LTR_A2I(x)                                             \
	(((x) >> BIT_SHIFT_CMU_DLY_LTR_A2I) & BIT_MASK_CMU_DLY_LTR_A2I)

#define BIT_SHIFT_CMU_DLY_LTR_I2A 16
#define BIT_MASK_CMU_DLY_LTR_I2A 0xff
#define BIT_CMU_DLY_LTR_I2A(x)                                                 \
	(((x) & BIT_MASK_CMU_DLY_LTR_I2A) << BIT_SHIFT_CMU_DLY_LTR_I2A)
#define BIT_GET_CMU_DLY_LTR_I2A(x)                                             \
	(((x) >> BIT_SHIFT_CMU_DLY_LTR_I2A) & BIT_MASK_CMU_DLY_LTR_I2A)

#define BIT_SHIFT_CMU_DLY_LTR_IDLE 8
#define BIT_MASK_CMU_DLY_LTR_IDLE 0xff
#define BIT_CMU_DLY_LTR_IDLE(x)                                                \
	(((x) & BIT_MASK_CMU_DLY_LTR_IDLE) << BIT_SHIFT_CMU_DLY_LTR_IDLE)
#define BIT_GET_CMU_DLY_LTR_IDLE(x)                                            \
	(((x) >> BIT_SHIFT_CMU_DLY_LTR_IDLE) & BIT_MASK_CMU_DLY_LTR_IDLE)

#define BIT_SHIFT_CMU_DLY_LTR_ACT 0
#define BIT_MASK_CMU_DLY_LTR_ACT 0xff
#define BIT_CMU_DLY_LTR_ACT(x)                                                 \
	(((x) & BIT_MASK_CMU_DLY_LTR_ACT) << BIT_SHIFT_CMU_DLY_LTR_ACT)
#define BIT_GET_CMU_DLY_LTR_ACT(x)                                             \
	(((x) >> BIT_SHIFT_CMU_DLY_LTR_ACT) & BIT_MASK_CMU_DLY_LTR_ACT)

/* 2 REG_H2CQ_TXBD_DESA			(Offset 0x1320) */

#define BIT_SHIFT_H2CQ_TXBD_DESA 0
#define BIT_MASK_H2CQ_TXBD_DESA 0xffffffffffffffffL
#define BIT_H2CQ_TXBD_DESA(x)                                                  \
	(((x) & BIT_MASK_H2CQ_TXBD_DESA) << BIT_SHIFT_H2CQ_TXBD_DESA)
#define BIT_GET_H2CQ_TXBD_DESA(x)                                              \
	(((x) >> BIT_SHIFT_H2CQ_TXBD_DESA) & BIT_MASK_H2CQ_TXBD_DESA)

/* 2 REG_H2CQ_TXBD_NUM			(Offset 0x1328) */

#define BIT_PCIE_H2CQ_FLAG BIT(14)

/* 2 REG_H2CQ_TXBD_NUM			(Offset 0x1328) */

#define BIT_SHIFT_H2CQ_DESC_MODE 12
#define BIT_MASK_H2CQ_DESC_MODE 0x3
#define BIT_H2CQ_DESC_MODE(x)                                                  \
	(((x) & BIT_MASK_H2CQ_DESC_MODE) << BIT_SHIFT_H2CQ_DESC_MODE)
#define BIT_GET_H2CQ_DESC_MODE(x)                                              \
	(((x) >> BIT_SHIFT_H2CQ_DESC_MODE) & BIT_MASK_H2CQ_DESC_MODE)

#define BIT_SHIFT_H2CQ_DESC_NUM 0
#define BIT_MASK_H2CQ_DESC_NUM 0xfff
#define BIT_H2CQ_DESC_NUM(x)                                                   \
	(((x) & BIT_MASK_H2CQ_DESC_NUM) << BIT_SHIFT_H2CQ_DESC_NUM)
#define BIT_GET_H2CQ_DESC_NUM(x)                                               \
	(((x) >> BIT_SHIFT_H2CQ_DESC_NUM) & BIT_MASK_H2CQ_DESC_NUM)

/* 2 REG_H2CQ_TXBD_IDX			(Offset 0x132C) */

#define BIT_SHIFT_H2CQ_HW_IDX 16
#define BIT_MASK_H2CQ_HW_IDX 0xfff
#define BIT_H2CQ_HW_IDX(x)                                                     \
	(((x) & BIT_MASK_H2CQ_HW_IDX) << BIT_SHIFT_H2CQ_HW_IDX)
#define BIT_GET_H2CQ_HW_IDX(x)                                                 \
	(((x) >> BIT_SHIFT_H2CQ_HW_IDX) & BIT_MASK_H2CQ_HW_IDX)

#define BIT_SHIFT_H2CQ_HOST_IDX 0
#define BIT_MASK_H2CQ_HOST_IDX 0xfff
#define BIT_H2CQ_HOST_IDX(x)                                                   \
	(((x) & BIT_MASK_H2CQ_HOST_IDX) << BIT_SHIFT_H2CQ_HOST_IDX)
#define BIT_GET_H2CQ_HOST_IDX(x)                                               \
	(((x) >> BIT_SHIFT_H2CQ_HOST_IDX) & BIT_MASK_H2CQ_HOST_IDX)

/* 2 REG_H2CQ_CSR				(Offset 0x1330) */

#define BIT_H2CQ_FULL BIT(31)
#define BIT_CLR_H2CQ_HOST_IDX BIT(16)
#define BIT_CLR_H2CQ_HW_IDX BIT(8)

/* 2 REG_CHANGE_PCIE_SPEED			(Offset 0x1350) */

#define BIT_CHANGE_PCIE_SPEED BIT(18)

/* 2 REG_CHANGE_PCIE_SPEED			(Offset 0x1350) */

#define BIT_SHIFT_GEN1_GEN2 16
#define BIT_MASK_GEN1_GEN2 0x3
#define BIT_GEN1_GEN2(x) (((x) & BIT_MASK_GEN1_GEN2) << BIT_SHIFT_GEN1_GEN2)
#define BIT_GET_GEN1_GEN2(x) (((x) >> BIT_SHIFT_GEN1_GEN2) & BIT_MASK_GEN1_GEN2)

/* 2 REG_CHANGE_PCIE_SPEED			(Offset 0x1350) */

#define BIT_SHIFT_AUTO_HANG_RELEASE 0
#define BIT_MASK_AUTO_HANG_RELEASE 0x7
#define BIT_AUTO_HANG_RELEASE(x)                                               \
	(((x) & BIT_MASK_AUTO_HANG_RELEASE) << BIT_SHIFT_AUTO_HANG_RELEASE)
#define BIT_GET_AUTO_HANG_RELEASE(x)                                           \
	(((x) >> BIT_SHIFT_AUTO_HANG_RELEASE) & BIT_MASK_AUTO_HANG_RELEASE)

/* 2 REG_OLD_DEHANG				(Offset 0x13F4) */

#define BIT_OLD_DEHANG BIT(1)

/* 2 REG_Q0_Q1_INFO				(Offset 0x1400) */

#define BIT_SHIFT_AC1_PKT_INFO 16
#define BIT_MASK_AC1_PKT_INFO 0xfff
#define BIT_AC1_PKT_INFO(x)                                                    \
	(((x) & BIT_MASK_AC1_PKT_INFO) << BIT_SHIFT_AC1_PKT_INFO)
#define BIT_GET_AC1_PKT_INFO(x)                                                \
	(((x) >> BIT_SHIFT_AC1_PKT_INFO) & BIT_MASK_AC1_PKT_INFO)

#define BIT_SHIFT_AC0_PKT_INFO 0
#define BIT_MASK_AC0_PKT_INFO 0xfff
#define BIT_AC0_PKT_INFO(x)                                                    \
	(((x) & BIT_MASK_AC0_PKT_INFO) << BIT_SHIFT_AC0_PKT_INFO)
#define BIT_GET_AC0_PKT_INFO(x)                                                \
	(((x) >> BIT_SHIFT_AC0_PKT_INFO) & BIT_MASK_AC0_PKT_INFO)

/* 2 REG_Q2_Q3_INFO				(Offset 0x1404) */

#define BIT_SHIFT_AC3_PKT_INFO 16
#define BIT_MASK_AC3_PKT_INFO 0xfff
#define BIT_AC3_PKT_INFO(x)                                                    \
	(((x) & BIT_MASK_AC3_PKT_INFO) << BIT_SHIFT_AC3_PKT_INFO)
#define BIT_GET_AC3_PKT_INFO(x)                                                \
	(((x) >> BIT_SHIFT_AC3_PKT_INFO) & BIT_MASK_AC3_PKT_INFO)

#define BIT_SHIFT_AC2_PKT_INFO 0
#define BIT_MASK_AC2_PKT_INFO 0xfff
#define BIT_AC2_PKT_INFO(x)                                                    \
	(((x) & BIT_MASK_AC2_PKT_INFO) << BIT_SHIFT_AC2_PKT_INFO)
#define BIT_GET_AC2_PKT_INFO(x)                                                \
	(((x) >> BIT_SHIFT_AC2_PKT_INFO) & BIT_MASK_AC2_PKT_INFO)

/* 2 REG_Q4_Q5_INFO				(Offset 0x1408) */

#define BIT_SHIFT_AC5_PKT_INFO 16
#define BIT_MASK_AC5_PKT_INFO 0xfff
#define BIT_AC5_PKT_INFO(x)                                                    \
	(((x) & BIT_MASK_AC5_PKT_INFO) << BIT_SHIFT_AC5_PKT_INFO)
#define BIT_GET_AC5_PKT_INFO(x)                                                \
	(((x) >> BIT_SHIFT_AC5_PKT_INFO) & BIT_MASK_AC5_PKT_INFO)

#define BIT_SHIFT_AC4_PKT_INFO 0
#define BIT_MASK_AC4_PKT_INFO 0xfff
#define BIT_AC4_PKT_INFO(x)                                                    \
	(((x) & BIT_MASK_AC4_PKT_INFO) << BIT_SHIFT_AC4_PKT_INFO)
#define BIT_GET_AC4_PKT_INFO(x)                                                \
	(((x) >> BIT_SHIFT_AC4_PKT_INFO) & BIT_MASK_AC4_PKT_INFO)

/* 2 REG_Q6_Q7_INFO				(Offset 0x140C) */

#define BIT_SHIFT_AC7_PKT_INFO 16
#define BIT_MASK_AC7_PKT_INFO 0xfff
#define BIT_AC7_PKT_INFO(x)                                                    \
	(((x) & BIT_MASK_AC7_PKT_INFO) << BIT_SHIFT_AC7_PKT_INFO)
#define BIT_GET_AC7_PKT_INFO(x)                                                \
	(((x) >> BIT_SHIFT_AC7_PKT_INFO) & BIT_MASK_AC7_PKT_INFO)

#define BIT_SHIFT_AC6_PKT_INFO 0
#define BIT_MASK_AC6_PKT_INFO 0xfff
#define BIT_AC6_PKT_INFO(x)                                                    \
	(((x) & BIT_MASK_AC6_PKT_INFO) << BIT_SHIFT_AC6_PKT_INFO)
#define BIT_GET_AC6_PKT_INFO(x)                                                \
	(((x) >> BIT_SHIFT_AC6_PKT_INFO) & BIT_MASK_AC6_PKT_INFO)

/* 2 REG_MGQ_HIQ_INFO			(Offset 0x1410) */

#define BIT_SHIFT_HIQ_PKT_INFO 16
#define BIT_MASK_HIQ_PKT_INFO 0xfff
#define BIT_HIQ_PKT_INFO(x)                                                    \
	(((x) & BIT_MASK_HIQ_PKT_INFO) << BIT_SHIFT_HIQ_PKT_INFO)
#define BIT_GET_HIQ_PKT_INFO(x)                                                \
	(((x) >> BIT_SHIFT_HIQ_PKT_INFO) & BIT_MASK_HIQ_PKT_INFO)

#define BIT_SHIFT_MGQ_PKT_INFO 0
#define BIT_MASK_MGQ_PKT_INFO 0xfff
#define BIT_MGQ_PKT_INFO(x)                                                    \
	(((x) & BIT_MASK_MGQ_PKT_INFO) << BIT_SHIFT_MGQ_PKT_INFO)
#define BIT_GET_MGQ_PKT_INFO(x)                                                \
	(((x) >> BIT_SHIFT_MGQ_PKT_INFO) & BIT_MASK_MGQ_PKT_INFO)

/* 2 REG_CMDQ_BCNQ_INFO			(Offset 0x1414) */

#define BIT_SHIFT_CMDQ_PKT_INFO 16
#define BIT_MASK_CMDQ_PKT_INFO 0xfff
#define BIT_CMDQ_PKT_INFO(x)                                                   \
	(((x) & BIT_MASK_CMDQ_PKT_INFO) << BIT_SHIFT_CMDQ_PKT_INFO)
#define BIT_GET_CMDQ_PKT_INFO(x)                                               \
	(((x) >> BIT_SHIFT_CMDQ_PKT_INFO) & BIT_MASK_CMDQ_PKT_INFO)

/* 2 REG_CMDQ_BCNQ_INFO			(Offset 0x1414) */

#define BIT_SHIFT_BCNQ_PKT_INFO 0
#define BIT_MASK_BCNQ_PKT_INFO 0xfff
#define BIT_BCNQ_PKT_INFO(x)                                                   \
	(((x) & BIT_MASK_BCNQ_PKT_INFO) << BIT_SHIFT_BCNQ_PKT_INFO)
#define BIT_GET_BCNQ_PKT_INFO(x)                                               \
	(((x) >> BIT_SHIFT_BCNQ_PKT_INFO) & BIT_MASK_BCNQ_PKT_INFO)

/* 2 REG_USEREG_SETTING			(Offset 0x1420) */

#define BIT_NDPA_USEREG BIT(21)

#define BIT_SHIFT_RETRY_USEREG 19
#define BIT_MASK_RETRY_USEREG 0x3
#define BIT_RETRY_USEREG(x)                                                    \
	(((x) & BIT_MASK_RETRY_USEREG) << BIT_SHIFT_RETRY_USEREG)
#define BIT_GET_RETRY_USEREG(x)                                                \
	(((x) >> BIT_SHIFT_RETRY_USEREG) & BIT_MASK_RETRY_USEREG)

#define BIT_SHIFT_TRYPKT_USEREG 17
#define BIT_MASK_TRYPKT_USEREG 0x3
#define BIT_TRYPKT_USEREG(x)                                                   \
	(((x) & BIT_MASK_TRYPKT_USEREG) << BIT_SHIFT_TRYPKT_USEREG)
#define BIT_GET_TRYPKT_USEREG(x)                                               \
	(((x) >> BIT_SHIFT_TRYPKT_USEREG) & BIT_MASK_TRYPKT_USEREG)

#define BIT_CTLPKT_USEREG BIT(16)

/* 2 REG_AESIV_SETTING			(Offset 0x1424) */

#define BIT_SHIFT_AESIV_OFFSET 0
#define BIT_MASK_AESIV_OFFSET 0xfff
#define BIT_AESIV_OFFSET(x)                                                    \
	(((x) & BIT_MASK_AESIV_OFFSET) << BIT_SHIFT_AESIV_OFFSET)
#define BIT_GET_AESIV_OFFSET(x)                                                \
	(((x) >> BIT_SHIFT_AESIV_OFFSET) & BIT_MASK_AESIV_OFFSET)

/* 2 REG_BF0_TIME_SETTING			(Offset 0x1428) */

#define BIT_BF0_TIMER_SET BIT(31)
#define BIT_BF0_TIMER_CLR BIT(30)
#define BIT_BF0_UPDATE_EN BIT(29)
#define BIT_BF0_TIMER_EN BIT(28)

#define BIT_SHIFT_BF0_PRETIME_OVER 16
#define BIT_MASK_BF0_PRETIME_OVER 0xfff
#define BIT_BF0_PRETIME_OVER(x)                                                \
	(((x) & BIT_MASK_BF0_PRETIME_OVER) << BIT_SHIFT_BF0_PRETIME_OVER)
#define BIT_GET_BF0_PRETIME_OVER(x)                                            \
	(((x) >> BIT_SHIFT_BF0_PRETIME_OVER) & BIT_MASK_BF0_PRETIME_OVER)

#define BIT_SHIFT_BF0_LIFETIME 0
#define BIT_MASK_BF0_LIFETIME 0xffff
#define BIT_BF0_LIFETIME(x)                                                    \
	(((x) & BIT_MASK_BF0_LIFETIME) << BIT_SHIFT_BF0_LIFETIME)
#define BIT_GET_BF0_LIFETIME(x)                                                \
	(((x) >> BIT_SHIFT_BF0_LIFETIME) & BIT_MASK_BF0_LIFETIME)

/* 2 REG_BF1_TIME_SETTING			(Offset 0x142C) */

#define BIT_BF1_TIMER_SET BIT(31)
#define BIT_BF1_TIMER_CLR BIT(30)
#define BIT_BF1_UPDATE_EN BIT(29)
#define BIT_BF1_TIMER_EN BIT(28)

#define BIT_SHIFT_BF1_PRETIME_OVER 16
#define BIT_MASK_BF1_PRETIME_OVER 0xfff
#define BIT_BF1_PRETIME_OVER(x)                                                \
	(((x) & BIT_MASK_BF1_PRETIME_OVER) << BIT_SHIFT_BF1_PRETIME_OVER)
#define BIT_GET_BF1_PRETIME_OVER(x)                                            \
	(((x) >> BIT_SHIFT_BF1_PRETIME_OVER) & BIT_MASK_BF1_PRETIME_OVER)

#define BIT_SHIFT_BF1_LIFETIME 0
#define BIT_MASK_BF1_LIFETIME 0xffff
#define BIT_BF1_LIFETIME(x)                                                    \
	(((x) & BIT_MASK_BF1_LIFETIME) << BIT_SHIFT_BF1_LIFETIME)
#define BIT_GET_BF1_LIFETIME(x)                                                \
	(((x) >> BIT_SHIFT_BF1_LIFETIME) & BIT_MASK_BF1_LIFETIME)

/* 2 REG_BF_TIMEOUT_EN			(Offset 0x1430) */

#define BIT_EN_VHT_LDPC BIT(9)
#define BIT_EN_HT_LDPC BIT(8)
#define BIT_BF1_TIMEOUT_EN BIT(1)
#define BIT_BF0_TIMEOUT_EN BIT(0)

/* 2 REG_MACID_RELEASE0			(Offset 0x1434) */

#define BIT_SHIFT_MACID31_0_RELEASE 0
#define BIT_MASK_MACID31_0_RELEASE 0xffffffffL
#define BIT_MACID31_0_RELEASE(x)                                               \
	(((x) & BIT_MASK_MACID31_0_RELEASE) << BIT_SHIFT_MACID31_0_RELEASE)
#define BIT_GET_MACID31_0_RELEASE(x)                                           \
	(((x) >> BIT_SHIFT_MACID31_0_RELEASE) & BIT_MASK_MACID31_0_RELEASE)

/* 2 REG_MACID_RELEASE1			(Offset 0x1438) */

#define BIT_SHIFT_MACID63_32_RELEASE 0
#define BIT_MASK_MACID63_32_RELEASE 0xffffffffL
#define BIT_MACID63_32_RELEASE(x)                                              \
	(((x) & BIT_MASK_MACID63_32_RELEASE) << BIT_SHIFT_MACID63_32_RELEASE)
#define BIT_GET_MACID63_32_RELEASE(x)                                          \
	(((x) >> BIT_SHIFT_MACID63_32_RELEASE) & BIT_MASK_MACID63_32_RELEASE)

/* 2 REG_MACID_RELEASE2			(Offset 0x143C) */

#define BIT_SHIFT_MACID95_64_RELEASE 0
#define BIT_MASK_MACID95_64_RELEASE 0xffffffffL
#define BIT_MACID95_64_RELEASE(x)                                              \
	(((x) & BIT_MASK_MACID95_64_RELEASE) << BIT_SHIFT_MACID95_64_RELEASE)
#define BIT_GET_MACID95_64_RELEASE(x)                                          \
	(((x) >> BIT_SHIFT_MACID95_64_RELEASE) & BIT_MASK_MACID95_64_RELEASE)

/* 2 REG_MACID_RELEASE3			(Offset 0x1440) */

#define BIT_SHIFT_MACID127_96_RELEASE 0
#define BIT_MASK_MACID127_96_RELEASE 0xffffffffL
#define BIT_MACID127_96_RELEASE(x)                                             \
	(((x) & BIT_MASK_MACID127_96_RELEASE) << BIT_SHIFT_MACID127_96_RELEASE)
#define BIT_GET_MACID127_96_RELEASE(x)                                         \
	(((x) >> BIT_SHIFT_MACID127_96_RELEASE) & BIT_MASK_MACID127_96_RELEASE)

/* 2 REG_MACID_RELEASE_SETTING		(Offset 0x1444) */

#define BIT_MACID_VALUE BIT(7)

#define BIT_SHIFT_MACID_OFFSET 0
#define BIT_MASK_MACID_OFFSET 0x7f
#define BIT_MACID_OFFSET(x)                                                    \
	(((x) & BIT_MASK_MACID_OFFSET) << BIT_SHIFT_MACID_OFFSET)
#define BIT_GET_MACID_OFFSET(x)                                                \
	(((x) >> BIT_SHIFT_MACID_OFFSET) & BIT_MASK_MACID_OFFSET)

/* 2 REG_FAST_EDCA_VOVI_SETTING		(Offset 0x1448) */

#define BIT_SHIFT_VI_FAST_EDCA_TO 24
#define BIT_MASK_VI_FAST_EDCA_TO 0xff
#define BIT_VI_FAST_EDCA_TO(x)                                                 \
	(((x) & BIT_MASK_VI_FAST_EDCA_TO) << BIT_SHIFT_VI_FAST_EDCA_TO)
#define BIT_GET_VI_FAST_EDCA_TO(x)                                             \
	(((x) >> BIT_SHIFT_VI_FAST_EDCA_TO) & BIT_MASK_VI_FAST_EDCA_TO)

#define BIT_VI_THRESHOLD_SEL BIT(23)

#define BIT_SHIFT_VI_FAST_EDCA_PKT_TH 16
#define BIT_MASK_VI_FAST_EDCA_PKT_TH 0x7f
#define BIT_VI_FAST_EDCA_PKT_TH(x)                                             \
	(((x) & BIT_MASK_VI_FAST_EDCA_PKT_TH) << BIT_SHIFT_VI_FAST_EDCA_PKT_TH)
#define BIT_GET_VI_FAST_EDCA_PKT_TH(x)                                         \
	(((x) >> BIT_SHIFT_VI_FAST_EDCA_PKT_TH) & BIT_MASK_VI_FAST_EDCA_PKT_TH)

#define BIT_SHIFT_VO_FAST_EDCA_TO 8
#define BIT_MASK_VO_FAST_EDCA_TO 0xff
#define BIT_VO_FAST_EDCA_TO(x)                                                 \
	(((x) & BIT_MASK_VO_FAST_EDCA_TO) << BIT_SHIFT_VO_FAST_EDCA_TO)
#define BIT_GET_VO_FAST_EDCA_TO(x)                                             \
	(((x) >> BIT_SHIFT_VO_FAST_EDCA_TO) & BIT_MASK_VO_FAST_EDCA_TO)

#define BIT_VO_THRESHOLD_SEL BIT(7)

#define BIT_SHIFT_VO_FAST_EDCA_PKT_TH 0
#define BIT_MASK_VO_FAST_EDCA_PKT_TH 0x7f
#define BIT_VO_FAST_EDCA_PKT_TH(x)                                             \
	(((x) & BIT_MASK_VO_FAST_EDCA_PKT_TH) << BIT_SHIFT_VO_FAST_EDCA_PKT_TH)
#define BIT_GET_VO_FAST_EDCA_PKT_TH(x)                                         \
	(((x) >> BIT_SHIFT_VO_FAST_EDCA_PKT_TH) & BIT_MASK_VO_FAST_EDCA_PKT_TH)

/* 2 REG_FAST_EDCA_BEBK_SETTING		(Offset 0x144C) */

#define BIT_SHIFT_BK_FAST_EDCA_TO 24
#define BIT_MASK_BK_FAST_EDCA_TO 0xff
#define BIT_BK_FAST_EDCA_TO(x)                                                 \
	(((x) & BIT_MASK_BK_FAST_EDCA_TO) << BIT_SHIFT_BK_FAST_EDCA_TO)
#define BIT_GET_BK_FAST_EDCA_TO(x)                                             \
	(((x) >> BIT_SHIFT_BK_FAST_EDCA_TO) & BIT_MASK_BK_FAST_EDCA_TO)

#define BIT_BK_THRESHOLD_SEL BIT(23)

#define BIT_SHIFT_BK_FAST_EDCA_PKT_TH 16
#define BIT_MASK_BK_FAST_EDCA_PKT_TH 0x7f
#define BIT_BK_FAST_EDCA_PKT_TH(x)                                             \
	(((x) & BIT_MASK_BK_FAST_EDCA_PKT_TH) << BIT_SHIFT_BK_FAST_EDCA_PKT_TH)
#define BIT_GET_BK_FAST_EDCA_PKT_TH(x)                                         \
	(((x) >> BIT_SHIFT_BK_FAST_EDCA_PKT_TH) & BIT_MASK_BK_FAST_EDCA_PKT_TH)

#define BIT_SHIFT_BE_FAST_EDCA_TO 8
#define BIT_MASK_BE_FAST_EDCA_TO 0xff
#define BIT_BE_FAST_EDCA_TO(x)                                                 \
	(((x) & BIT_MASK_BE_FAST_EDCA_TO) << BIT_SHIFT_BE_FAST_EDCA_TO)
#define BIT_GET_BE_FAST_EDCA_TO(x)                                             \
	(((x) >> BIT_SHIFT_BE_FAST_EDCA_TO) & BIT_MASK_BE_FAST_EDCA_TO)

#define BIT_BE_THRESHOLD_SEL BIT(7)

#define BIT_SHIFT_BE_FAST_EDCA_PKT_TH 0
#define BIT_MASK_BE_FAST_EDCA_PKT_TH 0x7f
#define BIT_BE_FAST_EDCA_PKT_TH(x)                                             \
	(((x) & BIT_MASK_BE_FAST_EDCA_PKT_TH) << BIT_SHIFT_BE_FAST_EDCA_PKT_TH)
#define BIT_GET_BE_FAST_EDCA_PKT_TH(x)                                         \
	(((x) >> BIT_SHIFT_BE_FAST_EDCA_PKT_TH) & BIT_MASK_BE_FAST_EDCA_PKT_TH)

/* 2 REG_MACID_DROP0				(Offset 0x1450) */

#define BIT_SHIFT_MACID31_0_DROP 0
#define BIT_MASK_MACID31_0_DROP 0xffffffffL
#define BIT_MACID31_0_DROP(x)                                                  \
	(((x) & BIT_MASK_MACID31_0_DROP) << BIT_SHIFT_MACID31_0_DROP)
#define BIT_GET_MACID31_0_DROP(x)                                              \
	(((x) >> BIT_SHIFT_MACID31_0_DROP) & BIT_MASK_MACID31_0_DROP)

/* 2 REG_MACID_DROP1				(Offset 0x1454) */

#define BIT_SHIFT_MACID63_32_DROP 0
#define BIT_MASK_MACID63_32_DROP 0xffffffffL
#define BIT_MACID63_32_DROP(x)                                                 \
	(((x) & BIT_MASK_MACID63_32_DROP) << BIT_SHIFT_MACID63_32_DROP)
#define BIT_GET_MACID63_32_DROP(x)                                             \
	(((x) >> BIT_SHIFT_MACID63_32_DROP) & BIT_MASK_MACID63_32_DROP)

/* 2 REG_MACID_DROP2				(Offset 0x1458) */

#define BIT_SHIFT_MACID95_64_DROP 0
#define BIT_MASK_MACID95_64_DROP 0xffffffffL
#define BIT_MACID95_64_DROP(x)                                                 \
	(((x) & BIT_MASK_MACID95_64_DROP) << BIT_SHIFT_MACID95_64_DROP)
#define BIT_GET_MACID95_64_DROP(x)                                             \
	(((x) >> BIT_SHIFT_MACID95_64_DROP) & BIT_MASK_MACID95_64_DROP)

/* 2 REG_MACID_DROP3				(Offset 0x145C) */

#define BIT_SHIFT_MACID127_96_DROP 0
#define BIT_MASK_MACID127_96_DROP 0xffffffffL
#define BIT_MACID127_96_DROP(x)                                                \
	(((x) & BIT_MASK_MACID127_96_DROP) << BIT_SHIFT_MACID127_96_DROP)
#define BIT_GET_MACID127_96_DROP(x)                                            \
	(((x) >> BIT_SHIFT_MACID127_96_DROP) & BIT_MASK_MACID127_96_DROP)

/* 2 REG_R_MACID_RELEASE_SUCCESS_0		(Offset 0x1460) */

#define BIT_SHIFT_R_MACID_RELEASE_SUCCESS_0 0
#define BIT_MASK_R_MACID_RELEASE_SUCCESS_0 0xffffffffL
#define BIT_R_MACID_RELEASE_SUCCESS_0(x)                                       \
	(((x) & BIT_MASK_R_MACID_RELEASE_SUCCESS_0)                            \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_0)
#define BIT_GET_R_MACID_RELEASE_SUCCESS_0(x)                                   \
	(((x) >> BIT_SHIFT_R_MACID_RELEASE_SUCCESS_0) &                        \
	 BIT_MASK_R_MACID_RELEASE_SUCCESS_0)

/* 2 REG_R_MACID_RELEASE_SUCCESS_1		(Offset 0x1464) */

#define BIT_SHIFT_R_MACID_RELEASE_SUCCESS_1 0
#define BIT_MASK_R_MACID_RELEASE_SUCCESS_1 0xffffffffL
#define BIT_R_MACID_RELEASE_SUCCESS_1(x)                                       \
	(((x) & BIT_MASK_R_MACID_RELEASE_SUCCESS_1)                            \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_1)
#define BIT_GET_R_MACID_RELEASE_SUCCESS_1(x)                                   \
	(((x) >> BIT_SHIFT_R_MACID_RELEASE_SUCCESS_1) &                        \
	 BIT_MASK_R_MACID_RELEASE_SUCCESS_1)

/* 2 REG_R_MACID_RELEASE_SUCCESS_2		(Offset 0x1468) */

#define BIT_SHIFT_R_MACID_RELEASE_SUCCESS_2 0
#define BIT_MASK_R_MACID_RELEASE_SUCCESS_2 0xffffffffL
#define BIT_R_MACID_RELEASE_SUCCESS_2(x)                                       \
	(((x) & BIT_MASK_R_MACID_RELEASE_SUCCESS_2)                            \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_2)
#define BIT_GET_R_MACID_RELEASE_SUCCESS_2(x)                                   \
	(((x) >> BIT_SHIFT_R_MACID_RELEASE_SUCCESS_2) &                        \
	 BIT_MASK_R_MACID_RELEASE_SUCCESS_2)

/* 2 REG_R_MACID_RELEASE_SUCCESS_3		(Offset 0x146C) */

#define BIT_SHIFT_R_MACID_RELEASE_SUCCESS_3 0
#define BIT_MASK_R_MACID_RELEASE_SUCCESS_3 0xffffffffL
#define BIT_R_MACID_RELEASE_SUCCESS_3(x)                                       \
	(((x) & BIT_MASK_R_MACID_RELEASE_SUCCESS_3)                            \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_3)
#define BIT_GET_R_MACID_RELEASE_SUCCESS_3(x)                                   \
	(((x) >> BIT_SHIFT_R_MACID_RELEASE_SUCCESS_3) &                        \
	 BIT_MASK_R_MACID_RELEASE_SUCCESS_3)

/* 2 REG_MGG_FIFO_CRTL			(Offset 0x1470) */

#define BIT_R_MGG_FIFO_EN BIT(31)

#define BIT_SHIFT_R_MGG_FIFO_PG_SIZE 28
#define BIT_MASK_R_MGG_FIFO_PG_SIZE 0x7
#define BIT_R_MGG_FIFO_PG_SIZE(x)                                              \
	(((x) & BIT_MASK_R_MGG_FIFO_PG_SIZE) << BIT_SHIFT_R_MGG_FIFO_PG_SIZE)
#define BIT_GET_R_MGG_FIFO_PG_SIZE(x)                                          \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_PG_SIZE) & BIT_MASK_R_MGG_FIFO_PG_SIZE)

#define BIT_SHIFT_R_MGG_FIFO_START_PG 16
#define BIT_MASK_R_MGG_FIFO_START_PG 0xfff
#define BIT_R_MGG_FIFO_START_PG(x)                                             \
	(((x) & BIT_MASK_R_MGG_FIFO_START_PG) << BIT_SHIFT_R_MGG_FIFO_START_PG)
#define BIT_GET_R_MGG_FIFO_START_PG(x)                                         \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_START_PG) & BIT_MASK_R_MGG_FIFO_START_PG)

#define BIT_SHIFT_R_MGG_FIFO_SIZE 14
#define BIT_MASK_R_MGG_FIFO_SIZE 0x3
#define BIT_R_MGG_FIFO_SIZE(x)                                                 \
	(((x) & BIT_MASK_R_MGG_FIFO_SIZE) << BIT_SHIFT_R_MGG_FIFO_SIZE)
#define BIT_GET_R_MGG_FIFO_SIZE(x)                                             \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_SIZE) & BIT_MASK_R_MGG_FIFO_SIZE)

#define BIT_R_MGG_FIFO_PAUSE BIT(13)

#define BIT_SHIFT_R_MGG_FIFO_RPTR 8
#define BIT_MASK_R_MGG_FIFO_RPTR 0x1f
#define BIT_R_MGG_FIFO_RPTR(x)                                                 \
	(((x) & BIT_MASK_R_MGG_FIFO_RPTR) << BIT_SHIFT_R_MGG_FIFO_RPTR)
#define BIT_GET_R_MGG_FIFO_RPTR(x)                                             \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_RPTR) & BIT_MASK_R_MGG_FIFO_RPTR)

#define BIT_R_MGG_FIFO_OV BIT(7)
#define BIT_R_MGG_FIFO_WPTR_ERROR BIT(6)
#define BIT_R_EN_CPU_LIFETIME BIT(5)

#define BIT_SHIFT_R_MGG_FIFO_WPTR 0
#define BIT_MASK_R_MGG_FIFO_WPTR 0x1f
#define BIT_R_MGG_FIFO_WPTR(x)                                                 \
	(((x) & BIT_MASK_R_MGG_FIFO_WPTR) << BIT_SHIFT_R_MGG_FIFO_WPTR)
#define BIT_GET_R_MGG_FIFO_WPTR(x)                                             \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_WPTR) & BIT_MASK_R_MGG_FIFO_WPTR)

/* 2 REG_MGG_FIFO_INT			(Offset 0x1474) */

#define BIT_SHIFT_R_MGG_FIFO_INT_FLAG 16
#define BIT_MASK_R_MGG_FIFO_INT_FLAG 0xffff
#define BIT_R_MGG_FIFO_INT_FLAG(x)                                             \
	(((x) & BIT_MASK_R_MGG_FIFO_INT_FLAG) << BIT_SHIFT_R_MGG_FIFO_INT_FLAG)
#define BIT_GET_R_MGG_FIFO_INT_FLAG(x)                                         \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_INT_FLAG) & BIT_MASK_R_MGG_FIFO_INT_FLAG)

#define BIT_SHIFT_R_MGG_FIFO_INT_MASK 0
#define BIT_MASK_R_MGG_FIFO_INT_MASK 0xffff
#define BIT_R_MGG_FIFO_INT_MASK(x)                                             \
	(((x) & BIT_MASK_R_MGG_FIFO_INT_MASK) << BIT_SHIFT_R_MGG_FIFO_INT_MASK)
#define BIT_GET_R_MGG_FIFO_INT_MASK(x)                                         \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_INT_MASK) & BIT_MASK_R_MGG_FIFO_INT_MASK)

/* 2 REG_MGG_FIFO_LIFETIME			(Offset 0x1478) */

#define BIT_SHIFT_R_MGG_FIFO_LIFETIME 16
#define BIT_MASK_R_MGG_FIFO_LIFETIME 0xffff
#define BIT_R_MGG_FIFO_LIFETIME(x)                                             \
	(((x) & BIT_MASK_R_MGG_FIFO_LIFETIME) << BIT_SHIFT_R_MGG_FIFO_LIFETIME)
#define BIT_GET_R_MGG_FIFO_LIFETIME(x)                                         \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_LIFETIME) & BIT_MASK_R_MGG_FIFO_LIFETIME)

#define BIT_SHIFT_R_MGG_FIFO_VALID_MAP 0
#define BIT_MASK_R_MGG_FIFO_VALID_MAP 0xffff
#define BIT_R_MGG_FIFO_VALID_MAP(x)                                            \
	(((x) & BIT_MASK_R_MGG_FIFO_VALID_MAP)                                 \
	 << BIT_SHIFT_R_MGG_FIFO_VALID_MAP)
#define BIT_GET_R_MGG_FIFO_VALID_MAP(x)                                        \
	(((x) >> BIT_SHIFT_R_MGG_FIFO_VALID_MAP) &                             \
	 BIT_MASK_R_MGG_FIFO_VALID_MAP)

/* 2 REG_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET (Offset 0x147C) */

#define BIT_SHIFT_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET 0
#define BIT_MASK_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET 0x7f
#define BIT_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET(x)                            \
	(((x) & BIT_MASK_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET)                 \
	 << BIT_SHIFT_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET)
#define BIT_GET_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET(x)                        \
	(((x) >> BIT_SHIFT_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET) &             \
	 BIT_MASK_R_MACID_RELEASE_SUCCESS_CLEAR_OFFSET)

#define BIT_SHIFT_P2PON_DIS_TXTIME 0
#define BIT_MASK_P2PON_DIS_TXTIME 0xff
#define BIT_P2PON_DIS_TXTIME(x)                                                \
	(((x) & BIT_MASK_P2PON_DIS_TXTIME) << BIT_SHIFT_P2PON_DIS_TXTIME)
#define BIT_GET_P2PON_DIS_TXTIME(x)                                            \
	(((x) >> BIT_SHIFT_P2PON_DIS_TXTIME) & BIT_MASK_P2PON_DIS_TXTIME)

/* 2 REG_MACID_SHCUT_OFFSET			(Offset 0x1480) */

#define BIT_SHIFT_MACID_SHCUT_OFFSET_V1 0
#define BIT_MASK_MACID_SHCUT_OFFSET_V1 0xff
#define BIT_MACID_SHCUT_OFFSET_V1(x)                                           \
	(((x) & BIT_MASK_MACID_SHCUT_OFFSET_V1)                                \
	 << BIT_SHIFT_MACID_SHCUT_OFFSET_V1)
#define BIT_GET_MACID_SHCUT_OFFSET_V1(x)                                       \
	(((x) >> BIT_SHIFT_MACID_SHCUT_OFFSET_V1) &                            \
	 BIT_MASK_MACID_SHCUT_OFFSET_V1)

/* 2 REG_MU_TX_CTL				(Offset 0x14C0) */

#define BIT_R_EN_REVERS_GTAB BIT(6)

#define BIT_SHIFT_R_MU_TABLE_VALID 0
#define BIT_MASK_R_MU_TABLE_VALID 0x3f
#define BIT_R_MU_TABLE_VALID(x)                                                \
	(((x) & BIT_MASK_R_MU_TABLE_VALID) << BIT_SHIFT_R_MU_TABLE_VALID)
#define BIT_GET_R_MU_TABLE_VALID(x)                                            \
	(((x) >> BIT_SHIFT_R_MU_TABLE_VALID) & BIT_MASK_R_MU_TABLE_VALID)

#define BIT_SHIFT_R_MU_STA_GTAB_VALID 0
#define BIT_MASK_R_MU_STA_GTAB_VALID 0xffffffffL
#define BIT_R_MU_STA_GTAB_VALID(x)                                             \
	(((x) & BIT_MASK_R_MU_STA_GTAB_VALID) << BIT_SHIFT_R_MU_STA_GTAB_VALID)
#define BIT_GET_R_MU_STA_GTAB_VALID(x)                                         \
	(((x) >> BIT_SHIFT_R_MU_STA_GTAB_VALID) & BIT_MASK_R_MU_STA_GTAB_VALID)

#define BIT_SHIFT_R_MU_STA_GTAB_POSITION 0
#define BIT_MASK_R_MU_STA_GTAB_POSITION 0xffffffffffffffffL
#define BIT_R_MU_STA_GTAB_POSITION(x)                                          \
	(((x) & BIT_MASK_R_MU_STA_GTAB_POSITION)                               \
	 << BIT_SHIFT_R_MU_STA_GTAB_POSITION)
#define BIT_GET_R_MU_STA_GTAB_POSITION(x)                                      \
	(((x) >> BIT_SHIFT_R_MU_STA_GTAB_POSITION) &                           \
	 BIT_MASK_R_MU_STA_GTAB_POSITION)

/* 2 REG_MU_TRX_DBG_CNT			(Offset 0x14D0) */

#define BIT_MU_DNGCNT_RST BIT(20)

#define BIT_SHIFT_MU_DBGCNT_SEL 16
#define BIT_MASK_MU_DBGCNT_SEL 0xf
#define BIT_MU_DBGCNT_SEL(x)                                                   \
	(((x) & BIT_MASK_MU_DBGCNT_SEL) << BIT_SHIFT_MU_DBGCNT_SEL)
#define BIT_GET_MU_DBGCNT_SEL(x)                                               \
	(((x) >> BIT_SHIFT_MU_DBGCNT_SEL) & BIT_MASK_MU_DBGCNT_SEL)

#define BIT_SHIFT_MU_DNGCNT 0
#define BIT_MASK_MU_DNGCNT 0xffff
#define BIT_MU_DNGCNT(x) (((x) & BIT_MASK_MU_DNGCNT) << BIT_SHIFT_MU_DNGCNT)
#define BIT_GET_MU_DNGCNT(x) (((x) >> BIT_SHIFT_MU_DNGCNT) & BIT_MASK_MU_DNGCNT)

/* 2 REG_CPUMGQ_TX_TIMER			(Offset 0x1500) */

#define BIT_SHIFT_CPUMGQ_TX_TIMER_V1 0
#define BIT_MASK_CPUMGQ_TX_TIMER_V1 0xffffffffL
#define BIT_CPUMGQ_TX_TIMER_V1(x)                                              \
	(((x) & BIT_MASK_CPUMGQ_TX_TIMER_V1) << BIT_SHIFT_CPUMGQ_TX_TIMER_V1)
#define BIT_GET_CPUMGQ_TX_TIMER_V1(x)                                          \
	(((x) >> BIT_SHIFT_CPUMGQ_TX_TIMER_V1) & BIT_MASK_CPUMGQ_TX_TIMER_V1)

/* 2 REG_PS_TIMER_A				(Offset 0x1504) */

#define BIT_SHIFT_PS_TIMER_A_V1 0
#define BIT_MASK_PS_TIMER_A_V1 0xffffffffL
#define BIT_PS_TIMER_A_V1(x)                                                   \
	(((x) & BIT_MASK_PS_TIMER_A_V1) << BIT_SHIFT_PS_TIMER_A_V1)
#define BIT_GET_PS_TIMER_A_V1(x)                                               \
	(((x) >> BIT_SHIFT_PS_TIMER_A_V1) & BIT_MASK_PS_TIMER_A_V1)

/* 2 REG_PS_TIMER_B				(Offset 0x1508) */

#define BIT_SHIFT_PS_TIMER_B_V1 0
#define BIT_MASK_PS_TIMER_B_V1 0xffffffffL
#define BIT_PS_TIMER_B_V1(x)                                                   \
	(((x) & BIT_MASK_PS_TIMER_B_V1) << BIT_SHIFT_PS_TIMER_B_V1)
#define BIT_GET_PS_TIMER_B_V1(x)                                               \
	(((x) >> BIT_SHIFT_PS_TIMER_B_V1) & BIT_MASK_PS_TIMER_B_V1)

/* 2 REG_PS_TIMER_C				(Offset 0x150C) */

#define BIT_SHIFT_PS_TIMER_C_V1 0
#define BIT_MASK_PS_TIMER_C_V1 0xffffffffL
#define BIT_PS_TIMER_C_V1(x)                                                   \
	(((x) & BIT_MASK_PS_TIMER_C_V1) << BIT_SHIFT_PS_TIMER_C_V1)
#define BIT_GET_PS_TIMER_C_V1(x)                                               \
	(((x) >> BIT_SHIFT_PS_TIMER_C_V1) & BIT_MASK_PS_TIMER_C_V1)

/* 2 REG_PS_TIMER_ABC_CPUMGQ_TIMER_CRTL	(Offset 0x1510) */

#define BIT_CPUMGQ_TIMER_EN BIT(31)
#define BIT_CPUMGQ_TX_EN BIT(28)

#define BIT_SHIFT_CPUMGQ_TIMER_TSF_SEL 24
#define BIT_MASK_CPUMGQ_TIMER_TSF_SEL 0x7
#define BIT_CPUMGQ_TIMER_TSF_SEL(x)                                            \
	(((x) & BIT_MASK_CPUMGQ_TIMER_TSF_SEL)                                 \
	 << BIT_SHIFT_CPUMGQ_TIMER_TSF_SEL)
#define BIT_GET_CPUMGQ_TIMER_TSF_SEL(x)                                        \
	(((x) >> BIT_SHIFT_CPUMGQ_TIMER_TSF_SEL) &                             \
	 BIT_MASK_CPUMGQ_TIMER_TSF_SEL)

#define BIT_PS_TIMER_C_EN BIT(23)

#define BIT_SHIFT_PS_TIMER_C_TSF_SEL 16
#define BIT_MASK_PS_TIMER_C_TSF_SEL 0x7
#define BIT_PS_TIMER_C_TSF_SEL(x)                                              \
	(((x) & BIT_MASK_PS_TIMER_C_TSF_SEL) << BIT_SHIFT_PS_TIMER_C_TSF_SEL)
#define BIT_GET_PS_TIMER_C_TSF_SEL(x)                                          \
	(((x) >> BIT_SHIFT_PS_TIMER_C_TSF_SEL) & BIT_MASK_PS_TIMER_C_TSF_SEL)

#define BIT_PS_TIMER_B_EN BIT(15)

#define BIT_SHIFT_PS_TIMER_B_TSF_SEL 8
#define BIT_MASK_PS_TIMER_B_TSF_SEL 0x7
#define BIT_PS_TIMER_B_TSF_SEL(x)                                              \
	(((x) & BIT_MASK_PS_TIMER_B_TSF_SEL) << BIT_SHIFT_PS_TIMER_B_TSF_SEL)
#define BIT_GET_PS_TIMER_B_TSF_SEL(x)                                          \
	(((x) >> BIT_SHIFT_PS_TIMER_B_TSF_SEL) & BIT_MASK_PS_TIMER_B_TSF_SEL)

#define BIT_PS_TIMER_A_EN BIT(7)

#define BIT_SHIFT_PS_TIMER_A_TSF_SEL 0
#define BIT_MASK_PS_TIMER_A_TSF_SEL 0x7
#define BIT_PS_TIMER_A_TSF_SEL(x)                                              \
	(((x) & BIT_MASK_PS_TIMER_A_TSF_SEL) << BIT_SHIFT_PS_TIMER_A_TSF_SEL)
#define BIT_GET_PS_TIMER_A_TSF_SEL(x)                                          \
	(((x) >> BIT_SHIFT_PS_TIMER_A_TSF_SEL) & BIT_MASK_PS_TIMER_A_TSF_SEL)

/* 2 REG_CPUMGQ_TX_TIMER_EARLY		(Offset 0x1514) */

#define BIT_SHIFT_CPUMGQ_TX_TIMER_EARLY 0
#define BIT_MASK_CPUMGQ_TX_TIMER_EARLY 0xff
#define BIT_CPUMGQ_TX_TIMER_EARLY(x)                                           \
	(((x) & BIT_MASK_CPUMGQ_TX_TIMER_EARLY)                                \
	 << BIT_SHIFT_CPUMGQ_TX_TIMER_EARLY)
#define BIT_GET_CPUMGQ_TX_TIMER_EARLY(x)                                       \
	(((x) >> BIT_SHIFT_CPUMGQ_TX_TIMER_EARLY) &                            \
	 BIT_MASK_CPUMGQ_TX_TIMER_EARLY)

/* 2 REG_PS_TIMER_A_EARLY			(Offset 0x1515) */

#define BIT_SHIFT_PS_TIMER_A_EARLY 0
#define BIT_MASK_PS_TIMER_A_EARLY 0xff
#define BIT_PS_TIMER_A_EARLY(x)                                                \
	(((x) & BIT_MASK_PS_TIMER_A_EARLY) << BIT_SHIFT_PS_TIMER_A_EARLY)
#define BIT_GET_PS_TIMER_A_EARLY(x)                                            \
	(((x) >> BIT_SHIFT_PS_TIMER_A_EARLY) & BIT_MASK_PS_TIMER_A_EARLY)

/* 2 REG_PS_TIMER_B_EARLY			(Offset 0x1516) */

#define BIT_SHIFT_PS_TIMER_B_EARLY 0
#define BIT_MASK_PS_TIMER_B_EARLY 0xff
#define BIT_PS_TIMER_B_EARLY(x)                                                \
	(((x) & BIT_MASK_PS_TIMER_B_EARLY) << BIT_SHIFT_PS_TIMER_B_EARLY)
#define BIT_GET_PS_TIMER_B_EARLY(x)                                            \
	(((x) >> BIT_SHIFT_PS_TIMER_B_EARLY) & BIT_MASK_PS_TIMER_B_EARLY)

/* 2 REG_PS_TIMER_C_EARLY			(Offset 0x1517) */

#define BIT_SHIFT_PS_TIMER_C_EARLY 0
#define BIT_MASK_PS_TIMER_C_EARLY 0xff
#define BIT_PS_TIMER_C_EARLY(x)                                                \
	(((x) & BIT_MASK_PS_TIMER_C_EARLY) << BIT_SHIFT_PS_TIMER_C_EARLY)
#define BIT_GET_PS_TIMER_C_EARLY(x)                                            \
	(((x) >> BIT_SHIFT_PS_TIMER_C_EARLY) & BIT_MASK_PS_TIMER_C_EARLY)

/* 2 REG_BCN_PSR_RPT2			(Offset 0x1600) */

#define BIT_SHIFT_DTIM_CNT2 24
#define BIT_MASK_DTIM_CNT2 0xff
#define BIT_DTIM_CNT2(x) (((x) & BIT_MASK_DTIM_CNT2) << BIT_SHIFT_DTIM_CNT2)
#define BIT_GET_DTIM_CNT2(x) (((x) >> BIT_SHIFT_DTIM_CNT2) & BIT_MASK_DTIM_CNT2)

#define BIT_SHIFT_DTIM_PERIOD2 16
#define BIT_MASK_DTIM_PERIOD2 0xff
#define BIT_DTIM_PERIOD2(x)                                                    \
	(((x) & BIT_MASK_DTIM_PERIOD2) << BIT_SHIFT_DTIM_PERIOD2)
#define BIT_GET_DTIM_PERIOD2(x)                                                \
	(((x) >> BIT_SHIFT_DTIM_PERIOD2) & BIT_MASK_DTIM_PERIOD2)

#define BIT_DTIM2 BIT(15)
#define BIT_TIM2 BIT(14)

#define BIT_SHIFT_PS_AID_2 0
#define BIT_MASK_PS_AID_2 0x7ff
#define BIT_PS_AID_2(x) (((x) & BIT_MASK_PS_AID_2) << BIT_SHIFT_PS_AID_2)
#define BIT_GET_PS_AID_2(x) (((x) >> BIT_SHIFT_PS_AID_2) & BIT_MASK_PS_AID_2)

/* 2 REG_BCN_PSR_RPT3			(Offset 0x1604) */

#define BIT_SHIFT_DTIM_CNT3 24
#define BIT_MASK_DTIM_CNT3 0xff
#define BIT_DTIM_CNT3(x) (((x) & BIT_MASK_DTIM_CNT3) << BIT_SHIFT_DTIM_CNT3)
#define BIT_GET_DTIM_CNT3(x) (((x) >> BIT_SHIFT_DTIM_CNT3) & BIT_MASK_DTIM_CNT3)

#define BIT_SHIFT_DTIM_PERIOD3 16
#define BIT_MASK_DTIM_PERIOD3 0xff
#define BIT_DTIM_PERIOD3(x)                                                    \
	(((x) & BIT_MASK_DTIM_PERIOD3) << BIT_SHIFT_DTIM_PERIOD3)
#define BIT_GET_DTIM_PERIOD3(x)                                                \
	(((x) >> BIT_SHIFT_DTIM_PERIOD3) & BIT_MASK_DTIM_PERIOD3)

#define BIT_DTIM3 BIT(15)
#define BIT_TIM3 BIT(14)

#define BIT_SHIFT_PS_AID_3 0
#define BIT_MASK_PS_AID_3 0x7ff
#define BIT_PS_AID_3(x) (((x) & BIT_MASK_PS_AID_3) << BIT_SHIFT_PS_AID_3)
#define BIT_GET_PS_AID_3(x) (((x) >> BIT_SHIFT_PS_AID_3) & BIT_MASK_PS_AID_3)

/* 2 REG_BCN_PSR_RPT4			(Offset 0x1608) */

#define BIT_SHIFT_DTIM_CNT4 24
#define BIT_MASK_DTIM_CNT4 0xff
#define BIT_DTIM_CNT4(x) (((x) & BIT_MASK_DTIM_CNT4) << BIT_SHIFT_DTIM_CNT4)
#define BIT_GET_DTIM_CNT4(x) (((x) >> BIT_SHIFT_DTIM_CNT4) & BIT_MASK_DTIM_CNT4)

#define BIT_SHIFT_DTIM_PERIOD4 16
#define BIT_MASK_DTIM_PERIOD4 0xff
#define BIT_DTIM_PERIOD4(x)                                                    \
	(((x) & BIT_MASK_DTIM_PERIOD4) << BIT_SHIFT_DTIM_PERIOD4)
#define BIT_GET_DTIM_PERIOD4(x)                                                \
	(((x) >> BIT_SHIFT_DTIM_PERIOD4) & BIT_MASK_DTIM_PERIOD4)

#define BIT_DTIM4 BIT(15)
#define BIT_TIM4 BIT(14)

#define BIT_SHIFT_PS_AID_4 0
#define BIT_MASK_PS_AID_4 0x7ff
#define BIT_PS_AID_4(x) (((x) & BIT_MASK_PS_AID_4) << BIT_SHIFT_PS_AID_4)
#define BIT_GET_PS_AID_4(x) (((x) >> BIT_SHIFT_PS_AID_4) & BIT_MASK_PS_AID_4)

/* 2 REG_A1_ADDR_MASK			(Offset 0x160C) */

#define BIT_SHIFT_A1_ADDR_MASK 0
#define BIT_MASK_A1_ADDR_MASK 0xffffffffL
#define BIT_A1_ADDR_MASK(x)                                                    \
	(((x) & BIT_MASK_A1_ADDR_MASK) << BIT_SHIFT_A1_ADDR_MASK)
#define BIT_GET_A1_ADDR_MASK(x)                                                \
	(((x) >> BIT_SHIFT_A1_ADDR_MASK) & BIT_MASK_A1_ADDR_MASK)

/* 2 REG_MACID2				(Offset 0x1620) */

#define BIT_SHIFT_MACID2 0
#define BIT_MASK_MACID2 0xffffffffffffL
#define BIT_MACID2(x) (((x) & BIT_MASK_MACID2) << BIT_SHIFT_MACID2)
#define BIT_GET_MACID2(x) (((x) >> BIT_SHIFT_MACID2) & BIT_MASK_MACID2)

/* 2 REG_BSSID2				(Offset 0x1628) */

#define BIT_SHIFT_BSSID2 0
#define BIT_MASK_BSSID2 0xffffffffffffL
#define BIT_BSSID2(x) (((x) & BIT_MASK_BSSID2) << BIT_SHIFT_BSSID2)
#define BIT_GET_BSSID2(x) (((x) >> BIT_SHIFT_BSSID2) & BIT_MASK_BSSID2)

/* 2 REG_MACID3				(Offset 0x1630) */

#define BIT_SHIFT_MACID3 0
#define BIT_MASK_MACID3 0xffffffffffffL
#define BIT_MACID3(x) (((x) & BIT_MASK_MACID3) << BIT_SHIFT_MACID3)
#define BIT_GET_MACID3(x) (((x) >> BIT_SHIFT_MACID3) & BIT_MASK_MACID3)

/* 2 REG_BSSID3				(Offset 0x1638) */

#define BIT_SHIFT_BSSID3 0
#define BIT_MASK_BSSID3 0xffffffffffffL
#define BIT_BSSID3(x) (((x) & BIT_MASK_BSSID3) << BIT_SHIFT_BSSID3)
#define BIT_GET_BSSID3(x) (((x) >> BIT_SHIFT_BSSID3) & BIT_MASK_BSSID3)

/* 2 REG_MACID4				(Offset 0x1640) */

#define BIT_SHIFT_MACID4 0
#define BIT_MASK_MACID4 0xffffffffffffL
#define BIT_MACID4(x) (((x) & BIT_MASK_MACID4) << BIT_SHIFT_MACID4)
#define BIT_GET_MACID4(x) (((x) >> BIT_SHIFT_MACID4) & BIT_MASK_MACID4)

/* 2 REG_BSSID4				(Offset 0x1648) */

#define BIT_SHIFT_BSSID4 0
#define BIT_MASK_BSSID4 0xffffffffffffL
#define BIT_BSSID4(x) (((x) & BIT_MASK_BSSID4) << BIT_SHIFT_BSSID4)
#define BIT_GET_BSSID4(x) (((x) >> BIT_SHIFT_BSSID4) & BIT_MASK_BSSID4)

/* 2 REG_PWRBIT_SETTING			(Offset 0x1660) */

#define BIT_CLI3_PWRBIT_OW_EN BIT(7)
#define BIT_CLI3_PWR_ST BIT(6)
#define BIT_CLI2_PWRBIT_OW_EN BIT(5)
#define BIT_CLI2_PWR_ST BIT(4)
#define BIT_CLI1_PWRBIT_OW_EN BIT(3)
#define BIT_CLI1_PWR_ST BIT(2)
#define BIT_CLI0_PWRBIT_OW_EN BIT(1)
#define BIT_CLI0_PWR_ST BIT(0)

/* 2 REG_WMAC_MU_BF_OPTION			(Offset 0x167C) */

#define BIT_WMAC_RESP_NONSTA1_DIS BIT(7)

/* 2 REG_WMAC_MU_BF_OPTION			(Offset 0x167C) */

#define BIT_BIT_WMAC_TXMU_ACKPOLICY_EN BIT(6)

/* 2 REG_WMAC_MU_BF_OPTION			(Offset 0x167C) */

#define BIT_SHIFT_WMAC_TXMU_ACKPOLICY 4
#define BIT_MASK_WMAC_TXMU_ACKPOLICY 0x3
#define BIT_WMAC_TXMU_ACKPOLICY(x)                                             \
	(((x) & BIT_MASK_WMAC_TXMU_ACKPOLICY) << BIT_SHIFT_WMAC_TXMU_ACKPOLICY)
#define BIT_GET_WMAC_TXMU_ACKPOLICY(x)                                         \
	(((x) >> BIT_SHIFT_WMAC_TXMU_ACKPOLICY) & BIT_MASK_WMAC_TXMU_ACKPOLICY)

#define BIT_SHIFT_WMAC_MU_BFEE_PORT_SEL 1
#define BIT_MASK_WMAC_MU_BFEE_PORT_SEL 0x7
#define BIT_WMAC_MU_BFEE_PORT_SEL(x)                                           \
	(((x) & BIT_MASK_WMAC_MU_BFEE_PORT_SEL)                                \
	 << BIT_SHIFT_WMAC_MU_BFEE_PORT_SEL)
#define BIT_GET_WMAC_MU_BFEE_PORT_SEL(x)                                       \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE_PORT_SEL) &                            \
	 BIT_MASK_WMAC_MU_BFEE_PORT_SEL)

#define BIT_WMAC_MU_BFEE_DIS BIT(0)

/* 2 REG_WMAC_PAUSE_BB_CLR_TH		(Offset 0x167D) */

#define BIT_SHIFT_WMAC_PAUSE_BB_CLR_TH 0
#define BIT_MASK_WMAC_PAUSE_BB_CLR_TH 0xff
#define BIT_WMAC_PAUSE_BB_CLR_TH(x)                                            \
	(((x) & BIT_MASK_WMAC_PAUSE_BB_CLR_TH)                                 \
	 << BIT_SHIFT_WMAC_PAUSE_BB_CLR_TH)
#define BIT_GET_WMAC_PAUSE_BB_CLR_TH(x)                                        \
	(((x) >> BIT_SHIFT_WMAC_PAUSE_BB_CLR_TH) &                             \
	 BIT_MASK_WMAC_PAUSE_BB_CLR_TH)

/* 2 REG_WMAC_MU_ARB				(Offset 0x167E) */

#define BIT_WMAC_ARB_HW_ADAPT_EN BIT(7)
#define BIT_WMAC_ARB_SW_EN BIT(6)

#define BIT_SHIFT_WMAC_ARB_SW_STATE 0
#define BIT_MASK_WMAC_ARB_SW_STATE 0x3f
#define BIT_WMAC_ARB_SW_STATE(x)                                               \
	(((x) & BIT_MASK_WMAC_ARB_SW_STATE) << BIT_SHIFT_WMAC_ARB_SW_STATE)
#define BIT_GET_WMAC_ARB_SW_STATE(x)                                           \
	(((x) >> BIT_SHIFT_WMAC_ARB_SW_STATE) & BIT_MASK_WMAC_ARB_SW_STATE)

/* 2 REG_WMAC_MU_OPTION			(Offset 0x167F) */

#define BIT_SHIFT_WMAC_MU_DBGSEL 5
#define BIT_MASK_WMAC_MU_DBGSEL 0x3
#define BIT_WMAC_MU_DBGSEL(x)                                                  \
	(((x) & BIT_MASK_WMAC_MU_DBGSEL) << BIT_SHIFT_WMAC_MU_DBGSEL)
#define BIT_GET_WMAC_MU_DBGSEL(x)                                              \
	(((x) >> BIT_SHIFT_WMAC_MU_DBGSEL) & BIT_MASK_WMAC_MU_DBGSEL)

#define BIT_SHIFT_WMAC_MU_CPRD_TIMEOUT 0
#define BIT_MASK_WMAC_MU_CPRD_TIMEOUT 0x1f
#define BIT_WMAC_MU_CPRD_TIMEOUT(x)                                            \
	(((x) & BIT_MASK_WMAC_MU_CPRD_TIMEOUT)                                 \
	 << BIT_SHIFT_WMAC_MU_CPRD_TIMEOUT)
#define BIT_GET_WMAC_MU_CPRD_TIMEOUT(x)                                        \
	(((x) >> BIT_SHIFT_WMAC_MU_CPRD_TIMEOUT) &                             \
	 BIT_MASK_WMAC_MU_CPRD_TIMEOUT)

/* 2 REG_WMAC_MU_BF_CTL			(Offset 0x1680) */

#define BIT_WMAC_INVLD_BFPRT_CHK BIT(15)
#define BIT_WMAC_RETXBFRPTSEQ_UPD BIT(14)

#define BIT_SHIFT_WMAC_MU_BFRPTSEG_SEL 12
#define BIT_MASK_WMAC_MU_BFRPTSEG_SEL 0x3
#define BIT_WMAC_MU_BFRPTSEG_SEL(x)                                            \
	(((x) & BIT_MASK_WMAC_MU_BFRPTSEG_SEL)                                 \
	 << BIT_SHIFT_WMAC_MU_BFRPTSEG_SEL)
#define BIT_GET_WMAC_MU_BFRPTSEG_SEL(x)                                        \
	(((x) >> BIT_SHIFT_WMAC_MU_BFRPTSEG_SEL) &                             \
	 BIT_MASK_WMAC_MU_BFRPTSEG_SEL)

#define BIT_SHIFT_WMAC_MU_BF_MYAID 0
#define BIT_MASK_WMAC_MU_BF_MYAID 0xfff
#define BIT_WMAC_MU_BF_MYAID(x)                                                \
	(((x) & BIT_MASK_WMAC_MU_BF_MYAID) << BIT_SHIFT_WMAC_MU_BF_MYAID)
#define BIT_GET_WMAC_MU_BF_MYAID(x)                                            \
	(((x) >> BIT_SHIFT_WMAC_MU_BF_MYAID) & BIT_MASK_WMAC_MU_BF_MYAID)

#define BIT_SHIFT_BFRPT_PARA 0
#define BIT_MASK_BFRPT_PARA 0xfff
#define BIT_BFRPT_PARA(x) (((x) & BIT_MASK_BFRPT_PARA) << BIT_SHIFT_BFRPT_PARA)
#define BIT_GET_BFRPT_PARA(x)                                                  \
	(((x) >> BIT_SHIFT_BFRPT_PARA) & BIT_MASK_BFRPT_PARA)

/* 2 REG_WMAC_MU_BFRPT_PARA			(Offset 0x1682) */

#define BIT_SHIFT_BIT_BFRPT_PARA_USERID_SEL 12
#define BIT_MASK_BIT_BFRPT_PARA_USERID_SEL 0x7
#define BIT_BIT_BFRPT_PARA_USERID_SEL(x)                                       \
	(((x) & BIT_MASK_BIT_BFRPT_PARA_USERID_SEL)                            \
	 << BIT_SHIFT_BIT_BFRPT_PARA_USERID_SEL)
#define BIT_GET_BIT_BFRPT_PARA_USERID_SEL(x)                                   \
	(((x) >> BIT_SHIFT_BIT_BFRPT_PARA_USERID_SEL) &                        \
	 BIT_MASK_BIT_BFRPT_PARA_USERID_SEL)

/* 2 REG_WMAC_ASSOCIATED_MU_BFMEE2		(Offset 0x1684) */

#define BIT_STATUS_BFEE2 BIT(10)
#define BIT_WMAC_MU_BFEE2_EN BIT(9)

#define BIT_SHIFT_WMAC_MU_BFEE2_AID 0
#define BIT_MASK_WMAC_MU_BFEE2_AID 0x1ff
#define BIT_WMAC_MU_BFEE2_AID(x)                                               \
	(((x) & BIT_MASK_WMAC_MU_BFEE2_AID) << BIT_SHIFT_WMAC_MU_BFEE2_AID)
#define BIT_GET_WMAC_MU_BFEE2_AID(x)                                           \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE2_AID) & BIT_MASK_WMAC_MU_BFEE2_AID)

/* 2 REG_WMAC_ASSOCIATED_MU_BFMEE3		(Offset 0x1686) */

#define BIT_STATUS_BFEE3 BIT(10)
#define BIT_WMAC_MU_BFEE3_EN BIT(9)

#define BIT_SHIFT_WMAC_MU_BFEE3_AID 0
#define BIT_MASK_WMAC_MU_BFEE3_AID 0x1ff
#define BIT_WMAC_MU_BFEE3_AID(x)                                               \
	(((x) & BIT_MASK_WMAC_MU_BFEE3_AID) << BIT_SHIFT_WMAC_MU_BFEE3_AID)
#define BIT_GET_WMAC_MU_BFEE3_AID(x)                                           \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE3_AID) & BIT_MASK_WMAC_MU_BFEE3_AID)

/* 2 REG_WMAC_ASSOCIATED_MU_BFMEE4		(Offset 0x1688) */

#define BIT_STATUS_BFEE4 BIT(10)
#define BIT_WMAC_MU_BFEE4_EN BIT(9)

#define BIT_SHIFT_WMAC_MU_BFEE4_AID 0
#define BIT_MASK_WMAC_MU_BFEE4_AID 0x1ff
#define BIT_WMAC_MU_BFEE4_AID(x)                                               \
	(((x) & BIT_MASK_WMAC_MU_BFEE4_AID) << BIT_SHIFT_WMAC_MU_BFEE4_AID)
#define BIT_GET_WMAC_MU_BFEE4_AID(x)                                           \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE4_AID) & BIT_MASK_WMAC_MU_BFEE4_AID)

/* 2 REG_WMAC_ASSOCIATED_MU_BFMEE5		(Offset 0x168A) */

#define BIT_R_WMAC_RX_SYNCFIFO_SYNC BIT(55)
#define BIT_R_WMAC_RXRST_DLY BIT(54)
#define BIT_R_WMAC_SRCH_TXRPT_REF_DROP BIT(53)
#define BIT_R_WMAC_SRCH_TXRPT_UA1 BIT(52)
#define BIT_STATUS_BFEE5 BIT(10)

/* 2 REG_WMAC_ASSOCIATED_MU_BFMEE5		(Offset 0x168A) */

#define BIT_WMAC_MU_BFEE5_EN BIT(9)

#define BIT_SHIFT_WMAC_MU_BFEE5_AID 0
#define BIT_MASK_WMAC_MU_BFEE5_AID 0x1ff
#define BIT_WMAC_MU_BFEE5_AID(x)                                               \
	(((x) & BIT_MASK_WMAC_MU_BFEE5_AID) << BIT_SHIFT_WMAC_MU_BFEE5_AID)
#define BIT_GET_WMAC_MU_BFEE5_AID(x)                                           \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE5_AID) & BIT_MASK_WMAC_MU_BFEE5_AID)

/* 2 REG_WMAC_ASSOCIATED_MU_BFMEE6		(Offset 0x168C) */

#define BIT_STATUS_BFEE6 BIT(10)
#define BIT_WMAC_MU_BFEE6_EN BIT(9)

#define BIT_SHIFT_WMAC_MU_BFEE6_AID 0
#define BIT_MASK_WMAC_MU_BFEE6_AID 0x1ff
#define BIT_WMAC_MU_BFEE6_AID(x)                                               \
	(((x) & BIT_MASK_WMAC_MU_BFEE6_AID) << BIT_SHIFT_WMAC_MU_BFEE6_AID)
#define BIT_GET_WMAC_MU_BFEE6_AID(x)                                           \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE6_AID) & BIT_MASK_WMAC_MU_BFEE6_AID)

/* 2 REG_WMAC_ASSOCIATED_MU_BFMEE7		(Offset 0x168E) */

#define BIT_BIT_STATUS_BFEE4 BIT(10)
#define BIT_WMAC_MU_BFEE7_EN BIT(9)

#define BIT_SHIFT_WMAC_MU_BFEE7_AID 0
#define BIT_MASK_WMAC_MU_BFEE7_AID 0x1ff
#define BIT_WMAC_MU_BFEE7_AID(x)                                               \
	(((x) & BIT_MASK_WMAC_MU_BFEE7_AID) << BIT_SHIFT_WMAC_MU_BFEE7_AID)
#define BIT_GET_WMAC_MU_BFEE7_AID(x)                                           \
	(((x) >> BIT_SHIFT_WMAC_MU_BFEE7_AID) & BIT_MASK_WMAC_MU_BFEE7_AID)

/* 2 REG_WMAC_BB_STOP_RX_COUNTER		(Offset 0x1690) */

#define BIT_RST_ALL_COUNTER BIT(31)

#define BIT_SHIFT_ABORT_RX_VBON_COUNTER 16
#define BIT_MASK_ABORT_RX_VBON_COUNTER 0xff
#define BIT_ABORT_RX_VBON_COUNTER(x)                                           \
	(((x) & BIT_MASK_ABORT_RX_VBON_COUNTER)                                \
	 << BIT_SHIFT_ABORT_RX_VBON_COUNTER)
#define BIT_GET_ABORT_RX_VBON_COUNTER(x)                                       \
	(((x) >> BIT_SHIFT_ABORT_RX_VBON_COUNTER) &                            \
	 BIT_MASK_ABORT_RX_VBON_COUNTER)

#define BIT_SHIFT_ABORT_RX_RDRDY_COUNTER 8
#define BIT_MASK_ABORT_RX_RDRDY_COUNTER 0xff
#define BIT_ABORT_RX_RDRDY_COUNTER(x)                                          \
	(((x) & BIT_MASK_ABORT_RX_RDRDY_COUNTER)                               \
	 << BIT_SHIFT_ABORT_RX_RDRDY_COUNTER)
#define BIT_GET_ABORT_RX_RDRDY_COUNTER(x)                                      \
	(((x) >> BIT_SHIFT_ABORT_RX_RDRDY_COUNTER) &                           \
	 BIT_MASK_ABORT_RX_RDRDY_COUNTER)

#define BIT_SHIFT_VBON_EARLY_FALLING_COUNTER 0
#define BIT_MASK_VBON_EARLY_FALLING_COUNTER 0xff
#define BIT_VBON_EARLY_FALLING_COUNTER(x)                                      \
	(((x) & BIT_MASK_VBON_EARLY_FALLING_COUNTER)                           \
	 << BIT_SHIFT_VBON_EARLY_FALLING_COUNTER)
#define BIT_GET_VBON_EARLY_FALLING_COUNTER(x)                                  \
	(((x) >> BIT_SHIFT_VBON_EARLY_FALLING_COUNTER) &                       \
	 BIT_MASK_VBON_EARLY_FALLING_COUNTER)

/* 2 REG_WMAC_PLCP_MONITOR			(Offset 0x1694) */

#define BIT_WMAC_PLCP_TRX_SEL BIT(31)

#define BIT_SHIFT_WMAC_PLCP_RDSIG_SEL 28
#define BIT_MASK_WMAC_PLCP_RDSIG_SEL 0x7
#define BIT_WMAC_PLCP_RDSIG_SEL(x)                                             \
	(((x) & BIT_MASK_WMAC_PLCP_RDSIG_SEL) << BIT_SHIFT_WMAC_PLCP_RDSIG_SEL)
#define BIT_GET_WMAC_PLCP_RDSIG_SEL(x)                                         \
	(((x) >> BIT_SHIFT_WMAC_PLCP_RDSIG_SEL) & BIT_MASK_WMAC_PLCP_RDSIG_SEL)

#define BIT_SHIFT_WMAC_RATE_IDX 24
#define BIT_MASK_WMAC_RATE_IDX 0xf
#define BIT_WMAC_RATE_IDX(x)                                                   \
	(((x) & BIT_MASK_WMAC_RATE_IDX) << BIT_SHIFT_WMAC_RATE_IDX)
#define BIT_GET_WMAC_RATE_IDX(x)                                               \
	(((x) >> BIT_SHIFT_WMAC_RATE_IDX) & BIT_MASK_WMAC_RATE_IDX)

#define BIT_SHIFT_WMAC_PLCP_RDSIG 0
#define BIT_MASK_WMAC_PLCP_RDSIG 0xffffff
#define BIT_WMAC_PLCP_RDSIG(x)                                                 \
	(((x) & BIT_MASK_WMAC_PLCP_RDSIG) << BIT_SHIFT_WMAC_PLCP_RDSIG)
#define BIT_GET_WMAC_PLCP_RDSIG(x)                                             \
	(((x) >> BIT_SHIFT_WMAC_PLCP_RDSIG) & BIT_MASK_WMAC_PLCP_RDSIG)

/* 2 REG_WMAC_PLCP_MONITOR_MUTX		(Offset 0x1698) */

#define BIT_WMAC_MUTX_IDX BIT(24)

/* 2 REG_TRANSMIT_ADDRSS_0			(Offset 0x16A0) */

#define BIT_SHIFT_TA0 0
#define BIT_MASK_TA0 0xffffffffffffL
#define BIT_TA0(x) (((x) & BIT_MASK_TA0) << BIT_SHIFT_TA0)
#define BIT_GET_TA0(x) (((x) >> BIT_SHIFT_TA0) & BIT_MASK_TA0)

/* 2 REG_TRANSMIT_ADDRSS_1			(Offset 0x16A8) */

#define BIT_SHIFT_TA1 0
#define BIT_MASK_TA1 0xffffffffffffL
#define BIT_TA1(x) (((x) & BIT_MASK_TA1) << BIT_SHIFT_TA1)
#define BIT_GET_TA1(x) (((x) >> BIT_SHIFT_TA1) & BIT_MASK_TA1)

/* 2 REG_TRANSMIT_ADDRSS_2			(Offset 0x16B0) */

#define BIT_SHIFT_TA2 0
#define BIT_MASK_TA2 0xffffffffffffL
#define BIT_TA2(x) (((x) & BIT_MASK_TA2) << BIT_SHIFT_TA2)
#define BIT_GET_TA2(x) (((x) >> BIT_SHIFT_TA2) & BIT_MASK_TA2)

/* 2 REG_TRANSMIT_ADDRSS_3			(Offset 0x16B8) */

#define BIT_SHIFT_TA3 0
#define BIT_MASK_TA3 0xffffffffffffL
#define BIT_TA3(x) (((x) & BIT_MASK_TA3) << BIT_SHIFT_TA3)
#define BIT_GET_TA3(x) (((x) >> BIT_SHIFT_TA3) & BIT_MASK_TA3)

/* 2 REG_TRANSMIT_ADDRSS_4			(Offset 0x16C0) */

#define BIT_SHIFT_TA4 0
#define BIT_MASK_TA4 0xffffffffffffL
#define BIT_TA4(x) (((x) & BIT_MASK_TA4) << BIT_SHIFT_TA4)
#define BIT_GET_TA4(x) (((x) >> BIT_SHIFT_TA4) & BIT_MASK_TA4)

/* 2 REG_WL2LTECOEX_INDIRECT_ACCESS_CTRL_V1 (Offset 0x1700) */

#define BIT_LTECOEX_ACCESS_START_V1 BIT(31)
#define BIT_LTECOEX_WRITE_MODE_V1 BIT(30)
#define BIT_LTECOEX_READY_BIT_V1 BIT(29)

#define BIT_SHIFT_WRITE_BYTE_EN_V1 16
#define BIT_MASK_WRITE_BYTE_EN_V1 0xf
#define BIT_WRITE_BYTE_EN_V1(x)                                                \
	(((x) & BIT_MASK_WRITE_BYTE_EN_V1) << BIT_SHIFT_WRITE_BYTE_EN_V1)
#define BIT_GET_WRITE_BYTE_EN_V1(x)                                            \
	(((x) >> BIT_SHIFT_WRITE_BYTE_EN_V1) & BIT_MASK_WRITE_BYTE_EN_V1)

#define BIT_SHIFT_LTECOEX_REG_ADDR_V1 0
#define BIT_MASK_LTECOEX_REG_ADDR_V1 0xffff
#define BIT_LTECOEX_REG_ADDR_V1(x)                                             \
	(((x) & BIT_MASK_LTECOEX_REG_ADDR_V1) << BIT_SHIFT_LTECOEX_REG_ADDR_V1)
#define BIT_GET_LTECOEX_REG_ADDR_V1(x)                                         \
	(((x) >> BIT_SHIFT_LTECOEX_REG_ADDR_V1) & BIT_MASK_LTECOEX_REG_ADDR_V1)

/* 2 REG_WL2LTECOEX_INDIRECT_ACCESS_WRITE_DATA_V1 (Offset 0x1704) */

#define BIT_SHIFT_LTECOEX_W_DATA_V1 0
#define BIT_MASK_LTECOEX_W_DATA_V1 0xffffffffL
#define BIT_LTECOEX_W_DATA_V1(x)                                               \
	(((x) & BIT_MASK_LTECOEX_W_DATA_V1) << BIT_SHIFT_LTECOEX_W_DATA_V1)
#define BIT_GET_LTECOEX_W_DATA_V1(x)                                           \
	(((x) >> BIT_SHIFT_LTECOEX_W_DATA_V1) & BIT_MASK_LTECOEX_W_DATA_V1)

/* 2 REG_WL2LTECOEX_INDIRECT_ACCESS_READ_DATA_V1 (Offset 0x1708) */

#define BIT_SHIFT_LTECOEX_R_DATA_V1 0
#define BIT_MASK_LTECOEX_R_DATA_V1 0xffffffffL
#define BIT_LTECOEX_R_DATA_V1(x)                                               \
	(((x) & BIT_MASK_LTECOEX_R_DATA_V1) << BIT_SHIFT_LTECOEX_R_DATA_V1)
#define BIT_GET_LTECOEX_R_DATA_V1(x)                                           \
	(((x) >> BIT_SHIFT_LTECOEX_R_DATA_V1) & BIT_MASK_LTECOEX_R_DATA_V1)

#endif /* __RTL_WLAN_BITDEF_H__ */
