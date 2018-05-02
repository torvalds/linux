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

/**************************************************************
 * Description:
 *
 * This file is for RTL8821A Co-exist mechanism
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
static struct coex_dm_8821a_1ant glcoex_dm_8821a_1ant;
static struct coex_dm_8821a_1ant *coex_dm = &glcoex_dm_8821a_1ant;
static struct coex_sta_8821a_1ant glcoex_sta_8821a_1ant;
static struct coex_sta_8821a_1ant *coex_sta = &glcoex_sta_8821a_1ant;
static void btc8821a1ant_act_bt_sco_hid_only_busy(struct btc_coexist *btcoexist,
						  u8 wifi_status);

static const char *const glbt_info_src_8821a_1ant[] = {
	  "BT Info[wifi fw]",
	  "BT Info[bt rsp]",
	  "BT Info[bt auto report]",
};

static u32 glcoex_ver_date_8821a_1ant = 20130816;
static u32 glcoex_ver_8821a_1ant = 0x41;

/**************************************************************
 * local function proto type if needed
 *
 * local function start with btc8821a1ant_
 **************************************************************/
static u8 btc8821a1ant_bt_rssi_state(struct btc_coexist *btcoexist,
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
			if (bt_rssi >= (rssi_thresh +
					BTC_RSSI_COEX_THRESH_TOL_8821A_1ANT)) {
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
			if (bt_rssi >= (rssi_thresh +
					BTC_RSSI_COEX_THRESH_TOL_8821A_1ANT)) {
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
			if (bt_rssi >= (rssi_thresh1 +
					BTC_RSSI_COEX_THRESH_TOL_8821A_1ANT)) {
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

static u8 btc8821a1ant_wifi_rssi_state(struct btc_coexist *btcoexist,
				       u8 index, u8 level_num, u8 rssi_thresh,
				       u8 rssi_thresh1)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	long	wifi_rssi = 0;
	u8	wifi_rssi_state = coex_sta->pre_wifi_rssi_state[index];

	btcoexist->btc_get(btcoexist, BTC_GET_S4_WIFI_RSSI, &wifi_rssi);

	if (level_num == 2) {
		if ((coex_sta->pre_wifi_rssi_state[index] ==
		     BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_wifi_rssi_state[index] ==
		     BTC_RSSI_STATE_STAY_LOW)) {
			if (wifi_rssi >= (rssi_thresh +
					BTC_RSSI_COEX_THRESH_TOL_8821A_1ANT)) {
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
			if (wifi_rssi >= (rssi_thresh +
					BTC_RSSI_COEX_THRESH_TOL_8821A_1ANT)) {
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
					BTC_RSSI_COEX_THRESH_TOL_8821A_1ANT)) {
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

static void btc8821a1ant_update_ra_mask(struct btc_coexist *btcoexist,
					bool force_exec, u32 dis_rate_mask)
{
	coex_dm->cur_ra_mask = dis_rate_mask;

	if (force_exec ||
	    (coex_dm->pre_ra_mask != coex_dm->cur_ra_mask)) {
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_UPDATE_RAMASK,
				   &coex_dm->cur_ra_mask);
	}
	coex_dm->pre_ra_mask = coex_dm->cur_ra_mask;
}

static void btc8821a1ant_auto_rate_fb_retry(struct btc_coexist *btcoexist,
					    bool force_exec, u8 type)
{
	bool wifi_under_b_mode = false;

	coex_dm->cur_arfr_type = type;

	if (force_exec ||
	    (coex_dm->pre_arfr_type != coex_dm->cur_arfr_type)) {
		switch (coex_dm->cur_arfr_type) {
		case 0:	/* normal mode */
			btcoexist->btc_write_4byte(btcoexist, 0x430,
						   coex_dm->backup_arfr_cnt1);
			btcoexist->btc_write_4byte(btcoexist, 0x434,
						   coex_dm->backup_arfr_cnt2);
			break;
		case 1:
			btcoexist->btc_get(btcoexist,
					   BTC_GET_BL_WIFI_UNDER_B_MODE,
					   &wifi_under_b_mode);
			if (wifi_under_b_mode) {
				btcoexist->btc_write_4byte(btcoexist, 0x430,
							   0x0);
				btcoexist->btc_write_4byte(btcoexist, 0x434,
							   0x01010101);
			} else {
				btcoexist->btc_write_4byte(btcoexist, 0x430,
							   0x0);
				btcoexist->btc_write_4byte(btcoexist, 0x434,
							   0x04030201);
			}
			break;
		default:
			break;
		}
	}

	coex_dm->pre_arfr_type = coex_dm->cur_arfr_type;
}

static void btc8821a1ant_retry_limit(struct btc_coexist *btcoexist,
				     bool force_exec, u8 type)
{
	coex_dm->cur_retry_limit_type = type;

	if (force_exec ||
	    (coex_dm->pre_retry_limit_type != coex_dm->cur_retry_limit_type)) {
		switch (coex_dm->cur_retry_limit_type) {
		case 0:	/* normal mode */
			btcoexist->btc_write_2byte(btcoexist, 0x42a,
						   coex_dm->backup_retry_limit);
			break;
		case 1:	/* retry limit = 8 */
			btcoexist->btc_write_2byte(btcoexist, 0x42a, 0x0808);
			break;
		default:
			break;
		}
	}
	coex_dm->pre_retry_limit_type = coex_dm->cur_retry_limit_type;
}

static void btc8821a1ant_ampdu_max_time(struct btc_coexist *btcoexist,
					bool force_exec, u8 type)
{
	coex_dm->cur_ampdu_time_type = type;

	if (force_exec ||
	    (coex_dm->pre_ampdu_time_type != coex_dm->cur_ampdu_time_type)) {
		switch (coex_dm->cur_ampdu_time_type) {
		case 0:	/* normal mode */
			btcoexist->btc_write_1byte(btcoexist, 0x456,
						   coex_dm->backup_ampdu_max_time);
			break;
		case 1:	/* AMPDU time = 0x38 * 32us */
			btcoexist->btc_write_1byte(btcoexist, 0x456, 0x38);
			break;
		default:
			break;
		}
	}

	coex_dm->pre_ampdu_time_type = coex_dm->cur_ampdu_time_type;
}

static void btc8821a1ant_limited_tx(struct btc_coexist *btcoexist,
				    bool force_exec, u8 ra_mask_type,
				    u8 arfr_type, u8 retry_limit_type,
				    u8 ampdu_time_type)
{
	switch (ra_mask_type) {
	case 0:	/* normal mode */
		btc8821a1ant_update_ra_mask(btcoexist, force_exec, 0x0);
		break;
	case 1:	/* disable cck 1/2 */
		btc8821a1ant_update_ra_mask(btcoexist, force_exec,
					    0x00000003);
		break;
	case 2:	/* disable cck 1/2/5.5, ofdm 6/9/12/18/24, mcs 0/1/2/3/4 */
		btc8821a1ant_update_ra_mask(btcoexist, force_exec,
					    0x0001f1f7);
		break;
	default:
		break;
	}

	btc8821a1ant_auto_rate_fb_retry(btcoexist, force_exec, arfr_type);
	btc8821a1ant_retry_limit(btcoexist, force_exec, retry_limit_type);
	btc8821a1ant_ampdu_max_time(btcoexist, force_exec, ampdu_time_type);
}

static void btc8821a1ant_limited_rx(struct btc_coexist *btcoexist,
				    bool force_exec, bool rej_ap_agg_pkt,
				    bool bt_ctrl_agg_buf_size, u8 agg_buf_size)
{
	bool reject_rx_agg = rej_ap_agg_pkt;
	bool bt_ctrl_rx_agg_size = bt_ctrl_agg_buf_size;
	u8 rx_agg_size = agg_buf_size;

	/* Rx Aggregation related setting */
	btcoexist->btc_set(btcoexist,
		 BTC_SET_BL_TO_REJ_AP_AGG_PKT, &reject_rx_agg);
	/* decide BT control aggregation buf size or not */
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_CTRL_AGG_SIZE,
			   &bt_ctrl_rx_agg_size);
	/* aggregation buf size, only work when BT control Rx agg size */
	btcoexist->btc_set(btcoexist, BTC_SET_U1_AGG_BUF_SIZE, &rx_agg_size);
	/* real update aggregation setting */
	btcoexist->btc_set(btcoexist, BTC_SET_ACT_AGGREGATE_CTRL, NULL);
}

static void btc8821a1ant_monitor_bt_ctr(struct btc_coexist *btcoexist)
{
	u32 reg_hp_tx_rx, reg_lp_tx_rx, u4_tmp;
	u32 reg_hp_tx = 0, reg_hp_rx = 0, reg_lp_tx = 0, reg_lp_rx = 0;

	reg_hp_tx_rx = 0x770;
	reg_lp_tx_rx = 0x774;

	u4_tmp = btcoexist->btc_read_4byte(btcoexist, reg_hp_tx_rx);
	reg_hp_tx = u4_tmp & MASKLWORD;
	reg_hp_rx = (u4_tmp & MASKHWORD) >> 16;

	u4_tmp = btcoexist->btc_read_4byte(btcoexist, reg_lp_tx_rx);
	reg_lp_tx = u4_tmp & MASKLWORD;
	reg_lp_rx = (u4_tmp & MASKHWORD) >> 16;

	coex_sta->high_priority_tx = reg_hp_tx;
	coex_sta->high_priority_rx = reg_hp_rx;
	coex_sta->low_priority_tx = reg_lp_tx;
	coex_sta->low_priority_rx = reg_lp_rx;

	/* reset counter */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0xc);
}

static void btc8821a1ant_query_bt_info(struct btc_coexist *btcoexist)
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

static void btc8821a1ant_update_bt_link_info(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info	*bt_link_info = &btcoexist->bt_link_info;
	bool bt_hs_on = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);

	bt_link_info->bt_link_exist = coex_sta->bt_link_exist;
	bt_link_info->sco_exist = coex_sta->sco_exist;
	bt_link_info->a2dp_exist = coex_sta->a2dp_exist;
	bt_link_info->pan_exist = coex_sta->pan_exist;
	bt_link_info->hid_exist = coex_sta->hid_exist;

	/* work around for HS mode */
	if (bt_hs_on) {
		bt_link_info->pan_exist = true;
		bt_link_info->bt_link_exist = true;
	}

	/* check if Sco only */
	if (bt_link_info->sco_exist &&
	    !bt_link_info->a2dp_exist &&
	    !bt_link_info->pan_exist &&
	    !bt_link_info->hid_exist)
		bt_link_info->sco_only = true;
	else
		bt_link_info->sco_only = false;

	/* check if A2dp only */
	if (!bt_link_info->sco_exist &&
	    bt_link_info->a2dp_exist &&
	    !bt_link_info->pan_exist &&
	    !bt_link_info->hid_exist)
		bt_link_info->a2dp_only = true;
	else
		bt_link_info->a2dp_only = false;

	/* check if Pan only */
	if (!bt_link_info->sco_exist &&
	    !bt_link_info->a2dp_exist &&
	    bt_link_info->pan_exist &&
	    !bt_link_info->hid_exist)
		bt_link_info->pan_only = true;
	else
		bt_link_info->pan_only = false;

	/* check if Hid only */
	if (!bt_link_info->sco_exist &&
	    !bt_link_info->a2dp_exist &&
	    !bt_link_info->pan_exist &&
	    bt_link_info->hid_exist)
		bt_link_info->hid_only = true;
	else
		bt_link_info->hid_only = false;
}

static u8 btc8821a1ant_action_algorithm(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool bt_hs_on = false;
	u8 algorithm = BT_8821A_1ANT_COEX_ALGO_UNDEFINED;
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
				 "[BTCoex], BT Profile = SCO only\n");
			algorithm = BT_8821A_1ANT_COEX_ALGO_SCO;
		} else {
			if (bt_link_info->hid_exist) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], BT Profile = HID only\n");
				algorithm = BT_8821A_1ANT_COEX_ALGO_HID;
			} else if (bt_link_info->a2dp_exist) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], BT Profile = A2DP only\n");
				algorithm = BT_8821A_1ANT_COEX_ALGO_A2DP;
			} else if (bt_link_info->pan_exist) {
				if (bt_hs_on) {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], BT Profile = PAN(HS) only\n");
					algorithm = BT_8821A_1ANT_COEX_ALGO_PANHS;
				} else {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], BT Profile = PAN(EDR) only\n");
					algorithm = BT_8821A_1ANT_COEX_ALGO_PANEDR;
				}
			}
		}
	} else if (num_of_diff_profile == 2) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], BT Profile = SCO + HID\n");
				algorithm = BT_8821A_1ANT_COEX_ALGO_HID;
			} else if (bt_link_info->a2dp_exist) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], BT Profile = SCO + A2DP ==> SCO\n");
				algorithm = BT_8821A_1ANT_COEX_ALGO_SCO;
			} else if (bt_link_info->pan_exist) {
				if (bt_hs_on) {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], BT Profile = SCO + PAN(HS)\n");
					algorithm = BT_8821A_1ANT_COEX_ALGO_SCO;
				} else {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], BT Profile = SCO + PAN(EDR)\n");
					algorithm = BT_8821A_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (bt_link_info->hid_exist &&
			    bt_link_info->a2dp_exist) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], BT Profile = HID + A2DP\n");
				algorithm = BT_8821A_1ANT_COEX_ALGO_HID_A2DP;
			} else if (bt_link_info->hid_exist &&
				   bt_link_info->pan_exist) {
				if (bt_hs_on) {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], BT Profile = HID + PAN(HS)\n");
					algorithm = BT_8821A_1ANT_COEX_ALGO_HID_A2DP;
				} else {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], BT Profile = HID + PAN(EDR)\n");
					algorithm = BT_8821A_1ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (bt_link_info->pan_exist &&
				   bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], BT Profile = A2DP + PAN(HS)\n");
					algorithm = BT_8821A_1ANT_COEX_ALGO_A2DP_PANHS;
				} else {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], BT Profile = A2DP + PAN(EDR)\n");
					algorithm = BT_8821A_1ANT_COEX_ALGO_PANEDR_A2DP;
				}
			}
		}
	} else if (num_of_diff_profile == 3) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist &&
			    bt_link_info->a2dp_exist) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], BT Profile = SCO + HID + A2DP ==> HID\n");
				algorithm = BT_8821A_1ANT_COEX_ALGO_HID;
			} else if (bt_link_info->hid_exist &&
				   bt_link_info->pan_exist) {
				if (bt_hs_on) {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], BT Profile = SCO + HID + PAN(HS)\n");
					algorithm = BT_8821A_1ANT_COEX_ALGO_HID_A2DP;
				} else {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], BT Profile = SCO + HID + PAN(EDR)\n");
					algorithm = BT_8821A_1ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (bt_link_info->pan_exist &&
				   bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], BT Profile = SCO + A2DP + PAN(HS)\n");
					algorithm = BT_8821A_1ANT_COEX_ALGO_SCO;
				} else {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], BT Profile = SCO + A2DP + PAN(EDR) ==> HID\n");
					algorithm = BT_8821A_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (bt_link_info->hid_exist &&
			    bt_link_info->pan_exist &&
			    bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], BT Profile = HID + A2DP + PAN(HS)\n");
					algorithm = BT_8821A_1ANT_COEX_ALGO_HID_A2DP;
				} else {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], BT Profile = HID + A2DP + PAN(EDR)\n");
					algorithm = BT_8821A_1ANT_COEX_ALGO_HID_A2DP_PANEDR;
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
						 "[BTCoex], Error!!! BT Profile = SCO + HID + A2DP + PAN(HS)\n");

				} else {
					RT_TRACE(rtlpriv, COMP_BT_COEXIST,
						 DBG_LOUD,
						 "[BTCoex], BT Profile = SCO + HID + A2DP + PAN(EDR)==>PAN(EDR)+HID\n");
					algorithm = BT_8821A_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
	}
	return algorithm;
}

static void btc8821a1ant_set_sw_penalty_tx_rate(struct btc_coexist *btcoexist,
						bool low_penalty_ra)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 h2c_parameter[6] = {0};

	h2c_parameter[0] = 0x6;	/* opCode, 0x6= Retry_Penalty*/

	if (low_penalty_ra) {
		h2c_parameter[1] |= BIT0;
		/* normal rate except MCS7/6/5, OFDM54/48/36 */
		h2c_parameter[2] = 0x00;
		h2c_parameter[3] = 0xf7; /* MCS7 or OFDM54 */
		h2c_parameter[4] = 0xf8; /* MCS6 or OFDM48 */
		h2c_parameter[5] = 0xf9; /* MCS5 or OFDM36 */
	}

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], set WiFi Low-Penalty Retry: %s",
		 (low_penalty_ra ? "ON!!" : "OFF!!"));

	btcoexist->btc_fill_h2c(btcoexist, 0x69, 6, h2c_parameter);
}

static void btc8821a1ant_low_penalty_ra(struct btc_coexist *btcoexist,
					bool force_exec, bool low_penalty_ra)
{
	coex_dm->cur_low_penalty_ra = low_penalty_ra;

	if (!force_exec) {
		if (coex_dm->pre_low_penalty_ra == coex_dm->cur_low_penalty_ra)
			return;
	}
	btc8821a1ant_set_sw_penalty_tx_rate(btcoexist,
					    coex_dm->cur_low_penalty_ra);

	coex_dm->pre_low_penalty_ra = coex_dm->cur_low_penalty_ra;
}

static void btc8821a1ant_set_coex_table(struct btc_coexist *btcoexist,
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

static void btc8821a1ant_coex_table(struct btc_coexist *btcoexist,
				    bool force_exec, u32 val0x6c0, u32 val0x6c4,
				    u32 val0x6c8, u8 val0x6cc)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], %s write Coex Table 0x6c0 = 0x%x, 0x6c4 = 0x%x, 0x6c8 = 0x%x, 0x6cc = 0x%x\n",
		    (force_exec ? "force to" : ""), val0x6c0, val0x6c4,
		    val0x6c8, val0x6cc);
	coex_dm->cur_val_0x6c0 = val0x6c0;
	coex_dm->cur_val_0x6c4 = val0x6c4;
	coex_dm->cur_val_0x6c8 = val0x6c8;
	coex_dm->cur_val_0x6cc = val0x6cc;

	if (!force_exec) {
		if ((coex_dm->pre_val_0x6c0 == coex_dm->cur_val_0x6c0) &&
		    (coex_dm->pre_val_0x6c4 == coex_dm->cur_val_0x6c4) &&
		    (coex_dm->pre_val_0x6c8 == coex_dm->cur_val_0x6c8) &&
		    (coex_dm->pre_val_0x6cc == coex_dm->cur_val_0x6cc))
			return;
	}
	btc8821a1ant_set_coex_table(btcoexist, val0x6c0, val0x6c4,
				    val0x6c8, val0x6cc);

	coex_dm->pre_val_0x6c0 = coex_dm->cur_val_0x6c0;
	coex_dm->pre_val_0x6c4 = coex_dm->cur_val_0x6c4;
	coex_dm->pre_val_0x6c8 = coex_dm->cur_val_0x6c8;
	coex_dm->pre_val_0x6cc = coex_dm->cur_val_0x6cc;
}

static void btc8821a1ant_coex_table_with_type(struct btc_coexist *btcoexist,
					      bool force_exec, u8 type)
{
	switch (type) {
	case 0:
		btc8821a1ant_coex_table(btcoexist, force_exec, 0x55555555,
					0x55555555, 0xffffff, 0x3);
		break;
	case 1:
		btc8821a1ant_coex_table(btcoexist, force_exec, 0x55555555,
					0x5a5a5a5a, 0xffffff, 0x3);
		break;
	case 2:
		btc8821a1ant_coex_table(btcoexist, force_exec, 0x5a5a5a5a,
					0x5a5a5a5a, 0xffffff, 0x3);
		break;
	case 3:
		btc8821a1ant_coex_table(btcoexist, force_exec, 0x5a5a5a5a,
					0xaaaaaaaa, 0xffffff, 0x3);
		break;
	case 4:
		btc8821a1ant_coex_table(btcoexist, force_exec, 0x55555555,
					0x5a5a5a5a, 0xffffff, 0x3);
		break;
	case 5:
		btc8821a1ant_coex_table(btcoexist, force_exec, 0x5a5a5a5a,
					0xaaaa5a5a, 0xffffff, 0x3);
		break;
	case 6:
		btc8821a1ant_coex_table(btcoexist, force_exec, 0x55555555,
					0xaaaa5a5a, 0xffffff, 0x3);
		break;
	case 7:
		btc8821a1ant_coex_table(btcoexist, force_exec, 0xaaaaaaaa,
					0xaaaaaaaa, 0xffffff, 0x3);
		break;
	default:
		break;
	}
}

static void btc8821a1ant_set_fw_ignore_wlan_act(struct btc_coexist *btcoexist,
						bool enable)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 h2c_parameter[1] = {0};

	if (enable)
		h2c_parameter[0] |= BIT0; /* function enable */

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], set FW for BT Ignore Wlan_Act, FW write 0x63 = 0x%x\n",
		 h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x63, 1, h2c_parameter);
}

static void btc8821a1ant_ignore_wlan_act(struct btc_coexist *btcoexist,
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
	btc8821a1ant_set_fw_ignore_wlan_act(btcoexist, enable);

	coex_dm->pre_ignore_wlan_act = coex_dm->cur_ignore_wlan_act;
}

static void btc8821a1ant_set_fw_ps_tdma(struct btc_coexist *btcoexist, u8 byte1,
					u8 byte2, u8 byte3, u8 byte4, u8 byte5)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 h2c_parameter[5] = {0};
	u8 real_byte1 = byte1, real_byte5 = byte5;
	bool ap_enable = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE,
			   &ap_enable);

	if (ap_enable) {
		if (byte1 & BIT4 && !(byte1 & BIT5)) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], FW for 1Ant AP mode\n");
			real_byte1 &= ~BIT4;
			real_byte1 |= BIT5;

			real_byte5 |= BIT5;
			real_byte5 &= ~BIT6;
		}
	}

	h2c_parameter[0] = real_byte1;
	h2c_parameter[1] = byte2;
	h2c_parameter[2] = byte3;
	h2c_parameter[3] = byte4;
	h2c_parameter[4] = real_byte5;

	coex_dm->ps_tdma_para[0] = real_byte1;
	coex_dm->ps_tdma_para[1] = byte2;
	coex_dm->ps_tdma_para[2] = byte3;
	coex_dm->ps_tdma_para[3] = byte4;
	coex_dm->ps_tdma_para[4] = real_byte5;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], PS-TDMA H2C cmd =0x%x%08x\n",
		 h2c_parameter[0],
		 h2c_parameter[1] << 24 |
		 h2c_parameter[2] << 16 |
		 h2c_parameter[3] << 8 |
		 h2c_parameter[4]);
	btcoexist->btc_fill_h2c(btcoexist, 0x60, 5, h2c_parameter);
}

static void btc8821a1ant_set_lps_rpwm(struct btc_coexist *btcoexist,
				      u8 lps_val, u8 rpwm_val)
{
	u8 lps = lps_val;
	u8 rpwm = rpwm_val;

	btcoexist->btc_set(btcoexist, BTC_SET_U1_LPS_VAL, &lps);
	btcoexist->btc_set(btcoexist, BTC_SET_U1_RPWM_VAL, &rpwm);
}

static void btc8821a1ant_lps_rpwm(struct btc_coexist *btcoexist,
				  bool force_exec, u8 lps_val, u8 rpwm_val)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], %s set lps/rpwm = 0x%x/0x%x\n",
		 (force_exec ? "force to" : ""), lps_val, rpwm_val);
	coex_dm->cur_lps = lps_val;
	coex_dm->cur_rpwm = rpwm_val;

	if (!force_exec) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], LPS-RxBeaconMode = 0x%x, LPS-RPWM = 0x%x!!\n",
			 coex_dm->cur_lps, coex_dm->cur_rpwm);

		if ((coex_dm->pre_lps == coex_dm->cur_lps) &&
		    (coex_dm->pre_rpwm == coex_dm->cur_rpwm)) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], LPS-RPWM_Last = 0x%x, LPS-RPWM_Now = 0x%x!!\n",
				 coex_dm->pre_rpwm, coex_dm->cur_rpwm);

			return;
		}
	}
	btc8821a1ant_set_lps_rpwm(btcoexist, lps_val, rpwm_val);

	coex_dm->pre_lps = coex_dm->cur_lps;
	coex_dm->pre_rpwm = coex_dm->cur_rpwm;
}

static void btc8821a1ant_sw_mechanism(struct btc_coexist *btcoexist,
				      bool low_penalty_ra)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], SM[LpRA] = %d\n", low_penalty_ra);

	btc8821a1ant_low_penalty_ra(btcoexist, NORMAL_EXEC, low_penalty_ra);
}

static void btc8821a1ant_set_ant_path(struct btc_coexist *btcoexist,
				      u8 ant_pos_type, bool init_hw_cfg,
				      bool wifi_off)
{
	struct btc_board_info *board_info = &btcoexist->board_info;
	u32 u4_tmp = 0;
	u8 h2c_parameter[2] = {0};

	if (init_hw_cfg) {
		/* 0x4c[23] = 0, 0x4c[24] = 1  Antenna control by WL/BT */
		u4_tmp = btcoexist->btc_read_4byte(btcoexist, 0x4c);
		u4_tmp &= ~BIT23;
		u4_tmp |= BIT24;
		btcoexist->btc_write_4byte(btcoexist, 0x4c, u4_tmp);

		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x975, 0x3, 0x3);
		btcoexist->btc_write_1byte(btcoexist, 0xcb4, 0x77);

		if (board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT) {
			/* tell firmware "antenna inverse"
			 * WRONG firmware antenna control code, need fw to fix
			 */
			h2c_parameter[0] = 1;
			h2c_parameter[1] = 1;
			btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
						h2c_parameter);
		} else {
			/* tell firmware "no antenna inverse"
			 * WRONG firmware antenna control code, need fw to fix
			 */
			h2c_parameter[0] = 0;
			h2c_parameter[1] = 1;
			btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
						h2c_parameter);
		}
	} else if (wifi_off) {
		/* 0x4c[24:23] = 00, Set Antenna control
		 * by BT_RFE_CTRL BT Vendor 0xac = 0xf002
		 */
		u4_tmp = btcoexist->btc_read_4byte(btcoexist, 0x4c);
		u4_tmp &= ~BIT23;
		u4_tmp &= ~BIT24;
		btcoexist->btc_write_4byte(btcoexist, 0x4c, u4_tmp);

		/* 0x765 = 0x18 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x765, 0x18, 0x3);
	} else {
		/* 0x765 = 0x0 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x765, 0x18, 0x0);
	}

	/* ext switch setting */
	switch (ant_pos_type) {
	case BTC_ANT_PATH_WIFI:
		btcoexist->btc_write_1byte(btcoexist, 0xcb4, 0x77);
		if (board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT)
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcb7,
							   0x30, 0x1);
		else
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcb7,
							   0x30, 0x2);
		break;
	case BTC_ANT_PATH_BT:
		btcoexist->btc_write_1byte(btcoexist, 0xcb4, 0x77);
		if (board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT)
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcb7,
							   0x30, 0x2);
		else
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcb7,
							   0x30, 0x1);
		break;
	default:
	case BTC_ANT_PATH_PTA:
		btcoexist->btc_write_1byte(btcoexist, 0xcb4, 0x66);
		if (board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT)
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcb7,
							   0x30, 0x1);
		else
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcb7,
							   0x30, 0x2);
		break;
	}
}

static void btc8821a1ant_ps_tdma(struct btc_coexist *btcoexist,
				 bool force_exec, bool turn_on, u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 rssi_adjust_val = 0;

	coex_dm->cur_ps_tdma_on = turn_on;
	coex_dm->cur_ps_tdma = type;

	if (!force_exec) {
		if (coex_dm->cur_ps_tdma_on) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], ********** TDMA(on, %d) **********\n",
				 coex_dm->cur_ps_tdma);
		} else {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], ********** TDMA(off, %d) **********\n",
				 coex_dm->cur_ps_tdma);
		}
		if ((coex_dm->pre_ps_tdma_on == coex_dm->cur_ps_tdma_on) &&
		    (coex_dm->pre_ps_tdma == coex_dm->cur_ps_tdma))
			return;
	}
	if (turn_on) {
		switch (type) {
		default:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x1a,
						    0x1a, 0x0, 0x50);
			break;
		case 1:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x3a,
						    0x03, 0x10, 0x50);
			rssi_adjust_val = 11;
			break;
		case 2:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x2b,
						    0x03, 0x10, 0x50);
			rssi_adjust_val = 14;
			break;
		case 3:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x1d,
						    0x1d, 0x0, 0x10);
			break;
		case 4:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x93, 0x15,
						    0x3, 0x14, 0x0);
			rssi_adjust_val = 17;
			break;
		case 5:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x61, 0x15,
						    0x3, 0x11, 0x10);
			break;
		case 6:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x61, 0x20,
						    0x3, 0x11, 0x13);
			break;
		case 7:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x13, 0xc,
						    0x5, 0x0, 0x0);
			break;
		case 8:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x93, 0x25,
						    0x3, 0x10, 0x0);
			break;
		case 9:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x21,
						    0x3, 0x10, 0x50);
			rssi_adjust_val = 18;
			break;
		case 10:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x13, 0xa,
						    0xa, 0x0, 0x40);
			break;
		case 11:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x15,
						    0x03, 0x10, 0x50);
			rssi_adjust_val = 20;
			break;
		case 12:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x0a,
						    0x0a, 0x0, 0x50);
			break;
		case 13:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x12,
						    0x12, 0x0, 0x50);
			break;
		case 14:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x1e,
						    0x3, 0x10, 0x14);
			break;
		case 15:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x13, 0xa,
						    0x3, 0x8, 0x0);
			break;
		case 16:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x93, 0x15,
						    0x3, 0x10, 0x0);
			rssi_adjust_val = 18;
			break;
		case 18:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x93, 0x25,
						    0x3, 0x10, 0x0);
			rssi_adjust_val = 14;
			break;
		case 20:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x61, 0x35,
						    0x03, 0x11, 0x10);
			break;
		case 21:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x61, 0x25,
						    0x03, 0x11, 0x11);
			break;
		case 22:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x61, 0x25,
						    0x03, 0x11, 0x10);
			break;
		case 23:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x25,
						    0x3, 0x31, 0x18);
			rssi_adjust_val = 22;
			break;
		case 24:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x15,
						    0x3, 0x31, 0x18);
			rssi_adjust_val = 22;
			break;
		case 25:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0xe3, 0xa,
						    0x3, 0x31, 0x18);
			rssi_adjust_val = 22;
			break;
		case 26:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0xe3, 0xa,
						    0x3, 0x31, 0x18);
			rssi_adjust_val = 22;
			break;
		case 27:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x25,
						    0x3, 0x31, 0x98);
			rssi_adjust_val = 22;
			break;
		case 28:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x69, 0x25,
						    0x3, 0x31, 0x0);
			break;
		case 29:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0xab, 0x1a,
						    0x1a, 0x1, 0x10);
			break;
		case 30:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x30,
						    0x3, 0x10, 0x10);
			break;
		case 31:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0xd3, 0x1a,
						    0x1a, 0, 0x58);
			break;
		case 32:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x61, 0x35,
						    0x3, 0x11, 0x11);
			break;
		case 33:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0xa3, 0x25,
						    0x3, 0x30, 0x90);
			break;
		case 34:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x53, 0x1a,
						    0x1a, 0x0, 0x10);
			break;
		case 35:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x63, 0x1a,
						    0x1a, 0x0, 0x10);
			break;
		case 36:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0xd3, 0x12,
						    0x3, 0x14, 0x50);
			break;
		case 40:
			/* SoftAP only with no sta associated, BT disable, TDMA
			 * mode for power saving
			 *
			 * here softap mode screen off will cost 70-80mA for
			 * phone
			 */
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x23, 0x18,
						    0x00, 0x10, 0x24);
			break;
		case 41:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x15,
						    0x3, 0x11, 0x11);
			break;
		case 42:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x20,
						    0x3, 0x11, 0x11);
			break;
		case 43:
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x30,
						    0x3, 0x10, 0x11);
			break;
		}
	} else {
		/* disable PS tdma */
		switch (type) {
		case 8:
			/* PTA Control */
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x8, 0x0, 0x0,
						    0x0, 0x0);
			btc8821a1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA,
						  false, false);
			break;
		case 0:
		default:
			/* Software control, Antenna at BT side */
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x0, 0x0, 0x0,
						    0x0, 0x0);
			btc8821a1ant_set_ant_path(btcoexist, BTC_ANT_PATH_BT,
						  false, false);
			break;
		case 9:
			/* Software control, Antenna at WiFi side */
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x0, 0x0, 0x0,
						    0x0, 0x0);
			btc8821a1ant_set_ant_path(btcoexist, BTC_ANT_PATH_WIFI,
						  false, false);
			break;
		case 10:
			/* under 5G */
			btc8821a1ant_set_fw_ps_tdma(btcoexist, 0x0, 0x0, 0x0,
						    0x8, 0x0);
			btc8821a1ant_set_ant_path(btcoexist, BTC_ANT_PATH_BT,
						  false, false);
			break;
		}
	}
	rssi_adjust_val = 0;
	btcoexist->btc_set(btcoexist,
		 BTC_SET_U1_RSSI_ADJ_VAL_FOR_1ANT_COEX_TYPE, &rssi_adjust_val);

	/* update pre state */
	coex_dm->pre_ps_tdma_on = coex_dm->cur_ps_tdma_on;
	coex_dm->pre_ps_tdma = coex_dm->cur_ps_tdma;
}

static bool btc8821a1ant_is_common_action(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	bool common = false, wifi_connected = false, wifi_busy = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	if (!wifi_connected &&
	    BT_8821A_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
	    coex_dm->bt_status) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], Wifi non connected-idle + BT non connected-idle!!\n");
		btc8821a1ant_sw_mechanism(btcoexist, false);

		common = true;
	} else if (wifi_connected &&
		   (BT_8821A_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
		    coex_dm->bt_status)) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], Wifi connected + BT non connected-idle!!\n");
		btc8821a1ant_sw_mechanism(btcoexist, false);

		common = true;
	} else if (!wifi_connected &&
		   (BT_8821A_1ANT_BT_STATUS_CONNECTED_IDLE ==
		    coex_dm->bt_status)) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], Wifi non connected-idle + BT connected-idle!!\n");
		btc8821a1ant_sw_mechanism(btcoexist, false);

		common = true;
	} else if (wifi_connected &&
		   (BT_8821A_1ANT_BT_STATUS_CONNECTED_IDLE ==
		    coex_dm->bt_status)) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], Wifi connected + BT connected-idle!!\n");
		btc8821a1ant_sw_mechanism(btcoexist, false);

		common = true;
	} else if (!wifi_connected &&
		   (BT_8821A_1ANT_BT_STATUS_CONNECTED_IDLE !=
		    coex_dm->bt_status)) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], Wifi non connected-idle + BT Busy!!\n");
		btc8821a1ant_sw_mechanism(btcoexist, false);

		common = true;
	} else {
		if (wifi_busy) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Wifi Connected-Busy + BT Busy!!\n");
		} else {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Wifi Connected-Idle + BT Busy!!\n");
		}

		common = false;
	}

	return common;
}

static void btc8821a1ant_ps_tdma_check_for_pwr_save(struct btc_coexist *btcoex,
						    bool new_ps_state)
{
	u8 lps_mode = 0x0;

	btcoex->btc_get(btcoex, BTC_GET_U1_LPS_MODE, &lps_mode);

	if (lps_mode) {
		/* already under LPS state */
		if (new_ps_state) {
			/* keep state under LPS, do nothing */
		} else {
			/* will leave LPS state, turn off psTdma first */
			btc8821a1ant_ps_tdma(btcoex, NORMAL_EXEC, false, 0);
		}
	} else {
		/* NO PS state*/
		if (new_ps_state) {
			/* will enter LPS state, turn off psTdma first */
			btc8821a1ant_ps_tdma(btcoex, NORMAL_EXEC, false, 0);
		} else {
			/* keep state under NO PS state, do nothing */
		}
	}
}

static void btc8821a1ant_power_save_state(struct btc_coexist *btcoexist,
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
		break;
	case BTC_PS_LPS_ON:
		btc8821a1ant_ps_tdma_check_for_pwr_save(btcoexist,
							true);
		btc8821a1ant_lps_rpwm(btcoexist, NORMAL_EXEC, lps_val,
				      rpwm_val);
		/* when coex force to enter LPS, do not enter 32k low power */
		low_pwr_disable = true;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);
		/* power save must executed before psTdma */
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_ENTER_LPS, NULL);
		break;
	case BTC_PS_LPS_OFF:
		btc8821a1ant_ps_tdma_check_for_pwr_save(btcoexist, false);
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
		break;
	default:
		break;
	}
}

static void btc8821a1ant_coex_under_5g(struct btc_coexist *btcoexist)
{
	btc8821a1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
				      0x0, 0x0);
	btc8821a1ant_ignore_wlan_act(btcoexist, NORMAL_EXEC, true);

	btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 10);

	btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

	btc8821a1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);

	btc8821a1ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 5);
}

/***********************************************
 *
 *	Software Coex Mechanism start
 *
 ***********************************************/

/* SCO only or SCO+PAN(HS) */
static void btc8821a1ant_action_sco(struct btc_coexist *btcoexist)
{
	btc8821a1ant_sw_mechanism(btcoexist, true);
}

static void btc8821a1ant_action_hid(struct btc_coexist *btcoexist)
{
	btc8821a1ant_sw_mechanism(btcoexist, true);
}

/* A2DP only / PAN(EDR) only/ A2DP+PAN(HS) */
static void btc8821a1ant_action_a2dp(struct btc_coexist *btcoexist)
{
	btc8821a1ant_sw_mechanism(btcoexist, false);
}

static void btc8821a1ant_action_a2dp_pan_hs(struct btc_coexist *btcoexist)
{
	btc8821a1ant_sw_mechanism(btcoexist, false);
}

static void btc8821a1ant_action_pan_edr(struct btc_coexist *btcoexist)
{
	btc8821a1ant_sw_mechanism(btcoexist, false);
}

/* PAN(HS) only */
static void btc8821a1ant_action_pan_hs(struct btc_coexist *btcoexist)
{
	btc8821a1ant_sw_mechanism(btcoexist, false);
}

/* PAN(EDR)+A2DP */
static void btc8821a1ant_action_pan_edr_a2dp(struct btc_coexist *btcoexist)
{
	btc8821a1ant_sw_mechanism(btcoexist, false);
}

static void btc8821a1ant_action_pan_edr_hid(struct btc_coexist *btcoexist)
{
	btc8821a1ant_sw_mechanism(btcoexist, true);
}

/* HID+A2DP+PAN(EDR) */
static void btc8821a1ant_action_hid_a2dp_pan_edr(struct btc_coexist *btcoexist)
{
	btc8821a1ant_sw_mechanism(btcoexist, true);
}

static void btc8821a1ant_action_hid_a2dp(struct btc_coexist *btcoexist)
{
	btc8821a1ant_sw_mechanism(btcoexist, true);
}

/***********************************************
 *
 *	Non-Software Coex Mechanism start
 *
 ***********************************************/
static
void btc8821a1ant_action_wifi_multi_port(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	btc8821a1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
	/* tdma and coex table */
	if (coex_dm->bt_status == BT_8821A_1ANT_BT_STATUS_ACL_BUSY) {
		if (bt_link_info->a2dp_exist) {
			btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 14);
			btc8821a1ant_coex_table_with_type(btcoexist,
							  NORMAL_EXEC, 1);
		} else if (bt_link_info->a2dp_exist &&
			   bt_link_info->pan_exist) {
			btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
			btc8821a1ant_coex_table_with_type(btcoexist,
							  NORMAL_EXEC, 4);
		} else {
			btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 20);
			btc8821a1ant_coex_table_with_type(btcoexist,
							  NORMAL_EXEC, 4);
		}
	} else if ((coex_dm->bt_status == BT_8821A_1ANT_BT_STATUS_SCO_BUSY) ||
		   (BT_8821A_1ANT_BT_STATUS_ACL_SCO_BUSY ==
		    coex_dm->bt_status)) {
		btc8821a1ant_act_bt_sco_hid_only_busy(btcoexist,
				BT_8821A_1ANT_WIFI_STATUS_CONNECTED_SCAN);
	} else {
		btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
	}
}

static
void btc8821a1ant_action_wifi_not_connected_asso_auth(
					struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	btc8821a1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0,
				      0x0);

	/* tdma and coex table */
	if ((bt_link_info->sco_exist) || (bt_link_info->hid_exist)) {
		btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 14);
		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
	} else if ((bt_link_info->a2dp_exist) || (bt_link_info->pan_exist)) {
		btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 20);
		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
	} else {
		btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
	}
}


static void btc8821a1ant_action_hs(struct btc_coexist *btcoexist)
{
	btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 5);
	btc8821a1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 2);
}

static void btc8821a1ant_action_bt_inquiry(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool wifi_connected = false;
	bool ap_enable = false;
	bool wifi_busy = false, bt_busy = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE,
			   &ap_enable);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bt_busy);

	if (!wifi_connected && !coex_sta->wifi_is_high_pri_task) {
		btc8821a1ant_power_save_state(btcoexist,
					      BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
	} else if ((bt_link_info->sco_exist) || (bt_link_info->a2dp_exist) ||
		   (bt_link_info->hid_only)) {
		/* SCO/HID-only busy */
		btc8821a1ant_power_save_state(btcoexist,
					      BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 32);
		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
	} else if ((bt_link_info->a2dp_exist) && (bt_link_info->hid_exist)) {
		/* A2DP+HID busy */
		btc8821a1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					      0x0, 0x0);
		btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 14);

		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
	} else if ((bt_link_info->pan_exist) || (wifi_busy)) {
		btc8821a1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					      0x0, 0x0);
		btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 20);

		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
	} else {
		btc8821a1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					      0x0, 0x0);
		btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
	}
}

static void btc8821a1ant_act_bt_sco_hid_only_busy(struct btc_coexist *btcoexist,
						  u8 wifi_status)
{
	/* tdma and coex table */
	btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 5);

	if (BT_8821A_1ANT_WIFI_STATUS_NON_CONNECTED_ASSO_AUTH_SCAN ==
	    wifi_status)
		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
	else
		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
}

static void btc8821a1ant_act_wifi_con_bt_acl_busy(struct btc_coexist *btcoexist,
						  u8 wifi_status)
{
	u8 bt_rssi_state;

	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	bt_rssi_state = btc8821a1ant_bt_rssi_state(btcoexist, 2, 28, 0);

	if (bt_link_info->hid_only) {
		/* HID */
		btc8821a1ant_act_bt_sco_hid_only_busy(btcoexist,
						      wifi_status);
		coex_dm->auto_tdma_adjust = false;
		return;
	} else if (bt_link_info->a2dp_only) {
		/* A2DP */
		if (wifi_status == BT_8821A_1ANT_WIFI_STATUS_CONNECTED_IDLE) {
			btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 32);
			btc8821a1ant_coex_table_with_type(btcoexist,
							  NORMAL_EXEC, 1);
			coex_dm->auto_tdma_adjust = false;
		} else if ((bt_rssi_state != BTC_RSSI_STATE_HIGH) &&
			   (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 14);
			btc8821a1ant_coex_table_with_type(btcoexist,
							  NORMAL_EXEC, 1);
		} else {
			/* for low BT RSSI */
			btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 14);
			btc8821a1ant_coex_table_with_type(btcoexist,
							  NORMAL_EXEC, 1);
			coex_dm->auto_tdma_adjust = false;
		}
	} else if (bt_link_info->hid_exist && bt_link_info->a2dp_exist) {
		/* HID+A2DP */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC,
					     true, 14);
			coex_dm->auto_tdma_adjust = false;
		} else {
			/*for low BT RSSI*/
			btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC,
					     true, 14);
			coex_dm->auto_tdma_adjust = false;
		}

		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
	} else if ((bt_link_info->pan_only) ||
		(bt_link_info->hid_exist && bt_link_info->pan_exist)) {
		/* PAN(OPP, FTP), HID+PAN(OPP, FTP) */
		btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 3);
		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 6);
		coex_dm->auto_tdma_adjust = false;
	} else if (((bt_link_info->a2dp_exist) && (bt_link_info->pan_exist)) ||
		   (bt_link_info->hid_exist && bt_link_info->a2dp_exist &&
		    bt_link_info->pan_exist)) {
		/* A2DP+PAN(OPP, FTP), HID+A2DP+PAN(OPP, FTP) */
		btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 43);
		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
		coex_dm->auto_tdma_adjust = false;
	} else {
		btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 11);
		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
		coex_dm->auto_tdma_adjust = false;
	}
}

static
void btc8821a1ant_action_wifi_not_connected(struct btc_coexist *btcoexist)
{
	/* power save state */
	btc8821a1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	/* tdma and coex table */
	btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
	btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
}

static void btc8821a1ant_act_wifi_not_conn_scan(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	btc8821a1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	/* tdma and coex table */
	if (coex_dm->bt_status == BT_8821A_1ANT_BT_STATUS_ACL_BUSY) {
		if (bt_link_info->a2dp_exist) {
			btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 14);
			btc8821a1ant_coex_table_with_type(btcoexist,
							  NORMAL_EXEC, 1);
		} else if (bt_link_info->a2dp_exist &&
			   bt_link_info->pan_exist) {
			btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 22);
			btc8821a1ant_coex_table_with_type(btcoexist,
							  NORMAL_EXEC, 4);
		} else {
			btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 20);
			btc8821a1ant_coex_table_with_type(btcoexist,
							  NORMAL_EXEC, 4);
		}
	} else if ((coex_dm->bt_status == BT_8821A_1ANT_BT_STATUS_SCO_BUSY) ||
		   (BT_8821A_1ANT_BT_STATUS_ACL_SCO_BUSY ==
		    coex_dm->bt_status)) {
		btc8821a1ant_act_bt_sco_hid_only_busy(btcoexist,
				BT_8821A_1ANT_WIFI_STATUS_CONNECTED_SCAN);
	} else {
		btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
	}
}

static
void btc8821a1ant_action_wifi_connected_scan(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	/* power save state */
	btc8821a1ant_power_save_state(btcoexist,
				      BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	/* tdma and coex table */
	if (BT_8821A_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) {
		if (bt_link_info->a2dp_exist) {
			btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 14);
			btc8821a1ant_coex_table_with_type(btcoexist,
							  NORMAL_EXEC, 1);
		} else {
			btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 20);
			btc8821a1ant_coex_table_with_type(btcoexist,
							  NORMAL_EXEC, 4);
		}
	} else if ((coex_dm->bt_status == BT_8821A_1ANT_BT_STATUS_SCO_BUSY) ||
		   (coex_dm->bt_status ==
		    BT_8821A_1ANT_BT_STATUS_ACL_SCO_BUSY)) {
		btc8821a1ant_act_bt_sco_hid_only_busy(btcoexist,
			BT_8821A_1ANT_WIFI_STATUS_CONNECTED_SCAN);
	} else {
		btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
	}
}

static void btc8821a1ant_act_wifi_conn_sp_pkt(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	btc8821a1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
				      0x0, 0x0);

	/* tdma and coex table */
	if ((bt_link_info->sco_exist) || (bt_link_info->hid_exist) ||
	    (bt_link_info->a2dp_exist)) {
		btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 32);
		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
	}

	if ((bt_link_info->hid_exist) && (bt_link_info->a2dp_exist)) {
		btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 14);
		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
	} else if (bt_link_info->pan_exist) {
		btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 20);
		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
	} else {
		btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
	}
}

static void btc8821a1ant_action_wifi_connected(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	bool wifi_busy = false;
	bool scan = false, link = false, roam = false;
	bool under_4way = false;
	bool ap_enable = false;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], CoexForWifiConnect()===>\n");

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS,
			   &under_4way);
	if (under_4way) {
		btc8821a1ant_act_wifi_conn_sp_pkt(btcoexist);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], CoexForWifiConnect(), return for wifi is under 4way<===\n");
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);
	if (scan || link || roam) {
		if (scan)
			btc8821a1ant_action_wifi_connected_scan(btcoexist);
		else
			btc8821a1ant_act_wifi_conn_sp_pkt(btcoexist);

		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], CoexForWifiConnect(), return for wifi is under scan<===\n");
		return;
	}

	/* power save state*/
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE,
			   &ap_enable);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	if (BT_8821A_1ANT_BT_STATUS_ACL_BUSY ==
	    coex_dm->bt_status && !ap_enable &&
	    !btcoexist->bt_link_info.hid_only) {
		if (!wifi_busy && btcoexist->bt_link_info.a2dp_only)
			/* A2DP */
			btc8821a1ant_power_save_state(btcoexist,
						BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		else
			btc8821a1ant_power_save_state(btcoexist, BTC_PS_LPS_ON,
						      0x50, 0x4);
	} else {
		btc8821a1ant_power_save_state(btcoexist,
					      BTC_PS_WIFI_NATIVE,
					      0x0, 0x0);
	}

	/* tdma and coex table */
	if (!wifi_busy) {
		if (BT_8821A_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) {
			btc8821a1ant_act_wifi_con_bt_acl_busy(btcoexist,
				BT_8821A_1ANT_WIFI_STATUS_CONNECTED_IDLE);
		} else if ((BT_8821A_1ANT_BT_STATUS_SCO_BUSY ==
			    coex_dm->bt_status) ||
			   (BT_8821A_1ANT_BT_STATUS_ACL_SCO_BUSY ==
			    coex_dm->bt_status)) {
			btc8821a1ant_act_bt_sco_hid_only_busy(btcoexist,
				BT_8821A_1ANT_WIFI_STATUS_CONNECTED_IDLE);
		} else {
			btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
			btc8821a1ant_coex_table_with_type(btcoexist,
							  NORMAL_EXEC, 2);
		}
	} else {
		if (BT_8821A_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) {
			btc8821a1ant_act_wifi_con_bt_acl_busy(btcoexist,
				BT_8821A_1ANT_WIFI_STATUS_CONNECTED_BUSY);
		} else if ((BT_8821A_1ANT_BT_STATUS_SCO_BUSY ==
			    coex_dm->bt_status) ||
			   (BT_8821A_1ANT_BT_STATUS_ACL_SCO_BUSY ==
			    coex_dm->bt_status)) {
			btc8821a1ant_act_bt_sco_hid_only_busy(btcoexist,
				BT_8821A_1ANT_WIFI_STATUS_CONNECTED_BUSY);
		} else {
			btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
			btc8821a1ant_coex_table_with_type(btcoexist,
							  NORMAL_EXEC, 2);
		}
	}
}

static void btc8821a1ant_run_sw_coex_mech(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 algorithm = 0;

	algorithm = btc8821a1ant_action_algorithm(btcoexist);
	coex_dm->cur_algorithm = algorithm;

	if (!btc8821a1ant_is_common_action(btcoexist)) {
		switch (coex_dm->cur_algorithm) {
		case BT_8821A_1ANT_COEX_ALGO_SCO:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action algorithm = SCO\n");
			btc8821a1ant_action_sco(btcoexist);
			break;
		case BT_8821A_1ANT_COEX_ALGO_HID:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action algorithm = HID\n");
			btc8821a1ant_action_hid(btcoexist);
			break;
		case BT_8821A_1ANT_COEX_ALGO_A2DP:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action algorithm = A2DP\n");
			btc8821a1ant_action_a2dp(btcoexist);
			break;
		case BT_8821A_1ANT_COEX_ALGO_A2DP_PANHS:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action algorithm = A2DP+PAN(HS)\n");
			btc8821a1ant_action_a2dp_pan_hs(btcoexist);
			break;
		case BT_8821A_1ANT_COEX_ALGO_PANEDR:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action algorithm = PAN(EDR)\n");
			btc8821a1ant_action_pan_edr(btcoexist);
			break;
		case BT_8821A_1ANT_COEX_ALGO_PANHS:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action algorithm = HS mode\n");
			btc8821a1ant_action_pan_hs(btcoexist);
			break;
		case BT_8821A_1ANT_COEX_ALGO_PANEDR_A2DP:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action algorithm = PAN+A2DP\n");
			btc8821a1ant_action_pan_edr_a2dp(btcoexist);
			break;
		case BT_8821A_1ANT_COEX_ALGO_PANEDR_HID:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action algorithm = PAN(EDR)+HID\n");
			btc8821a1ant_action_pan_edr_hid(btcoexist);
			break;
		case BT_8821A_1ANT_COEX_ALGO_HID_A2DP_PANEDR:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action algorithm = HID+A2DP+PAN\n");
			btc8821a1ant_action_hid_a2dp_pan_edr(btcoexist);
			break;
		case BT_8821A_1ANT_COEX_ALGO_HID_A2DP:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action algorithm = HID+A2DP\n");
			btc8821a1ant_action_hid_a2dp(btcoexist);
			break;
		default:
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Action algorithm = coexist All Off!!\n");
			/*btc8821a1ant_coex_all_off(btcoexist);*/
			break;
		}
		coex_dm->pre_algorithm = coex_dm->cur_algorithm;
	}
}

static void btc8821a1ant_run_coexist_mechanism(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool wifi_connected = false, bt_hs_on = false;
	bool increase_scan_dev_num = false;
	bool bt_ctrl_agg_buf_size = false;
	u8 agg_buf_size = 5;
	u8 wifi_rssi_state = BTC_RSSI_STATE_HIGH;
	u32 wifi_link_status = 0;
	u32 num_of_wifi_link = 0;
	bool wifi_under_5g = false;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], RunCoexistMechanism()===>\n");

	if (btcoexist->manual_control) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], RunCoexistMechanism(), return for Manual CTRL <===\n");
		return;
	}

	if (btcoexist->stop_coex_dm) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], RunCoexistMechanism(), return for Stop Coex DM <===\n");
		return;
	}

	if (coex_sta->under_ips) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], wifi is under IPS !!!\n");
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);
	if (wifi_under_5g) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], RunCoexistMechanism(), return for 5G <===\n");
		btc8821a1ant_coex_under_5g(btcoexist);
		return;
	}

	if ((BT_8821A_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) ||
	    (BT_8821A_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
	    (BT_8821A_1ANT_BT_STATUS_ACL_SCO_BUSY == coex_dm->bt_status))
		increase_scan_dev_num = true;

	btcoexist->btc_set(btcoexist, BTC_SET_BL_INC_SCAN_DEV_NUM,
			   &increase_scan_dev_num);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
			   &wifi_link_status);
	num_of_wifi_link = wifi_link_status >> 16;
	if ((num_of_wifi_link >= 2) ||
	    (wifi_link_status & WIFI_P2P_GO_CONNECTED)) {
		btc8821a1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);
		btc8821a1ant_limited_rx(btcoexist, NORMAL_EXEC, false,
					bt_ctrl_agg_buf_size, agg_buf_size);
		btc8821a1ant_action_wifi_multi_port(btcoexist);
		return;
	}

	if (!bt_link_info->sco_exist && !bt_link_info->hid_exist) {
		btc8821a1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);
	} else {
		if (wifi_connected) {
			wifi_rssi_state =
				btc8821a1ant_wifi_rssi_state(btcoexist, 1, 2,
							     30, 0);
			if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
				btc8821a1ant_limited_tx(btcoexist,
							NORMAL_EXEC, 1, 1,
							0, 1);
			} else {
				btc8821a1ant_limited_tx(btcoexist,
							NORMAL_EXEC, 1, 1,
							0, 1);
			}
		} else {
			btc8821a1ant_limited_tx(btcoexist, NORMAL_EXEC,
						0, 0, 0, 0);
		}
	}

	if (bt_link_info->sco_exist) {
		bt_ctrl_agg_buf_size = true;
		agg_buf_size = 0x3;
	} else if (bt_link_info->hid_exist) {
		bt_ctrl_agg_buf_size = true;
		agg_buf_size = 0x5;
	} else if (bt_link_info->a2dp_exist || bt_link_info->pan_exist) {
		bt_ctrl_agg_buf_size = true;
		agg_buf_size = 0x8;
	}
	btc8821a1ant_limited_rx(btcoexist, NORMAL_EXEC, false,
				bt_ctrl_agg_buf_size, agg_buf_size);

	btc8821a1ant_run_sw_coex_mech(btcoexist);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	if (coex_sta->c2h_bt_inquiry_page) {
		btc8821a1ant_action_bt_inquiry(btcoexist);
		return;
	} else if (bt_hs_on) {
		btc8821a1ant_action_hs(btcoexist);
		return;
	}

	if (!wifi_connected) {
		bool scan = false, link = false, roam = false;

		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], wifi is non connected-idle !!!\n");

		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);

		if (scan || link || roam) {
			if (scan)
				btc8821a1ant_act_wifi_not_conn_scan(btcoexist);
			else
				btc8821a1ant_action_wifi_not_connected_asso_auth(
					btcoexist);
		} else {
			btc8821a1ant_action_wifi_not_connected(btcoexist);
		}
	} else {
		/* wifi LPS/Busy */
		btc8821a1ant_action_wifi_connected(btcoexist);
	}
}

static void btc8821a1ant_init_coex_dm(struct btc_coexist *btcoexist)
{
	/* force to reset coex mechanism
	 * sw all off
	 */
	btc8821a1ant_sw_mechanism(btcoexist, false);

	btc8821a1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 0);
}

static void btc8821a1ant_init_hw_config(struct btc_coexist *btcoexist,
					bool back_up, bool wifi_only)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 u1_tmp = 0;
	bool wifi_under_5g = false;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], 1Ant Init HW Config!!\n");

	if (wifi_only)
		return;

	if (back_up) {
		coex_dm->backup_arfr_cnt1 = btcoexist->btc_read_4byte(btcoexist,
								      0x430);
		coex_dm->backup_arfr_cnt2 = btcoexist->btc_read_4byte(btcoexist,
								      0x434);
		coex_dm->backup_retry_limit =
			btcoexist->btc_read_2byte(btcoexist, 0x42a);
		coex_dm->backup_ampdu_max_time =
			btcoexist->btc_read_1byte(btcoexist, 0x456);
	}

	/* 0x790[5:0] = 0x5 */
	u1_tmp = btcoexist->btc_read_1byte(btcoexist, 0x790);
	u1_tmp &= 0xc0;
	u1_tmp |= 0x5;
	btcoexist->btc_write_1byte(btcoexist, 0x790, u1_tmp);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);

	/* Antenna config */
	if (wifi_under_5g)
		btc8821a1ant_set_ant_path(btcoexist, BTC_ANT_PATH_BT,
					  true, false);
	else
		btc8821a1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA,
					  true, false);
	/* PTA parameter */
	btc8821a1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 0);

	/* Enable counter statistics
	 * 0x76e[3] =1, WLAN_Act control by PTA
	 */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0xc);
	btcoexist->btc_write_1byte(btcoexist, 0x778, 0x3);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x40, 0x20, 0x1);
}

/**************************************************************
 * extern function start with ex_btc8821a1ant_
 **************************************************************/
void ex_btc8821a1ant_init_hwconfig(struct btc_coexist *btcoexist, bool wifionly)
{
	btc8821a1ant_init_hw_config(btcoexist, true, wifionly);
	btcoexist->auto_report_1ant = true;
}

void ex_btc8821a1ant_init_coex_dm(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], Coex Mechanism Init!!\n");

	btcoexist->stop_coex_dm = false;

	btc8821a1ant_init_coex_dm(btcoexist);

	btc8821a1ant_query_bt_info(btcoexist);
}

void ex_btc8821a1ant_display_coex_info(struct btc_coexist *btcoexist,
				       struct seq_file *m)
{
	struct btc_board_info *board_info = &btcoexist->board_info;
	struct btc_stack_info *stack_info = &btcoexist->stack_info;
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	u8 u1_tmp[4], i, bt_info_ext, ps_tdma_case = 0;
	u16 u2_tmp[4];
	u32 u4_tmp[4];
	bool roam = false, scan = false, link = false, wifi_under_5g = false;
	bool bt_hs_on = false, wifi_busy = false;
	long wifi_rssi = 0, bt_hs_rssi = 0;
	u32 wifi_bw, wifi_traffic_dir;
	u8 wifi_dot11_chnl, wifi_hs_chnl;
	u32 fw_ver = 0, bt_patch_ver = 0;

	seq_puts(m, "\n ============[BT Coexist info]============");

	if (btcoexist->manual_control) {
		seq_puts(m, "\n ============[Under Manual Control]============");
		seq_puts(m, "\n ==========================================");
	}
	if (btcoexist->stop_coex_dm) {
		seq_puts(m, "\n ============[Coex is STOPPED]============");
		seq_puts(m, "\n ==========================================");
	}

	seq_printf(m, "\n %-35s = %d/ %d/ %d",
		   "Ant PG Num/ Ant Mech/ Ant Pos:",
		   board_info->pg_ant_num,
		   board_info->btdm_ant_num,
		   board_info->btdm_ant_pos);

	seq_printf(m, "\n %-35s = %s / %d", "BT stack/ hci ext ver",
		   ((stack_info->profile_notified) ? "Yes" : "No"),
		   stack_info->hci_version);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER,
			   &bt_patch_ver);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
	seq_printf(m, "\n %-35s = %d_%x/ 0x%x/ 0x%x(%d)",
		   "CoexVer/ FwVer/ PatchVer",
		   glcoex_ver_date_8821a_1ant,
		   glcoex_ver_8821a_1ant,
		   fw_ver, bt_patch_ver,
		   bt_patch_ver);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION,
			   &bt_hs_on);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_DOT11_CHNL,
			   &wifi_dot11_chnl);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_HS_CHNL,
			   &wifi_hs_chnl);
	seq_printf(m, "\n %-35s = %d / %d(%d)",
		   "Dot11 channel / HsChnl(HsMode)",
		   wifi_dot11_chnl, wifi_hs_chnl, bt_hs_on);

	seq_printf(m, "\n %-35s = %3ph ",
		   "H2C Wifi inform bt chnl Info",
		   coex_dm->wifi_chnl_info);

	btcoexist->btc_get(btcoexist, BTC_GET_S4_WIFI_RSSI, &wifi_rssi);
	btcoexist->btc_get(btcoexist, BTC_GET_S4_HS_RSSI, &bt_hs_rssi);
	seq_printf(m, "\n %-35s = %d/ %d", "Wifi rssi/ HS rssi",
		   (int)wifi_rssi, (int)bt_hs_rssi);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);
	seq_printf(m, "\n %-35s = %d/ %d/ %d ", "Wifi link/ roam/ scan",
		   link, roam, scan);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G,
			   &wifi_under_5g);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW,
			   &wifi_bw);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY,
			   &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION,
			   &wifi_traffic_dir);
	seq_printf(m, "\n %-35s = %s / %s/ %s ", "Wifi status",
		   (wifi_under_5g ? "5G" : "2.4G"),
		   ((wifi_bw == BTC_WIFI_BW_LEGACY) ? "Legacy" :
		   (((wifi_bw == BTC_WIFI_BW_HT40) ? "HT40" : "HT20"))),
		   ((!wifi_busy) ? "idle" :
		   ((wifi_traffic_dir == BTC_WIFI_TRAFFIC_TX) ?
		   "uplink" : "downlink")));
	seq_printf(m, "\n %-35s = [%s/ %d/ %d] ",
		   "BT [status/ rssi/ retryCnt]",
		   ((coex_sta->bt_disabled) ? ("disabled") :
		   ((coex_sta->c2h_bt_inquiry_page) ? ("inquiry/page scan") :
		   ((BT_8821A_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
		     coex_dm->bt_status) ?
		   "non-connected idle" :
		   ((BT_8821A_1ANT_BT_STATUS_CONNECTED_IDLE ==
		     coex_dm->bt_status) ?
		   "connected-idle" : "busy")))),
		   coex_sta->bt_rssi, coex_sta->bt_retry_cnt);

	seq_printf(m, "\n %-35s = %d / %d / %d / %d", "SCO/HID/PAN/A2DP",
		   bt_link_info->sco_exist,
		   bt_link_info->hid_exist,
		   bt_link_info->pan_exist,
		   bt_link_info->a2dp_exist);
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_BT_LINK_INFO, m);

	bt_info_ext = coex_sta->bt_info_ext;
	seq_printf(m, "\n %-35s = %s",
		   "BT Info A2DP rate",
		   (bt_info_ext & BIT0) ?
		   "Basic rate" : "EDR rate");

	for (i = 0; i < BT_INFO_SRC_8821A_1ANT_MAX; i++) {
		if (coex_sta->bt_info_c2h_cnt[i]) {
			seq_printf(m, "\n %-35s = %7ph(%d)",
				   glbt_info_src_8821a_1ant[i],
				   coex_sta->bt_info_c2h[i],
				   coex_sta->bt_info_c2h_cnt[i]);
		}
	}
	seq_printf(m, "\n %-35s = %s/%s, (0x%x/0x%x)",
		   "PS state, IPS/LPS, (lps/rpwm)",
		   ((coex_sta->under_ips ? "IPS ON" : "IPS OFF")),
		   ((coex_sta->under_lps ? "LPS ON" : "LPS OFF")),
		   btcoexist->bt_info.lps_val,
		   btcoexist->bt_info.rpwm_val);
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_FW_PWR_MODE_CMD, m);

	if (!btcoexist->manual_control) {
		/* Sw mechanism*/
		seq_printf(m, "\n %-35s",
			   "============[Sw mechanism]============");

		seq_printf(m, "\n %-35s = %d", "SM[LowPenaltyRA]",
			   coex_dm->cur_low_penalty_ra);

		seq_printf(m, "\n %-35s = %s/ %s/ %d ",
			   "DelBA/ BtCtrlAgg/ AggSize",
			   (btcoexist->bt_info.reject_agg_pkt ? "Yes" : "No"),
			   (btcoexist->bt_info.bt_ctrl_buf_size ? "Yes" : "No"),
			   btcoexist->bt_info.agg_buf_size);
		seq_printf(m, "\n %-35s = 0x%x ", "Rate Mask",
			   btcoexist->bt_info.ra_mask);

		/* Fw mechanism */
		seq_printf(m, "\n %-35s",
			   "============[Fw mechanism]============");

		ps_tdma_case = coex_dm->cur_ps_tdma;
		seq_printf(m, "\n %-35s = %5ph case-%d (auto:%d)",
			   "PS TDMA",
			   coex_dm->ps_tdma_para,
			   ps_tdma_case,
			   coex_dm->auto_tdma_adjust);

		seq_printf(m, "\n %-35s = 0x%x ",
			   "Latest error condition(should be 0)",
			   coex_dm->error_condition);

		seq_printf(m, "\n %-35s = %d ", "IgnWlanAct",
			   coex_dm->cur_ignore_wlan_act);
	}

	/* Hw setting */
	seq_printf(m, "\n %-35s", "============[Hw setting]============");

	seq_printf(m, "\n %-35s = 0x%x/0x%x/0x%x/0x%x",
		   "backup ARFR1/ARFR2/RL/AMaxTime",
		   coex_dm->backup_arfr_cnt1,
		   coex_dm->backup_arfr_cnt2,
		   coex_dm->backup_retry_limit,
		   coex_dm->backup_ampdu_max_time);

	u4_tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x430);
	u4_tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x434);
	u2_tmp[0] = btcoexist->btc_read_2byte(btcoexist, 0x42a);
	u1_tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x456);
	seq_printf(m, "\n %-35s = 0x%x/0x%x/0x%x/0x%x",
		   "0x430/0x434/0x42a/0x456",
		   u4_tmp[0], u4_tmp[1], u2_tmp[0], u1_tmp[0]);

	u1_tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x778);
	u4_tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xc58);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x", "0x778/ 0xc58[29:25]",
		   u1_tmp[0], (u4_tmp[0] & 0x3e000000) >> 25);

	u1_tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x8db);
	seq_printf(m, "\n %-35s = 0x%x", "0x8db[6:5]",
		   ((u1_tmp[0] & 0x60) >> 5));

	u1_tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x975);
	u4_tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xcb4);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0xcb4[29:28]/0xcb4[7:0]/0x974[9:8]",
		   (u4_tmp[0] & 0x30000000) >> 28,
		    u4_tmp[0] & 0xff,
		    u1_tmp[0] & 0x3);

	u1_tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x40);
	u4_tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x4c);
	u1_tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x64);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x40/0x4c[24:23]/0x64[0]",
		   u1_tmp[0], ((u4_tmp[0] & 0x01800000) >> 23),
		   u1_tmp[1] & 0x1);

	u4_tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x550);
	u1_tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x522);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x", "0x550(bcn ctrl)/0x522",
		   u4_tmp[0], u1_tmp[0]);

	u4_tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xc50);
	seq_printf(m, "\n %-35s = 0x%x", "0xc50(dig)",
		   u4_tmp[0] & 0xff);

	u4_tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xf48);
	u1_tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0xa5d);
	u1_tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0xa5c);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x", "OFDM-FA/ CCK-FA",
		   u4_tmp[0], (u1_tmp[0] << 8) + u1_tmp[1]);

	u4_tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x6c0);
	u4_tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x6c4);
	u4_tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0x6c8);
	u1_tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x6cc);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x",
		   "0x6c0/0x6c4/0x6c8/0x6cc(coexTable)",
		   u4_tmp[0], u4_tmp[1], u4_tmp[2], u1_tmp[0]);

	seq_printf(m, "\n %-35s = %d/ %d", "0x770(high-pri rx/tx)",
		   coex_sta->high_priority_rx, coex_sta->high_priority_tx);
	seq_printf(m, "\n %-35s = %d/ %d", "0x774(low-pri rx/tx)",
		   coex_sta->low_priority_rx, coex_sta->low_priority_tx);
	if (btcoexist->auto_report_1ant)
		btc8821a1ant_monitor_bt_ctr(btcoexist);
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_COEX_STATISTICS, m);
}

void ex_btc8821a1ant_ips_notify(struct btc_coexist *btcoexist, u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	bool wifi_under_5g = false;

	if (btcoexist->manual_control || btcoexist->stop_coex_dm)
		return;
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);
	if (wifi_under_5g) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], RunCoexistMechanism(), return for 5G <===\n");
		btc8821a1ant_coex_under_5g(btcoexist);
		return;
	}

	if (BTC_IPS_ENTER == type) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], IPS ENTER notify\n");
		coex_sta->under_ips = true;
		btc8821a1ant_set_ant_path(btcoexist,
					  BTC_ANT_PATH_BT, false, true);
		/* set PTA control */
		btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);
		btc8821a1ant_coex_table_with_type(btcoexist,
						  NORMAL_EXEC, 0);
	} else if (BTC_IPS_LEAVE == type) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], IPS LEAVE notify\n");
		coex_sta->under_ips = false;

		btc8821a1ant_init_hw_config(btcoexist, false, false);
		btc8821a1ant_init_coex_dm(btcoexist);
		btc8821a1ant_query_bt_info(btcoexist);
	}
}

void ex_btc8821a1ant_lps_notify(struct btc_coexist *btcoexist, u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	if (btcoexist->manual_control || btcoexist->stop_coex_dm)
		return;

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

void ex_btc8821a1ant_scan_notify(struct btc_coexist *btcoexist, u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	bool wifi_connected = false, bt_hs_on = false;
	bool bt_ctrl_agg_buf_size = false;
	bool wifi_under_5g = false;
	u32 wifi_link_status = 0;
	u32 num_of_wifi_link = 0;
	u8 agg_buf_size = 5;

	if (btcoexist->manual_control || btcoexist->stop_coex_dm)
		return;
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);
	if (wifi_under_5g) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], RunCoexistMechanism(), return for 5G <===\n");
		btc8821a1ant_coex_under_5g(btcoexist);
		return;
	}

	if (type == BTC_SCAN_START) {
		coex_sta->wifi_is_high_pri_task = true;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], SCAN START notify\n");

		/* Force antenna setup for no scan result issue */
		btc8821a1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 8);
	} else {
		coex_sta->wifi_is_high_pri_task = false;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], SCAN FINISH notify\n");
	}

	if (coex_sta->bt_disabled)
		return;

	btcoexist->btc_get(btcoexist,
		 BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist,
		 BTC_GET_BL_WIFI_CONNECTED, &wifi_connected);

	btc8821a1ant_query_bt_info(btcoexist);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
			   &wifi_link_status);
	num_of_wifi_link = wifi_link_status >> 16;
	if (num_of_wifi_link >= 2) {
		btc8821a1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);
		btc8821a1ant_limited_rx(btcoexist, NORMAL_EXEC, false,
					bt_ctrl_agg_buf_size, agg_buf_size);
		btc8821a1ant_action_wifi_multi_port(btcoexist);
		return;
	}

	if (coex_sta->c2h_bt_inquiry_page) {
		btc8821a1ant_action_bt_inquiry(btcoexist);
		return;
	} else if (bt_hs_on) {
		btc8821a1ant_action_hs(btcoexist);
		return;
	}

	if (BTC_SCAN_START == type) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], SCAN START notify\n");
		if (!wifi_connected) {
			/* non-connected scan */
			btc8821a1ant_act_wifi_not_conn_scan(btcoexist);
		} else {
			/* wifi is connected */
			btc8821a1ant_action_wifi_connected_scan(btcoexist);
		}
	} else if (BTC_SCAN_FINISH == type) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], SCAN FINISH notify\n");
		if (!wifi_connected) {
			/* non-connected scan */
			btc8821a1ant_action_wifi_not_connected(btcoexist);
		} else {
			btc8821a1ant_action_wifi_connected(btcoexist);
		}
	}
}

void ex_btc8821a1ant_connect_notify(struct btc_coexist *btcoexist, u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	bool wifi_connected = false, bt_hs_on = false;
	u32 wifi_link_status = 0;
	u32 num_of_wifi_link = 0;
	bool bt_ctrl_agg_buf_size = false;
	bool wifi_under_5g = false;
	u8 agg_buf_size = 5;

	if (btcoexist->manual_control || btcoexist->stop_coex_dm ||
	    coex_sta->bt_disabled)
		return;
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);
	if (wifi_under_5g) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], RunCoexistMechanism(), return for 5G <===\n");
		btc8821a1ant_coex_under_5g(btcoexist);
		return;
	}

	if (type == BTC_ASSOCIATE_START) {
		coex_sta->wifi_is_high_pri_task = true;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], CONNECT START notify\n");
		coex_dm->arp_cnt = 0;
	} else {
		coex_sta->wifi_is_high_pri_task = false;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], CONNECT FINISH notify\n");
		coex_dm->arp_cnt = 0;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
			   &wifi_link_status);
	num_of_wifi_link = wifi_link_status >> 16;
	if (num_of_wifi_link >= 2) {
		btc8821a1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);
		btc8821a1ant_limited_rx(btcoexist, NORMAL_EXEC, false,
					bt_ctrl_agg_buf_size, agg_buf_size);
		btc8821a1ant_action_wifi_multi_port(btcoexist);
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	if (coex_sta->c2h_bt_inquiry_page) {
		btc8821a1ant_action_bt_inquiry(btcoexist);
		return;
	} else if (bt_hs_on) {
		btc8821a1ant_action_hs(btcoexist);
		return;
	}

	if (BTC_ASSOCIATE_START == type) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], CONNECT START notify\n");
		btc8821a1ant_act_wifi_not_conn_scan(btcoexist);
	} else if (BTC_ASSOCIATE_FINISH == type) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], CONNECT FINISH notify\n");

		btcoexist->btc_get(btcoexist,
			 BTC_GET_BL_WIFI_CONNECTED, &wifi_connected);
		if (!wifi_connected) {
			/* non-connected scan */
			btc8821a1ant_action_wifi_not_connected(btcoexist);
		} else {
			btc8821a1ant_action_wifi_connected(btcoexist);
		}
	}
}

void ex_btc8821a1ant_media_status_notify(struct btc_coexist *btcoexist,
					 u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 h2c_parameter[3] = {0};
	u32 wifi_bw;
	u8 wifi_central_chnl;
	bool wifi_under_5g = false;

	if (btcoexist->manual_control || btcoexist->stop_coex_dm ||
	    coex_sta->bt_disabled)
		return;
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);
	if (wifi_under_5g) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], RunCoexistMechanism(), return for 5G <===\n");
		btc8821a1ant_coex_under_5g(btcoexist);
		return;
	}

	if (BTC_MEDIA_CONNECT == type) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], MEDIA connect notify\n");
	} else {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], MEDIA disconnect notify\n");
		coex_dm->arp_cnt = 0;
	}

	/* only 2.4G we need to inform bt the chnl mask */
	btcoexist->btc_get(btcoexist,
			   BTC_GET_U1_WIFI_CENTRAL_CHNL,
			   &wifi_central_chnl);
	if ((type == BTC_MEDIA_CONNECT) &&
	    (wifi_central_chnl <= 14)) {
		h2c_parameter[0] = 0x0;
		h2c_parameter[1] = wifi_central_chnl;
		btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
		if (wifi_bw == BTC_WIFI_BW_HT40)
			h2c_parameter[2] = 0x30;
		else
			h2c_parameter[2] = 0x20;
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

void ex_btc8821a1ant_special_packet_notify(struct btc_coexist *btcoexist,
					   u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	bool bt_hs_on = false;
	bool bt_ctrl_agg_buf_size = false;
	bool wifi_under_5g = false;
	u32 wifi_link_status = 0;
	u32 num_of_wifi_link = 0;
	u8 agg_buf_size = 5;

	if (btcoexist->manual_control || btcoexist->stop_coex_dm ||
	    coex_sta->bt_disabled)
		return;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);
	if (wifi_under_5g) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], RunCoexistMechanism(), return for 5G <===\n");
		btc8821a1ant_coex_under_5g(btcoexist);
		return;
	}

	if (type == BTC_PACKET_DHCP || type == BTC_PACKET_EAPOL ||
	    type == BTC_PACKET_ARP) {
		coex_sta->wifi_is_high_pri_task = true;

		if (type == BTC_PACKET_ARP) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], specific Packet ARP notify\n");
		} else {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], specific Packet DHCP or EAPOL notify\n");
		}
	} else {
		coex_sta->wifi_is_high_pri_task = false;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], specific Packet [Type = %d] notify\n",
			 type);
	}

	coex_sta->special_pkt_period_cnt = 0;

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
			   &wifi_link_status);
	num_of_wifi_link = wifi_link_status >> 16;
	if (num_of_wifi_link >= 2) {
		btc8821a1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);
		btc8821a1ant_limited_rx(btcoexist, NORMAL_EXEC, false,
					bt_ctrl_agg_buf_size, agg_buf_size);
		btc8821a1ant_action_wifi_multi_port(btcoexist);
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	if (coex_sta->c2h_bt_inquiry_page) {
		btc8821a1ant_action_bt_inquiry(btcoexist);
		return;
	} else if (bt_hs_on) {
		btc8821a1ant_action_hs(btcoexist);
		return;
	}

	if (type == BTC_PACKET_DHCP || type == BTC_PACKET_EAPOL ||
	    type == BTC_PACKET_ARP) {
		if (type == BTC_PACKET_ARP) {
			coex_dm->arp_cnt++;
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], ARP Packet Count = %d\n",
				 coex_dm->arp_cnt);
			if (coex_dm->arp_cnt >= 10)
				/* if APR PKT > 10 after connect, do not go to
				 * btc8821a1ant_act_wifi_conn_sp_pkt
				 */
				return;
		}

		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], special Packet(%d) notify\n", type);
		btc8821a1ant_act_wifi_conn_sp_pkt(btcoexist);
	}
}

void ex_btc8821a1ant_bt_info_notify(struct btc_coexist *btcoexist,
				    u8 *tmp_buf, u8 length)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 i;
	u8 bt_info = 0;
	u8 rsp_source = 0;
	bool wifi_connected = false;
	bool bt_busy = false;
	bool wifi_under_5g = false;

	coex_sta->c2h_bt_info_req_sent = false;

	btcoexist->btc_get(btcoexist,
		 BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);

	rsp_source = tmp_buf[0] & 0xf;
	if (rsp_source >= BT_INFO_SRC_8821A_1ANT_MAX)
		rsp_source = BT_INFO_SRC_8821A_1ANT_WIFI_FW;
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

	if (BT_INFO_SRC_8821A_1ANT_WIFI_FW != rsp_source) {
		/* [3:0] */
		coex_sta->bt_retry_cnt =
			coex_sta->bt_info_c2h[rsp_source][2] & 0xf;

		coex_sta->bt_rssi =
			coex_sta->bt_info_c2h[rsp_source][3] * 2 + 10;

		coex_sta->bt_info_ext = coex_sta->bt_info_c2h[rsp_source][4];

		coex_sta->bt_tx_rx_mask =
			(coex_sta->bt_info_c2h[rsp_source][2] & 0x40);
		btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TX_RX_MASK,
				   &coex_sta->bt_tx_rx_mask);
		if (!coex_sta->bt_tx_rx_mask) {
			/* BT into is responded by BT FW and BT RF REG 0x3C !=
			 * 0x15 => Need to switch BT TRx Mask
			 */
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], Switch BT TRx Mask since BT RF REG 0x3C != 0x15\n");
			btcoexist->btc_set_bt_reg(btcoexist, BTC_BT_REG_RF,
						  0x3c, 0x15);
		}

		/* Here we need to resend some wifi info to BT
		 * because bt is reset and lost the info
		 */
		if (coex_sta->bt_info_ext & BIT1) {
			RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				 "[BTCoex], BT ext info bit1 check, send wifi BW&Chnl to BT!!\n");
			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
					   &wifi_connected);
			if (wifi_connected) {
				ex_btc8821a1ant_media_status_notify(btcoexist,
							       BTC_MEDIA_CONNECT);
			} else {
				ex_btc8821a1ant_media_status_notify(btcoexist,
							       BTC_MEDIA_DISCONNECT);
			}
		}

		if ((coex_sta->bt_info_ext & BIT3) && !wifi_under_5g) {
			if (!btcoexist->manual_control &&
			    !btcoexist->stop_coex_dm) {
				RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					 "[BTCoex], BT ext info bit3 check, set BT NOT to ignore Wlan active!!\n");
				btc8821a1ant_ignore_wlan_act(btcoexist,
							     FORCE_EXEC,
							     false);
			}
		}
	}

	/* check BIT2 first ==> check if bt is under inquiry or page scan */
	if (bt_info & BT_INFO_8821A_1ANT_B_INQ_PAGE)
		coex_sta->c2h_bt_inquiry_page = true;
	else
		coex_sta->c2h_bt_inquiry_page = false;

	/* set link exist status */
	if (!(bt_info & BT_INFO_8821A_1ANT_B_CONNECTION)) {
		coex_sta->bt_link_exist = false;
		coex_sta->pan_exist = false;
		coex_sta->a2dp_exist = false;
		coex_sta->hid_exist = false;
		coex_sta->sco_exist = false;
	} else {
		/* connection exists */
		coex_sta->bt_link_exist = true;
		if (bt_info & BT_INFO_8821A_1ANT_B_FTP)
			coex_sta->pan_exist = true;
		else
			coex_sta->pan_exist = false;
		if (bt_info & BT_INFO_8821A_1ANT_B_A2DP)
			coex_sta->a2dp_exist = true;
		else
			coex_sta->a2dp_exist = false;
		if (bt_info & BT_INFO_8821A_1ANT_B_HID)
			coex_sta->hid_exist = true;
		else
			coex_sta->hid_exist = false;
		if (bt_info & BT_INFO_8821A_1ANT_B_SCO_ESCO)
			coex_sta->sco_exist = true;
		else
			coex_sta->sco_exist = false;
	}

	btc8821a1ant_update_bt_link_info(btcoexist);

	/* mask profile bit for connect-ilde identification
	 * (for CSR case: A2DP idle --> 0x41)
	 */
	bt_info = bt_info & 0x1f;

	if (!(bt_info & BT_INFO_8821A_1ANT_B_CONNECTION)) {
		coex_dm->bt_status = BT_8821A_1ANT_BT_STATUS_NON_CONNECTED_IDLE;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], BtInfoNotify(), BT Non-Connected idle!!!\n");
	} else if (bt_info == BT_INFO_8821A_1ANT_B_CONNECTION) {
		/* connection exists but no busy */
		coex_dm->bt_status = BT_8821A_1ANT_BT_STATUS_CONNECTED_IDLE;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], BtInfoNotify(), BT Connected-idle!!!\n");
	} else if ((bt_info&BT_INFO_8821A_1ANT_B_SCO_ESCO) ||
		(bt_info & BT_INFO_8821A_1ANT_B_SCO_BUSY)) {
		coex_dm->bt_status = BT_8821A_1ANT_BT_STATUS_SCO_BUSY;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], BtInfoNotify(), BT SCO busy!!!\n");
	} else if (bt_info & BT_INFO_8821A_1ANT_B_ACL_BUSY) {
		if (BT_8821A_1ANT_BT_STATUS_ACL_BUSY != coex_dm->bt_status)
			coex_dm->auto_tdma_adjust = false;
		coex_dm->bt_status = BT_8821A_1ANT_BT_STATUS_ACL_BUSY;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], BtInfoNotify(), BT ACL busy!!!\n");
	} else {
		coex_dm->bt_status = BT_8821A_1ANT_BT_STATUS_MAX;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], BtInfoNotify(), BT Non-Defined state!!!\n");
	}

	if ((BT_8821A_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) ||
	    (BT_8821A_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
	    (BT_8821A_1ANT_BT_STATUS_ACL_SCO_BUSY == coex_dm->bt_status))
		bt_busy = true;
	else
		bt_busy = false;
	btcoexist->btc_set(btcoexist,
			   BTC_SET_BL_BT_TRAFFIC_BUSY, &bt_busy);

	btc8821a1ant_run_coexist_mechanism(btcoexist);
}

void ex_btc8821a1ant_halt_notify(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	bool wifi_under_5g = false;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], Halt notify\n");
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);
	if (wifi_under_5g) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], RunCoexistMechanism(), return for 5G <===\n");
		btc8821a1ant_coex_under_5g(btcoexist);
		return;
	}


	btcoexist->stop_coex_dm = true;

	btc8821a1ant_set_ant_path(btcoexist, BTC_ANT_PATH_BT, false, true);
	btc8821a1ant_ignore_wlan_act(btcoexist, FORCE_EXEC, true);

	btc8821a1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
	btc8821a1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 0);

	ex_btc8821a1ant_media_status_notify(btcoexist, BTC_MEDIA_DISCONNECT);
}

void ex_btc8821a1ant_pnp_notify(struct btc_coexist *btcoexist, u8 pnp_state)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	bool wifi_under_5g = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);
	if (wifi_under_5g) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], RunCoexistMechanism(), return for 5G <===\n");
		btc8821a1ant_coex_under_5g(btcoexist);
		return;
	}

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], Pnp notify\n");

	if (BTC_WIFI_PNP_SLEEP == pnp_state) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], Pnp notify to SLEEP\n");
		/* BT should clear UnderIPS/UnderLPS state to avoid mismatch
		 * state after wakeup.
		 */
		coex_sta->under_ips = false;
		coex_sta->under_lps = false;
		btcoexist->stop_coex_dm = true;
		btc8821a1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					      0x0, 0x0);
		btc8821a1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);
		btc8821a1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
		btc8821a1ant_set_ant_path(btcoexist, BTC_ANT_PATH_BT, false,
					  true);
	} else if (BTC_WIFI_PNP_WAKE_UP == pnp_state) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], Pnp notify to WAKE UP\n");
		btcoexist->stop_coex_dm = false;
		btc8821a1ant_init_hw_config(btcoexist, false, false);
		btc8821a1ant_init_coex_dm(btcoexist);
		btc8821a1ant_query_bt_info(btcoexist);
	}
}

void ex_btc8821a1ant_periodical(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	static u8 dis_ver_info_cnt;
	u32 fw_ver = 0, bt_patch_ver = 0;
	struct btc_board_info *board_info = &btcoexist->board_info;
	struct btc_stack_info *stack_info = &btcoexist->stack_info;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "[BTCoex], ==========================Periodical===========================\n");

	if (dis_ver_info_cnt <= 5) {
		dis_ver_info_cnt += 1;
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], ****************************************************************\n");
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], Ant PG Num/ Ant Mech/ Ant Pos = %d/ %d/ %d\n",
			      board_info->pg_ant_num,
			      board_info->btdm_ant_num,
			      board_info->btdm_ant_pos);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], BT stack/ hci ext ver = %s / %d\n",
			      stack_info->profile_notified ? "Yes" : "No",
			      stack_info->hci_version);
		btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER,
				   &bt_patch_ver);
		btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], CoexVer/ FwVer/ PatchVer = %d_%x/ 0x%x/ 0x%x(%d)\n",
			      glcoex_ver_date_8821a_1ant,
			      glcoex_ver_8821a_1ant,
			      fw_ver, bt_patch_ver,
			      bt_patch_ver);
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], ****************************************************************\n");
	}

	if (!btcoexist->auto_report_1ant) {
		btc8821a1ant_query_bt_info(btcoexist);
		btc8821a1ant_monitor_bt_ctr(btcoexist);
	} else {
		coex_sta->special_pkt_period_cnt++;
	}
}
