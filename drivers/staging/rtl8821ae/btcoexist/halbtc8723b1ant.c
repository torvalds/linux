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
#if 1
/***************************************************************
 * Global variables, these are static variables
 ***************************************************************/
static struct coex_dm_8723b_1ant glcoex_dm_8723b_1ant;
static struct coex_dm_8723b_1ant *coex_dm = &glcoex_dm_8723b_1ant;
static struct coex_sta_8723b_1ant glcoex_sta_8723b_1ant;
static struct coex_sta_8723b_1ant *coex_sta = &glcoex_sta_8723b_1ant;

const char *const GLBtInfoSrc8723b1Ant[]={
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

u32 glcoex_ver_date_8723b_1ant = 20130906;
u32 glcoex_ver_8723b_1ant = 0x45;

/***************************************************************
 * local function proto type if needed
 ***************************************************************/
/***************************************************************
 * local function start with halbtc8723b1ant_
 ***************************************************************/
u8 halbtc8723b1ant_bt_rssi_state(u8 level_num, u8 rssi_thresh, u8 rssi_thresh1)
{
	s32 bt_rssi=0;
	u8 bt_rssi_state = coex_sta->pre_bt_rssi_state;

	bt_rssi = coex_sta->bt_rssi;

	if (level_num == 2){
		if ((coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_STAY_LOW)) {
			if (bt_rssi >= rssi_thresh +
					BTC_RSSI_COEX_THRESH_TOL_8723B_1ANT) {
				bt_rssi_state = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "switch to High\n");
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "stay at Low\n");
			}
		} else {
			if (bt_rssi < rssi_thresh) {
				bt_rssi_state = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "switch to Low\n");
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "stay at High\n");
			}
		}
	} else if (level_num == 3) {
		if (rssi_thresh > rssi_thresh1) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
				  "[BTCoex], BT Rssi thresh error!!\n");
			return coex_sta->pre_bt_rssi_state;
		}

		if ((coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_STAY_LOW)) {
			if (bt_rssi >= rssi_thresh +
					BTC_RSSI_COEX_THRESH_TOL_8723B_1ANT) {
				bt_rssi_state = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "switch to Medium\n");
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "stay at Low\n");
			}
		} else if ((coex_sta->pre_bt_rssi_state ==
					BTC_RSSI_STATE_MEDIUM) ||
			  (coex_sta->pre_bt_rssi_state ==
			  		BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (bt_rssi >= rssi_thresh1 +
					BTC_RSSI_COEX_THRESH_TOL_8723B_1ANT) {
				bt_rssi_state = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "switch to High\n");
			} else if (bt_rssi < rssi_thresh) {
				bt_rssi_state = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "switch to Low\n");
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "stay at Medium\n");
			}
		} else {
			if (bt_rssi < rssi_thresh1) {
				bt_rssi_state = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "switch to Medium\n");
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state "
					  "stay at High\n");
			}
		}
	}

	coex_sta->pre_bt_rssi_state = bt_rssi_state;

	return bt_rssi_state;
}

u8 halbtc8723b1ant_wifi_rssi_state(struct btc_coexist *btcoexist,
				   u8 index, u8 level_num,
				   u8 rssi_thresh, u8 rssi_thresh1)
{
	s32 wifi_rssi=0;
	u8 wifi_rssi_state = coex_sta->pre_wifi_rssi_state[index];

	btcoexist->btc_get(btcoexist,
		BTC_GET_S4_WIFI_RSSI, &wifi_rssi);

	if (level_num == 2) {
		if ((coex_sta->pre_wifi_rssi_state[index] ==
					BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_wifi_rssi_state[index] ==
		    			BTC_RSSI_STATE_STAY_LOW)) {
			if (wifi_rssi >= rssi_thresh +
					BTC_RSSI_COEX_THRESH_TOL_8723B_1ANT) {
				wifi_rssi_state = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "switch to High\n");
			} else {
				wifi_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "stay at Low\n");
			}
		} else {
			if (wifi_rssi < rssi_thresh) {
				wifi_rssi_state = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "switch to Low\n");
			} else {
				wifi_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "stay at High\n");
			}
		}
	} else if (level_num == 3) {
		if (rssi_thresh > rssi_thresh1) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE,
				  "[BTCoex], wifi RSSI thresh error!!\n");
			return coex_sta->pre_wifi_rssi_state[index];
		}

		if ((coex_sta->pre_wifi_rssi_state[index] ==
						BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_wifi_rssi_state[index] ==
		    				BTC_RSSI_STATE_STAY_LOW)) {
			if (wifi_rssi >= rssi_thresh +
					 BTC_RSSI_COEX_THRESH_TOL_8723B_1ANT) {
				wifi_rssi_state = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "switch to Medium\n");
			} else {
				wifi_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "stay at Low\n");
			}
		} else if ((coex_sta->pre_wifi_rssi_state[index] ==
						BTC_RSSI_STATE_MEDIUM) ||
			   (coex_sta->pre_wifi_rssi_state[index] ==
						BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (wifi_rssi >= rssi_thresh1 +
					 BTC_RSSI_COEX_THRESH_TOL_8723B_1ANT) {
				wifi_rssi_state = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "switch to High\n");
			} else if (wifi_rssi < rssi_thresh) {
				wifi_rssi_state = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "switch to Low\n");
			} else {
				wifi_rssi_state = BTC_RSSI_STATE_STAY_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "stay at Medium\n");
			}
		} else {
			if (wifi_rssi < rssi_thresh1) {
				wifi_rssi_state = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "switch to Medium\n");
			} else {
				wifi_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state "
					  "stay at High\n");
			}
		}
	}

	coex_sta->pre_wifi_rssi_state[index] = wifi_rssi_state;

	return wifi_rssi_state;
}

void halbtc8723b1ant_updatera_mask(struct btc_coexist *btcoexist,
				   bool force_exec, u32 dis_rate_mask)
{
	coex_dm->curra_mask = dis_rate_mask;

	if (force_exec || (coex_dm->prera_mask != coex_dm->curra_mask))
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_UPDATE_ra_mask,
				   &coex_dm->curra_mask);

	coex_dm->prera_mask = coex_dm->curra_mask;
}

void halbtc8723b1ant_auto_rate_fallback_retry(struct btc_coexist *btcoexist,
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

void halbtc8723b1ant_retry_limit(struct btc_coexist *btcoexist,
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
		case 1:	/* retry limit=8 */
			btcoexist->btc_write_2byte(btcoexist, 0x42a, 0x0808);
			break;
		default:
			break;
		}
	}

	coex_dm->pre_retry_limit_type = coex_dm->cur_retry_limit_type;
}

void halbtc8723b1ant_ampdu_maxtime(struct btc_coexist *btcoexist,
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
				btcoexist->btc_write_1byte(btcoexist,
							   0x456, 0x38);
				break;
			default:
				break;
		}
	}

	coex_dm->pre_ampdu_time_type = coex_dm->cur_ampdu_time_type;
}

void halbtc8723b1ant_limited_tx(struct btc_coexist *btcoexist,
				bool force_exec, u8 ra_maskType, u8 arfr_type,
				u8 retry_limit_type, u8 ampdu_time_type)
{
	switch (ra_maskType) {
	case 0:	/* normal mode */
		halbtc8723b1ant_updatera_mask(btcoexist, force_exec, 0x0);
		break;
	case 1:	/* disable cck 1/2 */
		halbtc8723b1ant_updatera_mask(btcoexist, force_exec,
					      0x00000003);
		break;
	/* disable cck 1/2/5.5, ofdm 6/9/12/18/24, mcs 0/1/2/3/4*/
	case 2:
		halbtc8723b1ant_updatera_mask(btcoexist, force_exec,
					      0x0001f1f7);
		break;
	default:
		break;
	}

	halbtc8723b1ant_auto_rate_fallback_retry(btcoexist, force_exec,
						 arfr_type);
	halbtc8723b1ant_retry_limit(btcoexist, force_exec, retry_limit_type);
	halbtc8723b1ant_ampdu_maxtime(btcoexist, force_exec, ampdu_time_type);
}

void halbtc8723b1ant_limited_rx(struct btc_coexist *btcoexist,
				bool force_exec, bool rej_ap_agg_pkt,
				bool b_bt_ctrl_agg_buf_size, u8 agg_buf_size)
{
	bool reject_rx_agg = rej_ap_agg_pkt;
	bool bt_ctrl_rx_agg_size = b_bt_ctrl_agg_buf_size;
	u8 rxAggSize = agg_buf_size;

	/**********************************************
	 *	Rx Aggregation related setting
	 **********************************************/
	btcoexist->btc_set(btcoexist, BTC_SET_BL_TO_REJ_AP_AGG_PKT,
			   &reject_rx_agg);
	/* decide BT control aggregation buf size or not  */
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_CTRL_AGG_SIZE,
			   &bt_ctrl_rx_agg_size);
	/* aggregation buf size, only work
	 *when BT control Rx aggregation size.  */
	btcoexist->btc_set(btcoexist, BTC_SET_U1_AGG_BUF_SIZE, &rxAggSize);
	/* real update aggregation setting  */
	btcoexist->btc_set(btcoexist, BTC_SET_ACT_AGGREGATE_CTRL, NULL);
}

void halbtc8723b1ant_monitor_bt_ctr(struct btc_coexist *btcoexist)
{
	u32 reg_hp_txrx, reg_lp_txrx, u32tmp;
	u32 reg_hp_tx = 0, reg_hp_rx = 0;
	u32 reg_lp_tx = 0, reg_lp_rx = 0;

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

	/* reset counter */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0xc);
}

void halbtc8723b1ant_query_bt_info(struct btc_coexist *btcoexist)
{
	u8 h2c_parameter[1] = {0};

	coex_sta->c2h_bt_info_req_sent = true;

	h2c_parameter[0] |= BIT0;	/* trigger*/

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], Query Bt Info, FW write 0x61=0x%x\n",
		  h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x61, 1, h2c_parameter);
}

bool halbtc8723b1ant_is_wifi_status_changed(struct btc_coexist *btcoexist)
{
	static bool pre_wifi_busy = false;
	static bool pre_under_4way = false, pre_bt_hs_on = false;
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

void halbtc8723b1ant_update_bt_link_info(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool bt_hs_on = false;

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
	    !bt_link_info->pan_exist && bt_link_info->hid_exist )
		bt_link_info->hid_only = true;
	else
		bt_link_info->hid_only = false;
}

u8 halbtc8723b1ant_action_algorithm(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool bt_hs_on = false;
	u8 algorithm = BT_8723B_1ANT_COEX_ALGO_UNDEFINED;
	u8 numOfDiffProfile = 0;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);

	if (!bt_link_info->bt_link_exist) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], No BT link exists!!!\n");
		return algorithm;
	}

	if (bt_link_info->sco_exist)
		numOfDiffProfile++;
	if (bt_link_info->hid_exist)
		numOfDiffProfile++;
	if (bt_link_info->pan_exist)
		numOfDiffProfile++;
	if (bt_link_info->a2dp_exist)
		numOfDiffProfile++;

	if (numOfDiffProfile == 1) {
		if (bt_link_info->sco_exist) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], BT Profile = SCO only\n");
			algorithm = BT_8723B_1ANT_COEX_ALGO_SCO;
		} else {
			if (bt_link_info->hid_exist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
					  "[BTCoex], BT Profile = HID only\n");
				algorithm = BT_8723B_1ANT_COEX_ALGO_HID;
			} else if (bt_link_info->a2dp_exist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
					  "[BTCoex], BT Profile = A2DP only\n");
				algorithm = BT_8723B_1ANT_COEX_ALGO_A2DP;
			} else if (bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], BT Profile = "
						  "PAN(HS) only\n");
					algorithm =
						BT_8723B_1ANT_COEX_ALGO_PANHS;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], BT Profile = "
						  "PAN(EDR) only\n");
					algorithm =
						BT_8723B_1ANT_COEX_ALGO_PANEDR;
				}
			}
		}
	} else if (numOfDiffProfile == 2) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
					  "[BTCoex], BT Profile = SCO + HID\n");
				algorithm = BT_8723B_1ANT_COEX_ALGO_HID;
			} else if (bt_link_info->a2dp_exist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
					  "[BTCoex], BT Profile = "
					  "SCO + A2DP ==> SCO\n");
				algorithm = BT_8723B_1ANT_COEX_ALGO_SCO;
			} else if (bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], BT Profile "
						  "= SCO + PAN(HS)\n");
					algorithm = BT_8723B_1ANT_COEX_ALGO_SCO;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], BT Profile "
						  "= SCO + PAN(EDR)\n");
					algorithm =
					    BT_8723B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (bt_link_info->hid_exist &&
			    bt_link_info->a2dp_exist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
					  "[BTCoex], BT Profile = "
					  "HID + A2DP\n");
				algorithm = BT_8723B_1ANT_COEX_ALGO_HID_A2DP;
			} else if (bt_link_info->hid_exist &&
				   bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], BT Profile = "
						  "HID + PAN(HS)\n");
					algorithm =
					    BT_8723B_1ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], BT Profile = "
						  "HID + PAN(EDR)\n");
					algorithm =
					    BT_8723B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (bt_link_info->pan_exist &&
				   bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], BT Profile = "
						  "A2DP + PAN(HS)\n");
					algorithm =
					    BT_8723B_1ANT_COEX_ALGO_A2DP_PANHS;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], BT Profile = "
						  "A2DP + PAN(EDR)\n");
					algorithm =
					    BT_8723B_1ANT_COEX_ALGO_PANEDR_A2DP;
				}
			}
		}
	} else if (numOfDiffProfile == 3) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist &&
			    bt_link_info->a2dp_exist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
					  "[BTCoex], BT Profile = "
					  "SCO + HID + A2DP ==> HID\n");
				algorithm = BT_8723B_1ANT_COEX_ALGO_HID;
			} else if (bt_link_info->hid_exist &&
				   bt_link_info->pan_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], BT Profile = "
						  "SCO + HID + PAN(HS)\n");
					algorithm =
					    BT_8723B_1ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], BT Profile = "
						  "SCO + HID + PAN(EDR)\n");
					algorithm =
					    BT_8723B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (bt_link_info->pan_exist &&
				   bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], BT Profile = "
						  "SCO + A2DP + PAN(HS)\n");
					algorithm = BT_8723B_1ANT_COEX_ALGO_SCO;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], BT Profile = SCO + "
						  "A2DP + PAN(EDR) ==> HID\n");
					algorithm =
					    BT_8723B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (bt_link_info->hid_exist &&
			    bt_link_info->pan_exist &&
			    bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], BT Profile = "
						  "HID + A2DP + PAN(HS)\n");
					algorithm =
					    BT_8723B_1ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], BT Profile = "
						  "HID + A2DP + PAN(EDR)\n");
					algorithm =
					    BT_8723B_1ANT_COEX_ALGO_HID_A2DP_PANEDR;
				}
			}
		}
	} else if (numOfDiffProfile >= 3) {
		if (bt_link_info->sco_exist) {
			if (bt_link_info->hid_exist &&
			    bt_link_info->pan_exist &&
			    bt_link_info->a2dp_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], Error!!! "
						  "BT Profile = SCO + "
						  "HID + A2DP + PAN(HS)\n");
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], BT Profile = "
						  "SCO + HID + A2DP + PAN(EDR)"
						  "==>PAN(EDR)+HID\n");
					algorithm =
					    BT_8723B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
	}

	return algorithm;
}

bool halbtc8723b1ant_need_to_dec_bt_pwr(struct btc_coexist *btcoexist)
{
	bool ret = false;
	bool bt_hs_on = false, wifi_connected = false;
	s32 bt_hs_rssi = 0;
	u8 bt_rssi_state;

	if (!btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on))
		return false;
	if (!btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
				&wifi_connected))
		return false;
	if (!btcoexist->btc_get(btcoexist, BTC_GET_S4_HS_RSSI, &bt_hs_rssi))
		return false;

	bt_rssi_state = halbtc8723b1ant_bt_rssi_state(2, 35, 0);

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

void halbtc8723b1ant_set_fw_dac_swing_level(struct btc_coexist *btcoexist,
					    u8 dac_swing_lvl)
{
	u8 h2c_parameter[1] = {0};

	/* There are several type of dacswing
	 * 0x18/ 0x10/ 0xc/ 0x8/ 0x4/ 0x6 */
	h2c_parameter[0] = dac_swing_lvl;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], Set Dac Swing Level=0x%x\n", dac_swing_lvl);
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], FW write 0x64=0x%x\n", h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x64, 1, h2c_parameter);
}

void halbtc8723b1ant_set_fw_dec_bt_pwr(struct btc_coexist *btcoexist,
				       bool dec_bt_pwr)
{
	u8 h2c_parameter[1] = {0};

	h2c_parameter[0] = 0;

	if (dec_bt_pwr)
		h2c_parameter[0] |= BIT1;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], decrease Bt Power : %s, FW write 0x62=0x%x\n",
		  (dec_bt_pwr? "Yes!!":"No!!"),h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x62, 1, h2c_parameter);
}

void halbtc8723b1ant_dec_bt_pwr(struct btc_coexist *btcoexist,
				bool force_exec, bool dec_bt_pwr)
{
	return;
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
		  "[BTCoex], %s Dec BT power = %s\n",
		  (force_exec ? "force to" : ""), (dec_bt_pwr ? "ON" : "OFF"));
	coex_dm->cur_dec_bt_pwr = dec_bt_pwr;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], bPreDecBtPwr=%d, bCurDecBtPwr=%d\n",
			  coex_dm->pre_dec_bt_pwr, coex_dm->cur_dec_bt_pwr);

		if (coex_dm->pre_dec_bt_pwr == coex_dm->cur_dec_bt_pwr)
			return;
	}
	halbtc8723b1ant_set_fw_dec_bt_pwr(btcoexist, coex_dm->cur_dec_bt_pwr);

	coex_dm->pre_dec_bt_pwr = coex_dm->cur_dec_bt_pwr;
}

void halbtc8723b1ant_set_bt_auto_report(struct btc_coexist *btcoexist,
					bool enable_auto_report)
{
	u8 h2c_parameter[1] = {0};

	h2c_parameter[0] = 0;

	if (enable_auto_report)
		h2c_parameter[0] |= BIT0;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], BT FW auto report : %s, FW write 0x68=0x%x\n",
		  (enable_auto_report? "Enabled!!":"Disabled!!"),
		  h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x68, 1, h2c_parameter);
}

void halbtc8723b1ant_bt_auto_report(struct btc_coexist *btcoexist,
				    bool force_exec, bool enable_auto_report)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
		  "[BTCoex], %s BT Auto report = %s\n",
		  (force_exec? "force to":""),
		  ((enable_auto_report)? "Enabled":"Disabled"));
	coex_dm->cur_bt_auto_report = enable_auto_report;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], bPreBtAutoReport=%d, "
			  "bCurBtAutoReport=%d\n",
			  coex_dm->pre_bt_auto_report,
			  coex_dm->cur_bt_auto_report);

		if (coex_dm->pre_bt_auto_report == coex_dm->cur_bt_auto_report)
			return;
	}
	halbtc8723b1ant_set_bt_auto_report(btcoexist,
					   coex_dm->cur_bt_auto_report);

	coex_dm->pre_bt_auto_report = coex_dm->cur_bt_auto_report;
}

void halbtc8723b1ant_fw_dac_swing_lvl(struct btc_coexist *btcoexist,
				      bool force_exec, u8 fw_dac_swing_lvl)
{
	return;
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
		  "[BTCoex], %s set FW Dac Swing level = %d\n",
		  (force_exec? "force to":""), fw_dac_swing_lvl);
	coex_dm->cur_fw_dac_swing_lvl = fw_dac_swing_lvl;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], preFwDacSwingLvl=%d, "
			  "curFwDacSwingLvl=%d\n",
			  coex_dm->pre_fw_dac_swing_lvl,
			  coex_dm->cur_fw_dac_swing_lvl);

		if (coex_dm->pre_fw_dac_swing_lvl ==
		    coex_dm->cur_fw_dac_swing_lvl)
			return;
	}

	halbtc8723b1ant_set_fw_dac_swing_level(btcoexist,
					       coex_dm->cur_fw_dac_swing_lvl);

	coex_dm->pre_fw_dac_swing_lvl = coex_dm->cur_fw_dac_swing_lvl;
}

void halbtc8723b1ant_set_sw_rf_rx_lpf_corner(struct btc_coexist *btcoexist,
					     bool rx_rf_shrink_on)
{
	if (rx_rf_shrink_on) {
		/*Shrink RF Rx LPF corner */
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
			  "[BTCoex], Shrink RF Rx LPF corner!!\n");
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1e,
					  0xfffff, 0xffff7);
	} else {
		/*Resume RF Rx LPF corner
		 * After initialized, we can use coex_dm->btRf0x1eBackup */
		if (btcoexist->initilized) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
				  "[BTCoex], Resume RF Rx LPF corner!!\n");
			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A,
						  0x1e, 0xfffff,
						  coex_dm->bt_rf0x1e_backup);
		}
	}
}

void halbtc8723b1ant_rf_shrink(struct btc_coexist *btcoexist,
			       bool force_exec, bool rx_rf_shrink_on)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW,
		  "[BTCoex], %s turn Rx RF Shrink = %s\n",
		  (force_exec? "force to":""),
		  ((rx_rf_shrink_on)? "ON":"OFF"));
	coex_dm->cur_rf_rx_lpf_shrink = rx_rf_shrink_on;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL,
			  "[BTCoex], bPreRfRxLpfShrink=%d, "
			  "bCurRfRxLpfShrink=%d\n",
			  coex_dm->pre_rf_rx_lpf_shrink,
			  coex_dm->cur_rf_rx_lpf_shrink);

		if (coex_dm->pre_rf_rx_lpf_shrink ==
		    coex_dm->cur_rf_rx_lpf_shrink)
			return;
	}
	halbtc8723b1ant_set_sw_rf_rx_lpf_corner(btcoexist,
						coex_dm->cur_rf_rx_lpf_shrink);

	coex_dm->pre_rf_rx_lpf_shrink = coex_dm->cur_rf_rx_lpf_shrink;
}

void halbtc8723b1ant_set_sw_penalty_tx_rate_adaptive(
					struct btc_coexist *btcoexist,
					bool low_penalty_ra)
{
	u8 h2c_parameter[6] = {0};

	h2c_parameter[0] = 0x6;	/* opCode, 0x6= Retry_Penalty */

	if (low_penalty_ra) {
		h2c_parameter[1] |= BIT0;
		/*normal rate except MCS7/6/5, OFDM54/48/36 */
		h2c_parameter[2] = 0x00;
		h2c_parameter[3] = 0xf7;  /*MCS7 or OFDM54 */
		h2c_parameter[4] = 0xf8;  /*MCS6 or OFDM48 */
		h2c_parameter[5] = 0xf9;  /*MCS5 or OFDM36 */
	}

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], set WiFi Low-Penalty Retry: %s",
		  (low_penalty_ra ? "ON!!" : "OFF!!"));

	btcoexist->btc_fill_h2c(btcoexist, 0x69, 6, h2c_parameter);
}

void halbtc8723b1ant_low_penalty_ra(struct btc_coexist *btcoexist,
				    bool force_exec, bool low_penalty_ra)
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

void halbtc8723b1ant_set_dac_swing_reg(struct btc_coexist *btcoexist, u32 level)
{
	u8 val = (u8) level;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
		  "[BTCoex], Write SwDacSwing = 0x%x\n", level);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x883, 0x3e, val);
}

void halbtc8723b1ant_set_sw_full_time_dac_swing(struct btc_coexist *btcoexist,
						bool sw_dac_swing_on,
						u32 sw_dac_swing_lvl)
{
	if (sw_dac_swing_on)
		halbtc8723b1ant_set_dac_swing_reg(btcoexist, sw_dac_swing_lvl);
	else
		halbtc8723b1ant_set_dac_swing_reg(btcoexist, 0x18);
}


void halbtc8723b1ant_dac_swing(struct btc_coexist *btcoexist, bool force_exec,
			       bool dac_swing_on, u32 dac_swing_lvl)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW,
		  "[BTCoex], %s turn DacSwing=%s, dac_swing_lvl=0x%x\n",
		  (force_exec ? "force to" : ""), (dac_swing_on ? "ON" : "OFF"),
		  dac_swing_lvl);

	coex_dm->cur_dac_swing_on = dac_swing_on;
	coex_dm->cur_dac_swing_lvl = dac_swing_lvl;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL,
			  "[BTCoex], bPreDacSwingOn=%d, preDacSwingLvl=0x%x, "
			  "bCurDacSwingOn=%d, curDacSwingLvl=0x%x\n",
			  coex_dm->pre_dac_swing_on, coex_dm->pre_dac_swing_lvl,
			  coex_dm->cur_dac_swing_on,
			  coex_dm->cur_dac_swing_lvl);

		if ((coex_dm->pre_dac_swing_on == coex_dm->cur_dac_swing_on) &&
		    (coex_dm->pre_dac_swing_lvl == coex_dm->cur_dac_swing_lvl))
			return;
	}
	mdelay(30);
	halbtc8723b1ant_set_sw_full_time_dac_swing(btcoexist, dac_swing_on,
						   dac_swing_lvl);

	coex_dm->pre_dac_swing_on = coex_dm->cur_dac_swing_on;
	coex_dm->pre_dac_swing_lvl = coex_dm->cur_dac_swing_lvl;
}

void halbtc8723b1ant_set_adc_backoff(struct btc_coexist *btcoexist,
				     bool adc_backoff)
{
	if (adc_backoff) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
			  "[BTCoex], BB BackOff Level On!\n");
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x8db, 0x60, 0x3);
	} else {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
			  "[BTCoex], BB BackOff Level Off!\n");
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x8db, 0x60, 0x1);
	}
}

void halbtc8723b1ant_adc_backoff(struct btc_coexist *btcoexist,
				 bool force_exec, bool adc_backoff)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW,
		  "[BTCoex], %s turn AdcBackOff = %s\n",
		  (force_exec ? "force to" : ""), (adc_backoff ? "ON" : "OFF"));
	coex_dm->cur_adc_backoff = adc_backoff;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL,
			  "[BTCoex], bPreAdcBackOff=%d, bCurAdcBackOff=%d\n",
			  coex_dm->pre_adc_backoff, coex_dm->cur_adc_backoff);

		if(coex_dm->pre_adc_backoff == coex_dm->cur_adc_backoff)
			return;
	}
	halbtc8723b1ant_set_adc_backoff(btcoexist, coex_dm->cur_adc_backoff);

	coex_dm->pre_adc_backoff =
		coex_dm->cur_adc_backoff;
}

void halbtc8723b1ant_set_agc_table(struct btc_coexist *btcoexist,
				   bool adc_table_en)
{
	u8 rssi_adjust_val = 0;

	btcoexist->btc_set_rf_reg(btcoexist,
		BTC_RF_A, 0xef, 0xfffff, 0x02000);
	if (adc_table_en) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
			  "[BTCoex], Agc Table On!\n");
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b,
					  0xfffff, 0x3fa58);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b,
					  0xfffff, 0x37a58);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b,
					  0xfffff, 0x2fa58);
		rssi_adjust_val = 8;
	} else {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
			  "[BTCoex], Agc Table Off!\n");
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b,
					  0xfffff, 0x39258);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b,
					  0xfffff, 0x31258);
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x3b,
					  0xfffff, 0x29258);
	}
	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0xef, 0xfffff, 0x0);

	/* set rssi_adjust_val for wifi module. */
	btcoexist->btc_set(btcoexist, BTC_SET_U1_RSSI_ADJ_VAL_FOR_AGC_TABLE_ON,
			   &rssi_adjust_val);
}


void halbtc8723b1ant_agc_table(struct btc_coexist *btcoexist,
			       bool force_exec, bool adc_table_en)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW,
		  "[BTCoex], %s %s Agc Table\n",
		  (force_exec ? "force to" : ""),
		  (adc_table_en ? "Enable" : "Disable"));
	coex_dm->cur_agc_table_en = adc_table_en;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL,
			  "[BTCoex], bPreAgcTableEn=%d, bCurAgcTableEn=%d\n",
			  coex_dm->pre_agc_table_en,
			  coex_dm->cur_agc_table_en);

		if(coex_dm->pre_agc_table_en == coex_dm->cur_agc_table_en)
			return;
	}
	halbtc8723b1ant_set_agc_table(btcoexist, adc_table_en);

	coex_dm->pre_agc_table_en = coex_dm->cur_agc_table_en;
}

void halbtc8723b1ant_set_coex_table(struct btc_coexist *btcoexist,
				    u32 val0x6c0, u32 val0x6c4,
				    u32 val0x6c8, u8 val0x6cc)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
		  "[BTCoex], set coex table, set 0x6c0=0x%x\n", val0x6c0);
	btcoexist->btc_write_4byte(btcoexist, 0x6c0, val0x6c0);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
		  "[BTCoex], set coex table, set 0x6c4=0x%x\n", val0x6c4);
	btcoexist->btc_write_4byte(btcoexist, 0x6c4, val0x6c4);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
		  "[BTCoex], set coex table, set 0x6c8=0x%x\n", val0x6c8);
	btcoexist->btc_write_4byte(btcoexist, 0x6c8, val0x6c8);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
		  "[BTCoex], set coex table, set 0x6cc=0x%x\n", val0x6cc);
	btcoexist->btc_write_1byte(btcoexist, 0x6cc, val0x6cc);
}

void halbtc8723b1ant_coex_table(struct btc_coexist *btcoexist,
 				bool force_exec, u32 val0x6c0,
 				u32 val0x6c4, u32 val0x6c8,
 				u8 val0x6cc)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW,
		  "[BTCoex], %s write Coex Table 0x6c0=0x%x,"
		  " 0x6c4=0x%x, 0x6cc=0x%x\n", (force_exec ? "force to" : ""),
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

void halbtc8723b1ant_coex_table_with_type(struct btc_coexist *btcoexist,
					  bool force_exec, u8 type)
{
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
					   0xaaaaaaaa, 0xffffff, 0x3);
		break;
	case 4:
		halbtc8723b1ant_coex_table(btcoexist, force_exec, 0x55555555,
					   0x5aaa5aaa, 0xffffff, 0x3);
		break;
	case 5:
		halbtc8723b1ant_coex_table(btcoexist, force_exec, 0x5a5a5a5a,
					   0xaaaa5a5a, 0xffffff, 0x3);
		break;
	case 6:
		halbtc8723b1ant_coex_table(btcoexist, force_exec, 0x55555555,
					   0xaaaa5a5a, 0xffffff, 0x3);
		break;
	case 7:
		halbtc8723b1ant_coex_table(btcoexist, force_exec, 0x5afa5afa,
					   0x5afa5afa, 0xffffff, 0x3);
		break;
	default:
		break;
	}
}

void halbtc8723b1ant_SetFwIgnoreWlanAct(struct btc_coexist *btcoexist,
					bool enable)
{
	u8 h2c_parameter[1] = {0};

	if (enable)
		h2c_parameter[0] |= BIT0;	/* function enable */

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], set FW for BT Ignore Wlan_Act,"
		  " FW write 0x63=0x%x\n", h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x63, 1, h2c_parameter);
}

void halbtc8723b1ant_ignore_wlan_act(struct btc_coexist *btcoexist,
				     bool force_exec, bool enable)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
		  "[BTCoex], %s turn Ignore WlanAct %s\n",
		  (force_exec ? "force to" : ""), (enable ? "ON" : "OFF"));
	coex_dm->cur_ignore_wlan_act = enable;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], bPreIgnoreWlanAct = %d, "
			  "bCurIgnoreWlanAct = %d!!\n",
			  coex_dm->pre_ignore_wlan_act,
			  coex_dm->cur_ignore_wlan_act);

		if (coex_dm->pre_ignore_wlan_act ==
		    coex_dm->cur_ignore_wlan_act)
			return;
	}
	halbtc8723b1ant_SetFwIgnoreWlanAct(btcoexist, enable);

	coex_dm->pre_ignore_wlan_act = coex_dm->cur_ignore_wlan_act;
}

void halbtc8723b1ant_set_fw_ps_tdma(struct btc_coexist *btcoexist,
				    u8 byte1, u8 byte2, u8 byte3,
				    u8 byte4, u8 byte5)
{
	u8 h2c_parameter[5] = {0};
	u8 real_byte1 = byte1, real_byte5 = byte5;
	bool ap_enable = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE,
			   &ap_enable);

	if (ap_enable) {
		if ((byte1 & BIT4) && !(byte1 & BIT5)) {
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
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

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], PS-TDMA H2C cmd =0x%x%08x\n",
		  h2c_parameter[0],
		  h2c_parameter[1] << 24 |
		  h2c_parameter[2] << 16 |
		  h2c_parameter[3] << 8 |
		  h2c_parameter[4]);

	btcoexist->btc_fill_h2c(btcoexist, 0x60, 5, h2c_parameter);
}

void halbtc8723b1ant_SetLpsRpwm(struct btc_coexist *btcoexist,
				u8 lps_val, u8 rpwm_val)
{
	u8 lps = lps_val;
	u8 rpwm = rpwm_val;

	btcoexist->btc_set(btcoexist, BTC_SET_U1_1ANT_LPS, &lps);
	btcoexist->btc_set(btcoexist, BTC_SET_U1_1ANT_RPWM, &rpwm);
}

void halbtc8723b1ant_LpsRpwm(struct btc_coexist *btcoexist, bool force_exec,
			     u8 lps_val, u8 rpwm_val)
{

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
		  "[BTCoex], %s set lps/rpwm=0x%x/0x%x \n",
		  (force_exec ? "force to" : ""), lps_val, rpwm_val);
	coex_dm->cur_lps = lps_val;
	coex_dm->cur_rpwm = rpwm_val;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], LPS-RxBeaconMode=0x%x , LPS-RPWM=0x%x!!\n",
			  coex_dm->cur_lps, coex_dm->cur_rpwm);

		if ((coex_dm->pre_lps == coex_dm->cur_lps) &&
		    (coex_dm->pre_rpwm == coex_dm->cur_rpwm)) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
				  "[BTCoex], LPS-RPWM_Last=0x%x"
				  " , LPS-RPWM_Now=0x%x!!\n",
				  coex_dm->pre_rpwm, coex_dm->cur_rpwm);

			return;
		}
	}
	halbtc8723b1ant_SetLpsRpwm(btcoexist, lps_val, rpwm_val);

	coex_dm->pre_lps = coex_dm->cur_lps;
	coex_dm->pre_rpwm = coex_dm->cur_rpwm;
}

void halbtc8723b1ant_sw_mechanism1(struct btc_coexist *btcoexist,
				   bool shrink_rx_lpf, bool low_penalty_ra,
				   bool limited_dig, bool bt_lna_constrain)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR,
		  "[BTCoex], SM1[ShRf/ LpRA/ LimDig/ btLna] = %d %d %d %d\n",
		  shrink_rx_lpf, low_penalty_ra, limited_dig, bt_lna_constrain);

	halbtc8723b1ant_low_penalty_ra(btcoexist, NORMAL_EXEC, low_penalty_ra);
}

void halbtc8723b1ant_sw_mechanism2(struct btc_coexist *btcoexist,
				   bool agc_table_shift, bool adc_backoff,
				   bool sw_dac_swing, u32 dac_swing_lvl)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR,
		  "[BTCoex], SM2[AgcT/ AdcB/ SwDacSwing(lvl)] = %d %d %d\n",
		  agc_table_shift, adc_backoff, sw_dac_swing);
}

void halbtc8723b1ant_SetAntPath(struct btc_coexist *btcoexist,
				u8 ant_pos_type, bool init_hw_cfg,
				bool wifi_off)
{
	struct btc_board_info *board_info = &btcoexist->board_info;
	u32 fw_ver = 0, u32tmp = 0;
	bool pg_ext_switch = false;
	bool use_ext_switch = false;
	u8 h2c_parameter[2] = {0};

	btcoexist->btc_get(btcoexist, BTC_GET_BL_EXT_SWITCH, &pg_ext_switch);
	/* [31:16]=fw ver, [15:0]=fw sub ver */
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);


	if ((fw_ver < 0xc0000) || pg_ext_switch)
		use_ext_switch = true;

	if (init_hw_cfg){
		/*BT select s0/s1 is controlled by WiFi */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x20, 0x1);

		/*Force GNT_BT to Normal */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x765, 0x18, 0x0);
	} else if (wifi_off) {
		/*Force GNT_BT to High */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x765, 0x18, 0x3);
		/*BT select s0/s1 is controlled by BT */
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x67, 0x20, 0x0);

		/* 0x4c[24:23]=00, Set Antenna control by BT_RFE_CTRL
		 * BT Vendor 0xac=0xf002 */
		u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x4c);
		u32tmp &= ~BIT23;
		u32tmp &= ~BIT24;
		btcoexist->btc_write_4byte(btcoexist, 0x4c, u32tmp);
	}

	if (use_ext_switch) {
		if (init_hw_cfg) {
			/* 0x4c[23]=0, 0x4c[24]=1  Antenna control by WL/BT */
			u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x4c);
			u32tmp &= ~BIT23;
			u32tmp |= BIT24;
			btcoexist->btc_write_4byte(btcoexist, 0x4c, u32tmp);

			if (board_info->btdm_ant_pos ==
			    BTC_ANTENNA_AT_MAIN_PORT) {
				/* Main Ant to  BT for IPS case 0x4c[23]=1 */
				btcoexist->btc_write_1byte_bitmask(btcoexist,
								   0x64, 0x1,
								   0x1);

				/*tell firmware "no antenna inverse"*/
				h2c_parameter[0] = 0;
				h2c_parameter[1] = 1;  /*ext switch type*/
				btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
							h2c_parameter);
			} else {
				/*Aux Ant to  BT for IPS case 0x4c[23]=1 */
				btcoexist->btc_write_1byte_bitmask(btcoexist,
								   0x64, 0x1,
								   0x0);

				/*tell firmware "antenna inverse"*/
				h2c_parameter[0] = 1;
				h2c_parameter[1] = 1;  /*ext switch type*/
				btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
							h2c_parameter);
			}
		}

		/* fixed internal switch first*/
		/* fixed internal switch S1->WiFi, S0->BT*/
		if (board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT)
 			btcoexist->btc_write_2byte(btcoexist, 0x948, 0x0);
		else/* fixed internal switch S0->WiFi, S1->BT*/
			btcoexist->btc_write_2byte(btcoexist, 0x948, 0x280);

		/* ext switch setting */
		switch (ant_pos_type) {
		case BTC_ANT_PATH_WIFI:
			if (board_info->btdm_ant_pos ==
			    BTC_ANTENNA_AT_MAIN_PORT)
				btcoexist->btc_write_1byte_bitmask(btcoexist,
								   0x92c, 0x3,
								   0x1);
			else
				btcoexist->btc_write_1byte_bitmask(btcoexist,
								   0x92c, 0x3,
								   0x2);
			break;
		case BTC_ANT_PATH_BT:
			if (board_info->btdm_ant_pos ==
			    BTC_ANTENNA_AT_MAIN_PORT)
				btcoexist->btc_write_1byte_bitmask(btcoexist,
							    	   0x92c, 0x3,
							    	   0x2);
			else
				btcoexist->btc_write_1byte_bitmask(btcoexist,
							    	   0x92c, 0x3,
							    	   0x1);
			break;
		default:
		case BTC_ANT_PATH_PTA:
			if (board_info->btdm_ant_pos ==
			    BTC_ANTENNA_AT_MAIN_PORT)
				btcoexist->btc_write_1byte_bitmask(btcoexist,
							    	   0x92c, 0x3,
							    	   0x1);
			else
				btcoexist->btc_write_1byte_bitmask(btcoexist,
							    	   0x92c, 0x3,
							    	   0x2);
			break;
		}

	} else {
		if (init_hw_cfg) {
			/* 0x4c[23]=1, 0x4c[24]=0  Antenna control by 0x64*/
			u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x4c);
			u32tmp |= BIT23;
			u32tmp &= ~BIT24;
			btcoexist->btc_write_4byte(btcoexist, 0x4c, u32tmp);

			if (board_info->btdm_ant_pos ==
			    BTC_ANTENNA_AT_MAIN_PORT) {
				/*Main Ant to  WiFi for IPS case 0x4c[23]=1*/
				btcoexist->btc_write_1byte_bitmask(btcoexist,
								   0x64, 0x1,
								   0x0);

				/*tell firmware "no antenna inverse"*/
				h2c_parameter[0] = 0;
				h2c_parameter[1] = 0;  /*internal switch type*/
				btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
							h2c_parameter);
			} else {
				/*Aux Ant to  BT for IPS case 0x4c[23]=1*/
				btcoexist->btc_write_1byte_bitmask(btcoexist,
								   0x64, 0x1,
								   0x1);

				/*tell firmware "antenna inverse"*/
				h2c_parameter[0] = 1;
				h2c_parameter[1] = 0;  /*internal switch type*/
				btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
							h2c_parameter);
			}
		}

		/* fixed external switch first*/
		/*Main->WiFi, Aux->BT*/
		if(board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT)
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0x92c,
							   0x3, 0x1);
		else/*Main->BT, Aux->WiFi */
			btcoexist->btc_write_1byte_bitmask(btcoexist, 0x92c,
							   0x3, 0x2);

		/* internal switch setting*/
		switch (ant_pos_type) {
		case BTC_ANT_PATH_WIFI:
			if(board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT)
				btcoexist->btc_write_2byte(btcoexist, 0x948,
							   0x0);
			else
				btcoexist->btc_write_2byte(btcoexist, 0x948,
							   0x280);
			break;
		case BTC_ANT_PATH_BT:
			if(board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT)
				btcoexist->btc_write_2byte(btcoexist, 0x948,
							   0x280);
			else
				btcoexist->btc_write_2byte(btcoexist, 0x948,
							   0x0);
			break;
		default:
		case BTC_ANT_PATH_PTA:
			if(board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT)
				btcoexist->btc_write_2byte(btcoexist, 0x948,
							   0x200);
			else
				btcoexist->btc_write_2byte(btcoexist, 0x948,
							   0x80);
			break;
		}
	}
}

void halbtc8723b1ant_ps_tdma(struct btc_coexist *btcoexist, bool force_exec,
			     bool turn_on, u8 type)
{
	bool wifi_busy = false;
	u8 rssi_adjust_val = 0;

	coex_dm->cur_ps_tdma_on = turn_on;
	coex_dm->cur_ps_tdma = type;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	if (!force_exec) {
		if (coex_dm->cur_ps_tdma_on)
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
				  "[BTCoex], ******** TDMA(on, %d) *********\n",
				  coex_dm->cur_ps_tdma);
		else
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
				  "[BTCoex], ******** TDMA(off, %d) ********\n",
				  coex_dm->cur_ps_tdma);


		if ((coex_dm->pre_ps_tdma_on == coex_dm->cur_ps_tdma_on) &&
		    (coex_dm->pre_ps_tdma == coex_dm->cur_ps_tdma))
			return;
	}
	if (turn_on) {
		switch (type) {
		default:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x1a,
						       0x1a, 0x0, 0x50);
			break;
		case 1:
			if (wifi_busy)
				halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x51,
							       0x3a, 0x03,
							       0x10, 0x50);
			else
				halbtc8723b1ant_set_fw_ps_tdma(btcoexist,0x51,
							       0x3a, 0x03,
							       0x10, 0x51);

			rssi_adjust_val = 11;
			break;
		case 2:
			if (wifi_busy)
				halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x51,
							       0x2b, 0x03,
							       0x10, 0x50);
			else
				halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x51,
							       0x2b, 0x03,
							       0x10, 0x51);
			rssi_adjust_val = 14;
			break;
		case 3:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x1d,
						       0x1d, 0x0, 0x52);
			break;
		case 4:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x93, 0x15,
						       0x3, 0x14, 0x0);
			rssi_adjust_val = 17;
			break;
		case 5:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x61, 0x15,
						       0x3, 0x11, 0x10);
			break;
		case 6:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x61, 0x20,
						       0x3, 0x11, 0x13);
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
			if(wifi_busy)
				halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x51,
							       0x21, 0x3,
							       0x10, 0x50);
			else
				halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x51,
							       0x21, 0x3,
							       0x10, 0x50);
			rssi_adjust_val = 18;
			break;
		case 10:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x13, 0xa,
						       0xa, 0x0, 0x40);
			break;
		case 11:
			if (wifi_busy)
				halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x51,
							       0x15, 0x03,
							       0x10, 0x50);
			else
				halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x51,
							       0x15, 0x03,
							       0x10, 0x50);
			rssi_adjust_val = 20;
			break;
		case 12:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x0a,
						       0x0a, 0x0, 0x50);
			break;
		case 13:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x15,
						       0x15, 0x0, 0x50);
			break;
		case 14:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x21,
						       0x3, 0x10, 0x52);
			break;
		case 15:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x13, 0xa,
						       0x3, 0x8, 0x0);
			break;
		case 16:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x93, 0x15,
						       0x3, 0x10, 0x0);
			rssi_adjust_val = 18;
			break;
		case 18:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x93, 0x25,
						       0x3, 0x10, 0x0);
			rssi_adjust_val = 14;
			break;
		case 20:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x61, 0x35,
						       0x03, 0x11, 0x10);
			break;
		case 21:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x61, 0x15,
						       0x03, 0x11, 0x10);
			break;
		case 22:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x61, 0x25,
						       0x03, 0x11, 0x10);
			break;
		case 23:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x25,
						       0x3, 0x31, 0x18);
			rssi_adjust_val = 22;
			break;
		case 24:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x15,
						       0x3, 0x31, 0x18);
			rssi_adjust_val = 22;
			break;
		case 25:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0xe3, 0xa,
						       0x3, 0x31, 0x18);
			rssi_adjust_val = 22;
			break;
		case 26:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0xe3, 0xa,
						       0x3, 0x31, 0x18);
			rssi_adjust_val = 22;
			break;
		case 27:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0xe3, 0x25,
						       0x3, 0x31, 0x98);
			rssi_adjust_val = 22;
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
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x51, 0x14,
						       0x3, 0x10, 0x50);
			break;
		case 31:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0xd3, 0x1a,
						       0x1a, 0, 0x58);
			break;
		case 32:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x61, 0xa,
						       0x3, 0x10, 0x0);
			break;
		case 33:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0xa3, 0x25,
						       0x3, 0x30, 0x90);
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
		/* SoftAP only with no sta associated,BT disable ,
		 * TDMA mode for power saving
		 * here softap mode screen off will cost 70-80mA for phone */
		case 40:
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x23, 0x18,
						       0x00, 0x10, 0x24);
			break;
		}
	} else {
		switch (type) {
		case 8: /*PTA Control */
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x8, 0x0,
						       0x0, 0x0, 0x0);
			halbtc8723b1ant_SetAntPath(btcoexist, BTC_ANT_PATH_PTA,
						   false, false);
			break;
		case 0:
		default:  /*Software control, Antenna at BT side */
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x0, 0x0,
						       0x0, 0x0, 0x0);
			halbtc8723b1ant_SetAntPath(btcoexist, BTC_ANT_PATH_BT,
						   false, false);
			break;
		case 9:   /*Software control, Antenna at WiFi side */
			halbtc8723b1ant_set_fw_ps_tdma(btcoexist, 0x0, 0x0,
						       0x0, 0x0, 0x0);
			halbtc8723b1ant_SetAntPath(btcoexist, BTC_ANT_PATH_WIFI,
						   false, false);
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

void halbtc8723b1ant_coex_alloff(struct btc_coexist *btcoexist)
{
	/* fw all off */
	halbtc8723b1ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
	halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	/* sw all off */
	halbtc8723b1ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8723b1ant_sw_mechanism2(btcoexist, false, false, false, 0x18);


	/* hw all off */
	halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
}

bool halbtc8723b1ant_is_common_action(struct btc_coexist *btcoexist)
{
	bool commom = false, wifi_connected = false;
	bool wifi_busy = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	if (!wifi_connected &&
	    BT_8723B_1ANT_BT_STATUS_NON_CONNECTED_IDLE == coex_dm->bt_status) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], Wifi non connected-idle + "
			  "BT non connected-idle!!\n");
		halbtc8723b1ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

 		halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
					      false, false);
		halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
					      false, 0x18);

		commom = true;
	} else if (wifi_connected &&
		   (BT_8723B_1ANT_BT_STATUS_NON_CONNECTED_IDLE ==
		    coex_dm->bt_status)) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], Wifi connected + "
			  "BT non connected-idle!!\n");
		halbtc8723b1ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

      		halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
					      false, false);
		halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
					      false, 0x18);

		commom = true;
	} else if (!wifi_connected &&
		   (BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE ==
		    coex_dm->bt_status)) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], Wifi non connected-idle + "
			  "BT connected-idle!!\n");
		halbtc8723b1ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

		halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
					      false, false);
		halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
					      false, 0x18);

		commom = true;
	} else if (wifi_connected &&
		   (BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE ==
		    coex_dm->bt_status)) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], Wifi connected + BT connected-idle!!\n");
		halbtc8723b1ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

		halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
					      false, false);
		halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
					      false, 0x18);

		commom = true;
	} else if (!wifi_connected &&
		   (BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE !=
		    coex_dm->bt_status)) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			("[BTCoex], Wifi non connected-idle + BT Busy!!\n"));
		halbtc8723b1ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

		halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
					      false, false);
		halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
					      false, 0x18);

		commom = true;
	} else {
		if (wifi_busy)
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Wifi Connected-Busy"
				  " + BT Busy!!\n");
		else
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Wifi Connected-Idle"
				  " + BT Busy!!\n");

		commom = false;
	}

	return commom;
}


void halbtc8723b1ant_tdma_duration_adjust_for_acl(struct btc_coexist *btcoexist,
						  u8 wifi_status)
{
	static s32 up, dn, m, n, wait_count;
	/* 0: no change, +1: increase WiFi duration,
	 * -1: decrease WiFi duration */
	s32 result;
	u8 retry_count = 0, bt_info_ext;
	static bool pre_wifi_busy = false;
	bool wifi_busy = false;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
		  "[BTCoex], TdmaDurationAdjustForAcl()\n");

	if (BT_8723B_1ANT_WIFI_STATUS_CONNECTED_BUSY == wifi_status)
		wifi_busy = true;
	else
		wifi_busy = false;

	if ((BT_8723B_1ANT_WIFI_STATUS_NON_CONNECTED_ASSO_AUTH_SCAN ==
							 wifi_status) ||
	    (BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SCAN == wifi_status) ||
	    (BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SPECIAL_PKT == wifi_status)) {
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
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
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
		/*acquire the BT TRx retry count from BT_Info byte2 */
		retry_count = coex_sta->bt_retry_cnt;
		bt_info_ext = coex_sta->bt_info_ext;
		result = 0;
		wait_count++;
		/* no retry in the last 2-second duration */
		if (retry_count == 0) {
			up++;
			dn--;

			if (dn <= 0)
				dn = 0;

			if (up >= n) {
				wait_count = 0;
				n = 3;
				up = 0;
				dn = 0;
				result = 1;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_TRACE_FW_DETAIL,
					  "[BTCoex], Increase wifi "
					  "duration!!\n");
			}
		} else if (retry_count <= 3) {
			up--;
			dn++;

			if (up <= 0)
				up = 0;

			if (dn == 2) {
				if (wait_count <= 2)
					m++;
				else
					m = 1;

				if (m >= 20)
					m = 20;

				n = 3 * m;
				up = 0;
				dn = 0;
				wait_count = 0;
				result = -1;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_TRACE_FW_DETAIL,
					  "[BTCoex], Decrease wifi duration"
					  " for retryCounter<3!!\n");
			}
		} else {
			if (wait_count == 1)
				m++;
			else
				m = 1;

			if (m >= 20)
				m = 20;

			n = 3 * m;
			up = 0;
			dn = 0;
			wait_count = 0;
			result = -1;
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
				  "[BTCoex], Decrease wifi duration"
				  " for retryCounter>3!!\n");
		}

		if (result == -1) {
			if ((BT_INFO_8723B_1ANT_A2DP_BASIC_RATE(bt_info_ext)) &&
			    ((coex_dm->cur_ps_tdma == 1) ||
			     (coex_dm->cur_ps_tdma == 2))) {
				halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 9);
				coex_dm->ps_tdma_du_adj_type = 9;
			} else if (coex_dm->cur_ps_tdma == 1) {
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
		} else if(result == 1) {
			if ((BT_INFO_8723B_1ANT_A2DP_BASIC_RATE(bt_info_ext)) &&
			    ((coex_dm->cur_ps_tdma == 1) ||
			     (coex_dm->cur_ps_tdma == 2))) {
				halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
							true, 9);
				coex_dm->ps_tdma_du_adj_type = 9;
			} else if (coex_dm->cur_ps_tdma == 11) {
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
		} else {	  /*no change */
			/*if busy / idle change */
			if (wifi_busy != pre_wifi_busy) {
				pre_wifi_busy = wifi_busy;
				halbtc8723b1ant_ps_tdma(btcoexist, FORCE_EXEC,
						        true,
						        coex_dm->cur_ps_tdma);
			}

			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
				  "[BTCoex],********* TDMA(on, %d) ********\n",
				  coex_dm->cur_ps_tdma);
		}

		if (coex_dm->cur_ps_tdma != 1 && coex_dm->cur_ps_tdma != 2 &&
		    coex_dm->cur_ps_tdma != 9 && coex_dm->cur_ps_tdma != 11) {
			/* recover to previous adjust type */
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true,
						coex_dm->ps_tdma_du_adj_type);
		}
	}
}

u8 halbtc8723b1ant_ps_tdma_type_by_wifi_rssi(s32 wifi_rssi, s32 pre_wifi_rssi,
					     u8 wifi_rssi_thresh)
{
	u8 ps_tdma_type=0;

	if (wifi_rssi > pre_wifi_rssi) {
		if (wifi_rssi > (wifi_rssi_thresh + 5))
			ps_tdma_type = 26;
		else
			ps_tdma_type = 25;
	} else  {
		if (wifi_rssi > wifi_rssi_thresh)
 			ps_tdma_type = 26;
 		else
 			ps_tdma_type = 25;
 	}

	return ps_tdma_type;
}

void halbtc8723b1ant_PsTdmaCheckForPowerSaveState(struct btc_coexist *btcoexist,
						  bool new_ps_state)
{
	u8 lps_mode = 0x0;

	btcoexist->btc_get(btcoexist, BTC_GET_U1_LPS_MODE, &lps_mode);

	if (lps_mode) {	/* already under LPS state */
		if (new_ps_state) {
			/* keep state under LPS, do nothing. */
		} else {
			/* will leave LPS state, turn off psTdma first */
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						false, 0);
		}
	} else {	/* NO PS state */
		if (new_ps_state) {
			/* will enter LPS state, turn off psTdma first */
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						false, 0);
		} else {
			/* keep state under NO PS state, do nothing. */
		}
	}
}

void halbtc8723b1ant_power_save_state(struct btc_coexist *btcoexist,
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
		break;
	case BTC_PS_LPS_ON:
		halbtc8723b1ant_PsTdmaCheckForPowerSaveState(btcoexist, true);
		halbtc8723b1ant_LpsRpwm(btcoexist, NORMAL_EXEC, lps_val,
					rpwm_val);
		/* when coex force to enter LPS, do not enter 32k low power. */
		low_pwr_disable = true;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);
		/* power save must executed before psTdma.	 */
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_ENTER_LPS, NULL);
		break;
	case BTC_PS_LPS_OFF:
		halbtc8723b1ant_PsTdmaCheckForPowerSaveState(btcoexist, false);
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS, NULL);
		break;
	default:
		break;
	}
}

void halbtc8723b1ant_action_wifi_only(struct btc_coexist *btcoexist)
{
	halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
	halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 9);
}

void halbtc8723b1ant_monitor_bt_enable_disable(struct btc_coexist *btcoexist)
{
	static bool pre_bt_disabled = false;
	static u32 bt_disable_cnt = 0;
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
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR,
			  "[BTCoex], BT is enabled !!\n");
	} else {
		bt_disable_cnt++;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR,
			  "[BTCoex], bt all counters=0, %d times!!\n",
			  bt_disable_cnt);
		if (bt_disable_cnt >= 2) {
			bt_disabled = true;
			btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_DISABLE,
					   &bt_disabled);
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR,
				  "[BTCoex], BT is disabled !!\n");
			halbtc8723b1ant_action_wifi_only(btcoexist);
		}
	}
	if (pre_bt_disabled != bt_disabled) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR,
			  "[BTCoex], BT is from %s to %s!!\n",
			  (pre_bt_disabled ? "disabled" : "enabled"),
			  (bt_disabled ? "disabled" : "enabled"));
		pre_bt_disabled = bt_disabled;
		if (!bt_disabled) {
		} else {
			btcoexist->btc_set(btcoexist, BTC_SET_ACT_LEAVE_LPS,
					   NULL);
			btcoexist->btc_set(btcoexist, BTC_SET_ACT_NORMAL_LPS,
					   NULL);
		}
	}
}

/***************************************************
 *
 *	Software Coex Mechanism start
 *
 ***************************************************/
/* SCO only or SCO+PAN(HS) */
void halbtc8723b1ant_action_sco(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state =
		halbtc8723b1ant_wifi_rssi_state(btcoexist, 0, 2, 25, 0);

	halbtc8723b1ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 4);

	if (halbtc8723b1ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);
	else
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, true,
			  			      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
			  			      false, 0x18);
		} else {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, true,
			  			      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
			  			      false, 0x18);
		}
	} else {
		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		} else {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	}
}


void halbtc8723b1ant_action_hid(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = halbtc8723b1ant_wifi_rssi_state(btcoexist,
							  0, 2, 25, 0);
	bt_rssi_state = halbtc8723b1ant_bt_rssi_state(2, 50, 0);

	halbtc8723b1ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (halbtc8723b1ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);
	else
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist,
		BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		} else {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	} else {
		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		} else {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	}
}

/*A2DP only / PAN(EDR) only/ A2DP+PAN(HS) */
void halbtc8723b1ant_action_a2dp(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = halbtc8723b1ant_wifi_rssi_state(btcoexist,
							  0, 2, 25, 0);
	bt_rssi_state = halbtc8723b1ant_bt_rssi_state(2, 50, 0);

	halbtc8723b1ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (halbtc8723b1ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);
	else
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		} else {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	} else {
		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		} else {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	}
}

void halbtc8723b1ant_action_a2dp_pan_hs(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state, bt_info_ext;
	u32 wifi_bw;

	bt_info_ext = coex_sta->bt_info_ext;
	wifi_rssi_state = halbtc8723b1ant_wifi_rssi_state(btcoexist,
							  0, 2, 25, 0);
	bt_rssi_state = halbtc8723b1ant_bt_rssi_state(2, 50, 0);

	halbtc8723b1ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (halbtc8723b1ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);
	else
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
 			halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		} else {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	} else {
		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		} else {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	}
}

void halbtc8723b1ant_action_pan_edr(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = halbtc8723b1ant_wifi_rssi_state(btcoexist,
							  0, 2, 25, 0);
	bt_rssi_state = halbtc8723b1ant_bt_rssi_state(2, 50, 0);

	halbtc8723b1ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (halbtc8723b1ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);
	else
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		} else {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	} else {
		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		} else {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	}
}


/* PAN(HS) only */
void halbtc8723b1ant_action_pan_hs(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = halbtc8723b1ant_wifi_rssi_state(btcoexist,
							  0, 2, 25, 0);
	bt_rssi_state = halbtc8723b1ant_bt_rssi_state(2, 50, 0);

	halbtc8723b1ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/* fw mechanism */
		if((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		   (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC,
						   false);
		else
			halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC,
						   false);

		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		} else {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	} else {
		/* fw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC,
						   false);
		else
			halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC,
						   false);

		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		} else {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	}
}

/*PAN(EDR)+A2DP */
void halbtc8723b1ant_action_pan_edr_a2dp(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state, bt_info_ext;
	u32 wifi_bw;

	bt_info_ext = coex_sta->bt_info_ext;
	wifi_rssi_state = halbtc8723b1ant_wifi_rssi_state(btcoexist,
							  0, 2, 25, 0);
	bt_rssi_state = halbtc8723b1ant_bt_rssi_state(2, 50, 0);

	halbtc8723b1ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (halbtc8723b1ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);
	else
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist,
		BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		} else {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	} else {
		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		} else {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, false,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	}
}

void halbtc8723b1ant_action_pan_edr_hid(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = halbtc8723b1ant_wifi_rssi_state(btcoexist,
							  0, 2, 25, 0);
	bt_rssi_state = halbtc8723b1ant_bt_rssi_state(2, 50, 0);

	halbtc8723b1ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (halbtc8723b1ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);
	else
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		} else {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	} else {
		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		} else {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	}
}

/* HID+A2DP+PAN(EDR) */
void halbtc8723b1ant_action_hid_a2dp_pan_edr(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state, bt_info_ext;
	u32 wifi_bw;

	bt_info_ext = coex_sta->bt_info_ext;
	wifi_rssi_state = halbtc8723b1ant_wifi_rssi_state(btcoexist,
							  0, 2, 25, 0);
	bt_rssi_state = halbtc8723b1ant_bt_rssi_state(2, 50, 0);

	halbtc8723b1ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (halbtc8723b1ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);
	else
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/* sw mechanism */
		if((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		   (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		} else {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	} else {
		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		} else {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	}
}

void halbtc8723b1ant_action_hid_a2dp(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state, bt_info_ext;
	u32 wifi_bw;

	bt_info_ext = coex_sta->bt_info_ext;
	wifi_rssi_state = halbtc8723b1ant_wifi_rssi_state(btcoexist,
							  0, 2, 25, 0);
	bt_rssi_state = halbtc8723b1ant_bt_rssi_state(2, 50, 0);

	if (halbtc8723b1ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);
	else
		halbtc8723b1ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			(wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		} else {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	} else {
		/* sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		} else {
			halbtc8723b1ant_sw_mechanism1(btcoexist, false, true,
						      false, false);
			halbtc8723b1ant_sw_mechanism2(btcoexist, false, false,
						      false, 0x18);
		}
	}
}

/*****************************************************
 *
 *	Non-Software Coex Mechanism start
 *
 *****************************************************/
void halbtc8723b1ant_action_hs(struct btc_coexist *btcoexist)
{
	halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 5);
	halbtc8723b1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 2);
}

void halbtc8723b1ant_action_bt_inquiry(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool wifi_connected = false, ap_enable = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE,
			   &ap_enable);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);

	if (!wifi_connected) {
		halbtc8723b1ant_power_save_state(btcoexist,
						 BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 5);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
	} else if (bt_link_info->sco_exist || bt_link_info->hid_only) {
		/* SCO/HID-only busy */
		halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 32);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
	} else {
		if (ap_enable)
			halbtc8723b1ant_power_save_state(btcoexist,
							 BTC_PS_WIFI_NATIVE,
							 0x0, 0x0);
		else
			halbtc8723b1ant_power_save_state(btcoexist,
							 BTC_PS_LPS_ON,
							 0x50, 0x4);

		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 30);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
	}
}

void halbtc8723b1ant_action_bt_sco_hid_only_busy(struct btc_coexist * btcoexist,
						 u8 wifi_status)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool wifi_connected = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);

	/* tdma and coex table */

	if (bt_link_info->sco_exist) {
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 5);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 2);
	} else { /* HID */
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 6);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 5);
	}
}

void halbtc8723b1ant_action_wifi_connected_bt_acl_busy(
					struct btc_coexist *btcoexist,
					u8 wifi_status)
{
	u8 bt_rssi_state;

	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bt_rssi_state = halbtc8723b1ant_bt_rssi_state(2, 28, 0);

	if (bt_link_info->hid_only) {  /*HID */
		halbtc8723b1ant_action_bt_sco_hid_only_busy(btcoexist,
							    wifi_status);
		coex_dm->auto_tdma_adjust = false;
		return;
	} else if (bt_link_info->a2dp_only) { /*A2DP */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
 			 halbtc8723b1ant_tdma_duration_adjust_for_acl(btcoexist,
			 					   wifi_status);
		} else { /*for low BT RSSI */
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 11);
			coex_dm->auto_tdma_adjust = false;
		}

		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
	} else if (bt_link_info->hid_exist &&
			bt_link_info->a2dp_exist) { /*HID+A2DP */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 14);
			coex_dm->auto_tdma_adjust = false;
		} else { /*for low BT RSSI*/
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 14);
			coex_dm->auto_tdma_adjust = false;
		}

		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 6);
	 /*PAN(OPP,FTP), HID+PAN(OPP,FTP) */
	} else if (bt_link_info->pan_only ||
		   (bt_link_info->hid_exist && bt_link_info->pan_exist)) {
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 3);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 6);
		coex_dm->auto_tdma_adjust = false;
	 /*A2DP+PAN(OPP,FTP), HID+A2DP+PAN(OPP,FTP)*/
	} else if ((bt_link_info->a2dp_exist && bt_link_info->pan_exist) ||
		   (bt_link_info->hid_exist && bt_link_info->a2dp_exist &&
		    bt_link_info->pan_exist)) {
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 13);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
		coex_dm->auto_tdma_adjust = false;
	} else {
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 11);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
		coex_dm->auto_tdma_adjust = false;
	}
}

void halbtc8723b1ant_action_wifi_not_connected(struct btc_coexist *btcoexist)
{
	/* power save state */
	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					 0x0, 0x0);

	/* tdma and coex table */
	halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
	halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 0);
}

void halbtc8723b1ant_action_wifi_not_connected_asso_auth_scan(
						struct btc_coexist *btcoexist)
{
	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					 0x0, 0x0);

	halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 22);
	halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
}

void halbtc8723b1ant_ActionWifiConnectedScan(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					 0x0, 0x0);

	/* tdma and coex table */
	if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) {
		if (bt_link_info->a2dp_exist &&
		    bt_link_info->pan_exist) {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 22);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 1);
	 	} else {
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 20);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
		}
	} else if ((BT_8723B_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
		   (BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY ==
		    coex_dm->bt_status)) {
		halbtc8723b1ant_action_bt_sco_hid_only_busy(btcoexist,
				BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SCAN);
	} else {
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 20);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
	}
}

void halbtc8723b1ant_action_wifi_connected_special_packet(
						struct btc_coexist *btcoexist)
{
	bool hs_connecting = false;
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_CONNECTING, &hs_connecting);

	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					 0x0, 0x0);

	/* tdma and coex table */
	if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) {
		if (bt_link_info->a2dp_exist && bt_link_info->pan_exist) {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 22);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 1);
	 	} else {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						true, 20);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 1);
	 	}
	} else {
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, true, 20);
		halbtc8723b1ant_coex_table_with_type(btcoexist, NORMAL_EXEC, 1);
	}
}

void halbtc8723b1ant_action_wifi_connected(struct btc_coexist *btcoexist)
{
	bool wifi_busy = false;
	bool scan = false, link = false, roam = false;
	bool under_4way = false, ap_enable = false;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
		  "[BTCoex], CoexForWifiConnect()===>\n");

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS,
			   &under_4way);
	if (under_4way) {
		halbtc8723b1ant_action_wifi_connected_special_packet(btcoexist);
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], CoexForWifiConnect(), "
			  "return for wifi is under 4way<===\n");
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);

	if (scan || link || roam) {
		halbtc8723b1ant_ActionWifiConnectedScan(btcoexist);
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], CoexForWifiConnect(), "
			  "return for wifi is under scan<===\n");
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE,
			   &ap_enable);
	/* power save state */
	if (!ap_enable &&
	    BT_8723B_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status &&
	    !btcoexist->bt_link_info.hid_only)
		halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_LPS_ON,
						 0x50, 0x4);
	else
		halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);

	/* tdma and coex table */
	btcoexist->btc_get(btcoexist,
		BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	if (!wifi_busy) {
		if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) {
			halbtc8723b1ant_action_wifi_connected_bt_acl_busy(btcoexist,
				      BT_8723B_1ANT_WIFI_STATUS_CONNECTED_IDLE);
		} else if ((BT_8723B_1ANT_BT_STATUS_SCO_BUSY ==
						coex_dm->bt_status) ||
			   (BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY ==
			   			coex_dm->bt_status)) {
			halbtc8723b1ant_action_bt_sco_hid_only_busy(btcoexist,
				     BT_8723B_1ANT_WIFI_STATUS_CONNECTED_IDLE);
		} else {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC,
						false, 8);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 2);
		}
	} else {
		if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) {
			halbtc8723b1ant_action_wifi_connected_bt_acl_busy(btcoexist,
				    BT_8723B_1ANT_WIFI_STATUS_CONNECTED_BUSY);
		} else if ((BT_8723B_1ANT_BT_STATUS_SCO_BUSY ==
						coex_dm->bt_status) ||
			   (BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY ==
			   			coex_dm->bt_status)) {
			halbtc8723b1ant_action_bt_sco_hid_only_busy(btcoexist,
				    BT_8723B_1ANT_WIFI_STATUS_CONNECTED_BUSY);
		} else {
			halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 8);
			halbtc8723b1ant_coex_table_with_type(btcoexist,
							     NORMAL_EXEC, 2);
		}
	}
}

void halbtc8723b1ant_run_sw_coexist_mechanism(struct btc_coexist *btcoexist)
{
	u8 algorithm = 0;

	algorithm = halbtc8723b1ant_action_algorithm(btcoexist);
	coex_dm->cur_algorithm = algorithm;

	if (halbtc8723b1ant_is_common_action(btcoexist)) {
	} else {
		switch (coex_dm->cur_algorithm) {
		case BT_8723B_1ANT_COEX_ALGO_SCO:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action algorithm = SCO.\n");
			halbtc8723b1ant_action_sco(btcoexist);
			break;
		case BT_8723B_1ANT_COEX_ALGO_HID:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action algorithm = HID.\n");
			halbtc8723b1ant_action_hid(btcoexist);
			break;
		case BT_8723B_1ANT_COEX_ALGO_A2DP:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action algorithm = A2DP.\n");
			halbtc8723b1ant_action_a2dp(btcoexist);
			break;
		case BT_8723B_1ANT_COEX_ALGO_A2DP_PANHS:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action algorithm = "
				  "A2DP+PAN(HS).\n");
			halbtc8723b1ant_action_a2dp_pan_hs(btcoexist);
			break;
		case BT_8723B_1ANT_COEX_ALGO_PANEDR:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action algorithm = PAN(EDR).\n");
			halbtc8723b1ant_action_pan_edr(btcoexist);
			break;
		case BT_8723B_1ANT_COEX_ALGO_PANHS:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action algorithm = HS mode.\n");
			halbtc8723b1ant_action_pan_hs(btcoexist);
			break;
		case BT_8723B_1ANT_COEX_ALGO_PANEDR_A2DP:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action algorithm = PAN+A2DP.\n");
			halbtc8723b1ant_action_pan_edr_a2dp(btcoexist);
			break;
		case BT_8723B_1ANT_COEX_ALGO_PANEDR_HID:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action algorithm = "
				  "PAN(EDR)+HID.\n");
			halbtc8723b1ant_action_pan_edr_hid(btcoexist);
			break;
		case BT_8723B_1ANT_COEX_ALGO_HID_A2DP_PANEDR:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action algorithm = "
				  "HID+A2DP+PAN.\n");
			halbtc8723b1ant_action_hid_a2dp_pan_edr(btcoexist);
			break;
		case BT_8723B_1ANT_COEX_ALGO_HID_A2DP:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action algorithm = HID+A2DP.\n");
			halbtc8723b1ant_action_hid_a2dp(btcoexist);
			break;
		default:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action algorithm = "
				  "coexist All Off!!\n");
			break;
		}
		coex_dm->pre_algorithm = coex_dm->cur_algorithm;
	}
}

void halbtc8723b1ant_run_coexist_mechanism(struct btc_coexist *btcoexist)
{
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	bool wifi_connected = false, bt_hs_on = false;
	bool limited_dig = false, bIncreaseScanDevNum = false;
	bool b_bt_ctrl_agg_buf_size = false;
	u8 agg_buf_size = 5;
	u8 wifi_rssi_state = BTC_RSSI_STATE_HIGH;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
		  "[BTCoex], RunCoexistMechanism()===>\n");

	if (btcoexist->manual_control) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], RunCoexistMechanism(), "
			  "return for Manual CTRL <===\n");
		return;
	}

	if (btcoexist->stop_coex_dm) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], RunCoexistMechanism(), "
			  "return for Stop Coex DM <===\n");
		return;
	}

	if (coex_sta->under_ips) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], wifi is under IPS !!!\n");
		return;
	}

	if ((BT_8723B_1ANT_BT_STATUS_ACL_BUSY == coex_dm->bt_status) ||
	    (BT_8723B_1ANT_BT_STATUS_SCO_BUSY == coex_dm->bt_status) ||
	    (BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY == coex_dm->bt_status)) {
		limited_dig = true;
		bIncreaseScanDevNum = true;
	}

	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_LIMITED_DIG, &limited_dig);
	btcoexist->btc_set(btcoexist, BTC_SET_BL_INC_SCAN_DEV_NUM,
			   &bIncreaseScanDevNum);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);

	if (!bt_link_info->sco_exist && !bt_link_info->hid_exist) {
		halbtc8723b1ant_limited_tx(btcoexist, NORMAL_EXEC, 0, 0, 0, 0);
	} else {
		if (wifi_connected) {
			wifi_rssi_state =
				halbtc8723b1ant_wifi_rssi_state(btcoexist,
								1, 2, 30, 0);
			if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
			    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
				halbtc8723b1ant_limited_tx(btcoexist,
							   NORMAL_EXEC,
							   1, 1, 1, 1);
			} else {
				halbtc8723b1ant_limited_tx(btcoexist,
							   NORMAL_EXEC,
							   1, 1, 1, 1);
			}
		} else {
			halbtc8723b1ant_limited_tx(btcoexist, NORMAL_EXEC,
						   0, 0, 0, 0);
		}
	}

	if (bt_link_info->sco_exist) {
		b_bt_ctrl_agg_buf_size = true;
		agg_buf_size = 0x3;
	} else if (bt_link_info->hid_exist) {
		b_bt_ctrl_agg_buf_size = true;
		agg_buf_size = 0x5;
 	} else if (bt_link_info->a2dp_exist || bt_link_info->pan_exist) {
		b_bt_ctrl_agg_buf_size = true;
		agg_buf_size = 0x8;
	}
	halbtc8723b1ant_limited_rx(btcoexist, NORMAL_EXEC, false,
				   b_bt_ctrl_agg_buf_size, agg_buf_size);

	halbtc8723b1ant_run_sw_coexist_mechanism(btcoexist);

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

		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], wifi is non connected-idle !!!\n");

		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);

		if (scan || link || roam)
			halbtc8723b1ant_action_wifi_not_connected_asso_auth_scan(btcoexist);
		else
			halbtc8723b1ant_action_wifi_not_connected(btcoexist);
	} else { /* wifi LPS/Busy */
		halbtc8723b1ant_action_wifi_connected(btcoexist);
	}
}

void halbtc8723b1ant_init_coex_dm(struct btc_coexist *btcoexist)
{
	/* force to reset coex mechanism */
	halbtc8723b1ant_fw_dac_swing_lvl(btcoexist, FORCE_EXEC, 6);
	halbtc8723b1ant_dec_bt_pwr(btcoexist, FORCE_EXEC, false);

	/* sw all off */
	halbtc8723b1ant_sw_mechanism1(btcoexist, false, false, false, false);
	halbtc8723b1ant_sw_mechanism2(btcoexist,false, false, false, 0x18);

	halbtc8723b1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 8);
	halbtc8723b1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 0);
}

void halbtc8723b1ant_init_hw_config(struct btc_coexist *btcoexist, bool backup)
{
	u32 u32tmp = 0;
	u8 u8tmp = 0;
	u32 cnt_bt_cal_chk = 0;

	BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
		  "[BTCoex], 1Ant Init HW Config!!\n");

	if (backup) {/* backup rf 0x1e value */
		coex_dm->bt_rf0x1e_backup =
			btcoexist->btc_get_rf_reg(btcoexist,
						  BTC_RF_A, 0x1e, 0xfffff);

		coex_dm->backup_arfr_cnt1 =
			btcoexist->btc_read_4byte(btcoexist, 0x430);
		coex_dm->backup_arfr_cnt2 =
			btcoexist->btc_read_4byte(btcoexist, 0x434);
		coex_dm->backup_retry_limit =
			btcoexist->btc_read_2byte(btcoexist, 0x42a);
		coex_dm->backup_ampdu_max_time =
			btcoexist->btc_read_1byte(btcoexist, 0x456);
	}

	/* WiFi goto standby while GNT_BT 0-->1 */
	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1, 0xfffff, 0x780);
	/* BT goto standby while GNT_BT 1-->0 */
	btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x2, 0xfffff, 0x500);

	btcoexist->btc_write_1byte(btcoexist, 0x974, 0xff);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x944, 0x3, 0x3);
	btcoexist->btc_write_1byte(btcoexist, 0x930, 0x77);


	/* BT calibration check */
	while (cnt_bt_cal_chk <= 20) {
		u32tmp = btcoexist->btc_read_4byte(btcoexist, 0x49d);
		cnt_bt_cal_chk++;
		if (u32tmp & BIT0) {
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
				  "[BTCoex], ########### BT "
				  "calibration(cnt=%d) ###########\n",
				  cnt_bt_cal_chk);
			mdelay(50);
		} else {
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
				  "[BTCoex], ********** BT NOT "
				  "calibration (cnt=%d)**********\n",
				  cnt_bt_cal_chk);
			break;
		}
	}

	/* 0x790[5:0]=0x5 */
	u8tmp = btcoexist->btc_read_1byte(btcoexist, 0x790);
	u8tmp &= 0xc0;
	u8tmp |= 0x5;
	btcoexist->btc_write_1byte(btcoexist, 0x790, u8tmp);

	/* Enable counter statistics */
	/*0x76e[3] =1, WLAN_Act control by PTA */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0xc);
	btcoexist->btc_write_1byte(btcoexist, 0x778, 0x1);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x40, 0x20, 0x1);

	/*Antenna config */
	halbtc8723b1ant_SetAntPath(btcoexist, BTC_ANT_PATH_PTA, true, false);
	/* PTA parameter */
	halbtc8723b1ant_coex_table_with_type(btcoexist, FORCE_EXEC, 0);

}

void halbtc8723b1ant_wifi_off_hw_cfg(struct btc_coexist *btcoexist)
{
	/* set wlan_act to low */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0);
}

/**************************************************************
 * work around function start with wa_halbtc8723b1ant_
 **************************************************************/
/**************************************************************
 * extern function start with EXhalbtc8723b1ant_
 **************************************************************/

void ex_halbtc8723b1ant_init_hwconfig(struct btc_coexist *btcoexist)
{
	halbtc8723b1ant_init_hw_config(btcoexist, true);
}

void ex_halbtc8723b1ant_init_coex_dm(struct btc_coexist *btcoexist)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
		  "[BTCoex], Coex Mechanism Init!!\n");

	btcoexist->stop_coex_dm = false;

	halbtc8723b1ant_init_coex_dm(btcoexist);

	halbtc8723b1ant_query_bt_info(btcoexist);
}

void ex_halbtc8723b1ant_display_coex_info(struct btc_coexist *btcoexist)
{
	struct btc_board_info *board_info = &btcoexist->board_info;
	struct btc_stack_info *stack_info = &btcoexist->stack_info;
	struct btc_bt_link_info *bt_link_info = &btcoexist->bt_link_info;
	u8 *cli_buf = btcoexist->cli_buf;
	u8 u8tmp[4], i, bt_info_ext, psTdmaCase=0;
	u16 u16tmp[4];
	u32 u32tmp[4];
	bool roam = false, scan = false;
	bool link = false, wifi_under_5g = false;
	bool bt_hs_on = false, wifi_busy = false;
	s32 wifi_rssi =0, bt_hs_rssi = 0;
	u32 wifi_bw, wifi_traffic_dir, fa_ofdm, fa_cck;
	u8 wifi_dot11_chnl, wifi_hs_chnl;
	u32 fw_ver = 0, bt_patch_ver = 0;

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n ============[BT Coexist info]============");
	CL_PRINTF(cli_buf);

	if (btcoexist->manual_control) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n ============[Under Manual Control]==========");
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

	if (!board_info->bt_exist) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n BT not exists !!!");
		CL_PRINTF(cli_buf);
		return;
	}

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d",
		   "Ant PG Num/ Ant Mech/ Ant Pos:", \
		   board_info->pg_ant_num, board_info->btdm_ant_num,
		   board_info->btdm_ant_pos);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s / %d",
		   "BT stack/ hci ext ver", \
		   ((stack_info->profile_notified)? "Yes":"No"),
		   stack_info->hci_version);
	CL_PRINTF(cli_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER, &bt_patch_ver);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %d_%x/ 0x%x/ 0x%x(%d)",
		   "CoexVer/ FwVer/ PatchVer", \
		   glcoex_ver_date_8723b_1ant, glcoex_ver_8723b_1ant,
		   fw_ver, bt_patch_ver, bt_patch_ver);
	CL_PRINTF(cli_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_DOT11_CHNL,
			   &wifi_dot11_chnl);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_HS_CHNL, &wifi_hs_chnl);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d(%d)",
		   "Dot11 channel / HsChnl(HsMode)", \
		   wifi_dot11_chnl, wifi_hs_chnl, bt_hs_on);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x ",
		   "H2C Wifi inform bt chnl Info", \
		   coex_dm->wifi_chnl_info[0], coex_dm->wifi_chnl_info[1],
		   coex_dm->wifi_chnl_info[2]);
	CL_PRINTF(cli_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_S4_WIFI_RSSI, &wifi_rssi);
	btcoexist->btc_get(btcoexist, BTC_GET_S4_HS_RSSI, &bt_hs_rssi);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "Wifi rssi/ HS rssi", wifi_rssi, bt_hs_rssi);
	CL_PRINTF(cli_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d ",
		"Wifi link/ roam/ scan", link, roam, scan);
	CL_PRINTF(cli_buf);

	btcoexist->btc_get(btcoexist,BTC_GET_BL_WIFI_UNDER_5G,
			   &wifi_under_5g);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION,
			   &wifi_traffic_dir);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s / %s/ %s ",
		   "Wifi status", (wifi_under_5g? "5G":"2.4G"),
		   ((BTC_WIFI_BW_LEGACY==wifi_bw)? "Legacy":
			(((BTC_WIFI_BW_HT40==wifi_bw)? "HT40":"HT20"))),
		   ((!wifi_busy)? "idle":
			((BTC_WIFI_TRAFFIC_TX==wifi_traffic_dir)?
				"uplink":"downlink")));
	CL_PRINTF(cli_buf);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = [%s/ %d/ %d] ",
		"BT [status/ rssi/ retryCnt]",
		((btcoexist->bt_info.bt_disabled)? ("disabled"):
		  ((coex_sta->c2h_bt_inquiry_page)?("inquiry/page scan"):
		    ((BT_8723B_1ANT_BT_STATUS_NON_CONNECTED_IDLE == coex_dm->bt_status)?
		      "non-connected idle":
			((BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE == coex_dm->bt_status)?
			  "connected-idle":"busy")))),
			    coex_sta->bt_rssi, coex_sta->bt_retry_cnt);
	CL_PRINTF(cli_buf);


	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d / %d",
		"SCO/HID/PAN/A2DP", bt_link_info->sco_exist,
		bt_link_info->hid_exist, bt_link_info->pan_exist,
		bt_link_info->a2dp_exist);
	CL_PRINTF(cli_buf);
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_BT_LINK_INFO);

	bt_info_ext = coex_sta->bt_info_ext;
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s",
		   "BT Info A2DP rate",
		   (bt_info_ext & BIT0) ? "Basic rate" : "EDR rate");
	CL_PRINTF(cli_buf);

	for (i = 0; i < BT_INFO_SRC_8723B_1ANT_MAX; i++) {
		if (coex_sta->bt_info_c2h_cnt[i]) {
			CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
				   "\r\n %-35s = %02x %02x %02x "
				   "%02x %02x %02x %02x(%d)",
				   GLBtInfoSrc8723b1Ant[i],
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
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\r\n %-35s = %s/%s, (0x%x/0x%x)",
		   "PS state, IPS/LPS, (lps/rpwm)", \
		   ((coex_sta->under_ips? "IPS ON":"IPS OFF")),
		   ((coex_sta->under_lps? "LPS ON":"LPS OFF")),
		   btcoexist->bt_info.lps_1ant,
		   btcoexist->bt_info.rpwm_1ant);
	CL_PRINTF(cli_buf);
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_FW_PWR_MODE_CMD);

	if (!btcoexist->manual_control) {
		/* Sw mechanism	*/
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
			   "============[Sw mechanism]============");
		CL_PRINTF(cli_buf);

		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d ",
 			   "SM1[ShRf/ LpRA/ LimDig]", \
 			   coex_dm->cur_rf_rx_lpf_shrink,
			   coex_dm->cur_low_penalty_ra,
			   btcoexist->bt_info.limited_dig);
		CL_PRINTF(cli_buf);
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %d/ %d/ %d(0x%x) ",
			   "SM2[AgcT/ AdcB/ SwDacSwing(lvl)]", \
			   coex_dm->cur_agc_table_en,
			   coex_dm->cur_adc_backoff,
			   coex_dm->cur_dac_swing_on,
			   coex_dm->cur_dac_swing_lvl);
		CL_PRINTF(cli_buf);


		CL_PRINTF(cli_buf);
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x ",
			   "Rate Mask", btcoexist->bt_info.ra_mask);
		CL_PRINTF(cli_buf);

		/* Fw mechanism	*/
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
			   "============[Fw mechanism]============");
		CL_PRINTF(cli_buf);

		psTdmaCase = coex_dm->cur_ps_tdma;
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\r\n %-35s = %02x %02x %02x %02x %02x "
			   "case-%d (auto:%d)",
			   "PS TDMA", coex_dm->ps_tdma_para[0],
			   coex_dm->ps_tdma_para[1], coex_dm->ps_tdma_para[2],
			   coex_dm->ps_tdma_para[3], coex_dm->ps_tdma_para[4],
			   psTdmaCase, coex_dm->auto_tdma_adjust);
		CL_PRINTF(cli_buf);

		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x ",
			   "Latest error condition(should be 0)", \
			   coex_dm->error_condition);
		CL_PRINTF(cli_buf);

		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d ",
			   "DecBtPwr/ IgnWlanAct", coex_dm->cur_dec_bt_pwr,
			   coex_dm->cur_ignore_wlan_act);
		CL_PRINTF(cli_buf);
	}

	/* Hw setting */
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s",
		   "============[Hw setting]============");
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x",
		   "RF-A, 0x1e initVal", coex_dm->bt_rf0x1e_backup);
	CL_PRINTF(cli_buf);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/0x%x/0x%x/0x%x",
		   "backup ARFR1/ARFR2/RL/AMaxTime", coex_dm->backup_arfr_cnt1,
		   coex_dm->backup_arfr_cnt2, coex_dm->backup_retry_limit,
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
		   "0x778/0x6cc/0x880[29:25]", u8tmp[0], u32tmp[0],
		   (u32tmp[1] & 0x3e000000) >> 25);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x948);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x67);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x765);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x948/ 0x67[5] / 0x765",
		   u32tmp[0], ((u8tmp[0] & 0x20)>> 5), u8tmp[1]);
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
		   ((u8tmp[0] & 0x8)>>3), u8tmp[1],
		   ((u32tmp[0] & 0x01800000) >> 23), u8tmp[2] & 0x1);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x550);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x522);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "0x550(bcn ctrl)/0x522", u32tmp[0], u8tmp[0]);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xc50);
	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x49c);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x",
		   "0xc50(dig)/0x49c(null-drop)", u32tmp[0] & 0xff, u8tmp[0]);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xda0);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0xda4);
	u32tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0xda8);
	u32tmp[3] = btcoexist->btc_read_4byte(btcoexist, 0xcf0);

	u8tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0xa5b);
	u8tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0xa5c);

	fa_ofdm = ((u32tmp[0] & 0xffff0000) >> 16) +
		  ((u32tmp[1] & 0xffff0000) >> 16) +
		   (u32tmp[1] & 0xffff) +
		   (u32tmp[2] & 0xffff) + \
		  ((u32tmp[3] & 0xffff0000) >> 16) +
		   (u32tmp[3] & 0xffff) ;
	fa_cck = (u8tmp[0] << 8) + u8tmp[1];

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "OFDM-CCA/OFDM-FA/CCK-FA",
		   u32tmp[0] & 0xffff, fa_ofdm, fa_cck);
	CL_PRINTF(cli_buf);

	u32tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x6c0);
	u32tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x6c4);
	u32tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0x6c8);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x6c0/0x6c4/0x6c8(coexTable)",
		   u32tmp[0], u32tmp[1], u32tmp[2]);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "0x770(high-pri rx/tx)", coex_sta->high_priority_rx,
		   coex_sta->high_priority_tx);
	CL_PRINTF(cli_buf);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d",
		   "0x774(low-pri rx/tx)", coex_sta->low_priority_rx,
		   coex_sta->low_priority_tx);
	CL_PRINTF(cli_buf);
#if(BT_AUTO_REPORT_ONLY_8723B_1ANT == 1)
	halbtc8723b1ant_monitor_bt_ctr(btcoexist);
#endif
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_COEX_STATISTICS);
}


void ex_halbtc8723b1ant_ips_notify(struct btc_coexist *btcoexist, u8 type)
{

	if (btcoexist->manual_control || btcoexist->stop_coex_dm)
		return;

	if (BTC_IPS_ENTER == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], IPS ENTER notify\n");
		coex_sta->under_ips = true;

		halbtc8723b1ant_SetAntPath(btcoexist, BTC_ANT_PATH_BT,
					   false, true);
                /* set PTA control */
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 0);
		halbtc8723b1ant_coex_table_with_type(btcoexist,
			NORMAL_EXEC, 0);
	} else if (BTC_IPS_LEAVE == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], IPS LEAVE notify\n");
		coex_sta->under_ips = false;

		halbtc8723b1ant_run_coexist_mechanism(btcoexist);
	}
}

void ex_halbtc8723b1ant_lps_notify(struct btc_coexist *btcoexist, u8 type)
{
	if (btcoexist->manual_control || btcoexist->stop_coex_dm)
		return;

	if (BTC_LPS_ENABLE == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], LPS ENABLE notify\n");
		coex_sta->under_lps = true;
	} else if (BTC_LPS_DISABLE == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], LPS DISABLE notify\n");
		coex_sta->under_lps = false;
	}
}

void ex_halbtc8723b1ant_scan_notify(struct btc_coexist *btcoexist, u8 type)
{
	bool wifi_connected = false, bt_hs_on = false;

	if (btcoexist->manual_control || btcoexist->stop_coex_dm ||
	    btcoexist->bt_info.bt_disabled)
		return;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);

	halbtc8723b1ant_query_bt_info(btcoexist);

	if (coex_sta->c2h_bt_inquiry_page) {
		halbtc8723b1ant_action_bt_inquiry(btcoexist);
		return;
	} else if (bt_hs_on) {
		halbtc8723b1ant_action_hs(btcoexist);
		return;
	}

	if (BTC_SCAN_START == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], SCAN START notify\n");
		if (!wifi_connected)	/* non-connected scan */
			halbtc8723b1ant_action_wifi_not_connected_asso_auth_scan(btcoexist);
		else	/* wifi is connected */
			halbtc8723b1ant_ActionWifiConnectedScan(btcoexist);
	} else if (BTC_SCAN_FINISH == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], SCAN FINISH notify\n");
		if (!wifi_connected)	/* non-connected scan */
			halbtc8723b1ant_action_wifi_not_connected(btcoexist);
		else
			halbtc8723b1ant_action_wifi_connected(btcoexist);
	}
}

void ex_halbtc8723b1ant_connect_notify(struct btc_coexist *btcoexist, u8 type)
{
	bool wifi_connected = false, bt_hs_on = false;

	if (btcoexist->manual_control || btcoexist->stop_coex_dm ||
	    btcoexist->bt_info.bt_disabled)
		return;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	if (coex_sta->c2h_bt_inquiry_page) {
		halbtc8723b1ant_action_bt_inquiry(btcoexist);
		return;
	} else if (bt_hs_on) {
		halbtc8723b1ant_action_hs(btcoexist);
		return;
	}

	if (BTC_ASSOCIATE_START == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], CONNECT START notify\n");
		halbtc8723b1ant_action_wifi_not_connected_asso_auth_scan(btcoexist);
	} else if (BTC_ASSOCIATE_FINISH == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], CONNECT FINISH notify\n");

		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
				   &wifi_connected);
		if (!wifi_connected) /* non-connected scan */
			halbtc8723b1ant_action_wifi_not_connected(btcoexist);
		else
			halbtc8723b1ant_action_wifi_connected(btcoexist);
	}
}

void ex_halbtc8723b1ant_media_status_notify(struct btc_coexist *btcoexist,
					    u8 type)
{
	u8 h2c_parameter[3] ={0};
	u32 wifi_bw;
	u8 wifiCentralChnl;

	if (btcoexist->manual_control || btcoexist->stop_coex_dm ||
	    btcoexist->bt_info.bt_disabled )
		return;

	if (BTC_MEDIA_CONNECT == type)
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], MEDIA connect notify\n");
	else
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], MEDIA disconnect notify\n");

	/* only 2.4G we need to inform bt the chnl mask */
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_CENTRAL_CHNL,
			   &wifiCentralChnl);

	if ((BTC_MEDIA_CONNECT == type) &&
	    (wifiCentralChnl <= 14)) {
		h2c_parameter[0] = 0x0;
		h2c_parameter[1] = wifiCentralChnl;
		btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
		if (BTC_WIFI_BW_HT40 == wifi_bw)
			h2c_parameter[2] = 0x30;
		else
			h2c_parameter[2] = 0x20;
	}

	coex_dm->wifi_chnl_info[0] = h2c_parameter[0];
	coex_dm->wifi_chnl_info[1] = h2c_parameter[1];
	coex_dm->wifi_chnl_info[2] = h2c_parameter[2];

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], FW write 0x66=0x%x\n",
		  h2c_parameter[0] << 16 | h2c_parameter[1] << 8 |
		  h2c_parameter[2]);

	btcoexist->btc_fill_h2c(btcoexist, 0x66, 3, h2c_parameter);
}

void ex_halbtc8723b1ant_special_packet_notify(struct btc_coexist *btcoexist,
					      u8 type)
{
	bool bt_hs_on = false;

	if (btcoexist->manual_control || btcoexist->stop_coex_dm ||
	    btcoexist->bt_info.bt_disabled)
		return;

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
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], special Packet(%d) notify\n", type);
		halbtc8723b1ant_action_wifi_connected_special_packet(btcoexist);
	}
}

void ex_halbtc8723b1ant_bt_info_notify(struct btc_coexist *btcoexist,
				       u8 *tmp_buf, u8 length)
{
	u8 bt_info = 0;
	u8 i, rsp_source = 0;
	bool wifi_connected = false;
	bool bt_busy = false;

	coex_sta->c2h_bt_info_req_sent = false;

	rsp_source = tmp_buf[0] & 0xf;
	if (rsp_source >= BT_INFO_SRC_8723B_1ANT_MAX)
		rsp_source = BT_INFO_SRC_8723B_1ANT_WIFI_FW;
	coex_sta->bt_info_c2h_cnt[rsp_source]++;

	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
		  "[BTCoex], Bt info[%d], length=%d, hex data=[",
		  rsp_source, length);
	for (i=0; i<length; i++) {
		coex_sta->bt_info_c2h[rsp_source][i] = tmp_buf[i];
		if (i == 1)
			bt_info = tmp_buf[i];
		if (i == length - 1)
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
				  "0x%02x]\n", tmp_buf[i]);
		else
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
				  "0x%02x, ", tmp_buf[i]);
	}

	if (BT_INFO_SRC_8723B_1ANT_WIFI_FW != rsp_source) {
		coex_sta->bt_retry_cnt =	/* [3:0] */
			coex_sta->bt_info_c2h[rsp_source][2] & 0xf;

		coex_sta->bt_rssi =
			coex_sta->bt_info_c2h[rsp_source][3] * 2 + 10;

		coex_sta->bt_info_ext =
			coex_sta->bt_info_c2h[rsp_source][4];

		/* Here we need to resend some wifi info to BT
		 * because bt is reset and loss of the info.*/
		if(coex_sta->bt_info_ext & BIT1)
		{
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], BT ext info bit1 check, "
				  "send wifi BW&Chnl to BT!!\n");
			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
					   &wifi_connected);
			if(wifi_connected)
				ex_halbtc8723b1ant_media_status_notify(btcoexist,
							     BTC_MEDIA_CONNECT);
			else
				ex_halbtc8723b1ant_media_status_notify(btcoexist,
							  BTC_MEDIA_DISCONNECT);
		}

		if (coex_sta->bt_info_ext & BIT3) {
			if (!btcoexist->manual_control &&
			    !btcoexist->stop_coex_dm) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
					  "[BTCoex], BT ext info bit3 check, "
					  "set BT NOT ignore Wlan active!!\n");
				halbtc8723b1ant_ignore_wlan_act(btcoexist,
								FORCE_EXEC,
								false);
			}
		} else {
			/* BT already NOT ignore Wlan active, do nothing here.*/
		}
#if(BT_AUTO_REPORT_ONLY_8723B_1ANT == 0)
		if (coex_sta->bt_info_ext & BIT4) {
			/* BT auto report already enabled, do nothing */
		} else {
			halbtc8723b1ant_bt_auto_report(btcoexist, FORCE_EXEC,
						       true);
		}
#endif
	}

	/* check BIT2 first ==> check if bt is under inquiry or page scan */
	if (bt_info & BT_INFO_8723B_1ANT_B_INQ_PAGE)
		coex_sta->c2h_bt_inquiry_page = true;
	else
		coex_sta->c2h_bt_inquiry_page = false;

	/* set link exist status */
	if (!(bt_info & BT_INFO_8723B_1ANT_B_CONNECTION)) {
		coex_sta->bt_link_exist = false;
		coex_sta->pan_exist = false;
		coex_sta->a2dp_exist = false;
		coex_sta->hid_exist = false;
		coex_sta->sco_exist = false;
	} else { /* connection exists */
		coex_sta->bt_link_exist = true;
		if (bt_info & BT_INFO_8723B_1ANT_B_FTP)
			coex_sta->pan_exist = true;
		else
			coex_sta->pan_exist = false;
		if (bt_info & BT_INFO_8723B_1ANT_B_A2DP)
			coex_sta->a2dp_exist = true;
		else
			coex_sta->a2dp_exist = false;
		if (bt_info & BT_INFO_8723B_1ANT_B_HID)
			coex_sta->hid_exist = true;
		else
			coex_sta->hid_exist = false;
		if (bt_info & BT_INFO_8723B_1ANT_B_SCO_ESCO)
			coex_sta->sco_exist = true;
		else
			coex_sta->sco_exist = false;
	}

	halbtc8723b1ant_update_bt_link_info(btcoexist);

	if (!(bt_info&BT_INFO_8723B_1ANT_B_CONNECTION)) {
		coex_dm->bt_status = BT_8723B_1ANT_BT_STATUS_NON_CONNECTED_IDLE;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], BtInfoNotify(), "
			  "BT Non-Connected idle!!!\n");
	/* connection exists but no busy */
	} else if (bt_info == BT_INFO_8723B_1ANT_B_CONNECTION) {
		coex_dm->bt_status = BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], BtInfoNotify(), BT Connected-idle!!!\n");
	} else if ((bt_info & BT_INFO_8723B_1ANT_B_SCO_ESCO) ||
		(bt_info & BT_INFO_8723B_1ANT_B_SCO_BUSY)) {
		coex_dm->bt_status = BT_8723B_1ANT_BT_STATUS_SCO_BUSY;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], BtInfoNotify(), "
			  "BT SCO busy!!!\n");
	} else if (bt_info & BT_INFO_8723B_1ANT_B_ACL_BUSY) {
		if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY != coex_dm->bt_status)
			coex_dm->auto_tdma_adjust = false;

		coex_dm->bt_status = BT_8723B_1ANT_BT_STATUS_ACL_BUSY;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], BtInfoNotify(), BT ACL busy!!!\n");
	} else {
		coex_dm->bt_status =
			BT_8723B_1ANT_BT_STATUS_MAX;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
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

void ex_halbtc8723b1ant_halt_notify(struct btc_coexist *btcoexist)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, "[BTCoex], Halt notify\n");

	btcoexist->stop_coex_dm = true;

	halbtc8723b1ant_SetAntPath(btcoexist, BTC_ANT_PATH_BT, false, true);

	halbtc8723b1ant_wifi_off_hw_cfg(btcoexist);
	halbtc8723b1ant_ignore_wlan_act(btcoexist, FORCE_EXEC, true);

	halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
					 0x0, 0x0);
	halbtc8723b1ant_ps_tdma(btcoexist, FORCE_EXEC, false, 0);

	ex_halbtc8723b1ant_media_status_notify(btcoexist, BTC_MEDIA_DISCONNECT);
}

void ex_halbtc8723b1ant_pnp_notify(struct btc_coexist *btcoexist, u8 pnp_state)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, "[BTCoex], Pnp notify\n");

	if (BTC_WIFI_PNP_SLEEP == pnp_state) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], Pnp notify to SLEEP\n");
		btcoexist->stop_coex_dm = true;
		halbtc8723b1ant_ignore_wlan_act(btcoexist, FORCE_EXEC, true);
		halbtc8723b1ant_power_save_state(btcoexist, BTC_PS_WIFI_NATIVE,
						 0x0, 0x0);
		halbtc8723b1ant_ps_tdma(btcoexist, NORMAL_EXEC, false, 9);
	} else if (BTC_WIFI_PNP_WAKE_UP == pnp_state) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], Pnp notify to WAKE UP\n");
		btcoexist->stop_coex_dm = false;
		halbtc8723b1ant_init_hw_config(btcoexist, false);
		halbtc8723b1ant_init_coex_dm(btcoexist);
		halbtc8723b1ant_query_bt_info(btcoexist);
	}
}

void ex_halbtc8723b1ant_periodical(struct btc_coexist *btcoexist)
{
	struct btc_board_info *board_info = &btcoexist->board_info;
	struct btc_stack_info *stack_info = &btcoexist->stack_info;
	static u8 dis_ver_info_cnt = 0;
	u32 fw_ver = 0, bt_patch_ver = 0;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
		  "[BTCoex], =========================="
		  "Periodical===========================\n");

	if (dis_ver_info_cnt <= 5) {
		dis_ver_info_cnt += 1;
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
			  "[BTCoex], *************************"
			  "***************************************\n");
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
			  "[BTCoex], Ant PG Num/ Ant Mech/ "
			  "Ant Pos = %d/ %d/ %d\n", \
			  board_info->pg_ant_num, board_info->btdm_ant_num,
			  board_info->btdm_ant_pos);
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
			  "[BTCoex], BT stack/ hci ext ver = %s / %d\n", \
			  ((stack_info->profile_notified)? "Yes":"No"),
			  stack_info->hci_version);
		btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER,
				   &bt_patch_ver);
		btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
			  "[BTCoex], CoexVer/ FwVer/ PatchVer "
			  "= %d_%x/ 0x%x/ 0x%x(%d)\n", \
			  glcoex_ver_date_8723b_1ant,
			  glcoex_ver_8723b_1ant, fw_ver,
			  bt_patch_ver, bt_patch_ver);
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
			  "[BTCoex], *****************************"
			  "***********************************\n");
	}

#if(BT_AUTO_REPORT_ONLY_8723B_1ANT == 0)
	halbtc8723b1ant_query_bt_info(btcoexist);
	halbtc8723b1ant_monitor_bt_ctr(btcoexist);
	halbtc8723b1ant_monitor_bt_enable_disable(btcoexist);
#else
	if (halbtc8723b1ant_is_wifi_status_changed(btcoexist) ||
	    coex_dm->auto_tdma_adjust) {
		if (coex_sta->special_pkt_period_cnt > 2)
			halbtc8723b1ant_run_coexist_mechanism(btcoexist);
	}

	coex_sta->special_pkt_period_cnt++;
#endif
}


#endif

