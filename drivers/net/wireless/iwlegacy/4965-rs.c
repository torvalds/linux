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
#include <net/mac80211.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>

#include <linux/workqueue.h>

#include "common.h"
#include "4965.h"

#define IL4965_RS_NAME "iwl-4965-rs"

#define NUM_TRY_BEFORE_ANT_TOGGLE 1
#define IL_NUMBER_TRY      1
#define IL_HT_NUMBER_TRY   3

#define RATE_MAX_WINDOW		62	/* # tx in history win */
#define RATE_MIN_FAILURE_TH		6	/* min failures to calc tpt */
#define RATE_MIN_SUCCESS_TH		8	/* min successes to calc tpt */

/* max allowed rate miss before sync LQ cmd */
#define IL_MISSED_RATE_MAX		15
/* max time to accum history 2 seconds */
#define RATE_SCALE_FLUSH_INTVL   (3*HZ)

static u8 rs_ht_to_legacy[] = {
	RATE_6M_IDX, RATE_6M_IDX,
	RATE_6M_IDX, RATE_6M_IDX,
	RATE_6M_IDX,
	RATE_6M_IDX, RATE_9M_IDX,
	RATE_12M_IDX, RATE_18M_IDX,
	RATE_24M_IDX, RATE_36M_IDX,
	RATE_48M_IDX, RATE_54M_IDX
};

static const u8 ant_toggle_lookup[] = {
	/*ANT_NONE -> */ ANT_NONE,
	/*ANT_A    -> */ ANT_B,
	/*ANT_B    -> */ ANT_C,
	/*ANT_AB   -> */ ANT_BC,
	/*ANT_C    -> */ ANT_A,
	/*ANT_AC   -> */ ANT_AB,
	/*ANT_BC   -> */ ANT_AC,
	/*ANT_ABC  -> */ ANT_ABC,
};

#define IL_DECLARE_RATE_INFO(r, s, ip, in, rp, rn, pp, np)    \
	[RATE_##r##M_IDX] = { RATE_##r##M_PLCP,      \
				    RATE_SISO_##s##M_PLCP, \
				    RATE_MIMO2_##s##M_PLCP,\
				    RATE_##r##M_IEEE,      \
				    RATE_##ip##M_IDX,    \
				    RATE_##in##M_IDX,    \
				    RATE_##rp##M_IDX,    \
				    RATE_##rn##M_IDX,    \
				    RATE_##pp##M_IDX,    \
				    RATE_##np##M_IDX }

/*
 * Parameter order:
 *   rate, ht rate, prev rate, next rate, prev tgg rate, next tgg rate
 *
 * If there isn't a valid next or previous rate then INV is used which
 * maps to RATE_INVALID
 *
 */
const struct il_rate_info il_rates[RATE_COUNT] = {
	IL_DECLARE_RATE_INFO(1, INV, INV, 2, INV, 2, INV, 2),	/*  1mbps */
	IL_DECLARE_RATE_INFO(2, INV, 1, 5, 1, 5, 1, 5),		/*  2mbps */
	IL_DECLARE_RATE_INFO(5, INV, 2, 6, 2, 11, 2, 11),	/*5.5mbps */
	IL_DECLARE_RATE_INFO(11, INV, 9, 12, 9, 12, 5, 18),	/* 11mbps */
	IL_DECLARE_RATE_INFO(6, 6, 5, 9, 5, 11, 5, 11),		/*  6mbps */
	IL_DECLARE_RATE_INFO(9, 6, 6, 11, 6, 11, 5, 11),	/*  9mbps */
	IL_DECLARE_RATE_INFO(12, 12, 11, 18, 11, 18, 11, 18),	/* 12mbps */
	IL_DECLARE_RATE_INFO(18, 18, 12, 24, 12, 24, 11, 24),	/* 18mbps */
	IL_DECLARE_RATE_INFO(24, 24, 18, 36, 18, 36, 18, 36),	/* 24mbps */
	IL_DECLARE_RATE_INFO(36, 36, 24, 48, 24, 48, 24, 48),	/* 36mbps */
	IL_DECLARE_RATE_INFO(48, 48, 36, 54, 36, 54, 36, 54),	/* 48mbps */
	IL_DECLARE_RATE_INFO(54, 54, 48, INV, 48, INV, 48, INV),/* 54mbps */
	IL_DECLARE_RATE_INFO(60, 60, 48, INV, 48, INV, 48, INV),/* 60mbps */
};

static int
il4965_hwrate_to_plcp_idx(u32 rate_n_flags)
{
	int idx = 0;

	/* HT rate format */
	if (rate_n_flags & RATE_MCS_HT_MSK) {
		idx = (rate_n_flags & 0xff);

		if (idx >= RATE_MIMO2_6M_PLCP)
			idx = idx - RATE_MIMO2_6M_PLCP;

		idx += IL_FIRST_OFDM_RATE;
		/* skip 9M not supported in ht */
		if (idx >= RATE_9M_IDX)
			idx += 1;
		if (idx >= IL_FIRST_OFDM_RATE && idx <= IL_LAST_OFDM_RATE)
			return idx;

		/* legacy rate format, search for match in table */
	} else {
		for (idx = 0; idx < ARRAY_SIZE(il_rates); idx++)
			if (il_rates[idx].plcp == (rate_n_flags & 0xFF))
				return idx;
	}

	return -1;
}

static void il4965_rs_rate_scale_perform(struct il_priv *il,
					 struct sk_buff *skb,
					 struct ieee80211_sta *sta,
					 struct il_lq_sta *lq_sta);
static void il4965_rs_fill_link_cmd(struct il_priv *il,
				    struct il_lq_sta *lq_sta, u32 rate_n_flags);
static void il4965_rs_stay_in_table(struct il_lq_sta *lq_sta,
				    bool force_search);

#ifdef CONFIG_MAC80211_DEBUGFS
static void il4965_rs_dbgfs_set_mcs(struct il_lq_sta *lq_sta,
				    u32 *rate_n_flags, int idx);
#else
static void
il4965_rs_dbgfs_set_mcs(struct il_lq_sta *lq_sta, u32 * rate_n_flags, int idx)
{
}
#endif

/**
 * The following tables contain the expected throughput metrics for all rates
 *
 *	1, 2, 5.5, 11, 6, 9, 12, 18, 24, 36, 48, 54, 60 MBits
 *
 * where invalid entries are zeros.
 *
 * CCK rates are only valid in legacy table and will only be used in G
 * (2.4 GHz) band.
 */

static s32 expected_tpt_legacy[RATE_COUNT] = {
	7, 13, 35, 58, 40, 57, 72, 98, 121, 154, 177, 186, 0
};

static s32 expected_tpt_siso20MHz[4][RATE_COUNT] = {
	{0, 0, 0, 0, 42, 0, 76, 102, 124, 158, 183, 193, 202},	/* Norm */
	{0, 0, 0, 0, 46, 0, 82, 110, 132, 167, 192, 202, 210},	/* SGI */
	{0, 0, 0, 0, 48, 0, 93, 135, 176, 251, 319, 351, 381},	/* AGG */
	{0, 0, 0, 0, 53, 0, 102, 149, 193, 275, 348, 381, 413},	/* AGG+SGI */
};

static s32 expected_tpt_siso40MHz[4][RATE_COUNT] = {
	{0, 0, 0, 0, 77, 0, 127, 160, 184, 220, 242, 250, 257},	/* Norm */
	{0, 0, 0, 0, 83, 0, 135, 169, 193, 229, 250, 257, 264},	/* SGI */
	{0, 0, 0, 0, 96, 0, 182, 259, 328, 451, 553, 598, 640},	/* AGG */
	{0, 0, 0, 0, 106, 0, 199, 282, 357, 487, 593, 640, 683},	/* AGG+SGI */
};

static s32 expected_tpt_mimo2_20MHz[4][RATE_COUNT] = {
	{0, 0, 0, 0, 74, 0, 123, 155, 179, 213, 235, 243, 250},	/* Norm */
	{0, 0, 0, 0, 81, 0, 131, 164, 187, 221, 242, 250, 256},	/* SGI */
	{0, 0, 0, 0, 92, 0, 175, 250, 317, 436, 534, 578, 619},	/* AGG */
	{0, 0, 0, 0, 102, 0, 192, 273, 344, 470, 573, 619, 660},	/* AGG+SGI */
};

static s32 expected_tpt_mimo2_40MHz[4][RATE_COUNT] = {
	{0, 0, 0, 0, 123, 0, 182, 214, 235, 264, 279, 285, 289},	/* Norm */
	{0, 0, 0, 0, 131, 0, 191, 222, 242, 270, 284, 289, 293},	/* SGI */
	{0, 0, 0, 0, 180, 0, 327, 446, 545, 708, 828, 878, 922},	/* AGG */
	{0, 0, 0, 0, 197, 0, 355, 481, 584, 752, 872, 922, 966},	/* AGG+SGI */
};

/* mbps, mcs */
static const struct il_rate_mcs_info il_rate_mcs[RATE_COUNT] = {
	{"1", "BPSK DSSS"},
	{"2", "QPSK DSSS"},
	{"5.5", "BPSK CCK"},
	{"11", "QPSK CCK"},
	{"6", "BPSK 1/2"},
	{"9", "BPSK 1/2"},
	{"12", "QPSK 1/2"},
	{"18", "QPSK 3/4"},
	{"24", "16QAM 1/2"},
	{"36", "16QAM 3/4"},
	{"48", "64QAM 2/3"},
	{"54", "64QAM 3/4"},
	{"60", "64QAM 5/6"},
};

#define MCS_IDX_PER_STREAM	(8)

static inline u8
il4965_rs_extract_rate(u32 rate_n_flags)
{
	return (u8) (rate_n_flags & 0xFF);
}

static void
il4965_rs_rate_scale_clear_win(struct il_rate_scale_data *win)
{
	win->data = 0;
	win->success_counter = 0;
	win->success_ratio = IL_INVALID_VALUE;
	win->counter = 0;
	win->average_tpt = IL_INVALID_VALUE;
	win->stamp = 0;
}

static inline u8
il4965_rs_is_valid_ant(u8 valid_antenna, u8 ant_type)
{
	return (ant_type & valid_antenna) == ant_type;
}

/*
 *	removes the old data from the stats. All data that is older than
 *	TID_MAX_TIME_DIFF, will be deleted.
 */
static void
il4965_rs_tl_rm_old_stats(struct il_traffic_load *tl, u32 curr_time)
{
	/* The oldest age we want to keep */
	u32 oldest_time = curr_time - TID_MAX_TIME_DIFF;

	while (tl->queue_count && tl->time_stamp < oldest_time) {
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
static u8
il4965_rs_tl_add_packet(struct il_lq_sta *lq_data, struct ieee80211_hdr *hdr)
{
	u32 curr_time = jiffies_to_msecs(jiffies);
	u32 time_diff;
	s32 idx;
	struct il_traffic_load *tl = NULL;
	u8 tid;

	if (ieee80211_is_data_qos(hdr->frame_control)) {
		u8 *qc = ieee80211_get_qos_ctl(hdr);
		tid = qc[0] & 0xf;
	} else
		return MAX_TID_COUNT;

	if (unlikely(tid >= TID_MAX_LOAD_COUNT))
		return MAX_TID_COUNT;

	tl = &lq_data->load[tid];

	curr_time -= curr_time % TID_ROUND_VALUE;

	/* Happens only for the first packet. Initialize the data */
	if (!(tl->queue_count)) {
		tl->total = 1;
		tl->time_stamp = curr_time;
		tl->queue_count = 1;
		tl->head = 0;
		tl->packet_count[0] = 1;
		return MAX_TID_COUNT;
	}

	time_diff = TIME_WRAP_AROUND(tl->time_stamp, curr_time);
	idx = time_diff / TID_QUEUE_CELL_SPACING;

	/* The history is too long: remove data that is older than */
	/* TID_MAX_TIME_DIFF */
	if (idx >= TID_QUEUE_MAX_SIZE)
		il4965_rs_tl_rm_old_stats(tl, curr_time);

	idx = (tl->head + idx) % TID_QUEUE_MAX_SIZE;
	tl->packet_count[idx] = tl->packet_count[idx] + 1;
	tl->total = tl->total + 1;

	if ((idx + 1) > tl->queue_count)
		tl->queue_count = idx + 1;

	return tid;
}

/*
	get the traffic load value for tid
*/
static u32
il4965_rs_tl_get_load(struct il_lq_sta *lq_data, u8 tid)
{
	u32 curr_time = jiffies_to_msecs(jiffies);
	u32 time_diff;
	s32 idx;
	struct il_traffic_load *tl = NULL;

	if (tid >= TID_MAX_LOAD_COUNT)
		return 0;

	tl = &(lq_data->load[tid]);

	curr_time -= curr_time % TID_ROUND_VALUE;

	if (!(tl->queue_count))
		return 0;

	time_diff = TIME_WRAP_AROUND(tl->time_stamp, curr_time);
	idx = time_diff / TID_QUEUE_CELL_SPACING;

	/* The history is too long: remove data that is older than */
	/* TID_MAX_TIME_DIFF */
	if (idx >= TID_QUEUE_MAX_SIZE)
		il4965_rs_tl_rm_old_stats(tl, curr_time);

	return tl->total;
}

static int
il4965_rs_tl_turn_on_agg_for_tid(struct il_priv *il, struct il_lq_sta *lq_data,
				 u8 tid, struct ieee80211_sta *sta)
{
	int ret = -EAGAIN;
	u32 load;

	load = il4965_rs_tl_get_load(lq_data, tid);

	if (load > IL_AGG_LOAD_THRESHOLD) {
		D_HT("Starting Tx agg: STA: %pM tid: %d\n", sta->addr, tid);
		ret = ieee80211_start_tx_ba_session(sta, tid, 5000);
		if (ret == -EAGAIN) {
			/*
			 * driver and mac80211 is out of sync
			 * this might be cause by reloading firmware
			 * stop the tx ba session here
			 */
			IL_ERR("Fail start Tx agg on tid: %d\n", tid);
			ieee80211_stop_tx_ba_session(sta, tid);
		}
	} else
		D_HT("Aggregation not enabled for tid %d because load = %u\n",
		     tid, load);

	return ret;
}

static void
il4965_rs_tl_turn_on_agg(struct il_priv *il, u8 tid, struct il_lq_sta *lq_data,
			 struct ieee80211_sta *sta)
{
	if (tid < TID_MAX_LOAD_COUNT)
		il4965_rs_tl_turn_on_agg_for_tid(il, lq_data, tid, sta);
	else
		IL_ERR("tid exceeds max load count: %d/%d\n", tid,
		       TID_MAX_LOAD_COUNT);
}

static inline int
il4965_get_il4965_num_of_ant_from_rate(u32 rate_n_flags)
{
	return !!(rate_n_flags & RATE_MCS_ANT_A_MSK) +
	    !!(rate_n_flags & RATE_MCS_ANT_B_MSK) +
	    !!(rate_n_flags & RATE_MCS_ANT_C_MSK);
}

/*
 * Static function to get the expected throughput from an il_scale_tbl_info
 * that wraps a NULL pointer check
 */
static s32
il4965_get_expected_tpt(struct il_scale_tbl_info *tbl, int rs_idx)
{
	if (tbl->expected_tpt)
		return tbl->expected_tpt[rs_idx];
	return 0;
}

/**
 * il4965_rs_collect_tx_data - Update the success/failure sliding win
 *
 * We keep a sliding win of the last 62 packets transmitted
 * at this rate.  win->data contains the bitmask of successful
 * packets.
 */
static int
il4965_rs_collect_tx_data(struct il_scale_tbl_info *tbl, int scale_idx,
			  int attempts, int successes)
{
	struct il_rate_scale_data *win = NULL;
	static const u64 mask = (((u64) 1) << (RATE_MAX_WINDOW - 1));
	s32 fail_count, tpt;

	if (scale_idx < 0 || scale_idx >= RATE_COUNT)
		return -EINVAL;

	/* Select win for current tx bit rate */
	win = &(tbl->win[scale_idx]);

	/* Get expected throughput */
	tpt = il4965_get_expected_tpt(tbl, scale_idx);

	/*
	 * Keep track of only the latest 62 tx frame attempts in this rate's
	 * history win; anything older isn't really relevant any more.
	 * If we have filled up the sliding win, drop the oldest attempt;
	 * if the oldest attempt (highest bit in bitmap) shows "success",
	 * subtract "1" from the success counter (this is the main reason
	 * we keep these bitmaps!).
	 */
	while (attempts > 0) {
		if (win->counter >= RATE_MAX_WINDOW) {

			/* remove earliest */
			win->counter = RATE_MAX_WINDOW - 1;

			if (win->data & mask) {
				win->data &= ~mask;
				win->success_counter--;
			}
		}

		/* Increment frames-attempted counter */
		win->counter++;

		/* Shift bitmap by one frame to throw away oldest history */
		win->data <<= 1;

		/* Mark the most recent #successes attempts as successful */
		if (successes > 0) {
			win->success_counter++;
			win->data |= 0x1;
			successes--;
		}

		attempts--;
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
		win->average_tpt = (win->success_ratio * tpt + 64) / 128;
	else
		win->average_tpt = IL_INVALID_VALUE;

	/* Tag this win as having been updated */
	win->stamp = jiffies;

	return 0;
}

/*
 * Fill uCode API rate_n_flags field, based on "search" or "active" table.
 */
static u32
il4965_rate_n_flags_from_tbl(struct il_priv *il, struct il_scale_tbl_info *tbl,
			     int idx, u8 use_green)
{
	u32 rate_n_flags = 0;

	if (is_legacy(tbl->lq_type)) {
		rate_n_flags = il_rates[idx].plcp;
		if (idx >= IL_FIRST_CCK_RATE && idx <= IL_LAST_CCK_RATE)
			rate_n_flags |= RATE_MCS_CCK_MSK;

	} else if (is_Ht(tbl->lq_type)) {
		if (idx > IL_LAST_OFDM_RATE) {
			IL_ERR("Invalid HT rate idx %d\n", idx);
			idx = IL_LAST_OFDM_RATE;
		}
		rate_n_flags = RATE_MCS_HT_MSK;

		if (is_siso(tbl->lq_type))
			rate_n_flags |= il_rates[idx].plcp_siso;
		else
			rate_n_flags |= il_rates[idx].plcp_mimo2;
	} else {
		IL_ERR("Invalid tbl->lq_type %d\n", tbl->lq_type);
	}

	rate_n_flags |=
	    ((tbl->ant_type << RATE_MCS_ANT_POS) & RATE_MCS_ANT_ABC_MSK);

	if (is_Ht(tbl->lq_type)) {
		if (tbl->is_ht40) {
			if (tbl->is_dup)
				rate_n_flags |= RATE_MCS_DUP_MSK;
			else
				rate_n_flags |= RATE_MCS_HT40_MSK;
		}
		if (tbl->is_SGI)
			rate_n_flags |= RATE_MCS_SGI_MSK;

		if (use_green) {
			rate_n_flags |= RATE_MCS_GF_MSK;
			if (is_siso(tbl->lq_type) && tbl->is_SGI) {
				rate_n_flags &= ~RATE_MCS_SGI_MSK;
				IL_ERR("GF was set with SGI:SISO\n");
			}
		}
	}
	return rate_n_flags;
}

/*
 * Interpret uCode API's rate_n_flags format,
 * fill "search" or "active" tx mode table.
 */
static int
il4965_rs_get_tbl_info_from_mcs(const u32 rate_n_flags,
				enum ieee80211_band band,
				struct il_scale_tbl_info *tbl, int *rate_idx)
{
	u32 ant_msk = (rate_n_flags & RATE_MCS_ANT_ABC_MSK);
	u8 il4965_num_of_ant =
	    il4965_get_il4965_num_of_ant_from_rate(rate_n_flags);
	u8 mcs;

	memset(tbl, 0, sizeof(struct il_scale_tbl_info));
	*rate_idx = il4965_hwrate_to_plcp_idx(rate_n_flags);

	if (*rate_idx == RATE_INVALID) {
		*rate_idx = -1;
		return -EINVAL;
	}
	tbl->is_SGI = 0;	/* default legacy setup */
	tbl->is_ht40 = 0;
	tbl->is_dup = 0;
	tbl->ant_type = (ant_msk >> RATE_MCS_ANT_POS);
	tbl->lq_type = LQ_NONE;
	tbl->max_search = IL_MAX_SEARCH;

	/* legacy rate format */
	if (!(rate_n_flags & RATE_MCS_HT_MSK)) {
		if (il4965_num_of_ant == 1) {
			if (band == IEEE80211_BAND_5GHZ)
				tbl->lq_type = LQ_A;
			else
				tbl->lq_type = LQ_G;
		}
		/* HT rate format */
	} else {
		if (rate_n_flags & RATE_MCS_SGI_MSK)
			tbl->is_SGI = 1;

		if ((rate_n_flags & RATE_MCS_HT40_MSK) ||
		    (rate_n_flags & RATE_MCS_DUP_MSK))
			tbl->is_ht40 = 1;

		if (rate_n_flags & RATE_MCS_DUP_MSK)
			tbl->is_dup = 1;

		mcs = il4965_rs_extract_rate(rate_n_flags);

		/* SISO */
		if (mcs <= RATE_SISO_60M_PLCP) {
			if (il4965_num_of_ant == 1)
				tbl->lq_type = LQ_SISO;	/*else NONE */
			/* MIMO2 */
		} else {
			if (il4965_num_of_ant == 2)
				tbl->lq_type = LQ_MIMO2;
		}
	}
	return 0;
}

/* switch to another antenna/antennas and return 1 */
/* if no other valid antenna found, return 0 */
static int
il4965_rs_toggle_antenna(u32 valid_ant, u32 *rate_n_flags,
			 struct il_scale_tbl_info *tbl)
{
	u8 new_ant_type;

	if (!tbl->ant_type || tbl->ant_type > ANT_ABC)
		return 0;

	if (!il4965_rs_is_valid_ant(valid_ant, tbl->ant_type))
		return 0;

	new_ant_type = ant_toggle_lookup[tbl->ant_type];

	while (new_ant_type != tbl->ant_type &&
	       !il4965_rs_is_valid_ant(valid_ant, new_ant_type))
		new_ant_type = ant_toggle_lookup[new_ant_type];

	if (new_ant_type == tbl->ant_type)
		return 0;

	tbl->ant_type = new_ant_type;
	*rate_n_flags &= ~RATE_MCS_ANT_ABC_MSK;
	*rate_n_flags |= new_ant_type << RATE_MCS_ANT_POS;
	return 1;
}

/**
 * Green-field mode is valid if the station supports it and
 * there are no non-GF stations present in the BSS.
 */
static bool
il4965_rs_use_green(struct il_priv *il, struct ieee80211_sta *sta)
{
	return (sta->ht_cap.cap & IEEE80211_HT_CAP_GRN_FLD) &&
	       !il->ht.non_gf_sta_present;
}

/**
 * il4965_rs_get_supported_rates - get the available rates
 *
 * if management frame or broadcast frame only return
 * basic available rates.
 *
 */
static u16
il4965_rs_get_supported_rates(struct il_lq_sta *lq_sta,
			      struct ieee80211_hdr *hdr,
			      enum il_table_type rate_type)
{
	if (is_legacy(rate_type)) {
		return lq_sta->active_legacy_rate;
	} else {
		if (is_siso(rate_type))
			return lq_sta->active_siso_rate;
		else
			return lq_sta->active_mimo2_rate;
	}
}

static u16
il4965_rs_get_adjacent_rate(struct il_priv *il, u8 idx, u16 rate_mask,
			    int rate_type)
{
	u8 high = RATE_INVALID;
	u8 low = RATE_INVALID;

	/* 802.11A or ht walks to the next literal adjacent rate in
	 * the rate table */
	if (is_a_band(rate_type) || !is_legacy(rate_type)) {
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
		for (mask = (1 << i); i < RATE_COUNT; i++, mask <<= 1) {
			if (rate_mask & mask) {
				high = i;
				break;
			}
		}

		return (high << 8) | low;
	}

	low = idx;
	while (low != RATE_INVALID) {
		low = il_rates[low].prev_rs;
		if (low == RATE_INVALID)
			break;
		if (rate_mask & (1 << low))
			break;
		D_RATE("Skipping masked lower rate: %d\n", low);
	}

	high = idx;
	while (high != RATE_INVALID) {
		high = il_rates[high].next_rs;
		if (high == RATE_INVALID)
			break;
		if (rate_mask & (1 << high))
			break;
		D_RATE("Skipping masked higher rate: %d\n", high);
	}

	return (high << 8) | low;
}

static u32
il4965_rs_get_lower_rate(struct il_lq_sta *lq_sta,
			 struct il_scale_tbl_info *tbl, u8 scale_idx,
			 u8 ht_possible)
{
	s32 low;
	u16 rate_mask;
	u16 high_low;
	u8 switch_to_legacy = 0;
	u8 is_green = lq_sta->is_green;
	struct il_priv *il = lq_sta->drv;

	/* check if we need to switch from HT to legacy rates.
	 * assumption is that mandatory rates (1Mbps or 6Mbps)
	 * are always supported (spec demand) */
	if (!is_legacy(tbl->lq_type) && (!ht_possible || !scale_idx)) {
		switch_to_legacy = 1;
		scale_idx = rs_ht_to_legacy[scale_idx];
		if (lq_sta->band == IEEE80211_BAND_5GHZ)
			tbl->lq_type = LQ_A;
		else
			tbl->lq_type = LQ_G;

		if (il4965_num_of_ant(tbl->ant_type) > 1)
			tbl->ant_type =
			    il4965_first_antenna(il->hw_params.valid_tx_ant);

		tbl->is_ht40 = 0;
		tbl->is_SGI = 0;
		tbl->max_search = IL_MAX_SEARCH;
	}

	rate_mask = il4965_rs_get_supported_rates(lq_sta, NULL, tbl->lq_type);

	/* Mask with station rate restriction */
	if (is_legacy(tbl->lq_type)) {
		/* supp_rates has no CCK bits in A mode */
		if (lq_sta->band == IEEE80211_BAND_5GHZ)
			rate_mask =
			    (u16) (rate_mask &
				   (lq_sta->supp_rates << IL_FIRST_OFDM_RATE));
		else
			rate_mask = (u16) (rate_mask & lq_sta->supp_rates);
	}

	/* If we switched from HT to legacy, check current rate */
	if (switch_to_legacy && (rate_mask & (1 << scale_idx))) {
		low = scale_idx;
		goto out;
	}

	high_low =
	    il4965_rs_get_adjacent_rate(lq_sta->drv, scale_idx, rate_mask,
					tbl->lq_type);
	low = high_low & 0xff;

	if (low == RATE_INVALID)
		low = scale_idx;

out:
	return il4965_rate_n_flags_from_tbl(lq_sta->drv, tbl, low, is_green);
}

/*
 * Simple function to compare two rate scale table types
 */
static bool
il4965_table_type_matches(struct il_scale_tbl_info *a,
			  struct il_scale_tbl_info *b)
{
	return (a->lq_type == b->lq_type && a->ant_type == b->ant_type &&
		a->is_SGI == b->is_SGI);
}

/*
 * mac80211 sends us Tx status
 */
static void
il4965_rs_tx_status(void *il_r, struct ieee80211_supported_band *sband,
		    struct ieee80211_sta *sta, void *il_sta,
		    struct sk_buff *skb)
{
	int legacy_success;
	int retries;
	int rs_idx, mac_idx, i;
	struct il_lq_sta *lq_sta = il_sta;
	struct il_link_quality_cmd *table;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct il_priv *il = (struct il_priv *)il_r;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	enum mac80211_rate_control_flags mac_flags;
	u32 tx_rate;
	struct il_scale_tbl_info tbl_type;
	struct il_scale_tbl_info *curr_tbl, *other_tbl, *tmp_tbl;

	D_RATE("get frame ack response, update rate scale win\n");

	/* Treat uninitialized rate scaling data same as non-existing. */
	if (!lq_sta) {
		D_RATE("Station rate scaling not created yet.\n");
		return;
	} else if (!lq_sta->drv) {
		D_RATE("Rate scaling not initialized yet.\n");
		return;
	}

	if (!ieee80211_is_data(hdr->frame_control) ||
	    (info->flags & IEEE80211_TX_CTL_NO_ACK))
		return;

	/* This packet was aggregated but doesn't carry status info */
	if ((info->flags & IEEE80211_TX_CTL_AMPDU) &&
	    !(info->flags & IEEE80211_TX_STAT_AMPDU))
		return;

	/*
	 * Ignore this Tx frame response if its initial rate doesn't match
	 * that of latest Link Quality command.  There may be stragglers
	 * from a previous Link Quality command, but we're no longer interested
	 * in those; they're either from the "active" mode while we're trying
	 * to check "search" mode, or a prior "search" mode after we've moved
	 * to a new "search" mode (which might become the new "active" mode).
	 */
	table = &lq_sta->lq;
	tx_rate = le32_to_cpu(table->rs_table[0].rate_n_flags);
	il4965_rs_get_tbl_info_from_mcs(tx_rate, il->band, &tbl_type, &rs_idx);
	if (il->band == IEEE80211_BAND_5GHZ)
		rs_idx -= IL_FIRST_OFDM_RATE;
	mac_flags = info->status.rates[0].flags;
	mac_idx = info->status.rates[0].idx;
	/* For HT packets, map MCS to PLCP */
	if (mac_flags & IEEE80211_TX_RC_MCS) {
		mac_idx &= RATE_MCS_CODE_MSK;	/* Remove # of streams */
		if (mac_idx >= (RATE_9M_IDX - IL_FIRST_OFDM_RATE))
			mac_idx++;
		/*
		 * mac80211 HT idx is always zero-idxed; we need to move
		 * HT OFDM rates after CCK rates in 2.4 GHz band
		 */
		if (il->band == IEEE80211_BAND_2GHZ)
			mac_idx += IL_FIRST_OFDM_RATE;
	}
	/* Here we actually compare this rate to the latest LQ command */
	if (mac_idx < 0 ||
	    tbl_type.is_SGI != !!(mac_flags & IEEE80211_TX_RC_SHORT_GI) ||
	    tbl_type.is_ht40 != !!(mac_flags & IEEE80211_TX_RC_40_MHZ_WIDTH) ||
	    tbl_type.is_dup != !!(mac_flags & IEEE80211_TX_RC_DUP_DATA) ||
	    tbl_type.ant_type != info->status.antenna ||
	    !!(tx_rate & RATE_MCS_HT_MSK) != !!(mac_flags & IEEE80211_TX_RC_MCS)
	    || !!(tx_rate & RATE_MCS_GF_MSK) !=
	    !!(mac_flags & IEEE80211_TX_RC_GREEN_FIELD) || rs_idx != mac_idx) {
		D_RATE("initial rate %d does not match %d (0x%x)\n", mac_idx,
		       rs_idx, tx_rate);
		/*
		 * Since rates mis-match, the last LQ command may have failed.
		 * After IL_MISSED_RATE_MAX mis-matches, resync the uCode with
		 * ... driver.
		 */
		lq_sta->missed_rate_counter++;
		if (lq_sta->missed_rate_counter > IL_MISSED_RATE_MAX) {
			lq_sta->missed_rate_counter = 0;
			il_send_lq_cmd(il, &lq_sta->lq, CMD_ASYNC, false);
		}
		/* Regardless, ignore this status info for outdated rate */
		return;
	} else
		/* Rate did match, so reset the missed_rate_counter */
		lq_sta->missed_rate_counter = 0;

	/* Figure out if rate scale algorithm is in active or search table */
	if (il4965_table_type_matches
	    (&tbl_type, &(lq_sta->lq_info[lq_sta->active_tbl]))) {
		curr_tbl = &(lq_sta->lq_info[lq_sta->active_tbl]);
		other_tbl = &(lq_sta->lq_info[1 - lq_sta->active_tbl]);
	} else
	    if (il4965_table_type_matches
		(&tbl_type, &lq_sta->lq_info[1 - lq_sta->active_tbl])) {
		curr_tbl = &(lq_sta->lq_info[1 - lq_sta->active_tbl]);
		other_tbl = &(lq_sta->lq_info[lq_sta->active_tbl]);
	} else {
		D_RATE("Neither active nor search matches tx rate\n");
		tmp_tbl = &(lq_sta->lq_info[lq_sta->active_tbl]);
		D_RATE("active- lq:%x, ant:%x, SGI:%d\n", tmp_tbl->lq_type,
		       tmp_tbl->ant_type, tmp_tbl->is_SGI);
		tmp_tbl = &(lq_sta->lq_info[1 - lq_sta->active_tbl]);
		D_RATE("search- lq:%x, ant:%x, SGI:%d\n", tmp_tbl->lq_type,
		       tmp_tbl->ant_type, tmp_tbl->is_SGI);
		D_RATE("actual- lq:%x, ant:%x, SGI:%d\n", tbl_type.lq_type,
		       tbl_type.ant_type, tbl_type.is_SGI);
		/*
		 * no matching table found, let's by-pass the data collection
		 * and continue to perform rate scale to find the rate table
		 */
		il4965_rs_stay_in_table(lq_sta, true);
		goto done;
	}

	/*
	 * Updating the frame history depends on whether packets were
	 * aggregated.
	 *
	 * For aggregation, all packets were transmitted at the same rate, the
	 * first idx into rate scale table.
	 */
	if (info->flags & IEEE80211_TX_STAT_AMPDU) {
		tx_rate = le32_to_cpu(table->rs_table[0].rate_n_flags);
		il4965_rs_get_tbl_info_from_mcs(tx_rate, il->band, &tbl_type,
						&rs_idx);
		il4965_rs_collect_tx_data(curr_tbl, rs_idx,
					  info->status.ampdu_len,
					  info->status.ampdu_ack_len);

		/* Update success/fail counts if not searching for new mode */
		if (lq_sta->stay_in_tbl) {
			lq_sta->total_success += info->status.ampdu_ack_len;
			lq_sta->total_failed +=
			    (info->status.ampdu_len -
			     info->status.ampdu_ack_len);
		}
	} else {
		/*
		 * For legacy, update frame history with for each Tx retry.
		 */
		retries = info->status.rates[0].count - 1;
		/* HW doesn't send more than 15 retries */
		retries = min(retries, 15);

		/* The last transmission may have been successful */
		legacy_success = !!(info->flags & IEEE80211_TX_STAT_ACK);
		/* Collect data for each rate used during failed TX attempts */
		for (i = 0; i <= retries; ++i) {
			tx_rate = le32_to_cpu(table->rs_table[i].rate_n_flags);
			il4965_rs_get_tbl_info_from_mcs(tx_rate, il->band,
							&tbl_type, &rs_idx);
			/*
			 * Only collect stats if retried rate is in the same RS
			 * table as active/search.
			 */
			if (il4965_table_type_matches(&tbl_type, curr_tbl))
				tmp_tbl = curr_tbl;
			else if (il4965_table_type_matches
				 (&tbl_type, other_tbl))
				tmp_tbl = other_tbl;
			else
				continue;
			il4965_rs_collect_tx_data(tmp_tbl, rs_idx, 1,
						  i <
						  retries ? 0 : legacy_success);
		}

		/* Update success/fail counts if not searching for new mode */
		if (lq_sta->stay_in_tbl) {
			lq_sta->total_success += legacy_success;
			lq_sta->total_failed += retries + (1 - legacy_success);
		}
	}
	/* The last TX rate is cached in lq_sta; it's set in if/else above */
	lq_sta->last_rate_n_flags = tx_rate;
done:
	/* See if there's a better rate or modulation mode to try. */
	if (sta->supp_rates[sband->band])
		il4965_rs_rate_scale_perform(il, skb, sta, lq_sta);
}

/*
 * Begin a period of staying with a selected modulation mode.
 * Set "stay_in_tbl" flag to prevent any mode switches.
 * Set frame tx success limits according to legacy vs. high-throughput,
 * and reset overall (spanning all rates) tx success history stats.
 * These control how long we stay using same modulation mode before
 * searching for a new mode.
 */
static void
il4965_rs_set_stay_in_table(struct il_priv *il, u8 is_legacy,
			    struct il_lq_sta *lq_sta)
{
	D_RATE("we are staying in the same table\n");
	lq_sta->stay_in_tbl = 1;	/* only place this gets set */
	if (is_legacy) {
		lq_sta->table_count_limit = IL_LEGACY_TBL_COUNT;
		lq_sta->max_failure_limit = IL_LEGACY_FAILURE_LIMIT;
		lq_sta->max_success_limit = IL_LEGACY_SUCCESS_LIMIT;
	} else {
		lq_sta->table_count_limit = IL_NONE_LEGACY_TBL_COUNT;
		lq_sta->max_failure_limit = IL_NONE_LEGACY_FAILURE_LIMIT;
		lq_sta->max_success_limit = IL_NONE_LEGACY_SUCCESS_LIMIT;
	}
	lq_sta->table_count = 0;
	lq_sta->total_failed = 0;
	lq_sta->total_success = 0;
	lq_sta->flush_timer = jiffies;
	lq_sta->action_counter = 0;
}

/*
 * Find correct throughput table for given mode of modulation
 */
static void
il4965_rs_set_expected_tpt_table(struct il_lq_sta *lq_sta,
				 struct il_scale_tbl_info *tbl)
{
	/* Used to choose among HT tables */
	s32(*ht_tbl_pointer)[RATE_COUNT];

	/* Check for invalid LQ type */
	if (WARN_ON_ONCE(!is_legacy(tbl->lq_type) && !is_Ht(tbl->lq_type))) {
		tbl->expected_tpt = expected_tpt_legacy;
		return;
	}

	/* Legacy rates have only one table */
	if (is_legacy(tbl->lq_type)) {
		tbl->expected_tpt = expected_tpt_legacy;
		return;
	}

	/* Choose among many HT tables depending on number of streams
	 * (SISO/MIMO2), channel width (20/40), SGI, and aggregation
	 * status */
	if (is_siso(tbl->lq_type) && (!tbl->is_ht40 || lq_sta->is_dup))
		ht_tbl_pointer = expected_tpt_siso20MHz;
	else if (is_siso(tbl->lq_type))
		ht_tbl_pointer = expected_tpt_siso40MHz;
	else if (is_mimo2(tbl->lq_type) && (!tbl->is_ht40 || lq_sta->is_dup))
		ht_tbl_pointer = expected_tpt_mimo2_20MHz;
	else			/* if (is_mimo2(tbl->lq_type)) <-- must be true */
		ht_tbl_pointer = expected_tpt_mimo2_40MHz;

	if (!tbl->is_SGI && !lq_sta->is_agg)	/* Normal */
		tbl->expected_tpt = ht_tbl_pointer[0];
	else if (tbl->is_SGI && !lq_sta->is_agg)	/* SGI */
		tbl->expected_tpt = ht_tbl_pointer[1];
	else if (!tbl->is_SGI && lq_sta->is_agg)	/* AGG */
		tbl->expected_tpt = ht_tbl_pointer[2];
	else			/* AGG+SGI */
		tbl->expected_tpt = ht_tbl_pointer[3];
}

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
static s32
il4965_rs_get_best_rate(struct il_priv *il, struct il_lq_sta *lq_sta,
			struct il_scale_tbl_info *tbl,	/* "search" */
			u16 rate_mask, s8 idx)
{
	/* "active" values */
	struct il_scale_tbl_info *active_tbl =
	    &(lq_sta->lq_info[lq_sta->active_tbl]);
	s32 active_sr = active_tbl->win[idx].success_ratio;
	s32 active_tpt = active_tbl->expected_tpt[idx];

	/* expected "search" throughput */
	s32 *tpt_tbl = tbl->expected_tpt;

	s32 new_rate, high, low, start_hi;
	u16 high_low;
	s8 rate = idx;

	new_rate = high = low = start_hi = RATE_INVALID;

	for (;;) {
		high_low =
		    il4965_rs_get_adjacent_rate(il, rate, rate_mask,
						tbl->lq_type);

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
		if ((100 * tpt_tbl[rate] > lq_sta->last_tpt &&
		     (active_sr > RATE_DECREASE_TH && active_sr <= RATE_HIGH_TH
		      && tpt_tbl[rate] <= active_tpt)) ||
		    (active_sr >= RATE_SCALE_SWITCH &&
		     tpt_tbl[rate] > active_tpt)) {

			/* (2nd or later pass)
			 * If we've already tried to raise the rate, and are
			 * now trying to lower it, use the higher rate. */
			if (start_hi != RATE_INVALID) {
				new_rate = start_hi;
				break;
			}

			new_rate = rate;

			/* Loop again with lower rate */
			if (low != RATE_INVALID)
				rate = low;

			/* Lower rate not available, use the original */
			else
				break;

			/* Else try to raise the "search" rate to match "active" */
		} else {
			/* (2nd or later pass)
			 * If we've already tried to lower the rate, and are
			 * now trying to raise it, use the lower rate. */
			if (new_rate != RATE_INVALID)
				break;

			/* Loop again with higher rate */
			else if (high != RATE_INVALID) {
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

/*
 * Set up search table for MIMO2
 */
static int
il4965_rs_switch_to_mimo2(struct il_priv *il, struct il_lq_sta *lq_sta,
			  struct ieee80211_conf *conf,
			  struct ieee80211_sta *sta,
			  struct il_scale_tbl_info *tbl, int idx)
{
	u16 rate_mask;
	s32 rate;
	s8 is_green = lq_sta->is_green;

	if (!conf_is_ht(conf) || !sta->ht_cap.ht_supported)
		return -1;

	if (sta->smps_mode == IEEE80211_SMPS_STATIC)
		return -1;

	/* Need both Tx chains/antennas to support MIMO */
	if (il->hw_params.tx_chains_num < 2)
		return -1;

	D_RATE("LQ: try to switch to MIMO2\n");

	tbl->lq_type = LQ_MIMO2;
	tbl->is_dup = lq_sta->is_dup;
	tbl->action = 0;
	tbl->max_search = IL_MAX_SEARCH;
	rate_mask = lq_sta->active_mimo2_rate;

	if (il_is_ht40_tx_allowed(il, &sta->ht_cap))
		tbl->is_ht40 = 1;
	else
		tbl->is_ht40 = 0;

	il4965_rs_set_expected_tpt_table(lq_sta, tbl);

	rate = il4965_rs_get_best_rate(il, lq_sta, tbl, rate_mask, idx);

	D_RATE("LQ: MIMO2 best rate %d mask %X\n", rate, rate_mask);
	if (rate == RATE_INVALID || !((1 << rate) & rate_mask)) {
		D_RATE("Can't switch with idx %d rate mask %x\n", rate,
		       rate_mask);
		return -1;
	}
	tbl->current_rate =
	    il4965_rate_n_flags_from_tbl(il, tbl, rate, is_green);

	D_RATE("LQ: Switch to new mcs %X idx is green %X\n", tbl->current_rate,
	       is_green);
	return 0;
}

/*
 * Set up search table for SISO
 */
static int
il4965_rs_switch_to_siso(struct il_priv *il, struct il_lq_sta *lq_sta,
			 struct ieee80211_conf *conf, struct ieee80211_sta *sta,
			 struct il_scale_tbl_info *tbl, int idx)
{
	u16 rate_mask;
	u8 is_green = lq_sta->is_green;
	s32 rate;

	if (!conf_is_ht(conf) || !sta->ht_cap.ht_supported)
		return -1;

	D_RATE("LQ: try to switch to SISO\n");

	tbl->is_dup = lq_sta->is_dup;
	tbl->lq_type = LQ_SISO;
	tbl->action = 0;
	tbl->max_search = IL_MAX_SEARCH;
	rate_mask = lq_sta->active_siso_rate;

	if (il_is_ht40_tx_allowed(il, &sta->ht_cap))
		tbl->is_ht40 = 1;
	else
		tbl->is_ht40 = 0;

	if (is_green)
		tbl->is_SGI = 0;	/*11n spec: no SGI in SISO+Greenfield */

	il4965_rs_set_expected_tpt_table(lq_sta, tbl);
	rate = il4965_rs_get_best_rate(il, lq_sta, tbl, rate_mask, idx);

	D_RATE("LQ: get best rate %d mask %X\n", rate, rate_mask);
	if (rate == RATE_INVALID || !((1 << rate) & rate_mask)) {
		D_RATE("can not switch with idx %d rate mask %x\n", rate,
		       rate_mask);
		return -1;
	}
	tbl->current_rate =
	    il4965_rate_n_flags_from_tbl(il, tbl, rate, is_green);
	D_RATE("LQ: Switch to new mcs %X idx is green %X\n", tbl->current_rate,
	       is_green);
	return 0;
}

/*
 * Try to switch to new modulation mode from legacy
 */
static int
il4965_rs_move_legacy_other(struct il_priv *il, struct il_lq_sta *lq_sta,
			    struct ieee80211_conf *conf,
			    struct ieee80211_sta *sta, int idx)
{
	struct il_scale_tbl_info *tbl = &(lq_sta->lq_info[lq_sta->active_tbl]);
	struct il_scale_tbl_info *search_tbl =
	    &(lq_sta->lq_info[(1 - lq_sta->active_tbl)]);
	struct il_rate_scale_data *win = &(tbl->win[idx]);
	u32 sz =
	    (sizeof(struct il_scale_tbl_info) -
	     (sizeof(struct il_rate_scale_data) * RATE_COUNT));
	u8 start_action;
	u8 valid_tx_ant = il->hw_params.valid_tx_ant;
	u8 tx_chains_num = il->hw_params.tx_chains_num;
	int ret = 0;
	u8 update_search_tbl_counter = 0;

	tbl->action = IL_LEGACY_SWITCH_SISO;

	start_action = tbl->action;
	for (;;) {
		lq_sta->action_counter++;
		switch (tbl->action) {
		case IL_LEGACY_SWITCH_ANTENNA1:
		case IL_LEGACY_SWITCH_ANTENNA2:
			D_RATE("LQ: Legacy toggle Antenna\n");

			if ((tbl->action == IL_LEGACY_SWITCH_ANTENNA1 &&
			     tx_chains_num <= 1) ||
			    (tbl->action == IL_LEGACY_SWITCH_ANTENNA2 &&
			     tx_chains_num <= 2))
				break;

			/* Don't change antenna if success has been great */
			if (win->success_ratio >= IL_RS_GOOD_RATIO)
				break;

			/* Set up search table to try other antenna */
			memcpy(search_tbl, tbl, sz);

			if (il4965_rs_toggle_antenna
			    (valid_tx_ant, &search_tbl->current_rate,
			     search_tbl)) {
				update_search_tbl_counter = 1;
				il4965_rs_set_expected_tpt_table(lq_sta,
								 search_tbl);
				goto out;
			}
			break;
		case IL_LEGACY_SWITCH_SISO:
			D_RATE("LQ: Legacy switch to SISO\n");

			/* Set up search table to try SISO */
			memcpy(search_tbl, tbl, sz);
			search_tbl->is_SGI = 0;
			ret =
			    il4965_rs_switch_to_siso(il, lq_sta, conf, sta,
						     search_tbl, idx);
			if (!ret) {
				lq_sta->action_counter = 0;
				goto out;
			}

			break;
		case IL_LEGACY_SWITCH_MIMO2_AB:
		case IL_LEGACY_SWITCH_MIMO2_AC:
		case IL_LEGACY_SWITCH_MIMO2_BC:
			D_RATE("LQ: Legacy switch to MIMO2\n");

			/* Set up search table to try MIMO */
			memcpy(search_tbl, tbl, sz);
			search_tbl->is_SGI = 0;

			if (tbl->action == IL_LEGACY_SWITCH_MIMO2_AB)
				search_tbl->ant_type = ANT_AB;
			else if (tbl->action == IL_LEGACY_SWITCH_MIMO2_AC)
				search_tbl->ant_type = ANT_AC;
			else
				search_tbl->ant_type = ANT_BC;

			if (!il4965_rs_is_valid_ant
			    (valid_tx_ant, search_tbl->ant_type))
				break;

			ret =
			    il4965_rs_switch_to_mimo2(il, lq_sta, conf, sta,
						      search_tbl, idx);
			if (!ret) {
				lq_sta->action_counter = 0;
				goto out;
			}
			break;
		}
		tbl->action++;
		if (tbl->action > IL_LEGACY_SWITCH_MIMO2_BC)
			tbl->action = IL_LEGACY_SWITCH_ANTENNA1;

		if (tbl->action == start_action)
			break;

	}
	search_tbl->lq_type = LQ_NONE;
	return 0;

out:
	lq_sta->search_better_tbl = 1;
	tbl->action++;
	if (tbl->action > IL_LEGACY_SWITCH_MIMO2_BC)
		tbl->action = IL_LEGACY_SWITCH_ANTENNA1;
	if (update_search_tbl_counter)
		search_tbl->action = tbl->action;
	return 0;

}

/*
 * Try to switch to new modulation mode from SISO
 */
static int
il4965_rs_move_siso_to_other(struct il_priv *il, struct il_lq_sta *lq_sta,
			     struct ieee80211_conf *conf,
			     struct ieee80211_sta *sta, int idx)
{
	u8 is_green = lq_sta->is_green;
	struct il_scale_tbl_info *tbl = &(lq_sta->lq_info[lq_sta->active_tbl]);
	struct il_scale_tbl_info *search_tbl =
	    &(lq_sta->lq_info[(1 - lq_sta->active_tbl)]);
	struct il_rate_scale_data *win = &(tbl->win[idx]);
	struct ieee80211_sta_ht_cap *ht_cap = &sta->ht_cap;
	u32 sz =
	    (sizeof(struct il_scale_tbl_info) -
	     (sizeof(struct il_rate_scale_data) * RATE_COUNT));
	u8 start_action;
	u8 valid_tx_ant = il->hw_params.valid_tx_ant;
	u8 tx_chains_num = il->hw_params.tx_chains_num;
	u8 update_search_tbl_counter = 0;
	int ret;

	start_action = tbl->action;

	for (;;) {
		lq_sta->action_counter++;
		switch (tbl->action) {
		case IL_SISO_SWITCH_ANTENNA1:
		case IL_SISO_SWITCH_ANTENNA2:
			D_RATE("LQ: SISO toggle Antenna\n");
			if ((tbl->action == IL_SISO_SWITCH_ANTENNA1 &&
			     tx_chains_num <= 1) ||
			    (tbl->action == IL_SISO_SWITCH_ANTENNA2 &&
			     tx_chains_num <= 2))
				break;

			if (win->success_ratio >= IL_RS_GOOD_RATIO)
				break;

			memcpy(search_tbl, tbl, sz);
			if (il4965_rs_toggle_antenna
			    (valid_tx_ant, &search_tbl->current_rate,
			     search_tbl)) {
				update_search_tbl_counter = 1;
				goto out;
			}
			break;
		case IL_SISO_SWITCH_MIMO2_AB:
		case IL_SISO_SWITCH_MIMO2_AC:
		case IL_SISO_SWITCH_MIMO2_BC:
			D_RATE("LQ: SISO switch to MIMO2\n");
			memcpy(search_tbl, tbl, sz);
			search_tbl->is_SGI = 0;

			if (tbl->action == IL_SISO_SWITCH_MIMO2_AB)
				search_tbl->ant_type = ANT_AB;
			else if (tbl->action == IL_SISO_SWITCH_MIMO2_AC)
				search_tbl->ant_type = ANT_AC;
			else
				search_tbl->ant_type = ANT_BC;

			if (!il4965_rs_is_valid_ant
			    (valid_tx_ant, search_tbl->ant_type))
				break;

			ret =
			    il4965_rs_switch_to_mimo2(il, lq_sta, conf, sta,
						      search_tbl, idx);
			if (!ret)
				goto out;
			break;
		case IL_SISO_SWITCH_GI:
			if (!tbl->is_ht40 &&
			    !(ht_cap->cap & IEEE80211_HT_CAP_SGI_20))
				break;
			if (tbl->is_ht40 &&
			    !(ht_cap->cap & IEEE80211_HT_CAP_SGI_40))
				break;

			D_RATE("LQ: SISO toggle SGI/NGI\n");

			memcpy(search_tbl, tbl, sz);
			if (is_green) {
				if (!tbl->is_SGI)
					break;
				else
					IL_ERR("SGI was set in GF+SISO\n");
			}
			search_tbl->is_SGI = !tbl->is_SGI;
			il4965_rs_set_expected_tpt_table(lq_sta, search_tbl);
			if (tbl->is_SGI) {
				s32 tpt = lq_sta->last_tpt / 100;
				if (tpt >= search_tbl->expected_tpt[idx])
					break;
			}
			search_tbl->current_rate =
			    il4965_rate_n_flags_from_tbl(il, search_tbl, idx,
							 is_green);
			update_search_tbl_counter = 1;
			goto out;
		}
		tbl->action++;
		if (tbl->action > IL_SISO_SWITCH_GI)
			tbl->action = IL_SISO_SWITCH_ANTENNA1;

		if (tbl->action == start_action)
			break;
	}
	search_tbl->lq_type = LQ_NONE;
	return 0;

out:
	lq_sta->search_better_tbl = 1;
	tbl->action++;
	if (tbl->action > IL_SISO_SWITCH_GI)
		tbl->action = IL_SISO_SWITCH_ANTENNA1;
	if (update_search_tbl_counter)
		search_tbl->action = tbl->action;

	return 0;
}

/*
 * Try to switch to new modulation mode from MIMO2
 */
static int
il4965_rs_move_mimo2_to_other(struct il_priv *il, struct il_lq_sta *lq_sta,
			      struct ieee80211_conf *conf,
			      struct ieee80211_sta *sta, int idx)
{
	s8 is_green = lq_sta->is_green;
	struct il_scale_tbl_info *tbl = &(lq_sta->lq_info[lq_sta->active_tbl]);
	struct il_scale_tbl_info *search_tbl =
	    &(lq_sta->lq_info[(1 - lq_sta->active_tbl)]);
	struct il_rate_scale_data *win = &(tbl->win[idx]);
	struct ieee80211_sta_ht_cap *ht_cap = &sta->ht_cap;
	u32 sz =
	    (sizeof(struct il_scale_tbl_info) -
	     (sizeof(struct il_rate_scale_data) * RATE_COUNT));
	u8 start_action;
	u8 valid_tx_ant = il->hw_params.valid_tx_ant;
	u8 tx_chains_num = il->hw_params.tx_chains_num;
	u8 update_search_tbl_counter = 0;
	int ret;

	start_action = tbl->action;
	for (;;) {
		lq_sta->action_counter++;
		switch (tbl->action) {
		case IL_MIMO2_SWITCH_ANTENNA1:
		case IL_MIMO2_SWITCH_ANTENNA2:
			D_RATE("LQ: MIMO2 toggle Antennas\n");

			if (tx_chains_num <= 2)
				break;

			if (win->success_ratio >= IL_RS_GOOD_RATIO)
				break;

			memcpy(search_tbl, tbl, sz);
			if (il4965_rs_toggle_antenna
			    (valid_tx_ant, &search_tbl->current_rate,
			     search_tbl)) {
				update_search_tbl_counter = 1;
				goto out;
			}
			break;
		case IL_MIMO2_SWITCH_SISO_A:
		case IL_MIMO2_SWITCH_SISO_B:
		case IL_MIMO2_SWITCH_SISO_C:
			D_RATE("LQ: MIMO2 switch to SISO\n");

			/* Set up new search table for SISO */
			memcpy(search_tbl, tbl, sz);

			if (tbl->action == IL_MIMO2_SWITCH_SISO_A)
				search_tbl->ant_type = ANT_A;
			else if (tbl->action == IL_MIMO2_SWITCH_SISO_B)
				search_tbl->ant_type = ANT_B;
			else
				search_tbl->ant_type = ANT_C;

			if (!il4965_rs_is_valid_ant
			    (valid_tx_ant, search_tbl->ant_type))
				break;

			ret =
			    il4965_rs_switch_to_siso(il, lq_sta, conf, sta,
						     search_tbl, idx);
			if (!ret)
				goto out;

			break;

		case IL_MIMO2_SWITCH_GI:
			if (!tbl->is_ht40 &&
			    !(ht_cap->cap & IEEE80211_HT_CAP_SGI_20))
				break;
			if (tbl->is_ht40 &&
			    !(ht_cap->cap & IEEE80211_HT_CAP_SGI_40))
				break;

			D_RATE("LQ: MIMO2 toggle SGI/NGI\n");

			/* Set up new search table for MIMO2 */
			memcpy(search_tbl, tbl, sz);
			search_tbl->is_SGI = !tbl->is_SGI;
			il4965_rs_set_expected_tpt_table(lq_sta, search_tbl);
			/*
			 * If active table already uses the fastest possible
			 * modulation (dual stream with short guard interval),
			 * and it's working well, there's no need to look
			 * for a better type of modulation!
			 */
			if (tbl->is_SGI) {
				s32 tpt = lq_sta->last_tpt / 100;
				if (tpt >= search_tbl->expected_tpt[idx])
					break;
			}
			search_tbl->current_rate =
			    il4965_rate_n_flags_from_tbl(il, search_tbl, idx,
							 is_green);
			update_search_tbl_counter = 1;
			goto out;

		}
		tbl->action++;
		if (tbl->action > IL_MIMO2_SWITCH_GI)
			tbl->action = IL_MIMO2_SWITCH_ANTENNA1;

		if (tbl->action == start_action)
			break;
	}
	search_tbl->lq_type = LQ_NONE;
	return 0;
out:
	lq_sta->search_better_tbl = 1;
	tbl->action++;
	if (tbl->action > IL_MIMO2_SWITCH_GI)
		tbl->action = IL_MIMO2_SWITCH_ANTENNA1;
	if (update_search_tbl_counter)
		search_tbl->action = tbl->action;

	return 0;

}

/*
 * Check whether we should continue using same modulation mode, or
 * begin search for a new mode, based on:
 * 1) # tx successes or failures while using this mode
 * 2) # times calling this function
 * 3) elapsed time in this mode (not used, for now)
 */
static void
il4965_rs_stay_in_table(struct il_lq_sta *lq_sta, bool force_search)
{
	struct il_scale_tbl_info *tbl;
	int i;
	int active_tbl;
	int flush_interval_passed = 0;
	struct il_priv *il;

	il = lq_sta->drv;
	active_tbl = lq_sta->active_tbl;

	tbl = &(lq_sta->lq_info[active_tbl]);

	/* If we've been disallowing search, see if we should now allow it */
	if (lq_sta->stay_in_tbl) {

		/* Elapsed time using current modulation mode */
		if (lq_sta->flush_timer)
			flush_interval_passed =
			    time_after(jiffies,
				       (unsigned long)(lq_sta->flush_timer +
						       RATE_SCALE_FLUSH_INTVL));

		/*
		 * Check if we should allow search for new modulation mode.
		 * If many frames have failed or succeeded, or we've used
		 * this same modulation for a long time, allow search, and
		 * reset history stats that keep track of whether we should
		 * allow a new search.  Also (below) reset all bitmaps and
		 * stats in active history.
		 */
		if (force_search ||
		    lq_sta->total_failed > lq_sta->max_failure_limit ||
		    lq_sta->total_success > lq_sta->max_success_limit ||
		    (!lq_sta->search_better_tbl && lq_sta->flush_timer &&
		     flush_interval_passed)) {
			D_RATE("LQ: stay is expired %d %d %d\n:",
			       lq_sta->total_failed, lq_sta->total_success,
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
			if (lq_sta->table_count >= lq_sta->table_count_limit) {
				lq_sta->table_count = 0;

				D_RATE("LQ: stay in table clear win\n");
				for (i = 0; i < RATE_COUNT; i++)
					il4965_rs_rate_scale_clear_win(&
								       (tbl->
									win
									[i]));
			}
		}

		/* If transitioning to allow "search", reset all history
		 * bitmaps and stats in active table (this will become the new
		 * "search" table). */
		if (!lq_sta->stay_in_tbl) {
			for (i = 0; i < RATE_COUNT; i++)
				il4965_rs_rate_scale_clear_win(&(tbl->win[i]));
		}
	}
}

/*
 * setup rate table in uCode
 */
static void
il4965_rs_update_rate_tbl(struct il_priv *il, struct il_lq_sta *lq_sta,
			  struct il_scale_tbl_info *tbl, int idx, u8 is_green)
{
	u32 rate;

	/* Update uCode's rate table. */
	rate = il4965_rate_n_flags_from_tbl(il, tbl, idx, is_green);
	il4965_rs_fill_link_cmd(il, lq_sta, rate);
	il_send_lq_cmd(il, &lq_sta->lq, CMD_ASYNC, false);
}

/*
 * Do rate scaling and search for new modulation mode.
 */
static void
il4965_rs_rate_scale_perform(struct il_priv *il, struct sk_buff *skb,
			     struct ieee80211_sta *sta,
			     struct il_lq_sta *lq_sta)
{
	struct ieee80211_hw *hw = il->hw;
	struct ieee80211_conf *conf = &hw->conf;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	int low = RATE_INVALID;
	int high = RATE_INVALID;
	int idx;
	int i;
	struct il_rate_scale_data *win = NULL;
	int current_tpt = IL_INVALID_VALUE;
	int low_tpt = IL_INVALID_VALUE;
	int high_tpt = IL_INVALID_VALUE;
	u32 fail_count;
	s8 scale_action = 0;
	u16 rate_mask;
	u8 update_lq = 0;
	struct il_scale_tbl_info *tbl, *tbl1;
	u16 rate_scale_idx_msk = 0;
	u8 is_green = 0;
	u8 active_tbl = 0;
	u8 done_search = 0;
	u16 high_low;
	s32 sr;
	u8 tid = MAX_TID_COUNT;
	struct il_tid_data *tid_data;

	D_RATE("rate scale calculate new rate for skb\n");

	/* Send management frames and NO_ACK data using lowest rate. */
	/* TODO: this could probably be improved.. */
	if (!ieee80211_is_data(hdr->frame_control) ||
	    (info->flags & IEEE80211_TX_CTL_NO_ACK))
		return;

	lq_sta->supp_rates = sta->supp_rates[lq_sta->band];

	tid = il4965_rs_tl_add_packet(lq_sta, hdr);
	if (tid != MAX_TID_COUNT && (lq_sta->tx_agg_tid_en & (1 << tid))) {
		tid_data = &il->stations[lq_sta->lq.sta_id].tid[tid];
		if (tid_data->agg.state == IL_AGG_OFF)
			lq_sta->is_agg = 0;
		else
			lq_sta->is_agg = 1;
	} else
		lq_sta->is_agg = 0;

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
	if (is_legacy(tbl->lq_type))
		lq_sta->is_green = 0;
	else
		lq_sta->is_green = il4965_rs_use_green(il, sta);
	is_green = lq_sta->is_green;

	/* current tx rate */
	idx = lq_sta->last_txrate_idx;

	D_RATE("Rate scale idx %d for type %d\n", idx, tbl->lq_type);

	/* rates available for this association, and for modulation mode */
	rate_mask = il4965_rs_get_supported_rates(lq_sta, hdr, tbl->lq_type);

	D_RATE("mask 0x%04X\n", rate_mask);

	/* mask with station rate restriction */
	if (is_legacy(tbl->lq_type)) {
		if (lq_sta->band == IEEE80211_BAND_5GHZ)
			/* supp_rates has no CCK bits in A mode */
			rate_scale_idx_msk =
			    (u16) (rate_mask &
				   (lq_sta->supp_rates << IL_FIRST_OFDM_RATE));
		else
			rate_scale_idx_msk =
			    (u16) (rate_mask & lq_sta->supp_rates);

	} else
		rate_scale_idx_msk = rate_mask;

	if (!rate_scale_idx_msk)
		rate_scale_idx_msk = rate_mask;

	if (!((1 << idx) & rate_scale_idx_msk)) {
		IL_ERR("Current Rate is not valid\n");
		if (lq_sta->search_better_tbl) {
			/* revert to active table if search table is not valid */
			tbl->lq_type = LQ_NONE;
			lq_sta->search_better_tbl = 0;
			tbl = &(lq_sta->lq_info[lq_sta->active_tbl]);
			/* get "active" rate info */
			idx = il4965_hwrate_to_plcp_idx(tbl->current_rate);
			il4965_rs_update_rate_tbl(il, lq_sta, tbl, idx,
						      is_green);
		}
		return;
	}

	/* Get expected throughput table and history win for current rate */
	if (!tbl->expected_tpt) {
		IL_ERR("tbl->expected_tpt is NULL\n");
		return;
	}

	/* force user max rate if set by user */
	if (lq_sta->max_rate_idx != -1 && lq_sta->max_rate_idx < idx) {
		idx = lq_sta->max_rate_idx;
		update_lq = 1;
		win = &(tbl->win[idx]);
		goto lq_update;
	}

	win = &(tbl->win[idx]);

	/*
	 * If there is not enough history to calculate actual average
	 * throughput, keep analyzing results of more tx frames, without
	 * changing rate or mode (bypass most of the rest of this function).
	 * Set up new rate table in uCode only if old rate is not supported
	 * in current association (use new rate found above).
	 */
	fail_count = win->counter - win->success_counter;
	if (fail_count < RATE_MIN_FAILURE_TH &&
	    win->success_counter < RATE_MIN_SUCCESS_TH) {
		D_RATE("LQ: still below TH. succ=%d total=%d " "for idx %d\n",
		       win->success_counter, win->counter, idx);

		/* Can't calculate this yet; not enough history */
		win->average_tpt = IL_INVALID_VALUE;

		/* Should we stay with this modulation mode,
		 * or search for a new one? */
		il4965_rs_stay_in_table(lq_sta, false);

		goto out;
	}
	/* Else we have enough samples; calculate estimate of
	 * actual average throughput */
	if (win->average_tpt !=
	    ((win->success_ratio * tbl->expected_tpt[idx] + 64) / 128)) {
		IL_ERR("expected_tpt should have been calculated by now\n");
		win->average_tpt =
		    ((win->success_ratio * tbl->expected_tpt[idx] + 64) / 128);
	}

	/* If we are searching for better modulation mode, check success. */
	if (lq_sta->search_better_tbl) {
		/* If good success, continue using the "search" mode;
		 * no need to send new link quality command, since we're
		 * continuing to use the setup that we've been trying. */
		if (win->average_tpt > lq_sta->last_tpt) {

			D_RATE("LQ: SWITCHING TO NEW TBL "
			       "suc=%d cur-tpt=%d old-tpt=%d\n",
			       win->success_ratio, win->average_tpt,
			       lq_sta->last_tpt);

			if (!is_legacy(tbl->lq_type))
				lq_sta->enable_counter = 1;

			/* Swap tables; "search" becomes "active" */
			lq_sta->active_tbl = active_tbl;
			current_tpt = win->average_tpt;

			/* Else poor success; go back to mode in "active" table */
		} else {

			D_RATE("LQ: GOING BACK TO THE OLD TBL "
			       "suc=%d cur-tpt=%d old-tpt=%d\n",
			       win->success_ratio, win->average_tpt,
			       lq_sta->last_tpt);

			/* Nullify "search" table */
			tbl->lq_type = LQ_NONE;

			/* Revert to "active" table */
			active_tbl = lq_sta->active_tbl;
			tbl = &(lq_sta->lq_info[active_tbl]);

			/* Revert to "active" rate and throughput info */
			idx = il4965_hwrate_to_plcp_idx(tbl->current_rate);
			current_tpt = lq_sta->last_tpt;

			/* Need to set up a new rate table in uCode */
			update_lq = 1;
		}

		/* Either way, we've made a decision; modulation mode
		 * search is done, allow rate adjustment next time. */
		lq_sta->search_better_tbl = 0;
		done_search = 1;	/* Don't switch modes below! */
		goto lq_update;
	}

	/* (Else) not in search of better modulation mode, try for better
	 * starting rate, while staying in this mode. */
	high_low =
	    il4965_rs_get_adjacent_rate(il, idx, rate_scale_idx_msk,
					tbl->lq_type);
	low = high_low & 0xff;
	high = (high_low >> 8) & 0xff;

	/* If user set max rate, dont allow higher than user constrain */
	if (lq_sta->max_rate_idx != -1 && lq_sta->max_rate_idx < high)
		high = RATE_INVALID;

	sr = win->success_ratio;

	/* Collect measured throughputs for current and adjacent rates */
	current_tpt = win->average_tpt;
	if (low != RATE_INVALID)
		low_tpt = tbl->win[low].average_tpt;
	if (high != RATE_INVALID)
		high_tpt = tbl->win[high].average_tpt;

	scale_action = 0;

	/* Too many failures, decrease rate */
	if (sr <= RATE_DECREASE_TH || current_tpt == 0) {
		D_RATE("decrease rate because of low success_ratio\n");
		scale_action = -1;

		/* No throughput measured yet for adjacent rates; try increase. */
	} else if (low_tpt == IL_INVALID_VALUE && high_tpt == IL_INVALID_VALUE) {

		if (high != RATE_INVALID && sr >= RATE_INCREASE_TH)
			scale_action = 1;
		else if (low != RATE_INVALID)
			scale_action = 0;
	}

	/* Both adjacent throughputs are measured, but neither one has better
	 * throughput; we're using the best rate, don't change it! */
	else if (low_tpt != IL_INVALID_VALUE && high_tpt != IL_INVALID_VALUE &&
		 low_tpt < current_tpt && high_tpt < current_tpt)
		scale_action = 0;

	/* At least one adjacent rate's throughput is measured,
	 * and may have better performance. */
	else {
		/* Higher adjacent rate's throughput is measured */
		if (high_tpt != IL_INVALID_VALUE) {
			/* Higher rate has better throughput */
			if (high_tpt > current_tpt && sr >= RATE_INCREASE_TH)
				scale_action = 1;
			else
				scale_action = 0;

			/* Lower adjacent rate's throughput is measured */
		} else if (low_tpt != IL_INVALID_VALUE) {
			/* Lower rate has better throughput */
			if (low_tpt > current_tpt) {
				D_RATE("decrease rate because of low tpt\n");
				scale_action = -1;
			} else if (sr >= RATE_INCREASE_TH) {
				scale_action = 1;
			}
		}
	}

	/* Sanity check; asked for decrease, but success rate or throughput
	 * has been good at old rate.  Don't change it. */
	if (scale_action == -1 && low != RATE_INVALID &&
	    (sr > RATE_HIGH_TH || current_tpt > 100 * tbl->expected_tpt[low]))
		scale_action = 0;

	switch (scale_action) {
	case -1:
		/* Decrease starting rate, update uCode's rate table */
		if (low != RATE_INVALID) {
			update_lq = 1;
			idx = low;
		}

		break;
	case 1:
		/* Increase starting rate, update uCode's rate table */
		if (high != RATE_INVALID) {
			update_lq = 1;
			idx = high;
		}

		break;
	case 0:
		/* No change */
	default:
		break;
	}

	D_RATE("choose rate scale idx %d action %d low %d " "high %d type %d\n",
	       idx, scale_action, low, high, tbl->lq_type);

lq_update:
	/* Replace uCode's rate table for the destination station. */
	if (update_lq)
		il4965_rs_update_rate_tbl(il, lq_sta, tbl, idx, is_green);

	/* Should we stay with this modulation mode,
	 * or search for a new one? */
	il4965_rs_stay_in_table(lq_sta, false);

	/*
	 * Search for new modulation mode if we're:
	 * 1)  Not changing rates right now
	 * 2)  Not just finishing up a search
	 * 3)  Allowing a new search
	 */
	if (!update_lq && !done_search && !lq_sta->stay_in_tbl && win->counter) {
		/* Save current throughput to compare with "search" throughput */
		lq_sta->last_tpt = current_tpt;

		/* Select a new "search" modulation mode to try.
		 * If one is found, set up the new "search" table. */
		if (is_legacy(tbl->lq_type))
			il4965_rs_move_legacy_other(il, lq_sta, conf, sta, idx);
		else if (is_siso(tbl->lq_type))
			il4965_rs_move_siso_to_other(il, lq_sta, conf, sta,
						     idx);
		else		/* (is_mimo2(tbl->lq_type)) */
			il4965_rs_move_mimo2_to_other(il, lq_sta, conf, sta,
						      idx);

		/* If new "search" mode was selected, set up in uCode table */
		if (lq_sta->search_better_tbl) {
			/* Access the "search" table, clear its history. */
			tbl = &(lq_sta->lq_info[(1 - lq_sta->active_tbl)]);
			for (i = 0; i < RATE_COUNT; i++)
				il4965_rs_rate_scale_clear_win(&(tbl->win[i]));

			/* Use new "search" start rate */
			idx = il4965_hwrate_to_plcp_idx(tbl->current_rate);

			D_RATE("Switch current  mcs: %X idx: %d\n",
			       tbl->current_rate, idx);
			il4965_rs_fill_link_cmd(il, lq_sta, tbl->current_rate);
			il_send_lq_cmd(il, &lq_sta->lq, CMD_ASYNC, false);
		} else
			done_search = 1;
	}

	if (done_search && !lq_sta->stay_in_tbl) {
		/* If the "active" (non-search) mode was legacy,
		 * and we've tried switching antennas,
		 * but we haven't been able to try HT modes (not available),
		 * stay with best antenna legacy modulation for a while
		 * before next round of mode comparisons. */
		tbl1 = &(lq_sta->lq_info[lq_sta->active_tbl]);
		if (is_legacy(tbl1->lq_type) && !conf_is_ht(conf) &&
		    lq_sta->action_counter > tbl1->max_search) {
			D_RATE("LQ: STAY in legacy table\n");
			il4965_rs_set_stay_in_table(il, 1, lq_sta);
		}

		/* If we're in an HT mode, and all 3 mode switch actions
		 * have been tried and compared, stay in this best modulation
		 * mode for a while before next round of mode comparisons. */
		if (lq_sta->enable_counter &&
		    lq_sta->action_counter >= tbl1->max_search) {
			if (lq_sta->last_tpt > IL_AGG_TPT_THREHOLD &&
			    (lq_sta->tx_agg_tid_en & (1 << tid)) &&
			    tid != MAX_TID_COUNT) {
				tid_data =
				    &il->stations[lq_sta->lq.sta_id].tid[tid];
				if (tid_data->agg.state == IL_AGG_OFF) {
					D_RATE("try to aggregate tid %d\n",
					       tid);
					il4965_rs_tl_turn_on_agg(il, tid,
								 lq_sta, sta);
				}
			}
			il4965_rs_set_stay_in_table(il, 0, lq_sta);
		}
	}

out:
	tbl->current_rate =
	    il4965_rate_n_flags_from_tbl(il, tbl, idx, is_green);
	i = idx;
	lq_sta->last_txrate_idx = i;
}

/**
 * il4965_rs_initialize_lq - Initialize a station's hardware rate table
 *
 * The uCode's station table contains a table of fallback rates
 * for automatic fallback during transmission.
 *
 * NOTE: This sets up a default set of values.  These will be replaced later
 *       if the driver's iwl-4965-rs rate scaling algorithm is used, instead of
 *       rc80211_simple.
 *
 * NOTE: Run C_ADD_STA command to set up station table entry, before
 *       calling this function (which runs C_TX_LINK_QUALITY_CMD,
 *       which requires station table entry to exist).
 */
static void
il4965_rs_initialize_lq(struct il_priv *il, struct ieee80211_conf *conf,
			struct ieee80211_sta *sta, struct il_lq_sta *lq_sta)
{
	struct il_scale_tbl_info *tbl;
	int rate_idx;
	int i;
	u32 rate;
	u8 use_green;
	u8 active_tbl = 0;
	u8 valid_tx_ant;
	struct il_station_priv *sta_priv;

	if (!sta || !lq_sta)
		return;

	use_green = il4965_rs_use_green(il, sta);
	sta_priv = (void *)sta->drv_priv;

	i = lq_sta->last_txrate_idx;

	valid_tx_ant = il->hw_params.valid_tx_ant;

	if (!lq_sta->search_better_tbl)
		active_tbl = lq_sta->active_tbl;
	else
		active_tbl = 1 - lq_sta->active_tbl;

	tbl = &(lq_sta->lq_info[active_tbl]);

	if (i < 0 || i >= RATE_COUNT)
		i = 0;

	rate = il_rates[i].plcp;
	tbl->ant_type = il4965_first_antenna(valid_tx_ant);
	rate |= tbl->ant_type << RATE_MCS_ANT_POS;

	if (i >= IL_FIRST_CCK_RATE && i <= IL_LAST_CCK_RATE)
		rate |= RATE_MCS_CCK_MSK;

	il4965_rs_get_tbl_info_from_mcs(rate, il->band, tbl, &rate_idx);
	if (!il4965_rs_is_valid_ant(valid_tx_ant, tbl->ant_type))
		il4965_rs_toggle_antenna(valid_tx_ant, &rate, tbl);

	rate = il4965_rate_n_flags_from_tbl(il, tbl, rate_idx, use_green);
	tbl->current_rate = rate;
	il4965_rs_set_expected_tpt_table(lq_sta, tbl);
	il4965_rs_fill_link_cmd(NULL, lq_sta, rate);
	il->stations[lq_sta->lq.sta_id].lq = &lq_sta->lq;
	il_send_lq_cmd(il, &lq_sta->lq, CMD_SYNC, true);
}

static void
il4965_rs_get_rate(void *il_r, struct ieee80211_sta *sta, void *il_sta,
		   struct ieee80211_tx_rate_control *txrc)
{

	struct sk_buff *skb = txrc->skb;
	struct ieee80211_supported_band *sband = txrc->sband;
	struct il_priv *il __maybe_unused = (struct il_priv *)il_r;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct il_lq_sta *lq_sta = il_sta;
	int rate_idx;

	D_RATE("rate scale calculate new rate for skb\n");

	/* Get max rate if user set max rate */
	if (lq_sta) {
		lq_sta->max_rate_idx = txrc->max_rate_idx;
		if (sband->band == IEEE80211_BAND_5GHZ &&
		    lq_sta->max_rate_idx != -1)
			lq_sta->max_rate_idx += IL_FIRST_OFDM_RATE;
		if (lq_sta->max_rate_idx < 0 ||
		    lq_sta->max_rate_idx >= RATE_COUNT)
			lq_sta->max_rate_idx = -1;
	}

	/* Treat uninitialized rate scaling data same as non-existing. */
	if (lq_sta && !lq_sta->drv) {
		D_RATE("Rate scaling not initialized yet.\n");
		il_sta = NULL;
	}

	/* Send management frames and NO_ACK data using lowest rate. */
	if (rate_control_send_low(sta, il_sta, txrc))
		return;

	if (!lq_sta)
		return;

	rate_idx = lq_sta->last_txrate_idx;

	if (lq_sta->last_rate_n_flags & RATE_MCS_HT_MSK) {
		rate_idx -= IL_FIRST_OFDM_RATE;
		/* 6M and 9M shared same MCS idx */
		rate_idx = (rate_idx > 0) ? (rate_idx - 1) : 0;
		if (il4965_rs_extract_rate(lq_sta->last_rate_n_flags) >=
		    RATE_MIMO2_6M_PLCP)
			rate_idx = rate_idx + MCS_IDX_PER_STREAM;
		info->control.rates[0].flags = IEEE80211_TX_RC_MCS;
		if (lq_sta->last_rate_n_flags & RATE_MCS_SGI_MSK)
			info->control.rates[0].flags |=
			    IEEE80211_TX_RC_SHORT_GI;
		if (lq_sta->last_rate_n_flags & RATE_MCS_DUP_MSK)
			info->control.rates[0].flags |=
			    IEEE80211_TX_RC_DUP_DATA;
		if (lq_sta->last_rate_n_flags & RATE_MCS_HT40_MSK)
			info->control.rates[0].flags |=
			    IEEE80211_TX_RC_40_MHZ_WIDTH;
		if (lq_sta->last_rate_n_flags & RATE_MCS_GF_MSK)
			info->control.rates[0].flags |=
			    IEEE80211_TX_RC_GREEN_FIELD;
	} else {
		/* Check for invalid rates */
		if (rate_idx < 0 || rate_idx >= RATE_COUNT_LEGACY ||
		    (sband->band == IEEE80211_BAND_5GHZ &&
		     rate_idx < IL_FIRST_OFDM_RATE))
			rate_idx = rate_lowest_index(sband, sta);
		/* On valid 5 GHz rate, adjust idx */
		else if (sband->band == IEEE80211_BAND_5GHZ)
			rate_idx -= IL_FIRST_OFDM_RATE;
		info->control.rates[0].flags = 0;
	}
	info->control.rates[0].idx = rate_idx;
	info->control.rates[0].count = 1;
}

static void *
il4965_rs_alloc_sta(void *il_rate, struct ieee80211_sta *sta, gfp_t gfp)
{
	struct il_station_priv *sta_priv =
	    (struct il_station_priv *)sta->drv_priv;
	struct il_priv *il;

	il = (struct il_priv *)il_rate;
	D_RATE("create station rate scale win\n");

	return &sta_priv->lq_sta;
}

/*
 * Called after adding a new station to initialize rate scaling
 */
void
il4965_rs_rate_init(struct il_priv *il, struct ieee80211_sta *sta, u8 sta_id)
{
	int i, j;
	struct ieee80211_hw *hw = il->hw;
	struct ieee80211_conf *conf = &il->hw->conf;
	struct ieee80211_sta_ht_cap *ht_cap = &sta->ht_cap;
	struct il_station_priv *sta_priv;
	struct il_lq_sta *lq_sta;
	struct ieee80211_supported_band *sband;

	sta_priv = (struct il_station_priv *)sta->drv_priv;
	lq_sta = &sta_priv->lq_sta;
	sband = hw->wiphy->bands[conf->chandef.chan->band];

	lq_sta->lq.sta_id = sta_id;

	for (j = 0; j < LQ_SIZE; j++)
		for (i = 0; i < RATE_COUNT; i++)
			il4965_rs_rate_scale_clear_win(&lq_sta->lq_info[j].
						       win[i]);

	lq_sta->flush_timer = 0;
	lq_sta->supp_rates = sta->supp_rates[sband->band];
	for (j = 0; j < LQ_SIZE; j++)
		for (i = 0; i < RATE_COUNT; i++)
			il4965_rs_rate_scale_clear_win(&lq_sta->lq_info[j].
						       win[i]);

	D_RATE("LQ:" "*** rate scale station global init for station %d ***\n",
	       sta_id);
	/* TODO: what is a good starting rate for STA? About middle? Maybe not
	 * the lowest or the highest rate.. Could consider using RSSI from
	 * previous packets? Need to have IEEE 802.1X auth succeed immediately
	 * after assoc.. */

	lq_sta->is_dup = 0;
	lq_sta->max_rate_idx = -1;
	lq_sta->missed_rate_counter = IL_MISSED_RATE_MAX;
	lq_sta->is_green = il4965_rs_use_green(il, sta);
	lq_sta->active_legacy_rate = il->active_rate & ~(0x1000);
	lq_sta->band = il->band;
	/*
	 * active_siso_rate mask includes 9 MBits (bit 5), and CCK (bits 0-3),
	 * supp_rates[] does not; shift to convert format, force 9 MBits off.
	 */
	lq_sta->active_siso_rate = ht_cap->mcs.rx_mask[0] << 1;
	lq_sta->active_siso_rate |= ht_cap->mcs.rx_mask[0] & 0x1;
	lq_sta->active_siso_rate &= ~((u16) 0x2);
	lq_sta->active_siso_rate <<= IL_FIRST_OFDM_RATE;

	/* Same here */
	lq_sta->active_mimo2_rate = ht_cap->mcs.rx_mask[1] << 1;
	lq_sta->active_mimo2_rate |= ht_cap->mcs.rx_mask[1] & 0x1;
	lq_sta->active_mimo2_rate &= ~((u16) 0x2);
	lq_sta->active_mimo2_rate <<= IL_FIRST_OFDM_RATE;

	/* These values will be overridden later */
	lq_sta->lq.general_params.single_stream_ant_msk =
	    il4965_first_antenna(il->hw_params.valid_tx_ant);
	lq_sta->lq.general_params.dual_stream_ant_msk =
	    il->hw_params.valid_tx_ant & ~il4965_first_antenna(il->hw_params.
							       valid_tx_ant);
	if (!lq_sta->lq.general_params.dual_stream_ant_msk) {
		lq_sta->lq.general_params.dual_stream_ant_msk = ANT_AB;
	} else if (il4965_num_of_ant(il->hw_params.valid_tx_ant) == 2) {
		lq_sta->lq.general_params.dual_stream_ant_msk =
		    il->hw_params.valid_tx_ant;
	}

	/* as default allow aggregation for all tids */
	lq_sta->tx_agg_tid_en = IL_AGG_ALL_TID;
	lq_sta->drv = il;

	/* Set last_txrate_idx to lowest rate */
	lq_sta->last_txrate_idx = rate_lowest_index(sband, sta);
	if (sband->band == IEEE80211_BAND_5GHZ)
		lq_sta->last_txrate_idx += IL_FIRST_OFDM_RATE;
	lq_sta->is_agg = 0;

#ifdef CONFIG_MAC80211_DEBUGFS
	lq_sta->dbg_fixed_rate = 0;
#endif

	il4965_rs_initialize_lq(il, conf, sta, lq_sta);
}

static void
il4965_rs_fill_link_cmd(struct il_priv *il, struct il_lq_sta *lq_sta,
			u32 new_rate)
{
	struct il_scale_tbl_info tbl_type;
	int idx = 0;
	int rate_idx;
	int repeat_rate = 0;
	u8 ant_toggle_cnt = 0;
	u8 use_ht_possible = 1;
	u8 valid_tx_ant = 0;
	struct il_link_quality_cmd *lq_cmd = &lq_sta->lq;

	/* Override starting rate (idx 0) if needed for debug purposes */
	il4965_rs_dbgfs_set_mcs(lq_sta, &new_rate, idx);

	/* Interpret new_rate (rate_n_flags) */
	il4965_rs_get_tbl_info_from_mcs(new_rate, lq_sta->band, &tbl_type,
					&rate_idx);

	/* How many times should we repeat the initial rate? */
	if (is_legacy(tbl_type.lq_type)) {
		ant_toggle_cnt = 1;
		repeat_rate = IL_NUMBER_TRY;
	} else {
		repeat_rate = IL_HT_NUMBER_TRY;
	}

	lq_cmd->general_params.mimo_delimiter =
	    is_mimo(tbl_type.lq_type) ? 1 : 0;

	/* Fill 1st table entry (idx 0) */
	lq_cmd->rs_table[idx].rate_n_flags = cpu_to_le32(new_rate);

	if (il4965_num_of_ant(tbl_type.ant_type) == 1) {
		lq_cmd->general_params.single_stream_ant_msk =
		    tbl_type.ant_type;
	} else if (il4965_num_of_ant(tbl_type.ant_type) == 2) {
		lq_cmd->general_params.dual_stream_ant_msk = tbl_type.ant_type;
	}
	/* otherwise we don't modify the existing value */
	idx++;
	repeat_rate--;
	if (il)
		valid_tx_ant = il->hw_params.valid_tx_ant;

	/* Fill rest of rate table */
	while (idx < LINK_QUAL_MAX_RETRY_NUM) {
		/* Repeat initial/next rate.
		 * For legacy IL_NUMBER_TRY == 1, this loop will not execute.
		 * For HT IL_HT_NUMBER_TRY == 3, this executes twice. */
		while (repeat_rate > 0 && idx < LINK_QUAL_MAX_RETRY_NUM) {
			if (is_legacy(tbl_type.lq_type)) {
				if (ant_toggle_cnt < NUM_TRY_BEFORE_ANT_TOGGLE)
					ant_toggle_cnt++;
				else if (il &&
					 il4965_rs_toggle_antenna(valid_tx_ant,
								  &new_rate,
								  &tbl_type))
					ant_toggle_cnt = 1;
			}

			/* Override next rate if needed for debug purposes */
			il4965_rs_dbgfs_set_mcs(lq_sta, &new_rate, idx);

			/* Fill next table entry */
			lq_cmd->rs_table[idx].rate_n_flags =
			    cpu_to_le32(new_rate);
			repeat_rate--;
			idx++;
		}

		il4965_rs_get_tbl_info_from_mcs(new_rate, lq_sta->band,
						&tbl_type, &rate_idx);

		/* Indicate to uCode which entries might be MIMO.
		 * If initial rate was MIMO, this will finally end up
		 * as (IL_HT_NUMBER_TRY * 2), after 2nd pass, otherwise 0. */
		if (is_mimo(tbl_type.lq_type))
			lq_cmd->general_params.mimo_delimiter = idx;

		/* Get next rate */
		new_rate =
		    il4965_rs_get_lower_rate(lq_sta, &tbl_type, rate_idx,
					     use_ht_possible);

		/* How many times should we repeat the next rate? */
		if (is_legacy(tbl_type.lq_type)) {
			if (ant_toggle_cnt < NUM_TRY_BEFORE_ANT_TOGGLE)
				ant_toggle_cnt++;
			else if (il &&
				 il4965_rs_toggle_antenna(valid_tx_ant,
							  &new_rate, &tbl_type))
				ant_toggle_cnt = 1;

			repeat_rate = IL_NUMBER_TRY;
		} else {
			repeat_rate = IL_HT_NUMBER_TRY;
		}

		/* Don't allow HT rates after next pass.
		 * il4965_rs_get_lower_rate() will change type to LQ_A or LQ_G. */
		use_ht_possible = 0;

		/* Override next rate if needed for debug purposes */
		il4965_rs_dbgfs_set_mcs(lq_sta, &new_rate, idx);

		/* Fill next table entry */
		lq_cmd->rs_table[idx].rate_n_flags = cpu_to_le32(new_rate);

		idx++;
		repeat_rate--;
	}

	lq_cmd->agg_params.agg_frame_cnt_limit = LINK_QUAL_AGG_FRAME_LIMIT_DEF;
	lq_cmd->agg_params.agg_dis_start_th = LINK_QUAL_AGG_DISABLE_START_DEF;

	lq_cmd->agg_params.agg_time_limit =
	    cpu_to_le16(LINK_QUAL_AGG_TIME_LIMIT_DEF);
}

static void *
il4965_rs_alloc(struct ieee80211_hw *hw, struct dentry *debugfsdir)
{
	return hw->priv;
}

/* rate scale requires free function to be implemented */
static void
il4965_rs_free(void *il_rate)
{
	return;
}

static void
il4965_rs_free_sta(void *il_r, struct ieee80211_sta *sta, void *il_sta)
{
	struct il_priv *il __maybe_unused = il_r;

	D_RATE("enter\n");
	D_RATE("leave\n");
}

#ifdef CONFIG_MAC80211_DEBUGFS

static void
il4965_rs_dbgfs_set_mcs(struct il_lq_sta *lq_sta, u32 * rate_n_flags, int idx)
{
	struct il_priv *il;
	u8 valid_tx_ant;
	u8 ant_sel_tx;

	il = lq_sta->drv;
	valid_tx_ant = il->hw_params.valid_tx_ant;
	if (lq_sta->dbg_fixed_rate) {
		ant_sel_tx =
		    ((lq_sta->
		      dbg_fixed_rate & RATE_MCS_ANT_ABC_MSK) >>
		     RATE_MCS_ANT_POS);
		if ((valid_tx_ant & ant_sel_tx) == ant_sel_tx) {
			*rate_n_flags = lq_sta->dbg_fixed_rate;
			D_RATE("Fixed rate ON\n");
		} else {
			lq_sta->dbg_fixed_rate = 0;
			IL_ERR
			    ("Invalid antenna selection 0x%X, Valid is 0x%X\n",
			     ant_sel_tx, valid_tx_ant);
			D_RATE("Fixed rate OFF\n");
		}
	} else {
		D_RATE("Fixed rate OFF\n");
	}
}

static ssize_t
il4965_rs_sta_dbgfs_scale_table_write(struct file *file,
				      const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct il_lq_sta *lq_sta = file->private_data;
	struct il_priv *il;
	char buf[64];
	size_t buf_size;
	u32 parsed_rate;

	il = lq_sta->drv;
	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (sscanf(buf, "%x", &parsed_rate) == 1)
		lq_sta->dbg_fixed_rate = parsed_rate;
	else
		lq_sta->dbg_fixed_rate = 0;

	lq_sta->active_legacy_rate = 0x0FFF;	/* 1 - 54 MBits, includes CCK */
	lq_sta->active_siso_rate = 0x1FD0;	/* 6 - 60 MBits, no 9, no CCK */
	lq_sta->active_mimo2_rate = 0x1FD0;	/* 6 - 60 MBits, no 9, no CCK */

	D_RATE("sta_id %d rate 0x%X\n", lq_sta->lq.sta_id,
	       lq_sta->dbg_fixed_rate);

	if (lq_sta->dbg_fixed_rate) {
		il4965_rs_fill_link_cmd(NULL, lq_sta, lq_sta->dbg_fixed_rate);
		il_send_lq_cmd(lq_sta->drv, &lq_sta->lq, CMD_ASYNC, false);
	}

	return count;
}

static ssize_t
il4965_rs_sta_dbgfs_scale_table_read(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	char *buff;
	int desc = 0;
	int i = 0;
	int idx = 0;
	ssize_t ret;

	struct il_lq_sta *lq_sta = file->private_data;
	struct il_priv *il;
	struct il_scale_tbl_info *tbl = &(lq_sta->lq_info[lq_sta->active_tbl]);

	il = lq_sta->drv;
	buff = kmalloc(1024, GFP_KERNEL);
	if (!buff)
		return -ENOMEM;

	desc += sprintf(buff + desc, "sta_id %d\n", lq_sta->lq.sta_id);
	desc +=
	    sprintf(buff + desc, "failed=%d success=%d rate=0%X\n",
		    lq_sta->total_failed, lq_sta->total_success,
		    lq_sta->active_legacy_rate);
	desc +=
	    sprintf(buff + desc, "fixed rate 0x%X\n", lq_sta->dbg_fixed_rate);
	desc +=
	    sprintf(buff + desc, "valid_tx_ant %s%s%s\n",
		    (il->hw_params.valid_tx_ant & ANT_A) ? "ANT_A," : "",
		    (il->hw_params.valid_tx_ant & ANT_B) ? "ANT_B," : "",
		    (il->hw_params.valid_tx_ant & ANT_C) ? "ANT_C" : "");
	desc +=
	    sprintf(buff + desc, "lq type %s\n",
		    (is_legacy(tbl->lq_type)) ? "legacy" : "HT");
	if (is_Ht(tbl->lq_type)) {
		desc +=
		    sprintf(buff + desc, " %s",
			    (is_siso(tbl->lq_type)) ? "SISO" : "MIMO2");
		desc +=
		    sprintf(buff + desc, " %s",
			    (tbl->is_ht40) ? "40MHz" : "20MHz");
		desc +=
		    sprintf(buff + desc, " %s %s %s\n",
			    (tbl->is_SGI) ? "SGI" : "",
			    (lq_sta->is_green) ? "GF enabled" : "",
			    (lq_sta->is_agg) ? "AGG on" : "");
	}
	desc +=
	    sprintf(buff + desc, "last tx rate=0x%X\n",
		    lq_sta->last_rate_n_flags);
	desc +=
	    sprintf(buff + desc,
		    "general:" "flags=0x%X mimo-d=%d s-ant0x%x d-ant=0x%x\n",
		    lq_sta->lq.general_params.flags,
		    lq_sta->lq.general_params.mimo_delimiter,
		    lq_sta->lq.general_params.single_stream_ant_msk,
		    lq_sta->lq.general_params.dual_stream_ant_msk);

	desc +=
	    sprintf(buff + desc,
		    "agg:"
		    "time_limit=%d dist_start_th=%d frame_cnt_limit=%d\n",
		    le16_to_cpu(lq_sta->lq.agg_params.agg_time_limit),
		    lq_sta->lq.agg_params.agg_dis_start_th,
		    lq_sta->lq.agg_params.agg_frame_cnt_limit);

	desc +=
	    sprintf(buff + desc,
		    "Start idx [0]=0x%x [1]=0x%x [2]=0x%x [3]=0x%x\n",
		    lq_sta->lq.general_params.start_rate_idx[0],
		    lq_sta->lq.general_params.start_rate_idx[1],
		    lq_sta->lq.general_params.start_rate_idx[2],
		    lq_sta->lq.general_params.start_rate_idx[3]);

	for (i = 0; i < LINK_QUAL_MAX_RETRY_NUM; i++) {
		idx =
		    il4965_hwrate_to_plcp_idx(le32_to_cpu
					      (lq_sta->lq.rs_table[i].
					       rate_n_flags));
		if (is_legacy(tbl->lq_type)) {
			desc +=
			    sprintf(buff + desc, " rate[%d] 0x%X %smbps\n", i,
				    le32_to_cpu(lq_sta->lq.rs_table[i].
						rate_n_flags),
				    il_rate_mcs[idx].mbps);
		} else {
			desc +=
			    sprintf(buff + desc, " rate[%d] 0x%X %smbps (%s)\n",
				    i,
				    le32_to_cpu(lq_sta->lq.rs_table[i].
						rate_n_flags),
				    il_rate_mcs[idx].mbps,
				    il_rate_mcs[idx].mcs);
		}
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, buff, desc);
	kfree(buff);
	return ret;
}

static const struct file_operations rs_sta_dbgfs_scale_table_ops = {
	.write = il4965_rs_sta_dbgfs_scale_table_write,
	.read = il4965_rs_sta_dbgfs_scale_table_read,
	.open = simple_open,
	.llseek = default_llseek,
};

static ssize_t
il4965_rs_sta_dbgfs_stats_table_read(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	char *buff;
	int desc = 0;
	int i, j;
	ssize_t ret;

	struct il_lq_sta *lq_sta = file->private_data;

	buff = kmalloc(1024, GFP_KERNEL);
	if (!buff)
		return -ENOMEM;

	for (i = 0; i < LQ_SIZE; i++) {
		desc +=
		    sprintf(buff + desc,
			    "%s type=%d SGI=%d HT40=%d DUP=%d GF=%d\n"
			    "rate=0x%X\n", lq_sta->active_tbl == i ? "*" : "x",
			    lq_sta->lq_info[i].lq_type,
			    lq_sta->lq_info[i].is_SGI,
			    lq_sta->lq_info[i].is_ht40,
			    lq_sta->lq_info[i].is_dup, lq_sta->is_green,
			    lq_sta->lq_info[i].current_rate);
		for (j = 0; j < RATE_COUNT; j++) {
			desc +=
			    sprintf(buff + desc,
				    "counter=%d success=%d %%=%d\n",
				    lq_sta->lq_info[i].win[j].counter,
				    lq_sta->lq_info[i].win[j].success_counter,
				    lq_sta->lq_info[i].win[j].success_ratio);
		}
	}
	ret = simple_read_from_buffer(user_buf, count, ppos, buff, desc);
	kfree(buff);
	return ret;
}

static const struct file_operations rs_sta_dbgfs_stats_table_ops = {
	.read = il4965_rs_sta_dbgfs_stats_table_read,
	.open = simple_open,
	.llseek = default_llseek,
};

static ssize_t
il4965_rs_sta_dbgfs_rate_scale_data_read(struct file *file,
					 char __user *user_buf, size_t count,
					 loff_t *ppos)
{
	char buff[120];
	int desc = 0;
	struct il_lq_sta *lq_sta = file->private_data;
	struct il_scale_tbl_info *tbl = &lq_sta->lq_info[lq_sta->active_tbl];

	if (is_Ht(tbl->lq_type))
		desc +=
		    sprintf(buff + desc, "Bit Rate= %d Mb/s\n",
			    tbl->expected_tpt[lq_sta->last_txrate_idx]);
	else
		desc +=
		    sprintf(buff + desc, "Bit Rate= %d Mb/s\n",
			    il_rates[lq_sta->last_txrate_idx].ieee >> 1);

	return simple_read_from_buffer(user_buf, count, ppos, buff, desc);
}

static const struct file_operations rs_sta_dbgfs_rate_scale_data_ops = {
	.read = il4965_rs_sta_dbgfs_rate_scale_data_read,
	.open = simple_open,
	.llseek = default_llseek,
};

static void
il4965_rs_add_debugfs(void *il, void *il_sta, struct dentry *dir)
{
	struct il_lq_sta *lq_sta = il_sta;
	lq_sta->rs_sta_dbgfs_scale_table_file =
	    debugfs_create_file("rate_scale_table", S_IRUSR | S_IWUSR, dir,
				lq_sta, &rs_sta_dbgfs_scale_table_ops);
	lq_sta->rs_sta_dbgfs_stats_table_file =
	    debugfs_create_file("rate_stats_table", S_IRUSR, dir, lq_sta,
				&rs_sta_dbgfs_stats_table_ops);
	lq_sta->rs_sta_dbgfs_rate_scale_data_file =
	    debugfs_create_file("rate_scale_data", S_IRUSR, dir, lq_sta,
				&rs_sta_dbgfs_rate_scale_data_ops);
	lq_sta->rs_sta_dbgfs_tx_agg_tid_en_file =
	    debugfs_create_u8("tx_agg_tid_enable", S_IRUSR | S_IWUSR, dir,
			      &lq_sta->tx_agg_tid_en);

}

static void
il4965_rs_remove_debugfs(void *il, void *il_sta)
{
	struct il_lq_sta *lq_sta = il_sta;
	debugfs_remove(lq_sta->rs_sta_dbgfs_scale_table_file);
	debugfs_remove(lq_sta->rs_sta_dbgfs_stats_table_file);
	debugfs_remove(lq_sta->rs_sta_dbgfs_rate_scale_data_file);
	debugfs_remove(lq_sta->rs_sta_dbgfs_tx_agg_tid_en_file);
}
#endif

/*
 * Initialization of rate scaling information is done by driver after
 * the station is added. Since mac80211 calls this function before a
 * station is added we ignore it.
 */
static void
il4965_rs_rate_init_stub(void *il_r, struct ieee80211_supported_band *sband,
			 struct ieee80211_sta *sta, void *il_sta)
{
}

static struct rate_control_ops rs_4965_ops = {
	.module = NULL,
	.name = IL4965_RS_NAME,
	.tx_status = il4965_rs_tx_status,
	.get_rate = il4965_rs_get_rate,
	.rate_init = il4965_rs_rate_init_stub,
	.alloc = il4965_rs_alloc,
	.free = il4965_rs_free,
	.alloc_sta = il4965_rs_alloc_sta,
	.free_sta = il4965_rs_free_sta,
#ifdef CONFIG_MAC80211_DEBUGFS
	.add_sta_debugfs = il4965_rs_add_debugfs,
	.remove_sta_debugfs = il4965_rs_remove_debugfs,
#endif
};

int
il4965_rate_control_register(void)
{
	return ieee80211_rate_control_register(&rs_4965_ops);
}

void
il4965_rate_control_unregister(void)
{
	ieee80211_rate_control_unregister(&rs_4965_ops);
}
