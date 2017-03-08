/* ************************************************************
 * Description:
 *
 * This file is for RTL8822B Co-exist mechanism
 *
 * History
 * 2012/11/15 Cosa first check in.
 *
 * ************************************************************ */

/* ************************************************************
 * include files
 * ************************************************************ */
/*only for rf4ce*/
#include "Mp_Precomp.h"

#if (BT_SUPPORT == 1 && COEX_SUPPORT == 1)

#if (RTL8822B_SUPPORT == 1)
/* ************************************************************
 * Global variables, these are static variables
 * ************************************************************ */
static u8	 *trace_buf = &gl_btc_trace_buf[0];
static struct  coex_dm_8822b_1ant		glcoex_dm_8822b_1ant;
static struct  coex_dm_8822b_1ant	*coex_dm = &glcoex_dm_8822b_1ant;
static struct  coex_sta_8822b_1ant		glcoex_sta_8822b_1ant;
static struct  coex_sta_8822b_1ant	*coex_sta = &glcoex_sta_8822b_1ant;
static struct  psdscan_sta_8822b_1ant	gl_psd_scan_8822b_1ant;
static struct  psdscan_sta_8822b_1ant *psd_scan = &gl_psd_scan_8822b_1ant;
static struct	rfe_type_8822b_1ant		gl_rfe_type_8822b_1ant;
static struct	rfe_type_8822b_1ant		*rfe_type = &gl_rfe_type_8822b_1ant;



const char *const glbt_info_src_8822b_1ant[] = {
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

u32	glcoex_ver_date_8822b_1ant = 20170113;
u32	glcoex_ver_8822b_1ant = 0x41;
u32	glcoex_ver_btdesired_8822b_1ant = 0x41;


/* ************************************************************
 * local function proto type if needed
 * ************************************************************
 * ************************************************************
 * local function start with halbtc8822b1ant_
 * ************************************************************ */
u8 halbtc8822b1ant_bt_rssi_state(u8 level_num, u8 rssi_thresh, u8 rssi_thresh1)
{
	s32			bt_rssi = 0;
	u8			bt_rssi_state = coex_sta->pre_bt_rssi_state;

	bt_rssi = coex_sta->bt_rssi;

	if (level_num == 2) {
		if ((coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_bt_rssi_state ==
		     BTC_RSSI_STATE_STAY_LOW)) {
			if (bt_rssi >= (rssi_thresh +
					BTC_RSSI_COEX_THRESH_TOL_8822B_1ANT))
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
					BTC_RSSI_COEX_THRESH_TOL_8822B_1ANT))
				bt_rssi_state = BTC_RSSI_STATE_MEDIUM;
			else
				bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
		} else if ((coex_sta->pre_bt_rssi_state ==
			    BTC_RSSI_STATE_MEDIUM) ||
			   (coex_sta->pre_bt_rssi_state ==
			    BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (bt_rssi >= (rssi_thresh1 +
					BTC_RSSI_COEX_THRESH_TOL_8822B_1ANT))
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

u8 halbtc8822b1ant_wifi_rssi_state(IN struct btc_coexist *btcoexist,
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
					  BTC_RSSI_COEX_THRESH_TOL_8822B_1ANT))
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
					  BTC_RSSI_COEX_THRESH_TOL_8822B_1ANT))
				wifi_rssi_state = BTC_RSSI_STATE_MEDIUM;
			else
				wifi_rssi_state = BTC_RSSI_STATE_STAY_LOW;
		} else if ((coex_sta->pre_wifi_rssi_state[index] ==
			    BTC_RSSI_STATE_MEDIUM) ||
			   (coex_sta->pre_wifi_rssi_state[index] ==
			    BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (wifi_rssi >= (rssi_thresh1 +
					  BTC_RSSI_COEX_THRESH_TOL_8822B_1ANT))
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

void halbtc8822b1ant_update_ra_mask(IN struct btc_coexist *btcoexist,
				    IN boolean force_exec, IN u32 dis_rate_mask)
{
	coex_dm->cur_ra_mask = dis_rate_mask;

	if (force_exec || (coex_dm->pre_ra_mask != coex_dm->cur_ra_mask))
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_UPDATE_RAMASK,
				   &coex_dm->cur_ra_mask);
	coex_dm->pre_ra_mask = coex_dm->cur_ra_mask;
}

void halbtc8822b1ant_auto_rate_fallback_retry(IN struct btc_coexist *btcoexist,
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

void halbtc8822b1ant_retry_limit(IN struct btc_coexist *btcoexist,
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

void halbtc8822b1ant_ampdu_max_time(IN struct btc_coexist *btcoexist,
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

void halbtc8822b1ant_limited_tx(IN struct btc_coexist *btcoexist,
		IN boolean force_exec, IN u8 ra_mask_type, IN u8 arfr_type,
				IN u8 retry_limit_type, IN u8 ampdu_time_type)
{
	switch (ra_mask_type) {
	case 0:	/* normal mode */
		halbtc8822b1ant_update_ra_mask(btcoexist, force_exec,
					       0x0);
		break;
	case 1:	/* disable cck 1/2 */
		halbtc8822b1ant_update_ra_mask(btcoexist, force_exec,
					       0x00000003);
		break;
	case 2:	/* disable cck 1/2/5.5, ofdm 6/9/12/18/24, mcs 0/1/2/3/4 */
		halbtc8822b1ant_update_ra_mask(btcoexist, force_exec,
					       0x0001f1f7);
		break;
	default:
		break;
	}

	halbtc8822b1ant_auto_rate_fallback_retry(btcoexist, force_exec,
			arfr_type);
	halbtc8822b1ant_retry_limit(btcoexist, force_exec, retry_limit_type);
	halbtc8822b1ant_ampdu_max_time(btcoexist, force_exec, ampdu_time_type);
}

/*
rx agg size setting :
1:      true / don't care / don't care
max: false / false / don't care
7:     false / true / 7
*/

void halbtc8822b1ant_limited_rx(IN struct btc_coexist *btcoexist,
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

void halbtc8822b1ant_query_bt_info(IN struct btc_coexist *btcoexist)
{
	u8			h2c_parameter[1] = {0};

	coex_sta->c2h_bt_info_req_sent = true;

	h2c_parameter[0] |= BIT(0);	/* trigger */

	btcoexist->btc_fill_h2c(btcoexist, 0x61, 1, h2c_parameter);
}

void halbtc8822b1ant_monitor_bt_ctr(IN struct btc_coexist *btcoexist)
{
	u32			reg_hp_txrx, reg_lp_txrx, u32tmp;
	u32			reg_hp_tx = 0, reg_hp_rx = 0, reg_lp_tx = 0, reg_lp_rx = 0;
	static u8		num_of_bt_counter_chk = 0, cnt_slave = 0;
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	/* to avoid 0x76e[3] = 1 (WLAN_Act control by PTA) during IPS */
	/* if (! (btcoexist->btc_read_1byte(btcoexist, 0x76e) & 0x8) ) */

	if (coex_sta->under_ips) {
		/* coex_sta->high_priority_tx = 65535; */
		/* coex_sta->high_priority_rx = 65535; */
		/* coex_sta->low_priority_tx = 65535; */
		/* coex_sta->low_priority_rx = 65535; */
		/* return; */
	}

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

	if ((coex_sta->low_priority_tx > 1150)  &&
	    (!coex_sta->c2h_bt_inquiry_page))
		coex_sta->pop_event_cnt++;

	if ((coex_sta->low_priority_rx >= 1150) &&
	    (coex_sta->low_priority_rx >= coex_sta->low_priority_tx)
	    && (!coex_sta->under_ips)  &&
	    (!coex_sta->c2h_bt_inquiry_page) &&
	    (coex_sta->bt_link_exist))	{
		if (cnt_slave >= 3) {
			bt_link_info->slave_role = true;
			cnt_slave = 3;
		} else
			cnt_slave++;
	} else {
		if (cnt_slave == 0)	{
			bt_link_info->slave_role = false;
			cnt_slave = 0;
		} else
			cnt_slave--;

	}

	if ((coex_sta->high_priority_tx == 0) &&
	    (coex_sta->high_priority_rx == 0) &&
	    (coex_sta->low_priority_tx == 0) &&
	    (coex_sta->low_priority_rx == 0)) {
		num_of_bt_counter_chk++;

		if (num_of_bt_counter_chk >= 3) {
			halbtc8822b1ant_query_bt_info(
				btcoexist);
			num_of_bt_counter_chk = 0;
		}
	}
#if 0
	/* Add Hi-Pri Tx/Rx counter to avoid false detection */
	if (((coex_sta->hid_exist) || (coex_sta->sco_exist)) &&
	    (coex_sta->high_priority_tx + coex_sta->high_priority_rx
	     >= 160)
	    && (!coex_sta->c2h_bt_inquiry_page))
		coex_sta->bt_hi_pri_link_exist = true;
	else
		coex_sta->bt_hi_pri_link_exist = false;

	if ((coex_sta->acl_busy) &&
	    (coex_sta->num_of_profile == 0)) {
		if (coex_sta->low_priority_tx +
		    coex_sta->low_priority_rx >= 160) {
			coex_sta->pan_exist = true;
			coex_sta->num_of_profile++;
			coex_sta->wrong_profile_notification++;
		}
	}
#endif

}


void halbtc8822b1ant_monitor_wifi_ctr(IN struct btc_coexist *btcoexist)
{
	s32 wifi_rssi = 0;
	boolean wifi_busy = false, wifi_under_b_mode = false;
	static u8 cck_lock_counter = 0;
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
		coex_sta->crc_ok_cck	= btcoexist->btc_read_2byte(
						  btcoexist,
						  0xf04);
		coex_sta->crc_ok_11g	= btcoexist->btc_read_2byte(
						  btcoexist,
						  0xf14);
		coex_sta->crc_ok_11n	= btcoexist->btc_read_2byte(
						  btcoexist,
						  0xf10);
		coex_sta->crc_ok_11n_agg = btcoexist->btc_read_2byte(
						   btcoexist,
						   0xf0c);

		coex_sta->crc_err_cck	 = btcoexist->btc_read_2byte(
			   btcoexist, 0xf00) +  btcoexist->btc_read_2byte(
						   btcoexist, 0xf06);

		coex_sta->crc_err_11g	 = btcoexist->btc_read_2byte(
						   btcoexist,
						   0xf16);
		coex_sta->crc_err_11n	 = btcoexist->btc_read_2byte(
						   btcoexist,
						   0xf12);
		coex_sta->crc_err_11n_agg = btcoexist->btc_read_2byte(
						    btcoexist,
						    0xf0e);
	}


	/* reset counter */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0xb58, 0x1, 0x1);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0xb58, 0x1, 0x0);

	if ((wifi_busy) && (wifi_rssi >= 30) && (!wifi_under_b_mode)) {
		total_cnt = coex_sta->crc_ok_cck + coex_sta->crc_ok_11g
			    +
			    coex_sta->crc_ok_11n +
			    coex_sta->crc_ok_11n_agg;

		if ((coex_dm->bt_status ==
		     BT_8822B_1ANT_BT_STATUS_ACL_BUSY) ||
		    (coex_dm->bt_status ==
		     BT_8822B_1ANT_BT_STATUS_ACL_SCO_BUSY) ||
		    (coex_dm->bt_status ==
		     BT_8822B_1ANT_BT_STATUS_SCO_BUSY)) {
			if (coex_sta->crc_ok_cck > (total_cnt -
						    coex_sta->crc_ok_cck)) {
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


boolean halbtc8822b1ant_is_wifi_status_changed(IN struct btc_coexist *btcoexist)
{
	static boolean	pre_wifi_busy = false, pre_under_4way = false,
			pre_bt_hs_on = false, pre_rf4ce_enabled = false, pre_bt_off = false;
	boolean wifi_busy = false, under_4way = false, bt_hs_on = false, rf4ce_enabled = false;
	boolean wifi_connected = false;

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
		return true;
	}
	btcoexist->btc_get(btcoexist, BTC_GET_BL_RF4CE_CONNECTED, &rf4ce_enabled);

		if (rf4ce_enabled != pre_rf4ce_enabled) {
		pre_rf4ce_enabled = rf4ce_enabled;

		if (rf4ce_enabled)
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], rf4ce is enabled !!\n");
		else
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], rf4ce is disabled !!\n");

		BTC_TRACE(trace_buf);


		return true;
	}

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

void halbtc8822b1ant_update_bt_link_info(IN struct btc_coexist *btcoexist)
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
	bt_link_info->acl_busy = coex_sta->acl_busy;

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

void halbtc8822b1ant_update_wifi_channel_info(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	u8			h2c_parameter[3] = {0};
	u32			wifi_bw;
	u8			wifi_central_chnl;

	/* only 2.4G we need to inform bt the chnl mask */
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_CENTRAL_CHNL,
			   &wifi_central_chnl);
	if ((BTC_MEDIA_CONNECT == type) &&
	    (wifi_central_chnl <= 14)) {

		h2c_parameter[0] =
			0x1;  /* enable BT AFH skip WL channel for 8822b because BT Rx LO interference */
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

u8 halbtc8822b1ant_action_algorithm(IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info	*bt_link_info = &btcoexist->bt_link_info;
	boolean				bt_hs_on = false;
	u8				algorithm = BT_8822B_1ANT_COEX_ALGO_UNDEFINED;
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
				    "[BTCoex], BT Profile = SCO only\n");
			BTC_TRACE(trace_buf);
			algorithm = BT_8822B_1ANT_COEX_ALGO_SCO;
		} else {
			if (bt_link_info->hid_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT Profile = HID only\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8822B_1ANT_COEX_ALGO_HID;
			} else if (bt_link_info->a2dp_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT Profile = A2DP only\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8822B_1ANT_COEX_ALGO_A2DP;
			} else if (bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = PAN(HS) only\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8822B_1ANT_COEX_ALGO_PANHS;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = PAN(EDR) only\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8822B_1ANT_COEX_ALGO_PANEDR;
				}
			}
		}
	} else if (num_of_diff_profile == 2) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT Profile = SCO + HID\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8822B_1ANT_COEX_ALGO_HID;
			} else if (bt_link_info->a2dp_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT Profile = SCO + A2DP ==> SCO\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8822B_1ANT_COEX_ALGO_SCO;
			} else if (bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = SCO + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm = BT_8822B_1ANT_COEX_ALGO_SCO;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = SCO + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8822B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (bt_link_info->hid_exist &&
			    bt_link_info->a2dp_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT Profile = HID + A2DP\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8822B_1ANT_COEX_ALGO_HID_A2DP;
			} else if (bt_link_info->hid_exist &&
				   bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = HID + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8822B_1ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = HID + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8822B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (bt_link_info->pan_exist &&
				   bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = A2DP + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8822B_1ANT_COEX_ALGO_A2DP_PANHS;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = A2DP + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8822B_1ANT_COEX_ALGO_PANEDR_A2DP;
				}
			}
		}
	} else if (num_of_diff_profile == 3) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist &&
			    bt_link_info->a2dp_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT Profile = SCO + HID + A2DP ==> HID\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8822B_1ANT_COEX_ALGO_HID;
			} else if (bt_link_info->hid_exist &&
				   bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = SCO + HID + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8822B_1ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = SCO + HID + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8822B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (bt_link_info->pan_exist &&
				   bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = SCO + A2DP + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm = BT_8822B_1ANT_COEX_ALGO_SCO;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = SCO + A2DP + PAN(EDR) ==> HID\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8822B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (bt_link_info->hid_exist &&
			    bt_link_info->pan_exist &&
			    bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = HID + A2DP + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8822B_1ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = HID + A2DP + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8822B_1ANT_COEX_ALGO_HID_A2DP_PANEDR;
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
						"[BTCoex], Error!!! BT Profile = SCO + HID + A2DP + PAN(HS)\n");
					BTC_TRACE(trace_buf);

				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = SCO + HID + A2DP + PAN(EDR)==>PAN(EDR)+HID\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8822B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
	}

	return algorithm;
}

void halbtc8822b1ant_set_bt_auto_report(IN struct btc_coexist *btcoexist,
					IN boolean enable_auto_report)
{
	u8			h2c_parameter[1] = {0};

	h2c_parameter[0] = 0;

	if (enable_auto_report)
		h2c_parameter[0] |= BIT(0);

	btcoexist->btc_fill_h2c(btcoexist, 0x68, 1, h2c_parameter);
}

void halbtc8822b1ant_bt_auto_report(IN struct btc_coexist *btcoexist,
		    IN boolean force_exec, IN boolean enable_auto_report)
{
	coex_dm->cur_bt_auto_report = enable_auto_report;

	if (!force_exec) {
		if (coex_dm->pre_bt_auto_report == coex_dm->cur_bt_auto_report)
			return;
	}
	halbtc8822b1ant_set_bt_auto_report(btcoexist,
					   coex_dm->cur_bt_auto_report);

	coex_dm->pre_bt_auto_report = coex_dm->cur_bt_auto_report;
}



void halbtc8822b1ant_set_sw_penalty_tx_rate_adaptive(IN struct btc_coexist
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

void halbtc8822b1ant_low_penalty_ra(IN struct btc_coexist *btcoexist,
			    IN boolean force_exec, IN boolean low_penalty_ra)
{
#if 1
	coex_dm->cur_low_penalty_ra = low_penalty_ra;

	if (!force_exec) {
		if (coex_dm->pre_low_penalty_ra ==
		    coex_dm->cur_low_penalty_ra)
			return;
	}

	if (low_penalty_ra)
		btcoexist->btc_phydm_modify_RA_PCR_threshold(btcoexist, 0, 25);
	else
		btcoexist->btc_phydm_modify_RA_PCR_threshold(btcoexist, 0, 0);

	coex_dm->pre_low_penalty_ra = coex_dm->cur_low_penalty_ra;

#endif
}

void halbtc8822b1ant_write_score_board(
	IN	struct  btc_coexist		*btcoexist,
	IN	u16				bitpos,
	IN	boolean		state
)
{

	static u16 originalval = 0x8002;

	if (state)
		originalval = originalval | bitpos;
	else
		originalval = originalval & (~bitpos);

	btcoexist->btc_write_2byte(btcoexist, 0xaa, originalval);
}

void halbtc8822b1ant_read_score_board(
	IN	struct  btc_coexist		*btcoexist,
	IN   u16				*score_board_val
)
{

	*score_board_val = (btcoexist->btc_read_2byte(btcoexist,
			    0xaa)) & 0x7fff;
}

void halbtc8822b1ant_post_state_to_bt(
	IN	struct  btc_coexist		*btcoexist,
	IN	u16						type,
	IN  boolean                 state
)
{

	halbtc8822b1ant_write_score_board(btcoexist, (u16) type, state);

}


void halbtc8822b1ant_monitor_bt_enable_disable(IN struct btc_coexist *btcoexist)
{
	static u32		bt_disable_cnt = 0;
	boolean			bt_active = true, bt_disabled = false,
				wifi_under_5g = false;
	u16		u16tmp;

	/* This function check if bt is disabled */
#if 0
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


#else

	/* Read BT on/off status from scoreboard[1], enable this only if BT patch support this feature */
	halbtc8822b1ant_read_score_board(btcoexist, &u16tmp);

	bt_active = u16tmp & BIT(1);


#endif

	if (bt_active) {
		bt_disable_cnt = 0;
		bt_disabled = false;
		btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_DISABLE,
				   &bt_disabled);
	} else {

		bt_disable_cnt++;
		if (bt_disable_cnt >= 2) {
			bt_disabled = true;
			bt_disable_cnt = 2;
		}

		btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_DISABLE,
				   &bt_disabled);
	}


	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G,
			   &wifi_under_5g);

	if ((wifi_under_5g) || (bt_disabled))
		halbtc8822b1ant_low_penalty_ra(btcoexist,
					       NORMAL_EXEC, false);
	else
		halbtc8822b1ant_low_penalty_ra(btcoexist,
					       NORMAL_EXEC, true);


	if (coex_sta->bt_disabled != bt_disabled) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is from %s to %s!!\n",
			    (coex_sta->bt_disabled ? "disabled" :
			     "enabled"),
			    (bt_disabled ? "disabled" : "enabled"));
		BTC_TRACE(trace_buf);
		coex_sta->bt_disabled = bt_disabled;
	}

}



void halbtc8822b1ant_enable_gnt_to_gpio(IN struct btc_coexist *btcoexist,
					boolean isenable)
{
	static u8			bitVal[5] = {0, 0, 0, 0, 0};
	static boolean		state = false;

	if (state == isenable)
		return;

	state = isenable;

	if (isenable) {

		/* enable GNT_WL, GNT_BT to GPIO for debug */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x73, 0x8, 0x1);

		/* store original value */
		bitVal[0] = (btcoexist->btc_read_1byte(btcoexist,
				       0x66) & BIT(4)) >> 4;	/*0x66[4] */
		bitVal[1] = (btcoexist->btc_read_1byte(btcoexist,
					       0x67) & BIT(0));		/*0x66[8] */
		bitVal[2] = (btcoexist->btc_read_1byte(btcoexist,
				       0x42) & BIT(3)) >> 3;  /*0x40[19] */
		bitVal[3] = (btcoexist->btc_read_1byte(btcoexist,
				       0x65) & BIT(7)) >> 7;  /*0x64[15] */
		bitVal[4] = (btcoexist->btc_read_1byte(btcoexist,
				       0x72) & BIT(2)) >> 2;  /*0x70[18] */

		/*  switch GPIO Mux */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x66, BIT(4),
						   0x0);  /*0x66[4] = 0 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, BIT(0),
						   0x0);  /*0x66[8] = 0 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x42, BIT(3),
						   0x0);  /*0x40[19] = 0 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x65, BIT(7),
						   0x0);  /*0x64[15] = 0 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x72, BIT(2),
						   0x0);  /*0x70[18] = 0 */


	} else {
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x73, 0x8, 0x0);

		/*  Restore original value  */
		/*  switch GPIO Mux */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x66, BIT(4),
						   bitVal[0]);  /*0x66[4] = 0 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, BIT(0),
						   bitVal[1]);  /*0x66[8] = 0 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x42, BIT(3),
					   bitVal[2]);  /*0x40[19] = 0 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x65, BIT(7),
					   bitVal[3]);  /*0x64[15] = 0 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x72, BIT(2),
					   bitVal[4]);  /*0x70[18] = 0 */
	}

}


u32 halbtc8822b1ant_ltecoex_indirect_read_reg(IN struct btc_coexist *btcoexist,
		IN u16 reg_addr)
{
	u32 j = 0, delay_count = 0;


	/* wait for ready bit before access 0x1700		 */
	btcoexist->btc_write_4byte(btcoexist, 0x1700, 0x800F0000 | reg_addr);
#if 0
	do {
		j++;
	} while (((btcoexist->btc_read_1byte(btcoexist,
					     0x1703) & BIT(5)) == 0) &&
		 (j < BT_8822B_1ANT_LTECOEX_INDIRECTREG_ACCESS_TIMEOUT));
#endif
	while (1) {
		if ((btcoexist->btc_read_1byte(btcoexist, 0x1703)&BIT(5)) == 0) {
			delay_ms(50);
			delay_count++;
			if (delay_count >= 10) {
				delay_count = 0;
				break;
			}
		} else
			break;
	}
	return btcoexist->btc_read_4byte(btcoexist,
					 0x1708);  /* get read data */

}

void halbtc8822b1ant_ltecoex_indirect_write_reg(IN struct btc_coexist
		*btcoexist,
		IN u16 reg_addr, IN u32 bit_mask, IN u32 reg_value)
{
	u32 val, i = 0, j = 0, bitpos = 0, delay_count = 0;


	if (bit_mask == 0x0)
		return;
	if (bit_mask == 0xffffffff) {
		btcoexist->btc_write_4byte(btcoexist, 0x1704,
					   reg_value); /* put write data */

		/* wait for ready bit before access 0x1700 */
#if 0
		do {
			j++;
		} while (((btcoexist->btc_read_1byte(btcoexist,
						     0x1703) & BIT(5)) == 0) &&
			(j < BT_8822B_1ANT_LTECOEX_INDIRECTREG_ACCESS_TIMEOUT));
#endif

		while (1) {
			if ((btcoexist->btc_read_1byte(btcoexist, 0x1703)&BIT(5)) == 0) {
				delay_ms(50);
				delay_count++;
				if (delay_count >= 10)	{
					delay_count = 0;
					break;
}
			} else
				break;
		}
		btcoexist->btc_write_4byte(btcoexist, 0x1700,
					   0xc00F0000 | reg_addr);
	} else {
		for (i = 0; i <= 31; i++) {
			if (((bit_mask >> i) & 0x1) == 0x1) {
				bitpos = i;
				break;
			}
		}

		/* read back register value before write */
		val = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist,
				reg_addr);
		val = (val & (~bit_mask)) | (reg_value << bitpos);

		/* put write data value */
		btcoexist->btc_write_4byte(btcoexist, 0x1704,
					   val); /* put write data */

		/* wait for ready bit before access 0x1700		 */
#if	0
		do {
			j++;
		} while (((btcoexist->btc_read_1byte(btcoexist,
						     0x1703) & BIT(5)) == 0) &&
			(j < BT_8822B_1ANT_LTECOEX_INDIRECTREG_ACCESS_TIMEOUT));
#endif

		while (1) {
			if ((btcoexist->btc_read_1byte(btcoexist, 0x1703)&BIT(5)) == 0) {
				delay_ms(50);
				delay_count++;
				if (delay_count >= 10)	{
					delay_count = 0;
					break;
				}
			} else
				break;
		}



		btcoexist->btc_write_4byte(btcoexist, 0x1700,
					   0xc00F0000 | reg_addr);

	}

}

void halbtc8822b1ant_ltecoex_enable(IN struct btc_coexist *btcoexist,
				    IN boolean enable)
{
	u8 val;

	val = (enable) ? 1 : 0;
	/* 0x38[7] */
	halbtc8822b1ant_ltecoex_indirect_write_reg(btcoexist, 0x38, 0x80,
			val);  /* 0x38[7] */

}


void halbtc8822b1ant_ltecoex_pathcontrol_owner(IN struct btc_coexist *btcoexist,
		IN boolean wifi_control)
{
	u8 val;

	val = (wifi_control) ? 1 : 0;
	/* 0x70[26] */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x73, 0x4,
					   val); /* 0x70[26] */

}

void halbtc8822b1ant_ltecoex_set_gnt_bt(IN struct btc_coexist *btcoexist,
			IN u8 control_block, IN boolean sw_control, IN u8 state)
{
	u32 val = 0, bit_mask;

	state = state & 0x1;
	/*LTE indirect 0x38=0xccxx (sw : gnt_wl=1,sw gnt_bt=1)
	0x38=0xddxx (sw : gnt_bt=1 , sw gnt_wl=0)
	0x38=0x55xx(hw pta :gnt_wl /gnt_bt ) */
	val = (sw_control) ? ((state << 1) | 0x1) : 0;

	switch (control_block) {
	case BT_8822B_1ANT_GNT_BLOCK_RFC_BB:
	default:
		bit_mask = 0xc000;
		halbtc8822b1ant_ltecoex_indirect_write_reg(btcoexist,
				0x38, bit_mask, val); /* 0x38[15:14] */
		bit_mask = 0x0c00;
		halbtc8822b1ant_ltecoex_indirect_write_reg(btcoexist,
				0x38, bit_mask, val); /* 0x38[11:10]						 */
		break;
	case BT_8822B_1ANT_GNT_BLOCK_RFC:
		bit_mask = 0xc000;
		halbtc8822b1ant_ltecoex_indirect_write_reg(btcoexist,
				0x38, bit_mask, val); /* 0x38[15:14] */
		break;
	case BT_8822B_1ANT_GNT_BLOCK_BB:
		bit_mask = 0x0c00;
		halbtc8822b1ant_ltecoex_indirect_write_reg(btcoexist,
				0x38, bit_mask, val); /* 0x38[11:10] */
		break;

	}

}

void halbtc8822b1ant_ltecoex_set_gnt_wl(IN struct btc_coexist *btcoexist,
			IN u8 control_block, IN boolean sw_control, IN u8 state)
{
	u32 val = 0, bit_mask;
	/*LTE indirect 0x38=0xccxx (sw : gnt_wl=1,sw gnt_bt=1)
	0x38=0xddxx (sw : gnt_bt=1 , sw gnt_wl=0)
	0x38=0x55xx(hw pta :gnt_wl /gnt_bt ) */

	state = state & 0x1;
	val = (sw_control) ? ((state << 1) | 0x1) : 0;

	switch (control_block) {
	case BT_8822B_1ANT_GNT_BLOCK_RFC_BB:
	default:
		bit_mask = 0x3000;
		halbtc8822b1ant_ltecoex_indirect_write_reg(btcoexist,
				0x38, bit_mask, val); /* 0x38[13:12] */
		bit_mask = 0x0300;
		halbtc8822b1ant_ltecoex_indirect_write_reg(btcoexist,
				0x38, bit_mask, val); /* 0x38[9:8]						 */
		break;
	case BT_8822B_1ANT_GNT_BLOCK_RFC:
		bit_mask = 0x3000;
		halbtc8822b1ant_ltecoex_indirect_write_reg(btcoexist,
				0x38, bit_mask, val); /* 0x38[13:12] */
		break;
	case BT_8822B_1ANT_GNT_BLOCK_BB:
		bit_mask = 0x0300;
		halbtc8822b1ant_ltecoex_indirect_write_reg(btcoexist,
				0x38, bit_mask, val); /* 0x38[9:8] */
		break;

	}

}

void halbtc8822b1ant_ltecoex_set_coex_table(IN struct btc_coexist *btcoexist,
		IN u8 table_type, IN u16 table_content)
{
	u16 reg_addr = 0x0000;

	switch (table_type) {
	case BT_8822B_1ANT_CTT_WL_VS_LTE:
		reg_addr = 0xa0;
		break;
	case BT_8822B_1ANT_CTT_BT_VS_LTE:
		reg_addr = 0xa4;
		break;
	}

	if (reg_addr != 0x0000)
		halbtc8822b1ant_ltecoex_indirect_write_reg(btcoexist, reg_addr,
			0xffff, table_content); /* 0xa0[15:0] or 0xa4[15:0] */


}


void halbtc8822b1ant_ltcoex_set_break_table(IN struct btc_coexist *btcoexist,
		IN u8 table_type, IN u8 table_content)
{
	u16 reg_addr = 0x0000;

	switch (table_type) {
	case BT_8822B_1ANT_LBTT_WL_BREAK_LTE:
		reg_addr = 0xa8;
		break;
	case BT_8822B_1ANT_LBTT_BT_BREAK_LTE:
		reg_addr = 0xac;
		break;
	case BT_8822B_1ANT_LBTT_LTE_BREAK_WL:
		reg_addr = 0xb0;
		break;
	case BT_8822B_1ANT_LBTT_LTE_BREAK_BT:
		reg_addr = 0xb4;
		break;
	}

	if (reg_addr != 0x0000)
		halbtc8822b1ant_ltecoex_indirect_write_reg(btcoexist, reg_addr,
			0xff, table_content); /* 0xa8[15:0] or 0xb4[15:0] */


}

void halbtc8822b1ant_set_wltoggle_coex_table(IN struct btc_coexist *btcoexist,
		IN boolean force_exec,  IN u8 interval,
		IN u8 val0x6c4_b0, IN u8 val0x6c4_b1, IN u8 val0x6c4_b2,
		IN u8 val0x6c4_b3)
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

	if (!force_exec) {
		for (i = 1; i <= 5; i++) {
			if (cur_h2c_parameter[i] != pre_h2c_parameter[i])
				break;

			match_cnt++;
		}

		if (match_cnt == 5)
			return;
	}

	for (i = 1; i <= 5; i++)
		pre_h2c_parameter[i] = cur_h2c_parameter[i];

	btcoexist->btc_fill_h2c(btcoexist, 0x69, 6, cur_h2c_parameter);
}

void halbtc8822b1ant_set_coex_table(IN struct btc_coexist *btcoexist,
	    IN u32 val0x6c0, IN u32 val0x6c4, IN u32 val0x6c8, IN u8 val0x6cc)
{
	btcoexist->btc_write_4byte(btcoexist, 0x6c0, val0x6c0);

	btcoexist->btc_write_4byte(btcoexist, 0x6c4, val0x6c4);

	btcoexist->btc_write_4byte(btcoexist, 0x6c8, val0x6c8);

	btcoexist->btc_write_1byte(btcoexist, 0x6cc, val0x6cc);
}

void halbtc8822b1ant_coex_table(IN struct btc_coexist *btcoexist,
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
	halbtc8822b1ant_set_coex_table(btcoexist, val0x6c0, val0x6c4, val0x6c8,
				       val0x6cc);

	coex_dm->pre_val0x6c0 = coex_dm->cur_val0x6c0;
	coex_dm->pre_val0x6c4 = coex_dm->cur_val0x6c4;
	coex_dm->pre_val0x6c8 = coex_dm->cur_val0x6c8;
	coex_dm->pre_val0x6cc = coex_dm->cur_val0x6cc;
}

void halbtc8822b1ant_coex_table_with_type(IN struct btc_coexist *btcoexist,
		IN boolean force_exec, IN u8 type)
{
	u32	break_table;
	u8	select_table;



	coex_sta->coex_table_type = type;

	if (coex_sta->concurrent_rx_mode_on == true) {
		break_table = 0xf0ffffff;	/* set WL hi-pri can break BT */
		select_table = 0x3;		/* set Tx response = Hi-Pri (ex: Transmitting ACK,BA,CTS) */
	} else {
		break_table = 0xffffff;
		select_table = 0x3;
	}

	switch (type) {
	case 0:
		halbtc8822b1ant_coex_table(btcoexist, force_exec,
					   0x55555555, 0x55555555, break_table,
					   select_table);
		break;
	case 1:
		halbtc8822b1ant_coex_table(btcoexist, force_exec,
					   0x55555555, 0x5a5a5a5a, break_table,
					   select_table);
		break;
	case 2:
		halbtc8822b1ant_coex_table(btcoexist, force_exec,
					   0xaa5a5a5a, 0xaa5a5a5a, break_table,
					   select_table);
		break;
	case 3:
		halbtc8822b1ant_coex_table(btcoexist, force_exec,
					   0x55555555, 0xaa5a5a5a, break_table,
					   select_table);
		break;
	case 4:
		halbtc8822b1ant_coex_table(btcoexist,
					   force_exec, 0xaa555555, 0xaa5a5a5a,
					   break_table, select_table);
		break;
	case 5:
		halbtc8822b1ant_coex_table(btcoexist,
					   force_exec, 0x5a5a5a5a, 0x5a5a5a5a,
					   break_table, select_table);
		break;
	case 6:
		halbtc8822b1ant_coex_table(btcoexist, force_exec,
					   0x55555555, 0xaaaaaaaa, break_table,
					   select_table);
		break;
	case 7:
		halbtc8822b1ant_coex_table(btcoexist, force_exec,
					   0xaaaaaaaa, 0xaaaaaaaa, break_table,
					   select_table);
		break;
	case 8:
		halbtc8822b1ant_coex_table(btcoexist, force_exec,
					   0xffffffff, 0xffffffff, break_table,
					   select_table);
		break;
	case 9:
		halbtc8822b1ant_coex_table(btcoexist, force_exec,
					   0x5a5a5555, 0xaaaa5a5a, break_table,
					   select_table);
		break;
	case 10:
		halbtc8822b1ant_coex_table(btcoexist, force_exec,
					   0xaaaa5aaa, 0xaaaa5aaa, break_table,
					   select_table);
		break;
	case 11:
		halbtc8822b1ant_coex_table(btcoexist, force_exec,
					   0xaaaaa5aa, 0xaaaaaaaa, break_table,
					   select_table);
		break;
	case 12:
		halbtc8822b1ant_coex_table(btcoexist, force_exec,
					   0xaaaaa5aa, 0xaaaaa5aa, break_table,
					   select_table);
		break;
	case 13:
		halbtc8822b1ant_coex_table(btcoexist, force_exec,
					   0x55555555, 0xaaaa5a5a, break_table,
					   select_table);
		break;
	case 14:
		halbtc8822b1ant_coex_table(btcoexist, force_exec,
					   0x5a5a555a, 0xaaaa5a5a, break_table,
					   select_table);
		break;
	case 15:
		halbtc8822b1ant_coex_table(btcoexist, force_exec,
					   0x55555555, 0xaaaa55aa, break_table,
					   select_table);
		break;
	case 16:
		halbtc8822b1ant_coex_table(btcoexist, force_exec,
					   0x5a5a555a, 0x5a5a555a, break_table,
					   select_table);
		break;
	case 17:
		halbtc8822b1ant_coex_table(btcoexist, force_exec,
					   0xaaaa55aa, 0xaaaa55aa, break_table,
					   select_table);
		break;

	default:
		break;
	}
}

void halbtc8822b1ant_set_fw_ignore_wlan_act(IN struct btc_coexist *btcoexist,
		IN boolean enable)
{


	u8			h2c_parameter[1] = {0};

	if (enable)
		h2c_parameter[0] |= BIT(0);		/* function enable */

	btcoexist->btc_fill_h2c(btcoexist, 0x63, 1, h2c_parameter);
}

void halbtc8822b1ant_ignore_wlan_act(IN struct btc_coexist *btcoexist,
				     IN boolean force_exec, IN boolean enable)
{

	coex_dm->cur_ignore_wlan_act = enable;

	if (!force_exec) {
		if (coex_dm->pre_ignore_wlan_act ==
		    coex_dm->cur_ignore_wlan_act) {

			coex_dm->pre_ignore_wlan_act =
				coex_dm->cur_ignore_wlan_act;
			return;
		}
	}

	halbtc8822b1ant_set_fw_ignore_wlan_act(btcoexist, enable);

	coex_dm->pre_ignore_wlan_act = coex_dm->cur_ignore_wlan_act;
}

void halbtc8822b1ant_set_lps_rpwm(IN struct btc_coexist *btcoexist,
				  IN u8 lps_val, IN u8 rpwm_val)
{
	u8	lps = lps_val;
	u8	rpwm = rpwm_val;

	btcoexist->btc_set(btcoexist, BTC_SET_U1_LPS_VAL, &lps);
	btcoexist->btc_set(btcoexist, BTC_SET_U1_RPWM_VAL, &rpwm);
}

void halbtc8822b1ant_lps_rpwm(IN struct btc_coexist *btcoexist,
		      IN boolean force_exec, IN u8 lps_val, IN u8 rpwm_val)
{
	coex_dm->cur_lps = lps_val;
	coex_dm->cur_rpwm = rpwm_val;

	if (!force_exec) {
		if ((coex_dm->pre_lps == coex_dm->cur_lps) &&
		    (coex_dm->pre_rpwm == coex_dm->cur_rpwm))
			return;
	}
	halbtc8822b1ant_set_lps_rpwm(btcoexist, lps_val, rpwm_val);

	coex_dm->pre_lps = coex_dm->cur_lps;
	coex_dm->pre_rpwm = coex_dm->cur_rpwm;
}

void halbtc8822b1ant_ps_tdma_check_for_power_save_state(
	IN struct btc_coexist *btcoexist, IN boolean new_ps_state)
{
	u8	lps_mode = 0x0;
	u8	h2c_parameter[5] = {0x8, 0, 0, 0, 0};

	btcoexist->btc_get(btcoexist, BTC_GET_U1_LPS_MODE, &lps_mode);

	if (lps_mode) { /* already under LPS state */
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


void halbtc8822b1ant_power_save_state(IN struct btc_coexist *btcoexist,
			      IN u8 ps_type, IN u8 lps_val, IN u8 rpwm_val)
{
	boolean		low_pwr_disable = false;

	switch (ps_type) {
	case BTC_PS_WIFI_NATIVE:
		/* recover to original 32k low power setting */
		coex_sta->force_lps_on = false;
		low_pwr_disable = false;
		btcoexist->btc_set(btcoexist,
				   BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_NORMAL_LPS,
				   NULL);
		coex_sta->force_lps_on = false;
		break;
	case BTC_PS_LPS_ON:

		coex_sta->force_lps_on = true;
		halbtc8822b1ant_ps_tdma_check_for_power_save_state(
			btcoexist, true);
		halbtc8822b1ant_lps_rpwm(btcoexist, NORMAL_EXEC,
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

		coex_sta->force_lps_on = false;
		halbtc8822b1ant_ps_tdma_check_for_power_save_state(
			btcoexist, false);
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS,
				   NULL);
		coex_sta->force_lps_on = false;
		break;
	default:
		break;
	}
}


void halbtc8822b1ant_set_fw_pstdma(IN struct btc_coexist *btcoexist,
	   IN u8 byte1, IN u8 byte2, IN u8 byte3, IN u8 byte4, IN u8 byte5)
{
	u8			h2c_parameter[5] = {0};
	u8			real_byte1 = byte1, real_byte5 = byte5;
	boolean		ap_enable = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE,
			   &ap_enable);

		/*if (ap_enable) {
		if (byte1 & BIT(4) && !(byte1 & BIT(5))) {*/
		if ((ap_enable) && (byte1 & BIT(4) && !(byte1 & BIT(5)))) {

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], FW for 1Ant AP mode\n");
			BTC_TRACE(trace_buf);

			real_byte1 &= ~BIT(4);
			real_byte1 |= BIT(5);

			real_byte5 |= BIT(5);
			real_byte5 &= ~BIT(6);

			halbtc8822b1ant_power_save_state(btcoexist,
						 BTC_PS_WIFI_NATIVE, 0x0,
							 0x0);

	} else if (byte1 & BIT(4) && !(byte1 & BIT(5))) {

		halbtc8822b1ant_power_save_state(btcoexist,
						 BTC_PS_LPS_ON, 0x50,
						 0x4);
	} else {
		halbtc8822b1ant_power_save_state(btcoexist,
						 BTC_PS_WIFI_NATIVE, 0x0,
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
}

void halbtc8822b1ant_ps_tdma(IN struct btc_coexist *btcoexist,
		     IN boolean force_exec, IN boolean turn_on, IN u8 type)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	struct  btc_board_info	*board_info = &btcoexist->board_info;
	boolean			wifi_busy = false;
	static u8			psTdmaByte4Modify = 0x0, pre_psTdmaByte4Modify = 0x0;
	static boolean	 pre_wifi_busy = false;

	coex_dm->cur_ps_tdma_on = turn_on;
	coex_dm->cur_ps_tdma = type;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	if (wifi_busy != pre_wifi_busy) {
		force_exec = true;
		pre_wifi_busy = wifi_busy;
	}

	/* 0x778 = 0x1 at wifi slot (no blocking BT Low-Pri pkts) */
	if ((bt_link_info->slave_role) && (bt_link_info->a2dp_exist))
		psTdmaByte4Modify = 0x1;
	else
		psTdmaByte4Modify = 0x0;

	if (pre_psTdmaByte4Modify != psTdmaByte4Modify) {

		force_exec = true;
		pre_psTdmaByte4Modify = psTdmaByte4Modify;
	}

	if (coex_dm->cur_ps_tdma_on) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], ********TDMA(on, %d) **********\n",
			    coex_dm->cur_ps_tdma);
		BTC_TRACE(trace_buf);
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], **********TDMA(off, %d) **********\n",
			    coex_dm->cur_ps_tdma);
		BTC_TRACE(trace_buf);
	}

	if (!force_exec) {
		if ((coex_dm->pre_ps_tdma_on == coex_dm->cur_ps_tdma_on) &&
		    (coex_dm->pre_ps_tdma == coex_dm->cur_ps_tdma)) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], return for no-TDMA case change\n");
			BTC_TRACE(trace_buf);

			return;
		}
	}


	if (turn_on) {

		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x550, 0x8,
					   0x1);  /* enable TBTT nterrupt */

		switch (type) {
		default:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x1a, 0x1a, 0x0, 0x10);
			break;
		case 1:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x3a, 0x03, 0x11, 0x10);
			break;
		case 3:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x3a, 0x03, 0x10, 0x10);
			break;
		case 4:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x21, 0x03, 0x10, 0x10);
			break;
		case 5:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x15, 0x3, 0x11, 0x11);
			break;
		case 7:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x10, 0x03, 0x10,  0x14 |
						      psTdmaByte4Modify);
			break;
		case 8:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x10, 0x03, 0x10,  0x14 |
						      psTdmaByte4Modify);
			break;
		case 13:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x25, 0x03, 0x10,  0x10 |
						      psTdmaByte4Modify);
			break;
		case 14:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x15, 0x03, 0x10,  0x10 |
						      psTdmaByte4Modify);
			break;
		case 15:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x20, 0x03, 0x10,  0x10 |
						      psTdmaByte4Modify);
			break;
		case 17:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x10, 0x03, 0x11,  0x14 |
						      psTdmaByte4Modify);
			break;

		case 20:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x30, 0x03, 0x11, 0x10);
			break;
		case 22:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x25, 0x03, 0x11, 0x10);
			break;
		case 32:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x35, 0x3, 0x11, 0x11);
			break;
		case 33:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x35, 0x03, 0x11, 0x10);
			break;
		case 41:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x45, 0x3, 0x11, 0x11);
			break;
		case 42:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x1e, 0x3, 0x10, 0x14 |
						      psTdmaByte4Modify);
			break;
		case 43:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x45, 0x3, 0x10, 0x14);
			break;
		case 44:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x25, 0x3, 0x10, 0x10);
			break;
		case 45:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x29, 0x3, 0x10, 0x10);
			break;
		case 46:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x1a, 0x3, 0x10, 0x10);
			break;
		case 47:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x32, 0x3, 0x10, 0x10);
			break;
		case 48:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x29, 0x3, 0x10, 0x10);
			break;
		case 49:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x55, 0x1e, 0x3, 0x10, 0x54);
			break;
		case 50:
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x4a, 0x3, 0x10, 0x10);
			break;

		}
	} else {

		switch (type) {
		case 0:
		default:  /* Software control, Antenna at BT side */
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
						      0x0, 0x0, 0x0, 0x0, 0x0);
			/*
			halbtc8822b1ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_BT, FORCE_EXEC, false,
						     false); */
			break;
		case 8: /* PTA Control */
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
						      0x8, 0x0, 0x0, 0x0, 0x0);
			/*
			halbtc8822b1ant_set_ant_path(btcoexist,
				     BTC_ANT_PATH_PTA, FORCE_EXEC,  false,
				     false);  */
			break;
		case 9:   /* Software control, Antenna at WiFi side */
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
						      0x0, 0x0, 0x0, 0x0, 0x0);
			/*
			halbtc8822b1ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_WIFI, FORCE_EXEC,false,false); */
			break;
		case 10:	/* under 5G , 0x778=1*/
			halbtc8822b1ant_set_fw_pstdma(btcoexist,
						      0x0, 0x0, 0x0, 0x0, 0x0);

			/*
			halbtc8822b1ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_WIFI5G, FORCE_EXEC, false,
						     false);	*/

			break;
		}
	}


	/* update pre state */
	coex_dm->pre_ps_tdma_on = coex_dm->cur_ps_tdma_on;
	coex_dm->pre_ps_tdma = coex_dm->cur_ps_tdma;
}


void halbtc8822b1ant_sw_mechanism(IN struct btc_coexist *btcoexist,
				  IN boolean low_penalty_ra)
{
	halbtc8822b1ant_low_penalty_ra(btcoexist, NORMAL_EXEC, low_penalty_ra);
}
/*rf4 type by efuse , and for ant at main aux inverse use , because is 2x2 ,and control types are the same ,does not need */

void halbtc8822b1ant_set_rfe_type(IN struct btc_coexist *btcoexist)
{
	struct  btc_board_info *board_info = &btcoexist->board_info;

	/* Ext switch buffer mux */
		btcoexist->btc_write_1byte(btcoexist, 0x974, 0xff);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x1991, 0x3, 0x0);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcbe, 0x8, 0x0);

	/* the following setup should be got from Efuse in the future */
	rfe_type->rfe_module_type = board_info->rfe_type;

	rfe_type->ext_ant_switch_ctrl_polarity = 0;

	switch (rfe_type->rfe_module_type) {
	case 0:
	default:
		rfe_type->ext_ant_switch_exist = true;
		rfe_type->ext_ant_switch_type =
			BT_8822B_1ANT_EXT_ANT_SWITCH_USE_SPDT;
		break;
	case 1:
		rfe_type->ext_ant_switch_exist = true;
		rfe_type->ext_ant_switch_type =
			BT_8822B_1ANT_EXT_ANT_SWITCH_USE_SPDT;
		break;
	case 2:
		rfe_type->ext_ant_switch_exist = true;
		rfe_type->ext_ant_switch_type =
			BT_8822B_1ANT_EXT_ANT_SWITCH_USE_SPDT;
		break;
	case 3:
		rfe_type->ext_ant_switch_exist = true;
		rfe_type->ext_ant_switch_type =
			BT_8822B_1ANT_EXT_ANT_SWITCH_USE_SPDT;
		break;
	case 4:
		rfe_type->ext_ant_switch_exist = true;
		rfe_type->ext_ant_switch_type =
BT_8822B_1ANT_EXT_ANT_SWITCH_USE_SPDT;
		break;
	case 5:
		rfe_type->ext_ant_switch_exist = true;
		rfe_type->ext_ant_switch_type =
BT_8822B_1ANT_EXT_ANT_SWITCH_USE_SPDT;
		break;
	case 6:
		rfe_type->ext_ant_switch_exist = true;
		rfe_type->ext_ant_switch_type =
BT_8822B_1ANT_EXT_ANT_SWITCH_USE_SPDT;
		break;
	case 7:
		rfe_type->ext_ant_switch_exist = true;
		rfe_type->ext_ant_switch_type =
BT_8822B_1ANT_EXT_ANT_SWITCH_USE_SPDT;
		break;
	}


}

/*anttenna control by bb mac bt antdiv pta to write 0x4c 0xcb4,0xcbd*/

void halbtc8822b1ant_set_ext_ant_switch(IN struct btc_coexist *btcoexist,
			IN boolean force_exec, IN u8 ctrl_type, IN u8 pos_type)
{
	struct  btc_board_info	*board_info = &btcoexist->board_info;
	boolean	switch_polatiry_inverse = false;
	u8		regval_0xcbd = 0, regval_0x64;
	u32		u32tmp1 = 0, u32tmp2 = 0, u32tmp3 = 0;
	/* Ext switch buffer mux */
		btcoexist->btc_write_1byte(btcoexist, 0x974, 0xff);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x1991, 0x3, 0x0);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcbe, 0x8, 0x0);

	if (!rfe_type->ext_ant_switch_exist)
		return;

	coex_dm->cur_ext_ant_switch_status = (ctrl_type << 8)  + pos_type;

	if (!force_exec) {
		if (coex_dm->pre_ext_ant_switch_status ==
		    coex_dm->cur_ext_ant_switch_status)
			return;
	}

	coex_dm->pre_ext_ant_switch_status = coex_dm->cur_ext_ant_switch_status;

	/* swap control polarity if use different switch control polarity*/
	/*  Normal switch polarity for SPDT, 0xcbd[1:0] = 2b'01 => Ant to BTG,  0xcbd[1:0] = 2b'10 => Ant to WLG */
	switch_polatiry_inverse = (rfe_type->ext_ant_switch_ctrl_polarity == 1 ?
			   ~switch_polatiry_inverse : switch_polatiry_inverse);


	switch (pos_type) {
	default:
	case BT_8822B_1ANT_EXT_ANT_SWITCH_TO_BT:
	case BT_8822B_1ANT_EXT_ANT_SWITCH_TO_NOCARE:

		break;
	case BT_8822B_1ANT_EXT_ANT_SWITCH_TO_WLG:
		break;
	case BT_8822B_1ANT_EXT_ANT_SWITCH_TO_WLA:
		break;
	}


	if (rfe_type->ext_ant_switch_type ==
	    BT_8822B_1ANT_EXT_ANT_SWITCH_USE_SPDT) {
		switch (ctrl_type) {
		default:
		case BT_8822B_1ANT_EXT_ANT_SWITCH_CTRL_BY_BBSW:
			btcoexist->btc_write_1byte_bitmask(
				btcoexist, 0x4e, 0x80,
				0x0);  /*  0x4c[23] = 0 */
			btcoexist->btc_write_1byte_bitmask(
				btcoexist, 0x4f, 0x01,
				0x1);  /* 0x4c[24] = 1 */
			btcoexist->btc_write_1byte_bitmask(
				btcoexist, 0xcb4, 0xff,
				0x77);	/* BB SW, DPDT use RFE_ctrl8 and RFE_ctrl9 as conctrol pin */

			regval_0xcbd = (switch_polatiry_inverse
					== false ?  0x1 :
				0x2);    /* 0xcbd[1:0] = 2b'01 for no switch_polatiry_inverse, ANTSWB =1, ANTSW =0  */
			btcoexist->btc_write_1byte_bitmask(
				btcoexist, 0xcbd, 0x3,
				regval_0xcbd);

			break;
		case BT_8822B_1ANT_EXT_ANT_SWITCH_CTRL_BY_PTA:
			btcoexist->btc_write_1byte_bitmask(
				btcoexist, 0x4e, 0x80,
				0x0);  /* 0x4c[23] = 0 */
			btcoexist->btc_write_1byte_bitmask(
				btcoexist, 0x4f, 0x01,
				0x1);  /* 0x4c[24] = 1 */
			btcoexist->btc_write_1byte_bitmask(
				btcoexist, 0xcb4, 0xff,
				0x66);	/* PTA,  DPDT use RFE_ctrl8 and RFE_ctrl9 as conctrol pin */

			regval_0xcbd = (switch_polatiry_inverse
					== false ?  0x2 :
				0x1);    /* 0xcbd[1:0] = 2b'10 for no switch_polatiry_inverse, ANTSWB =1, ANTSW =0  @ GNT_BT=1 */
			btcoexist->btc_write_1byte_bitmask(
				btcoexist, 0xcbd, 0x3,
				regval_0xcbd);

			break;
		case BT_8822B_1ANT_EXT_ANT_SWITCH_CTRL_BY_ANTDIV
				:
			btcoexist->btc_write_1byte_bitmask(
				btcoexist, 0x4e, 0x80,
				0x0);  /* 0x4c[23] = 0 */
			btcoexist->btc_write_1byte_bitmask(
				btcoexist, 0x4f, 0x01,
				0x1);  /* 0x4c[24] = 1 */
			btcoexist->btc_write_1byte_bitmask(
				btcoexist, 0xcb4, 0xff,
				0x88);  /* */

			/* no regval_0xcbd setup required, because  antenna switch control value by antenna diversity */

			break;
		case BT_8822B_1ANT_EXT_ANT_SWITCH_CTRL_BY_MAC:
			btcoexist->btc_write_1byte_bitmask(
				btcoexist, 0x4e, 0x80,
				0x1);  /*  0x4c[23] = 1 */

			regval_0x64 = (switch_polatiry_inverse
				       == false ?  0x0 :
				0x1);    /* 0x64[0] = 1b'0 for no switch_polatiry_inverse, DPDT_SEL_N =1, DPDT_SEL_P =0 */
			btcoexist->btc_write_1byte_bitmask(
				btcoexist, 0x64, 0x1,
				regval_0x64);
			break;
		case BT_8822B_1ANT_EXT_ANT_SWITCH_CTRL_BY_BT:
			btcoexist->btc_write_1byte_bitmask(
				btcoexist, 0x4e, 0x80,
				0x0);  /* 0x4c[23] = 0 */
			btcoexist->btc_write_1byte_bitmask(
				btcoexist, 0x4f, 0x01,
				0x0);  /* 0x4c[24] = 0 */

			/* no  setup required, because  antenna switch control value by BT vendor 0xac[1:0] */
			break;
		}
	}

	u32tmp1 = btcoexist->btc_read_4byte(btcoexist, 0xcbc);
	u32tmp2 = btcoexist->btc_read_4byte(btcoexist, 0x4c);
	u32tmp3 = btcoexist->btc_read_4byte(btcoexist, 0x64) & 0xff;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], ********** (After Ext Ant switch setup) 0xcbc = 0x%08x, 0x4c = 0x%08x, 0x64= 0x%02x**********\n",
		    u32tmp1, u32tmp2, u32tmp3);
	BTC_TRACE(trace_buf);


}

/*set gnt_wl gnt_bt control by sw high low , or hwpta while in power on,ini,wlan off,wlan only ,wl2g non-currrent ,wl2g current,wl5g*/

void halbtc8822b1ant_set_ant_path(IN struct btc_coexist *btcoexist,
				  IN u8 ant_pos_type, IN boolean force_exec,
				  IN u8 phase)

{
	struct  btc_board_info *board_info = &btcoexist->board_info;
	u8			u8tmp = 0;
	u32			u32tmp1 = 0, u32tmp2 = 0, u32tmp3 = 0;
	/* Ext switch buffer mux */
		btcoexist->btc_write_1byte(btcoexist, 0x974, 0xff);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x1991, 0x3, 0x0);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcbe, 0x8, 0x0);

	coex_dm->cur_ant_pos_type = (ant_pos_type << 8)  + phase;

	if (!force_exec) {
		if (coex_dm->cur_ant_pos_type ==
		    coex_dm->pre_ant_pos_type)
			return;
	}

	coex_dm->pre_ant_pos_type = coex_dm->cur_ant_pos_type;

#if 1
	u32tmp1 = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist,
			0x38);
	u32tmp2 = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist,
			0x54);
	u32tmp3 = btcoexist->btc_read_4byte(btcoexist, 0xcb4);

	u8tmp  = btcoexist->btc_read_1byte(btcoexist, 0x73);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], ********** (Before Ant Setup) 0xcb4 = 0x%x, 0x73 = 0x%x, 0x38= 0x%x, 0x54= 0x%x**********\n",
		    u32tmp3, u8tmp, u32tmp1, u32tmp2);
	BTC_TRACE(trace_buf);
#endif

	switch (phase) {
	case BT_8822B_1ANT_PHASE_COEX_INIT:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], ********** (set_ant_path - 1ANT_PHASE_COEX_INIT) **********\n");
		BTC_TRACE(trace_buf);

		/* Disable LTE Coex Function in WiFi side (this should be on if LTE coex is required) */
		halbtc8822b1ant_ltecoex_enable(btcoexist, 0x0);

		/* GNT_WL_LTE always = 1 (this should be config if LTE coex is required) */
		halbtc8822b1ant_ltecoex_set_coex_table(btcoexist,
					       BT_8822B_1ANT_CTT_WL_VS_LTE,
						       0xffff);

		/* GNT_BT_LTE always = 1 (this should be config if LTE coex is required) */
		halbtc8822b1ant_ltecoex_set_coex_table(btcoexist,
					       BT_8822B_1ANT_CTT_BT_VS_LTE,
						       0xffff);

		/* set GNT_BT to SW high */
		halbtc8822b1ant_ltecoex_set_gnt_bt(btcoexist,
					   BT_8822B_1ANT_GNT_BLOCK_RFC_BB,
						   BT_8822B_1ANT_GNT_CTRL_BY_SW,
					   BT_8822B_1ANT_SIG_STA_SET_TO_HIGH);

		/* set GNT_WL to SW low */
		halbtc8822b1ant_ltecoex_set_gnt_wl(btcoexist,
					   BT_8822B_1ANT_GNT_BLOCK_RFC_BB,
						   BT_8822B_1ANT_GNT_CTRL_BY_SW,
					   BT_8822B_1ANT_SIG_STA_SET_TO_LOW);

		/* set Path control owner to WL at initial step */
		halbtc8822b1ant_ltecoex_pathcontrol_owner(btcoexist,
				BT_8822B_1ANT_PCO_WLSIDE);

		coex_sta->run_time_state = false;

		/* Ext switch buffer mux */
		btcoexist->btc_write_1byte(btcoexist, 0x974, 0xff);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x1991,
						   0x3, 0x0);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcbe,
						   0x8, 0x0);

		if (BTC_ANT_PATH_AUTO == ant_pos_type)
			ant_pos_type = BTC_ANT_PATH_BT;

		break;
	case BT_8822B_1ANT_PHASE_WLANONLY_INIT:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], ********** (set_ant_path - 1ANT_PHASE_WLANONLY_INIT) **********\n");
		BTC_TRACE(trace_buf);

		/* Disable LTE Coex Function in WiFi side (this should be on if LTE coex is required) */
		halbtc8822b1ant_ltecoex_enable(btcoexist, 0x0);

		/* GNT_WL_LTE always = 1 (this should be config if LTE coex is required) */
		halbtc8822b1ant_ltecoex_set_coex_table(btcoexist,
					       BT_8822B_1ANT_CTT_WL_VS_LTE,
						       0xffff);

		/* GNT_BT_LTE always = 1 (this should be config if LTE coex is required) */
		halbtc8822b1ant_ltecoex_set_coex_table(btcoexist,
					       BT_8822B_1ANT_CTT_BT_VS_LTE,
						       0xffff);

		/* set GNT_BT to SW Low */
		halbtc8822b1ant_ltecoex_set_gnt_bt(btcoexist,
					   BT_8822B_1ANT_GNT_BLOCK_RFC_BB,
						   BT_8822B_1ANT_GNT_CTRL_BY_SW,
					   BT_8822B_1ANT_SIG_STA_SET_TO_LOW);

		/* Set GNT_WL to SW high */
		halbtc8822b1ant_ltecoex_set_gnt_wl(btcoexist,
					   BT_8822B_1ANT_GNT_BLOCK_RFC_BB,
						   BT_8822B_1ANT_GNT_CTRL_BY_SW,
					   BT_8822B_1ANT_SIG_STA_SET_TO_HIGH);

		/* set Path control owner to WL at initial step */
		halbtc8822b1ant_ltecoex_pathcontrol_owner(btcoexist,
				BT_8822B_1ANT_PCO_WLSIDE);

		coex_sta->run_time_state = false;

		/* Ext switch buffer mux */
		btcoexist->btc_write_1byte(btcoexist, 0x974, 0xff);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x1991,
						   0x3, 0x0);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcbe,
						   0x8, 0x0);

		if (BTC_ANT_PATH_AUTO == ant_pos_type)
			ant_pos_type = BTC_ANT_PATH_WIFI;

		break;
	case BT_8822B_1ANT_PHASE_WLAN_OFF:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], ********** (set_ant_path - 1ANT_PHASE_WLAN_OFF) **********\n");
		BTC_TRACE(trace_buf);

		/* Disable LTE Coex Function in WiFi side */
		halbtc8822b1ant_ltecoex_enable(btcoexist, 0x0);

		/* set Path control owner to BT */
		halbtc8822b1ant_ltecoex_pathcontrol_owner(btcoexist,
				BT_8822B_1ANT_PCO_BTSIDE);

		/* Set Ext Ant Switch to BT control at wifi off step */
		halbtc8822b1ant_set_ext_ant_switch(btcoexist,
						   FORCE_EXEC,
				   BT_8822B_1ANT_EXT_ANT_SWITCH_CTRL_BY_BT,
				   BT_8822B_1ANT_EXT_ANT_SWITCH_TO_NOCARE);

		coex_sta->run_time_state = false;

		break;
	case BT_8822B_1ANT_PHASE_2G_RUNTIME:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], ********** (set_ant_path - 1ANT_PHASE_2G_RUNTIME) **********\n");
		BTC_TRACE(trace_buf);

		/* set GNT_BT to PTA */
		halbtc8822b1ant_ltecoex_set_gnt_bt(btcoexist,
					   BT_8822B_1ANT_GNT_BLOCK_RFC_BB,
					   BT_8822B_1ANT_GNT_CTRL_BY_PTA,
					   BT_8822B_1ANT_SIG_STA_SET_BY_HW);

		/* Set GNT_WL to PTA */
		halbtc8822b1ant_ltecoex_set_gnt_wl(btcoexist,
					   BT_8822B_1ANT_GNT_BLOCK_RFC_BB,
					   BT_8822B_1ANT_GNT_CTRL_BY_PTA,
					   BT_8822B_1ANT_SIG_STA_SET_BY_HW);

		/* set Path control owner to WL at runtime step */
		halbtc8822b1ant_ltecoex_pathcontrol_owner(btcoexist,
				BT_8822B_1ANT_PCO_WLSIDE);

		coex_sta->run_time_state = true;

		if (BTC_ANT_PATH_AUTO == ant_pos_type)
			ant_pos_type = BTC_ANT_PATH_PTA;

		break;
	case BT_8822B_1ANT_PHASE_5G_RUNTIME:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], ********** (set_ant_path - 1ANT_PHASE_5G_RUNTIME) **********\n");
		BTC_TRACE(trace_buf);

		/* set GNT_BT to SW Hi */
		halbtc8822b1ant_ltecoex_set_gnt_bt(btcoexist,
					   BT_8822B_1ANT_GNT_BLOCK_RFC_BB,
						   BT_8822B_1ANT_GNT_CTRL_BY_SW,
					   BT_8822B_1ANT_SIG_STA_SET_TO_HIGH);

		/* Set GNT_WL to SW Hi */
		halbtc8822b1ant_ltecoex_set_gnt_wl(btcoexist,
					   BT_8822B_1ANT_GNT_BLOCK_RFC_BB,
						   BT_8822B_1ANT_GNT_CTRL_BY_SW,
					   BT_8822B_1ANT_SIG_STA_SET_TO_HIGH);

		/* set Path control owner to WL at runtime step */
		halbtc8822b1ant_ltecoex_pathcontrol_owner(btcoexist,
				BT_8822B_1ANT_PCO_WLSIDE);

		coex_sta->run_time_state = true;

		if (BTC_ANT_PATH_AUTO == ant_pos_type)
			ant_pos_type =
				BTC_ANT_PATH_WIFI5G;

		break;
	case BT_8822B_1ANT_PHASE_BTMPMODE:
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], ********** (set_ant_path - 1ANT_PHASE_BTMPMODE) **********\n");
		BTC_TRACE(trace_buf);

		/* Disable LTE Coex Function in WiFi side */
		halbtc8822b1ant_ltecoex_enable(btcoexist, 0x0);

		/* set GNT_BT to SW Hi */
		halbtc8822b1ant_ltecoex_set_gnt_bt(btcoexist,
					   BT_8822B_1ANT_GNT_BLOCK_RFC_BB,
						   BT_8822B_1ANT_GNT_CTRL_BY_SW,
					   BT_8822B_1ANT_SIG_STA_SET_TO_HIGH);

		/* Set GNT_WL to SW Lo */
		halbtc8822b1ant_ltecoex_set_gnt_wl(btcoexist,
					   BT_8822B_1ANT_GNT_BLOCK_RFC_BB,
						   BT_8822B_1ANT_GNT_CTRL_BY_SW,
					   BT_8822B_1ANT_SIG_STA_SET_TO_LOW);

		/* set Path control owner to WL */
		halbtc8822b1ant_ltecoex_pathcontrol_owner(btcoexist,
				BT_8822B_1ANT_PCO_WLSIDE);

		coex_sta->run_time_state = false;

		/* Set Ext Ant Switch to BT side at BT MP mode */
		if (BTC_ANT_PATH_AUTO == ant_pos_type)
			ant_pos_type = BTC_ANT_PATH_BT;

		break;
	}


	if (phase != BT_8822B_1ANT_PHASE_WLAN_OFF) {
		switch (ant_pos_type) {
		case BTC_ANT_PATH_WIFI:
			halbtc8822b1ant_set_ext_ant_switch(
				btcoexist,
				force_exec,
				BT_8822B_1ANT_EXT_ANT_SWITCH_CTRL_BY_BBSW,
				BT_8822B_1ANT_EXT_ANT_SWITCH_TO_WLG);
			break;
		case BTC_ANT_PATH_WIFI5G
				:
			halbtc8822b1ant_set_ext_ant_switch(
				btcoexist,
				force_exec,
				BT_8822B_1ANT_EXT_ANT_SWITCH_CTRL_BY_BBSW,
				BT_8822B_1ANT_EXT_ANT_SWITCH_TO_WLA);
			break;
		case BTC_ANT_PATH_BT:
			halbtc8822b1ant_set_ext_ant_switch(
				btcoexist,
				force_exec,
				BT_8822B_1ANT_EXT_ANT_SWITCH_CTRL_BY_BBSW,
				BT_8822B_1ANT_EXT_ANT_SWITCH_TO_BT);
			break;
		default:
		case BTC_ANT_PATH_PTA:
			halbtc8822b1ant_set_ext_ant_switch(
				btcoexist,
				force_exec,
				BT_8822B_1ANT_EXT_ANT_SWITCH_CTRL_BY_PTA,
				BT_8822B_1ANT_EXT_ANT_SWITCH_TO_NOCARE);
			break;
		}

	}
#if 1
	u32tmp1 = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist, 0x38);
	u32tmp2 = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist, 0x54);
	u32tmp3 = btcoexist->btc_read_4byte(btcoexist, 0xcb4);

	u8tmp  = btcoexist->btc_read_1byte(btcoexist, 0x73);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], ********** (After Ant Setup) 0xcb4 = 0x%x, 0x73 = 0x%x, 0x38= 0x%x, 0x54= 0x%x**********\n",
		    u32tmp3, u8tmp, u32tmp1, u32tmp2);
	BTC_TRACE(trace_buf);

#endif

}


void halbtc8822b1ant_coex_all_off(IN struct btc_coexist *btcoexist)
{
	/* sw all off */
	halbtc8822b1ant_sw_mechanism(btcoexist, false);

	/* hw all off */
	halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
}
boolean halbtc8822b1ant_is_common_action(IN struct btc_coexist *btcoexist)
{
	boolean			common = false, wifi_connected = false, wifi_busy = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	if (!wifi_connected &&
	    BT_8822B_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
	    coex_dm->bt_status) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Wifi non connected-idle + BT non connected-idle!!\n");
		BTC_TRACE(trace_buf);

		/* halbtc8822b1ant_sw_mechanism(btcoexist, false); */

		common = true;
	} else if (wifi_connected &&
		   (BT_8822B_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
		    coex_dm->bt_status)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Wifi connected + BT non connected-idle!!\n");
		BTC_TRACE(trace_buf);

		/* halbtc8822b1ant_sw_mechanism(btcoexist, false); */

		common = true;
	} else if (!wifi_connected &&
		   (BT_8822B_1ANT_BT_STATUS_CONNECTED_IDLE ==
		    coex_dm->bt_status)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Wifi non connected-idle + BT connected-idle!!\n");
		BTC_TRACE(trace_buf);

		/* halbtc8822b1ant_sw_mechanism(btcoexist, false); */

		common = true;
	} else if (wifi_connected &&
		   (BT_8822B_1ANT_BT_STATUS_CONNECTED_IDLE ==
		    coex_dm->bt_status)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Wifi connected + BT connected-idle!!\n");
		BTC_TRACE(trace_buf);

		/* halbtc8822b1ant_sw_mechanism(btcoexist, false); */

		common = true;
	} else if (!wifi_connected &&
		   (BT_8822B_1ANT_BT_STATUS_CONNECTED_IDLE !=
		    coex_dm->bt_status)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Wifi non connected-idle + BT Busy!!\n");
		BTC_TRACE(trace_buf);

		/* halbtc8822b1ant_sw_mechanism(btcoexist, false); */

		common = true;
	} else {
		if (wifi_busy) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Wifi Connected-Busy + BT Busy!!\n");
			BTC_TRACE(trace_buf);
		} else {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Wifi Connected-Idle + BT Busy!!\n");
			BTC_TRACE(trace_buf);
		}

		common = false;
	}

	return common;
}

void halbtc8822b1ant_action_wifi_under5g(IN struct btc_coexist *btcoexist)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], under 5g start\n");
	BTC_TRACE(trace_buf);
	/* for test : s3 bt disappear , fail rate 1/600*/
/*
	halbtc8822b1ant_ignore_wlan_act(btcoexist, NORMAL_EXEC, true);
*/
/*set sw gnt wl bt  high*/
	halbtc8822b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO, FORCE_EXEC,
							 BT_8822B_1ANT_PHASE_5G_RUNTIME);

	halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
	halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcbd,
						   0x3, 1);
/*
	halbtc8822b1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);

	halbtc8822b1ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 5);
*/
}



void halbtc8822b1ant_action_wifi_only(IN struct btc_coexist *btcoexist)
{
	boolean wifi_under_5g = false, rf4ce_enabled = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);
	if (wifi_under_5g) {
		halbtc8822b1ant_action_wifi_under5g(btcoexist);

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], ********** (wlan only -- under 5g ) **********\n");
		BTC_TRACE(trace_buf);
		return;
	}

	if (rf4ce_enabled) {
				btcoexist->btc_write_1byte_bitmask(
					btcoexist, 0x45e, 0x8, 0x1);

				halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true,
							50);

				halbtc8822b1ant_coex_table_with_type(btcoexist,
				NORMAL_EXEC, 1);
		return;
		}
	halbtc8822b1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 0);
	halbtc8822b1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 8);

	halbtc8822b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO, FORCE_EXEC,
				     BT_8822B_1ANT_PHASE_2G_RUNTIME);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], ********** (wlan only -- under 2g ) **********\n");
	BTC_TRACE(trace_buf);

}

/* *********************************************
 *
 *	Software Coex Mechanism start
 *
 * ********************************************* */

/* SCO only or SCO+PAN(HS) */

/*
void halbtc8822b1ant_action_sco(IN struct btc_coexist* btcoexist)
{
	halbtc8822b1ant_sw_mechanism(btcoexist, true);
}


void halbtc8822b1ant_action_hid(IN struct btc_coexist* btcoexist)
{
	halbtc8822b1ant_sw_mechanism(btcoexist, true);
}


void halbtc8822b1ant_action_a2dp(IN struct btc_coexist* btcoexist)
{
	halbtc8822b1ant_sw_mechanism(btcoexist, false);
}

void halbtc8822b1ant_action_a2dp_pan_hs(IN struct btc_coexist* btcoexist)
{
	halbtc8822b1ant_sw_mechanism(btcoexist, false);
}

void halbtc8822b1ant_action_pan_edr(IN struct btc_coexist* btcoexist)
{
	halbtc8822b1ant_sw_mechanism(btcoexist, false);
}


void halbtc8822b1ant_action_pan_hs(IN struct btc_coexist* btcoexist)
{
	halbtc8822b1ant_sw_mechanism(btcoexist, false);
}


void halbtc8822b1ant_action_pan_edr_a2dp(IN struct btc_coexist* btcoexist)
{
	halbtc8822b1ant_sw_mechanism(btcoexist, false);
}

void halbtc8822b1ant_action_pan_edr_hid(IN struct btc_coexist* btcoexist)
{
	halbtc8822b1ant_sw_mechanism(btcoexist, true);
}


void halbtc8822b1ant_action_hid_a2dp_pan_edr(IN struct btc_coexist* btcoexist)
{
	halbtc8822b1ant_sw_mechanism(btcoexist, true);
}

void halbtc8822b1ant_action_hid_a2dp(IN struct btc_coexist* btcoexist)
{
	halbtc8822b1ant_sw_mechanism(btcoexist, true);
}

*/

/* *********************************************
 *
 *	Non-Software Coex Mechanism start
 *
 * ********************************************* */
void halbtc8822b1ant_action_bt_whck_test(IN struct btc_coexist *btcoexist)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex],action_bt_whck_test\n");
	BTC_TRACE(trace_buf);

	halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);

	halbtc8822b1ant_set_ant_path(btcoexist,
				     BTC_ANT_PATH_AUTO,
				     NORMAL_EXEC,
				     BT_8822B_1ANT_PHASE_2G_RUNTIME);

	halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
}

void halbtc8822b1ant_action_wifi_multi_port(IN struct btc_coexist *btcoexist)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex],action_wifi_multi_port\n");
	BTC_TRACE(trace_buf);

	halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);

	halbtc8822b1ant_set_ant_path(btcoexist,
				     BTC_ANT_PATH_AUTO,
				     NORMAL_EXEC,
				     BT_8822B_1ANT_PHASE_2G_RUNTIME);

	halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
}

void halbtc8822b1ant_action_hs(IN struct btc_coexist *btcoexist)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], action_hs\n");
	BTC_TRACE(trace_buf);

	halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 5);

	halbtc8822b1ant_set_ant_path(btcoexist,
				     BTC_ANT_PATH_AUTO,
				     NORMAL_EXEC,
				     BT_8822B_1ANT_PHASE_2G_RUNTIME);

	halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 5);
}

/*"""bt inquiry"""" + wifi any + bt any*/
void halbtc8822b1ant_action_bt_inquiry(IN struct btc_coexist *btcoexist)
{

	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean			wifi_connected = false, ap_enable = false, wifi_busy = false,
				bt_busy = false, rf4ce_enabled = false;


	boolean		wifi_scan = false, link = false, roam = false;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], ********** (bt inquiry) **********\n");
	BTC_TRACE(trace_buf);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE,
			   &ap_enable);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bt_busy);


	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &wifi_scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);


	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], ********** scan = %d,  link =%d, roam = %d**********\n",
		    wifi_scan, link, roam);
	BTC_TRACE(trace_buf);

	if ((link) || (roam) || (coex_sta->wifi_is_high_pri_task)) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], ********** (bt inquiry wifi  connect or scan ) **********\n");
		BTC_TRACE(trace_buf);

		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 1);

		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 6);

	} else if ((wifi_scan) && (coex_sta->bt_create_connection)) {

		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 22);
		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 6);

	} else if ((!wifi_connected) && (!wifi_scan)) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], ********** (bt inquiry wifi non connect) **********\n");
		BTC_TRACE(trace_buf);

		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);

		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

	} else if ((bt_link_info->a2dp_exist) && (bt_link_info->pan_exist)) {

		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 22);
		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
	} else if (bt_link_info->a2dp_exist) {

		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 32);

		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 3);
	} else if (wifi_scan) {

		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 20);

		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
	} else if (wifi_busy) {

		/* for BT inquiry/page fail after S4 resume */
		/* halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 20);		 */
		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 32);
/*aaaa->55aa for bt connect while wl busy*/
		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     15);
	if (rf4ce_enabled) {
				btcoexist->btc_write_1byte_bitmask(
					btcoexist, 0x45e, 0x8, 0x1);

				halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true,
							50);

				halbtc8822b1ant_coex_table_with_type(btcoexist,
				NORMAL_EXEC, 0);

		}
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], ********** (bt inquiry wifi connect) **********\n");
		BTC_TRACE(trace_buf);

		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 22);

		halbtc8822b1ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_AUTO,
					     NORMAL_EXEC,
					     BT_8822B_1ANT_PHASE_2G_RUNTIME);

		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     4);
	}

	/*
		if ((wifi_link) || (wifi_roam) || (coex_sta->wifi_is_high_pri_task)) {

			halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 33);
			halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 6);

		} else if ((wifi_scan) && (coex_sta->bt_create_connection)) {

			halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 22);
			halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 6);

		} else if ((!wifi_connected) && (!wifi_scan)) {

			halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);

			halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
		} else if ((bt_link_info->a2dp_exist) && (bt_link_info->pan_exist)) {

			halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 22);
			halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);

		} else if (bt_link_info->a2dp_exist) {

			halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 32);

			halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
		} else if (wifi_scan) {

			halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 20);

			halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);

		} else if (wifi_busy) {
			halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 21);

			halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
		} else {
			halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 19);

			halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
		}
	*/
}

void halbtc8822b1ant_action_bt_sco_hid_only_busy(IN struct btc_coexist
		*btcoexist, IN u8 wifi_status)
{

	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x45e, 0x8, 0x1);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], ********** (bt sco hid only busy) **********\n");
	BTC_TRACE(trace_buf);

	/*SCO  + wifi connected idle or busy / 0x778=1@wifi slot*/
	if (bt_link_info->sco_exist) {

		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x45e, 0x8, 0x1);
		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 5);
		/*case16 for connect SCO first then connect a2sp fail issue*/
		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 16);

	} else { /* HID  + wifi connected idle or busy / 0x778=1@wifi slot */

		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x45e, 0x8, 0x1);

		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 5);
		/*case16 for connect HID first then connect a2sp fail issue*/
		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 16);

	}
}

/*wifi connected + bt acl busy*/
void halbtc8822b1ant_action_wifi_connected_bt_acl_busy(IN struct btc_coexist
		*btcoexist, IN u8 wifi_status)
{

	u8		bt_rssi_state;
	struct	btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean	wifi_busy = false, wifi_turbo = false;
	u32  wifi_bw = 1;

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW,
				   &wifi_bw);
btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM,
				   &coex_sta->scan_ap_num);
	bt_rssi_state = halbtc8822b1ant_bt_rssi_state(2, 28, 0);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
"[BTCoex], ********** (wifi connect acl busy) **********\n");
	BTC_TRACE(trace_buf);



	if (bt_link_info->hid_only) { /* HID  + wifi connected idle or busy / 0x778=1@wifi slot */




		btcoexist->btc_write_1byte_bitmask(
			btcoexist, 0x45e, 0x8, 0x1);



		halbtc8822b1ant_action_bt_sco_hid_only_busy(btcoexist,
				wifi_status);
		return;
	} else if (
		bt_link_info->a2dp_only) { /* A2DP  + wifi connected idle or busy */
		if (BT_8822B_1ANT_WIFI_STATUS_CONNECTED_IDLE == wifi_status) {


			btcoexist->btc_write_1byte_bitmask(btcoexist, 0x45e,
							   0x8, 0x1);

			halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						32);
			halbtc8822b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 0);
		} else {
			if (coex_sta->scan_ap_num >=
			    BT_8822B_1ANT_WIFI_NOISY_THRESH)  {
				halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true,
							17);
			}	else {




				btcoexist->btc_write_1byte_bitmask(
					btcoexist, 0x45e, 0x8, 0x1);

				halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true,
							42);

				halbtc8822b1ant_coex_table_with_type(btcoexist,
				NORMAL_EXEC, 1);
		}
/*
			halbtc8822b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 1);
*/
			coex_dm->auto_tdma_adjust = true;
		}

		/* A2DP+PAN(OPP,FTP), HID+A2DP+PAN(OPP,FTP) */
	} else if (((bt_link_info->a2dp_exist) && (bt_link_info->pan_exist)) ||
		   (bt_link_info->hid_exist && bt_link_info->a2dp_exist &&
		bt_link_info->pan_exist)) { /* A2DP+PAN(OPP,FTP), HID+A2DP+PAN(OPP,FTP) */

		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x45e, 0x8, 0x1);
		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 47);
		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     6);
	} else if (bt_link_info->hid_exist &&
		   bt_link_info->a2dp_exist) { /* HID+A2DP */

		btcoexist->btc_write_1byte_bitmask(
			btcoexist, 0x45e, 0x8, 0x1);
		if (!wifi_busy) {
/*a2dp glitch while wl idle , change the 0x6c0=5a5a5a5a->5555or aaaa55aa*/
		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
								true,
								32);
			} else if (wifi_bw == 0) { /* 11bg mode */
			halbtc8822b1ant_coex_table_with_type(btcoexist,
										 NORMAL_EXEC, 1);

		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
								true,
								42);
		} else {
		/*if in open space , and ask wl tp , 0x60=6135031111,55555555,aaaa55aa*/
		halbtc8822b1ant_coex_table_with_type(btcoexist,
										 NORMAL_EXEC, 1);
		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 42);
}
		/*
				halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
								     1);
		*/




	} else if ((bt_link_info->pan_only) || (bt_link_info->hid_exist &&
		bt_link_info->pan_exist)) { /* PAN(OPP,FTP), HID+PAN(OPP,FTP)*/
		/*
		if (BT_8723D_1ANT_WIFI_STATUS_CONNECTED_IDLE == wifi_status)
			halbtc8723d1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						4);
		else
		*/

		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x45e, 0x8, 0x1);
		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 47);
		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     1);
	} else {
		/* BT no-profile busy (0x9) */

		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x45e, 0x8, 0x1);
		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 33);
		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     1);
	}
}

/*wifi not connected + bt action*/
void halbtc8822b1ant_action_wifi_not_connected(IN struct btc_coexist *btcoexist)
{
	boolean wifi_under_5g = false, rf4ce_enabled = false;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], ********** (wifi not connect) **********\n");
	BTC_TRACE(trace_buf);

	/* tdma and coex table */
	if (rf4ce_enabled) {
				btcoexist->btc_write_1byte_bitmask(
					btcoexist, 0x45e, 0x8, 0x1);

				halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true,
							50);

				halbtc8822b1ant_coex_table_with_type(btcoexist,
				NORMAL_EXEC, 1);
		return;
		}
	halbtc8822b1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 8);

	halbtc8822b1ant_set_ant_path(btcoexist,
				     BTC_ANT_PATH_AUTO,
				     NORMAL_EXEC,
				     BT_8822B_1ANT_PHASE_2G_RUNTIME);

	halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
}

/*""""wl not connected scan"""" + bt action*/
void halbtc8822b1ant_action_wifi_not_connected_scan(IN struct btc_coexist
		*btcoexist)
{

	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean wifi_connected = false, bt_hs_on = false;
	u32	wifi_link_status = 0;
	u32	num_of_wifi_link = 0;
	boolean	bt_ctrl_agg_buf_size = false;
	u8	agg_buf_size = 5;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], ********** (wifi non connect scan) **********\n");
	BTC_TRACE(trace_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
			   &wifi_link_status);

	num_of_wifi_link = wifi_link_status >> 16;

	if (num_of_wifi_link >= 2) {
		halbtc8822b1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);
		halbtc8822b1ant_limited_rx(btcoexist, NORMAL_EXEC, false,
					   bt_ctrl_agg_buf_size, agg_buf_size);

		if (coex_sta->c2h_bt_inquiry_page) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"############# [BTCoex],  BT Is Inquirying\n");
			BTC_TRACE(trace_buf);
			halbtc8822b1ant_action_bt_inquiry(btcoexist);
		} else
			halbtc8822b1ant_action_wifi_multi_port(btcoexist);
		return;
	}

	if (coex_sta->c2h_bt_inquiry_page) {
		halbtc8822b1ant_action_bt_inquiry(btcoexist);
		return;
	} else if (bt_hs_on) {
		halbtc8822b1ant_action_hs(btcoexist);
		return;
	}

	/* tdma and coex table */
	if (BT_8822B_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) {
		if (bt_link_info->a2dp_exist) {
			halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						32);
			halbtc8822b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 1);
		} else if (bt_link_info->a2dp_exist &&
			   bt_link_info->pan_exist) {
			halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						22);
			halbtc8822b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 1);
		} else {
			halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						20);
			halbtc8822b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 1);
		}
	} else if ((BT_8822B_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
		   (BT_8822B_1ANT_BT_STATUS_ACL_SCO_BUSY ==
		    coex_dm->bt_status)) {
		halbtc8822b1ant_action_bt_sco_hid_only_busy(btcoexist,
				BT_8822B_1ANT_WIFI_STATUS_CONNECTED_SCAN);
	} else {

		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);

		halbtc8822b1ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_AUTO,
					     NORMAL_EXEC,
					     BT_8822B_1ANT_PHASE_2G_RUNTIME);

		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     5);
	}
}

/*""""wl not connected asso"""" + bt action*/
void halbtc8822b1ant_action_wifi_not_connected_asso_auth(
	IN struct btc_coexist *btcoexist)
{

	struct	btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean wifi_connected = false, bt_hs_on = false;
	u32 wifi_link_status = 0;
	u32 num_of_wifi_link = 0;
	boolean bt_ctrl_agg_buf_size = false;
	u8	agg_buf_size = 5;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], ********** (wifi non connect asso_auth) **********\n");
	BTC_TRACE(trace_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
			   &wifi_link_status);

	num_of_wifi_link = wifi_link_status >> 16;

	if (num_of_wifi_link >= 2) {

		halbtc8822b1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);
		halbtc8822b1ant_limited_rx(btcoexist, NORMAL_EXEC, false,
					   bt_ctrl_agg_buf_size,
					   agg_buf_size);

		if (coex_sta->c2h_bt_inquiry_page) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"############# [BTCoex],  BT Is Inquirying\n");
			BTC_TRACE(trace_buf);
			halbtc8822b1ant_action_bt_inquiry(btcoexist);
		} else
			halbtc8822b1ant_action_wifi_multi_port(
				btcoexist);
		return;
	}

	if (coex_sta->c2h_bt_inquiry_page) {
		halbtc8822b1ant_action_bt_inquiry(btcoexist);
		return;
	} else if (bt_hs_on) {
		halbtc8822b1ant_action_hs(btcoexist);
		return;
	}


	/* tdma and coex table */
	if ((bt_link_info->sco_exist)  || (bt_link_info->hid_exist) ||
	    (bt_link_info->a2dp_exist)) {

		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 32);
		halbtc8822b1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 4);
	} else if (bt_link_info->pan_exist) {
		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 20);
		halbtc8822b1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 4);
	} else {

		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);

		halbtc8822b1ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_AUTO,
					     NORMAL_EXEC,
					     BT_8822B_1ANT_PHASE_2G_RUNTIME);

		halbtc8822b1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 2);
	}
}

/*""""wl  connected scan"""" + bt action*/
void halbtc8822b1ant_action_wifi_connected_scan(IN struct btc_coexist
		*btcoexist)
{

	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean wifi_connected = false, bt_hs_on = false;
	u32	wifi_link_status = 0;
	u32	num_of_wifi_link = 0;
	boolean	bt_ctrl_agg_buf_size = false;
	u8	agg_buf_size = 5;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], ********** (wifi connect scan) **********\n");
	BTC_TRACE(trace_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
			   &wifi_link_status);

	num_of_wifi_link = wifi_link_status >> 16;

	if (num_of_wifi_link >= 2) {
		halbtc8822b1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);
		halbtc8822b1ant_limited_rx(btcoexist, NORMAL_EXEC, false,
					   bt_ctrl_agg_buf_size, agg_buf_size);

		if (coex_sta->c2h_bt_inquiry_page) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"############# [BTCoex],  BT Is Inquirying\n");
			BTC_TRACE(trace_buf);
			halbtc8822b1ant_action_bt_inquiry(btcoexist);
		} else
			halbtc8822b1ant_action_wifi_multi_port(btcoexist);
		return;
	}

	if (coex_sta->c2h_bt_inquiry_page) {
		halbtc8822b1ant_action_bt_inquiry(btcoexist);
		return;
	} else if (bt_hs_on) {
		halbtc8822b1ant_action_hs(btcoexist);
		return;
	}

	/* tdma and coex table */
	if (BT_8822B_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) {
		if (bt_link_info->a2dp_exist) {
			halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						32);
			halbtc8822b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 1);
		} else if (bt_link_info->a2dp_exist &&
			   bt_link_info->pan_exist) {
			halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						22);
			halbtc8822b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 1);
		} else {
			halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						20);
			halbtc8822b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 1);
		}
	} else if ((BT_8822B_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
		   (BT_8822B_1ANT_BT_STATUS_ACL_SCO_BUSY ==
		    coex_dm->bt_status)) {
		halbtc8822b1ant_action_bt_sco_hid_only_busy(btcoexist,
				BT_8822B_1ANT_WIFI_STATUS_CONNECTED_SCAN);
	} else {

		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);

		halbtc8822b1ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_AUTO,
					     NORMAL_EXEC,
					     BT_8822B_1ANT_PHASE_2G_RUNTIME);

		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     6);
	}
}

/*""""wl  connected specific packet"""" + bt action*/
void halbtc8822b1ant_action_wifi_connected_specific_packet(
	IN struct btc_coexist *btcoexist)
{

	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean wifi_connected = false, bt_hs_on = false;
	u32	wifi_link_status = 0;
	u32	num_of_wifi_link = 0;
	boolean	bt_ctrl_agg_buf_size = false;
	u8	agg_buf_size = 5;
	boolean wifi_busy = false;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], ********** (wifi connect specific packet) **********\n");
	BTC_TRACE(trace_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_LINK_STATUS,
			   &wifi_link_status);

	num_of_wifi_link = wifi_link_status >> 16;

	if (num_of_wifi_link >= 2) {
		halbtc8822b1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);
		halbtc8822b1ant_limited_rx(btcoexist, NORMAL_EXEC, false,
					   bt_ctrl_agg_buf_size, agg_buf_size);

		if (coex_sta->c2h_bt_inquiry_page) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"############# [BTCoex],  BT Is Inquirying\n");
			BTC_TRACE(trace_buf);
			halbtc8822b1ant_action_bt_inquiry(btcoexist);
		} else
			halbtc8822b1ant_action_wifi_multi_port(btcoexist);
		return;
	}

	if (coex_sta->c2h_bt_inquiry_page) {
		halbtc8822b1ant_action_bt_inquiry(btcoexist);
		return;
	} else if (bt_hs_on) {
		halbtc8822b1ant_action_hs(btcoexist);
		return;
	}


	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	/* no specific packet process for both WiFi and BT very busy */
	if ((wifi_busy) && ((bt_link_info->pan_exist) ||
			    (coex_sta->num_of_profile >= 2)))
		return;

	/* tdma and coex table */
	if ((bt_link_info->sco_exist) || (bt_link_info->hid_exist)) {
		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 32);
		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 5);
	} else if (bt_link_info->a2dp_exist) {
		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 32);
		/*for a2dp glitch,change from 1 to 15*/
		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     15);
	} else if (bt_link_info->pan_exist) {
		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 20);
		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     1);
	} else {

		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);

		halbtc8822b1ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_AUTO,
					     NORMAL_EXEC,
					     BT_8822B_1ANT_PHASE_2G_RUNTIME);

		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     5);
	}
}

/*wifi connected input point : to set different ps and tdma case (+bt different case)*/
void halbtc8822b1ant_action_wifi_connected(IN struct btc_coexist *btcoexist)
{

	boolean	wifi_busy = false, rf4ce_enabled = false;
	boolean		scan = false, link = false, roam = false;
	boolean		under_4way = false, ap_enable = false, wifi_under_5g = false;
	u8 wifi_rssi_state;


	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], CoexForWifiConnect()===>\n");
	BTC_TRACE(trace_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);

	if (wifi_under_5g) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], CoexForWifiConnect(), return for wifi is under 5g<===\n");
		BTC_TRACE(trace_buf);

		halbtc8822b1ant_action_wifi_under5g(btcoexist);

		return;
	}

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], CoexForWifiConnect(), return for wifi is under 2g<===\n");
	BTC_TRACE(trace_buf);

	halbtc8822b1ant_set_ant_path(btcoexist,
				     BTC_ANT_PATH_AUTO,
				     NORMAL_EXEC,
				     BT_8822B_1ANT_PHASE_2G_RUNTIME);


	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS,
			   &under_4way);

	if (under_4way) {
		halbtc8822b1ant_action_wifi_connected_specific_packet(
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
			halbtc8822b1ant_action_wifi_connected_scan(btcoexist);
		else
			halbtc8822b1ant_action_wifi_connected_specific_packet(
				btcoexist);
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], CoexForWifiConnect(), return for wifi is under scan<===\n");
		BTC_TRACE(trace_buf);
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE,
			   &ap_enable);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	/* tdma and coex table */
	if (!wifi_busy) {
		if (BT_8822B_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) {
			halbtc8822b1ant_action_wifi_connected_bt_acl_busy(
				btcoexist,
				BT_8822B_1ANT_WIFI_STATUS_CONNECTED_IDLE);
		} else if ((BT_8822B_1ANT_BT_STATUS_SCO_BUSY ==
			    coex_dm->bt_status) ||
			   (BT_8822B_1ANT_BT_STATUS_ACL_SCO_BUSY ==
			    coex_dm->bt_status)) {
			halbtc8822b1ant_action_bt_sco_hid_only_busy(btcoexist,
				BT_8822B_1ANT_WIFI_STATUS_CONNECTED_IDLE);
		} else {

			halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false,
						8);

			halbtc8822b1ant_set_ant_path(btcoexist,
						     BTC_ANT_PATH_AUTO,
						     NORMAL_EXEC,
					     BT_8822B_1ANT_PHASE_2G_RUNTIME);

			if ((coex_sta->high_priority_tx) +
			    (coex_sta->high_priority_rx) <= 60)
				/*sy modify case16 -> case17*/
				halbtc8822b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 1);
			else
				halbtc8822b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 1);
		}
	} else {
		if (BT_8822B_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) {
			halbtc8822b1ant_action_wifi_connected_bt_acl_busy(
				btcoexist,
				BT_8822B_1ANT_WIFI_STATUS_CONNECTED_BUSY);
		} else if ((BT_8822B_1ANT_BT_STATUS_SCO_BUSY ==
			    coex_dm->bt_status) ||
			   (BT_8822B_1ANT_BT_STATUS_ACL_SCO_BUSY ==
			    coex_dm->bt_status)) {
			halbtc8822b1ant_action_bt_sco_hid_only_busy(btcoexist,
				BT_8822B_1ANT_WIFI_STATUS_CONNECTED_BUSY);
		} else {
				if (rf4ce_enabled) {
				btcoexist->btc_write_1byte_bitmask(
					btcoexist, 0x45e, 0x8, 0x1);

				halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true,
							50);

				halbtc8822b1ant_coex_table_with_type(btcoexist,
				NORMAL_EXEC, 1);
		return;
		}



			halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false,
						8);

			halbtc8822b1ant_set_ant_path(btcoexist,
						     BTC_ANT_PATH_AUTO,
						     NORMAL_EXEC,
					     BT_8822B_1ANT_PHASE_2G_RUNTIME);

			/*

						halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
											32);

						halbtc8822b1ant_set_ant_path(btcoexist,
						BTC_ANT_PATH_AUTO, NORMAL_EXEC, BT_8822B_1ANT_PHASE_2G_RUNTIME);
			*/



			wifi_rssi_state = halbtc8822b1ant_wifi_rssi_state(
						  btcoexist, 1, 2, 25, 0);

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], ********** before  **********\n");
			BTC_TRACE(trace_buf);
/*
			if ((BT_8822B_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
			     coex_dm->bt_status)  &&
			    (coex_sta->scan_ap_num <= 3) &&
			    (wifi_rssi_state == BTC_RSSI_STATE_LOW ||
			     wifi_rssi_state == BTC_RSSI_STATE_STAY_LOW)) {
*/
			if (BT_8822B_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
			coex_dm->bt_status) {
					if (rf4ce_enabled) {
				btcoexist->btc_write_1byte_bitmask(
					btcoexist, 0x45e, 0x8, 0x1);

				halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true,
							50);

				halbtc8822b1ant_coex_table_with_type(btcoexist,
				NORMAL_EXEC, 1);
		return;
		}


				halbtc8822b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 7);
			} else

				halbtc8822b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 1);



			/*
						else if ((coex_sta->high_priority_tx +
										 coex_sta->high_priority_rx) <= 60)
							halbtc8822b1ant_coex_table_with_type(btcoexist,
										     NORMAL_EXEC, 1);
						else
							halbtc8822b1ant_coex_table_with_type(btcoexist,
										     NORMAL_EXEC, 4);
			*/
		}
	}
}

void halbtc8822b1ant_run_sw_coexist_mechanism(IN struct btc_coexist *btcoexist)
{

	u8				algorithm = 0;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], ********** (runswcoexmech) **********\n");
	BTC_TRACE(trace_buf);
	algorithm = halbtc8822b1ant_action_algorithm(btcoexist);
	coex_dm->cur_algorithm = algorithm;

	if (halbtc8822b1ant_is_common_action(btcoexist)) {

	} else {
		switch (coex_dm->cur_algorithm) {
		case BT_8822B_1ANT_COEX_ALGO_SCO:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Action algorithm = SCO.\n");
			BTC_TRACE(trace_buf);
			/* halbtc8822b1ant_action_sco(btcoexist); */
			break;
		case BT_8822B_1ANT_COEX_ALGO_HID:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Action algorithm = HID.\n");
			BTC_TRACE(trace_buf);
			/* halbtc8822b1ant_action_hid(btcoexist); */
			break;
		case BT_8822B_1ANT_COEX_ALGO_A2DP:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Action algorithm = A2DP.\n");
			BTC_TRACE(trace_buf);
			/* halbtc8822b1ant_action_a2dp(btcoexist); */
			break;
		case BT_8822B_1ANT_COEX_ALGO_A2DP_PANHS:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action algorithm = A2DP+PAN(HS).\n");
			BTC_TRACE(trace_buf);
			/* halbtc8822b1ant_action_a2dp_pan_hs(btcoexist); */
			break;
		case BT_8822B_1ANT_COEX_ALGO_PANEDR:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Action algorithm = PAN(EDR).\n");
			BTC_TRACE(trace_buf);
			/* halbtc8822b1ant_action_pan_edr(btcoexist); */
			break;
		case BT_8822B_1ANT_COEX_ALGO_PANHS:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Action algorithm = HS mode.\n");
			BTC_TRACE(trace_buf);
			/* halbtc8822b1ant_action_pan_hs(btcoexist); */
			break;
		case BT_8822B_1ANT_COEX_ALGO_PANEDR_A2DP:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Action algorithm = PAN+A2DP.\n");
			BTC_TRACE(trace_buf);
			/* halbtc8822b1ant_action_pan_edr_a2dp(btcoexist); */
			break;
		case BT_8822B_1ANT_COEX_ALGO_PANEDR_HID:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action algorithm = PAN(EDR)+HID.\n");
			BTC_TRACE(trace_buf);
			/* halbtc8822b1ant_action_pan_edr_hid(btcoexist); */
			break;
		case BT_8822B_1ANT_COEX_ALGO_HID_A2DP_PANEDR:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action algorithm = HID+A2DP+PAN.\n");
			BTC_TRACE(trace_buf);
			/* halbtc8822b1ant_action_hid_a2dp_pan_edr(btcoexist); */
			break;
		case BT_8822B_1ANT_COEX_ALGO_HID_A2DP:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Action algorithm = HID+A2DP.\n");
			BTC_TRACE(trace_buf);
			/* halbtc8822b1ant_action_hid_a2dp(btcoexist); */
			break;
		default:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action algorithm = coexist All Off!!\n");
			BTC_TRACE(trace_buf);
			/* halbtc8822b1ant_coex_all_off(btcoexist); */
			break;
		}
		coex_dm->pre_algorithm = coex_dm->cur_algorithm;
	}
}

void halbtc8822b1ant_run_coexist_mechanism(IN struct btc_coexist *btcoexist)
{

	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean	wifi_connected = false, bt_hs_on = false;
	boolean	increase_scan_dev_num = false;
	boolean	bt_ctrl_agg_buf_size = false;
	boolean	miracast_plus_bt = false;
	u8	agg_buf_size = 5;
	u32	wifi_link_status = 0;
	u32	num_of_wifi_link = 0, wifi_bw;
	u8	iot_peer = BTC_IOT_PEER_UNKNOWN;
	boolean wifi_under_5g = false;


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

	if (!coex_sta->run_time_state) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], return for run_time_state = false !!!\n");
		BTC_TRACE(trace_buf);
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);
	if (wifi_under_5g) {
		halbtc8822b1ant_action_wifi_under5g(btcoexist);

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], WiFi is under 5G!!!\n");
		BTC_TRACE(trace_buf);
		return;
	}

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], WiFi is under 2G!!!\n");
	BTC_TRACE(trace_buf);

	halbtc8822b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO,
				     NORMAL_EXEC,
				     BT_8822B_1ANT_PHASE_2G_RUNTIME);

	if (coex_sta->bt_whck_test) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is under WHCK TEST!!!\n");
		BTC_TRACE(trace_buf);
		halbtc8822b1ant_action_bt_whck_test(btcoexist);
		return;
	}

	if (coex_sta->bt_disabled) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is disabled !!!\n");
		halbtc8822b1ant_action_wifi_only(btcoexist);
		return;
	}

	if ((BT_8822B_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) ||
	    (BT_8822B_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
	    (BT_8822B_1ANT_BT_STATUS_ACL_SCO_BUSY == coex_dm->bt_status))
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
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"############# [BTCoex],  Multi-Port num_of_wifi_link = %d, wifi_link_status = 0x%x\n",
			    num_of_wifi_link, wifi_link_status);
		BTC_TRACE(trace_buf);

		if (bt_link_info->bt_link_exist) {
			halbtc8822b1ant_limited_tx(btcoexist, NORMAL_EXEC, 1, 1,
						   0, 1);
			miracast_plus_bt = true;
		} else {
			halbtc8822b1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0,
						   0, 0);
			miracast_plus_bt = false;
		}
		btcoexist->btc_set(btcoexist, BTC_SET_BL_MIRACAST_PLUS_BT,
				   &miracast_plus_bt);
		halbtc8822b1ant_limited_rx(btcoexist, NORMAL_EXEC, false,
					   bt_ctrl_agg_buf_size, agg_buf_size);

		if ((bt_link_info->a2dp_exist) &&
		    (coex_sta->c2h_bt_inquiry_page)) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"############# [BTCoex],  BT Is Inquirying\n");
			BTC_TRACE(trace_buf);
			halbtc8822b1ant_action_bt_inquiry(btcoexist);
		} else
			halbtc8822b1ant_action_wifi_multi_port(btcoexist);

		return;
	}

	miracast_plus_bt = false;
	btcoexist->btc_set(btcoexist, BTC_SET_BL_MIRACAST_PLUS_BT,
			   &miracast_plus_bt);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if ((bt_link_info->bt_link_exist) && (wifi_connected)) {
		halbtc8822b1ant_limited_tx(btcoexist, NORMAL_EXEC, 1, 1, 0, 1);

		btcoexist->btc_get(btcoexist, BTC_GET_U1_IOT_PEER, &iot_peer);

		if (BTC_IOT_PEER_CISCO != iot_peer) {
			if (bt_link_info->sco_exist) /* if (bt_link_info->bt_hi_pri_link_exist) */
				halbtc8822b1ant_limited_rx(btcoexist,
					   NORMAL_EXEC, true, false, 0x5);
			else
				halbtc8822b1ant_limited_rx(btcoexist,
					   NORMAL_EXEC, false, false, 0x5);
		} else {
			if (bt_link_info->sco_exist)
				halbtc8822b1ant_limited_rx(btcoexist,
					   NORMAL_EXEC, true, false, 0x5);
			else {
				if (BTC_WIFI_BW_HT40 == wifi_bw)
					halbtc8822b1ant_limited_rx(btcoexist,
						NORMAL_EXEC, false, true, 0x10);
				else
					halbtc8822b1ant_limited_rx(btcoexist,
						NORMAL_EXEC, false, true, 0x8);
			}
		}

		halbtc8822b1ant_sw_mechanism(btcoexist, true);
		halbtc8822b1ant_run_sw_coexist_mechanism(
			btcoexist);  /* just print debug message */
	} else {
		halbtc8822b1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);

		halbtc8822b1ant_limited_rx(btcoexist, NORMAL_EXEC, false, false,
					   0x5);

		halbtc8822b1ant_sw_mechanism(btcoexist, false);
		halbtc8822b1ant_run_sw_coexist_mechanism(
			btcoexist); /* just print debug message */
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	if (coex_sta->c2h_bt_inquiry_page) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "############# [BTCoex],  BT Is Inquirying\n");
		BTC_TRACE(trace_buf);
		halbtc8822b1ant_action_bt_inquiry(btcoexist);
		return;
	} else if (bt_hs_on) {
		halbtc8822b1ant_action_hs(btcoexist);
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
				halbtc8822b1ant_action_wifi_not_connected_scan(
					btcoexist);
			else
				halbtc8822b1ant_action_wifi_not_connected_asso_auth(
					btcoexist);
		} else
			halbtc8822b1ant_action_wifi_not_connected(btcoexist);
	} else	/* wifi LPS/Busy */
		halbtc8822b1ant_action_wifi_connected(btcoexist);
}

u32 halbtc8822b1ant_psd_log2base(IN struct btc_coexist *btcoexist, IN u32 val)
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

		tmp = (tmp >> 1);
		shiftcount++;
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

void halbtc8822b1ant_init_coex_dm(IN struct btc_coexist *btcoexist)
{
	/* force to reset coex mechanism */

	halbtc8822b1ant_low_penalty_ra(btcoexist, NORMAL_EXEC, false);

	/* sw all off */
	halbtc8822b1ant_sw_mechanism(btcoexist, false);

	/* halbtc8822b1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 8); */
	/* halbtc8822b1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 0); */

	coex_sta->pop_event_cnt = 0;
}

void halbtc8822b1ant_init_hw_config(IN struct btc_coexist *btcoexist,
				    IN boolean back_up, IN boolean wifi_only)
{

	u8	u8tmp = 0;
	boolean			wifi_under_5g = false;
	u32				u32tmp1 = 0, u32tmp2 = 0, u32tmp3 = 0;


	u32tmp3 = btcoexist->btc_read_4byte(btcoexist, 0xcb4);
	u32tmp1 = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist, 0x38);
	u32tmp2 = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist, 0x54);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], ********** (Before Init HW config) 0xcb4 = 0x%x, 0x38= 0x%x, 0x54= 0x%x**********\n",
		    u32tmp3, u32tmp1, u32tmp2);
	BTC_TRACE(trace_buf);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], 1Ant Init HW Config!!\n");
	BTC_TRACE(trace_buf);

	coex_sta->bt_coex_supported_feature = 0;
	coex_sta->bt_coex_supported_version = 0;

	/* Setup RF front end type */
	halbtc8822b1ant_set_rfe_type(btcoexist);

	/* 0xf0[15:12] --> Chip Cut information */
	coex_sta->cut_version = (btcoexist->btc_read_1byte(btcoexist,
				 0xf1) & 0xf0) >> 4;

	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x550, 0x8,
					   0x1);  /* enable TBTT nterrupt */

	/* BT report packet sample rate	 */
	/* 0x790[5:0]=0x5 */
	u8tmp = btcoexist->btc_read_1byte(btcoexist, 0x790);
	u8tmp &= 0xc0;
	u8tmp |= 0x5;
	btcoexist->btc_write_1byte(btcoexist, 0x790, u8tmp);

	/* Enable BT counter statistics */
	btcoexist->btc_write_1byte(btcoexist, 0x778, 0x1);

	/* Enable PTA (3-wire function form BT side) */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x40, 0x20, 0x1);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x41, 0x02, 0x1);

	/* Enable PTA (tx/rx signal form WiFi side) */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4c6, 0x10, 0x1);
	/*GNT_BT=1 while select both */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x763, 0x10, 0x1);

	/* enable GNT_WL/GNT_BT debug signal to GPIO14/15 */
	/*btcoexist->btc_write_1byte_bitmask(btcoexist, 0x73, 0x8, 0x1);*/

	/* enable GNT_WL */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4e, 0x40, 0x0);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x1, 0x0);

	if (btcoexist->btc_read_1byte(btcoexist, 0x80) == 0xc6)
		halbtc8822b1ant_post_state_to_bt(btcoexist,
					 BT_8822B_1ANT_SCOREBOARD_ONOFF, true);

	/* Antenna config */
	if (wifi_only) {

		coex_sta->concurrent_rx_mode_on = false;
		halbtc8822b1ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_WIFI,
					     FORCE_EXEC,
					     BT_8822B_1ANT_PHASE_WLANONLY_INIT);
	} else {

		coex_sta->concurrent_rx_mode_on = true;

		halbtc8822b1ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_AUTO,
					     FORCE_EXEC,
					     BT_8822B_1ANT_PHASE_COEX_INIT);
	}


	/* PTA parameter */
	halbtc8822b1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 0);

	halbtc8822b1ant_enable_gnt_to_gpio(btcoexist, true);

}


void halbtc8822b1ant_init_hw_config_without_bt(IN struct btc_coexist *btcoexist,
		IN boolean back_up, IN boolean wifi_only)
{

	u8	u8tmp = 0;
	boolean		wifi_under_5g = false;
	u32			u32tmp1 = 0, u32tmp2 = 0, u32tmp3 = 0;


	/* 0xf0[15:12] --> Chip Cut information */
	coex_sta->cut_version = (btcoexist->btc_read_1byte(btcoexist,
				 0xf1) & 0xf0) >> 4;

	/* enable TBTT interrupt */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x550, 0x8,
					   0x1);

	/* enable GNT_WL */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4e, 0x40, 0x0);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x1, 0x0);

	/* Antenna config */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4e, 0x80,
					   0x0);  /*  0x4c[23] = 0 */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4f, 0x01,
					   0x1);  /* 0x4c[24] = 1 */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcb4, 0xff,
		0x77);	/* BB SW, DPDT use RFE_ctrl8 and RFE_ctrl9 as conctrol pin */


	/* Ext switch buffer mux */
	btcoexist->btc_write_1byte(btcoexist, 0x974, 0xff);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x1991, 0x3, 0x0);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcbe, 0x8, 0x0);

	/*enable sw control gnt_wl=1 / gnt_bt=1 */
	btcoexist->btc_write_1byte(btcoexist, 0x73, 0x0e);

	btcoexist->btc_write_4byte(btcoexist, 0x1704, 0x0000ff00);

	btcoexist->btc_write_4byte(btcoexist, 0x1700, 0xc00f0038);


	/* ant switch WL2G or WL5G*/
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G,
			   &wifi_under_5g);
	if (wifi_under_5g)

		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcbd,
						   0x3, 1);

	else

		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcbd,
						   0x3, 2);

}

void halbtc8822b1ant_psd_showdata(IN struct btc_coexist *btcoexist)
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

			i = 1;
			j = 1;

		}
	}


}

void halbtc8822b1ant_psd_max_holddata(IN struct btc_coexist *btcoexist,
				      IN u32 gen_count)
{
	u32	i = 0, i_max = 0, val_max = 0;

	if (gen_count == 1) {
		memcpy(psd_scan->psd_report_max_hold,
		       psd_scan->psd_report,
		       BT_8822B_1ANT_ANTDET_PSD_POINTS * sizeof(u32));

		for (i = psd_scan->psd_start_point;
		     i <= psd_scan->psd_stop_point; i++) {

		}

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

u32 halbtc8822b1ant_psd_getdata(IN struct btc_coexist *btcoexist, IN u32 point)
{
	/* reg 0x808[9:0]: FFT data x */
	/* reg 0x808[22]: 0-->1 to get 1 FFT data y */
	/* reg 0x8b4[15:0]: FFT data y report */

	u32 val = 0, psd_report = 0;

	val = btcoexist->btc_read_4byte(btcoexist, 0x808);

	val &= 0xffbffc00;
	val |= point;

	btcoexist->btc_write_4byte(btcoexist, 0x808, val);

	val |= 0x00400000;
	btcoexist->btc_write_4byte(btcoexist, 0x808, val);


	val = btcoexist->btc_read_4byte(btcoexist, 0x8b4);

	psd_report = val & 0x0000ffff;

	return psd_report;
}


void halbtc8822b1ant_psd_sweep_point(IN struct btc_coexist *btcoexist,
	     IN u32 cent_freq, IN s32 offset, IN u32 span, IN u32 points,
				     IN u32 avgnum)
{
	u32	 i, val, n, k = 0;
	u32	points1 = 0, psd_report = 0;
	u32	start_p = 0, stop_p = 0, delta_freq_per_point = 156250;
	u32    psd_center_freq = 20 * 10 ^ 6, freq, freq1, freq2;
	boolean outloop = false;
	u8	 flag = 0;
	u32	tmp, psd_rep1, psd_rep2;
	u32	wifi_original_channel = 1;

	psd_scan->is_psd_running = true;

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

			/* Set  RF mode = Rx, RF Gain = 0x8a0 */
			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x0,
						  0xfffff, 0x308a0);

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

			flag = 3;
			break;
		case 3:
			memset(psd_scan->psd_report, 0,
			       psd_scan->psd_point * sizeof(u32));
			start_p = psd_scan->psd_start_point +
				  psd_scan->psd_start_base;
			stop_p = psd_scan->psd_stop_point +
				 psd_scan->psd_start_base + 1;

			i = start_p;

			while (i < stop_p) {
				if (i >= points1)
					psd_report =
						halbtc8822b1ant_psd_getdata(
							btcoexist, i - points1);
				else
					psd_report =
						halbtc8822b1ant_psd_getdata(
							btcoexist, i);

				if (psd_report == 0)
					tmp = 0;
				else
					/* tmp =  20*log10((double)psd_report); */
					/* 20*log2(x)/log2(10), log2Base return theresult of the psd_report*100 */
					tmp = 6 * halbtc8822b1ant_psd_log2base(
						      btcoexist, psd_report);

				n = i - psd_scan->psd_start_base;
				psd_scan->psd_report[n] =  tmp;
				psd_rep1 = psd_scan->psd_report[n] / 100;
				psd_rep2 = psd_scan->psd_report[n] - psd_rep1 *
					   100;

				freq = ((cent_freq - 20) * 1000000 + n *
					delta_freq_per_point);
				freq1 = freq / 1000000;
				freq2 = freq / 1000 - freq1 * 1000;

				i++;

				k = 0;

				/* Add Delay between PSD point */
				while (1) {
					if (k++ > 20000)
						break;
				}

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


}

#if (BTC_COEX_OFFLOAD == 1)
void halbtc8822b1ant_wifi_info_notify(IN struct btc_coexist *btcoexist)
{
	u8			h2c_para[4] = {0};
	u8			opcode_ver = 0;
	u8			ap_num = 0;
	s32			wifi_rssi = 0;
	boolean			wifi_busy = false;

	btcoexist->btc_get(btcoexist, BTC_GET_S4_WIFI_RSSI, &wifi_rssi);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM, &ap_num);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	h2c_para[0] = ap_num;					/* AP number */
	h2c_para[1] = (u8)wifi_busy;		/* Busy */
	h2c_para[2] = (u8)wifi_rssi;			/* RSSI */

	btcoexist->btc_coex_h2c_process(btcoexist, COL_OP_WIFI_INFO_NOTIFY,
					opcode_ver, &h2c_para[0], 3);
}

void halbtc8822b1ant_setManual(IN struct btc_coexist *btcoexist,
			       IN boolean manual)
{
	u8			h2c_para[4] = {0};
	u8			opcode_ver = 0;
	u8			set_type = 0;

	if (manual)
		set_type = 1;
	else
		set_type = 0;

	h2c_para[0] = set_type;				/* set_type */

	btcoexist->btc_coex_h2c_process(btcoexist, COL_OP_SET_CONTROL,
					opcode_ver,
					&h2c_para[0], 1);
}

/* ************************************************************
 * work around function start with wa_halbtc8822b1ant_
 * ************************************************************
 * ************************************************************
 * extern function start with ex_halbtc8822b1ant_
 * ************************************************************ */

void ex_halbtc8822b1ant_power_on_setting(IN struct btc_coexist *btcoexist)
{}
void ex_halbtc8822b1ant_pre_load_firmware(IN struct btc_coexist *btcoexist)
{}
void ex_halbtc8822b1ant_init_hw_config(IN struct btc_coexist *btcoexist,
				       IN boolean wifi_only)
{}
void ex_halbtc8822b1ant_init_coex_dm(IN struct btc_coexist *btcoexist)
{}
void ex_halbtc8822b1ant_ips_notify(IN struct btc_coexist *btcoexist, IN u8 type)
{
	u8			h2c_para[4] = {0};
	u8			opcode_ver = 0;
	u8			ips_notify = 0;

	if (BTC_IPS_ENTER == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], IPS ENTER notify\n");
		BTC_TRACE(trace_buf);
		ips_notify = 1;
	} else if (BTC_IPS_LEAVE == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], IPS LEAVE notify\n");
		BTC_TRACE(trace_buf);
	}

	h2c_para[0] = ips_notify;		/* IPS notify */
	h2c_para[1] = 0xff;			/* LPS notify */
	h2c_para[2] = 0xff;			/* RF state notify */
	h2c_para[3] = 0xff;			/* pnp notify */

	btcoexist->btc_coex_h2c_process(btcoexist,
					COL_OP_WIFI_POWER_STATE_NOTIFY,
					opcode_ver, &h2c_para[0], 4);
}
void ex_halbtc8822b1ant_lps_notify(IN struct btc_coexist *btcoexist, IN u8 type)
{
	u8			h2c_para[4] = {0};
	u8			opcode_ver = 0;
	u8			lps_notify = 0;

	if (BTC_LPS_ENABLE == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], LPS ENABLE notify\n");
		BTC_TRACE(trace_buf);
		lps_notify = 1;
	} else if (BTC_LPS_DISABLE == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], LPS DISABLE notify\n");
		BTC_TRACE(trace_buf);
	}

	h2c_para[0] = 0xff;			/* IPS notify */
	h2c_para[1] = lps_notify;		/* LPS notify */
	h2c_para[2] = 0xff;			/* RF state notify */
	h2c_para[3] = 0xff;			/* pnp notify */

	btcoexist->btc_coex_h2c_process(btcoexist,
					COL_OP_WIFI_POWER_STATE_NOTIFY,
					opcode_ver, &h2c_para[0], 4);
}

void ex_halbtc8822b1ant_scan_notify(IN struct btc_coexist *btcoexist,
				    IN u8 type)
{
	u8			h2c_para[4] = {0};
	u8			opcode_ver = 0;
	u8			scan_start = 0;
	boolean			under_4way = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS,
			   &under_4way);
	if (BTC_SCAN_START == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], SCAN START notify\n");
		BTC_TRACE(trace_buf);
		scan_start = 1;
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], SCAN FINISH notify\n");
		BTC_TRACE(trace_buf);
	}

	h2c_para[0] = scan_start;		/* scan notify */
	h2c_para[1] = 0xff;			/* connect notify */
	h2c_para[2] = 0xff;			/* specific packet notify */
	if (under_4way)
		h2c_para[3] = 1;			/* under 4way progress */
	else
		h2c_para[3] = 0;

	btcoexist->btc_coex_h2c_process(btcoexist, COL_OP_WIFI_PROGRESS_NOTIFY,
					opcode_ver, &h2c_para[0], 4);
}

void ex_halbtc8822b1ant_connect_notify(IN struct btc_coexist *btcoexist,
				       IN u8 type)
{
	u8			h2c_para[4] = {0};
	u8			opcode_ver = 0;
	u8			connect_start = 0;
	boolean			under_4way = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS,
			   &under_4way);
	if (BTC_ASSOCIATE_START == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], CONNECT START notify\n");
		BTC_TRACE(trace_buf);
		connect_start = 1;
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], CONNECT FINISH notify\n");
		BTC_TRACE(trace_buf);
	}

	h2c_para[0] = 0xff;			/* scan notify */
	h2c_para[1] = connect_start;	/* connect notify */
	h2c_para[2] = 0xff;			/* specific packet notify */
	if (under_4way)
		h2c_para[3] = 1;			/* under 4way progress */
	else
		h2c_para[3] = 0;

	btcoexist->btc_coex_h2c_process(btcoexist, COL_OP_WIFI_PROGRESS_NOTIFY,
					opcode_ver, &h2c_para[0], 4);
}

void ex_halbtc8822b1ant_media_status_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	u32			wifi_bw;
	u8			wifi_central_chnl;
	u8			h2c_para[5] = {0};
	u8			opcode_ver = 0;
	u8			port = 0, connected = 0, freq = 0, bandwidth = 0, iot_peer = 0;
	boolean			wifi_under_5g = false;

	if (BTC_MEDIA_CONNECT == type)
		connected = 1;

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	bandwidth = (u8)wifi_bw;
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);
	if (wifi_under_5g)
		freq = 1;
	else
		freq = 0;
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_CENTRAL_CHNL,
			   &wifi_central_chnl);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_IOT_PEER, &iot_peer);

	h2c_para[0] = (connected << 4) |
		port;	/* port need to be implemented in the future (p2p port, ...) */
	h2c_para[1] = (freq << 4) | bandwidth;
	h2c_para[2] = wifi_central_chnl;
	h2c_para[3] = iot_peer;
	btcoexist->btc_coex_h2c_process(btcoexist, COL_OP_WIFI_STATUS_NOTIFY,
					opcode_ver, &h2c_para[0], 4);
}

void ex_halbtc8822b1ant_specific_packet_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	u8			h2c_para[4] = {0};
	u8			opcode_ver = 0;
	u8			connect_start = 0;
	boolean			under_4way = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS,
			   &under_4way);

	h2c_para[0] = 0xff;			/* scan notify */
	h2c_para[1] = 0xff;			/* connect notify */
	h2c_para[2] = type;			/* specific packet notify */
	if (under_4way)
		h2c_para[3] = 1;			/* under 4way progress */
	else
		h2c_para[3] = 0;

	btcoexist->btc_coex_h2c_process(btcoexist, COL_OP_WIFI_PROGRESS_NOTIFY,
					opcode_ver, &h2c_para[0], 4);
}

void ex_halbtc8822b1ant_bt_info_notify(IN struct btc_coexist *btcoexist,
				       IN u8 *tmp_buf, IN u8 length)
{}
void ex_halbtc8822b1ant_rf_status_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	u8			h2c_para[4] = {0};
	u8			opcode_ver = 0;
	u8			rfstate_notify = 0;

	if (BTC_RF_ON == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RF is turned ON!!\n");
		BTC_TRACE(trace_buf);
		rfstate_notify = 1;
	} else if (BTC_RF_OFF == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RF is turned OFF!!\n");
		BTC_TRACE(trace_buf);
	}

	h2c_para[0] = 0xff;			/* IPS notify */
	h2c_para[1] = 0xff;			/* LPS notify */
	h2c_para[2] = rfstate_notify;	/* RF state notify */
	h2c_para[3] = 0xff;			/* pnp notify */

	btcoexist->btc_coex_h2c_process(btcoexist,
					COL_OP_WIFI_POWER_STATE_NOTIFY,
					opcode_ver, &h2c_para[0], 4);
}

void ex_halbtc8822b1ant_halt_notify(IN struct btc_coexist *btcoexist)
{}
void ex_halbtc8822b1ant_pnp_notify(IN struct btc_coexist *btcoexist,
				   IN u8 pnp_state)
{
	u8			h2c_para[4] = {0};
	u8			opcode_ver = 0;
	u8			pnp_notify = 0;

	if (BTC_WIFI_PNP_SLEEP == pnp_state) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Pnp notify to SLEEP\n");
		BTC_TRACE(trace_buf);
		pnp_notify = 1;
	} else if (BTC_WIFI_PNP_WAKE_UP == pnp_state) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Pnp notify to WAKE UP\n");
		BTC_TRACE(trace_buf);
	}

	h2c_para[0] = 0xff;			/* IPS notify */
	h2c_para[1] = 0xff;			/* LPS notify */
	h2c_para[2] = 0xff;			/* RF state notify */
	h2c_para[3] = pnp_notify;		/* pnp notify */

	btcoexist->btc_coex_h2c_process(btcoexist,
					COL_OP_WIFI_POWER_STATE_NOTIFY,
					opcode_ver, &h2c_para[0], 4);
}

void ex_halbtc8822b1ant_coex_dm_reset(IN struct btc_coexist *btcoexist)
{}
void ex_halbtc8822b1ant_periodical(IN struct btc_coexist *btcoexist)
{

	halbtc8822b1ant_wifi_info_notify(btcoexist);
}

void ex_halbtc8822b1ant_display_coex_info(IN struct btc_coexist *btcoexist)
{
	struct  btc_board_info		*board_info = &btcoexist->board_info;
	struct  btc_stack_info		*stack_info = &btcoexist->stack_info;
	struct  btc_bt_link_info	*bt_link_info = &btcoexist->bt_link_info;
	u8				*cli_buf = btcoexist->cli_buf;
	u8				u8tmp[4], i, bt_info_ext, ps_tdma_case = 0;
	u16				u16tmp[4];
	u32				u32tmp[4];
	u32				fa_ofdm, fa_cck, cca_ofdm, cca_cck;
	u32				fw_ver = 0, bt_patch_ver = 0, bt_coex_ver = 0;
	static u8			pop_report_in_10s = 0;
	u32			phyver = 0;
	boolean			lte_coex_on = false;

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
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %d/ %d/ %s / %d",
			   "Ant PG Num/ Mech/ Pos/ RFE",
			   board_info->pg_ant_num, board_info->btdm_ant_num,
			   (board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT
			    ? "Main" : "Aux"),
			   rfe_type->rfe_module_type);
		CL_PRINTF(cli_buf);
	} else {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %d/ %d/ %s/ %d  (%d/%d/%d)",
			   "Ant PG Num/ Mech(Ant_Det)/ Pos/ RFE",
			   board_info->pg_ant_num,
			   board_info->btdm_ant_num_by_ant_det,
			   (board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT
			    ? "Main" : "Aux"),
			   rfe_type->rfe_module_type,
			   psd_scan->ant_det_try_count,
			   psd_scan->ant_det_fail_count,
			   psd_scan->ant_det_result);
		CL_PRINTF(cli_buf);

		if (board_info->btdm_ant_det_finish) {
			if (psd_scan->ant_det_result != 12)
				CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s",
					"Ant Det PSD Value",
					psd_scan->ant_det_peak_val);
			else
				CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
					"\r\n %-35s = %d",
					"Ant Det PSD Value",
				psd_scan->ant_det_psd_scan_peak_val / 100);

			CL_PRINTF(cli_buf);
		}
	}

	btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER, &bt_patch_ver);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %d_%x/ 0x%x/ 0x%x(%d)",
		   "CoexVer/ FwVer/ PatchVer",
		   glcoex_ver_date_8822b_1ant, glcoex_ver_8822b_1ant, fw_ver,
		   bt_patch_ver, bt_patch_ver);
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

	pop_report_in_10s++;
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = [%s/ %d/ %d/ %d] ",
		   "BT [status/ rssi/ retryCnt/ popCnt]",
		   ((coex_sta->bt_disabled) ? ("disabled") :	((
		   coex_sta->c2h_bt_inquiry_page) ? ("inquiry/page scan")
			   : ((BT_8822B_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
			       coex_dm->bt_status) ? "non-connected idle" :
		((BT_8822B_1ANT_BT_STATUS_CONNECTED_IDLE == coex_dm->bt_status)
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
			"\r\n %-35s = %s%s%s%s%s",
			"Profiles",
			((bt_link_info->a2dp_exist) ? "A2DP," : ""),
			((bt_link_info->sco_exist) ?  "SCO," : ""),
			((bt_link_info->hid_exist) ?
			((coex_sta->hid_busy_num >= 2) ? "HID(4/18)," : "HID(2/18),") : ""),
			((bt_link_info->pan_exist) ?  "PAN," : ""),
			((coex_sta->voice_over_HOGP) ? "Voice" : ""));
	else
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			"\r\n %-35s = None", "Profiles");

	CL_PRINTF(cli_buf);

	if (bt_link_info->a2dp_exist) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d/ %s",
			"A2DP Rate/Bitpool/Auto_Slot",
			((coex_sta->is_A2DP_3M) ? "3M" : "No_3M"),
			coex_sta->a2dp_bit_pool,
			((coex_sta->is_autoslot) ? "On" : "Off"));
		CL_PRINTF(cli_buf);
	}

	if (bt_link_info->hid_exist) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
			   "HID PairNum/Forbid_Slot",
			 coex_sta->hid_pair_cnt,
			 coex_sta->forbidden_slot
			 );
		CL_PRINTF(cli_buf);
	}

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s / %s/ 0x%x/ 0x%x",
		"Role/IgnWlanAct/Feature/BLEScan",
		((bt_link_info->slave_role) ? "Slave" : "Master"),
		((coex_dm->cur_ignore_wlan_act) ? "Yes":"No"),
		coex_sta->bt_coex_supported_feature,
		coex_sta->bt_ble_scan_type);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d/ %d",
				   "ReInit/ReLink/IgnWlact/Page/NameReq",
			 coex_sta->cnt_ReInit,
		   coex_sta->cnt_setupLink,
			 coex_sta->cnt_IgnWlanAct,
				 coex_sta->cnt_Page,
			 coex_sta->cnt_RemoteNameReq
			 );
		CL_PRINTF(cli_buf);

	halbtc8822b1ant_read_score_board(btcoexist,	&u16tmp[0]);

	if ((coex_sta->bt_reg_vendor_ae == 0xffff) ||
	    (coex_sta->bt_reg_vendor_ac == 0xffff))
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = x/ x/ %04x",
			   "0xae[4]/0xac[1:0]/Scoreboard", u16tmp[0]);
	else
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = 0x%x/ 0x%x/ %04x",
			   "0xae[4]/0xac[1:0]/Scoreboard",
			 ((coex_sta->bt_reg_vendor_ae & BIT(4))>>4),
			   coex_sta->bt_reg_vendor_ac & 0x3, u16tmp[0]);
	CL_PRINTF(cli_buf);

	for (i = 0; i < BT_INFO_SRC_8822B_1ANT_MAX; i++) {
		if (coex_sta->bt_info_c2h_cnt[i]) {
			CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
				"\r\n %-35s = %02x %02x %02x %02x %02x %02x %02x(%d)",
				   glbt_info_src_8822b_1ant[i],
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
		   "WL/BT Coex Table Type",
		   coex_sta->coex_table_type);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x6c0);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x6c4);
	u32tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0x6c8);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x6c0/0x6c4/0x6c8(coexTable)",
		   u32tmp[0], u32tmp[1], u32tmp[2]);
	CL_PRINTF(cli_buf);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x778);
	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x6cc);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x778/0x6cc/IgnWlanAct",
		   u8tmp[0], u32tmp[0],  coex_dm->cur_ignore_wlan_act);
	CL_PRINTF(cli_buf);

	u32tmp[0] = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist,
			0xa0);
	u32tmp[1] = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist,
			0xa4);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "LTE Coex Table W_L/B_L",
		   u32tmp[0] & 0xffff, u32tmp[1] & 0xffff);
	CL_PRINTF(cli_buf);

	u32tmp[0] = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist,
			0xa8);
	u32tmp[1] = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist,
			0xac);
	u32tmp[2] = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist,
			0xb0);
	u32tmp[3] = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist,
			0xb4);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x",
		   "LTE Break Table W_L/B_L/L_W/L_B",
		   u32tmp[0] & 0xffff, u32tmp[1] & 0xffff,
		   u32tmp[2] & 0xffff, u32tmp[3] & 0xffff);
	CL_PRINTF(cli_buf);

	/* Hw setting		 */
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
		   "============[Hw setting]============");
	CL_PRINTF(cli_buf);
/*
	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x430);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x434);
	u16tmp[0] = btcoexist->btc_read_2byte(btcoexist, 0x42a);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x456);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/0x%x/0x%x/0x%x",
		   "0x430/0x434/0x42a/0x456",
		   u32tmp[0], u32tmp[1], u16tmp[0], u8tmp[0]);
	CL_PRINTF(cli_buf);
*/

	u32tmp[0] = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist, 0x38);
	u32tmp[1] = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist, 0x54);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x73);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %s",
		   "LTE CoexOn/Path Ctrl Owner",
		   (int)((u32tmp[0]&BIT(7)) >> 7),
		   ((u8tmp[0]&BIT(2)) ? "WL" : "BT"));
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d",
		   "LTE 3Wire/OPMode/UART/UARTMode",
		   (int)((u32tmp[0]&BIT(6)) >> 6),
		   (int)((u32tmp[0] & (BIT(5) | BIT(4))) >> 4),
		   (int)((u32tmp[0]&BIT(3)) >> 3),
		   (int)(u32tmp[0] & (BIT(2) | BIT(1) | BIT(0))));
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %s",
		   "GNT_WL_SWCtrl/GNT_BT_SWCtrl/Dbg",
		   (int)((u32tmp[0]&BIT(12)) >> 12),
		   (int)((u32tmp[0]&BIT(14)) >> 14),
		   ((u8tmp[0]&BIT(3)) ? "On" : "Off"));
	CL_PRINTF(cli_buf);

	u32tmp[0] = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist, 0x54);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d",
		   "GNT_WL/GNT_BT/LTE_Busy/UART_Busy",
		   (int)((u32tmp[0]&BIT(2)) >> 2),
		   (int)((u32tmp[0]&BIT(3)) >> 3),
		   (int)((u32tmp[0]&BIT(1)) >> 1), (int)(u32tmp[0]&BIT(0)));
	CL_PRINTF(cli_buf);


	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x4c6);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x40);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "0x4c6[4]/0x40[5] (WL/BT PTA)",
		   (int)((u8tmp[0] & BIT(4)) >> 4),
		   (int)((u8tmp[1] & BIT(5)) >> 5));
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x550);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x522);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x953);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ %s",
		   "0x550(bcn ctrl)/0x522/4-RxAGC",
		   u32tmp[0], u8tmp[0], (u8tmp[1] & 0x2) ? "On" : "Off");
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
				   0xffff);
	fa_cck = (u8tmp[0] << 8) + u8tmp[1];

	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0xc50);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x",
		   "0xc50/OFDM-CCA/OFDM-FA/CCK-FA",
		   u32tmp[1] & 0xff, u32tmp[0] & 0xffff, fa_ofdm, fa_cck);
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

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "0x770(high-pri rx/tx)",
		   coex_sta->high_priority_rx, coex_sta->high_priority_tx);
	CL_PRINTF(cli_buf);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "0x774(low-pri rx/tx)",
		   coex_sta->low_priority_rx, coex_sta->low_priority_tx);
	CL_PRINTF(cli_buf);
#if (BT_AUTO_REPORT_ONLY_8822B_1ANT == 1)
	/* halbtc8822b1ant_monitor_bt_ctr(btcoexist); */
#endif
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_COEX_STATISTICS);
}
void ex_halbtc8822b1ant_antenna_detection(IN struct btc_coexist *btcoexist,
		IN u32 cent_freq, IN u32 offset, IN u32 span, IN u32 seconds)
{}
void ex_halbtc8822b1ant_display_ant_detection(IN struct btc_coexist *btcoexist)
{}
void ex_halbtc8822b1ant_dbg_control(IN struct btc_coexist *btcoexist,
				    IN u8 op_code, IN u8 op_len, IN u8 *pdata)
{
	switch (op_code) {
	case BTC_DBG_SET_COEX_MANUAL_CTRL: {
		boolean		manual = (boolean) *pdata;

		halbtc8822b1ant_setManual(btcoexist, manual);
	}
	break;
	default:
		break;
	}
}

#else
void ex_halbtc8822b1ant_power_on_setting(IN struct btc_coexist *btcoexist)
{
	struct  btc_board_info	*board_info = &btcoexist->board_info;
	u8 u8tmp = 0x0;
	u16 u16tmp = 0x0;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"xxxxxxxxxxxxxxxx Execute 8822b 1-Ant PowerOn Setting!! xxxxxxxxxxxxxxxx\n");
	BTC_TRACE(trace_buf);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "Ant Det Finish = %s, Ant Det Number  = %d\n",
		    board_info->btdm_ant_det_finish ? "Yes" : "No",
		    board_info->btdm_ant_num_by_ant_det);
	BTC_TRACE(trace_buf);

	btcoexist->stop_coex_dm = true;

	/* enable BB, REG_SYS_FUNC_EN such that we can write 0x948 correctly. */
	u16tmp = btcoexist->btc_read_2byte(btcoexist, 0x2);
	btcoexist->btc_write_2byte(btcoexist, 0x2, u16tmp | BIT(0) | BIT(1));

	/* set Path control owner to WiFi */
	halbtc8822b1ant_ltecoex_pathcontrol_owner(btcoexist,
			BT_8822B_1ANT_PCO_WLSIDE);

	/* set GNT_BT to high */
	halbtc8822b1ant_ltecoex_set_gnt_bt(btcoexist,
					   BT_8822B_1ANT_GNT_BLOCK_RFC_BB,
					   BT_8822B_1ANT_GNT_CTRL_BY_SW,
					   BT_8822B_1ANT_SIG_STA_SET_TO_HIGH);
	/* Set GNT_WL to low */
	halbtc8822b1ant_ltecoex_set_gnt_wl(btcoexist,
					   BT_8822B_1ANT_GNT_BLOCK_RFC_BB,
					   BT_8822B_1ANT_GNT_CTRL_BY_SW,
					   BT_8822B_1ANT_SIG_STA_SET_TO_LOW);

	/* set WLAN_ACT = 0 */
	/* btcoexist->btc_write_1byte(btcoexist, 0x76e, 0x4); */

	/* SD1 Chunchu red x issue */
	btcoexist->btc_write_1byte(btcoexist, 0xff1a, 0x0);

	halbtc8822b1ant_enable_gnt_to_gpio(btcoexist, true);

	/* */
	/* S0 or S1 setting and Local register setting(By the setting fw can get ant number, S0/S1, ... info) */
	/* Local setting bit define */
	/*	BIT0: "0" for no antenna inverse; "1" for antenna inverse  */
	/*	BIT1: "0" for internal switch; "1" for external switch */
	/*	BIT2: "0" for one antenna; "1" for two antenna */
	/* NOTE: here default all internal switch and 1-antenna ==> BIT1=0 and BIT2=0 */

	u8tmp = 0;
	board_info->btdm_ant_pos = BTC_ANTENNA_AT_MAIN_PORT;

	if (btcoexist->chip_interface == BTC_INTF_USB)
		btcoexist->btc_write_local_reg_1byte(btcoexist, 0xfe08, u8tmp);
	else if (btcoexist->chip_interface == BTC_INTF_SDIO)
		btcoexist->btc_write_local_reg_1byte(btcoexist, 0x60, u8tmp);

	BTC_TRACE(trace_buf);


}

void ex_halbtc8822b1ant_power_on_setting_without_bt(IN struct btc_coexist
		*btcoexist)
{
	struct  btc_board_info	*board_info = &btcoexist->board_info;
	u8 u8tmp = 0x0;
	u16 u16tmp = 0x0;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    " 8822b 1-Ant PowerOn Setting without bt\n");
	BTC_TRACE(trace_buf);

	btcoexist->stop_coex_dm = true;

	/* enable BB, REG_SYS_FUNC_EN such that we can write 0x948 correctly. */
	u16tmp = btcoexist->btc_read_2byte(btcoexist, 0x2);
	btcoexist->btc_write_2byte(btcoexist, 0x2, u16tmp | BIT(0) | BIT(1));


	/* SD1 Chunchu red x issue */
	btcoexist->btc_write_1byte(btcoexist, 0xff1a, 0x0);

	u8tmp = 0;
	board_info->btdm_ant_pos = BTC_ANTENNA_AT_MAIN_PORT;

	if (btcoexist->chip_interface == BTC_INTF_USB)
		btcoexist->btc_write_local_reg_1byte(btcoexist, 0xfe08, u8tmp);
	else if (btcoexist->chip_interface == BTC_INTF_SDIO)
		btcoexist->btc_write_local_reg_1byte(btcoexist, 0x60, u8tmp);



}


void ex_halbtc8822b1ant_pre_load_firmware(IN struct btc_coexist *btcoexist)
{
}

void ex_halbtc8822b1ant_init_hw_config(IN struct btc_coexist *btcoexist,
				       IN boolean wifi_only)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], ********** (ini hw config) **********\n");

	halbtc8822b1ant_init_hw_config(btcoexist, true, wifi_only);
	btcoexist->stop_coex_dm = false;
}

void ex_halbtc8822b1ant_init_hw_config_without_bt(IN struct btc_coexist
		*btcoexist,
		IN boolean wifi_only)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], ********** (ini hw config) **********\n");

	halbtc8822b1ant_init_hw_config_without_bt(btcoexist, true, wifi_only);
	btcoexist->stop_coex_dm = true;
}

void ex_halbtc8822b1ant_antenna_switch_without_bt(IN struct btc_coexist
		*btcoexist)
{

	boolean wifi_under_5g = false;


	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);

	if (wifi_under_5g)

		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcbd,
						   0x3, 1);

	else
		btcoexist->btc_write_1byte_bitmask(btcoexist,
						   0xcbd, 0x3, 2);


}

void ex_halbtc8822b1ant_init_coex_dm(IN struct btc_coexist *btcoexist)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], Coex Mechanism Init!!\n");
	BTC_TRACE(trace_buf);

	btcoexist->stop_coex_dm = false;

	halbtc8822b1ant_init_coex_dm(btcoexist);

	halbtc8822b1ant_query_bt_info(btcoexist);
}

void ex_halbtc8822b1ant_display_coex_info(IN struct btc_coexist *btcoexist)
{

	struct  btc_board_info		*board_info = &btcoexist->board_info;
	struct  btc_bt_link_info	*bt_link_info = &btcoexist->bt_link_info;
	u8				*cli_buf = btcoexist->cli_buf;
	u8				u8tmp[4], i, bt_info_ext, ps_tdma_case = 0;
	u16				u16tmp[4];
	u32				fa_ofdm, fa_cck, cca_ofdm, cca_cck;
	u32				u32tmp[4];
	u32				fa_of_dm;
	u32				fw_ver = 0, bt_patch_ver = 0;
	static u8			pop_report_in_10s = 0;
	u32                       phyver = 0;
	 /*u32 pnp_wake_cnt=0;*/

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], ********** (display coexinfo) **********\n");
	BTC_TRACE(trace_buf);
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], ********** displaycoexinfostart, 0xcb4/0xcbd = 0x%x/0x%x\n",
		    btcoexist->btc_read_1byte(btcoexist, 0xcb4),
		    btcoexist->btc_read_1byte(btcoexist, 0xcbd));
	BTC_TRACE(trace_buf);

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
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %d/ %d/ %d/ %d",
			   "Ant PG Num/ Mech/ Pos/ RFE",
			   board_info->pg_ant_num, board_info->btdm_ant_num,
			   board_info->btdm_ant_pos,
			   rfe_type->rfe_module_type);
		CL_PRINTF(cli_buf);
	} else {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %d/ %d/ %d/ %d  (%d/%d/%d)",
			   "Ant PG Num/ Mech(Ant_Det)/ Pos/ RFE",
			   board_info->pg_ant_num,
			   board_info->btdm_ant_num_by_ant_det,
			   board_info->btdm_ant_pos,
			   rfe_type->rfe_module_type,
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
	/*btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER, &bt_patch_ver);*/
	bt_patch_ver = btcoexist->bt_info.bt_get_fw_ver;
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
	phyver = btcoexist->btc_get_bt_phydm_version(btcoexist);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %d_%02x/ 0x%02x/ 0x%02x (%s)",
		   "CoexVer WL/  BT_Desired/ BT_Report",
		   glcoex_ver_date_8822b_1ant, glcoex_ver_8822b_1ant,
		   glcoex_ver_btdesired_8822b_1ant,
		   ((coex_sta->bt_coex_supported_version & 0xff00) >>
		    8),
		   (((coex_sta->bt_coex_supported_version & 0xff00) >>
		     8) >=
		    glcoex_ver_btdesired_8822b_1ant ? "Match" :
		    "Mis-Match"));
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ v%d/ %c",
		   "W_FW/ B_FW/ Phy/ Kt",
		   fw_ver, bt_patch_ver, phyver,
		   coex_sta->cut_version + 65);
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

	pop_report_in_10s++;
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = [%s/ %d/ %d/ %d] ",
		   "BT [status/ rssi/ retryCnt/ popCnt]",
		   ((coex_sta->bt_disabled) ? ("disabled") :	((
		   coex_sta->c2h_bt_inquiry_page) ? ("inquiry/page scan")
			   : ((BT_8822B_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
			       coex_dm->bt_status) ? "non-connected idle" :
		((BT_8822B_1ANT_BT_STATUS_CONNECTED_IDLE == coex_dm->bt_status)
				       ? "connected-idle" : "busy")))),
		   coex_sta->bt_rssi - 100, coex_sta->bt_retry_cnt,
		   coex_sta->pop_event_cnt);
	CL_PRINTF(cli_buf);
	/*bt rssi */
	/*bt pop_even_cnt : bt retry*/

	if (pop_report_in_10s >= 5) {
		coex_sta->pop_event_cnt = 0;
		pop_report_in_10s = 0;
	}

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %d / %d / %d / %d / %d",
		   "SCO/HID/PAN/A2DP/Hi-Pri",
		   bt_link_info->sco_exist, bt_link_info->hid_exist,
		   bt_link_info->pan_exist, bt_link_info->a2dp_exist,
		   bt_link_info->bt_hi_pri_link_exist);
	CL_PRINTF(cli_buf);

	{
		bt_info_ext = coex_sta->bt_info_ext;

		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %s / %s / %d/ %x/ %02x",
			   "Role/A2DP Rate/Bitpool/Feature/Ver",
			   ((bt_link_info->slave_role) ? "Slave" : "Master"),
			   (bt_info_ext & BIT(0)) ? "BR" : "EDR",
			   coex_sta->a2dp_bit_pool,
			   coex_sta->bt_coex_supported_feature,
			((coex_sta->bt_coex_supported_version & 0xff00) >> 8));
		CL_PRINTF(cli_buf);
	}

	/*bt info*/
	for (i = 0; i < BT_INFO_SRC_8822B_1ANT_MAX; i++) {
		if (coex_sta->bt_info_c2h_cnt[i]) {
			CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
				"\r\n %-35s = %02x %02x %02x %02x %02x %02x %02x(%d)",
				   glbt_info_src_8822b_1ant[i],
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

	/*(ps)tdma*/
	ps_tdma_case = coex_dm->cur_ps_tdma;
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %02x %02x %02x %02x %02x case-%d (%s,%s)",
		   "PS TDMA",
		   coex_dm->ps_tdma_para[0], coex_dm->ps_tdma_para[1],
		   coex_dm->ps_tdma_para[2], coex_dm->ps_tdma_para[3],
		   coex_dm->ps_tdma_para[4], ps_tdma_case,
		   (coex_dm->cur_ps_tdma_on ? "On" : "Off"),
		   (coex_dm->auto_tdma_adjust ? "Adj" : "Fix"));

	CL_PRINTF(cli_buf);

	/*coex table type*/
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d",
		   "WL/BT Coex Table Type",
		   coex_sta->coex_table_type);
	CL_PRINTF(cli_buf);

	/*coex table :0x6c0 0x6c4 , break table: 0x6c8*/
	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x6c0);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x6c4);
	u32tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0x6c8);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x6c0/0x6c4/0x6c8(coexTable)",
		   u32tmp[0], u32tmp[1], u32tmp[2]);
	CL_PRINTF(cli_buf);

	/*PTA : 0x778=1/3/d , WL BA,RTS,CTS :0x6cc  H/L pri */
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x778);
	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x6cc);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x778/0x6cc/IgnWlanAct",
		   u8tmp[0], u32tmp[0],  coex_dm->cur_ignore_wlan_act);
	CL_PRINTF(cli_buf);


	/*LTE : WL/LTE coex table : 0xa0 */

	u32tmp[0] = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist,
			0xa0);

	/*LTE : BT/LTE coex table : 0xa4 */
	u32tmp[1] = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist,
			0xa4);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "LTE Coex Table W_L/B_L",
		   u32tmp[0] & 0xffff, u32tmp[1] & 0xffff);
	CL_PRINTF(cli_buf);

	/*LTE : WL/LTE break table : 0xa8 */
	u32tmp[0] = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist,
			0xa8);
	/*LTE : WL/LTE break table : 0xac */
	u32tmp[1] = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist,
			0xac);
	/*LTE : LTE/WL break table : 0xb0 */
	u32tmp[2] = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist,
			0xb0);
	/*LTE : LTE/BT break table : 0xb4 */
	u32tmp[3] = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist,
			0xb4);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x",
		   "LTE Break Table W_L/B_L/L_W/L_B",
		   u32tmp[0] & 0xffff, u32tmp[1] & 0xffff,
		   u32tmp[2] & 0xffff, u32tmp[3] & 0xffff);
	CL_PRINTF(cli_buf);

	/* Hw setting		 */
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
		   "============[Hw setting]============");
	CL_PRINTF(cli_buf);


	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x430);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x434);
	u16tmp[0] = btcoexist->btc_read_2byte(btcoexist, 0x42a);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x456);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/0x%x/0x%x/0x%x",
		   "0x430/0x434/0x42a/0x456",
		   u32tmp[0], u32tmp[1], u16tmp[0], u8tmp[0]);
	CL_PRINTF(cli_buf);

	/*ANT setting*/
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0xcb4);
	u8tmp[2] = btcoexist->btc_read_1byte(btcoexist, 0xcbd);
	u8tmp[3] = btcoexist->btc_read_1byte(btcoexist, 0x1991);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0xcb4/0xcbd/0x1991",
		   u8tmp[1], u8tmp[2], u8tmp[3]);
	CL_PRINTF(cli_buf);

	/*LTE on/off , Path ctrl owner*/
	/*sy add*/
	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x4c);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x73);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x64);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x73/ 0x4c/ 0x64[0]", u8tmp[0],
		   (u32tmp[0] & (BIT(24) | BIT(23))) >> 23,
		   u8tmp[1] & 0x1);
	CL_PRINTF(cli_buf);


	u32tmp[0] = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist, 0x38);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %s",
		   "LTE CoexOn/Path Ctrl Owner",
		   (int)((u32tmp[0]&BIT(7)) >> 7),
		   ((u8tmp[0]&BIT(2)) ? "WL" : "BT"));
	CL_PRINTF(cli_buf);

	/*LTE mode*/
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d",
		   "LTE 3Wire/OPMode/UART/UARTMode",
		   (int)((u32tmp[0]&BIT(6)) >> 6),
		   (int)((u32tmp[0] & (BIT(5) | BIT(4))) >> 4),
		   (int)((u32tmp[0]&BIT(3)) >> 3),
		   (int)(u32tmp[0] & (BIT(2) | BIT(1) | BIT(0))));
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %s",
		   "GNT_WL_SWCtrl/GNT_BT_SWCtrl/Dbg",
		   (int)((u32tmp[0]&BIT(12)) >> 12),
		   (int)((u32tmp[0]&BIT(14)) >> 14),
		   ((u8tmp[0]&BIT(3)) ? "On" : "Off"));
	CL_PRINTF(cli_buf);

	u32tmp[0] = halbtc8822b1ant_ltecoex_indirect_read_reg(btcoexist, 0x54);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d",
		   "GNT_WL/GNT_BT/LTE_Busy/UART_Busy",
		   (int)((u32tmp[0]&BIT(2)) >> 2),
		   (int)((u32tmp[0]&BIT(3)) >> 3),
		   (int)((u32tmp[0]&BIT(1)) >> 1), (int)(u32tmp[0]&BIT(0)));
	CL_PRINTF(cli_buf);


	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x4c6);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x40);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "0x4c6[4]/0x40[5] (WL/BT PTA)",
		   (int)((u8tmp[0] & BIT(4)) >> 4),
		   (int)((u8tmp[1] & BIT(5)) >> 5));
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x550);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x522);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x953);
	u8tmp[2] = btcoexist->btc_read_1byte(btcoexist, 0xc50);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ %s/ 0x%x",
		   "0x550/0x522/4-RxAGC/0xc50",
		u32tmp[0], u8tmp[0], (u8tmp[1] & 0x2) ? "On" : "Off", u8tmp[2]);
	CL_PRINTF(cli_buf);

#if 0 /* phydm v008 doesn't support this feature */
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
#endif


#if 1
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
#endif

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s/ %s",
		   "Wifi-HiPri/ Ccklock/ CckEverLock",
		   (coex_sta->wifi_is_high_pri_task ? "Yes" : "No"),
		   (coex_sta->cck_lock ? "Yes" : "No"),
		   (coex_sta->cck_ever_lock ? "Yes" : "No"));
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "0x770(high-pri rx/tx)",
		   coex_sta->high_priority_rx, coex_sta->high_priority_tx);
	CL_PRINTF(cli_buf);
	/*0x774:bt low pri trx*/
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "0x774(low-pri rx/tx)",
		   coex_sta->low_priority_rx, coex_sta->low_priority_tx);
	CL_PRINTF(cli_buf);

	halbtc8822b1ant_read_score_board(btcoexist,	&u16tmp[0]);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %04x",
		   "ScoreBoard[14:0] (from BT)", u16tmp[0]);

	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_COEX_STATISTICS);
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], ********** displaycoexinfo end, 0xcb4/0xcbd = 0x%x/0x%x\n",
		    btcoexist->btc_read_1byte(btcoexist, 0xcb4),
		    btcoexist->btc_read_1byte(btcoexist, 0xcbd));
	BTC_TRACE(trace_buf);
	/*
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], ********** pnp_wake_cnt = %d\n", pnp_wake_cnt);
	BTC_TRACE(trace_buf);
	*/
}


void ex_halbtc8822b1ant_ips_notify(IN struct btc_coexist *btcoexist, IN u8 type)
{

	if (btcoexist->manual_control ||	btcoexist->stop_coex_dm)
		return;


	if (BTC_IPS_ENTER == type) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], IPS ENTER notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_ips = true;

		/* Write WL "Active" in Score-board for LPS off */
		halbtc8822b1ant_post_state_to_bt(btcoexist,
				BT_8822B_1ANT_SCOREBOARD_ACTIVE, false);

		halbtc8822b1ant_post_state_to_bt(btcoexist,
				BT_8822B_1ANT_SCOREBOARD_ONOFF, false);

		halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);

		halbtc8822b1ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_AUTO,
					     FORCE_EXEC,
					     BT_8822B_1ANT_PHASE_WLAN_OFF);

		halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
	} else if (BTC_IPS_LEAVE == type) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], IPS LEAVE notify\n");
		BTC_TRACE(trace_buf);
		halbtc8822b1ant_post_state_to_bt(btcoexist,
				BT_8822B_1ANT_SCOREBOARD_ACTIVE, true);

		halbtc8822b1ant_post_state_to_bt(btcoexist,
				BT_8822B_1ANT_SCOREBOARD_ONOFF, true);

		/*leave IPS : run ini hw config (exclude wifi only)*/
		halbtc8822b1ant_init_hw_config(btcoexist, false, false);
		/*sw all off*/
		halbtc8822b1ant_init_coex_dm(btcoexist);
		/*leave IPS : Query bt info*/
		halbtc8822b1ant_query_bt_info(btcoexist);

		coex_sta->under_ips = false;
	}
}

void ex_halbtc8822b1ant_lps_notify(IN struct btc_coexist *btcoexist, IN u8 type)
{
	if (btcoexist->manual_control || btcoexist->stop_coex_dm)
		return;

	if (BTC_LPS_ENABLE == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], LPS ENABLE notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_lps = true;

		if (coex_sta->force_lps_on == true) { /* LPS No-32K */
			/* Write WL "Active" in Score-board for PS-TDMA */
			halbtc8822b1ant_post_state_to_bt(btcoexist,
					 BT_8822B_1ANT_SCOREBOARD_ACTIVE, true);

		} else { /* LPS-32K, need check if this h2c 0x71 can work?? (2015/08/28) */
			/* Write WL "Non-Active" in Score-board for Native-PS */
			halbtc8822b1ant_post_state_to_bt(btcoexist,
				 BT_8822B_1ANT_SCOREBOARD_ACTIVE, false);

		}
	} else if (BTC_LPS_DISABLE == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], LPS DISABLE notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_lps = false;

		/* Write WL "Active" in Score-board for LPS off */
		halbtc8822b1ant_post_state_to_bt(btcoexist,
					 BT_8822B_1ANT_SCOREBOARD_ACTIVE, true);

	}
}


void ex_halbtc8822b1ant_scan_notify(IN struct btc_coexist *btcoexist,
				    IN u8 type)
{
	boolean	wifi_connected = false;
	boolean wifi_under_5g = false;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], SCAN notify()\n");
	BTC_TRACE(trace_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);



	if (btcoexist->manual_control ||
	    btcoexist->stop_coex_dm)
		return;

	if (BTC_SCAN_START == type) {
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);

		if (wifi_under_5g) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], ********** (scan_notify_5g_scan_start) **********\n");
			BTC_TRACE(trace_buf);
			halbtc8822b1ant_action_wifi_under5g(btcoexist);
			return;
		}

		/* under 2.4G */
		coex_sta->wifi_is_high_pri_task = true;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], ********** (scan_notify_2g_scan_start) **********\n");
		BTC_TRACE(trace_buf);

		if (!wifi_connected) {	/* non-connected scan */
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], ********** wifi is not connected scan **********\n");
			BTC_TRACE(trace_buf);
			halbtc8822b1ant_action_wifi_not_connected_scan(
				btcoexist);
		} else {	/* wifi is connected */
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], ********** wifi is connected scan **********\n");
			BTC_TRACE(trace_buf);
			halbtc8822b1ant_action_wifi_connected_scan(
				btcoexist);
		}

		return;
	}

	if (BTC_SCAN_START_2G == type) {
		coex_sta->wifi_is_high_pri_task = true;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], ********** (scan_notify_2g_sacn_start_for_switch_band_used) **********\n");
		BTC_TRACE(trace_buf);

		if (!wifi_connected) {	/* non-connected scan */
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], ********** wifi is not connected **********\n");
			BTC_TRACE(trace_buf);

			halbtc8822b1ant_action_wifi_not_connected_scan(
				btcoexist);
		} else	{ /* wifi is connected */
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], ********** wifi is connected **********\n");
			BTC_TRACE(trace_buf);
			halbtc8822b1ant_action_wifi_connected_scan(btcoexist);
		}
	} else {
		coex_sta->wifi_is_high_pri_task = false;

		/*WL scan finish , then get and update sacn ap numbers */
		btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM,
				   &coex_sta->scan_ap_num);

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], ********** (scan_finish_notify) **********\n");
		BTC_TRACE(trace_buf);

		if (!wifi_connected)	/* non-connected scan */
			halbtc8822b1ant_action_wifi_not_connected(btcoexist);
		else {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], ********** scan_finish_notify wifi is connected **********\n");
			BTC_TRACE(trace_buf);
			halbtc8822b1ant_action_wifi_connected(btcoexist);
		}
	}
}



void ex_halbtc8822b1ant_scan_notify_without_bt(IN struct btc_coexist *btcoexist,
		IN u8 type)
{

	boolean wifi_under_5g = false;

	if (BTC_SCAN_START == type) {
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);

		if (wifi_under_5g) {
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcbd,
							   0x3, 1);
			return;
		}

		/* under 2.4G */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcbd,
						   0x3, 2);
		return;
	}
	if (BTC_SCAN_START_2G == type)
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcbd, 0x3, 2);
}

void ex_halbtc8822b1ant_switchband_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{

	boolean wifi_connected = false;


	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], ********** (switchband_notify) **********\n");
	BTC_TRACE(trace_buf);

	if (btcoexist->manual_control ||
	    btcoexist->stop_coex_dm)
		return;

	coex_sta->switch_band_notify_to = type;

	if (type == BTC_SWITCH_TO_5G) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], ********** (switchband_notify BTC_SWITCH_TO_5G) **********\n");
		BTC_TRACE(trace_buf);

		halbtc8822b1ant_action_wifi_under5g(btcoexist);
		return;
	} else if (type == BTC_SWITCH_TO_24G_NOFORSCAN) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], ********** (switchband_notify BTC_SWITCH_TO_2G (no for scan)) **********\n");
		BTC_TRACE(trace_buf);

		halbtc8822b1ant_run_coexist_mechanism(btcoexist);

	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], ********** (switchband_notify BTC_SWITCH_TO_2G) **********\n");
		BTC_TRACE(trace_buf);

		ex_halbtc8822b1ant_scan_notify(btcoexist,
					       BTC_SCAN_START_2G);
	}
	coex_sta->switch_band_notify_to = BTC_NOT_SWITCH;

}


void ex_halbtc8822b1ant_switchband_notify_without_bt(IN struct btc_coexist
		*btcoexist,
		IN u8 type)
{
	boolean wifi_under_5g = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);

	if (type == BTC_SWITCH_TO_5G) {

		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcbd,
						   0x3, 1);
		return;
	} else if (type == BTC_SWITCH_TO_24G_NOFORSCAN) {

		if (wifi_under_5g)

			btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcbd,
							   0x3, 1);

		else
			btcoexist->btc_write_1byte_bitmask(btcoexist,
							   0xcbd, 0x3, 2);
	} else {

		ex_halbtc8822b1ant_scan_notify_without_bt(btcoexist,
				BTC_SCAN_START_2G);
	}

}

void ex_halbtc8822b1ant_connect_notify(IN struct btc_coexist *btcoexist,
				       IN u8 type)
{
	boolean	wifi_connected = false;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], ********** (connect notify) **********\n");
	BTC_TRACE(trace_buf);

	halbtc8822b1ant_post_state_to_bt(btcoexist,
					 BT_8822B_1ANT_SCOREBOARD_SCAN, true);

	if (btcoexist->manual_control ||
	    btcoexist->stop_coex_dm)
		return;


	if ((BTC_ASSOCIATE_5G_START == type) ||
	    (BTC_ASSOCIATE_5G_FINISH == type)) {

		if (BTC_ASSOCIATE_5G_START == type) {

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], ********** (5G associate start notify) **********\n");
			BTC_TRACE(trace_buf);

			halbtc8822b1ant_action_wifi_under5g(btcoexist);

		} else if (BTC_ASSOCIATE_5G_FINISH == type) {

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], ********** (5G associate finish notify) **********\n");
			BTC_TRACE(trace_buf);

		}

		return;

	}


	if (BTC_ASSOCIATE_START == type) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], 2G CONNECT START notify\n");
		BTC_TRACE(trace_buf);

		coex_sta->wifi_is_high_pri_task = true;

		halbtc8822b1ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_AUTO,
					     FORCE_EXEC,
					     BT_8822B_1ANT_PHASE_2G_RUNTIME);

		coex_dm->arp_cnt = 0;

		halbtc8822b1ant_action_wifi_not_connected_asso_auth(btcoexist);

	} else {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], 2G CONNECT Finish notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->wifi_is_high_pri_task = false;

		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
				   &wifi_connected);

		if (!wifi_connected) /* non-connected scan */
			halbtc8822b1ant_action_wifi_not_connected(btcoexist);
		else
			halbtc8822b1ant_action_wifi_connected(btcoexist);
	}

}

void ex_halbtc8822b1ant_media_status_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	boolean			wifi_under_b_mode = false;
	boolean wifi_under_5g = false;
	u32			cnt_bt_cal_chk = 0;
	boolean			is_in_mp_mode = false;
	u8			u8tmp = 0;
	u32			u32tmp1 = 0, u32tmp2 = 0;


	if (btcoexist->manual_control ||
	    btcoexist->stop_coex_dm)
		return;


	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);




	if (BTC_MEDIA_CONNECT == type) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], 2g media connect notify");
		BTC_TRACE(trace_buf);

		halbtc8822b1ant_post_state_to_bt(btcoexist,
					 BT_8822B_1ANT_SCOREBOARD_ACTIVE, true);

		if (wifi_under_5g) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], 5g media notify\n");
BTC_TRACE(trace_buf);

			halbtc8822b1ant_action_wifi_under5g(btcoexist);
			return;
		}
		/* Force antenna setup for no scan result issue */
		halbtc8822b1ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO,
					     FORCE_EXEC,
					     BT_8822B_1ANT_PHASE_2G_RUNTIME);

		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_B_MODE,
				   &wifi_under_b_mode);

		/* Set CCK Tx/Rx high Pri except 11b mode */
		if (wifi_under_b_mode) {

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], ********** (media status notity under b mode) **********\n");
			BTC_TRACE(trace_buf);
			btcoexist->btc_write_1byte(btcoexist, 0x6cd,
						   0x00); /* CCK Tx */
			btcoexist->btc_write_1byte(btcoexist, 0x6cf,
						   0x00); /* CCK Rx */
		} else {
			/* btcoexist->btc_write_1byte(btcoexist, 0x6cd, 0x10); */ /*CCK Tx */
			/* btcoexist->btc_write_1byte(btcoexist, 0x6cf, 0x10); */ /*CCK Rx */
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], ********** (media status notity not under b mode) **********\n");
			BTC_TRACE(trace_buf);
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
			    "[BTCoex], 2g media disconnect notify\n");
		BTC_TRACE(trace_buf);
		coex_dm->arp_cnt = 0;

		halbtc8822b1ant_post_state_to_bt(btcoexist,
				 BT_8822B_1ANT_SCOREBOARD_ACTIVE, false);

		btcoexist->btc_write_1byte(btcoexist, 0x6cd, 0x0); /* CCK Tx */
		btcoexist->btc_write_1byte(btcoexist, 0x6cf, 0x0); /* CCK Rx */

		coex_sta->cck_ever_lock = false;
	}

	halbtc8822b1ant_update_wifi_channel_info(btcoexist, type);

}

void ex_halbtc8822b1ant_specific_packet_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	boolean	under_4way = false, wifi_under_5g = false;

	if (btcoexist->manual_control ||
	    btcoexist->stop_coex_dm)
		return;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);
	if (wifi_under_5g) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], 5g special packet notify\n");
		BTC_TRACE(trace_buf);

		halbtc8822b1ant_action_wifi_under5g(btcoexist);
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS,
			   &under_4way);

	if (under_4way) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], specific Packet ---- under_4way!!\n");
		BTC_TRACE(trace_buf);

		coex_sta->wifi_is_high_pri_task = true;
		coex_sta->specific_pkt_period_cnt = 2;
	} else if (BTC_PACKET_ARP == type) {

		coex_dm->arp_cnt++;

		if (coex_sta->wifi_is_high_pri_task) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], specific Packet ARP notify -cnt = %d\n",
				    coex_dm->arp_cnt);
			BTC_TRACE(trace_buf);
		}

	} else {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], specific Packet DHCP or EAPOL notify [Type = %d]\n",
			    type);
		BTC_TRACE(trace_buf);

		coex_sta->wifi_is_high_pri_task = true;
		coex_sta->specific_pkt_period_cnt = 2;
	}

	if (coex_sta->wifi_is_high_pri_task)
		halbtc8822b1ant_action_wifi_connected_specific_packet(
			btcoexist);

}

void ex_halbtc8822b1ant_bt_info_notify(IN struct btc_coexist *btcoexist,
				       IN u8 *tmp_buf, IN u8 length)
{
	u8				bt_info = 0;
	u8				i, rsp_source = 0;
	boolean				bt_busy = false;
	boolean				wifi_connected = false;
	boolean wifi_under_5g = false;


	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);

	coex_sta->c2h_bt_info_req_sent = false;
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], ********** (BtInfo Notify) **********\n");
	BTC_TRACE(trace_buf);

	rsp_source = tmp_buf[0] & 0xf;
	if (rsp_source >= BT_INFO_SRC_8822B_1ANT_MAX)
		rsp_source = BT_INFO_SRC_8822B_1ANT_WIFI_FW;
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


	if (BT_INFO_SRC_8822B_1ANT_WIFI_FW != rsp_source) {

		/* if 0xff, it means BT is under WHCK test */
		if (bt_info == 0xff)
			coex_sta->bt_whck_test = true;
		else
			coex_sta->bt_whck_test = false;

		coex_sta->bt_retry_cnt =	/* [3:0] */
			coex_sta->bt_info_c2h[rsp_source][2] & 0xf;

		if (coex_sta->bt_retry_cnt >= 1)
			coex_sta->pop_event_cnt++;

		if (coex_sta->bt_info_c2h[rsp_source][2] & 0x20)
			coex_sta->c2h_bt_page = true;
		else
			coex_sta->c2h_bt_page = false;

		if (coex_sta->bt_info_c2h[rsp_source][2] & 0x80)
			coex_sta->bt_create_connection = true;
		else
			coex_sta->bt_create_connection = false;

		/* unit: %, value-100 to translate to unit: dBm */
		coex_sta->bt_rssi = coex_sta->bt_info_c2h[rsp_source][3] * 2 +
				    10;

		/* coex_sta->bt_info_c2h[rsp_source][3] * 2 - 90; */

		if ((coex_sta->bt_info_c2h[rsp_source][1] & 0x49) == 0x49) {
			coex_sta->a2dp_bit_pool =
				coex_sta->bt_info_c2h[rsp_source][6];
		} else
			coex_sta->a2dp_bit_pool = 0;

		if (coex_sta->bt_info_c2h[rsp_source][1] & 0x9)
			coex_sta->acl_busy = true;
		else
			coex_sta->acl_busy = false;

		coex_sta->bt_info_ext =
			coex_sta->bt_info_c2h[rsp_source][4];

		/* Here we need to resend some wifi info to BT */
		/* because bt is reset and loss of the info. */

		if ((!btcoexist->manual_control) &&
		    (!btcoexist->stop_coex_dm)) {

			/*	Re-Init */
			if (coex_sta->bt_info_ext & BIT(1)) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT ext info bit1 check, send wifi BW&Chnl to BT!!\n");
				BTC_TRACE(trace_buf);
				btcoexist->btc_get(btcoexist,
						   BTC_GET_BL_WIFI_CONNECTED,
						   &wifi_connected);
				if (wifi_connected)
					halbtc8822b1ant_update_wifi_channel_info(
						btcoexist,
						BTC_MEDIA_CONNECT);
				else
					halbtc8822b1ant_update_wifi_channel_info(
						btcoexist,
						BTC_MEDIA_DISCONNECT);
			}

			/*	If Ignore_WLanAct && not SetUp_Link */
			if ((coex_sta->bt_info_ext & BIT(3)) &&
			    (!(coex_sta->bt_info_ext & BIT(2)))) {

				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT ext info bit3 check, set BT NOT to ignore Wlan active!!\n");
				BTC_TRACE(trace_buf);
				halbtc8822b1ant_ignore_wlan_act(btcoexist,
								FORCE_EXEC,
								false);
			}
		}
		/* check BIT2 first ==> check if bt is under inquiry or page scan */
		if (bt_info & BT_INFO_8822B_1ANT_B_INQ_PAGE)
			coex_sta->c2h_bt_inquiry_page = true;
		else
			coex_sta->c2h_bt_inquiry_page = false;
	}

	coex_sta->num_of_profile = 0;

	/* set link exist status */
	if (!(bt_info & BT_INFO_8822B_1ANT_B_CONNECTION)) {
		coex_sta->bt_link_exist = false;
		coex_sta->pan_exist = false;
		coex_sta->a2dp_exist = false;
		coex_sta->hid_exist = false;
		coex_sta->sco_exist = false;

		coex_sta->bt_hi_pri_link_exist = false;
	} else {	/* connection exists */
		coex_sta->bt_link_exist = true;
		if (bt_info & BT_INFO_8822B_1ANT_B_FTP) {
			coex_sta->pan_exist = true;
			coex_sta->num_of_profile++;
		} else
			coex_sta->pan_exist = false;
		if (bt_info & BT_INFO_8822B_1ANT_B_A2DP) {
			coex_sta->a2dp_exist = true;
			coex_sta->num_of_profile++;
		} else
			coex_sta->a2dp_exist = false;
		if (bt_info & BT_INFO_8822B_1ANT_B_HID) {
			coex_sta->hid_exist = true;
			coex_sta->num_of_profile++;
		} else
			coex_sta->hid_exist = false;
		if (bt_info & BT_INFO_8822B_1ANT_B_SCO_ESCO) {
			coex_sta->sco_exist = true;
			coex_sta->num_of_profile++;
		} else
			coex_sta->sco_exist = false;

	}

	halbtc8822b1ant_update_bt_link_info(btcoexist);

	bt_info = bt_info &
		0x1f;  /* mask profile bit for connect-ilde identification ( for CSR case: A2DP idle --> 0x41) */

	if (!(bt_info & BT_INFO_8822B_1ANT_B_CONNECTION)) {
		coex_dm->bt_status = BT_8822B_1ANT_BT_STATUS_NON_CONNECTED_IDLE;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], BtInfoNotify(), BT Non-Connected idle!!!\n");
		BTC_TRACE(trace_buf);
	} else if (bt_info ==
		BT_INFO_8822B_1ANT_B_CONNECTION) {	/* connection exists but no busy */
		coex_dm->bt_status = BT_8822B_1ANT_BT_STATUS_CONNECTED_IDLE;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT Connected-idle!!!\n");
		BTC_TRACE(trace_buf);
	} else if ((bt_info & BT_INFO_8822B_1ANT_B_SCO_ESCO) ||
		   (bt_info & BT_INFO_8822B_1ANT_B_SCO_BUSY)) {
		coex_dm->bt_status = BT_8822B_1ANT_BT_STATUS_SCO_BUSY;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT SCO busy!!!\n");
		BTC_TRACE(trace_buf);
	} else if (bt_info & BT_INFO_8822B_1ANT_B_ACL_BUSY) {
		if (BT_8822B_1ANT_BT_STATUS_ACL_BUSY != coex_dm->bt_status)
			coex_dm->bt_status = BT_8822B_1ANT_BT_STATUS_ACL_BUSY;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT ACL busy!!!\n");
		BTC_TRACE(trace_buf);
	} else {
		coex_dm->bt_status = BT_8822B_1ANT_BT_STATUS_MAX;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], BtInfoNotify(), BT Non-Defined state!!!\n");
		BTC_TRACE(trace_buf);
	}

	if ((BT_8822B_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) ||
	    (BT_8822B_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
	    (BT_8822B_1ANT_BT_STATUS_ACL_SCO_BUSY == coex_dm->bt_status))
		bt_busy = true;
	else
		bt_busy = false;

	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bt_busy);

	halbtc8822b1ant_run_coexist_mechanism(btcoexist);
}

void ex_halbtc8822b1ant_rf_status_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], RF Status notify\n");
	BTC_TRACE(trace_buf);

	if (BTC_RF_ON == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RF is turned ON!!\n");
		BTC_TRACE(trace_buf);
		btcoexist->stop_coex_dm = false;

		halbtc8822b1ant_post_state_to_bt(btcoexist,
					 BT_8822B_1ANT_SCOREBOARD_ACTIVE, true);
		halbtc8822b1ant_post_state_to_bt(btcoexist,
					 BT_8822B_1ANT_SCOREBOARD_ONOFF, true);

	} else if (BTC_RF_OFF == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RF is turned OFF!!\n");
		BTC_TRACE(trace_buf);

		halbtc8822b1ant_post_state_to_bt(btcoexist,
				 BT_8822B_1ANT_SCOREBOARD_ACTIVE, false);
		halbtc8822b1ant_post_state_to_bt(btcoexist,
					 BT_8822B_1ANT_SCOREBOARD_ONOFF, false);
		halbtc8822b1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 0);

		halbtc8822b1ant_set_ant_path(btcoexist,
					     BTC_ANT_PATH_AUTO,
					     FORCE_EXEC,
					     BT_8822B_1ANT_PHASE_WLAN_OFF);
		/* for test : s3 bt disppear , fail rate 1/600*/

		halbtc8822b1ant_ignore_wlan_act(btcoexist, FORCE_EXEC, true);

		btcoexist->stop_coex_dm = true;
	}
}

void ex_halbtc8822b1ant_halt_notify(IN struct btc_coexist *btcoexist)
{

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Halt notify\n");
	BTC_TRACE(trace_buf);

	halbtc8822b1ant_post_state_to_bt(btcoexist,
				 BT_8822B_1ANT_SCOREBOARD_ACTIVE, false);
	halbtc8822b1ant_post_state_to_bt(btcoexist,
					 BT_8822B_1ANT_SCOREBOARD_ONOFF, false);

	halbtc8822b1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 0);

	halbtc8822b1ant_set_ant_path(btcoexist,
				     BTC_ANT_PATH_AUTO,
				     FORCE_EXEC,
				     BT_8822B_1ANT_PHASE_WLAN_OFF);
	/* for test : s3 bt disppear , fail rate 1/600*/

	halbtc8822b1ant_ignore_wlan_act(btcoexist, FORCE_EXEC, true);

	ex_halbtc8822b1ant_media_status_notify(btcoexist, BTC_MEDIA_DISCONNECT);

	halbtc8822b1ant_enable_gnt_to_gpio(btcoexist, false);

	btcoexist->stop_coex_dm = true;
}


void ex_halbtc8822b1ant_pnp_notify(IN struct btc_coexist *btcoexist,
				   IN u8 pnp_state)
{
	boolean wifi_under_5g = false;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Pnp notify\n");
	BTC_TRACE(trace_buf);

btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);
	if (BTC_WIFI_PNP_SLEEP == pnp_state) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Pnp notify to SLEEP\n");
		BTC_TRACE(trace_buf);

		halbtc8822b1ant_post_state_to_bt(btcoexist,
				 BT_8822B_1ANT_SCOREBOARD_ACTIVE, false);
		halbtc8822b1ant_post_state_to_bt(btcoexist,
					 BT_8822B_1ANT_SCOREBOARD_ONOFF, false);
		/*
				halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);

				halbtc8822b1ant_set_ant_path(btcoexist,
									BTC_ANT_PATH_AUTO,
								FORCE_EXEC,
								BT_8822B_1ANT_PHASE_WLAN_OFF);

				halbtc8822b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
								     5);

				halbtc8822b1ant_enable_gnt_to_gpio(btcoexist, false);
		*/
		btcoexist->stop_coex_dm = true;
	} else if (BTC_WIFI_PNP_WAKE_UP == pnp_state) {
		/*pnp_wake_cnt++;*/
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Pnp notify to WAKE UP\n");
		BTC_TRACE(trace_buf);

		halbtc8822b1ant_post_state_to_bt(btcoexist,
					 BT_8822B_1ANT_SCOREBOARD_ACTIVE, true);
		halbtc8822b1ant_post_state_to_bt(btcoexist,
					 BT_8822B_1ANT_SCOREBOARD_ONOFF, true);

		btcoexist->stop_coex_dm = false;
	}
}

void ex_halbtc8822b1ant_coex_dm_reset(IN struct btc_coexist *btcoexist)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], *****************Coex DM Reset*****************\n");
	BTC_TRACE(trace_buf);

	halbtc8822b1ant_init_hw_config(btcoexist, false, false);
	halbtc8822b1ant_init_coex_dm(btcoexist);
}

void ex_halbtc8822b1ant_periodical(IN struct btc_coexist *btcoexist)
{

	struct  btc_board_info	*board_info = &btcoexist->board_info;
	boolean wifi_busy = false;
	u16 bt_scoreboard_val = 0;
	u32 bt_patch_ver;
	static u8 cnt = 0;
	boolean bt_relink_finish = false;
	boolean rf4ce_connected = false;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], ==========================Periodical===========================\n");
	BTC_TRACE(trace_buf);

#if (BT_AUTO_REPORT_ONLY_8822B_1ANT == 0)
	halbtc8822b1ant_query_bt_info(btcoexist);
#endif

	halbtc8822b1ant_monitor_bt_ctr(btcoexist);
	halbtc8822b1ant_monitor_wifi_ctr(btcoexist);

	halbtc8822b1ant_monitor_bt_enable_disable(btcoexist);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	halbtc8822b1ant_read_score_board(btcoexist, &bt_scoreboard_val);

	/*RF4CE for arris , rf4ce always on*/
	/*
	btcoexist->btc_get(btcoexist, BTC_GET_BL_RF4CE_CONNECTED, &rf4ce_connected);
	if (rf4ce_connected){
		btcoexist->btc_write_1byte_bitmask(
					btcoexist, 0x45e, 0x8, 0x1);

				halbtc8822b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true,
							50);

				halbtc8822b1ant_coex_table_with_type(btcoexist,
				NORMAL_EXEC, 1);
				return;
		}
		*/

	if (wifi_busy) {
		/*WL scoreboard to BT,BT see WL Ccoreboard bit[6]=1 WL BUSY*/
		halbtc8822b1ant_post_state_to_bt(btcoexist,
			BT_8822B_1ANT_SCOREBOARD_WLBUSY, true);
		/*BT scoreboard to WL  , let WL see BT scoreboard bit[6]=1*/
		if (bt_scoreboard_val & BIT(6))
			/*btcoexist->btc_set_bt_wake(btcoexist, 45, 1);*/
			halbtc8822b1ant_query_bt_info(btcoexist);
	} else {
		halbtc8822b1ant_post_state_to_bt(btcoexist,
			BT_8822B_1ANT_SCOREBOARD_WLBUSY, false);
	}

	/* for 4-way, DHCP, EAPOL packet */
	if (coex_sta->specific_pkt_period_cnt > 0) {

		coex_sta->specific_pkt_period_cnt--;

		if ((coex_sta->specific_pkt_period_cnt == 0) &&
		    (coex_sta->wifi_is_high_pri_task))
			coex_sta->wifi_is_high_pri_task = false;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], ***************** Hi-Pri Task = %s*****************\n",
			    (coex_sta->wifi_is_high_pri_task ? "Yes" :
			     "No"));
		BTC_TRACE(trace_buf);
	}

	if (!coex_sta->bt_disabled) {
		if (coex_sta->bt_coex_supported_feature == 0)
			btcoexist->btc_get(btcoexist, BTC_GET_U4_SUPPORTED_FEATURE, &coex_sta->bt_coex_supported_feature);

		if ((coex_sta->bt_coex_supported_version == 0) ||
			(coex_sta->bt_coex_supported_version == 0xffff))
			btcoexist->btc_get(btcoexist, BTC_GET_U4_SUPPORTED_VERSION, &coex_sta->bt_coex_supported_version);

		btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER, &bt_patch_ver);
		btcoexist->bt_info.bt_get_fw_ver = bt_patch_ver;

		if (halbtc8822b1ant_is_wifi_status_changed(btcoexist))
			halbtc8822b1ant_run_coexist_mechanism(btcoexist);

	}
}

void ex_halbtc8822b1ant_antenna_detection(IN struct btc_coexist *btcoexist,
		IN u32 cent_freq, IN u32 offset, IN u32 span, IN u32 seconds)
{
}

void ex_halbtc8822b1ant_antenna_isolation(IN struct btc_coexist *btcoexist,
		IN u32 cent_freq, IN u32 offset, IN u32 span, IN u32 seconds)
{


}

void ex_halbtc8822b1ant_psd_scan(IN struct btc_coexist *btcoexist,
		 IN u32 cent_freq, IN u32 offset, IN u32 span, IN u32 seconds)
{


}

void ex_halbtc8822b1ant_display_ant_detection(IN struct btc_coexist *btcoexist)
{

}

void ex_halbtc8822b1ant_dbg_control(IN struct btc_coexist *btcoexist,
				    IN u8 op_code, IN u8 op_len, IN u8 *pdata)
{}
#endif	/*  #if(BTC_COEX_OFFLOAD == 1) */

#endif

#else

void ex_halbtc8822b1ant_switch_band_without_bt(IN struct btc_coexist *btcoexist,
		IN boolean wifi_only_5g)
{
	/* ant switch WL2G or WL5G*/
	if (wifi_only_5g)

		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcbd, 0x3, 1);

	else

		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcbd, 0x3, 2);

}

void ex_halbtc8822b1ant_init_hw_config_without_bt(IN struct btc_coexist
		*btcoexist)
{
	/* enable GNT_WL */

	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x1,
					   0x0);

	/* Antenna config */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4e,
					   0x80, 0x0);  /*  0x4c[23] = 0 */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4f,
					   0x01, 0x1);  /* 0x4c[24] = 1 */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcb4,
		0xff, 0x77);	/* BB SW, DPDT use RFE_ctrl8 and RFE_ctrl9 as conctrol pin */


	/* Ext switch buffer mux */
	btcoexist->btc_write_1byte(btcoexist, 0x974, 0xff);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x1991,
					   0x3, 0x0);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcbe,
					   0x8, 0x0);

	/*enable sw control gnt_wl=1 / gnt_bt=1 */
	btcoexist->btc_write_1byte(btcoexist, 0x73, 0x0e);

	btcoexist->btc_write_4byte(btcoexist, 0x1704,
				   0x0000ff00);

	btcoexist->btc_write_4byte(btcoexist, 0x1700,
				   0xc00f0038);
}

#endif	/* #if (BT_SUPPORT == 1 && COEX_SUPPORT == 1) */



