/*
 * Copyright (c) 2014, Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "ath9k.h"
#include "hw.h"
#include "dynack.h"

#define COMPUTE_TO		(5 * HZ)
#define LATEACK_DELAY		(10 * HZ)
#define LATEACK_TO		256
#define MAX_DELAY		300
#define EWMA_LEVEL		96
#define EWMA_DIV		128

/**
 * ath_dynack_ewma - EWMA (Exponentially Weighted Moving Average) calculation
 *
 */
static inline int ath_dynack_ewma(int old, int new)
{
	if (old > 0)
		return (new * (EWMA_DIV - EWMA_LEVEL) +
			old * EWMA_LEVEL) / EWMA_DIV;
	else
		return new;
}

/**
 * ath_dynack_get_sifs - get sifs time based on phy used
 * @ah: ath hw
 * @phy: phy used
 *
 */
static inline u32 ath_dynack_get_sifs(struct ath_hw *ah, int phy)
{
	u32 sifs = CCK_SIFS_TIME;

	if (phy == WLAN_RC_PHY_OFDM) {
		if (IS_CHAN_QUARTER_RATE(ah->curchan))
			sifs = OFDM_SIFS_TIME_QUARTER;
		else if (IS_CHAN_HALF_RATE(ah->curchan))
			sifs = OFDM_SIFS_TIME_HALF;
		else
			sifs = OFDM_SIFS_TIME;
	}
	return sifs;
}

/**
 * ath_dynack_bssidmask - filter out ACK frames based on BSSID mask
 * @ah: ath hw
 * @mac: receiver address
 */
static inline bool ath_dynack_bssidmask(struct ath_hw *ah, const u8 *mac)
{
	int i;
	struct ath_common *common = ath9k_hw_common(ah);

	for (i = 0; i < ETH_ALEN; i++) {
		if ((common->macaddr[i] & common->bssidmask[i]) !=
		    (mac[i] & common->bssidmask[i]))
			return false;
	}

	return true;
}

/**
 * ath_dynack_compute_ackto - compute ACK timeout as the maximum STA timeout
 * @ah: ath hw
 *
 * should be called while holding qlock
 */
static void ath_dynack_compute_ackto(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_dynack *da = &ah->dynack;
	struct ath_node *an;
	int to = 0;

	list_for_each_entry(an, &da->nodes, list)
		if (an->ackto > to)
			to = an->ackto;

	if (to && da->ackto != to) {
		u32 slottime;

		slottime = (to - 3) / 2;
		da->ackto = to;
		ath_dbg(common, DYNACK, "ACK timeout %u slottime %u\n",
			da->ackto, slottime);
		ath9k_hw_setslottime(ah, slottime);
		ath9k_hw_set_ack_timeout(ah, da->ackto);
		ath9k_hw_set_cts_timeout(ah, da->ackto);
	}
}

/**
 * ath_dynack_compute_to - compute STA ACK timeout
 * @ah: ath hw
 *
 * should be called while holding qlock
 */
static void ath_dynack_compute_to(struct ath_hw *ah)
{
	u32 ackto, ack_ts;
	u8 *dst, *src;
	struct ieee80211_sta *sta;
	struct ath_node *an;
	struct ts_info *st_ts;
	struct ath_dynack *da = &ah->dynack;

	rcu_read_lock();

	while (da->st_rbf.h_rb != da->st_rbf.t_rb &&
	       da->ack_rbf.h_rb != da->ack_rbf.t_rb) {
		ack_ts = da->ack_rbf.tstamp[da->ack_rbf.h_rb];
		st_ts = &da->st_rbf.ts[da->st_rbf.h_rb];
		dst = da->st_rbf.addr[da->st_rbf.h_rb].h_dest;
		src = da->st_rbf.addr[da->st_rbf.h_rb].h_src;

		ath_dbg(ath9k_hw_common(ah), DYNACK,
			"ack_ts %u st_ts %u st_dur %u [%u-%u]\n",
			ack_ts, st_ts->tstamp, st_ts->dur,
			da->ack_rbf.h_rb, da->st_rbf.h_rb);

		if (ack_ts > st_ts->tstamp + st_ts->dur) {
			ackto = ack_ts - st_ts->tstamp - st_ts->dur;

			if (ackto < MAX_DELAY) {
				sta = ieee80211_find_sta_by_ifaddr(ah->hw, dst,
								   src);
				if (sta) {
					an = (struct ath_node *)sta->drv_priv;
					an->ackto = ath_dynack_ewma(an->ackto,
								    ackto);
					ath_dbg(ath9k_hw_common(ah), DYNACK,
						"%pM to %d [%u]\n", dst,
						an->ackto, ackto);
					if (time_is_before_jiffies(da->lto)) {
						ath_dynack_compute_ackto(ah);
						da->lto = jiffies + COMPUTE_TO;
					}
				}
				INCR(da->ack_rbf.h_rb, ATH_DYN_BUF);
			}
			INCR(da->st_rbf.h_rb, ATH_DYN_BUF);
		} else {
			INCR(da->ack_rbf.h_rb, ATH_DYN_BUF);
		}
	}

	rcu_read_unlock();
}

/**
 * ath_dynack_sample_tx_ts - status timestamp sampling method
 * @ah: ath hw
 * @skb: socket buffer
 * @ts: tx status info
 * @sta: station pointer
 *
 */
void ath_dynack_sample_tx_ts(struct ath_hw *ah, struct sk_buff *skb,
			     struct ath_tx_status *ts,
			     struct ieee80211_sta *sta)
{
	u8 ridx;
	struct ieee80211_hdr *hdr;
	struct ath_dynack *da = &ah->dynack;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	if ((info->flags & IEEE80211_TX_CTL_NO_ACK) || !da->enabled)
		return;

	spin_lock_bh(&da->qlock);

	hdr = (struct ieee80211_hdr *)skb->data;

	/* late ACK */
	if (ts->ts_status & ATH9K_TXERR_XRETRY) {
		if (ieee80211_is_assoc_req(hdr->frame_control) ||
		    ieee80211_is_assoc_resp(hdr->frame_control) ||
		    ieee80211_is_auth(hdr->frame_control)) {
			ath_dbg(common, DYNACK, "late ack\n");

			ath9k_hw_setslottime(ah, (LATEACK_TO - 3) / 2);
			ath9k_hw_set_ack_timeout(ah, LATEACK_TO);
			ath9k_hw_set_cts_timeout(ah, LATEACK_TO);
			if (sta) {
				struct ath_node *an;

				an = (struct ath_node *)sta->drv_priv;
				an->ackto = -1;
			}
			da->lto = jiffies + LATEACK_DELAY;
		}

		spin_unlock_bh(&da->qlock);
		return;
	}

	ridx = ts->ts_rateindex;

	da->st_rbf.ts[da->st_rbf.t_rb].tstamp = ts->ts_tstamp;
	da->st_rbf.ts[da->st_rbf.t_rb].dur = ts->duration;
	ether_addr_copy(da->st_rbf.addr[da->st_rbf.t_rb].h_dest, hdr->addr1);
	ether_addr_copy(da->st_rbf.addr[da->st_rbf.t_rb].h_src, hdr->addr2);

	if (!(info->status.rates[ridx].flags & IEEE80211_TX_RC_MCS)) {
		u32 phy, sifs;
		const struct ieee80211_rate *rate;
		struct ieee80211_tx_rate *rates = info->status.rates;

		rate = &common->sbands[info->band].bitrates[rates[ridx].idx];
		if (info->band == NL80211_BAND_2GHZ &&
		    !(rate->flags & IEEE80211_RATE_ERP_G))
			phy = WLAN_RC_PHY_CCK;
		else
			phy = WLAN_RC_PHY_OFDM;

		sifs = ath_dynack_get_sifs(ah, phy);
		da->st_rbf.ts[da->st_rbf.t_rb].dur -= sifs;
	}

	ath_dbg(common, DYNACK, "{%pM} tx sample %u [dur %u][h %u-t %u]\n",
		hdr->addr1, da->st_rbf.ts[da->st_rbf.t_rb].tstamp,
		da->st_rbf.ts[da->st_rbf.t_rb].dur, da->st_rbf.h_rb,
		(da->st_rbf.t_rb + 1) % ATH_DYN_BUF);

	INCR(da->st_rbf.t_rb, ATH_DYN_BUF);
	if (da->st_rbf.t_rb == da->st_rbf.h_rb)
		INCR(da->st_rbf.h_rb, ATH_DYN_BUF);

	ath_dynack_compute_to(ah);

	spin_unlock_bh(&da->qlock);
}
EXPORT_SYMBOL(ath_dynack_sample_tx_ts);

/**
 * ath_dynack_sample_ack_ts - ACK timestamp sampling method
 * @ah: ath hw
 * @skb: socket buffer
 * @ts: rx timestamp
 *
 */
void ath_dynack_sample_ack_ts(struct ath_hw *ah, struct sk_buff *skb,
			      u32 ts)
{
	struct ath_dynack *da = &ah->dynack;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	if (!ath_dynack_bssidmask(ah, hdr->addr1) || !da->enabled)
		return;

	spin_lock_bh(&da->qlock);
	da->ack_rbf.tstamp[da->ack_rbf.t_rb] = ts;

	ath_dbg(common, DYNACK, "rx sample %u [h %u-t %u]\n",
		da->ack_rbf.tstamp[da->ack_rbf.t_rb],
		da->ack_rbf.h_rb, (da->ack_rbf.t_rb + 1) % ATH_DYN_BUF);

	INCR(da->ack_rbf.t_rb, ATH_DYN_BUF);
	if (da->ack_rbf.t_rb == da->ack_rbf.h_rb)
		INCR(da->ack_rbf.h_rb, ATH_DYN_BUF);

	ath_dynack_compute_to(ah);

	spin_unlock_bh(&da->qlock);
}
EXPORT_SYMBOL(ath_dynack_sample_ack_ts);

/**
 * ath_dynack_node_init - init ath_node related info
 * @ah: ath hw
 * @an: ath node
 *
 */
void ath_dynack_node_init(struct ath_hw *ah, struct ath_node *an)
{
	/* ackto = slottime + sifs + air delay */
	u32 ackto = 9 + 16 + 64;
	struct ath_dynack *da = &ah->dynack;

	an->ackto = ackto;

	spin_lock(&da->qlock);
	list_add_tail(&an->list, &da->nodes);
	spin_unlock(&da->qlock);
}
EXPORT_SYMBOL(ath_dynack_node_init);

/**
 * ath_dynack_node_deinit - deinit ath_node related info
 * @ah: ath hw
 * @an: ath node
 *
 */
void ath_dynack_node_deinit(struct ath_hw *ah, struct ath_node *an)
{
	struct ath_dynack *da = &ah->dynack;

	spin_lock(&da->qlock);
	list_del(&an->list);
	spin_unlock(&da->qlock);
}
EXPORT_SYMBOL(ath_dynack_node_deinit);

/**
 * ath_dynack_reset - reset dynack processing
 * @ah: ath hw
 *
 */
void ath_dynack_reset(struct ath_hw *ah)
{
	/* ackto = slottime + sifs + air delay */
	u32 ackto = 9 + 16 + 64;
	struct ath_dynack *da = &ah->dynack;

	da->lto = jiffies;
	da->ackto = ackto;

	da->st_rbf.t_rb = 0;
	da->st_rbf.h_rb = 0;
	da->ack_rbf.t_rb = 0;
	da->ack_rbf.h_rb = 0;

	/* init acktimeout */
	ath9k_hw_setslottime(ah, (ackto - 3) / 2);
	ath9k_hw_set_ack_timeout(ah, ackto);
	ath9k_hw_set_cts_timeout(ah, ackto);
}
EXPORT_SYMBOL(ath_dynack_reset);

/**
 * ath_dynack_init - init dynack data structure
 * @ah: ath hw
 *
 */
void ath_dynack_init(struct ath_hw *ah)
{
	struct ath_dynack *da = &ah->dynack;

	memset(da, 0, sizeof(struct ath_dynack));

	spin_lock_init(&da->qlock);
	INIT_LIST_HEAD(&da->nodes);

	ah->hw->wiphy->features |= NL80211_FEATURE_ACKTO_ESTIMATION;
}
