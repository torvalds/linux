/******************************************************************************
 *
 * Copyright(c) 2005 - 2007 Intel Corporation. All rights reserved.
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
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/wireless.h>
#include <net/mac80211.h>
#include <net/ieee80211.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>

#include <linux/workqueue.h>

#define IWL 3945

#include "../net/mac80211/ieee80211_rate.h"

#include "iwlwifi.h"

#define RS_NAME "iwl-3945-rs"

struct iwl_rate_scale_data {
	u64 data;
	s32 success_counter;
	s32 success_ratio;
	s32 counter;
	s32 average_tpt;
	unsigned long stamp;
};

struct iwl_rate_scale_priv {
	spinlock_t lock;
	s32 *expected_tpt;
	unsigned long last_partial_flush;
	unsigned long last_flush;
	u32 flush_time;
	u32 last_tx_packets;
	u32 tx_packets;
	u8 tgg;
	u8 flush_pending;
	u8 start_rate;
	u8 ibss_sta_added;
	struct timer_list rate_scale_flush;
	struct iwl_rate_scale_data win[IWL_RATE_COUNT];
};

static s32 iwl_expected_tpt_g[IWL_RATE_COUNT] = {
	0, 0, 76, 104, 130, 168, 191, 202, 7, 13, 35, 58
};

static s32 iwl_expected_tpt_g_prot[IWL_RATE_COUNT] = {
	0, 0, 0, 80, 93, 113, 123, 125, 7, 13, 35, 58
};

static s32 iwl_expected_tpt_a[IWL_RATE_COUNT] = {
	40, 57, 72, 98, 121, 154, 177, 186, 0, 0, 0, 0
};

static s32 iwl_expected_tpt_b[IWL_RATE_COUNT] = {
	0, 0, 0, 0, 0, 0, 0, 0, 7, 13, 35, 58
};

struct iwl_tpt_entry {
	s8 min_rssi;
	u8 index;
};

static struct iwl_tpt_entry iwl_tpt_table_a[] = {
	{-60, IWL_RATE_54M_INDEX},
	{-64, IWL_RATE_48M_INDEX},
	{-72, IWL_RATE_36M_INDEX},
	{-80, IWL_RATE_24M_INDEX},
	{-84, IWL_RATE_18M_INDEX},
	{-85, IWL_RATE_12M_INDEX},
	{-87, IWL_RATE_9M_INDEX},
	{-89, IWL_RATE_6M_INDEX}
};

static struct iwl_tpt_entry iwl_tpt_table_b[] = {
	{-86, IWL_RATE_11M_INDEX},
	{-88, IWL_RATE_5M_INDEX},
	{-90, IWL_RATE_2M_INDEX},
	{-92, IWL_RATE_1M_INDEX}

};

static struct iwl_tpt_entry iwl_tpt_table_g[] = {
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
#define IWL_RATE_FLUSH        (3*HZ/10)
#define IWL_RATE_WIN_FLUSH       (HZ/2)
#define IWL_RATE_HIGH_TH          11520
#define IWL_RATE_MIN_FAILURE_TH       8
#define IWL_RATE_MIN_SUCCESS_TH       8
#define IWL_RATE_DECREASE_TH       1920

static u8 iwl_get_rate_index_by_rssi(s32 rssi, u8 mode)
{
	u32 index = 0;
	u32 table_size = 0;
	struct iwl_tpt_entry *tpt_table = NULL;

	if ((rssi < IWL_MIN_RSSI_VAL) || (rssi > IWL_MAX_RSSI_VAL))
		rssi = IWL_MIN_RSSI_VAL;

	switch (mode) {
	case MODE_IEEE80211G:
		tpt_table = iwl_tpt_table_g;
		table_size = ARRAY_SIZE(iwl_tpt_table_g);
		break;

	case MODE_IEEE80211A:
		tpt_table = iwl_tpt_table_a;
		table_size = ARRAY_SIZE(iwl_tpt_table_a);
		break;

	default:
	case MODE_IEEE80211B:
		tpt_table = iwl_tpt_table_b;
		table_size = ARRAY_SIZE(iwl_tpt_table_b);
		break;
	}

	while ((index < table_size) && (rssi < tpt_table[index].min_rssi))
		index++;

	index = min(index, (table_size - 1));

	return tpt_table[index].index;
}

static void iwl_clear_window(struct iwl_rate_scale_data *window)
{
	window->data = 0;
	window->success_counter = 0;
	window->success_ratio = IWL_INVALID_VALUE;
	window->counter = 0;
	window->average_tpt = IWL_INVALID_VALUE;
	window->stamp = 0;
}

/**
 * iwl_rate_scale_flush_windows - flush out the rate scale windows
 *
 * Returns the number of windows that have gathered data but were
 * not flushed.  If there were any that were not flushed, then
 * reschedule the rate flushing routine.
 */
static int iwl_rate_scale_flush_windows(struct iwl_rate_scale_priv *rs_priv)
{
	int unflushed = 0;
	int i;
	unsigned long flags;

	/*
	 * For each rate, if we have collected data on that rate
	 * and it has been more than IWL_RATE_WIN_FLUSH
	 * since we flushed, clear out the gathered statistics
	 */
	for (i = 0; i < IWL_RATE_COUNT; i++) {
		if (!rs_priv->win[i].counter)
			continue;

		spin_lock_irqsave(&rs_priv->lock, flags);
		if (time_after(jiffies, rs_priv->win[i].stamp +
			       IWL_RATE_WIN_FLUSH)) {
			IWL_DEBUG_RATE("flushing %d samples of rate "
				       "index %d\n",
				       rs_priv->win[i].counter, i);
			iwl_clear_window(&rs_priv->win[i]);
		} else
			unflushed++;
		spin_unlock_irqrestore(&rs_priv->lock, flags);
	}

	return unflushed;
}

#define IWL_RATE_FLUSH_MAX              5000	/* msec */
#define IWL_RATE_FLUSH_MIN              50	/* msec */

static void iwl_bg_rate_scale_flush(unsigned long data)
{
	struct iwl_rate_scale_priv *rs_priv = (void *)data;
	int unflushed = 0;
	unsigned long flags;
	u32 packet_count, duration, pps;

	IWL_DEBUG_RATE("enter\n");

	unflushed = iwl_rate_scale_flush_windows(rs_priv);

	spin_lock_irqsave(&rs_priv->lock, flags);

	rs_priv->flush_pending = 0;

	/* Number of packets Rx'd since last time this timer ran */
	packet_count = (rs_priv->tx_packets - rs_priv->last_tx_packets) + 1;

	rs_priv->last_tx_packets = rs_priv->tx_packets + 1;

	if (unflushed) {
		duration =
		    jiffies_to_msecs(jiffies - rs_priv->last_partial_flush);
/*              duration = jiffies_to_msecs(rs_priv->flush_time); */

		IWL_DEBUG_RATE("Tx'd %d packets in %dms\n",
			       packet_count, duration);

		/* Determine packets per second */
		if (duration)
			pps = (packet_count * 1000) / duration;
		else
			pps = 0;

		if (pps) {
			duration = IWL_RATE_FLUSH_MAX / pps;
			if (duration < IWL_RATE_FLUSH_MIN)
				duration = IWL_RATE_FLUSH_MIN;
		} else
			duration = IWL_RATE_FLUSH_MAX;

		rs_priv->flush_time = msecs_to_jiffies(duration);

		IWL_DEBUG_RATE("new flush period: %d msec ave %d\n",
			       duration, packet_count);

		mod_timer(&rs_priv->rate_scale_flush, jiffies +
			  rs_priv->flush_time);

		rs_priv->last_partial_flush = jiffies;
	}

	/* If there weren't any unflushed entries, we don't schedule the timer
	 * to run again */

	rs_priv->last_flush = jiffies;

	spin_unlock_irqrestore(&rs_priv->lock, flags);

	IWL_DEBUG_RATE("leave\n");
}

/**
 * iwl_collect_tx_data - Update the success/failure sliding window
 *
 * We keep a sliding window of the last 64 packets transmitted
 * at this rate.  window->data contains the bitmask of successful
 * packets.
 */
static void iwl_collect_tx_data(struct iwl_rate_scale_priv *rs_priv,
				struct iwl_rate_scale_data *window,
				int success, int retries)
{
	unsigned long flags;

	if (!retries) {
		IWL_DEBUG_RATE("leave: retries == 0 -- should be at least 1\n");
		return;
	}

	while (retries--) {
		spin_lock_irqsave(&rs_priv->lock, flags);

		/* If we have filled up the window then subtract one from the
		 * success counter if the high-bit is counting toward
		 * success */
		if (window->counter == IWL_RATE_MAX_WINDOW) {
			if (window->data & (1ULL << (IWL_RATE_MAX_WINDOW - 1)))
				window->success_counter--;
		} else
			window->counter++;

		/* Slide the window to the left one bit */
		window->data = (window->data << 1);

		/* If this packet was a success then set the low bit high */
		if (success) {
			window->success_counter++;
			window->data |= 1;
		}

		/* window->counter can't be 0 -- it is either >0 or
		 * IWL_RATE_MAX_WINDOW */
		window->success_ratio = 12800 * window->success_counter /
		    window->counter;

		/* Tag this window as having been updated */
		window->stamp = jiffies;

		spin_unlock_irqrestore(&rs_priv->lock, flags);
	}
}

static void rs_rate_init(void *priv_rate, void *priv_sta,
			 struct ieee80211_local *local, struct sta_info *sta)
{
	int i;

	IWL_DEBUG_RATE("enter\n");

	/* TODO: what is a good starting rate for STA? About middle? Maybe not
	 * the lowest or the highest rate.. Could consider using RSSI from
	 * previous packets? Need to have IEEE 802.1X auth succeed immediately
	 * after assoc.. */

	for (i = IWL_RATE_COUNT - 1; i >= 0; i--) {
		if (sta->supp_rates & (1 << i)) {
			sta->txrate = i;
			break;
		}
	}

	sta->last_txrate = sta->txrate;

	IWL_DEBUG_RATE("leave\n");
}

static void *rs_alloc(struct ieee80211_local *local)
{
	return local->hw.priv;
}

/* rate scale requires free function to be implmented */
static void rs_free(void *priv)
{
	return;
}
static void rs_clear(void *priv)
{
	return;
}


static void *rs_alloc_sta(void *priv, gfp_t gfp)
{
	struct iwl_rate_scale_priv *rs_priv;
	int i;

	IWL_DEBUG_RATE("enter\n");

	rs_priv = kzalloc(sizeof(struct iwl_rate_scale_priv), gfp);
	if (!rs_priv) {
		IWL_DEBUG_RATE("leave: ENOMEM\n");
		return NULL;
	}

	spin_lock_init(&rs_priv->lock);

	rs_priv->start_rate = IWL_RATE_INVALID;

	/* default to just 802.11b */
	rs_priv->expected_tpt = iwl_expected_tpt_b;

	rs_priv->last_partial_flush = jiffies;
	rs_priv->last_flush = jiffies;
	rs_priv->flush_time = IWL_RATE_FLUSH;
	rs_priv->last_tx_packets = 0;
	rs_priv->ibss_sta_added = 0;

	init_timer(&rs_priv->rate_scale_flush);
	rs_priv->rate_scale_flush.data = (unsigned long)rs_priv;
	rs_priv->rate_scale_flush.function = &iwl_bg_rate_scale_flush;

	for (i = 0; i < IWL_RATE_COUNT; i++)
		iwl_clear_window(&rs_priv->win[i]);

	IWL_DEBUG_RATE("leave\n");

	return rs_priv;
}

static void rs_free_sta(void *priv, void *priv_sta)
{
	struct iwl_rate_scale_priv *rs_priv = priv_sta;

	IWL_DEBUG_RATE("enter\n");
	del_timer_sync(&rs_priv->rate_scale_flush);
	kfree(rs_priv);
	IWL_DEBUG_RATE("leave\n");
}

/**
 * rs_tx_status - Update rate control values based on Tx results
 *
 * NOTE: Uses iwl_priv->retry_rate for the # of retries attempted by
 * the hardware for each rate.
 */
static void rs_tx_status(void *priv_rate,
			 struct net_device *dev,
			 struct sk_buff *skb,
			 struct ieee80211_tx_status *tx_resp)
{
	u8 retries, current_count;
	int scale_rate_index, first_index, last_index;
	unsigned long flags;
	struct sta_info *sta;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct iwl_priv *priv = (struct iwl_priv *)priv_rate;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct iwl_rate_scale_priv *rs_priv;

	IWL_DEBUG_RATE("enter\n");

	retries = tx_resp->retry_count;

	first_index = tx_resp->control.tx_rate;
	if ((first_index < 0) || (first_index >= IWL_RATE_COUNT)) {
		IWL_DEBUG_RATE("leave: Rate out of bounds: %0x for %d\n",
			       tx_resp->control.tx_rate, first_index);
		return;
	}

	sta = sta_info_get(local, hdr->addr1);
	if (!sta || !sta->rate_ctrl_priv) {
		if (sta)
			sta_info_put(sta);
		IWL_DEBUG_RATE("leave: No STA priv data to update!\n");
		return;
	}

	rs_priv = (void *)sta->rate_ctrl_priv;

	rs_priv->tx_packets++;

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
	while (retries > 0) {
		if (retries < priv->retry_rate) {
			current_count = retries;
			last_index = scale_rate_index;
		} else {
			current_count = priv->retry_rate;
			last_index = iwl_get_prev_ieee_rate(scale_rate_index);
		}

		/* Update this rate accounting for as many retries
		 * as was used for it (per current_count) */
		iwl_collect_tx_data(rs_priv,
				    &rs_priv->win[scale_rate_index],
				    0, current_count);
		IWL_DEBUG_RATE("Update rate %d for %d retries.\n",
			       scale_rate_index, current_count);

		retries -= current_count;

		if (retries)
			scale_rate_index =
			    iwl_get_prev_ieee_rate(scale_rate_index);
	}

	/* Update the last index window with success/failure based on ACK */
	IWL_DEBUG_RATE("Update rate %d with %s.\n",
		       last_index,
		       (tx_resp->flags & IEEE80211_TX_STATUS_ACK) ?
		       "success" : "failure");
	iwl_collect_tx_data(rs_priv,
			    &rs_priv->win[last_index],
			    tx_resp->flags & IEEE80211_TX_STATUS_ACK, 1);

	/* We updated the rate scale window -- if its been more than
	 * flush_time since the last run, schedule the flush
	 * again */
	spin_lock_irqsave(&rs_priv->lock, flags);

	if (!rs_priv->flush_pending &&
	    time_after(jiffies, rs_priv->last_partial_flush +
		       rs_priv->flush_time)) {

		rs_priv->flush_pending = 1;
		mod_timer(&rs_priv->rate_scale_flush,
			  jiffies + rs_priv->flush_time);
	}

	spin_unlock_irqrestore(&rs_priv->lock, flags);

	sta_info_put(sta);

	IWL_DEBUG_RATE("leave\n");

	return;
}

static struct ieee80211_rate *iwl_get_lowest_rate(struct ieee80211_local
						  *local)
{
	struct ieee80211_hw_mode *mode = local->oper_hw_mode;
	int i;

	for (i = 0; i < mode->num_rates; i++) {
		struct ieee80211_rate *rate = &mode->rates[i];

		if (rate->flags & IEEE80211_RATE_SUPPORTED)
			return rate;
	}

	return &mode->rates[0];
}

static u16 iwl_get_adjacent_rate(struct iwl_rate_scale_priv *rs_priv,
				 u8 index, u16 rate_mask, int phymode)
{
	u8 high = IWL_RATE_INVALID;
	u8 low = IWL_RATE_INVALID;

	/* 802.11A walks to the next literal adjascent rate in
	 * the rate table */
	if (unlikely(phymode == MODE_IEEE80211A)) {
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
		for (mask = (1 << i); i < IWL_RATE_COUNT; i++, mask <<= 1) {
			if (rate_mask & mask) {
				high = i;
				break;
			}
		}

		return (high << 8) | low;
	}

	low = index;
	while (low != IWL_RATE_INVALID) {
		if (rs_priv->tgg)
			low = iwl_rates[low].prev_rs_tgg;
		else
			low = iwl_rates[low].prev_rs;
		if (low == IWL_RATE_INVALID)
			break;
		if (rate_mask & (1 << low))
			break;
		IWL_DEBUG_RATE("Skipping masked lower rate: %d\n", low);
	}

	high = index;
	while (high != IWL_RATE_INVALID) {
		if (rs_priv->tgg)
			high = iwl_rates[high].next_rs_tgg;
		else
			high = iwl_rates[high].next_rs;
		if (high == IWL_RATE_INVALID)
			break;
		if (rate_mask & (1 << high))
			break;
		IWL_DEBUG_RATE("Skipping masked higher rate: %d\n", high);
	}

	return (high << 8) | low;
}

/**
 * rs_get_rate - find the rate for the requested packet
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
static struct ieee80211_rate *rs_get_rate(void *priv_rate,
					  struct net_device *dev,
					  struct sk_buff *skb,
					  struct rate_control_extra *extra)
{
	u8 low = IWL_RATE_INVALID;
	u8 high = IWL_RATE_INVALID;
	u16 high_low;
	int index;
	struct iwl_rate_scale_priv *rs_priv;
	struct iwl_rate_scale_data *window = NULL;
	int current_tpt = IWL_INVALID_VALUE;
	int low_tpt = IWL_INVALID_VALUE;
	int high_tpt = IWL_INVALID_VALUE;
	u32 fail_count;
	s8 scale_action = 0;
	unsigned long flags;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct sta_info *sta;
	u16 fc, rate_mask;
	struct iwl_priv *priv = (struct iwl_priv *)priv_rate;
	DECLARE_MAC_BUF(mac);

	IWL_DEBUG_RATE("enter\n");

	memset(extra, 0, sizeof(*extra));

	fc = le16_to_cpu(hdr->frame_control);
	if (((fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_DATA) ||
	    (is_multicast_ether_addr(hdr->addr1))) {
		/* Send management frames and broadcast/multicast data using
		 * lowest rate. */
		/* TODO: this could probably be improved.. */
		IWL_DEBUG_RATE("leave: lowest rate (not data or is "
			       "multicast)\n");

		return iwl_get_lowest_rate(local);
	}

	sta = sta_info_get(local, hdr->addr1);
	if (!sta || !sta->rate_ctrl_priv) {
		IWL_DEBUG_RATE("leave: No STA priv data to update!\n");
		if (sta)
			sta_info_put(sta);
		return NULL;
	}

	rate_mask = sta->supp_rates;
	index = min(sta->txrate & 0xffff, IWL_RATE_COUNT - 1);

	rs_priv = (void *)sta->rate_ctrl_priv;

	if ((priv->iw_mode == IEEE80211_IF_TYPE_IBSS) &&
	    !rs_priv->ibss_sta_added) {
		u8 sta_id = iwl_hw_find_station(priv, hdr->addr1);

		if (sta_id == IWL_INVALID_STATION) {
			IWL_DEBUG_RATE("LQ: ADD station %s\n",
				       print_mac(mac, hdr->addr1));
			sta_id = iwl_add_station(priv,
				    hdr->addr1, 0, CMD_ASYNC);
		}
		if (sta_id != IWL_INVALID_STATION)
			rs_priv->ibss_sta_added = 1;
	}

	spin_lock_irqsave(&rs_priv->lock, flags);

	if (rs_priv->start_rate != IWL_RATE_INVALID) {
		index = rs_priv->start_rate;
		rs_priv->start_rate = IWL_RATE_INVALID;
	}

	window = &(rs_priv->win[index]);

	fail_count = window->counter - window->success_counter;

	if (((fail_count <= IWL_RATE_MIN_FAILURE_TH) &&
	     (window->success_counter < IWL_RATE_MIN_SUCCESS_TH))) {
		window->average_tpt = IWL_INVALID_VALUE;
		spin_unlock_irqrestore(&rs_priv->lock, flags);

		IWL_DEBUG_RATE("Invalid average_tpt on rate %d: "
			       "counter: %d, success_counter: %d, "
			       "expected_tpt is %sNULL\n",
			       index,
			       window->counter,
			       window->success_counter,
			       rs_priv->expected_tpt ? "not " : "");
		goto out;

	}

	window->average_tpt = ((window->success_ratio *
				rs_priv->expected_tpt[index] + 64) / 128);
	current_tpt = window->average_tpt;

	high_low = iwl_get_adjacent_rate(rs_priv, index, rate_mask,
					 local->hw.conf.phymode);
	low = high_low & 0xff;
	high = (high_low >> 8) & 0xff;

	if (low != IWL_RATE_INVALID)
		low_tpt = rs_priv->win[low].average_tpt;

	if (high != IWL_RATE_INVALID)
		high_tpt = rs_priv->win[high].average_tpt;

	spin_unlock_irqrestore(&rs_priv->lock, flags);

	scale_action = 1;

	if ((window->success_ratio < IWL_RATE_DECREASE_TH) || !current_tpt) {
		IWL_DEBUG_RATE("decrease rate because of low success_ratio\n");
		scale_action = -1;
	} else if ((low_tpt == IWL_INVALID_VALUE) &&
		   (high_tpt == IWL_INVALID_VALUE))
		scale_action = 1;
	else if ((low_tpt != IWL_INVALID_VALUE) &&
		   (high_tpt != IWL_INVALID_VALUE)
		   && (low_tpt < current_tpt)
		   && (high_tpt < current_tpt)) {
		IWL_DEBUG_RATE("No action -- low [%d] & high [%d] < "
			       "current_tpt [%d]\n",
			       low_tpt, high_tpt, current_tpt);
		scale_action = 0;
	} else {
		if (high_tpt != IWL_INVALID_VALUE) {
			if (high_tpt > current_tpt)
				scale_action = 1;
			else {
				IWL_DEBUG_RATE
				    ("decrease rate because of high tpt\n");
				scale_action = -1;
			}
		} else if (low_tpt != IWL_INVALID_VALUE) {
			if (low_tpt > current_tpt) {
				IWL_DEBUG_RATE
				    ("decrease rate because of low tpt\n");
				scale_action = -1;
			} else
				scale_action = 1;
		}
	}

	if ((window->success_ratio > IWL_RATE_HIGH_TH) ||
	    (current_tpt > window->average_tpt)) {
		IWL_DEBUG_RATE("No action -- success_ratio [%d] > HIGH_TH or "
			       "current_tpt [%d] > average_tpt [%d]\n",
			       window->success_ratio,
			       current_tpt, window->average_tpt);
		scale_action = 0;
	}

	switch (scale_action) {
	case -1:
		if (low != IWL_RATE_INVALID)
			index = low;
		break;

	case 1:
		if (high != IWL_RATE_INVALID)
			index = high;

		break;

	case 0:
	default:
		break;
	}

	IWL_DEBUG_RATE("Selected %d (action %d) - low %d high %d\n",
		       index, scale_action, low, high);

 out:

	sta->last_txrate = index;
	sta->txrate = sta->last_txrate;
	sta_info_put(sta);

	IWL_DEBUG_RATE("leave: %d\n", index);

	return &priv->ieee_rates[index];
}

static struct rate_control_ops rs_ops = {
	.module = NULL,
	.name = RS_NAME,
	.tx_status = rs_tx_status,
	.get_rate = rs_get_rate,
	.rate_init = rs_rate_init,
	.clear = rs_clear,
	.alloc = rs_alloc,
	.free = rs_free,
	.alloc_sta = rs_alloc_sta,
	.free_sta = rs_free_sta,
};

int iwl_fill_rs_info(struct ieee80211_hw *hw, char *buf, u8 sta_id)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct iwl_priv *priv = hw->priv;
	struct iwl_rate_scale_priv *rs_priv;
	struct sta_info *sta;
	unsigned long flags;
	int count = 0, i;
	u32 samples = 0, success = 0, good = 0;
	unsigned long now = jiffies;
	u32 max_time = 0;

	sta = sta_info_get(local, priv->stations[sta_id].sta.sta.addr);
	if (!sta || !sta->rate_ctrl_priv) {
		if (sta) {
			sta_info_put(sta);
			IWL_DEBUG_RATE("leave - no private rate data!\n");
		} else
			IWL_DEBUG_RATE("leave - no station!\n");
		return sprintf(buf, "station %d not found\n", sta_id);
	}

	rs_priv = (void *)sta->rate_ctrl_priv;
	spin_lock_irqsave(&rs_priv->lock, flags);
	i = IWL_RATE_54M_INDEX;
	while (1) {
		u64 mask;
		int j;

		count +=
		    sprintf(&buf[count], " %2dMbs: ", iwl_rates[i].ieee / 2);

		mask = (1ULL << (IWL_RATE_MAX_WINDOW - 1));
		for (j = 0; j < IWL_RATE_MAX_WINDOW; j++, mask >>= 1)
			buf[count++] =
			    (rs_priv->win[i].data & mask) ? '1' : '0';

		samples += rs_priv->win[i].counter;
		good += rs_priv->win[i].success_counter;
		success += rs_priv->win[i].success_counter * iwl_rates[i].ieee;

		if (rs_priv->win[i].stamp) {
			int delta =
			    jiffies_to_msecs(now - rs_priv->win[i].stamp);

			if (delta > max_time)
				max_time = delta;

			count += sprintf(&buf[count], "%5dms\n", delta);
		} else
			buf[count++] = '\n';

		j = iwl_get_prev_ieee_rate(i);
		if (j == i)
			break;
		i = j;
	}
	spin_unlock_irqrestore(&rs_priv->lock, flags);
	sta_info_put(sta);

	/* Display the average rate of all samples taken.
	 *
	 * NOTE:  We multiple # of samples by 2 since the IEEE measurement
	 * added from iwl_rates is actually 2X the rate */
	if (samples)
		count += sprintf(
			&buf[count],
			"\nAverage rate is %3d.%02dMbs over last %4dms\n"
			"%3d%% success (%d good packets over %d tries)\n",
			success / (2 * samples), (success * 5 / samples) % 10,
			max_time, good * 100 / samples, good, samples);
	else
		count += sprintf(&buf[count], "\nAverage rate: 0Mbs\n");

	return count;
}

void iwl_rate_scale_init(struct ieee80211_hw *hw, s32 sta_id)
{
	struct iwl_priv *priv = hw->priv;
	s32 rssi = 0;
	unsigned long flags;
	struct ieee80211_local *local = hw_to_local(hw);
	struct iwl_rate_scale_priv *rs_priv;
	struct sta_info *sta;

	IWL_DEBUG_RATE("enter\n");

	if (!local->rate_ctrl->ops->name ||
	    strcmp(local->rate_ctrl->ops->name, RS_NAME)) {
		IWL_WARNING("iwl-3945-rs not selected as rate control algo!\n");
		IWL_DEBUG_RATE("leave - mac80211 picked the wrong RC algo.\n");
		return;
	}

	sta = sta_info_get(local, priv->stations[sta_id].sta.sta.addr);
	if (!sta || !sta->rate_ctrl_priv) {
		if (sta)
			sta_info_put(sta);
		IWL_DEBUG_RATE("leave - no private rate data!\n");
		return;
	}

	rs_priv = (void *)sta->rate_ctrl_priv;

	spin_lock_irqsave(&rs_priv->lock, flags);

	rs_priv->tgg = 0;
	switch (priv->phymode) {
	case MODE_IEEE80211G:
		if (priv->active_rxon.flags & RXON_FLG_TGG_PROTECT_MSK) {
			rs_priv->tgg = 1;
			rs_priv->expected_tpt = iwl_expected_tpt_g_prot;
		} else
			rs_priv->expected_tpt = iwl_expected_tpt_g;
		break;

	case MODE_IEEE80211A:
		rs_priv->expected_tpt = iwl_expected_tpt_a;
		break;

	default:
		IWL_WARNING("Invalid phymode.  Defaulting to 802.11b\n");
	case MODE_IEEE80211B:
		rs_priv->expected_tpt = iwl_expected_tpt_b;
		break;
	}

	sta_info_put(sta);
	spin_unlock_irqrestore(&rs_priv->lock, flags);

	rssi = priv->last_rx_rssi;
	if (rssi == 0)
		rssi = IWL_MIN_RSSI_VAL;

	IWL_DEBUG(IWL_DL_INFO | IWL_DL_RATE, "Network RSSI: %d\n", rssi);

	rs_priv->start_rate = iwl_get_rate_index_by_rssi(rssi, priv->phymode);

	IWL_DEBUG_RATE("leave: rssi %d assign rate index: "
		       "%d (plcp 0x%x)\n", rssi, rs_priv->start_rate,
		       iwl_rates[rs_priv->start_rate].plcp);
}

void iwl_rate_control_register(struct ieee80211_hw *hw)
{
	ieee80211_rate_control_register(&rs_ops);
}

void iwl_rate_control_unregister(struct ieee80211_hw *hw)
{
	ieee80211_rate_control_unregister(&rs_ops);
}


