/******************************************************************************
 *
 * Copyright(c) 2005 - 2008 Intel Corporation. All rights reserved.
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

#include "../net/mac80211/rate.h"

#include "iwl-4965.h"
#include "iwl-core.h"
#include "iwl-helpers.h"

#define RS_NAME "iwl-4965-rs"

#define NUM_TRY_BEFORE_ANTENNA_TOGGLE 1
#define IWL_NUMBER_TRY      1
#define IWL_HT_NUMBER_TRY   3

#define IWL_RATE_MAX_WINDOW		62	/* # tx in history window */
#define IWL_RATE_MIN_FAILURE_TH		6	/* min failures to calc tpt */
#define IWL_RATE_MIN_SUCCESS_TH		8	/* min successes to calc tpt */

/* max time to accum history 2 seconds */
#define IWL_RATE_SCALE_FLUSH_INTVL   (2*HZ)

static u8 rs_ht_to_legacy[] = {
	IWL_RATE_6M_INDEX, IWL_RATE_6M_INDEX,
	IWL_RATE_6M_INDEX, IWL_RATE_6M_INDEX,
	IWL_RATE_6M_INDEX,
	IWL_RATE_6M_INDEX, IWL_RATE_9M_INDEX,
	IWL_RATE_12M_INDEX, IWL_RATE_18M_INDEX,
	IWL_RATE_24M_INDEX, IWL_RATE_36M_INDEX,
	IWL_RATE_48M_INDEX, IWL_RATE_54M_INDEX
};

struct iwl4965_rate {
	u32 rate_n_flags;
} __attribute__ ((packed));

/**
 * struct iwl4965_rate_scale_data -- tx success history for one rate
 */
struct iwl4965_rate_scale_data {
	u64 data;		/* bitmap of successful frames */
	s32 success_counter;	/* number of frames successful */
	s32 success_ratio;	/* per-cent * 128  */
	s32 counter;		/* number of frames attempted */
	s32 average_tpt;	/* success ratio * expected throughput */
	unsigned long stamp;
};

/**
 * struct iwl4965_scale_tbl_info -- tx params and success history for all rates
 *
 * There are two of these in struct iwl4965_lq_sta,
 * one for "active", and one for "search".
 */
struct iwl4965_scale_tbl_info {
	enum iwl4965_table_type lq_type;
	enum iwl4965_antenna_type antenna_type;
	u8 is_SGI;	/* 1 = short guard interval */
	u8 is_fat;	/* 1 = 40 MHz channel width */
	u8 is_dup;	/* 1 = duplicated data streams */
	u8 action;	/* change modulation; IWL_[LEGACY/SISO/MIMO]_SWITCH_* */
	s32 *expected_tpt;	/* throughput metrics; expected_tpt_G, etc. */
	struct iwl4965_rate current_rate;  /* rate_n_flags, uCode API format */
	struct iwl4965_rate_scale_data win[IWL_RATE_COUNT]; /* rate histories */
};

#ifdef CONFIG_IWL4965_HT

struct iwl4965_traffic_load {
	unsigned long time_stamp;	/* age of the oldest statistics */
	u32 packet_count[TID_QUEUE_MAX_SIZE];   /* packet count in this time
						 * slice */
	u32 total;			/* total num of packets during the
					 * last TID_MAX_TIME_DIFF */
	u8 queue_count;			/* number of queues that has
					 * been used since the last cleanup */
	u8 head;			/* start of the circular buffer */
};

#endif /* CONFIG_IWL4965_HT */

/**
 * struct iwl4965_lq_sta -- driver's rate scaling private structure
 *
 * Pointer to this gets passed back and forth between driver and mac80211.
 */
struct iwl4965_lq_sta {
	u8 active_tbl;		/* index of active table, range 0-1 */
	u8 enable_counter;	/* indicates HT mode */
	u8 stay_in_tbl;		/* 1: disallow, 0: allow search for new mode */
	u8 search_better_tbl;	/* 1: currently trying alternate mode */
	s32 last_tpt;

	/* The following determine when to search for a new mode */
	u32 table_count_limit;
	u32 max_failure_limit;	/* # failed frames before new search */
	u32 max_success_limit;	/* # successful frames before new search */
	u32 table_count;
	u32 total_failed;	/* total failed frames, any/all rates */
	u32 total_success;	/* total successful frames, any/all rates */
	u32 flush_timer;	/* time staying in mode before new search */

	u8 action_counter;	/* # mode-switch actions tried */
	u8 antenna;
	u8 valid_antenna;
	u8 is_green;
	u8 is_dup;
	enum ieee80211_band band;
	u8 ibss_sta_added;

	/* The following are bitmaps of rates; IWL_RATE_6M_MASK, etc. */
	u32 supp_rates;
	u16 active_rate;
	u16 active_siso_rate;
	u16 active_mimo_rate;
	u16 active_rate_basic;

	struct iwl_link_quality_cmd lq;
	struct iwl4965_scale_tbl_info lq_info[LQ_SIZE]; /* "active", "search" */
#ifdef CONFIG_IWL4965_HT
	struct iwl4965_traffic_load load[TID_MAX_LOAD_COUNT];
	u8 tx_agg_tid_en;
#endif
#ifdef CONFIG_MAC80211_DEBUGFS
	struct dentry *rs_sta_dbgfs_scale_table_file;
	struct dentry *rs_sta_dbgfs_stats_table_file;
#ifdef CONFIG_IWL4965_HT
	struct dentry *rs_sta_dbgfs_tx_agg_tid_en_file;
#endif
	struct iwl4965_rate dbg_fixed;
	struct iwl_priv *drv;
#endif
};

static void rs_rate_scale_perform(struct iwl_priv *priv,
				   struct net_device *dev,
				   struct ieee80211_hdr *hdr,
				   struct sta_info *sta);
static void rs_fill_link_cmd(struct iwl4965_lq_sta *lq_sta,
			     struct iwl4965_rate *tx_mcs,
			     struct iwl_link_quality_cmd *tbl);


#ifdef CONFIG_MAC80211_DEBUGFS
static void rs_dbgfs_set_mcs(struct iwl4965_lq_sta *lq_sta,
				struct iwl4965_rate *mcs, int index);
#else
static void rs_dbgfs_set_mcs(struct iwl4965_lq_sta *lq_sta,
				struct iwl4965_rate *mcs, int index)
{}
#endif

/*
 * Expected throughput metrics for following rates:
 * 1, 2, 5.5, 11, 6, 9, 12, 18, 24, 36, 48, 54, 60 MBits
 * "G" is the only table that supports CCK (the first 4 rates).
 */
static s32 expected_tpt_A[IWL_RATE_COUNT] = {
	0, 0, 0, 0, 40, 57, 72, 98, 121, 154, 177, 186, 186
};

static s32 expected_tpt_G[IWL_RATE_COUNT] = {
	7, 13, 35, 58, 40, 57, 72, 98, 121, 154, 177, 186, 186
};

static s32 expected_tpt_siso20MHz[IWL_RATE_COUNT] = {
	0, 0, 0, 0, 42, 42, 76, 102, 124, 159, 183, 193, 202
};

static s32 expected_tpt_siso20MHzSGI[IWL_RATE_COUNT] = {
	0, 0, 0, 0, 46, 46, 82, 110, 132, 168, 192, 202, 211
};

static s32 expected_tpt_mimo20MHz[IWL_RATE_COUNT] = {
	0, 0, 0, 0, 74, 74, 123, 155, 179, 214, 236, 244, 251
};

static s32 expected_tpt_mimo20MHzSGI[IWL_RATE_COUNT] = {
	0, 0, 0, 0, 81, 81, 131, 164, 188, 222, 243, 251, 257
};

static s32 expected_tpt_siso40MHz[IWL_RATE_COUNT] = {
	0, 0, 0, 0, 77, 77, 127, 160, 184, 220, 242, 250, 257
};

static s32 expected_tpt_siso40MHzSGI[IWL_RATE_COUNT] = {
	0, 0, 0, 0, 83, 83, 135, 169, 193, 229, 250, 257, 264
};

static s32 expected_tpt_mimo40MHz[IWL_RATE_COUNT] = {
	0, 0, 0, 0, 123, 123, 182, 214, 235, 264, 279, 285, 289
};

static s32 expected_tpt_mimo40MHzSGI[IWL_RATE_COUNT] = {
	0, 0, 0, 0, 131, 131, 191, 222, 242, 270, 284, 289, 293
};

static inline u8 iwl4965_rate_get_rate(u32 rate_n_flags)
{
	return (u8)(rate_n_flags & 0xFF);
}

static void rs_rate_scale_clear_window(struct iwl4965_rate_scale_data *window)
{
	window->data = 0;
	window->success_counter = 0;
	window->success_ratio = IWL_INVALID_VALUE;
	window->counter = 0;
	window->average_tpt = IWL_INVALID_VALUE;
	window->stamp = 0;
}

#ifdef CONFIG_IWL4965_HT
/*
 *	removes the old data from the statistics. All data that is older than
 *	TID_MAX_TIME_DIFF, will be deleted.
 */
static void rs_tl_rm_old_stats(struct iwl4965_traffic_load *tl, u32 curr_time)
{
	/* The oldest age we want to keep */
	u32 oldest_time = curr_time - TID_MAX_TIME_DIFF;

	while (tl->queue_count &&
	       (tl->time_stamp < oldest_time)) {
		tl->total -= tl->packet_count[tl->head];
		tl->packet_count[tl->head] = 0;
		tl->time_stamp += TID_QUEUE_CELL_SPACING;
		tl->queue_count--;
		tl->head++;
		if (tl->head >= TID_QUEUE_MAX_SIZE)
			tl->head = 0;
	}
}

/*
 *	increment traffic load value for tid and also remove
 *	any old values if passed the certain time period
 */
static void rs_tl_add_packet(struct iwl4965_lq_sta *lq_data, u8 tid)
{
	u32 curr_time = jiffies_to_msecs(jiffies);
	u32 time_diff;
	s32 index;
	struct iwl4965_traffic_load *tl = NULL;

	if (tid >= TID_MAX_LOAD_COUNT)
		return;

	tl = &lq_data->load[tid];

	curr_time -= curr_time % TID_ROUND_VALUE;

	/* Happens only for the first packet. Initialize the data */
	if (!(tl->queue_count)) {
		tl->total = 1;
		tl->time_stamp = curr_time;
		tl->queue_count = 1;
		tl->head = 0;
		tl->packet_count[0] = 1;
		return;
	}

	time_diff = TIME_WRAP_AROUND(tl->time_stamp, curr_time);
	index = time_diff / TID_QUEUE_CELL_SPACING;

	/* The history is too long: remove data that is older than */
	/* TID_MAX_TIME_DIFF */
	if (index >= TID_QUEUE_MAX_SIZE)
		rs_tl_rm_old_stats(tl, curr_time);

	index = (tl->head + index) % TID_QUEUE_MAX_SIZE;
	tl->packet_count[index] = tl->packet_count[index] + 1;
	tl->total = tl->total + 1;

	if ((index + 1) > tl->queue_count)
		tl->queue_count = index + 1;
}

/*
	get the traffic load value for tid
*/
static u32 rs_tl_get_load(struct iwl4965_lq_sta *lq_data, u8 tid)
{
	u32 curr_time = jiffies_to_msecs(jiffies);
	u32 time_diff;
	s32 index;
	struct iwl4965_traffic_load *tl = NULL;

	if (tid >= TID_MAX_LOAD_COUNT)
		return 0;

	tl = &(lq_data->load[tid]);

	curr_time -= curr_time % TID_ROUND_VALUE;

	if (!(tl->queue_count))
		return 0;

	time_diff = TIME_WRAP_AROUND(tl->time_stamp, curr_time);
	index = time_diff / TID_QUEUE_CELL_SPACING;

	/* The history is too long: remove data that is older than */
	/* TID_MAX_TIME_DIFF */
	if (index >= TID_QUEUE_MAX_SIZE)
		rs_tl_rm_old_stats(tl, curr_time);

	return tl->total;
}

static void rs_tl_turn_on_agg_for_tid(struct iwl_priv *priv,
				struct iwl4965_lq_sta *lq_data, u8 tid,
				struct sta_info *sta)
{
	unsigned long state;
	DECLARE_MAC_BUF(mac);

	spin_lock_bh(&sta->ampdu_mlme.ampdu_tx);
	state = sta->ampdu_mlme.tid_state_tx[tid];
	spin_unlock_bh(&sta->ampdu_mlme.ampdu_tx);

	if (state == HT_AGG_STATE_IDLE &&
	    rs_tl_get_load(lq_data, tid) > IWL_AGG_LOAD_THRESHOLD) {
		IWL_DEBUG_HT("Starting Tx agg: STA: %s tid: %d\n",
				print_mac(mac, sta->addr), tid);
		ieee80211_start_tx_ba_session(priv->hw, sta->addr, tid);
	}
}

static void rs_tl_turn_on_agg(struct iwl_priv *priv, u8 tid,
				struct iwl4965_lq_sta *lq_data,
				struct sta_info *sta)
{
	if ((tid < TID_MAX_LOAD_COUNT))
		rs_tl_turn_on_agg_for_tid(priv, lq_data, tid, sta);
	else if (tid == IWL_AGG_ALL_TID)
		for (tid = 0; tid < TID_MAX_LOAD_COUNT; tid++)
			rs_tl_turn_on_agg_for_tid(priv, lq_data, tid, sta);
}

#endif /* CONFIG_IWLWIFI_HT */

/**
 * rs_collect_tx_data - Update the success/failure sliding window
 *
 * We keep a sliding window of the last 62 packets transmitted
 * at this rate.  window->data contains the bitmask of successful
 * packets.
 */
static int rs_collect_tx_data(struct iwl4965_rate_scale_data *windows,
			      int scale_index, s32 tpt, int retries,
			      int successes)
{
	struct iwl4965_rate_scale_data *window = NULL;
	u64 mask;
	u8 win_size = IWL_RATE_MAX_WINDOW;
	s32 fail_count;

	if (scale_index < 0 || scale_index >= IWL_RATE_COUNT)
		return -EINVAL;

	/* Select data for current tx bit rate */
	window = &(windows[scale_index]);

	/*
	 * Keep track of only the latest 62 tx frame attempts in this rate's
	 * history window; anything older isn't really relevant any more.
	 * If we have filled up the sliding window, drop the oldest attempt;
	 * if the oldest attempt (highest bit in bitmap) shows "success",
	 * subtract "1" from the success counter (this is the main reason
	 * we keep these bitmaps!).
	 */
	while (retries > 0) {
		if (window->counter >= win_size) {
			window->counter = win_size - 1;
			mask = 1;
			mask = (mask << (win_size - 1));
			if (window->data & mask) {
				window->data &= ~mask;
				window->success_counter =
					window->success_counter - 1;
			}
		}

		/* Increment frames-attempted counter */
		window->counter++;

		/* Shift bitmap by one frame (throw away oldest history),
		 * OR in "1", and increment "success" if this
		 * frame was successful. */
		mask = window->data;
		window->data = (mask << 1);
		if (successes > 0) {
			window->success_counter = window->success_counter + 1;
			window->data |= 0x1;
			successes--;
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
		window->average_tpt = (window->success_ratio * tpt + 64) / 128;
	else
		window->average_tpt = IWL_INVALID_VALUE;

	/* Tag this window as having been updated */
	window->stamp = jiffies;

	return 0;
}

/*
 * Fill uCode API rate_n_flags field, based on "search" or "active" table.
 */
static void rs_mcs_from_tbl(struct iwl4965_rate *mcs_rate,
			   struct iwl4965_scale_tbl_info *tbl,
			   int index, u8 use_green)
{
	if (is_legacy(tbl->lq_type)) {
		mcs_rate->rate_n_flags = iwl4965_rates[index].plcp;
		if (index >= IWL_FIRST_CCK_RATE && index <= IWL_LAST_CCK_RATE)
			mcs_rate->rate_n_flags |= RATE_MCS_CCK_MSK;

	} else if (is_siso(tbl->lq_type)) {
		if (index > IWL_LAST_OFDM_RATE)
			index = IWL_LAST_OFDM_RATE;
		 mcs_rate->rate_n_flags = iwl4965_rates[index].plcp_siso |
					  RATE_MCS_HT_MSK;
	} else {
		if (index > IWL_LAST_OFDM_RATE)
			index = IWL_LAST_OFDM_RATE;
		mcs_rate->rate_n_flags = iwl4965_rates[index].plcp_mimo |
					 RATE_MCS_HT_MSK;
	}

	switch (tbl->antenna_type) {
	case ANT_BOTH:
		mcs_rate->rate_n_flags |= RATE_MCS_ANT_AB_MSK;
		break;
	case ANT_MAIN:
		mcs_rate->rate_n_flags |= RATE_MCS_ANT_A_MSK;
		break;
	case ANT_AUX:
		mcs_rate->rate_n_flags |= RATE_MCS_ANT_B_MSK;
		break;
	case ANT_NONE:
		break;
	}

	if (is_legacy(tbl->lq_type))
		return;

	if (tbl->is_fat) {
		if (tbl->is_dup)
			mcs_rate->rate_n_flags |= RATE_MCS_DUP_MSK;
		else
			mcs_rate->rate_n_flags |= RATE_MCS_FAT_MSK;
	}
	if (tbl->is_SGI)
		mcs_rate->rate_n_flags |= RATE_MCS_SGI_MSK;

	if (use_green) {
		mcs_rate->rate_n_flags |= RATE_MCS_GF_MSK;
		if (is_siso(tbl->lq_type))
			mcs_rate->rate_n_flags &= ~RATE_MCS_SGI_MSK;
	}
}

/*
 * Interpret uCode API's rate_n_flags format,
 * fill "search" or "active" tx mode table.
 */
static int rs_get_tbl_info_from_mcs(const struct iwl4965_rate *mcs_rate,
				    enum ieee80211_band band,
				    struct iwl4965_scale_tbl_info *tbl,
				    int *rate_idx)
{
	int index;
	u32 ant_msk;

	index = iwl4965_hwrate_to_plcp_idx(mcs_rate->rate_n_flags);

	if (index  == IWL_RATE_INVALID) {
		*rate_idx = -1;
		return -EINVAL;
	}
	tbl->is_SGI = 0;	/* default legacy setup */
	tbl->is_fat = 0;
	tbl->is_dup = 0;
	tbl->antenna_type = ANT_BOTH;	/* default MIMO setup */

	/* legacy rate format */
	if (!(mcs_rate->rate_n_flags & RATE_MCS_HT_MSK)) {
		ant_msk = (mcs_rate->rate_n_flags & RATE_MCS_ANT_AB_MSK);

		if (ant_msk == RATE_MCS_ANT_AB_MSK)
			tbl->lq_type = LQ_NONE;
		else {

			if (band == IEEE80211_BAND_5GHZ)
				tbl->lq_type = LQ_A;
			else
				tbl->lq_type = LQ_G;

			if (mcs_rate->rate_n_flags & RATE_MCS_ANT_A_MSK)
				tbl->antenna_type = ANT_MAIN;
			else
				tbl->antenna_type = ANT_AUX;
		}
		*rate_idx = index;

	/* HT rate format, SISO (might be 20 MHz legacy or 40 MHz fat width) */
	} else if (iwl4965_rate_get_rate(mcs_rate->rate_n_flags)
					<= IWL_RATE_SISO_60M_PLCP) {
		tbl->lq_type = LQ_SISO;

		ant_msk = (mcs_rate->rate_n_flags & RATE_MCS_ANT_AB_MSK);
		if (ant_msk == RATE_MCS_ANT_AB_MSK)
			tbl->lq_type = LQ_NONE;
		else {
			if (mcs_rate->rate_n_flags & RATE_MCS_ANT_A_MSK)
				tbl->antenna_type = ANT_MAIN;
			else
				tbl->antenna_type = ANT_AUX;
		}
		if (mcs_rate->rate_n_flags & RATE_MCS_SGI_MSK)
			tbl->is_SGI = 1;

		if ((mcs_rate->rate_n_flags & RATE_MCS_FAT_MSK) ||
		    (mcs_rate->rate_n_flags & RATE_MCS_DUP_MSK))
			tbl->is_fat = 1;

		if (mcs_rate->rate_n_flags & RATE_MCS_DUP_MSK)
			tbl->is_dup = 1;

		*rate_idx = index;

	/* HT rate format, MIMO (might be 20 MHz legacy or 40 MHz fat width) */
	} else {
		tbl->lq_type = LQ_MIMO;
		if (mcs_rate->rate_n_flags & RATE_MCS_SGI_MSK)
			tbl->is_SGI = 1;

		if ((mcs_rate->rate_n_flags & RATE_MCS_FAT_MSK) ||
		    (mcs_rate->rate_n_flags & RATE_MCS_DUP_MSK))
			tbl->is_fat = 1;

		if (mcs_rate->rate_n_flags & RATE_MCS_DUP_MSK)
			tbl->is_dup = 1;
		*rate_idx = index;
	}
	return 0;
}

static inline void rs_toggle_antenna(struct iwl4965_rate *new_rate,
				     struct iwl4965_scale_tbl_info *tbl)
{
	if (tbl->antenna_type == ANT_AUX) {
		tbl->antenna_type = ANT_MAIN;
		new_rate->rate_n_flags &= ~RATE_MCS_ANT_B_MSK;
		new_rate->rate_n_flags |= RATE_MCS_ANT_A_MSK;
	} else {
		tbl->antenna_type = ANT_AUX;
		new_rate->rate_n_flags &= ~RATE_MCS_ANT_A_MSK;
		new_rate->rate_n_flags |= RATE_MCS_ANT_B_MSK;
	}
}

static inline u8 rs_use_green(struct iwl_priv *priv,
			      struct ieee80211_conf *conf)
{
#ifdef CONFIG_IWL4965_HT
	return ((conf->flags & IEEE80211_CONF_SUPPORT_HT_MODE) &&
		priv->current_ht_config.is_green_field &&
		!priv->current_ht_config.non_GF_STA_present);
#endif	/* CONFIG_IWL4965_HT */
	return 0;
}

/**
 * rs_get_supported_rates - get the available rates
 *
 * if management frame or broadcast frame only return
 * basic available rates.
 *
 */
static void rs_get_supported_rates(struct iwl4965_lq_sta *lq_sta,
				   struct ieee80211_hdr *hdr,
				   enum iwl4965_table_type rate_type,
				   u16 *data_rate)
{
	if (is_legacy(rate_type))
		*data_rate = lq_sta->active_rate;
	else {
		if (is_siso(rate_type))
			*data_rate = lq_sta->active_siso_rate;
		else
			*data_rate = lq_sta->active_mimo_rate;
	}

	if (hdr && is_multicast_ether_addr(hdr->addr1) &&
	    lq_sta->active_rate_basic) {
		*data_rate = lq_sta->active_rate_basic;
	}
}

static u16 rs_get_adjacent_rate(u8 index, u16 rate_mask, int rate_type)
{
	u8 high = IWL_RATE_INVALID;
	u8 low = IWL_RATE_INVALID;

	/* 802.11A or ht walks to the next literal adjacent rate in
	 * the rate table */
	if (is_a_band(rate_type) || !is_legacy(rate_type)) {
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
		low = iwl4965_rates[low].prev_rs;
		if (low == IWL_RATE_INVALID)
			break;
		if (rate_mask & (1 << low))
			break;
		IWL_DEBUG_RATE("Skipping masked lower rate: %d\n", low);
	}

	high = index;
	while (high != IWL_RATE_INVALID) {
		high = iwl4965_rates[high].next_rs;
		if (high == IWL_RATE_INVALID)
			break;
		if (rate_mask & (1 << high))
			break;
		IWL_DEBUG_RATE("Skipping masked higher rate: %d\n", high);
	}

	return (high << 8) | low;
}

static void rs_get_lower_rate(struct iwl4965_lq_sta *lq_sta,
			     struct iwl4965_scale_tbl_info *tbl, u8 scale_index,
			     u8 ht_possible, struct iwl4965_rate *mcs_rate)
{
	s32 low;
	u16 rate_mask;
	u16 high_low;
	u8 switch_to_legacy = 0;
	u8 is_green = lq_sta->is_green;

	/* check if we need to switch from HT to legacy rates.
	 * assumption is that mandatory rates (1Mbps or 6Mbps)
	 * are always supported (spec demand) */
	if (!is_legacy(tbl->lq_type) && (!ht_possible || !scale_index)) {
		switch_to_legacy = 1;
		scale_index = rs_ht_to_legacy[scale_index];
		if (lq_sta->band == IEEE80211_BAND_5GHZ)
			tbl->lq_type = LQ_A;
		else
			tbl->lq_type = LQ_G;

		if ((tbl->antenna_type == ANT_BOTH) ||
		    (tbl->antenna_type == ANT_NONE))
			tbl->antenna_type = ANT_MAIN;

		tbl->is_fat = 0;
		tbl->is_SGI = 0;
	}

	rs_get_supported_rates(lq_sta, NULL, tbl->lq_type, &rate_mask);

	/* Mask with station rate restriction */
	if (is_legacy(tbl->lq_type)) {
		/* supp_rates has no CCK bits in A mode */
		if (lq_sta->band == IEEE80211_BAND_5GHZ)
			rate_mask  = (u16)(rate_mask &
			   (lq_sta->supp_rates << IWL_FIRST_OFDM_RATE));
		else
			rate_mask = (u16)(rate_mask & lq_sta->supp_rates);
	}

	/* If we switched from HT to legacy, check current rate */
	if (switch_to_legacy && (rate_mask & (1 << scale_index))) {
		rs_mcs_from_tbl(mcs_rate, tbl, scale_index, is_green);
		return;
	}

	high_low = rs_get_adjacent_rate(scale_index, rate_mask, tbl->lq_type);
	low = high_low & 0xff;

	if (low != IWL_RATE_INVALID)
		rs_mcs_from_tbl(mcs_rate, tbl, low, is_green);
	else
		rs_mcs_from_tbl(mcs_rate, tbl, scale_index, is_green);
}

/*
 * mac80211 sends us Tx status
 */
static void rs_tx_status(void *priv_rate, struct net_device *dev,
			 struct sk_buff *skb,
			 struct ieee80211_tx_status *tx_resp)
{
	int status;
	u8 retries;
	int rs_index, index = 0;
	struct iwl4965_lq_sta *lq_sta;
	struct iwl_link_quality_cmd *table;
	struct sta_info *sta;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct iwl_priv *priv = (struct iwl_priv *)priv_rate;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_hw *hw = local_to_hw(local);
	struct iwl4965_rate_scale_data *window = NULL;
	struct iwl4965_rate_scale_data *search_win = NULL;
	struct iwl4965_rate tx_mcs;
	struct iwl4965_scale_tbl_info tbl_type;
	struct iwl4965_scale_tbl_info *curr_tbl, *search_tbl;
	u8 active_index = 0;
	u16 fc = le16_to_cpu(hdr->frame_control);
	s32 tpt = 0;

	IWL_DEBUG_RATE_LIMIT("get frame ack response, update rate scale window\n");

	if (!ieee80211_is_data(fc) || is_multicast_ether_addr(hdr->addr1))
		return;

	/* This packet was aggregated but doesn't carry rate scale info */
	if ((tx_resp->control.flags & IEEE80211_TXCTL_AMPDU) &&
	    !(tx_resp->flags & IEEE80211_TX_STATUS_AMPDU))
		return;

	retries = tx_resp->retry_count;

	if (retries > 15)
		retries = 15;

	rcu_read_lock();

	sta = sta_info_get(local, hdr->addr1);

	if (!sta || !sta->rate_ctrl_priv)
		goto out;


	lq_sta = (struct iwl4965_lq_sta *)sta->rate_ctrl_priv;

	if (!priv->lq_mngr.lq_ready)
		goto out;

	if ((priv->iw_mode == IEEE80211_IF_TYPE_IBSS) &&
	    !lq_sta->ibss_sta_added)
		goto out;

	table = &lq_sta->lq;
	active_index = lq_sta->active_tbl;

	/* Get mac80211 antenna info */
	lq_sta->antenna =
		(lq_sta->valid_antenna & local->hw.conf.antenna_sel_tx);
	if (!lq_sta->antenna)
		lq_sta->antenna = lq_sta->valid_antenna;

	/* Ignore mac80211 antenna info for now */
	lq_sta->antenna = lq_sta->valid_antenna;

	curr_tbl = &(lq_sta->lq_info[active_index]);
	search_tbl = &(lq_sta->lq_info[(1 - active_index)]);
	window = (struct iwl4965_rate_scale_data *)
	    &(curr_tbl->win[0]);
	search_win = (struct iwl4965_rate_scale_data *)
	    &(search_tbl->win[0]);

	/*
	 * Ignore this Tx frame response if its initial rate doesn't match
	 * that of latest Link Quality command.  There may be stragglers
	 * from a previous Link Quality command, but we're no longer interested
	 * in those; they're either from the "active" mode while we're trying
	 * to check "search" mode, or a prior "search" mode after we've moved
	 * to a new "search" mode (which might become the new "active" mode).
	 */
	tx_mcs.rate_n_flags = le32_to_cpu(table->rs_table[0].rate_n_flags);
	rs_get_tbl_info_from_mcs(&tx_mcs, priv->band, &tbl_type, &rs_index);
	if (priv->band == IEEE80211_BAND_5GHZ)
		rs_index -= IWL_FIRST_OFDM_RATE;

	if ((tx_resp->control.tx_rate == NULL) ||
	    (tbl_type.is_SGI ^
		!!(tx_resp->control.flags & IEEE80211_TXCTL_SHORT_GI)) ||
	    (tbl_type.is_fat ^
		!!(tx_resp->control.flags & IEEE80211_TXCTL_40_MHZ_WIDTH)) ||
	    (tbl_type.is_dup ^
		!!(tx_resp->control.flags & IEEE80211_TXCTL_DUP_DATA)) ||
	    (tbl_type.antenna_type ^
		tx_resp->control.antenna_sel_tx) ||
	    (!!(tx_mcs.rate_n_flags & RATE_MCS_HT_MSK) ^
		!!(tx_resp->control.flags & IEEE80211_TXCTL_OFDM_HT)) ||
	    (!!(tx_mcs.rate_n_flags & RATE_MCS_GF_MSK) ^
		!!(tx_resp->control.flags & IEEE80211_TXCTL_GREEN_FIELD)) ||
	    (hw->wiphy->bands[priv->band]->bitrates[rs_index].bitrate !=
		tx_resp->control.tx_rate->bitrate)) {
		IWL_DEBUG_RATE("initial rate does not match 0x%x\n",
				tx_mcs.rate_n_flags);
		goto out;
	}

	/* Update frame history window with "failure" for each Tx retry. */
	while (retries) {
		/* Look up the rate and other info used for each tx attempt.
		 * Each tx attempt steps one entry deeper in the rate table. */
		tx_mcs.rate_n_flags =
		    le32_to_cpu(table->rs_table[index].rate_n_flags);
		rs_get_tbl_info_from_mcs(&tx_mcs, priv->band,
					  &tbl_type, &rs_index);

		/* If type matches "search" table,
		 * add failure to "search" history */
		if ((tbl_type.lq_type == search_tbl->lq_type) &&
		    (tbl_type.antenna_type == search_tbl->antenna_type) &&
		    (tbl_type.is_SGI == search_tbl->is_SGI)) {
			if (search_tbl->expected_tpt)
				tpt = search_tbl->expected_tpt[rs_index];
			else
				tpt = 0;
			rs_collect_tx_data(search_win, rs_index, tpt, 1, 0);

		/* Else if type matches "current/active" table,
		 * add failure to "current/active" history */
		} else if ((tbl_type.lq_type == curr_tbl->lq_type) &&
			   (tbl_type.antenna_type == curr_tbl->antenna_type) &&
			   (tbl_type.is_SGI == curr_tbl->is_SGI)) {
			if (curr_tbl->expected_tpt)
				tpt = curr_tbl->expected_tpt[rs_index];
			else
				tpt = 0;
			rs_collect_tx_data(window, rs_index, tpt, 1, 0);
		}

		/* If not searching for a new mode, increment failed counter
		 * ... this helps determine when to start searching again */
		if (lq_sta->stay_in_tbl)
			lq_sta->total_failed++;
		--retries;
		index++;

	}

	/*
	 * Find (by rate) the history window to update with final Tx attempt;
	 * if Tx was successful first try, use original rate,
	 * else look up the rate that was, finally, successful.
	 */
	tx_mcs.rate_n_flags = le32_to_cpu(table->rs_table[index].rate_n_flags);
	rs_get_tbl_info_from_mcs(&tx_mcs, priv->band, &tbl_type, &rs_index);

	/* Update frame history window with "success" if Tx got ACKed ... */
	if (tx_resp->flags & IEEE80211_TX_STATUS_ACK)
		status = 1;
	else
		status = 0;

	/* If type matches "search" table,
	 * add final tx status to "search" history */
	if ((tbl_type.lq_type == search_tbl->lq_type) &&
	    (tbl_type.antenna_type == search_tbl->antenna_type) &&
	    (tbl_type.is_SGI == search_tbl->is_SGI)) {
		if (search_tbl->expected_tpt)
			tpt = search_tbl->expected_tpt[rs_index];
		else
			tpt = 0;
		if (tx_resp->control.flags & IEEE80211_TXCTL_AMPDU)
			rs_collect_tx_data(search_win, rs_index, tpt,
					   tx_resp->ampdu_ack_len,
					   tx_resp->ampdu_ack_map);
		else
			rs_collect_tx_data(search_win, rs_index, tpt,
					   1, status);
	/* Else if type matches "current/active" table,
	 * add final tx status to "current/active" history */
	} else if ((tbl_type.lq_type == curr_tbl->lq_type) &&
		   (tbl_type.antenna_type == curr_tbl->antenna_type) &&
		   (tbl_type.is_SGI == curr_tbl->is_SGI)) {
		if (curr_tbl->expected_tpt)
			tpt = curr_tbl->expected_tpt[rs_index];
		else
			tpt = 0;
		if (tx_resp->control.flags & IEEE80211_TXCTL_AMPDU)
			rs_collect_tx_data(window, rs_index, tpt,
					   tx_resp->ampdu_ack_len,
					   tx_resp->ampdu_ack_map);
		else
			rs_collect_tx_data(window, rs_index, tpt,
					   1, status);
	}

	/* If not searching for new mode, increment success/failed counter
	 * ... these help determine when to start searching again */
	if (lq_sta->stay_in_tbl) {
		if (tx_resp->control.flags & IEEE80211_TXCTL_AMPDU) {
			lq_sta->total_success += tx_resp->ampdu_ack_map;
			lq_sta->total_failed +=
			     (tx_resp->ampdu_ack_len - tx_resp->ampdu_ack_map);
		} else {
			if (status)
				lq_sta->total_success++;
			else
				lq_sta->total_failed++;
		}
	}

	/* See if there's a better rate or modulation mode to try. */
	rs_rate_scale_perform(priv, dev, hdr, sta);
out:
	rcu_read_unlock();
	return;
}

static u8 rs_is_ant_connected(u8 valid_antenna,
			      enum iwl4965_antenna_type antenna_type)
{
	if (antenna_type == ANT_AUX)
		return ((valid_antenna & 0x2) ? 1:0);
	else if (antenna_type == ANT_MAIN)
		return ((valid_antenna & 0x1) ? 1:0);
	else if (antenna_type == ANT_BOTH)
		return ((valid_antenna & 0x3) == 0x3);

	return 1;
}

static u8 rs_is_other_ant_connected(u8 valid_antenna,
				    enum iwl4965_antenna_type antenna_type)
{
	if (antenna_type == ANT_AUX)
		return rs_is_ant_connected(valid_antenna, ANT_MAIN);
	else
		return rs_is_ant_connected(valid_antenna, ANT_AUX);

	return 0;
}

/*
 * Begin a period of staying with a selected modulation mode.
 * Set "stay_in_tbl" flag to prevent any mode switches.
 * Set frame tx success limits according to legacy vs. high-throughput,
 * and reset overall (spanning all rates) tx success history statistics.
 * These control how long we stay using same modulation mode before
 * searching for a new mode.
 */
static void rs_set_stay_in_table(u8 is_legacy,
				 struct iwl4965_lq_sta *lq_sta)
{
	IWL_DEBUG_HT("we are staying in the same table\n");
	lq_sta->stay_in_tbl = 1;	/* only place this gets set */
	if (is_legacy) {
		lq_sta->table_count_limit = IWL_LEGACY_TABLE_COUNT;
		lq_sta->max_failure_limit = IWL_LEGACY_FAILURE_LIMIT;
		lq_sta->max_success_limit = IWL_LEGACY_SUCCESS_LIMIT;
	} else {
		lq_sta->table_count_limit = IWL_NONE_LEGACY_TABLE_COUNT;
		lq_sta->max_failure_limit = IWL_NONE_LEGACY_FAILURE_LIMIT;
		lq_sta->max_success_limit = IWL_NONE_LEGACY_SUCCESS_LIMIT;
	}
	lq_sta->table_count = 0;
	lq_sta->total_failed = 0;
	lq_sta->total_success = 0;
}

/*
 * Find correct throughput table for given mode of modulation
 */
static void rs_get_expected_tpt_table(struct iwl4965_lq_sta *lq_sta,
				      struct iwl4965_scale_tbl_info *tbl)
{
	if (is_legacy(tbl->lq_type)) {
		if (!is_a_band(tbl->lq_type))
			tbl->expected_tpt = expected_tpt_G;
		else
			tbl->expected_tpt = expected_tpt_A;
	} else if (is_siso(tbl->lq_type)) {
		if (tbl->is_fat && !lq_sta->is_dup)
			if (tbl->is_SGI)
				tbl->expected_tpt = expected_tpt_siso40MHzSGI;
			else
				tbl->expected_tpt = expected_tpt_siso40MHz;
		else if (tbl->is_SGI)
			tbl->expected_tpt = expected_tpt_siso20MHzSGI;
		else
			tbl->expected_tpt = expected_tpt_siso20MHz;

	} else if (is_mimo(tbl->lq_type)) {
		if (tbl->is_fat && !lq_sta->is_dup)
			if (tbl->is_SGI)
				tbl->expected_tpt = expected_tpt_mimo40MHzSGI;
			else
				tbl->expected_tpt = expected_tpt_mimo40MHz;
		else if (tbl->is_SGI)
			tbl->expected_tpt = expected_tpt_mimo20MHzSGI;
		else
			tbl->expected_tpt = expected_tpt_mimo20MHz;
	} else
		tbl->expected_tpt = expected_tpt_G;
}

#ifdef CONFIG_IWL4965_HT
/*
 * Find starting rate for new "search" high-throughput mode of modulation.
 * Goal is to find lowest expected rate (under perfect conditions) that is
 * above the current measured throughput of "active" mode, to give new mode
 * a fair chance to prove itself without too many challenges.
 *
 * This gets called when transitioning to more aggressive modulation
 * (i.e. legacy to SISO or MIMO, or SISO to MIMO), as well as less aggressive
 * (i.e. MIMO to SISO).  When moving to MIMO, bit rate will typically need
 * to decrease to match "active" throughput.  When moving from MIMO to SISO,
 * bit rate will typically need to increase, but not if performance was bad.
 */
static s32 rs_get_best_rate(struct iwl_priv *priv,
			    struct iwl4965_lq_sta *lq_sta,
			    struct iwl4965_scale_tbl_info *tbl,	/* "search" */
			    u16 rate_mask, s8 index, s8 rate)
{
	/* "active" values */
	struct iwl4965_scale_tbl_info *active_tbl =
	    &(lq_sta->lq_info[lq_sta->active_tbl]);
	s32 active_sr = active_tbl->win[index].success_ratio;
	s32 active_tpt = active_tbl->expected_tpt[index];

	/* expected "search" throughput */
	s32 *tpt_tbl = tbl->expected_tpt;

	s32 new_rate, high, low, start_hi;
	u16 high_low;

	new_rate = high = low = start_hi = IWL_RATE_INVALID;

	for (; ;) {
		high_low = rs_get_adjacent_rate(rate, rate_mask, tbl->lq_type);

		low = high_low & 0xff;
		high = (high_low >> 8) & 0xff;

		/*
		 * Lower the "search" bit rate, to give new "search" mode
		 * approximately the same throughput as "active" if:
		 *
		 * 1) "Active" mode has been working modestly well (but not
		 *    great), and expected "search" throughput (under perfect
		 *    conditions) at candidate rate is above the actual
		 *    measured "active" throughput (but less than expected
		 *    "active" throughput under perfect conditions).
		 * OR
		 * 2) "Active" mode has been working perfectly or very well
		 *    and expected "search" throughput (under perfect
		 *    conditions) at candidate rate is above expected
		 *    "active" throughput (under perfect conditions).
		 */
		if ((((100 * tpt_tbl[rate]) > lq_sta->last_tpt) &&
		     ((active_sr > IWL_RATE_DECREASE_TH) &&
		      (active_sr <= IWL_RATE_HIGH_TH) &&
		      (tpt_tbl[rate] <= active_tpt))) ||
		    ((active_sr >= IWL_RATE_SCALE_SWITCH) &&
		     (tpt_tbl[rate] > active_tpt))) {

			/* (2nd or later pass)
			 * If we've already tried to raise the rate, and are
			 * now trying to lower it, use the higher rate. */
			if (start_hi != IWL_RATE_INVALID) {
				new_rate = start_hi;
				break;
			}

			new_rate = rate;

			/* Loop again with lower rate */
			if (low != IWL_RATE_INVALID)
				rate = low;

			/* Lower rate not available, use the original */
			else
				break;

		/* Else try to raise the "search" rate to match "active" */
		} else {
			/* (2nd or later pass)
			 * If we've already tried to lower the rate, and are
			 * now trying to raise it, use the lower rate. */
			if (new_rate != IWL_RATE_INVALID)
				break;

			/* Loop again with higher rate */
			else if (high != IWL_RATE_INVALID) {
				start_hi = high;
				rate = high;

			/* Higher rate not available, use the original */
			} else {
				new_rate = rate;
				break;
			}
		}
	}

	return new_rate;
}
#endif				/* CONFIG_IWL4965_HT */

static inline u8 rs_is_both_ant_supp(u8 valid_antenna)
{
	return (rs_is_ant_connected(valid_antenna, ANT_BOTH));
}

/*
 * Set up search table for MIMO
 */
static int rs_switch_to_mimo(struct iwl_priv *priv,
			     struct iwl4965_lq_sta *lq_sta,
			     struct ieee80211_conf *conf,
			     struct sta_info *sta,
			     struct iwl4965_scale_tbl_info *tbl, int index)
{
#ifdef CONFIG_IWL4965_HT
	u16 rate_mask;
	s32 rate;
	s8 is_green = lq_sta->is_green;

	if (!(conf->flags & IEEE80211_CONF_SUPPORT_HT_MODE) ||
	    !sta->ht_info.ht_supported)
		return -1;

	IWL_DEBUG_HT("LQ: try to switch to MIMO\n");
	tbl->lq_type = LQ_MIMO;
	rs_get_supported_rates(lq_sta, NULL, tbl->lq_type,
				&rate_mask);

	if (priv->current_ht_config.tx_mimo_ps_mode == IWL_MIMO_PS_STATIC)
		return -1;

	/* Need both Tx chains/antennas to support MIMO */
	if (!rs_is_both_ant_supp(lq_sta->antenna))
		return -1;

	tbl->is_dup = lq_sta->is_dup;
	tbl->action = 0;
	if (priv->current_ht_config.supported_chan_width
	    == IWL_CHANNEL_WIDTH_40MHZ)
		tbl->is_fat = 1;
	else
		tbl->is_fat = 0;

	if (tbl->is_fat) {
		if (priv->current_ht_config.sgf & HT_SHORT_GI_40MHZ_ONLY)
			tbl->is_SGI = 1;
		else
			tbl->is_SGI = 0;
	} else if (priv->current_ht_config.sgf & HT_SHORT_GI_20MHZ_ONLY)
		tbl->is_SGI = 1;
	else
		tbl->is_SGI = 0;

	rs_get_expected_tpt_table(lq_sta, tbl);

	rate = rs_get_best_rate(priv, lq_sta, tbl, rate_mask, index, index);

	IWL_DEBUG_HT("LQ: MIMO best rate %d mask %X\n", rate, rate_mask);
	if ((rate == IWL_RATE_INVALID) || !((1 << rate) & rate_mask))
		return -1;
	rs_mcs_from_tbl(&tbl->current_rate, tbl, rate, is_green);

	IWL_DEBUG_HT("LQ: Switch to new mcs %X index is green %X\n",
		     tbl->current_rate.rate_n_flags, is_green);
	return 0;
#else
	return -1;
#endif	/*CONFIG_IWL4965_HT */
}

/*
 * Set up search table for SISO
 */
static int rs_switch_to_siso(struct iwl_priv *priv,
			     struct iwl4965_lq_sta *lq_sta,
			     struct ieee80211_conf *conf,
			     struct sta_info *sta,
			     struct iwl4965_scale_tbl_info *tbl, int index)
{
#ifdef CONFIG_IWL4965_HT
	u16 rate_mask;
	u8 is_green = lq_sta->is_green;
	s32 rate;

	IWL_DEBUG_HT("LQ: try to switch to SISO\n");
	if (!(conf->flags & IEEE80211_CONF_SUPPORT_HT_MODE) ||
	    !sta->ht_info.ht_supported)
		return -1;

	tbl->is_dup = lq_sta->is_dup;
	tbl->lq_type = LQ_SISO;
	tbl->action = 0;
	rs_get_supported_rates(lq_sta, NULL, tbl->lq_type,
				&rate_mask);

	if (priv->current_ht_config.supported_chan_width
	    == IWL_CHANNEL_WIDTH_40MHZ)
		tbl->is_fat = 1;
	else
		tbl->is_fat = 0;

	if (tbl->is_fat) {
		if (priv->current_ht_config.sgf & HT_SHORT_GI_40MHZ_ONLY)
			tbl->is_SGI = 1;
		else
			tbl->is_SGI = 0;
	} else if (priv->current_ht_config.sgf & HT_SHORT_GI_20MHZ_ONLY)
		tbl->is_SGI = 1;
	else
		tbl->is_SGI = 0;

	if (is_green)
		tbl->is_SGI = 0;

	rs_get_expected_tpt_table(lq_sta, tbl);
	rate = rs_get_best_rate(priv, lq_sta, tbl, rate_mask, index, index);

	IWL_DEBUG_HT("LQ: get best rate %d mask %X\n", rate, rate_mask);
	if ((rate == IWL_RATE_INVALID) || !((1 << rate) & rate_mask)) {
		IWL_DEBUG_HT("can not switch with index %d rate mask %x\n",
			     rate, rate_mask);
		return -1;
	}
	rs_mcs_from_tbl(&tbl->current_rate, tbl, rate, is_green);
	IWL_DEBUG_HT("LQ: Switch to new mcs %X index is green %X\n",
		     tbl->current_rate.rate_n_flags, is_green);
	return 0;
#else
	return -1;

#endif	/*CONFIG_IWL4965_HT */
}

/*
 * Try to switch to new modulation mode from legacy
 */
static int rs_move_legacy_other(struct iwl_priv *priv,
				struct iwl4965_lq_sta *lq_sta,
				struct ieee80211_conf *conf,
				struct sta_info *sta,
				int index)
{
	int ret = 0;
	struct iwl4965_scale_tbl_info *tbl =
	    &(lq_sta->lq_info[lq_sta->active_tbl]);
	struct iwl4965_scale_tbl_info *search_tbl =
	    &(lq_sta->lq_info[(1 - lq_sta->active_tbl)]);
	struct iwl4965_rate_scale_data *window = &(tbl->win[index]);
	u32 sz = (sizeof(struct iwl4965_scale_tbl_info) -
		  (sizeof(struct iwl4965_rate_scale_data) * IWL_RATE_COUNT));
	u8 start_action = tbl->action;

	for (; ;) {
		switch (tbl->action) {
		case IWL_LEGACY_SWITCH_ANTENNA:
			IWL_DEBUG_HT("LQ Legacy switch Antenna\n");

			search_tbl->lq_type = LQ_NONE;
			lq_sta->action_counter++;

			/* Don't change antenna if success has been great */
			if (window->success_ratio >= IWL_RS_GOOD_RATIO)
				break;

			/* Don't change antenna if other one is not connected */
			if (!rs_is_other_ant_connected(lq_sta->antenna,
							tbl->antenna_type))
				break;

			/* Set up search table to try other antenna */
			memcpy(search_tbl, tbl, sz);

			rs_toggle_antenna(&(search_tbl->current_rate),
					   search_tbl);
			rs_get_expected_tpt_table(lq_sta, search_tbl);
			lq_sta->search_better_tbl = 1;
			goto out;

		case IWL_LEGACY_SWITCH_SISO:
			IWL_DEBUG_HT("LQ: Legacy switch to SISO\n");

			/* Set up search table to try SISO */
			memcpy(search_tbl, tbl, sz);
			search_tbl->lq_type = LQ_SISO;
			search_tbl->is_SGI = 0;
			search_tbl->is_fat = 0;
			ret = rs_switch_to_siso(priv, lq_sta, conf, sta,
						 search_tbl, index);
			if (!ret) {
				lq_sta->search_better_tbl = 1;
				lq_sta->action_counter = 0;
				goto out;
			}

			break;
		case IWL_LEGACY_SWITCH_MIMO:
			IWL_DEBUG_HT("LQ: Legacy switch MIMO\n");

			/* Set up search table to try MIMO */
			memcpy(search_tbl, tbl, sz);
			search_tbl->lq_type = LQ_MIMO;
			search_tbl->is_SGI = 0;
			search_tbl->is_fat = 0;
			search_tbl->antenna_type = ANT_BOTH;
			ret = rs_switch_to_mimo(priv, lq_sta, conf, sta,
						 search_tbl, index);
			if (!ret) {
				lq_sta->search_better_tbl = 1;
				lq_sta->action_counter = 0;
				goto out;
			}
			break;
		}
		tbl->action++;
		if (tbl->action > IWL_LEGACY_SWITCH_MIMO)
			tbl->action = IWL_LEGACY_SWITCH_ANTENNA;

		if (tbl->action == start_action)
			break;

	}
	return 0;

 out:
	tbl->action++;
	if (tbl->action > IWL_LEGACY_SWITCH_MIMO)
		tbl->action = IWL_LEGACY_SWITCH_ANTENNA;
	return 0;

}

/*
 * Try to switch to new modulation mode from SISO
 */
static int rs_move_siso_to_other(struct iwl_priv *priv,
				 struct iwl4965_lq_sta *lq_sta,
				 struct ieee80211_conf *conf,
				 struct sta_info *sta,
				 int index)
{
	int ret;
	u8 is_green = lq_sta->is_green;
	struct iwl4965_scale_tbl_info *tbl =
	    &(lq_sta->lq_info[lq_sta->active_tbl]);
	struct iwl4965_scale_tbl_info *search_tbl =
	    &(lq_sta->lq_info[(1 - lq_sta->active_tbl)]);
	struct iwl4965_rate_scale_data *window = &(tbl->win[index]);
	u32 sz = (sizeof(struct iwl4965_scale_tbl_info) -
		  (sizeof(struct iwl4965_rate_scale_data) * IWL_RATE_COUNT));
	u8 start_action = tbl->action;

	for (;;) {
		lq_sta->action_counter++;
		switch (tbl->action) {
		case IWL_SISO_SWITCH_ANTENNA:
			IWL_DEBUG_HT("LQ: SISO SWITCH ANTENNA SISO\n");
			search_tbl->lq_type = LQ_NONE;
			if (window->success_ratio >= IWL_RS_GOOD_RATIO)
				break;
			if (!rs_is_other_ant_connected(lq_sta->antenna,
						       tbl->antenna_type))
				break;

			memcpy(search_tbl, tbl, sz);
			search_tbl->action = IWL_SISO_SWITCH_MIMO;
			rs_toggle_antenna(&(search_tbl->current_rate),
					   search_tbl);
			lq_sta->search_better_tbl = 1;

			goto out;

		case IWL_SISO_SWITCH_MIMO:
			IWL_DEBUG_HT("LQ: SISO SWITCH TO MIMO FROM SISO\n");
			memcpy(search_tbl, tbl, sz);
			search_tbl->lq_type = LQ_MIMO;
			search_tbl->is_SGI = 0;
			search_tbl->is_fat = 0;
			search_tbl->antenna_type = ANT_BOTH;
			ret = rs_switch_to_mimo(priv, lq_sta, conf, sta,
						 search_tbl, index);
			if (!ret) {
				lq_sta->search_better_tbl = 1;
				goto out;
			}
			break;
		case IWL_SISO_SWITCH_GI:
			IWL_DEBUG_HT("LQ: SISO SWITCH TO GI\n");

			memcpy(search_tbl, tbl, sz);
			search_tbl->action = 0;
			if (search_tbl->is_SGI)
				search_tbl->is_SGI = 0;
			else if (!is_green)
				search_tbl->is_SGI = 1;
			else
				break;
			lq_sta->search_better_tbl = 1;
			if ((tbl->lq_type == LQ_SISO) &&
			    (tbl->is_SGI)) {
				s32 tpt = lq_sta->last_tpt / 100;
				if (((!tbl->is_fat) &&
				     (tpt >= expected_tpt_siso20MHz[index])) ||
				    ((tbl->is_fat) &&
				     (tpt >= expected_tpt_siso40MHz[index])))
					lq_sta->search_better_tbl = 0;
			}
			rs_get_expected_tpt_table(lq_sta, search_tbl);
			rs_mcs_from_tbl(&search_tbl->current_rate,
					     search_tbl, index, is_green);
			goto out;
		}
		tbl->action++;
		if (tbl->action > IWL_SISO_SWITCH_GI)
			tbl->action = IWL_SISO_SWITCH_ANTENNA;

		if (tbl->action == start_action)
			break;
	}
	return 0;

 out:
	tbl->action++;
	if (tbl->action > IWL_SISO_SWITCH_GI)
		tbl->action = IWL_SISO_SWITCH_ANTENNA;
	return 0;
}

/*
 * Try to switch to new modulation mode from MIMO
 */
static int rs_move_mimo_to_other(struct iwl_priv *priv,
				 struct iwl4965_lq_sta *lq_sta,
				 struct ieee80211_conf *conf,
				 struct sta_info *sta,
				 int index)
{
	int ret;
	s8 is_green = lq_sta->is_green;
	struct iwl4965_scale_tbl_info *tbl =
	    &(lq_sta->lq_info[lq_sta->active_tbl]);
	struct iwl4965_scale_tbl_info *search_tbl =
	    &(lq_sta->lq_info[(1 - lq_sta->active_tbl)]);
	u32 sz = (sizeof(struct iwl4965_scale_tbl_info) -
		  (sizeof(struct iwl4965_rate_scale_data) * IWL_RATE_COUNT));
	u8 start_action = tbl->action;

	for (;;) {
		lq_sta->action_counter++;
		switch (tbl->action) {
		case IWL_MIMO_SWITCH_ANTENNA_A:
		case IWL_MIMO_SWITCH_ANTENNA_B:
			IWL_DEBUG_HT("LQ: MIMO SWITCH TO SISO\n");


			/* Set up new search table for SISO */
			memcpy(search_tbl, tbl, sz);
			search_tbl->lq_type = LQ_SISO;
			search_tbl->is_SGI = 0;
			search_tbl->is_fat = 0;
			if (tbl->action == IWL_MIMO_SWITCH_ANTENNA_A)
				search_tbl->antenna_type = ANT_MAIN;
			else
				search_tbl->antenna_type = ANT_AUX;

			ret = rs_switch_to_siso(priv, lq_sta, conf, sta,
						 search_tbl, index);
			if (!ret) {
				lq_sta->search_better_tbl = 1;
				goto out;
			}
			break;

		case IWL_MIMO_SWITCH_GI:
			IWL_DEBUG_HT("LQ: MIMO SWITCH TO GI\n");

			/* Set up new search table for MIMO */
			memcpy(search_tbl, tbl, sz);
			search_tbl->lq_type = LQ_MIMO;
			search_tbl->antenna_type = ANT_BOTH;
			search_tbl->action = 0;
			if (search_tbl->is_SGI)
				search_tbl->is_SGI = 0;
			else
				search_tbl->is_SGI = 1;
			lq_sta->search_better_tbl = 1;

			/*
			 * If active table already uses the fastest possible
			 * modulation (dual stream with short guard interval),
			 * and it's working well, there's no need to look
			 * for a better type of modulation!
			 */
			if ((tbl->lq_type == LQ_MIMO) &&
			    (tbl->is_SGI)) {
				s32 tpt = lq_sta->last_tpt / 100;
				if (((!tbl->is_fat) &&
				     (tpt >= expected_tpt_mimo20MHz[index])) ||
				    ((tbl->is_fat) &&
				     (tpt >= expected_tpt_mimo40MHz[index])))
					lq_sta->search_better_tbl = 0;
			}
			rs_get_expected_tpt_table(lq_sta, search_tbl);
			rs_mcs_from_tbl(&search_tbl->current_rate,
					     search_tbl, index, is_green);
			goto out;

		}
		tbl->action++;
		if (tbl->action > IWL_MIMO_SWITCH_GI)
			tbl->action = IWL_MIMO_SWITCH_ANTENNA_A;

		if (tbl->action == start_action)
			break;
	}

	return 0;
 out:
	tbl->action++;
	if (tbl->action > IWL_MIMO_SWITCH_GI)
		tbl->action = IWL_MIMO_SWITCH_ANTENNA_A;
	return 0;

}

/*
 * Check whether we should continue using same modulation mode, or
 * begin search for a new mode, based on:
 * 1) # tx successes or failures while using this mode
 * 2) # times calling this function
 * 3) elapsed time in this mode (not used, for now)
 */
static void rs_stay_in_table(struct iwl4965_lq_sta *lq_sta)
{
	struct iwl4965_scale_tbl_info *tbl;
	int i;
	int active_tbl;
	int flush_interval_passed = 0;

	active_tbl = lq_sta->active_tbl;

	tbl = &(lq_sta->lq_info[active_tbl]);

	/* If we've been disallowing search, see if we should now allow it */
	if (lq_sta->stay_in_tbl) {

		/* Elapsed time using current modulation mode */
		if (lq_sta->flush_timer)
			flush_interval_passed =
			    time_after(jiffies,
				       (unsigned long)(lq_sta->flush_timer +
					IWL_RATE_SCALE_FLUSH_INTVL));

		/* For now, disable the elapsed time criterion */
		flush_interval_passed = 0;

		/*
		 * Check if we should allow search for new modulation mode.
		 * If many frames have failed or succeeded, or we've used
		 * this same modulation for a long time, allow search, and
		 * reset history stats that keep track of whether we should
		 * allow a new search.  Also (below) reset all bitmaps and
		 * stats in active history.
		 */
		if ((lq_sta->total_failed > lq_sta->max_failure_limit) ||
		    (lq_sta->total_success > lq_sta->max_success_limit) ||
		    ((!lq_sta->search_better_tbl) && (lq_sta->flush_timer)
		     && (flush_interval_passed))) {
			IWL_DEBUG_HT("LQ: stay is expired %d %d %d\n:",
				     lq_sta->total_failed,
				     lq_sta->total_success,
				     flush_interval_passed);

			/* Allow search for new mode */
			lq_sta->stay_in_tbl = 0;	/* only place reset */
			lq_sta->total_failed = 0;
			lq_sta->total_success = 0;
			lq_sta->flush_timer = 0;

		/*
		 * Else if we've used this modulation mode enough repetitions
		 * (regardless of elapsed time or success/failure), reset
		 * history bitmaps and rate-specific stats for all rates in
		 * active table.
		 */
		} else {
			lq_sta->table_count++;
			if (lq_sta->table_count >=
			    lq_sta->table_count_limit) {
				lq_sta->table_count = 0;

				IWL_DEBUG_HT("LQ: stay in table clear win\n");
				for (i = 0; i < IWL_RATE_COUNT; i++)
					rs_rate_scale_clear_window(
						&(tbl->win[i]));
			}
		}

		/* If transitioning to allow "search", reset all history
		 * bitmaps and stats in active table (this will become the new
		 * "search" table). */
		if (!lq_sta->stay_in_tbl) {
			for (i = 0; i < IWL_RATE_COUNT; i++)
				rs_rate_scale_clear_window(&(tbl->win[i]));
		}
	}
}

/*
 * Do rate scaling and search for new modulation mode.
 */
static void rs_rate_scale_perform(struct iwl_priv *priv,
				  struct net_device *dev,
				  struct ieee80211_hdr *hdr,
				  struct sta_info *sta)
{
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_hw *hw = local_to_hw(local);
	struct ieee80211_conf *conf = &hw->conf;
	int low = IWL_RATE_INVALID;
	int high = IWL_RATE_INVALID;
	int index;
	int i;
	struct iwl4965_rate_scale_data *window = NULL;
	int current_tpt = IWL_INVALID_VALUE;
	int low_tpt = IWL_INVALID_VALUE;
	int high_tpt = IWL_INVALID_VALUE;
	u32 fail_count;
	s8 scale_action = 0;
	u16 fc, rate_mask;
	u8 update_lq = 0;
	struct iwl4965_lq_sta *lq_sta;
	struct iwl4965_scale_tbl_info *tbl, *tbl1;
	u16 rate_scale_index_msk = 0;
	struct iwl4965_rate mcs_rate;
	u8 is_green = 0;
	u8 active_tbl = 0;
	u8 done_search = 0;
	u16 high_low;
#ifdef CONFIG_IWL4965_HT
	u8 tid = MAX_TID_COUNT;
	__le16 *qc;
#endif

	IWL_DEBUG_RATE("rate scale calculate new rate for skb\n");

	fc = le16_to_cpu(hdr->frame_control);
	if (!ieee80211_is_data(fc) || is_multicast_ether_addr(hdr->addr1)) {
		/* Send management frames and broadcast/multicast data using
		 * lowest rate. */
		/* TODO: this could probably be improved.. */
		return;
	}

	if (!sta || !sta->rate_ctrl_priv)
		return;

	if (!priv->lq_mngr.lq_ready) {
		IWL_DEBUG_RATE("still rate scaling not ready\n");
		return;
	}
	lq_sta = (struct iwl4965_lq_sta *)sta->rate_ctrl_priv;

#ifdef CONFIG_IWL4965_HT
	qc = ieee80211_get_qos_ctrl(hdr);
	if (qc) {
		tid = (u8)(le16_to_cpu(*qc) & 0xf);
		rs_tl_add_packet(lq_sta, tid);
	}
#endif
	/*
	 * Select rate-scale / modulation-mode table to work with in
	 * the rest of this function:  "search" if searching for better
	 * modulation mode, or "active" if doing rate scaling within a mode.
	 */
	if (!lq_sta->search_better_tbl)
		active_tbl = lq_sta->active_tbl;
	else
		active_tbl = 1 - lq_sta->active_tbl;

	tbl = &(lq_sta->lq_info[active_tbl]);
	is_green = lq_sta->is_green;

	/* current tx rate */
	index = sta->last_txrate_idx;

	IWL_DEBUG_RATE("Rate scale index %d for type %d\n", index,
		       tbl->lq_type);

	/* rates available for this association, and for modulation mode */
	rs_get_supported_rates(lq_sta, hdr, tbl->lq_type,
				&rate_mask);

	IWL_DEBUG_RATE("mask 0x%04X \n", rate_mask);

	/* mask with station rate restriction */
	if (is_legacy(tbl->lq_type)) {
		if (lq_sta->band == IEEE80211_BAND_5GHZ)
			/* supp_rates has no CCK bits in A mode */
			rate_scale_index_msk = (u16) (rate_mask &
				(lq_sta->supp_rates << IWL_FIRST_OFDM_RATE));
		else
			rate_scale_index_msk = (u16) (rate_mask &
						      lq_sta->supp_rates);

	} else
		rate_scale_index_msk = rate_mask;

	if (!rate_scale_index_msk)
		rate_scale_index_msk = rate_mask;

	/* If current rate is no longer supported on current association,
	 * or user changed preferences for rates, find a new supported rate. */
	if (index < 0 || !((1 << index) & rate_scale_index_msk)) {
		index = IWL_INVALID_VALUE;
		update_lq = 1;

		/* get the highest available rate */
		for (i = 0; i <= IWL_RATE_COUNT; i++) {
			if ((1 << i) & rate_scale_index_msk)
				index = i;
		}

		if (index == IWL_INVALID_VALUE) {
			IWL_WARNING("Can not find a suitable rate\n");
			return;
		}
	}

	/* Get expected throughput table and history window for current rate */
	if (!tbl->expected_tpt)
		rs_get_expected_tpt_table(lq_sta, tbl);

	window = &(tbl->win[index]);

	/*
	 * If there is not enough history to calculate actual average
	 * throughput, keep analyzing results of more tx frames, without
	 * changing rate or mode (bypass most of the rest of this function).
	 * Set up new rate table in uCode only if old rate is not supported
	 * in current association (use new rate found above).
	 */
	fail_count = window->counter - window->success_counter;
	if (((fail_count < IWL_RATE_MIN_FAILURE_TH) &&
	     (window->success_counter < IWL_RATE_MIN_SUCCESS_TH))
	    || (tbl->expected_tpt == NULL)) {
		IWL_DEBUG_RATE("LQ: still below TH succ %d total %d "
			       "for index %d\n",
			       window->success_counter, window->counter, index);

		/* Can't calculate this yet; not enough history */
		window->average_tpt = IWL_INVALID_VALUE;

		/* Should we stay with this modulation mode,
		 * or search for a new one? */
		rs_stay_in_table(lq_sta);

		/* Set up new rate table in uCode, if needed */
		if (update_lq) {
			rs_mcs_from_tbl(&mcs_rate, tbl, index, is_green);
			rs_fill_link_cmd(lq_sta, &mcs_rate, &lq_sta->lq);
			iwl_send_lq_cmd(priv, &lq_sta->lq, CMD_ASYNC);
		}
		goto out;

	/* Else we have enough samples; calculate estimate of
	 * actual average throughput */
	} else
		window->average_tpt = ((window->success_ratio *
					tbl->expected_tpt[index] + 64) / 128);

	/* If we are searching for better modulation mode, check success. */
	if (lq_sta->search_better_tbl) {
		int success_limit = IWL_RATE_SCALE_SWITCH;

		/* If good success, continue using the "search" mode;
		 * no need to send new link quality command, since we're
		 * continuing to use the setup that we've been trying. */
		if ((window->success_ratio > success_limit) ||
		    (window->average_tpt > lq_sta->last_tpt)) {
			if (!is_legacy(tbl->lq_type)) {
				IWL_DEBUG_HT("LQ: we are switching to HT"
					     " rate suc %d current tpt %d"
					     " old tpt %d\n",
					     window->success_ratio,
					     window->average_tpt,
					     lq_sta->last_tpt);
				lq_sta->enable_counter = 1;
			}
			/* Swap tables; "search" becomes "active" */
			lq_sta->active_tbl = active_tbl;
			current_tpt = window->average_tpt;

		/* Else poor success; go back to mode in "active" table */
		} else {
			/* Nullify "search" table */
			tbl->lq_type = LQ_NONE;

			/* Revert to "active" table */
			active_tbl = lq_sta->active_tbl;
			tbl = &(lq_sta->lq_info[active_tbl]);

			/* Revert to "active" rate and throughput info */
			index = iwl4965_hwrate_to_plcp_idx(
				tbl->current_rate.rate_n_flags);
			current_tpt = lq_sta->last_tpt;

			/* Need to set up a new rate table in uCode */
			update_lq = 1;
			IWL_DEBUG_HT("XXY GO BACK TO OLD TABLE\n");
		}

		/* Either way, we've made a decision; modulation mode
		 * search is done, allow rate adjustment next time. */
		lq_sta->search_better_tbl = 0;
		done_search = 1;	/* Don't switch modes below! */
		goto lq_update;
	}

	/* (Else) not in search of better modulation mode, try for better
	 * starting rate, while staying in this mode. */
	high_low = rs_get_adjacent_rate(index, rate_scale_index_msk,
					tbl->lq_type);
	low = high_low & 0xff;
	high = (high_low >> 8) & 0xff;

	/* Collect measured throughputs for current and adjacent rates */
	current_tpt = window->average_tpt;
	if (low != IWL_RATE_INVALID)
		low_tpt = tbl->win[low].average_tpt;
	if (high != IWL_RATE_INVALID)
		high_tpt = tbl->win[high].average_tpt;

	/* Assume rate increase */
	scale_action = 1;

	/* Too many failures, decrease rate */
	if ((window->success_ratio <= IWL_RATE_DECREASE_TH) ||
	    (current_tpt == 0)) {
		IWL_DEBUG_RATE("decrease rate because of low success_ratio\n");
		scale_action = -1;

	/* No throughput measured yet for adjacent rates; try increase. */
	} else if ((low_tpt == IWL_INVALID_VALUE) &&
		   (high_tpt == IWL_INVALID_VALUE))
		scale_action = 1;

	/* Both adjacent throughputs are measured, but neither one has better
	 * throughput; we're using the best rate, don't change it! */
	else if ((low_tpt != IWL_INVALID_VALUE) &&
		 (high_tpt != IWL_INVALID_VALUE) &&
		 (low_tpt < current_tpt) &&
		 (high_tpt < current_tpt))
		scale_action = 0;

	/* At least one adjacent rate's throughput is measured,
	 * and may have better performance. */
	else {
		/* Higher adjacent rate's throughput is measured */
		if (high_tpt != IWL_INVALID_VALUE) {
			/* Higher rate has better throughput */
			if (high_tpt > current_tpt)
				scale_action = 1;
			else {
				IWL_DEBUG_RATE
				    ("decrease rate because of high tpt\n");
				scale_action = -1;
			}

		/* Lower adjacent rate's throughput is measured */
		} else if (low_tpt != IWL_INVALID_VALUE) {
			/* Lower rate has better throughput */
			if (low_tpt > current_tpt) {
				IWL_DEBUG_RATE
				    ("decrease rate because of low tpt\n");
				scale_action = -1;
			} else
				scale_action = 1;
		}
	}

	/* Sanity check; asked for decrease, but success rate or throughput
	 * has been good at old rate.  Don't change it. */
	if (scale_action == -1) {
		if ((low != IWL_RATE_INVALID) &&
		    ((window->success_ratio > IWL_RATE_HIGH_TH) ||
		     (current_tpt > (100 * tbl->expected_tpt[low]))))
			scale_action = 0;

	/* Sanity check; asked for increase, but success rate has not been great
	 * even at old rate, higher rate will be worse.  Don't change it. */
	} else if ((scale_action == 1) &&
		   (window->success_ratio < IWL_RATE_INCREASE_TH))
		scale_action = 0;

	switch (scale_action) {
	case -1:
		/* Decrease starting rate, update uCode's rate table */
		if (low != IWL_RATE_INVALID) {
			update_lq = 1;
			index = low;
		}
		break;
	case 1:
		/* Increase starting rate, update uCode's rate table */
		if (high != IWL_RATE_INVALID) {
			update_lq = 1;
			index = high;
		}

		break;
	case 0:
		/* No change */
	default:
		break;
	}

	IWL_DEBUG_HT("choose rate scale index %d action %d low %d "
		    "high %d type %d\n",
		     index, scale_action, low, high, tbl->lq_type);

 lq_update:
	/* Replace uCode's rate table for the destination station. */
	if (update_lq) {
		rs_mcs_from_tbl(&mcs_rate, tbl, index, is_green);
		rs_fill_link_cmd(lq_sta, &mcs_rate, &lq_sta->lq);
		iwl_send_lq_cmd(priv, &lq_sta->lq, CMD_ASYNC);
	}

	/* Should we stay with this modulation mode, or search for a new one? */
	rs_stay_in_table(lq_sta);

	/*
	 * Search for new modulation mode if we're:
	 * 1)  Not changing rates right now
	 * 2)  Not just finishing up a search
	 * 3)  Allowing a new search
	 */
	if (!update_lq && !done_search && !lq_sta->stay_in_tbl) {
		/* Save current throughput to compare with "search" throughput*/
		lq_sta->last_tpt = current_tpt;

		/* Select a new "search" modulation mode to try.
		 * If one is found, set up the new "search" table. */
		if (is_legacy(tbl->lq_type))
			rs_move_legacy_other(priv, lq_sta, conf, sta, index);
		else if (is_siso(tbl->lq_type))
			rs_move_siso_to_other(priv, lq_sta, conf, sta, index);
		else
			rs_move_mimo_to_other(priv, lq_sta, conf, sta, index);

		/* If new "search" mode was selected, set up in uCode table */
		if (lq_sta->search_better_tbl) {
			/* Access the "search" table, clear its history. */
			tbl = &(lq_sta->lq_info[(1 - lq_sta->active_tbl)]);
			for (i = 0; i < IWL_RATE_COUNT; i++)
				rs_rate_scale_clear_window(&(tbl->win[i]));

			/* Use new "search" start rate */
			index = iwl4965_hwrate_to_plcp_idx(
					tbl->current_rate.rate_n_flags);

			IWL_DEBUG_HT("Switch current  mcs: %X index: %d\n",
				     tbl->current_rate.rate_n_flags, index);
			rs_fill_link_cmd(lq_sta, &tbl->current_rate,
					 &lq_sta->lq);
			iwl_send_lq_cmd(priv, &lq_sta->lq, CMD_ASYNC);
		}

		/* If the "active" (non-search) mode was legacy,
		 * and we've tried switching antennas,
		 * but we haven't been able to try HT modes (not available),
		 * stay with best antenna legacy modulation for a while
		 * before next round of mode comparisons. */
		tbl1 = &(lq_sta->lq_info[lq_sta->active_tbl]);
		if (is_legacy(tbl1->lq_type) &&
#ifdef CONFIG_IWL4965_HT
		   (!(conf->flags & IEEE80211_CONF_SUPPORT_HT_MODE)) &&
#endif
		    (lq_sta->action_counter >= 1)) {
			lq_sta->action_counter = 0;
			IWL_DEBUG_HT("LQ: STAY in legacy table\n");
			rs_set_stay_in_table(1, lq_sta);
		}

		/* If we're in an HT mode, and all 3 mode switch actions
		 * have been tried and compared, stay in this best modulation
		 * mode for a while before next round of mode comparisons. */
		if (lq_sta->enable_counter &&
		    (lq_sta->action_counter >= IWL_ACTION_LIMIT)) {
#ifdef CONFIG_IWL4965_HT
			if ((lq_sta->last_tpt > IWL_AGG_TPT_THREHOLD) &&
			    (lq_sta->tx_agg_tid_en & (1 << tid)) &&
			    (tid != MAX_TID_COUNT)) {
				IWL_DEBUG_HT("try to aggregate tid %d\n", tid);
				rs_tl_turn_on_agg(priv, tid, lq_sta, sta);
			}
#endif /*CONFIG_IWL4965_HT */
			lq_sta->action_counter = 0;
			rs_set_stay_in_table(0, lq_sta);
		}

	/*
	 * Else, don't search for a new modulation mode.
	 * Put new timestamp in stay-in-modulation-mode flush timer if:
	 * 1)  Not changing rates right now
	 * 2)  Not just finishing up a search
	 * 3)  flush timer is empty
	 */
	} else {
		if ((!update_lq) && (!done_search) && (!lq_sta->flush_timer))
			lq_sta->flush_timer = jiffies;
	}

out:
	rs_mcs_from_tbl(&tbl->current_rate, tbl, index, is_green);
	i = index;
	sta->last_txrate_idx = i;

	/* sta->txrate_idx is an index to A mode rates which start
	 * at IWL_FIRST_OFDM_RATE
	 */
	if (lq_sta->band == IEEE80211_BAND_5GHZ)
		sta->txrate_idx = i - IWL_FIRST_OFDM_RATE;
	else
		sta->txrate_idx = i;

	return;
}


static void rs_initialize_lq(struct iwl_priv *priv,
			     struct ieee80211_conf *conf,
			     struct sta_info *sta)
{
	int i;
	struct iwl4965_lq_sta *lq_sta;
	struct iwl4965_scale_tbl_info *tbl;
	u8 active_tbl = 0;
	int rate_idx;
	u8 use_green = rs_use_green(priv, conf);
	struct iwl4965_rate mcs_rate;

	if (!sta || !sta->rate_ctrl_priv)
		goto out;

	lq_sta = (struct iwl4965_lq_sta *)sta->rate_ctrl_priv;
	i = sta->last_txrate_idx;

	if ((lq_sta->lq.sta_id == 0xff) &&
	    (priv->iw_mode == IEEE80211_IF_TYPE_IBSS))
		goto out;

	if (!lq_sta->search_better_tbl)
		active_tbl = lq_sta->active_tbl;
	else
		active_tbl = 1 - lq_sta->active_tbl;

	tbl = &(lq_sta->lq_info[active_tbl]);

	if ((i < 0) || (i >= IWL_RATE_COUNT))
		i = 0;

	mcs_rate.rate_n_flags = iwl4965_rates[i].plcp ;
	mcs_rate.rate_n_flags |= RATE_MCS_ANT_B_MSK;
	mcs_rate.rate_n_flags &= ~RATE_MCS_ANT_A_MSK;

	if (i >= IWL_FIRST_CCK_RATE && i <= IWL_LAST_CCK_RATE)
		mcs_rate.rate_n_flags |= RATE_MCS_CCK_MSK;

	tbl->antenna_type = ANT_AUX;
	rs_get_tbl_info_from_mcs(&mcs_rate, priv->band, tbl, &rate_idx);
	if (!rs_is_ant_connected(priv->valid_antenna, tbl->antenna_type))
	    rs_toggle_antenna(&mcs_rate, tbl);

	rs_mcs_from_tbl(&mcs_rate, tbl, rate_idx, use_green);
	tbl->current_rate.rate_n_flags = mcs_rate.rate_n_flags;
	rs_get_expected_tpt_table(lq_sta, tbl);
	rs_fill_link_cmd(lq_sta, &mcs_rate, &lq_sta->lq);
	iwl_send_lq_cmd(priv, &lq_sta->lq, CMD_ASYNC);
 out:
	return;
}

static void rs_get_rate(void *priv_rate, struct net_device *dev,
			struct ieee80211_supported_band *sband,
			struct sk_buff *skb,
			struct rate_selection *sel)
{

	int i;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct ieee80211_conf *conf = &local->hw.conf;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct sta_info *sta;
	u16 fc;
	struct iwl_priv *priv = (struct iwl_priv *)priv_rate;
	struct iwl4965_lq_sta *lq_sta;

	IWL_DEBUG_RATE_LIMIT("rate scale calculate new rate for skb\n");

	rcu_read_lock();

	sta = sta_info_get(local, hdr->addr1);

	/* Send management frames and broadcast/multicast data using lowest
	 * rate. */
	fc = le16_to_cpu(hdr->frame_control);
	if (!ieee80211_is_data(fc) || is_multicast_ether_addr(hdr->addr1) ||
	    !sta || !sta->rate_ctrl_priv) {
		sel->rate = rate_lowest(local, sband, sta);
		goto out;
	}

	lq_sta = (struct iwl4965_lq_sta *)sta->rate_ctrl_priv;
	i = sta->last_txrate_idx;

	if ((priv->iw_mode == IEEE80211_IF_TYPE_IBSS) &&
	    !lq_sta->ibss_sta_added) {
		u8 sta_id = iwl4965_hw_find_station(priv, hdr->addr1);
		DECLARE_MAC_BUF(mac);

		if (sta_id == IWL_INVALID_STATION) {
			IWL_DEBUG_RATE("LQ: ADD station %s\n",
				       print_mac(mac, hdr->addr1));
			sta_id = iwl4965_add_station_flags(priv, hdr->addr1,
							0, CMD_ASYNC, NULL);
		}
		if ((sta_id != IWL_INVALID_STATION)) {
			lq_sta->lq.sta_id = sta_id;
			lq_sta->lq.rs_table[0].rate_n_flags = 0;
			lq_sta->ibss_sta_added = 1;
			rs_initialize_lq(priv, conf, sta);
		}
		if (!lq_sta->ibss_sta_added)
			goto done;
	}

done:
	if ((i < 0) || (i > IWL_RATE_COUNT)) {
		sel->rate = rate_lowest(local, sband, sta);
		goto out;
	}

	sel->rate = &priv->ieee_rates[i];
out:
	rcu_read_unlock();
}

static void *rs_alloc_sta(void *priv, gfp_t gfp)
{
	struct iwl4965_lq_sta *lq_sta;
	int i, j;

	IWL_DEBUG_RATE("create station rate scale window\n");

	lq_sta = kzalloc(sizeof(struct iwl4965_lq_sta), gfp);

	if (lq_sta == NULL)
		return NULL;
	lq_sta->lq.sta_id = 0xff;


	for (j = 0; j < LQ_SIZE; j++)
		for (i = 0; i < IWL_RATE_COUNT; i++)
			rs_rate_scale_clear_window(&(lq_sta->lq_info[j].win[i]));

	return lq_sta;
}

static void rs_rate_init(void *priv_rate, void *priv_sta,
			 struct ieee80211_local *local,
			 struct sta_info *sta)
{
	int i, j;
	struct ieee80211_conf *conf = &local->hw.conf;
	struct ieee80211_supported_band *sband;
	struct iwl_priv *priv = (struct iwl_priv *)priv_rate;
	struct iwl4965_lq_sta *lq_sta = priv_sta;

	sband = local->hw.wiphy->bands[local->hw.conf.channel->band];

	lq_sta->flush_timer = 0;
	lq_sta->supp_rates = sta->supp_rates[sband->band];
	sta->txrate_idx = 3;
	for (j = 0; j < LQ_SIZE; j++)
		for (i = 0; i < IWL_RATE_COUNT; i++)
			rs_rate_scale_clear_window(&(lq_sta->lq_info[j].win[i]));

	IWL_DEBUG_RATE("rate scale global init\n");
	/* TODO: what is a good starting rate for STA? About middle? Maybe not
	 * the lowest or the highest rate.. Could consider using RSSI from
	 * previous packets? Need to have IEEE 802.1X auth succeed immediately
	 * after assoc.. */

	lq_sta->ibss_sta_added = 0;
	if (priv->iw_mode == IEEE80211_IF_TYPE_AP) {
		u8 sta_id = iwl4965_hw_find_station(priv, sta->addr);
		DECLARE_MAC_BUF(mac);

		/* for IBSS the call are from tasklet */
		IWL_DEBUG_HT("LQ: ADD station %s\n",
			     print_mac(mac, sta->addr));

		if (sta_id == IWL_INVALID_STATION) {
			IWL_DEBUG_RATE("LQ: ADD station %s\n",
				       print_mac(mac, sta->addr));
			sta_id = iwl4965_add_station_flags(priv, sta->addr,
							0, CMD_ASYNC, NULL);
		}
		if ((sta_id != IWL_INVALID_STATION)) {
			lq_sta->lq.sta_id = sta_id;
			lq_sta->lq.rs_table[0].rate_n_flags = 0;
		}
		/* FIXME: this is w/a remove it later */
		priv->assoc_station_added = 1;
	}

	/* Find highest tx rate supported by hardware and destination station */
	for (i = 0; i < sband->n_bitrates; i++)
		if (sta->supp_rates[sband->band] & BIT(i))
			sta->txrate_idx = i;

	sta->last_txrate_idx = sta->txrate_idx;
	/* WTF is with this bogus comment? A doesn't have cck rates */
	/* For MODE_IEEE80211A, cck rates are at end of rate table */
	if (local->hw.conf.channel->band == IEEE80211_BAND_5GHZ)
		sta->last_txrate_idx += IWL_FIRST_OFDM_RATE;

	lq_sta->is_dup = 0;
	lq_sta->valid_antenna = priv->valid_antenna;
	lq_sta->antenna = priv->antenna;
	lq_sta->is_green = rs_use_green(priv, conf);
	lq_sta->active_rate = priv->active_rate;
	lq_sta->active_rate &= ~(0x1000);
	lq_sta->active_rate_basic = priv->active_rate_basic;
	lq_sta->band = priv->band;
#ifdef CONFIG_IWL4965_HT
	/*
	 * active_siso_rate mask includes 9 MBits (bit 5), and CCK (bits 0-3),
	 * supp_rates[] does not; shift to convert format, force 9 MBits off.
	 */
	lq_sta->active_siso_rate = (priv->current_ht_config.supp_mcs_set[0] << 1);
	lq_sta->active_siso_rate |=
			(priv->current_ht_config.supp_mcs_set[0] & 0x1);
	lq_sta->active_siso_rate &= ~((u16)0x2);
	lq_sta->active_siso_rate =
			lq_sta->active_siso_rate << IWL_FIRST_OFDM_RATE;

	/* Same here */
	lq_sta->active_mimo_rate = (priv->current_ht_config.supp_mcs_set[1] << 1);
	lq_sta->active_mimo_rate |=
			(priv->current_ht_config.supp_mcs_set[1] & 0x1);
	lq_sta->active_mimo_rate &= ~((u16)0x2);
	lq_sta->active_mimo_rate =
			lq_sta->active_mimo_rate << IWL_FIRST_OFDM_RATE;
	IWL_DEBUG_HT("SISO RATE 0x%X MIMO RATE 0x%X\n",
		     lq_sta->active_siso_rate,
		     lq_sta->active_mimo_rate);
	/* as default allow aggregation for all tids */
	lq_sta->tx_agg_tid_en = IWL_AGG_ALL_TID;
#endif /*CONFIG_IWL4965_HT*/
#ifdef CONFIG_MAC80211_DEBUGFS
	lq_sta->drv = priv;
#endif

	if (priv->assoc_station_added)
		priv->lq_mngr.lq_ready = 1;

	rs_initialize_lq(priv, conf, sta);
}

static void rs_fill_link_cmd(struct iwl4965_lq_sta *lq_sta,
			    struct iwl4965_rate *tx_mcs,
			    struct iwl_link_quality_cmd *lq_cmd)
{
	int index = 0;
	int rate_idx;
	int repeat_rate = 0;
	u8 ant_toggle_count = 0;
	u8 use_ht_possible = 1;
	struct iwl4965_rate new_rate;
	struct iwl4965_scale_tbl_info tbl_type = { 0 };

	/* Override starting rate (index 0) if needed for debug purposes */
	rs_dbgfs_set_mcs(lq_sta, tx_mcs, index);

	/* Interpret rate_n_flags */
	rs_get_tbl_info_from_mcs(tx_mcs, lq_sta->band,
				  &tbl_type, &rate_idx);

	/* How many times should we repeat the initial rate? */
	if (is_legacy(tbl_type.lq_type)) {
		ant_toggle_count = 1;
		repeat_rate = IWL_NUMBER_TRY;
	} else
		repeat_rate = IWL_HT_NUMBER_TRY;

	lq_cmd->general_params.mimo_delimiter =
			is_mimo(tbl_type.lq_type) ? 1 : 0;

	/* Fill 1st table entry (index 0) */
	lq_cmd->rs_table[index].rate_n_flags =
			cpu_to_le32(tx_mcs->rate_n_flags);
	new_rate.rate_n_flags = tx_mcs->rate_n_flags;

	if (is_mimo(tbl_type.lq_type) || (tbl_type.antenna_type == ANT_MAIN))
		lq_cmd->general_params.single_stream_ant_msk
			= LINK_QUAL_ANT_A_MSK;
	else
		lq_cmd->general_params.single_stream_ant_msk
			= LINK_QUAL_ANT_B_MSK;

	index++;
	repeat_rate--;

	/* Fill rest of rate table */
	while (index < LINK_QUAL_MAX_RETRY_NUM) {
		/* Repeat initial/next rate.
		 * For legacy IWL_NUMBER_TRY == 1, this loop will not execute.
		 * For HT IWL_HT_NUMBER_TRY == 3, this executes twice. */
		while (repeat_rate > 0 && (index < LINK_QUAL_MAX_RETRY_NUM)) {
			if (is_legacy(tbl_type.lq_type)) {
				if (ant_toggle_count <
				    NUM_TRY_BEFORE_ANTENNA_TOGGLE)
					ant_toggle_count++;
				else {
					rs_toggle_antenna(&new_rate, &tbl_type);
					ant_toggle_count = 1;
				}
			}

			/* Override next rate if needed for debug purposes */
			rs_dbgfs_set_mcs(lq_sta, &new_rate, index);

			/* Fill next table entry */
			lq_cmd->rs_table[index].rate_n_flags =
					cpu_to_le32(new_rate.rate_n_flags);
			repeat_rate--;
			index++;
		}

		rs_get_tbl_info_from_mcs(&new_rate, lq_sta->band, &tbl_type,
						&rate_idx);

		/* Indicate to uCode which entries might be MIMO.
		 * If initial rate was MIMO, this will finally end up
		 * as (IWL_HT_NUMBER_TRY * 2), after 2nd pass, otherwise 0. */
		if (is_mimo(tbl_type.lq_type))
			lq_cmd->general_params.mimo_delimiter = index;

		/* Get next rate */
		rs_get_lower_rate(lq_sta, &tbl_type, rate_idx,
				  use_ht_possible, &new_rate);

		/* How many times should we repeat the next rate? */
		if (is_legacy(tbl_type.lq_type)) {
			if (ant_toggle_count < NUM_TRY_BEFORE_ANTENNA_TOGGLE)
				ant_toggle_count++;
			else {
				rs_toggle_antenna(&new_rate, &tbl_type);
				ant_toggle_count = 1;
			}
			repeat_rate = IWL_NUMBER_TRY;
		} else
			repeat_rate = IWL_HT_NUMBER_TRY;

		/* Don't allow HT rates after next pass.
		 * rs_get_lower_rate() will change type to LQ_A or LQ_G. */
		use_ht_possible = 0;

		/* Override next rate if needed for debug purposes */
		rs_dbgfs_set_mcs(lq_sta, &new_rate, index);

		/* Fill next table entry */
		lq_cmd->rs_table[index].rate_n_flags =
				cpu_to_le32(new_rate.rate_n_flags);

		index++;
		repeat_rate--;
	}

	lq_cmd->general_params.dual_stream_ant_msk = 3;
	lq_cmd->agg_params.agg_dis_start_th = 3;
	lq_cmd->agg_params.agg_time_limit = cpu_to_le16(4000);
}

static void *rs_alloc(struct ieee80211_local *local)
{
	return local->hw.priv;
}
/* rate scale requires free function to be implemented */
static void rs_free(void *priv_rate)
{
	return;
}

static void rs_clear(void *priv_rate)
{
	struct iwl_priv *priv = (struct iwl_priv *) priv_rate;

	IWL_DEBUG_RATE("enter\n");

	priv->lq_mngr.lq_ready = 0;

	IWL_DEBUG_RATE("leave\n");
}

static void rs_free_sta(void *priv, void *priv_sta)
{
	struct iwl4965_lq_sta *lq_sta = priv_sta;

	IWL_DEBUG_RATE("enter\n");
	kfree(lq_sta);
	IWL_DEBUG_RATE("leave\n");
}


#ifdef CONFIG_MAC80211_DEBUGFS
static int open_file_generic(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}
static void rs_dbgfs_set_mcs(struct iwl4965_lq_sta *lq_sta,
				struct iwl4965_rate *mcs, int index)
{
	u32 base_rate;

	if (lq_sta->band == IEEE80211_BAND_5GHZ)
		base_rate = 0x800D;
	else
		base_rate = 0x820A;

	if (lq_sta->dbg_fixed.rate_n_flags) {
		if (index < 12)
			mcs->rate_n_flags = lq_sta->dbg_fixed.rate_n_flags;
		else
			mcs->rate_n_flags = base_rate;
		IWL_DEBUG_RATE("Fixed rate ON\n");
		return;
	}

	IWL_DEBUG_RATE("Fixed rate OFF\n");
}

static ssize_t rs_sta_dbgfs_scale_table_write(struct file *file,
			const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct iwl4965_lq_sta *lq_sta = file->private_data;
	char buf[64];
	int buf_size;
	u32 parsed_rate;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (sscanf(buf, "%x", &parsed_rate) == 1)
		lq_sta->dbg_fixed.rate_n_flags = parsed_rate;
	else
		lq_sta->dbg_fixed.rate_n_flags = 0;

	lq_sta->active_rate = 0x0FFF;	/* 1 - 54 MBits, includes CCK */
	lq_sta->active_siso_rate = 0x1FD0;	/* 6 - 60 MBits, no 9, no CCK */
	lq_sta->active_mimo_rate = 0x1FD0;	/* 6 - 60 MBits, no 9, no CCK */

	IWL_DEBUG_RATE("sta_id %d rate 0x%X\n",
		lq_sta->lq.sta_id, lq_sta->dbg_fixed.rate_n_flags);

	if (lq_sta->dbg_fixed.rate_n_flags) {
		rs_fill_link_cmd(lq_sta, &lq_sta->dbg_fixed, &lq_sta->lq);
		iwl_send_lq_cmd(lq_sta->drv, &lq_sta->lq, CMD_ASYNC);
	}

	return count;
}

static ssize_t rs_sta_dbgfs_scale_table_read(struct file *file,
			char __user *user_buf, size_t count, loff_t *ppos)
{
	char buff[1024];
	int desc = 0;
	int i = 0;

	struct iwl4965_lq_sta *lq_sta = file->private_data;

	desc += sprintf(buff+desc, "sta_id %d\n", lq_sta->lq.sta_id);
	desc += sprintf(buff+desc, "failed=%d success=%d rate=0%X\n",
			lq_sta->total_failed, lq_sta->total_success,
			lq_sta->active_rate);
	desc += sprintf(buff+desc, "fixed rate 0x%X\n",
			lq_sta->dbg_fixed.rate_n_flags);
	desc += sprintf(buff+desc, "general:"
		"flags=0x%X mimo-d=%d s-ant0x%x d-ant=0x%x\n",
		lq_sta->lq.general_params.flags,
		lq_sta->lq.general_params.mimo_delimiter,
		lq_sta->lq.general_params.single_stream_ant_msk,
		lq_sta->lq.general_params.dual_stream_ant_msk);

	desc += sprintf(buff+desc, "agg:"
			"time_limit=%d dist_start_th=%d frame_cnt_limit=%d\n",
			le16_to_cpu(lq_sta->lq.agg_params.agg_time_limit),
			lq_sta->lq.agg_params.agg_dis_start_th,
			lq_sta->lq.agg_params.agg_frame_cnt_limit);

	desc += sprintf(buff+desc,
			"Start idx [0]=0x%x [1]=0x%x [2]=0x%x [3]=0x%x\n",
			lq_sta->lq.general_params.start_rate_index[0],
			lq_sta->lq.general_params.start_rate_index[1],
			lq_sta->lq.general_params.start_rate_index[2],
			lq_sta->lq.general_params.start_rate_index[3]);


	for (i = 0; i < LINK_QUAL_MAX_RETRY_NUM; i++)
		desc += sprintf(buff+desc, " rate[%d] 0x%X\n",
			i, le32_to_cpu(lq_sta->lq.rs_table[i].rate_n_flags));

	return simple_read_from_buffer(user_buf, count, ppos, buff, desc);
}

static const struct file_operations rs_sta_dbgfs_scale_table_ops = {
	.write = rs_sta_dbgfs_scale_table_write,
	.read = rs_sta_dbgfs_scale_table_read,
	.open = open_file_generic,
};
static ssize_t rs_sta_dbgfs_stats_table_read(struct file *file,
			char __user *user_buf, size_t count, loff_t *ppos)
{
	char buff[1024];
	int desc = 0;
	int i, j;

	struct iwl4965_lq_sta *lq_sta = file->private_data;
	for (i = 0; i < LQ_SIZE; i++) {
		desc += sprintf(buff+desc, "%s type=%d SGI=%d FAT=%d DUP=%d\n"
				"rate=0x%X\n",
				lq_sta->active_tbl == i?"*":"x",
				lq_sta->lq_info[i].lq_type,
				lq_sta->lq_info[i].is_SGI,
				lq_sta->lq_info[i].is_fat,
				lq_sta->lq_info[i].is_dup,
				lq_sta->lq_info[i].current_rate.rate_n_flags);
		for (j = 0; j < IWL_RATE_COUNT; j++) {
			desc += sprintf(buff+desc,
				"counter=%d success=%d %%=%d\n",
				lq_sta->lq_info[i].win[j].counter,
				lq_sta->lq_info[i].win[j].success_counter,
				lq_sta->lq_info[i].win[j].success_ratio);
		}
	}
	return simple_read_from_buffer(user_buf, count, ppos, buff, desc);
}

static const struct file_operations rs_sta_dbgfs_stats_table_ops = {
	.read = rs_sta_dbgfs_stats_table_read,
	.open = open_file_generic,
};

static void rs_add_debugfs(void *priv, void *priv_sta,
					struct dentry *dir)
{
	struct iwl4965_lq_sta *lq_sta = priv_sta;
	lq_sta->rs_sta_dbgfs_scale_table_file =
		debugfs_create_file("rate_scale_table", 0600, dir,
				lq_sta, &rs_sta_dbgfs_scale_table_ops);
	lq_sta->rs_sta_dbgfs_stats_table_file =
		debugfs_create_file("rate_stats_table", 0600, dir,
			lq_sta, &rs_sta_dbgfs_stats_table_ops);
#ifdef CONFIG_IWL4965_HT
	lq_sta->rs_sta_dbgfs_tx_agg_tid_en_file =
		debugfs_create_u8("tx_agg_tid_enable", 0600, dir,
		&lq_sta->tx_agg_tid_en);
#endif

}

static void rs_remove_debugfs(void *priv, void *priv_sta)
{
	struct iwl4965_lq_sta *lq_sta = priv_sta;
	debugfs_remove(lq_sta->rs_sta_dbgfs_scale_table_file);
	debugfs_remove(lq_sta->rs_sta_dbgfs_stats_table_file);
#ifdef CONFIG_IWL4965_HT
	debugfs_remove(lq_sta->rs_sta_dbgfs_tx_agg_tid_en_file);
#endif
}
#endif

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
#ifdef CONFIG_MAC80211_DEBUGFS
	.add_sta_debugfs = rs_add_debugfs,
	.remove_sta_debugfs = rs_remove_debugfs,
#endif
};

int iwl4965_fill_rs_info(struct ieee80211_hw *hw, char *buf, u8 sta_id)
{
	struct ieee80211_local *local = hw_to_local(hw);
	struct iwl_priv *priv = hw->priv;
	struct iwl4965_lq_sta *lq_sta;
	struct sta_info *sta;
	int cnt = 0, i;
	u32 samples = 0, success = 0, good = 0;
	unsigned long now = jiffies;
	u32 max_time = 0;
	u8 lq_type, antenna;

	rcu_read_lock();

	sta = sta_info_get(local, priv->stations[sta_id].sta.sta.addr);
	if (!sta || !sta->rate_ctrl_priv) {
		if (sta)
			IWL_DEBUG_RATE("leave - no private rate data!\n");
		else
			IWL_DEBUG_RATE("leave - no station!\n");
		rcu_read_unlock();
		return sprintf(buf, "station %d not found\n", sta_id);
	}

	lq_sta = (void *)sta->rate_ctrl_priv;

	lq_type = lq_sta->lq_info[lq_sta->active_tbl].lq_type;
	antenna = lq_sta->lq_info[lq_sta->active_tbl].antenna_type;

	if (is_legacy(lq_type))
		i = IWL_RATE_54M_INDEX;
	else
		i = IWL_RATE_60M_INDEX;
	while (1) {
		u64 mask;
		int j;
		int active = lq_sta->active_tbl;

		cnt +=
		    sprintf(&buf[cnt], " %2dMbs: ", iwl4965_rates[i].ieee / 2);

		mask = (1ULL << (IWL_RATE_MAX_WINDOW - 1));
		for (j = 0; j < IWL_RATE_MAX_WINDOW; j++, mask >>= 1)
			buf[cnt++] =
				(lq_sta->lq_info[active].win[i].data & mask)
				? '1' : '0';

		samples += lq_sta->lq_info[active].win[i].counter;
		good += lq_sta->lq_info[active].win[i].success_counter;
		success += lq_sta->lq_info[active].win[i].success_counter *
			   iwl4965_rates[i].ieee;

		if (lq_sta->lq_info[active].win[i].stamp) {
			int delta =
				   jiffies_to_msecs(now -
				   lq_sta->lq_info[active].win[i].stamp);

			if (delta > max_time)
				max_time = delta;

			cnt += sprintf(&buf[cnt], "%5dms\n", delta);
		} else
			buf[cnt++] = '\n';

		j = iwl4965_get_prev_ieee_rate(i);
		if (j == i)
			break;
		i = j;
	}

	/* Display the average rate of all samples taken.
	 *
	 * NOTE:  We multiply # of samples by 2 since the IEEE measurement
	 * added from iwl4965_rates is actually 2X the rate */
	if (samples)
		cnt += sprintf(&buf[cnt],
			 "\nAverage rate is %3d.%02dMbs over last %4dms\n"
			 "%3d%% success (%d good packets over %d tries)\n",
			 success / (2 * samples), (success * 5 / samples) % 10,
			 max_time, good * 100 / samples, good, samples);
	else
		cnt += sprintf(&buf[cnt], "\nAverage rate: 0Mbs\n");

	cnt += sprintf(&buf[cnt], "\nrate scale type %d antenna %d "
			 "active_search %d rate index %d\n", lq_type, antenna,
			 lq_sta->search_better_tbl, sta->last_txrate_idx);

	rcu_read_unlock();
	return cnt;
}

void iwl4965_rate_scale_init(struct ieee80211_hw *hw, s32 sta_id)
{
	struct iwl_priv *priv = hw->priv;

	priv->lq_mngr.lq_ready = 1;
}

int iwl4965_rate_control_register(void)
{
	return ieee80211_rate_control_register(&rs_ops);
}

void iwl4965_rate_control_unregister(void)
{
	ieee80211_rate_control_unregister(&rs_ops);
}

