/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
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
#include "wifi.h"
#include "stats.h"
#include <linux/export.h>

u8 rtl_query_rxpwrpercentage(char antpower)
{
	if ((antpower <= -100) || (antpower >= 20))
		return 0;
	else if (antpower >= 0)
		return 100;
	else
		return 100 + antpower;
}
EXPORT_SYMBOL(rtl_query_rxpwrpercentage);

u8 rtl_evm_db_to_percentage(char value)
{
	char ret_val;
	ret_val = value;

	if (ret_val >= 0)
		ret_val = 0;
	if (ret_val <= -33)
		ret_val = -33;
	ret_val = 0 - ret_val;
	ret_val *= 3;
	if (ret_val == 99)
		ret_val = 100;

	return ret_val;
}
EXPORT_SYMBOL(rtl_evm_db_to_percentage);

static long rtl_translate_todbm(struct ieee80211_hw *hw,
				u8 signal_strength_index)
{
	long signal_power;

	signal_power = (long)((signal_strength_index + 1) >> 1);
	signal_power -= 95;
	return signal_power;
}

long rtl_signal_scale_mapping(struct ieee80211_hw *hw, long currsig)
{
	long retsig;

	if (currsig >= 61 && currsig <= 100)
		retsig = 90 + ((currsig - 60) / 4);
	else if (currsig >= 41 && currsig <= 60)
		retsig = 78 + ((currsig - 40) / 2);
	else if (currsig >= 31 && currsig <= 40)
		retsig = 66 + (currsig - 30);
	else if (currsig >= 21 && currsig <= 30)
		retsig = 54 + (currsig - 20);
	else if (currsig >= 5 && currsig <= 20)
		retsig = 42 + (((currsig - 5) * 2) / 3);
	else if (currsig == 4)
		retsig = 36;
	else if (currsig == 3)
		retsig = 27;
	else if (currsig == 2)
		retsig = 18;
	else if (currsig == 1)
		retsig = 9;
	else
		retsig = currsig;

	return retsig;
}
EXPORT_SYMBOL(rtl_signal_scale_mapping);

static void rtl_process_ui_rssi(struct ieee80211_hw *hw,
				struct rtl_stats *pstatus)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u8 rfpath;
	u32 last_rssi, tmpval;

	rtlpriv->stats.rssi_calculate_cnt++;

	if (rtlpriv->stats.ui_rssi.total_num++ >= PHY_RSSI_SLID_WIN_MAX) {
		rtlpriv->stats.ui_rssi.total_num = PHY_RSSI_SLID_WIN_MAX;
		last_rssi = rtlpriv->stats.ui_rssi.elements[
			rtlpriv->stats.ui_rssi.index];
		rtlpriv->stats.ui_rssi.total_val -= last_rssi;
	}
	rtlpriv->stats.ui_rssi.total_val += pstatus->signalstrength;
	rtlpriv->stats.ui_rssi.elements[rtlpriv->stats.ui_rssi.index++] =
	    pstatus->signalstrength;
	if (rtlpriv->stats.ui_rssi.index >= PHY_RSSI_SLID_WIN_MAX)
		rtlpriv->stats.ui_rssi.index = 0;
	tmpval = rtlpriv->stats.ui_rssi.total_val /
		rtlpriv->stats.ui_rssi.total_num;
	rtlpriv->stats.signal_strength = rtl_translate_todbm(hw,
		(u8) tmpval);
	pstatus->rssi = rtlpriv->stats.signal_strength;

	if (pstatus->is_cck)
		return;

	for (rfpath = RF90_PATH_A; rfpath < rtlphy->num_total_rfpath;
	     rfpath++) {
		if (rtlpriv->stats.rx_rssi_percentage[rfpath] == 0) {
			rtlpriv->stats.rx_rssi_percentage[rfpath] =
			    pstatus->rx_mimo_signalstrength[rfpath];

		}
		if (pstatus->rx_mimo_signalstrength[rfpath] >
		    rtlpriv->stats.rx_rssi_percentage[rfpath]) {
			rtlpriv->stats.rx_rssi_percentage[rfpath] =
			    ((rtlpriv->stats.rx_rssi_percentage[rfpath] *
			      (RX_SMOOTH_FACTOR - 1)) +
			     (pstatus->rx_mimo_signalstrength[rfpath])) /
			    (RX_SMOOTH_FACTOR);
			rtlpriv->stats.rx_rssi_percentage[rfpath] =
			    rtlpriv->stats.rx_rssi_percentage[rfpath] + 1;
		} else {
			rtlpriv->stats.rx_rssi_percentage[rfpath] =
			    ((rtlpriv->stats.rx_rssi_percentage[rfpath] *
			      (RX_SMOOTH_FACTOR - 1)) +
			     (pstatus->rx_mimo_signalstrength[rfpath])) /
			    (RX_SMOOTH_FACTOR);
		}
	}
}

static void rtl_update_rxsignalstatistics(struct ieee80211_hw *hw,
					  struct rtl_stats *pstatus)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int weighting = 0;

	if (rtlpriv->stats.recv_signal_power == 0)
		rtlpriv->stats.recv_signal_power = pstatus->recvsignalpower;
	if (pstatus->recvsignalpower > rtlpriv->stats.recv_signal_power)
		weighting = 5;
	else if (pstatus->recvsignalpower < rtlpriv->stats.recv_signal_power)
		weighting = (-5);
	rtlpriv->stats.recv_signal_power = (rtlpriv->stats.recv_signal_power *
		5 + pstatus->recvsignalpower + weighting) / 6;
}

static void rtl_process_pwdb(struct ieee80211_hw *hw, struct rtl_stats *pstatus)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_sta_info *drv_priv = NULL;
	struct ieee80211_sta *sta = NULL;
	long undec_sm_pwdb;

	rcu_read_lock();
	if (rtlpriv->mac80211.opmode != NL80211_IFTYPE_STATION)
		sta = rtl_find_sta(hw, pstatus->psaddr);

	/* adhoc or ap mode */
	if (sta) {
		drv_priv = (struct rtl_sta_info *) sta->drv_priv;
		undec_sm_pwdb = drv_priv->rssi_stat.undec_sm_pwdb;
	} else {
		undec_sm_pwdb = rtlpriv->dm.undec_sm_pwdb;
	}

	if (undec_sm_pwdb < 0)
		undec_sm_pwdb = pstatus->rx_pwdb_all;
	if (pstatus->rx_pwdb_all > (u32) undec_sm_pwdb) {
		undec_sm_pwdb = (((undec_sm_pwdb) *
		      (RX_SMOOTH_FACTOR - 1)) +
		     (pstatus->rx_pwdb_all)) / (RX_SMOOTH_FACTOR);
		undec_sm_pwdb = undec_sm_pwdb + 1;
	} else {
		undec_sm_pwdb = (((undec_sm_pwdb) * (RX_SMOOTH_FACTOR - 1)) +
		     (pstatus->rx_pwdb_all)) / (RX_SMOOTH_FACTOR);
	}

	if (sta) {
		drv_priv->rssi_stat.undec_sm_pwdb = undec_sm_pwdb;
	} else {
		rtlpriv->dm.undec_sm_pwdb = undec_sm_pwdb;
	}
	rcu_read_unlock();

	rtl_update_rxsignalstatistics(hw, pstatus);
}

static void rtl_process_ui_link_quality(struct ieee80211_hw *hw,
					struct rtl_stats *pstatus)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u32 last_evm, n_stream, tmpval;

	if (pstatus->signalquality == 0)
		return;

	if (rtlpriv->stats.ui_link_quality.total_num++ >=
	    PHY_LINKQUALITY_SLID_WIN_MAX) {
		rtlpriv->stats.ui_link_quality.total_num =
		    PHY_LINKQUALITY_SLID_WIN_MAX;
		last_evm = rtlpriv->stats.ui_link_quality.elements[
			rtlpriv->stats.ui_link_quality.index];
		rtlpriv->stats.ui_link_quality.total_val -= last_evm;
	}
	rtlpriv->stats.ui_link_quality.total_val += pstatus->signalquality;
	rtlpriv->stats.ui_link_quality.elements[
		rtlpriv->stats.ui_link_quality.index++] =
						 pstatus->signalquality;
	if (rtlpriv->stats.ui_link_quality.index >=
	    PHY_LINKQUALITY_SLID_WIN_MAX)
		rtlpriv->stats.ui_link_quality.index = 0;
	tmpval = rtlpriv->stats.ui_link_quality.total_val /
	    rtlpriv->stats.ui_link_quality.total_num;
	rtlpriv->stats.signal_quality = tmpval;
	rtlpriv->stats.last_sigstrength_inpercent = tmpval;
	for (n_stream = 0; n_stream < 2; n_stream++) {
		if (pstatus->rx_mimo_sig_qual[n_stream] != -1) {
			if (rtlpriv->stats.rx_evm_percentage[n_stream] == 0) {
				rtlpriv->stats.rx_evm_percentage[n_stream] =
				    pstatus->rx_mimo_sig_qual[n_stream];
			}
			rtlpriv->stats.rx_evm_percentage[n_stream] =
			    ((rtlpriv->stats.rx_evm_percentage[n_stream]
			      * (RX_SMOOTH_FACTOR - 1)) +
			     (pstatus->rx_mimo_sig_qual[n_stream] * 1)) /
			    (RX_SMOOTH_FACTOR);
		}
	}
}

void rtl_process_phyinfo(struct ieee80211_hw *hw, u8 *buffer,
	struct rtl_stats *pstatus)
{

	if (!pstatus->packet_matchbssid)
		return;

	rtl_process_ui_rssi(hw, pstatus);
	rtl_process_pwdb(hw, pstatus);
	rtl_process_ui_link_quality(hw, pstatus);
}
EXPORT_SYMBOL(rtl_process_phyinfo);
