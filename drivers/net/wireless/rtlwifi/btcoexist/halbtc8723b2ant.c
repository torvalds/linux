/******************************************************************************
 *
 * Copyright(c) 2012  Realtek Corporation.
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
/***************************************************************
 * Description:
 *
 * This file is for RTL8723B Co-exist mechanism
 *
 * History
 * 2012/11/15 Cosa first check in.
 *
 **************************************************************/
/**************************************************************
 * include files
 **************************************************************/
#include "halbt_precomp.h"
/**************************************************************
 * Global variables, these are static variables
 **************************************************************/
static struct coex_dm_8723b_2ant glcoex_dm_8723b_2ant;
static struct coex_dm_8723b_2ant *coex_dm = &glcoex_dm_8723b_2ant;
static struct coex_sta_8723b_2ant glcoex_sta_8723b_2ant;
static struct coex_sta_8723b_2ant *coex_sta = &glcoex_sta_8723b_2ant;

static const char *const glbt_info_src_8723b_2ant[] = {
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

static u32 glcoex_ver_date_8723b_2ant = 20130731;
static u32 glcoex_ver_8723b_2ant = 0x3b;

/**************************************************************
 * local function proto type if needed
 **************************************************************/
/**************************************************************
 * local function start with btc8723b2ant_
 **************************************************************/
static u8 btc8723b2ant_bt_rssi_state(u8 level_num, u8 rssi_thresh,
				     u8 rssi_thresh1)
{
	s32 bt_rssi = 0;
	u8 bt_rssi_state = coex_sta->pre_bt_rssi_state;

	bt_rssi = coex_sta->bt_rssi;

	if (level_num == 2) {
		if ((coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_STAY_LOW)) {
			if (bt_rssi >= rssi_thresh +
				       BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT) {
				bt_rssi_state = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "switch to High\n");
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "stay at Low\n");
			}
		} else {
			if (bt_rssi < rssi_thresh) {
				bt_rssi_state = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "switch to Low\n");
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "stay at High\n");
			}
		}
	} else if (level_num == 3) {
		if (rssi_thresh > rssi_thresh1) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
				  "[BTCoex], BT Rssi thresh error!!\n");
			return coex_sta->pre_bt_rssi_state;
		}

		if ((coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_STAY_LOW)) {
			if (bt_rssi >= rssi_thresh +
				       BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT) {
				bt_rssi_state = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "switch to Medium\n");
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "stay at Low\n");
			}
		} else if ((coex_sta->pre_bt_rssi_state ==
						BTC_RSSI_STATE_MEDIUM) ||
			   (coex_sta->pre_bt_rssi_state ==
						BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (bt_rssi >= rssi_thresh1 +
				       BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT) {
				bt_rssi_state = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "switch to High\n");
			} else if (bt_rssi < rssi_thresh) {
				bt_rssi_state = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "switch to Low\n");
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "stay at Medium\n");
			}
		} else {
			if (bt_rssi < rssi_thresh1) {
				bt_rssi_state = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "switch to Medium\n");
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "stay at High\n");
			}
		}
	}

	coex_sta->pre_bt_rssi_state = bt_rssi_state;

	return bt_rssi_state;
}

static u8 btc8723b2ant_wifi_rssi_state(struct btc_coexist *btcoexist,
				       u8 index, u8 level_num,
				       u8 rssi_thresh, u8 rssi_thresh1)
{
	s32 wifi_rssi = 0;
	u8 wifi_rssi_state = coex_sta->pre_wifi_rssi_state[index];

	btcoexist->btc_get(btcoexist, BTC_GET_S4_WIFI_RSSI, &wifi_rssi);

	if (level_num == 2) {
		if ((coex_sta->pre_wifi_rssi_state[index] ==
						BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_wifi_rssi_state[index] ==
						BTC_RSSI_STATE_STAY_LOW)) {
			if (wifi_rssi >= rssi_thresh +
					 BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT) {
				wifi_rssi_state = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "switch to High\n");
			} else {
				wifi_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "stay at Low\n");
			}
		} else {
			if (wifi_rssi < rssi_thresh) {
				wifi_rssi_state = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "switch to Low\n");
			} else {
				wifi_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "stay at High\n");
			}
		}
	} else if (level_num == 3) {
		if (rssi_thresh > rssi_thresh1) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE,
				  "[BTCoex], wifi RSSI thresh error!!\n");
			return coex_sta->pre_wifi_rssi_state[index];
		}

		if ((coex_sta->pre_wifi_rssi_state[index] ==
						BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_wifi_rssi_state[index] ==
						BTC_RSSI_STATE_STAY_LOW)) {
			if (wifi_rssi >= rssi_thresh +
					BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT) {
				wifi_rssi_state = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "switch to Medium\n");
			} else {
				wifi_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "stay at Low\n");
			}
		} else if ((coex_sta->pre_wifi_rssi_state[index] ==
						BTC_RSSI_STATE_MEDIUM) ||
			   (coex_sta->pre_wifi_rssi_state[index] ==
						BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (wifi_rssi >= rssi_thresh1 +
					 BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT) {
				wifi_rssi_state = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "switch to High\n");
			} else if (wifi_rssi < rssi_thresh) {
				wifi_rssi_state = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "switch to Low\n");
			} else {
				wifi_rssi_state = BTC_RSSI_STATE_STAY_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "stay at Medium\n");
			}
		} else {
			if (wifi_rssi < rssi_thresh1) {
				wifi_rssi_state = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "switch to Medium\n");
			} else {
				wifi_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "stay at High\n");
			}
		}
	}

	coex_sta->pre_wifi_rssi_state[index] = wifi_rssi_state;

	return wifi_rssi_state;
}

static void btc8723b2ant_monitor_bt_ctr(struct btc_coexist *btcoexist)
{
	u32 reg_hp_txrx, reg_lp_txrx, u32tmp;
	u32 reg_hp_tx = 0, reg_hp_rx = 0;
	u32 reg_lp_tx = 0, reg_lp_rx = 0;

	reg_hp_txrx = 0x770;
	reg_lp_txrx = 0x774;

	u32tmp = btcoexist->btc_read_4byte(btcoexist, reg_hp_txrx);
	reg_hp_tx = u32tmp & MASKLWORD;
	reg_hp_rx = (u32tmp & MASKHWORD) >> 16;

	u32tmp = btcoexist->btc_read_4byte(btcoexist, reg_lp_txrx);
	reg_lp_tx = u32tmp & MASKLWORD;
	reg_lp_rx = (u32tmp & MASKHWORD) >> 16;

	coex_sta->high_priority_tx = reg_hp_tx;
	coex_sta->high_priority_rx = reg_hp_rx;
	coex_sta->low_priority_tx = reg_lp_tx;
	coex_sta->low_priority_rx = reg_lp_rx;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR,
		  "[BTCoex], High Priority Tx/Rx(reg 0x%x)=0x%x(%d)/0x%x(%d)\n",
		  reg_hp_txrx, reg_hp_tx, reg_hp_tx, reg_hp_rx, reg_hp_rx);
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR,
		  "[BTCoex], Low Priority Tx/Rx(reg 0x%x)=0x%x(%d)/0x%x(%d)\n",
		  reg_lp_txrx, reg_lp_tx, reg_lp_tx, reg_lp_rx, reg_lp_rx);

	/* reset counter */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0xc);
}

static bool btc8723b2ant_is_wifi_status_changed(struct btc_coexist *btcoexist)
{
	static bool pre_wifi_busy;
	static bool pre_under_4way;
	static bool pre_bt_hs_on;
	bool wifi_busy = false, under_4way = false, bt_hs_on = false;
	bool wifi_connected = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS,
			   &under_4way);

	if (wifi_connected) {
		if (wifi_busy != pre_wifi_busy) {
			pre_wifi_busy = wifi_busy;
			return true;
		}

		if (under_4way != pre_under_4way) {
			pre_under_4way = under_4way;
			return true;
		}

		if (bt_hs_on != pre_bt_hs_on) {
			pre_bt_hs_on = bt_hs_on;
			return true;
		}
	}

	return false;
}

static void btc8723b2ant_update_bt_link_info(struct btc_coexist *btcoexist)
{
	/*struct btc_stack_info *stack_info = &btcoexist->stack_info;*/
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool bt_hs_on = false;

#if (BT_AUTO_REPORT_ONLY_8723B_2ANT == 1) /* profile from bt patch */
	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);

	bt_link_info->bt_link_exist = coex_sta->bt_link_exist;
	bt_link_info->sco_exist = coex_sta->sco_exist;
	bt_link_info->a2dp_exist = coex_sta->a2dp_exist;
	bt_link_info->pan_exist = coex_sta->pan_exist;
	bt_link_info->hid_exist = coex_sta->hid_exist;

	/* work around for HS mode. */
	if (bt_hs_on) {
		bt_link_info->pan_exist = true;
		bt_link_info->bt_link_exist = true;
	}
#else	/* profile from bt stack */
	bt_link_info->bt_link_exist = stack_info->bt_link_exist;
	bt_link_info->sco_exist = stack_info->sco_exist;
	bt_link_info->a2dp_exist = stack_info->a2dp_exist;
	bt_link_info->pan_exist = stack_info->pan_exist;
	bt_link_info->hid_exist = stack_info->hid_exist;

	/*for win-8 stack HID report error*/
	if (!stack_info->hid_exist)
		stack_info->hid_exist = coex_sta->hid_exist;
	/*sync  BTInfo with BT firmware and stack*/
	/* when stack HID report error, here we use the info from bt fw.*/
	if (!stack_info->bt_link_exist)
		stack_info->bt_link_exist = coex_sta->bt_link_exist;
#endif
	/* check if Sco only */
	if (bt_link_info->sco_exist && !bt_link_info->a2dp_exist &&
	    !bt_link_info->pan_exist && !bt_link_info->hid_exist)
		bt_link_info->sco_only = true;
	else
		bt_link_info->sco_only = false;

	/* check if A2dp only */
	if (!bt_link_info->sco_exist && bt_link_info->a2dp_exist &&
	    !bt_link_info->pan_exist && !bt_link_info->hid_exist)
		bt_link_info->a2dp_only = true;
	else
		bt_link_info->a2dp_only = false;

	/* check if Pan only */
	if (!bt_link_info->sco_exist && !bt_link_info->a2dp_exist &&
	    bt_link_info->pan_exist && !bt_link_info->hid_exist)
		bt_link_info->pan_only = true;
	else
		bt_link_info->pan_only = false;

	/* check if Hid only */
	if (!bt_link_info->sco_exist && !bt_link_info->a2dp_exist &&
	    !bt_link_info->pan_exist && bt_link_info->hid_exist)
		bt_link_info->hid_only = true;
	else
		bt_link_info->hid_only = false;
}

static u8 btc8723b2ant_action_algorithm(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool bt_hs_on = false;
	u8 algorithm = BT_8723B_2ANT_COEX_ALGO_UNDEFINED;
	u8 num_of_diff_profile = 0;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);

	if (!bt_link_info->bt_link_exist) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], No BT link exists!!!\n");
		return algorithm;
	}

	if (bt_link_info->sco_exist)
		num_of_diff_profile++;
	if (bt_link_info->hid_exist)
		num_of_diff_profile++;
	if (bt_link_info->pan_exist)
		num_of_diff_profile++;
	if (bt_link_info->a2dp_exist)
		num_of_diff_profile++;

	if (num_of_diff_profile == 1) {
		if (bt_link_info->sco_exist) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], SCO only\n");
			algorithm = BT_8723B_2ANT_COEX_ALGO_SCO;
		} else {
			if (bt_link_info->hid_exist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
					  "[BTCoex], HID only\n");
				algorithm = BT_8723B_2ANT_COEX_ALGO_HID;
			} else if (bt_link_info->a2dp_exist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
					  "[BTCoex], A2DP only\n");
				algorithm = BT_8723B_2ANT_COEX_ALGO_A2DP;
			} else if (bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], PAN(HS) only\n");
					algorithm =
						BT_8723B_2ANT_COEX_ALGO_PANHS;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], PAN(EDR) only\n");
					algorithm =
						BT_8723B_2ANT_COEX_ALGO_PANEDR;
				}
			}
		}
	} else if (num_of_diff_profile == 2) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
					  "[BTCoex], SCO + HID\n");
				algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
			} else if (bt_link_info->a2dp_exist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
					  "[BTCoex], SCO + A2DP ==> SCO\n");
				algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
			} else if (bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], SCO + PAN(HS)\n");
					algorithm = BT_8723B_2ANT_COEX_ALGO_SCO;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], SCO + PAN(EDR)\n");
					algorithm =
					    BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (bt_link_info->hid_exist &&
			    bt_link_info->a2dp_exist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
					  "[BTCoex], HID + A2DP\n");
				algorithm = BT_8723B_2ANT_COEX_ALGO_HID_A2DP;
			} else if (bt_link_info->hid_exist &&
				   bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], HID + PAN(HS)\n");
					algorithm = BT_8723B_2ANT_COEX_ALGO_HID;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], HID + PAN(EDR)\n");
					algorithm =
					    BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (bt_link_info->pan_exist &&
				   bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], A2DP + PAN(HS)\n");
					algorithm =
					    BT_8723B_2ANT_COEX_ALGO_A2DP_PANHS;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex],A2DP + PAN(EDR)\n");
					algorithm =
					    BT_8723B_2ANT_COEX_ALGO_PANEDR_A2DP;
				}
			}
		}
	} else if (num_of_diff_profile == 3) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist &&
			    bt_link_info->a2dp_exist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
					  "[BTCoex], SCO + HID + A2DP"
					  " ==> HID\n");
				algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
			} else if (bt_link_info->hid_exist &&
				   bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], SCO + HID + "
						  "PAN(HS)\n");
					algorithm =
					    BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], SCO + HID + "
						  "PAN(EDR)\n");
					algorithm =
					    BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (bt_link_info->pan_exist &&
				   bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], SCO + A2DP + "
						  "PAN(HS)\n");
					algorithm =
					    BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], SCO + A2DP + "
						  "PAN(EDR) ==> HID\n");
					algorithm =
					    BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (bt_link_info->hid_exist &&
			    bt_link_info->pan_exist &&
			    bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], HID + A2DP + "
						  "PAN(HS)\n");
					algorithm =
					    BT_8723B_2ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], HID + A2DP + "
						  "PAN(EDR)\n");
					algorithm =
					BT_8723B_2ANT_COEX_ALGO_HID_A2DP_PANEDR;
				}
			}
		}
	} else if (num_of_diff_profile >= 3) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist &&
			    bt_link_info->pan_exist &&
			    bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], Error!!! SCO + HID"
						  " + A2DP + PAN(HS)\n");
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], SCO + HID + A2DP +"
						  " PAN(EDR)==>PAN(EDR)+HID\n");
					algorithm =
					    BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
	}
	return algorithm;
}

static bool btc8723b_need_dec_pwr(struct btc_coexist *btcoexist)
{
	bool ret = false;
	bool bt_hs_on = false, wifi_connected = false;
	s32 bt_hs_rssi = 0;
	u8 bt_rssi_state;

	if (!btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on))
		return false;
	if (!btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
				&wifi_connected))
		return false;
	if (!btcoexist->btc_get(btcoexist, BTC_GET_S4_HS_RSSI, &bt_hs_rssi))
		return false;

	bt_rssi_state = btc8723b2ant_bt_rssi_state(2, 35, 0);

	if (wifi_connected) {
		if (bt_hs_on) {
			if (bt_hs_rssi > 37) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
					  "[BTCoex], Need to decrease bt "
					  "power for HS mode!!\n");
				ret = true;
			}
		} else {
			if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
			    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
					  "[BTCoex], Need to decrease bt "
					  "power for Wifi is connected!!\n");
				ret = true;
			}
		}
	}

	return ret;
}

static void btc8723b2ant_set_fw_dac_swing_level(struct btc_coexist *btcoexist,
						u8 dac_swing_lvl)
{
	u8 h2c_parameter[1] = {0};

	/* There are several type of dacswing
	 * 0x18/ 0x10/ 0xc/ 0x8/ 0x4/ 0x6
	 */
	h2c_parameter[0] = dac_swing_lvl;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], Set Dac Swing Level=0x%x\n", dac_swing_lvl);
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], FW write 0x64=0x%x\n", h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x64, 1, h2c_parameter);
}

static void btc8723b2ant_set_fw_dec_bt_pwr(struct btc_coexist *btcoexist,
					   bool dec_bt_pwr)
{
	u8 h2c_parameter[1] = {0};

	h2c_parameter[0] = 0;

	if (dec_bt_pwr)
		h2c_parameter[0] |= BIT1;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], decrease Bt Power : %s, FW write 0x62=0x%x\n",
		  (dec_bt_pwr ? "Yes!!" : "No!!"), h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x62, 1, h2c_parameter);
}

static void btc8723b2ant_dec_bt_pwr(struct btc_coexist *btcoexist,
				    bool force_exec, bool dec_bt_pwr)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
		  "[BTCoex], %s Dec BT power = %s\n",
		  (force_exec ? "force to" : ""), (dec_bt_pwr ? "ON" : "OFF"));
	coex_dm->cur_dec_bt_pwr = dec_bt_pwr;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], bPreDecBtPwr=%d, bCurDecBtPwr=%d\n",
			  coex_dm->pre_dec_bt_pwr, coex_dm->cur_dec_bt_pwr);

		if (coex_dm->pre_dec_bt_pwr == coex_dm->cur_dec_bt_pwr)
			return;
	}
	btc8723b2ant_set_fw_dec_bt_pwr(btcoexist, coex_dm->cur_dec_bt_pwr);

	coex_dm->pre_dec_bt_pwr = coex_dm->cur_dec_bt_pwr;
}

static void btc8723b2ant_fw_dac_swing_lvl(struct btc_coexist *btcoexist,
					  bool force_exec, u8 fw_dac_swing_lvl)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
		  "[BTCoex], %s set FW Dac Swing level = %d\n",
		  (force_exec ? "force to" : ""), fw_dac_swing_lvl);
	coex_dm->cur_fw_dac_swing_lvl = fw_dac_swing_lvl;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], preFwDacSwingLvl=%d, "
			  "curFwDacSwingLvl=%d\n",
			  coex_dm->pre_fw_dac_swing_lvl,
			  coex_dm->cur_fw_dac_swing_lvl);

		if (coex_dm->pre_fw_dac_swing_lvl ==
		   coex_dm->cur_fw_dac_swing_lvl)
			return;
	}

	btc8723b2ant_set_fw_dac_swing_level(btcoexist,
					    coex_dm->cur_fw_dac_swing_lvl);
	coex_dm->pre_fw_dac_swing_lvl = coex_dm->cur_fw_dac_swing_lvl;
}

static void btc8723b2ant_set_sw_rf_rx_lpf_corner(struct btc_coexist *btcoexist,
						 bool rx_rf_shrink_on)
{
	if (rx_rf_shrink_on) {
		/* Shrink RF Rx LPF corner */
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
			  "[BTCoex], Shrink RF Rx LPF corner!!\n");
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1e,
					  0xfffff, 0xffffc);
	} else {
		/* Resume RF Rx LPF corner */
		/* After initialized, we can use coex_dm->btRf0x1eBackup */
		if (btcoexist->initilized) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
				  "[BTCoex], Resume RF Rx LPF corner!!\n");
			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1e,
						  0xfffff,
						  coex_dm->bt_rf0x1e_backup);
		}
	}
}

static void btc8723b2ant_rf_shrink(struct btc_coexist *btcoexist,
				   bool force_exec, bool rx_rf_shrink_on)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW,
		  "[BTCoex], %s turn Rx RF Shrink = %s\n",
		  (force_exec ? "force to" : ""), (rx_rf_shrink_on ?
		  "ON" : "OFF"));
	coex_dm->cur_rf_rx_lpf_shrink = rx_rf_shrink_on;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL,
			  "[BTCoex], bPreRfRxLpfShrink=%d, "
			  "bCurRfRxLpfShrink=%d\n",
			  coex_dm->pre_rf_rx_lpf_shrink,
			  coex_dm->cur_rf_rx_lpf_shrink);

		if (coex_dm->pre_rf_rx_lpf_shrink ==
		    coex_dm->cur_rf_rx_lpf_shrink)
			return;
	}
	btc8723b2ant_set_sw_rf_rx_lpf_corner(btcoexist,
					     coex_dm->cur_rf_rx_lpf_shrink);

	coex_dm->pre_rf_rx_lpf_shrink = coex_dm->cur_rf_rx_lpf_shrink;
}

static void btc8723b_set_penalty_txrate(struct btc_coexist *btcoexist,
					bool low_penalty_ra)
{
	u8 h2c_parameter[6] = {0};

	h2c_parameter[0] = 0x6;	/* opCode, 0x6= Retry_Penalty*/

	if (low_penalty_ra) {
		h2c_parameter[1] |= BIT0;
		/*normal rate except MCS7/6/5, OFDM54/48/36*/
		h2c_parameter[2] = 0x00;
		h2c_parameter[3] = 0xf7;  /*MCS7 or OFDM54*/
		h2c_parameter[4] = 0xf8;  /*MCS6 or OFDM48*/
		h2c_parameter[5] = 0xf9;  /*MCS5 or OFDM36*/
	}

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], set WiFi Low-Penalty Retry: %s",
		  (low_penalty_ra ? "ON!!" : "OFF!!"));

	btcoexist->btc_fill_h2c(btcoexist, 0x69, 6, h2c_parameter);
}

static void btc8723b2ant_low_penalty_ra(struct btc_coexist *btcoexist,
					bool force_exec, bool low_penalty_ra)
{
	/*return; */
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW,
		  "[BTCoex], %s turn LowPenaltyRA = %s\n",
		  (force_exec ? "force to" : ""), (low_penalty_ra ?
		  "ON" : "OFF"));
	coex_dm->cur_low_penalty_ra = low_penalty_ra;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL,
			  "[BTCoex], bPreLowPenaltyRa=%d, "
			  "bCurLowPenaltyRa=%d\n",
			  coex_dm->pre_low_penalty_ra,
			  coex_dm->cur_low_penalty_ra);

		if (coex_dm->pre_low_penalty_ra == coex_dm->cur_low_penalty_ra)
			return;
	}
	btc8723b_set_penalty_txrate(btcoexist, coex_dm->cur_low_penalty_ra);

	coex_dm->pre_low_penalty_ra = coex_dm->cur_low_penalty_ra;
}

static void btc8723b2ant_set_dac_swing_reg(struct btc_coexist *btcoexist,
					   u32 level)
{
	u8 val = (u8) level;
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
		  "[BTCoex], Write SwDacSwing = 0x%x\n", level);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x883, 0x3e, val);
}

static void btc8723b2ant_set_sw_fulltime_dac_swing(struct btc_coexist *btcoex,
						   bool sw_dac_swing_on,
						   u32 sw_dac_swing_lvl)
{
	if (sw_dac_swing_on)
		btc8723b2ant_set_dac_swing_reg(btcoex, sw_dac_swing_lvl);
	else
		btc8723b2ant_set_dac_swing_reg(btcoex, 0x18);
}


static void btc8723b2ant_dac_swing(struct btc_coexist *btcoexist,
				   bool force_exec, bool dac_swing_on,
				   u32 dac_swing_lvl)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW,
		  "[BTCoex], %s turn DacSwing=%s, dac_swing_lvl=0x%x\n",
		  (force_exec ? "force to" : ""),
		  (dac_swing_on ? "ON" : "OFF"), dac_swing_lvl);
	coex_dm->cur_dac_swing_on = dac_swing_on;
	coex_dm->cur_dac_swing_lvl = dac_swing_lvl;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL,
			  "[BTCoex], bPreDacSwingOn=%d, preDacSwingLvl=0x%x,"
			  " bCurDacSwingOn=%d, curDacSwingLvl=0x%x\n",
			  coex_dm->pre_dac_swing_on, coex_dm->pre_dac_swing_lvl,
			  coex_dm->cur_dac_swing_on,
			  coex_dm->cur_dac_swing_lvl);

		if ((coex_dm->pre_dac_swing_on == coex_dm->cur_dac_swing_on) &&
		    (coex_dm->pre_dac_swing_lvl == coex_dm->cur_dac_swing_lvl))
			return;
	}
	mdelay(30);
	btc8723b2ant_set_sw_fulltime_dac_swing(btcoexist, dac_swing_on,
					       dac_swing_lvl);

	coex_dm->pre_dac_swing_on = coex_dm->cur_dac_swing_on;
	coex_dm->pre_dac_swing_lvl = coex_dm->cur_dac_swing_lvl;
}

static void btc8723b2ant_set_agc_table(struct btc_coexist *btcoexist,
				       bool agc_table_en)
{
	u8 rssi_adjust_val = 0;

	/*  BB AGC Gain Table */
	if (agc_table_en) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
			  "[BTCoex], BB Agc Table On!\n");
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x6e1A0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x6d1B0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x6c1C0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x6b1D0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x6a1E0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x691F0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x68200001);
	} else {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
			  "[BTCoex], BB Agc Table Off!\n");
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0xaa1A0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0xa91B0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0xa81C0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0xa71D0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0xa61E0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0xa51F0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0xa4200001);
	}


	/* RF Gain */
	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xef, 0xfffff, 0x02000);
	if (agc_table_en) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
			  "[BTCoex], Agc Table On!\n");
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b,
					  0xfffff, 0x38fff);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b,
					  0xfffff, 0x38ffe);
	} else {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
			  "[BTCoex], Agc Table Off!\n");
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b,
					  0xfffff, 0x380c3);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b,
					  0xfffff, 0x28ce6);
	}
	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xef, 0xfffff, 0x0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xed, 0xfffff, 0x1);

	if (agc_table_en) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
			  "[BTCoex], Agc Table On!\n");
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x40,
					  0xfffff, 0x38fff);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x40,
					  0xfffff, 0x38ffe);
	} else {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
			  "[BTCoex], Agc Table Off!\n");
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x40,
					  0xfffff, 0x380c3);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x40,
					  0xfffff, 0x28ce6);
	}
	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xed, 0xfffff, 0x0);

	/* set rssiAdjustVal for wifi module. */
	if (agc_table_en)
		rssi_adjust_val = 8;
	btcoexist->btc_set(btcoexist, BTC_SET_U1_RSSI_ADJ_VAL_FOR_AGC_TABLE_ON,
			   &rssi_adjust_val);
}

static void btc8723b2ant_agc_table(struct btc_coexist *btcoexist,
				   bool force_exec, bool agc_table_en)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW,
		  "[BTCoex], %s %s Agc Table\n",
		  (force_exec ? "force to" : ""),
		  (agc_table_en ? "Enable" : "Disable"));
	coex_dm->cur_agc_table_en = agc_table_en;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL,
			  "[BTCoex], bPreAgcTableEn=%d, bCurAgcTableEn=%d\n",
			  coex_dm->pre_agc_table_en, coex_dm->cur_agc_table_en);

		if (coex_dm->pre_agc_table_en == coex_dm->cur_agc_table_en)
			return;
	}
	btc8723b2ant_set_agc_table(btcoexist, agc_table_en);

	coex_dm->pre_agc_table_en = coex_dm->cur_agc_table_en;
}

static void btc8723b2ant_set_coex_table(struct btc_coexist *btcoexist,
					u32 val0x6c0, u32 val0x6c4,
					u32 val0x6c8, u8 val0x6cc)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
		  "[BTCoex], set coex table, set 0x6c0=0x%x\n", val0x6c0);
	btcoexist->btc_write_4byte(btcoexist, 0x6c0, val0x6c0);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
		  "[BTCoex], set coex table, set 0x6c4=0x%x\n", val0x6c4);
	btcoexist->btc_write_4byte(btcoexist, 0x6c4, val0x6c4);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
		  "[BTCoex], set coex table, set 0x6c8=0x%x\n", val0x6c8);
	btcoexist->btc_write_4byte(btcoexist, 0x6c8, val0x6c8);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
		  "[BTCoex], set coex table, set 0x6cc=0x%x\n", val0x6cc);
	btcoexist->btc_write_1byte(btcoexist, 0x6cc, val0x6cc);
}

static void btc8723b2ant_coex_table(struct btc_coexist *btcoexist,
				    bool force_exec, u32 val0x6c0,
				    u32 val0x6c4, u32 val0x6c8,
				    u8 val0x6cc)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW,
		  "[BTCoex], %s write Coex Table 0x6c0=0x%x,"
		  " 0x6c4=0x%x, 0x6c8=0x%x, 0x6cc=0x%x\n",
		  (force_exec ? "force to" : ""), val0x6c0,
		  val0x6c4, val0x6c8, val0x6cc);
	coex_dm->cur_val0x6c0 = val0x6c0;
	coex_dm->cur_val0x6c4 = val0x6c4;
	coex_dm->cur_val0x6c8 = val0x6c8;
	coex_dm->cur_val0x6cc = val0x6cc;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL,
			  "[BTCoex], preVal0x6c0=0x%x, "
			  "preVal0x6c4=0x%x, preVal0x6c8=0x%x, "
			  "preVal0x6cc=0x%x !!\n",
			  coex_dm->pre_val0x6c0, coex_dm->pre_val0x6c4,
			  coex_dm->pre_val0x6c8, coex_dm->pre_val0x6cc);
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL,
			  "[BTCoex], curVal0x6c0=0x%x, "
			  "curVal0x6c4=0x%x, curVal0x6c8=0x%x, "
			  "curVal0x6cc=0x%x !!\n",
			  coex_dm->cur_val0x6c0, coex_dm->cur_val0x6c4,
			  coex_dm->cur_val0x6c8, coex_dm->cur_val0x6cc);

		if ((coex_dm->pre_val0x6c0 == coex_dm->cur_val0x6c0) &&
		    (coex_dm->pre_val0x6c4 == coex_dm->cur_val0x6c4) &&
		    (coex_dm->pre_val0x6c8 == coex_dm->cur_val0x6c8) &&
		    (coex_dm->pre_val0x6cc == coex_dm->cur_val0x6cc))
			return;
	}
	btc8723b2ant_set_coex_table(btcoexist, val0x6c0, val0x6c4,
				    val0x6c8, val0x6cc);

	coex_dm->pre_val0x6c0 = coex_dm->cur_val0x6c0;
	coex_dm->pre_val0x6c4 = coex_dm->cur_val0x6c4;
	coex_dm->pre_val0x6c8 = coex_dm->cur_val0x6c8;
	coex_dm->pre_val0x6cc = coex_dm->cur_val0x6cc;
}

static void btc8723b_coex_tbl_type(struct btc_coexist *btcoexist,
				   bool force_exec, u8 type)
{
	switch (type) {
	case 0:
		btc8723b2ant_coex_table(btcoexist, force_exec, 0x55555555,
					0x55555555, 0xffff, 0x3);
		break;
	case 1:
		btc8723b2ant_coex_table(btcoexist, force_exec, 0x55555555,
					0x5afa5afa, 0xffff, 0x3);
		break;
	case 2:
		btc8723b2ant_coex_table(btcoexist, force_exec, 0x5a5a5a5a,
					0x5a5a5a5a, 0xffff, 0x3);
		break;
	case 3:
		btc8723b2ant_coex_table(btcoexist, force_exec, 0xaaaaaaaa,
					0xaaaaaaaa, 0xffff, 0x3);
		break;
	case 4:
		btc8723b2ant_coex_table(btcoexist, force_exec, 0xffffffff,
					0xffffffff, 0xffff, 0x3);
		break;
	case 5:
		btc8723b2ant_coex_table(btcoexist, force_exec, 0x5fff5fff,
					0x5fff5fff, 0xffff, 0x3);
		break;
	case 6:
		btc8723b2ant_coex_table(btcoexist, force_exec, 0x55ff55ff,
					0x5a5a5a5a, 0xffff, 0x3);
		break;
	case 7:
		btc8723b2ant_coex_table(btcoexist, force_exec, 0x55ff55ff,
					0x5afa5afa, 0xffff, 0x3);
		break;
	case 8:
		btc8723b2ant_coex_table(btcoexist, force_exec, 0x5aea5aea,
					0x5aea5aea, 0xffff, 0x3);
		break;
	case 9:
		btc8723b2ant_coex_table(btcoexist, force_exec, 0x55ff55ff,
					0x5aea5aea, 0xffff, 0x3);
		break;
	case 10:
		btc8723b2ant_coex_table(btcoexist, force_exec, 0x55ff55ff,
					0x5aff5aff, 0xffff, 0x3);
		break;
	case 11:
		btc8723b2ant_coex_table(btcoexist, force_exec, 0x55ff55ff,
					0x5a5f5a5f, 0xffff, 0x3);
		break;
	case 12:
		btc8723b2ant_coex_table(btcoexist, force_exec, 0x55ff55ff,
					0x5f5f5f5f, 0xffff, 0x3);
		break;
	default:
		break;
	}
}

static void btc8723b2ant_set_fw_ignore_wlan_act(struct btc_coexist *btcoexist,
						bool enable)
{
	u8 h2c_parameter[1] = {0};

	if (enable)
		h2c_parameter[0] |= BIT0;/* function enable*/

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], set FW for BT Ignore Wlan_Act, "
		  "FW write 0x63=0x%x\n", h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x63, 1, h2c_parameter);
}

static void btc8723b2ant_ignore_wlan_act(struct btc_coexist *btcoexist,
					 bool force_exec, bool enable)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
		  "[BTCoex], %s turn Ignore WlanAct %s\n",
		  (force_exec ? "force to" : ""), (enable ? "ON" : "OFF"));
	coex_dm->cur_ignore_wlan_act = enable;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], bPreIgnoreWlanAct = %d, "
			  "bCurIgnoreWlanAct = %d!!\n",
			  coex_dm->pre_ignore_wlan_act,
			  coex_dm->cur_ignore_wlan_act);

		if (coex_dm->pre_ignore_wlan_act ==
		    coex_dm->cur_ignore_wlan_act)
			return;
	}
	btc8723b2ant_set_fw_ignore_wlan_act(btcoexist, enable);

	coex_dm->pre_ignore_wlan_act = coex_dm->cur_ignore_wlan_act;
}

static void btc8723b2ant_set_fw_ps_tdma(struct btc_coexist *btcoexist, u8 byte1,
					u8 byte2, u8 byte3, u8 byte4, u8 byte5)
{
	u8 h2c_parameter[5];

	h2c_parameter[0] = byte1;
	h2c_parameter[1] = byte2;
	h2c_parameter[2] = byte3;
	h2c_parameter[3] = byte4;
	h2c_parameter[4] = byte5;

	coex_dm->ps_tdma_para[0] = byte1;
	coex_dm->ps_tdma_para[1] = byte2;
	coex_dm->ps_tdma_para[2] = byte3;
	coex_dm->ps_tdma_para[3] = byte4;
	coex_dm->ps_tdma_para[4] = byte5;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], FW write 0x60(5bytes)=0x%x%08x\n",
		  h2c_parameter[0],
		  h2c_parameter[1] << 24 | h2c_parameter[2] << 16 |
		  h2c_parameter[3] << 8 | h2c_parameter[4]);

	btcoexist->btc_fill_h2c(btcoexist, 0x60, 5, h2c_parameter);
}

static void btc8723b2ant_sw_mechanism1(struct btc_coexist *btcoexist,
				       bool shrink_rx_lpf, bool low_penalty_ra,
				       bool limited_dig, bool bt_lna_constrain)
{
	btc8723b2ant_rf_shrink(btcoexist, NORMAL_EXEC, shrink_rx_lpf);
	btc8723b2ant_low_penalty_ra(btcoexist, NORMAL_EXEC, low_penalty_ra);
}

static void btc8723b2ant_sw_mechanism2(struct btc_coexist *btcoexist,
				       bool agc_table_shift, bool adc_backoff,
				       bool sw_dac_swing, u32 dac_swing_lvl)
{
	btc8723b2ant_agc_table(btcoexist, NORMAL_EXEC, agc_table_shift);
	btc8723b2ant_dac_swing(btcoexist, NORMAL_EXEC, sw_dac_swing,
			       dac_swing_lvl);
}

static void btc8723b2ant_ps_tdma(struct btc_coexist *btcoexist, bool force_exec,
			     bool turn_on, u8 type)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
		  "[BTCoex], %s turn %s PS TDMA, type=%d\n",
		  (force_exec ? "force to" : ""),
		  (turn_on ? "ON" : "OFF"), type);
	coex_dm->cur_ps_tdma_on = turn_on;
	coex_dm->cur_ps_tdma = type;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], bPrePsTdmaOn = %d, bCurPsTdmaOn = %d!!\n",
			  coex_dm->pre_ps_tdma_on, coex_dm->cur_ps_tdma_on);
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], prePsTdma = %d, curPsTdma = %d!!\n",
			  coex_dm->pre_ps_tdma, coex_dm->cur_ps_tdma);

		if ((coex_dm->pre_ps_tdma_on == coex_dm->cur_ps_tdma_on) &&
		    (coex_dm->pre_ps_tdma == coex_dm->cur_ps_tdma))
			return;
	}
	if (turn_on) {
		switch (type) {
		case 1:
		default:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x1a,
						    0x1a, 0xe1, 0x90);
			break;
		case 2:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x12,
						    0x12, 0xe1, 0x90);
			break;
		case 3:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x1c,
						    0x3, 0xf1, 0x90);
			break;
		case 4:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x10,
						    0x03, 0xf1, 0x90);
			break;
		case 5:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x1a,
						    0x1a, 0x60, 0x90);
			break;
		case 6:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x12,
						    0x12, 0x60, 0x90);
			break;
		case 7:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x1c,
						    0x3, 0x70, 0x90);
			break;
		case 8:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xa3, 0x10,
						    0x3, 0x70, 0x90);
			break;
		case 9:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x1a,
						    0x1a, 0xe1, 0x90);
			break;
		case 10:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x12,
						    0x12, 0xe1, 0x90);
			break;
		case 11:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0xa,
						    0xa, 0xe1, 0x90);
			break;
		case 12:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x5,
						    0x5, 0xe1, 0x90);
			break;
		case 13:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x1a,
						    0x1a, 0x60, 0x90);
			break;
		case 14:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x12,
						    0x12, 0x60, 0x90);
			break;
		case 15:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0xa,
						    0xa, 0x60, 0x90);
			break;
		case 16:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x5,
						    0x5, 0x60, 0x90);
			break;
		case 17:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xa3, 0x2f,
						    0x2f, 0x60, 0x90);
			break;
		case 18:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x5,
						    0x5, 0xe1, 0x90);
			break;
		case 19:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x25,
						    0x25, 0xe1, 0x90);
			break;
		case 20:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x25,
						    0x25, 0x60, 0x90);
			break;
		case 21:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x15,
						    0x03, 0x70, 0x90);
			break;
		case 71:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x1a,
						    0x1a, 0xe1, 0x90);
			break;
		}
	} else {
		/* disable PS tdma */
		switch (type) {
		case 0:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0x0, 0x0, 0x0,
						    0x40, 0x0);
			break;
		case 1:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0x0, 0x0, 0x0,
						    0x48, 0x0);
			break;
		default:
			btc8723b2ant_set_fw_ps_tdma(btcoexist, 0x0, 0x0, 0x0,
						    0x40, 0x0);
			break;
		}
	}

	/* update pre state */
	coex_dm->pre_ps_tdma_on = coex_dm->cur_ps_tdma_on;
	coex_dm->pre_ps_tdma = coex_dm->cur_ps_tdma;
}

static void btc8723b2ant_coex_alloff(struct btc_coexist *btcoexist)
{
	/* fw all off */
	btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
	btc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
	btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	/* sw all off */
	btc8723b2ant_sw_mechanism1(btcoexist, false, false, false, false);
	btc8723b2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);

	/* hw all off */
	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);
	btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 0);
}

static void btc8723b2ant_init_coex_dm(struct btc_coexist *btcoexist)
{
	/* force to reset coex mechanism*/

	btc8723b2ant_ps_tdma(btcoexist, FORCE_EXEC, false, 1);
	btc8723b2ant_fw_dac_swing_lvl(btcoexist, FORCE_EXEC, 6);
	btc8723b2ant_dec_bt_pwr(btcoexist, FORCE_EXEC, false);

	btc8723b2ant_sw_mechanism1(btcoexist, false, false, false, false);
	btc8723b2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
}

static void btc8723b2ant_action_bt_inquiry(struct btc_coexist *btcoexist)
{
	bool wifi_connected = false;
	bool low_pwr_disable = true;

	btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
			   &low_pwr_disable);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);

	if (wifi_connected) {
		btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 7);
		btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 3);
	} else {
		btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 0);
		btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
	}
	btc8723b2ant_fw_dac_swing_lvl(btcoexist, FORCE_EXEC, 6);
	btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btc8723b2ant_sw_mechanism1(btcoexist, false, false, false, false);
	btc8723b2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);

	coex_dm->need_recover_0x948 = true;
	coex_dm->backup_0x948 = btcoexist->btc_read_2byte(btcoexist, 0x948);

	btcoexist->btc_write_2byte(btcoexist, 0x948, 0x280);
}

static bool btc8723b2ant_is_common_action(struct btc_coexist *btcoexist)
{
	bool common = false, wifi_connected = false;
	bool wifi_busy = false;
	bool bt_hs_on = false, low_pwr_disable = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	if (!wifi_connected) {
		low_pwr_disable = false;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);

		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], Wifi non-connected idle!!\n");

		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff,
					  0x0);
		btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 0);
		btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
		btc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

		btc8723b2ant_sw_mechanism1(btcoexist, false, false, false,
					   false);
		btc8723b2ant_sw_mechanism2(btcoexist, false, false, false,
					   0x18);

		common = true;
	} else {
		if (BT_8723B_2ANT_BT_STATUS_NON_CONNECTED_IDLE ==
		    coex_dm->bt_status) {
			low_pwr_disable = false;
			btcoexist->btc_set(btcoexist,
					   BTC_SET_ACT_DISABLE_LOW_POWER,
					   &low_pwr_disable);

			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Wifi connected + "
				  "BT non connected-idle!!\n");

			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1,
						  0xfffff, 0x0);
			btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 0);
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
			btc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC,
						      0xb);
			btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC,
						false);

			btc8723b2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);

			common = true;
		} else if (BT_8723B_2ANT_BT_STATUS_CONNECTED_IDLE ==
			   coex_dm->bt_status) {
			low_pwr_disable = true;
			btcoexist->btc_set(btcoexist,
					   BTC_SET_ACT_DISABLE_LOW_POWER,
					   &low_pwr_disable);

			if (bt_hs_on)
				return false;
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Wifi connected + "
				  "BT connected-idle!!\n");

			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1,
						  0xfffff, 0x0);
			btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 0);
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
			btc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC,
						      0xb);
			btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC,
						false);

			btc8723b2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);

			common = true;
		} else {
			low_pwr_disable = true;
			btcoexist->btc_set(btcoexist,
					   BTC_SET_ACT_DISABLE_LOW_POWER,
					   &low_pwr_disable);

			if (wifi_busy) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
					  "[BTCoex], Wifi Connected-Busy + "
					  "BT Busy!!\n");
				common = false;
			} else {
				if (bt_hs_on)
					return false;

				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
					  "[BTCoex], Wifi Connected-Idle + "
					  "BT Busy!!\n");

				btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A,
							  0x1, 0xfffff, 0x0);
				btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC,
						       7);
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 21);
				btc8723b2ant_fw_dac_swing_lvl(btcoexist,
							      NORMAL_EXEC,
							      0xb);
				if (btc8723b_need_dec_pwr(btcoexist))
					btc8723b2ant_dec_bt_pwr(btcoexist,
								NORMAL_EXEC,
								true);
				else
					btc8723b2ant_dec_bt_pwr(btcoexist,
								NORMAL_EXEC,
								false);
				btc8723b2ant_sw_mechanism1(btcoexist, false,
							   false, false,
							   false);
				btc8723b2ant_sw_mechanism2(btcoexist, false,
							   false, false,
							   0x18);
				common = true;
			}
		}
	}

	return common;
}

static void set_tdma_int1(struct btc_coexist *btcoexist, bool tx_pause,
			  s32 result)
{
	/* Set PS TDMA for max interval == 1 */
	if (tx_pause) {
		BTC_PRINT(BTC_MSG_ALGORITHM,
			  ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], TxPause = 1\n");

		if (coex_dm->cur_ps_tdma == 71) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
					     true, 5);
			coex_dm->tdma_adj_type = 5;
		} else if (coex_dm->cur_ps_tdma == 1) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
					     true, 5);
			coex_dm->tdma_adj_type = 5;
		} else if (coex_dm->cur_ps_tdma == 2) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
					     true, 6);
			coex_dm->tdma_adj_type = 6;
		} else if (coex_dm->cur_ps_tdma == 3) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
					     true, 7);
			coex_dm->tdma_adj_type = 7;
		} else if (coex_dm->cur_ps_tdma == 4) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
					     true, 8);
			coex_dm->tdma_adj_type = 8;
		} else if (coex_dm->cur_ps_tdma == 9) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
					     true, 13);
			coex_dm->tdma_adj_type = 13;
		} else if (coex_dm->cur_ps_tdma == 10) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
					     true, 14);
			coex_dm->tdma_adj_type = 14;
		} else if (coex_dm->cur_ps_tdma == 11) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
					     true, 15);
			coex_dm->tdma_adj_type = 15;
		} else if (coex_dm->cur_ps_tdma == 12) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
					     true, 16);
			coex_dm->tdma_adj_type = 16;
		}

		if (result == -1) {
			if (coex_dm->cur_ps_tdma == 5) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 6);
				coex_dm->tdma_adj_type = 6;
			} else if (coex_dm->cur_ps_tdma == 6) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 7);
				coex_dm->tdma_adj_type = 7;
			} else if (coex_dm->cur_ps_tdma == 7) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 8);
				coex_dm->tdma_adj_type = 8;
			} else if (coex_dm->cur_ps_tdma == 13) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 14);
				coex_dm->tdma_adj_type = 14;
			} else if (coex_dm->cur_ps_tdma == 14) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 15);
				coex_dm->tdma_adj_type = 15;
			} else if (coex_dm->cur_ps_tdma == 15) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 16);
				coex_dm->tdma_adj_type = 16;
			}
		}  else if (result == 1) {
			if (coex_dm->cur_ps_tdma == 8) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 7);
				coex_dm->tdma_adj_type = 7;
			} else if (coex_dm->cur_ps_tdma == 7) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 6);
				coex_dm->tdma_adj_type = 6;
			} else if (coex_dm->cur_ps_tdma == 6) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 5);
				coex_dm->tdma_adj_type = 5;
			} else if (coex_dm->cur_ps_tdma == 16) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 15);
				coex_dm->tdma_adj_type = 15;
			} else if (coex_dm->cur_ps_tdma == 15) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 14);
				coex_dm->tdma_adj_type = 14;
			} else if (coex_dm->cur_ps_tdma == 14) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 13);
				coex_dm->tdma_adj_type = 13;
			}
		}
	} else {
		BTC_PRINT(BTC_MSG_ALGORITHM,
			  ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], TxPause = 0\n");
		if (coex_dm->cur_ps_tdma == 5) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 71);
			coex_dm->tdma_adj_type = 71;
		} else if (coex_dm->cur_ps_tdma == 6) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 2);
			coex_dm->tdma_adj_type = 2;
		} else if (coex_dm->cur_ps_tdma == 7) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 3);
			coex_dm->tdma_adj_type = 3;
		} else if (coex_dm->cur_ps_tdma == 8) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 4);
			coex_dm->tdma_adj_type = 4;
		} else if (coex_dm->cur_ps_tdma == 13) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 9);
			coex_dm->tdma_adj_type = 9;
		} else if (coex_dm->cur_ps_tdma == 14) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 10);
			coex_dm->tdma_adj_type = 10;
		} else if (coex_dm->cur_ps_tdma == 15) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 11);
			coex_dm->tdma_adj_type = 11;
		} else if (coex_dm->cur_ps_tdma == 16) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 12);
			coex_dm->tdma_adj_type = 12;
		}

		if (result == -1) {
			if (coex_dm->cur_ps_tdma == 71) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 1);
				coex_dm->tdma_adj_type = 1;
			} else if (coex_dm->cur_ps_tdma == 1) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 2);
				coex_dm->tdma_adj_type = 2;
			} else if (coex_dm->cur_ps_tdma == 2) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 3);
				coex_dm->tdma_adj_type = 3;
			} else if (coex_dm->cur_ps_tdma == 3) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 4);
				coex_dm->tdma_adj_type = 4;
			} else if (coex_dm->cur_ps_tdma == 9) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 10);
				coex_dm->tdma_adj_type = 10;
			} else if (coex_dm->cur_ps_tdma == 10) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 11);
				coex_dm->tdma_adj_type = 11;
			} else if (coex_dm->cur_ps_tdma == 11) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 12);
				coex_dm->tdma_adj_type = 12;
			}
		}  else if (result == 1) {
			int tmp = coex_dm->cur_ps_tdma;
			switch (tmp) {
			case 4:
			case 3:
			case 2:
			case 12:
			case 11:
			case 10:
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, tmp - 1);
				coex_dm->tdma_adj_type = tmp - 1;
				break;
			case 1:
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 71);
				coex_dm->tdma_adj_type = 71;
				break;
			}
		}
	}
}

static void set_tdma_int2(struct btc_coexist *btcoexist, bool tx_pause,
			  s32 result)
{
	/* Set PS TDMA for max interval == 2 */
	if (tx_pause) {
		BTC_PRINT(BTC_MSG_ALGORITHM,
			  ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], TxPause = 1\n");
		if (coex_dm->cur_ps_tdma == 1) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 6);
			coex_dm->tdma_adj_type = 6;
		} else if (coex_dm->cur_ps_tdma == 2) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 6);
			coex_dm->tdma_adj_type = 6;
		} else if (coex_dm->cur_ps_tdma == 3) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 7);
			coex_dm->tdma_adj_type = 7;
		} else if (coex_dm->cur_ps_tdma == 4) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 8);
			coex_dm->tdma_adj_type = 8;
		} else if (coex_dm->cur_ps_tdma == 9) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 14);
			coex_dm->tdma_adj_type = 14;
		} else if (coex_dm->cur_ps_tdma == 10) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 14);
			coex_dm->tdma_adj_type = 14;
		} else if (coex_dm->cur_ps_tdma == 11) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 15);
			coex_dm->tdma_adj_type = 15;
		} else if (coex_dm->cur_ps_tdma == 12) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 16);
			coex_dm->tdma_adj_type = 16;
		}
		if (result == -1) {
			if (coex_dm->cur_ps_tdma == 5) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 6);
				coex_dm->tdma_adj_type = 6;
			} else if (coex_dm->cur_ps_tdma == 6) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 7);
				coex_dm->tdma_adj_type = 7;
			} else if (coex_dm->cur_ps_tdma == 7) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 8);
				coex_dm->tdma_adj_type = 8;
			} else if (coex_dm->cur_ps_tdma == 13) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 14);
				coex_dm->tdma_adj_type = 14;
			} else if (coex_dm->cur_ps_tdma == 14) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 15);
				coex_dm->tdma_adj_type = 15;
			} else if (coex_dm->cur_ps_tdma == 15) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 16);
				coex_dm->tdma_adj_type = 16;
			}
		}  else if (result == 1) {
			if (coex_dm->cur_ps_tdma == 8) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 7);
				coex_dm->tdma_adj_type = 7;
			} else if (coex_dm->cur_ps_tdma == 7) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 6);
				coex_dm->tdma_adj_type = 6;
			} else if (coex_dm->cur_ps_tdma == 6) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 6);
				coex_dm->tdma_adj_type = 6;
			} else if (coex_dm->cur_ps_tdma == 16) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 15);
				coex_dm->tdma_adj_type = 15;
			} else if (coex_dm->cur_ps_tdma == 15) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 14);
				coex_dm->tdma_adj_type = 14;
			} else if (coex_dm->cur_ps_tdma == 14) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 14);
				coex_dm->tdma_adj_type = 14;
			}
		}
	} else {
		BTC_PRINT(BTC_MSG_ALGORITHM,
			  ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], TxPause = 0\n");
		if (coex_dm->cur_ps_tdma == 5) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 2);
			coex_dm->tdma_adj_type = 2;
		} else if (coex_dm->cur_ps_tdma == 6) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 2);
			coex_dm->tdma_adj_type = 2;
		} else if (coex_dm->cur_ps_tdma == 7) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 3);
			coex_dm->tdma_adj_type = 3;
		} else if (coex_dm->cur_ps_tdma == 8) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 4);
			coex_dm->tdma_adj_type = 4;
		} else if (coex_dm->cur_ps_tdma == 13) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 10);
			coex_dm->tdma_adj_type = 10;
		} else if (coex_dm->cur_ps_tdma == 14) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 10);
			coex_dm->tdma_adj_type = 10;
		} else if (coex_dm->cur_ps_tdma == 15) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 11);
			coex_dm->tdma_adj_type = 11;
		} else if (coex_dm->cur_ps_tdma == 16) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 12);
			coex_dm->tdma_adj_type = 12;
		}
		if (result == -1) {
			if (coex_dm->cur_ps_tdma == 1) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 2);
				coex_dm->tdma_adj_type = 2;
			} else if (coex_dm->cur_ps_tdma == 2) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 3);
				coex_dm->tdma_adj_type = 3;
			} else if (coex_dm->cur_ps_tdma == 3) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 4);
				coex_dm->tdma_adj_type = 4;
			} else if (coex_dm->cur_ps_tdma == 9) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 10);
				coex_dm->tdma_adj_type = 10;
			} else if (coex_dm->cur_ps_tdma == 10) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 11);
				coex_dm->tdma_adj_type = 11;
			} else if (coex_dm->cur_ps_tdma == 11) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 12);
				coex_dm->tdma_adj_type = 12;
			}
		} else if (result == 1) {
			if (coex_dm->cur_ps_tdma == 4) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 3);
				coex_dm->tdma_adj_type = 3;
			} else if (coex_dm->cur_ps_tdma == 3) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 2);
				coex_dm->tdma_adj_type = 2;
			} else if (coex_dm->cur_ps_tdma == 2) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 2);
				coex_dm->tdma_adj_type = 2;
			} else if (coex_dm->cur_ps_tdma == 12) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 11);
				coex_dm->tdma_adj_type = 11;
			} else if (coex_dm->cur_ps_tdma == 11) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 10);
				coex_dm->tdma_adj_type = 10;
			} else if (coex_dm->cur_ps_tdma == 10) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 10);
				coex_dm->tdma_adj_type = 10;
			}
		}
	}
}

static void set_tdma_int3(struct btc_coexist *btcoexist, bool tx_pause,
			  s32 result)
{
	/* Set PS TDMA for max interval == 3 */
	if (tx_pause) {
		BTC_PRINT(BTC_MSG_ALGORITHM,
			  ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], TxPause = 1\n");
		if (coex_dm->cur_ps_tdma == 1) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 7);
			coex_dm->tdma_adj_type = 7;
		} else if (coex_dm->cur_ps_tdma == 2) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 7);
			coex_dm->tdma_adj_type = 7;
		} else if (coex_dm->cur_ps_tdma == 3) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 7);
			coex_dm->tdma_adj_type = 7;
		} else if (coex_dm->cur_ps_tdma == 4) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 8);
			coex_dm->tdma_adj_type = 8;
		} else if (coex_dm->cur_ps_tdma == 9) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 15);
			coex_dm->tdma_adj_type = 15;
		} else if (coex_dm->cur_ps_tdma == 10) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 15);
			coex_dm->tdma_adj_type = 15;
		} else if (coex_dm->cur_ps_tdma == 11) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 15);
			coex_dm->tdma_adj_type = 15;
		} else if (coex_dm->cur_ps_tdma == 12) {
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 16);
			coex_dm->tdma_adj_type = 16;
		}
		if (result == -1) {
			if (coex_dm->cur_ps_tdma == 5) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 7);
				coex_dm->tdma_adj_type = 7;
			} else if (coex_dm->cur_ps_tdma == 6) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 7);
				coex_dm->tdma_adj_type = 7;
			} else if (coex_dm->cur_ps_tdma == 7) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 8);
				coex_dm->tdma_adj_type = 8;
			} else if (coex_dm->cur_ps_tdma == 13) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 15);
				coex_dm->tdma_adj_type = 15;
			} else if (coex_dm->cur_ps_tdma == 14) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 15);
				coex_dm->tdma_adj_type = 15;
			} else if (coex_dm->cur_ps_tdma == 15) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 16);
				coex_dm->tdma_adj_type = 16;
			}
		}  else if (result == 1) {
			if (coex_dm->cur_ps_tdma == 8) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 7);
				coex_dm->tdma_adj_type = 7;
			} else if (coex_dm->cur_ps_tdma == 7) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 7);
				coex_dm->tdma_adj_type = 7;
			} else if (coex_dm->cur_ps_tdma == 6) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 7);
				coex_dm->tdma_adj_type = 7;
			} else if (coex_dm->cur_ps_tdma == 16) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 15);
				coex_dm->tdma_adj_type = 15;
			} else if (coex_dm->cur_ps_tdma == 15) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 15);
				coex_dm->tdma_adj_type = 15;
			} else if (coex_dm->cur_ps_tdma == 14) {
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 15);
				coex_dm->tdma_adj_type = 15;
			}
		}
	} else {
		BTC_PRINT(BTC_MSG_ALGORITHM,
			  ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], TxPause = 0\n");
		switch (coex_dm->cur_ps_tdma) {
		case 5:
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 3);
			coex_dm->tdma_adj_type = 3;
			break;
		case 6:
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 3);
			coex_dm->tdma_adj_type = 3;
			break;
		case 7:
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 3);
			coex_dm->tdma_adj_type = 3;
			break;
		case 8:
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 4);
			coex_dm->tdma_adj_type = 4;
			break;
		case 13:
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 11);
			coex_dm->tdma_adj_type = 11;
			break;
		case 14:
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 11);
			coex_dm->tdma_adj_type = 11;
			break;
		case 15:
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 11);
			coex_dm->tdma_adj_type = 11;
			break;
		case 16:
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 12);
			coex_dm->tdma_adj_type = 12;
			break;
		}
		if (result == -1) {
			switch (coex_dm->cur_ps_tdma) {
			case 1:
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 3);
				coex_dm->tdma_adj_type = 3;
				break;
			case 2:
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 3);
				coex_dm->tdma_adj_type = 3;
				break;
			case 3:
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 4);
				coex_dm->tdma_adj_type = 4;
				break;
			case 9:
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 11);
				coex_dm->tdma_adj_type = 11;
				break;
			case 10:
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 11);
				coex_dm->tdma_adj_type = 11;
				break;
			case 11:
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 12);
				coex_dm->tdma_adj_type = 12;
				break;
			}
		} else if (result == 1) {
			switch (coex_dm->cur_ps_tdma) {
			case 4:
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 3);
				coex_dm->tdma_adj_type = 3;
				break;
			case 3:
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 3);
				coex_dm->tdma_adj_type = 3;
				break;
			case 2:
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 3);
				coex_dm->tdma_adj_type = 3;
				break;
			case 12:
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 11);
				coex_dm->tdma_adj_type = 11;
				break;
			case 11:
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 11);
				coex_dm->tdma_adj_type = 11;
				break;
			case 10:
				btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						     true, 11);
				coex_dm->tdma_adj_type = 11;
			}
		}
	}
}

static void btc8723b2ant_tdma_duration_adjust(struct btc_coexist *btcoexist,
					  bool sco_hid, bool tx_pause,
					  u8 max_interval)
{
	static s32 up, dn, m, n, wait_count;
	/*0: no change, +1: increase WiFi duration, -1: decrease WiFi duration*/
	s32 result;
	u8 retry_count = 0;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
		  "[BTCoex], TdmaDurationAdjust()\n");

	if (!coex_dm->auto_tdma_adjust) {
		coex_dm->auto_tdma_adjust = true;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], first run TdmaDurationAdjust()!!\n");
		if (sco_hid) {
			if (tx_pause) {
				if (max_interval == 1) {
					btc8723b2ant_ps_tdma(btcoexist,
							     NORMAL_EXEC,
							     true, 13);
					coex_dm->tdma_adj_type = 13;
				} else if (max_interval == 2) {
					btc8723b2ant_ps_tdma(btcoexist,
							     NORMAL_EXEC,
							     true, 14);
					coex_dm->tdma_adj_type = 14;
				} else if (max_interval == 3) {
					btc8723b2ant_ps_tdma(btcoexist,
							     NORMAL_EXEC,
							     true, 15);
					coex_dm->tdma_adj_type = 15;
				} else {
					btc8723b2ant_ps_tdma(btcoexist,
							     NORMAL_EXEC,
							     true, 15);
					coex_dm->tdma_adj_type = 15;
				}
			} else {
				if (max_interval == 1) {
					btc8723b2ant_ps_tdma(btcoexist,
							     NORMAL_EXEC,
							     true, 9);
					coex_dm->tdma_adj_type = 9;
				} else if (max_interval == 2) {
					btc8723b2ant_ps_tdma(btcoexist,
							     NORMAL_EXEC,
							     true, 10);
					coex_dm->tdma_adj_type = 10;
				} else if (max_interval == 3) {
					btc8723b2ant_ps_tdma(btcoexist,
							     NORMAL_EXEC,
							     true, 11);
					coex_dm->tdma_adj_type = 11;
				} else {
					btc8723b2ant_ps_tdma(btcoexist,
							     NORMAL_EXEC,
							     true, 11);
					coex_dm->tdma_adj_type = 11;
				}
			}
		} else {
			if (tx_pause) {
				if (max_interval == 1) {
					btc8723b2ant_ps_tdma(btcoexist,
							     NORMAL_EXEC,
							     true, 5);
					coex_dm->tdma_adj_type = 5;
				} else if (max_interval == 2) {
					btc8723b2ant_ps_tdma(btcoexist,
							     NORMAL_EXEC,
							     true, 6);
					coex_dm->tdma_adj_type = 6;
				} else if (max_interval == 3) {
					btc8723b2ant_ps_tdma(btcoexist,
							     NORMAL_EXEC,
							     true, 7);
					coex_dm->tdma_adj_type = 7;
				} else {
					btc8723b2ant_ps_tdma(btcoexist,
							     NORMAL_EXEC,
							     true, 7);
					coex_dm->tdma_adj_type = 7;
				}
			} else {
				if (max_interval == 1) {
					btc8723b2ant_ps_tdma(btcoexist,
							     NORMAL_EXEC,
							     true, 1);
					coex_dm->tdma_adj_type = 1;
				} else if (max_interval == 2) {
					btc8723b2ant_ps_tdma(btcoexist,
							     NORMAL_EXEC,
							     true, 2);
					coex_dm->tdma_adj_type = 2;
				} else if (max_interval == 3) {
					btc8723b2ant_ps_tdma(btcoexist,
							     NORMAL_EXEC,
							     true, 3);
					coex_dm->tdma_adj_type = 3;
				} else {
					btc8723b2ant_ps_tdma(btcoexist,
							     NORMAL_EXEC,
							     true, 3);
					coex_dm->tdma_adj_type = 3;
				}
			}
		}

		up = 0;
		dn = 0;
		m = 1;
		n = 3;
		result = 0;
		wait_count = 0;
	} else {
		/*accquire the BT TRx retry count from BT_Info byte2*/
		retry_count = coex_sta->bt_retry_cnt;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], retry_count = %d\n", retry_count);
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], up=%d, dn=%d, m=%d, n=%d, wait_count=%d\n",
			  up, dn, m, n, wait_count);
		result = 0;
		wait_count++;
		 /* no retry in the last 2-second duration*/
		if (retry_count == 0) {
			up++;
			dn--;

			if (dn <= 0)
				dn = 0;

			if (up >= n) {
				wait_count = 0;
				n = 3;
				up = 0;
				dn = 0;
				result = 1;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_TRACE_FW_DETAIL,
					  "[BTCoex], Increase wifi "
					  "duration!!\n");
			} /* <=3 retry in the last 2-second duration*/
		} else if (retry_count <= 3) {
			up--;
			dn++;

			if (up <= 0)
				up = 0;

			if (dn == 2) {
				if (wait_count <= 2)
					m++;
				else
					m = 1;

				if (m >= 20)
					m = 20;

				n = 3 * m;
				up = 0;
				dn = 0;
				wait_count = 0;
				result = -1;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_TRACE_FW_DETAIL,
					  "[BTCoex], Decrease wifi duration "
					  "for retry_counter<3!!\n");
			}
		} else {
			if (wait_count == 1)
				m++;
			else
				m = 1;

			if (m >= 20)
				m = 20;

			n = 3 * m;
			up = 0;
			dn = 0;
			wait_count = 0;
			result = -1;
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
				  "[BTCoex], Decrease wifi duration "
				  "for retry_counter>3!!\n");
		}

		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], max Interval = %d\n", max_interval);
		if (max_interval == 1)
			set_tdma_int1(btcoexist, tx_pause, result);
		else if (max_interval == 2)
			set_tdma_int2(btcoexist, tx_pause, result);
		else if (max_interval == 3)
			set_tdma_int3(btcoexist, tx_pause, result);
	}

	/*if current PsTdma not match with the recorded one (when scan, dhcp..),
	 *then we have to adjust it back to the previous recorded one.
	 */
	if (coex_dm->cur_ps_tdma != coex_dm->tdma_adj_type) {
		bool scan = false, link = false, roam = false;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], PsTdma type dismatch!!!, "
			  "curPsTdma=%d, recordPsTdma=%d\n",
			  coex_dm->cur_ps_tdma, coex_dm->tdma_adj_type);

		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);

		if (!scan && !link && !roam)
			btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
					     coex_dm->tdma_adj_type);
		else
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
				  "[BTCoex], roaming/link/scan is under"
				  " progress, will adjust next time!!!\n");
	}
}

/* SCO only or SCO+PAN(HS) */
static void btc8723b2ant_action_sco(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = btc8723b2ant_wifi_rssi_state(btcoexist,
						       0, 2, 15, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	btc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 4);

	if (btc8723b_need_dec_pwr(btcoexist))
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	/*for SCO quality at 11b/g mode*/
	if (BTC_WIFI_BW_LEGACY == wifi_bw)
		btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 2);
	else  /*for SCO quality & wifi performance balance at 11n mode*/
		btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 8);

	/*for voice quality */
	btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);

	/* sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8723b2ant_sw_mechanism1(btcoexist, true, true,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, true, false,
						   true, 0x4);
		} else {
			btc8723b2ant_sw_mechanism1(btcoexist, true, true,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   true, 0x4);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8723b2ant_sw_mechanism1(btcoexist, false, true,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, true, false,
						   true, 0x4);
		} else {
			btc8723b2ant_sw_mechanism1(btcoexist, false, true,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   true, 0x4);
		}
	}
}

static void btc8723b2ant_action_hid(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = btc8723b2ant_wifi_rssi_state(btcoexist,
						       0, 2, 15, 0);
	bt_rssi_state = btc8723b2ant_bt_rssi_state(2, 35, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	btc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (btc8723b_need_dec_pwr(btcoexist))
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_LEGACY == wifi_bw) /*/for HID at 11b/g mode*/
		btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 7);
	else  /*for HID quality & wifi performance balance at 11n mode*/
		btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 9);

	if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
	    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
		btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 9);
	else
		btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 13);

	/* sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8723b2ant_sw_mechanism1(btcoexist, true, true,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8723b2ant_sw_mechanism1(btcoexist, true, true,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8723b2ant_sw_mechanism1(btcoexist, false, true,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8723b2ant_sw_mechanism1(btcoexist, false, true,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	}
}

/*A2DP only / PAN(EDR) only/ A2DP+PAN(HS)*/
static void btc8723b2ant_action_a2dp(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = btc8723b2ant_wifi_rssi_state(btcoexist,
						       0, 2, 15, 0);
	bt_rssi_state = btc8723b2ant_bt_rssi_state(2, 35, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	btc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (btc8723b_need_dec_pwr(btcoexist))
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 7);

	if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
	    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
		btc8723b2ant_tdma_duration_adjust(btcoexist, false,
						  false, 1);
	else
		btc8723b2ant_tdma_duration_adjust(btcoexist, false, true, 1);

	/* sw mechanism */
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8723b2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8723b2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8723b2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8723b2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	}
}

static void btc8723b2ant_action_a2dp_pan_hs(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = btc8723b2ant_wifi_rssi_state(btcoexist,
						       0, 2, 15, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	btc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (btc8723b_need_dec_pwr(btcoexist))
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 7);

	btc8723b2ant_tdma_duration_adjust(btcoexist, false, true, 2);

	/* sw mechanism */
	btcoexist->btc_get(btcoexist,
		BTC_GET_U4_WIFI_BW, &wifi_bw);
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8723b2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8723b2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8723b2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8723b2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	}
}

static void btc8723b2ant_action_pan_edr(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = btc8723b2ant_wifi_rssi_state(btcoexist,
						       0, 2, 15, 0);
	bt_rssi_state = btc8723b2ant_bt_rssi_state(2, 35, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	btc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (btc8723b_need_dec_pwr(btcoexist))
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 10);

	if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
	    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
		btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 1);
	else
		btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 5);

	/* sw mechanism */
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8723b2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8723b2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8723b2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8723b2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	}
}

/*PAN(HS) only*/
static void btc8723b2ant_action_pan_hs(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = btc8723b2ant_wifi_rssi_state(btcoexist,
						       0, 2, 15, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	btc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
	    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 7);

	btc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8723b2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8723b2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8723b2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8723b2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	}
}

/*PAN(EDR)+A2DP*/
static void btc8723b2ant_action_pan_edr_a2dp(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = btc8723b2ant_wifi_rssi_state(btcoexist,
						       0, 2, 15, 0);
	bt_rssi_state = btc8723b2ant_bt_rssi_state(2, 35, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	btc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (btc8723b_need_dec_pwr(btcoexist))
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
	    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
		btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 12);
		if (BTC_WIFI_BW_HT40 == wifi_bw)
			btc8723b2ant_tdma_duration_adjust(btcoexist, false,
							  true, 3);
		else
			btc8723b2ant_tdma_duration_adjust(btcoexist, false,
							  false, 3);
	} else {
		btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 7);
		btc8723b2ant_tdma_duration_adjust(btcoexist, false, true, 3);
	}

	/* sw mechanism	*/
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8723b2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8723b2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8723b2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8723b2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	}
}

static void btc8723b2ant_action_pan_edr_hid(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = btc8723b2ant_wifi_rssi_state(btcoexist,
						       0, 2, 15, 0);
	bt_rssi_state = btc8723b2ant_bt_rssi_state(2, 35, 0);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (btc8723b_need_dec_pwr(btcoexist))
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
	    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
		if (BTC_WIFI_BW_HT40 == wifi_bw) {
			btc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC,
						      3);
			btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 11);
			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1,
						  0xfffff, 0x780);
		} else {
			btc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC,
						      6);
			btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 7);
			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1,
						  0xfffff, 0x0);
		}
		btc8723b2ant_tdma_duration_adjust(btcoexist, true, false, 2);
	} else {
		btc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 11);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff,
					  0x0);
		btc8723b2ant_tdma_duration_adjust(btcoexist, true, true, 2);
	}

	/* sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8723b2ant_sw_mechanism1(btcoexist, true, true,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8723b2ant_sw_mechanism1(btcoexist, true, true,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8723b2ant_sw_mechanism1(btcoexist, false, true,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8723b2ant_sw_mechanism1(btcoexist, false, true,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	}
}

/* HID+A2DP+PAN(EDR) */
static void btc8723b2ant_action_hid_a2dp_pan_edr(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = btc8723b2ant_wifi_rssi_state(btcoexist,
						       0, 2, 15, 0);
	bt_rssi_state = btc8723b2ant_bt_rssi_state(2, 35, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	btc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (btc8723b_need_dec_pwr(btcoexist))
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 7);

	if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
	    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
		if (BTC_WIFI_BW_HT40 == wifi_bw)
			btc8723b2ant_tdma_duration_adjust(btcoexist, true,
							  true, 2);
		else
			btc8723b2ant_tdma_duration_adjust(btcoexist, true,
							  false, 3);
	} else {
		btc8723b2ant_tdma_duration_adjust(btcoexist, true, true, 3);
	}

	/* sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8723b2ant_sw_mechanism1(btcoexist, true, true,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8723b2ant_sw_mechanism1(btcoexist, true, true,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8723b2ant_sw_mechanism1(btcoexist, false, true,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8723b2ant_sw_mechanism1(btcoexist, false, true,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	}
}

static void btc8723b2ant_action_hid_a2dp(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = btc8723b2ant_wifi_rssi_state(btcoexist,
							  0, 2, 15, 0);
	bt_rssi_state = btc8723b2ant_bt_rssi_state(2, 35, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	btc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (btc8723b_need_dec_pwr(btcoexist))
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		btc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	btc8723b_coex_tbl_type(btcoexist, NORMAL_EXEC, 7);

	if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
	    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
		btc8723b2ant_tdma_duration_adjust(btcoexist, true, false, 2);
	else
		btc8723b2ant_tdma_duration_adjust(btcoexist, true, true, 2);

	/* sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8723b2ant_sw_mechanism1(btcoexist, true, true,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8723b2ant_sw_mechanism1(btcoexist, true, true,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8723b2ant_sw_mechanism1(btcoexist, false, true,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8723b2ant_sw_mechanism1(btcoexist, false, true,
						   false, false);
			btc8723b2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	}
}

static void btc8723b2ant_run_coexist_mechanism(struct btc_coexist *btcoexist)
{
	u8 algorithm = 0;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
		  "[BTCoex], RunCoexistMechanism()===>\n");

	if (btcoexist->manual_control) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], RunCoexistMechanism(), "
			  "return for Manual CTRL <===\n");
		return;
	}

	if (coex_sta->under_ips) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], wifi is under IPS !!!\n");
		return;
	}

	algorithm = btc8723b2ant_action_algorithm(btcoexist);
	if (coex_sta->c2h_bt_inquiry_page &&
	    (BT_8723B_2ANT_COEX_ALGO_PANHS != algorithm)) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], BT is under inquiry/page scan !!\n");
		btc8723b2ant_action_bt_inquiry(btcoexist);
		return;
	} else {
		if (coex_dm->need_recover_0x948) {
			coex_dm->need_recover_0x948 = false;
			btcoexist->btc_write_2byte(btcoexist, 0x948,
						   coex_dm->backup_0x948);
		}
	}

	coex_dm->cur_algorithm = algorithm;
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, "[BTCoex], Algorithm = %d\n",
		  coex_dm->cur_algorithm);

	if (btc8723b2ant_is_common_action(btcoexist)) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], Action 2-Ant common.\n");
		coex_dm->auto_tdma_adjust = false;
	} else {
		if (coex_dm->cur_algorithm != coex_dm->pre_algorithm) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], preAlgorithm=%d, "
				  "curAlgorithm=%d\n", coex_dm->pre_algorithm,
				  coex_dm->cur_algorithm);
			coex_dm->auto_tdma_adjust = false;
		}
		switch (coex_dm->cur_algorithm) {
		case BT_8723B_2ANT_COEX_ALGO_SCO:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, algorithm = SCO.\n");
			btc8723b2ant_action_sco(btcoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_HID:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, algorithm = HID.\n");
			btc8723b2ant_action_hid(btcoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_A2DP:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, "
				  "algorithm = A2DP.\n");
			btc8723b2ant_action_a2dp(btcoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_A2DP_PANHS:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, "
				  "algorithm = A2DP+PAN(HS).\n");
			btc8723b2ant_action_a2dp_pan_hs(btcoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_PANEDR:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, "
				  "algorithm = PAN(EDR).\n");
			btc8723b2ant_action_pan_edr(btcoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_PANHS:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, "
				  "algorithm = HS mode.\n");
			btc8723b2ant_action_pan_hs(btcoexist);
				break;
		case BT_8723B_2ANT_COEX_ALGO_PANEDR_A2DP:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, "
				  "algorithm = PAN+A2DP.\n");
			btc8723b2ant_action_pan_edr_a2dp(btcoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_PANEDR_HID:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, "
				  "algorithm = PAN(EDR)+HID.\n");
			btc8723b2ant_action_pan_edr_hid(btcoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_HID_A2DP_PANEDR:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, "
				  "algorithm = HID+A2DP+PAN.\n");
			btc8723b2ant_action_hid_a2dp_pan_edr(btcoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_HID_A2DP:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, "
				  "algorithm = HID+A2DP.\n");
			btc8723b2ant_action_hid_a2dp(btcoexist);
			break;
		default:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, "
				  "algorithm = coexist All Off!!\n");
			btc8723b2ant_coex_alloff(btcoexist);
			break;
		}
		coex_dm->pre_algorithm = coex_dm->cur_algorithm;
	}
}



/*********************************************************************
 *  work around function start with wa_btc8723b2ant_
 *********************************************************************/
/*********************************************************************
 *  extern function start with EXbtc8723b2ant_
 *********************************************************************/
void ex_halbtc8723b2ant_init_hwconfig(struct btc_coexist *btcoexist)
{
	struct btc_board_info *board_info = &btcoexist->board_info;
	u32 u32tmp = 0, fw_ver;
	u8 u8tmp = 0;
	u8 h2c_parameter[2] = {0};


	BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
		  "[BTCoex], 2Ant Init HW Config!!\n");

	/* backup rf 0x1e value */
	coex_dm->bt_rf0x1e_backup = btcoexist->btc_get_rf_reg(btcoexist,
							      BTC_RF_A, 0x1e,
							      0xfffff);

	/* 0x4c[23]=0, 0x4c[24]=1  Antenna control by WL/BT */
	u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x4c);
	u32tmp &= ~BIT23;
	u32tmp |= BIT24;
	btcoexist->btc_write_4byte(btcoexist, 0x4c, u32tmp);

	btcoexist->btc_write_1byte(btcoexist, 0x974, 0xff);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x944, 0x3, 0x3);
	btcoexist->btc_write_1byte(btcoexist, 0x930, 0x77);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x20, 0x1);

	/* Antenna switch control parameter */
	/* btcoexist->btc_write_4byte(btcoexist, 0x858, 0x55555555);*/

	/*Force GNT_BT to low*/
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x765, 0x18, 0x0);
	btcoexist->btc_write_2byte(btcoexist, 0x948, 0x0);

	/* 0x790[5:0]=0x5 */
	u8tmp = btcoexist->btc_read_1byte(btcoexist, 0x790);
	u8tmp &= 0xc0;
	u8tmp |= 0x5;
	btcoexist->btc_write_1byte(btcoexist, 0x790, u8tmp);


	/*Antenna config	*/
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);

 /*ext switch for fw ver < 0xc */
	if (fw_ver < 0xc00) {
		if (board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT) {
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0x92c,
							   0x3, 0x1);
			/*Main Ant to  BT for IPS case 0x4c[23]=1*/
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0x64, 0x1,
							   0x1);

			/*tell firmware "no antenna inverse"*/
			h2c_parameter[0] = 0;
			h2c_parameter[1] = 1;  /* ext switch type */
			btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
						h2c_parameter);
		} else {
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0x92c,
							   0x3, 0x2);
			/*Aux Ant to  BT for IPS case 0x4c[23]=1*/
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0x64, 0x1,
							   0x0);

			/*tell firmware "antenna inverse"*/
			h2c_parameter[0] = 1;
			h2c_parameter[1] = 1;  /*ext switch type*/
			btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
						h2c_parameter);
		}
	} else {
		/*ext switch always at s1 (if exist) */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x92c, 0x3, 0x1);
		/*Main Ant to  BT for IPS case 0x4c[23]=1*/
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x64, 0x1, 0x1);

		if (board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT) {
			/*tell firmware "no antenna inverse"*/
			h2c_parameter[0] = 0;
			h2c_parameter[1] = 0;  /*ext switch type*/
			btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
						h2c_parameter);
		} else {
			/*tell firmware "antenna inverse"*/
			h2c_parameter[0] = 1;
			h2c_parameter[1] = 0;  /*ext switch type*/
			btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
						h2c_parameter);
		}
	}

	/* PTA parameter */
	btc8723b_coex_tbl_type(btcoexist, FORCE_EXEC, 0);

	/* Enable counter statistics */
	/*0x76e[3] =1, WLAN_Act control by PTA*/
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0xc);
	btcoexist->btc_write_1byte(btcoexist, 0x778, 0x3);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x40, 0x20, 0x1);
}

void ex_halbtc8723b2ant_init_coex_dm(struct btc_coexist *btcoexist)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
		  "[BTCoex], Coex Mechanism Init!!\n");
	btc8723b2ant_init_coex_dm(btcoexist);
}

void ex_halbtc8723b2ant_display_coex_info(struct btc_coexist *btcoexist)
{
	struct btc_board_info *board_info = &btcoexist->board_info;
	struct btc_stack_info *stack_info = &btcoexist->stack_info;
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	u8 *cli_buf = btcoexist->cli_buf;
	u8 u8tmp[4], i, bt_info_ext, ps_tdma_case = 0;
	u32 u32tmp[4];
	bool roam = false, scan = false;
	bool link = false, wifi_under_5g = false;
	bool bt_hs_on = false, wifi_busy = false;
	s32 wifi_rssi = 0, bt_hs_rssi = 0;
	u32 wifi_bw, wifi_traffic_dir, fa_ofdm, fa_cck;
	u8 wifi_dot11_chnl, wifi_hs_chnl;
	u32 fw_ver = 0, bt_patch_ver = 0;

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n ============[BT Coexist info]============");
	CL_PRINTF(cli_buf);

	if (btcoexist->manual_control) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n ==========[Under Manual Control]============");
		CL_PRINTF(cli_buf);
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n ==========================================");
		CL_PRINTF(cli_buf);
	}

	if (!board_info->bt_exist) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n BT not exists !!!");
		CL_PRINTF(cli_buf);
		return;
	}

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d ",
		   "Ant PG number/ Ant mechanism:",
		   board_info->pg_ant_num, board_info->btdm_ant_num);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s / %d",
		   "BT stack/ hci ext ver",
		   ((stack_info->profile_notified) ? "Yes" : "No"),
		   stack_info->hci_version);
	CL_PRINTF(cli_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER, &bt_patch_ver);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %d_%x/ 0x%x/ 0x%x(%d)",
		   "CoexVer/ FwVer/ PatchVer",
		   glcoex_ver_date_8723b_2ant, glcoex_ver_8723b_2ant,
		   fw_ver, bt_patch_ver, bt_patch_ver);
	CL_PRINTF(cli_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_DOT11_CHNL,
			   &wifi_dot11_chnl);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_HS_CHNL, &wifi_hs_chnl);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d(%d)",
		   "Dot11 channel / HsChnl(HsMode)",
		   wifi_dot11_chnl, wifi_hs_chnl, bt_hs_on);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x ",
		   "H2C Wifi inform bt chnl Info", coex_dm->wifi_chnl_info[0],
		   coex_dm->wifi_chnl_info[1], coex_dm->wifi_chnl_info[2]);
	CL_PRINTF(cli_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_S4_WIFI_RSSI, &wifi_rssi);
	btcoexist->btc_get(btcoexist, BTC_GET_S4_HS_RSSI, &bt_hs_rssi);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "Wifi rssi/ HS rssi", wifi_rssi, bt_hs_rssi);
	CL_PRINTF(cli_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d ",
		   "Wifi link/ roam/ scan", link, roam, scan);
	CL_PRINTF(cli_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION,
			   &wifi_traffic_dir);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s / %s/ %s ",
		   "Wifi status", (wifi_under_5g ? "5G" : "2.4G"),
		   ((BTC_WIFI_BW_LEGACY == wifi_bw) ? "Legacy" :
		   (((BTC_WIFI_BW_HT40 == wifi_bw) ? "HT40" : "HT20"))),
		   ((!wifi_busy) ? "idle" :
		   ((BTC_WIFI_TRAFFIC_TX == wifi_traffic_dir) ?
		   "uplink" : "downlink")));
	CL_PRINTF(cli_buf);

	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d / %d",
		   "SCO/HID/PAN/A2DP",
		   bt_link_info->sco_exist, bt_link_info->hid_exist,
		   bt_link_info->pan_exist, bt_link_info->a2dp_exist);
	CL_PRINTF(cli_buf);
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_BT_LINK_INFO);

	bt_info_ext = coex_sta->bt_info_ext;
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s",
		   "BT Info A2DP rate",
		   (bt_info_ext&BIT0) ? "Basic rate" : "EDR rate");
	CL_PRINTF(cli_buf);

	for (i = 0; i < BT_INFO_SRC_8723B_2ANT_MAX; i++) {
		if (coex_sta->bt_info_c2h_cnt[i]) {
			CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
				   "\r\n %-35s = %02x %02x %02x "
				   "%02x %02x %02x %02x(%d)",
				   glbt_info_src_8723b_2ant[i],
				   coex_sta->bt_info_c2h[i][0],
				   coex_sta->bt_info_c2h[i][1],
				   coex_sta->bt_info_c2h[i][2],
				   coex_sta->bt_info_c2h[i][3],
				   coex_sta->bt_info_c2h[i][4],
				   coex_sta->bt_info_c2h[i][5],
				   coex_sta->bt_info_c2h[i][6],
				   coex_sta->bt_info_c2h_cnt[i]);
			CL_PRINTF(cli_buf);
		}
	}

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/%s",
		   "PS state, IPS/LPS",
		   ((coex_sta->under_ips ? "IPS ON" : "IPS OFF")),
		   ((coex_sta->under_lps ? "LPS ON" : "LPS OFF")));
	CL_PRINTF(cli_buf);
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_FW_PWR_MODE_CMD);

	/* Sw mechanism	*/
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s", "============[Sw mechanism]============");
	CL_PRINTF(cli_buf);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d ",
		   "SM1[ShRf/ LpRA/ LimDig]", coex_dm->cur_rf_rx_lpf_shrink,
		   coex_dm->cur_low_penalty_ra, coex_dm->limited_dig);
	CL_PRINTF(cli_buf);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d(0x%x) ",
		   "SM2[AgcT/ AdcB/ SwDacSwing(lvl)]",
		   coex_dm->cur_agc_table_en, coex_dm->cur_adc_back_off,
		   coex_dm->cur_dac_swing_on, coex_dm->cur_dac_swing_lvl);
	CL_PRINTF(cli_buf);

	/* Fw mechanism	*/
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
		   "============[Fw mechanism]============");
	CL_PRINTF(cli_buf);

	ps_tdma_case = coex_dm->cur_ps_tdma;
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %02x %02x %02x %02x %02x case-%d (auto:%d)",
		   "PS TDMA", coex_dm->ps_tdma_para[0],
		   coex_dm->ps_tdma_para[1], coex_dm->ps_tdma_para[2],
		   coex_dm->ps_tdma_para[3], coex_dm->ps_tdma_para[4],
		   ps_tdma_case, coex_dm->auto_tdma_adjust);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d ",
		   "DecBtPwr/ IgnWlanAct", coex_dm->cur_dec_bt_pwr,
		   coex_dm->cur_ignore_wlan_act);
	CL_PRINTF(cli_buf);

	/* Hw setting */
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
		   "============[Hw setting]============");
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x",
		   "RF-A, 0x1e initVal", coex_dm->bt_rf0x1e_backup);
	CL_PRINTF(cli_buf);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x778);
	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x880);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "0x778/0x880[29:25]", u8tmp[0],
		   (u32tmp[0]&0x3e000000) >> 25);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x948);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x67);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x765);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x948/ 0x67[5] / 0x765",
		   u32tmp[0], ((u8tmp[0]&0x20) >> 5), u8tmp[1]);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x92c);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x930);
	u32tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0x944);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x92c[1:0]/ 0x930[7:0]/0x944[1:0]",
		   u32tmp[0]&0x3, u32tmp[1]&0xff, u32tmp[2]&0x3);
	CL_PRINTF(cli_buf);


	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x39);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x40);
	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x4c);
	u8tmp[2] = btcoexist->btc_read_1byte(btcoexist, 0x64);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x",
		   "0x38[11]/0x40/0x4c[24:23]/0x64[0]",
		   ((u8tmp[0] & 0x8)>>3), u8tmp[1],
		   ((u32tmp[0]&0x01800000)>>23), u8tmp[2]&0x1);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x550);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x522);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "0x550(bcn ctrl)/0x522", u32tmp[0], u8tmp[0]);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xc50);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x49c);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "0xc50(dig)/0x49c(null-drop)", u32tmp[0]&0xff, u8tmp[0]);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xda0);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0xda4);
	u32tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0xda8);
	u32tmp[3] = btcoexist->btc_read_4byte(btcoexist, 0xcf0);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0xa5b);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0xa5c);

	fa_ofdm = ((u32tmp[0]&0xffff0000) >> 16) +
		  ((u32tmp[1]&0xffff0000) >> 16) +
		   (u32tmp[1] & 0xffff) +
		   (u32tmp[2] & 0xffff) +
		  ((u32tmp[3]&0xffff0000) >> 16) +
		   (u32tmp[3] & 0xffff);
	fa_cck = (u8tmp[0] << 8) + u8tmp[1];

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "OFDM-CCA/OFDM-FA/CCK-FA",
		   u32tmp[0]&0xffff, fa_ofdm, fa_cck);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x6c0);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x6c4);
	u32tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0x6c8);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x6cc);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x",
		   "0x6c0/0x6c4/0x6c8/0x6cc(coexTable)",
		   u32tmp[0], u32tmp[1], u32tmp[2], u8tmp[0]);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "0x770(high-pri rx/tx)",
		   coex_sta->high_priority_rx, coex_sta->high_priority_tx);
	CL_PRINTF(cli_buf);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "0x774(low-pri rx/tx)", coex_sta->low_priority_rx,
		   coex_sta->low_priority_tx);
	CL_PRINTF(cli_buf);
#if (BT_AUTO_REPORT_ONLY_8723B_2ANT == 1)
	btc8723b2ant_monitor_bt_ctr(btcoexist);
#endif
	btcoexist->btc_disp_dbg_msg(btcoexist,
	BTC_DBG_DISP_COEX_STATISTICS);
}


void ex_halbtc8723b2ant_ips_notify(struct btc_coexist *btcoexist, u8 type)
{
	if (BTC_IPS_ENTER == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], IPS ENTER notify\n");
		coex_sta->under_ips = true;
		btc8723b2ant_coex_alloff(btcoexist);
	} else if (BTC_IPS_LEAVE == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], IPS LEAVE notify\n");
		coex_sta->under_ips = false;
	}
}

void ex_halbtc8723b2ant_lps_notify(struct btc_coexist *btcoexist, u8 type)
{
	if (BTC_LPS_ENABLE == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], LPS ENABLE notify\n");
		coex_sta->under_lps = true;
	} else if (BTC_LPS_DISABLE == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], LPS DISABLE notify\n");
		coex_sta->under_lps = false;
	}
}

void ex_halbtc8723b2ant_scan_notify(struct btc_coexist *btcoexist, u8 type)
{
	if (BTC_SCAN_START == type)
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], SCAN START notify\n");
	else if (BTC_SCAN_FINISH == type)
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], SCAN FINISH notify\n");
}

void ex_halbtc8723b2ant_connect_notify(struct btc_coexist *btcoexist, u8 type)
{
	if (BTC_ASSOCIATE_START == type)
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], CONNECT START notify\n");
	else if (BTC_ASSOCIATE_FINISH == type)
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], CONNECT FINISH notify\n");
}

void btc8723b_med_stat_notify(struct btc_coexist *btcoexist,
					    u8 type)
{
	u8 h2c_parameter[3] = {0};
	u32 wifi_bw;
	u8 wifi_central_chnl;

	if (BTC_MEDIA_CONNECT == type)
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], MEDIA connect notify\n");
	else
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], MEDIA disconnect notify\n");

	/* only 2.4G we need to inform bt the chnl mask */
	btcoexist->btc_get(btcoexist,
		BTC_GET_U1_WIFI_CENTRAL_CHNL, &wifi_central_chnl);
	if ((BTC_MEDIA_CONNECT == type) &&
	    (wifi_central_chnl <= 14)) {
		h2c_parameter[0] = 0x1;
		h2c_parameter[1] = wifi_central_chnl;
		btcoexist->btc_get(btcoexist,
			BTC_GET_U4_WIFI_BW, &wifi_bw);
		if (BTC_WIFI_BW_HT40 == wifi_bw)
			h2c_parameter[2] = 0x30;
		else
			h2c_parameter[2] = 0x20;
	}

	coex_dm->wifi_chnl_info[0] = h2c_parameter[0];
	coex_dm->wifi_chnl_info[1] = h2c_parameter[1];
	coex_dm->wifi_chnl_info[2] = h2c_parameter[2];

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], FW write 0x66=0x%x\n",
		  h2c_parameter[0] << 16 | h2c_parameter[1] << 8 |
		  h2c_parameter[2]);

	btcoexist->btc_fill_h2c(btcoexist, 0x66, 3, h2c_parameter);
}

void ex_halbtc8723b2ant_special_packet_notify(struct btc_coexist *btcoexist,
					      u8 type)
{
	if (type == BTC_PACKET_DHCP)
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], DHCP Packet notify\n");
}

void ex_halbtc8723b2ant_bt_info_notify(struct btc_coexist *btcoexist,
				       u8 *tmpbuf, u8 length)
{
	u8 bt_info = 0;
	u8 i, rsp_source = 0;
	bool bt_busy = false, limited_dig = false;
	bool wifi_connected = false;

	coex_sta->c2h_bt_info_req_sent = false;

	rsp_source = tmpbuf[0]&0xf;
	if (rsp_source >= BT_INFO_SRC_8723B_2ANT_MAX)
		rsp_source = BT_INFO_SRC_8723B_2ANT_WIFI_FW;
	coex_sta->bt_info_c2h_cnt[rsp_source]++;

	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
		  "[BTCoex], Bt info[%d], length=%d, hex data=[",
		  rsp_source, length);
	for (i = 0; i < length; i++) {
		coex_sta->bt_info_c2h[rsp_source][i] = tmpbuf[i];
		if (i == 1)
			bt_info = tmpbuf[i];
		if (i == length-1)
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
				  "0x%02x]\n", tmpbuf[i]);
		else
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
				  "0x%02x, ", tmpbuf[i]);
	}

	if (btcoexist->manual_control) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], BtInfoNotify(), "
			  "return for Manual CTRL<===\n");
		return;
	}

	if (BT_INFO_SRC_8723B_2ANT_WIFI_FW != rsp_source) {
		coex_sta->bt_retry_cnt =	/* [3:0]*/
			coex_sta->bt_info_c2h[rsp_source][2] & 0xf;

		coex_sta->bt_rssi =
			coex_sta->bt_info_c2h[rsp_source][3] * 2 + 10;

		coex_sta->bt_info_ext =
			coex_sta->bt_info_c2h[rsp_source][4];

		/* Here we need to resend some wifi info to BT
		 * because bt is reset and loss of the info.
		 */
		if ((coex_sta->bt_info_ext & BIT1)) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], BT ext info bit1 check,"
				  " send wifi BW&Chnl to BT!!\n");
			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
					   &wifi_connected);
			if (wifi_connected)
				btc8723b_med_stat_notify(btcoexist,
							 BTC_MEDIA_CONNECT);
			else
				btc8723b_med_stat_notify(btcoexist,
							 BTC_MEDIA_DISCONNECT);
		}

		if ((coex_sta->bt_info_ext & BIT3)) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], BT ext info bit3 check, "
				  "set BT NOT to ignore Wlan active!!\n");
			btc8723b2ant_ignore_wlan_act(btcoexist, FORCE_EXEC,
						     false);
		} else {
			/* BT already NOT ignore Wlan active, do nothing here.*/
		}
#if (BT_AUTO_REPORT_ONLY_8723B_2ANT == 0)
		if ((coex_sta->bt_info_ext & BIT4)) {
			/* BT auto report already enabled, do nothing*/
		} else {
			btc8723b2ant_bt_auto_report(btcoexist, FORCE_EXEC,
						    true);
		}
#endif
	}

	/* check BIT2 first ==> check if bt is under inquiry or page scan*/
	if (bt_info & BT_INFO_8723B_2ANT_B_INQ_PAGE)
		coex_sta->c2h_bt_inquiry_page = true;
	else
		coex_sta->c2h_bt_inquiry_page = false;

	/* set link exist status*/
	if (!(bt_info & BT_INFO_8723B_2ANT_B_CONNECTION)) {
		coex_sta->bt_link_exist = false;
		coex_sta->pan_exist = false;
		coex_sta->a2dp_exist = false;
		coex_sta->hid_exist = false;
		coex_sta->sco_exist = false;
	} else { /*  connection exists */
		coex_sta->bt_link_exist = true;
		if (bt_info & BT_INFO_8723B_2ANT_B_FTP)
			coex_sta->pan_exist = true;
		else
			coex_sta->pan_exist = false;
		if (bt_info & BT_INFO_8723B_2ANT_B_A2DP)
			coex_sta->a2dp_exist = true;
		else
			coex_sta->a2dp_exist = false;
		if (bt_info & BT_INFO_8723B_2ANT_B_HID)
			coex_sta->hid_exist = true;
		else
			coex_sta->hid_exist = false;
		if (bt_info & BT_INFO_8723B_2ANT_B_SCO_ESCO)
			coex_sta->sco_exist = true;
		else
			coex_sta->sco_exist = false;
	}

	btc8723b2ant_update_bt_link_info(btcoexist);

	if (!(bt_info & BT_INFO_8723B_2ANT_B_CONNECTION)) {
		coex_dm->bt_status = BT_8723B_2ANT_BT_STATUS_NON_CONNECTED_IDLE;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], BtInfoNotify(), "
			  "BT Non-Connected idle!!!\n");
	/* connection exists but no busy */
	} else if (bt_info == BT_INFO_8723B_2ANT_B_CONNECTION) {
		coex_dm->bt_status = BT_8723B_2ANT_BT_STATUS_CONNECTED_IDLE;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], BtInfoNotify(), BT Connected-idle!!!\n");
	} else if ((bt_info & BT_INFO_8723B_2ANT_B_SCO_ESCO) ||
		   (bt_info & BT_INFO_8723B_2ANT_B_SCO_BUSY)) {
		coex_dm->bt_status = BT_8723B_2ANT_BT_STATUS_SCO_BUSY;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], BtInfoNotify(), BT SCO busy!!!\n");
	} else if (bt_info & BT_INFO_8723B_2ANT_B_ACL_BUSY) {
		coex_dm->bt_status = BT_8723B_2ANT_BT_STATUS_ACL_BUSY;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], BtInfoNotify(), BT ACL busy!!!\n");
	} else {
		coex_dm->bt_status = BT_8723B_2ANT_BT_STATUS_MAX;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], BtInfoNotify(), "
			  "BT Non-Defined state!!!\n");
	}

	if ((BT_8723B_2ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) ||
	    (BT_8723B_2ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
	    (BT_8723B_2ANT_BT_STATUS_ACL_SCO_BUSY == coex_dm->bt_status)) {
		bt_busy = true;
		limited_dig = true;
	} else {
		bt_busy = false;
		limited_dig = false;
	}

	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bt_busy);

	coex_dm->limited_dig = limited_dig;
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_LIMITED_DIG, &limited_dig);

	btc8723b2ant_run_coexist_mechanism(btcoexist);
}

void ex_halbtc8723b2ant_stack_operation_notify(struct btc_coexist *btcoexist,
					       u8 type)
{
	if (BTC_STACK_OP_INQ_PAGE_PAIR_START == type)
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex],StackOP Inquiry/page/pair start notify\n");
	else if (BTC_STACK_OP_INQ_PAGE_PAIR_FINISH == type)
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex],StackOP Inquiry/page/pair finish notify\n");
}

void ex_halbtc8723b2ant_halt_notify(struct btc_coexist *btcoexist)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, "[BTCoex], Halt notify\n");

	btc8723b2ant_ignore_wlan_act(btcoexist, FORCE_EXEC, true);
	btc8723b_med_stat_notify(btcoexist, BTC_MEDIA_DISCONNECT);
}

void ex_halbtc8723b2ant_periodical(struct btc_coexist *btcoexist)
{
	struct btc_board_info *board_info = &btcoexist->board_info;
	struct btc_stack_info *stack_info = &btcoexist->stack_info;
	static u8 dis_ver_info_cnt;
	u32 fw_ver = 0, bt_patch_ver = 0;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
		  "[BTCoex], =========================="
		  "Periodical===========================\n");

	if (dis_ver_info_cnt <= 5) {
		dis_ver_info_cnt += 1;
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
			  "[BTCoex], ****************************"
			  "************************************\n");
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
			  "[BTCoex], Ant PG Num/ Ant Mech/ "
			  "Ant Pos = %d/ %d/ %d\n", board_info->pg_ant_num,
			  board_info->btdm_ant_num, board_info->btdm_ant_pos);
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
			  "[BTCoex], BT stack/ hci ext ver = %s / %d\n",
			  ((stack_info->profile_notified) ? "Yes" : "No"),
			  stack_info->hci_version);
		btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER,
				   &bt_patch_ver);
		btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
			  "[BTCoex], CoexVer/ FwVer/ PatchVer = "
			  "%d_%x/ 0x%x/ 0x%x(%d)\n",
			  glcoex_ver_date_8723b_2ant, glcoex_ver_8723b_2ant,
			  fw_ver, bt_patch_ver, bt_patch_ver);
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
			  "[BTCoex], *****************************"
			  "***********************************\n");
	}

#if (BT_AUTO_REPORT_ONLY_8723B_2ANT == 0)
	btc8723b2ant_query_bt_info(btcoexist);
	btc8723b2ant_monitor_bt_ctr(btcoexist);
	btc8723b2ant_monitor_bt_enable_disable(btcoexist);
#else
	if (btc8723b2ant_is_wifi_status_changed(btcoexist) ||
	    coex_dm->auto_tdma_adjust)
		btc8723b2ant_run_coexist_mechanism(btcoexist);
#endif
}
