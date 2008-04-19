/*
	Copyright (C) 2004 - 2008 rt2x00 SourceForge Project
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

#include "rt2x00.h"
#include "rt2x00lib.h"
#include "rt2x00dump.h"

/*
 * Link tuning handlers
 */
void rt2x00lib_reset_link_tuner(struct rt2x00_dev *rt2x00dev)
{
	if (!test_bit(DEVICE_ENABLED_RADIO, &rt2x00dev->flags))
		return;

	/*
	 * Reset link information.
	 * Both the currently active vgc level as well as
	 * the link tuner counter should be reset. Resetting
	 * the counter is important for devices where the
	 * device should only perform link tuning during the
	 * first minute after being enabled.
	 */
	rt2x00dev->link.count = 0;
	rt2x00dev->link.vgc_level = 0;

	/*
	 * Reset the link tuner.
	 */
	rt2x00dev->ops->lib->reset_tuner(rt2x00dev);
}

static void rt2x00lib_start_link_tuner(struct rt2x00_dev *rt2x00dev)
{
	/*
	 * Clear all (possibly) pre-existing quality statistics.
	 */
	memset(&rt2x00dev->link.qual, 0, sizeof(rt2x00dev->link.qual));

	/*
	 * The RX and TX percentage should start at 50%
	 * this will assure we will get at least get some
	 * decent value when the link tuner starts.
	 * The value will be dropped and overwritten with
	 * the correct (measured )value anyway during the
	 * first run of the link tuner.
	 */
	rt2x00dev->link.qual.rx_percentage = 50;
	rt2x00dev->link.qual.tx_percentage = 50;

	rt2x00lib_reset_link_tuner(rt2x00dev);

	queue_delayed_work(rt2x00dev->hw->workqueue,
			   &rt2x00dev->link.work, LINK_TUNE_INTERVAL);
}

static void rt2x00lib_stop_link_tuner(struct rt2x00_dev *rt2x00dev)
{
	cancel_delayed_work_sync(&rt2x00dev->link.work);
}

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
	if (test_bit(DEVICE_ENABLED_RADIO, &rt2x00dev->flags) ||
	    test_bit(DEVICE_DISABLED_RADIO_HW, &rt2x00dev->flags))
		return 0;

	/*
	 * Initialize all data queues.
	 */
	rt2x00queue_init_rx(rt2x00dev);
	rt2x00queue_init_tx(rt2x00dev);

	/*
	 * Enable radio.
	 */
	status =
	    rt2x00dev->ops->lib->set_device_state(rt2x00dev, STATE_RADIO_ON);
	if (status)
		return status;

	rt2x00leds_led_radio(rt2x00dev, true);

	__set_bit(DEVICE_ENABLED_RADIO, &rt2x00dev->flags);

	/*
	 * Enable RX.
	 */
	rt2x00lib_toggle_rx(rt2x00dev, STATE_RADIO_RX_ON);

	/*
	 * Start the TX queues.
	 */
	ieee80211_start_queues(rt2x00dev->hw);

	return 0;
}

void rt2x00lib_disable_radio(struct rt2x00_dev *rt2x00dev)
{
	if (!__test_and_clear_bit(DEVICE_ENABLED_RADIO, &rt2x00dev->flags))
		return;

	/*
	 * Stop all scheduled work.
	 */
	if (work_pending(&rt2x00dev->intf_work))
		cancel_work_sync(&rt2x00dev->intf_work);
	if (work_pending(&rt2x00dev->filter_work))
		cancel_work_sync(&rt2x00dev->filter_work);

	/*
	 * Stop the TX queues.
	 */
	ieee80211_stop_queues(rt2x00dev->hw);

	/*
	 * Disable RX.
	 */
	rt2x00lib_toggle_rx(rt2x00dev, STATE_RADIO_RX_OFF);

	/*
	 * Disable radio.
	 */
	rt2x00dev->ops->lib->set_device_state(rt2x00dev, STATE_RADIO_OFF);
	rt2x00leds_led_radio(rt2x00dev, false);
}

void rt2x00lib_toggle_rx(struct rt2x00_dev *rt2x00dev, enum dev_state state)
{
	/*
	 * When we are disabling the RX, we should also stop the link tuner.
	 */
	if (state == STATE_RADIO_RX_OFF)
		rt2x00lib_stop_link_tuner(rt2x00dev);

	rt2x00dev->ops->lib->set_device_state(rt2x00dev, state);

	/*
	 * When we are enabling the RX, we should also start the link tuner.
	 */
	if (state == STATE_RADIO_RX_ON &&
	    (rt2x00dev->intf_ap_count || rt2x00dev->intf_sta_count))
		rt2x00lib_start_link_tuner(rt2x00dev);
}

static void rt2x00lib_evaluate_antenna_sample(struct rt2x00_dev *rt2x00dev)
{
	enum antenna rx = rt2x00dev->link.ant.active.rx;
	enum antenna tx = rt2x00dev->link.ant.active.tx;
	int sample_a =
	    rt2x00_get_link_ant_rssi_history(&rt2x00dev->link, ANTENNA_A);
	int sample_b =
	    rt2x00_get_link_ant_rssi_history(&rt2x00dev->link, ANTENNA_B);

	/*
	 * We are done sampling. Now we should evaluate the results.
	 */
	rt2x00dev->link.ant.flags &= ~ANTENNA_MODE_SAMPLE;

	/*
	 * During the last period we have sampled the RSSI
	 * from both antenna's. It now is time to determine
	 * which antenna demonstrated the best performance.
	 * When we are already on the antenna with the best
	 * performance, then there really is nothing for us
	 * left to do.
	 */
	if (sample_a == sample_b)
		return;

	if (rt2x00dev->link.ant.flags & ANTENNA_RX_DIVERSITY)
		rx = (sample_a > sample_b) ? ANTENNA_A : ANTENNA_B;

	if (rt2x00dev->link.ant.flags & ANTENNA_TX_DIVERSITY)
		tx = (sample_a > sample_b) ? ANTENNA_A : ANTENNA_B;

	rt2x00lib_config_antenna(rt2x00dev, rx, tx);
}

static void rt2x00lib_evaluate_antenna_eval(struct rt2x00_dev *rt2x00dev)
{
	enum antenna rx = rt2x00dev->link.ant.active.rx;
	enum antenna tx = rt2x00dev->link.ant.active.tx;
	int rssi_curr = rt2x00_get_link_ant_rssi(&rt2x00dev->link);
	int rssi_old = rt2x00_update_ant_rssi(&rt2x00dev->link, rssi_curr);

	/*
	 * Legacy driver indicates that we should swap antenna's
	 * when the difference in RSSI is greater that 5. This
	 * also should be done when the RSSI was actually better
	 * then the previous sample.
	 * When the difference exceeds the threshold we should
	 * sample the rssi from the other antenna to make a valid
	 * comparison between the 2 antennas.
	 */
	if (abs(rssi_curr - rssi_old) < 5)
		return;

	rt2x00dev->link.ant.flags |= ANTENNA_MODE_SAMPLE;

	if (rt2x00dev->link.ant.flags & ANTENNA_RX_DIVERSITY)
		rx = (rx == ANTENNA_A) ? ANTENNA_B : ANTENNA_A;

	if (rt2x00dev->link.ant.flags & ANTENNA_TX_DIVERSITY)
		tx = (tx == ANTENNA_A) ? ANTENNA_B : ANTENNA_A;

	rt2x00lib_config_antenna(rt2x00dev, rx, tx);
}

static void rt2x00lib_evaluate_antenna(struct rt2x00_dev *rt2x00dev)
{
	/*
	 * Determine if software diversity is enabled for
	 * either the TX or RX antenna (or both).
	 * Always perform this check since within the link
	 * tuner interval the configuration might have changed.
	 */
	rt2x00dev->link.ant.flags &= ~ANTENNA_RX_DIVERSITY;
	rt2x00dev->link.ant.flags &= ~ANTENNA_TX_DIVERSITY;

	if (rt2x00dev->hw->conf.antenna_sel_rx == 0 &&
	    rt2x00dev->default_ant.rx == ANTENNA_SW_DIVERSITY)
		rt2x00dev->link.ant.flags |= ANTENNA_RX_DIVERSITY;
	if (rt2x00dev->hw->conf.antenna_sel_tx == 0 &&
	    rt2x00dev->default_ant.tx == ANTENNA_SW_DIVERSITY)
		rt2x00dev->link.ant.flags |= ANTENNA_TX_DIVERSITY;

	if (!(rt2x00dev->link.ant.flags & ANTENNA_RX_DIVERSITY) &&
	    !(rt2x00dev->link.ant.flags & ANTENNA_TX_DIVERSITY)) {
		rt2x00dev->link.ant.flags = 0;
		return;
	}

	/*
	 * If we have only sampled the data over the last period
	 * we should now harvest the data. Otherwise just evaluate
	 * the data. The latter should only be performed once
	 * every 2 seconds.
	 */
	if (rt2x00dev->link.ant.flags & ANTENNA_MODE_SAMPLE)
		rt2x00lib_evaluate_antenna_sample(rt2x00dev);
	else if (rt2x00dev->link.count & 1)
		rt2x00lib_evaluate_antenna_eval(rt2x00dev);
}

static void rt2x00lib_update_link_stats(struct link *link, int rssi)
{
	int avg_rssi = rssi;

	/*
	 * Update global RSSI
	 */
	if (link->qual.avg_rssi)
		avg_rssi = MOVING_AVERAGE(link->qual.avg_rssi, rssi, 8);
	link->qual.avg_rssi = avg_rssi;

	/*
	 * Update antenna RSSI
	 */
	if (link->ant.rssi_ant)
		rssi = MOVING_AVERAGE(link->ant.rssi_ant, rssi, 8);
	link->ant.rssi_ant = rssi;
}

static void rt2x00lib_precalculate_link_signal(struct link_qual *qual)
{
	if (qual->rx_failed || qual->rx_success)
		qual->rx_percentage =
		    (qual->rx_success * 100) /
		    (qual->rx_failed + qual->rx_success);
	else
		qual->rx_percentage = 50;

	if (qual->tx_failed || qual->tx_success)
		qual->tx_percentage =
		    (qual->tx_success * 100) /
		    (qual->tx_failed + qual->tx_success);
	else
		qual->tx_percentage = 50;

	qual->rx_success = 0;
	qual->rx_failed = 0;
	qual->tx_success = 0;
	qual->tx_failed = 0;
}

static int rt2x00lib_calculate_link_signal(struct rt2x00_dev *rt2x00dev,
					   int rssi)
{
	int rssi_percentage = 0;
	int signal;

	/*
	 * We need a positive value for the RSSI.
	 */
	if (rssi < 0)
		rssi += rt2x00dev->rssi_offset;

	/*
	 * Calculate the different percentages,
	 * which will be used for the signal.
	 */
	if (rt2x00dev->rssi_offset)
		rssi_percentage = (rssi * 100) / rt2x00dev->rssi_offset;

	/*
	 * Add the individual percentages and use the WEIGHT
	 * defines to calculate the current link signal.
	 */
	signal = ((WEIGHT_RSSI * rssi_percentage) +
		  (WEIGHT_TX * rt2x00dev->link.qual.tx_percentage) +
		  (WEIGHT_RX * rt2x00dev->link.qual.rx_percentage)) / 100;

	return (signal > 100) ? 100 : signal;
}

static void rt2x00lib_link_tuner(struct work_struct *work)
{
	struct rt2x00_dev *rt2x00dev =
	    container_of(work, struct rt2x00_dev, link.work.work);

	/*
	 * When the radio is shutting down we should
	 * immediately cease all link tuning.
	 */
	if (!test_bit(DEVICE_ENABLED_RADIO, &rt2x00dev->flags))
		return;

	/*
	 * Update statistics.
	 */
	rt2x00dev->ops->lib->link_stats(rt2x00dev, &rt2x00dev->link.qual);
	rt2x00dev->low_level_stats.dot11FCSErrorCount +=
	    rt2x00dev->link.qual.rx_failed;

	/*
	 * Only perform the link tuning when Link tuning
	 * has been enabled (This could have been disabled from the EEPROM).
	 */
	if (!test_bit(CONFIG_DISABLE_LINK_TUNING, &rt2x00dev->flags))
		rt2x00dev->ops->lib->link_tuner(rt2x00dev);

	/*
	 * Precalculate a portion of the link signal which is
	 * in based on the tx/rx success/failure counters.
	 */
	rt2x00lib_precalculate_link_signal(&rt2x00dev->link.qual);

	/*
	 * Send a signal to the led to update the led signal strength.
	 */
	rt2x00leds_led_quality(rt2x00dev, rt2x00dev->link.qual.avg_rssi);

	/*
	 * Evaluate antenna setup, make this the last step since this could
	 * possibly reset some statistics.
	 */
	rt2x00lib_evaluate_antenna(rt2x00dev);

	/*
	 * Increase tuner counter, and reschedule the next link tuner run.
	 */
	rt2x00dev->link.count++;
	queue_delayed_work(rt2x00dev->hw->workqueue, &rt2x00dev->link.work,
			   LINK_TUNE_INTERVAL);
}

static void rt2x00lib_packetfilter_scheduled(struct work_struct *work)
{
	struct rt2x00_dev *rt2x00dev =
	    container_of(work, struct rt2x00_dev, filter_work);

	rt2x00dev->ops->lib->config_filter(rt2x00dev, rt2x00dev->packet_filter);
}

static void rt2x00lib_intf_scheduled_iter(void *data, u8 *mac,
					  struct ieee80211_vif *vif)
{
	struct rt2x00_dev *rt2x00dev = data;
	struct rt2x00_intf *intf = vif_to_intf(vif);
	struct sk_buff *skb;
	struct ieee80211_tx_control control;
	struct ieee80211_bss_conf conf;
	int delayed_flags;

	/*
	 * Copy all data we need during this action under the protection
	 * of a spinlock. Otherwise race conditions might occur which results
	 * into an invalid configuration.
	 */
	spin_lock(&intf->lock);

	memcpy(&conf, &intf->conf, sizeof(conf));
	delayed_flags = intf->delayed_flags;
	intf->delayed_flags = 0;

	spin_unlock(&intf->lock);

	if (delayed_flags & DELAYED_UPDATE_BEACON) {
		skb = ieee80211_beacon_get(rt2x00dev->hw, vif, &control);
		if (skb && rt2x00dev->ops->hw->beacon_update(rt2x00dev->hw,
							     skb, &control))
			dev_kfree_skb(skb);
	}

	if (delayed_flags & DELAYED_CONFIG_ERP)
		rt2x00lib_config_erp(rt2x00dev, intf, &intf->conf);

	if (delayed_flags & DELAYED_LED_ASSOC)
		rt2x00leds_led_assoc(rt2x00dev, !!rt2x00dev->intf_associated);
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

/*
 * Interrupt context handlers.
 */
static void rt2x00lib_beacondone_iter(void *data, u8 *mac,
				      struct ieee80211_vif *vif)
{
	struct rt2x00_intf *intf = vif_to_intf(vif);

	if (vif->type != IEEE80211_IF_TYPE_AP &&
	    vif->type != IEEE80211_IF_TYPE_IBSS)
		return;

	spin_lock(&intf->lock);
	intf->delayed_flags |= DELAYED_UPDATE_BEACON;
	spin_unlock(&intf->lock);
}

void rt2x00lib_beacondone(struct rt2x00_dev *rt2x00dev)
{
	if (!test_bit(DEVICE_ENABLED_RADIO, &rt2x00dev->flags))
		return;

	ieee80211_iterate_active_interfaces(rt2x00dev->hw,
					    rt2x00lib_beacondone_iter,
					    rt2x00dev);

	queue_work(rt2x00dev->hw->workqueue, &rt2x00dev->intf_work);
}
EXPORT_SYMBOL_GPL(rt2x00lib_beacondone);

void rt2x00lib_txdone(struct queue_entry *entry,
		      struct txdone_entry_desc *txdesc)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct skb_frame_desc *skbdesc;
	struct ieee80211_tx_status tx_status;
	int success = !!(txdesc->status == TX_SUCCESS ||
			 txdesc->status == TX_SUCCESS_RETRY);
	int fail = !!(txdesc->status == TX_FAIL_RETRY ||
		      txdesc->status == TX_FAIL_INVALID ||
		      txdesc->status == TX_FAIL_OTHER);

	/*
	 * Update TX statistics.
	 */
	rt2x00dev->link.qual.tx_success += success;
	rt2x00dev->link.qual.tx_failed += txdesc->retry + fail;

	/*
	 * Initialize TX status
	 */
	tx_status.flags = 0;
	tx_status.ack_signal = 0;
	tx_status.excessive_retries = (txdesc->status == TX_FAIL_RETRY);
	tx_status.retry_count = txdesc->retry;
	memcpy(&tx_status.control, txdesc->control, sizeof(*txdesc->control));

	if (!(tx_status.control.flags & IEEE80211_TXCTL_NO_ACK)) {
		if (success)
			tx_status.flags |= IEEE80211_TX_STATUS_ACK;
		else
			rt2x00dev->low_level_stats.dot11ACKFailureCount++;
	}

	tx_status.queue_length = entry->queue->limit;
	tx_status.queue_number = tx_status.control.queue;

	if (tx_status.control.flags & IEEE80211_TXCTL_USE_RTS_CTS) {
		if (success)
			rt2x00dev->low_level_stats.dot11RTSSuccessCount++;
		else
			rt2x00dev->low_level_stats.dot11RTSFailureCount++;
	}

	/*
	 * Send the tx_status to debugfs. Only send the status report
	 * to mac80211 when the frame originated from there. If this was
	 * a extra frame coming through a mac80211 library call (RTS/CTS)
	 * then we should not send the status report back.
	 * If send to mac80211, mac80211 will clean up the skb structure,
	 * otherwise we have to do it ourself.
	 */
	skbdesc = get_skb_frame_desc(entry->skb);
	skbdesc->frame_type = DUMP_FRAME_TXDONE;

	rt2x00debug_dump_frame(rt2x00dev, entry->skb);

	if (!(skbdesc->flags & FRAME_DESC_DRIVER_GENERATED))
		ieee80211_tx_status_irqsafe(rt2x00dev->hw,
					    entry->skb, &tx_status);
	else
		dev_kfree_skb(entry->skb);
	entry->skb = NULL;
}
EXPORT_SYMBOL_GPL(rt2x00lib_txdone);

void rt2x00lib_rxdone(struct queue_entry *entry,
		      struct rxdone_entry_desc *rxdesc)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct ieee80211_rx_status *rx_status = &rt2x00dev->rx_status;
	struct ieee80211_supported_band *sband;
	struct ieee80211_hdr *hdr;
	const struct rt2x00_rate *rate;
	unsigned int i;
	int idx = -1;
	u16 fc;

	/*
	 * Update RX statistics.
	 */
	sband = &rt2x00dev->bands[rt2x00dev->curr_band];
	for (i = 0; i < sband->n_bitrates; i++) {
		rate = rt2x00_get_rate(sband->bitrates[i].hw_value);

		if (((rxdesc->dev_flags & RXDONE_SIGNAL_PLCP) &&
		     (rate->plcp == rxdesc->signal)) ||
		    (!(rxdesc->dev_flags & RXDONE_SIGNAL_PLCP) &&
		      (rate->bitrate == rxdesc->signal))) {
			idx = i;
			break;
		}
	}

	if (idx < 0) {
		WARNING(rt2x00dev, "Frame received with unrecognized signal,"
			"signal=0x%.2x, plcp=%d.\n", rxdesc->signal,
			!!(rxdesc->dev_flags & RXDONE_SIGNAL_PLCP));
		idx = 0;
	}

	/*
	 * Only update link status if this is a beacon frame carrying our bssid.
	 */
	hdr = (struct ieee80211_hdr *)entry->skb->data;
	fc = le16_to_cpu(hdr->frame_control);
	if (is_beacon(fc) && (rxdesc->dev_flags & RXDONE_MY_BSS))
		rt2x00lib_update_link_stats(&rt2x00dev->link, rxdesc->rssi);

	rt2x00dev->link.qual.rx_success++;

	rx_status->rate_idx = idx;
	rx_status->signal =
	    rt2x00lib_calculate_link_signal(rt2x00dev, rxdesc->rssi);
	rx_status->ssi = rxdesc->rssi;
	rx_status->flag = rxdesc->flags;
	rx_status->antenna = rt2x00dev->link.ant.active.rx;

	/*
	 * Send frame to mac80211 & debugfs.
	 * mac80211 will clean up the skb structure.
	 */
	get_skb_frame_desc(entry->skb)->frame_type = DUMP_FRAME_RXDONE;
	rt2x00debug_dump_frame(rt2x00dev, entry->skb);
	ieee80211_rx_irqsafe(rt2x00dev->hw, entry->skb, rx_status);
	entry->skb = NULL;
}
EXPORT_SYMBOL_GPL(rt2x00lib_rxdone);

/*
 * TX descriptor initializer
 */
void rt2x00lib_write_tx_desc(struct rt2x00_dev *rt2x00dev,
			     struct sk_buff *skb,
			     struct ieee80211_tx_control *control)
{
	struct txentry_desc txdesc;
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skbdesc->data;
	const struct rt2x00_rate *rate;
	int tx_rate;
	int length;
	int duration;
	int residual;
	u16 frame_control;
	u16 seq_ctrl;

	memset(&txdesc, 0, sizeof(txdesc));

	txdesc.queue = skbdesc->entry->queue->qid;
	txdesc.cw_min = skbdesc->entry->queue->cw_min;
	txdesc.cw_max = skbdesc->entry->queue->cw_max;
	txdesc.aifs = skbdesc->entry->queue->aifs;

	/*
	 * Read required fields from ieee80211 header.
	 */
	frame_control = le16_to_cpu(hdr->frame_control);
	seq_ctrl = le16_to_cpu(hdr->seq_ctrl);

	tx_rate = control->tx_rate->hw_value;

	/*
	 * Check whether this frame is to be acked
	 */
	if (!(control->flags & IEEE80211_TXCTL_NO_ACK))
		__set_bit(ENTRY_TXD_ACK, &txdesc.flags);

	/*
	 * Check if this is a RTS/CTS frame
	 */
	if (is_rts_frame(frame_control) || is_cts_frame(frame_control)) {
		__set_bit(ENTRY_TXD_BURST, &txdesc.flags);
		if (is_rts_frame(frame_control)) {
			__set_bit(ENTRY_TXD_RTS_FRAME, &txdesc.flags);
			__set_bit(ENTRY_TXD_ACK, &txdesc.flags);
		} else
			__clear_bit(ENTRY_TXD_ACK, &txdesc.flags);
		if (control->rts_cts_rate)
			tx_rate = control->rts_cts_rate->hw_value;
	}

	rate = rt2x00_get_rate(tx_rate);

	/*
	 * Check if more fragments are pending
	 */
	if (ieee80211_get_morefrag(hdr)) {
		__set_bit(ENTRY_TXD_BURST, &txdesc.flags);
		__set_bit(ENTRY_TXD_MORE_FRAG, &txdesc.flags);
	}

	/*
	 * Beacons and probe responses require the tsf timestamp
	 * to be inserted into the frame.
	 */
	if (control->queue == RT2X00_BCN_QUEUE_BEACON ||
	    is_probe_resp(frame_control))
		__set_bit(ENTRY_TXD_REQ_TIMESTAMP, &txdesc.flags);

	/*
	 * Determine with what IFS priority this frame should be send.
	 * Set ifs to IFS_SIFS when the this is not the first fragment,
	 * or this fragment came after RTS/CTS.
	 */
	if ((seq_ctrl & IEEE80211_SCTL_FRAG) > 0 ||
	    test_bit(ENTRY_TXD_RTS_FRAME, &txdesc.flags))
		txdesc.ifs = IFS_SIFS;
	else
		txdesc.ifs = IFS_BACKOFF;

	/*
	 * PLCP setup
	 * Length calculation depends on OFDM/CCK rate.
	 */
	txdesc.signal = rate->plcp;
	txdesc.service = 0x04;

	length = skbdesc->data_len + FCS_LEN;
	if (rate->flags & DEV_RATE_OFDM) {
		__set_bit(ENTRY_TXD_OFDM_RATE, &txdesc.flags);

		txdesc.length_high = (length >> 6) & 0x3f;
		txdesc.length_low = length & 0x3f;
	} else {
		/*
		 * Convert length to microseconds.
		 */
		residual = get_duration_res(length, rate->bitrate);
		duration = get_duration(length, rate->bitrate);

		if (residual != 0) {
			duration++;

			/*
			 * Check if we need to set the Length Extension
			 */
			if (rate->bitrate == 110 && residual <= 30)
				txdesc.service |= 0x80;
		}

		txdesc.length_high = (duration >> 8) & 0xff;
		txdesc.length_low = duration & 0xff;

		/*
		 * When preamble is enabled we should set the
		 * preamble bit for the signal.
		 */
		if (rt2x00_get_rate_preamble(tx_rate))
			txdesc.signal |= 0x08;
	}

	rt2x00dev->ops->lib->write_tx_desc(rt2x00dev, skb, &txdesc, control);

	/*
	 * Update queue entry.
	 */
	skbdesc->entry->skb = skb;

	/*
	 * The frame has been completely initialized and ready
	 * for sending to the device. The caller will push the
	 * frame to the device, but we are going to push the
	 * frame to debugfs here.
	 */
	skbdesc->frame_type = DUMP_FRAME_TX;
	rt2x00debug_dump_frame(rt2x00dev, skb);
}
EXPORT_SYMBOL_GPL(rt2x00lib_write_tx_desc);

/*
 * Driver initialization handlers.
 */
const struct rt2x00_rate rt2x00_supported_rates[12] = {
	{
		.flags = DEV_RATE_CCK | DEV_RATE_BASIC,
		.bitrate = 10,
		.ratemask = BIT(0),
		.plcp = 0x00,
	},
	{
		.flags = DEV_RATE_CCK | DEV_RATE_SHORT_PREAMBLE | DEV_RATE_BASIC,
		.bitrate = 20,
		.ratemask = BIT(1),
		.plcp = 0x01,
	},
	{
		.flags = DEV_RATE_CCK | DEV_RATE_SHORT_PREAMBLE | DEV_RATE_BASIC,
		.bitrate = 55,
		.ratemask = BIT(2),
		.plcp = 0x02,
	},
	{
		.flags = DEV_RATE_CCK | DEV_RATE_SHORT_PREAMBLE | DEV_RATE_BASIC,
		.bitrate = 110,
		.ratemask = BIT(3),
		.plcp = 0x03,
	},
	{
		.flags = DEV_RATE_OFDM | DEV_RATE_BASIC,
		.bitrate = 60,
		.ratemask = BIT(4),
		.plcp = 0x0b,
	},
	{
		.flags = DEV_RATE_OFDM,
		.bitrate = 90,
		.ratemask = BIT(5),
		.plcp = 0x0f,
	},
	{
		.flags = DEV_RATE_OFDM | DEV_RATE_BASIC,
		.bitrate = 120,
		.ratemask = BIT(6),
		.plcp = 0x0a,
	},
	{
		.flags = DEV_RATE_OFDM,
		.bitrate = 180,
		.ratemask = BIT(7),
		.plcp = 0x0e,
	},
	{
		.flags = DEV_RATE_OFDM | DEV_RATE_BASIC,
		.bitrate = 240,
		.ratemask = BIT(8),
		.plcp = 0x09,
	},
	{
		.flags = DEV_RATE_OFDM,
		.bitrate = 360,
		.ratemask = BIT(9),
		.plcp = 0x0d,
	},
	{
		.flags = DEV_RATE_OFDM,
		.bitrate = 480,
		.ratemask = BIT(10),
		.plcp = 0x08,
	},
	{
		.flags = DEV_RATE_OFDM,
		.bitrate = 540,
		.ratemask = BIT(11),
		.plcp = 0x0c,
	},
};

static void rt2x00lib_channel(struct ieee80211_channel *entry,
			      const int channel, const int tx_power,
			      const int value)
{
	entry->center_freq = ieee80211_channel_to_frequency(channel);
	entry->hw_value = value;
	entry->max_power = tx_power;
	entry->max_antenna_gain = 0xff;
}

static void rt2x00lib_rate(struct ieee80211_rate *entry,
			   const u16 index, const struct rt2x00_rate *rate)
{
	entry->flags = 0;
	entry->bitrate = rate->bitrate;
	entry->hw_value = rt2x00_create_rate_hw_value(index, 0);
	entry->hw_value_short = entry->hw_value;

	if (rate->flags & DEV_RATE_SHORT_PREAMBLE) {
		entry->flags |= IEEE80211_RATE_SHORT_PREAMBLE;
		entry->hw_value_short |= rt2x00_create_rate_hw_value(index, 1);
	}
}

static int rt2x00lib_probe_hw_modes(struct rt2x00_dev *rt2x00dev,
				    struct hw_mode_spec *spec)
{
	struct ieee80211_hw *hw = rt2x00dev->hw;
	struct ieee80211_channel *channels;
	struct ieee80211_rate *rates;
	unsigned int num_rates;
	unsigned int i;
	unsigned char tx_power;

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
		if (spec->channels[i].channel <= 14) {
			if (spec->tx_power_bg)
				tx_power = spec->tx_power_bg[i];
			else
				tx_power = spec->tx_power_default;
		} else {
			if (spec->tx_power_a)
				tx_power = spec->tx_power_a[i];
			else
				tx_power = spec->tx_power_default;
		}

		rt2x00lib_channel(&channels[i],
				  spec->channels[i].channel, tx_power, i);
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
	}

	return 0;

 exit_free_channels:
	kfree(channels);
	ERROR(rt2x00dev, "Allocation ieee80211 modes failed.\n");
	return -ENOMEM;
}

static void rt2x00lib_remove_hw(struct rt2x00_dev *rt2x00dev)
{
	if (test_bit(DEVICE_REGISTERED_HW, &rt2x00dev->flags))
		ieee80211_unregister_hw(rt2x00dev->hw);

	if (likely(rt2x00dev->hw->wiphy->bands[IEEE80211_BAND_2GHZ])) {
		kfree(rt2x00dev->hw->wiphy->bands[IEEE80211_BAND_2GHZ]->channels);
		kfree(rt2x00dev->hw->wiphy->bands[IEEE80211_BAND_2GHZ]->bitrates);
		rt2x00dev->hw->wiphy->bands[IEEE80211_BAND_2GHZ] = NULL;
		rt2x00dev->hw->wiphy->bands[IEEE80211_BAND_5GHZ] = NULL;
	}
}

static int rt2x00lib_probe_hw(struct rt2x00_dev *rt2x00dev)
{
	struct hw_mode_spec *spec = &rt2x00dev->spec;
	int status;

	/*
	 * Initialize HW modes.
	 */
	status = rt2x00lib_probe_hw_modes(rt2x00dev, spec);
	if (status)
		return status;

	/*
	 * Register HW.
	 */
	status = ieee80211_register_hw(rt2x00dev->hw);
	if (status) {
		rt2x00lib_remove_hw(rt2x00dev);
		return status;
	}

	__set_bit(DEVICE_REGISTERED_HW, &rt2x00dev->flags);

	return 0;
}

/*
 * Initialization/uninitialization handlers.
 */
static void rt2x00lib_uninitialize(struct rt2x00_dev *rt2x00dev)
{
	if (!__test_and_clear_bit(DEVICE_INITIALIZED, &rt2x00dev->flags))
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

	if (test_bit(DEVICE_INITIALIZED, &rt2x00dev->flags))
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
	if (status)
		goto exit;

	__set_bit(DEVICE_INITIALIZED, &rt2x00dev->flags);

	/*
	 * Register the extra components.
	 */
	rt2x00rfkill_register(rt2x00dev);

	return 0;

exit:
	rt2x00lib_uninitialize(rt2x00dev);

	return status;
}

int rt2x00lib_start(struct rt2x00_dev *rt2x00dev)
{
	int retval;

	if (test_bit(DEVICE_STARTED, &rt2x00dev->flags))
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

	/*
	 * Enable radio.
	 */
	retval = rt2x00lib_enable_radio(rt2x00dev);
	if (retval) {
		rt2x00lib_uninitialize(rt2x00dev);
		return retval;
	}

	rt2x00dev->intf_ap_count = 0;
	rt2x00dev->intf_sta_count = 0;
	rt2x00dev->intf_associated = 0;

	__set_bit(DEVICE_STARTED, &rt2x00dev->flags);

	return 0;
}

void rt2x00lib_stop(struct rt2x00_dev *rt2x00dev)
{
	if (!test_bit(DEVICE_STARTED, &rt2x00dev->flags))
		return;

	/*
	 * Perhaps we can add something smarter here,
	 * but for now just disabling the radio should do.
	 */
	rt2x00lib_disable_radio(rt2x00dev);

	rt2x00dev->intf_ap_count = 0;
	rt2x00dev->intf_sta_count = 0;
	rt2x00dev->intf_associated = 0;

	__clear_bit(DEVICE_STARTED, &rt2x00dev->flags);
}

/*
 * driver allocation handlers.
 */
int rt2x00lib_probe_dev(struct rt2x00_dev *rt2x00dev)
{
	int retval = -ENOMEM;

	/*
	 * Make room for rt2x00_intf inside the per-interface
	 * structure ieee80211_vif.
	 */
	rt2x00dev->hw->vif_data_size = sizeof(struct rt2x00_intf);

	/*
	 * Let the driver probe the device to detect the capabilities.
	 */
	retval = rt2x00dev->ops->lib->probe_hw(rt2x00dev);
	if (retval) {
		ERROR(rt2x00dev, "Failed to allocate device.\n");
		goto exit;
	}

	/*
	 * Initialize configuration work.
	 */
	INIT_WORK(&rt2x00dev->intf_work, rt2x00lib_intf_scheduled);
	INIT_WORK(&rt2x00dev->filter_work, rt2x00lib_packetfilter_scheduled);
	INIT_DELAYED_WORK(&rt2x00dev->link.work, rt2x00lib_link_tuner);

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
	rt2x00leds_register(rt2x00dev);
	rt2x00rfkill_allocate(rt2x00dev);
	rt2x00debug_register(rt2x00dev);

	__set_bit(DEVICE_PRESENT, &rt2x00dev->flags);

	return 0;

exit:
	rt2x00lib_remove_dev(rt2x00dev);

	return retval;
}
EXPORT_SYMBOL_GPL(rt2x00lib_probe_dev);

void rt2x00lib_remove_dev(struct rt2x00_dev *rt2x00dev)
{
	__clear_bit(DEVICE_PRESENT, &rt2x00dev->flags);

	/*
	 * Disable radio.
	 */
	rt2x00lib_disable_radio(rt2x00dev);

	/*
	 * Uninitialize device.
	 */
	rt2x00lib_uninitialize(rt2x00dev);

	/*
	 * Free extra components
	 */
	rt2x00debug_deregister(rt2x00dev);
	rt2x00rfkill_free(rt2x00dev);
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
	int retval;

	NOTICE(rt2x00dev, "Going to sleep.\n");
	__clear_bit(DEVICE_PRESENT, &rt2x00dev->flags);

	/*
	 * Only continue if mac80211 has open interfaces.
	 */
	if (!test_bit(DEVICE_STARTED, &rt2x00dev->flags))
		goto exit;
	__set_bit(DEVICE_STARTED_SUSPEND, &rt2x00dev->flags);

	/*
	 * Disable radio.
	 */
	rt2x00lib_stop(rt2x00dev);
	rt2x00lib_uninitialize(rt2x00dev);

	/*
	 * Suspend/disable extra components.
	 */
	rt2x00leds_suspend(rt2x00dev);
	rt2x00rfkill_suspend(rt2x00dev);
	rt2x00debug_deregister(rt2x00dev);

exit:
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
	retval = rt2x00dev->ops->lib->set_device_state(rt2x00dev, STATE_SLEEP);
	if (retval)
		WARNING(rt2x00dev, "Device failed to enter sleep state, "
			"continue suspending.\n");

	return 0;
}
EXPORT_SYMBOL_GPL(rt2x00lib_suspend);

static void rt2x00lib_resume_intf(void *data, u8 *mac,
				  struct ieee80211_vif *vif)
{
	struct rt2x00_dev *rt2x00dev = data;
	struct rt2x00_intf *intf = vif_to_intf(vif);

	spin_lock(&intf->lock);

	rt2x00lib_config_intf(rt2x00dev, intf,
			      vif->type, intf->mac, intf->bssid);


	/*
	 * Master or Ad-hoc mode require a new beacon update.
	 */
	if (vif->type == IEEE80211_IF_TYPE_AP ||
	    vif->type == IEEE80211_IF_TYPE_IBSS)
		intf->delayed_flags |= DELAYED_UPDATE_BEACON;

	spin_unlock(&intf->lock);
}

int rt2x00lib_resume(struct rt2x00_dev *rt2x00dev)
{
	int retval;

	NOTICE(rt2x00dev, "Waking up.\n");

	/*
	 * Restore/enable extra components.
	 */
	rt2x00debug_register(rt2x00dev);
	rt2x00rfkill_resume(rt2x00dev);
	rt2x00leds_resume(rt2x00dev);

	/*
	 * Only continue if mac80211 had open interfaces.
	 */
	if (!__test_and_clear_bit(DEVICE_STARTED_SUSPEND, &rt2x00dev->flags))
		return 0;

	/*
	 * Reinitialize device and all active interfaces.
	 */
	retval = rt2x00lib_start(rt2x00dev);
	if (retval)
		goto exit;

	/*
	 * Reconfigure device.
	 */
	rt2x00lib_config(rt2x00dev, &rt2x00dev->hw->conf, 1);
	if (!rt2x00dev->hw->conf.radio_enabled)
		rt2x00lib_disable_radio(rt2x00dev);

	/*
	 * Iterator over each active interface to
	 * reconfigure the hardware.
	 */
	ieee80211_iterate_active_interfaces(rt2x00dev->hw,
					    rt2x00lib_resume_intf, rt2x00dev);

	/*
	 * We are ready again to receive requests from mac80211.
	 */
	__set_bit(DEVICE_PRESENT, &rt2x00dev->flags);

	/*
	 * It is possible that during that mac80211 has attempted
	 * to send frames while we were suspending or resuming.
	 * In that case we have disabled the TX queue and should
	 * now enable it again
	 */
	ieee80211_start_queues(rt2x00dev->hw);

	/*
	 * During interface iteration we might have changed the
	 * delayed_flags, time to handles the event by calling
	 * the work handler directly.
	 */
	rt2x00lib_intf_scheduled(&rt2x00dev->intf_work);

	return 0;

exit:
	rt2x00lib_disable_radio(rt2x00dev);
	rt2x00lib_uninitialize(rt2x00dev);
	rt2x00debug_deregister(rt2x00dev);

	return retval;
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
