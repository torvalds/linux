/*
 * Copyright (c) 2013 Qualcomm Atheros, Inc.
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

/*************/
/* node_aggr */
/*************/

static ssize_t read_file_node_aggr(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct ath_node *an = file->private_data;
	struct ath_softc *sc = an->sc;
	struct ath_atx_tid *tid;
	struct ath_txq *txq;
	u32 len = 0, size = 4096;
	char *buf;
	size_t retval;
	int tidno;

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	if (!an->sta->deflink.ht_cap.ht_supported) {
		len = scnprintf(buf, size, "%s\n",
				"HT not supported");
		goto exit;
	}

	len = scnprintf(buf, size, "Max-AMPDU: %d\n",
			an->maxampdu);
	len += scnprintf(buf + len, size - len, "MPDU Density: %d\n\n",
			 an->mpdudensity);

	len += scnprintf(buf + len, size - len,
			 "\n%3s%11s%10s%10s%10s%10s%9s%6s%8s\n",
			 "TID", "SEQ_START", "SEQ_NEXT", "BAW_SIZE",
			 "BAW_HEAD", "BAW_TAIL", "BAR_IDX", "SCHED", "PAUSED");

	for (tidno = 0; tidno < IEEE80211_NUM_TIDS; tidno++) {
		tid = ath_node_to_tid(an, tidno);
		txq = tid->txq;
		ath_txq_lock(sc, txq);
		if (tid->active) {
			len += scnprintf(buf + len, size - len,
					 "%3d%11d%10d%10d%10d%10d%9d%6d\n",
					 tid->tidno,
					 tid->seq_start,
					 tid->seq_next,
					 tid->baw_size,
					 tid->baw_head,
					 tid->baw_tail,
					 tid->bar_index,
					 !list_empty(&tid->list));
		}
		ath_txq_unlock(sc, txq);
	}
exit:
	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

static const struct file_operations fops_node_aggr = {
	.read = read_file_node_aggr,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

/*************/
/* node_recv */
/*************/

void ath_debug_rate_stats(struct ath_softc *sc,
			  struct ath_rx_status *rs,
			  struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ath_hw *ah = sc->sc_ah;
	struct ieee80211_rx_status *rxs;
	struct ath_rx_rate_stats *rstats;
	struct ieee80211_sta *sta;
	struct ath_node *an;

	if (!ieee80211_is_data(hdr->frame_control))
		return;

	rcu_read_lock();

	sta = ieee80211_find_sta_by_ifaddr(sc->hw, hdr->addr2, NULL);
	if (!sta)
		goto exit;

	an = (struct ath_node *) sta->drv_priv;
	rstats = &an->rx_rate_stats;
	rxs = IEEE80211_SKB_RXCB(skb);

	if (IS_HT_RATE(rs->rs_rate)) {
		if (rxs->rate_idx >= ARRAY_SIZE(rstats->ht_stats))
			goto exit;

		if (rxs->bw == RATE_INFO_BW_40)
			rstats->ht_stats[rxs->rate_idx].ht40_cnt++;
		else
			rstats->ht_stats[rxs->rate_idx].ht20_cnt++;

		if (rxs->enc_flags & RX_ENC_FLAG_SHORT_GI)
			rstats->ht_stats[rxs->rate_idx].sgi_cnt++;
		else
			rstats->ht_stats[rxs->rate_idx].lgi_cnt++;

		goto exit;
	}

	if (IS_CCK_RATE(rs->rs_rate)) {
		if (rxs->enc_flags & RX_ENC_FLAG_SHORTPRE)
			rstats->cck_stats[rxs->rate_idx].cck_sp_cnt++;
		else
			rstats->cck_stats[rxs->rate_idx].cck_lp_cnt++;

		goto exit;
	}

	if (IS_OFDM_RATE(rs->rs_rate)) {
		if (ah->curchan->chan->band == NL80211_BAND_2GHZ)
			rstats->ofdm_stats[rxs->rate_idx - 4].ofdm_cnt++;
		else
			rstats->ofdm_stats[rxs->rate_idx].ofdm_cnt++;
	}
exit:
	rcu_read_unlock();
}

#define PRINT_CCK_RATE(str, i, sp)					\
	do {								\
		len += scnprintf(buf + len, size - len,			\
			 "%11s : %10u\n",				\
			 str,						\
			 (sp) ? rstats->cck_stats[i].cck_sp_cnt :	\
			 rstats->cck_stats[i].cck_lp_cnt);		\
	} while (0)

#define PRINT_OFDM_RATE(str, i)					\
	do {							\
		len += scnprintf(buf + len, size - len,		\
			 "%11s : %10u\n",			\
			 str,					\
			 rstats->ofdm_stats[i].ofdm_cnt);	\
	} while (0)

static ssize_t read_file_node_recv(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct ath_node *an = file->private_data;
	struct ath_softc *sc = an->sc;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_rx_rate_stats *rstats;
	struct ieee80211_sta *sta = an->sta;
	enum nl80211_band band;
	u32 len = 0, size = 4096;
	char *buf;
	size_t retval;
	int i;

	buf = kzalloc(size, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	band = ah->curchan->chan->band;
	rstats = &an->rx_rate_stats;

	if (!sta->deflink.ht_cap.ht_supported)
		goto legacy;

	len += scnprintf(buf + len, size - len,
			 "%24s%10s%10s%10s\n",
			 "HT20", "HT40", "SGI", "LGI");

	for (i = 0; i < 24; i++) {
		len += scnprintf(buf + len, size - len,
				 "%8s%3u : %10u%10u%10u%10u\n",
				 "MCS", i,
				 rstats->ht_stats[i].ht20_cnt,
				 rstats->ht_stats[i].ht40_cnt,
				 rstats->ht_stats[i].sgi_cnt,
				 rstats->ht_stats[i].lgi_cnt);
	}

	len += scnprintf(buf + len, size - len, "\n");

legacy:
	if (band == NL80211_BAND_2GHZ) {
		PRINT_CCK_RATE("CCK-1M/LP", 0, false);
		PRINT_CCK_RATE("CCK-2M/LP", 1, false);
		PRINT_CCK_RATE("CCK-5.5M/LP", 2, false);
		PRINT_CCK_RATE("CCK-11M/LP", 3, false);

		PRINT_CCK_RATE("CCK-2M/SP", 1, true);
		PRINT_CCK_RATE("CCK-5.5M/SP", 2, true);
		PRINT_CCK_RATE("CCK-11M/SP", 3, true);
	}

	PRINT_OFDM_RATE("OFDM-6M", 0);
	PRINT_OFDM_RATE("OFDM-9M", 1);
	PRINT_OFDM_RATE("OFDM-12M", 2);
	PRINT_OFDM_RATE("OFDM-18M", 3);
	PRINT_OFDM_RATE("OFDM-24M", 4);
	PRINT_OFDM_RATE("OFDM-36M", 5);
	PRINT_OFDM_RATE("OFDM-48M", 6);
	PRINT_OFDM_RATE("OFDM-54M", 7);

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);

	return retval;
}

#undef PRINT_OFDM_RATE
#undef PRINT_CCK_RATE

static const struct file_operations fops_node_recv = {
	.read = read_file_node_recv,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

void ath9k_sta_add_debugfs(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta,
			   struct dentry *dir)
{
	struct ath_node *an = (struct ath_node *)sta->drv_priv;

	debugfs_create_file("node_aggr", 0444, dir, an, &fops_node_aggr);
	debugfs_create_file("node_recv", 0444, dir, an, &fops_node_recv);
}
