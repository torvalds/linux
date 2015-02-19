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

#ifndef __RTL8723E_DEF_H__
#define __RTL8723E_DEF_H__

#define HAL_RETRY_LIMIT_INFRA				48
#define HAL_RETRY_LIMIT_AP_ADHOC			7

#define RESET_DELAY_8185					20

#define RT_IBSS_INT_MASKS	(IMR_BCNINT | IMR_TBDOK | IMR_TBDER)
#define RT_AC_INT_MASKS		(IMR_VIDOK | IMR_VODOK | IMR_BEDOK|IMR_BKDOK)

#define NUM_OF_FIRMWARE_QUEUE				10
#define NUM_OF_PAGES_IN_FW					0x100
#define NUM_OF_PAGE_IN_FW_QUEUE_BK			0x07
#define NUM_OF_PAGE_IN_FW_QUEUE_BE			0x07
#define NUM_OF_PAGE_IN_FW_QUEUE_VI			0x07
#define NUM_OF_PAGE_IN_FW_QUEUE_VO			0x07
#define NUM_OF_PAGE_IN_FW_QUEUE_HCCA		0x0
#define NUM_OF_PAGE_IN_FW_QUEUE_CMD			0x0
#define NUM_OF_PAGE_IN_FW_QUEUE_MGNT		0x02
#define NUM_OF_PAGE_IN_FW_QUEUE_HIGH		0x02
#define NUM_OF_PAGE_IN_FW_QUEUE_BCN			0x2
#define NUM_OF_PAGE_IN_FW_QUEUE_PUB			0xA1

#define NUM_OF_PAGE_IN_FW_QUEUE_BK_DTM		0x026
#define NUM_OF_PAGE_IN_FW_QUEUE_BE_DTM		0x048
#define NUM_OF_PAGE_IN_FW_QUEUE_VI_DTM		0x048
#define NUM_OF_PAGE_IN_FW_QUEUE_VO_DTM		0x026
#define NUM_OF_PAGE_IN_FW_QUEUE_PUB_DTM		0x00

#define MAX_LINES_HWCONFIG_TXT				1000
#define MAX_BYTES_LINE_HWCONFIG_TXT			256

#define SW_THREE_WIRE						0
#define HW_THREE_WIRE						2

#define BT_DEMO_BOARD						0
#define BT_QA_BOARD							1
#define BT_FPGA								2

#define HAL_PRIME_CHNL_OFFSET_DONT_CARE		0
#define HAL_PRIME_CHNL_OFFSET_LOWER			1
#define HAL_PRIME_CHNL_OFFSET_UPPER			2

#define MAX_H2C_QUEUE_NUM					10

#define RX_MPDU_QUEUE						0
#define RX_CMD_QUEUE						1
#define RX_MAX_QUEUE						2
#define AC2QUEUEID(_AC)						(_AC)

#define	C2H_RX_CMD_HDR_LEN					8
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
#define	GET_C2H_CMD_FEEDBACK_CCX_LEN(__pcmdfbhdr)		\
	LE_BITS_TO_4BYTE((__pcmdfbhdr), 8, 8)
#define	GET_C2H_CMD_FEEDBACK_CCX_CMD_CNT(__pcmdfbhdr)	\
	LE_BITS_TO_4BYTE((__pcmdfbhdr), 16, 16)
#define	GET_C2H_CMD_FEEDBACK_CCX_MAC_ID(__pcmdfbhdr)	\
	LE_BITS_TO_4BYTE(((__pcmdfbhdr) + 4), 0, 5)
#define	GET_C2H_CMD_FEEDBACK_CCX_VALID(__pcmdfbhdr)		\
	LE_BITS_TO_4BYTE(((__pcmdfbhdr) + 4), 7, 1)
#define	GET_C2H_CMD_FEEDBACK_CCX_RETRY_CNT(__pcmdfbhdr)	\
	LE_BITS_TO_4BYTE(((__pcmdfbhdr) + 4), 8, 5)
#define	GET_C2H_CMD_FEEDBACK_CCX_TOK(__pcmdfbhdr)		\
	LE_BITS_TO_4BYTE(((__pcmdfbhdr) + 4), 15, 1)
#define	GET_C2H_CMD_FEEDBACK_CCX_QSEL(__pcmdfbhdr)		\
	LE_BITS_TO_4BYTE(((__pcmdfbhdr) + 4), 16, 4)
#define	GET_C2H_CMD_FEEDBACK_CCX_SEQ(__pcmdfbhdr)		\
	LE_BITS_TO_4BYTE(((__pcmdfbhdr) + 4), 20, 12)

#define CHIP_BONDING_IDENTIFIER(_value)	(((_value)>>22)&0x3)
#define	CHIP_BONDING_92C_1T2R		0x1

#define CHIP_8723		BIT(0)
#define NORMAL_CHIP		BIT(3)
#define RF_TYPE_1T1R		(~(BIT(4)|BIT(5)|BIT(6)))
#define RF_TYPE_1T2R		BIT(4)
#define RF_TYPE_2T2R		BIT(5)
#define CHIP_VENDOR_UMC		BIT(7)
#define B_CUT_VERSION		BIT(12)
#define C_CUT_VERSION		BIT(13)
#define D_CUT_VERSION		((BIT(12)|BIT(13)))
#define E_CUT_VERSION		BIT(14)
#define	RF_RL_ID		(BIT(31)|BIT(30)|BIT(29)|BIT(28))

/* MASK */
#define IC_TYPE_MASK		(BIT(0)|BIT(1)|BIT(2))
#define CHIP_TYPE_MASK		BIT(3)
#define RF_TYPE_MASK		(BIT(4)|BIT(5)|BIT(6))
#define MANUFACTUER_MASK	BIT(7)
#define ROM_VERSION_MASK	(BIT(11)|BIT(10)|BIT(9)|BIT(8))
#define CUT_VERSION_MASK	(BIT(15)|BIT(14)|BIT(13)|BIT(12))

/* Get element */
#define GET_CVID_IC_TYPE(version)	((version) & IC_TYPE_MASK)
#define GET_CVID_CHIP_TYPE(version)	((version) & CHIP_TYPE_MASK)
#define GET_CVID_RF_TYPE(version)	((version) & RF_TYPE_MASK)
#define GET_CVID_MANUFACTUER(version)	((version) & MANUFACTUER_MASK)
#define GET_CVID_ROM_VERSION(version)	((version) & ROM_VERSION_MASK)
#define GET_CVID_CUT_VERSION(version)	((version) & CUT_VERSION_MASK)

#define IS_81XXC(version)	((GET_CVID_IC_TYPE(version) == 0) ?\
						true : false)
#define IS_8723_SERIES(version)	((GET_CVID_IC_TYPE(version) == CHIP_8723) ? \
						true : false)
#define IS_1T1R(version)	((GET_CVID_RF_TYPE(version)) ? false : true)
#define IS_1T2R(version)	((GET_CVID_RF_TYPE(version) == RF_TYPE_1T2R)\
						? true : false)
#define IS_2T2R(version)	((GET_CVID_RF_TYPE(version) == RF_TYPE_2T2R)\
						? true : false)
#define IS_CHIP_VENDOR_UMC(version)	((GET_CVID_MANUFACTUER(version)) ? \
						true : false)

#define IS_VENDOR_UMC_A_CUT(version)	((IS_CHIP_VENDOR_UMC(version))\
					? ((GET_CVID_CUT_VERSION(version)) ? \
					false : true) : false)
#define IS_VENDOR_8723_A_CUT(version)	((IS_8723_SERIES(version))\
					? ((GET_CVID_CUT_VERSION(version)) ? \
					false : true) : false)
#define IS_VENDOR_8723A_B_CUT(version)	((IS_8723_SERIES(version))\
		? ((GET_CVID_CUT_VERSION(version) == \
		B_CUT_VERSION) ? true : false) : false)
#define IS_81xxC_VENDOR_UMC_B_CUT(version)	((IS_CHIP_VENDOR_UMC(version))\
		? ((GET_CVID_CUT_VERSION(version) == \
		B_CUT_VERSION) ? true : false) : false)

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

enum power_policy_config {
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

enum hal_fw_c2h_cmd_id {
	HAL_FW_C2H_CMD_Read_MACREG = 0,
	HAL_FW_C2H_CMD_Read_BBREG = 1,
	HAL_FW_C2H_CMD_Read_RFREG = 2,
	HAL_FW_C2H_CMD_Read_EEPROM = 3,
	HAL_FW_C2H_CMD_Read_EFUSE = 4,
	HAL_FW_C2H_CMD_Read_CAM = 5,
	HAL_FW_C2H_CMD_Get_BasicRate = 6,
	HAL_FW_C2H_CMD_Get_DataRate = 7,
	HAL_FW_C2H_CMD_Survey = 8,
	HAL_FW_C2H_CMD_SurveyDone = 9,
	HAL_FW_C2H_CMD_JoinBss = 10,
	HAL_FW_C2H_CMD_AddSTA = 11,
	HAL_FW_C2H_CMD_DelSTA = 12,
	HAL_FW_C2H_CMD_AtimDone = 13,
	HAL_FW_C2H_CMD_TX_Report = 14,
	HAL_FW_C2H_CMD_CCX_Report = 15,
	HAL_FW_C2H_CMD_DTM_Report = 16,
	HAL_FW_C2H_CMD_TX_Rate_Statistics = 17,
	HAL_FW_C2H_CMD_C2HLBK = 18,
	HAL_FW_C2H_CMD_C2HDBG = 19,
	HAL_FW_C2H_CMD_C2HFEEDBACK = 20,
	HAL_FW_C2H_CMD_MAX
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

enum rtl_desc8723e_rate {
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

struct phy_sts_cck_8723e_t {
	u8 adc_pwdb_X[4];
	u8 sq_rpt;
	u8 cck_agc_rpt;
};

struct h2c_cmd_8723e {
	u8 element_id;
	u32 cmd_len;
	u8 *p_cmdbuffer;
};

#endif
