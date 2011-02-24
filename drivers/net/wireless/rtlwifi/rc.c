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

#include "wifi.h"
#include "base.h"
#include "rc.h"

/*
 *Finds the highest rate index we can use
 *if skb is special data like DHCP/EAPOL, we set should
 *it to lowest rate CCK_1M, otherwise we set rate to
 *CCK11M or OFDM_54M based on wireless mode.
 */
static u8 _rtl_rc_get_highest_rix(struct rtl_priv *rtlpriv,
				  struct sk_buff *skb, bool not_data)
{
	struct rtl_mac *rtlmac = rtl_mac(rtlpriv);

	/*
	 *mgt use 1M, although we have check it
	 *before this function use rate_control_send_low,
	 *we still check it here
	 */
	if (not_data)
		return rtlpriv->cfg->maps[RTL_RC_CCK_RATE1M];

	/*
	 *this rate is no use for true rate, firmware
	 *will control rate at all it just used for
	 *1.show in iwconfig in B/G mode
	 *2.in rtl_get_tcb_desc when we check rate is
	 *      1M we will not use FW rate but user rate.
	 */
	if (rtl_is_special_data(rtlpriv->mac80211.hw, skb, true)) {
		return rtlpriv->cfg->maps[RTL_RC_CCK_RATE1M];
	} else {
		if (rtlmac->mode == WIRELESS_MODE_B)
			return rtlpriv->cfg->maps[RTL_RC_CCK_RATE11M];
		else
			return rtlpriv->cfg->maps[RTL_RC_OFDM_RATE54M];
	}
}

static void _rtl_rc_rate_set_series(struct rtl_priv *rtlpriv,
				    struct ieee80211_tx_rate *rate,
				    struct ieee80211_tx_rate_control *txrc,
				    u8 tries, u8 rix, int rtsctsenable,
				    bool not_data)
{
	struct rtl_mac *mac = rtl_mac(rtlpriv);

	rate->count = tries;
	rate->idx = (rix > 0x2) ? rix : 0x2;

	if (!not_data) {
		if (txrc->short_preamble)
			rate->flags |= IEEE80211_TX_RC_USE_SHORT_PREAMBLE;
		if (mac->bw_40)
			rate->flags |= IEEE80211_TX_RC_40_MHZ_WIDTH;
		if (mac->sgi_20 || mac->sgi_40)
			rate->flags |= IEEE80211_TX_RC_SHORT_GI;
		if (mac->ht_enable)
			rate->flags |= IEEE80211_TX_RC_MCS;
	}
}

static void rtl_get_rate(void *ppriv, struct ieee80211_sta *sta,
			 void *priv_sta, struct ieee80211_tx_rate_control *txrc)
{
	struct rtl_priv *rtlpriv = ppriv;
	struct sk_buff *skb = txrc->skb;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *rates = tx_info->control.rates;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	__le16 fc = hdr->frame_control;
	u8 try_per_rate, i, rix;
	bool not_data = !ieee80211_is_data(fc);

	if (rate_control_send_low(sta, priv_sta, txrc))
		return;

	rix = _rtl_rc_get_highest_rix(rtlpriv, skb, not_data);

	try_per_rate = 1;
	_rtl_rc_rate_set_series(rtlpriv, &rates[0], txrc,
				try_per_rate, rix, 1, not_data);

	if (!not_data) {
		for (i = 1; i < 4; i++)
			_rtl_rc_rate_set_series(rtlpriv, &rates[i],
						txrc, i, (rix - i), 1,
						not_data);
	}
}

static bool _rtl_tx_aggr_check(struct rtl_priv *rtlpriv, u16 tid)
{
	struct rtl_mac *mac = rtl_mac(rtlpriv);

	if (mac->act_scanning)
		return false;

	if (mac->cnt_after_linked < 3)
		return false;

	if (mac->tids[tid].agg.agg_state == RTL_AGG_OFF)
		return true;

	return false;
}

/*mac80211 Rate Control callbacks*/
static void rtl_tx_status(void *ppriv,
			  struct ieee80211_supported_band *sband,
			  struct ieee80211_sta *sta, void *priv_sta,
			  struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = ppriv;
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	struct ieee80211_hdr *hdr;
	__le16 fc;

	hdr = (struct ieee80211_hdr *)skb->data;
	fc = hdr->frame_control;

	if (!priv_sta || !ieee80211_is_data(fc))
		return;

	if (rtl_is_special_data(mac->hw, skb, true))
		return;

	if (is_multicast_ether_addr(ieee80211_get_DA(hdr))
	    || is_broadcast_ether_addr(ieee80211_get_DA(hdr)))
		return;

	/* Check if aggregation has to be enabled for this tid */
	if (conf_is_ht(&mac->hw->conf) &&
	    !(skb->protocol == cpu_to_be16(ETH_P_PAE))) {
		if (ieee80211_is_data_qos(fc)) {
			u8 *qc, tid;

			qc = ieee80211_get_qos_ctl(hdr);
			tid = qc[0] & 0xf;

			if (_rtl_tx_aggr_check(rtlpriv, tid))
				ieee80211_start_tx_ba_session(sta, tid, 5000);
		}
	}
}

static void rtl_rate_init(void *ppriv,
			  struct ieee80211_supported_band *sband,
			  struct ieee80211_sta *sta, void *priv_sta)
{
	struct rtl_priv *rtlpriv = ppriv;
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	u8 is_ht = conf_is_ht(&mac->hw->conf);

	if ((mac->opmode == NL80211_IFTYPE_STATION) ||
	    (mac->opmode == NL80211_IFTYPE_MESH_POINT) ||
	    (mac->opmode == NL80211_IFTYPE_ADHOC)) {

		switch (sband->band) {
		case IEEE80211_BAND_2GHZ:
			rtlpriv->rate_priv->cur_ratetab_idx =
			    RATR_INX_WIRELESS_G;
			if (is_ht)
				rtlpriv->rate_priv->cur_ratetab_idx =
				    RATR_INX_WIRELESS_NGB;
			break;
		case IEEE80211_BAND_5GHZ:
			rtlpriv->rate_priv->cur_ratetab_idx =
			    RATR_INX_WIRELESS_A;
			if (is_ht)
				rtlpriv->rate_priv->cur_ratetab_idx =
				    RATR_INX_WIRELESS_NGB;
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
				 ("Invalid band\n"));
			rtlpriv->rate_priv->cur_ratetab_idx =
			    RATR_INX_WIRELESS_NGB;
			break;
		}

		RT_TRACE(rtlpriv, COMP_RATE, DBG_DMESG,
			 ("Choosing rate table index: %d\n",
			  rtlpriv->rate_priv->cur_ratetab_idx));

	}

}

static void rtl_rate_update(void *ppriv,
			    struct ieee80211_supported_band *sband,
			    struct ieee80211_sta *sta, void *priv_sta,
			    u32 changed,
			    enum nl80211_channel_type oper_chan_type)
{
	struct rtl_priv *rtlpriv = ppriv;
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	bool oper_cw40 = false, oper_sgi40;
	bool local_cw40 = mac->bw_40;
	bool local_sgi40 = mac->sgi_40;
	u8 is_ht = conf_is_ht(&mac->hw->conf);

	if (changed & IEEE80211_RC_HT_CHANGED) {
		if (mac->opmode != NL80211_IFTYPE_STATION)
			return;

		if (rtlhal->hw->conf.channel_type == NL80211_CHAN_HT40MINUS ||
		    rtlhal->hw->conf.channel_type == NL80211_CHAN_HT40PLUS)
			oper_cw40 = true;

		oper_sgi40 = mac->sgi_40;

		if ((local_cw40 != oper_cw40) || (local_sgi40 != oper_sgi40)) {
			switch (sband->band) {
			case IEEE80211_BAND_2GHZ:
				rtlpriv->rate_priv->cur_ratetab_idx =
				    RATR_INX_WIRELESS_G;
				if (is_ht)
					rtlpriv->rate_priv->cur_ratetab_idx =
					    RATR_INX_WIRELESS_NGB;
				break;
			case IEEE80211_BAND_5GHZ:
				rtlpriv->rate_priv->cur_ratetab_idx =
				    RATR_INX_WIRELESS_A;
				if (is_ht)
					rtlpriv->rate_priv->cur_ratetab_idx =
					    RATR_INX_WIRELESS_NGB;
				break;
			default:
				RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
					 ("Invalid band\n"));
				rtlpriv->rate_priv->cur_ratetab_idx =
				    RATR_INX_WIRELESS_NGB;
				break;
			}
		}
	}
}

static void *rtl_rate_alloc(struct ieee80211_hw *hw,
		struct dentry *debugfsdir)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	return rtlpriv;
}

static void rtl_rate_free(void *rtlpriv)
{
	return;
}

static void *rtl_rate_alloc_sta(void *ppriv,
				struct ieee80211_sta *sta, gfp_t gfp)
{
	struct rtl_priv *rtlpriv = ppriv;
	struct rtl_rate_priv *rate_priv;

	rate_priv = kzalloc(sizeof(struct rtl_rate_priv), gfp);
	if (!rate_priv) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("Unable to allocate private rc structure\n"));
		return NULL;
	}

	rtlpriv->rate_priv = rate_priv;

	return rate_priv;
}

static void rtl_rate_free_sta(void *rtlpriv,
			      struct ieee80211_sta *sta, void *priv_sta)
{
	struct rtl_rate_priv *rate_priv = priv_sta;
	kfree(rate_priv);
}

static struct rate_control_ops rtl_rate_ops = {
	.module = NULL,
	.name = "rtl_rc",
	.alloc = rtl_rate_alloc,
	.free = rtl_rate_free,
	.alloc_sta = rtl_rate_alloc_sta,
	.free_sta = rtl_rate_free_sta,
	.rate_init = rtl_rate_init,
	.rate_update = rtl_rate_update,
	.tx_status = rtl_tx_status,
	.get_rate = rtl_get_rate,
};

int rtl_rate_control_register(void)
{
	return ieee80211_rate_control_register(&rtl_rate_ops);
}

void rtl_rate_control_unregister(void)
{
	ieee80211_rate_control_unregister(&rtl_rate_ops);
}
