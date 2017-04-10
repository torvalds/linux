/******************************************************************************
 *
 * Copyright(c) 2009-2014  Realtek Corporation.
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
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __RTL92E__FW__H__
#define __RTL92E__FW__H__

#define FW_8192C_SIZE				0x8000
#define FW_8192C_START_ADDRESS			0x1000
#define FW_8192C_END_ADDRESS			0x5FFF
#define FW_8192C_PAGE_SIZE			4096
#define FW_8192C_POLLING_DELAY			5
#define FW_8192C_POLLING_TIMEOUT_COUNT		3000

#define IS_FW_HEADER_EXIST(_pfwhdr)	\
	((le16_to_cpu(_pfwhdr->signature) & 0xFFF0) == 0x92E0)
#define USE_OLD_WOWLAN_DEBUG_FW 0

#define H2C_92E_RSVDPAGE_LOC_LEN		5
#define H2C_92E_PWEMODE_LENGTH			5
#define H2C_92E_JOINBSSRPT_LENGTH		1
#define H2C_92E_AP_OFFLOAD_LENGTH		3
#define H2C_92E_WOWLAN_LENGTH			3
#define H2C_92E_KEEP_ALIVE_CTRL_LENGTH		3
#if (USE_OLD_WOWLAN_DEBUG_FW == 0)
#define H2C_92E_REMOTE_WAKE_CTRL_LEN		1
#else
#define H2C_92E_REMOTE_WAKE_CTRL_LEN		3
#endif
#define H2C_92E_AOAC_GLOBAL_INFO_LEN		2
#define H2C_92E_AOAC_RSVDPAGE_LOC_LEN		7

/* Fw PS state for RPWM.
*BIT[2:0] = HW state
*BIT[3] = Protocol PS state,  1: register active state, 0: register sleep state
*BIT[4] = sub-state
*/
#define	FW_PS_RF_ON		BIT(2)
#define	FW_PS_REGISTER_ACTIVE	BIT(3)

#define	FW_PS_ACK		BIT(6)
#define	FW_PS_TOGGLE		BIT(7)

 /* 92E RPWM value*/
 /* BIT[0] = 1: 32k, 0: 40M*/
#define	FW_PS_CLOCK_OFF		BIT(0)		/* 32k */
#define	FW_PS_CLOCK_ON		0		/* 40M */

#define	FW_PS_STATE_MASK		(0x0F)
#define	FW_PS_STATE_HW_MASK		(0x07)
#define	FW_PS_STATE_INT_MASK		(0x3F)

#define	FW_PS_STATE(x)			(FW_PS_STATE_MASK & (x))

#define	FW_PS_STATE_ALL_ON_92E		(FW_PS_CLOCK_ON)
#define	FW_PS_STATE_RF_ON_92E		(FW_PS_CLOCK_ON)
#define	FW_PS_STATE_RF_OFF_92E		(FW_PS_CLOCK_ON)
#define	FW_PS_STATE_RF_OFF_LOW_PWR	(FW_PS_CLOCK_OFF)

/* For 92E H2C PwrMode Cmd ID 5.*/
#define	FW_PWR_STATE_ACTIVE	((FW_PS_RF_ON) | (FW_PS_REGISTER_ACTIVE))
#define	FW_PWR_STATE_RF_OFF	0

#define	FW_PS_IS_ACK(x)		((x) & FW_PS_ACK)

#define	IS_IN_LOW_POWER_STATE_92E(__state)		\
	(FW_PS_STATE(__state) == FW_PS_CLOCK_OFF)

#define	FW_PWR_STATE_ACTIVE	((FW_PS_RF_ON) | (FW_PS_REGISTER_ACTIVE))
#define	FW_PWR_STATE_RF_OFF	0

enum rtl8192e_h2c_cmd {
	H2C_92E_RSVDPAGE = 0,
	H2C_92E_MSRRPT = 1,
	H2C_92E_SCAN = 2,
	H2C_92E_KEEP_ALIVE_CTRL = 3,
	H2C_92E_DISCONNECT_DECISION = 4,
#if (USE_OLD_WOWLAN_DEBUG_FW == 1)
	H2C_92E_WO_WLAN = 5,
#endif
	H2C_92E_INIT_OFFLOAD = 6,
#if (USE_OLD_WOWLAN_DEBUG_FW == 1)
	H2C_92E_REMOTE_WAKE_CTRL = 7,
#endif
	H2C_92E_AP_OFFLOAD = 8,
	H2C_92E_BCN_RSVDPAGE = 9,
	H2C_92E_PROBERSP_RSVDPAGE = 10,

	H2C_92E_SETPWRMODE = 0x20,
	H2C_92E_PS_TUNING_PARA = 0x21,
	H2C_92E_PS_TUNING_PARA2 = 0x22,
	H2C_92E_PS_LPS_PARA = 0x23,
	H2C_92E_P2P_PS_OFFLOAD = 024,

#if (USE_OLD_WOWLAN_DEBUG_FW == 0)
	H2C_92E_WO_WLAN = 0x80,
	H2C_92E_REMOTE_WAKE_CTRL = 0x81,
	H2C_92E_AOAC_GLOBAL_INFO = 0x82,
	H2C_92E_AOAC_RSVDPAGE = 0x83,
#endif
	H2C_92E_RA_MASK = 0x40,
	H2C_92E_RSSI_REPORT = 0x42,
	H2C_92E_SELECTIVE_SUSPEND_ROF_CMD,
	H2C_92E_P2P_PS_MODE,
	H2C_92E_PSD_RESULT,
	/*Not defined CTW CMD for P2P yet*/
	H2C_92E_P2P_PS_CTW_CMD,
	MAX_92E_H2CCMD
};

enum rtl8192e_c2h_evt {
	C2H_8192E_DBG = 0,
	C2H_8192E_LB = 1,
	C2H_8192E_TXBF = 2,
	C2H_8192E_TX_REPORT = 3,
	C2H_8192E_BT_INFO = 9,
	C2H_8192E_BT_MP = 11,
	C2H_8192E_RA_RPT = 12,
	MAX_8192E_C2HEVENT
};

#define pagenum_128(_len)	\
	(u32)(((_len) >> 7) + ((_len) & 0x7F ? 1 : 0))

#define SET_H2CCMD_PWRMODE_PARM_MODE(__ph2ccmd, __val)			\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 0, 8, __val)
#define SET_H2CCMD_PWRMODE_PARM_RLBM(__cmd, __val)			\
	SET_BITS_TO_LE_1BYTE((__cmd)+1, 0, 4, __val)
#define SET_H2CCMD_PWRMODE_PARM_SMART_PS(__cmd, __val)		\
	SET_BITS_TO_LE_1BYTE((__cmd)+1, 4, 4, __val)
#define SET_H2CCMD_PWRMODE_PARM_AWAKE_INTERVAL(__cmd, __val)	\
	SET_BITS_TO_LE_1BYTE((__cmd)+2, 0, 8, __val)
#define SET_H2CCMD_PWRMODE_PARM_ALL_QUEUE_UAPSD(__cmd, __val)	\
	SET_BITS_TO_LE_1BYTE((__cmd)+3, 0, 8, __val)
#define SET_H2CCMD_PWRMODE_PARM_PWR_STATE(__cmd, __val)		\
	SET_BITS_TO_LE_1BYTE((__cmd)+4, 0, 8, __val)
#define GET_92E_H2CCMD_PWRMODE_PARM_MODE(__cmd)			\
	LE_BITS_TO_1BYTE(__cmd, 0, 8)

#define SET_H2CCMD_JOINBSSRPT_PARM_OPMODE(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 0, 8, __val)
#define SET_H2CCMD_RSVDPAGE_LOC_PROBE_RSP(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 0, 8, __val)
#define SET_H2CCMD_RSVDPAGE_LOC_PSPOLL(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+1, 0, 8, __val)
#define SET_H2CCMD_RSVDPAGE_LOC_NULL_DATA(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd)+2, 0, 8, __val)
#define SET_H2CCMD_RSVDPAGE_LOC_QOS_NULL_DATA(__ph2ccmd, __val)		\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd) + 3, 0, 8, __val)
#define SET_H2CCMD_RSVDPAGE_LOC_BT_QOS_NULL_DATA(__ph2ccmd, __val)	\
	SET_BITS_TO_LE_1BYTE((__ph2ccmd) + 4, 0, 8, __val)

/* _MEDIA_STATUS_RPT_PARM_CMD1 */
#define SET_H2CCMD_MSRRPT_PARM_OPMODE(__cmd, __val)		\
	SET_BITS_TO_LE_1BYTE(__cmd, 0, 1, __val)
#define SET_H2CCMD_MSRRPT_PARM_MACID_IND(__cmd, __val)		\
	SET_BITS_TO_LE_1BYTE(__cmd, 1, 1, __val)
#define SET_H2CCMD_MSRRPT_PARM_MACID(__cmd, __val)		\
	SET_BITS_TO_LE_1BYTE(__cmd+1, 0, 8, __val)
#define SET_H2CCMD_MSRRPT_PARM_MACID_END(__cmd, __val)		\
	SET_BITS_TO_LE_1BYTE(__cmd+2, 0, 8, __val)

int rtl92ee_download_fw(struct ieee80211_hw *hw, bool buse_wake_on_wlan_fw);
void rtl92ee_fill_h2c_cmd(struct ieee80211_hw *hw, u8 element_id,
			  u32 cmd_len, u8 *cmdbuffer);
void rtl92ee_firmware_selfreset(struct ieee80211_hw *hw);
void rtl92ee_set_fw_pwrmode_cmd(struct ieee80211_hw *hw, u8 mode);
void rtl92ee_set_fw_media_status_rpt_cmd(struct ieee80211_hw *hw, u8 mstatus);
void rtl92ee_set_fw_rsvdpagepkt(struct ieee80211_hw *hw, bool b_dl_finished);
void rtl92ee_set_p2p_ps_offload_cmd(struct ieee80211_hw *hw, u8 p2p_ps_state);
void rtl92ee_c2h_packet_handler(struct ieee80211_hw *hw, u8 *buffer, u8 len);
void rtl92ee_c2h_content_parsing(struct ieee80211_hw *hw, u8 c2h_cmd_id,
				 u8 c2h_cmd_len, u8 *tmp_buf);
#endif
