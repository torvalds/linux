/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
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

#ifndef __RTL92D_DEF_H__
#define __RTL92D_DEF_H__

/* Min Spacing related settings. */
#define	MAX_MSS_DENSITY_2T				0x13
#define	MAX_MSS_DENSITY_1T				0x0A

#define RF6052_MAX_TX_PWR				0x3F
#define RF6052_MAX_REG					0x3F
#define RF6052_MAX_PATH					2

#define HAL_RETRY_LIMIT_INFRA				48
#define HAL_RETRY_LIMIT_AP_ADHOC			7

#define	PHY_RSSI_SLID_WIN_MAX				100
#define	PHY_LINKQUALITY_SLID_WIN_MAX			20
#define	PHY_BEACON_RSSI_SLID_WIN_MAX			10

#define RESET_DELAY_8185				20

#define RT_IBSS_INT_MASKS	(IMR_BCNINT | IMR_TBDOK | IMR_TBDER)
#define RT_AC_INT_MASKS		(IMR_VIDOK | IMR_VODOK | IMR_BEDOK|IMR_BKDOK)

#define NUM_OF_FIRMWARE_QUEUE				10
#define NUM_OF_PAGES_IN_FW				0x100
#define NUM_OF_PAGE_IN_FW_QUEUE_BK			0x07
#define NUM_OF_PAGE_IN_FW_QUEUE_BE			0x07
#define NUM_OF_PAGE_IN_FW_QUEUE_VI			0x07
#define NUM_OF_PAGE_IN_FW_QUEUE_VO			0x07
#define NUM_OF_PAGE_IN_FW_QUEUE_HCCA			0x0
#define NUM_OF_PAGE_IN_FW_QUEUE_CMD			0x0
#define NUM_OF_PAGE_IN_FW_QUEUE_MGNT			0x02
#define NUM_OF_PAGE_IN_FW_QUEUE_HIGH			0x02
#define NUM_OF_PAGE_IN_FW_QUEUE_BCN			0x2
#define NUM_OF_PAGE_IN_FW_QUEUE_PUB			0xA1

#define NUM_OF_PAGE_IN_FW_QUEUE_BK_DTM			0x026
#define NUM_OF_PAGE_IN_FW_QUEUE_BE_DTM			0x048
#define NUM_OF_PAGE_IN_FW_QUEUE_VI_DTM			0x048
#define NUM_OF_PAGE_IN_FW_QUEUE_VO_DTM			0x026
#define NUM_OF_PAGE_IN_FW_QUEUE_PUB_DTM			0x00

#define MAX_LINES_HWCONFIG_TXT				1000
#define MAX_BYTES_LINE_HWCONFIG_TXT			256

#define SW_THREE_WIRE					0
#define HW_THREE_WIRE					2

#define BT_DEMO_BOARD					0
#define BT_QA_BOARD					1
#define BT_FPGA						2

#define RX_SMOOTH_FACTOR				20

#define HAL_PRIME_CHNL_OFFSET_DONT_CARE			0
#define HAL_PRIME_CHNL_OFFSET_LOWER			1
#define HAL_PRIME_CHNL_OFFSET_UPPER			2

#define MAX_H2C_QUEUE_NUM				10

#define RX_MPDU_QUEUE					0
#define RX_CMD_QUEUE					1
#define RX_MAX_QUEUE					2

#define	C2H_RX_CMD_HDR_LEN				8
#define	GET_C2H_CMD_CMD_LEN(__prxhdr)			\
	LE_BITS_TO_4BYTE((__prxhdr), 0, 16)
#define	GET_C2H_CMD_ELEMENT_ID(__prxhdr)		\
	LE_BITS_TO_4BYTE((__prxhdr), 16, 8)
#define	GET_C2H_CMD_CMD_SEQ(__prxhdr)			\
	LE_BITS_TO_4BYTE((__prxhdr), 24, 7)
#define	GET_C2H_CMD_CONTINUE(__prxhdr)			\
	LE_BITS_TO_4BYTE((__prxhdr), 31, 1)
#define	GET_C2H_CMD_CONTENT(__prxhdr)			\
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

/*
 * 92D chip ver:
 * BIT8: IS 92D
 * BIT9: single phy
 * BIT10: C-cut
 * BIT11: D-cut
 */

/* Chip specific */
#define CHIP_92C			BIT(0)
#define CHIP_92C_1T2R			BIT(1)
#define CHIP_8723			BIT(2) /* RTL8723 With BT feature */
#define CHIP_8723_DRV_REV		BIT(3) /* RTL8723 Driver Revised */
#define NORMAL_CHIP			BIT(4)
#define CHIP_VENDOR_UMC			BIT(5)
#define CHIP_VENDOR_UMC_B_CUT		BIT(6) /* Chip version for ECO */

/* for 92D */
#define CHIP_92D			BIT(8)
#define CHIP_92D_SINGLEPHY		BIT(9)
#define CHIP_92D_C_CUT			BIT(10)
#define CHIP_92D_D_CUT			BIT(11)

enum version_8192d {
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
	VERSION_TEST_CHIP_92D_SINGLEPHY = 0x300,
	VERSION_TEST_CHIP_92D_DUALPHY = 0x100,
	VERSION_NORMAL_CHIP_92D_SINGLEPHY = 0x310,
	VERSION_NORMAL_CHIP_92D_DUALPHY = 0x110,
	VERSION_NORMAL_CHIP_92D_C_CUT_SINGLEPHY = 0x710,
	VERSION_NORMAL_CHIP_92D_C_CUT_DUALPHY = 0x510,
	VERSION_NORMAL_CHIP_92D_D_CUT_SINGLEPHY = 0xB10,
	VERSION_NORMAL_CHIP_92D_D_CUT_DUALPHY = 0x910,
};

#define IS_92D_SINGLEPHY(version)		\
	((version & CHIP_92D_SINGLEPHY) ? true : false)
#define IS_92D_C_CUT(version)			\
	((version & CHIP_92D_C_CUT) ? true : false)
#define IS_92D_D_CUT(version)			\
	((version & CHIP_92D_D_CUT) ? true : false)

enum rf_optype {
	RF_OP_BY_SW_3WIRE = 0,
	RF_OP_BY_FW,
	RF_OP_MAX
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

enum rtl_desc92d_rate {
	DESC92D_RATE1M = 0x00,
	DESC92D_RATE2M = 0x01,
	DESC92D_RATE5_5M = 0x02,
	DESC92D_RATE11M = 0x03,

	DESC92D_RATE6M = 0x04,
	DESC92D_RATE9M = 0x05,
	DESC92D_RATE12M = 0x06,
	DESC92D_RATE18M = 0x07,
	DESC92D_RATE24M = 0x08,
	DESC92D_RATE36M = 0x09,
	DESC92D_RATE48M = 0x0a,
	DESC92D_RATE54M = 0x0b,

	DESC92D_RATEMCS0 = 0x0c,
	DESC92D_RATEMCS1 = 0x0d,
	DESC92D_RATEMCS2 = 0x0e,
	DESC92D_RATEMCS3 = 0x0f,
	DESC92D_RATEMCS4 = 0x10,
	DESC92D_RATEMCS5 = 0x11,
	DESC92D_RATEMCS6 = 0x12,
	DESC92D_RATEMCS7 = 0x13,
	DESC92D_RATEMCS8 = 0x14,
	DESC92D_RATEMCS9 = 0x15,
	DESC92D_RATEMCS10 = 0x16,
	DESC92D_RATEMCS11 = 0x17,
	DESC92D_RATEMCS12 = 0x18,
	DESC92D_RATEMCS13 = 0x19,
	DESC92D_RATEMCS14 = 0x1a,
	DESC92D_RATEMCS15 = 0x1b,
	DESC92D_RATEMCS15_SG = 0x1c,
	DESC92D_RATEMCS32 = 0x20,
};

enum channel_plan {
	CHPL_FCC	= 0,
	CHPL_IC		= 1,
	CHPL_ETSI	= 2,
	CHPL_SPAIN	= 3,
	CHPL_FRANCE	= 4,
	CHPL_MKK	= 5,
	CHPL_MKK1	= 6,
	CHPL_ISRAEL	= 7,
	CHPL_TELEC	= 8,
	CHPL_GLOBAL	= 9,
	CHPL_WORLD	= 10,
};

struct phy_sts_cck_8192d {
	u8 adc_pwdb_X[4];
	u8 sq_rpt;
	u8 cck_agc_rpt;
};

struct h2c_cmd_8192c {
	u8 element_id;
	u32 cmd_len;
	u8 *p_cmdbuffer;
};

struct txpower_info {
	u8 cck_index[RF6052_MAX_PATH][CHANNEL_GROUP_MAX];
	u8 ht40_1sindex[RF6052_MAX_PATH][CHANNEL_GROUP_MAX];
	u8 ht40_2sindexdiff[RF6052_MAX_PATH][CHANNEL_GROUP_MAX];
	u8 ht20indexdiff[RF6052_MAX_PATH][CHANNEL_GROUP_MAX];
	u8 ofdmindexdiff[RF6052_MAX_PATH][CHANNEL_GROUP_MAX];
	u8 ht40maxoffset[RF6052_MAX_PATH][CHANNEL_GROUP_MAX];
	u8 ht20maxoffset[RF6052_MAX_PATH][CHANNEL_GROUP_MAX];
	u8 tssi_a[3];		/* 5GL/5GM/5GH */
	u8 tssi_b[3];
};

#endif
