/*  Description: */
/*  This file is for RTL8821A Co-exist mechanism */
/*  History */
/*  2012/08/22 Cosa first check in. */
/*  2012/11/14 Cosa Revise for 8821A 2Ant out sourcing. */

/*  include files */
#include "halbt_precomp.h"
/*  Global variables, these are static variables */
static struct coex_dm_8821a_2ant	glcoex_dm_8821a_2ant;
static struct coex_dm_8821a_2ant	*coex_dm = &glcoex_dm_8821a_2ant;
static struct coex_sta_8821a_2ant	glcoex_sta_8821a_2ant;
static struct coex_sta_8821a_2ant	*coex_sta = &glcoex_sta_8821a_2ant;

static const char *const glbt_info_src_8821a_2ant[] = {
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

static u32 glcoex_ver_date_8821a_2ant = 20130618;
static u32 glcoex_ver_8821a_2ant = 0x5050;

/*  local function proto type if needed */
/*  local function start with halbtc8821a2ant_ */
static u8 halbtc8821a2ant_bt_rssi_state(u8 level_num, u8 rssi_thresh,
					u8 rssi_thresh1)
{
	long bt_rssi = 0;
	u8 bt_rssi_state = coex_sta->pre_bt_rssi_state;

	bt_rssi = coex_sta->bt_rssi;

	if (level_num == 2) {
		if ((coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_bt_rssi_state == BTC_RSSI_STATE_STAY_LOW)) {
			if (bt_rssi >= (rssi_thresh +
			    BTC_RSSI_COEX_THRESH_TOL_8821A_2ANT)) {
				bt_rssi_state = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state switch to High\n");
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state stay at Low\n");
			}
		} else {
			if (bt_rssi < rssi_thresh) {
				bt_rssi_state = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state switch to Low\n");
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state stay at High\n");
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
			if (bt_rssi >= (rssi_thresh +
			    BTC_RSSI_COEX_THRESH_TOL_8821A_2ANT)) {
				bt_rssi_state = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state switch to Medium\n");
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state stay at Low\n");
			}
		} else if ((coex_sta->pre_bt_rssi_state ==
			    BTC_RSSI_STATE_MEDIUM) ||
			   (coex_sta->pre_bt_rssi_state ==
			    BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (bt_rssi >= (rssi_thresh1 +
			    BTC_RSSI_COEX_THRESH_TOL_8821A_2ANT)) {
				bt_rssi_state = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state switch to High\n");
			} else if (bt_rssi < rssi_thresh) {
				bt_rssi_state = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state switch to Low\n");
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state stay at Medium\n");
			}
		} else {
			if (bt_rssi < rssi_thresh1) {
				bt_rssi_state = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state switch to Medium\n");
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_BT_RSSI_STATE,
					  "[BTCoex], BT Rssi state stay at High\n");
			}
		}
	}

	coex_sta->pre_bt_rssi_state = bt_rssi_state;

	return bt_rssi_state;
}

static u8 wifi21a_rssi_state(struct btc_coexist *btcoexist,
			     u8 index, u8 level_num,
			     u8 rssi_thresh, u8 rssi_thresh1)
{
	long	wifi_rssi = 0;
	u8 wifi_rssi_state = coex_sta->pre_wifi_rssi_state[index];

	btcoexist->btc_get(btcoexist, BTC_GET_S4_WIFI_RSSI, &wifi_rssi);

	if (level_num == 2) {
		if ((coex_sta->pre_wifi_rssi_state[index] ==
		     BTC_RSSI_STATE_LOW) ||
		    (coex_sta->pre_wifi_rssi_state[index] ==
		     BTC_RSSI_STATE_STAY_LOW)) {
			if (wifi_rssi >= (rssi_thresh +
			    BTC_RSSI_COEX_THRESH_TOL_8821A_2ANT)) {
				wifi_rssi_state = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state switch to High\n");
			} else {
				wifi_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state stay at Low\n");
			}
		} else {
			if (wifi_rssi < rssi_thresh) {
				wifi_rssi_state = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state switch to Low\n");
			} else {
				wifi_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state stay at High\n");
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
			if (wifi_rssi >= (rssi_thresh +
			    BTC_RSSI_COEX_THRESH_TOL_8821A_2ANT)) {
				wifi_rssi_state = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state switch to Medium\n");
			} else {
				wifi_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state stay at Low\n");
			}
		} else if ((coex_sta->pre_wifi_rssi_state[index] ==
			    BTC_RSSI_STATE_MEDIUM) ||
			   (coex_sta->pre_wifi_rssi_state[index] ==
			    BTC_RSSI_STATE_STAY_MEDIUM)) {
			if (wifi_rssi >= (rssi_thresh1+BTC_RSSI_COEX_THRESH_TOL_8821A_2ANT)) {
				wifi_rssi_state = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state switch to High\n");
			} else if (wifi_rssi < rssi_thresh) {
				wifi_rssi_state = BTC_RSSI_STATE_LOW;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state switch to Low\n");
			} else {
				wifi_rssi_state = BTC_RSSI_STATE_STAY_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state stay at Medium\n");
			}
		} else {
			if (wifi_rssi < rssi_thresh1) {
				wifi_rssi_state = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state switch to Medium\n");
			} else {
				wifi_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_WIFI_RSSI_STATE,
					  "[BTCoex], wifi RSSI state stay at High\n");
			}
		}
	}

	coex_sta->pre_wifi_rssi_state[index] = wifi_rssi_state;

	return wifi_rssi_state;
}

static void monitor_bt_enable_disable(struct btc_coexist *btcoexist)
{
	static bool pre_bt_disabled;
	static u32 bt_disable_cnt;
	bool bt_active = true, bt_disabled = false;

	/*  This function check if bt is disabled */

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
			  "[BTCoex], bt all counters = 0, %d times!!\n",
			  bt_disable_cnt);
		if (bt_disable_cnt >= 2) {
			bt_disabled = true;
			btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_DISABLE,
					   &bt_disabled);
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR,
				  "[BTCoex], BT is disabled !!\n");
		}
	}
	if (pre_bt_disabled != bt_disabled) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR,
			  "[BTCoex], BT is from %s to %s!!\n",
			  (pre_bt_disabled ? "disabled" : "enabled"),
			  (bt_disabled ? "disabled" : "enabled"));
		pre_bt_disabled = bt_disabled;
	}
}

static void halbtc8821a2ant_monitor_bt_ctr(struct btc_coexist *btcoexist)
{
	u32 reg_hp_txrx, reg_lp_txrx, u4tmp;
	u32 reg_hp_tx = 0, reg_hp_rx = 0, reg_lp_tx = 0, reg_lp_rx = 0;

	reg_hp_txrx = 0x770;
	reg_lp_txrx = 0x774;

	u4tmp = btcoexist->btc_read_4byte(btcoexist, reg_hp_txrx);
	reg_hp_tx = u4tmp & MASKLWORD;
	reg_hp_rx = (u4tmp & MASKHWORD)>>16;

	u4tmp = btcoexist->btc_read_4byte(btcoexist, reg_lp_txrx);
	reg_lp_tx = u4tmp & MASKLWORD;
	reg_lp_rx = (u4tmp & MASKHWORD)>>16;

	coex_sta->high_priority_tx = reg_hp_tx;
	coex_sta->high_priority_rx = reg_hp_rx;
	coex_sta->low_priority_tx = reg_lp_tx;
	coex_sta->low_priority_rx = reg_lp_rx;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR,
		  "[BTCoex], High Priority Tx/Rx (reg 0x%x) = 0x%x(%d)/0x%x(%d)\n",
		  reg_hp_txrx, reg_hp_tx, reg_hp_tx, reg_hp_rx, reg_hp_rx);
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_BT_MONITOR,
		  "[BTCoex], Low Priority Tx/Rx (reg 0x%x) = 0x%x(%d)/0x%x(%d)\n",
		  reg_lp_txrx, reg_lp_tx, reg_lp_tx, reg_lp_rx, reg_lp_rx);

	/*  reset counter */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0xc);
}

static void halbtc8821a2ant_query_bt_info(struct btc_coexist *btcoexist)
{
	u8 h2c_parameter[1] = {0};

	coex_sta->c2h_bt_info_req_sent = true;

	h2c_parameter[0] |= BIT(0);	/*  trigger */

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], Query Bt Info, FW write 0x61 = 0x%x\n",
		  h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x61, 1, h2c_parameter);
}

static u8 halbtc8821a2ant_action_algorithm(struct btc_coexist *btcoexist)
{
	struct btc_stack_info *stack_info = &btcoexist->stack_info;
	bool bt_hs_on = false;
	u8 algorithm = BT_8821A_2ANT_COEX_ALGO_UNDEFINED;
	u8 num_of_diff_profile = 0;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);

	/* for win-8 stack HID report error */
	if (!stack_info->hid_exist) {
		/* sync  BTInfo with BT firmware and stack */
		stack_info->hid_exist = coex_sta->hid_exist;
	}
	/*  when stack HID report error, here we use the info from bt fw. */
	if (!stack_info->bt_link_exist)
		stack_info->bt_link_exist = coex_sta->bt_link_exist;

	if (!coex_sta->bt_link_exist) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], No profile exists!!!\n");
		return algorithm;
	}

	if (coex_sta->sco_exist)
		num_of_diff_profile++;
	if (coex_sta->hid_exist)
		num_of_diff_profile++;
	if (coex_sta->pan_exist)
		num_of_diff_profile++;
	if (coex_sta->a2dp_exist)
		num_of_diff_profile++;

	if (num_of_diff_profile == 1) {
		if (coex_sta->sco_exist) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], SCO only\n");
			algorithm = BT_8821A_2ANT_COEX_ALGO_SCO;
		} else {
			if (coex_sta->hid_exist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
					  "[BTCoex], HID only\n");
				algorithm = BT_8821A_2ANT_COEX_ALGO_HID;
			} else if (coex_sta->a2dp_exist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
					  "[BTCoex], A2DP only\n");
				algorithm = BT_8821A_2ANT_COEX_ALGO_A2DP;
			} else if (coex_sta->pan_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], PAN(HS) only\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_PANHS;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], PAN(EDR) only\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_PANEDR;
				}
			}
		}
	} else if (num_of_diff_profile == 2) {
		if (coex_sta->sco_exist) {
			if (coex_sta->hid_exist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, "[BTCoex], SCO + HID\n");
				algorithm = BT_8821A_2ANT_COEX_ALGO_PANEDR_HID;
			} else if (coex_sta->a2dp_exist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
					  "[BTCoex], SCO + A2DP ==> SCO\n");
				algorithm = BT_8821A_2ANT_COEX_ALGO_PANEDR_HID;
			} else if (coex_sta->pan_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], SCO + PAN(HS)\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_SCO;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], SCO + PAN(EDR)\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (coex_sta->hid_exist &&
			    coex_sta->a2dp_exist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
					  "[BTCoex], HID + A2DP\n");
				algorithm = BT_8821A_2ANT_COEX_ALGO_HID_A2DP;
			} else if (coex_sta->hid_exist &&
				   coex_sta->pan_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], HID + PAN(HS)\n");
					algorithm =  BT_8821A_2ANT_COEX_ALGO_HID;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], HID + PAN(EDR)\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (coex_sta->pan_exist &&
				   coex_sta->a2dp_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], A2DP + PAN(HS)\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_A2DP_PANHS;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], A2DP + PAN(EDR)\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_PANEDR_A2DP;
				}
			}
		}
	} else if (num_of_diff_profile == 3) {
		if (coex_sta->sco_exist) {
			if (coex_sta->hid_exist &&
			    coex_sta->a2dp_exist) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
					  "[BTCoex], SCO + HID + A2DP ==> HID\n");
				algorithm = BT_8821A_2ANT_COEX_ALGO_PANEDR_HID;
			} else if (coex_sta->hid_exist &&
				   coex_sta->pan_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], SCO + HID + PAN(HS)\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_PANEDR_HID;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], SCO + HID + PAN(EDR)\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (coex_sta->pan_exist &&
				   coex_sta->a2dp_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], SCO + A2DP + PAN(HS)\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_PANEDR_HID;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], SCO + A2DP + PAN(EDR) ==> HID\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (coex_sta->hid_exist &&
			    coex_sta->pan_exist &&
			    coex_sta->a2dp_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], HID + A2DP + PAN(HS)\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], HID + A2DP + PAN(EDR)\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_HID_A2DP_PANEDR;
				}
			}
		}
	} else if (num_of_diff_profile >= 3) {
		if (coex_sta->sco_exist) {
			if (coex_sta->hid_exist &&
			    coex_sta->pan_exist &&
			    coex_sta->a2dp_exist) {
				if (bt_hs_on) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], Error!!! SCO + HID + A2DP + PAN(HS)\n");

				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
						  "[BTCoex], SCO + HID + A2DP + PAN(EDR) ==>PAN(EDR)+HID\n");
					algorithm = BT_8821A_2ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
	}
	return algorithm;
}

static bool halbtc8821a2ant_need_to_dec_bt_pwr(struct btc_coexist *btcoexist)
{
	bool ret = false;
	bool bt_hs_on = false, wifi_connected = false;
	long bt_hs_rssi = 0;
	u8 bt_rssi_state;

	if (!btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on))
		return false;
	if (!btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
				&wifi_connected))
		return false;
	if (!btcoexist->btc_get(btcoexist, BTC_GET_S4_HS_RSSI, &bt_hs_rssi))
		return false;

	bt_rssi_state = halbtc8821a2ant_bt_rssi_state(2, 35, 0);

	if (wifi_connected) {
		if (bt_hs_on) {
			if (bt_hs_rssi > 37) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
					  "[BTCoex], Need to decrease bt power for HS mode!!\n");
				ret = true;
			}
		} else {
			if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
			    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
					  "[BTCoex], Need to decrease bt power for Wifi is connected!!\n");
				ret = true;
			}
		}
	}
	return ret;
}

static void set_fw_dac_swing_level(struct btc_coexist *btcoexist,
				   u8 dac_swing_lvl)
{
	u8 h2c_parameter[1] = {0};

	/*  There are several type of dacswing */
	/*  0x18/ 0x10/ 0xc/ 0x8/ 0x4/ 0x6 */
	h2c_parameter[0] = dac_swing_lvl;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], Set Dac Swing Level = 0x%x\n", dac_swing_lvl);
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], FW write 0x64 = 0x%x\n", h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x64, 1, h2c_parameter);
}

static void halbtc8821a2ant_set_fw_dec_bt_pwr(struct btc_coexist *btcoexist,
					      bool dec_bt_pwr)
{
	u8 h2c_parameter[1] = {0};

	h2c_parameter[0] = 0;

	if (dec_bt_pwr)
		h2c_parameter[0] |= BIT(1);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], decrease Bt Power : %s, FW write 0x62 = 0x%x\n",
		  (dec_bt_pwr ? "Yes!!" : "No!!"), h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x62, 1, h2c_parameter);
}

static void halbtc8821a2ant_dec_bt_pwr(struct btc_coexist *btcoexist,
				       bool force_exec, bool dec_bt_pwr)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
		  "[BTCoex], %s Dec BT power = %s\n",
		  (force_exec ? "force to" : ""),
		  ((dec_bt_pwr) ? "ON" : "OFF"));
	coex_dm->cur_dec_bt_pwr = dec_bt_pwr;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], pre_dec_bt_pwr =%d, cur_dec_bt_pwr =%d\n",
			  coex_dm->pre_dec_bt_pwr, coex_dm->cur_dec_bt_pwr);

		if (coex_dm->pre_dec_bt_pwr == coex_dm->cur_dec_bt_pwr)
			return;
	}
	halbtc8821a2ant_set_fw_dec_bt_pwr(btcoexist, coex_dm->cur_dec_bt_pwr);

	coex_dm->pre_dec_bt_pwr = coex_dm->cur_dec_bt_pwr;
}

static void set_fw_bt_lna_constrain(struct btc_coexist *btcoexist,
				    bool bt_lna_cons_on)
{
	u8 h2c_parameter[2] = {0};

	h2c_parameter[0] = 0x3;	/*  opCode, 0x3 = BT_SET_LNA_CONSTRAIN */

	if (bt_lna_cons_on)
		h2c_parameter[1] |= BIT(0);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], set BT LNA Constrain: %s, FW write 0x69 = 0x%x\n",
		  (bt_lna_cons_on ? "ON!!" : "OFF!!"),
		  h2c_parameter[0]<<8|h2c_parameter[1]);

	btcoexist->btc_fill_h2c(btcoexist, 0x69, 2, h2c_parameter);
}

static void set_bt_lna_constrain(struct btc_coexist *btcoexist, bool force_exec,
				 bool bt_lna_cons_on)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
		  "[BTCoex], %s BT Constrain = %s\n",
		  (force_exec ? "force" : ""),
		  ((bt_lna_cons_on) ? "ON" : "OFF"));
	coex_dm->cur_bt_lna_constrain = bt_lna_cons_on;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], pre_bt_lna_constrain =%d, cur_bt_lna_constrain =%d\n",
			  coex_dm->pre_bt_lna_constrain,
			  coex_dm->cur_bt_lna_constrain);

		if (coex_dm->pre_bt_lna_constrain ==
		    coex_dm->cur_bt_lna_constrain)
			return;
	}
	set_fw_bt_lna_constrain(btcoexist, coex_dm->cur_bt_lna_constrain);

	coex_dm->pre_bt_lna_constrain = coex_dm->cur_bt_lna_constrain;
}

static void halbtc8821a2ant_set_fw_bt_psd_mode(struct btc_coexist *btcoexist,
					       u8 bt_psd_mode)
{
	u8 h2c_parameter[2] = {0};

	h2c_parameter[0] = 0x2;	/*  opCode, 0x2 = BT_SET_PSD_MODE */

	h2c_parameter[1] = bt_psd_mode;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], set BT PSD mode = 0x%x, FW write 0x69 = 0x%x\n",
		  h2c_parameter[1],
		  h2c_parameter[0] << 8 | h2c_parameter[1]);

	btcoexist->btc_fill_h2c(btcoexist, 0x69, 2, h2c_parameter);
}

static void halbtc8821a2ant_set_bt_psd_mode(struct btc_coexist *btcoexist,
					    bool force_exec, u8 bt_psd_mode)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
		  "[BTCoex], %s BT PSD mode = 0x%x\n",
		  (force_exec ? "force" : ""), bt_psd_mode);
	coex_dm->cur_bt_psd_mode = bt_psd_mode;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], pre_bt_psd_mode = 0x%x, cur_bt_psd_mode = 0x%x\n",
			  coex_dm->pre_bt_psd_mode, coex_dm->cur_bt_psd_mode);

		if (coex_dm->pre_bt_psd_mode == coex_dm->cur_bt_psd_mode)
			return;
	}
	halbtc8821a2ant_set_fw_bt_psd_mode(btcoexist, coex_dm->cur_bt_psd_mode);

	coex_dm->pre_bt_psd_mode = coex_dm->cur_bt_psd_mode;
}

static void halbtc8821a2ant_set_bt_auto_report(struct btc_coexist *btcoexist,
					       bool enable_auto_report)
{
	u8 h2c_parameter[1] = {0};

	h2c_parameter[0] = 0;

	if (enable_auto_report)
		h2c_parameter[0] |= BIT(0);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], BT FW auto report : %s, FW write 0x68 = 0x%x\n",
		  (enable_auto_report ? "Enabled!!" : "Disabled!!"),
		  h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x68, 1, h2c_parameter);
}

static void halbtc8821a2ant_bt_auto_report(struct btc_coexist *btcoexist,
					   bool force_exec,
					   bool enable_auto_report)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
		  "[BTCoex], %s BT Auto report = %s\n",
		  (force_exec ? "force to" : ""),
		  ((enable_auto_report) ? "Enabled" : "Disabled"));
	coex_dm->cur_bt_auto_report = enable_auto_report;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], pre_bt_auto_report =%d, cur_bt_auto_report =%d\n",
			  coex_dm->pre_bt_auto_report,
			  coex_dm->cur_bt_auto_report);

		if (coex_dm->pre_bt_auto_report == coex_dm->cur_bt_auto_report)
			return;
	}
	halbtc8821a2ant_set_bt_auto_report(btcoexist,
					   coex_dm->cur_bt_auto_report);

	coex_dm->pre_bt_auto_report = coex_dm->cur_bt_auto_report;
}

static void halbtc8821a2ant_fw_dac_swing_lvl(struct btc_coexist *btcoexist,
					     bool force_exec,
					     u8 fw_dac_swing_lvl)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
		  "[BTCoex], %s set FW Dac Swing level = %d\n",
		  (force_exec ? "force to" : ""), fw_dac_swing_lvl);
	coex_dm->cur_fw_dac_swing_lvl = fw_dac_swing_lvl;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], pre_fw_dac_swing_lvl =%d, cur_fw_dac_swing_lvl =%d\n",
			  coex_dm->pre_fw_dac_swing_lvl,
			  coex_dm->cur_fw_dac_swing_lvl);

		if (coex_dm->pre_fw_dac_swing_lvl ==
		    coex_dm->cur_fw_dac_swing_lvl)
			return;
	}

	set_fw_dac_swing_level(btcoexist, coex_dm->cur_fw_dac_swing_lvl);

	coex_dm->pre_fw_dac_swing_lvl = coex_dm->cur_fw_dac_swing_lvl;
}

static void set_sw_rf_rx_lpf_corner(struct btc_coexist *btcoexist,
				    bool rx_rf_shrink_on)
{
	if (rx_rf_shrink_on) {
		/* Shrink RF Rx LPF corner */
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
			  "[BTCoex], Shrink RF Rx LPF corner!!\n");
		btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1e,
					  0xfffff, 0xffffc);
	} else {
		/* Resume RF Rx LPF corner */
		/*  After initialized, we can use coex_dm->bt_rf0x1e_backup */
		if (btcoexist->initilized) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
				  "[BTCoex], Resume RF Rx LPF corner!!\n");
			btcoexist->btc_set_rf_reg(btcoexist, BTC_RF_A, 0x1e,
						  0xfffff,
						  coex_dm->bt_rf0x1e_backup);
		}
	}
}

static void halbtc8821a2ant_RfShrink(struct btc_coexist *btcoexist,
				     bool force_exec, bool rx_rf_shrink_on)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW,
		  "[BTCoex], %s turn Rx RF Shrink = %s\n",
		  (force_exec ? "force to" : ""),
		  ((rx_rf_shrink_on) ? "ON" : "OFF"));
	coex_dm->cur_rf_rx_lpf_shrink = rx_rf_shrink_on;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL,
			  "[BTCoex], pre_rf_rx_lpf_shrink =%d, cur_rf_rx_lpf_shrink =%d\n",
			  coex_dm->pre_rf_rx_lpf_shrink,
			  coex_dm->cur_rf_rx_lpf_shrink);

		if (coex_dm->pre_rf_rx_lpf_shrink ==
		    coex_dm->cur_rf_rx_lpf_shrink)
			return;
	}
	set_sw_rf_rx_lpf_corner(btcoexist, coex_dm->cur_rf_rx_lpf_shrink);

	coex_dm->pre_rf_rx_lpf_shrink = coex_dm->cur_rf_rx_lpf_shrink;
}

static void set_sw_penalty_tx_rate_adap(struct btc_coexist *btcoexist,
					bool low_penalty_ra)
{
	u8 h2c_parameter[6] = {0};

	h2c_parameter[0] = 0x6;	/*  opCode, 0x6 = Retry_Penalty */

	if (low_penalty_ra) {
		h2c_parameter[1] |= BIT(0);
		/* normal rate except MCS7/6/5, OFDM54/48/36 */
		h2c_parameter[2] = 0x00;
		h2c_parameter[3] = 0xf7;  /* MCS7 or OFDM54 */
		h2c_parameter[4] = 0xf8;  /* MCS6 or OFDM48 */
		h2c_parameter[5] = 0xf9;  /* MCS5 or OFDM36  */
	}

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], set WiFi Low-Penalty Retry: %s",
		  (low_penalty_ra ? "ON!!" : "OFF!!"));

	btcoexist->btc_fill_h2c(btcoexist, 0x69, 6, h2c_parameter);
}

static void halbtc8821a2ant_low_penalty_ra(struct btc_coexist *btcoexist,
					   bool force_exec, bool low_penalty_ra)
{
	/* return; */
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW,
		  "[BTCoex], %s turn LowPenaltyRA = %s\n",
		  (force_exec ? "force to" : ""),
		  ((low_penalty_ra) ? "ON" : "OFF"));
	coex_dm->cur_low_penalty_ra = low_penalty_ra;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL,
			  "[BTCoex], pre_low_penalty_ra =%d, cur_low_penalty_ra =%d\n",
			  coex_dm->pre_low_penalty_ra,
			  coex_dm->cur_low_penalty_ra);

		if (coex_dm->pre_low_penalty_ra == coex_dm->cur_low_penalty_ra)
			return;
	}
	set_sw_penalty_tx_rate_adap(btcoexist, coex_dm->cur_low_penalty_ra);

	coex_dm->pre_low_penalty_ra = coex_dm->cur_low_penalty_ra;
}

static void halbtc8821a2ant_set_dac_swing_reg(struct btc_coexist *btcoexist,
					      u32 level)
{
	u8 val = (u8)level;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
		  "[BTCoex], Write SwDacSwing = 0x%x\n", level);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0xc5b, 0x3e, val);
}

static void set_sw_fulltime_dac_swing(struct btc_coexist *btcoexist,
				      bool sw_dac_swing_on,
				      u32 sw_dac_swing_lvl)
{
	if (sw_dac_swing_on)
		halbtc8821a2ant_set_dac_swing_reg(btcoexist, sw_dac_swing_lvl);
	else
		halbtc8821a2ant_set_dac_swing_reg(btcoexist, 0x18);
}

static void halbtc8821a2ant_dac_swing(struct btc_coexist *btcoexist,
				      bool force_exec, bool dac_swing_on,
				      u32 dac_swing_lvl)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW,
		  "[BTCoex], %s turn DacSwing =%s, dac_swing_lvl = 0x%x\n",
		  (force_exec ? "force to" : ""),
		  ((dac_swing_on) ? "ON" : "OFF"), dac_swing_lvl);
	coex_dm->cur_dac_swing_on = dac_swing_on;
	coex_dm->cur_dac_swing_lvl = dac_swing_lvl;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL,
			  "[BTCoex], pre_dac_swing_on =%d, pre_dac_swing_lvl = 0x%x, cur_dac_swing_on =%d, cur_dac_swing_lvl = 0x%x\n",
			  coex_dm->pre_dac_swing_on,
			  coex_dm->pre_dac_swing_lvl,
			  coex_dm->cur_dac_swing_on,
			  coex_dm->cur_dac_swing_lvl);

		if ((coex_dm->pre_dac_swing_on == coex_dm->cur_dac_swing_on) &&
		    (coex_dm->pre_dac_swing_lvl == coex_dm->cur_dac_swing_lvl))
			return;
	}
	mdelay(30);
	set_sw_fulltime_dac_swing(btcoexist, dac_swing_on, dac_swing_lvl);

	coex_dm->pre_dac_swing_on = coex_dm->cur_dac_swing_on;
	coex_dm->pre_dac_swing_lvl = coex_dm->cur_dac_swing_lvl;
}

static void halbtc8821a2ant_set_adc_back_off(struct btc_coexist *btcoexist,
					     bool adc_back_off)
{
	if (adc_back_off) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
			  "[BTCoex], BB BackOff Level On!\n");
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x8db, 0x60, 0x3);
	} else {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
			  "[BTCoex], BB BackOff Level Off!\n");
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0x8db, 0x60, 0x1);
	}
}

static void halbtc8821a2ant_adc_back_off(struct btc_coexist *btcoexist,
					 bool force_exec, bool adc_back_off)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW,
		  "[BTCoex], %s turn AdcBackOff = %s\n",
		  (force_exec ? "force to" : ""),
		  ((adc_back_off) ? "ON" : "OFF"));
	coex_dm->cur_adc_back_off = adc_back_off;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL,
			  "[BTCoex], pre_adc_back_off =%d, cur_adc_back_off =%d\n",
			coex_dm->pre_adc_back_off, coex_dm->cur_adc_back_off);

		if (coex_dm->pre_adc_back_off == coex_dm->cur_adc_back_off)
			return;
	}
	halbtc8821a2ant_set_adc_back_off(btcoexist, coex_dm->cur_adc_back_off);

	coex_dm->pre_adc_back_off = coex_dm->cur_adc_back_off;
}

static void halbtc8821a2ant_set_coex_table(struct btc_coexist *btcoexist,
					   u32 val0x6c0, u32 val0x6c4,
					   u32 val0x6c8, u8 val0x6cc)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
		  "[BTCoex], set coex table, set 0x6c0 = 0x%x\n", val0x6c0);
	btcoexist->btc_write_4byte(btcoexist, 0x6c0, val0x6c0);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
		  "[BTCoex], set coex table, set 0x6c4 = 0x%x\n", val0x6c4);
	btcoexist->btc_write_4byte(btcoexist, 0x6c4, val0x6c4);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
		  "[BTCoex], set coex table, set 0x6c8 = 0x%x\n", val0x6c8);
	btcoexist->btc_write_4byte(btcoexist, 0x6c8, val0x6c8);

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_EXEC,
		  "[BTCoex], set coex table, set 0x6cc = 0x%x\n", val0x6cc);
	btcoexist->btc_write_1byte(btcoexist, 0x6cc, val0x6cc);
}

static void halbtc8821a2ant_coex_table(struct btc_coexist *btcoexist,
				       bool force_exec, u32 val0x6c0,
				       u32 val0x6c4, u32 val0x6c8, u8 val0x6cc)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW,
		  "[BTCoex], %s write Coex Table 0x6c0 = 0x%x, 0x6c4 = 0x%x, 0x6c8 = 0x%x, 0x6cc = 0x%x\n",
		  (force_exec ? "force to" : ""),
		  val0x6c0, val0x6c4, val0x6c8, val0x6cc);
	coex_dm->cur_val0x6c0 = val0x6c0;
	coex_dm->cur_val0x6c4 = val0x6c4;
	coex_dm->cur_val0x6c8 = val0x6c8;
	coex_dm->cur_val0x6cc = val0x6cc;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL,
			  "[BTCoex], pre_val0x6c0 = 0x%x, pre_val0x6c4 = 0x%x, pre_val0x6c8 = 0x%x, pre_val0x6cc = 0x%x !!\n",
			  coex_dm->pre_val0x6c0, coex_dm->pre_val0x6c4,
			  coex_dm->pre_val0x6c8, coex_dm->pre_val0x6cc);
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_SW_DETAIL,
			  "[BTCoex], cur_val0x6c0 = 0x%x, cur_val0x6c4 = 0x%x, cur_val0x6c8 = 0x%x, cur_val0x6cc = 0x%x !!\n",
			  coex_dm->cur_val0x6c0, coex_dm->cur_val0x6c4,
			  coex_dm->cur_val0x6c8, coex_dm->cur_val0x6cc);

		if ((coex_dm->pre_val0x6c0 == coex_dm->cur_val0x6c0) &&
			(coex_dm->pre_val0x6c4 == coex_dm->cur_val0x6c4) &&
			(coex_dm->pre_val0x6c8 == coex_dm->cur_val0x6c8) &&
			(coex_dm->pre_val0x6cc == coex_dm->cur_val0x6cc))
			return;
	}
	halbtc8821a2ant_set_coex_table(btcoexist, val0x6c0, val0x6c4,
				       val0x6c8, val0x6cc);

	coex_dm->pre_val0x6c0 = coex_dm->cur_val0x6c0;
	coex_dm->pre_val0x6c4 = coex_dm->cur_val0x6c4;
	coex_dm->pre_val0x6c8 = coex_dm->cur_val0x6c8;
	coex_dm->pre_val0x6cc = coex_dm->cur_val0x6cc;
}

static void set_fw_ignore_wlan_act(struct btc_coexist *btcoexist, bool enable)
{
	u8 h2c_parameter[1] = {0};

	if (enable)
		h2c_parameter[0] |= BIT(0);		/*  function enable */

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], set FW for BT Ignore Wlan_Act, FW write 0x63 = 0x%x\n",
		  h2c_parameter[0]);

	btcoexist->btc_fill_h2c(btcoexist, 0x63, 1, h2c_parameter);
}

static void halbtc8821a2ant_ignore_wlan_act(struct btc_coexist *btcoexist,
					    bool force_exec, bool enable)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
		  "[BTCoex], %s turn Ignore WlanAct %s\n",
		  (force_exec ? "force to" : ""), (enable ? "ON" : "OFF"));
	coex_dm->cur_ignore_wlan_act = enable;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], pre_ignore_wlan_act = %d, cur_ignore_wlan_act = %d!!\n",
			  coex_dm->pre_ignore_wlan_act,
			  coex_dm->cur_ignore_wlan_act);
		if (coex_dm->pre_ignore_wlan_act ==
		    coex_dm->cur_ignore_wlan_act)
			return;
	}
	set_fw_ignore_wlan_act(btcoexist, enable);

	coex_dm->pre_ignore_wlan_act = coex_dm->cur_ignore_wlan_act;
}

static void halbtc8821a2ant_set_fw_pstdma(struct btc_coexist *btcoexist,
					  u8 byte1, u8 byte2, u8 byte3,
					  u8 byte4, u8 byte5)
{
	u8 h2c_parameter[5] = {0};

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

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], FW write 0x60(5bytes) = 0x%x%08x\n",
		  h2c_parameter[0],
		  h2c_parameter[1] << 24 | h2c_parameter[2] << 16 |
		  h2c_parameter[3]<<8|h2c_parameter[4]);

	btcoexist->btc_fill_h2c(btcoexist, 0x60, 5, h2c_parameter);
}

static void sw_mechanism1(struct btc_coexist *btcoexist, bool shrink_rx_lpf,
			  bool low_penalty_ra, bool limited_dig,
			  bool bt_lna_constrain)
{
	u32 wifi_bw;

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_HT40 != wifi_bw) {  /* only shrink RF Rx LPF for HT40 */
		if (shrink_rx_lpf)
			shrink_rx_lpf = false;
	}

	 halbtc8821a2ant_RfShrink(btcoexist, NORMAL_EXEC, shrink_rx_lpf);
	halbtc8821a2ant_low_penalty_ra(btcoexist, NORMAL_EXEC, low_penalty_ra);

	/* no limited DIG */
	/* set_bt_lna_constrain(btcoexist, NORMAL_EXEC, bBTLNAConstrain); */
}

static void sw_mechanism2(struct btc_coexist *btcoexist, bool agc_table_shift,
			  bool adc_back_off, bool sw_dac_swing,
			  u32 dac_swing_lvl)
{
	/* halbtc8821a2ant_AgcTable(btcoexist, NORMAL_EXEC, bAGCTableShift); */
	halbtc8821a2ant_adc_back_off(btcoexist, NORMAL_EXEC, adc_back_off);
	halbtc8821a2ant_dac_swing(btcoexist, NORMAL_EXEC, sw_dac_swing,
				  sw_dac_swing);
}

static void halbtc8821a2ant_set_ant_path(struct btc_coexist *btcoexist,
					 u8 ant_pos_type, bool init_hw_cfg,
					 bool wifi_off)
{
	struct btc_board_info *board_info = &btcoexist->board_info;
	u32 u4tmp = 0;
	u8 h2c_parameter[2] = {0};

	if (init_hw_cfg) {
		/*  0x4c[23] = 0, 0x4c[24] = 1  Antenna control by WL/BT */
		u4tmp = btcoexist->btc_read_4byte(btcoexist, 0x4c);
		u4tmp &= ~BIT(23);
		u4tmp |= BIT(24);
		btcoexist->btc_write_4byte(btcoexist, 0x4c, u4tmp);

		btcoexist->btc_write_4byte(btcoexist, 0x974, 0x3ff);
		btcoexist->btc_write_1byte(btcoexist, 0xcb4, 0x77);

		if (board_info->btdm_ant_pos == BTC_ANTENNA_AT_MAIN_PORT) {
			/* tell firmware "antenna inverse"  ==> WRONG firmware
			 * antenna control code.==>need fw to fix */
			h2c_parameter[0] = 1;
			h2c_parameter[1] = 1;
			btcoexist->btc_fill_h2c(btcoexist, 0x65, 2, h2c_parameter);
		} else {
			/* tell firmware "no antenna inverse" ==> WRONG firmware
			 * antenna control code.==>need fw to fix */
			h2c_parameter[0] = 0;
			h2c_parameter[1] = 1;
			btcoexist->btc_fill_h2c(btcoexist, 0x65, 2,
						h2c_parameter);
		}
	}

	/*  ext switch setting */
	switch (ant_pos_type) {
	case BTC_ANT_WIFI_AT_MAIN:
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcb7, 0x30, 0x1);
		break;
	case BTC_ANT_WIFI_AT_AUX:
		btcoexist->btc_write_1byte_bitmask(btcoexist, 0xcb7, 0x30, 0x2);
		break;
	}
}

static void ps21a_tdma(struct btc_coexist *btcoexist, bool force_exec,
		       bool turn_on, u8 type)
{
	/* bool turn_on_by_cnt = false; */
	/* u8 ps_tdma_type_by_cnt = 0; */

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
		  "[BTCoex], %s turn %s PS TDMA, type =%d\n",
		  (force_exec ? "force to" : ""),
		  (turn_on ? "ON" : "OFF"), type);
	coex_dm->cur_ps_tdma_on = turn_on;
	coex_dm->cur_ps_tdma = type;

	if (!force_exec) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], pre_ps_tdma_on = %d, cur_ps_tdma_on = %d!!\n",
			  coex_dm->pre_ps_tdma_on, coex_dm->cur_ps_tdma_on);
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], pre_ps_tdma = %d, cur_ps_tdma = %d!!\n",
			  coex_dm->pre_ps_tdma, coex_dm->cur_ps_tdma);

		if ((coex_dm->pre_ps_tdma_on == coex_dm->cur_ps_tdma_on) &&
		    (coex_dm->pre_ps_tdma == coex_dm->cur_ps_tdma))
			return;
	}
	if (turn_on) {
		switch (type) {
		case 1:
		default:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xe3, 0x1a,
						      0x1a, 0xe1, 0x90);
			break;
		case 2:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xe3, 0x12,
						      0x12, 0xe1, 0x90);
			break;
		case 3:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xe3, 0x1c,
						      0x3, 0xf1, 0x90);
			break;
		case 4:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xe3, 0x10,
						      0x03, 0xf1, 0x90);
			break;
		case 5:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xe3, 0x1a,
						      0x1a, 0x60, 0x90);
			break;
		case 6:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xe3, 0x12,
						      0x12, 0x60, 0x90);
			break;
		case 7:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xe3, 0x1c,
						      0x3, 0x70, 0x90);
			break;
		case 8:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xa3, 0x10,
						      0x3, 0x70, 0x90);
			break;
		case 9:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xe3, 0x1a,
						      0x1a, 0xe1, 0x90);
			break;
		case 10:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xe3, 0x12,
						      0x12, 0xe1, 0x90);
			break;
		case 11:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xe3, 0xa,
						      0xa, 0xe1, 0x90);
			break;
		case 12:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xe3, 0x5,
						      0x5, 0xe1, 0x90);
			break;
		case 13:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xe3, 0x1a,
						      0x1a, 0x60, 0x90);
			break;
		case 14:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xe3, 0x12,
						      0x12, 0x60, 0x90);
			break;
		case 15:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xe3, 0xa,
						      0xa, 0x60, 0x90);
			break;
		case 16:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xe3, 0x5,
						      0x5, 0x60, 0x90);
			break;
		case 17:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xa3, 0x2f,
						      0x2f, 0x60, 0x90);
			break;
		case 18:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xe3, 0x5,
						      0x5, 0xe1, 0x90);
			break;
		case 19:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xe3, 0x25,
						      0x25, 0xe1, 0x90);
			break;
		case 20:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xe3, 0x25,
						      0x25, 0x60, 0x90);
			break;
		case 21:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xe3, 0x15,
						      0x03, 0x70, 0x90);
			break;
		case 71:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0xe3, 0x1a,
						      0x1a, 0xe1, 0x90);
			break;
		}
	} else {
		/*  disable PS tdma */
		switch (type) {
		case 0:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0x0, 0x0, 0x0,
						      0x40, 0x0);
			break;
		case 1:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0x0, 0x0, 0x0,
						      0x48, 0x0);
			break;
		default:
			halbtc8821a2ant_set_fw_pstdma(btcoexist, 0x0, 0x0, 0x0,
						      0x40, 0x0);
			break;
		}
	}

	/*  update pre state */
	coex_dm->pre_ps_tdma_on = coex_dm->cur_ps_tdma_on;
	coex_dm->pre_ps_tdma = coex_dm->cur_ps_tdma;
}

static void halbtc8821a2ant_coex_all_off(struct btc_coexist *btcoexist)
{
	/*  fw all off */
	ps21a_tdma(btcoexist, NORMAL_EXEC, false, 1);
	halbtc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
	halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	/*  sw all off */
	sw_mechanism1(btcoexist, false, false, false, false);
	sw_mechanism2(btcoexist, false, false, false, 0x18);

	/*  hw all off */
	halbtc8821a2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55555555,
				   0x55555555, 0xffff, 0x3);
}

static void halbtc8821a2ant_coex_under_5g(struct btc_coexist *btcoexist)
{
	halbtc8821a2ant_coex_all_off(btcoexist);
}

static void halbtc8821a2ant_init_coex_dm(struct btc_coexist *btcoexist)
{
	/*  force to reset coex mechanism */
	halbtc8821a2ant_coex_table(btcoexist, FORCE_EXEC, 0x55555555,
				   0x55555555, 0xffff, 0x3);

	ps21a_tdma(btcoexist, FORCE_EXEC, false, 1);
	halbtc8821a2ant_fw_dac_swing_lvl(btcoexist, FORCE_EXEC, 6);
	halbtc8821a2ant_dec_bt_pwr(btcoexist, FORCE_EXEC, false);

	sw_mechanism1(btcoexist, false, false, false, false);
	sw_mechanism2(btcoexist, false, false, false, 0x18);
}

static void halbtc8821a2ant_bt_inquiry_page(struct btc_coexist *btcoexist)
{
	bool low_pwr_disable = true;

	btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
			   &low_pwr_disable);

	halbtc8821a2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55ff55ff,
				   0x5afa5afa, 0xffff, 0x3);
	ps21a_tdma(btcoexist, NORMAL_EXEC, true, 3);
}

static bool halbtc8821a2ant_is_common_action(struct btc_coexist *btcoexist)
{
	bool common = false, wifi_connected = false, wifi_busy = false;
	bool low_pwr_disable = false;

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
			   &wifi_connected);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);

	halbtc8821a2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55ff55ff,
				   0x5afa5afa, 0xffff, 0x3);

	if (!wifi_connected &&
	    BT_8821A_2ANT_BT_STATUS_IDLE == coex_dm->bt_status) {
		low_pwr_disable = false;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);

		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], Wifi IPS + BT IPS!!\n");

		ps21a_tdma(btcoexist, NORMAL_EXEC, false, 1);
		halbtc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

		sw_mechanism1(btcoexist, false, false, false, false);
		sw_mechanism2(btcoexist, false, false, false, 0x18);

		common = true;
	} else if (wifi_connected &&
		   (BT_8821A_2ANT_BT_STATUS_IDLE == coex_dm->bt_status)) {
		low_pwr_disable = false;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);

		if (wifi_busy) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Wifi Busy + BT IPS!!\n");
			ps21a_tdma(btcoexist, NORMAL_EXEC, false, 1);
		} else {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Wifi LPS + BT IPS!!\n");
			ps21a_tdma(btcoexist, NORMAL_EXEC, false, 1);
		}

		halbtc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

		sw_mechanism1(btcoexist, false, false, false, false);
		sw_mechanism2(btcoexist, false, false, false, 0x18);

		common = true;
	} else if (!wifi_connected &&
		   (BT_8821A_2ANT_BT_STATUS_CONNECTED_IDLE ==
		    coex_dm->bt_status)) {
		low_pwr_disable = true;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);

		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], Wifi IPS + BT LPS!!\n");

		ps21a_tdma(btcoexist, NORMAL_EXEC, false, 1);
		halbtc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

		sw_mechanism1(btcoexist, false, false, false, false);
		sw_mechanism2(btcoexist, false, false, false, 0x18);

		common = true;
	} else if (wifi_connected &&
		   (BT_8821A_2ANT_BT_STATUS_CONNECTED_IDLE ==
		    coex_dm->bt_status)) {
		low_pwr_disable = true;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);

		if (wifi_busy) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Wifi Busy + BT LPS!!\n");
			ps21a_tdma(btcoexist, NORMAL_EXEC, false, 1);
		} else {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Wifi LPS + BT LPS!!\n");
			ps21a_tdma(btcoexist, NORMAL_EXEC, false, 1);
		}

		halbtc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

		sw_mechanism1(btcoexist, true, true, true, true);
		sw_mechanism2(btcoexist, false, false, false, 0x18);

		common = true;
	} else if (!wifi_connected &&
		   (BT_8821A_2ANT_BT_STATUS_NON_IDLE == coex_dm->bt_status)) {
		low_pwr_disable = false;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);

		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], Wifi IPS + BT Busy!!\n");

		ps21a_tdma(btcoexist, NORMAL_EXEC, false, 1);
		halbtc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

		sw_mechanism1(btcoexist, false, false, false, false);
		sw_mechanism2(btcoexist, false, false, false, 0x18);

		common = true;
	} else {
		low_pwr_disable = true;
		btcoexist->btc_set(btcoexist, BTC_SET_ACT_DISABLE_LOW_POWER,
				   &low_pwr_disable);

		if (wifi_busy) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Wifi Busy + BT Busy!!\n");
			common = false;
		} else {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Wifi LPS + BT Busy!!\n");
			ps21a_tdma(btcoexist, NORMAL_EXEC, true, 21);

			if (halbtc8821a2ant_need_to_dec_bt_pwr(btcoexist))
				halbtc8821a2ant_dec_bt_pwr(btcoexist,
							   NORMAL_EXEC, true);
			else
				halbtc8821a2ant_dec_bt_pwr(btcoexist,
							   NORMAL_EXEC, false);

			common = true;
		}
		sw_mechanism1(btcoexist, true, true, true, true);
	}
	return common;
}

static void tdma_duration_adjust(struct btc_coexist *btcoexist,
				 bool sco_hid, bool tx_pause, u8 max_interval)
{
	static long up, dn, m, n, wait_count;
	long result;
	/* 0: no change, +1: incr WiFi duration, -1: decr WiFi duration */
	u8 retry_count = 0;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW,
		  "[BTCoex], TdmaDurationAdjust()\n");

	if (coex_dm->reset_tdma_adjust) {
		coex_dm->reset_tdma_adjust = false;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], first run TdmaDurationAdjust()!!\n");
		if (sco_hid) {
			if (tx_pause) {
				if (max_interval == 1) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 13);
					coex_dm->ps_tdma_du_adj_type = 13;
				} else if (max_interval == 2) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 14);
					coex_dm->ps_tdma_du_adj_type = 14;
				} else if (max_interval == 3) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				}
			} else {
				if (max_interval == 1) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 9);
					coex_dm->ps_tdma_du_adj_type = 9;
				} else if (max_interval == 2) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 10);
					coex_dm->ps_tdma_du_adj_type = 10;
				} else if (max_interval == 3) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				}
			}
		} else {
			if (tx_pause) {
				if (max_interval == 1) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 5);
					coex_dm->ps_tdma_du_adj_type = 5;
				} else if (max_interval == 2) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 6);
					coex_dm->ps_tdma_du_adj_type = 6;
				} else if (max_interval == 3) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				}
			} else {
				if (max_interval == 1) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 1);
					coex_dm->ps_tdma_du_adj_type = 1;
				} else if (max_interval == 2) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 2);
					coex_dm->ps_tdma_du_adj_type = 2;
				} else if (max_interval == 3) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				}
			}
		}
		up = 0;
		dn = 0;
		m = 1;
		n = 3;
		result = 0;
		wait_count = 0;
	} else {
		/* accquire the BT TRx retry count from BT_Info byte2 */
		retry_count = coex_sta->bt_retry_cnt;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], retry_count = %d\n", retry_count);
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], up =%d, dn =%d, m =%d, n =%d, wait_count =%d\n",
			  (int)up, (int)dn, (int)m, (int)n, (int)wait_count);
		result = 0;
		wait_count++;

		if (retry_count == 0) {
			/*  no retry in the last 2-second duration */
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
					  "[BTCoex], Increase wifi duration!!\n");
			}
		} else if (retry_count <= 3) {
			/*  <= 3 retry in the last 2-second duration */
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

				n = 3*m;
				up = 0;
				dn = 0;
				wait_count = 0;
				result = -1;
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_TRACE_FW_DETAIL,
					  "[BTCoex], Decrease wifi duration for retryCounter<3!!\n");
			}
		} else {
			if (wait_count == 1)
				m++;
			else
				m = 1;

			if (m >= 20)
				m = 20;

			n = 3*m;
			up = 0;
			dn = 0;
			wait_count = 0;
			result = -1;
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
				  "[BTCoex], Decrease wifi duration for retryCounter>3!!\n");
		}

		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], max Interval = %d\n", max_interval);
		if (max_interval == 1) {
			if (tx_pause) {
				/* TODO: refactor here */
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_TRACE_FW_DETAIL,
					  "[BTCoex], TxPause = 1\n");
				if (coex_dm->cur_ps_tdma == 71) {
					ps21a_tdma(btcoexist,
								NORMAL_EXEC,
								true, 5);
					coex_dm->ps_tdma_du_adj_type = 5;
				} else if (coex_dm->cur_ps_tdma == 1) {
					ps21a_tdma(btcoexist,
								NORMAL_EXEC,
								true, 5);
					coex_dm->ps_tdma_du_adj_type = 5;
				} else if (coex_dm->cur_ps_tdma == 2) {
					ps21a_tdma(btcoexist,
								NORMAL_EXEC,
								true, 6);
					coex_dm->ps_tdma_du_adj_type = 6;
				} else if (coex_dm->cur_ps_tdma == 3) {
					ps21a_tdma(btcoexist,
								NORMAL_EXEC,
								true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 4) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 8);
					coex_dm->ps_tdma_du_adj_type = 8;
				}
				if (coex_dm->cur_ps_tdma == 9) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 13);
					coex_dm->ps_tdma_du_adj_type = 13;
				} else if (coex_dm->cur_ps_tdma == 10) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 14);
					coex_dm->ps_tdma_du_adj_type = 14;
				} else if (coex_dm->cur_ps_tdma == 11) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 12) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 16);
					coex_dm->ps_tdma_du_adj_type = 16;
				}

				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 5) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 6);
						coex_dm->ps_tdma_du_adj_type = 6;
					} else if (coex_dm->cur_ps_tdma == 6) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 7);
						coex_dm->ps_tdma_du_adj_type = 7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 8);
						coex_dm->ps_tdma_du_adj_type = 8;
					} else if (coex_dm->cur_ps_tdma == 13) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 14);
						coex_dm->ps_tdma_du_adj_type = 14;
					} else if (coex_dm->cur_ps_tdma == 14) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 15);
						coex_dm->ps_tdma_du_adj_type = 15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 16);
						coex_dm->ps_tdma_du_adj_type = 16;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 8) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 7);
						coex_dm->ps_tdma_du_adj_type = 7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 6);
						coex_dm->ps_tdma_du_adj_type = 6;
					} else if (coex_dm->cur_ps_tdma == 6) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 5);
						coex_dm->ps_tdma_du_adj_type = 5;
					} else if (coex_dm->cur_ps_tdma == 16) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 15);
						coex_dm->ps_tdma_du_adj_type = 15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 14);
						coex_dm->ps_tdma_du_adj_type = 14;
					} else if (coex_dm->cur_ps_tdma == 14) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 13);
						coex_dm->ps_tdma_du_adj_type = 13;
					}
				}
			} else {
				/* TODO: refactor here */
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, "[BTCoex], TxPause = 0\n");
				if (coex_dm->cur_ps_tdma == 5) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 71);
					coex_dm->ps_tdma_du_adj_type = 71;
				} else if (coex_dm->cur_ps_tdma == 6) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 2);
					coex_dm->ps_tdma_du_adj_type = 2;
				} else if (coex_dm->cur_ps_tdma == 7) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 8) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 4);
					coex_dm->ps_tdma_du_adj_type = 4;
				}
				if (coex_dm->cur_ps_tdma == 13) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 9);
					coex_dm->ps_tdma_du_adj_type = 9;
				} else if (coex_dm->cur_ps_tdma == 14) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 10);
					coex_dm->ps_tdma_du_adj_type = 10;
				} else if (coex_dm->cur_ps_tdma == 15) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 16) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 12);
					coex_dm->ps_tdma_du_adj_type = 12;
				}

				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 71) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 1);
						coex_dm->ps_tdma_du_adj_type = 1;
					} else if (coex_dm->cur_ps_tdma == 1) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 2);
						coex_dm->ps_tdma_du_adj_type = 2;
					} else if (coex_dm->cur_ps_tdma == 2) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 3);
						coex_dm->ps_tdma_du_adj_type = 3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 4);
						coex_dm->ps_tdma_du_adj_type = 4;
					} else if (coex_dm->cur_ps_tdma == 9) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 10);
						coex_dm->ps_tdma_du_adj_type = 10;
					} else if (coex_dm->cur_ps_tdma == 10) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 11);
						coex_dm->ps_tdma_du_adj_type = 11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 12);
						coex_dm->ps_tdma_du_adj_type = 12;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 4) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 3);
						coex_dm->ps_tdma_du_adj_type = 3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 2);
						coex_dm->ps_tdma_du_adj_type = 2;
					} else if (coex_dm->cur_ps_tdma == 2) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 1);
						coex_dm->ps_tdma_du_adj_type = 1;
					} else if (coex_dm->cur_ps_tdma == 1) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 71);
						coex_dm->ps_tdma_du_adj_type = 71;
					} else if (coex_dm->cur_ps_tdma == 12) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 11);
						coex_dm->ps_tdma_du_adj_type = 11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 10);
						coex_dm->ps_tdma_du_adj_type = 10;
					} else if (coex_dm->cur_ps_tdma == 10) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 9);
						coex_dm->ps_tdma_du_adj_type = 9;
					}
				}
			}
		} else if (max_interval == 2) {
			if (tx_pause) {
				/* TODO: refactor here */
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, "[BTCoex], TxPause = 1\n");
				if (coex_dm->cur_ps_tdma == 1) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 6);
					coex_dm->ps_tdma_du_adj_type = 6;
				} else if (coex_dm->cur_ps_tdma == 2) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 6);
					coex_dm->ps_tdma_du_adj_type = 6;
				} else if (coex_dm->cur_ps_tdma == 3) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 4) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 8);
					coex_dm->ps_tdma_du_adj_type = 8;
				}
				if (coex_dm->cur_ps_tdma == 9) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 14);
					coex_dm->ps_tdma_du_adj_type = 14;
				} else if (coex_dm->cur_ps_tdma == 10) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 14);
					coex_dm->ps_tdma_du_adj_type = 14;
				} else if (coex_dm->cur_ps_tdma == 11) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 12) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 16);
					coex_dm->ps_tdma_du_adj_type = 16;
				}
				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 5) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 6);
						coex_dm->ps_tdma_du_adj_type = 6;
					} else if (coex_dm->cur_ps_tdma == 6) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 7);
						coex_dm->ps_tdma_du_adj_type = 7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 8);
						coex_dm->ps_tdma_du_adj_type = 8;
					} else if (coex_dm->cur_ps_tdma == 13) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 14);
						coex_dm->ps_tdma_du_adj_type = 14;
					} else if (coex_dm->cur_ps_tdma == 14) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 15);
						coex_dm->ps_tdma_du_adj_type = 15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 16);
						coex_dm->ps_tdma_du_adj_type = 16;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 8) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 7);
						coex_dm->ps_tdma_du_adj_type = 7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 6);
						coex_dm->ps_tdma_du_adj_type = 6;
					} else if (coex_dm->cur_ps_tdma == 6) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 6);
						coex_dm->ps_tdma_du_adj_type = 6;
					} else if (coex_dm->cur_ps_tdma == 16) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 15);
						coex_dm->ps_tdma_du_adj_type = 15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 14);
						coex_dm->ps_tdma_du_adj_type = 14;
					} else if (coex_dm->cur_ps_tdma == 14) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 14);
						coex_dm->ps_tdma_du_adj_type = 14;
					}
				}
			} else {
				/* TODO: refactor here */
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
					  "[BTCoex], TxPause = 0\n");
				if (coex_dm->cur_ps_tdma == 5) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 2);
					coex_dm->ps_tdma_du_adj_type = 2;
				} else if (coex_dm->cur_ps_tdma == 6) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 2);
					coex_dm->ps_tdma_du_adj_type = 2;
				} else if (coex_dm->cur_ps_tdma == 7) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 8) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 4);
					coex_dm->ps_tdma_du_adj_type = 4;
				}
				if (coex_dm->cur_ps_tdma == 13) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 10);
					coex_dm->ps_tdma_du_adj_type = 10;
				} else if (coex_dm->cur_ps_tdma == 14) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 10);
					coex_dm->ps_tdma_du_adj_type = 10;
				} else if (coex_dm->cur_ps_tdma == 15) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 16) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 12);
					coex_dm->ps_tdma_du_adj_type = 12;
				}
				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 1) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 2);
						coex_dm->ps_tdma_du_adj_type = 2;
					} else if (coex_dm->cur_ps_tdma == 2) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 3);
						coex_dm->ps_tdma_du_adj_type = 3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 4);
						coex_dm->ps_tdma_du_adj_type = 4;
					} else if (coex_dm->cur_ps_tdma == 9) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 10);
						coex_dm->ps_tdma_du_adj_type = 10;
					} else if (coex_dm->cur_ps_tdma == 10) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 11);
						coex_dm->ps_tdma_du_adj_type = 11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 12);
						coex_dm->ps_tdma_du_adj_type = 12;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 4) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 3);
						coex_dm->ps_tdma_du_adj_type = 3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 2);
						coex_dm->ps_tdma_du_adj_type = 2;
					} else if (coex_dm->cur_ps_tdma == 2) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 2);
						coex_dm->ps_tdma_du_adj_type = 2;
					} else if (coex_dm->cur_ps_tdma == 12) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 11);
						coex_dm->ps_tdma_du_adj_type = 11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 10);
						coex_dm->ps_tdma_du_adj_type = 10;
					} else if (coex_dm->cur_ps_tdma == 10) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 10);
						coex_dm->ps_tdma_du_adj_type = 10;
					}
				}
			}
		} else if (max_interval == 3) {
			if (tx_pause) {
				/* TODO: refactor here */
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
					  "[BTCoex], TxPause = 1\n");
				if (coex_dm->cur_ps_tdma == 1) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 2) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 3) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 7);
					coex_dm->ps_tdma_du_adj_type = 7;
				} else if (coex_dm->cur_ps_tdma == 4) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 8);
					coex_dm->ps_tdma_du_adj_type = 8;
				}
				if (coex_dm->cur_ps_tdma == 9) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 10) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 11) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 15);
					coex_dm->ps_tdma_du_adj_type = 15;
				} else if (coex_dm->cur_ps_tdma == 12) {
					ps21a_tdma(btcoexist, NORMAL_EXEC, true, 16);
					coex_dm->ps_tdma_du_adj_type = 16;
				}
				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 5) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 7);
						coex_dm->ps_tdma_du_adj_type = 7;
					} else if (coex_dm->cur_ps_tdma == 6) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 7);
						coex_dm->ps_tdma_du_adj_type = 7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 8);
						coex_dm->ps_tdma_du_adj_type = 8;
					} else if (coex_dm->cur_ps_tdma == 13) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 15);
						coex_dm->ps_tdma_du_adj_type = 15;
					} else if (coex_dm->cur_ps_tdma == 14) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 15);
						coex_dm->ps_tdma_du_adj_type = 15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 16);
						coex_dm->ps_tdma_du_adj_type = 16;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 8) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 7);
						coex_dm->ps_tdma_du_adj_type = 7;
					} else if (coex_dm->cur_ps_tdma == 7) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 7);
						coex_dm->ps_tdma_du_adj_type = 7;
					} else if (coex_dm->cur_ps_tdma == 6) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 7);
						coex_dm->ps_tdma_du_adj_type = 7;
					} else if (coex_dm->cur_ps_tdma == 16) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 15);
						coex_dm->ps_tdma_du_adj_type = 15;
					} else if (coex_dm->cur_ps_tdma == 15) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 15);
						coex_dm->ps_tdma_du_adj_type = 15;
					} else if (coex_dm->cur_ps_tdma == 14) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 15);
						coex_dm->ps_tdma_du_adj_type = 15;
					}
				}
			} else {
				BTC_PRINT(BTC_MSG_ALGORITHM,
					  ALGO_TRACE_FW_DETAIL,
					  "[BTCoex], TxPause = 0\n");
				if (coex_dm->cur_ps_tdma == 5) {
					ps21a_tdma(btcoexist, NORMAL_EXEC,
						   true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 6) {
					ps21a_tdma(btcoexist, NORMAL_EXEC,
						   true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 7) {
					ps21a_tdma(btcoexist, NORMAL_EXEC,
						   true, 3);
					coex_dm->ps_tdma_du_adj_type = 3;
				} else if (coex_dm->cur_ps_tdma == 8) {
					ps21a_tdma(btcoexist, NORMAL_EXEC,
						   true, 4);
					coex_dm->ps_tdma_du_adj_type = 4;
				}
				if (coex_dm->cur_ps_tdma == 13) {
					ps21a_tdma(btcoexist, NORMAL_EXEC,
						   true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 14) {
					ps21a_tdma(btcoexist, NORMAL_EXEC,
						   true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 15) {
					ps21a_tdma(btcoexist, NORMAL_EXEC,
						   true, 11);
					coex_dm->ps_tdma_du_adj_type = 11;
				} else if (coex_dm->cur_ps_tdma == 16) {
					ps21a_tdma(btcoexist, NORMAL_EXEC,
						   true, 12);
					coex_dm->ps_tdma_du_adj_type = 12;
				}
				if (result == -1) {
					if (coex_dm->cur_ps_tdma == 1) {
						ps21a_tdma(btcoexist,
							   NORMAL_EXEC,
							   true, 3);
						coex_dm->ps_tdma_du_adj_type = 3;
					} else if (coex_dm->cur_ps_tdma == 2) {
						ps21a_tdma(btcoexist,
							   NORMAL_EXEC,
							   true, 3);
						coex_dm->ps_tdma_du_adj_type = 3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						ps21a_tdma(btcoexist,
							   NORMAL_EXEC,
							   true, 4);
						coex_dm->ps_tdma_du_adj_type = 4;
					} else if (coex_dm->cur_ps_tdma == 9) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 11);
						coex_dm->ps_tdma_du_adj_type = 11;
					} else if (coex_dm->cur_ps_tdma == 10) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 11);
						coex_dm->ps_tdma_du_adj_type = 11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 12);
						coex_dm->ps_tdma_du_adj_type = 12;
					}
				} else if (result == 1) {
					if (coex_dm->cur_ps_tdma == 4) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 3);
						coex_dm->ps_tdma_du_adj_type = 3;
					} else if (coex_dm->cur_ps_tdma == 3) {
						ps21a_tdma(btcoexist, NORMAL_EXEC, true, 3);
						coex_dm->ps_tdma_du_adj_type = 3;
					} else if (coex_dm->cur_ps_tdma == 2) {
						ps21a_tdma(btcoexist,
							   NORMAL_EXEC, true, 3);
						coex_dm->ps_tdma_du_adj_type = 3;
					} else if (coex_dm->cur_ps_tdma == 12) {
						ps21a_tdma(btcoexist, NORMAL_EXEC,
							   true, 11);
						coex_dm->ps_tdma_du_adj_type = 11;
					} else if (coex_dm->cur_ps_tdma == 11) {
						ps21a_tdma(btcoexist,
							   NORMAL_EXEC, true, 11);
						coex_dm->ps_tdma_du_adj_type = 11;
					} else if (coex_dm->cur_ps_tdma == 10) {
						ps21a_tdma(btcoexist,
							   NORMAL_EXEC, true, 11);
						coex_dm->ps_tdma_du_adj_type = 11;
					}
				}
			}
		}
	}

	/*  if current PsTdma not match with the recorded one
	 * (when scan, dhcp...),
	 *  then we have to adjust it back to the previous record one. */
	if (coex_dm->cur_ps_tdma != coex_dm->ps_tdma_du_adj_type) {
		bool scan = false, link = false, roam = false;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
			  "[BTCoex], PsTdma type dismatch!!!, cur_ps_tdma =%d, recordPsTdma =%d\n",
			  coex_dm->cur_ps_tdma, coex_dm->ps_tdma_du_adj_type);

		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
		btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);

		if (!scan && !link && !roam)
			ps21a_tdma(btcoexist, NORMAL_EXEC, true,
				   coex_dm->ps_tdma_du_adj_type);
		else
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL,
				  "[BTCoex], roaming/link/scan is under progress, will adjust next time!!!\n");
	}

	/* when tdma_duration_adjust() is called, fw dac swing is
	 * included in the function. */
	halbtc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 0x6);
}

/*  SCO only or SCO+PAN(HS) */
static void halbtc8821a2ant_action_sco(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = wifi21a_rssi_state(btcoexist, 0, 2, 15, 0);
	bt_rssi_state = halbtc8821a2ant_bt_rssi_state(2, 35, 0);

	halbtc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 4);

	if (halbtc8821a2ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_LEGACY == wifi_bw) /* for SCO quality at 11b/g mode */
		halbtc8821a2ant_coex_table(btcoexist, NORMAL_EXEC, 0x5a5a5a5a,
					   0x5a5a5a5a, 0xffff, 0x3);
	else  /* for SCO quality & wifi performance balance at 11n mode */
		halbtc8821a2ant_coex_table(btcoexist, NORMAL_EXEC, 0x5aea5aea,
					   0x5aea5aea, 0xffff, 0x3);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/*  fw mechanism */
		/* ps21a_tdma(btcoexist, NORMAL_EXEC, true, 5); */

		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			ps21a_tdma(btcoexist, NORMAL_EXEC,
				   false, 0); /* for voice qual */
		else
			ps21a_tdma(btcoexist, NORMAL_EXEC,
				   false, 0); /* for voice qual */

		/*  sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			sw_mechanism1(btcoexist, true, true, false, false);
			sw_mechanism2(btcoexist, true, false, false, 0x18);
		} else {
			sw_mechanism1(btcoexist, true, true, false, false);
			sw_mechanism2(btcoexist, false, false, false, 0x18);
		}
	} else {
		/*  fw mechanism */

		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			ps21a_tdma(btcoexist, NORMAL_EXEC,
				   false, 0); /* for voice qual */
		else
			ps21a_tdma(btcoexist, NORMAL_EXEC,
				   false, 0); /* for voice qual */

		/*  sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			sw_mechanism1(btcoexist, false, true, false, false);
			sw_mechanism2(btcoexist, true, false, false, 0x18);
		} else {
			sw_mechanism1(btcoexist, false, true, false, false);
			sw_mechanism2(btcoexist, false, false, false, 0x18);
		}
	}
}

static void halbtc8821a2ant_action_hid(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = wifi21a_rssi_state(btcoexist, 0, 2, 15, 0);
	bt_rssi_state = halbtc8821a2ant_bt_rssi_state(2, 35, 0);

	halbtc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (halbtc8821a2ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_LEGACY == wifi_bw) /* for HID at 11b/g mode */
		halbtc8821a2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55ff55ff,
					   0x5a5a5a5a, 0xffff, 0x3);
	else  /* for HID quality & wifi performance balance at 11n mode */
		halbtc8821a2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55ff55ff,
					   0x5aea5aea, 0xffff, 0x3);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/*  fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			ps21a_tdma(btcoexist, NORMAL_EXEC, true, 9);
		else
			ps21a_tdma(btcoexist, NORMAL_EXEC, true, 13);
		/*  sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			sw_mechanism1(btcoexist, true, true, false, false);
			sw_mechanism2(btcoexist, true, false, false, 0x18);
		} else {
			sw_mechanism1(btcoexist, true, true, false, false);
			sw_mechanism2(btcoexist, false, false, false, 0x18);
		}
	} else {
		/*  fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			ps21a_tdma(btcoexist, NORMAL_EXEC, true, 9);
		else
			ps21a_tdma(btcoexist, NORMAL_EXEC, true, 13);

		/*  sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			sw_mechanism1(btcoexist, false, true, false, false);
			sw_mechanism2(btcoexist, true, false, false, 0x18);
		} else {
			sw_mechanism1(btcoexist, false, true, false, false);
			sw_mechanism2(btcoexist, false, false, false, 0x18);
		}
	}
}

/* A2DP only / PAN(EDR) only/ A2DP+PAN(HS) */
static void halbtc8821a2ant_action_a2dp(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = wifi21a_rssi_state(btcoexist, 0, 2, 15, 0);
	bt_rssi_state = halbtc8821a2ant_bt_rssi_state(2, 35, 0);

	/* fw dac swing is called in tdma_duration_adjust() */
	/* halbtc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6); */

	if (halbtc8821a2ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/*  fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			tdma_duration_adjust(btcoexist, false, false, 1);
		else
			tdma_duration_adjust(btcoexist, false, true, 1);

		/*  sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			sw_mechanism1(btcoexist, true, false, false, false);
			sw_mechanism2(btcoexist, true, false, false, 0x18);
		} else {
			sw_mechanism1(btcoexist, true, false, false, false);
			sw_mechanism2(btcoexist, false, false, false, 0x18);
		}
	} else {
		/*  fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			tdma_duration_adjust(btcoexist, false, false, 1);
		else
			tdma_duration_adjust(btcoexist, false, true, 1);

		/*  sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			sw_mechanism1(btcoexist, false, false, false, false);
			sw_mechanism2(btcoexist, true, false, false, 0x18);
		} else {
			sw_mechanism1(btcoexist, false, false, false, false);
			sw_mechanism2(btcoexist, false, false, false, 0x18);
		}
	}
}

static void halbtc8821a2ant_action_a2dp_pan_hs(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state, bt_info_ext;
	u32 wifi_bw;

	bt_info_ext = coex_sta->bt_info_ext;
	wifi_rssi_state = wifi21a_rssi_state(btcoexist, 0, 2, 15, 0);
	bt_rssi_state = halbtc8821a2ant_bt_rssi_state(2, 35, 0);

	/* fw dac swing is called in tdma_duration_adjust() */
	/* halbtc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6); */

	if (halbtc8821a2ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/*  fw mechanism */
		if (bt_info_ext & BIT(0))	/* a2dp basic rate */
			tdma_duration_adjust(btcoexist, false, true, 2);
		else				/* a2dp edr rate */
			tdma_duration_adjust(btcoexist, false, true, 1);

		/*  sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			sw_mechanism1(btcoexist, true, false, false, false);
			sw_mechanism2(btcoexist, true, false, false, 0x18);
		} else {
			sw_mechanism1(btcoexist, true, false, false, false);
			sw_mechanism2(btcoexist, false, false, false, 0x18);
		}
	} else {
		/*  fw mechanism */
		if (bt_info_ext & BIT(0))	/* a2dp basic rate */
			tdma_duration_adjust(btcoexist, false, true, 2);
		else				/* a2dp edr rate */
			tdma_duration_adjust(btcoexist, false, true, 1);

		/*  sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			sw_mechanism1(btcoexist, false, false, false, false);
			sw_mechanism2(btcoexist, true, false, false, 0x18);
		} else {
			sw_mechanism1(btcoexist, false, false, false, false);
			sw_mechanism2(btcoexist, false, false, false, 0x18);
		}
	}
}

static void halbtc8821a2ant_action_pan_edr(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = wifi21a_rssi_state(btcoexist, 0, 2, 15, 0);
	bt_rssi_state = halbtc8821a2ant_bt_rssi_state(2, 35, 0);

	halbtc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (halbtc8821a2ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_LEGACY == wifi_bw) /* for HID at 11b/g mode */
		halbtc8821a2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55ff55ff,
					   0x5aff5aff, 0xffff, 0x3);
	else  /* for HID quality & wifi performance balance at 11n mode */
		halbtc8821a2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55ff55ff,
					   0x5aff5aff, 0xffff, 0x3);

		if (BTC_WIFI_BW_HT40 == wifi_bw) {
			/*  fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			ps21a_tdma(btcoexist, NORMAL_EXEC, true, 1);
		else
			ps21a_tdma(btcoexist, NORMAL_EXEC, true, 5);

		/*  sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			sw_mechanism1(btcoexist, true, false, false, false);
			sw_mechanism2(btcoexist, true, false, false, 0x18);
		} else {
			sw_mechanism1(btcoexist, true, false, false, false);
			sw_mechanism2(btcoexist, false, false, false, 0x18);
		}
	} else {
		/*  fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			ps21a_tdma(btcoexist, NORMAL_EXEC, true, 1);
		else
			ps21a_tdma(btcoexist, NORMAL_EXEC, true, 5);

		/*  sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			sw_mechanism1(btcoexist, false, false, false, false);
			sw_mechanism2(btcoexist, true, false, false, 0x18);
		} else {
			sw_mechanism1(btcoexist, false, false, false, false);
			sw_mechanism2(btcoexist, false, false, false, 0x18);
		}
	}
}

/* PAN(HS) only */
static void halbtc8821a2ant_action_pan_hs(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = wifi21a_rssi_state(btcoexist, 0, 2, 15, 0);
	bt_rssi_state = halbtc8821a2ant_bt_rssi_state(2, 35, 0);

	halbtc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/*  fw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC,
						   true);
		else
			halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC,
						   false);
		ps21a_tdma(btcoexist, NORMAL_EXEC, false, 1);

		/*  sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			sw_mechanism1(btcoexist, true, false, false, false);
			sw_mechanism2(btcoexist, true, false, false, 0x18);
		} else {
			sw_mechanism1(btcoexist, true, false, false, false);
			sw_mechanism2(btcoexist, false, false, false, 0x18);
		}
	} else {
		/*  fw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC,
						   true);
		else
			halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC,
						   false);

		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			ps21a_tdma(btcoexist, NORMAL_EXEC, false, 1);
		else
			ps21a_tdma(btcoexist, NORMAL_EXEC, false, 1);

		/*  sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			sw_mechanism1(btcoexist, false, false, false, false);
			sw_mechanism2(btcoexist, true, false, false, 0x18);
		} else {
			sw_mechanism1(btcoexist, false, false, false, false);
			sw_mechanism2(btcoexist, false, false, false, 0x18);
		}
	}
}

/* PAN(EDR)+A2DP */
static void halbtc8821a2ant_action_pan_edr_a2dp(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state, bt_info_ext;
	u32 wifi_bw;

	bt_info_ext = coex_sta->bt_info_ext;
	wifi_rssi_state = wifi21a_rssi_state(btcoexist, 0, 2, 15, 0);
	bt_rssi_state = halbtc8821a2ant_bt_rssi_state(2, 35, 0);

	halbtc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (halbtc8821a2ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_LEGACY == wifi_bw) /* for HID at 11b/g mode */
		halbtc8821a2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55ff55ff,
					   0x5afa5afa, 0xffff, 0x3);
	else  /* for HID quality & wifi performance balance at 11n mode */
		halbtc8821a2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55ff55ff,
					   0x5afa5afa, 0xffff, 0x3);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/*  fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				tdma_duration_adjust(btcoexist, false,
						     false, 3);
			else				/* a2dp edr rate */
				tdma_duration_adjust(btcoexist, false,
						     false, 3);
		} else {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				tdma_duration_adjust(btcoexist, false, true, 3);
			else				/* a2dp edr rate */
				tdma_duration_adjust(btcoexist, false, true, 3);
		}

		/*  sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			sw_mechanism1(btcoexist, true, false, false, false);
			sw_mechanism2(btcoexist, true, false, false, 0x18);
		} else {
			sw_mechanism1(btcoexist, true, false, false, false);
			sw_mechanism2(btcoexist, false, false, false, 0x18);
		};
	} else {
		/*  fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				tdma_duration_adjust(btcoexist, false,
						     false, 3);
			else				/* a2dp edr rate */
				tdma_duration_adjust(btcoexist, false,
						     false, 3);
		} else {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				tdma_duration_adjust(btcoexist, false, true, 3);
			else				/* a2dp edr rate */
				tdma_duration_adjust(btcoexist, false, true, 3);
		}

		/*  sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			sw_mechanism1(btcoexist, false, false, false, false);
			sw_mechanism2(btcoexist, true, false, false, 0x18);
		} else {
			sw_mechanism1(btcoexist, false, false, false, false);
			sw_mechanism2(btcoexist, false, false, false, 0x18);
		}
	}
}

static void halbtc8821a2ant_action_pan_edr_hid(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state;
	u32 wifi_bw;

	wifi_rssi_state = wifi21a_rssi_state(btcoexist, 0, 2, 15, 0);
	bt_rssi_state = halbtc8821a2ant_bt_rssi_state(2, 35, 0);

	halbtc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (halbtc8821a2ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_LEGACY == wifi_bw) /* for HID at 11b/g mode */
		halbtc8821a2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55ff55ff,
					   0x5a5f5a5f, 0xffff, 0x3);
	else  /* for HID quality & wifi performance balance at 11n mode */
		halbtc8821a2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55ff55ff,
					   0x5a5f5a5f, 0xffff, 0x3);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		halbtc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 3);
		/*  fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			ps21a_tdma(btcoexist, NORMAL_EXEC, true, 10);
		else
			ps21a_tdma(btcoexist, NORMAL_EXEC, true, 14);

		/*  sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			sw_mechanism1(btcoexist, true, true, false, false);
			sw_mechanism2(btcoexist, true, false, false, 0x18);
		} else {
			sw_mechanism1(btcoexist, true, true, false, false);
			sw_mechanism2(btcoexist, false, false, false, 0x18);
		}
	} else {
		halbtc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);
		/*  fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH))
			ps21a_tdma(btcoexist, NORMAL_EXEC, true, 10);
		else
			ps21a_tdma(btcoexist, NORMAL_EXEC, true, 14);

		/*  sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			sw_mechanism1(btcoexist, false, true, false, false);
			sw_mechanism2(btcoexist, true, false, false, 0x18);
		} else {
			sw_mechanism1(btcoexist, false, true, false, false);
			sw_mechanism2(btcoexist, false, false, false, 0x18);
		}
	}
}

/*  HID+A2DP+PAN(EDR) */
static void action_hid_a2dp_pan_edr(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state, bt_info_ext;
	u32 wifi_bw;

	bt_info_ext = coex_sta->bt_info_ext;
	wifi_rssi_state = wifi21a_rssi_state(btcoexist, 0, 2, 15, 0);
	bt_rssi_state = halbtc8821a2ant_bt_rssi_state(2, 35, 0);

	halbtc8821a2ant_fw_dac_swing_lvl(btcoexist, NORMAL_EXEC, 6);

	if (halbtc8821a2ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_LEGACY == wifi_bw) /* for HID at 11b/g mode */
		halbtc8821a2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55ff55ff,
					   0x5a5a5a5a, 0xffff, 0x3);
	else  /* for HID quality & wifi performance balance at 11n mode */
		halbtc8821a2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55ff55ff,
					   0x5a5a5a5a, 0xffff, 0x3);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/*  fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			if (bt_info_ext & BIT(0)) {	/* a2dp basic rate */
				tdma_duration_adjust(btcoexist, true, true, 3);
			} else {
				/* a2dp edr rate */
				tdma_duration_adjust(btcoexist, true, true, 3);
			}
		} else {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				tdma_duration_adjust(btcoexist, true, true, 3);
			else				/* a2dp edr rate */
				tdma_duration_adjust(btcoexist, true, true, 3);
		}

		/*  sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			sw_mechanism1(btcoexist, true, true, false, false);
			sw_mechanism2(btcoexist, true, false, false, 0x18);
		} else {
			sw_mechanism1(btcoexist, true, true, false, false);
			sw_mechanism2(btcoexist, false, false, false, 0x18);
		}
	} else {
		/*  fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				tdma_duration_adjust(btcoexist, true, false, 3);
			else				/* a2dp edr rate */
				tdma_duration_adjust(btcoexist, true, false, 3);
		} else {
			if (bt_info_ext & BIT(0)) {
				/* a2dp basic rate */
				tdma_duration_adjust(btcoexist, true, true, 3);
			} else				/* a2dp edr rate */ {
				tdma_duration_adjust(btcoexist, true, true, 3);
			}
		}

		/*  sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			sw_mechanism1(btcoexist, false, true, false, false);
			sw_mechanism2(btcoexist, true, false, false, 0x18);
		} else {
			sw_mechanism1(btcoexist, false, true, false, false);
			sw_mechanism2(btcoexist, false, false, false, 0x18);
		}
	}
}

static void halbtc8821a2ant_action_hid_a2dp(struct btc_coexist *btcoexist)
{
	u8 wifi_rssi_state, bt_rssi_state, bt_info_ext;
	u32 wifi_bw;

	bt_info_ext = coex_sta->bt_info_ext;
	wifi_rssi_state = wifi21a_rssi_state(btcoexist, 0, 2, 15, 0);
	bt_rssi_state = halbtc8821a2ant_bt_rssi_state(2, 35, 0);

	if (halbtc8821a2ant_need_to_dec_bt_pwr(btcoexist))
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, true);
	else
		halbtc8821a2ant_dec_bt_pwr(btcoexist, NORMAL_EXEC, false);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);

	if (BTC_WIFI_BW_LEGACY == wifi_bw) /* for HID at 11b/g mode */
		halbtc8821a2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55ff55ff,
					   0x5f5b5f5b, 0xffffff, 0x3);
	else  /* for HID quality & wifi performance balance at 11n mode */
		halbtc8821a2ant_coex_table(btcoexist, NORMAL_EXEC, 0x55ff55ff,
					   0x5f5b5f5b, 0xffffff, 0x3);

	if (BTC_WIFI_BW_HT40 == wifi_bw) {
		/*  fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				tdma_duration_adjust(btcoexist, true, true, 2);
			else				/* a2dp edr rate */
				tdma_duration_adjust(btcoexist, true, true, 2);
		} else {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				tdma_duration_adjust(btcoexist, true, true, 2);
			else				/* a2dp edr rate */
				tdma_duration_adjust(btcoexist, true, true, 2);
		}

		/*  sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			sw_mechanism1(btcoexist, true, true, false, false);
			sw_mechanism2(btcoexist, true, false, false, 0x18);
		} else {
			sw_mechanism1(btcoexist, true, true, false, false);
			sw_mechanism2(btcoexist, false, false, false, 0x18);
		}
	} else {
		/*  fw mechanism */
		if ((bt_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (bt_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				tdma_duration_adjust(btcoexist, true, true, 2);
			else				/* a2dp edr rate */
				tdma_duration_adjust(btcoexist, true, true, 2);
		} else {
			if (bt_info_ext & BIT(0))	/* a2dp basic rate */
				tdma_duration_adjust(btcoexist, true, true, 2);
			else				/* a2dp edr rate */
				tdma_duration_adjust(btcoexist, true, true, 2);
		}

		/*  sw mechanism */
		if ((wifi_rssi_state == BTC_RSSI_STATE_HIGH) ||
		    (wifi_rssi_state == BTC_RSSI_STATE_STAY_HIGH)) {
			sw_mechanism1(btcoexist, false, true, false, false);
			sw_mechanism2(btcoexist, true, false, false, 0x18);
		} else {
			sw_mechanism1(btcoexist, false, true, false, false);
			sw_mechanism2(btcoexist, false, false, false, 0x18);
		}
	}
}

static void halbtc8821a2ant_run_coexist_mechanism(struct btc_coexist *btcoexist)
{
	bool wifi_under_5g = false;
	u8 algorithm = 0;

	if (btcoexist->manual_control) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], Manual control!!!\n");
		return;
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);

	if (wifi_under_5g) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], RunCoexistMechanism(), run 5G coex setting!!<===\n");
		halbtc8821a2ant_coex_under_5g(btcoexist);
		return;
	}

	algorithm = halbtc8821a2ant_action_algorithm(btcoexist);
	if (coex_sta->c2h_bt_inquiry_page &&
	    (BT_8821A_2ANT_COEX_ALGO_PANHS != algorithm)) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], BT is under inquiry/page scan !!\n");
		halbtc8821a2ant_bt_inquiry_page(btcoexist);
		return;
	}

	coex_dm->cur_algorithm = algorithm;
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
		  "[BTCoex], Algorithm = %d\n", coex_dm->cur_algorithm);

	if (halbtc8821a2ant_is_common_action(btcoexist)) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
			  "[BTCoex], Action 2-Ant common.\n");
		coex_dm->reset_tdma_adjust = true;
	} else {
		if (coex_dm->cur_algorithm != coex_dm->pre_algorithm) {
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], pre_algorithm =%d, cur_algorithm =%d\n",
				  coex_dm->pre_algorithm,
				  coex_dm->cur_algorithm);
			coex_dm->reset_tdma_adjust = true;
		}
		switch (coex_dm->cur_algorithm) {
		case BT_8821A_2ANT_COEX_ALGO_SCO:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, algorithm = SCO.\n");
			halbtc8821a2ant_action_sco(btcoexist);
			break;
		case BT_8821A_2ANT_COEX_ALGO_HID:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, algorithm = HID.\n");
			halbtc8821a2ant_action_hid(btcoexist);
			break;
		case BT_8821A_2ANT_COEX_ALGO_A2DP:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, algorithm = A2DP.\n");
			halbtc8821a2ant_action_a2dp(btcoexist);
			break;
		case BT_8821A_2ANT_COEX_ALGO_A2DP_PANHS:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, algorithm = A2DP+PAN(HS).\n");
			halbtc8821a2ant_action_a2dp_pan_hs(btcoexist);
			break;
		case BT_8821A_2ANT_COEX_ALGO_PANEDR:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, algorithm = PAN(EDR).\n");
			halbtc8821a2ant_action_pan_edr(btcoexist);
			break;
		case BT_8821A_2ANT_COEX_ALGO_PANHS:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, algorithm = HS mode.\n");
			halbtc8821a2ant_action_pan_hs(btcoexist);
			break;
		case BT_8821A_2ANT_COEX_ALGO_PANEDR_A2DP:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, algorithm = PAN+A2DP.\n");
			halbtc8821a2ant_action_pan_edr_a2dp(btcoexist);
			break;
		case BT_8821A_2ANT_COEX_ALGO_PANEDR_HID:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, algorithm = PAN(EDR)+HID.\n");
			halbtc8821a2ant_action_pan_edr_hid(btcoexist);
			break;
		case BT_8821A_2ANT_COEX_ALGO_HID_A2DP_PANEDR:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, algorithm = HID+A2DP+PAN.\n");
			action_hid_a2dp_pan_edr(btcoexist);
			break;
		case BT_8821A_2ANT_COEX_ALGO_HID_A2DP:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, algorithm = HID+A2DP.\n");
			halbtc8821a2ant_action_hid_a2dp(btcoexist);
			break;
		default:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
				  "[BTCoex], Action 2-Ant, algorithm = coexist All Off!!\n");
			halbtc8821a2ant_coex_all_off(btcoexist);
			break;
		}
		coex_dm->pre_algorithm = coex_dm->cur_algorithm;
	}
}

/*  work around function start with wa_halbtc8821a2ant_ */
/*  extern function start with EXhalbtc8821a2ant_ */
void ex_halbtc8821a2ant_init_hwconfig(struct btc_coexist *btcoexist)
{
	u8 u1tmp = 0;

	BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
		  "[BTCoex], 2Ant Init HW Config!!\n");

	/*  backup rf 0x1e value */
	coex_dm->bt_rf0x1e_backup =
		btcoexist->btc_get_rf_reg(btcoexist, BTC_RF_A, 0x1e, 0xfffff);

	/*  0x790[5:0] = 0x5 */
	u1tmp = btcoexist->btc_read_1byte(btcoexist, 0x790);
	u1tmp &= 0xc0;
	u1tmp |= 0x5;
	btcoexist->btc_write_1byte(btcoexist, 0x790, u1tmp);

	/* Antenna config */
	halbtc8821a2ant_set_ant_path(btcoexist, BTC_ANT_WIFI_AT_MAIN,
				     true, false);

	/*  PTA parameter */
	halbtc8821a2ant_coex_table(btcoexist, FORCE_EXEC,
				   0x55555555, 0x55555555,
				   0xffff, 0x3);

	/*  Enable counter statistics */
	/* 0x76e[3] = 1, WLAN_Act control by PTA */
	btcoexist->btc_write_1byte(btcoexist, 0x76e, 0xc);
	btcoexist->btc_write_1byte(btcoexist, 0x778, 0x3);
	btcoexist->btc_write_1byte_bitmask(btcoexist, 0x40, 0x20, 0x1);
}

void ex_halbtc8821a2ant_init_coex_dm(struct btc_coexist *btcoexist)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
		  "[BTCoex], Coex Mechanism Init!!\n");

	halbtc8821a2ant_init_coex_dm(btcoexist);
}

void ex_halbtc8821a2ant_display_coex_info(struct btc_coexist *btcoexist)
{
	struct btc_board_info *board_info = &btcoexist->board_info;
	struct btc_stack_info *stack_info = &btcoexist->stack_info;
	u8 *cli_buf = btcoexist->cli_buf;
	u8 u1tmp[4], i, bt_info_ext, ps_tdma_case = 0;
	u32 u4tmp[4];
	bool roam = false, scan = false, link = false, wifi_under_5g = false;
	bool bt_hs_on = false, wifi_busy = false;
	long wifi_rssi = 0, bt_hs_rssi = 0;
	u32 wifi_bw, wifi_traffic_dir;
	u8 wifi_dot_11_chnl, wifi_hs_chnl;
	u32 fw_ver = 0, bt_patch_ver = 0;

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\n ============[BT Coexist info] ============");
	CL_PRINTF(cli_buf);

	if (!board_info->bt_exist) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n BT not exists !!!");
		CL_PRINTF(cli_buf);
		return;
	}

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = %d/ %d ",
		   "Ant PG number/ Ant mechanism: ",
		   board_info->pg_ant_num, board_info->btdm_ant_num);
	CL_PRINTF(cli_buf);

	if (btcoexist->manual_control) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s",
			   "[Action Manual control]!!");
		CL_PRINTF(cli_buf);
	}

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = %s / %d",
		   "BT stack/ hci ext ver",
		   ((stack_info->profile_notified) ? "Yes" : "No"),
		   stack_info->hci_version);
	CL_PRINTF(cli_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER, &bt_patch_ver);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = %d_%d/ 0x%x/ 0x%x(%d)",
		   "CoexVer/ FwVer/ PatchVer",
		   glcoex_ver_date_8821a_2ant, glcoex_ver_8821a_2ant,
		   fw_ver, bt_patch_ver, bt_patch_ver);
	CL_PRINTF(cli_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_DOT11_CHNL,
			   &wifi_dot_11_chnl);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_HS_CHNL, &wifi_hs_chnl);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = %d / %d(%d)",
		   "Dot11 channel / HsMode(HsChnl)",
		   wifi_dot_11_chnl, bt_hs_on, wifi_hs_chnl);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\n %-35s = %02x %02x %02x ", "H2C Wifi inform bt chnl Info",
		   coex_dm->wifi_chnl_info[0], coex_dm->wifi_chnl_info[1],
		   coex_dm->wifi_chnl_info[2]);
	CL_PRINTF(cli_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_S4_WIFI_RSSI, &wifi_rssi);
	btcoexist->btc_get(btcoexist, BTC_GET_S4_HS_RSSI, &bt_hs_rssi);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\n %-35s = %ld/ %ld", "Wifi rssi/ HS rssi",
		   wifi_rssi, bt_hs_rssi);
	CL_PRINTF(cli_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = %d/ %d/ %d ",
		   "Wifi link/ roam/ scan",
		   link, roam, scan);
	CL_PRINTF(cli_buf);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION,
			   &wifi_traffic_dir);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = %s / %s/ %s ",
		   "Wifi status",
		   (wifi_under_5g ? "5G" : "2.4G"),
		   ((BTC_WIFI_BW_LEGACY == wifi_bw) ? "Legacy" :
		    (((BTC_WIFI_BW_HT40 == wifi_bw) ? "HT40" : "HT20"))),
		   ((!wifi_busy) ? "idle" :
		    ((BTC_WIFI_TRAFFIC_TX == wifi_traffic_dir) ? "uplink" :
		    "downlink")));
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = [%s/ %d/ %d] ",
		   "BT [status/ rssi/ retryCnt]",
		   ((coex_sta->c2h_bt_inquiry_page) ? ("inquiry/page scan") :
		   ((BT_8821A_2ANT_BT_STATUS_IDLE == coex_dm->bt_status) ?
		    "idle" : ((BT_8821A_2ANT_BT_STATUS_CONNECTED_IDLE ==
		    coex_dm->bt_status) ? "connected-idle" : "busy"))),
		coex_sta->bt_rssi, coex_sta->bt_retry_cnt);
	CL_PRINTF(cli_buf);

	if (stack_info->profile_notified) {
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\n %-35s = %d / %d / %d / %d", "SCO/HID/PAN/A2DP",
			   stack_info->sco_exist, stack_info->hid_exist,
			   stack_info->pan_exist, stack_info->a2dp_exist);
		CL_PRINTF(cli_buf);

		btcoexist->btc_disp_dbg_msg(btcoexist,
					    BTC_DBG_DISP_BT_LINK_INFO);
	}

	bt_info_ext = coex_sta->bt_info_ext;
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = %s",
		   "BT Info A2DP rate",
		   (bt_info_ext & BIT(0)) ? "Basic rate" : "EDR rate");
	CL_PRINTF(cli_buf);

	for (i = 0; i < BT_INFO_SRC_8821A_2ANT_MAX; i++) {
		if (coex_sta->bt_info_c2h_cnt[i]) {
			CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
				   "\n %-35s = %02x %02x %02x %02x %02x %02x %02x(%d)",
				   glbt_info_src_8821a_2ant[i],
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

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = %s/%s",
		   "PS state, IPS/LPS",
		   ((coex_sta->under_ips ? "IPS ON" : "IPS OFF")),
		   ((coex_sta->under_lps ? "LPS ON" : "LPS OFF")));
	CL_PRINTF(cli_buf);
	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_FW_PWR_MODE_CMD);

	/*  Sw mechanism	 */
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s",
		   "============[Sw mechanism] ============");
	CL_PRINTF(cli_buf);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = %d/ %d/ %d/ %d ",
		   "SM1[ShRf/ LpRA/ LimDig/ btLna]",
		   coex_dm->cur_rf_rx_lpf_shrink, coex_dm->cur_low_penalty_ra,
		   coex_dm->limited_dig, coex_dm->cur_bt_lna_constrain);
	CL_PRINTF(cli_buf);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = %d/ %d/ %d(0x%x) ",
		   "SM2[AgcT/ AdcB/ SwDacSwing(lvl)]",
		   coex_dm->cur_agc_table_en, coex_dm->cur_adc_back_off,
		   coex_dm->cur_dac_swing_on, coex_dm->cur_dac_swing_lvl);
	CL_PRINTF(cli_buf);

	/*  Fw mechanism		 */
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s",
		   "============[Fw mechanism] ============");
	CL_PRINTF(cli_buf);

	if (!btcoexist->manual_control) {
		ps_tdma_case = coex_dm->cur_ps_tdma;
		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
			   "\n %-35s = %02x %02x %02x %02x %02x case-%d",
			   "PS TDMA",
			   coex_dm->ps_tdma_para[0], coex_dm->ps_tdma_para[1],
			   coex_dm->ps_tdma_para[2], coex_dm->ps_tdma_para[3],
			   coex_dm->ps_tdma_para[4], ps_tdma_case);
		CL_PRINTF(cli_buf);

		CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = %d/ %d ",
			   "DecBtPwr/ IgnWlanAct",
			   coex_dm->cur_dec_bt_pwr,
			   coex_dm->cur_ignore_wlan_act);
		CL_PRINTF(cli_buf);
	}

	/*  Hw setting		 */
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\n %-35s", "============[Hw setting] ============");
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = 0x%x",
		   "RF-A, 0x1e initVal",
		   coex_dm->bt_rf0x1e_backup);
	CL_PRINTF(cli_buf);

	u1tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x778);
	u1tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0x6cc);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = 0x%x/ 0x%x ",
		   "0x778 (W_Act)/ 0x6cc (CoTab Sel)",
		   u1tmp[0], u1tmp[1]);
	CL_PRINTF(cli_buf);

	u1tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x8db);
	u1tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0xc5b);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = 0x%x/ 0x%x",
		   "0x8db(ADC)/0xc5b[29:25](DAC)",
		   ((u1tmp[0]&0x60)>>5), ((u1tmp[1]&0x3e)>>1));
	CL_PRINTF(cli_buf);

	u4tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xcb4);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = 0x%x/ 0x%x",
		   "0xcb4[7:0](ctrl)/ 0xcb4[29:28](val)",
		   u4tmp[0]&0xff, ((u4tmp[0]&0x30000000)>>28));
	CL_PRINTF(cli_buf);

	u1tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x40);
	u4tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x4c);
	u4tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x974);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x40/ 0x4c[24:23]/ 0x974",
		   u1tmp[0], ((u4tmp[0]&0x01800000)>>23), u4tmp[1]);
	CL_PRINTF(cli_buf);

	u4tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x550);
	u1tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x522);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = 0x%x/ 0x%x",
		   "0x550(bcn ctrl)/0x522",
		   u4tmp[0], u1tmp[0]);
	CL_PRINTF(cli_buf);

	u4tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xc50);
	u1tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0xa0a);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = 0x%x/ 0x%x",
		   "0xc50(DIG)/0xa0a(CCK-TH)",
		   u4tmp[0], u1tmp[0]);
	CL_PRINTF(cli_buf);

	u4tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0xf48);
	u1tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0xa5b);
	u1tmp[1] = btcoexist->btc_read_1byte(btcoexist, 0xa5c);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE,
		   "\n %-35s = 0x%x/ 0x%x", "OFDM-FA/ CCK-FA",
		   u4tmp[0], (u1tmp[0]<<8) + u1tmp[1]);
	CL_PRINTF(cli_buf);

	u4tmp[0] = btcoexist->btc_read_4byte(btcoexist, 0x6c0);
	u4tmp[1] = btcoexist->btc_read_4byte(btcoexist, 0x6c4);
	u4tmp[2] = btcoexist->btc_read_4byte(btcoexist, 0x6c8);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = 0x%x/ 0x%x/ 0x%x",
		   "0x6c0/0x6c4/0x6c8",
		   u4tmp[0], u4tmp[1], u4tmp[2]);
	CL_PRINTF(cli_buf);

	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = %d/ %d",
		   "0x770 (hi-pri Rx/Tx)",
		   coex_sta->high_priority_rx, coex_sta->high_priority_tx);
	CL_PRINTF(cli_buf);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = %d/ %d",
		   "0x774(low-pri Rx/Tx)",
		   coex_sta->low_priority_rx, coex_sta->low_priority_tx);
	CL_PRINTF(cli_buf);

	/*  Tx mgnt queue hang or not, 0x41b should = 0xf, ex: 0xd ==>hang */
	u1tmp[0] = btcoexist->btc_read_1byte(btcoexist, 0x41b);
	CL_SPRINTF(cli_buf, BT_TMP_BUF_SIZE, "\n %-35s = 0x%x",
		   "0x41b (mgntQ hang chk == 0xf)",
		   u1tmp[0]);
	CL_PRINTF(cli_buf);

	btcoexist->btc_disp_dbg_msg(btcoexist, BTC_DBG_DISP_COEX_STATISTICS);
}

void ex_halbtc8821a2ant_ips_notify(struct btc_coexist *btcoexist, u8 type)
{
	if (BTC_IPS_ENTER == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], IPS ENTER notify\n");
		coex_sta->under_ips = true;
		halbtc8821a2ant_coex_all_off(btcoexist);
	} else if (BTC_IPS_LEAVE == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], IPS LEAVE notify\n");
		coex_sta->under_ips = false;
	}
}

void ex_halbtc8821a2ant_lps_notify(struct btc_coexist *btcoexist, u8 type)
{
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

void ex_halbtc8821a2ant_scan_notify(struct btc_coexist *btcoexist, u8 type)
{
	if (BTC_SCAN_START == type)
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], SCAN START notify\n");
	else if (BTC_SCAN_FINISH == type)
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], SCAN FINISH notify\n");
}

void ex_halbtc8821a2ant_connect_notify(struct btc_coexist *btcoexist, u8 type)
{
	if (BTC_ASSOCIATE_START == type)
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], CONNECT START notify\n");
	else if (BTC_ASSOCIATE_FINISH == type)
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], CONNECT FINISH notify\n");
}

void ex_halbtc8821a2ant_media_status_notify(struct btc_coexist *btcoexist,
					    u8 type)
{
	u8 h2c_parameter[3] = {0};
	u32 wifi_bw;
	u8 wifi_central_chnl;

	if (BTC_MEDIA_CONNECT == type)
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], MEDIA connect notify\n");
	else
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], MEDIA disconnect notify\n");

	/*  only 2.4G we need to inform bt the chnl mask */
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

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC,
		  "[BTCoex], FW write 0x66 = 0x%x\n",
		  h2c_parameter[0] << 16 |
		  h2c_parameter[1] << 8 | h2c_parameter[2]);

	btcoexist->btc_fill_h2c(btcoexist, 0x66, 3, h2c_parameter);
}

void ex_halbtc8821a2ant_special_packet_notify(struct btc_coexist *btcoexist,
					      u8 type)
{
	if (type == BTC_PACKET_DHCP)
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
			  "[BTCoex], DHCP Packet notify\n");
}

void ex_halbtc8821a2ant_bt_info_notify(struct btc_coexist *btcoexist,
				       u8 *tmp_buf, u8 length)
{
	u8 bt_info = 0;
	u8 i, rsp_source = 0;
	static u32 set_bt_lna_cnt, set_bt_psd_mode;
	bool bt_busy = false, limited_dig = false;
	bool wifi_connected = false, bt_hs_on = false;

	coex_sta->c2h_bt_info_req_sent = false;
	rsp_source = tmp_buf[0]&0xf;
	if (rsp_source >= BT_INFO_SRC_8821A_2ANT_MAX)
		rsp_source = BT_INFO_SRC_8821A_2ANT_WIFI_FW;
	coex_sta->bt_info_c2h_cnt[rsp_source]++;

	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY,
		  "[BTCoex], Bt info[%d], length =%d, hex data =[",
		  rsp_source, length);
	for (i = 0; i < length; i++) {
		coex_sta->bt_info_c2h[rsp_source][i] = tmp_buf[i];
		if (i == 1)
			bt_info = tmp_buf[i];
		if (i == length-1)
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, "0x%02x]\n",
				  tmp_buf[i]);
		else
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, "0x%02x, ",
				  tmp_buf[i]);
	}

	if (BT_INFO_SRC_8821A_2ANT_WIFI_FW != rsp_source) {
		coex_sta->bt_retry_cnt =	/*  [3:0] */
			coex_sta->bt_info_c2h[rsp_source][2]&0xf;
		coex_sta->bt_rssi =
			coex_sta->bt_info_c2h[rsp_source][3]*2+10;
		coex_sta->bt_info_ext =
			coex_sta->bt_info_c2h[rsp_source][4];

		/*  Here we need to resend some wifi info to BT */
		/*  because bt is reset and loss of the info. */
		if ((coex_sta->bt_info_ext & BIT(1))) {
			btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_CONNECTED,
					   &wifi_connected);
			if (wifi_connected)
				ex_halbtc8821a2ant_media_status_notify(btcoexist,
								       BTC_MEDIA_CONNECT);
			else
				ex_halbtc8821a2ant_media_status_notify(btcoexist,
								       BTC_MEDIA_DISCONNECT);

			set_bt_psd_mode = 0;
		}
		if (set_bt_psd_mode <= 3) {
			/* fix CH-BW mode  */
			halbtc8821a2ant_set_bt_psd_mode(btcoexist,
							FORCE_EXEC, 0x0);
			set_bt_psd_mode++;
		}

		if (coex_dm->cur_bt_lna_constrain) {
			if (!(coex_sta->bt_info_ext & BIT(2))) {
				if (set_bt_lna_cnt <= 3) {
					set_bt_lna_constrain(btcoexist,
							     FORCE_EXEC, true);
					set_bt_lna_cnt++;
				}
			}
		} else {
			set_bt_lna_cnt = 0;
		}

		if ((coex_sta->bt_info_ext & BIT(3)))
			halbtc8821a2ant_ignore_wlan_act(btcoexist,
							FORCE_EXEC, false);
		else
			/* BT already NOT ignore Wlan active, do nothing here */

		if (!(coex_sta->bt_info_ext & BIT(4)))
			halbtc8821a2ant_bt_auto_report(btcoexist,
						       FORCE_EXEC, true);
	}

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	/*  check BIT(2) first ==> check if bt is under inquiry or page scan */
	if (bt_info & BT_INFO_8821A_2ANT_B_INQ_PAGE) {
		coex_sta->c2h_bt_inquiry_page = true;
		coex_dm->bt_status = BT_8821A_2ANT_BT_STATUS_NON_IDLE;
	} else {
		coex_sta->c2h_bt_inquiry_page = false;
		if (bt_info == 0x1) {	/*  connection exists but not busy */
			coex_sta->bt_link_exist = true;
			coex_dm->bt_status =
				BT_8821A_2ANT_BT_STATUS_CONNECTED_IDLE;
		} else if (bt_info & BT_INFO_8821A_2ANT_B_CONNECTION) {
			/*  connection exists and some link is busy */
			coex_sta->bt_link_exist = true;
			if (bt_info & BT_INFO_8821A_2ANT_B_FTP)
				coex_sta->pan_exist = true;
			else
				coex_sta->pan_exist = false;
			if (bt_info & BT_INFO_8821A_2ANT_B_A2DP)
				coex_sta->a2dp_exist = true;
			else
				coex_sta->a2dp_exist = false;
			if (bt_info & BT_INFO_8821A_2ANT_B_HID)
				coex_sta->hid_exist = true;
			else
				coex_sta->hid_exist = false;
			if (bt_info & BT_INFO_8821A_2ANT_B_SCO_ESCO)
				coex_sta->sco_exist = true;
			else
				coex_sta->sco_exist = false;
			coex_dm->bt_status = BT_8821A_2ANT_BT_STATUS_NON_IDLE;
		} else {
			coex_sta->bt_link_exist = false;
			coex_sta->pan_exist = false;
			coex_sta->a2dp_exist = false;
			coex_sta->hid_exist = false;
			coex_sta->sco_exist = false;
			coex_dm->bt_status = BT_8821A_2ANT_BT_STATUS_IDLE;
		}

		if (bt_hs_on)
			coex_dm->bt_status = BT_8821A_2ANT_BT_STATUS_NON_IDLE;
	}

	if (BT_8821A_2ANT_BT_STATUS_NON_IDLE == coex_dm->bt_status)
		bt_busy = true;
	else
		bt_busy = false;
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bt_busy);

	if (BT_8821A_2ANT_BT_STATUS_IDLE != coex_dm->bt_status)
		limited_dig = true;
	else
		limited_dig = false;
	coex_dm->limited_dig = limited_dig;
	btcoexist->btc_set(btcoexist, BTC_SET_BL_BT_LIMITED_DIG, &limited_dig);

	halbtc8821a2ant_run_coexist_mechanism(btcoexist);
}

void ex_halbtc8821a2ant_halt_notify(struct btc_coexist *btcoexist)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, "[BTCoex], Halt notify\n");

	halbtc8821a2ant_ignore_wlan_act(btcoexist, FORCE_EXEC, true);
	ex_halbtc8821a2ant_media_status_notify(btcoexist, BTC_MEDIA_DISCONNECT);
}

void ex_halbtc8821a2ant_periodical(struct btc_coexist *btcoexist)
{
	static u8 dis_ver_info_cnt;
	u32 fw_ver = 0, bt_patch_ver = 0;
	struct btc_board_info *board_info = &btcoexist->board_info;
	struct btc_stack_info *stack_info = &btcoexist->stack_info;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE,
		  "[BTCoex], ========================== Periodical ===========================\n");

	if (dis_ver_info_cnt <= 5) {
		dis_ver_info_cnt += 1;
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
			  "[BTCoex], ****************************************************************\n");
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
			  "[BTCoex], Ant PG Num/ Ant Mech/ Ant Pos = %d/ %d/ %d\n",
			  board_info->pg_ant_num, board_info->btdm_ant_num,
			  board_info->btdm_ant_pos);
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
			  "[BTCoex], BT stack/ hci ext ver = %s / %d\n",
			  ((stack_info->profile_notified) ? "Yes" : "No"),
			  stack_info->hci_version);
		btcoexist->btc_get(btcoexist, BTC_GET_U4_BT_PATCH_VER,
				   &bt_patch_ver);
		btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
			  "[BTCoex], CoexVer/ FwVer/ PatchVer = %d_%x/ 0x%x/ 0x%x(%d)\n",
			  glcoex_ver_date_8821a_2ant,
			  glcoex_ver_8821a_2ant,
			  fw_ver, bt_patch_ver, bt_patch_ver);
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT,
			  "[BTCoex], ****************************************************************\n");
	}

	halbtc8821a2ant_query_bt_info(btcoexist);
	halbtc8821a2ant_monitor_bt_ctr(btcoexist);
	monitor_bt_enable_disable(btcoexist);
}
