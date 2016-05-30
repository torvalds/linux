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
 * This file is for RTL8192E Co-exist mechanism
 *
 * History
 * 2012/11/15 Cosa first check in.
 *
 **************************************************************/

/**************************************************************
 *   include files
 **************************************************************/
#include "halbt_precomp.h"
/**************************************************************
 *   Global variables, these are static variables
 **************************************************************/
static struct coex_dm_8192e_2ant glcoex_dm_8192e_2ant;
static struct coex_dm_8192e_2ant *coex_dm = &glcoex_dm_8192e_2ant;
static struct coex_sta_8192e_2ant glcoex_sta_8192e_2ant;
static struct coex_sta_8192e_2ant *coex_sta = &glcoex_sta_8192e_2ant;

static const char *const GLBtInfoSrc8192e2Ant[] = {
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

static u32 glcoex_ver_date_8192e_2ant = 20130902;
static u32 glcoex_ver_8192e_2ant = 0x34;

/**************************************************************
 *   local function proto type if needed
 **************************************************************/
/**************************************************************
 *   local function start with halbtc8192e2ant_
 **************************************************************/
static u8 halbtc8192e2ant_btrssi_state(u8 level_num, u8 rssi_thresh,
				       u8 rssi_thresh1)
{
	int btrssi = 0;
	u8 btrssi_state = coex_sta->pre_bt_rssi_state;

	btrssi = coex_sta->bt_rssi;

	if (level_num == 2) {
		if ((coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_STAY_LOW)) {
			btc_alg_dbg(ALGO_BT_RSSI_STATE,
				    "BT Rssi pre state = LOW\n");
			if (btrssi >= (rssi_thresh +
				       BTC_RSSI_COEX_THRESH_TOL_8192E_2ANT)) {
				btrssi_state = BTC_RSSI_STATE_HIGH;
				btc_alg_dbg(ALGO_BT_RSSI_STATE,
					    "BT Rssi state switch to High\n");
			} else {
				btrssi_state = BTC_RSSI_STATE_STAY_LOW;
				btc_alg_dbg(ALGO_BT_RSSI_STATE,
					    "BT Rssi state stay at Low\n");
			}
		} else {
			btc_alg_dbg(ALGO_BT_RSSI_STATE,
				    "BT Rssi pre state = HIGH\n");
			if (btrssi < rssi_thresh) {
				btrssi_state = BTC_RSSI_STATE_LOW;
				btc_alg_dbg(ALGO_BT_RSSI_STATE,
					    "BT Rssi state switch to Low\n");
			} else {
				btrssi_state = BTC_RSSI_STATE_STAY_HIGH;
				btc_alg_dbg(ALGO_BT_RSSI_STATE,
					    "BT Rssi state stay at High\n");
			}
		}
	} else if (level_num == 3) {
		if (rssi_thresh > rssi_thresh1) {
			btc_alg_dbg(ALGO_BT_RSSI_STATE,
				    "BT Rssi thresh error!!\n");
			return coex_sta->pre_bt_rssi_state;
		}

		if ((coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_STAY_LOW)) {
			btc_alg_dbg(ALGO_BT_RSSI_STATE,
				    "BT Rssi pre state = LOW\n");
			if (btrssi >= (rssi_thresh +
				      BTC_RSSI_COEX_THRESH_TOL_8192E_2ANT)) {
				btrssi_state = BTC_RSSI_STATE_MEDIUM;
				btc_alg_dbg(ALGO_BT_RSSI_STATE,
					    "BT Rssi state switch to Medium\n");
			} else {
				btrssi_state = BTC_RSSI_STATE_STAY_LOW;
				btc_alg_dbg(ALGO_BT_RSSI_STATE,
					    "BT Rssi state stay at Low\n");
			}
		} else if ((coex_sta->pre_bt_rssi_state ==
			    BTC_RSSI_STATE_MEDIUM) ||
			   (coex_sta->pre_bt_rssi_state ==
			    BTC_RSSI_STATE_STAY_MEDIUM)) {
			btc_alg_dbg(ALGO_BT_RSSI_STATE,
				    "[BTCoex], BT Rssi pre state = MEDIUM\n");
			if (btrssi >= (rssi_thresh1 +
				       BTC_RSSI_COEX_THRESH_TOL_8192E_2ANT)) {
				btrssi_state = BTC_RSSI_STATE_HIGH;
				btc_alg_dbg(ALGO_BT_RSSI_STATE,
					    "BT Rssi state switch to High\n");
			} else if (btrssi < rssi_thresh) {
				btrssi_state = BTC_RSSI_STATE_LOW;
				btc_alg_dbg(ALGO_BT_RSSI_STATE,
					    "BT Rssi state switch to Low\n");
			} else {
				btrssi_state = BTC_RSSI_STATE_STAY_MEDIUM;
				btc_alg_dbg(ALGO_BT_RSSI_STATE,
					    "BT Rssi state stay at Medium\n");
			}
		} else {
			btc_alg_dbg(ALGO_BT_RSSI_STATE,
				    "BT Rssi pre state = HIGH\n");
			if (btrssi < rssi_thresh1) {
				btrssi_state = BTC_RSSI_STATE_MEDIUM;
				btc_alg_dbg(ALGO_BT_RSSI_STATE,
					    "BT Rssi state switch to Medium\n");
			} else {
				btrssi_state = BTC_RSSI_STATE_STAY_HIGH;
				btc_alg_dbg(ALGO_BT_RSSI_STATE,
					    "BT Rssi state stay at High\n");
			}
		}
	}

	coex_sta->pre_bt_rssi_state = btrssi_state;

	return btrssi_state;
}

static u8 halbtc8192e2ant_wifirssi_state(struct btc_coexist *btcoexist,
					 u8 index, u8 level_num, u8 rssi_thresh,
					 u8 rssi_thresh1)
{
	int wifirssi = 0;
	u8 wifirssi_state = coex_sta->pre_wifi_rssi_state[index];

	btcoexist->btc_get(btcoexist, BTC_GET_S4_WIFI_RSSI, &wifirssi);

	if (level_num == 2) {
		if ((coex_sta->pre_wifi_rssi_state[index] ==
		     BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_wifi_rssi_state[index] ==
		     BTC_RSSI_STATE_STAY_LOW)) {
			if (wifirssi >= (rssi_thresh +
					 BTC_RSSI_COEX_THRESH_TOL_8192E_2ANT)) {
				wifirssi_state = BTC_RSSI_STATE_HIGH;
				btc_alg_dbg(ALGO_WIFI_RSSI_STATE,
					    "wifi RSSI state switch to High\n");
			} else {
				wifirssi_state = BTC_RSSI_STATE_STAY_LOW;
				btc_alg_dbg(ALGO_WIFI_RSSI_STATE,
					    "wifi RSSI state stay at Low\n");
			}
		} else {
			if (wifirssi < rssi_thresh) {
				wifirssi_state = BTC_RSSI_STATE_LOW;
				btc_alg_dbg(ALGO_WIFI_RSSI_STATE,
					    "wifi RSSI state switch to Low\n");
			} else {
				wifirssi_state = BTC_RSSI_STATE_STAY_HIGH;
				btc_alg_dbg(ALGO_WIFI_RSSI_STATE,
					    "wifi RSSI state stay at High\n");
			}
		}
	} else if (level_num == 3) {
		if (rssi_thresh > rssi_thresh1) {
			btc_alg_dbg(ALGO_WIFI_RSSI_STATE,
				    "wifi RSSI thresh error!!\n");
			return coex_sta->pre_wifi_rssi_state[index];
		}

		if ((coex_sta->pre_wifi_rssi_state[index] ==
		     BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_wifi_rssi_state[index] ==
		     BTC_RSSI_STATE_STAY_LOW)) {
			if (wifirssi >= (rssi_thresh +
					 BTC_RSSI_COEX_THRESH_TOL_8192E_2ANT)) {
				wifirssi_state = BTC_RSSI_STATE_MEDIUM;
				btc_alg_dbg(ALGO_WIFI_RSSI_STATE,
					    "wifi RSSI state switch to Medium\n");
			} else {
				wifirssi_state = BTC_RSSI_STATE_STAY_LOW;
				btc_alg_dbg(ALGO_WIFI_RSSI_STATE,
					    "wifi RSSI state stay at Low\n");
			}
		} else if ((coex_sta->pre_wifi_rssi_state[index] ==
			    BTC_RSSI_STATE_MEDIUM) ||
			   (coex_sta->pre_wifi_rssi_state[index] ==
			    BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (wifirssi >= (rssi_thresh1 +
					 BTC_RSSI_COEX_THRESH_TOL_8192E_2ANT)) {
				wifirssi_state = BTC_RSSI_STATE_HIGH;
				btc_alg_dbg(ALGO_WIFI_RSSI_STATE,
					    "wifi RSSI state switch to High\n");
			} else if (wifirssi < rssi_thresh) {
				wifirssi_state = BTC_RSSI_STATE_LOW;
				btc_alg_dbg(ALGO_WIFI_RSSI_STATE,
					    "wifi RSSI state switch to Low\n");
			} else {
				wifirssi_state = BTC_RSSI_STATE_STAY_MEDIUM;
				btc_alg_dbg(ALGO_WIFI_RSSI_STATE,
					    "wifi RSSI state stay at Medium\n");
			}
		} else {
			if (wifirssi < rssi_thresh1) {
				wifirssi_state = BTC_RSSI_STATE_MEDIUM;
				btc_alg_dbg(ALGO_WIFI_RSSI_STATE,
					    "wifi RSSI state switch to Medium\n");
			} else {
				wifirssi_state = BTC_RSSI_STATE_STAY_HIGH;
				btc_alg_dbg(ALGO_WIFI_RSSI_STATE,
					    "wifi RSSI state stay at High\n");
			}
		}
	}

	coex_sta->pre_wifi_rssi_state[index] = wifirssi_state;

	return wifirssi_state;
}

static void btc8192e2ant_monitor_bt_enable_dis(struct btc_coexist *btcoexist)
{
	static bool pre_bt_disabled;
	static u32 bt_disable_cnt;
	bool bt_active = true, bt_disabled = false;

	/* This function check if bt is disabled */

	if (coex_sta->high_priority_tx == 0 &&
	    coex_sta->high_priority_rx == 0 &&
	    coex_sta->low_priority_tx == 0 &&
	    coex_sta->low_priority_rx == 0)
		bt_active = false;

	if (coex_sta->high_priority_tx == 0xffff &&
	    coex_sta->high_priority_rx == 0xffff &&
	    coex_sta->low_priority_tx == 0xffff &&
	    coex_sta->low_priority_rx == 0xffff)
		bt_active = false;

	if (bt_active) {
		bt_disable_cnt = 0;
		bt_disabled = false;
		btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_DISABLE,
				   &bt_disabled);
		btc_alg_dbg(ALGO_BT_MONITOR,
			    "[BTCoex], BT is enabled !!\n");
	} else {
		bt_disable_cnt++;
		btc_alg_dbg(ALGO_BT_MONITOR,
			    "[BTCoex], bt all counters = 0, %d times!!\n",
			    bt_disable_cnt);
		if (bt_disable_cnt >= 2) {
			bt_disabled = true;
			btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_DISABLE,
					   &bt_disabled);
			btc_alg_dbg(ALGO_BT_MONITOR,
				    "[BTCoex], BT is disabled !!\n");
		}
	}
	if (pre_bt_disabled != bt_disabled) {
		btc_alg_dbg(ALGO_BT_MONITOR,
			    "[BTCoex], BT is from %s to %s!!\n",
			    (pre_bt_disabled ? "disabled" : "enabled"),
			    (bt_disabled ? "disabled" : "enabled"));
		pre_bt_disabled = bt_disabled;
	}
}

static u32 halbtc8192e2ant_decidera_mask(struct btc_coexist *btcoexist,
					 u8 sstype, u32 ra_masktype)
{
	u32 disra_mask = 0x0;

	switch (ra_masktype) {
	case 0: /* normal mode */
		if (sstype == 2)
			disra_mask = 0x0;	/* enable 2ss */
		else
			disra_mask = 0xfff00000;/* disable 2ss */
		break;
	case 1: /* disable cck 1/2 */
		if (sstype == 2)
			disra_mask = 0x00000003;/* enable 2ss */
		else
			disra_mask = 0xfff00003;/* disable 2ss */
		break;
	case 2: /* disable cck 1/2/5.5, ofdm 6/9/12/18/24, mcs 0/1/2/3/4 */
		if (sstype == 2)
			disra_mask = 0x0001f1f7;/* enable 2ss */
		else
			disra_mask = 0xfff1f1f7;/* disable 2ss */
		break;
	default:
		break;
	}

	return disra_mask;
}

static void halbtc8192e2ant_Updatera_mask(struct btc_coexist *btcoexist,
					  bool force_exec, u32 dis_ratemask)
{
	coex_dm->curra_mask = dis_ratemask;

	if (force_exec || (coex_dm->prera_mask != coex_dm->curra_mask))
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_UPDATE_ra_mask,
				   &coex_dm->curra_mask);
	coex_dm->prera_mask = coex_dm->curra_mask;
}

static void btc8192e2ant_autorate_fallback_retry(struct btc_coexist *btcoexist,
						 bool force_exec, u8 type)
{
	bool wifi_under_bmode = false;

	coex_dm->cur_arfrtype = type;

	if (force_exec || (coex_dm->pre_arfrtype != coex_dm->cur_arfrtype)) {
		switch (coex_dm->cur_arfrtype) {
		case 0:	/* normal mode */
			btcoexist->btc_write_4byte(btcoexist, 0x430,
						   coex_dm->backup_arfr_cnt1);
			btcoexist->btc_write_4byte(btcoexist, 0x434,
						   coex_dm->backup_arfr_cnt2);
			break;
		case 1:
			btcoexist->btc_get(btcoexist,
					   BTC_GET_BL_WIFI_UNDER_B_MODE,
					   &wifi_under_bmode);
			if (wifi_under_bmode) {
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

	coex_dm->pre_arfrtype = coex_dm->cur_arfrtype;
}

static void halbtc8192e2ant_retrylimit(struct btc_coexist *btcoexist,
				       bool force_exec, u8 type)
{
	coex_dm->cur_retrylimit_type = type;

	if (force_exec || (coex_dm->pre_retrylimit_type !=
			   coex_dm->cur_retrylimit_type)) {
		switch (coex_dm->cur_retrylimit_type) {
		case 0:	/* normal mode */
				btcoexist->btc_write_2byte(btcoexist, 0x42a,
						    coex_dm->backup_retrylimit);
				break;
		case 1:	/* retry limit = 8 */
				btcoexist->btc_write_2byte(btcoexist, 0x42a,
							   0x0808);
				break;
		default:
				break;
		}
	}

	coex_dm->pre_retrylimit_type = coex_dm->cur_retrylimit_type;
}

static void halbtc8192e2ant_ampdu_maxtime(struct btc_coexist *btcoexist,
					  bool force_exec, u8 type)
{
	coex_dm->cur_ampdutime_type = type;

	if (force_exec || (coex_dm->pre_ampdutime_type !=
			   coex_dm->cur_ampdutime_type)) {
		switch (coex_dm->cur_ampdutime_type) {
		case 0:	/* normal mode */
			btcoexist->btc_write_1byte(btcoexist, 0x456,
						coex_dm->backup_ampdu_maxtime);
			break;
		case 1:	/* AMPDU timw = 0x38 * 32us */
			btcoexist->btc_write_1byte(btcoexist, 0x456, 0x38);
			break;
		default:
			break;
		}
	}

	coex_dm->pre_ampdutime_type = coex_dm->cur_ampdutime_type;
}

static void halbtc8192e2ant_limited_tx(struct btc_coexist *btcoexist,
				       bool force_exec, u8 ra_masktype,
				       u8 arfr_type, u8 retrylimit_type,
				       u8 ampdutime_type)
{
	u32 disra_mask = 0x0;

	coex_dm->curra_masktype = ra_masktype;
	disra_mask = halbtc8192e2ant_decidera_mask(btcoexist,
						   coex_dm->cur_sstype,
						   ra_masktype);
	halbtc8192e2ant_Updatera_mask(btcoexist, force_exec, disra_mask);
btc8192e2ant_autorate_fallback_retry(btcoexist, force_exec, arfr_type);
	halbtc8192e2ant_retrylimit(btcoexist, force_exec, retrylimit_type);
	halbtc8192e2ant_ampdu_maxtime(btcoexist, force_exec, ampdutime_type);
}

static void halbtc8192e2ant_limited_rx(struct btc_coexist *btcoexist,
				       bool force_exec, bool rej_ap_agg_pkt,
				       bool bt_ctrl_agg_buf_size,
				       u8 agg_buf_size)
{
	bool reject_rx_agg = rej_ap_agg_pkt;
	bool bt_ctrl_rx_agg_size = bt_ctrl_agg_buf_size;
	u8 rx_agg_size = agg_buf_size;

	/*********************************************
	 *	Rx Aggregation related setting
	 *********************************************/
	btcoexist->btc_set(btcoexist, BTC_SET_BL_TO_REJ_AP_AGG_PKT,
			   &reject_rx_agg);
	/* decide BT control aggregation buf size or not */
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_CTRL_AGG_SIZE,
			   &bt_ctrl_rx_agg_size);
	/* aggregation buf size, only work
	 * when BT control Rx aggregation size.
	 */
	btcoexist->btc_set(btcoexist, BTC_SET_U1_AGG_BUF_SIZE, &rx_agg_size);
	/* real update aggregation setting */
	btcoexist->btc_set(btcoexist, BTC_SET_ACT_AGGREGATE_CTRL, NULL);
}

static void halbtc8192e2ant_monitor_bt_ctr(struct btc_coexist *btcoexist)
{
	u32 reg_hp_txrx, reg_lp_txrx, u32tmp;
	u32 reg_hp_tx = 0, reg_hp_rx = 0, reg_lp_tx = 0, reg_lp_rx = 0;

	reg_hp_txrx = 0x770;
	reg_lp_txrx = 0x774;

	u32tmp = btcoexist->btc_read_4byte(btcoexist, reg_hp_txrx);
	reg_hp_tx = u32tmp & MASKLWORD;
	reg_hp_rx = (u32tmp & MASKHWORD)>>16;

	u32tmp = btcoexist->btc_read_4byte(btcoexist, reg_lp_txrx);
	reg_lp_tx = u32tmp & MASKLWORD;
	reg_lp_rx = (u32tmp & MASKHWORD)>>16;

	coex_sta->high_priority_tx = reg_hp_tx;
	coex_sta->high_priority_rx = reg_hp_rx;
	coex_sta->low_priority_tx = reg_lp_tx;
	coex_sta->low_priority_rx = reg_lp_rx;

	btc_alg_dbg(ALGO_BT_MONITOR,
		    "[BTCoex] High Priority Tx/Rx (reg 0x%x) = 0x%x(%d)/0x%x(%d)\n",
		    reg_hp_txrx, reg_hp_tx, reg_hp_tx, reg_hp_rx, reg_hp_rx);
	btc_alg_dbg(ALGO_BT_MONITOR,
		    "[BTCoex] Low Priority Tx/Rx (reg 0x%x) = 0x%x(%d)/0x%x(%d)\n",
		    reg_lp_txrx, reg_lp_tx, reg_lp_tx, reg_lp_rx, reg_lp_rx);

	/* reset counter */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0xc);
}

static void halbtc8192e2ant_querybt_info(struct btc_coexist *btcoexist)
{
	u8 h2c_parameter[1] = {0};

	coex_sta->c2h_bt_info_req_sent = true;

	h2c_parameter[0] |= BIT0;	/* trigger */

	btc_alg_dbg(ALGO_TRACE_FW_EXEC,
		    "[BTCoex], Query Bt Info, FW write 0x61 = 0x%x\n",
		    h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x61, 1, h2c_parameter);
}

static void halbtc8192e2ant_update_btlink_info(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool bt_hson = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hson);

	bt_link_info->bt_link_exist = coex_sta->bt_link_exist;
	bt_link_info->sco_exist = coex_sta->sco_exist;
	bt_link_info->a2dp_exist = coex_sta->a2dp_exist;
	bt_link_info->pan_exist = coex_sta->pan_exist;
	bt_link_info->hid_exist = coex_sta->hid_exist;

	/* work around for HS mode. */
	if (bt_hson) {
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

static u8 halbtc8192e2ant_action_algorithm(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	struct btc_stack_info *stack_info = &btcoexist->stack_info;
	bool bt_hson = false;
	u8 algorithm = BT_8192E_2ANT_COEX_ALGO_UNDEFINED;
	u8 numdiffprofile = 0;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hson);

	if (!bt_link_info->bt_link_exist) {
		btc_alg_dbg(ALGO_TRACE,
			    "No BT link exists!!!\n");
		return algorithm;
	}

	if (bt_link_info->sco_exist)
		numdiffprofile++;
	if (bt_link_info->hid_exist)
		numdiffprofile++;
	if (bt_link_info->pan_exist)
		numdiffprofile++;
	if (bt_link_info->a2dp_exist)
		numdiffprofile++;

	if (numdiffprofile == 1) {
		if (bt_link_info->sco_exist) {
			btc_alg_dbg(ALGO_TRACE,
				    "SCO only\n");
			algorithm = BT_8192E_2ANT_COEX_ALGO_SCO;
		} else {
			if (bt_link_info->hid_exist) {
				btc_alg_dbg(ALGO_TRACE,
					    "HID only\n");
				algorithm = BT_8192E_2ANT_COEX_ALGO_HID;
			} else if (bt_link_info->a2dp_exist) {
				btc_alg_dbg(ALGO_TRACE,
					    "A2DP only\n");
				algorithm = BT_8192E_2ANT_COEX_ALGO_A2DP;
			} else if (bt_link_info->pan_exist) {
				if (bt_hson) {
					btc_alg_dbg(ALGO_TRACE,
						    "PAN(HS) only\n");
					algorithm =
						BT_8192E_2ANT_COEX_ALGO_PANHS;
				} else {
					btc_alg_dbg(ALGO_TRACE,
						    "PAN(EDR) only\n");
					algorithm =
						BT_8192E_2ANT_COEX_ALGO_PANEDR;
				}
			}
		}
	} else if (numdiffprofile == 2) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist) {
				btc_alg_dbg(ALGO_TRACE,
					    "SCO + HID\n");
				algorithm = BT_8192E_2ANT_COEX_ALGO_SCO;
			} else if (bt_link_info->a2dp_exist) {
				btc_alg_dbg(ALGO_TRACE,
					    "SCO + A2DP ==> SCO\n");
				algorithm = BT_8192E_2ANT_COEX_ALGO_PANEDR_HID;
			} else if (bt_link_info->pan_exist) {
				if (bt_hson) {
					btc_alg_dbg(ALGO_TRACE,
						    "SCO + PAN(HS)\n");
					algorithm = BT_8192E_2ANT_COEX_ALGO_SCO;
				} else {
					btc_alg_dbg(ALGO_TRACE,
						    "SCO + PAN(EDR)\n");
					algorithm =
						BT_8192E_2ANT_COEX_ALGO_SCO_PAN;
				}
			}
		} else {
			if (bt_link_info->hid_exist &&
			    bt_link_info->a2dp_exist) {
				if (stack_info->num_of_hid >= 2) {
					btc_alg_dbg(ALGO_TRACE,
						    "HID*2 + A2DP\n");
					algorithm =
					BT_8192E_2ANT_COEX_ALGO_HID_A2DP_PANEDR;
				} else {
					btc_alg_dbg(ALGO_TRACE,
						    "HID + A2DP\n");
					algorithm =
					    BT_8192E_2ANT_COEX_ALGO_HID_A2DP;
				}
			} else if (bt_link_info->hid_exist &&
				   bt_link_info->pan_exist) {
				if (bt_hson) {
					btc_alg_dbg(ALGO_TRACE,
						    "HID + PAN(HS)\n");
					algorithm = BT_8192E_2ANT_COEX_ALGO_HID;
				} else {
					btc_alg_dbg(ALGO_TRACE,
						    "HID + PAN(EDR)\n");
					algorithm =
					    BT_8192E_2ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (bt_link_info->pan_exist &&
				   bt_link_info->a2dp_exist) {
				if (bt_hson) {
					btc_alg_dbg(ALGO_TRACE,
						    "A2DP + PAN(HS)\n");
					algorithm =
					    BT_8192E_2ANT_COEX_ALGO_A2DP_PANHS;
				} else {
					btc_alg_dbg(ALGO_TRACE,
						    "A2DP + PAN(EDR)\n");
					algorithm =
					    BT_8192E_2ANT_COEX_ALGO_PANEDR_A2DP;
				}
			}
		}
	} else if (numdiffprofile == 3) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist &&
			    bt_link_info->a2dp_exist) {
				btc_alg_dbg(ALGO_TRACE,
					    "SCO + HID + A2DP ==> HID\n");
				algorithm = BT_8192E_2ANT_COEX_ALGO_PANEDR_HID;
			} else if (bt_link_info->hid_exist &&
				   bt_link_info->pan_exist) {
				if (bt_hson) {
					btc_alg_dbg(ALGO_TRACE,
						    "SCO + HID + PAN(HS)\n");
					algorithm = BT_8192E_2ANT_COEX_ALGO_SCO;
				} else {
					btc_alg_dbg(ALGO_TRACE,
						    "SCO + HID + PAN(EDR)\n");
					algorithm =
						BT_8192E_2ANT_COEX_ALGO_SCO_PAN;
				}
			} else if (bt_link_info->pan_exist &&
				   bt_link_info->a2dp_exist) {
				if (bt_hson) {
					btc_alg_dbg(ALGO_TRACE,
						    "SCO + A2DP + PAN(HS)\n");
					algorithm = BT_8192E_2ANT_COEX_ALGO_SCO;
				} else {
					btc_alg_dbg(ALGO_TRACE,
						    "SCO + A2DP + PAN(EDR)\n");
					algorithm =
					    BT_8192E_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (bt_link_info->hid_exist &&
			    bt_link_info->pan_exist &&
			    bt_link_info->a2dp_exist) {
				if (bt_hson) {
					btc_alg_dbg(ALGO_TRACE,
						    "HID + A2DP + PAN(HS)\n");
					algorithm =
					    BT_8192E_2ANT_COEX_ALGO_HID_A2DP;
				} else {
					btc_alg_dbg(ALGO_TRACE,
						    "HID + A2DP + PAN(EDR)\n");
					algorithm =
					BT_8192E_2ANT_COEX_ALGO_HID_A2DP_PANEDR;
				}
			}
		}
	} else if (numdiffprofile >= 3) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist &&
			    bt_link_info->pan_exist &&
			    bt_link_info->a2dp_exist) {
				if (bt_hson) {
					btc_alg_dbg(ALGO_TRACE,
						    "ErrorSCO+HID+A2DP+PAN(HS)\n");

				} else {
					btc_alg_dbg(ALGO_TRACE,
						    "SCO+HID+A2DP+PAN(EDR)\n");
					algorithm =
					    BT_8192E_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
	}

	return algorithm;
}

static void halbtc8192e2ant_setfw_dac_swinglevel(struct btc_coexist *btcoexist,
						 u8 dac_swinglvl)
{
	u8 h2c_parameter[1] = {0};

	/* There are several type of dacswing
	 * 0x18/ 0x10/ 0xc/ 0x8/ 0x4/ 0x6
	 */
	h2c_parameter[0] = dac_swinglvl;

	btc_alg_dbg(ALGO_TRACE_FW_EXEC,
		    "[BTCoex], Set Dac Swing Level = 0x%x\n", dac_swinglvl);
	btc_alg_dbg(ALGO_TRACE_FW_EXEC,
		    "[BTCoex], FW write 0x64 = 0x%x\n", h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x64, 1, h2c_parameter);
}

static void halbtc8192e2ant_set_fwdec_btpwr(struct btc_coexist *btcoexist,
					    u8 dec_btpwr_lvl)
{
	u8 h2c_parameter[1] = {0};

	h2c_parameter[0] = dec_btpwr_lvl;

	btc_alg_dbg(ALGO_TRACE_FW_EXEC,
		    "[BTCoex] decrease Bt Power level = %d, FW write 0x62 = 0x%x\n",
		    dec_btpwr_lvl, h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x62, 1, h2c_parameter);
}

static void halbtc8192e2ant_dec_btpwr(struct btc_coexist *btcoexist,
				      bool force_exec, u8 dec_btpwr_lvl)
{
	btc_alg_dbg(ALGO_TRACE_FW,
		    "[BTCoex], %s Dec BT power level = %d\n",
		    (force_exec ? "force to" : ""), dec_btpwr_lvl);
	coex_dm->cur_dec_bt_pwr = dec_btpwr_lvl;

	if (!force_exec) {
		btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
			    "[BTCoex], preBtDecPwrLvl=%d, curBtDecPwrLvl=%d\n",
			    coex_dm->pre_dec_bt_pwr, coex_dm->cur_dec_bt_pwr);
	}
	halbtc8192e2ant_set_fwdec_btpwr(btcoexist, coex_dm->cur_dec_bt_pwr);

	coex_dm->pre_dec_bt_pwr = coex_dm->cur_dec_bt_pwr;
}

static void halbtc8192e2ant_set_bt_autoreport(struct btc_coexist *btcoexist,
					      bool enable_autoreport)
{
	u8 h2c_parameter[1] = {0};

	h2c_parameter[0] = 0;

	if (enable_autoreport)
		h2c_parameter[0] |= BIT0;

	btc_alg_dbg(ALGO_TRACE_FW_EXEC,
		    "[BTCoex], BT FW auto report : %s, FW write 0x68 = 0x%x\n",
		    (enable_autoreport ? "Enabled!!" : "Disabled!!"),
		    h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x68, 1, h2c_parameter);
}

static void halbtc8192e2ant_bt_autoreport(struct btc_coexist *btcoexist,
					  bool force_exec,
					  bool enable_autoreport)
{
	btc_alg_dbg(ALGO_TRACE_FW,
		    "[BTCoex], %s BT Auto report = %s\n",
		    (force_exec ? "force to" : ""),
		    ((enable_autoreport) ? "Enabled" : "Disabled"));
	coex_dm->cur_bt_auto_report = enable_autoreport;

	if (!force_exec) {
		btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
			    "[BTCoex] bPreBtAutoReport=%d, bCurBtAutoReport=%d\n",
			    coex_dm->pre_bt_auto_report,
			    coex_dm->cur_bt_auto_report);

		if (coex_dm->pre_bt_auto_report == coex_dm->cur_bt_auto_report)
			return;
	}
	halbtc8192e2ant_set_bt_autoreport(btcoexist,
					  coex_dm->cur_bt_auto_report);

	coex_dm->pre_bt_auto_report = coex_dm->cur_bt_auto_report;
}

static void halbtc8192e2ant_fw_dac_swinglvl(struct btc_coexist *btcoexist,
					    bool force_exec, u8 fw_dac_swinglvl)
{
	btc_alg_dbg(ALGO_TRACE_FW,
		    "[BTCoex], %s set FW Dac Swing level = %d\n",
		    (force_exec ? "force to" : ""), fw_dac_swinglvl);
	coex_dm->cur_fw_dac_swing_lvl = fw_dac_swinglvl;

	if (!force_exec) {
		btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
			    "[BTCoex] preFwDacSwingLvl=%d, curFwDacSwingLvl=%d\n",
			    coex_dm->pre_fw_dac_swing_lvl,
			    coex_dm->cur_fw_dac_swing_lvl);

		if (coex_dm->pre_fw_dac_swing_lvl ==
		    coex_dm->cur_fw_dac_swing_lvl)
			return;
	}

	halbtc8192e2ant_setfw_dac_swinglevel(btcoexist,
					     coex_dm->cur_fw_dac_swing_lvl);

	coex_dm->pre_fw_dac_swing_lvl = coex_dm->cur_fw_dac_swing_lvl;
}

static void btc8192e2ant_set_sw_rf_rx_lpf_corner(struct btc_coexist *btcoexist,
						 bool rx_rf_shrink_on)
{
	if (rx_rf_shrink_on) {
		/* Shrink RF Rx LPF corner */
		btc_alg_dbg(ALGO_TRACE_SW_EXEC,
			    "[BTCoex], Shrink RF Rx LPF corner!!\n");
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1e,
					  0xfffff, 0xffffc);
	} else {
		/* Resume RF Rx LPF corner
		 * After initialized, we can use coex_dm->btRf0x1eBackup
		 */
		if (btcoexist->initilized) {
			btc_alg_dbg(ALGO_TRACE_SW_EXEC,
				    "[BTCoex], Resume RF Rx LPF corner!!\n");
			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1e,
						  0xfffff,
						  coex_dm->bt_rf0x1e_backup);
		}
	}
}

static void halbtc8192e2ant_rf_shrink(struct btc_coexist *btcoexist,
				      bool force_exec, bool rx_rf_shrink_on)
{
	btc_alg_dbg(ALGO_TRACE_SW,
		    "[BTCoex], %s turn Rx RF Shrink = %s\n",
		    (force_exec ? "force to" : ""),
		    ((rx_rf_shrink_on) ? "ON" : "OFF"));
	coex_dm->cur_rf_rx_lpf_shrink = rx_rf_shrink_on;

	if (!force_exec) {
		btc_alg_dbg(ALGO_TRACE_SW_DETAIL,
			    "[BTCoex]bPreRfRxLpfShrink=%d,bCurRfRxLpfShrink=%d\n",
			    coex_dm->pre_rf_rx_lpf_shrink,
			    coex_dm->cur_rf_rx_lpf_shrink);

		if (coex_dm->pre_rf_rx_lpf_shrink ==
		    coex_dm->cur_rf_rx_lpf_shrink)
			return;
	}
	btc8192e2ant_set_sw_rf_rx_lpf_corner(btcoexist,
					     coex_dm->cur_rf_rx_lpf_shrink);

	coex_dm->pre_rf_rx_lpf_shrink = coex_dm->cur_rf_rx_lpf_shrink;
}

static void halbtc8192e2ant_set_dac_swingreg(struct btc_coexist *btcoexist,
					     u32 level)
{
	u8 val = (u8)level;

	btc_alg_dbg(ALGO_TRACE_SW_EXEC,
		    "[BTCoex], Write SwDacSwing = 0x%x\n", level);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x883, 0x3e, val);
}

static void btc8192e2ant_setsw_full_swing(struct btc_coexist *btcoexist,
					  bool sw_dac_swingon,
					  u32 sw_dac_swinglvl)
{
	if (sw_dac_swingon)
		halbtc8192e2ant_set_dac_swingreg(btcoexist, sw_dac_swinglvl);
	else
		halbtc8192e2ant_set_dac_swingreg(btcoexist, 0x18);
}

static void halbtc8192e2ant_DacSwing(struct btc_coexist *btcoexist,
				     bool force_exec, bool dac_swingon,
				     u32 dac_swinglvl)
{
	btc_alg_dbg(ALGO_TRACE_SW,
		    "[BTCoex], %s turn DacSwing=%s, dac_swinglvl = 0x%x\n",
		    (force_exec ? "force to" : ""),
		    ((dac_swingon) ? "ON" : "OFF"), dac_swinglvl);
	coex_dm->cur_dac_swing_on = dac_swingon;
	coex_dm->cur_dac_swing_lvl = dac_swinglvl;

	if (!force_exec) {
		btc_alg_dbg(ALGO_TRACE_SW_DETAIL,
			    "[BTCoex], bPreDacSwingOn=%d, preDacSwingLvl = 0x%x, ",
			    coex_dm->pre_dac_swing_on,
			    coex_dm->pre_dac_swing_lvl);
		btc_alg_dbg(ALGO_TRACE_SW_DETAIL,
			    "bCurDacSwingOn=%d, curDacSwingLvl = 0x%x\n",
			    coex_dm->cur_dac_swing_on,
			    coex_dm->cur_dac_swing_lvl);

		if ((coex_dm->pre_dac_swing_on == coex_dm->cur_dac_swing_on) &&
		    (coex_dm->pre_dac_swing_lvl == coex_dm->cur_dac_swing_lvl))
			return;
	}
	mdelay(30);
	btc8192e2ant_setsw_full_swing(btcoexist, dac_swingon, dac_swinglvl);

	coex_dm->pre_dac_swing_on = coex_dm->cur_dac_swing_on;
	coex_dm->pre_dac_swing_lvl = coex_dm->cur_dac_swing_lvl;
}

static void halbtc8192e2ant_set_agc_table(struct btc_coexist *btcoexist,
					  bool agc_table_en)
{
	/* BB AGC Gain Table */
	if (agc_table_en) {
		btc_alg_dbg(ALGO_TRACE_SW_EXEC,
			    "[BTCoex], BB Agc Table On!\n");
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x0a1A0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x091B0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x081C0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x071D0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x061E0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x051F0001);
	} else {
		btc_alg_dbg(ALGO_TRACE_SW_EXEC,
			    "[BTCoex], BB Agc Table Off!\n");
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0xaa1A0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0xa91B0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0xa81C0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0xa71D0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0xa61E0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0xa51F0001);
	}
}

static void halbtc8192e2ant_AgcTable(struct btc_coexist *btcoexist,
				     bool force_exec, bool agc_table_en)
{
	btc_alg_dbg(ALGO_TRACE_SW,
		    "[BTCoex], %s %s Agc Table\n",
		    (force_exec ? "force to" : ""),
		    ((agc_table_en) ? "Enable" : "Disable"));
	coex_dm->cur_agc_table_en = agc_table_en;

	if (!force_exec) {
		btc_alg_dbg(ALGO_TRACE_SW_DETAIL,
			    "[BTCoex], bPreAgcTableEn=%d, bCurAgcTableEn=%d\n",
			    coex_dm->pre_agc_table_en,
			    coex_dm->cur_agc_table_en);

		if (coex_dm->pre_agc_table_en == coex_dm->cur_agc_table_en)
			return;
	}
	halbtc8192e2ant_set_agc_table(btcoexist, agc_table_en);

	coex_dm->pre_agc_table_en = coex_dm->cur_agc_table_en;
}

static void halbtc8192e2ant_set_coex_table(struct btc_coexist *btcoexist,
					   u32 val0x6c0, u32 val0x6c4,
					   u32 val0x6c8, u8 val0x6cc)
{
	btc_alg_dbg(ALGO_TRACE_SW_EXEC,
		    "[BTCoex], set coex table, set 0x6c0 = 0x%x\n", val0x6c0);
	btcoexist->btc_write_4byte(btcoexist, 0x6c0, val0x6c0);

	btc_alg_dbg(ALGO_TRACE_SW_EXEC,
		    "[BTCoex], set coex table, set 0x6c4 = 0x%x\n", val0x6c4);
	btcoexist->btc_write_4byte(btcoexist, 0x6c4, val0x6c4);

	btc_alg_dbg(ALGO_TRACE_SW_EXEC,
		    "[BTCoex], set coex table, set 0x6c8 = 0x%x\n", val0x6c8);
	btcoexist->btc_write_4byte(btcoexist, 0x6c8, val0x6c8);

	btc_alg_dbg(ALGO_TRACE_SW_EXEC,
		    "[BTCoex], set coex table, set 0x6cc = 0x%x\n", val0x6cc);
	btcoexist->btc_write_1byte(btcoexist, 0x6cc, val0x6cc);
}

static void halbtc8192e2ant_coex_table(struct btc_coexist *btcoexist,
				       bool force_exec,
				       u32 val0x6c0, u32 val0x6c4,
				       u32 val0x6c8, u8 val0x6cc)
{
	btc_alg_dbg(ALGO_TRACE_SW,
		    "[BTCoex], %s write Coex Table 0x6c0 = 0x%x, ",
		    (force_exec ? "force to" : ""), val0x6c0);
	btc_alg_dbg(ALGO_TRACE_SW,
		    "0x6c4 = 0x%x, 0x6c8 = 0x%x, 0x6cc = 0x%x\n",
		    val0x6c4, val0x6c8, val0x6cc);
	coex_dm->cur_val0x6c0 = val0x6c0;
	coex_dm->cur_val0x6c4 = val0x6c4;
	coex_dm->cur_val0x6c8 = val0x6c8;
	coex_dm->cur_val0x6cc = val0x6cc;

	if (!force_exec) {
		btc_alg_dbg(ALGO_TRACE_SW_DETAIL,
			    "[BTCoex], preVal0x6c0 = 0x%x, preVal0x6c4 = 0x%x, ",
			    coex_dm->pre_val0x6c0, coex_dm->pre_val0x6c4);
		btc_alg_dbg(ALGO_TRACE_SW_DETAIL,
			    "preVal0x6c8 = 0x%x, preVal0x6cc = 0x%x !!\n",
			    coex_dm->pre_val0x6c8, coex_dm->pre_val0x6cc);
		btc_alg_dbg(ALGO_TRACE_SW_DETAIL,
			    "[BTCoex], curVal0x6c0 = 0x%x, curVal0x6c4 = 0x%x\n",
			    coex_dm->cur_val0x6c0, coex_dm->cur_val0x6c4);
		btc_alg_dbg(ALGO_TRACE_SW_DETAIL,
			    "curVal0x6c8 = 0x%x, curVal0x6cc = 0x%x !!\n",
			    coex_dm->cur_val0x6c8, coex_dm->cur_val0x6cc);

		if ((coex_dm->pre_val0x6c0 == coex_dm->cur_val0x6c0) &&
		    (coex_dm->pre_val0x6c4 == coex_dm->cur_val0x6c4) &&
		    (coex_dm->pre_val0x6c8 == coex_dm->cur_val0x6c8) &&
		    (coex_dm->pre_val0x6cc == coex_dm->cur_val0x6cc))
			return;
	}
	halbtc8192e2ant_set_coex_table(btcoexist, val0x6c0, val0x6c4,
				       val0x6c8, val0x6cc);

	coex_dm->pre_val0x6c0 = coex_dm->cur_val0x6c0;
	coex_dm->pre_val0x6c4 = coex_dm->cur_val0x6c4;
	coex_dm->pre_val0x6c8 = coex_dm->cur_val0x6c8;
	coex_dm->pre_val0x6cc = coex_dm->cur_val0x6cc;
}

static void btc8192e2ant_coex_tbl_w_type(struct btc_coexist *btcoexist,
					 bool force_exec, u8 type)
{
	switch (type) {
	case 0:
		halbtc8192e2ant_coex_table(btcoexist, force_exec, 0x55555555,
					   0x5a5a5a5a, 0xffffff, 0x3);
		break;
	case 1:
		halbtc8192e2ant_coex_table(btcoexist, force_exec, 0x5a5a5a5a,
					   0x5a5a5a5a, 0xffffff, 0x3);
		break;
	case 2:
		halbtc8192e2ant_coex_table(btcoexist, force_exec, 0x55555555,
					   0x5ffb5ffb, 0xffffff, 0x3);
		break;
	case 3:
		halbtc8192e2ant_coex_table(btcoexist, force_exec, 0xdfffdfff,
					   0x5fdb5fdb, 0xffffff, 0x3);
		break;
	case 4:
		halbtc8192e2ant_coex_table(btcoexist, force_exec, 0xdfffdfff,
					   0x5ffb5ffb, 0xffffff, 0x3);
		break;
	default:
		break;
	}
}

static void halbtc8192e2ant_set_fw_ignore_wlanact(struct btc_coexist *btcoexist,
						  bool enable)
{
	u8 h2c_parameter[1] = {0};

	if (enable)
		h2c_parameter[0] |= BIT0; /* function enable */

	btc_alg_dbg(ALGO_TRACE_FW_EXEC,
		    "[BTCoex]set FW for BT Ignore Wlan_Act, FW write 0x63 = 0x%x\n",
		    h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x63, 1, h2c_parameter);
}

static void halbtc8192e2ant_IgnoreWlanAct(struct btc_coexist *btcoexist,
					  bool force_exec, bool enable)
{
	btc_alg_dbg(ALGO_TRACE_FW,
		    "[BTCoex], %s turn Ignore WlanAct %s\n",
		    (force_exec ? "force to" : ""), (enable ? "ON" : "OFF"));
	coex_dm->cur_ignore_wlan_act = enable;

	if (!force_exec) {
		btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
			    "[BTCoex], bPreIgnoreWlanAct = %d ",
			    coex_dm->pre_ignore_wlan_act);
		btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
			    "bCurIgnoreWlanAct = %d!!\n",
			    coex_dm->cur_ignore_wlan_act);

		if (coex_dm->pre_ignore_wlan_act ==
		    coex_dm->cur_ignore_wlan_act)
			return;
	}
	halbtc8192e2ant_set_fw_ignore_wlanact(btcoexist, enable);

	coex_dm->pre_ignore_wlan_act = coex_dm->cur_ignore_wlan_act;
}

static void halbtc8192e2ant_SetFwPstdma(struct btc_coexist *btcoexist, u8 byte1,
					u8 byte2, u8 byte3, u8 byte4, u8 byte5)
{
	u8 h2c_parameter[5] = {0};

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

	btc_alg_dbg(ALGO_TRACE_FW_EXEC,
		    "[BTCoex], FW write 0x60(5bytes) = 0x%x%08x\n",
		    h2c_parameter[0],
		    h2c_parameter[1] << 24 | h2c_parameter[2] << 16 |
		    h2c_parameter[3] << 8 | h2c_parameter[4]);

	btcoexist->btc_fill_h2c(btcoexist, 0x60, 5, h2c_parameter);
}

static void btc8192e2ant_sw_mec1(struct btc_coexist *btcoexist,
				 bool shrink_rx_lpf, bool low_penalty_ra,
				 bool limited_dig, bool btlan_constrain)
{
	halbtc8192e2ant_rf_shrink(btcoexist, NORMAL_EXEC, shrink_rx_lpf);
}

static void btc8192e2ant_sw_mec2(struct btc_coexist *btcoexist,
				 bool agc_table_shift, bool adc_backoff,
				 bool sw_dac_swing, u32 dac_swinglvl)
{
	halbtc8192e2ant_AgcTable(btcoexist, NORMAL_EXEC, agc_table_shift);
	halbtc8192e2ant_DacSwing(btcoexist, NORMAL_EXEC, sw_dac_swing,
				 dac_swinglvl);
}

static void halbtc8192e2ant_ps_tdma(struct btc_coexist *btcoexist,
				    bool force_exec, bool turn_on, u8 type)
{
	btc_alg_dbg(ALGO_TRACE_FW,
		    "[BTCoex], %s turn %s PS TDMA, type=%d\n",
		    (force_exec ? "force to" : ""),
		    (turn_on ? "ON" : "OFF"), type);
	coex_dm->cur_ps_tdma_on = turn_on;
	coex_dm->cur_ps_tdma = type;

	if (!force_exec) {
		btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
			    "[BTCoex], bPrePsTdmaOn = %d, bCurPsTdmaOn = %d!!\n",
			    coex_dm->pre_ps_tdma_on, coex_dm->cur_ps_tdma_on);
		btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
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
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xe3, 0x1a,
						    0x1a, 0xe1, 0x90);
			break;
		case 2:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xe3, 0x12,
						    0x12, 0xe1, 0x90);
			break;
		case 3:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xe3, 0x1c,
						    0x3, 0xf1, 0x90);
			break;
		case 4:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xe3, 0x10,
						    0x3, 0xf1, 0x90);
			break;
		case 5:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xe3, 0x1a,
						    0x1a, 0x60, 0x90);
			break;
		case 6:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xe3, 0x12,
						    0x12, 0x60, 0x90);
			break;
		case 7:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xe3, 0x1c,
						    0x3, 0x70, 0x90);
			break;
		case 8:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xa3, 0x10,
						    0x3, 0x70, 0x90);
			break;
		case 9:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xe3, 0x1a,
						    0x1a, 0xe1, 0x10);
			break;
		case 10:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xe3, 0x12,
						    0x12, 0xe1, 0x10);
			break;
		case 11:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xe3, 0x1c,
						    0x3, 0xf1, 0x10);
			break;
		case 12:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xe3, 0x10,
						    0x3, 0xf1, 0x10);
			break;
		case 13:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xe3, 0x1a,
						    0x1a, 0xe0, 0x10);
			break;
		case 14:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xe3, 0x12,
						    0x12, 0xe0, 0x10);
			break;
		case 15:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xe3, 0x1c,
						    0x3, 0xf0, 0x10);
			break;
		case 16:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xe3, 0x12,
						    0x3, 0xf0, 0x10);
			break;
		case 17:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0x61, 0x20,
						    0x03, 0x10, 0x10);
			break;
		case 18:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xe3, 0x5,
						    0x5, 0xe1, 0x90);
			break;
		case 19:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xe3, 0x25,
						    0x25, 0xe1, 0x90);
			break;
		case 20:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xe3, 0x25,
						    0x25, 0x60, 0x90);
			break;
		case 21:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xe3, 0x15,
						    0x03, 0x70, 0x90);
			break;
		case 71:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0xe3, 0x1a,
						    0x1a, 0xe1, 0x90);
			break;
		}
	} else {
		/* disable PS tdma */
		switch (type) {
		default:
		case 0:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0x8, 0x0, 0x0,
						    0x0, 0x0);
			btcoexist->btc_write_1byte(btcoexist, 0x92c, 0x4);
			break;
		case 1:
			halbtc8192e2ant_SetFwPstdma(btcoexist, 0x0, 0x0, 0x0,
						    0x8, 0x0);
			mdelay(5);
			btcoexist->btc_write_1byte(btcoexist, 0x92c, 0x20);
			break;
		}
	}

	/* update pre state */
	coex_dm->pre_ps_tdma_on = coex_dm->cur_ps_tdma_on;
	coex_dm->pre_ps_tdma = coex_dm->cur_ps_tdma;
}

static void halbtc8192e2ant_set_switch_sstype(struct btc_coexist *btcoexist,
					      u8 sstype)
{
	u8 mimops = BTC_MIMO_PS_DYNAMIC;
	u32 disra_mask = 0x0;

	btc_alg_dbg(ALGO_TRACE,
		    "[BTCoex], REAL set SS Type = %d\n", sstype);

	disra_mask = halbtc8192e2ant_decidera_mask(btcoexist, sstype,
						   coex_dm->curra_masktype);
	halbtc8192e2ant_Updatera_mask(btcoexist, FORCE_EXEC, disra_mask);

	if (sstype == 1) {
		halbtc8192e2ant_ps_tdma(btcoexist, FORCE_EXEC, false, 1);
		/* switch ofdm path */
		btcoexist->btc_write_1byte(btcoexist, 0xc04, 0x11);
		btcoexist->btc_write_1byte(btcoexist, 0xd04, 0x1);
		btcoexist->btc_write_4byte(btcoexist, 0x90c, 0x81111111);
		/* switch cck patch */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xe77, 0x4, 0x1);
		btcoexist->btc_write_1byte(btcoexist, 0xa07, 0x81);
		mimops = BTC_MIMO_PS_STATIC;
	} else if (sstype == 2) {
		halbtc8192e2ant_ps_tdma(btcoexist, FORCE_EXEC, false, 0);
		btcoexist->btc_write_1byte(btcoexist, 0xc04, 0x33);
		btcoexist->btc_write_1byte(btcoexist, 0xd04, 0x3);
		btcoexist->btc_write_4byte(btcoexist, 0x90c, 0x81121313);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xe77, 0x4, 0x0);
		btcoexist->btc_write_1byte(btcoexist, 0xa07, 0x41);
		mimops = BTC_MIMO_PS_DYNAMIC;
	}
	/* set rx 1ss or 2ss */
	btcoexist->btc_set(btcoexist, BTC_SET_ACT_SEND_MIMO_PS, &mimops);
}

static void halbtc8192e2ant_switch_sstype(struct btc_coexist *btcoexist,
					  bool force_exec, u8 new_sstype)
{
	btc_alg_dbg(ALGO_TRACE,
		    "[BTCoex], %s Switch SS Type = %d\n",
		    (force_exec ? "force to" : ""), new_sstype);
	coex_dm->cur_sstype = new_sstype;

	if (!force_exec) {
		if (coex_dm->pre_sstype == coex_dm->cur_sstype)
			return;
	}
	halbtc8192e2ant_set_switch_sstype(btcoexist, coex_dm->cur_sstype);

	coex_dm->pre_sstype = coex_dm->cur_sstype;
}

static void halbtc8192e2ant_coex_alloff(struct btc_coexist *btcoexist)
{
	/* fw all off */
	halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
	halbtc8192e2ant_fw_dac_swinglvl(btcoexist, NORMAL_EXEC, 6);
	halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 0);

	/* sw all off */
	btc8192e2ant_sw_mec1(btcoexist, false, false, false, false);
	btc8192e2ant_sw_mec2(btcoexist, false, false, false, 0x18);

	/* hw all off */
	btc8192e2ant_coex_tbl_w_type(btcoexist, NORMAL_EXEC, 0);
}

static void halbtc8192e2ant_init_coex_dm(struct btc_coexist *btcoexist)
{
	/* force to reset coex mechanism */

	halbtc8192e2ant_ps_tdma(btcoexist, FORCE_EXEC, false, 1);
	halbtc8192e2ant_fw_dac_swinglvl(btcoexist, FORCE_EXEC, 6);
	halbtc8192e2ant_dec_btpwr(btcoexist, FORCE_EXEC, 0);

	btc8192e2ant_coex_tbl_w_type(btcoexist, FORCE_EXEC, 0);
	halbtc8192e2ant_switch_sstype(btcoexist, FORCE_EXEC, 2);

	btc8192e2ant_sw_mec1(btcoexist, false, false, false, false);
	btc8192e2ant_sw_mec2(btcoexist, false, false, false, 0x18);
}

static void halbtc8192e2ant_action_bt_inquiry(struct btc_coexist *btcoexist)
{
	bool low_pwr_disable = true;

	btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
			   &low_pwr_disable);

	halbtc8192e2ant_switch_sstype(btcoexist, NORMAL_EXEC, 1);

	btc8192e2ant_coex_tbl_w_type(btcoexist, NORMAL_EXEC, 2);
	halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 3);
	halbtc8192e2ant_fw_dac_swinglvl(btcoexist, NORMAL_EXEC, 6);
	halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 0);

	btc8192e2ant_sw_mec1(btcoexist, false, false, false, false);
	btc8192e2ant_sw_mec2(btcoexist, false, false, false, 0x18);
}

static bool halbtc8192e2ant_is_common_action(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool common = false, wifi_connected = false, wifi_busy = false;
	bool bt_hson = false, low_pwr_disable = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hson);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	if (bt_link_info->sco_exist || bt_link_info->hid_exist)
		halbtc8192e2ant_limited_tx(btcoexist, NORMAL_EXEC, 1, 0, 0, 0);
	else
		halbtc8192e2ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);

	if (!wifi_connected) {
		low_pwr_disable = false;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);

		btc_alg_dbg(ALGO_TRACE,
			    "[BTCoex], Wifi non-connected idle!!\n");

		if ((BT_8192E_2ANT_BT_STATUS_NON_CONNECTED_IDLE ==
		     coex_dm->bt_status) ||
		    (BT_8192E_2ANT_BT_STATUS_CONNECTED_IDLE ==
		     coex_dm->bt_status)) {
			halbtc8192e2ant_switch_sstype(btcoexist,
						      NORMAL_EXEC, 2);
			btc8192e2ant_coex_tbl_w_type(btcoexist,
						     NORMAL_EXEC, 1);
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						false, 0);
		} else {
			halbtc8192e2ant_switch_sstype(btcoexist,
						      NORMAL_EXEC, 1);
			btc8192e2ant_coex_tbl_w_type(btcoexist,
						     NORMAL_EXEC, 0);
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						false, 1);
		}

		halbtc8192e2ant_fw_dac_swinglvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 0);

		btc8192e2ant_sw_mec1(btcoexist, false, false, false, false);
		btc8192e2ant_sw_mec2(btcoexist, false, false, false, 0x18);

		common = true;
	} else {
		if (BT_8192E_2ANT_BT_STATUS_NON_CONNECTED_IDLE ==
		    coex_dm->bt_status) {
			low_pwr_disable = false;
			btcoexist->btc_set(btcoexist,
					   BTC_SET_ACT_DISABLE_LOW_POWER,
					   &low_pwr_disable);

			btc_alg_dbg(ALGO_TRACE,
				    "Wifi connected + BT non connected-idle!!\n");

			halbtc8192e2ant_switch_sstype(btcoexist,
						      NORMAL_EXEC, 2);
			btc8192e2ant_coex_tbl_w_type(btcoexist,
						     NORMAL_EXEC, 1);
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						false, 0);
			halbtc8192e2ant_fw_dac_swinglvl(btcoexist,
							NORMAL_EXEC, 6);
			halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 0);

			btc8192e2ant_sw_mec1(btcoexist, false, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x18);

			common = true;
		} else if (BT_8192E_2ANT_BT_STATUS_CONNECTED_IDLE ==
			   coex_dm->bt_status) {
			low_pwr_disable = true;
			btcoexist->btc_set(btcoexist,
					   BTC_SET_ACT_DISABLE_LOW_POWER,
					   &low_pwr_disable);

			if (bt_hson)
				return false;
			btc_alg_dbg(ALGO_TRACE,
				    "Wifi connected + BT connected-idle!!\n");

			halbtc8192e2ant_switch_sstype(btcoexist,
						      NORMAL_EXEC, 2);
			btc8192e2ant_coex_tbl_w_type(btcoexist,
						     NORMAL_EXEC, 1);
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						false, 0);
			halbtc8192e2ant_fw_dac_swinglvl(btcoexist,
							NORMAL_EXEC, 6);
			halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 0);

			btc8192e2ant_sw_mec1(btcoexist, true, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x18);

			common = true;
		} else {
			low_pwr_disable = true;
			btcoexist->btc_set(btcoexist,
					   BTC_SET_ACT_DISABLE_LOW_POWER,
					   &low_pwr_disable);

			if (wifi_busy) {
				btc_alg_dbg(ALGO_TRACE,
					    "Wifi Connected-Busy + BT Busy!!\n");
				common = false;
			} else {
				btc_alg_dbg(ALGO_TRACE,
					    "Wifi Connected-Idle + BT Busy!!\n");

				halbtc8192e2ant_switch_sstype(btcoexist,
							      NORMAL_EXEC, 1);
				btc8192e2ant_coex_tbl_w_type(btcoexist,
							     NORMAL_EXEC, 2);
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 21);
				halbtc8192e2ant_fw_dac_swinglvl(btcoexist,
								NORMAL_EXEC, 6);
				halbtc8192e2ant_dec_btpwr(btcoexist,
							  NORMAL_EXEC, 0);
				btc8192e2ant_sw_mec1(btcoexist, false,
						     false, false, false);
				btc8192e2ant_sw_mec2(btcoexist, false,
						     false, false, 0x18);
				common = true;
			}
		}
	}
	return common;
}

static void btc8192e_int1(struct btc_coexist *btcoexist, bool tx_pause,
			  int result)
{
	if (tx_pause) {
		btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
			    "[BTCoex], TxPause = 1\n");

		if (coex_dm->cur_ps_tdma == 71) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 5);
			coex_dm->tdma_adj_type = 5;
		} else if (coex_dm->cur_ps_tdma == 1) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 5);
			coex_dm->tdma_adj_type = 5;
		} else if (coex_dm->cur_ps_tdma == 2) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 6);
			coex_dm->tdma_adj_type = 6;
		} else if (coex_dm->cur_ps_tdma == 3) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 7);
			coex_dm->tdma_adj_type = 7;
		} else if (coex_dm->cur_ps_tdma == 4) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 8);
			coex_dm->tdma_adj_type = 8;
		}
		if (coex_dm->cur_ps_tdma == 9) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 13);
			coex_dm->tdma_adj_type = 13;
		} else if (coex_dm->cur_ps_tdma == 10) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 14);
			coex_dm->tdma_adj_type = 14;
		} else if (coex_dm->cur_ps_tdma == 11) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 15);
			coex_dm->tdma_adj_type = 15;
		} else if (coex_dm->cur_ps_tdma == 12) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 16);
			coex_dm->tdma_adj_type = 16;
		}

		if (result == -1) {
			if (coex_dm->cur_ps_tdma == 5) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 6);
				coex_dm->tdma_adj_type = 6;
			} else if (coex_dm->cur_ps_tdma == 6) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 7);
				coex_dm->tdma_adj_type = 7;
			} else if (coex_dm->cur_ps_tdma == 7) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 8);
				coex_dm->tdma_adj_type = 8;
			} else if (coex_dm->cur_ps_tdma == 13) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 14);
				coex_dm->tdma_adj_type = 14;
			} else if (coex_dm->cur_ps_tdma == 14) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 15);
				coex_dm->tdma_adj_type = 15;
			} else if (coex_dm->cur_ps_tdma == 15) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 16);
				coex_dm->tdma_adj_type = 16;
			}
		} else if (result == 1) {
			if (coex_dm->cur_ps_tdma == 8) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 7);
				coex_dm->tdma_adj_type = 7;
			} else if (coex_dm->cur_ps_tdma == 7) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 6);
				coex_dm->tdma_adj_type = 6;
			} else if (coex_dm->cur_ps_tdma == 6) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 5);
				coex_dm->tdma_adj_type = 5;
			} else if (coex_dm->cur_ps_tdma == 16) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 15);
				coex_dm->tdma_adj_type = 15;
			} else if (coex_dm->cur_ps_tdma == 15) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 14);
				coex_dm->tdma_adj_type = 14;
			} else if (coex_dm->cur_ps_tdma == 14) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 13);
				coex_dm->tdma_adj_type = 13;
			}
		}
	} else {
		btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
			    "[BTCoex], TxPause = 0\n");
		if (coex_dm->cur_ps_tdma == 5) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 71);
			coex_dm->tdma_adj_type = 71;
		} else if (coex_dm->cur_ps_tdma == 6) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 2);
			coex_dm->tdma_adj_type = 2;
		} else if (coex_dm->cur_ps_tdma == 7) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 3);
			coex_dm->tdma_adj_type = 3;
		} else if (coex_dm->cur_ps_tdma == 8) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 4);
			coex_dm->tdma_adj_type = 4;
		}
		if (coex_dm->cur_ps_tdma == 13) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 9);
			coex_dm->tdma_adj_type = 9;
		} else if (coex_dm->cur_ps_tdma == 14) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 10);
			coex_dm->tdma_adj_type = 10;
		} else if (coex_dm->cur_ps_tdma == 15) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 11);
			coex_dm->tdma_adj_type = 11;
		} else if (coex_dm->cur_ps_tdma == 16) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 12);
			coex_dm->tdma_adj_type = 12;
		}

		if (result == -1) {
			if (coex_dm->cur_ps_tdma == 71) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 1);
				coex_dm->tdma_adj_type = 1;
			} else if (coex_dm->cur_ps_tdma == 1) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 2);
				coex_dm->tdma_adj_type = 2;
			} else if (coex_dm->cur_ps_tdma == 2) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 3);
				coex_dm->tdma_adj_type = 3;
			} else if (coex_dm->cur_ps_tdma == 3) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 4);
				coex_dm->tdma_adj_type = 4;
			} else if (coex_dm->cur_ps_tdma == 9) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 10);
				coex_dm->tdma_adj_type = 10;
			} else if (coex_dm->cur_ps_tdma == 10) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 11);
				coex_dm->tdma_adj_type = 11;
			} else if (coex_dm->cur_ps_tdma == 11) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 12);
				coex_dm->tdma_adj_type = 12;
			}
		} else if (result == 1) {
			if (coex_dm->cur_ps_tdma == 4) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 3);
				coex_dm->tdma_adj_type = 3;
			} else if (coex_dm->cur_ps_tdma == 3) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 2);
				coex_dm->tdma_adj_type = 2;
			} else if (coex_dm->cur_ps_tdma == 2) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 1);
				coex_dm->tdma_adj_type = 1;
			} else if (coex_dm->cur_ps_tdma == 1) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 71);
				coex_dm->tdma_adj_type = 71;
			} else if (coex_dm->cur_ps_tdma == 12) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 11);
				coex_dm->tdma_adj_type = 11;
			} else if (coex_dm->cur_ps_tdma == 11) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 10);
				coex_dm->tdma_adj_type = 10;
			} else if (coex_dm->cur_ps_tdma == 10) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 9);
				coex_dm->tdma_adj_type = 9;
			}
		}
	}
}

static void btc8192e_int2(struct btc_coexist *btcoexist, bool tx_pause,
			  int result)
{
	if (tx_pause) {
		btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
			    "[BTCoex], TxPause = 1\n");
		if (coex_dm->cur_ps_tdma == 1) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 6);
			coex_dm->tdma_adj_type = 6;
		} else if (coex_dm->cur_ps_tdma == 2) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 6);
			coex_dm->tdma_adj_type = 6;
		} else if (coex_dm->cur_ps_tdma == 3) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 7);
			coex_dm->tdma_adj_type = 7;
		} else if (coex_dm->cur_ps_tdma == 4) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 8);
			coex_dm->tdma_adj_type = 8;
		}
		if (coex_dm->cur_ps_tdma == 9) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 14);
			coex_dm->tdma_adj_type = 14;
		} else if (coex_dm->cur_ps_tdma == 10) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 14);
			coex_dm->tdma_adj_type = 14;
		} else if (coex_dm->cur_ps_tdma == 11) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 15);
			coex_dm->tdma_adj_type = 15;
		} else if (coex_dm->cur_ps_tdma == 12) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 16);
			coex_dm->tdma_adj_type = 16;
		}
		if (result == -1) {
			if (coex_dm->cur_ps_tdma == 5) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 6);
				coex_dm->tdma_adj_type = 6;
			} else if (coex_dm->cur_ps_tdma == 6) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 7);
				coex_dm->tdma_adj_type = 7;
			} else if (coex_dm->cur_ps_tdma == 7) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 8);
				coex_dm->tdma_adj_type = 8;
			} else if (coex_dm->cur_ps_tdma == 13) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 14);
				coex_dm->tdma_adj_type = 14;
			} else if (coex_dm->cur_ps_tdma == 14) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 15);
				coex_dm->tdma_adj_type = 15;
			} else if (coex_dm->cur_ps_tdma == 15) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 16);
				coex_dm->tdma_adj_type = 16;
			}
		} else if (result == 1) {
			if (coex_dm->cur_ps_tdma == 8) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 7);
				coex_dm->tdma_adj_type = 7;
			} else if (coex_dm->cur_ps_tdma == 7) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 6);
				coex_dm->tdma_adj_type = 6;
			} else if (coex_dm->cur_ps_tdma == 6) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 6);
				coex_dm->tdma_adj_type = 6;
			} else if (coex_dm->cur_ps_tdma == 16) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 15);
				coex_dm->tdma_adj_type = 15;
			} else if (coex_dm->cur_ps_tdma == 15) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 14);
				coex_dm->tdma_adj_type = 14;
			} else if (coex_dm->cur_ps_tdma == 14) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 14);
				coex_dm->tdma_adj_type = 14;
			}
		}
	} else {
		btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
			    "[BTCoex], TxPause = 0\n");
		if (coex_dm->cur_ps_tdma == 5) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 2);
			coex_dm->tdma_adj_type = 2;
		} else if (coex_dm->cur_ps_tdma == 6) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 2);
			coex_dm->tdma_adj_type = 2;
		} else if (coex_dm->cur_ps_tdma == 7) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 3);
			coex_dm->tdma_adj_type = 3;
		} else if (coex_dm->cur_ps_tdma == 8) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 4);
			coex_dm->tdma_adj_type = 4;
		}
		if (coex_dm->cur_ps_tdma == 13) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 10);
			coex_dm->tdma_adj_type = 10;
		} else if (coex_dm->cur_ps_tdma == 14) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 10);
			coex_dm->tdma_adj_type = 10;
		} else if (coex_dm->cur_ps_tdma == 15) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 11);
			coex_dm->tdma_adj_type = 11;
		} else if (coex_dm->cur_ps_tdma == 16) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 12);
			coex_dm->tdma_adj_type = 12;
		}
		if (result == -1) {
			if (coex_dm->cur_ps_tdma == 1) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 2);
				coex_dm->tdma_adj_type = 2;
			} else if (coex_dm->cur_ps_tdma == 2) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 3);
				coex_dm->tdma_adj_type = 3;
			} else if (coex_dm->cur_ps_tdma == 3) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 4);
				coex_dm->tdma_adj_type = 4;
			} else if (coex_dm->cur_ps_tdma == 9) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 10);
				coex_dm->tdma_adj_type = 10;
			} else if (coex_dm->cur_ps_tdma == 10) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 11);
				coex_dm->tdma_adj_type = 11;
			} else if (coex_dm->cur_ps_tdma == 11) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 12);
				coex_dm->tdma_adj_type = 12;
			}
		} else if (result == 1) {
			if (coex_dm->cur_ps_tdma == 4) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 3);
				coex_dm->tdma_adj_type = 3;
			} else if (coex_dm->cur_ps_tdma == 3) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 2);
				coex_dm->tdma_adj_type = 2;
			} else if (coex_dm->cur_ps_tdma == 2) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 2);
				coex_dm->tdma_adj_type = 2;
			} else if (coex_dm->cur_ps_tdma == 12) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 11);
				coex_dm->tdma_adj_type = 11;
			} else if (coex_dm->cur_ps_tdma == 11) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 10);
				coex_dm->tdma_adj_type = 10;
			} else if (coex_dm->cur_ps_tdma == 10) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 10);
				coex_dm->tdma_adj_type = 10;
			}
		}
	}
}

static void btc8192e_int3(struct btc_coexist *btcoexist, bool tx_pause,
			  int result)
{
	if (tx_pause) {
		btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
			    "[BTCoex], TxPause = 1\n");
		if (coex_dm->cur_ps_tdma == 1) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 7);
			coex_dm->tdma_adj_type = 7;
		} else if (coex_dm->cur_ps_tdma == 2) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 7);
			coex_dm->tdma_adj_type = 7;
		} else if (coex_dm->cur_ps_tdma == 3) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 7);
			coex_dm->tdma_adj_type = 7;
		} else if (coex_dm->cur_ps_tdma == 4) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 8);
			coex_dm->tdma_adj_type = 8;
		}
		if (coex_dm->cur_ps_tdma == 9) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 15);
			coex_dm->tdma_adj_type = 15;
		} else if (coex_dm->cur_ps_tdma == 10) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 15);
			coex_dm->tdma_adj_type = 15;
		} else if (coex_dm->cur_ps_tdma == 11) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 15);
			coex_dm->tdma_adj_type = 15;
		} else if (coex_dm->cur_ps_tdma == 12) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 16);
			coex_dm->tdma_adj_type = 16;
		}
		if (result == -1) {
			if (coex_dm->cur_ps_tdma == 5) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 7);
				coex_dm->tdma_adj_type = 7;
			} else if (coex_dm->cur_ps_tdma == 6) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 7);
				coex_dm->tdma_adj_type = 7;
			} else if (coex_dm->cur_ps_tdma == 7) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 8);
				coex_dm->tdma_adj_type = 8;
			} else if (coex_dm->cur_ps_tdma == 13) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 15);
				coex_dm->tdma_adj_type = 15;
			} else if (coex_dm->cur_ps_tdma == 14) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 15);
				coex_dm->tdma_adj_type = 15;
			} else if (coex_dm->cur_ps_tdma == 15) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 16);
				coex_dm->tdma_adj_type = 16;
			}
		} else if (result == 1) {
			if (coex_dm->cur_ps_tdma == 8) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 7);
				coex_dm->tdma_adj_type = 7;
			} else if (coex_dm->cur_ps_tdma == 7) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 7);
				coex_dm->tdma_adj_type = 7;
			} else if (coex_dm->cur_ps_tdma == 6) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 7);
				coex_dm->tdma_adj_type = 7;
			} else if (coex_dm->cur_ps_tdma == 16) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 15);
				coex_dm->tdma_adj_type = 15;
			} else if (coex_dm->cur_ps_tdma == 15) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 15);
				coex_dm->tdma_adj_type = 15;
			} else if (coex_dm->cur_ps_tdma == 14) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 15);
				coex_dm->tdma_adj_type = 15;
			}
		}
	} else {
		btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
			    "[BTCoex], TxPause = 0\n");
		if (coex_dm->cur_ps_tdma == 5) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 3);
			coex_dm->tdma_adj_type = 3;
		} else if (coex_dm->cur_ps_tdma == 6) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 3);
			coex_dm->tdma_adj_type = 3;
		} else if (coex_dm->cur_ps_tdma == 7) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 3);
			coex_dm->tdma_adj_type = 3;
		} else if (coex_dm->cur_ps_tdma == 8) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 4);
			coex_dm->tdma_adj_type = 4;
		}
		if (coex_dm->cur_ps_tdma == 13) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 11);
			coex_dm->tdma_adj_type = 11;
		} else if (coex_dm->cur_ps_tdma == 14) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 11);
			coex_dm->tdma_adj_type = 11;
		} else if (coex_dm->cur_ps_tdma == 15) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 11);
			coex_dm->tdma_adj_type = 11;
		} else if (coex_dm->cur_ps_tdma == 16) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 12);
			coex_dm->tdma_adj_type = 12;
		}
		if (result == -1) {
			if (coex_dm->cur_ps_tdma == 1) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 3);
				coex_dm->tdma_adj_type = 3;
			} else if (coex_dm->cur_ps_tdma == 2) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 3);
				coex_dm->tdma_adj_type = 3;
			} else if (coex_dm->cur_ps_tdma == 3) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 4);
				coex_dm->tdma_adj_type = 4;
			} else if (coex_dm->cur_ps_tdma == 9) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 11);
				coex_dm->tdma_adj_type = 11;
			} else if (coex_dm->cur_ps_tdma == 10) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 11);
				coex_dm->tdma_adj_type = 11;
			} else if (coex_dm->cur_ps_tdma == 11) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 12);
				coex_dm->tdma_adj_type = 12;
			}
		} else if (result == 1) {
			if (coex_dm->cur_ps_tdma == 4) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 3);
				coex_dm->tdma_adj_type = 3;
			} else if (coex_dm->cur_ps_tdma == 3) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 3);
				coex_dm->tdma_adj_type = 3;
			} else if (coex_dm->cur_ps_tdma == 2) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 3);
				coex_dm->tdma_adj_type = 3;
			} else if (coex_dm->cur_ps_tdma == 12) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 11);
				coex_dm->tdma_adj_type = 11;
			} else if (coex_dm->cur_ps_tdma == 11) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 11);
				coex_dm->tdma_adj_type = 11;
			} else if (coex_dm->cur_ps_tdma == 10) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 11);
				coex_dm->tdma_adj_type = 11;
			}
		}
	}
}

static void halbtc8192e2ant_tdma_duration_adjust(struct btc_coexist *btcoexist,
						 bool sco_hid, bool tx_pause,
						 u8 max_interval)
{
	static int up, dn, m, n, wait_cnt;
	/* 0: no change, +1: increase WiFi duration,
	 * -1: decrease WiFi duration
	 */
	int result;
	u8 retry_cnt = 0;

	btc_alg_dbg(ALGO_TRACE_FW,
		    "[BTCoex], TdmaDurationAdjust()\n");

	if (!coex_dm->auto_tdma_adjust) {
		coex_dm->auto_tdma_adjust = true;
		btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
			    "[BTCoex], first run TdmaDurationAdjust()!!\n");
		if (sco_hid) {
			if (tx_pause) {
				if (max_interval == 1) {
					halbtc8192e2ant_ps_tdma(btcoexist,
								NORMAL_EXEC,
								true, 13);
					coex_dm->tdma_adj_type = 13;
				} else if (max_interval == 2) {
					halbtc8192e2ant_ps_tdma(btcoexist,
								NORMAL_EXEC,
								true, 14);
					coex_dm->tdma_adj_type = 14;
				} else if (max_interval == 3) {
					halbtc8192e2ant_ps_tdma(btcoexist,
								NORMAL_EXEC,
								true, 15);
					coex_dm->tdma_adj_type = 15;
				} else {
					halbtc8192e2ant_ps_tdma(btcoexist,
								NORMAL_EXEC,
								true, 15);
					coex_dm->tdma_adj_type = 15;
				}
			} else {
				if (max_interval == 1) {
					halbtc8192e2ant_ps_tdma(btcoexist,
								NORMAL_EXEC,
								true, 9);
					coex_dm->tdma_adj_type = 9;
				} else if (max_interval == 2) {
					halbtc8192e2ant_ps_tdma(btcoexist,
								NORMAL_EXEC,
								true, 10);
					coex_dm->tdma_adj_type = 10;
				} else if (max_interval == 3) {
					halbtc8192e2ant_ps_tdma(btcoexist,
								NORMAL_EXEC,
								true, 11);
					coex_dm->tdma_adj_type = 11;
				} else {
					halbtc8192e2ant_ps_tdma(btcoexist,
								NORMAL_EXEC,
								true, 11);
					coex_dm->tdma_adj_type = 11;
				}
			}
		} else {
			if (tx_pause) {
				if (max_interval == 1) {
					halbtc8192e2ant_ps_tdma(btcoexist,
								NORMAL_EXEC,
								true, 5);
					coex_dm->tdma_adj_type = 5;
				} else if (max_interval == 2) {
					halbtc8192e2ant_ps_tdma(btcoexist,
								NORMAL_EXEC,
								true, 6);
					coex_dm->tdma_adj_type = 6;
				} else if (max_interval == 3) {
					halbtc8192e2ant_ps_tdma(btcoexist,
								NORMAL_EXEC,
								true, 7);
					coex_dm->tdma_adj_type = 7;
				} else {
					halbtc8192e2ant_ps_tdma(btcoexist,
								NORMAL_EXEC,
								true, 7);
					coex_dm->tdma_adj_type = 7;
				}
			} else {
				if (max_interval == 1) {
					halbtc8192e2ant_ps_tdma(btcoexist,
								NORMAL_EXEC,
								true, 1);
					coex_dm->tdma_adj_type = 1;
				} else if (max_interval == 2) {
					halbtc8192e2ant_ps_tdma(btcoexist,
								NORMAL_EXEC,
								true, 2);
					coex_dm->tdma_adj_type = 2;
				} else if (max_interval == 3) {
					halbtc8192e2ant_ps_tdma(btcoexist,
								NORMAL_EXEC,
								true, 3);
					coex_dm->tdma_adj_type = 3;
				} else {
					halbtc8192e2ant_ps_tdma(btcoexist,
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
		wait_cnt = 0;
	} else {
		/* accquire the BT TRx retry count from BT_Info byte2 */
		retry_cnt = coex_sta->bt_retry_cnt;
		btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
			    "[BTCoex], retry_cnt = %d\n", retry_cnt);
		btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
			    "[BTCoex], up=%d, dn=%d, m=%d, n=%d, wait_cnt=%d\n",
			    up, dn, m, n, wait_cnt);
		result = 0;
		wait_cnt++;
		/* no retry in the last 2-second duration */
		if (retry_cnt == 0) {
			up++;
			dn--;

			if (dn <= 0)
				dn = 0;

			if (up >= n) {
				wait_cnt = 0;
				n = 3;
				up = 0;
				dn = 0;
				result = 1;
				btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
					    "[BTCoex]Increase wifi duration!!\n");
			}
		} else if (retry_cnt <= 3) {
			up--;
			dn++;

			if (up <= 0)
				up = 0;

			if (dn == 2) {
				if (wait_cnt <= 2)
					m++;
				else
					m = 1;

				if (m >= 20)
					m = 20;

				n = 3 * m;
				up = 0;
				dn = 0;
				wait_cnt = 0;
				result = -1;
				btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
					    "Reduce wifi duration for retry<3\n");
			}
		} else {
			if (wait_cnt == 1)
				m++;
			else
				m = 1;

			if (m >= 20)
				m = 20;

			n = 3*m;
			up = 0;
			dn = 0;
			wait_cnt = 0;
			result = -1;
			btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
				    "Decrease wifi duration for retryCounter>3!!\n");
		}

		btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
			    "[BTCoex], max Interval = %d\n", max_interval);
		if (max_interval == 1)
			btc8192e_int1(btcoexist, tx_pause, result);
		else if (max_interval == 2)
			btc8192e_int2(btcoexist, tx_pause, result);
		else if (max_interval == 3)
			btc8192e_int3(btcoexist, tx_pause, result);
	}

	/* if current PsTdma not match with
	 * the recorded one (when scan, dhcp...),
	 * then we have to adjust it back to the previous record one.
	 */
	if (coex_dm->cur_ps_tdma != coex_dm->tdma_adj_type) {
		bool scan = false, link = false, roam = false;

		btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
			    "[BTCoex], PsTdma type dismatch!!!, ");
		btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
			    "curPsTdma=%d, recordPsTdma=%d\n",
			    coex_dm->cur_ps_tdma, coex_dm->tdma_adj_type);

		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);

		if (!scan && !link && !roam)
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true,
						coex_dm->tdma_adj_type);
		else
			btc_alg_dbg(ALGO_TRACE_FW_DETAIL,
				    "[BTCoex], roaming/link/scan is under progress, will adjust next time!!!\n");
	}
}

/* SCO only or SCO+PAN(HS) */
static void halbtc8192e2ant_action_sco(struct btc_coexist *btcoexist)
{
	u8 wifirssi_state, btrssi_state = BTC_RSSI_STATE_STAY_LOW;
	u32 wifi_bw;

	wifirssi_state = halbtc8192e2ant_wifirssi_state(btcoexist, 0, 2, 15, 0);

	halbtc8192e2ant_switch_sstype(btcoexist, NORMAL_EXEC, 1);
	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8192e2ant_fw_dac_swinglvl(btcoexist, NORMAL_EXEC, 6);

	btc8192e2ant_coex_tbl_w_type(btcoexist, NORMAL_EXEC, 4);

	btrssi_state = halbtc8192e2ant_btrssi_state(3, 34, 42);

	if ((btrssi_state == BTC_RSSI_STATE_LOW) ||
	    (btrssi_state == BTC_RSSI_STATE_STAY_LOW)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 0);
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 13);
	} else if ((btrssi_state == BTC_RSSI_STATE_MEDIUM) ||
		   (btrssi_state == BTC_RSSI_STATE_STAY_MEDIUM)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 2);
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 9);
	} else if ((btrssi_state == BTC_RSSI_STATE_HIGH) ||
		   (btrssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 4);
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 9);
	}

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	/* sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, true, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     false, 0x6);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, true, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x6);
		}
	} else {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, false, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     false, 0x6);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, false, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x6);
		}
	}
}

static void halbtc8192e2ant_action_sco_pan(struct btc_coexist *btcoexist)
{
	u8 wifirssi_state, btrssi_state = BTC_RSSI_STATE_STAY_LOW;
	u32 wifi_bw;

	wifirssi_state = halbtc8192e2ant_wifirssi_state(btcoexist, 0, 2, 15, 0);

	halbtc8192e2ant_switch_sstype(btcoexist, NORMAL_EXEC, 1);
	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8192e2ant_fw_dac_swinglvl(btcoexist, NORMAL_EXEC, 6);

	btc8192e2ant_coex_tbl_w_type(btcoexist, NORMAL_EXEC, 4);

	btrssi_state = halbtc8192e2ant_btrssi_state(3, 34, 42);

	if ((btrssi_state == BTC_RSSI_STATE_LOW) ||
	    (btrssi_state == BTC_RSSI_STATE_STAY_LOW)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 0);
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 14);
	} else if ((btrssi_state == BTC_RSSI_STATE_MEDIUM) ||
		   (btrssi_state == BTC_RSSI_STATE_STAY_MEDIUM)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 2);
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 10);
	} else if ((btrssi_state == BTC_RSSI_STATE_HIGH) ||
		   (btrssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 4);
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 10);
	}

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	/* sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, true, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     false, 0x6);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, true, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x6);
		}
	} else {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, false, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     false, 0x6);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, false, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x6);
		}
	}
}

static void halbtc8192e2ant_action_hid(struct btc_coexist *btcoexist)
{
	u8 wifirssi_state, btrssi_state = BTC_RSSI_STATE_HIGH;
	u32 wifi_bw;

	wifirssi_state = halbtc8192e2ant_wifirssi_state(btcoexist, 0, 2, 15, 0);
	btrssi_state = halbtc8192e2ant_btrssi_state(3, 34, 42);

	halbtc8192e2ant_switch_sstype(btcoexist, NORMAL_EXEC, 1);
	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8192e2ant_fw_dac_swinglvl(btcoexist, NORMAL_EXEC, 6);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	btc8192e2ant_coex_tbl_w_type(btcoexist, NORMAL_EXEC, 3);

	if ((btrssi_state == BTC_RSSI_STATE_LOW) ||
	    (btrssi_state == BTC_RSSI_STATE_STAY_LOW)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 0);
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 13);
	} else if ((btrssi_state == BTC_RSSI_STATE_MEDIUM) ||
		   (btrssi_state == BTC_RSSI_STATE_STAY_MEDIUM)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 2);
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 9);
	} else if ((btrssi_state == BTC_RSSI_STATE_HIGH) ||
		   (btrssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 4);
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 9);
	}

	/* sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, true, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     false, 0x18);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, true, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x18);
		}
	} else {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, false, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     false, 0x18);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, false, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x18);
		}
	}
}

/* A2DP only / PAN(EDR) only/ A2DP+PAN(HS) */
static void halbtc8192e2ant_action_a2dp(struct btc_coexist *btcoexist)
{
	u8 wifirssi_state, btrssi_state = BTC_RSSI_STATE_HIGH;
	u32 wifi_bw;
	bool long_dist = false;

	wifirssi_state = halbtc8192e2ant_wifirssi_state(btcoexist, 0, 2, 15, 0);
	btrssi_state = halbtc8192e2ant_btrssi_state(3, 34, 42);

	if ((btrssi_state == BTC_RSSI_STATE_LOW ||
	     btrssi_state == BTC_RSSI_STATE_STAY_LOW) &&
	    (wifirssi_state == BTC_RSSI_STATE_LOW ||
	     wifirssi_state == BTC_RSSI_STATE_STAY_LOW)) {
		btc_alg_dbg(ALGO_TRACE,
			    "[BTCoex], A2dp, wifi/bt rssi both LOW!!\n");
		long_dist = true;
	}
	if (long_dist) {
		halbtc8192e2ant_switch_sstype(btcoexist, NORMAL_EXEC, 2);
		halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, true,
					   0x4);
	} else {
		halbtc8192e2ant_switch_sstype(btcoexist, NORMAL_EXEC, 1);
		halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false,
					   0x8);
	}

	halbtc8192e2ant_fw_dac_swinglvl(btcoexist, NORMAL_EXEC, 6);

	if (long_dist)
		btc8192e2ant_coex_tbl_w_type(btcoexist, NORMAL_EXEC, 0);
	else
		btc8192e2ant_coex_tbl_w_type(btcoexist, NORMAL_EXEC, 2);

	if (long_dist) {
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 17);
		coex_dm->auto_tdma_adjust = false;
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 0);
	} else {
		if ((btrssi_state == BTC_RSSI_STATE_LOW) ||
		    (btrssi_state == BTC_RSSI_STATE_STAY_LOW)) {
			halbtc8192e2ant_tdma_duration_adjust(btcoexist, false,
							     true, 1);
			halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 0);
		} else if ((btrssi_state == BTC_RSSI_STATE_MEDIUM) ||
			   (btrssi_state == BTC_RSSI_STATE_STAY_MEDIUM)) {
			halbtc8192e2ant_tdma_duration_adjust(btcoexist, false,
							     false, 1);
			halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 2);
		} else if ((btrssi_state == BTC_RSSI_STATE_HIGH) ||
			   (btrssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8192e2ant_tdma_duration_adjust(btcoexist, false,
							     false, 1);
			halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 4);
		}
	}

	/* sw mechanism */
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, true, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     false, 0x18);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, true, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x18);
		}
	} else {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, false, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     false, 0x18);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, false, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x18);
		}
	}
}

static void halbtc8192e2ant_action_a2dp_pan_hs(struct btc_coexist *btcoexist)
{
	u8 wifirssi_state, btrssi_state = BTC_RSSI_STATE_HIGH;
	u32 wifi_bw;

	wifirssi_state = halbtc8192e2ant_wifirssi_state(btcoexist, 0, 2, 15, 0);
	btrssi_state = halbtc8192e2ant_btrssi_state(3, 34, 42);

	halbtc8192e2ant_switch_sstype(btcoexist, NORMAL_EXEC, 1);
	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8192e2ant_fw_dac_swinglvl(btcoexist, NORMAL_EXEC, 6);
	btc8192e2ant_coex_tbl_w_type(btcoexist, NORMAL_EXEC, 2);

	if ((btrssi_state == BTC_RSSI_STATE_LOW) ||
	    (btrssi_state == BTC_RSSI_STATE_STAY_LOW)) {
		halbtc8192e2ant_tdma_duration_adjust(btcoexist, false, true, 2);
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 0);
	} else if ((btrssi_state == BTC_RSSI_STATE_MEDIUM) ||
		   (btrssi_state == BTC_RSSI_STATE_STAY_MEDIUM)) {
		halbtc8192e2ant_tdma_duration_adjust(btcoexist, false,
						     false, 2);
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 2);
	} else if ((btrssi_state == BTC_RSSI_STATE_HIGH) ||
		   (btrssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
		halbtc8192e2ant_tdma_duration_adjust(btcoexist, false,
						     false, 2);
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 4);
	}

	/* sw mechanism */
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, true, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     true, 0x6);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, true, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     true, 0x6);
		}
	} else {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, false, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     true, 0x6);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, false, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     true, 0x6);
		}
	}
}

static void halbtc8192e2ant_action_pan_edr(struct btc_coexist *btcoexist)
{
	u8 wifirssi_state, btrssi_state = BTC_RSSI_STATE_HIGH;
	u32 wifi_bw;

	wifirssi_state = halbtc8192e2ant_wifirssi_state(btcoexist, 0, 2, 15, 0);
	btrssi_state = halbtc8192e2ant_btrssi_state(3, 34, 42);

	halbtc8192e2ant_switch_sstype(btcoexist, NORMAL_EXEC, 1);
	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8192e2ant_fw_dac_swinglvl(btcoexist, NORMAL_EXEC, 6);

	btc8192e2ant_coex_tbl_w_type(btcoexist, NORMAL_EXEC, 2);

	if ((btrssi_state == BTC_RSSI_STATE_LOW) ||
	    (btrssi_state == BTC_RSSI_STATE_STAY_LOW)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 0);
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 5);
	} else if ((btrssi_state == BTC_RSSI_STATE_MEDIUM) ||
		   (btrssi_state == BTC_RSSI_STATE_STAY_MEDIUM)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 2);
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 1);
	} else if ((btrssi_state == BTC_RSSI_STATE_HIGH) ||
		   (btrssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 4);
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 1);
	}

	/* sw mechanism */
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, true, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     false, 0x18);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, true, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x18);
		}
	} else {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, false, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     false, 0x18);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, false, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x18);
		}
	}
}

/* PAN(HS) only */
static void halbtc8192e2ant_action_pan_hs(struct btc_coexist *btcoexist)
{
	u8 wifirssi_state, btrssi_state = BTC_RSSI_STATE_HIGH;
	u32 wifi_bw;

	wifirssi_state = halbtc8192e2ant_wifirssi_state(btcoexist, 0, 2, 15, 0);
	btrssi_state = halbtc8192e2ant_btrssi_state(3, 34, 42);

	halbtc8192e2ant_switch_sstype(btcoexist, NORMAL_EXEC, 1);
	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8192e2ant_fw_dac_swinglvl(btcoexist, NORMAL_EXEC, 6);

	btc8192e2ant_coex_tbl_w_type(btcoexist, NORMAL_EXEC, 2);

	if ((btrssi_state == BTC_RSSI_STATE_LOW) ||
	    (btrssi_state == BTC_RSSI_STATE_STAY_LOW)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 0);
	} else if ((btrssi_state == BTC_RSSI_STATE_MEDIUM) ||
		   (btrssi_state == BTC_RSSI_STATE_STAY_MEDIUM)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 2);
	} else if ((btrssi_state == BTC_RSSI_STATE_HIGH) ||
		   (btrssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 4);
	}
	halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, true, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     false, 0x18);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, true, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x18);
		}
	} else {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, false, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     false, 0x18);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, false, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x18);
		}
	}
}

/* PAN(EDR)+A2DP */
static void halbtc8192e2ant_action_pan_edr_a2dp(struct btc_coexist *btcoexist)
{
	u8 wifirssi_state, btrssi_state = BTC_RSSI_STATE_HIGH;
	u32 wifi_bw;

	wifirssi_state = halbtc8192e2ant_wifirssi_state(btcoexist, 0, 2, 15, 0);
	btrssi_state = halbtc8192e2ant_btrssi_state(3, 34, 42);

	halbtc8192e2ant_switch_sstype(btcoexist, NORMAL_EXEC, 1);
	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8192e2ant_fw_dac_swinglvl(btcoexist, NORMAL_EXEC, 6);

	btc8192e2ant_coex_tbl_w_type(btcoexist, NORMAL_EXEC, 2);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if ((btrssi_state == BTC_RSSI_STATE_LOW) ||
	    (btrssi_state == BTC_RSSI_STATE_STAY_LOW)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 0);
		halbtc8192e2ant_tdma_duration_adjust(btcoexist, false, true, 3);
	} else if ((btrssi_state == BTC_RSSI_STATE_MEDIUM) ||
		   (btrssi_state == BTC_RSSI_STATE_STAY_MEDIUM)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 2);
		halbtc8192e2ant_tdma_duration_adjust(btcoexist, false,
						     false, 3);
	} else if ((btrssi_state == BTC_RSSI_STATE_HIGH) ||
		   (btrssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 4);
		halbtc8192e2ant_tdma_duration_adjust(btcoexist, false,
						     false, 3);
	}

	/* sw mechanism	*/
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, true, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     false, 0x18);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, true, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x18);
		}
	} else {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, false, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     false, 0x18);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, false, false,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x18);
		}
	}
}

static void halbtc8192e2ant_action_pan_edr_hid(struct btc_coexist *btcoexist)
{
	u8 wifirssi_state, btrssi_state = BTC_RSSI_STATE_HIGH;
	u32 wifi_bw;

	wifirssi_state = halbtc8192e2ant_wifirssi_state(btcoexist, 0, 2, 15, 0);
	btrssi_state = halbtc8192e2ant_btrssi_state(3, 34, 42);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	halbtc8192e2ant_switch_sstype(btcoexist, NORMAL_EXEC, 1);
	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8192e2ant_fw_dac_swinglvl(btcoexist, NORMAL_EXEC, 6);

	btc8192e2ant_coex_tbl_w_type(btcoexist, NORMAL_EXEC, 3);

	if ((btrssi_state == BTC_RSSI_STATE_LOW) ||
	    (btrssi_state == BTC_RSSI_STATE_STAY_LOW)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 0);
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 14);
	} else if ((btrssi_state == BTC_RSSI_STATE_MEDIUM) ||
		   (btrssi_state == BTC_RSSI_STATE_STAY_MEDIUM)) {
			halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 2);
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 10);
	} else if ((btrssi_state == BTC_RSSI_STATE_HIGH) ||
		   (btrssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 4);
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 10);
	}

	/* sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, true, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     false, 0x18);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, true, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x18);
		}
	} else {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, false, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     false, 0x18);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, false, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x18);
		}
	}
}

/* HID+A2DP+PAN(EDR) */
static void btc8192e2ant_action_hid_a2dp_pan_edr(struct btc_coexist *btcoexist)
{
	u8 wifirssi_state, btrssi_state = BTC_RSSI_STATE_HIGH;
	u32 wifi_bw;

	wifirssi_state = halbtc8192e2ant_wifirssi_state(btcoexist, 0, 2, 15, 0);
	btrssi_state = halbtc8192e2ant_btrssi_state(3, 34, 42);

	halbtc8192e2ant_switch_sstype(btcoexist, NORMAL_EXEC, 1);
	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8192e2ant_fw_dac_swinglvl(btcoexist, NORMAL_EXEC, 6);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	btc8192e2ant_coex_tbl_w_type(btcoexist, NORMAL_EXEC, 3);

	if ((btrssi_state == BTC_RSSI_STATE_LOW) ||
	    (btrssi_state == BTC_RSSI_STATE_STAY_LOW)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 0);
		halbtc8192e2ant_tdma_duration_adjust(btcoexist, true, true, 3);
	} else if ((btrssi_state == BTC_RSSI_STATE_MEDIUM) ||
		   (btrssi_state == BTC_RSSI_STATE_STAY_MEDIUM)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 2);
		halbtc8192e2ant_tdma_duration_adjust(btcoexist, true, false, 3);
	} else if ((btrssi_state == BTC_RSSI_STATE_HIGH) ||
		   (btrssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 4);
		halbtc8192e2ant_tdma_duration_adjust(btcoexist, true, false, 3);
	}

	/* sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, true, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     false, 0x18);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, true, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x18);
		}
	} else {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, false, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     false, 0x18);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, false, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x18);
		}
	}
}

static void halbtc8192e2ant_action_hid_a2dp(struct btc_coexist *btcoexist)
{
	u8 wifirssi_state, btrssi_state = BTC_RSSI_STATE_HIGH;
	u32 wifi_bw;

	wifirssi_state = halbtc8192e2ant_wifirssi_state(btcoexist, 0, 2, 15, 0);
	btrssi_state = halbtc8192e2ant_btrssi_state(3, 34, 42);

	halbtc8192e2ant_switch_sstype(btcoexist, NORMAL_EXEC, 1);
	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	btc8192e2ant_coex_tbl_w_type(btcoexist, NORMAL_EXEC, 3);

	if ((btrssi_state == BTC_RSSI_STATE_LOW) ||
	    (btrssi_state == BTC_RSSI_STATE_STAY_LOW)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 0);
		halbtc8192e2ant_tdma_duration_adjust(btcoexist, true, true, 2);
	} else if ((btrssi_state == BTC_RSSI_STATE_MEDIUM) ||
		   (btrssi_state == BTC_RSSI_STATE_STAY_MEDIUM))	{
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 2);
		halbtc8192e2ant_tdma_duration_adjust(btcoexist, true, false, 2);
	} else if ((btrssi_state == BTC_RSSI_STATE_HIGH) ||
		   (btrssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
		halbtc8192e2ant_dec_btpwr(btcoexist, NORMAL_EXEC, 4);
		halbtc8192e2ant_tdma_duration_adjust(btcoexist, true, false, 2);
	}

	/* sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, true, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     false, 0x18);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, true, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x18);
		}
	} else {
		if ((wifirssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifirssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			btc8192e2ant_sw_mec1(btcoexist, false, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, true, false,
					     false, 0x18);
		} else {
			btc8192e2ant_sw_mec1(btcoexist, false, true,
					     false, false);
			btc8192e2ant_sw_mec2(btcoexist, false, false,
					     false, 0x18);
		}
	}
}

static void halbtc8192e2ant_run_coexist_mechanism(struct btc_coexist *btcoexist)
{
	u8 algorithm = 0;

	btc_alg_dbg(ALGO_TRACE,
		    "[BTCoex], RunCoexistMechanism()===>\n");

	if (btcoexist->manual_control) {
		btc_alg_dbg(ALGO_TRACE,
			    "[BTCoex], return for Manual CTRL <===\n");
		return;
	}

	if (coex_sta->under_ips) {
		btc_alg_dbg(ALGO_TRACE,
			    "[BTCoex], wifi is under IPS !!!\n");
		return;
	}

	algorithm = halbtc8192e2ant_action_algorithm(btcoexist);
	if (coex_sta->c2h_bt_inquiry_page &&
	    (BT_8192E_2ANT_COEX_ALGO_PANHS != algorithm)) {
		btc_alg_dbg(ALGO_TRACE,
			    "[BTCoex], BT is under inquiry/page scan !!\n");
		halbtc8192e2ant_action_bt_inquiry(btcoexist);
		return;
	}

	coex_dm->cur_algorithm = algorithm;
	btc_alg_dbg(ALGO_TRACE,
		    "[BTCoex], Algorithm = %d\n", coex_dm->cur_algorithm);

	if (halbtc8192e2ant_is_common_action(btcoexist)) {
		btc_alg_dbg(ALGO_TRACE,
			    "[BTCoex], Action 2-Ant common\n");
		coex_dm->auto_tdma_adjust = false;
	} else {
		if (coex_dm->cur_algorithm != coex_dm->pre_algorithm) {
			btc_alg_dbg(ALGO_TRACE,
				    "[BTCoex] preAlgorithm=%d, curAlgorithm=%d\n",
				    coex_dm->pre_algorithm,
				    coex_dm->cur_algorithm);
			coex_dm->auto_tdma_adjust = false;
		}
		switch (coex_dm->cur_algorithm) {
		case BT_8192E_2ANT_COEX_ALGO_SCO:
			btc_alg_dbg(ALGO_TRACE,
				    "Action 2-Ant, algorithm = SCO\n");
			halbtc8192e2ant_action_sco(btcoexist);
			break;
		case BT_8192E_2ANT_COEX_ALGO_SCO_PAN:
			btc_alg_dbg(ALGO_TRACE,
				    "Action 2-Ant, algorithm = SCO+PAN(EDR)\n");
			halbtc8192e2ant_action_sco_pan(btcoexist);
			break;
		case BT_8192E_2ANT_COEX_ALGO_HID:
			btc_alg_dbg(ALGO_TRACE,
				    "Action 2-Ant, algorithm = HID\n");
			halbtc8192e2ant_action_hid(btcoexist);
			break;
		case BT_8192E_2ANT_COEX_ALGO_A2DP:
			btc_alg_dbg(ALGO_TRACE,
				    "Action 2-Ant, algorithm = A2DP\n");
			halbtc8192e2ant_action_a2dp(btcoexist);
			break;
		case BT_8192E_2ANT_COEX_ALGO_A2DP_PANHS:
			btc_alg_dbg(ALGO_TRACE,
				    "Action 2-Ant, algorithm = A2DP+PAN(HS)\n");
			halbtc8192e2ant_action_a2dp_pan_hs(btcoexist);
			break;
		case BT_8192E_2ANT_COEX_ALGO_PANEDR:
			btc_alg_dbg(ALGO_TRACE,
				    "Action 2-Ant, algorithm = PAN(EDR)\n");
			halbtc8192e2ant_action_pan_edr(btcoexist);
			break;
		case BT_8192E_2ANT_COEX_ALGO_PANHS:
			btc_alg_dbg(ALGO_TRACE,
				    "Action 2-Ant, algorithm = HS mode\n");
			halbtc8192e2ant_action_pan_hs(btcoexist);
			break;
		case BT_8192E_2ANT_COEX_ALGO_PANEDR_A2DP:
			btc_alg_dbg(ALGO_TRACE,
				    "Action 2-Ant, algorithm = PAN+A2DP\n");
			halbtc8192e2ant_action_pan_edr_a2dp(btcoexist);
			break;
		case BT_8192E_2ANT_COEX_ALGO_PANEDR_HID:
			btc_alg_dbg(ALGO_TRACE,
				    "Action 2-Ant, algorithm = PAN(EDR)+HID\n");
			halbtc8192e2ant_action_pan_edr_hid(btcoexist);
			break;
		case BT_8192E_2ANT_COEX_ALGO_HID_A2DP_PANEDR:
			btc_alg_dbg(ALGO_TRACE,
				    "Action 2-Ant, algorithm = HID+A2DP+PAN\n");
			btc8192e2ant_action_hid_a2dp_pan_edr(btcoexist);
			break;
		case BT_8192E_2ANT_COEX_ALGO_HID_A2DP:
			btc_alg_dbg(ALGO_TRACE,
				    "Action 2-Ant, algorithm = HID+A2DP\n");
			halbtc8192e2ant_action_hid_a2dp(btcoexist);
			break;
		default:
			btc_alg_dbg(ALGO_TRACE,
				    "Action 2-Ant, algorithm = unknown!!\n");
			/* halbtc8192e2ant_coex_alloff(btcoexist); */
			break;
		}
		coex_dm->pre_algorithm = coex_dm->cur_algorithm;
	}
}

static void halbtc8192e2ant_init_hwconfig(struct btc_coexist *btcoexist,
					  bool backup)
{
	u16 u16tmp = 0;
	u8 u8tmp = 0;

	btc_iface_dbg(INTF_INIT,
		      "[BTCoex], 2Ant Init HW Config!!\n");

	if (backup) {
		/* backup rf 0x1e value */
		coex_dm->bt_rf0x1e_backup =
			btcoexist->btc_get_rf_reg(btcoexist, BTC_RF_A,
						  0x1e, 0xfffff);

		coex_dm->backup_arfr_cnt1 = btcoexist->btc_read_4byte(btcoexist,
								      0x430);
		coex_dm->backup_arfr_cnt2 = btcoexist->btc_read_4byte(btcoexist,
								     0x434);
		coex_dm->backup_retrylimit = btcoexist->btc_read_2byte(
								    btcoexist,
								    0x42a);
		coex_dm->backup_ampdu_maxtime = btcoexist->btc_read_1byte(
								    btcoexist,
								    0x456);
	}

	/* antenna sw ctrl to bt */
	btcoexist->btc_write_1byte(btcoexist, 0x4f, 0x6);
	btcoexist->btc_write_1byte(btcoexist, 0x944, 0x24);
	btcoexist->btc_write_4byte(btcoexist, 0x930, 0x700700);
	btcoexist->btc_write_1byte(btcoexist, 0x92c, 0x20);
	if (btcoexist->chip_interface == BTC_INTF_USB)
		btcoexist->btc_write_4byte(btcoexist, 0x64, 0x30430004);
	else
		btcoexist->btc_write_4byte(btcoexist, 0x64, 0x30030004);

	btc8192e2ant_coex_tbl_w_type(btcoexist, FORCE_EXEC, 0);

	/* antenna switch control parameter */
	btcoexist->btc_write_4byte(btcoexist, 0x858, 0x55555555);

	/* coex parameters */
	btcoexist->btc_write_1byte(btcoexist, 0x778, 0x3);
	/* 0x790[5:0] = 0x5 */
	u8tmp = btcoexist->btc_read_1byte(btcoexist, 0x790);
	u8tmp &= 0xc0;
	u8tmp |= 0x5;
	btcoexist->btc_write_1byte(btcoexist, 0x790, u8tmp);

	/* enable counter statistics */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0x4);

	/* enable PTA */
	btcoexist->btc_write_1byte(btcoexist, 0x40, 0x20);
	/* enable mailbox interface */
	u16tmp = btcoexist->btc_read_2byte(btcoexist, 0x40);
	u16tmp |= BIT9;
	btcoexist->btc_write_2byte(btcoexist, 0x40, u16tmp);

	/* enable PTA I2C mailbox  */
	u8tmp = btcoexist->btc_read_1byte(btcoexist, 0x101);
	u8tmp |= BIT4;
	btcoexist->btc_write_1byte(btcoexist, 0x101, u8tmp);

	/* enable bt clock when wifi is disabled. */
	u8tmp = btcoexist->btc_read_1byte(btcoexist, 0x93);
	u8tmp |= BIT0;
	btcoexist->btc_write_1byte(btcoexist, 0x93, u8tmp);
	/* enable bt clock when suspend. */
	u8tmp = btcoexist->btc_read_1byte(btcoexist, 0x7);
	u8tmp |= BIT0;
	btcoexist->btc_write_1byte(btcoexist, 0x7, u8tmp);
}

/*************************************************************
 *   work around function start with wa_halbtc8192e2ant_
 *************************************************************/

/************************************************************
 *   extern function start with EXhalbtc8192e2ant_
 ************************************************************/

void ex_halbtc8192e2ant_init_hwconfig(struct btc_coexist *btcoexist)
{
	halbtc8192e2ant_init_hwconfig(btcoexist, true);
}

void ex_halbtc8192e2ant_init_coex_dm(struct btc_coexist *btcoexist)
{
	btc_iface_dbg(INTF_INIT,
		      "[BTCoex], Coex Mechanism Init!!\n");
	halbtc8192e2ant_init_coex_dm(btcoexist);
}

void ex_halbtc8192e2ant_display_coex_info(struct btc_coexist *btcoexist)
{
	struct btc_board_info *board_info = &btcoexist->board_info;
	struct btc_stack_info *stack_info = &btcoexist->stack_info;
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 u8tmp[4], i, bt_info_ext, ps_tdma_case = 0;
	u16 u16tmp[4];
	u32 u32tmp[4];
	bool roam = false, scan = false, link = false, wifi_under_5g = false;
	bool bt_hson = false, wifi_busy = false;
	int wifirssi = 0, bt_hs_rssi = 0;
	u32 wifi_bw, wifi_traffic_dir;
	u8 wifi_dot11_chnl, wifi_hs_chnl;
	u32 fw_ver = 0, bt_patch_ver = 0;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
		   "\r\n ============[BT Coexist info]============");

	if (btcoexist->manual_control) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			   "\r\n ===========[Under Manual Control]===========");
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			   "\r\n ==========================================");
	}

	if (!board_info->bt_exist) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n BT not exists !!!");
		return;
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
		   "\r\n %-35s = %d/ %d ", "Ant PG number/ Ant mechanism:",
		   board_info->pg_ant_num, board_info->btdm_ant_num);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = %s / %d",
		   "BT stack/ hci ext ver",
		   ((stack_info->profile_notified) ? "Yes" : "No"),
		   stack_info->hci_version);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER, &bt_patch_ver);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
		   "\r\n %-35s = %d_%d/ 0x%x/ 0x%x(%d)",
		   "CoexVer/ FwVer/ PatchVer",
		   glcoex_ver_date_8192e_2ant, glcoex_ver_8192e_2ant,
		   fw_ver, bt_patch_ver, bt_patch_ver);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hson);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_DOT11_CHNL,
			   &wifi_dot11_chnl);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_HS_CHNL, &wifi_hs_chnl);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = %d / %d(%d)",
		   "Dot11 channel / HsMode(HsChnl)",
		   wifi_dot11_chnl, bt_hson, wifi_hs_chnl);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = %3ph ",
		   "H2C Wifi inform bt chnl Info", coex_dm->wifi_chnl_info);

	btcoexist->btc_get(btcoexist, BTC_GET_S4_WIFI_RSSI, &wifirssi);
	btcoexist->btc_get(btcoexist, BTC_GET_S4_HS_RSSI, &bt_hs_rssi);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = %d/ %d",
		   "Wifi rssi/ HS rssi", wifirssi, bt_hs_rssi);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = %d/ %d/ %d ",
		   "Wifi link/ roam/ scan", link, roam, scan);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION,
			   &wifi_traffic_dir);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = %s / %s/ %s ",
		   "Wifi status", (wifi_under_5g ? "5G" : "2.4G"),
		   ((BTC_WIFI_BW_LEGACY == wifi_bw) ? "Legacy" :
			(((BTC_WIFI_BW_HT40 == wifi_bw) ? "HT40" : "HT20"))),
		   ((!wifi_busy) ? "idle" :
			((BTC_WIFI_TRAFFIC_TX == wifi_traffic_dir) ?
				"uplink" : "downlink")));

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = [%s/ %d/ %d] ",
		   "BT [status/ rssi/ retryCnt]",
		   ((btcoexist->bt_info.bt_disabled) ? ("disabled") :
		    ((coex_sta->c2h_bt_inquiry_page) ?
		     ("inquiry/page scan") :
		      ((BT_8192E_2ANT_BT_STATUS_NON_CONNECTED_IDLE ==
			coex_dm->bt_status) ? "non-connected idle" :
			 ((BT_8192E_2ANT_BT_STATUS_CONNECTED_IDLE ==
			   coex_dm->bt_status) ? "connected-idle" : "busy")))),
		   coex_sta->bt_rssi, coex_sta->bt_retry_cnt);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = %d / %d / %d / %d",
		   "SCO/HID/PAN/A2DP", stack_info->sco_exist,
		   stack_info->hid_exist, stack_info->pan_exist,
		   stack_info->a2dp_exist);
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_BT_LINK_INFO);

	bt_info_ext = coex_sta->bt_info_ext;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = %s",
		   "BT Info A2DP rate",
		   (bt_info_ext&BIT0) ? "Basic rate" : "EDR rate");

	for (i = 0; i < BT_INFO_SRC_8192E_2ANT_MAX; i++) {
		if (coex_sta->bt_info_c2h_cnt[i]) {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
				   "\r\n %-35s = %7ph(%d)",
				   GLBtInfoSrc8192e2Ant[i],
				   coex_sta->bt_info_c2h[i],
				   coex_sta->bt_info_c2h_cnt[i]);
		}
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = %s/%s",
		   "PS state, IPS/LPS",
		   ((coex_sta->under_ips ? "IPS ON" : "IPS OFF")),
		   ((coex_sta->under_lps ? "LPS ON" : "LPS OFF")));
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_FW_PWR_MODE_CMD);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = 0x%x ", "SS Type",
		   coex_dm->cur_sstype);

	/* Sw mechanism	*/
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s",
		   "============[Sw mechanism]============");
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = %d/ %d/ %d ",
		   "SM1[ShRf/ LpRA/ LimDig]", coex_dm->cur_rf_rx_lpf_shrink,
		   coex_dm->cur_low_penalty_ra, coex_dm->limited_dig);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = %d/ %d/ %d(0x%x) ",
		   "SM2[AgcT/ AdcB/ SwDacSwing(lvl)]",
		   coex_dm->cur_agc_table_en, coex_dm->cur_adc_back_off,
		   coex_dm->cur_dac_swing_on, coex_dm->cur_dac_swing_lvl);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = 0x%x ", "Rate Mask",
		   btcoexist->bt_info.ra_mask);

	/* Fw mechanism	*/
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s",
		   "============[Fw mechanism]============");

	ps_tdma_case = coex_dm->cur_ps_tdma;
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
		   "\r\n %-35s = %5ph case-%d (auto:%d)",
		   "PS TDMA", coex_dm->ps_tdma_para,
		   ps_tdma_case, coex_dm->auto_tdma_adjust);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = %d/ %d ",
		   "DecBtPwr/ IgnWlanAct",
		   coex_dm->cur_dec_bt_pwr, coex_dm->cur_ignore_wlan_act);

	/* Hw setting */
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s",
		   "============[Hw setting]============");

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = 0x%x",
		   "RF-A, 0x1e initVal", coex_dm->bt_rf0x1e_backup);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = 0x%x/0x%x/0x%x/0x%x",
		   "backup ARFR1/ARFR2/RL/AMaxTime", coex_dm->backup_arfr_cnt1,
		   coex_dm->backup_arfr_cnt2, coex_dm->backup_retrylimit,
		   coex_dm->backup_ampdu_maxtime);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x430);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x434);
	u16tmp[0] = btcoexist->btc_read_2byte(btcoexist, 0x42a);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x456);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = 0x%x/0x%x/0x%x/0x%x",
		   "0x430/0x434/0x42a/0x456",
		   u32tmp[0], u32tmp[1], u16tmp[0], u8tmp[0]);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xc04);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0xd04);
	u32tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0x90c);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0xc04/ 0xd04/ 0x90c", u32tmp[0], u32tmp[1], u32tmp[2]);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x778);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = 0x%x", "0x778",
		   u8tmp[0]);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x92c);
	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x930);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = 0x%x/ 0x%x",
		   "0x92c/ 0x930", (u8tmp[0]), u32tmp[0]);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x40);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x4f);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = 0x%x/ 0x%x",
		   "0x40/ 0x4f", u8tmp[0], u8tmp[1]);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x550);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x522);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = 0x%x/ 0x%x",
		   "0x550(bcn ctrl)/0x522", u32tmp[0], u8tmp[0]);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xc50);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = 0x%x", "0xc50(dig)",
		   u32tmp[0]);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x6c0);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x6c4);
	u32tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0x6c8);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x6cc);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x",
		   "0x6c0/0x6c4/0x6c8/0x6cc(coexTable)",
		   u32tmp[0], u32tmp[1], u32tmp[2], u8tmp[0]);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = %d/ %d",
		   "0x770(hp rx[31:16]/tx[15:0])",
		   coex_sta->high_priority_rx, coex_sta->high_priority_tx);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, "\r\n %-35s = %d/ %d",
		   "0x774(lp rx[31:16]/tx[15:0])",
		   coex_sta->low_priority_rx, coex_sta->low_priority_tx);
#if (BT_AUTO_REPORT_ONLY_8192E_2ANT == 1)
	halbtc8192e2ant_monitor_bt_ctr(btcoexist);
#endif
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_COEX_STATISTICS);
}

void ex_halbtc8192e2ant_ips_notify(struct btc_coexist *btcoexist, u8 type)
{
	if (BTC_IPS_ENTER == type) {
		btc_iface_dbg(INTF_NOTIFY,
			      "[BTCoex], IPS ENTER notify\n");
		coex_sta->under_ips = true;
		halbtc8192e2ant_coex_alloff(btcoexist);
	} else if (BTC_IPS_LEAVE == type) {
		btc_iface_dbg(INTF_NOTIFY,
			      "[BTCoex], IPS LEAVE notify\n");
		coex_sta->under_ips = false;
	}
}

void ex_halbtc8192e2ant_lps_notify(struct btc_coexist *btcoexist, u8 type)
{
	if (BTC_LPS_ENABLE == type) {
		btc_iface_dbg(INTF_NOTIFY,
			      "[BTCoex], LPS ENABLE notify\n");
		coex_sta->under_lps = true;
	} else if (BTC_LPS_DISABLE == type) {
		btc_iface_dbg(INTF_NOTIFY,
			      "[BTCoex], LPS DISABLE notify\n");
		coex_sta->under_lps = false;
	}
}

void ex_halbtc8192e2ant_scan_notify(struct btc_coexist *btcoexist, u8 type)
{
	if (BTC_SCAN_START == type)
		btc_iface_dbg(INTF_NOTIFY,
			      "[BTCoex], SCAN START notify\n");
	else if (BTC_SCAN_FINISH == type)
		btc_iface_dbg(INTF_NOTIFY,
			      "[BTCoex], SCAN FINISH notify\n");
}

void ex_halbtc8192e2ant_connect_notify(struct btc_coexist *btcoexist, u8 type)
{
	if (BTC_ASSOCIATE_START == type)
		btc_iface_dbg(INTF_NOTIFY,
			      "[BTCoex], CONNECT START notify\n");
	else if (BTC_ASSOCIATE_FINISH == type)
		btc_iface_dbg(INTF_NOTIFY,
			      "[BTCoex], CONNECT FINISH notify\n");
}

void ex_halbtc8192e2ant_media_status_notify(struct btc_coexist *btcoexist,
					    u8 type)
{
	u8 h2c_parameter[3] = {0};
	u32 wifi_bw;
	u8 wifi_center_chnl;

	if (btcoexist->manual_control ||
	    btcoexist->stop_coex_dm ||
	    btcoexist->bt_info.bt_disabled)
		return;

	if (BTC_MEDIA_CONNECT == type)
		btc_iface_dbg(INTF_NOTIFY,
			      "[BTCoex], MEDIA connect notify\n");
	else
		btc_iface_dbg(INTF_NOTIFY,
			      "[BTCoex], MEDIA disconnect notify\n");

	/* only 2.4G we need to inform bt the chnl mask */
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_CENTRAL_CHNL,
			   &wifi_center_chnl);
	if ((BTC_MEDIA_CONNECT == type) &&
	    (wifi_center_chnl <= 14)) {
		h2c_parameter[0] = 0x1;
		h2c_parameter[1] = wifi_center_chnl;
		btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
		if (BTC_WIFI_BW_HT40 == wifi_bw)
			h2c_parameter[2] = 0x30;
		else
			h2c_parameter[2] = 0x20;
	}

	coex_dm->wifi_chnl_info[0] = h2c_parameter[0];
	coex_dm->wifi_chnl_info[1] = h2c_parameter[1];
	coex_dm->wifi_chnl_info[2] = h2c_parameter[2];

	btc_alg_dbg(ALGO_TRACE_FW_EXEC,
		    "[BTCoex], FW write 0x66 = 0x%x\n",
		    h2c_parameter[0] << 16 | h2c_parameter[1] << 8 |
		    h2c_parameter[2]);

	btcoexist->btc_fill_h2c(btcoexist, 0x66, 3, h2c_parameter);
}

void ex_halbtc8192e2ant_special_packet_notify(struct btc_coexist *btcoexist,
					      u8 type)
{
	if (type == BTC_PACKET_DHCP)
		btc_iface_dbg(INTF_NOTIFY,
			      "[BTCoex], DHCP Packet notify\n");
}

void ex_halbtc8192e2ant_bt_info_notify(struct btc_coexist *btcoexist,
				       u8 *tmp_buf, u8 length)
{
	u8 bt_info = 0;
	u8 i, rsp_source = 0;
	bool bt_busy = false, limited_dig = false;
	bool wifi_connected = false;

	coex_sta->c2h_bt_info_req_sent = false;

	rsp_source = tmp_buf[0] & 0xf;
	if (rsp_source >= BT_INFO_SRC_8192E_2ANT_MAX)
		rsp_source = BT_INFO_SRC_8192E_2ANT_WIFI_FW;
	coex_sta->bt_info_c2h_cnt[rsp_source]++;

	btc_iface_dbg(INTF_NOTIFY,
		      "[BTCoex], Bt info[%d], length=%d, hex data = [",
		      rsp_source, length);
	for (i = 0; i < length; i++) {
		coex_sta->bt_info_c2h[rsp_source][i] = tmp_buf[i];
		if (i == 1)
			bt_info = tmp_buf[i];
		if (i == length-1)
			btc_iface_dbg(INTF_NOTIFY,
				      "0x%02x]\n", tmp_buf[i]);
		else
			btc_iface_dbg(INTF_NOTIFY,
				      "0x%02x, ", tmp_buf[i]);
	}

	if (BT_INFO_SRC_8192E_2ANT_WIFI_FW != rsp_source) {
		coex_sta->bt_retry_cnt =	/* [3:0] */
			coex_sta->bt_info_c2h[rsp_source][2] & 0xf;

		coex_sta->bt_rssi =
			coex_sta->bt_info_c2h[rsp_source][3] * 2 + 10;

		coex_sta->bt_info_ext =
			coex_sta->bt_info_c2h[rsp_source][4];

		/* Here we need to resend some wifi info to BT
		 * because bt is reset and loss of the info.
		 */
		if ((coex_sta->bt_info_ext & BIT1)) {
			btc_alg_dbg(ALGO_TRACE,
				    "bit1, send wifi BW&Chnl to BT!!\n");
			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
					   &wifi_connected);
			if (wifi_connected)
				ex_halbtc8192e2ant_media_status_notify(
							btcoexist,
							BTC_MEDIA_CONNECT);
			else
				ex_halbtc8192e2ant_media_status_notify(
							btcoexist,
							BTC_MEDIA_DISCONNECT);
		}

		if ((coex_sta->bt_info_ext & BIT3)) {
			if (!btcoexist->manual_control &&
			    !btcoexist->stop_coex_dm) {
				btc_alg_dbg(ALGO_TRACE,
					    "bit3, BT NOT ignore Wlan active!\n");
				halbtc8192e2ant_IgnoreWlanAct(btcoexist,
							      FORCE_EXEC,
							      false);
			}
		} else {
			/* BT already NOT ignore Wlan active,
			 * do nothing here.
			 */
		}

#if (BT_AUTO_REPORT_ONLY_8192E_2ANT == 0)
		if ((coex_sta->bt_info_ext & BIT4)) {
			/* BT auto report already enabled, do nothing */
		} else {
			halbtc8192e2ant_bt_autoreport(btcoexist, FORCE_EXEC,
						      true);
		}
#endif
	}

	/* check BIT2 first ==> check if bt is under inquiry or page scan */
	if (bt_info & BT_INFO_8192E_2ANT_B_INQ_PAGE)
		coex_sta->c2h_bt_inquiry_page = true;
	else
		coex_sta->c2h_bt_inquiry_page = false;

	/* set link exist status */
	if (!(bt_info&BT_INFO_8192E_2ANT_B_CONNECTION)) {
		coex_sta->bt_link_exist = false;
		coex_sta->pan_exist = false;
		coex_sta->a2dp_exist = false;
		coex_sta->hid_exist = false;
		coex_sta->sco_exist = false;
	} else {/* connection exists */
		coex_sta->bt_link_exist = true;
		if (bt_info & BT_INFO_8192E_2ANT_B_FTP)
			coex_sta->pan_exist = true;
		else
			coex_sta->pan_exist = false;
		if (bt_info & BT_INFO_8192E_2ANT_B_A2DP)
			coex_sta->a2dp_exist = true;
		else
			coex_sta->a2dp_exist = false;
		if (bt_info & BT_INFO_8192E_2ANT_B_HID)
			coex_sta->hid_exist = true;
		else
			coex_sta->hid_exist = false;
		if (bt_info & BT_INFO_8192E_2ANT_B_SCO_ESCO)
			coex_sta->sco_exist = true;
		else
			coex_sta->sco_exist = false;
	}

	halbtc8192e2ant_update_btlink_info(btcoexist);

	if (!(bt_info&BT_INFO_8192E_2ANT_B_CONNECTION)) {
		coex_dm->bt_status = BT_8192E_2ANT_BT_STATUS_NON_CONNECTED_IDLE;
		btc_alg_dbg(ALGO_TRACE,
			    "[BTCoex], BT Non-Connected idle!!!\n");
	} else if (bt_info == BT_INFO_8192E_2ANT_B_CONNECTION) {
		coex_dm->bt_status = BT_8192E_2ANT_BT_STATUS_CONNECTED_IDLE;
		btc_alg_dbg(ALGO_TRACE,
			    "[BTCoex], bt_infoNotify(), BT Connected-idle!!!\n");
	} else if ((bt_info&BT_INFO_8192E_2ANT_B_SCO_ESCO) ||
		   (bt_info&BT_INFO_8192E_2ANT_B_SCO_BUSY)) {
		coex_dm->bt_status = BT_8192E_2ANT_BT_STATUS_SCO_BUSY;
		btc_alg_dbg(ALGO_TRACE,
			    "[BTCoex], bt_infoNotify(), BT SCO busy!!!\n");
	} else if (bt_info&BT_INFO_8192E_2ANT_B_ACL_BUSY) {
		coex_dm->bt_status = BT_8192E_2ANT_BT_STATUS_ACL_BUSY;
		btc_alg_dbg(ALGO_TRACE,
			    "[BTCoex], bt_infoNotify(), BT ACL busy!!!\n");
	} else {
		coex_dm->bt_status = BT_8192E_2ANT_BT_STATUS_MAX;
		btc_alg_dbg(ALGO_TRACE,
			    "[BTCoex]bt_infoNotify(), BT Non-Defined state!!!\n");
	}

	if ((BT_8192E_2ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) ||
	    (BT_8192E_2ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
	    (BT_8192E_2ANT_BT_STATUS_ACL_SCO_BUSY == coex_dm->bt_status)) {
		bt_busy = true;
		limited_dig = true;
	} else {
		bt_busy = false;
		limited_dig = false;
	}

	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bt_busy);

	coex_dm->limited_dig = limited_dig;
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_LIMITED_DIG, &limited_dig);

	halbtc8192e2ant_run_coexist_mechanism(btcoexist);
}

void ex_halbtc8192e2ant_stack_operation_notify(struct btc_coexist *btcoexist,
					       u8 type)
{
}

void ex_halbtc8192e2ant_halt_notify(struct btc_coexist *btcoexist)
{
	btc_iface_dbg(INTF_NOTIFY, "[BTCoex], Halt notify\n");

	halbtc8192e2ant_IgnoreWlanAct(btcoexist, FORCE_EXEC, true);
	ex_halbtc8192e2ant_media_status_notify(btcoexist, BTC_MEDIA_DISCONNECT);
}

void ex_halbtc8192e2ant_periodical(struct btc_coexist *btcoexist)
{
	static u8 dis_ver_info_cnt;
	u32 fw_ver = 0, bt_patch_ver = 0;
	struct btc_board_info *board_info = &btcoexist->board_info;
	struct btc_stack_info *stack_info = &btcoexist->stack_info;

	btc_alg_dbg(ALGO_TRACE,
		    "=======================Periodical=======================\n");
	if (dis_ver_info_cnt <= 5) {
		dis_ver_info_cnt += 1;
		btc_iface_dbg(INTF_INIT,
			      "************************************************\n");
		btc_iface_dbg(INTF_INIT,
			      "Ant PG Num/ Ant Mech/ Ant Pos = %d/ %d/ %d\n",
			      board_info->pg_ant_num, board_info->btdm_ant_num,
			      board_info->btdm_ant_pos);
		btc_iface_dbg(INTF_INIT,
			      "BT stack/ hci ext ver = %s / %d\n",
			      ((stack_info->profile_notified) ? "Yes" : "No"),
			      stack_info->hci_version);
		btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER,
				   &bt_patch_ver);
		btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
		btc_iface_dbg(INTF_INIT,
			      "CoexVer/ FwVer/ PatchVer = %d_%x/ 0x%x/ 0x%x(%d)\n",
			      glcoex_ver_date_8192e_2ant, glcoex_ver_8192e_2ant,
			      fw_ver, bt_patch_ver, bt_patch_ver);
		btc_iface_dbg(INTF_INIT,
			      "************************************************\n");
	}

#if (BT_AUTO_REPORT_ONLY_8192E_2ANT == 0)
	halbtc8192e2ant_querybt_info(btcoexist);
	halbtc8192e2ant_monitor_bt_ctr(btcoexist);
	btc8192e2ant_monitor_bt_enable_dis(btcoexist);
#else
	if (halbtc8192e2ant_iswifi_status_changed(btcoexist) ||
	    coex_dm->auto_tdma_adjust)
		halbtc8192e2ant_run_coexist_mechanism(btcoexist);
#endif
}
