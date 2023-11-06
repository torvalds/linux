// SPDX-License-Identifier: GPL-2.0-or-later
/*
	Copyright (C) 2004 - 2009 Ivo van Doorn <IvDoorn@gmail.com>
	<http://rt2x00.serialmonkey.com>

 */

/*
	Module: rt2x00lib
	Abstract: rt2x00 generic link tuning routines.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "rt2x00.h"
#include "rt2x00lib.h"

/*
 * When we lack RSSI information return something less then -80 to
 * tell the driver to tune the device to maximum sensitivity.
 */
#define DEFAULT_RSSI		-128

static inline int rt2x00link_get_avg_rssi(struct ewma_rssi *ewma)
{
	unsigned long avg;

	avg = ewma_rssi_read(ewma);
	if (avg)
		return -avg;

	return DEFAULT_RSSI;
}

static int rt2x00link_antenna_get_link_rssi(struct rt2x00_dev *rt2x00dev)
{
	struct link_ant *ant = &rt2x00dev->link.ant;

	if (rt2x00dev->link.qual.rx_success)
		return rt2x00link_get_avg_rssi(&ant->rssi_ant);

	return DEFAULT_RSSI;
}

static int rt2x00link_antenna_get_rssi_history(struct rt2x00_dev *rt2x00dev)
{
	struct link_ant *ant = &rt2x00dev->link.ant;

	if (ant->rssi_history)
		return ant->rssi_history;
	return DEFAULT_RSSI;
}

static void rt2x00link_antenna_update_rssi_history(struct rt2x00_dev *rt2x00dev,
						   int rssi)
{
	struct link_ant *ant = &rt2x00dev->link.ant;
	ant->rssi_history = rssi;
}

static void rt2x00link_antenna_reset(struct rt2x00_dev *rt2x00dev)
{
	ewma_rssi_init(&rt2x00dev->link.ant.rssi_ant);
}

static void rt2x00lib_antenna_diversity_sample(struct rt2x00_dev *rt2x00dev)
{
	struct link_ant *ant = &rt2x00dev->link.ant;
	struct antenna_setup new_ant;
	int other_antenna;

	int sample_current = rt2x00link_antenna_get_link_rssi(rt2x00dev);
	int sample_other = rt2x00link_antenna_get_rssi_history(rt2x00dev);

	memcpy(&new_ant, &ant->active, sizeof(new_ant));

	/*
	 * We are done sampling. Now we should evaluate the results.
	 */
	ant->flags &= ~ANTENNA_MODE_SAMPLE;

	/*
	 * During the last period we have sampled the RSSI
	 * from both antennas. It now is time to determine
	 * which antenna demonstrated the best performance.
	 * When we are already on the antenna with the best
	 * performance, just create a good starting point
	 * for the history and we are done.
	 */
	if (sample_current >= sample_other) {
		rt2x00link_antenna_update_rssi_history(rt2x00dev,
			sample_current);
		return;
	}

	other_antenna = (ant->active.rx == ANTENNA_A) ? ANTENNA_B : ANTENNA_A;

	if (ant->flags & ANTENNA_RX_DIVERSITY)
		new_ant.rx = other_antenna;

	if (ant->flags & ANTENNA_TX_DIVERSITY)
		new_ant.tx = other_antenna;

	rt2x00lib_config_antenna(rt2x00dev, new_ant);
}

static void rt2x00lib_antenna_diversity_eval(struct rt2x00_dev *rt2x00dev)
{
	struct link_ant *ant = &rt2x00dev->link.ant;
	struct antenna_setup new_ant;
	int rssi_curr;
	int rssi_old;

	memcpy(&new_ant, &ant->active, sizeof(new_ant));

	/*
	 * Get current RSSI value along with the historical value,
	 * after that update the history with the current value.
	 */
	rssi_curr = rt2x00link_antenna_get_link_rssi(rt2x00dev);
	rssi_old = rt2x00link_antenna_get_rssi_history(rt2x00dev);
	rt2x00link_antenna_update_rssi_history(rt2x00dev, rssi_curr);

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

	ant->flags |= ANTENNA_MODE_SAMPLE;

	if (ant->flags & ANTENNA_RX_DIVERSITY)
		new_ant.rx = (new_ant.rx == ANTENNA_A) ? ANTENNA_B : ANTENNA_A;

	if (ant->flags & ANTENNA_TX_DIVERSITY)
		new_ant.tx = (new_ant.tx == ANTENNA_A) ? ANTENNA_B : ANTENNA_A;

	rt2x00lib_config_antenna(rt2x00dev, new_ant);
}

static bool rt2x00lib_antenna_diversity(struct rt2x00_dev *rt2x00dev)
{
	struct link_ant *ant = &rt2x00dev->link.ant;

	/*
	 * Determine if software diversity is enabled for
	 * either the TX or RX antenna (or both).
	 */
	if (!(ant->flags & ANTENNA_RX_DIVERSITY) &&
	    !(ant->flags & ANTENNA_TX_DIVERSITY)) {
		ant->flags = 0;
		return true;
	}

	/*
	 * If we have only sampled the data over the last period
	 * we should now harvest the data. Otherwise just evaluate
	 * the data. The latter should only be performed once
	 * every 2 seconds.
	 */
	if (ant->flags & ANTENNA_MODE_SAMPLE) {
		rt2x00lib_antenna_diversity_sample(rt2x00dev);
		return true;
	} else if (rt2x00dev->link.count & 1) {
		rt2x00lib_antenna_diversity_eval(rt2x00dev);
		return true;
	}

	return false;
}

void rt2x00link_update_stats(struct rt2x00_dev *rt2x00dev,
			     struct sk_buff *skb,
			     struct rxdone_entry_desc *rxdesc)
{
	struct link *link = &rt2x00dev->link;
	struct link_qual *qual = &rt2x00dev->link.qual;
	struct link_ant *ant = &rt2x00dev->link.ant;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	/*
	 * No need to update the stats for !=STA interfaces
	 */
	if (!rt2x00dev->intf_sta_count)
		return;

	/*
	 * Frame was received successfully since non-successful
	 * frames would have been dropped by the hardware.
	 */
	qual->rx_success++;

	/*
	 * We are only interested in quality statistics from
	 * beacons which came from the BSS which we are
	 * associated with.
	 */
	if (!ieee80211_is_beacon(hdr->frame_control) ||
	    !(rxdesc->dev_flags & RXDONE_MY_BSS))
		return;

	/*
	 * Update global RSSI
	 */
	ewma_rssi_add(&link->avg_rssi, -rxdesc->rssi);

	/*
	 * Update antenna RSSI
	 */
	ewma_rssi_add(&ant->rssi_ant, -rxdesc->rssi);
}

void rt2x00link_start_tuner(struct rt2x00_dev *rt2x00dev)
{
	struct link *link = &rt2x00dev->link;

	/*
	 * Single monitor mode interfaces should never have
	 * work with link tuners.
	 */
	if (!rt2x00dev->intf_ap_count && !rt2x00dev->intf_sta_count)
		return;

	/*
	 * While scanning, link tuning is disabled. By default
	 * the most sensitive settings will be used to make sure
	 * that all beacons and probe responses will be received
	 * during the scan.
	 */
	if (test_bit(DEVICE_STATE_SCANNING, &rt2x00dev->flags))
		return;

	rt2x00link_reset_tuner(rt2x00dev, false);

	if (test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags))
		ieee80211_queue_delayed_work(rt2x00dev->hw,
					     &link->work, LINK_TUNE_INTERVAL);
}

void rt2x00link_stop_tuner(struct rt2x00_dev *rt2x00dev)
{
	cancel_delayed_work_sync(&rt2x00dev->link.work);
}

void rt2x00link_reset_tuner(struct rt2x00_dev *rt2x00dev, bool antenna)
{
	struct link_qual *qual = &rt2x00dev->link.qual;
	u8 vgc_level = qual->vgc_level_reg;

	if (!test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
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
	memset(qual, 0, sizeof(*qual));
	ewma_rssi_init(&rt2x00dev->link.avg_rssi);

	/*
	 * Restore the VGC level as stored in the registers,
	 * the driver can use this to determine if the register
	 * must be updated during reset or not.
	 */
	qual->vgc_level_reg = vgc_level;

	/*
	 * Reset the link tuner.
	 */
	rt2x00dev->ops->lib->reset_tuner(rt2x00dev, qual);

	if (antenna)
		rt2x00link_antenna_reset(rt2x00dev);
}

static void rt2x00link_reset_qual(struct rt2x00_dev *rt2x00dev)
{
	struct link_qual *qual = &rt2x00dev->link.qual;

	qual->rx_success = 0;
	qual->rx_failed = 0;
	qual->tx_success = 0;
	qual->tx_failed = 0;
}

static void rt2x00link_tuner_sta(struct rt2x00_dev *rt2x00dev, struct link *link)
{
	struct link_qual *qual = &rt2x00dev->link.qual;

	/*
	 * Update statistics.
	 */
	rt2x00dev->ops->lib->link_stats(rt2x00dev, qual);
	rt2x00dev->low_level_stats.dot11FCSErrorCount += qual->rx_failed;

	/*
	 * Update quality RSSI for link tuning,
	 * when we have received some frames and we managed to
	 * collect the RSSI data we could use this. Otherwise we
	 * must fallback to the default RSSI value.
	 */
	if (!qual->rx_success)
		qual->rssi = DEFAULT_RSSI;
	else
		qual->rssi = rt2x00link_get_avg_rssi(&link->avg_rssi);

	/*
	 * Check if link tuning is supported by the hardware, some hardware
	 * do not support link tuning at all, while other devices can disable
	 * the feature from the EEPROM.
	 */
	if (rt2x00_has_cap_link_tuning(rt2x00dev))
		rt2x00dev->ops->lib->link_tuner(rt2x00dev, qual, link->count);

	/*
	 * Send a signal to the led to update the led signal strength.
	 */
	rt2x00leds_led_quality(rt2x00dev, qual->rssi);

	/*
	 * Evaluate antenna setup, make this the last step when
	 * rt2x00lib_antenna_diversity made changes the quality
	 * statistics will be reset.
	 */
	if (rt2x00lib_antenna_diversity(rt2x00dev))
		rt2x00link_reset_qual(rt2x00dev);
}

static void rt2x00link_tuner(struct work_struct *work)
{
	struct rt2x00_dev *rt2x00dev =
	    container_of(work, struct rt2x00_dev, link.work.work);
	struct link *link = &rt2x00dev->link;

	/*
	 * When the radio is shutting down we should
	 * immediately cease all link tuning.
	 */
	if (!test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags) ||
	    test_bit(DEVICE_STATE_SCANNING, &rt2x00dev->flags))
		return;

	/* Do not race with rt2x00mac_config(). */
	mutex_lock(&rt2x00dev->conf_mutex);

	if (rt2x00dev->intf_sta_count)
		rt2x00link_tuner_sta(rt2x00dev, link);

	if (rt2x00dev->ops->lib->gain_calibration &&
	    (link->count % (AGC_SECONDS / LINK_TUNE_SECONDS)) == 0)
		rt2x00dev->ops->lib->gain_calibration(rt2x00dev);

	if (rt2x00dev->ops->lib->vco_calibration &&
	    rt2x00_has_cap_vco_recalibration(rt2x00dev) &&
	    (link->count % (VCO_SECONDS / LINK_TUNE_SECONDS)) == 0)
		rt2x00dev->ops->lib->vco_calibration(rt2x00dev);

	mutex_unlock(&rt2x00dev->conf_mutex);

	/*
	 * Increase tuner counter, and reschedule the next link tuner run.
	 */
	link->count++;

	if (test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags))
		ieee80211_queue_delayed_work(rt2x00dev->hw,
					     &link->work, LINK_TUNE_INTERVAL);
}

void rt2x00link_start_watchdog(struct rt2x00_dev *rt2x00dev)
{
	struct link *link = &rt2x00dev->link;

	if (test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags) &&
	    rt2x00dev->ops->lib->watchdog && !link->watchdog_disabled)
		ieee80211_queue_delayed_work(rt2x00dev->hw,
					     &link->watchdog_work,
					     link->watchdog_interval);
}

void rt2x00link_stop_watchdog(struct rt2x00_dev *rt2x00dev)
{
	cancel_delayed_work_sync(&rt2x00dev->link.watchdog_work);
}

static void rt2x00link_watchdog(struct work_struct *work)
{
	struct rt2x00_dev *rt2x00dev =
	    container_of(work, struct rt2x00_dev, link.watchdog_work.work);
	struct link *link = &rt2x00dev->link;

	/*
	 * When the radio is shutting down we should
	 * immediately cease the watchdog monitoring.
	 */
	if (!test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		return;

	rt2x00dev->ops->lib->watchdog(rt2x00dev);

	if (test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags))
		ieee80211_queue_delayed_work(rt2x00dev->hw,
					     &link->watchdog_work,
					     link->watchdog_interval);
}

void rt2x00link_register(struct rt2x00_dev *rt2x00dev)
{
	struct link *link = &rt2x00dev->link;

	INIT_DELAYED_WORK(&link->work, rt2x00link_tuner);
	INIT_DELAYED_WORK(&link->watchdog_work, rt2x00link_watchdog);

	if (link->watchdog_interval == 0)
		link->watchdog_interval = WATCHDOG_INTERVAL;
}
