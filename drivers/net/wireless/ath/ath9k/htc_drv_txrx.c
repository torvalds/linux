/*
 * Copyright (c) 2010 Atheros Communications Inc.
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

#include "htc.h"

/******/
/* TX */
/******/

static const int subtype_txq_to_hwq[] = {
	[WME_AC_BE] = ATH_TXQ_AC_BE,
	[WME_AC_BK] = ATH_TXQ_AC_BK,
	[WME_AC_VI] = ATH_TXQ_AC_VI,
	[WME_AC_VO] = ATH_TXQ_AC_VO,
};

#define ATH9K_HTC_INIT_TXQ(subtype) do {			\
		qi.tqi_subtype = subtype_txq_to_hwq[subtype];	\
		qi.tqi_aifs = ATH9K_TXQ_USEDEFAULT;		\
		qi.tqi_cwmin = ATH9K_TXQ_USEDEFAULT;		\
		qi.tqi_cwmax = ATH9K_TXQ_USEDEFAULT;		\
		qi.tqi_physCompBuf = 0;				\
		qi.tqi_qflags = TXQ_FLAG_TXEOLINT_ENABLE |	\
			TXQ_FLAG_TXDESCINT_ENABLE;		\
	} while (0)

int get_hw_qnum(u16 queue, int *hwq_map)
{
	switch (queue) {
	case 0:
		return hwq_map[WME_AC_VO];
	case 1:
		return hwq_map[WME_AC_VI];
	case 2:
		return hwq_map[WME_AC_BE];
	case 3:
		return hwq_map[WME_AC_BK];
	default:
		return hwq_map[WME_AC_BE];
	}
}

int ath_htc_txq_update(struct ath9k_htc_priv *priv, int qnum,
		       struct ath9k_tx_queue_info *qinfo)
{
	struct ath_hw *ah = priv->ah;
	int error = 0;
	struct ath9k_tx_queue_info qi;

	ath9k_hw_get_txq_props(ah, qnum, &qi);

	qi.tqi_aifs = qinfo->tqi_aifs;
	qi.tqi_cwmin = qinfo->tqi_cwmin / 2; /* XXX */
	qi.tqi_cwmax = qinfo->tqi_cwmax;
	qi.tqi_burstTime = qinfo->tqi_burstTime;
	qi.tqi_readyTime = qinfo->tqi_readyTime;

	if (!ath9k_hw_set_txq_props(ah, qnum, &qi)) {
		ath_err(ath9k_hw_common(ah),
			"Unable to update hardware queue %u!\n", qnum);
		error = -EIO;
	} else {
		ath9k_hw_resettxqueue(ah, qnum);
	}

	return error;
}

int ath9k_htc_tx_start(struct ath9k_htc_priv *priv, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_sta *sta = tx_info->control.sta;
	struct ath9k_htc_sta *ista;
	struct ath9k_htc_tx_ctl tx_ctl;
	enum htc_endpoint_id epid;
	u16 qnum;
	__le16 fc;
	u8 *tx_fhdr;
	u8 sta_idx, vif_idx;

	hdr = (struct ieee80211_hdr *) skb->data;
	fc = hdr->frame_control;

	if (tx_info->control.vif &&
			(struct ath9k_htc_vif *) tx_info->control.vif->drv_priv)
		vif_idx = ((struct ath9k_htc_vif *)
				tx_info->control.vif->drv_priv)->index;
	else
		vif_idx = priv->nvifs;

	if (sta) {
		ista = (struct ath9k_htc_sta *) sta->drv_priv;
		sta_idx = ista->index;
	} else {
		sta_idx = 0;
	}

	memset(&tx_ctl, 0, sizeof(struct ath9k_htc_tx_ctl));

	if (ieee80211_is_data(fc)) {
		struct tx_frame_hdr tx_hdr;
		u8 *qc;

		memset(&tx_hdr, 0, sizeof(struct tx_frame_hdr));

		tx_hdr.node_idx = sta_idx;
		tx_hdr.vif_idx = vif_idx;

		if (tx_info->flags & IEEE80211_TX_CTL_AMPDU) {
			tx_ctl.type = ATH9K_HTC_AMPDU;
			tx_hdr.data_type = ATH9K_HTC_AMPDU;
		} else {
			tx_ctl.type = ATH9K_HTC_NORMAL;
			tx_hdr.data_type = ATH9K_HTC_NORMAL;
		}

		if (ieee80211_is_data_qos(fc)) {
			qc = ieee80211_get_qos_ctl(hdr);
			tx_hdr.tidno = qc[0] & IEEE80211_QOS_CTL_TID_MASK;
		}

		/* Check for RTS protection */
		if (priv->hw->wiphy->rts_threshold != (u32) -1)
			if (skb->len > priv->hw->wiphy->rts_threshold)
				tx_hdr.flags |= ATH9K_HTC_TX_RTSCTS;

		/* CTS-to-self */
		if (!(tx_hdr.flags & ATH9K_HTC_TX_RTSCTS) &&
		    (priv->op_flags & OP_PROTECT_ENABLE))
			tx_hdr.flags |= ATH9K_HTC_TX_CTSONLY;

		tx_hdr.key_type = ath9k_cmn_get_hw_crypto_keytype(skb);
		if (tx_hdr.key_type == ATH9K_KEY_TYPE_CLEAR)
			tx_hdr.keyix = (u8) ATH9K_TXKEYIX_INVALID;
		else
			tx_hdr.keyix = tx_info->control.hw_key->hw_key_idx;

		tx_fhdr = skb_push(skb, sizeof(tx_hdr));
		memcpy(tx_fhdr, (u8 *) &tx_hdr, sizeof(tx_hdr));

		qnum = skb_get_queue_mapping(skb);

		switch (qnum) {
		case 0:
			TX_QSTAT_INC(WME_AC_VO);
			epid = priv->data_vo_ep;
			break;
		case 1:
			TX_QSTAT_INC(WME_AC_VI);
			epid = priv->data_vi_ep;
			break;
		case 2:
			TX_QSTAT_INC(WME_AC_BE);
			epid = priv->data_be_ep;
			break;
		case 3:
		default:
			TX_QSTAT_INC(WME_AC_BK);
			epid = priv->data_bk_ep;
			break;
		}
	} else {
		struct tx_mgmt_hdr mgmt_hdr;

		memset(&mgmt_hdr, 0, sizeof(struct tx_mgmt_hdr));

		tx_ctl.type = ATH9K_HTC_NORMAL;

		mgmt_hdr.node_idx = sta_idx;
		mgmt_hdr.vif_idx = vif_idx;
		mgmt_hdr.tidno = 0;
		mgmt_hdr.flags = 0;

		mgmt_hdr.key_type = ath9k_cmn_get_hw_crypto_keytype(skb);
		if (mgmt_hdr.key_type == ATH9K_KEY_TYPE_CLEAR)
			mgmt_hdr.keyix = (u8) ATH9K_TXKEYIX_INVALID;
		else
			mgmt_hdr.keyix = tx_info->control.hw_key->hw_key_idx;

		tx_fhdr = skb_push(skb, sizeof(mgmt_hdr));
		memcpy(tx_fhdr, (u8 *) &mgmt_hdr, sizeof(mgmt_hdr));
		epid = priv->mgmt_ep;
	}

	return htc_send(priv->htc, skb, epid, &tx_ctl);
}

static bool ath9k_htc_check_tx_aggr(struct ath9k_htc_priv *priv,
				    struct ath9k_htc_sta *ista, u8 tid)
{
	bool ret = false;

	spin_lock_bh(&priv->tx_lock);
	if ((tid < ATH9K_HTC_MAX_TID) && (ista->tid_state[tid] == AGGR_STOP))
		ret = true;
	spin_unlock_bh(&priv->tx_lock);

	return ret;
}

void ath9k_tx_tasklet(unsigned long data)
{
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *)data;
	struct ieee80211_sta *sta;
	struct ieee80211_hdr *hdr;
	struct ieee80211_tx_info *tx_info;
	struct sk_buff *skb = NULL;
	__le16 fc;

	while ((skb = skb_dequeue(&priv->tx_queue)) != NULL) {

		hdr = (struct ieee80211_hdr *) skb->data;
		fc = hdr->frame_control;
		tx_info = IEEE80211_SKB_CB(skb);

		memset(&tx_info->status, 0, sizeof(tx_info->status));

		rcu_read_lock();

		sta = ieee80211_find_sta(priv->vif, hdr->addr1);
		if (!sta) {
			rcu_read_unlock();
			ieee80211_tx_status(priv->hw, skb);
			continue;
		}

		/* Check if we need to start aggregation */

		if (sta && conf_is_ht(&priv->hw->conf) &&
		    !(skb->protocol == cpu_to_be16(ETH_P_PAE))) {
			if (ieee80211_is_data_qos(fc)) {
				u8 *qc, tid;
				struct ath9k_htc_sta *ista;

				qc = ieee80211_get_qos_ctl(hdr);
				tid = qc[0] & 0xf;
				ista = (struct ath9k_htc_sta *)sta->drv_priv;

				if (ath9k_htc_check_tx_aggr(priv, ista, tid)) {
					ieee80211_start_tx_ba_session(sta, tid);
					spin_lock_bh(&priv->tx_lock);
					ista->tid_state[tid] = AGGR_PROGRESS;
					spin_unlock_bh(&priv->tx_lock);
				}
			}
		}

		rcu_read_unlock();

		/* Send status to mac80211 */
		ieee80211_tx_status(priv->hw, skb);
	}

	/* Wake TX queues if needed */
	spin_lock_bh(&priv->tx_lock);
	if (priv->tx_queues_stop) {
		priv->tx_queues_stop = false;
		spin_unlock_bh(&priv->tx_lock);
		ath_print(ath9k_hw_common(priv->ah), ATH_DBG_XMIT,
			  "Waking up TX queues\n");
		ieee80211_wake_queues(priv->hw);
		return;
	}
	spin_unlock_bh(&priv->tx_lock);
}

void ath9k_htc_txep(void *drv_priv, struct sk_buff *skb,
		    enum htc_endpoint_id ep_id, bool txok)
{
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *) drv_priv;
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ieee80211_tx_info *tx_info;

	if (!skb)
		return;

	if (ep_id == priv->mgmt_ep) {
		skb_pull(skb, sizeof(struct tx_mgmt_hdr));
	} else if ((ep_id == priv->data_bk_ep) ||
		   (ep_id == priv->data_be_ep) ||
		   (ep_id == priv->data_vi_ep) ||
		   (ep_id == priv->data_vo_ep)) {
		skb_pull(skb, sizeof(struct tx_frame_hdr));
	} else {
		ath_err(common, "Unsupported TX EPID: %d\n", ep_id);
		dev_kfree_skb_any(skb);
		return;
	}

	tx_info = IEEE80211_SKB_CB(skb);

	if (txok)
		tx_info->flags |= IEEE80211_TX_STAT_ACK;

	skb_queue_tail(&priv->tx_queue, skb);
	tasklet_schedule(&priv->tx_tasklet);
}

int ath9k_tx_init(struct ath9k_htc_priv *priv)
{
	skb_queue_head_init(&priv->tx_queue);
	return 0;
}

void ath9k_tx_cleanup(struct ath9k_htc_priv *priv)
{

}

bool ath9k_htc_txq_setup(struct ath9k_htc_priv *priv, int subtype)
{
	struct ath_hw *ah = priv->ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_tx_queue_info qi;
	int qnum;

	memset(&qi, 0, sizeof(qi));
	ATH9K_HTC_INIT_TXQ(subtype);

	qnum = ath9k_hw_setuptxqueue(priv->ah, ATH9K_TX_QUEUE_DATA, &qi);
	if (qnum == -1)
		return false;

	if (qnum >= ARRAY_SIZE(priv->hwq_map)) {
		ath_err(common, "qnum %u out of range, max %zu!\n",
			qnum, ARRAY_SIZE(priv->hwq_map));
		ath9k_hw_releasetxqueue(ah, qnum);
		return false;
	}

	priv->hwq_map[subtype] = qnum;
	return true;
}

int ath9k_htc_cabq_setup(struct ath9k_htc_priv *priv)
{
	struct ath9k_tx_queue_info qi;

	memset(&qi, 0, sizeof(qi));
	ATH9K_HTC_INIT_TXQ(0);

	return ath9k_hw_setuptxqueue(priv->ah, ATH9K_TX_QUEUE_CAB, &qi);
}

/******/
/* RX */
/******/

/*
 * Calculate the RX filter to be set in the HW.
 */
u32 ath9k_htc_calcrxfilter(struct ath9k_htc_priv *priv)
{
#define	RX_FILTER_PRESERVE (ATH9K_RX_FILTER_PHYERR | ATH9K_RX_FILTER_PHYRADAR)

	struct ath_hw *ah = priv->ah;
	u32 rfilt;

	rfilt = (ath9k_hw_getrxfilter(ah) & RX_FILTER_PRESERVE)
		| ATH9K_RX_FILTER_UCAST | ATH9K_RX_FILTER_BCAST
		| ATH9K_RX_FILTER_MCAST;

	if (priv->rxfilter & FIF_PROBE_REQ)
		rfilt |= ATH9K_RX_FILTER_PROBEREQ;

	/*
	 * Set promiscuous mode when FIF_PROMISC_IN_BSS is enabled for station
	 * mode interface or when in monitor mode. AP mode does not need this
	 * since it receives all in-BSS frames anyway.
	 */
	if (((ah->opmode != NL80211_IFTYPE_AP) &&
	     (priv->rxfilter & FIF_PROMISC_IN_BSS)) ||
	    (ah->opmode == NL80211_IFTYPE_MONITOR))
		rfilt |= ATH9K_RX_FILTER_PROM;

	if (priv->rxfilter & FIF_CONTROL)
		rfilt |= ATH9K_RX_FILTER_CONTROL;

	if ((ah->opmode == NL80211_IFTYPE_STATION) &&
	    !(priv->rxfilter & FIF_BCN_PRBRESP_PROMISC))
		rfilt |= ATH9K_RX_FILTER_MYBEACON;
	else
		rfilt |= ATH9K_RX_FILTER_BEACON;

	if (conf_is_ht(&priv->hw->conf))
		rfilt |= ATH9K_RX_FILTER_COMP_BAR;

	return rfilt;

#undef RX_FILTER_PRESERVE
}

/*
 * Recv initialization for opmode change.
 */
static void ath9k_htc_opmode_init(struct ath9k_htc_priv *priv)
{
	struct ath_hw *ah = priv->ah;
	struct ath_common *common = ath9k_hw_common(ah);

	u32 rfilt, mfilt[2];

	/* configure rx filter */
	rfilt = ath9k_htc_calcrxfilter(priv);
	ath9k_hw_setrxfilter(ah, rfilt);

	/* configure bssid mask */
	ath_hw_setbssidmask(common);

	/* configure operational mode */
	ath9k_hw_setopmode(ah);

	/* calculate and install multicast filter */
	mfilt[0] = mfilt[1] = ~0;
	ath9k_hw_setmcastfilter(ah, mfilt[0], mfilt[1]);
}

void ath9k_host_rx_init(struct ath9k_htc_priv *priv)
{
	ath9k_hw_rxena(priv->ah);
	ath9k_htc_opmode_init(priv);
	ath9k_hw_startpcureceive(priv->ah, (priv->op_flags & OP_SCANNING));
	priv->rx.last_rssi = ATH_RSSI_DUMMY_MARKER;
}

static void ath9k_process_rate(struct ieee80211_hw *hw,
			       struct ieee80211_rx_status *rxs,
			       u8 rx_rate, u8 rs_flags)
{
	struct ieee80211_supported_band *sband;
	enum ieee80211_band band;
	unsigned int i = 0;

	if (rx_rate & 0x80) {
		/* HT rate */
		rxs->flag |= RX_FLAG_HT;
		if (rs_flags & ATH9K_RX_2040)
			rxs->flag |= RX_FLAG_40MHZ;
		if (rs_flags & ATH9K_RX_GI)
			rxs->flag |= RX_FLAG_SHORT_GI;
		rxs->rate_idx = rx_rate & 0x7f;
		return;
	}

	band = hw->conf.channel->band;
	sband = hw->wiphy->bands[band];

	for (i = 0; i < sband->n_bitrates; i++) {
		if (sband->bitrates[i].hw_value == rx_rate) {
			rxs->rate_idx = i;
			return;
		}
		if (sband->bitrates[i].hw_value_short == rx_rate) {
			rxs->rate_idx = i;
			rxs->flag |= RX_FLAG_SHORTPRE;
			return;
		}
	}

}

static bool ath9k_rx_prepare(struct ath9k_htc_priv *priv,
			     struct ath9k_htc_rxbuf *rxbuf,
			     struct ieee80211_rx_status *rx_status)

{
	struct ieee80211_hdr *hdr;
	struct ieee80211_hw *hw = priv->hw;
	struct sk_buff *skb = rxbuf->skb;
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct ath_htc_rx_status *rxstatus;
	int hdrlen, padpos, padsize;
	int last_rssi = ATH_RSSI_DUMMY_MARKER;
	__le16 fc;

	if (skb->len <= HTC_RX_FRAME_HEADER_SIZE) {
		ath_err(common, "Corrupted RX frame, dropping\n");
		goto rx_next;
	}

	rxstatus = (struct ath_htc_rx_status *)skb->data;

	if (be16_to_cpu(rxstatus->rs_datalen) -
	    (skb->len - HTC_RX_FRAME_HEADER_SIZE) != 0) {
		ath_err(common,
			"Corrupted RX data len, dropping (dlen: %d, skblen: %d)\n",
			rxstatus->rs_datalen, skb->len);
		goto rx_next;
	}

	/* Get the RX status information */
	memcpy(&rxbuf->rxstatus, rxstatus, HTC_RX_FRAME_HEADER_SIZE);
	skb_pull(skb, HTC_RX_FRAME_HEADER_SIZE);

	hdr = (struct ieee80211_hdr *)skb->data;
	fc = hdr->frame_control;
	hdrlen = ieee80211_get_hdrlen_from_skb(skb);

	padpos = ath9k_cmn_padpos(fc);

	padsize = padpos & 3;
	if (padsize && skb->len >= padpos+padsize+FCS_LEN) {
		memmove(skb->data + padsize, skb->data, padpos);
		skb_pull(skb, padsize);
	}

	memset(rx_status, 0, sizeof(struct ieee80211_rx_status));

	if (rxbuf->rxstatus.rs_status != 0) {
		if (rxbuf->rxstatus.rs_status & ATH9K_RXERR_CRC)
			rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;
		if (rxbuf->rxstatus.rs_status & ATH9K_RXERR_PHY)
			goto rx_next;

		if (rxbuf->rxstatus.rs_status & ATH9K_RXERR_DECRYPT) {
			/* FIXME */
		} else if (rxbuf->rxstatus.rs_status & ATH9K_RXERR_MIC) {
			if (ieee80211_is_ctl(fc))
				/*
				 * Sometimes, we get invalid
				 * MIC failures on valid control frames.
				 * Remove these mic errors.
				 */
				rxbuf->rxstatus.rs_status &= ~ATH9K_RXERR_MIC;
			else
				rx_status->flag |= RX_FLAG_MMIC_ERROR;
		}

		/*
		 * Reject error frames with the exception of
		 * decryption and MIC failures. For monitor mode,
		 * we also ignore the CRC error.
		 */
		if (priv->ah->opmode == NL80211_IFTYPE_MONITOR) {
			if (rxbuf->rxstatus.rs_status &
			    ~(ATH9K_RXERR_DECRYPT | ATH9K_RXERR_MIC |
			      ATH9K_RXERR_CRC))
				goto rx_next;
		} else {
			if (rxbuf->rxstatus.rs_status &
			    ~(ATH9K_RXERR_DECRYPT | ATH9K_RXERR_MIC)) {
				goto rx_next;
			}
		}
	}

	if (!(rxbuf->rxstatus.rs_status & ATH9K_RXERR_DECRYPT)) {
		u8 keyix;
		keyix = rxbuf->rxstatus.rs_keyix;
		if (keyix != ATH9K_RXKEYIX_INVALID) {
			rx_status->flag |= RX_FLAG_DECRYPTED;
		} else if (ieee80211_has_protected(fc) &&
			   skb->len >= hdrlen + 4) {
			keyix = skb->data[hdrlen + 3] >> 6;
			if (test_bit(keyix, common->keymap))
				rx_status->flag |= RX_FLAG_DECRYPTED;
		}
	}

	ath9k_process_rate(hw, rx_status, rxbuf->rxstatus.rs_rate,
			   rxbuf->rxstatus.rs_flags);

	if (priv->op_flags & OP_ASSOCIATED) {
		if (rxbuf->rxstatus.rs_rssi != ATH9K_RSSI_BAD &&
		    !rxbuf->rxstatus.rs_moreaggr)
			ATH_RSSI_LPF(priv->rx.last_rssi,
				     rxbuf->rxstatus.rs_rssi);

		last_rssi = priv->rx.last_rssi;

		if (likely(last_rssi != ATH_RSSI_DUMMY_MARKER))
			rxbuf->rxstatus.rs_rssi = ATH_EP_RND(last_rssi,
							     ATH_RSSI_EP_MULTIPLIER);

		if (rxbuf->rxstatus.rs_rssi < 0)
			rxbuf->rxstatus.rs_rssi = 0;

		if (ieee80211_is_beacon(fc))
			priv->ah->stats.avgbrssi = rxbuf->rxstatus.rs_rssi;
	}

	rx_status->mactime = be64_to_cpu(rxbuf->rxstatus.rs_tstamp);
	rx_status->band = hw->conf.channel->band;
	rx_status->freq = hw->conf.channel->center_freq;
	rx_status->signal =  rxbuf->rxstatus.rs_rssi + ATH_DEFAULT_NOISE_FLOOR;
	rx_status->antenna = rxbuf->rxstatus.rs_antenna;
	rx_status->flag |= RX_FLAG_TSFT;

	return true;

rx_next:
	return false;
}

/*
 * FIXME: Handle FLUSH later on.
 */
void ath9k_rx_tasklet(unsigned long data)
{
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *)data;
	struct ath9k_htc_rxbuf *rxbuf = NULL, *tmp_buf = NULL;
	struct ieee80211_rx_status rx_status;
	struct sk_buff *skb;
	unsigned long flags;
	struct ieee80211_hdr *hdr;

	do {
		spin_lock_irqsave(&priv->rx.rxbuflock, flags);
		list_for_each_entry(tmp_buf, &priv->rx.rxbuf, list) {
			if (tmp_buf->in_process) {
				rxbuf = tmp_buf;
				break;
			}
		}

		if (rxbuf == NULL) {
			spin_unlock_irqrestore(&priv->rx.rxbuflock, flags);
			break;
		}

		if (!rxbuf->skb)
			goto requeue;

		if (!ath9k_rx_prepare(priv, rxbuf, &rx_status)) {
			dev_kfree_skb_any(rxbuf->skb);
			goto requeue;
		}

		memcpy(IEEE80211_SKB_RXCB(rxbuf->skb), &rx_status,
		       sizeof(struct ieee80211_rx_status));
		skb = rxbuf->skb;
		hdr = (struct ieee80211_hdr *) skb->data;

		if (ieee80211_is_beacon(hdr->frame_control) && priv->ps_enabled)
				ieee80211_queue_work(priv->hw, &priv->ps_work);

		spin_unlock_irqrestore(&priv->rx.rxbuflock, flags);

		ieee80211_rx(priv->hw, skb);

		spin_lock_irqsave(&priv->rx.rxbuflock, flags);
requeue:
		rxbuf->in_process = false;
		rxbuf->skb = NULL;
		list_move_tail(&rxbuf->list, &priv->rx.rxbuf);
		rxbuf = NULL;
		spin_unlock_irqrestore(&priv->rx.rxbuflock, flags);
	} while (1);

}

void ath9k_htc_rxep(void *drv_priv, struct sk_buff *skb,
		    enum htc_endpoint_id ep_id)
{
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *)drv_priv;
	struct ath_hw *ah = priv->ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_htc_rxbuf *rxbuf = NULL, *tmp_buf = NULL;

	spin_lock(&priv->rx.rxbuflock);
	list_for_each_entry(tmp_buf, &priv->rx.rxbuf, list) {
		if (!tmp_buf->in_process) {
			rxbuf = tmp_buf;
			break;
		}
	}
	spin_unlock(&priv->rx.rxbuflock);

	if (rxbuf == NULL) {
		ath_print(common, ATH_DBG_ANY,
			  "No free RX buffer\n");
		goto err;
	}

	spin_lock(&priv->rx.rxbuflock);
	rxbuf->skb = skb;
	rxbuf->in_process = true;
	spin_unlock(&priv->rx.rxbuflock);

	tasklet_schedule(&priv->rx_tasklet);
	return;
err:
	dev_kfree_skb_any(skb);
}

/* FIXME: Locking for cleanup/init */

void ath9k_rx_cleanup(struct ath9k_htc_priv *priv)
{
	struct ath9k_htc_rxbuf *rxbuf, *tbuf;

	list_for_each_entry_safe(rxbuf, tbuf, &priv->rx.rxbuf, list) {
		list_del(&rxbuf->list);
		if (rxbuf->skb)
			dev_kfree_skb_any(rxbuf->skb);
		kfree(rxbuf);
	}
}

int ath9k_rx_init(struct ath9k_htc_priv *priv)
{
	struct ath_hw *ah = priv->ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_htc_rxbuf *rxbuf;
	int i = 0;

	INIT_LIST_HEAD(&priv->rx.rxbuf);
	spin_lock_init(&priv->rx.rxbuflock);

	for (i = 0; i < ATH9K_HTC_RXBUF; i++) {
		rxbuf = kzalloc(sizeof(struct ath9k_htc_rxbuf), GFP_KERNEL);
		if (rxbuf == NULL) {
			ath_err(common, "Unable to allocate RX buffers\n");
			goto err;
		}
		list_add_tail(&rxbuf->list, &priv->rx.rxbuf);
	}

	return 0;

err:
	ath9k_rx_cleanup(priv);
	return -ENOMEM;
}
