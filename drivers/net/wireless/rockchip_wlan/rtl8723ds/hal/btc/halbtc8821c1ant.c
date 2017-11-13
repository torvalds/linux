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
 * This file is for RTL8821C Co-exist mechanism
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

#if (RTL8821C_SUPPORT == 1)
/* ************************************************************
 * Global variables, these are static variables
 * ************************************************************ */
static u8	*trace_buf = &gl_btc_trace_buf[0];
static struct  coex_dm_8821c_1ant		glcoex_dm_8821c_1ant;
static struct  coex_dm_8821c_1ant	*coex_dm = &glcoex_dm_8821c_1ant;
static struct  coex_sta_8821c_1ant		glcoex_sta_8821c_1ant;
static struct  coex_sta_8821c_1ant	*coex_sta = &glcoex_sta_8821c_1ant;
static struct  psdscan_sta_8821c_1ant		gl_psd_scan_8821c_1ant;
static struct  psdscan_sta_8821c_1ant	*psd_scan = &gl_psd_scan_8821c_1ant;
static struct	rfe_type_8821c_1ant		gl_rfe_type_8821c_1ant;
static struct	rfe_type_8821c_1ant		*rfe_type = &gl_rfe_type_8821c_1ant;


const char *const glbt_info_src_8821c_1ant[] = {
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

u32	glcoex_ver_date_8821c_1ant = 20170727;
u32	glcoex_ver_8821c_1ant = 0x1b;
u32	glcoex_ver_btdesired_8821c_1ant = 0x1b;


/* ************************************************************
 * local function proto type if needed
 * ************************************************************
 * ************************************************************
 * local function start with halbtc8821c1ant_
 * ************************************************************ */
u8 halbtc8821c1ant_bt_rssi_state(u8 level_num, u8 rssi_thresh, u8 rssi_thresh1)
{
	s32			bt_rssi = 0;
	u8			bt_rssi_state = coex_sta->pre_bt_rssi_state;

	bt_rssi = coex_sta->bt_rssi;

	if (level_num == 2) {
		if ((coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_bt_rssi_state ==
		     BTC_RSSI_STATE_STAY_LOW)) {
			if (bt_rssi >= (rssi_thresh +
					BTC_RSSI_COEX_THRESH_TOL_8821C_1ANT))
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
					BTC_RSSI_COEX_THRESH_TOL_8821C_1ANT))
				bt_rssi_state = BTC_RSSI_STATE_MEDIUM;
			else
				bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
		} else if ((coex_sta->pre_bt_rssi_state ==
			    BTC_RSSI_STATE_MEDIUM) ||
			   (coex_sta->pre_bt_rssi_state ==
			    BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (bt_rssi >= (rssi_thresh1 +
					BTC_RSSI_COEX_THRESH_TOL_8821C_1ANT))
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

u8 halbtc8821c1ant_wifi_rssi_state(IN struct btc_coexist *btcoexist,
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
					  BTC_RSSI_COEX_THRESH_TOL_8821C_1ANT))
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
					  BTC_RSSI_COEX_THRESH_TOL_8821C_1ANT))
				wifi_rssi_state = BTC_RSSI_STATE_MEDIUM;
			else
				wifi_rssi_state = BTC_RSSI_STATE_STAY_LOW;
		} else if ((coex_sta->pre_wifi_rssi_state[index] ==
			    BTC_RSSI_STATE_MEDIUM) ||
			   (coex_sta->pre_wifi_rssi_state[index] ==
			    BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (wifi_rssi >= (rssi_thresh1 +
					  BTC_RSSI_COEX_THRESH_TOL_8821C_1ANT))
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

void halbtc8821c1ant_update_ra_mask(IN struct btc_coexist *btcoexist,
				    IN boolean force_exec, IN u32 dis_rate_mask)
{
	coex_dm->cur_ra_mask = dis_rate_mask;

	if (force_exec || (coex_dm->pre_ra_mask != coex_dm->cur_ra_mask))
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_UPDATE_RAMASK,
				   &coex_dm->cur_ra_mask);
	coex_dm->pre_ra_mask = coex_dm->cur_ra_mask;
}

void halbtc8821c1ant_auto_rate_fallback_retry(IN struct btc_coexist *btcoexist,
		IN boolean force_exec, IN u8 type)
{
	boolean	wifi_under_b_mode = FALSE;

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

void halbtc8821c1ant_retry_limit(IN struct btc_coexist *btcoexist,
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

void halbtc8821c1ant_ampdu_max_time(IN struct btc_coexist *btcoexist,
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

void halbtc8821c1ant_limited_tx(IN struct btc_coexist *btcoexist,
		IN boolean force_exec, IN u8 ra_mask_type, IN u8 arfr_type,
				IN u8 retry_limit_type, IN u8 ampdu_time_type)
{
	switch (ra_mask_type) {
	case 0:	/* normal mode */
		halbtc8821c1ant_update_ra_mask(btcoexist, force_exec,
					       0x0);
		break;
	case 1:	/* disable cck 1/2 */
		halbtc8821c1ant_update_ra_mask(btcoexist, force_exec,
					       0x00000003);
		break;
	case 2:	/* disable cck 1/2/5.5, ofdm 6/9/12/18/24, mcs 0/1/2/3/4 */
		halbtc8821c1ant_update_ra_mask(btcoexist, force_exec,
					       0x0001f1f7);
		break;
	default:
		break;
	}

	halbtc8821c1ant_auto_rate_fallback_retry(btcoexist, force_exec, arfr_type);
	halbtc8821c1ant_retry_limit(btcoexist, force_exec, retry_limit_type);
	halbtc8821c1ant_ampdu_max_time(btcoexist, force_exec, ampdu_time_type);
}

void halbtc8821c1ant_limited_rx(IN struct btc_coexist *btcoexist,
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

void halbtc8821c1ant_query_bt_info(IN struct btc_coexist *btcoexist)
{
	u8			h2c_parameter[1] = {0};

	if (coex_sta->bt_disabled) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], No query BT info because BT is disabled!\n");
		BTC_TRACE(trace_buf);
		return;
	}


	h2c_parameter[0] |= BIT(0);	/* trigger */

	btcoexist->btc_fill_h2c(btcoexist, 0x61, 1, h2c_parameter);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], WL query BT info!!\n");
	BTC_TRACE(trace_buf);
}

void halbtc8821c1ant_monitor_bt_ctr(IN struct btc_coexist *btcoexist)
{
	u32			reg_hp_txrx, reg_lp_txrx, u32tmp;
	u32			reg_hp_tx = 0, reg_hp_rx = 0, reg_lp_tx = 0, reg_lp_rx = 0;
	static u8		num_of_bt_counter_chk = 0, cnt_slave = 0, cnt_overhead = 0,
				cnt_autoslot_hang = 0;
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

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], Hi-Pri Rx/Tx: %d/%d, Lo-Pri Rx/Tx: %d/%d\n",
		    reg_hp_rx, reg_hp_tx, reg_lp_rx, reg_lp_tx);

	BTC_TRACE(trace_buf);

	if (BT_8821C_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
	    coex_dm->bt_status) {

		if (coex_sta->high_priority_rx >= 15) {
			if (cnt_overhead < 3)
				cnt_overhead++;

			if (cnt_overhead == 3)
				coex_sta->is_hiPri_rx_overhead = TRUE;

		} else {
			if (cnt_overhead > 0)
				cnt_overhead--;

			if (cnt_overhead == 0)
				coex_sta->is_hiPri_rx_overhead = FALSE;
		}
	} else
		coex_sta->is_hiPri_rx_overhead = FALSE;

	/* reset counter */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0xc);

	if ((coex_sta->low_priority_tx > 1150)  &&
	    (!coex_sta->c2h_bt_inquiry_page))
		coex_sta->pop_event_cnt++;

	if ((coex_sta->low_priority_rx >= 1150) &&
	    (coex_sta->low_priority_rx >= coex_sta->low_priority_tx)
	    && (!coex_sta->under_ips)
	    && (!coex_sta->c2h_bt_inquiry_page)
	    && ((bt_link_info->a2dp_exist) || (bt_link_info->pan_exist))) {
		if (cnt_slave >= 2) {
			bt_link_info->slave_role = TRUE;
			cnt_slave = 2;
		} else
			cnt_slave++;
	} else {
		if (cnt_slave == 0)	{
			bt_link_info->slave_role = FALSE;
			cnt_slave = 0;
		} else
			cnt_slave--;
	}

	if (coex_sta->is_tdma_btautoslot) {
		if ((coex_sta->low_priority_tx >= 1300) &&
		(coex_sta->low_priority_rx <= 150)) {
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

	if (bt_link_info->hid_only) {
		if (coex_sta->low_priority_rx > 50)
			coex_sta->is_hid_low_pri_tx_overhead = true;
		else
			coex_sta->is_hid_low_pri_tx_overhead = false;
	}

	if (!coex_sta->bt_disabled) {

		if ((coex_sta->high_priority_tx == 0) &&
		    (coex_sta->high_priority_rx == 0) &&
		    (coex_sta->low_priority_tx == 0) &&
		    (coex_sta->low_priority_rx == 0)) {
			num_of_bt_counter_chk++;
			if (num_of_bt_counter_chk >= 3) {
				halbtc8821c1ant_query_bt_info(btcoexist);
				num_of_bt_counter_chk = 0;
			}
		}
	}

}



void halbtc8821c1ant_monitor_wifi_ctr(IN struct btc_coexist *btcoexist)
{
	s32	wifi_rssi = 0;
	boolean wifi_busy = FALSE, wifi_under_b_mode = FALSE,
			wifi_scan = FALSE, wifi_connected = FALSE;
	boolean bt_idle = FALSE, wl_idle = FALSE, is_cck_deadlock = FALSE;
	static u8 cck_lock_counter = 0, wl_noisy_count0 = 0,
		  wl_noisy_count1 = 3, wl_noisy_count2 = 0;
	u32	total_cnt, reg_val1, reg_val2, cnt_cck;
	u32 cnt_crcok = 0, cnt_crcerr = 0;
	static u8 cnt = 0, cnt_ccklocking = 0;
	u8	h2c_parameter[1] = {0};
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	/* Only enable for windows becaus 8821cu H2C 0x69 unknown fail @ linux */
	if (btcoexist->chip_interface != BTC_INTF_USB) {
		/*send h2c to query WL FW dbg info  */
		if (((coex_dm->cur_ps_tdma_on) && (coex_sta->force_lps_ctrl)) ||
			 ((coex_sta->acl_busy) && (bt_link_info->a2dp_exist))) {
			h2c_parameter[0] = 0x8;
			btcoexist->btc_fill_h2c(btcoexist, 0x69, 1, h2c_parameter);
		}
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

	/* CCK lock identification */
	if (coex_sta->cck_lock)
		cnt_ccklocking++;
	else if	(cnt_ccklocking != 0)
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


void halbtc8821c1ant_update_bt_link_info(IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info	*bt_link_info = &btcoexist->bt_link_info;
	boolean				bt_hs_on = FALSE;
	boolean		bt_busy = FALSE;


	coex_sta->num_of_profile = 0;

	/* set link exist status */
	if (!(coex_sta->bt_info & BT_INFO_8821C_1ANT_B_CONNECTION)) {
		coex_sta->bt_link_exist = FALSE;
		coex_sta->pan_exist = FALSE;
		coex_sta->a2dp_exist = FALSE;
		coex_sta->hid_exist = FALSE;
		coex_sta->sco_exist = FALSE;
	} else {	/* connection exists */
		coex_sta->bt_link_exist = TRUE;
		if (coex_sta->bt_info & BT_INFO_8821C_1ANT_B_FTP) {
			coex_sta->pan_exist = TRUE;
			coex_sta->num_of_profile++;
		} else
			coex_sta->pan_exist = FALSE;

		if (coex_sta->bt_info & BT_INFO_8821C_1ANT_B_A2DP) {
			coex_sta->a2dp_exist = TRUE;
			coex_sta->num_of_profile++;
		} else
			coex_sta->a2dp_exist = FALSE;

		if (coex_sta->bt_info & BT_INFO_8821C_1ANT_B_HID) {
			coex_sta->hid_exist = TRUE;
			coex_sta->num_of_profile++;
		} else
			coex_sta->hid_exist = FALSE;

		if (coex_sta->bt_info & BT_INFO_8821C_1ANT_B_SCO_ESCO) {
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

	if (coex_sta->bt_info & BT_INFO_8821C_1ANT_B_INQ_PAGE) {
		coex_dm->bt_status = BT_8821C_1ANT_BT_STATUS_INQ_PAGE;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT Inq/page!!!\n");
	} else if (!(coex_sta->bt_info & BT_INFO_8821C_1ANT_B_CONNECTION)) {
		coex_dm->bt_status = BT_8821C_1ANT_BT_STATUS_NON_CONNECTED_IDLE;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], BtInfoNotify(), BT Non-Connected idle!!!\n");
	} else if (coex_sta->bt_info == BT_INFO_8821C_1ANT_B_CONNECTION) {
		/* connection exists but no busy */
		coex_dm->bt_status = BT_8821C_1ANT_BT_STATUS_CONNECTED_IDLE;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT Connected-idle!!!\n");
	} else if (((coex_sta->bt_info & BT_INFO_8821C_1ANT_B_SCO_ESCO) ||
		    (coex_sta->bt_info & BT_INFO_8821C_1ANT_B_SCO_BUSY)) &&
		   (coex_sta->bt_info & BT_INFO_8821C_1ANT_B_ACL_BUSY)) {
		coex_dm->bt_status = BT_8821C_1ANT_BT_STATUS_ACL_SCO_BUSY;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT ACL SCO busy!!!\n");
	} else if ((coex_sta->bt_info & BT_INFO_8821C_1ANT_B_SCO_ESCO) ||
		   (coex_sta->bt_info & BT_INFO_8821C_1ANT_B_SCO_BUSY)) {
		coex_dm->bt_status = BT_8821C_1ANT_BT_STATUS_SCO_BUSY;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT SCO busy!!!\n");
	} else if (coex_sta->bt_info & BT_INFO_8821C_1ANT_B_ACL_BUSY) {
		coex_dm->bt_status = BT_8821C_1ANT_BT_STATUS_ACL_BUSY;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT ACL busy!!!\n");
	} else {
		coex_dm->bt_status = BT_8821C_1ANT_BT_STATUS_MAX;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], BtInfoNotify(), BT Non-Defined state!!!\n");
	}

	BTC_TRACE(trace_buf);

	if ((BT_8821C_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) ||
	    (BT_8821C_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
	    (BT_8821C_1ANT_BT_STATUS_ACL_SCO_BUSY == coex_dm->bt_status))
		bt_busy = TRUE;
	else
		bt_busy = FALSE;

	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bt_busy);
}

void halbtc8821c1ant_update_wifi_channel_info(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	u8			h2c_parameter[3] = {0}, i;
	u32			wifi_bw;
	u8			wifi_central_chnl = 0;
	u8			wifi_5g_chnl[19] = {120, 124, 128, 132, 136, 140, 144, 149,
									153, 157, 118, 126, 134, 142, 151, 159,
									122, 138, 155};
	u8			bt_skip_cneter_chanl[19] = {2, 8, 17, 26, 34, 42, 51, 62,
											71, 77, 2, 12, 29, 46, 66, 76,
											10, 37, 68};
	u8			bt_skip_span[19] = {4, 8, 8, 10, 8, 10, 8, 8, 10, 4, 4, 16,
									16, 16, 16, 4, 20, 34, 20};
	boolean		wifi_under_5g = FALSE;

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);

	if (type == BTC_MEDIA_CONNECT) {

		if (!wifi_under_5g) {

			btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_CENTRAL_CHNL,
				&wifi_central_chnl);

			h2c_parameter[0] = 0x1;
			h2c_parameter[1] = wifi_central_chnl;

			if (wifi_bw == BTC_WIFI_BW_HT40)
				h2c_parameter[2] = 0x30;
			else
				h2c_parameter[2] = 0x20;
		} else {  /* for 5G  */

			wifi_central_chnl =
				btcoexist->btc_get_rf_reg(btcoexist, BTC_RF_A, 0x18, 0xfffff) & 0xff;

			for (i = 0; i <= 18; i++) {
				if (wifi_central_chnl == wifi_5g_chnl[i])
					break;
			}

			if (i <= 18) {
				h2c_parameter[0] = 0x3;
				h2c_parameter[1] = bt_skip_cneter_chanl[i];
				h2c_parameter[2] = bt_skip_span[i];
			}

		}
	}

	coex_dm->wifi_chnl_info[0] = h2c_parameter[0];
	coex_dm->wifi_chnl_info[1] = h2c_parameter[1];
	coex_dm->wifi_chnl_info[2] = h2c_parameter[2];
	coex_sta->wl_center_channel = wifi_central_chnl;

	btcoexist->btc_fill_h2c(btcoexist, 0x66, 3, h2c_parameter);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], para[0:2] = 0x%x 0x%x 0x%x\n",
				h2c_parameter[0], h2c_parameter[1], h2c_parameter[2]);
	BTC_TRACE(trace_buf);

}

u8 halbtc8821c1ant_action_algorithm(IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info	*bt_link_info = &btcoexist->bt_link_info;
	boolean				bt_hs_on = FALSE;
	u8				algorithm = BT_8821C_1ANT_COEX_ALGO_UNDEFINED;
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
			algorithm = BT_8821C_1ANT_COEX_ALGO_SCO;
		} else {
			if (bt_link_info->hid_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT Profile = HID only\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8821C_1ANT_COEX_ALGO_HID;
			} else if (bt_link_info->a2dp_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT Profile = A2DP only\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8821C_1ANT_COEX_ALGO_A2DP;
			} else if (bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = PAN(HS) only\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821C_1ANT_COEX_ALGO_PANHS;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = PAN(EDR) only\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821C_1ANT_COEX_ALGO_PANEDR;
				}
			}
		}
	} else if (num_of_diff_profile == 2) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT Profile = SCO + HID\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8821C_1ANT_COEX_ALGO_HID;
			} else if (bt_link_info->a2dp_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT Profile = SCO + A2DP ==> SCO\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8821C_1ANT_COEX_ALGO_SCO;
			} else if (bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = SCO + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm = BT_8821C_1ANT_COEX_ALGO_SCO;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = SCO + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821C_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (bt_link_info->hid_exist &&
			    bt_link_info->a2dp_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT Profile = HID + A2DP\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8821C_1ANT_COEX_ALGO_HID_A2DP;
			} else if (bt_link_info->hid_exist &&
				   bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = HID + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821C_1ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = HID + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821C_1ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (bt_link_info->pan_exist &&
				   bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = A2DP + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821C_1ANT_COEX_ALGO_A2DP_PANHS;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = A2DP + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821C_1ANT_COEX_ALGO_PANEDR_A2DP;
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
				algorithm = BT_8821C_1ANT_COEX_ALGO_HID;
			} else if (bt_link_info->hid_exist &&
				   bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = SCO + HID + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821C_1ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = SCO + HID + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821C_1ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (bt_link_info->pan_exist &&
				   bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = SCO + A2DP + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm = BT_8821C_1ANT_COEX_ALGO_SCO;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = SCO + A2DP + PAN(EDR) ==> HID\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821C_1ANT_COEX_ALGO_PANEDR_HID;
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
						BT_8821C_1ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], BT Profile = HID + A2DP + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821C_1ANT_COEX_ALGO_HID_A2DP_PANEDR;
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
						BT_8821C_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
	}

	return algorithm;
}

void halbtc8821c1ant_set_bt_auto_report(IN struct btc_coexist *btcoexist,
					IN boolean enable_auto_report)
{
	u8			h2c_parameter[1] = {0};

	h2c_parameter[0] = 0;

	if (enable_auto_report)
		h2c_parameter[0] |= BIT(0);

	btcoexist->btc_fill_h2c(btcoexist, 0x68, 1, h2c_parameter);
}

void halbtc8821c1ant_bt_auto_report(IN struct btc_coexist *btcoexist,
		    IN boolean force_exec, IN boolean enable_auto_report)
{
	coex_dm->cur_bt_auto_report = enable_auto_report;

	if (!force_exec) {
		if (coex_dm->pre_bt_auto_report == coex_dm->cur_bt_auto_report)
			return;
	}
	halbtc8821c1ant_set_bt_auto_report(btcoexist,
					   coex_dm->cur_bt_auto_report);

	coex_dm->pre_bt_auto_report = coex_dm->cur_bt_auto_report;
}


void halbtc8821c1ant_low_penalty_ra(IN struct btc_coexist *btcoexist,
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
		btcoexist->btc_phydm_modify_RA_PCR_threshold(btcoexist, 0, 15);
	else
		btcoexist->btc_phydm_modify_RA_PCR_threshold(btcoexist, 0, 0);

	coex_dm->pre_low_penalty_ra = coex_dm->cur_low_penalty_ra;

#endif

}

void halbtc8821c1ant_write_score_board(
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
		"[BTCoex], halbtc8821c1ant_write_score_board: return for nochange\n");
		BTC_TRACE(trace_buf);
	}
}

void halbtc8821c1ant_read_score_board(
	IN	struct  btc_coexist		*btcoexist,
	IN   u16				*score_board_val
)
{

	*score_board_val = (btcoexist->btc_read_2byte(btcoexist,
			    0xaa)) & 0x7fff;
}

void halbtc8821c1ant_post_state_to_bt(
	IN	struct  btc_coexist		*btcoexist,
	IN	u16						type,
	IN  boolean                 state
)
{

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], halbtc8821c1ant_post_state_to_bt: type = %d, state =%d\n",
		type, state);
	BTC_TRACE(trace_buf);

	halbtc8821c1ant_write_score_board(btcoexist, (u16) type, state);
}

boolean halbtc8821c1ant_is_wifibt_status_changed(IN struct btc_coexist
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
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

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
		return TRUE;
	}


	if (wifi_connected) {
#if 0
		if ((wifi_busy) &&
			(coex_dm->bt_status ==  BT_8821C_1ANT_BT_STATUS_ACL_BUSY))
			halbtc8821c1ant_post_state_to_bt(btcoexist,
						BT_8821C_1ANT_SCOREBOARD_UNDERTEST, TRUE);
		else
			halbtc8821c1ant_post_state_to_bt(btcoexist,
						BT_8821C_1ANT_SCOREBOARD_UNDERTEST, FALSE);
#endif
		if (wifi_busy != pre_wifi_busy) {
			pre_wifi_busy = wifi_busy;
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

		if (pre_bt_setup_link != coex_sta->is_setupLink) {
			pre_bt_setup_link = coex_sta->is_setupLink;
			return TRUE;
		}
	}

	return FALSE;
}


void halbtc8821c1ant_monitor_bt_enable_disable(IN struct btc_coexist *btcoexist)
{
	static u32		bt_disable_cnt = 0;
	boolean		bt_active = TRUE, bt_disabled = FALSE, wifi_under_5g = FALSE;
	u16			u16tmp;

	/* This function check if bt is disabled */
#if 0
	if (coex_sta->high_priority_tx == 0 &&
	    coex_sta->high_priority_rx == 0 &&
	    coex_sta->low_priority_tx == 0 &&
	    coex_sta->low_priority_rx == 0)
		bt_active = FALSE;
	if (coex_sta->high_priority_tx == 0xffff &&
	    coex_sta->high_priority_rx == 0xffff &&
	    coex_sta->low_priority_tx == 0xffff &&
	    coex_sta->low_priority_rx == 0xffff)
		bt_active = FALSE;


#else

	/* Read BT on/off status from scoreboard[1], enable this only if BT patch support this feature */
	halbtc8821c1ant_read_score_board(btcoexist,	&u16tmp);

	bt_active = u16tmp & BIT(1);


#endif

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

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);

	if ((wifi_under_5g) || (bt_disabled))
		halbtc8821c1ant_low_penalty_ra(btcoexist, NORMAL_EXEC, FALSE);
	else
		halbtc8821c1ant_low_penalty_ra(btcoexist, NORMAL_EXEC, TRUE);


	if (coex_sta->bt_disabled != bt_disabled) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is from %s to %s!!\n",
			    (coex_sta->bt_disabled ? "disabled" : "enabled"),
			    (bt_disabled ? "disabled" : "enabled"));
		BTC_TRACE(trace_buf);
		coex_sta->bt_disabled = bt_disabled;
	}

}

void halbtc8821c1ant_enable_gnt_to_gpio(IN struct btc_coexist *btcoexist,
					boolean isenable)
{
#if BT_8821C_1ANT_COEX_DBG
	static u8			bitVal[5] = {0, 0, 0, 0, 0};
	static boolean		state = FALSE;
	/*
		if (state ==isenable)
			return;
		else
			state = isenable;
	*/
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

		/*  Restore original value */
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

#endif
}

u32 halbtc8821c1ant_ltecoex_indirect_read_reg(IN struct btc_coexist *btcoexist,
		IN u16 reg_addr)
{
	u32 j = 0, delay_count = 0;

	/* wait for ready bit before access 0x1700 */
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

	btcoexist->btc_write_4byte(btcoexist, 0x1700, 0x800F0000 | reg_addr);

	return btcoexist->btc_read_4byte(btcoexist,
					 0x1708);  /* get read data */
}

void halbtc8821c1ant_ltecoex_indirect_write_reg(IN struct btc_coexist
		*btcoexist,
		IN u16 reg_addr, IN u32 bit_mask, IN u32 reg_value)
{
	u32 val, i = 0, j = 0, bitpos = 0, delay_count = 0;

	if (bit_mask == 0x0)
		return;

	if (bit_mask == 0xffffffff) {
		/* wait for ready bit before access 0x1700/0x1704 */
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

		btcoexist->btc_write_4byte(btcoexist, 0x1704,
					   reg_value); /* put write data */

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
		val = halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist,
				reg_addr);
		val = (val & (~bit_mask)) | (reg_value << bitpos);

		/* wait for ready bit before access 0x1700/0x1704 */
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

		btcoexist->btc_write_4byte(btcoexist, 0x1704,
					   val); /* put write data */

		btcoexist->btc_write_4byte(btcoexist, 0x1700,
					   0xc00F0000 | reg_addr);

	}

}

void halbtc8821c1ant_ltecoex_enable(IN struct btc_coexist *btcoexist,
				    IN boolean enable)
{
	u8 val;

	val = (enable) ? 1 : 0;
	halbtc8821c1ant_ltecoex_indirect_write_reg(btcoexist, 0x38, 0x80,
			val);  /* 0x38[7] */

}

void halbtc8821c1ant_ltecoex_pathcontrol_owner(IN struct btc_coexist *btcoexist,
		IN boolean wifi_control)
{
	u8 val;

	val = (wifi_control) ? 1 : 0;
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x73, 0x4,
					   val); /* 0x70[26] */

}

void halbtc8821c1ant_ltecoex_set_gnt_bt(IN struct btc_coexist *btcoexist,
			IN u8 control_block, IN boolean sw_control, IN u8 state)
{
	u32 val = 0, val_orig = 0;

	if (!sw_control)
		val = 0x0;
	else if (state & 0x1)
		val = 0x3;
	else
		val = 0x1;

	val_orig = halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist,
			0x38);

	switch (control_block) {
	case BT_8821C_1ANT_GNT_BLOCK_RFC_BB:
	default:
		val = ((val << 14) | (val << 10)) | (val_orig & 0xffff33ff);
		break;
	case BT_8821C_1ANT_GNT_BLOCK_RFC:
		val = (val << 14) | (val_orig & 0xffff3fff);
		break;
	case BT_8821C_1ANT_GNT_BLOCK_BB:
		val = (val << 10) | (val_orig & 0xfffff3ff);
		break;
	}

	halbtc8821c1ant_ltecoex_indirect_write_reg(btcoexist,
			0x38, 0xffffffff, val);
}

void halbtc8821c1ant_ltecoex_set_gnt_wl(IN struct btc_coexist *btcoexist,
			IN u8 control_block, IN boolean sw_control, IN u8 state)
{
	u32 val = 0, val_orig = 0;

	if (!sw_control)
		val = 0x0;
	else if (state & 0x1)
		val = 0x3;
	else
		val = 0x1;

	val_orig = halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist,
			0x38);

	switch (control_block) {
	case BT_8821C_1ANT_GNT_BLOCK_RFC_BB:
	default:
		val = ((val << 12) | (val << 8)) | (val_orig & 0xffffccff);
		break;
	case BT_8821C_1ANT_GNT_BLOCK_RFC:
		val = (val << 12) | (val_orig & 0xffffcfff);
		break;
	case BT_8821C_1ANT_GNT_BLOCK_BB:
		val = (val << 8) | (val_orig & 0xfffffcff);
		break;
	}

	halbtc8821c1ant_ltecoex_indirect_write_reg(btcoexist,
			0x38, 0xffffffff, val);
}

void halbtc8821c1ant_ltecoex_set_coex_table(IN struct btc_coexist *btcoexist,
		IN u8 table_type, IN u16 table_content)
{
	u16 reg_addr = 0x0000;

	switch (table_type) {
	case BT_8821C_1ANT_CTT_WL_VS_LTE:
		reg_addr = 0xa0;
		break;
	case BT_8821C_1ANT_CTT_BT_VS_LTE:
		reg_addr = 0xa4;
		break;
	}

	if (reg_addr != 0x0000)
		halbtc8821c1ant_ltecoex_indirect_write_reg(btcoexist, reg_addr,
			0xffff, table_content); /* 0xa0[15:0] or 0xa4[15:0] */


}


void halbtc8821c1ant_ltecoex_set_break_table(IN struct btc_coexist *btcoexist,
		IN u8 table_type, IN u8 table_content)
{
	u16 reg_addr = 0x0000;

	switch (table_type) {
	case BT_8821C_1ANT_LBTT_WL_BREAK_LTE:
		reg_addr = 0xa8;
		break;
	case BT_8821C_1ANT_LBTT_BT_BREAK_LTE:
		reg_addr = 0xac;
		break;
	case BT_8821C_1ANT_LBTT_LTE_BREAK_WL:
		reg_addr = 0xb0;
		break;
	case BT_8821C_1ANT_LBTT_LTE_BREAK_BT:
		reg_addr = 0xb4;
		break;
	}

	if (reg_addr != 0x0000)
		halbtc8821c1ant_ltecoex_indirect_write_reg(btcoexist, reg_addr,
			0xff, table_content); /* 0xa8[15:0] or 0xb4[15:0] */


}

void halbtc8821c1ant_set_wltoggle_coex_table(IN struct btc_coexist *btcoexist,
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


void halbtc8821c1ant_set_coex_table(IN struct btc_coexist *btcoexist,
	    IN u32 val0x6c0, IN u32 val0x6c4, IN u32 val0x6c8, IN u8 val0x6cc)
{
	btcoexist->btc_write_4byte(btcoexist, 0x6c0, val0x6c0);

	btcoexist->btc_write_4byte(btcoexist, 0x6c4, val0x6c4);

	btcoexist->btc_write_4byte(btcoexist, 0x6c8, val0x6c8);

	btcoexist->btc_write_1byte(btcoexist, 0x6cc, val0x6cc);
}

void halbtc8821c1ant_coex_table(IN struct btc_coexist *btcoexist,
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

	halbtc8821c1ant_set_coex_table(btcoexist, val0x6c0, val0x6c4, val0x6c8,
				       val0x6cc);

	coex_dm->pre_val0x6c0 = coex_dm->cur_val0x6c0;
	coex_dm->pre_val0x6c4 = coex_dm->cur_val0x6c4;
	coex_dm->pre_val0x6c8 = coex_dm->cur_val0x6c8;
	coex_dm->pre_val0x6cc = coex_dm->cur_val0x6cc;
}

void halbtc8821c1ant_coex_table_with_type(IN struct btc_coexist *btcoexist,
		IN boolean force_exec, IN u8 type)
{
	u32	break_table;
	u8	select_table;

	coex_sta->coex_table_type = type;

	if (coex_sta->concurrent_rx_mode_on == TRUE) {
		 /* set WL hi-pri can break BT */
		break_table = 0xf0ffffff;
		 /* set Tx response = Hi-Pri (ex: Transmitting ACK,BA,CTS) */
		select_table = 0xb;
	} else {
		break_table = 0xffffff;
		select_table = 0x3;
	}

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], ********** Table-%d **********\n",
		    coex_sta->coex_table_type);
	BTC_TRACE(trace_buf);

	switch (type) {
	case 0:
		halbtc8821c1ant_coex_table(btcoexist, force_exec,
					   0x55555555, 0x55555555, break_table,
					   select_table);
		break;
	case 1:
		halbtc8821c1ant_coex_table(btcoexist, force_exec,
					   0xa5555555, 0xaa5a5a5a, break_table,
					   select_table);
		break;
	case 2:
		halbtc8821c1ant_coex_table(btcoexist, force_exec,
					   0xaa5a5a5a, 0xaa5a5a5a, break_table,
					   select_table);
		break;
	case 3:
		halbtc8821c1ant_coex_table(btcoexist, force_exec,
					   0xa5555555, 0x5a5a5a5a, break_table,
					   select_table);
		break;
	case 4:
		halbtc8821c1ant_coex_table(btcoexist, force_exec,
					   0xa5555555, 0xaa5a5a5a, break_table,
					   select_table);
		break;
	case 5:
		halbtc8821c1ant_coex_table(btcoexist, force_exec,
					   0x5a5a5a5a, 0x5a5a5a5a, break_table,
					   select_table);
		break;
	case 6:
		halbtc8821c1ant_coex_table(btcoexist, force_exec,
					   0xa5555555, 0xaa5a5a5a, break_table,
					   select_table);
		break;
	case 7:
		halbtc8821c1ant_coex_table(btcoexist, force_exec,
					   0xaa555555, 0xaa555555, break_table,
					   select_table);
		break;
	case 8:
		halbtc8821c1ant_coex_table(btcoexist, force_exec,
					   0xa5555555, 0xaaaa5aaa, break_table,
					   select_table);
		break;
	case 9:
		halbtc8821c1ant_coex_table(btcoexist, force_exec,
					   0x5a5a5a5a, 0xaaaa5aaa, break_table,
					   select_table);
		break;
	case 10:
		halbtc8821c1ant_coex_table(btcoexist, force_exec,
					   0xaaaaaaaa, 0xaaaaaaaa, break_table,
					   select_table);
		break;
	case 11:
		halbtc8821c1ant_coex_table(btcoexist, force_exec,
					   0xa5a55555, 0xaaaa5a5a, break_table,
					   select_table);
		break;
	case 12:
		halbtc8821c1ant_coex_table(btcoexist, force_exec,
					   0xa5555555, 0xaaaa5a5a, break_table,
					   select_table);
		break;
	case 13:
		halbtc8821c1ant_coex_table(btcoexist, force_exec,
					   0xaa5555aa, 0xaa5a5a5a, break_table,
					   select_table);
		break;
	case 14:
		halbtc8821c1ant_coex_table(btcoexist, force_exec,
					   0xaa5555aa, 0x5a5a5a5a, break_table,
					   select_table);
		break;
	default:
		break;
	}
}

void halbtc8821c1ant_set_fw_ignore_wlan_act(IN struct btc_coexist *btcoexist,
		IN boolean enable)
{
	u8			h2c_parameter[1] = {0};

	if (enable)
		h2c_parameter[0] |= BIT(0);		/* function enable */

	btcoexist->btc_fill_h2c(btcoexist, 0x63, 1, h2c_parameter);
}

void halbtc8821c1ant_ignore_wlan_act(IN struct btc_coexist *btcoexist,
				     IN boolean force_exec, IN boolean enable)
{
	coex_dm->cur_ignore_wlan_act = enable;

	if (!force_exec) {
		if (coex_dm->pre_ignore_wlan_act ==
		    coex_dm->cur_ignore_wlan_act)
			return;
	}
	halbtc8821c1ant_set_fw_ignore_wlan_act(btcoexist, enable);

	coex_dm->pre_ignore_wlan_act = coex_dm->cur_ignore_wlan_act;
}

void halbtc8821c1ant_set_lps_rpwm(IN struct btc_coexist *btcoexist,
				  IN u8 lps_val, IN u8 rpwm_val)
{
	u8	lps = lps_val;
	u8	rpwm = rpwm_val;

	btcoexist->btc_set(btcoexist, BTC_SET_U1_LPS_VAL, &lps);
	btcoexist->btc_set(btcoexist, BTC_SET_U1_RPWM_VAL, &rpwm);
}

void halbtc8821c1ant_lps_rpwm(IN struct btc_coexist *btcoexist,
		      IN boolean force_exec, IN u8 lps_val, IN u8 rpwm_val)
{
	coex_dm->cur_lps = lps_val;
	coex_dm->cur_rpwm = rpwm_val;

	if (!force_exec) {
		if ((coex_dm->pre_lps == coex_dm->cur_lps) &&
		    (coex_dm->pre_rpwm == coex_dm->cur_rpwm))
			return;
	}
	halbtc8821c1ant_set_lps_rpwm(btcoexist, lps_val, rpwm_val);

	coex_dm->pre_lps = coex_dm->cur_lps;
	coex_dm->pre_rpwm = coex_dm->cur_rpwm;
}

void halbtc8821c1ant_ps_tdma_check_for_power_save_state(
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

boolean halbtc8821c1ant_power_save_state(IN struct btc_coexist *btcoexist,
			      IN u8 ps_type, IN u8 lps_val, IN u8 rpwm_val)
{
	boolean		low_pwr_disable = FALSE, result = TRUE;

	switch (ps_type) {
	case BTC_PS_WIFI_NATIVE:
		/* recover to original 32k low power setting */
		coex_sta->force_lps_ctrl = FALSE;
		low_pwr_disable = FALSE;
		/* btcoexist->btc_set(btcoexist,
				   BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable); */
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_PRE_NORMAL_LPS,
				   NULL);

		break;
	case BTC_PS_LPS_ON:
		coex_sta->force_lps_ctrl = TRUE;
		halbtc8821c1ant_ps_tdma_check_for_power_save_state(
			btcoexist, TRUE);
		halbtc8821c1ant_lps_rpwm(btcoexist, NORMAL_EXEC,
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
		halbtc8821c1ant_ps_tdma_check_for_power_save_state(
			btcoexist, FALSE);
		result = btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS,
				   NULL);

		break;
	default:
		break;
	}

	return result;
}


void halbtc8821c1ant_set_fw_pstdma(IN struct btc_coexist *btcoexist,
	   IN u8 byte1, IN u8 byte2, IN u8 byte3, IN u8 byte4, IN u8 byte5)
{
	u8			h2c_parameter[5] = {0};
	u8			real_byte1 = byte1, real_byte5 = byte5;
	boolean			ap_enable = FALSE, result = FALSE;
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
			    "[BTCoex], halbtc8821c1ant_set_fw_pstdma == FW for 1Ant AP mode\n");
		BTC_TRACE(trace_buf);
		real_byte1 &= ~BIT(4);
		real_byte1 |= BIT(5);

		real_byte5 |= BIT(5);
		real_byte5 &= ~BIT(6);

		ps_type = BTC_PS_WIFI_NATIVE;
		halbtc8821c1ant_power_save_state(btcoexist,
					 ps_type, 0x0,
						 0x0);
	} else if (byte1 & BIT(4) && !(byte1 & BIT(5))) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], halbtc8821c1ant_set_fw_pstdma == Force LPS Leave (byte1 = 0x%x)\n", byte1);
		BTC_TRACE(trace_buf);

		ps_type = BTC_PS_LPS_OFF;
		if (!halbtc8821c1ant_power_save_state(btcoexist, ps_type, 0x50, 0x4))
			result = TRUE;

	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], halbtc8821c1ant_set_fw_pstdma == native power save (byte1 = 0x%x)\n", byte1);
		BTC_TRACE(trace_buf);

		ps_type = BTC_PS_WIFI_NATIVE;
		halbtc8821c1ant_power_save_state(btcoexist, ps_type,
						 0x0,
						 0x0);
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

		btcoexist->btc_fill_h2c(btcoexist, 0x60, 5, h2c_parameter);

	} else {
		coex_sta->cnt_set_ps_state_fail++;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], halbtc8821c1ant_set_fw_pstdma == Force Leave LPS Fail (cnt = %d)\n",
			    coex_sta->cnt_set_ps_state_fail);
		BTC_TRACE(trace_buf);
	}

	if (ps_type == BTC_PS_WIFI_NATIVE)
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_POST_NORMAL_LPS, NULL);
}


void halbtc8821c1ant_ps_tdma(IN struct btc_coexist *btcoexist,
		     IN boolean force_exec, IN boolean turn_on, IN u8 type)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	struct  btc_board_info	*board_info = &btcoexist->board_info;
	boolean			wifi_busy = FALSE;
	static u8			psTdmaByte4Modify = 0x0, pre_psTdmaByte4Modify = 0x0;
	static boolean	 pre_wifi_busy = FALSE;


	coex_dm->cur_ps_tdma_on = turn_on;
	coex_dm->cur_ps_tdma = type;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	if (wifi_busy != pre_wifi_busy) {
		force_exec = TRUE;
		pre_wifi_busy = wifi_busy;
	}

	/* 0x778 = 0x1 at wifi slot (no blocking BT Low-Pri pkts) */
	if ((bt_link_info->slave_role) && (bt_link_info->a2dp_exist))
		psTdmaByte4Modify = 0x1;
	else
		psTdmaByte4Modify = 0x0;

	if (pre_psTdmaByte4Modify != psTdmaByte4Modify) {
		force_exec = TRUE;
		pre_psTdmaByte4Modify = psTdmaByte4Modify;
	}

	if (!force_exec) {
		if ((coex_dm->pre_ps_tdma_on == coex_dm->cur_ps_tdma_on) &&
		    (coex_dm->pre_ps_tdma == coex_dm->cur_ps_tdma)) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Skip TDMA because no change TDMA(%s, %d)\n",
				    (coex_dm->cur_ps_tdma_on ? "on" : "off"),
				    coex_dm->cur_ps_tdma);
			BTC_TRACE(trace_buf);
			return;
		}
	}

	if (coex_dm->cur_ps_tdma_on) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], ********** TDMA(on, %d) **********\n",
			    coex_dm->cur_ps_tdma);
		BTC_TRACE(trace_buf);

		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x550, 0x8,
					   0x1);  /* enable TBTT nterrupt */
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], ********** TDMA(off, %d) **********\n",
			    coex_dm->cur_ps_tdma);
		BTC_TRACE(trace_buf);
	}


	if (turn_on) {
		switch (type) {
		default:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x35, 0x03, 0x11, 0x11);
			break;
		case 3:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x30, 0x03, 0x10, 0x50);
			break;
		case 4:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x21, 0x03, 0x10, 0x50);
			break;
		case 5:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x3a, 0x03, 0x11, 0x11);
			break;
		case 6:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x20, 0x03, 0x11, 0x10);
			break;
		case 7:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x10, 0x03, 0x10,  0x54 |
						      psTdmaByte4Modify);
			break;
		case 8:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x10, 0x03, 0x10,  0x54 |
						      psTdmaByte4Modify);
			break;
		case 9:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x10, 0x03, 0x10,  0x54 |
						      psTdmaByte4Modify);
			break;
		case 10:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x30, 0x03, 0x11, 0x10);
			break;
		case 11:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x25, 0x03, 0x11, 0x11);
			break;
		case 12:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x35, 0x03, 0x10,  0x50 |
						      psTdmaByte4Modify);
			break;
		case 13:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x10, 0x07, 0x10,  0x54 |
						      psTdmaByte4Modify);
			break;
		case 14:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x15, 0x03, 0x10,  0x50 |
						      psTdmaByte4Modify);
			break;
		case 15:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x20, 0x03, 0x10,  0x50 |
						      psTdmaByte4Modify);
			break;
		case 16:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x10, 0x03, 0x11,  0x15 |
						      psTdmaByte4Modify);
			break;
		case 17:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x10, 0x03, 0x11, 0x14 |
							psTdmaByte4Modify);
			break;
		case 18:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x30, 0x03, 0x10,  0x50 |
						      psTdmaByte4Modify);
			break;
		case 19:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x15, 0x03, 0x11, 0x10);
			break;
		case 20:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x30, 0x03, 0x11, 0x10);
			break;
		case 21:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x30, 0x03, 0x11, 0x10);
			break;
		case 22:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x25, 0x03, 0x11, 0x10);
			break;
		case 23:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x10, 0x03, 0x11, 0x10);
			break;
		case 24:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x08, 0x03, 0x10,  0x54 |
						      psTdmaByte4Modify);
			break;
		case 27:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x10, 0x03, 0x11, 0x15);
			break;
		case 28:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x10, 0x0b, 0x10,  0x54);
			break;
		case 32:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x35, 0x03, 0x11, 0x11);
			break;
		case 33:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x35, 0x03, 0x11, 0x10);
			break;
		case 57:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x10, 0x03, 0x10,  0x50 |
						      psTdmaByte4Modify);
			break;
		case 58:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x51, 0x10, 0x03, 0x10,  0x50 |
						      psTdmaByte4Modify);
			break;
		case 67:
			halbtc8821c1ant_set_fw_pstdma(btcoexist,
					      0x61, 0x10, 0x03, 0x11,  0x10 |
						      psTdmaByte4Modify);
			break;
		}
	} else {

		/* disable PS tdma */
		switch (type) {
		case 8: /* PTA Control */
			halbtc8821c1ant_set_fw_pstdma(btcoexist, 0x8,
						      0x0, 0x0, 0x0, 0x0);
			break;
		case 0:
		default:  /* Software control, Antenna at BT side */
			halbtc8821c1ant_set_fw_pstdma(btcoexist, 0x0,
						      0x0, 0x0, 0x0, 0x0);
			break;
		case 1: /* 2-Ant, 0x778=3, antenna control by antenna diversity */
			halbtc8821c1ant_set_fw_pstdma(btcoexist, 0x0,
						      0x0, 0x0, 0x48, 0x0);
			break;
		}
	}

	if (!coex_sta->is_set_ps_state_fail) {
		/* update pre state */
		coex_dm->pre_ps_tdma_on = coex_dm->cur_ps_tdma_on;
		coex_dm->pre_ps_tdma = coex_dm->cur_ps_tdma;
	}
}

void halbtc8821c1ant_set_int_block(IN struct btc_coexist *btcoexist,
				   IN boolean force_exec,  IN u8 pos_type)
{
#if 0
	u8		regval_0xcba;
	u32		u32tmp1 = 0;

	coex_dm->cur_int_block_status = pos_type;

	if (!force_exec) {
		if (coex_dm->pre_int_block_status ==
		    coex_dm->cur_int_block_status)
			return;
	}

	coex_dm->pre_int_block_status = coex_dm->cur_int_block_status;

	regval_0xcba =  btcoexist->btc_read_1byte(btcoexist, 0xcba);

	switch (pos_type) {

	case BT_8821C_1ANT_INT_BLOCK_SWITCH_TO_WLG_OF_BTG:
		regval_0xcba = (regval_0xcba | BIT(0)) & (~(BIT(
			2)));				/* 0xcb8[16] = 1, 0xcb8[18] = 0, WL_G select BTG */
		regval_0xcba =   regval_0xcba & 0x0f;

		/*btcoexist->btc_write_1byte_bitmask(btcoexist, 0xc1d, 0x0f, 0x5); */ /* Gain Table */
		/*btcoexist->btc_write_1byte_bitmask(btcoexist, 0xa9e, 0x0f, 0x2); */ /* CCK Gain Table */

		break;
	case BT_8821C_1ANT_INT_BLOCK_SWITCH_TO_WLG_OF_WLAG:
		regval_0xcba =   regval_0xcba & (~(BIT(2) | BIT(
			0)));			/* 0xcb8[16] = 0, 0xcb8[18] = 0, WL_G select WLAG */

		/* regval_0xcba =   regval_0xcba | BIT(4) | BIT(5) ; */			/* 0xcb8[21:20] = 2b'11, WL_G @ WLAG on */
		/* regval_0xcba =    (regval_0xcba | BIT(6)) & (~(BIT(7)) ) ; */		/* 0xcb8[23:22] = 2b'01, WL_A @ WLAG off */
		/*btcoexist->btc_write_1byte_bitmask(btcoexist, 0xc1d, 0x0f, 0x0); */ /* Gain Table */
		/*btcoexist->btc_write_1byte_bitmask(btcoexist, 0xa9e, 0x0f, 0x6); */ /* CCK Gain Table */

		break;
	case BT_8821C_1ANT_INT_BLOCK_SWITCH_TO_WLA_OF_WLAG:
		regval_0xcba =   regval_0xcba & (~(BIT(2) | BIT(
			0)));			/* 0xcb8[16] = 0, 0xcb8[18] = 0, WL_G select WLAG */
		/*regval_0xcba = (regval_0xcba | BIT(4)) & (~(BIT(5))); */			/* 0xcb8[21:20] = 2b'01, WL_G @ WLAG off */
		/*regval_0xcba = regval_0xcba | BIT(6) | BIT(7); */				/* 0xcb8[23:22] = 2b'11, WL_A @ WLAG on */

		break;
	}

	btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcba, 0xff,
					   regval_0xcba);

	u32tmp1 = btcoexist->btc_read_4byte(btcoexist, 0xcb8);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], ********** (After Int Block setup) 0xcb8 = 0x%08x **********\n",
		    u32tmp1);
	BTC_TRACE(trace_buf);

#endif
}

void halbtc8821c1ant_set_ext_band_switch(IN struct btc_coexist *btcoexist,
		IN boolean force_exec, IN u8 pos_type)
{

#if 0
	boolean	switch_polatiry_inverse = FALSE;
	u8		regval_0xcb6;
	u32		u32tmp1 = 0, u32tmp2 = 0;

	if (!rfe_type->ext_band_switch_exist)
		return;

	coex_dm->cur_ext_band_switch_status = pos_type;

	if (!force_exec) {
		if (coex_dm->pre_ext_band_switch_status ==
		    coex_dm->cur_ext_band_switch_status)
			return;
	}

	coex_dm->pre_ext_band_switch_status =
		coex_dm->cur_ext_band_switch_status;

	/* swap control polarity if use different switch control polarity*/
	switch_polatiry_inverse = (rfe_type->ext_band_switch_ctrl_polarity == 1
		   ? ~switch_polatiry_inverse : switch_polatiry_inverse);

	/*swap control polarity for WL_A, default polarity 0xcb4[21] = 0 && 0xcb4[23] = 1 is for WL_G */
	switch_polatiry_inverse = (pos_type ==
		BT_8821C_1ANT_EXT_BAND_SWITCH_TO_WLA ? ~switch_polatiry_inverse
				   : switch_polatiry_inverse);

	regval_0xcb6 =  btcoexist->btc_read_1byte(btcoexist, 0xcb6);

	/* for normal switch polrity, 0xcb4[21] =1 && 0xcb4[23] = 0 for WL_A, vice versa */
	regval_0xcb6 = (switch_polatiry_inverse == 1 ? ((regval_0xcb6 & (~(BIT(
		7)))) | BIT(5)) : ((regval_0xcb6 & (~(BIT(5)))) | BIT(7)));

	btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcb6, 0xff,
					   regval_0xcb6);

	u32tmp1 = btcoexist->btc_read_4byte(btcoexist, 0xcb0);
	u32tmp2 = btcoexist->btc_read_4byte(btcoexist, 0xcb4);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], ********** (After Ext Band switch setup) 0xcb0 = 0x%08x, 0xcb4 = 0x%08x**********\n",
		    u32tmp1, u32tmp2);
	BTC_TRACE(trace_buf);
#endif

}

void halbtc8821c1ant_set_ext_ant_switch(IN struct btc_coexist *btcoexist,
			IN boolean force_exec, IN u8 ctrl_type, IN u8 pos_type)
{
	struct  btc_board_info	*board_info = &btcoexist->board_info;
	boolean	switch_polatiry_inverse = FALSE;
	u8		regval_0xcb7 = 0, regval_0x64;
	u32		u32tmp1 = 0, u32tmp2 = 0, u32tmp3 = 0;

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
	/*  Normal switch polarity for DPDT, 0xcb4[29:28] = 2b'01 => BTG to Main, WLG to Aux,  0xcb4[29:28] = 2b'10 => BTG to Aux, WLG to Main */
	/*  Normal switch polarity for SPDT, 0xcb4[29:28] = 2b'01 => Ant to BTG,  0xcb4[29:28] = 2b'10 => Ant to WLG */
	if (rfe_type->ext_ant_switch_ctrl_polarity)
		switch_polatiry_inverse =  ~switch_polatiry_inverse;

	/* swap control polarity if 1-Ant at Aux */
	if (rfe_type->ant_at_main_port == FALSE)
		switch_polatiry_inverse =  ~switch_polatiry_inverse;

	switch (pos_type) {
	default:
	case BT_8821C_1ANT_EXT_ANT_SWITCH_TO_BT:
	case BT_8821C_1ANT_EXT_ANT_SWITCH_TO_NOCARE:
	case BT_8821C_1ANT_EXT_ANT_SWITCH_TO_WLA:

		break;
	case BT_8821C_1ANT_EXT_ANT_SWITCH_TO_WLG:
		if (!rfe_type->wlg_Locate_at_btg)
			switch_polatiry_inverse =  ~switch_polatiry_inverse;
		break;
	}

	if (board_info->ant_div_cfg)
		ctrl_type = BT_8821C_1ANT_EXT_ANT_SWITCH_CTRL_BY_ANTDIV;


	switch (ctrl_type) {
	default:
	case BT_8821C_1ANT_EXT_ANT_SWITCH_CTRL_BY_BBSW:
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4e,
					   0x80, 0x0);  /*  0x4c[23] = 0 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4f,
					   0x01, 0x1);  /* 0x4c[24] = 1 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcb4,
			0xff, 0x77);	/* BB SW, DPDT use RFE_ctrl8 and RFE_ctrl9 as control pin */

		regval_0xcb7 = (switch_polatiry_inverse == FALSE ?
			0x1 : 0x2);     /* 0xcb4[29:28] = 2b'01 for no switch_polatiry_inverse, DPDT_SEL_N =1, DPDT_SEL_P =0 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcb7,
						   0x30, regval_0xcb7);

		break;
	case BT_8821C_1ANT_EXT_ANT_SWITCH_CTRL_BY_PTA:
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4e,
					   0x80, 0x0);  /* 0x4c[23] = 0 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4f,
					   0x01, 0x1);  /* 0x4c[24] = 1 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcb4,
			0xff, 0x66);	/* PTA,  DPDT use RFE_ctrl8 and RFE_ctrl9 as control pin */

		regval_0xcb7 = (switch_polatiry_inverse == FALSE ?
			0x2 : 0x1);     /* 0xcb4[29:28] = 2b'10 for no switch_polatiry_inverse, DPDT_SEL_N =1, DPDT_SEL_P =0  @ GNT_BT=1 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcb7,
						   0x30, regval_0xcb7);

		break;
	case BT_8821C_1ANT_EXT_ANT_SWITCH_CTRL_BY_ANTDIV:
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4e,
					   0x80, 0x0);  /* 0x4c[23] = 0 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4f,
					   0x01, 0x1);  /* 0x4c[24] = 1 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcb4,
						   0xff, 0x88);  /* */

		/* no regval_0xcb7 setup required, because  antenna switch control value by antenna diversity */

		break;
	case BT_8821C_1ANT_EXT_ANT_SWITCH_CTRL_BY_MAC:
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4e,
					   0x80, 0x1);  /*  0x4c[23] = 1 */

		regval_0x64 = (switch_polatiry_inverse == FALSE ?  0x0 :
			0x1);     /* 0x64[0] = 1b'0 for no switch_polatiry_inverse, DPDT_SEL_N =1, DPDT_SEL_P =0 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x64, 0x1,
						   regval_0x64);
		break;
	case BT_8821C_1ANT_EXT_ANT_SWITCH_CTRL_BY_BT:
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4e,
					   0x80, 0x0);  /* 0x4c[23] = 0 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4f,
					   0x01, 0x0);  /* 0x4c[24] = 0 */

		/* no  setup required, because  antenna switch control value by BT vendor 0xac[1:0] */
		break;
	}

	/* PAPE, LNA_ON control by BT  while WLAN off for current leakage issue */
	if (ctrl_type == BT_8821C_1ANT_EXT_ANT_SWITCH_CTRL_BY_BT) {
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x20,
					   0x0);  /* PAPE   0x64[29] = 0 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x10,
					   0x0);  /* LNA_ON 0x64[28] = 0 */
	} else {
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x20,
					   0x1);  /* PAPE   0x64[29] = 1 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x10,
					   0x1);  /* LNA_ON 0x64[28] = 1 */
	}

#if BT_8821C_1ANT_COEX_DBG
	u32tmp1 = btcoexist->btc_read_4byte(btcoexist, 0xcb4);
	u32tmp2 = btcoexist->btc_read_4byte(btcoexist, 0x4c);
	u32tmp3 = btcoexist->btc_read_4byte(btcoexist, 0x64) & 0xff;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], (After Ext Ant switch setup) 0xcb4 = 0x%08x, 0x4c = 0x%08x, 0x64= 0x%02x\n",
		    u32tmp1, u32tmp2, u32tmp3);
	BTC_TRACE(trace_buf);
#endif

}

void halbtc8821c1ant_set_rfe_type(IN struct btc_coexist *btcoexist)
{
	struct  btc_board_info *board_info = &btcoexist->board_info;


	/* the following setup should be got from Efuse in the future */
	rfe_type->rfe_module_type = board_info->rfe_type & 0x1f;

	rfe_type->ext_ant_switch_ctrl_polarity = 0;

	switch (rfe_type->rfe_module_type) {
	case 0:
	case 8:
	default:
		rfe_type->ext_ant_switch_exist = TRUE;
		rfe_type->ext_ant_switch_type =
			BT_8821C_1ANT_EXT_ANT_SWITCH_USE_DPDT;    /*2-Ant, DPDT, WLG*/
		rfe_type->wlg_Locate_at_btg = FALSE;
		rfe_type->ant_at_main_port = TRUE;
		break;
	case 1:
	case 9:
		rfe_type->ext_ant_switch_exist = TRUE;
		rfe_type->ext_ant_switch_type =
			BT_8821C_1ANT_EXT_ANT_SWITCH_USE_SPDT;     /*1-Ant, Main, WLG */
		rfe_type->wlg_Locate_at_btg = FALSE;
		rfe_type->ant_at_main_port = TRUE;
		break;
	case 2:
	case 10:
		rfe_type->ext_ant_switch_exist = TRUE;
		rfe_type->ext_ant_switch_type =
			BT_8821C_1ANT_EXT_ANT_SWITCH_USE_SPDT;     /*1-Ant, Main, BTG */
		rfe_type->wlg_Locate_at_btg = TRUE;
		rfe_type->ant_at_main_port = TRUE;
		break;
	case 3:
	case 11:
		rfe_type->ext_ant_switch_exist = TRUE;
		rfe_type->ext_ant_switch_type =
			BT_8821C_1ANT_EXT_ANT_SWITCH_USE_DPDT;    /*1-Ant, Aux, WLG */
		rfe_type->wlg_Locate_at_btg = FALSE;
		rfe_type->ant_at_main_port = FALSE;
		break;
	case 4:
	case 12:
		rfe_type->ext_ant_switch_exist = TRUE;
		rfe_type->ext_ant_switch_type =
			BT_8821C_1ANT_EXT_ANT_SWITCH_USE_DPDT;    /*1-Ant, Aux, BTG */
		rfe_type->wlg_Locate_at_btg = TRUE;
		rfe_type->ant_at_main_port = FALSE;
		break;
	case 5:
	case 13:
		rfe_type->ext_ant_switch_exist = FALSE;		/*2-Ant, no switch, WLG*/
		rfe_type->ext_ant_switch_type =
			BT_8821C_1ANT_EXT_ANT_SWITCH_NONE;
		rfe_type->wlg_Locate_at_btg = FALSE;
		rfe_type->ant_at_main_port = TRUE;
		break;
	case 6:
	case 14:
		rfe_type->ext_ant_switch_exist = FALSE;		/*2-Ant, no antenna switch, WLG*/
		rfe_type->ext_ant_switch_type =
			BT_8821C_1ANT_EXT_ANT_SWITCH_NONE;
		rfe_type->wlg_Locate_at_btg = FALSE;
		rfe_type->ant_at_main_port = TRUE;
		break;
	case 7:
	case 15:
		rfe_type->ext_ant_switch_exist = TRUE;	/*2-Ant, DPDT, BTG*/
		rfe_type->ext_ant_switch_type =
			BT_8821C_1ANT_EXT_ANT_SWITCH_USE_DPDT;
		rfe_type->wlg_Locate_at_btg = TRUE;
		rfe_type->ant_at_main_port = TRUE;
		break;
	}

#if 0
	if (rfe_type->wlg_Locate_at_btg)
		halbtc8821c1ant_set_int_block(btcoexist, FORCE_EXEC,
			      BT_8821C_1ANT_INT_BLOCK_SWITCH_TO_WLG_OF_BTG);
	else
		halbtc8821c1ant_set_int_block(btcoexist, FORCE_EXEC,
			      BT_8821C_1ANT_INT_BLOCK_SWITCH_TO_WLG_OF_WLAG);
#endif

}


void halbtc8821c1ant_set_ant_path(IN struct btc_coexist *btcoexist,
				  IN u8 ant_pos_type, IN boolean force_exec,
				  IN u8 phase)
{
	struct  btc_board_info *board_info = &btcoexist->board_info;
	u32			cnt_bt_cal_chk = 0;
	boolean			is_in_mp_mode = FALSE;
	u8			u8tmp = 0;
	u32			u32tmp1 = 0, u32tmp2 = 0, u32tmp3 = 0;
	u16			u16tmp1 = 0;


	u32tmp1 = halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist,
			0x38);

	/* To avoid indirect access fail  */
	if (((u32tmp1 & 0xf000) >> 12) != ((u32tmp1 & 0x0f00) >> 8)) {
		force_exec = TRUE;
		coex_sta->gnt_error_cnt++;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex],(Before Ant Setup) 0x38= 0x%x\n",
		    u32tmp1);
		BTC_TRACE(trace_buf);
	}


#if BT_8821C_1ANT_COEX_DBG

	u32tmp2 = halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist,
			0x54);
	u8tmp  = btcoexist->btc_read_1byte(btcoexist, 0x73);

	u32tmp3 = btcoexist->btc_read_4byte(btcoexist, 0xcb4);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex],(Before Ant Setup) 0xcb4 = 0x%x, 0x73 = 0x%x, 0x38= 0x%x, 0x54= 0x%x\n",
		    u32tmp3, u8tmp, u32tmp1, u32tmp2);
	BTC_TRACE(trace_buf);
#endif

	coex_dm->cur_ant_pos_type = (ant_pos_type << 8)  + phase;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex],(Before Ant Setup) pre_ant_pos_type = 0x%x, cur_ant_pos_type = 0x%x\n",
		    coex_dm->pre_ant_pos_type,
		    coex_dm->cur_ant_pos_type);
	BTC_TRACE(trace_buf);


	if (!force_exec) {
		if (coex_dm->cur_ant_pos_type == coex_dm->pre_ant_pos_type)
			return;
	}

	coex_dm->pre_ant_pos_type = coex_dm->cur_ant_pos_type;


	switch (phase) {
	case BT_8821C_1ANT_PHASE_COEX_POWERON:

		/* set Path control owner to BT at power-on step */
		halbtc8821c1ant_ltecoex_pathcontrol_owner(btcoexist,
				BT_8821C_1ANT_PCO_BTSIDE);

		/* set GNT_BT to SW high */
		halbtc8821c1ant_ltecoex_set_gnt_bt(btcoexist,
					   BT_8821C_1ANT_GNT_BLOCK_RFC_BB,
					   BT_8821C_1ANT_GNT_TYPE_CTRL_BY_SW,
					   BT_8821C_1ANT_SIG_STA_SET_TO_HIGH);
		/* Set GNT_WL to SW high */
		halbtc8821c1ant_ltecoex_set_gnt_wl(btcoexist,
					   BT_8821C_1ANT_GNT_BLOCK_RFC_BB,
					   BT_8821C_1ANT_GNT_TYPE_CTRL_BY_SW,
					   BT_8821C_1ANT_SIG_STA_SET_TO_HIGH);

		if (BTC_ANT_PATH_AUTO == ant_pos_type)
			ant_pos_type = BTC_ANT_PATH_BT;

		coex_sta->run_time_state = FALSE;

		break;
	case BT_8821C_1ANT_PHASE_COEX_INIT:
		/* Disable LTE Coex Function in WiFi side (this should be on if LTE coex is required) */
		halbtc8821c1ant_ltecoex_enable(btcoexist, 0x0);

		/* GNT_WL_LTE always = 1 (this should be config if LTE coex is required) */
		halbtc8821c1ant_ltecoex_set_coex_table(
			btcoexist,
			BT_8821C_1ANT_CTT_WL_VS_LTE,
			0xffff);

		/* GNT_BT_LTE always = 1 (this should be config if LTE coex is required) */
		halbtc8821c1ant_ltecoex_set_coex_table(
			btcoexist,
			BT_8821C_1ANT_CTT_BT_VS_LTE,
			0xffff);

		/* Wait If BT IQK running, because Path control owner is at BT during BT IQK (setup by WiFi firmware) */
		while (cnt_bt_cal_chk <= 20) {
			u8tmp = btcoexist->btc_read_1byte(
					btcoexist,
					0x49c);
			cnt_bt_cal_chk++;
			if (u8tmp & BIT(1)) {
				BTC_SPRINTF(
					trace_buf,
					BT_TMP_BUF_SIZE,
					"[BTCoex], ########### BT is calibrating (wait cnt=%d) ###########\n",
					cnt_bt_cal_chk);
				BTC_TRACE(
					trace_buf);
				delay_ms(50);
			} else {
				BTC_SPRINTF(
					trace_buf,
					BT_TMP_BUF_SIZE,
					"[BTCoex], ********** BT is NOT calibrating (wait cnt=%d)**********\n",
					cnt_bt_cal_chk);
				BTC_TRACE(
					trace_buf);
				break;
			}
		}

		/* set Path control owner to WL at initial step */
		halbtc8821c1ant_ltecoex_pathcontrol_owner(
			btcoexist,
			BT_8821C_1ANT_PCO_WLSIDE);

		/* set GNT_BT to SW high */
		halbtc8821c1ant_ltecoex_set_gnt_bt(btcoexist,
					   BT_8821C_1ANT_GNT_BLOCK_RFC_BB,
					   BT_8821C_1ANT_GNT_TYPE_CTRL_BY_SW,
					   BT_8821C_1ANT_SIG_STA_SET_TO_HIGH);
		/* Set GNT_WL to SW low */
		halbtc8821c1ant_ltecoex_set_gnt_wl(btcoexist,
					   BT_8821C_1ANT_GNT_BLOCK_RFC_BB,
					   BT_8821C_1ANT_GNT_TYPE_CTRL_BY_SW,
					   BT_8821C_1ANT_SIG_STA_SET_TO_LOW);

		coex_sta->run_time_state = FALSE;

		if (BTC_ANT_PATH_AUTO == ant_pos_type)
			ant_pos_type = BTC_ANT_PATH_BT;

		break;
	case BT_8821C_1ANT_PHASE_WLANONLY_INIT:
		/* Disable LTE Coex Function in WiFi side (this should be on if LTE coex is required) */
		halbtc8821c1ant_ltecoex_enable(btcoexist, 0x0);

		/* GNT_WL_LTE always = 1 (this should be config if LTE coex is required) */
		halbtc8821c1ant_ltecoex_set_coex_table(
			btcoexist,
			BT_8821C_1ANT_CTT_WL_VS_LTE,
			0xffff);

		/* GNT_BT_LTE always = 1 (this should be config if LTE coex is required) */
		halbtc8821c1ant_ltecoex_set_coex_table(
			btcoexist,
			BT_8821C_1ANT_CTT_BT_VS_LTE,
			0xffff);

		/* set Path control owner to WL at initial step */
		halbtc8821c1ant_ltecoex_pathcontrol_owner(
			btcoexist,
			BT_8821C_1ANT_PCO_WLSIDE);

		/* set GNT_BT to SW Low */
		halbtc8821c1ant_ltecoex_set_gnt_bt(btcoexist,
						BT_8821C_1ANT_GNT_BLOCK_RFC_BB,
						BT_8821C_1ANT_GNT_TYPE_CTRL_BY_SW,
						BT_8821C_1ANT_SIG_STA_SET_TO_LOW);
		/* Set GNT_WL to SW high */
		halbtc8821c1ant_ltecoex_set_gnt_wl(btcoexist,
						BT_8821C_1ANT_GNT_BLOCK_RFC_BB,
						BT_8821C_1ANT_GNT_TYPE_CTRL_BY_SW,
						BT_8821C_1ANT_SIG_STA_SET_TO_HIGH);

		coex_sta->run_time_state = FALSE;

		if (BTC_ANT_PATH_AUTO == ant_pos_type)
			ant_pos_type = BTC_ANT_PATH_WIFI;

		break;
	case BT_8821C_1ANT_PHASE_WLAN_OFF:
		/* Disable LTE Coex Function in WiFi side */
		halbtc8821c1ant_ltecoex_enable(btcoexist, 0x0);

		/* set Path control owner to BT */
		halbtc8821c1ant_ltecoex_pathcontrol_owner(
			btcoexist,
			BT_8821C_1ANT_PCO_BTSIDE);

		/* Set Ext Ant Switch to BT control at wifi off step */
		halbtc8821c1ant_set_ext_ant_switch(btcoexist,
						FORCE_EXEC,
						BT_8821C_1ANT_EXT_ANT_SWITCH_CTRL_BY_BT,
						BT_8821C_1ANT_EXT_ANT_SWITCH_TO_NOCARE);

		halbtc8821c1ant_ignore_wlan_act(btcoexist,
							FORCE_EXEC, TRUE);

		coex_sta->run_time_state = FALSE;
		break;
	case BT_8821C_1ANT_PHASE_2G_RUNTIME:

		while (cnt_bt_cal_chk <= 20) {
			/* 0x49c[0]=1 WL IQK, 0x49c[1]=1 BT IQK*/
			u8tmp = btcoexist->btc_read_1byte(
					btcoexist,
					0x49c);

			cnt_bt_cal_chk++;
			if (u8tmp & BIT(0)) {
				BTC_SPRINTF(trace_buf,
					    BT_TMP_BUF_SIZE,
					"[BTCoex], ########### WL is IQK (wait cnt=%d)\n",
					    cnt_bt_cal_chk);
				BTC_TRACE(trace_buf);
				delay_ms(50);
			} else if (u8tmp & BIT(1)) {
				BTC_SPRINTF(trace_buf,
					    BT_TMP_BUF_SIZE,
					"[BTCoex], ########### BT is IQK (wait cnt=%d)\n",
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

		/* set Path control owner to WL at runtime step */
		halbtc8821c1ant_ltecoex_pathcontrol_owner(
			btcoexist,
			BT_8821C_1ANT_PCO_WLSIDE);

		/* set GNT_BT to PTA */
		halbtc8821c1ant_ltecoex_set_gnt_bt(btcoexist,
					   BT_8821C_1ANT_GNT_BLOCK_RFC_BB,
					   BT_8821C_1ANT_GNT_TYPE_CTRL_BY_PTA,
					   BT_8821C_1ANT_SIG_STA_SET_BY_HW);

		halbtc8821c1ant_ltecoex_set_gnt_wl(btcoexist,
					   BT_8821C_1ANT_GNT_BLOCK_RFC_BB,
					   BT_8821C_1ANT_GNT_TYPE_CTRL_BY_PTA,
					   BT_8821C_1ANT_SIG_STA_SET_BY_HW);

		coex_sta->run_time_state = TRUE;

		if (BTC_ANT_PATH_AUTO == ant_pos_type) {
			if (rfe_type->wlg_Locate_at_btg)
				ant_pos_type =
					BTC_ANT_PATH_WIFI;
			else
				ant_pos_type = BTC_ANT_PATH_PTA;
		}

		if (rfe_type->wlg_Locate_at_btg)
			halbtc8821c1ant_set_int_block(btcoexist,
						      NORMAL_EXEC,
				BT_8821C_1ANT_INT_BLOCK_SWITCH_TO_WLG_OF_BTG);
		else
			halbtc8821c1ant_set_int_block(btcoexist,
						      NORMAL_EXEC,
				BT_8821C_1ANT_INT_BLOCK_SWITCH_TO_WLG_OF_WLAG);

		break;
	case BT_8821C_1ANT_PHASE_5G_RUNTIME:

		/* set Path control owner to WL at runtime step */
		halbtc8821c1ant_ltecoex_pathcontrol_owner(
			btcoexist,
			BT_8821C_1ANT_PCO_WLSIDE);

		/* set GNT_BT to SW Hi */
		halbtc8821c1ant_ltecoex_set_gnt_bt(btcoexist,
					   BT_8821C_1ANT_GNT_BLOCK_RFC_BB,
					   BT_8821C_1ANT_GNT_TYPE_CTRL_BY_PTA,
					   BT_8821C_1ANT_SIG_STA_SET_BY_HW);

		/* Set GNT_WL to SW Hi */
		halbtc8821c1ant_ltecoex_set_gnt_wl(btcoexist,
					   BT_8821C_1ANT_GNT_BLOCK_RFC_BB,
					   BT_8821C_1ANT_GNT_TYPE_CTRL_BY_SW,
					   BT_8821C_1ANT_SIG_STA_SET_TO_HIGH);

		coex_sta->run_time_state = TRUE;

		if (BTC_ANT_PATH_AUTO == ant_pos_type) {
			/*	if (rfe_type->ext_band_switch_exist)
					ant_pos_type =   BTC_ANT_PATH_PTA;
				else */
			ant_pos_type =
				BTC_ANT_PATH_WIFI5G;
		}

		halbtc8821c1ant_set_int_block(btcoexist,
					      NORMAL_EXEC,
			      BT_8821C_1ANT_INT_BLOCK_SWITCH_TO_WLA_OF_WLAG);

		break;
	case BT_8821C_1ANT_PHASE_BTMPMODE:
		/* Disable LTE Coex Function in WiFi side */
		halbtc8821c1ant_ltecoex_enable(btcoexist, 0x0);

		/* set Path control owner to WL */
		halbtc8821c1ant_ltecoex_pathcontrol_owner(
			btcoexist,
			BT_8821C_1ANT_PCO_WLSIDE);

		/* set GNT_BT to SW Hi */
		halbtc8821c1ant_ltecoex_set_gnt_bt(btcoexist,
					   BT_8821C_1ANT_GNT_BLOCK_RFC_BB,
					   BT_8821C_1ANT_GNT_TYPE_CTRL_BY_SW,
					   BT_8821C_1ANT_SIG_STA_SET_TO_HIGH);

		/* Set GNT_WL to SW Lo */
		halbtc8821c1ant_ltecoex_set_gnt_wl(btcoexist,
					   BT_8821C_1ANT_GNT_BLOCK_RFC_BB,
					   BT_8821C_1ANT_GNT_TYPE_CTRL_BY_SW,
					   BT_8821C_1ANT_SIG_STA_SET_TO_LOW);

		coex_sta->run_time_state = FALSE;

		/* Set Ext Ant Switch to BT side at BT MP mode */
		if (BTC_ANT_PATH_AUTO == ant_pos_type)
			ant_pos_type = BTC_ANT_PATH_BT;

		break;
	case BT_8821C_1ANT_PHASE_ANTENNA_DET:
		halbtc8821c1ant_ltecoex_pathcontrol_owner(btcoexist,
				BT_8821C_1ANT_PCO_WLSIDE);

		/* set GNT_BT to high */
		halbtc8821c1ant_ltecoex_set_gnt_bt(btcoexist,
					   BT_8821C_1ANT_GNT_BLOCK_RFC_BB,
					   BT_8821C_1ANT_GNT_TYPE_CTRL_BY_SW,
					   BT_8821C_1ANT_SIG_STA_SET_TO_HIGH);
		/* Set GNT_WL to high */
		halbtc8821c1ant_ltecoex_set_gnt_wl(btcoexist,
					   BT_8821C_1ANT_GNT_BLOCK_RFC_BB,
					   BT_8821C_1ANT_GNT_TYPE_CTRL_BY_SW,
					   BT_8821C_1ANT_SIG_STA_SET_TO_HIGH);

		if (BTC_ANT_PATH_AUTO == ant_pos_type)
			ant_pos_type = BTC_ANT_PATH_BT;

		coex_sta->run_time_state = FALSE;

		break;
	}

	if (phase != BT_8821C_1ANT_PHASE_WLAN_OFF) {
		switch (ant_pos_type) {
		case BTC_ANT_PATH_WIFI:
			halbtc8821c1ant_set_ext_ant_switch(
				btcoexist,
				force_exec,
				BT_8821C_1ANT_EXT_ANT_SWITCH_CTRL_BY_BBSW,
				BT_8821C_1ANT_EXT_ANT_SWITCH_TO_WLG);
			break;
		case BTC_ANT_PATH_WIFI5G
				:
			halbtc8821c1ant_set_ext_ant_switch(
				btcoexist,
				force_exec,
				BT_8821C_1ANT_EXT_ANT_SWITCH_CTRL_BY_BBSW,
				BT_8821C_1ANT_EXT_ANT_SWITCH_TO_WLA);
			break;
		case BTC_ANT_PATH_BT:
			halbtc8821c1ant_set_ext_ant_switch(
				btcoexist,
				force_exec,
				BT_8821C_1ANT_EXT_ANT_SWITCH_CTRL_BY_BBSW,
				BT_8821C_1ANT_EXT_ANT_SWITCH_TO_BT);
			break;
		default:
		case BTC_ANT_PATH_PTA:
			halbtc8821c1ant_set_ext_ant_switch(
				btcoexist,
				force_exec,
				BT_8821C_1ANT_EXT_ANT_SWITCH_CTRL_BY_PTA,
				BT_8821C_1ANT_EXT_ANT_SWITCH_TO_NOCARE);
			break;
		}

	}

#if BT_8821C_1ANT_COEX_DBG
	u32tmp1 = halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist, 0x38);
	u32tmp2 = halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist, 0x54);
	u32tmp3 = btcoexist->btc_read_4byte(btcoexist, 0xcb4);
	u8tmp  = btcoexist->btc_read_1byte(btcoexist, 0x73);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex],(After Ant-Setup phase---%d) 0xcb4 = 0x%x, 0x73 = 0x%x, 0x38= 0x%x, 0x54= 0x%x\n",
		    phase, u32tmp3, u8tmp, u32tmp1, u32tmp2);

	BTC_TRACE(trace_buf);
#endif
}


boolean halbtc8821c1ant_is_common_action(IN struct btc_coexist *btcoexist)
{
	boolean			common = FALSE, wifi_connected = FALSE, wifi_busy = FALSE;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	if (!wifi_connected &&
	    BT_8821C_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
	    coex_dm->bt_status) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Wifi non connected-idle + BT non connected-idle!!\n");
		BTC_TRACE(trace_buf);

		common = TRUE;
	} else if (wifi_connected &&
		   (BT_8821C_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
		    coex_dm->bt_status)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Wifi connected + BT non connected-idle!!\n");
		BTC_TRACE(trace_buf);

		common = TRUE;
	} else if (!wifi_connected &&
		   (BT_8821C_1ANT_BT_STATUS_CONNECTED_IDLE ==
		    coex_dm->bt_status)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Wifi non connected-idle + BT connected-idle!!\n");
		BTC_TRACE(trace_buf);

		common = TRUE;
	} else if (wifi_connected &&
		   (BT_8821C_1ANT_BT_STATUS_CONNECTED_IDLE ==
		    coex_dm->bt_status)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Wifi connected + BT connected-idle!!\n");
		BTC_TRACE(trace_buf);

		common = TRUE;
	} else if (!wifi_connected &&
		   (BT_8821C_1ANT_BT_STATUS_CONNECTED_IDLE !=
		    coex_dm->bt_status)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Wifi non connected-idle + BT Busy!!\n");
		BTC_TRACE(trace_buf);

		common = TRUE;
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

		common = FALSE;
	}

	return common;
}


/* *********************************************
 *
 *	Software Coex Mechanism start
 *
 * ********************************************* */



/* *********************************************
 *
 *	Non-Software Coex Mechanism start
 *
 * ********************************************* */
void halbtc8821c1ant_action_bt_whql_test(IN struct btc_coexist *btcoexist)
{
	halbtc8821c1ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO, NORMAL_EXEC,
				     BT_8821C_1ANT_PHASE_2G_RUNTIME);
	halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
	halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 8);
}

void halbtc8821c1ant_action_bt_hs(IN struct btc_coexist *btcoexist)
{
	halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
	halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 5);
}

void halbtc8821c1ant_action_bt_relink(IN struct btc_coexist *btcoexist)
{
	if (coex_sta->is_bt_multi_link == TRUE)
		return;

	halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
	halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 8);
}

void halbtc8821c1ant_action_bt_idle(IN struct btc_coexist *btcoexist)
{
	boolean wifi_busy = FALSE;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	if (!wifi_busy) {
		halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 3);
		halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 6);
	} else {  /* if wl busy */

		if (BT_8821C_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
		    coex_dm->bt_status) {

			halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);
			halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 33);
		} else {

			halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);
			halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 32);
		}
	}

}

void halbtc8821c1ant_action_bt_inquiry(IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean			wifi_connected = FALSE, wifi_busy = FALSE,
				bt_busy = FALSE;
	boolean	wifi_scan = FALSE, wifi_link = FALSE, wifi_roam = FALSE;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED, &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bt_busy);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &wifi_scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &wifi_link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &wifi_roam);


	if ((coex_sta->bt_create_connection) && ((wifi_link) || (wifi_roam)
		|| (wifi_scan) || (wifi_busy) || (coex_sta->wifi_is_high_pri_task))) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Wifi link/roam/Scan/busy/hi-pri-task + BT Inq/Page!!\n");
		BTC_TRACE(trace_buf);

		halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);

		if ((bt_link_info->a2dp_exist) && (!bt_link_info->pan_exist))
			halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 17);
		else
			halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 33);
	} else if ((!wifi_connected) && (!wifi_scan)) {

		halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
		halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 8);
	} else if (bt_link_info->pan_exist) {

		halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
		halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 22);
	} else if (bt_link_info->a2dp_exist) {

		halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
		halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 16);
	} else {

		halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);

		if ((wifi_link) || (wifi_roam) || (wifi_scan) || (wifi_busy)
			|| (coex_sta->wifi_is_high_pri_task))
			halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 21);
		else
			halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 23);
	}
}

void halbtc8821c1ant_action_bt_sco_hid_only_busy(IN struct btc_coexist
		*btcoexist)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean	wifi_connected = FALSE, wifi_busy = FALSE, wifi_cckdeadlock_ap = FALSE;
	u32  wifi_bw = 1;
	u8	iot_peer = BTC_IOT_PEER_UNKNOWN;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED, &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_IOT_PEER, &iot_peer);

	if ((iot_peer == BTC_IOT_PEER_ATHEROS) && (coex_sta->cck_lock_ever))
		wifi_cckdeadlock_ap = TRUE;

	if (bt_link_info->sco_exist) {
		halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
		halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 5);
	} else {
		/* for HID exist */
		if ((wifi_cckdeadlock_ap) && (coex_sta->is_bt_multi_link)) {

			if (coex_sta->hid_busy_num < 2)
				halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 14);
			else
				halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 13);
			halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 18);
		} else if (coex_sta->is_hid_low_pri_tx_overhead) {
			if (coex_sta->hid_busy_num < 2)
				halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 3);
			else
				halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 6);
			halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 18);
		} else if (coex_sta->hid_busy_num < 2) {
			halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 3);
			halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 11);
		} else if (wifi_bw == 0) { /* if 11bg mode */

			if (coex_sta->is_bt_multi_link) {
				halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 11);
				halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 11);
			} else {
				halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 11);
				halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 11);
			}
		} else {
			halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 6);
			halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 11);
		}
	}
}

void halbtc8821c1ant_action_wifi_under5g(IN struct btc_coexist *btcoexist)
{
	halbtc8821c1ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO, NORMAL_EXEC,
				     BT_8821C_1ANT_PHASE_5G_RUNTIME);
	halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
	halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 8);
}


void halbtc8821c1ant_action_wifi_only(IN struct btc_coexist *btcoexist)
{
	halbtc8821c1ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO, FORCE_EXEC,
				     BT_8821C_1ANT_PHASE_2G_RUNTIME);
	halbtc8821c1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 10);
	halbtc8821c1ant_ps_tdma(btcoexist, FORCE_EXEC, FALSE, 8);
}

void halbtc8821c1ant_action_wifi_native_lps(IN struct btc_coexist *btcoexist)
{
	halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 5);
	halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 8);
}

void halbtc8821c1ant_action_wifi_cck_dead_lock(IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	if ((bt_link_info->hid_exist) && (bt_link_info->a2dp_exist) &&
		(!bt_link_info->pan_exist)) {

		if ((coex_sta->cck_lock) || (coex_sta->cck_lock_warn)) {
			halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 13);
			halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 28);
		} else {

			halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 13);
			halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 8);
		}
	}
}

void halbtc8821c1ant_action_wifi_multi_port(IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	if ((BT_8821C_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
		coex_dm->bt_status) ||
		(BT_8821C_1ANT_BT_STATUS_CONNECTED_IDLE ==
		coex_dm->bt_status))
		halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
	else if (!bt_link_info->pan_exist)
		halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
	else
		halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);

	halbtc8821c1ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO, NORMAL_EXEC,
				     BT_8821C_1ANT_PHASE_2G_RUNTIME);
	halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 8);
}


void halbtc8821c1ant_action_wifi_linkscan_process(IN struct btc_coexist
		*btcoexist)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	if (bt_link_info->pan_exist) {

		halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
		halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 22);
	} else if (bt_link_info->a2dp_exist) {

		halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
		halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 27);
	} else {

		halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
		halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 21);
	}

}

void halbtc8821c1ant_action_wifi_connected_bt_acl_busy(IN struct btc_coexist
		*btcoexist)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean	wifi_busy = FALSE, wifi_turbo = FALSE, wifi_cckdeadlock_ap = FALSE;
	u32  wifi_bw = 1;
	u8	iot_peer = BTC_IOT_PEER_UNKNOWN;

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW,
			   &wifi_bw);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM, &coex_sta->scan_ap_num);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_IOT_PEER, &iot_peer);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"############# [BTCoex],  scan_ap_num = %d, wl_noisy_level = %d\n",
		    coex_sta->scan_ap_num, coex_sta->wl_noisy_level);
	BTC_TRACE(trace_buf);

	if ((wifi_busy) && (coex_sta->wl_noisy_level == 0))
		wifi_turbo = TRUE;

	if ((iot_peer == BTC_IOT_PEER_ATHEROS) && (coex_sta->cck_lock_ever))
		wifi_cckdeadlock_ap = TRUE;

	if ((bt_link_info->a2dp_exist) && (coex_sta->is_bt_a2dp_sink)) {

		if (wifi_cckdeadlock_ap)
			halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 13);
		else
			halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 6);

		halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 12);
	} else if (bt_link_info->a2dp_only) { /* A2DP		 */

		if (wifi_cckdeadlock_ap)
			halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 13);
		else if (wifi_turbo)
			halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);
		else
			halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);

		halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 7);
	} else if (((bt_link_info->a2dp_exist) &&
		    (bt_link_info->pan_exist)) ||
			(bt_link_info->hid_exist && bt_link_info->a2dp_exist &&
		bt_link_info->pan_exist)) { /* A2DP+PAN(OPP,FTP), HID+A2DP+PAN(OPP,FTP) */

		if (wifi_cckdeadlock_ap) {
			if ((bt_link_info->hid_exist) && (coex_sta->hid_busy_num < 2))
				halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 14);
			else
				halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 13);
		} else if (bt_link_info->hid_exist) {
			if (coex_sta->hid_busy_num < 2)
				halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 3);
			else
				halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
		} else if (wifi_turbo)
			halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);
		else
			halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);

		if (wifi_busy)
			halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 13);
		else
			halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 14);
	} else if (bt_link_info->hid_exist &&
		   bt_link_info->a2dp_exist) { /* HID+A2DP */

		if (wifi_cckdeadlock_ap) {
#if 1
			if (coex_sta->hid_busy_num < 2)
				halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 14);
			else
				halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 13);

			if (coex_sta->hid_pair_cnt > 1)
				halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 24);
			else
				halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 8);
#endif

#if 0
			halbtc8821c1ant_action_wifi_cck_dead_lock(btcoexist);
#endif
		} else {
			if (coex_sta->hid_busy_num < 2) /* 2/18 HID */
				halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 3);
			else if (wifi_bw == 0)/* if 11bg mode */
				halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 12);
			else
				halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);

			if (coex_sta->hid_pair_cnt > 1)
				halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 24);
			else
				halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 8);
		}
	} else if ((bt_link_info->pan_only)
		   || (bt_link_info->hid_exist && bt_link_info->pan_exist)) {
			/* PAN(OPP,FTP), HID+PAN(OPP,FTP) */

		if (wifi_cckdeadlock_ap) {
			if ((bt_link_info->hid_exist) && (coex_sta->hid_busy_num < 2))
				halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 14);
			else
				halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 13);
		} else if (bt_link_info->hid_exist) {
			if (coex_sta->hid_busy_num < 2)
				halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 3);
			else
				halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
		} else if (wifi_turbo)
			halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);
		else
			halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);

		if (!wifi_busy)
			halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 4);
		else
			halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 3);
	} else {
		/* BT no-profile busy (0x9) */
		halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 4);
		halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, TRUE, 33);
	}

}

void halbtc8821c1ant_action_wifi_not_connected(IN struct btc_coexist *btcoexist)
{
	/* tdma and coex table */
	halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
	halbtc8821c1ant_ps_tdma(btcoexist, FORCE_EXEC, FALSE, 8);
}

void halbtc8821c1ant_action_wifi_connected(IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean	wifi_busy = FALSE;
	boolean	wifi_under_5g = FALSE;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], CoexForWifiConnect()===>\n");
	BTC_TRACE(trace_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);

	if (wifi_under_5g) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], WiFi is under 5G!!!\n");
		BTC_TRACE(trace_buf);

		halbtc8821c1ant_action_wifi_under5g(btcoexist);
		return;
	}

	halbtc8821c1ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO,
				     NORMAL_EXEC,
				     BT_8821C_1ANT_PHASE_2G_RUNTIME);

	if ((coex_dm->bt_status == BT_8821C_1ANT_BT_STATUS_ACL_BUSY) ||
		(coex_dm->bt_status == BT_8821C_1ANT_BT_STATUS_ACL_SCO_BUSY)) {

		if (bt_link_info->hid_only)  /* HID only */
			halbtc8821c1ant_action_bt_sco_hid_only_busy(btcoexist);
		else
			halbtc8821c1ant_action_wifi_connected_bt_acl_busy(btcoexist);

	} else if (coex_dm->bt_status == BT_8821C_1ANT_BT_STATUS_SCO_BUSY)
		halbtc8821c1ant_action_bt_sco_hid_only_busy(btcoexist);
	else
		halbtc8821c1ant_action_bt_idle(btcoexist);

}

void halbtc8821c1ant_run_sw_coexist_mechanism(IN struct btc_coexist *btcoexist)
{
	u8				algorithm = 0;

	algorithm = halbtc8821c1ant_action_algorithm(btcoexist);
	coex_dm->cur_algorithm = algorithm;

	if (!halbtc8821c1ant_is_common_action(btcoexist)) {
		switch (coex_dm->cur_algorithm) {
		case BT_8821C_1ANT_COEX_ALGO_SCO:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Action algorithm = SCO.\n");
			BTC_TRACE(trace_buf);
			break;
		case BT_8821C_1ANT_COEX_ALGO_HID:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Action algorithm = HID.\n");
			BTC_TRACE(trace_buf);
			break;
		case BT_8821C_1ANT_COEX_ALGO_A2DP:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Action algorithm = A2DP.\n");
			BTC_TRACE(trace_buf);
			break;
		case BT_8821C_1ANT_COEX_ALGO_A2DP_PANHS:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action algorithm = A2DP+PAN(HS).\n");
			BTC_TRACE(trace_buf);
			break;
		case BT_8821C_1ANT_COEX_ALGO_PANEDR:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Action algorithm = PAN(EDR).\n");
			BTC_TRACE(trace_buf);
			break;
		case BT_8821C_1ANT_COEX_ALGO_PANHS:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Action algorithm = HS mode.\n");
			BTC_TRACE(trace_buf);
			break;
		case BT_8821C_1ANT_COEX_ALGO_PANEDR_A2DP:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Action algorithm = PAN+A2DP.\n");
			BTC_TRACE(trace_buf);
			break;
		case BT_8821C_1ANT_COEX_ALGO_PANEDR_HID:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action algorithm = PAN(EDR)+HID.\n");
			BTC_TRACE(trace_buf);
			break;
		case BT_8821C_1ANT_COEX_ALGO_HID_A2DP_PANEDR:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action algorithm = HID+A2DP+PAN.\n");
			BTC_TRACE(trace_buf);
			break;
		case BT_8821C_1ANT_COEX_ALGO_HID_A2DP:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Action algorithm = HID+A2DP.\n");
			BTC_TRACE(trace_buf);
			break;
		default:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action algorithm = coexist All Off!!\n");
			BTC_TRACE(trace_buf);
			break;
		}
		coex_dm->pre_algorithm = coex_dm->cur_algorithm;
	}
}


void halbtc8821c1ant_run_coexist_mechanism(IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean	wifi_connected = FALSE, bt_hs_on = FALSE;
	boolean	increase_scan_dev_num = FALSE;
	boolean	bt_ctrl_agg_buf_size = FALSE;
	boolean	miracast_plus_bt = FALSE, wifi_under_5g = FALSE;
	u8	agg_buf_size = 5;
	u32	wifi_link_status = 0;
	u32	num_of_wifi_link = 0, wifi_bw;
	u8	iot_peer = BTC_IOT_PEER_UNKNOWN;
	boolean	scan = FALSE, link = FALSE, roam = FALSE, under_4way = FALSE;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS, &under_4way);

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
			    "[BTCoex], RunCoexistMechanism(), wifi is under IPS !!!\n");
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
			"[BTCoex], BtInfoNotify(), return for freeze_coexrun_by_btinfo\n");
		BTC_TRACE(trace_buf);
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);

	if ((wifi_under_5g) &&
		(coex_sta->switch_band_notify_to != BTC_SWITCH_TO_24G) &&
		(coex_sta->switch_band_notify_to != BTC_SWITCH_TO_24G_NOFORSCAN)) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], WiFi is under 5G!!!\n");
		BTC_TRACE(trace_buf);

		halbtc8821c1ant_action_wifi_under5g(btcoexist);
		return;
	} else {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], WiFi is under 2G!!!\n");
		BTC_TRACE(trace_buf);

		halbtc8821c1ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO,
					     NORMAL_EXEC,
					     BT_8821C_1ANT_PHASE_2G_RUNTIME);
	}

	if ((coex_sta->under_lps) && (!coex_sta->force_lps_ctrl) &&
		(!coex_sta->acl_busy)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RunCoexistMechanism(), wifi is under LPS !!!\n");
		BTC_TRACE(trace_buf);
		halbtc8821c1ant_action_wifi_native_lps(btcoexist);
		return;
	}

	if (coex_sta->bt_whck_test) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is under WHCK TEST!!!\n");
		BTC_TRACE(trace_buf);
		halbtc8821c1ant_action_bt_whql_test(btcoexist);
		return;
	}

	if (coex_sta->bt_disabled) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is disabled !!!\n");
		BTC_TRACE(trace_buf);
		halbtc8821c1ant_action_wifi_only(btcoexist);
		return;
	}

	if (coex_sta->c2h_bt_inquiry_page) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is under inquiry/page scan !!\n");
		BTC_TRACE(trace_buf);
		halbtc8821c1ant_action_bt_inquiry(btcoexist);
		return;
	}

	if ((coex_sta->is_setupLink) &&
			(coex_sta->bt_relink_downcount != 0)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is re-link !!!\n");
		BTC_TRACE(trace_buf);
		halbtc8821c1ant_action_bt_relink(btcoexist);
		return;
	}

	if ((BT_8821C_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) ||
	    (BT_8821C_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
	    (BT_8821C_1ANT_BT_STATUS_ACL_SCO_BUSY == coex_dm->bt_status))
		increase_scan_dev_num = TRUE;

	btcoexist->btc_set(btcoexist, BTC_SET_BL_INC_SCAN_DEV_NUM,
			   &increase_scan_dev_num);

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

			halbtc8821c1ant_action_wifi_linkscan_process(btcoexist);
		} else
			halbtc8821c1ant_action_wifi_multi_port(btcoexist);

		return;
	} else {

		miracast_plus_bt = FALSE;
		btcoexist->btc_set(btcoexist, BTC_SET_BL_MIRACAST_PLUS_BT,
				   &miracast_plus_bt);
	}

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED, &wifi_connected);

	if ((bt_link_info->bt_link_exist) && (wifi_connected)) {

		btcoexist->btc_get(btcoexist, BTC_GET_U1_IOT_PEER, &iot_peer);

		if (BTC_IOT_PEER_CISCO == iot_peer) {

			if (BTC_WIFI_BW_HT40 == wifi_bw)
				halbtc8821c1ant_limited_rx(btcoexist,
					   NORMAL_EXEC, FALSE, TRUE, 0x10);
			else
				halbtc8821c1ant_limited_rx(btcoexist,
					   NORMAL_EXEC, FALSE, TRUE, 0x8);
		}
	}

	/* just print debug message */
	halbtc8821c1ant_run_sw_coexist_mechanism(btcoexist);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);

	if (bt_hs_on) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "############# [BTCoex],  BT Is hs\n");
		BTC_TRACE(trace_buf);
		halbtc8821c1ant_action_bt_hs(btcoexist);
		return;
	}

	if ((BT_8821C_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
	     coex_dm->bt_status) ||
	    (BT_8821C_1ANT_BT_STATUS_CONNECTED_IDLE ==
	     coex_dm->bt_status)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "############# [BTCoex],  BT Is idle\n");
		BTC_TRACE(trace_buf);
		halbtc8821c1ant_action_bt_idle(btcoexist);
		return;
	}

	if (scan || link || roam || under_4way) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], scan = %d, link = %d, roam = %d 4way = %d!!!\n",
			    scan, link, roam, under_4way);
		BTC_TRACE(trace_buf);

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], wifi is under linkscan process!!\n");
		BTC_TRACE(trace_buf);

		halbtc8821c1ant_action_wifi_linkscan_process(btcoexist);
	} else if (wifi_connected) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], wifi is under connected!!\n");
		BTC_TRACE(trace_buf);

		halbtc8821c1ant_action_wifi_connected(btcoexist);
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], wifi is under not-connected!!\n");
		BTC_TRACE(trace_buf);

		halbtc8821c1ant_action_wifi_not_connected(btcoexist);
	}
}

void halbtc8821c1ant_init_coex_dm(IN struct btc_coexist *btcoexist)
{
	/* force to reset coex mechanism */
	halbtc8821c1ant_low_penalty_ra(btcoexist, FORCE_EXEC, FALSE);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], Coex Mechanism Init!!\n");
	BTC_TRACE(trace_buf);

	coex_sta->pop_event_cnt = 0;
	coex_sta->cnt_RemoteNameReq = 0;
	coex_sta->cnt_ReInit = 0;
	coex_sta->cnt_setupLink = 0;
	coex_sta->cnt_IgnWlanAct = 0;
	coex_sta->cnt_Page = 0;
	coex_sta->cnt_RoleSwitch = 0;
	coex_sta->switch_band_notify_to = BTC_NOT_SWITCH;

	halbtc8821c1ant_query_bt_info(btcoexist);
}

void halbtc8821c1ant_init_hw_config(IN struct btc_coexist *btcoexist,
				    IN boolean back_up, IN boolean wifi_only)
{
	u32			u32tmp1 = 0, u32tmp2 = 0, u32tmp3 = 0;
	u16			u16tmp1 = 0;
	u8			i;
	struct  btc_board_info *board_info = &btcoexist->board_info;


	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], 1Ant Init HW Config!!\n");
	BTC_TRACE(trace_buf);

	u32tmp3 = btcoexist->btc_read_4byte(btcoexist, 0xcb4);
	u32tmp1 = halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist, 0x38);
	u32tmp2 = halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist, 0x54);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex],(Before Init HW config) 0xcb4 = 0x%x, 0x38= 0x%x, 0x54= 0x%x\n",
		    u32tmp3, u32tmp1, u32tmp2);
	BTC_TRACE(trace_buf);


	coex_sta->bt_coex_supported_feature = 0;
	coex_sta->bt_coex_supported_version = 0;
	coex_sta->bt_ble_scan_type = 0;
	coex_sta->bt_ble_scan_para[0] = 0;
	coex_sta->bt_ble_scan_para[1] = 0;
	coex_sta->bt_ble_scan_para[2] = 0;
	coex_sta->bt_reg_vendor_ac = 0xffff;
	coex_sta->bt_reg_vendor_ae = 0xffff;
	coex_sta->isolation_btween_wb = BT_8821C_1ANT_DEFAULT_ISOLATION;
	coex_sta->gnt_error_cnt = 0;
	coex_sta->bt_relink_downcount = 0;
	coex_sta->is_set_ps_state_fail = FALSE;
	coex_sta->cnt_set_ps_state_fail = 0;
	coex_sta->wl_rx_rate = BTC_UNKNOWN;
	coex_sta->wl_rts_rx_rate = BTC_UNKNOWN;
	coex_sta->wl_center_channel = 0;

	for (i = 0; i <= 9; i++)
		coex_sta->bt_afh_map[i] = 0;

	/* Setup RF front end type */
	halbtc8821c1ant_set_rfe_type(btcoexist);

	/* 0xf0[15:12] --> Chip Cut information */
	coex_sta->cut_version = (btcoexist->btc_read_1byte(btcoexist,
				 0xf1) & 0xf0) >> 4;

	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x550, 0x8,
					   0x1);  /* enable TBTT nterrupt */

	/* BT report packet sample rate	 */
	btcoexist->btc_write_1byte(btcoexist, 0x790, 0x5);

	/* Init 0x778 = 0x1 for 1-Ant */
	btcoexist->btc_write_1byte(btcoexist, 0x778, 0x1);

	/* Enable PTA (3-wire function form BT side) */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x40, 0x20, 0x1);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x41, 0x02, 0x1);

	/* Enable PTA (tx/rx signal form WiFi side) */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x4c6, 0x10, 0x1);

	/* set GNT_BT=1 for coex table select both */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x763, 0x10, 0x1);

	halbtc8821c1ant_enable_gnt_to_gpio(btcoexist, TRUE);

#if 0
	/* check if WL firmware download ok */
	/*if (btcoexist->btc_read_1byte(btcoexist, 0x80) == 0xc6)*/
	 halbtc8821c1ant_post_state_to_bt(btcoexist,
					BT_8821C_1ANT_SCOREBOARD_ONOFF, TRUE);
#endif

	/* PTA parameter */
	halbtc8821c1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 0);
	halbtc8821c1ant_ps_tdma(btcoexist, FORCE_EXEC, FALSE, 8);

	psd_scan->ant_det_is_ant_det_available = TRUE;

	/* Antenna config */
	if (coex_sta->is_rf_state_off) {

		halbtc8821c1ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO,
					     FORCE_EXEC,
					     BT_8821C_1ANT_PHASE_WLAN_OFF);

		btcoexist->stop_coex_dm = TRUE;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], **********  halbtc8821c1ant_init_hw_config (RF Off)**********\n");
		BTC_TRACE(trace_buf);
	} else if (wifi_only) {
		coex_sta->concurrent_rx_mode_on = FALSE;
		halbtc8821c1ant_set_ant_path(btcoexist, BTC_ANT_PATH_WIFI,
					     FORCE_EXEC,
					     BT_8821C_1ANT_PHASE_WLANONLY_INIT);

		btcoexist->stop_coex_dm = TRUE;
	} else {
		/*Set BT polluted packet on for Tx rate adaptive not including Tx retry break by PTA, 0x45c[19] =1 */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x45e, 0x8, 0x1);

		coex_sta->concurrent_rx_mode_on = TRUE;
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0x2, 0x0);

		halbtc8821c1ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO,
					     FORCE_EXEC,
					     BT_8821C_1ANT_PHASE_COEX_INIT);

		btcoexist->stop_coex_dm = FALSE;
	}

	u32tmp3 = btcoexist->btc_read_4byte(btcoexist, 0xcb4);
	u32tmp1 = halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist, 0x38);
	u32tmp2 = halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist, 0x54);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], (After Init HW config) 0xcb4 = 0x%x, 0x38= 0x%x, 0x54= 0x%x\n",
		    u32tmp3, u32tmp1, u32tmp2);
	BTC_TRACE(trace_buf);

}


/* ************************************************************
 * work around function start with wa_halbtc8821c1ant_
 * ************************************************************
 * ************************************************************
 * extern function start with ex_halbtc8821c1ant_
 * ************************************************************ */
void ex_halbtc8821c1ant_power_on_setting(IN struct btc_coexist *btcoexist)
{
	struct  btc_board_info	*board_info = &btcoexist->board_info;
	u8 u8tmp = 0x0;
	u16 u16tmp = 0x0;
	u32	value = 0;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"xxxxxxxxxxxxxxxx Execute 8821c 1-Ant PowerOn Setting xxxxxxxxxxxxxxxx!!\n");
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

	/* Set Antenna Path to BT side */
	/* Check efuse 0xc3[6] for Single Antenna Path */
	if (board_info->single_ant_path == 0) {

		board_info->btdm_ant_pos = BTC_ANTENNA_AT_AUX_PORT;
		u8tmp = 1;
	} else if (board_info->single_ant_path == 1) {

		board_info->btdm_ant_pos = BTC_ANTENNA_AT_MAIN_PORT;
		u8tmp = 0;
	}

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], ********** (Power On) single_ant_path  = %d, btdm_ant_pos = %d\n",
		    board_info->single_ant_path , board_info->btdm_ant_pos);
	BTC_TRACE(trace_buf);

	/* Setup RF front end type */
	halbtc8821c1ant_set_rfe_type(btcoexist);

	/* Set Antenna Path to BT side */
	halbtc8821c1ant_set_ant_path(btcoexist,
				     BTC_ANT_PATH_AUTO,
				     FORCE_EXEC,
				     BT_8821C_1ANT_PHASE_COEX_POWERON);

	/* Save"single antenna position" info in Local register setting for FW reading, because FW may not ready at  power on */
	if (btcoexist->chip_interface == BTC_INTF_PCI)
		btcoexist->btc_write_local_reg_1byte(btcoexist, 0x3e0, u8tmp);
	else if (btcoexist->chip_interface == BTC_INTF_USB)
		btcoexist->btc_write_local_reg_1byte(btcoexist, 0xfe08, u8tmp);
	else if (btcoexist->chip_interface == BTC_INTF_SDIO)
		btcoexist->btc_write_local_reg_1byte(btcoexist, 0x60, u8tmp);

	/* enable GNT_WL/GNT_BT debug signal to GPIO14/15 */
	halbtc8821c1ant_enable_gnt_to_gpio(btcoexist, TRUE);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], **********  LTE coex Reg 0x38 (Power-On) = 0x%x\n",
		    halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist, 0x38));
	BTC_TRACE(trace_buf);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], **********  MAC Reg 0x70/ BB Reg 0xcb4 (Power-On) = 0x%x / 0x%x\n",
		    btcoexist->btc_read_4byte(btcoexist, 0x70),
		    btcoexist->btc_read_4byte(btcoexist, 0xcb4));
	BTC_TRACE(trace_buf);

}

void ex_halbtc8821c1ant_pre_load_firmware(IN struct btc_coexist *btcoexist)
{
}

void ex_halbtc8821c1ant_init_hw_config(IN struct btc_coexist *btcoexist,
				       IN boolean wifi_only)
{
	halbtc8821c1ant_init_hw_config(btcoexist, TRUE, wifi_only);
}

void ex_halbtc8821c1ant_init_coex_dm(IN struct btc_coexist *btcoexist)
{
	halbtc8821c1ant_init_coex_dm(btcoexist);
}

void ex_halbtc8821c1ant_display_simple_coex_info(IN struct btc_coexist *btcoexist)
{
	struct  btc_board_info		*board_info = &btcoexist->board_info;
	struct  btc_bt_link_info	*bt_link_info = &btcoexist->bt_link_info;

	u8		*cli_buf = btcoexist->cli_buf;
	u32		bt_patch_ver = 0, bt_coex_ver = 0;
	static u8	cnt = 0;


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

	// BT coex. info.
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n _____[BT Coexist info]____");
	CL_PRINTF(cli_buf);

	if (btcoexist->manual_control) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			"\r\n __[Under Manual Control]_");
	CL_PRINTF(cli_buf);
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			"\r\n _________________________");
		CL_PRINTF(cli_buf);
	}
	if (btcoexist->stop_coex_dm) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n ____[Coex is STOPPED]____");
		CL_PRINTF(cli_buf);
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n _________________________");
		CL_PRINTF(cli_buf);
	}

	if (psd_scan->ant_det_try_count == 0) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %d/ %d/ %s / 0x%x",
			   "Ant PG Num/ Mech/ Pos/ RFE",
			   board_info->pg_ant_num, board_info->btdm_ant_num,
			   (board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT
			    ? "Main" : "Aux"),
			   board_info->rfe_type);
		CL_PRINTF(cli_buf);
	} else {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %d/ %d/ %s/ 0x%x  (%d/%d/%d)",
			   "Ant PG Num/ Mech(Ant_Det)/ Pos/ RFE",
			   board_info->pg_ant_num,
			   board_info->btdm_ant_num_by_ant_det,
			   (board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT
			    ? "Main" : "Aux"),
			   board_info->rfe_type,
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

	bt_coex_ver = ((coex_sta->bt_coex_supported_version & 0xff00) >> 8);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			"\r\n %-35s = %d_%02x/ 0x%02x/ 0x%02x (%s)",
			"CoexVer WL/  BT_Desired/ BT_Report",
			glcoex_ver_date_8821c_1ant, glcoex_ver_8821c_1ant,
			glcoex_ver_btdesired_8821c_1ant,
			bt_coex_ver,
			(bt_coex_ver == 0xff ? "Unknown" :
			(coex_sta->bt_disabled ? "BT-disable" :
			(bt_coex_ver >= glcoex_ver_btdesired_8821c_1ant ?
			"Match" : "Mis-Match"))));
	CL_PRINTF(cli_buf);

	// BT Status
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s ", "BT status",
			((coex_sta->bt_disabled) ? ("disabled") :	((
			coex_sta->c2h_bt_inquiry_page) ? ("inquiry/page")
			: ((BT_8821C_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
			coex_dm->bt_status) ? "non-connected idle" :
			((BT_8821C_1ANT_BT_STATUS_CONNECTED_IDLE ==
			coex_dm->bt_status) ? "connected-idle" : "busy")))));
	CL_PRINTF(cli_buf);

	// HW settings
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
			   "0x770(Hi-pri rx/tx)",
			   coex_sta->high_priority_rx, coex_sta->high_priority_tx);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d %s",
			   "0x774(Lo-pri rx/tx)",
			   coex_sta->low_priority_rx, coex_sta->low_priority_tx,
			   (bt_link_info->slave_role ? "(Slave!!)" : (
			   coex_sta->is_tdma_btautoslot_hang ? "(auto-slot hang!!)" : "")));
	CL_PRINTF(cli_buf);

}

void ex_halbtc8821c1ant_display_coex_info(IN struct btc_coexist *btcoexist)
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
	boolean			lte_coex_on = FALSE;
	static u8 cnt = 0;


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
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %d/ %d/ %s / 0x%x",
			   "Ant PG Num/ Mech/ Pos/ RFE",
			   board_info->pg_ant_num, board_info->btdm_ant_num,
			   (board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT
			    ? "Main" : "Aux"),
			   board_info->rfe_type);
		CL_PRINTF(cli_buf);
	} else {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %d/ %d/ %s/ 0x%x  (%d/%d/%d)",
			   "Ant PG Num/ Mech(Ant_Det)/ Pos/ RFE",
			   board_info->pg_ant_num,
			   board_info->btdm_ant_num_by_ant_det,
			   (board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT
			    ? "Main" : "Aux"),
			   board_info->rfe_type,
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

	bt_patch_ver = btcoexist->bt_info.bt_get_fw_ver;
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
	phyver = btcoexist->btc_get_bt_phydm_version(btcoexist);

	bt_coex_ver = ((coex_sta->bt_coex_supported_version & 0xff00) >> 8);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %d_%02x/ 0x%02x/ 0x%02x (%s)",
		   "CoexVer WL/  BT_Desired/ BT_Report",
		   glcoex_ver_date_8821c_1ant, glcoex_ver_8821c_1ant,
		   glcoex_ver_btdesired_8821c_1ant,
		   bt_coex_ver,
		   (bt_coex_ver == 0xff ? "Unknown" :
		    (coex_sta->bt_disabled ? "BT-disable" :
		     (bt_coex_ver >= glcoex_ver_btdesired_8821c_1ant ?
		      "Match" : "Mis-Match"))));
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ v%d/ %c",
		   "W_FW/ B_FW/ Phy/ Kt",
		   fw_ver, bt_patch_ver, phyver,
		   coex_sta->cut_version + 65);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x (RF-Ch = %d)",
		   "AFH Map to BT",
		   coex_dm->wifi_chnl_info[0], coex_dm->wifi_chnl_info[1],
		   coex_dm->wifi_chnl_info[2], coex_sta->wl_center_channel);
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
		   "\r\n %-35s = [%s/ %d dBm/ %d/ %d] ",
		   "BT [status/ rssi/ retryCnt/ popCnt]",
		   ((coex_sta->bt_disabled) ? ("disabled") :	((
			   coex_sta->c2h_bt_inquiry_page) ? ("inquiry/page")
			   : ((BT_8821C_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
			       coex_dm->bt_status) ? "non-connected idle" :
		((BT_8821C_1ANT_BT_STATUS_CONNECTED_IDLE == coex_dm->bt_status)
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
			   ((bt_link_info->a2dp_exist) ?
			   ((coex_sta->is_bt_a2dp_sink) ? "A2DP sink," :
			    "A2DP,") : ""),
			   ((bt_link_info->sco_exist) ?  "HFP," : ""),
			   ((bt_link_info->hid_exist) ?
			    ((coex_sta->hid_busy_num >= 2) ? "HID(4/18)," :
			     "HID(2/18),") : ""),
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
			   ((coex_sta->is_autoslot) ? "On" : "Off")
			  );
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

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %d/ %s/ 0x%x",
				"Role/RoleSwCnt/IgnWlact/Feature",
				((bt_link_info->slave_role) ? "Slave" : "Master"),
				coex_sta->cnt_RoleSwitch,
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
		   coex_sta->cnt_ReInit,
		   coex_sta->cnt_setupLink,
		   coex_sta->cnt_IgnWlanAct,
		   coex_sta->cnt_Page,
		   coex_sta->cnt_RemoteNameReq
		  );
	CL_PRINTF(cli_buf);

	halbtc8821c1ant_read_score_board(btcoexist,	&u16tmp[0]);

	if ((coex_sta->bt_reg_vendor_ae == 0xffff) ||
	    (coex_sta->bt_reg_vendor_ac == 0xffff))
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = x/ x/ %04x",
			   "0xae[4]/0xac[1:0]/Scoreboard(B->W)", u16tmp[0]);
	else
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = 0x%x/ 0x%x/ %04x",
			   "0xae[4]/0xac[1:0]/Scoreboard",
			   ((coex_sta->bt_reg_vendor_ae & BIT(4)) >> 4),
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

	for (i = 0; i < BT_INFO_SRC_8821C_1ANT_MAX; i++) {
		if (coex_sta->bt_info_c2h_cnt[i]) {
			CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
				"\r\n %-35s = %02x %02x %02x %02x %02x %02x %02x(%d)",
				   glbt_info_src_8821c_1ant[i],
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
			   "============[Mechanisms]============");

	CL_PRINTF(cli_buf);

	ps_tdma_case = coex_dm->cur_ps_tdma;
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %02x %02x %02x %02x %02x (case-%d, %s)",
		   "TDMA",
		   coex_dm->ps_tdma_para[0], coex_dm->ps_tdma_para[1],
		   coex_dm->ps_tdma_para[2], coex_dm->ps_tdma_para[3],
		   coex_dm->ps_tdma_para[4], ps_tdma_case,
		   (coex_dm->cur_ps_tdma_on ? "TDMA On" : "TDMA Off"));

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
		   "\r\n %-35s = 0x%x/ 0x%x/ %04x",
		   "0x778/0x6cc/Scoreboard(W->B)",
		   u8tmp[0], u32tmp[0], coex_sta->score_board_WB);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s/ %s/ %d",
		   "AntDiv/BtCtrlLPS/LPRA/PsFail",
		   ((board_info->ant_div_cfg) ? "On" : "Off"),
		   ((coex_sta->force_lps_ctrl) ? "On" : "Off"),
		   ((coex_dm->cur_low_penalty_ra) ? "On" : "Off"),
		   coex_sta->cnt_set_ps_state_fail);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d/ %d",
		   "Null All/Retry/Ack/BT_Empty/BT_Late",
		   coex_sta->wl_fw_dbg_info[1],
		   coex_sta->wl_fw_dbg_info[2],
		   coex_sta->wl_fw_dbg_info[3],
		   coex_sta->wl_fw_dbg_info[4],
		   coex_sta->wl_fw_dbg_info[5]);
	CL_PRINTF(cli_buf);

	u32tmp[0] = halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist, 0x38);
	lte_coex_on = ((u32tmp[0] & BIT(7)) >> 7) ?  TRUE : FALSE;

	if (lte_coex_on) {

		u32tmp[0] = halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist,
				0xa0);
		u32tmp[1] = halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist,
				0xa4);

		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
			   "LTE Coex Table W_L/B_L",
			   u32tmp[0] & 0xffff, u32tmp[1] & 0xffff);
		CL_PRINTF(cli_buf);

		u32tmp[0] = halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist,
				0xa8);
		u32tmp[1] = halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist,
				0xac);
		u32tmp[2] = halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist,
				0xb0);
		u32tmp[3] = halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist,
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

	u32tmp[0] = halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist, 0x38);
	u32tmp[1] = halbtc8821c1ant_ltecoex_indirect_read_reg(btcoexist, 0x54);
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
			   (int)((u32tmp[1] & BIT(1)) >> 1),
			   (int)(u32tmp[1] & BIT(0)));
		CL_PRINTF(cli_buf);
	}
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %s (BB:%s)/ %s (BB:%s)/ %s %d",
		   "GNT_WL_Ctrl/GNT_BT_Ctrl/Dbg",
		   ((u32tmp[0] & BIT(12)) ? "SW" : "HW"),
		   ((u32tmp[0] & BIT(8)) ?  "SW" : "HW"),
		   ((u32tmp[0] & BIT(14)) ? "SW" : "HW"),
		   ((u32tmp[0] & BIT(10)) ?  "SW" : "HW"),
		   ((u8tmp[0] & BIT(3)) ? "On" : "Off"),
		   coex_sta->gnt_error_cnt);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ 0x%x",
		   "GNT_WL/GNT_BT/ RF_0x1",
		   (int)((u32tmp[1] & BIT(2)) >> 2),
		   (int)((u32tmp[1] & BIT(3)) >> 3),
		   btcoexist->btc_get_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff));
	CL_PRINTF(cli_buf);


	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xcb0);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0xcb4);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0xcba);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%04x/ 0x%04x/ 0x%02x %s",
		   "0xcb0/0xcb4/0xcb8[23:16]",
		   u32tmp[0], u32tmp[1], u8tmp[0],
		   ((u8tmp[0] & 0x1) == 0x1 ?  "(BTG)" :   "(WL_A+G)"));
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x4c);
	u8tmp[2] = btcoexist->btc_read_1byte(btcoexist, 0x64);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x4c6);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x40);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x",
		   "4c[24:23]/64[0]/4c6[4]/40[5]",
		   (u32tmp[0] & (BIT(24) | BIT(23))) >> 23 , u8tmp[2] & 0x1 ,
		   (int)((u8tmp[0] & BIT(4)) >> 4),
		   (int)((u8tmp[1] & BIT(5)) >> 5));
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x550);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x522);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x953);
	u8tmp[2] = btcoexist->btc_read_1byte(btcoexist, 0xc50);
	u8tmp[3] = btcoexist->btc_read_1byte(btcoexist, 0x60a);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ %s/ 0x%x/ 0x%x",
		   "0x550/0x522/4-RxAGC/0xc50/0x60a",
		u32tmp[0], u8tmp[0], (u8tmp[1] & 0x2) ? "On" : "Off",
		u8tmp[2], u8tmp[3]);
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

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d (Rx_rate Data/RTS= %d/%d)",
		   "CRC_OK CCK/11g/11n/11ac",
		   coex_sta->crc_ok_cck, coex_sta->crc_ok_11g,
		   coex_sta->crc_ok_11n, coex_sta->crc_ok_11n_vht,
		   coex_sta->wl_rx_rate, coex_sta->wl_rts_rx_rate);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d",
		   "CRC_Err CCK/11g/11n/11ac",
		   coex_sta->crc_err_cck, coex_sta->crc_err_11g,
		   coex_sta->crc_err_11n, coex_sta->crc_err_11n_vht);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s/ %s/ %s/ %d",
		   "HiPr/ Locking/ warn/ Locked/ Noisy",
		   (coex_sta->wifi_is_high_pri_task ? "Yes" : "No"),
		   (coex_sta->cck_lock ? "Yes" : "No"),
		   (coex_sta->cck_lock_warn ? "Yes" : "No"),
		   (coex_sta->cck_lock_ever ? "Yes" : "No"),
		   coex_sta->wl_noisy_level);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "0x770(Hi-pri rx/tx)",
		   coex_sta->high_priority_rx, coex_sta->high_priority_tx);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d %s",
		   "0x774(Lo-pri rx/tx)",
		   coex_sta->low_priority_rx, coex_sta->low_priority_tx,
		   (bt_link_info->slave_role ? "(Slave!!)" : (
		   coex_sta->is_tdma_btautoslot_hang ? "(auto-slot hang!!)" : "")));
	CL_PRINTF(cli_buf);

	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_COEX_STATISTICS);
}


void ex_halbtc8821c1ant_ips_notify(IN struct btc_coexist *btcoexist, IN u8 type)
{
	if (btcoexist->manual_control ||	btcoexist->stop_coex_dm)
		return;

	if (type == BTC_IPS_ENTER) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], IPS ENTER notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_ips = TRUE;

		/* Write WL "Active" in Score-board for LPS off */
		halbtc8821c1ant_post_state_to_bt(btcoexist,
				BT_8821C_1ANT_SCOREBOARD_ACTIVE |
				BT_8821C_1ANT_SCOREBOARD_ONOFF |
				BT_8821C_1ANT_SCOREBOARD_SCAN |
				BT_8821C_1ANT_SCOREBOARD_UNDERTEST,
				FALSE);

		halbtc8821c1ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO,
					     FORCE_EXEC,
					     BT_8821C_1ANT_PHASE_WLAN_OFF);
		halbtc8821c1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
		halbtc8821c1ant_ps_tdma(btcoexist, NORMAL_EXEC, FALSE, 0);
	} else if (type == BTC_IPS_LEAVE) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], IPS LEAVE notify\n");
		BTC_TRACE(trace_buf);
#if 0
		halbtc8821c1ant_post_state_to_bt(btcoexist,
				BT_8821C_1ANT_SCOREBOARD_ACTIVE, TRUE);

		halbtc8821c1ant_post_state_to_bt(btcoexist,
				BT_8821C_1ANT_SCOREBOARD_ONOFF, TRUE);
#endif
		halbtc8821c1ant_init_hw_config(btcoexist, FALSE, FALSE);
		halbtc8821c1ant_init_coex_dm(btcoexist);

		coex_sta->under_ips = FALSE;
	}
}

void ex_halbtc8821c1ant_lps_notify(IN struct btc_coexist *btcoexist, IN u8 type)
{
	static boolean  pre_force_lps_on = FALSE;

	if (btcoexist->manual_control || btcoexist->stop_coex_dm)
		return;

	if (type == BTC_LPS_ENABLE) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], LPS ENABLE notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_lps = TRUE;

		if (coex_sta->force_lps_ctrl == TRUE) { /* LPS No-32K */
			/* Write WL "Active" in Score-board for PS-TDMA */
			pre_force_lps_on = TRUE;
			halbtc8821c1ant_post_state_to_bt(btcoexist,
					 BT_8821C_1ANT_SCOREBOARD_ACTIVE, TRUE);
		} else { /* LPS-32K, need check if this h2c 0x71 can work?? (2015/08/28) */
			/* Write WL "Non-Active" in Score-board for Native-PS */
			pre_force_lps_on = FALSE;
			halbtc8821c1ant_post_state_to_bt(btcoexist,
				 BT_8821C_1ANT_SCOREBOARD_ACTIVE, FALSE);

			halbtc8821c1ant_action_wifi_native_lps(btcoexist);
		}
	} else if (type == BTC_LPS_DISABLE) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], LPS DISABLE notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_lps = FALSE;

		/* Write WL "Active" in Score-board for LPS off */
		halbtc8821c1ant_post_state_to_bt(btcoexist,
				BT_8821C_1ANT_SCOREBOARD_ACTIVE, TRUE);

		if ((!pre_force_lps_on) && (!coex_sta->force_lps_ctrl))
			halbtc8821c1ant_query_bt_info(btcoexist);
	}
}

void ex_halbtc8821c1ant_scan_notify(IN struct btc_coexist *btcoexist,
				    IN u8 type)
{
	boolean wifi_connected = FALSE;
	boolean wifi_under_5g = FALSE;

	if (btcoexist->manual_control ||
	    btcoexist->stop_coex_dm)
		return;

	coex_sta->freeze_coexrun_by_btinfo = FALSE;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED, &wifi_connected);

	if (wifi_connected)
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], ********** WL connected before SCAN\n");
	else
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], **********  WL is not connected before SCAN\n");

	BTC_TRACE(trace_buf);

	if ((type == BTC_SCAN_START) || (type == BTC_SCAN_START_2G)) {

		halbtc8821c1ant_post_state_to_bt(btcoexist,
					BT_8821C_1ANT_SCOREBOARD_ACTIVE |
					BT_8821C_1ANT_SCOREBOARD_SCAN |
					BT_8821C_1ANT_SCOREBOARD_ONOFF,
					TRUE);

		halbtc8821c1ant_query_bt_info(btcoexist);
	}


	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);

	if ((type == BTC_SCAN_START) && (wifi_under_5g)) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], SCAN START notify (5G)\n");
		BTC_TRACE(trace_buf);

		halbtc8821c1ant_action_wifi_under5g(btcoexist);
	} else if ((type == BTC_SCAN_START_2G) || (type == BTC_SCAN_START)) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], SCAN START notify (2G)\n");
		BTC_TRACE(trace_buf);

		if (!wifi_connected)
			coex_sta->wifi_is_high_pri_task = TRUE;

		/* Force antenna setup for no scan result issue */
		halbtc8821c1ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO,
					     FORCE_EXEC,
					     BT_8821C_1ANT_PHASE_2G_RUNTIME);

		halbtc8821c1ant_run_coexist_mechanism(btcoexist);
	} else {

		btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM,
				   &coex_sta->scan_ap_num);

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], SCAN FINISH notify  (Scan-AP = %d)\n",
			    coex_sta->scan_ap_num);
		BTC_TRACE(trace_buf);

		coex_sta->wifi_is_high_pri_task = FALSE;

		halbtc8821c1ant_run_coexist_mechanism(btcoexist);
	}

}

void ex_halbtc8821c1ant_switchband_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{

	boolean wifi_connected = FALSE, bt_hs_on = FALSE;
	u32	wifi_link_status = 0;
	u32	num_of_wifi_link = 0;
	boolean	bt_ctrl_agg_buf_size = FALSE;
	u8	agg_buf_size = 5;


	if (btcoexist->manual_control ||
	    btcoexist->stop_coex_dm)
		return;

	coex_sta->switch_band_notify_to = type;

	if (type == BTC_SWITCH_TO_5G) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], switchband_notify ---  switch to 5G\n");
		BTC_TRACE(trace_buf);

		halbtc8821c1ant_action_wifi_under5g(btcoexist);

	} else if (type == BTC_SWITCH_TO_24G_NOFORSCAN) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], switchband_notify --- BTC_SWITCH_TO_2G (no for scan)\n");
		BTC_TRACE(trace_buf);

		halbtc8821c1ant_run_coexist_mechanism(btcoexist);

	} else {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], switchband_notify ---  switch to 2G\n");
		BTC_TRACE(trace_buf);

		ex_halbtc8821c1ant_scan_notify(btcoexist,
					       BTC_SCAN_START_2G);
	}

	coex_sta->switch_band_notify_to = BTC_NOT_SWITCH;
}


void ex_halbtc8821c1ant_connect_notify(IN struct btc_coexist *btcoexist,
				       IN u8 type)
{

	if (btcoexist->manual_control ||
	    btcoexist->stop_coex_dm)
		return;

	halbtc8821c1ant_post_state_to_bt(btcoexist,
					 BT_8821C_1ANT_SCOREBOARD_ACTIVE |
					 BT_8821C_1ANT_SCOREBOARD_SCAN |
					 BT_8821C_1ANT_SCOREBOARD_ONOFF,
					 TRUE);

	if ((type == BTC_ASSOCIATE_5G_START) ||
	    (type == BTC_ASSOCIATE_5G_FINISH)) {

		if (type == BTC_ASSOCIATE_5G_START)
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], connect_notify ---  5G start\n");
		else
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], connect_notify ---  5G finish\n");

		BTC_TRACE(trace_buf);

		halbtc8821c1ant_action_wifi_under5g(btcoexist);
	} else if (type == BTC_ASSOCIATE_START) {

		coex_sta->wifi_is_high_pri_task = TRUE;
		coex_dm->arp_cnt = 0;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], CONNECT START notify (2G)\n");
		BTC_TRACE(trace_buf);

		/* Force antenna setup for no scan result issue */
		halbtc8821c1ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO,
					     FORCE_EXEC,
					     BT_8821C_1ANT_PHASE_2G_RUNTIME);

		halbtc8821c1ant_run_coexist_mechanism(btcoexist);

		/* To keep TDMA case during connect process,
		to avoid changed by Btinfo and runcoexmechanism */
		coex_sta->freeze_coexrun_by_btinfo = TRUE;
	} else {

		coex_sta->wifi_is_high_pri_task = FALSE;
		coex_sta->freeze_coexrun_by_btinfo = FALSE;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], CONNECT FINISH notify (2G)\n");
		BTC_TRACE(trace_buf);

		halbtc8821c1ant_run_coexist_mechanism(btcoexist);
	}

}

void ex_halbtc8821c1ant_media_status_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	boolean			wifi_under_b_mode = FALSE, wifi_under_5g = FALSE;


	if (btcoexist->manual_control ||
	    btcoexist->stop_coex_dm)
		return;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);

	if (type == BTC_MEDIA_CONNECT) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], MEDIA connect notify\n");
		BTC_TRACE(trace_buf);

		halbtc8821c1ant_post_state_to_bt(btcoexist,
					 BT_8821C_1ANT_SCOREBOARD_ACTIVE |
					 BT_8821C_1ANT_SCOREBOARD_ONOFF,
					 TRUE);

		if (wifi_under_5g) {

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], WiFi is under 5G!!!\n");
			BTC_TRACE(trace_buf);

			halbtc8821c1ant_action_wifi_under5g(btcoexist);
		} else {

			/* Force antenna setup for no scan result issue */
			halbtc8821c1ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO,
						     FORCE_EXEC,
						     BT_8821C_1ANT_PHASE_2G_RUNTIME);

			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_B_MODE,
					   &wifi_under_b_mode);

			/* Set CCK Tx/Rx high Pri except 11b mode */
			if (wifi_under_b_mode) {
				btcoexist->btc_write_1byte(btcoexist, 0x6cd, 0x00); /* CCK Tx */
				btcoexist->btc_write_1byte(btcoexist, 0x6cf, 0x00); /* CCK Rx */
			} else {
				btcoexist->btc_write_1byte(btcoexist, 0x6cd, 0x00); /* CCK Tx */
				btcoexist->btc_write_1byte(btcoexist, 0x6cf, 0x10); /* CCK Rx */
			}
		}
	} else {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], MEDIA disconnect notify\n");
		BTC_TRACE(trace_buf);

		halbtc8821c1ant_post_state_to_bt(btcoexist,
				 BT_8821C_1ANT_SCOREBOARD_ACTIVE, FALSE);

		btcoexist->btc_write_1byte(btcoexist, 0x6cd, 0x0); /* CCK Tx */
		btcoexist->btc_write_1byte(btcoexist, 0x6cf, 0x0); /* CCK Rx */

		coex_sta->cck_lock_ever = FALSE;
		coex_sta->cck_lock_warn = FALSE;
		coex_sta->cck_lock = FALSE;
	}

	halbtc8821c1ant_update_wifi_channel_info(btcoexist, type);

}

void ex_halbtc8821c1ant_specific_packet_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	boolean	under_4way = FALSE, wifi_under_5g = FALSE;

	if (btcoexist->manual_control ||
	    btcoexist->stop_coex_dm)
		return;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);

	if (wifi_under_5g) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], WiFi is under 5G!!!\n");
		BTC_TRACE(trace_buf);

		halbtc8821c1ant_action_wifi_under5g(btcoexist);
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS, &under_4way);

	if (under_4way) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], specific Packet ---- under_4way!!\n");
		BTC_TRACE(trace_buf);

		coex_sta->wifi_is_high_pri_task = TRUE;
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

		coex_sta->wifi_is_high_pri_task = TRUE;
		coex_sta->specific_pkt_period_cnt = 2;
	}

	if (coex_sta->wifi_is_high_pri_task) {
		halbtc8821c1ant_post_state_to_bt(btcoexist,
					 BT_8821C_1ANT_SCOREBOARD_SCAN, TRUE);
		halbtc8821c1ant_run_coexist_mechanism(btcoexist);
	}

}

void ex_halbtc8821c1ant_bt_info_notify(IN struct btc_coexist *btcoexist,
				       IN u8 *tmp_buf, IN u8 length)
{
	u8				i, rsp_source = 0;
	boolean				wifi_connected = FALSE;
	boolean	wifi_scan = FALSE, wifi_link = FALSE, wifi_roam = FALSE,
			wifi_busy = FALSE;
	static boolean is_scoreboard_scan = FALSE;

	if (psd_scan->is_AntDet_running == TRUE) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], bt_info_notify return for AntDet is running\n");
		BTC_TRACE(trace_buf);
		return;
	}

	rsp_source = tmp_buf[0] & 0xf;
	if (rsp_source >= BT_INFO_SRC_8821C_1ANT_MAX)
		rsp_source = BT_INFO_SRC_8821C_1ANT_WIFI_FW;
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

	if (BT_INFO_SRC_8821C_1ANT_WIFI_FW != rsp_source) {

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
			  BT_INFO_8821C_1ANT_B_INQ_PAGE) ? TRUE : FALSE);

		coex_sta->a2dp_bit_pool = (((
			coex_sta->bt_info_c2h[rsp_source][1] & 0x49) == 0x49) ?
				   (coex_sta->bt_info_c2h[rsp_source][6] & 0x7f) : 0);

		coex_sta->is_bt_a2dp_sink = (coex_sta->bt_info_c2h[rsp_source][6] & 0x80) ?
									TRUE : FALSE;

		coex_sta->bt_retry_cnt = coex_sta->bt_info_c2h[rsp_source][2] &
					 0xf;

		coex_sta->is_autoslot = coex_sta->bt_info_ext2 & 0x8;

		coex_sta->forbidden_slot = coex_sta->bt_info_ext2 & 0x7;

		coex_sta->hid_busy_num = (coex_sta->bt_info_ext2 & 0x30) >> 4;

		coex_sta->hid_pair_cnt = (coex_sta->bt_info_ext2 & 0xc0) >> 6;

		if (coex_sta->bt_retry_cnt >= 1)
			coex_sta->pop_event_cnt++;

		if (coex_sta->c2h_bt_remote_name_req)
			coex_sta->cnt_RemoteNameReq++;

		if (coex_sta->bt_info_ext & BIT(1))
			coex_sta->cnt_ReInit++;

		if (coex_sta->bt_info_ext & BIT(2)) {
			coex_sta->cnt_setupLink++;
			coex_sta->is_setupLink = TRUE;
			coex_sta->bt_relink_downcount = 2;
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Re-Link start in BT info!!\n");
			BTC_TRACE(trace_buf);
		} else {
			coex_sta->is_setupLink = FALSE;
			coex_sta->bt_relink_downcount = 0;
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Re-Link stop in BT info!!\n");
			BTC_TRACE(trace_buf);
		}

		if (coex_sta->bt_info_ext & BIT(3))
			coex_sta->cnt_IgnWlanAct++;

		if (coex_sta->bt_info_ext & BIT(6))
			coex_sta->cnt_RoleSwitch++;

		if (coex_sta->bt_info_ext & BIT(7))
			coex_sta->is_bt_multi_link = TRUE;
		else
			coex_sta->is_bt_multi_link = FALSE;

		if (coex_sta->bt_create_connection) {
			coex_sta->cnt_Page++;

			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &wifi_scan);
			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &wifi_link);
			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &wifi_roam);

			if ((wifi_link) || (wifi_roam) || (wifi_scan) ||
			    (coex_sta->wifi_is_high_pri_task) || (wifi_busy)) {

				is_scoreboard_scan = TRUE;
				halbtc8821c1ant_post_state_to_bt(btcoexist,
					 BT_8821C_1ANT_SCOREBOARD_SCAN, TRUE);

			} else
				halbtc8821c1ant_post_state_to_bt(btcoexist,
					 BT_8821C_1ANT_SCOREBOARD_SCAN, FALSE);

		} else {
				if (is_scoreboard_scan) {
					halbtc8821c1ant_post_state_to_bt(btcoexist,
						 BT_8821C_1ANT_SCOREBOARD_SCAN, FALSE);
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
					halbtc8821c1ant_update_wifi_channel_info(
						btcoexist, BTC_MEDIA_CONNECT);
				else
					halbtc8821c1ant_update_wifi_channel_info(
						btcoexist,
						BTC_MEDIA_DISCONNECT);
			}

			/*  If Ignore_WLanAct && not SetUp_Link */
			if ((coex_sta->bt_info_ext & BIT(3)) &&
			    (!(coex_sta->bt_info_ext & BIT(2)))) {

				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], BT ext info bit3 check, set BT NOT to ignore Wlan active!!\n");
				BTC_TRACE(trace_buf);
				halbtc8821c1ant_ignore_wlan_act(btcoexist,
							FORCE_EXEC, FALSE);
			}
		}

	}

	if ((coex_sta->bt_info_ext & BIT(5))) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], BT ext info bit4 check, query BLE Scan type!!\n");
		BTC_TRACE(trace_buf);
		coex_sta->bt_ble_scan_type = btcoexist->btc_get_ble_scan_type_from_bt(
						     btcoexist);

		if ((coex_sta->bt_ble_scan_type & 0x1) == 0x1)
			coex_sta->bt_ble_scan_para[0]  =
				btcoexist->btc_get_ble_scan_para_from_bt(btcoexist,
						0x1);
		if ((coex_sta->bt_ble_scan_type & 0x2) == 0x2)
			coex_sta->bt_ble_scan_para[1]  =
				btcoexist->btc_get_ble_scan_para_from_bt(btcoexist,
						0x2);
		if ((coex_sta->bt_ble_scan_type & 0x4) == 0x4)
			coex_sta->bt_ble_scan_para[2]  =
				btcoexist->btc_get_ble_scan_para_from_bt(btcoexist,
						0x4);

	}

	halbtc8821c1ant_update_bt_link_info(btcoexist);

	halbtc8821c1ant_run_coexist_mechanism(btcoexist);
}

void ex_halbtc8821c1ant_wl_fwdbginfo_notify(IN struct btc_coexist *btcoexist,
				       IN u8 *tmp_buf, IN u8 length)
{
	u8 i = 0;
	static u8 tmp_buf_pre[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], WiFi Fw Dbg info = %d %d %d %d %d %d (len = %d)\n",
			    tmp_buf[0], tmp_buf[1],
				tmp_buf[2], tmp_buf[3],
				tmp_buf[4], tmp_buf[5], length);
	BTC_TRACE(trace_buf);

	if (tmp_buf[0] == 0x8) {
		for (i = 1; i <= 5; i++) {
			coex_sta->wl_fw_dbg_info[i] =
				(tmp_buf[i] >= tmp_buf_pre[i]) ?
				(tmp_buf[i] - tmp_buf_pre[i]) :
				(255 - tmp_buf_pre[i] + tmp_buf[i]);

			tmp_buf_pre[i] = tmp_buf[i];
		}
	}
}


void ex_halbtc8821c1ant_rx_rate_change_notify(IN struct btc_coexist *btcoexist,
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

	if ((wifi_connected) &&
		((coex_dm->bt_status ==  BT_8821C_1ANT_BT_STATUS_ACL_BUSY) ||
		(coex_dm->bt_status ==  BT_8821C_1ANT_BT_STATUS_ACL_SCO_BUSY) ||
		(coex_dm->bt_status == BT_8821C_1ANT_BT_STATUS_SCO_BUSY))) {

		if ((coex_sta->wl_rx_rate == BTC_CCK_5_5) ||
			(coex_sta->wl_rx_rate == BTC_OFDM_6) ||
			(coex_sta->wl_rx_rate == BTC_MCS_0)) {

			coex_sta->cck_lock_warn = TRUE;

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], cck lock warning...\n");
			BTC_TRACE(trace_buf);
		} else if ((coex_sta->wl_rx_rate == BTC_CCK_1) ||
			(coex_sta->wl_rx_rate == BTC_CCK_2) ||
			(coex_sta->wl_rts_rx_rate == BTC_CCK_1) ||
			(coex_sta->wl_rts_rx_rate == BTC_CCK_2)) {

			coex_sta->cck_lock = TRUE;

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
		if ((coex_dm->bt_status ==
			BT_8821C_1ANT_BT_STATUS_CONNECTED_IDLE) ||
			(coex_dm->bt_status ==
			BT_8821C_1ANT_BT_STATUS_NON_CONNECTED_IDLE)) {
			coex_sta->cck_lock_warn = FALSE;
			coex_sta->cck_lock = FALSE;
		}
	}

}

void ex_halbtc8821c1ant_rf_status_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], RF Status notify\n");
	BTC_TRACE(trace_buf);

	if (type == BTC_RF_ON) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RF is turned ON!!\n");
		BTC_TRACE(trace_buf);

		btcoexist->stop_coex_dm = FALSE;
		coex_sta->is_rf_state_off = FALSE;
#if 0
		halbtc8821c1ant_post_state_to_bt(btcoexist,
					 BT_8821C_1ANT_SCOREBOARD_ACTIVE |
					 BT_8821C_1ANT_SCOREBOARD_ONOFF,
					 TRUE);
#endif
	} else if (type == BTC_RF_OFF) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], RF is turned OFF!!\n");
		BTC_TRACE(trace_buf);

		halbtc8821c1ant_post_state_to_bt(btcoexist,
				BT_8821C_1ANT_SCOREBOARD_ACTIVE |
				BT_8821C_1ANT_SCOREBOARD_ONOFF |
				BT_8821C_1ANT_SCOREBOARD_SCAN |
				BT_8821C_1ANT_SCOREBOARD_UNDERTEST,
				FALSE);

		halbtc8821c1ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO,
					     FORCE_EXEC,
					     BT_8821C_1ANT_PHASE_WLAN_OFF);
		halbtc8821c1ant_ps_tdma(btcoexist, FORCE_EXEC, FALSE, 0);

		btcoexist->stop_coex_dm = TRUE;
		coex_sta->is_rf_state_off = TRUE;
	}
}

void ex_halbtc8821c1ant_halt_notify(IN struct btc_coexist *btcoexist)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Halt notify\n");
	BTC_TRACE(trace_buf);

	halbtc8821c1ant_post_state_to_bt(btcoexist,
				BT_8821C_1ANT_SCOREBOARD_ACTIVE |
				BT_8821C_1ANT_SCOREBOARD_ONOFF |
				BT_8821C_1ANT_SCOREBOARD_SCAN |
				BT_8821C_1ANT_SCOREBOARD_UNDERTEST,
				FALSE);

	halbtc8821c1ant_set_ant_path(btcoexist, BTC_ANT_PATH_AUTO, FORCE_EXEC,
				     BT_8821C_1ANT_PHASE_WLAN_OFF);

	ex_halbtc8821c1ant_media_status_notify(btcoexist, BTC_MEDIA_DISCONNECT);

	halbtc8821c1ant_ps_tdma(btcoexist, FORCE_EXEC, FALSE, 0);

	btcoexist->stop_coex_dm = TRUE;
}

void ex_halbtc8821c1ant_pnp_notify(IN struct btc_coexist *btcoexist,
				   IN u8 pnp_state)
{
	boolean wifi_under_5g = FALSE;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Pnp notify\n");
	BTC_TRACE(trace_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);

	if ((BTC_WIFI_PNP_SLEEP == pnp_state) ||
	    (BTC_WIFI_PNP_SLEEP_KEEP_ANT == pnp_state)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Pnp notify to SLEEP\n");
		BTC_TRACE(trace_buf);

		halbtc8821c1ant_post_state_to_bt(btcoexist,
				BT_8821C_1ANT_SCOREBOARD_ACTIVE |
				BT_8821C_1ANT_SCOREBOARD_ONOFF |
				BT_8821C_1ANT_SCOREBOARD_SCAN |
				BT_8821C_1ANT_SCOREBOARD_UNDERTEST,
				FALSE);

		if (BTC_WIFI_PNP_SLEEP_KEEP_ANT == pnp_state) {

			if (wifi_under_5g)
				halbtc8821c1ant_set_ant_path(btcoexist,
						BTC_ANT_PATH_AUTO, FORCE_EXEC,
						BT_8821C_1ANT_PHASE_5G_RUNTIME);
			else
				halbtc8821c1ant_set_ant_path(btcoexist,
						BTC_ANT_PATH_AUTO, FORCE_EXEC,
						BT_8821C_1ANT_PHASE_2G_RUNTIME);
		} else {

			halbtc8821c1ant_set_ant_path(btcoexist,
						BTC_ANT_PATH_AUTO,
						FORCE_EXEC,
						BT_8821C_1ANT_PHASE_WLAN_OFF);
		}

		btcoexist->stop_coex_dm = TRUE;
	} else if (BTC_WIFI_PNP_WAKE_UP == pnp_state) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Pnp notify to WAKE UP\n");
		BTC_TRACE(trace_buf);
#if 0
		halbtc8821c1ant_post_state_to_bt(btcoexist,
					 BT_8821C_1ANT_SCOREBOARD_ACTIVE |
					 BT_8821C_1ANT_SCOREBOARD_ONOFF,
					 TRUE);
#endif
		btcoexist->stop_coex_dm = FALSE;
	}
}


void ex_halbtc8821c1ant_coex_dm_reset(IN struct btc_coexist *btcoexist)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], *****************Coex DM Reset*****************\n");
	BTC_TRACE(trace_buf);

	halbtc8821c1ant_init_hw_config(btcoexist, FALSE, FALSE);
	halbtc8821c1ant_init_coex_dm(btcoexist);
}

void ex_halbtc8821c1ant_periodical(IN struct btc_coexist *btcoexist)
{

	struct  btc_board_info	*board_info = &btcoexist->board_info;
	boolean wifi_busy = FALSE;
	u16 bt_scoreboard_val = 0;
	boolean bt_relink_finish = FALSE;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], ************* Periodical *************\n");
	BTC_TRACE(trace_buf);

#if (BT_AUTO_REPORT_ONLY_8821C_1ANT == 0)
	halbtc8821c1ant_query_bt_info(btcoexist);

#endif

	halbtc8821c1ant_monitor_bt_ctr(btcoexist);
	halbtc8821c1ant_monitor_wifi_ctr(btcoexist);

	halbtc8821c1ant_monitor_bt_enable_disable(btcoexist);

#if 0
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	/* halbtc8821c1ant_read_score_board(btcoexist, &bt_scoreboard_val); */

	if (wifi_busy) {
		halbtc8821c1ant_post_state_to_bt(btcoexist,
				BT_8821C_1ANT_SCOREBOARD_UNDERTEST, TRUE);
		/*
		halbtc8821c1ant_post_state_to_bt(btcoexist,
					 BT_8821C_1ANT_SCOREBOARD_WLBUSY, TRUE);

		if (bt_scoreboard_val & BIT(6))
			halbtc8821c1ant_query_bt_info(btcoexist); */
	} else {
		halbtc8821c1ant_post_state_to_bt(btcoexist,
					BT_8821C_1ANT_SCOREBOARD_UNDERTEST, FALSE);
	}
#endif

	if (coex_sta->bt_relink_downcount != 0) {
		coex_sta->bt_relink_downcount--;

		if (coex_sta->bt_relink_downcount == 0) {
			coex_sta->is_setupLink = FALSE;
			bt_relink_finish = TRUE;

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Re-Link stop by periodical count-down!!\n");
			BTC_TRACE(trace_buf);
		}
	}

	/* for 4-way, DHCP, EAPOL packet */
	if (coex_sta->specific_pkt_period_cnt > 0) {

		coex_sta->specific_pkt_period_cnt--;

		if ((coex_sta->specific_pkt_period_cnt == 0) &&
		    (coex_sta->wifi_is_high_pri_task))
			coex_sta->wifi_is_high_pri_task = FALSE;

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], ***************** Hi-Pri Task = %s\n",
			    (coex_sta->wifi_is_high_pri_task ? "Yes" :
			     "No"));
		BTC_TRACE(trace_buf);

	}

	if (halbtc8821c1ant_is_wifibt_status_changed(btcoexist) || (bt_relink_finish)
		|| (coex_sta->is_set_ps_state_fail))
		halbtc8821c1ant_run_coexist_mechanism(btcoexist);

}

/*#pragma optimize( "", off )*/
void ex_halbtc8821c1ant_antenna_detection(IN struct btc_coexist *btcoexist,
		IN u32 cent_freq, IN u32 offset, IN u32 span, IN u32 seconds)
{

}


void ex_halbtc8821c1ant_display_ant_detection(IN struct btc_coexist *btcoexist)
{

}

void ex_halbtc8821c1ant_antenna_isolation(IN struct btc_coexist *btcoexist,
		IN u32 cent_freq, IN u32 offset, IN u32 span, IN u32 seconds)
{


}

void ex_halbtc8821c1ant_psd_scan(IN struct btc_coexist *btcoexist,
		 IN u32 cent_freq, IN u32 offset, IN u32 span, IN u32 seconds)
{


}


#endif

#endif	/* #if (BT_SUPPORT == 1 && COEX_SUPPORT == 1) */


