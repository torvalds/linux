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
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ****************************************************************************
 */

#ifndef __RTL8723E_HAL_BTC_H__
#define __RTL8723E_HAL_BTC_H__

#include "../wifi.h"
#include "btc.h"
#include "hal_bt_coexist.h"

#define	BT_TXRX_CNT_THRES_1		1200
#define	BT_TXRX_CNT_THRES_2		1400
#define	BT_TXRX_CNT_THRES_3		3000
#define	BT_TXRX_CNT_LEVEL_0		0	/* < 1200 */
#define	BT_TXRX_CNT_LEVEL_1		1	/* >= 1200 && < 1400 */
#define	BT_TXRX_CNT_LEVEL_2		2	/* >= 1400 */
#define	BT_TXRX_CNT_LEVEL_3		3

/* TDMA mode definition */
#define	TDMA_2ANT		0
#define	TDMA_1ANT		1
#define	TDMA_NAV_OFF		0
#define	TDMA_NAV_ON		1
#define	TDMA_DAC_SWING_OFF	0
#define	TDMA_DAC_SWING_ON	1

/* PTA mode related definition */
#define	BT_PTA_MODE_OFF		0
#define	BT_PTA_MODE_ON		1

/* Penalty Tx Rate Adaptive */
#define	BT_TX_RATE_ADAPTIVE_NORMAL	0
#define	BT_TX_RATE_ADAPTIVE_LOW_PENALTY	1

/* RF Corner */
#define	BT_RF_RX_LPF_CORNER_RESUME	0
#define	BT_RF_RX_LPF_CORNER_SHRINK	1

#define C2H_EVT_HOST_CLOSE		0x00
#define C2H_EVT_FW_CLOSE		0xFF

enum bt_traffic_mode {
	BT_MOTOR_EXT_BE = 0x00,
	BT_MOTOR_EXT_GUL = 0x01,
	BT_MOTOR_EXT_GUB = 0x02,
	BT_MOTOR_EXT_GULB = 0x03
};

enum bt_traffic_mode_profile {
	BT_PROFILE_NONE,
	BT_PROFILE_A2DP,
	BT_PROFILE_PAN,
	BT_PROFILE_HID,
	BT_PROFILE_SCO
};

enum hci_ext_bt_operation {
	HCI_BT_OP_NONE = 0x0,
	HCI_BT_OP_INQUIRE_START	= 0x1,
	HCI_BT_OP_INQUIRE_FINISH = 0x2,
	HCI_BT_OP_PAGING_START = 0x3,
	HCI_BT_OP_PAGING_SUCCESS = 0x4,
	HCI_BT_OP_PAGING_UNSUCCESS = 0x5,
	HCI_BT_OP_PAIRING_START = 0x6,
	HCI_BT_OP_PAIRING_FINISH = 0x7,
	HCI_BT_OP_BT_DEV_ENABLE = 0x8,
	HCI_BT_OP_BT_DEV_DISABLE = 0x9,
	HCI_BT_OP_MAX,
};

enum bt_spec {
	BT_SPEC_1_0_b = 0x00,
	BT_SPEC_1_1 = 0x01,
	BT_SPEC_1_2 = 0x02,
	BT_SPEC_2_0_EDR = 0x03,
	BT_SPEC_2_1_EDR = 0x04,
	BT_SPEC_3_0_HS = 0x05,
	BT_SPEC_4_0 = 0x06
};

struct c2h_evt_hdr {
	u8 cmd_id;
	u8 cmd_len;
	u8 cmd_seq;
};

enum bt_state {
	BT_INFO_STATE_DISABLED = 0,
	BT_INFO_STATE_NO_CONNECTION = 1,
	BT_INFO_STATE_CONNECT_IDLE = 2,
	BT_INFO_STATE_INQ_OR_PAG = 3,
	BT_INFO_STATE_ACL_ONLY_BUSY = 4,
	BT_INFO_STATE_SCO_ONLY_BUSY = 5,
	BT_INFO_STATE_ACL_SCO_BUSY = 6,
	BT_INFO_STATE_HID_BUSY = 7,
	BT_INFO_STATE_HID_SCO_BUSY = 8,
	BT_INFO_STATE_MAX = 7
};

enum rtl8723ae_c2h_evt {
	C2H_DBG = 0,
	C2H_TSF = 1,
	C2H_AP_RPT_RSP = 2,
	C2H_CCX_TX_RPT = 3,	/* The FW notify the report of the specific */
				/* tx packet. */
	C2H_BT_RSSI = 4,
	C2H_BT_OP_MODE = 5,
	C2H_HW_INFO_EXCH = 10,
	C2H_C2H_H2C_TEST = 11,
	BT_INFO = 12,
	MAX_C2HEVENT
};

void rtl8723ae_dm_bt_fw_coex_all_off_8723a(struct ieee80211_hw *hw);
void rtl8723ae_dm_bt_sw_coex_all_off_8723a(struct ieee80211_hw *hw);
void rtl8723ae_dm_bt_hw_coex_all_off_8723a(struct ieee80211_hw *hw);
void rtl8723ae_dm_bt_coexist_8723(struct ieee80211_hw *hw);
void rtl8723ae_dm_bt_set_bt_dm(struct ieee80211_hw *hw,
			      struct btdm_8723 *p_btdm);
void rtl_8723e_c2h_command_handle(struct ieee80211_hw *hw);
void rtl_8723e_bt_wifi_media_status_notify(struct ieee80211_hw *hw,
					   bool mstatus);
void rtl8723ae_bt_coex_off_before_lps(struct ieee80211_hw *hw);

#endif
