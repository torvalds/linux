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
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <net/mac80211.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>

#include <linux/workqueue.h>

#include "commands.h"
#include "3945.h"

#define RS_NAME "iwl-3945-rs"

static s32 il3945_expected_tpt_g[RATE_COUNT_3945] = {
	7, 13, 35, 58, 0, 0, 76, 104, 130, 168, 191, 202
};

static s32 il3945_expected_tpt_g_prot[RATE_COUNT_3945] = {
	7, 13, 35, 58, 0, 0, 0, 80, 93, 113, 123, 125
};

static s32 il3945_expected_tpt_a[RATE_COUNT_3945] = {
	0, 0, 0, 0, 40, 57, 72, 98, 121, 154, 177, 186
};

static s32 il3945_expected_tpt_b[RATE_COUNT_3945] = {
	7, 13, 35, 58, 0, 0, 0, 0, 0, 0, 0, 0
};

struct il3945_tpt_entry {
	s8 min_rssi;
	u8 idx;
};

static struct il3945_tpt_entry il3945_tpt_table_a[] = {
	{-60, RATE_54M_IDX},
	{-64, RATE_48M_IDX},
	{-72, RATE_36M_IDX},
	{-80, RATE_24M_IDX},
	{-84, RATE_18M_IDX},
	{-85, RATE_12M_IDX},
	{-87, RATE_9M_IDX},
	{-89, RATE_6M_IDX}
};

static struct il3945_tpt_entry il3945_tpt_table_g[] = {
	{-60, RATE_54M_IDX},
	{-64, RATE_48M_IDX},
	{-68, RATE_36M_IDX},
	{-80, RATE_24M_IDX},
	{-84, RATE_18M_IDX},
	{-85, RATE_12M_IDX},
	{-86, RATE_11M_IDX},
	{-88, RATE_5M_IDX},
	{-90, RATE_2M_IDX},
	{-92, RATE_1M_IDX}
};

#define RATE_MAX_WINDOW		62
#define RATE_FLUSH		(3*HZ)
#define RATE_WIN_FLUSH		(HZ/2)
#define IL39_RATE_HIGH_TH	11520
#define IL_SUCCESS_UP_TH	8960
#define IL_SUCCESS_DOWN_TH	10880
#define RATE_MIN_FAILURE_TH	6
#define RATE_MIN_SUCCESS_TH	8
#define RATE_DECREASE_TH	1920
#define RATE_RETRY_TH		15

static u8
il3945_get_rate_idx_by_rssi(s32 rssi, enum nl80211_band band)
{
	u32 idx = 0;
	u32 table_size = 0;
	struct il3945_tpt_entry *tpt_table = NULL;

	if (rssi < IL_MIN_RSSI_VAL || rssi > IL_MAX_RSSI_VAL)
		rssi = IL_MIN_RSSI_VAL;

	switch (band) {
	case NL80211_BAND_2GHZ:
		tpt_table = il3945_tpt_table_g;
		table_size = ARRAY_SIZE(il3945_tpt_table_g);
		break;
	case NL80211_BAND_5GHZ:
		tpt_table = il3945_tpt_table_a;
		table_size = ARRAY_SIZE(il3945_tpt_table_a);
		break;
	default:
		BUG();
		break;
	}

	while (idx < table_size && rssi < tpt_table[idx].min_rssi)
		idx++;

	idx = min(idx, table_size - 1);

	return tpt_table[idx].idx;
}

static void
il3945_clear_win(struct il3945_rate_scale_data *win)
{
	win->data = 0;
	win->success_counter = 0;
	win->success_ratio = -1;
	win->counter = 0;
	win->average_tpt = IL_INVALID_VALUE;
	win->stamp = 0;
}

/**
 * il3945_rate_scale_flush_wins - flush out the rate scale wins
 *
 * Returns the number of wins that have gathered data but were
 * not flushed.  If there were any that were not flushed, then
 * reschedule the rate flushing routine.
 */
static int
il3945_rate_scale_flush_wins(struct il3945_rs_sta *rs_sta)
{
	int unflushed = 0;
	int i;
	unsigned long flags;
	struct il_priv *il __maybe_unused = rs_sta->il;

	/*
	 * For each rate, if we have collected data on that rate
	 * and it has been more than RATE_WIN_FLUSH
	 * since we flushed, clear out the gathered stats
	 */
	for (i = 0; i < RATE_COUNT_3945; i++) {
		if (!rs_sta->win[i].counter)
			continue;

		spin_lock_irqsave(&rs_sta->lock, flags);
		if (time_after(jiffies, rs_sta->win[i].stamp + RATE_WIN_FLUSH)) {
			D_RATE("flushing %d samples of rate " "idx %d\n",
			       rs_sta->win[i].counter, i);
			il3945_clear_win(&rs_sta->win[i]);
		} else
			unflushed++;
		spin_unlock_irqrestore(&rs_sta->lock, flags);
	}

	return unflushed;
}

#define RATE_FLUSH_MAX              5000	/* msec */
#define RATE_FLUSH_MIN              50	/* msec */
#define IL_AVERAGE_PACKETS             1500

static void
il3945_bg_rate_scale_flush(unsigned long data)
{
	struct il3945_rs_sta *rs_sta = (void *)data;
	struct il_priv *il __maybe_unused = rs_sta->il;
	int unflushed = 0;
	unsigned long flags;
	u32 packet_count, duration, pps;

	D_RATE("enter\n");

	unflushed = il3945_rate_scale_flush_wins(rs_sta);

	spin_lock_irqsave(&rs_sta->lock, flags);

	/* Number of packets Rx'd since last time this timer ran */
	packet_count = (rs_sta->tx_packets - rs_sta->last_tx_packets) + 1;

	rs_sta->last_tx_packets = rs_sta->tx_packets + 1;

	if (unflushed) {
		duration =
		    jiffies_to_msecs(jiffies - rs_sta->last_partial_flush);

		D_RATE("Tx'd %d packets in %dms\n", packet_count, duration);

		/* Determine packets per second */
		if (duration)
			pps = (packet_count * 1000) / duration;
		else
			pps = 0;

		if (pps) {
			duration = (IL_AVERAGE_PACKETS * 1000) / pps;
			if (duration < RATE_FLUSH_MIN)
				duration = RATE_FLUSH_MIN;
			else if (duration > RATE_FLUSH_MAX)
				duration = RATE_FLUSH_MAX;
		} else
			duration = RATE_FLUSH_MAX;

		rs_sta->flush_time = msecs_to_jiffies(duration);

		D_RATE("new flush period: %d msec ave %d\n", duration,
		       packet_count);

		mod_timer(&rs_sta->rate_scale_flush,
			  jiffies + rs_sta->flush_time);

		rs_sta->last_partial_flush = jiffies;
	} else {
		rs_sta->flush_time = RATE_FLUSH;
		rs_sta->flush_pending = 0;
	}
	/* If there weren't any unflushed entries, we don't schedule the timer
	 * to run again */

	rs_sta->last_flush = jiffies;

	spin_unlock_irqrestore(&rs_sta->lock, flags);

	D_RATE("leave\n");
}

/**
 * il3945_collect_tx_data - Update the success/failure sliding win
 *
 * We keep a sliding win of the last 64 packets transmitted
 * at this rate.  win->data contains the bitmask of successful
 * packets.
 */
static void
il3945_collect_tx_data(struct il3945_rs_sta *rs_sta,
		       struct il3945_rate_scale_data *win, int success,
		       int retries, int idx)
{
	unsigned long flags;
	s32 fail_count;
	struct il_priv *il __maybe_unused = rs_sta->il;

	if (!retries) {
		D_RATE("leave: retries == 0 -- should be at least 1\n");
		return;
	}

	spin_lock_irqsave(&rs_sta->lock, flags);

	/*
	 * Keep track of only the latest 62 tx frame attempts in this rate's
	 * history win; anything older isn't really relevant any more.
	 * If we have filled up the sliding win, drop the oldest attempt;
	 * if the oldest attempt (highest bit in bitmap) shows "success",
	 * subtract "1" from the success counter (this is the main reason
	 * we keep these bitmaps!).
	 * */
	while (retries > 0) {
		if (win->counter >= RATE_MAX_WINDOW) {

			/* remove earliest */
			win->counter = RATE_MAX_WINDOW - 1;

			if (win->data & (1ULL << (RATE_MAX_WINDOW - 1))) {
				win->data &= ~(1ULL << (RATE_MAX_WINDOW - 1));
				win->success_counter--;
			}
		}

		/* Increment frames-attempted counter */
		win->counter++;

		/* Shift bitmap by one frame (throw away oldest history),
		 * OR in "1", and increment "success" if this
		 * frame was successful. */
		win->data <<= 1;
		if (success > 0) {
			win->success_counter++;
			win->data |= 0x1;
			success--;
		}

		retries--;
	}

	/* Calculate current success ratio, avoid divide-by-0! */
	if (win->counter > 0)
		win->success_ratio =
		    128 * (100 * win->success_counter) / win->counter;
	else
		win->success_ratio = IL_INVALID_VALUE;

	fail_count = win->counter - win->success_counter;

	/* Calculate average throughput, if we have enough history. */
	if (fail_count >= RATE_MIN_FAILURE_TH ||
	    win->success_counter >= RATE_MIN_SUCCESS_TH)
		win->average_tpt =
		    ((win->success_ratio * rs_sta->expected_tpt[idx] +
		      64) / 128);
	else
		win->average_tpt = IL_INVALID_VALUE;

	/* Tag this win as having been updated */
	win->stamp = jiffies;

	spin_unlock_irqrestore(&rs_sta->lock, flags);
}

/*
 * Called after adding a new station to initialize rate scaling
 */
void
il3945_rs_rate_init(struct il_priv *il, struct ieee80211_sta *sta, u8 sta_id)
{
	struct ieee80211_hw *hw = il->hw;
	struct ieee80211_conf *conf = &il->hw->conf;
	struct il3945_sta_priv *psta;
	struct il3945_rs_sta *rs_sta;
	struct ieee80211_supported_band *sband;
	int i;

	D_INFO("enter\n");
	if (sta_id == il->hw_params.bcast_id)
		goto out;

	psta = (struct il3945_sta_priv *)sta->drv_priv;
	rs_sta = &psta->rs_sta;
	sband = hw->wiphy->bands[conf->chandef.chan->band];

	rs_sta->il = il;

	rs_sta->start_rate = RATE_INVALID;

	/* default to just 802.11b */
	rs_sta->expected_tpt = il3945_expected_tpt_b;

	rs_sta->last_partial_flush = jiffies;
	rs_sta->last_flush = jiffies;
	rs_sta->flush_time = RATE_FLUSH;
	rs_sta->last_tx_packets = 0;

	rs_sta->rate_scale_flush.data = (unsigned long)rs_sta;
	rs_sta->rate_scale_flush.function = il3945_bg_rate_scale_flush;

	for (i = 0; i < RATE_COUNT_3945; i++)
		il3945_clear_win(&rs_sta->win[i]);

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

	il->_3945.sta_supp_rates = sta->supp_rates[sband->band];
	/* For 5 GHz band it start at IL_FIRST_OFDM_RATE */
	if (sband->band == NL80211_BAND_5GHZ) {
		rs_sta->last_txrate_idx += IL_FIRST_OFDM_RATE;
		il->_3945.sta_supp_rates <<= IL_FIRST_OFDM_RATE;
	}

out:
	il->stations[sta_id].used &= ~IL_STA_UCODE_INPROGRESS;

	D_INFO("leave\n");
}

static void *
il3945_rs_alloc(struct ieee80211_hw *hw, struct dentry *debugfsdir)
{
	return hw->priv;
}

/* rate scale requires free function to be implemented */
static void
il3945_rs_free(void *il)
{
}

static void *
il3945_rs_alloc_sta(void *il_priv, struct ieee80211_sta *sta, gfp_t gfp)
{
	struct il3945_rs_sta *rs_sta;
	struct il3945_sta_priv *psta = (void *)sta->drv_priv;
	struct il_priv *il __maybe_unused = il_priv;

	D_RATE("enter\n");

	rs_sta = &psta->rs_sta;

	spin_lock_init(&rs_sta->lock);
	init_timer(&rs_sta->rate_scale_flush);

	D_RATE("leave\n");

	return rs_sta;
}

static void
il3945_rs_free_sta(void *il_priv, struct ieee80211_sta *sta, void *il_sta)
{
	struct il3945_rs_sta *rs_sta = il_sta;

	/*
	 * Be careful not to use any members of il3945_rs_sta (like trying
	 * to use il_priv to print out debugging) since it may not be fully
	 * initialized at this point.
	 */
	del_timer_sync(&rs_sta->rate_scale_flush);
}

/**
 * il3945_rs_tx_status - Update rate control values based on Tx results
 *
 * NOTE: Uses il_priv->retry_rate for the # of retries attempted by
 * the hardware for each rate.
 */
static void
il3945_rs_tx_status(void *il_rate, struct ieee80211_supported_band *sband,
		    struct ieee80211_sta *sta, void *il_sta,
		    struct sk_buff *skb)
{
	s8 retries = 0, current_count;
	int scale_rate_idx, first_idx, last_idx;
	unsigned long flags;
	struct il_priv *il = (struct il_priv *)il_rate;
	struct il3945_rs_sta *rs_sta = il_sta;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	D_RATE("enter\n");

	retries = info->status.rates[0].count;
	/* Sanity Check for retries */
	if (retries > RATE_RETRY_TH)
		retries = RATE_RETRY_TH;

	first_idx = sband->bitrates[info->status.rates[0].idx].hw_value;
	if (first_idx < 0 || first_idx >= RATE_COUNT_3945) {
		D_RATE("leave: Rate out of bounds: %d\n", first_idx);
		return;
	}

	if (!il_sta) {
		D_RATE("leave: No STA il data to update!\n");
		return;
	}

	/* Treat uninitialized rate scaling data same as non-existing. */
	if (!rs_sta->il) {
		D_RATE("leave: STA il data uninitialized!\n");
		return;
	}

	rs_sta->tx_packets++;

	scale_rate_idx = first_idx;
	last_idx = first_idx;

	/*
	 * Update the win for each rate.  We determine which rates
	 * were Tx'd based on the total number of retries vs. the number
	 * of retries configured for each rate -- currently set to the
	 * il value 'retry_rate' vs. rate specific
	 *
	 * On exit from this while loop last_idx indicates the rate
	 * at which the frame was finally transmitted (or failed if no
	 * ACK)
	 */
	while (retries > 1) {
		if ((retries - 1) < il->retry_rate) {
			current_count = (retries - 1);
			last_idx = scale_rate_idx;
		} else {
			current_count = il->retry_rate;
			last_idx = il3945_rs_next_rate(il, scale_rate_idx);
		}

		/* Update this rate accounting for as many retries
		 * as was used for it (per current_count) */
		il3945_collect_tx_data(rs_sta, &rs_sta->win[scale_rate_idx], 0,
				       current_count, scale_rate_idx);
		D_RATE("Update rate %d for %d retries.\n", scale_rate_idx,
		       current_count);

		retries -= current_count;

		scale_rate_idx = last_idx;
	}

	/* Update the last idx win with success/failure based on ACK */
	D_RATE("Update rate %d with %s.\n", last_idx,
	       (info->flags & IEEE80211_TX_STAT_ACK) ? "success" : "failure");
	il3945_collect_tx_data(rs_sta, &rs_sta->win[last_idx],
			       info->flags & IEEE80211_TX_STAT_ACK, 1,
			       last_idx);

	/* We updated the rate scale win -- if its been more than
	 * flush_time since the last run, schedule the flush
	 * again */
	spin_lock_irqsave(&rs_sta->lock, flags);

	if (!rs_sta->flush_pending &&
	    time_after(jiffies, rs_sta->last_flush + rs_sta->flush_time)) {

		rs_sta->last_partial_flush = jiffies;
		rs_sta->flush_pending = 1;
		mod_timer(&rs_sta->rate_scale_flush,
			  jiffies + rs_sta->flush_time);
	}

	spin_unlock_irqrestore(&rs_sta->lock, flags);

	D_RATE("leave\n");
}

static u16
il3945_get_adjacent_rate(struct il3945_rs_sta *rs_sta, u8 idx, u16 rate_mask,
			 enum nl80211_band band)
{
	u8 high = RATE_INVALID;
	u8 low = RATE_INVALID;
	struct il_priv *il __maybe_unused = rs_sta->il;

	/* 802.11A walks to the next literal adjacent rate in
	 * the rate table */
	if (unlikely(band == NL80211_BAND_5GHZ)) {
		int i;
		u32 mask;

		/* Find the previous rate that is in the rate mask */
		i = idx - 1;
		for (mask = (1 << i); i >= 0; i--, mask >>= 1) {
			if (rate_mask & mask) {
				low = i;
				break;
			}
		}

		/* Find the next rate that is in the rate mask */
		i = idx + 1;
		for (mask = (1 << i); i < RATE_COUNT_3945; i++, mask <<= 1) {
			if (rate_mask & mask) {
				high = i;
				break;
			}
		}

		return (high << 8) | low;
	}

	low = idx;
	while (low != RATE_INVALID) {
		if (rs_sta->tgg)
			low = il3945_rates[low].prev_rs_tgg;
		else
			low = il3945_rates[low].prev_rs;
		if (low == RATE_INVALID)
			break;
		if (rate_mask & (1 << low))
			break;
		D_RATE("Skipping masked lower rate: %d\n", low);
	}

	high = idx;
	while (high != RATE_INVALID) {
		if (rs_sta->tgg)
			high = il3945_rates[high].next_rs_tgg;
		else
			high = il3945_rates[high].next_rs;
		if (high == RATE_INVALID)
			break;
		if (rate_mask & (1 << high))
			break;
		D_RATE("Skipping masked higher rate: %d\n", high);
	}

	return (high << 8) | low;
}

/**
 * il3945_rs_get_rate - find the rate for the requested packet
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
 * As such, we can't convert the idx obtained below into the hw_mode's
 * rate table and must reference the driver allocated rate table
 *
 */
static void
il3945_rs_get_rate(void *il_r, struct ieee80211_sta *sta, void *il_sta,
		   struct ieee80211_tx_rate_control *txrc)
{
	struct ieee80211_supported_band *sband = txrc->sband;
	struct sk_buff *skb = txrc->skb;
	u8 low = RATE_INVALID;
	u8 high = RATE_INVALID;
	u16 high_low;
	int idx;
	struct il3945_rs_sta *rs_sta = il_sta;
	struct il3945_rate_scale_data *win = NULL;
	int current_tpt = IL_INVALID_VALUE;
	int low_tpt = IL_INVALID_VALUE;
	int high_tpt = IL_INVALID_VALUE;
	u32 fail_count;
	s8 scale_action = 0;
	unsigned long flags;
	u16 rate_mask;
	s8 max_rate_idx = -1;
	struct il_priv *il __maybe_unused = (struct il_priv *)il_r;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	D_RATE("enter\n");

	/* Treat uninitialized rate scaling data same as non-existing. */
	if (rs_sta && !rs_sta->il) {
		D_RATE("Rate scaling information not initialized yet.\n");
		il_sta = NULL;
	}

	if (rate_control_send_low(sta, il_sta, txrc))
		return;

	rate_mask = sta->supp_rates[sband->band];

	/* get user max rate if set */
	max_rate_idx = fls(txrc->rate_idx_mask) - 1;
	if (sband->band == NL80211_BAND_5GHZ && max_rate_idx != -1)
		max_rate_idx += IL_FIRST_OFDM_RATE;
	if (max_rate_idx < 0 || max_rate_idx >= RATE_COUNT)
		max_rate_idx = -1;

	idx = min(rs_sta->last_txrate_idx & 0xffff, RATE_COUNT_3945 - 1);

	if (sband->band == NL80211_BAND_5GHZ)
		rate_mask = rate_mask << IL_FIRST_OFDM_RATE;

	spin_lock_irqsave(&rs_sta->lock, flags);

	/* for recent assoc, choose best rate regarding
	 * to rssi value
	 */
	if (rs_sta->start_rate != RATE_INVALID) {
		if (rs_sta->start_rate < idx &&
		    (rate_mask & (1 << rs_sta->start_rate)))
			idx = rs_sta->start_rate;
		rs_sta->start_rate = RATE_INVALID;
	}

	/* force user max rate if set by user */
	if (max_rate_idx != -1 && max_rate_idx < idx) {
		if (rate_mask & (1 << max_rate_idx))
			idx = max_rate_idx;
	}

	win = &(rs_sta->win[idx]);

	fail_count = win->counter - win->success_counter;

	if (fail_count < RATE_MIN_FAILURE_TH &&
	    win->success_counter < RATE_MIN_SUCCESS_TH) {
		spin_unlock_irqrestore(&rs_sta->lock, flags);

		D_RATE("Invalid average_tpt on rate %d: "
		       "counter: %d, success_counter: %d, "
		       "expected_tpt is %sNULL\n", idx, win->counter,
		       win->success_counter,
		       rs_sta->expected_tpt ? "not " : "");

		/* Can't calculate this yet; not enough history */
		win->average_tpt = IL_INVALID_VALUE;
		goto out;

	}

	current_tpt = win->average_tpt;

	high_low =
	    il3945_get_adjacent_rate(rs_sta, idx, rate_mask, sband->band);
	low = high_low & 0xff;
	high = (high_low >> 8) & 0xff;

	/* If user set max rate, dont allow higher than user constrain */
	if (max_rate_idx != -1 && max_rate_idx < high)
		high = RATE_INVALID;

	/* Collect Measured throughputs of adjacent rates */
	if (low != RATE_INVALID)
		low_tpt = rs_sta->win[low].average_tpt;

	if (high != RATE_INVALID)
		high_tpt = rs_sta->win[high].average_tpt;

	spin_unlock_irqrestore(&rs_sta->lock, flags);

	scale_action = 0;

	/* Low success ratio , need to drop the rate */
	if (win->success_ratio < RATE_DECREASE_TH || !current_tpt) {
		D_RATE("decrease rate because of low success_ratio\n");
		scale_action = -1;
		/* No throughput measured yet for adjacent rates,
		 * try increase */
	} else if (low_tpt == IL_INVALID_VALUE && high_tpt == IL_INVALID_VALUE) {

		if (high != RATE_INVALID &&
		    win->success_ratio >= RATE_INCREASE_TH)
			scale_action = 1;
		else if (low != RATE_INVALID)
			scale_action = 0;

		/* Both adjacent throughputs are measured, but neither one has
		 * better throughput; we're using the best rate, don't change
		 * it! */
	} else if (low_tpt != IL_INVALID_VALUE && high_tpt != IL_INVALID_VALUE
		   && low_tpt < current_tpt && high_tpt < current_tpt) {

		D_RATE("No action -- low [%d] & high [%d] < "
		       "current_tpt [%d]\n", low_tpt, high_tpt, current_tpt);
		scale_action = 0;

		/* At least one of the rates has better throughput */
	} else {
		if (high_tpt != IL_INVALID_VALUE) {

			/* High rate has better throughput, Increase
			 * rate */
			if (high_tpt > current_tpt &&
			    win->success_ratio >= RATE_INCREASE_TH)
				scale_action = 1;
			else {
				D_RATE("decrease rate because of high tpt\n");
				scale_action = 0;
			}
		} else if (low_tpt != IL_INVALID_VALUE) {
			if (low_tpt > current_tpt) {
				D_RATE("decrease rate because of low tpt\n");
				scale_action = -1;
			} else if (win->success_ratio >= RATE_INCREASE_TH) {
				/* Lower rate has better
				 * throughput,decrease rate */
				scale_action = 1;
			}
		}
	}

	/* Sanity check; asked for decrease, but success rate or throughput
	 * has been good at old rate.  Don't change it. */
	if (scale_action == -1 && low != RATE_INVALID &&
	    (win->success_ratio > RATE_HIGH_TH ||
	     current_tpt > 100 * rs_sta->expected_tpt[low]))
		scale_action = 0;

	switch (scale_action) {
	case -1:
		/* Decrese rate */
		if (low != RATE_INVALID)
			idx = low;
		break;
	case 1:
		/* Increase rate */
		if (high != RATE_INVALID)
			idx = high;

		break;
	case 0:
	default:
		/* No change */
		break;
	}

	D_RATE("Selected %d (action %d) - low %d high %d\n", idx, scale_action,
	       low, high);

out:

	if (sband->band == NL80211_BAND_5GHZ) {
		if (WARN_ON_ONCE(idx < IL_FIRST_OFDM_RATE))
			idx = IL_FIRST_OFDM_RATE;
		rs_sta->last_txrate_idx = idx;
		info->control.rates[0].idx = idx - IL_FIRST_OFDM_RATE;
	} else {
		rs_sta->last_txrate_idx = idx;
		info->control.rates[0].idx = rs_sta->last_txrate_idx;
	}
	info->control.rates[0].count = 1;

	D_RATE("leave: %d\n", idx);
}

#ifdef CONFIG_MAC80211_DEBUGFS

static ssize_t
il3945_sta_dbgfs_stats_table_read(struct file *file, char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	char *buff;
	int desc = 0;
	int j;
	ssize_t ret;
	struct il3945_rs_sta *lq_sta = file->private_data;

	buff = kmalloc(1024, GFP_KERNEL);
	if (!buff)
		return -ENOMEM;

	desc +=
	    sprintf(buff + desc,
		    "tx packets=%d last rate idx=%d\n"
		    "rate=0x%X flush time %d\n", lq_sta->tx_packets,
		    lq_sta->last_txrate_idx, lq_sta->start_rate,
		    jiffies_to_msecs(lq_sta->flush_time));
	for (j = 0; j < RATE_COUNT_3945; j++) {
		desc +=
		    sprintf(buff + desc, "counter=%d success=%d %%=%d\n",
			    lq_sta->win[j].counter,
			    lq_sta->win[j].success_counter,
			    lq_sta->win[j].success_ratio);
	}
	ret = simple_read_from_buffer(user_buf, count, ppos, buff, desc);
	kfree(buff);
	return ret;
}

static const struct file_operations rs_sta_dbgfs_stats_table_ops = {
	.read = il3945_sta_dbgfs_stats_table_read,
	.open = simple_open,
	.llseek = default_llseek,
};

static void
il3945_add_debugfs(void *il, void *il_sta, struct dentry *dir)
{
	struct il3945_rs_sta *lq_sta = il_sta;

	lq_sta->rs_sta_dbgfs_stats_table_file =
	    debugfs_create_file("rate_stats_table", 0600, dir, lq_sta,
				&rs_sta_dbgfs_stats_table_ops);

}

static void
il3945_remove_debugfs(void *il, void *il_sta)
{
	struct il3945_rs_sta *lq_sta = il_sta;
	debugfs_remove(lq_sta->rs_sta_dbgfs_stats_table_file);
}
#endif

/*
 * Initialization of rate scaling information is done by driver after
 * the station is added. Since mac80211 calls this function before a
 * station is added we ignore it.
 */
static void
il3945_rs_rate_init_stub(void *il_r, struct ieee80211_supported_band *sband,
			 struct cfg80211_chan_def *chandef,
			 struct ieee80211_sta *sta, void *il_sta)
{
}

static const struct rate_control_ops rs_ops = {
	.name = RS_NAME,
	.tx_status = il3945_rs_tx_status,
	.get_rate = il3945_rs_get_rate,
	.rate_init = il3945_rs_rate_init_stub,
	.alloc = il3945_rs_alloc,
	.free = il3945_rs_free,
	.alloc_sta = il3945_rs_alloc_sta,
	.free_sta = il3945_rs_free_sta,
#ifdef CONFIG_MAC80211_DEBUGFS
	.add_sta_debugfs = il3945_add_debugfs,
	.remove_sta_debugfs = il3945_remove_debugfs,
#endif

};

void
il3945_rate_scale_init(struct ieee80211_hw *hw, s32 sta_id)
{
	struct il_priv *il = hw->priv;
	s32 rssi = 0;
	unsigned long flags;
	struct il3945_rs_sta *rs_sta;
	struct ieee80211_sta *sta;
	struct il3945_sta_priv *psta;

	D_RATE("enter\n");

	rcu_read_lock();

	sta = ieee80211_find_sta(il->vif, il->stations[sta_id].sta.sta.addr);
	if (!sta) {
		D_RATE("Unable to find station to initialize rate scaling.\n");
		rcu_read_unlock();
		return;
	}

	psta = (void *)sta->drv_priv;
	rs_sta = &psta->rs_sta;

	spin_lock_irqsave(&rs_sta->lock, flags);

	rs_sta->tgg = 0;
	switch (il->band) {
	case NL80211_BAND_2GHZ:
		/* TODO: this always does G, not a regression */
		if (il->active.flags & RXON_FLG_TGG_PROTECT_MSK) {
			rs_sta->tgg = 1;
			rs_sta->expected_tpt = il3945_expected_tpt_g_prot;
		} else
			rs_sta->expected_tpt = il3945_expected_tpt_g;
		break;
	case NL80211_BAND_5GHZ:
		rs_sta->expected_tpt = il3945_expected_tpt_a;
		break;
	default:
		BUG();
		break;
	}

	spin_unlock_irqrestore(&rs_sta->lock, flags);

	rssi = il->_3945.last_rx_rssi;
	if (rssi == 0)
		rssi = IL_MIN_RSSI_VAL;

	D_RATE("Network RSSI: %d\n", rssi);

	rs_sta->start_rate = il3945_get_rate_idx_by_rssi(rssi, il->band);

	D_RATE("leave: rssi %d assign rate idx: " "%d (plcp 0x%x)\n", rssi,
	       rs_sta->start_rate, il3945_rates[rs_sta->start_rate].plcp);
	rcu_read_unlock();
}

int
il3945_rate_control_register(void)
{
	return ieee80211_rate_control_register(&rs_ops);
}

void
il3945_rate_control_unregister(void)
{
	ieee80211_rate_control_unregister(&rs_ops);
}
