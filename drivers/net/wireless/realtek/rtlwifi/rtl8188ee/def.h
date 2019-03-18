/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2013  Realtek Corporation.*/

#ifndef __RTL92C_DEF_H__
#define __RTL92C_DEF_H__

#define HAL_PRIME_CHNL_OFFSET_DONT_CARE			0
#define HAL_PRIME_CHNL_OFFSET_LOWER			1
#define HAL_PRIME_CHNL_OFFSET_UPPER			2

#define RX_MPDU_QUEUE					0
#define RX_CMD_QUEUE					1

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

#define CHIP_BONDING_IDENTIFIER(_value)	(((_value)>>22)&0x3)

/* [15:12] IC version(CUT): A-cut=0, B-cut=1, C-cut=2, D-cut=3
 * [7] Manufacturer: TSMC=0, UMC=1
 * [6:4] RF type: 1T1R=0, 1T2R=1, 2T2R=2
 * [3] Chip type: TEST=0, NORMAL=1
 * [2:0] IC type: 81xxC=0, 8723=1, 92D=2
 */
#define CHIP_8723			BIT(0)
#define CHIP_92D			BIT(1)
#define NORMAL_CHIP			BIT(3)
#define RF_TYPE_1T1R			(~(BIT(4)|BIT(5)|BIT(6)))
#define RF_TYPE_1T2R			BIT(4)
#define RF_TYPE_2T2R			BIT(5)
#define CHIP_VENDOR_UMC			BIT(7)
#define B_CUT_VERSION			BIT(12)
#define C_CUT_VERSION			BIT(13)
#define D_CUT_VERSION			((BIT(12)|BIT(13)))
#define E_CUT_VERSION			BIT(14)

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

#define IS_81XXC(version)						\
	((GET_CVID_IC_TYPE(version) == 0) ? true : false)
#define IS_8723_SERIES(version)						\
	((GET_CVID_IC_TYPE(version) == CHIP_8723) ? true : false)
#define IS_92D(version)							\
	((GET_CVID_IC_TYPE(version) == CHIP_92D) ? true : false)

#define IS_NORMAL_CHIP(version)						\
	((GET_CVID_CHIP_TYPE(version)) ? true : false)
#define IS_NORMAL_CHIP92D(version)					\
	((GET_CVID_CHIP_TYPE(version)) ? true : false)

#define IS_1T1R(version)						\
	((GET_CVID_RF_TYPE(version)) ? false : true)
#define IS_1T2R(version)						\
	((GET_CVID_RF_TYPE(version) == RF_TYPE_1T2R) ? true : false)
#define IS_2T2R(version)						\
	((GET_CVID_RF_TYPE(version) == RF_TYPE_2T2R) ? true : false)
#define IS_CHIP_VENDOR_UMC(version)					\
	((GET_CVID_MANUFACTUER(version)) ? true : false)

#define IS_92C_SERIAL(version)						\
	((IS_81XXC(version) && IS_2T2R(version)) ? true : false)
#define IS_81XXC_VENDOR_UMC_B_CUT(version)				\
	(IS_81XXC(version) ? (IS_CHIP_VENDOR_UMC(version) ?		\
	((GET_CVID_CUT_VERSION(version) == B_CUT_VERSION) ? true	\
	: false) : false) : false)

enum version_8188e {
	VERSION_TEST_CHIP_88E = 0x00,
	VERSION_NORMAL_CHIP_88E = 0x01,
	VERSION_UNKNOWN = 0xFF,
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

enum rtl_desc92c_rate {
	DESC92C_RATE1M = 0x00,
	DESC92C_RATE2M = 0x01,
	DESC92C_RATE5_5M = 0x02,
	DESC92C_RATE11M = 0x03,

	DESC92C_RATE6M = 0x04,
	DESC92C_RATE9M = 0x05,
	DESC92C_RATE12M = 0x06,
	DESC92C_RATE18M = 0x07,
	DESC92C_RATE24M = 0x08,
	DESC92C_RATE36M = 0x09,
	DESC92C_RATE48M = 0x0a,
	DESC92C_RATE54M = 0x0b,

	DESC92C_RATEMCS0 = 0x0c,
	DESC92C_RATEMCS1 = 0x0d,
	DESC92C_RATEMCS2 = 0x0e,
	DESC92C_RATEMCS3 = 0x0f,
	DESC92C_RATEMCS4 = 0x10,
	DESC92C_RATEMCS5 = 0x11,
	DESC92C_RATEMCS6 = 0x12,
	DESC92C_RATEMCS7 = 0x13,
	DESC92C_RATEMCS8 = 0x14,
	DESC92C_RATEMCS9 = 0x15,
	DESC92C_RATEMCS10 = 0x16,
	DESC92C_RATEMCS11 = 0x17,
	DESC92C_RATEMCS12 = 0x18,
	DESC92C_RATEMCS13 = 0x19,
	DESC92C_RATEMCS14 = 0x1a,
	DESC92C_RATEMCS15 = 0x1b,
	DESC92C_RATEMCS15_SG = 0x1c,
	DESC92C_RATEMCS32 = 0x20,
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
