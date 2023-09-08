// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2012  Realtek Corporation.*/

/***************************************************************
 * Description:
 *
 * This file is for RTL8723B Co-exist mechanism
 *
 * History
 * 2012/11/15 Cosa first check in.
 *
 ***************************************************************/

/***************************************************************
 * include files
 ***************************************************************/
#include "halbt_precomp.h"
/***************************************************************
 * Global variables, these are static variables
 ***************************************************************/
static struct coex_dm_8723b_1ant glcoex_dm_8723b_1ant;
static struct coex_dm_8723b_1ant *coex_dm = &glcoex_dm_8723b_1ant;
static struct coex_sta_8723b_1ant glcoex_sta_8723b_1ant;
static struct coex_sta_8723b_1ant *coex_sta = &glcoex_sta_8723b_1ant;

static const char *const glbt_info_src_8723b_1ant[] = {
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

static u32 glcoex_ver_date_8723b_1ant = 20130918;
static u32 glcoex_ver_8723b_1ant = 0x47;

/***************************************************************
 * local function proto type if needed
 ***************************************************************/
/***************************************************************
 * local function start with halbtc8723b1ant_
 ***************************************************************/

static void halbtc8723b1ant_updatera_mask(struct btc_coexist *btcoexist,
					  bool force_exec, u32 dis_rate_mask)
{
	coex_dm->curra_mask = dis_rate_mask;

	if (force_exec || (coex_dm->prera_mask != coex_dm->curra_mask))
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_UPDATE_RAMASK,
				   &coex_dm->curra_mask);

	coex_dm->prera_mask = coex_dm->curra_mask;
}

static void btc8723b1ant_auto_rate_fb_retry(struct btc_coexist *btcoexist,
					    bool force_exec, u8 type)
{
	bool wifi_under_bmode = false;

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
					   &wifi_under_bmode);
			if (wifi_under_bmode) {
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

static void halbtc8723b1ant_retry_limit(struct btc_coexist *btcoexist,
					bool force_exec, u8 type)
{
	coex_dm->cur_retry_limit_type = type;

	if (force_exec || (coex_dm->pre_retry_limit_type !=
			   coex_dm->cur_retry_limit_type)) {
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

static void halbtc8723b1ant_ampdu_maxtime(struct btc_coexist *btcoexist,
					  bool force_exec, u8 type)
{
	coex_dm->cur_ampdu_time_type = type;

	if (force_exec || (coex_dm->pre_ampdu_time_type !=
		coex_dm->cur_ampdu_time_type)) {
		switch (coex_dm->cur_ampdu_time_type) {
		case 0:	/* normal mode */
			btcoexist->btc_write_1byte(btcoexist, 0x456,
					coex_dm->backup_ampdu_max_time);
			break;
		case 1:	/* AMPDU timw = 0x38 * 32us */
			btcoexist->btc_write_1byte(btcoexist, 0x456, 0x38);
			break;
		default:
			break;
		}
	}

	coex_dm->pre_ampdu_time_type = coex_dm->cur_ampdu_time_type;
}

static void halbtc8723b1ant_limited_tx(struct btc_coexist *btcoexist,
				       bool force_exec, u8 ra_masktype,
				       u8 arfr_type, u8 retry_limit_type,
				       u8 ampdu_time_type)
{
	switch (ra_masktype) {
	case 0:	/* normal mode */
		halbtc8723b1ant_updatera_mask(btcoexist, force_exec, 0x0);
		break;
	case 1:	/* disable cck 1/2 */
		halbtc8723b1ant_updatera_mask(btcoexist, force_exec,
					      0x00000003);
		break;
	/* disable cck 1/2/5.5, ofdm 6/9/12/18/24, mcs 0/1/2/3/4 */
	case 2:
		halbtc8723b1ant_updatera_mask(btcoexist, force_exec,
					      0x0001f1f7);
		break;
	default:
		break;
	}

	btc8723b1ant_auto_rate_fb_retry(btcoexist, force_exec, arfr_type);
	halbtc8723b1ant_retry_limit(btcoexist, force_exec, retry_limit_type);
	halbtc8723b1ant_ampdu_maxtime(btcoexist, force_exec, ampdu_time_type);
}

static void halbtc8723b1ant_limited_rx(struct btc_coexist *btcoexist,
				       bool force_exec, bool rej_ap_agg_pkt,
				       bool bt_ctrl_agg_buf_size,
				       u8 agg_buf_size)
{
	bool reject_rx_agg = rej_ap_agg_pkt;
	bool bt_ctrl_rx_agg_size = bt_ctrl_agg_buf_size;
	u8 rxaggsize = agg_buf_size;

	/**********************************************
	 *	Rx Aggregation related setting
	 **********************************************/
	btcoexist->btc_set(btcoexist, BTC_SET_BL_TO_REJ_AP_AGG_PKT,
			   &reject_rx_agg);
	/* decide BT control aggregation buf size or not  */
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_CTRL_AGG_SIZE,
			   &bt_ctrl_rx_agg_size);
	/* aggregation buf size, only work
	 * when BT control Rx aggregation size.
	 */
	btcoexist->btc_set(btcoexist, BTC_SET_U1_AGG_BUF_SIZE, &rxaggsize);
	/* real update aggregation setting  */
	btcoexist->btc_set(btcoexist, BTC_SET_ACT_AGGREGATE_CTRL, NULL);
}

static void halbtc8723b1ant_query_bt_info(struct btc_coexist *btcoexist)
{
	u8 h2c_parameter[1] = {0};

	coex_sta->c2h_bt_info_req_sent = true;

	/* trigger */
	h2c_parameter[0] |= BIT(0);

	btcoexist->btc_fill_h2c(btcoexist, 0x61, 1, h2c_parameter);
}

static void halbtc8723b1ant_monitor_bt_ctr(struct btc_coexist *btcoexist)
{
	u32 reg_hp_txrx, reg_lp_txrx, u32tmp;
	u32 reg_hp_tx = 0, reg_hp_rx = 0;
	u32 reg_lp_tx = 0, reg_lp_rx = 0;
	static u32 num_of_bt_counter_chk;

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

	if ((coex_sta->low_priority_tx > 1050) &&
	    (!coex_sta->c2h_bt_inquiry_page))
		coex_sta->pop_event_cnt++;

	/* reset counter */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0xc);

	/* This part is for wifi FW and driver to update BT's status as
	 * disabled.
	 *
	 * The flow is as the following
	 * 1. disable BT
	 * 2. if all BT Tx/Rx counter = 0, after 6 sec we query bt info
	 * 3. Because BT will not rsp from mailbox, so wifi fw will know BT is
	 * disabled
	 *
	 * 4. FW will rsp c2h for BT that driver will know BT is disabled.
	 */
	if ((reg_hp_tx == 0) && (reg_hp_rx == 0) && (reg_lp_tx == 0) &&
	    (reg_lp_rx == 0)) {
		num_of_bt_counter_chk++;
		if (num_of_bt_counter_chk == 3)
			halbtc8723b1ant_query_bt_info(btcoexist);
	} else {
		num_of_bt_counter_chk = 0;
	}
}

static void halbtc8723b1ant_monitor_wifi_ctr(struct btc_coexist *btcoexist)
{
	s32 wifi_rssi = 0;
	bool wifi_busy = false, wifi_under_b_mode = false;
	static u8 cck_lock_counter;
	u32 total_cnt;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_S4_WIFI_RSSI, &wifi_rssi);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_B_MODE,
			   &wifi_under_b_mode);

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

	if ((wifi_busy) && (wifi_rssi >= 30) && (!wifi_under_b_mode)) {
		total_cnt = coex_sta->crc_ok_cck + coex_sta->crc_ok_11g +
			    coex_sta->crc_ok_11n + coex_sta->crc_ok_11n_agg;

		if ((coex_dm->bt_status == BT_8723B_1ANT_BT_STATUS_ACL_BUSY) ||
		    (coex_dm->bt_status ==
		     BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY) ||
		    (coex_dm->bt_status == BT_8723B_1ANT_BT_STATUS_SCO_BUSY)) {
			if (coex_sta->crc_ok_cck >
			    (total_cnt - coex_sta->crc_ok_cck)) {
				if (cck_lock_counter < 3)
					cck_lock_counter++;
			} else {
				if (cck_lock_counter > 0)
					cck_lock_counter--;
			}

		} else {
			if (cck_lock_counter > 0)
				cck_lock_counter--;
		}
	} else {
		if (cck_lock_counter > 0)
			cck_lock_counter--;
	}

	if (!coex_sta->pre_ccklock) {
		if (cck_lock_counter >= 3)
			coex_sta->cck_lock = true;
		else
			coex_sta->cck_lock = false;
	} else {
		if (cck_lock_counter == 0)
			coex_sta->cck_lock = false;
		else
			coex_sta->cck_lock = true;
	}

	if (coex_sta->cck_lock)
		coex_sta->cck_ever_lock = true;

	coex_sta->pre_ccklock = coex_sta->cck_lock;
}

static bool btc8723b1ant_is_wifi_status_changed(struct btc_coexist *btcoexist)
{
	static bool pre_wifi_busy;
	static bool pre_under_4way, pre_bt_hs_on;
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

static void halbtc8723b1ant_update_bt_link_info(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool bt_hs_on = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);

	bt_link_info->bt_link_exist = coex_sta->bt_link_exist;
	bt_link_info->sco_exist = coex_sta->sco_exist;
	bt_link_info->a2dp_exist = coex_sta->a2dp_exist;
	bt_link_info->pan_exist = coex_sta->pan_exist;
	bt_link_info->hid_exist = coex_sta->hid_exist;
	bt_link_info->bt_hi_pri_link_exist = coex_sta->bt_hi_pri_link_exist;

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

static void halbtc8723b1ant_set_bt_auto_report(struct btc_coexist *btcoexist,
					       bool enable_auto_report)
{
	u8 h2c_parameter[1] = {0};

	h2c_parameter[0] = 0;

	if (enable_auto_report)
		h2c_parameter[0] |= BIT(0);

	btcoexist->btc_fill_h2c(btcoexist, 0x68, 1, h2c_parameter);
}

static void halbtc8723b1ant_bt_auto_report(struct btc_coexist *btcoexist,
					   bool force_exec,
					   bool enable_auto_report)
{
	coex_dm->cur_bt_auto_report = enable_auto_report;

	if (!force_exec) {
		if (coex_dm->pre_bt_auto_report == coex_dm->cur_bt_auto_report)
			return;
	}
	halbtc8723b1ant_set_bt_auto_report(btcoexist,
					   coex_dm->cur_bt_auto_report);

	coex_dm->pre_bt_auto_report = coex_dm->cur_bt_auto_report;
}

static void btc8723b1ant_set_sw_pen_tx_rate_adapt(struct btc_coexist *btcoexist,
						  bool low_penalty_ra)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 h2c_parameter[6] = {0};

	h2c_parameter[0] = 0x6;	/* opCode, 0x6= Retry_Penalty */

	if (low_penalty_ra) {
		h2c_parameter[1] |= BIT0;
		/* normal rate except MCS7/6/5, OFDM54/48/36 */
		h2c_parameter[2] = 0x00;
		h2c_parameter[3] = 0xf7;  /* MCS7 or OFDM54 */
		h2c_parameter[4] = 0xf8;  /* MCS6 or OFDM48 */
		h2c_parameter[5] = 0xf9;  /* MCS5 or OFDM36 */
	}

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], set WiFi Low-Penalty Retry: %s",
		(low_penalty_ra ? "ON!!" : "OFF!!"));

	btcoexist->btc_fill_h2c(btcoexist, 0x69, 6, h2c_parameter);
}

static void halbtc8723b1ant_low_penalty_ra(struct btc_coexist *btcoexist,
					   bool force_exec, bool low_penalty_ra)
{
	coex_dm->cur_low_penalty_ra = low_penalty_ra;

	if (!force_exec) {
		if (coex_dm->pre_low_penalty_ra == coex_dm->cur_low_penalty_ra)
			return;
	}
	btc8723b1ant_set_sw_pen_tx_rate_adapt(btcoexist,
					      coex_dm->cur_low_penalty_ra);

	coex_dm->pre_low_penalty_ra = coex_dm->cur_low_penalty_ra;
}

static void halbtc8723b1ant_set_coex_table(struct btc_coexist *btcoexist,
					   u32 val0x6c0, u32 val0x6c4,
					   u32 val0x6c8, u8 val0x6cc)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], set coex table, set 0x6c0 = 0x%x\n", val0x6c0);
	btcoexist->btc_write_4byte(btcoexist, 0x6c0, val0x6c0);

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], set coex table, set 0x6c4 = 0x%x\n", val0x6c4);
	btcoexist->btc_write_4byte(btcoexist, 0x6c4, val0x6c4);

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], set coex table, set 0x6c8 = 0x%x\n", val0x6c8);
	btcoexist->btc_write_4byte(btcoexist, 0x6c8, val0x6c8);

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], set coex table, set 0x6cc = 0x%x\n", val0x6cc);
	btcoexist->btc_write_1byte(btcoexist, 0x6cc, val0x6cc);
}

static void halbtc8723b1ant_coex_table(struct btc_coexist *btcoexist,
				       bool force_exec, u32 val0x6c0,
				       u32 val0x6c4, u32 val0x6c8,
				       u8 val0x6cc)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], %s write Coex Table 0x6c0 = 0x%x, 0x6c4 = 0x%x, 0x6cc = 0x%x\n",
		 (force_exec ? "force to" : ""),
		 val0x6c0, val0x6c4, val0x6cc);
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
	halbtc8723b1ant_set_coex_table(btcoexist, val0x6c0, val0x6c4,
				       val0x6c8, val0x6cc);

	coex_dm->pre_val0x6c0 = coex_dm->cur_val0x6c0;
	coex_dm->pre_val0x6c4 = coex_dm->cur_val0x6c4;
	coex_dm->pre_val0x6c8 = coex_dm->cur_val0x6c8;
	coex_dm->pre_val0x6cc = coex_dm->cur_val0x6cc;
}

static void halbtc8723b1ant_coex_table_with_type(struct btc_coexist *btcoexist,
						 bool force_exec, u8 type)
{
	coex_sta->coex_table_type = type;

	switch (type) {
	case 0:
		halbtc8723b1ant_coex_table(btcoexist, force_exec, 0x55555555,
					   0x55555555, 0xffffff, 0x3);
		break;
	case 1:
		halbtc8723b1ant_coex_table(btcoexist, force_exec, 0x55555555,
					   0x5a5a5a5a, 0xffffff, 0x3);
		break;
	case 2:
		halbtc8723b1ant_coex_table(btcoexist, force_exec, 0x5a5a5a5a,
					   0x5a5a5a5a, 0xffffff, 0x3);
		break;
	case 3:
		halbtc8723b1ant_coex_table(btcoexist, force_exec, 0x55555555,
					   0x5a5a5a5a, 0xffffff, 0x3);
		break;
	case 4:
		if ((coex_sta->cck_ever_lock) && (coex_sta->scan_ap_num <= 5))
			halbtc8723b1ant_coex_table(btcoexist, force_exec,
						   0x55555555, 0xaaaa5a5a,
						   0xffffff, 0x3);
		else
			halbtc8723b1ant_coex_table(btcoexist, force_exec,
						   0x55555555, 0x5a5a5a5a,
						   0xffffff, 0x3);
		break;
	case 5:
		halbtc8723b1ant_coex_table(btcoexist, force_exec, 0x5a5a5a5a,
					   0x5aaa5a5a, 0xffffff, 0x3);
		break;
	case 6:
		halbtc8723b1ant_coex_table(btcoexist, force_exec, 0x55555555,
					   0xaaaaaaaa, 0xffffff, 0x3);
		break;
	case 7:
		halbtc8723b1ant_coex_table(btcoexist, force_exec, 0xaaaaaaaa,
					   0xaaaaaaaa, 0xffffff, 0x3);
		break;
	case 8:
		halbtc8723b1ant_coex_table(btcoexist, force_exec, 0x55dd55dd,
					   0x5ada5ada, 0xffffff, 0x3);
		break;
	case 9:
		halbtc8723b1ant_coex_table(btcoexist, force_exec, 0x55dd55dd,
					   0x5ada5ada, 0xffffff, 0x3);
		break;
	case 10:
		halbtc8723b1ant_coex_table(btcoexist, force_exec, 0x55dd55dd,
					   0x5ada5ada, 0xffffff, 0x3);
		break;
	case 11:
		halbtc8723b1ant_coex_table(btcoexist, force_exec, 0x55dd55dd,
					   0x5ada5ada, 0xffffff, 0x3);
		break;
	case 12:
		halbtc8723b1ant_coex_table(btcoexist, force_exec, 0x55dd55dd,
					   0x5ada5ada, 0xffffff, 0x3);
		break;
	case 13:
		halbtc8723b1ant_coex_table(btcoexist, force_exec, 0x5fff5fff,
					   0xaaaaaaaa, 0xffffff, 0x3);
		break;
	case 14:
		halbtc8723b1ant_coex_table(btcoexist, force_exec, 0x5fff5fff,
					   0x5ada5ada, 0xffffff, 0x3);
		break;
	case 15:
		halbtc8723b1ant_coex_table(btcoexist, force_exec, 0x55dd55dd,
					   0xaaaaaaaa, 0xffffff, 0x3);
		break;
	default:
		break;
	}
}

static void
halbtc8723b1ant_set_fw_ignore_wlan_act(struct btc_coexist *btcoexist,
				       bool enable)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 h2c_parameter[1] = {0};

	if (enable)
		h2c_parameter[0] |= BIT0;	/* function enable */

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], set FW for BT Ignore Wlan_Act, FW write 0x63 = 0x%x\n",
		h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x63, 1, h2c_parameter);
}

static void halbtc8723b1ant_ignore_wlan_act(struct btc_coexist *btcoexist,
					    bool force_exec, bool enable)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], %s turn Ignore WlanAct %s\n",
		(force_exec ? "force to" : ""), (enable ? "ON" : "OFF"));
	coex_dm->cur_ignore_wlan_act = enable;

	if (!force_exec) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], bPreIgnoreWlanAct = %d, bCurIgnoreWlanAct = %d!!\n",
			coex_dm->pre_ignore_wlan_act,
			 coex_dm->cur_ignore_wlan_act);

		if (coex_dm->pre_ignore_wlan_act ==
		    coex_dm->cur_ignore_wlan_act)
			return;
	}
	halbtc8723b1ant_set_fw_ignore_wlan_act(btcoexist, enable);

	coex_dm->pre_ignore_wlan_act = coex_dm->cur_ignore_wlan_act;
}

static void halbtc8723b1ant_set_fw_ps_tdma(struct btc_coexist *btcoexist,
					   u8 byte1, u8 byte2, u8 byte3,
					   u8 byte4, u8 byte5)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 h2c_parameter[5] = {0};
	u8 real_byte1 = byte1, real_byte5 = byte5;
	bool ap_enable = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE,
			   &ap_enable);

	if (ap_enable) {
		if ((byte1 & BIT4) && !(byte1 & BIT5)) {
			rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
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

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], PS-TDMA H2C cmd =0x%x%08x\n",
		h2c_parameter[0],
		h2c_parameter[1] << 24 |
		h2c_parameter[2] << 16 |
		h2c_parameter[3] << 8 |
		h2c_parameter[4]);

	btcoexist->btc_fill_h2c(btcoexist, 0x60, 5, h2c_parameter);
}

static void halbtc8723b1ant_set_lps_rpwm(struct btc_coexist *btcoexist,
					 u8 lps_val, u8 rpwm_val)
{
	u8 lps = lps_val;
	u8 rpwm = rpwm_val;

	btcoexist->btc_set(btcoexist, BTC_SET_U1_LPS_VAL, &lps);
	btcoexist->btc_set(btcoexist, BTC_SET_U1_RPWM_VAL, &rpwm);
}

static void halbtc8723b1ant_lps_rpwm(struct btc_coexist *btcoexist,
				     bool force_exec,
				     u8 lps_val, u8 rpwm_val)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], %s set lps/rpwm = 0x%x/0x%x\n",
		(force_exec ? "force to" : ""), lps_val, rpwm_val);
	coex_dm->cur_lps = lps_val;
	coex_dm->cur_rpwm = rpwm_val;

	if (!force_exec) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], LPS-RxBeaconMode = 0x%x , LPS-RPWM = 0x%x!!\n",
			coex_dm->cur_lps, coex_dm->cur_rpwm);

		if ((coex_dm->pre_lps == coex_dm->cur_lps) &&
		    (coex_dm->pre_rpwm == coex_dm->cur_rpwm)) {
			rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				"[BTCoex], LPS-RPWM_Last = 0x%x , LPS-RPWM_Now = 0x%x!!\n",
				coex_dm->pre_rpwm, coex_dm->cur_rpwm);

			return;
		}
	}
	halbtc8723b1ant_set_lps_rpwm(btcoexist, lps_val, rpwm_val);

	coex_dm->pre_lps = coex_dm->cur_lps;
	coex_dm->pre_rpwm = coex_dm->cur_rpwm;
}

static void halbtc8723b1ant_sw_mechanism(struct btc_coexist *btcoexist,
					 bool low_penalty_ra)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], SM[LpRA] = %d\n", low_penalty_ra);

	halbtc8723b1ant_low_penalty_ra(btcoexist, NORMAL_EXEC, low_penalty_ra);
}

static void halbtc8723b1ant_set_ant_path(struct btc_coexist *btcoexist,
					 u8 ant_pos_type, bool force_exec,
					 bool init_hw_cfg, bool wifi_off)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct btc_board_info *board_info = &btcoexist->board_info;
	u32 fw_ver = 0, u32tmp = 0, cnt_bt_cal_chk = 0;
	bool pg_ext_switch = false;
	bool use_ext_switch = false;
	bool is_in_mp_mode = false;
	u8 h2c_parameter[2] = {0}, u8tmp = 0;

	coex_dm->cur_ant_pos_type = ant_pos_type;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_EXT_SWITCH, &pg_ext_switch);
	/* [31:16] = fw ver, [15:0] = fw sub ver */
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);

	if ((fw_ver < 0xc0000) || pg_ext_switch)
		use_ext_switch = true;

	if (init_hw_cfg) {
		/* WiFi TRx Mask on */
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff,
					  0x780);
		/* remove due to interrupt is disabled that polling c2h will
		 * fail and delay 100ms.
		 */

		if (fw_ver >= 0x180000) {
			/* Use H2C to set GNT_BT to HIGH */
			h2c_parameter[0] = 1;
			btcoexist->btc_fill_h2c(btcoexist, 0x6E, 1,
						h2c_parameter);
		} else {
			/* set grant_bt to high */
			btcoexist->btc_write_1byte(btcoexist, 0x765, 0x18);
		}
		/* set wlan_act control by PTA */
		btcoexist->btc_write_1byte(btcoexist, 0x76e, 0x4);

		/* BT select s0/s1 is controlled by BT */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x20, 0x0);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x39, 0x8, 0x1);
		btcoexist->btc_write_1byte(btcoexist, 0x974, 0xff);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x944, 0x3, 0x3);
		btcoexist->btc_write_1byte(btcoexist, 0x930, 0x77);
	} else if (wifi_off) {
		if (fw_ver >= 0x180000) {
			/* Use H2C to set GNT_BT to HIGH */
			h2c_parameter[0] = 1;
			btcoexist->btc_fill_h2c(btcoexist, 0x6E, 1,
						h2c_parameter);
		} else {
			/* set grant_bt to high */
			btcoexist->btc_write_1byte(btcoexist, 0x765, 0x18);
		}
		/* set wlan_act to always low */
		btcoexist->btc_write_1byte(btcoexist, 0x76e, 0x4);

		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_IS_IN_MP_MODE,
				   &is_in_mp_mode);
		if (!is_in_mp_mode)
			/* BT select s0/s1 is controlled by BT */
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67,
							   0x20, 0x0);
		else
			/* BT select s0/s1 is controlled by WiFi */
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67,
							   0x20, 0x1);

		/* 0x4c[24:23]=00, Set Antenna control by BT_RFE_CTRL
		 * BT Vendor 0xac=0xf002
		 */
		u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x4c);
		u32tmp &= ~BIT23;
		u32tmp &= ~BIT24;
		btcoexist->btc_write_4byte(btcoexist, 0x4c, u32tmp);
	} else {
		/* Use H2C to set GNT_BT to LOW */
		if (fw_ver >= 0x180000) {
			if (btcoexist->btc_read_1byte(btcoexist, 0x765) != 0) {
				h2c_parameter[0] = 0;
				btcoexist->btc_fill_h2c(btcoexist, 0x6E, 1,
							h2c_parameter);
			}
		} else {
			/* BT calibration check */
			while (cnt_bt_cal_chk <= 20) {
				u8tmp = btcoexist->btc_read_1byte(btcoexist,
								  0x49d);
				cnt_bt_cal_chk++;
				if (u8tmp & BIT(0)) {
					rtl_dbg(rtlpriv, COMP_BT_COEXIST,
						DBG_LOUD,
						"[BTCoex], ########### BT is calibrating (wait cnt=%d) ###########\n",
						cnt_bt_cal_chk);
					mdelay(50);
				} else {
					rtl_dbg(rtlpriv, COMP_BT_COEXIST,
						DBG_LOUD,
						"[BTCoex], ********** BT is NOT calibrating (wait cnt=%d)**********\n",
						cnt_bt_cal_chk);
					break;
				}
			}

			/* set grant_bt to PTA */
			btcoexist->btc_write_1byte(btcoexist, 0x765, 0x0);
		}

		if (btcoexist->btc_read_1byte(btcoexist, 0x76e) != 0xc) {
			/* set wlan_act control by PTA */
			btcoexist->btc_write_1byte(btcoexist, 0x76e, 0xc);
		}

		btcoexist->btc_write_1byte_bitmask(
			btcoexist, 0x67, 0x20,
			0x1); /* BT select s0/s1 is controlled by WiFi */
	}

	if (use_ext_switch) {
		if (init_hw_cfg) {
			/* 0x4c[23] = 0, 0x4c[24] = 1
			 * Antenna control by WL/BT
			 */
			u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x4c);
			u32tmp &= ~BIT23;
			u32tmp |= BIT24;
			btcoexist->btc_write_4byte(btcoexist, 0x4c, u32tmp);

			/* fixed internal switch S1->WiFi, S0->BT */
			btcoexist->btc_write_4byte(btcoexist, 0x948, 0x0);

			if (board_info->btdm_ant_pos ==
			    BTC_ANTENNA_AT_MAIN_PORT) {
				/* tell firmware "no antenna inverse" */
				h2c_parameter[0] = 0;
				/* ext switch type */
				h2c_parameter[1] = 1;
				btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
							h2c_parameter);
			} else {
				/* tell firmware "antenna inverse" */
				h2c_parameter[0] = 1;
				/* ext switch type */
				h2c_parameter[1] = 1;
				btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
							h2c_parameter);
			}
		}

		if (force_exec ||
		    (coex_dm->cur_ant_pos_type != coex_dm->pre_ant_pos_type)) {
			/* ext switch setting */
			switch (ant_pos_type) {
			case BTC_ANT_PATH_WIFI:
				if (board_info->btdm_ant_pos ==
				    BTC_ANTENNA_AT_MAIN_PORT)
					btcoexist->btc_write_1byte_bitmask(
						btcoexist, 0x92c, 0x3, 0x1);
				else
					btcoexist->btc_write_1byte_bitmask(
						btcoexist, 0x92c, 0x3, 0x2);
				break;
			case BTC_ANT_PATH_BT:
				if (board_info->btdm_ant_pos ==
				    BTC_ANTENNA_AT_MAIN_PORT)
					btcoexist->btc_write_1byte_bitmask(
						btcoexist, 0x92c, 0x3, 0x2);
				else
					btcoexist->btc_write_1byte_bitmask(
						btcoexist, 0x92c, 0x3, 0x1);
				break;
			default:
			case BTC_ANT_PATH_PTA:
				if (board_info->btdm_ant_pos ==
				    BTC_ANTENNA_AT_MAIN_PORT)
					btcoexist->btc_write_1byte_bitmask(
						btcoexist, 0x92c, 0x3, 0x1);
				else
					btcoexist->btc_write_1byte_bitmask(
						btcoexist, 0x92c, 0x3, 0x2);
				break;
			}
		}
	} else {
		if (init_hw_cfg) {
			/* 0x4c[23] = 1, 0x4c[24] = 0,
			 * Antenna control by 0x64
			 */
			u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x4c);
			u32tmp |= BIT23;
			u32tmp &= ~BIT24;
			btcoexist->btc_write_4byte(btcoexist, 0x4c, u32tmp);

			/* Fix Ext switch Main->S1, Aux->S0 */
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0x64, 0x1,
							   0x0);

			if (board_info->btdm_ant_pos ==
			    BTC_ANTENNA_AT_MAIN_PORT) {
				/* tell firmware "no antenna inverse" */
				h2c_parameter[0] = 0;
				/* internal switch type */
				h2c_parameter[1] = 0;
				btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
							h2c_parameter);
			} else {
				/* tell firmware "antenna inverse" */
				h2c_parameter[0] = 1;
				/* internal switch type */
				h2c_parameter[1] = 0;
				btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
							h2c_parameter);
			}
		}

		if (force_exec ||
		    (coex_dm->cur_ant_pos_type != coex_dm->pre_ant_pos_type)) {
			/* internal switch setting */
			switch (ant_pos_type) {
			case BTC_ANT_PATH_WIFI:
				if (board_info->btdm_ant_pos ==
				    BTC_ANTENNA_AT_MAIN_PORT)
					btcoexist->btc_write_4byte(btcoexist,
							0x948, 0x0);
				else
					btcoexist->btc_write_4byte(btcoexist,
							0x948, 0x280);
				break;
			case BTC_ANT_PATH_BT:
				if (board_info->btdm_ant_pos ==
				    BTC_ANTENNA_AT_MAIN_PORT)
					btcoexist->btc_write_4byte(btcoexist,
							0x948, 0x280);
				else
					btcoexist->btc_write_4byte(btcoexist,
							0x948, 0x0);
				break;
			default:
			case BTC_ANT_PATH_PTA:
				if (board_info->btdm_ant_pos ==
				    BTC_ANTENNA_AT_MAIN_PORT)
					btcoexist->btc_write_4byte(btcoexist,
							0x948, 0x200);
				else
					btcoexist->btc_write_4byte(btcoexist,
							0x948, 0x80);
				break;
			}
		}
	}

	coex_dm->pre_ant_pos_type = coex_dm->cur_ant_pos_type;
}

static void halbtc8723b1ant_ps_tdma(struct btc_coexist *btcoexist,
				    bool force_exec, bool turn_on, u8 type)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool wifi_busy = false;
	u8 rssi_adjust_val = 0;
	u8 ps_tdma_byte0_val = 0x51;
	u8 ps_tdma_byte3_val = 0x10;
	u8 ps_tdma_byte4_val = 0x50;
	s8 wifi_duration_adjust = 0x0;
	static bool pre_wifi_busy;

	coex_dm->cur_ps_tdma_on = turn_on;
	coex_dm->cur_ps_tdma = type;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	if (wifi_busy != pre_wifi_busy) {
		force_exec = true;
		pre_wifi_busy = wifi_busy;
	}

	if (!force_exec) {
		if ((coex_dm->pre_ps_tdma_on == coex_dm->cur_ps_tdma_on) &&
		    (coex_dm->pre_ps_tdma == coex_dm->cur_ps_tdma))
			return;
	}

	if (coex_sta->scan_ap_num <= 5) {
		wifi_duration_adjust = 5;

		if (coex_sta->a2dp_bit_pool >= 35)
			wifi_duration_adjust = -10;
		else if (coex_sta->a2dp_bit_pool >= 45)
			wifi_duration_adjust = -15;
	} else if (coex_sta->scan_ap_num >= 40) {
		wifi_duration_adjust = -15;

		if (coex_sta->a2dp_bit_pool < 35)
			wifi_duration_adjust = -5;
		else if (coex_sta->a2dp_bit_pool < 45)
			wifi_duration_adjust = -10;
	} else if (coex_sta->scan_ap_num >= 20) {
		wifi_duration_adjust = -10;

		if (coex_sta->a2dp_bit_pool >= 45)
			wifi_duration_adjust = -15;
	} else {
		wifi_duration_adjust = 0;

		if (coex_sta->a2dp_bit_pool >= 35)
			wifi_duration_adjust = -10;
		else if (coex_sta->a2dp_bit_pool >= 45)
			wifi_duration_adjust = -15;
	}

	if ((type == 1) || (type == 2) || (type == 9) || (type == 11) ||
	    (type == 101) || (type == 102) || (type == 109) || (type == 111)) {
		if (!coex_sta->force_lps_on) {
			/* Native power save TDMA, only for A2DP-only case
			 * 1/2/9/11 while wifi noisy threshold > 30
			 */

			/* no null-pkt */
			ps_tdma_byte0_val = 0x61;
			/* no tx-pause at BT-slot */
			ps_tdma_byte3_val = 0x11;
			/* 0x778 = d/1 toggle, no dynamic slot */
			ps_tdma_byte4_val = 0x10;
		} else {
			/* null-pkt */
			ps_tdma_byte0_val = 0x51;
			/* tx-pause at BT-slot */
			ps_tdma_byte3_val = 0x10;
			/* 0x778 = d/1 toggle, dynamic slot */
			ps_tdma_byte4_val = 0x50;
		}
	} else if ((type == 3) || (type == 13) || (type == 14) ||
		   (type == 103) || (type == 113) || (type == 114)) {
		/* null-pkt */
		ps_tdma_byte0_val = 0x51;
		/* tx-pause at BT-slot */
		ps_tdma_byte3_val = 0x10;
		/* 0x778 = d/1 toggle, no dynamic slot */
		ps_tdma_byte4_val = 0x10;
	} else { /* native power save case */
		/* no null-pkt */
		ps_tdma_byte0_val = 0x61;
		/* no tx-pause at BT-slot */
		ps_tdma_byte3_val = 0x11;
		/* 0x778 = d/1 toggle, no dynamic slot */
		ps_tdma_byte4_val = 0x11;
		/* psTdmaByte4Va is not define for 0x778 = d/1, 1/1 case */
	}

	/* if (bt_link_info->slave_role) */
	if ((bt_link_info->slave_role) && (bt_link_info->a2dp_exist))
		/* 0x778 = 0x1 at wifi slot (no blocking BT Low-Pri pkts) */
		ps_tdma_byte4_val = ps_tdma_byte4_val | 0x1;

	if (type > 100) {
		/* set antenna control by SW	 */
		ps_tdma_byte0_val = ps_tdma_byte0_val | 0x82;
		/* set antenna no toggle, control by antenna diversity */
		ps_tdma_byte3_val = ps_tdma_byte3_val | 0x60;
	}

	if (turn_on) {
		switch (type) {
		default:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x1a,
						      0x1a, 0x0,
						      ps_tdma_byte4_val);
			break;
		case 1:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val,
				0x3a + wifi_duration_adjust, 0x03,
				ps_tdma_byte3_val, ps_tdma_byte4_val);

			rssi_adjust_val = 11;
			break;
		case 2:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val,
				0x2d + wifi_duration_adjust, 0x03,
				ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 3:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val, 0x30, 0x03,
				ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 4:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x93, 0x15,
						      0x3, 0x14, 0x0);
			break;
		case 5:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val, 0x1f, 0x3,
				ps_tdma_byte3_val, 0x11);
			break;
		case 6:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val, 0x20, 0x3,
				ps_tdma_byte3_val, 0x11);
			break;
		case 7:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x13, 0xc,
						       0x5, 0x0, 0x0);
			break;
		case 8:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x93, 0x25,
						      0x3, 0x10, 0x0);
			break;
		case 9:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val, 0x21, 0x3,
				ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 10:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x13, 0xa,
						       0xa, 0x0, 0x40);
			break;
		case 11:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val, 0x21, 0x03,
				ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 12:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x0a,
						      0x0a, 0x0, 0x50);
			break;
		case 13:
			if (coex_sta->scan_ap_num <= 3)
				halbtc8723b1ant_set_fw_ps_tdma(
					btcoexist, ps_tdma_byte0_val, 0x40, 0x3,
					ps_tdma_byte3_val, ps_tdma_byte4_val);
			else
				halbtc8723b1ant_set_fw_ps_tdma(
					btcoexist, ps_tdma_byte0_val, 0x21, 0x3,
					ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 14:
			if (coex_sta->scan_ap_num <= 3)
				halbtc8723b1ant_set_fw_ps_tdma(
					btcoexist, 0x51, 0x30, 0x3, 0x10, 0x50);
			else
				halbtc8723b1ant_set_fw_ps_tdma(
					btcoexist, ps_tdma_byte0_val, 0x21, 0x3,
					ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 15:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x13, 0xa,
						       0x3, 0x8, 0x0);
			break;
		case 16:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x93, 0x15,
						      0x3, 0x10, 0x0);
			break;
		case 18:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x93, 0x25,
						      0x3, 0x10, 0x0);
			break;
		case 20:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val, 0x3f, 0x03,
				ps_tdma_byte3_val, 0x10);
			break;
		case 21:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x61, 0x25,
						      0x03, 0x11, 0x11);
			break;
		case 22:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val, 0x25, 0x03,
				ps_tdma_byte3_val, 0x10);
			break;
		case 23:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x25,
						      0x3, 0x31, 0x18);
			break;
		case 24:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x15,
						      0x3, 0x31, 0x18);
			break;
		case 25:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0xe3, 0xa,
						       0x3, 0x31, 0x18);
			break;
		case 26:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0xe3, 0xa,
						       0x3, 0x31, 0x18);
			break;
		case 27:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x25,
						      0x3, 0x31, 0x98);
			break;
		case 28:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x69, 0x25,
						      0x3, 0x31, 0x0);
			break;
		case 29:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0xab, 0x1a,
						      0x1a, 0x1, 0x10);
			break;
		case 30:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x30,
						       0x3, 0x10, 0x10);
			break;
		case 31:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0xd3, 0x1a,
						      0x1a, 0, 0x58);
			break;
		case 32:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val, 0x35, 0x3,
				ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 33:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val, 0x35, 0x3,
				ps_tdma_byte3_val, 0x10);
			break;
		case 34:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x53, 0x1a,
						      0x1a, 0x0, 0x10);
			break;
		case 35:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x63, 0x1a,
						      0x1a, 0x0, 0x10);
			break;
		case 36:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0xd3, 0x12,
						      0x3, 0x14, 0x50);
			break;
		case 40:
			/* SoftAP only with no sta associated,BT disable ,TDMA
			 * mode for power saving
			 *
			 * here softap mode screen off will cost 70-80mA for
			 * phone
			 */
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x23, 0x18,
						      0x00, 0x10, 0x24);
			break;

		case 101:
			/* for 1-Ant translate to 2-Ant	 */
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val,
				0x3a + wifi_duration_adjust, 0x03,
				ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 102:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val,
				0x2d + wifi_duration_adjust, 0x03,
				ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 103:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val, 0x3a, 0x03,
				ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 105:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val, 0x15, 0x3,
				ps_tdma_byte3_val, 0x11);
			break;
		case 106:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val, 0x20, 0x3,
				ps_tdma_byte3_val, 0x11);
			break;
		case 109:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val, 0x21, 0x3,
				ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 111:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val, 0x21, 0x03,
				ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 113:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val, 0x21, 0x3,
				ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 114:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val, 0x21, 0x3,
				ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 120:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val, 0x3f, 0x03,
				ps_tdma_byte3_val, 0x10);
			break;
		case 122:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val, 0x25, 0x03,
				ps_tdma_byte3_val, 0x10);
			break;
		case 132:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val, 0x25, 0x03,
				ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 133:
			halbtc8723b1ant_set_fw_ps_tdma(
				btcoexist, ps_tdma_byte0_val, 0x25, 0x03,
				ps_tdma_byte3_val, 0x11);
			break;
		}
	} else {
		/* disable PS tdma */
		switch (type) {
		case 8: /* PTA Control */
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x8, 0x0,
						       0x0, 0x0, 0x0);
			halbtc8723b1ant_set_ant_path(btcoexist,
						     BTC_ANT_PATH_PTA,
						     FORCE_EXEC,
						     false, false);
			break;
		case 0:
		default:
			/* Software control, Antenna at BT side */
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x0, 0x0,
						       0x0, 0x0, 0x0);
			break;
		case 1: /* 2-Ant, 0x778=3, antenna control by ant diversity */
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x0, 0x0, 0x0,
						       0x48, 0x0);
			break;
		}
	}
	rssi_adjust_val = 0;
	btcoexist->btc_set(btcoexist,
			   BTC_SET_U1_RSSI_ADJ_VAL_FOR_1ANT_COEX_TYPE,
			   &rssi_adjust_val);

	/* update pre state */
	coex_dm->pre_ps_tdma_on = coex_dm->cur_ps_tdma_on;
	coex_dm->pre_ps_tdma = coex_dm->cur_ps_tdma;
}

static
void btc8723b1ant_tdma_dur_adj_for_acl(struct btc_coexist *btcoexist,
				       u8 wifi_status)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	static s32 up, dn, m, n, wait_count;
	/*  0: no change, +1: increase WiFi duration,
	 * -1: decrease WiFi duration
	 */
	s32 result;
	u8 retry_count = 0;

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], TdmaDurationAdjustForAcl()\n");

	if ((wifi_status ==
	     BT_8723B_1ANT_WIFI_STATUS_NON_CONNECTED_ASSO_AUTH_SCAN) ||
	    (wifi_status == BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SCAN) ||
	    (wifi_status == BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SPECIAL_PKT)) {
		if (coex_dm->cur_ps_tdma != 1 && coex_dm->cur_ps_tdma != 2 &&
		    coex_dm->cur_ps_tdma != 3 && coex_dm->cur_ps_tdma != 9) {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 9);
			coex_dm->ps_tdma_du_adj_type = 9;

			up = 0;
			dn = 0;
			m = 1;
			n = 3;
			result = 0;
			wait_count = 0;
		}
		return;
	}

	if (!coex_dm->auto_tdma_adjust) {
		coex_dm->auto_tdma_adjust = true;
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], first run TdmaDurationAdjust()!!\n");

		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 2);
		coex_dm->ps_tdma_du_adj_type = 2;

		up = 0;
		dn = 0;
		m = 1;
		n = 3;
		result = 0;
		wait_count = 0;
	} else {
		/* acquire the BT TRx retry count from BT_Info byte2 */
		retry_count = coex_sta->bt_retry_cnt;

		if ((coex_sta->low_priority_tx) > 1050 ||
		    (coex_sta->low_priority_rx) > 1250)
			retry_count++;

		result = 0;
		wait_count++;
		/* no retry in the last 2-second duration */
		if (retry_count == 0) {
			up++;
			dn--;

			if (dn <= 0)
				dn = 0;

			if (up >= n) {
				/* if retry count during continuous n*2 seconds
				 * is 0, enlarge WiFi duration
				 */
				wait_count = 0;
				n = 3;
				up = 0;
				dn = 0;
				result = 1;
				rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					"[BTCoex], Increase wifi duration!!\n");
			}
		} else if (retry_count <= 3) {
			/* <=3 retry in the last 2-second duration */
			up--;
			dn++;

			if (up <= 0)
				up = 0;

			if (dn == 2) {
				/* if continuous 2 retry count(every 2 seconds)
				 * >0 and < 3, reduce WiFi duration
				 */
				if (wait_count <= 2)
					/* avoid loop between the two levels */
					m++;
				else
					m = 1;

				if (m >= 20)
					/* maximum of m = 20 ' will recheck if
					 * need to adjust wifi duration in
					 * maximum time interval 120 seconds
					 */
					m = 20;

				n = 3 * m;
				up = 0;
				dn = 0;
				wait_count = 0;
				result = -1;
				rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					"[BTCoex], Decrease wifi duration for retryCounter<3!!\n");
			}
		} else {
			/* retry count > 3, once retry count > 3, to reduce
			 * WiFi duration
			 */
			if (wait_count == 1)
				/* to avoid loop between the two levels */
				m++;
			else
				m = 1;

			if (m >= 20)
				/* maximum of m = 20 ' will recheck if need to
				 * adjust wifi duration in maximum time interval
				 * 120 seconds
				 */
				m = 20;

			n = 3 * m;
			up = 0;
			dn = 0;
			wait_count = 0;
			result = -1;
			rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				"[BTCoex], Decrease wifi duration for retryCounter>3!!\n");
		}

		if (result == -1) {
			if (coex_dm->cur_ps_tdma == 1) {
				halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 2);
				coex_dm->ps_tdma_du_adj_type = 2;
			} else if (coex_dm->cur_ps_tdma == 2) {
				halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 9);
				coex_dm->ps_tdma_du_adj_type = 9;
			} else if (coex_dm->cur_ps_tdma == 9) {
				halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 11);
				coex_dm->ps_tdma_du_adj_type = 11;
			}
		} else if (result == 1) {
			if (coex_dm->cur_ps_tdma == 11) {
				halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 9);
				coex_dm->ps_tdma_du_adj_type = 9;
			} else if (coex_dm->cur_ps_tdma == 9) {
				halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 2);
				coex_dm->ps_tdma_du_adj_type = 2;
			} else if (coex_dm->cur_ps_tdma == 2) {
				halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 1);
				coex_dm->ps_tdma_du_adj_type = 1;
			}
		}

		if (coex_dm->cur_ps_tdma != 1 && coex_dm->cur_ps_tdma != 2 &&
		    coex_dm->cur_ps_tdma != 9 && coex_dm->cur_ps_tdma != 11) {
			/* recover to previous adjust type */
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						coex_dm->ps_tdma_du_adj_type);
		}
	}
}

static void halbtc8723b1ant_ps_tdma_chk_pwr_save(struct btc_coexist *btcoexist,
					  bool new_ps_state)
{
	u8 lps_mode = 0x0;

	btcoexist->btc_get(btcoexist, BTC_GET_U1_LPS_MODE, &lps_mode);

	if (lps_mode) {
		/* already under LPS state */
		if (new_ps_state) {
			/* keep state under LPS, do nothing. */
		} else {
			/* will leave LPS state, turn off psTdma first */
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						false, 0);
		}
	} else {
		/* NO PS state */
		if (new_ps_state) {
			/* will enter LPS state, turn off psTdma first */
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						false, 0);
		} else {
			/* keep state under NO PS state, do nothing. */
		}
	}
}

static void halbtc8723b1ant_power_save_state(struct btc_coexist *btcoexist,
					     u8 ps_type, u8 lps_val,
					     u8 rpwm_val)
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
		halbtc8723b1ant_ps_tdma_chk_pwr_save(btcoexist, true);
		halbtc8723b1ant_lps_rpwm(btcoexist, NORMAL_EXEC, lps_val,
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
		halbtc8723b1ant_ps_tdma_chk_pwr_save(btcoexist, false);
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
		coex_sta->force_lps_on = false;
		break;
	default:
		break;
	}
}

static void halbtc8723b1ant_action_wifi_only(struct btc_coexist *btcoexist)
{
	halbtc8723b1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 0);
	halbtc8723b1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 8);
	halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA,
				     FORCE_EXEC, false, false);
}

/* check if BT is disabled */
static void halbtc8723b1ant_monitor_bt_enable_disable(struct btc_coexist
						      *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	static u32 bt_disable_cnt;
	bool bt_active = true, bt_disabled = false;

	if (coex_sta->high_priority_tx == 0 &&
	    coex_sta->high_priority_rx == 0 && coex_sta->low_priority_tx == 0 &&
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
	} else {
		bt_disable_cnt++;
		if (bt_disable_cnt >= 2)
			bt_disabled = true;
	}
	if (coex_sta->bt_disabled != bt_disabled) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], BT is from %s to %s!!\n",
			(coex_sta->bt_disabled ? "disabled" : "enabled"),
			(bt_disabled ? "disabled" : "enabled"));

		coex_sta->bt_disabled = bt_disabled;
		btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_DISABLE,
				   &bt_disabled);
		if (bt_disabled) {
			halbtc8723b1ant_action_wifi_only(btcoexist);
			btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS,
					   NULL);
			btcoexist->btc_set(btcoexist, BTC_SET_ACT_NORMAL_LPS,
					   NULL);
		}
	}
}

/*****************************************************
 *
 *	Non-Software Coex Mechanism start
 *
 *****************************************************/

static void halbtc8723b1ant_action_bt_whck_test(struct btc_coexist *btcoexist)
{
	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0,
					 0x0);

	halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
	halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA, NORMAL_EXEC,
				     false, false);
	halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
}

static void halbtc8723b1ant_action_wifi_multiport(struct btc_coexist *btcoexist)
{
	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					 0x0, 0x0);

	halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
	halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA, NORMAL_EXEC,
				     false, false);
	halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
}

static void halbtc8723b1ant_action_hs(struct btc_coexist *btcoexist)
{
	halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 5);
	halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
}

static void halbtc8723b1ant_action_bt_inquiry(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool wifi_connected = false, ap_enable = false;
	bool wifi_busy = false, bt_busy = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE,
			   &ap_enable);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bt_busy);

	if (coex_sta->bt_abnormal_scan) {
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 33);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
	} else if (!wifi_connected && !coex_sta->wifi_is_high_pri_task) {
		halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA,
					     NORMAL_EXEC, false, false);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
	} else if (bt_link_info->sco_exist || bt_link_info->hid_exist ||
		   bt_link_info->a2dp_exist) {
		/* SCO/HID/A2DP busy */
		halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
		if (coex_sta->c2h_bt_remote_name_req)
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						33);
		else
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						32);

		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
	} else if (bt_link_info->pan_exist || wifi_busy) {
		halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
		if (coex_sta->c2h_bt_remote_name_req)
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						33);
		else
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						32);

		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
	} else {
		halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA,
					     NORMAL_EXEC, false, false);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
	}
}

static void btc8723b1ant_act_bt_sco_hid_only_busy(struct btc_coexist *btcoexist,
						  u8 wifi_status)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool wifi_connected = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);

	/* tdma and coex table */
	if (bt_link_info->sco_exist) {
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 5);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 5);
	} else {
		/* HID */
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 6);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 5);
	}
}

static void halbtc8723b1ant_action_wifi_connected_bt_acl_busy(
					struct btc_coexist *btcoexist,
					u8 wifi_status)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	if ((coex_sta->low_priority_rx >= 950) && (!coex_sta->under_ips))
		bt_link_info->slave_role = true;
	else
		bt_link_info->slave_role = false;

	if (bt_link_info->hid_only) { /* HID */
		btc8723b1ant_act_bt_sco_hid_only_busy(btcoexist, wifi_status);
		coex_dm->auto_tdma_adjust = false;
		return;
	} else if (bt_link_info->a2dp_only) { /* A2DP */
		if (wifi_status == BT_8723B_1ANT_WIFI_STATUS_CONNECTED_IDLE) {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 32);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 4);
			coex_dm->auto_tdma_adjust = false;
		} else {
			btc8723b1ant_tdma_dur_adj_for_acl(btcoexist,
							  wifi_status);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 1);
			coex_dm->auto_tdma_adjust = true;
		}
	} else if (((bt_link_info->a2dp_exist) && (bt_link_info->pan_exist)) ||
		   (bt_link_info->hid_exist && bt_link_info->a2dp_exist &&
		    bt_link_info->pan_exist)) {
		/* A2DP + PAN(OPP,FTP), HID + A2DP + PAN(OPP,FTP) */
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 13);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
		coex_dm->auto_tdma_adjust = false;
	} else if (bt_link_info->hid_exist && bt_link_info->a2dp_exist) {
		/* HID + A2DP */
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,	true, 14);
		coex_dm->auto_tdma_adjust = false;

		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
	} else if (bt_link_info->pan_only ||
			(bt_link_info->hid_exist && bt_link_info->pan_exist)) {
		/* PAN(OPP,FTP), HID + PAN(OPP,FTP) */
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 3);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
		coex_dm->auto_tdma_adjust = false;
	} else {
		/* BT no-profile busy (0x9) */
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 33);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
		coex_dm->auto_tdma_adjust = false;
	}
}

static void btc8723b1ant_action_wifi_not_conn(struct btc_coexist *btcoexist)
{
	/* power save state */
	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					 0x0, 0x0);

	/* tdma and coex table */
	halbtc8723b1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 8);
	halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA, NORMAL_EXEC,
				     false, false);
	halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
}

static void
btc8723b1ant_action_wifi_not_conn_scan(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					 0x0, 0x0);

	/* tdma and coex table */
	if (coex_dm->bt_status == BT_8723B_1ANT_BT_STATUS_ACL_BUSY) {
		if (bt_link_info->a2dp_exist) {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 32);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 4);
		} else if (bt_link_info->pan_exist) {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 22);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 4);
		} else {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 20);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 1);
		}
	} else if (coex_dm->bt_status == BT_8723B_1ANT_BT_STATUS_SCO_BUSY ||
		   coex_dm->bt_status == BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY){
		btc8723b1ant_act_bt_sco_hid_only_busy(btcoexist,
				BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SCAN);
	} else {
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA,
					     NORMAL_EXEC, false, false);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
	}
}

static void
btc8723b1ant_act_wifi_not_conn_asso_auth(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					 0x0, 0x0);

	/* tdma and coex table */
	if ((bt_link_info->sco_exist) || (bt_link_info->hid_exist) ||
	    (bt_link_info->a2dp_exist)) {
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 32);
		halbtc8723b1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 4);
	} else if (bt_link_info->pan_exist) {
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 20);
		halbtc8723b1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 4);
	} else {
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA,
					     NORMAL_EXEC, false, false);
		halbtc8723b1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 2);
	}
}

static void btc8723b1ant_action_wifi_conn_scan(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					 0x0, 0x0);

	/* tdma and coex table */
	if (coex_dm->bt_status == BT_8723B_1ANT_BT_STATUS_ACL_BUSY) {
		if (bt_link_info->a2dp_exist) {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 32);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 4);
		} else if (bt_link_info->pan_exist) {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 22);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 4);
		} else {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 20);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 4);
		}
	} else if (coex_dm->bt_status == BT_8723B_1ANT_BT_STATUS_SCO_BUSY ||
		   coex_dm->bt_status == BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY) {
		btc8723b1ant_act_bt_sco_hid_only_busy(btcoexist,
				BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SCAN);
	} else {
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA,
					     NORMAL_EXEC, false, false);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
	}
}

static void halbtc8723b1ant_action_wifi_connected_special_packet(
						struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool wifi_busy = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	/* no special packet process for both WiFi and BT very busy */
	if ((wifi_busy) &&
	    ((bt_link_info->pan_exist) || (coex_sta->num_of_profile >= 2)))
		return;

	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					 0x0, 0x0);

	/* tdma and coex table */
	if ((bt_link_info->sco_exist) || (bt_link_info->hid_exist)) {
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 32);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 5);
	} else if (bt_link_info->a2dp_exist) {
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 32);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
	} else if (bt_link_info->pan_exist) {
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 20);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
	} else {
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA,
					     NORMAL_EXEC, false, false);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
	}
}

static void halbtc8723b1ant_action_wifi_connected(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	bool wifi_busy = false;
	bool scan = false, link = false, roam = false;
	bool under_4way = false, ap_enable = false;

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], CoexForWifiConnect()===>\n");

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS,
			   &under_4way);
	if (under_4way) {
		halbtc8723b1ant_action_wifi_connected_special_packet(btcoexist);
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], CoexForWifiConnect(), return for wifi is under 4way<===\n");
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);

	if (scan || link || roam) {
		if (scan)
			btc8723b1ant_action_wifi_conn_scan(btcoexist);
		else
			halbtc8723b1ant_action_wifi_connected_special_packet(
								     btcoexist);
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], CoexForWifiConnect(), return for wifi is under scan<===\n");
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE,
			   &ap_enable);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	/* power save state */
	if (!ap_enable &&
	    coex_dm->bt_status == BT_8723B_1ANT_BT_STATUS_ACL_BUSY &&
	    !btcoexist->bt_link_info.hid_only) {
		if (btcoexist->bt_link_info.a2dp_only) {
			if (!wifi_busy) {
				halbtc8723b1ant_power_save_state(btcoexist,
							 BTC_PS_WIFI_NATIVE,
							 0x0, 0x0);
			} else { /* busy */
				if (coex_sta->scan_ap_num >=
				    BT_8723B_1ANT_WIFI_NOISY_THRESH)
					/* no force LPS, no PS-TDMA,
					 * use pure TDMA
					 */
					halbtc8723b1ant_power_save_state(
						btcoexist, BTC_PS_WIFI_NATIVE,
						0x0, 0x0);
				else
					halbtc8723b1ant_power_save_state(
						btcoexist, BTC_PS_LPS_ON, 0x50,
						0x4);
			}
		} else if ((!coex_sta->pan_exist) && (!coex_sta->a2dp_exist) &&
			   (!coex_sta->hid_exist))
			halbtc8723b1ant_power_save_state(
				btcoexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		else
			halbtc8723b1ant_power_save_state(btcoexist,
							 BTC_PS_LPS_ON,
							 0x50, 0x4);
	} else {
		halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
	}
	/* tdma and coex table */
	if (!wifi_busy) {
		if (coex_dm->bt_status == BT_8723B_1ANT_BT_STATUS_ACL_BUSY) {
			halbtc8723b1ant_action_wifi_connected_bt_acl_busy(
				btcoexist,
				BT_8723B_1ANT_WIFI_STATUS_CONNECTED_IDLE);
		} else if (coex_dm->bt_status ==
				BT_8723B_1ANT_BT_STATUS_SCO_BUSY ||
			   coex_dm->bt_status ==
				BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY) {
			btc8723b1ant_act_bt_sco_hid_only_busy(btcoexist,
				     BT_8723B_1ANT_WIFI_STATUS_CONNECTED_IDLE);
		} else {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						false, 8);
			halbtc8723b1ant_set_ant_path(btcoexist,
						     BTC_ANT_PATH_PTA,
						     NORMAL_EXEC, false, false);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 2);
		}
	} else {
		if (coex_dm->bt_status == BT_8723B_1ANT_BT_STATUS_ACL_BUSY) {
			halbtc8723b1ant_action_wifi_connected_bt_acl_busy(
				btcoexist,
				BT_8723B_1ANT_WIFI_STATUS_CONNECTED_BUSY);
		} else if (coex_dm->bt_status ==
				BT_8723B_1ANT_BT_STATUS_SCO_BUSY ||
			   coex_dm->bt_status ==
				BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY) {
			btc8723b1ant_act_bt_sco_hid_only_busy(btcoexist,
				    BT_8723B_1ANT_WIFI_STATUS_CONNECTED_BUSY);
		} else {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 32);
			halbtc8723b1ant_set_ant_path(btcoexist,
						     BTC_ANT_PATH_PTA,
						     NORMAL_EXEC, false, false);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 4);
		}
	}
}

static void halbtc8723b1ant_run_coexist_mechanism(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool wifi_connected = false, bt_hs_on = false, wifi_busy = false;
	bool increase_scan_dev_num = false;
	bool bt_ctrl_agg_buf_size = false;
	bool miracast_plus_bt = false;
	u8 agg_buf_size = 5;
	u8 iot_peer = BTC_IOT_PEER_UNKNOWN;
	u32 wifi_link_status = 0;
	u32 num_of_wifi_link = 0;
	u32 wifi_bw;

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], RunCoexistMechanism()===>\n");

	if (btcoexist->manual_control) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], RunCoexistMechanism(), return for Manual CTRL <===\n");
		return;
	}

	if (btcoexist->stop_coex_dm) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], RunCoexistMechanism(), return for Stop Coex DM <===\n");
		return;
	}

	if (coex_sta->under_ips) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], wifi is under IPS !!!\n");
		return;
	}

	if (coex_sta->bt_whck_test) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], wifi is under IPS !!!\n");
		halbtc8723b1ant_action_bt_whck_test(btcoexist);
		return;
	}

	if (coex_dm->bt_status == BT_8723B_1ANT_BT_STATUS_ACL_BUSY ||
	    coex_dm->bt_status == BT_8723B_1ANT_BT_STATUS_SCO_BUSY ||
	    coex_dm->bt_status == BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY)
		increase_scan_dev_num = true;

	btcoexist->btc_set(btcoexist, BTC_SET_BL_INC_SCAN_DEV_NUM,
			   &increase_scan_dev_num);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
			   &wifi_link_status);
	num_of_wifi_link = wifi_link_status >> 16;

	if (num_of_wifi_link >= 2 ||
	    wifi_link_status & WIFI_P2P_GO_CONNECTED) {
		if (bt_link_info->bt_link_exist) {
			halbtc8723b1ant_limited_tx(btcoexist, NORMAL_EXEC, 1, 1,
						   0, 1);
			miracast_plus_bt = true;
		} else {
			halbtc8723b1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0,
						   0, 0);
			miracast_plus_bt = false;
		}
		btcoexist->btc_set(btcoexist, BTC_SET_BL_MIRACAST_PLUS_BT,
				   &miracast_plus_bt);
		halbtc8723b1ant_limited_rx(btcoexist, NORMAL_EXEC, false,
					   bt_ctrl_agg_buf_size, agg_buf_size);

		if ((bt_link_info->a2dp_exist || wifi_busy) &&
		    (coex_sta->c2h_bt_inquiry_page))
			halbtc8723b1ant_action_bt_inquiry(btcoexist);
		else
			halbtc8723b1ant_action_wifi_multiport(btcoexist);

		return;
	}

	miracast_plus_bt = false;
	btcoexist->btc_set(btcoexist, BTC_SET_BL_MIRACAST_PLUS_BT,
			   &miracast_plus_bt);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (bt_link_info->bt_link_exist && wifi_connected) {
		halbtc8723b1ant_limited_tx(btcoexist, NORMAL_EXEC, 1, 1, 0, 1);

		btcoexist->btc_get(btcoexist, BTC_GET_U1_IOT_PEER, &iot_peer);

		if (iot_peer != BTC_IOT_PEER_CISCO &&
		    iot_peer != BTC_IOT_PEER_BROADCOM) {
			bool sco_exist = bt_link_info->sco_exist;

			halbtc8723b1ant_limited_rx(btcoexist,
						   NORMAL_EXEC, sco_exist,
						   false, 0x5);
		} else {
			if (bt_link_info->sco_exist) {
				halbtc8723b1ant_limited_rx(btcoexist,
							   NORMAL_EXEC, true,
							   false, 0x5);
			} else {
				if (wifi_bw == BTC_WIFI_BW_HT40)
					halbtc8723b1ant_limited_rx(
						btcoexist, NORMAL_EXEC, false,
						true, 0x10);
				else
					halbtc8723b1ant_limited_rx(
						btcoexist, NORMAL_EXEC, false,
						true, 0x8);
			}
		}

		halbtc8723b1ant_sw_mechanism(btcoexist, true);
	} else {
		halbtc8723b1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);

		halbtc8723b1ant_limited_rx(btcoexist, NORMAL_EXEC, false, false,
					   0x5);

		halbtc8723b1ant_sw_mechanism(btcoexist, false);
	}
	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);

	if (coex_sta->c2h_bt_inquiry_page) {
		halbtc8723b1ant_action_bt_inquiry(btcoexist);
		return;
	} else if (bt_hs_on) {
		halbtc8723b1ant_action_hs(btcoexist);
		return;
	}

	if (!wifi_connected) {
		bool scan = false, link = false, roam = false;

		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], wifi is non connected-idle !!!\n");

		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);

		if (scan || link || roam) {
			if (scan)
				btc8723b1ant_action_wifi_not_conn_scan(
								     btcoexist);
			else
				btc8723b1ant_act_wifi_not_conn_asso_auth(
								     btcoexist);
		} else {
			btc8723b1ant_action_wifi_not_conn(btcoexist);
		}
	} else { /* wifi LPS/Busy */
		halbtc8723b1ant_action_wifi_connected(btcoexist);
	}
}

/* force coex mechanism to reset */
static void halbtc8723b1ant_init_coex_dm(struct btc_coexist *btcoexist)
{
	/* sw all off */
	halbtc8723b1ant_sw_mechanism(btcoexist, false);

	coex_sta->pop_event_cnt = 0;
}

static void halbtc8723b1ant_init_hw_config(struct btc_coexist *btcoexist,
					   bool backup, bool wifi_only)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u32 u32tmp = 0;
	u8 u8tmpa = 0, u8tmpb = 0;

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], 1Ant Init HW Config!!\n");

	/* 0xf0[15:12] --> Chip Cut information */
	coex_sta->cut_version =
		(btcoexist->btc_read_1byte(btcoexist, 0xf1) & 0xf0) >> 4;
	/* enable TBTT interrupt */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x550, 0x8, 0x1);

	/* 0x790[5:0] = 0x5 */
	btcoexist->btc_write_1byte(btcoexist, 0x790, 0x5);

	/* Enable counter statistics */
	btcoexist->btc_write_1byte(btcoexist, 0x778, 0x1);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x40, 0x20, 0x1);

	halbtc8723b1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 8);

	/* Antenna config */
	if (wifi_only)
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_WIFI,
					     FORCE_EXEC, true, false);
	else
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_BT,
					     FORCE_EXEC, true, false);

	/* PTA parameter */
	halbtc8723b1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 0);

	u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x948);
	u8tmpa = btcoexist->btc_read_1byte(btcoexist, 0x765);
	u8tmpb = btcoexist->btc_read_1byte(btcoexist, 0x67);

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"############# [BTCoex], 0x948=0x%x, 0x765=0x%x, 0x67=0x%x\n",
		u32tmp, u8tmpa, u8tmpb);
}

/**************************************************************
 * extern function start with ex_btc8723b1ant_
 **************************************************************/
void ex_btc8723b1ant_power_on_setting(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct btc_board_info *board_info = &btcoexist->board_info;
	u8 u8tmp = 0x0;
	u16 u16tmp = 0x0;
	u32 value;

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"xxxxxxxxxxxxxxxx Execute 8723b 1-Ant PowerOn Setting xxxxxxxxxxxxxxxx!!\n");

	btcoexist->stop_coex_dm = true;

	btcoexist->btc_write_1byte(btcoexist, 0x67, 0x20);

	/* enable BB, REG_SYS_FUNC_EN such that we can write 0x948 correctly. */
	u16tmp = btcoexist->btc_read_2byte(btcoexist, 0x2);
	btcoexist->btc_write_2byte(btcoexist, 0x2, u16tmp | BIT0 | BIT1);

	/* set GRAN_BT = 1 */
	btcoexist->btc_write_1byte(btcoexist, 0x765, 0x18);
	/* set WLAN_ACT = 0 */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0x4);

	/* S0 or S1 setting and Local register setting(By the setting fw can get
	 * ant number, S0/S1, ... info)
	 *
	 * Local setting bit define
	 *	BIT0: "0" for no antenna inverse; "1" for antenna inverse
	 *	BIT1: "0" for internal switch; "1" for external switch
	 *	BIT2: "0" for one antenna; "1" for two antenna
	 * NOTE: here default all internal switch and 1-antenna ==> BIT1=0 and
	 * BIT2 = 0
	 */
	if (btcoexist->chip_interface == BTC_INTF_USB) {
		/* fixed at S0 for USB interface */
		btcoexist->btc_write_4byte(btcoexist, 0x948, 0x0);

		u8tmp |= 0x1; /* antenna inverse */
		btcoexist->btc_write_local_reg_1byte(btcoexist, 0xfe08, u8tmp);

		board_info->btdm_ant_pos = BTC_ANTENNA_AT_AUX_PORT;
	} else {
		/* for PCIE and SDIO interface, we check efuse 0xc3[6] */
		if (board_info->single_ant_path == 0) {
			/* set to S1 */
			btcoexist->btc_write_4byte(btcoexist, 0x948, 0x280);
			board_info->btdm_ant_pos = BTC_ANTENNA_AT_MAIN_PORT;
			value = 1;
		} else if (board_info->single_ant_path == 1) {
			/* set to S0 */
			btcoexist->btc_write_4byte(btcoexist, 0x948, 0x0);
			u8tmp |= 0x1; /* antenna inverse */
			board_info->btdm_ant_pos = BTC_ANTENNA_AT_AUX_PORT;
			value = 0;
		}

		btcoexist->btc_set(btcoexist, BTC_SET_ACT_ANTPOSREGRISTRY_CTRL,
				   &value);

		if (btcoexist->chip_interface == BTC_INTF_PCI)
			btcoexist->btc_write_local_reg_1byte(btcoexist, 0x384,
							     u8tmp);
		else if (btcoexist->chip_interface == BTC_INTF_SDIO)
			btcoexist->btc_write_local_reg_1byte(btcoexist, 0x60,
							     u8tmp);
	}
}


void ex_btc8723b1ant_init_hwconfig(struct btc_coexist *btcoexist,
				   bool wifi_only)
{
	halbtc8723b1ant_init_hw_config(btcoexist, true, wifi_only);
	btcoexist->stop_coex_dm = false;
}

void ex_btc8723b1ant_init_coex_dm(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], Coex Mechanism Init!!\n");

	btcoexist->stop_coex_dm = false;

	halbtc8723b1ant_init_coex_dm(btcoexist);

	halbtc8723b1ant_query_bt_info(btcoexist);
}

void ex_btc8723b1ant_display_coex_info(struct btc_coexist *btcoexist,
				       struct seq_file *m)
{
	struct btc_board_info *board_info = &btcoexist->board_info;
	struct btc_stack_info *stack_info = &btcoexist->stack_info;
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	u8 u8tmp[4], i, bt_info_ext, pstdmacase = 0;
	u16 u16tmp[4];
	u32 u32tmp[4];
	bool roam = false, scan = false;
	bool link = false, wifi_under_5g = false;
	bool bt_hs_on = false, wifi_busy = false;
	s32 wifi_rssi = 0, bt_hs_rssi = 0;
	u32 wifi_bw, wifi_traffic_dir, fa_ofdm, fa_cck, wifi_link_status;
	u8 wifi_dot11_chnl, wifi_hs_chnl;
	u32 fw_ver = 0, bt_patch_ver = 0;

	seq_puts(m, "\n ============[BT Coexist info]============");

	if (btcoexist->manual_control) {
		seq_puts(m, "\n ============[Under Manual Control]==========");
		seq_puts(m, "\n ==========================================");
	}
	if (btcoexist->stop_coex_dm) {
		seq_puts(m, "\n ============[Coex is STOPPED]============");
		seq_puts(m, "\n ==========================================");
	}

	seq_printf(m, "\n %-35s = %d/ %d/ %d",
		   "Ant PG Num/ Ant Mech/ Ant Pos:",
		   board_info->pg_ant_num, board_info->btdm_ant_num,
		   board_info->btdm_ant_pos);

	seq_printf(m, "\n %-35s = %s / %d",
		   "BT stack/ hci ext ver",
		   ((stack_info->profile_notified) ? "Yes" : "No"),
		   stack_info->hci_version);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER, &bt_patch_ver);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
	seq_printf(m, "\n %-35s = %d_%x/ 0x%x/ 0x%x(%d)",
		   "CoexVer/ FwVer/ PatchVer",
		   glcoex_ver_date_8723b_1ant, glcoex_ver_8723b_1ant,
		   fw_ver, bt_patch_ver, bt_patch_ver);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_DOT11_CHNL,
			   &wifi_dot11_chnl);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_HS_CHNL, &wifi_hs_chnl);
	seq_printf(m, "\n %-35s = %d / %d(%d)",
		   "Dot11 channel / HsChnl(HsMode)",
		   wifi_dot11_chnl, wifi_hs_chnl, bt_hs_on);

	seq_printf(m, "\n %-35s = %3ph ",
		   "H2C Wifi inform bt chnl Info",
		   coex_dm->wifi_chnl_info);

	btcoexist->btc_get(btcoexist, BTC_GET_S4_WIFI_RSSI, &wifi_rssi);
	btcoexist->btc_get(btcoexist, BTC_GET_S4_HS_RSSI, &bt_hs_rssi);
	seq_printf(m, "\n %-35s = %d/ %d",
		   "Wifi rssi/ HS rssi", wifi_rssi, bt_hs_rssi);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);
	seq_printf(m, "\n %-35s = %d/ %d/ %d ",
		   "Wifi link/ roam/ scan", link, roam, scan);

	btcoexist->btc_get(btcoexist , BTC_GET_BL_WIFI_UNDER_5G,
			   &wifi_under_5g);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION,
			   &wifi_traffic_dir);

	seq_printf(m, "\n %-35s = %s / %s/ %s ",
		   "Wifi status", (wifi_under_5g ? "5G" : "2.4G"),
		   ((wifi_bw == BTC_WIFI_BW_LEGACY) ? "Legacy" :
		    ((wifi_bw == BTC_WIFI_BW_HT40) ? "HT40" : "HT20")),
		    ((!wifi_busy) ? "idle" :
		     ((wifi_traffic_dir == BTC_WIFI_TRAFFIC_TX) ?
		     "uplink" : "downlink")));

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
			   &wifi_link_status);
	seq_printf(m, "\n %-35s = %d/ %d/ %d/ %d/ %d",
		   "sta/vwifi/hs/p2pGo/p2pGc",
		   ((wifi_link_status & WIFI_STA_CONNECTED) ? 1 : 0),
		   ((wifi_link_status & WIFI_AP_CONNECTED) ? 1 : 0),
		   ((wifi_link_status & WIFI_HS_CONNECTED) ? 1 : 0),
		   ((wifi_link_status & WIFI_P2P_GO_CONNECTED) ? 1 : 0),
		   ((wifi_link_status & WIFI_P2P_GC_CONNECTED) ? 1 : 0));

	seq_printf(m, "\n %-35s = [%s/ %d/ %d] ",
		   "BT [status/ rssi/ retryCnt]",
		   ((coex_sta->bt_disabled) ? ("disabled") :
		    ((coex_sta->c2h_bt_inquiry_page) ? ("inquiry/page scan") :
		     ((BT_8723B_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
		       coex_dm->bt_status) ?
		      "non-connected idle" :
		      ((BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE ==
			coex_dm->bt_status) ?
		       "connected-idle" : "busy")))),
		       coex_sta->bt_rssi, coex_sta->bt_retry_cnt);

	seq_printf(m, "\n %-35s = %d / %d / %d / %d",
		   "SCO/HID/PAN/A2DP", bt_link_info->sco_exist,
		   bt_link_info->hid_exist, bt_link_info->pan_exist,
		   bt_link_info->a2dp_exist);
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_BT_LINK_INFO, m);

	bt_info_ext = coex_sta->bt_info_ext;
	seq_printf(m, "\n %-35s = %s",
		   "BT Info A2DP rate",
		   (bt_info_ext & BIT0) ? "Basic rate" : "EDR rate");

	for (i = 0; i < BT_INFO_SRC_8723B_1ANT_MAX; i++) {
		if (coex_sta->bt_info_c2h_cnt[i]) {
			seq_printf(m, "\n %-35s = %7ph(%d)",
				   glbt_info_src_8723b_1ant[i],
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
		/* Sw mechanism	*/
		seq_printf(m, "\n %-35s",
			   "============[Sw mechanism]============");

		seq_printf(m, "\n %-35s = %d/",
			   "SM[LowPenaltyRA]", coex_dm->cur_low_penalty_ra);

		seq_printf(m, "\n %-35s = %s/ %s/ %d ",
			   "DelBA/ BtCtrlAgg/ AggSize",
			   (btcoexist->bt_info.reject_agg_pkt ? "Yes" : "No"),
			   (btcoexist->bt_info.bt_ctrl_buf_size ? "Yes" : "No"),
			   btcoexist->bt_info.agg_buf_size);

		seq_printf(m, "\n %-35s = 0x%x ",
			   "Rate Mask", btcoexist->bt_info.ra_mask);

		/* Fw mechanism	*/
		seq_printf(m, "\n %-35s",
			   "============[Fw mechanism]============");

		pstdmacase = coex_dm->cur_ps_tdma;
		seq_printf(m, "\n %-35s = %5ph case-%d (auto:%d)",
			   "PS TDMA", coex_dm->ps_tdma_para,
			   pstdmacase, coex_dm->auto_tdma_adjust);

		seq_printf(m, "\n %-35s = %d ",
			   "IgnWlanAct", coex_dm->cur_ignore_wlan_act);

		seq_printf(m, "\n %-35s = 0x%x ",
			   "Latest error condition(should be 0)",
			   coex_dm->error_condition);
	}

	seq_printf(m, "\n %-35s = %d",
		   "Coex Table Type", coex_sta->coex_table_type);

	/* Hw setting */
	seq_printf(m, "\n %-35s",
		   "============[Hw setting]============");

	seq_printf(m, "\n %-35s = 0x%x/0x%x/0x%x/0x%x",
		   "backup ARFR1/ARFR2/RL/AMaxTime", coex_dm->backup_arfr_cnt1,
		   coex_dm->backup_arfr_cnt2, coex_dm->backup_retry_limit,
		   coex_dm->backup_ampdu_max_time);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x430);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x434);
	u16tmp[0] = btcoexist->btc_read_2byte(btcoexist, 0x42a);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x456);
	seq_printf(m, "\n %-35s = 0x%x/0x%x/0x%x/0x%x",
		   "0x430/0x434/0x42a/0x456",
		   u32tmp[0], u32tmp[1], u16tmp[0], u8tmp[0]);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x778);
	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x6cc);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x880);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x778/0x6cc/0x880[29:25]", u8tmp[0], u32tmp[0],
		   (u32tmp[1] & 0x3e000000) >> 25);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x948);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x67);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x765);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x948/ 0x67[5] / 0x765",
		   u32tmp[0], ((u8tmp[0] & 0x20) >> 5), u8tmp[1]);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x92c);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x930);
	u32tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0x944);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x92c[1:0]/ 0x930[7:0]/0x944[1:0]",
		   u32tmp[0] & 0x3, u32tmp[1] & 0xff, u32tmp[2] & 0x3);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x39);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x40);
	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x4c);
	u8tmp[2] = btcoexist->btc_read_1byte(btcoexist, 0x64);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x",
		   "0x38[11]/0x40/0x4c[24:23]/0x64[0]",
		   ((u8tmp[0] & 0x8) >> 3), u8tmp[1],
		    ((u32tmp[0] & 0x01800000) >> 23), u8tmp[2] & 0x1);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x550);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x522);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x",
		   "0x550(bcn ctrl)/0x522", u32tmp[0], u8tmp[0]);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xc50);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x49c);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x",
		   "0xc50(dig)/0x49c(null-drop)", u32tmp[0] & 0xff, u8tmp[0]);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xda0);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0xda4);
	u32tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0xda8);
	u32tmp[3] = btcoexist->btc_read_4byte(btcoexist, 0xcf0);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0xa5b);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0xa5c);

	fa_ofdm = ((u32tmp[0] & 0xffff0000) >> 16) +
		  ((u32tmp[1] & 0xffff0000) >> 16) +
		   (u32tmp[1] & 0xffff) +
		   (u32tmp[2] & 0xffff) +
		  ((u32tmp[3] & 0xffff0000) >> 16) +
		   (u32tmp[3] & 0xffff);
	fa_cck = (u8tmp[0] << 8) + u8tmp[1];

	seq_printf(m, "\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "OFDM-CCA/OFDM-FA/CCK-FA",
		 u32tmp[0] & 0xffff, fa_ofdm, fa_cck);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x6c0);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x6c4);
	u32tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0x6c8);
	seq_printf(m, "\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x6c0/0x6c4/0x6c8(coexTable)",
		   u32tmp[0], u32tmp[1], u32tmp[2]);

	seq_printf(m, "\n %-35s = %d/ %d",
		   "0x770(high-pri rx/tx)", coex_sta->high_priority_rx,
		   coex_sta->high_priority_tx);
	seq_printf(m, "\n %-35s = %d/ %d",
		   "0x774(low-pri rx/tx)", coex_sta->low_priority_rx,
		   coex_sta->low_priority_tx);
	if (btcoexist->auto_report_1ant)
		halbtc8723b1ant_monitor_bt_ctr(btcoexist);
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_COEX_STATISTICS, m);
}

void ex_btc8723b1ant_ips_notify(struct btc_coexist *btcoexist, u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	if (btcoexist->manual_control || btcoexist->stop_coex_dm)
		return;

	if (BTC_IPS_ENTER == type) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], IPS ENTER notify\n");
		coex_sta->under_ips = true;

		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_BT,
					     FORCE_EXEC, false, true);
		/* set PTA control */
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);
		halbtc8723b1ant_coex_table_with_type(btcoexist,
						     NORMAL_EXEC, 0);
	} else if (BTC_IPS_LEAVE == type) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], IPS LEAVE notify\n");
		coex_sta->under_ips = false;

		halbtc8723b1ant_init_hw_config(btcoexist, false, false);
		halbtc8723b1ant_init_coex_dm(btcoexist);
		halbtc8723b1ant_query_bt_info(btcoexist);
	}
}

void ex_btc8723b1ant_lps_notify(struct btc_coexist *btcoexist, u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	if (btcoexist->manual_control || btcoexist->stop_coex_dm)
		return;

	if (BTC_LPS_ENABLE == type) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], LPS ENABLE notify\n");
		coex_sta->under_lps = true;
	} else if (BTC_LPS_DISABLE == type) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], LPS DISABLE notify\n");
		coex_sta->under_lps = false;
	}
}

void ex_btc8723b1ant_scan_notify(struct btc_coexist *btcoexist, u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	bool wifi_connected = false, bt_hs_on = false;
	u8 u8tmpa, u8tmpb;
	u32 u32tmp;
	u32 wifi_link_status = 0;
	u32 num_of_wifi_link = 0;
	bool bt_ctrl_agg_buf_size = false;
	u8 agg_buf_size = 5;

	if (btcoexist->manual_control || btcoexist->stop_coex_dm)
		return;

	if (type == BTC_SCAN_START) {
		coex_sta->wifi_is_high_pri_task = true;
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], SCAN START notify\n");
		/* Force antenna setup for no scan result issue */
		halbtc8723b1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 8);
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA,
					     FORCE_EXEC, false, false);
		u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x948);
		u8tmpa = btcoexist->btc_read_1byte(btcoexist, 0x765);
		u8tmpb = btcoexist->btc_read_1byte(btcoexist, 0x67);

		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], 0x948=0x%x, 0x765=0x%x, 0x67=0x%x\n",
			u32tmp, u8tmpa, u8tmpb);
	} else {
		coex_sta->wifi_is_high_pri_task = false;
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], SCAN FINISH notify\n");

		btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM,
				   &coex_sta->scan_ap_num);
	}

	if (coex_sta->bt_disabled)
		return;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);

	halbtc8723b1ant_query_bt_info(btcoexist);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
			   &wifi_link_status);
	num_of_wifi_link = wifi_link_status >> 16;
	if (num_of_wifi_link >= 2) {
		halbtc8723b1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);
		halbtc8723b1ant_limited_rx(btcoexist, NORMAL_EXEC, false,
					   bt_ctrl_agg_buf_size, agg_buf_size);
		halbtc8723b1ant_action_wifi_multiport(btcoexist);
		return;
	}

	if (coex_sta->c2h_bt_inquiry_page) {
		halbtc8723b1ant_action_bt_inquiry(btcoexist);
		return;
	} else if (bt_hs_on) {
		halbtc8723b1ant_action_hs(btcoexist);
		return;
	}

	if (BTC_SCAN_START == type) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], SCAN START notify\n");
		if (!wifi_connected)
			/* non-connected scan */
			btc8723b1ant_action_wifi_not_conn_scan(btcoexist);
		else
			/* wifi is connected */
			btc8723b1ant_action_wifi_conn_scan(btcoexist);
	} else if (BTC_SCAN_FINISH == type) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], SCAN FINISH notify\n");
		if (!wifi_connected)
			/* non-connected scan */
			btc8723b1ant_action_wifi_not_conn(btcoexist);
		else
			halbtc8723b1ant_action_wifi_connected(btcoexist);
	}
}

void ex_btc8723b1ant_connect_notify(struct btc_coexist *btcoexist, u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	bool wifi_connected = false, bt_hs_on = false;
	u32 wifi_link_status = 0;
	u32 num_of_wifi_link = 0;
	bool bt_ctrl_agg_buf_size = false, under_4way = false;
	u8 agg_buf_size = 5;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS,
			   &under_4way);

	if (btcoexist->manual_control || btcoexist->stop_coex_dm ||
	    coex_sta->bt_disabled)
		return;

	if (type == BTC_ASSOCIATE_START) {
		coex_sta->wifi_is_high_pri_task = true;

		/* Force antenna setup for no scan result issue */
		halbtc8723b1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 8);
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA,
					     FORCE_EXEC, false, false);
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], CONNECT START notify\n");
		coex_dm->arp_cnt = 0;
	} else {
		coex_sta->wifi_is_high_pri_task = false;
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], CONNECT FINISH notify\n");
	}

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
			   &wifi_link_status);
	num_of_wifi_link = wifi_link_status>>16;
	if (num_of_wifi_link >= 2) {
		halbtc8723b1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);
		halbtc8723b1ant_limited_rx(btcoexist, NORMAL_EXEC, false,
					   bt_ctrl_agg_buf_size, agg_buf_size);
		halbtc8723b1ant_action_wifi_multiport(btcoexist);
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	if (coex_sta->c2h_bt_inquiry_page) {
		halbtc8723b1ant_action_bt_inquiry(btcoexist);
		return;
	} else if (bt_hs_on) {
		halbtc8723b1ant_action_hs(btcoexist);
		return;
	}

	if (BTC_ASSOCIATE_START == type) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], CONNECT START notify\n");
		btc8723b1ant_act_wifi_not_conn_asso_auth(btcoexist);
	} else if (BTC_ASSOCIATE_FINISH == type) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], CONNECT FINISH notify\n");

		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
				   &wifi_connected);
		if (!wifi_connected)
			/* non-connected scan */
			btc8723b1ant_action_wifi_not_conn(btcoexist);
		else
			halbtc8723b1ant_action_wifi_connected(btcoexist);
	}
}

void ex_btc8723b1ant_media_status_notify(struct btc_coexist *btcoexist,
					 u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 h2c_parameter[3] = {0};
	u32 wifi_bw;
	u8 wifi_central_chnl;
	bool wifi_under_b_mode = false;

	if (btcoexist->manual_control || btcoexist->stop_coex_dm ||
	    coex_sta->bt_disabled)
		return;

	if (type == BTC_MEDIA_CONNECT) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], MEDIA connect notify\n");
		/* Force antenna setup for no scan result issue */
		halbtc8723b1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 8);
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA,
					     FORCE_EXEC, false, false);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_B_MODE,
				   &wifi_under_b_mode);

		/* Set CCK Tx/Rx high Pri except 11b mode */
		if (wifi_under_b_mode) {
			btcoexist->btc_write_1byte(btcoexist, 0x6cd,
						   0x00); /* CCK Tx */
			btcoexist->btc_write_1byte(btcoexist, 0x6cf,
						   0x00); /* CCK Rx */
		} else {
			btcoexist->btc_write_1byte(btcoexist, 0x6cd,
						   0x00); /* CCK Tx */
			btcoexist->btc_write_1byte(btcoexist, 0x6cf,
						   0x10); /* CCK Rx */
		}

		coex_dm->backup_arfr_cnt1 =
			btcoexist->btc_read_4byte(btcoexist, 0x430);
		coex_dm->backup_arfr_cnt2 =
			btcoexist->btc_read_4byte(btcoexist, 0x434);
		coex_dm->backup_retry_limit =
			btcoexist->btc_read_2byte(btcoexist, 0x42a);
		coex_dm->backup_ampdu_max_time =
			btcoexist->btc_read_1byte(btcoexist, 0x456);
	} else {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], MEDIA disconnect notify\n");
		coex_dm->arp_cnt = 0;

		btcoexist->btc_write_1byte(btcoexist, 0x6cd, 0x0); /* CCK Tx */
		btcoexist->btc_write_1byte(btcoexist, 0x6cf, 0x0); /* CCK Rx */

		coex_sta->cck_ever_lock = false;
	}

	/* only 2.4G we need to inform bt the chnl mask */
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_CENTRAL_CHNL,
			   &wifi_central_chnl);

	if (type == BTC_MEDIA_CONNECT && wifi_central_chnl <= 14) {
		h2c_parameter[0] = 0x0;
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

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], FW write 0x66 = 0x%x\n",
		h2c_parameter[0] << 16 | h2c_parameter[1] << 8 |
		h2c_parameter[2]);

	btcoexist->btc_fill_h2c(btcoexist, 0x66, 3, h2c_parameter);
}

void ex_btc8723b1ant_special_packet_notify(struct btc_coexist *btcoexist,
					   u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	bool bt_hs_on = false;
	u32 wifi_link_status = 0;
	u32 num_of_wifi_link = 0;
	bool bt_ctrl_agg_buf_size = false, under_4way = false;
	u8 agg_buf_size = 5;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS,
			   &under_4way);

	if (btcoexist->manual_control || btcoexist->stop_coex_dm ||
	    coex_sta->bt_disabled)
		return;

	if (type == BTC_PACKET_DHCP || type == BTC_PACKET_EAPOL ||
	    type == BTC_PACKET_ARP) {
		if (type == BTC_PACKET_ARP) {
			rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				"[BTCoex], special Packet ARP notify\n");

			coex_dm->arp_cnt++;
			rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				"[BTCoex], ARP Packet Count = %d\n",
				 coex_dm->arp_cnt);

			if ((coex_dm->arp_cnt >= 10) && (!under_4way))
				/* if APR PKT > 10 after connect, do not go to
				 * ActionWifiConnectedSpecificPacket(btcoexist)
				 */
				coex_sta->wifi_is_high_pri_task = false;
			else
				coex_sta->wifi_is_high_pri_task = true;
		} else {
			coex_sta->wifi_is_high_pri_task = true;
			rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				"[BTCoex], special Packet DHCP or EAPOL notify\n");
		}
	} else {
		coex_sta->wifi_is_high_pri_task = false;
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], special Packet [Type = %d] notify\n",
			 type);
	}

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
		&wifi_link_status);
	num_of_wifi_link = wifi_link_status >> 16;
	if (num_of_wifi_link >= 2) {
		halbtc8723b1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);
		halbtc8723b1ant_limited_rx(btcoexist, NORMAL_EXEC, false,
					   bt_ctrl_agg_buf_size, agg_buf_size);
		halbtc8723b1ant_action_wifi_multiport(btcoexist);
		return;
	}

	coex_sta->special_pkt_period_cnt = 0;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	if (coex_sta->c2h_bt_inquiry_page) {
		halbtc8723b1ant_action_bt_inquiry(btcoexist);
		return;
	} else if (bt_hs_on) {
		halbtc8723b1ant_action_hs(btcoexist);
		return;
	}

	if (BTC_PACKET_DHCP == type ||
	    BTC_PACKET_EAPOL == type) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], special Packet(%d) notify\n", type);
		halbtc8723b1ant_action_wifi_connected_special_packet(btcoexist);
	}
}

void ex_btc8723b1ant_bt_info_notify(struct btc_coexist *btcoexist,
				    u8 *tmp_buf, u8 length)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 bt_info = 0;
	u8 i, rsp_source = 0;
	bool wifi_connected = false;
	bool bt_busy = false;

	coex_sta->c2h_bt_info_req_sent = false;

	rsp_source = tmp_buf[0] & 0xf;
	if (rsp_source >= BT_INFO_SRC_8723B_1ANT_MAX)
		rsp_source = BT_INFO_SRC_8723B_1ANT_WIFI_FW;
	coex_sta->bt_info_c2h_cnt[rsp_source]++;

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], Bt info[%d], length=%d, hex data = [",
		 rsp_source, length);
	for (i = 0; i < length; i++) {
		coex_sta->bt_info_c2h[rsp_source][i] = tmp_buf[i];
		if (i == 1)
			bt_info = tmp_buf[i];
		if (i == length - 1)
			rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				"0x%02x]\n", tmp_buf[i]);
		else
			rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				"0x%02x, ", tmp_buf[i]);
	}

	/* if 0xff, it means BT is under WHCK test */
	if (bt_info == 0xff)
		coex_sta->bt_whck_test = true;
	else
		coex_sta->bt_whck_test = false;

	if (rsp_source != BT_INFO_SRC_8723B_1ANT_WIFI_FW) {
		coex_sta->bt_retry_cnt = /* [3:0] */
			coex_sta->bt_info_c2h[rsp_source][2] & 0xf;

		if (coex_sta->bt_retry_cnt >= 1)
			coex_sta->pop_event_cnt++;

		if (coex_sta->bt_info_c2h[rsp_source][2] & 0x20)
			coex_sta->c2h_bt_remote_name_req = true;
		else
			coex_sta->c2h_bt_remote_name_req = false;

		coex_sta->bt_rssi =
			coex_sta->bt_info_c2h[rsp_source][3] * 2 - 90;

		coex_sta->bt_info_ext =
			coex_sta->bt_info_c2h[rsp_source][4];

		if (coex_sta->bt_info_c2h[rsp_source][1] == 0x49) {
			coex_sta->a2dp_bit_pool =
				coex_sta->bt_info_c2h[rsp_source][6];
		} else {
			coex_sta->a2dp_bit_pool = 0;
		}

		coex_sta->bt_tx_rx_mask =
			(coex_sta->bt_info_c2h[rsp_source][2] & 0x40);
		btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TX_RX_MASK,
				   &coex_sta->bt_tx_rx_mask);

		if (!coex_sta->bt_tx_rx_mask) {
			/* BT into is responded by BT FW and BT RF REG
			 * 0x3C != 0x15 => Need to switch BT TRx Mask
			 */
			rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				"[BTCoex], Switch BT TRx Mask since BT RF REG 0x3C != 0x15\n");
			btcoexist->btc_set_bt_reg(btcoexist, BTC_BT_REG_RF,
						  0x3c, 0x15);

			/* BT TRx Mask lock 0x2c[0], 0x30[0] = 0 */
			btcoexist->btc_set_bt_reg(btcoexist, BTC_BT_REG_RF,
						  0x2c, 0x7c44);
			btcoexist->btc_set_bt_reg(btcoexist, BTC_BT_REG_RF,
						  0x30, 0x7c44);
		}

		/* Here we need to resend some wifi info to BT
		 * because bt is reset and loss of the info.
		 */
		if (coex_sta->bt_info_ext & BIT1) {
			rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
				"[BTCoex], BT ext info bit1 check, send wifi BW&Chnl to BT!!\n");
			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
					   &wifi_connected);
			if (wifi_connected)
				ex_btc8723b1ant_media_status_notify(btcoexist,
						BTC_MEDIA_CONNECT);
			else
				ex_btc8723b1ant_media_status_notify(btcoexist,
						BTC_MEDIA_DISCONNECT);
		}

		if (coex_sta->bt_info_ext & BIT3) {
			if (!btcoexist->manual_control &&
			    !btcoexist->stop_coex_dm) {
				rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
					"[BTCoex], BT ext info bit3 check, set BT NOT ignore Wlan active!!\n");
				halbtc8723b1ant_ignore_wlan_act(btcoexist,
								FORCE_EXEC,
								false);
			}
		} else {
			/* BT already NOT ignore Wlan active, do nothing here.*/
		}
		if (!btcoexist->auto_report_1ant) {
			if (coex_sta->bt_info_ext & BIT4) {
				/* BT auto report already enabled, do nothing */
			} else {
				halbtc8723b1ant_bt_auto_report(btcoexist,
							       FORCE_EXEC,
							       true);
			}
		}
	}

	/* check BIT2 first ==> check if bt is under inquiry or page scan */
	if (bt_info & BT_INFO_8723B_1ANT_B_INQ_PAGE)
		coex_sta->c2h_bt_inquiry_page = true;
	else
		coex_sta->c2h_bt_inquiry_page = false;

	coex_sta->num_of_profile = 0;

	/* set link exist status */
	if (!(bt_info & BT_INFO_8723B_1ANT_B_CONNECTION)) {
		coex_sta->bt_link_exist = false;
		coex_sta->pan_exist = false;
		coex_sta->a2dp_exist = false;
		coex_sta->hid_exist = false;
		coex_sta->sco_exist = false;

		coex_sta->bt_hi_pri_link_exist = false;
	} else {
		/* connection exists */
		coex_sta->bt_link_exist = true;
		if (bt_info & BT_INFO_8723B_1ANT_B_FTP) {
			coex_sta->pan_exist = true;
			coex_sta->num_of_profile++;
		} else {
			coex_sta->pan_exist = false;
		}
		if (bt_info & BT_INFO_8723B_1ANT_B_A2DP) {
			coex_sta->a2dp_exist = true;
			coex_sta->num_of_profile++;
		} else {
			coex_sta->a2dp_exist = false;
		}
		if (bt_info & BT_INFO_8723B_1ANT_B_HID) {
			coex_sta->hid_exist = true;
			coex_sta->num_of_profile++;
		} else {
			coex_sta->hid_exist = false;
		}
		if (bt_info & BT_INFO_8723B_1ANT_B_SCO_ESCO) {
			coex_sta->sco_exist = true;
			coex_sta->num_of_profile++;
		} else {
			coex_sta->sco_exist = false;
		}

		if ((!coex_sta->hid_exist) &&
		    (!coex_sta->c2h_bt_inquiry_page) &&
		    (!coex_sta->sco_exist)) {
			if (coex_sta->high_priority_tx +
				    coex_sta->high_priority_rx >=
			    160) {
				coex_sta->hid_exist = true;
				coex_sta->wrong_profile_notification++;
				coex_sta->num_of_profile++;
				bt_info = bt_info | 0x28;
			}
		}

		/* Add Hi-Pri Tx/Rx counter to avoid false detection */
		if (((coex_sta->hid_exist) || (coex_sta->sco_exist)) &&
		    (coex_sta->high_priority_tx + coex_sta->high_priority_rx >=
		     160) &&
		    (!coex_sta->c2h_bt_inquiry_page))
			coex_sta->bt_hi_pri_link_exist = true;

		if ((bt_info & BT_INFO_8723B_1ANT_B_ACL_BUSY) &&
		    (coex_sta->num_of_profile == 0)) {
			if (coex_sta->low_priority_tx +
				    coex_sta->low_priority_rx >=
			    160) {
				coex_sta->pan_exist = true;
				coex_sta->num_of_profile++;
				coex_sta->wrong_profile_notification++;
				bt_info = bt_info | 0x88;
			}
		}
	}

	halbtc8723b1ant_update_bt_link_info(btcoexist);

	/* mask profile bit for connect-ilde identification
	 * ( for CSR case: A2DP idle --> 0x41)
	 */
	bt_info = bt_info & 0x1f;

	if (!(bt_info & BT_INFO_8723B_1ANT_B_CONNECTION)) {
		coex_dm->bt_status = BT_8723B_1ANT_BT_STATUS_NON_CONNECTED_IDLE;
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], BtInfoNotify(), BT Non-Connected idle!\n");
	/* connection exists but no busy */
	} else if (bt_info == BT_INFO_8723B_1ANT_B_CONNECTION) {
		coex_dm->bt_status = BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE;
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], BtInfoNotify(), BT Connected-idle!!!\n");
	} else if ((bt_info & BT_INFO_8723B_1ANT_B_SCO_ESCO) ||
		(bt_info & BT_INFO_8723B_1ANT_B_SCO_BUSY)) {
		coex_dm->bt_status = BT_8723B_1ANT_BT_STATUS_SCO_BUSY;
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], BtInfoNotify(), BT SCO busy!!!\n");
	} else if (bt_info & BT_INFO_8723B_1ANT_B_ACL_BUSY) {
		if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY != coex_dm->bt_status)
			coex_dm->auto_tdma_adjust = false;

		coex_dm->bt_status = BT_8723B_1ANT_BT_STATUS_ACL_BUSY;
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], BtInfoNotify(), BT ACL busy!!!\n");
	} else {
		coex_dm->bt_status = BT_8723B_1ANT_BT_STATUS_MAX;
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], BtInfoNotify(), BT Non-Defined state!!\n");
	}

	if ((BT_8723B_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) ||
	    (BT_8723B_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
	    (BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY == coex_dm->bt_status))
		bt_busy = true;
	else
		bt_busy = false;
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bt_busy);

	halbtc8723b1ant_run_coexist_mechanism(btcoexist);
}

void ex_btc8723b1ant_rf_status_notify(struct btc_coexist *btcoexist, u8 type)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u32 u32tmp;
	u8 u8tmpa, u8tmpb, u8tmpc;

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], RF Status notify\n");

	if (type == BTC_RF_ON) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], RF is turned ON!!\n");
		btcoexist->stop_coex_dm = false;
	} else if (type == BTC_RF_OFF) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], RF is turned OFF!!\n");

		halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
		halbtc8723b1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 0);
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_BT,
					     FORCE_EXEC, false, true);

		halbtc8723b1ant_ignore_wlan_act(btcoexist, FORCE_EXEC, true);
		btcoexist->stop_coex_dm = true;

		u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x948);
		u8tmpa = btcoexist->btc_read_1byte(btcoexist, 0x765);
		u8tmpb = btcoexist->btc_read_1byte(btcoexist, 0x67);
		u8tmpc = btcoexist->btc_read_1byte(btcoexist, 0x76e);

		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"############# [BTCoex], 0x948=0x%x, 0x765=0x%x, 0x67=0x%x, 0x76e=0x%x\n",
			u32tmp, u8tmpa, u8tmpb, u8tmpc);
	}
}

void ex_btc8723b1ant_halt_notify(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD, "[BTCoex], Halt notify\n");

	btcoexist->stop_coex_dm = true;

	halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_BT, FORCE_EXEC,
				     false, true);

	halbtc8723b1ant_ignore_wlan_act(btcoexist, FORCE_EXEC, true);

	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					 0x0, 0x0);
	halbtc8723b1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 0);

	ex_btc8723b1ant_media_status_notify(btcoexist, BTC_MEDIA_DISCONNECT);

	btcoexist->stop_coex_dm = true;
}

void ex_btc8723b1ant_pnp_notify(struct btc_coexist *btcoexist, u8 pnp_state)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD, "[BTCoex], Pnp notify\n");

	if (BTC_WIFI_PNP_SLEEP == pnp_state) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], Pnp notify to SLEEP\n");
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_BT,
					     FORCE_EXEC, false, true);
		halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);

		/* Driver do not leave IPS/LPS when driver is going to sleep, so
		 * BTCoexistence think wifi is still under IPS/LPS
		 *
		 * BT should clear UnderIPS/UnderLPS state to avoid mismatch
		 * state after wakeup.
		 */
		coex_sta->under_ips = false;
		coex_sta->under_lps = false;
		btcoexist->stop_coex_dm = true;
	} else if (BTC_WIFI_PNP_WAKE_UP == pnp_state) {
		rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			"[BTCoex], Pnp notify to WAKE UP\n");
		btcoexist->stop_coex_dm = false;
		halbtc8723b1ant_init_hw_config(btcoexist, false, false);
		halbtc8723b1ant_init_coex_dm(btcoexist);
		halbtc8723b1ant_query_bt_info(btcoexist);
	}
}

void ex_btc8723b1ant_coex_dm_reset(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], *****************Coex DM Reset****************\n");

	halbtc8723b1ant_init_hw_config(btcoexist, false, false);
	halbtc8723b1ant_init_coex_dm(btcoexist);
}

void ex_btc8723b1ant_periodical(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"[BTCoex], ==========================Periodical===========================\n");

	if (!btcoexist->auto_report_1ant) {
		halbtc8723b1ant_query_bt_info(btcoexist);
		halbtc8723b1ant_monitor_bt_enable_disable(btcoexist);
	} else {
		halbtc8723b1ant_monitor_bt_ctr(btcoexist);
		halbtc8723b1ant_monitor_wifi_ctr(btcoexist);

		if ((coex_sta->high_priority_tx + coex_sta->high_priority_rx < 50) &&
		    bt_link_info->hid_exist)
			bt_link_info->hid_exist = false;

		if (btc8723b1ant_is_wifi_status_changed(btcoexist) ||
		    coex_dm->auto_tdma_adjust) {
			halbtc8723b1ant_run_coexist_mechanism(btcoexist);
		}
		coex_sta->special_pkt_period_cnt++;
	}
}
