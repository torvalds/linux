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
static struct  coex_dm_8723b_2ant		glcoex_dm_8723b_2ant;
static struct  coex_dm_8723b_2ant	*coex_dm = &glcoex_dm_8723b_2ant;
static struct  coex_sta_8723b_2ant		glcoex_sta_8723b_2ant;
static struct  coex_sta_8723b_2ant	*coex_sta = &glcoex_sta_8723b_2ant;

const char *const glbt_info_src_8723b_2ant[] = {
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

u32	glcoex_ver_date_8723b_2ant = 20151223;
u32	glcoex_ver_8723b_2ant = 0x4a;

/* ************************************************************
 * local function proto type if needed
 * ************************************************************
 * ************************************************************
 * local function start with halbtc8723b2ant_
 * ************************************************************ */
u8 halbtc8723b2ant_bt_rssi_state(u8 *ppre_bt_rssi_state, u8 level_num,
				 u8 rssi_thresh, u8 rssi_thresh1)
{
	s32			bt_rssi = 0;
	u8			bt_rssi_state = *ppre_bt_rssi_state;

	bt_rssi = coex_sta->bt_rssi;

	if (level_num == 2) {
		if ((*ppre_bt_rssi_state == BTC_RSSI_STATE_LOW) ||
		    (*ppre_bt_rssi_state == BTC_RSSI_STATE_STAY_LOW)) {
			if (bt_rssi >= (rssi_thresh +
					BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT))
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
					BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT))
				bt_rssi_state = BTC_RSSI_STATE_MEDIUM;
			else
				bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
		} else if ((*ppre_bt_rssi_state == BTC_RSSI_STATE_MEDIUM) ||
			(*ppre_bt_rssi_state == BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (bt_rssi >= (rssi_thresh1 +
					BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT))
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

u8 halbtc8723b2ant_wifi_rssi_state(IN struct btc_coexist *btcoexist,
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
					  BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT))
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
					  BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT))
				wifi_rssi_state = BTC_RSSI_STATE_MEDIUM;
			else
				wifi_rssi_state = BTC_RSSI_STATE_STAY_LOW;
		} else if ((*pprewifi_rssi_state == BTC_RSSI_STATE_MEDIUM) ||
			(*pprewifi_rssi_state == BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (wifi_rssi >= (rssi_thresh1 +
					  BTC_RSSI_COEX_THRESH_TOL_8723B_2ANT))
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

void halbtc8723b2ant_monitor_bt_enable_disable(IN struct btc_coexist *btcoexist)
{
	static u32		bt_disable_cnt = 0;
	boolean		bt_active = true, bt_disabled = false;

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
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], bt all counters=0, %d times!!\n",
			    bt_disable_cnt);
		BTC_TRACE(trace_buf);
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
			btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS,
					   NULL);
			btcoexist->btc_set(btcoexist, BTC_SET_ACT_NORMAL_LPS,
					   NULL);
		}
	}
}


void halbtc8723b2ant_limited_rx(IN struct btc_coexist *btcoexist,
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

void halbtc8723b2ant_monitor_bt_ctr(IN struct btc_coexist *btcoexist)
{
	u32			reg_hp_txrx, reg_lp_txrx, u32tmp;
	u32			reg_hp_tx = 0, reg_hp_rx = 0, reg_lp_tx = 0, reg_lp_rx = 0;

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

	if ((coex_sta->low_priority_tx > 1050)  &&
	    (!coex_sta->c2h_bt_inquiry_page))
		coex_sta->pop_event_cnt++;

	if ((coex_sta->low_priority_rx >= 950)  &&
	    (coex_sta->low_priority_rx >= coex_sta->low_priority_tx) &&
	    (!coex_sta->under_ips))
		bt_link_info->slave_role = true;
	else
		bt_link_info->slave_role = false;

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

void halbtc8723b2ant_monitor_wifi_ctr(IN struct btc_coexist *btcoexist)
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
}

void halbtc8723b2ant_query_bt_info(IN struct btc_coexist *btcoexist)
{
	u8			h2c_parameter[1] = {0};

	coex_sta->c2h_bt_info_req_sent = true;

	h2c_parameter[0] |= BIT(0);	/* trigger */

	btcoexist->btc_fill_h2c(btcoexist, 0x61, 1, h2c_parameter);
}

boolean halbtc8723b2ant_is_wifi_status_changed(IN struct btc_coexist *btcoexist)
{
	static boolean	pre_wifi_busy = false, pre_under_4way = false,
			pre_bt_hs_on = false;
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW;
	boolean			wifi_busy = false, under_4way = false, bt_hs_on = false;
	boolean			wifi_connected = false;
	u8			wifi_rssi_state = BTC_RSSI_STATE_HIGH;


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


		wifi_rssi_state = halbtc8723b2ant_wifi_rssi_state(btcoexist,
				  &prewifi_rssi_state, 2,
				  BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES -
				  coex_dm->switch_thres_offset, 0);

		if ((BTC_RSSI_STATE_HIGH == wifi_rssi_state) ||
		    (BTC_RSSI_STATE_LOW == wifi_rssi_state))
			return true;

	}

	return false;
}

void halbtc8723b2ant_update_bt_link_info(IN struct btc_coexist *btcoexist)
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

u8 halbtc8723b2ant_action_algorithm(IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info	*bt_link_info = &btcoexist->bt_link_info;
	boolean				bt_hs_on = false;
	u8				algorithm = BT_8723B_2ANT_COEX_ALGO_UNDEFINED;
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
			algorithm = BT_8723B_2ANT_COEX_ALGO_SCO;
		} else {
			if (bt_link_info->hid_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					    "[BTCoex], HID only\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8723B_2ANT_COEX_ALGO_HID;
			} else if (bt_link_info->a2dp_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					    "[BTCoex], A2DP only\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8723B_2ANT_COEX_ALGO_A2DP;
			} else if (bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						    "[BTCoex], PAN(HS) only\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723B_2ANT_COEX_ALGO_PANHS;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], PAN(EDR) only\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723B_2ANT_COEX_ALGO_PANEDR;
				}
			}
		}
	} else if (num_of_diff_profile == 2) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					    "[BTCoex], SCO + HID\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
			} else if (bt_link_info->a2dp_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					    "[BTCoex], SCO + A2DP ==> SCO\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
			} else if (bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm = BT_8723B_2ANT_COEX_ALGO_SCO;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
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
						BT_8723B_2ANT_COEX_ALGO_HID_A2DP;
				}
			} else if (bt_link_info->hid_exist &&
				   bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], HID + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm = BT_8723B_2ANT_COEX_ALGO_HID;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], HID + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (bt_link_info->pan_exist &&
				   bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], A2DP + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723B_2ANT_COEX_ALGO_A2DP_PANHS;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], A2DP + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723B_2ANT_COEX_ALGO_PANEDR_A2DP;
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
				algorithm = BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
			} else if (bt_link_info->hid_exist &&
				   bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + HID + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + HID + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (bt_link_info->pan_exist &&
				   bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + A2DP + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + A2DP + PAN(EDR) ==> HID\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
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
						BT_8723B_2ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], HID + A2DP + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8723B_2ANT_COEX_ALGO_HID_A2DP_PANEDR;
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
						BT_8723B_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
	}

	return algorithm;
}

void halbtc8723b2ant_set_fw_dac_swing_level(IN struct btc_coexist *btcoexist,
		IN u8 dac_swing_lvl)
{
	u8			h2c_parameter[1] = {0};

	/* There are several type of dacswing */
	/* 0x18/ 0x10/ 0xc/ 0x8/ 0x4/ 0x6 */
	h2c_parameter[0] = dac_swing_lvl;

	btcoexist->btc_fill_h2c(btcoexist, 0x64, 1, h2c_parameter);
}

void halbtc8723b2ant_set_fw_dec_bt_pwr(IN struct btc_coexist *btcoexist,
				       IN u8 dec_bt_pwr_lvl)
{
	u8			h2c_parameter[1] = {0};

	h2c_parameter[0] = dec_bt_pwr_lvl;

	btcoexist->btc_fill_h2c(btcoexist, 0x62, 1, h2c_parameter);
}

void halbtc8723b2ant_dec_bt_pwr(IN struct btc_coexist *btcoexist,
				IN boolean force_exec, IN u8 dec_bt_pwr_lvl)
{
	coex_dm->cur_bt_dec_pwr_lvl = dec_bt_pwr_lvl;

	if (!force_exec) {
		if (coex_dm->pre_bt_dec_pwr_lvl == coex_dm->cur_bt_dec_pwr_lvl)
			return;
	}
	halbtc8723b2ant_set_fw_dec_bt_pwr(btcoexist,
					  coex_dm->cur_bt_dec_pwr_lvl);

	coex_dm->pre_bt_dec_pwr_lvl = coex_dm->cur_bt_dec_pwr_lvl;
}

void halbtc8723b2ant_set_bt_auto_report(IN struct btc_coexist *btcoexist,
					IN boolean enable_auto_report)
{
	u8			h2c_parameter[1] = {0};

	h2c_parameter[0] = 0;

	if (enable_auto_report)
		h2c_parameter[0] |= BIT(0);

	btcoexist->btc_fill_h2c(btcoexist, 0x68, 1, h2c_parameter);
}

void halbtc8723b2ant_bt_auto_report(IN struct btc_coexist *btcoexist,
		    IN boolean force_exec, IN boolean enable_auto_report)
{
	coex_dm->cur_bt_auto_report = enable_auto_report;

	if (!force_exec) {
		if (coex_dm->pre_bt_auto_report == coex_dm->cur_bt_auto_report)
			return;
	}
	halbtc8723b2ant_set_bt_auto_report(btcoexist,
					   coex_dm->cur_bt_auto_report);

	coex_dm->pre_bt_auto_report = coex_dm->cur_bt_auto_report;
}

void halbtc8723b2ant_fw_dac_swing_lvl(IN struct btc_coexist *btcoexist,
			      IN boolean force_exec, IN u8 fw_dac_swing_lvl)
{
	coex_dm->cur_fw_dac_swing_lvl = fw_dac_swing_lvl;

	if (!force_exec) {
		if (coex_dm->pre_fw_dac_swing_lvl ==
		    coex_dm->cur_fw_dac_swing_lvl)
			return;
	}

	halbtc8723b2ant_set_fw_dac_swing_level(btcoexist,
					       coex_dm->cur_fw_dac_swing_lvl);

	coex_dm->pre_fw_dac_swing_lvl = coex_dm->cur_fw_dac_swing_lvl;
}

void halbtc8723b2ant_set_sw_rf_rx_lpf_corner(IN struct btc_coexist *btcoexist,
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

void halbtc8723b2ant_rf_shrink(IN struct btc_coexist *btcoexist,
		       IN boolean force_exec, IN boolean rx_rf_shrink_on)
{
	coex_dm->cur_rf_rx_lpf_shrink = rx_rf_shrink_on;

	if (!force_exec) {
		if (coex_dm->pre_rf_rx_lpf_shrink ==
		    coex_dm->cur_rf_rx_lpf_shrink)
			return;
	}
	halbtc8723b2ant_set_sw_rf_rx_lpf_corner(btcoexist,
						coex_dm->cur_rf_rx_lpf_shrink);

	coex_dm->pre_rf_rx_lpf_shrink = coex_dm->cur_rf_rx_lpf_shrink;
}

void halbtc8723b2ant_set_sw_penalty_tx_rate_adaptive(IN struct btc_coexist
		*btcoexist, IN boolean low_penalty_ra)
{
	u8			h2c_parameter[6] = {0};

	h2c_parameter[0] = 0x6;	/* op_code, 0x6= Retry_Penalty */

	if (low_penalty_ra) {
		h2c_parameter[1] |= BIT(0);
		h2c_parameter[2] =
			0x00;  /* normal rate except MCS7/6/5, OFDM54/48/36 */
		h2c_parameter[3] = 0xf4;  /* MCS7 or OFDM54 */
		h2c_parameter[4] = 0xf5;  /* MCS6 or OFDM48 */
		h2c_parameter[5] = 0xf6;	/* MCS5 or OFDM36	 */
	}

	btcoexist->btc_fill_h2c(btcoexist, 0x69, 6, h2c_parameter);
}

void halbtc8723b2ant_low_penalty_ra(IN struct btc_coexist *btcoexist,
			    IN boolean force_exec, IN boolean low_penalty_ra)
{
	coex_dm->cur_low_penalty_ra = low_penalty_ra;

	if (!force_exec) {
		if (coex_dm->pre_low_penalty_ra == coex_dm->cur_low_penalty_ra)
			return;
	}
	halbtc8723b2ant_set_sw_penalty_tx_rate_adaptive(btcoexist,
			coex_dm->cur_low_penalty_ra);

	coex_dm->pre_low_penalty_ra = coex_dm->cur_low_penalty_ra;
}

void halbtc8723b2ant_set_dac_swing_reg(IN struct btc_coexist *btcoexist,
				       IN u32 level)
{
	u8	val = (u8)level;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], Write SwDacSwing = 0x%x\n", level);
	BTC_TRACE(trace_buf);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x883, 0x3e, val);
}

void halbtc8723b2ant_set_sw_full_time_dac_swing(IN struct btc_coexist
		*btcoexist, IN boolean sw_dac_swing_on, IN u32 sw_dac_swing_lvl)
{
	if (sw_dac_swing_on)
		halbtc8723b2ant_set_dac_swing_reg(btcoexist, sw_dac_swing_lvl);
	else
		halbtc8723b2ant_set_dac_swing_reg(btcoexist, 0x18);
}


void halbtc8723b2ant_dac_swing(IN struct btc_coexist *btcoexist,
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
	halbtc8723b2ant_set_sw_full_time_dac_swing(btcoexist, dac_swing_on,
			dac_swing_lvl);

	coex_dm->pre_dac_swing_on = coex_dm->cur_dac_swing_on;
	coex_dm->pre_dac_swing_lvl = coex_dm->cur_dac_swing_lvl;
}

void halbtc8723b2ant_set_adc_back_off(IN struct btc_coexist *btcoexist,
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

void halbtc8723b2ant_adc_back_off(IN struct btc_coexist *btcoexist,
			  IN boolean force_exec, IN boolean adc_back_off)
{
	coex_dm->cur_adc_back_off = adc_back_off;

	if (!force_exec) {
		if (coex_dm->pre_adc_back_off == coex_dm->cur_adc_back_off)
			return;
	}
	halbtc8723b2ant_set_adc_back_off(btcoexist, coex_dm->cur_adc_back_off);

	coex_dm->pre_adc_back_off = coex_dm->cur_adc_back_off;
}

void halbtc8723b2ant_set_agc_table(IN struct btc_coexist *btcoexist,
				   IN boolean agc_table_en)
{
	u8		rssi_adjust_val = 0;

	/* =================BB AGC Gain Table */
	if (agc_table_en) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BB Agc Table On!\n");
		BTC_TRACE(trace_buf);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x6e1A0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x6d1B0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x6c1C0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x6b1D0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x6a1E0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x691F0001);
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0x68200001);
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
		btcoexist->btc_write_4byte(btcoexist, 0xc78, 0xa4200001);
	}


	/* =================RF Gain */
	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xef, 0xfffff, 0x02000);
	if (agc_table_en) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Agc Table On!\n");
		BTC_TRACE(trace_buf);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b, 0xfffff,
					  0x38fff);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b, 0xfffff,
					  0x38ffe);
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Agc Table Off!\n");
		BTC_TRACE(trace_buf);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b, 0xfffff,
					  0x380c3);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b, 0xfffff,
					  0x28ce6);
	}
	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xef, 0xfffff, 0x0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xed, 0xfffff, 0x1);
	if (agc_table_en) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Agc Table On!\n");
		BTC_TRACE(trace_buf);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x40, 0xfffff,
					  0x38fff);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x40, 0xfffff,
					  0x38ffe);
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Agc Table Off!\n");
		BTC_TRACE(trace_buf);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x40, 0xfffff,
					  0x380c3);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x40, 0xfffff,
					  0x28ce6);
	}
	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xed, 0xfffff, 0x0);

	/* set rssi_adjust_val for wifi module. */
	if (agc_table_en)
		rssi_adjust_val = 8;
	btcoexist->btc_set(btcoexist, BTC_SET_U1_RSSI_ADJ_VAL_FOR_AGC_TABLE_ON,
			   &rssi_adjust_val);
}

void halbtc8723b2ant_agc_table(IN struct btc_coexist *btcoexist,
			       IN boolean force_exec, IN boolean agc_table_en)
{
	coex_dm->cur_agc_table_en = agc_table_en;

	if (!force_exec) {
		if (coex_dm->pre_agc_table_en == coex_dm->cur_agc_table_en)
			return;
	}
	halbtc8723b2ant_set_agc_table(btcoexist, agc_table_en);

	coex_dm->pre_agc_table_en = coex_dm->cur_agc_table_en;
}

void halbtc8723b2ant_set_coex_table(IN struct btc_coexist *btcoexist,
	    IN u32 val0x6c0, IN u32 val0x6c4, IN u32 val0x6c8, IN u8 val0x6cc)
{
	btcoexist->btc_write_4byte(btcoexist, 0x6c0, val0x6c0);

	btcoexist->btc_write_4byte(btcoexist, 0x6c4, val0x6c4);

	btcoexist->btc_write_4byte(btcoexist, 0x6c8, val0x6c8);

	btcoexist->btc_write_1byte(btcoexist, 0x6cc, val0x6cc);
}

void halbtc8723b2ant_coex_table(IN struct btc_coexist *btcoexist,
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
	halbtc8723b2ant_set_coex_table(btcoexist, val0x6c0, val0x6c4, val0x6c8,
				       val0x6cc);

	coex_dm->pre_val0x6c0 = coex_dm->cur_val0x6c0;
	coex_dm->pre_val0x6c4 = coex_dm->cur_val0x6c4;
	coex_dm->pre_val0x6c8 = coex_dm->cur_val0x6c8;
	coex_dm->pre_val0x6cc = coex_dm->cur_val0x6cc;
}

void halbtc8723b2ant_coex_table_with_type(IN struct btc_coexist *btcoexist,
		IN boolean force_exec, IN u8 type)
{
	coex_sta->coex_table_type = type;

	switch (type) {
	case 0:
		halbtc8723b2ant_coex_table(btcoexist, force_exec,
				   0x55555555, 0x55555555, 0xffffff, 0x3);
		break;
	case 1:
		halbtc8723b2ant_coex_table(btcoexist, force_exec,
				   0x55555555, 0x5afa5afa, 0xffffff, 0x3);
		break;
	case 2:
		halbtc8723b2ant_coex_table(btcoexist, force_exec,
				   0x5ada5ada, 0x5ada5ada, 0xffffff, 0x3);
		break;
	case 3:
		halbtc8723b2ant_coex_table(btcoexist, force_exec,
				   0xaaaaaaaa, 0xaaaaaaaa, 0xffffff, 0x3);
		break;
	case 4:
		halbtc8723b2ant_coex_table(btcoexist, force_exec,
				   0xffffffff, 0xffffffff, 0xffffff, 0x3);
		break;
	case 5:
		halbtc8723b2ant_coex_table(btcoexist, force_exec,
				   0x5fff5fff, 0x5fff5fff, 0xffffff, 0x3);
		break;
	case 6:
		halbtc8723b2ant_coex_table(btcoexist, force_exec,
				   0x55ff55ff, 0x5a5a5a5a, 0xffffff, 0x3);
		break;
	case 7:
		halbtc8723b2ant_coex_table(btcoexist, force_exec,
				   0x55dd55dd, 0x5ada5ada, 0xffffff, 0x3);
		break;
	case 8:
		halbtc8723b2ant_coex_table(btcoexist, force_exec,
				   0x55dd55dd, 0x5ada5ada, 0xffffff, 0x3);
		break;
	case 9:
		halbtc8723b2ant_coex_table(btcoexist, force_exec,
				   0x55dd55dd, 0x5ada5ada, 0xffffff, 0x3);
		break;
	case 10:
		halbtc8723b2ant_coex_table(btcoexist, force_exec,
				   0x55dd55dd, 0x5ada5ada, 0xffffff, 0x3);
		break;
	case 11:
		halbtc8723b2ant_coex_table(btcoexist, force_exec,
				   0x55dd55dd, 0x5ada5ada, 0xffffff, 0x3);
		break;
	case 12:
		halbtc8723b2ant_coex_table(btcoexist, force_exec,
				   0x55dd55dd, 0x5ada5ada, 0xffffff, 0x3);
		break;
	case 13:
		halbtc8723b2ant_coex_table(btcoexist, force_exec,
				   0x5fff5fff, 0xaaaaaaaa, 0xffffff, 0x3);
		break;
	case 14:
		halbtc8723b2ant_coex_table(btcoexist, force_exec,
				   0x5fff5fff, 0x5ada5ada, 0xffffff, 0x3);
		break;
	case 15:
		halbtc8723b2ant_coex_table(btcoexist, force_exec,
				   0x55dd55dd, 0xaaaaaaaa, 0xffffff, 0x3);
		break;
	default:
		break;
	}
}

void halbtc8723b2ant_set_fw_ignore_wlan_act(IN struct btc_coexist *btcoexist,
		IN boolean enable)
{
	u8			h2c_parameter[1] = {0};

	if (enable) {
		h2c_parameter[0] |= BIT(0);		/* function enable */
	}

	btcoexist->btc_fill_h2c(btcoexist, 0x63, 1, h2c_parameter);
}

void halbtc8723b2ant_set_lps_rpwm(IN struct btc_coexist *btcoexist,
				  IN u8 lps_val, IN u8 rpwm_val)
{
	u8	lps = lps_val;
	u8	rpwm = rpwm_val;

	btcoexist->btc_set(btcoexist, BTC_SET_U1_LPS_VAL, &lps);
	btcoexist->btc_set(btcoexist, BTC_SET_U1_RPWM_VAL, &rpwm);
}

void halbtc8723b2ant_lps_rpwm(IN struct btc_coexist *btcoexist,
		      IN boolean force_exec, IN u8 lps_val, IN u8 rpwm_val)
{
	coex_dm->cur_lps = lps_val;
	coex_dm->cur_rpwm = rpwm_val;

	if (!force_exec) {
		if ((coex_dm->pre_lps == coex_dm->cur_lps) &&
		    (coex_dm->pre_rpwm == coex_dm->cur_rpwm))
			return;
	}
	halbtc8723b2ant_set_lps_rpwm(btcoexist, lps_val, rpwm_val);

	coex_dm->pre_lps = coex_dm->cur_lps;
	coex_dm->pre_rpwm = coex_dm->cur_rpwm;
}

void halbtc8723b2ant_ignore_wlan_act(IN struct btc_coexist *btcoexist,
				     IN boolean force_exec, IN boolean enable)
{
	coex_dm->cur_ignore_wlan_act = enable;

	if (!force_exec) {
		if (coex_dm->pre_ignore_wlan_act ==
		    coex_dm->cur_ignore_wlan_act)
			return;
	}
	halbtc8723b2ant_set_fw_ignore_wlan_act(btcoexist, enable);

	coex_dm->pre_ignore_wlan_act = coex_dm->cur_ignore_wlan_act;
}

void halbtc8723b2ant_set_fw_pstdma(IN struct btc_coexist *btcoexist,
	   IN u8 byte1, IN u8 byte2, IN u8 byte3, IN u8 byte4, IN u8 byte5)
{
	u8			h2c_parameter[5] = {0};


	if ((coex_sta->a2dp_exist) && (coex_sta->hid_exist))
		byte5 = byte5 | 0x1;

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

void halbtc8723b2ant_sw_mechanism1(IN struct btc_coexist *btcoexist,
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

	/* halbtc8723b2ant_rf_shrink(btcoexist, NORMAL_EXEC, shrink_rx_lpf); */
	halbtc8723b2ant_low_penalty_ra(btcoexist, NORMAL_EXEC, low_penalty_ra);
}

void halbtc8723b2ant_sw_mechanism2(IN struct btc_coexist *btcoexist,
			   IN boolean agc_table_shift, IN boolean adc_back_off,
			   IN boolean sw_dac_swing, IN u32 dac_swing_lvl)
{
	/* halbtc8723b2ant_agc_table(btcoexist, NORMAL_EXEC, agc_table_shift); */
	/* halbtc8723b2ant_adc_back_off(btcoexist, NORMAL_EXEC, adc_back_off); */
	/* halbtc8723b2ant_dac_swing(btcoexist, NORMAL_EXEC, sw_dac_swing, dac_swing_lvl); */
}

void halbtc8723b2ant_set_ant_path(IN struct btc_coexist *btcoexist,
	  IN u8 ant_pos_type, IN boolean init_hwcfg, IN boolean wifi_off)
{
	struct  btc_board_info *board_info = &btcoexist->board_info;
	PADAPTER 		pAdapter = btcoexist->Adapter;
	u32			fw_ver = 0, u32tmp = 0, cnt_bt_cal_chk = 0;
	boolean			pg_ext_switch = false;
	boolean			use_ext_switch = false;
	u8			h2c_parameter[2] = {0};
	u32				u32tmp_1[4];

	btcoexist->btc_get(btcoexist, BTC_GET_BL_EXT_SWITCH, &pg_ext_switch);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER,
			   &fw_ver);	/* [31:16]=fw ver, [15:0]=fw sub ver */

	if ((fw_ver > 0 && fw_ver < 0xc0000) || pg_ext_switch)
		use_ext_switch = true;

	if (init_hwcfg) {
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x39, 0x8, 0x1);
		btcoexist->btc_write_1byte(btcoexist, 0x974, 0xff);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x944, 0x3, 0x3);
		btcoexist->btc_write_1byte(btcoexist, 0x930, 0x77);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x20, 0x1);

		if (fw_ver >= 0x180000) {
			/* Use H2C to set GNT_BT to High to avoid A2DP click */
			h2c_parameter[0] = 1;
			btcoexist->btc_fill_h2c(btcoexist, 0x6E, 1,
						h2c_parameter);

			cnt_bt_cal_chk = 0;
			while(1)
			{
				if( pAdapter->bFWReady == FALSE )
				{
					//RT_TRACE(COMP_INIT , DBG_LOUD, ("halbtc8723b2ant_SetAntPath(): we don't need to wait for H2C command completion because of Fw download fail!!!\n"));
					break;
				}
				
				if( btcoexist->btc_read_1byte(btcoexist, 0x765) == 0x18 )
					break;

				cnt_bt_cal_chk++;
				if( cnt_bt_cal_chk > 20 )
					break;
			}
		} else
			btcoexist->btc_write_1byte(btcoexist, 0x765, 0x18);
		u32tmp_1[0] = btcoexist->btc_read_4byte(btcoexist, 0x948);
		if( (u32tmp_1[0] == 0x40) || (u32tmp_1[0] == 0x240))
			btcoexist->btc_write_4byte(btcoexist, 0x948, u32tmp_1[0]);
		else
			btcoexist->btc_write_4byte(btcoexist, 0x948, 0x0);

		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff,
					  0x0); /* WiFi TRx Mask off */
		/* remove due to interrupt is disabled that polling c2h will fail and delay 100ms. */
		/* btcoexist->btc_set_bt_reg(btcoexist, BTC_BT_REG_RF, 0x3c, 0x01); //BT TRx Mask off */

		if (board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT) {
			/* tell firmware "no antenna inverse" */
			h2c_parameter[0] = 0;
		} else {
			/* tell firmware "antenna inverse" */
			h2c_parameter[0] = 1;
		}

		if (use_ext_switch) {
			/* ext switch type */
			h2c_parameter[1] = 1;
		} else {
			/* int switch type */
			h2c_parameter[1] = 0;
		}
		btcoexist->btc_fill_h2c(btcoexist, 0x65, 2, h2c_parameter);
	} else {
		if (fw_ver >= 0x180000) {
			/* Use H2C to set GNT_BT to "Control by PTA"*/
			h2c_parameter[0] = 0;
			btcoexist->btc_fill_h2c(btcoexist, 0x6E, 1,
						h2c_parameter);

			cnt_bt_cal_chk = 0;
			while(1)
			{
				if( pAdapter->bFWReady == FALSE )
				{
					//RT_TRACE(COMP_INIT , DBG_LOUD, ("halbtc8723b2ant_SetAntPath(): we don't need to wait for H2C command completion because of Fw download fail!!!\n"));
					break;
				}
				
				if( btcoexist->btc_read_1byte(btcoexist, 0x765) == 0x0 )
					break;

				cnt_bt_cal_chk++;
				if( cnt_bt_cal_chk > 20 )
					break;
			}
		} else
			btcoexist->btc_write_1byte(btcoexist, 0x765, 0x0);
	}

	/* ext switch setting */
	if (use_ext_switch) {
		if (init_hwcfg) {
			/* 0x4c[23]=0, 0x4c[24]=1  Antenna control by WL/BT */
			u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x4c);
			u32tmp &= ~BIT(23);
			u32tmp |= BIT(24);
			btcoexist->btc_write_4byte(btcoexist, 0x4c, u32tmp);
		}
		u32tmp_1[0] = btcoexist->btc_read_4byte(btcoexist, 0x948);
		if( (u32tmp_1[0] == 0x40) || (u32tmp_1[0] == 0x240))
			btcoexist->btc_write_4byte(btcoexist, 0x948, u32tmp_1[0]);
		else
			btcoexist->btc_write_4byte(btcoexist, 0x948, 0x0);
		
		switch (ant_pos_type) {
		case BTC_ANT_WIFI_AT_MAIN:
			btcoexist->btc_write_1byte_bitmask(btcoexist,
							   0x92c, 0x3,
					   0x1);	/* ext switch main at wifi */
			break;
		case BTC_ANT_WIFI_AT_AUX:
			btcoexist->btc_write_1byte_bitmask(btcoexist,
							   0x92c, 0x3,
					   0x2);	/* ext switch aux at wifi */
			break;
		}
	} else {	/* internal switch */
		if (init_hwcfg) {
			/* 0x4c[23]=0, 0x4c[24]=1  Antenna control by WL/BT */
			u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x4c);
			u32tmp |= BIT(23);
			u32tmp &= ~BIT(24);
			btcoexist->btc_write_4byte(btcoexist, 0x4c, u32tmp);
		}

		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x64, 0x1,
			   0x0); /* fixed external switch S1->Main, S0->Aux */
		switch (ant_pos_type) {
		case BTC_ANT_WIFI_AT_MAIN:
			u32tmp_1[0] = btcoexist->btc_read_4byte(btcoexist, 0x948);
			if( (u32tmp_1[0] == 0x40) || (u32tmp_1[0] == 0x240))
				btcoexist->btc_write_4byte(btcoexist, 0x948, u32tmp_1[0]);
			else
				btcoexist->btc_write_4byte(btcoexist, 0x948, 0x0);
			break;
		case BTC_ANT_WIFI_AT_AUX:
			u32tmp_1[0] = btcoexist->btc_read_4byte(btcoexist, 0x948);
			if( (u32tmp_1[0] == 0x40) || (u32tmp_1[0] == 0x240))
				btcoexist->btc_write_4byte(btcoexist, 0x948, u32tmp_1[0]);
			else
				btcoexist->btc_write_4byte(btcoexist, 0x948, 0x280);
			break;
		}
	}
}
#if 0
boolean halbtc8723b2ant_CoexSwitchThresCheck(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8 wifi_rssi_state1, bt_rssi_state;
	u32 vendor;
	u8 offset = 0;

	btcoexist->btc_get(btcoexist, BTC_GET_U4_VENDOR, &vendor);

	/* if (vendor == BTC_VENDOR_LENOVO) */
	/*	offset = 20; */

	wifi_rssi_state1 = halbtc8723b2ant_wifi_rssi_state(btcoexist,
		&prewifi_rssi_state, 2, BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES
			   - coex_dm->switch_thres_offset, 0);
	bt_rssi_state = halbtc8723b2ant_bt_rssi_state(&pre_bt_rssi_state, 2,
			BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES -
			coex_dm->switch_thres_offset, 0);

	if (BTC_RSSI_LOW(wifi_rssi_state1) || BTC_RSSI_LOW(bt_rssi_state))
		return true;

	return false;
}
#endif

void halbtc8723b2ant_ps_tdma(IN struct btc_coexist *btcoexist,
		     IN boolean force_exec, IN boolean turn_on, IN u8 type)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8			wifi_rssi_state1,  bt_rssi_state;
	s8			wifi_duration_adjust = 0x0;
	u8			psTdmaByte4Modify = 0x0;
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	wifi_rssi_state1 = halbtc8723b2ant_wifi_rssi_state(btcoexist,
		&prewifi_rssi_state, 2, BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES
			   - coex_dm->switch_thres_offset, 0);
	bt_rssi_state = halbtc8723b2ant_bt_rssi_state(&pre_bt_rssi_state, 2,
			BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES -
			coex_dm->switch_thres_offset, 0);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], %s turn %s PS TDMA, type=%d\n",
		(force_exec ? "force to" : ""), (turn_on ? "ON" : "OFF"), type);
	BTC_TRACE(trace_buf);
	coex_dm->cur_ps_tdma_on = turn_on;
	coex_dm->cur_ps_tdma = type;

	if (!(BTC_RSSI_HIGH(wifi_rssi_state1) &&
	      BTC_RSSI_HIGH(bt_rssi_state)) && turn_on)
		/* if (halbtc8723b2ant_CoexSwitchThresCheck(btcoexist) &&  turn_on) */
	{
		type = type + 100; /* for WiFi RSSI low or BT RSSI low */
		coex_dm->is_switch_to_1dot5_ant = true;
	} else
		coex_dm->is_switch_to_1dot5_ant = false;


	if (!force_exec) {
		if ((coex_dm->pre_ps_tdma_on == coex_dm->cur_ps_tdma_on) &&
		    (coex_dm->pre_ps_tdma == coex_dm->cur_ps_tdma))
			return;
	}

	if (coex_sta->scan_ap_num <= 5) {
		if (coex_sta->a2dp_bit_pool >= 45)
			wifi_duration_adjust = -15;
		else if (coex_sta->a2dp_bit_pool >= 35)
			wifi_duration_adjust = -10;
		else
			wifi_duration_adjust = 5;
	} else  if (coex_sta->scan_ap_num <= 20) {
		if (coex_sta->a2dp_bit_pool >= 45)
			wifi_duration_adjust = -15;
		else if (coex_sta->a2dp_bit_pool >= 35)
			wifi_duration_adjust = -10;
		else
			wifi_duration_adjust = 0;
	} else if (coex_sta->scan_ap_num <= 40) {
		if (coex_sta->a2dp_bit_pool >= 45)
			wifi_duration_adjust = -15;
		else if (coex_sta->a2dp_bit_pool >= 35)
			wifi_duration_adjust = -10;
		else
			wifi_duration_adjust = -5;
	} else {
		if (coex_sta->a2dp_bit_pool >= 45)
			wifi_duration_adjust = -15;
		else if (coex_sta->a2dp_bit_pool >= 35)
			wifi_duration_adjust = -10;
		else
			wifi_duration_adjust = -10;
	}

	if ((bt_link_info->slave_role == true)	&& (bt_link_info->a2dp_exist))
		psTdmaByte4Modify =
			0x1;  /* 0x778 = 0x1 at wifi slot (no blocking BT Low-Pri pkts) */


	if (turn_on) {
		switch (type) {
		case 1:
		default:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
				      0x3c + wifi_duration_adjust, 0x03, 0xf1,
						      0x90 | psTdmaByte4Modify);
			break;
		case 2:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
				      0x2d + wifi_duration_adjust, 0x03, 0xf1,
						      0x90 | psTdmaByte4Modify);
			break;
		case 3:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x1c, 0x3, 0xf1,  0x90 |
						      psTdmaByte4Modify);
			break;
		case 4:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x10, 0x03, 0xf1,  0x90 |
						      psTdmaByte4Modify);
			break;
		case 5:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
				      0x3c + wifi_duration_adjust, 0x3, 0x70,
						      0x90 | psTdmaByte4Modify);
			break;
		case 6:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
				      0x2d + wifi_duration_adjust, 0x3, 0x70,
						      0x90 | psTdmaByte4Modify);
			break;
		case 7:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x1c, 0x3, 0x70,  0x90 |
						      psTdmaByte4Modify);
			break;
		case 8:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xa3,
						      0x10, 0x3, 0x70,  0x90 |
						      psTdmaByte4Modify);
			break;
		case 9:
		/*
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
				      0x3c + wifi_duration_adjust, 0x03, 0xf1,
						      0x90 | psTdmaByte4Modify);
		*/
		/* Bryant Modify for BT no-profile busy case */
		halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
				      0x3c + wifi_duration_adjust, 0x03, 0xf1,
						      0x91);
						      
			break;
		case 10:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
				      0x2d + wifi_duration_adjust, 0x03, 0xf1,
						      0x90 | psTdmaByte4Modify);
			break;
		case 11:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x1c, 0x3, 0xf1,  0x90 |
						      psTdmaByte4Modify);
			break;
		case 12:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x10, 0x3, 0xf1,  0x90 |
						      psTdmaByte4Modify);
			break;
		case 13:
		/*
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
				      0x3c + wifi_duration_adjust, 0x3, 0x70,
						      0x90 | psTdmaByte4Modify);
		*/
		/* Bryant Modify for BT no-profile busy case */
		halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
				      0x3c + wifi_duration_adjust, 0x3, 0x70,
						      0x91);						      
			break;
		case 14:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
				      0x2d + wifi_duration_adjust, 0x3, 0x70,
						      0x90 | psTdmaByte4Modify);
			break;
		case 15:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x1c, 0x3, 0x70,  0x90 |
						      psTdmaByte4Modify);
			break;
		case 16:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x10, 0x3, 0x70,  0x90 |
						      psTdmaByte4Modify);
			break;
		case 17:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xa3,
						      0x2f, 0x2f, 0x60, 0x90);
			break;
		case 18:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x5, 0x5, 0xe1, 0x90);
			break;
		case 19:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x25, 0x25, 0xe1, 0x90);
			break;
		case 20:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x25, 0x25, 0x60, 0x90);
			break;
		case 21:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x15, 0x03, 0x70, 0x90);
			break;
	  case 22:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x35, 0x03, 0xf1, 0x90);
			break;	
		case 23:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x35, 0x03, 0x71, 0x10);
			break;	
			
	  case 33:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x1c, 0x3, 0xf1,  0x91);		
		
			break;
		case 71:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
				      0x3c + wifi_duration_adjust, 0x03, 0xf1,
						      0x90);
			break;
		case 101:
		case 105:
		case 113:
		case 171:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xd3,
				      0x3a + wifi_duration_adjust, 0x03, 0x70,
						      0x50 | psTdmaByte4Modify);
			break;
		case 102:
		case 106:
		case 110:
		case 114:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xd3,
				      0x2d + wifi_duration_adjust, 0x03, 0x70,
						      0x50 | psTdmaByte4Modify);
			break;
		case 103:
		case 107:
		case 111:
		case 115:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xd3,
						      0x1c, 0x03, 0x70, 0x50 |
						      psTdmaByte4Modify);
			break;
		case 104:
		case 108:
		case 112:
		case 116:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xd3,
						      0x10, 0x03, 0x70, 0x50 |
						      psTdmaByte4Modify);
			break;
		case 109:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x3c, 0x03, 0xf1, 0x90 |
						      psTdmaByte4Modify);
			break;
		/* case 113:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x3c, 0x03, 0x70, 0x90 |
						      psTdmaByte4Modify);
			break; */
		case 121:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x15, 0x03, 0x70, 0x90 |
						      psTdmaByte4Modify);
			break;	
		case 122:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x35, 0x03, 0x71, 0x11);
			break;
	  case 123:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xe3,
						      0x35, 0x03, 0x71, 0x10);
			break;		
	  case 133:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0xd3,
						      0x1c, 0x3, 0x70,  0x51);		
		
			break;		
		}
	} else {
		/* disable PS tdma */
		switch (type) {
		case 0:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0x0,
						      0x0, 0x0, 0x40, 0x0);
			break;
		case 1:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0x0,
						      0x0, 0x0, 0x48, 0x0);
			break;
		default:
			halbtc8723b2ant_set_fw_pstdma(btcoexist, 0x0,
						      0x0, 0x0, 0x40, 0x0);
			break;
		}
	}

	/* update pre state */
	coex_dm->pre_ps_tdma_on = coex_dm->cur_ps_tdma_on;
	coex_dm->pre_ps_tdma = coex_dm->cur_ps_tdma;
}

void halbtc8723b2ant_ps_tdma_check_for_power_save_state(
	IN struct btc_coexist *btcoexist, IN boolean new_ps_state)
{
	u8	lps_mode = 0x0;

	btcoexist->btc_get(btcoexist, BTC_GET_U1_LPS_MODE, &lps_mode);

	if (lps_mode) {	/* already under LPS state */
		if (new_ps_state) {
			/* keep state under LPS, do nothing. */
		} else {
			/* will leave LPS state, turn off psTdma first */
			halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, false,
						1);
		}
	} else {					/* NO PS state */
		if (new_ps_state) {
			/* will enter LPS state, turn off psTdma first */
			halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, false,
						1);
		} else {
			/* keep state under NO PS state, do nothing. */
		}
	}
}

void halbtc8723b2ant_power_save_state(IN struct btc_coexist *btcoexist,
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
		halbtc8723b2ant_ps_tdma_check_for_power_save_state(
			btcoexist, true);
		halbtc8723b2ant_lps_rpwm(btcoexist, NORMAL_EXEC,
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
		halbtc8723b2ant_ps_tdma_check_for_power_save_state(
			btcoexist, false);
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS,
				   NULL);
		coex_sta->force_lps_on = false;
		break;
	default:
		break;
	}
}


void halbtc8723b2ant_coex_all_off(IN struct btc_coexist *btcoexist)
{
	/* fw all off */
	halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0,
					 0x0);
	halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
	halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
	halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	/* sw all off */
	halbtc8723b2ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8723b2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);

	/* hw all off */
	/* btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0); */
	halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
}

void halbtc8723b2ant_init_coex_dm(IN struct btc_coexist *btcoexist)
{
	/* force to reset coex mechanism */
	halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

	halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0,
					 0x0);
	halbtc8723b2ant_ps_tdma(btcoexist, FORCE_EXEC, false, 1);
	halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, FORCE_EXEC, 6);
	halbtc8723b2ant_dec_bt_pwr(btcoexist, FORCE_EXEC, 0);

	halbtc8723b2ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8723b2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);

	coex_sta->pop_event_cnt = 0;

}

void halbtc8723b2ant_action_bt_inquiry(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW,
			prewifi_rssi_state1 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state, wifi_rssi_state1, bt_rssi_state;
	boolean	wifi_connected = false;
	boolean	low_pwr_disable = true;
	boolean		scan = false, link = false, roam = false;
	boolean wifi_busy = false;


	wifi_rssi_state = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2, 15, 0);
	wifi_rssi_state1 = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state1, 2,
			   BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES -
			   coex_dm->switch_thres_offset, 0);
	bt_rssi_state = halbtc8723b2ant_bt_rssi_state(&pre_bt_rssi_state, 2,
			BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES -
			coex_dm->switch_thres_offset, 0);

	btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
			   &low_pwr_disable);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0,
					 0x0);

	if (coex_sta->bt_abnormal_scan) {
		halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						23);
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 3);				
	} else if (scan || link || roam) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Wifi link process + BT Inq/Page!!\n");
		BTC_TRACE(trace_buf);
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     7);
		halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 3);
	} else if (wifi_connected) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Wifi connected + BT Inq/Page!!\n");
		BTC_TRACE(trace_buf);
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     7);
						     
		if (wifi_busy)				     
			halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 3);
		else
		  halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 33); 	
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Wifi no-link + BT Inq/Page!!\n");
		BTC_TRACE(trace_buf);
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
		halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
	}

	halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, FORCE_EXEC, 6);
	halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	halbtc8723b2ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8723b2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
	/*
		coex_dm->need_recover0x948 = true;
		coex_dm->backup0x948 = btcoexist->btc_read_4byte(btcoexist, 0x948);

		halbtc8723b2ant_set_ant_path(btcoexist, BTC_ANT_WIFI_AT_AUX, false, false);
	*/
}


void halbtc8723b2ant_action_wifi_link_process(IN struct btc_coexist *btcoexist)
{
	u32	u32tmp;
	u8	u8tmpa, u8tmpb;

	halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 15);
	halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 22);

	halbtc8723b2ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8723b2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);


	u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x948);
	u8tmpa = btcoexist->btc_read_1byte(btcoexist, 0x765);
	u8tmpb = btcoexist->btc_read_1byte(btcoexist, 0x76e);

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"############# [BTCoex], 0x948=0x%x, 0x765=0x%x, 0x76e=0x%x\n",
		    u32tmp,  u8tmpa, u8tmpb);
	BTC_TRACE(trace_buf);
}

boolean halbtc8723b2ant_action_wifi_idle_process(IN struct btc_coexist
		*btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW,
			prewifi_rssi_state1 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state, wifi_rssi_state1, bt_rssi_state;
	u8		ap_num = 0;

	wifi_rssi_state = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2, 15, 0);
	/* wifi_rssi_state1 = halbtc8723b2ant_wifi_rssi_state(btcoexist, 1, 2, BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES-coex_dm->switch_thres_offset-coex_dm->switch_thres_offset, 0); */
	wifi_rssi_state1 = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state1, 2,
			   BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES -
		coex_dm->switch_thres_offset - coex_dm->switch_thres_offset, 0);
	bt_rssi_state = halbtc8723b2ant_bt_rssi_state(&pre_bt_rssi_state, 2,
			BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES -
		coex_dm->switch_thres_offset - coex_dm->switch_thres_offset, 0);

	btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM, &ap_num);

	/* define the office environment */
	if (BTC_RSSI_HIGH(wifi_rssi_state1) &&
	    (coex_sta->hid_exist == true) &&
	    (coex_sta->a2dp_exist == true)) {

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Wifi  idle process for BT HID+A2DP exist!!\n");
		BTC_TRACE(trace_buf);

		halbtc8723b2ant_dac_swing(btcoexist, NORMAL_EXEC, true, 0x6);
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

		/* sw all off */
		halbtc8723b2ant_sw_mechanism1(btcoexist, false, false, false,
					      false);
		halbtc8723b2ant_sw_mechanism2(btcoexist, false, false, false,
					      0x18);

		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

		halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
		halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);

		return true;
	} else {
		halbtc8723b2ant_dac_swing(btcoexist, NORMAL_EXEC, true, 0x18);
		return false;
	}


}



boolean halbtc8723b2ant_is_common_action(IN struct btc_coexist *btcoexist)
{
	boolean			common = false, wifi_connected = false, wifi_busy = false;
	boolean			bt_hs_on = false, low_pwr_disable = false;
	boolean			asus_8723b = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	if (!wifi_connected) {
		low_pwr_disable = false;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);
		halbtc8723b2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false,
					   0x8);

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Wifi non-connected idle!!\n");
		BTC_TRACE(trace_buf);

		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff,
					  0x0);
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

		halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
		halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
		halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

		halbtc8723b2ant_sw_mechanism1(btcoexist, false, false, false,
					      false);
		halbtc8723b2ant_sw_mechanism2(btcoexist, false, false, false,
					      0x18);

		common = true;
	} else {
		if (BT_8723B_2ANT_BT_STATUS_NON_CONNECTED_IDLE ==
		    coex_dm->bt_status) {
			low_pwr_disable = false;
			btcoexist->btc_set(btcoexist,
					   BTC_SET_ACT_DISABLE_LOW_POWER,
					   &low_pwr_disable);
			halbtc8723b2ant_limited_rx(btcoexist, NORMAL_EXEC,
						   false, false, 0x8);

			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Wifi connected + BT non connected-idle!!\n");
			BTC_TRACE(trace_buf);

			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1,
						  0xfffff, 0x0);
			halbtc8723b2ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 0);

			halbtc8723b2ant_power_save_state(btcoexist,
						 BTC_PS_WIFI_NATIVE, 0x0, 0x0);
			halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, false,
						1);
			halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC,
							 0xb);
			halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

			halbtc8723b2ant_sw_mechanism1(btcoexist, false, false, false,
						      false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);

			common = true;
		} else if (BT_8723B_2ANT_BT_STATUS_CONNECTED_IDLE ==
			   coex_dm->bt_status) {
			low_pwr_disable = true;
			btcoexist->btc_set(btcoexist,
					   BTC_SET_ACT_DISABLE_LOW_POWER,
					   &low_pwr_disable);

			if (bt_hs_on)
				return false;
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Wifi connected + BT connected-idle!!\n");
			BTC_TRACE(trace_buf);
			halbtc8723b2ant_limited_rx(btcoexist, NORMAL_EXEC,
						   false, false, 0x8);

			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1,
						  0xfffff, 0x0);
			halbtc8723b2ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 0);

			halbtc8723b2ant_power_save_state(btcoexist,
						 BTC_PS_WIFI_NATIVE, 0x0, 0x0);
			halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, false,
						1);
			halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC,
							 0xb);
			halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

			halbtc8723b2ant_sw_mechanism1(btcoexist, true, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);

			common = true;
		} else {
			low_pwr_disable = true;
			btcoexist->btc_set(btcoexist,
					   BTC_SET_ACT_DISABLE_LOW_POWER,
					   &low_pwr_disable);

			if (wifi_busy) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], Wifi Connected-Busy + BT Busy!!\n");
				BTC_TRACE(trace_buf);
				/* btcoexist->btc_get(btcoexist,
					BTC_GET_BL_IS_ASUS_8723B, &asus_8723b);
				if (!asus_8723b) 
					common = false;
				else
					common = halbtc8723b2ant_action_wifi_idle_process(
							 btcoexist); */
				common = false;
			} else {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], Wifi Connected-Idle + BT Busy!!\n");
				BTC_TRACE(trace_buf);
				/* common = false;	 */
				common = halbtc8723b2ant_action_wifi_idle_process(
						 btcoexist);
			}
		}
	}

	return common;
}
void halbtc8723b2ant_tdma_duration_adjust(IN struct btc_coexist *btcoexist,
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
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 13);
						coex_dm->ps_tdma_du_adj_type =
							13;
					} else if (max_interval == 2) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					} else if (max_interval == 3) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					}
				} else {
					if (max_interval == 1) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 9);
						coex_dm->ps_tdma_du_adj_type =
							9;
					} else if (max_interval == 2) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 10);
						coex_dm->ps_tdma_du_adj_type =
							10;
					} else if (max_interval == 3) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					}
				}
			} else {
				if (tx_pause) {
					if (max_interval == 1) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 5);
						coex_dm->ps_tdma_du_adj_type =
							5;
					} else if (max_interval == 2) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (max_interval == 3) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					}
				} else {
					if (max_interval == 1) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 1);
						coex_dm->ps_tdma_du_adj_type =
							1;
					} else if (max_interval == 2) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (max_interval == 3) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else {
						halbtc8723b2ant_ps_tdma(
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
		/* accquire the BT TRx retry count from BT_Info byte2 */
		retry_count = coex_sta->bt_retry_cnt;

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

			if (dn == 2) {/* if continuous 2 retry count(every 2 seconds) >0 and < 3, reduce WiFi duration */
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
				if (coex_dm->cur_ps_tdma == 71) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 5);
					coex_dm->ps_tdma_du_adj_type = 5;
				} else if (coex_dm->cur_ps_tdma == 1) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 5);
					coex_dm->ps_tdma_du_adj_type = 5;
				} else if (coex_dm->cur_ps_tdma == 2) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 6);
					coex_dm->ps_tdma_du_adj_type = 6;
				} else if (coex_dm->cur_ps_tdma == 3) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 4) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 8);
					coex_dm->ps_tdma_du_adj_type = 8;
				}
				if (coex_dm->cur_ps_tdma == 9) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 13);
					coex_dm->ps_tdma_du_adj_type = 13;
				} else if (coex_dm->cur_ps_tdma == 10) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 14);
					coex_dm->ps_tdma_du_adj_type = 14;
				} else if (coex_dm->cur_ps_tdma == 11) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 12) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 16);
					coex_dm->ps_tdma_du_adj_type = 16;
				}

				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 5) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (coex_dm->cur_ps_tdma == 6) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 8);
						coex_dm->ps_tdma_du_adj_type =
							8;
					} else if (coex_dm->cur_ps_tdma == 13) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					} else if (coex_dm->cur_ps_tdma == 14) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 16);
						coex_dm->ps_tdma_du_adj_type =
							16;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 8) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (coex_dm->cur_ps_tdma == 6) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 5);
						coex_dm->ps_tdma_du_adj_type =
							5;
					} else if (coex_dm->cur_ps_tdma == 16) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					} else if (coex_dm->cur_ps_tdma == 14) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 13);
						coex_dm->ps_tdma_du_adj_type =
							13;
					}
				}
			} else {
				if (coex_dm->cur_ps_tdma == 5) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 71);
					coex_dm->ps_tdma_du_adj_type = 71;
				} else if (coex_dm->cur_ps_tdma == 6) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 2);
					coex_dm->ps_tdma_du_adj_type = 2;
				} else if (coex_dm->cur_ps_tdma == 7) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 8) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 4);
					coex_dm->ps_tdma_du_adj_type = 4;
				}
				if (coex_dm->cur_ps_tdma == 13) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 9);
					coex_dm->ps_tdma_du_adj_type = 9;
				} else if (coex_dm->cur_ps_tdma == 14) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 10);
					coex_dm->ps_tdma_du_adj_type = 10;
				} else if (coex_dm->cur_ps_tdma == 15) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 16) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 12);
					coex_dm->ps_tdma_du_adj_type = 12;
				}

				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 71) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 1);
						coex_dm->ps_tdma_du_adj_type =
							1;
					} else if (coex_dm->cur_ps_tdma == 1) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (coex_dm->cur_ps_tdma == 2) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 4);
						coex_dm->ps_tdma_du_adj_type =
							4;
					} else if (coex_dm->cur_ps_tdma == 9) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 10);
						coex_dm->ps_tdma_du_adj_type =
							10;
					} else if (coex_dm->cur_ps_tdma == 10) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 12);
						coex_dm->ps_tdma_du_adj_type =
							12;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 4) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (coex_dm->cur_ps_tdma == 2) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 1);
						coex_dm->ps_tdma_du_adj_type =
							1;
					} else if (coex_dm->cur_ps_tdma == 1) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 71);
						coex_dm->ps_tdma_du_adj_type =
							71;
					} else if (coex_dm->cur_ps_tdma == 12) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 10);
						coex_dm->ps_tdma_du_adj_type =
							10;
					} else if (coex_dm->cur_ps_tdma == 10) {
						halbtc8723b2ant_ps_tdma(
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
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 6);
					coex_dm->ps_tdma_du_adj_type = 6;
				} else if (coex_dm->cur_ps_tdma == 2) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 6);
					coex_dm->ps_tdma_du_adj_type = 6;
				} else if (coex_dm->cur_ps_tdma == 3) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 4) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 8);
					coex_dm->ps_tdma_du_adj_type = 8;
				}
				if (coex_dm->cur_ps_tdma == 9) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 14);
					coex_dm->ps_tdma_du_adj_type = 14;
				} else if (coex_dm->cur_ps_tdma == 10) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 14);
					coex_dm->ps_tdma_du_adj_type = 14;
				} else if (coex_dm->cur_ps_tdma == 11) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 12) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 16);
					coex_dm->ps_tdma_du_adj_type = 16;
				}
				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 5) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (coex_dm->cur_ps_tdma == 6) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 8);
						coex_dm->ps_tdma_du_adj_type =
							8;
					} else if (coex_dm->cur_ps_tdma == 13) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					} else if (coex_dm->cur_ps_tdma == 14) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 16);
						coex_dm->ps_tdma_du_adj_type =
							16;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 8) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (coex_dm->cur_ps_tdma == 6) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (coex_dm->cur_ps_tdma == 16) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					} else if (coex_dm->cur_ps_tdma == 14) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					}
				}
			} else {
				if (coex_dm->cur_ps_tdma == 5) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 2);
					coex_dm->ps_tdma_du_adj_type = 2;
				} else if (coex_dm->cur_ps_tdma == 6) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 2);
					coex_dm->ps_tdma_du_adj_type = 2;
				} else if (coex_dm->cur_ps_tdma == 7) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 8) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 4);
					coex_dm->ps_tdma_du_adj_type = 4;
				}
				if (coex_dm->cur_ps_tdma == 13) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 10);
					coex_dm->ps_tdma_du_adj_type = 10;
				} else if (coex_dm->cur_ps_tdma == 14) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 10);
					coex_dm->ps_tdma_du_adj_type = 10;
				} else if (coex_dm->cur_ps_tdma == 15) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 16) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 12);
					coex_dm->ps_tdma_du_adj_type = 12;
				}
				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 1) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (coex_dm->cur_ps_tdma == 2) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 4);
						coex_dm->ps_tdma_du_adj_type =
							4;
					} else if (coex_dm->cur_ps_tdma == 9) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 10);
						coex_dm->ps_tdma_du_adj_type =
							10;
					} else if (coex_dm->cur_ps_tdma == 10) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 12);
						coex_dm->ps_tdma_du_adj_type =
							12;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 4) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (coex_dm->cur_ps_tdma == 2) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (coex_dm->cur_ps_tdma == 12) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 10);
						coex_dm->ps_tdma_du_adj_type =
							10;
					} else if (coex_dm->cur_ps_tdma == 10) {
						halbtc8723b2ant_ps_tdma(
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
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 2) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 3) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 4) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 8);
					coex_dm->ps_tdma_du_adj_type = 8;
				}
				if (coex_dm->cur_ps_tdma == 9) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 10) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 11) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 12) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 16);
					coex_dm->ps_tdma_du_adj_type = 16;
				}
				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 5) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 6) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 8);
						coex_dm->ps_tdma_du_adj_type =
							8;
					} else if (coex_dm->cur_ps_tdma == 13) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 14) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 16);
						coex_dm->ps_tdma_du_adj_type =
							16;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 8) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 6) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 16) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 14) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					}
				}
			} else {
				if (coex_dm->cur_ps_tdma == 5) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 6) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 7) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 8) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 4);
					coex_dm->ps_tdma_du_adj_type = 4;
				}
				if (coex_dm->cur_ps_tdma == 13) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 14) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 15) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 16) {
					halbtc8723b2ant_ps_tdma(btcoexist,
							NORMAL_EXEC, true, 12);
					coex_dm->ps_tdma_du_adj_type = 12;
				}
				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 1) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 2) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 4);
						coex_dm->ps_tdma_du_adj_type =
							4;
					} else if (coex_dm->cur_ps_tdma == 9) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 10) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 12);
						coex_dm->ps_tdma_du_adj_type =
							12;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 4) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 2) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 12) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						halbtc8723b2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 10) {
						halbtc8723b2ant_ps_tdma(
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
			halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						coex_dm->ps_tdma_du_adj_type);
		else {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], roaming/link/scan is under progress, will adjust next time!!!\n");
			BTC_TRACE(trace_buf);
		}
	}
}

/* SCO only or SCO+PAN(HS) */
void halbtc8723b2ant_action_sco(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8	wifi_rssi_state, bt_rssi_state;
	u32	wifi_bw;

	wifi_rssi_state = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2, 15, 0);
	bt_rssi_state = halbtc8723b2ant_bt_rssi_state(&pre_bt_rssi_state, 2,
			BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES -
			coex_dm->switch_thres_offset, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 4);

	if (BTC_RSSI_HIGH(bt_rssi_state))
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
	else
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_LEGACY == wifi_bw) /* for SCO quality at 11b/g mode */
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
	else  /* for SCO quality & wifi performance balance at 11n mode */
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 8);

	halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0,
					 0x0);
	halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, false,
				0); /* for voice quality */

	/* sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, true,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      true, 0x4);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, true,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      true, 0x4);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      true, 0x4);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      true, 0x4);
		}
	}
}


void halbtc8723b2ant_action_hid(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8	wifi_rssi_state, bt_rssi_state;
	u32	wifi_bw;

	wifi_rssi_state = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2, 15, 0);
	bt_rssi_state = halbtc8723b2ant_bt_rssi_state(&pre_bt_rssi_state, 2,
			BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES -
			coex_dm->switch_thres_offset, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(bt_rssi_state))
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
	else
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_LEGACY == wifi_bw) /* for HID at 11b/g mode */
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
	else  /* for HID quality & wifi performance balance at 11n mode */
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 9);

	halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0,
					 0x0);

	if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
	    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
		halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 9);
	else
		halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 13);

	/* sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, true,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      false, 0x18);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, true,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      false, 0x18);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	}
}

/* A2DP only / PAN(EDR) only/ A2DP+PAN(HS) */
void halbtc8723b2ant_action_a2dp(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW,
			prewifi_rssi_state1 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state, wifi_rssi_state1, bt_rssi_state;
	u32		wifi_bw;
	u8		ap_num = 0;

	wifi_rssi_state = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2, 15, 0);
	wifi_rssi_state1 = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state1, 2,
			   BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES -
			   coex_dm->switch_thres_offset, 0);
	bt_rssi_state = halbtc8723b2ant_bt_rssi_state(&pre_bt_rssi_state, 2,
			BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES -
			coex_dm->switch_thres_offset, 0);

	btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM, &ap_num);

	/* define the office environment */
	if ((ap_num >= 10) && BTC_RSSI_HIGH(wifi_rssi_state1) &&
	    BTC_RSSI_HIGH(bt_rssi_state)) {
		/* dbg_print(" AP#>10(%d)\n", ap_num); */
		halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);

		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff,
					  0x0);
		halbtc8723b2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false,
					   0x8);
		halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);

		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

		halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
		halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);

		/* sw mechanism */
		btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
		if (BTC_WIFI_BW_HT40 == wifi_bw) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      true, 0x18);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      true, 0x18);
		}
		return;

	}

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);
	halbtc8723b2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(bt_rssi_state))
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
	else
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);


	if (BTC_RSSI_HIGH(wifi_rssi_state1) && BTC_RSSI_HIGH(bt_rssi_state)) {
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
		halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
	} else {
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     13);
		halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_LPS_ON, 0x50,
						 0x4);
	}


	if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
	    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
		halbtc8723b2ant_tdma_duration_adjust(btcoexist, false, false,
						     1);
	else
		halbtc8723b2ant_tdma_duration_adjust(btcoexist, false, true, 1);

	/* sw mechanism */
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      false, 0x18);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      false, 0x18);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	}
}

void halbtc8723b2ant_action_a2dp_pan_hs(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW,
			prewifi_rssi_state1 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state, wifi_rssi_state1, bt_rssi_state;
	u32		wifi_bw;

	wifi_rssi_state = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2, 15, 0);
	wifi_rssi_state1 = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state1, 2,
			   BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES -
			   coex_dm->switch_thres_offset, 0);
	bt_rssi_state = halbtc8723b2ant_bt_rssi_state(&pre_bt_rssi_state, 2,
			BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES -
			coex_dm->switch_thres_offset, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(bt_rssi_state))
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
	else
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	if (BTC_RSSI_HIGH(wifi_rssi_state1) && BTC_RSSI_HIGH(bt_rssi_state)) {
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
		halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
	} else {
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     13);
		halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_LPS_ON, 0x50,
						 0x4);
	}

	halbtc8723b2ant_tdma_duration_adjust(btcoexist, false, true, 2);

	/* sw mechanism */
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      false, 0x18);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      false, 0x18);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	}
}

void halbtc8723b2ant_action_pan_edr(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW,
			prewifi_rssi_state1 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state, wifi_rssi_state1, bt_rssi_state;
	u32		wifi_bw;

	wifi_rssi_state = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2, 15, 0);
	wifi_rssi_state1 = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state1, 2,
			   BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES -
			   coex_dm->switch_thres_offset, 0);
	bt_rssi_state = halbtc8723b2ant_bt_rssi_state(&pre_bt_rssi_state, 2,
			BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES -
			coex_dm->switch_thres_offset, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(bt_rssi_state))
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
	else
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	if (BTC_RSSI_HIGH(wifi_rssi_state1) && BTC_RSSI_HIGH(bt_rssi_state)) {
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     10);
		halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
	} else {
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     13);
		halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_LPS_ON, 0x50,
						 0x4);
	}

	if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
	    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
		halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 1);
	else
		halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 5);

	/* sw mechanism */
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      false, 0x18);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      false, 0x18);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	}
}


/* PAN(HS) only */
void halbtc8723b2ant_action_pan_hs(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW,
			prewifi_rssi_state1 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state, wifi_rssi_state1, bt_rssi_state;
	u32		wifi_bw;

	wifi_rssi_state = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2, 15, 0);
	wifi_rssi_state1 = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state1, 2,
			   BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES -
			   coex_dm->switch_thres_offset, 0);
	bt_rssi_state = halbtc8723b2ant_bt_rssi_state(&pre_bt_rssi_state, 2,
			BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES -
			coex_dm->switch_thres_offset, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(bt_rssi_state))
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
	else
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);

	halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0,
					 0x0);
	halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      false, 0x18);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      false, 0x18);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	}
}

/* PAN(EDR)+A2DP */
void halbtc8723b2ant_action_pan_edr_a2dp(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW,
			prewifi_rssi_state1 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state, wifi_rssi_state1, bt_rssi_state;
	u8		ap_num = 0;
	u32		wifi_bw;

	wifi_rssi_state = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2, 15, 0);
	wifi_rssi_state1 = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state1, 2,
			   BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES -
			   coex_dm->switch_thres_offset, 0);
	bt_rssi_state = halbtc8723b2ant_bt_rssi_state(&pre_bt_rssi_state, 2,
			BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES -
			coex_dm->switch_thres_offset, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(bt_rssi_state))
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
	else
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	if (BTC_RSSI_HIGH(wifi_rssi_state1) && BTC_RSSI_HIGH(bt_rssi_state))
		halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
	else
		halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_LPS_ON, 0x50,
						 0x4);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM,
					   &ap_num);

	if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
	    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     12);

		if(ap_num < 10)
			halbtc8723b2ant_tdma_duration_adjust(btcoexist, true, false, 1);
		else
			halbtc8723b2ant_tdma_duration_adjust(btcoexist, true, false, 3);

	} else {
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     13);
		if(ap_num < 10)
			halbtc8723b2ant_tdma_duration_adjust(btcoexist, true, true, 1);
		else
			halbtc8723b2ant_tdma_duration_adjust(btcoexist, true, true, 3);
	}

	/* sw mechanism	 */
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      false, 0x18);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      false, 0x18);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	}
}

void halbtc8723b2ant_action_pan_edr_hid(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW,
			prewifi_rssi_state1 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state, wifi_rssi_state1, bt_rssi_state;
	u32		wifi_bw;

	wifi_rssi_state = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2, 15, 0);
	wifi_rssi_state1 = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state1, 2,
			   BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES -
			   coex_dm->switch_thres_offset, 0);
	bt_rssi_state = halbtc8723b2ant_bt_rssi_state(&pre_bt_rssi_state, 2,
			BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES -
			coex_dm->switch_thres_offset, 0);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	halbtc8723b2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	if (BTC_RSSI_HIGH(bt_rssi_state))
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
	else
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	if (BTC_RSSI_HIGH(wifi_rssi_state1) && BTC_RSSI_HIGH(bt_rssi_state)) {
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
		halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
	} else {
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     14);
		halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_LPS_ON, 0x50,
						 0x4);
	}

	if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
	    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
		if (BTC_WIFI_BW_HT40 == wifi_bw) {
			halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC,
							 3);
			/* halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 11); */
			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1,
						  0xfffff, 0x780);
		} else {
			halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC,
							 6);
			/* halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7); */
			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1,
						  0xfffff, 0x0);
		}
		halbtc8723b2ant_tdma_duration_adjust(btcoexist, true, false, 2);
	} else {
		halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		/* halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 14); */
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff,
					  0x0);
		halbtc8723b2ant_tdma_duration_adjust(btcoexist, true, true, 2);
	}

	/* sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, true,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      false, 0x18);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, true,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      false, 0x18);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	}
}

/* HID+A2DP+PAN(EDR) */
void halbtc8723b2ant_action_hid_a2dp_pan_edr(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW,
			prewifi_rssi_state1 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state, wifi_rssi_state1,  bt_rssi_state;
	u32		wifi_bw;

	wifi_rssi_state = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2, 15, 0);
	wifi_rssi_state1 = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state1, 2,
			   BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES -
			   coex_dm->switch_thres_offset, 0);
	bt_rssi_state = halbtc8723b2ant_bt_rssi_state(&pre_bt_rssi_state, 2,
			BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES -
			coex_dm->switch_thres_offset, 0);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_limited_rx(btcoexist, NORMAL_EXEC, false, false, 0x8);

	halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (BTC_RSSI_HIGH(bt_rssi_state))
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
	else
		halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	if (BTC_RSSI_HIGH(wifi_rssi_state1) && BTC_RSSI_HIGH(bt_rssi_state)) {
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
		halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
	} else {
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     14);
		halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_LPS_ON, 0x50,
						 0x4);
	}

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
	    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
		if (BTC_WIFI_BW_HT40 == wifi_bw)
			halbtc8723b2ant_tdma_duration_adjust(btcoexist, true,
							     true, 3);
		else
			halbtc8723b2ant_tdma_duration_adjust(btcoexist, true,
							     false, 3);
	} else
		halbtc8723b2ant_tdma_duration_adjust(btcoexist, true, true, 3);

	/* sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, true,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      false, 0x18);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, true,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      false, 0x18);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	}
}

void halbtc8723b2ant_action_hid_a2dp(IN struct btc_coexist *btcoexist)
{
	static u8	prewifi_rssi_state = BTC_RSSI_STATE_LOW,
			prewifi_rssi_state1 = BTC_RSSI_STATE_LOW;
	static u8	pre_bt_rssi_state = BTC_RSSI_STATE_LOW;
	u8		wifi_rssi_state, wifi_rssi_state1, bt_rssi_state;
	u32		wifi_bw;
	u8		ap_num = 0;

	wifi_rssi_state = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			  &prewifi_rssi_state, 2, 15, 0);
	/* bt_rssi_state = halbtc8723b2ant_bt_rssi_state(2, 29, 0); */
	wifi_rssi_state1 = halbtc8723b2ant_wifi_rssi_state(btcoexist,
			   &prewifi_rssi_state1, 2,
			   BT_8723B_2ANT_WIFI_RSSI_COEXSWITCH_THRES -
			   coex_dm->switch_thres_offset, 0);
	bt_rssi_state = halbtc8723b2ant_bt_rssi_state(&pre_bt_rssi_state, 3,
			BT_8723B_2ANT_BT_RSSI_COEXSWITCH_THRES -
			coex_dm->switch_thres_offset, 37);

	btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM,
					   &ap_num);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0);

	halbtc8723b2ant_limited_rx(btcoexist, NORMAL_EXEC, false, true, 0x5);

	halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	if (BTC_WIFI_BW_LEGACY == wifi_bw) {
		if (BTC_RSSI_HIGH(bt_rssi_state))
			halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
		else if (BTC_RSSI_MEDIUM(bt_rssi_state))
			halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
		else
			halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);
	} else {
		/* only 802.11N mode we have to dec bt power to 4 degree */
		if (BTC_RSSI_HIGH(bt_rssi_state)) {
			/* need to check ap Number of Not */
			if (ap_num < 10)
				halbtc8723b2ant_dec_bt_pwr(btcoexist,
							   NORMAL_EXEC, 4);
			else
				halbtc8723b2ant_dec_bt_pwr(btcoexist,
							   NORMAL_EXEC, 2);
		} else if (BTC_RSSI_MEDIUM(bt_rssi_state))
			halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 2);
		else
			halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);
	}

	if (BTC_RSSI_HIGH(wifi_rssi_state1) && BTC_RSSI_HIGH(bt_rssi_state)) {
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 7);
		halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
	} else {
		halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC,
						     14);
		halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_LPS_ON, 0x50,
						 0x4);
	}

	if(BTC_RSSI_HIGH(bt_rssi_state)) {
		if(ap_num < 10)
			halbtc8723b2ant_tdma_duration_adjust(btcoexist, true, false, 1);
		else
			halbtc8723b2ant_tdma_duration_adjust(btcoexist, true, false, 3);
	} else {
		halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 18);
		btcoexist->btc_write_1byte(btcoexist, 0x456, 0x38);
		btcoexist->btc_write_2byte(btcoexist, 0x42a, 0x0808);
		btcoexist->btc_write_4byte(btcoexist, 0x430, 0x0);
		btcoexist->btc_write_4byte(btcoexist, 0x434, 0x01010000);

		if(ap_num < 10)
			halbtc8723b2ant_tdma_duration_adjust(btcoexist, true, true, 1);
		else
			halbtc8723b2ant_tdma_duration_adjust(btcoexist, true, true, 3);
	}

	/* sw mechanism */
	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, true,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      false, 0x18);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, true, true,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	} else {
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, true, false,
						      false, 0x18);
		} else {
			halbtc8723b2ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b2ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	}
}

void halbtc8723b2ant_action_bt_whck_test(IN struct btc_coexist *btcoexist)
{
	halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	/* sw all off */
	halbtc8723b2ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8723b2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);

	halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0,
					 0x0);

	halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
	halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
}

void halbtc8723b2ant_action_wifi_multi_port(IN struct btc_coexist *btcoexist)
{
	halbtc8723b2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
	halbtc8723b2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, 0);

	/* sw all off */
	halbtc8723b2ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8723b2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);

	/* hw all off */
	/* btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x0); */
	halbtc8723b2ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);

	halbtc8723b2ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE, 0x0,
					 0x0);
	halbtc8723b2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
}

void halbtc8723b2ant_run_coexist_mechanism(IN struct btc_coexist *btcoexist)
{
	u8				algorithm = 0;
	u32				num_of_wifi_link = 0;
	u32				wifi_link_status = 0;
	struct  btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	boolean				miracast_plus_bt = false;
	boolean				scan = false, link = false, roam = false;

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

	if (coex_sta->bt_whck_test) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is under WHCK TEST!!!\n");
		BTC_TRACE(trace_buf);
		halbtc8723b2ant_action_bt_whck_test(btcoexist);
		return;
	}

	algorithm = halbtc8723b2ant_action_algorithm(btcoexist);
	if (coex_sta->c2h_bt_inquiry_page &&
	    (BT_8723B_2ANT_COEX_ALGO_PANHS != algorithm)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BT is under inquiry/page scan !!\n");
		BTC_TRACE(trace_buf);
		halbtc8723b2ant_action_bt_inquiry(btcoexist);
		return;
	} else {
		/*
			if(coex_dm->need_recover0x948)
			{
				coex_dm->need_recover0x948 = false;
				btcoexist->btc_write_4byte(btcoexist, 0x948, coex_dm->backup0x948);
			}
		*/
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);

	if (scan || link || roam) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], WiFi is under Link Process !!\n");
		BTC_TRACE(trace_buf);
		halbtc8723b2ant_action_wifi_link_process(btcoexist);
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
			miracast_plus_bt = true;
		else
			miracast_plus_bt = false;

		btcoexist->btc_set(btcoexist, BTC_SET_BL_MIRACAST_PLUS_BT,
				   &miracast_plus_bt);
		halbtc8723b2ant_action_wifi_multi_port(btcoexist);

		return;
	} else {
		miracast_plus_bt = false;
		btcoexist->btc_set(btcoexist, BTC_SET_BL_MIRACAST_PLUS_BT,
				   &miracast_plus_bt);
	}

	coex_dm->cur_algorithm = algorithm;
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Algorithm = %d\n",
		    coex_dm->cur_algorithm);
	BTC_TRACE(trace_buf);

	if (halbtc8723b2ant_is_common_action(btcoexist)) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Action 2-Ant common.\n");
		BTC_TRACE(trace_buf);
		coex_dm->auto_tdma_adjust = false;
	} else {
		if (coex_dm->cur_algorithm != coex_dm->pre_algorithm) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], pre_algorithm=%d, cur_algorithm=%d\n",
				coex_dm->pre_algorithm, coex_dm->cur_algorithm);
			BTC_TRACE(trace_buf);
			coex_dm->auto_tdma_adjust = false;
		}
		switch (coex_dm->cur_algorithm) {
		case BT_8723B_2ANT_COEX_ALGO_SCO:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = SCO.\n");
			BTC_TRACE(trace_buf);
			halbtc8723b2ant_action_sco(btcoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_HID:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = HID.\n");
			BTC_TRACE(trace_buf);
			halbtc8723b2ant_action_hid(btcoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_A2DP:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = A2DP.\n");
			BTC_TRACE(trace_buf);
			halbtc8723b2ant_action_a2dp(btcoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_A2DP_PANHS:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = A2DP+PAN(HS).\n");
			BTC_TRACE(trace_buf);
			halbtc8723b2ant_action_a2dp_pan_hs(btcoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_PANEDR:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = PAN(EDR).\n");
			BTC_TRACE(trace_buf);
			halbtc8723b2ant_action_pan_edr(btcoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_PANHS:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = HS mode.\n");
			BTC_TRACE(trace_buf);
			halbtc8723b2ant_action_pan_hs(btcoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_PANEDR_A2DP:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = PAN+A2DP.\n");
			BTC_TRACE(trace_buf);
			halbtc8723b2ant_action_pan_edr_a2dp(btcoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_PANEDR_HID:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = PAN(EDR)+HID.\n");
			BTC_TRACE(trace_buf);
			halbtc8723b2ant_action_pan_edr_hid(btcoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_HID_A2DP_PANEDR:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = HID+A2DP+PAN.\n");
			BTC_TRACE(trace_buf);
			halbtc8723b2ant_action_hid_a2dp_pan_edr(
				btcoexist);
			break;
		case BT_8723B_2ANT_COEX_ALGO_HID_A2DP:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = HID+A2DP.\n");
			BTC_TRACE(trace_buf);
			halbtc8723b2ant_action_hid_a2dp(btcoexist);
			break;
		default:
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Action 2-Ant, algorithm = coexist All Off!!\n");
			BTC_TRACE(trace_buf);
			halbtc8723b2ant_coex_all_off(btcoexist);
			break;
		}
		coex_dm->pre_algorithm = coex_dm->cur_algorithm;
	}
}

void halbtc8723b2ant_wifi_off_hw_cfg(IN struct btc_coexist *btcoexist)
{
	boolean	is_in_mp_mode = false;
	u8 h2c_parameter[2] = {0};
	u32 fw_ver = 0;

	/* set wlan_act to low */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0x4);

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff,
			  0x780); /* WiFi goto standby while GNT_BT 0-->1 */
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
	if (fw_ver >= 0x180000) {
		/* Use H2C to set GNT_BT to HIGH */
		h2c_parameter[0] = 1;
		btcoexist->btc_fill_h2c(btcoexist, 0x6E, 1, h2c_parameter);
	} else
		btcoexist->btc_write_1byte(btcoexist, 0x765, 0x18);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_IS_IN_MP_MODE,
			   &is_in_mp_mode);
	if (!is_in_mp_mode)
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x20,
			   0x0); /* BT select s0/s1 is controlled by BT */
	else
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x20,
			   0x1); /* BT select s0/s1 is controlled by WiFi */
}

void halbtc8723b2ant_init_hw_config(IN struct btc_coexist *btcoexist,
				    IN boolean back_up)
{
	u8	u8tmp = 0;
	u32	 vendor;


	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], 2Ant Init HW Config!!\n");
	BTC_TRACE(trace_buf);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_VENDOR, &vendor);
	if (vendor == BTC_VENDOR_LENOVO)
		coex_dm->switch_thres_offset = 0;
	else if (vendor == BTC_VENDOR_ASUS)
		coex_dm->switch_thres_offset = 0;
	else
		coex_dm->switch_thres_offset = 20;

	/* 0xf0[15:12] --> Chip Cut information */
	coex_sta->cut_version = (btcoexist->btc_read_1byte(btcoexist,
				 0xf1) & 0xf0) >> 4;

	/* backup rf 0x1e value */
	coex_dm->bt_rf_0x1e_backup =
		btcoexist->btc_get_rf_reg(btcoexist, BTC_RF_A, 0x1e, 0xfffff);

	/* 0x790[5:0]=0x5 */
	u8tmp = btcoexist->btc_read_1byte(btcoexist, 0x790);
	u8tmp &= 0xc0;
	u8tmp |= 0x5;
	btcoexist->btc_write_1byte(btcoexist, 0x790, u8tmp);

	/* Antenna config	 */
	halbtc8723b2ant_set_ant_path(btcoexist, BTC_ANT_WIFI_AT_MAIN, true,
				     false);
	coex_sta->dis_ver_info_cnt = 0;

	/* PTA parameter */
	halbtc8723b2ant_coex_table_with_type(btcoexist, FORCE_EXEC, 0);

	/* Enable counter statistics */
	btcoexist->btc_write_1byte(btcoexist, 0x76e,
			   0x4); /* 0x76e[3] =1, WLAN_Act control by PTA */
	btcoexist->btc_write_1byte(btcoexist, 0x778, 0x3);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x40, 0x20, 0x1);
}

/* ************************************************************
 * work around function start with wa_halbtc8723b2ant_
 * ************************************************************
 * ************************************************************
 * extern function start with ex_halbtc8723b2ant_
 * ************************************************************ */
void ex_halbtc8723b2ant_power_on_setting(IN struct btc_coexist *btcoexist)
{
	struct  btc_board_info	*board_info = &btcoexist->board_info;
	u16 u16tmp = 0x0;
	u32				value = 0;
	u32    u32tmp_1[4];

	btcoexist->btc_write_1byte(btcoexist, 0x67, 0x20);

	/* enable BB, REG_SYS_FUNC_EN such that we can write 0x948 correctly. */
	u16tmp = btcoexist->btc_read_2byte(btcoexist, 0x2);
	btcoexist->btc_write_2byte(btcoexist, 0x2, u16tmp | BIT(0) | BIT(1));
 DbgPrint("--- TEST 5 ---\n");
		
	btcoexist->btc_write_4byte(btcoexist, 0x948, 0x0);

	if (btcoexist->chip_interface == BTC_INTF_USB) {
		/* fixed at S0 for USB interface */
		board_info->btdm_ant_pos = BTC_ANTENNA_AT_AUX_PORT;
	} else {
		/* for PCIE and SDIO interface, we check efuse 0xc3[6] */
		if (board_info->single_ant_path == 0) {
			/* set to S1 */
			board_info->btdm_ant_pos = BTC_ANTENNA_AT_MAIN_PORT;
		} else if (board_info->single_ant_path == 1) {
			/* set to S0 */
			board_info->btdm_ant_pos = BTC_ANTENNA_AT_AUX_PORT;
		}
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_ANTPOSREGRISTRY_CTRL,
				   &value);
	}
}

void ex_halbtc8723b2ant_pre_load_firmware(IN struct btc_coexist *btcoexist)
{
	struct  btc_board_info	*board_info = &btcoexist->board_info;
	u8 u8tmp = 0x4; /* Set BIT2 by default since it's 2ant case */

	/*  */
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
			btcoexist->btc_write_local_reg_1byte(btcoexist, 0x384,
							     u8tmp);
		else if (btcoexist->chip_interface == BTC_INTF_SDIO)
			btcoexist->btc_write_local_reg_1byte(btcoexist, 0x60,
							     u8tmp);
	}
}

void ex_halbtc8723b2ant_init_hw_config(IN struct btc_coexist *btcoexist,
				       IN boolean wifi_only)
{
	halbtc8723b2ant_init_hw_config(btcoexist, true);
}

void ex_halbtc8723b2ant_init_coex_dm(IN struct btc_coexist *btcoexist)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], Coex Mechanism Init!!\n");
	BTC_TRACE(trace_buf);

	halbtc8723b2ant_init_coex_dm(btcoexist);
}

void ex_halbtc8723b2ant_display_coex_info(IN struct btc_coexist *btcoexist)
{
	struct  btc_board_info		*board_info = &btcoexist->board_info;
	struct  btc_bt_link_info	*bt_link_info = &btcoexist->bt_link_info;
	u8				*cli_buf = btcoexist->cli_buf;
	u8				u8tmp[4], i, bt_info_ext, ps_tdma_case = 0;
	u32				u32tmp[4];
	u32				fa_of_dm, fa_cck;
	u32				fw_ver = 0, bt_patch_ver = 0;
	static u8			pop_report_in_10s = 0;

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

	btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER, &bt_patch_ver);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %d_%x/ 0x%x/ 0x%x(%d)/ %c",
		   "Version Coex/ Fw/ Patch/ Cut",
		   glcoex_ver_date_8723b_2ant, glcoex_ver_8723b_2ant, fw_ver,
		   bt_patch_ver, bt_patch_ver, coex_sta->cut_version + 65);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x ",
		   "Wifi channel informed to BT",
		   coex_dm->wifi_chnl_info[0], coex_dm->wifi_chnl_info[1],
		   coex_dm->wifi_chnl_info[2]);
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
		((BT_8723B_2ANT_BT_STATUS_CONNECTED_IDLE == coex_dm->bt_status)
				       ? "connected-idle" : "busy")))),
		   coex_sta->bt_rssi - 100, coex_sta->bt_retry_cnt,
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

	{
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s",
			   "BT Role",
			   (bt_link_info->slave_role) ? "Slave" : "Master");
		CL_PRINTF(cli_buf);
	}

	bt_info_ext = coex_sta->bt_info_ext;
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s / %d",
		   "A2DP Rate/Bitpool",
		(bt_info_ext & BIT(0)) ? "BR" : "EDR", coex_sta->a2dp_bit_pool);
	CL_PRINTF(cli_buf);

	for (i = 0; i < BT_INFO_SRC_8723B_2ANT_MAX; i++) {
		if (coex_sta->bt_info_c2h_cnt[i]) {
			CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
				"\r\n %-35s = %02x %02x %02x %02x %02x %02x %02x(%d)",
				   glbt_info_src_8723b_2ant[i],
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
			"============[Sw mechanism] (before Manual)============");
	else
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
			   "============[Sw mechanism]============");

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
	if (btcoexist->manual_control)
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
			"============[Fw mechanism] (before Manual) ============");
	else
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
			   "============[Fw mechanism]============");

	ps_tdma_case = coex_dm->cur_ps_tdma;

	if (coex_dm->is_switch_to_1dot5_ant)
		ps_tdma_case = ps_tdma_case + 100;

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

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x778);
	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x880);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "0x778/0x880[29:25]",
		   u8tmp[0], (u32tmp[0] & 0x3e000000) >> 25);
	CL_PRINTF(cli_buf);


	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x948);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x67);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x765);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x948/ 0x67[5] / 0x765",
		   u32tmp[0], ((u8tmp[0] & 0x20) >> 5), u8tmp[1]);
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

	fa_of_dm = ((u32tmp[0] & 0xffff0000) >> 16) + ((u32tmp[1] & 0xffff0000)
			>> 16) + (u32tmp[1] & 0xffff) + (u32tmp[2] & 0xffff) + \
		   ((u32tmp[3] & 0xffff0000) >> 16) + (u32tmp[3] &
				   0xffff) ;
	fa_cck = (u8tmp[0] << 8) + u8tmp[1];

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "OFDM-CCA/OFDM-FA/CCK-FA",
		   u32tmp[0] & 0xffff, fa_of_dm, fa_cck);
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
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x6cc);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x",
		   "0x6c0/0x6c4/0x6c8/0x6cc(coexTable)",
		   u32tmp[0], u32tmp[1], u32tmp[2], u8tmp[0]);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "0x770(high-pri rx/tx)",
		   coex_sta->high_priority_rx, coex_sta->high_priority_tx);
	CL_PRINTF(cli_buf);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "0x774(low-pri rx/tx)",
		   coex_sta->low_priority_rx, coex_sta->low_priority_tx);
	CL_PRINTF(cli_buf);
#if (BT_AUTO_REPORT_ONLY_8723B_2ANT == 1)
	/* halbtc8723b2ant_monitor_bt_ctr(btcoexist); */
#endif
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_COEX_STATISTICS);
}


void ex_halbtc8723b2ant_ips_notify(IN struct btc_coexist *btcoexist, IN u8 type)
{
	if (BTC_IPS_ENTER == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], IPS ENTER notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_ips = true;
		halbtc8723b2ant_wifi_off_hw_cfg(btcoexist);
		halbtc8723b2ant_ignore_wlan_act(btcoexist, FORCE_EXEC, true);
		halbtc8723b2ant_coex_all_off(btcoexist);
	} else if (BTC_IPS_LEAVE == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], IPS LEAVE notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_ips = false;
		halbtc8723b2ant_init_hw_config(btcoexist, false);
		halbtc8723b2ant_init_coex_dm(btcoexist);
		halbtc8723b2ant_query_bt_info(btcoexist);
	}
}

void ex_halbtc8723b2ant_lps_notify(IN struct btc_coexist *btcoexist, IN u8 type)
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

void ex_halbtc8723b2ant_scan_notify(IN struct btc_coexist *btcoexist,
				    IN u8 type)
{
	u32	u32tmp;
	u8	u8tmpa, u8tmpb;



	u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x948);
	u8tmpa = btcoexist->btc_read_1byte(btcoexist, 0x765);
	u8tmpb = btcoexist->btc_read_1byte(btcoexist, 0x76e);

	if (BTC_SCAN_START == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], SCAN START notify\n");
		BTC_TRACE(trace_buf);
		halbtc8723b2ant_set_ant_path(btcoexist, BTC_ANT_WIFI_AT_MAIN, FALSE, FALSE);
	} else if (BTC_SCAN_FINISH == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], SCAN FINISH notify\n");
		BTC_TRACE(trace_buf);
		btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM,
				   &coex_sta->scan_ap_num);
	}

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"############# [BTCoex], 0x948=0x%x, 0x765=0x%x, 0x76e=0x%x\n",
		    u32tmp,  u8tmpa, u8tmpb);
	BTC_TRACE(trace_buf);
}

void ex_halbtc8723b2ant_connect_notify(IN struct btc_coexist *btcoexist,
				       IN u8 type)
{
	if (BTC_ASSOCIATE_START == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], CONNECT START notify\n");
		BTC_TRACE(trace_buf);
		halbtc8723b2ant_set_ant_path(btcoexist, BTC_ANT_WIFI_AT_MAIN, FALSE, FALSE);
	} else if (BTC_ASSOCIATE_FINISH == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], CONNECT FINISH notify\n");
		BTC_TRACE(trace_buf);
	}
}

void ex_halbtc8723b2ant_media_status_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	u8			h2c_parameter[3] = {0};
	u32			wifi_bw;
	u8			wifi_central_chnl;
	u8			ap_num = 0;

	if (BTC_MEDIA_CONNECT == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], MEDIA connect notify\n");
		BTC_TRACE(trace_buf);
		halbtc8723b2ant_set_ant_path(btcoexist, BTC_ANT_WIFI_AT_MAIN, FALSE, FALSE);
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
		else {
			btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM,
					   &ap_num);
			if (ap_num < 10)
				h2c_parameter[2] = 0x30;
			else
				h2c_parameter[2] = 0x20;
		}
	}

	coex_dm->wifi_chnl_info[0] = h2c_parameter[0];
	coex_dm->wifi_chnl_info[1] = h2c_parameter[1];
	coex_dm->wifi_chnl_info[2] = h2c_parameter[2];

	btcoexist->btc_fill_h2c(btcoexist, 0x66, 3, h2c_parameter);
}

void ex_halbtc8723b2ant_specific_packet_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	if (type == BTC_PACKET_DHCP) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], DHCP Packet notify\n");
		BTC_TRACE(trace_buf);
	}
}

void ex_halbtc8723b2ant_bt_info_notify(IN struct btc_coexist *btcoexist,
				       IN u8 *tmp_buf, IN u8 length)
{
	u8			bt_info = 0;
	u8			i, rsp_source = 0;
	boolean			bt_busy = false, limited_dig = false;
	boolean			wifi_connected = false;

	coex_sta->c2h_bt_info_req_sent = false;

	rsp_source = tmp_buf[0] & 0xf;
	if (rsp_source >= BT_INFO_SRC_8723B_2ANT_MAX)
		rsp_source = BT_INFO_SRC_8723B_2ANT_WIFI_FW;
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

	if (btcoexist->manual_control) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], BtInfoNotify(), return for Manual CTRL<===\n");
		BTC_TRACE(trace_buf);
		return;
	}

	/* if 0xff, it means BT is under WHCK test */
	if (bt_info == 0xff)
		coex_sta->bt_whck_test = true;
	else
		coex_sta->bt_whck_test = false;

	if (BT_INFO_SRC_8723B_2ANT_WIFI_FW != rsp_source) {
		coex_sta->bt_retry_cnt =	/* [3:0] */
			coex_sta->bt_info_c2h[rsp_source][2] & 0xf;

		if (coex_sta->bt_retry_cnt >= 1)
			coex_sta->pop_event_cnt++;

		coex_sta->bt_rssi =
			coex_sta->bt_info_c2h[rsp_source][3] * 2 + 10;

		coex_sta->bt_info_ext =
			coex_sta->bt_info_c2h[rsp_source][4];

		if (coex_sta->bt_info_c2h[rsp_source][2] & 0x20)
			coex_sta->c2h_bt_remote_name_req = true;
		else
			coex_sta->c2h_bt_remote_name_req = false;

		if (coex_sta->bt_info_c2h[rsp_source][1] == 0x49) {
			coex_sta->a2dp_bit_pool =
				coex_sta->bt_info_c2h[rsp_source][6];
		} else
			coex_sta->a2dp_bit_pool = 0;

		coex_sta->bt_tx_rx_mask = (coex_sta->bt_info_c2h[rsp_source][2]
					   & 0x40);
		btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TX_RX_MASK,
				   &coex_sta->bt_tx_rx_mask);
		if (coex_sta->bt_tx_rx_mask) {
			/* BT into is responded by BT FW and BT RF REG 0x3C != 0x01 => Need to switch BT TRx Mask */
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], Switch BT TRx Mask since BT RF REG 0x3C != 0x01\n");
			BTC_TRACE(trace_buf);
			btcoexist->btc_set_bt_reg(btcoexist, BTC_BT_REG_RF,
						  0x3c, 0x01);
		}

		/* Here we need to resend some wifi info to BT */
		/* because bt is reset and loss of the info. */
		if ((coex_sta->bt_info_ext & BIT(1))) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], BT ext info bit1 check, send wifi BW&Chnl to BT!!\n");
			BTC_TRACE(trace_buf);
			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
					   &wifi_connected);
			if (wifi_connected)
				ex_halbtc8723b2ant_media_status_notify(
					btcoexist, BTC_MEDIA_CONNECT);
			else
				ex_halbtc8723b2ant_media_status_notify(
					btcoexist, BTC_MEDIA_DISCONNECT);
		}

		if ((coex_sta->bt_info_ext & BIT(3))) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], BT ext info bit3 check, set BT NOT to ignore Wlan active!!\n");
			BTC_TRACE(trace_buf);
			halbtc8723b2ant_ignore_wlan_act(btcoexist, FORCE_EXEC,
							false);
		} else {
			/* BT already NOT ignore Wlan active, do nothing here. */
		}
#if (BT_AUTO_REPORT_ONLY_8723B_2ANT == 0)
		if ((coex_sta->bt_info_ext & BIT(4))) {
			/* BT auto report already enabled, do nothing */
		} else
			halbtc8723b2ant_bt_auto_report(btcoexist, FORCE_EXEC,
						       true);
#endif
	}

	/* check BIT2 first ==> check if bt is under inquiry or page scan */
	if (bt_info & BT_INFO_8723B_2ANT_B_INQ_PAGE)
		coex_sta->c2h_bt_inquiry_page = true;
	else
		coex_sta->c2h_bt_inquiry_page = false;

	/* set link exist status */
	if (!(bt_info & BT_INFO_8723B_2ANT_B_CONNECTION)) {
		coex_sta->bt_link_exist = false;
		coex_sta->pan_exist = false;
		coex_sta->a2dp_exist = false;
		coex_sta->hid_exist = false;
		coex_sta->sco_exist = false;
	} else {	/* connection exists */
		coex_sta->bt_link_exist = true;
		if (bt_info & BT_INFO_8723B_2ANT_B_FTP)
			coex_sta->pan_exist = true;
		else
			coex_sta->pan_exist = false;
		if (bt_info & BT_INFO_8723B_2ANT_B_A2DP)
			coex_sta->a2dp_exist = true;
		else
			coex_sta->a2dp_exist = false;
		if (bt_info & BT_INFO_8723B_2ANT_B_HID)
			coex_sta->hid_exist = true;
		else
			coex_sta->hid_exist = false;
		if (bt_info & BT_INFO_8723B_2ANT_B_SCO_ESCO)
			coex_sta->sco_exist = true;
		else
			coex_sta->sco_exist = false;

		if ((coex_sta->hid_exist == false) &&
		    (coex_sta->c2h_bt_inquiry_page == false) &&
		    (coex_sta->sco_exist == false)) {
			if (coex_sta->high_priority_tx  +
			    coex_sta->high_priority_rx >= 160) {
				coex_sta->hid_exist = true;
				bt_info = bt_info | 0x28;
			}
		}
	}

	halbtc8723b2ant_update_bt_link_info(btcoexist);

	if (!(bt_info & BT_INFO_8723B_2ANT_B_CONNECTION)) {
		coex_dm->bt_status = BT_8723B_2ANT_BT_STATUS_NON_CONNECTED_IDLE;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], BtInfoNotify(), BT Non-Connected idle!!!\n");
		BTC_TRACE(trace_buf);
	} else if (bt_info ==
		BT_INFO_8723B_2ANT_B_CONNECTION) {	/* connection exists but no busy */
		coex_dm->bt_status = BT_8723B_2ANT_BT_STATUS_CONNECTED_IDLE;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT Connected-idle!!!\n");
		BTC_TRACE(trace_buf);
	} else if ((bt_info & BT_INFO_8723B_2ANT_B_SCO_ESCO) ||
		   (bt_info & BT_INFO_8723B_2ANT_B_SCO_BUSY)) {
		coex_dm->bt_status = BT_8723B_2ANT_BT_STATUS_SCO_BUSY;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT SCO busy!!!\n");
		BTC_TRACE(trace_buf);
	} else if (bt_info & BT_INFO_8723B_2ANT_B_ACL_BUSY) {
		coex_dm->bt_status = BT_8723B_2ANT_BT_STATUS_ACL_BUSY;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BtInfoNotify(), BT ACL busy!!!\n");
		BTC_TRACE(trace_buf);
	} else {
		coex_dm->bt_status = BT_8723B_2ANT_BT_STATUS_MAX;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], BtInfoNotify(), BT Non-Defined state!!!\n");
		BTC_TRACE(trace_buf);
	}

	if ((BT_8723B_2ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) ||
	    (BT_8723B_2ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
	    (BT_8723B_2ANT_BT_STATUS_ACL_SCO_BUSY == coex_dm->bt_status)) {
		bt_busy = true;
		limited_dig = true;
	} else {
		bt_busy = false;
		limited_dig = false;
	}

	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bt_busy);

	coex_dm->limited_dig = limited_dig;
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_LIMITED_DIG, &limited_dig);

	halbtc8723b2ant_run_coexist_mechanism(btcoexist);
}

void ex_halbtc8723b2ant_halt_notify(IN struct btc_coexist *btcoexist)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Halt notify\n");
	BTC_TRACE(trace_buf);

	halbtc8723b2ant_wifi_off_hw_cfg(btcoexist);
	/* remove due to interrupt is disabled that polling c2h will fail and delay 100ms. */
	/* btcoexist->btc_set_bt_reg(btcoexist, BTC_BT_REG_RF, 0x3c, 0x15); //BT goto standby while GNT_BT 1-->0 */
	halbtc8723b2ant_ignore_wlan_act(btcoexist, FORCE_EXEC, true);

	ex_halbtc8723b2ant_media_status_notify(btcoexist, BTC_MEDIA_DISCONNECT);
}

void ex_halbtc8723b2ant_pnp_notify(IN struct btc_coexist *btcoexist,
				   IN u8 pnp_state)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Pnp notify\n");
	BTC_TRACE(trace_buf);

	if (BTC_WIFI_PNP_SLEEP == pnp_state) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Pnp notify to SLEEP\n");
		BTC_TRACE(trace_buf);

		/* Sinda 20150819, workaround for driver skip leave IPS/LPS to speed up sleep time. */
		/* Driver do not leave IPS/LPS when driver is going to sleep, so BTCoexistence think wifi is still under IPS/LPS */
		/* BT should clear UnderIPS/UnderLPS state to avoid mismatch state after wakeup. */
		coex_sta->under_ips = false;
		coex_sta->under_lps = false;
	} else if (BTC_WIFI_PNP_WAKE_UP == pnp_state) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Pnp notify to WAKE UP\n");
		BTC_TRACE(trace_buf);
		halbtc8723b2ant_init_hw_config(btcoexist, false);
		halbtc8723b2ant_init_coex_dm(btcoexist);
		halbtc8723b2ant_query_bt_info(btcoexist);
	}
}

void ex_halbtc8723b2ant_periodical(IN struct btc_coexist *btcoexist)
{
	struct  btc_bt_link_info	*bt_link_info = &btcoexist->bt_link_info;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		"[BTCoex], ==========================Periodical===========================\n");
	BTC_TRACE(trace_buf);
	if (coex_sta->dis_ver_info_cnt <= 5) {
		coex_sta->dis_ver_info_cnt += 1;
		if (coex_sta->dis_ver_info_cnt == 3) {
			/* Antenna config to set 0x765 = 0x0 (GNT_BT control by PTA) after initial */
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Set GNT_BT control by PTA\n");
			BTC_TRACE(trace_buf);
			halbtc8723b2ant_set_ant_path(btcoexist,
				     BTC_ANT_WIFI_AT_MAIN, false, false);
		}
	}

#if (BT_AUTO_REPORT_ONLY_8723B_2ANT == 0)
	halbtc8723b2ant_query_bt_info(btcoexist);
	halbtc8723b2ant_monitor_bt_enable_disable(btcoexist);
#else
	halbtc8723b2ant_monitor_bt_ctr(btcoexist);
	halbtc8723b2ant_monitor_wifi_ctr(btcoexist);

	/* for some BT speaker that Hi-Pri pkt appear begore start play, this will cause HID exist */
	if ((coex_sta->high_priority_tx  + coex_sta->high_priority_rx < 50) &&
	    (bt_link_info->hid_exist == true))
		bt_link_info->hid_exist  = false;

	if (halbtc8723b2ant_is_wifi_status_changed(btcoexist) ||
	    coex_dm->auto_tdma_adjust)
		halbtc8723b2ant_run_coexist_mechanism(btcoexist);
#endif
}

#endif

#endif	/* #if (BT_SUPPORT == 1 && COEX_SUPPORT == 1) */

