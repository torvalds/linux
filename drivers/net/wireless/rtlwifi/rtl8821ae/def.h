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

#ifndef __RTL8821AE_DEF_H__
#define __RTL8821AE_DEF_H__

/*--------------------------Define -------------------------------------------*/
#define	USE_SPECIFIC_FW_TO_SUPPORT_WOWLAN	1

/* BIT 7 HT Rate*/
/*TxHT = 0*/
#define	MGN_1M				0x02
#define	MGN_2M				0x04
#define	MGN_5_5M			0x0b
#define	MGN_11M				0x16

#define	MGN_6M				0x0c
#define	MGN_9M				0x12
#define	MGN_12M				0x18
#define	MGN_18M				0x24
#define	MGN_24M				0x30
#define	MGN_36M				0x48
#define	MGN_48M				0x60
#define	MGN_54M				0x6c

/* TxHT = 1 */
#define	MGN_MCS0			0x80
#define	MGN_MCS1			0x81
#define	MGN_MCS2			0x82
#define	MGN_MCS3			0x83
#define	MGN_MCS4			0x84
#define	MGN_MCS5			0x85
#define	MGN_MCS6			0x86
#define	MGN_MCS7			0x87
#define	MGN_MCS8			0x88
#define	MGN_MCS9			0x89
#define	MGN_MCS10			0x8a
#define	MGN_MCS11			0x8b
#define	MGN_MCS12			0x8c
#define	MGN_MCS13			0x8d
#define	MGN_MCS14			0x8e
#define	MGN_MCS15			0x8f
/* VHT rate */
#define	MGN_VHT1SS_MCS0		0x90
#define	MGN_VHT1SS_MCS1		0x91
#define	MGN_VHT1SS_MCS2		0x92
#define	MGN_VHT1SS_MCS3		0x93
#define	MGN_VHT1SS_MCS4		0x94
#define	MGN_VHT1SS_MCS5		0x95
#define	MGN_VHT1SS_MCS6		0x96
#define	MGN_VHT1SS_MCS7		0x97
#define	MGN_VHT1SS_MCS8		0x98
#define	MGN_VHT1SS_MCS9		0x99
#define	MGN_VHT2SS_MCS0		0x9a
#define	MGN_VHT2SS_MCS1		0x9b
#define	MGN_VHT2SS_MCS2		0x9c
#define	MGN_VHT2SS_MCS3		0x9d
#define	MGN_VHT2SS_MCS4		0x9e
#define	MGN_VHT2SS_MCS5		0x9f
#define	MGN_VHT2SS_MCS6		0xa0
#define	MGN_VHT2SS_MCS7		0xa1
#define	MGN_VHT2SS_MCS8		0xa2
#define	MGN_VHT2SS_MCS9		0xa3

#define	MGN_VHT3SS_MCS0		0xa4
#define	MGN_VHT3SS_MCS1		0xa5
#define	MGN_VHT3SS_MCS2		0xa6
#define	MGN_VHT3SS_MCS3		0xa7
#define	MGN_VHT3SS_MCS4		0xa8
#define	MGN_VHT3SS_MCS5		0xa9
#define	MGN_VHT3SS_MCS6		0xaa
#define	MGN_VHT3SS_MCS7		0xab
#define	MGN_VHT3SS_MCS8		0xac
#define	MGN_VHT3SS_MCS9		0xad

#define	MGN_MCS0_SG			0xc0
#define	MGN_MCS1_SG			0xc1
#define	MGN_MCS2_SG			0xc2
#define	MGN_MCS3_SG			0xc3
#define	MGN_MCS4_SG			0xc4
#define	MGN_MCS5_SG			0xc5
#define	MGN_MCS6_SG			0xc6
#define	MGN_MCS7_SG			0xc7
#define	MGN_MCS8_SG			0xc8
#define	MGN_MCS9_SG			0xc9
#define	MGN_MCS10_SG		0xca
#define	MGN_MCS11_SG		0xcb
#define	MGN_MCS12_SG		0xcc
#define	MGN_MCS13_SG		0xcd
#define	MGN_MCS14_SG		0xce
#define	MGN_MCS15_SG		0xcf

#define	MGN_UNKNOWN			0xff

/* 30 ms */
#define	WIFI_NAV_UPPER_US				30000
#define HAL_92C_NAV_UPPER_UNIT			128

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

#define MAX_RX_DMA_BUFFER_SIZE				0x3E80

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

#define MAX_RX_DMA_BUFFER_SIZE_8812	0x3E80

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

#define CHIP_8812				BIT(2)
#define CHIP_8821				(BIT(0)|BIT(2))

#define CHIP_8821A				(BIT(0)|BIT(2))
#define NORMAL_CHIP				BIT(3)
#define RF_TYPE_1T1R				(~(BIT(4)|BIT(5)|BIT(6)))
#define RF_TYPE_1T2R				BIT(4)
#define RF_TYPE_2T2R				BIT(5)
#define CHIP_VENDOR_UMC				BIT(7)
#define B_CUT_VERSION				BIT(12)
#define C_CUT_VERSION				BIT(13)
#define D_CUT_VERSION				((BIT(12)|BIT(13)))
#define E_CUT_VERSION				BIT(14)
#define	RF_RL_ID			(BIT(31)|BIT(30)|BIT(29)|BIT(28))

enum version_8821ae {
	VERSION_TEST_CHIP_1T1R_8812 = 0x0004,
	VERSION_TEST_CHIP_2T2R_8812 = 0x0024,
	VERSION_NORMAL_TSMC_CHIP_1T1R_8812 = 0x100c,
	VERSION_NORMAL_TSMC_CHIP_2T2R_8812 = 0x102c,
	VERSION_NORMAL_TSMC_CHIP_1T1R_8812_C_CUT = 0x200c,
	VERSION_NORMAL_TSMC_CHIP_2T2R_8812_C_CUT = 0x202c,
	VERSION_TEST_CHIP_8821 = 0x0005,
	VERSION_NORMAL_TSMC_CHIP_8821 = 0x000d,
	VERSION_NORMAL_TSMC_CHIP_8821_B_CUT = 0x100d,
	VERSION_UNKNOWN = 0xFF,
};

enum vht_data_sc {
	VHT_DATA_SC_DONOT_CARE = 0,
	VHT_DATA_SC_20_UPPER_OF_80MHZ = 1,
	VHT_DATA_SC_20_LOWER_OF_80MHZ = 2,
	VHT_DATA_SC_20_UPPERST_OF_80MHZ = 3,
	VHT_DATA_SC_20_LOWEST_OF_80MHZ = 4,
	VHT_DATA_SC_20_RECV1 = 5,
	VHT_DATA_SC_20_RECV2 = 6,
	VHT_DATA_SC_20_RECV3 = 7,
	VHT_DATA_SC_20_RECV4 = 8,
	VHT_DATA_SC_40_UPPER_OF_80MHZ = 9,
	VHT_DATA_SC_40_LOWER_OF_80MHZ = 10,
};

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

#define IS_1T1R(version)	((GET_CVID_RF_TYPE(version)) ? false : true)
#define IS_1T2R(version)	((GET_CVID_RF_TYPE(version) == RF_TYPE_1T2R)\
							? true : false)
#define IS_2T2R(version)	((GET_CVID_RF_TYPE(version) == RF_TYPE_2T2R)\
							? true : false)

#define IS_8812_SERIES(version)	((GET_CVID_IC_TYPE(version) == CHIP_8812) ? \
								true : false)
#define IS_8821_SERIES(version)	((GET_CVID_IC_TYPE(version) == CHIP_8821) ? \
								true : false)

#define IS_VENDOR_8812A_TEST_CHIP(version)	((IS_8812_SERIES(version)) ? \
					((IS_NORMAL_CHIP(version)) ? \
						false : true) : false)
#define IS_VENDOR_8812A_MP_CHIP(version)	((IS_8812_SERIES(version)) ? \
					((IS_NORMAL_CHIP(version)) ? \
						true : false) : false)
#define IS_VENDOR_8812A_C_CUT(version)		((IS_8812_SERIES(version)) ? \
					((GET_CVID_CUT_VERSION(version) == \
					C_CUT_VERSION) ? \
					true : false) : false)

#define IS_VENDOR_8821A_TEST_CHIP(version)	((IS_8821_SERIES(version)) ? \
					((IS_NORMAL_CHIP(version)) ? \
					false : true) : false)
#define IS_VENDOR_8821A_MP_CHIP(version)	((IS_8821_SERIES(version)) ? \
					((IS_NORMAL_CHIP(version)) ? \
						true : false) : false)
#define IS_VENDOR_8821A_B_CUT(version)		((IS_8821_SERIES(version)) ? \
					((GET_CVID_CUT_VERSION(version) == \
					B_CUT_VERSION) ? \
					true : false) : false)
enum board_type {
	ODM_BOARD_DEFAULT = 0,	  /* The DEFAULT case. */
	ODM_BOARD_MINICARD = BIT(0), /* 0 = non-mini card, 1 = mini card. */
	ODM_BOARD_SLIM = BIT(1), /* 0 = non-slim card, 1 = slim card */
	ODM_BOARD_BT = BIT(2), /* 0 = without BT card, 1 = with BT */
	ODM_BOARD_EXT_PA = BIT(3), /* 1 = existing 2G ext-PA */
	ODM_BOARD_EXT_LNA = BIT(4), /* 1 = existing 2G ext-LNA */
	ODM_BOARD_EXT_TRSW = BIT(5), /* 1 = existing ext-TRSW */
	ODM_BOARD_EXT_PA_5G = BIT(6), /* 1 = existing 5G ext-PA */
	ODM_BOARD_EXT_LNA_5G = BIT(7), /* 1 = existing 5G ext-LNA */
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

enum hal_fw_c2h_cmd_id {
	HAL_FW_C2H_CMD_READ_MACREG = 0,
	HAL_FW_C2H_CMD_READ_BBREG = 1,
	HAL_FW_C2H_CMD_READ_RFREG = 2,
	HAL_FW_C2H_CMD_READ_EEPROM = 3,
	HAL_FW_C2H_CMD_READ_EFUSE = 4,
	HAL_FW_C2H_CMD_READ_CAM = 5,
	HAL_FW_C2H_CMD_GET_BASICRATE = 6,
	HAL_FW_C2H_CMD_GET_DATARATE = 7,
	HAL_FW_C2H_CMD_SURVEY = 8,
	HAL_FW_C2H_CMD_SURVEYDONE = 9,
	HAL_FW_C2H_CMD_JOINBSS = 10,
	HAL_FW_C2H_CMD_ADDSTA = 11,
	HAL_FW_C2H_CMD_DELSTA = 12,
	HAL_FW_C2H_CMD_ATIMDONE = 13,
	HAL_FW_C2H_CMD_TX_REPORT = 14,
	HAL_FW_C2H_CMD_CCX_REPORT = 15,
	HAL_FW_C2H_CMD_DTM_REPORT = 16,
	HAL_FW_C2H_CMD_TX_RATE_STATISTICS = 17,
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

enum rx_packet_type {
	NORMAL_RX,
	TX_REPORT1,
	TX_REPORT2,
	HIS_REPORT,
	C2H_PACKET,
};

struct phy_sts_cck_8821ae_t {
	u8 adc_pwdb_X[4];
	u8 sq_rpt;
	u8 cck_agc_rpt;
};

struct h2c_cmd_8821ae {
	u8 element_id;
	u32 cmd_len;
	u8 *p_cmdbuffer;
};

#endif
