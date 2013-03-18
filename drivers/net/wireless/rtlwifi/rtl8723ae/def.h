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
 ****************************************************************************
 */

#ifndef __RTL8723E_DEF_H__
#define __RTL8723E_DEF_H__

#define HAL_PRIME_CHNL_OFFSET_LOWER			1

#define RX_MPDU_QUEUE					0

#define CHIP_8723			BIT(0)
#define NORMAL_CHIP			BIT(3)
#define RF_TYPE_1T2R			BIT(4)
#define RF_TYPE_2T2R			BIT(5)
#define CHIP_VENDOR_UMC			BIT(7)
#define B_CUT_VERSION			BIT(12)
#define C_CUT_VERSION			BIT(13)
#define D_CUT_VERSION			((BIT(12)|BIT(13)))
#define E_CUT_VERSION			BIT(14)
#define	RF_RL_ID			(BIT(31)|BIT(30)|BIT(29)|BIT(28))

enum version_8723e {
	VERSION_TEST_UMC_CHIP_8723 = 0x0081,
	VERSION_NORMAL_UMC_CHIP_8723_1T1R_A_CUT = 0x0089,
	VERSION_NORMAL_UMC_CHIP_8723_1T1R_B_CUT = 0x1089,
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
#define GET_CVID_MANUFACTUER(version)	((version) & MANUFACTUER_MASK)
#define GET_CVID_CUT_VERSION(version)	((version) & CUT_VERSION_MASK)

#define IS_81XXC(version)		((GET_CVID_IC_TYPE(version) == 0) ?\
					true : false)
#define IS_8723_SERIES(version)						\
		((GET_CVID_IC_TYPE(version) == CHIP_8723) ? true : false)
#define IS_CHIP_VENDOR_UMC(version)					\
		((GET_CVID_MANUFACTUER(version)) ? true : false)

#define IS_VENDOR_UMC_A_CUT(version)	((IS_CHIP_VENDOR_UMC(version)) ? \
		((GET_CVID_CUT_VERSION(version)) ? false : true) : false)
#define IS_VENDOR_8723_A_CUT(version)	((IS_8723_SERIES(version)) ?	\
		((GET_CVID_CUT_VERSION(version)) ? false : true) : false)
#define IS_81xxC_VENDOR_UMC_B_CUT(version)	((IS_CHIP_VENDOR_UMC(version)) \
		? ((GET_CVID_CUT_VERSION(version) == B_CUT_VERSION) ? \
		true : false) : false)

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
