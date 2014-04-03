/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "hal_bt_coexist.h"
#include "../pci.h"
#include "dm.h"
#include "fw.h"
#include "phy.h"
#include "reg.h"
#include "hal_btc.h"

static bool bt_operation_on = false;

void rtl8821ae_dm_bt_reject_ap_aggregated_packet(struct ieee80211_hw *hw, bool b_reject)
{
#if 0
	struct rtl_priv rtlpriv = rtl_priv(hw);
	PRX_TS_RECORD			pRxTs = NULL;

	if(b_reject){
		// Do not allow receiving A-MPDU aggregation.
		if (rtlpriv->mac80211.vendor == PEER_CISCO) {
				if (pHTInfo->bAcceptAddbaReq) {
					RTPRINT(FBT, BT_TRACE, ("BT_Disallow AMPDU \n"));
					pHTInfo->bAcceptAddbaReq = FALSE;
					if(GetTs(Adapter, (PTS_COMMON_INFO*)(&pRxTs), pMgntInfo->Bssid, 0, RX_DIR, FALSE))
						TsInitDelBA(Adapter, (PTS_COMMON_INFO)pRxTs, RX_DIR);
				}
			} else {
				if (!pHTInfo->bAcceptAddbaReq) {
					RTPRINT(FBT, BT_TRACE, ("BT_Allow AMPDU BT Idle\n"));
					pHTInfo->bAcceptAddbaReq = TRUE;
				}
			}
		} else {
			if(rtlpriv->mac80211.vendor == PEER_CISCO) {
				if (!pHTInfo->bAcceptAddbaReq) {
					RTPRINT(FBT, BT_TRACE, ("BT_Allow AMPDU \n"));
					pHTInfo->bAcceptAddbaReq = TRUE;
				}
			}
		}
#endif
}

void _rtl8821ae_dm_bt_check_wifi_state(struct ieee80211_hw *hw)
{
struct rtl_priv *rtlpriv = rtl_priv(hw);
struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
struct rtl_phy *rtlphy = &(rtlpriv->phy);

if (rtlpriv->link_info.b_busytraffic) {
	rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_IDLE;

	if(rtlpriv->link_info.b_tx_busy_traffic) {
		rtlpcipriv->btcoexist.current_state |= BT_COEX_STATE_WIFI_UPLINK;
	} else {
		rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_UPLINK;
	}

	if(rtlpriv->link_info.b_rx_busy_traffic) {
		rtlpcipriv->btcoexist.current_state |= BT_COEX_STATE_WIFI_DOWNLINK;
	} else {
		rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_DOWNLINK;
	}
} else {
	rtlpcipriv->btcoexist.current_state |= BT_COEX_STATE_WIFI_IDLE;
	rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_UPLINK;
	rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_DOWNLINK;
}

if (rtlpriv->mac80211.mode == WIRELESS_MODE_G
	|| rtlpriv->mac80211.mode == WIRELESS_MODE_B) {
	rtlpcipriv->btcoexist.current_state |= BT_COEX_STATE_WIFI_LEGACY;
	rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_HT20;
	rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_HT40;
} else {
	rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_LEGACY;
	if(rtlphy->current_chan_bw == HT_CHANNEL_WIDTH_20_40) {
		rtlpcipriv->btcoexist.current_state |= BT_COEX_STATE_WIFI_HT40;
		rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_HT20;
	} else {
		rtlpcipriv->btcoexist.current_state |= BT_COEX_STATE_WIFI_HT20;
		rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_HT40;
	}
}

if (bt_operation_on) {
	rtlpcipriv->btcoexist.current_state |= BT_COEX_STATE_BT30;
} else {
	rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_BT30;
}
}


u8 rtl8821ae_dm_bt_check_coex_rssi_state1(struct ieee80211_hw *hw,
						u8	level_num, u8	rssi_thresh, u8 rssi_thresh1)

{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	long undecoratedsmoothed_pwdb = 0;
	u8 bt_rssi_state = 0;

	undecoratedsmoothed_pwdb =  rtl8821ae_dm_bt_get_rx_ss(hw);

	if(level_num == 2) {
		rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_RSSI_1_MEDIUM;

		if( (rtlpcipriv->btcoexist.bt_pre_rssi_state == BT_RSSI_STATE_LOW) ||
			(rtlpcipriv->btcoexist.bt_pre_rssi_state == BT_RSSI_STATE_STAY_LOW)) {
			if(undecoratedsmoothed_pwdb >= (rssi_thresh + BT_FW_COEX_THRESH_TOL)) {
				bt_rssi_state = BT_RSSI_STATE_HIGH;
				rtlpcipriv->btcoexist.current_state |= BT_COEX_STATE_WIFI_RSSI_1_HIGH;
				rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_RSSI_1_LOW;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[DM][BT], RSSI_1 state switch to High\n"));
			} else {
				bt_rssi_state = BT_RSSI_STATE_STAY_LOW;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[DM][BT], RSSI_1 state stay at Low\n"));
			}
		} else {
			if(undecoratedsmoothed_pwdb < rssi_thresh) {
				bt_rssi_state = BT_RSSI_STATE_LOW;
				rtlpcipriv->btcoexist.current_state |= BT_COEX_STATE_WIFI_RSSI_1_LOW;
				rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_RSSI_1_HIGH;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[DM][BT], RSSI_1 state switch to Low\n"));
			} else {
				bt_rssi_state = BT_RSSI_STATE_STAY_HIGH;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[DM][BT], RSSI_1 state stay at High\n"));
			}
		}
	} else if(level_num == 3) {
		if(rssi_thresh > rssi_thresh1) {
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[DM][BT], RSSI_1 thresh error!!\n"));
			return rtlpcipriv->btcoexist.bt_pre_rssi_state;
		}

		if( (rtlpcipriv->btcoexist.bt_pre_rssi_state == BT_RSSI_STATE_LOW) ||
			(rtlpcipriv->btcoexist.bt_pre_rssi_state == BT_RSSI_STATE_STAY_LOW)) {
			if(undecoratedsmoothed_pwdb >= (rssi_thresh+BT_FW_COEX_THRESH_TOL)) {
				bt_rssi_state = BT_RSSI_STATE_MEDIUM;
				rtlpcipriv->btcoexist.current_state |= BT_COEX_STATE_WIFI_RSSI_1_MEDIUM;
				rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_RSSI_1_LOW;
				rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_RSSI_1_HIGH;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[DM][BT], RSSI_1 state switch to Medium\n"));
			} else {
				bt_rssi_state = BT_RSSI_STATE_STAY_LOW;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[DM][BT], RSSI_1 state stay at Low\n"));
			}
		} else if( (rtlpcipriv->btcoexist.bt_pre_rssi_state == BT_RSSI_STATE_MEDIUM) ||
			(rtlpcipriv->btcoexist.bt_pre_rssi_state == BT_RSSI_STATE_STAY_MEDIUM)) {
			if(undecoratedsmoothed_pwdb >= (rssi_thresh1 + BT_FW_COEX_THRESH_TOL)) {
				bt_rssi_state = BT_RSSI_STATE_HIGH;
				rtlpcipriv->btcoexist.current_state |= BT_COEX_STATE_WIFI_RSSI_1_HIGH;
				rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_RSSI_1_LOW;
				rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_RSSI_1_MEDIUM;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[DM][BT], RSSI_1 state switch to High\n"));
			} else if(undecoratedsmoothed_pwdb < rssi_thresh) {
				bt_rssi_state = BT_RSSI_STATE_LOW;
				rtlpcipriv->btcoexist.current_state |= BT_COEX_STATE_WIFI_RSSI_1_LOW;
				rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_RSSI_1_HIGH;
				rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_RSSI_1_MEDIUM;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[DM][BT], RSSI_1 state switch to Low\n"));
			} else {
				bt_rssi_state = BT_RSSI_STATE_STAY_MEDIUM;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[DM][BT], RSSI_1 state stay at Medium\n"));
			}
		} else {
			if(undecoratedsmoothed_pwdb < rssi_thresh1) {
				bt_rssi_state = BT_RSSI_STATE_MEDIUM;
				rtlpcipriv->btcoexist.current_state |= BT_COEX_STATE_WIFI_RSSI_1_MEDIUM;
				rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_RSSI_1_HIGH;
				rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_RSSI_1_LOW;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,("[DM][BT], RSSI_1 state switch to Medium\n"));
			} else {
				bt_rssi_state = BT_RSSI_STATE_STAY_HIGH;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[DM][BT], RSSI_1 state stay at High\n"));
			}
		}
	}

	rtlpcipriv->btcoexist.bt_pre_rssi_state1 = bt_rssi_state;

	return bt_rssi_state;
}

u8 rtl8821ae_dm_bt_check_coex_rssi_state(struct ieee80211_hw *hw,
						u8	level_num, u8	rssi_thresh, u8 rssi_thresh1)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	long undecoratedsmoothed_pwdb = 0;
	u8 bt_rssi_state = 0;

	undecoratedsmoothed_pwdb = rtl8821ae_dm_bt_get_rx_ss(hw);

	if (level_num == 2) {
		rtlpcipriv->btcoexist.current_state &= ~BT_COEX_STATE_WIFI_RSSI_MEDIUM;

		if ((rtlpcipriv->btcoexist.bt_pre_rssi_state == BT_RSSI_STATE_LOW) ||
			(rtlpcipriv->btcoexist.bt_pre_rssi_state == BT_RSSI_STATE_STAY_LOW)){
			if (undecoratedsmoothed_pwdb
				>= (rssi_thresh + BT_FW_COEX_THRESH_TOL)) {
				bt_rssi_state = BT_RSSI_STATE_HIGH;
				rtlpcipriv->btcoexist.current_state
					|= BT_COEX_STATE_WIFI_RSSI_HIGH;
				rtlpcipriv->btcoexist.current_state
					&= ~BT_COEX_STATE_WIFI_RSSI_LOW;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
					("[DM][BT], RSSI state switch to High\n"));
			} else {
				bt_rssi_state = BT_RSSI_STATE_STAY_LOW;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
					("[DM][BT], RSSI state stay at Low\n"));
			}
		} else {
			if (undecoratedsmoothed_pwdb < rssi_thresh) {
				bt_rssi_state = BT_RSSI_STATE_LOW;
				rtlpcipriv->btcoexist.current_state
					|= BT_COEX_STATE_WIFI_RSSI_LOW;
				rtlpcipriv->btcoexist.current_state
					&= ~BT_COEX_STATE_WIFI_RSSI_HIGH;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
					("[DM][BT], RSSI state switch to Low\n"));
			} else {
				bt_rssi_state = BT_RSSI_STATE_STAY_HIGH;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
					("[DM][BT], RSSI state stay at High\n"));
			}
		}
	}
	else if (level_num == 3) {
		if (rssi_thresh > rssi_thresh1) {
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
				("[DM][BT], RSSI thresh error!!\n"));
			return rtlpcipriv->btcoexist.bt_pre_rssi_state;
		}
		if ((rtlpcipriv->btcoexist.bt_pre_rssi_state == BT_RSSI_STATE_LOW) ||
			(rtlpcipriv->btcoexist.bt_pre_rssi_state == BT_RSSI_STATE_STAY_LOW)) {
			if(undecoratedsmoothed_pwdb
				>= (rssi_thresh + BT_FW_COEX_THRESH_TOL)) {
				bt_rssi_state = BT_RSSI_STATE_MEDIUM;
				rtlpcipriv->btcoexist.current_state
					|= BT_COEX_STATE_WIFI_RSSI_MEDIUM;
				rtlpcipriv->btcoexist.current_state
					&= ~BT_COEX_STATE_WIFI_RSSI_LOW;
				rtlpcipriv->btcoexist.current_state
					&= ~BT_COEX_STATE_WIFI_RSSI_HIGH;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
					("[DM][BT], RSSI state switch to Medium\n"));
			} else {
				bt_rssi_state = BT_RSSI_STATE_STAY_LOW;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
					("[DM][BT], RSSI state stay at Low\n"));
			}
		} else if ((rtlpcipriv->btcoexist.bt_pre_rssi_state == BT_RSSI_STATE_MEDIUM) ||
			(rtlpcipriv->btcoexist.bt_pre_rssi_state == BT_RSSI_STATE_STAY_MEDIUM)) {
			if (undecoratedsmoothed_pwdb
				>= (rssi_thresh1 + BT_FW_COEX_THRESH_TOL)) {
				bt_rssi_state = BT_RSSI_STATE_HIGH;
				rtlpcipriv->btcoexist.current_state
					|= BT_COEX_STATE_WIFI_RSSI_HIGH;
				rtlpcipriv->btcoexist.current_state
					&= ~BT_COEX_STATE_WIFI_RSSI_LOW;
				rtlpcipriv->btcoexist.current_state
					&= ~BT_COEX_STATE_WIFI_RSSI_MEDIUM;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
					("[DM][BT], RSSI state switch to High\n"));
			} else if(undecoratedsmoothed_pwdb < rssi_thresh)
			{
				bt_rssi_state = BT_RSSI_STATE_LOW;
				rtlpcipriv->btcoexist.current_state
					|= BT_COEX_STATE_WIFI_RSSI_LOW;
				rtlpcipriv->btcoexist.current_state
					&= ~BT_COEX_STATE_WIFI_RSSI_HIGH;
				rtlpcipriv->btcoexist.current_state
					&= ~BT_COEX_STATE_WIFI_RSSI_MEDIUM;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
					("[DM][BT], RSSI state switch to Low\n"));
			} else {
				bt_rssi_state = BT_RSSI_STATE_STAY_MEDIUM;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
					("[DM][BT], RSSI state stay at Medium\n"));
			}
		} else {
			if(undecoratedsmoothed_pwdb < rssi_thresh1) {
				bt_rssi_state = BT_RSSI_STATE_MEDIUM;
				rtlpcipriv->btcoexist.current_state
					|= BT_COEX_STATE_WIFI_RSSI_MEDIUM;
				rtlpcipriv->btcoexist.current_state
					&= ~BT_COEX_STATE_WIFI_RSSI_HIGH;
				rtlpcipriv->btcoexist.current_state
					&= ~BT_COEX_STATE_WIFI_RSSI_LOW;
				RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
					("[DM][BT], RSSI state switch to Medium\n"));
			} else {
			bt_rssi_state = BT_RSSI_STATE_STAY_HIGH;
			RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
				("[DM][BT], RSSI state stay at High\n"));
			}
		}
	}

	rtlpcipriv->btcoexist.bt_pre_rssi_state = bt_rssi_state;
	return bt_rssi_state;
}
long rtl8821ae_dm_bt_get_rx_ss(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	long undecoratedsmoothed_pwdb = 0;

	if (rtlpriv->mac80211.link_state >= MAC80211_LINKED) {
		undecoratedsmoothed_pwdb = GET_UNDECORATED_AVERAGE_RSSI(rtlpriv);
	} else {
		undecoratedsmoothed_pwdb
			= rtlpriv->dm.entry_min_undecoratedsmoothed_pwdb;
	}
	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("rtl8821ae_dm_bt_get_rx_ss() = %ld\n", undecoratedsmoothed_pwdb));

	return undecoratedsmoothed_pwdb;
}

void rtl8821ae_dm_bt_balance(struct ieee80211_hw *hw,
							bool b_balance_on, u8 ms0, u8 ms1)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 h2c_parameter[3] ={0};

	if (b_balance_on) {
		h2c_parameter[2] = 1;
		h2c_parameter[1] = ms1;
		h2c_parameter[0] = ms0;
		rtlpcipriv->btcoexist.b_fw_coexist_all_off = false;
	} else {
		h2c_parameter[2] = 0;
		h2c_parameter[1] = 0;
		h2c_parameter[0] = 0;
	}
	rtlpcipriv->btcoexist.b_balance_on = b_balance_on;

	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("[DM][BT], Balance=[%s:%dms:%dms], write 0xc=0x%x\n",
		b_balance_on?"ON":"OFF", ms0, ms1,
		h2c_parameter[0]<<16 | h2c_parameter[1]<<8 | h2c_parameter[2]));

	rtl8821ae_fill_h2c_cmd(hw, 0xc, 3, h2c_parameter);
}


void rtl8821ae_dm_bt_agc_table(struct ieee80211_hw *hw, u8 type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);

	if (type == BT_AGCTABLE_OFF) {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BT]AGCTable Off!\n"));
		rtl_write_dword(rtlpriv, 0xc78,0x641c0001);
		rtl_write_dword(rtlpriv, 0xc78,0x631d0001);
		rtl_write_dword(rtlpriv, 0xc78,0x621e0001);
		rtl_write_dword(rtlpriv, 0xc78,0x611f0001);
		rtl_write_dword(rtlpriv, 0xc78,0x60200001);

		rtl8821ae_phy_set_rf_reg(hw, RF90_PATH_A,
					RF_RX_AGC_HP, 0xfffff, 0x32000);
		rtl8821ae_phy_set_rf_reg(hw, RF90_PATH_A,
					RF_RX_AGC_HP, 0xfffff, 0x71000);
		rtl8821ae_phy_set_rf_reg(hw, RF90_PATH_A,
					RF_RX_AGC_HP, 0xfffff, 0xb0000);
		rtl8821ae_phy_set_rf_reg(hw, RF90_PATH_A,
					RF_RX_AGC_HP, 0xfffff, 0xfc000);
		rtl8821ae_phy_set_rf_reg(hw, RF90_PATH_A,
					RF_RX_G1, 0xfffff, 0x30355);
	} else if (type == BT_AGCTABLE_ON) {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BT]AGCTable On!\n"));
		rtl_write_dword(rtlpriv, 0xc78,0x4e1c0001);
		rtl_write_dword(rtlpriv, 0xc78,0x4d1d0001);
		rtl_write_dword(rtlpriv, 0xc78,0x4c1e0001);
		rtl_write_dword(rtlpriv, 0xc78,0x4b1f0001);
		rtl_write_dword(rtlpriv, 0xc78,0x4a200001);

		rtl8821ae_phy_set_rf_reg(hw, RF90_PATH_A,
					RF_RX_AGC_HP, 0xfffff, 0xdc000);
		rtl8821ae_phy_set_rf_reg(hw, RF90_PATH_A,
					RF_RX_AGC_HP, 0xfffff, 0x90000);
		rtl8821ae_phy_set_rf_reg(hw, RF90_PATH_A,
					RF_RX_AGC_HP, 0xfffff, 0x51000);
		rtl8821ae_phy_set_rf_reg(hw, RF90_PATH_A,
					RF_RX_AGC_HP, 0xfffff, 0x12000);
		rtl8821ae_phy_set_rf_reg(hw, RF90_PATH_A,
					RF_RX_G1, 0xfffff, 0x00355);

		rtlpcipriv->btcoexist.b_sw_coexist_all_off = false;
	}
}

void rtl8821ae_dm_bt_bb_back_off_level(struct ieee80211_hw *hw, u8 type)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);

	if (type == BT_BB_BACKOFF_OFF) {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BT]BBBackOffLevel Off!\n"));
		rtl_write_dword(rtlpriv, 0xc04,0x3a05611);
	} else if (type == BT_BB_BACKOFF_ON) {
		RT_TRACE(COMP_BT_COEXIST, DBG_TRACE, ("[BT]BBBackOffLevel On!\n"));
		rtl_write_dword(rtlpriv, 0xc04,0x3a07611);
		rtlpcipriv->btcoexist.b_sw_coexist_all_off = false;
	}
}

void rtl8821ae_dm_bt_fw_coex_all_off(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("rtl8821ae_dm_bt_fw_coex_all_off()\n"));

	if(rtlpcipriv->btcoexist.b_fw_coexist_all_off)
		return;

	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("rtl8821ae_dm_bt_fw_coex_all_off(), real Do\n"));
	rtl8821ae_dm_bt_fw_coex_all_off_8723a(hw);
	rtlpcipriv->btcoexist.b_fw_coexist_all_off = true;
}

void rtl8821ae_dm_bt_sw_coex_all_off(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("rtl8821ae_dm_bt_sw_coex_all_off()\n"));

	if(rtlpcipriv->btcoexist.b_sw_coexist_all_off)
		return;

	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("rtl8821ae_dm_bt_sw_coex_all_off(), real Do\n"));
	rtl8821ae_dm_bt_sw_coex_all_off_8723a(hw);
	rtlpcipriv->btcoexist.b_sw_coexist_all_off = true;
}

void rtl8821ae_dm_bt_hw_coex_all_off(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("rtl8821ae_dm_bt_hw_coex_all_off()\n"));

	if(rtlpcipriv->btcoexist.b_hw_coexist_all_off)
		return;
	RT_TRACE(COMP_BT_COEXIST, DBG_TRACE,
		("rtl8821ae_dm_bt_hw_coex_all_off(), real Do\n"));

	rtl8821ae_dm_bt_hw_coex_all_off_8723a(hw);

	rtlpcipriv->btcoexist.b_hw_coexist_all_off = true;
}

void rtl8821ae_btdm_coex_all_off(struct ieee80211_hw *hw)
{
	rtl8821ae_dm_bt_fw_coex_all_off(hw);
	rtl8821ae_dm_bt_sw_coex_all_off(hw);
	rtl8821ae_dm_bt_hw_coex_all_off(hw);
}

bool rtl8821ae_dm_bt_is_coexist_state_changed(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *rtlpcipriv = rtl_pcipriv(hw);

	if((rtlpcipriv->btcoexist.previous_state
		== rtlpcipriv->btcoexist.current_state)
		&& (rtlpcipriv->btcoexist.previous_state_h
		== rtlpcipriv->btcoexist.current_state_h))
		return false;
	else
		return true;
}

bool rtl8821ae_dm_bt_is_wifi_up_link(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->link_info.b_tx_busy_traffic)
		return true;
	else
		return false;
}

