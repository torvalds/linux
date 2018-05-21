/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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
/* ************************************************************
 * Description:
 *
 * This file is for RTL8192E Co-exist mechanism
 *
 * History
 * 2012/11/15 Cosa first check in.
 *
 * ************************************************************ */

/* ************************************************************
 * include files
 * ************************************************************ */
#include "mp_precomp.h"


#if (BT_SUPPORT == 1 && COEX_SUPPORT == 1)

#if (RTL8192E_SUPPORT == 1)
/* ************************************************************
 * Global variables, these are static variables
 * ************************************************************ */
static u8	 *trace_buf = &gl_btc_trace_buf[0];
static struct  coex_dm_8192e_2ant		glcoex_dm_8192e_2ant;
static struct  coex_dm_8192e_2ant	*coex_dm = &glcoex_dm_8192e_2ant;
static struct  coex_sta_8192e_2ant		glcoex_sta_8192e_2ant;
static struct  coex_sta_8192e_2ant	*coex_sta = &glcoex_sta_8192e_2ant;

const char *const glbt_info_src_8192e_2ant[] = {
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};
/* ************************************************************
 * BtCoex Version Format:
 * 1. date :			glcoex_ver_date_XXXXX_1ant
 * 2. WifiCoexVersion : glcoex_ver_XXXX_1ant
 * 3. BtCoexVersion :	glcoex_ver_btdesired_XXXXX_1ant
 * 4. others :			glcoex_ver_XXXXXX_XXXXX_1ant
 *
 * Variable should be indicated IC and Antenna numbers !!!
 * Please strictly follow this order and naming style !!!
 *
 * ************************************************************ */
u32	glcoex_ver_date_8192e_2ant = 20170113;
u32	glcoex_ver_8192e_2ant = 0x46;
u32	glcoex_ver_btdesired_8192e_2ant = 0x04;
/*1.fix miracast issue*/
/*2.update BT FW for power on latch issue*/
/*3. move the function of polling BT version to ex_halbtc8192e2ant_display_coex_info*/
/* ************************************************************
 * local function proto type if needed
 * ************************************************************
 * ************************************************************
 * local function start with halbtc8192e2ant_
 * ************************************************************ */
u8 halbtc8192e2ant_bt_rssi_state(u8 level_num, u8 rssi_thresh, u8 rssi_thresh1)
{
	s32			bt_rssi = 0;
	u8			bt_rssi_state = coex_sta->pre_bt_rssi_state;

	bt_rssi = coex_sta->bt_rssi;

	if (level_num == 2) {
		if ((coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_bt_rssi_state ==
		     BTC_RSSI_STATE_STAY_LOW)) {
			if (bt_rssi >= (rssi_thresh +
					BTC_RSSI_COEX_THRESH_TOL_8192E_2ANT))
				bt_rssi_state = BTC_RSSI_STATE_HIGH;
			else
				bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
		} else {
			if (bt_rssi < rssi_thresh)
				bt_rssi_state = BTC_RSSI_STATE_LOW;
			else
				bt_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
		}
	} else if (level_num == 3) {
		if (rssi_thresh > rssi_thresh1) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], BT Rssi thresh error!!\n");
			BTC_TRACE(trace_buf);
			return coex_sta->pre_bt_rssi_state;
		}

		if ((coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_bt_rssi_state ==
		     BTC_RSSI_STATE_STAY_LOW)) {
			if (bt_rssi >= (rssi_thresh +
					BTC_RSSI_COEX_THRESH_TOL_8192E_2ANT))
				bt_rssi_state = BTC_RSSI_STATE_MEDIUM;
			else
				bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
		} else if ((coex_sta->pre_bt_rssi_state ==
			    BTC_RSSI_STATE_MEDIUM) ||
			   (coex_sta->pre_bt_rssi_state ==
			    BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (bt_rssi >= (rssi_thresh1 +
					BTC_RSSI_COEX_THRESH_TOL_8192E_2ANT))
				bt_rssi_state = BTC_RSSI_STATE_HIGH;
			else if (bt_rssi < rssi_thresh)
				bt_rssi_state = BTC_RSSI_STATE_LOW;
			else
				bt_rssi_state = BTC_RSSI_STATE_STAY_MEDIUM;
		} else {
			if (bt_rssi < rssi_thresh1)
				bt_rssi_state = BTC_RSSI_STATE_MEDIUM;
			else
				bt_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
		}
	}

	coex_sta->pre_bt_rssi_state = bt_rssi_state;

	return bt_rssi_state;
}

u8 halbtc8192e2ant_wifi_rssi_state(IN struct btc_coexist *btcoexist,
	   IN u8 index, IN u8 level_num, IN u8 rssi_thresh, IN u8 rssi_thresh1)
{
	s32			wifi_rssi = 0;
	u8			wifi_rssi_state = coex_sta->pre_wifi_rssi_state[index];

	btcoexist->btc_get(btcoexist, BTC_GET_S4_WIFI_RSSI, &wifi_rssi);

	if (level_num == 2) {
		if ((coex_sta->pre_wifi_rssi_state[index] == BTC_RSSI_STATE_LOW)
		    ||
		    (coex_sta->pre_wifi_rssi_state[index] ==
		     BTC_RSSI_STATE_STAY_LOW)) {
			if (wifi_rssi >= (rssi_thresh +
					  BTC_RSSI_COEX_THRESH_TOL_8192E_2ANT))
				wifi_rssi_state = BTC_RSSI_STATE_HIGH;
			else
				wifi_rssi_state = BTC_RSSI_STATE_STAY_LOW;
		} else {
			if (wifi_rssi < rssi_thresh)
				wifi_rssi_state = BTC_RSSI_STATE_LOW;
			else
				wifi_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
		}
	} else if (level_num == 3) {
		if (rssi_thresh > rssi_thresh1) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], wifi RSSI thresh error!!\n");
			BTC_TRACE(trace_buf);
			return coex_sta->pre_wifi_rssi_state[index];
		}

		if ((coex_sta->pre_wifi_rssi_state[index] == BTC_RSSI_STATE_LOW)
		    ||
		    (coex_sta->pre_wifi_rssi_state[index] ==
		     BTC_RSSI_STATE_STAY_LOW)) {
			if (wifi_rssi >= (rssi_thresh +
					  BTC_RSSI_COEX_THRESH_TOL_8192E_2ANT))
				wifi_rssi_state = BTC_RSSI_STATE_MEDIUM;
			else
				wifi_rssi_state = BTC_RSSI_STATE_STAY_LOW;
		} else if ((coex_sta->pre_wifi_rssi_state[index] ==
			    BTC_RSSI_STATE_MEDIUM) ||
			   (coex_sta->pre_wifi_rssi_state[index] ==
			    BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (wifi_rssi >= (rssi_thresh1 +
					  BTC_RSSI_COEX_THRESH_TOL_8192E_2ANT))
				wifi_rssi_state = BTC_RSSI_STATE_HIGH;
			else if (wifi_rssi < rssi_thresh)
				wifi_rssi_state = BTC_RSSI_STATE_LOW;
			else
				wifi_rssi_state = BTC_RSSI_STATE_STAY_MEDIUM;
		} else {
			if (wifi_rssi < rssi_thresh1)
				wifi_rssi_state = BTC_RSSI_STATE_MEDIUM;
			else
				wifi_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
		}
	}

	coex_sta->pre_wifi_rssi_state[index] = wifi_rssi_state;

	return wifi_rssi_state;
}

u32 halbtc8192e2ant_decide_ra_mask(IN struct btc_coexist *btcoexist,
				   IN u8 ss_type, IN u32 ra_mask_type)
{
	u32	dis_ra_mask = 0x0;

	switch (ra_mask_type) {
	case 0: /* normal mode */
		if (ss_type == 2)
			dis_ra_mask = 0x0;			/* enable 2ss */
		else
			dis_ra_mask = 0xfff00000;		/* disable 2ss */
		break;
	case 1: /* disable cck 1/2 */
		if (ss_type == 2)
			dis_ra_mask = 0x00000003;		/* enable 2ss */
		else
			dis_ra_mask = 0xfff00003;		/* disable 2ss */
		break;
	case 2: /* disable cck 1/2/5.5, ofdm 6/9/12/18/24, mcs 0/1/2/3/4 */
		if (ss_type == 2)
			dis_ra_mask = 0x0001f1f7;		/* enable 2ss */
		else
			dis_ra_mask = 0xfff1f1f7;		/* disable 2ss */
		break;
	default:
		break;
	}

	return dis_ra_mask;
}

void halbtc8192e2ant_update_ra_mask(IN struct btc_coexist *btcoexist,
				    IN boolean force_exec, IN u32 dis_rate_mask)
{
	coex_dm->cur_ra_mask = dis_rate_mask;

	if (force_exec || (coex_dm->pre_ra_mask != coex_dm->cur_ra_mask))
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_UPDATE_RAMASK,
				   &coex_dm->cur_ra_mask);
	coex_dm->pre_ra_mask = coex_dm->cur_ra_mask;
}

void halbtc8192e2ant_auto_rate_fallback_retry(IN struct btc_coexist *btcoexist,
		IN boolean force_exec, IN u8 type)
{
	boolean	wifi_under_b_mode = false;

	coex_dm->cur_arfr_type = type;

	if (force_exec || (coex_dm->pre_arfr_type != coex_dm->cur_arfr_type)) {
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
				btcoexist->btc_write_4byte(btcoexist,
							   0x430, 0x0);
				btcoexist->btc_write_4byte(btcoexist,
							   0x434, 0x01010101);
			} else {
				btcoexist->btc_write_4byte(btcoexist,
							   0x430, 0x0);
				btcoexist->btc_write_4byte(btcoexist,
							   0x434, 0x04030201);
			}
			break;
		default:
			break;
		}
	}

	coex_dm->pre_arfr_type = coex_dm->cur_arfr_type;
}

void halbtc8192e2ant_retry_limit(IN struct btc_coexist *btcoexist,
				 IN boolean force_exec, IN u8 type)
{
	coex_dm->cur_retry_limit_type = type;

	if (force_exec ||
	    (coex_dm->pre_retry_limit_type !=
	     coex_dm->cur_retry_limit_type)) {
		switch (coex_dm->cur_retry_limit_type) {
		case 0:	/* normal mode */
			btcoexist->btc_write_2byte(btcoexist, 0x42a,
						   coex_dm->backup_retry_limit);
			break;
		case 1:	/* retry limit=8 */
			btcoexist->btc_write_2byte(btcoexist, 0x42a,
						   0x0808);
			break;
		default:
			break;
		}
	}

	coex_dm->pre_retry_limit_type = coex_dm->cur_retry_limit_type;
}

void halbtc8192e2ant_ampdu_max_time(IN struct btc_coexist *btcoexist,
				    IN boolean force_exec, IN u8 type)
{
	coex_dm->cur_ampdu_time_type = type;

	if (force_exec ||
	    (coex_dm->pre_ampdu_time_type != coex_dm->cur_ampdu_time_type)) {
		switch (coex_dm->cur_ampdu_time_type) {
		case 0:	/* normal mode */
			btcoexist->btc_write_1byte(btcoexist, 0x456,
					   coex_dm->backup_ampdu_max_time);
			break;
		case 1:	/* AMPDU timw = 0x38 * 32us */
			btcoexist->btc_write_1byte(btcoexist, 0x456,
						   0x38);
			break;
		default:
			break;
		}
	}

	coex_dm->pre_ampdu_time_type = coex_dm->cur_ampdu_time_type;
}

void halbtc8192e2ant_limited_tx(IN struct btc_coexist *btcoexist,
		IN boolean force_exec, IN u8 ra_mask_type, IN u8 arfr_type,
				IN u8 retry_limit_type, IN u8 ampdu_time_type)
{
	u32	dis_ra_mask = 0x0;

	coex_dm->cur_ra_mask_type = ra_mask_type;
	dis_ra_mask = halbtc8192e2ant_decide_ra_mask(btcoexist,
			coex_dm->cur_ss_type, ra_mask_type);
	halbtc8192e2ant_update_ra_mask(btcoexist, force_exec, dis_ra_mask);

	halbtc8192e2ant_auto_rate_fallback_retry(btcoexist, force_exec,
			arfr_type);
	halbtc8192e2ant_retry_limit(btcoexist, force_exec, retry_limit_type);
	halbtc8192e2ant_ampdu_max_time(btcoexist, force_exec, ampdu_time_type);
}

void halbtc8192e2ant_limited_rx(IN struct btc_coexist *btcoexist,
			IN boolean force_exec, IN boolean rej_ap_agg_pkt,
			IN boolean bt_ctrl_agg_buf_size, IN u8 agg_buf_size)
{
	boolean	reject_rx_agg = rej_ap_agg_pkt;
	boolean	bt_ctrl_rx_agg_size = bt_ctrl_agg_buf_size;
	u8	rx_agg_size = agg_buf_size;

	/* ============================================ */
	/*	Rx Aggregation related setting */
	/* ============================================ */
	btcoexist->btc_set(btcoexist, BTC_SET_BL_TO_REJ_AP_AGG_PKT,
			   &reject_rx_agg);
	/* decide BT control aggregation buf size or not */
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_CTRL_AGG_SIZE,
			   &bt_ctrl_rx_agg_size);
	/* aggregation buf size, only work when BT control Rx aggregation size. */
	btcoexist->btc_set(btcoexist, BTC_SET_U1_AGG_BUF_SIZE, &rx_agg_size);
	/* real update aggregation setting */
	btcoexist->btc_set(btcoexist, BTC_SET_ACT_AGGREGATE_CTRL, NULL);


}

void halbtc8192e2ant_monitor_bt_ctr(IN struct btc_coexist *btcoexist)
{
	u32			reg_hp_txrx, reg_lp_txrx, u32tmp;
	u32			reg_hp_tx = 0, reg_hp_rx = 0, reg_lp_tx = 0, reg_lp_rx = 0;

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

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], High Priority Tx/Rx (reg 0x%x)=0x%x(%d)/0x%x(%d)\n",
		    reg_hp_txrx, reg_hp_tx, reg_hp_tx, reg_hp_rx, reg_hp_rx);
	BTC_TRACE(trace_buf);
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], Low Priority Tx/Rx (reg 0x%x)=0x%x(%d)/0x%x(%d)\n",
		    reg_lp_txrx, reg_lp_tx, reg_lp_tx, reg_lp_rx, reg_lp_rx);
	BTC_TRACE(trace_buf);

	/* reset counter */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0xc);
}


void halbtc8192e2ant_monitor_wifi_ctr(IN struct btc_coexist *btcoexist)
{
#if 1

	coex_sta->crc_ok_cck =
		btcoexist->btc_phydm_query_PHY_counter(
			btcoexist,
			PHYDM_INFO_CRC32_OK_CCK);
	coex_sta->crc_ok_11g =
		btcoexist->btc_phydm_query_PHY_counter(
			btcoexist,
			PHYDM_INFO_CRC32_OK_LEGACY);
	coex_sta->crc_ok_11n =
		btcoexist->btc_phydm_query_PHY_counter(
			btcoexist,
			PHYDM_INFO_CRC32_OK_HT);
	coex_sta->crc_ok_11n_vht =
		btcoexist->btc_phydm_query_PHY_counter(
			btcoexist,
			PHYDM_INFO_CRC32_OK_VHT);

	coex_sta->crc_err_cck =
		btcoexist->btc_phydm_query_PHY_counter(
			btcoexist,
			PHYDM_INFO_CRC32_ERROR_CCK);
	coex_sta->crc_err_11g =
		btcoexist->btc_phydm_query_PHY_counter(
			btcoexist,
			PHYDM_INFO_CRC32_ERROR_LEGACY);
	coex_sta->crc_err_11n =
		btcoexist->btc_phydm_query_PHY_counter(
			btcoexist,
			PHYDM_INFO_CRC32_ERROR_HT);
	coex_sta->crc_err_11n_vht =
		btcoexist->btc_phydm_query_PHY_counter(
			btcoexist,
			PHYDM_INFO_CRC32_ERROR_VHT);
#endif
}



void halbtc8192e2ant_query_bt_info(IN struct btc_coexist *btcoexist)
{
	u8			h2c_parameter[1] = {0};

	coex_sta->c2h_bt_info_req_sent = true;

	h2c_parameter[0] |= BIT(0);	/* trigger */

	btcoexist->btc_fill_h2c(btcoexist, 0x61, 1, h2c_parameter);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex],Query BT info!!!! H2C 0x61 = 0x1\n");
	BTC_TRACE(trace_buf);
}

boolean halbtc8192e2ant_is_wifi_status_changed(IN struct btc_coexist *btcoexist)
{
	static boolean	pre_wifi_busy = false, pre_under_4way = false,
			pre_bt_hs_on = false;
	boolean	wifi_busy = false, under_4way = false, bt_hs_on = false;
	boolean	wifi_connected = false;
	u8	wifi_rssi_state = BTC_RSSI_STATE_HIGH;


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

		wifi_rssi_state = halbtc8192e2ant_wifi_rssi_state(btcoexist, 0,
				  2, 34, 0);

		if ((BTC_RSSI_STATE_HIGH == wifi_rssi_state) ||
		    (BTC_RSSI_STATE_LOW == wifi_rssi_state))
			return true;
	}

	return false;
}

void halbtc8192e2ant_update_bt_link_info(IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info	*bt_link_info = &btcoexist->bt_link_info;
	boolean				bt_hs_on = false;

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

u8 halbtc8192e2ant_action_algorithm(IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info	*bt_link_info = &btcoexist->bt_link_info;
	struct  btc_stack_info	*stack_info = &btcoexist->stack_info;
	boolean				bt_hs_on = false;
	u8				algorithm = BT_8192E_2ANT_COEX_ALGO_UNDEFINED;
	u8				num_of_diff_profile = 0;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);

	if (!bt_link_info->bt_link_exist) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], No BT link exists!!!\n");
		BTC_TRACE(trace_buf);
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
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], SCO only\n");
			BTC_TRACE(trace_buf);
			algorithm = BT_8192E_2ANT_COEX_ALGO_SCO;
		} else {
			if (bt_link_info->hid_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					    "[BTCoex], HID only\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8192E_2ANT_COEX_ALGO_HID;
			} else if (bt_link_info->a2dp_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					    "[BTCoex], A2DP only\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8192E_2ANT_COEX_ALGO_A2DP;
			} else if (bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						    "[BTCoex], PAN(HS) only\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8192E_2ANT_COEX_ALGO_PANHS;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], PAN(EDR) only\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8192E_2ANT_COEX_ALGO_PANEDR;
				}
			}
		}
	} else if (num_of_diff_profile == 2) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					    "[BTCoex], SCO + HID\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8192E_2ANT_COEX_ALGO_SCO;
			} else if (bt_link_info->a2dp_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					    "[BTCoex], SCO + A2DP ==> SCO\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8192E_2ANT_COEX_ALGO_PANEDR_HID;
			} else if (bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm = BT_8192E_2ANT_COEX_ALGO_SCO;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8192E_2ANT_COEX_ALGO_SCO_PAN;
				}
			}
		} else {
			if (bt_link_info->hid_exist &&
			    bt_link_info->a2dp_exist) {
				if (stack_info->num_of_hid >= 2) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						    "[BTCoex], HID*2 + A2DP\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8192E_2ANT_COEX_ALGO_HID_A2DP_PANEDR;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						    "[BTCoex], HID + A2DP\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8192E_2ANT_COEX_ALGO_HID_A2DP;
				}
			} else if (bt_link_info->hid_exist &&
				   bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], HID + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm = BT_8192E_2ANT_COEX_ALGO_HID;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], HID + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8192E_2ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (bt_link_info->pan_exist &&
				   bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], A2DP + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8192E_2ANT_COEX_ALGO_A2DP_PANHS;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], A2DP + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8192E_2ANT_COEX_ALGO_PANEDR_A2DP;
				}
			}
		}
	} else if (num_of_diff_profile == 3) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist &&
			    bt_link_info->a2dp_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], SCO + HID + A2DP ==> HID\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8192E_2ANT_COEX_ALGO_PANEDR_HID;
			} else if (bt_link_info->hid_exist &&
				   bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + HID + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm = BT_8192E_2ANT_COEX_ALGO_SCO;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + HID + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8192E_2ANT_COEX_ALGO_SCO_PAN;
				}
			} else if (bt_link_info->pan_exist &&
				   bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + A2DP + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm = BT_8192E_2ANT_COEX_ALGO_SCO;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + A2DP + PAN(EDR) ==> HID\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8192E_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (bt_link_info->hid_exist &&
			    bt_link_info->pan_exist &&
			    bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], HID + A2DP + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8192E_2ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], HID + A2DP + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8192E_2ANT_COEX_ALGO_HID_A2DP_PANEDR;
				}
			}
		}
	} else if (num_of_diff_profile >= 3) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist &&
			    bt_link_info->pan_exist &&
			    bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], Error!!! SCO + HID + A2DP + PAN(HS)\n");
					BTC_TRACE(trace_buf);
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + HID + A2DP + PAN(EDR)==>PAN(EDR)+HID\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8192E_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
	}

	return algorithm;
}

void halbtc8192e2ant_set_fw_dac_swing_level(IN struct btc_coexist *btcoexist,
		IN u8 dac_swing_lvl)
{
	u8			h2c_parameter[1] = {0};

	/* There are several type of dacswing */
	/* 0x18/ 0x10/ 0xc/ 0x8/ 0x4/ 0x6 */
	h2c_parameter[0] = dac_swing_lvl;

	btcoexist->btc_fill_h2c(btcoexist, 0x64, 1, h2c_parameter);
}

void halbtc8192e2ant_set_fw_dec_bt_pwr(IN struct btc_coexist *btcoexist,
				       IN u8 dec_bt_pwr_lvl)
{
	u8			h2c_parameter[1] = {0};

	h2c_parameter[0] = dec_bt_pwr_lvl;

	btcoexist->btc_fill_h2c(btcoexist, 0x62, 1, h2c_parameter);
}

void halbtc8192e2ant_dec_bt_pwr(IN struct btc_coexist *btcoexist,
				IN boolean force_exec, IN u8 dec_bt_pwr_lvl)
{
	coex_dm->cur_bt_dec_pwr_lvl = dec_bt_pwr_lvl;

	if (!force_exec) {
#if 0	/* work around, avoid h2c command fail. */
		if (coex_dm->pre_bt_dec_pwr_lvl == coex_dm->cur_bt_dec_pwr_lvl)
			return;
#endif
	}
	halbtc8192e2ant_set_fw_dec_bt_pwr(btcoexist,
					  coex_dm->cur_bt_dec_pwr_lvl);

	coex_dm->pre_bt_dec_pwr_lvl = coex_dm->cur_bt_dec_pwr_lvl;
}

void halbtc8192e2ant_set_bt_auto_report(IN struct btc_coexist *btcoexist,
					IN boolean enable_auto_report)
{
	u8			h2c_parameter[1] = {0};

	h2c_parameter[0] = 0;

	if (enable_auto_report)
		h2c_parameter[0] |= BIT(0);

	btcoexist->btc_fill_h2c(btcoexist, 0x68, 1, h2c_parameter);
}

void halbtc8192e2ant_bt_auto_report(IN struct btc_coexist *btcoexist,
		    IN boolean force_exec, IN boolean enable_auto_report)
{
	coex_dm->cur_bt_auto_report = enable_auto_report;

	if (!force_exec) {
		if (coex_dm->pre_bt_auto_report == coex_dm->cur_bt_auto_report)
			return;
	}
	halbtc8192e2ant_set_bt_auto_report(btcoexist,
					   coex_dm->cur_bt_auto_report);

	coex_dm->pre_bt_auto_report = coex_dm->cur_bt_auto_report;
}

void halbtc8192e2ant_fw_dac_swing_lvl(IN struct btc_coexist *btcoexist,
			      IN boolean force_exec, IN u8 fw_dac_swing_lvl)
{
	coex_dm->cur_fw_dac_swing_lvl = fw_dac_swing_lvl;

	if (!force_exec) {
		if (coex_dm->pre_fw_dac_swing_lvl ==
		    coex_dm->cur_fw_dac_swing_lvl)
			return;
	}

	halbtc8192e2ant_set_fw_dac_swing_level(btcoexist,
					       coex_dm->cur_fw_dac_swing_lvl);

	coex_dm->pre_fw_dac_swing_lvl = coex_dm->cur_fw_dac_swing_lvl;
}

void halbtc8192e2ant_set_sw_rf_rx_lpf_corner(IN struct btc_coexist *btcoexist,
		IN boolean rx_rf_shrink_on)
{
	if (rx_rf_shrink_on) {
		/* Shrink RF Rx LPF corner */
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Shrink RF Rx LPF corner!!\n");
		BTC_TRACE(trace_buf);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1e, 0xfffff,
					  0xffffc);
	} else {
		/* Resume RF Rx LPF corner */
		/* After initialized, we can use coex_dm->bt_rf_0x1e_backup */
		if (btcoexist->initilized) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Resume RF Rx LPF corner!!\n");
			BTC_TRACE(trace_buf);
			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1e,
					  0xfffff, coex_dm->bt_rf_0x1e_backup);
		}
	}
}

void halbtc8192e2ant_rf_shrink(IN struct btc_coexist *btcoexist,
		       IN boolean force_exec, IN boolean rx_rf_shrink_on)
{
	coex_dm->cur_rf_rx_lpf_shrink = rx_rf_shrink_on;

	if (!force_exec) {
		if (coex_dm->pre_rf_rx_lpf_shrink ==
		    coex_dm->cur_rf_rx_lpf_shrink)
			return;
	}
	halbtc8192e2ant_set_sw_rf_rx_lpf_corner(btcoexist,
						coex_dm->cur_rf_rx_lpf_shrink);

	coex_dm->pre_rf_rx_lpf_shrink = coex_dm->cur_rf_rx_lpf_shrink;
}

void halbtc8192e2ant_set_sw_penalty_tx_rate_adaptive(IN struct btc_coexist
		*btcoexist, IN boolean low_penalty_ra)
{
	u8			h2c_parameter[6] = {0};

	h2c_parameter[0] = 0x6;	/* op_code, 0x6= Retry_Penalty */

	if (low_penalty_ra) {
		h2c_parameter[1] |= BIT(0);
		h2c_parameter[2] =
			0x00;  /* normal rate except MCS7/6/5, OFDM54/48/36 */
		h2c_parameter[3] = 0xf7;  /* MCS7 or OFDM54 */
		h2c_parameter[4] = 0xf8;  /* MCS6 or OFDM48 */
		h2c_parameter[5] = 0xf9;	/* MCS5 or OFDM36	 */
	}

	btcoexist->btc_fill_h2c(btcoexist, 0x69, 6, h2c_parameter);
}

void halbtc8192e2ant_low_penalty_ra(IN struct btc_coexist *btcoexist,
			    IN boolean force_exec, IN boolean low_penalty_ra)
{
	coex_dm->cur_low_penalty_ra = low_penalty_ra;

	if (!force_exec) {
		if (coex_dm->pre_low_penalty_ra == coex_dm->cur_low_penalty_ra)
			return;
	}
	halbtc8192e2ant_set_sw_penalty_tx_rate_adaptive(btcoexist,
			coex_dm->cur_low_penalty_ra);

	coex_dm->pre_low_penalty_ra = coex_dm->cur_low_penalty_ra;
}

void halbtc8192e2ant_set_dac_swing_reg(IN struct btc_coexist *btcoexist,
				       IN u32 level)
{
	u8	val = (u8)level;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], Write SwDacSwing = 0x%x\n", level);
	BTC_TRACE(trace_buf);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x883, 0x3e, val);
}

void halbtc8192e2ant_set_sw_full_time_dac_swing(IN struct btc_coexist
		*btcoexist, IN boolean sw_dac_swing_on, IN u32 sw_dac_swing_lvl)
{
	if (sw_dac_swing_on)
		halbtc8192e2ant_set_dac_swing_reg(btcoexist, sw_dac_swing_lvl);
	else
		halbtc8192e2ant_set_dac_swing_reg(btcoexist, 0x18);
}


void halbtc8192e2ant_dac_swing(IN struct btc_coexist *btcoexist,
	IN boolean force_exec, IN boolean dac_swing_on, IN u32 dac_swing_lvl)
{
	coex_dm->cur_dac_swing_on = dac_swing_on;
	coex_dm->cur_dac_swing_lvl = dac_swing_lvl;

	if (!force_exec) {
		if ((coex_dm->pre_dac_swing_on == coex_dm->cur_dac_swing_on) &&
		    (coex_dm->pre_dac_swing_lvl ==
		     coex_dm->cur_dac_swing_lvl))
			return;
	}
	delay_ms(30);
	halbtc8192e2ant_set_sw_full_time_dac_swing(btcoexist, dac_swing_on,
			dac_swing_lvl);

	coex_dm->pre_dac_swing_on = coex_dm->cur_dac_swing_on;
	coex_dm->pre_dac_swing_lvl = coex_dm->cur_dac_swing_lvl;
}

void halbtc8192e2ant_set_adc_back_off(IN struct btc_coexist *btcoexist,
				      IN boolean adc_back_off)
{
	if (adc_back_off) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BB BackOff Level On!\n");
		BTC_TRACE(trace_buf);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xc05, 0x30, 0x3);
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BB BackOff Level Off!\n");
		BTC_TRACE(trace_buf);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xc05, 0x30, 0x1);
	}
}

void halbtc8192e2ant_adc_back_off(IN struct btc_coexist *btcoexist,
			  IN boolean force_exec, IN boolean adc_back_off)
{
	coex_dm->cur_adc_back_off = adc_back_off;

	if (!force_exec) {
		if (coex_dm->pre_adc_back_off == coex_dm->cur_adc_back_off)
			return;
	}
	halbtc8192e2ant_set_adc_back_off(btcoexist, coex_dm->cur_adc_back_off);

	coex_dm->pre_adc_back_off = coex_dm->cur_adc_back_off;
}

void halbtc8192e2ant_set_agc_table(IN struct btc_coexist *btcoexist,
				   IN boolean agc_table_en)
{
	/* =================BB AGC Gain Table */
	if (agc_table_en) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BB Agc Table On!\n");
		BTC_TRACE(trace_buf);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x0a1A0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x091B0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x081C0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x071D0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x061E0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x051F0001);
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BB Agc Table Off!\n");
		BTC_TRACE(trace_buf);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0xaa1A0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0xa91B0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0xa81C0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0xa71D0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0xa61E0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0xa51F0001);
	}
}

void halbtc8192e2ant_agc_table(IN struct btc_coexist *btcoexist,
			       IN boolean force_exec, IN boolean agc_table_en)
{
	coex_dm->cur_agc_table_en = agc_table_en;

	if (!force_exec) {
		if (coex_dm->pre_agc_table_en == coex_dm->cur_agc_table_en)
			return;
	}
	halbtc8192e2ant_set_agc_table(btcoexist, agc_table_en);

	coex_dm->pre_agc_table_en = coex_dm->cur_agc_table_en;
}

void halbtc8192e2ant_set_coex_table(IN struct btc_coexist *btcoexist,
	    IN u32 val0x6c0, IN u32 val0x6c4, IN u32 val0x6c8, IN u8 val0x6cc)
{
	btcoexist->btc_write_4byte(btcoexist, 0x6c0, val0x6c0);

	btcoexist->btc_write_4byte(btcoexist, 0x6c4, val0x6c4);

	btcoexist->btc_write_4byte(btcoexist, 0x6c8, val0x6c8);

	btcoexist->btc_write_1byte(btcoexist, 0x6cc, val0x6cc);
}

void halbtc8192e2ant_coex_table(IN struct btc_coexist *btcoexist,
			IN boolean force_exec, IN u32 val0x6c0, IN u32 val0x6c4,
				IN u32 val0x6c8, IN u8 val0x6cc)
{
	coex_dm->cur_val0x6c0 = val0x6c0;
	coex_dm->cur_val0x6c4 = val0x6c4;
	coex_dm->cur_val0x6c8 = val0x6c8;
	coex_dm->cur_val0x6cc = val0x6cc;

	if (!force_exec) {
		if ((coex_dm->pre_val0x6c0 == coex_dm->cur_val0x6c0) &&
		    (coex_dm->pre_val0x6c4 == coex_dm->cur_val0x6c4) &&
		    (coex_dm->pre_val0x6c8 == coex_dm->cur_val0x6c8) &&
		    (coex_dm->pre_val0x6cc == coex_dm->cur_val0x6cc))
			return;
	}
	halbtc8192e2ant_set_coex_table(btcoexist, val0x6c0, val0x6c4, val0x6c8,
				       val0x6cc);

	coex_dm->pre_val0x6c0 = coex_dm->cur_val0x6c0;
	coex_dm->pre_val0x6c4 = coex_dm->cur_val0x6c4;
	coex_dm->pre_val0x6c8 = coex_dm->cur_val0x6c8;
	coex_dm->pre_val0x6cc = coex_dm->cur_val0x6cc;
}

void halbtc8192e2ant_coex_table_with_type(IN struct btc_coexist *btcoexist,
		IN boolean force_exec, IN u8 type)
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
		halbtc8192e2ant_coex_table(btcoexist, force_exec, 0x55dd55dd,
					   0x5ada5ada, 0xffffff, 0x3);
		break;
	case 3:
		halbtc8192e2ant_coex_table(btcoexist, force_exec, 0x5fdf5fdf,
					   0x5fdb5fdb, 0xffffff, 0x3);
		break;
	case 4:
		halbtc8192e2ant_coex_table(btcoexist, force_exec, 0xdfffdfff,
					   0x5ffb5ffb, 0xffffff, 0x3);
		break;
	case 5:
		halbtc8192e2ant_coex_table(btcoexist, force_exec, 0x5ddd5ddd,
					   0x5fdb5fdb, 0xffffff, 0x3);
		break;
	case 6:
		halbtc8192e2ant_coex_table(btcoexist, force_exec, 0x5fff5fff,
					   0x5a5a5a5a, 0xffffff, 0x3);
		break;
	case 7:
		if (coex_sta->scan_ap_num <= NOISY_AP_NUM_THRESH_8192E)
			halbtc8192e2ant_coex_table(btcoexist, force_exec,  0xffffffff, 0xfafafafa, 0xffffff, 0x3);
		else
			halbtc8192e2ant_coex_table(btcoexist, force_exec, 0xffffffff, 0x5a5a5a5a, 0xffffff, 0x3);
		break;
	case 8:
		halbtc8192e2ant_coex_table(btcoexist, force_exec, 0x5f5f5f5f, 0x5a5a5a5a, 0xffffff, 0x3);
		break;
	case 9:
		halbtc8192e2ant_coex_table(btcoexist, force_exec, 0x55555555, 0xfafafafa, 0xffffff, 0x3);
		break;
	case 10:
		halbtc8192e2ant_coex_table(btcoexist, force_exec, 0x55555555, 0xaaaaaaaa, 0xffffff, 0x3);
		break;
	case 11:
		halbtc8192e2ant_coex_table(btcoexist, force_exec, 0x55555555, 0xfafafafa, 0xffffff, 0x3);
		break;
	default:
		break;
	}
}

void halbtc8192e2ant_set_fw_ignore_wlan_act(IN struct btc_coexist *btcoexist,
		IN boolean enable)
{
	u8			h2c_parameter[1] = {0};

	if (enable)
		h2c_parameter[0] |= BIT(0);		/* function enable */

	btcoexist->btc_fill_h2c(btcoexist, 0x63, 1, h2c_parameter);
}

void halbtc8192e2ant_ignore_wlan_act(IN struct btc_coexist *btcoexist,
				     IN boolean force_exec, IN boolean enable)
{
	coex_dm->cur_ignore_wlan_act = enable;

	if (!force_exec) {
		if (coex_dm->pre_ignore_wlan_act ==
		    coex_dm->cur_ignore_wlan_act)
			return;
	}
	halbtc8192e2ant_set_fw_ignore_wlan_act(btcoexist, enable);

	coex_dm->pre_ignore_wlan_act = coex_dm->cur_ignore_wlan_act;
}

void halbtc8192e2ant_set_lps_rpwm(IN struct btc_coexist *btcoexist,
				  IN u8 lps_val, IN u8 rpwm_val)
{
	u8	lps = lps_val;
	u8	rpwm = rpwm_val;

	btcoexist->btc_set(btcoexist, BTC_SET_U1_LPS_VAL, &lps);
	btcoexist->btc_set(btcoexist, BTC_SET_U1_RPWM_VAL, &rpwm);
}

void halbtc8192e2ant_lps_rpwm(IN struct btc_coexist *btcoexist,
		      IN boolean force_exec, IN u8 lps_val, IN u8 rpwm_val)
{
	coex_dm->cur_lps = lps_val;
	coex_dm->cur_rpwm = rpwm_val;

	if (!force_exec) {
		if ((coex_dm->pre_lps == coex_dm->cur_lps) &&
		    (coex_dm->pre_rpwm == coex_dm->cur_rpwm))
			return;
	}
	halbtc8192e2ant_set_lps_rpwm(btcoexist, lps_val, rpwm_val);

	coex_dm->pre_lps = coex_dm->cur_lps;
	coex_dm->pre_rpwm = coex_dm->cur_rpwm;
}

void halbtc8192e2ant_ps_tdma_check_for_power_save_state(
	IN struct btc_coexist *btcoexist, IN boolean new_ps_state)
{
	u8	lps_mode = 0x0;
	u8	h2c_parameter[5] = {0x8, 0, 0, 0, 0};

	btcoexist->btc_get(btcoexist, BTC_GET_U1_LPS_MODE, &lps_mode);

	if (lps_mode) {	/* already under LPS state */
		if (new_ps_state) {
			/* keep state under LPS, do nothing. */
		} else {
			/* will leave LPS state, turn off psTdma first */
			/*halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);*/
			btcoexist->btc_fill_h2c(btcoexist, 0x60, 5, h2c_parameter);

		}
	} else {					/* NO PS state */
		if (new_ps_state) {
			/* will enter LPS state, turn off psTdma first */
			/*halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);*/
			btcoexist->btc_fill_h2c(btcoexist, 0x60, 5, h2c_parameter);
		} else {
			/* keep state under NO PS state, do nothing. */
		}
	}
}


void halbtc8192e2ant_power_save_state(IN struct btc_coexist *btcoexist,
		      IN u8 ps_type,  IN u8 lps_val,  IN u8 rpwm_val)
{
	boolean		low_pwr_disable = false;
	boolean		bApEnable = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE, &bApEnable);
	if (bApEnable) {
		ps_type = BTC_PS_WIFI_NATIVE;
		lps_val = 0x0;
		rpwm_val = 0x0;
	}
	/*for 8192e, NO 32k*/
	low_pwr_disable = true;
	switch (ps_type) {
	case BTC_PS_WIFI_NATIVE:
		coex_sta->force_lps_on = false;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER, &low_pwr_disable);
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_NORMAL_LPS, NULL);
		break;
	case BTC_PS_LPS_ON:
		coex_sta->force_lps_on = true;
		halbtc8192e2ant_ps_tdma_check_for_power_save_state(btcoexist, true);
		halbtc8192e2ant_lps_rpwm(btcoexist, NORMAL_EXEC, lps_val, rpwm_val);
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER, &low_pwr_disable);
		/* power save must executed before psTdma. */
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_ENTER_LPS, NULL);
		break;
	case BTC_PS_LPS_OFF:
		coex_sta->force_lps_on = false;
		halbtc8192e2ant_ps_tdma_check_for_power_save_state(btcoexist, false);
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
		break;
	default:
		break;
	}
}

void halbtc8192e2ant_set_fw_pstdma(IN struct btc_coexist *btcoexist,
	   IN u8 byte1, IN u8 byte2, IN u8 byte3, IN u8 byte4, IN u8 byte5)
{
	u8			h2c_parameter[5] = {0};
	u8			real_byte1 = byte1, real_byte5 = byte5;
	boolean		ap_enable = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE,
			   &ap_enable);

	if ((ap_enable) && (byte1 & BIT(4) && !(byte1 & BIT(5)))) {
		real_byte1 &= ~BIT(4);
		real_byte1 |= BIT(5);

		real_byte5 |= BIT(5);
		real_byte5 &= ~BIT(6);
	} else if ((byte1 & BIT(4)) && (!(byte1 & BIT(5))))
		halbtc8192e2ant_power_save_state(btcoexist, BTC_PS_LPS_ON, 0x50, 0x4);
	else
		halbtc8192e2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);


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

	btcoexist->btc_fill_h2c(btcoexist, 0x60, 5, h2c_parameter);
}

void halbtc8192e2ant_sw_mechanism1(IN struct btc_coexist *btcoexist,
			   IN boolean shrink_rx_lpf, IN boolean low_penalty_ra,
			   IN boolean limited_dig, IN boolean bt_lna_constrain)
{
	/*
	u32	wifi_bw;

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if(BTC_WIFI_BW_HT40 != wifi_bw)
	{
		if (shrink_rx_lpf)
			shrink_rx_lpf = false;
	}
	*/

	halbtc8192e2ant_rf_shrink(btcoexist, NORMAL_EXEC, shrink_rx_lpf);
	/* halbtc8192e2ant_low_penalty_ra(btcoexist, NORMAL_EXEC, low_penalty_ra); */
}

void halbtc8192e2ant_sw_mechanism2(IN struct btc_coexist *btcoexist,
			   IN boolean agc_table_shift, IN boolean adc_back_off,
			   IN boolean sw_dac_swing, IN u32 dac_swing_lvl)
{
	halbtc8192e2ant_agc_table(btcoexist, NORMAL_EXEC, agc_table_shift);
	/* halbtc8192e2ant_adc_back_off(btcoexist, NORMAL_EXEC, adc_back_off); */
	halbtc8192e2ant_dac_swing(btcoexist, NORMAL_EXEC, sw_dac_swing,
				  dac_swing_lvl);
}

void halbtc8192e2ant_set_ant_path(IN struct btc_coexist *btcoexist,
	  IN u8 ant_pos_type, IN boolean init_hwcfg, IN boolean wifi_off)
{
	u32			u32tmp = 0;

	if (init_hwcfg) {
		btcoexist->btc_write_1byte(btcoexist, 0x944, 0x24);
		btcoexist->btc_write_4byte(btcoexist, 0x930, 0x700700);
		if (btcoexist->chip_interface == BTC_INTF_USB)
			btcoexist->btc_write_4byte(btcoexist, 0x64, 0x30430004);
		else
			btcoexist->btc_write_4byte(btcoexist, 0x64, 0x30030004);

		/* 0x4c[27][24]='00', Set Antenna to BB */
		u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x4c);
		u32tmp &= ~BIT(24);
		u32tmp &= ~BIT(27);
		btcoexist->btc_write_4byte(btcoexist, 0x4c, u32tmp);
	} else if (wifi_off) {
		if (btcoexist->chip_interface == BTC_INTF_USB)
			btcoexist->btc_write_4byte(btcoexist, 0x64, 0x30430004);
		else
			btcoexist->btc_write_4byte(btcoexist, 0x64, 0x30030004);

		/* 0x4c[27][24]='11', Set Antenna to BT, 0x64[8:7]=0, 0x64[2]=1 */
		u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x4c);
		u32tmp |= BIT(24);
		u32tmp |= BIT(27);
		btcoexist->btc_write_4byte(btcoexist, 0x4c, u32tmp);
	}

	/* ext switch setting */
	switch (ant_pos_type) {
	case BTC_ANT_PATH_WIFI:
		btcoexist->btc_write_1byte(btcoexist, 0x92c, 0x4);
		break;
	case BTC_ANT_PATH_BT:
		btcoexist->btc_write_1byte(btcoexist, 0x92c, 0x20);
		break;
	default:
	case BTC_ANT_PATH_PTA:
		btcoexist->btc_write_1byte(btcoexist, 0x92c, 0x4);
		break;
	}
}

void halbtc8192e2ant_set_switch_ss_type(IN struct btc_coexist *btcoexist,
					IN u8 ss_type)
{
	u8	mimo_ps = BTC_MIMO_PS_DYNAMIC;
	u32	dis_ra_mask = 0x0;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], REAL set SS Type = %d\n", ss_type);
	BTC_TRACE(trace_buf);

	dis_ra_mask = halbtc8192e2ant_decide_ra_mask(btcoexist, ss_type,
			coex_dm->cur_ra_mask_type);
	halbtc8192e2ant_update_ra_mask(btcoexist, FORCE_EXEC, dis_ra_mask);

	if (ss_type == 1) {
		/*halbtc8192e2ant_ps_tdma(btcoexist, FORCE_EXEC, false, 1);*/
		/* switch ofdm path */
		btcoexist->btc_write_1byte(btcoexist, 0xc04, 0x11);
		btcoexist->btc_write_1byte(btcoexist, 0xd04, 0x1);
		btcoexist->btc_write_4byte(btcoexist, 0x90c, 0x81111111);
		/* switch cck patch */
		/* btcoexist->btc_write_1byte_bitmask(btcoexist, 0xe77, 0x4, 0x1); */
		/* btcoexist->btc_write_1byte(btcoexist, 0xa07, 0x81); */
		mimo_ps = BTC_MIMO_PS_STATIC;
	} else if (ss_type == 2) {
		/*halbtc8192e2ant_ps_tdma(btcoexist, FORCE_EXEC, false, 0);*/
		btcoexist->btc_write_1byte(btcoexist, 0xc04, 0x33);
		btcoexist->btc_write_1byte(btcoexist, 0xd04, 0x3);
		btcoexist->btc_write_4byte(btcoexist, 0x90c, 0x81121313);
		/* remove, if 0xe77[2]=0x0 then CCK will fail, advised by Jenyu */
		/* btcoexist->btc_write_1byte_bitmask(btcoexist, 0xe77, 0x4, 0x0); */
		/* btcoexist->btc_write_1byte(btcoexist, 0xa07, 0x41); */
		mimo_ps = BTC_MIMO_PS_DYNAMIC;
	}

	btcoexist->btc_set(btcoexist, BTC_SET_ACT_SEND_MIMO_PS,
			   &mimo_ps);	/* set rx 1ss or 2ss */
}

void halbtc8192e2ant_switch_ss_type(IN struct btc_coexist *btcoexist,
				    IN boolean force_exec, IN u8 new_ss_type)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], %s Switch SS Type = %d\n",
		    (force_exec ? "force to" : ""), new_ss_type);
	BTC_TRACE(trace_buf);
	coex_dm->cur_ss_type = new_ss_type;

	if (!force_exec) {
		if (coex_dm->pre_ss_type == coex_dm->cur_ss_type)
			return;
	}
	halbtc8192e2ant_set_switch_ss_type(btcoexist, coex_dm->cur_ss_type);

	coex_dm->pre_ss_type = coex_dm->cur_ss_type;
}

void halbtc8192e2ant_ps_tdma(IN struct btc_coexist *btcoexist,
		     IN boolean force_exec, IN boolean turn_on, IN u8 type)
{
	s8		wifi_duration_adjust = 0x0;

	coex_dm->cur_ps_tdma_on = turn_on;
	coex_dm->cur_ps_tdma = type;

	if (!force_exec) {
		if ((coex_dm->pre_ps_tdma_on == coex_dm->cur_ps_tdma_on) &&
		    (coex_dm->pre_ps_tdma == coex_dm->cur_ps_tdma))
			return;
	}

	if (coex_sta->scan_ap_num  >= 40)
		wifi_duration_adjust = -15;
	else if (coex_sta->scan_ap_num  >= 20)
		wifi_duration_adjust = -10;


	if (turn_on) {
		switch (type) {
		case 1:
		default:	/*d1,wb*/
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x3c, 0x03, 0x11, 0x10);
			break;
		case 2:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x32, 0x03, 0x11, 0x10);
			break;
		case 3:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x28, 0x03, 0x11, 0x10);
			break;
		case 4:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x1e, 0x03, 0x11, 0x10);
			break;
		case 5:		/*d1,pb,TXpause*/
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x63, 0x3c, 0x03, 0x90, 0x10);
			break;
		case 6:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x63, 0x32, 0x03, 0x90, 0x10);
			break;
		case 7:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x63, 0x28, 0x03, 0x90, 0x10);
			break;
		case 8:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x63, 0x1e, 0x03, 0x90, 0x10);
			break;
		case 9:		/*d1,bb*/
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x3c, 0x03, 0x31, 0x10);
			break;
		case 10:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x32, 0x03, 0x31, 0x10);
			break;
		case 11:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x28, 0x03, 0x31, 0x10);
			break;
		case 12:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x1e, 0x03, 0x31, 0x10);
			break;
		case 13:		/*d1,bb,TXpause*/
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x3c, 0x03, 0x30, 0x10);
			break;
		case 14:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x32, 0x03, 0x30, 0x10);
			break;
		case 15:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x28, 0x03, 0x30, 0x10);
			break;
		case 16:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x1e, 0x03, 0x30, 0x10);
			break;
		case 17:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x61, 0x20, 0x03, 0x10, 0x10);
			break;
		case 18:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x5, 0x5, 0xe1, 0x90);
			break;
		case 19:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x25, 0x25, 0xe1, 0x90);
			break;
		case 20:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x25, 0x25, 0x60, 0x90);
			break;
		case 21:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x61, 0x15, 0x03, 0x11, 0x11);
			break;
		case 22:	/* d1,wb */
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x14, 0x03, 0x11, 0x14);
			break;
		case 23:	/* d1,pb,TXpause */
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x63, 0x14, 0x03, 0x90, 0x14);
			break;
		case 24:	/* d1,bb */
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x14, 0x03, 0x31, 0x14);
			break;
		case 25:	/* d1,bb,TXpause */
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x14, 0x03, 0x30, 0x14);
			break;
		case 26:	/*d1,wp,noTXpause*/
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe1, 0x3c, 0x03, 0x11, 0x10);
			break;
		case 27:	/*11,pp,noTXpause*/
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x61, 0x3c, 0x03, 0x11, 0x11);
			break;
		case 28:	/*11,pp,noTXpause*/
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x61, 0x50, 0x03, 0x11, 0x11);
			break;
		case 29:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x61, 0x15, 0x03, 0x11, 0x11);
			break;
		case 30:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x14, 0x03, 0x10, 0x14);
			break;
		case 31: /*d3,bb,TXpause*/
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x14, 0x03, 0x30, 0x94);
			break;
		case 32:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x61, 0x35, 0x03, 0x11, 0x11);
			break;
		case 71:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0xe3, 0x1a, 0x1a, 0xe1, 0x90);
			break;
		/* following cases is for wifi rssi low // bad antenna isolation, started from 81 */
		case 80:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x51, 0x3c, 0x3, 0x10, 0x50);
			break;
		case 81:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x51, 0x3a + wifi_duration_adjust, 0x3, 0x10, 0x50);
			break;
		case 82:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x51, 0x30 + wifi_duration_adjust, 0x03, 0x10, 0x50);
			break;
		case 83:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x51, 0x21, 0x03, 0x10, 0x50);
			break;
		case 84:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x51, 0x15, 0x3, 0x10, 0x50);
			break;
		case 85:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x51, 0x3a, 0x03, 0x10, 0x50);
			break;
		case 86:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x51, 0x21, 0x03, 0x10, 0x50);
			break;
		case 87:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x51, 0x14, 0x03, 0x10, 0x54);
			break;
		case 88:
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x53, 0x14, 0x03, 0x10, 0x54);
			break;
		}
	} else {
		/* disable PS tdma */
		switch (type) {
		default:
		case 0:  /* ANT2PTA, 0x778=1 */
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x8, 0x0, 0x0, 0x0, 0x0);
			halbtc8192e2ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA, false, false);
			break;
		case 1:  /* ANT2BT, 0x778=3 */
			halbtc8192e2ant_set_fw_pstdma(btcoexist, 0x0, 0x0, 0x0, 0x8, 0x0);
			delay_ms(5);
			halbtc8192e2ant_set_ant_path(btcoexist, BTC_ANT_PATH_BT, false, false);
			break;
		}
	}

	/* update pre state */
	coex_dm->pre_ps_tdma_on = coex_dm->cur_ps_tdma_on;
	coex_dm->pre_ps_tdma = coex_dm->cur_ps_tdma;
}


void halbtc8192e2ant_monitor_bt_enable_disable(IN struct btc_coexist *btcoexist)
{
	static u32	bt_disable_cnt = 0;
	boolean			bt_active = true, bt_disabled = false;

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
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is enabled !!\n");
		BTC_TRACE(trace_buf);
	} else {
		bt_disable_cnt++;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], bt all counters=0, %d times!!\n",
			    bt_disable_cnt);
		BTC_TRACE(trace_buf);
		if (bt_disable_cnt >= 2) {
			bt_disabled = true;
			btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_DISABLE,
					   &bt_disabled);
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], BT is disabled !!\n");
			BTC_TRACE(trace_buf);
		}
	}
	if (coex_sta->bt_disabled != bt_disabled) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is from %s to %s!!\n",
			    (coex_sta->bt_disabled ? "disabled" : "enabled"),
			    (bt_disabled ? "disabled" : "enabled"));
		BTC_TRACE(trace_buf);
		coex_sta->bt_disabled = bt_disabled;
		if (bt_disabled) {
			halbtc8192e2ant_ps_tdma(btcoexist, FORCE_EXEC, false, 0);
			halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
		}
	}
}


void halbtc8192e2ant_coex_all_off(IN struct btc_coexist *btcoexist)
{
	/* fw all off */
	halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
	halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
	halbtc8192e2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	/* sw all off */
	halbtc8192e2ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);

	/* hw all off */
	halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
}

void halbtc8192e2ant_init_coex_dm(IN struct btc_coexist *btcoexist)
{
	coex_sta->cnt_setup_link = 0;

	/* force to reset coex mechanism */
	halbtc8192e2ant_ps_tdma(btcoexist, FORCE_EXEC, false, 0);
	halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, FORCE_EXEC, 6);
	halbtc8192e2ant_dec_bt_pwr(btcoexist, FORCE_EXEC, 0);

	halbtc8192e2ant_coex_table_with_type(btcoexist, FORCE_EXEC, 0);
	halbtc8192e2ant_switch_ss_type(btcoexist, FORCE_EXEC, 2);

	halbtc8192e2ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
}

void halbtc8192e2ant_action_bt_inquiry(IN struct btc_coexist *btcoexist)
{
	struct	btc_bt_link_info	*bt_link_info = &btcoexist->bt_link_info;

	if (coex_sta->wifi_is_high_pri_task && (!bt_link_info->a2dp_exist)) {
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 11);
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 32);
	} else {
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 29);
	}

	halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
	halbtc8192e2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	halbtc8192e2ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
}

boolean halbtc8192e2ant_is_common_action(IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info	*bt_link_info = &btcoexist->bt_link_info;
	boolean			common = false, wifi_connected = false, wifi_busy = false;
	boolean			bt_hs_on = false, low_pwr_disable = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	if (bt_link_info->sco_exist || bt_link_info->hid_exist)
		halbtc8192e2ant_limited_tx(btcoexist, NORMAL_EXEC, 1, 0, 0, 0);
	else
		halbtc8192e2ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);

	if (!wifi_connected) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Wifi non-connected idle!!\n");
		BTC_TRACE(trace_buf);

		if ((coex_dm->bt_status == BT_8192E_2ANT_BT_STATUS_NON_CONNECTED_IDLE) ||
		    (coex_dm->bt_status == BT_8192E_2ANT_BT_STATUS_CONNECTED_IDLE)) {
			halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);
		} else {
			halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
		}
		halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8192e2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

		halbtc8192e2ant_sw_mechanism1(btcoexist, false, false, false, false);
		halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
		common = true;
	} else {
		if (coex_dm->bt_status == BT_8192E_2ANT_BT_STATUS_NON_CONNECTED_IDLE) {
			if (wifi_busy && (coex_sta->high_priority_rx > 10))  {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Wifi busy + BT non connected-idle!!\n");
				BTC_TRACE(trace_buf);

				halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 9);
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 26);
				halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
				halbtc8192e2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);
				halbtc8192e2ant_sw_mechanism1(btcoexist, false, false, false, false);
				halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
				common = true;
			} else {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Wifi connected-idle + BT non connected-idle!!\n");
				BTC_TRACE(trace_buf);
				halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);
				halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
				halbtc8192e2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);
				halbtc8192e2ant_sw_mechanism1(btcoexist, false, false, false, false);
				halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
				common = true;
			}
		} else if (coex_dm->bt_status == BT_8192E_2ANT_BT_STATUS_CONNECTED_IDLE) {

			if (bt_hs_on)
				return false;

			if (wifi_busy) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Wifi busy + BT connected-idle!!\n");
				BTC_TRACE(trace_buf);
				halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 10);
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 27);
				halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
				halbtc8192e2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);
				halbtc8192e2ant_sw_mechanism1(btcoexist, false, false, false, false);
				halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
				common = true;
			} else {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Wifi connected-idle + BT connected-idle!!\n");
				BTC_TRACE(trace_buf);
				halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);
				halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
				halbtc8192e2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);
				halbtc8192e2ant_sw_mechanism1(btcoexist, true, false, false, false);
				halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
				common = true;
			}
		} else {

			if (wifi_busy) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], Wifi Connected-Busy + BT Busy!!\n");
				BTC_TRACE(trace_buf);
				common = false;
			} else {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], Wifi Connected-Idle + BT Busy!!\n");
				BTC_TRACE(trace_buf);
				halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 21);
				halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
				halbtc8192e2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);
				halbtc8192e2ant_sw_mechanism1(btcoexist, false, false, false, false);
				halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
				common = true;
			}
		}
	}
	return common;
}


void halbtc8192e2ant_tdma_duration_adjust(IN struct btc_coexist *btcoexist,
		IN boolean sco_hid, IN boolean tx_pause, IN u8 max_interval)
{
	static s32		up, dn, m, n, wait_count;
	s32			result;   /* 0: no change, +1: increase WiFi duration, -1: decrease WiFi duration */
	u8			retry_count = 0;


	if (!coex_dm->auto_tdma_adjust) {
		coex_dm->auto_tdma_adjust = true;
		{
			if (sco_hid) {
				if (tx_pause) {
					if (max_interval == 1) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 13);
						coex_dm->ps_tdma_du_adj_type =
							13;
					} else if (max_interval == 2) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					} else if (max_interval == 3) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					}
				} else {
					if (max_interval == 1) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 9);
						coex_dm->ps_tdma_du_adj_type =
							9;
					} else if (max_interval == 2) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 10);
						coex_dm->ps_tdma_du_adj_type =
							10;
					} else if (max_interval == 3) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					}
				}
			} else {
				if (tx_pause) {
					if (max_interval == 1) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 5);
						coex_dm->ps_tdma_du_adj_type =
							5;
					} else if (max_interval == 2) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (max_interval == 3) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					}
				} else {
					if (max_interval == 1) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 1);
						coex_dm->ps_tdma_du_adj_type =
							1;
					} else if (max_interval == 2) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (max_interval == 3) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					}
				}
			}
		}
		/* ============ */
		up = 0;
		dn = 0;
		m = 1;
		n = 3;
		result = 0;
		wait_count = 0;
	} else {
		/* acquire the BT TRx retry count from BT_Info byte2 */
		retry_count = coex_sta->bt_retry_cnt;
		result = 0;
		wait_count++;

		if (retry_count ==
		    0) { /* no retry in the last 2-second duration */
			up++;
			dn--;

			if (dn <= 0)
				dn = 0;

			if (up >= n) {	/* if retry count during continuous n*2 seconds is 0, enlarge WiFi duration */
				wait_count = 0;
				n = 3;
				up = 0;
				dn = 0;
				result = 1;
			}
		} else if (retry_count <=
			   3) {	/* <=3 retry in the last 2-second duration */
			up--;
			dn++;

			if (up <= 0)
				up = 0;

			if (dn == 2) {	/* if continuous 2 retry count(every 2 seconds) >0 and < 3, reduce WiFi duration */
				if (wait_count <= 2)
					m++; /* to avoid loop between the two levels */
				else
					m = 1;

				if (m >= 20)  /* maximum of m = 20 ' will recheck if need to adjust wifi duration in maximum time interval 120 seconds */
					m = 20;

				n = 3 * m;
				up = 0;
				dn = 0;
				wait_count = 0;
				result = -1;
			}
		} else { /* retry count > 3, once retry count > 3, to reduce WiFi duration */
			if (wait_count == 1)
				m++; /* to avoid loop between the two levels */
			else
				m = 1;

			if (m >= 20)  /* maximum of m = 20 ' will recheck if need to adjust wifi duration in maximum time interval 120 seconds */
				m = 20;

			n = 3 * m;
			up = 0;
			dn = 0;
			wait_count = 0;
			result = -1;
		}

		if (max_interval == 1) {
			if (tx_pause) {
				if (coex_dm->cur_ps_tdma == 1) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 5);
					coex_dm->ps_tdma_du_adj_type = 5;
				} else if (coex_dm->cur_ps_tdma == 2) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 6);
					coex_dm->ps_tdma_du_adj_type = 6;
				} else if (coex_dm->cur_ps_tdma == 3) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 4) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 8);
					coex_dm->ps_tdma_du_adj_type = 8;
				}
				if (coex_dm->cur_ps_tdma == 9) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 13);
					coex_dm->ps_tdma_du_adj_type = 13;
				} else if (coex_dm->cur_ps_tdma == 10) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 14);
					coex_dm->ps_tdma_du_adj_type = 14;
				} else if (coex_dm->cur_ps_tdma == 11) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 12) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 16);
					coex_dm->ps_tdma_du_adj_type = 16;
				}

				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 5) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (coex_dm->cur_ps_tdma == 6) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 8);
						coex_dm->ps_tdma_du_adj_type =
							8;
					} else if (coex_dm->cur_ps_tdma == 13) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					} else if (coex_dm->cur_ps_tdma == 14) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 16);
						coex_dm->ps_tdma_du_adj_type =
							16;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 8) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (coex_dm->cur_ps_tdma == 6) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 5);
						coex_dm->ps_tdma_du_adj_type =
							5;
					} else if (coex_dm->cur_ps_tdma == 16) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					} else if (coex_dm->cur_ps_tdma == 14) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 13);
						coex_dm->ps_tdma_du_adj_type =
							13;
					}
				}
			} else {
				if (coex_dm->cur_ps_tdma == 5) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 1);
					coex_dm->ps_tdma_du_adj_type = 1;
				} else if (coex_dm->cur_ps_tdma == 6) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 2);
					coex_dm->ps_tdma_du_adj_type = 2;
				} else if (coex_dm->cur_ps_tdma == 7) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 8) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 4);
					coex_dm->ps_tdma_du_adj_type = 4;
				}
				if (coex_dm->cur_ps_tdma == 13) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 9);
					coex_dm->ps_tdma_du_adj_type = 9;
				} else if (coex_dm->cur_ps_tdma == 14) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 10);
					coex_dm->ps_tdma_du_adj_type = 10;
				} else if (coex_dm->cur_ps_tdma == 15) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 16) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 12);
					coex_dm->ps_tdma_du_adj_type = 12;
				}

				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 1) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (coex_dm->cur_ps_tdma == 2) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 4);
						coex_dm->ps_tdma_du_adj_type =
							4;
					} else if (coex_dm->cur_ps_tdma == 9) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 10);
						coex_dm->ps_tdma_du_adj_type =
							10;
					} else if (coex_dm->cur_ps_tdma == 10) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 12);
						coex_dm->ps_tdma_du_adj_type =
							12;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 4) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (coex_dm->cur_ps_tdma == 2) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 1);
						coex_dm->ps_tdma_du_adj_type =
							1;
					} else if (coex_dm->cur_ps_tdma == 1) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 71);
						coex_dm->ps_tdma_du_adj_type =
							71;
					} else if (coex_dm->cur_ps_tdma == 12) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 10);
						coex_dm->ps_tdma_du_adj_type =
							10;
					} else if (coex_dm->cur_ps_tdma == 10) {
						halbtc8192e2ant_ps_tdma(
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
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 6);
					coex_dm->ps_tdma_du_adj_type = 6;
				} else if (coex_dm->cur_ps_tdma == 2) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 6);
					coex_dm->ps_tdma_du_adj_type = 6;
				} else if (coex_dm->cur_ps_tdma == 3) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 4) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 8);
					coex_dm->ps_tdma_du_adj_type = 8;
				}
				if (coex_dm->cur_ps_tdma == 9) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 14);
					coex_dm->ps_tdma_du_adj_type = 14;
				} else if (coex_dm->cur_ps_tdma == 10) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 14);
					coex_dm->ps_tdma_du_adj_type = 14;
				} else if (coex_dm->cur_ps_tdma == 11) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 12) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 16);
					coex_dm->ps_tdma_du_adj_type = 16;
				}
				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 5) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (coex_dm->cur_ps_tdma == 6) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 8);
						coex_dm->ps_tdma_du_adj_type =
							8;
					} else if (coex_dm->cur_ps_tdma == 13) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					} else if (coex_dm->cur_ps_tdma == 14) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 16);
						coex_dm->ps_tdma_du_adj_type =
							16;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 8) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (coex_dm->cur_ps_tdma == 6) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (coex_dm->cur_ps_tdma == 16) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					} else if (coex_dm->cur_ps_tdma == 14) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					}
				}
			} else {
				if (coex_dm->cur_ps_tdma == 5) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 2);
					coex_dm->ps_tdma_du_adj_type = 2;
				} else if (coex_dm->cur_ps_tdma == 6) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 2);
					coex_dm->ps_tdma_du_adj_type = 2;
				} else if (coex_dm->cur_ps_tdma == 7) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 8) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 4);
					coex_dm->ps_tdma_du_adj_type = 4;
				}
				if (coex_dm->cur_ps_tdma == 13) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 10);
					coex_dm->ps_tdma_du_adj_type = 10;
				} else if (coex_dm->cur_ps_tdma == 14) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 10);
					coex_dm->ps_tdma_du_adj_type = 10;
				} else if (coex_dm->cur_ps_tdma == 15) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 16) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 12);
					coex_dm->ps_tdma_du_adj_type = 12;
				}
				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 1) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (coex_dm->cur_ps_tdma == 2) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 4);
						coex_dm->ps_tdma_du_adj_type =
							4;
					} else if (coex_dm->cur_ps_tdma == 9) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 10);
						coex_dm->ps_tdma_du_adj_type =
							10;
					} else if (coex_dm->cur_ps_tdma == 10) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 12);
						coex_dm->ps_tdma_du_adj_type =
							12;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 4) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (coex_dm->cur_ps_tdma == 2) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (coex_dm->cur_ps_tdma == 12) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 10);
						coex_dm->ps_tdma_du_adj_type =
							10;
					} else if (coex_dm->cur_ps_tdma == 10) {
						halbtc8192e2ant_ps_tdma(
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
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 2) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 3) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 4) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 8);
					coex_dm->ps_tdma_du_adj_type = 8;
				}
				if (coex_dm->cur_ps_tdma == 9) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 10) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 11) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 12) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 16);
					coex_dm->ps_tdma_du_adj_type = 16;
				}
				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 5) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 6) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 8);
						coex_dm->ps_tdma_du_adj_type =
							8;
					} else if (coex_dm->cur_ps_tdma == 13) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 14) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 16);
						coex_dm->ps_tdma_du_adj_type =
							16;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 8) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 6) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 16) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 14) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					}
				}
			} else {
				if (coex_dm->cur_ps_tdma == 5) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 6) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 7) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 8) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 4);
					coex_dm->ps_tdma_du_adj_type = 4;
				}
				if (coex_dm->cur_ps_tdma == 13) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 14) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 15) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 16) {
					halbtc8192e2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 12);
					coex_dm->ps_tdma_du_adj_type = 12;
				}
				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 1) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 2) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 4);
						coex_dm->ps_tdma_du_adj_type =
							4;
					} else if (coex_dm->cur_ps_tdma == 9) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 10) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 12);
						coex_dm->ps_tdma_du_adj_type =
							12;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 4) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 2) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 12) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 10) {
						halbtc8192e2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					}
				}
			}
		}
	}

	/* if current PsTdma not match with the recorded one (when scan, dhcp...), */
	/* then we have to adjust it back to the previous record one. */
	if (coex_dm->cur_ps_tdma != coex_dm->ps_tdma_du_adj_type) {
		boolean	scan = false, link = false, roam = false;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], PsTdma type dismatch!!!, cur_ps_tdma=%d, recordPsTdma=%d\n",
			    coex_dm->cur_ps_tdma, coex_dm->ps_tdma_du_adj_type);
		BTC_TRACE(trace_buf);

		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);

		if (!scan && !link && !roam)
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						coex_dm->ps_tdma_du_adj_type);
		else {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], roaming/link/scan is under progress, will adjust next time!!!\n");
			BTC_TRACE(trace_buf);
		}
	}
}

/* ******************
 * pstdma for wifi rssi low
 * ****************** */
void halbtc8192e2ant_tdma_duration_adjust_for_wifi_rssi_low(
	IN struct btc_coexist *btcoexist/* , */ /* IN u8 wifi_status */)
{
	static s32		up, dn, m, n, wait_count;
	s32			result;   /* 0: no change, +1: increase WiFi duration, -1: decrease WiFi duration */
	u8			retry_count = 0, bt_info_ext;

	coex_dm->auto_tdma_adjust = false;

	retry_count = coex_sta->bt_retry_cnt;
	bt_info_ext = coex_sta->bt_info_ext;

	if (!coex_dm->auto_tdma_adjust_low_rssi) {
		coex_dm->auto_tdma_adjust_low_rssi = true;
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 81);
		coex_dm->ps_tdma_du_adj_type = 81;
		/* ============ */
		up = 0;
		dn = 0;
		m = 1;
		n = 3;
		result = 0;
		wait_count = 0;
	} else {
		/* acquire the BT TRx retry count from BT_Info byte2
		*		retry_count = coex_sta->bt_retry_cnt;
		*		bt_info_ext = coex_sta->bt_info_ext; */
		result = 0;
		wait_count++;

		if ((coex_sta->low_priority_tx) > 1050 ||
		    (coex_sta->low_priority_rx) > 1250)
			retry_count++;

		if (retry_count ==
		    0) { /* no retry in the last 2-second duration */
			up++;
			dn--;

			if (dn <= 0)
				dn = 0;

			if (up >= n) {	/* if retry count during continuous n*2 seconds is 0, enlarge WiFi duration */
				wait_count = 0;
				n = 3;
				up = 0;
				dn = 0;
				result = 1;
			}
		} else if (retry_count <=
			   3) {	/* <=3 retry in the last 2-second duration */
			up--;
			dn++;

			if (up <= 0)
				up = 0;

			if (dn == 2) {	/* if continuous 2 retry count(every 2 seconds) >0 and < 3, reduce WiFi duration */
				if (wait_count <= 2)
					m++; /* to avoid loop between the two levels */
				else
					m = 1;

				if (m >= 20)  /* maximum of m = 20 ' will recheck if need to adjust wifi duration in maximum time interval 120 seconds */
					m = 20;

				n = 3 * m;
				up = 0;
				dn = 0;
				wait_count = 0;
				result = -1;
			}
		} else { /* retry count > 3, once retry count > 3, to reduce WiFi duration */
			if (wait_count == 1)
				m++; /* to avoid loop between the two levels */
			else
				m = 1;

			if (m >= 20)  /* maximum of m = 20 ' will recheck if need to adjust wifi duration in maximum time interval 120 seconds */
				m = 20;

			n = 3 * m;
			up = 0;
			dn = 0;
			wait_count = 0;
			result = -1;
		}

		if (result == -1) {
			if (coex_dm->cur_ps_tdma == 80) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 81);
				coex_dm->ps_tdma_du_adj_type = 81;
			} else if (coex_dm->cur_ps_tdma == 81) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 82);
				coex_dm->ps_tdma_du_adj_type = 82;
			} else if (coex_dm->cur_ps_tdma == 82) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 83);
				coex_dm->ps_tdma_du_adj_type = 83;
			} else if (coex_dm->cur_ps_tdma == 83) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 84);
				coex_dm->ps_tdma_du_adj_type = 84;
			}
		} else if (result == 1) {
			if (coex_dm->cur_ps_tdma == 84) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 83);
				coex_dm->ps_tdma_du_adj_type = 83;
			} else if (coex_dm->cur_ps_tdma == 83) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 82);
				coex_dm->ps_tdma_du_adj_type = 82;
			} else if (coex_dm->cur_ps_tdma == 82) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 81);
				coex_dm->ps_tdma_du_adj_type = 81;
			} else if ((coex_dm->cur_ps_tdma == 81) &&
				   (coex_sta->scan_ap_num <= 5)) {
				halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 81);
				coex_dm->ps_tdma_du_adj_type = 81;
			}
		}

		if (coex_dm->cur_ps_tdma != 80 &&
		    coex_dm->cur_ps_tdma != 81 &&
		    coex_dm->cur_ps_tdma != 82 &&
		    coex_dm->cur_ps_tdma != 83 &&
		    coex_dm->cur_ps_tdma != 84) {
			/* recover to previous adjust type */
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						coex_dm->ps_tdma_du_adj_type);
		}
	}
}


void halbtc8192e2ant_get_bt_rssi_threshold(IN struct btc_coexist *btcoexist,
		IN u8 *pThres0, IN u8 *pThres1)
{
	u8 ant_type;

	struct  btc_board_info  *board_info = &btcoexist->board_info;

	ant_type = board_info->ant_type;

	switch (ant_type) {
	case BTC_ANT_TYPE_0:
		*pThres0 = 100;
		*pThres1 = 100;
		break;
	case BTC_ANT_TYPE_1:
		*pThres0 = 34;
		*pThres1 = 42;
		break;
	case BTC_ANT_TYPE_2:
		*pThres0 = 34;
		*pThres1 = 42;
		break;
	case BTC_ANT_TYPE_3:
		*pThres0 = 34;
		*pThres1 = 42;
		break;
	case BTC_ANT_TYPE_4:
		*pThres0 = 34;
		*pThres1 = 42;
		break;
	default:
		break;
	}
}

void halbtc8192e2ant_action_bt_relink(IN struct btc_coexist *btcoexist)
{
	if (coex_sta->wifi_is_high_pri_task) {
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
	} else {
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);
	}
	halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
	halbtc8192e2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);
	halbtc8192e2ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
}


/* SCO only or SCO+PAN(HS) */
void halbtc8192e2ant_action_sco(IN struct btc_coexist *btcoexist)
{
	u8	wifi_rssi_state, bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
	u32	wifi_bw;
	u8	bt_thresh0 = 0, bt_thresh1 = 0;

	halbtc8192e2ant_get_bt_rssi_threshold(btcoexist, &bt_thresh0,
					      &bt_thresh1);
	bt_rssi_state = halbtc8192e2ant_bt_rssi_state(3, bt_thresh0,
			bt_thresh1);

	wifi_rssi_state = halbtc8192e2ant_wifi_rssi_state(btcoexist, 0, 2, 34,
			  0);

	if (BTC_RSSI_HIGH(wifi_rssi_state) && (!BTC_RSSI_LOW(bt_rssi_state)))
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
	else
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);


	if (BTC_RSSI_HIGH(wifi_rssi_state) &&	(!BTC_RSSI_LOW(bt_rssi_state)))
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);
	else
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);


	halbtc8192e2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);
	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);
	halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	halbtc8192e2ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
}

void halbtc8192e2ant_action_sco_pan(IN struct btc_coexist *btcoexist)
{
	u8	wifi_rssi_state, bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
	u32	wifi_bw;
	u8		bt_thresh0 = 0, bt_thresh1 = 0;


	halbtc8192e2ant_get_bt_rssi_threshold(btcoexist, &bt_thresh0,
					      &bt_thresh1);
	bt_rssi_state = halbtc8192e2ant_bt_rssi_state(3, bt_thresh0,
			bt_thresh1);

	wifi_rssi_state = halbtc8192e2ant_wifi_rssi_state(btcoexist, 0, 2, 34,
			  0);

	if (BTC_RSSI_HIGH(wifi_rssi_state) && (!BTC_RSSI_LOW(bt_rssi_state)))
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
	else
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);



	if (BTC_RSSI_HIGH(wifi_rssi_state) && (!BTC_RSSI_LOW(bt_rssi_state)))
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 10);
	else
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 86);

	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);
	halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	halbtc8192e2ant_sw_mechanism1(btcoexist, false, false, false, false);
	if (BTC_RSSI_HIGH(wifi_rssi_state) && (!BTC_RSSI_LOW(bt_rssi_state)) &&
	    (coex_sta->scan_ap_num < NOISY_AP_NUM_THRESH_8192E))
		halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false,
					      0x18);
	else if (BTC_RSSI_HIGH(wifi_rssi_state) &&
		 (!BTC_RSSI_LOW(bt_rssi_state)) &&
		 (coex_sta->scan_ap_num > NOISY_AP_NUM_THRESH_8192E))
		halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false,
					      0x18);
	else
		halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false,
					      0x18);
}


void halbtc8192e2ant_action_hid(IN struct btc_coexist *btcoexist)
{
	u8	wifi_rssi_state, bt_rssi_state = BTC_RSSI_STATE_HIGH;
	u32	wifi_bw;
	u8	anttype = 0;

	btcoexist->btc_get(btcoexist, BTC_GET_U1_ANT_TYPE, &anttype);

	wifi_rssi_state = halbtc8192e2ant_wifi_rssi_state(btcoexist, 0, 2, 34, 0);
	bt_rssi_state = halbtc8192e2ant_bt_rssi_state(3, 34, 42);

	if (anttype == 0) {
		/*ANTTYPE = 0   2ant with SPDT*/
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 28);
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
	} else if (anttype == 1) {
		/*2ant with coupler and bad ant. isolation, 3ant with bad ant. isolation*/
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 28);
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
	} else if (anttype == 2) {
		/*ANTTYPE = 2, 2ant with coupler and normal/good ant. isolation, 3ant with normal ant. isolation*/
		if (BTC_RSSI_HIGH(wifi_rssi_state) && (!BTC_RSSI_LOW(bt_rssi_state))) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 9);
			halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 3);
		} else {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 9);
			halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 3);
		}
	} else if (anttype == 3) {
		/*ANTTYPE = 3,  3ant with good ant. isolation*/
		if (BTC_RSSI_HIGH(wifi_rssi_state) && (!BTC_RSSI_LOW(bt_rssi_state))) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
			halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
		} else {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
			halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
		}
	}

	halbtc8192e2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);
	halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	halbtc8192e2ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
}

/* A2DP only / PAN(EDR) only/ A2DP+PAN(HS) */
void halbtc8192e2ant_action_a2dp(IN struct btc_coexist *btcoexist)
{
	u8		wifi_rssi_state, bt_rssi_state = BTC_RSSI_STATE_HIGH;
	u32		wifi_bw;
	boolean		long_dist = false;
	u8	anttype = 0;

	btcoexist->btc_get(btcoexist, BTC_GET_U1_ANT_TYPE, &anttype);

	wifi_rssi_state = halbtc8192e2ant_wifi_rssi_state(btcoexist, 0, 2, 34, 0);
	bt_rssi_state = halbtc8192e2ant_bt_rssi_state(3, 34, 42);

	if (anttype == 0) {
		/*ANTTYPE = 0   2ant with SPDT*/
		if ((coex_sta->scan_ap_num > 40) && (BTC_RSSI_HIGH(wifi_rssi_state) && (!BTC_RSSI_LOW(bt_rssi_state)))) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 30);
			halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
		} else {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 88);
			halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
		}
	} else if (anttype == 1) {
		/*2ant with coupler and bad ant. isolation, 3ant with bad ant. isolation*/
		if (BTC_RSSI_HIGH(wifi_rssi_state) && (!BTC_RSSI_LOW(bt_rssi_state))) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 25);
			halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
		} else {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 88);
			halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
		}
	} else if (anttype == 2) {
		/*ANTTYPE = 2, 2ant with coupler and normal/good ant. isolation, 3ant with normal ant. isolation*/
		if (BTC_RSSI_HIGH(wifi_rssi_state) && (!BTC_RSSI_LOW(bt_rssi_state))) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 22);
			halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 5);
		} else {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 88);
			halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
		}
	} else if (anttype == 3) {
		/*ANTTYPE = 3,  3ant with good ant. isolation*/
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
	}
	halbtc8192e2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);
	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);
	halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
	halbtc8192e2ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
}

void halbtc8192e2ant_action_a2dp_pan_hs(IN struct btc_coexist *btcoexist)
{
	u8		wifi_rssi_state, bt_rssi_state = BTC_RSSI_STATE_HIGH;
	u32		wifi_bw;

	wifi_rssi_state = halbtc8192e2ant_wifi_rssi_state(btcoexist, 0, 2, 15,
			  0);
	bt_rssi_state = halbtc8192e2ant_bt_rssi_state(3, 34, 42);

	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
	halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);

	if ((bt_rssi_state == BTC_RSSI_STATE_LOW) ||
	    (bt_rssi_state == BTC_RSSI_STATE_STAY_LOW)) {
		halbtc8192e2ant_tdma_duration_adjust(btcoexist, false, true, 2);
		halbtc8192e2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);
	} else if ((bt_rssi_state == BTC_RSSI_STATE_MEDIUM) ||
		   (bt_rssi_state == BTC_RSSI_STATE_STAY_MEDIUM)) {
		halbtc8192e2ant_tdma_duration_adjust(btcoexist, false, false,
						     2);
		halbtc8192e2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
	} else if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		   (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
		halbtc8192e2ant_tdma_duration_adjust(btcoexist, false, false,
						     2);
		halbtc8192e2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 4);
	}

	/* sw mechanism */
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8192e2ant_sw_mechanism1(btcoexist, true, false,
						      false, false);
			halbtc8192e2ant_sw_mechanism2(btcoexist, true, false,
						      true, 0x6);
		} else {
			halbtc8192e2ant_sw_mechanism1(btcoexist, true, false,
						      false, false);
			halbtc8192e2ant_sw_mechanism2(btcoexist, false, false,
						      true, 0x6);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8192e2ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8192e2ant_sw_mechanism2(btcoexist, true, false,
						      true, 0x6);
		} else {
			halbtc8192e2ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8192e2ant_sw_mechanism2(btcoexist, false, false,
						      true, 0x6);
		}
	}
}

void halbtc8192e2ant_action_pan_edr(IN struct btc_coexist *btcoexist)
{
	u8		wifi_rssi_state, bt_rssi_state = BTC_RSSI_STATE_HIGH;
	u32		wifi_bw;
	u8		bt_thresh0 = 0, bt_thresh1 = 0;

	halbtc8192e2ant_get_bt_rssi_threshold(btcoexist, &bt_thresh0,
					      &bt_thresh1);
	bt_rssi_state = halbtc8192e2ant_bt_rssi_state(3, bt_thresh0,
			bt_thresh1);
	/*	wifi_rssi_state = halbtc8192e2ant_wifi_rssi_state(btcoexist, 0, 2, 34, 0); */
	wifi_rssi_state = BTC_RSSI_STATE_LOW;

	if (BTC_RSSI_HIGH(wifi_rssi_state) && (!BTC_RSSI_LOW(bt_rssi_state)))
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
	else
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

	if (BTC_RSSI_HIGH(wifi_rssi_state) &&	(!BTC_RSSI_LOW(bt_rssi_state)))
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 1);
	else
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 85);

	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);
	halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	halbtc8192e2ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
}

/* PAN(HS) only */
void halbtc8192e2ant_action_pan_hs(IN struct btc_coexist *btcoexist)
{
	u8		wifi_rssi_state, bt_rssi_state = BTC_RSSI_STATE_HIGH;
	u32		wifi_bw;

	wifi_rssi_state = halbtc8192e2ant_wifi_rssi_state(btcoexist, 0, 2, 15,
			  0);
	bt_rssi_state = halbtc8192e2ant_bt_rssi_state(3, 34, 42);

	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);

	if ((bt_rssi_state == BTC_RSSI_STATE_LOW) ||
	    (bt_rssi_state == BTC_RSSI_STATE_STAY_LOW))
		halbtc8192e2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);
	else if ((bt_rssi_state == BTC_RSSI_STATE_MEDIUM) ||
		 (bt_rssi_state == BTC_RSSI_STATE_STAY_MEDIUM))
		halbtc8192e2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
	else if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		 (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
		halbtc8192e2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 4);
	halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8192e2ant_sw_mechanism1(btcoexist, true, false,
						      false, false);
			halbtc8192e2ant_sw_mechanism2(btcoexist, true, false,
						      false, 0x18);
		} else {
			halbtc8192e2ant_sw_mechanism1(btcoexist, true, false,
						      false, false);
			halbtc8192e2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8192e2ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8192e2ant_sw_mechanism2(btcoexist, true, false,
						      false, 0x18);
		} else {
			halbtc8192e2ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8192e2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	}
}

/* PAN(EDR)+A2DP */
void halbtc8192e2ant_action_pan_edr_a2dp(IN struct btc_coexist *btcoexist)
{
	u8		wifi_rssi_state, bt_rssi_state = BTC_RSSI_STATE_HIGH;
	u32		wifi_bw;
	u8		bt_thresh0 = 0, bt_thresh1 = 0;

	halbtc8192e2ant_get_bt_rssi_threshold(btcoexist, &bt_thresh0,
					      &bt_thresh1);
	bt_rssi_state = halbtc8192e2ant_bt_rssi_state(3, bt_thresh0,
			bt_thresh1);
	/*	wifi_rssi_state = halbtc8192e2ant_wifi_rssi_state(btcoexist, 0, 2, 34, 0); */
	wifi_rssi_state = BTC_RSSI_STATE_LOW;

	if (BTC_RSSI_HIGH(wifi_rssi_state) && (!BTC_RSSI_LOW(bt_rssi_state)))
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
	else
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

	if (BTC_RSSI_HIGH(wifi_rssi_state) && (!BTC_RSSI_LOW(bt_rssi_state)))
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 4);
	else
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 86);

	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);
	halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	halbtc8192e2ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);

}

void halbtc8192e2ant_action_pan_edr_hid(IN struct btc_coexist *btcoexist)
{
	u8		wifi_rssi_state, bt_rssi_state = BTC_RSSI_STATE_HIGH;
	u32		wifi_bw;
	u8		bt_thresh0 = 0, bt_thresh1 = 0;

	halbtc8192e2ant_get_bt_rssi_threshold(btcoexist, &bt_thresh0,
					      &bt_thresh1);
	bt_rssi_state = halbtc8192e2ant_bt_rssi_state(3, bt_thresh0,
			bt_thresh1);

	wifi_rssi_state = halbtc8192e2ant_wifi_rssi_state(btcoexist, 0, 2, 34,
			  0);

	if (BTC_RSSI_HIGH(wifi_rssi_state) && (!BTC_RSSI_LOW(bt_rssi_state)))
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 3);
	else
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

	if (BTC_RSSI_HIGH(wifi_rssi_state) &&	(!BTC_RSSI_LOW(bt_rssi_state)))
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 10);
	else
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 86);

	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);
	halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	halbtc8192e2ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
}

/* HID+A2DP+PAN(EDR) */
void halbtc8192e2ant_action_hid_a2dp_pan_edr(IN struct btc_coexist *btcoexist)
{
	u8		wifi_rssi_state, bt_rssi_state = BTC_RSSI_STATE_HIGH;
	u32		wifi_bw;
	u8		bt_thresh0 = 0, bt_thresh1 = 0;

	halbtc8192e2ant_get_bt_rssi_threshold(btcoexist, &bt_thresh0,
					      &bt_thresh1);
	bt_rssi_state = halbtc8192e2ant_bt_rssi_state(3, bt_thresh0,
			bt_thresh1);
	wifi_rssi_state = halbtc8192e2ant_wifi_rssi_state(btcoexist, 0, 2, 34,
			  0);

	if (BTC_RSSI_HIGH(wifi_rssi_state) && (!BTC_RSSI_LOW(bt_rssi_state)))
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 3);
	else
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

	if (BTC_RSSI_HIGH(wifi_rssi_state) && (!BTC_RSSI_LOW(bt_rssi_state)))
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 4);
	else
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 86);

	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);
	halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	halbtc8192e2ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
}

void halbtc8192e2ant_action_hid_a2dp(IN struct btc_coexist *btcoexist)
{
	u8		wifi_rssi_state, bt_rssi_state = BTC_RSSI_STATE_HIGH;
	u32		wifi_bw;
	u8		bt_thresh0 = 0, bt_thresh1 = 0, anttype = 0;

	btcoexist->btc_get(btcoexist, BTC_GET_U1_ANT_TYPE, &anttype);

	wifi_rssi_state = halbtc8192e2ant_wifi_rssi_state(btcoexist, 0, 2, 34, 0);
	bt_rssi_state = halbtc8192e2ant_bt_rssi_state(3, 34, 42);

	if (anttype == 0) {
		/*2ant with SPDT*/
		if ((coex_sta->scan_ap_num > 40) && (BTC_RSSI_HIGH(wifi_rssi_state) && (!BTC_RSSI_LOW(bt_rssi_state)))) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 31);
			halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 11);
		} else {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 88);
			halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);
		}
	} else if (anttype == 1) {
		/*2ant with coupler and bad ant. isolation, 3ant with bad ant. isolation*/
		if (BTC_RSSI_HIGH(wifi_rssi_state) && (!BTC_RSSI_LOW(bt_rssi_state))) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 25);
			halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);
		} else {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 87);
			halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);
		}
	} else if (anttype == 2) {
		/*ANTTYPE = 2, 2ant with coupler and normal/good ant. isolation, 3ant with normal ant. isolation*/
		if (BTC_RSSI_HIGH(wifi_rssi_state) && (!BTC_RSSI_LOW(bt_rssi_state))) {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 25);
			halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);
		} else {
			halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 87);
			halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);
		}
	} else if (anttype == 3) {
		/*ANTTYPE = 3,  3ant with good ant. isolation*/
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
	}

	halbtc8192e2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);
	halbtc8192e2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	halbtc8192e2ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8192e2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
}

void halbtc8192e2ant_run_coexist_mechanism(IN struct btc_coexist *btcoexist)
{
	u8				algorithm = 0;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], RunCoexistMechanism()===>\n");
	BTC_TRACE(trace_buf);

	if (btcoexist->manual_control) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], RunCoexistMechanism(), return for Manual CTRL <===\n");
		BTC_TRACE(trace_buf);
		return;
	}

	if (coex_sta->under_ips) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], wifi is under IPS !!!\n");
		BTC_TRACE(trace_buf);
		return;
	}

	algorithm = halbtc8192e2ant_action_algorithm(btcoexist);
	if (coex_sta->c2h_bt_inquiry_page &&
	    (BT_8192E_2ANT_COEX_ALGO_PANHS != algorithm)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is under inquiry/page scan !!\n");
		BTC_TRACE(trace_buf);
		halbtc8192e2ant_action_bt_inquiry(btcoexist);
		return;
	}

	if (coex_sta->is_setup_link) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is re-link !!!\n");
		halbtc8192e2ant_action_bt_relink(btcoexist);
		return;
	}


	coex_dm->cur_algorithm = algorithm;
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Algorithm = %d\n",
		    coex_dm->cur_algorithm);
	BTC_TRACE(trace_buf);

	if (halbtc8192e2ant_is_common_action(btcoexist)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Action 2-Ant common.\n");
		BTC_TRACE(trace_buf);
		coex_dm->auto_tdma_adjust = false;
		coex_dm->auto_tdma_adjust_low_rssi = false;

	} else {
		if (coex_dm->cur_algorithm != coex_dm->pre_algorithm) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], pre_algorithm=%d, cur_algorithm=%d\n",
				coex_dm->pre_algorithm, coex_dm->cur_algorithm);
			BTC_TRACE(trace_buf);
			coex_dm->auto_tdma_adjust = false;
			coex_dm->auto_tdma_adjust_low_rssi = false;

		}
		switch (coex_dm->cur_algorithm) {
		case BT_8192E_2ANT_COEX_ALGO_SCO:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = SCO.\n");
			BTC_TRACE(trace_buf);
			halbtc8192e2ant_action_sco(btcoexist);
			break;
		case BT_8192E_2ANT_COEX_ALGO_SCO_PAN:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = SCO+PAN(EDR).\n");
			BTC_TRACE(trace_buf);
			halbtc8192e2ant_action_sco_pan(btcoexist);
			break;
		case BT_8192E_2ANT_COEX_ALGO_HID:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = HID.\n");
			BTC_TRACE(trace_buf);
			halbtc8192e2ant_action_hid(btcoexist);
			break;
		case BT_8192E_2ANT_COEX_ALGO_A2DP:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = A2DP.\n");
			BTC_TRACE(trace_buf);
			halbtc8192e2ant_action_a2dp(btcoexist);
			break;
		case BT_8192E_2ANT_COEX_ALGO_A2DP_PANHS:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = A2DP+PAN(HS).\n");
			BTC_TRACE(trace_buf);
			halbtc8192e2ant_action_a2dp_pan_hs(btcoexist);
			break;
		case BT_8192E_2ANT_COEX_ALGO_PANEDR:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = PAN(EDR).\n");
			BTC_TRACE(trace_buf);
			halbtc8192e2ant_action_pan_edr(btcoexist);
			break;
		case BT_8192E_2ANT_COEX_ALGO_PANHS:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = HS mode.\n");
			BTC_TRACE(trace_buf);
			halbtc8192e2ant_action_pan_hs(btcoexist);
			break;
		case BT_8192E_2ANT_COEX_ALGO_PANEDR_A2DP:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = PAN+A2DP.\n");
			BTC_TRACE(trace_buf);
			halbtc8192e2ant_action_pan_edr_a2dp(btcoexist);
			break;
		case BT_8192E_2ANT_COEX_ALGO_PANEDR_HID:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = PAN(EDR)+HID.\n");
			BTC_TRACE(trace_buf);
			halbtc8192e2ant_action_pan_edr_hid(btcoexist);
			break;
		case BT_8192E_2ANT_COEX_ALGO_HID_A2DP_PANEDR:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = HID+A2DP+PAN.\n");
			BTC_TRACE(trace_buf);
			halbtc8192e2ant_action_hid_a2dp_pan_edr(
				btcoexist);
			break;
		case BT_8192E_2ANT_COEX_ALGO_HID_A2DP:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = HID+A2DP.\n");
			BTC_TRACE(trace_buf);
			halbtc8192e2ant_action_hid_a2dp(btcoexist);
			break;
		default:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = unknown!!\n");
			BTC_TRACE(trace_buf);
			halbtc8192e2ant_coex_all_off(btcoexist);
			break;
		}
		coex_dm->pre_algorithm = coex_dm->cur_algorithm;
	}
}

void halbtc8192e2ant_init_hw_config(IN struct btc_coexist *btcoexist,
				    IN boolean back_up)
{
	u16	u16tmp = 0;
	u8	u8tmp = 0;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], 2Ant Init HW Config!!\n");
	BTC_TRACE(trace_buf);

	if (back_up) {
		/* backup rf 0x1e value */
		coex_dm->bt_rf_0x1e_backup =
			btcoexist->btc_get_rf_reg(btcoexist, BTC_RF_A, 0x1e,
						  0xfffff);

		coex_dm->backup_arfr_cnt1 = btcoexist->btc_read_4byte(btcoexist,
					    0x430);
		coex_dm->backup_arfr_cnt2 = btcoexist->btc_read_4byte(btcoexist,
					    0x434);
		coex_dm->backup_retry_limit = btcoexist->btc_read_2byte(
						      btcoexist, 0x42a);
		coex_dm->backup_ampdu_max_time = btcoexist->btc_read_1byte(
				btcoexist, 0x456);
	}

	/* antenna sw ctrl to bt */
	halbtc8192e2ant_set_ant_path(btcoexist, BTC_ANT_PATH_BT, true, false);

	halbtc8192e2ant_coex_table_with_type(btcoexist, FORCE_EXEC, 0);

	/* antenna switch control parameter */
	/*	btcoexist->btc_write_4byte(btcoexist, 0x858, 0x55555555); */

	/* coex parameters */
	btcoexist->btc_write_1byte(btcoexist, 0x778, 0x3);
	/* 0x790[5:0]=0x5 */
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
	u16tmp |= BIT(9);
	btcoexist->btc_write_2byte(btcoexist, 0x40, u16tmp);

	/* enable PTA I2C mailbox */
	u8tmp = btcoexist->btc_read_1byte(btcoexist, 0x101);
	u8tmp |= BIT(4);
	btcoexist->btc_write_1byte(btcoexist, 0x101, u8tmp);

	/* enable bt clock when wifi is disabled. */
	u8tmp = btcoexist->btc_read_1byte(btcoexist, 0x93);
	u8tmp |= BIT(0);
	btcoexist->btc_write_1byte(btcoexist, 0x93, u8tmp);
	/* enable bt clock when suspend. */
	u8tmp = btcoexist->btc_read_1byte(btcoexist, 0x7);
	u8tmp |= BIT(0);
	btcoexist->btc_write_1byte(btcoexist, 0x7, u8tmp);

	/* Give bt_coex_supported_version the default value */
	coex_sta->bt_coex_supported_version = 0;
}

/* ************************************************************
 * work around function start with wa_halbtc8192e2ant_
 * ************************************************************
 * ************************************************************
 * extern function start with ex_halbtc8192e2ant_
 * ************************************************************ */
void ex_halbtc8192e2ant_power_on_setting(IN struct btc_coexist *btcoexist)
{
}

void ex_halbtc8192e2ant_init_hw_config(IN struct btc_coexist *btcoexist,
				       IN boolean wifi_only)
{
	halbtc8192e2ant_init_hw_config(btcoexist, true);
}

void ex_halbtc8192e2ant_init_coex_dm(IN struct btc_coexist *btcoexist)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], Coex Mechanism Init!!\n");
	BTC_TRACE(trace_buf);

	halbtc8192e2ant_init_coex_dm(btcoexist);
}

void ex_halbtc8192e2ant_display_coex_info(IN struct btc_coexist *btcoexist)
{
	struct  btc_board_info		*board_info = &btcoexist->board_info;
	struct  btc_stack_info		*stack_info = &btcoexist->stack_info;
	u8				*cli_buf = btcoexist->cli_buf;
	u8				u8tmp[4], i, bt_info_ext, ps_tdma_case = 0;
	u16				u16tmp[4];
	u32				u32tmp[4];
	u32				fa_ofdm, fa_cck, cca_ofdm, cca_cck;
	u32				fw_ver = 0, bt_patch_ver = 0, bt_coex_ver = 0;
	u32				phyver = 0;

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n ============[BT Coexist info]============");
	CL_PRINTF(cli_buf);

	if (btcoexist->manual_control) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			"\r\n ============[Under Manual Control]============");
		CL_PRINTF(cli_buf);
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n ==========================================");
		CL_PRINTF(cli_buf);
	}

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d ",
		   "Ant PG number/ Ant mechanism:",
		   board_info->pg_ant_num, board_info->btdm_ant_num);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "Antenna type:",
		   board_info->ant_type);
	CL_PRINTF(cli_buf);

	if (!coex_sta->bt_disabled) {
		if ((coex_sta->bt_coex_supported_version == 0) ||
			(coex_sta->bt_coex_supported_version == 0xffff))
			btcoexist->btc_get(btcoexist, BTC_GET_U4_SUPPORTED_VERSION,
						&coex_sta->bt_coex_supported_version);

		btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER,
						&bt_patch_ver);
		btcoexist->bt_info.bt_get_fw_ver = bt_patch_ver;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER, &bt_patch_ver);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
	phyver = btcoexist->btc_get_bt_phydm_version(btcoexist);
	bt_coex_ver = coex_sta->bt_coex_supported_version & 0xff;

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %d_%02x/ 0x%02x/ 0x%02x (%s)",
		   "CoexVer WL/  BT_Desired/ BT_Report",
		   glcoex_ver_date_8192e_2ant, glcoex_ver_8192e_2ant,
		   glcoex_ver_btdesired_8192e_2ant,
		   bt_coex_ver,
		   (bt_coex_ver == 0xff ? "Unknown" :
		    (coex_sta->bt_disabled ? "BT-disable" :
		     (bt_coex_ver >= glcoex_ver_btdesired_8192e_2ant ?
		      "Match" : "Mis-Match"))));
	CL_PRINTF(cli_buf);


	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ v%d",
		   "W_FW/ B_FW/ Phy", fw_ver, bt_patch_ver, phyver);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x ",
		   "Wifi channel informed to BT",
		   coex_dm->wifi_chnl_info[0], coex_dm->wifi_chnl_info[1],
		   coex_dm->wifi_chnl_info[2]);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/", "WifibHiPri",
		(coex_sta->wifi_is_high_pri_task ? "Yes" : "No"));
	CL_PRINTF(cli_buf);

#if 0
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/", "test patch version",
			"20161003_v3");
		CL_PRINTF(cli_buf);
#endif

	/* wifi status */
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
		   "============[Wifi Status]============");
	CL_PRINTF(cli_buf);
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_WIFI_STATUS);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
		   "============[BT Status]============");
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = [%s/ %d/ %d] ",
		   "BT [status/ rssi/ retryCnt]",
		   ((coex_sta->bt_disabled) ? ("disabled") :	((
		   coex_sta->c2h_bt_inquiry_page) ? ("inquiry/page scan")
			   : ((BT_8192E_2ANT_BT_STATUS_NON_CONNECTED_IDLE ==
			       coex_dm->bt_status) ? "non-connected idle" :
		((BT_8192E_2ANT_BT_STATUS_CONNECTED_IDLE == coex_dm->bt_status)
				       ? "connected-idle" : "busy")))),
		   coex_sta->bt_rssi, coex_sta->bt_retry_cnt);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d / %d",
		   "SCO/HID/PAN/A2DP",
		   stack_info->sco_exist, stack_info->hid_exist,
		   stack_info->pan_exist, stack_info->a2dp_exist);
	CL_PRINTF(cli_buf);
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_BT_LINK_INFO);

	bt_info_ext = coex_sta->bt_info_ext;
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s",
		   "BT Info A2DP rate",
		   (bt_info_ext & BIT(0)) ? "Basic rate" : "EDR rate");
	CL_PRINTF(cli_buf);

	for (i = 0; i < BT_INFO_SRC_8192E_2ANT_MAX; i++) {
		if (coex_sta->bt_info_c2h_cnt[i]) {
			CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
				"\r\n %-35s = %02x %02x %02x %02x %02x %02x %02x(%d)",
				   glbt_info_src_8192e_2ant[i],
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

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x ", "SS Type",
		   coex_dm->cur_ss_type);
	CL_PRINTF(cli_buf);

	/* Sw mechanism	 */
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
		   "============[Sw mechanism]============");
	CL_PRINTF(cli_buf);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d ",
		   "SM1[ShRf/ LpRA/ LimDig]",
		   coex_dm->cur_rf_rx_lpf_shrink, coex_dm->cur_low_penalty_ra,
		   coex_dm->limited_dig);
	CL_PRINTF(cli_buf);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d(0x%x) ",
		   "SM2[AgcT/ AdcB/ SwDacSwing(lvl)]",
		   coex_dm->cur_agc_table_en, coex_dm->cur_adc_back_off,
		   coex_dm->cur_dac_swing_on, coex_dm->cur_dac_swing_lvl);
	CL_PRINTF(cli_buf);

	/* Fw mechanism		 */
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
		   "============[Fw mechanism]============");
	CL_PRINTF(cli_buf);

	ps_tdma_case = coex_dm->cur_ps_tdma;
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %02x %02x %02x %02x %02x case-%d (auto:%d)",
		   "PS TDMA",
		   coex_dm->ps_tdma_para[0], coex_dm->ps_tdma_para[1],
		   coex_dm->ps_tdma_para[2], coex_dm->ps_tdma_para[3],
		   coex_dm->ps_tdma_para[4], ps_tdma_case,
		   coex_dm->auto_tdma_adjust);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d ",
		   "DecBtPwr/ IgnWlanAct",
		   coex_dm->cur_bt_dec_pwr_lvl, coex_dm->cur_ignore_wlan_act);
	CL_PRINTF(cli_buf);

	/* Hw setting		 */
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
		   "============[Hw setting]============");
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x",
		   "RF-A, 0x1e initVal",
		   coex_dm->bt_rf_0x1e_backup);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/0x%x/0x%x/0x%x",
		   "backup ARFR1/ARFR2/RL/AMaxTime",
		   coex_dm->backup_arfr_cnt1, coex_dm->backup_arfr_cnt2,
		   coex_dm->backup_retry_limit,
		   coex_dm->backup_ampdu_max_time);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x430);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x434);
	u16tmp[0] = btcoexist->btc_read_2byte(btcoexist, 0x42a);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x456);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/0x%x/0x%x/0x%x",
		   "0x430/0x434/0x42a/0x456",
		   u32tmp[0], u32tmp[1], u16tmp[0], u8tmp[0]);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xc04);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0xd04);
	u32tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0x90c);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0xc04/ 0xd04/ 0x90c",
		   u32tmp[0], u32tmp[1], u32tmp[2]);
	CL_PRINTF(cli_buf);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x778);
	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x880);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x", "0x778/0x880[29:25]",
		u8tmp[0], (u32tmp[0] & 0x3e000000) >> 25);
	CL_PRINTF(cli_buf);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x92c);
	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x930);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "0x92c/ 0x930",
		   (u8tmp[0]), u32tmp[0]);
	CL_PRINTF(cli_buf);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x40);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x4f);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "0x40/ 0x4f",
		   u8tmp[0], u8tmp[1]);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x550);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x522);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "0x550(bcn ctrl)/0x522",
		   u32tmp[0], u8tmp[0]);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xc50);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "0xc50(dig)",
		   u32tmp[0]);
	CL_PRINTF(cli_buf);

	fa_ofdm = btcoexist->btc_phydm_query_PHY_counter(btcoexist,
			PHYDM_INFO_FA_OFDM);
	fa_cck = btcoexist->btc_phydm_query_PHY_counter(btcoexist,
			PHYDM_INFO_FA_CCK);
	cca_ofdm = btcoexist->btc_phydm_query_PHY_counter(btcoexist,
			PHYDM_INFO_CCA_OFDM);
	cca_cck = btcoexist->btc_phydm_query_PHY_counter(btcoexist,
			PHYDM_INFO_CCA_CCK);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x",
		   "CCK-CCA/CCK-FA/OFDM-CCA/OFDM-FA",
		   cca_cck, fa_cck, cca_ofdm, fa_ofdm);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d",
		   "CRC_OK CCK/11g/11n/11n-agg",
		   coex_sta->crc_ok_cck, coex_sta->crc_ok_11g,
		   coex_sta->crc_ok_11n, coex_sta->crc_ok_11n_vht);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d",
		   "CRC_Err CCK/11g/11n/11n-agg",
		   coex_sta->crc_err_cck, coex_sta->crc_err_11g,
		   coex_sta->crc_err_11n, coex_sta->crc_err_11n_vht);
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
		   "0x770(hp rx[31:16]/tx[15:0])",
		   coex_sta->high_priority_rx, coex_sta->high_priority_tx);
	CL_PRINTF(cli_buf);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "0x774(lp rx[31:16]/tx[15:0])",
		   coex_sta->low_priority_rx, coex_sta->low_priority_tx);
	CL_PRINTF(cli_buf);
#if (BT_AUTO_REPORT_ONLY_8192E_2ANT == 1)
	halbtc8192e2ant_monitor_bt_ctr(btcoexist);
#endif
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_COEX_STATISTICS);
}


void ex_halbtc8192e2ant_ips_notify(IN struct btc_coexist *btcoexist, IN u8 type)
{
	if (BTC_IPS_ENTER == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], IPS ENTER notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_ips = true;
		halbtc8192e2ant_coex_all_off(btcoexist);
		halbtc8192e2ant_set_ant_path(btcoexist, BTC_ANT_PATH_BT, false, true);
		halbtc8192e2ant_ignore_wlan_act(btcoexist, FORCE_EXEC, true);
		ex_halbtc8192e2ant_media_status_notify(btcoexist, BTC_MEDIA_DISCONNECT);
	} else if (BTC_IPS_LEAVE == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], IPS LEAVE notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_ips = false;
		halbtc8192e2ant_init_hw_config(btcoexist, false);
		halbtc8192e2ant_init_coex_dm(btcoexist);
	}
}

void ex_halbtc8192e2ant_lps_notify(IN struct btc_coexist *btcoexist, IN u8 type)
{
	if (BTC_LPS_ENABLE == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], LPS ENABLE notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_lps = true;

	} else if (BTC_LPS_DISABLE == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], LPS DISABLE notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_lps = false;
	}
}

void ex_halbtc8192e2ant_scan_notify(IN struct btc_coexist *btcoexist,
				    IN u8 type)
{
	if (BTC_SCAN_START == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], SCAN START notify\n");
		BTC_TRACE(trace_buf);
	} else if (BTC_SCAN_FINISH == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], SCAN FINISH notify\n");
		BTC_TRACE(trace_buf);
		btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM,
				   &coex_sta->scan_ap_num);

	}
}

void ex_halbtc8192e2ant_connect_notify(IN struct btc_coexist *btcoexist,
				       IN u8 type)
{
	if (BTC_ASSOCIATE_START == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], CONNECT START notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->wifi_is_high_pri_task = true;
		coex_sta->cnt_wifi_high_pri = 2;
		halbtc8192e2ant_ignore_wlan_act(btcoexist, FORCE_EXEC, false);
		halbtc8192e2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 32);
		halbtc8192e2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 11);
		halbtc8192e2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);
	} else if (BTC_ASSOCIATE_FINISH == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], CONNECT FINISH notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->wifi_is_high_pri_task = false;
		halbtc8192e2ant_run_coexist_mechanism(btcoexist);
	}
}

void ex_halbtc8192e2ant_media_status_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	u8			h2c_parameter[3] = {0};
	u32			wifi_bw;
	u8			wifi_central_chnl;

	if (btcoexist->manual_control ||
	    btcoexist->stop_coex_dm ||
	    coex_sta->bt_disabled)
		return;

	if (BTC_MEDIA_CONNECT == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], MEDIA connect notify\n");
		BTC_TRACE(trace_buf);
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], MEDIA disconnect notify\n");
		BTC_TRACE(trace_buf);
	}

	/* only 2.4G we need to inform bt the chnl mask */
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_CENTRAL_CHNL,
			   &wifi_central_chnl);
	if ((BTC_MEDIA_CONNECT == type) &&
	    (wifi_central_chnl <= 14)) {
		h2c_parameter[0] = 0x1;
		h2c_parameter[1] = wifi_central_chnl;
		btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
		if (BTC_WIFI_BW_HT40 == wifi_bw)
			h2c_parameter[2] = 0x30;
		else
			h2c_parameter[2] = 0x20;
	}

	coex_dm->wifi_chnl_info[0] = h2c_parameter[0];
	coex_dm->wifi_chnl_info[1] = h2c_parameter[1];
	coex_dm->wifi_chnl_info[2] = h2c_parameter[2];

	btcoexist->btc_fill_h2c(btcoexist, 0x66, 3, h2c_parameter);
}

void ex_halbtc8192e2ant_specific_packet_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	if (type == BTC_PACKET_DHCP) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], DHCP Packet notify\n");
		BTC_TRACE(trace_buf);
	}
}

void ex_halbtc8192e2ant_bt_info_notify(IN struct btc_coexist *btcoexist,
				       IN u8 *tmp_buf, IN u8 length)
{
	u8			bt_info = 0;
	u8			i, rsp_source = 0;
	boolean			bt_busy = false, limited_dig = false;
	boolean			wifi_connected = false;
	static u8		bt_info_for_wifi_fw_count = 0;

	coex_sta->c2h_bt_info_req_sent = false;

	rsp_source = tmp_buf[0] & 0xf;
	if (rsp_source >= BT_INFO_SRC_8192E_2ANT_MAX)
		rsp_source = BT_INFO_SRC_8192E_2ANT_WIFI_FW;
	coex_sta->bt_info_c2h_cnt[rsp_source]++;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], Bt info[%d], length=%d, hex data=[", rsp_source,
		    length);
	BTC_TRACE(trace_buf);

	/*avoid mailbox issue*/
	if (rsp_source == BT_INFO_SRC_8192E_2ANT_WIFI_FW) {
		bt_info_for_wifi_fw_count++;
		if (bt_info_for_wifi_fw_count < 5)
			return;
	} else
		bt_info_for_wifi_fw_count = 0;

	for (i = 0; i < length; i++) {
		coex_sta->bt_info_c2h[rsp_source][i] = tmp_buf[i];
		if (i == 1)
			bt_info = tmp_buf[i];
		if (i == length - 1) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "0x%02x]\n",
				    tmp_buf[i]);
			BTC_TRACE(trace_buf);
		} else {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "0x%02x, ",
				    tmp_buf[i]);
			BTC_TRACE(trace_buf);
		}
	}

	if (BT_INFO_SRC_8192E_2ANT_WIFI_FW != rsp_source) {
		coex_sta->bt_retry_cnt =	/* [3:0] */
			coex_sta->bt_info_c2h[rsp_source][2] & 0xf;

		coex_sta->bt_rssi =
			coex_sta->bt_info_c2h[rsp_source][3] * 2 + 10;

		coex_sta->bt_info_ext =
			coex_sta->bt_info_c2h[rsp_source][4];

		/* Here we need to resend some wifi info to BT */
		/* because bt is reset and loss of the info. */
		if ((coex_sta->bt_info_ext & BIT(1))) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], BT ext info bit1 check, send wifi BW&Chnl to BT!!\n");
			BTC_TRACE(trace_buf);
			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
					   &wifi_connected);
			if (wifi_connected)
				ex_halbtc8192e2ant_media_status_notify(
					btcoexist, BTC_MEDIA_CONNECT);
			else
				ex_halbtc8192e2ant_media_status_notify(
					btcoexist, BTC_MEDIA_DISCONNECT);
		}

		if (coex_sta->bt_info_ext & BIT(2)) {
			coex_sta->cnt_setup_link++;
			coex_sta->is_setup_link = true;

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Re-Link start in BT info!!\n");
			BTC_TRACE(trace_buf);
		} else
			coex_sta->is_setup_link = false;

		if ((coex_sta->bt_info_ext & BIT(3))) {
			if (!btcoexist->manual_control &&
			    !btcoexist->stop_coex_dm) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT ext info bit3 check, set BT NOT to ignore Wlan active!!\n");
				BTC_TRACE(trace_buf);
				halbtc8192e2ant_ignore_wlan_act(btcoexist,
							FORCE_EXEC, false);
			}
		} else {
			/* BT already NOT ignore Wlan active, do nothing here. */
		}

#if (BT_AUTO_REPORT_ONLY_8192E_2ANT == 0)
		if ((coex_sta->bt_info_ext & BIT(4))) {
			/* BT auto report already enabled, do nothing */
		} else
			halbtc8192e2ant_bt_auto_report(btcoexist, FORCE_EXEC,
						       true);
#endif
	}

	/* check BIT2 first ==> check if bt is under inquiry or page scan */
	if (bt_info & BT_INFO_8192E_2ANT_B_INQ_PAGE)
		coex_sta->c2h_bt_inquiry_page = true;
	else
		coex_sta->c2h_bt_inquiry_page = false;

	/* set link exist status */
	if (!(bt_info & BT_INFO_8192E_2ANT_B_CONNECTION)) {
		coex_sta->bt_link_exist = false;
		coex_sta->pan_exist = false;
		coex_sta->a2dp_exist = false;
		coex_sta->hid_exist = false;
		coex_sta->sco_exist = false;
	} else {	/* connection exists */
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

	halbtc8192e2ant_update_bt_link_info(btcoexist);

	if (!(bt_info & BT_INFO_8192E_2ANT_B_CONNECTION)) {
		coex_dm->bt_status = BT_8192E_2ANT_BT_STATUS_NON_CONNECTED_IDLE;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], BtInfoNotify(), BT Non-Connected idle!!!\n");
		BTC_TRACE(trace_buf);
	} else if (bt_info ==
		BT_INFO_8192E_2ANT_B_CONNECTION) {	/* connection exists but no busy */
		coex_dm->bt_status = BT_8192E_2ANT_BT_STATUS_CONNECTED_IDLE;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT Connected-idle!!!\n");
		BTC_TRACE(trace_buf);
	} else if ((bt_info & BT_INFO_8192E_2ANT_B_SCO_ESCO) ||
		   (bt_info & BT_INFO_8192E_2ANT_B_SCO_BUSY)) {
		coex_dm->bt_status = BT_8192E_2ANT_BT_STATUS_SCO_BUSY;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT SCO busy!!!\n");
		BTC_TRACE(trace_buf);
	} else if (bt_info & BT_INFO_8192E_2ANT_B_ACL_BUSY) {
		coex_dm->bt_status = BT_8192E_2ANT_BT_STATUS_ACL_BUSY;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT ACL busy!!!\n");
		BTC_TRACE(trace_buf);
	} else {
		coex_dm->bt_status = BT_8192E_2ANT_BT_STATUS_MAX;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], BtInfoNotify(), BT Non-Defined state!!!\n");
		BTC_TRACE(trace_buf);
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

void ex_halbtc8192e2ant_halt_notify(IN struct btc_coexist *btcoexist)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Halt notify\n");
	BTC_TRACE(trace_buf);

	halbtc8192e2ant_set_ant_path(btcoexist, BTC_ANT_PATH_BT, false, true);
	halbtc8192e2ant_ignore_wlan_act(btcoexist, FORCE_EXEC, true);
	ex_halbtc8192e2ant_media_status_notify(btcoexist, BTC_MEDIA_DISCONNECT);
}

void ex_halbtc8192e2ant_periodical(IN struct btc_coexist *btcoexist)
{
	boolean			wifi_connected = false;
	static u8			count = 0;
	static boolean		pre_wifi_connected = false;

/*If wifi is connecting, the update of wifi channel mask may fail caused by wifi FW*/
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED, &wifi_connected);

	if (pre_wifi_connected != wifi_connected)
		count = 0;

	if  (count < 10)
		count++;

	if (count == 2) {
		if (wifi_connected)
			ex_halbtc8192e2ant_media_status_notify(btcoexist, BTC_MEDIA_CONNECT);
		else
			ex_halbtc8192e2ant_media_status_notify(btcoexist, BTC_MEDIA_DISCONNECT);
	}

	pre_wifi_connected = wifi_connected;
/*If wifi is connecting, the update of wifi channel mask may fail caused by wifi FW*/

#if (BT_AUTO_REPORT_ONLY_8192E_2ANT == 0)
	halbtc8192e2ant_query_bt_info(btcoexist);
	halbtc8192e2ant_monitor_bt_ctr(btcoexist);
	halbtc8192e2ant_monitor_wifi_ctr(btcoexist);
	halbtc8192e2ant_monitor_bt_enable_disable(btcoexist);
#else
	halbtc8192e2ant_monitor_wifi_ctr(btcoexist);

	if (halbtc8192e2ant_is_wifi_status_changed(btcoexist) ||
	    coex_dm->auto_tdma_adjust)
		halbtc8192e2ant_run_coexist_mechanism(btcoexist);
#endif
}

#endif

#endif	/* #if (BT_SUPPORT == 1 && COEX_SUPPORT == 1) */


