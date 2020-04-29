// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2012  Realtek Corporation.*/

/************************************************************
 * Description:
 *
 * This file is for RTL8821A Co-exist mechanism
 *
 * History
 * 2012/08/22 Cosa first check in.
 * 2012/11/14 Cosa Revise for 8821A 2Ant out sourcing.
 *
 ************************************************************/

/************************************************************
 * include files
 ************************************************************/
#include "halbt_precomp.h"
/************************************************************
 * Global variables, these are static variables
 ************************************************************/
static struct coex_dm_8821a_2ant glcoex_dm_8821a_2ant;
static struct coex_dm_8821a_2ant *coex_dm = &glcoex_dm_8821a_2ant;
static struct coex_sta_8821a_2ant glcoex_sta_8821a_2ant;
static struct coex_sta_8821a_2ant *coex_sta = &glcoex_sta_8821a_2ant;

static const char *const glbt_info_src_8821a_2ant[] = {
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

static u32 glcoex_ver_date_8821a_2ant = 20130618;
static u32 glcoex_ver_8821a_2ant = 0x5050;

/************************************************************
 * local function proto type if needed
 *
 * local function start with btc8821a2ant_
 ************************************************************/
static u8 btc8821a2ant_bt_rssi_state(struct btc_coexist *btcoexist,
				     u8 level_num, u8 rssi_thresh,
				     u8 rssi_thresh1)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	long bt_rssi = 0;
	u8 bt_rssi_state = coex_sta->pre_bt_rssi_state;

	bt_rssi = coex_sta->bt_rssi;

	if (level_num == 2) {
		if ((coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_STAY_LOW)) {
			if (bt_rssi >=
			    rssi_thresh + BTC_RSSI_COEX_THRESH_TOL_8821A_2ANT) {
				bt_rssi_state = BTC_RSSI_STATE_HIGH;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], BT Rssi state switch to High\n");
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], BT Rssi state stay at Low\n");
			}
		} else {
			if (bt_rssi < rssi_thresh) {
				bt_rssi_state = BTC_RSSI_STATE_LOW;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], BT Rssi state switch to Low\n");
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], BT Rssi state stay at High\n");
			}
		}
	} else if (level_num == 3) {
		if (rssi_thresh > rssi_thresh1) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], BT Rssi thresh error!!\n");
			return coex_sta->pre_bt_rssi_state;
		}

		if ((coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_STAY_LOW)) {
			if (bt_rssi >=
			    (rssi_thresh +
			     BTC_RSSI_COEX_THRESH_TOL_8821A_2ANT)) {
				bt_rssi_state = BTC_RSSI_STATE_MEDIUM;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], BT Rssi state switch to Medium\n");
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], BT Rssi state stay at Low\n");
			}
		} else if ((coex_sta->pre_bt_rssi_state ==
			   BTC_RSSI_STATE_MEDIUM) ||
			   (coex_sta->pre_bt_rssi_state ==
			    BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (bt_rssi >=
			    (rssi_thresh1 +
			     BTC_RSSI_COEX_THRESH_TOL_8821A_2ANT)) {
				bt_rssi_state = BTC_RSSI_STATE_HIGH;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], BT Rssi state switch to High\n");
			} else if (bt_rssi < rssi_thresh) {
				bt_rssi_state = BTC_RSSI_STATE_LOW;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], BT Rssi state switch to Low\n");
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_MEDIUM;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], BT Rssi state stay at Medium\n");
			}
		} else {
			if (bt_rssi < rssi_thresh1) {
				bt_rssi_state = BTC_RSSI_STATE_MEDIUM;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], BT Rssi state switch to Medium\n");
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], BT Rssi state stay at High\n");
			}
		}
	}

	coex_sta->pre_bt_rssi_state = bt_rssi_state;

	return bt_rssi_state;
}

static u8 btc8821a2ant_wifi_rssi_state(struct btc_coexist *btcoexist,
				       u8 index, u8 level_num,
				       u8 rssi_thresh, u8 rssi_thresh1)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	long wifi_rssi = 0;
	u8 wifi_rssi_state = coex_sta->pre_wifi_rssi_state[index];

	btcoexist->btc_get(btcoexist, BTC_GET_S4_WIFI_RSSI, &wifi_rssi);

	if (level_num == 2) {
		if ((coex_sta->pre_wifi_rssi_state[index] ==
		     BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_wifi_rssi_state[index] ==
		     BTC_RSSI_STATE_STAY_LOW)) {
			if (wifi_rssi >=
			    (rssi_thresh+BTC_RSSI_COEX_THRESH_TOL_8821A_2ANT)) {
				wifi_rssi_state = BTC_RSSI_STATE_HIGH;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], wifi RSSI state switch to High\n");
			} else {
				wifi_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], wifi RSSI state stay at Low\n");
			}
		} else {
			if (wifi_rssi < rssi_thresh) {
				wifi_rssi_state = BTC_RSSI_STATE_LOW;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], wifi RSSI state switch to Low\n");
			} else {
				wifi_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], wifi RSSI state stay at High\n");
			}
		}
	} else if (level_num == 3) {
		if (rssi_thresh > rssi_thresh1) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], wifi RSSI thresh error!!\n");
			return coex_sta->pre_wifi_rssi_state[index];
		}

		if ((coex_sta->pre_wifi_rssi_state[index] ==
		    BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_wifi_rssi_state[index] ==
		     BTC_RSSI_STATE_STAY_LOW)) {
			if (wifi_rssi >=
			    (rssi_thresh +
			     BTC_RSSI_COEX_THRESH_TOL_8821A_2ANT)) {
				wifi_rssi_state = BTC_RSSI_STATE_MEDIUM;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], wifi RSSI state switch to Medium\n");
			} else {
				wifi_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], wifi RSSI state stay at Low\n");
			}
		} else if ((coex_sta->pre_wifi_rssi_state[index] ==
			   BTC_RSSI_STATE_MEDIUM) ||
			   (coex_sta->pre_wifi_rssi_state[index] ==
			    BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (wifi_rssi >= (rssi_thresh1 +
			    BTC_RSSI_COEX_THRESH_TOL_8821A_2ANT)) {
				wifi_rssi_state = BTC_RSSI_STATE_HIGH;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], wifi RSSI state switch to High\n");
			} else if (wifi_rssi < rssi_thresh) {
				wifi_rssi_state = BTC_RSSI_STATE_LOW;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], wifi RSSI state switch to Low\n");
			} else {
				wifi_rssi_state = BTC_RSSI_STATE_STAY_MEDIUM;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], wifi RSSI state stay at Medium\n");
			}
		} else {
			if (wifi_rssi < rssi_thresh1) {
				wifi_rssi_state = BTC_RSSI_STATE_MEDIUM;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], wifi RSSI state switch to Medium\n");
			} else {
				wifi_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], wifi RSSI state stay at High\n");
			}
		}
	}
	coex_sta->pre_wifi_rssi_state[index] = wifi_rssi_state;

	return wifi_rssi_state;
}

static
void btc8821a2ant_limited_rx(struct btc_coexist *btcoexist, bool force_exec,
			     bool rej_ap_agg_pkt, bool bt_ctrl_agg_buf_size,
			     u8 agg_buf_size)
{
	bool reject_rx_agg = rej_ap_agg_pkt;
	bool bt_ctrl_rx_agg_size = bt_ctrl_agg_buf_size;
	u8 rx_agg_size = agg_buf_size;

	/* Rx Aggregation related setting */
	btcoexist->btc_set(btcoexist, BTC_SET_BL_TO_REJ_AP_AGG_PKT,
			   &reject_rx_agg);
	/* decide BT control aggregation buf size or not */
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_CTRL_AGG_SIZE,
			   &bt_ctrl_rx_agg_size);
	/* aggregation buf size, works when BT control Rx aggregation size */
	btcoexist->btc_set(btcoexist, BTC_SET_U1_AGG_BUF_SIZE, &rx_agg_size);
	/* real update aggregation setting */
	btcoexist->btc_set(btcoexist, BTC_SET_ACT_AGGREGATE_CTRL, NULL);
}

static void btc8821a2ant_monitor_bt_ctr(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	u32 reg_hp_txrx, reg_lp_txrx, u4tmp;
	u32 reg_hp_tx = 0, reg_hp_rx = 0, reg_lp_tx = 0, reg_lp_rx = 0;

	reg_hp_txrx = 0x770;
	reg_lp_txrx = 0x774;

	u4tmp = btcoexist->btc_read_4byte(btcoexist, reg_hp_txrx);
	reg_hp_tx = u4tmp & MASKLWORD;
	reg_hp_rx = (u4tmp & MASKHWORD) >> 16;

	u4tmp = btcoexist->btc_read_4byte(btcoexist, reg_lp_txrx);
	reg_lp_tx = u4tmp & MASKLWORD;
	reg_lp_rx = (u4tmp & MASKHWORD) >> 16;

	coex_sta->high_priority_tx = reg_hp_tx;
	coex_sta->high_priority_rx = reg_hp_rx;
	coex_sta->low_priority_tx = reg_lp_tx;
	coex_sta->low_priority_rx = reg_lp_rx;

	if ((coex_sta->low_priority_rx >= 950) &&
	    (coex_sta->low_priority_rx >= coex_sta->low_priority_tx) &&
	    (!coex_sta->under_ips))
		bt_link_info->slave_role = true;
	else
		bt_link_info->slave_role = false;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], High Priority Tx/Rx (reg 0x%x) = 0x%x(%d)/0x%x(%d)\n",
		    reg_hp_txrx, reg_hp_tx, reg_hp_tx, reg_hp_rx, reg_hp_rx);
	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], Low Priority Tx/Rx (reg 0x%x) = 0x%x(%d)/0x%x(%d)\n",
		 reg_lp_txrx, reg_lp_tx, reg_lp_tx, reg_lp_rx, reg_lp_rx);

	/* reset counter */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0xc);
}

static void btc8821a2ant_monitor_wifi_ctr(struct btc_coexist *btcoexist)
{
	if (coex_sta->under_ips) {
		coex_sta->crc_ok_cck = 0;
		coex_sta->crc_ok_11g = 0;
		coex_sta->crc_ok_11n = 0;
		coex_sta->crc_ok_11n_agg = 0;

		coex_sta->crc_err_cck = 0;
		coex_sta->crc_err_11g = 0;
		coex_sta->crc_err_11n = 0;
		coex_sta->crc_err_11n_agg = 0;
	} else {
		coex_sta->crc_ok_cck =
			btcoexist->btc_read_4byte(btcoexist, 0xf88);
		coex_sta->crc_ok_11g =
			btcoexist->btc_read_2byte(btcoexist, 0xf94);
		coex_sta->crc_ok_11n =
			btcoexist->btc_read_2byte(btcoexist, 0xf90);
		coex_sta->crc_ok_11n_agg =
			btcoexist->btc_read_2byte(btcoexist, 0xfb8);

		coex_sta->crc_err_cck =
			btcoexist->btc_read_4byte(btcoexist, 0xf84);
		coex_sta->crc_err_11g =
			btcoexist->btc_read_2byte(btcoexist, 0xf96);
		coex_sta->crc_err_11n =
			btcoexist->btc_read_2byte(btcoexist, 0xf92);
		coex_sta->crc_err_11n_agg =
			btcoexist->btc_read_2byte(btcoexist, 0xfba);
	}

	/* reset counter */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0xf16, 0x1, 0x1);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0xf16, 0x1, 0x0);
}

static void btc8821a2ant_query_bt_info(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 h2c_parameter[1] = {0};

	coex_sta->c2h_bt_info_req_sent = true;

	h2c_parameter[0] |= BIT0; /* trigger */

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], Query Bt Info, FW write 0x61 = 0x%x\n",
		 h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x61, 1, h2c_parameter);
}

static bool btc8821a2ant_is_wifi_status_changed(struct btc_coexist *btcoexist)
{
	static bool pre_wifi_busy = true;
	static bool pre_under_4way = true;
	static bool pre_bt_hs_on = true;
	bool wifi_busy = false, under_4way = false, bt_hs_on = false;
	bool wifi_connected = false;
	u8 wifi_rssi_state = BTC_RSSI_STATE_HIGH;

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

		wifi_rssi_state = btc8821a2ant_wifi_rssi_state(btcoexist, 3, 2,
				BT_8821A_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);

		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_LOW))
			return true;
	}

	return false;
}

static void btc8821a2ant_update_bt_link_info(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool bt_hs_on = false;

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

static u8 btc8821a2ant_action_algorithm(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool bt_hs_on = false;
	u8 algorithm = BT_8821A_2ANT_COEX_ALGO_UNDEFINED;
	u8 num_of_diff_profile = 0;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);

	if (!bt_link_info->bt_link_exist) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
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
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], SCO only\n");
			algorithm = BT_8821A_2ANT_COEX_ALGO_SCO;
		} else {
			if (bt_link_info->hid_exist) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], HID only\n");
				algorithm = BT_8821A_2ANT_COEX_ALGO_HID;
			} else if (bt_link_info->a2dp_exist) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], A2DP only\n");
				algorithm = BT_8821A_2ANT_COEX_ALGO_A2DP;
			} else if (bt_link_info->pan_exist) {
				if (bt_hs_on) {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], PAN(HS) only\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_PANHS;
				} else {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], PAN(EDR) only\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_PANEDR;
				}
			}
		}
	} else if (num_of_diff_profile == 2) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], SCO + HID\n");
				algorithm = BT_8821A_2ANT_COEX_ALGO_SCO;
			} else if (bt_link_info->a2dp_exist) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], SCO + A2DP ==> SCO\n");
				algorithm = BT_8821A_2ANT_COEX_ALGO_SCO;
			} else if (bt_link_info->pan_exist) {
				if (bt_hs_on) {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], SCO + PAN(HS)\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_SCO;
				} else {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], SCO + PAN(EDR)\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_SCO;
				}
			}
		} else {
			if (bt_link_info->hid_exist &&
			    bt_link_info->a2dp_exist) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], HID + A2DP\n");
				algorithm = BT_8821A_2ANT_COEX_ALGO_HID_A2DP;
			} else if (bt_link_info->hid_exist &&
				bt_link_info->pan_exist) {
				if (bt_hs_on) {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], HID + PAN(HS)\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_HID;
				} else {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], HID + PAN(EDR)\n");
					algorithm =
					    BT_8821A_2ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (bt_link_info->pan_exist &&
				bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], A2DP + PAN(HS)\n");
					algorithm =
					    BT_8821A_2ANT_COEX_ALGO_A2DP_PANHS;
				} else {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], A2DP + PAN(EDR)\n");
					algorithm =
					    BT_8821A_2ANT_COEX_ALGO_PANEDR_A2DP;
				}
			}
		}
	} else if (num_of_diff_profile == 3) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist &&
			    bt_link_info->a2dp_exist) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], SCO + HID + A2DP ==> HID\n");
				algorithm = BT_8821A_2ANT_COEX_ALGO_SCO;
			} else if (bt_link_info->hid_exist &&
				bt_link_info->pan_exist) {
				if (bt_hs_on) {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], SCO + HID + PAN(HS)\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_SCO;
				} else {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], SCO + HID + PAN(EDR)\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_SCO;
				}
			} else if (bt_link_info->pan_exist &&
				   bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], SCO + A2DP + PAN(HS)\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_SCO;
				} else {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], SCO + A2DP + PAN(EDR) ==> HID\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_SCO;
				}
			}
		} else {
			if (bt_link_info->hid_exist &&
			    bt_link_info->pan_exist &&
			    bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], HID + A2DP + PAN(HS)\n");
					algorithm =
					    BT_8821A_2ANT_COEX_ALGO_HID_A2DP;
				} else {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], HID + A2DP + PAN(EDR)\n");
					algorithm =
					BT_8821A_2ANT_COEX_ALGO_HID_A2DP_PANEDR;
				}
			}
		}
	} else if (num_of_diff_profile >= 3) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist &&
			    bt_link_info->pan_exist &&
			    bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], Error!!! SCO + HID + A2DP + PAN(HS)\n");

				} else {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], SCO + HID + A2DP + PAN(EDR)==>PAN(EDR)+HID\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_SCO;
				}
			}
		}
	}
	return algorithm;
}

static void btc8821a2ant_set_fw_dac_swing_lvl(struct btc_coexist *btcoexist,
					      u8 dac_swing_lvl)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 h2c_parameter[1] = {0};

	/* There are several type of dacswing
	 * 0x18/ 0x10/ 0xc/ 0x8/ 0x4/ 0x6
	 */
	h2c_parameter[0] = dac_swing_lvl;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], Set Dac Swing Level = 0x%x\n", dac_swing_lvl);
	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], FW write 0x64 = 0x%x\n", h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x64, 1, h2c_parameter);
}

static void btc8821a2ant_set_fw_dec_bt_pwr(struct btc_coexist *btcoexist,
					   u8 dec_bt_pwr_lvl)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 h2c_parameter[1] = {0};

	h2c_parameter[0] = dec_bt_pwr_lvl;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], decrease Bt Power Level : %u, FW write 0x62 = 0x%x\n",
		 dec_bt_pwr_lvl, h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x62, 1, h2c_parameter);
}

static void btc8821a2ant_dec_bt_pwr(struct btc_coexist *btcoexist,
				    bool force_exec, u8 dec_bt_pwr_lvl)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], %s Dec BT power level = %u\n",
		    (force_exec ? "force to" : ""), dec_bt_pwr_lvl);
	coex_dm->cur_dec_bt_pwr_lvl = dec_bt_pwr_lvl;

	if (!force_exec) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], pre_dec_bt_pwr_lvl = %d, cur_dec_bt_pwr_lvl = %d\n",
			    coex_dm->pre_dec_bt_pwr_lvl,
			    coex_dm->cur_dec_bt_pwr_lvl);

		if (coex_dm->pre_dec_bt_pwr_lvl == coex_dm->cur_dec_bt_pwr_lvl)
			return;
	}
	btc8821a2ant_set_fw_dec_bt_pwr(btcoexist, coex_dm->cur_dec_bt_pwr_lvl);

	coex_dm->pre_dec_bt_pwr_lvl = coex_dm->cur_dec_bt_pwr_lvl;
}

static void btc8821a2ant_fw_dac_swing_lvl(struct btc_coexist *btcoexist,
					  bool force_exec, u8 fw_dac_swing_lvl)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], %s set FW Dac Swing level = %d\n",
		 (force_exec ? "force to" : ""), fw_dac_swing_lvl);
	coex_dm->cur_fw_dac_swing_lvl = fw_dac_swing_lvl;

	if (!force_exec) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], pre_fw_dac_swing_lvl = %d, cur_fw_dac_swing_lvl = %d\n",
			 coex_dm->pre_fw_dac_swing_lvl,
			 coex_dm->cur_fw_dac_swing_lvl);

		if (coex_dm->pre_fw_dac_swing_lvl ==
		    coex_dm->cur_fw_dac_swing_lvl)
			return;
	}

	btc8821a2ant_set_fw_dac_swing_lvl(btcoexist,
					  coex_dm->cur_fw_dac_swing_lvl);

	coex_dm->pre_fw_dac_swing_lvl = coex_dm->cur_fw_dac_swing_lvl;
}

static void btc8821a2ant_set_sw_penalty_tx_rate_adaptive(
		struct btc_coexist *btcoexist, bool low_penalty_ra)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 h2c_parameter[6] = {0};

	h2c_parameter[0] = 0x6;	/* opCode, 0x6 = Retry_Penalty */

	if (low_penalty_ra) {
		h2c_parameter[1] |= BIT0;
		/* normal rate except MCS7/6/5, OFDM54/48/36 */
		h2c_parameter[2] = 0x00;
		/* MCS7 or OFDM54 */
		h2c_parameter[3] = 0xf5;
		/* MCS6 or OFDM48 */
		h2c_parameter[4] = 0xa0;
		/* MCS5 or OFDM36 */
		h2c_parameter[5] = 0xa0;
	}

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], set WiFi Low-Penalty Retry: %s",
		 (low_penalty_ra ? "ON!!" : "OFF!!"));

	btcoexist->btc_fill_h2c(btcoexist, 0x69, 6, h2c_parameter);
}

static void btc8821a2ant_low_penalty_ra(struct btc_coexist *btcoexist,
					bool force_exec, bool low_penalty_ra)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], %s turn LowPenaltyRA = %s\n",
		 (force_exec ? "force to" : ""),
		 ((low_penalty_ra) ? "ON" : "OFF"));
	coex_dm->cur_low_penalty_ra = low_penalty_ra;

	if (!force_exec) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], pre_low_penalty_ra = %d, cur_low_penalty_ra = %d\n",
			 coex_dm->pre_low_penalty_ra,
			 coex_dm->cur_low_penalty_ra);

		if (coex_dm->pre_low_penalty_ra == coex_dm->cur_low_penalty_ra)
			return;
	}
	btc8821a2ant_set_sw_penalty_tx_rate_adaptive(btcoexist,
					 coex_dm->cur_low_penalty_ra);

	coex_dm->pre_low_penalty_ra = coex_dm->cur_low_penalty_ra;
}

static void btc8821a2ant_set_dac_swing_reg(struct btc_coexist *btcoexist,
					   u32 level)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 val = (u8)level;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], Write SwDacSwing = 0x%x\n", level);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0xc5b, 0x3e, val);
}

static void btc8821a2ant_set_sw_full_dac_swing(struct btc_coexist *btcoexist,
					       bool sw_dac_swing_on,
					       u32 sw_dac_swing_lvl)
{
	if (sw_dac_swing_on)
		btc8821a2ant_set_dac_swing_reg(btcoexist, sw_dac_swing_lvl);
	else
		btc8821a2ant_set_dac_swing_reg(btcoexist, 0x18);
}

static void btc8821a2ant_dac_swing(struct btc_coexist *btcoexist,
				   bool force_exec, bool dac_swing_on,
				   u32 dac_swing_lvl)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], %s turn DacSwing = %s, dac_swing_lvl = 0x%x\n",
		 (force_exec ? "force to" : ""),
		 ((dac_swing_on) ? "ON" : "OFF"),
		 dac_swing_lvl);
	coex_dm->cur_dac_swing_on = dac_swing_on;
	coex_dm->cur_dac_swing_lvl = dac_swing_lvl;

	if (!force_exec) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], pre_dac_swing_on = %d, pre_dac_swing_lvl = 0x%x, cur_dac_swing_on = %d, cur_dac_swing_lvl = 0x%x\n",
			 coex_dm->pre_dac_swing_on,
			 coex_dm->pre_dac_swing_lvl,
			 coex_dm->cur_dac_swing_on,
			 coex_dm->cur_dac_swing_lvl);

		if ((coex_dm->pre_dac_swing_on == coex_dm->cur_dac_swing_on) &&
		    (coex_dm->pre_dac_swing_lvl ==
		     coex_dm->cur_dac_swing_lvl))
			return;
	}
	mdelay(30);
	btc8821a2ant_set_sw_full_dac_swing(btcoexist, dac_swing_on,
					   dac_swing_lvl);

	coex_dm->pre_dac_swing_on = coex_dm->cur_dac_swing_on;
	coex_dm->pre_dac_swing_lvl = coex_dm->cur_dac_swing_lvl;
}

static void btc8821a2ant_set_coex_table(struct btc_coexist *btcoexist,
					u32 val0x6c0, u32 val0x6c4,
					u32 val0x6c8, u8 val0x6cc)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], set coex table, set 0x6c0 = 0x%x\n", val0x6c0);
	btcoexist->btc_write_4byte(btcoexist, 0x6c0, val0x6c0);

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], set coex table, set 0x6c4 = 0x%x\n", val0x6c4);
	btcoexist->btc_write_4byte(btcoexist, 0x6c4, val0x6c4);

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], set coex table, set 0x6c8 = 0x%x\n", val0x6c8);
	btcoexist->btc_write_4byte(btcoexist, 0x6c8, val0x6c8);

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], set coex table, set 0x6cc = 0x%x\n", val0x6cc);
	btcoexist->btc_write_1byte(btcoexist, 0x6cc, val0x6cc);
}

static void btc8821a2ant_coex_table(struct btc_coexist *btcoexist,
				    bool force_exec, u32 val0x6c0,
				    u32 val0x6c4, u32 val0x6c8, u8 val0x6cc)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], %s write Coex Table 0x6c0 = 0x%x, 0x6c4 = 0x%x, 0x6c8 = 0x%x, 0x6cc = 0x%x\n",
		 (force_exec ? "force to" : ""),
		 val0x6c0, val0x6c4, val0x6c8, val0x6cc);
	coex_dm->cur_val0x6c0 = val0x6c0;
	coex_dm->cur_val0x6c4 = val0x6c4;
	coex_dm->cur_val0x6c8 = val0x6c8;
	coex_dm->cur_val0x6cc = val0x6cc;

	if (!force_exec) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], pre_val0x6c0 = 0x%x, pre_val0x6c4 = 0x%x, pre_val0x6c8 = 0x%x, pre_val0x6cc = 0x%x !!\n",
			 coex_dm->pre_val0x6c0,
			 coex_dm->pre_val0x6c4,
			 coex_dm->pre_val0x6c8,
			 coex_dm->pre_val0x6cc);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], cur_val0x6c0 = 0x%x, cur_val0x6c4 = 0x%x, cur_val0x6c8 = 0x%x, cur_val0x6cc = 0x%x !!\n",
			 coex_dm->cur_val0x6c0,
			 coex_dm->cur_val0x6c4,
			 coex_dm->cur_val0x6c8,
			 coex_dm->cur_val0x6cc);

		if ((coex_dm->pre_val0x6c0 == coex_dm->cur_val0x6c0) &&
		    (coex_dm->pre_val0x6c4 == coex_dm->cur_val0x6c4) &&
		    (coex_dm->pre_val0x6c8 == coex_dm->cur_val0x6c8) &&
		    (coex_dm->pre_val0x6cc == coex_dm->cur_val0x6cc))
			return;
	}
	btc8821a2ant_set_coex_table(btcoexist, val0x6c0, val0x6c4, val0x6c8,
				    val0x6cc);

	coex_dm->pre_val0x6c0 = coex_dm->cur_val0x6c0;
	coex_dm->pre_val0x6c4 = coex_dm->cur_val0x6c4;
	coex_dm->pre_val0x6c8 = coex_dm->cur_val0x6c8;
	coex_dm->pre_val0x6cc = coex_dm->cur_val0x6cc;
}

static void btc8821a2ant_coex_table_with_type(struct btc_coexist *btcoexist,
					      bool force_exec, u8 type)
{
	coex_sta->coex_table_type = type;

	switch (type) {
	case 0:
		btc8821a2ant_coex_table(btcoexist, force_exec, 0x55555555,
					0x55555555, 0xffffff, 0x3);
		break;
	case 1:
		btc8821a2ant_coex_table(btcoexist, force_exec, 0x55555555,
					0x5afa5afa, 0xffffff, 0x3);
		break;
	case 2:
		btc8821a2ant_coex_table(btcoexist, force_exec, 0x5ada5ada,
					0x5ada5ada, 0xffffff, 0x3);
		break;
	case 3:
		btc8821a2ant_coex_table(btcoexist, force_exec, 0xaaaaaaaa,
					0xaaaaaaaa, 0xffffff, 0x3);
		break;
	case 4:
		btc8821a2ant_coex_table(btcoexist, force_exec, 0xffffffff,
					0xffffffff, 0xffffff, 0x3);
		break;
	case 5:
		btc8821a2ant_coex_table(btcoexist, force_exec, 0x5fff5fff,
					0x5fff5fff, 0xffffff, 0x3);
		break;
	case 6:
		btc8821a2ant_coex_table(btcoexist, force_exec, 0x55ff55ff,
					0x5a5a5a5a, 0xffffff, 0x3);
		break;
	case 7:
		btc8821a2ant_coex_table(btcoexist, force_exec, 0x55dd55dd,
					0x5ada5ada, 0xffffff, 0x3);
		break;
	case 8:
		btc8821a2ant_coex_table(btcoexist, force_exec, 0x55dd55dd,
					0x5ada5ada, 0xffffff, 0x3);
		break;
	case 9:
		btc8821a2ant_coex_table(btcoexist, force_exec, 0x55dd55dd,
					0x5ada5ada, 0xffffff, 0x3);
		break;
	case 10:
		btc8821a2ant_coex_table(btcoexist, force_exec, 0x55dd55dd,
					0x5ada5ada, 0xffffff, 0x3);
		break;
	case 11:
		btc8821a2ant_coex_table(btcoexist, force_exec, 0x55dd55dd,
					0x5ada5ada, 0xffffff, 0x3);
		break;
	case 12:
		btc8821a2ant_coex_table(btcoexist, force_exec, 0x55dd55dd,
					0x5ada5ada, 0xffffff, 0x3);
		break;
	case 13:
		btc8821a2ant_coex_table(btcoexist, force_exec, 0x5fff5fff,
					0xaaaaaaaa, 0xffffff, 0x3);
		break;
	case 14:
		btc8821a2ant_coex_table(btcoexist, force_exec, 0x5fff5fff,
					0x5ada5ada, 0xffffff, 0x3);
		break;
	case 15:
		btc8821a2ant_coex_table(btcoexist, force_exec, 0x55dd55dd,
					0xaaaaaaaa, 0xffffff, 0x3);
		break;
	case 16:
		btc8821a2ant_coex_table(btcoexist, force_exec, 0x5fdf5fdf,
					0x5fdb5fdb, 0xffffff, 0x3);
		break;
	case 17:
		btc8821a2ant_coex_table(btcoexist, force_exec, 0xfafafafa,
					0xfafafafa, 0xffffff, 0x3);
		break;
	default:
		break;
	}
}

static void btc8821a2ant_set_fw_ignore_wlan_act(struct btc_coexist *btcoex,
						bool enable)
{
	struct rtl_priv *rtlpriv = btcoex->adapter;
	u8 h2c_parameter[1] = {0};

	if (enable)
		h2c_parameter[0] |= BIT0; /* function enable */

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], set FW for BT Ignore Wlan_Act, FW write 0x63 = 0x%x\n",
		 h2c_parameter[0]);

	btcoex->btc_fill_h2c(btcoex, 0x63, 1, h2c_parameter);
}

static void btc8821a2ant_set_lps_rpwm(struct btc_coexist *btcoexist, u8 lps_val,
				      u8 rpwm_val)
{
	u8 lps = lps_val;
	u8 rpwm = rpwm_val;

	btcoexist->btc_set(btcoexist, BTC_SET_U1_LPS_VAL, &lps);
	btcoexist->btc_set(btcoexist, BTC_SET_U1_RPWM_VAL, &rpwm);
}

static void btc8821a2ant_lps_rpwm(struct btc_coexist *btcoexist,
				  bool force_exec, u8 lps_val, u8 rpwm_val)
{
	coex_dm->cur_lps = lps_val;
	coex_dm->cur_rpwm = rpwm_val;

	if (!force_exec) {
		if ((coex_dm->pre_lps == coex_dm->cur_lps) &&
		    (coex_dm->pre_rpwm == coex_dm->cur_rpwm))
			return;
	}
	btc8821a2ant_set_lps_rpwm(btcoexist, lps_val, rpwm_val);

	coex_dm->pre_lps = coex_dm->cur_lps;
	coex_dm->pre_rpwm = coex_dm->cur_rpwm;
}

static void btc8821a2ant_ignore_wlan_act(struct btc_coexist *btcoexist,
					 bool force_exec, bool enable)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], %s turn Ignore WlanAct %s\n",
		 (force_exec ? "force to" : ""), (enable ? "ON" : "OFF"));
	coex_dm->cur_ignore_wlan_act = enable;

	if (!force_exec) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], pre_ignore_wlan_act = %d, cur_ignore_wlan_act = %d!!\n",
			 coex_dm->pre_ignore_wlan_act,
			 coex_dm->cur_ignore_wlan_act);

		if (coex_dm->pre_ignore_wlan_act ==
		    coex_dm->cur_ignore_wlan_act)
			return;
	}
	btc8821a2ant_set_fw_ignore_wlan_act(btcoexist, enable);

	coex_dm->pre_ignore_wlan_act = coex_dm->cur_ignore_wlan_act;
}

static void btc8821a2ant_set_fw_ps_tdma(struct btc_coexist *btcoexist,
					u8 byte1, u8 byte2, u8 byte3,
					u8 byte4, u8 byte5)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
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

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], FW write 0x60(5bytes) = 0x%x%08x\n",
		 h2c_parameter[0],
		 h2c_parameter[1] << 24 |
		 h2c_parameter[2] << 16 |
		 h2c_parameter[3] << 8 |
		 h2c_parameter[4]);

	btcoexist->btc_fill_h2c(btcoexist, 0x60, 5, h2c_parameter);
}

static void btc8821a2ant_sw_mechanism1(struct btc_coexist *btcoexist,
				       bool shrink_rx_lpf, bool low_penalty_ra,
				       bool limited_dig, bool bt_lna_constrain)
{
	btc8821a2ant_low_penalty_ra(btcoexist, NORMAL_EXEC, low_penalty_ra);
}

static void btc8821a2ant_sw_mechanism2(struct btc_coexist *btcoexist,
				       bool agc_table_shift, bool adc_back_off,
				       bool sw_dac_swing, u32 dac_swing_lvl)
{
	btc8821a2ant_dac_swing(btcoexist, NORMAL_EXEC, sw_dac_swing,
			       dac_swing_lvl);
}

static void btc8821a2ant_set_ant_path(struct btc_coexist *btcoexist,
				      u8 ant_pos_type, bool init_hw_cfg,
				      bool wifi_off)
{
	struct btc_board_info *board_info = &btcoexist->board_info;
	u32 u4tmp = 0;
	u8 h2c_parameter[2] = {0};

	if (init_hw_cfg) {
		/*  0x4c[23] = 0, 0x4c[24] = 1  Antenna control by WL/BT */
		u4tmp = btcoexist->btc_read_4byte(btcoexist, 0x4c);
		u4tmp &= ~BIT23;
		u4tmp |= BIT24;
		btcoexist->btc_write_4byte(btcoexist, 0x4c, u4tmp);

		btcoexist->btc_write_4byte(btcoexist, 0x974, 0x3ff);

		if (board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT) {
			/* tell firmware "antenna inverse"  ==> WRONG firmware
			 * antenna control code ==>need fw to fix
			 */
			h2c_parameter[0] = 1;
			h2c_parameter[1] = 1;
			btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
						h2c_parameter);
		} else {
			/* tell firmware "no antenna inverse" ==> WRONG firmware
			 * antenna control code ==>need fw to fix
			 */
			h2c_parameter[0] = 0;
			h2c_parameter[1] = 1;
			btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
						h2c_parameter);
		}
	}

	/* ext switch setting */
	switch (ant_pos_type) {
	case BTC_ANT_WIFI_AT_MAIN:
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcb7, 0x30, 0x1);
		break;
	case BTC_ANT_WIFI_AT_AUX:
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcb7, 0x30, 0x2);
		break;
	}
}

static void btc8821a2ant_ps_tdma(struct btc_coexist *btcoexist,
				 bool force_exec, bool turn_on, u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	u8 wifi_rssi_state, bt_rssi_state;

	wifi_rssi_state = btc8821a2ant_wifi_rssi_state(btcoexist, 1, 2,
				BT_8821A_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);
	bt_rssi_state = btc8821a2ant_bt_rssi_state(btcoexist, 2,
				BT_8821A_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	if (!(BTC_RSSI_HIGH(wifi_rssi_state) &&
	      BTC_RSSI_HIGH(bt_rssi_state)) &&
	    turn_on) {
		/* for WiFi RSSI low or BT RSSI low */
		type = type + 100;
	}

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], %s turn %s PS TDMA, type = %d\n",
		 (force_exec ? "force to" : ""), (turn_on ? "ON" : "OFF"),
		 type);
	coex_dm->cur_ps_tdma_on = turn_on;
	coex_dm->cur_ps_tdma = type;

	if (!force_exec) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], pre_ps_tdma_on = %d, cur_ps_tdma_on = %d!!\n",
			 coex_dm->pre_ps_tdma_on, coex_dm->cur_ps_tdma_on);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], pre_ps_tdma = %d, cur_ps_tdma = %d!!\n",
			 coex_dm->pre_ps_tdma, coex_dm->cur_ps_tdma);

		if ((coex_dm->pre_ps_tdma_on == coex_dm->cur_ps_tdma_on) &&
		    (coex_dm->pre_ps_tdma == coex_dm->cur_ps_tdma))
			return;
	}
	if (turn_on) {
		switch (type) {
		case 1:
		default:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x3c,
						    0x03, 0xf1, 0x90);
			break;
		case 2:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x2d,
						    0x03, 0xf1, 0x90);
			break;
		case 3:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x1c,
						    0x3, 0xf1, 0x90);
			break;
		case 4:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x10,
						    0x03, 0xf1, 0x90);
			break;
		case 5:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x3c,
						    0x3, 0x70, 0x90);
			break;
		case 6:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x2d,
						    0x3, 0x70, 0x90);
			break;
		case 7:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x1c,
						    0x3, 0x70, 0x90);
			break;
		case 8:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xa3, 0x10,
						    0x3, 0x70, 0x90);
			break;
		case 9:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x3c,
						    0x03, 0xf1, 0x90);
			break;
		case 10:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x2d,
						    0x03, 0xf1, 0x90);
			break;
		case 11:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x1c,
						    0x3, 0xf1, 0x90);
			break;
		case 12:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x10,
						    0x3, 0xf1, 0x90);
			break;
		case 13:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x3c,
						    0x3, 0x70, 0x90);
			break;
		case 14:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x2d,
						    0x3, 0x70, 0x90);
			break;
		case 15:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x1c,
						    0x3, 0x70, 0x90);
			break;
		case 16:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x10,
						    0x3, 0x70, 0x90);
			break;
		case 17:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xa3, 0x2f,
						    0x2f, 0x60, 0x90);
			break;
		case 18:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x5, 0x5,
						    0xe1, 0x90);
			break;
		case 19:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x25,
						    0x25, 0xe1, 0x90);
			break;
		case 20:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x25,
						    0x25, 0x60, 0x90);
			break;
		case 21:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x15,
						    0x03, 0x70, 0x90);
			break;
		case 23:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x1e,
						    0x03, 0xf0, 0x14);
			break;
		case 24:
		case 124:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xd3, 0x3c,
						    0x03, 0x70, 0x50);
			break;
		case 25:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x14,
						    0x03, 0xf1, 0x90);
			break;
		case 26:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x30,
						    0x03, 0xf1, 0x90);
			break;
		case 71:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x3c,
						    0x03, 0xf1, 0x90);
			break;
		case 101:
		case 105:
		case 171:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xd3, 0x3a,
						    0x03, 0x70, 0x50);
			break;
		case 102:
		case 106:
		case 110:
		case 114:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xd3, 0x2d,
						    0x03, 0x70, 0x50);
			break;
		case 103:
		case 107:
		case 111:
		case 115:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xd3, 0x1c,
						    0x03, 0x70, 0x50);
			break;
		case 104:
		case 108:
		case 112:
		case 116:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xd3, 0x10,
						    0x03, 0x70, 0x50);
			break;
		case 109:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x3c,
						    0x03, 0xf1, 0x90);
			break;
		case 113:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x3c,
						    0x03, 0x70, 0x90);
			break;
		case 121:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x15,
						    0x03, 0x70, 0x90);
			break;
		case 22:
		case 122:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x35,
						    0x03, 0x71, 0x11);
			break;
		case 123:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xd3, 0x1c,
						    0x03, 0x70, 0x54);
			break;
		case 125:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xd3, 0x14,
						    0x03, 0x70, 0x50);
			break;
		case 126:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0xd3, 0x30,
						    0x03, 0x70, 0x50);
			break;
		}
	} else {
		/* disable PS tdma */
		switch (type) {
		case 0:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0x0, 0x0, 0x0,
						    0x40, 0x0);
			break;
		case 1:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0x0, 0x0, 0x0,
						    0x48, 0x0);
			break;
		default:
			btc8821a2ant_set_fw_ps_tdma(btcoexist, 0x0, 0x0, 0x0,
						    0x40, 0x0);
			break;
		}
	}

	/* update pre state */
	coex_dm->pre_ps_tdma_on = coex_dm->cur_ps_tdma_on;
	coex_dm->pre_ps_tdma = coex_dm->cur_ps_tdma;
}

static void
btc8821a2ant_ps_tdma_check_for_power_save_state(struct btc_coexist *btcoexist,
						bool new_ps_state)
{
	u8 lps_mode = 0x0;

	btcoexist->btc_get(btcoexist, BTC_GET_U1_LPS_MODE, &lps_mode);

	if (lps_mode) {
		/* already under LPS state */
		if (new_ps_state) {
			/* keep state under LPS, do nothing */
		} else {
			/* will leave LPS state, turn off psTdma first */
			btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
		}
	} else {
		/* NO PS state */
		if (new_ps_state) {
			/* will enter LPS state, turn off psTdma first */
			btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
		} else {
			/* keep state under NO PS state, do nothing */
		}
	}
}

static void btc8821a2ant_power_save_state(struct btc_coexist *btcoexist,
					  u8 ps_type, u8 lps_val, u8 rpwm_val)
{
	bool low_pwr_disable = false;

	switch (ps_type) {
	case BTC_PS_WIFI_NATIVE:
		/* recover to original 32k low power setting */
		low_pwr_disable = false;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_NORMAL_LPS, NULL);
		coex_sta->force_lps_on = false;
		break;
	case BTC_PS_LPS_ON:
		btc8821a2ant_ps_tdma_check_for_power_save_state(btcoexist,
								true);
		btc8821a2ant_lps_rpwm(btcoexist, NORMAL_EXEC, lps_val,
				      rpwm_val);
		/* when coex force to enter LPS, do not enter 32k low power */
		low_pwr_disable = true;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);
		/* power save must executed before psTdma */
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_ENTER_LPS, NULL);
		coex_sta->force_lps_on = true;
		break;
	case BTC_PS_LPS_OFF:
		btc8821a2ant_ps_tdma_check_for_power_save_state(btcoexist,
								false);
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
		coex_sta->force_lps_on = false;
		break;
	default:
		break;
	}
}

static void btc8821a2ant_coex_all_off(struct btc_coexist *btcoexist)
{
	/* fw all off */
	btc8821a2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
	btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
	btc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
	btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	/* sw all off */
	btc8821a2ant_sw_mechanism1(btcoexist, false, false, false, false);
	btc8821a2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);

	/* hw all off */
	btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
}

static void btc8821a2ant_coex_under_5g(struct btc_coexist *btcoexist)
{
	btc8821a2ant_coex_all_off(btcoexist);
	btc8821a2ant_ignore_wlan_act(btcoexist, NORMAL_EXEC, true);
}

static void btc8821a2ant_init_coex_dm(struct btc_coexist *btcoexist)
{
	/* force to reset coex mechanism */
	btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

	btc8821a2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
	btc8821a2ant_ps_tdma(btcoexist, FORCE_EXEC, false, 1);
	btc8821a2ant_fw_dac_swing_lvl(btcoexist, FORCE_EXEC, 6);
	btc8821a2ant_dec_bt_pwr(btcoexist, FORCE_EXEC, 0);

	btc8821a2ant_sw_mechanism1(btcoexist, false, false, false, false);
	btc8821a2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
}

static void btc8821a2ant_action_bt_inquiry(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 wifi_rssi_state, wifi_rssi_state1, bt_rssi_state;
	bool wifi_connected = false;
	bool low_pwr_disable = true;
	bool scan = false, link = false, roam = false;

	wifi_rssi_state =
		btc8821a2ant_wifi_rssi_state(btcoexist, 0, 2, 15, 0);
	wifi_rssi_state1 = btc8821a2ant_wifi_rssi_state(btcoexist, 1, 2,
				BT_8821A_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);
	bt_rssi_state = btc8821a2ant_bt_rssi_state(btcoexist,
		2, BT_8821A_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
			   &low_pwr_disable);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);

	btc8821a2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	if (scan || link || roam) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], Wifi link process + BT Inq/Page!!\n");
		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 15);
		btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 22);
	} else if (wifi_connected) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], Wifi connected + BT Inq/Page!!\n");
		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 15);
		btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 22);
	} else {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], Wifi no-link + BT Inq/Page!!\n");
		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
		btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
	}

	btc8821a2ant_fw_dac_swing_lvl(btcoexist, FORCE_EXEC, 6);
	btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	btc8821a2ant_sw_mechanism1(btcoexist, false, false, false, false);
	btc8821a2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
}

static void btc8821a2ant_action_wifi_link_process(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 u8tmpa, u8tmpb;

	btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 15);
	btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 22);

	btc8821a2ant_sw_mechanism1(btcoexist, false, false, false, false);
	btc8821a2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);

	u8tmpa = btcoexist->btc_read_1byte(btcoexist, 0x765);
	u8tmpb = btcoexist->btc_read_1byte(btcoexist, 0x76e);

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], 0x765=0x%x, 0x76e=0x%x\n", u8tmpa, u8tmpb);
}

static bool btc8821a2ant_action_wifi_idle_process(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 wifi_rssi_state, wifi_rssi_state1, bt_rssi_state;
	u8 ap_num = 0;

	wifi_rssi_state =
		btc8821a2ant_wifi_rssi_state(btcoexist, 0, 2, 15, 0);
	wifi_rssi_state1 = btc8821a2ant_wifi_rssi_state(btcoexist, 1, 2,
			BT_8821A_2ANT_WIFI_RSSI_COEXSWITCH_THRES - 20, 0);
	bt_rssi_state = btc8821a2ant_bt_rssi_state(btcoexist,
			2, BT_8821A_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM, &ap_num);

	/* define the office environment */
	if (BTC_RSSI_HIGH(wifi_rssi_state1) && (coex_sta->hid_exist) &&
	    (coex_sta->a2dp_exist)) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], Wifi  idle process for BT HID+A2DP exist!!\n");

		btc8821a2ant_dac_swing(btcoexist, NORMAL_EXEC, true, 0x6);
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

		/* sw all off */
		btc8821a2ant_sw_mechanism1(btcoexist, false, false, false,
					   false);
		btc8821a2ant_sw_mechanism2(btcoexist, false, false, false,
					   0x18);

		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
		btc8821a2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					      0x0, 0x0);
		btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);

		return true;
	} else if (coex_sta->pan_exist) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], Wifi  idle process for BT PAN exist!!\n");

		btc8821a2ant_dac_swing(btcoexist, NORMAL_EXEC, true, 0x6);
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

		/* sw all off */
		btc8821a2ant_sw_mechanism1(btcoexist, false, false, false,
					   false);
		btc8821a2ant_sw_mechanism2(btcoexist, false, false, false,
					   0x18);

		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
		btc8821a2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					      0x0, 0x0);
		btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);

		return true;
	}
	btc8821a2ant_dac_swing(btcoexist, NORMAL_EXEC, true, 0x18);
	return false;
}

static bool btc8821a2ant_is_common_action(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	bool common = false, wifi_connected = false, wifi_busy = false;
	bool low_pwr_disable = false;
	bool bt_hs_on = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	if (!wifi_connected) {
		low_pwr_disable = false;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);
		btc8821a2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false,
					0x8);

		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			    "[BTCoex], Wifi non-connected idle!!\n");

		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff,
					  0x0);
		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
		btc8821a2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					      0x0, 0x0);
		btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
		btc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

		btc8821a2ant_sw_mechanism1(btcoexist, false, false, false,
					   false);
		btc8821a2ant_sw_mechanism2(btcoexist, false, false, false,
					   0x18);

		common = true;
	} else {
		if (BT_8821A_2ANT_BT_STATUS_IDLE ==
		    coex_dm->bt_status) {
			low_pwr_disable = false;
			btcoexist->btc_set(btcoexist,
					   BTC_SET_ACT_DISABLE_LOW_POWER,
					   &low_pwr_disable);
			btc8821a2ant_limited_rx(btcoexist, NORMAL_EXEC,
						false, false, 0x8);

			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Wifi connected + BT non connected-idle!!\n");

			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1,
						  0xfffff, 0x0);
			btc8821a2ant_coex_table_with_type(btcoexist,
							  NORMAL_EXEC, 0);

			btc8821a2ant_power_save_state(
				btcoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
			btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
			btc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC,
						      0xb);
			btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

			btc8821a2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);

			common = true;
		} else if (BT_8821A_2ANT_BT_STATUS_CON_IDLE ==
			   coex_dm->bt_status) {
			low_pwr_disable = true;
			btcoexist->btc_set(btcoexist,
					   BTC_SET_ACT_DISABLE_LOW_POWER,
					   &low_pwr_disable);

			if (bt_hs_on)
				return false;
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Wifi connected + BT connected-idle!!\n");
			btc8821a2ant_limited_rx(btcoexist, NORMAL_EXEC,
						false, false, 0x8);

			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1,
						  0xfffff, 0x0);
			btc8821a2ant_coex_table_with_type(btcoexist,
							  NORMAL_EXEC, 0);

			btc8821a2ant_power_save_state(
				btcoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
			btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
			btc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC,
						      0xb);
			btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

			btc8821a2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
			common = true;
		} else {
			low_pwr_disable = true;
			btcoexist->btc_set(btcoexist,
					   BTC_SET_ACT_DISABLE_LOW_POWER,
					   &low_pwr_disable);

			if (wifi_busy) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], Wifi Connected-Busy + BT Busy!!\n");
				common = false;
			} else {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], Wifi Connected-Idle + BT Busy!!\n");
				common =
				    btc8821a2ant_action_wifi_idle_process(
					     btcoexist);
			}
		}
	}
	return common;
}

static void btc8821a2ant_tdma_duration_adjust(struct btc_coexist *btcoexist,
					      bool sco_hid, bool tx_pause,
					      u8 max_interval)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	static long up, dn, m, n, wait_count;
	 /* 0 : no change
	  * +1: increase WiFi duration
	  * -1: decrease WiFi duration
	  */
	int result;
	u8 retry_count = 0;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], TdmaDurationAdjust()\n");

	if (coex_dm->auto_tdma_adjust) {
		coex_dm->auto_tdma_adjust = false;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], first run TdmaDurationAdjust()!!\n");
		if (sco_hid) {
			if (tx_pause) {
				if (max_interval == 1) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 13);
					coex_dm->ps_tdma_du_adj_type = 13;
				} else if (max_interval == 2) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 14);
					coex_dm->ps_tdma_du_adj_type = 14;
				} else if (max_interval == 3) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				}
			} else {
				if (max_interval == 1) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 9);
					coex_dm->ps_tdma_du_adj_type = 9;
				} else if (max_interval == 2) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 10);
					coex_dm->ps_tdma_du_adj_type = 10;
				} else if (max_interval == 3) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				}
			}
		} else {
			if (tx_pause) {
				if (max_interval == 1) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 5);
					coex_dm->ps_tdma_du_adj_type = 5;
				} else if (max_interval == 2) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 6);
					coex_dm->ps_tdma_du_adj_type = 6;
				} else if (max_interval == 3) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				}
			} else {
				if (max_interval == 1) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 1);
					coex_dm->ps_tdma_du_adj_type = 1;
				} else if (max_interval == 2) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 2);
					coex_dm->ps_tdma_du_adj_type = 2;
				} else if (max_interval == 3) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
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
		/* accquire the BT TRx retry count from BT_Info byte2 */
		retry_count = coex_sta->bt_retry_cnt;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], retry_count = %d\n", retry_count);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], up = %d, dn = %d, m = %d, n = %d, wait_count = %d\n",
			    (int)up, (int)dn, (int)m, (int)n, (int)wait_count);
		result = 0;
		wait_count++;

		if (retry_count == 0) {
			/* no retry in the last 2-second duration */
			up++;
			dn--;

			if (dn <= 0)
				dn = 0;

			if (up >= n) {
				/* if (retry count == 0) for 2*n seconds,
				 * make WiFi duration wider
				 */
				wait_count = 0;
				n = 3;
				up = 0;
				dn = 0;
				result = 1;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], Increase wifi duration!!\n");
			}
		} else if (retry_count <= 3) {
			/* <=3 retry in the last 2-second duration */
			up--;
			dn++;

			if (up <= 0)
				up = 0;

			if (dn == 2) {
				/* if retry count < 3 for 2*2 seconds,
				 * shrink wifi duration
				 */
				if (wait_count <= 2)
					m++; /* avoid bounce in two levels */
				else
					m = 1;
				/* m max value is 20, max time is 120 second,
				 * recheck if adjust WiFi duration.
				 */
				if (m >= 20)
					m = 20;

				n = 3 * m;
				up = 0;
				dn = 0;
				wait_count = 0;
				result = -1;
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], Decrease wifi duration for retryCounter<3!!\n");
			}
		} else {
			/* retry count > 3, if retry count > 3 happens once,
			 * shrink WiFi duration
			 */
			if (wait_count == 1)
				m++; /* avoid bounce in two levels */
			else
				m = 1;
			/* m max value is 20, max time is 120 second,
			 * recheck if adjust WiFi duration.
			 */
			if (m >= 20)
				m = 20;

			n = 3 * m;
			up = 0;
			dn = 0;
			wait_count = 0;
			result = -1;
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Decrease wifi duration for retryCounter>3!!\n");
		}

		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], max Interval = %d\n", max_interval);

		if (max_interval == 1) {
			if (tx_pause) {
				if (coex_dm->cur_ps_tdma == 71) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 5);
					coex_dm->ps_tdma_du_adj_type = 5;
				} else if (coex_dm->cur_ps_tdma == 1) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 5);
					coex_dm->ps_tdma_du_adj_type = 5;
				} else if (coex_dm->cur_ps_tdma == 2) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 6);
					coex_dm->ps_tdma_du_adj_type = 6;
				} else if (coex_dm->cur_ps_tdma == 3) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 4) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 8);
					coex_dm->ps_tdma_du_adj_type = 8;
				}
				if (coex_dm->cur_ps_tdma == 9) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 13);
					coex_dm->ps_tdma_du_adj_type = 13;
				} else if (coex_dm->cur_ps_tdma == 10) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 14);
					coex_dm->ps_tdma_du_adj_type = 14;
				} else if (coex_dm->cur_ps_tdma == 11) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 12) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 16);
					coex_dm->ps_tdma_du_adj_type = 16;
				}

				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 5) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (coex_dm->cur_ps_tdma == 6) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 8);
						coex_dm->ps_tdma_du_adj_type =
							8;
					} else if (coex_dm->cur_ps_tdma == 13) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					} else if (coex_dm->cur_ps_tdma == 14) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 16);
						coex_dm->ps_tdma_du_adj_type =
							16;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 8) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (coex_dm->cur_ps_tdma == 6) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 5);
						coex_dm->ps_tdma_du_adj_type =
							5;
					} else if (coex_dm->cur_ps_tdma == 16) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					} else if (coex_dm->cur_ps_tdma == 14) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 13);
						coex_dm->ps_tdma_du_adj_type =
							13;
					}
				}
			} else {
				if (coex_dm->cur_ps_tdma == 5) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 71);
					coex_dm->ps_tdma_du_adj_type = 71;
				} else if (coex_dm->cur_ps_tdma == 6) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 2);
					coex_dm->ps_tdma_du_adj_type = 2;
				} else if (coex_dm->cur_ps_tdma == 7) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 8) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 4);
					coex_dm->ps_tdma_du_adj_type = 4;
				}
				if (coex_dm->cur_ps_tdma == 13) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 9);
					coex_dm->ps_tdma_du_adj_type = 9;
				} else if (coex_dm->cur_ps_tdma == 14) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 10);
					coex_dm->ps_tdma_du_adj_type = 10;
				} else if (coex_dm->cur_ps_tdma == 15) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 16) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 12);
					coex_dm->ps_tdma_du_adj_type = 12;
				}

				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 71) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 1);
						coex_dm->ps_tdma_du_adj_type =
							1;
					} else if (coex_dm->cur_ps_tdma == 1) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (coex_dm->cur_ps_tdma == 2) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 4);
						coex_dm->ps_tdma_du_adj_type =
							4;
					} else if (coex_dm->cur_ps_tdma == 9) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 10);
						coex_dm->ps_tdma_du_adj_type =
							10;
					} else if (coex_dm->cur_ps_tdma == 10) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 12);
						coex_dm->ps_tdma_du_adj_type =
							12;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 4) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (coex_dm->cur_ps_tdma == 2) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 1);
						coex_dm->ps_tdma_du_adj_type =
							1;
					} else if (coex_dm->cur_ps_tdma == 1) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 71);
						coex_dm->ps_tdma_du_adj_type =
							71;
					} else if (coex_dm->cur_ps_tdma == 12) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 10);
						coex_dm->ps_tdma_du_adj_type =
							10;
					} else if (coex_dm->cur_ps_tdma == 10) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 9);
						coex_dm->ps_tdma_du_adj_type =
							9;
					}
				}
			}
		} else if (max_interval == 2) {
			if (tx_pause) {
				if (coex_dm->cur_ps_tdma == 1) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 6);
					coex_dm->ps_tdma_du_adj_type = 6;
				} else if (coex_dm->cur_ps_tdma == 2) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 6);
					coex_dm->ps_tdma_du_adj_type = 6;
				} else if (coex_dm->cur_ps_tdma == 3) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 4) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 8);
					coex_dm->ps_tdma_du_adj_type = 8;
				}
				if (coex_dm->cur_ps_tdma == 9) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 14);
					coex_dm->ps_tdma_du_adj_type = 14;
				} else if (coex_dm->cur_ps_tdma == 10) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 14);
					coex_dm->ps_tdma_du_adj_type = 14;
				} else if (coex_dm->cur_ps_tdma == 11) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 12) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 16);
					coex_dm->ps_tdma_du_adj_type = 16;
				}
				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 5) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (coex_dm->cur_ps_tdma == 6) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 8);
						coex_dm->ps_tdma_du_adj_type =
							8;
					} else if (coex_dm->cur_ps_tdma == 13) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					} else if (coex_dm->cur_ps_tdma == 14) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 16);
						coex_dm->ps_tdma_du_adj_type =
							16;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 8) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (coex_dm->cur_ps_tdma == 6) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (coex_dm->cur_ps_tdma == 16) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					} else if (coex_dm->cur_ps_tdma == 14) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					}
				}
			} else {
				if (coex_dm->cur_ps_tdma == 5) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 2);
					coex_dm->ps_tdma_du_adj_type = 2;
				} else if (coex_dm->cur_ps_tdma == 6) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 2);
					coex_dm->ps_tdma_du_adj_type = 2;
				} else if (coex_dm->cur_ps_tdma == 7) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 8) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 4);
					coex_dm->ps_tdma_du_adj_type = 4;
				}
				if (coex_dm->cur_ps_tdma == 13) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 10);
					coex_dm->ps_tdma_du_adj_type = 10;
				} else if (coex_dm->cur_ps_tdma == 14) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 10);
					coex_dm->ps_tdma_du_adj_type = 10;
				} else if (coex_dm->cur_ps_tdma == 15) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 16) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 12);
					coex_dm->ps_tdma_du_adj_type = 12;
				}
				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 1) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (coex_dm->cur_ps_tdma == 2) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 4);
						coex_dm->ps_tdma_du_adj_type =
							4;
					} else if (coex_dm->cur_ps_tdma == 9) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 10);
						coex_dm->ps_tdma_du_adj_type =
							10;
					} else if (coex_dm->cur_ps_tdma == 10) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 12);
						coex_dm->ps_tdma_du_adj_type =
							12;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 4) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (coex_dm->cur_ps_tdma == 2) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (coex_dm->cur_ps_tdma == 12) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 10);
						coex_dm->ps_tdma_du_adj_type =
							10;
					} else if (coex_dm->cur_ps_tdma == 10) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 10);
						coex_dm->ps_tdma_du_adj_type =
							10;
					}
				}
			}
		} else if (max_interval == 3) {
			if (tx_pause) {
				if (coex_dm->cur_ps_tdma == 1) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 2) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 3) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 4) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 8);
					coex_dm->ps_tdma_du_adj_type = 8;
				}
				if (coex_dm->cur_ps_tdma == 9) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 10) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 11) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 12) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 16);
					coex_dm->ps_tdma_du_adj_type = 16;
				}
				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 5) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 6) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 8);
						coex_dm->ps_tdma_du_adj_type =
							8;
					} else if (coex_dm->cur_ps_tdma == 13) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 14) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 16);
						coex_dm->ps_tdma_du_adj_type =
							16;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 8) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 6) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 16) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 14) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					}
				}
			} else {
				if (coex_dm->cur_ps_tdma == 5) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 6) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 7) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 8) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 4);
					coex_dm->ps_tdma_du_adj_type = 4;
				}
				if (coex_dm->cur_ps_tdma == 13) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 14) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 15) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 16) {
					btc8821a2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 12);
					coex_dm->ps_tdma_du_adj_type = 12;
				}
				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 1) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 2) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 4);
						coex_dm->ps_tdma_du_adj_type =
							4;
					} else if (coex_dm->cur_ps_tdma == 9) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 10) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 12);
						coex_dm->ps_tdma_du_adj_type =
							12;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 4) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 2) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 12) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 10) {
						btc8821a2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					}
				}
			}
		}
	}

	/* if current PsTdma not match with the recorded one
	 * (when scan, dhcp...), then we have to adjust it back to
	 * the previous recorded one.
	 */
	if (coex_dm->cur_ps_tdma != coex_dm->ps_tdma_du_adj_type) {
		bool scan = false, link = false, roam = false;

		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], PsTdma type mismatch!!!, cur_ps_tdma = %d, recordPsTdma = %d\n",
			 coex_dm->cur_ps_tdma, coex_dm->ps_tdma_du_adj_type);

		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);

		if (!scan && !link && !roam) {
			btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
					     coex_dm->ps_tdma_du_adj_type);
		} else {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], roaming/link/scan is under progress, will adjust next time!!!\n");
		}
	}
}

/* SCO only or SCO+PAN(HS)*/
static void btc8821a2ant_action_sco(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = btc8821a2ant_wifi_rssi_state(btcoexist, 0, 2, 15, 0);
	bt_rssi_state = btc8821a2ant_bt_rssi_state(btcoexist, 2, 35, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	btc8821a2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);
	btc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 4);

	if (BTC_RSSI_HIGH(bt_rssi_state))
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (wifi_bw == BTC_WIFI_BW_LEGACY) {
		/* for SCO quality at 11b/g mode */
		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
	} else {
		/* for SCO quality & wifi performance balance at 11n mode */
		if (wifi_bw == BTC_WIFI_BW_HT40) {
			btc8821a2ant_coex_table_with_type(btcoexist,
							  NORMAL_EXEC, 8);
		} else {
			if (bt_link_info->sco_only)
				btc8821a2ant_coex_table_with_type(
					btcoexist, NORMAL_EXEC, 17);
			else
				btc8821a2ant_coex_table_with_type(
					btcoexist, NORMAL_EXEC, 12);
		}
	}

	btc8821a2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
	/* for voice quality */
	btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);

	/* sw mechanism */
	if (wifi_bw == BTC_WIFI_BW_HT40) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_sw_mechanism1(btcoexist, true, true,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   true, 0x18);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, true, true,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   true, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_sw_mechanism1(btcoexist, false, true,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   true, 0x18);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, false, true,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   true, 0x18);
		}
	}
}

static void btc8821a2ant_action_hid(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = btc8821a2ant_wifi_rssi_state(btcoexist, 0, 2, 15, 0);
	bt_rssi_state = btc8821a2ant_bt_rssi_state(btcoexist,
		2, BT_8821A_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	btc8821a2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);
	btc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(bt_rssi_state))
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (wifi_bw == BTC_WIFI_BW_LEGACY) {
		/* for HID at 11b/g mode */
		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
	} else {
		/* for HID quality & wifi performance balance at 11n mode */
		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
	}

	btc8821a2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
	btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 24);

	if (wifi_bw == BTC_WIFI_BW_HT40) {
		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_sw_mechanism1(btcoexist, true, true,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, true, true,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	} else {
		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_sw_mechanism1(btcoexist, false, true,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, false, true,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	}
}

/* A2DP only / PAN(EDR) only/ A2DP+PAN(HS) */
static void btc8821a2ant_action_a2dp(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, wifi_rssi_state1, bt_rssi_state;
	u8 ap_num = 0;
	u32 wifi_bw;

	wifi_rssi_state = btc8821a2ant_wifi_rssi_state(btcoexist, 0, 2, 15, 0);
	wifi_rssi_state1 = btc8821a2ant_wifi_rssi_state(btcoexist, 1, 2,
				BT_8821A_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);
	bt_rssi_state = btc8821a2ant_bt_rssi_state(btcoexist,
		2, BT_8821A_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	if ((ap_num >= 10) && BTC_RSSI_HIGH(wifi_rssi_state1) &&
	    BTC_RSSI_HIGH(bt_rssi_state)) {
		btc8821a2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					      0x0, 0x0);

		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff,
					  0x0);
		btc8821a2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false,
					0x8);
		btc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);

		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

		btc8821a2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					      0x0, 0x0);
		btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 23);

		/* sw mechanism */
		btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
		if (wifi_bw == BTC_WIFI_BW_HT40) {
			btc8821a2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   true, 0x6);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   true, 0x6);
		}
		return;
	}

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);
	btc8821a2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	btc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(bt_rssi_state))
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
	else
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	if (BTC_RSSI_HIGH(wifi_rssi_state1) && BTC_RSSI_HIGH(bt_rssi_state)) {
		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
		btc8821a2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					      0x0, 0x0);
	} else {
		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 13);
		btc8821a2ant_power_save_state(btcoexist, BTC_PS_LPS_ON, 0x50,
					      0x4);
	}

	if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
	    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
		btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 23);
	} else {
		btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 23);
	}

	/* sw mechanism */
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	if (wifi_bw == BTC_WIFI_BW_HT40) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	}
}

static void btc8821a2ant_action_a2dp_pan_hs(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, wifi_rssi_state1, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = btc8821a2ant_wifi_rssi_state(btcoexist, 0, 2, 15, 0);
	wifi_rssi_state1 = btc8821a2ant_wifi_rssi_state(btcoexist, 1, 2,
				BT_8821A_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);
	bt_rssi_state = btc8821a2ant_bt_rssi_state(btcoexist,
		2, BT_8821A_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	btc8821a2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);
	btc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(bt_rssi_state))
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
	else
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	if (BTC_RSSI_HIGH(wifi_rssi_state1) && BTC_RSSI_HIGH(bt_rssi_state)) {
		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
		btc8821a2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					      0x0, 0x0);
	} else {
		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 13);
		btc8821a2ant_power_save_state(btcoexist, BTC_PS_LPS_ON, 0x50,
					      0x4);
	}

	btc8821a2ant_tdma_duration_adjust(btcoexist, false, true, 2);

	/* sw mechanism */
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	if (wifi_bw == BTC_WIFI_BW_HT40) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	}
}

static void btc8821a2ant_action_pan_edr(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, wifi_rssi_state1, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = btc8821a2ant_wifi_rssi_state(btcoexist, 0, 2, 15, 0);
	wifi_rssi_state1 = btc8821a2ant_wifi_rssi_state(btcoexist, 1, 2,
				BT_8821A_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);
	bt_rssi_state = btc8821a2ant_bt_rssi_state(btcoexist,
				2, BT_8821A_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	btc8821a2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	btc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(bt_rssi_state))
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	if (BTC_RSSI_HIGH(wifi_rssi_state1) && BTC_RSSI_HIGH(bt_rssi_state)) {
		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 10);
		btc8821a2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					      0x0, 0x0);
	} else {
		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 13);
		btc8821a2ant_power_save_state(btcoexist, BTC_PS_LPS_ON, 0x50,
					      0x4);
	}

	if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
	    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
		btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 26);
	else
		btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 26);

	/* sw mechanism */
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	if (wifi_bw == BTC_WIFI_BW_HT40) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	}
}

/* PAN(HS) only */
static void btc8821a2ant_action_pan_hs(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, wifi_rssi_state1, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = btc8821a2ant_wifi_rssi_state(btcoexist, 0, 2, 15, 0);
	wifi_rssi_state1 = btc8821a2ant_wifi_rssi_state(btcoexist, 1, 2,
				BT_8821A_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);
	bt_rssi_state = btc8821a2ant_bt_rssi_state(btcoexist,
				2, BT_8821A_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	btc8821a2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);
	btc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(bt_rssi_state))
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
	else
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
	btc8821a2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
	btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	if (wifi_bw == BTC_WIFI_BW_HT40) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	}
}

/* PAN(EDR)+A2DP */
static void btc8821a2ant_action_pan_edr_a2dp(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, wifi_rssi_state1, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = btc8821a2ant_wifi_rssi_state(btcoexist, 0, 2, 15, 0);
	wifi_rssi_state1 = btc8821a2ant_wifi_rssi_state(btcoexist, 1, 2,
				BT_8821A_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);
	bt_rssi_state = btc8821a2ant_bt_rssi_state(btcoexist,
				2, BT_8821A_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	btc8821a2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	btc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(bt_rssi_state))
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
	else
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	if (BTC_RSSI_HIGH(wifi_rssi_state1) && BTC_RSSI_HIGH(bt_rssi_state))
		btc8821a2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					      0x0, 0x0);
	else
		btc8821a2ant_power_save_state(btcoexist, BTC_PS_LPS_ON, 0x50,
					      0x4);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
	    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 12);

		if (wifi_bw == BTC_WIFI_BW_HT40)
			btc8821a2ant_tdma_duration_adjust(btcoexist, false,
							  true, 3);
		else
			btc8821a2ant_tdma_duration_adjust(btcoexist, false,
							  false, 3);
	} else {
		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 13);
		btc8821a2ant_tdma_duration_adjust(btcoexist, false, true, 3);
	}

	/* sw mechanism	 */
	if (wifi_bw == BTC_WIFI_BW_HT40) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, true, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, false, false,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	}
}

static void btc8821a2ant_action_pan_edr_hid(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = btc8821a2ant_wifi_rssi_state(btcoexist, 0, 2, 15, 0);
	bt_rssi_state = btc8821a2ant_bt_rssi_state(btcoexist,
				2, BT_8821A_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	btc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(bt_rssi_state))
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (wifi_bw == BTC_WIFI_BW_LEGACY) {
		/* for HID at 11b/g mode */
		btc8821a2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55ff55ff,
					0x5a5f5a5f, 0xffff, 0x3);
	} else {
		/* for HID quality & wifi performance balance at 11n mode */
		btc8821a2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55ff55ff,
					0x5a5f5a5f, 0xffff, 0x3);
	}

	if (wifi_bw == BTC_WIFI_BW_HT40) {
		btc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 3);
		/* fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC,
					     true, 10);
		} else {
			btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 14);
		}

		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_sw_mechanism1(btcoexist, true, true,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, true, true,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	} else {
		btc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		/* fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 10);
		} else {
			btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 14);
		}

		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_sw_mechanism1(btcoexist, false, true,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, false, true,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	}
}

/* HID+A2DP+PAN(EDR) */
static void btc8821a2ant_act_hid_a2dp_pan_edr(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, wifi_rssi_state1, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = btc8821a2ant_wifi_rssi_state(btcoexist, 0, 2, 15, 0);
	wifi_rssi_state1 = btc8821a2ant_wifi_rssi_state(btcoexist, 1, 2,
				BT_8821A_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);
	bt_rssi_state = btc8821a2ant_bt_rssi_state(btcoexist,
				2, BT_8821A_2ANT_BT_RSSI_COEXSWITCH_THRES, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	btc8821a2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);
	btc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(bt_rssi_state))
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
	else
		btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	if (BTC_RSSI_HIGH(wifi_rssi_state1) && BTC_RSSI_HIGH(bt_rssi_state)) {
		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
		btc8821a2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					      0x0, 0x0);
	} else {
		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 14);
		btc8821a2ant_power_save_state(btcoexist, BTC_PS_LPS_ON, 0x50,
					      0x4);
	}

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
	    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
		if (wifi_bw == BTC_WIFI_BW_HT40)
			btc8821a2ant_tdma_duration_adjust(btcoexist, true,
							  true, 3);
		else
			btc8821a2ant_tdma_duration_adjust(btcoexist, true,
							  false, 3);
	} else {
		btc8821a2ant_tdma_duration_adjust(btcoexist, true, true, 3);
	}

	/* sw mechanism */
	if (wifi_bw == BTC_WIFI_BW_HT40) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_sw_mechanism1(btcoexist, true, true,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, true, true,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_sw_mechanism1(btcoexist, false, true,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, false, true,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	}
}

static void btc8821a2ant_action_hid_a2dp(struct btc_coexist *btcoexist)
{
	u32 wifi_bw;
	u8 wifi_rssi_state, wifi_rssi_state1, bt_rssi_state;
	u8 ap_num = 0;

	wifi_rssi_state = btc8821a2ant_wifi_rssi_state(btcoexist, 0, 2, 15, 0);
	wifi_rssi_state1 = btc8821a2ant_wifi_rssi_state(btcoexist, 1, 2,
				BT_8821A_2ANT_WIFI_RSSI_COEXSWITCH_THRES, 0);
	bt_rssi_state = btc8821a2ant_bt_rssi_state(btcoexist,
				3, BT_8821A_2ANT_BT_RSSI_COEXSWITCH_THRES, 37);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	btc8821a2ant_limited_rx(btcoexist, NORMAL_EXEC, false, true, 0x5);
	btc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	if (wifi_bw == BTC_WIFI_BW_LEGACY) {
		if (BTC_RSSI_HIGH(bt_rssi_state))
			btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
		else if (BTC_RSSI_MEDIUM(bt_rssi_state))
			btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
		else
			btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);
	} else {
		/* only 802.11N mode we have to dec bt power to 4 degree */
		if (BTC_RSSI_HIGH(bt_rssi_state)) {
			btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM,
					   &ap_num);
			if (ap_num < 10)
				btc8821a2ant_dec_bt_pwr(btcoexist,
							NORMAL_EXEC, 4);
			else
				btc8821a2ant_dec_bt_pwr(btcoexist,
							NORMAL_EXEC, 2);
		} else if (BTC_RSSI_MEDIUM(bt_rssi_state)) {
			btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
		} else {
			btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);
		}
	}

	if (wifi_bw == BTC_WIFI_BW_LEGACY) {
		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
		btc8821a2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					      0x0, 0x0);
	} else {
		btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 14);
		btc8821a2ant_power_save_state(btcoexist, BTC_PS_LPS_ON, 0x50,
					      0x4);
	}

	if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
	    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
		btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 23);
	} else {
		btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 23);
	}

	/* sw mechanism */
	if (wifi_bw == BTC_WIFI_BW_HT40) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_sw_mechanism1(btcoexist, true, true,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, true, true,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a2ant_sw_mechanism1(btcoexist, false, true,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, true, false,
						   false, 0x18);
		} else {
			btc8821a2ant_sw_mechanism1(btcoexist, false, true,
						   false, false);
			btc8821a2ant_sw_mechanism2(btcoexist, false, false,
						   false, 0x18);
		}
	}
}

static void btc8821a2ant_action_wifi_multi_port(struct btc_coexist *btcoexist)
{
	btc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
	btc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	/* sw all off */
	btc8821a2ant_sw_mechanism1(btcoexist, false, false, false, false);
	btc8821a2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);

	/* hw all off */
	btc8821a2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

	btc8821a2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
	btc8821a2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
}

static void btc8821a2ant_run_coexist_mechanism(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool wifi_under_5g = false;
	u8 algorithm = 0;
	u32 num_of_wifi_link = 0;
	u32 wifi_link_status = 0;
	bool miracast_plus_bt = false;
	bool scan = false, link = false, roam = false;

	if (btcoexist->manual_control) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], Manual control!!!\n");
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);

	if (wifi_under_5g) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], RunCoexistMechanism(), run 5G coex setting!!<===\n");
		btc8821a2ant_coex_under_5g(btcoexist);
		return;
	}

	if (coex_sta->under_ips) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], wifi is under IPS !!!\n");
		return;
	}

	algorithm = btc8821a2ant_action_algorithm(btcoexist);
	if (coex_sta->c2h_bt_inquiry_page &&
	    (BT_8821A_2ANT_COEX_ALGO_PANHS != algorithm)) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], BT is under inquiry/page scan !!\n");
		btc8821a2ant_action_bt_inquiry(btcoexist);
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);

	if (scan || link || roam) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], WiFi is under Link Process !!\n");
		btc8821a2ant_action_wifi_link_process(btcoexist);
		return;
	}

	/* for P2P */
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
			   &wifi_link_status);
	num_of_wifi_link = wifi_link_status >> 16;

	if ((num_of_wifi_link >= 2) ||
	    (wifi_link_status & WIFI_P2P_GO_CONNECTED)) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "############# [BTCoex],  Multi-Port num_of_wifi_link = %d, wifi_link_status = 0x%x\n",
			 num_of_wifi_link, wifi_link_status);

		if (bt_link_info->bt_link_exist)
			miracast_plus_bt = true;
		else
			miracast_plus_bt = false;

		btcoexist->btc_set(btcoexist, BTC_SET_BL_MIRACAST_PLUS_BT,
				   &miracast_plus_bt);
		btc8821a2ant_action_wifi_multi_port(btcoexist);

		return;
	}

	miracast_plus_bt = false;
	btcoexist->btc_set(btcoexist, BTC_SET_BL_MIRACAST_PLUS_BT,
			   &miracast_plus_bt);

	coex_dm->cur_algorithm = algorithm;
	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], Algorithm = %d\n", coex_dm->cur_algorithm);

	if (btc8821a2ant_is_common_action(btcoexist)) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], Action 2-Ant common\n");
		coex_dm->auto_tdma_adjust = true;
	} else {
		if (coex_dm->cur_algorithm != coex_dm->pre_algorithm) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], pre_algorithm = %d, cur_algorithm = %d\n",
				    coex_dm->pre_algorithm,
				    coex_dm->cur_algorithm);
			coex_dm->auto_tdma_adjust = false;
		}
		switch (coex_dm->cur_algorithm) {
		case BT_8821A_2ANT_COEX_ALGO_SCO:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action 2-Ant, algorithm = SCO\n");
			btc8821a2ant_action_sco(btcoexist);
			break;
		case BT_8821A_2ANT_COEX_ALGO_HID:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action 2-Ant, algorithm = HID\n");
			btc8821a2ant_action_hid(btcoexist);
			break;
		case BT_8821A_2ANT_COEX_ALGO_A2DP:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action 2-Ant, algorithm = A2DP\n");
			btc8821a2ant_action_a2dp(btcoexist);
			break;
		case BT_8821A_2ANT_COEX_ALGO_A2DP_PANHS:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action 2-Ant, algorithm = A2DP+PAN(HS)\n");
			btc8821a2ant_action_a2dp_pan_hs(btcoexist);
			break;
		case BT_8821A_2ANT_COEX_ALGO_PANEDR:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action 2-Ant, algorithm = PAN(EDR)\n");
			btc8821a2ant_action_pan_edr(btcoexist);
			break;
		case BT_8821A_2ANT_COEX_ALGO_PANHS:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action 2-Ant, algorithm = HS mode\n");
			btc8821a2ant_action_pan_hs(btcoexist);
			break;
		case BT_8821A_2ANT_COEX_ALGO_PANEDR_A2DP:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action 2-Ant, algorithm = PAN+A2DP\n");
			btc8821a2ant_action_pan_edr_a2dp(btcoexist);
			break;
		case BT_8821A_2ANT_COEX_ALGO_PANEDR_HID:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action 2-Ant, algorithm = PAN(EDR)+HID\n");
			btc8821a2ant_action_pan_edr_hid(btcoexist);
			break;
		case BT_8821A_2ANT_COEX_ALGO_HID_A2DP_PANEDR:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action 2-Ant, algorithm = HID+A2DP+PAN\n");
			btc8821a2ant_act_hid_a2dp_pan_edr(btcoexist);
			break;
		case BT_8821A_2ANT_COEX_ALGO_HID_A2DP:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action 2-Ant, algorithm = HID+A2DP\n");
			btc8821a2ant_action_hid_a2dp(btcoexist);
			break;
		default:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action 2-Ant, algorithm = coexist All Off!!\n");
			btc8821a2ant_coex_all_off(btcoexist);
			break;
		}
		coex_dm->pre_algorithm = coex_dm->cur_algorithm;
	}
}

static void btc8821a2ant_wifi_off_hw_cfg(struct btc_coexist *btcoexist)
{
	u8 h2c_parameter[2] = {0};
	u32 fw_ver = 0;

	/* set wlan_act to low */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0x4);

	/* WiFi goto standby while GNT_BT 0-->1 */
	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x780);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
	if (fw_ver >= 0x180000) {
		/* Use H2C to set GNT_BT to HIGH */
		h2c_parameter[0] = 1;
		btcoexist->btc_fill_h2c(btcoexist, 0x6E, 1, h2c_parameter);
	} else {
		btcoexist->btc_write_1byte(btcoexist, 0x765, 0x18);
	}
}

/**************************************************************
 * extern function start with ex_btc8821a2ant_
 **************************************************************/
void ex_btc8821a2ant_init_hwconfig(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 u1tmp = 0;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], 2Ant Init HW Config!!\n");

	/* backup rf 0x1e value */
	coex_dm->bt_rf0x1e_backup =
		btcoexist->btc_get_rf_reg(btcoexist, BTC_RF_A, 0x1e, 0xfffff);

	/* 0x790[5:0] = 0x5 */
	u1tmp = btcoexist->btc_read_1byte(btcoexist, 0x790);
	u1tmp &= 0xc0;
	u1tmp |= 0x5;
	btcoexist->btc_write_1byte(btcoexist, 0x790, u1tmp);

	/* Antenna config */
	btc8821a2ant_set_ant_path(btcoexist, BTC_ANT_WIFI_AT_MAIN, true, false);
	coex_sta->dis_ver_info_cnt = 0;

	/* PTA parameter */
	btc8821a2ant_coex_table_with_type(btcoexist, FORCE_EXEC, 0);

	/* Enable counter statistics */
	/* 0x76e[3] = 1, WLAN_Act control by PTA */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0x4);
	btcoexist->btc_write_1byte(btcoexist, 0x778, 0x3);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x40, 0x20, 0x1);
}

void ex_btc8821a2ant_pre_load_firmware(struct btc_coexist *btcoexist)
{
	struct btc_board_info *board_info = &btcoexist->board_info;
	u8 u8tmp = 0x4; /* Set BIT2 by default since it's 2ant case */

	/**
	 * S0 or S1 setting and Local register setting(By the setting fw can get
	 * ant number, S0/S1, ... info)
	 *
	 * Local setting bit define
	 *	BIT0: "0" for no antenna inverse; "1" for antenna inverse
	 *	BIT1: "0" for internal switch; "1" for external switch
	 *	BIT2: "0" for one antenna; "1" for two antenna
	 * NOTE: here default all internal switch and 1-antenna ==> BIT1=0 and
	 * BIT2=0
	 */
	if (btcoexist->chip_interface == BTC_INTF_USB) {
		/* fixed at S0 for USB interface */
		u8tmp |= 0x1; /* antenna inverse */
		btcoexist->btc_write_local_reg_1byte(btcoexist, 0xfe08, u8tmp);
	} else {
		/* for PCIE and SDIO interface, we check efuse 0xc3[6] */
		if (board_info->single_ant_path == 0) {
		} else if (board_info->single_ant_path == 1) {
			/* set to S0 */
			u8tmp |= 0x1; /* antenna inverse */
		}

		if (btcoexist->chip_interface == BTC_INTF_PCI)
			btcoexist->btc_write_local_reg_1byte(btcoexist, 0x384,
							     u8tmp);
		else if (btcoexist->chip_interface == BTC_INTF_SDIO)
			btcoexist->btc_write_local_reg_1byte(btcoexist, 0x60,
							     u8tmp);
	}
}

void ex_btc8821a2ant_init_coex_dm(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], Coex Mechanism Init!!\n");

	btc8821a2ant_init_coex_dm(btcoexist);
}

void ex_btc8821a2ant_display_coex_info(struct btc_coexist *btcoexist,
				       struct seq_file *m)
{
	struct btc_board_info *board_info = &btcoexist->board_info;
	struct btc_stack_info *stack_info = &btcoexist->stack_info;
	u8 u1tmp[4], i, bt_info_ext, ps_tdma_case = 0;
	u32 u4tmp[4];
	bool roam = false, scan = false, link = false, wifi_under_5g = false;
	bool bt_hs_on = false, wifi_busy = false;
	long wifi_rssi = 0, bt_hs_rssi = 0;
	u32 wifi_bw, wifi_traffic_dir;
	u8 wifi_dot_11_chnl, wifi_hs_chnl;
	u32 fw_ver = 0, bt_patch_ver = 0;

	seq_puts(m, "\n ============[BT Coexist info]============");

	seq_printf(m, "\n %-35s = %d/ %d ", "Ant PG number/ Ant mechanism:",
		   board_info->pg_ant_num, board_info->btdm_ant_num);

	if (btcoexist->manual_control) {
		seq_printf(m, "\n %-35s", "[Action Manual control]!!");
	}

	seq_printf(m, "\n %-35s = %s / %d", "BT stack/ hci ext ver",
		   ((stack_info->profile_notified) ? "Yes" : "No"),
		   stack_info->hci_version);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER, &bt_patch_ver);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
	seq_printf(m, "\n %-35s = %d_%d/ 0x%x/ 0x%x(%d)",
		   "CoexVer/ FwVer/ PatchVer",
		   glcoex_ver_date_8821a_2ant, glcoex_ver_8821a_2ant,
		   fw_ver, bt_patch_ver, bt_patch_ver);

	btcoexist->btc_get(btcoexist,
		BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist,
		BTC_GET_U1_WIFI_DOT11_CHNL, &wifi_dot_11_chnl);
	btcoexist->btc_get(btcoexist,
		BTC_GET_U1_WIFI_HS_CHNL, &wifi_hs_chnl);
	seq_printf(m, "\n %-35s = %d / %d(%d)",
		   "Dot11 channel / HsMode(HsChnl)",
		   wifi_dot_11_chnl, bt_hs_on, wifi_hs_chnl);

	seq_printf(m, "\n %-35s = %3ph ",
		   "H2C Wifi inform bt chnl Info",
		   coex_dm->wifi_chnl_info);

	btcoexist->btc_get(btcoexist, BTC_GET_S4_WIFI_RSSI, &wifi_rssi);
	btcoexist->btc_get(btcoexist, BTC_GET_S4_HS_RSSI, &bt_hs_rssi);
	seq_printf(m, "\n %-35s = %ld/ %ld", "Wifi rssi/ HS rssi",
		   wifi_rssi, bt_hs_rssi);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);
	seq_printf(m, "\n %-35s = %d/ %d/ %d ", "Wifi link/ roam/ scan",
		   link, roam, scan);

	btcoexist->btc_get(btcoexist,
		BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);
	btcoexist->btc_get(btcoexist,
		BTC_GET_U4_WIFI_BW, &wifi_bw);
	btcoexist->btc_get(btcoexist,
		BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist,
		BTC_GET_U4_WIFI_TRAFFIC_DIRECTION, &wifi_traffic_dir);
	seq_printf(m, "\n %-35s = %s / %s/ %s ", "Wifi status",
		   (wifi_under_5g ? "5G" : "2.4G"),
		   ((BTC_WIFI_BW_LEGACY == wifi_bw) ? "Legacy" :
		    (((BTC_WIFI_BW_HT40 == wifi_bw) ? "HT40" : "HT20"))),
		   ((!wifi_busy) ? "idle" :
		    ((BTC_WIFI_TRAFFIC_TX == wifi_traffic_dir) ?
		     "uplink" : "downlink")));

	if (stack_info->profile_notified) {
		seq_printf(m, "\n %-35s = %d / %d / %d / %d",
			   "SCO/HID/PAN/A2DP",
			   stack_info->sco_exist, stack_info->hid_exist,
			   stack_info->pan_exist, stack_info->a2dp_exist);

		btcoexist->btc_disp_dbg_msg(btcoexist,
					    BTC_DBG_DISP_BT_LINK_INFO,
					    m);
	}

	bt_info_ext = coex_sta->bt_info_ext;
	seq_printf(m, "\n %-35s = %s", "BT Info A2DP rate",
		   (bt_info_ext&BIT0) ? "Basic rate" : "EDR rate");

	for (i = 0; i < BT_INFO_SRC_8821A_2ANT_MAX; i++) {
		if (coex_sta->bt_info_c2h_cnt[i]) {
			seq_printf(m, "\n %-35s = %7ph(%d)",
				   glbt_info_src_8821a_2ant[i],
				   coex_sta->bt_info_c2h[i],
				   coex_sta->bt_info_c2h_cnt[i]);
		}
	}

	seq_printf(m, "\n %-35s = %s/%s",
		   "PS state, IPS/LPS",
		   ((coex_sta->under_ips ? "IPS ON" : "IPS OFF")),
		   ((coex_sta->under_lps ? "LPS ON" : "LPS OFF")));
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_FW_PWR_MODE_CMD, m);

	/* Sw mechanism*/
	seq_printf(m, "\n %-35s",
		   "============[Sw mechanism]============");
	seq_printf(m, "\n %-35s = %d/ %d/ %d(0x%x) ",
		   "SM2[AgcT/ AdcB/ SwDacSwing(lvl)]",
		   coex_dm->cur_agc_table_en, coex_dm->cur_adc_back_off,
		   coex_dm->cur_dac_swing_on, coex_dm->cur_dac_swing_lvl);

	/* Fw mechanism*/
	seq_printf(m, "\n %-35s",
		   "============[Fw mechanism]============");

	if (!btcoexist->manual_control) {
		ps_tdma_case = coex_dm->cur_ps_tdma;
		seq_printf(m, "\n %-35s = %5ph case-%d",
			   "PS TDMA",
			   coex_dm->ps_tdma_para, ps_tdma_case);

		seq_printf(m, "\n %-35s = %d/ %d ", "DecBtPwr/ IgnWlanAct",
			   coex_dm->cur_dec_bt_pwr_lvl,
			   coex_dm->cur_ignore_wlan_act);
	}

	/* Hw setting*/
	seq_printf(m, "\n %-35s", "============[Hw setting]============");

	seq_printf(m, "\n %-35s = 0x%x", "RF-A, 0x1e initVal",
		   coex_dm->bt_rf0x1e_backup);

	u1tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x778);
	u1tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x6cc);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x ",
		   "0x778 (W_Act)/ 0x6cc (CoTab Sel)",
		   u1tmp[0], u1tmp[1]);

	u1tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x8db);
	u1tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0xc5b);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x",
		   "0x8db(ADC)/0xc5b[29:25](DAC)",
		   ((u1tmp[0] & 0x60) >> 5), ((u1tmp[1] & 0x3e) >> 1));

	u4tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xcb4);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x",
		   "0xcb4[7:0](ctrl)/ 0xcb4[29:28](val)",
		   u4tmp[0] & 0xff, ((u4tmp[0] & 0x30000000) >> 28));

	u1tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x40);
	u4tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x4c);
	u4tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x974);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x40/ 0x4c[24:23]/ 0x974",
		   u1tmp[0], ((u4tmp[0] & 0x01800000) >> 23), u4tmp[1]);

	u4tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x550);
	u1tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x522);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x",
		   "0x550(bcn ctrl)/0x522",
		   u4tmp[0], u1tmp[0]);

	u4tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xc50);
	u1tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0xa0a);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x",
		   "0xc50(DIG)/0xa0a(CCK-TH)",
		   u4tmp[0], u1tmp[0]);

	u4tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xf48);
	u1tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0xa5b);
	u1tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0xa5c);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x",
		   "OFDM-FA/ CCK-FA",
		   u4tmp[0], (u1tmp[0] << 8) + u1tmp[1]);

	u4tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x6c0);
	u4tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x6c4);
	u4tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0x6c8);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x6c0/0x6c4/0x6c8",
		   u4tmp[0], u4tmp[1], u4tmp[2]);

	seq_printf(m, "\n %-35s = %d/ %d",
		   "0x770 (hi-pri Rx/Tx)",
		   coex_sta->high_priority_rx, coex_sta->high_priority_tx);
	seq_printf(m, "\n %-35s = %d/ %d",
		   "0x774(low-pri Rx/Tx)",
		   coex_sta->low_priority_rx, coex_sta->low_priority_tx);

	/* Tx mgnt queue hang or not, 0x41b should = 0xf, ex: 0xd ==>hang*/
	u1tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x41b);
	seq_printf(m, "\n %-35s = 0x%x",
		   "0x41b (mgntQ hang chk == 0xf)",
		   u1tmp[0]);

	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_COEX_STATISTICS, m);
}

void ex_btc8821a2ant_ips_notify(struct btc_coexist *btcoexist, u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	if (BTC_IPS_ENTER == type) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], IPS ENTER notify\n");
		coex_sta->under_ips = true;
		btc8821a2ant_wifi_off_hw_cfg(btcoexist);
		btc8821a2ant_ignore_wlan_act(btcoexist, FORCE_EXEC, true);
		btc8821a2ant_coex_all_off(btcoexist);
	} else if (BTC_IPS_LEAVE == type) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], IPS LEAVE notify\n");
		coex_sta->under_ips = false;
		ex_btc8821a2ant_init_hwconfig(btcoexist);
		btc8821a2ant_init_coex_dm(btcoexist);
		btc8821a2ant_query_bt_info(btcoexist);
	}
}

void ex_btc8821a2ant_lps_notify(struct btc_coexist *btcoexist, u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	if (BTC_LPS_ENABLE == type) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], LPS ENABLE notify\n");
		coex_sta->under_lps = true;
	} else if (BTC_LPS_DISABLE == type) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], LPS DISABLE notify\n");
		coex_sta->under_lps = false;
	}
}

void ex_btc8821a2ant_scan_notify(struct btc_coexist *btcoexist, u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	if (BTC_SCAN_START == type) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], SCAN START notify\n");
	} else if (BTC_SCAN_FINISH == type) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], SCAN FINISH notify\n");
	}
}

void ex_btc8821a2ant_connect_notify(struct btc_coexist *btcoexist, u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	if (BTC_ASSOCIATE_START == type) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], CONNECT START notify\n");
	} else if (BTC_ASSOCIATE_FINISH == type) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], CONNECT FINISH notify\n");
	}
}

void ex_btc8821a2ant_media_status_notify(struct btc_coexist *btcoexist,
					 u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 h2c_parameter[3] = {0};
	u32 wifi_bw;
	u8 wifi_central_chnl;
	u8 ap_num = 0;

	if (BTC_MEDIA_CONNECT == type) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], MEDIA connect notify\n");
	} else {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], MEDIA disconnect notify\n");
	}

	/* only 2.4G we need to inform bt the chnl mask */
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_CENTRAL_CHNL,
			   &wifi_central_chnl);
	if ((BTC_MEDIA_CONNECT == type) &&
	    (wifi_central_chnl <= 14)) {
		h2c_parameter[0] = 0x1;
		h2c_parameter[1] = wifi_central_chnl;
		btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
		if (wifi_bw == BTC_WIFI_BW_HT40) {
			h2c_parameter[2] = 0x30;
		} else {
			h2c_parameter[2] = 0x20;
			if (ap_num < 10)
				h2c_parameter[2] = 0x30;
			else
				h2c_parameter[2] = 0x20;
		}
	}

	coex_dm->wifi_chnl_info[0] = h2c_parameter[0];
	coex_dm->wifi_chnl_info[1] = h2c_parameter[1];
	coex_dm->wifi_chnl_info[2] = h2c_parameter[2];

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], FW write 0x66 = 0x%x\n",
		 h2c_parameter[0] << 16 |
		 h2c_parameter[1] << 8 |
		 h2c_parameter[2]);

	btcoexist->btc_fill_h2c(btcoexist, 0x66, 3, h2c_parameter);
}

void ex_btc8821a2ant_special_packet_notify(struct btc_coexist *btcoexist,
					   u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	if (type == BTC_PACKET_DHCP) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], DHCP Packet notify\n");
	}
}

void ex_btc8821a2ant_bt_info_notify(struct btc_coexist *btcoexist,
				    u8 *tmp_buf, u8 length)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 bt_info = 0;
	u8 i, rsp_source = 0;
	bool bt_busy = false, limited_dig = false;
	bool wifi_connected = false, wifi_under_5g = false;

	coex_sta->c2h_bt_info_req_sent = false;
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);

	rsp_source = tmp_buf[0] & 0xf;
	if (rsp_source >= BT_INFO_SRC_8821A_2ANT_MAX)
		rsp_source = BT_INFO_SRC_8821A_2ANT_WIFI_FW;
	coex_sta->bt_info_c2h_cnt[rsp_source]++;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], Bt info[%d], length = %d, hex data = [",
		      rsp_source, length);
	for (i = 0; i < length; i++) {
		coex_sta->bt_info_c2h[rsp_source][i] = tmp_buf[i];
		if (i == 1)
			bt_info = tmp_buf[i];
		if (i == length - 1) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "0x%02x]\n", tmp_buf[i]);
		} else {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "0x%02x, ", tmp_buf[i]);
		}
	}

	if (btcoexist->manual_control) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], BtInfoNotify(), return for Manual CTRL<===\n");
		return;
	}

	if (BT_INFO_SRC_8821A_2ANT_WIFI_FW != rsp_source) {
		/* [3:0] */
		coex_sta->bt_retry_cnt =
			coex_sta->bt_info_c2h[rsp_source][2]&0xf;

		coex_sta->bt_rssi =
			coex_sta->bt_info_c2h[rsp_source][3] * 2 + 10;

		coex_sta->bt_info_ext = coex_sta->bt_info_c2h[rsp_source][4];

		coex_sta->bt_tx_rx_mask =
			(coex_sta->bt_info_c2h[rsp_source][2] & 0x40);
		btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TX_RX_MASK,
				   &coex_sta->bt_tx_rx_mask);
		if (coex_sta->bt_tx_rx_mask) {
			/* BT into is responded by BT FW and BT RF REG 0x3C !=
			 * 0x01 => Need to switch BT TRx Mask
			 */
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Switch BT TRx Mask since BT RF REG 0x3C != 0x01\n");
			btcoexist->btc_set_bt_reg(btcoexist, BTC_BT_REG_RF,
						  0x3c, 0x01);
		}

		/* Here we need to resend some wifi info to BT
		 * because bt is reset and loss of the info
		 */
		if ((coex_sta->bt_info_ext & BIT1)) {
			btcoexist->btc_get(btcoexist,
				BTC_GET_BL_WIFI_CONNECTED, &wifi_connected);
			if (wifi_connected) {
				ex_btc8821a2ant_media_status_notify(btcoexist,
					BTC_MEDIA_CONNECT);
			} else {
				ex_btc8821a2ant_media_status_notify(btcoexist,
					BTC_MEDIA_DISCONNECT);
			}

		}

		if (!btcoexist->manual_control && !wifi_under_5g) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], BT ext info = 0x%x!!\n",
				    coex_sta->bt_info_ext);
			if ((coex_sta->bt_info_ext & BIT(3))) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], BT ext info bit3=1, wifi_connected=%d\n",
					 wifi_connected);
				if (wifi_connected) {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], BT ext info bit3 check, set BT NOT to ignore Wlan active!!\n");
					btc8821a2ant_ignore_wlan_act(btcoexist,
								     FORCE_EXEC,
								     false);
				}
			} else {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], BT ext info bit3=0, wifi_connected=%d\n",
					 wifi_connected);
				/* BT already NOT ignore Wlan active, do nothing
				 * here.
				 */
				if (!wifi_connected) {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						"[BTCoex], BT ext info bit3 check, set BT to ignore Wlan active!!\n");
					btc8821a2ant_ignore_wlan_act(
						btcoexist, FORCE_EXEC, true);
				}
			}
		}
	}

	/* check BIT2 first ==> check if bt is under inquiry or page scan*/
	if (bt_info & BT_INFO_8821A_2ANT_B_INQ_PAGE) {
		coex_sta->c2h_bt_inquiry_page = true;
	} else {
		coex_sta->c2h_bt_inquiry_page = false;
	}
	/* set link exist status */
	if (!(bt_info & BT_INFO_8821A_2ANT_B_CONNECTION)) {
		coex_sta->bt_link_exist = false;
		coex_sta->pan_exist = false;
		coex_sta->a2dp_exist = false;
		coex_sta->hid_exist = false;
		coex_sta->sco_exist = false;
	} else { /* connection exists */
		coex_sta->bt_link_exist = true;
		if (bt_info & BT_INFO_8821A_2ANT_B_FTP)
			coex_sta->pan_exist = true;
		else
			coex_sta->pan_exist = false;
		if (bt_info & BT_INFO_8821A_2ANT_B_A2DP)
			coex_sta->a2dp_exist = true;
		else
			coex_sta->a2dp_exist = false;
		if (bt_info & BT_INFO_8821A_2ANT_B_HID)
			coex_sta->hid_exist = true;
		else
			coex_sta->hid_exist = false;
		if (bt_info & BT_INFO_8821A_2ANT_B_SCO_ESCO)
			coex_sta->sco_exist = true;
		else
			coex_sta->sco_exist = false;

		if ((!coex_sta->hid_exist) &&
		    (!coex_sta->c2h_bt_inquiry_page) &&
		    (!coex_sta->sco_exist)) {
			if (coex_sta->high_priority_tx +
				    coex_sta->high_priority_rx >= 160)
				coex_sta->hid_exist = true;
		}
	}

	btc8821a2ant_update_bt_link_info(btcoexist);

	if (!(bt_info & BT_INFO_8821A_2ANT_B_CONNECTION)) {
		coex_dm->bt_status = BT_8821A_2ANT_BT_STATUS_IDLE;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], BtInfoNotify(), BT Non-Connected idle!!!\n");
	} else if (bt_info == BT_INFO_8821A_2ANT_B_CONNECTION) {
		/* connection exists but no busy */
		coex_dm->bt_status = BT_8821A_2ANT_BT_STATUS_CON_IDLE;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], BtInfoNotify(), BT Connected-idle!!!\n");
	} else if ((bt_info & BT_INFO_8821A_2ANT_B_SCO_ESCO) ||
		   (bt_info & BT_INFO_8821A_2ANT_B_SCO_BUSY)) {
		coex_dm->bt_status = BT_8821A_2ANT_BT_STATUS_SCO_BUSY;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], BtInfoNotify(), BT SCO busy!!!\n");
	} else if (bt_info & BT_INFO_8821A_2ANT_B_ACL_BUSY) {
		coex_dm->bt_status = BT_8821A_2ANT_BT_STATUS_ACL_BUSY;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], BtInfoNotify(), BT ACL busy!!!\n");
	} else {
		coex_dm->bt_status = BT_8821A_2ANT_BT_STATUS_MAX;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], BtInfoNotify(), BT Non-Defined state!!!\n");
	}

	if ((coex_dm->bt_status == BT_8821A_2ANT_BT_STATUS_ACL_BUSY) ||
	    (coex_dm->bt_status == BT_8821A_2ANT_BT_STATUS_SCO_BUSY) ||
	    (coex_dm->bt_status == BT_8821A_2ANT_BT_STATUS_ACL_SCO_BUSY)) {
		bt_busy = true;
		limited_dig = true;
	} else {
		bt_busy = false;
		limited_dig = false;
	}

	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bt_busy);

	coex_dm->limited_dig = limited_dig;
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_LIMITED_DIG, &limited_dig);

	btc8821a2ant_run_coexist_mechanism(btcoexist);
}

void ex_btc8821a2ant_halt_notify(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], Halt notify\n");

	btc8821a2ant_wifi_off_hw_cfg(btcoexist);
	btc8821a2ant_ignore_wlan_act(btcoexist, FORCE_EXEC, true);
	ex_btc8821a2ant_media_status_notify(btcoexist, BTC_MEDIA_DISCONNECT);
}

void ex_btc8821a2ant_pnp_notify(struct btc_coexist *btcoexist, u8 pnp_state)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD, "[BTCoex], Pnp notify\n");

	if (pnp_state == BTC_WIFI_PNP_SLEEP) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], Pnp notify to SLEEP\n");
	} else if (pnp_state == BTC_WIFI_PNP_WAKE_UP) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], Pnp notify to WAKE UP\n");
		ex_btc8821a2ant_init_hwconfig(btcoexist);
		btc8821a2ant_init_coex_dm(btcoexist);
		btc8821a2ant_query_bt_info(btcoexist);
	}
}

void ex_btc8821a2ant_periodical(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], ==========================Periodical===========================\n");

	if (coex_sta->dis_ver_info_cnt <= 5) {
		coex_sta->dis_ver_info_cnt += 1;
		if (coex_sta->dis_ver_info_cnt == 3) {
			/* Antenna config to set 0x765 = 0x0 (GNT_BT control by
			 * PTA) after initial
			 */
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Set GNT_BT control by PTA\n");
			btc8821a2ant_set_ant_path(btcoexist,
					BTC_ANT_WIFI_AT_MAIN, false, false);
		}
	}

	if (btcoexist->auto_report_2ant) {
		btc8821a2ant_query_bt_info(btcoexist);
	} else {
		btc8821a2ant_monitor_bt_ctr(btcoexist);
		btc8821a2ant_monitor_wifi_ctr(btcoexist);

		if (btc8821a2ant_is_wifi_status_changed(btcoexist) ||
		    coex_dm->auto_tdma_adjust)
			btc8821a2ant_run_coexist_mechanism(btcoexist);
	}
}
