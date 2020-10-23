/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef __RTL8703B_CMD_H__
#define __RTL8703B_CMD_H__

/* ---------------------------------------------------------------------------------------------------------
 * ----------------------------------    H2C CMD DEFINITION    ------------------------------------------------
 * --------------------------------------------------------------------------------------------------------- */

enum h2c_cmd_8703B {
	/* Common Class: 000 */
	H2C_8703B_RSVD_PAGE = 0x00,
	H2C_8703B_MEDIA_STATUS_RPT = 0x01,
	H2C_8703B_SCAN_ENABLE = 0x02,
	H2C_8703B_KEEP_ALIVE = 0x03,
	H2C_8703B_DISCON_DECISION = 0x04,
	H2C_8703B_PSD_OFFLOAD = 0x05,
	H2C_8703B_AP_OFFLOAD = 0x08,
	H2C_8703B_BCN_RSVDPAGE = 0x09,
	H2C_8703B_PROBERSP_RSVDPAGE = 0x0A,
	H2C_8703B_FCS_RSVDPAGE = 0x10,
	H2C_8703B_FCS_INFO = 0x11,
	H2C_8703B_AP_WOW_GPIO_CTRL = 0x13,

	/* PoweSave Class: 001 */
	H2C_8703B_SET_PWR_MODE = 0x20,
	H2C_8703B_PS_TUNING_PARA = 0x21,
	H2C_8703B_PS_TUNING_PARA2 = 0x22,
	H2C_8703B_P2P_LPS_PARAM = 0x23,
	H2C_8703B_P2P_PS_OFFLOAD = 0x24,
	H2C_8703B_PS_SCAN_ENABLE = 0x25,
	H2C_8703B_SAP_PS_ = 0x26,
	H2C_8703B_INACTIVE_PS_ = 0x27, /* Inactive_PS */
	H2C_8703B_FWLPS_IN_IPS_ = 0x28,

	/* Dynamic Mechanism Class: 010 */
	H2C_8703B_MACID_CFG = 0x40,
	H2C_8703B_TXBF = 0x41,
	H2C_8703B_RSSI_SETTING = 0x42,
	H2C_8703B_AP_REQ_TXRPT = 0x43,
	H2C_8703B_INIT_RATE_COLLECT = 0x44,
	H2C_8703B_RA_PARA_ADJUST = 0x46,

	/* BT Class: 011 */
	H2C_8703B_B_TYPE_TDMA = 0x60,
	H2C_8703B_BT_INFO = 0x61,
	H2C_8703B_FORCE_BT_TXPWR = 0x62,
	H2C_8703B_BT_IGNORE_WLANACT = 0x63,
	H2C_8703B_DAC_SWING_VALUE = 0x64,
	H2C_8703B_ANT_SEL_RSV = 0x65,
	H2C_8703B_WL_OPMODE = 0x66,
	H2C_8703B_BT_MP_OPER = 0x67,
	H2C_8703B_BT_CONTROL = 0x68,
	H2C_8703B_BT_WIFI_CTRL = 0x69,
	H2C_8703B_BT_FW_PATCH = 0x6A,
	H2C_8703B_BT_WLAN_CALIBRATION = 0x6D,

	/* WOWLAN Class: 100 */
	H2C_8703B_WOWLAN = 0x80,
	H2C_8703B_REMOTE_WAKE_CTRL = 0x81,
	H2C_8703B_AOAC_GLOBAL_INFO = 0x82,
	H2C_8703B_AOAC_RSVD_PAGE = 0x83,
	H2C_8703B_AOAC_RSVD_PAGE2 = 0x84,
	H2C_8703B_D0_SCAN_OFFLOAD_CTRL = 0x85,
	H2C_8703B_D0_SCAN_OFFLOAD_INFO = 0x86,
	H2C_8703B_CHNL_SWITCH_OFFLOAD = 0x87,
	H2C_8703B_P2P_OFFLOAD_RSVD_PAGE = 0x8A,
	H2C_8703B_P2P_OFFLOAD = 0x8B,

	H2C_8703B_RESET_TSF = 0xC0,
	H2C_8703B_MAXID,
};

/* ---------------------------------------------------------------------------------------------------------
 * ----------------------------------    H2C CMD CONTENT    --------------------------------------------------
 * ---------------------------------------------------------------------------------------------------------
 * _RSVDPAGE_LOC_CMD_0x00 */
#define SET_8703B_H2CCMD_RSVDPAGE_LOC_PROBE_RSP(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)
#define SET_8703B_H2CCMD_RSVDPAGE_LOC_PSPOLL(__pH2CCmd, __Value)				SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 0, 8, __Value)
#define SET_8703B_H2CCMD_RSVDPAGE_LOC_NULL_DATA(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE((__pH2CCmd)+2, 0, 8, __Value)
#define SET_8703B_H2CCMD_RSVDPAGE_LOC_QOS_NULL_DATA(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE((__pH2CCmd)+3, 0, 8, __Value)
#define SET_8703B_H2CCMD_RSVDPAGE_LOC_BT_QOS_NULL_DATA(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd)+4, 0, 8, __Value)

/* _KEEP_ALIVE_CMD_0x03 */
#define SET_8703B_H2CCMD_KEEPALIVE_PARM_ENABLE(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 1, __Value)
#define SET_8703B_H2CCMD_KEEPALIVE_PARM_ADOPT(__pH2CCmd, __Value)				SET_BITS_TO_LE_1BYTE(__pH2CCmd, 1, 1, __Value)
#define SET_8703B_H2CCMD_KEEPALIVE_PARM_PKT_TYPE(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd, 2, 1, __Value)
#define SET_8703B_H2CCMD_KEEPALIVE_PARM_CHECK_PERIOD(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd+1, 0, 8, __Value)

/* _DISCONNECT_DECISION_CMD_0x04 */
#define SET_8703B_H2CCMD_DISCONDECISION_PARM_ENABLE(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 1, __Value)
#define SET_8703B_H2CCMD_DISCONDECISION_PARM_ADOPT(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd, 1, 1, __Value)
#define SET_8703B_H2CCMD_DISCONDECISION_PARM_CHECK_PERIOD(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE(__pH2CCmd+1, 0, 8, __Value)
#define SET_8703B_H2CCMD_DISCONDECISION_PARM_TRY_PKT_NUM(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE(__pH2CCmd+2, 0, 8, __Value)

/* _PWR_MOD_CMD_0x20 */
#define SET_8703B_H2CCMD_PWRMODE_PARM_MODE(__pH2CCmd, __Value)				SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)
#define SET_8703B_H2CCMD_PWRMODE_PARM_RLBM(__pH2CCmd, __Value)				SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 0, 4, __Value)
#define SET_8703B_H2CCMD_PWRMODE_PARM_SMART_PS(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE((__pH2CCmd)+1, 4, 4, __Value)
#define SET_8703B_H2CCMD_PWRMODE_PARM_BCN_PASS_TIME(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE((__pH2CCmd)+2, 0, 8, __Value)
#define SET_8703B_H2CCMD_PWRMODE_PARM_ALL_QUEUE_UAPSD(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd)+3, 0, 8, __Value)
#define SET_8703B_H2CCMD_PWRMODE_PARM_BCN_EARLY_C2H_RPT(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE((__pH2CCmd)+3, 2, 1, __Value)
#define SET_8703B_H2CCMD_PWRMODE_PARM_PWR_STATE(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE((__pH2CCmd)+4, 0, 8, __Value)

#define GET_8703B_H2CCMD_PWRMODE_PARM_MODE(__pH2CCmd)					LE_BITS_TO_1BYTE(__pH2CCmd, 0, 8)

/* _PS_TUNE_PARAM_CMD_0x21 */
#define SET_8703B_H2CCMD_PSTUNE_PARM_BCN_TO_LIMIT(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)
#define SET_8703B_H2CCMD_PSTUNE_PARM_DTIM_TIMEOUT(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd+1, 0, 8, __Value)
#define SET_8703B_H2CCMD_PSTUNE_PARM_ADOPT(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd+2, 0, 1, __Value)
#define SET_8703B_H2CCMD_PSTUNE_PARM_PS_TIMEOUT(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd+2, 1, 7, __Value)
#define SET_8703B_H2CCMD_PSTUNE_PARM_DTIM_PERIOD(__pH2CCmd, __Value)			SET_BITS_TO_LE_1BYTE(__pH2CCmd+3, 0, 8, __Value)

/* _MACID_CFG_CMD_0x40 */
#define SET_8703B_H2CCMD_MACID_CFG_MACID(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)
#define SET_8703B_H2CCMD_MACID_CFG_RAID(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd+1, 0, 5, __Value)
#define SET_8703B_H2CCMD_MACID_CFG_SGI_EN(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd+1, 7, 1, __Value)
#define SET_8703B_H2CCMD_MACID_CFG_BW(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd+2, 0, 2, __Value)
#define SET_8703B_H2CCMD_MACID_CFG_NO_UPDATE(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd+2, 3, 1, __Value)
#define SET_8703B_H2CCMD_MACID_CFG_VHT_EN(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd+2, 4, 2, __Value)
#define SET_8703B_H2CCMD_MACID_CFG_DISPT(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd+2, 6, 1, __Value)
#define SET_8703B_H2CCMD_MACID_CFG_DISRA(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd+2, 7, 1, __Value)
#define SET_8703B_H2CCMD_MACID_CFG_RATE_MASK0(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd+3, 0, 8, __Value)
#define SET_8703B_H2CCMD_MACID_CFG_RATE_MASK1(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd+4, 0, 8, __Value)
#define SET_8703B_H2CCMD_MACID_CFG_RATE_MASK2(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd+5, 0, 8, __Value)
#define SET_8703B_H2CCMD_MACID_CFG_RATE_MASK3(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd+6, 0, 8, __Value)

/* _RSSI_SETTING_CMD_0x42 */
#define SET_8703B_H2CCMD_RSSI_SETTING_MACID(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)
#define SET_8703B_H2CCMD_RSSI_SETTING_RSSI(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd+2, 0, 7, __Value)
#define SET_8703B_H2CCMD_RSSI_SETTING_ULDL_STATE(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd+3, 0, 8, __Value)

/* _AP_REQ_TXRPT_CMD_0x43 */
#define SET_8703B_H2CCMD_APREQRPT_PARM_MACID1(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)
#define SET_8703B_H2CCMD_APREQRPT_PARM_MACID2(__pH2CCmd, __Value)		SET_BITS_TO_LE_1BYTE(__pH2CCmd+1, 0, 8, __Value)

/* _FORCE_BT_TXPWR_CMD_0x62 */
#define SET_8703B_H2CCMD_BT_PWR_IDX(__pH2CCmd, __Value)							SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)

/* _FORCE_BT_MP_OPER_CMD_0x67 */
#define SET_8703B_H2CCMD_BT_MPOPER_VER(__pH2CCmd, __Value)							SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 4, __Value)
#define SET_8703B_H2CCMD_BT_MPOPER_REQNUM(__pH2CCmd, __Value)							SET_BITS_TO_LE_1BYTE(__pH2CCmd, 4, 4, __Value)
#define SET_8703B_H2CCMD_BT_MPOPER_IDX(__pH2CCmd, __Value)							SET_BITS_TO_LE_1BYTE(__pH2CCmd+1, 0, 8, __Value)
#define SET_8703B_H2CCMD_BT_MPOPER_PARAM1(__pH2CCmd, __Value)							SET_BITS_TO_LE_1BYTE(__pH2CCmd+2, 0, 8, __Value)
#define SET_8703B_H2CCMD_BT_MPOPER_PARAM2(__pH2CCmd, __Value)							SET_BITS_TO_LE_1BYTE(__pH2CCmd+3, 0, 8, __Value)
#define SET_8703B_H2CCMD_BT_MPOPER_PARAM3(__pH2CCmd, __Value)							SET_BITS_TO_LE_1BYTE(__pH2CCmd+4, 0, 8, __Value)

/* _BT_FW_PATCH_0x6A */
#define SET_8703B_H2CCMD_BT_FW_PATCH_SIZE(__pH2CCmd, __Value)					SET_BITS_TO_LE_2BYTE((u8 *)(__pH2CCmd), 0, 16, __Value)
#define SET_8703B_H2CCMD_BT_FW_PATCH_ADDR0(__pH2CCmd, __Value)					SET_BITS_TO_LE_1BYTE((__pH2CCmd)+2, 0, 8, __Value)
#define SET_8703B_H2CCMD_BT_FW_PATCH_ADDR1(__pH2CCmd, __Value)					SET_BITS_TO_LE_1BYTE((__pH2CCmd)+3, 0, 8, __Value)
#define SET_8703B_H2CCMD_BT_FW_PATCH_ADDR2(__pH2CCmd, __Value)					SET_BITS_TO_LE_1BYTE((__pH2CCmd)+4, 0, 8, __Value)
#define SET_8703B_H2CCMD_BT_FW_PATCH_ADDR3(__pH2CCmd, __Value)					SET_BITS_TO_LE_1BYTE((__pH2CCmd)+5, 0, 8, __Value)

/* ---------------------------------------------------------------------------------------------------------
 * -------------------------------------------    Structure    --------------------------------------------------
 * --------------------------------------------------------------------------------------------------------- */


/* ---------------------------------------------------------------------------------------------------------
 * ----------------------------------    Function Statement     --------------------------------------------------
 * --------------------------------------------------------------------------------------------------------- */

/* host message to firmware cmd */
void rtl8703b_set_FwPwrMode_cmd(PADAPTER padapter, u8 Mode);
void rtl8703b_set_FwJoinBssRpt_cmd(PADAPTER padapter, u8 mstatus);
void rtl8703b_fw_try_ap_cmd(PADAPTER padapter, u32 need_ack);
/* s32 rtl8703b_set_lowpwr_lps_cmd(PADAPTER padapter, u8 enable); */
void rtl8703b_set_FwPsTuneParam_cmd(PADAPTER padapter);
void rtl8703b_set_FwBtMpOper_cmd(PADAPTER padapter, u8 idx, u8 ver, u8 reqnum, u8 *param);
void rtl8703b_download_rsvd_page(PADAPTER padapter, u8 mstatus);
#ifdef CONFIG_BT_COEXIST
	void rtl8703b_download_BTCoex_AP_mode_rsvd_page(PADAPTER padapter);
#endif /* CONFIG_BT_COEXIST */
#ifdef CONFIG_P2P
	void rtl8703b_set_p2p_ps_offload_cmd(PADAPTER padapter, u8 p2p_ps_state);
#endif /* CONFIG_P2P */

#ifdef CONFIG_P2P_WOWLAN
	void rtl8703b_set_p2p_wowlan_offload_cmd(PADAPTER padapter);
#endif

void rtl8703b_set_FwPwrModeInIPS_cmd(PADAPTER padapter, u8 cmd_param);

s32 FillH2CCmd8703B(PADAPTER padapter, u8 ElementID, u32 CmdLen, u8 *pCmdBuffer);
u8 GetTxBufferRsvdPageNum8703B(_adapter *padapter, bool wowlan);
#endif
