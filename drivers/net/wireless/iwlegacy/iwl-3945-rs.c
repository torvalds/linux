/******************************************************************************
 *
 * Copyright(c) 2005 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/wireless.h>
#include <net/mac80211.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>

#include <linux/workqueue.h>

#include "iwl-commands.h"
#include "iwl-3945.h"
#include "iwl-sta.h"

#define RS_NAME "iwl-3945-rs"

static s32 iwl3945_expected_tpt_g[IWL_RATE_COUNT_3945] = {
	7, 13, 35, 58, 0, 0, 76, 104, 130, 168, 191, 202
};

static s32 iwl3945_expected_tpt_g_prot[IWL_RATE_COUNT_3945] = {
	7, 13, 35, 58, 0, 0, 0, 80, 93, 113, 123, 125
};

static s32 iwl3945_expected_tpt_a[IWL_RATE_COUNT_3945] = {
	0, 0, 0, 0, 40, 57, 72, 98, 121, 154, 177, 186
};

static s32 iwl3945_expected_tpt_b[IWL_RATE_COUNT_3945] = {
	7, 13, 35, 58, 0, 0, 0, 0, 0, 0, 0, 0
};

struct iwl3945_tpt_entry {
	s8 min_rssi;
	u8 index;
};

static struct iwl3945_tpt_entry iwl3945_tpt_table_a[] = {
	{-60, IWL_RATE_54M_INDEX},
	{-64, IWL_RATE_48M_INDEX},
	{-72, IWL_RATE_36M_INDEX},
	{-80, IWL_RATE_24M_INDEX},
	{-84, IWL_RATE_18M_INDEX},
	{-85, IWL_RATE_12M_INDEX},
	{-87, IWL_RATE_9M_INDEX},
	{-89, IWL_RATE_6M_INDEX}
};

static struct iwl3945_tpt_entry iwl3945_tpt_table_g[] = {
	{-60, IWL_RATE_54M_INDEX},
	{-64, IWL_RATE_48M_INDEX},
	{-68, IWL_RATE_36M_INDEX},
	{-80, IWL_RATE_24M_INDEX},
	{-84, IWL_RATE_18M_INDEX},
	{-85, IWL_RATE_12M_INDEX},
	{-86, IWL_RATE_11M_INDEX},
	{-88, IWL_RATE_5M_INDEX},
	{-90, IWL_RATE_2M_INDEX},
	{-92, IWL_RATE_1M_INDEX}
};

#define IWL_RATE_MAX_WINDOW          62
#define IWL_RATE_FLUSH		(3*HZ)
#define IWL_RATE_WIN_FLUSH       (HZ/2)
#define IWL39_RATE_HIGH_TH          11520
#define IWL_SUCCESS_UP_TH	   8960
#define IWL_SUCCESS_DOWN_TH	  10880
#define IWL_RATE_MIN_FAILURE_TH       6
#define IWL_RATE_MIN_SUCCESS_TH       8
#define IWL_RATE_DECREASE_TH       1920
#define IWL_RATE_RETRY_TH	     15

static u8 iwl3945_get_rate_index_by_rssi(s32 rssi, enum ieee80211_band band)
{
	u32 index = 0;
	u32 table_size = 0;
	struct iwl3945_tpt_entry *tpt_table = NULL;

	if ((rssi < IWL_MIN_RSSI_VAL) || (rssi > IWL_MAX_RSSI_VAL))
		rssi = IWL_MIN_RSSI_VAL;

	switch (band) {
	case IEEE80211_BAND_2GHZ:
		tpt_table = iwl3945_tpt_table_g;
		table_size = ARRAY_SIZE(iwl3945_tpt_table_g);
		break;

	case IEEE80211_BAND_5GHZ:
		tpt_table = iwl3945_tpt_table_a;
		table_size = ARRAY_SIZE(iwl3945_tpt_table_a);
		break;

	default:
		BUG();
		break;
	}

	while ((index < table_size) && (rssi < tpt_table[index].min_rssi))
		index++;

	index = min(index, (table_size - 1));

	return tpt_table[index].index;
}

static void iwl3945_clear_window(struct iwl3945_rate_scale_data *window)
{
	window->data = 0;
	window->success_counter = 0;
	window->success_ratio = -1;
	window->counter = 0;
	window->average_tpt = IWL_INVALID_VALUE;
	window->stamp = 0;
}

/**
 * iwl3945_rate_scale_flush_windows - flush out the rate scale windows
 *
 * Returns the number of windows that have gathered data but were
 * not flushed.  If there were any that were not flushed, then
 * reschedule the rate flushing routine.
 */
static int iwl3945_rate_scale_flush_windows(struct iwl3945_rs_sta *rs_sta)
{
	int unflushed = 0;
	int i;
	unsigned long flags;
	struct iwl_priv *priv __maybe_unused = rs_sta->priv;

	/*
	 * For each rate, if we have collected data on that rate
	 * and it has been more than IWL_RATE_WIN_FLUSH
	 * since we flushed, clear out the gathered statistics
	 */
	for (i = 0; i < IWL_RATE_COUNT_3945; i++) {
		if (!rs_sta->win[i].counter)
			continue;

		spin_lock_irqsave(&rs_sta->lock, flags);
		if (time_after(jiffies, rs_sta->win[i].stamp +
			       IWL_RATE_WIN_FLUSH)) {
			IWL_DEBUG_RATE(priv, "flushing %d samples of rate "
				       "index %d\n",
				       rs_sta->win[i].counter, i);
			iwl3945_clear_window(&rs_sta->win[i]);
		} else
			unflushed++;
		spin_unlock_irqrestore(&rs_sta->lock, flags);
	}

	return unflushed;
}

#define IWL_RATE_FLUSH_MAX              5000	/* msec */
#define IWL_RATE_FLUSH_MIN              50	/* msec */
#define IWL_AVERAGE_PACKETS             1500

static void iwl3945_bg_rate_scale_flush(unsigned long data)
{
	struct iwl3945_rs_sta *rs_sta = (void *)data;
	struct iwl_priv *priv __maybe_unused = rs_sta->priv;
	int unflushed = 0;
	unsigned long flags;
	u32 packet_count, duration, pps;

	IWL_DEBUG_RATE(priv, "enter\n");

	unflushed = iwl3945_rate_scale_flush_windows(rs_sta);

	spin_lock_irqsave(&rs_sta->lock, flags);

	/* Number of packets Rx'd since last time this timer ran */
	packet_count = (rs_sta->tx_packets - rs_sta->last_tx_packets) + 1;

	rs_sta->last_tx_packets = rs_sta->tx_packets + 1;

	if (unflushed) {
		duration =
		    jiffies_to_msecs(jiffies - rs_sta->last_partial_flush);

		IWL_DEBUG_RATE(priv, "Tx'd %d packets in %dms\n",
			       packet_count, duration);

		/* Determine packets per second */
		if (duration)
			pps = (packet_count * 1000) / duration;
		else
			pps = 0;

		if (pps) {
			duration = (IWL_AVERAGE_PACKETS * 1000) / pps;
			if (duration < IWL_RATE_FLUSH_MIN)
				duration = IWL_RATE_FLUSH_MIN;
			else if (duration > IWL_RATE_FLUSH_MAX)
				duration = IWL_RATE_FLUSH_MAX;
		} else
			duration = IWL_RATE_FLUSH_MAX;

		rs_sta->flush_time = msecs_to_jiffies(duration);

		IWL_DEBUG_RATE(priv, "new flush period: %d msec ave %d\n",
			       duration, packet_count);

		mod_timer(&rs_sta->rate_scale_flush, jiffies +
			  rs_sta->flush_time);

		rs_sta->last_partial_flush = jiffies;
	} else {
		rs_sta->flush_time = IWL_RATE_FLUSH;
		rs_sta->flush_pending = 0;
	}
	/* If there weren't any unflushed entries, we don't schedule the timer
	 * to run again */

	rs_sta->last_flush = jiffies;

	spin_unlock_irqrestore(&rs_sta->lock, flags);

	IWL_DEBUG_RATE(priv, "leave\n");
}

/**
 * iwl3945_collect_tx_data - Update the success/failure sliding window
 *
 * We keep a sliding window of the last 64 packets transmitted
 * at this rate.  window->data contains the bitmask of successful
 * packets.
 */
static void iwl3945_collect_tx_data(struct iwl3945_rs_sta *rs_sta,
				struct iwl3945_rate_scale_data *window,
				int success, int retries, int index)
{
	unsigned long flags;
	s32 fail_count;
	struct iwl_priv *priv __maybe_unused = rs_sta->priv;

	if (!retries) {
		IWL_DEBUG_RATE(priv, "leave: retries == 0 -- should be at least 1\n");
		return;
	}

	spin_lock_irqsave(&rs_sta->lock, flags);

	/*
	 * Keep track of only the latest 62 tx frame attempts in this rate's
	 * history window; anything older isn't really relevant any more.
	 * If we have filled up the sliding window, drop the oldest attempt;
	 * if the oldest attempt (highest bit in bitmap) shows "success",
	 * subtract "1" from the success counter (this is the main reason
	 * we keep these bitmaps!).
	 * */
	while (retries > 0) {
		if (window->counter >= IWL_RATE_MAX_WINDOW) {

			/* remove earliest */
			window->counter = IWL_RATE_MAX_WINDOW - 1;

			if (window->data & (1ULL << (IWL_RATE_MAX_WINDOW - 1))) {
				window->data &= ~(1ULL << (IWL_RATE_MAX_WINDOW - 1));
				window->success_counter--;
			}
		}

		/* Increment frames-attempted counter */
		window->counter++;

		/* Shift bitmap by one frame (throw away oldest history),
		 * OR in "1", and increment "success" if this
		 * frame was successful. */
		window->data <<= 1;
		if (success > 0) {
			window->success_counter++;
			window->data |= 0x1;
			success--;
		}

		retries--;
	}

	/* Calculate current success ratio, avoid divide-by-0! */
	if (window->counter > 0)
		window->success_ratio = 128 * (100 * window->success_counter)
					/ window->counter;
	else
		window->success_ratio = IWL_INVALID_VALUE;

	fail_count = window->counter - window->success_counter;

	/* Calculate average throughput, if we have enough history. */
	if ((fail_count >= IWL_RATE_MIN_FAILURE_TH) ||
	    (window->success_counter >= IWL_RATE_MIN_SUCCESS_TH))
		window->average_tpt = ((window->success_ratio *
				rs_sta->expected_tpt[index] + 64) / 128);
	else
		window->average_tpt = IWL_INVALID_VALUE;

	/* Tag this window as having been updated */
	window->stamp = jiffies;

	spin_unlock_irqrestore(&rs_sta->lock, flags);

}

/*
 * Called after adding a new station to initialize rate scaling
 */
void iwl3945_rs_rate_init(struct iwl_priv *priv, struct ieee80211_sta *sta, u8 sta_id)
{
	struct ieee80211_hw *hw = priv->hw;
	struct ieee80211_conf *conf = &priv->hw->conf;
	struct iwl3945_sta_priv *psta;
	struct iwl3945_rs_sta *rs_sta;
	struct ieee80211_supported_band *sband;
	int i;

	IWL_DEBUG_INFO(priv, "enter\n");
	if (sta_id == priv->contexts[IWL_RXON_CTX_BSS].bcast_sta_id)
		goto out;

	psta = (struct iwl3945_sta_priv *) sta->drv_priv;
	rs_sta = &psta->rs_sta;
	sband = hw->wiphy->bands[conf->channel->band];

	rs_sta->priv = priv;

	rs_sta->start_rate = IWL_RATE_INVALID;

	/* default to just 802.11b */
	rs_sta->expected_tpt = iwl3945_expected_tpt_b;

	rs_sta->last_partial_flush = jiffies;
	rs_sta->last_flush = jiffies;
	rs_sta->flush_time = IWL_RATE_FLUSH;
	rs_sta->last_tx_packets = 0;

	rs_sta->rate_scale_flush.data = (unsigned long)rs_sta;
	rs_sta->rate_scale_flush.function = iwl3945_bg_rate_scale_flush;

	for (i = 0; i < IWL_RATE_COUNT_3945; i++)
		iwl3945_clear_window(&rs_sta->win[i]);

	/* TODO: what is a good starting rate for STA? About middle? Maybe not
	 * the lowest or the highest rate.. Could consider using RSSI from
	 * previous packets? Need to have IEEE 802.1X auth succeed immediately
	 * after assoc.. */

	for (i = sband->n_bitrates - 1; i >= 0; i--) {
		if (sta->supp_rates[sband->band] & (1 << i)) {
			rs_sta->last_txrate_idx = i;
			break;
		}
	}

	priv->_3945.sta_supp_rates = sta->supp_rates[sband->band];
	/* For 5 GHz band it start at IWL_FIRST_OFDM_RATE */
	if (sband->band == IEEE80211_BAND_5GHZ) {
		rs_sta->last_txrate_idx += IWL_FIRST_OFDM_RATE;
		priv->_3945.sta_supp_rates = priv->_3945.sta_supp_rates <<
						IWL_FIRST_OFDM_RATE;
	}

out:
	priv->stations[sta_id].used &= ~IWL_STA_UCODE_INPROGRESS;

	IWL_DEBUG_INFO(priv, "leave\n");
}

static void *iwl3945_rs_alloc(struct ieee80211_hw *hw, struct dentry *debugfsdir)
{
	return hw->priv;
}

/* rate scale requires free function to be implemented */
static void iwl3945_rs_free(void *priv)
{
	return;
}

static void *iwl3945_rs_alloc_sta(void *iwl_priv, struct ieee80211_sta *sta, gfp_t gfp)
{
	struct iwl3945_rs_sta *rs_sta;
	struct iwl3945_sta_priv *psta = (void *) sta->drv_priv;
	struct iwl_priv *priv __maybe_unused = iwl_priv;

	IWL_DEBUG_RATE(priv, "enter\n");

	rs_sta = &psta->rs_sta;

	spin_lock_init(&rs_sta->lock);
	init_timer(&rs_sta->rate_scale_flush);

	IWL_DEBUG_RATE(priv, "leave\n");

	return rs_sta;
}

static void iwl3945_rs_free_sta(void *iwl_priv, struct ieee80211_sta *sta,
			void *priv_sta)
{
	struct iwl3945_rs_sta *rs_sta = priv_sta;

	/*
	 * Be careful not to use any members of iwl3945_rs_sta (like trying
	 * to use iwl_priv to print out debugging) since it may not be fully
	 * initialized at this point.
	 */
	del_timer_sync(&rs_sta->rate_scale_flush);
}


/**
 * iwl3945_rs_tx_status - Update rate control values based on Tx results
 *
 * NOTE: Uses iwl_priv->retry_rate for the # of retries attempted by
 * the hardware for each rate.
 */
static void iwl3945_rs_tx_status(void *priv_rate, struct ieee80211_supported_band *sband,
			 struct ieee80211_sta *sta, void *priv_sta,
			 struct sk_buff *skb)
{
	s8 retries = 0, current_count;
	int scale_rate_index, first_index, last_index;
	unsigned long flags;
	struct iwl_priv *priv = (struct iwl_priv *)priv_rate;
	struct iwl3945_rs_sta *rs_sta = priv_sta;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	IWL_DEBUG_RATE(priv, "enter\n");

	retries = info->status.rates[0].count;
	/* Sanity Check for retries */
	if (retries > IWL_RATE_RETRY_TH)
		retries = IWL_RATE_RETRY_TH;

	first_index = sband->bitrates[info->status.rates[0].idx].hw_value;
	if ((first_index < 0) || (first_index >= IWL_RATE_COUNT_3945)) {
		IWL_DEBUG_RATE(priv, "leave: Rate out of bounds: %d\n", first_index);
		return;
	}

	if (!priv_sta) {
		IWL_DEBUG_RATE(priv, "leave: No STA priv data to update!\n");
		return;
	}

	/* Treat uninitialized rate scaling data same as non-existing. */
	if (!rs_sta->priv) {
		IWL_DEBUG_RATE(priv, "leave: STA priv data uninitialized!\n");
		return;
	}


	rs_sta->tx_packets++;

	scale_rate_index = first_index;
	last_index = first_index;

	/*
	 * Update the window for each rate.  We determine which rates
	 * were Tx'd based on the total number of retries vs. the number
	 * of retries configured for each rate -- currently set to the
	 * priv value 'retry_rate' vs. rate specific
	 *
	 * On exit from this while loop last_index indicates the rate
	 * at which the frame was finally transmitted (or failed if no
	 * ACK)
	 */
	while (retries > 1) {
		if ((retries - 1) < priv->retry_rate) {
			current_count = (retries - 1);
			last_index = scale_rate_index;
		} else {
			current_count = priv->retry_rate;
			last_index = iwl3945_rs_next_rate(priv,
							 scale_rate_index);
		}

		/* Update this rate accounting for as many retries
		 * as was used for it (per current_count) */
		iwl3945_collect_tx_data(rs_sta,
				    &rs_sta->win[scale_rate_index],
				    0, current_count, scale_rate_index);
		IWL_DEBUG_RATE(priv, "Update rate %d for %d retries.\n",
			       scale_rate_index, current_count);

		retries -= current_count;

		scale_rate_index = last_index;
	}


	/* Update the last index window with success/failure based on ACK */
	IWL_DEBUG_RATE(priv, "Update rate %d with %s.\n",
		       last_index,
		       (info->flags & IEEE80211_TX_STAT_ACK) ?
		       "success" : "failure");
	iwl3945_collect_tx_data(rs_sta,
			    &rs_sta->win[last_index],
			    info->flags & IEEE80211_TX_STAT_ACK, 1, last_index);

	/* We updated the rate scale window -- if its been more than
	 * flush_time since the last run, schedule the flush
	 * again */
	spin_lock_irqsave(&rs_sta->lock, flags);

	if (!rs_sta->flush_pending &&
	    time_after(jiffies, rs_sta->last_flush +
		       rs_sta->flush_time)) {

		rs_sta->last_partial_flush = jiffies;
		rs_sta->flush_pending = 1;
		mod_timer(&rs_sta->rate_scale_flush,
			  jiffies + rs_sta->flush_time);
	}

	spin_unlock_irqrestore(&rs_sta->lock, flags);

	IWL_DEBUG_RATE(priv, "leave\n");
}

static u16 iwl3945_get_adjacent_rate(struct iwl3945_rs_sta *rs_sta,
				 u8 index, u16 rate_mask, enum ieee80211_band band)
{
	u8 high = IWL_RATE_INVALID;
	u8 low = IWL_RATE_INVALID;
	struct iwl_priv *priv __maybe_unused = rs_sta->priv;

	/* 802.11A walks to the next literal adjacent rate in
	 * the rate table */
	if (unlikely(band == IEEE80211_BAND_5GHZ)) {
		int i;
		u32 mask;

		/* Find the previous rate that is in the rate mask */
		i = index - 1;
		for (mask = (1 << i); i >= 0; i--, mask >>= 1) {
			if (rate_mask & mask) {
				low = i;
				break;
			}
		}

		/* Find the next rate that is in the rate mask */
		i = index + 1;
		for (mask = (1 << i); i < IWL_RATE_COUNT_3945;
		     i++, mask <<= 1) {
			if (rate_mask & mask) {
				high = i;
				break;
			}
		}

		return (high << 8) | low;
	}

	low = index;
	while (low != IWL_RATE_INVALID) {
		if (rs_sta->tgg)
			low = iwl3945_rates[low].prev_rs_tgg;
		else
			low = iwl3945_rates[low].prev_rs;
		if (low == IWL_RATE_INVALID)
			break;
		if (rate_mask & (1 << low))
			break;
		IWL_DEBUG_RATE(priv, "Skipping masked lower rate: %d\n", low);
	}

	high = index;
	while (high != IWL_RATE_INVALID) {
		if (rs_sta->tgg)
			high = iwl3945_rates[high].next_rs_tgg;
		else
			high = iwl3945_rates[high].next_rs;
		if (high == IWL_RATE_INVALID)
			break;
		if (rate_mask & (1 << high))
			break;
		IWL_DEBUG_RATE(priv, "Skipping masked higher rate: %d\n", high);
	}

	return (high << 8) | low;
}

/**
 * iwl3945_rs_get_rate - find the rate for the requested packet
 *
 * Returns the ieee80211_rate structure allocated by the driver.
 *
 * The rate control algorithm has no internal mapping between hw_mode's
 * rate ordering and the rate ordering used by the rate control algorithm.
 *
 * The rate control algorithm uses a single table of rates that goes across
 * the entire A/B/G spectrum vs. being limited to just one particular
 * hw_mode.
 *
 * As such, we can't convert the index obtained below into the hw_mode's
 * rate table and must reference the driver allocated rate table
 *
 */
static void iwl3945_rs_get_rate(void *priv_r, struct ieee80211_sta *sta,
			void *priv_sta,	struct ieee80211_tx_rate_control *txrc)
{
	struct ieee80211_supported_band *sband = txrc->sband;
	struct sk_buff *skb = txrc->skb;
	u8 low = IWL_RATE_INVALID;
	u8 high = IWL_RATE_INVALID;
	u16 high_low;
	int index;
	struct iwl3945_rs_sta *rs_sta = priv_sta;
	struct iwl3945_rate_scale_data *window = NULL;
	int current_tpt = IWL_INVALID_VALUE;
	int low_tpt = IWL_INVALID_VALUE;
	int high_tpt = IWL_INVALID_VALUE;
	u32 fail_count;
	s8 scale_action = 0;
	unsigned long flags;
	u16 rate_mask;
	s8 max_rate_idx = -1;
	struct iwl_priv *priv __maybe_unused = (struct iwl_priv *)priv_r;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	IWL_DEBUG_RATE(priv, "enter\n");

	/* Treat uninitialized rate scaling data same as non-existing. */
	if (rs_sta && !rs_sta->priv) {
		IWL_DEBUG_RATE(priv, "Rate scaling information not initialized yet.\n");
		priv_sta = NULL;
	}

	if (rate_control_send_low(sta, priv_sta, txrc))
		return;

	rate_mask = sta->supp_rates[sband->band];

	/* get user max rate if set */
	max_rate_idx = txrc->max_rate_idx;
	if ((sband->band == IEEE80211_BAND_5GHZ) && (max_rate_idx != -1))
		max_rate_idx += IWL_FIRST_OFDM_RATE;
	if ((max_rate_idx < 0) || (max_rate_idx >= IWL_RATE_COUNT))
		max_rate_idx = -1;

	index = min(rs_sta->last_txrate_idx & 0xffff, IWL_RATE_COUNT_3945 - 1);

	if (sband->band == IEEE80211_BAND_5GHZ)
		rate_mask = rate_mask << IWL_FIRST_OFDM_RATE;

	spin_lock_irqsave(&rs_sta->lock, flags);

	/* for recent assoc, choose best rate regarding
	 * to rssi value
	 */
	if (rs_sta->start_rate != IWL_RATE_INVALID) {
		if (rs_sta->start_rate < index &&
		   (rate_mask & (1 << rs_sta->start_rate)))
			index = rs_sta->start_rate;
		rs_sta->start_rate = IWL_RATE_INVALID;
	}

	/* force user max rate if set by user */
	if ((max_rate_idx != -1) && (max_rate_idx < index)) {
		if (rate_mask & (1 << max_rate_idx))
			index = max_rate_idx;
	}

	window = &(rs_sta->win[index]);

	fail_count = window->counter - window->success_counter;

	if (((fail_count < IWL_RATE_MIN_FAILURE_TH) &&
	     (window->success_counter < IWL_RATE_MIN_SUCCESS_TH))) {
		spin_unlock_irqrestore(&rs_sta->lock, flags);

		IWL_DEBUG_RATE(priv, "Invalid average_tpt on rate %d: "
			       "counter: %d, success_counter: %d, "
			       "expected_tpt is %sNULL\n",
			       index,
			       window->counter,
			       window->success_counter,
			       rs_sta->expected_tpt ? "not " : "");

	   /* Can't calculate this yet; not enough history */
		window->average_tpt = IWL_INVALID_VALUE;
		goto out;

	}

	current_tpt = window->average_tpt;

	high_low = iwl3945_get_adjacent_rate(rs_sta, index, rate_mask,
					     sband->band);
	low = high_low & 0xff;
	high = (high_low >> 8) & 0xff;

	/* If user set max rate, dont allow higher than user constrain */
	if ((max_rate_idx != -1) && (max_rate_idx < high))
		high = IWL_RATE_INVALID;

	/* Collect Measured throughputs of adjacent rates */
	if (low != IWL_RATE_INVALID)
		low_tpt = rs_sta->win[low].average_tpt;

	if (high != IWL_RATE_INVALID)
		high_tpt = rs_sta->win[high].average_tpt;

	spin_unlock_irqrestore(&rs_sta->lock, flags);

	scale_action = 0;

	/* Low success ratio , need to drop the rate */
	if ((window->success_ratio < IWL_RATE_DECREASE_TH) || !current_tpt) {
		IWL_DEBUG_RATE(priv, "decrease rate because of low success_ratio\n");
		scale_action = -1;
	/* No throughput measured yet for adjacent rates,
	 * try increase */
	} else if ((low_tpt == IWL_INVALID_VALUE) &&
		   (high_tpt == IWL_INVALID_VALUE)) {

		if (high != IWL_RATE_INVALID && window->success_ratio >= IWL_RATE_INCREASE_TH)
			scale_action = 1;
		else if (low != IWL_RATE_INVALID)
			scale_action = 0;

	/* Both adjacent throughputs are measured, but neither one has
	 * better throughput; we're using the best rate, don't change
	 * it! */
	} else if ((low_tpt != IWL_INVALID_VALUE) &&
		 (high_tpt != IWL_INVALID_VALUE) &&
		 (low_tpt < current_tpt) && (high_tpt < current_tpt)) {

		IWL_DEBUG_RATE(priv, "No action -- low [%d] & high [%d] < "
			       "current_tpt [%d]\n",
			       low_tpt, high_tpt, current_tpt);
		scale_action = 0;

	/* At least one of the rates has better throughput */
	} else {
		if (high_tpt != IWL_INVALID_VALUE) {

			/* High rate has better throughput, Increase
			 * rate */
			if (high_tpt > current_tpt &&
				window->success_ratio >= IWL_RATE_INCREASE_TH)
				scale_action = 1;
			else {
				IWL_DEBUG_RATE(priv,
				    "decrease rate because of high tpt\n");
				scale_action = 0;
			}
		} else if (low_tpt != IWL_INVALID_VALUE) {
			if (low_tpt > current_tpt) {
				IWL_DEBUG_RATE(priv,
				    "decrease rate because of low tpt\n");
				scale_action = -1;
			} else if (window->success_ratio >= IWL_RATE_INCREASE_TH) {
				/* Lower rate has better
				 * throughput,decrease rate */
				scale_action = 1;
			}
		}
	}

	/* Sanity check; asked for decrease, but success rate or throughput
	 * has been good at old rate.  Don't change it. */
	if ((scale_action == -1) && (low != IWL_RATE_INVALID) &&
		    ((window->success_ratio > IWL_RATE_HIGH_TH) ||
		     (current_tpt > (100 * rs_sta->expected_tpt[low]))))
		scale_action = 0;

	switch (scale_action) {
	case -1:

		/* Decrese rate */
		if (low != IWL_RATE_INVALID)
			index = low;
		break;

	case 1:
		/* Increase rate */
		if (high != IWL_RATE_INVALID)
			index = high;

		break;

	case 0:
	default:
		/* No change */
		break;
	}

	IWL_DEBUG_RATE(priv, "Selected %d (action %d) - low %d high %d\n",
		       index, scale_action, low, high);

 out:

	rs_sta->last_txrate_idx = index;
	if (sband->band == IEEE80211_BAND_5GHZ)
		info->control.rates[0].idx = rs_sta->last_txrate_idx -
				IWL_FIRST_OFDM_RATE;
	else
		info->control.rates[0].idx = rs_sta->last_txrate_idx;

	IWL_DEBUG_RATE(priv, "leave: %d\n", index);
}

#ifdef CONFIG_MAC80211_DEBUGFS
static int iwl3945_open_file_generic(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t iwl3945_sta_dbgfs_stats_table_read(struct file *file,
						  char __user *user_buf,
						  size_t count, loff_t *ppos)
{
	char *buff;
	int desc = 0;
	int j;
	ssize_t ret;
	struct iwl3945_rs_sta *lq_sta = file->private_data;

	buff = kmalloc(1024, GFP_KERNEL);
	if (!buff)
		return -ENOMEM;

	desc += sprintf(buff + desc, "tx packets=%d last rate index=%d\n"
			"rate=0x%X flush time %d\n",
			lq_sta->tx_packets,
			lq_sta->last_txrate_idx,
			lq_sta->start_rate, jiffies_to_msecs(lq_sta->flush_time));
	for (j = 0; j < IWL_RATE_COUNT_3945; j++) {
		desc += sprintf(buff+desc,
				"counter=%d success=%d %%=%d\n",
				lq_sta->win[j].counter,
				lq_sta->win[j].success_counter,
				lq_sta->win[j].success_ratio);
	}
	ret = simple_read_from_buffer(user_buf, count, ppos, buff, desc);
	kfree(buff);
	return ret;
}

static const struct file_operations rs_sta_dbgfs_stats_table_ops = {
	.read = iwl3945_sta_dbgfs_stats_table_read,
	.open = iwl3945_open_file_generic,
	.llseek = default_llseek,
};

static void iwl3945_add_debugfs(void *priv, void *priv_sta,
				struct dentry *dir)
{
	struct iwl3945_rs_sta *lq_sta = priv_sta;

	lq_sta->rs_sta_dbgfs_stats_table_file =
		debugfs_create_file("rate_stats_table", 0600, dir,
		lq_sta, &rs_sta_dbgfs_stats_table_ops);

}

static void iwl3945_remove_debugfs(void *priv, void *priv_sta)
{
	struct iwl3945_rs_sta *lq_sta = priv_sta;
	debugfs_remove(lq_sta->rs_sta_dbgfs_stats_table_file);
}
#endif

/*
 * Initialization of rate scaling information is done by driver after
 * the station is added. Since mac80211 calls this function before a
 * station is added we ignore it.
 */
static void iwl3945_rs_rate_init_stub(void *priv_r,
				struct ieee80211_supported_band *sband,
			      struct ieee80211_sta *sta, void *priv_sta)
{
}

static struct rate_control_ops rs_ops = {
	.module = NULL,
	.name = RS_NAME,
	.tx_status = iwl3945_rs_tx_status,
	.get_rate = iwl3945_rs_get_rate,
	.rate_init = iwl3945_rs_rate_init_stub,
	.alloc = iwl3945_rs_alloc,
	.free = iwl3945_rs_free,
	.alloc_sta = iwl3945_rs_alloc_sta,
	.free_sta = iwl3945_rs_free_sta,
#ifdef CONFIG_MAC80211_DEBUGFS
	.add_sta_debugfs = iwl3945_add_debugfs,
	.remove_sta_debugfs = iwl3945_remove_debugfs,
#endif

};
void iwl3945_rate_scale_init(struct ieee80211_hw *hw, s32 sta_id)
{
	struct iwl_priv *priv = hw->priv;
	s32 rssi = 0;
	unsigned long flags;
	struct iwl3945_rs_sta *rs_sta;
	struct ieee80211_sta *sta;
	struct iwl3945_sta_priv *psta;

	IWL_DEBUG_RATE(priv, "enter\n");

	rcu_read_lock();

	sta = ieee80211_find_sta(priv->contexts[IWL_RXON_CTX_BSS].vif,
				 priv->stations[sta_id].sta.sta.addr);
	if (!sta) {
		IWL_DEBUG_RATE(priv, "Unable to find station to initialize rate scaling.\n");
		rcu_read_unlock();
		return;
	}

	psta = (void *) sta->drv_priv;
	rs_sta = &psta->rs_sta;

	spin_lock_irqsave(&rs_sta->lock, flags);

	rs_sta->tgg = 0;
	switch (priv->band) {
	case IEEE80211_BAND_2GHZ:
		/* TODO: this always does G, not a regression */
		if (priv->contexts[IWL_RXON_CTX_BSS].active.flags &
						RXON_FLG_TGG_PROTECT_MSK) {
			rs_sta->tgg = 1;
			rs_sta->expected_tpt = iwl3945_expected_tpt_g_prot;
		} else
			rs_sta->expected_tpt = iwl3945_expected_tpt_g;
		break;

	case IEEE80211_BAND_5GHZ:
		rs_sta->expected_tpt = iwl3945_expected_tpt_a;
		break;
	case IEEE80211_NUM_BANDS:
		BUG();
		break;
	}

	spin_unlock_irqrestore(&rs_sta->lock, flags);

	rssi = priv->_3945.last_rx_rssi;
	if (rssi == 0)
		rssi = IWL_MIN_RSSI_VAL;

	IWL_DEBUG_RATE(priv, "Network RSSI: %d\n", rssi);

	rs_sta->start_rate = iwl3945_get_rate_index_by_rssi(rssi, priv->band);

	IWL_DEBUG_RATE(priv, "leave: rssi %d assign rate index: "
		       "%d (plcp 0x%x)\n", rssi, rs_sta->start_rate,
		       iwl3945_rates[rs_sta->start_rate].plcp);
	rcu_read_unlock();
}

int iwl3945_rate_control_register(void)
{
	return ieee80211_rate_control_register(&rs_ops);
}

void iwl3945_rate_control_unregister(void)
{
	ieee80211_rate_control_unregister(&rs_ops);
}
