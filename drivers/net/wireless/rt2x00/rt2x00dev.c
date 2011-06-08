/*
	Copyright (C) 2010 Willow Garage <http://www.willowgarage.com>
	Copyright (C) 2004 - 2010 Ivo van Doorn <IvDoorn@gmail.com>
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: rt2x00lib
	Abstract: rt2x00 generic device routines.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/log2.h>

#include "rt2x00.h"
#include "rt2x00lib.h"

/*
 * Radio control handlers.
 */
int rt2x00lib_enable_radio(struct rt2x00_dev *rt2x00dev)
{
	int status;

	/*
	 * Don't enable the radio twice.
	 * And check if the hardware button has been disabled.
	 */
	if (test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		return 0;

	/*
	 * Initialize all data queues.
	 */
	rt2x00queue_init_queues(rt2x00dev);

	/*
	 * Enable radio.
	 */
	status =
	    rt2x00dev->ops->lib->set_device_state(rt2x00dev, STATE_RADIO_ON);
	if (status)
		return status;

	rt2x00dev->ops->lib->set_device_state(rt2x00dev, STATE_RADIO_IRQ_ON);

	rt2x00leds_led_radio(rt2x00dev, true);
	rt2x00led_led_activity(rt2x00dev, true);

	set_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags);

	/*
	 * Enable queues.
	 */
	rt2x00queue_start_queues(rt2x00dev);
	rt2x00link_start_tuner(rt2x00dev);
	rt2x00link_start_agc(rt2x00dev);

	/*
	 * Start watchdog monitoring.
	 */
	rt2x00link_start_watchdog(rt2x00dev);

	return 0;
}

void rt2x00lib_disable_radio(struct rt2x00_dev *rt2x00dev)
{
	if (!test_and_clear_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		return;

	/*
	 * Stop watchdog monitoring.
	 */
	rt2x00link_stop_watchdog(rt2x00dev);

	/*
	 * Stop all queues
	 */
	rt2x00link_stop_agc(rt2x00dev);
	rt2x00link_stop_tuner(rt2x00dev);
	rt2x00queue_stop_queues(rt2x00dev);
	rt2x00queue_flush_queues(rt2x00dev, true);

	/*
	 * Disable radio.
	 */
	rt2x00dev->ops->lib->set_device_state(rt2x00dev, STATE_RADIO_OFF);
	rt2x00dev->ops->lib->set_device_state(rt2x00dev, STATE_RADIO_IRQ_OFF);
	rt2x00led_led_activity(rt2x00dev, false);
	rt2x00leds_led_radio(rt2x00dev, false);
}

static void rt2x00lib_intf_scheduled_iter(void *data, u8 *mac,
					  struct ieee80211_vif *vif)
{
	struct rt2x00_dev *rt2x00dev = data;
	struct rt2x00_intf *intf = vif_to_intf(vif);

	/*
	 * It is possible the radio was disabled while the work had been
	 * scheduled. If that happens we should return here immediately,
	 * note that in the spinlock protected area above the delayed_flags
	 * have been cleared correctly.
	 */
	if (!test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		return;

	if (test_and_clear_bit(DELAYED_UPDATE_BEACON, &intf->delayed_flags))
		rt2x00queue_update_beacon(rt2x00dev, vif);
}

static void rt2x00lib_intf_scheduled(struct work_struct *work)
{
	struct rt2x00_dev *rt2x00dev =
	    container_of(work, struct rt2x00_dev, intf_work);

	/*
	 * Iterate over each interface and perform the
	 * requested configurations.
	 */
	ieee80211_iterate_active_interfaces(rt2x00dev->hw,
					    rt2x00lib_intf_scheduled_iter,
					    rt2x00dev);
}

static void rt2x00lib_autowakeup(struct work_struct *work)
{
	struct rt2x00_dev *rt2x00dev =
	    container_of(work, struct rt2x00_dev, autowakeup_work.work);

	if (rt2x00dev->ops->lib->set_device_state(rt2x00dev, STATE_AWAKE))
		ERROR(rt2x00dev, "Device failed to wakeup.\n");
	clear_bit(CONFIG_POWERSAVING, &rt2x00dev->flags);
}

/*
 * Interrupt context handlers.
 */
static void rt2x00lib_bc_buffer_iter(void *data, u8 *mac,
				     struct ieee80211_vif *vif)
{
	struct rt2x00_dev *rt2x00dev = data;
	struct sk_buff *skb;

	/*
	 * Only AP mode interfaces do broad- and multicast buffering
	 */
	if (vif->type != NL80211_IFTYPE_AP)
		return;

	/*
	 * Send out buffered broad- and multicast frames
	 */
	skb = ieee80211_get_buffered_bc(rt2x00dev->hw, vif);
	while (skb) {
		rt2x00mac_tx(rt2x00dev->hw, skb);
		skb = ieee80211_get_buffered_bc(rt2x00dev->hw, vif);
	}
}

static void rt2x00lib_beaconupdate_iter(void *data, u8 *mac,
					struct ieee80211_vif *vif)
{
	struct rt2x00_dev *rt2x00dev = data;

	if (vif->type != NL80211_IFTYPE_AP &&
	    vif->type != NL80211_IFTYPE_ADHOC &&
	    vif->type != NL80211_IFTYPE_MESH_POINT &&
	    vif->type != NL80211_IFTYPE_WDS)
		return;

	/*
	 * Update the beacon without locking. This is safe on PCI devices
	 * as they only update the beacon periodically here. This should
	 * never be called for USB devices.
	 */
	WARN_ON(rt2x00_is_usb(rt2x00dev));
	rt2x00queue_update_beacon_locked(rt2x00dev, vif);
}

void rt2x00lib_beacondone(struct rt2x00_dev *rt2x00dev)
{
	if (!test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		return;

	/* send buffered bc/mc frames out for every bssid */
	ieee80211_iterate_active_interfaces_atomic(rt2x00dev->hw,
						   rt2x00lib_bc_buffer_iter,
						   rt2x00dev);
	/*
	 * Devices with pre tbtt interrupt don't need to update the beacon
	 * here as they will fetch the next beacon directly prior to
	 * transmission.
	 */
	if (test_bit(CAPABILITY_PRE_TBTT_INTERRUPT, &rt2x00dev->cap_flags))
		return;

	/* fetch next beacon */
	ieee80211_iterate_active_interfaces_atomic(rt2x00dev->hw,
						   rt2x00lib_beaconupdate_iter,
						   rt2x00dev);
}
EXPORT_SYMBOL_GPL(rt2x00lib_beacondone);

void rt2x00lib_pretbtt(struct rt2x00_dev *rt2x00dev)
{
	if (!test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		return;

	/* fetch next beacon */
	ieee80211_iterate_active_interfaces_atomic(rt2x00dev->hw,
						   rt2x00lib_beaconupdate_iter,
						   rt2x00dev);
}
EXPORT_SYMBOL_GPL(rt2x00lib_pretbtt);

void rt2x00lib_dmastart(struct queue_entry *entry)
{
	set_bit(ENTRY_OWNER_DEVICE_DATA, &entry->flags);
	rt2x00queue_index_inc(entry, Q_INDEX);
}
EXPORT_SYMBOL_GPL(rt2x00lib_dmastart);

void rt2x00lib_dmadone(struct queue_entry *entry)
{
	set_bit(ENTRY_DATA_STATUS_PENDING, &entry->flags);
	clear_bit(ENTRY_OWNER_DEVICE_DATA, &entry->flags);
	rt2x00queue_index_inc(entry, Q_INDEX_DMA_DONE);
}
EXPORT_SYMBOL_GPL(rt2x00lib_dmadone);

void rt2x00lib_txdone(struct queue_entry *entry,
		      struct txdone_entry_desc *txdesc)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(entry->skb);
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(entry->skb);
	unsigned int header_length, i;
	u8 rate_idx, rate_flags, retry_rates;
	u8 skbdesc_flags = skbdesc->flags;
	bool success;

	/*
	 * Unmap the skb.
	 */
	rt2x00queue_unmap_skb(entry);

	/*
	 * Remove the extra tx headroom from the skb.
	 */
	skb_pull(entry->skb, rt2x00dev->ops->extra_tx_headroom);

	/*
	 * Signal that the TX descriptor is no longer in the skb.
	 */
	skbdesc->flags &= ~SKBDESC_DESC_IN_SKB;

	/*
	 * Determine the length of 802.11 header.
	 */
	header_length = ieee80211_get_hdrlen_from_skb(entry->skb);

	/*
	 * Remove L2 padding which was added during
	 */
	if (test_bit(REQUIRE_L2PAD, &rt2x00dev->cap_flags))
		rt2x00queue_remove_l2pad(entry->skb, header_length);

	/*
	 * If the IV/EIV data was stripped from the frame before it was
	 * passed to the hardware, we should now reinsert it again because
	 * mac80211 will expect the same data to be present it the
	 * frame as it was passed to us.
	 */
	if (test_bit(CAPABILITY_HW_CRYPTO, &rt2x00dev->cap_flags))
		rt2x00crypto_tx_insert_iv(entry->skb, header_length);

	/*
	 * Send frame to debugfs immediately, after this call is completed
	 * we are going to overwrite the skb->cb array.
	 */
	rt2x00debug_dump_frame(rt2x00dev, DUMP_FRAME_TXDONE, entry->skb);

	/*
	 * Determine if the frame has been successfully transmitted.
	 */
	success =
	    test_bit(TXDONE_SUCCESS, &txdesc->flags) ||
	    test_bit(TXDONE_UNKNOWN, &txdesc->flags);

	/*
	 * Update TX statistics.
	 */
	rt2x00dev->link.qual.tx_success += success;
	rt2x00dev->link.qual.tx_failed += !success;

	rate_idx = skbdesc->tx_rate_idx;
	rate_flags = skbdesc->tx_rate_flags;
	retry_rates = test_bit(TXDONE_FALLBACK, &txdesc->flags) ?
	    (txdesc->retry + 1) : 1;

	/*
	 * Initialize TX status
	 */
	memset(&tx_info->status, 0, sizeof(tx_info->status));
	tx_info->status.ack_signal = 0;

	/*
	 * Frame was send with retries, hardware tried
	 * different rates to send out the frame, at each
	 * retry it lowered the rate 1 step except when the
	 * lowest rate was used.
	 */
	for (i = 0; i < retry_rates && i < IEEE80211_TX_MAX_RATES; i++) {
		tx_info->status.rates[i].idx = rate_idx - i;
		tx_info->status.rates[i].flags = rate_flags;

		if (rate_idx - i == 0) {
			/*
			 * The lowest rate (index 0) was used until the
			 * number of max retries was reached.
			 */
			tx_info->status.rates[i].count = retry_rates - i;
			i++;
			break;
		}
		tx_info->status.rates[i].count = 1;
	}
	if (i < (IEEE80211_TX_MAX_RATES - 1))
		tx_info->status.rates[i].idx = -1; /* terminate */

	if (!(tx_info->flags & IEEE80211_TX_CTL_NO_ACK)) {
		if (success)
			tx_info->flags |= IEEE80211_TX_STAT_ACK;
		else
			rt2x00dev->low_level_stats.dot11ACKFailureCount++;
	}

	/*
	 * Every single frame has it's own tx status, hence report
	 * every frame as ampdu of size 1.
	 *
	 * TODO: if we can find out how many frames were aggregated
	 * by the hw we could provide the real ampdu_len to mac80211
	 * which would allow the rc algorithm to better decide on
	 * which rates are suitable.
	 */
	if (test_bit(TXDONE_AMPDU, &txdesc->flags) ||
	    tx_info->flags & IEEE80211_TX_CTL_AMPDU) {
		tx_info->flags |= IEEE80211_TX_STAT_AMPDU;
		tx_info->status.ampdu_len = 1;
		tx_info->status.ampdu_ack_len = success ? 1 : 0;

		if (!success)
			tx_info->flags |= IEEE80211_TX_STAT_AMPDU_NO_BACK;
	}

	if (rate_flags & IEEE80211_TX_RC_USE_RTS_CTS) {
		if (success)
			rt2x00dev->low_level_stats.dot11RTSSuccessCount++;
		else
			rt2x00dev->low_level_stats.dot11RTSFailureCount++;
	}

	/*
	 * Only send the status report to mac80211 when it's a frame
	 * that originated in mac80211. If this was a extra frame coming
	 * through a mac80211 library call (RTS/CTS) then we should not
	 * send the status report back.
	 */
	if (!(skbdesc_flags & SKBDESC_NOT_MAC80211)) {
		if (test_bit(REQUIRE_TASKLET_CONTEXT, &rt2x00dev->cap_flags))
			ieee80211_tx_status(rt2x00dev->hw, entry->skb);
		else
			ieee80211_tx_status_ni(rt2x00dev->hw, entry->skb);
	} else
		dev_kfree_skb_any(entry->skb);

	/*
	 * Make this entry available for reuse.
	 */
	entry->skb = NULL;
	entry->flags = 0;

	rt2x00dev->ops->lib->clear_entry(entry);

	rt2x00queue_index_inc(entry, Q_INDEX_DONE);

	/*
	 * If the data queue was below the threshold before the txdone
	 * handler we must make sure the packet queue in the mac80211 stack
	 * is reenabled when the txdone handler has finished.
	 */
	if (!rt2x00queue_threshold(entry->queue))
		rt2x00queue_unpause_queue(entry->queue);
}
EXPORT_SYMBOL_GPL(rt2x00lib_txdone);

void rt2x00lib_txdone_noinfo(struct queue_entry *entry, u32 status)
{
	struct txdone_entry_desc txdesc;

	txdesc.flags = 0;
	__set_bit(status, &txdesc.flags);
	txdesc.retry = 0;

	rt2x00lib_txdone(entry, &txdesc);
}
EXPORT_SYMBOL_GPL(rt2x00lib_txdone_noinfo);

static u8 *rt2x00lib_find_ie(u8 *data, unsigned int len, u8 ie)
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

static void rt2x00lib_rxdone_check_ps(struct rt2x00_dev *rt2x00dev,
				      struct sk_buff *skb,
				      struct rxdone_entry_desc *rxdesc)
{
	struct ieee80211_hdr *hdr = (void *) skb->data;
	struct ieee80211_tim_ie *tim_ie;
	u8 *tim;
	u8 tim_len;
	bool cam;

	/* If this is not a beacon, or if mac80211 has no powersaving
	 * configured, or if the device is already in powersaving mode
	 * we can exit now. */
	if (likely(!ieee80211_is_beacon(hdr->frame_control) ||
		   !(rt2x00dev->hw->conf.flags & IEEE80211_CONF_PS)))
		return;

	/* min. beacon length + FCS_LEN */
	if (skb->len <= 40 + FCS_LEN)
		return;

	/* and only beacons from the associated BSSID, please */
	if (!(rxdesc->dev_flags & RXDONE_MY_BSS) ||
	    !rt2x00dev->aid)
		return;

	rt2x00dev->last_beacon = jiffies;

	tim = rt2x00lib_find_ie(skb->data, skb->len - FCS_LEN, WLAN_EID_TIM);
	if (!tim)
		return;

	if (tim[1] < sizeof(*tim_ie))
		return;

	tim_len = tim[1];
	tim_ie = (struct ieee80211_tim_ie *) &tim[2];

	/* Check whenever the PHY can be turned off again. */

	/* 1. What about buffered unicast traffic for our AID? */
	cam = ieee80211_check_tim(tim_ie, tim_len, rt2x00dev->aid);

	/* 2. Maybe the AP wants to send multicast/broadcast data? */
	cam |= (tim_ie->bitmap_ctrl & 0x01);

	if (!cam && !test_bit(CONFIG_POWERSAVING, &rt2x00dev->flags))
		rt2x00lib_config(rt2x00dev, &rt2x00dev->hw->conf,
				 IEEE80211_CONF_CHANGE_PS);
}

static int rt2x00lib_rxdone_read_signal(struct rt2x00_dev *rt2x00dev,
					struct rxdone_entry_desc *rxdesc)
{
	struct ieee80211_supported_band *sband;
	const struct rt2x00_rate *rate;
	unsigned int i;
	int signal = rxdesc->signal;
	int type = (rxdesc->dev_flags & RXDONE_SIGNAL_MASK);

	switch (rxdesc->rate_mode) {
	case RATE_MODE_CCK:
	case RATE_MODE_OFDM:
		/*
		 * For non-HT rates the MCS value needs to contain the
		 * actually used rate modulation (CCK or OFDM).
		 */
		if (rxdesc->dev_flags & RXDONE_SIGNAL_MCS)
			signal = RATE_MCS(rxdesc->rate_mode, signal);

		sband = &rt2x00dev->bands[rt2x00dev->curr_band];
		for (i = 0; i < sband->n_bitrates; i++) {
			rate = rt2x00_get_rate(sband->bitrates[i].hw_value);
			if (((type == RXDONE_SIGNAL_PLCP) &&
			     (rate->plcp == signal)) ||
			    ((type == RXDONE_SIGNAL_BITRATE) &&
			      (rate->bitrate == signal)) ||
			    ((type == RXDONE_SIGNAL_MCS) &&
			      (rate->mcs == signal))) {
				return i;
			}
		}
		break;
	case RATE_MODE_HT_MIX:
	case RATE_MODE_HT_GREENFIELD:
		if (signal >= 0 && signal <= 76)
			return signal;
		break;
	default:
		break;
	}

	WARNING(rt2x00dev, "Frame received with unrecognized signal, "
		"mode=0x%.4x, signal=0x%.4x, type=%d.\n",
		rxdesc->rate_mode, signal, type);
	return 0;
}

void rt2x00lib_rxdone(struct queue_entry *entry)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct rxdone_entry_desc rxdesc;
	struct sk_buff *skb;
	struct ieee80211_rx_status *rx_status;
	unsigned int header_length;
	int rate_idx;

	if (!test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags) ||
	    !test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		goto submit_entry;

	if (test_bit(ENTRY_DATA_IO_FAILED, &entry->flags))
		goto submit_entry;

	/*
	 * Allocate a new sk_buffer. If no new buffer available, drop the
	 * received frame and reuse the existing buffer.
	 */
	skb = rt2x00queue_alloc_rxskb(entry);
	if (!skb)
		goto submit_entry;

	/*
	 * Unmap the skb.
	 */
	rt2x00queue_unmap_skb(entry);

	/*
	 * Extract the RXD details.
	 */
	memset(&rxdesc, 0, sizeof(rxdesc));
	rt2x00dev->ops->lib->fill_rxdone(entry, &rxdesc);

	/*
	 * The data behind the ieee80211 header must be
	 * aligned on a 4 byte boundary.
	 */
	header_length = ieee80211_get_hdrlen_from_skb(entry->skb);

	/*
	 * Hardware might have stripped the IV/EIV/ICV data,
	 * in that case it is possible that the data was
	 * provided separately (through hardware descriptor)
	 * in which case we should reinsert the data into the frame.
	 */
	if ((rxdesc.dev_flags & RXDONE_CRYPTO_IV) &&
	    (rxdesc.flags & RX_FLAG_IV_STRIPPED))
		rt2x00crypto_rx_insert_iv(entry->skb, header_length,
					  &rxdesc);
	else if (header_length &&
		 (rxdesc.size > header_length) &&
		 (rxdesc.dev_flags & RXDONE_L2PAD))
		rt2x00queue_remove_l2pad(entry->skb, header_length);

	/* Trim buffer to correct size */
	skb_trim(entry->skb, rxdesc.size);

	/*
	 * Translate the signal to the correct bitrate index.
	 */
	rate_idx = rt2x00lib_rxdone_read_signal(rt2x00dev, &rxdesc);
	if (rxdesc.rate_mode == RATE_MODE_HT_MIX ||
	    rxdesc.rate_mode == RATE_MODE_HT_GREENFIELD)
		rxdesc.flags |= RX_FLAG_HT;

	/*
	 * Check if this is a beacon, and more frames have been
	 * buffered while we were in powersaving mode.
	 */
	rt2x00lib_rxdone_check_ps(rt2x00dev, entry->skb, &rxdesc);

	/*
	 * Update extra components
	 */
	rt2x00link_update_stats(rt2x00dev, entry->skb, &rxdesc);
	rt2x00debug_update_crypto(rt2x00dev, &rxdesc);
	rt2x00debug_dump_frame(rt2x00dev, DUMP_FRAME_RXDONE, entry->skb);

	/*
	 * Initialize RX status information, and send frame
	 * to mac80211.
	 */
	rx_status = IEEE80211_SKB_RXCB(entry->skb);
	rx_status->mactime = rxdesc.timestamp;
	rx_status->band = rt2x00dev->curr_band;
	rx_status->freq = rt2x00dev->curr_freq;
	rx_status->rate_idx = rate_idx;
	rx_status->signal = rxdesc.rssi;
	rx_status->flag = rxdesc.flags;
	rx_status->antenna = rt2x00dev->link.ant.active.rx;

	ieee80211_rx_ni(rt2x00dev->hw, entry->skb);

	/*
	 * Replace the skb with the freshly allocated one.
	 */
	entry->skb = skb;

submit_entry:
	entry->flags = 0;
	rt2x00queue_index_inc(entry, Q_INDEX_DONE);
	if (test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags) &&
	    test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		rt2x00dev->ops->lib->clear_entry(entry);
}
EXPORT_SYMBOL_GPL(rt2x00lib_rxdone);

/*
 * Driver initialization handlers.
 */
const struct rt2x00_rate rt2x00_supported_rates[12] = {
	{
		.flags = DEV_RATE_CCK,
		.bitrate = 10,
		.ratemask = BIT(0),
		.plcp = 0x00,
		.mcs = RATE_MCS(RATE_MODE_CCK, 0),
	},
	{
		.flags = DEV_RATE_CCK | DEV_RATE_SHORT_PREAMBLE,
		.bitrate = 20,
		.ratemask = BIT(1),
		.plcp = 0x01,
		.mcs = RATE_MCS(RATE_MODE_CCK, 1),
	},
	{
		.flags = DEV_RATE_CCK | DEV_RATE_SHORT_PREAMBLE,
		.bitrate = 55,
		.ratemask = BIT(2),
		.plcp = 0x02,
		.mcs = RATE_MCS(RATE_MODE_CCK, 2),
	},
	{
		.flags = DEV_RATE_CCK | DEV_RATE_SHORT_PREAMBLE,
		.bitrate = 110,
		.ratemask = BIT(3),
		.plcp = 0x03,
		.mcs = RATE_MCS(RATE_MODE_CCK, 3),
	},
	{
		.flags = DEV_RATE_OFDM,
		.bitrate = 60,
		.ratemask = BIT(4),
		.plcp = 0x0b,
		.mcs = RATE_MCS(RATE_MODE_OFDM, 0),
	},
	{
		.flags = DEV_RATE_OFDM,
		.bitrate = 90,
		.ratemask = BIT(5),
		.plcp = 0x0f,
		.mcs = RATE_MCS(RATE_MODE_OFDM, 1),
	},
	{
		.flags = DEV_RATE_OFDM,
		.bitrate = 120,
		.ratemask = BIT(6),
		.plcp = 0x0a,
		.mcs = RATE_MCS(RATE_MODE_OFDM, 2),
	},
	{
		.flags = DEV_RATE_OFDM,
		.bitrate = 180,
		.ratemask = BIT(7),
		.plcp = 0x0e,
		.mcs = RATE_MCS(RATE_MODE_OFDM, 3),
	},
	{
		.flags = DEV_RATE_OFDM,
		.bitrate = 240,
		.ratemask = BIT(8),
		.plcp = 0x09,
		.mcs = RATE_MCS(RATE_MODE_OFDM, 4),
	},
	{
		.flags = DEV_RATE_OFDM,
		.bitrate = 360,
		.ratemask = BIT(9),
		.plcp = 0x0d,
		.mcs = RATE_MCS(RATE_MODE_OFDM, 5),
	},
	{
		.flags = DEV_RATE_OFDM,
		.bitrate = 480,
		.ratemask = BIT(10),
		.plcp = 0x08,
		.mcs = RATE_MCS(RATE_MODE_OFDM, 6),
	},
	{
		.flags = DEV_RATE_OFDM,
		.bitrate = 540,
		.ratemask = BIT(11),
		.plcp = 0x0c,
		.mcs = RATE_MCS(RATE_MODE_OFDM, 7),
	},
};

static void rt2x00lib_channel(struct ieee80211_channel *entry,
			      const int channel, const int tx_power,
			      const int value)
{
	/* XXX: this assumption about the band is wrong for 802.11j */
	entry->band = channel <= 14 ? IEEE80211_BAND_2GHZ : IEEE80211_BAND_5GHZ;
	entry->center_freq = ieee80211_channel_to_frequency(channel,
							    entry->band);
	entry->hw_value = value;
	entry->max_power = tx_power;
	entry->max_antenna_gain = 0xff;
}

static void rt2x00lib_rate(struct ieee80211_rate *entry,
			   const u16 index, const struct rt2x00_rate *rate)
{
	entry->flags = 0;
	entry->bitrate = rate->bitrate;
	entry->hw_value = index;
	entry->hw_value_short = index;

	if (rate->flags & DEV_RATE_SHORT_PREAMBLE)
		entry->flags |= IEEE80211_RATE_SHORT_PREAMBLE;
}

static int rt2x00lib_probe_hw_modes(struct rt2x00_dev *rt2x00dev,
				    struct hw_mode_spec *spec)
{
	struct ieee80211_hw *hw = rt2x00dev->hw;
	struct ieee80211_channel *channels;
	struct ieee80211_rate *rates;
	unsigned int num_rates;
	unsigned int i;

	num_rates = 0;
	if (spec->supported_rates & SUPPORT_RATE_CCK)
		num_rates += 4;
	if (spec->supported_rates & SUPPORT_RATE_OFDM)
		num_rates += 8;

	channels = kzalloc(sizeof(*channels) * spec->num_channels, GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	rates = kzalloc(sizeof(*rates) * num_rates, GFP_KERNEL);
	if (!rates)
		goto exit_free_channels;

	/*
	 * Initialize Rate list.
	 */
	for (i = 0; i < num_rates; i++)
		rt2x00lib_rate(&rates[i], i, rt2x00_get_rate(i));

	/*
	 * Initialize Channel list.
	 */
	for (i = 0; i < spec->num_channels; i++) {
		rt2x00lib_channel(&channels[i],
				  spec->channels[i].channel,
				  spec->channels_info[i].max_power, i);
	}

	/*
	 * Intitialize 802.11b, 802.11g
	 * Rates: CCK, OFDM.
	 * Channels: 2.4 GHz
	 */
	if (spec->supported_bands & SUPPORT_BAND_2GHZ) {
		rt2x00dev->bands[IEEE80211_BAND_2GHZ].n_channels = 14;
		rt2x00dev->bands[IEEE80211_BAND_2GHZ].n_bitrates = num_rates;
		rt2x00dev->bands[IEEE80211_BAND_2GHZ].channels = channels;
		rt2x00dev->bands[IEEE80211_BAND_2GHZ].bitrates = rates;
		hw->wiphy->bands[IEEE80211_BAND_2GHZ] =
		    &rt2x00dev->bands[IEEE80211_BAND_2GHZ];
		memcpy(&rt2x00dev->bands[IEEE80211_BAND_2GHZ].ht_cap,
		       &spec->ht, sizeof(spec->ht));
	}

	/*
	 * Intitialize 802.11a
	 * Rates: OFDM.
	 * Channels: OFDM, UNII, HiperLAN2.
	 */
	if (spec->supported_bands & SUPPORT_BAND_5GHZ) {
		rt2x00dev->bands[IEEE80211_BAND_5GHZ].n_channels =
		    spec->num_channels - 14;
		rt2x00dev->bands[IEEE80211_BAND_5GHZ].n_bitrates =
		    num_rates - 4;
		rt2x00dev->bands[IEEE80211_BAND_5GHZ].channels = &channels[14];
		rt2x00dev->bands[IEEE80211_BAND_5GHZ].bitrates = &rates[4];
		hw->wiphy->bands[IEEE80211_BAND_5GHZ] =
		    &rt2x00dev->bands[IEEE80211_BAND_5GHZ];
		memcpy(&rt2x00dev->bands[IEEE80211_BAND_5GHZ].ht_cap,
		       &spec->ht, sizeof(spec->ht));
	}

	return 0;

 exit_free_channels:
	kfree(channels);
	ERROR(rt2x00dev, "Allocation ieee80211 modes failed.\n");
	return -ENOMEM;
}

static void rt2x00lib_remove_hw(struct rt2x00_dev *rt2x00dev)
{
	if (test_bit(DEVICE_STATE_REGISTERED_HW, &rt2x00dev->flags))
		ieee80211_unregister_hw(rt2x00dev->hw);

	if (likely(rt2x00dev->hw->wiphy->bands[IEEE80211_BAND_2GHZ])) {
		kfree(rt2x00dev->hw->wiphy->bands[IEEE80211_BAND_2GHZ]->channels);
		kfree(rt2x00dev->hw->wiphy->bands[IEEE80211_BAND_2GHZ]->bitrates);
		rt2x00dev->hw->wiphy->bands[IEEE80211_BAND_2GHZ] = NULL;
		rt2x00dev->hw->wiphy->bands[IEEE80211_BAND_5GHZ] = NULL;
	}

	kfree(rt2x00dev->spec.channels_info);
}

static int rt2x00lib_probe_hw(struct rt2x00_dev *rt2x00dev)
{
	struct hw_mode_spec *spec = &rt2x00dev->spec;
	int status;

	if (test_bit(DEVICE_STATE_REGISTERED_HW, &rt2x00dev->flags))
		return 0;

	/*
	 * Initialize HW modes.
	 */
	status = rt2x00lib_probe_hw_modes(rt2x00dev, spec);
	if (status)
		return status;

	/*
	 * Initialize HW fields.
	 */
	rt2x00dev->hw->queues = rt2x00dev->ops->tx_queues;

	/*
	 * Initialize extra TX headroom required.
	 */
	rt2x00dev->hw->extra_tx_headroom =
		max_t(unsigned int, IEEE80211_TX_STATUS_HEADROOM,
		      rt2x00dev->ops->extra_tx_headroom);

	/*
	 * Take TX headroom required for alignment into account.
	 */
	if (test_bit(REQUIRE_L2PAD, &rt2x00dev->cap_flags))
		rt2x00dev->hw->extra_tx_headroom += RT2X00_L2PAD_SIZE;
	else if (test_bit(REQUIRE_DMA, &rt2x00dev->cap_flags))
		rt2x00dev->hw->extra_tx_headroom += RT2X00_ALIGN_SIZE;

	/*
	 * Allocate tx status FIFO for driver use.
	 */
	if (test_bit(REQUIRE_TXSTATUS_FIFO, &rt2x00dev->cap_flags)) {
		/*
		 * Allocate the txstatus fifo. In the worst case the tx
		 * status fifo has to hold the tx status of all entries
		 * in all tx queues. Hence, calculate the kfifo size as
		 * tx_queues * entry_num and round up to the nearest
		 * power of 2.
		 */
		int kfifo_size =
			roundup_pow_of_two(rt2x00dev->ops->tx_queues *
					   rt2x00dev->ops->tx->entry_num *
					   sizeof(u32));

		status = kfifo_alloc(&rt2x00dev->txstatus_fifo, kfifo_size,
				     GFP_KERNEL);
		if (status)
			return status;
	}

	/*
	 * Initialize tasklets if used by the driver. Tasklets are
	 * disabled until the interrupts are turned on. The driver
	 * has to handle that.
	 */
#define RT2X00_TASKLET_INIT(taskletname) \
	if (rt2x00dev->ops->lib->taskletname) { \
		tasklet_init(&rt2x00dev->taskletname, \
			     rt2x00dev->ops->lib->taskletname, \
			     (unsigned long)rt2x00dev); \
		tasklet_disable(&rt2x00dev->taskletname); \
	}

	RT2X00_TASKLET_INIT(txstatus_tasklet);
	RT2X00_TASKLET_INIT(pretbtt_tasklet);
	RT2X00_TASKLET_INIT(tbtt_tasklet);
	RT2X00_TASKLET_INIT(rxdone_tasklet);
	RT2X00_TASKLET_INIT(autowake_tasklet);

#undef RT2X00_TASKLET_INIT

	/*
	 * Register HW.
	 */
	status = ieee80211_register_hw(rt2x00dev->hw);
	if (status)
		return status;

	set_bit(DEVICE_STATE_REGISTERED_HW, &rt2x00dev->flags);

	return 0;
}

/*
 * Initialization/uninitialization handlers.
 */
static void rt2x00lib_uninitialize(struct rt2x00_dev *rt2x00dev)
{
	if (!test_and_clear_bit(DEVICE_STATE_INITIALIZED, &rt2x00dev->flags))
		return;

	/*
	 * Unregister extra components.
	 */
	rt2x00rfkill_unregister(rt2x00dev);

	/*
	 * Allow the HW to uninitialize.
	 */
	rt2x00dev->ops->lib->uninitialize(rt2x00dev);

	/*
	 * Free allocated queue entries.
	 */
	rt2x00queue_uninitialize(rt2x00dev);
}

static int rt2x00lib_initialize(struct rt2x00_dev *rt2x00dev)
{
	int status;

	if (test_bit(DEVICE_STATE_INITIALIZED, &rt2x00dev->flags))
		return 0;

	/*
	 * Allocate all queue entries.
	 */
	status = rt2x00queue_initialize(rt2x00dev);
	if (status)
		return status;

	/*
	 * Initialize the device.
	 */
	status = rt2x00dev->ops->lib->initialize(rt2x00dev);
	if (status) {
		rt2x00queue_uninitialize(rt2x00dev);
		return status;
	}

	set_bit(DEVICE_STATE_INITIALIZED, &rt2x00dev->flags);

	/*
	 * Register the extra components.
	 */
	rt2x00rfkill_register(rt2x00dev);

	return 0;
}

int rt2x00lib_start(struct rt2x00_dev *rt2x00dev)
{
	int retval;

	if (test_bit(DEVICE_STATE_STARTED, &rt2x00dev->flags))
		return 0;

	/*
	 * If this is the first interface which is added,
	 * we should load the firmware now.
	 */
	retval = rt2x00lib_load_firmware(rt2x00dev);
	if (retval)
		return retval;

	/*
	 * Initialize the device.
	 */
	retval = rt2x00lib_initialize(rt2x00dev);
	if (retval)
		return retval;

	rt2x00dev->intf_ap_count = 0;
	rt2x00dev->intf_sta_count = 0;
	rt2x00dev->intf_associated = 0;

	/* Enable the radio */
	retval = rt2x00lib_enable_radio(rt2x00dev);
	if (retval)
		return retval;

	set_bit(DEVICE_STATE_STARTED, &rt2x00dev->flags);

	return 0;
}

void rt2x00lib_stop(struct rt2x00_dev *rt2x00dev)
{
	if (!test_and_clear_bit(DEVICE_STATE_STARTED, &rt2x00dev->flags))
		return;

	/*
	 * Perhaps we can add something smarter here,
	 * but for now just disabling the radio should do.
	 */
	rt2x00lib_disable_radio(rt2x00dev);

	rt2x00dev->intf_ap_count = 0;
	rt2x00dev->intf_sta_count = 0;
	rt2x00dev->intf_associated = 0;
}

/*
 * driver allocation handlers.
 */
int rt2x00lib_probe_dev(struct rt2x00_dev *rt2x00dev)
{
	int retval = -ENOMEM;

	spin_lock_init(&rt2x00dev->irqmask_lock);
	mutex_init(&rt2x00dev->csr_mutex);

	set_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags);

	/*
	 * Make room for rt2x00_intf inside the per-interface
	 * structure ieee80211_vif.
	 */
	rt2x00dev->hw->vif_data_size = sizeof(struct rt2x00_intf);

	/*
	 * Determine which operating modes are supported, all modes
	 * which require beaconing, depend on the availability of
	 * beacon entries.
	 */
	rt2x00dev->hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
	if (rt2x00dev->ops->bcn->entry_num > 0)
		rt2x00dev->hw->wiphy->interface_modes |=
		    BIT(NL80211_IFTYPE_ADHOC) |
		    BIT(NL80211_IFTYPE_AP) |
		    BIT(NL80211_IFTYPE_MESH_POINT) |
		    BIT(NL80211_IFTYPE_WDS);

	/*
	 * Initialize work.
	 */
	rt2x00dev->workqueue =
	    alloc_ordered_workqueue(wiphy_name(rt2x00dev->hw->wiphy), 0);
	if (!rt2x00dev->workqueue) {
		retval = -ENOMEM;
		goto exit;
	}

	INIT_WORK(&rt2x00dev->intf_work, rt2x00lib_intf_scheduled);
	INIT_DELAYED_WORK(&rt2x00dev->autowakeup_work, rt2x00lib_autowakeup);

	/*
	 * Let the driver probe the device to detect the capabilities.
	 */
	retval = rt2x00dev->ops->lib->probe_hw(rt2x00dev);
	if (retval) {
		ERROR(rt2x00dev, "Failed to allocate device.\n");
		goto exit;
	}

	/*
	 * Allocate queue array.
	 */
	retval = rt2x00queue_allocate(rt2x00dev);
	if (retval)
		goto exit;

	/*
	 * Initialize ieee80211 structure.
	 */
	retval = rt2x00lib_probe_hw(rt2x00dev);
	if (retval) {
		ERROR(rt2x00dev, "Failed to initialize hw.\n");
		goto exit;
	}

	/*
	 * Register extra components.
	 */
	rt2x00link_register(rt2x00dev);
	rt2x00leds_register(rt2x00dev);
	rt2x00debug_register(rt2x00dev);

	return 0;

exit:
	rt2x00lib_remove_dev(rt2x00dev);

	return retval;
}
EXPORT_SYMBOL_GPL(rt2x00lib_probe_dev);

void rt2x00lib_remove_dev(struct rt2x00_dev *rt2x00dev)
{
	clear_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags);

	/*
	 * Disable radio.
	 */
	rt2x00lib_disable_radio(rt2x00dev);

	/*
	 * Stop all work.
	 */
	cancel_work_sync(&rt2x00dev->intf_work);
	if (rt2x00_is_usb(rt2x00dev)) {
		del_timer_sync(&rt2x00dev->txstatus_timer);
		cancel_work_sync(&rt2x00dev->rxdone_work);
		cancel_work_sync(&rt2x00dev->txdone_work);
	}
	destroy_workqueue(rt2x00dev->workqueue);

	/*
	 * Free the tx status fifo.
	 */
	kfifo_free(&rt2x00dev->txstatus_fifo);

	/*
	 * Kill the tx status tasklet.
	 */
	tasklet_kill(&rt2x00dev->txstatus_tasklet);
	tasklet_kill(&rt2x00dev->pretbtt_tasklet);
	tasklet_kill(&rt2x00dev->tbtt_tasklet);
	tasklet_kill(&rt2x00dev->rxdone_tasklet);
	tasklet_kill(&rt2x00dev->autowake_tasklet);

	/*
	 * Uninitialize device.
	 */
	rt2x00lib_uninitialize(rt2x00dev);

	/*
	 * Free extra components
	 */
	rt2x00debug_deregister(rt2x00dev);
	rt2x00leds_unregister(rt2x00dev);

	/*
	 * Free ieee80211_hw memory.
	 */
	rt2x00lib_remove_hw(rt2x00dev);

	/*
	 * Free firmware image.
	 */
	rt2x00lib_free_firmware(rt2x00dev);

	/*
	 * Free queue structures.
	 */
	rt2x00queue_free(rt2x00dev);
}
EXPORT_SYMBOL_GPL(rt2x00lib_remove_dev);

/*
 * Device state handlers
 */
#ifdef CONFIG_PM
int rt2x00lib_suspend(struct rt2x00_dev *rt2x00dev, pm_message_t state)
{
	NOTICE(rt2x00dev, "Going to sleep.\n");

	/*
	 * Prevent mac80211 from accessing driver while suspended.
	 */
	if (!test_and_clear_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags))
		return 0;

	/*
	 * Cleanup as much as possible.
	 */
	rt2x00lib_uninitialize(rt2x00dev);

	/*
	 * Suspend/disable extra components.
	 */
	rt2x00leds_suspend(rt2x00dev);
	rt2x00debug_deregister(rt2x00dev);

	/*
	 * Set device mode to sleep for power management,
	 * on some hardware this call seems to consistently fail.
	 * From the specifications it is hard to tell why it fails,
	 * and if this is a "bad thing".
	 * Overall it is safe to just ignore the failure and
	 * continue suspending. The only downside is that the
	 * device will not be in optimal power save mode, but with
	 * the radio and the other components already disabled the
	 * device is as good as disabled.
	 */
	if (rt2x00dev->ops->lib->set_device_state(rt2x00dev, STATE_SLEEP))
		WARNING(rt2x00dev, "Device failed to enter sleep state, "
			"continue suspending.\n");

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00lib_suspend);

int rt2x00lib_resume(struct rt2x00_dev *rt2x00dev)
{
	NOTICE(rt2x00dev, "Waking up.\n");

	/*
	 * Restore/enable extra components.
	 */
	rt2x00debug_register(rt2x00dev);
	rt2x00leds_resume(rt2x00dev);

	/*
	 * We are ready again to receive requests from mac80211.
	 */
	set_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags);

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00lib_resume);
#endif /* CONFIG_PM */

/*
 * rt2x00lib module information.
 */
MODULE_AUTHOR(DRV_PROJECT);
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("rt2x00 library");
MODULE_LICENSE("GPL");
