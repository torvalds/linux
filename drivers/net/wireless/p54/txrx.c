/*
 * Common code for mac80211 Prism54 drivers
 *
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright (c) 2007-2009, Christian Lamparter <chunkeey@web.de>
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 *
 * Based on:
 * - the islsm (softmac prism54) driver, which is:
 *   Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 * - stlc45xx driver
 *   Copyright (C) 2008 Nokia Corporation and/or its subsidiary(-ies).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/firmware.h>
#include <linux/etherdevice.h>
#include <asm/div64.h>

#include <net/mac80211.h>

#include "p54.h"
#include "lmac.h"

#ifdef P54_MM_DEBUG
static void p54_dump_tx_queue(struct p54_common *priv)
{
	unsigned long flags;
	struct ieee80211_tx_info *info;
	struct p54_tx_info *range;
	struct sk_buff *skb;
	struct p54_hdr *hdr;
	unsigned int i = 0;
	u32 prev_addr;
	u32 largest_hole = 0, free;

	spin_lock_irqsave(&priv->tx_queue.lock, flags);
	wiphy_debug(priv->hw->wiphy, "/ --- tx queue dump (%d entries) ---\n",
		    skb_queue_len(&priv->tx_queue));

	prev_addr = priv->rx_start;
	skb_queue_walk(&priv->tx_queue, skb) {
		info = IEEE80211_SKB_CB(skb);
		range = (void *) info->rate_driver_data;
		hdr = (void *) skb->data;

		free = range->start_addr - prev_addr;
		wiphy_debug(priv->hw->wiphy,
			    "| [%02d] => [skb:%p skb_len:0x%04x "
			    "hdr:{flags:%02x len:%04x req_id:%04x type:%02x} "
			    "mem:{start:%04x end:%04x, free:%d}]\n",
			    i++, skb, skb->len,
			    le16_to_cpu(hdr->flags), le16_to_cpu(hdr->len),
			    le32_to_cpu(hdr->req_id), le16_to_cpu(hdr->type),
			    range->start_addr, range->end_addr, free);

		prev_addr = range->end_addr;
		largest_hole = max(largest_hole, free);
	}
	free = priv->rx_end - prev_addr;
	largest_hole = max(largest_hole, free);
	wiphy_debug(priv->hw->wiphy,
		    "\\ --- [free: %d], largest free block: %d ---\n",
		    free, largest_hole);
	spin_unlock_irqrestore(&priv->tx_queue.lock, flags);
}
#endif /* P54_MM_DEBUG */

/*
 * So, the firmware is somewhat stupid and doesn't know what places in its
 * memory incoming data should go to. By poking around in the firmware, we
 * can find some unused memory to upload our packets to. However, data that we
 * want the card to TX needs to stay intact until the card has told us that
 * it is done with it. This function finds empty places we can upload to and
 * marks allocated areas as reserved if necessary. p54_find_and_unlink_skb or
 * p54_free_skb frees allocated areas.
 */
static int p54_assign_address(struct p54_common *priv, struct sk_buff *skb)
{
	struct sk_buff *entry, *target_skb = NULL;
	struct ieee80211_tx_info *info;
	struct p54_tx_info *range;
	struct p54_hdr *data = (void *) skb->data;
	unsigned long flags;
	u32 last_addr = priv->rx_start;
	u32 target_addr = priv->rx_start;
	u16 len = priv->headroom + skb->len + priv->tailroom + 3;

	info = IEEE80211_SKB_CB(skb);
	range = (void *) info->rate_driver_data;
	len = (range->extra_len + len) & ~0x3;

	spin_lock_irqsave(&priv->tx_queue.lock, flags);
	if (unlikely(skb_queue_len(&priv->tx_queue) == 32)) {
		/*
		 * The tx_queue is now really full.
		 *
		 * TODO: check if the device has crashed and reset it.
		 */
		spin_unlock_irqrestore(&priv->tx_queue.lock, flags);
		return -EBUSY;
	}

	skb_queue_walk(&priv->tx_queue, entry) {
		u32 hole_size;
		info = IEEE80211_SKB_CB(entry);
		range = (void *) info->rate_driver_data;
		hole_size = range->start_addr - last_addr;

		if (!target_skb && hole_size >= len) {
			target_skb = entry->prev;
			hole_size -= len;
			target_addr = last_addr;
			break;
		}
		last_addr = range->end_addr;
	}
	if (unlikely(!target_skb)) {
		if (priv->rx_end - last_addr >= len) {
			target_skb = priv->tx_queue.prev;
			if (!skb_queue_empty(&priv->tx_queue)) {
				info = IEEE80211_SKB_CB(target_skb);
				range = (void *)info->rate_driver_data;
				target_addr = range->end_addr;
			}
		} else {
			spin_unlock_irqrestore(&priv->tx_queue.lock, flags);
			return -ENOSPC;
		}
	}

	info = IEEE80211_SKB_CB(skb);
	range = (void *) info->rate_driver_data;
	range->start_addr = target_addr;
	range->end_addr = target_addr + len;
	data->req_id = cpu_to_le32(target_addr + priv->headroom);
	if (IS_DATA_FRAME(skb) &&
	    unlikely(GET_HW_QUEUE(skb) == P54_QUEUE_BEACON))
		priv->beacon_req_id = data->req_id;

	__skb_queue_after(&priv->tx_queue, target_skb, skb);
	spin_unlock_irqrestore(&priv->tx_queue.lock, flags);
	return 0;
}

static void p54_tx_pending(struct p54_common *priv)
{
	struct sk_buff *skb;
	int ret;

	skb = skb_dequeue(&priv->tx_pending);
	if (unlikely(!skb))
		return ;

	ret = p54_assign_address(priv, skb);
	if (unlikely(ret))
		skb_queue_head(&priv->tx_pending, skb);
	else
		priv->tx(priv->hw, skb);
}

static void p54_wake_queues(struct p54_common *priv)
{
	unsigned long flags;
	unsigned int i;

	if (unlikely(priv->mode == NL80211_IFTYPE_UNSPECIFIED))
		return ;

	p54_tx_pending(priv);

	spin_lock_irqsave(&priv->tx_stats_lock, flags);
	for (i = 0; i < priv->hw->queues; i++) {
		if (priv->tx_stats[i + P54_QUEUE_DATA].len <
		    priv->tx_stats[i + P54_QUEUE_DATA].limit)
			ieee80211_wake_queue(priv->hw, i);
	}
	spin_unlock_irqrestore(&priv->tx_stats_lock, flags);
}

static int p54_tx_qos_accounting_alloc(struct p54_common *priv,
				       struct sk_buff *skb,
				       const u16 p54_queue)
{
	struct p54_tx_queue_stats *queue;
	unsigned long flags;

	if (WARN_ON(p54_queue >= P54_QUEUE_NUM))
		return -EINVAL;

	queue = &priv->tx_stats[p54_queue];

	spin_lock_irqsave(&priv->tx_stats_lock, flags);
	if (unlikely(queue->len >= queue->limit && IS_QOS_QUEUE(p54_queue))) {
		spin_unlock_irqrestore(&priv->tx_stats_lock, flags);
		return -ENOSPC;
	}

	queue->len++;
	queue->count++;

	if (unlikely(queue->len == queue->limit && IS_QOS_QUEUE(p54_queue))) {
		u16 ac_queue = p54_queue - P54_QUEUE_DATA;
		ieee80211_stop_queue(priv->hw, ac_queue);
	}

	spin_unlock_irqrestore(&priv->tx_stats_lock, flags);
	return 0;
}

static void p54_tx_qos_accounting_free(struct p54_common *priv,
				       struct sk_buff *skb)
{
	if (IS_DATA_FRAME(skb)) {
		unsigned long flags;

		spin_lock_irqsave(&priv->tx_stats_lock, flags);
		priv->tx_stats[GET_HW_QUEUE(skb)].len--;
		spin_unlock_irqrestore(&priv->tx_stats_lock, flags);

		if (unlikely(GET_HW_QUEUE(skb) == P54_QUEUE_BEACON)) {
			if (priv->beacon_req_id == GET_REQ_ID(skb)) {
				/* this is the  active beacon set anymore */
				priv->beacon_req_id = 0;
			}
			complete(&priv->beacon_comp);
		}
	}
	p54_wake_queues(priv);
}

void p54_free_skb(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	struct p54_common *priv = dev->priv;
	if (unlikely(!skb))
		return ;

	skb_unlink(skb, &priv->tx_queue);
	p54_tx_qos_accounting_free(priv, skb);
	dev_kfree_skb_any(skb);
}
EXPORT_SYMBOL_GPL(p54_free_skb);

static struct sk_buff *p54_find_and_unlink_skb(struct p54_common *priv,
					       const __le32 req_id)
{
	struct sk_buff *entry;
	unsigned long flags;

	spin_lock_irqsave(&priv->tx_queue.lock, flags);
	skb_queue_walk(&priv->tx_queue, entry) {
		struct p54_hdr *hdr = (struct p54_hdr *) entry->data;

		if (hdr->req_id == req_id) {
			__skb_unlink(entry, &priv->tx_queue);
			spin_unlock_irqrestore(&priv->tx_queue.lock, flags);
			p54_tx_qos_accounting_free(priv, entry);
			return entry;
		}
	}
	spin_unlock_irqrestore(&priv->tx_queue.lock, flags);
	return NULL;
}

void p54_tx(struct p54_common *priv, struct sk_buff *skb)
{
	skb_queue_tail(&priv->tx_pending, skb);
	p54_tx_pending(priv);
}

static int p54_rssi_to_dbm(struct p54_common *priv, int rssi)
{
	if (priv->rxhw != 5) {
		return ((rssi * priv->cur_rssi->mul) / 64 +
			 priv->cur_rssi->add) / 4;
	} else {
		/*
		 * TODO: find the correct formula
		 */
		return rssi / 2 - 110;
	}
}

/*
 * Even if the firmware is capable of dealing with incoming traffic,
 * while dozing, we have to prepared in case mac80211 uses PS-POLL
 * to retrieve outstanding frames from our AP.
 * (see comment in net/mac80211/mlme.c @ line 1993)
 */
static void p54_pspoll_workaround(struct p54_common *priv, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (void *) skb->data;
	struct ieee80211_tim_ie *tim_ie;
	u8 *tim;
	u8 tim_len;
	bool new_psm;

	/* only beacons have a TIM IE */
	if (!ieee80211_is_beacon(hdr->frame_control))
		return;

	if (!priv->aid)
		return;

	/* only consider beacons from the associated BSSID */
	if (compare_ether_addr(hdr->addr3, priv->bssid))
		return;

	tim = p54_find_ie(skb, WLAN_EID_TIM);
	if (!tim)
		return;

	tim_len = tim[1];
	tim_ie = (struct ieee80211_tim_ie *) &tim[2];

	new_psm = ieee80211_check_tim(tim_ie, tim_len, priv->aid);
	if (new_psm != priv->powersave_override) {
		priv->powersave_override = new_psm;
		p54_set_ps(priv);
	}
}

static int p54_rx_data(struct p54_common *priv, struct sk_buff *skb)
{
	struct p54_rx_data *hdr = (struct p54_rx_data *) skb->data;
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	u16 freq = le16_to_cpu(hdr->freq);
	size_t header_len = sizeof(*hdr);
	u32 tsf32;
	u8 rate = hdr->rate & 0xf;

	/*
	 * If the device is in a unspecified state we have to
	 * ignore all data frames. Else we could end up with a
	 * nasty crash.
	 */
	if (unlikely(priv->mode == NL80211_IFTYPE_UNSPECIFIED))
		return 0;

	if (!(hdr->flags & cpu_to_le16(P54_HDR_FLAG_DATA_IN_FCS_GOOD)))
		return 0;

	if (hdr->decrypt_status == P54_DECRYPT_OK)
		rx_status->flag |= RX_FLAG_DECRYPTED;
	if ((hdr->decrypt_status == P54_DECRYPT_FAIL_MICHAEL) ||
	    (hdr->decrypt_status == P54_DECRYPT_FAIL_TKIP))
		rx_status->flag |= RX_FLAG_MMIC_ERROR;

	rx_status->signal = p54_rssi_to_dbm(priv, hdr->rssi);
	if (hdr->rate & 0x10)
		rx_status->flag |= RX_FLAG_SHORTPRE;
	if (priv->hw->conf.channel->band == IEEE80211_BAND_5GHZ)
		rx_status->rate_idx = (rate < 4) ? 0 : rate - 4;
	else
		rx_status->rate_idx = rate;

	rx_status->freq = freq;
	rx_status->band =  priv->hw->conf.channel->band;
	rx_status->antenna = hdr->antenna;

	tsf32 = le32_to_cpu(hdr->tsf32);
	if (tsf32 < priv->tsf_low32)
		priv->tsf_high32++;
	rx_status->mactime = ((u64)priv->tsf_high32) << 32 | tsf32;
	priv->tsf_low32 = tsf32;

	rx_status->flag |= RX_FLAG_MACTIME_MPDU;

	if (hdr->flags & cpu_to_le16(P54_HDR_FLAG_DATA_ALIGN))
		header_len += hdr->align[0];

	skb_pull(skb, header_len);
	skb_trim(skb, le16_to_cpu(hdr->len));
	if (unlikely(priv->hw->conf.flags & IEEE80211_CONF_PS))
		p54_pspoll_workaround(priv, skb);

	ieee80211_rx_irqsafe(priv->hw, skb);

	ieee80211_queue_delayed_work(priv->hw, &priv->work,
			   msecs_to_jiffies(P54_STATISTICS_UPDATE));

	return -1;
}

static void p54_rx_frame_sent(struct p54_common *priv, struct sk_buff *skb)
{
	struct p54_hdr *hdr = (struct p54_hdr *) skb->data;
	struct p54_frame_sent *payload = (struct p54_frame_sent *) hdr->data;
	struct ieee80211_tx_info *info;
	struct p54_hdr *entry_hdr;
	struct p54_tx_data *entry_data;
	struct sk_buff *entry;
	unsigned int pad = 0, frame_len;
	int count, idx;

	entry = p54_find_and_unlink_skb(priv, hdr->req_id);
	if (unlikely(!entry))
		return ;

	frame_len = entry->len;
	info = IEEE80211_SKB_CB(entry);
	entry_hdr = (struct p54_hdr *) entry->data;
	entry_data = (struct p54_tx_data *) entry_hdr->data;
	priv->stats.dot11ACKFailureCount += payload->tries - 1;

	/*
	 * Frames in P54_QUEUE_FWSCAN and P54_QUEUE_BEACON are
	 * generated by the driver. Therefore tx_status is bogus
	 * and we don't want to confuse the mac80211 stack.
	 */
	if (unlikely(entry_data->hw_queue < P54_QUEUE_FWSCAN)) {
		dev_kfree_skb_any(entry);
		return ;
	}

	/*
	 * Clear manually, ieee80211_tx_info_clear_status would
	 * clear the counts too and we need them.
	 */
	memset(&info->status.ampdu_ack_len, 0,
	       sizeof(struct ieee80211_tx_info) -
	       offsetof(struct ieee80211_tx_info, status.ampdu_ack_len));
	BUILD_BUG_ON(offsetof(struct ieee80211_tx_info,
			      status.ampdu_ack_len) != 23);

	if (entry_hdr->flags & cpu_to_le16(P54_HDR_FLAG_DATA_ALIGN))
		pad = entry_data->align[0];

	/* walk through the rates array and adjust the counts */
	count = payload->tries;
	for (idx = 0; idx < 4; idx++) {
		if (count >= info->status.rates[idx].count) {
			count -= info->status.rates[idx].count;
		} else if (count > 0) {
			info->status.rates[idx].count = count;
			count = 0;
		} else {
			info->status.rates[idx].idx = -1;
			info->status.rates[idx].count = 0;
		}
	}

	if (!(info->flags & IEEE80211_TX_CTL_NO_ACK) &&
	     !(payload->status & P54_TX_FAILED))
		info->flags |= IEEE80211_TX_STAT_ACK;
	if (payload->status & P54_TX_PSM_CANCELLED)
		info->flags |= IEEE80211_TX_STAT_TX_FILTERED;
	info->status.ack_signal = p54_rssi_to_dbm(priv,
						  (int)payload->ack_rssi);

	/* Undo all changes to the frame. */
	switch (entry_data->key_type) {
	case P54_CRYPTO_TKIPMICHAEL: {
		u8 *iv = (u8 *)(entry_data->align + pad +
				entry_data->crypt_offset);

		/* Restore the original TKIP IV. */
		iv[2] = iv[0];
		iv[0] = iv[1];
		iv[1] = (iv[0] | 0x20) & 0x7f;	/* WEPSeed - 8.3.2.2 */

		frame_len -= 12; /* remove TKIP_MMIC + TKIP_ICV */
		break;
		}
	case P54_CRYPTO_AESCCMP:
		frame_len -= 8; /* remove CCMP_MIC */
		break;
	case P54_CRYPTO_WEP:
		frame_len -= 4; /* remove WEP_ICV */
		break;
	}

	skb_trim(entry, frame_len);
	skb_pull(entry, sizeof(*hdr) + pad + sizeof(*entry_data));
	ieee80211_tx_status_irqsafe(priv->hw, entry);
}

static void p54_rx_eeprom_readback(struct p54_common *priv,
				   struct sk_buff *skb)
{
	struct p54_hdr *hdr = (struct p54_hdr *) skb->data;
	struct p54_eeprom_lm86 *eeprom = (struct p54_eeprom_lm86 *) hdr->data;
	struct sk_buff *tmp;

	if (!priv->eeprom)
		return ;

	if (priv->fw_var >= 0x509) {
		memcpy(priv->eeprom, eeprom->v2.data,
		       le16_to_cpu(eeprom->v2.len));
	} else {
		memcpy(priv->eeprom, eeprom->v1.data,
		       le16_to_cpu(eeprom->v1.len));
	}

	priv->eeprom = NULL;
	tmp = p54_find_and_unlink_skb(priv, hdr->req_id);
	dev_kfree_skb_any(tmp);
	complete(&priv->eeprom_comp);
}

static void p54_rx_stats(struct p54_common *priv, struct sk_buff *skb)
{
	struct p54_hdr *hdr = (struct p54_hdr *) skb->data;
	struct p54_statistics *stats = (struct p54_statistics *) hdr->data;
	struct sk_buff *tmp;
	struct ieee80211_channel *chan;
	unsigned int i, rssi, tx, cca, dtime, dtotal, dcca, dtx, drssi, unit;
	u32 tsf32;

	if (unlikely(priv->mode == NL80211_IFTYPE_UNSPECIFIED))
		return ;

	tsf32 = le32_to_cpu(stats->tsf32);
	if (tsf32 < priv->tsf_low32)
		priv->tsf_high32++;
	priv->tsf_low32 = tsf32;

	priv->stats.dot11RTSFailureCount = le32_to_cpu(stats->rts_fail);
	priv->stats.dot11RTSSuccessCount = le32_to_cpu(stats->rts_success);
	priv->stats.dot11FCSErrorCount = le32_to_cpu(stats->rx_bad_fcs);

	priv->noise = p54_rssi_to_dbm(priv, le32_to_cpu(stats->noise));

	/*
	 * STSW450X LMAC API page 26 - 3.8 Statistics
	 * "The exact measurement period can be derived from the
	 * timestamp member".
	 */
	dtime = tsf32 - priv->survey_raw.timestamp;

	/*
	 * STSW450X LMAC API page 26 - 3.8.1 Noise histogram
	 * The LMAC samples RSSI, CCA and transmit state at regular
	 * periods (typically 8 times per 1k [as in 1024] usec).
	 */
	cca = le32_to_cpu(stats->sample_cca);
	tx = le32_to_cpu(stats->sample_tx);
	rssi = 0;
	for (i = 0; i < ARRAY_SIZE(stats->sample_noise); i++)
		rssi += le32_to_cpu(stats->sample_noise[i]);

	dcca = cca - priv->survey_raw.cached_cca;
	drssi = rssi - priv->survey_raw.cached_rssi;
	dtx = tx - priv->survey_raw.cached_tx;
	dtotal = dcca + drssi + dtx;

	/*
	 * update statistics when more than a second is over since the
	 * last call, or when a update is badly needed.
	 */
	if (dtotal && (priv->update_stats || dtime >= USEC_PER_SEC) &&
	    dtime >= dtotal) {
		priv->survey_raw.timestamp = tsf32;
		priv->update_stats = false;
		unit = dtime / dtotal;

		if (dcca) {
			priv->survey_raw.cca += dcca * unit;
			priv->survey_raw.cached_cca = cca;
		}
		if (dtx) {
			priv->survey_raw.tx += dtx * unit;
			priv->survey_raw.cached_tx = tx;
		}
		if (drssi) {
			priv->survey_raw.rssi += drssi * unit;
			priv->survey_raw.cached_rssi = rssi;
		}

		/* 1024 usec / 8 times = 128 usec / time */
		if (!(priv->phy_ps || priv->phy_idle))
			priv->survey_raw.active += dtotal * unit;
		else
			priv->survey_raw.active += (dcca + dtx) * unit;
	}

	chan = priv->curchan;
	if (chan) {
		struct survey_info *survey = &priv->survey[chan->hw_value];
		survey->noise = clamp_t(s8, priv->noise, -128, 127);
		survey->channel_time = priv->survey_raw.active;
		survey->channel_time_tx = priv->survey_raw.tx;
		survey->channel_time_busy = priv->survey_raw.tx +
			priv->survey_raw.cca;
		do_div(survey->channel_time, 1024);
		do_div(survey->channel_time_tx, 1024);
		do_div(survey->channel_time_busy, 1024);
	}

	tmp = p54_find_and_unlink_skb(priv, hdr->req_id);
	dev_kfree_skb_any(tmp);
	complete(&priv->stat_comp);
}

static void p54_rx_trap(struct p54_common *priv, struct sk_buff *skb)
{
	struct p54_hdr *hdr = (struct p54_hdr *) skb->data;
	struct p54_trap *trap = (struct p54_trap *) hdr->data;
	u16 event = le16_to_cpu(trap->event);
	u16 freq = le16_to_cpu(trap->frequency);

	switch (event) {
	case P54_TRAP_BEACON_TX:
		break;
	case P54_TRAP_RADAR:
		wiphy_info(priv->hw->wiphy, "radar (freq:%d MHz)\n", freq);
		break;
	case P54_TRAP_NO_BEACON:
		if (priv->vif)
			ieee80211_beacon_loss(priv->vif);
		break;
	case P54_TRAP_SCAN:
		break;
	case P54_TRAP_TBTT:
		break;
	case P54_TRAP_TIMER:
		break;
	case P54_TRAP_FAA_RADIO_OFF:
		wiphy_rfkill_set_hw_state(priv->hw->wiphy, true);
		break;
	case P54_TRAP_FAA_RADIO_ON:
		wiphy_rfkill_set_hw_state(priv->hw->wiphy, false);
		break;
	default:
		wiphy_info(priv->hw->wiphy, "received event:%x freq:%d\n",
			   event, freq);
		break;
	}
}

static int p54_rx_control(struct p54_common *priv, struct sk_buff *skb)
{
	struct p54_hdr *hdr = (struct p54_hdr *) skb->data;

	switch (le16_to_cpu(hdr->type)) {
	case P54_CONTROL_TYPE_TXDONE:
		p54_rx_frame_sent(priv, skb);
		break;
	case P54_CONTROL_TYPE_TRAP:
		p54_rx_trap(priv, skb);
		break;
	case P54_CONTROL_TYPE_BBP:
		break;
	case P54_CONTROL_TYPE_STAT_READBACK:
		p54_rx_stats(priv, skb);
		break;
	case P54_CONTROL_TYPE_EEPROM_READBACK:
		p54_rx_eeprom_readback(priv, skb);
		break;
	default:
		wiphy_debug(priv->hw->wiphy,
			    "not handling 0x%02x type control frame\n",
			    le16_to_cpu(hdr->type));
		break;
	}
	return 0;
}

/* returns zero if skb can be reused */
int p54_rx(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	struct p54_common *priv = dev->priv;
	u16 type = le16_to_cpu(*((__le16 *)skb->data));

	if (type & P54_HDR_FLAG_CONTROL)
		return p54_rx_control(priv, skb);
	else
		return p54_rx_data(priv, skb);
}
EXPORT_SYMBOL_GPL(p54_rx);

static void p54_tx_80211_header(struct p54_common *priv, struct sk_buff *skb,
				struct ieee80211_tx_info *info, u8 *queue,
				u32 *extra_len, u16 *flags, u16 *aid,
				bool *burst_possible)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	if (ieee80211_is_data_qos(hdr->frame_control))
		*burst_possible = true;
	else
		*burst_possible = false;

	if (!(info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ))
		*flags |= P54_HDR_FLAG_DATA_OUT_SEQNR;

	if (info->flags & IEEE80211_TX_CTL_POLL_RESPONSE)
		*flags |= P54_HDR_FLAG_DATA_OUT_NOCANCEL;

	if (info->flags & IEEE80211_TX_CTL_CLEAR_PS_FILT)
		*flags |= P54_HDR_FLAG_DATA_OUT_NOCANCEL;

	*queue = skb_get_queue_mapping(skb) + P54_QUEUE_DATA;

	switch (priv->mode) {
	case NL80211_IFTYPE_MONITOR:
		/*
		 * We have to set P54_HDR_FLAG_DATA_OUT_PROMISC for
		 * every frame in promiscuous/monitor mode.
		 * see STSW45x0C LMAC API - page 12.
		 */
		*aid = 0;
		*flags |= P54_HDR_FLAG_DATA_OUT_PROMISC;
		break;
	case NL80211_IFTYPE_STATION:
		*aid = 1;
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_MESH_POINT:
		if (info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM) {
			*aid = 0;
			*queue = P54_QUEUE_CAB;
			return;
		}

		if (unlikely(ieee80211_is_mgmt(hdr->frame_control))) {
			if (ieee80211_is_probe_resp(hdr->frame_control)) {
				*aid = 0;
				*flags |= P54_HDR_FLAG_DATA_OUT_TIMESTAMP |
					  P54_HDR_FLAG_DATA_OUT_NOCANCEL;
				return;
			} else if (ieee80211_is_beacon(hdr->frame_control)) {
				*aid = 0;

				if (info->flags & IEEE80211_TX_CTL_INJECTED) {
					/*
					 * Injecting beacons on top of a AP is
					 * not a good idea... nevertheless,
					 * it should be doable.
					 */

					return;
				}

				*flags |= P54_HDR_FLAG_DATA_OUT_TIMESTAMP;
				*queue = P54_QUEUE_BEACON;
				*extra_len = IEEE80211_MAX_TIM_LEN;
				return;
			}
		}

		if (info->control.sta)
			*aid = info->control.sta->aid;
		break;
	}
}

static u8 p54_convert_algo(u32 cipher)
{
	switch (cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		return P54_CRYPTO_WEP;
	case WLAN_CIPHER_SUITE_TKIP:
		return P54_CRYPTO_TKIPMICHAEL;
	case WLAN_CIPHER_SUITE_CCMP:
		return P54_CRYPTO_AESCCMP;
	default:
		return 0;
	}
}

void p54_tx_80211(struct ieee80211_hw *dev, struct sk_buff *skb)
{
	struct p54_common *priv = dev->priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct p54_tx_info *p54info;
	struct p54_hdr *hdr;
	struct p54_tx_data *txhdr;
	unsigned int padding, len, extra_len = 0;
	int i, j, ridx;
	u16 hdr_flags = 0, aid = 0;
	u8 rate, queue = 0, crypt_offset = 0;
	u8 cts_rate = 0x20;
	u8 rc_flags;
	u8 calculated_tries[4];
	u8 nrates = 0, nremaining = 8;
	bool burst_allowed = false;

	p54_tx_80211_header(priv, skb, info, &queue, &extra_len,
			    &hdr_flags, &aid, &burst_allowed);

	if (p54_tx_qos_accounting_alloc(priv, skb, queue)) {
		dev_kfree_skb_any(skb);
		return;
	}

	padding = (unsigned long)(skb->data - (sizeof(*hdr) + sizeof(*txhdr))) & 3;
	len = skb->len;

	if (info->control.hw_key) {
		crypt_offset = ieee80211_get_hdrlen_from_skb(skb);
		if (info->control.hw_key->cipher == WLAN_CIPHER_SUITE_TKIP) {
			u8 *iv = (u8 *)(skb->data + crypt_offset);
			/*
			 * The firmware excepts that the IV has to have
			 * this special format
			 */
			iv[1] = iv[0];
			iv[0] = iv[2];
			iv[2] = 0;
		}
	}

	txhdr = (struct p54_tx_data *) skb_push(skb, sizeof(*txhdr) + padding);
	hdr = (struct p54_hdr *) skb_push(skb, sizeof(*hdr));

	if (padding)
		hdr_flags |= P54_HDR_FLAG_DATA_ALIGN;
	hdr->type = cpu_to_le16(aid);
	hdr->rts_tries = info->control.rates[0].count;

	/*
	 * we register the rates in perfect order, and
	 * RTS/CTS won't happen on 5 GHz
	 */
	cts_rate = info->control.rts_cts_rate_idx;

	memset(&txhdr->rateset, 0, sizeof(txhdr->rateset));

	/* see how many rates got used */
	for (i = 0; i < dev->max_rates; i++) {
		if (info->control.rates[i].idx < 0)
			break;
		nrates++;
	}

	/* limit tries to 8/nrates per rate */
	for (i = 0; i < nrates; i++) {
		/*
		 * The magic expression here is equivalent to 8/nrates for
		 * all values that matter, but avoids division and jumps.
		 * Note that nrates can only take the values 1 through 4.
		 */
		calculated_tries[i] = min_t(int, ((15 >> nrates) | 1) + 1,
						 info->control.rates[i].count);
		nremaining -= calculated_tries[i];
	}

	/* if there are tries left, distribute from back to front */
	for (i = nrates - 1; nremaining > 0 && i >= 0; i--) {
		int tmp = info->control.rates[i].count - calculated_tries[i];

		if (tmp <= 0)
			continue;
		/* RC requested more tries at this rate */

		tmp = min_t(int, tmp, nremaining);
		calculated_tries[i] += tmp;
		nremaining -= tmp;
	}

	ridx = 0;
	for (i = 0; i < nrates && ridx < 8; i++) {
		/* we register the rates in perfect order */
		rate = info->control.rates[i].idx;
		if (info->band == IEEE80211_BAND_5GHZ)
			rate += 4;

		/* store the count we actually calculated for TX status */
		info->control.rates[i].count = calculated_tries[i];

		rc_flags = info->control.rates[i].flags;
		if (rc_flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE) {
			rate |= 0x10;
			cts_rate |= 0x10;
		}
		if (rc_flags & IEEE80211_TX_RC_USE_RTS_CTS) {
			burst_allowed = false;
			rate |= 0x40;
		} else if (rc_flags & IEEE80211_TX_RC_USE_CTS_PROTECT) {
			rate |= 0x20;
			burst_allowed = false;
		}
		for (j = 0; j < calculated_tries[i] && ridx < 8; j++) {
			txhdr->rateset[ridx] = rate;
			ridx++;
		}
	}

	if (burst_allowed)
		hdr_flags |= P54_HDR_FLAG_DATA_OUT_BURST;

	/* TODO: enable bursting */
	hdr->flags = cpu_to_le16(hdr_flags);
	hdr->tries = ridx;
	txhdr->rts_rate_idx = 0;
	if (info->control.hw_key) {
		txhdr->key_type = p54_convert_algo(info->control.hw_key->cipher);
		txhdr->key_len = min((u8)16, info->control.hw_key->keylen);
		memcpy(txhdr->key, info->control.hw_key->key, txhdr->key_len);
		if (info->control.hw_key->cipher == WLAN_CIPHER_SUITE_TKIP) {
			/* reserve space for the MIC key */
			len += 8;
			memcpy(skb_put(skb, 8), &(info->control.hw_key->key
				[NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY]), 8);
		}
		/* reserve some space for ICV */
		len += info->control.hw_key->icv_len;
		memset(skb_put(skb, info->control.hw_key->icv_len), 0,
		       info->control.hw_key->icv_len);
	} else {
		txhdr->key_type = 0;
		txhdr->key_len = 0;
	}
	txhdr->crypt_offset = crypt_offset;
	txhdr->hw_queue = queue;
	txhdr->backlog = priv->tx_stats[queue].len - 1;
	memset(txhdr->durations, 0, sizeof(txhdr->durations));
	txhdr->tx_antenna = ((info->antenna_sel_tx == 0) ?
		2 : info->antenna_sel_tx - 1) & priv->tx_diversity_mask;
	if (priv->rxhw == 5) {
		txhdr->longbow.cts_rate = cts_rate;
		txhdr->longbow.output_power = cpu_to_le16(priv->output_power);
	} else {
		txhdr->normal.output_power = priv->output_power;
		txhdr->normal.cts_rate = cts_rate;
	}
	if (padding)
		txhdr->align[0] = padding;

	hdr->len = cpu_to_le16(len);
	/* modifies skb->cb and with it info, so must be last! */
	p54info = (void *) info->rate_driver_data;
	p54info->extra_len = extra_len;

	p54_tx(priv, skb);
}
