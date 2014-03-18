/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
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

#include "core.h"
#include "txrx.h"
#include "htt.h"
#include "mac.h"
#include "debug.h"

static void ath10k_report_offchan_tx(struct ath10k *ar, struct sk_buff *skb)
{
	if (!ATH10K_SKB_CB(skb)->htt.is_offchan)
		return;

	/* If the original wait_for_completion() timed out before
	 * {data,mgmt}_tx_completed() was called then we could complete
	 * offchan_tx_completed for a different skb. Prevent this by using
	 * offchan_tx_skb. */
	spin_lock_bh(&ar->data_lock);
	if (ar->offchan_tx_skb != skb) {
		ath10k_warn("completed old offchannel frame\n");
		goto out;
	}

	complete(&ar->offchan_tx_completed);
	ar->offchan_tx_skb = NULL; /* just for sanity */

	ath10k_dbg(ATH10K_DBG_HTT, "completed offchannel skb %p\n", skb);
out:
	spin_unlock_bh(&ar->data_lock);
}

void ath10k_txrx_tx_unref(struct ath10k_htt *htt,
			  const struct htt_tx_done *tx_done)
{
	struct device *dev = htt->ar->dev;
	struct ieee80211_tx_info *info;
	struct ath10k_skb_cb *skb_cb;
	struct sk_buff *msdu;
	int ret;

	ath10k_dbg(ATH10K_DBG_HTT, "htt tx completion msdu_id %u discard %d no_ack %d\n",
		   tx_done->msdu_id, !!tx_done->discard, !!tx_done->no_ack);

	if (tx_done->msdu_id >= htt->max_num_pending_tx) {
		ath10k_warn("warning: msdu_id %d too big, ignoring\n",
			    tx_done->msdu_id);
		return;
	}

	msdu = htt->pending_tx[tx_done->msdu_id];
	skb_cb = ATH10K_SKB_CB(msdu);

	ret = ath10k_skb_unmap(dev, msdu);
	if (ret)
		ath10k_warn("data skb unmap failed (%d)\n", ret);

	if (skb_cb->htt.frag_len)
		skb_pull(msdu, skb_cb->htt.frag_len + skb_cb->htt.pad_len);

	ath10k_report_offchan_tx(htt->ar, msdu);

	info = IEEE80211_SKB_CB(msdu);
	memset(&info->status, 0, sizeof(info->status));

	if (tx_done->discard) {
		ieee80211_free_txskb(htt->ar->hw, msdu);
		goto exit;
	}

	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK))
		info->flags |= IEEE80211_TX_STAT_ACK;

	if (tx_done->no_ack)
		info->flags &= ~IEEE80211_TX_STAT_ACK;

	ieee80211_tx_status(htt->ar->hw, msdu);
	/* we do not own the msdu anymore */

exit:
	spin_lock_bh(&htt->tx_lock);
	htt->pending_tx[tx_done->msdu_id] = NULL;
	ath10k_htt_tx_free_msdu_id(htt, tx_done->msdu_id);
	__ath10k_htt_tx_dec_pending(htt);
	if (htt->num_pending_tx == 0)
		wake_up(&htt->empty_tx_wq);
	spin_unlock_bh(&htt->tx_lock);
}

static const u8 rx_legacy_rate_idx[] = {
	3,	/* 0x00  - 11Mbps  */
	2,	/* 0x01  - 5.5Mbps */
	1,	/* 0x02  - 2Mbps   */
	0,	/* 0x03  - 1Mbps   */
	3,	/* 0x04  - 11Mbps  */
	2,	/* 0x05  - 5.5Mbps */
	1,	/* 0x06  - 2Mbps   */
	0,	/* 0x07  - 1Mbps   */
	10,	/* 0x08  - 48Mbps  */
	8,	/* 0x09  - 24Mbps  */
	6,	/* 0x0A  - 12Mbps  */
	4,	/* 0x0B  - 6Mbps   */
	11,	/* 0x0C  - 54Mbps  */
	9,	/* 0x0D  - 36Mbps  */
	7,	/* 0x0E  - 18Mbps  */
	5,	/* 0x0F  - 9Mbps   */
};

static void process_rx_rates(struct ath10k *ar, struct htt_rx_info *info,
			     enum ieee80211_band band,
			     struct ieee80211_rx_status *status)
{
	u8 cck, rate, rate_idx, bw, sgi, mcs, nss;
	u8 info0 = info->rate.info0;
	u32 info1 = info->rate.info1;
	u32 info2 = info->rate.info2;
	u8 preamble = 0;

	/* Check if valid fields */
	if (!(info0 & HTT_RX_INDICATION_INFO0_START_VALID))
		return;

	preamble = MS(info1, HTT_RX_INDICATION_INFO1_PREAMBLE_TYPE);

	switch (preamble) {
	case HTT_RX_LEGACY:
		cck = info0 & HTT_RX_INDICATION_INFO0_LEGACY_RATE_CCK;
		rate = MS(info0, HTT_RX_INDICATION_INFO0_LEGACY_RATE);
		rate_idx = 0;

		if (rate < 0x08 || rate > 0x0F)
			break;

		switch (band) {
		case IEEE80211_BAND_2GHZ:
			if (cck)
				rate &= ~BIT(3);
			rate_idx = rx_legacy_rate_idx[rate];
			break;
		case IEEE80211_BAND_5GHZ:
			rate_idx = rx_legacy_rate_idx[rate];
			/* We are using same rate table registering
			   HW - ath10k_rates[]. In case of 5GHz skip
			   CCK rates, so -4 here */
			rate_idx -= 4;
			break;
		default:
			break;
		}

		status->rate_idx = rate_idx;
		break;
	case HTT_RX_HT:
	case HTT_RX_HT_WITH_TXBF:
		/* HT-SIG - Table 20-11 in info1 and info2 */
		mcs = info1 & 0x1F;
		nss = mcs >> 3;
		bw = (info1 >> 7) & 1;
		sgi = (info2 >> 7) & 1;

		status->rate_idx = mcs;
		status->flag |= RX_FLAG_HT;
		if (sgi)
			status->flag |= RX_FLAG_SHORT_GI;
		if (bw)
			status->flag |= RX_FLAG_40MHZ;
		break;
	case HTT_RX_VHT:
	case HTT_RX_VHT_WITH_TXBF:
		/* VHT-SIG-A1 in info 1, VHT-SIG-A2 in info2
		   TODO check this */
		mcs = (info2 >> 4) & 0x0F;
		nss = ((info1 >> 10) & 0x07) + 1;
		bw = info1 & 3;
		sgi = info2 & 1;

		status->rate_idx = mcs;
		status->vht_nss = nss;

		if (sgi)
			status->flag |= RX_FLAG_SHORT_GI;

		switch (bw) {
		/* 20MHZ */
		case 0:
			break;
		/* 40MHZ */
		case 1:
			status->flag |= RX_FLAG_40MHZ;
			break;
		/* 80MHZ */
		case 2:
			status->flag |= RX_FLAG_80MHZ;
		}

		status->flag |= RX_FLAG_VHT;
		break;
	default:
		break;
	}
}

void ath10k_process_rx(struct ath10k *ar, struct htt_rx_info *info)
{
	struct ieee80211_rx_status *status;
	struct ieee80211_channel *ch;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)info->skb->data;

	status = IEEE80211_SKB_RXCB(info->skb);
	memset(status, 0, sizeof(*status));

	if (info->encrypt_type != HTT_RX_MPDU_ENCRYPT_NONE) {
		status->flag |= RX_FLAG_DECRYPTED | RX_FLAG_IV_STRIPPED |
				RX_FLAG_MMIC_STRIPPED;
		hdr->frame_control = __cpu_to_le16(
				__le16_to_cpu(hdr->frame_control) &
				~IEEE80211_FCTL_PROTECTED);
	}

	if (info->mic_err)
		status->flag |= RX_FLAG_MMIC_ERROR;

	if (info->fcs_err)
		status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (info->amsdu_more)
		status->flag |= RX_FLAG_AMSDU_MORE;

	status->signal = info->signal;

	spin_lock_bh(&ar->data_lock);
	ch = ar->scan_channel;
	if (!ch)
		ch = ar->rx_channel;
	spin_unlock_bh(&ar->data_lock);

	if (!ch) {
		ath10k_warn("no channel configured; ignoring frame!\n");
		dev_kfree_skb_any(info->skb);
		return;
	}

	process_rx_rates(ar, info, ch->band, status);
	status->band = ch->band;
	status->freq = ch->center_freq;

	ath10k_dbg(ATH10K_DBG_DATA,
		   "rx skb %p len %u %s%s%s%s%s %srate_idx %u vht_nss %u freq %u band %u\n",
		   info->skb,
		   info->skb->len,
		   status->flag == 0 ? "legacy" : "",
		   status->flag & RX_FLAG_HT ? "ht" : "",
		   status->flag & RX_FLAG_VHT ? "vht" : "",
		   status->flag & RX_FLAG_40MHZ ? "40" : "",
		   status->flag & RX_FLAG_80MHZ ? "80" : "",
		   status->flag & RX_FLAG_SHORT_GI ? "sgi " : "",
		   status->rate_idx,
		   status->vht_nss,
		   status->freq,
		   status->band);
	ath10k_dbg_dump(ATH10K_DBG_HTT_DUMP, NULL, "rx skb: ",
			info->skb->data, info->skb->len);

	ieee80211_rx(ar->hw, info->skb);
}

struct ath10k_peer *ath10k_peer_find(struct ath10k *ar, int vdev_id,
				     const u8 *addr)
{
	struct ath10k_peer *peer;

	lockdep_assert_held(&ar->data_lock);

	list_for_each_entry(peer, &ar->peers, list) {
		if (peer->vdev_id != vdev_id)
			continue;
		if (memcmp(peer->addr, addr, ETH_ALEN))
			continue;

		return peer;
	}

	return NULL;
}

static struct ath10k_peer *ath10k_peer_find_by_id(struct ath10k *ar,
						  int peer_id)
{
	struct ath10k_peer *peer;

	lockdep_assert_held(&ar->data_lock);

	list_for_each_entry(peer, &ar->peers, list)
		if (test_bit(peer_id, peer->peer_ids))
			return peer;

	return NULL;
}

static int ath10k_wait_for_peer_common(struct ath10k *ar, int vdev_id,
				       const u8 *addr, bool expect_mapped)
{
	int ret;

	ret = wait_event_timeout(ar->peer_mapping_wq, ({
			bool mapped;

			spin_lock_bh(&ar->data_lock);
			mapped = !!ath10k_peer_find(ar, vdev_id, addr);
			spin_unlock_bh(&ar->data_lock);

			mapped == expect_mapped;
		}), 3*HZ);

	if (ret <= 0)
		return -ETIMEDOUT;

	return 0;
}

int ath10k_wait_for_peer_created(struct ath10k *ar, int vdev_id, const u8 *addr)
{
	return ath10k_wait_for_peer_common(ar, vdev_id, addr, true);
}

int ath10k_wait_for_peer_deleted(struct ath10k *ar, int vdev_id, const u8 *addr)
{
	return ath10k_wait_for_peer_common(ar, vdev_id, addr, false);
}

void ath10k_peer_map_event(struct ath10k_htt *htt,
			   struct htt_peer_map_event *ev)
{
	struct ath10k *ar = htt->ar;
	struct ath10k_peer *peer;

	spin_lock_bh(&ar->data_lock);
	peer = ath10k_peer_find(ar, ev->vdev_id, ev->addr);
	if (!peer) {
		peer = kzalloc(sizeof(*peer), GFP_ATOMIC);
		if (!peer)
			goto exit;

		peer->vdev_id = ev->vdev_id;
		memcpy(peer->addr, ev->addr, ETH_ALEN);
		list_add(&peer->list, &ar->peers);
		wake_up(&ar->peer_mapping_wq);
	}

	ath10k_dbg(ATH10K_DBG_HTT, "htt peer map vdev %d peer %pM id %d\n",
		   ev->vdev_id, ev->addr, ev->peer_id);

	set_bit(ev->peer_id, peer->peer_ids);
exit:
	spin_unlock_bh(&ar->data_lock);
}

void ath10k_peer_unmap_event(struct ath10k_htt *htt,
			     struct htt_peer_unmap_event *ev)
{
	struct ath10k *ar = htt->ar;
	struct ath10k_peer *peer;

	spin_lock_bh(&ar->data_lock);
	peer = ath10k_peer_find_by_id(ar, ev->peer_id);
	if (!peer) {
		ath10k_warn("unknown peer id %d\n", ev->peer_id);
		goto exit;
	}

	ath10k_dbg(ATH10K_DBG_HTT, "htt peer unmap vdev %d peer %pM id %d\n",
		   peer->vdev_id, peer->addr, ev->peer_id);

	clear_bit(ev->peer_id, peer->peer_ids);

	if (bitmap_empty(peer->peer_ids, ATH10K_MAX_NUM_PEER_IDS)) {
		list_del(&peer->list);
		kfree(peer);
		wake_up(&ar->peer_mapping_wq);
	}

exit:
	spin_unlock_bh(&ar->data_lock);
}
