/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
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

#ifndef __RTL8822B__FW__H__
#define __RTL8822B__FW__H__

#define USE_OLD_WOWLAN_DEBUG_FW	0

#define H2C_8822B_RSVDPAGE_LOC_LEN	5
#define H2C_8822B_PWEMODE_LENGTH	7
#define H2C_8822B_JOINBSSRPT_LENGTH	1
#define H2C_8822B_AP_OFFLOAD_LENGTH	3
#define H2C_8822B_WOWLAN_LENGTH	3
#define H2C_8822B_KEEP_ALIVE_CTRL_LENGTH	3
#if (USE_OLD_WOWLAN_DEBUG_FW == 0)
#define H2C_8822B_REMOTE_WAKE_CTRL_LEN	1
#else
#define H2C_8822B_REMOTE_WAKE_CTRL_LEN	3
#endif
#define H2C_8822B_AOAC_GLOBAL_INFO_LEN	2
#define H2C_8822B_AOAC_RSVDPAGE_LOC_LEN	7
#define H2C_DEFAULT_PORT_ID_LEN	2

/* Fw PS state for RPWM.
 *BIT[2:0] = HW state
 *BIT[3] = Protocol PS state,  1: register active state, 0: register sleep state
 *BIT[4] = sub-state
 */
#define FW_PS_RF_ON	BIT(2)
#define FW_PS_REGISTER_ACTIVE	BIT(3)

#define FW_PS_ACK	BIT(6)
#define FW_PS_TOGGLE	BIT(7)

/* 8822B RPWM value*/
/* BIT[0] = 1: 32k, 0: 40M*/
#define FW_PS_CLOCK_OFF	BIT(0) /* 32k */
#define FW_PS_CLOCK_ON	0 /* 40M */

#define FW_PS_STATE_MASK	(0x0F)
#define FW_PS_STATE_HW_MASK	(0x07)
#define FW_PS_STATE_INT_MASK	(0x3F)

#define FW_PS_STATE(x) (FW_PS_STATE_MASK & (x))

#define FW_PS_STATE_ALL_ON_8822B	(FW_PS_CLOCK_ON)
#define FW_PS_STATE_RF_ON_8822B	(FW_PS_CLOCK_ON)
#define FW_PS_STATE_RF_OFF_8822B	(FW_PS_CLOCK_ON)
#define FW_PS_STATE_RF_OFF_LOW_PWR	(FW_PS_CLOCK_OFF)

/* For 8822B H2C PwrMode Cmd ID 5.*/
#define FW_PWR_STATE_ACTIVE ((FW_PS_RF_ON) | (FW_PS_REGISTER_ACTIVE))
#define FW_PWR_STATE_RF_OFF	0

#define FW_PS_IS_ACK(x) ((x) & FW_PS_ACK)

#define IS_IN_LOW_POWER_STATE_8822B(fw_ps_state)                               \
	(FW_PS_STATE(fw_ps_state) == FW_PS_CLOCK_OFF)

#define FW_PWR_STATE_ACTIVE ((FW_PS_RF_ON) | (FW_PS_REGISTER_ACTIVE))
#define FW_PWR_STATE_RF_OFF	0

enum rtl8822b_h2c_cmd {
	H2C_8822B_RSVDPAGE	= 0,
	H2C_8822B_MSRRPT	= 1,
	H2C_8822B_SCAN	= 2,
	H2C_8822B_KEEP_ALIVE_CTRL	= 3,
	H2C_8822B_DISCONNECT_DECISION	= 4,
#if (USE_OLD_WOWLAN_DEBUG_FW == 1)
	H2C_8822B_WO_WLAN	= 5,
#endif
	H2C_8822B_INIT_OFFLOAD	= 6,
#if (USE_OLD_WOWLAN_DEBUG_FW == 1)
	H2C_8822B_REMOTE_WAKE_CTRL	= 7,
#endif
	H2C_8822B_AP_OFFLOAD	= 8,
	H2C_8822B_BCN_RSVDPAGE	= 9,
	H2C_8822B_PROBERSP_RSVDPAGE	= 10,

	H2C_8822B_SETPWRMODE	= 0x20,
	H2C_8822B_PS_TUNING_PARA	= 0x21,
	H2C_8822B_PS_TUNING_PARA2	= 0x22,
	H2C_8822B_PS_LPS_PARA	= 0x23,
	H2C_8822B_P2P_PS_OFFLOAD	= 024,
	H2C_8822B_DEFAULT_PORT_ID	= 0x2C,

#if (USE_OLD_WOWLAN_DEBUG_FW == 0)
	H2C_8822B_WO_WLAN	= 0x80,
	H2C_8822B_REMOTE_WAKE_CTRL	= 0x81,
	H2C_8822B_AOAC_GLOBAL_INFO	= 0x82,
	H2C_8822B_AOAC_RSVDPAGE	= 0x83,
#endif
	H2C_8822B_MACID_CFG	= 0x40,
	H2C_8822B_RSSI_REPORT	= 0x42,
	H2C_8822B_MACID_CFG_3SS	= 0x46,
	/*Not defined CTW CMD for P2P yet*/
	H2C_8822B_P2P_PS_CTW_CMD	= 0x99,
	MAX_8822B_H2CCMD
};

enum rtl8822b_c2h_evt {
	C2H_8822B_DBG	= 0x00,
	C2H_8822B_LB	= 0x01,
	C2H_8822B_TXBF	= 0x02,
	C2H_8822B_TX_REPORT	= 0x03,
	C2H_8822B_BT_INFO	= 0x09,
	C2H_8822B_BT_MP	= 0x0B,
	C2H_8822B_RA_RPT	= 0x0C,
	MAX_8822B_C2HEVENT
};

/* H2C: 0x20 */
#define SET_H2CCMD_PWRMODE_PARM_MODE(__ph2ccmd, __val)                         \
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 0, 7, __val)
#define SET_H2CCMD_PWRMODE_PARM_CLK_REQ(__ph2ccmd, __val)                      \
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 7, 1, __val)
#define SET_H2CCMD_PWRMODE_PARM_RLBM(__ph2ccmd, __val)                         \
	SET_BITS_TO_LE_1BYTE((__ph2ccmd) + 1, 0, 4, __val)
#define SET_H2CCMD_PWRMODE_PARM_SMART_PS(__ph2ccmd, __val)                     \
	SET_BITS_TO_LE_1BYTE((__ph2ccmd) + 1, 4, 4, __val)
#define SET_H2CCMD_PWRMODE_PARM_AWAKE_INTERVAL(__ph2ccmd, __val)               \
	SET_BITS_TO_LE_1BYTE((__ph2ccmd) + 2, 0, 8, __val)
#define SET_H2CCMD_PWRMODE_PARM_ALL_QUEUE_UAPSD(__ph2ccmd, __val)              \
	SET_BITS_TO_LE_1BYTE((__ph2ccmd) + 3, 0, 1, __val)
#define SET_H2CCMD_PWRMODE_PARM_BCN_EARLY_RPT(__ph2ccmd, __val)                \
	SET_BITS_TO_LE_1BYTE((__ph2ccmd) + 3, 2, 1, __val)
#define SET_H2CCMD_PWRMODE_PARM_PORT_ID(__ph2ccmd, __val)                      \
	SET_BITS_TO_LE_1BYTE((__ph2ccmd) + 3, 5, 3, __val)
#define SET_H2CCMD_PWRMODE_PARM_PWR_STATE(__ph2ccmd, __val)                    \
	SET_BITS_TO_LE_1BYTE((__ph2ccmd) + 4, 0, 8, __val)
#define SET_H2CCMD_PWRMODE_PARM_BYTE5(__ph2ccmd, __val)                        \
	SET_BITS_TO_LE_1BYTE((__ph2ccmd) + 5, 0, 8, __val)

/* H2C: 0x00 */
#define SET_H2CCMD_RSVDPAGE_LOC_PROBE_RSP(__ph2ccmd, __val)                    \
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 0, 8, __val)
#define SET_H2CCMD_RSVDPAGE_LOC_PSPOLL(__ph2ccmd, __val)                       \
	SET_BITS_TO_LE_1BYTE((__ph2ccmd) + 1, 0, 8, __val)
#define SET_H2CCMD_RSVDPAGE_LOC_NULL_DATA(__ph2ccmd, __val)                    \
	SET_BITS_TO_LE_1BYTE((__ph2ccmd) + 2, 0, 8, __val)
#define SET_H2CCMD_RSVDPAGE_LOC_QOS_NULL_DATA(__ph2ccmd, __val)                \
	SET_BITS_TO_LE_1BYTE((__ph2ccmd) + 3, 0, 8, __val)
#define SET_H2CCMD_RSVDPAGE_LOC_BT_QOS_NULL_DATA(__ph2ccmd, __val)             \
	SET_BITS_TO_LE_1BYTE((__ph2ccmd) + 4, 0, 8, __val)

/* H2C: 0x01 */
#define SET_H2CCMD_MSRRPT_PARM_OPMODE(__ph2ccmd, __val)                        \
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 0, 1, __val)
#define SET_H2CCMD_MSRRPT_PARM_MACID_IND(__ph2ccmd, __val)                     \
	SET_BITS_TO_LE_1BYTE(__ph2ccmd, 1, 1, __val)
#define SET_H2CCMD_MSRRPT_PARM_MACID(__ph2ccmd, __val)                         \
	SET_BITS_TO_LE_1BYTE(__ph2ccmd + 1, 0, 8, __val)
#define SET_H2CCMD_MSRRPT_PARM_MACID_END(__ph2ccmd, __val)                     \
	SET_BITS_TO_LE_1BYTE(__ph2ccmd + 2, 0, 8, __val)

/* H2C: 0x2C */
#define SET_H2CCMD_DFTPID_PORT_ID(__ph2ccmd, __val)                            \
	SET_BITS_TO_LE_1BYTE(((u8 *)(__ph2ccmd)), 0, 8, (__val))
#define SET_H2CCMD_DFTPID_MAC_ID(__ph2ccmd, __val)                             \
	SET_BITS_TO_LE_1BYTE(((u8 *)(__ph2ccmd)) + 1, 0, 8, (__val))

void rtl8822be_fill_h2c_cmd(struct ieee80211_hw *hw, u8 element_id, u32 cmd_len,
			    u8 *cmdbuffer);
void rtl8822be_set_default_port_id_cmd(struct ieee80211_hw *hw);
void rtl8822be_set_fw_pwrmode_cmd(struct ieee80211_hw *hw, u8 mode);
void rtl8822be_set_fw_media_status_rpt_cmd(struct ieee80211_hw *hw, u8 mstatus);
void rtl8822be_set_fw_rsvdpagepkt(struct ieee80211_hw *hw, bool b_dl_finished);
void rtl8822be_set_p2p_ps_offload_cmd(struct ieee80211_hw *hw, u8 p2p_ps_state);
void rtl8822be_c2h_packet_handler(struct ieee80211_hw *hw, u8 *buffer, u8 len);
void rtl8822be_c2h_content_parsing(struct ieee80211_hw *hw, u8 c2h_cmd_id,
				   u8 c2h_cmd_len, u8 *tmp_buf);
bool rtl8822b_halmac_cb_write_data_rsvd_page(struct rtl_priv *rtlpriv, u8 *buf,
					     u32 size);
bool rtl8822b_halmac_cb_write_data_h2c(struct rtl_priv *rtlpriv, u8 *buf,
				       u32 size);
#endif
