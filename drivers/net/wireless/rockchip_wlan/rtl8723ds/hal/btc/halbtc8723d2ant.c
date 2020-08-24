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
 * This file is for RTL8723D Co-exist mechanism
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

#if (RTL8723D_SUPPORT == 1)
/* ************************************************************
 * Global variables, these are static variables
 * ************************************************************ */
static u8	*trace_buf = &gl_btc_trace_buf[0];
static struct  coex_dm_8723d_2ant		glcoex_dm_8723d_2ant;
static struct  coex_dm_8723d_2ant	*coex_dm = &glcoex_dm_8723d_2ant;
static struct  coex_sta_8723d_2ant		glcoex_sta_8723d_2ant;
static struct  coex_sta_8723d_2ant	*coex_sta = &glcoex_sta_8723d_2ant;
static struct  psdscan_sta_8723d_2ant	gl_psd_scan_8723d_2ant;
static struct  psdscan_sta_8723d_2ant *psd_scan = &gl_psd_scan_8723d_2ant;

const char *const glbt_info_src_8723d_2ant[] = {
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

u32	glcoex_ver_date_8723d_2ant = 20200103;
u32	glcoex_ver_8723d_2ant = 0x35;
u32	glcoex_ver_btdesired_8723d_2ant = 0x35;

static
u8 halbtc8723d2ant_bt_rssi_state(IN struct btc_coexist *btcoexist,
	u8 *ppre_bt_rssi_state, u8 level_num,
	u8 rssi_thresh, u8 rssi_thresh1)
{
	s32			bt_rssi = 0;
	u8			bt_rssi_state = *ppre_bt_rssi_state;

	bt_rssi = coex_sta->bt_rssi;

	if (level_num == 2) {
		if ((*ppre_bt_rssi_state == BTC_RSSI_STATE_LOW) ||
		    (*ppre_bt_rssi_state == BTC_RSSI_STATE_STAY_LOW)) {
			if (bt_rssi >= (rssi_thresh +
					BTC_RSSI_COEX_THRESH_TOL_8723D_2ANT))
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
			return *ppre_bt_rssi_state;
		}

		if ((*ppre_bt_rssi_state == BTC_RSSI_STATE_LOW) ||
		    (*ppre_bt_rssi_state == BTC_RSSI_STATE_STAY_LOW)) {
			if (bt_rssi >= (rssi_thresh +
					BTC_RSSI_COEX_THRESH_TOL_8723D_2ANT))
				bt_rssi_state = BTC_RSSI_STATE_MEDIUM;
			else
				bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
		} else if ((*ppre_bt_rssi_state == BTC_RSSI_STATE_MEDIUM) ||
			(*ppre_bt_rssi_state == BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (bt_rssi >= (rssi_thresh1 +
					BTC_RSSI_COEX_THRESH_TOL_8723D_2ANT))
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

	*ppre_bt_rssi_state = bt_rssi_state;

	return bt_rssi_state;
}

static
u8 halbtc8723d2ant_wifi_rssi_state(IN struct btc_coexist *btcoexist,
	   IN u8 *pprewifi_rssi_state, IN u8 level_num, IN u8 rssi_thresh,
				   IN u8 rssi_thresh1)
{
	s32			wifi_rssi = 0;
	u8			wifi_rssi_state = *pprewifi_rssi_state;

	btcoexist->btc_get(btcoexist, BTC_GET_S4_WIFI_RSSI, &wifi_rssi);

	if (level_num == 2) {
		if ((*pprewifi_rssi_state == BTC_RSSI_STATE_LOW) ||
		    (*pprewifi_rssi_state == BTC_RSSI_STATE_STAY_LOW)) {
			if (wifi_rssi >= (rssi_thresh +
					  BTC_RSSI_COEX_THRESH_TOL_8723D_2ANT))
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
			return *pprewifi_rssi_state;
		}

		if ((*pprewifi_rssi_state == BTC_RSSI_STATE_LOW) ||
		    (*pprewifi_rssi_state == BTC_RSSI_STATE_STAY_LOW)) {
			if (wifi_rssi >= (rssi_thresh +
					  BTC_RSSI_COEX_THRESH_TOL_8723D_2ANT))
				wifi_rssi_state = BTC_RSSI_STATE_MEDIUM;
			else
				wifi_rssi_state = BTC_RSSI_STATE_STAY_LOW;
		} else if ((*pprewifi_rssi_state == BTC_RSSI_STATE_MEDIUM) ||
			(*pprewifi_rssi_state == BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (wifi_rssi >= (rssi_thresh1 +
					  BTC_RSSI_COEX_THRESH_TOL_8723D_2ANT))
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

	*pprewifi_rssi_state = wifi_rssi_state;

	return wifi_rssi_state;
}

static
void halbtc8723d2ant_coex_switch_threshold(IN struct btc_coexist *btcoexist,
		IN u8 isolation_measuared)
{
	s8	interference_wl_tx = 0, interference_bt_tx = 0;


	interference_wl_tx = BT_8723D_2ANT_WIFI_MAX_TX_POWER -
			     isolation_measuared;
	interference_bt_tx = BT_8723D_2ANT_BT_MAX_TX_POWER -
			     isolation_measuared;



	coex_sta->wifi_coex_thres		 = BT_8723D_2ANT_WIFI_RSSI_COEXSWITCH_THRES1;
	coex_sta->wifi_coex_thres2	 = BT_8723D_2ANT_WIFI_RSSI_COEXSWITCH_THRES2;

	coex_sta->bt_coex_thres		 = BT_8723D_2ANT_BT_RSSI_COEXSWITCH_THRES1;
	coex_sta->bt_coex_thres2		 = BT_8723D_2ANT_BT_RSSI_COEXSWITCH_THRES2;
}

static void
halbtc8723d2ant_limited_tx(struct btc_coexist *btcoexist, boolean force_exec,
			   boolean tx_limit_en,  boolean ampdu_limit_en)
{
	boolean wifi_under_b_mode = FALSE;
	u32	wifi_link_status = 0, num_of_wifi_link = 0;

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
			   &wifi_link_status);

	num_of_wifi_link = wifi_link_status >> 16;

	/* Force Max Tx retry limit = 8*/
	if (!coex_sta->wl_tx_limit_en) {
		coex_sta->wl_0x430_backup =
			btcoexist->btc_read_4byte(btcoexist, 0x430);
		coex_sta->wl_0x434_backup =
			btcoexist->btc_read_4byte(btcoexist, 0x434);
		coex_sta->wl_0x42a_backup =
			btcoexist->btc_read_2byte(btcoexist, 0x42a);
	}

	if (!coex_sta->wl_ampdu_limit_en)
		coex_sta->wl_0x456_backup = btcoexist->btc_read_1byte(btcoexist,
								      0x456);

	if (!force_exec && tx_limit_en == coex_sta->wl_tx_limit_en &&
	    ampdu_limit_en == coex_sta->wl_ampdu_limit_en)
		return;

	coex_sta->wl_tx_limit_en = tx_limit_en;
	coex_sta->wl_ampdu_limit_en = ampdu_limit_en;

	if (tx_limit_en) {
		/* Set BT polluted packet on for Tx rate adaptive not
		 * including Tx retry break by PTA, 0x45c[19] =1
		 *
		 * Set queue life time to avoid can't reach tx retry limit
		 * if tx is always break by GNT_BT.
		 */
		if ((wifi_link_status & WIFI_STA_CONNECTED) &&
		     num_of_wifi_link == 1) {
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0x45e, 0x8, 0x1);
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0x426, 0xf, 0xf);
		}

		/* Max Tx retry limit = 8*/
		btcoexist->btc_write_2byte(btcoexist, 0x42a, 0x0808);

		/* AMPDU duration limit*/
		btcoexist->btc_write_1byte(btcoexist, 0x456, 0x20);

		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_B_MODE,
				   &wifi_under_b_mode);

		/* Auto rate fallback step within 8 retry*/
		if (wifi_under_b_mode) {
			btcoexist->btc_write_4byte(btcoexist, 0x430, 0x1000000);
			btcoexist->btc_write_4byte(btcoexist, 0x434, 0x1010101);
		} else {
			btcoexist->btc_write_4byte(btcoexist, 0x430, 0x1000000);
			btcoexist->btc_write_4byte(btcoexist, 0x434, 0x4030201);
		}
	} else {
		/* Set BT polluted packet on for Tx rate adaptive not
		 *including Tx retry break by PTA, 0x45c[19] =1
		 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x45e, 0x8, 0x0);

		/* Set queue life time to avoid can't reach tx retry limit
		 * if tx is always break by GNT_BT.
		 */
		if (num_of_wifi_link  == 1)
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0x426, 0xf, 0x0);

		/* Recovery Max Tx retry limit*/
		btcoexist->btc_write_2byte(btcoexist, 0x42a,
					   coex_sta->wl_0x42a_backup);
		btcoexist->btc_write_4byte(btcoexist, 0x430,
					   coex_sta->wl_0x430_backup);
		btcoexist->btc_write_4byte(btcoexist, 0x434,
					   coex_sta->wl_0x434_backup);
	}

	if (ampdu_limit_en)
		btcoexist->btc_write_1byte(btcoexist, 0x456, 0x20);
	else
		btcoexist->btc_write_1byte(btcoexist, 0x456,
					   coex_sta->wl_0x456_backup);
}

static void
halbtc8723d2ant_limited_rx(struct btc_coexist *btcoexist, boolean force_exec,
			   boolean rej_ap_agg_pkt, boolean bt_ctrl_agg_buf_size,
			   u8 agg_buf_size)
{
#if 0
	boolean reject_rx_agg = rej_ap_agg_pkt;
	boolean bt_ctrl_rx_agg_size = bt_ctrl_agg_buf_size;
	u8 rx_agg_size = agg_buf_size;

	if (!force_exec &&
	    bt_ctrl_agg_buf_size == coex_sta->wl_rxagg_limit_en &&
	    agg_buf_size == coex_sta->wl_rxagg_size)
		return;

	coex_sta->wl_rxagg_limit_en = bt_ctrl_agg_buf_size;
	coex_sta->wl_rxagg_size = agg_buf_size;

	/*btc->btc_set(btcoexist, BTC_SET_BL_TO_REJ_AP_AGG_PKT,
	 *&reject_rx_agg);
	 */
	/* decide BT control aggregation buf size or not */
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_CTRL_AGG_SIZE,
			   &bt_ctrl_rx_agg_size);
	/* aggregation buf size, only work
	 * when BT control Rx aggregation size
	 */
	btcoexist->btc_set(btcoexist, BTC_SET_U1_AGG_BUF_SIZE, &rx_agg_size);
	/* real update aggregation setting */
	btcoexist->btc_set(btcoexist, BTC_SET_ACT_AGGREGATE_CTRL, NULL);
#endif
}

static void
halbtc8723d2ant_set_tdma_timer_base(struct btc_coexist *btc, u8 type)
{
	u16 tbtt_interval = 100;
	u8 h2c_para[2] = {0xb, 0x1};

	btc->btc_get(btc, BTC_GET_U2_BEACON_PERIOD, &tbtt_interval);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		 "[BTCoex], tbtt_interval = %d\n", tbtt_interval);
	BTC_TRACE(trace_buf);

	/* Add for JIRA coex-256 */
	if (type == 3 && tbtt_interval < 120) { /* 4-slot  */
		if (coex_sta->tdma_timer_base == 3)
			return;

		h2c_para[1] = 0xc1; /* 4-slot */
		coex_sta->tdma_timer_base = 3;
	} else if (tbtt_interval < 80 && tbtt_interval > 0) {
		if (coex_sta->tdma_timer_base == 2)
			return;
		h2c_para[1] = (100 / tbtt_interval);

		if (100 % tbtt_interval != 0)
			h2c_para[1] = h2c_para[1] + 1;

		h2c_para[1] = h2c_para[1] & 0x3f;
		coex_sta->tdma_timer_base = 2;
	} else if (tbtt_interval >= 180) {
		if (coex_sta->tdma_timer_base == 1)
			return;
		h2c_para[1] = (tbtt_interval / 100);

		if (tbtt_interval % 100 <= 80)
			h2c_para[1] = h2c_para[1] - 1;

		h2c_para[1] = h2c_para[1] & 0x3f;
		h2c_para[1] = h2c_para[1] | 0x80;
		coex_sta->tdma_timer_base = 1;
	} else {
		if (coex_sta->tdma_timer_base == 0)
			return;
		h2c_para[1] = 0x1;
		coex_sta->tdma_timer_base = 0;
	}

	btc->btc_fill_h2c(btc, 0x69, 2, h2c_para);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		 "[BTCoex], %s() h2c_0x69 = 0x%x\n", __func__, h2c_para[1]);
	BTC_TRACE(trace_buf);

#if 0
	/* no 5ms_wl_slot_extend for 4-slot mode  */
	if (coex_sta->tdma_timer_base == 3)
		halbtc8822b2ant_ccklock_action(btc);
#endif
}


static
void halbtc8723d2ant_set_fw_low_penalty_ra(IN struct btc_coexist
		*btcoexist, IN boolean low_penalty_ra)
{
	u8 h2c_parameter[6] = {0};

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

static
void halbtc8723d2ant_low_penalty_ra(IN struct btc_coexist *btcoexist,
				    IN boolean force_exec,
				    IN boolean low_penalty_ra)
{
	coex_dm->cur_low_penalty_ra = low_penalty_ra;

	if (!force_exec) {
		if (coex_dm->pre_low_penalty_ra == coex_dm->cur_low_penalty_ra)
			return;
	}

	halbtc8723d2ant_set_fw_low_penalty_ra(btcoexist,
					      coex_dm->cur_low_penalty_ra);

#if 0
	if (low_penalty_ra)
		btcoexist->btc_phydm_modify_RA_PCR_threshold(btcoexist, 0, 15);
	else
		btcoexist->btc_phydm_modify_RA_PCR_threshold(btcoexist, 0, 0);
#endif
	coex_dm->pre_low_penalty_ra = coex_dm->cur_low_penalty_ra;
}

static
void halbtc8723d2ant_query_bt_info(IN struct btc_coexist *btcoexist)
{
	u8			h2c_parameter[1] = {0};


	h2c_parameter[0] |= BIT(0);	/* trigger */

	btcoexist->btc_fill_h2c(btcoexist, 0x61, 1, h2c_parameter);
}

static
boolean halbtc8723d2ant_monitor_bt_ctr(IN struct btc_coexist *btcoexist)
{
	u32 reg_hp_txrx, reg_lp_txrx, u32tmp, cnt_bt_all;
	u32 reg_hp_tx = 0, reg_hp_rx = 0, reg_lp_tx = 0, reg_lp_rx = 0;
	static u8	num_of_bt_counter_chk = 0, cnt_overhead = 0,
		cnt_autoslot_hang = 0;
	static u32 cnt_bt_pre = 0;
	boolean is_run_coex = FALSE;

	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

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
		    "[BTCoex], Hi-Pri Rx/Tx: %d/%d, Lo-Pri Rx/Tx: %d/%d\n",
		    reg_hp_rx, reg_hp_tx, reg_lp_rx, reg_lp_tx);

	BTC_TRACE(trace_buf);
	
	/* reset counter */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0xc);
	
	if (coex_sta->under_lps || coex_sta->under_ips ||
	     (coex_sta->high_priority_tx == 65535 &&
	      coex_sta->high_priority_rx == 65535 &&
	      coex_sta->low_priority_tx == 65535 &&
	      coex_sta->low_priority_rx == 65535))
		coex_sta->bt_ctr_ok = FALSE;
	else
		coex_sta->bt_ctr_ok = TRUE;
	
	if (!coex_sta->bt_ctr_ok)
		return FALSE;

	if (coex_dm->bt_status == BT_8723D_2ANT_BT_STATUS_NON_CONNECTED_IDLE) {
		if (coex_sta->high_priority_rx >= 15) {
			if (cnt_overhead < 3)
				cnt_overhead++;

			if (cnt_overhead == 3)
				coex_sta->is_hipri_rx_overhead = TRUE;
		} else {
			if (cnt_overhead > 0)
				cnt_overhead--;

			if (cnt_overhead == 0)
				coex_sta->is_hipri_rx_overhead = FALSE;
		}
	} else {
		coex_sta->is_hipri_rx_overhead = FALSE;
	}

	if (coex_sta->low_priority_tx > 1050  &&
	    !coex_sta->c2h_bt_inquiry_page)
		coex_sta->pop_event_cnt++;

	if (coex_sta->is_tdma_btautoslot) {
		if (coex_sta->low_priority_tx >= 1300 &&
		    coex_sta->low_priority_rx <= 150) {
			if (cnt_autoslot_hang >= 2) {
				coex_sta->is_tdma_btautoslot_hang = TRUE;
				cnt_autoslot_hang = 2;
			} else
				cnt_autoslot_hang++;
		} else {
			if (cnt_autoslot_hang == 0)	{
				coex_sta->is_tdma_btautoslot_hang = FALSE;
				cnt_autoslot_hang = 0;
			} else
				cnt_autoslot_hang--;
		}
	}

	if (coex_sta->sco_exist) {
		if (coex_sta->high_priority_tx >= 400 &&
		    coex_sta->high_priority_rx >= 400)
			coex_sta->is_esco_mode = FALSE;
		else
			coex_sta->is_esco_mode = TRUE;
	}

	if (bt_link_info->hid_only) {
		if (coex_sta->low_priority_tx > 50)
			coex_sta->is_hid_low_pri_tx_overhead = true;
		else
			coex_sta->is_hid_low_pri_tx_overhead = false;
	}

	if (!coex_sta->bt_disabled) {
		if (coex_sta->high_priority_tx == 0 &&
		    coex_sta->high_priority_rx == 0 &&
		    coex_sta->low_priority_tx == 0 &&
		    coex_sta->low_priority_rx == 0) {
			num_of_bt_counter_chk++;
			if (num_of_bt_counter_chk >= 3) {
				halbtc8723d2ant_query_bt_info(btcoexist);
				num_of_bt_counter_chk = 0;
			}
		}
	}

	cnt_bt_all = coex_sta->high_priority_tx +
		     coex_sta->high_priority_rx +
		     coex_sta->low_priority_tx +
		     coex_sta->low_priority_rx;

	if ((cnt_bt_pre > (cnt_bt_all + 50) ||
	    cnt_bt_all > (cnt_bt_pre + 50)) &&
	    coex_dm->bt_status == BT_8723D_2ANT_BT_STATUS_NON_CONNECTED_IDLE)
		is_run_coex = TRUE;

	cnt_bt_pre = cnt_bt_all;

	return is_run_coex;
}

static
void halbtc8723d2ant_monitor_wifi_ctr(IN struct btc_coexist *btcoexist)
{
	s32 wifi_rssi = 0;
	boolean wifi_busy = FALSE, wifi_under_b_mode = FALSE,
			wifi_scan = FALSE, wifi_connected = FALSE;
	boolean bt_idle = FALSE, wl_idle = FALSE, is_cck_deadlock = FALSE;
	static u8 cck_lock_counter = 0, wl_noisy_count0 = 0,
		  wl_noisy_count1 = 3, wl_noisy_count2 = 0;
	u32 total_cnt, reg_val1, reg_val2, cnt_cck;
	u32 cnt_crcok = 0, cnt_crcerr = 0;
	static u8 cnt = 0, cnt_cal = 0, cnt_ccklocking = 0;
	u8	h2c_parameter[1] = {0};
	struct	btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;


	/*send h2c to query WL FW dbg info	*/
	if (coex_dm->cur_ps_tdma_on) {
		h2c_parameter[0] = 0x8;
		btcoexist->btc_fill_h2c(btcoexist, 0x69, 1, h2c_parameter);
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_S4_WIFI_RSSI, &wifi_rssi);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_B_MODE,
			   &wifi_under_b_mode);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &wifi_scan);

	coex_sta->crc_ok_cck = btcoexist->btc_phydm_query_PHY_counter(
						btcoexist,
						PHYDM_INFO_CRC32_OK_CCK);
	coex_sta->crc_ok_11g = btcoexist->btc_phydm_query_PHY_counter(
						btcoexist,
						PHYDM_INFO_CRC32_OK_LEGACY);
	coex_sta->crc_ok_11n = btcoexist->btc_phydm_query_PHY_counter(
						btcoexist,
						PHYDM_INFO_CRC32_OK_HT);
	coex_sta->crc_ok_11n_vht = btcoexist->btc_phydm_query_PHY_counter(
						btcoexist,
						PHYDM_INFO_CRC32_OK_VHT);

	coex_sta->crc_err_cck = btcoexist->btc_phydm_query_PHY_counter(
						btcoexist, PHYDM_INFO_CRC32_ERROR_CCK);
	coex_sta->crc_err_11g =  btcoexist->btc_phydm_query_PHY_counter(
						btcoexist, PHYDM_INFO_CRC32_ERROR_LEGACY);
	coex_sta->crc_err_11n = btcoexist->btc_phydm_query_PHY_counter(
						btcoexist, PHYDM_INFO_CRC32_ERROR_HT);
	coex_sta->crc_err_11n_vht = btcoexist->btc_phydm_query_PHY_counter(
						btcoexist,
						PHYDM_INFO_CRC32_ERROR_VHT);

	cnt_crcok =  coex_sta->crc_ok_cck + coex_sta->crc_ok_11g
				+ coex_sta->crc_ok_11n
				+ coex_sta->crc_ok_11n_vht;

	cnt_crcerr =  coex_sta->crc_err_cck + coex_sta->crc_err_11g
				+ coex_sta->crc_err_11n
				+ coex_sta->crc_err_11n_vht;


	if ((wifi_busy) && (cnt_crcerr != 0)) {
		if (cnt_cal == 0)
			coex_sta->cnt_crcok_max_in_10s = 0;

		cnt_cal++;

		if (cnt_crcok > coex_sta->cnt_crcok_max_in_10s)
			coex_sta->cnt_crcok_max_in_10s = cnt_crcok;

		if (cnt_cal == 5)
			cnt_cal = 0;

		coex_sta->now_crc_ratio = cnt_crcok/cnt_crcerr;

		if (cnt == 0)
			coex_sta->acc_crc_ratio = coex_sta->now_crc_ratio;
		else
			coex_sta->acc_crc_ratio = (coex_sta->acc_crc_ratio * 7 +
				coex_sta->now_crc_ratio * 3)/10;

		if (cnt >= 10)
			cnt = 0;
		else
			cnt++;
	}

	/*	CCK lock identification	*/
	if (coex_sta->cck_lock)
		cnt_ccklocking++;
	else if (cnt_ccklocking != 0)
		cnt_ccklocking--;

	if (cnt_ccklocking >= 3) {
		cnt_ccklocking = 3;
		coex_sta->cck_lock_ever = TRUE;
	}

	/* WiFi environment noisy identification */
	cnt_cck = coex_sta->crc_ok_cck + coex_sta->crc_err_cck;

	if ((!wifi_busy) && (!coex_sta->cck_lock)) {
		if (cnt_cck > 250) {
			if (wl_noisy_count2 < 3)
				wl_noisy_count2++;

			if (wl_noisy_count2 == 3) {
				wl_noisy_count0 = 0;
				wl_noisy_count1 = 0;
			}

		} else if (cnt_cck < 50) {
			if (wl_noisy_count0 < 3)
				wl_noisy_count0++;

			if (wl_noisy_count0 == 3) {
				wl_noisy_count1 = 0;
				wl_noisy_count2 = 0;
			}

		} else {
			if (wl_noisy_count1 < 3)
				wl_noisy_count1++;

			if (wl_noisy_count1 == 3) {
				wl_noisy_count0 = 0;
				wl_noisy_count2 = 0;
			}
		}

		if (wl_noisy_count2 == 3)
			coex_sta->wl_noisy_level = 2;
		else if (wl_noisy_count1 == 3)
			coex_sta->wl_noisy_level = 1;
		else
			coex_sta->wl_noisy_level = 0;
	}
}

static
void halbtc8723d2ant_update_bt_link_info(IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean		bt_hs_on = FALSE, bt_busy = FALSE;
	u32		val = 0, wifi_link_status = 0, num_of_wifi_link = 0;
	static u8		pre_num_of_profile, cur_num_of_profile, cnt;
	static boolean	pre_ble_scan_en;

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
			   &wifi_link_status);

	num_of_wifi_link = wifi_link_status >> 16;

	if (coex_sta->is_ble_scan_en && !pre_ble_scan_en) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT ext info bit4 check, query BLE Scan type!!\n");
				BTC_TRACE(trace_buf);
		coex_sta->bt_ble_scan_type = btcoexist->btc_get_ble_scan_type_from_bt(btcoexist);

		if ((coex_sta->bt_ble_scan_type & 0x1) == 0x1)
			coex_sta->bt_ble_scan_para[0]  =
				btcoexist->btc_get_ble_scan_para_from_bt(btcoexist, 0x1);
		if ((coex_sta->bt_ble_scan_type & 0x2) == 0x2)
			coex_sta->bt_ble_scan_para[1]  =
				btcoexist->btc_get_ble_scan_para_from_bt(btcoexist, 0x2);
		if ((coex_sta->bt_ble_scan_type & 0x4) == 0x4)
			coex_sta->bt_ble_scan_para[2]  =
				btcoexist->btc_get_ble_scan_para_from_bt(btcoexist, 0x4);
	}

	pre_ble_scan_en = coex_sta->is_ble_scan_en;
	coex_sta->num_of_profile = 0;

	/* set link exist status */
	if (!(coex_sta->bt_info & BT_INFO_8723D_2ANT_B_CONNECTION)) {
		coex_sta->bt_link_exist = FALSE;
		coex_sta->pan_exist = FALSE;
		coex_sta->a2dp_exist = FALSE;
		coex_sta->hid_exist = FALSE;
		coex_sta->sco_exist = FALSE;
	} else {	/* connection exists */
		coex_sta->bt_link_exist = TRUE;
		if (coex_sta->bt_info & BT_INFO_8723D_2ANT_B_FTP) {
			coex_sta->pan_exist = TRUE;
			coex_sta->num_of_profile++;
		} else
			coex_sta->pan_exist = FALSE;

		if (coex_sta->bt_info & BT_INFO_8723D_2ANT_B_A2DP) {
			coex_sta->a2dp_exist = TRUE;
			coex_sta->num_of_profile++;
		} else
			coex_sta->a2dp_exist = FALSE;

		if (coex_sta->bt_info & BT_INFO_8723D_2ANT_B_HID) {
			coex_sta->hid_exist = TRUE;
			coex_sta->num_of_profile++;
		} else
			coex_sta->hid_exist = FALSE;

		if (coex_sta->bt_info & BT_INFO_8723D_2ANT_B_SCO_ESCO) {
			coex_sta->sco_exist = TRUE;
			coex_sta->num_of_profile++;
		} else
			coex_sta->sco_exist = FALSE;

	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);

	bt_link_info->bt_link_exist = coex_sta->bt_link_exist;
	bt_link_info->sco_exist = coex_sta->sco_exist;
	bt_link_info->a2dp_exist = coex_sta->a2dp_exist;
	bt_link_info->pan_exist = coex_sta->pan_exist;
	bt_link_info->hid_exist = coex_sta->hid_exist;
	bt_link_info->acl_busy = coex_sta->acl_busy;

	/* work around for HS mode. */
	if (bt_hs_on) {
		bt_link_info->pan_exist = TRUE;
		bt_link_info->bt_link_exist = TRUE;
	}

	/* check if Sco only */
	if (bt_link_info->sco_exist &&
	    !bt_link_info->a2dp_exist &&
	    !bt_link_info->pan_exist &&
	    !bt_link_info->hid_exist)
		bt_link_info->sco_only = TRUE;
	else
		bt_link_info->sco_only = FALSE;

	/* check if A2dp only */
	if (!bt_link_info->sco_exist &&
	    bt_link_info->a2dp_exist &&
	    !bt_link_info->pan_exist &&
	    !bt_link_info->hid_exist)
		bt_link_info->a2dp_only = TRUE;
	else
		bt_link_info->a2dp_only = FALSE;

	/* check if Pan only */
	if (!bt_link_info->sco_exist &&
	    !bt_link_info->a2dp_exist &&
	    bt_link_info->pan_exist &&
	    !bt_link_info->hid_exist)
		bt_link_info->pan_only = TRUE;
	else
		bt_link_info->pan_only = FALSE;

	/* check if Hid only */
	if (!bt_link_info->sco_exist &&
	    !bt_link_info->a2dp_exist &&
	    !bt_link_info->pan_exist &&
	    bt_link_info->hid_exist)
		bt_link_info->hid_only = TRUE;
	else
		bt_link_info->hid_only = FALSE;

	if (coex_sta->bt_info & BT_INFO_8723D_2ANT_B_INQ_PAGE) {
		coex_dm->bt_status = BT_8723D_2ANT_BT_STATUS_INQ_PAGE;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], BtInfoNotify(), BT Inq/page!!!\n");
	} else if (!(coex_sta->bt_info & BT_INFO_8723D_2ANT_B_CONNECTION)) {
		coex_dm->bt_status = BT_8723D_2ANT_BT_STATUS_NON_CONNECTED_IDLE;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], BtInfoNotify(), BT Non-Connected idle!!!\n");
	} else if (coex_sta->bt_info == BT_INFO_8723D_2ANT_B_CONNECTION) {
		/* connection exists but no busy */
		coex_dm->bt_status = BT_8723D_2ANT_BT_STATUS_CONNECTED_IDLE;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT Connected-idle!!!\n");
	} else if (((coex_sta->bt_info & BT_INFO_8723D_2ANT_B_SCO_ESCO) ||
		    (coex_sta->bt_info & BT_INFO_8723D_2ANT_B_SCO_BUSY)) &&
		   (coex_sta->bt_info & BT_INFO_8723D_2ANT_B_ACL_BUSY)) {
		coex_dm->bt_status = BT_8723D_2ANT_BT_STATUS_ACL_SCO_BUSY;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT ACL SCO busy!!!\n");
	} else if ((coex_sta->bt_info & BT_INFO_8723D_2ANT_B_SCO_ESCO) ||
		   (coex_sta->bt_info & BT_INFO_8723D_2ANT_B_SCO_BUSY)) {
		coex_dm->bt_status = BT_8723D_2ANT_BT_STATUS_SCO_BUSY;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT SCO busy!!!\n");
	} else if (coex_sta->bt_info & BT_INFO_8723D_2ANT_B_ACL_BUSY) {
		coex_dm->bt_status = BT_8723D_2ANT_BT_STATUS_ACL_BUSY;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT ACL busy!!!\n");
	} else {
		coex_dm->bt_status = BT_8723D_2ANT_BT_STATUS_MAX;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], BtInfoNotify(), BT Non-Defined state!!!\n");
	}

	BTC_TRACE(trace_buf);

	if ((BT_8723D_2ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) ||
	    (BT_8723D_2ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
	    (BT_8723D_2ANT_BT_STATUS_ACL_SCO_BUSY == coex_dm->bt_status))
		bt_busy = TRUE;
	else
		bt_busy = FALSE;

	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bt_busy);

	cur_num_of_profile = coex_sta->num_of_profile;

	if (cur_num_of_profile != pre_num_of_profile)
		cnt = 2;

	if (bt_link_info->a2dp_exist) {

		if (((coex_sta->bt_a2dp_vendor_id == 0) &&
			(coex_sta->bt_a2dp_device_name == 0)) ||
			(cur_num_of_profile != pre_num_of_profile)) {

			btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_DEVICE_INFO, &val);

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], BtInfoNotify(), get BT DEVICE_INFO = %x\n",
				    val);
			BTC_TRACE(trace_buf);

			coex_sta->bt_a2dp_vendor_id = (u8)(val & 0xff);
			coex_sta->bt_a2dp_device_name = (val & 0xffffff00) >> 8;
		}

		if (((coex_sta->legacy_forbidden_slot == 0) &&
			(coex_sta->le_forbidden_slot == 0)) ||
			(cur_num_of_profile != pre_num_of_profile) ||
			(cnt > 0)) {

			if (cnt > 0)
				cnt--;

			btcoexist->btc_get(btcoexist,
					   BTC_GET_U4_BT_FORBIDDEN_SLOT_VAL,
					   &val);

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], BtInfoNotify(), get BT FORBIDDEN_SLOT_VAL = %x, cnt = %d\n",
				    val, cnt);
			BTC_TRACE(trace_buf);

			coex_sta->legacy_forbidden_slot = (u16)(val & 0xffff);
			coex_sta->le_forbidden_slot = (u16)((val & 0xffff0000) >> 16);
		}
	}

	pre_num_of_profile = coex_sta->num_of_profile;

	if (btcoexist->manual_control || btcoexist->stop_coex_dm)
		return;

	if (num_of_wifi_link == 0 ||
	    coex_dm->bt_status == BT_8723D_2ANT_BT_STATUS_NON_CONNECTED_IDLE) {
		halbtc8723d2ant_low_penalty_ra(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_limited_tx(btcoexist, NORMAL_EXEC, FALSE,
					   FALSE);
		halbtc8723d2ant_limited_rx(btcoexist, NM_EXCU, FALSE, TRUE, 64);
	} else if (wifi_link_status & WIFI_P2P_GO_CONNECTED ||
		    wifi_link_status & WIFI_P2P_GC_CONNECTED) {
		halbtc8723d2ant_low_penalty_ra(btcoexist, NORMAL_EXEC, TRUE);
		halbtc8723d2ant_limited_tx(btcoexist, NM_EXCU, TRUE, TRUE);
		halbtc8723d2ant_limited_rx(btcoexist, NM_EXCU, FALSE, TRUE, 16);
	} else {
		halbtc8723d2ant_low_penalty_ra(btcoexist, NORMAL_EXEC, TRUE);

		if (bt_link_info->hid_exist || coex_sta->hid_pair_cnt > 0 ||
		    bt_link_info->sco_exist) {
			halbtc8723d2ant_limited_tx(btcoexist, NM_EXCU, TRUE,
						   TRUE);
			halbtc8723d2ant_limited_rx(btcoexist, NM_EXCU, FALSE,
						   TRUE, 16);
		} else {
			halbtc8723d2ant_limited_tx(btcoexist, NM_EXCU, TRUE,
						   FALSE);
			halbtc8723d2ant_limited_rx(btcoexist, NM_EXCU, FALSE,
						   TRUE, 64);
		}
	}
}

static
void halbtc8723d2ant_update_wifi_channel_info(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	u8			h2c_parameter[3] = {0};
	u32			wifi_bw;
	u8			wifi_central_chnl;

	/* only 2.4G we need to inform bt the chnl mask */
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_CENTRAL_CHNL,
			   &wifi_central_chnl);
	if (type == BTC_MEDIA_CONNECT && wifi_central_chnl <= 14) {
		/* enable BT AFH skip WL channel for 8723d
		 * because BT Rx LO interference
		 * h2c_parameter[0] = 0x0;
		 */
		h2c_parameter[0] = 0x1;
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

	btcoexist->btc_fill_h2c(btcoexist, 0x66, 3, h2c_parameter);

}

static
void halbtc8723d2ant_write_score_board(
	IN	struct  btc_coexist		*btcoexist,
	IN	u16				bitpos,
	IN	boolean		state
)
{

	static u16 originalval = 0x8002, preval = 0x0;

	if (state)
		originalval = originalval | bitpos;
	else
		originalval = originalval & (~bitpos);

	coex_sta->score_board_WB = originalval;

	if (originalval != preval) {
		preval = originalval;
		btcoexist->btc_write_2byte(btcoexist, 0xaa, originalval);
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], halbtc8723d2ant_write_score_board: return for nochange\n");
		BTC_TRACE(trace_buf);
	}
}

static
void halbtc8723d2ant_read_score_board(
	IN	struct  btc_coexist		*btcoexist,
	IN   u16				*score_board_val
)
{
	*score_board_val = (btcoexist->btc_read_2byte(btcoexist, 0xaa)) &
				0x7fff;
}

static
void halbtc8723d2ant_post_state_to_bt(
	IN struct  btc_coexist	*btcoexist,
	IN u16		type,
	IN boolean	state
)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], %s: type = %d, state =%d\n",
		    __func__, type, state);
	BTC_TRACE(trace_buf);

	halbtc8723d2ant_write_score_board(btcoexist, (u16) type, state);
}

static
void halbtc8723d2ant_adjust_wl_tx_power(IN struct btc_coexist *btcoexist,
			      IN boolean force_exec, IN u8 fw_dac_swing_lvl)
{

	coex_dm->cur_fw_dac_swing_lvl = fw_dac_swing_lvl;

	if (!force_exec) {
		if (coex_dm->pre_fw_dac_swing_lvl ==
		    coex_dm->cur_fw_dac_swing_lvl)
			return;
	}

	btcoexist->btc_write_1byte(btcoexist, 0x883, fw_dac_swing_lvl);

	coex_dm->pre_fw_dac_swing_lvl = coex_dm->cur_fw_dac_swing_lvl;
}

static
void halbtc8723d2ant_adjust_bt_tx_power(IN struct btc_coexist *btcoexist,
				IN boolean force_exec, IN u8 dec_bt_pwr_lvl)
{
	u8	h2c_parameter[1] = {0};

	coex_dm->cur_bt_dec_pwr_lvl = dec_bt_pwr_lvl;

	if (!force_exec) {
		if (coex_dm->pre_bt_dec_pwr_lvl == coex_dm->cur_bt_dec_pwr_lvl)
			return;
	}

	h2c_parameter[0] = 0 - dec_bt_pwr_lvl;

	btcoexist->btc_fill_h2c(btcoexist, 0x62, 1, h2c_parameter);

	coex_dm->pre_bt_dec_pwr_lvl = coex_dm->cur_bt_dec_pwr_lvl;
}

static
void halbtc8723d2ant_adjust_wl_rx_gain(IN struct btc_coexist *btcoexist,
			       IN boolean force_exec, IN boolean agc_table_en)
{
	u32 rx_gain_value_enable[] = {0xec120101, 0xeb130101, 0xce140101,
				      0xcd150101, 0xcc160101, 0xcb170101,
				      0xca180101, 0x8d190101, 0x8c1a0101,
				      0x8b1b0101, 0x4f1c0101, 0x4e1d0101,
				      0x4d1e0101, 0x4c1f0101, 0x0e200101,
				      0x0d210101, 0x0c220101, 0x0b230101,
				      0xcf240001, 0xce250001, 0xcd260001,
				      0xcc270001, 0x8f280001, 0xffffffff};
	u32 rx_gain_value_disable[] = {0xec120101, 0xeb130101, 0xea140101,
				       0xe9150101, 0xe8160101, 0xe7170101,
				       0xe6180101, 0xe5190101, 0xe41a0101,
				       0xe31b0101, 0xe21c0101, 0xe11d0101,
				       0xe01e0101, 0x861f0101, 0x85200101,
				       0x84210101, 0x83220101, 0x82230101,
				       0x81240101, 0x80250101, 0x44260101,
				       0x43270101, 0x42280101, 0xffffffff};

	u8 i;

	coex_dm->cur_agc_table_en = agc_table_en;

	if (!force_exec) {
		if (coex_dm->pre_agc_table_en == coex_dm->cur_agc_table_en)
			return;
	}

	if (agc_table_en) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BB Agc Table On!\n");
		BTC_TRACE(trace_buf);

		for (i = 0; i < ARRAY_SIZE(rx_gain_value_enable); i++) {
			if (rx_gain_value_enable[i] == 0xffffffff)
				break;

			btcoexist->btc_write_4byte(btcoexist,
				0xc78, rx_gain_value_enable[i]);
		}

	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BB Agc Table Off!\n");
		BTC_TRACE(trace_buf);

		for (i = 0; i < ARRAY_SIZE(rx_gain_value_disable); i++) {
			if (rx_gain_value_disable[i] == 0xffffffff)
				break;

			btcoexist->btc_write_4byte(btcoexist,
				0xc78, rx_gain_value_disable[i]);
		}
	}

	coex_dm->pre_agc_table_en = coex_dm->cur_agc_table_en;
}

static
void halbtc8723d2ant_adjust_bt_rx_gain(IN struct btc_coexist *btcoexist,
			       IN boolean force_exec, IN boolean rx_gain_en)
{
	u8 lna_lvl = 1;

	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_LNA_CONSTRAIN_LEVEL,
			   &lna_lvl);

	/* use scoreboard[4] to notify BT Rx gain table change   */
	halbtc8723d2ant_post_state_to_bt(btcoexist,
		BT_8723D_2ANT_SCOREBOARD_RXGAIN, rx_gain_en);
}

static
boolean halbtc8723d2ant_is_wifibt_status_changed(IN struct btc_coexist
		*btcoexist)
{
	static boolean	pre_wifi_busy = FALSE, pre_under_4way = FALSE,
		pre_bt_hs_on = FALSE, pre_bt_off = FALSE,
		pre_bt_slave = FALSE, pre_hid_low_pri_tx_overhead = FALSE,
		pre_wifi_under_lps = FALSE, pre_bt_setup_link = FALSE,
		pre_cck_lock = FALSE, pre_cck_lock_warn = FALSE;
	static u8 pre_hid_busy_num = 0, pre_wl_noisy_level = 0;
	boolean wifi_busy = FALSE, under_4way = FALSE, bt_hs_on = FALSE;
	boolean wifi_connected = FALSE;
	struct	btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS,
			   &under_4way);

	if (coex_sta->bt_disabled != pre_bt_off) {
		pre_bt_off = coex_sta->bt_disabled;

		if (coex_sta->bt_disabled)
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], BT is disabled !!\n");
		else
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], BT is enabled !!\n");

		BTC_TRACE(trace_buf);

		coex_sta->bt_coex_supported_feature = 0;
		coex_sta->bt_coex_supported_version = 0;
		coex_sta->bt_ble_scan_type = 0;
		coex_sta->bt_ble_scan_para[0] = 0;
		coex_sta->bt_ble_scan_para[1] = 0;
		coex_sta->bt_ble_scan_para[2] = 0;
		coex_sta->bt_reg_vendor_ac = 0xffff;
		coex_sta->bt_reg_vendor_ae = 0xffff;
		coex_sta->legacy_forbidden_slot = 0;
		coex_sta->le_forbidden_slot = 0;
		coex_sta->bt_a2dp_vendor_id = 0;
		coex_sta->bt_a2dp_device_name = 0;
		return TRUE;
	}

	if (wifi_connected) {
		if (wifi_busy != pre_wifi_busy) {
			pre_wifi_busy = wifi_busy;

			if (wifi_busy)
				halbtc8723d2ant_post_state_to_bt(btcoexist,
						BT_8723D_2ANT_SCOREBOARD_UNDERTEST, TRUE);
			else
				halbtc8723d2ant_post_state_to_bt(btcoexist,
						BT_8723D_2ANT_SCOREBOARD_UNDERTEST, FALSE);
			return TRUE;
		}
		if (under_4way != pre_under_4way) {
			pre_under_4way = under_4way;
			return TRUE;
		}
		if (bt_hs_on != pre_bt_hs_on) {
			pre_bt_hs_on = bt_hs_on;
			return TRUE;
		}
		if (coex_sta->wl_noisy_level != pre_wl_noisy_level) {
			pre_wl_noisy_level = coex_sta->wl_noisy_level;
			return TRUE;
		}
		if (coex_sta->under_lps != pre_wifi_under_lps) {
			pre_wifi_under_lps = coex_sta->under_lps;
			if (coex_sta->under_lps == TRUE)
				return TRUE;
		}
		if (coex_sta->cck_lock != pre_cck_lock) {
			pre_cck_lock = coex_sta->cck_lock;
			return TRUE;
		}
		if (coex_sta->cck_lock_warn != pre_cck_lock_warn) {
			pre_cck_lock_warn = coex_sta->cck_lock_warn;
			return TRUE;
		}
	}

	if (!coex_sta->bt_disabled) {
		if (coex_sta->hid_busy_num != pre_hid_busy_num) {
			pre_hid_busy_num = coex_sta->hid_busy_num;
			return TRUE;
		}

		if (bt_link_info->slave_role != pre_bt_slave) {
			pre_bt_slave = bt_link_info->slave_role;
			return TRUE;
		}

		if (pre_hid_low_pri_tx_overhead != coex_sta->is_hid_low_pri_tx_overhead) {
			pre_hid_low_pri_tx_overhead = coex_sta->is_hid_low_pri_tx_overhead;
			return TRUE;
		}

		if (pre_bt_setup_link != coex_sta->is_setup_link) {
			pre_bt_setup_link = coex_sta->is_setup_link;
			return TRUE;
		}
	}

	return FALSE;
}

static
void halbtc8723d2ant_monitor_bt_enable_disable(IN struct btc_coexist *btcoexist)
{
	static u32		bt_disable_cnt = 0;
	boolean			bt_active = TRUE, bt_disabled = FALSE;
	u16			u16tmp;

	/* This function check if bt is disabled
	 * Read BT on/off status from scoreboard[1],
	 * enable this only if BT patch support this feature
	 */
	halbtc8723d2ant_read_score_board(btcoexist, &u16tmp);
	bt_active = u16tmp & BIT(1);

	if (bt_active) {
		bt_disable_cnt = 0;
		bt_disabled = FALSE;
		btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_DISABLE,
				   &bt_disabled);
	} else {

		bt_disable_cnt++;
		if (bt_disable_cnt >= 2) {
			bt_disabled = TRUE;
			bt_disable_cnt = 2;
		}

		btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_DISABLE,
				   &bt_disabled);
	}

	if (coex_sta->bt_disabled != bt_disabled) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is from %s to %s!!\n",
			    (coex_sta->bt_disabled ? "disabled" : "enabled"),
			    (bt_disabled ? "disabled" : "enabled"));
		BTC_TRACE(trace_buf);
		coex_sta->bt_disabled = bt_disabled;
	}

}

static
void halbtc8723d2ant_enable_gnt_to_gpio(IN struct btc_coexist *btcoexist,
					boolean isenable)
{
#if BT_8723D_2ANT_COEX_DBG
	if (isenable) {
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x73, 0x8, 0x1);

		/* enable GNT_BT to GPIO debug */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4e, 0x40, 0x0);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x1, 0x0);

		/* 0x48[20] = 0  for GPIO14 =  GNT_WL*/
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4a, 0x10, 0x0);
		/* 0x40[17] = 0  for GPIO14 =  GNT_WL*/
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x42, 0x02, 0x0);

		/* 0x66[9] = 0   for GPIO15 =  GNT_BT*/
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x02, 0x0);
		/* 0x66[7] = 0
		for GPIO15 =  GNT_BT*/
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x66, 0x80, 0x0);
		/* 0x8[8] = 0    for GPIO15 =  GNT_BT*/
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x9, 0x1, 0x0);

		/* BT Vendor Reg 0x76[0] = 0  for GPIO15 =  GNT_BT, this is not set here*/
	} else {
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x73, 0x8, 0x0);

		/* Disable GNT_BT debug to GPIO, and enable chip_wakeup_host */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4e, 0x40, 0x1);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x1, 0x1);

		/* 0x48[20] = 0  for GPIO14 =  GNT_WL*/
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4a, 0x10, 0x1);
	}

#endif
}

static
u32 halbtc8723d2ant_ltecoex_indirect_read_reg(IN struct btc_coexist *btcoexist,
		IN u16 reg_addr)
{
	u32 j = 0, delay_count = 0;

	/* wait for ready bit before access 0x7c0/0x7c4 */
	while (1) {
		if ((btcoexist->btc_read_1byte(btcoexist, 0x7c3)&BIT(5)) == 0) {
			delay_ms(10);
			delay_count++;
			if (delay_count >= 10) {
				delay_count = 0;
				break;
			}
		} else
			break;
	}

	btcoexist->btc_write_4byte(btcoexist, 0x7c0, 0x800F0000 | reg_addr);

	return btcoexist->btc_read_4byte(btcoexist,
					 0x7c8);  /* get read data */

}

static
void halbtc8723d2ant_ltecoex_indirect_write_reg(IN struct btc_coexist
		*btcoexist,
		IN u16 reg_addr, IN u32 bit_mask, IN u32 reg_value)
{
	u32 val, i = 0, j = 0, bitpos = 0, delay_count = 0;


	if (bit_mask == 0x0)
		return;
	if (bit_mask == 0xffffffff) {
		/* wait for ready bit before access 0x7c0/0x7c4 */
		while (1) {
			if ((btcoexist->btc_read_1byte(btcoexist, 0x7c3)&BIT(5)) == 0) {
				delay_ms(10);
				delay_count++;
				if (delay_count >= 10) {
					delay_count = 0;
					break;
				}
			} else
				break;
		}

		btcoexist->btc_write_4byte(btcoexist, 0x7c4,
					   reg_value); /* put write data */

		btcoexist->btc_write_4byte(btcoexist, 0x7c0,
					   0xc00F0000 | reg_addr);
	} else {
		for (i = 0; i <= 31; i++) {
			if (((bit_mask >> i) & 0x1) == 0x1) {
				bitpos = i;
				break;
			}
		}

		/* read back register value before write */
		val = halbtc8723d2ant_ltecoex_indirect_read_reg(btcoexist,
				reg_addr);
		val = (val & (~bit_mask)) | (reg_value << bitpos);

		/* wait for ready bit before access 0x7c0/0x7c4	*/
		while (1) {
			if ((btcoexist->btc_read_1byte(btcoexist, 0x7c3)&BIT(5)) == 0) {
				delay_ms(10);
				delay_count++;
				if (delay_count >= 10) {
					delay_count = 0;
					break;
				}
			} else
				break;
		}

		btcoexist->btc_write_4byte(btcoexist, 0x7c4,
					   val); /* put write data */

		btcoexist->btc_write_4byte(btcoexist, 0x7c0,
					   0xc00F0000 | reg_addr);

	}

}

static
void halbtc8723d2ant_ltecoex_enable(IN struct btc_coexist *btcoexist,
				    IN boolean enable)
{
	u8 val;

	val = (enable) ? 1 : 0;
	halbtc8723d2ant_ltecoex_indirect_write_reg(btcoexist, 0x38, 0x80,
			val);  /* 0x38[7] */

}

static
void halbtc8723d2ant_coex_ctrl_owner(IN struct btc_coexist *btcoexist,
				     IN boolean wifi_control)
{
	u8 val;

	val = (wifi_control) ? 1 : 0;
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x73, 0x4,
					   val); /* 0x70[26] */

}

static
void halbtc8723d2ant_ltecoex_set_gnt_bt(IN struct btc_coexist *btcoexist,
			IN u8 control_block, IN boolean sw_control, IN u8 state)
{
	u32 val = 0, val_orig = 0;

	if (!sw_control)
		val = 0x0;
	else if (state & 0x1)
		val = 0x3;
	else
		val = 0x1;

	val_orig = halbtc8723d2ant_ltecoex_indirect_read_reg(btcoexist,
				0x38);

	switch (control_block) {
	case BT_8723D_2ANT_GNT_BLOCK_RFC_BB:
	default:
		val = ((val << 14) | (val << 10)) | (val_orig & 0xffff33ff);
		break;
	case BT_8723D_2ANT_GNT_BLOCK_RFC:
		val = (val << 14) | (val_orig & 0xffff3fff);
		break;
	case BT_8723D_2ANT_GNT_BLOCK_BB:
		val = (val << 10) | (val_orig & 0xfffff3ff);
		break;
	}

	halbtc8723d2ant_ltecoex_indirect_write_reg(btcoexist,
				0x38, 0xffffffff, val);
}

static
void halbtc8723d2ant_ltecoex_set_gnt_wl(IN struct btc_coexist *btcoexist,
			IN u8 control_block, IN boolean sw_control, IN u8 state)
{
	u32 val = 0, val_orig = 0;

	if (!sw_control)
		val = 0x0;
	else if (state & 0x1)
		val = 0x3;
	else
		val = 0x1;

	val_orig = halbtc8723d2ant_ltecoex_indirect_read_reg(btcoexist,
				0x38);

	switch (control_block) {
	case BT_8723D_2ANT_GNT_BLOCK_RFC_BB:
	default:
		val = ((val << 12) | (val << 8)) | (val_orig & 0xffffccff);
		break;
	case BT_8723D_2ANT_GNT_BLOCK_RFC:
		val = (val << 12) | (val_orig & 0xffffcfff);
		break;
	case BT_8723D_2ANT_GNT_BLOCK_BB:
		val = (val << 8) | (val_orig & 0xfffffcff);
		break;
	}

	halbtc8723d2ant_ltecoex_indirect_write_reg(btcoexist,
				0x38, 0xffffffff, val);
}

static
void halbtc8723d2ant_ltecoex_set_coex_table(IN struct btc_coexist *btcoexist,
		IN u8 table_type, IN u16 table_content)
{
	u16 reg_addr = 0x0000;

	switch (table_type) {
	case BT_8723D_2ANT_CTT_WL_VS_LTE:
		reg_addr = 0xa0;
		break;
	case BT_8723D_2ANT_CTT_BT_VS_LTE:
		reg_addr = 0xa4;
		break;
	}

	if (reg_addr != 0x0000)
		halbtc8723d2ant_ltecoex_indirect_write_reg(btcoexist, reg_addr,
			0xffff, table_content); /* 0xa0[15:0] or 0xa4[15:0] */


}

static
void halbtc8723d2ant_coex_table(IN struct btc_coexist *btcoexist,
				IN boolean force_exec, IN u32 val0x6c0,
				IN u32 val0x6c4, IN u32 val0x6c8,
				IN u8 val0x6cc)
{
	if (!force_exec) {
		if (val0x6c0 == coex_dm->cur_val0x6c0 &&
		    val0x6c4 == coex_dm->cur_val0x6c4 &&
		    val0x6c8 == coex_dm->cur_val0x6c8 &&
		    val0x6cc == coex_dm->cur_val0x6cc)
			return;
	}

	btcoexist->btc_write_4byte(btcoexist, 0x6c0, val0x6c0);
	btcoexist->btc_write_4byte(btcoexist, 0x6c4, val0x6c4);
	btcoexist->btc_write_4byte(btcoexist, 0x6c8, val0x6c8);
	btcoexist->btc_write_1byte(btcoexist, 0x6cc, val0x6cc);

	coex_dm->cur_val0x6c0 = val0x6c0;
	coex_dm->cur_val0x6c4 = val0x6c4;
	coex_dm->cur_val0x6c8 = val0x6c8;
	coex_dm->cur_val0x6cc = val0x6cc;

}

static
void halbtc8723d2ant_coex_table_with_type(IN struct btc_coexist *btcoexist,
		IN boolean force_exec, IN u8 type)
{
	u32	break_table;
	u8	select_table;

	coex_sta->coex_table_type = type;

	if (coex_sta->concurrent_rx_mode_on == TRUE) {
		break_table = 0xf0ffffff;  /* set WL hi-pri can break BT */
		select_table =
			0xb;		/* set Tx response = Hi-Pri (ex: Transmitting ACK,BA,CTS) */
	} else {
		break_table = 0xffffff;
		select_table = 0x3;
	}

	switch (type) {
	case 0:
		halbtc8723d2ant_coex_table(btcoexist, force_exec,
			   0xffffffff, 0xffffffff, break_table, select_table);
		break;
	case 1:
		halbtc8723d2ant_coex_table(btcoexist, force_exec,
			   0x55555555, 0xfafafafa, break_table, select_table);
		break;
	case 2:
		halbtc8723d2ant_coex_table(btcoexist, force_exec,
			   0x5a5a5a5a, 0x5a5a5a5a, break_table, select_table);
		break;
	case 3:
		halbtc8723d2ant_coex_table(btcoexist, force_exec,
			   0x55555555, 0x5a5a5a5a, break_table, select_table);
		break;
	case 4:
		halbtc8723d2ant_coex_table(btcoexist, force_exec,
			   0xffff55ff, 0xfafafafa, break_table, select_table);
		break;
	case 5:
		halbtc8723d2ant_coex_table(btcoexist, force_exec,
			   0x55555555, 0x55555555, break_table, select_table);
		break;
	case 6:
		halbtc8723d2ant_coex_table(btcoexist, force_exec,
			   0xaaffffaa, 0xfafafafa, break_table, select_table);
		break;
	case 7:
		halbtc8723d2ant_coex_table(btcoexist, force_exec,
			   0xaaffffaa, 0xfafafafa, break_table, select_table);
		break;
	case 8:
		halbtc8723d2ant_coex_table(btcoexist, force_exec,
			   0xffff55ff, 0xfafafafa, break_table, select_table);
		break;
	case 9:
		halbtc8723d2ant_coex_table(btcoexist, force_exec,
			   0x5a5a5a5a, 0xaaaa5aaa, break_table, select_table);
		break;
	default:
		break;
	}
}

static
void halbtc8723d2ant_set_fw_ignore_wlan_act(IN struct btc_coexist *btcoexist,
		IN boolean enable)
{
	u8			h2c_parameter[1] = {0};

	if (enable) {
		h2c_parameter[0] |= BIT(0);		/* function enable */
	}

	btcoexist->btc_fill_h2c(btcoexist, 0x63, 1, h2c_parameter);
}

static
void halbtc8723d2ant_ignore_wlan_act(IN struct btc_coexist *btcoexist,
				     IN boolean force_exec, IN boolean enable)
{
	coex_dm->cur_ignore_wlan_act = enable;

	if (!force_exec) {
		if (coex_dm->pre_ignore_wlan_act ==
		    coex_dm->cur_ignore_wlan_act)
			return;
	}
	halbtc8723d2ant_set_fw_ignore_wlan_act(btcoexist, enable);

	coex_dm->pre_ignore_wlan_act = coex_dm->cur_ignore_wlan_act;
}

static
void halbtc8723d2ant_set_lps_rpwm(IN struct btc_coexist *btcoexist,
				  IN u8 lps_val, IN u8 rpwm_val)
{
	u8	lps = lps_val;
	u8	rpwm = rpwm_val;

	btcoexist->btc_set(btcoexist, BTC_SET_U1_LPS_VAL, &lps);
	btcoexist->btc_set(btcoexist, BTC_SET_U1_RPWM_VAL, &rpwm);
}

static
void halbtc8723d2ant_lps_rpwm(IN struct btc_coexist *btcoexist,
		      IN boolean force_exec, IN u8 lps_val, IN u8 rpwm_val)
{
	coex_dm->cur_lps = lps_val;
	coex_dm->cur_rpwm = rpwm_val;

	if (!force_exec) {
		if ((coex_dm->pre_lps == coex_dm->cur_lps) &&
		    (coex_dm->pre_rpwm == coex_dm->cur_rpwm))
			return;
	}
	halbtc8723d2ant_set_lps_rpwm(btcoexist, lps_val, rpwm_val);

	coex_dm->pre_lps = coex_dm->cur_lps;
	coex_dm->pre_rpwm = coex_dm->cur_rpwm;
}

static
void halbtc8723d2ant_ps_tdma_check_for_power_save_state(
	IN struct btc_coexist *btcoexist, IN boolean new_ps_state)
{
	u8	lps_mode = 0x0;
	u8	h2c_parameter[5] = {0, 0, 0, 0x40, 0};

	btcoexist->btc_get(btcoexist, BTC_GET_U1_LPS_MODE, &lps_mode);

	if (lps_mode) {	/* already under LPS state */
		if (new_ps_state) {
			/* keep state under LPS, do nothing. */
		} else {
			/* will leave LPS state, turn off psTdma first */
			btcoexist->btc_fill_h2c(btcoexist, 0x60, 5,
						h2c_parameter);
		}
	} else {					/* NO PS state */
		if (new_ps_state) {
			/* will enter LPS state, turn off psTdma first */
			btcoexist->btc_fill_h2c(btcoexist, 0x60, 5,
						h2c_parameter);
		} else {
			/* keep state under NO PS state, do nothing. */
		}
	}
}

static
void halbtc8723d2ant_power_save_state(IN struct btc_coexist *btcoexist,
			      IN u8 ps_type, IN u8 lps_val, IN u8 rpwm_val)
{
	boolean		low_pwr_disable = FALSE;

	switch (ps_type) {
	case BTC_PS_WIFI_NATIVE:
		coex_sta->force_lps_ctrl = FALSE;
		/* recover to original 32k low power setting */
		low_pwr_disable = FALSE;
		/* btcoexist->btc_set(btcoexist,
				   BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable); */
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_PRE_NORMAL_LPS,
				   NULL);
		break;
	case BTC_PS_LPS_ON:
		coex_sta->force_lps_ctrl = TRUE;
		halbtc8723d2ant_ps_tdma_check_for_power_save_state(
			btcoexist, TRUE);
		halbtc8723d2ant_lps_rpwm(btcoexist, NORMAL_EXEC,
					 lps_val, rpwm_val);
		/* when coex force to enter LPS, do not enter 32k low power. */
		low_pwr_disable = TRUE;
		btcoexist->btc_set(btcoexist,
				   BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);
		/* power save must executed before psTdma.			 */
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_ENTER_LPS,
				   NULL);
		break;
	case BTC_PS_LPS_OFF:
		coex_sta->force_lps_ctrl = TRUE;
		halbtc8723d2ant_ps_tdma_check_for_power_save_state(
			btcoexist, FALSE);
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS,
				   NULL);
		break;
	default:
		break;
	}
}

static
void halbtc8723d2ant_set_fw_pstdma(IN struct btc_coexist *btcoexist,
	   IN u8 byte1, IN u8 byte2, IN u8 byte3, IN u8 byte4, IN u8 byte5)
{
	u8			h2c_parameter[5] = {0};
	u8			real_byte1 = byte1, real_byte5 = byte5;
	boolean			ap_enable = FALSE;
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	u8		ps_type = BTC_PS_WIFI_NATIVE;

	if (byte5 & BIT(2))
		coex_sta->is_tdma_btautoslot = TRUE;
	else
		coex_sta->is_tdma_btautoslot = FALSE;

	/* release bt-auto slot for auto-slot hang is detected!! */
	if (coex_sta->is_tdma_btautoslot)
		if ((coex_sta->is_tdma_btautoslot_hang) ||
			(bt_link_info->slave_role))
			byte5 = byte5 & 0xfb;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE,
			   &ap_enable);

	if ((ap_enable) && (byte1 & BIT(4) && !(byte1 & BIT(5)))) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], FW for AP mode\n");
		BTC_TRACE(trace_buf);
		real_byte1 &= ~BIT(4);
		real_byte1 |= BIT(5);

		real_byte5 |= BIT(5);
		real_byte5 &= ~BIT(6);

		ps_type = BTC_PS_WIFI_NATIVE;
		halbtc8723d2ant_power_save_state(btcoexist,
					ps_type, 0x0,
					0x0);
	} else if (byte1 & BIT(4) && !(byte1 & BIT(5))) {

		ps_type = BTC_PS_LPS_ON;
		halbtc8723d2ant_power_save_state(
			btcoexist, ps_type, 0x50,
			0x4);
	} else {
		ps_type = BTC_PS_WIFI_NATIVE;
		halbtc8723d2ant_power_save_state(btcoexist, ps_type,
						 0x0,
						 0x0);
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

	btcoexist->btc_fill_h2c(btcoexist, 0x60, 5, h2c_parameter);

	if (ps_type == BTC_PS_WIFI_NATIVE)
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_POST_NORMAL_LPS, NULL);
}

static
void halbtc8723d2ant_ps_tdma(IN struct btc_coexist *btcoexist,
		     IN boolean force_exec, IN boolean turn_on, IN u8 tcase)
{

	static u8 tdma_byte4_modify, pre_tdma_byte4_modify;
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	u8 type;
	
	btcoexist->btc_set_atomic(btcoexist, &coex_dm->setting_tdma, TRUE);

	/* tcase: bit0~7 --> tdma case index
	 *        bit8   --> for 4-slot (50ms) mode
	 */
	if (tcase &  TDMA_4SLOT)/* 4-slot (50ms) mode */
		halbtc8723d2ant_set_tdma_timer_base(btcoexist, 3);
	else
		halbtc8723d2ant_set_tdma_timer_base(btcoexist, 0);
	
	type = tcase & 0xff;
	
	/* 0x778 = 0x1 at wifi slot (no blocking BT Low-Pri pkts) */
	if ((bt_link_info->slave_role) && (bt_link_info->a2dp_exist))
		tdma_byte4_modify = 0x1;
	else
		tdma_byte4_modify = 0x0;

	if (pre_tdma_byte4_modify != tdma_byte4_modify) {
		force_exec = TRUE;
		pre_tdma_byte4_modify = tdma_byte4_modify;
	}

	if (!force_exec) {
		if (turn_on == coex_dm->cur_ps_tdma_on &&
		    type == coex_dm->cur_ps_tdma) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Skip TDMA because no change TDMA(%s, %d)\n",
				    (coex_dm->cur_ps_tdma_on ? "on" : "off"),
				    coex_dm->cur_ps_tdma);
			BTC_TRACE(trace_buf);

			btcoexist->btc_set_atomic(btcoexist,
						  &coex_dm->setting_tdma,
						  FALSE);
			return;
		}
	}

	if (turn_on) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], ********** TDMA(on, %d)\n", type);
		BTC_TRACE(trace_buf);
		
		if (coex_sta->a2dp_exist && coex_sta->bt_inq_page_remain)
			halbtc8723d2ant_post_state_to_bt(btcoexist,
						 BT_8723D_2ANT_SCOREBOARD_TDMA,
						 FALSE);
		else
			halbtc8723d2ant_post_state_to_bt(btcoexist,
						 BT_8723D_2ANT_SCOREBOARD_TDMA,
						 TRUE);

		/* enable TBTT interrupt */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x550, 0x8, 0x1);
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], ********** TDMA(off, %d)\n", type);
		BTC_TRACE(trace_buf);

		halbtc8723d2ant_post_state_to_bt(btcoexist,
						 BT_8723D_2ANT_SCOREBOARD_TDMA,
						 FALSE);
	}


	if (turn_on) {
		switch (type) {
		case 1:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x10, 0x03, 0x91, 0x54 |
						      tdma_byte4_modify);
			break;
		case 2:
		default:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x35, 0x03, 0x11, 0x11 |
						      tdma_byte4_modify);
			break;
		case 3:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x3a, 0x3, 0x91, 0x10 |
						      tdma_byte4_modify);
			break;
		case 4:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x21, 0x3, 0x91, 0x10 |
						      tdma_byte4_modify);
			break;
		case 5:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x25, 0x3, 0x91, 0x10 |
						      tdma_byte4_modify);
			break;
		case 6:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x10, 0x3, 0x91, 0x10 |
						      tdma_byte4_modify);
			break;
		case 7:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x20, 0x3, 0x91, 0x10 |
						      tdma_byte4_modify);
			break;
		case 8:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x15, 0x03, 0x11, 0x11);
			break;
		case 10:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x30, 0x03, 0x11, 0x10);
			break;
		case 11:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x35, 0x03, 0x11, 0x10 |
						      tdma_byte4_modify);
			break;
		case 12:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x35, 0x03, 0x11, 0x11);
			break;
		case 13:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x1c, 0x03, 0x11, 0x10 |
						      tdma_byte4_modify);
			break;
		case 14:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x20, 0x03, 0x11, 0x11);
			break;
		case 15:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x10, 0x03, 0x11, 0x14);
			break;
		case 16:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x10, 0x03, 0x11, 0x15);
			break;
		case 17:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x10, 0x07, 0x11, 0x14);
			break;
		case 21:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x30, 0x03, 0x11, 0x10);
			break;
		case 22:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x25, 0x03, 0x11, 0x10);
			break;
		case 23:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x10, 0x03, 0x11, 0x10);
			break;
		case 51:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x10, 0x03, 0x91, 0x10 |
						      tdma_byte4_modify);
			break;
		case 101:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x51,
						      0x10, 0x03, 0x10, 0x54 |
						      tdma_byte4_modify);
			break;
		case 102:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x35, 0x03, 0x11, 0x11 |
						      tdma_byte4_modify);
			break;
		case 103:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x51,
						      0x30, 0x3, 0x10, 0x50 |
						      tdma_byte4_modify);
			break;
		case 104:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x51,
						      0x21, 0x3, 0x10, 0x50 |
						      tdma_byte4_modify);
			break;
		case 105:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x51,
						      0x35, 0x3, 0x10, 0x50 |
						      tdma_byte4_modify);
			break;
		case 106:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x51,
						      0x08, 0x3, 0x10, 0x50 |
						      tdma_byte4_modify);
			break;
		case 107:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x51,
						      0x08, 0x7, 0x10, 0x54);
			break;
		case 108:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x51,
						      0x30, 0x3, 0x10, 0x50 |
						      tdma_byte4_modify);
			break;
		case 109:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x51,
						      0x10, 0x03, 0x10, 0x54 |
						      tdma_byte4_modify);
			break;
		case 110:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x51,
						      0x30, 0x03, 0x10, 0x50 |
						      tdma_byte4_modify);
			break;
		case 111:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x25, 0x03, 0x11, 0x11);
			break;
		case 112:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x51,
						      0x4a, 0x3, 0x10, 0x50);
			break;
		case 113:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x48, 0x03, 0x11, 0x10);
			break;
		case 116:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x51,
						      0x08, 0x03, 0x10, 0x54 |
						      tdma_byte4_modify);
			break;
		case 117:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x08, 0x03, 0x10, 0x14 |
						      tdma_byte4_modify);
			break;
		case 119:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x10, 0x03, 0x11, 0x14 |
						      tdma_byte4_modify);
			break;
		case 120:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x51,
						      0x10, 0x03, 0x10, 0x55);
			break;
		case 121:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x61,
						      0x10, 0x03, 0x11, 0x15);
			break;
		case 151:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x51,
						      0x10, 0x03, 0x10, 0x50 |
						      tdma_byte4_modify);
			break;
		}
	} else {
		/* disable PS tdma */
		switch (type) {
		case 0:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x0,
						      0x0, 0x0, 0x40, 0x01);
			break;
		case 1:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x0,
						      0x0, 0x0, 0x48, 0x0);
			break;
		default:
			halbtc8723d2ant_set_fw_pstdma(btcoexist, 0x0,
						      0x0, 0x0, 0x40, 0x0);
			break;
		}
	}

	coex_dm->cur_ps_tdma_on = turn_on;
	coex_dm->cur_ps_tdma = type;

	btcoexist->btc_set_atomic(btcoexist, &coex_dm->setting_tdma, FALSE);
}

static
void halbtc8723d2ant_set_ant_path(IN struct btc_coexist *btcoexist,
				  IN u8 ant_pos_type, IN boolean force_exec,
				  IN u8 phase)
{
	struct  btc_board_info *board_info = &btcoexist->board_info;
	u32			u32tmp = 0;
	boolean			pg_ext_switch = FALSE,  is_hw_ant_div_on = FALSE;
	u8			h2c_parameter[2] = {0};
	u32			cnt_bt_cal_chk = 0;
	u8			u8tmp0 = 0, u8tmp1 = 0;
	boolean			is_in_mp_mode = FALSE;
	u32				u32tmp0 = 0, u32tmp1 = 0, u32tmp2 = 0;
	u16				u16tmp0 = 0,  u16tmp1 = 0;


#if BT_8723D_2ANT_COEX_DBG

	u32tmp1 = halbtc8723d2ant_ltecoex_indirect_read_reg(btcoexist, 0x38);

     /* To avoid indirect access fail   */
	if (((u32tmp1 & 0xf000) >> 12) != ((u32tmp1 & 0x0f00) >> 8)) {
		force_exec = TRUE;
		coex_sta->gnt_error_cnt++;
	}

	u32tmp2 = halbtc8723d2ant_ltecoex_indirect_read_reg(btcoexist, 0x54);
	u16tmp0 = btcoexist->btc_read_2byte(btcoexist, 0xaa);
	u16tmp1 = btcoexist->btc_read_2byte(btcoexist, 0x948);
	u8tmp1  = btcoexist->btc_read_1byte(btcoexist, 0x73);
	u8tmp0 =  btcoexist->btc_read_1byte(btcoexist, 0x67);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], ********** 0x67 = 0x%x, 0x948 = 0x%x, 0x73 = 0x%x(Before Set Ant Pat)\n",
		    u8tmp0, u16tmp1, u8tmp1);
	BTC_TRACE(trace_buf);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], **********0x38= 0x%x, 0x54= 0x%x, 0xaa = 0x%x (Before Set Ant Path)\n",
		    u32tmp1, u32tmp2, u16tmp0);
	BTC_TRACE(trace_buf);
#endif

	coex_sta->is_2g_freerun = ((phase == BT_8723D_2ANT_PHASE_2G_FREERUN) ? TRUE : FALSE);

	coex_dm->cur_ant_pos_type = ant_pos_type;

	if (!force_exec) {
		if (coex_dm->cur_ant_pos_type == coex_dm->pre_ant_pos_type) {

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Skip Antenna Path Setup because no change!!\n");
			BTC_TRACE(trace_buf);
			return;
		}
	}

	coex_dm->pre_ant_pos_type = coex_dm->cur_ant_pos_type;

	switch (phase) {
	case BT_8723D_2ANT_PHASE_COEX_POWERON:
		/* Set Path control to WL */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x80, 0x0);

		/* set Path control owner to WL at initial step */
		halbtc8723d2ant_coex_ctrl_owner(btcoexist,
						BT_8723D_2ANT_PCO_BTSIDE);

		if (BTC_ANT_PATH_AUTO == ant_pos_type)
			ant_pos_type = BTC_ANT_PATH_WIFI;

		coex_sta->run_time_state = FALSE;

		break;
	case BT_8723D_2ANT_PHASE_COEX_INIT:
		/* Disable LTE Coex Function in WiFi side
		 * (this should be on if LTE coex is required)
		 */
		halbtc8723d2ant_ltecoex_enable(btcoexist, 0x0);

		/* GNT_WL_LTE always = 1
		 * (this should be config if LTE coex is required)
		 */
		halbtc8723d2ant_ltecoex_set_coex_table(
			btcoexist,
			BT_8723D_2ANT_CTT_WL_VS_LTE,
			0xffff);

		/* GNT_BT_LTE always = 1
		 * (this should be config if LTE coex is required)
		 */
		halbtc8723d2ant_ltecoex_set_coex_table(
			btcoexist,
			BT_8723D_2ANT_CTT_BT_VS_LTE,
			0xffff);

		/* Wait If BT IQK running, because Path control owner is
		 * at BT during BT IQK (setup by WiFi firmware)
		 */
		while (cnt_bt_cal_chk <= 20) {
			u8tmp0 = btcoexist->btc_read_1byte(btcoexist, 0x49d);
			cnt_bt_cal_chk++;
			if (u8tmp0 & BIT(0)) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					    "[BTCoex],  BT is calibrating (wait cnt=%d)\n",
					    cnt_bt_cal_chk);
				BTC_TRACE(trace_buf);
				delay_ms(50);
			} else {
				BTC_SPRINTF(trace_buf,
					    BT_TMP_BUF_SIZE,
					    "[BTCoex], BT is NOT calibrating (wait cnt=%d)\n",
					    cnt_bt_cal_chk);
				BTC_TRACE(trace_buf);
				break;
			}
		}


		/* Set Path control to WL */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x80, 0x1);

		/* set Path control owner to WL at initial step */
		halbtc8723d2ant_coex_ctrl_owner(btcoexist,
						BT_8723D_2ANT_PCO_WLSIDE);

		/* set GNT_BT to SW high */
		halbtc8723d2ant_ltecoex_set_gnt_bt(btcoexist,
					   BT_8723D_2ANT_GNT_BLOCK_RFC_BB,
					   BT_8723D_2ANT_GNT_TYPE_CTRL_BY_SW,
					   BT_8723D_2ANT_SIG_STA_SET_TO_HIGH);
		/* Set GNT_WL to SW high */
		halbtc8723d2ant_ltecoex_set_gnt_wl(btcoexist,
					   BT_8723D_2ANT_GNT_BLOCK_RFC_BB,
					   BT_8723D_2ANT_GNT_TYPE_CTRL_BY_SW,
					   BT_8723D_2ANT_SIG_STA_SET_TO_HIGH);

		coex_sta->run_time_state = FALSE;

		if (BTC_ANT_PATH_AUTO == ant_pos_type) {
			if (board_info->btdm_ant_pos ==
			    BTC_ANTENNA_AT_MAIN_PORT)
				ant_pos_type = BTC_ANT_WIFI_AT_MAIN;
			else
				ant_pos_type = BTC_ANT_WIFI_AT_AUX;
		}

		break;
	case BT_8723D_2ANT_PHASE_WLANONLY_INIT:
		/* Disable LTE Coex Function in WiFi side
		 * (this should be on if LTE coex is required)
		 */
		halbtc8723d2ant_ltecoex_enable(btcoexist, 0x0);

		/* GNT_WL_LTE always = 1
		 * (this should be config if LTE coex is required)
		 */
		halbtc8723d2ant_ltecoex_set_coex_table(
			btcoexist,
			BT_8723D_2ANT_CTT_WL_VS_LTE,
			0xffff);

		/* GNT_BT_LTE always = 1
		 *(this should be config if LTE coex is required)
		 */
		halbtc8723d2ant_ltecoex_set_coex_table(
			btcoexist,
			BT_8723D_2ANT_CTT_BT_VS_LTE,
			0xffff);

		/* Set Path control to WL */
		btcoexist->btc_write_1byte_bitmask(btcoexist,
						   0x67, 0x80, 0x1);

		/* set Path control owner to WL at initial step */
		halbtc8723d2ant_coex_ctrl_owner(btcoexist,
						BT_8723D_2ANT_PCO_WLSIDE);

		/* set GNT_BT to SW Low */
		halbtc8723d2ant_ltecoex_set_gnt_bt(btcoexist,
					   BT_8723D_2ANT_GNT_BLOCK_RFC_BB,
					   BT_8723D_2ANT_GNT_TYPE_CTRL_BY_SW,
					   BT_8723D_2ANT_SIG_STA_SET_TO_LOW);
		/* Set GNT_WL to SW high */
		halbtc8723d2ant_ltecoex_set_gnt_wl(btcoexist,
					   BT_8723D_2ANT_GNT_BLOCK_RFC_BB,
					   BT_8723D_2ANT_GNT_TYPE_CTRL_BY_SW,
					   BT_8723D_2ANT_SIG_STA_SET_TO_HIGH);

		coex_sta->run_time_state = FALSE;

		if (BTC_ANT_PATH_AUTO == ant_pos_type) {
			if (board_info->btdm_ant_pos ==
			    BTC_ANTENNA_AT_MAIN_PORT)
				ant_pos_type = BTC_ANT_WIFI_AT_MAIN;
			else
				ant_pos_type = BTC_ANT_WIFI_AT_AUX;
		}

		break;
	case BT_8723D_2ANT_PHASE_WLAN_OFF:
		/* Disable LTE Coex Function in WiFi side */
		halbtc8723d2ant_ltecoex_enable(btcoexist, 0x0);

		/* Set Path control to BT */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x80, 0x0);

		/* set Path control owner to BT */
		halbtc8723d2ant_coex_ctrl_owner(btcoexist,
						BT_8723D_2ANT_PCO_BTSIDE);

		coex_sta->run_time_state = FALSE;
		break;
	case BT_8723D_2ANT_PHASE_2G_RUNTIME:

		/* wait for WL/BT IQK finish, keep 0x38 = 0xff00 for WL IQK */
		while (cnt_bt_cal_chk <= 20) {
			u8tmp0 = btcoexist->btc_read_1byte(
					 btcoexist,
					 0x1e6);

			u8tmp1 = btcoexist->btc_read_1byte(
					 btcoexist,
					 0x49d);

			cnt_bt_cal_chk++;
			if ((u8tmp0 & BIT(0)) || (u8tmp1 & BIT(0))) {
				BTC_SPRINTF(trace_buf,
					    BT_TMP_BUF_SIZE,
					    "[BTCoex], ########### WL or BT is IQK (wait cnt=%d)\n",
					    cnt_bt_cal_chk);
				BTC_TRACE(trace_buf);
				delay_ms(50);
			} else {
				BTC_SPRINTF(trace_buf,
					    BT_TMP_BUF_SIZE,
					    "[BTCoex], ********** WL and BT is NOT IQK (wait cnt=%d)\n",
					    cnt_bt_cal_chk);
				BTC_TRACE(trace_buf);
				break;
			}
		}

		/* Set Path control to WL
		 * btcoexist->btc_write_1byte_bitmask(
		 * btcoexist, 0x67, 0x80, 0x1);
		 * set Path control owner to WL at runtime step
		 */
		halbtc8723d2ant_coex_ctrl_owner(btcoexist,
						BT_8723D_2ANT_PCO_WLSIDE);

		halbtc8723d2ant_ltecoex_set_gnt_bt(btcoexist,
					   BT_8723D_2ANT_GNT_BLOCK_RFC_BB,
					   BT_8723D_2ANT_GNT_TYPE_CTRL_BY_PTA,
					   BT_8723D_2ANT_SIG_STA_SET_TO_HIGH);

		/* Set GNT_WL to PTA */
		halbtc8723d2ant_ltecoex_set_gnt_wl(btcoexist,
					   BT_8723D_2ANT_GNT_BLOCK_RFC_BB,
					   BT_8723D_2ANT_GNT_TYPE_CTRL_BY_PTA,
					   BT_8723D_2ANT_SIG_STA_SET_BY_HW);

		coex_sta->run_time_state = TRUE;

		if (BTC_ANT_PATH_AUTO == ant_pos_type) {
			if (board_info->btdm_ant_pos ==
			    BTC_ANTENNA_AT_MAIN_PORT)
				ant_pos_type = BTC_ANT_WIFI_AT_MAIN;
			else
				ant_pos_type = BTC_ANT_WIFI_AT_AUX;
		}

		break;
	case BT_8723D_2ANT_PHASE_2G_FREERUN:

		/* set Path control owner to WL at runtime step */
		halbtc8723d2ant_coex_ctrl_owner(btcoexist,
						BT_8723D_2ANT_PCO_WLSIDE);

		/* set GNT_BT to SW Hi */
		halbtc8723d2ant_ltecoex_set_gnt_bt(btcoexist,
					   BT_8723D_2ANT_GNT_BLOCK_RFC_BB,
					   BT_8723D_2ANT_GNT_TYPE_CTRL_BY_SW,
					   BT_8723D_2ANT_SIG_STA_SET_TO_HIGH);
		/* Set GNT_WL to SW Hi */
		halbtc8723d2ant_ltecoex_set_gnt_wl(btcoexist,
					   BT_8723D_2ANT_GNT_BLOCK_RFC_BB,
					   BT_8723D_2ANT_GNT_TYPE_CTRL_BY_SW,
					   BT_8723D_2ANT_SIG_STA_SET_TO_HIGH);

		coex_sta->run_time_state = TRUE;

		break;
	case BT_8723D_2ANT_PHASE_BTMPMODE:
		/* Disable LTE Coex Function in WiFi side */
		halbtc8723d2ant_ltecoex_enable(btcoexist, 0x0);

		/* Set Path control to WL */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x80, 0x1);

		/* set Path control owner to WL */
		halbtc8723d2ant_coex_ctrl_owner(btcoexist,
						BT_8723D_2ANT_PCO_WLSIDE);

		/* set GNT_BT to SW Hi */
		halbtc8723d2ant_ltecoex_set_gnt_bt(btcoexist,
					   BT_8723D_2ANT_GNT_BLOCK_RFC_BB,
					   BT_8723D_2ANT_GNT_TYPE_CTRL_BY_SW,
					   BT_8723D_2ANT_SIG_STA_SET_TO_HIGH);

		/* Set GNT_WL to SW Lo */
		halbtc8723d2ant_ltecoex_set_gnt_wl(btcoexist,
					   BT_8723D_2ANT_GNT_BLOCK_RFC_BB,
					   BT_8723D_2ANT_GNT_TYPE_CTRL_BY_SW,
					   BT_8723D_2ANT_SIG_STA_SET_TO_LOW);

		coex_sta->run_time_state = FALSE;

		if (BTC_ANT_PATH_AUTO == ant_pos_type) {
			if (board_info->btdm_ant_pos ==
			    BTC_ANTENNA_AT_MAIN_PORT)
				ant_pos_type = BTC_ANT_WIFI_AT_MAIN;
			else
				ant_pos_type = BTC_ANT_WIFI_AT_AUX;
		}

		break;
	case BT_8723D_2ANT_PHASE_ANTENNA_DET:

		/* Set Path control to WL */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x80, 0x1);

		/* set Path control owner to WL */
		halbtc8723d2ant_coex_ctrl_owner(btcoexist,
						BT_8723D_2ANT_PCO_WLSIDE);

		/* Set Antenna Path,  both GNT_WL/GNT_BT = 1,
		 *and control by SW
		 * set GNT_BT to SW high
		 */
		halbtc8723d2ant_ltecoex_set_gnt_bt(btcoexist,
					   BT_8723D_2ANT_GNT_BLOCK_RFC_BB,
					   BT_8723D_2ANT_GNT_TYPE_CTRL_BY_SW,
					   BT_8723D_2ANT_SIG_STA_SET_TO_HIGH);

		/* Set GNT_WL to SW high */
		halbtc8723d2ant_ltecoex_set_gnt_wl(btcoexist,
					   BT_8723D_2ANT_GNT_BLOCK_RFC_BB,
					   BT_8723D_2ANT_GNT_TYPE_CTRL_BY_SW,
					   BT_8723D_2ANT_SIG_STA_SET_TO_HIGH);

		if (BTC_ANT_PATH_AUTO == ant_pos_type)
			ant_pos_type = BTC_ANT_WIFI_AT_AUX;

		coex_sta->run_time_state = FALSE;

		break;
	}

	is_hw_ant_div_on = board_info->ant_div_cfg;

	if (is_hw_ant_div_on && phase != BT_8723D_2ANT_PHASE_ANTENNA_DET)
		btcoexist->btc_write_2byte(btcoexist, 0x948, 0x140);
	else if ((is_hw_ant_div_on == FALSE) &&
		 (phase != BT_8723D_2ANT_PHASE_WLAN_OFF)) {

		switch (ant_pos_type) {
		case BTC_ANT_WIFI_AT_MAIN:
			btcoexist->btc_write_2byte(btcoexist, 0x948, 0x0);
			break;
		case BTC_ANT_WIFI_AT_AUX:
			btcoexist->btc_write_2byte(btcoexist, 0x948, 0x280);
			break;
		}
	}


#if BT_8723D_2ANT_COEX_DBG
	u32tmp1 = halbtc8723d2ant_ltecoex_indirect_read_reg(btcoexist, 0x38);
	u32tmp2 = halbtc8723d2ant_ltecoex_indirect_read_reg(btcoexist, 0x54);
	u16tmp0 = btcoexist->btc_read_2byte(btcoexist, 0xaa);
	u16tmp1 = btcoexist->btc_read_2byte(btcoexist, 0x948);
	u8tmp1  = btcoexist->btc_read_1byte(btcoexist, 0x73);
	u8tmp0 =  btcoexist->btc_read_1byte(btcoexist, 0x67);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], ********** 0x67 = 0x%x, 0x948 = 0x%x, 0x73 = 0x%x(After Set Ant Pat)\n",
		    u8tmp0, u16tmp1, u8tmp1);
	BTC_TRACE(trace_buf);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], **********0x38= 0x%x, 0x54= 0x%x, 0xaa= 0x%x (After Set Ant Path)\n",
		    u32tmp1, u32tmp2, u16tmp0);
	BTC_TRACE(trace_buf);
#endif

}

static
u8 halbtc8723d2ant_action_algorithm(IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info	*bt_link_info = &btcoexist->bt_link_info;
	boolean				bt_hs_on = FALSE;
	u8				algorithm = BT_8723D_2ANT_COEX_ALGO_UNDEFINED;
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

	if (num_of_diff_profile == 0) {

		if (bt_link_info->acl_busy) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], No-Profile busy\n");
			BTC_TRACE(trace_buf);
			algorithm = BT_8723D_2ANT_COEX_ALGO_NOPROFILEBUSY;
		}
	} else if ((bt_link_info->a2dp_exist) && (coex_sta->is_bt_a2dp_sink)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], A2DP Sink\n");
		BTC_TRACE(trace_buf);
		algorithm = BT_8723D_2ANT_COEX_ALGO_A2DPSINK;
	} else if (num_of_diff_profile == 1) {
		if (bt_link_info->sco_exist) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], SCO only\n");
			BTC_TRACE(trace_buf);
			algorithm = BT_8723D_2ANT_COEX_ALGO_SCO;
		} else {
			if (bt_link_info->hid_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					    "[BTCoex], HID only\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8723D_2ANT_COEX_ALGO_HID;
			} else if (bt_link_info->a2dp_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					    "[BTCoex], A2DP only\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8723D_2ANT_COEX_ALGO_A2DP;
			} else if (bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						    "[BTCoex], PAN(HS) only\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723D_2ANT_COEX_ALGO_PANHS;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], PAN(EDR) only\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723D_2ANT_COEX_ALGO_PANEDR;
				}
			}
		}
	} else if (num_of_diff_profile == 2) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					    "[BTCoex], SCO + HID\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8723D_2ANT_COEX_ALGO_SCO;
			} else if (bt_link_info->a2dp_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					    "[BTCoex], SCO + A2DP ==> A2DP\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8723D_2ANT_COEX_ALGO_A2DP;
			} else if (bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm = BT_8723D_2ANT_COEX_ALGO_SCO;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723D_2ANT_COEX_ALGO_PANEDR;
				}
			}
		} else {
			if (bt_link_info->hid_exist &&
			    bt_link_info->a2dp_exist) {
				{
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						    "[BTCoex], HID + A2DP\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723D_2ANT_COEX_ALGO_HID_A2DP;
				}
			} else if (bt_link_info->hid_exist &&
				   bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], HID + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm = BT_8723D_2ANT_COEX_ALGO_HID;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], HID + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723D_2ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (bt_link_info->pan_exist &&
				   bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], A2DP + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723D_2ANT_COEX_ALGO_A2DP_PANHS;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], A2DP + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723D_2ANT_COEX_ALGO_PANEDR_A2DP;
				}
			}
		}
	} else if (num_of_diff_profile == 3) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist &&
			    bt_link_info->a2dp_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], SCO + HID + A2DP ==> HID + A2DP\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8723D_2ANT_COEX_ALGO_HID_A2DP;
			} else if (bt_link_info->hid_exist &&
				   bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						    "[BTCoex], SCO + HID + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723D_2ANT_COEX_ALGO_PANEDR_HID;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						    "[BTCoex], SCO + HID + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723D_2ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (bt_link_info->pan_exist &&
				   bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + A2DP + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723D_2ANT_COEX_ALGO_PANEDR_A2DP;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + A2DP + PAN(EDR) ==> HID\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723D_2ANT_COEX_ALGO_PANEDR_A2DP;
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
						BT_8723D_2ANT_COEX_ALGO_HID_A2DP_PANEDR;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], HID + A2DP + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723D_2ANT_COEX_ALGO_HID_A2DP_PANEDR;
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
					algorithm =
						BT_8723D_2ANT_COEX_ALGO_HID_A2DP_PANEDR;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + HID + A2DP + PAN(EDR)==>PAN(EDR)+HID\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723D_2ANT_COEX_ALGO_HID_A2DP_PANEDR;
				}
			}
		}
	}

	return algorithm;
}

static
void halbtc8723d2ant_action_coex_all_off(IN struct btc_coexist *btcoexist)
{

	halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
	halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
	halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
	halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

	halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

	/* fw all off */
	halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
}

static
void halbtc8723d2ant_action_bt_whql_test(IN struct btc_coexist *btcoexist)
{
	halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
	halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
	halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
	halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

	halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

	halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
}

static
void halbtc8723d2ant_action_freerun(IN struct btc_coexist *btcoexist)
{
	boolean wifi_busy = FALSE;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], wifi_freerun!!\n");
	BTC_TRACE(trace_buf);

	halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0x90);
	halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, TRUE);
	halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);

	/* just for GNT_WL = 1 && GNT_BT = 1, not for antenna control  */
	halbtc8723d2ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO, FORCE_EXEC,
								 BT_8723D_2ANT_PHASE_2G_FREERUN);
	if (wifi_busy)
		//halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, TRUE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
	else
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

	halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
	halbtc8723d2ant_ps_tdma(btcoexist, FORCE_EXEC, FALSE, 0);
}

static
void halbtc8723d2ant_action_bt_hs(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state, bt_rssi_state;

	static u8	prewifi_rssi_state2 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state2 = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state2, bt_rssi_state2;
	boolean wifi_busy = FALSE, wifi_turbo = FALSE;


	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM,
			   &coex_sta->scan_ap_num);
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"############# [BTCoex],  scan_ap_num = %d, wl_noisy = %d\n",
			coex_sta->scan_ap_num, coex_sta->wl_noisy_level);
	BTC_TRACE(trace_buf);

#if 1
	if ((wifi_busy) && (coex_sta->wl_noisy_level == 0))
		wifi_turbo = TRUE;
#endif


	wifi_rssi_state = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2,
			  coex_sta->wifi_coex_thres , 0);

	wifi_rssi_state2 = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state2, 2,
			   coex_sta->wifi_coex_thres2, 0);

	bt_rssi_state = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state, 2,
			coex_sta->bt_coex_thres, 0);

	bt_rssi_state2 = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state2, 2,
			coex_sta->bt_coex_thres2, 0);

	if (BTC_RSSI_HIGH(wifi_rssi_state) &&
		BTC_RSSI_HIGH(bt_rssi_state)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
	} else if (BTC_RSSI_HIGH(wifi_rssi_state2) &&
		   BTC_RSSI_HIGH(bt_rssi_state2)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0x90);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 2);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);

	} else {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = TRUE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
	}

}

static
void halbtc8723d2ant_action_bt_inquiry(IN struct btc_coexist *btcoexist)
{

	boolean wifi_connected = FALSE;
	boolean wifi_scan = FALSE, wifi_link = FALSE, wifi_roam = FALSE;
	boolean wifi_busy = FALSE;
	struct	btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;


	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &wifi_scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &wifi_link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &wifi_roam);

	halbtc8723d2ant_adjust_wl_tx_power(btcoexist, FORCE_EXEC, 0xb2);
	halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
	halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
	halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

	if ((coex_sta->bt_create_connection) && ((wifi_link) || (wifi_roam)
		|| (wifi_scan) || (coex_sta->wifi_is_high_pri_task))) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Wifi link/roam/Scan/busy/hi-pri-task + BT Inq/Page!!\n");
		BTC_TRACE(trace_buf);

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
							 8);

		if ((bt_link_info->a2dp_exist) && (!bt_link_info->pan_exist))
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE,
						15);
		else
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE,
						11);
	}  else if ((!wifi_connected) && (!wifi_scan)) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Wifi no-link + no-scan + BT Inq/Page!!\n");
		BTC_TRACE(trace_buf);

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
	} else if (wifi_busy) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], bt inq/page +  wifi busy\n");
		BTC_TRACE(trace_buf);

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
	} else if (wifi_connected) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], bt inq/page +  wifi connected\n");
		BTC_TRACE(trace_buf);

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
	} else if (bt_link_info->pan_exist) {

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);

		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 22);
	} else if (bt_link_info->a2dp_exist) {

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);

		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 16);
	} else {

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);

		if ((wifi_link) || (wifi_roam) || (wifi_scan)
			|| (coex_sta->wifi_is_high_pri_task))
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 21);
		else
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 23);
	}

}

static
void halbtc8723d2ant_action_bt_relink(IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
#if 0
	if ((!coex_sta->is_bt_multi_link && !bt_link_info->pan_exist) ||
	    (bt_link_info->a2dp_exist && bt_link_info->hid_exist)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, FORCE_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist,
						  NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist,
						  NORMAL_EXEC, FALSE);

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 8);
	}
#endif
	halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
	halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
}

static
void halbtc8723d2ant_action_bt_idle(IN struct btc_coexist *btcoexist)
{
	boolean wifi_busy = FALSE;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	halbtc8723d2ant_adjust_wl_tx_power(btcoexist, FORCE_EXEC, 0xb2);
	halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
	halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
	halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

	if (!wifi_busy) {

		halbtc8723d2ant_coex_table_with_type(btcoexist,
						     NORMAL_EXEC, 8);

		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 14);
	} else {  /* if wl busy */

		if (BT_8723D_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
			coex_dm->bt_status) {

			halbtc8723d2ant_coex_table_with_type(btcoexist,
								 NORMAL_EXEC, 0);

			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
	   } else {

			halbtc8723d2ant_coex_table_with_type(btcoexist,
									NORMAL_EXEC, 8);
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE,
							12);
		}
	}

}



/* SCO only or SCO+PAN(HS) */
static
void halbtc8723d2ant_action_sco(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8	wifi_rssi_state, bt_rssi_state;

	static u8	prewifi_rssi_state2 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state2 = BTC_RSSI_STATE_LOW;
	u8	wifi_rssi_state2, bt_rssi_state2;
	boolean	wifi_busy = FALSE;
	u32  wifi_bw = 1;

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW,
			   &wifi_bw);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	wifi_rssi_state = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2,
			  coex_sta->wifi_coex_thres , 0);

	wifi_rssi_state2 = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state2, 2,
			   coex_sta->wifi_coex_thres2, 0);

	bt_rssi_state = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state, 2,
			coex_sta->bt_coex_thres, 0);

	bt_rssi_state2 = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state2, 2,
			coex_sta->bt_coex_thres2, 0);


	if (BTC_RSSI_HIGH(wifi_rssi_state) &&
		BTC_RSSI_HIGH(bt_rssi_state)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
	}  else {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		if (coex_sta->is_esco_mode)
			halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
		else  /* 2-Ant free run if eSCO mode */
			halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 8);
	}

}

static
void halbtc8723d2ant_action_hid(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state, bt_rssi_state;

	static u8	prewifi_rssi_state2 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state2 = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state2, bt_rssi_state2;
	boolean wifi_busy = FALSE;
	u32  wifi_bw = 1;


	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW,  &wifi_bw);

	wifi_rssi_state = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2,
			  coex_sta->wifi_coex_thres , 0);

	wifi_rssi_state2 = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state2, 2,
			   coex_sta->wifi_coex_thres2, 0);

	bt_rssi_state = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state, 2,
			coex_sta->bt_coex_thres, 0);

	bt_rssi_state2 = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state2, 2,
			coex_sta->bt_coex_thres2, 0);


	if (BTC_RSSI_HIGH(wifi_rssi_state) &&
		BTC_RSSI_HIGH(bt_rssi_state)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
	}  else {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		if (coex_sta->is_hid_low_pri_tx_overhead) {

			halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 108);
		} else if (coex_sta->is_hid_rcu) {

			halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 113);
		} else if (wifi_bw == 0) {   /* if 11bg mode */

			halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 111);
		} else {

			halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 111);
		}
	}

}

static
void halbtc8723d2ant_action_a2dpsink(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state, bt_rssi_state;

	static u8	prewifi_rssi_state2 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state2 = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state2, bt_rssi_state2;
	boolean wifi_busy = FALSE, wifi_turbo = FALSE;
	struct	btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;


	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM,
			   &coex_sta->scan_ap_num);
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"############# [BTCoex],  scan_ap_num = %d, wl_noisy = %d\n",
			coex_sta->scan_ap_num, coex_sta->wl_noisy_level);
	BTC_TRACE(trace_buf);

#if 1
	if ((wifi_busy) && (coex_sta->wl_noisy_level == 0))
		wifi_turbo = TRUE;
#endif

	wifi_rssi_state = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2,
			  coex_sta->wifi_coex_thres, 0);

	wifi_rssi_state2 = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state2, 2,
			   coex_sta->wifi_coex_thres2, 0);

	bt_rssi_state = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state, 2,
			coex_sta->bt_coex_thres, 0);

	bt_rssi_state2 = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state2, 2,
			coex_sta->bt_coex_thres2, 0);


	if (BTC_RSSI_HIGH(wifi_rssi_state) &&
		BTC_RSSI_HIGH(bt_rssi_state)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
	} else if (BTC_RSSI_HIGH(wifi_rssi_state2) &&
		   BTC_RSSI_HIGH(bt_rssi_state2)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0x90);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 2);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);

		if (wifi_busy)
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 1);
		else
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 16);
	} else {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = TRUE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);
		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 104);
	}

}

/* A2DP only / PAN(EDR) only/ A2DP+PAN(HS) */
static
void halbtc8723d2ant_action_a2dp(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state, bt_rssi_state;

	static u8	prewifi_rssi_state2 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state2 = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state2, bt_rssi_state2;

	static u8	prewifi_rssi_state3 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state3 = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state3, bt_rssi_state3;

	boolean wifi_busy = FALSE, wifi_turbo = FALSE;
	u8	iot_peer = BTC_IOT_PEER_UNKNOWN;
	u32	wifi_link_status = 0;


	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM,
			   &coex_sta->scan_ap_num);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_IOT_PEER, &iot_peer);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
			   &wifi_link_status);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"############# [BTCoex],  scan_ap_num = %d, wl_noisy = %d\n",
			coex_sta->scan_ap_num, coex_sta->wl_noisy_level);
	BTC_TRACE(trace_buf);

#if 1
	if ((wifi_busy) && (coex_sta->wl_noisy_level == 0))
		wifi_turbo = TRUE;
#endif

	wifi_rssi_state = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2,
			  coex_sta->wifi_coex_thres , 0);

	wifi_rssi_state2 = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state2, 2,
			   coex_sta->wifi_coex_thres2, 0);

	wifi_rssi_state3 = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state3, 2,
			   40, 0);

	bt_rssi_state = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state, 2,
			coex_sta->bt_coex_thres, 0);

	bt_rssi_state2 = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state2, 2,
			coex_sta->bt_coex_thres2, 0);

	bt_rssi_state3 = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state3, 2,
			80, 0);

#if 1
	if (BTC_RSSI_HIGH(wifi_rssi_state) && BTC_RSSI_HIGH(bt_rssi_state)) {
		halbtc8723d2ant_action_freerun(btcoexist);
	} else {

		if (wifi_link_status & WIFI_P2P_GC_CONNECTED) {
			halbtc8723d2ant_adjust_wl_tx_power(btcoexist,
							   NORMAL_EXEC, 0xb2);
			halbtc8723d2ant_adjust_bt_tx_power(btcoexist,
							   NORMAL_EXEC, 15);

		} else {
			halbtc8723d2ant_adjust_wl_tx_power(btcoexist,
							   NORMAL_EXEC, 0x90);
			halbtc8723d2ant_adjust_bt_tx_power(btcoexist,
							   NORMAL_EXEC, 0x0);
		}

		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist,
						  NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist,
						  NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = TRUE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);

		if (BTC_RSSI_HIGH(wifi_rssi_state3)) {
			if (coex_sta->connect_ap_period_cnt > 0)
				//halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC,
				//			TRUE, 121);
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							TRUE, 23);
			else
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							TRUE, 119);
		} else {
			if (coex_sta->connect_ap_period_cnt > 0)
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							TRUE, 120);
			else
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							TRUE, 101);
		}

	}

#else

	if (BTC_RSSI_HIGH(wifi_rssi_state) &&
		BTC_RSSI_HIGH(bt_rssi_state)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
	} else if (BTC_RSSI_HIGH(wifi_rssi_state2) &&
		   BTC_RSSI_HIGH(bt_rssi_state2)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0x90);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 2);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);

		if (wifi_busy)
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 1);
		else
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 16);
	} else {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = TRUE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);
		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 101);
	}
#endif

}

static
void halbtc8723d2ant_action_pan_edr(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state, bt_rssi_state;

	static u8	prewifi_rssi_state2 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state2 = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state2, bt_rssi_state2;
	boolean wifi_busy = FALSE, wifi_turbo = FALSE;

	static u8	prewifi_rssi_state3 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state3 = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state3, bt_rssi_state3;


	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM, &coex_sta->scan_ap_num);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"############# [BTCoex],  scan_ap_num = %d, wl_noisy = %d\n",
			coex_sta->scan_ap_num, coex_sta->wl_noisy_level);
	BTC_TRACE(trace_buf);

#if 1
	if ((wifi_busy) && (coex_sta->wl_noisy_level == 0))
		wifi_turbo = TRUE;
#endif

	wifi_rssi_state = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2,
			  coex_sta->wifi_coex_thres , 0);

	wifi_rssi_state2 = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state2, 2,
			   coex_sta->wifi_coex_thres2, 0);

	wifi_rssi_state3 = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state3, 2,
			   58, 0);

	bt_rssi_state = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state, 2,
			coex_sta->bt_coex_thres, 0);

	bt_rssi_state2 = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state2, 2,
			coex_sta->bt_coex_thres2, 0);

	bt_rssi_state3 = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state3, 2,
			47, 0);

#if 0
	halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
	halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
	halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
	halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

	coex_dm->is_switch_to_1dot5_ant = FALSE;

	halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

	halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
#endif


#if 1
	if (BTC_RSSI_HIGH(wifi_rssi_state) &&
		BTC_RSSI_HIGH(bt_rssi_state)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
	} else if (BTC_RSSI_HIGH(wifi_rssi_state2) &&
		   BTC_RSSI_HIGH(bt_rssi_state2)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0x90);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 2);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);

		if (wifi_busy)
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 3);
		else
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 4);
	} else {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = TRUE;

		/* for Lenovo CPT_For_WiFi OPP test  */
		if (btcoexist->board_info.customer_id == RT_CID_LENOVO_CHINA &&
		    BTC_RSSI_HIGH(wifi_rssi_state3) && wifi_busy) {
			halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);

			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 112);
		} else {

			halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);

			if (wifi_busy)
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 103);
			else
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 104);
		}
	}

#endif

}

static
void halbtc8723d2ant_action_hid_a2dp(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state, bt_rssi_state;

	static u8	prewifi_rssi_state2 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state2 = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state2, bt_rssi_state2;

	static u8	prewifi_rssi_state3 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state3 = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state3, bt_rssi_state3;


	boolean wifi_busy = FALSE;
	u32 wifi_bw = 1;

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW,
			   &wifi_bw);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	wifi_rssi_state = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2,
			  coex_sta->wifi_coex_thres , 0);

	wifi_rssi_state2 = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state2, 2,
			   coex_sta->wifi_coex_thres2, 0);

	wifi_rssi_state3 = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state3, 2,
			   40, 0);

	bt_rssi_state = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state, 2,
			coex_sta->bt_coex_thres, 0);

	bt_rssi_state2 = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state2, 2,
			coex_sta->bt_coex_thres2, 0);

	bt_rssi_state3 = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state3, 2,
			40, 0);

#if 1

	if (BTC_RSSI_HIGH(wifi_rssi_state) &&
		BTC_RSSI_HIGH(bt_rssi_state)) {

		halbtc8723d2ant_action_freerun(btcoexist);
	} else {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0x90);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0x0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = TRUE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);

		if (coex_sta->hid_pair_cnt > 1) {
			if (BTC_RSSI_HIGH(wifi_rssi_state3))
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 117);
			else
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 116);
		} else {
		if (BTC_RSSI_HIGH(wifi_rssi_state3))
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 119);
		else
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 109);
		}
	}

#else

	if (BTC_RSSI_HIGH(wifi_rssi_state) &&
		BTC_RSSI_HIGH(bt_rssi_state)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
	} else if (BTC_RSSI_HIGH(wifi_rssi_state2) &&
		   BTC_RSSI_HIGH(bt_rssi_state2)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0x90);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 2);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);

		if (wifi_busy)
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 1);
		else
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 16);
	} else {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = TRUE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);

		if (coex_sta->hid_pair_cnt > 1) {
			if (BTC_RSSI_HIGH(wifi_rssi_state3))
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 117);
			else
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 116);
		} else {
		if (BTC_RSSI_HIGH(wifi_rssi_state3))
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 119);
		else
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 109);
		}
	}
#endif
}

static
void halbtc8723d2ant_action_a2dp_pan_hs(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state, bt_rssi_state;

	static u8	prewifi_rssi_state2 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state2 = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state2, bt_rssi_state2;
	boolean wifi_busy = FALSE, wifi_turbo = FALSE;


	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM,
			   &coex_sta->scan_ap_num);
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"############# [BTCoex],  scan_ap_num = %d, wl_noisy = %d\n",
			coex_sta->scan_ap_num, coex_sta->wl_noisy_level);
	BTC_TRACE(trace_buf);

#if 1
	if ((wifi_busy) && (coex_sta->wl_noisy_level == 0))
		wifi_turbo = TRUE;
#endif


	wifi_rssi_state = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2,
			  coex_sta->wifi_coex_thres , 0);

	wifi_rssi_state2 = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state2, 2,
			   coex_sta->wifi_coex_thres2, 0);

	bt_rssi_state = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state, 2,
			coex_sta->bt_coex_thres, 0);

	bt_rssi_state2 = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state2, 2,
			coex_sta->bt_coex_thres2, 0);


	if (BTC_RSSI_HIGH(wifi_rssi_state) &&
		BTC_RSSI_HIGH(bt_rssi_state)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
	} else if (BTC_RSSI_HIGH(wifi_rssi_state2) &&
		   BTC_RSSI_HIGH(bt_rssi_state2)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0x90);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 2);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);

		if (wifi_busy) {

			if ((coex_sta->a2dp_bit_pool > 40) &&
				(coex_sta->a2dp_bit_pool < 255))
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							TRUE, 7);
			else
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC,
							TRUE, 5);
		} else
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE,
						6);

	} else {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = TRUE;

		if (wifi_turbo)
			halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 6);
		else
			halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);

		if (wifi_busy) {

			if ((coex_sta->a2dp_bit_pool > 40) &&
				(coex_sta->a2dp_bit_pool < 255))
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 107);
			else
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 105);
		} else
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 106);

	}

}


/* PAN(EDR)+A2DP */
static
void halbtc8723d2ant_action_pan_edr_a2dp(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state, bt_rssi_state;

	static u8	prewifi_rssi_state2 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state2 = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state2, bt_rssi_state2;

	static u8	prewifi_rssi_state3 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state3 = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state3, bt_rssi_state3;

	boolean	wifi_busy = FALSE, wifi_turbo = FALSE;
	u8	iot_peer = BTC_IOT_PEER_UNKNOWN;


	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM,
			   &coex_sta->scan_ap_num);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_IOT_PEER, &iot_peer);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"############# [BTCoex],  scan_ap_num = %d, wl_noisy = %d\n",
			coex_sta->scan_ap_num, coex_sta->wl_noisy_level);
	BTC_TRACE(trace_buf);

#if 1
	if ((wifi_busy) && (coex_sta->wl_noisy_level == 0))
		wifi_turbo = TRUE;
#endif


	wifi_rssi_state = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2,
			  coex_sta->wifi_coex_thres , 0);

	wifi_rssi_state2 = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state2, 2,
			   coex_sta->wifi_coex_thres2, 0);

	wifi_rssi_state3 = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state3, 2,
			   40, 0);

	bt_rssi_state = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state, 2,
			coex_sta->bt_coex_thres, 0);

	bt_rssi_state2 = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state2, 2,
			coex_sta->bt_coex_thres2, 0);

	bt_rssi_state3 = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state3, 2,
			45, 0);

	if (BTC_RSSI_HIGH(wifi_rssi_state) &&
		BTC_RSSI_HIGH(bt_rssi_state)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
	} else if (BTC_RSSI_HIGH(wifi_rssi_state2) &&
		   BTC_RSSI_HIGH(bt_rssi_state2)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0x90);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 2);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);

		if (wifi_busy) {

			if (((coex_sta->a2dp_bit_pool > 40) &&
				 (coex_sta->a2dp_bit_pool < 255)) ||
				(!coex_sta->is_A2DP_3M))
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 7);
			else
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 5);
		} else
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 6);
	} else {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = TRUE;

		/* for Lenovo coex test case    */
		if (btcoexist->board_info.customer_id == RT_CID_LENOVO_CHINA &&
		    coex_sta->scan_ap_num <= 10) {

			halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);

			/* for CPT_for_WiFi   */
			if (BTC_RSSI_HIGH(bt_rssi_state3) && BTC_RSSI_LOW(wifi_rssi_state3)) {
				halbtc8723d2ant_adjust_bt_tx_power(btcoexist, FORCE_EXEC, 10);
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 105);
			} else {  /* for CPT_for_BT   */
				halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 107);
			}
		} else {

			halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
			//halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);
			halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 3);

			if (wifi_busy)
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 107);
			else
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 106);
		}
	}

}

static
void halbtc8723d2ant_action_pan_edr_hid(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state, bt_rssi_state;

	static u8	prewifi_rssi_state2 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state2 = BTC_RSSI_STATE_LOW;
	u8	wifi_rssi_state2, bt_rssi_state2;
	boolean	wifi_busy = FALSE;
	u32 wifi_bw = 1;

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW,
			   &wifi_bw);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	wifi_rssi_state = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2,
			  coex_sta->wifi_coex_thres , 0);

	wifi_rssi_state2 = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state2, 2,
			   coex_sta->wifi_coex_thres2, 0);

	bt_rssi_state = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state, 2,
			coex_sta->bt_coex_thres, 0);

	bt_rssi_state2 = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state2, 2,
			coex_sta->bt_coex_thres2, 0);


	if (BTC_RSSI_HIGH(wifi_rssi_state) &&
		BTC_RSSI_HIGH(bt_rssi_state)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
	} else if (BTC_RSSI_HIGH(wifi_rssi_state2) &&
		   BTC_RSSI_HIGH(bt_rssi_state2)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0x90);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 2);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);

		if (wifi_busy)
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 3);
		else
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 4);
	} else {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = TRUE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);

		if (wifi_busy)
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 103);
		else
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 104);
	}

}


/* HID+A2DP+PAN(EDR) */
static
void halbtc8723d2ant_action_hid_a2dp_pan_edr(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8	wifi_rssi_state, bt_rssi_state;

	static u8	prewifi_rssi_state2 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state2 = BTC_RSSI_STATE_LOW;
	u8	wifi_rssi_state2, bt_rssi_state2;
	boolean	wifi_busy = FALSE;
	u32 wifi_bw = 1;

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW,
			   &wifi_bw);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	wifi_rssi_state = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2,
			  coex_sta->wifi_coex_thres, 0);

	wifi_rssi_state2 = halbtc8723d2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state2, 2,
			   coex_sta->wifi_coex_thres2 , 0);

	bt_rssi_state = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state, 2,
			coex_sta->bt_coex_thres, 0);

	bt_rssi_state2 = halbtc8723d2ant_bt_rssi_state(btcoexist,
			&pre_bt_rssi_state2, 2,
			coex_sta->bt_coex_thres2, 0);


	if (BTC_RSSI_HIGH(wifi_rssi_state) &&
		BTC_RSSI_HIGH(bt_rssi_state)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
	} else if (BTC_RSSI_HIGH(wifi_rssi_state2) &&
		   BTC_RSSI_HIGH(bt_rssi_state2)) {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0x90);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 2);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = FALSE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);

		if (wifi_busy) {

			if (((coex_sta->a2dp_bit_pool > 40) &&
				 (coex_sta->a2dp_bit_pool < 255)) ||
				(!coex_sta->is_A2DP_3M))
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 7);
			else
				halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 5);
		} else
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 6);
	} else {

		halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
		halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
		halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
		halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

		coex_dm->is_switch_to_1dot5_ant = TRUE;

		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);

		if (wifi_busy)
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 107);
		else
			halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 106);
	}

}

static
void halbtc8723d2ant_action_wifi_native_lps(IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	if (bt_link_info->pan_exist)
		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
	else
		halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 5);
	halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
}

static
void halbtc8723d2ant_action_wifi_multi_port(IN struct btc_coexist *btcoexist)
{
	halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
	halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 15);
	halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
	halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

	/* hw all off */
	halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

	halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
}

static
void halbtc8723d2ant_action_wifi_linkscan_process(IN struct btc_coexist *btcoexist)
{
	struct	btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean	link = FALSE, wifi_connected = FALSE;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	halbtc8723d2ant_adjust_wl_tx_power(btcoexist, FORCE_EXEC, 0xb2);
	halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
	halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
	halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

	halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);
#if 0
	if (bt_link_info->pan_exist)
		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 22);
	else if (bt_link_info->a2dp_exist)
		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 16);
	else
		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 21);
#endif
	if (bt_link_info->a2dp_exist && 
	   (bt_link_info->pan_exist || coex_sta->is_bt_multi_link || 
		link || !wifi_connected))
		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 23);
	else if (bt_link_info->a2dp_exist)
		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 15);
	else if (bt_link_info->pan_exist)
		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 22);
	else
		halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 21);
}

static
void halbtc8723d2ant_action_wifi_not_connected(IN struct btc_coexist *btcoexist)
{
	halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
	halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
	halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
	halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

	halbtc8723d2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

	/* fw all off */
	halbtc8723d2ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
}

static
void halbtc8723d2ant_action_wifi_connected(IN struct btc_coexist *btcoexist)
{
	switch (coex_dm->cur_algorithm) {

	case BT_8723D_2ANT_COEX_ALGO_SCO:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Action 2-Ant, algorithm = SCO.\n");
		BTC_TRACE(trace_buf);
		halbtc8723d2ant_action_sco(btcoexist);
		break;
	case BT_8723D_2ANT_COEX_ALGO_HID:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Action 2-Ant, algorithm = HID.\n");
		BTC_TRACE(trace_buf);
		halbtc8723d2ant_action_hid(btcoexist);
		break;
	case BT_8723D_2ANT_COEX_ALGO_A2DP:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Action 2-Ant, algorithm = A2DP.\n");
		BTC_TRACE(trace_buf);
		halbtc8723d2ant_action_a2dp(btcoexist);
		break;
	case BT_8723D_2ANT_COEX_ALGO_A2DPSINK:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Action 2-Ant, algorithm = A2DP Sink.\n");
		BTC_TRACE(trace_buf);
		halbtc8723d2ant_action_a2dpsink(btcoexist);
		break;
	case BT_8723D_2ANT_COEX_ALGO_A2DP_PANHS:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Action 2-Ant, algorithm = A2DP+PAN(HS).\n");
		BTC_TRACE(trace_buf);
		halbtc8723d2ant_action_a2dp_pan_hs(btcoexist);
		break;
	case BT_8723D_2ANT_COEX_ALGO_PANEDR:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Action 2-Ant, algorithm = PAN(EDR).\n");
		BTC_TRACE(trace_buf);
		halbtc8723d2ant_action_pan_edr(btcoexist);
		break;
	case BT_8723D_2ANT_COEX_ALGO_PANEDR_A2DP:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Action 2-Ant, algorithm = PAN+A2DP.\n");
		BTC_TRACE(trace_buf);
		halbtc8723d2ant_action_pan_edr_a2dp(btcoexist);
		break;
	case BT_8723D_2ANT_COEX_ALGO_PANEDR_HID:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Action 2-Ant, algorithm = PAN(EDR)+HID.\n");
		BTC_TRACE(trace_buf);
		halbtc8723d2ant_action_pan_edr_hid(btcoexist);
		break;
	case BT_8723D_2ANT_COEX_ALGO_HID_A2DP_PANEDR:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Action 2-Ant, algorithm = HID+A2DP+PAN.\n");
		BTC_TRACE(trace_buf);
		halbtc8723d2ant_action_hid_a2dp_pan_edr(
			btcoexist);
		break;
	case BT_8723D_2ANT_COEX_ALGO_HID_A2DP:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Action 2-Ant, algorithm = HID+A2DP.\n");
		BTC_TRACE(trace_buf);
		halbtc8723d2ant_action_hid_a2dp(btcoexist);
		break;
	case BT_8723D_2ANT_COEX_ALGO_NOPROFILEBUSY:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Action 2-Ant, algorithm = No-Profile busy.\n");
		BTC_TRACE(trace_buf);
		halbtc8723d2ant_action_bt_idle(btcoexist);
		break;
	default:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Action 2-Ant, algorithm = coexist All Off!!\n");
		BTC_TRACE(trace_buf);
		halbtc8723d2ant_action_coex_all_off(btcoexist);
		break;
		}

	 coex_dm->pre_algorithm = coex_dm->cur_algorithm;

}

static
void halbtc8723d2ant_run_coexist_mechanism(IN struct btc_coexist *btcoexist)
{
	u8	algorithm = 0;
	u32	num_of_wifi_link = 0;
	u32	wifi_link_status = 0;
	struct	btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean miracast_plus_bt = FALSE;
	boolean	scan = FALSE, link = FALSE, roam = FALSE,
			under_4way = FALSE, wifi_busy,
			wifi_connected = FALSE, wifi_under_5g = FALSE,
			bt_hs_on = FALSE;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS,
			   &under_4way);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], RunCoexistMechanism()===>\n");
	BTC_TRACE(trace_buf);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], under_lps = %d, force_lps_ctrl = %d, acl_busy = %d!!!\n",
			coex_sta->under_lps, coex_sta->force_lps_ctrl, coex_sta->acl_busy);
	BTC_TRACE(trace_buf);

	if (btcoexist->manual_control) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], RunCoexistMechanism(), return for Manual CTRL <===\n");
		BTC_TRACE(trace_buf);
		return;
	}

	if (btcoexist->stop_coex_dm) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], RunCoexistMechanism(), return for Stop Coex DM <===\n");
		BTC_TRACE(trace_buf);
		return;
	}

	if (coex_sta->under_ips) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], wifi is under IPS !!!\n");
		BTC_TRACE(trace_buf);
		return;
	}

	if (!coex_sta->run_time_state) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], return for run_time_state = FALSE !!!\n");
		BTC_TRACE(trace_buf);
		return;
	}

	if (coex_sta->freeze_coexrun_by_btinfo) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], return for freeze_coexrun_by_btinfo\n");
		BTC_TRACE(trace_buf);
		return;
	}

	/*If wifi connect-idle, we don't need to inform BT wifi's channel*/
	if(!wifi_busy)
		halbtc8723d2ant_update_wifi_channel_info(btcoexist, BTC_MEDIA_DISCONNECT);
	else
		halbtc8723d2ant_update_wifi_channel_info(btcoexist, BTC_MEDIA_CONNECT);
	
	if (coex_sta->under_lps && !coex_sta->force_lps_ctrl) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], RunCoexistMechanism(), wifi is under LPS !!!\n");
		BTC_TRACE(trace_buf);
		halbtc8723d2ant_action_wifi_native_lps(btcoexist);
		return;
	}

	if (coex_sta->bt_whck_test) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], BT is under WHCK TEST!!!\n");
		BTC_TRACE(trace_buf);
		halbtc8723d2ant_action_bt_whql_test(btcoexist);
		return;
	}

	if (coex_sta->bt_disabled) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				 "[BTCoex], BT is disabled!!!\n");
		BTC_TRACE(trace_buf);
		halbtc8723d2ant_action_coex_all_off(btcoexist);
		return;
	}

	if (coex_sta->c2h_bt_inquiry_page) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], BT is under inquiry/page scan !!\n");
		BTC_TRACE(trace_buf);
		halbtc8723d2ant_action_bt_inquiry(btcoexist);
		return;
	}

	if (coex_sta->is_setup_link || coex_sta->bt_relink_downcount != 0) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], BT is re-link !!!\n");
		BTC_TRACE(trace_buf);
		halbtc8723d2ant_action_bt_relink(btcoexist);
		return;
	}

	/* for P2P */
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
			   &wifi_link_status);
	num_of_wifi_link = wifi_link_status >> 16;

	if ((num_of_wifi_link >= 2) ||
		(wifi_link_status & WIFI_P2P_GO_CONNECTED)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"############# [BTCoex],  Multi-Port num_of_wifi_link = %d, wifi_link_status = 0x%x\n",
				num_of_wifi_link, wifi_link_status);
		BTC_TRACE(trace_buf);

		if (bt_link_info->bt_link_exist)
			miracast_plus_bt = TRUE;
		else
			miracast_plus_bt = FALSE;

		btcoexist->btc_set(btcoexist, BTC_SET_BL_MIRACAST_PLUS_BT,
				   &miracast_plus_bt);

		if (scan || link || roam || under_4way) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], scan = %d, link = %d, roam = %d 4way = %d!!!\n",
				    scan, link, roam, under_4way);
			BTC_TRACE(trace_buf);

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], wifi is under linkscan process + Multi-Port !!\n");
			BTC_TRACE(trace_buf);

			halbtc8723d2ant_action_wifi_linkscan_process(btcoexist);
		} else

			halbtc8723d2ant_action_wifi_multi_port(btcoexist);

		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);

	if (bt_hs_on) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"############# [BTCoex],  BT Is hs\n");
			BTC_TRACE(trace_buf);
			halbtc8723d2ant_action_bt_hs(btcoexist);
			return;
	}

	if ((coex_dm->bt_status == BT_8723D_2ANT_BT_STATUS_NON_CONNECTED_IDLE ||
	     coex_dm->bt_status == BT_8723D_2ANT_BT_STATUS_CONNECTED_IDLE)
	     && wifi_connected) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, bt idle!!.\n");
		BTC_TRACE(trace_buf);

		halbtc8723d2ant_action_bt_idle(btcoexist);
		return;
	}

	algorithm = halbtc8723d2ant_action_algorithm(btcoexist);
	coex_dm->cur_algorithm = algorithm;
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Algorithm = %d\n",
			coex_dm->cur_algorithm);
	BTC_TRACE(trace_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);

	if (scan || link || roam || under_4way) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], WiFi is under Link Process !!\n");
		BTC_TRACE(trace_buf);
		halbtc8723d2ant_action_wifi_linkscan_process(btcoexist);
	} else if (wifi_connected) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, wifi connected!!.\n");
		BTC_TRACE(trace_buf);
		halbtc8723d2ant_action_wifi_connected(btcoexist);

	} else {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, wifi not-connected!!.\n");
		BTC_TRACE(trace_buf);
		halbtc8723d2ant_action_wifi_not_connected(btcoexist);
	}
}

static
void halbtc8723d2ant_init_coex_dm(IN struct btc_coexist *btcoexist)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], Coex Mechanism Init!!\n");
	BTC_TRACE(trace_buf);

	halbtc8723d2ant_adjust_wl_tx_power(btcoexist, NORMAL_EXEC, 0xb2);
	halbtc8723d2ant_adjust_bt_tx_power(btcoexist, NORMAL_EXEC, 0);
	halbtc8723d2ant_adjust_wl_rx_gain(btcoexist, NORMAL_EXEC, FALSE);
	halbtc8723d2ant_adjust_bt_rx_gain(btcoexist, NORMAL_EXEC, FALSE);

	/* sw all off */
	halbtc8723d2ant_low_penalty_ra(btcoexist, NORMAL_EXEC, FALSE);

	coex_sta->pop_event_cnt = 0;
	coex_sta->cnt_remotenamereq = 0;
	coex_sta->cnt_reinit = 0;
	coex_sta->cnt_setuplink = 0;
	coex_sta->cnt_ignwlanact = 0;
	coex_sta->cnt_page = 0;
	coex_sta->cnt_roleswitch = 0;

	halbtc8723d2ant_query_bt_info(btcoexist);
}

static
void halbtc8723d2ant_init_hw_config(IN struct btc_coexist *btcoexist,
				    IN boolean wifi_only)
{
	u8	u8tmp0 = 0, u8tmp1 = 0;
	u32	vendor;
	u32	u32tmp0 = 0, u32tmp1 = 0, u32tmp2 = 0;
	u16 u16tmp1 = 0;
	u8 i = 0;


	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], 2Ant Init HW Config!!\n");
	BTC_TRACE(trace_buf);

#if BT_8723D_2ANT_COEX_DBG
	u32tmp1 = halbtc8723d2ant_ltecoex_indirect_read_reg(btcoexist,
			0x38);
	u32tmp2 = halbtc8723d2ant_ltecoex_indirect_read_reg(btcoexist,
			0x54);
	u16tmp1 = btcoexist->btc_read_2byte(btcoexist, 0x948);
	u8tmp1	= btcoexist->btc_read_1byte(btcoexist, 0x73);
	u8tmp0 =  btcoexist->btc_read_1byte(btcoexist, 0x67);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], ********** 0x67 = 0x%x, 0x948 = 0x%x, 0x73 = 0x%x(Before init_hw_config)\n",
		    u8tmp0, u16tmp1, u8tmp1);
	BTC_TRACE(trace_buf);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], **********0x38= 0x%x, 0x54= 0x%x (Before init_hw_config)\n",
		    u32tmp1, u32tmp2);
	BTC_TRACE(trace_buf);
#endif


	coex_sta->bt_coex_supported_feature = 0;
	coex_sta->bt_coex_supported_version = 0;
	coex_sta->bt_ble_scan_type = 0;
	coex_sta->bt_ble_scan_para[0] = 0;
	coex_sta->bt_ble_scan_para[1] = 0;
	coex_sta->bt_ble_scan_para[2] = 0;
	coex_sta->bt_reg_vendor_ac = 0xffff;
	coex_sta->bt_reg_vendor_ae = 0xffff;
	coex_sta->gnt_error_cnt = 0;
	coex_sta->bt_relink_downcount = 0;
	coex_sta->wl_rx_rate = BTC_UNKNOWN;

	for (i = 0; i <= 9; i++)
		coex_sta->bt_afh_map[i] = 0;

	/* 0xf0[15:12] --> kt_ver */
	coex_sta->kt_ver = (btcoexist->btc_read_1byte(btcoexist,
				 0xf1) & 0xf0) >> 4;

	coex_sta->dis_ver_info_cnt = 0;

	/* default isolation = 15dB */
	coex_sta->isolation_btween_wb = BT_8723D_2ANT_DEFAULT_ISOLATION;
	halbtc8723d2ant_coex_switch_threshold(btcoexist,
					      coex_sta->isolation_btween_wb);

	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x550, 0x8,
					   0x1);  /* enable TBTT nterrupt */

	/* BT report packet sample rate	 */
	btcoexist->btc_write_1byte(btcoexist, 0x790, 0x5);

	/* Init 0x778 = 0x1 for 2-Ant */
	btcoexist->btc_write_1byte(btcoexist, 0x778, 0x1);

	/* Enable PTA (3-wire function form BT side) */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x40, 0x20, 0x1);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x41, 0x02, 0x1);

	/* Enable PTA (tx/rx signal form WiFi side) */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4c6, 0x10, 0x1);

	halbtc8723d2ant_enable_gnt_to_gpio(btcoexist, TRUE);

	/* Enable counter statistics */
	btcoexist->btc_write_1byte(btcoexist, 0x76e,
			   0x4); /* 0x76e[3] =1, WLAN_Act control by PTA */

	/* WLAN_Tx by GNT_WL  0x950[29] = 0 */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x953, 0x20, 0x0);

	psd_scan->ant_det_is_ant_det_available = TRUE;

	if (coex_sta->is_rf_state_off) {

		halbtc8723d2ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO,
					     FORCE_EXEC,
					     BT_8723D_2ANT_PHASE_WLAN_OFF);

		btcoexist->stop_coex_dm = TRUE;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], **********  halbtc8723d2ant_init_hw_config (RF Off)**********\n");
		BTC_TRACE(trace_buf);
	} else if (wifi_only) {
		coex_sta->concurrent_rx_mode_on = FALSE;
		/* Path config	 */
		/* Set Antenna Path */
		halbtc8723d2ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_AUTO,
					     FORCE_EXEC,
					     BT_8723D_2ANT_PHASE_WLANONLY_INIT);

		btcoexist->stop_coex_dm = TRUE;
	} else {
		coex_sta->concurrent_rx_mode_on = TRUE;
		/* btcoexist->btc_write_1byte_bitmask(btcoexist, 0x953, 0x2, 0x1); */

		/* RF 0x1[0] = 0->Set GNT_WL_RF_Rx always = 1 for con-current Rx */
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0x1, 0x0);

		/* Path config	 */
		halbtc8723d2ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_AUTO,
					     FORCE_EXEC,
					     BT_8723D_2ANT_PHASE_COEX_INIT);

		btcoexist->stop_coex_dm = FALSE;
	}

	halbtc8723d2ant_coex_table_with_type(btcoexist, FORCE_EXEC, 0);

	halbtc8723d2ant_ps_tdma(btcoexist, FORCE_EXEC, FALSE, 0);
}

#ifdef PLATFORM_WINDOWS
#pragma optimize("", off)
#endif

/* ************************************************************
 * work around function start with wa_halbtc8723d2ant_
 * ************************************************************
 * ************************************************************
 * extern function start with ex_halbtc8723d2ant_
 * ************************************************************ */
void ex_halbtc8723d2ant_power_on_setting(IN struct btc_coexist *btcoexist)
{
	struct  btc_board_info	*board_info = &btcoexist->board_info;
	u8 u8tmp = 0x0;
	u16 u16tmp = 0x0;
	u32	value = 0;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"xxxxxxxxxxxxxxxx Execute 8723d 2-Ant PowerOn Setting xxxxxxxxxxxxxxxx!!\n");
	BTC_TRACE(trace_buf);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "Ant Det Finish = %s, Ant Det Number  = %d\n",
		    (board_info->btdm_ant_det_finish ? "Yes" : "No"),
		    board_info->btdm_ant_num_by_ant_det);
	BTC_TRACE(trace_buf);


	btcoexist->stop_coex_dm = TRUE;
	coex_sta->is_rf_state_off = FALSE;
	psd_scan->ant_det_is_ant_det_available = FALSE;

	/* enable BB, REG_SYS_FUNC_EN such that we can write BB Register correctly. */
	u16tmp = btcoexist->btc_read_2byte(btcoexist, 0x2);
	btcoexist->btc_write_2byte(btcoexist, 0x2, u16tmp | BIT(0) | BIT(1));


	/* Local setting bit define */
	/*	BIT0: "0" for no antenna inverse; "1" for antenna inverse  */
	/*	BIT1: "0" for internal switch; "1" for external switch */
	/*	BIT2: "0" for one antenna; "1" for two antenna */
	/* NOTE: here default all internal switch and 1-antenna ==> BIT1=0 and BIT2=0 */

	/* Set path control to WL */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x80, 0x1);
	btcoexist->btc_write_2byte(btcoexist, 0x948, 0x0);

	/* Check efuse 0xc3[6] for Single Antenna Path */
	if (board_info->single_ant_path == 0) {
		/* set to S1 */
		board_info->btdm_ant_pos = BTC_ANTENNA_AT_MAIN_PORT;
		u8tmp = 4;
		value = 1;
	} else if (board_info->single_ant_path == 1) {
		/* set to S0 */
		board_info->btdm_ant_pos = BTC_ANTENNA_AT_AUX_PORT;
		u8tmp = 5;
		value = 0;
	}

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], ********** (Power On) single_ant_path  = %d, btdm_ant_pos = %d **********\n",
		    board_info->single_ant_path , board_info->btdm_ant_pos);
	BTC_TRACE(trace_buf);

	/* Set Antenna Path to BT side */
	halbtc8723d2ant_set_ant_path(btcoexist,
				     BTC_ANT_PATH_AUTO,
				     FORCE_EXEC,
				     BT_8723D_1ANT_PHASE_COEX_POWERON);

	/* Write Single Antenna Position to Registry to tell BT for 872db. This line can be removed
	since BT EFuse also add "single antenna position" in EFuse for 8723d*/
	btcoexist->btc_set(btcoexist, BTC_SET_ACT_ANTPOSREGRISTRY_CTRL,
			   &value);

	/* Save"single antenna position" info in Local register setting for FW reading, because FW may not ready at  power on */
	if (btcoexist->chip_interface == BTC_INTF_PCI)
		btcoexist->btc_write_local_reg_1byte(btcoexist, 0x3e0, u8tmp);
	else if (btcoexist->chip_interface == BTC_INTF_USB)
		btcoexist->btc_write_local_reg_1byte(btcoexist, 0xfe08, u8tmp);
	else if (btcoexist->chip_interface == BTC_INTF_SDIO)
		btcoexist->btc_write_local_reg_1byte(btcoexist, 0x60, u8tmp);

	/* enable GNT_WL/GNT_BT debug signal to GPIO14/15 */
	halbtc8723d2ant_enable_gnt_to_gpio(btcoexist, TRUE);

#if BT_8723D_2ANT_COEX_DBG

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], **********  LTE coex Reg 0x38 (Power-On) = 0x%x**********\n",
		    halbtc8723d2ant_ltecoex_indirect_read_reg(btcoexist, 0x38));
	BTC_TRACE(trace_buf);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], **********  MAC Reg 0x70/ BB Reg 0x948 (Power-On) = 0x%x / 0x%x**********\n",
		    btcoexist->btc_read_4byte(btcoexist, 0x70),
		    btcoexist->btc_read_2byte(btcoexist, 0x948));
	BTC_TRACE(trace_buf);

#endif
}

void ex_halbtc8723d2ant_pre_load_firmware(IN struct btc_coexist *btcoexist)
{
	struct  btc_board_info	*board_info = &btcoexist->board_info;
	u8 u8tmp = 0x4; /* Set BIT2 by default since it's 2ant case */

	/* */
	/* S0 or S1 setting and Local register setting(By the setting fw can get ant number, S0/S1, ... info) */
	/* Local setting bit define */
	/*	BIT0: "0" for no antenna inverse; "1" for antenna inverse  */
	/*	BIT1: "0" for internal switch; "1" for external switch */
	/*	BIT2: "0" for one antenna; "1" for two antenna */
	/* NOTE: here default all internal switch and 1-antenna ==> BIT1=0 and BIT2=0 */
	if (btcoexist->chip_interface == BTC_INTF_USB) {
		/* fixed at S0 for USB interface */
		u8tmp |= 0x1;	/* antenna inverse */
		btcoexist->btc_write_local_reg_1byte(btcoexist, 0xfe08, u8tmp);
	} else {
		/* for PCIE and SDIO interface, we check efuse 0xc3[6] */
		if (board_info->single_ant_path == 0) {
		} else if (board_info->single_ant_path == 1) {
			/* set to S0 */
			u8tmp |= 0x1;	/* antenna inverse */
		}

		if (btcoexist->chip_interface == BTC_INTF_PCI)
			btcoexist->btc_write_local_reg_1byte(btcoexist, 0x3e0,
							     u8tmp);
		else if (btcoexist->chip_interface == BTC_INTF_SDIO)
			btcoexist->btc_write_local_reg_1byte(btcoexist, 0x60,
							     u8tmp);
	}
}


void ex_halbtc8723d2ant_init_hw_config(IN struct btc_coexist *btcoexist,
				       IN boolean wifi_only)
{
	halbtc8723d2ant_init_hw_config(btcoexist, wifi_only);
}

void ex_halbtc8723d2ant_init_coex_dm(IN struct btc_coexist *btcoexist)
{

	halbtc8723d2ant_init_coex_dm(btcoexist);
}

void ex_halbtc8723d2ant_display_coex_info(IN struct btc_coexist *btcoexist)
{
	struct  btc_board_info		*board_info = &btcoexist->board_info;
	struct  btc_bt_link_info	*bt_link_info = &btcoexist->bt_link_info;
	u8				*cli_buf = btcoexist->cli_buf;
	u8				u8tmp[4], i, ps_tdma_case = 0;
	u32				u32tmp[4];
	u16				u16tmp[4];
	u32				fa_ofdm, fa_cck, cca_ofdm, cca_cck, bt_coex_ver = 0;
	u32				fw_ver = 0, bt_patch_ver = 0;
	static u8			pop_report_in_10s = 0, cnt = 0;
	u32			phyver = 0;
	boolean			lte_coex_on = FALSE;

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n ============[BT Coexist info 8723D]============");
	CL_PRINTF(cli_buf);

	if (btcoexist->manual_control) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			"\r\n ============[Under Manual Control]============");
		CL_PRINTF(cli_buf);
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n ==========================================");
		CL_PRINTF(cli_buf);
	}

	if (!coex_sta->bt_disabled) {

		if (coex_sta->bt_coex_supported_feature == 0)
			btcoexist->btc_get(btcoexist, BTC_GET_U4_SUPPORTED_FEATURE,
						&coex_sta->bt_coex_supported_feature);

		if ((coex_sta->bt_coex_supported_version == 0) ||
			 (coex_sta->bt_coex_supported_version == 0xffff))
			btcoexist->btc_get(btcoexist, BTC_GET_U4_SUPPORTED_VERSION,
						&coex_sta->bt_coex_supported_version);

		if (coex_sta->bt_reg_vendor_ac == 0xffff)
			coex_sta->bt_reg_vendor_ac = (u16)(
						btcoexist->btc_get_bt_reg(btcoexist, 3,
						0xac) & 0xffff);

		if (coex_sta->bt_reg_vendor_ae == 0xffff)
			coex_sta->bt_reg_vendor_ae = (u16)(
						btcoexist->btc_get_bt_reg(btcoexist, 3,
						0xae) & 0xffff);

		btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER,
						&bt_patch_ver);
		btcoexist->bt_info.bt_get_fw_ver = bt_patch_ver;

		if (coex_sta->num_of_profile > 0) {
			cnt++;

			if (cnt >= 3) {
				btcoexist->btc_get_bt_afh_map_from_bt(btcoexist, 0,
					&coex_sta->bt_afh_map[0]);
				cnt = 0;
			}
		}
	}

	if (psd_scan->ant_det_try_count == 0) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %s/ %s",
			   "Ant PG Num/ Mech/ Pos",
			   board_info->pg_ant_num,
			   (board_info->btdm_ant_num == 1 ?
			   "Shared" : "Non-Shared"),
			   (board_info->btdm_ant_pos == 1 ?
			   "S1" : "S0"));
		CL_PRINTF(cli_buf);
	} else {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			"\r\n %-35s = %d/ %d/ %s  (retry=%d/fail=%d/result=%d)",
			   "Ant PG Num/ Mech(Ant_Det)/ Pos",
			   board_info->pg_ant_num,
			   board_info->btdm_ant_num_by_ant_det,
			   (board_info->btdm_ant_pos == 1 ? "S1" : "S0"),
			   psd_scan->ant_det_try_count,
			   psd_scan->ant_det_fail_count,
			   psd_scan->ant_det_result);
		CL_PRINTF(cli_buf);

		if (board_info->btdm_ant_det_finish) {

			if (psd_scan->ant_det_result != 12)
				CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
					   "\r\n %-35s = %s",
					   "Ant Det PSD Value",
					   psd_scan->ant_det_peak_val);
			else
				CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
					   "\r\n %-35s = %d",
					   "Ant Det PSD Value",
					   psd_scan->ant_det_psd_scan_peak_val
					   / 100);
			CL_PRINTF(cli_buf);
		}
	}

	if (board_info->ant_det_result_five_complete) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %d/ %d",
			   "AntDet(Registry) Num/PSD Value",
			   board_info->btdm_ant_num_by_ant_det,
			   (board_info->antdetval & 0x7f));
		CL_PRINTF(cli_buf);
	}


	bt_patch_ver = btcoexist->bt_info.bt_get_fw_ver;
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
	phyver = btcoexist->btc_get_bt_phydm_version(btcoexist);

	bt_coex_ver = coex_sta->bt_coex_supported_version & 0xff;

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
				"\r\n %-35s = %d_%02x/ 0x%02x/ 0x%02x (%s)",
				"CoexVer WL/  BT_Desired/ BT_Report",
				glcoex_ver_date_8723d_2ant, glcoex_ver_8723d_2ant,
				glcoex_ver_btdesired_8723d_2ant,
				bt_coex_ver,
				(bt_coex_ver == 0xff ? "Unknown" :
				(coex_sta->bt_disabled ? "BT-disable" :
				(bt_coex_ver >= glcoex_ver_btdesired_8723d_2ant ?
				"Match" : "Mis-Match"))));
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ v%d/ %c",
		   "W_FW/ B_FW/ Phy/ Kt",
		   fw_ver, bt_patch_ver, phyver,
		   coex_sta->kt_ver + 65);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x ",
		   "Wifi channel informed to BT",
		   coex_dm->wifi_chnl_info[0], coex_dm->wifi_chnl_info[1],
		   coex_dm->wifi_chnl_info[2]);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d ",
		   "Isolation/WL_Thres/BT_Thres",
		   coex_sta->isolation_btween_wb,
		   coex_sta->wifi_coex_thres,
		   coex_sta->bt_coex_thres);
	CL_PRINTF(cli_buf);

	/* wifi status */
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
		   "============[Wifi Status]============");
	CL_PRINTF(cli_buf);
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_WIFI_STATUS);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
		   "============[BT Status]============");
	CL_PRINTF(cli_buf);

	pop_report_in_10s++;
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %s/ %ddBm/ %d/ %d",
		   "BT status/ rssi/ retryCnt/ popCnt",
		   ((coex_sta->bt_disabled) ? ("disabled") :	((
			   coex_sta->c2h_bt_inquiry_page) ? ("inquiry-page")
			   : ((BT_8723D_2ANT_BT_STATUS_NON_CONNECTED_IDLE ==
			       coex_dm->bt_status) ? "non-connected idle" :
		((BT_8723D_2ANT_BT_STATUS_CONNECTED_IDLE == coex_dm->bt_status)
				       ? "connected-idle" : "busy")))),
		   coex_sta->bt_rssi - 100, coex_sta->bt_retry_cnt,
		   coex_sta->pop_event_cnt);
	CL_PRINTF(cli_buf);

	if (pop_report_in_10s >= 5) {
		coex_sta->pop_event_cnt = 0;
		pop_report_in_10s = 0;
	}

	if (coex_sta->num_of_profile != 0)
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
				"\r\n %-35s = %s%s%s%s%s (multilink = %d)",
				"Profiles",
				((bt_link_info->a2dp_exist) ?
				((coex_sta->is_bt_a2dp_sink) ? "A2DP sink," :
				"A2DP,") : ""),
				((bt_link_info->sco_exist) ?  "HFP," : ""),
				((bt_link_info->hid_exist) ?
				((coex_sta->is_hid_rcu) ? "HID(RCU)" :
				((coex_sta->hid_busy_num >= 2) ? "HID(4/18)," :
				"HID(2/18),")) : ""),
				((bt_link_info->pan_exist) ?
				((coex_sta->is_bt_opp_exist) ? "OPP," : "PAN,") : ""),
				((coex_sta->voice_over_HOGP) ? "Voice" : ""),
				coex_sta->is_bt_multi_link);
	else
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = None", "Profiles");

	CL_PRINTF(cli_buf);


	if (bt_link_info->a2dp_exist) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %s/ %d/ 0x%x/ 0x%x",
			   "CQDDR/Bitpool/V_ID/D_name",
			   ((coex_sta->is_A2DP_3M) ? "On" : "Off"),
			   coex_sta->a2dp_bit_pool,
			   coex_sta->bt_a2dp_vendor_id,
			   coex_sta->bt_a2dp_device_name);
		CL_PRINTF(cli_buf);
	}

	if (bt_link_info->hid_exist) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d",
			   "HID PairNum",
			   coex_sta->hid_pair_cnt
			  );
		CL_PRINTF(cli_buf);
	}

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d/ %s/ 0x%x",
				"Role/RoleSwCnt/IgnWlact/Feature",
				((bt_link_info->slave_role) ? "Slave" : "Master"),
				coex_sta->cnt_roleswitch,
				((coex_dm->cur_ignore_wlan_act) ? "Yes" : "No"),
				coex_sta->bt_coex_supported_feature);
	CL_PRINTF(cli_buf);

	if ((coex_sta->bt_ble_scan_type & 0x7) != 0x0) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			"\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x",
			"BLEScan Type/TV/Init/Ble",
			coex_sta->bt_ble_scan_type,
			(coex_sta->bt_ble_scan_type & 0x1 ?
			coex_sta->bt_ble_scan_para[0] : 0x0),
			(coex_sta->bt_ble_scan_type & 0x2 ?
			coex_sta->bt_ble_scan_para[1] : 0x0),
			(coex_sta->bt_ble_scan_type & 0x4 ?
			coex_sta->bt_ble_scan_para[2] : 0x0));
		CL_PRINTF(cli_buf);
	}

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d/ %d",
		   "ReInit/ReLink/IgnWlact/Page/NameReq",
		   coex_sta->cnt_reinit,
		   coex_sta->cnt_setuplink,
		   coex_sta->cnt_ignwlanact,
		   coex_sta->cnt_page,
		   coex_sta->cnt_remotenamereq);
	CL_PRINTF(cli_buf);

	halbtc8723d2ant_read_score_board(btcoexist,	&u16tmp[0]);

	if ((coex_sta->bt_reg_vendor_ae == 0xffff) ||
	    (coex_sta->bt_reg_vendor_ac == 0xffff))
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = x/ x/ 0x%04x",
			   "0xae[4]/0xac[1:0]/Scoreboard(B->W)", u16tmp[0]);
	else
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = 0x%x/ 0x%x/ 0x%04x",
			   "0xae[4]/0xac[1:0]/Scoreboard(B->W)",
			   (int)((coex_sta->bt_reg_vendor_ae & BIT(4)) >> 4),
			   coex_sta->bt_reg_vendor_ac & 0x3, u16tmp[0]);
	CL_PRINTF(cli_buf);

	if (coex_sta->num_of_profile > 0) {

		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			"\r\n %-35s = %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x",
			"AFH MAP",
			coex_sta->bt_afh_map[0],
			coex_sta->bt_afh_map[1],
			coex_sta->bt_afh_map[2],
			coex_sta->bt_afh_map[3],
			coex_sta->bt_afh_map[4],
			coex_sta->bt_afh_map[5],
			coex_sta->bt_afh_map[6],
			coex_sta->bt_afh_map[7],
			coex_sta->bt_afh_map[8],
			coex_sta->bt_afh_map[9]
			   );
		CL_PRINTF(cli_buf);
	}

	for (i = 0; i < BT_INFO_SRC_8723D_2ANT_MAX; i++) {
		if (coex_sta->bt_info_c2h_cnt[i]) {
			CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
				"\r\n %-35s = %02x %02x %02x %02x %02x %02x %02x (%d)",
				   glbt_info_src_8723d_2ant[i],
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

	/* Sw mechanism	 */
	if (btcoexist->manual_control)
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
			"============[mechanism] (before Manual)============");
	else
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
			   "============[Mechanism]============");

	CL_PRINTF(cli_buf);


	ps_tdma_case = coex_dm->cur_ps_tdma;

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %02x %02x %02x %02x %02x (case-%d, %s, %s, Timer:%d)",
		   "TDMA",
		   coex_dm->ps_tdma_para[0], coex_dm->ps_tdma_para[1],
		   coex_dm->ps_tdma_para[2], coex_dm->ps_tdma_para[3],
		   coex_dm->ps_tdma_para[4], ps_tdma_case,
		   (coex_dm->cur_ps_tdma_on ? "TDMA On" : "TDMA Off"),
		   (coex_dm->is_switch_to_1dot5_ant ? "1.5Ant" : "2Ant"),
		   coex_sta->tdma_timer_base);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x6c0);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x6c4);
	u32tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0x6c8);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %d/ 0x%x/ 0x%x/ 0x%x",
		   "Table/0x6c0/0x6c4/0x6c8",
		   coex_sta->coex_table_type, u32tmp[0], u32tmp[1], u32tmp[2]);
	CL_PRINTF(cli_buf);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x778);
	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x6cc);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%04x",
		   "0x778/0x6cc/Scoreboard(W->B)",
		   u8tmp[0], u32tmp[0], coex_sta->score_board_WB);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s",
		   "AntDiv/ ForceLPS",
		   ((board_info->ant_div_cfg) ? "On" : "Off"),
		   ((coex_sta->force_lps_ctrl) ? "On" : "Off"));
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "WL_Pwr/ BT_Pwr", coex_dm->cur_fw_dac_swing_lvl,
		   coex_dm->cur_bt_dec_pwr_lvl);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "BT_Empty/BT_Late",
		   coex_sta->wl_fw_dbg_info[4],
		   coex_sta->wl_fw_dbg_info[5]);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %s",
		   "TDMA_Togg_cnt/WL5ms_cnt/WL5ms_off",
		   coex_sta->wl_fw_dbg_info[6], coex_sta->wl_fw_dbg_info[7],
		   ((coex_sta->is_no_wl_5ms_extend) ? "Yes" : "No"));
	CL_PRINTF(cli_buf);

	u32tmp[0] = halbtc8723d2ant_ltecoex_indirect_read_reg(btcoexist, 0x38);
	lte_coex_on = ((u32tmp[0] & BIT(7)) >> 7) ?  TRUE : FALSE;

	if (lte_coex_on) {

		u32tmp[0] = halbtc8723d2ant_ltecoex_indirect_read_reg(btcoexist,
				0xa0);
		u32tmp[1] = halbtc8723d2ant_ltecoex_indirect_read_reg(btcoexist,
				0xa4);

		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
			   "LTE Coex  Table W_L/B_L",
			   u32tmp[0] & 0xffff, u32tmp[1] & 0xffff);
		CL_PRINTF(cli_buf);

		u32tmp[0] = halbtc8723d2ant_ltecoex_indirect_read_reg(btcoexist,
				0xa8);
		u32tmp[1] = halbtc8723d2ant_ltecoex_indirect_read_reg(btcoexist,
				0xac);
		u32tmp[2] = halbtc8723d2ant_ltecoex_indirect_read_reg(btcoexist,
				0xb0);
		u32tmp[3] = halbtc8723d2ant_ltecoex_indirect_read_reg(btcoexist,
				0xb4);

		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x",
			   "LTE Break Table W_L/B_L/L_W/L_B",
			   u32tmp[0] & 0xffff, u32tmp[1] & 0xffff,
			   u32tmp[2] & 0xffff, u32tmp[3] & 0xffff);
		CL_PRINTF(cli_buf);

	}

	/* Hw setting		 */
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
		   "============[Hw setting]============");
	CL_PRINTF(cli_buf);

	u32tmp[0] = halbtc8723d2ant_ltecoex_indirect_read_reg(btcoexist, 0x38);
	u32tmp[1] = halbtc8723d2ant_ltecoex_indirect_read_reg(btcoexist, 0x54);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x73);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s",
		   "LTE Coex/Path Owner",
		   ((lte_coex_on) ? "On" : "Off") ,
		   ((u8tmp[0] & BIT(2)) ? "WL" : "BT"));
	CL_PRINTF(cli_buf);

	if (lte_coex_on) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %d/ %d/ %d/ %d",
			   "LTE 3Wire/OPMode/UART/UARTMode",
			   (int)((u32tmp[0] & BIT(6)) >> 6),
			   (int)((u32tmp[0] & (BIT(5) | BIT(4))) >> 4),
			   (int)((u32tmp[0] & BIT(3)) >> 3),
			   (int)(u32tmp[0] & (BIT(2) | BIT(1) | BIT(0))));
		CL_PRINTF(cli_buf);

		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
			   "LTE_Busy/UART_Busy",
			(int)((u32tmp[1] & BIT(1)) >> 1), (int)(u32tmp[1] & BIT(0)));
		CL_PRINTF(cli_buf);
	}

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %s (BB:%s)/ %s (BB:%s)/ %s (gnt_err = %d)",
			   "GNT_WL_Ctrl/GNT_BT_Ctrl/Dbg",
			   ((u32tmp[0] & BIT(12)) ? "SW" : "HW"),
			   ((u32tmp[0] & BIT(8)) ? "SW" : "HW"),
			   ((u32tmp[0] & BIT(14)) ? "SW" : "HW"),
			   ((u32tmp[0] & BIT(10)) ?  "SW" : "HW"),
			   ((u8tmp[0] & BIT(3)) ? "On" : "Off"),
			   coex_sta->gnt_error_cnt);
		CL_PRINTF(cli_buf);

		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "GNT_WL/GNT_BT",
		   (int)((u32tmp[1] & BIT(2)) >> 2),
		   (int)((u32tmp[1] & BIT(3)) >> 3));
	CL_PRINTF(cli_buf);

	u16tmp[0] = btcoexist->btc_read_2byte(btcoexist, 0x948);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x67);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x883);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x948/0x67[7]/0x883",
		   u16tmp[0], (int)((u8tmp[0] & BIT(7)) >> 7),
		   u8tmp[1]);
	CL_PRINTF(cli_buf);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x964);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x864);
	u8tmp[2] = btcoexist->btc_read_1byte(btcoexist, 0xab7);
	u8tmp[3] = btcoexist->btc_read_1byte(btcoexist, 0xa01);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x",
		   "0x964[1]/0x864[0]/0xab7[5]/0xa01[7]",
		   (int)((u8tmp[0] & BIT(1)) >> 1), (int)((u8tmp[1] & BIT(0))),
		   (int)((u8tmp[2] & BIT(3)) >> 3),
		   (int)((u8tmp[3] & BIT(7)) >> 7));
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x430);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x434);
	u16tmp[0] = btcoexist->btc_read_2byte(btcoexist, 0x42a);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x426);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x45e);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x/ 0x%x",
		   "0x430/0x434/0x42a/0x426/0x45e[3]",
		   u32tmp[0], u32tmp[1], u16tmp[0], u8tmp[0],
		   (int)((u8tmp[1] & BIT(3)) >> 3));
	CL_PRINTF(cli_buf);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x4c6);
	u16tmp[0] = btcoexist->btc_read_2byte(btcoexist, 0x40);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x", "0x4c6[4]/0x40[5]",
		   (int)((u8tmp[0] & BIT(4)) >> 4),
		   (int)((u16tmp[0] & BIT(5)) >> 5));
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x550);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x522);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x953);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ %s",
		   "0x550/0x522/4-RxAGC",
		   u32tmp[0], u8tmp[0], (u8tmp[1] & 0x2) ? "On" : "Off");
	CL_PRINTF(cli_buf);

	fa_ofdm = btcoexist->btc_phydm_query_PHY_counter(btcoexist, PHYDM_INFO_FA_OFDM);
	fa_cck = btcoexist->btc_phydm_query_PHY_counter(btcoexist, PHYDM_INFO_FA_CCK);
	cca_ofdm = btcoexist->btc_phydm_query_PHY_counter(btcoexist, PHYDM_INFO_CCA_OFDM);
	cca_cck = btcoexist->btc_phydm_query_PHY_counter(btcoexist, PHYDM_INFO_CCA_CCK);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x",
		   "CCK-CCA/CCK-FA/OFDM-CCA/OFDM-FA",
		   cca_cck, fa_cck, cca_ofdm, fa_ofdm);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d (Rx_rate Data/RTS= %d/%d)",
		   "CRC_OK CCK/11g/11n/11ac",
		   coex_sta->crc_ok_cck, coex_sta->crc_ok_11g,
		   coex_sta->crc_ok_11n, coex_sta->crc_ok_11n_vht,
		   coex_sta->wl_rx_rate, coex_sta->wl_rts_rx_rate);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d",
		   "CRC_Err CCK/11g/11n/11n-agg",
		   coex_sta->crc_err_cck, coex_sta->crc_err_11g,
		   coex_sta->crc_err_11n, coex_sta->crc_err_11n_vht);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s/ %s/ %d",
		   "WlHiPri/ Locking/ Locked/ Noisy",
		   (coex_sta->wifi_is_high_pri_task ? "Yes" : "No"),
		   (coex_sta->cck_lock ? "Yes" : "No"),
		   (coex_sta->cck_lock_ever ? "Yes" : "No"),
		   coex_sta->wl_noisy_level);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d %s",
		   "0x770(Hi-pri rx/tx)",
		   coex_sta->high_priority_rx, coex_sta->high_priority_tx,
		   (coex_sta->is_hipri_rx_overhead ? "(scan overhead!!)" : ""));
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d %s",
		   "0x774(Lo-pri rx/tx)",
		   coex_sta->low_priority_rx, coex_sta->low_priority_tx,
		   (bt_link_info->slave_role ? "(Slave!!)" : (
		   coex_sta->is_tdma_btautoslot_hang ? "(auto-slot hang!!)" : "")));
	CL_PRINTF(cli_buf);

	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_COEX_STATISTICS);
}


void ex_halbtc8723d2ant_ips_notify(IN struct btc_coexist *btcoexist, IN u8 type)
{
	if (btcoexist->manual_control ||	btcoexist->stop_coex_dm)
		return;

	if (BTC_IPS_ENTER == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], IPS ENTER notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_ips = TRUE;
		coex_sta->under_lps = FALSE;

		halbtc8723d2ant_post_state_to_bt(btcoexist,
				BT_8723D_2ANT_SCOREBOARD_ACTIVE |
				BT_8723D_2ANT_SCOREBOARD_ONOFF |
				BT_8723D_2ANT_SCOREBOARD_SCAN |
				BT_8723D_2ANT_SCOREBOARD_UNDERTEST,
				FALSE);

		halbtc8723d2ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_AUTO,
					     FORCE_EXEC,
					     BT_8723D_2ANT_PHASE_WLAN_OFF);

		halbtc8723d2ant_action_coex_all_off(btcoexist);
	} else if (BTC_IPS_LEAVE == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], IPS LEAVE notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_ips = FALSE;
#if 0
		halbtc8723d2ant_post_state_to_bt(btcoexist,
					 BT_8723D_2ANT_SCOREBOARD_ACTIVE, TRUE);

		halbtc8723d2ant_post_state_to_bt(btcoexist,
					BT_8723D_2ANT_SCOREBOARD_ONOFF, TRUE);
#endif
		halbtc8723d2ant_init_hw_config(btcoexist, FALSE);
		halbtc8723d2ant_init_coex_dm(btcoexist);
		halbtc8723d2ant_query_bt_info(btcoexist);
	}
}

void ex_halbtc8723d2ant_lps_notify(IN struct btc_coexist *btcoexist, IN u8 type)
{
	static boolean  pre_force_lps_on = FALSE;

	if (btcoexist->manual_control || btcoexist->stop_coex_dm)
		return;

	if (BTC_LPS_ENABLE == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], LPS ENABLE notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_lps = TRUE;
		coex_sta->under_ips = FALSE;

		if (coex_sta->force_lps_ctrl == TRUE) { /* LPS No-32K */
			/* Write WL "Active" in Score-board for PS-TDMA */
			pre_force_lps_on = TRUE;
			halbtc8723d2ant_post_state_to_bt(btcoexist,
					 BT_8723D_2ANT_SCOREBOARD_ACTIVE, TRUE);

		} else { /* LPS-32K, need check if this h2c 0x71 can work?? (2015/08/28) */
			/* Write WL "Non-Active" in Score-board for Native-PS */
			pre_force_lps_on = FALSE;
			halbtc8723d2ant_post_state_to_bt(btcoexist,
				 BT_8723D_2ANT_SCOREBOARD_ACTIVE, FALSE);

			halbtc8723d2ant_action_wifi_native_lps(btcoexist);
		}

	} else if (BTC_LPS_DISABLE == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], LPS DISABLE notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_lps = FALSE;

		halbtc8723d2ant_post_state_to_bt(btcoexist,
					 BT_8723D_2ANT_SCOREBOARD_ACTIVE, TRUE);

		if ((!pre_force_lps_on) && (!coex_sta->force_lps_ctrl))
			halbtc8723d2ant_query_bt_info(btcoexist);
	}
}

void ex_halbtc8723d2ant_scan_notify(IN struct btc_coexist *btcoexist,
				    IN u8 type)
{
	u32	u32tmp;
	u8	u8tmpa, u8tmpb;
	boolean	wifi_connected = FALSE;


	if (btcoexist->manual_control ||
	    btcoexist->stop_coex_dm)
		return;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);

	/*  this can't be removed for RF off_on event, or BT would dis-connect */

	if (BTC_SCAN_START == type) {

		if (!wifi_connected)
			coex_sta->wifi_is_high_pri_task = TRUE;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], SCAN START notify\n");
		BTC_TRACE(trace_buf);

		halbtc8723d2ant_post_state_to_bt(btcoexist,
					BT_8723D_2ANT_SCOREBOARD_ACTIVE |
					BT_8723D_2ANT_SCOREBOARD_SCAN |
					BT_8723D_2ANT_SCOREBOARD_ONOFF,
					TRUE);

		halbtc8723d2ant_query_bt_info(btcoexist);

		halbtc8723d2ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_AUTO,
					     FORCE_EXEC,
					     BT_8723D_2ANT_PHASE_2G_RUNTIME);

		halbtc8723d2ant_run_coexist_mechanism(btcoexist);

	} else if (BTC_SCAN_FINISH == type) {

		coex_sta->wifi_is_high_pri_task = FALSE;

		btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM,
				   &coex_sta->scan_ap_num);

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], SCAN FINISH notify  (Scan-AP = %d)\n",
			    coex_sta->scan_ap_num);
		BTC_TRACE(trace_buf);

		halbtc8723d2ant_post_state_to_bt(btcoexist,
					 BT_8723D_2ANT_SCOREBOARD_SCAN, FALSE);

		halbtc8723d2ant_run_coexist_mechanism(btcoexist);
	}

}

void ex_halbtc8723d2ant_connect_notify(IN struct btc_coexist *btcoexist,
				       IN u8 type)
{
	if (btcoexist->manual_control ||
	    btcoexist->stop_coex_dm)
		return;

	if (BTC_ASSOCIATE_START == type) {

		coex_sta->wifi_is_high_pri_task = TRUE;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], CONNECT START notify\n");
		BTC_TRACE(trace_buf);

		halbtc8723d2ant_post_state_to_bt(btcoexist,
					 BT_8723D_2ANT_SCOREBOARD_ACTIVE |
					 BT_8723D_2ANT_SCOREBOARD_SCAN |
					 BT_8723D_2ANT_SCOREBOARD_ONOFF,
					 TRUE);

		halbtc8723d2ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_AUTO,
					     FORCE_EXEC,
					     BT_8723D_2ANT_PHASE_2G_RUNTIME);

		halbtc8723d2ant_run_coexist_mechanism(btcoexist);
		/* To keep TDMA case during connect process,
		to avoid changed by Btinfo and runcoexmechanism */
		coex_sta->freeze_coexrun_by_btinfo = TRUE;

		coex_dm->arp_cnt = 0;
		coex_sta->connect_ap_period_cnt = 2;

	} else if (BTC_ASSOCIATE_FINISH == type) {

		coex_sta->wifi_is_high_pri_task = FALSE;
		coex_sta->freeze_coexrun_by_btinfo = FALSE;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], CONNECT FINISH notify\n");
		BTC_TRACE(trace_buf);

		halbtc8723d2ant_run_coexist_mechanism(btcoexist);
	}
}

void ex_halbtc8723d2ant_media_status_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	u8			h2c_parameter[3] = {0};
	u32			wifi_bw;
	u8			wifi_central_chnl;
	u8			ap_num = 0;
	boolean		wifi_under_b_mode = FALSE;

	if (btcoexist->manual_control ||
	    btcoexist->stop_coex_dm)
		return;

	if (BTC_MEDIA_CONNECT == type) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], MEDIA connect notify\n");
		BTC_TRACE(trace_buf);

		halbtc8723d2ant_post_state_to_bt(btcoexist,
					 BT_8723D_2ANT_SCOREBOARD_ACTIVE |
					 BT_8723D_2ANT_SCOREBOARD_ONOFF,
					 TRUE);

		halbtc8723d2ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_AUTO,
					     FORCE_EXEC,
					     BT_8723D_2ANT_PHASE_2G_RUNTIME);

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

	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], MEDIA disconnect notify\n");
		BTC_TRACE(trace_buf);

		btcoexist->btc_write_1byte(btcoexist, 0x6cd, 0x0); /* CCK Tx */
		btcoexist->btc_write_1byte(btcoexist, 0x6cf, 0x0); /* CCK Rx */

		halbtc8723d2ant_post_state_to_bt(btcoexist,
				 BT_8723D_2ANT_SCOREBOARD_ACTIVE, FALSE);
	}
	btcoexist->btc_get(btcoexist, BTC_GET_U1_IOT_PEER, &coex_sta->wl_iot_peer);
	halbtc8723d2ant_update_wifi_channel_info(btcoexist, type);
	
}

void ex_halbtc8723d2ant_specific_packet_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	boolean under_4way = FALSE;

	if (btcoexist->manual_control ||
	    btcoexist->stop_coex_dm)
		return;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS,
			   &under_4way);

	if (under_4way) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], specific Packet ---- under_4way!!\n");
		BTC_TRACE(trace_buf);

		coex_sta->wifi_is_high_pri_task = TRUE;
		coex_sta->specific_pkt_period_cnt = 2;

	} else if (BTC_PACKET_ARP == type) {

		coex_dm->arp_cnt++;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], specific Packet ARP notify -cnt = %d\n",
			    coex_dm->arp_cnt);
		BTC_TRACE(trace_buf);

	} else {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], specific Packet DHCP or EAPOL notify [Type = %d]\n",
			    type);
		BTC_TRACE(trace_buf);

		coex_sta->wifi_is_high_pri_task = TRUE;
		coex_sta->specific_pkt_period_cnt = 2;
	}

	if (coex_sta->wifi_is_high_pri_task) {
		halbtc8723d2ant_post_state_to_bt(btcoexist,
					 BT_8723D_2ANT_SCOREBOARD_ACTIVE, TRUE);
		halbtc8723d2ant_run_coexist_mechanism(btcoexist);
	}

}

void ex_halbtc8723d2ant_bt_info_notify(IN struct btc_coexist *btcoexist,
				       IN u8 *tmp_buf, IN u8 length)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	u8 i, rsp_source = 0;
	boolean	wifi_connected = FALSE;
	boolean	wifi_scan = FALSE, wifi_link = FALSE, wifi_roam = FALSE,
		wifi_busy = FALSE;
	static boolean is_scoreboard_scan = FALSE;

	if (psd_scan->is_antdet_running) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], bt_info_notify return for AntDet is running\n");
		BTC_TRACE(trace_buf);
		return;
	}

	rsp_source = tmp_buf[0] & 0xf;
	if (rsp_source >= BT_INFO_SRC_8723D_2ANT_MAX)
		rsp_source = BT_INFO_SRC_8723D_2ANT_WIFI_FW;
	coex_sta->bt_info_c2h_cnt[rsp_source]++;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], Bt_info[%d], len=%d, data=[", rsp_source,
		    length);
	BTC_TRACE(trace_buf);

	for (i = 0; i < length; i++) {
		coex_sta->bt_info_c2h[rsp_source][i] = tmp_buf[i];

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

	coex_sta->bt_info = coex_sta->bt_info_c2h[rsp_source][1];
	coex_sta->bt_info_ext = coex_sta->bt_info_c2h[rsp_source][4];
	coex_sta->bt_info_ext2 = coex_sta->bt_info_c2h[rsp_source][5];

	if (BT_INFO_SRC_8723D_2ANT_WIFI_FW != rsp_source) {

		/* if 0xff, it means BT is under WHCK test */
		coex_sta->bt_whck_test = ((coex_sta->bt_info == 0xff) ? TRUE :
					  FALSE);

		coex_sta->bt_create_connection = ((
			coex_sta->bt_info_c2h[rsp_source][2] & 0x80) ? TRUE :
						  FALSE);

		/* unit: %, value-100 to translate to unit: dBm */
		coex_sta->bt_rssi = coex_sta->bt_info_c2h[rsp_source][3] * 2 +
				    10;

		coex_sta->c2h_bt_remote_name_req = ((
			coex_sta->bt_info_c2h[rsp_source][2] & 0x20) ? TRUE :
						    FALSE);

		coex_sta->is_A2DP_3M = ((coex_sta->bt_info_c2h[rsp_source][2] &
					 0x10) ? TRUE : FALSE);

		coex_sta->acl_busy = ((coex_sta->bt_info_c2h[rsp_source][1] &
				       0x8) ? TRUE : FALSE);

		coex_sta->voice_over_HOGP = ((coex_sta->bt_info_ext & 0x10) ?
					     TRUE : FALSE);

		coex_sta->c2h_bt_inquiry_page = ((coex_sta->bt_info &
			  BT_INFO_8723D_2ANT_B_INQ_PAGE) ? TRUE : FALSE);
		
		if (coex_sta->bt_inq_page_pre != coex_sta->c2h_bt_inquiry_page) {
			coex_sta->bt_inq_page_pre = coex_sta->c2h_bt_inquiry_page;
			coex_sta->bt_inq_page_remain = TRUE;
					
			if (!coex_sta->c2h_bt_inquiry_page)
				coex_sta->bt_inq_page_downcount = 2;
		}

		coex_sta->a2dp_bit_pool = (((
			coex_sta->bt_info_c2h[rsp_source][1] & 0x49) == 0x49) ?
			(coex_sta->bt_info_c2h[rsp_source][6] & 0x7f) : 0);

		coex_sta->is_bt_a2dp_sink = (coex_sta->bt_info_c2h[rsp_source][6] & 0x80) ?
									TRUE : FALSE;

		coex_sta->bt_retry_cnt = coex_sta->bt_info_c2h[rsp_source][2] &
					 0xf;

		bt_link_info->slave_role  = coex_sta->bt_info_ext2 & 0x8;

		coex_sta->forbidden_slot = coex_sta->bt_info_ext2 & 0x7;

		coex_sta->hid_busy_num = (coex_sta->bt_info_ext2 & 0x30) >> 4;

		coex_sta->hid_pair_cnt = (coex_sta->bt_info_ext2 & 0xc0) >> 6;

		coex_sta->is_bt_opp_exist = (coex_sta->bt_info_ext2 & 0x1) ? TRUE : FALSE;

		if (coex_sta->bt_retry_cnt >= 1)
			coex_sta->pop_event_cnt++;

		if (coex_sta->c2h_bt_remote_name_req)
			coex_sta->cnt_remotenamereq++;

		if (coex_sta->bt_info_ext & BIT(1))
			coex_sta->cnt_reinit++;

		if (coex_sta->bt_info_ext & BIT(2) ||
		    (coex_sta->bt_create_connection &&
		    coex_sta->pnp_awake_period_cnt > 0)) {
			coex_sta->cnt_setuplink++;
			coex_sta->is_setup_link = TRUE;
			coex_sta->bt_relink_downcount = 2;
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Re-Link start in BT info!!\n");
			BTC_TRACE(trace_buf);
		}

		if (coex_sta->bt_info_ext & BIT(3))
			coex_sta->cnt_ignwlanact++;

		if (coex_sta->bt_info_ext & BIT(6))
			coex_sta->cnt_roleswitch++;

		if (coex_sta->bt_info_ext & BIT(7))
			coex_sta->is_bt_multi_link = TRUE;
		else
			coex_sta->is_bt_multi_link = FALSE;

		if (coex_sta->bt_info_ext & BIT(0))
			coex_sta->is_hid_rcu = TRUE;
		else
			coex_sta->is_hid_rcu = FALSE;

		if (coex_sta->bt_info_ext & BIT(5))
			coex_sta->is_ble_scan_en = TRUE;
		else
			coex_sta->is_ble_scan_en = FALSE;

		if (coex_sta->bt_create_connection) {
			coex_sta->cnt_page++;

			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY,
					   &wifi_busy);

			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &wifi_scan);
			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &wifi_link);
			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &wifi_roam);

			if ((wifi_link) || (wifi_roam) || (wifi_scan) ||
			    (coex_sta->wifi_is_high_pri_task) || (wifi_busy)) {

				is_scoreboard_scan = TRUE;
				halbtc8723d2ant_post_state_to_bt(btcoexist,
					 BT_8723D_2ANT_SCOREBOARD_SCAN, TRUE);

			} else
				halbtc8723d2ant_post_state_to_bt(btcoexist,
					 BT_8723D_2ANT_SCOREBOARD_SCAN, FALSE);

		} else {
				if (is_scoreboard_scan) {
					halbtc8723d2ant_post_state_to_bt(btcoexist,
						 BT_8723D_2ANT_SCOREBOARD_SCAN, FALSE);
					is_scoreboard_scan = FALSE;
				}
		}

		/* Here we need to resend some wifi info to BT */
		/* because bt is reset and loss of the info. */

		if ((!btcoexist->manual_control) &&
		    (!btcoexist->stop_coex_dm)) {

			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
					   &wifi_connected);

			/*  Re-Init */
			if ((coex_sta->bt_info_ext & BIT(1))) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT ext info bit1 check, send wifi BW&Chnl to BT!!\n");
				BTC_TRACE(trace_buf);
				if (wifi_connected)
					halbtc8723d2ant_update_wifi_channel_info(
						btcoexist, BTC_MEDIA_CONNECT);
				else
					halbtc8723d2ant_update_wifi_channel_info(
						btcoexist,
						BTC_MEDIA_DISCONNECT);
			}


			/*  If Ignore_WLanAct && not SetUp_Link or Role_Switch */
			if ((coex_sta->bt_info_ext & BIT(3)) &&
				(!(coex_sta->bt_info_ext & BIT(2))) &&
				(!(coex_sta->bt_info_ext & BIT(6)))) {

				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT ext info bit3 check, set BT NOT to ignore Wlan active!!\n");
				BTC_TRACE(trace_buf);
				halbtc8723d2ant_ignore_wlan_act(btcoexist,
							FORCE_EXEC, FALSE);
			} else {
				if (coex_sta->bt_info_ext & BIT(2)) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT ignore Wlan active because Re-link!!\n");
					BTC_TRACE(trace_buf);
				} else if (coex_sta->bt_info_ext & BIT(6)) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT ignore Wlan active because Role-Switch!!\n");
					BTC_TRACE(trace_buf);
				}
			}
		}

	}

	halbtc8723d2ant_update_bt_link_info(btcoexist);

	halbtc8723d2ant_run_coexist_mechanism(btcoexist);
}

void ex_halbtc8723d2ant_wl_fwdbginfo_notify(IN struct btc_coexist *btcoexist,
				       IN u8 *tmp_buf, IN u8 length)
{
	u8 i = 0;
	static u8 tmp_buf_pre[10], cnt;
	u8 h2c_parameter[2] = {0};

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], WiFi Fw Dbg info = %d %d %d %d %d %d %d %d (len = %d)\n",
		    tmp_buf[0], tmp_buf[1], tmp_buf[2], tmp_buf[3], tmp_buf[4],
		    tmp_buf[5], tmp_buf[6], tmp_buf[7], length);
	BTC_TRACE(trace_buf);

	if (tmp_buf[0] == 0x8) {
		for (i = 1; i <= 7; i++) {
			coex_sta->wl_fw_dbg_info[i] =
				(tmp_buf[i] >= tmp_buf_pre[i]) ?
					(tmp_buf[i] - tmp_buf_pre[i]) :
					(255 - tmp_buf_pre[i] + tmp_buf[i]);

			tmp_buf_pre[i] = tmp_buf[i];
		}
	}
#if 0
	if (!coex_sta->is_no_wl_5ms_extend && coex_sta->force_lps_ctrl &&
	    !coex_sta->cck_lock_ever) {
		if (coex_sta->wl_fw_dbg_info[7] <= 5)
			cnt++;
		else
			cnt = 0;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], 5ms WL slot extend cnt = %d!!\n", cnt);
		BTC_TRACE(trace_buf);

		if (cnt == 7) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], set h2c 0x69 opcode 12 to turn off 5ms WL slot extend!!\n");
			BTC_TRACE(trace_buf);

			h2c_parameter[0] = 0xc;
			h2c_parameter[1] = 0x1;
			btcoexist->btc_fill_h2c(btcoexist, 0x69, 2,
						h2c_parameter);
			coex_sta->is_no_wl_5ms_extend = TRUE;
			cnt = 0;
		}
	}

	if (coex_sta->is_no_wl_5ms_extend && coex_sta->cck_lock) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], set h2c 0x69 opcode 12 to turn on 5ms WL slot extend!!\n");
		BTC_TRACE(trace_buf);

		h2c_parameter[0] = 0xc;
		h2c_parameter[1] = 0x0;
		btcoexist->btc_fill_h2c(btcoexist, 0x69, 2, h2c_parameter);
		coex_sta->is_no_wl_5ms_extend = FALSE;
	}
#endif
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], set h2c 0x69 opcode 12 to turn off 5ms WL slot extend!!\n");
	BTC_TRACE(trace_buf);

	h2c_parameter[0] = 0xc;
	h2c_parameter[1] = 0x1;
	btcoexist->btc_fill_h2c(btcoexist, 0x69, 2,
					h2c_parameter);
	coex_sta->is_no_wl_5ms_extend = TRUE;
}

void ex_halbtc8723d2ant_rx_rate_change_notify(IN struct btc_coexist *btcoexist,
		IN BOOLEAN is_data_frame, IN u8 btc_rate_id)
{
	BOOLEAN wifi_connected = FALSE;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);

	if (is_data_frame) {
		coex_sta->wl_rx_rate = btc_rate_id;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], rx_rate_change_notify data rate id = %d, RTS_Rate = %d\n",
			coex_sta->wl_rx_rate, coex_sta->wl_rts_rx_rate);
		BTC_TRACE(trace_buf);
	} else {
		coex_sta->wl_rts_rx_rate = btc_rate_id;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], rts_rate_change_notify RTS rate id = %d, RTS_Rate = %d\n",
			coex_sta->wl_rts_rx_rate, coex_sta->wl_rts_rx_rate);
		BTC_TRACE(trace_buf);
	}

	if (wifi_connected &&
	    (coex_dm->bt_status ==  BT_8723D_2ANT_BT_STATUS_ACL_BUSY ||
	     coex_dm->bt_status ==  BT_8723D_2ANT_BT_STATUS_ACL_SCO_BUSY ||
	     coex_dm->bt_status == BT_8723D_2ANT_BT_STATUS_SCO_BUSY)) {

		if (coex_sta->wl_rx_rate == BTC_CCK_5_5 ||
		    coex_sta->wl_rx_rate == BTC_OFDM_6 ||
		    coex_sta->wl_rx_rate == BTC_MCS_0) {

			coex_sta->cck_lock_warn = TRUE;

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], cck lock warning...\n");
			BTC_TRACE(trace_buf);
		} else if (coex_sta->wl_rx_rate == BTC_CCK_1 ||
			   coex_sta->wl_rx_rate == BTC_CCK_2 ||
			   coex_sta->wl_rts_rx_rate == BTC_CCK_1 ||
			   coex_sta->wl_rts_rx_rate == BTC_CCK_2) {

			coex_sta->cck_lock = TRUE;
			coex_sta->cck_lock_ever = TRUE;

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], cck locking...\n");
			BTC_TRACE(trace_buf);
		} else {
			coex_sta->cck_lock_warn = FALSE;
			coex_sta->cck_lock = FALSE;

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], cck unlock...\n");
			BTC_TRACE(trace_buf);
		}
	} else {
		if (coex_dm->bt_status ==
		    BT_8723D_2ANT_BT_STATUS_CONNECTED_IDLE ||
		    coex_dm->bt_status ==
		    BT_8723D_2ANT_BT_STATUS_NON_CONNECTED_IDLE) {
			coex_sta->cck_lock_warn = FALSE;
			coex_sta->cck_lock = FALSE;
		}
	}

}



void ex_halbtc8723d2ant_rf_status_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], RF Status notify\n");
	BTC_TRACE(trace_buf);

	if (BTC_RF_ON == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RF is turned ON!!\n");
		BTC_TRACE(trace_buf);

		btcoexist->stop_coex_dm = FALSE;
		coex_sta->is_rf_state_off = FALSE;
#if 0
		halbtc8723d2ant_post_state_to_bt(btcoexist,
					 BT_8723D_2ANT_SCOREBOARD_ACTIVE, TRUE);
		halbtc8723d2ant_post_state_to_bt(btcoexist,
					 BT_8723D_2ANT_SCOREBOARD_ONOFF, TRUE);
#endif
	} else if (BTC_RF_OFF == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RF is turned OFF!!\n");
		BTC_TRACE(trace_buf);

		halbtc8723d2ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_AUTO,
					     FORCE_EXEC,
					     BT_8723D_2ANT_PHASE_WLAN_OFF);

		halbtc8723d2ant_action_coex_all_off(btcoexist);

		halbtc8723d2ant_post_state_to_bt(btcoexist,
				BT_8723D_2ANT_SCOREBOARD_ACTIVE |
				BT_8723D_2ANT_SCOREBOARD_ONOFF |
				BT_8723D_2ANT_SCOREBOARD_SCAN |
				BT_8723D_2ANT_SCOREBOARD_UNDERTEST,
				FALSE);

		btcoexist->stop_coex_dm = TRUE;
		coex_sta->is_rf_state_off = FALSE;
	}
}

void ex_halbtc8723d2ant_halt_notify(IN struct btc_coexist *btcoexist)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Halt notify\n");
	BTC_TRACE(trace_buf);

	halbtc8723d2ant_set_ant_path(btcoexist,
				     BTC_ANT_PATH_AUTO,
				     FORCE_EXEC,
				     BT_8723D_2ANT_PHASE_WLAN_OFF);

	/*halbtc8723d2ant_ignore_wlan_act(btcoexist, FORCE_EXEC, TRUE);*/

	ex_halbtc8723d2ant_media_status_notify(btcoexist, BTC_MEDIA_DISCONNECT);

	halbtc8723d2ant_post_state_to_bt(btcoexist,
				BT_8723D_2ANT_SCOREBOARD_ACTIVE |
				BT_8723D_2ANT_SCOREBOARD_ONOFF |
				BT_8723D_2ANT_SCOREBOARD_SCAN |
				BT_8723D_2ANT_SCOREBOARD_UNDERTEST,
				FALSE);
}

void ex_halbtc8723d2ant_pnp_notify(IN struct btc_coexist *btcoexist,
				   IN u8 pnp_state)
{
	static u8 pre_pnp_state;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Pnp notify\n");
	BTC_TRACE(trace_buf);

	if ((BTC_WIFI_PNP_SLEEP == pnp_state) ||
	    (BTC_WIFI_PNP_SLEEP_KEEP_ANT == pnp_state)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Pnp notify to SLEEP\n");
		BTC_TRACE(trace_buf);

		/* Sinda 20150819, workaround for driver skip leave IPS/LPS to speed up sleep time. */
		/* Driver do not leave IPS/LPS when driver is going to sleep, so BTCoexistence think wifi is still under IPS/LPS */
		/* BT should clear UnderIPS/UnderLPS state to avoid mismatch state after wakeup. */
		coex_sta->under_ips = FALSE;
		coex_sta->under_lps = FALSE;

		halbtc8723d2ant_post_state_to_bt(btcoexist,
				BT_8723D_2ANT_SCOREBOARD_ACTIVE |
				BT_8723D_2ANT_SCOREBOARD_ONOFF |
				BT_8723D_2ANT_SCOREBOARD_SCAN |
				BT_8723D_2ANT_SCOREBOARD_UNDERTEST,
				FALSE);

		if (BTC_WIFI_PNP_SLEEP_KEEP_ANT == pnp_state) {

			halbtc8723d2ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO,
						     FORCE_EXEC,
					     BT_8723D_2ANT_PHASE_2G_RUNTIME);
		} else {

			halbtc8723d2ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO,
						     FORCE_EXEC,
					     BT_8723D_2ANT_PHASE_WLAN_OFF);
		}

		btcoexist->stop_coex_dm = TRUE;

	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Pnp notify to WAKE UP\n");
		BTC_TRACE(trace_buf);

		coex_sta->pnp_awake_period_cnt = 3;

		/*WoWLAN*/
		if (pre_pnp_state == BTC_WIFI_PNP_SLEEP_KEEP_ANT ||
		    pnp_state == BTC_WIFI_PNP_WOWLAN) {
			coex_sta->run_time_state = TRUE;
			btcoexist->stop_coex_dm = FALSE;
			halbtc8723d2ant_run_coexist_mechanism(btcoexist);
		}

#if 0
		halbtc8723d2ant_post_state_to_bt(btcoexist,
					BT_8723D_2ANT_SCOREBOARD_ACTIVE, TRUE);
		halbtc8723d2ant_post_state_to_bt(btcoexist,
					BT_8723D_2ANT_SCOREBOARD_ONOFF, TRUE);
#endif
	}

	pre_pnp_state = pnp_state;
}

void ex_halbtc8723d2ant_periodical(IN struct btc_coexist *btcoexist)
{
	struct  btc_board_info	*board_info = &btcoexist->board_info;
	boolean wifi_busy = FALSE, bt_ctr_change = FALSE;
	u32	bt_patch_ver;
	static u8 cnt = 0;
	boolean bt_relink_finish = FALSE;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], ************* Periodical *************\n");
	BTC_TRACE(trace_buf);

#if (BT_AUTO_REPORT_ONLY_8723D_2ANT == 0)
	halbtc8723d2ant_query_bt_info(btcoexist);
#endif

	bt_ctr_change = halbtc8723d2ant_monitor_bt_ctr(btcoexist);
	halbtc8723d2ant_monitor_wifi_ctr(btcoexist);
	halbtc8723d2ant_monitor_bt_enable_disable(btcoexist);

	if (coex_sta->bt_relink_downcount != 0) {
		coex_sta->bt_relink_downcount--;

		if (coex_sta->bt_relink_downcount == 0) {
			coex_sta->is_setup_link = FALSE;
			bt_relink_finish = TRUE;
		}
	}
	
	if (coex_sta->bt_inq_page_downcount != 0) {
		coex_sta->bt_inq_page_downcount--;
		if (coex_sta->bt_relink_downcount == 0)
			coex_sta->bt_inq_page_remain = FALSE;
	}

	if (coex_sta->pnp_awake_period_cnt > 0)
		coex_sta->pnp_awake_period_cnt--;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], pnp_awake_period_cnt = %d\n",
				coex_sta->pnp_awake_period_cnt);
	BTC_TRACE(trace_buf);

	/*for A2DP glitch during connecting AP*/
	if (coex_sta->connect_ap_period_cnt > 0)
		coex_sta->connect_ap_period_cnt--;

	/* for 4-way, DHCP, EAPOL packet */
	if (coex_sta->specific_pkt_period_cnt > 0) {

		coex_sta->specific_pkt_period_cnt--;

		if ((coex_sta->specific_pkt_period_cnt == 0) &&
		    (coex_sta->wifi_is_high_pri_task))
			coex_sta->wifi_is_high_pri_task = FALSE;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			   "[BTCoex], ***************** Hi-Pri Task = %s\n",
			    (coex_sta->wifi_is_high_pri_task ? "Yes" : "No"));
		BTC_TRACE(trace_buf);

	}

	if (halbtc8723d2ant_is_wifibt_status_changed(btcoexist) ||
	    bt_relink_finish || bt_ctr_change)
		halbtc8723d2ant_run_coexist_mechanism(btcoexist);
}

void ex_halbtc8723d2ant_set_antenna_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	struct  btc_board_info	*board_info = &btcoexist->board_info;

	if (btcoexist->manual_control || btcoexist->stop_coex_dm)
		return;

	if (type == 2) { /* two antenna */
		board_info->ant_div_cfg = TRUE;

		halbtc8723d2ant_set_ant_path(btcoexist, BTC_ANT_PATH_WIFI,
					     FORCE_EXEC,
					     BT_8723D_2ANT_PHASE_2G_RUNTIME);

	} else { /* one antenna */
		halbtc8723d2ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO,
					     FORCE_EXEC,
					     BT_8723D_2ANT_PHASE_2G_RUNTIME);
	}
}

#ifdef PLATFORM_WINDOWS
#pragma optimize("", off)
#endif
void ex_halbtc8723d2ant_antenna_detection(IN struct btc_coexist *btcoexist,
		IN u32 cent_freq, IN u32 offset, IN u32 span, IN u32 seconds)
{

	static u32 ant_det_count = 0, ant_det_fail_count = 0;
	struct  btc_board_info	*board_info = &btcoexist->board_info;
	u16		u16tmp;
	u8			AntDetval = 0;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "xxxxxxxxxxxxxxxx Ext Call AntennaDetect()!!\n");
	BTC_TRACE(trace_buf);
}


void ex_halbtc8723d2ant_display_ant_detection(IN struct btc_coexist *btcoexist)
{
#if 0
	struct  btc_board_info	*board_info = &btcoexist->board_info;

	if (psd_scan->ant_det_try_count != 0)	{
		halbtc8723d2ant_psd_show_antenna_detect_result(btcoexist);

		if (board_info->btdm_ant_det_finish)
			halbtc8723d2ant_psd_showdata(btcoexist);
	}
#endif
}


#endif

#endif	/*  #if (RTL8723D_SUPPORT == 1) */

