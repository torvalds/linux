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

#include <linux/ip.h>
#include "wifi.h"
#include "rc.h"
#include "base.h"
#include "efuse.h"
#include "cam.h"
#include "ps.h"
#include "regd.h"

/*
 *NOTICE!!!: This file will be very big, we hsould
 *keep it clear under follwing roles:
 *
 *This file include follwing part, so, if you add new
 *functions into this file, please check which part it
 *should includes. or check if you should add new part
 *for this file:
 *
 *1) mac80211 init functions
 *2) tx information functions
 *3) functions called by core.c
 *4) wq & timer callback functions
 *5) frame process functions
 *6) IOT functions
 *7) sysfs functions
 *8) ...
 */

/*********************************************************
 *
 * mac80211 init functions
 *
 *********************************************************/
static struct ieee80211_channel rtl_channeltable_2g[] = {
	{.center_freq = 2412, .hw_value = 1,},
	{.center_freq = 2417, .hw_value = 2,},
	{.center_freq = 2422, .hw_value = 3,},
	{.center_freq = 2427, .hw_value = 4,},
	{.center_freq = 2432, .hw_value = 5,},
	{.center_freq = 2437, .hw_value = 6,},
	{.center_freq = 2442, .hw_value = 7,},
	{.center_freq = 2447, .hw_value = 8,},
	{.center_freq = 2452, .hw_value = 9,},
	{.center_freq = 2457, .hw_value = 10,},
	{.center_freq = 2462, .hw_value = 11,},
	{.center_freq = 2467, .hw_value = 12,},
	{.center_freq = 2472, .hw_value = 13,},
	{.center_freq = 2484, .hw_value = 14,},
};

static struct ieee80211_channel rtl_channeltable_5g[] = {
	{.center_freq = 5180, .hw_value = 36,},
	{.center_freq = 5200, .hw_value = 40,},
	{.center_freq = 5220, .hw_value = 44,},
	{.center_freq = 5240, .hw_value = 48,},
	{.center_freq = 5260, .hw_value = 52,},
	{.center_freq = 5280, .hw_value = 56,},
	{.center_freq = 5300, .hw_value = 60,},
	{.center_freq = 5320, .hw_value = 64,},
	{.center_freq = 5500, .hw_value = 100,},
	{.center_freq = 5520, .hw_value = 104,},
	{.center_freq = 5540, .hw_value = 108,},
	{.center_freq = 5560, .hw_value = 112,},
	{.center_freq = 5580, .hw_value = 116,},
	{.center_freq = 5600, .hw_value = 120,},
	{.center_freq = 5620, .hw_value = 124,},
	{.center_freq = 5640, .hw_value = 128,},
	{.center_freq = 5660, .hw_value = 132,},
	{.center_freq = 5680, .hw_value = 136,},
	{.center_freq = 5700, .hw_value = 140,},
	{.center_freq = 5745, .hw_value = 149,},
	{.center_freq = 5765, .hw_value = 153,},
	{.center_freq = 5785, .hw_value = 157,},
	{.center_freq = 5805, .hw_value = 161,},
	{.center_freq = 5825, .hw_value = 165,},
};

static struct ieee80211_rate rtl_ratetable_2g[] = {
	{.bitrate = 10, .hw_value = 0x00,},
	{.bitrate = 20, .hw_value = 0x01,},
	{.bitrate = 55, .hw_value = 0x02,},
	{.bitrate = 110, .hw_value = 0x03,},
	{.bitrate = 60, .hw_value = 0x04,},
	{.bitrate = 90, .hw_value = 0x05,},
	{.bitrate = 120, .hw_value = 0x06,},
	{.bitrate = 180, .hw_value = 0x07,},
	{.bitrate = 240, .hw_value = 0x08,},
	{.bitrate = 360, .hw_value = 0x09,},
	{.bitrate = 480, .hw_value = 0x0a,},
	{.bitrate = 540, .hw_value = 0x0b,},
};

static struct ieee80211_rate rtl_ratetable_5g[] = {
	{.bitrate = 60, .hw_value = 0x04,},
	{.bitrate = 90, .hw_value = 0x05,},
	{.bitrate = 120, .hw_value = 0x06,},
	{.bitrate = 180, .hw_value = 0x07,},
	{.bitrate = 240, .hw_value = 0x08,},
	{.bitrate = 360, .hw_value = 0x09,},
	{.bitrate = 480, .hw_value = 0x0a,},
	{.bitrate = 540, .hw_value = 0x0b,},
};

static const struct ieee80211_supported_band rtl_band_2ghz = {
	.band = IEEE80211_BAND_2GHZ,

	.channels = rtl_channeltable_2g,
	.n_channels = ARRAY_SIZE(rtl_channeltable_2g),

	.bitrates = rtl_ratetable_2g,
	.n_bitrates = ARRAY_SIZE(rtl_ratetable_2g),

	.ht_cap = {0},
};

static struct ieee80211_supported_band rtl_band_5ghz = {
	.band = IEEE80211_BAND_5GHZ,

	.channels = rtl_channeltable_5g,
	.n_channels = ARRAY_SIZE(rtl_channeltable_5g),

	.bitrates = rtl_ratetable_5g,
	.n_bitrates = ARRAY_SIZE(rtl_ratetable_5g),

	.ht_cap = {0},
};

static const u8 tid_to_ac[] = {
	2, /* IEEE80211_AC_BE */
	3, /* IEEE80211_AC_BK */
	3, /* IEEE80211_AC_BK */
	2, /* IEEE80211_AC_BE */
	1, /* IEEE80211_AC_VI */
	1, /* IEEE80211_AC_VI */
	0, /* IEEE80211_AC_VO */
	0, /* IEEE80211_AC_VO */
};

u8 rtl_tid_to_ac(struct ieee80211_hw *hw, u8 tid)
{
	return tid_to_ac[tid];
}

static void _rtl_init_hw_ht_capab(struct ieee80211_hw *hw,
				  struct ieee80211_sta_ht_cap *ht_cap)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	ht_cap->ht_supported = true;
	ht_cap->cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
	    IEEE80211_HT_CAP_SGI_40 |
	    IEEE80211_HT_CAP_SGI_20 |
	    IEEE80211_HT_CAP_DSSSCCK40 | IEEE80211_HT_CAP_MAX_AMSDU;

	if (rtlpriv->rtlhal.disable_amsdu_8k)
		ht_cap->cap &= ~IEEE80211_HT_CAP_MAX_AMSDU;

	/*
	 *Maximum length of AMPDU that the STA can receive.
	 *Length = 2 ^ (13 + max_ampdu_length_exp) - 1 (octets)
	 */
	ht_cap->ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;

	/*Minimum MPDU start spacing , */
	ht_cap->ampdu_density = IEEE80211_HT_MPDU_DENSITY_16;

	ht_cap->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;

	/*
	 *hw->wiphy->bands[IEEE80211_BAND_2GHZ]
	 *base on ant_num
	 *rx_mask: RX mask
	 *if rx_ant =1 rx_mask[0]=0xff;==>MCS0-MCS7
	 *if rx_ant =2 rx_mask[1]=0xff;==>MCS8-MCS15
	 *if rx_ant >=3 rx_mask[2]=0xff;
	 *if BW_40 rx_mask[4]=0x01;
	 *highest supported RX rate
	 */
	if (get_rf_type(rtlphy) == RF_1T2R || get_rf_type(rtlphy) == RF_2T2R) {

		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, ("1T2R or 2T2R\n"));

		ht_cap->mcs.rx_mask[0] = 0xFF;
		ht_cap->mcs.rx_mask[1] = 0xFF;
		ht_cap->mcs.rx_mask[4] = 0x01;

		ht_cap->mcs.rx_highest = cpu_to_le16(MAX_BIT_RATE_40MHZ_MCS15);
	} else if (get_rf_type(rtlphy) == RF_1T1R) {

		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, ("1T1R\n"));

		ht_cap->mcs.rx_mask[0] = 0xFF;
		ht_cap->mcs.rx_mask[1] = 0x00;
		ht_cap->mcs.rx_mask[4] = 0x01;

		ht_cap->mcs.rx_highest = cpu_to_le16(MAX_BIT_RATE_40MHZ_MCS7);
	}
}

static void _rtl_init_mac80211(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct rtl_mac *rtlmac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct ieee80211_supported_band *sband;


	if (rtlhal->macphymode == SINGLEMAC_SINGLEPHY && rtlhal->bandset ==
	    BAND_ON_BOTH) {
		/* 1: 2.4 G bands */
		/* <1> use  mac->bands as mem for hw->wiphy->bands */
		sband = &(rtlmac->bands[IEEE80211_BAND_2GHZ]);

		/* <2> set hw->wiphy->bands[IEEE80211_BAND_2GHZ]
		 * to default value(1T1R) */
		memcpy(&(rtlmac->bands[IEEE80211_BAND_2GHZ]), &rtl_band_2ghz,
				sizeof(struct ieee80211_supported_band));

		/* <3> init ht cap base on ant_num */
		_rtl_init_hw_ht_capab(hw, &sband->ht_cap);

		/* <4> set mac->sband to wiphy->sband */
		hw->wiphy->bands[IEEE80211_BAND_2GHZ] = sband;

		/* 2: 5 G bands */
		/* <1> use  mac->bands as mem for hw->wiphy->bands */
		sband = &(rtlmac->bands[IEEE80211_BAND_5GHZ]);

		/* <2> set hw->wiphy->bands[IEEE80211_BAND_5GHZ]
		 * to default value(1T1R) */
		memcpy(&(rtlmac->bands[IEEE80211_BAND_5GHZ]), &rtl_band_5ghz,
				sizeof(struct ieee80211_supported_band));

		/* <3> init ht cap base on ant_num */
		_rtl_init_hw_ht_capab(hw, &sband->ht_cap);

		/* <4> set mac->sband to wiphy->sband */
		hw->wiphy->bands[IEEE80211_BAND_5GHZ] = sband;
	} else {
		if (rtlhal->current_bandtype == BAND_ON_2_4G) {
			/* <1> use  mac->bands as mem for hw->wiphy->bands */
			sband = &(rtlmac->bands[IEEE80211_BAND_2GHZ]);

			/* <2> set hw->wiphy->bands[IEEE80211_BAND_2GHZ]
			 * to default value(1T1R) */
			memcpy(&(rtlmac->bands[IEEE80211_BAND_2GHZ]),
				 &rtl_band_2ghz,
				 sizeof(struct ieee80211_supported_band));

			/* <3> init ht cap base on ant_num */
			_rtl_init_hw_ht_capab(hw, &sband->ht_cap);

			/* <4> set mac->sband to wiphy->sband */
			hw->wiphy->bands[IEEE80211_BAND_2GHZ] = sband;
		} else if (rtlhal->current_bandtype == BAND_ON_5G) {
			/* <1> use  mac->bands as mem for hw->wiphy->bands */
			sband = &(rtlmac->bands[IEEE80211_BAND_5GHZ]);

			/* <2> set hw->wiphy->bands[IEEE80211_BAND_5GHZ]
			 * to default value(1T1R) */
			memcpy(&(rtlmac->bands[IEEE80211_BAND_5GHZ]),
				 &rtl_band_5ghz,
				 sizeof(struct ieee80211_supported_band));

			/* <3> init ht cap base on ant_num */
			_rtl_init_hw_ht_capab(hw, &sband->ht_cap);

			/* <4> set mac->sband to wiphy->sband */
			hw->wiphy->bands[IEEE80211_BAND_5GHZ] = sband;
		} else {
			RT_TRACE(rtlpriv, COMP_INIT, DBG_EMERG,
				 ("Err BAND %d\n",
				 rtlhal->current_bandtype));
		}
	}
	/* <5> set hw caps */
	hw->flags = IEEE80211_HW_SIGNAL_DBM |
	    IEEE80211_HW_RX_INCLUDES_FCS |
	    IEEE80211_HW_BEACON_FILTER |
	    IEEE80211_HW_AMPDU_AGGREGATION |
	    IEEE80211_HW_REPORTS_TX_ACK_STATUS | 0;

	/* swlps or hwlps has been set in diff chip in init_sw_vars */
	if (rtlpriv->psc.swctrl_lps)
		hw->flags |= IEEE80211_HW_SUPPORTS_PS |
			IEEE80211_HW_PS_NULLFUNC_STACK |
			/* IEEE80211_HW_SUPPORTS_DYNAMIC_PS | */
			0;

	hw->wiphy->interface_modes =
	    BIT(NL80211_IFTYPE_AP) |
	    BIT(NL80211_IFTYPE_STATION) |
	    BIT(NL80211_IFTYPE_ADHOC);

	hw->wiphy->rts_threshold = 2347;

	hw->queues = AC_MAX;
	hw->extra_tx_headroom = RTL_TX_HEADER_SIZE;

	/* TODO: Correct this value for our hw */
	/* TODO: define these hard code value */
	hw->channel_change_time = 100;
	hw->max_listen_interval = 10;
	hw->max_rate_tries = 4;
	/* hw->max_rates = 1; */
	hw->sta_data_size = sizeof(struct rtl_sta_info);

	/* <6> mac address */
	if (is_valid_ether_addr(rtlefuse->dev_addr)) {
		SET_IEEE80211_PERM_ADDR(hw, rtlefuse->dev_addr);
	} else {
		u8 rtlmac[] = { 0x00, 0xe0, 0x4c, 0x81, 0x92, 0x00 };
		get_random_bytes((rtlmac + (ETH_ALEN - 1)), 1);
		SET_IEEE80211_PERM_ADDR(hw, rtlmac);
	}

}

static void _rtl_init_deferred_work(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	/* <1> timer */
	init_timer(&rtlpriv->works.watchdog_timer);
	setup_timer(&rtlpriv->works.watchdog_timer,
		    rtl_watch_dog_timer_callback, (unsigned long)hw);

	/* <2> work queue */
	rtlpriv->works.hw = hw;
	rtlpriv->works.rtl_wq = alloc_workqueue(rtlpriv->cfg->name, 0, 0);
	INIT_DELAYED_WORK(&rtlpriv->works.watchdog_wq,
			  (void *)rtl_watchdog_wq_callback);
	INIT_DELAYED_WORK(&rtlpriv->works.ips_nic_off_wq,
			  (void *)rtl_ips_nic_off_wq_callback);
	INIT_DELAYED_WORK(&rtlpriv->works.ps_work,
			  (void *)rtl_swlps_wq_callback);
	INIT_DELAYED_WORK(&rtlpriv->works.ps_rfon_wq,
			  (void *)rtl_swlps_rfon_wq_callback);

}

void rtl_deinit_deferred_work(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	del_timer_sync(&rtlpriv->works.watchdog_timer);

	cancel_delayed_work(&rtlpriv->works.watchdog_wq);
	cancel_delayed_work(&rtlpriv->works.ips_nic_off_wq);
	cancel_delayed_work(&rtlpriv->works.ps_work);
	cancel_delayed_work(&rtlpriv->works.ps_rfon_wq);
}

void rtl_init_rfkill(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	bool radio_state;
	bool blocked;
	u8 valid = 0;

	/*set init state to on */
	rtlpriv->rfkill.rfkill_state = 1;
	wiphy_rfkill_set_hw_state(hw->wiphy, 0);

	radio_state = rtlpriv->cfg->ops->radio_onoff_checking(hw, &valid);

	if (valid) {
		printk(KERN_INFO "rtlwifi: wireless switch is %s\n",
				rtlpriv->rfkill.rfkill_state ? "on" : "off");

		rtlpriv->rfkill.rfkill_state = radio_state;

		blocked = (rtlpriv->rfkill.rfkill_state == 1) ? 0 : 1;
		wiphy_rfkill_set_hw_state(hw->wiphy, blocked);
	}

	wiphy_rfkill_start_polling(hw->wiphy);
}

void rtl_deinit_rfkill(struct ieee80211_hw *hw)
{
	wiphy_rfkill_stop_polling(hw->wiphy);
}

int rtl_init_core(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *rtlmac = rtl_mac(rtl_priv(hw));

	/* <1> init mac80211 */
	_rtl_init_mac80211(hw);
	rtlmac->hw = hw;

	/* <2> rate control register */
	hw->rate_control_algorithm = "rtl_rc";

	/*
	 * <3> init CRDA must come after init
	 * mac80211 hw  in _rtl_init_mac80211.
	 */
	if (rtl_regd_init(hw, rtl_reg_notifier)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, ("REGD init failed\n"));
		return 1;
	} else {
		/* CRDA regd hint must after init CRDA */
		if (regulatory_hint(hw->wiphy, rtlpriv->regd.alpha2)) {
			RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
				 ("regulatory_hint fail\n"));
		}
	}

	/* <4> locks */
	mutex_init(&rtlpriv->locks.conf_mutex);
	spin_lock_init(&rtlpriv->locks.ips_lock);
	spin_lock_init(&rtlpriv->locks.irq_th_lock);
	spin_lock_init(&rtlpriv->locks.h2c_lock);
	spin_lock_init(&rtlpriv->locks.rf_ps_lock);
	spin_lock_init(&rtlpriv->locks.rf_lock);
	spin_lock_init(&rtlpriv->locks.lps_lock);
	spin_lock_init(&rtlpriv->locks.waitq_lock);
	spin_lock_init(&rtlpriv->locks.cck_and_rw_pagea_lock);

	rtlmac->link_state = MAC80211_NOLINK;

	/* <5> init deferred work */
	_rtl_init_deferred_work(hw);

	return 0;
}

void rtl_deinit_core(struct ieee80211_hw *hw)
{
}

void rtl_init_rx_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_RCR, (u8 *) (&mac->rx_conf));
}

/*********************************************************
 *
 * tx information functions
 *
 *********************************************************/
static void _rtl_qurey_shortpreamble_mode(struct ieee80211_hw *hw,
					  struct rtl_tcb_desc *tcb_desc,
					  struct ieee80211_tx_info *info)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 rate_flag = info->control.rates[0].flags;

	tcb_desc->use_shortpreamble = false;

	/* 1M can only use Long Preamble. 11B spec */
	if (tcb_desc->hw_rate == rtlpriv->cfg->maps[RTL_RC_CCK_RATE1M])
		return;
	else if (rate_flag & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
		tcb_desc->use_shortpreamble = true;

	return;
}

static void _rtl_query_shortgi(struct ieee80211_hw *hw,
			       struct ieee80211_sta *sta,
			       struct rtl_tcb_desc *tcb_desc,
			       struct ieee80211_tx_info *info)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u8 rate_flag = info->control.rates[0].flags;
	u8 sgi_40 = 0, sgi_20 = 0, bw_40 = 0;
	tcb_desc->use_shortgi = false;

	if (sta == NULL)
		return;

	sgi_40 = sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40;
	sgi_20 = sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20;

	if (!(sta->ht_cap.ht_supported))
		return;

	if (!sgi_40 && !sgi_20)
		return;

	if (mac->opmode == NL80211_IFTYPE_STATION)
		bw_40 = mac->bw_40;
	else if (mac->opmode == NL80211_IFTYPE_AP ||
		mac->opmode == NL80211_IFTYPE_ADHOC)
		bw_40 = sta->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40;

	if ((bw_40 == true) && sgi_40)
		tcb_desc->use_shortgi = true;
	else if ((bw_40 == false) && sgi_20)
		tcb_desc->use_shortgi = true;

	if (!(rate_flag & IEEE80211_TX_RC_SHORT_GI))
		tcb_desc->use_shortgi = false;
}

static void _rtl_query_protection_mode(struct ieee80211_hw *hw,
				       struct rtl_tcb_desc *tcb_desc,
				       struct ieee80211_tx_info *info)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u8 rate_flag = info->control.rates[0].flags;

	/* Common Settings */
	tcb_desc->rts_stbc = false;
	tcb_desc->cts_enable = false;
	tcb_desc->rts_sc = 0;
	tcb_desc->rts_bw = false;
	tcb_desc->rts_use_shortpreamble = false;
	tcb_desc->rts_use_shortgi = false;

	if (rate_flag & IEEE80211_TX_RC_USE_CTS_PROTECT) {
		/* Use CTS-to-SELF in protection mode. */
		tcb_desc->rts_enable = true;
		tcb_desc->cts_enable = true;
		tcb_desc->rts_rate = rtlpriv->cfg->maps[RTL_RC_OFDM_RATE24M];
	} else if (rate_flag & IEEE80211_TX_RC_USE_RTS_CTS) {
		/* Use RTS-CTS in protection mode. */
		tcb_desc->rts_enable = true;
		tcb_desc->rts_rate = rtlpriv->cfg->maps[RTL_RC_OFDM_RATE24M];
	}
}

static void _rtl_txrate_selectmode(struct ieee80211_hw *hw,
				   struct ieee80211_sta *sta,
				   struct rtl_tcb_desc *tcb_desc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_sta_info *sta_entry = NULL;
	u8 ratr_index = 7;

	if (sta) {
		sta_entry = (struct rtl_sta_info *) sta->drv_priv;
		ratr_index = sta_entry->ratr_index;
	}
	if (!tcb_desc->disable_ratefallback || !tcb_desc->use_driver_rate) {
		if (mac->opmode == NL80211_IFTYPE_STATION) {
			tcb_desc->ratr_index = 0;
		} else if (mac->opmode == NL80211_IFTYPE_ADHOC) {
			if (tcb_desc->multicast || tcb_desc->broadcast) {
				tcb_desc->hw_rate =
				    rtlpriv->cfg->maps[RTL_RC_CCK_RATE2M];
				tcb_desc->use_driver_rate = 1;
			} else {
				/* TODO */
			}
			tcb_desc->ratr_index = ratr_index;
		} else if (mac->opmode == NL80211_IFTYPE_AP) {
			tcb_desc->ratr_index = ratr_index;
		}
	}

	if (rtlpriv->dm.useramask) {
		/* TODO we will differentiate adhoc and station futrue  */
		if (mac->opmode == NL80211_IFTYPE_STATION) {
			tcb_desc->mac_id = 0;

			if (mac->mode == WIRELESS_MODE_N_24G)
				tcb_desc->ratr_index = RATR_INX_WIRELESS_NGB;
			else if (mac->mode == WIRELESS_MODE_N_5G)
				tcb_desc->ratr_index = RATR_INX_WIRELESS_NG;
			else if (mac->mode & WIRELESS_MODE_G)
				tcb_desc->ratr_index = RATR_INX_WIRELESS_GB;
			else if (mac->mode & WIRELESS_MODE_B)
				tcb_desc->ratr_index = RATR_INX_WIRELESS_B;
			else if (mac->mode & WIRELESS_MODE_A)
				tcb_desc->ratr_index = RATR_INX_WIRELESS_G;
		} else if (mac->opmode == NL80211_IFTYPE_AP ||
			mac->opmode == NL80211_IFTYPE_ADHOC) {
			if (NULL != sta) {
				if (sta->aid > 0)
					tcb_desc->mac_id = sta->aid + 1;
				else
					tcb_desc->mac_id = 1;
			} else {
				tcb_desc->mac_id = 0;
			}
		}
	}

}

static void _rtl_query_bandwidth_mode(struct ieee80211_hw *hw,
				      struct ieee80211_sta *sta,
				      struct rtl_tcb_desc *tcb_desc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	tcb_desc->packet_bw = false;
	if (!sta)
		return;
	if (mac->opmode == NL80211_IFTYPE_AP ||
	    mac->opmode == NL80211_IFTYPE_ADHOC) {
		if (!(sta->ht_cap.ht_supported) ||
		    !(sta->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40))
			return;
	} else if (mac->opmode == NL80211_IFTYPE_STATION) {
		if (!mac->bw_40 || !(sta->ht_cap.ht_supported))
			return;
	}
	if (tcb_desc->multicast || tcb_desc->broadcast)
		return;

	/*use legency rate, shall use 20MHz */
	if (tcb_desc->hw_rate <= rtlpriv->cfg->maps[RTL_RC_OFDM_RATE54M])
		return;

	tcb_desc->packet_bw = true;
}

static u8 _rtl_get_highest_n_rate(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	u8 hw_rate;

	if (get_rf_type(rtlphy) == RF_2T2R)
		hw_rate = rtlpriv->cfg->maps[RTL_RC_HT_RATEMCS15];
	else
		hw_rate = rtlpriv->cfg->maps[RTL_RC_HT_RATEMCS7];

	return hw_rate;
}

void rtl_get_tcb_desc(struct ieee80211_hw *hw,
		      struct ieee80211_tx_info *info,
		      struct ieee80211_sta *sta,
		      struct sk_buff *skb, struct rtl_tcb_desc *tcb_desc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *rtlmac = rtl_mac(rtl_priv(hw));
	struct ieee80211_hdr *hdr = rtl_get_hdr(skb);
	struct ieee80211_rate *txrate;
	__le16 fc = hdr->frame_control;

	txrate = ieee80211_get_tx_rate(hw, info);
	tcb_desc->hw_rate = txrate->hw_value;

	if (ieee80211_is_data(fc)) {
		/*
		 *we set data rate INX 0
		 *in rtl_rc.c   if skb is special data or
		 *mgt which need low data rate.
		 */

		/*
		 *So tcb_desc->hw_rate is just used for
		 *special data and mgt frames
		 */
		if (info->control.rates[0].idx == 0 &&
				ieee80211_is_nullfunc(fc)) {
			tcb_desc->use_driver_rate = true;
			tcb_desc->ratr_index = RATR_INX_WIRELESS_MC;

			tcb_desc->disable_ratefallback = 1;
		} else {
			/*
			 *because hw will nerver use hw_rate
			 *when tcb_desc->use_driver_rate = false
			 *so we never set highest N rate here,
			 *and N rate will all be controlled by FW
			 *when tcb_desc->use_driver_rate = false
			 */
			if (sta && (sta->ht_cap.ht_supported)) {
				tcb_desc->hw_rate = _rtl_get_highest_n_rate(hw);
			} else {
				if (rtlmac->mode == WIRELESS_MODE_B) {
					tcb_desc->hw_rate =
					   rtlpriv->cfg->maps[RTL_RC_CCK_RATE11M];
				} else {
					tcb_desc->hw_rate =
					   rtlpriv->cfg->maps[RTL_RC_OFDM_RATE54M];
				}
			}
		}

		if (is_multicast_ether_addr(ieee80211_get_DA(hdr)))
			tcb_desc->multicast = 1;
		else if (is_broadcast_ether_addr(ieee80211_get_DA(hdr)))
			tcb_desc->broadcast = 1;

		_rtl_txrate_selectmode(hw, sta, tcb_desc);
		_rtl_query_bandwidth_mode(hw, sta, tcb_desc);
		_rtl_qurey_shortpreamble_mode(hw, tcb_desc, info);
		_rtl_query_shortgi(hw, sta, tcb_desc, info);
		_rtl_query_protection_mode(hw, tcb_desc, info);
	} else {
		tcb_desc->use_driver_rate = true;
		tcb_desc->ratr_index = RATR_INX_WIRELESS_MC;
		tcb_desc->disable_ratefallback = 1;
		tcb_desc->mac_id = 0;
		tcb_desc->packet_bw = false;
	}
}
EXPORT_SYMBOL(rtl_get_tcb_desc);

bool rtl_action_proc(struct ieee80211_hw *hw, struct sk_buff *skb, u8 is_tx)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct ieee80211_hdr *hdr = rtl_get_hdr(skb);
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	__le16 fc = hdr->frame_control;
	u8 *act = (u8 *) (((u8 *) skb->data + MAC80211_3ADDR_LEN));
	u8 category;

	if (!ieee80211_is_action(fc))
		return true;

	category = *act;
	act++;
	switch (category) {
	case ACT_CAT_BA:
		switch (*act) {
		case ACT_ADDBAREQ:
			if (mac->act_scanning)
				return false;

			RT_TRACE(rtlpriv, (COMP_SEND | COMP_RECV), DBG_DMESG,
				 ("%s ACT_ADDBAREQ From :" MAC_FMT "\n",
				  is_tx ? "Tx" : "Rx", MAC_ARG(hdr->addr2)));
			break;
		case ACT_ADDBARSP:
			RT_TRACE(rtlpriv, (COMP_SEND | COMP_RECV), DBG_DMESG,
				 ("%s ACT_ADDBARSP From :" MAC_FMT "\n",
				  is_tx ? "Tx" : "Rx", MAC_ARG(hdr->addr2)));
			break;
		case ACT_DELBA:
			RT_TRACE(rtlpriv, (COMP_SEND | COMP_RECV), DBG_DMESG,
				 ("ACT_ADDBADEL From :" MAC_FMT "\n",
				  MAC_ARG(hdr->addr2)));
			break;
		}
		break;
	default:
		break;
	}

	return true;
}

/*should call before software enc*/
u8 rtl_is_special_data(struct ieee80211_hw *hw, struct sk_buff *skb, u8 is_tx)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	__le16 fc = rtl_get_fc(skb);
	u16 ether_type;
	u8 mac_hdr_len = ieee80211_get_hdrlen_from_skb(skb);
	const struct iphdr *ip;

	if (!ieee80211_is_data(fc))
		return false;


	ip = (struct iphdr *)((u8 *) skb->data + mac_hdr_len +
			      SNAP_SIZE + PROTOC_TYPE_SIZE);
	ether_type = *(u16 *) ((u8 *) skb->data + mac_hdr_len + SNAP_SIZE);
	/*	ether_type = ntohs(ether_type); */

	if (ETH_P_IP == ether_type) {
		if (IPPROTO_UDP == ip->protocol) {
			struct udphdr *udp = (struct udphdr *)((u8 *) ip +
							       (ip->ihl << 2));
			if (((((u8 *) udp)[1] == 68) &&
			     (((u8 *) udp)[3] == 67)) ||
			    ((((u8 *) udp)[1] == 67) &&
			     (((u8 *) udp)[3] == 68))) {
				/*
				 * 68 : UDP BOOTP client
				 * 67 : UDP BOOTP server
				 */
				RT_TRACE(rtlpriv, (COMP_SEND | COMP_RECV),
					 DBG_DMESG, ("dhcp %s !!\n",
						     (is_tx) ? "Tx" : "Rx"));

				if (is_tx) {
					rtl_lps_leave(hw);
					ppsc->last_delaylps_stamp_jiffies =
					    jiffies;
				}

				return true;
			}
		}
	} else if (ETH_P_ARP == ether_type) {
		if (is_tx) {
			rtl_lps_leave(hw);
			ppsc->last_delaylps_stamp_jiffies = jiffies;
		}

		return true;
	} else if (ETH_P_PAE == ether_type) {
		RT_TRACE(rtlpriv, (COMP_SEND | COMP_RECV), DBG_DMESG,
			 ("802.1X %s EAPOL pkt!!\n", (is_tx) ? "Tx" : "Rx"));

		if (is_tx) {
			rtl_lps_leave(hw);
			ppsc->last_delaylps_stamp_jiffies = jiffies;
		}

		return true;
	} else if (ETH_P_IPV6 == ether_type) {
		/* IPv6 */
		return true;
	}

	return false;
}

/*********************************************************
 *
 * functions called by core.c
 *
 *********************************************************/
int rtl_tx_agg_start(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta, u16 tid, u16 *ssn)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_tid_data *tid_data;
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_sta_info *sta_entry = NULL;

	if (sta == NULL)
		return -EINVAL;

	if (unlikely(tid >= MAX_TID_COUNT))
		return -EINVAL;

	sta_entry = (struct rtl_sta_info *)sta->drv_priv;
	if (!sta_entry)
		return -ENXIO;
	tid_data = &sta_entry->tids[tid];

	RT_TRACE(rtlpriv, COMP_SEND, DBG_DMESG,
		 ("on ra = %pM tid = %d seq:%d\n", sta->addr, tid,
		 tid_data->seq_number));

	*ssn = tid_data->seq_number;
	tid_data->agg.agg_state = RTL_AGG_START;

	ieee80211_start_tx_ba_cb_irqsafe(mac->vif, sta->addr, tid);

	return 0;
}

int rtl_tx_agg_stop(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta, u16 tid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_tid_data *tid_data;
	struct rtl_sta_info *sta_entry = NULL;

	if (sta == NULL)
		return -EINVAL;

	if (!sta->addr) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, ("ra = NULL\n"));
		return -EINVAL;
	}

	RT_TRACE(rtlpriv, COMP_SEND, DBG_DMESG,
		 ("on ra = %pM tid = %d\n", sta->addr, tid));

	if (unlikely(tid >= MAX_TID_COUNT))
		return -EINVAL;

	sta_entry = (struct rtl_sta_info *)sta->drv_priv;
	tid_data = &sta_entry->tids[tid];
	sta_entry->tids[tid].agg.agg_state = RTL_AGG_STOP;

	ieee80211_stop_tx_ba_cb_irqsafe(mac->vif, sta->addr, tid);

	return 0;
}

int rtl_tx_agg_oper(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta, u16 tid)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_tid_data *tid_data;
	struct rtl_sta_info *sta_entry = NULL;

	if (sta == NULL)
		return -EINVAL;

	if (!sta->addr) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, ("ra = NULL\n"));
		return -EINVAL;
	}

	RT_TRACE(rtlpriv, COMP_SEND, DBG_DMESG,
		 ("on ra = %pM tid = %d\n", sta->addr, tid));

	if (unlikely(tid >= MAX_TID_COUNT))
		return -EINVAL;

	sta_entry = (struct rtl_sta_info *)sta->drv_priv;
	tid_data = &sta_entry->tids[tid];
	sta_entry->tids[tid].agg.agg_state = RTL_AGG_OPERATIONAL;

	return 0;
}

/*********************************************************
 *
 * wq & timer callback functions
 *
 *********************************************************/
void rtl_watchdog_wq_callback(void *data)
{
	struct rtl_works *rtlworks = container_of_dwork_rtl(data,
							    struct rtl_works,
							    watchdog_wq);
	struct ieee80211_hw *hw = rtlworks->hw;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	bool busytraffic = false;
	bool higher_busytraffic = false;
	bool higher_busyrxtraffic = false;
	u8 idx, tid;
	u32 rx_cnt_inp4eriod = 0;
	u32 tx_cnt_inp4eriod = 0;
	u32 aver_rx_cnt_inperiod = 0;
	u32 aver_tx_cnt_inperiod = 0;
	u32 aver_tidtx_inperiod[MAX_TID_COUNT] = {0};
	u32 tidtx_inp4eriod[MAX_TID_COUNT] = {0};
	bool enter_ps = false;

	if (is_hal_stop(rtlhal))
		return;

	/* <1> Determine if action frame is allowed */
	if (mac->link_state > MAC80211_NOLINK) {
		if (mac->cnt_after_linked < 20)
			mac->cnt_after_linked++;
	} else {
		mac->cnt_after_linked = 0;
	}

	/*
	 *<3> to check if traffic busy, if
	 * busytraffic we don't change channel
	 */
	if (mac->link_state >= MAC80211_LINKED) {

		/* (1) get aver_rx_cnt_inperiod & aver_tx_cnt_inperiod */
		for (idx = 0; idx <= 2; idx++) {
			rtlpriv->link_info.num_rx_in4period[idx] =
			    rtlpriv->link_info.num_rx_in4period[idx + 1];
			rtlpriv->link_info.num_tx_in4period[idx] =
			    rtlpriv->link_info.num_tx_in4period[idx + 1];
		}
		rtlpriv->link_info.num_rx_in4period[3] =
		    rtlpriv->link_info.num_rx_inperiod;
		rtlpriv->link_info.num_tx_in4period[3] =
		    rtlpriv->link_info.num_tx_inperiod;
		for (idx = 0; idx <= 3; idx++) {
			rx_cnt_inp4eriod +=
			    rtlpriv->link_info.num_rx_in4period[idx];
			tx_cnt_inp4eriod +=
			    rtlpriv->link_info.num_tx_in4period[idx];
		}
		aver_rx_cnt_inperiod = rx_cnt_inp4eriod / 4;
		aver_tx_cnt_inperiod = tx_cnt_inp4eriod / 4;

		/* (2) check traffic busy */
		if (aver_rx_cnt_inperiod > 100 || aver_tx_cnt_inperiod > 100)
			busytraffic = true;

		/* Higher Tx/Rx data. */
		if (aver_rx_cnt_inperiod > 4000 ||
		    aver_tx_cnt_inperiod > 4000) {
			higher_busytraffic = true;

			/* Extremely high Rx data. */
			if (aver_rx_cnt_inperiod > 5000)
				higher_busyrxtraffic = true;
		}

		/* check every tid's tx traffic */
		for (tid = 0; tid <= 7; tid++) {
			for (idx = 0; idx <= 2; idx++)
				rtlpriv->link_info.tidtx_in4period[tid][idx] =
				  rtlpriv->link_info.tidtx_in4period[tid]
				  [idx + 1];
			rtlpriv->link_info.tidtx_in4period[tid][3] =
				rtlpriv->link_info.tidtx_inperiod[tid];

			for (idx = 0; idx <= 3; idx++)
				tidtx_inp4eriod[tid] +=
				  rtlpriv->link_info.tidtx_in4period[tid][idx];
			aver_tidtx_inperiod[tid] = tidtx_inp4eriod[tid] / 4;
			if (aver_tidtx_inperiod[tid] > 5000)
				rtlpriv->link_info.higher_busytxtraffic[tid] =
						   true;
			else
				rtlpriv->link_info.higher_busytxtraffic[tid] =
						   false;
		}

		if (((rtlpriv->link_info.num_rx_inperiod +
		      rtlpriv->link_info.num_tx_inperiod) > 8) ||
		    (rtlpriv->link_info.num_rx_inperiod > 2))
			enter_ps = false;
		else
			enter_ps = true;

		/* LeisurePS only work in infra mode. */
		if (enter_ps)
			rtl_lps_enter(hw);
		else
			rtl_lps_leave(hw);
	}

	rtlpriv->link_info.num_rx_inperiod = 0;
	rtlpriv->link_info.num_tx_inperiod = 0;
	for (tid = 0; tid <= 7; tid++)
		rtlpriv->link_info.tidtx_inperiod[tid] = 0;

	rtlpriv->link_info.busytraffic = busytraffic;
	rtlpriv->link_info.higher_busytraffic = higher_busytraffic;
	rtlpriv->link_info.higher_busyrxtraffic = higher_busyrxtraffic;

	/* <3> DM */
	rtlpriv->cfg->ops->dm_watchdog(hw);
}

void rtl_watch_dog_timer_callback(unsigned long data)
{
	struct ieee80211_hw *hw = (struct ieee80211_hw *)data;
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	queue_delayed_work(rtlpriv->works.rtl_wq,
			   &rtlpriv->works.watchdog_wq, 0);

	mod_timer(&rtlpriv->works.watchdog_timer,
		  jiffies + MSECS(RTL_WATCH_DOG_TIME));
}

/*********************************************************
 *
 * frame process functions
 *
 *********************************************************/
u8 *rtl_find_ie(u8 *data, unsigned int len, u8 ie)
{
	struct ieee80211_mgmt *mgmt = (void *)data;
	u8 *pos, *end;

	pos = (u8 *)mgmt->u.beacon.variable;
	end = data + len;
	while (pos < end) {
		if (pos + 2 + pos[1] > end)
			return NULL;

		if (pos[0] == ie)
			return pos;

		pos += 2 + pos[1];
	}
	return NULL;
}

/* when we use 2 rx ants we send IEEE80211_SMPS_OFF */
/* when we use 1 rx ant we send IEEE80211_SMPS_STATIC */
static struct sk_buff *rtl_make_smps_action(struct ieee80211_hw *hw,
		enum ieee80211_smps_mode smps, u8 *da, u8 *bssid)
{
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct sk_buff *skb;
	struct ieee80211_mgmt *action_frame;

	/* 27 = header + category + action + smps mode */
	skb = dev_alloc_skb(27 + hw->extra_tx_headroom);
	if (!skb)
		return NULL;

	skb_reserve(skb, hw->extra_tx_headroom);
	action_frame = (void *)skb_put(skb, 27);
	memset(action_frame, 0, 27);
	memcpy(action_frame->da, da, ETH_ALEN);
	memcpy(action_frame->sa, rtlefuse->dev_addr, ETH_ALEN);
	memcpy(action_frame->bssid, bssid, ETH_ALEN);
	action_frame->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
						  IEEE80211_STYPE_ACTION);
	action_frame->u.action.category = WLAN_CATEGORY_HT;
	action_frame->u.action.u.ht_smps.action = WLAN_HT_ACTION_SMPS;
	switch (smps) {
	case IEEE80211_SMPS_AUTOMATIC:/* 0 */
	case IEEE80211_SMPS_NUM_MODES:/* 4 */
		WARN_ON(1);
	case IEEE80211_SMPS_OFF:/* 1 */ /*MIMO_PS_NOLIMIT*/
		action_frame->u.action.u.ht_smps.smps_control =
				WLAN_HT_SMPS_CONTROL_DISABLED;/* 0 */
		break;
	case IEEE80211_SMPS_STATIC:/* 2 */ /*MIMO_PS_STATIC*/
		action_frame->u.action.u.ht_smps.smps_control =
				WLAN_HT_SMPS_CONTROL_STATIC;/* 1 */
		break;
	case IEEE80211_SMPS_DYNAMIC:/* 3 */ /*MIMO_PS_DYNAMIC*/
		action_frame->u.action.u.ht_smps.smps_control =
				WLAN_HT_SMPS_CONTROL_DYNAMIC;/* 3 */
		break;
	}

	return skb;
}

int rtl_send_smps_action(struct ieee80211_hw *hw,
		struct ieee80211_sta *sta, u8 *da, u8 *bssid,
		enum ieee80211_smps_mode smps)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct sk_buff *skb = rtl_make_smps_action(hw, smps, da, bssid);
	struct rtl_tcb_desc tcb_desc;
	memset(&tcb_desc, 0, sizeof(struct rtl_tcb_desc));

	if (rtlpriv->mac80211.act_scanning)
		goto err_free;

	if (!sta)
		goto err_free;

	if (unlikely(is_hal_stop(rtlhal) || ppsc->rfpwr_state != ERFON))
		goto err_free;

	if (!test_bit(RTL_STATUS_INTERFACE_START, &rtlpriv->status))
		goto err_free;

	/* this is a type = mgmt * stype = action frame */
	if (skb) {
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
		struct rtl_sta_info *sta_entry =
			(struct rtl_sta_info *) sta->drv_priv;
		sta_entry->mimo_ps = smps;
		rtlpriv->cfg->ops->update_rate_tbl(hw, sta, 0);

		info->control.rates[0].idx = 0;
		info->control.sta = sta;
		info->band = hw->conf.channel->band;
		rtlpriv->intf_ops->adapter_tx(hw, skb, &tcb_desc);
	}
err_free:
	return 0;
}

/*********************************************************
 *
 * IOT functions
 *
 *********************************************************/
static bool rtl_chk_vendor_ouisub(struct ieee80211_hw *hw,
		struct octet_string vendor_ie)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	bool matched = false;
	static u8 athcap_1[] = { 0x00, 0x03, 0x7F };
	static u8 athcap_2[] = { 0x00, 0x13, 0x74 };
	static u8 broadcap_1[] = { 0x00, 0x10, 0x18 };
	static u8 broadcap_2[] = { 0x00, 0x0a, 0xf7 };
	static u8 broadcap_3[] = { 0x00, 0x05, 0xb5 };
	static u8 racap[] = { 0x00, 0x0c, 0x43 };
	static u8 ciscocap[] = { 0x00, 0x40, 0x96 };
	static u8 marvcap[] = { 0x00, 0x50, 0x43 };

	if (memcmp(vendor_ie.octet, athcap_1, 3) == 0 ||
		memcmp(vendor_ie.octet, athcap_2, 3) == 0) {
		rtlpriv->mac80211.vendor = PEER_ATH;
		matched = true;
	} else if (memcmp(vendor_ie.octet, broadcap_1, 3) == 0 ||
		memcmp(vendor_ie.octet, broadcap_2, 3) == 0 ||
		memcmp(vendor_ie.octet, broadcap_3, 3) == 0) {
		rtlpriv->mac80211.vendor = PEER_BROAD;
		matched = true;
	} else if (memcmp(vendor_ie.octet, racap, 3) == 0) {
		rtlpriv->mac80211.vendor = PEER_RAL;
		matched = true;
	} else if (memcmp(vendor_ie.octet, ciscocap, 3) == 0) {
		rtlpriv->mac80211.vendor = PEER_CISCO;
		matched = true;
	} else if (memcmp(vendor_ie.octet, marvcap, 3) == 0) {
		rtlpriv->mac80211.vendor = PEER_MARV;
		matched = true;
	}

	return matched;
}

static bool rtl_find_221_ie(struct ieee80211_hw *hw, u8 *data,
		unsigned int len)
{
	struct ieee80211_mgmt *mgmt = (void *)data;
	struct octet_string vendor_ie;
	u8 *pos, *end;

	pos = (u8 *)mgmt->u.beacon.variable;
	end = data + len;
	while (pos < end) {
		if (pos[0] == 221) {
			vendor_ie.length = pos[1];
			vendor_ie.octet = &pos[2];
			if (rtl_chk_vendor_ouisub(hw, vendor_ie))
				return true;
		}

		if (pos + 2 + pos[1] > end)
			return false;

		pos += 2 + pos[1];
	}
	return false;
}

void rtl_recognize_peer(struct ieee80211_hw *hw, u8 *data, unsigned int len)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct ieee80211_hdr *hdr = (void *)data;
	u32 vendor = PEER_UNKNOWN;

	static u8 ap3_1[3] = { 0x00, 0x14, 0xbf };
	static u8 ap3_2[3] = { 0x00, 0x1a, 0x70 };
	static u8 ap3_3[3] = { 0x00, 0x1d, 0x7e };
	static u8 ap4_1[3] = { 0x00, 0x90, 0xcc };
	static u8 ap4_2[3] = { 0x00, 0x0e, 0x2e };
	static u8 ap4_3[3] = { 0x00, 0x18, 0x02 };
	static u8 ap4_4[3] = { 0x00, 0x17, 0x3f };
	static u8 ap4_5[3] = { 0x00, 0x1c, 0xdf };
	static u8 ap5_1[3] = { 0x00, 0x1c, 0xf0 };
	static u8 ap5_2[3] = { 0x00, 0x21, 0x91 };
	static u8 ap5_3[3] = { 0x00, 0x24, 0x01 };
	static u8 ap5_4[3] = { 0x00, 0x15, 0xe9 };
	static u8 ap5_5[3] = { 0x00, 0x17, 0x9A };
	static u8 ap5_6[3] = { 0x00, 0x18, 0xE7 };
	static u8 ap6_1[3] = { 0x00, 0x17, 0x94 };
	static u8 ap7_1[3] = { 0x00, 0x14, 0xa4 };

	if (mac->opmode != NL80211_IFTYPE_STATION)
		return;

	if (mac->link_state == MAC80211_NOLINK) {
		mac->vendor = PEER_UNKNOWN;
		return;
	}

	if (mac->cnt_after_linked > 2)
		return;

	/* check if this really is a beacon */
	if (!ieee80211_is_beacon(hdr->frame_control))
		return;

	/* min. beacon length + FCS_LEN */
	if (len <= 40 + FCS_LEN)
		return;

	/* and only beacons from the associated BSSID, please */
	if (compare_ether_addr(hdr->addr3, rtlpriv->mac80211.bssid))
		return;

	if (rtl_find_221_ie(hw, data, len))
		vendor = mac->vendor;

	if ((memcmp(mac->bssid, ap5_1, 3) == 0) ||
		(memcmp(mac->bssid, ap5_2, 3) == 0) ||
		(memcmp(mac->bssid, ap5_3, 3) == 0) ||
		(memcmp(mac->bssid, ap5_4, 3) == 0) ||
		(memcmp(mac->bssid, ap5_5, 3) == 0) ||
		(memcmp(mac->bssid, ap5_6, 3) == 0) ||
		vendor == PEER_ATH) {
		vendor = PEER_ATH;
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD, ("=>ath find\n"));
	} else if ((memcmp(mac->bssid, ap4_4, 3) == 0) ||
		(memcmp(mac->bssid, ap4_5, 3) == 0) ||
		(memcmp(mac->bssid, ap4_1, 3) == 0) ||
		(memcmp(mac->bssid, ap4_2, 3) == 0) ||
		(memcmp(mac->bssid, ap4_3, 3) == 0) ||
		vendor == PEER_RAL) {
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD, ("=>ral findn\n"));
		vendor = PEER_RAL;
	} else if (memcmp(mac->bssid, ap6_1, 3) == 0 ||
		vendor == PEER_CISCO) {
		vendor = PEER_CISCO;
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD, ("=>cisco find\n"));
	} else if ((memcmp(mac->bssid, ap3_1, 3) == 0) ||
		(memcmp(mac->bssid, ap3_2, 3) == 0) ||
		(memcmp(mac->bssid, ap3_3, 3) == 0) ||
		vendor == PEER_BROAD) {
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD, ("=>broad find\n"));
		vendor = PEER_BROAD;
	} else if (memcmp(mac->bssid, ap7_1, 3) == 0 ||
		vendor == PEER_MARV) {
		vendor = PEER_MARV;
		RT_TRACE(rtlpriv, COMP_MAC80211, DBG_LOUD, ("=>marv find\n"));
	}

	mac->vendor = vendor;
}

/*********************************************************
 *
 * sysfs functions
 *
 *********************************************************/
static ssize_t rtl_show_debug_level(struct device *d,
				    struct device_attribute *attr, char *buf)
{
	struct ieee80211_hw *hw = dev_get_drvdata(d);
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	return sprintf(buf, "0x%08X\n", rtlpriv->dbg.global_debuglevel);
}

static ssize_t rtl_store_debug_level(struct device *d,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct ieee80211_hw *hw = dev_get_drvdata(d);
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	unsigned long val;
	int ret;

	ret = strict_strtoul(buf, 0, &val);
	if (ret) {
		printk(KERN_DEBUG "%s is not in hex or decimal form.\n", buf);
	} else {
		rtlpriv->dbg.global_debuglevel = val;
		printk(KERN_DEBUG "debuglevel:%x\n",
		       rtlpriv->dbg.global_debuglevel);
	}

	return strnlen(buf, count);
}

static DEVICE_ATTR(debug_level, S_IWUSR | S_IRUGO,
		   rtl_show_debug_level, rtl_store_debug_level);

static struct attribute *rtl_sysfs_entries[] = {

	&dev_attr_debug_level.attr,

	NULL
};

/*
 * "name" is folder name witch will be
 * put in device directory like :
 * sys/devices/pci0000:00/0000:00:1c.4/
 * 0000:06:00.0/rtl_sysfs
 */
struct attribute_group rtl_attribute_group = {
	.name = "rtlsysfs",
	.attrs = rtl_sysfs_entries,
};

MODULE_AUTHOR("lizhaoming	<chaoming_li@realsil.com.cn>");
MODULE_AUTHOR("Realtek WlanFAE	<wlanfae@realtek.com>");
MODULE_AUTHOR("Larry Finger	<Larry.FInger@lwfinger.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek 802.11n PCI wireless core");

static int __init rtl_core_module_init(void)
{
	if (rtl_rate_control_register())
		printk(KERN_ERR "rtlwifi: Unable to register rtl_rc,"
		       "use default RC !!\n");

	return 0;
}

static void __exit rtl_core_module_exit(void)
{
	/*RC*/
	rtl_rate_control_unregister();
}

module_init(rtl_core_module_init);
module_exit(rtl_core_module_exit);
