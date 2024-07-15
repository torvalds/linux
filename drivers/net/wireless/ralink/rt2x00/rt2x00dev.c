// SPDX-License-Identifier: GPL-2.0-or-later
/*
	Copyright (C) 2010 Willow Garage <http://www.willowgarage.com>
	Copyright (C) 2004 - 2010 Ivo van Doorn <IvDoorn@gmail.com>
	<http://rt2x00.serialmonkey.com>

 */

/*
	Module: rt2x00lib
	Abstract: rt2x00 generic device routines.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/log2.h>
#include <linux/of.h>
#include <linux/of_net.h>

#include "rt2x00.h"
#include "rt2x00lib.h"

/*
 * Utility functions.
 */
u32 rt2x00lib_get_bssidx(struct rt2x00_dev *rt2x00dev,
			 struct ieee80211_vif *vif)
{
	/*
	 * When in STA mode, bssidx is always 0 otherwise local_address[5]
	 * contains the bss number, see BSS_ID_MASK comments for details.
	 */
	if (rt2x00dev->intf_sta_count)
		return 0;
	return vif->addr[5] & (rt2x00dev->ops->max_ap_intf - 1);
}
EXPORT_SYMBOL_GPL(rt2x00lib_get_bssidx);

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
	rt2x00link_stop_tuner(rt2x00dev);
	rt2x00queue_stop_queues(rt2x00dev);
	rt2x00queue_flush_queues(rt2x00dev, true);
	rt2x00queue_stop_queue(rt2x00dev->bcn);

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

	if (test_and_clear_bit(DELAYED_UPDATE_BEACON, &intf->delayed_flags)) {
		mutex_lock(&intf->beacon_skb_mutex);
		rt2x00queue_update_beacon(rt2x00dev, vif);
		mutex_unlock(&intf->beacon_skb_mutex);
	}
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
					    IEEE80211_IFACE_ITER_RESUME_ALL,
					    rt2x00lib_intf_scheduled_iter,
					    rt2x00dev);
}

static void rt2x00lib_autowakeup(struct work_struct *work)
{
	struct rt2x00_dev *rt2x00dev =
	    container_of(work, struct rt2x00_dev, autowakeup_work.work);

	if (!test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags))
		return;

	if (rt2x00dev->ops->lib->set_device_state(rt2x00dev, STATE_AWAKE))
		rt2x00_err(rt2x00dev, "Device failed to wakeup\n");
	clear_bit(CONFIG_POWERSAVING, &rt2x00dev->flags);
}

/*
 * Interrupt context handlers.
 */
static void rt2x00lib_bc_buffer_iter(void *data, u8 *mac,
				     struct ieee80211_vif *vif)
{
	struct ieee80211_tx_control control = {};
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
		rt2x00mac_tx(rt2x00dev->hw, &control, skb);
		skb = ieee80211_get_buffered_bc(rt2x00dev->hw, vif);
	}
}

static void rt2x00lib_beaconupdate_iter(void *data, u8 *mac,
					struct ieee80211_vif *vif)
{
	struct rt2x00_dev *rt2x00dev = data;

	if (vif->type != NL80211_IFTYPE_AP &&
	    vif->type != NL80211_IFTYPE_ADHOC &&
	    vif->type != NL80211_IFTYPE_MESH_POINT)
		return;

	/*
	 * Update the beacon without locking. This is safe on PCI devices
	 * as they only update the beacon periodically here. This should
	 * never be called for USB devices.
	 */
	WARN_ON(rt2x00_is_usb(rt2x00dev));
	rt2x00queue_update_beacon(rt2x00dev, vif);
}

void rt2x00lib_beacondone(struct rt2x00_dev *rt2x00dev)
{
	if (!test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		return;

	/* send buffered bc/mc frames out for every bssid */
	ieee80211_iterate_active_interfaces_atomic(
		rt2x00dev->hw, IEEE80211_IFACE_ITER_RESUME_ALL,
		rt2x00lib_bc_buffer_iter, rt2x00dev);
	/*
	 * Devices with pre tbtt interrupt don't need to update the beacon
	 * here as they will fetch the next beacon directly prior to
	 * transmission.
	 */
	if (rt2x00_has_cap_pre_tbtt_interrupt(rt2x00dev))
		return;

	/* fetch next beacon */
	ieee80211_iterate_active_interfaces_atomic(
		rt2x00dev->hw, IEEE80211_IFACE_ITER_RESUME_ALL,
		rt2x00lib_beaconupdate_iter, rt2x00dev);
}
EXPORT_SYMBOL_GPL(rt2x00lib_beacondone);

void rt2x00lib_pretbtt(struct rt2x00_dev *rt2x00dev)
{
	if (!test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		return;

	/* fetch next beacon */
	ieee80211_iterate_active_interfaces_atomic(
		rt2x00dev->hw, IEEE80211_IFACE_ITER_RESUME_ALL,
		rt2x00lib_beaconupdate_iter, rt2x00dev);
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

static inline int rt2x00lib_txdone_bar_status(struct queue_entry *entry)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct ieee80211_bar *bar = (void *) entry->skb->data;
	struct rt2x00_bar_list_entry *bar_entry;
	int ret;

	if (likely(!ieee80211_is_back_req(bar->frame_control)))
		return 0;

	/*
	 * Unlike all other frames, the status report for BARs does
	 * not directly come from the hardware as it is incapable of
	 * matching a BA to a previously send BAR. The hardware will
	 * report all BARs as if they weren't acked at all.
	 *
	 * Instead the RX-path will scan for incoming BAs and set the
	 * block_acked flag if it sees one that was likely caused by
	 * a BAR from us.
	 *
	 * Remove remaining BARs here and return their status for
	 * TX done processing.
	 */
	ret = 0;
	rcu_read_lock();
	list_for_each_entry_rcu(bar_entry, &rt2x00dev->bar_list, list) {
		if (bar_entry->entry != entry)
			continue;

		spin_lock_bh(&rt2x00dev->bar_list_lock);
		/* Return whether this BAR was blockacked or not */
		ret = bar_entry->block_acked;
		/* Remove the BAR from our checklist */
		list_del_rcu(&bar_entry->list);
		spin_unlock_bh(&rt2x00dev->bar_list_lock);
		kfree_rcu(bar_entry, head);

		break;
	}
	rcu_read_unlock();

	return ret;
}

static void rt2x00lib_fill_tx_status(struct rt2x00_dev *rt2x00dev,
				     struct ieee80211_tx_info *tx_info,
				     struct skb_frame_desc *skbdesc,
				     struct txdone_entry_desc *txdesc,
				     bool success)
{
	u8 rate_idx, rate_flags, retry_rates;
	int i;

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

	if (test_bit(TXDONE_NO_ACK_REQ, &txdesc->flags))
		tx_info->flags |= IEEE80211_TX_CTL_NO_ACK;

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
		tx_info->flags |= IEEE80211_TX_STAT_AMPDU |
				  IEEE80211_TX_CTL_AMPDU;
		tx_info->status.ampdu_len = 1;
		tx_info->status.ampdu_ack_len = success ? 1 : 0;
	}

	if (rate_flags & IEEE80211_TX_RC_USE_RTS_CTS) {
		if (success)
			rt2x00dev->low_level_stats.dot11RTSSuccessCount++;
		else
			rt2x00dev->low_level_stats.dot11RTSFailureCount++;
	}
}

static void rt2x00lib_clear_entry(struct rt2x00_dev *rt2x00dev,
				  struct queue_entry *entry)
{
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
	 * is reenabled when the txdone handler has finished. This has to be
	 * serialized with rt2x00mac_tx(), otherwise we can wake up queue
	 * before it was stopped.
	 */
	spin_lock_bh(&entry->queue->tx_lock);
	if (!rt2x00queue_threshold(entry->queue))
		rt2x00queue_unpause_queue(entry->queue);
	spin_unlock_bh(&entry->queue->tx_lock);
}

void rt2x00lib_txdone_nomatch(struct queue_entry *entry,
			      struct txdone_entry_desc *txdesc)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(entry->skb);
	struct ieee80211_tx_info txinfo = {};
	bool success;

	/*
	 * Unmap the skb.
	 */
	rt2x00queue_unmap_skb(entry);

	/*
	 * Signal that the TX descriptor is no longer in the skb.
	 */
	skbdesc->flags &= ~SKBDESC_DESC_IN_SKB;

	/*
	 * Send frame to debugfs immediately, after this call is completed
	 * we are going to overwrite the skb->cb array.
	 */
	rt2x00debug_dump_frame(rt2x00dev, DUMP_FRAME_TXDONE, entry);

	/*
	 * Determine if the frame has been successfully transmitted and
	 * remove BARs from our check list while checking for their
	 * TX status.
	 */
	success =
	    rt2x00lib_txdone_bar_status(entry) ||
	    test_bit(TXDONE_SUCCESS, &txdesc->flags);

	if (!test_bit(TXDONE_UNKNOWN, &txdesc->flags)) {
		/*
		 * Update TX statistics.
		 */
		rt2x00dev->link.qual.tx_success += success;
		rt2x00dev->link.qual.tx_failed += !success;

		rt2x00lib_fill_tx_status(rt2x00dev, &txinfo, skbdesc, txdesc,
					 success);
		ieee80211_tx_status_noskb(rt2x00dev->hw, skbdesc->sta, &txinfo);
	}

	dev_kfree_skb_any(entry->skb);
	rt2x00lib_clear_entry(rt2x00dev, entry);
}
EXPORT_SYMBOL_GPL(rt2x00lib_txdone_nomatch);

void rt2x00lib_txdone(struct queue_entry *entry,
		      struct txdone_entry_desc *txdesc)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(entry->skb);
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(entry->skb);
	u8 skbdesc_flags = skbdesc->flags;
	unsigned int header_length;
	bool success;

	/*
	 * Unmap the skb.
	 */
	rt2x00queue_unmap_skb(entry);

	/*
	 * Remove the extra tx headroom from the skb.
	 */
	skb_pull(entry->skb, rt2x00dev->extra_tx_headroom);

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
	if (rt2x00_has_cap_flag(rt2x00dev, REQUIRE_L2PAD))
		rt2x00queue_remove_l2pad(entry->skb, header_length);

	/*
	 * If the IV/EIV data was stripped from the frame before it was
	 * passed to the hardware, we should now reinsert it again because
	 * mac80211 will expect the same data to be present it the
	 * frame as it was passed to us.
	 */
	if (rt2x00_has_cap_hw_crypto(rt2x00dev))
		rt2x00crypto_tx_insert_iv(entry->skb, header_length);

	/*
	 * Send frame to debugfs immediately, after this call is completed
	 * we are going to overwrite the skb->cb array.
	 */
	rt2x00debug_dump_frame(rt2x00dev, DUMP_FRAME_TXDONE, entry);

	/*
	 * Determine if the frame has been successfully transmitted and
	 * remove BARs from our check list while checking for their
	 * TX status.
	 */
	success =
	    rt2x00lib_txdone_bar_status(entry) ||
	    test_bit(TXDONE_SUCCESS, &txdesc->flags) ||
	    test_bit(TXDONE_UNKNOWN, &txdesc->flags);

	/*
	 * Update TX statistics.
	 */
	rt2x00dev->link.qual.tx_success += success;
	rt2x00dev->link.qual.tx_failed += !success;

	rt2x00lib_fill_tx_status(rt2x00dev, tx_info, skbdesc, txdesc, success);

	/*
	 * Only send the status report to mac80211 when it's a frame
	 * that originated in mac80211. If this was a extra frame coming
	 * through a mac80211 library call (RTS/CTS) then we should not
	 * send the status report back.
	 */
	if (!(skbdesc_flags & SKBDESC_NOT_MAC80211)) {
		if (rt2x00_has_cap_flag(rt2x00dev, REQUIRE_TASKLET_CONTEXT))
			ieee80211_tx_status_skb(rt2x00dev->hw, entry->skb);
		else
			ieee80211_tx_status_ni(rt2x00dev->hw, entry->skb);
	} else {
		dev_kfree_skb_any(entry->skb);
	}

	rt2x00lib_clear_entry(rt2x00dev, entry);
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

static void rt2x00lib_sleep(struct work_struct *work)
{
	struct rt2x00_dev *rt2x00dev =
	    container_of(work, struct rt2x00_dev, sleep_work);

	if (!test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags))
		return;

	/*
	 * Check again is powersaving is enabled, to prevent races from delayed
	 * work execution.
	 */
	if (!test_bit(CONFIG_POWERSAVING, &rt2x00dev->flags))
		rt2x00lib_config(rt2x00dev, &rt2x00dev->hw->conf,
				 IEEE80211_CONF_CHANGE_PS);
}

static void rt2x00lib_rxdone_check_ba(struct rt2x00_dev *rt2x00dev,
				      struct sk_buff *skb,
				      struct rxdone_entry_desc *rxdesc)
{
	struct rt2x00_bar_list_entry *entry;
	struct ieee80211_bar *ba = (void *)skb->data;

	if (likely(!ieee80211_is_back(ba->frame_control)))
		return;

	if (rxdesc->size < sizeof(*ba) + FCS_LEN)
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(entry, &rt2x00dev->bar_list, list) {

		if (ba->start_seq_num != entry->start_seq_num)
			continue;

#define TID_CHECK(a, b) (						\
	((a) & cpu_to_le16(IEEE80211_BAR_CTRL_TID_INFO_MASK)) ==	\
	((b) & cpu_to_le16(IEEE80211_BAR_CTRL_TID_INFO_MASK)))		\

		if (!TID_CHECK(ba->control, entry->control))
			continue;

#undef TID_CHECK

		if (!ether_addr_equal_64bits(ba->ra, entry->ta))
			continue;

		if (!ether_addr_equal_64bits(ba->ta, entry->ra))
			continue;

		/* Mark BAR since we received the according BA */
		spin_lock_bh(&rt2x00dev->bar_list_lock);
		entry->block_acked = 1;
		spin_unlock_bh(&rt2x00dev->bar_list_lock);
		break;
	}
	rcu_read_unlock();

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
		queue_work(rt2x00dev->workqueue, &rt2x00dev->sleep_work);
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

	rt2x00_warn(rt2x00dev, "Frame received with unrecognized signal, mode=0x%.4x, signal=0x%.4x, type=%d\n",
		    rxdesc->rate_mode, signal, type);
	return 0;
}

void rt2x00lib_rxdone(struct queue_entry *entry, gfp_t gfp)
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
	skb = rt2x00queue_alloc_rxskb(entry, gfp);
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
	 * Check for valid size in case we get corrupted descriptor from
	 * hardware.
	 */
	if (unlikely(rxdesc.size == 0 ||
		     rxdesc.size > entry->queue->data_size)) {
		rt2x00_err(rt2x00dev, "Wrong frame size %d max %d\n",
			   rxdesc.size, entry->queue->data_size);
		dev_kfree_skb(entry->skb);
		goto renew_skb;
	}

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
		rxdesc.encoding = RX_ENC_HT;

	/*
	 * Check if this is a beacon, and more frames have been
	 * buffered while we were in powersaving mode.
	 */
	rt2x00lib_rxdone_check_ps(rt2x00dev, entry->skb, &rxdesc);

	/*
	 * Check for incoming BlockAcks to match to the BlockAckReqs
	 * we've send out.
	 */
	rt2x00lib_rxdone_check_ba(rt2x00dev, entry->skb, &rxdesc);

	/*
	 * Update extra components
	 */
	rt2x00link_update_stats(rt2x00dev, entry->skb, &rxdesc);
	rt2x00debug_update_crypto(rt2x00dev, &rxdesc);
	rt2x00debug_dump_frame(rt2x00dev, DUMP_FRAME_RXDONE, entry);

	/*
	 * Initialize RX status information, and send frame
	 * to mac80211.
	 */
	rx_status = IEEE80211_SKB_RXCB(entry->skb);

	/* Ensure that all fields of rx_status are initialized
	 * properly. The skb->cb array was used for driver
	 * specific informations, so rx_status might contain
	 * garbage.
	 */
	memset(rx_status, 0, sizeof(*rx_status));

	rx_status->mactime = rxdesc.timestamp;
	rx_status->band = rt2x00dev->curr_band;
	rx_status->freq = rt2x00dev->curr_freq;
	rx_status->rate_idx = rate_idx;
	rx_status->signal = rxdesc.rssi;
	rx_status->flag = rxdesc.flags;
	rx_status->enc_flags = rxdesc.enc_flags;
	rx_status->encoding = rxdesc.encoding;
	rx_status->bw = rxdesc.bw;
	rx_status->antenna = rt2x00dev->link.ant.active.rx;

	ieee80211_rx_ni(rt2x00dev->hw, entry->skb);

renew_skb:
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
	entry->band = channel <= 14 ? NL80211_BAND_2GHZ : NL80211_BAND_5GHZ;
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

void rt2x00lib_set_mac_address(struct rt2x00_dev *rt2x00dev, u8 *eeprom_mac_addr)
{
	of_get_mac_address(rt2x00dev->dev->of_node, eeprom_mac_addr);

	if (!is_valid_ether_addr(eeprom_mac_addr)) {
		eth_random_addr(eeprom_mac_addr);
		rt2x00_eeprom_dbg(rt2x00dev, "MAC: %pM\n", eeprom_mac_addr);
	}
}
EXPORT_SYMBOL_GPL(rt2x00lib_set_mac_address);

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

	channels = kcalloc(spec->num_channels, sizeof(*channels), GFP_KERNEL);
	if (!channels)
		return -ENOMEM;

	rates = kcalloc(num_rates, sizeof(*rates), GFP_KERNEL);
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
		rt2x00dev->bands[NL80211_BAND_2GHZ].n_channels = 14;
		rt2x00dev->bands[NL80211_BAND_2GHZ].n_bitrates = num_rates;
		rt2x00dev->bands[NL80211_BAND_2GHZ].channels = channels;
		rt2x00dev->bands[NL80211_BAND_2GHZ].bitrates = rates;
		hw->wiphy->bands[NL80211_BAND_2GHZ] =
		    &rt2x00dev->bands[NL80211_BAND_2GHZ];
		memcpy(&rt2x00dev->bands[NL80211_BAND_2GHZ].ht_cap,
		       &spec->ht, sizeof(spec->ht));
	}

	/*
	 * Intitialize 802.11a
	 * Rates: OFDM.
	 * Channels: OFDM, UNII, HiperLAN2.
	 */
	if (spec->supported_bands & SUPPORT_BAND_5GHZ) {
		rt2x00dev->bands[NL80211_BAND_5GHZ].n_channels =
		    spec->num_channels - 14;
		rt2x00dev->bands[NL80211_BAND_5GHZ].n_bitrates =
		    num_rates - 4;
		rt2x00dev->bands[NL80211_BAND_5GHZ].channels = &channels[14];
		rt2x00dev->bands[NL80211_BAND_5GHZ].bitrates = &rates[4];
		hw->wiphy->bands[NL80211_BAND_5GHZ] =
		    &rt2x00dev->bands[NL80211_BAND_5GHZ];
		memcpy(&rt2x00dev->bands[NL80211_BAND_5GHZ].ht_cap,
		       &spec->ht, sizeof(spec->ht));
	}

	return 0;

 exit_free_channels:
	kfree(channels);
	rt2x00_err(rt2x00dev, "Allocation ieee80211 modes failed\n");
	return -ENOMEM;
}

static void rt2x00lib_remove_hw(struct rt2x00_dev *rt2x00dev)
{
	if (test_bit(DEVICE_STATE_REGISTERED_HW, &rt2x00dev->flags))
		ieee80211_unregister_hw(rt2x00dev->hw);

	if (likely(rt2x00dev->hw->wiphy->bands[NL80211_BAND_2GHZ])) {
		kfree(rt2x00dev->hw->wiphy->bands[NL80211_BAND_2GHZ]->channels);
		kfree(rt2x00dev->hw->wiphy->bands[NL80211_BAND_2GHZ]->bitrates);
		rt2x00dev->hw->wiphy->bands[NL80211_BAND_2GHZ] = NULL;
		rt2x00dev->hw->wiphy->bands[NL80211_BAND_5GHZ] = NULL;
	}

	kfree(rt2x00dev->spec.channels_info);
	kfree(rt2x00dev->chan_survey);
}

static const struct ieee80211_tpt_blink rt2x00_tpt_blink[] = {
	{ .throughput = 0 * 1024, .blink_time = 334 },
	{ .throughput = 1 * 1024, .blink_time = 260 },
	{ .throughput = 2 * 1024, .blink_time = 220 },
	{ .throughput = 5 * 1024, .blink_time = 190 },
	{ .throughput = 10 * 1024, .blink_time = 170 },
	{ .throughput = 25 * 1024, .blink_time = 150 },
	{ .throughput = 54 * 1024, .blink_time = 130 },
	{ .throughput = 120 * 1024, .blink_time = 110 },
	{ .throughput = 265 * 1024, .blink_time = 80 },
	{ .throughput = 586 * 1024, .blink_time = 50 },
};

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
		      rt2x00dev->extra_tx_headroom);

	/*
	 * Take TX headroom required for alignment into account.
	 */
	if (rt2x00_has_cap_flag(rt2x00dev, REQUIRE_L2PAD))
		rt2x00dev->hw->extra_tx_headroom += RT2X00_L2PAD_SIZE;
	else if (rt2x00_has_cap_flag(rt2x00dev, REQUIRE_DMA))
		rt2x00dev->hw->extra_tx_headroom += RT2X00_ALIGN_SIZE;

	/*
	 * Tell mac80211 about the size of our private STA structure.
	 */
	rt2x00dev->hw->sta_data_size = sizeof(struct rt2x00_sta);

	/*
	 * Allocate tx status FIFO for driver use.
	 */
	if (rt2x00_has_cap_flag(rt2x00dev, REQUIRE_TXSTATUS_FIFO)) {
		/*
		 * Allocate the txstatus fifo. In the worst case the tx
		 * status fifo has to hold the tx status of all entries
		 * in all tx queues. Hence, calculate the kfifo size as
		 * tx_queues * entry_num and round up to the nearest
		 * power of 2.
		 */
		int kfifo_size =
			roundup_pow_of_two(rt2x00dev->ops->tx_queues *
					   rt2x00dev->tx->limit *
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
		tasklet_setup(&rt2x00dev->taskletname, \
			     rt2x00dev->ops->lib->taskletname); \
	}

	RT2X00_TASKLET_INIT(txstatus_tasklet);
	RT2X00_TASKLET_INIT(pretbtt_tasklet);
	RT2X00_TASKLET_INIT(tbtt_tasklet);
	RT2X00_TASKLET_INIT(rxdone_tasklet);
	RT2X00_TASKLET_INIT(autowake_tasklet);

#undef RT2X00_TASKLET_INIT

	ieee80211_create_tpt_led_trigger(rt2x00dev->hw,
					 IEEE80211_TPT_LEDTRIG_FL_RADIO,
					 rt2x00_tpt_blink,
					 ARRAY_SIZE(rt2x00_tpt_blink));

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
	 * Stop rfkill polling.
	 */
	if (rt2x00_has_cap_flag(rt2x00dev, REQUIRE_DELAYED_RFKILL))
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
	 * Start rfkill polling.
	 */
	if (rt2x00_has_cap_flag(rt2x00dev, REQUIRE_DELAYED_RFKILL))
		rt2x00rfkill_register(rt2x00dev);

	return 0;
}

int rt2x00lib_start(struct rt2x00_dev *rt2x00dev)
{
	int retval = 0;

	/*
	 * If this is the first interface which is added,
	 * we should load the firmware now.
	 */
	retval = rt2x00lib_load_firmware(rt2x00dev);
	if (retval)
		goto out;

	/*
	 * Initialize the device.
	 */
	retval = rt2x00lib_initialize(rt2x00dev);
	if (retval)
		goto out;

	rt2x00dev->intf_ap_count = 0;
	rt2x00dev->intf_sta_count = 0;
	rt2x00dev->intf_associated = 0;
	rt2x00dev->intf_beaconing = 0;

	/* Enable the radio */
	retval = rt2x00lib_enable_radio(rt2x00dev);
	if (retval)
		goto out;

	set_bit(DEVICE_STATE_STARTED, &rt2x00dev->flags);

out:
	return retval;
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
	rt2x00dev->intf_beaconing = 0;
}

static inline void rt2x00lib_set_if_combinations(struct rt2x00_dev *rt2x00dev)
{
	struct ieee80211_iface_limit *if_limit;
	struct ieee80211_iface_combination *if_combination;

	if (rt2x00dev->ops->max_ap_intf < 2)
		return;

	/*
	 * Build up AP interface limits structure.
	 */
	if_limit = &rt2x00dev->if_limits_ap;
	if_limit->max = rt2x00dev->ops->max_ap_intf;
	if_limit->types = BIT(NL80211_IFTYPE_AP);
#ifdef CONFIG_MAC80211_MESH
	if_limit->types |= BIT(NL80211_IFTYPE_MESH_POINT);
#endif

	/*
	 * Build up AP interface combinations structure.
	 */
	if_combination = &rt2x00dev->if_combinations[IF_COMB_AP];
	if_combination->limits = if_limit;
	if_combination->n_limits = 1;
	if_combination->max_interfaces = if_limit->max;
	if_combination->num_different_channels = 1;

	/*
	 * Finally, specify the possible combinations to mac80211.
	 */
	rt2x00dev->hw->wiphy->iface_combinations = rt2x00dev->if_combinations;
	rt2x00dev->hw->wiphy->n_iface_combinations = 1;
}

static unsigned int rt2x00dev_extra_tx_headroom(struct rt2x00_dev *rt2x00dev)
{
	if (WARN_ON(!rt2x00dev->tx))
		return 0;

	if (rt2x00_is_usb(rt2x00dev))
		return rt2x00dev->tx[0].winfo_size + rt2x00dev->tx[0].desc_size;

	return rt2x00dev->tx[0].winfo_size;
}

/*
 * driver allocation handlers.
 */
int rt2x00lib_probe_dev(struct rt2x00_dev *rt2x00dev)
{
	int retval = -ENOMEM;

	/*
	 * Set possible interface combinations.
	 */
	rt2x00lib_set_if_combinations(rt2x00dev);

	/*
	 * Allocate the driver data memory, if necessary.
	 */
	if (rt2x00dev->ops->drv_data_size > 0) {
		rt2x00dev->drv_data = kzalloc(rt2x00dev->ops->drv_data_size,
			                      GFP_KERNEL);
		if (!rt2x00dev->drv_data) {
			retval = -ENOMEM;
			goto exit;
		}
	}

	spin_lock_init(&rt2x00dev->irqmask_lock);
	mutex_init(&rt2x00dev->csr_mutex);
	mutex_init(&rt2x00dev->conf_mutex);
	INIT_LIST_HEAD(&rt2x00dev->bar_list);
	spin_lock_init(&rt2x00dev->bar_list_lock);
	hrtimer_init(&rt2x00dev->txstatus_timer, CLOCK_MONOTONIC,
		     HRTIMER_MODE_REL);

	set_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags);

	/*
	 * Make room for rt2x00_intf inside the per-interface
	 * structure ieee80211_vif.
	 */
	rt2x00dev->hw->vif_data_size = sizeof(struct rt2x00_intf);

	/*
	 * rt2x00 devices can only use the last n bits of the MAC address
	 * for virtual interfaces.
	 */
	rt2x00dev->hw->wiphy->addr_mask[ETH_ALEN - 1] =
		(rt2x00dev->ops->max_ap_intf - 1);

	/*
	 * Initialize work.
	 */
	rt2x00dev->workqueue =
	    alloc_ordered_workqueue("%s", 0, wiphy_name(rt2x00dev->hw->wiphy));
	if (!rt2x00dev->workqueue) {
		retval = -ENOMEM;
		goto exit;
	}

	INIT_WORK(&rt2x00dev->intf_work, rt2x00lib_intf_scheduled);
	INIT_DELAYED_WORK(&rt2x00dev->autowakeup_work, rt2x00lib_autowakeup);
	INIT_WORK(&rt2x00dev->sleep_work, rt2x00lib_sleep);

	/*
	 * Let the driver probe the device to detect the capabilities.
	 */
	retval = rt2x00dev->ops->lib->probe_hw(rt2x00dev);
	if (retval) {
		rt2x00_err(rt2x00dev, "Failed to allocate device\n");
		goto exit;
	}

	/*
	 * Allocate queue array.
	 */
	retval = rt2x00queue_allocate(rt2x00dev);
	if (retval)
		goto exit;

	/* Cache TX headroom value */
	rt2x00dev->extra_tx_headroom = rt2x00dev_extra_tx_headroom(rt2x00dev);

	/*
	 * Determine which operating modes are supported, all modes
	 * which require beaconing, depend on the availability of
	 * beacon entries.
	 */
	rt2x00dev->hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
	if (rt2x00dev->bcn->limit > 0)
		rt2x00dev->hw->wiphy->interface_modes |=
		    BIT(NL80211_IFTYPE_ADHOC) |
#ifdef CONFIG_MAC80211_MESH
		    BIT(NL80211_IFTYPE_MESH_POINT) |
#endif
		    BIT(NL80211_IFTYPE_AP);

	rt2x00dev->hw->wiphy->flags |= WIPHY_FLAG_IBSS_RSN;

	wiphy_ext_feature_set(rt2x00dev->hw->wiphy,
			      NL80211_EXT_FEATURE_CQM_RSSI_LIST);

	/*
	 * Initialize ieee80211 structure.
	 */
	retval = rt2x00lib_probe_hw(rt2x00dev);
	if (retval) {
		rt2x00_err(rt2x00dev, "Failed to initialize hw\n");
		goto exit;
	}

	/*
	 * Register extra components.
	 */
	rt2x00link_register(rt2x00dev);
	rt2x00leds_register(rt2x00dev);
	rt2x00debug_register(rt2x00dev);

	/*
	 * Start rfkill polling.
	 */
	if (!rt2x00_has_cap_flag(rt2x00dev, REQUIRE_DELAYED_RFKILL))
		rt2x00rfkill_register(rt2x00dev);

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
	 * Stop rfkill polling.
	 */
	if (!rt2x00_has_cap_flag(rt2x00dev, REQUIRE_DELAYED_RFKILL))
		rt2x00rfkill_unregister(rt2x00dev);

	/*
	 * Disable radio.
	 */
	rt2x00lib_disable_radio(rt2x00dev);

	/*
	 * Stop all work.
	 */
	cancel_work_sync(&rt2x00dev->intf_work);
	cancel_delayed_work_sync(&rt2x00dev->autowakeup_work);
	cancel_work_sync(&rt2x00dev->sleep_work);

	hrtimer_cancel(&rt2x00dev->txstatus_timer);

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

	if (rt2x00dev->workqueue)
		destroy_workqueue(rt2x00dev->workqueue);

	/*
	 * Free the tx status fifo.
	 */
	kfifo_free(&rt2x00dev->txstatus_fifo);

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

	/*
	 * Free the driver data.
	 */
	kfree(rt2x00dev->drv_data);
}
EXPORT_SYMBOL_GPL(rt2x00lib_remove_dev);

/*
 * Device state handlers
 */
int rt2x00lib_suspend(struct rt2x00_dev *rt2x00dev)
{
	rt2x00_dbg(rt2x00dev, "Going to sleep\n");

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
		rt2x00_warn(rt2x00dev, "Device failed to enter sleep state, continue suspending\n");

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00lib_suspend);

int rt2x00lib_resume(struct rt2x00_dev *rt2x00dev)
{
	rt2x00_dbg(rt2x00dev, "Waking up\n");

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

/*
 * rt2x00lib module information.
 */
MODULE_AUTHOR(DRV_PROJECT);
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("rt2x00 library");
MODULE_LICENSE("GPL");
