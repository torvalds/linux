/* ************************************************************
 * Description:
 *
 * This file is for RTL8723B Co-exist mechanism
 *
 * History
 * 2012/11/15 Cosa first check in.
 *
 * ************************************************************ */

/* ************************************************************
 * include files
 * ************************************************************ */
#include "Mp_Precomp.h"

#if (BT_SUPPORT == 1 && COEX_SUPPORT == 1)

#if (RTL8723B_SUPPORT == 1)
/* ************************************************************
 * Global variables, these are static variables
 * ************************************************************ */
static u8	 *trace_buf = &gl_btc_trace_buf[0];
static struct  coex_dm_8723b_1ant		glcoex_dm_8723b_1ant;
static struct  coex_dm_8723b_1ant	*coex_dm = &glcoex_dm_8723b_1ant;
static struct  coex_sta_8723b_1ant		glcoex_sta_8723b_1ant;
static struct  coex_sta_8723b_1ant	*coex_sta = &glcoex_sta_8723b_1ant;
static struct  psdscan_sta_8723b_1ant	gl_psd_scan_8723b_1ant;
static struct  psdscan_sta_8723b_1ant *psd_scan = &gl_psd_scan_8723b_1ant;


const char *const glbt_info_src_8723b_1ant[] = {
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

u32	glcoex_ver_date_8723b_1ant = 20151127;
u32	glcoex_ver_8723b_1ant = 0x65;

/* ************************************************************
 * local function proto type if needed
 * ************************************************************
 * ************************************************************
 * local function start with halbtc8723b1ant_
 * ************************************************************ */

void halbtc8723b1ant_update_ra_mask(IN struct btc_coexist *btcoexist,
				    IN boolean force_exec, IN u32 dis_rate_mask)
{
	coex_dm->cur_ra_mask = dis_rate_mask;

	if (force_exec || (coex_dm->pre_ra_mask != coex_dm->cur_ra_mask))
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_UPDATE_RAMASK,
				   &coex_dm->cur_ra_mask);
	coex_dm->pre_ra_mask = coex_dm->cur_ra_mask;
}

void halbtc8723b1ant_auto_rate_fallback_retry(IN struct btc_coexist *btcoexist,
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

void halbtc8723b1ant_retry_limit(IN struct btc_coexist *btcoexist,
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

void halbtc8723b1ant_ampdu_max_time(IN struct btc_coexist *btcoexist,
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

void halbtc8723b1ant_limited_tx(IN struct btc_coexist *btcoexist,
		IN boolean force_exec, IN u8 ra_mask_type, IN u8 arfr_type,
				IN u8 retry_limit_type, IN u8 ampdu_time_type)
{
	switch (ra_mask_type) {
	case 0:	/* normal mode */
		halbtc8723b1ant_update_ra_mask(btcoexist, force_exec,
					       0x0);
		break;
	case 1:	/* disable cck 1/2 */
		halbtc8723b1ant_update_ra_mask(btcoexist, force_exec,
					       0x00000003);
		break;
	case 2:	/* disable cck 1/2/5.5, ofdm 6/9/12/18/24, mcs 0/1/2/3/4 */
		halbtc8723b1ant_update_ra_mask(btcoexist, force_exec,
					       0x0001f1f7);
		break;
	default:
		break;
	}

	halbtc8723b1ant_auto_rate_fallback_retry(btcoexist, force_exec,
			arfr_type);
	halbtc8723b1ant_retry_limit(btcoexist, force_exec, retry_limit_type);
	halbtc8723b1ant_ampdu_max_time(btcoexist, force_exec, ampdu_time_type);
}

void halbtc8723b1ant_limited_rx(IN struct btc_coexist *btcoexist,
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

void halbtc8723b1ant_query_bt_info(IN struct btc_coexist *btcoexist)
{
	u8			h2c_parameter[1] = {0};

	coex_sta->c2h_bt_info_req_sent = true;

	h2c_parameter[0] |= BIT(0);	/* trigger */

	btcoexist->btc_fill_h2c(btcoexist, 0x61, 1, h2c_parameter);
}

void halbtc8723b1ant_monitor_bt_ctr(IN struct btc_coexist *btcoexist)
{
	u32			reg_hp_txrx, reg_lp_txrx, u32tmp;
	u32			reg_hp_tx = 0, reg_hp_rx = 0, reg_lp_tx = 0, reg_lp_rx = 0;
	static u32		num_of_bt_counter_chk = 0;
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	/* to avoid 0x76e[3] = 1 (WLAN_Act control by PTA) during IPS */
	/* if (! (btcoexist->btc_read_1byte(btcoexist, 0x76e) & 0x8) ) */

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

	if ((coex_sta->high_priority_tx  + coex_sta->high_priority_rx < 50) &&
	    (bt_link_info->hid_exist == true))
		bt_link_info->hid_exist  = false;

	if ((coex_sta->low_priority_tx > 1050)  &&
	    (!coex_sta->c2h_bt_inquiry_page))
		coex_sta->pop_event_cnt++;

	if ((coex_sta->low_priority_rx >= 950)  && (!coex_sta->under_ips)
	    && (coex_sta->low_priority_rx >=
		coex_sta->low_priority_tx)  &&
	    (!coex_sta->c2h_bt_inquiry_page))
		bt_link_info->slave_role = true;
	else
		bt_link_info->slave_role = false;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], Hi-Pri Rx/Tx: %d/%d, Lo-Pri Rx/Tx: %d/%d\n",
		    reg_hp_rx, reg_hp_tx, reg_lp_rx, reg_lp_tx);
	BTC_TRACE(trace_buf);

	/* reset counter */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0xc);

	/* This part is for wifi FW and driver to update BT's status as disabled. */
	/* The flow is as the following */
	/* 1. disable BT */
	/* 2. if all BT Tx/Rx counter=0, after 6 sec we query bt info */
	/* 3. Because BT will not rsp from mailbox, so wifi fw will know BT is disabled */
	/* 4. FW will rsp c2h for BT that driver will know BT is disabled. */
	if ((reg_hp_tx == 0) && (reg_hp_rx == 0) && (reg_lp_tx == 0) &&
	    (reg_lp_rx == 0)) {
		num_of_bt_counter_chk++;
		if (num_of_bt_counter_chk >= 3) {
			halbtc8723b1ant_query_bt_info(btcoexist);
			num_of_bt_counter_chk = 0;
		}
	} 

}


void halbtc8723b1ant_monitor_wifi_ctr(IN struct btc_coexist *btcoexist)
{
	s32	wifi_rssi = 0;
	boolean wifi_busy = false, wifi_under_b_mode = false;
	static u8 cck_lock_counter = 0;
	u32	total_cnt;

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
		coex_sta->crc_ok_cck	= btcoexist->btc_read_4byte(btcoexist,
					  0xf88);
		coex_sta->crc_ok_11g	= btcoexist->btc_read_2byte(btcoexist,
					  0xf94);
		coex_sta->crc_ok_11n	= btcoexist->btc_read_2byte(btcoexist,
					  0xf90);
		coex_sta->crc_ok_11n_agg = btcoexist->btc_read_2byte(btcoexist,
					   0xfb8);

		coex_sta->crc_err_cck	 = btcoexist->btc_read_4byte(btcoexist,
					   0xf84);
		coex_sta->crc_err_11g	 = btcoexist->btc_read_2byte(btcoexist,
					   0xf96);
		coex_sta->crc_err_11n	 = btcoexist->btc_read_2byte(btcoexist,
					   0xf92);
		coex_sta->crc_err_11n_agg = btcoexist->btc_read_2byte(btcoexist,
					    0xfba);
	}


	/* reset counter */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0xf16, 0x1, 0x1);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0xf16, 0x1, 0x0);

	if ((wifi_busy) && (wifi_rssi >= 30) && (!wifi_under_b_mode)) {
		total_cnt = coex_sta->crc_ok_cck + coex_sta->crc_ok_11g +
			    coex_sta->crc_ok_11n +
			    coex_sta->crc_ok_11n_agg;

		if ((coex_dm->bt_status == BT_8723B_1ANT_BT_STATUS_ACL_BUSY) ||
		    (coex_dm->bt_status ==
		     BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY) ||
		    (coex_dm->bt_status ==
		     BT_8723B_1ANT_BT_STATUS_SCO_BUSY)) {
			if (coex_sta->crc_ok_cck > (total_cnt -
						    coex_sta->crc_ok_cck))			{
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

	coex_sta->pre_ccklock =  coex_sta->cck_lock;


}

boolean halbtc8723b1ant_is_wifi_status_changed(IN struct btc_coexist *btcoexist)
{
	static boolean	pre_wifi_busy = false, pre_under_4way = false,
			pre_bt_hs_on = false;
	boolean wifi_busy = false, under_4way = false, bt_hs_on = false;
	boolean wifi_connected = false;

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

void halbtc8723b1ant_update_bt_link_info(IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info	*bt_link_info = &btcoexist->bt_link_info;
	boolean				bt_hs_on = false;

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

void halbtc8723b1ant_set_bt_auto_report(IN struct btc_coexist *btcoexist,
					IN boolean enable_auto_report)
{
	u8			h2c_parameter[1] = {0};

	h2c_parameter[0] = 0;

	if (enable_auto_report)
		h2c_parameter[0] |= BIT(0);

	btcoexist->btc_fill_h2c(btcoexist, 0x68, 1, h2c_parameter);
}

void halbtc8723b1ant_bt_auto_report(IN struct btc_coexist *btcoexist,
		    IN boolean force_exec, IN boolean enable_auto_report)
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

void halbtc8723b1ant_set_sw_penalty_tx_rate_adaptive(IN struct btc_coexist
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

void halbtc8723b1ant_low_penalty_ra(IN struct btc_coexist *btcoexist,
			    IN boolean force_exec, IN boolean low_penalty_ra)
{
	coex_dm->cur_low_penalty_ra = low_penalty_ra;

	if (!force_exec) {
		if (coex_dm->pre_low_penalty_ra == coex_dm->cur_low_penalty_ra)
			return;
	}
	halbtc8723b1ant_set_sw_penalty_tx_rate_adaptive(btcoexist,
			coex_dm->cur_low_penalty_ra);

	coex_dm->pre_low_penalty_ra = coex_dm->cur_low_penalty_ra;
}

void halbtc8723b1ant_set_coex_table(IN struct btc_coexist *btcoexist,
	    IN u32 val0x6c0, IN u32 val0x6c4, IN u32 val0x6c8, IN u8 val0x6cc)
{
	btcoexist->btc_write_4byte(btcoexist, 0x6c0, val0x6c0);

	btcoexist->btc_write_4byte(btcoexist, 0x6c4, val0x6c4);

	btcoexist->btc_write_4byte(btcoexist, 0x6c8, val0x6c8);

	btcoexist->btc_write_1byte(btcoexist, 0x6cc, val0x6cc);
}

void halbtc8723b1ant_coex_table(IN struct btc_coexist *btcoexist,
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
	halbtc8723b1ant_set_coex_table(btcoexist, val0x6c0, val0x6c4, val0x6c8,
				       val0x6cc);

	coex_dm->pre_val0x6c0 = coex_dm->cur_val0x6c0;
	coex_dm->pre_val0x6c4 = coex_dm->cur_val0x6c4;
	coex_dm->pre_val0x6c8 = coex_dm->cur_val0x6c8;
	coex_dm->pre_val0x6cc = coex_dm->cur_val0x6cc;
}

void halbtc8723b1ant_coex_table_with_type(IN struct btc_coexist *btcoexist,
		IN boolean force_exec, IN u8 type)
{
	struct  btc_board_info	*board_info = &btcoexist->board_info;

#if BT_8723B_1ANT_ANTDET_ENABLE
#if BT_8723B_1ANT_ANTDET_COEXMECHANISMSWITCH_ENABLE
	if (board_info->btdm_ant_num_by_ant_det == 2) {
		if (type == 3)
			type = 14;
		else if (type == 4)
			type  = 13;
		else if (type == 5)
			type = 8;
	}
#endif
#endif

	coex_sta->coex_table_type = type;

	switch (type) {
	case 0:
		halbtc8723b1ant_coex_table(btcoexist, force_exec,
				   0x55555555, 0x55555555, 0xffffff, 0x3);
		break;
	case 1:
		halbtc8723b1ant_coex_table(btcoexist, force_exec,
				   0x55555555, 0x5a5a5a5a, 0xffffff, 0x3);
		break;
	case 2:
		halbtc8723b1ant_coex_table(btcoexist, force_exec,
				   0x5a5a5a5a, 0x5a5a5a5a, 0xffffff, 0x3);
		break;
	case 3:
		halbtc8723b1ant_coex_table(btcoexist, force_exec,
				   0x55555555, 0x5a5a5a5a, 0xffffff, 0x3);
		break;
	case 4:
		if ((coex_sta->cck_ever_lock)  &&
		    (coex_sta->scan_ap_num <= 5))
			halbtc8723b1ant_coex_table(btcoexist,
					   force_exec, 0x55555555, 0xaaaa5a5a,
						   0xffffff, 0x3);
		else
			halbtc8723b1ant_coex_table(btcoexist,
					   force_exec, 0x55555555, 0x5a5a5a5a,
						   0xffffff, 0x3);
		break;
	case 5:
		if ((coex_sta->cck_ever_lock)  &&
		    (coex_sta->scan_ap_num <= 5))
			halbtc8723b1ant_coex_table(btcoexist,
					   force_exec, 0x5a5a5a5a, 0x5aaa5a5a,
						   0xffffff, 0x3);
		else
			halbtc8723b1ant_coex_table(btcoexist,
					   force_exec, 0x5a5a5a5a, 0x5aaa5a5a,
						   0xffffff, 0x3);
		break;
	case 6:
		halbtc8723b1ant_coex_table(btcoexist, force_exec,
				   0x55555555, 0xaaaaaaaa, 0xffffff, 0x3);
		break;
	case 7:
		halbtc8723b1ant_coex_table(btcoexist, force_exec,
				   0xaaaaaaaa, 0xaaaaaaaa, 0xffffff, 0x3);
		break;
	case 8:
		halbtc8723b1ant_coex_table(btcoexist, force_exec,
				   0x55dd55dd, 0x5ada5ada, 0xffffff, 0x3);
		break;
	case 9:
		halbtc8723b1ant_coex_table(btcoexist, force_exec,
				   0x55dd55dd, 0x5ada5ada, 0xffffff, 0x3);
		break;
	case 10:
		halbtc8723b1ant_coex_table(btcoexist, force_exec,
				   0x55dd55dd, 0x5ada5ada, 0xffffff, 0x3);
		break;
	case 11:
		halbtc8723b1ant_coex_table(btcoexist, force_exec,
				   0x55dd55dd, 0x5ada5ada, 0xffffff, 0x3);
		break;
	case 12:
		halbtc8723b1ant_coex_table(btcoexist, force_exec,
				   0x55dd55dd, 0x5ada5ada, 0xffffff, 0x3);
		break;
	case 13:
		halbtc8723b1ant_coex_table(btcoexist, force_exec,
				   0x5fff5fff, 0xaaaaaaaa, 0xffffff, 0x3);
		break;
	case 14:
		halbtc8723b1ant_coex_table(btcoexist, force_exec,
				   0x5fff5fff, 0x5ada5ada, 0xffffff, 0x3);
		break;
	case 15:
		halbtc8723b1ant_coex_table(btcoexist, force_exec,
				   0x55dd55dd, 0xaaaaaaaa, 0xffffff, 0x3);
		break;
	default:
		break;
	}
}

void halbtc8723b1ant_set_fw_ignore_wlan_act(IN struct btc_coexist *btcoexist,
		IN boolean enable)
{
	u8			h2c_parameter[1] = {0};

	if (enable) {
		h2c_parameter[0] |= BIT(0);		/* function enable */
	}

	btcoexist->btc_fill_h2c(btcoexist, 0x63, 1, h2c_parameter);
}

void halbtc8723b1ant_ignore_wlan_act(IN struct btc_coexist *btcoexist,
				     IN boolean force_exec, IN boolean enable)
{
	coex_dm->cur_ignore_wlan_act = enable;

	if (!force_exec) {
		if (coex_dm->pre_ignore_wlan_act ==
		    coex_dm->cur_ignore_wlan_act)
			return;
	}
	halbtc8723b1ant_set_fw_ignore_wlan_act(btcoexist, enable);

	coex_dm->pre_ignore_wlan_act = coex_dm->cur_ignore_wlan_act;
}

void halbtc8723b1ant_set_lps_rpwm(IN struct btc_coexist *btcoexist,
				  IN u8 lps_val, IN u8 rpwm_val)
{
	u8	lps = lps_val;
	u8	rpwm = rpwm_val;

	btcoexist->btc_set(btcoexist, BTC_SET_U1_LPS_VAL, &lps);
	btcoexist->btc_set(btcoexist, BTC_SET_U1_RPWM_VAL, &rpwm);
}

void halbtc8723b1ant_lps_rpwm(IN struct btc_coexist *btcoexist,
		      IN boolean force_exec, IN u8 lps_val, IN u8 rpwm_val)
{
	coex_dm->cur_lps = lps_val;
	coex_dm->cur_rpwm = rpwm_val;

	if (!force_exec) {
		if ((coex_dm->pre_lps == coex_dm->cur_lps) &&
		    (coex_dm->pre_rpwm == coex_dm->cur_rpwm))
			return;
	}
	halbtc8723b1ant_set_lps_rpwm(btcoexist, lps_val, rpwm_val);

	coex_dm->pre_lps = coex_dm->cur_lps;
	coex_dm->pre_rpwm = coex_dm->cur_rpwm;
}

void halbtc8723b1ant_sw_mechanism(IN struct btc_coexist *btcoexist,
				  IN boolean low_penalty_ra)
{
	halbtc8723b1ant_low_penalty_ra(btcoexist, NORMAL_EXEC, low_penalty_ra);
}

void halbtc8723b1ant_set_ant_path(IN struct btc_coexist *btcoexist,
	  IN u8 ant_pos_type, IN boolean force_exec, IN boolean init_hwcfg,
				  IN boolean wifi_off)
{
	struct  btc_board_info *board_info = &btcoexist->board_info;
	PADAPTER		padapter = btcoexist->Adapter;
	u32			fw_ver = 0, u32tmp = 0, cnt_bt_cal_chk = 0;
	boolean			pg_ext_switch = false;
	boolean			use_ext_switch = false;
	boolean			is_in_mp_mode = false;
	u8			h2c_parameter[2] = {0}, u8tmp = 0;
	u32         u32tmp_1[4];

	coex_dm->cur_ant_pos_type = ant_pos_type;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_EXT_SWITCH, &pg_ext_switch);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER,
			   &fw_ver);	/* [31:16]=fw ver, [15:0]=fw sub ver */

	if ((fw_ver > 0 && fw_ver < 0xc0000) || pg_ext_switch)
		use_ext_switch = true;

#if BT_8723B_1ANT_ANTDET_ENABLE
#if BT_8723B_1ANT_ANTDET_COEXMECHANISMSWITCH_ENABLE
	if (ant_pos_type == BTC_ANT_PATH_PTA) {
		if ((board_info->btdm_ant_det_finish) &&
		    (board_info->btdm_ant_num_by_ant_det == 2)) {
			if (board_info->btdm_ant_pos ==
			    BTC_ANTENNA_AT_MAIN_PORT)
				ant_pos_type = BTC_ANT_PATH_WIFI;
			else
				ant_pos_type = BTC_ANT_PATH_BT;
		}
	}
#endif
#endif

	if (init_hwcfg) {
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff,
					  0x780); /* WiFi TRx Mask on */
		/* remove due to interrupt is disabled that polling c2h will fail and delay 100ms. */
		/* btcoexist->btc_set_bt_reg(btcoexist, BTC_BT_REG_RF, 0x3c, 0x15); */ /*BT TRx Mask on */

		if (fw_ver >= 0x180000) {
			/* Use H2C to set GNT_BT to HIGH */
			h2c_parameter[0] = 1;
			btcoexist->btc_fill_h2c(btcoexist, 0x6E, 1,
						h2c_parameter);

			cnt_bt_cal_chk = 0;
			while (1) {
				if (padapter->bFWReady == false) {
					BTC_SPRINTF(trace_buf , BT_TMP_BUF_SIZE,
						("halbtc8723b1ant_set_ant_path(): we don't need to wait for H2C command completion because of Fw download fail!!!\n"));
					BTC_TRACE(trace_buf);
					break;
				}

				if (btcoexist->btc_read_1byte(btcoexist,
							      0x765) == 0x18)
					break;

				cnt_bt_cal_chk++;
				if (cnt_bt_cal_chk > 20)
					break;
			}
		} else {
			/* set grant_bt to high */
			btcoexist->btc_write_1byte(btcoexist, 0x765, 0x18);
		}
		/* set wlan_act control by PTA */
		btcoexist->btc_write_1byte(btcoexist, 0x76e, 0x4);

		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x20,
			   0x0); /* BT select s0/s1 is controlled by BT */

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

			cnt_bt_cal_chk = 0;
			while (1) {
				if (padapter->bFWReady == false) {
					BTC_SPRINTF(trace_buf , BT_TMP_BUF_SIZE,
						("halbtc8723b1ant_set_ant_path(): we don't need to wait for H2C command completion because of Fw download fail!!!\n"));
					BTC_TRACE(trace_buf);
					break;
				}

				if (btcoexist->btc_read_1byte(btcoexist,
							      0x765) == 0x18)
					break;

				cnt_bt_cal_chk++;
				if (cnt_bt_cal_chk > 20)
					break;
			}
		} else {
			/* set grant_bt to high */
			btcoexist->btc_write_1byte(btcoexist, 0x765, 0x18);
		}
		/* set wlan_act to always low */
		btcoexist->btc_write_1byte(btcoexist, 0x76e, 0x4);

		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_IS_IN_MP_MODE,
				   &is_in_mp_mode);
		if (!is_in_mp_mode)
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67,
				0x20, 0x0); /* BT select s0/s1 is controlled by BT */
		else
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67,
				0x20, 0x1); /* BT select s0/s1 is controlled by WiFi */

		/* 0x4c[24:23]=00, Set Antenna control by BT_RFE_CTRL	BT Vendor 0xac=0xf002 */
		u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x4c);
		u32tmp &= ~BIT(23);
		u32tmp &= ~BIT(24);
		btcoexist->btc_write_4byte(btcoexist, 0x4c, u32tmp);
	} else {
		/* Use H2C to set GNT_BT to LOW */
		if (fw_ver >= 0x180000) {
			if (btcoexist->btc_read_1byte(btcoexist, 0x765) != 0) {
				h2c_parameter[0] = 0;
				btcoexist->btc_fill_h2c(btcoexist, 0x6E, 1,
							h2c_parameter);

				cnt_bt_cal_chk = 0;
				while (1) {
					if (padapter->bFWReady == false) {
						BTC_SPRINTF(trace_buf , BT_TMP_BUF_SIZE,
							("halbtc8723b1ant_set_ant_path(): we don't need to wait for H2C command completion because of Fw download fail!!!\n"));
						BTC_TRACE(trace_buf);	
						break;
					}

					if (btcoexist->btc_read_1byte(btcoexist,
							      0x765) == 0x0)
						break;

					cnt_bt_cal_chk++;
					if (cnt_bt_cal_chk > 20)
						break;
				}
			}
		} else {
			/* BT calibration check */
			while (cnt_bt_cal_chk <= 20) {
				u8tmp = btcoexist->btc_read_1byte(btcoexist,
								  0x49d);
				cnt_bt_cal_chk++;
				if (u8tmp & BIT(0)) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], ########### BT is calibrating (wait cnt=%d) ###########\n",
						    cnt_bt_cal_chk);
					BTC_TRACE(trace_buf);
					delay_ms(50);
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], ********** BT is NOT calibrating (wait cnt=%d)**********\n",
						    cnt_bt_cal_chk);
					BTC_TRACE(trace_buf);
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

		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x20,
			   0x1); /* BT select s0/s1 is controlled by WiFi */
	}

	if (use_ext_switch) {
		if (init_hwcfg) {
			/* 0x4c[23]=0, 0x4c[24]=1  Antenna control by WL/BT */
			u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x4c);
			u32tmp &= ~BIT(23);
			u32tmp |= BIT(24);
			btcoexist->btc_write_4byte(btcoexist, 0x4c, u32tmp);


			u32tmp_1[0] = btcoexist->btc_read_4byte(btcoexist,
								0x948);
			if ((u32tmp_1[0] == 0x40) || (u32tmp_1[0] == 0x240))
				btcoexist->btc_write_4byte(btcoexist, 0x948,
							   u32tmp_1[0]);
			else
				btcoexist->btc_write_4byte(btcoexist, 0x948,
							   0x0);


			if (board_info->btdm_ant_pos ==
			    BTC_ANTENNA_AT_MAIN_PORT) {
				/* tell firmware "no antenna inverse" */
				h2c_parameter[0] = 0;
				h2c_parameter[1] = 1;  /* ext switch type */
				btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
							h2c_parameter);
			} else {
				/* tell firmware "antenna inverse" */
				h2c_parameter[0] = 1;
				h2c_parameter[1] = 1;  /* ext switch type */
				btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
							h2c_parameter);
			}
		}

		if (force_exec ||
		    (coex_dm->cur_ant_pos_type !=
		     coex_dm->pre_ant_pos_type)) {
			/* ext switch setting */
			switch (ant_pos_type) {
			case BTC_ANT_PATH_WIFI:
				if (board_info->btdm_ant_pos ==
				    BTC_ANTENNA_AT_MAIN_PORT)
					btcoexist->btc_write_1byte_bitmask(
						btcoexist, 0x92c, 0x3,
						0x1);
				else
					btcoexist->btc_write_1byte_bitmask(
						btcoexist, 0x92c, 0x3,
						0x2);
				break;
			case BTC_ANT_PATH_BT:
				if (board_info->btdm_ant_pos ==
				    BTC_ANTENNA_AT_MAIN_PORT)
					btcoexist->btc_write_1byte_bitmask(
						btcoexist, 0x92c, 0x3,
						0x2);
				else
					btcoexist->btc_write_1byte_bitmask(
						btcoexist, 0x92c, 0x3,
						0x1);
				break;
			default:
			case BTC_ANT_PATH_PTA:
				if (board_info->btdm_ant_pos ==
				    BTC_ANTENNA_AT_MAIN_PORT)
					btcoexist->btc_write_1byte_bitmask(
						btcoexist, 0x92c, 0x3,
						0x1);
				else
					btcoexist->btc_write_1byte_bitmask(
						btcoexist, 0x92c, 0x3,
						0x2);
				break;
			}
		}
	} else {
		if (init_hwcfg) {
			/* 0x4c[23]=1, 0x4c[24]=0  Antenna control by 0x64 */
			u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x4c);
			u32tmp |= BIT(23);
			u32tmp &= ~BIT(24);
			btcoexist->btc_write_4byte(btcoexist, 0x4c, u32tmp);

			/* Fix Ext switch Main->S1, Aux->S0 */
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0x64, 0x1,
							   0x0);

			if (board_info->btdm_ant_pos ==
			    BTC_ANTENNA_AT_MAIN_PORT) {

				/* tell firmware "no antenna inverse" */
				h2c_parameter[0] = 0;
				h2c_parameter[1] =
					0;  /* internal switch type */
				btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
							h2c_parameter);
			} else {

				/* tell firmware "antenna inverse" */
				h2c_parameter[0] = 1;
				h2c_parameter[1] =
					0;  /* internal switch type */
				btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
							h2c_parameter);
			}
		}

		if (force_exec ||
		    (coex_dm->cur_ant_pos_type !=
		     coex_dm->pre_ant_pos_type)) {
			/* internal switch setting */
			switch (ant_pos_type) {
			case BTC_ANT_PATH_WIFI:
				if (board_info->btdm_ant_pos ==
				    BTC_ANTENNA_AT_MAIN_PORT) {
					u32tmp_1[0] = btcoexist->btc_read_4byte(
							      btcoexist, 0x948);
					if ((u32tmp_1[0] == 0x40) ||
					    (u32tmp_1[0] == 0x240))
						btcoexist->btc_write_4byte(
							btcoexist, 0x948,
							u32tmp_1[0]);
					else
						btcoexist->btc_write_4byte(
							btcoexist, 0x948, 0x0);
				} else {
					u32tmp_1[0] = btcoexist->btc_read_4byte(
							      btcoexist, 0x948);
					if ((u32tmp_1[0] == 0x40) ||
					    (u32tmp_1[0] == 0x240))
						btcoexist->btc_write_4byte(
							btcoexist, 0x948,
							u32tmp_1[0]);
					else
						btcoexist->btc_write_4byte(
							btcoexist, 0x948,
							0x280);
				}
				break;
			case BTC_ANT_PATH_BT:
				if (board_info->btdm_ant_pos ==
				    BTC_ANTENNA_AT_MAIN_PORT) {
					u32tmp_1[0] = btcoexist->btc_read_4byte(
							      btcoexist, 0x948);
					if ((u32tmp_1[0] == 0x40) ||
					    (u32tmp_1[0] == 0x240))
						btcoexist->btc_write_4byte(
							btcoexist, 0x948,
							u32tmp_1[0]);
					else
						btcoexist->btc_write_4byte(
							btcoexist, 0x948,
							0x280);
				} else {
					u32tmp_1[0] = btcoexist->btc_read_4byte(
							      btcoexist, 0x948);
					if ((u32tmp_1[0] == 0x40) ||
					    (u32tmp_1[0] == 0x240))
						btcoexist->btc_write_4byte(
							btcoexist, 0x948,
							u32tmp_1[0]);
					else
						btcoexist->btc_write_4byte(
							btcoexist, 0x948, 0x0);
				}
				break;
			default:
			case BTC_ANT_PATH_PTA:
				if (board_info->btdm_ant_pos ==
				    BTC_ANTENNA_AT_MAIN_PORT)
					btcoexist->btc_write_4byte(
						btcoexist, 0x948,
						0x200);
				else
					btcoexist->btc_write_4byte(
						btcoexist, 0x948, 0x80);
				break;
			}
		}
	}

	coex_dm->pre_ant_pos_type = coex_dm->cur_ant_pos_type;
}

void halbtc8723b1ant_set_fw_pstdma(IN struct btc_coexist *btcoexist,
	   IN u8 byte1, IN u8 byte2, IN u8 byte3, IN u8 byte4, IN u8 byte5)
{
	u8			h2c_parameter[5] = {0};
	u8			real_byte1 = byte1, real_byte5 = byte5;
	boolean			ap_enable = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE,
			   &ap_enable);

	if (ap_enable) {
		if (byte1 & BIT(4) && !(byte1 & BIT(5))) {
			real_byte1 &= ~BIT(4);
			real_byte1 |= BIT(5);

			real_byte5 |= BIT(5);
			real_byte5 &= ~BIT(6);
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

	btcoexist->btc_fill_h2c(btcoexist, 0x60, 5, h2c_parameter);
}


void halbtc8723b1ant_ps_tdma(IN struct btc_coexist *btcoexist,
		     IN boolean force_exec, IN boolean turn_on, IN u8 type)
{
	struct  btc_board_info	*board_info = &btcoexist->board_info;
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean			wifi_busy = false;
	u8			rssi_adjust_val = 0;
	u8			ps_tdma_byte4_val = 0x50, ps_tdma_byte0_val = 0x51,
				ps_tdma_byte3_val =  0x10;
	s8			wifi_duration_adjust = 0x0;
	static boolean	 pre_wifi_busy = false;

	coex_dm->cur_ps_tdma_on = turn_on;
	coex_dm->cur_ps_tdma = type;

#if BT_8723B_1ANT_ANTDET_ENABLE
#if BT_8723B_1ANT_ANTDET_COEXMECHANISMSWITCH_ENABLE
	if (board_info->btdm_ant_num_by_ant_det == 2) {
		if (turn_on)
			type = type +
			       100; /* for WiFi RSSI low or BT RSSI low */
		else
			type = 1; /* always translate to TDMA(off,1) for TDMA-off case */
	}

#endif
#endif

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
	    (type == 101)
	    || (type == 102) || (type == 109) || (type == 101)) {
		if (!coex_sta->force_lps_on) { /* Native power save TDMA, only for A2DP-only case 1/2/9/11 while wifi noisy threshold > 30 */
			ps_tdma_byte0_val = 0x61;  /* no null-pkt */
			ps_tdma_byte3_val = 0x11; /* no tx-pause at BT-slot */
			ps_tdma_byte4_val =
				0x10; /* 0x778 = d/1 toggle, no dynamic slot */
		} else {
			ps_tdma_byte0_val = 0x51;  /* null-pkt */
			ps_tdma_byte3_val = 0x10; /* tx-pause at BT-slot */
			ps_tdma_byte4_val =
				0x50; /* 0x778 = d/1 toggle, dynamic slot */
		}
	} else if ((type == 3) || (type == 13) || (type == 14) ||
		   (type == 103) || (type == 113) || (type == 114)) {
		ps_tdma_byte0_val = 0x51;  /* null-pkt */
		ps_tdma_byte3_val = 0x10; /* tx-pause at BT-slot */
		ps_tdma_byte4_val =
			0x10; /* 0x778 = d/1 toggle, no dynamic slot */
#if 0
		if (!wifi_busy)
			ps_tdma_byte4_val = ps_tdma_byte4_val |
				0x1;  /* 0x778 = 0x1 at wifi slot (no blocking BT Low-Pri pkts) */
#endif
	} else { /* native power save case */
		ps_tdma_byte0_val = 0x61;  /* no null-pkt */
		ps_tdma_byte3_val = 0x11; /* no tx-pause at BT-slot */
		ps_tdma_byte4_val =
			0x11; /* 0x778 = d/1 toggle, no dynamic slot */
		/* psTdmaByte4Va is not defne for 0x778 = d/1, 1/1 case */
	}

	/* if (bt_link_info->slave_role == true) */
	if ((bt_link_info->slave_role == true)	&& (bt_link_info->a2dp_exist))
		ps_tdma_byte4_val = ps_tdma_byte4_val |
			0x1;  /* 0x778 = 0x1 at wifi slot (no blocking BT Low-Pri pkts) */

	if (type > 100) {
		ps_tdma_byte0_val = ps_tdma_byte0_val |
				    0x82; /* set antenna control by SW	 */
		ps_tdma_byte3_val = ps_tdma_byte3_val |
			0x60;  /* set antenna no toggle, control by antenna diversity */
	}


	if (turn_on) {
		switch (type) {
		default:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0x51,
				      0x1a, 0x1a, 0x0, ps_tdma_byte4_val);
			break;
		case 1:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
						      ps_tdma_byte0_val, 0x3a +
					      wifi_duration_adjust, 0x03,
				      ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 2:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
						      ps_tdma_byte0_val, 0x2d +
					      wifi_duration_adjust, 0x03,
				      ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 3:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x30, 0x03,
				      ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 4:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0x93,
						      0x15, 0x3, 0x14, 0x0);
			break;
		case 5:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x1f, 0x3,
						      ps_tdma_byte3_val, 0x11);
			break;
		case 6:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x20, 0x3,
						      ps_tdma_byte3_val, 0x11);
			break;
		case 7:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0x13,
						      0xc, 0x5, 0x0, 0x0);
			break;
		case 8:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0x93,
						      0x25, 0x3, 0x10, 0x0);
			break;
		case 9:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x21, 0x3,
				      ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 10:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0x13,
						      0xa, 0xa, 0x0, 0x40);
			break;
		case 11:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x21, 0x03,
				      ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 12:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0x51,
						      0x0a, 0x0a, 0x0, 0x50);
			break;
		case 13:
			if (coex_sta->scan_ap_num <= 3)
				halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x40, 0x3,
							      ps_tdma_byte3_val,
						      ps_tdma_byte4_val);
			else
				halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x21, 0x3,
							      ps_tdma_byte3_val,
						      ps_tdma_byte4_val);
			break;
		case 14:
			if (coex_sta->scan_ap_num <= 3)
				halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x30, 0x3, 0x10, 0x50);
			else
				halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x21, 0x3,
							      ps_tdma_byte3_val,
						      ps_tdma_byte4_val);
			break;
		case 15:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0x13,
						      0xa, 0x3, 0x8, 0x0);
			break;
		case 16:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0x93,
						      0x15, 0x3, 0x10, 0x0);
			break;
		case 18:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0x93,
						      0x25, 0x3, 0x10, 0x0);
			break;
		case 20:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x3f, 0x03,
						      ps_tdma_byte3_val, 0x10);
			break;
		case 21:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0x61,
						      0x25, 0x03, 0x11, 0x11);
			break;
		case 22:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x25, 0x03,
						      ps_tdma_byte3_val, 0x10);
			break;
		case 23:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x25, 0x3, 0x31, 0x18);
			break;
		case 24:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x15, 0x3, 0x31, 0x18);
			break;
		case 25:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0xe3,
						      0xa, 0x3, 0x31, 0x18);
			break;
		case 26:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0xe3,
						      0xa, 0x3, 0x31, 0x18);
			break;
		case 27:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x25, 0x3, 0x31, 0x98);
			break;
		case 28:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0x69,
						      0x25, 0x3, 0x31, 0x0);
			break;
		case 29:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0xab,
						      0x1a, 0x1a, 0x1, 0x10);
			break;
		case 30:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0x51,
						      0x30, 0x3, 0x10, 0x10);
			break;
		case 31:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0xd3,
						      0x1a, 0x1a, 0, 0x58);
			break;
		case 32:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x35, 0x3,
				      ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 33:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x35, 0x3,
						      ps_tdma_byte3_val, 0x10);
			break;
		case 34:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0x53,
						      0x1a, 0x1a, 0x0, 0x10);
			break;
		case 35:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0x63,
						      0x1a, 0x1a, 0x0, 0x10);
			break;
		case 36:
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0xd3,
						      0x12, 0x3, 0x14, 0x50);
			break;
		case 40: /* SoftAP only with no sta associated,BT disable ,TDMA mode for power saving */
			/* here softap mode screen off will cost 70-80mA for phone */
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0x23,
						      0x18, 0x00, 0x10, 0x24);
			break;

		/* for 1-Ant translate to 2-Ant	 */
		case 101:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
						      ps_tdma_byte0_val, 0x3a +
					      wifi_duration_adjust, 0x03,
				      ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 102:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
						      ps_tdma_byte0_val, 0x2d +
					      wifi_duration_adjust, 0x03,
				      ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 103:
			/* halbtc8723b1ant_set_fw_pstdma(btcoexist, 0x51, 0x1d, 0x1d, 0x0, ps_tdma_byte4_val); */
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x3a, 0x03,
				      ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 105:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x15, 0x3,
						      ps_tdma_byte3_val, 0x11);
			break;
		case 106:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x20, 0x3,
						      ps_tdma_byte3_val, 0x11);
			break;
		case 109:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x21, 0x3,
				      ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 111:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x21, 0x03,
				      ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 113:
			/* halbtc8723b1ant_set_fw_pstdma(btcoexist, 0x51, 0x12, 0x12, 0x0, ps_tdma_byte4_val); */
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x21, 0x3,
				      ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 114:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x21, 0x3,
				      ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 120:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x3f, 0x03,
						      ps_tdma_byte3_val, 0x10);
			break;
		case 122:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x25, 0x03,
						      ps_tdma_byte3_val, 0x10);
			break;
		case 132:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x25, 0x03,
				      ps_tdma_byte3_val, ps_tdma_byte4_val);
			break;
		case 133:
			halbtc8723b1ant_set_fw_pstdma(btcoexist,
					      ps_tdma_byte0_val, 0x25, 0x03,
						      ps_tdma_byte3_val, 0x11);
			break;

		}
	} else {

		/* disable PS tdma */
		switch (type) {
		case 8: /* PTA Control */
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0x8,
						      0x0, 0x0, 0x0, 0x0);
			break;
		case 0:
		default:  /* Software control, Antenna at BT side */
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0x0,
						      0x0, 0x0, 0x0, 0x0);
			break;
		case 1: /* 2-Ant, 0x778=3, antenna control by antenna diversity */
			halbtc8723b1ant_set_fw_pstdma(btcoexist, 0x0,
						      0x0, 0x0, 0x48, 0x0);
			break;
		}
	}
	rssi_adjust_val = 0;
	btcoexist->btc_set(btcoexist,
		BTC_SET_U1_RSSI_ADJ_VAL_FOR_1ANT_COEX_TYPE, &rssi_adjust_val);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"############# [BTCoex], 0x948=0x%x, 0x765=0x%x, 0x67=0x%x\n",
		    btcoexist->btc_read_4byte(btcoexist, 0x948),
		    btcoexist->btc_read_1byte(btcoexist, 0x765),
		    btcoexist->btc_read_1byte(btcoexist, 0x67));
	BTC_TRACE(trace_buf);

	/* update pre state */
	coex_dm->pre_ps_tdma_on = coex_dm->cur_ps_tdma_on;
	coex_dm->pre_ps_tdma = coex_dm->cur_ps_tdma;
}

void halbtc8723b1ant_tdma_duration_adjust_for_acl(IN struct btc_coexist
		*btcoexist, IN u8 wifi_status)
{
	static s32		up, dn, m, n, wait_count;
	s32			result;   /* 0: no change, +1: increase WiFi duration, -1: decrease WiFi duration */
	u8			retry_count = 0, bt_info_ext;
	boolean			wifi_busy = false;

	if (BT_8723B_1ANT_WIFI_STATUS_CONNECTED_BUSY == wifi_status)
		wifi_busy = true;
	else
		wifi_busy = false;

	if ((BT_8723B_1ANT_WIFI_STATUS_NON_CONNECTED_ASSO_AUTH_SCAN ==
	     wifi_status) ||
	    (BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SCAN == wifi_status) ||
	    (BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SPECIFIC_PKT ==
	     wifi_status)) {
		if (coex_dm->cur_ps_tdma != 1 &&
		    coex_dm->cur_ps_tdma != 2 &&
		    coex_dm->cur_ps_tdma != 3 &&
		    coex_dm->cur_ps_tdma != 9) {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						9);
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

		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 2);
		coex_dm->ps_tdma_du_adj_type = 2;
		/* ============ */
		up = 0;
		dn = 0;
		m = 1;
		n = 3;
		result = 0;
		wait_count = 0;
	} else {
		/* accquire the BT TRx retry count from BT_Info byte2 */
		retry_count = coex_sta->bt_retry_cnt;
		bt_info_ext = coex_sta->bt_info_ext;

		if ((coex_sta->low_priority_tx) > 1050 ||
		    (coex_sta->low_priority_rx) > 1250)
			retry_count++;

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

		if (result == -1) {
			/*		if( (BT_INFO_8723B_1ANT_A2DP_BASIC_RATE(bt_info_ext)) &&
						((coex_dm->cur_ps_tdma == 1) ||(coex_dm->cur_ps_tdma == 2)) )
					{
						halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 9);
						coex_dm->ps_tdma_du_adj_type = 9;
					}
					else */ if (coex_dm->cur_ps_tdma == 1) {
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
			/*			if( (BT_INFO_8723B_1ANT_A2DP_BASIC_RATE(bt_info_ext)) &&
							((coex_dm->cur_ps_tdma == 1) ||(coex_dm->cur_ps_tdma == 2)) )
						{
							halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 9);
							coex_dm->ps_tdma_du_adj_type = 9;
						}
						else */ if (coex_dm->cur_ps_tdma == 11) {
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
		} else { /* no change */
			/* Bryant Modify
			if(wifi_busy != pre_wifi_busy)
			{
				pre_wifi_busy = wifi_busy;
				halbtc8723b1ant_ps_tdma(btcoexist, FORCE_EXEC, true, coex_dm->cur_ps_tdma);
			}
			*/

		}

		if (coex_dm->cur_ps_tdma != 1 &&
		    coex_dm->cur_ps_tdma != 2 &&
		    coex_dm->cur_ps_tdma != 9 &&
		    coex_dm->cur_ps_tdma != 11) {
			/* recover to previous adjust type */
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						coex_dm->ps_tdma_du_adj_type);
		}
	}
}

void halbtc8723b1ant_ps_tdma_check_for_power_save_state(
	IN struct btc_coexist *btcoexist, IN boolean new_ps_state)
{
	u8	lps_mode = 0x0;

	btcoexist->btc_get(btcoexist, BTC_GET_U1_LPS_MODE, &lps_mode);

	if (lps_mode) {	/* already under LPS state */
		if (new_ps_state) {
			/* keep state under LPS, do nothing. */
		} else {
			/* will leave LPS state, turn off psTdma first */
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false,
						8);
		}
	} else {					/* NO PS state */
		if (new_ps_state) {
			/* will enter LPS state, turn off psTdma first */
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false,
						8);
		} else {
			/* keep state under NO PS state, do nothing. */
		}
	}
}

void halbtc8723b1ant_power_save_state(IN struct btc_coexist *btcoexist,
			      IN u8 ps_type, IN u8 lps_val, IN u8 rpwm_val)
{
	boolean		low_pwr_disable = false;

	switch (ps_type) {
	case BTC_PS_WIFI_NATIVE:
		/* recover to original 32k low power setting */
		low_pwr_disable = false;
		btcoexist->btc_set(btcoexist,
				   BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_NORMAL_LPS,
				   NULL);
		coex_sta->force_lps_on = false;
		break;
	case BTC_PS_LPS_ON:
		halbtc8723b1ant_ps_tdma_check_for_power_save_state(
			btcoexist, true);
		halbtc8723b1ant_lps_rpwm(btcoexist, NORMAL_EXEC,
					 lps_val, rpwm_val);
		/* when coex force to enter LPS, do not enter 32k low power. */
		low_pwr_disable = true;
		btcoexist->btc_set(btcoexist,
				   BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);
		/* power save must executed before psTdma.			 */
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_ENTER_LPS,
				   NULL);
		coex_sta->force_lps_on = true;
		break;
	case BTC_PS_LPS_OFF:
		halbtc8723b1ant_ps_tdma_check_for_power_save_state(
			btcoexist, false);
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS,
				   NULL);
		coex_sta->force_lps_on = false;
		break;
	default:
		break;
	}
}

void halbtc8723b1ant_action_wifi_only(IN struct btc_coexist *btcoexist)
{
	halbtc8723b1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 0);
	halbtc8723b1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 8);
	halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA, FORCE_EXEC,
				     false, false);
}

void halbtc8723b1ant_monitor_bt_enable_disable(IN struct btc_coexist *btcoexist)
{
	static u32		bt_disable_cnt = 0;
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
	} else {
		bt_disable_cnt++;
		if (bt_disable_cnt >= 2)
			bt_disabled = true;
	}
	if (coex_sta->bt_disabled != bt_disabled) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is from %s to %s!!\n",
			    (coex_sta->bt_disabled ? "disabled" : "enabled"),
			    (bt_disabled ? "disabled" : "enabled"));
		BTC_TRACE(trace_buf);

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

/* *********************************************
 *
 *	Non-Software Coex Mechanism start
 *
 * ********************************************* */
void halbtc8723b1ant_action_bt_whck_test(IN struct btc_coexist *btcoexist)
{
	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0,
					 0x0);

	halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
	halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA, NORMAL_EXEC,
				     false, false);
	halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
}

void halbtc8723b1ant_action_wifi_multi_port(IN struct btc_coexist *btcoexist)
{
	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0,
					 0x0);

	halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
	halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA, NORMAL_EXEC,
				     false, false);
	halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
}

void halbtc8723b1ant_action_hs(IN struct btc_coexist *btcoexist)
{
	halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 5);
	halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
}

void halbtc8723b1ant_action_bt_inquiry(IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean			wifi_connected = false, ap_enable = false, wifi_busy = false,
				bt_busy = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE,
			   &ap_enable);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bt_busy);

	if (coex_sta->bt_abnormal_scan) {
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
					33);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
	} else if ((!wifi_connected) && (!coex_sta->wifi_is_high_pri_task)) {
		halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA,
					     NORMAL_EXEC, false, false);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
	} else if ((bt_link_info->sco_exist) || (bt_link_info->hid_exist) ||
		   (bt_link_info->a2dp_exist)) {
		/* SCO/HID/A2DP  busy */
		halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);

		if (coex_sta->c2h_bt_remote_name_req)
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						33);
		else
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						32);

		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
	} else if ((bt_link_info->pan_exist) || (wifi_busy)) {
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

void halbtc8723b1ant_action_bt_sco_hid_only_busy(IN struct btc_coexist
		*btcoexist, IN u8 wifi_status)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean	wifi_connected = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);

	/* tdma and coex table */

	if (bt_link_info->sco_exist) {
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 5);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 5);
	} else { /* HID */
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 6);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 5);
	}
}

void halbtc8723b1ant_action_wifi_connected_bt_acl_busy(IN struct btc_coexist
		*btcoexist, IN u8 wifi_status)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	if ((coex_sta->low_priority_rx >= 950)  && (!coex_sta->under_ips))
		bt_link_info->slave_role = true;
	else
		bt_link_info->slave_role = false;

	if (bt_link_info->hid_only) { /* HID */
		halbtc8723b1ant_action_bt_sco_hid_only_busy(btcoexist,
				wifi_status);
		coex_dm->auto_tdma_adjust = false;
		return;
	} else if (bt_link_info->a2dp_only) { /* A2DP		 */
		if (BT_8723B_1ANT_WIFI_STATUS_CONNECTED_IDLE == wifi_status) {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						32);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 4);
			coex_dm->auto_tdma_adjust = false;
		} else {
			halbtc8723b1ant_tdma_duration_adjust_for_acl(btcoexist,
					wifi_status);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 4);
			coex_dm->auto_tdma_adjust = true;
		}
	} else if (((bt_link_info->a2dp_exist) && (bt_link_info->pan_exist)) ||
		   (bt_link_info->hid_exist && bt_link_info->a2dp_exist &&
		bt_link_info->pan_exist)) { /* A2DP+PAN(OPP,FTP), HID+A2DP+PAN(OPP,FTP) */
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 13);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
		coex_dm->auto_tdma_adjust = false;
	} else if (bt_link_info->hid_exist &&
		   bt_link_info->a2dp_exist) { /* HID+A2DP */
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 14);
		coex_dm->auto_tdma_adjust = false;

		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
	} else if ((bt_link_info->pan_only) || (bt_link_info->hid_exist &&
		bt_link_info->pan_exist)) { /* PAN(OPP,FTP), HID+PAN(OPP,FTP)			 */

		if (BT_8723B_1ANT_WIFI_STATUS_CONNECTED_IDLE == wifi_status)
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 9);
		else
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

void halbtc8723b1ant_action_wifi_not_connected(IN struct btc_coexist *btcoexist)
{
	/* power save state */
	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0,
					 0x0);

	/* tdma and coex table */
	halbtc8723b1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 8);
	halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA, NORMAL_EXEC,
				     false, false);
	halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
}

void halbtc8723b1ant_action_wifi_not_connected_scan(IN struct btc_coexist
		*btcoexist)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0,
					 0x0);

	/* tdma and coex table */
	if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) {
		if (bt_link_info->a2dp_exist) {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						32);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 4);
		} else if (bt_link_info->a2dp_exist &&
			   bt_link_info->pan_exist) {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						22);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 4);
		} else {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						20);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 4);
		}
	} else if ((BT_8723B_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
		   (BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY ==
		    coex_dm->bt_status)) {
		halbtc8723b1ant_action_bt_sco_hid_only_busy(btcoexist,
				BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SCAN);
	} else {
		/* Bryant Add */
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA,
					     NORMAL_EXEC, false, false);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
	}
}

void halbtc8723b1ant_action_wifi_not_connected_asso_auth(
	IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0,
					 0x0);

	/* tdma and coex table */
	if ((bt_link_info->sco_exist)  || (bt_link_info->hid_exist) ||
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

void halbtc8723b1ant_action_wifi_connected_scan(IN struct btc_coexist
		*btcoexist)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0,
					 0x0);

	/* tdma and coex table */
	if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) {
		if (bt_link_info->a2dp_exist) {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						32);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 4);
		} else if (bt_link_info->a2dp_exist &&
			   bt_link_info->pan_exist) {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						22);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 4);
		} else {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						20);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 4);
		}
	} else if ((BT_8723B_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
		   (BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY ==
		    coex_dm->bt_status)) {
		halbtc8723b1ant_action_bt_sco_hid_only_busy(btcoexist,
				BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SCAN);
	} else {
		/* Bryant Add */
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA,
					     NORMAL_EXEC, false, false);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
	}
}

void halbtc8723b1ant_action_wifi_connected_specific_packet(
	IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean wifi_busy = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	/* no specific packet process for both WiFi and BT very busy */
	if ((wifi_busy) && ((bt_link_info->pan_exist) ||
			    (coex_sta->num_of_profile >= 2)))
		return;

	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0,
					 0x0);

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

void halbtc8723b1ant_action_wifi_connected(IN struct btc_coexist *btcoexist)
{
	boolean	wifi_busy = false;
	boolean		scan = false, link = false, roam = false;
	boolean		under_4way = false, ap_enable = false;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], CoexForWifiConnect()===>\n");
	BTC_TRACE(trace_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS,
			   &under_4way);
	if (under_4way) {
		halbtc8723b1ant_action_wifi_connected_specific_packet(
			btcoexist);
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], CoexForWifiConnect(), return for wifi is under 4way<===\n");
		BTC_TRACE(trace_buf);
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);
	if (scan || link || roam) {
		if (scan)
			halbtc8723b1ant_action_wifi_connected_scan(btcoexist);
		else
			halbtc8723b1ant_action_wifi_connected_specific_packet(
				btcoexist);
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], CoexForWifiConnect(), return for wifi is under scan<===\n");
		BTC_TRACE(trace_buf);
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE,
			   &ap_enable);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	/* power save state */
	if (!ap_enable &&
	    BT_8723B_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status &&
	    !btcoexist->bt_link_info.hid_only) {
		if (btcoexist->bt_link_info.a2dp_only) {	/* A2DP */
			if (!wifi_busy)
				halbtc8723b1ant_power_save_state(btcoexist,
						 BTC_PS_WIFI_NATIVE, 0x0, 0x0);
			else { /* busy */
				if (coex_sta->scan_ap_num >=
				    BT_8723B_1ANT_WIFI_NOISY_THRESH)  /* no force LPS, no PS-TDMA, use pure TDMA */
					halbtc8723b1ant_power_save_state(
						btcoexist, BTC_PS_WIFI_NATIVE,
						0x0, 0x0);
				else
					halbtc8723b1ant_power_save_state(
						btcoexist, BTC_PS_LPS_ON, 0x50,
						0x4);
			}
		} else if ((coex_sta->pan_exist == false) &&
			   (coex_sta->a2dp_exist == false) &&
			   (coex_sta->hid_exist == false))
			halbtc8723b1ant_power_save_state(btcoexist,
						 BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		else
			halbtc8723b1ant_power_save_state(btcoexist,
						 BTC_PS_LPS_ON, 0x50, 0x4);
	} else
		halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);

	/* tdma and coex table */
	if (!wifi_busy) {
		if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) {
			halbtc8723b1ant_action_wifi_connected_bt_acl_busy(
				btcoexist,
				BT_8723B_1ANT_WIFI_STATUS_CONNECTED_IDLE);
		} else if ((BT_8723B_1ANT_BT_STATUS_SCO_BUSY ==
			    coex_dm->bt_status) ||
			   (BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY ==
			    coex_dm->bt_status)) {
			halbtc8723b1ant_action_bt_sco_hid_only_busy(btcoexist,
				BT_8723B_1ANT_WIFI_STATUS_CONNECTED_IDLE);
		} else {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false,
						8);
			halbtc8723b1ant_set_ant_path(btcoexist,
				BTC_ANT_PATH_PTA, NORMAL_EXEC, false, false);
			/* if ((coex_sta->high_priority_tx) +
			    (coex_sta->high_priority_rx) <= 60) */
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 2);
			/* else
				halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 7); */
		}
	} else {
		if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) {
			halbtc8723b1ant_action_wifi_connected_bt_acl_busy(
				btcoexist,
				BT_8723B_1ANT_WIFI_STATUS_CONNECTED_BUSY);
		} else if ((BT_8723B_1ANT_BT_STATUS_SCO_BUSY ==
			    coex_dm->bt_status) ||
			   (BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY ==
			    coex_dm->bt_status)) {
			halbtc8723b1ant_action_bt_sco_hid_only_busy(btcoexist,
				BT_8723B_1ANT_WIFI_STATUS_CONNECTED_BUSY);
		} else {
			/* halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false,
						8);
			halbtc8723b1ant_set_ant_path(btcoexist,
				BTC_ANT_PATH_PTA, NORMAL_EXEC, false, false);
			if ((coex_sta->high_priority_tx) +
			    (coex_sta->high_priority_rx) <= 60)
				halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 2);
			else
				halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 7); */
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						32);
			halbtc8723b1ant_set_ant_path(btcoexist,
				BTC_ANT_PATH_PTA, NORMAL_EXEC, false, false);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 4);

		}
	}
}

void halbtc8723b1ant_run_coexist_mechanism(IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean	wifi_connected = false, bt_hs_on = false, wifi_busy = false;
	boolean	increase_scan_dev_num = false;
	boolean	bt_ctrl_agg_buf_size = false;
	boolean	miracast_plus_bt = false;
	u8	agg_buf_size = 5;
	u32	wifi_link_status = 0;
	u32	num_of_wifi_link = 0, wifi_bw;
	u8	iot_peer = BTC_IOT_PEER_UNKNOWN;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], RunCoexistMechanism()===>\n");
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

	if (coex_sta->bt_whck_test) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is under WHCK TEST!!!\n");
		BTC_TRACE(trace_buf);
		halbtc8723b1ant_action_bt_whck_test(btcoexist);
		return;
	}

	if ((BT_8723B_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) ||
	    (BT_8723B_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
	    (BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY == coex_dm->bt_status))
		increase_scan_dev_num = true;

	btcoexist->btc_set(btcoexist, BTC_SET_BL_INC_SCAN_DEV_NUM,
			   &increase_scan_dev_num);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
			   &wifi_link_status);
	num_of_wifi_link = wifi_link_status >> 16;

	if ((num_of_wifi_link >= 2) ||
	    (wifi_link_status & WIFI_P2P_GO_CONNECTED)) {
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

		if (((bt_link_info->a2dp_exist) || (wifi_busy)) &&
		    (coex_sta->c2h_bt_inquiry_page))
			halbtc8723b1ant_action_bt_inquiry(btcoexist);
		else
			halbtc8723b1ant_action_wifi_multi_port(btcoexist);

		return;
	} else {
		miracast_plus_bt = false;
		btcoexist->btc_set(btcoexist, BTC_SET_BL_MIRACAST_PLUS_BT,
				   &miracast_plus_bt);
	}

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if ((bt_link_info->bt_link_exist) && (wifi_connected)) {
		halbtc8723b1ant_limited_tx(btcoexist, NORMAL_EXEC, 1, 1, 0, 1);

		btcoexist->btc_get(btcoexist, BTC_GET_U1_IOT_PEER, &iot_peer);

		/* if(BTC_IOT_PEER_CISCO != iot_peer)		 */
		if ((BTC_IOT_PEER_CISCO != iot_peer) &&
		    (BTC_IOT_PEER_BROADCOM != iot_peer)) {
			if (bt_link_info->sco_exist) /* if (bt_link_info->bt_hi_pri_link_exist) */
				/* halbtc8723b1ant_limited_rx(btcoexist, NORMAL_EXEC, true, false, 0x5);				 */
				halbtc8723b1ant_limited_rx(btcoexist,
					   NORMAL_EXEC, true, false, 0x5);
			else
				halbtc8723b1ant_limited_rx(btcoexist,
					   NORMAL_EXEC, false, false, 0x5);
			/* halbtc8723b1ant_limited_rx(btcoexist, NORMAL_EXEC, false, true, 0x8);		 */
		} else {
			if (bt_link_info->sco_exist)
				halbtc8723b1ant_limited_rx(btcoexist,
					   NORMAL_EXEC, true, false, 0x5);
			else {
				if (BTC_WIFI_BW_HT40 == wifi_bw)
					halbtc8723b1ant_limited_rx(btcoexist,
						NORMAL_EXEC, false, true, 0x10);
				else
					halbtc8723b1ant_limited_rx(btcoexist,
						NORMAL_EXEC, false, true, 0x8);
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
		boolean	scan = false, link = false, roam = false;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], wifi is non connected-idle !!!\n");
		BTC_TRACE(trace_buf);

		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);

		if (scan || link || roam) {
			if (scan)
				halbtc8723b1ant_action_wifi_not_connected_scan(
					btcoexist);
			else
				halbtc8723b1ant_action_wifi_not_connected_asso_auth(
					btcoexist);
		} else
			halbtc8723b1ant_action_wifi_not_connected(btcoexist);
	} else	/* wifi LPS/Busy */
		halbtc8723b1ant_action_wifi_connected(btcoexist);
}

void halbtc8723b1ant_init_coex_dm(IN struct btc_coexist *btcoexist)
{
	/* force to reset coex mechanism */

	/* sw all off */
	halbtc8723b1ant_sw_mechanism(btcoexist, false);

	/* halbtc8723b1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 8); */
	/* halbtc8723b1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 0); */

	coex_sta->pop_event_cnt = 0;
}

void halbtc8723b1ant_init_hw_config(IN struct btc_coexist *btcoexist,
				    IN boolean back_up, IN boolean wifi_only)
{
	u32				u32tmp = 0; /* , fw_ver; */
	u8				u8tmpa = 0, u8tmpb = 0;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], 1Ant Init HW Config!!\n");
	BTC_TRACE(trace_buf);

	psd_scan->ant_det_is_ant_det_available = false;

	/* 0xf0[15:12] --> Chip Cut information */
	coex_sta->cut_version = (btcoexist->btc_read_1byte(btcoexist,
				 0xf1) & 0xf0) >> 4;

	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x550, 0x8,
					   0x1);  /* enable TBTT nterrupt */

	/* 0x790[5:0]=0x5	 */
	btcoexist->btc_write_1byte(btcoexist, 0x790, 0x5);

	/* Enable counter statistics */
	/* btcoexist->btc_write_1byte(btcoexist, 0x76e, 0xc); */ /*0x76e[3] =1, WLAN_Act control by PTA */
	btcoexist->btc_write_1byte(btcoexist, 0x778, 0x1);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x40, 0x20, 0x1);


	/* btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x20, 0x1); */ /*BT select s0/s1 is controlled by WiFi */

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

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"############# [BTCoex], 0x948=0x%x, 0x765=0x%x, 0x67=0x%x\n",
		    u32tmp,  u8tmpa, u8tmpb);
	BTC_TRACE(trace_buf);
}

void halbtc8723b1ant_mechanism_switch(IN struct btc_coexist *btcoexist,
				      IN boolean bSwitchTo2Antenna)
{

	if (bSwitchTo2Antenna) { /* 1-Ant -> 2-Ant */
		/* un-lock TRx Mask setup for 8723b f-cut */
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xdd, 0x80, 0x1);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xdf, 0x1, 0x1);
		/* WiFi TRx Mask on				 */
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff,
					  0x0);

		/* BT TRx Mask un-lock 0x2c[0], 0x30[0] = 1 */
		btcoexist->btc_set_bt_reg(btcoexist, BTC_BT_REG_RF, 0x2c,
					  0x7c45);
		btcoexist->btc_set_bt_reg(btcoexist, BTC_BT_REG_RF, 0x30,
					  0x7c45);

		/* BT TRx Mask on */
		btcoexist->btc_set_bt_reg(btcoexist, BTC_BT_REG_RF, 0x3c, 0x1);

		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_BT,
					     FORCE_EXEC, false, false);
	} else {
		/* WiFi TRx Mask on				 */
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff,
					  0x780);

		/* lock TRx Mask setup for 8723b f-cut */
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xdd, 0x80, 0x0);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xdf, 0x1, 0x0);

		/* BT TRx Mask on */
		btcoexist->btc_set_bt_reg(btcoexist, BTC_BT_REG_RF, 0x3c, 0x15);

		/* BT TRx Mask ock 0x2c[0], 0x30[0]  = 0 */
		btcoexist->btc_set_bt_reg(btcoexist, BTC_BT_REG_RF, 0x2c,
					  0x7c44);
		btcoexist->btc_set_bt_reg(btcoexist, BTC_BT_REG_RF, 0x30,
					  0x7c44);


		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA,
					     FORCE_EXEC, false, false);
	}

}

u32 halbtc8723b1ant_psd_log2base(IN struct btc_coexist *btcoexist, IN u32 val)
{
	u8	j;
	u32	tmp, tmp2, val_integerd_b = 0, tindex, shiftcount = 0;
	u32	result, val_fractiond_b = 0, table_fraction[21] = {0, 432, 332, 274, 232, 200,
				   174, 151, 132, 115, 100, 86, 74, 62, 51, 42,
							   32, 23, 15, 7, 0
							      };

	if (val == 0)
		return 0;

	tmp = val;

	while (1) {
		if (tmp == 1)
			break;
		else {
			tmp = (tmp >> 1);
			shiftcount++;
		}
	}


	val_integerd_b = shiftcount + 1;

	tmp2 = 1;
	for (j = 1; j <= val_integerd_b; j++)
		tmp2 = tmp2 * 2;

	tmp = (val * 100) / tmp2;
	tindex = tmp / 5;

	if (tindex > 20)
		tindex = 20;

	val_fractiond_b = table_fraction[tindex];

	result = val_integerd_b * 100 - val_fractiond_b;

	return result;


}

void halbtc8723b1ant_psd_show_antenna_detect_result(IN struct btc_coexist
		*btcoexist)
{
	u8		*cli_buf = btcoexist->cli_buf;
	struct  btc_board_info	*board_info = &btcoexist->board_info;

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n============[Antenna Detection info]  ============\n");
	CL_PRINTF(cli_buf);

	if (psd_scan->ant_det_result == 1)
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s (>%d)",
			   "Ant Det Result", "2-Antenna (Bad-Isolation)",
			   BT_8723B_1ANT_ANTDET_PSDTHRES_2ANT_BADISOLATION);
	else if (psd_scan->ant_det_result == 2)
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s (%d~%d)",
			   "Ant Det Result", "2-Antenna (Good-Isolation)",
			   BT_8723B_1ANT_ANTDET_PSDTHRES_2ANT_GOODISOLATION
			   + psd_scan->ant_det_thres_offset,
			   BT_8723B_1ANT_ANTDET_PSDTHRES_2ANT_BADISOLATION);
	else
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s (%d~%d)",
			   "Ant Det Result", "1-Antenna",
			   BT_8723B_1ANT_ANTDET_PSDTHRES_1ANT,
			   BT_8723B_1ANT_ANTDET_PSDTHRES_2ANT_GOODISOLATION
			   + psd_scan->ant_det_thres_offset);

	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s ",
		   "Antenna Detection Finish",
		   (board_info->btdm_ant_det_finish
		    ? "Yes" : "No"));
	CL_PRINTF(cli_buf);

	switch (psd_scan->ant_det_result) {
	case 0:
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "(BT is not available)");
		break;
	case 1:  /* 2-Ant bad-isolation */
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "(BT is available)");
		break;
	case 2:  /* 2-Ant good-isolation */
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "(BT is available)");
		break;
	case 3:  /* 1-Ant */
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "(BT is available)");
		break;
	case 4:
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "(Uncertainty result)");
		break;
	case 5:
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "(Pre-Scan fai)");
		break;
	case 6:
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "(WiFi is Scanning)");
		break;
	case 7:
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "(BT is not idle)");
		break;
	case 8:
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "(Abort by WiFi Scanning)");
		break;
	case 9:
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "(Antenna Init is not ready)");
		break;
	case 10:
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "(BT is Inquiry or page)");
		break;
	case 11:
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "(BT is Disabled)");
		break;
	}
	CL_PRINTF(cli_buf);


	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d",
		   "Ant Detect Total Count", psd_scan->ant_det_try_count);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d",
		   "Ant Detect Fail Count", psd_scan->ant_det_fail_count);
	CL_PRINTF(cli_buf);

	if ((!board_info->btdm_ant_det_finish) &&
	    (psd_scan->ant_det_result != 5))
		return;

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s", "BT Response",
		   (psd_scan->ant_det_result ? "ok" : "fail"));
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d ms", "BT Tx Time",
		   psd_scan->ant_det_bt_tx_time);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "BT Tx Ch",
		   psd_scan->ant_det_bt_le_channel);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d",
		   "WiFi PSD Cent-Ch/Offset/Span",
		   psd_scan->real_cent_freq, psd_scan->real_offset,
		   psd_scan->real_span);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d dB",
		   "PSD Pre-Scan Peak Value",
		   psd_scan->ant_det_pre_psdscan_peak_val / 100);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s (<= %d)",
		   "PSD Pre-Scan result",
		   (psd_scan->ant_det_result != 5 ? "ok" : "fail"),
		   BT_8723B_1ANT_ANTDET_PSDTHRES_BACKGROUND
		   + psd_scan->ant_det_thres_offset);
	CL_PRINTF(cli_buf);

	if (psd_scan->ant_det_result == 5)
		return;

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s dB",
		   "PSD Scan Peak Value", psd_scan->ant_det_peak_val);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s MHz",
		   "PSD Scan Peak Freq", psd_scan->ant_det_peak_freq);
	CL_PRINTF(cli_buf);


	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s", "TFBGA Package",
		   (board_info->tfbga_package) ?  "Yes" : "No");
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d",
		   "PSD Threshold Offset", psd_scan->ant_det_thres_offset);
	CL_PRINTF(cli_buf);

}

void halbtc8723b1ant_psd_showdata(IN struct btc_coexist *btcoexist)
{
	u8		*cli_buf = btcoexist->cli_buf;
	u32		delta_freq_per_point;
	u32		freq, freq1, freq2, n = 0, i = 0, j = 0, m = 0, psd_rep1, psd_rep2;

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n\n============[PSD info]  (%d)============\n",
		   psd_scan->psd_gen_count);
	CL_PRINTF(cli_buf);

	if (psd_scan->psd_gen_count == 0) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n No data !!\n");
		CL_PRINTF(cli_buf);
		return;
	}

	if (psd_scan->psd_point == 0)
		delta_freq_per_point = 0;
	else
		delta_freq_per_point = psd_scan->psd_band_width /
				       psd_scan->psd_point;

	/* if (psd_scan->is_psd_show_max_only) */
	if (0) {
		psd_rep1 = psd_scan->psd_max_value / 100;
		psd_rep2 = psd_scan->psd_max_value - psd_rep1 * 100;

		freq = ((psd_scan->real_cent_freq - 20) * 1000000 +
			psd_scan->psd_max_value_point * delta_freq_per_point);
		freq1 = freq / 1000000;
		freq2 = freq / 1000 - freq1 * 1000;

		if (freq2 < 100)
			CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
				   "\r\n Freq = %d.0%d MHz",
				   freq1, freq2);
		else
			CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
				   "\r\n Freq = %d.%d MHz",
				   freq1, freq2);

		if (psd_rep2 < 10)
			CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
				   ", Value = %d.0%d dB, (%d)\n",
				   psd_rep1, psd_rep2, psd_scan->psd_max_value);
		else
			CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
				   ", Value = %d.%d dB, (%d)\n",
				   psd_rep1, psd_rep2, psd_scan->psd_max_value);

		CL_PRINTF(cli_buf);
	} else {
		m = psd_scan->psd_start_point;
		n = psd_scan->psd_start_point;
		i = 1;
		j = 1;

		while (1) {
			do {
				freq = ((psd_scan->real_cent_freq - 20) *
					1000000 + m *
					delta_freq_per_point);
				freq1 = freq / 1000000;
				freq2 = freq / 1000 - freq1 * 1000;

				if (i == 1) {
					if (freq2 == 0)
						CL_SPRINTF(cli_buf,
							   BT_TMP_BUF_SIZE,
							   "\r\n Freq%6d.000",
							   freq1);
					else if (freq2 < 100)
						CL_SPRINTF(cli_buf,
							   BT_TMP_BUF_SIZE,
							   "\r\n Freq%6d.0%2d",
							   freq1,
							   freq2);
					else
						CL_SPRINTF(cli_buf,
							   BT_TMP_BUF_SIZE,
							   "\r\n Freq%6d.%3d",
							   freq1,
							   freq2);
				} else if ((i % 8 == 0) ||
					   (m == psd_scan->psd_stop_point)) {
					if (freq2 == 0)
						CL_SPRINTF(cli_buf,
							   BT_TMP_BUF_SIZE,
							   "%6d.000\n", freq1);
					else if (freq2 < 100)
						CL_SPRINTF(cli_buf,
							   BT_TMP_BUF_SIZE,
							   "%6d.0%2d\n", freq1,
							   freq2);
					else
						CL_SPRINTF(cli_buf,
							   BT_TMP_BUF_SIZE,
							   "%6d.%3d\n", freq1,
							   freq2);
				} else {
					if (freq2 == 0)
						CL_SPRINTF(cli_buf,
							   BT_TMP_BUF_SIZE,
							   "%6d.000", freq1);
					else if (freq2 < 100)
						CL_SPRINTF(cli_buf,
							   BT_TMP_BUF_SIZE,
							   "%6d.0%2d", freq1,
							   freq2);
					else
						CL_SPRINTF(cli_buf,
							   BT_TMP_BUF_SIZE,
							   "%6d.%3d", freq1,
							   freq2);
				}

				i++;
				m++;
				CL_PRINTF(cli_buf);

			} while ((i <= 8) && (m <= psd_scan->psd_stop_point));


			do {
				psd_rep1 = psd_scan->psd_report_max_hold[n] /
					   100;
				psd_rep2 = psd_scan->psd_report_max_hold[n] -
					   psd_rep1 *
					   100;

				if (j == 1) {
					if (psd_rep2 < 10)
						CL_SPRINTF(cli_buf,
							   BT_TMP_BUF_SIZE,
							   "\r\n Val %7d.0%d",
							   psd_rep1,
							   psd_rep2);
					else
						CL_SPRINTF(cli_buf,
							   BT_TMP_BUF_SIZE,
							   "\r\n Val %7d.%d",
							   psd_rep1,
							   psd_rep2);
				} else if ((j % 8 == 0)  ||
					   (n == psd_scan->psd_stop_point)) {
					if (psd_rep2 < 10)
						CL_SPRINTF(cli_buf,
							   BT_TMP_BUF_SIZE,
							"%7d.0%d\n", psd_rep1,
							   psd_rep2);
					else
						CL_SPRINTF(cli_buf,
							   BT_TMP_BUF_SIZE,
							   "%7d.%d\n", psd_rep1,
							   psd_rep2);
				} else {
					if (psd_rep2 < 10)
						CL_SPRINTF(cli_buf,
							   BT_TMP_BUF_SIZE,
							   "%7d.0%d", psd_rep1,
							   psd_rep2);
					else
						CL_SPRINTF(cli_buf,
							   BT_TMP_BUF_SIZE,
							   "%7d.%d", psd_rep1,
							   psd_rep2);
				}

				j++;
				n++;
				CL_PRINTF(cli_buf);

			} while ((j <= 8) && (n <= psd_scan->psd_stop_point));

			if ((m > psd_scan->psd_stop_point) ||
			    (n > psd_scan->psd_stop_point))
				break;
			else {
				i = 1;
				j = 1;
			}

		}
	}


}

void halbtc8723b1ant_psd_max_holddata(IN struct btc_coexist *btcoexist,
				      IN u32 gen_count)
{
	u32	i = 0, i_max = 0, val_max = 0;

	if (gen_count == 1) {
		memcpy(psd_scan->psd_report_max_hold,
		       psd_scan->psd_report,
		       BT_8723B_1ANT_ANTDET_PSD_POINTS * sizeof(u32));

		psd_scan->psd_max_value_point = 0;
		psd_scan->psd_max_value = 0;

	} else {
		for (i = psd_scan->psd_start_point;
		     i <= psd_scan->psd_stop_point; i++) {
			if (psd_scan->psd_report[i] >
			    psd_scan->psd_report_max_hold[i])
				psd_scan->psd_report_max_hold[i] =
					psd_scan->psd_report[i];

			/* search Max Value */
			if (i == psd_scan->psd_start_point) {
				i_max = i;
				val_max = psd_scan->psd_report_max_hold[i];
			} else {
				if (psd_scan->psd_report_max_hold[i] >
				    val_max) {
					i_max = i;
					val_max = psd_scan->psd_report_max_hold[i];
				}
			}

		}

		psd_scan->psd_max_value_point = i_max;
		psd_scan->psd_max_value = val_max;

	}


}

u32 halbtc8723b1ant_psd_getdata(IN struct btc_coexist *btcoexist, IN u32 point)
{
	/* reg 0x808[9:0]: FFT data x */
	/* reg 0x808[22]: 0-->1 to get 1 FFT data y */
	/* reg 0x8b4[15:0]: FFT data y report */

	u32 val = 0, psd_report = 0;
	int k = 0;

	val = btcoexist->btc_read_4byte(btcoexist, 0x808);

	val &= 0xffbffc00;
	val |= point;

	btcoexist->btc_write_4byte(btcoexist, 0x808, val);

	val |= 0x00400000;
	btcoexist->btc_write_4byte(btcoexist, 0x808, val);

	while (1) {
		if (k++ > BT_8723B_1ANT_ANTDET_SWEEPPOINT_DELAY)
			break;
	}

	val = btcoexist->btc_read_4byte(btcoexist, 0x8b4);

	psd_report = val & 0x0000ffff;

	return psd_report;
}


boolean halbtc8723b1ant_psd_sweep_point(IN struct btc_coexist *btcoexist,
		IN u32 cent_freq, IN s32 offset, IN u32 span, IN u32 points,
					IN u32 avgnum, IN u32 loopcnt)
{
	u32	 i, val, n, k = 0, j, point_index = 0;
	u32	points1 = 0, psd_report = 0;
	u32	start_p = 0, stop_p = 0, delta_freq_per_point = 156250;
	u32    psd_center_freq = 20 * 10 ^ 6;
	boolean outloop = false, scan , roam, is_sweep_ok = true;
	u8	 flag = 0;
	u32	tmp;
	u32	wifi_original_channel = 1;

	psd_scan->is_psd_running = true;
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "xxxxxxxxxxxxxxxx PSD Sweep Start!!\n");
	BTC_TRACE(trace_buf);

	do {
		switch (flag) {
		case 0:  /* Get PSD parameters */
		default:

			psd_scan->psd_band_width = 40 * 1000000;
			psd_scan->psd_point = points;
			psd_scan->psd_start_base = points / 2;
			psd_scan->psd_avg_num = avgnum;
			psd_scan->real_cent_freq = cent_freq;
			psd_scan->real_offset = offset;
			psd_scan->real_span = span;


			points1 = psd_scan->psd_point;
			delta_freq_per_point = psd_scan->psd_band_width /
					       psd_scan->psd_point;

			/* PSD point setup */
			val = btcoexist->btc_read_4byte(btcoexist, 0x808);
			val &= 0xffff0fff;

			switch (psd_scan->psd_point) {
			case 128:
				val |= 0x0;
				break;
			case 256:
			default:
				val |= 0x00004000;
				break;
			case 512:
				val |= 0x00008000;
				break;
			case 1024:
				val |= 0x0000c000;
				break;
			}

			switch (psd_scan->psd_avg_num) {
			case 1:
				val |= 0x0;
				break;
			case 8:
				val |= 0x00001000;
				break;
			case 16:
				val |= 0x00002000;
				break;
			case 32:
			default:
				val |= 0x00003000;
				break;
			}
			btcoexist->btc_write_4byte(btcoexist, 0x808, val);

			flag = 1;
			break;
		case 1:	  /* calculate the PSD point index from freq/offset/span */
			psd_center_freq = psd_scan->psd_band_width / 2 +
					  offset * (1000000);

			start_p = psd_scan->psd_start_base + (psd_center_freq -
				span * (1000000) / 2) / delta_freq_per_point;
			psd_scan->psd_start_point = start_p -
						    psd_scan->psd_start_base;

			stop_p = psd_scan->psd_start_base + (psd_center_freq +
				span * (1000000) / 2) / delta_freq_per_point;
			psd_scan->psd_stop_point = stop_p -
						   psd_scan->psd_start_base - 1;

			flag = 2;
			break;
		case 2:  /* set RF channel/BW/Mode */

			/* set 3-wire off */
			val = btcoexist->btc_read_4byte(btcoexist, 0x88c);
			val |= 0x00300000;
			btcoexist->btc_write_4byte(btcoexist, 0x88c, val);

			/* CCK off */
			val = btcoexist->btc_read_4byte(btcoexist, 0x800);
			val &= 0xfeffffff;
			btcoexist->btc_write_4byte(btcoexist, 0x800, val);

			/* store WiFi original channel */
			wifi_original_channel = btcoexist->btc_get_rf_reg(
					btcoexist, BTC_RF_A, 0x18, 0x3ff);

			/* Set RF channel */
			if (cent_freq == 2484)
				btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A,
							  0x18, 0x3ff, 0xe);
			else
				btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A,
					  0x18, 0x3ff, (cent_freq - 2412) / 5 +
						  1); /* WiFi TRx Mask on */


			/* Set RF Rx filter corner */
			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1e,
						  0xfffff, 0x3e4);

			/* Set TRx mask off */
			/* un-lock TRx Mask setup */
			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xdd,
						  0x80, 0x1);
			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xdf,
						  0x1, 0x1);

			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1,
						  0xfffff, 0x0);

			/* Set  RF mode = Rx, RF Gain = 0x8a0 */
			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x0,
						  0xfffff, 0x308a0);

			while (1) {
				if (k++ > BT_8723B_1ANT_ANTDET_SWEEPPOINT_DELAY)
					break;
			}
			flag = 3;
			break;
		case 3:
			psd_scan->psd_gen_count = 0;
			for (j = 1; j <= loopcnt; j++) {

				btcoexist->btc_get(btcoexist,
						   BTC_GET_BL_WIFI_SCAN, &scan);
				btcoexist->btc_get(btcoexist,
						   BTC_GET_BL_WIFI_ROAM, &roam);

				if (scan || roam) {
					is_sweep_ok = false;
					break;
				}
				memset(psd_scan->psd_report, 0,
				       psd_scan->psd_point * sizeof(u32));
				start_p = psd_scan->psd_start_point +
					  psd_scan->psd_start_base;
				stop_p = psd_scan->psd_stop_point +
					 psd_scan->psd_start_base + 1;

				i = start_p;
				point_index = 0;

				while (i < stop_p) {
					if (i >= points1)
						psd_report =
							halbtc8723b1ant_psd_getdata(
							btcoexist, i - points1);
					else
						psd_report =
							halbtc8723b1ant_psd_getdata(
								btcoexist, i);

					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"Point=%d, psd_raw_data = 0x%08x\n",
						    i, psd_report);
					BTC_TRACE(trace_buf);
					if (psd_report == 0)
						tmp = 0;
					else
						/* tmp =  20*log10((double)psd_report); */
						/* 20*log2(x)/log2(10), log2Base return theresult of the psd_report*100 */
						tmp = 6 * halbtc8723b1ant_psd_log2base(
							btcoexist, psd_report);

					n = i - psd_scan->psd_start_base;
					psd_scan->psd_report[n] =  tmp;


					halbtc8723b1ant_psd_max_holddata(
						btcoexist, j);

					i++;

				}

				psd_scan->psd_gen_count = j;
			}

			flag = 100;
			break;
		case 99:	/* error */

			outloop = true;
			break;
		case 100: /* recovery */

			/* set 3-wire on */
			val = btcoexist->btc_read_4byte(btcoexist, 0x88c);
			val &= 0xffcfffff;
			btcoexist->btc_write_4byte(btcoexist, 0x88c, val);

			/* CCK on */
			val = btcoexist->btc_read_4byte(btcoexist, 0x800);
			val |= 0x01000000;
			btcoexist->btc_write_4byte(btcoexist, 0x800, val);

			/* PSD off */
			val = btcoexist->btc_read_4byte(btcoexist, 0x808);
			val &= 0xffbfffff;
			btcoexist->btc_write_4byte(btcoexist, 0x808, val);

			/* TRx Mask on */
			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1,
						  0xfffff, 0x780);

			/* lock TRx Mask setup */
			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xdd,
						  0x80, 0x0);
			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xdf,
						  0x1, 0x0);

			/* Set RF Rx filter corner */
			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1e,
						  0xfffff, 0x0);

			/* restore WiFi original channel */
			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x18,
						  0x3ff, wifi_original_channel);

			outloop = true;
			break;

		}

	} while (!outloop);



	psd_scan->is_psd_running = false;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "xxxxxxxxxxxxxxxx PSD Sweep Stop!!\n");
	BTC_TRACE(trace_buf);
	return is_sweep_ok;

}

void halbtc8723b1ant_psd_antenna_detection(IN struct btc_coexist *btcoexist,
		IN u32 bt_tx_time, IN u32 bt_le_channel)
{
	u32	i = 0;
	u32	wlpsd_cent_freq = 2484, wlpsd_span = 2, wlpsd_sweep_count = 50;
	s32	wlpsd_offset = -4;
	u8	bt_le_ch[13] = {3, 6, 8, 11, 13, 16, 18, 21, 23, 26, 28, 31, 33};

	u8	h2c_parameter[3] = {0}, u8tmpa, u8tmpb;

	u8	state = 0;
	boolean		outloop = false, bt_resp = false;
	u32		freq, freq1, freq2, psd_rep1, psd_rep2, delta_freq_per_point,
			u32tmp;
	struct  btc_board_info	*board_info = &btcoexist->board_info;

	board_info->btdm_ant_det_finish = false;
	memset(psd_scan->ant_det_peak_val, 0, 16 * sizeof(u8));
	memset(psd_scan->ant_det_peak_freq, 0, 16 * sizeof(u8));

	if (board_info->tfbga_package) /* for TFBGA */
		psd_scan->ant_det_thres_offset = 5;
	else
		psd_scan->ant_det_thres_offset = 0;

	do {
		switch (state) {
		case 0:
			if (bt_le_channel == 39)
				wlpsd_cent_freq = 2484;
			else {
				for (i = 1; i <= 13; i++) {
					if (bt_le_ch[i - 1] ==
					    bt_le_channel) {
						wlpsd_cent_freq = 2412
								  + (i - 1) * 5;
						break;
					}
				}

				if (i == 14) {

					BTC_SPRINTF(trace_buf,
						    BT_TMP_BUF_SIZE,
						"xxxxxxxxxxxxxxxx AntennaDetect(), Abort!!, Invalid LE channel = %d\n ",
						    bt_le_channel);
					BTC_TRACE(trace_buf);
					outloop = true;
					break;
				}
			}

			wlpsd_sweep_count = bt_tx_time * 238 /
					    100; /* bt_tx_time/0.42								 */
			wlpsd_sweep_count = wlpsd_sweep_count / 5;

			if (wlpsd_sweep_count % 5 != 0)
				wlpsd_sweep_count = (wlpsd_sweep_count /
						     5 + 1) * 5;

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"xxxxxxxxxxxxxxxx AntennaDetect(), BT_LETxTime=%d,  BT_LECh = %d\n",
				    bt_tx_time, bt_le_channel);
			BTC_TRACE(trace_buf);
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"xxxxxxxxxxxxxxxx AntennaDetect(), wlpsd_cent_freq=%d,  wlpsd_offset = %d, wlpsd_span = %d, wlpsd_sweep_count = %d\n",
				    wlpsd_cent_freq,
				    wlpsd_offset,
				    wlpsd_span,
				    wlpsd_sweep_count);
			BTC_TRACE(trace_buf);

			state = 1;
			break;
		case 1: /* stop coex DM & set antenna path */
			/* Stop Coex DM */
			btcoexist->stop_coex_dm = true;

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"xxxxxxxxxxxxxxxx AntennaDetect(), Stop Coex DM!!\n");
			BTC_TRACE(trace_buf);

			/* set native power save */
			halbtc8723b1ant_power_save_state(btcoexist,
						 BTC_PS_WIFI_NATIVE, 0x0, 0x0);

			/* Set TDMA off,				 */
			halbtc8723b1ant_ps_tdma(btcoexist, FORCE_EXEC,
						false, 0);

			/* Set coex table */
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     FORCE_EXEC, 0);

			if (board_info->btdm_ant_pos ==
			    BTC_ANTENNA_AT_MAIN_PORT) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"xxxxxxxxxxxxxxxx AntennaDetect(), Antenna at Main Port\n");
				BTC_TRACE(trace_buf);
			} else {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"xxxxxxxxxxxxxxxx AntennaDetect(), Antenna at Aux Port\n");
				BTC_TRACE(trace_buf);
			}

			/* Set Antenna path, switch WiFi to un-certain antenna port */
			halbtc8723b1ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_BT, FORCE_EXEC, false,
						     false);

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"xxxxxxxxxxxxxxxx AntennaDetect(), Set Antenna to BT!!\n");
			BTC_TRACE(trace_buf);

			/* Set AFH mask on at WiFi channel 2472MHz +/- 10MHz */
			h2c_parameter[0] = 0x1;
			h2c_parameter[1] = 0xd;
			h2c_parameter[2] = 0x14;

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"xxxxxxxxxxxxxxxx AntennaDetect(), Set AFH on, Cent-Ch= %d,  Mask=%d\n",
				    h2c_parameter[1],
				    h2c_parameter[2]);
			BTC_TRACE(trace_buf);

			btcoexist->btc_fill_h2c(btcoexist, 0x66, 3,
						h2c_parameter);

			u32tmp = btcoexist->btc_read_4byte(btcoexist,
							   0x948);
			u8tmpa = btcoexist->btc_read_1byte(btcoexist, 0x765);
			u8tmpb = btcoexist->btc_read_1byte(btcoexist,
							   0x778);

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"############# [BTCoex], 0x948=0x%x, 0x765=0x%x, 0x778=0x%x\n",
				    u32tmp,  u8tmpa, u8tmpb);
			BTC_TRACE(trace_buf);

			state = 2;
			break;
		case 2:	/* Pre-sweep background psd */
			if (!halbtc8723b1ant_psd_sweep_point(btcoexist,
				     wlpsd_cent_freq, wlpsd_offset, wlpsd_span,
					     BT_8723B_1ANT_ANTDET_PSD_POINTS,
				     BT_8723B_1ANT_ANTDET_PSD_AVGNUM, 3)) {
				board_info->btdm_ant_det_finish = false;
				board_info->btdm_ant_num_by_ant_det = 1;
				psd_scan->ant_det_result = 8;
				state = 99;
				break;
			}

			psd_scan->ant_det_pre_psdscan_peak_val =
				psd_scan->psd_max_value;

			if (psd_scan->psd_max_value >
			    (BT_8723B_1ANT_ANTDET_PSDTHRES_BACKGROUND
			     + psd_scan->ant_det_thres_offset) * 100) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"xxxxxxxxxxxxxxxx AntennaDetect(), Abort Antenna Detection!! becaus background = %d > thres (%d)\n",
					    psd_scan->psd_max_value / 100,
					BT_8723B_1ANT_ANTDET_PSDTHRES_BACKGROUND
					    + psd_scan->ant_det_thres_offset);
				BTC_TRACE(trace_buf);
				board_info->btdm_ant_det_finish = false;
				board_info->btdm_ant_num_by_ant_det = 1;
				psd_scan->ant_det_result = 5;
				state = 99;
			} else {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"xxxxxxxxxxxxxxxx AntennaDetect(), Start Antenna Detection!! becaus background = %d <= thres (%d)\n",
					    psd_scan->psd_max_value / 100,
					BT_8723B_1ANT_ANTDET_PSDTHRES_BACKGROUND
					    + psd_scan->ant_det_thres_offset);
				BTC_TRACE(trace_buf);
				state = 3;
			}
			break;
		case 3:
			bt_resp = btcoexist->btc_set_bt_ant_detection(
					  btcoexist, (u8)(bt_tx_time & 0xff),
					  (u8)(bt_le_channel & 0xff));

			if (!halbtc8723b1ant_psd_sweep_point(btcoexist,
					     wlpsd_cent_freq, wlpsd_offset,
							     wlpsd_span,
					     BT_8723B_1ANT_ANTDET_PSD_POINTS,
					     BT_8723B_1ANT_ANTDET_PSD_AVGNUM,
						     wlpsd_sweep_count)) {
				board_info->btdm_ant_det_finish
					= false;
				board_info->btdm_ant_num_by_ant_det
					= 1;
				psd_scan->ant_det_result = 8;
				state = 99;
				break;
			}

			psd_scan->ant_det_psd_scan_peak_val =
				psd_scan->psd_max_value;
			psd_scan->ant_det_psd_scan_peak_freq =
				psd_scan->psd_max_value_point;
			state = 4;
			break;
		case 4:

			if (psd_scan->psd_point == 0)
				delta_freq_per_point = 0;
			else
				delta_freq_per_point =
					psd_scan->psd_band_width /
					psd_scan->psd_point;

			psd_rep1 = psd_scan->psd_max_value / 100;
			psd_rep2 = psd_scan->psd_max_value - psd_rep1 *
				   100;

			freq = ((psd_scan->real_cent_freq - 20) *
				1000000 + psd_scan->psd_max_value_point
				* delta_freq_per_point);
			freq1 = freq / 1000000;
			freq2 = freq / 1000 - freq1 * 1000;

			if (freq2 < 100) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"xxxxxxxxxxxxxxxx AntennaDetect(), Max Value: Freq = %d.0%d MHz",
					    freq1, freq2);
				BTC_TRACE(trace_buf);
				CL_SPRINTF(psd_scan->ant_det_peak_freq,
					   BT_8723B_1ANT_ANTDET_BUF_LEN,
					   "%d.0%d", freq1, freq2);
			} else {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"xxxxxxxxxxxxxxxx AntennaDetect(), Max Value: Freq = %d.%d MHz",
					    freq1, freq2);
				BTC_TRACE(trace_buf);
				CL_SPRINTF(psd_scan->ant_det_peak_freq,
					   BT_8723B_1ANT_ANTDET_BUF_LEN,
					   "%d.%d", freq1, freq2);
			}

			if (psd_rep2 < 10) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					    ", Value = %d.0%d dB\n",
					    psd_rep1, psd_rep2);
				BTC_TRACE(trace_buf);
				CL_SPRINTF(psd_scan->ant_det_peak_val,
					   BT_8723B_1ANT_ANTDET_BUF_LEN,
					   "%d.0%d", psd_rep1, psd_rep2);
			} else {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					    ", Value = %d.%d dB\n",
					    psd_rep1, psd_rep2);
				BTC_TRACE(trace_buf);
				CL_SPRINTF(psd_scan->ant_det_peak_val,
					   BT_8723B_1ANT_ANTDET_BUF_LEN,
					   "%d.%d", psd_rep1, psd_rep2);
			}

			psd_scan->ant_det_is_btreply_available = true;

			if (bt_resp == false) {
				psd_scan->ant_det_is_btreply_available =
					false;
				psd_scan->ant_det_result = 0;
				board_info->btdm_ant_det_finish = false;
				board_info->btdm_ant_num_by_ant_det = 1;
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"xxxxxxxxxxxxxxxx AntennaDetect(), BT Response = Fail\n ");
				BTC_TRACE(trace_buf);
			} else if (psd_scan->psd_max_value >
				(BT_8723B_1ANT_ANTDET_PSDTHRES_2ANT_BADISOLATION)
				   * 100) {
				psd_scan->ant_det_result = 1;
				board_info->btdm_ant_det_finish = true;
				board_info->btdm_ant_det_already_init_phydm =
					true;
				board_info->btdm_ant_num_by_ant_det = 2;
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"xxxxxxxxxxxxxxxx AntennaDetect(), Detect Result = 2-Ant, Bad-Isolation!!\n");
				BTC_TRACE(trace_buf);
			} else if (psd_scan->psd_max_value >
				(BT_8723B_1ANT_ANTDET_PSDTHRES_2ANT_GOODISOLATION
				    + psd_scan->ant_det_thres_offset) * 100) {
				psd_scan->ant_det_result = 2;
				board_info->btdm_ant_det_finish = true;
				board_info->btdm_ant_det_already_init_phydm =
					true;
				board_info->btdm_ant_num_by_ant_det = 2;
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"xxxxxxxxxxxxxxxx AntennaDetect(), Detect Result = 2-Ant, Good-Isolation!!\n");
				BTC_TRACE(trace_buf);
			} else if (psd_scan->psd_max_value >
				   (BT_8723B_1ANT_ANTDET_PSDTHRES_1ANT) *
				   100) {
				psd_scan->ant_det_result = 3;
				board_info->btdm_ant_det_finish = true;
				board_info->btdm_ant_det_already_init_phydm =
					true;
				board_info->btdm_ant_num_by_ant_det = 1;
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"xxxxxxxxxxxxxxxx AntennaDetect(), Detect Result = 1-Ant!!\n");
				BTC_TRACE(trace_buf);
			} else {
				psd_scan->ant_det_result = 4;
				board_info->btdm_ant_det_finish = false;
				board_info->btdm_ant_num_by_ant_det = 1;
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"xxxxxxxxxxxxxxxx AntennaDetect(), Detect Result = 1-Ant, un-certainity!!\n");
				BTC_TRACE(trace_buf);
			}

			state = 99;
			break;
		case 99:  /* restore setup */

			/* Set AFH mask off at WiFi channel 2472MHz +/- 10MHz */
			h2c_parameter[0] = 0x0;
			h2c_parameter[1] = 0x0;
			h2c_parameter[2] = 0x0;

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"xxxxxxxxxxxxxxxx AntennaDetect(), Set AFH on, Cent-Ch= %d,  Mask=%d\n",
				    h2c_parameter[1], h2c_parameter[2]);
			BTC_TRACE(trace_buf);

			btcoexist->btc_fill_h2c(btcoexist, 0x66, 3,
						h2c_parameter);

			/* Set Antenna Path					 */
			halbtc8723b1ant_set_ant_path(btcoexist,
				     BTC_ANT_PATH_PTA, FORCE_EXEC, false,
						     false);
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"xxxxxxxxxxxxxxxx AntennaDetect(), Set Antenna to PTA\n!!");
			BTC_TRACE(trace_buf);

			/* Resume Coex DM */
			btcoexist->stop_coex_dm = false;
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"xxxxxxxxxxxxxxxx AntennaDetect(), Resume Coex DM\n!!");
			BTC_TRACE(trace_buf);

			/* stimulate coex running */
			halbtc8723b1ant_run_coexist_mechanism(
				btcoexist);
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"xxxxxxxxxxxxxxxx AntennaDetect(), Stimulate Coex running\n!!");
			BTC_TRACE(trace_buf);

			outloop = true;
			break;
		}

	} while (!outloop);



}

void halbtc8723b1ant_psd_antenna_detection_check(IN struct btc_coexist
		*btcoexist)
{
	static u32 ant_det_count = 0, ant_det_fail_count = 0;
	struct  btc_board_info	*board_info = &btcoexist->board_info;

	boolean scan, roam;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);


	/* psd_scan->ant_det_bt_tx_time = 20; */
	psd_scan->ant_det_bt_tx_time =
		BT_8723B_1ANT_ANTDET_BTTXTIME;	   /* 0.42ms*50 = 20ms (0.42ms = 1 PSD sweep) */
	psd_scan->ant_det_bt_le_channel = BT_8723B_1ANT_ANTDET_BTTXCHANNEL;

	ant_det_count++;

	psd_scan->ant_det_try_count = ant_det_count;

	if (scan || roam) {
		board_info->btdm_ant_det_finish = false;
		psd_scan->ant_det_result = 6;
	} else if (coex_sta->bt_disabled) {
		board_info->btdm_ant_det_finish = false;
		psd_scan->ant_det_result = 11;
	} else if (coex_sta->num_of_profile >= 1) {
		board_info->btdm_ant_det_finish = false;
		psd_scan->ant_det_result = 7;
	} else if (
		!psd_scan->ant_det_is_ant_det_available) { /* Antenna initial setup is not ready */
		board_info->btdm_ant_det_finish = false;
		psd_scan->ant_det_result = 9;
	} else if (coex_sta->c2h_bt_inquiry_page) {
		board_info->btdm_ant_det_finish = false;
		psd_scan->ant_det_result = 10;
	} else
		halbtc8723b1ant_psd_antenna_detection(btcoexist,
					      psd_scan->ant_det_bt_tx_time,
					      psd_scan->ant_det_bt_le_channel);

	if (!board_info->btdm_ant_det_finish)
		ant_det_fail_count++;

	psd_scan->ant_det_fail_count = ant_det_fail_count;

}


/* ************************************************************
 * work around function start with wa_halbtc8723b1ant_
 * ************************************************************
 * ************************************************************
 * extern function start with ex_halbtc8723b1ant_
 * ************************************************************ */
void ex_halbtc8723b1ant_power_on_setting(IN struct btc_coexist *btcoexist)
{
	struct  btc_board_info	*board_info = &btcoexist->board_info;
	u8				u8tmp = 0x0;
	u16				u16tmp = 0x0;
	u32				value;


	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"xxxxxxxxxxxxxxxx Execute 8723b 1-Ant PowerOn Setting xxxxxxxxxxxxxxxx!!\n");
	BTC_TRACE(trace_buf);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "Ant Det Finish = %s, Ant Det Number  = %d\n",
		    (board_info->btdm_ant_det_finish ? "Yes" : "No"),
		    board_info->btdm_ant_num_by_ant_det);
	BTC_TRACE(trace_buf);


	btcoexist->stop_coex_dm = true;

	btcoexist->btc_write_1byte(btcoexist, 0x67, 0x20);

	/* enable BB, REG_SYS_FUNC_EN such that we can write 0x948 correctly. */
	u16tmp = btcoexist->btc_read_2byte(btcoexist, 0x2);
	btcoexist->btc_write_2byte(btcoexist, 0x2, u16tmp | BIT(0) | BIT(1));

	/* set GRAN_BT = 1 */
	btcoexist->btc_write_1byte(btcoexist, 0x765, 0x18);
	/* set WLAN_ACT = 0 */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0x4);

	/* */
	/* S0 or S1 setting and Local register setting(By the setting fw can get ant number, S0/S1, ... info) */
	/* Local setting bit define */
	/*	BIT0: "0" for no antenna inverse; "1" for antenna inverse  */
	/*	BIT1: "0" for internal switch; "1" for external switch */
	/*	BIT2: "0" for one antenna; "1" for two antenna */
	/* NOTE: here default all internal switch and 1-antenna ==> BIT1=0 and BIT2=0 */
	if (btcoexist->chip_interface == BTC_INTF_USB) {
		/* fixed at S0 for USB interface */
		btcoexist->btc_write_4byte(btcoexist, 0x948, 0x0);

		u8tmp |= 0x1;	/* antenna inverse */
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
			u8tmp |= 0x1;	/* antenna inverse */
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

void ex_halbtc8723b1ant_pre_load_firmware(IN struct btc_coexist *btcoexist)
{
}

void ex_halbtc8723b1ant_init_hw_config(IN struct btc_coexist *btcoexist,
				       IN boolean wifi_only)
{
	halbtc8723b1ant_init_hw_config(btcoexist, true, wifi_only);
	btcoexist->stop_coex_dm = false;
}

void ex_halbtc8723b1ant_init_coex_dm(IN struct btc_coexist *btcoexist)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], Coex Mechanism Init!!\n");
	BTC_TRACE(trace_buf);

	btcoexist->stop_coex_dm = false;

	halbtc8723b1ant_init_coex_dm(btcoexist);

	halbtc8723b1ant_query_bt_info(btcoexist);
}

void ex_halbtc8723b1ant_display_coex_info(IN struct btc_coexist *btcoexist)
{
	struct  btc_board_info	*board_info = &btcoexist->board_info;
	struct  btc_bt_link_info	*bt_link_info = &btcoexist->bt_link_info;
	u8				*cli_buf = btcoexist->cli_buf;
	u8				u8tmp[4], i, bt_info_ext, ps_tdma_case = 0;
	u16				u16tmp[4];
	u32				u32tmp[4];
	u32				fa_ofdm, fa_cck;
	u32				fw_ver = 0, bt_patch_ver = 0;
	static u8		pop_report_in_10s = 0;

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
	if (btcoexist->stop_coex_dm) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n ============[Coex is STOPPED]============");
		CL_PRINTF(cli_buf);
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n ==========================================");
		CL_PRINTF(cli_buf);
	}

	if (psd_scan->ant_det_try_count == 0) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d",
			   "Ant PG Num/ Mech/ Pos",
			   board_info->pg_ant_num, board_info->btdm_ant_num,
			   board_info->btdm_ant_pos);
		CL_PRINTF(cli_buf);
	} else {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %d/ %d/ %d  (%d/%d/%d)",
			   "Ant PG Num/ Mech(Ant_Det)/ Pos",
			   board_info->pg_ant_num,
			   board_info->btdm_ant_num_by_ant_det,
			   board_info->btdm_ant_pos,
			   psd_scan->ant_det_try_count,
			   psd_scan->ant_det_fail_count,
			   psd_scan->ant_det_result);
		CL_PRINTF(cli_buf);

		if (board_info->btdm_ant_det_finish) {
			CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s",
				   "Ant Det PSD Value",
				   psd_scan->ant_det_peak_val);
			CL_PRINTF(cli_buf);
		}
	}

	btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER, &bt_patch_ver);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %d_%x/ 0x%x/ 0x%x(%d)/ %c",
		   "Version Coex/ Fw/ Patch/ Cut",
		   glcoex_ver_date_8723b_1ant, glcoex_ver_8723b_1ant, fw_ver,
		   bt_patch_ver, bt_patch_ver, coex_sta->cut_version + 65);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x ",
		   "Wifi channel informed to BT",
		   coex_dm->wifi_chnl_info[0], coex_dm->wifi_chnl_info[1],
		   coex_dm->wifi_chnl_info[2]);
	CL_PRINTF(cli_buf);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s/ %s",
		   "WifibHiPri/ Ccklock/ CckEverLock",
		   (coex_sta->wifi_is_high_pri_task ? "Yes" : "No"),
		   (coex_sta->cck_lock ? "Yes" : "No"),
		   (coex_sta->cck_ever_lock ? "Yes" : "No"));
	CL_PRINTF(cli_buf);

	/* wifi status */
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
		   "============[Wifi Status]============");
	CL_PRINTF(cli_buf);
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_WIFI_STATUS);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
		   "============[BT Status]============");
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s",
		   "BT Abnormal scan",
		   (coex_sta->bt_abnormal_scan) ? "Yes" : "No");
	CL_PRINTF(cli_buf);

	pop_report_in_10s++;
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = [%s/ %d/ %d/ %d] ",
		   "BT [status/ rssi/ retryCnt/ popCnt]",
		   ((coex_sta->bt_disabled) ? ("disabled") :	((
		   coex_sta->c2h_bt_inquiry_page) ? ("inquiry/page scan")
			   : ((BT_8723B_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
			       coex_dm->bt_status) ? "non-connected idle" :
		((BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE == coex_dm->bt_status)
				       ? "connected-idle" : "busy")))),
		   coex_sta->bt_rssi, coex_sta->bt_retry_cnt,
		   coex_sta->pop_event_cnt);
	CL_PRINTF(cli_buf);

	if (pop_report_in_10s >= 5) {
		coex_sta->pop_event_cnt = 0;
		pop_report_in_10s = 0;
	}

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %d / %d / %d / %d / %d / %d",
		   "SCO/HID/PAN/A2DP/NameReq/WHQL",
		   bt_link_info->sco_exist, bt_link_info->hid_exist,
		   bt_link_info->pan_exist, bt_link_info->a2dp_exist,
		   coex_sta->c2h_bt_remote_name_req,
		   coex_sta->bt_whck_test);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s",
		   "BT Role",
		   (bt_link_info->slave_role) ? "Slave" : "Master");
	CL_PRINTF(cli_buf);

	bt_info_ext = coex_sta->bt_info_ext;
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d",
		   "A2DP Rate/Bitpool",
		(bt_info_ext & BIT(0)) ? "BR" : "EDR", coex_sta->a2dp_bit_pool);
	CL_PRINTF(cli_buf);

	for (i = 0; i < BT_INFO_SRC_8723B_1ANT_MAX; i++) {
		if (coex_sta->bt_info_c2h_cnt[i]) {
			CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
				"\r\n %-35s = %02x %02x %02x %02x %02x %02x %02x(%d)",
				   glbt_info_src_8723b_1ant[i],
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


	if (btcoexist->manual_control)
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
			"============[mechanisms] (before Manual)============");
	else
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
			   "============[mechanisms]============");
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d",
		   "SM[LowPenaltyRA]",
		   coex_dm->cur_low_penalty_ra);
	CL_PRINTF(cli_buf);

	ps_tdma_case = coex_dm->cur_ps_tdma;
	if (board_info->btdm_ant_num_by_ant_det == 2) {
		if (coex_dm->cur_ps_tdma_on)
			ps_tdma_case = ps_tdma_case +
				100; /* for WiFi RSSI low or BT RSSI low */
		else
			ps_tdma_case =
				1; /* always translate to TDMA(off,1) for TDMA-off case */
	}
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %02x %02x %02x %02x %02x case-%d (%s,%s)",
		   "PS TDMA",
		   coex_dm->ps_tdma_para[0], coex_dm->ps_tdma_para[1],
		   coex_dm->ps_tdma_para[2], coex_dm->ps_tdma_para[3],
		   coex_dm->ps_tdma_para[4], ps_tdma_case,
		   (coex_dm->cur_ps_tdma_on ? "On" : "Off"),
		   (coex_dm->auto_tdma_adjust ? "Adj" : "Fix"));

	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d",
		   "Coex Table Type",
		   coex_sta->coex_table_type);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d",
		   "IgnWlanAct",
		   coex_dm->cur_ignore_wlan_act);
	CL_PRINTF(cli_buf);

	/*
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x ", "Latest error condition(should be 0)",
		coex_dm->error_condition);
	CL_PRINTF(cli_buf);
	*/

	/* Hw setting		 */
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
		   "============[Hw setting]============");
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

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x778);
	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x6cc);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x880);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x778/0x6cc/0x880[29:25]",
		   u8tmp[0], u32tmp[0], (u32tmp[1] & 0x3e000000) >> 25);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x948);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x67);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x764);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x76e);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x",
		   "0x948/ 0x67[5] / 0x764 / 0x76e",
		   u32tmp[0], ((u8tmp[0] & 0x20) >> 5), (u32tmp[1] & 0xffff),
		   u8tmp[1]);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x92c);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x930);
	u32tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0x944);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x92c[1:0]/ 0x930[7:0]/0x944[1:0]",
		   u32tmp[0] & 0x3, u32tmp[1] & 0xff, u32tmp[2] & 0x3);
	CL_PRINTF(cli_buf);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x39);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x40);
	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x4c);
	u8tmp[2] = btcoexist->btc_read_1byte(btcoexist, 0x64);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x",
		   "0x38[11]/0x40/0x4c[24:23]/0x64[0]",
		   ((u8tmp[0] & 0x8) >> 3), u8tmp[1],
		   ((u32tmp[0] & 0x01800000) >> 23), u8tmp[2] & 0x1);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x550);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x522);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "0x550(bcn ctrl)/0x522",
		   u32tmp[0], u8tmp[0]);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xc50);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x49c);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "0xc50(dig)/0x49c(null-drop)",
		   u32tmp[0] & 0xff, u8tmp[0]);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xda0);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0xda4);
	u32tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0xda8);
	u32tmp[3] = btcoexist->btc_read_4byte(btcoexist, 0xcf0);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0xa5b);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0xa5c);

	fa_ofdm = ((u32tmp[0] & 0xffff0000) >> 16) + ((u32tmp[1] & 0xffff0000)
			>> 16) + (u32tmp[1] & 0xffff) + (u32tmp[2] & 0xffff) +
		  ((u32tmp[3] & 0xffff0000) >> 16) + (u32tmp[3] &
				  0xffff) ;
	fa_cck = (u8tmp[0] << 8) + u8tmp[1];

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "OFDM-CCA/OFDM-FA/CCK-FA",
		   u32tmp[0] & 0xffff, fa_ofdm, fa_cck);
	CL_PRINTF(cli_buf);


	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d",
		   "CRC_OK CCK/11g/11n/11n-Agg",
		   coex_sta->crc_ok_cck, coex_sta->crc_ok_11g,
		   coex_sta->crc_ok_11n, coex_sta->crc_ok_11n_agg);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d",
		   "CRC_Err CCK/11g/11n/11n-Agg",
		   coex_sta->crc_err_cck, coex_sta->crc_err_11g,
		   coex_sta->crc_err_11n, coex_sta->crc_err_11n_agg);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x6c0);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x6c4);
	u32tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0x6c8);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x6c0/0x6c4/0x6c8(coexTable)",
		   u32tmp[0], u32tmp[1], u32tmp[2]);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "0x770(high-pri rx/tx)",
		   coex_sta->high_priority_rx, coex_sta->high_priority_tx);
	CL_PRINTF(cli_buf);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "0x774(low-pri rx/tx)",
		   coex_sta->low_priority_rx, coex_sta->low_priority_tx);
	CL_PRINTF(cli_buf);
#if (BT_AUTO_REPORT_ONLY_8723B_1ANT == 1)
	/* halbtc8723b1ant_monitor_bt_ctr(btcoexist); */
#endif
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_COEX_STATISTICS);
}


void ex_halbtc8723b1ant_ips_notify(IN struct btc_coexist *btcoexist, IN u8 type)
{
	if (btcoexist->manual_control ||	btcoexist->stop_coex_dm)
		return;

	if (BTC_IPS_ENTER == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], IPS ENTER notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_ips = true;

		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_BT,
					     FORCE_EXEC, false, true);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
	} else if (BTC_IPS_LEAVE == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], IPS LEAVE notify\n");
		BTC_TRACE(trace_buf);

		halbtc8723b1ant_init_hw_config(btcoexist, false, false);
		halbtc8723b1ant_init_coex_dm(btcoexist);
		halbtc8723b1ant_query_bt_info(btcoexist);

		coex_sta->under_ips = false;
	}
}

void ex_halbtc8723b1ant_lps_notify(IN struct btc_coexist *btcoexist, IN u8 type)
{
	if (btcoexist->manual_control || btcoexist->stop_coex_dm)
		return;

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

void ex_halbtc8723b1ant_scan_notify(IN struct btc_coexist *btcoexist,
				    IN u8 type)
{
	boolean wifi_connected = false, bt_hs_on = false;
	u32	wifi_link_status = 0;
	u32	num_of_wifi_link = 0;
	boolean	bt_ctrl_agg_buf_size = false;
	u8	agg_buf_size = 5;

	u8 u8tmpa, u8tmpb;
	u32 u32tmp;

	if (btcoexist->manual_control ||
	    btcoexist->stop_coex_dm)
		return;

	if (BTC_SCAN_START == type) {
		coex_sta->wifi_is_high_pri_task = true;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], SCAN START notify\n");
		BTC_TRACE(trace_buf);
		psd_scan->ant_det_is_ant_det_available = true;
		halbtc8723b1ant_ps_tdma(btcoexist, FORCE_EXEC, false,
			8);  /* Force antenna setup for no scan result issue */
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA,
					     FORCE_EXEC, false, false);
		u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x948);
		u8tmpa = btcoexist->btc_read_1byte(btcoexist, 0x765);
		u8tmpb = btcoexist->btc_read_1byte(btcoexist, 0x67);


		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], 0x948=0x%x, 0x765=0x%x, 0x67=0x%x\n",
			    u32tmp,  u8tmpa, u8tmpb);
		BTC_TRACE(trace_buf);
	} else {
		coex_sta->wifi_is_high_pri_task = false;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], SCAN FINISH notify\n");
		BTC_TRACE(trace_buf);

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
		halbtc8723b1ant_action_wifi_multi_port(btcoexist);
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
		if (!wifi_connected)	/* non-connected scan */
			halbtc8723b1ant_action_wifi_not_connected_scan(
				btcoexist);
		else	/* wifi is connected */
			halbtc8723b1ant_action_wifi_connected_scan(btcoexist);
	} else if (BTC_SCAN_FINISH == type) {
		if (!wifi_connected)	/* non-connected scan */
			halbtc8723b1ant_action_wifi_not_connected(btcoexist);
		else
			halbtc8723b1ant_action_wifi_connected(btcoexist);
	}
}

void ex_halbtc8723b1ant_connect_notify(IN struct btc_coexist *btcoexist,
				       IN u8 type)
{
	boolean	wifi_connected = false, bt_hs_on = false;
	u32	wifi_link_status = 0;
	u32	num_of_wifi_link = 0;
	boolean	bt_ctrl_agg_buf_size = false;
	u8	agg_buf_size = 5;

	if (btcoexist->manual_control ||
	    btcoexist->stop_coex_dm ||
	    coex_sta->bt_disabled)
		return;

	if (BTC_ASSOCIATE_START == type) {
		coex_sta->wifi_is_high_pri_task = true;
		psd_scan->ant_det_is_ant_det_available = true;
		halbtc8723b1ant_ps_tdma(btcoexist, FORCE_EXEC, false,
			8);  /* Force antenna setup for no scan result issue */
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA,
					     FORCE_EXEC, false, false);
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], CONNECT START notify\n");
		BTC_TRACE(trace_buf);
		coex_dm->arp_cnt = 0;
	} else {
		coex_sta->wifi_is_high_pri_task = false;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], CONNECT FINISH notify\n");
		BTC_TRACE(trace_buf);
		/* coex_dm->arp_cnt = 0; */
	}

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
			   &wifi_link_status);
	num_of_wifi_link = wifi_link_status >> 16;
	if (num_of_wifi_link >= 2) {
		halbtc8723b1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);
		halbtc8723b1ant_limited_rx(btcoexist, NORMAL_EXEC, false,
					   bt_ctrl_agg_buf_size, agg_buf_size);
		halbtc8723b1ant_action_wifi_multi_port(btcoexist);
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

	if (BTC_ASSOCIATE_START == type)
		halbtc8723b1ant_action_wifi_not_connected_asso_auth(btcoexist);
	else if (BTC_ASSOCIATE_FINISH == type) {
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
				   &wifi_connected);
		if (!wifi_connected) /* non-connected scan */
			halbtc8723b1ant_action_wifi_not_connected(btcoexist);
		else
			halbtc8723b1ant_action_wifi_connected(btcoexist);
	}
}

void ex_halbtc8723b1ant_media_status_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	u8			h2c_parameter[3] = {0};
	u32			wifi_bw;
	u8			wifi_central_chnl;
	boolean			wifi_under_b_mode = false;

	if (btcoexist->manual_control ||
	    btcoexist->stop_coex_dm ||
	    coex_sta->bt_disabled)
		return;

	if (BTC_MEDIA_CONNECT == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], MEDIA connect notify\n");
		BTC_TRACE(trace_buf);
		halbtc8723b1ant_ps_tdma(btcoexist, FORCE_EXEC, false,
			8);  /* Force antenna setup for no scan result issue */
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_PTA,
					     FORCE_EXEC, false, false);
		psd_scan->ant_det_is_ant_det_available = true;
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

		coex_dm->backup_arfr_cnt1 = btcoexist->btc_read_4byte(btcoexist,
					    0x430);
		coex_dm->backup_arfr_cnt2 = btcoexist->btc_read_4byte(btcoexist,
					    0x434);
		coex_dm->backup_retry_limit = btcoexist->btc_read_2byte(
						      btcoexist, 0x42a);
		coex_dm->backup_ampdu_max_time = btcoexist->btc_read_1byte(
				btcoexist, 0x456);
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], MEDIA disconnect notify\n");
		BTC_TRACE(trace_buf);
		coex_dm->arp_cnt = 0;

		btcoexist->btc_write_1byte(btcoexist, 0x6cd, 0x0); /* CCK Tx */
		btcoexist->btc_write_1byte(btcoexist, 0x6cf, 0x0); /* CCK Rx */

		coex_sta->cck_ever_lock = false;
	}

	/* only 2.4G we need to inform bt the chnl mask */
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_CENTRAL_CHNL,
			   &wifi_central_chnl);
	if ((BTC_MEDIA_CONNECT == type) &&
	    (wifi_central_chnl <= 14)) {
		/* h2c_parameter[0] = 0x1; */
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

	btcoexist->btc_fill_h2c(btcoexist, 0x66, 3, h2c_parameter);
}

void ex_halbtc8723b1ant_specific_packet_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	boolean	bt_hs_on = false;
	u32	wifi_link_status = 0;
	u32	num_of_wifi_link = 0;
	boolean	bt_ctrl_agg_buf_size = false, under_4way = false;
	u8	agg_buf_size = 5;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS,
			   &under_4way);

	if (btcoexist->manual_control ||
	    btcoexist->stop_coex_dm ||
	    coex_sta->bt_disabled)
		return;

	if (BTC_PACKET_DHCP == type ||
	    BTC_PACKET_EAPOL == type ||
	    BTC_PACKET_ARP == type) {
		if (BTC_PACKET_ARP == type) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], specific Packet ARP notify\n");
			BTC_TRACE(trace_buf);

			coex_dm->arp_cnt++;
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], ARP Packet Count = %d\n",
				    coex_dm->arp_cnt);
			BTC_TRACE(trace_buf);

			if ((coex_dm->arp_cnt >= 10) &&
			    (!under_4way))  /* if APR PKT > 10 after connect, do not go to ActionWifiConnectedSpecificPacket(btcoexist) */
				coex_sta->wifi_is_high_pri_task = false;
			else
				coex_sta->wifi_is_high_pri_task = true;
		} else {
			coex_sta->wifi_is_high_pri_task = true;
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], specific Packet DHCP or EAPOL notify\n");
			BTC_TRACE(trace_buf);
		}
	} else {
		coex_sta->wifi_is_high_pri_task = false;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], specific Packet [Type = %d] notify\n", type);
		BTC_TRACE(trace_buf);
	}

	coex_sta->specific_pkt_period_cnt = 0;

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
			   &wifi_link_status);
	num_of_wifi_link = wifi_link_status >> 16;
	if (num_of_wifi_link >= 2) {
		halbtc8723b1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);
		halbtc8723b1ant_limited_rx(btcoexist, NORMAL_EXEC, false,
					   bt_ctrl_agg_buf_size, agg_buf_size);
		halbtc8723b1ant_action_wifi_multi_port(btcoexist);
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

	if (BTC_PACKET_DHCP == type ||
	    BTC_PACKET_EAPOL == type ||
	    ((BTC_PACKET_ARP == type) && (coex_sta->wifi_is_high_pri_task)))
		halbtc8723b1ant_action_wifi_connected_specific_packet(
			btcoexist);
}

void ex_halbtc8723b1ant_bt_info_notify(IN struct btc_coexist *btcoexist,
				       IN u8 *tmp_buf, IN u8 length)
{
	u8				bt_info = 0;
	u8				i, rsp_source = 0;
	boolean				wifi_connected = false;
	boolean				bt_busy = false;
	struct  btc_board_info	*board_info = &btcoexist->board_info;

	coex_sta->c2h_bt_info_req_sent = false;

	rsp_source = tmp_buf[0] & 0xf;
	if (rsp_source >= BT_INFO_SRC_8723B_1ANT_MAX)
		rsp_source = BT_INFO_SRC_8723B_1ANT_WIFI_FW;
	coex_sta->bt_info_c2h_cnt[rsp_source]++;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], Bt info[%d], length=%d, hex data=[", rsp_source,
		    length);
	BTC_TRACE(trace_buf);
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

	/* if 0xff, it means BT is under WHCK test */
	if (bt_info == 0xff)
		coex_sta->bt_whck_test = true;
	else
		coex_sta->bt_whck_test = false;

	if (BT_INFO_SRC_8723B_1ANT_WIFI_FW != rsp_source) {
		coex_sta->bt_retry_cnt =	/* [3:0] */
			coex_sta->bt_info_c2h[rsp_source][2] & 0xf;

		if (coex_sta->bt_retry_cnt >= 1)
			coex_sta->pop_event_cnt++;

		if (coex_sta->bt_info_c2h[rsp_source][2] & 0x20)
			coex_sta->c2h_bt_remote_name_req = true;
		else
			coex_sta->c2h_bt_remote_name_req = false;

		coex_sta->bt_rssi =
			coex_sta->bt_info_c2h[rsp_source][3] * 2 - 90;
		/* coex_sta->bt_info_c2h[rsp_source][3]*2+10; */

		coex_sta->bt_info_ext =
			coex_sta->bt_info_c2h[rsp_source][4];

		if (coex_sta->bt_info_c2h[rsp_source][1] == 0x49) {
			coex_sta->a2dp_bit_pool =
				coex_sta->bt_info_c2h[rsp_source][6];
		} else
			coex_sta->a2dp_bit_pool = 0;

		coex_sta->bt_tx_rx_mask = (coex_sta->bt_info_c2h[rsp_source][2]
					   & 0x40);
		btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TX_RX_MASK,
				   &coex_sta->bt_tx_rx_mask);

#if BT_8723B_1ANT_ANTDET_ENABLE
#if BT_8723B_1ANT_ANTDET_COEXMECHANISMSWITCH_ENABLE
		if ((board_info->btdm_ant_det_finish) &&
		    (board_info->btdm_ant_num_by_ant_det == 2)) {
			if (coex_sta->bt_tx_rx_mask) {
				/* BT into is responded by BT FW and BT RF REG 0x3C != 0x15 => Need to switch BT TRx Mask */
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], Switch BT TRx Mask since BT RF REG 0x3C != 0x1\n");
				BTC_TRACE(trace_buf);

				/* BT TRx Mask un-lock 0x2c[0], 0x30[0] = 1 */
				btcoexist->btc_set_bt_reg(btcoexist,
						  BTC_BT_REG_RF, 0x2c, 0x7c45);
				btcoexist->btc_set_bt_reg(btcoexist,
						  BTC_BT_REG_RF, 0x30, 0x7c45);

				btcoexist->btc_set_bt_reg(btcoexist,
						  BTC_BT_REG_RF, 0x3c, 0x1);
			}
		} else
#endif
#endif

		{
			if (!coex_sta->bt_tx_rx_mask) {
				/* BT into is responded by BT FW and BT RF REG 0x3C != 0x15 => Need to switch BT TRx Mask */
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], Switch BT TRx Mask since BT RF REG 0x3C != 0x15\n");
				BTC_TRACE(trace_buf);
				btcoexist->btc_set_bt_reg(btcoexist,
							  BTC_BT_REG_RF,
							  0x3c, 0x15);

				/* BT TRx Mask lock 0x2c[0], 0x30[0] = 0 */
				btcoexist->btc_set_bt_reg(btcoexist,
							  BTC_BT_REG_RF,
							  0x2c, 0x7c44);
				btcoexist->btc_set_bt_reg(btcoexist,
							  BTC_BT_REG_RF,
							  0x30, 0x7c44);
			}
		}

		/* Here we need to resend some wifi info to BT */
		/* because bt is reset and loss of the info. */
		if (coex_sta->bt_info_ext & BIT(1)) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], BT ext info bit1 check, send wifi BW&Chnl to BT!!\n");
			BTC_TRACE(trace_buf);
			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
					   &wifi_connected);
			if (wifi_connected)
				ex_halbtc8723b1ant_media_status_notify(
					btcoexist, BTC_MEDIA_CONNECT);
			else
				ex_halbtc8723b1ant_media_status_notify(
					btcoexist, BTC_MEDIA_DISCONNECT);
		}

		if (coex_sta->bt_info_ext & BIT(3)) {
			if (!btcoexist->manual_control &&
			    !btcoexist->stop_coex_dm) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT ext info bit3 check, set BT NOT to ignore Wlan active!!\n");
				BTC_TRACE(trace_buf);
				halbtc8723b1ant_ignore_wlan_act(btcoexist,
							FORCE_EXEC, false);
			}
		} else {
			/* BT already NOT ignore Wlan active, do nothing here. */
		}
#if (BT_AUTO_REPORT_ONLY_8723B_1ANT == 0)
		if ((coex_sta->bt_info_ext & BIT(4))) {
			/* BT auto report already enabled, do nothing */
		} else
			halbtc8723b1ant_bt_auto_report(btcoexist, FORCE_EXEC,
						       true);
#endif
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
	} else {	/* connection exists */
		coex_sta->bt_link_exist = true;
		if (bt_info & BT_INFO_8723B_1ANT_B_FTP) {
			coex_sta->pan_exist = true;
			coex_sta->num_of_profile++;
		} else
			coex_sta->pan_exist = false;
		if (bt_info & BT_INFO_8723B_1ANT_B_A2DP) {
			coex_sta->a2dp_exist = true;
			coex_sta->num_of_profile++;
		} else
			coex_sta->a2dp_exist = false;
		if (bt_info & BT_INFO_8723B_1ANT_B_HID) {
			coex_sta->hid_exist = true;
			coex_sta->num_of_profile++;
		} else
			coex_sta->hid_exist = false;
		if (bt_info & BT_INFO_8723B_1ANT_B_SCO_ESCO) {
			coex_sta->sco_exist = true;
			coex_sta->num_of_profile++;
		} else
			coex_sta->sco_exist = false;

		if ((coex_sta->hid_exist == false) &&
		    (coex_sta->c2h_bt_inquiry_page == false) &&
		    (coex_sta->sco_exist == false)) {
			if (coex_sta->high_priority_tx  +
			    coex_sta->high_priority_rx >= 160) {
				coex_sta->hid_exist = true;
				coex_sta->wrong_profile_notification++;
				coex_sta->num_of_profile++;
				bt_info = bt_info | 0x28;
			}
		}

		/* Add Hi-Pri Tx/Rx counter to avoid false detection */
		if (((coex_sta->hid_exist) || (coex_sta->sco_exist)) &&
		    (coex_sta->high_priority_tx  +
		     coex_sta->high_priority_rx >= 160)
		    && (!coex_sta->c2h_bt_inquiry_page))
			coex_sta->bt_hi_pri_link_exist = true;

		if ((bt_info & BT_INFO_8723B_1ANT_B_ACL_BUSY) &&
		    (coex_sta->num_of_profile == 0)) {
			if (coex_sta->low_priority_tx  +
			    coex_sta->low_priority_rx >= 160) {
				coex_sta->pan_exist = true;
				coex_sta->num_of_profile++;
				coex_sta->wrong_profile_notification++;
				bt_info = bt_info | 0x88;
			}
		}
	}

	halbtc8723b1ant_update_bt_link_info(btcoexist);

	bt_info = bt_info &
		0x1f;  /* mask profile bit for connect-ilde identification ( for CSR case: A2DP idle --> 0x41) */

	if (!(bt_info & BT_INFO_8723B_1ANT_B_CONNECTION))
		coex_dm->bt_status = BT_8723B_1ANT_BT_STATUS_NON_CONNECTED_IDLE;
	else if (bt_info ==
		BT_INFO_8723B_1ANT_B_CONNECTION)	/* connection exists but no busy */
		coex_dm->bt_status = BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE;
	else if ((bt_info & BT_INFO_8723B_1ANT_B_SCO_ESCO) ||
		 (bt_info & BT_INFO_8723B_1ANT_B_SCO_BUSY))
		coex_dm->bt_status = BT_8723B_1ANT_BT_STATUS_SCO_BUSY;
	else if (bt_info & BT_INFO_8723B_1ANT_B_ACL_BUSY) {
		if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY != coex_dm->bt_status)
			coex_dm->auto_tdma_adjust = false;
		coex_dm->bt_status = BT_8723B_1ANT_BT_STATUS_ACL_BUSY;
	} else
		coex_dm->bt_status = BT_8723B_1ANT_BT_STATUS_MAX;

	if ((BT_8723B_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) ||
	    (BT_8723B_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
	    (BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY == coex_dm->bt_status))
		bt_busy = true;
	else
		bt_busy = false;
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bt_busy);

	halbtc8723b1ant_run_coexist_mechanism(btcoexist);
}

void ex_halbtc8723b1ant_rf_status_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	u32	u32tmp;
	u8	u8tmpa, u8tmpb, u8tmpc;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], RF Status notify\n");
	BTC_TRACE(trace_buf);

	if (BTC_RF_ON == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RF is turned ON!!\n");
		BTC_TRACE(trace_buf);
		btcoexist->stop_coex_dm = false;
	} else if (BTC_RF_OFF == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RF is turned OFF!!\n");
		BTC_TRACE(trace_buf);

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


		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"############# [BTCoex], 0x948=0x%x, 0x765=0x%x, 0x67=0x%x, 0x76e=0x%x\n",
			    u32tmp,  u8tmpa, u8tmpb, u8tmpc);
		BTC_TRACE(trace_buf);

	}
}

void ex_halbtc8723b1ant_halt_notify(IN struct btc_coexist *btcoexist)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Halt notify\n");
	BTC_TRACE(trace_buf);

	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0,
					 0x0);
	halbtc8723b1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 0);
	halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_BT, FORCE_EXEC,
				     false, true);

	halbtc8723b1ant_ignore_wlan_act(btcoexist, FORCE_EXEC, true);

	ex_halbtc8723b1ant_media_status_notify(btcoexist, BTC_MEDIA_DISCONNECT);

	btcoexist->stop_coex_dm = true;
}

void ex_halbtc8723b1ant_pnp_notify(IN struct btc_coexist *btcoexist,
				   IN u8 pnp_state)
{
	if (BTC_WIFI_PNP_SLEEP == pnp_state) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Pnp notify to SLEEP\n");
		BTC_TRACE(trace_buf);

		halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);
		halbtc8723b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_BT,
					     FORCE_EXEC, false, true);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);

		/* Sinda 20150819, workaround for driver skip leave IPS/LPS to speed up sleep time. */
		/* Driver do not leave IPS/LPS when driver is going to sleep, so BTCoexistence think wifi is still under IPS/LPS */
		/* BT should clear UnderIPS/UnderLPS state to avoid mismatch state after wakeup. */
		coex_sta->under_ips = false;
		coex_sta->under_lps = false;
		btcoexist->stop_coex_dm = true;
	} else if (BTC_WIFI_PNP_WAKE_UP == pnp_state) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Pnp notify to WAKE UP\n");
		BTC_TRACE(trace_buf);
		btcoexist->stop_coex_dm = false;
		halbtc8723b1ant_init_hw_config(btcoexist, false, false);
		halbtc8723b1ant_init_coex_dm(btcoexist);
		halbtc8723b1ant_query_bt_info(btcoexist);
	}
}

void ex_halbtc8723b1ant_coex_dm_reset(IN struct btc_coexist *btcoexist)
{

	halbtc8723b1ant_init_hw_config(btcoexist, false, false);
	/* btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0); */
	/* btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x2, 0xfffff, 0x0); */
	halbtc8723b1ant_init_coex_dm(btcoexist);
}

void ex_halbtc8723b1ant_periodical(IN struct btc_coexist *btcoexist)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], ==========================Periodical===========================\n");
	BTC_TRACE(trace_buf);

#if (BT_AUTO_REPORT_ONLY_8723B_1ANT == 0)
	halbtc8723b1ant_query_bt_info(btcoexist);
#endif
	halbtc8723b1ant_monitor_bt_ctr(btcoexist);
	halbtc8723b1ant_monitor_wifi_ctr(btcoexist);

	halbtc8723b1ant_monitor_bt_enable_disable(btcoexist);


	if (halbtc8723b1ant_is_wifi_status_changed(btcoexist) ||
	    coex_dm->auto_tdma_adjust)
		halbtc8723b1ant_run_coexist_mechanism(btcoexist);

	coex_sta->specific_pkt_period_cnt++;

	/* sample to set bt to execute Ant detection */
	/* btcoexist->btc_set_bt_ant_detection(btcoexist, 20, 14);
	*
	if (psd_scan->is_ant_det_enable)
	{
		 if (psd_scan->psd_gen_count > psd_scan->realseconds)
			psd_scan->psd_gen_count = 0;

		halbtc8723b1ant_antenna_detection(btcoexist, psd_scan->realcent_freq,  psd_scan->realoffset, psd_scan->realspan,  psd_scan->realseconds);
		psd_scan->psd_gen_total_count +=2;
		psd_scan->psd_gen_count += 2;
	}
	*/
}

void ex_halbtc8723b1ant_antenna_detection(IN struct btc_coexist *btcoexist,
		IN u32 cent_freq, IN u32 offset, IN u32 span, IN u32 seconds)
{
#if BT_8723B_1ANT_ANTDET_ENABLE
	static u32 ant_det_count = 0, ant_det_fail_count = 0;
	struct  btc_board_info	*board_info = &btcoexist->board_info;
	/*boolean scan, roam;*/

	if (seconds == 0) {
		psd_scan->ant_det_try_count	= 0;
		psd_scan->ant_det_fail_count	= 0;
		ant_det_count = 0;
		ant_det_fail_count = 0;
		board_info->btdm_ant_det_finish = false;
		board_info->btdm_ant_num_by_ant_det = 1;
		return;
	}

	if (!board_info->btdm_ant_det_finish) {
		psd_scan->ant_det_inteval_count =
			psd_scan->ant_det_inteval_count + 2;

		if (psd_scan->ant_det_inteval_count >=
		    BT_8723B_1ANT_ANTDET_RETRY_INTERVAL) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"xxxxxxxxxxxxxxxx AntennaDetect(), Antenna Det Timer is up, Try Detect!!\n");
			BTC_TRACE(trace_buf);
			halbtc8723b1ant_psd_antenna_detection_check(btcoexist);

			if (board_info->btdm_ant_det_finish) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"xxxxxxxxxxxxxxxx AntennaDetect(), Antenna Det Success!!\n");
				BTC_TRACE(trace_buf);
#if 1
				if (board_info->btdm_ant_num_by_ant_det == 2)
					halbtc8723b1ant_mechanism_switch(
						btcoexist, true);
				else
					halbtc8723b1ant_mechanism_switch(
						btcoexist, false);
#endif
			} else {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"xxxxxxxxxxxxxxxx AntennaDetect(), Antenna Det Fail!!\n");
				BTC_TRACE(trace_buf);
			}
			psd_scan->ant_det_inteval_count = 0;
		} else {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"xxxxxxxxxxxxxxxx AntennaDetect(), Antenna Det Timer is not up! (%d)\n",
				    psd_scan->ant_det_inteval_count);
			BTC_TRACE(trace_buf);
		}

	}
#endif


	/*
			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);


			psd_scan->ant_det_bt_tx_time = seconds;
		psd_scan->ant_det_bt_le_channel = cent_freq;

		if (seconds == 0)
			{
			psd_scan->ant_det_try_count	= 0;
			psd_scan->ant_det_fail_count	= 0;
			ant_det_count = 0;
			ant_det_fail_count = 0;
			board_info->btdm_ant_det_finish = false;
			board_info->btdm_ant_num_by_ant_det = 1;
				 return;
		}
		else
		{
			ant_det_count++;

			psd_scan->ant_det_try_count = ant_det_count;

				if (scan ||roam)
			{
				board_info->btdm_ant_det_finish = false;
				psd_scan->ant_det_result = 6;
			}
			else if (coex_sta->num_of_profile >= 1)
			{
				board_info->btdm_ant_det_finish = false;
				psd_scan->ant_det_result = 7;
			}
				else if (!psd_scan->ant_det_is_ant_det_available)
			{
				board_info->btdm_ant_det_finish = false;
				psd_scan->ant_det_result = 9;
			}
			else if (coex_sta->c2h_bt_inquiry_page)
			{
				board_info->btdm_ant_det_finish = false;
				psd_scan->ant_det_result = 10;
			}
			else
			{

		}

			if (!board_info->btdm_ant_det_finish)
				ant_det_fail_count++;

			psd_scan->ant_det_fail_count = ant_det_fail_count;
		}
	*/
}


void ex_halbtc8723b1ant_display_ant_detection(IN struct btc_coexist *btcoexist)
{
#if BT_8723B_1ANT_ANTDET_ENABLE
	struct  btc_board_info	*board_info = &btcoexist->board_info;

	if (psd_scan->ant_det_try_count != 0) {
		halbtc8723b1ant_psd_show_antenna_detect_result(btcoexist);

		if (board_info->btdm_ant_det_finish)
			halbtc8723b1ant_psd_showdata(btcoexist);
		return;
	}
#endif

	/* halbtc8723b1ant_show_psd_data(btcoexist); */
}

#endif

#endif	/* #if (BT_SUPPORT == 1 && COEX_SUPPORT == 1) */


