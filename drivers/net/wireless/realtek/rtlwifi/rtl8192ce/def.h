/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __RTL92C_DEF_H__
#define __RTL92C_DEF_H__

#define	PHY_RSSI_SLID_WIN_MAX				100
#define	PHY_LINKQUALITY_SLID_WIN_MAX			20
#define	PHY_BEACON_RSSI_SLID_WIN_MAX			10

#define RX_SMOOTH_FACTOR				20

#define HAL_PRIME_CHNL_OFFSET_DONT_CARE			0
#define HAL_PRIME_CHNL_OFFSET_LOWER			1
#define HAL_PRIME_CHNL_OFFSET_UPPER			2

#define RX_MPDU_QUEUE					0
#define RX_CMD_QUEUE					1

#define	C2H_RX_CMD_HDR_LEN				8
#define	GET_C2H_CMD_CMD_LEN(__prxhdr)		\
	LE_BITS_TO_4BYTE((__prxhdr), 0, 16)
#define	GET_C2H_CMD_ELEMENT_ID(__prxhdr)	\
	LE_BITS_TO_4BYTE((__prxhdr), 16, 8)
#define	GET_C2H_CMD_CMD_SEQ(__prxhdr)		\
	LE_BITS_TO_4BYTE((__prxhdr), 24, 7)
#define	GET_C2H_CMD_CONTINUE(__prxhdr)		\
	LE_BITS_TO_4BYTE((__prxhdr), 31, 1)
#define	GET_C2H_CMD_CONTENT(__prxhdr)		\
	((u8 *)(__prxhdr) + C2H_RX_CMD_HDR_LEN)

#define	GET_C2H_CMD_FEEDBACK_ELEMENT_ID(__pcmdfbhdr)	\
	LE_BITS_TO_4BYTE((__pcmdfbhdr), 0, 8)
#define	GET_C2H_CMD_FEEDBACK_CCX_LEN(__pcmdfbhdr)	\
	LE_BITS_TO_4BYTE((__pcmdfbhdr), 8, 8)
#define	GET_C2H_CMD_FEEDBACK_CCX_CMD_CNT(__pcmdfbhdr)	\
	LE_BITS_TO_4BYTE((__pcmdfbhdr), 16, 16)
#define	GET_C2H_CMD_FEEDBACK_CCX_MAC_ID(__pcmdfbhdr)	\
	LE_BITS_TO_4BYTE(((__pcmdfbhdr) + 4), 0, 5)
#define	GET_C2H_CMD_FEEDBACK_CCX_VALID(__pcmdfbhdr)	\
	LE_BITS_TO_4BYTE(((__pcmdfbhdr) + 4), 7, 1)
#define	GET_C2H_CMD_FEEDBACK_CCX_RETRY_CNT(__pcmdfbhdr)	\
	LE_BITS_TO_4BYTE(((__pcmdfbhdr) + 4), 8, 5)
#define	GET_C2H_CMD_FEEDBACK_CCX_TOK(__pcmdfbhdr)	\
	LE_BITS_TO_4BYTE(((__pcmdfbhdr) + 4), 15, 1)
#define	GET_C2H_CMD_FEEDBACK_CCX_QSEL(__pcmdfbhdr)	\
	LE_BITS_TO_4BYTE(((__pcmdfbhdr) + 4), 16, 4)
#define	GET_C2H_CMD_FEEDBACK_CCX_SEQ(__pcmdfbhdr)	\
	LE_BITS_TO_4BYTE(((__pcmdfbhdr) + 4), 20, 12)
#define GET_RX_STATUS_DESC_BUFF_ADDR(__pdesc)			\
	SHIFT_AND_MASK_LE(__pdesc + 24, 0, 32)

#define CHIP_VER_B			BIT(4)
#define CHIP_BONDING_IDENTIFIER(_value) (((_value) >> 22) & 0x3)
#define CHIP_BONDING_92C_1T2R		0x1
#define RF_TYPE_1T2R			BIT(1)
#define CHIP_92C_BITMASK		BIT(0)
#define CHIP_UNKNOWN			BIT(7)
#define CHIP_92C_1T2R			0x03
#define CHIP_92C			0x01
#define CHIP_88C			0x00

enum version_8192c {
	VERSION_A_CHIP_92C = 0x01,
	VERSION_A_CHIP_88C = 0x00,
	VERSION_B_CHIP_92C = 0x11,
	VERSION_B_CHIP_88C = 0x10,
	VERSION_TEST_CHIP_88C = 0x00,
	VERSION_TEST_CHIP_92C = 0x01,
	VERSION_NORMAL_TSMC_CHIP_88C = 0x10,
	VERSION_NORMAL_TSMC_CHIP_92C = 0x11,
	VERSION_NORMAL_TSMC_CHIP_92C_1T2R = 0x13,
	VERSION_NORMAL_UMC_CHIP_88C_A_CUT = 0x30,
	VERSION_NORMAL_UMC_CHIP_92C_A_CUT = 0x31,
	VERSION_NORMAL_UMC_CHIP_92C_1T2R_A_CUT = 0x33,
	VERSION_NORMA_UMC_CHIP_8723_1T1R_A_CUT = 0x34,
	VERSION_NORMA_UMC_CHIP_8723_1T1R_B_CUT = 0x3c,
	VERSION_NORMAL_UMC_CHIP_88C_B_CUT = 0x70,
	VERSION_NORMAL_UMC_CHIP_92C_B_CUT = 0x71,
	VERSION_NORMAL_UMC_CHIP_92C_1T2R_B_CUT = 0x73,
	VERSION_UNKNOWN = 0x88,
};

enum rtl819x_loopback_e {
	RTL819X_NO_LOOPBACK = 0,
	RTL819X_MAC_LOOPBACK = 1,
	RTL819X_DMA_LOOPBACK = 2,
	RTL819X_CCK_LOOPBACK = 3,
};

enum rf_optype {
	RF_OP_BY_SW_3WIRE = 0,
	RF_OP_BY_FW,
	RF_OP_MAX
};

enum rf_power_state {
	RF_ON,
	RF_OFF,
	RF_SLEEP,
	RF_SHUT_DOWN,
};

enum power_save_mode {
	POWER_SAVE_MODE_ACTIVE,
	POWER_SAVE_MODE_SAVE,
};

enum power_polocy_config {
	POWERCFG_MAX_POWER_SAVINGS,
	POWERCFG_GLOBAL_POWER_SAVINGS,
	POWERCFG_LOCAL_POWER_SAVINGS,
	POWERCFG_LENOVO,
};

enum interface_select_pci {
	INTF_SEL1_MINICARD = 0,
	INTF_SEL0_PCIE = 1,
	INTF_SEL2_RSV = 2,
	INTF_SEL3_RSV = 3,
};

enum rtl_desc_qsel {
	QSLT_BK = 0x2,
	QSLT_BE = 0x0,
	QSLT_VI = 0x5,
	QSLT_VO = 0x7,
	QSLT_BEACON = 0x10,
	QSLT_HIGH = 0x11,
	QSLT_MGNT = 0x12,
	QSLT_CMD = 0x13,
};

struct phy_sts_cck_8192s_t {
	u8 adc_pwdb_X[4];
	u8 sq_rpt;
	u8 cck_agc_rpt;
};

struct h2c_cmd_8192c {
	u8 element_id;
	u32 cmd_len;
	u8 *p_cmdbuffer;
};

#endif
