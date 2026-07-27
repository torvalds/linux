/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2026 Morse Micro
 */

#ifndef _MM81X_MMRC_H_
#define _MM81X_MMRC_H_

#include <linux/version.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/random.h>
#include <linux/time.h>

/* The max length of a retry chain for a single packet transmission */
#define MMRC_MAX_CHAIN_LENGTH 4

/* Rate minimum allowed attempts */
#define MMRC_MIN_CHAIN_ATTEMPTS 1

/* Rate upper limit for attempts */
#define MMRC_MAX_CHAIN_ATTEMPTS 2

/* The frequency of MMRC stat table updates */
#define MMRC_UPDATE_FREQUENCY_MS 100

enum mmrc_flags {
	MMRC_FLAGS_CTS_RTS,
};

enum mmrc_mcs_rate {
	MMRC_MCS0,
	MMRC_MCS1,
	MMRC_MCS2,
	MMRC_MCS3,
	MMRC_MCS4,
	MMRC_MCS5,
	MMRC_MCS6,
	MMRC_MCS7,
	MMRC_MCS8,
	MMRC_MCS9,
	MMRC_MCS10,
	MMRC_MCS_UNUSED,
};

enum mmrc_bw {
	MMRC_BW_1MHZ = 0,
	MMRC_BW_2MHZ = 1,
	MMRC_BW_4MHZ = 2,
	MMRC_BW_8MHZ = 3,
	MMRC_BW_16MHZ = 4,
	MMRC_BW_MAX = 5,
};

enum mmrc_spatial_stream {
	MMRC_SPATIAL_STREAM_1 = 0,
	MMRC_SPATIAL_STREAM_2 = 1,
	MMRC_SPATIAL_STREAM_3 = 2,
	MMRC_SPATIAL_STREAM_4 = 3,
	MMRC_SPATIAL_STREAM_MAX,
};

enum mmrc_guard {
	MMRC_GUARD_LONG = 0,
	MMRC_GUARD_SHORT = 1,
	MMRC_GUARD_MAX,
};

#define MMRC_RATE_TO_BITFIELD(x) ((x) & 0xF)
#define MMRC_ATTEMPTS_TO_BITFIELD(x) ((x) & 0x7)
#define MMRC_GUARD_TO_BITFIELD(x) ((x) & 0x1)
#define MMRC_SS_TO_BITFIELD(x) ((x) & 0x3)
#define MMRC_BW_TO_BITFIELD(x) ((x) & 0x7)
#define MMRC_FLAGS_TO_BITFIELD(x) ((x) & 0x7)

struct mmrc_rate {
	u8 rate : 4;
	u8 attempts : 3;
	u8 guard : 1;
	u8 ss : 2;
	u8 bw : 3;
	u8 flags : 3;
	u16 index;
};

struct mmrc_rate_table {
	struct mmrc_rate rates[MMRC_MAX_CHAIN_LENGTH];
};

#define SGI_PER_BW(bw) (1 << (bw))

struct mmrc_sta_capabilities {
	u8 max_rates : 3;
	u8 max_retries : 3;
	u8 bandwidth : 5;
	u8 spatial_streams : 4;
	u16 rates : 11;
	u8 guard : 2;
	u8 sta_flags : 4;
	u8 sgi_per_bw : 5;
};

struct mmrc_stats_table {
	u32 avg_throughput_counter;
	u32 sum_throughput;
	u32 max_throughput;
	u16 sent;
	u16 sent_success;
	u16 back_mpdu_success;
	u16 back_mpdu_failure;
	u32 total_sent;
	u32 total_success;
	u16 evidence;
	u8 prob;
	bool have_sent_ampdus;
};

struct mmrc_table {
	struct mmrc_sta_capabilities caps;
	struct mmrc_rate best_tp;
	struct mmrc_rate second_tp;
	struct mmrc_rate baseline;
	struct mmrc_rate best_prob;
	struct mmrc_rate fixed_rate;
	u32 cycle_cnt;
	u32 last_lookaround_cycle;
	u8 lookaround_cnt;

	/* The ratio of using normal rate and sampling */
	u8 lookaround_wrap;

	/*
	 * A counter that is used to determine when we should force a
	 * lookaround. Should be a portion of the above lookaround with
	 * less constraints
	 */
	u8 forced_lookaround;

	u8 current_lookaround_rate_attempts;
	u16 current_lookaround_rate_index;
	u32 total_lookaround;

	/*
	 * A counter to detect if the current best rate is optimal
	 * and may slow down sample frequency.
	 */
	u32 stability_cnt;

	u32 stability_cnt_threshold;
	u8 probability_variation;

	/* The difference in MCS from each of the last 2 rate changes */
	s8 best_rate_diff[2];

	/* Indication of random versus consistently one-sided variation */
	s8 probability_variation_direction;

	/* Has rate control detected possible interference */
	bool interference_likely;

	/* Has rate control detected the best rate is no longer converged */
	bool unconverged;

	/* Is rate control just entering unconverged state */
	bool newly_unconverged;

	/*
	 * Number of rate control cycles the best rate has remained
	 * unchanged
	 */
	s32 best_rate_cycle_count;

	/*
	 * The probability table for the STA. This MUST always be the last
	 * element in the struct.
	 */
	struct mmrc_stats_table table[];
};

void mmrc_sta_init(struct mmrc_table *tb, struct mmrc_sta_capabilities *caps,
		   s8 rssi);
size_t mmrc_memory_required_for_caps(struct mmrc_sta_capabilities *caps);
void mmrc_get_rates(struct mmrc_table *tb, struct mmrc_rate_table *out,
		    size_t size);
void mmrc_feedback(struct mmrc_table *tb, struct mmrc_rate_table *rates,
		   s32 retry_count, bool was_aggregated);
void mmrc_update(struct mmrc_table *tb);
bool mmrc_set_fixed_rate(struct mmrc_table *tb, struct mmrc_rate fixed_rate);
u32 mmrc_calculate_theoretical_throughput(struct mmrc_rate rate);
u32 mmrc_calculate_rate_tx_time(struct mmrc_rate *rate, size_t size);

#endif /* _MMRC_H_ */
