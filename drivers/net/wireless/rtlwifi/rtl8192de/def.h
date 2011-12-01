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

enum version_8192d {
	VERSION_TEST_CHIP_88C = 0x0000,
	VERSION_TEST_CHIP_92C = 0x0020,
	VERSION_TEST_UMC_CHIP_8723 = 0x0081,
	VERSION_NORMAL_TSMC_CHIP_88C = 0x0008,
	VERSION_NORMAL_TSMC_CHIP_92C = 0x0028,
	VERSION_NORMAL_TSMC_CHIP_92C_1T2R = 0x0018,
	VERSION_NORMAL_UMC_CHIP_88C_A_CUT = 0x0088,
	VERSION_NORMAL_UMC_CHIP_92C_A_CUT = 0x00a8,
	VERSION_NORMAL_UMC_CHIP_92C_1T2R_A_CUT = 0x0098,
	VERSION_NORMAL_UMC_CHIP_8723_1T1R_A_CUT = 0x0089,
	VERSION_NORMAL_UMC_CHIP_8723_1T1R_B_CUT = 0x1089,
	VERSION_NORMAL_UMC_CHIP_88C_B_CUT = 0x1088,
	VERSION_NORMAL_UMC_CHIP_92C_B_CUT = 0x10a8,
	VERSION_NORMAL_UMC_CHIP_92C_1T2R_B_CUT = 0x1090,
	VERSION_TEST_CHIP_92D_SINGLEPHY = 0x0022,
	VERSION_TEST_CHIP_92D_DUALPHY = 0x0002,
	VERSION_NORMAL_CHIP_92D_SINGLEPHY = 0x002a,
	VERSION_NORMAL_CHIP_92D_DUALPHY = 0x000a,
	VERSION_NORMAL_CHIP_92D_C_CUT_SINGLEPHY = 0x202a,
	VERSION_NORMAL_CHIP_92D_C_CUT_DUALPHY = 0x200a,
	VERSION_NORMAL_CHIP_92D_D_CUT_SINGLEPHY = 0x302a,
	VERSION_NORMAL_CHIP_92D_D_CUT_DUALPHY = 0x300a,
	VERSION_NORMAL_CHIP_92D_E_CUT_SINGLEPHY = 0x402a,
	VERSION_NORMAL_CHIP_92D_E_CUT_DUALPHY = 0x400a,
};

/* for 92D */
#define CHIP_92D_SINGLEPHY		BIT(9)
#define C_CUT_VERSION			BIT(13)
#define D_CUT_VERSION			((BIT(12)|BIT(13)))
#define E_CUT_VERSION			BIT(14)

/* Chip specific */
#define CHIP_BONDING_IDENTIFIER(_value)	(((_value)>>22)&0x3)
#define CHIP_BONDING_92C_1T2R			0x1
#define CHIP_BONDING_88C_USB_MCARD		0x2
#define CHIP_BONDING_88C_USB_HP			0x1

/* [15:12] IC version(CUT): A-cut=0, B-cut=1, C-cut=2, D-cut=3 */
/* [7] Manufacturer: TSMC=0, UMC=1 */
/* [6:4] RF type: 1T1R=0, 1T2R=1, 2T2R=2 */
/* [3] Chip type: TEST=0, NORMAL=1 */
/* [2:0] IC type: 81xxC=0, 8723=1, 92D=2 */
#define CHIP_8723			BIT(0)
#define CHIP_92D			BIT(1)
#define NORMAL_CHIP			BIT(3)
#define RF_TYPE_1T1R			(~(BIT(4)|BIT(5)|BIT(6)))
#define RF_TYPE_1T2R			BIT(4)
#define RF_TYPE_2T2R			BIT(5)
#define CHIP_VENDOR_UMC			BIT(7)
#define B_CUT_VERSION			BIT(12)

/* MASK */
#define IC_TYPE_MASK			(BIT(0)|BIT(1)|BIT(2))
#define CHIP_TYPE_MASK			BIT(3)
#define RF_TYPE_MASK			(BIT(4)|BIT(5)|BIT(6))
#define MANUFACTUER_MASK		BIT(7)
#define ROM_VERSION_MASK		(BIT(11)|BIT(10)|BIT(9)|BIT(8))
#define CUT_VERSION_MASK		(BIT(15)|BIT(14)|BIT(13)|BIT(12))


/* Get element */
#define GET_CVID_IC_TYPE(version)	((version) & IC_TYPE_MASK)
#define GET_CVID_CHIP_TYPE(version)	((version) & CHIP_TYPE_MASK)
#define GET_CVID_RF_TYPE(version)	((version) & RF_TYPE_MASK)
#define GET_CVID_MANUFACTUER(version)	((version) & MANUFACTUER_MASK)
#define GET_CVID_ROM_VERSION(version)	((version) & ROM_VERSION_MASK)
#define GET_CVID_CUT_VERSION(version)	((version) & CUT_VERSION_MASK)

#define IS_1T1R(version)		((GET_CVID_RF_TYPE(version)) ?	\
					 false : true)
#define IS_1T2R(version)		((GET_CVID_RF_TYPE(version) ==	\
					 RF_TYPE_1T2R) ? true : false)
#define IS_2T2R(version)		((GET_CVID_RF_TYPE(version) ==	\
					 RF_TYPE_2T2R) ? true : false)

#define IS_92D_SINGLEPHY(version)	((IS_92D(version)) ?		\
				 (IS_2T2R(version) ? true : false) : false)
#define IS_92D(version)			((GET_CVID_IC_TYPE(version) ==	\
					 CHIP_92D) ? true : false)
#define IS_92D_C_CUT(version)		((IS_92D(version)) ?		\
				 ((GET_CVID_CUT_VERSION(version) ==	\
				 0x2000) ? true : false) : false)
#define IS_92D_D_CUT(version)			((IS_92D(version)) ?	\
				 ((GET_CVID_CUT_VERSION(version) ==	\
				 0x3000) ? true : false) : false)
#define IS_92D_E_CUT(version)		((IS_92D(version)) ?		\
				 ((GET_CVID_CUT_VERSION(version) ==	\
				 0x4000) ? true : false) : false)
#define CHIP_92D_C_CUT			BIT(10)
#define CHIP_92D_D_CUT			BIT(11)

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
