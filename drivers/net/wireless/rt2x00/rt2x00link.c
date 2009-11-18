/*
	Copyright (C) 2004 - 2009 rt2x00 SourceForge Project
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

/*
 * When no TX/RX percentage could be calculated due to lack of
 * frames on the air, we fallback to a percentage of 50%.
 * This will assure we will get at least get some decent value
 * when the link tuner starts.
 * The value will be dropped and overwritten with the correct (measured)
 * value anyway during the first run of the link tuner.
 */
#define DEFAULT_PERCENTAGE	50

/*
 * Small helper macro for percentage calculation
 * This is a very simple macro with the only catch that it will
 * produce a default value in case no total value was provided.
 */
#define PERCENTAGE(__value, __total) \
	( (__total) ? (((__value) * 100) / (__total)) : (DEFAULT_PERCENTAGE) )

/*
 * Helper struct and macro to work with moving/walking averages.
 * When adding a value to the average value the following calculation
 * is needed:
 *
 *        avg_rssi = ((avg_rssi * 7) + rssi) / 8;
 *
 * The advantage of this approach is that we only need 1 variable
 * to store the average in (No need for a count and a total).
 * But more importantly, normal average values will over time
 * move less and less towards newly added values this results
 * that with link tuning, the device can have a very good RSSI
 * for a few minutes but when the device is moved away from the AP
 * the average will not decrease fast enough to compensate.
 * The walking average compensates this and will move towards
 * the new values correctly allowing a effective link tuning,
 * the speed of the average moving towards other values depends
 * on the value for the number of samples. The higher the number
 * of samples, the slower the average will move.
 * We use two variables to keep track of the average value to
 * compensate for the rounding errors. This can be a significant
 * error (>5dBm) if the factor is too low.
 */
#define AVG_SAMPLES	8
#define AVG_FACTOR	1000
#define MOVING_AVERAGE(__avg, __val) \
({ \
	struct avg_val __new; \
	__new.avg_weight = \
	    (__avg).avg_weight  ? \
		((((__avg).avg_weight * ((AVG_SAMPLES) - 1)) + \
		  ((__val) * (AVG_FACTOR))) / \
		 (AVG_SAMPLES) ) : \
		((__val) * (AVG_FACTOR)); \
	__new.avg = __new.avg_weight / (AVG_FACTOR); \
	__new; \
})

/*
 * For calculating the Signal quality we have determined
 * the total number of success and failed RX and TX frames.
 * With the addition of the average RSSI value we can determine
 * the link quality using the following algorithm:
 *
 *         rssi_percentage = (avg_rssi * 100) / rssi_offset
 *         rx_percentage = (rx_success * 100) / rx_total
 *         tx_percentage = (tx_success * 100) / tx_total
 *         avg_signal = ((WEIGHT_RSSI * avg_rssi) +
 *                       (WEIGHT_TX * tx_percentage) +
 *                       (WEIGHT_RX * rx_percentage)) / 100
 *
 * This value should then be checked to not be greater then 100.
 * This means the values of WEIGHT_RSSI, WEIGHT_RX, WEIGHT_TX must
 * sum up to 100 as well.
 */
#define WEIGHT_RSSI	20
#define WEIGHT_RX	40
#define WEIGHT_TX	40

static int rt2x00link_antenna_get_link_rssi(struct rt2x00_dev *rt2x00dev)
{
	struct link_ant *ant = &rt2x00dev->link.ant;

	if (ant->rssi_ant.avg && rt2x00dev->link.qual.rx_success)
		return ant->rssi_ant.avg;
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
	rt2x00dev->link.ant.rssi_ant.avg = 0;
	rt2x00dev->link.ant.rssi_ant.avg_weight = 0;
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
	unsigned int flags = ant->flags;

	/*
	 * Determine if software diversity is enabled for
	 * either the TX or RX antenna (or both).
	 * Always perform this check since within the link
	 * tuner interval the configuration might have changed.
	 */
	flags &= ~ANTENNA_RX_DIVERSITY;
	flags &= ~ANTENNA_TX_DIVERSITY;

	if (rt2x00dev->default_ant.rx == ANTENNA_SW_DIVERSITY)
		flags |= ANTENNA_RX_DIVERSITY;
	if (rt2x00dev->default_ant.tx == ANTENNA_SW_DIVERSITY)
		flags |= ANTENNA_TX_DIVERSITY;

	if (!(ant->flags & ANTENNA_RX_DIVERSITY) &&
	    !(ant->flags & ANTENNA_TX_DIVERSITY)) {
		ant->flags = 0;
		return true;
	}

	/* Update flags */
	ant->flags = flags;

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
	 * Frame was received successfully since non-succesfull
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
	link->avg_rssi = MOVING_AVERAGE(link->avg_rssi, rxdesc->rssi);

	/*
	 * Update antenna RSSI
	 */
	ant->rssi_ant = MOVING_AVERAGE(ant->rssi_ant, rxdesc->rssi);
}

static void rt2x00link_precalculate_signal(struct rt2x00_dev *rt2x00dev)
{
	struct link *link = &rt2x00dev->link;
	struct link_qual *qual = &rt2x00dev->link.qual;

	link->rx_percentage =
	    PERCENTAGE(qual->rx_success, qual->rx_failed + qual->rx_success);
	link->tx_percentage =
	    PERCENTAGE(qual->tx_success, qual->tx_failed + qual->tx_success);
}

int rt2x00link_calculate_signal(struct rt2x00_dev *rt2x00dev, int rssi)
{
	struct link *link = &rt2x00dev->link;
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
	rssi_percentage = PERCENTAGE(rssi, rt2x00dev->rssi_offset);

	/*
	 * Add the individual percentages and use the WEIGHT
	 * defines to calculate the current link signal.
	 */
	signal = ((WEIGHT_RSSI * rssi_percentage) +
		  (WEIGHT_TX * link->tx_percentage) +
		  (WEIGHT_RX * link->rx_percentage)) / 100;

	return max_t(int, signal, 100);
}

void rt2x00link_start_tuner(struct rt2x00_dev *rt2x00dev)
{
	struct link *link = &rt2x00dev->link;

	/*
	 * Link tuning should only be performed when
	 * an active sta or master interface exists.
	 * Single monitor mode interfaces should never have
	 * work with link tuners.
	 */
	if (!rt2x00dev->intf_ap_count && !rt2x00dev->intf_sta_count)
		return;

	link->rx_percentage = DEFAULT_PERCENTAGE;
	link->tx_percentage = DEFAULT_PERCENTAGE;

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

static void rt2x00link_tuner(struct work_struct *work)
{
	struct rt2x00_dev *rt2x00dev =
	    container_of(work, struct rt2x00_dev, link.work.work);
	struct link *link = &rt2x00dev->link;
	struct link_qual *qual = &rt2x00dev->link.qual;

	/*
	 * When the radio is shutting down we should
	 * immediately cease all link tuning.
	 */
	if (!test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		return;

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
	if (!link->avg_rssi.avg || !qual->rx_success)
		qual->rssi = DEFAULT_RSSI;
	else
		qual->rssi = link->avg_rssi.avg;

	/*
	 * Only perform the link tuning when Link tuning
	 * has been enabled (This could have been disabled from the EEPROM).
	 */
	if (!test_bit(CONFIG_DISABLE_LINK_TUNING, &rt2x00dev->flags))
		rt2x00dev->ops->lib->link_tuner(rt2x00dev, qual, link->count);

	/*
	 * Precalculate a portion of the link signal which is
	 * in based on the tx/rx success/failure counters.
	 */
	rt2x00link_precalculate_signal(rt2x00dev);

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

	/*
	 * Increase tuner counter, and reschedule the next link tuner run.
	 */
	link->count++;

	if (test_bit(DEVICE_STATE_PRESENT, &rt2x00dev->flags))
		ieee80211_queue_delayed_work(rt2x00dev->hw,
					     &link->work, LINK_TUNE_INTERVAL);
}

void rt2x00link_register(struct rt2x00_dev *rt2x00dev)
{
	INIT_DELAYED_WORK(&rt2x00dev->link.work, rt2x00link_tuner);
}
