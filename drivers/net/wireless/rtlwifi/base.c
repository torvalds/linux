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
 *6) sysfs functions
 *7) ...
 */

/*********************************************************
 *
 * mac80211 init functions
 *
 *********************************************************/
static struct ieee80211_channel rtl_channeltable[] = {
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

static struct ieee80211_rate rtl_ratetable[] = {
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

static const struct ieee80211_supported_band rtl_band_2ghz = {
	.band = IEEE80211_BAND_2GHZ,

	.channels = rtl_channeltable,
	.n_channels = ARRAY_SIZE(rtl_channeltable),

	.bitrates = rtl_ratetable,
	.n_bitrates = ARRAY_SIZE(rtl_ratetable),

	.ht_cap = {0},
};

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

		ht_cap->mcs.rx_highest = MAX_BIT_RATE_40MHZ_MCS15;
	} else if (get_rf_type(rtlphy) == RF_1T1R) {

		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, ("1T1R\n"));

		ht_cap->mcs.rx_mask[0] = 0xFF;
		ht_cap->mcs.rx_mask[1] = 0x00;
		ht_cap->mcs.rx_mask[4] = 0x01;

		ht_cap->mcs.rx_highest = MAX_BIT_RATE_40MHZ_MCS7;
	}
}

static void _rtl_init_mac80211(struct ieee80211_hw *hw)
{
	struct rtl_mac *rtlmac = rtl_mac(rtl_priv(hw));
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	struct ieee80211_supported_band *sband;

	/* <1> use  mac->bands as mem for hw->wiphy->bands */
	sband = &(rtlmac->bands[IEEE80211_BAND_2GHZ]);

	/*
	 * <2> set hw->wiphy->bands[IEEE80211_BAND_2GHZ]
	 * to default value(1T1R)
	 */
	memcpy(&(rtlmac->bands[IEEE80211_BAND_2GHZ]), &rtl_band_2ghz,
	       sizeof(struct ieee80211_supported_band));

	/* <3> init ht cap base on ant_num */
	_rtl_init_hw_ht_capab(hw, &sband->ht_cap);

	/* <4> set mac->sband to wiphy->sband */
	hw->wiphy->bands[IEEE80211_BAND_2GHZ] = sband;

	/* <5> set hw caps */
	hw->flags = IEEE80211_HW_SIGNAL_DBM |
	    IEEE80211_HW_RX_INCLUDES_FCS |
	    IEEE80211_HW_BEACON_FILTER | IEEE80211_HW_AMPDU_AGGREGATION | /*PS*/
	    /*IEEE80211_HW_SUPPORTS_PS | */
	    /*IEEE80211_HW_PS_NULLFUNC_STACK | */
	    /*IEEE80211_HW_SUPPORTS_DYNAMIC_PS | */
	    IEEE80211_HW_REPORTS_TX_ACK_STATUS | 0;

	hw->wiphy->interface_modes =
	    BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_ADHOC);

	hw->wiphy->rts_threshold = 2347;

	hw->queues = AC_MAX;
	hw->extra_tx_headroom = RTL_TX_HEADER_SIZE;

	/* TODO: Correct this value for our hw */
	/* TODO: define these hard code value */
	hw->channel_change_time = 100;
	hw->max_listen_interval = 5;
	hw->max_rate_tries = 4;
	/* hw->max_rates = 1; */

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

}

void rtl_deinit_deferred_work(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	del_timer_sync(&rtlpriv->works.watchdog_timer);

	cancel_delayed_work(&rtlpriv->works.watchdog_wq);
	cancel_delayed_work(&rtlpriv->works.ips_nic_off_wq);
}

void rtl_init_rfkill(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	bool radio_state;
	bool blocked;
	u8 valid = 0;

	radio_state = rtlpriv->cfg->ops->radio_onoff_checking(hw, &valid);

	/*set init state to that of switch */
	rtlpriv->rfkill.rfkill_state = radio_state;
	printk(KERN_INFO "rtlwifi: wireless switch is %s\n",
	       rtlpriv->rfkill.rfkill_state ? "on" : "off");

	if (valid) {
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
	if (rtl_rate_control_register()) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("rtl: Unable to register rtl_rc,"
			  "use default RC !!\n"));
	} else {
		hw->rate_control_algorithm = "rtl_rc";
	}

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

	rtlmac->link_state = MAC80211_NOLINK;

	/* <5> init deferred work */
	_rtl_init_deferred_work(hw);

	return 0;
}

void rtl_deinit_core(struct ieee80211_hw *hw)
{
	 /*RC*/
	rtl_rate_control_unregister();
}

void rtl_init_rx_config(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_RCR, (u8 *) (&mac->rx_conf));
	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_MGT_FILTER,
				      (u8 *) (&mac->rx_mgt_filter));
	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_CTRL_FILTER,
				      (u8 *) (&mac->rx_ctrl_filter));
	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_DATA_FILTER,
				      (u8 *) (&mac->rx_data_filter));
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
			       struct rtl_tcb_desc *tcb_desc,
			       struct ieee80211_tx_info *info)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	u8 rate_flag = info->control.rates[0].flags;

	tcb_desc->use_shortgi = false;

	if (!mac->ht_enable)
		return;

	if (!mac->sgi_40 && !mac->sgi_20)
		return;

	if ((mac->bw_40 == true) && mac->sgi_40)
		tcb_desc->use_shortgi = true;
	else if ((mac->bw_40 == false) && mac->sgi_20)
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
	tcb_desc->b_rts_stbc = false;
	tcb_desc->b_cts_enable = false;
	tcb_desc->rts_sc = 0;
	tcb_desc->b_rts_bw = false;
	tcb_desc->b_rts_use_shortpreamble = false;
	tcb_desc->b_rts_use_shortgi = false;

	if (rate_flag & IEEE80211_TX_RC_USE_CTS_PROTECT) {
		/* Use CTS-to-SELF in protection mode. */
		tcb_desc->b_rts_enable = true;
		tcb_desc->b_cts_enable = true;
		tcb_desc->rts_rate = rtlpriv->cfg->maps[RTL_RC_OFDM_RATE24M];
	} else if (rate_flag & IEEE80211_TX_RC_USE_RTS_CTS) {
		/* Use RTS-CTS in protection mode. */
		tcb_desc->b_rts_enable = true;
		tcb_desc->rts_rate = rtlpriv->cfg->maps[RTL_RC_OFDM_RATE24M];
	}

}

static void _rtl_txrate_selectmode(struct ieee80211_hw *hw,
				   struct rtl_tcb_desc *tcb_desc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	if (!tcb_desc->disable_ratefallback || !tcb_desc->use_driver_rate) {
		if (mac->opmode == NL80211_IFTYPE_STATION)
			tcb_desc->ratr_index = 0;
		else if (mac->opmode == NL80211_IFTYPE_ADHOC) {
			if (tcb_desc->b_multicast || tcb_desc->b_broadcast) {
				tcb_desc->hw_rate =
				    rtlpriv->cfg->maps[RTL_RC_CCK_RATE2M];
				tcb_desc->use_driver_rate = 1;
			} else {
				/* TODO */
			}
		}
	}

	if (rtlpriv->dm.b_useramask) {
		/* TODO we will differentiate adhoc and station futrue  */
		tcb_desc->mac_id = 0;

		if ((mac->mode == WIRELESS_MODE_N_24G) ||
		    (mac->mode == WIRELESS_MODE_N_5G)) {
			tcb_desc->ratr_index = RATR_INX_WIRELESS_NGB;
		} else if (mac->mode & WIRELESS_MODE_G) {
			tcb_desc->ratr_index = RATR_INX_WIRELESS_GB;
		} else if (mac->mode & WIRELESS_MODE_B) {
			tcb_desc->ratr_index = RATR_INX_WIRELESS_B;
		}
	}

}

static void _rtl_query_bandwidth_mode(struct ieee80211_hw *hw,
				      struct rtl_tcb_desc *tcb_desc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	tcb_desc->b_packet_bw = false;

	if (!mac->bw_40 || !mac->ht_enable)
		return;

	if (tcb_desc->b_multicast || tcb_desc->b_broadcast)
		return;

	/*use legency rate, shall use 20MHz */
	if (tcb_desc->hw_rate <= rtlpriv->cfg->maps[RTL_RC_OFDM_RATE54M])
		return;

	tcb_desc->b_packet_bw = true;
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
		      struct sk_buff *skb, struct rtl_tcb_desc *tcb_desc)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *rtlmac = rtl_mac(rtl_priv(hw));
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)(skb->data);
	struct ieee80211_rate *txrate;
	u16 fc = le16_to_cpu(hdr->frame_control);

	memset(tcb_desc, 0, sizeof(struct rtl_tcb_desc));

	if (ieee80211_is_data(fc)) {
		txrate = ieee80211_get_tx_rate(hw, info);
		tcb_desc->hw_rate = txrate->hw_value;

		/*
		 *we set data rate RTL_RC_CCK_RATE1M
		 *in rtl_rc.c   if skb is special data or
		 *mgt which need low data rate.
		 */

		/*
		 *So tcb_desc->hw_rate is just used for
		 *special data and mgt frames
		 */
		if (tcb_desc->hw_rate < rtlpriv->cfg->maps[RTL_RC_CCK_RATE11M]) {
			tcb_desc->use_driver_rate = true;
			tcb_desc->ratr_index = 7;

			tcb_desc->hw_rate =
			    rtlpriv->cfg->maps[RTL_RC_CCK_RATE1M];
			tcb_desc->disable_ratefallback = 1;
		} else {
			/*
			 *because hw will nerver use hw_rate
			 *when tcb_desc->use_driver_rate = false
			 *so we never set highest N rate here,
			 *and N rate will all be controled by FW
			 *when tcb_desc->use_driver_rate = false
			 */
			if (rtlmac->ht_enable) {
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
			tcb_desc->b_multicast = 1;
		else if (is_broadcast_ether_addr(ieee80211_get_DA(hdr)))
			tcb_desc->b_broadcast = 1;

		_rtl_txrate_selectmode(hw, tcb_desc);
		_rtl_query_bandwidth_mode(hw, tcb_desc);
		_rtl_qurey_shortpreamble_mode(hw, tcb_desc, info);
		_rtl_query_shortgi(hw, tcb_desc, info);
		_rtl_query_protection_mode(hw, tcb_desc, info);
	} else {
		tcb_desc->use_driver_rate = true;
		tcb_desc->ratr_index = 7;
		tcb_desc->disable_ratefallback = 1;
		tcb_desc->mac_id = 0;

		tcb_desc->hw_rate = rtlpriv->cfg->maps[RTL_RC_CCK_RATE1M];
	}
}
EXPORT_SYMBOL(rtl_get_tcb_desc);

bool rtl_tx_mgmt_proc(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)(skb->data);
	u16 fc = le16_to_cpu(hdr->frame_control);

	if (ieee80211_is_auth(fc)) {
		RT_TRACE(rtlpriv, COMP_SEND, DBG_DMESG, ("MAC80211_LINKING\n"));
		rtl_ips_nic_on(hw);

		mac->link_state = MAC80211_LINKING;
	}

	return true;
}

bool rtl_action_proc(struct ieee80211_hw *hw, struct sk_buff *skb, u8 is_tx)
{
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)(skb->data);
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	u16 fc = le16_to_cpu(hdr->frame_control);
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
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)(skb->data);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	u16 fc = le16_to_cpu(hdr->frame_control);
	u16 ether_type;
	u8 mac_hdr_len = ieee80211_get_hdrlen_from_skb(skb);
	const struct iphdr *ip;

	if (!ieee80211_is_data(fc))
		goto end;

	if (ieee80211_is_nullfunc(fc))
		return true;

	ip = (struct iphdr *)((u8 *) skb->data + mac_hdr_len +
			      SNAP_SIZE + PROTOC_TYPE_SIZE);
	ether_type = *(u16 *) ((u8 *) skb->data + mac_hdr_len + SNAP_SIZE);
	ether_type = ntohs(ether_type);

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
	} else if (0x86DD == ether_type) {
		return true;
	}

end:
	return false;
}

/*********************************************************
 *
 * functions called by core.c
 *
 *********************************************************/
int rtl_tx_agg_start(struct ieee80211_hw *hw, const u8 *ra, u16 tid, u16 *ssn)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_tid_data *tid_data;
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	RT_TRACE(rtlpriv, COMP_SEND, DBG_DMESG,
		 ("on ra = %pM tid = %d\n", ra, tid));

	if (unlikely(tid >= MAX_TID_COUNT))
		return -EINVAL;

	if (mac->tids[tid].agg.agg_state != RTL_AGG_OFF) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 ("Start AGG when state is not RTL_AGG_OFF !\n"));
		return -ENXIO;
	}

	tid_data = &mac->tids[tid];
	*ssn = SEQ_TO_SN(tid_data->seq_number);

	RT_TRACE(rtlpriv, COMP_SEND, DBG_DMESG,
		 ("HW queue is empty tid:%d\n", tid));
	tid_data->agg.agg_state = RTL_AGG_ON;

	ieee80211_start_tx_ba_cb_irqsafe(mac->vif, ra, tid);

	return 0;
}

int rtl_tx_agg_stop(struct ieee80211_hw *hw, const u8 * ra, u16 tid)
{
	int ssn = -1;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_tid_data *tid_data;

	if (!ra) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG, ("ra = NULL\n"));
		return -EINVAL;
	}

	if (unlikely(tid >= MAX_TID_COUNT))
		return -EINVAL;

	if (mac->tids[tid].agg.agg_state != RTL_AGG_ON)
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 ("Stopping AGG while state not ON or starting\n"));

	tid_data = &mac->tids[tid];
	ssn = (tid_data->seq_number & IEEE80211_SCTL_SEQ) >> 4;

	mac->tids[tid].agg.agg_state = RTL_AGG_OFF;

	ieee80211_stop_tx_ba_cb_irqsafe(mac->vif, ra, tid);

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

	bool b_busytraffic = false;
	bool b_higher_busytraffic = false;
	bool b_higher_busyrxtraffic = false;
	bool b_higher_busytxtraffic = false;

	u8 idx = 0;
	u32 rx_cnt_inp4eriod = 0;
	u32 tx_cnt_inp4eriod = 0;
	u32 aver_rx_cnt_inperiod = 0;
	u32 aver_tx_cnt_inperiod = 0;

	bool benter_ps = false;

	if (is_hal_stop(rtlhal))
		return;

	/* <1> Determine if action frame is allowed */
	if (mac->link_state > MAC80211_NOLINK) {
		if (mac->cnt_after_linked < 20)
			mac->cnt_after_linked++;
	} else {
		mac->cnt_after_linked = 0;
	}

	/* <2> DM */
	rtlpriv->cfg->ops->dm_watchdog(hw);

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
			b_busytraffic = true;

		/* Higher Tx/Rx data. */
		if (aver_rx_cnt_inperiod > 4000 ||
		    aver_tx_cnt_inperiod > 4000) {
			b_higher_busytraffic = true;

			/* Extremely high Rx data. */
			if (aver_rx_cnt_inperiod > 5000)
				b_higher_busyrxtraffic = true;
			else
				b_higher_busytxtraffic = false;
		}

		if (((rtlpriv->link_info.num_rx_inperiod +
		      rtlpriv->link_info.num_tx_inperiod) > 8) ||
		    (rtlpriv->link_info.num_rx_inperiod > 2))
			benter_ps = false;
		else
			benter_ps = true;

		/* LeisurePS only work in infra mode. */
		if (benter_ps)
			rtl_lps_enter(hw);
		else
			rtl_lps_leave(hw);
	}

	rtlpriv->link_info.num_rx_inperiod = 0;
	rtlpriv->link_info.num_tx_inperiod = 0;

	rtlpriv->link_info.b_busytraffic = b_busytraffic;
	rtlpriv->link_info.b_higher_busytraffic = b_higher_busytraffic;
	rtlpriv->link_info.b_higher_busyrxtraffic = b_higher_busyrxtraffic;

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
	return 0;
}

static void __exit rtl_core_module_exit(void)
{
}

module_init(rtl_core_module_init);
module_exit(rtl_core_module_exit);
