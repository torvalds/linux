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

#include "mp_precomp.h"

#if (BT_SUPPORT == 1 && COEX_SUPPORT == 1)

#if (RTL8822B_SUPPORT == 1)
static u8 *trace_buf = &gl_btc_trace_buf[0];

static const char *const glbt_info_src_8822b_2ant[] = {
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
	"BT Info[bt iqk]",
	"BT Info[bt scbd]"
};

u32 glcoex_ver_date_8822b_2ant = 20190625;
u32 glcoex_ver_8822b_2ant = 0x72;
u32 glcoex_ver_btdesired_8822b_2ant = 0x6c;

static u8 halbtc8822b2ant_bt_rssi_state(struct btc_coexist *btc,
					u8 *ppre_bt_rssi_state, u8 level_num,
					u8 rssi_thresh, u8 rssi_thresh1)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;

	s32 bt_rssi = 0;
	u8 bt_rssi_state = *ppre_bt_rssi_state;

	bt_rssi = coex_sta->bt_rssi;

	if (level_num == 2) {
		if (*ppre_bt_rssi_state == BTC_RSSI_STATE_LOW ||
		    *ppre_bt_rssi_state == BTC_RSSI_STATE_STAY_LOW) {
			if (bt_rssi >=
			    (rssi_thresh + BTC_RSSI_COEX_THRESH_TOL_8822B_2ANT))
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

		if (*ppre_bt_rssi_state == BTC_RSSI_STATE_LOW ||
		    *ppre_bt_rssi_state == BTC_RSSI_STATE_STAY_LOW) {
			if (bt_rssi >=
			    (rssi_thresh + BTC_RSSI_COEX_THRESH_TOL_8822B_2ANT))
				bt_rssi_state = BTC_RSSI_STATE_MEDIUM;
			else
				bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
		} else if (*ppre_bt_rssi_state == BTC_RSSI_STATE_MEDIUM ||
			   *ppre_bt_rssi_state == BTC_RSSI_STATE_STAY_MEDIUM) {
			if (bt_rssi >= (rssi_thresh1 +
					BTC_RSSI_COEX_THRESH_TOL_8822B_2ANT))
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

static u8 halbtc8822b2ant_wifi_rssi_state(struct btc_coexist *btc,
					  u8 *pprewifi_rssi_state,
					  u8 level_num, u8 rssi_thresh,
					  u8 rssi_thresh1)
{
	s32 wifi_rssi = 0;
	u8 wifi_rssi_state = *pprewifi_rssi_state;

	btc->btc_get(btc, BTC_GET_S4_WIFI_RSSI, &wifi_rssi);

	if (level_num == 2) {
		if (*pprewifi_rssi_state == BTC_RSSI_STATE_LOW ||
		    *pprewifi_rssi_state == BTC_RSSI_STATE_STAY_LOW) {
			if (wifi_rssi >=
			    (rssi_thresh + BTC_RSSI_COEX_THRESH_TOL_8822B_2ANT))
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

		if (*pprewifi_rssi_state == BTC_RSSI_STATE_LOW ||
		    *pprewifi_rssi_state == BTC_RSSI_STATE_STAY_LOW) {
			if (wifi_rssi >=
			    (rssi_thresh + BTC_RSSI_COEX_THRESH_TOL_8822B_2ANT))
				wifi_rssi_state = BTC_RSSI_STATE_MEDIUM;
			else
				wifi_rssi_state = BTC_RSSI_STATE_STAY_LOW;
		} else if (*pprewifi_rssi_state == BTC_RSSI_STATE_MEDIUM ||
			   *pprewifi_rssi_state ==
			    BTC_RSSI_STATE_STAY_MEDIUM) {
			if (wifi_rssi >= (rssi_thresh1 +
					  BTC_RSSI_COEX_THRESH_TOL_8822B_2ANT))
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

static void
halbtc8822b2ant_limited_tx(struct btc_coexist *btc, boolean force_exec,
			   boolean tx_limit_en, boolean ampdu_limit_en)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct wifi_link_info_8822b_2ant *wifi_link_info_ext =
					 &btc->wifi_link_info_8822b_2ant;
	boolean wifi_under_b_mode = FALSE;

	/* Force Max Tx retry limit = 8*/
	if (!coex_sta->wl_tx_limit_en) {
		coex_sta->wl_0x430_backup = btc->btc_read_4byte(btc, 0x430);
		coex_sta->wl_0x434_backup = btc->btc_read_4byte(btc, 0x434);
		coex_sta->wl_0x42a_backup = btc->btc_read_2byte(btc, 0x42a);
	}

	if (!coex_sta->wl_ampdu_limit_en)
		coex_sta->wl_0x455_backup = btc->btc_read_1byte(btc, 0x455);

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
		btc->btc_write_1byte_bitmask(btc, 0x45e, 0x8, 0x1);
		btc->btc_write_1byte_bitmask(btc, 0x426, 0xf, 0xf);

		/* Max Tx retry limit = 8*/
		btc->btc_write_2byte(btc, 0x42a, 0x0808);

		btc->btc_get(btc, BTC_GET_BL_WIFI_UNDER_B_MODE,
			     &wifi_under_b_mode);

		/* Auto rate fallback step within 8 retry*/
		if (wifi_under_b_mode) {
			btc->btc_write_4byte(btc, 0x430, 0x1000000);
			btc->btc_write_4byte(btc, 0x434, 0x1010101);
		} else {
			btc->btc_write_4byte(btc, 0x430, 0x1000000);
			btc->btc_write_4byte(btc, 0x434, 0x4030201);
		}
	} else {
		/* Set BT polluted packet on for Tx rate adaptive not
		 *including Tx retry break by PTA, 0x45c[19] =1
		 */
		btc->btc_write_1byte_bitmask(btc, 0x45e, 0x8, 0x0);

		/* Set queue life time to avoid can't reach tx retry limit
		 * if tx is always break by GNT_BT.
		 */
		btc->btc_write_1byte_bitmask(btc, 0x426, 0xf, 0x0);

		/* Recovery Max Tx retry limit*/
		btc->btc_write_2byte(btc, 0x42a, coex_sta->wl_0x42a_backup);
		btc->btc_write_4byte(btc, 0x430, coex_sta->wl_0x430_backup);
		btc->btc_write_4byte(btc, 0x434, coex_sta->wl_0x434_backup);
	}

	if (ampdu_limit_en)
		btc->btc_write_1byte(btc, 0x455, 0x20);
	else
		btc->btc_write_1byte(btc, 0x455, coex_sta->wl_0x455_backup);
}

static void
halbtc8822b2ant_limited_rx(struct btc_coexist *btc, boolean force_exec,
			   boolean rej_ap_agg_pkt, boolean bt_ctrl_agg_buf_size,
			   u8 agg_buf_size)
{
#if 0
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	boolean reject_rx_agg = rej_ap_agg_pkt;
	boolean bt_ctrl_rx_agg_size = bt_ctrl_agg_buf_size;
	u8 rx_agg_size = agg_buf_size;

	if (!force_exec &&
	    bt_ctrl_agg_buf_size == coex_sta->wl_rxagg_limit_en &&
	    agg_buf_size == coex_sta->wl_rxagg_size)
		return;

	coex_sta->wl_rxagg_limit_en = bt_ctrl_agg_buf_size;
	coex_sta->wl_rxagg_size = agg_buf_size;

	/*btc->btc_set(btc, BTC_SET_BL_TO_REJ_AP_AGG_PKT, &reject_rx_agg);*/
	/* decide BT control aggregation buf size or not */
	btc->btc_set(btc, BTC_SET_BL_BT_CTRL_AGG_SIZE, &bt_ctrl_rx_agg_size);
	/* aggregation buf size, only work when BT control Rx aggregation size*/
	btc->btc_set(btc, BTC_SET_U1_AGG_BUF_SIZE, &rx_agg_size);
	/* real update aggregation setting */
	btc->btc_set(btc, BTC_SET_ACT_AGGREGATE_CTRL, NULL);
#endif
}

static void
halbtc8822b2ant_ccklock_action(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	u8 h2c_parameter[2] = {0};
	static u8 cnt;

	if (coex_sta->tdma_timer_base == 3) {
		if (!coex_sta->is_no_wl_5ms_extend) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], set h2c 0x69 opcode 12 to turn off 5ms WL slot extend!!\n");
			BTC_TRACE(trace_buf);

			h2c_parameter[0] = 0xc;
			h2c_parameter[1] = 0x1;
			btc->btc_fill_h2c(btc, 0x69, 2, h2c_parameter);
			coex_sta->is_no_wl_5ms_extend = TRUE;
			cnt = 0;
		}
		return;
	}

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
			btc->btc_fill_h2c(btc, 0x69, 2, h2c_parameter);
			coex_sta->is_no_wl_5ms_extend = TRUE;
			cnt = 0;
		}
	} else if (coex_sta->is_no_wl_5ms_extend && coex_sta->cck_lock) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], set h2c 0x69 opcode 12 to turn on 5ms WL slot extend!!\n");
		BTC_TRACE(trace_buf);

		h2c_parameter[0] = 0xc;
		h2c_parameter[1] = 0x0;
		btc->btc_fill_h2c(btc, 0x69, 2, h2c_parameter);
		coex_sta->is_no_wl_5ms_extend = FALSE;
	}
}

static void
halbtc8822b2ant_ccklock_detect(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	struct btc_wifi_link_info_ext *link_info_ext = &btc->wifi_link_info_ext;
	boolean is_cck_lock_rate = FALSE;

	if (coex_dm->bt_status == BTC_BTSTATUS_INQ_PAGE ||
	    coex_sta->is_setup_link) {
		coex_sta->cck_lock = FALSE;
		return;
	}

	if (coex_sta->wl_rx_rate <= BTC_CCK_2 ||
	    coex_sta->wl_rts_rx_rate <= BTC_CCK_2)
		is_cck_lock_rate = TRUE;

	if (link_info_ext->is_connected && coex_sta->gl_wifi_busy &&
	    (coex_dm->bt_status == BT_8822B_2ANT_BSTATUS_ACL_BUSY ||
	     coex_dm->bt_status == BT_8822B_2ANT_BSTATUS_ACL_SCO_BUSY ||
	     coex_dm->bt_status == BT_8822B_2ANT_BSTATUS_SCO_BUSY)) {
		if (is_cck_lock_rate) {
			coex_sta->cck_lock = TRUE;

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], cck locking...\n");
			BTC_TRACE(trace_buf);
		} else {
			coex_sta->cck_lock = FALSE;

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], cck unlock...\n");
			BTC_TRACE(trace_buf);
		}
	} else {
		coex_sta->cck_lock = FALSE;
	}
}

static void
halbtc8822b2ant_set_tdma_timer_base(struct btc_coexist *btc, u8 type)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	u16 tbtt_interval = 100;
	u8 h2c_para[2] = {0xb, 0x1};

	btc->btc_get(btc, BTC_GET_U2_BEACON_PERIOD, &tbtt_interval);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], tbtt_interval = %d\n", tbtt_interval);
	BTC_TRACE(trace_buf);

	/* Add for JIRA coex-256 */
	if (type == 3) { /* 4-slot  */
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

	/* no 5ms_wl_slot_extend for 4-slot mode  */
	if (coex_sta->tdma_timer_base == 3)
		halbtc8822b2ant_ccklock_action(btc);
}

static void halbtc8822b2ant_coex_switch_thres(struct btc_coexist *btc,
					      u8 isolation_measuared)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	s8 interference_wl_tx = 0, interference_bt_tx = 0;

	interference_wl_tx =
		BT_8822B_2ANT_WIFI_MAX_TX_POWER - isolation_measuared;
	interference_bt_tx =
		BT_8822B_2ANT_BT_MAX_TX_POWER - isolation_measuared;

	/* coex_sta->isolation_btween_wb default = 25dB,
	 * should be from config file
	 */
	if (coex_sta->isolation_btween_wb > 20) {
		coex_sta->wifi_coex_thres =
			BT_8822B_2ANT_WIFI_RSSI_COEXSWITCH_THRES1;
		coex_sta->wifi_coex_thres2 =
			BT_8822B_2ANT_WIFI_RSSI_COEXSWITCH_THRES2;

		coex_sta->bt_coex_thres =
			BT_8822B_2ANT_BT_RSSI_COEXSWITCH_THRES1;
		coex_sta->bt_coex_thres2 =
			BT_8822B_2ANT_BT_RSSI_COEXSWITCH_THRES2;
	} else {
		coex_sta->wifi_coex_thres = 90;
		coex_sta->wifi_coex_thres2 = 90;

		coex_sta->bt_coex_thres = 90;
		coex_sta->bt_coex_thres2 = 90;
	}

#if 0
	coex_sta->wifi_coex_thres =
		interference_wl_tx + BT_8822B_2ANT_WIFI_SIR_THRES1;
	coex_sta->wifi_coex_thres2 =
		interference_wl_tx + BT_8822B_2ANT_WIFI_SIR_THRES2;

	coex_sta->bt_coex_thres =
		interference_bt_tx + BT_8822B_2ANT_BT_SIR_THRES1;
	coex_sta->bt_coex_thres2 =
		interference_bt_tx + BT_8822B_2ANT_BT_SIR_THRES2;

	if  (BT_8822B_2ANT_WIFI_RSSI_COEXSWITCH_THRES1 <
	     (isolation_measuared - BT_8822B_2ANT_DEFAULT_ISOLATION))
		coex_sta->wifi_coex_thres =
			BT_8822B_2ANT_WIFI_RSSI_COEXSWITCH_THRES1;
	else
		coex_sta->wifi_coex_thres =
			BT_8822B_2ANT_WIFI_RSSI_COEXSWITCH_THRES1 -
			(isolation_measuared - BT_8822B_2ANT_DEFAULT_ISOLATION);

	if  (BT_8822B_2ANT_BT_RSSI_COEXSWITCH_THRES1 <
	     (isolation_measuared - BT_8822B_2ANT_DEFAULT_ISOLATION))
		coex_sta->bt_coex_thres =
			BT_8822B_2ANT_BT_RSSI_COEXSWITCH_THRES1;
	else
		coex_sta->bt_coex_thres =
			BT_8822B_2ANT_BT_RSSI_COEXSWITCH_THRES1 -
			(isolation_measuared - BT_8822B_2ANT_DEFAULT_ISOLATION);

#endif
}

static void halbtc8822b2ant_low_penalty_ra(struct btc_coexist *btc,
					   boolean force_exec,
					   boolean low_penalty_ra,
					   u8 thres)
{
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	static u8 cur_thres;

	if (!force_exec) {
		if (low_penalty_ra == coex_dm->cur_low_penalty_ra &&
		    thres == cur_thres)
			return;
	}

	if (low_penalty_ra)
		btc->btc_phydm_modify_RA_PCR_threshold(btc, 0, thres);
	else
		btc->btc_phydm_modify_RA_PCR_threshold(btc, 0, 0);

	coex_dm->cur_low_penalty_ra = low_penalty_ra;
	cur_thres = thres;
}

static boolean
halbtc8822b2ant_freerun_check(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	static u8 prewifi_rssi_state = BTC_RSSI_STATE_LOW;
	static u8 pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8 wifi_rssi_state, bt_rssi_state;

	wifi_rssi_state =
		halbtc8822b2ant_wifi_rssi_state(btc, &prewifi_rssi_state, 2,
						coex_sta->wifi_coex_thres, 0);

	bt_rssi_state =
		halbtc8822b2ant_bt_rssi_state(btc, &pre_bt_rssi_state, 2,
					      coex_sta->bt_coex_thres, 0);

	if (btc->board_info.ant_distance >= 40)
		return TRUE;

	if (btc->board_info.ant_distance <= 5)
		return FALSE;

	/* ant_distance = 5 ~ 40  */
	if (BTC_RSSI_HIGH(wifi_rssi_state) &&
	    BTC_RSSI_HIGH(bt_rssi_state) && coex_sta->gl_wifi_busy)
		return TRUE;
	else
		return FALSE;
}

static void halbtc8822b2ant_write_scbd(struct btc_coexist *btc, u16 bitpos,
				       boolean state)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	static u16 originalval = 0x8002, preval;

	if (state)
		originalval = originalval | bitpos;
	else
		originalval = originalval & (~bitpos);

	coex_sta->score_board_WB = originalval;

	if (originalval != preval) {
		preval = originalval;
		btc->btc_write_2byte(btc, 0xaa, originalval);
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], %s: return for nochange\n", __func__);
		BTC_TRACE(trace_buf);
	}
}

static void halbtc8822b2ant_read_scbd(struct btc_coexist *btc,
				      u16 *score_board_val)
{
	*score_board_val = (btc->btc_read_2byte(btc, 0xaa)) & 0x7fff;
}

static void halbtc8822b2ant_query_bt_info(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	u8 h2c_parameter[1] = {0};

	if (coex_sta->bt_disabled) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], No query BT info because BT is disabled!\n");
		BTC_TRACE(trace_buf);
		return;
	}

	h2c_parameter[0] |= BIT(0); /* trigger */

	btc->btc_fill_h2c(btc, 0x61, 1, h2c_parameter);
}

static void halbtc8822b2ant_enable_gnt_to_gpio(struct btc_coexist *btc,
					       boolean isenable)
{
	static u8 bit_val[5] = {0, 0, 0, 0, 0};

	if (!btc->dbg_mode)
		return;

	if (isenable) {
		/* enable GNT_WL, GNT_BT to GPIO for debug */
		btc->btc_write_1byte_bitmask(btc, 0x73, 0x8, 0x1);

		/* store original value */
		bit_val[0] = (btc->btc_read_1byte(btc, 0x66) & BIT(4)) >> 4;
		bit_val[1] = (btc->btc_read_1byte(btc, 0x67) & BIT(0));
		bit_val[2] = (btc->btc_read_1byte(btc, 0x42) & BIT(3)) >> 3;
		bit_val[3] = (btc->btc_read_1byte(btc, 0x65) & BIT(7)) >> 7;
		bit_val[4] = (btc->btc_read_1byte(btc, 0x72) & BIT(2)) >> 2;

		/*  switch GPIO Mux */
		btc->btc_write_1byte_bitmask(btc, 0x66, BIT(4), 0x0);
		btc->btc_write_1byte_bitmask(btc, 0x67, BIT(0), 0x0);
		btc->btc_write_1byte_bitmask(btc, 0x42, BIT(3), 0x0);
		btc->btc_write_1byte_bitmask(btc, 0x65, BIT(7), 0x0);
		btc->btc_write_1byte_bitmask(btc, 0x72, BIT(2), 0x0);
	} else {
		btc->btc_write_1byte_bitmask(btc, 0x73, 0x8, 0x0);

		/*  Restore original value  */
		/*  switch GPIO Mux */
		btc->btc_write_1byte_bitmask(btc, 0x66, BIT(4), bit_val[0]);
		btc->btc_write_1byte_bitmask(btc, 0x67, BIT(0), bit_val[1]);
		btc->btc_write_1byte_bitmask(btc, 0x42, BIT(3), bit_val[2]);
		btc->btc_write_1byte_bitmask(btc, 0x65, BIT(7), bit_val[3]);
		btc->btc_write_1byte_bitmask(btc, 0x72, BIT(2), bit_val[4]);
	}
}

static void halbtc8822b2ant_monitor_bt_ctr(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	u32 reg_hp_txrx, reg_lp_txrx, u32tmp;
	u32 reg_hp_tx = 0, reg_hp_rx = 0, reg_lp_tx = 0, reg_lp_rx = 0;
	static u8 num_of_bt_counter_chk, cnt_autoslot_hang;

	struct btc_bt_link_info *bt_link_info = &btc->bt_link_info;

	reg_hp_txrx = 0x770;
	reg_lp_txrx = 0x774;

	u32tmp = btc->btc_read_4byte(btc, reg_hp_txrx);
	reg_hp_tx = u32tmp & MASKLWORD;
	reg_hp_rx = (u32tmp & MASKHWORD) >> 16;

	u32tmp = btc->btc_read_4byte(btc, reg_lp_txrx);
	reg_lp_tx = u32tmp & MASKLWORD;
	reg_lp_rx = (u32tmp & MASKHWORD) >> 16;

	coex_sta->high_priority_tx = reg_hp_tx;
	coex_sta->high_priority_rx = reg_hp_rx;
	coex_sta->low_priority_tx = reg_lp_tx;
	coex_sta->low_priority_rx = reg_lp_rx;

	/* reset counter */
	btc->btc_write_1byte(btc, 0x76e, 0xc);

	if (coex_sta->low_priority_tx > 1050 &&
	    !coex_sta->c2h_bt_inquiry_page)
		coex_sta->pop_event_cnt++;

	if (coex_sta->sco_exist) {
		if (coex_sta->high_priority_tx >= 400 &&
		    coex_sta->high_priority_rx >= 400)
			coex_sta->is_esco_mode = FALSE;
		else
			coex_sta->is_esco_mode = TRUE;
	}
}

static void halbtc8822b2ant_monitor_wifi_ctr(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	s32 wifi_rssi = 0;
	boolean wifi_busy = FALSE, wifi_scan = FALSE;
	static u8 wl_noisy_count0, wl_noisy_count1 = 3, wl_noisy_count2;
	u32 cnt_cck;
	static u8 cnt_ccklocking;
	u8 h2c_parameter[1] = {0};
	struct btc_bt_link_info *bt_link_info = &btc->bt_link_info;

#if 0
	/*send h2c to query WL FW dbg info  */
	if (coex_dm->cur_ps_tdma_on) {
		h2c_parameter[0] = 0x8;
		btc->btc_fill_h2c(btc, 0x69, 1, h2c_parameter);
	}
#endif

	btc->btc_get(btc, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btc->btc_get(btc, BTC_GET_S4_WIFI_RSSI, &wifi_rssi);
	btc->btc_get(btc, BTC_GET_BL_WIFI_SCAN, &wifi_scan);

	coex_sta->crc_ok_cck =
		btc->btc_phydm_query_PHY_counter(btc, PHYDM_INFO_CRC32_OK_CCK);
	coex_sta->crc_ok_11g =
	    btc->btc_phydm_query_PHY_counter(btc, PHYDM_INFO_CRC32_OK_LEGACY);
	coex_sta->crc_ok_11n =
		btc->btc_phydm_query_PHY_counter(btc, PHYDM_INFO_CRC32_OK_HT);
	coex_sta->crc_ok_11n_vht =
		btc->btc_phydm_query_PHY_counter(btc, PHYDM_INFO_CRC32_OK_VHT);

	coex_sta->crc_err_cck =
	    btc->btc_phydm_query_PHY_counter(btc, PHYDM_INFO_CRC32_ERROR_CCK);
	coex_sta->crc_err_11g =
	  btc->btc_phydm_query_PHY_counter(btc, PHYDM_INFO_CRC32_ERROR_LEGACY);
	coex_sta->crc_err_11n =
	    btc->btc_phydm_query_PHY_counter(btc, PHYDM_INFO_CRC32_ERROR_HT);
	coex_sta->crc_err_11n_vht =
	    btc->btc_phydm_query_PHY_counter(btc, PHYDM_INFO_CRC32_ERROR_VHT);

	/* CCK lock identification */
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

	if (!wifi_busy && !coex_sta->cck_lock) {
		if (cnt_cck > 250) {
			if (wl_noisy_count2 < 3)
				wl_noisy_count2++;

			if (wl_noisy_count2 == 3) {
				wl_noisy_count0 = 0;
				wl_noisy_count1 = 0;
			}

		} else if (cnt_cck < 100) {
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

static void
halbtc8822b2ant_monitor_bt_enable(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	static u32 bt_disable_cnt;
	boolean bt_active = TRUE, bt_disabled = FALSE;
	u16 u16tmp;
	u8 lna_lvl = 1;

	/* Read BT on/off status from scoreboard[1],
	 * enable this only if BT patch support this feature
	 */
	halbtc8822b2ant_read_scbd(btc, &u16tmp);

	bt_active = u16tmp & BIT(1);

	if (bt_active) {
		bt_disable_cnt = 0;
		bt_disabled = FALSE;
		btc->btc_set(btc, BTC_SET_BL_BT_DISABLE, &bt_disabled);
	} else {
		bt_disable_cnt++;
		if (bt_disable_cnt >= 10) {
			bt_disabled = TRUE;
			bt_disable_cnt = 10;
		}

		btc->btc_set(btc, BTC_SET_BL_BT_DISABLE, &bt_disabled);
	}

	if (coex_sta->bt_disabled != bt_disabled) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is from %s to %s!!\n",
			    (coex_sta->bt_disabled ? "disabled" : "enabled"),
			    (bt_disabled ? "disabled" : "enabled"));
		BTC_TRACE(trace_buf);
		coex_sta->bt_disabled = bt_disabled;

		/*for win10 BT disable->enable trigger wifi scan issue   */
		if (!coex_sta->bt_disabled) {
			coex_sta->is_bt_reenable = TRUE;
			coex_sta->cnt_bt_reenable = 15;
			btc->btc_set(btc, BTC_SET_BL_BT_LNA_CONSTRAIN_LEVEL,
				     &lna_lvl);
		} else {
			coex_sta->is_bt_reenable = FALSE;
			coex_sta->cnt_bt_reenable = 0;
		}
	}
}

static boolean
halbtc8822b2ant_moniter_wifibt_status(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct wifi_link_info_8822b_2ant *wifi_link_info_ext =
					 &btc->wifi_link_info_8822b_2ant;
	static boolean pre_wifi_busy, pre_under_4way,
		     pre_bt_off, pre_bt_slave,
		     pre_wifi_under_lps, pre_bt_setup_link,
		     pre_bt_acl_busy;
	static u8 pre_hid_busy_num, pre_wl_noisy_level;
	boolean wifi_busy = FALSE, under_4way = FALSE;
	boolean wifi_connected = FALSE;
	struct btc_bt_link_info *bt_link_info = &btc->bt_link_info;
	static u8 cnt_wifi_busytoidle;
	u32 num_of_wifi_link = 0, wifi_link_mode = 0;
	static u32 pre_num_of_wifi_link, pre_wifi_link_mode;
	boolean miracast_plus_bt = FALSE;
	u8 lna_lvl = 1;

	btc->btc_get(btc, BTC_GET_BL_WIFI_CONNECTED, &wifi_connected);
	btc->btc_get(btc, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btc->btc_get(btc, BTC_GET_BL_WIFI_4_WAY_PROGRESS, &under_4way);

	if (wifi_busy) {
		coex_sta->gl_wifi_busy = TRUE;
		cnt_wifi_busytoidle = 6;
	} else {
		if (coex_sta->gl_wifi_busy && cnt_wifi_busytoidle > 0)
			cnt_wifi_busytoidle--;
		else if (cnt_wifi_busytoidle == 0)
			coex_sta->gl_wifi_busy = FALSE;
	}

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
		coex_sta->bt_a2dp_vendor_id = 0;
		coex_sta->bt_a2dp_device_name = 0;
		btc->bt_info.bt_get_fw_ver = 0;
		return TRUE;
	}

	num_of_wifi_link = wifi_link_info_ext->num_of_active_port;

	if (num_of_wifi_link != pre_num_of_wifi_link) {
		pre_num_of_wifi_link = num_of_wifi_link;

		if (wifi_link_info_ext->is_p2p_connected) {
			if (bt_link_info->bt_link_exist)
				miracast_plus_bt = TRUE;
			else
				miracast_plus_bt = FALSE;

			btc->btc_set(btc, BTC_SET_BL_MIRACAST_PLUS_BT,
				     &miracast_plus_bt);
		}

		return TRUE;
	}

	wifi_link_mode = btc->wifi_link_info.link_mode;
	if (wifi_link_mode != pre_wifi_link_mode) {
		pre_wifi_link_mode = wifi_link_mode;
		return TRUE;
	}

	if (wifi_connected) {
		if (wifi_busy != pre_wifi_busy) {
			pre_wifi_busy = wifi_busy;
			return TRUE;
		}
		if (under_4way != pre_under_4way) {
			pre_under_4way = under_4way;
			return TRUE;
		}
		if (coex_sta->wl_noisy_level != pre_wl_noisy_level) {
			pre_wl_noisy_level = coex_sta->wl_noisy_level;
			return TRUE;
		}
		if (coex_sta->under_lps != pre_wifi_under_lps) {
			pre_wifi_under_lps = coex_sta->under_lps;
			if (coex_sta->under_lps)
				return TRUE;
		}
	}

	if (!coex_sta->bt_disabled) {
		if (coex_sta->acl_busy != pre_bt_acl_busy) {
			pre_bt_acl_busy = coex_sta->acl_busy;
			return TRUE;
		}

		if (coex_sta->hid_busy_num != pre_hid_busy_num) {
			pre_hid_busy_num = coex_sta->hid_busy_num;
			return TRUE;
		}

		if (bt_link_info->slave_role != pre_bt_slave) {
			pre_bt_slave = bt_link_info->slave_role;
			return TRUE;
		}

		if (pre_bt_setup_link != coex_sta->is_setup_link) {
			pre_bt_setup_link = coex_sta->is_setup_link;
			return TRUE;
		}
	}

	return FALSE;
}

static void halbtc8822b2ant_update_wifi_link_info(struct btc_coexist *btc,
						  u8 reason)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct wifi_link_info_8822b_2ant *wifi_link_info_ext =
					 &btc->wifi_link_info_8822b_2ant;
	struct btc_wifi_link_info wifi_link_info;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	u8 wifi_central_chnl = 0, num_of_wifi_link = 0;
	boolean isunder5G = FALSE, ismcc25g = FALSE, isp2pconnected = FALSE;
	u32 wifi_link_status = 0;

	btc->btc_get(btc, BTC_GET_BL_WIFI_CONNECTED,
		     &wifi_link_info_ext->is_connected);

	btc->btc_get(btc, BTC_GET_U4_WIFI_LINK_STATUS, &wifi_link_status);
	wifi_link_info_ext->port_connect_status = wifi_link_status & 0xffff;

	btc->btc_get(btc, BTC_GET_BL_WIFI_LINK_INFO, &wifi_link_info);
	btc->wifi_link_info = wifi_link_info;

	btc->btc_get(btc, BTC_GET_U1_WIFI_CENTRAL_CHNL, &wifi_central_chnl);
	coex_sta->wl_center_channel = wifi_central_chnl;

	/* Check scan/connect/special-pkt action first  */
	switch (reason) {
	case BT_8822B_2ANT_RSN_5GSCANSTART:
	case BT_8822B_2ANT_RSN_5GSWITCHBAND:
	case BT_8822B_2ANT_RSN_5GCONSTART:

		isunder5G = TRUE;
		break;
	case BT_8822B_2ANT_RSN_2GSCANSTART:
	case BT_8822B_2ANT_RSN_2GSWITCHBAND:
	case BT_8822B_2ANT_RSN_2GCONSTART:

		isunder5G = FALSE;
		break;
	case BT_8822B_2ANT_RSN_2GCONFINISH:
	case BT_8822B_2ANT_RSN_5GCONFINISH:
	case BT_8822B_2ANT_RSN_2GMEDIA:
	case BT_8822B_2ANT_RSN_5GMEDIA:
	case BT_8822B_2ANT_RSN_BTINFO:
	case BT_8822B_2ANT_RSN_PERIODICAL:
	case BT_8822B_2ANT_RSN_2GSPECIALPKT:
	case BT_8822B_2ANT_RSN_5GSPECIALPKT:
	default:
		switch (wifi_link_info.link_mode) {
		case BTC_LINK_5G_MCC_GO_STA:
		case BTC_LINK_5G_MCC_GC_STA:
		case BTC_LINK_5G_SCC_GO_STA:
		case BTC_LINK_5G_SCC_GC_STA:

			isunder5G = TRUE;
			break;
		case BTC_LINK_2G_MCC_GO_STA:
		case BTC_LINK_2G_MCC_GC_STA:
		case BTC_LINK_2G_SCC_GO_STA:
		case BTC_LINK_2G_SCC_GC_STA:

			isunder5G = FALSE;
			break;
		case BTC_LINK_25G_MCC_GO_STA:
		case BTC_LINK_25G_MCC_GC_STA:

			isunder5G = FALSE;
			ismcc25g = TRUE;
			break;
		case BTC_LINK_ONLY_STA:
			if (wifi_link_info.sta_center_channel > 14)
				isunder5G = TRUE;
			else
				isunder5G = FALSE;
			break;
		case BTC_LINK_ONLY_GO:
		case BTC_LINK_ONLY_GC:
		case BTC_LINK_ONLY_AP:
		default:
			if (wifi_link_info.p2p_center_channel > 14)
				isunder5G = TRUE;
			else
				isunder5G = FALSE;

			break;
		}
		break;
	}

	wifi_link_info_ext->is_all_under_5g = isunder5G;
	wifi_link_info_ext->is_mcc_25g = ismcc25g;

	if (wifi_link_status & WIFI_STA_CONNECTED)
		num_of_wifi_link++;

	if (wifi_link_status & WIFI_AP_CONNECTED)
		num_of_wifi_link++;

	if (wifi_link_status & WIFI_P2P_GO_CONNECTED) {
		num_of_wifi_link++;
		isp2pconnected = TRUE;
	}

	if (wifi_link_status & WIFI_P2P_GC_CONNECTED) {
		num_of_wifi_link++;
		isp2pconnected = TRUE;
	}

	wifi_link_info_ext->num_of_active_port = num_of_wifi_link;
	wifi_link_info_ext->is_p2p_connected = isp2pconnected;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], wifi_link_info: link_mode=%d, STA_Ch=%d, P2P_Ch=%d, AnyClient_Join_Go=%d !\n",
		    btc->wifi_link_info.link_mode,
		    btc->wifi_link_info.sta_center_channel,
		    btc->wifi_link_info.p2p_center_channel,
		    btc->wifi_link_info.bany_client_join_go);
	BTC_TRACE(trace_buf);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], wifi_link_info: center_ch=%d, is_all_under_5g=%d, is_mcc_25g=%d!\n",
		    coex_sta->wl_center_channel,
		    wifi_link_info_ext->is_all_under_5g,
		    wifi_link_info_ext->is_mcc_25g);
	BTC_TRACE(trace_buf);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], wifi_link_info: port_connect_status=0x%x, active_port_cnt=%d, P2P_Connect=%d!\n",
		    wifi_link_info_ext->port_connect_status,
		    wifi_link_info_ext->num_of_active_port,
		    wifi_link_info_ext->is_p2p_connected);
	BTC_TRACE(trace_buf);

	switch (reason) {
	case BT_8822B_2ANT_RSN_2GSCANSTART:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Update reason = %s\n", "2GSCANSTART");
		break;
	case BT_8822B_2ANT_RSN_5GSCANSTART:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Update reason = %s\n", "5GSCANSTART");
		break;
	case BT_8822B_2ANT_RSN_SCANFINISH:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Update reason = %s\n", "SCANFINISH");
		break;
	case BT_8822B_2ANT_RSN_2GSWITCHBAND:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Update reason = %s\n", "2GSWITCHBAND");
		break;
	case BT_8822B_2ANT_RSN_5GSWITCHBAND:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Update reason = %s\n", "5GSWITCHBAND");
		break;
	case BT_8822B_2ANT_RSN_2GCONSTART:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Update reason = %s\n", "2GCONNECTSTART");
		break;
	case BT_8822B_2ANT_RSN_5GCONSTART:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Update reason = %s\n", "5GCONNECTSTART");
		break;
	case BT_8822B_2ANT_RSN_2GCONFINISH:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Update reason = %s\n",
			    "2GCONNECTFINISH");
		break;
	case BT_8822B_2ANT_RSN_5GCONFINISH:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Update reason = %s\n",
			    "5GCONNECTFINISH");
		break;
	case BT_8822B_2ANT_RSN_2GMEDIA:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Update reason = %s\n", "2GMEDIASTATUS");
		break;
	case BT_8822B_2ANT_RSN_5GMEDIA:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Update reason = %s\n", "5GMEDIASTATUS");
		break;
	case BT_8822B_2ANT_RSN_MEDIADISCON:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Update reason = %s\n",
			    "MEDIADISCONNECT");
		break;
	case BT_8822B_2ANT_RSN_2GSPECIALPKT:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Update reason = %s\n", "2GSPECIALPKT");
		break;
	case BT_8822B_2ANT_RSN_5GSPECIALPKT:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Update reason = %s\n", "5GSPECIALPKT");
		break;
	case BT_8822B_2ANT_RSN_BTINFO:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Update reason = %s\n", "BTINFO");
		break;
	case BT_8822B_2ANT_RSN_PERIODICAL:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Update reason = %s\n", "PERIODICAL");
		break;
	case BT_8822B_2ANT_RSN_PNP:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Update reason = %s\n", "PNPNotify");
		break;
	case BT_8822B_2ANT_RSN_LPS:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Update reason = %s\n", "LPSNotify");
		break;
	default:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Update reason = %s\n", "UNKNOWN");
		break;
	}

	BTC_TRACE(trace_buf);

	if (btc->manual_control || btc->stop_coex_dm)
		return;

	if (wifi_link_info_ext->is_all_under_5g || num_of_wifi_link == 0 ||
	    coex_dm->bt_status == BT_8822B_2ANT_BSTATUS_NCON_IDLE) {
		halbtc8822b2ant_low_penalty_ra(btc, NM_EXCU, FALSE, 0);
		halbtc8822b2ant_limited_tx(btc, NM_EXCU, FALSE, FALSE);
		halbtc8822b2ant_limited_rx(btc, NM_EXCU, FALSE, TRUE, 64);
	} else if (wifi_link_info_ext->num_of_active_port > 1) {
		halbtc8822b2ant_low_penalty_ra(btc, NM_EXCU, TRUE, 30);
		halbtc8822b2ant_limited_tx(btc, NM_EXCU, FALSE, TRUE);
		halbtc8822b2ant_limited_rx(btc, NM_EXCU, FALSE, TRUE, 16);
	} else {
		halbtc8822b2ant_low_penalty_ra(btc, NM_EXCU, TRUE, 15);

		if (coex_sta->hid_exist || coex_sta->hid_pair_cnt > 0 ||
		    coex_sta->sco_exist) {
			halbtc8822b2ant_limited_tx(btc, NM_EXCU, TRUE, TRUE);
			halbtc8822b2ant_limited_rx(btc, NM_EXCU, FALSE, TRUE,
						   16);
		} else {
			halbtc8822b2ant_limited_tx(btc, NM_EXCU, TRUE, FALSE);
			halbtc8822b2ant_limited_rx(btc, NM_EXCU, FALSE, TRUE,
						   64);
		}
	}

	/* coex-276  P2P-Go beacon request can't release issue
	 * Only PCIe can set 0x454[6] = 1 to solve this issue,
	 * WL SDIO/USB interface need driver support.
	 */
	if (btc->chip_interface == BTC_INTF_PCI &&
	    wifi_link_info.link_mode == BTC_LINK_ONLY_GO)
		btc->btc_write_1byte_bitmask(btc, 0x454, BIT(6), 0x1);
	else
		btc->btc_write_1byte_bitmask(btc, 0x454, BIT(6), 0x0);
}

static void halbtc8822b2ant_update_bt_link_info(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	struct btc_bt_link_info *bt_link_info = &btc->bt_link_info;
	boolean bt_busy = FALSE, increase_scan_dev_num = FALSE;
	u32 val = 0;
	static u8 pre_num_of_profile, cur_num_of_profile, cnt, ble_cnt;

	if (++ble_cnt >= 3)
		ble_cnt = 0;

	if (coex_sta->is_ble_scan_en && ble_cnt == 0) {
		u32 *p = NULL;
		u8 scantype;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT ext info bit4 check, query BLE Scan type!!\n");
		BTC_TRACE(trace_buf);
		coex_sta->bt_ble_scan_type =
			btc->btc_get_ble_scan_type_from_bt(btc);

		if ((coex_sta->bt_ble_scan_type & 0x1) == 0x1) {
			p = &coex_sta->bt_ble_scan_para[0];
			scantype = 0x1;
		}

		if ((coex_sta->bt_ble_scan_type & 0x2) == 0x2) {
			p = &coex_sta->bt_ble_scan_para[1];
			scantype = 0x2;
		}

		if ((coex_sta->bt_ble_scan_type & 0x4) == 0x4) {
			p = &coex_sta->bt_ble_scan_para[2];
			scantype = 0x4;
		}

		if (p)
			*p = btc->btc_get_ble_scan_para_from_bt(btc, scantype);
	}

	coex_sta->num_of_profile = 0;

	/* set link exist status */
	if (!(coex_sta->bt_info_lb2 & BT_INFO_8822B_2ANT_B_CONNECTION)) {
		coex_sta->bt_link_exist = FALSE;
		coex_sta->pan_exist = FALSE;
		coex_sta->a2dp_exist = FALSE;
		coex_sta->hid_exist = FALSE;
		coex_sta->sco_exist = FALSE;
		coex_sta->msft_mr_exist = FALSE;
	} else { /* connection exists */
		coex_sta->bt_link_exist = TRUE;
		if (coex_sta->bt_info_lb2 & BT_INFO_8822B_2ANT_B_FTP) {
			coex_sta->pan_exist = TRUE;
			coex_sta->num_of_profile++;
		} else {
			coex_sta->pan_exist = FALSE;
		}

		if (coex_sta->bt_info_lb2 & BT_INFO_8822B_2ANT_B_A2DP) {
			coex_sta->a2dp_exist = TRUE;
			coex_sta->num_of_profile++;
		} else {
			coex_sta->a2dp_exist = FALSE;
		}

		if (coex_sta->bt_info_lb2 & BT_INFO_8822B_2ANT_B_HID) {
			coex_sta->hid_exist = TRUE;
			coex_sta->num_of_profile++;
		} else {
			coex_sta->hid_exist = FALSE;
		}

		if (coex_sta->bt_info_lb2 & BT_INFO_8822B_2ANT_B_SCO_ESCO) {
			coex_sta->sco_exist = TRUE;
			coex_sta->num_of_profile++;
		} else {
			coex_sta->sco_exist = FALSE;
		}

		if (coex_sta->hid_busy_num == 0 &&
		    coex_sta->hid_pair_cnt > 0 &&
		    coex_sta->low_priority_tx > 1000 &&
		    coex_sta->low_priority_rx > 1000 &&
		    !coex_sta->c2h_bt_inquiry_page)
			coex_sta->msft_mr_exist = TRUE;
		else
			coex_sta->msft_mr_exist = FALSE;
	}

	bt_link_info->bt_link_exist = coex_sta->bt_link_exist;
	bt_link_info->sco_exist = coex_sta->sco_exist;
	bt_link_info->a2dp_exist = coex_sta->a2dp_exist;
	bt_link_info->pan_exist = coex_sta->pan_exist;
	bt_link_info->hid_exist = coex_sta->hid_exist;
	bt_link_info->acl_busy = coex_sta->acl_busy;

	/* check if Sco only */
	if (bt_link_info->sco_exist && !bt_link_info->a2dp_exist &&
	    !bt_link_info->pan_exist && !bt_link_info->hid_exist)
		bt_link_info->sco_only = TRUE;
	else
		bt_link_info->sco_only = FALSE;

	/* check if A2dp only */
	if (!bt_link_info->sco_exist && bt_link_info->a2dp_exist &&
	    !bt_link_info->pan_exist && !bt_link_info->hid_exist)
		bt_link_info->a2dp_only = TRUE;
	else
		bt_link_info->a2dp_only = FALSE;

	/* check if Pan only */
	if (!bt_link_info->sco_exist && !bt_link_info->a2dp_exist &&
	    bt_link_info->pan_exist && !bt_link_info->hid_exist)
		bt_link_info->pan_only = TRUE;
	else
		bt_link_info->pan_only = FALSE;

	/* check if Hid only */
	if (!bt_link_info->sco_exist && !bt_link_info->a2dp_exist &&
	    !bt_link_info->pan_exist && bt_link_info->hid_exist)
		bt_link_info->hid_only = TRUE;
	else
		bt_link_info->hid_only = FALSE;

	if (coex_sta->bt_info_lb2 & BT_INFO_8822B_2ANT_B_INQ_PAGE) {
		coex_dm->bt_status = BT_8822B_2ANT_BSTATUS_INQ_PAGE;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT Inq/page!!!\n");
	} else if (!(coex_sta->bt_info_lb2 & BT_INFO_8822B_2ANT_B_CONNECTION)) {
		coex_dm->bt_status = BT_8822B_2ANT_BSTATUS_NCON_IDLE;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT Non-Connected idle!!!\n");
	} else if (coex_sta->bt_info_lb2 == BT_INFO_8822B_2ANT_B_CONNECTION) {
		/* connection exists but no busy */

		if (coex_sta->msft_mr_exist) {
			coex_dm->bt_status = BT_8822B_2ANT_BSTATUS_ACL_BUSY;
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], BtInfoNotify(),  BT ACL busy!!\n");
		} else {
			coex_dm->bt_status =
				BT_8822B_2ANT_BSTATUS_CON_IDLE;
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], BtInfoNotify(), BT Connected-idle!!!\n");
		}
	} else if (((coex_sta->bt_info_lb2 & BT_INFO_8822B_2ANT_B_SCO_ESCO) ||
		    (coex_sta->bt_info_lb2 & BT_INFO_8822B_2ANT_B_SCO_BUSY)) &&
		   (coex_sta->bt_info_lb2 & BT_INFO_8822B_2ANT_B_ACL_BUSY)) {
		coex_dm->bt_status = BT_8822B_2ANT_BSTATUS_ACL_SCO_BUSY;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT ACL SCO busy!!!\n");
	} else if ((coex_sta->bt_info_lb2 & BT_INFO_8822B_2ANT_B_SCO_ESCO) ||
		   (coex_sta->bt_info_lb2 & BT_INFO_8822B_2ANT_B_SCO_BUSY)) {
		coex_dm->bt_status = BT_8822B_2ANT_BSTATUS_SCO_BUSY;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT SCO busy!!!\n");
	} else if (coex_sta->bt_info_lb2 & BT_INFO_8822B_2ANT_B_ACL_BUSY) {
		coex_dm->bt_status = BT_8822B_2ANT_BSTATUS_ACL_BUSY;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT ACL busy!!!\n");
	} else {
		coex_dm->bt_status = BT_8822B_2ANT_BSTATUS_MAX;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT Non-Defined state!!!\n");
	}

	BTC_TRACE(trace_buf);

	if (coex_dm->bt_status == BT_8822B_2ANT_BSTATUS_ACL_BUSY ||
	    coex_dm->bt_status == BT_8822B_2ANT_BSTATUS_SCO_BUSY ||
	    coex_dm->bt_status == BT_8822B_2ANT_BSTATUS_ACL_SCO_BUSY) {
		bt_busy = TRUE;
		increase_scan_dev_num = TRUE;
	} else {
		bt_busy = FALSE;
		increase_scan_dev_num = FALSE;
	}

	btc->btc_set(btc, BTC_SET_BL_BT_TRAFFIC_BUSY, &bt_busy);
	btc->btc_set(btc, BTC_SET_BL_INC_SCAN_DEV_NUM, &increase_scan_dev_num);

	cur_num_of_profile = coex_sta->num_of_profile;

	if (cur_num_of_profile != pre_num_of_profile)
		cnt = 2;

	if (bt_link_info->a2dp_exist && cnt > 0) {
		cnt--;
		if (coex_sta->bt_a2dp_vendor_id == 0 &&
		    coex_sta->bt_a2dp_device_name == 0) {
			btc->btc_get(btc, BTC_GET_U4_BT_DEVICE_INFO, &val);

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], BtInfoNotify(), get BT DEVICE_INFO = %x\n",
				    val);
			BTC_TRACE(trace_buf);

			coex_sta->bt_a2dp_vendor_id = (u8)(val & 0xff);
			coex_sta->bt_a2dp_device_name = (val & 0xffffff00) >> 8;

			btc->btc_get(btc, BTC_GET_U4_BT_A2DP_FLUSH_VAL, &val);
			coex_sta->bt_a2dp_flush_time = val;
		}
	} else if (!bt_link_info->a2dp_exist) {
		coex_sta->bt_a2dp_vendor_id = 0;
		coex_sta->bt_a2dp_device_name = 0;
		coex_sta->bt_a2dp_flush_time = 0;
	}

	pre_num_of_profile = coex_sta->num_of_profile;
}

static void
halbtc8822b2ant_update_wifi_ch_info(struct btc_coexist *btc, u8 type)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	struct wifi_link_info_8822b_2ant *wifi_link_info_ext =
					 &btc->wifi_link_info_8822b_2ant;
	u8 h2c_parameter[3] = {0}, i;
	u32 wifi_bw;
	u8 wl_ch = 0;
	u8 wl_5g_ch[] = {120, 124, 128, 132, 136, 140, 144, 149, 153, 157,
			 118, 126, 134, 142, 151, 159, 122, 138, 155};
	u8 bt_skip_ch[] = {2, 8,  17, 26, 34, 42, 51, 62, 71, 77,
			   2, 12, 29, 46, 66, 76, 10, 37, 68};
	u8 bt_skip_span[] = {4, 8,  8,  10, 8,  10, 8,  8,  10, 4,
			     4, 16, 16, 16, 16, 4,  20, 34, 20};

	if (btc->manual_control)
		return;

	btc->btc_get(btc, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (btc->stop_coex_dm || btc->wl_rf_state_off) {
		wl_ch = 0;
	} else if (type != BTC_MEDIA_DISCONNECT ||
		   (type == BTC_MEDIA_DISCONNECT &&
		    wifi_link_info_ext->num_of_active_port > 0)) {
		if (wifi_link_info_ext->num_of_active_port == 1) {
			if (wifi_link_info_ext->is_p2p_connected)
				wl_ch = btc->wifi_link_info.p2p_center_channel;
			else
				wl_ch = btc->wifi_link_info.sta_center_channel;
		} else { /* port > 2 */
			if (btc->wifi_link_info.p2p_center_channel > 14 &&
			    btc->wifi_link_info.sta_center_channel > 14)
				wl_ch = btc->wifi_link_info.p2p_center_channel;
			else if (btc->wifi_link_info.p2p_center_channel <= 14)
				wl_ch = btc->wifi_link_info.p2p_center_channel;
			else if (btc->wifi_link_info.sta_center_channel <= 14)
				wl_ch = btc->wifi_link_info.sta_center_channel;
		}
	}

	if (wl_ch > 0) {
		if (wl_ch <= 14) {
			h2c_parameter[0] = 0x1;
			h2c_parameter[1] = wl_ch;

			if (wifi_bw == BTC_WIFI_BW_HT40)
				h2c_parameter[2] = 0x36;
			else
				h2c_parameter[2] = 0x24;
		} else { /* for 5G  */
			for (i = 0; i < ARRAY_SIZE(wl_5g_ch); i++) {
				if (wl_ch == wl_5g_ch[i]) {
					h2c_parameter[0] = 0x3;
					h2c_parameter[1] = bt_skip_ch[i];
					h2c_parameter[2] = bt_skip_span[i];
					break;
				}
			}
		}
	}

	coex_dm->wifi_chnl_info[0] = h2c_parameter[0];
	coex_dm->wifi_chnl_info[1] = h2c_parameter[1];
	coex_dm->wifi_chnl_info[2] = h2c_parameter[2];

	btc->btc_fill_h2c(btc, 0x66, 3, h2c_parameter);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], para[0:2] = 0x%x 0x%x 0x%x\n", h2c_parameter[0],
		    h2c_parameter[1], h2c_parameter[2]);
	BTC_TRACE(trace_buf);
}

static void halbtc8822b2ant_set_wl_tx_power(struct btc_coexist *btc,
					    boolean force_exec,
					    u8 wl_pwr_lvl)
{
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;

	if (!force_exec) {
		if (wl_pwr_lvl == coex_dm->cur_wl_pwr_lvl)
			return;
	}

	btc->btc_write_1byte_bitmask(btc, 0xc5b, 0xff, wl_pwr_lvl);
	btc->btc_write_1byte_bitmask(btc, 0xe5b, 0xff, wl_pwr_lvl);

	coex_dm->cur_wl_pwr_lvl = wl_pwr_lvl;
}

static void halbtc8822b2ant_set_bt_tx_power(struct btc_coexist *btc,
					    boolean force_exec,
					    u8 bt_pwr_lvl)
{
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	u8 h2c_parameter[1] = {0};

	if (!force_exec) {
		if (bt_pwr_lvl == coex_dm->cur_bt_pwr_lvl)
			return;
	}

#if 0
	h2c_parameter[0] = 0 - bt_pwr_lvl;
#endif
	h2c_parameter[0] = bt_pwr_lvl;

	btc->btc_fill_h2c(btc, 0x62, 1, h2c_parameter);

	coex_dm->cur_bt_pwr_lvl = bt_pwr_lvl;
}

static void halbtc8822b2ant_set_wl_rx_gain(struct btc_coexist *btc,
					   boolean force_exec,
					   boolean agc_table_en)
{
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	u8 i;

	/*20171116*/
	u32 rx_gain_value_en[] = {0xff000003,
		0xbd120003, 0xbe100003, 0xbf080003, 0xbf060003, 0xbf050003,
		0xbc140003, 0xbb160003, 0xba180003, 0xb91a0003, 0xb81c0003,
		0xb71e0003, 0xb4200003, 0xb5220003, 0xb4240003, 0xb3260003,
		0xb2280003, 0xb12a0003, 0xb02c0003, 0xaf2e0003, 0xae300003,
		0xad320003, 0xac340003, 0xab360003, 0x8d380003, 0x8c3a0003,
		0x8b3c0003, 0x8a3e0003, 0x6e400003, 0x6d420003, 0x6c440003,
		0x6b460003, 0x6a480003, 0x694a0003, 0x684c0003, 0x674e0003,
		0x66500003, 0x65520003, 0x64540003, 0x64560003, 0x007e0403};

	u32 rx_gain_value_dis[] = {0xff000003,
		0xf4120003, 0xf5100003, 0xf60e0003, 0xf70c0003, 0xf80a0003,
		0xf3140003, 0xf2160003, 0xf1180003, 0xf01a0003, 0xef1c0003,
		0xee1e0003, 0xed200003, 0xec220003, 0xeb240003, 0xea260003,
		0xe9280003, 0xe82a0003, 0xe72c0003, 0xe62e0003, 0xe5300003,
		0xc8320003, 0xc7340003, 0xc6360003, 0xc5380003, 0xc43a0003,
		0xc33c0003, 0xc23e0003, 0xc1400003, 0xc0420003, 0xa5440003,
		0xa4460003, 0xa3480003, 0xa24a0003, 0xa14c0003, 0x834e0003,
		0x82500003, 0x81520003, 0x80540003, 0x65560003, 0x007e0403};

#if 0
	/*20180228--58~-110:use from line 5 to line 8 , others :line 5*/
	u32 rx_gain_value_en[] = {
		0xff000003, 0xf4120003, 0xf5100003, 0xf60e0003, 0xf70c0003,
		0xf80a0003, 0xf3140003, 0xf2160003, 0xf1180003, 0xf01a0003,
		0xef1c0003, 0xee1e0003, 0xed200003, 0xec220003, 0xeb240003,
		0xea260003, 0xe9280003, 0xe82a0003, 0xe72c0003, 0xe62e0003,
		0xe5300003, 0xc8320003, 0xc7340003, 0xab360003, 0x8d380003,
		0x8c3a0003, 0x8b3c0003, 0x8a3e0003, 0x6e400003, 0x6d420003,
		0x6c440003, 0x6b460003, 0x6a480003, 0x694a0003, 0x684c0003,
		0x674e0003, 0x66500003, 0x65520003, 0x64540003, 0x64560003,
		0x007e0403};

	u32 rx_gain_value_dis[] = {
		0xff000003, 0xf4120003, 0xf5100003, 0xf60e0003, 0xf70c0003,
		0xf80a0003, 0xf3140003, 0xf2160003, 0xf1180003, 0xf01a0003,
		0xef1c0003, 0xee1e0003, 0xed200003, 0xec220003, 0xeb240003,
		0xea260003, 0xe9280003, 0xe82a0003, 0xe72c0003, 0xe62e0003,
		0xe5300003, 0xc8320003, 0xc7340003, 0xc6360003, 0xc5380003,
		0xc43a0003, 0xc33c0003, 0xc23e0003, 0xc1400003, 0xc0420003,
		0xa5440003, 0xa4460003, 0xa3480003, 0xa24a0003, 0xa14c0003,
		0x834e0003, 0x82500003, 0x81520003, 0x80540003, 0x65560003,
		0x007e0403};
#endif

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], *************wl rx gain*************\n");
	BTC_TRACE(trace_buf);

	if (!force_exec) {
		if (agc_table_en == coex_dm->cur_agc_table_en)
			return;
	}

	if (agc_table_en) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BB Agc Table On!\n");
		BTC_TRACE(trace_buf);

		for (i = 0; i < ARRAY_SIZE(rx_gain_value_en); i++)
			btc->btc_write_4byte(btc, 0x81c, rx_gain_value_en[i]);

		/* set Rx filter corner RCK offset */
		btc->btc_set_rf_reg(btc, BTC_RF_A, 0xde, 0x2, 0x1);
		btc->btc_set_rf_reg(btc, BTC_RF_A, 0x1d, 0x3f, 0x3f);
		btc->btc_set_rf_reg(btc, BTC_RF_B, 0xde, 0x2, 0x1);
		btc->btc_set_rf_reg(btc, BTC_RF_B, 0x1d, 0x3f, 0x3f);
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BB Agc Table Off!\n");
		BTC_TRACE(trace_buf);

		for (i = 0; i < ARRAY_SIZE(rx_gain_value_dis); i++)
			btc->btc_write_4byte(btc, 0x81c, rx_gain_value_dis[i]);

		/* set Rx filter corner RCK offset */
		btc->btc_set_rf_reg(btc, BTC_RF_A, 0x1d, 0x3f, 0x4);
		btc->btc_set_rf_reg(btc, BTC_RF_A, 0xde, 0x2, 0x0);
		btc->btc_set_rf_reg(btc, BTC_RF_B, 0x1d, 0x3f, 0x4);
		btc->btc_set_rf_reg(btc, BTC_RF_B, 0xde, 0x2, 0x0);
	}

	coex_dm->cur_agc_table_en = agc_table_en;
}

static void halbtc8822b2ant_set_bt_rx_gain(struct btc_coexist *btc,
					   boolean force_exec,
					   boolean rx_gain_en)
{
	/* use scoreboard[4] to notify BT Rx gain table change   */
	halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_RXGAIN, rx_gain_en);
}

static u32
halbtc8822b2ant_wait_indirect_reg_ready(struct btc_coexist *btc)
{
	u32 delay_count = 0;

	/* wait for ready bit before access 0x1700 */
	while (1) {
		if ((btc->btc_read_1byte(btc, 0x1703) & BIT(5)) == 0) {
			delay_ms(10);
			if (++delay_count >= 10)
				break;
		} else {
			break;
		}
	}

	return delay_count;
}

static u32
halbtc8822b2ant_read_indirect_reg(struct btc_coexist *btc, u16 reg_addr)
{
	u32 delay_count = 0;

	halbtc8822b2ant_wait_indirect_reg_ready(btc);

	/* wait for ready bit before access 0x1700		 */
	btc->btc_write_4byte(btc, 0x1700, 0x800F0000 | reg_addr);
	return btc->btc_read_4byte(btc, 0x1708); /* get read data */
}

static void
halbtc8822b2ant_write_indirect_reg(struct btc_coexist *btc, u16 reg_addr,
				   u32 bit_mask, u32 reg_value)
{
	u32 val, i = 0, bitpos = 0, delay_count = 0;

	if (bit_mask == 0x0)
		return;
	if (bit_mask == 0xffffffff) {
		/* wait for ready bit before access 0x1700/0x1704 */
		halbtc8822b2ant_wait_indirect_reg_ready(btc);

		/* put write data */
		btc->btc_write_4byte(btc, 0x1704, reg_value);
		btc->btc_write_4byte(btc, 0x1700, 0xc00F0000 | reg_addr);
	} else {
		for (i = 0; i <= 31; i++) {
			if (((bit_mask >> i) & 0x1) == 0x1) {
				bitpos = i;
				break;
			}
		}

		/* read back register value before write */
		val = halbtc8822b2ant_read_indirect_reg(btc, reg_addr);
		val = (val & (~bit_mask)) | (reg_value << bitpos);

		/* wait for ready bit before access 0x1700/0x1704 */
		halbtc8822b2ant_wait_indirect_reg_ready(btc);

		/* put write data */
		btc->btc_write_4byte(btc, 0x1704, val);
		btc->btc_write_4byte(btc, 0x1700, 0xc00F0000 | reg_addr);
	}
}

static void halbtc8822b2ant_ltecoex_enable(struct btc_coexist *btc,
					   boolean enable)
{
	u8 val;

	/* 0x38[7] */
	val = (enable) ? 1 : 0;
	halbtc8822b2ant_write_indirect_reg(btc, 0x38, BIT(7), val);
}

static void halbtc8822b2ant_ltecoex_table(struct btc_coexist *btc,
					  u8 table_type, u16 table_val)
{
	u16 reg_addr = 0x0000;

	switch (table_type) {
	case BT_8822B_2ANT_CTT_WL_VS_LTE:
		reg_addr = 0xa0;
		break;
	case BT_8822B_2ANT_CTT_BT_VS_LTE:
		reg_addr = 0xa4;
		break;
	}

	if (reg_addr == 0x0000)
		return;

	/* 0xa0[15:0] or 0xa4[15:0] */
	halbtc8822b2ant_write_indirect_reg(btc, reg_addr, 0xffff, table_val);
}

static void
halbtc8822b2ant_coex_ctrl_owner(struct btc_coexist *btc, boolean wifi_control)
{
	u8 val;

	val = (wifi_control) ? 1 : 0;
	btc->btc_write_1byte_bitmask(btc, 0x73, BIT(2), val);
}

static void
halbtc8822b2ant_set_gnt_bt(struct btc_coexist *btc, u8 state)
{
	switch (state) {
	case BTC_GNT_SET_SW_LOW:
		halbtc8822b2ant_write_indirect_reg(btc, 0x38, 0xc000, 0x1);
		halbtc8822b2ant_write_indirect_reg(btc, 0x38, 0x0c00, 0x1);
		break;
	case BTC_GNT_SET_SW_HIGH:
		halbtc8822b2ant_write_indirect_reg(btc, 0x38, 0xc000, 0x3);
		halbtc8822b2ant_write_indirect_reg(btc, 0x38, 0x0c00, 0x3);
		break;
	case BTC_GNT_SET_HW_PTA:
	default:
		halbtc8822b2ant_write_indirect_reg(btc, 0x38, 0xc000, 0x0);
		halbtc8822b2ant_write_indirect_reg(btc, 0x38, 0x0c00, 0x0);
		break;
	}
}

static void
halbtc8822b2ant_set_gnt_wl(struct btc_coexist *btc, u8 state)
{
	switch (state) {
	case BTC_GNT_SET_SW_LOW:
		halbtc8822b2ant_write_indirect_reg(btc, 0x38, 0x3000, 0x1);
		halbtc8822b2ant_write_indirect_reg(btc, 0x38, 0x0300, 0x1);
		break;
	case BTC_GNT_SET_SW_HIGH:
		halbtc8822b2ant_write_indirect_reg(btc, 0x38, 0x3000, 0x3);
		halbtc8822b2ant_write_indirect_reg(btc, 0x38, 0x0300, 0x3);
		break;
	case BTC_GNT_SET_HW_PTA:
	default:
		halbtc8822b2ant_write_indirect_reg(btc, 0x38, 0x3000, 0x0);
		halbtc8822b2ant_write_indirect_reg(btc, 0x38, 0x0300, 0x0);
		break;
	}
}

static void halbtc8822b2ant_set_table(struct btc_coexist *btc,
				      boolean force_exec, u32 val0x6c0,
				      u32 val0x6c4, u32 val0x6c8, u8 val0x6cc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;

	if (!force_exec && !coex_sta->wl_slot_toggle_change) {
		if (val0x6c0 == coex_dm->cur_val0x6c0 &&
		    val0x6c4 == coex_dm->cur_val0x6c4 &&
		    val0x6c8 == coex_dm->cur_val0x6c8 &&
		    val0x6cc == coex_dm->cur_val0x6cc)
			return;
	}

	btc->btc_write_4byte(btc, 0x6c0, val0x6c0);
	btc->btc_write_4byte(btc, 0x6c4, val0x6c4);
	btc->btc_write_4byte(btc, 0x6c8, val0x6c8);
	btc->btc_write_1byte(btc, 0x6cc, val0x6cc);

	coex_dm->cur_val0x6c0 = val0x6c0;
	coex_dm->cur_val0x6c4 = val0x6c4;
	coex_dm->cur_val0x6c8 = val0x6c8;
	coex_dm->cur_val0x6cc = val0x6cc;
}

static void halbtc8822b2ant_table(struct btc_coexist *btc, boolean force_exec,
				  u8 type)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	u32 break_table;
	u8 select_table;

	coex_sta->coex_table_type = type;

	if (coex_sta->concurrent_rx_mode_on) {
		break_table = 0xf0ffffff; /* set WL hi-pri can break BT */
		/* set Tx response = Hi-Pri (ex: Transmitting ACK,BA,CTS) */
		select_table = 0x1b;
	} else {
		break_table = 0xffffff;
		select_table = 0x13;
	}

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], ********** Table-%d **********\n",
		    coex_sta->coex_table_type);
	BTC_TRACE(trace_buf);

	switch (type) {
	case 0:
		halbtc8822b2ant_set_table(btc, force_exec, 0xffffffff,
					  0xffffffff, break_table,
					  select_table);
		break;
	case 1:
		halbtc8822b2ant_set_table(btc, force_exec, 0x55555555,
					  0x5a5a5a5a, break_table,
					  select_table);
		break;
	case 2:
		halbtc8822b2ant_set_table(btc, force_exec, 0x5a5a5a5a,
					  0x5a5a5a5a, break_table,
					  select_table);
		break;
	case 3:
		halbtc8822b2ant_set_table(btc, force_exec, 0x55555555,
					  0x5a5a5a5a, break_table,
					  select_table);
		break;
	case 4:
		halbtc8822b2ant_set_table(btc, force_exec, 0x55555555,
					  0x5a5a5a5a, break_table,
					  select_table);
		break;
	case 5:
		halbtc8822b2ant_set_table(btc, force_exec, 0x55555555,
					  0x55555555, break_table,
					  select_table);
		break;
	case 6:
		halbtc8822b2ant_set_table(btc, force_exec, 0xa5555555,
					  0xfafafafa, break_table,
					  select_table);
		break;
	case 7:
		halbtc8822b2ant_set_table(btc, force_exec, 0xa5555555,
					  0xaa5a5a5a, break_table,
					  select_table);
		break;
	case 8:
		halbtc8822b2ant_set_table(btc, force_exec, 0xffff55ff,
					  0xfafafafa, break_table,
					  select_table);
		break;
	case 9:
		halbtc8822b2ant_set_table(btc, force_exec, 0x5a5a5a5a,
					  0xaaaa5aaa, break_table,
					  select_table);
		break;
	case 10:
		halbtc8822b2ant_set_table(btc, force_exec, 0x55555555,
					  0x5a5a555a, break_table,
					  select_table);
		break;
	case 11:
		halbtc8822b2ant_set_table(btc, force_exec, 0xaaffffaa,
					  0xfafafafa, break_table,
					  select_table);
		break;
	case 12:
		halbtc8822b2ant_set_table(btc, force_exec, 0xffff55ff,
					  0x5afa5afa, break_table,
					  select_table);
		break;
	case 13:
		halbtc8822b2ant_set_table(btc, force_exec, 0xffffffff,
					  0xfafafafa, break_table,
					  select_table);
		break;
	case 14:
		halbtc8822b2ant_set_table(btc, force_exec, 0xffff55ff,
					  0xaaaaaaaa, break_table,
					  select_table);
		break;
	case 15:
		halbtc8822b2ant_set_table(btc, force_exec, 0x55ff55ff,
					  0x5afa5afa, break_table,
					  select_table);
		break;
	case 16:
		halbtc8822b2ant_set_table(btc, force_exec, 0x55ff55ff,
					  0xaaaaaaaa, break_table,
					  select_table);
		break;
	case 17:
		halbtc8822b2ant_set_table(btc, force_exec, 0x55ff55ff,
					  0x55ff55ff, break_table,
					  select_table);
		break;
	default:
		break;
	}
}

static void
halbtc8822b2ant_wltoggle_table(IN struct btc_coexist *btc,
			       IN boolean force_exec,  IN u8 interval,
			       IN u8 val0x6c4_b0, IN u8 val0x6c4_b1,
			       IN u8 val0x6c4_b2, IN u8 val0x6c4_b3)
{
	static u8 pre_h2c_parameter[6] = {0};
	u8	cur_h2c_parameter[6] = {0};
	u8 i, match_cnt = 0;

	cur_h2c_parameter[0] = 0x7;	/* op_code, 0x7= wlan toggle slot*/

	cur_h2c_parameter[1] = interval;
	cur_h2c_parameter[2] = val0x6c4_b0;
	cur_h2c_parameter[3] = val0x6c4_b1;
	cur_h2c_parameter[4] = val0x6c4_b2;
	cur_h2c_parameter[5] = val0x6c4_b3;
#if 0
	if (!force_exec) {
		for (i = 1; i <= 5; i++) {
			if (cur_h2c_parameter[i] != pre_h2c_parameter[i])
				break;

			match_cnt++;
		}

		if (match_cnt == 5)
			return;
	}
#endif
	for (i = 1; i <= 5; i++)
		pre_h2c_parameter[i] = cur_h2c_parameter[i];

	btc->btc_fill_h2c(btc, 0x69, 6, cur_h2c_parameter);
}

static void halbtc8822b2ant_ignore_wlan_act(struct btc_coexist *btc,
					    boolean force_exec,
					    boolean enable)
{
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	u8 h2c_parameter[1] = {0};

	if (btc->manual_control || btc->stop_coex_dm)
		return;

	if (!force_exec) {
		if (enable == coex_dm->cur_ignore_wlan_act)
			return;
	}

	if (enable)
		h2c_parameter[0] |= BIT(0); /* function enable */

	btc->btc_fill_h2c(btc, 0x63, 1, h2c_parameter);

	coex_dm->cur_ignore_wlan_act = enable;
}

static void halbtc8822b2ant_lps_rpwm(struct btc_coexist *btc,
				     boolean force_exec, u8 lps_val,
				     u8 rpwm_val)
{
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;

	if (!force_exec) {
		if (lps_val == coex_dm->cur_lps &&
		    rpwm_val == coex_dm->cur_rpwm)
			return;
	}

	btc->btc_set(btc, BTC_SET_U1_LPS_VAL, &lps_val);
	btc->btc_set(btc, BTC_SET_U1_RPWM_VAL, &rpwm_val);

	coex_dm->cur_lps = lps_val;
	coex_dm->cur_rpwm = rpwm_val;
}

static void
halbtc8822b2ant_tdma_check(struct btc_coexist *btc, boolean new_ps_state)
{
	u8 lps_mode = 0x0;
	u8 h2c_parameter[5] = {0, 0, 0, 0x40, 0};
#if 0
	u32 RTL97F_8822B = 0;

	if (RTL97F_8822B)
		return;
#endif

	btc->btc_get(btc, BTC_GET_U1_LPS_MODE, &lps_mode);

	if (lps_mode) { /* already under LPS state */
		if (new_ps_state) {
			/* keep state under LPS, do nothing. */
		} else {
			btc->btc_fill_h2c(btc, 0x60, 5, h2c_parameter);
		}
	} else { /* NO PS state */
		if (new_ps_state) {
			btc->btc_fill_h2c(btc, 0x60, 5, h2c_parameter);
		} else {
			/* keep state under NO PS state, do nothing. */
		}
	}
}

static boolean halbtc8822b2ant_power_save_state(struct btc_coexist *btc,
						u8 ps_type, u8 lps_val,
						u8 rpwm_val)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	boolean low_pwr_dis = FALSE, result = TRUE;

	switch (ps_type) {
	case BTC_PS_WIFI_NATIVE:
		coex_sta->force_lps_ctrl = FALSE;
		/* recover to original 32k low power setting */
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], %s == BTC_PS_WIFI_NATIVE\n", __func__);
		BTC_TRACE(trace_buf);

		low_pwr_dis = FALSE;
		/* recover to original 32k low power setting */

		btc->btc_set(btc, BTC_SET_ACT_PRE_NORMAL_LPS, NULL);
		break;
	case BTC_PS_LPS_ON:
		coex_sta->force_lps_ctrl = TRUE;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], %s == BTC_PS_LPS_ON\n", __func__);
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_tdma_check(btc, TRUE);
		halbtc8822b2ant_lps_rpwm(btc, NM_EXCU, lps_val, rpwm_val);
		/* when coex force to enter LPS, do not enter 32k low power. */
		low_pwr_dis = TRUE;
		btc->btc_set(btc, BTC_SET_ACT_DISABLE_LOW_POWER, &low_pwr_dis);
		/* power save must executed before psTdma. */
		btc->btc_set(btc, BTC_SET_ACT_ENTER_LPS, NULL);
		break;
	case BTC_PS_LPS_OFF:
		coex_sta->force_lps_ctrl = TRUE;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], %s == BTC_PS_LPS_OFF\n", __func__);
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_tdma_check(btc, FALSE);
		result = btc->btc_set(btc, BTC_SET_ACT_LEAVE_LPS, NULL);
		break;
	default:
		break;
	}

	return result;
}

static void halbtc8822b2ant_set_tdma(struct btc_coexist *btc, u8 byte1,
				     u8 byte2, u8 byte3, u8 byte4, u8 byte5)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	u8 h2c_parameter[5] = {0};
	u8 real_byte1 = byte1, real_byte5 = byte5;
	boolean ap_enable = FALSE, result = FALSE;
	struct btc_bt_link_info *bt_link_info = &btc->bt_link_info;
	u8 ps_type = BTC_PS_WIFI_NATIVE;

	if (byte5 & BIT(2))
		coex_sta->is_tdma_btautoslot = TRUE;
	else
		coex_sta->is_tdma_btautoslot = FALSE;

	if (btc->wifi_link_info.link_mode == BTC_LINK_ONLY_GO &&
	    btc->wifi_link_info.bhotspot &&
	    btc->wifi_link_info.bany_client_join_go)
		ap_enable = TRUE;

	if ((ap_enable) && (byte1 & BIT(4) && !(byte1 & BIT(5)))) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], %s == FW for AP mode\n", __func__);
		BTC_TRACE(trace_buf);

		real_byte1 &= ~BIT(4);
		real_byte1 |= BIT(5);

		real_byte5 |= BIT(5);
		real_byte5 &= ~BIT(6);

		ps_type = BTC_PS_WIFI_NATIVE;
		halbtc8822b2ant_power_save_state(btc, ps_type, 0x0, 0x0);
	} else if (byte1 & BIT(4) && !(byte1 & BIT(5))) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], %s == Force LPS (byte1 = 0x%x)\n",
			    __func__, byte1);
		BTC_TRACE(trace_buf);

		ps_type = BTC_PS_LPS_OFF;
		if (!halbtc8822b2ant_power_save_state(btc, ps_type, 0x50, 0x4))
			result = TRUE;

	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], %s == Native LPS (byte1 = 0x%x)\n",
			    __func__, byte1);
		BTC_TRACE(trace_buf);

		ps_type = BTC_PS_WIFI_NATIVE;
		halbtc8822b2ant_power_save_state(btc, ps_type, 0x0, 0x0);
	}

	coex_sta->is_set_ps_state_fail = result;

	if (!coex_sta->is_set_ps_state_fail) {
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

		btc->btc_fill_h2c(btc, 0x60, 5, h2c_parameter);

		if (real_byte1 & BIT(2)) {
			coex_sta->wl_slot_toggle = TRUE;
			coex_sta->wl_slot_toggle_change = FALSE;
		} else {
			if (coex_sta->wl_slot_toggle)
				coex_sta->wl_slot_toggle_change = TRUE;
			else
				coex_sta->wl_slot_toggle_change = FALSE;
			coex_sta->wl_slot_toggle = FALSE;
		}
	} else {
		coex_sta->cnt_set_ps_state_fail++;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], %s == Force Leave LPS Fail (cnt = %d)\n",
			    __func__, coex_sta->cnt_set_ps_state_fail);
		BTC_TRACE(trace_buf);
	}

	if (ps_type == BTC_PS_WIFI_NATIVE)
		btc->btc_set(btc, BTC_SET_ACT_POST_NORMAL_LPS, NULL);
}

static void
halbtc8822b2ant_tdma(struct btc_coexist *btc, boolean force_exec,
		     boolean turn_on, u32 tcase)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	static u8 tdma_byte4_modify, pre_tdma_byte4_modify;
	struct btc_bt_link_info *bt_link_info = &btc->bt_link_info;
	u8 type;

	btc->btc_set_atomic(btc, &coex_dm->setting_tdma, TRUE);

	/* tcase: bit0~7 --> tdma case index
	 *        bit8   --> for 4-slot (50ms) mode
	 */
	if (tcase & BIT(8))/* 4-slot (50ms) mode */
		halbtc8822b2ant_set_tdma_timer_base(btc, 3);
	else
		halbtc8822b2ant_set_tdma_timer_base(btc, 0);

	type = tcase & 0xff;

#if 0
	/* 0x778 = 0x1 at wifi slot (no blocking BT Low-Pri pkts) */
	if (bt_link_info->slave_role)
		tdma_byte4_modify = 0x1;
	else
		tdma_byte4_modify = 0x0;

	if (pre_tdma_byte4_modify != tdma_byte4_modify) {
		force_exec = TRUE;
		pre_tdma_byte4_modify = tdma_byte4_modify;
	}
#endif

	if (!force_exec &&
	    (turn_on == coex_dm->cur_ps_tdma_on &&
	    type == coex_dm->cur_ps_tdma)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Skip TDMA because no change TDMA(%s, %d)\n",
			    (coex_dm->cur_ps_tdma_on ? "on" : "off"),
			    coex_dm->cur_ps_tdma);
		BTC_TRACE(trace_buf);

		btc->btc_set_atomic(btc, &coex_dm->setting_tdma, FALSE);
		return;
	}

	if (turn_on) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], ********** TDMA(on, %d) **********\n",
			    type);
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_TDMA, TRUE);

		/* enable TBTT nterrupt */
		btc->btc_write_1byte_bitmask(btc, 0x550, 0x8, 0x1);

		switch (type) {
		case 1:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x10, 0x03, 0x91,
						 0x50);
			break;
		case 2:
		default:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x35, 0x03, 0x11,
						 0x11);
			break;
		case 3:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x3a, 0x3, 0x91,
						 0x10);
			break;
		case 4:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x21, 0x3, 0x91,
						 0x10);
			break;
		case 5:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x25, 0x3, 0x91,
						 0x10);
			break;
		case 6:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x10, 0x3, 0x91,
						 0x10);
			break;
		case 7:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x20, 0x3, 0x91,
						 0x10);
			break;
		case 8:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x15, 0x03, 0x11,
						 0x11);
			break;
		case 10:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x30, 0x03, 0x11,
						 0x10);
			break;
		case 11:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x35, 0x03, 0x11,
						 0x10);
			break;
		case 12:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x35, 0x03, 0x11,
						 0x11);
			break;
		case 13:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x1a, 0x03, 0x11,
						 0x10);
			break;
		case 14:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x20, 0x03, 0x11,
						 0x11);
			break;
		case 15:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x10, 0x03, 0x11,
						 0x10);
			break;
		case 16:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x10, 0x03, 0x11,
						 0x11);
			break;
		case 17:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x08, 0x03, 0x11,
						 0x14);
			break;
		case 21:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x30, 0x03, 0x11,
						 0x10);
			break;
		case 22:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x25, 0x03, 0x11,
						 0x10);
			break;
		case 23:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x10, 0x03, 0x11,
						 0x10);
			break;
		case 25:
			halbtc8822b2ant_set_tdma(btc, 0x51, 0x3a, 0x3, 0x11,
						 0x50);
			break;
		case 51:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x10, 0x03, 0x91,
						 0x10);
			break;
		case 101:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x25, 0x03, 0x11,
						 0x11);
			break;
		case 102:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x35, 0x03, 0x11,
						 0x11);
			break;
		case 103:
			halbtc8822b2ant_set_tdma(btc, 0x51, 0x3a, 0x3, 0x10,
						 0x50);
			break;
		case 104:
			halbtc8822b2ant_set_tdma(btc, 0x51, 0x21, 0x3, 0x10,
						 0x50);
			break;
		case 105:
			halbtc8822b2ant_set_tdma(btc, 0x51, 0x30, 0x3, 0x10,
						 0x50);
			break;
		case 106:
			halbtc8822b2ant_set_tdma(btc, 0x51, 0x10, 0x3, 0x10,
						 0x50);
			break;
		case 107:
			halbtc8822b2ant_set_tdma(btc, 0x51, 0x08, 0x7, 0x10,
						 0x54);
			break;
		case 108:
			halbtc8822b2ant_set_tdma(btc, 0x51, 0x30, 0x3, 0x10,
						 0x50 | tdma_byte4_modify);
			break;
		case 109:
			halbtc8822b2ant_set_tdma(btc, 0x51, 0x08, 0x03, 0x10,
						 0x54);
			break;
		case 110:
			halbtc8822b2ant_set_tdma(btc, 0x55, 0x30, 0x03, 0x10,
						 0x50);
			break;
		case 111:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x25, 0x03, 0x11,
						 0x11);
			break;
		case 113:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x48, 0x03, 0x11,
						 0x10);
			break;
		case 119:
			halbtc8822b2ant_set_tdma(btc, 0x61, 0x08, 0x03, 0x10,
						 0x14);
			break;
		case 151:
			halbtc8822b2ant_set_tdma(btc, 0x51, 0x10, 0x03, 0x10,
						 0x50);
			break;
		}
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], ********** TDMA(off, %d) **********\n",
			    type);
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_TDMA, FALSE);

		/* disable PS tdma */
		switch (type) {
		case 0:
			halbtc8822b2ant_set_tdma(btc, 0x0, 0x0, 0x0, 0x40, 0x0);
			break;
		default:
			halbtc8822b2ant_set_tdma(btc, 0x0, 0x0, 0x0, 0x40, 0x0);
			break;
		}
	}

	if (!coex_sta->is_set_ps_state_fail) {
		/* update pre state */
		coex_dm->cur_ps_tdma_on = turn_on;
		coex_dm->cur_ps_tdma = type;
	}

	btc->btc_set_atomic(btc, &coex_dm->setting_tdma, FALSE);
}

static void halbtc8822b2ant_set_rfe_type(struct btc_coexist *btc)
{
	struct btc_board_info *board_info = &btc->board_info;
	struct rfe_type_8822b_2ant *rfe_type = &btc->rfe_type_8822b_2ant;

	rfe_type->ext_band_switch_exist = FALSE;
	rfe_type->ext_band_switch_type =
		BT_8822B_2ANT_EXT_BAND_SWITCH_USE_SPDT; /* SPDT; */
	rfe_type->ext_band_switch_ctrl_polarity = 0;

	/* Ext switch buffer mux */
	btc->btc_write_1byte(btc, 0x974, 0xff);
	btc->btc_write_1byte_bitmask(btc, 0x1991, 0x3, 0x0);
	btc->btc_write_1byte_bitmask(btc, 0xcbe, 0x8, 0x0);

	if (rfe_type->ext_band_switch_exist) {
		/* band switch use RFE_ctrl1 (pin name: PAPE_A) and
		 * RFE_ctrl3 (pin name: LNAON_A)
		 */

		/* set RFE_ctrl1 as software control */
		btc->btc_write_1byte_bitmask(btc, 0xcb0, 0xf0, 0x7);

		/* set RFE_ctrl3 as software control */
		btc->btc_write_1byte_bitmask(btc, 0xcb1, 0xf0, 0x7);
	}

	/* the following setup should be got from Efuse in the future */
	rfe_type->rfe_module_type = board_info->rfe_type;
	rfe_type->ext_switch_polarity = 0;

	if (rfe_type->rfe_module_type == 0x12 ||
	    rfe_type->rfe_module_type == 0x15 ||
	    rfe_type->rfe_module_type == 0x16)
		rfe_type->ext_switch_exist = FALSE;
	else
		rfe_type->ext_switch_exist = TRUE;

	rfe_type->ext_switch_type = BT_8822B_2ANT_SWITCH_USE_SPDT;
}

static
void hallbtc8822b2ant_set_ant_switch(struct btc_coexist *btc,
				     boolean force_exec, u8 ctrl_type,
				     u8 pos_type)
{
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	struct rfe_type_8822b_2ant *rfe_type = &btc->rfe_type_8822b_2ant;
	boolean polarity_inverse;
	u8 val = 0;

	if (!rfe_type->ext_switch_exist)
		return;

	if (!force_exec) {
		if (((ctrl_type << 8) + pos_type) == coex_dm->cur_switch_status)
			return;
	}

	coex_dm->cur_switch_status = (ctrl_type << 8) + pos_type;

	/* swap control polarity if use different switch control polarity*/
	/* Normal switch polarity for SPDT,
	 * 0xcbd[1:0] = 2b'01 => Ant to BTG, WLA
	 * 0xcbd[1:0] = 2b'10 => Ant to WLG
	 */
	polarity_inverse = (rfe_type->ext_switch_polarity == 1);

	if (rfe_type->ext_switch_type == BT_8822B_2ANT_SWITCH_USE_SPDT) {
		switch (ctrl_type) {
		default:
		case BT_8822B_2ANT_CTRL_BY_BBSW:
			/*  0x4c[23] = 0 */
			btc->btc_write_1byte_bitmask(btc, 0x4e, 0x80, 0x0);
			/* 0x4c[24] = 1 */
			btc->btc_write_1byte_bitmask(btc, 0x4f, 0x01, 0x1);
			/* BB SW, DPDT use RFE_ctrl8 and RFE_ctrl9 as ctrl pin*/
			btc->btc_write_1byte_bitmask(btc, 0xcb4, 0xff, 0x77);

			/* 0xcbd[1:0] = 2b'01 for no switch_polarity_inverse,
			 * ANTSWB =1, ANTSW =0
			 */
			if (pos_type == BT_8822B_2ANT_SWITCH_MAIN_TO_WLG)
				val = (!polarity_inverse ? 0x2 : 0x1);
			else
				val = (!polarity_inverse ? 0x1 : 0x2);
			btc->btc_write_1byte_bitmask(btc, 0xcbd, 0x3, val);
			break;
		case BT_8822B_2ANT_CTRL_BY_PTA:
			/* 0x4c[23] = 0 */
			btc->btc_write_1byte_bitmask(btc, 0x4e, 0x80, 0x0);
			/* 0x4c[24] = 1 */
			btc->btc_write_1byte_bitmask(btc, 0x4f, 0x01, 0x1);
			/* PTA,  DPDT use RFE_ctrl8 and RFE_ctrl9 as ctrl pin */
			btc->btc_write_1byte_bitmask(btc, 0xcb4, 0xff, 0x66);

			/* 0xcbd[1:0] = 2b'10 for no switch_polarity_inverse,
			 * ANTSWB =1, ANTSW =0  @ GNT_BT=1
			 */
			val = (!polarity_inverse ? 0x2 : 0x1);
			btc->btc_write_1byte_bitmask(btc, 0xcbd, 0x3, val);
			break;
		case BT_8822B_2ANT_CTRL_BY_ANTDIV:
			/* 0x4c[23] = 0 */
			btc->btc_write_1byte_bitmask(btc, 0x4e, 0x80, 0x0);
			/* 0x4c[24] = 1 */
			btc->btc_write_1byte_bitmask(btc, 0x4f, 0x01, 0x1);
			btc->btc_write_1byte_bitmask(btc, 0xcb4, 0xff, 0x88);

			/* no regval_0xcbd setup required, because
			 * antenna switch control value by antenna diversity
			 */
			break;
		case BT_8822B_2ANT_CTRL_BY_MAC:
			/*  0x4c[23] = 1 */
			btc->btc_write_1byte_bitmask(btc, 0x4e, 0x80, 0x1);

			/* 0x64[0] = 1b'0 for no switch_polarity_inverse,
			 * DPDT_SEL_N =1, DPDT_SEL_P =0
			 */
			val = (!polarity_inverse ? 0x0 : 0x1);
			btc->btc_write_1byte_bitmask(btc, 0x64, 0x1, val);
			break;
		case BT_8822B_2ANT_CTRL_BY_FW:
			/* 0x4c[23] = 0 */
			btc->btc_write_1byte_bitmask(btc, 0x4e, 0x80, 0x0);
			/* 0x4c[24] = 1 */
			btc->btc_write_1byte_bitmask(btc, 0x4f, 0x01, 0x1);
			break;
		case BT_8822B_2ANT_CTRL_BY_BT:
			/* 0x4c[23] = 0 */
			btc->btc_write_1byte_bitmask(btc, 0x4e, 0x80, 0x0);
			/* 0x4c[24] = 0 */
			btc->btc_write_1byte_bitmask(btc, 0x4f, 0x01, 0x0);

			/* no setup required, because antenna switch control
			 * value by BT vendor 0xac[1:0]
			 */
			break;
		}
	}
}

static void halbtc8822b2ant_set_ant_path(struct btc_coexist *btc,
					 u8 ant_pos_type, boolean force_exec,
					 u8 phase)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	u32 u32tmp1 = 0, u32tmp2 = 0;
	u8 u8tmp = 0, ctrl_type, pos_type;

	if (!force_exec) {
		if (coex_dm->cur_ant_pos_type == ((ant_pos_type << 8) + phase))
			return;
	}

	coex_dm->cur_ant_pos_type = (ant_pos_type << 8) + phase;

	coex_sta->is_2g_freerun =
		((phase == BT_8822B_2ANT_PHASE_2G_FREERUN) ? TRUE : FALSE);

	if (btc->dbg_mode) {
		u32tmp1 = btc->btc_read_4byte(btc, 0xcbc);
		u32tmp2 = btc->btc_read_4byte(btc, 0xcb4);
		u8tmp  = btc->btc_read_1byte(btc, 0x73);

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], (Before Ant Setup) 0xcb4 = 0x%x, 0xcbc = 0x%x, 0x73 = 0x%x\n",
			    u32tmp1, u32tmp2, u8tmp);
		BTC_TRACE(trace_buf);
	}

	ctrl_type = BT_8822B_2ANT_CTRL_BY_BBSW;
	pos_type = BT_8822B_2ANT_SWITCH_MAIN_TO_WLG;

	switch (phase) {
	case BT_8822B_2ANT_PHASE_POWERON:

		/* set Path control owner to WL at initial step */
		halbtc8822b2ant_coex_ctrl_owner(btc, BT_8822B_2ANT_PCO_BTSIDE);

		coex_sta->run_time_state = FALSE;
		pos_type = BT_8822B_2ANT_SWITCH_MAIN_TO_WLG;

		break;
	case BT_8822B_2ANT_PHASE_INIT:
		/* Disable LTE Coex Function in WiFi side
		 * (this should be on if LTE coex is required)
		 */
		halbtc8822b2ant_ltecoex_enable(btc, 0x0);

		/* GNT_WL_LTE always = 1
		 * (this should be config if LTE coex is required)
		 */
		halbtc8822b2ant_ltecoex_table(btc,
					      BT_8822B_2ANT_CTT_WL_VS_LTE,
					      0xffff);

		/* GNT_BT_LTE always = 1
		 * (this should be config if LTE coex is required)
		 */
		halbtc8822b2ant_ltecoex_table(btc,
					      BT_8822B_2ANT_CTT_BT_VS_LTE,
					      0xffff);

		/* set Path control owner to WL at initial step */
		halbtc8822b2ant_coex_ctrl_owner(btc, BT_8822B_2ANT_PCO_WLSIDE);

		/* set GNT_BT to SW high */
		halbtc8822b2ant_set_gnt_bt(btc, BTC_GNT_SET_SW_HIGH);
		/* Set GNT_WL to SW high */
		halbtc8822b2ant_set_gnt_wl(btc, BTC_GNT_SET_SW_HIGH);

		pos_type = BT_8822B_2ANT_SWITCH_MAIN_TO_WLG;

		coex_sta->run_time_state = FALSE;

		break;
	case BT_8822B_2ANT_PHASE_WONLY:
		/* Disable LTE Coex Function in WiFi side
		 * (this should be on if LTE coex is required)
		 */
		halbtc8822b2ant_ltecoex_enable(btc, 0x0);

		/* GNT_WL_LTE always = 1
		 * (this should be config if LTE coex is required)
		 */
		halbtc8822b2ant_ltecoex_table(btc,
					      BT_8822B_2ANT_CTT_WL_VS_LTE,
					      0xffff);

		/* GNT_BT_LTE always = 1
		 * (this should be config if LTE coex is required)
		 */
		halbtc8822b2ant_ltecoex_table(btc,
					      BT_8822B_2ANT_CTT_BT_VS_LTE,
					      0xffff);

		/* set Path control owner to WL at initial step */
		halbtc8822b2ant_coex_ctrl_owner(btc, BT_8822B_2ANT_PCO_WLSIDE);

		/* set GNT_BT to SW Low */
		halbtc8822b2ant_set_gnt_bt(btc, BTC_GNT_SET_SW_LOW);
		/* Set GNT_WL to SW high */
		halbtc8822b2ant_set_gnt_wl(btc, BTC_GNT_SET_SW_HIGH);

		pos_type = BT_8822B_2ANT_SWITCH_MAIN_TO_WLG;

		coex_sta->run_time_state = FALSE;

		break;
	case BT_8822B_2ANT_PHASE_WOFF:
		/* Disable LTE Coex Function in WiFi side */
		halbtc8822b2ant_ltecoex_enable(btc, 0x0);

		/* set Path control owner to BT */
		halbtc8822b2ant_coex_ctrl_owner(btc, BT_8822B_2ANT_PCO_BTSIDE);

		pos_type = BT_8822B_2ANT_SWITCH_MAIN_TO_WLA;

		coex_sta->run_time_state = FALSE;
		break;
	case BT_8822B_2ANT_PHASE_2G:
	case BT_8822B_2ANT_PHASE_2G_FREERUN:

		/* set Path control owner to WL at runtime step */
		halbtc8822b2ant_coex_ctrl_owner(btc, BT_8822B_2ANT_PCO_WLSIDE);

		if (phase == BT_8822B_2ANT_PHASE_2G_FREERUN) {
			/* set GNT_BT to SW Hi */
			halbtc8822b2ant_set_gnt_bt(btc, BTC_GNT_SET_SW_HIGH);

			/* Set GNT_WL to SW Hi */
			halbtc8822b2ant_set_gnt_wl(btc, BTC_GNT_SET_SW_HIGH);
		} else {
			/* set GNT_BT to PTA */
			halbtc8822b2ant_set_gnt_bt(btc, BTC_GNT_SET_HW_PTA);

			/* Set GNT_WL to PTA */
			halbtc8822b2ant_set_gnt_wl(btc, BTC_GNT_SET_HW_PTA);
		}

		coex_sta->run_time_state = TRUE;

		pos_type = BT_8822B_2ANT_SWITCH_MAIN_TO_WLG;
		break;

	case BT_8822B_2ANT_PHASE_5G:

		/* set Path control owner to WL at runtime step */
		halbtc8822b2ant_coex_ctrl_owner(btc, BT_8822B_2ANT_PCO_WLSIDE);

		/* set GNT_BT to SW Hi */
		halbtc8822b2ant_set_gnt_bt(btc, BTC_GNT_SET_SW_HIGH);
		/* Set GNT_WL to SW Hi */
		halbtc8822b2ant_set_gnt_wl(btc, BTC_GNT_SET_SW_HIGH);

		coex_sta->run_time_state = TRUE;

		pos_type = BT_8822B_2ANT_SWITCH_MAIN_TO_WLA;

		break;
	case BT_8822B_2ANT_PHASE_BTMP:
		/* Disable LTE Coex Function in WiFi side */
		halbtc8822b2ant_ltecoex_enable(btc, 0x0);

		/* set Path control owner to WL */
		halbtc8822b2ant_coex_ctrl_owner(btc, BT_8822B_2ANT_PCO_WLSIDE);

		/* set GNT_BT to SW Hi */
		halbtc8822b2ant_set_gnt_bt(btc, BTC_GNT_SET_SW_HIGH);

		/* Set GNT_WL to SW Lo */
		halbtc8822b2ant_set_gnt_wl(btc, BTC_GNT_SET_SW_LOW);

		pos_type = BT_8822B_2ANT_SWITCH_MAIN_TO_WLA;

		coex_sta->run_time_state = FALSE;
		break;
	}

	hallbtc8822b2ant_set_ant_switch(btc, force_exec, ctrl_type, pos_type);

	if (btc->dbg_mode) {
		u32tmp1 = btc->btc_read_4byte(btc, 0xcbc);
		u32tmp2 = btc->btc_read_4byte(btc, 0xcb4);
		u8tmp  = btc->btc_read_1byte(btc, 0x73);

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], (After Ant Setup) 0xcb4 = 0x%x, 0xcbc = 0x%x, 0x73 = 0x%x\n",
			    u32tmp1, u32tmp2, u8tmp);
		BTC_TRACE(trace_buf);
	}
}

static u8 halbtc8822b2ant_action_algorithm(struct btc_coexist *btc)
{
	struct btc_bt_link_info *bt_link_info = &btc->bt_link_info;
	u8 algorithm = BT_8822B_2ANT_COEX_UNDEFINED;
	u8 profile_map = 0;

	if (bt_link_info->sco_exist)
		profile_map = profile_map | BIT(0);

	if (bt_link_info->hid_exist)
		profile_map = profile_map | BIT(1);

	if (bt_link_info->a2dp_exist)
		profile_map = profile_map | BIT(2);

	if (bt_link_info->pan_exist)
		profile_map = profile_map | BIT(3);

	switch (profile_map) {
	case 0:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], No BT link exists!!!\n");
		BTC_TRACE(trace_buf);
		algorithm = BT_8822B_2ANT_COEX_UNDEFINED;
		break;
	case 1:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT Profile = SCO only\n");
		BTC_TRACE(trace_buf);
		algorithm = BT_8822B_2ANT_COEX_SCO;
		break;
	case 2:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT Profile = HID only\n");
		BTC_TRACE(trace_buf);
		algorithm = BT_8822B_2ANT_COEX_HID;
		break;
	case 3:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT Profile = SCO + HID ==> HID\n");
		BTC_TRACE(trace_buf);
		algorithm = BT_8822B_2ANT_COEX_HID;
		break;
	case 4:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT Profile = A2DP only\n");
		BTC_TRACE(trace_buf);
		algorithm = BT_8822B_2ANT_COEX_A2DP;
		break;
	case 5:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT Profile = SCO + A2DP ==> HID + A2DP\n");
		BTC_TRACE(trace_buf);
		algorithm = BT_8822B_2ANT_COEX_HID_A2DP;
		break;
	case 6:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT Profile = HID + A2DP\n");
		BTC_TRACE(trace_buf);
		algorithm = BT_8822B_2ANT_COEX_HID_A2DP;
		break;
	case 7:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT Profile = SCO + HID + A2DP ==> HID + A2DP\n");
		BTC_TRACE(trace_buf);
		algorithm = BT_8822B_2ANT_COEX_HID_A2DP;
		break;
	case 8:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT Profile = PAN(EDR) only\n");
		BTC_TRACE(trace_buf);
		algorithm = BT_8822B_2ANT_COEX_PAN;
		break;
	case 9:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT Profile = SCO + PAN(EDR) ==> HID + PAN(EDR)\n");
		BTC_TRACE(trace_buf);
		algorithm = BT_8822B_2ANT_COEX_PAN_HID;
		break;
	case 10:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT Profile = HID + PAN(EDR)\n");
		BTC_TRACE(trace_buf);
		algorithm = BT_8822B_2ANT_COEX_PAN_HID;
		break;
	case 11:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT Profile = SCO + HID + PAN(EDR) ==> HID + PAN(EDR)\n");
		BTC_TRACE(trace_buf);
		algorithm = BT_8822B_2ANT_COEX_PAN_HID;
		break;
	case 12:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT Profile = A2DP + PAN(EDR)\n");
		BTC_TRACE(trace_buf);
		algorithm = BT_8822B_2ANT_COEX_PAN_A2DP;
		break;
	case 13:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT Profile = SCO + A2DP + PAN(EDR) ==> A2DP + PAN(EDR)\n");
		BTC_TRACE(trace_buf);
		algorithm = BT_8822B_2ANT_COEX_PAN_A2DP;
		break;
	case 14:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT Profile = HID + A2DP + PAN(EDR) ==> A2DP + PAN(EDR)\n");
		BTC_TRACE(trace_buf);
		algorithm = BT_8822B_2ANT_COEX_PAN_A2DP;
		break;
	case 15:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT Profile = SCO + HID + A2DP + PAN(EDR) ==> A2DP + PAN(EDR)\n");
		BTC_TRACE(trace_buf);
		algorithm = BT_8822B_2ANT_COEX_PAN_A2DP;
		break;
	}

	return algorithm;
}

static void halbtc8822b2ant_action_freerun(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	static u8 pre_wifi_rssi_state1 = BTC_RSSI_STATE_LOW,
		pre_wifi_rssi_state2 = BTC_RSSI_STATE_LOW,
		pre_wifi_rssi_state3 = BTC_RSSI_STATE_LOW;
	u8 wifi_rssi_state1, wifi_rssi_state2, wifi_rssi_state3;
	static u8 pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8 bt_rssi_state, lna_lvl = 1;
	u8 bt_tx_offset = 0;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], ************* freerunXXXX*************\n");
	BTC_TRACE(trace_buf);

	wifi_rssi_state1 =
		halbtc8822b2ant_wifi_rssi_state(btc, &pre_wifi_rssi_state1, 2,
						60, 0);

	wifi_rssi_state2 =
		halbtc8822b2ant_wifi_rssi_state(btc, &pre_wifi_rssi_state2, 2,
						50, 0);

	wifi_rssi_state3 =
		halbtc8822b2ant_wifi_rssi_state(btc, &pre_wifi_rssi_state3, 2,
						44, 0);

	bt_rssi_state =
		halbtc8822b2ant_bt_rssi_state(btc, &pre_bt_rssi_state, 2,
					      55, 0);

	halbtc8822b2ant_table(btc, NM_EXCU, 0);

	halbtc8822b2ant_tdma(btc, NM_EXCU, FALSE, 0);

	halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, FC_EXCU,
				     BT_8822B_2ANT_PHASE_2G_FREERUN);

	btc->btc_set(btc, BTC_SET_BL_BT_LNA_CONSTRAIN_LEVEL, &lna_lvl);

	halbtc8822b2ant_update_wifi_ch_info(btc, BTC_MEDIA_CONNECT);

	halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_CQDDR, TRUE);

	if (coex_sta->wl_noisy_level == 0)
		bt_tx_offset = 3;

	/*avoid tdma off to write 0xc5b ,0xe5b */
	halbtc8822b2ant_set_bt_rx_gain(btc, FC_EXCU, TRUE);
	halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, TRUE);

	if (BTC_RSSI_HIGH(wifi_rssi_state1)) {
		halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xc8);
		halbtc8822b2ant_set_bt_tx_power(btc, FC_EXCU,
						0x0 - bt_tx_offset);
	} else if (BTC_RSSI_HIGH(wifi_rssi_state2)) {
		halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xcc);
		halbtc8822b2ant_set_bt_tx_power(btc, FC_EXCU,
						0xfa - bt_tx_offset);
	} else if (BTC_RSSI_HIGH(wifi_rssi_state3)) {
		halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd0);
		halbtc8822b2ant_set_bt_tx_power(btc, FC_EXCU, 0xf7
						- bt_tx_offset);
	} else {
		halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd4);
		halbtc8822b2ant_set_bt_tx_power(btc, FC_EXCU, 0xf3);
	}
}

static void halbtc8822b2ant_action_coex_all_off(struct btc_coexist *btc)
{
	halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd8);
	halbtc8822b2ant_set_bt_tx_power(btc, NM_EXCU, 0);
	halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, FALSE);
	halbtc8822b2ant_set_bt_rx_gain(btc, NM_EXCU, FALSE);

	halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
				     BT_8822B_2ANT_PHASE_2G);

	halbtc8822b2ant_table(btc, NM_EXCU, 5);
	halbtc8822b2ant_tdma(btc, NM_EXCU, FALSE, 0);
}

static void halbtc8822b2ant_action_bt_whql_test(struct btc_coexist *btc)
{
	halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd8);
	halbtc8822b2ant_set_bt_tx_power(btc, NM_EXCU, 0);
	halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, FALSE);
	halbtc8822b2ant_set_bt_rx_gain(btc, NM_EXCU, FALSE);

	halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
				     BT_8822B_2ANT_PHASE_2G);

	halbtc8822b2ant_table(btc, NM_EXCU, 0);
	halbtc8822b2ant_tdma(btc, NM_EXCU, FALSE, 0);
}

static void halbtc8822b2ant_action_bt_relink(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct btc_bt_link_info *bt_link_info = &btc->bt_link_info;

	if ((!coex_sta->is_bt_multi_link && !bt_link_info->pan_exist) ||
	    (bt_link_info->a2dp_exist && bt_link_info->hid_exist)) {
		halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd8);
		halbtc8822b2ant_set_bt_tx_power(btc, NM_EXCU, 0);
		halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, FALSE);
		halbtc8822b2ant_set_bt_rx_gain(btc, NM_EXCU, FALSE);

		halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
					     BT_8822B_2ANT_PHASE_2G);

		halbtc8822b2ant_table(btc, NM_EXCU, 5);
		halbtc8822b2ant_tdma(btc, NM_EXCU, FALSE, 0);
	}
}

static void halbtc8822b2ant_action_bt_idle(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	boolean wifi_busy = FALSE;

	halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd8);
	halbtc8822b2ant_set_bt_tx_power(btc, NM_EXCU, 0);
	halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, FALSE);
	halbtc8822b2ant_set_bt_rx_gain(btc, NM_EXCU, FALSE);

	if (coex_dm->bt_status == BT_8822B_2ANT_BSTATUS_NCON_IDLE)
		halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
					     BT_8822B_2ANT_PHASE_2G_FREERUN);
	else
		halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
					     BT_8822B_2ANT_PHASE_2G);

#ifdef PLATFORM_WINDOWS
	if (coex_sta->wl_noisy_level > 0) {
		halbtc8822b2ant_table(btc, NM_EXCU, 17);
		halbtc8822b2ant_tdma(btc, NM_EXCU, FALSE, 0);
		return;
	}
#endif

	btc->btc_get(btc, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	if (!wifi_busy) {
		halbtc8822b2ant_table(btc, NM_EXCU, 8);
		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 14);
	} else { /* if wl busy */
		if ((coex_sta->bt_ble_scan_type & 0x2) &&
		    coex_dm->bt_status ==
		    BT_8822B_2ANT_BSTATUS_NCON_IDLE) {
			halbtc8822b2ant_table(btc, NM_EXCU, 14);
			halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 12);
		} else {
			halbtc8822b2ant_table(btc, NM_EXCU, 3);
			halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 12);
		}
	}
}

static void halbtc8822b2ant_action_bt_mr(struct btc_coexist *btc)
{
	struct wifi_link_info_8822b_2ant *wifi_link_info_ext =
					 &btc->wifi_link_info_8822b_2ant;

	if (!wifi_link_info_ext->is_all_under_5g) {
		halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
					     BT_8822B_2ANT_PHASE_2G);

		halbtc8822b2ant_table(btc, NM_EXCU, 0);
		halbtc8822b2ant_tdma(btc, NM_EXCU, FALSE, 0);
	} else {
		halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
					     BT_8822B_2ANT_PHASE_5G);

		halbtc8822b2ant_table(btc, NM_EXCU, 0);
		halbtc8822b2ant_tdma(btc, NM_EXCU, FALSE, 0);
	}
}

static void halbtc8822b2ant_action_bt_inquiry(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	boolean wifi_connected = FALSE, wifi_busy = FALSE;
	struct	btc_bt_link_info *bt_link_info = &btc->bt_link_info;

	btc->btc_get(btc, BTC_GET_BL_WIFI_CONNECTED, &wifi_connected);
	btc->btc_get(btc, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd8);
	halbtc8822b2ant_set_bt_tx_power(btc, NM_EXCU, 0);
	halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, FALSE);
	halbtc8822b2ant_set_bt_rx_gain(btc, NM_EXCU, FALSE);

	halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
				     BT_8822B_2ANT_PHASE_2G);

	if (coex_sta->is_wifi_linkscan_process ||
	    coex_sta->wifi_high_pri_task1 ||
	    coex_sta->wifi_high_pri_task2) {
		if (coex_sta->bt_create_connection) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], bt page +  wifi hi-pri task\n");
			BTC_TRACE(trace_buf);

			halbtc8822b2ant_table(btc, NM_EXCU, 12);

			if (bt_link_info->a2dp_exist &&
			    !bt_link_info->pan_exist) {
				halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 17);
			} else if (coex_sta->wifi_high_pri_task1) {
				halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 113);
			} else {
				halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 21);
			}
		} else {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], bt inquiry +  wifi hi-pri task\n");
			BTC_TRACE(trace_buf);

			halbtc8822b2ant_table(btc, NM_EXCU, 12);
			halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 21);
		}
	} else if (wifi_busy) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], bt inq/page +  wifi busy\n");
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_table(btc, NM_EXCU, 12);
		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 23);
	} else if (wifi_connected) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], bt inq/page +  wifi connected\n");
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_table(btc, NM_EXCU, 12);
		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 23);
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], bt inq/page +  wifi not-connected\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_table(btc, NM_EXCU, 5);
		halbtc8822b2ant_tdma(btc, NM_EXCU, FALSE, 0);
	}
}

static void halbtc8822b2ant_action_sco(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;

	halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd8);
	halbtc8822b2ant_set_bt_tx_power(btc, NM_EXCU, 0);
	halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, FALSE);
	halbtc8822b2ant_set_bt_rx_gain(btc, NM_EXCU, FALSE);

	halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
				     BT_8822B_2ANT_PHASE_2G);

	if (coex_sta->is_bt_multi_link) {
		halbtc8822b2ant_table(btc, NM_EXCU, 8);
		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 25);
	} else {
		if (coex_sta->is_esco_mode)
			halbtc8822b2ant_table(btc, NM_EXCU, 1);
		else /* 2-Ant free run if SCO mode */
			halbtc8822b2ant_table(btc, NM_EXCU, 0);

		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 8);
	}
}

static void halbtc8822b2ant_action_hid(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;

	halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd8);
	halbtc8822b2ant_set_bt_tx_power(btc, NM_EXCU, 0);
	halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, FALSE);
	halbtc8822b2ant_set_bt_rx_gain(btc, NM_EXCU,  FALSE);

	halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
				     BT_8822B_2ANT_PHASE_2G);

	if (coex_sta->is_hid_rcu) {
		if (coex_sta->wl_noisy_level > 0) {
			halbtc8822b2ant_table(btc, NM_EXCU, 15);
			halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 102);
		} else {
			halbtc8822b2ant_table(btc, NM_EXCU, 16);
			halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 13);
		}
	} else if (coex_sta->bt_a2dp_active) {
		halbtc8822b2ant_table(btc, NM_EXCU, 12);
		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 108);
	} else {
		halbtc8822b2ant_table(btc, NM_EXCU, 12);
		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 111);
	}
}

static void halbtc8822b2ant_action_a2dpsink(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;

	halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd8);
	halbtc8822b2ant_set_bt_tx_power(btc, NM_EXCU, 0);
	halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, FALSE);
	halbtc8822b2ant_set_bt_rx_gain(btc, NM_EXCU, FALSE);

	halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
				     BT_8822B_2ANT_PHASE_2G);

	halbtc8822b2ant_table(btc, NM_EXCU, 8);
	halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 104);
}

static void halbtc8822b2ant_action_a2dp(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	static u8 prewifi_rssi_state2 = BTC_RSSI_STATE_LOW;
	u8 wifi_rssi_state2;
	boolean wifi_busy = FALSE;

	btc->btc_get(btc, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	if (!wifi_busy)
		wifi_busy = coex_sta->gl_wifi_busy;

	wifi_rssi_state2 =
		halbtc8822b2ant_wifi_rssi_state(btc, &prewifi_rssi_state2, 2,
						45, 0);
	halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd8);
	halbtc8822b2ant_set_bt_tx_power(btc, NM_EXCU, 0);
	halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, FALSE);
	halbtc8822b2ant_set_bt_rx_gain(btc, NM_EXCU, FALSE);

	halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
				     BT_8822B_2ANT_PHASE_2G);

	if (!wifi_busy) {
		halbtc8822b2ant_table(btc, NM_EXCU, 0);
		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 12);
	} else if (BTC_RSSI_HIGH(wifi_rssi_state2)) {
		halbtc8822b2ant_table(btc, NM_EXCU, 8);
		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 119);
	} else {
		halbtc8822b2ant_table(btc, NM_EXCU, 8);
		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 109);
	}
}

static void halbtc8822b2ant_action_pan(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	boolean wifi_busy = FALSE;

	btc->btc_get(btc, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd8);
	halbtc8822b2ant_set_bt_tx_power(btc, NM_EXCU, 0);
	halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, FALSE);
	halbtc8822b2ant_set_bt_rx_gain(btc, NM_EXCU, FALSE);

	halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
				     BT_8822B_2ANT_PHASE_2G);

	halbtc8822b2ant_table(btc, NM_EXCU, 8);

	if (wifi_busy)
		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 103);
	else
		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 104);
}

static void halbtc8822b2ant_action_hid_a2dp(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;

	static u8 prewifi_rssi_state2 = BTC_RSSI_STATE_LOW;
	u8 wifi_rssi_state2;

	wifi_rssi_state2 =
		halbtc8822b2ant_wifi_rssi_state(btc, &prewifi_rssi_state2, 2,
						45, 0);

	halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd8);
	halbtc8822b2ant_set_bt_tx_power(btc, NM_EXCU, 0);
	halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, FALSE);
	halbtc8822b2ant_set_bt_rx_gain(btc, NM_EXCU, FALSE);

	halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
				     BT_8822B_2ANT_PHASE_2G);

	if (coex_sta->is_hid_rcu)
		halbtc8822b2ant_table(btc, NM_EXCU, 15);
	else
		halbtc8822b2ant_table(btc, NM_EXCU, 12);

	if (BTC_RSSI_HIGH(wifi_rssi_state2))
		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 119);
	else
		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 109);
}

static void halbtc8822b2ant_action_pan_a2dp(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	boolean wifi_busy = FALSE;

	btc->btc_get(btc, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd8);
	halbtc8822b2ant_set_bt_tx_power(btc, NM_EXCU, 0);
	halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, FALSE);
	halbtc8822b2ant_set_bt_rx_gain(btc, NM_EXCU, FALSE);

	halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
				     BT_8822B_2ANT_PHASE_2G);

	halbtc8822b2ant_table(btc, NM_EXCU, 8);
	if (wifi_busy)
		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 107);
	else
		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 106);
}

static void halbtc8822b2ant_action_pan_hid(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	boolean wifi_busy = FALSE;

	btc->btc_get(btc, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd8);
	halbtc8822b2ant_set_bt_tx_power(btc, NM_EXCU, 0);
	halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, FALSE);
	halbtc8822b2ant_set_bt_rx_gain(btc, NM_EXCU, FALSE);

	halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
				     BT_8822B_2ANT_PHASE_2G);

	halbtc8822b2ant_table(btc, NM_EXCU, 12);

	if (wifi_busy)
		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 103);
	else
		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 104);
}

static void
halbtc8822b2ant_action_hid_a2dp_pan(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	u8  val;
	boolean wifi_busy = FALSE;
	u32 wifi_bw = 1;

	btc->btc_get(btc, BTC_GET_U4_WIFI_BW, &wifi_bw);
	btc->btc_get(btc, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd8);
	halbtc8822b2ant_set_bt_tx_power(btc, NM_EXCU, 0);
	halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, FALSE);
	halbtc8822b2ant_set_bt_rx_gain(btc, NM_EXCU, FALSE);

	halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
				     BT_8822B_2ANT_PHASE_2G);

	if (coex_sta->hid_busy_num >= 2) {
		halbtc8822b2ant_table(btc, NM_EXCU, 12);

		if (wifi_bw == 0)
			val = 0x1;
		else
			val = 0x2;

		halbtc8822b2ant_wltoggle_table(btc, NM_EXCU, val,
					       0xaa, 0x5a,
					       0xaa, 0xaa);

		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 110);
	} else {
		halbtc8822b2ant_table(btc, NM_EXCU, 1);

		if (wifi_busy) {
			if (coex_sta->a2dp_bit_pool > 40 &&
			    coex_sta->a2dp_bit_pool < 255)
				halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 107);
			else
				halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 105);
		} else {
			halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 106);
		}
	}
}

static void halbtc8822b2ant_action_wifi_under5g(struct btc_coexist *btc)
{
	/* fw all off */
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], ************* under5g *************\n");
	BTC_TRACE(trace_buf);

	halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd8);
	halbtc8822b2ant_set_bt_tx_power(btc, NM_EXCU, 0);
	halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, FALSE);
	halbtc8822b2ant_set_bt_rx_gain(btc, NM_EXCU, FALSE);

	halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, FC_EXCU,
				     BT_8822B_2ANT_PHASE_5G);

	halbtc8822b2ant_table(btc, NM_EXCU, 0);
	halbtc8822b2ant_tdma(btc, NM_EXCU, FALSE, 0);
}

static void
halbtc8822b2ant_action_wifi_native_lps(struct btc_coexist *btc)
{
	struct wifi_link_info_8822b_2ant *wifi_link_info_ext =
					 &btc->wifi_link_info_8822b_2ant;
	struct btc_bt_link_info *bt_link_info = &btc->bt_link_info;

	if (wifi_link_info_ext->is_all_under_5g)
		return;

	halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd8);
	halbtc8822b2ant_set_bt_tx_power(btc, NM_EXCU, 0);
	halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, FALSE);
	halbtc8822b2ant_set_bt_rx_gain(btc, NM_EXCU, FALSE);

	halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
				     BT_8822B_2ANT_PHASE_2G);

	halbtc8822b2ant_table(btc, NM_EXCU, 8);
	halbtc8822b2ant_tdma(btc, NM_EXCU, FALSE, 0);
}

static void
halbtc8822b2ant_action_wifi_linkscan(struct btc_coexist *btc)
{
	struct btc_bt_link_info *bt_link_info = &btc->bt_link_info;

	halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd8);
	halbtc8822b2ant_set_bt_tx_power(btc, NM_EXCU, 0);
	halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, FALSE);
	halbtc8822b2ant_set_bt_rx_gain(btc, NM_EXCU, FALSE);

	halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
				     BT_8822B_2ANT_PHASE_2G);

	if (bt_link_info->pan_exist) {
		halbtc8822b2ant_table(btc, NM_EXCU, 8);
		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 22);
	} else if (bt_link_info->a2dp_exist) {
		halbtc8822b2ant_table(btc, NM_EXCU, 8);
		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 16);

	} else {
		halbtc8822b2ant_table(btc, NM_EXCU, 8);
		halbtc8822b2ant_tdma(btc, NM_EXCU, TRUE, 21);
	}
}

static void
halbtc8822b2ant_action_wifi_not_connected(struct btc_coexist *btc)
{
	halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd8);
	halbtc8822b2ant_set_bt_tx_power(btc, NM_EXCU, 0);
	halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, FALSE);
	halbtc8822b2ant_set_bt_rx_gain(btc, NM_EXCU, FALSE);

	halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
				     BT_8822B_2ANT_PHASE_2G);

	halbtc8822b2ant_table(btc, NM_EXCU, 0);
	halbtc8822b2ant_tdma(btc, NM_EXCU, FALSE, 0);
}

static void halbtc8822b2ant_action_wifi_connected(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;

	coex_dm->cur_algorithm  = halbtc8822b2ant_action_algorithm(btc);

	switch (coex_dm->cur_algorithm) {
	case BT_8822B_2ANT_COEX_SCO:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Action 2-Ant, algorithm = SCO.\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_action_sco(btc);
		break;
	case BT_8822B_2ANT_COEX_HID:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Action 2-Ant, algorithm = HID.\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_action_hid(btc);
		break;
	case BT_8822B_2ANT_COEX_A2DP:
		if (halbtc8822b2ant_freerun_check(btc))	{
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Action 2-Ant, algorithm = A2DP -> Freerun\n");
			BTC_TRACE(trace_buf);
			halbtc8822b2ant_action_freerun(btc);
		} else if (coex_sta->is_bt_multi_link &&
			   coex_sta->hid_pair_cnt == 0) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Action 2-Ant, algorithm = A2DP + PAN\n");
			BTC_TRACE(trace_buf);
			halbtc8822b2ant_action_pan_a2dp(btc);
		} else if (coex_sta->is_bt_a2dp_sink) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Action 2-Ant, algorithm = A2DP Sink.\n");
			BTC_TRACE(trace_buf);
			halbtc8822b2ant_action_a2dpsink(btc);
		} else {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Action 2-Ant, algorithm = A2DP.\n");
			BTC_TRACE(trace_buf);
			halbtc8822b2ant_action_a2dp(btc);
		}
		break;
	case BT_8822B_2ANT_COEX_PAN:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Action 2-Ant, algorithm = PAN(EDR).\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_action_pan(btc);
		break;
	case BT_8822B_2ANT_COEX_PAN_A2DP:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Action 2-Ant, algorithm = PAN+A2DP.\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_action_pan_a2dp(btc);
		break;
	case BT_8822B_2ANT_COEX_PAN_HID:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Action 2-Ant, algorithm = PAN(EDR)+HID.\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_action_pan_hid(btc);
		break;
	case BT_8822B_2ANT_COEX_HID_A2DP_PAN:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Action 2-Ant, algorithm = HID+A2DP+PAN.\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_action_hid_a2dp_pan(btc);
		break;
	case BT_8822B_2ANT_COEX_HID_A2DP:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Action 2-Ant, algorithm = HID+A2DP.\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_action_hid_a2dp(btc);
		break;
	default:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Action 2-Ant, algorithm = coexist All Off!!\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_action_coex_all_off(btc);
		break;
	}
}

static void
halbtc8822b2ant_action_wifi_multiport25g(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;

	halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd8);
	halbtc8822b2ant_set_bt_tx_power(btc, NM_EXCU, 0);
	halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, FALSE);
	halbtc8822b2ant_set_bt_rx_gain(btc, NM_EXCU, FALSE);

	halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_CQDDR, TRUE);

	if (coex_sta->is_setup_link || coex_sta->bt_relink_downcount != 0) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], wifi_multiport25g(), BT Relink!!\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_table(btc, NM_EXCU, 0);
		halbtc8822b2ant_tdma(btc, NM_EXCU, FALSE, 0);
	} else if (coex_sta->c2h_bt_inquiry_page) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], wifi_multiport25g(), BT Inq-Page!!\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_table(btc, NM_EXCU, 13);
		halbtc8822b2ant_tdma(btc, NM_EXCU, FALSE, 0);
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], wifi_multiport25g(), BT idle or busy!!\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_table(btc, NM_EXCU, 13);
		halbtc8822b2ant_tdma(btc, NM_EXCU, FALSE, 0);
	}
}

static void
halbtc8822b2ant_action_wifi_multiport2g(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct btc_bt_link_info *bt_link_info = &btc->bt_link_info;

	halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_CQDDR, TRUE);

	if (coex_sta->bt_disabled) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is disabled !!!\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_action_coex_all_off(btc);
	} else if (coex_sta->is_setup_link ||
		   coex_sta->bt_relink_downcount != 0) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], wifi_multiport2g, BT Relink!!\n");
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_action_freerun(btc);
	} else if (coex_sta->c2h_bt_inquiry_page) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], wifi_multiport2g, BT Inq-Page!!\n");
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_action_freerun(btc);
	} else if (coex_sta->num_of_profile == 0) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], wifi_multiport2g, BT idle!!\n");
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, NM_EXCU,
					     BT_8822B_2ANT_PHASE_2G_FREERUN);
		halbtc8822b2ant_set_wl_tx_power(btc, NM_EXCU, 0xd8);
		halbtc8822b2ant_set_bt_tx_power(btc, FC_EXCU, 0x0);
		halbtc8822b2ant_set_wl_rx_gain(btc, NM_EXCU, FALSE);
		halbtc8822b2ant_set_bt_rx_gain(btc, FC_EXCU, FALSE);

		halbtc8822b2ant_table(btc, NM_EXCU, 5);
		halbtc8822b2ant_tdma(btc, NM_EXCU, FALSE, 0);
	} else if (coex_sta->is_wifi_linkscan_process) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], wifi_multiport2g, WL scan!!\n");
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_action_freerun(btc);
	} else {
		if (!coex_sta->is_bt_multi_link &&
		    (bt_link_info->sco_exist || bt_link_info->hid_exist ||
		     coex_sta->is_hid_rcu)) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], wifi_multiport2g, 2G multi-port + BT HID/HFP/RCU!!\n");
			BTC_TRACE(trace_buf);

			halbtc8822b2ant_action_freerun(btc);
		} else {
			switch (btc->wifi_link_info.link_mode) {
			default:
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					    "[BTCoex], wifi_multiport2g, Other multi-port + BT busy!!\n");
				BTC_TRACE(trace_buf);

				halbtc8822b2ant_action_freerun(btc);
				break;
			}
		}
	}
}

static void halbtc8822b2ant_run_coex(struct btc_coexist *btc, u8 reason)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	struct wifi_link_info_8822b_2ant *wifi_link_info_ext =
					 &btc->wifi_link_info_8822b_2ant;
	boolean wifi_connected = FALSE, wifi_32k = FALSE;
	boolean scan = FALSE, link = FALSE, roam = FALSE, under_4way = FALSE;

	btc->btc_get(btc, BTC_GET_BL_WIFI_SCAN, &scan);
	btc->btc_get(btc, BTC_GET_BL_WIFI_LINK, &link);
	btc->btc_get(btc, BTC_GET_BL_WIFI_ROAM, &roam);
	btc->btc_get(btc, BTC_GET_BL_WIFI_4_WAY_PROGRESS, &under_4way);
	btc->btc_get(btc, BTC_GET_BL_WIFI_CONNECTED, &wifi_connected);
	btc->btc_get(btc, BTC_GET_BL_WIFI_LW_PWR_STATE, &wifi_32k);

	if (scan || link || roam || under_4way ||
	    reason == BT_8822B_2ANT_RSN_2GSCANSTART ||
	    reason == BT_8822B_2ANT_RSN_2GSWITCHBAND ||
	    reason == BT_8822B_2ANT_RSN_2GCONSTART ||
	    reason == BT_8822B_2ANT_RSN_2GSPECIALPKT) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], scan = %d, link = %d, roam = %d 4way = %d!!!\n",
			    scan, link, roam, under_4way);
		BTC_TRACE(trace_buf);
		coex_sta->is_wifi_linkscan_process = TRUE;
	} else {
		coex_sta->is_wifi_linkscan_process = FALSE;
	}

	/* update wifi_link_info_ext variable */
	halbtc8822b2ant_update_wifi_link_info(btc, reason);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], RunCoexistMechanism()===> reason = %d\n",
		    reason);
	BTC_TRACE(trace_buf);

	coex_sta->coex_run_reason = reason;

	if (btc->manual_control) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RunCoexistMechanism(), return for Manual CTRL <===\n");
		BTC_TRACE(trace_buf);
		return;
	}

	if (btc->stop_coex_dm) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RunCoexistMechanism(), return for Stop Coex DM <===\n");
		BTC_TRACE(trace_buf);
		return;
	}

	if (coex_sta->under_ips) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RunCoexistMechanism(), return for wifi is under IPS !!!\n");
		BTC_TRACE(trace_buf);
		return;
	}

	if (coex_sta->under_lps && wifi_32k) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RunCoexistMechanism(), return for wifi is under LPS-32K !!!\n");
		BTC_TRACE(trace_buf);
		return;
	}

	if (!coex_sta->run_time_state) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RunCoexistMechanism(), return for run_time_state = FALSE !!!\n");
		BTC_TRACE(trace_buf);
		return;
	}

	if (coex_sta->freeze_coexrun_by_btinfo && !coex_sta->is_setup_link) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RunCoexistMechanism(), return for freeze_coexrun_by_btinfo\n");
		BTC_TRACE(trace_buf);
		return;
	}

	coex_sta->coex_run_cnt++;

	if (coex_sta->msft_mr_exist && wifi_connected) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RunCoexistMechanism(), microsoft MR!!\n")
			    ;
		BTC_TRACE(trace_buf);

		coex_sta->wl_coex_mode = BT_8822B_2ANT_WLINK_BTMR;
		halbtc8822b2ant_action_bt_mr(btc);
		return;
	}

	/* Pure-5G Coex Process */
	if (wifi_link_info_ext->is_all_under_5g) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], WiFi is under 5G!!!\n");
		BTC_TRACE(trace_buf);

		coex_sta->wl_coex_mode = BT_8822B_2ANT_WLINK_5G;
		halbtc8822b2ant_action_wifi_under5g(btc);
		return;
	}

	if (wifi_link_info_ext->is_mcc_25g) { /* not iclude scan action */

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], WiFi is under mcc dual-band!!!\n");
		BTC_TRACE(trace_buf);

		coex_sta->wl_coex_mode = BT_8822B_2ANT_WLINK_25GMPORT;
		halbtc8822b2ant_action_wifi_multiport25g(btc);
		return;
	}

	/* if multi-port or P2PGO+Client_Join  */
	if (wifi_link_info_ext->num_of_active_port > 1 ||
	    (btc->wifi_link_info.link_mode == BTC_LINK_ONLY_GO &&
	     !btc->wifi_link_info.bhotspot &&
	     btc->wifi_link_info.bany_client_join_go) ||
	     btc->wifi_link_info.link_mode == BTC_LINK_ONLY_GC) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], WiFi is under scc-2g/mcc-2g/p2pGO-only/P2PGC-Only!!!\n");
		BTC_TRACE(trace_buf);

		if (btc->wifi_link_info.link_mode == BTC_LINK_ONLY_GO)
			coex_sta->wl_coex_mode = BT_8822B_2ANT_WLINK_2GGO;
		else if (btc->wifi_link_info.link_mode == BTC_LINK_ONLY_GC)
			coex_sta->wl_coex_mode = BT_8822B_2ANT_WLINK_2GGC;
		else
			coex_sta->wl_coex_mode = BT_8822B_2ANT_WLINK_2GMPORT;
		halbtc8822b2ant_action_wifi_multiport2g(btc);
		return;
	}

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], WiFi is single-port 2G!!!\n");
	BTC_TRACE(trace_buf);

	coex_sta->wl_coex_mode = BT_8822B_2ANT_WLINK_2G1PORT;

	halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_CQDDR, TRUE);

	if (coex_sta->bt_disabled) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is disabled !!!\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_action_coex_all_off(btc);
		return;
	}

	if (coex_sta->under_lps && !coex_sta->force_lps_ctrl) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RunCoexistMechanism(), wifi is under LPS !!!\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_action_wifi_native_lps(btc);
		return;
	}

	if (coex_sta->bt_whck_test) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is under WHCK TEST!!!\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_action_bt_whql_test(btc);
		return;
	}

	if (coex_sta->is_setup_link || coex_sta->bt_relink_downcount != 0) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is re-link !!!\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_action_bt_relink(btc);
		return;
	}

	if (coex_sta->c2h_bt_inquiry_page) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is under inquiry/page !!\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_action_bt_inquiry(btc);
		return;
	}

	if ((coex_dm->bt_status == BT_8822B_2ANT_BSTATUS_NCON_IDLE ||
	     coex_dm->bt_status == BT_8822B_2ANT_BSTATUS_CON_IDLE) &&
	     wifi_link_info_ext->is_connected) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "############# [BTCoex],  BT Is idle\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_action_bt_idle(btc);
		return;
	}

	if (coex_sta->is_wifi_linkscan_process) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], wifi is under linkscan process!!\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_action_wifi_linkscan(btc);
		return;
	}

	if (wifi_connected) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], wifi is under connected!!\n");
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_action_wifi_connected(btc);
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], wifi is under not-connected!!\n");
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_action_wifi_not_connected(btc);
	}
}

static void halbtc8822b2ant_init_coex_var(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;

	/* Reset Coex variable */
	btc->btc_set(btc, BTC_SET_RESET_COEX_VAR, NULL);

	coex_sta->bt_reg_vendor_ac = 0xffff;
	coex_sta->bt_reg_vendor_ae = 0xffff;

	coex_sta->isolation_btween_wb = BT_8822B_2ANT_DEFAULT_ISOLATION;
	btc->bt_info.bt_get_fw_ver = 0;
}

static void halbtc8822b2ant_init_coex_dm(struct btc_coexist *btc)
{
}

static void halbtc8822b2ant_init_hw_config(struct btc_coexist *btc,
					   boolean wifi_only)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct rfe_type_8822b_2ant *rfe_type = &btc->rfe_type_8822b_2ant;
	u32 u32tmp1 = 0, u32tmp2 = 0, u32tmp3 = 0;
	u8 i = 0;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], %s()!\n", __func__);
	BTC_TRACE(trace_buf);

#if 0
	u32tmp3 = btc->btc_read_4byte(btc, 0xcb4);
	u32tmp1 = halbtc8822b2ant_read_indirect_reg(btc, 0x38);
	u32tmp2 = halbtc8822b2ant_read_indirect_reg(btc, 0x54);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], (Before Init HW config) 0xcb4 = 0x%x, 0x38= 0x%x, 0x54= 0x%x\n",
		    u32tmp3, u32tmp1, u32tmp2);
	BTC_TRACE(trace_buf);
#endif

	halbtc8822b2ant_init_coex_var(btc);

	/* 0xf0[15:12] --> kt_ver */
	coex_sta->kt_ver = (btc->btc_read_1byte(btc, 0xf1) & 0xf0) >> 4;

	if (rfe_type->rfe_module_type == 2 || rfe_type->rfe_module_type == 4)
		halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_EXTFEM,
					   TRUE);
	else
		halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_EXTFEM,
					   FALSE);

	halbtc8822b2ant_coex_switch_thres(btc, coex_sta->isolation_btween_wb);

	/* enable TBTT nterrupt */
	btc->btc_write_1byte_bitmask(btc, 0x550, 0x8, 0x1);

	/* BT report packet sample rate	 */
	btc->btc_write_1byte(btc, 0x790, 0x5);

	/* Init 0x778 = 0x1 for 2-Ant */
	btc->btc_write_1byte(btc, 0x778, 0x1);

	/* Enable PTA (3-wire function form BT side) */
	btc->btc_write_1byte_bitmask(btc, 0x40, 0x20, 0x1);
	btc->btc_write_1byte_bitmask(btc, 0x41, 0x02, 0x1);

	/* Enable PTA (tx/rx signal form WiFi side) */
	btc->btc_write_1byte_bitmask(btc, 0x4c6, 0x30, 0x1);

	/* beacon queue always hi-pri  */
	btc->btc_write_1byte_bitmask(btc, 0x6cf, BIT(3), 0x1);

	halbtc8822b2ant_enable_gnt_to_gpio(btc, TRUE);

	/*GNT_BT=1 while select both */
	btc->btc_write_1byte_bitmask(btc, 0x763, 0x10, 0x1);

	/*Enable counter statistics */ /* 0x76e[3] =1, WLAN_Act control by PTA*/
	btc->btc_write_1byte(btc, 0x76e, 0x4);

	if (btc->wl_rf_state_off) {
		halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, FC_EXCU,
					     BT_8822B_2ANT_PHASE_WOFF);
		halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_ALL, FALSE);
		btc->stop_coex_dm = TRUE;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], %s: RF Off\n", __func__);
		BTC_TRACE(trace_buf);
	} else if (wifi_only) {
		coex_sta->concurrent_rx_mode_on = FALSE;
		/* Path config	 */
		/* Set Antenna Path */
		halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, FC_EXCU,
					     BT_8822B_2ANT_PHASE_WONLY);
		halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_ACTIVE |
					   BT_8822B_2ANT_SCBD_ONOFF,
					   TRUE);
		btc->stop_coex_dm = TRUE;
	} else {
		coex_sta->concurrent_rx_mode_on = TRUE;
		btc->btc_set_rf_reg(btc, BTC_RF_A, 0x1, 0x2, 0x0);

		/* Set Antenna Path */
		halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, FC_EXCU,
					     BT_8822B_2ANT_PHASE_INIT);
		halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_ACTIVE |
					   BT_8822B_2ANT_SCBD_ONOFF,
					   TRUE);
		btc->stop_coex_dm = FALSE;
	}

	halbtc8822b2ant_table(btc, FC_EXCU, 5);
	halbtc8822b2ant_tdma(btc, FC_EXCU, FALSE, 0);

	halbtc8822b2ant_query_bt_info(btc);
}

void ex_halbtc8822b2ant_power_on_setting(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;

	u8 u8tmp = 0x0;
	u16 u16tmp = 0x0;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], Execute %s !!\n", __func__);
	BTC_TRACE(trace_buf);

	btc->stop_coex_dm = TRUE;
	btc->wl_rf_state_off = FALSE;

	/* enable BB, REG_SYS_FUNC_EN such that we can write BB Reg correctly */
	u16tmp = btc->btc_read_2byte(btc, 0x2);
	btc->btc_write_2byte(btc, 0x2, u16tmp | BIT(0) | BIT(1));

	/* Local setting bit define
	 *	BIT0: "0" for no antenna inverse; "1" for antenna inverse
	 *	BIT1: "0" for internal switch; "1" for external switch
	 *	BIT2: "0" for one antenna; "1" for two antenna
	 * NOTE: here default all internal switch and 1-antenna ==>
	 * BIT1=0 and BIT2=0
	 */

	/* Check efuse 0xc3[6] for Single Antenna Path */

	/* Setup RF front end type */
	halbtc8822b2ant_set_rfe_type(btc);

	/* Set Antenna Path to BT side */
	halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, FC_EXCU,
				     BT_8822B_2ANT_PHASE_POWERON);

	halbtc8822b2ant_table(btc, FC_EXCU, 0);

	/* Save"single antenna position" info in Local register setting for
	 * FW reading, because FW may not ready at power on
	 */
	if (btc->chip_interface == BTC_INTF_PCI)
		btc->btc_write_local_reg_1byte(btc, 0x3e0, u8tmp);
	else if (btc->chip_interface == BTC_INTF_USB)
		btc->btc_write_local_reg_1byte(btc, 0xfe08, u8tmp);
	else if (btc->chip_interface == BTC_INTF_SDIO)
		btc->btc_write_local_reg_1byte(btc, 0x60, u8tmp);

	/* enable GNT_WL/GNT_BT debug signal to GPIO14/15 */
	halbtc8822b2ant_enable_gnt_to_gpio(btc, TRUE);

	if (btc->dbg_mode) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], LTE coex Reg 0x38 (Power-On) = 0x%x\n",
			    halbtc8822b2ant_read_indirect_reg(btc, 0x38));
		BTC_TRACE(trace_buf);

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], MACReg 0x70/ BBReg 0xcbc (Power-On) = 0x%x/ 0x%x\n",
			    btc->btc_read_4byte(btc, 0x70),
			    btc->btc_read_4byte(btc, 0xcbc));
		BTC_TRACE(trace_buf);
	}
}

void ex_halbtc8822b2ant_pre_load_firmware(struct btc_coexist *btc)
{
	struct btc_board_info *board_info = &btc->board_info;
	u8 u8tmp = 0x4; /* Set BIT2 by default since it's 2ant case */

	/* */
	/* S0 or S1 setting and Local register setting
	 * (By the setting fw can get ant number, S0/S1, ... info)
	 */
	/* Local setting bit define */
	/*	BIT0: "0" for no antenna inverse; "1" for antenna inverse  */
	/*	BIT1: "0" for internal switch; "1" for external switch */
	/*	BIT2: "0" for one antenna; "1" for two antenna */
	/* NOTE: here default all internal switch and 1-antenna ==>
	 *       BIT1=0 and BIT2=0
	 */
	if (btc->chip_interface == BTC_INTF_USB) {
		/* fixed at S0 for USB interface */
		u8tmp |= 0x1; /* antenna inverse */
		btc->btc_write_local_reg_1byte(btc, 0xfe08, u8tmp);
	} else {
		/* for PCIE and SDIO interface, we check efuse 0xc3[6] */
		if (board_info->single_ant_path == 0) {
		} else if (board_info->single_ant_path == 1) {
			/* set to S0 */
			u8tmp |= 0x1; /* antenna inverse */
		}

		if (btc->chip_interface == BTC_INTF_PCI)
			btc->btc_write_local_reg_1byte(btc, 0x3e0, u8tmp);
		else if (btc->chip_interface == BTC_INTF_SDIO)
			btc->btc_write_local_reg_1byte(btc, 0x60, u8tmp);
	}
}

void ex_halbtc8822b2ant_init_hw_config(struct btc_coexist *btc,
				       boolean wifi_only)
{
	halbtc8822b2ant_init_hw_config(btc, wifi_only);
}

void ex_halbtc8822b2ant_init_coex_dm(struct btc_coexist *btc)
{
	btc->stop_coex_dm = FALSE;
	btc->auto_report = TRUE;
	btc->dbg_mode = FALSE;

	halbtc8822b2ant_init_coex_dm(btc);
}

void ex_halbtc8822b2ant_display_simple_coex_info(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	struct rfe_type_8822b_2ant *rfe_type = &btc->rfe_type_8822b_2ant;
	struct btc_board_info *board_info = &btc->board_info;
	struct btc_bt_link_info *bt_link_info = &btc->bt_link_info;

	u8 *cli_buf = btc->cli_buf;
	u32 bt_patch_ver = 0, bt_coex_ver = 0;
	static u8 cnt;
	u8 * const p = &coex_sta->bt_afh_map[0];

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n _____[BT Coexist info]____");
	CL_PRINTF(cli_buf);

	if (btc->manual_control) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n __[Under Manual Control]_");
		CL_PRINTF(cli_buf);
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n _________________________");
		CL_PRINTF(cli_buf);
	}

	if (btc->stop_coex_dm) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n ____[Coex is STOPPED]____");
		CL_PRINTF(cli_buf);
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n _________________________");
		CL_PRINTF(cli_buf);
	}

	if (!coex_sta->bt_disabled &&
	    (coex_sta->bt_coex_supported_version == 0 ||
	    coex_sta->bt_coex_supported_version == 0xffff) &&
	    cnt == 0) {
		btc->btc_get(btc, BTC_GET_U4_SUPPORTED_FEATURE,
			     &coex_sta->bt_coex_supported_feature);

		btc->btc_get(btc, BTC_GET_U4_SUPPORTED_VERSION,
			     &coex_sta->bt_coex_supported_version);

		coex_sta->bt_reg_vendor_ac = (u16)(btc->btc_get_bt_reg(btc, 3,
								       0xac) &
						   0xffff);

		coex_sta->bt_reg_vendor_ae = (u16)(btc->btc_get_bt_reg(btc, 3,
								       0xae) &
						   0xffff);

		btc->btc_get(btc, BTC_GET_U4_BT_PATCH_VER, &bt_patch_ver);
		btc->bt_info.bt_get_fw_ver = bt_patch_ver;

		if (coex_sta->num_of_profile > 0)
			btc->btc_get_bt_afh_map_from_bt(btc, 0, p);
	}

	if (++cnt >= 3)
		cnt = 0;

	/* BT coex. info. */
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %s / %d",
		   "Ant PG Num/ Mech/ Pos/ RFE", board_info->pg_ant_num,
		   board_info->btdm_ant_num,
		   (board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT ?
			    "Main" :
			    "Aux"),
		   rfe_type->rfe_module_type);
	CL_PRINTF(cli_buf);

	bt_coex_ver = (coex_sta->bt_coex_supported_version & 0xff);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %d_%02x/ 0x%02x/ 0x%02x (%s)",
		   "CoexVer WL/  BT_Desired/ BT_Report",
		   glcoex_ver_date_8822b_2ant, glcoex_ver_8822b_2ant,
		   glcoex_ver_btdesired_8822b_2ant,
		   bt_coex_ver,
		   (bt_coex_ver == 0xff ? "Unknown" :
		    (coex_sta->bt_disabled ? "BT-disable" :
		     (bt_coex_ver >= glcoex_ver_btdesired_8822b_2ant ?
		      "Match" : "Mis-Match"))));
	CL_PRINTF(cli_buf);

	/* BT status */
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s", "BT status",
		   ((coex_sta->bt_disabled) ?
		    ("disabled") :
		    ((coex_sta->c2h_bt_inquiry_page) ?
		     ("inquiry/page") :
		     ((BT_8822B_2ANT_BSTATUS_NCON_IDLE ==
			    coex_dm->bt_status) ?
				   "non-connected idle" :
				   ((coex_dm->bt_status ==
				     BT_8822B_2ANT_BSTATUS_CON_IDLE) ?
					    "connected-idle" :
					    "busy")))));
	CL_PRINTF(cli_buf);

	/* HW Settings */
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "0x770(Hi-pri rx/tx)", coex_sta->high_priority_rx,
		   coex_sta->high_priority_tx);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d %s",
		   "0x774(Lo-pri rx/tx)", coex_sta->low_priority_rx,
		   coex_sta->low_priority_tx,
		   (bt_link_info->slave_role ? "(Slave!!)" : " "));
	CL_PRINTF(cli_buf);
}

void ex_halbtc8822b2ant_display_coex_info(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	struct rfe_type_8822b_2ant *rfe_type = &btc->rfe_type_8822b_2ant;
	struct btc_board_info *board_info = &btc->board_info;
	struct btc_bt_link_info *bt_link_info = &btc->bt_link_info;

	u8 *cli_buf = btc->cli_buf;
	u8 u8tmp[4], i, ps_tdma_case = 0;
	u32 u32tmp[4];
	u16 u16tmp[4];
	u32 fa_ofdm, fa_cck, cca_ofdm, cca_cck;
	u32 fw_ver = 0, bt_patch_ver = 0, bt_coex_ver = 0;
	static u8 pop_report_in_10s;
	u32 phyver = 0, val = 0;
	boolean lte_coex_on = FALSE, is_bt_reply = FALSE;
	static u8 cnt;
	u8 * const p = &coex_sta->bt_afh_map[0];

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n ============[BT Coexist info 8822B]============");
	CL_PRINTF(cli_buf);

	if (btc->manual_control) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n ============[Under Manual Control]============");
		CL_PRINTF(cli_buf);
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n ==========================================");
		CL_PRINTF(cli_buf);
	} else if (btc->stop_coex_dm) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n ============[Coex is STOPPED]============");
		CL_PRINTF(cli_buf);
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n ==========================================");
		CL_PRINTF(cli_buf);
	} else if (!coex_sta->run_time_state) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n ============[Run Time State = False]============");
		CL_PRINTF(cli_buf);
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n ==========================================");
		CL_PRINTF(cli_buf);
	} else if (coex_sta->freeze_coexrun_by_btinfo) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n ============[freeze_coexrun_by_btinfo]============");
		CL_PRINTF(cli_buf);
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n ==========================================");
		CL_PRINTF(cli_buf);
	}

	if (!coex_sta->bt_disabled && cnt == 0) {
		if (coex_sta->bt_coex_supported_version == 0 ||
		    coex_sta->bt_coex_supported_version == 0xffff) {
			btc->btc_get(btc, BTC_GET_U4_SUPPORTED_VERSION,
				     &coex_sta->bt_coex_supported_version);

			if (coex_sta->bt_coex_supported_version > 0 &&
			    coex_sta->bt_coex_supported_version < 0xffff)
				is_bt_reply = TRUE;
		} else {
			is_bt_reply = TRUE;
		}

		if (coex_sta->num_of_profile > 0)
			btc->btc_get_bt_afh_map_from_bt(btc, 0, p);
	}

	if (is_bt_reply) {
		if (coex_sta->bt_coex_supported_feature == 0)
			btc->btc_get(btc, BTC_GET_U4_SUPPORTED_FEATURE,
				     &coex_sta->bt_coex_supported_feature);

		if (coex_sta->bt_reg_vendor_ac == 0xffff) {
			val = btc->btc_get_bt_reg(btc, 3, 0xac);
			coex_sta->bt_reg_vendor_ac = (u16)(val & 0xffff);
		}

		if (coex_sta->bt_reg_vendor_ae == 0xffff) {
			val = btc->btc_get_bt_reg(btc, 3, 0xae);
			coex_sta->bt_reg_vendor_ae = (u16)(val & 0xffff);
		}

		if (btc->bt_info.bt_get_fw_ver == 0) {
			btc->btc_get(btc, BTC_GET_U4_BT_PATCH_VER,
				     &bt_patch_ver);
			btc->bt_info.bt_get_fw_ver = bt_patch_ver;
		}
	}

	if (++cnt >= 3)
		cnt = 0;

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %s/ %s / %d",
		   "Ant PG Num/ Mech/ Pos/ RFE", board_info->pg_ant_num,
		   (board_info->btdm_ant_num == 1 ? "Shared" : "Non-Shared"),
		   (board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT ?
			    "Main" :
			    "Aux"),
		   rfe_type->rfe_module_type);
	CL_PRINTF(cli_buf);

	bt_patch_ver = btc->bt_info.bt_get_fw_ver;
	btc->btc_get(btc, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
	phyver = btc->btc_get_bt_phydm_version(btc);
	bt_coex_ver = (coex_sta->bt_coex_supported_version & 0xff);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %d_%02x/ 0x%02x/ 0x%02x (%s)",
		   "CoexVer WL/  BT_Desired/ BT_Report",
		   glcoex_ver_date_8822b_2ant, glcoex_ver_8822b_2ant,
		   glcoex_ver_btdesired_8822b_2ant, bt_coex_ver,
		   (bt_coex_ver == 0xff ?
			"Unknown" :
			(coex_sta->bt_disabled ?
			  "BT-disable" :
			  (bt_coex_ver >= glcoex_ver_btdesired_8822b_2ant ?
				   "Match" :
				   "Mis-Match"))));
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ v%d/ %c",
		   "W_FW/ B_FW/ Phy/ Kt", fw_ver, bt_patch_ver, phyver,
		   coex_sta->kt_ver + 65);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %02x %02x %02x (RF-Ch = %d)", "AFH Map to BT",
		   coex_dm->wifi_chnl_info[0], coex_dm->wifi_chnl_info[1],
		   coex_dm->wifi_chnl_info[2], coex_sta->wl_center_channel);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d ",
		   "Isolation/WL_Thres/BT_Thres", coex_sta->isolation_btween_wb,
		   coex_sta->wifi_coex_thres, coex_sta->bt_coex_thres);
	CL_PRINTF(cli_buf);

	/* wifi status */
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
		   "============[Wifi Status]============");
	CL_PRINTF(cli_buf);
	btc->btc_disp_dbg_msg(btc, BTC_DBG_DISP_WIFI_STATUS);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
		   "============[BT Status]============");
	CL_PRINTF(cli_buf);

	pop_report_in_10s++;
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %ddBm/ %d/ %d",
		   "BT status/ rssi/ retryCnt/ popCnt",
		   ((coex_sta->bt_disabled) ?
		    ("disabled") :
		    ((coex_sta->c2h_bt_inquiry_page) ?
			  ("inquiry-page") :
			  ((BT_8822B_2ANT_BSTATUS_NCON_IDLE ==
			    coex_dm->bt_status) ?
				   "non-connected-idle" :
				   ((coex_dm->bt_status ==
				     BT_8822B_2ANT_BSTATUS_CON_IDLE) ?
					    "connected-idle" :
					    "busy")))),
		coex_sta->bt_rssi - 100, coex_sta->bt_retry_cnt,
		coex_sta->pop_event_cnt);
	CL_PRINTF(cli_buf);

	if (pop_report_in_10s >= 5) {
		coex_sta->pop_event_cnt = 0;
		pop_report_in_10s = 0;
	}

	if (coex_sta->num_of_profile != 0)
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %s%s%s%s%s%s (multilink = %d)",
			   "Profiles", ((bt_link_info->a2dp_exist) ?
						((coex_sta->is_bt_a2dp_sink) ?
							 "A2DP sink," :
							 "A2DP,") :
				 ""),
			   ((bt_link_info->sco_exist) ? "HFP," : ""),
			   ((bt_link_info->hid_exist) ?
				 ((coex_sta->is_hid_rcu) ?
					  "HID(RCU)" :
					  ((coex_sta->hid_busy_num >= 2) ?
						   "HID(4/18)," :
						   "HID(2/18),")) :
				 ""),
			   ((bt_link_info->pan_exist) ?
				 ((coex_sta->is_bt_opp_exist) ? "OPP," :
								"PAN,") :
				 ""),
			   ((coex_sta->voice_over_HOGP) ? "Voice," : ""),
			   ((coex_sta->msft_mr_exist) ? "MR" : ""),
			   coex_sta->is_bt_multi_link);
	else
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s",
			   "Profiles",
			   (coex_sta->msft_mr_exist) ? "MR" : "None");

	CL_PRINTF(cli_buf);

	if (bt_link_info->a2dp_exist) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %s/ %d/ 0x%x/ 0x%x/ %d",
			   "CQDDR/Bitpool/V_ID/D_name/Flush",
			   ((coex_sta->is_A2DP_3M) ? "On" : "Off"),
			   coex_sta->a2dp_bit_pool,
			   coex_sta->bt_a2dp_vendor_id,
			   coex_sta->bt_a2dp_device_name,
			   coex_sta->bt_a2dp_flush_time);
		CL_PRINTF(cli_buf);
	}

	if (bt_link_info->hid_exist) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d",
			   "HID PairNum", coex_sta->hid_pair_cnt);
		CL_PRINTF(cli_buf);
	}

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d/ %s/ 0x%x",
		   "Role/RoleSwCnt/IgnWlact/Feature",
		   ((bt_link_info->slave_role) ? "Slave" : "Master"),
		   coex_sta->cnt_role_switch,
		   ((coex_dm->cur_ignore_wlan_act) ? "Yes" : "No"),
		   coex_sta->bt_coex_supported_feature);
	CL_PRINTF(cli_buf);

	if (coex_sta->is_ble_scan_en) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x",
			   "BLEScan Type/TV/Init/Ble",
			   coex_sta->bt_ble_scan_type,
			   (coex_sta->bt_ble_scan_type & 0x1 ?
				    coex_sta->bt_ble_scan_para[0] :
				    0x0),
			   (coex_sta->bt_ble_scan_type & 0x2 ?
				    coex_sta->bt_ble_scan_para[1] :
				    0x0),
			   (coex_sta->bt_ble_scan_type & 0x4 ?
				    coex_sta->bt_ble_scan_para[2] :
				    0x0));
		CL_PRINTF(cli_buf);
	}

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %d/ %d/ %d/ %d/ %d %s",
		   "ReInit/ReLink/IgnWlact/Page/NameReq", coex_sta->cnt_reinit,
		   coex_sta->cnt_setup_link, coex_sta->cnt_ign_wlan_act,
		   coex_sta->cnt_page, coex_sta->cnt_remote_name_req,
		   (coex_sta->is_setup_link ? "(Relink!!)" : ""));
	CL_PRINTF(cli_buf);

	halbtc8822b2ant_read_scbd(btc, &u16tmp[0]);

	if (coex_sta->bt_reg_vendor_ae == 0xffff ||
	    coex_sta->bt_reg_vendor_ac == 0xffff)
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = x/ x/ 0x%04x",
			   "0xae[4]/0xac[1:0]/ScBd(B->W)", u16tmp[0]);
	else
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = 0x%x/ 0x%x/ 0x%04x",
			   "0xae[4]/0xac[1:0]/ScBd(B->W)",
			   (int)((coex_sta->bt_reg_vendor_ae & BIT(4)) >> 4),
			   coex_sta->bt_reg_vendor_ac & 0x3, u16tmp[0]);
	CL_PRINTF(cli_buf);

	if (coex_sta->num_of_profile > 0) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x",
			   "AFH MAP", coex_sta->bt_afh_map[0],
			   coex_sta->bt_afh_map[1], coex_sta->bt_afh_map[2],
			   coex_sta->bt_afh_map[3], coex_sta->bt_afh_map[4],
			   coex_sta->bt_afh_map[5], coex_sta->bt_afh_map[6],
			   coex_sta->bt_afh_map[7], coex_sta->bt_afh_map[8],
			   coex_sta->bt_afh_map[9]);
		CL_PRINTF(cli_buf);
	}

	for (i = 0; i < BT_8822B_2ANT_INFO_SRC_MAX; i++) {
		if (coex_sta->bt_info_c2h_cnt[i]) {
			CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
				   "\r\n %-35s = %02x %02x %02x %02x %02x %02x %02x(%d)",
				   glbt_info_src_8822b_2ant[i],
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
	if (btc->manual_control)
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
			   "============[mechanism] (before Manual)============");
	else
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
			   "============[Mechanism]============");

	CL_PRINTF(cli_buf);

	ps_tdma_case = coex_dm->cur_ps_tdma;
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %02x %02x %02x %02x %02x (case-%d, %s)",
		   "TDMA",
		   coex_dm->ps_tdma_para[0], coex_dm->ps_tdma_para[1],
		   coex_dm->ps_tdma_para[2], coex_dm->ps_tdma_para[3],
		   coex_dm->ps_tdma_para[4], ps_tdma_case,
		   (coex_dm->cur_ps_tdma_on ? "TDMA-On" : "TDMA-Off"));
	CL_PRINTF(cli_buf);

	switch (coex_sta->wl_coex_mode) {
	case BT_8822B_2ANT_WLINK_2G1PORT:
	default:
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %s", "Coex_Mode", "2G-SP");
		break;
	case BT_8822B_2ANT_WLINK_2GMPORT:
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %s", "Coex_Mode", "2G-MP");
		break;
	case BT_8822B_2ANT_WLINK_25GMPORT:
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %s", "Coex_Mode", "25G-MP");
		break;
	case BT_8822B_2ANT_WLINK_5G:
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %s", "Coex_Mode", "5G");
		break;
	case BT_8822B_2ANT_WLINK_2GGO:
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %s", "Coex_Mode", "2G-P2PGO");
		break;
	case BT_8822B_2ANT_WLINK_2GGC:
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %s", "Coex_Mode", "2G-P2PGC");
		break;
	case BT_8822B_2ANT_WLINK_BTMR:
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %s", "Coex_Mode", "BT-MR");
		break;
	}

	CL_PRINTF(cli_buf);

	u32tmp[0] = btc->btc_read_4byte(btc, 0x6c0);
	u32tmp[1] = btc->btc_read_4byte(btc, 0x6c4);
	u32tmp[2] = btc->btc_read_4byte(btc, 0x6c8);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %d/ 0x%x/ 0x%x/ 0x%x",
		   "Table/0x6c0/0x6c4/0x6c8", coex_sta->coex_table_type,
		   u32tmp[0], u32tmp[1], u32tmp[2]);
	CL_PRINTF(cli_buf);

	u8tmp[0] = btc->btc_read_1byte(btc, 0x778);
	u32tmp[0] = btc->btc_read_4byte(btc, 0x6cc);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%04x/ %d/ %d",
		   "0x778/0x6cc/ScBd(W->B)/RunCnt/Rsn", u8tmp[0], u32tmp[0],
		   coex_sta->score_board_WB, coex_sta->coex_run_cnt,
		   coex_sta->coex_run_reason);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s/ %s/ %d/ %d",
		   "AntDiv/BtCtrlLPS/LPRA/PsFail/g_busy",
		   ((board_info->ant_div_cfg) ? "On" : "Off"),
		   ((coex_sta->force_lps_ctrl) ? "On" : "Off"),
		   ((coex_dm->cur_low_penalty_ra) ? "On" : "Off"),
		   coex_sta->cnt_set_ps_state_fail, coex_sta->gl_wifi_busy);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d/ %d",
		   "Null All/Retry/Ack/BT_Empty/BT_Late",
		   coex_sta->wl_fw_dbg_info[1], coex_sta->wl_fw_dbg_info[2],
		   coex_sta->wl_fw_dbg_info[3], coex_sta->wl_fw_dbg_info[4],
		   coex_sta->wl_fw_dbg_info[5]);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %s/ %d",
		   "cnt TDMA_Togg/LK5ms/LK5ms_off/fw",
		   coex_sta->wl_fw_dbg_info[6], coex_sta->wl_fw_dbg_info[7],
		   ((coex_sta->is_no_wl_5ms_extend) ? "Yes" : "No"),
		   coex_sta->cnt_wl_fw_notify);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "WL_Pwr/ BT_Pwr", coex_dm->cur_wl_pwr_lvl,
		   coex_dm->cur_bt_pwr_lvl);
	CL_PRINTF(cli_buf);

	u32tmp[0] = halbtc8822b2ant_read_indirect_reg(btc, 0x38);
	lte_coex_on = ((u32tmp[0] & BIT(7)) >> 7) ? TRUE : FALSE;

	if (lte_coex_on) {
		u32tmp[0] = halbtc8822b2ant_read_indirect_reg(btc, 0xa0);
		u32tmp[1] = halbtc8822b2ant_read_indirect_reg(btc, 0xa4);
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
			   "LTE Coex Table W_L/B_L", u32tmp[0] & 0xffff,
			   u32tmp[1] & 0xffff);
		CL_PRINTF(cli_buf);

		u32tmp[0] = halbtc8822b2ant_read_indirect_reg(btc, 0xa8);
		u32tmp[1] = halbtc8822b2ant_read_indirect_reg(btc, 0xac);
		u32tmp[2] = halbtc8822b2ant_read_indirect_reg(btc, 0xb0);
		u32tmp[3] = halbtc8822b2ant_read_indirect_reg(btc, 0xb4);
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

	u32tmp[0] = halbtc8822b2ant_read_indirect_reg(btc, 0x38);
	u32tmp[1] = halbtc8822b2ant_read_indirect_reg(btc, 0x54);
	u8tmp[0] = btc->btc_read_1byte(btc, 0x73);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s",
		   "LTE Coex/Path Owner", ((lte_coex_on) ? "On" : "Off"),
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
			   (int)((u32tmp[1] & BIT(1)) >> 1),
			   (int)(u32tmp[1] & BIT(0)));
		CL_PRINTF(cli_buf);
	}
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %s (BB:%s)/ %s (BB:%s)/ %s (gnt_err = %d)",
		   "GNT_WL_Ctrl/GNT_BT_Ctrl/Dbg",
		   ((u32tmp[0] & BIT(12)) ? "SW" : "HW"),
		   ((u32tmp[0] & BIT(8)) ? "SW" : "HW"),
		   ((u32tmp[0] & BIT(14)) ? "SW" : "HW"),
		   ((u32tmp[0] & BIT(10)) ? "SW" : "HW"),
		   ((u8tmp[0] & BIT(3)) ? "On" : "Off"),
		   coex_sta->gnt_error_cnt);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "GNT_WL/GNT_BT", (int)((u32tmp[1] & BIT(2)) >> 2),
		   (int)((u32tmp[1] & BIT(3)) >> 3));
	CL_PRINTF(cli_buf);

	u32tmp[0] = btc->btc_read_4byte(btc, 0xcb0);
	u32tmp[1] = btc->btc_read_4byte(btc, 0xcb4);
	u8tmp[0] = btc->btc_read_1byte(btc, 0xcba);
	u8tmp[1] = btc->btc_read_1byte(btc, 0xcbd);
	u8tmp[2] = btc->btc_read_1byte(btc, 0xc58);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%04x/ 0x%04x/ 0x%02x/ 0x%02x  0x%02x %s",
		   "0xcb0/0xcb4/0xcba/0xcbd/0xc58", u32tmp[0], u32tmp[1],
		   u8tmp[0], u8tmp[1], u8tmp[2],
		   ((u8tmp[1] & 0x1) == 0x1 ? "(BT_WL5G)" : "(WL2G)"));
	CL_PRINTF(cli_buf);

	u32tmp[0] = btc->btc_read_4byte(btc, 0x430);
	u32tmp[1] = btc->btc_read_4byte(btc, 0x434);
	u16tmp[0] = btc->btc_read_2byte(btc, 0x42a);
	u8tmp[0] = btc->btc_read_1byte(btc, 0x426);
	u8tmp[1] = btc->btc_read_1byte(btc, 0x45e);
	u8tmp[2] = btc->btc_read_1byte(btc, 0x455);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x/ 0x%x/ 0x%x",
		   "430/434/42a/426/45e[3]/455",
		   u32tmp[0], u32tmp[1], u16tmp[0], u8tmp[0],
		   (int)((u8tmp[1] & BIT(3)) >> 3), u8tmp[2]);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btc->btc_read_4byte(btc, 0x4c);
	u8tmp[2] = btc->btc_read_1byte(btc, 0x64);
	u8tmp[0] = btc->btc_read_1byte(btc, 0x4c6);
	u8tmp[1] = btc->btc_read_1byte(btc, 0x40);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x",
		   "4c[24:23]/64[0]/4c6[4]/40[5]",
		   (int)(u32tmp[0] & (BIT(24) | BIT(23))) >> 23, u8tmp[2] & 0x1,
		   (int)((u8tmp[0] & BIT(4)) >> 4),
		   (int)((u8tmp[1] & BIT(5)) >> 5));
	CL_PRINTF(cli_buf);

	u32tmp[0] = btc->btc_read_4byte(btc, 0x550);
	u8tmp[0] = btc->btc_read_1byte(btc, 0x522);
	u8tmp[1] = btc->btc_read_1byte(btc, 0x953);
	u8tmp[2] = btc->btc_read_1byte(btc, 0xc50);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ %s/ 0x%x",
		   "0x550/0x522/4-RxAGC/0xc50", u32tmp[0], u8tmp[0],
		   (u8tmp[1] & 0x2) ? "On" : "Off", u8tmp[2]);
	CL_PRINTF(cli_buf);

	fa_ofdm = btc->btc_phydm_query_PHY_counter(btc, PHYDM_INFO_FA_OFDM);
	fa_cck = btc->btc_phydm_query_PHY_counter(btc, PHYDM_INFO_FA_CCK);
	cca_ofdm = btc->btc_phydm_query_PHY_counter(btc, PHYDM_INFO_CCA_OFDM);
	cca_cck = btc->btc_phydm_query_PHY_counter(btc, PHYDM_INFO_CCA_CCK);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x",
		   "CCK-CCA/CCK-FA/OFDM-CCA/OFDM-FA", cca_cck, fa_cck, cca_ofdm,
		   fa_ofdm);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d",
		   "CRC_OK CCK/11g/11n/11ac",
		   coex_sta->crc_ok_cck, coex_sta->crc_ok_11g,
		   coex_sta->crc_ok_11n, coex_sta->crc_ok_11n_vht);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d",
		   "CRC_Err CCK/11g/11n/11ac",
		   coex_sta->crc_err_cck, coex_sta->crc_err_11g,
		   coex_sta->crc_err_11n, coex_sta->crc_err_11n_vht);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %d/ %d/ %s-%d/ %d (Tx macid: %d)",
		   "Rate RxD/RxRTS/TxD/TxRetry_ratio",
		   coex_sta->wl_rx_rate, coex_sta->wl_rts_rx_rate,
		   (coex_sta->wl_tx_rate & 0x80 ? "SGI" : "LGI"),
		   coex_sta->wl_tx_rate & 0x7f,
		   coex_sta->wl_tx_retry_ratio,
		   coex_sta->wl_tx_macid);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s/ %s/ %s/ %d",
		   "HiPr/ Locking/ warn/ Locked/ Noisy",
		   (coex_sta->wifi_high_pri_task1 ? "Yes" : "No"),
		   (coex_sta->cck_lock ? "Yes" : "No"),
		   (coex_sta->cck_lock_warn ? "Yes" : "No"),
		   (coex_sta->cck_lock_ever ? "Yes" : "No"),
		   coex_sta->wl_noisy_level);
	CL_PRINTF(cli_buf);

	u8tmp[0] = btc->btc_read_1byte(btc, 0xf8e);
	u8tmp[1] = btc->btc_read_1byte(btc, 0xf8f);
	u8tmp[2] = btc->btc_read_1byte(btc, 0xd14);
	u8tmp[3] = btc->btc_read_1byte(btc, 0xd54);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d",
		   "EVM_A/ EVM_B/ SNR_A/ SNR_B",
		   (u8tmp[0] > 127 ? u8tmp[0] - 256 : u8tmp[0]),
		   (u8tmp[1] > 127 ? u8tmp[1] - 256 : u8tmp[1]),
		   (u8tmp[2] > 127 ? u8tmp[2] - 256 : u8tmp[2]),
		   (u8tmp[3] > 127 ? u8tmp[3] - 256 : u8tmp[3]));
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "0x770(Hi-pri rx/tx)", coex_sta->high_priority_rx,
		   coex_sta->high_priority_tx);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d %s",
		   "0x774(Lo-pri rx/tx)", coex_sta->low_priority_rx,
		   coex_sta->low_priority_tx,
		   (bt_link_info->slave_role ? "(Slave!!)" : " "));
	CL_PRINTF(cli_buf);

	btc->btc_disp_dbg_msg(btc, BTC_DBG_DISP_COEX_STATISTICS);
}

void ex_halbtc8822b2ant_ips_notify(struct btc_coexist *btc, u8 type)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;

	if (btc->manual_control || btc->stop_coex_dm)
		return;

	if (type == BTC_IPS_ENTER) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], IPS ENTER notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_ips = TRUE;
		coex_sta->under_lps = FALSE;

		halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_ACTIVE |
					   BT_8822B_2ANT_SCBD_ONOFF |
					   BT_8822B_2ANT_SCBD_SCAN |
					   BT_8822B_2ANT_SCBD_UNDERTEST |
					   BT_8822B_2ANT_SCBD_RXGAIN,
					   FALSE);

		halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO,
					     FC_EXCU,
					     BT_8822B_2ANT_PHASE_WOFF);

		halbtc8822b2ant_action_coex_all_off(btc);
	} else if (type == BTC_IPS_LEAVE) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], IPS LEAVE notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_ips = FALSE;

		halbtc8822b2ant_init_hw_config(btc, FALSE);
		halbtc8822b2ant_init_coex_dm(btc);
		halbtc8822b2ant_query_bt_info(btc);
	}
}

void ex_halbtc8822b2ant_lps_notify(struct btc_coexist *btc, u8 type)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;

	if (btc->manual_control || btc->stop_coex_dm)
		return;

	if (type == BTC_LPS_ENABLE) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], LPS ENABLE notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_lps = TRUE;
		coex_sta->under_ips = FALSE;

		if (coex_sta->force_lps_ctrl) { /* LPS No-32K */
			/* Write WL "Active" in Score-board for PS-TDMA */
			halbtc8822b2ant_write_scbd(btc,
						   BT_8822B_2ANT_SCBD_ACTIVE,
						   TRUE);

		} else {
			/* Write WL "Non-Active" in Score-board for Native-PS */
			halbtc8822b2ant_write_scbd(btc,
						   BT_8822B_2ANT_SCBD_ACTIVE,
						   FALSE);
			halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_LPS);
		}

	} else if (type == BTC_LPS_DISABLE) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], LPS DISABLE notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_lps = FALSE;

		halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_ACTIVE,
					   TRUE);

		if (!coex_sta->force_lps_ctrl) {
			halbtc8822b2ant_query_bt_info(btc);
			halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_LPS);
		}
	}
}

void ex_halbtc8822b2ant_scan_notify(struct btc_coexist *btc, u8 type)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	boolean wifi_connected = FALSE;

	if (btc->manual_control || btc->stop_coex_dm)
		return;

	btc->btc_get(btc, BTC_GET_BL_WIFI_CONNECTED, &wifi_connected);

	if (wifi_connected)
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], ********** WL connected before SCAN\n");
	else
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], **********  WL is not connected before SCAN\n");

	BTC_TRACE(trace_buf);

	/* this can't be removed for RF off_on event, or BT would dis-connect */
	if (type != BTC_SCAN_FINISH) {
		halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_ACTIVE |
					   BT_8822B_2ANT_SCBD_SCAN |
					   BT_8822B_2ANT_SCBD_ONOFF,
					   TRUE);
	}

	if (type == BTC_SCAN_START_5G) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], SCAN START notify (5G)\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, FC_EXCU,
					     BT_8822B_2ANT_PHASE_5G);

		halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_5GSCANSTART);
	} else if ((type == BTC_SCAN_START_2G) || (type == BTC_SCAN_START)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], SCAN START notify (2G)\n");
		BTC_TRACE(trace_buf);

		if (!wifi_connected)
			coex_sta->wifi_high_pri_task2 = TRUE;

		halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, FC_EXCU,
					     BT_8822B_2ANT_PHASE_2G);

		halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_2GSCANSTART);
	} else if (type == BTC_SCAN_FINISH) {
		btc->btc_get(btc, BTC_GET_U1_AP_NUM, &coex_sta->scan_ap_num);

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], SCAN FINISH notify  (Scan-AP = %d)\n",
			    coex_sta->scan_ap_num);
		BTC_TRACE(trace_buf);

		coex_sta->wifi_high_pri_task2 = FALSE;

		halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_SCANFINISH);
	}
}

void ex_halbtc8822b2ant_switchband_notify(struct btc_coexist *btc, u8 type)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;

	if (btc->manual_control || btc->stop_coex_dm)
		return;
	coex_sta->switch_band_notify_to = type;

	if (type == BTC_SWITCH_TO_5G) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], switchband_notify ---  switch to 5G\n");
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_5GSWITCHBAND);

	} else if (type == BTC_SWITCH_TO_24G_NOFORSCAN) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], ********** switchband_notify --- BTC_SWITCH_TO_2G (no for scan)\n");
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_2GSWITCHBAND);

	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], switchband_notify ---  switch to 2G\n");
		BTC_TRACE(trace_buf);

		ex_halbtc8822b2ant_scan_notify(btc, BTC_SCAN_START_2G);
	}
	coex_sta->switch_band_notify_to = BTC_NOT_SWITCH;
}

void ex_halbtc8822b2ant_connect_notify(struct btc_coexist *btc, u8 type)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;

	if (btc->manual_control || btc->stop_coex_dm)
		return;

	halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_ACTIVE |
				   BT_8822B_2ANT_SCBD_SCAN |
				   BT_8822B_2ANT_SCBD_ONOFF,
				   TRUE);

	if (type == BTC_ASSOCIATE_5G_START) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], CONNECT START notify (5G)\n");
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, FC_EXCU,
					     BT_8822B_2ANT_PHASE_5G);
		halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_5GCONSTART);
	} else if  (type == BTC_ASSOCIATE_5G_FINISH) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], CONNECT FINISH notify (5G)\n");
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, FC_EXCU,
					     BT_8822B_2ANT_PHASE_5G);
		halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_5GCONFINISH);
	} else if (type == BTC_ASSOCIATE_START) {
		coex_sta->wifi_high_pri_task1 = TRUE;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], CONNECT START notify (2G)\n");
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, FC_EXCU,
					     BT_8822B_2ANT_PHASE_2G);
		halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_2GCONSTART);

		/* To keep TDMA case during connect process,
		 * to avoid changed by Btinfo and runcoexmechanism
		 */
		coex_sta->freeze_coexrun_by_btinfo = TRUE;
		coex_dm->arp_cnt = 0;
	} else if (type == BTC_ASSOCIATE_FINISH) {
		coex_sta->wifi_high_pri_task1 = FALSE;
		coex_sta->freeze_coexrun_by_btinfo = FALSE;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], CONNECT FINISH notify (2G)\n");
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_2GCONFINISH);
	}
}

void ex_halbtc8822b2ant_media_status_notify(struct btc_coexist *btc, u8 type)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	boolean wifi_under_b_mode = FALSE;

	if (btc->manual_control || btc->stop_coex_dm)
		return;

	if (type == BTC_MEDIA_CONNECT_5G) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], MEDIA connect notify --- 5G\n");
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_ACTIVE,
					   TRUE);

		halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, FC_EXCU,
					     BT_8822B_2ANT_PHASE_5G);

		halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_5GMEDIA);
	} else if (type == BTC_MEDIA_CONNECT) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], MEDIA connect notify --- 2G\n");
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_ACTIVE,
					   TRUE);
		halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, FC_EXCU,
					     BT_8822B_2ANT_PHASE_2G);

		btc->btc_get(btc, BTC_GET_BL_WIFI_UNDER_B_MODE,
			     &wifi_under_b_mode);

		/* Set CCK Tx/Rx high Pri except 11b mode */
		if (wifi_under_b_mode)
			btc->btc_write_1byte_bitmask(btc, 0x6cf, BIT(4), 0x0);
		else
			btc->btc_write_1byte_bitmask(btc, 0x6cf, BIT(4), 0x1);

		halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_2GMEDIA);
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], MEDIA disconnect notify\n");
		BTC_TRACE(trace_buf);

		btc->btc_write_1byte_bitmask(btc, 0x6cf, BIT(4), 0x0);

		coex_sta->cck_lock_ever = FALSE;
		coex_sta->cck_lock_warn = FALSE;
		coex_sta->cck_lock = FALSE;

		halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_MEDIADISCON);
	}

	halbtc8822b2ant_update_wifi_ch_info(btc, type);
}

void ex_halbtc8822b2ant_specific_packet_notify(struct btc_coexist *btc, u8 type)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct coex_dm_8822b_2ant *coex_dm = &btc->coex_dm_8822b_2ant;
	boolean under_4way = FALSE;

	if (btc->manual_control || btc->stop_coex_dm)
		return;

	if (type & BTC_5G_BAND) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], WiFi is under 5G!!!\n");
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_5GSPECIALPKT);
		return;
	}

	btc->btc_get(btc, BTC_GET_BL_WIFI_4_WAY_PROGRESS, &under_4way);

	if (under_4way) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], specific Packet ---- under_4way!!\n");
		BTC_TRACE(trace_buf);

		coex_sta->wifi_high_pri_task1 = TRUE;
		coex_sta->specific_pkt_period_cnt = 2;

	} else if (type == BTC_PACKET_ARP) {
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

		coex_sta->wifi_high_pri_task1 = TRUE;
		coex_sta->specific_pkt_period_cnt = 2;
	}

	if (coex_sta->wifi_high_pri_task1) {
		halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_SCAN, TRUE);
		halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_2GSPECIALPKT);
	}
}

void ex_halbtc8822b2ant_bt_info_notify(struct btc_coexist *btc, u8 *tmp_buf,
				       u8 length)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct btc_bt_link_info *bt_link_info = &btc->bt_link_info;
	u8 i, rsp_source = 0, type;
	boolean wifi_connected = FALSE, wifi_busy = FALSE;
	static boolean is_scoreboard_scan;
	const u16 type_is_scan = BT_8822B_2ANT_SCBD_SCAN;

	rsp_source = tmp_buf[0] & 0xf;
	if (rsp_source >= BT_8822B_2ANT_INFO_SRC_MAX)
		rsp_source = BT_8822B_2ANT_INFO_SRC_WIFI_FW;
	coex_sta->bt_info_c2h_cnt[rsp_source]++;

	if (rsp_source == BT_8822B_2ANT_INFO_SRC_BT_SCBD) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT Scoreboard change notify by WL FW c2h, 0xaa = 0x%02x, 0xab = 0x%02x\n",
			    tmp_buf[1], tmp_buf[2]);
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_monitor_bt_enable(btc);
		halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_BTINFO);
		return;
	};

	if (rsp_source == BT_8822B_2ANT_INFO_SRC_BT_RSP ||
	    rsp_source == BT_8822B_2ANT_INFO_SRC_BT_ACT) {
		if (coex_sta->bt_disabled) {
			coex_sta->bt_disabled = FALSE;
			coex_sta->is_bt_reenable = TRUE;
			coex_sta->cnt_bt_reenable = 15;

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], BT enable detected by bt_info\n");
			BTC_TRACE(trace_buf);
		}
	}


	if (rsp_source == BT_8822B_2ANT_INFO_SRC_WIFI_FW) {
#if 0
		halbtc8822b2ant_update_bt_link_info(btc);
		halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_BTINFO);
#endif
		return;
	}

	if (length != 7) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Bt_info length = %d invalid!!\n",
			    length);
		BTC_TRACE(trace_buf);
		return;
	}

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], Bt_info[%d], len=%d, data=[%02x %02x %02x %02x %02x %02x]\n",
		    tmp_buf[0], length, tmp_buf[1], tmp_buf[2], tmp_buf[3],
		    tmp_buf[4], tmp_buf[5], tmp_buf[6]);
	BTC_TRACE(trace_buf);

	for (i = 0; i < 7; i++)
		coex_sta->bt_info_c2h[rsp_source][i] = tmp_buf[i];

	if (coex_sta->bt_info_c2h[rsp_source][1] == coex_sta->bt_info_lb2 &&
	    coex_sta->bt_info_c2h[rsp_source][2] == coex_sta->bt_info_lb3 &&
	    coex_sta->bt_info_c2h[rsp_source][3] == coex_sta->bt_info_hb0 &&
	    coex_sta->bt_info_c2h[rsp_source][4] == coex_sta->bt_info_hb1 &&
	    coex_sta->bt_info_c2h[rsp_source][5] == coex_sta->bt_info_hb2 &&
	    coex_sta->bt_info_c2h[rsp_source][6] == coex_sta->bt_info_hb3) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Return because Btinfo duplicate!!\n");
		BTC_TRACE(trace_buf);
		return;
	}

	coex_sta->bt_info_lb2 = coex_sta->bt_info_c2h[rsp_source][1];
	coex_sta->bt_info_lb3 = coex_sta->bt_info_c2h[rsp_source][2];
	coex_sta->bt_info_hb0 = coex_sta->bt_info_c2h[rsp_source][3];
	coex_sta->bt_info_hb1 = coex_sta->bt_info_c2h[rsp_source][4];
	coex_sta->bt_info_hb2 = coex_sta->bt_info_c2h[rsp_source][5];
	coex_sta->bt_info_hb3 = coex_sta->bt_info_c2h[rsp_source][6];

	/* if 0xff, it means BT is under WHCK test */
	coex_sta->bt_whck_test =
		((coex_sta->bt_info_lb2 == 0xff) ? TRUE : FALSE);

	coex_sta->bt_create_connection =
		((coex_sta->bt_info_lb3 & BIT(7)) ? TRUE : FALSE);

	/* unit: %, value-100 to translate to unit: dBm */
	coex_sta->bt_rssi = coex_sta->bt_info_hb0 * 2 + 10;

	coex_sta->c2h_bt_remote_name_req =
		((coex_sta->bt_info_lb3 & BIT(5)) ? TRUE : FALSE);

	coex_sta->is_A2DP_3M =
		((coex_sta->bt_info_lb3 & BIT(4)) ? TRUE : FALSE);

	coex_sta->acl_busy =
		((coex_sta->bt_info_lb2 & BIT(3)) ? TRUE : FALSE);

	coex_sta->voice_over_HOGP =
		((coex_sta->bt_info_hb1 & BIT(4)) ? TRUE : FALSE);

	coex_sta->c2h_bt_inquiry_page =
		((coex_sta->bt_info_lb2 & BIT(2)) ? TRUE : FALSE);

	if ((coex_sta->bt_info_lb2 & 0x49) == 0x49)
		coex_sta->a2dp_bit_pool = (coex_sta->bt_info_hb3 & 0x7f);
	else
		coex_sta->a2dp_bit_pool = 0;

	coex_sta->is_bt_a2dp_sink =
		(coex_sta->bt_info_hb3 & BIT(7)) ? TRUE : FALSE;

	coex_sta->bt_retry_cnt = coex_sta->bt_info_lb3 & 0xf;

	bt_link_info->slave_role  = coex_sta->bt_info_hb2 & 0x8;

	coex_sta->bt_a2dp_active = ((coex_sta->bt_info_hb2 & BIT(2)) == BIT(2));

	coex_sta->forbidden_slot = coex_sta->bt_info_hb2 & 0x7;

	coex_sta->hid_busy_num = (coex_sta->bt_info_hb2 & 0x30) >> 4;

	coex_sta->hid_pair_cnt = (coex_sta->bt_info_hb2 & 0xc0) >> 6;

	if (coex_sta->hid_pair_cnt > 0 && coex_sta->hid_busy_num >= 2)
		coex_sta->bt_418_hid_exist = TRUE;
	else if (coex_sta->hid_pair_cnt == 0)
		coex_sta->bt_418_hid_exist = FALSE;

	coex_sta->is_bt_opp_exist =
		(coex_sta->bt_info_hb2 & BIT(0)) ? TRUE : FALSE;

	if (coex_sta->bt_retry_cnt >= 1)
		coex_sta->pop_event_cnt++;

	if (coex_sta->c2h_bt_remote_name_req)
		coex_sta->cnt_remote_name_req++;

	if (coex_sta->bt_info_hb1 & BIT(1))
		coex_sta->cnt_reinit++;

	if ((coex_sta->bt_info_hb1 & BIT(2)) ||
	    (coex_sta->bt_create_connection &&
	     coex_sta->wl_pnp_wakeup_downcnt > 0)) {
		coex_sta->cnt_setup_link++;
		coex_sta->is_setup_link = TRUE;

		if (coex_sta->is_bt_reenable)
			coex_sta->bt_relink_downcount = 6;
		else
			coex_sta->bt_relink_downcount = 2;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Re-Link start in BT info!!\n");
		BTC_TRACE(trace_buf);
	}

	if (coex_sta->bt_info_hb1 & BIT(3))
		coex_sta->cnt_ign_wlan_act++;

	if (coex_sta->bt_info_hb1 & BIT(6))
		coex_sta->cnt_role_switch++;

	if (coex_sta->bt_info_hb1 & BIT(7))
		coex_sta->is_bt_multi_link = TRUE;
	else
		coex_sta->is_bt_multi_link = FALSE;

	if (coex_sta->bt_info_hb1 & BIT(0))
		coex_sta->is_hid_rcu = TRUE;
	else
		coex_sta->is_hid_rcu = FALSE;

	if (coex_sta->bt_info_hb1 & BIT(5))
		coex_sta->is_ble_scan_en = TRUE;
	else
		coex_sta->is_ble_scan_en = FALSE;

	if (coex_sta->bt_create_connection) {
		coex_sta->cnt_page++;

		btc->btc_get(btc, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

		if (coex_sta->is_wifi_linkscan_process ||
		    coex_sta->wifi_high_pri_task1 ||
		    coex_sta->wifi_high_pri_task2 || wifi_busy) {
			is_scoreboard_scan = TRUE;
			halbtc8822b2ant_write_scbd(btc, type_is_scan, TRUE);
		} else {
			halbtc8822b2ant_write_scbd(btc, type_is_scan, FALSE);
		}
	} else {
		if (is_scoreboard_scan) {
			halbtc8822b2ant_write_scbd(btc, type_is_scan, FALSE);
			is_scoreboard_scan = FALSE;
		}
	}

	/* Here we need to resend some wifi info to BT */
	/* because bt is reset and loss of the info. */
	btc->btc_get(btc, BTC_GET_BL_WIFI_CONNECTED, &wifi_connected);
	/*  Re-Init */
	if ((coex_sta->bt_info_hb1 & BIT(1))) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT ext info bit1 check, send wifi BW&Chnl to BT!!\n");
		BTC_TRACE(trace_buf);
		if (wifi_connected)
			type = BTC_MEDIA_CONNECT;
		else
			type = BTC_MEDIA_DISCONNECT;
		halbtc8822b2ant_update_wifi_ch_info(btc, type);
	}

	/* If Ignore_WLanAct && not SetUp_Link */
	if ((coex_sta->bt_info_hb1 & BIT(3)) &&
	    (!(coex_sta->bt_info_hb1 & BIT(2)))) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT ext info bit3 check, set BT NOT to ignore Wlan active!!\n");
		BTC_TRACE(trace_buf);
		halbtc8822b2ant_ignore_wlan_act(btc, FC_EXCU, FALSE);
	}

	halbtc8822b2ant_update_bt_link_info(btc);
	halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_BTINFO);
}

void ex_halbtc8822b2ant_wl_fwdbginfo_notify(struct btc_coexist *btc,
					    u8 *tmp_buf, u8 length)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	u8 i = 0;
	static u8 tmp_buf_pre[10];

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], WiFi Fw Dbg info = %d %d %d %d %d %d (len = %d)\n",
		    tmp_buf[0], tmp_buf[1], tmp_buf[2], tmp_buf[3], tmp_buf[4],
		    tmp_buf[5], length);
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

	coex_sta->cnt_wl_fw_notify++;
	halbtc8822b2ant_ccklock_action(btc);
}

void ex_halbtc8822b2ant_rx_rate_change_notify(struct btc_coexist *btc,
					      BOOLEAN is_data_frame,
					      u8 btc_rate_id)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;

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

	halbtc8822b2ant_ccklock_detect(btc);
}

void ex_halbtc8822b2ant_tx_rate_change_notify(struct btc_coexist *btc,
					      u8 tx_rate, u8 tx_retry_ratio,
					      u8 macid)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], tx_rate_change_notify Tx_Rate = %d, Tx_Retry_Ratio = %d, macid =%d\n",
		    tx_rate, tx_retry_ratio, macid);
	BTC_TRACE(trace_buf);

	coex_sta->wl_tx_rate = tx_rate;
	coex_sta->wl_tx_retry_ratio = tx_retry_ratio;
	coex_sta->wl_tx_macid = macid;
}

void ex_halbtc8822b2ant_rf_status_notify(struct btc_coexist *btc, u8 type)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], RF Status notify\n");
	BTC_TRACE(trace_buf);

	if (type == BTC_RF_ON) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RF is turned ON!!\n");
		BTC_TRACE(trace_buf);

		btc->stop_coex_dm = FALSE;
		btc->wl_rf_state_off = FALSE;
	} else if (type == BTC_RF_OFF) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RF is turned OFF!!\n");
		BTC_TRACE(trace_buf);

		halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_ACTIVE |
					   BT_8822B_2ANT_SCBD_ONOFF |
					   BT_8822B_2ANT_SCBD_SCAN |
					   BT_8822B_2ANT_SCBD_UNDERTEST |
					   BT_8822B_2ANT_SCBD_RXGAIN,
					   FALSE);

		halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, FC_EXCU,
					     BT_8822B_2ANT_PHASE_WOFF);

		halbtc8822b2ant_action_coex_all_off(btc);

		btc->stop_coex_dm = TRUE;
		btc->wl_rf_state_off = TRUE;

		/* must place in the last step */
		halbtc8822b2ant_update_wifi_ch_info(btc, BTC_MEDIA_DISCONNECT);
	}
}

void ex_halbtc8822b2ant_halt_notify(struct btc_coexist *btc)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Halt notify\n");
	BTC_TRACE(trace_buf);

	halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_ACTIVE |
				   BT_8822B_2ANT_SCBD_ONOFF |
				   BT_8822B_2ANT_SCBD_SCAN |
				   BT_8822B_2ANT_SCBD_UNDERTEST |
				   BT_8822B_2ANT_SCBD_RXGAIN, FALSE);

	halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, FC_EXCU,
				     BT_8822B_2ANT_PHASE_WOFF);

	halbtc8822b2ant_action_coex_all_off(btc);

	btc->stop_coex_dm = TRUE;

	/* must place in the last step */
	halbtc8822b2ant_update_wifi_ch_info(btc, BTC_MEDIA_DISCONNECT);
}

void ex_halbtc8822b2ant_pnp_notify(struct btc_coexist *btc, u8 pnp_state)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	struct wifi_link_info_8822b_2ant *wifi_link_info_ext =
					 &btc->wifi_link_info_8822b_2ant;
	static u8 pre_pnp_state;
	u8 phase;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Pnp notify\n");
	BTC_TRACE(trace_buf);

	if (pnp_state == BTC_WIFI_PNP_SLEEP ||
	    pnp_state == BTC_WIFI_PNP_SLEEP_KEEP_ANT) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Pnp notify to SLEEP\n");
		BTC_TRACE(trace_buf);

		/* Sinda 20150819, workaround for driver skip leave IPS/LPS to
		 * speed up sleep time.
		 * Driver do not leave IPS/LPS when driver is going to sleep,
		 * so BTCoexistence think wifi is still under IPS/LPS.
		 * BT should clear UnderIPS/UnderLPS state to avoid mismatch
		 * state after wakeup.
		 */
		coex_sta->under_ips = FALSE;
		coex_sta->under_lps = FALSE;

		halbtc8822b2ant_write_scbd(btc, BT_8822B_2ANT_SCBD_ACTIVE |
					   BT_8822B_2ANT_SCBD_ONOFF |
					   BT_8822B_2ANT_SCBD_SCAN |
					   BT_8822B_2ANT_SCBD_UNDERTEST |
					   BT_8822B_2ANT_SCBD_RXGAIN,
					   FALSE);

		if (pnp_state == BTC_WIFI_PNP_SLEEP_KEEP_ANT) {
			if (wifi_link_info_ext->is_all_under_5g)
				phase = BT_8822B_2ANT_PHASE_5G;
			else
				phase = BT_8822B_2ANT_PHASE_2G;
		} else {
			phase = BT_8822B_2ANT_PHASE_WOFF;
		}
		halbtc8822b2ant_set_ant_path(btc, BTC_ANT_PATH_AUTO, FC_EXCU,
					     phase);

		btc->stop_coex_dm = TRUE;
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Pnp notify to WAKE UP\n");
		BTC_TRACE(trace_buf);

		coex_sta->wl_pnp_wakeup_downcnt = 3;

		/*WoWLAN*/
		if (pre_pnp_state == BTC_WIFI_PNP_SLEEP_KEEP_ANT ||
		    pnp_state == BTC_WIFI_PNP_WOWLAN) {
			coex_sta->run_time_state = TRUE;
			btc->stop_coex_dm = FALSE;
			halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_PNP);
		}
	}

	pre_pnp_state = pnp_state;
}

void ex_halbtc8822b2ant_periodical(struct btc_coexist *btc)
{
	struct coex_sta_8822b_2ant *coex_sta = &btc->coex_sta_8822b_2ant;
	boolean bt_relink_finish = FALSE, is_defreeze = FALSE;
	static u8 freeze_cnt;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], ************* Periodical *************\n");
	BTC_TRACE(trace_buf);

	if (!btc->auto_report)
		halbtc8822b2ant_query_bt_info(btc);

	halbtc8822b2ant_monitor_bt_ctr(btc);
	halbtc8822b2ant_monitor_wifi_ctr(btc);
	halbtc8822b2ant_update_wifi_link_info(btc,
					      BT_8822B_2ANT_RSN_PERIODICAL);
	halbtc8822b2ant_monitor_bt_enable(btc);

	if (coex_sta->bt_relink_downcount != 0) {
		coex_sta->bt_relink_downcount--;
		if (coex_sta->bt_relink_downcount == 0) {
			coex_sta->is_setup_link = FALSE;
			bt_relink_finish = TRUE;
		}
	}

	if (coex_sta->freeze_coexrun_by_btinfo) {
		freeze_cnt++;

		if (freeze_cnt >= 5) {
			freeze_cnt = 0;
			coex_sta->freeze_coexrun_by_btinfo = FALSE;
			is_defreeze = TRUE;
		}
	} else {
		freeze_cnt = 0;
	}

	/* for 4-way, DHCP, EAPOL packet */
	if (coex_sta->specific_pkt_period_cnt > 0) {
		coex_sta->specific_pkt_period_cnt--;

		if (!coex_sta->freeze_coexrun_by_btinfo &&
		    coex_sta->specific_pkt_period_cnt == 0)
			coex_sta->wifi_high_pri_task1 = FALSE;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], ***************** Hi-Pri Task = %s*****************\n",
			    (coex_sta->wifi_high_pri_task1 ? "Yes" : "No"));
		BTC_TRACE(trace_buf);
	}

	if (coex_sta->wl_pnp_wakeup_downcnt > 0) {
		coex_sta->wl_pnp_wakeup_downcnt--;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], wl_pnp_wakeup_downcnt = %d!!\n",
			    coex_sta->wl_pnp_wakeup_downcnt);
		BTC_TRACE(trace_buf);
	}

	if (coex_sta->cnt_bt_reenable > 0) {
		coex_sta->cnt_bt_reenable--;
		if (coex_sta->cnt_bt_reenable == 0) {
			coex_sta->is_bt_reenable = FALSE;
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], BT renable 30s finish!!\n");
			BTC_TRACE(trace_buf);
		}
	}

	if (halbtc8822b2ant_moniter_wifibt_status(btc) || bt_relink_finish ||
	    coex_sta->is_set_ps_state_fail || is_defreeze)
		halbtc8822b2ant_run_coex(btc, BT_8822B_2ANT_RSN_PERIODICAL);
}

#endif

#endif /*  #if (RTL8822B_SUPPORT == 1) */
