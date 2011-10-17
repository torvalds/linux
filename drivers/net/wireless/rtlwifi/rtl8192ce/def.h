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

#ifndef __RTL92C_DEF_H__
#define __RTL92C_DEF_H__

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
#define AC2QUEUEID(_AC)					(_AC)

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

#define CHIP_VER_B			BIT(4)
#define CHIP_92C_BITMASK		BIT(0)
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

#define CUT_VERSION_MASK		(BIT(6)|BIT(7))
#define CHIP_VENDOR_UMC			BIT(5)
#define CHIP_VENDOR_UMC_B_CUT		BIT(6) /* Chip version for ECO */
#define IS_VENDOR_UMC_A_CUT(version)	((IS_CHIP_VENDOR_UMC(version)) ? \
	((GET_CVID_CUT_VERSION(version)) ? false : true) : false)
#define IS_CHIP_VER_B(version)  ((version & CHIP_VER_B) ? true : false)
#define IS_VENDOR_UMC_A_CUT(version)	((IS_CHIP_VENDOR_UMC(version)) ? \
	((GET_CVID_CUT_VERSION(version)) ? false : true) : false)
#define IS_92C_SERIAL(version)  ((version & CHIP_92C_BITMASK) ? true : false)
#define IS_CHIP_VENDOR_UMC(version)		\
	((version & CHIP_VENDOR_UMC) ? true : false)
#define GET_CVID_CUT_VERSION(version)	((version) & CUT_VERSION_MASK)
#define IS_81xxC_VENDOR_UMC_B_CUT(version)		\
	((IS_CHIP_VENDOR_UMC(version)) ? \
	((GET_CVID_CUT_VERSION(version) == CHIP_VENDOR_UMC_B_CUT) ?	\
	true : false) : false)

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
