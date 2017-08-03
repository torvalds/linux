/* ************************************************************
 * Description:
 *
 * This file is for RTL8821A_CSR_CSR Co-exist mechanism
 *
 * History
 * 2012/08/22 Cosa first check in.
 * 2012/11/14 Cosa Revise for 8821A_CSR 2Ant out sourcing.
 *
 * ************************************************************ */

/* ************************************************************
 * include files
 * ************************************************************ */
#include "Mp_Precomp.h"

#if (BT_SUPPORT == 1 && COEX_SUPPORT == 1)

#if (RTL8821A_SUPPORT == 1)

#define _BTCOEX_CSR 1

#ifndef rtw_warn_on_8821acsr2ant
#define rtw_warn_on_8821acsr2ant(condition) do {} while (0)
#endif
/* ************************************************************
 * Global variables, these are static variables
 * ************************************************************ */
static u8	 *trace_buf = &gl_btc_trace_buf[0];
static struct  coex_dm_8821a_csr_2ant	glcoex_dm_8821a_csr_2ant;
static struct  coex_dm_8821a_csr_2ant	*coex_dm = &glcoex_dm_8821a_csr_2ant;
static struct  coex_sta_8821a_csr_2ant	glcoex_sta_8821a_csr_2ant;
static struct  coex_sta_8821a_csr_2ant	*coex_sta = &glcoex_sta_8821a_csr_2ant;

const char *const glbt_info_src_8821a_csr_2ant[] = {
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

u32 glcoex_ver_date_8821a_csr_2ant = 20140901;
u32 glcoex_ver_8821a_csr_2ant = 0x51;

/* ************************************************************
 * local function proto type if needed
 * ************************************************************
 * ************************************************************
 * local function start with halbtc8821aCsr2ant_
 * ************************************************************ */
u8 halbtc8821aCsr2ant_bt_rssi_state(u8 level_num, u8 rssi_thresh,
				    u8 rssi_thresh1)
{
	s32			bt_rssi = 0;
	u8			bt_rssi_state = coex_sta->pre_bt_rssi_state;

	bt_rssi = coex_sta->bt_rssi;

	if (level_num == 2) {
		if ((coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_bt_rssi_state ==
		     BTC_RSSI_STATE_STAY_LOW)) {
			if (bt_rssi >= (rssi_thresh +
				BTC_RSSI_COEX_THRESH_TOL_8821A_CSR_2ANT))
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
				BTC_RSSI_COEX_THRESH_TOL_8821A_CSR_2ANT))
				bt_rssi_state = BTC_RSSI_STATE_MEDIUM;
			else
				bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
		} else if ((coex_sta->pre_bt_rssi_state ==
			    BTC_RSSI_STATE_MEDIUM) ||
			   (coex_sta->pre_bt_rssi_state ==
			    BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (bt_rssi >= (rssi_thresh1 +
				BTC_RSSI_COEX_THRESH_TOL_8821A_CSR_2ANT))
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

u8 halbtc8821aCsr2ant_wifi_rssi_state(IN struct btc_coexist *btcoexist,
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
				  BTC_RSSI_COEX_THRESH_TOL_8821A_CSR_2ANT))
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
				  BTC_RSSI_COEX_THRESH_TOL_8821A_CSR_2ANT))
				wifi_rssi_state = BTC_RSSI_STATE_MEDIUM;
			else
				wifi_rssi_state = BTC_RSSI_STATE_STAY_LOW;
		} else if ((coex_sta->pre_wifi_rssi_state[index] ==
			    BTC_RSSI_STATE_MEDIUM) ||
			   (coex_sta->pre_wifi_rssi_state[index] ==
			    BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (wifi_rssi >= (rssi_thresh1 +
				  BTC_RSSI_COEX_THRESH_TOL_8821A_CSR_2ANT))
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

void halbtc8821aCsr2ant_monitor_bt_enable_disable(IN struct btc_coexist
		*btcoexist)
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
		if (!bt_disabled) {
		} else {
		}
	}
}

void halbtc8821aCsr2ant_monitor_bt_ctr(IN struct btc_coexist *btcoexist)
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
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0x5d);
}

void halbtc8821aCsr2ant_update_ra_mask(IN struct btc_coexist *btcoexist,
			       IN boolean force_exec, IN u32 dis_rate_mask)
{
	coex_dm->cur_ra_mask = dis_rate_mask;

	if (force_exec || (coex_dm->pre_ra_mask != coex_dm->cur_ra_mask))
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_UPDATE_RAMASK,
				   &coex_dm->cur_ra_mask);
	coex_dm->pre_ra_mask = coex_dm->cur_ra_mask;
}

void halbtc8821aCsr2ant_auto_rate_fallback_retry(IN struct btc_coexist
		*btcoexist, IN boolean force_exec, IN u8 type)
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

void halbtc8821aCsr2ant_retry_limit(IN struct btc_coexist *btcoexist,
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

void halbtc8821aCsr2ant_ampdu_max_time(IN struct btc_coexist *btcoexist,
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
		case 2:
			btcoexist->btc_write_1byte(btcoexist, 0x456,
						   0x17);
			break;
		default:
			break;
		}
	}

	coex_dm->pre_ampdu_time_type = coex_dm->cur_ampdu_time_type;
}

void halbtc8821aCsr2Ant_AmpduMaxNum(IN struct btc_coexist *btcoexist,
				    IN boolean force_exec, IN u8 type)
{
	coex_dm->cur_ampdu_num_type = type;

	if (force_exec ||
	    (coex_dm->pre_ampdu_num_type != coex_dm->cur_ampdu_num_type)) {
		switch (coex_dm->cur_ampdu_num_type) {
		case 0:	/* normal mode */
			btcoexist->btc_write_2byte(btcoexist, 0x4ca,
					   coex_dm->backup_ampdu_max_num);
			break;
		case 1:
			btcoexist->btc_write_2byte(btcoexist, 0x4ca,
						   0x0808);
			break;
		case 2:
			btcoexist->btc_write_2byte(btcoexist, 0x4ca,
						   0x1f1f);
			break;
		default:
			break;
		}
	}

	coex_dm->pre_ampdu_num_type = coex_dm->cur_ampdu_num_type;

}

void halbtc8821aCsr2ant_limited_tx(IN struct btc_coexist *btcoexist,
		   IN boolean force_exec, IN u8 ra_mask_type, IN u8 arfr_type,
	   IN u8 retry_limit_type, IN u8 ampdu_time_type, IN u8 ampdu_num_type)
{
	switch (ra_mask_type) {
	case 0:	/* normal mode */
		halbtc8821aCsr2ant_update_ra_mask(btcoexist, force_exec,
						  0x0);
		break;
	case 1:	/* disable cck 1/2 */
		halbtc8821aCsr2ant_update_ra_mask(btcoexist, force_exec,
						  0x00000003);
		break;
	case 2:	/* disable cck 1/2/5.5, ofdm 6/9/12/18/24, mcs 0/1/2/3/4 */
		halbtc8821aCsr2ant_update_ra_mask(btcoexist, force_exec,
						  0x0001f1f7);
		break;
	default:
		break;
	}

	halbtc8821aCsr2ant_auto_rate_fallback_retry(btcoexist, force_exec,
			arfr_type);
	halbtc8821aCsr2ant_retry_limit(btcoexist, force_exec, retry_limit_type);
	halbtc8821aCsr2ant_ampdu_max_time(btcoexist, force_exec,
					  ampdu_time_type);
	halbtc8821aCsr2Ant_AmpduMaxNum(btcoexist, force_exec, ampdu_num_type);
}



void halbtc8821aCsr2ant_limited_rx(IN struct btc_coexist *btcoexist,
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

void halbtc8821aCsr2ant_query_bt_info(IN struct btc_coexist *btcoexist)
{
	u8			h2c_parameter[1] = {0};

	coex_sta->c2h_bt_info_req_sent = true;

	h2c_parameter[0] |= BIT(0);	/* trigger */

	rtw_warn_on_8821acsr2ant(_BTCOEX_CSR);
	btcoexist->btc_fill_h2c(btcoexist, 0x61, 1, h2c_parameter);
}

u8 halbtc8821aCsr2ant_action_algorithm(IN struct btc_coexist *btcoexist)
{
	struct  btc_stack_info		*stack_info = &btcoexist->stack_info;
	boolean				bt_hs_on = false;
	u8				algorithm = BT_8821A_CSR_2ANT_COEX_ALGO_UNDEFINED;
	u8				num_of_diff_profile = 0;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);

	/* sync StackInfo with BT firmware and stack */
	stack_info->hid_exist = coex_sta->hid_exist;
	stack_info->bt_link_exist = coex_sta->bt_link_exist;
	stack_info->sco_exist = coex_sta->sco_exist;
	stack_info->pan_exist = coex_sta->pan_exist;
	stack_info->a2dp_exist = coex_sta->a2dp_exist;

	if (!stack_info->bt_link_exist) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], No profile exists!!!\n");
		BTC_TRACE(trace_buf);
		return algorithm;
	}

	if (stack_info->sco_exist)
		num_of_diff_profile++;
	if (stack_info->hid_exist)
		num_of_diff_profile++;
	if (stack_info->pan_exist)
		num_of_diff_profile++;
	if (stack_info->a2dp_exist)
		num_of_diff_profile++;

	if (num_of_diff_profile == 1) {
		if (stack_info->sco_exist) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], SCO only\n");
			BTC_TRACE(trace_buf);
			algorithm = BT_8821A_CSR_2ANT_COEX_ALGO_SCO;
		} else {
			if (stack_info->hid_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					    "[BTCoex], HID only\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8821A_CSR_2ANT_COEX_ALGO_HID;
			} else if (stack_info->a2dp_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					    "[BTCoex], A2DP only\n");
				BTC_TRACE(trace_buf);
				algorithm = BT_8821A_CSR_2ANT_COEX_ALGO_A2DP;
			} else if (stack_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						    "[BTCoex], PAN(HS) only\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821A_CSR_2ANT_COEX_ALGO_PANHS;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], PAN(EDR) only\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821A_CSR_2ANT_COEX_ALGO_PANEDR;
				}
			}
		}
	} else if (num_of_diff_profile == 2) {
		if (stack_info->sco_exist) {
			if (stack_info->hid_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					    "[BTCoex], SCO + HID\n");
				BTC_TRACE(trace_buf);
				algorithm =
					BT_8821A_CSR_2ANT_COEX_ALGO_PANEDR_HID;
			} else if (stack_info->a2dp_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					    "[BTCoex], SCO + A2DP ==> SCO\n");
				BTC_TRACE(trace_buf);
				algorithm =
					BT_8821A_CSR_2ANT_COEX_ALGO_PANEDR_HID;
			} else if (stack_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821A_CSR_2ANT_COEX_ALGO_SCO;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821A_CSR_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (stack_info->hid_exist &&
			    stack_info->a2dp_exist) {
				if (stack_info->num_of_hid >= 2) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						    "[BTCoex], HID*2 + A2DP\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821A_CSR_2ANT_COEX_ALGO_HID_A2DP_PANEDR;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						    "[BTCoex], HID + A2DP\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821A_CSR_2ANT_COEX_ALGO_HID_A2DP;
				}
			} else if (stack_info->hid_exist &&
				   stack_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], HID + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821A_CSR_2ANT_COEX_ALGO_HID;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], HID + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821A_CSR_2ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (stack_info->pan_exist &&
				   stack_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], A2DP + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821A_CSR_2ANT_COEX_ALGO_A2DP_PANHS;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], A2DP + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821A_CSR_2ANT_COEX_ALGO_PANEDR_A2DP;
				}
			}
		}
	} else if (num_of_diff_profile == 3) {
		if (stack_info->sco_exist) {
			if (stack_info->hid_exist &&
			    stack_info->a2dp_exist) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], SCO + HID + A2DP ==> HID\n");
				BTC_TRACE(trace_buf);
				algorithm =
					BT_8821A_CSR_2ANT_COEX_ALGO_PANEDR_HID;
			} else if (stack_info->hid_exist &&
				   stack_info->pan_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + HID + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821A_CSR_2ANT_COEX_ALGO_PANEDR_HID;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + HID + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821A_CSR_2ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (stack_info->pan_exist &&
				   stack_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + A2DP + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821A_CSR_2ANT_COEX_ALGO_PANEDR_HID;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + A2DP + PAN(EDR) ==> HID\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821A_CSR_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (stack_info->hid_exist &&
			    stack_info->pan_exist &&
			    stack_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], HID + A2DP + PAN(HS)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821A_CSR_2ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], HID + A2DP + PAN(EDR)\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821A_CSR_2ANT_COEX_ALGO_HID_A2DP_PANEDR;
				}
			}
		}
	} else if (num_of_diff_profile >= 3) {
		if (stack_info->sco_exist) {
			if (stack_info->hid_exist &&
			    stack_info->pan_exist &&
			    stack_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], Error!!! SCO + HID + A2DP + PAN(HS)\n");
					BTC_TRACE(trace_buf);

				} else {
					BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
						"[BTCoex], SCO + HID + A2DP + PAN(EDR)==>PAN(EDR)+HID\n");
					BTC_TRACE(trace_buf);
					algorithm =
						BT_8821A_CSR_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
	}

	return algorithm;
}

boolean halbtc8821aCsr2ant_need_to_dec_bt_pwr(IN struct btc_coexist *btcoexist)
{
	boolean		ret = false;
	boolean		bt_hs_on = false, wifi_connected = false;
	s32		bt_hs_rssi = 0;
	u8		bt_rssi_state;

	if (!btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on))
		return false;
	if (!btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
				&wifi_connected))
		return false;
	if (!btcoexist->btc_get(btcoexist, BTC_GET_S4_HS_RSSI, &bt_hs_rssi))
		return false;

	bt_rssi_state = halbtc8821aCsr2ant_bt_rssi_state(2, 35, 0);

	if (wifi_connected) {
		if (bt_hs_on) {
			if (bt_hs_rssi > 37)
				ret = true;
		} else {
			if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
			    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
				ret = true;
		}
	}

	return ret;
}

void halbtc8821aCsr2ant_set_fw_dac_swing_level(IN struct btc_coexist *btcoexist,
		IN u8 dac_swing_lvl)
{
	u8			h2c_parameter[1] = {0};

	/* There are several type of dacswing */
	/* 0x18/ 0x10/ 0xc/ 0x8/ 0x4/ 0x6 */
	h2c_parameter[0] = dac_swing_lvl;

	btcoexist->btc_fill_h2c(btcoexist, 0x64, 1, h2c_parameter);
}

void halbtc8821aCsr2ant_set_fw_dec_bt_pwr(IN struct btc_coexist *btcoexist,
		IN boolean dec_bt_pwr)
{
	u8			h2c_parameter[1] = {0};

	h2c_parameter[0] = 0;

	if (dec_bt_pwr)
		h2c_parameter[0] |= BIT(1);

	rtw_warn_on_8821acsr2ant(_BTCOEX_CSR);
	btcoexist->btc_fill_h2c(btcoexist, 0x62, 1, h2c_parameter);
}

void halbtc8821aCsr2ant_dec_bt_pwr(IN struct btc_coexist *btcoexist,
				   IN boolean force_exec, IN boolean dec_bt_pwr)
{
	coex_dm->cur_dec_bt_pwr = dec_bt_pwr;

	if (!force_exec) {
		if (coex_dm->pre_dec_bt_pwr == coex_dm->cur_dec_bt_pwr)
			return;
	}

	/* TODO: may CSR consider to decrease BT power? */
	/* halbtc8821aCsr2ant_set_fw_dec_bt_pwr(btcoexist, coex_dm->cur_dec_bt_pwr); */

	coex_dm->pre_dec_bt_pwr = coex_dm->cur_dec_bt_pwr;
}

void halbtc8821aCsr2ant_set_bt_auto_report(IN struct btc_coexist *btcoexist,
		IN boolean enable_auto_report)
{
	u8			h2c_parameter[1] = {0};

	h2c_parameter[0] = 0;

	if (enable_auto_report)
		h2c_parameter[0] |= BIT(0);

	rtw_warn_on_8821acsr2ant(_BTCOEX_CSR);
	btcoexist->btc_fill_h2c(btcoexist, 0x68, 1, h2c_parameter);
}

void halbtc8821aCsr2ant_bt_auto_report(IN struct btc_coexist *btcoexist,
		       IN boolean force_exec, IN boolean enable_auto_report)
{
	coex_dm->cur_bt_auto_report = enable_auto_report;

	if (!force_exec) {
		if (coex_dm->pre_bt_auto_report == coex_dm->cur_bt_auto_report)
			return;
	}
	/* halbtc8821aCsr2ant_set_bt_auto_report(btcoexist, coex_dm->cur_bt_auto_report); */

	coex_dm->pre_bt_auto_report = coex_dm->cur_bt_auto_report;
}

void halbtc8821aCsr2ant_fw_dac_swing_lvl(IN struct btc_coexist *btcoexist,
		IN boolean force_exec, IN u8 fw_dac_swing_lvl)
{
	coex_dm->cur_fw_dac_swing_lvl = fw_dac_swing_lvl;

	if (!force_exec) {
		if (coex_dm->pre_fw_dac_swing_lvl ==
		    coex_dm->cur_fw_dac_swing_lvl)
			return;
	}

	halbtc8821aCsr2ant_set_fw_dac_swing_level(btcoexist,
			coex_dm->cur_fw_dac_swing_lvl);

	coex_dm->pre_fw_dac_swing_lvl = coex_dm->cur_fw_dac_swing_lvl;
}

void halbtc8821aCsr2ant_set_sw_rf_rx_lpf_corner(IN struct btc_coexist
		*btcoexist, IN boolean rx_rf_shrink_on)
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

void halbtc8821aCsr2ant_rf_shrink(IN struct btc_coexist *btcoexist,
			  IN boolean force_exec, IN boolean rx_rf_shrink_on)
{
	coex_dm->cur_rf_rx_lpf_shrink = rx_rf_shrink_on;

	if (!force_exec) {
		if (coex_dm->pre_rf_rx_lpf_shrink ==
		    coex_dm->cur_rf_rx_lpf_shrink)
			return;
	}
	halbtc8821aCsr2ant_set_sw_rf_rx_lpf_corner(btcoexist,
			coex_dm->cur_rf_rx_lpf_shrink);

	coex_dm->pre_rf_rx_lpf_shrink = coex_dm->cur_rf_rx_lpf_shrink;
}

void halbtc8821aCsr2ant_set_sw_penalty_tx_rate_adaptive(
	IN struct btc_coexist *btcoexist, IN boolean low_penalty_ra)
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

void halbtc8821aCsr2ant_low_penalty_ra(IN struct btc_coexist *btcoexist,
			       IN boolean force_exec, IN boolean low_penalty_ra)
{
	coex_dm->cur_low_penalty_ra = low_penalty_ra;

	if (!force_exec) {
		if (coex_dm->pre_low_penalty_ra == coex_dm->cur_low_penalty_ra)
			return;
	}
	halbtc8821aCsr2ant_set_sw_penalty_tx_rate_adaptive(btcoexist,
			coex_dm->cur_low_penalty_ra);

	coex_dm->pre_low_penalty_ra = coex_dm->cur_low_penalty_ra;
}

void halbtc8821aCsr2ant_set_dac_swing_reg(IN struct btc_coexist *btcoexist,
		IN u32 level)
{
	u8	val = (u8)level;

	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], Write SwDacSwing = 0x%x\n", level);
	BTC_TRACE(trace_buf);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0xc5b, 0x3e, val);
}

void halbtc8821aCsr2ant_set_sw_full_time_dac_swing(IN struct btc_coexist
		*btcoexist, IN boolean sw_dac_swing_on, IN u32 sw_dac_swing_lvl)
{
	if (sw_dac_swing_on)
		halbtc8821aCsr2ant_set_dac_swing_reg(btcoexist,
						     sw_dac_swing_lvl);
	else
		halbtc8821aCsr2ant_set_dac_swing_reg(btcoexist, 0x18);
}


void halbtc8821aCsr2ant_dac_swing(IN struct btc_coexist *btcoexist,
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
	halbtc8821aCsr2ant_set_sw_full_time_dac_swing(btcoexist, dac_swing_on,
			dac_swing_lvl);

	coex_dm->pre_dac_swing_on = coex_dm->cur_dac_swing_on;
	coex_dm->pre_dac_swing_lvl = coex_dm->cur_dac_swing_lvl;
}

void halbtc8821aCsr2ant_set_adc_back_off(IN struct btc_coexist *btcoexist,
		IN boolean adc_back_off)
{
	if (adc_back_off) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BB BackOff Level On!\n");
		BTC_TRACE(trace_buf);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x8db, 0x60, 0x3);
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], BB BackOff Level Off!\n");
		BTC_TRACE(trace_buf);
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x8db, 0x60, 0x1);
	}
}

void halbtc8821aCsr2ant_adc_back_off(IN struct btc_coexist *btcoexist,
			     IN boolean force_exec, IN boolean adc_back_off)
{
	coex_dm->cur_adc_back_off = adc_back_off;

	if (!force_exec) {
		if (coex_dm->pre_adc_back_off == coex_dm->cur_adc_back_off)
			return;
	}
	halbtc8821aCsr2ant_set_adc_back_off(btcoexist,
					    coex_dm->cur_adc_back_off);

	coex_dm->pre_adc_back_off = coex_dm->cur_adc_back_off;
}

void halbtc8821aCsr2ant_set_agc_table(IN struct btc_coexist *btcoexist,
				      IN boolean agc_table_en)
{
	u8		rssi_adjust_val = 0;

	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xef, 0xfffff, 0x02000);
	if (agc_table_en) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Agc Table On!\n");
		BTC_TRACE(trace_buf);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b, 0xfffff,
					  0x28F4B);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b, 0xfffff,
					  0x10AB2);
		rssi_adjust_val = 8;
	} else {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Agc Table Off!\n");
		BTC_TRACE(trace_buf);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b, 0xfffff,
					  0x2884B);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b, 0xfffff,
					  0x104B2);
	}
	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xef, 0xfffff, 0x0);

	/* set rssi_adjust_val for wifi module. */
	btcoexist->btc_set(btcoexist, BTC_SET_U1_RSSI_ADJ_VAL_FOR_AGC_TABLE_ON,
			   &rssi_adjust_val);
}

void halbtc8821aCsr2ant_agc_table(IN struct btc_coexist *btcoexist,
			  IN boolean force_exec, IN boolean agc_table_en)
{
	coex_dm->cur_agc_table_en = agc_table_en;

	if (!force_exec) {
		if (coex_dm->pre_agc_table_en == coex_dm->cur_agc_table_en)
			return;
	}
	halbtc8821aCsr2ant_set_agc_table(btcoexist, agc_table_en);

	coex_dm->pre_agc_table_en = coex_dm->cur_agc_table_en;
}

void halbtc8821aCsr2ant_set_coex_table(IN struct btc_coexist *btcoexist,
	IN u32 val0x6c0, IN u32 val0x6c4, IN u32 val0x6c8, IN u8 val0x6cc)
{
	btcoexist->btc_write_4byte(btcoexist, 0x6c0, val0x6c0);

	btcoexist->btc_write_4byte(btcoexist, 0x6c4, val0x6c4);

	btcoexist->btc_write_4byte(btcoexist, 0x6c8, val0x6c8);

	btcoexist->btc_write_1byte(btcoexist, 0x6cc, val0x6cc);
}

void halbtc8821aCsr2ant_coex_table(IN struct btc_coexist *btcoexist,
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
	halbtc8821aCsr2ant_set_coex_table(btcoexist, val0x6c0, val0x6c4,
					  val0x6c8, val0x6cc);

	coex_dm->pre_val0x6c0 = coex_dm->cur_val0x6c0;
	coex_dm->pre_val0x6c4 = coex_dm->cur_val0x6c4;
	coex_dm->pre_val0x6c8 = coex_dm->cur_val0x6c8;
	coex_dm->pre_val0x6cc = coex_dm->cur_val0x6cc;
}

void halbtc8821aCsr2ant_set_fw_ignore_wlan_act(IN struct btc_coexist *btcoexist,
		IN boolean enable)
{
	u8			h2c_parameter[1] = {0};

	if (enable) {
		h2c_parameter[0] |= BIT(0);		/* function enable */
	}

	rtw_warn_on_8821acsr2ant(_BTCOEX_CSR);
	btcoexist->btc_fill_h2c(btcoexist, 0x63, 1, h2c_parameter);
}

void halbtc8821aCsr2ant_ignore_wlan_act(IN struct btc_coexist *btcoexist,
				IN boolean force_exec, IN boolean enable)
{
	coex_dm->cur_ignore_wlan_act = enable;

	if (!force_exec) {
		if (coex_dm->pre_ignore_wlan_act ==
		    coex_dm->cur_ignore_wlan_act)
			return;
	}
	/* halbtc8821aCsr2ant_set_fw_ignore_wlan_act(btcoexist, enable); */

	coex_dm->pre_ignore_wlan_act = coex_dm->cur_ignore_wlan_act;
}

void halbtc8821aCsr2ant_set_fw_pstdma(IN struct btc_coexist *btcoexist,
	      IN u8 byte1, IN u8 byte2, IN u8 byte3, IN u8 byte4, IN u8 byte5)
{
	u8			h2c_parameter[6] = {0};

	h2c_parameter[0] = byte1;
	h2c_parameter[1] = byte2;
	h2c_parameter[2] = byte3;
	h2c_parameter[3] = byte4;
	h2c_parameter[4] = byte5;
	h2c_parameter[5] = 0x01;

	coex_dm->ps_tdma_para[0] = byte1;
	coex_dm->ps_tdma_para[1] = byte2;
	coex_dm->ps_tdma_para[2] = byte3;
	coex_dm->ps_tdma_para[3] = byte4;
	coex_dm->ps_tdma_para[4] = byte5;
	coex_dm->ps_tdma_para[5] = 0x01;

	btcoexist->btc_fill_h2c(btcoexist, 0x60, 6, h2c_parameter);
}

void halbtc8821aCsr2ant_sw_mechanism1(IN struct btc_coexist *btcoexist,
		      IN boolean shrink_rx_lpf, IN boolean low_penalty_ra,
		      IN boolean limited_dig, IN boolean bt_lna_constrain)
{
	u32	wifi_bw;

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_HT40 != wifi_bw) { /* only shrink RF Rx LPF for HT40 */
		if (shrink_rx_lpf)
			shrink_rx_lpf = false;
	}

	halbtc8821aCsr2ant_rf_shrink(btcoexist, NORMAL_EXEC, shrink_rx_lpf);
	halbtc8821aCsr2ant_low_penalty_ra(btcoexist, NORMAL_EXEC,
					  low_penalty_ra);

	/* no limited DIG */
	/* halbtc8821aCsr2ant_setBtLnaConstrain(btcoexist, NORMAL_EXEC, bt_lna_constrain); */
}

void halbtc8821aCsr2ant_sw_mechanism2(IN struct btc_coexist *btcoexist,
		      IN boolean agc_table_shift, IN boolean adc_back_off,
			      IN boolean sw_dac_swing, IN u32 dac_swing_lvl)
{
	/* halbtc8821aCsr2ant_agc_table(btcoexist, NORMAL_EXEC, agc_table_shift); */
	halbtc8821aCsr2ant_adc_back_off(btcoexist, NORMAL_EXEC, adc_back_off);
	halbtc8821aCsr2ant_dac_swing(btcoexist, NORMAL_EXEC, sw_dac_swing,
				     dac_swing_lvl);
}

void halbtc8821aCsr2ant_set_ant_path(IN struct btc_coexist *btcoexist,
	     IN u8 ant_pos_type, IN boolean init_hwcfg, IN boolean wifi_off)
{
	struct  btc_board_info	*board_info = &btcoexist->board_info;
	u32				u32tmp = 0;
	u8				h2c_parameter[2] = {0};

	if (init_hwcfg) {
		/* 0x4c[23]=0, 0x4c[24]=1  Antenna control by WL/BT */
		u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x4c);
		u32tmp &= ~BIT(23);
		u32tmp |= BIT(24);
		btcoexist->btc_write_4byte(btcoexist, 0x4c, u32tmp);

		btcoexist->btc_write_4byte(btcoexist, 0x974, 0x3ff);
		btcoexist->btc_write_1byte(btcoexist, 0xcb4, 0x77);

		if (board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT) {
			/* tell firmware "antenna inverse"  ==> WRONG firmware antenna control code.==>need fw to fix */
			h2c_parameter[0] = 1;
			h2c_parameter[1] = 1;
			btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
						h2c_parameter);
		} else {
			/* tell firmware "no antenna inverse" ==> WRONG firmware antenna control code.==>need fw to fix */
			h2c_parameter[0] = 0;
			h2c_parameter[1] = 1;
			btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
						h2c_parameter);
		}
	}

	/* ext switch setting */
	switch (ant_pos_type) {
	case BTC_ANT_WIFI_AT_MAIN:
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcb7,
						   0x30, 0x1);
		break;
	case BTC_ANT_WIFI_AT_AUX:
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcb7,
						   0x30, 0x2);
		break;
	}
}

void halbtc8821aCsr2ant_ps_tdma(IN struct btc_coexist *btcoexist,
			IN boolean force_exec, IN boolean turn_on, IN u8 type)
{
	coex_dm->cur_ps_tdma_on = turn_on;
	coex_dm->cur_ps_tdma = type;

	if (!force_exec) {
		if ((coex_dm->pre_ps_tdma_on == coex_dm->cur_ps_tdma_on) &&
		    (coex_dm->pre_ps_tdma == coex_dm->cur_ps_tdma))
			return;
	}
	if (turn_on) {
		switch (type) {
		case 1:
		default:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xe3, 0x1a, 0x1a, 0xe1, 0x90);
			break;
		case 2:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xe3, 0x12, 0x12, 0xe1, 0x90);
			break;
		case 3:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xe3, 0x1c, 0x3, 0xf1, 0x90);
			break;
		case 4:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xe3, 0x10, 0x03, 0xf1, 0x90);
			break;
		case 5:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xe3, 0x1a, 0x1a, 0x60, 0x90);
			break;
		case 6:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xe3, 0x12, 0x12, 0x60, 0x90);
			break;
		case 7:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xe3, 0x1c, 0x3, 0x70, 0x90);
			break;
		case 8:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xa3, 0x10, 0x3, 0x70, 0x90);
			break;
		case 9:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xe3, 0x1a, 0x1a, 0xe1, 0x90);
			break;
		case 10:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xe3, 0x12, 0x12, 0xe1, 0x90);
			break;
		case 11:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xe3, 0xa, 0xa, 0xe1, 0x90);
			break;
		case 12:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xe3, 0x5, 0x5, 0xe1, 0x90);
			break;
		case 13:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xe3, 0x1a, 0x1a, 0x60, 0x90);
			break;
		case 14:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xe3, 0x12, 0x12, 0x60, 0x90);
			break;
		case 15:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xe3, 0xa, 0xa, 0x60, 0x90);
			break;
		case 16:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xe3, 0x5, 0x5, 0x60, 0x90);
			break;
		case 17:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xa3, 0x2f, 0x2f, 0x60, 0x90);
			break;
		case 18:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xe3, 0x5, 0x5, 0xe1, 0x90);
			break;
		case 19:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xe3, 0x25, 0x25, 0xe1, 0x90);
			break;
		case 20:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xe3, 0x25, 0x25, 0x60, 0x90);
			break;
		case 21:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xe3, 0x15, 0x03, 0x70, 0x90);
			break;
		case 22:	/* ad2dp master */
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xeb, 0x11, 0x11, 0x21, 0x10);
			break;
		case 23:	/* a2dp slave */
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xeb, 0x12, 0x12, 0x20, 0x10);
			break;
		case 71:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist,
						 0xe3, 0x1a, 0x1a, 0xe1, 0x90);
			break;
		}
	} else {
		/* disable PS tdma */
		switch (type) {
		case 0:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist, 0x0,
							 0x0, 0x0, 0x40, 0x0);
			break;
		case 1:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist, 0x0,
							 0x0, 0x0, 0x48, 0x0);
			break;
		default:
			halbtc8821aCsr2ant_set_fw_pstdma(btcoexist, 0x0,
							 0x0, 0x0, 0x40, 0x0);
			break;
		}
	}

	/* update pre state */
	coex_dm->pre_ps_tdma_on = coex_dm->cur_ps_tdma_on;
	coex_dm->pre_ps_tdma = coex_dm->cur_ps_tdma;
}

void halbtc8821aCsr2ant_coex_all_off(IN struct btc_coexist *btcoexist)
{
	/* fw all off */
	halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
	halbtc8821aCsr2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
	halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	/* sw all off */
	halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);

	/* hw all off */
	halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55555555,
				      0x55555555, 0xffff, 0x3);
}

void halbtc8821aCsr2ant_coex_under_5g(IN struct btc_coexist *btcoexist)
{
	halbtc8821aCsr2ant_coex_all_off(btcoexist);

	halbtc8821aCsr2ant_ignore_wlan_act(btcoexist, NORMAL_EXEC, true);
}

void halbtc8821aCsr2ant_init_coex_dm(IN struct btc_coexist *btcoexist)
{
	/* force to reset coex mechanism */
	halbtc8821aCsr2ant_coex_table(btcoexist, FORCE_EXEC, 0x55555555,
				      0x55555555, 0xffff, 0x3);

	halbtc8821aCsr2ant_ps_tdma(btcoexist, FORCE_EXEC, false, 1);
	halbtc8821aCsr2ant_fw_dac_swing_lvl(btcoexist, FORCE_EXEC, 6);
	halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, FORCE_EXEC, false);

	halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false, false, false, 0x18);
}

void halbtc8821aCsr2ant_bt_inquiry_page(IN struct btc_coexist *btcoexist)
{
	boolean	low_pwr_disable = true;

	btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
			   &low_pwr_disable);

	halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55ff55ff,
				      0x5afa5afa, 0xffff, 0x3);
	halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 3);
}
boolean halbtc8821aCsr2ant_is_common_action(IN struct btc_coexist *btcoexist)
{
	boolean			common = false, wifi_connected = false, wifi_busy = false;
	boolean			low_pwr_disable = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	if (!wifi_connected &&
	    BT_8821A_CSR_2ANT_BT_STATUS_IDLE == coex_dm->bt_status) {
		low_pwr_disable = false;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Wifi IPS + BT IPS!!\n");
		BTC_TRACE(trace_buf);


		halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
		halbtc8821aCsr2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

		halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false, false, false,
						 false);
		halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false, false, false,
						 0x18);
		halbtc8821aCsr2ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0,
					      0, 0);
		halbtc8821aCsr2ant_limited_rx(btcoexist, NORMAL_EXEC, 0, 0, 0);

		common = true;
	} else if (wifi_connected &&
		   (BT_8821A_CSR_2ANT_BT_STATUS_IDLE ==
		    coex_dm->bt_status)) {
		low_pwr_disable = false;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);

		if (wifi_busy) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Wifi Busy + BT IPS!!\n");
			BTC_TRACE(trace_buf);
			halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						   false, 1);
		} else {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Wifi LPS + BT IPS!!\n");
			BTC_TRACE(trace_buf);
			halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						   false, 1);
		}

		halbtc8821aCsr2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

		halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false, false, false,
						 false);
		halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false, false, false,
						 0x18);
		halbtc8821aCsr2ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0,
					      0, 0);
		halbtc8821aCsr2ant_limited_rx(btcoexist, NORMAL_EXEC, 0, 0, 0);

		common = true;
	} else if (!wifi_connected &&
		   (BT_8821A_CSR_2ANT_BT_STATUS_CONNECTED_IDLE ==
		    coex_dm->bt_status)) {
		low_pwr_disable = true;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Wifi IPS + BT LPS!!\n");
		BTC_TRACE(trace_buf);

		halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);
		halbtc8821aCsr2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

		halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false, false, false,
						 false);
		halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false, false, false,
						 0x18);
		halbtc8821aCsr2ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0,
					      0, 0);
		halbtc8821aCsr2ant_limited_rx(btcoexist, NORMAL_EXEC, 0, 0, 0);

		common = true;
	} else if (wifi_connected &&
		   (BT_8821A_CSR_2ANT_BT_STATUS_CONNECTED_IDLE ==
		    coex_dm->bt_status)) {
		low_pwr_disable = true;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);

		if (wifi_busy) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Wifi Busy + BT LPS!!\n");
			BTC_TRACE(trace_buf);
			halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						   false, 1);
		} else {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Wifi LPS + BT LPS!!\n");
			BTC_TRACE(trace_buf);
			halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						   false, 1);
		}

		halbtc8821aCsr2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

		halbtc8821aCsr2ant_sw_mechanism1(btcoexist, true, true, true,
						 true);
		halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false, false, false,
						 0x18);
		halbtc8821aCsr2ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0,
					      0, 0);
		halbtc8821aCsr2ant_limited_rx(btcoexist, NORMAL_EXEC, 0, 0, 0);

		common = true;
	} else if (!wifi_connected &&
		   (BT_8821A_CSR_2ANT_BT_STATUS_NON_IDLE ==
		    coex_dm->bt_status)) {
		low_pwr_disable = false;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);

		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Wifi IPS + BT Busy!!\n");
		BTC_TRACE(trace_buf);

		/* halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1); */
		halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);
		halbtc8821aCsr2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

		halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false, false, false,
						 false);
		halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false, false, false,
						 0x18);
		halbtc8821aCsr2ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0,
					      0, 0);
		halbtc8821aCsr2ant_limited_rx(btcoexist, NORMAL_EXEC, 0, 0, 0);

		common = true;
	} else {
		low_pwr_disable = true;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);

		if (wifi_busy) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Wifi Busy + BT Busy!!\n");
			BTC_TRACE(trace_buf);
			common = false;
		} else {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Wifi LPS + BT Busy!!\n");
			BTC_TRACE(trace_buf);
			halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						   21);

			if (halbtc8821aCsr2ant_need_to_dec_bt_pwr(btcoexist))
				halbtc8821aCsr2ant_dec_bt_pwr(btcoexist,
						      NORMAL_EXEC, true);
			else
				halbtc8821aCsr2ant_dec_bt_pwr(btcoexist,
						      NORMAL_EXEC, false);

			common = true;
		}

		halbtc8821aCsr2ant_sw_mechanism1(btcoexist, true, true, true,
						 true);
	}

	if (common == true)
		halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC,
				      0x55ff55ff, 0x5afa5afa, 0xffff, 0x3);

	return common;
}
void halbtc8821aCsr2ant_tdma_duration_adjust(IN struct btc_coexist *btcoexist,
		IN boolean sco_hid, IN boolean tx_pause, IN u8 max_interval)
{
	static s32		up, dn, m, n, wait_count;
	s32			result;   /* 0: no change, +1: increase WiFi duration, -1: decrease WiFi duration */
	u8			retry_count = 0;

	if (coex_dm->reset_tdma_adjust) {
		coex_dm->reset_tdma_adjust = false;
		{
			if (sco_hid) {
				if (tx_pause) {
					if (max_interval == 1) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 13);
						coex_dm->ps_tdma_du_adj_type =
							13;
					} else if (max_interval == 2) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					} else if (max_interval == 3) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					}
				} else {
					if (max_interval == 1) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 9);
						coex_dm->ps_tdma_du_adj_type =
							9;
					} else if (max_interval == 2) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 10);
						coex_dm->ps_tdma_du_adj_type =
							10;
					} else if (max_interval == 3) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					}
				}
			} else {
				if (tx_pause) {
					if (max_interval == 1) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 5);
						coex_dm->ps_tdma_du_adj_type =
							5;
					} else if (max_interval == 2) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (max_interval == 3) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					}
				} else {
					if (max_interval == 1) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 1);
						coex_dm->ps_tdma_du_adj_type =
							1;
					} else if (max_interval == 2) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (max_interval == 3) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else {
						halbtc8821aCsr2ant_ps_tdma(
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
		result = 0;
		wait_count++;

		if (retry_count ==
		    0) { /* no retry in the last 2-second duration */
			up++;
			dn--;

			if (dn <= 0)
				dn = 0;

			if (up >= n) {	/* if 連續 n 個2秒 retry count為0, 則調寬WiFi duration */
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

			if (dn == 2) {	/* if 連續 2 個2秒 retry count< 3, 則調窄WiFi duration */
				if (wait_count <= 2)
					m++; /* 避免一直在兩個level中來回 */
				else
					m = 1;

				if (m >= 20)  /* m 最大值 = 20 ' 最大120秒 recheck是否調整 WiFi duration. */
					m = 20;

				n = 3 * m;
				up = 0;
				dn = 0;
				wait_count = 0;
				result = -1;
			}
		} else { /* retry count > 3, 只要1次 retry count > 3, 則調窄WiFi duration */
			if (wait_count == 1)
				m++; /* 避免一直在兩個level中來回 */
			else
				m = 1;

			if (m >= 20)  /* m 最大值 = 20 ' 最大120秒 recheck是否調整 WiFi duration. */
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
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 5);
					coex_dm->ps_tdma_du_adj_type = 5;
				} else if (coex_dm->cur_ps_tdma == 1) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 5);
					coex_dm->ps_tdma_du_adj_type = 5;
				} else if (coex_dm->cur_ps_tdma == 2) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 6);
					coex_dm->ps_tdma_du_adj_type = 6;
				} else if (coex_dm->cur_ps_tdma == 3) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 4) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 8);
					coex_dm->ps_tdma_du_adj_type = 8;
				}
				if (coex_dm->cur_ps_tdma == 9) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 13);
					coex_dm->ps_tdma_du_adj_type = 13;
				} else if (coex_dm->cur_ps_tdma == 10) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 14);
					coex_dm->ps_tdma_du_adj_type = 14;
				} else if (coex_dm->cur_ps_tdma == 11) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 12) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 16);
					coex_dm->ps_tdma_du_adj_type = 16;
				}

				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 5) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (coex_dm->cur_ps_tdma == 6) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 8);
						coex_dm->ps_tdma_du_adj_type =
							8;
					} else if (coex_dm->cur_ps_tdma == 13) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					} else if (coex_dm->cur_ps_tdma == 14) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 16);
						coex_dm->ps_tdma_du_adj_type =
							16;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 8) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (coex_dm->cur_ps_tdma == 6) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 5);
						coex_dm->ps_tdma_du_adj_type =
							5;
					} else if (coex_dm->cur_ps_tdma == 16) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					} else if (coex_dm->cur_ps_tdma == 14) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 13);
						coex_dm->ps_tdma_du_adj_type =
							13;
					}
				}
			} else {
				if (coex_dm->cur_ps_tdma == 5) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 71);
					coex_dm->ps_tdma_du_adj_type = 71;
				} else if (coex_dm->cur_ps_tdma == 6) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 2);
					coex_dm->ps_tdma_du_adj_type = 2;
				} else if (coex_dm->cur_ps_tdma == 7) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 8) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 4);
					coex_dm->ps_tdma_du_adj_type = 4;
				}
				if (coex_dm->cur_ps_tdma == 13) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 9);
					coex_dm->ps_tdma_du_adj_type = 9;
				} else if (coex_dm->cur_ps_tdma == 14) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 10);
					coex_dm->ps_tdma_du_adj_type = 10;
				} else if (coex_dm->cur_ps_tdma == 15) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 16) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 12);
					coex_dm->ps_tdma_du_adj_type = 12;
				}

				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 71) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 1);
						coex_dm->ps_tdma_du_adj_type =
							1;
					} else if (coex_dm->cur_ps_tdma == 1) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (coex_dm->cur_ps_tdma == 2) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 4);
						coex_dm->ps_tdma_du_adj_type =
							4;
					} else if (coex_dm->cur_ps_tdma == 9) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 10);
						coex_dm->ps_tdma_du_adj_type =
							10;
					} else if (coex_dm->cur_ps_tdma == 10) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 12);
						coex_dm->ps_tdma_du_adj_type =
							12;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 4) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (coex_dm->cur_ps_tdma == 2) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 1);
						coex_dm->ps_tdma_du_adj_type =
							1;
					} else if (coex_dm->cur_ps_tdma == 1) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 71);
						coex_dm->ps_tdma_du_adj_type =
							71;
					} else if (coex_dm->cur_ps_tdma == 12) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 10);
						coex_dm->ps_tdma_du_adj_type =
							10;
					} else if (coex_dm->cur_ps_tdma == 10) {
						halbtc8821aCsr2ant_ps_tdma(
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
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 6);
					coex_dm->ps_tdma_du_adj_type = 6;
				} else if (coex_dm->cur_ps_tdma == 2) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 6);
					coex_dm->ps_tdma_du_adj_type = 6;
				} else if (coex_dm->cur_ps_tdma == 3) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 4) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 8);
					coex_dm->ps_tdma_du_adj_type = 8;
				}
				if (coex_dm->cur_ps_tdma == 9) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 14);
					coex_dm->ps_tdma_du_adj_type = 14;
				} else if (coex_dm->cur_ps_tdma == 10) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 14);
					coex_dm->ps_tdma_du_adj_type = 14;
				} else if (coex_dm->cur_ps_tdma == 11) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 12) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 16);
					coex_dm->ps_tdma_du_adj_type = 16;
				}
				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 5) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (coex_dm->cur_ps_tdma == 6) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 8);
						coex_dm->ps_tdma_du_adj_type =
							8;
					} else if (coex_dm->cur_ps_tdma == 13) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					} else if (coex_dm->cur_ps_tdma == 14) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 16);
						coex_dm->ps_tdma_du_adj_type =
							16;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 8) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (coex_dm->cur_ps_tdma == 6) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 6);
						coex_dm->ps_tdma_du_adj_type =
							6;
					} else if (coex_dm->cur_ps_tdma == 16) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					} else if (coex_dm->cur_ps_tdma == 14) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 14);
						coex_dm->ps_tdma_du_adj_type =
							14;
					}
				}
			} else {
				if (coex_dm->cur_ps_tdma == 5) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 2);
					coex_dm->ps_tdma_du_adj_type = 2;
				} else if (coex_dm->cur_ps_tdma == 6) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 2);
					coex_dm->ps_tdma_du_adj_type = 2;
				} else if (coex_dm->cur_ps_tdma == 7) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 8) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 4);
					coex_dm->ps_tdma_du_adj_type = 4;
				}
				if (coex_dm->cur_ps_tdma == 13) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 10);
					coex_dm->ps_tdma_du_adj_type = 10;
				} else if (coex_dm->cur_ps_tdma == 14) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 10);
					coex_dm->ps_tdma_du_adj_type = 10;
				} else if (coex_dm->cur_ps_tdma == 15) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 16) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 12);
					coex_dm->ps_tdma_du_adj_type = 12;
				}
				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 1) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (coex_dm->cur_ps_tdma == 2) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 4);
						coex_dm->ps_tdma_du_adj_type =
							4;
					} else if (coex_dm->cur_ps_tdma == 9) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 10);
						coex_dm->ps_tdma_du_adj_type =
							10;
					} else if (coex_dm->cur_ps_tdma == 10) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 12);
						coex_dm->ps_tdma_du_adj_type =
							12;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 4) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (coex_dm->cur_ps_tdma == 2) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 2);
						coex_dm->ps_tdma_du_adj_type =
							2;
					} else if (coex_dm->cur_ps_tdma == 12) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 10);
						coex_dm->ps_tdma_du_adj_type =
							10;
					} else if (coex_dm->cur_ps_tdma == 10) {
						halbtc8821aCsr2ant_ps_tdma(
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
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 2) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 3) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 4) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 8);
					coex_dm->ps_tdma_du_adj_type = 8;
				}
				if (coex_dm->cur_ps_tdma == 9) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 10) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 11) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 12) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 16);
					coex_dm->ps_tdma_du_adj_type = 16;
				}
				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 5) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 6) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 8);
						coex_dm->ps_tdma_du_adj_type =
							8;
					} else if (coex_dm->cur_ps_tdma == 13) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 14) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 16);
						coex_dm->ps_tdma_du_adj_type =
							16;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 8) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 6) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 7);
						coex_dm->ps_tdma_du_adj_type =
							7;
					} else if (coex_dm->cur_ps_tdma == 16) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					} else if (coex_dm->cur_ps_tdma == 14) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 15);
						coex_dm->ps_tdma_du_adj_type =
							15;
					}
				}
			} else {
				if (coex_dm->cur_ps_tdma == 5) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 6) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 7) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 8) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 4);
					coex_dm->ps_tdma_du_adj_type = 4;
				}
				if (coex_dm->cur_ps_tdma == 13) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 14) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 15) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 16) {
					halbtc8821aCsr2ant_ps_tdma(btcoexist,
						   NORMAL_EXEC, true, 12);
					coex_dm->ps_tdma_du_adj_type = 12;
				}
				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 1) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 2) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 4);
						coex_dm->ps_tdma_du_adj_type =
							4;
					} else if (coex_dm->cur_ps_tdma == 9) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 10) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 12);
						coex_dm->ps_tdma_du_adj_type =
							12;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 4) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 2) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 3);
						coex_dm->ps_tdma_du_adj_type =
							3;
					} else if (coex_dm->cur_ps_tdma == 12) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						halbtc8821aCsr2ant_ps_tdma(
							btcoexist, NORMAL_EXEC,
							true, 11);
						coex_dm->ps_tdma_du_adj_type =
							11;
					} else if (coex_dm->cur_ps_tdma == 10) {
						halbtc8821aCsr2ant_ps_tdma(
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
			halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
					   coex_dm->ps_tdma_du_adj_type);
		else {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], roaming/link/scan is under progress, will adjust next time!!!\n");
			BTC_TRACE(trace_buf);
		}
	}

	/* when halbtc8821aCsr2ant_tdma_duration_adjust() is called, fw dac swing is included in the function. */
	/* if(coex_dm->ps_tdma_du_adj_type == 71) */
	/*	halbtc8821aCsr2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 0xc); //Skip because A2DP get worse at HT40 */
	/* else */
	halbtc8821aCsr2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC,
					    0x6);
}

/* SCO only or SCO+PAN(HS) */
void halbtc8821aCsr2ant_action_sco(IN struct btc_coexist *btcoexist)
{
	halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55555555,
				      0x55555555, 0xffffff, 0x3);
	halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);

	halbtc8821aCsr2ant_low_penalty_ra(btcoexist, NORMAL_EXEC, true);

	halbtc8821aCsr2ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 1, 0, 2, 0);

	if (coex_sta->slave == false)
		halbtc8821aCsr2ant_limited_rx(btcoexist, NORMAL_EXEC, false,
					      true, 0x4);
	else
		halbtc8821aCsr2ant_limited_rx(btcoexist, NORMAL_EXEC, false,
					      true, 0x2);

	/*
		wifi_rssi_state = halbtc8821aCsr2ant_wifi_rssi_state(btcoexist, 0, 2, 15, 0);
		bt_rssi_state = halbtc8821aCsr2ant_bt_rssi_state(2, 35, 0);

		halbtc8821aCsr2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 4);

		if(halbtc8821aCsr2ant_need_to_dec_bt_pwr(btcoexist))
			halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
		else
			halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

		btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

		if (BTC_WIFI_BW_LEGACY == wifi_bw)
		{
			halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC, 0x5a5a5a5a, 0x5a5a5a5a, 0xffff, 0x3);
		}
		else
		{
			halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC, 0x5aea5aea, 0x5aea5aea, 0xffff, 0x3);
		}

		if(BTC_WIFI_BW_HT40 == wifi_bw)
		{




			if( (bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
				(bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
			{
				halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);
			}
			else
			{
				halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);
			}


			if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
				(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
			{
				halbtc8821aCsr2ant_sw_mechanism1(btcoexist,true,true,false,false);
				halbtc8821aCsr2ant_sw_mechanism2(btcoexist,true,false,false,0x18);
			}
			else
			{
				halbtc8821aCsr2ant_sw_mechanism1(btcoexist,true,true,false,false);
				halbtc8821aCsr2ant_sw_mechanism2(btcoexist,false,false,false,0x18);
			}
		}
		else
		{



			if( (bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
				(bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
			{
				halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);
			}
			else
			{
				halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);
			}


			if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
				(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
			{
				 halbtc8821aCsr2ant_sw_mechanism1(btcoexist,false,true,false,false);
				 halbtc8821aCsr2ant_sw_mechanism2(btcoexist,true,false,false,0x18);
			}
			else
			{
				 halbtc8821aCsr2ant_sw_mechanism1(btcoexist,false,true,false,false);
				 halbtc8821aCsr2ant_sw_mechanism2(btcoexist,false,false,false,0x18);
			}
		}
	*/
}


void halbtc8821aCsr2ant_action_hid(IN struct btc_coexist *btcoexist)
{
	u8	wifi_rssi_state, bt_rssi_state;
	u32	wifi_bw;

	wifi_rssi_state = halbtc8821aCsr2ant_wifi_rssi_state(btcoexist, 0, 2,
			  15, 0);
	bt_rssi_state = halbtc8821aCsr2ant_bt_rssi_state(2, 35, 0);

	halbtc8821aCsr2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (halbtc8821aCsr2ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_LEGACY == wifi_bw) /* for HID at 11b/g mode */
		halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC,
				      0x55ff55ff, 0x5a5a5a5a, 0xffff, 0x3);
	else  /* for HID quality & wifi performance balance at 11n mode */
		halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC,
				      0x55ff55ff, 0x5aea5aea, 0xffff, 0x3);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/* fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						   9);
		else
			halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						   13);

		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, true, true,
							 false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, true,
							 false, false, 0x18);
		} else {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, true, true,
							 false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false,
							 false, false, 0x18);
		}
	} else {
		/* fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						   9);
		else
			halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						   13);

		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false,
							 true, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, true,
							 false, false, 0x18);
		} else {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false,
							 true, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false,
							 false, false, 0x18);
		}
	}
}

/* A2DP only / PAN(EDR) only/ A2DP+PAN(HS) */
void halbtc8821aCsr2ant_action_a2dp(IN struct btc_coexist *btcoexist)
{
	halbtc8821aCsr2ant_limited_rx(btcoexist, NORMAL_EXEC, false, true, 0x8);

	if (coex_sta->slave == false) {
		halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC,
				      0xfdfdfdfd, 0xdfdadfda, 0xffffff, 0x3);
		halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 22);
		halbtc8821aCsr2ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0,
					      0, 1);
		halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false, false, true,
						 0x0c);
	} else {
		halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC,
				      0xfdfdfdfd, 0xdfdadfda, 0xffffff, 0x3);
		halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 23);
		halbtc8821aCsr2ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0,
					      0, 2);
		halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false, false, true,
						 0x18);
	}

	/*
		wifi_rssi_state = halbtc8821aCsr2ant_wifi_rssi_state(btcoexist, 0, 2, 15, 0);
		bt_rssi_state = halbtc8821aCsr2ant_bt_rssi_state(2, 35, 0);





		if(halbtc8821aCsr2ant_need_to_dec_bt_pwr(btcoexist))
			halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
		else
			halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

		btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

		if(BTC_WIFI_BW_HT40 == wifi_bw)
		{

			if( (bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
				(bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
			{
				halbtc8821aCsr2ant_tdma_duration_adjust(btcoexist, false, false, 1);
			}
			else
			{
				halbtc8821aCsr2ant_tdma_duration_adjust(btcoexist, false, true, 1);
			}


			if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
				(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
			{
				 halbtc8821aCsr2ant_sw_mechanism1(btcoexist,true,false,false,false);
				 halbtc8821aCsr2ant_sw_mechanism2(btcoexist,true,false,false,0x18);
			}
			else
			{
				 halbtc8821aCsr2ant_sw_mechanism1(btcoexist,true,false,false,false);
				 halbtc8821aCsr2ant_sw_mechanism2(btcoexist,false,false,false,0x18);
			}
		}
		else
		{

			if( (bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
				(bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
			{
				halbtc8821aCsr2ant_tdma_duration_adjust(btcoexist, false, false, 1);
			}
			else
			{
				halbtc8821aCsr2ant_tdma_duration_adjust(btcoexist, false, true, 1);
			}


			if( (wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
				(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH) )
			{
				 halbtc8821aCsr2ant_sw_mechanism1(btcoexist,false,false,false,false);
				 halbtc8821aCsr2ant_sw_mechanism2(btcoexist,true,false,false,0x18);
			}
			else
			{
				 halbtc8821aCsr2ant_sw_mechanism1(btcoexist,false,false,false,false);
				 halbtc8821aCsr2ant_sw_mechanism2(btcoexist,false,false,false,0x18);
			}
		}
	*/
}

void halbtc8821aCsr2ant_action_a2dp_pan_hs(IN struct btc_coexist *btcoexist)
{
	u8		wifi_rssi_state, bt_rssi_state, bt_info_ext;
	u32		wifi_bw;

	bt_info_ext = coex_sta->bt_info_ext;
	wifi_rssi_state = halbtc8821aCsr2ant_wifi_rssi_state(btcoexist, 0, 2,
			  15, 0);
	bt_rssi_state = halbtc8821aCsr2ant_bt_rssi_state(2, 35, 0);

	/* fw dac swing is called in halbtc8821aCsr2ant_tdma_duration_adjust() */
	/* halbtc8821aCsr2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6); */


	if (halbtc8821aCsr2ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/* fw mechanism */
		if (bt_info_ext & BIT(0))	/* a2dp basic rate */
			halbtc8821aCsr2ant_tdma_duration_adjust(btcoexist,
								false, true, 2);
		else				/* a2dp edr rate */
			halbtc8821aCsr2ant_tdma_duration_adjust(btcoexist,
								false, true, 1);

		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, true,
							 false, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, true,
							 false, false, 0x18);
		} else {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, true,
							 false, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false,
							 false, false, 0x18);
		}
	} else {
		/* fw mechanism */
		if (bt_info_ext & BIT(0))	/* a2dp basic rate */
			halbtc8821aCsr2ant_tdma_duration_adjust(btcoexist,
								false, true, 2);
		else				/* a2dp edr rate */
			halbtc8821aCsr2ant_tdma_duration_adjust(btcoexist,
								false, true, 1);

		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false,
							 false, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, true,
							 false, false, 0x18);
		} else {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false,
							 false, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false,
							 false, false, 0x18);
		}
	}
}

void halbtc8821aCsr2ant_action_pan_edr(IN struct btc_coexist *btcoexist)
{
	u8		wifi_rssi_state, bt_rssi_state;
	u32		wifi_bw;

	wifi_rssi_state = halbtc8821aCsr2ant_wifi_rssi_state(btcoexist, 0, 2,
			  15, 0);
	bt_rssi_state = halbtc8821aCsr2ant_bt_rssi_state(2, 35, 0);

	halbtc8821aCsr2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (halbtc8821aCsr2ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_LEGACY == wifi_bw) /* for HID at 11b/g mode */
		halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC,
				      0x55ff55ff, 0x5aff5aff, 0xffff, 0x3);
	else  /* for HID quality & wifi performance balance at 11n mode */
		halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC,
				      0x55ff55ff, 0x5aff5aff, 0xffff, 0x3);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/* fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						   1);
		else
			halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						   5);

		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, true,
							 false, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, true,
							 false, false, 0x18);
		} else {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, true,
							 false, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false,
							 false, false, 0x18);
		}
	} else {
		/* fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						   1);
		else
			halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						   5);

		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false,
							 false, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, true,
							 false, false, 0x18);
		} else {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false,
							 false, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false,
							 false, false, 0x18);
		}
	}
}


/* PAN(HS) only */
void halbtc8821aCsr2ant_action_pan_hs(IN struct btc_coexist *btcoexist)
{
	u8		wifi_rssi_state, bt_rssi_state;
	u32		wifi_bw;

	wifi_rssi_state = halbtc8821aCsr2ant_wifi_rssi_state(btcoexist, 0, 2,
			  15, 0);
	bt_rssi_state = halbtc8821aCsr2ant_bt_rssi_state(2, 35, 0);

	halbtc8821aCsr2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/* fw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC,
						      true);
		else
			halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC,
						      false);
		halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 1);

		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, true,
							 false, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, true,
							 false, false, 0x18);
		} else {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, true,
							 false, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false,
							 false, false, 0x18);
		}
	} else {
		/* fw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC,
						      true);
		else
			halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC,
						      false);

		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						   false, 1);
		else
			halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC,
						   false, 1);

		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false,
							 false, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, true,
							 false, false, 0x18);
		} else {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false,
							 false, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false,
							 false, false, 0x18);
		}
	}
}

/* PAN(EDR)+A2DP */
void halbtc8821aCsr2ant_action_pan_edr_a2dp(IN struct btc_coexist *btcoexist)
{
	u8		wifi_rssi_state, bt_rssi_state, bt_info_ext;
	u32		wifi_bw;

	bt_info_ext = coex_sta->bt_info_ext;
	wifi_rssi_state = halbtc8821aCsr2ant_wifi_rssi_state(btcoexist, 0, 2,
			  15, 0);
	bt_rssi_state = halbtc8821aCsr2ant_bt_rssi_state(2, 35, 0);

	halbtc8821aCsr2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (halbtc8821aCsr2ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_LEGACY == wifi_bw) /* for HID at 11b/g mode */
		halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC,
				      0x55ff55ff, 0x5afa5afa, 0xffff, 0x3);
	else  /* for HID quality & wifi performance balance at 11n mode */
		halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC,
				      0x55ff55ff, 0x5afa5afa, 0xffff, 0x3);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/* fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, false, false, 3);
			else				/* a2dp edr rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, false, false, 3);
		} else {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, false, true, 3);
			else				/* a2dp edr rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, false, true, 3);
		}

		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, true,
							 false, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, true,
							 false, false, 0x18);
		} else {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, true,
							 false, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false,
							 false, false, 0x18);
		};
	} else {
		/* fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, false, false, 3);
			else				/* a2dp edr rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, false, false, 3);
		} else {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, false, true, 3);
			else				/* a2dp edr rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, false, true, 3);
		}

		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false,
							 false, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, true,
							 false, false, 0x18);
		} else {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false,
							 false, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false,
							 false, false, 0x18);
		}
	}
}

void halbtc8821aCsr2ant_action_pan_edr_hid(IN struct btc_coexist *btcoexist)
{
	u8		wifi_rssi_state, bt_rssi_state;
	u32		wifi_bw;

	wifi_rssi_state = halbtc8821aCsr2ant_wifi_rssi_state(btcoexist, 0, 2,
			  15, 0);
	bt_rssi_state = halbtc8821aCsr2ant_bt_rssi_state(2, 35, 0);

	halbtc8821aCsr2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (halbtc8821aCsr2ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_LEGACY == wifi_bw) /* for HID at 11b/g mode */
		halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC,
				      0x55ff55ff, 0x5a5f5a5f, 0xffff, 0x3);
	else  /* for HID quality & wifi performance balance at 11n mode */
		halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC,
				      0x55ff55ff, 0x5a5f5a5f, 0xffff, 0x3);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		halbtc8821aCsr2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 3);
		/* fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						   10);
		else
			halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						   14);

		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, true, true,
							 false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, true,
							 false, false, 0x18);
		} else {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, true, true,
							 false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false,
							 false, false, 0x18);
		}
	} else {
		halbtc8821aCsr2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		/* fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						   10);
		else
			halbtc8821aCsr2ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						   14);

		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false,
							 true, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, true,
							 false, false, 0x18);
		} else {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false,
							 true, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false,
							 false, false, 0x18);
		}
	}
}

/* HID+A2DP+PAN(EDR) */
void halbtc8821aCsr2ant_action_hid_a2dp_pan_edr(IN struct btc_coexist
		*btcoexist)
{
	u8		wifi_rssi_state, bt_rssi_state, bt_info_ext;
	u32		wifi_bw;

	bt_info_ext = coex_sta->bt_info_ext;
	wifi_rssi_state = halbtc8821aCsr2ant_wifi_rssi_state(btcoexist, 0, 2,
			  15, 0);
	bt_rssi_state = halbtc8821aCsr2ant_bt_rssi_state(2, 35, 0);

	halbtc8821aCsr2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (halbtc8821aCsr2ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_LEGACY == wifi_bw) /* for HID at 11b/g mode */
		halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC,
				      0x55ff55ff, 0x5a5a5a5a, 0xffff, 0x3);
	else  /* for HID quality & wifi performance balance at 11n mode */
		halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC,
				      0x55ff55ff, 0x5a5a5a5a, 0xffff, 0x3);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/* fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, true, true, 3);
			else				/* a2dp edr rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, true, true, 3);
		} else {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, true, true, 3);
			else				/* a2dp edr rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, true, true, 3);
		}

		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, true, true,
							 false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, true,
							 false, false, 0x18);
		} else {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, true, true,
							 false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false,
							 false, false, 0x18);
		}
	} else {
		/* fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, true, false, 3);
			else				/* a2dp edr rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, true, false, 3);
		} else {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, true, true, 3);
			else				/* a2dp edr rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, true, true, 3);
		}

		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false,
							 true, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, true,
							 false, false, 0x18);
		} else {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false,
							 true, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false,
							 false, false, 0x18);
		}
	}
}

void halbtc8821aCsr2ant_action_hid_a2dp(IN struct btc_coexist *btcoexist)
{
	u8		wifi_rssi_state, bt_rssi_state, bt_info_ext;
	u32		wifi_bw;

	bt_info_ext = coex_sta->bt_info_ext;
	wifi_rssi_state = halbtc8821aCsr2ant_wifi_rssi_state(btcoexist, 0, 2,
			  15, 0);
	bt_rssi_state = halbtc8821aCsr2ant_bt_rssi_state(2, 35, 0);

	if (halbtc8821aCsr2ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		halbtc8821aCsr2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_LEGACY == wifi_bw) { /* for HID at 11b/g mode */
		/* Allen		halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55ff55ff, 0x5a5a5a5a, 0xffff, 0x3); */
		halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC,
				      0x55ff55ff, 0x5f5b5f5b, 0xffffff, 0x3);
	} else { /* for HID quality & wifi performance balance at 11n mode */
		/* Allen		halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55ff55ff, 0x5a5a5a5a, 0xffff, 0x3); */
		halbtc8821aCsr2ant_coex_table(btcoexist, NORMAL_EXEC,
				      0x55ff55ff, 0x5f5b5f5b, 0xffffff, 0x3);

	}

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/* fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, true, true, 2);
			else				/* a2dp edr rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, true, true, 2);
		} else {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, true, true, 2);
			else				/* a2dp edr rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, true, true, 2);
		}

		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, true, true,
							 false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, true,
							 false, false, 0x18);
		} else {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, true, true,
							 false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false,
							 false, false, 0x18);
		}
	} else {
		/* fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			if (bt_info_ext & BIT(0)) {	/* a2dp basic rate */
				/*				halbtc8821aCsr2ant_tdma_duration_adjust(btcoexist, true, false, 2); */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, true, true, 2);

			} else {			/* a2dp edr rate */
				/* Allen				halbtc8821aCsr2ant_tdma_duration_adjust(btcoexist, true, false, 2); */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, true, true, 2);
			}
		} else {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, true, true, 2);
			else				/* a2dp edr rate */
				halbtc8821aCsr2ant_tdma_duration_adjust(
					btcoexist, true, true, 2);
		}

		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false,
							 true, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, true,
							 false, false, 0x18);
		} else {
			halbtc8821aCsr2ant_sw_mechanism1(btcoexist, false,
							 true, false, false);
			halbtc8821aCsr2ant_sw_mechanism2(btcoexist, false,
							 false, false, 0x18);
		}
	}
}

void halbtc8821aCsr2ant_run_coexist_mechanism(IN struct btc_coexist *btcoexist)
{
	boolean				wifi_under_5g = false;
	u8				algorithm = 0;

	if (btcoexist->manual_control) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Manual control!!!\n");
		BTC_TRACE(trace_buf);
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);

	if (wifi_under_5g) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], RunCoexistMechanism(), run 5G coex setting!!<===\n");
		BTC_TRACE(trace_buf);
		halbtc8821aCsr2ant_coex_under_5g(btcoexist);
		return;
	}

	{
		algorithm = halbtc8821aCsr2ant_action_algorithm(btcoexist);
		if (coex_sta->c2h_bt_inquiry_page &&
		    (BT_8821A_CSR_2ANT_COEX_ALGO_PANHS != algorithm)) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				"[BTCoex], BT is under inquiry/page scan !!\n");
			BTC_TRACE(trace_buf);
			halbtc8821aCsr2ant_bt_inquiry_page(btcoexist);
			return;
		}

		coex_dm->cur_algorithm = algorithm;
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			"[BTCoex], Algorithm = %d\n", coex_dm->cur_algorithm);
		BTC_TRACE(trace_buf);

		if (halbtc8821aCsr2ant_is_common_action(btcoexist)) {
			BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
				    "[BTCoex], Action 2-Ant common.\n");
			BTC_TRACE(trace_buf);
			coex_dm->reset_tdma_adjust = true;
		} else {
			if (coex_dm->cur_algorithm != coex_dm->pre_algorithm) {
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], pre_algorithm=%d, cur_algorithm=%d\n",
					    coex_dm->pre_algorithm,
					    coex_dm->cur_algorithm);
				BTC_TRACE(trace_buf);
				coex_dm->reset_tdma_adjust = true;
			}
			switch (coex_dm->cur_algorithm) {
			case BT_8821A_CSR_2ANT_COEX_ALGO_SCO:
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], Action 2-Ant, algorithm = SCO.\n");
				BTC_TRACE(trace_buf);
				halbtc8821aCsr2ant_action_sco(
					btcoexist);
				break;
			case BT_8821A_CSR_2ANT_COEX_ALGO_HID:
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], Action 2-Ant, algorithm = HID.\n");
				BTC_TRACE(trace_buf);
				halbtc8821aCsr2ant_action_hid(
					btcoexist);
				break;
			case BT_8821A_CSR_2ANT_COEX_ALGO_A2DP:
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], Action 2-Ant, algorithm = A2DP.\n");
				BTC_TRACE(trace_buf);
				halbtc8821aCsr2ant_action_a2dp(
					btcoexist);
				break;
			case BT_8821A_CSR_2ANT_COEX_ALGO_A2DP_PANHS:
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], Action 2-Ant, algorithm = A2DP+PAN(HS).\n");
				BTC_TRACE(trace_buf);
				halbtc8821aCsr2ant_action_a2dp_pan_hs(
					btcoexist);
				break;
			case BT_8821A_CSR_2ANT_COEX_ALGO_PANEDR:
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], Action 2-Ant, algorithm = PAN(EDR).\n");
				BTC_TRACE(trace_buf);
				halbtc8821aCsr2ant_action_pan_edr(
					btcoexist);
				break;
			case BT_8821A_CSR_2ANT_COEX_ALGO_PANHS:
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], Action 2-Ant, algorithm = HS mode.\n");
				BTC_TRACE(trace_buf);
				halbtc8821aCsr2ant_action_pan_hs(
					btcoexist);
				break;
			case BT_8821A_CSR_2ANT_COEX_ALGO_PANEDR_A2DP:
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], Action 2-Ant, algorithm = PAN+A2DP.\n");
				BTC_TRACE(trace_buf);
				halbtc8821aCsr2ant_action_pan_edr_a2dp(
					btcoexist);
				break;
			case BT_8821A_CSR_2ANT_COEX_ALGO_PANEDR_HID:
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], Action 2-Ant, algorithm = PAN(EDR)+HID.\n");
				BTC_TRACE(trace_buf);
				halbtc8821aCsr2ant_action_pan_edr_hid(
					btcoexist);
				break;
			case BT_8821A_CSR_2ANT_COEX_ALGO_HID_A2DP_PANEDR
					:
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], Action 2-Ant, algorithm = HID+A2DP+PAN.\n");
				BTC_TRACE(trace_buf);
				halbtc8821aCsr2ant_action_hid_a2dp_pan_edr(
					btcoexist);
				break;
			case BT_8821A_CSR_2ANT_COEX_ALGO_HID_A2DP:
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], Action 2-Ant, algorithm = HID+A2DP.\n");
				BTC_TRACE(trace_buf);
				halbtc8821aCsr2ant_action_hid_a2dp(
					btcoexist);
				break;
			default:
				BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
					"[BTCoex], Action 2-Ant, algorithm = coexist All Off!!\n");
				BTC_TRACE(trace_buf);
				halbtc8821aCsr2ant_coex_all_off(
					btcoexist);
				break;
			}
			coex_dm->pre_algorithm = coex_dm->cur_algorithm;
		}
	}
}



/* ************************************************************
 * work around function start with wa_halbtc8821aCsr2ant_
 * ************************************************************
 * ************************************************************
 * extern function start with ex_halbtc8821aCsr2ant_
 * ************************************************************ */
void ex_halbtc8821aCsr2ant_power_on_setting(IN struct btc_coexist *btcoexist)
{
}

void ex_halbtc8821aCsr2ant_init_hw_config(IN struct btc_coexist *btcoexist,
		IN boolean wifi_only)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], 2Ant Init HW Config!!\n");
	BTC_TRACE(trace_buf);

	if (wifi_only)
		return;

	/* if(back_up) */
	{
		/* backup rf 0x1e value */
		coex_dm->bt_rf_0x1e_backup = btcoexist->btc_get_rf_reg(
				     btcoexist, BTC_RF_A, 0x1e, 0xfffff);
		coex_dm->backup_arfr_cnt1 = btcoexist->btc_read_4byte(btcoexist,
					    0x430);
		coex_dm->backup_arfr_cnt2 = btcoexist->btc_read_4byte(btcoexist,
					    0x434);
		coex_dm->backup_retry_limit = btcoexist->btc_read_2byte(
						      btcoexist, 0x42a);
		coex_dm->backup_ampdu_max_time = btcoexist->btc_read_1byte(
				btcoexist, 0x456);
		coex_dm->backup_ampdu_max_num = btcoexist->btc_read_2byte(
							btcoexist, 0x4ca);
	}

#if 0 /* REMOVE */
	/* 0x790[5:0]=0x5 */
	u8tmp = btcoexist->btc_read_1byte(btcoexist, 0x790);
	u8tmp &= 0xc0;
	u8tmp |= 0x5;
	btcoexist->btc_write_1byte(btcoexist, 0x790, u8tmp);
#endif

	/* Antenna config */
	halbtc8821aCsr2ant_set_ant_path(btcoexist, BTC_ANT_WIFI_AT_MAIN, true,
					false);

	/* PTA parameter */
	halbtc8821aCsr2ant_coex_table(btcoexist, FORCE_EXEC, 0x55555555,
				      0x55555555, 0xffff, 0x3);

	/* Enable counter statistics */
	btcoexist->btc_write_1byte(btcoexist, 0x76e,
			   0xc); /* 0x76e[3] =1, WLAN_Act control by PTA */
	btcoexist->btc_write_1byte(btcoexist, 0x778, 0x3);

#if 0 /* REMOVE */
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x40, 0x20, 0x1);
#endif
}

void ex_halbtc8821aCsr2ant_init_coex_dm(IN struct btc_coexist *btcoexist)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
		    "[BTCoex], Coex Mechanism Init!!\n");
	BTC_TRACE(trace_buf);

	halbtc8821aCsr2ant_init_coex_dm(btcoexist);
}

void ex_halbtc8821aCsr2ant_display_coex_info(IN struct btc_coexist *btcoexist)
{
	struct  btc_board_info		*board_info = &btcoexist->board_info;
	struct  btc_stack_info		*stack_info = &btcoexist->stack_info;
	u8				*cli_buf = btcoexist->cli_buf;
	u8				u8tmp[4], i, bt_info_ext, ps_tdma_case = 0;
	u32				u32tmp[4];
	u32				fw_ver = 0, bt_patch_ver = 0;

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n ============[BT Coexist info]============");
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d ",
		   "Ant PG number/ Ant mechanism:",
		   board_info->pg_ant_num, board_info->btdm_ant_num);
	CL_PRINTF(cli_buf);

	if (btcoexist->manual_control) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
			   "[Action Manual control]!!");
		CL_PRINTF(cli_buf);
	}

	btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER, &bt_patch_ver);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %d_%d/ 0x%x/ 0x%x(%d)",
		   "CoexVer/ FwVer/ PatchVer",
		   glcoex_ver_date_8821a_csr_2ant, glcoex_ver_8821a_csr_2ant,
		   fw_ver, bt_patch_ver, bt_patch_ver);
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

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = [%s/ %d/ %d] ",
		   "BT [status/ rssi/ retryCnt]",
		   ((coex_sta->c2h_bt_inquiry_page) ? ("inquiry/page scan") : ((
		   BT_8821A_CSR_2ANT_BT_STATUS_IDLE == coex_dm->bt_status)
		   ? "idle" : ((BT_8821A_CSR_2ANT_BT_STATUS_CONNECTED_IDLE
			== coex_dm->bt_status) ? "connected-idle" : "busy"))),
		   coex_sta->bt_rssi, coex_sta->bt_retry_cnt);
	CL_PRINTF(cli_buf);

	if (stack_info->profile_notified) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %d / %d / %d / %d", "SCO/HID/PAN/A2DP",
			   stack_info->sco_exist, stack_info->hid_exist,
			   stack_info->pan_exist, stack_info->a2dp_exist);
		CL_PRINTF(cli_buf);

		btcoexist->btc_disp_dbg_msg(btcoexist,
					    BTC_DBG_DISP_BT_LINK_INFO);
	}

	bt_info_ext = coex_sta->bt_info_ext;
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s",
		   "BT Info A2DP rate",
		   (bt_info_ext & BIT(0)) ? "Basic rate" : "EDR rate");
	CL_PRINTF(cli_buf);

	for (i = 0; i < BT_INFO_SRC_8821A_CSR_2ANT_MAX; i++) {
		if (coex_sta->bt_info_c2h_cnt[i]) {
			CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
				"\r\n %-35s = %02x %02x %02x %02x %02x %02x %02x(%d)",
				   glbt_info_src_8821a_csr_2ant[i],
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

	if (!btcoexist->manual_control) {
		ps_tdma_case = coex_dm->cur_ps_tdma;
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %02x %02x %02x %02x %02x case-%d",
			   "PS TDMA",
			   coex_dm->ps_tdma_para[0], coex_dm->ps_tdma_para[1],
			   coex_dm->ps_tdma_para[2], coex_dm->ps_tdma_para[3],
			   coex_dm->ps_tdma_para[4], ps_tdma_case);
		CL_PRINTF(cli_buf);

		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d ",
			   "DecBtPwr/ IgnWlanAct",
			coex_dm->cur_dec_bt_pwr, coex_dm->cur_ignore_wlan_act);
		CL_PRINTF(cli_buf);
	}

	/* Hw setting		 */
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
		   "============[Hw setting]============");
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x",
		   "RF-A, 0x1e initVal",
		   coex_dm->bt_rf_0x1e_backup);
	CL_PRINTF(cli_buf);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x778);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x6cc);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x ",
		   "0x778 (W_Act)/ 0x6cc (CoTab Sel)",
		   u8tmp[0], u8tmp[1]);
	CL_PRINTF(cli_buf);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x8db);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0xc5b);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "0x8db(ADC)/0xc5b[29:25](DAC)",
		   ((u8tmp[0] & 0x60) >> 5), ((u8tmp[1] & 0x3e) >> 1));
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xcb4);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "0xcb4[7:0](ctrl)/ 0xcb4[29:28](val)",
		   u32tmp[0] & 0xff, ((u32tmp[0] & 0x30000000) >> 28));
	CL_PRINTF(cli_buf);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x40);
	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x4c);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x974);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x40/ 0x4c[24:23]/ 0x974",
		   u8tmp[0], ((u32tmp[0] & 0x01800000) >> 23), u32tmp[1]);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x550);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x522);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "0x550(bcn ctrl)/0x522",
		   u32tmp[0], u8tmp[0]);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xc50);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0xa0a);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "0xc50(DIG)/0xa0a(CCK-TH)",
		   u32tmp[0], u8tmp[0]);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xf48);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0xa5b);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0xa5c);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "OFDM-FA/ CCK-FA",
		   u32tmp[0], (u8tmp[0] << 8) + u8tmp[1]);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x6c0);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x6c4);
	u32tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0x6c8);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x6c0/0x6c4/0x6c8",
		   u32tmp[0], u32tmp[1], u32tmp[2]);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "0x770 (hi-pri Rx/Tx)",
		   coex_sta->high_priority_rx, coex_sta->high_priority_tx);
	CL_PRINTF(cli_buf);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "0x774(low-pri Rx/Tx)",
		   coex_sta->low_priority_rx, coex_sta->low_priority_tx);
	CL_PRINTF(cli_buf);

	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_COEX_STATISTICS);
}


void ex_halbtc8821aCsr2ant_ips_notify(IN struct btc_coexist *btcoexist,
				      IN u8 type)
{
	if (BTC_IPS_ENTER == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], IPS ENTER notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_ips = true;
		halbtc8821aCsr2ant_coex_all_off(btcoexist);
	} else if (BTC_IPS_LEAVE == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], IPS LEAVE notify\n");
		BTC_TRACE(trace_buf);
		coex_sta->under_ips = false;
		/* halbtc8821aCsr2ant_init_coex_dm(btcoexist); */
	}
}

void ex_halbtc8821aCsr2ant_lps_notify(IN struct btc_coexist *btcoexist,
				      IN u8 type)
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

void ex_halbtc8821aCsr2ant_scan_notify(IN struct btc_coexist *btcoexist,
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
	}
}

void ex_halbtc8821aCsr2ant_connect_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	if (BTC_ASSOCIATE_START == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], CONNECT START notify\n");
		BTC_TRACE(trace_buf);
	} else if (BTC_ASSOCIATE_FINISH == type) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], CONNECT FINISH notify\n");
		BTC_TRACE(trace_buf);
	}
}

void ex_halbtc8821aCsr2ant_media_status_notify(IN struct btc_coexist *btcoexist,
		IN u8 type)
{
	u8			h2c_parameter[3] = {0};
	u32			wifi_bw;
	u8			wifi_central_chnl;

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

}

void ex_halbtc8821aCsr2ant_specific_packet_notify(IN struct btc_coexist
		*btcoexist, IN u8 type)
{
	if (type == BTC_PACKET_DHCP) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], DHCP Packet notify\n");
		BTC_TRACE(trace_buf);
	}
}

void ex_halbtc8821aCsr2ant_bt_info_notify(IN struct btc_coexist *btcoexist,
		IN u8 *tmp_buf, IN u8 length)
{
	u8			bt_info = 0;
	u8			i, rsp_source = 0;
	boolean			bt_busy = false, limited_dig = false;
	boolean			wifi_connected = false, bt_hs_on = false,
				wifi_under_5g = false;

	coex_sta->c2h_bt_info_req_sent = false;
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);

	rsp_source = tmp_buf[0] & 0xf;
	if (rsp_source >= BT_INFO_SRC_8821A_CSR_2ANT_MAX)
		rsp_source = BT_INFO_SRC_8821A_CSR_2ANT_WIFI_FW;
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

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	if (BT_INFO_SRC_8821A_CSR_2ANT_WIFI_FW != rsp_source) {
		coex_sta->bt_retry_cnt =	/* [3:0] */
			coex_sta->bt_info_c2h[rsp_source][2] & 0xf;

		coex_sta->bt_rssi =
			coex_sta->bt_info_c2h[rsp_source][3] * 2 + 10;

		coex_sta->bt_info_ext =
			coex_sta->bt_info_c2h[rsp_source][4];

#if 0 /* REMOVE */
		/* Here we need to resend some wifi info to BT */
		/* because bt is reset and loss of the info. */
		if ((coex_sta->bt_info_ext & BIT(1))) {

			if (wifi_connected)
				ex_halbtc8821aCsr2ant_media_status_notify(
					btcoexist, BTC_MEDIA_CONNECT);
			else
				ex_halbtc8821aCsr2ant_media_status_notify(
					btcoexist, BTC_MEDIA_DISCONNECT);
		}
#endif

	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);

	if (bt_info ==
	    BT_INFO_8821A_CSR_2ANT_B_CONNECTION) {	/* connection exists but no busy */
		coex_sta->bt_link_exist = true;
		coex_dm->bt_status =
			BT_8821A_CSR_2ANT_BT_STATUS_CONNECTED_IDLE;
	} else if (bt_info &
		BT_INFO_8821A_CSR_2ANT_B_CONNECTION) {	/* connection exists and some link is busy */
		coex_sta->bt_link_exist = true;

		if (bt_info & BT_INFO_8821A_CSR_2ANT_B_FTP)
			coex_sta->pan_exist = true;
		else
			coex_sta->pan_exist = false;

		if (bt_info & BT_INFO_8821A_CSR_2ANT_B_A2DP)
			coex_sta->a2dp_exist = true;
		else
			coex_sta->a2dp_exist = false;

		if (bt_info & BT_INFO_8821A_CSR_2ANT_B_HID)
			coex_sta->hid_exist = true;
		else
			coex_sta->hid_exist = false;

		if (bt_info & BT_INFO_8821A_CSR_2ANT_B_SCO_ESCO)
			coex_sta->sco_exist = true;
		else
			coex_sta->sco_exist = false;

		if (coex_sta->bt_info_ext & 0x80)
			coex_sta->slave = true; /* Slave */
		else
			coex_sta->slave = false; /* Master */

		coex_dm->bt_status =
			BT_8821A_CSR_2ANT_BT_STATUS_NON_IDLE;
	} else {
		coex_sta->bt_link_exist = false;
		coex_sta->pan_exist = false;
		coex_sta->a2dp_exist = false;
		coex_sta->slave = false;
		coex_sta->hid_exist = false;
		coex_sta->sco_exist = false;
		coex_dm->bt_status = BT_8821A_CSR_2ANT_BT_STATUS_IDLE;
	}

	if (bt_hs_on)
		coex_dm->bt_status =
			BT_8821A_CSR_2ANT_BT_STATUS_NON_IDLE;

	if (bt_info & BT_INFO_8821A_CSR_2ANT_B_INQ_PAGE) {
		coex_sta->c2h_bt_inquiry_page = true;
		coex_dm->bt_status = BT_8821A_CSR_2ANT_BT_STATUS_NON_IDLE;
	} else
		coex_sta->c2h_bt_inquiry_page = false;


	if (BT_8821A_CSR_2ANT_BT_STATUS_NON_IDLE == coex_dm->bt_status)
		bt_busy = true;
	else
		bt_busy = false;
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bt_busy);

	if (BT_8821A_CSR_2ANT_BT_STATUS_IDLE != coex_dm->bt_status)
		limited_dig = true;
	else
		limited_dig = false;
	coex_dm->limited_dig = limited_dig;
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_LIMITED_DIG, &limited_dig);

	halbtc8821aCsr2ant_run_coexist_mechanism(btcoexist);
}

void ex_halbtc8821aCsr2ant_halt_notify(IN struct btc_coexist *btcoexist)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Halt notify\n");
	BTC_TRACE(trace_buf);

	halbtc8821aCsr2ant_ignore_wlan_act(btcoexist, FORCE_EXEC, true);
	ex_halbtc8821aCsr2ant_media_status_notify(btcoexist,
			BTC_MEDIA_DISCONNECT);
}

void ex_halbtc8821aCsr2ant_pnp_notify(IN struct btc_coexist *btcoexist,
				      IN u8 pnp_state)
{
	BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE, "[BTCoex], Pnp notify\n");
	BTC_TRACE(trace_buf);

	if (BTC_WIFI_PNP_SLEEP == pnp_state) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Pnp notify to SLEEP\n");
		BTC_TRACE(trace_buf);
		halbtc8821aCsr2ant_ignore_wlan_act(btcoexist, FORCE_EXEC, true);

		/* Sinda 20150819, workaround for driver skip leave IPS/LPS to speed up sleep time. */
		/* Driver do not leave IPS/LPS when driver is going to sleep, so BTCoexistence think wifi is still under IPS/LPS */
		/* BT should clear UnderIPS/UnderLPS state to avoid mismatch state after wakeup. */
		coex_sta->under_ips = false;
		coex_sta->under_lps = false;
	} else if (BTC_WIFI_PNP_WAKE_UP == pnp_state) {
		BTC_SPRINTF(trace_buf, BT_TMP_BUF_SIZE,
			    "[BTCoex], Pnp notify to WAKE UP\n");
		BTC_TRACE(trace_buf);
	}
}

void ex_halbtc8821aCsr2ant_periodical(IN struct btc_coexist *btcoexist)
{
	halbtc8821aCsr2ant_monitor_bt_ctr(btcoexist);
	halbtc8821aCsr2ant_monitor_bt_enable_disable(btcoexist);
}

#endif

#endif	/* #if (BT_SUPPORT == 1 && COEX_SUPPORT == 1) */