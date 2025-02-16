/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2025 Intel Corporation
 */
#ifndef __iwl_mld_ptp_h__
#define __iwl_mld_ptp_h__

#include <linux/ptp_clock_kernel.h>

/**
 * struct ptp_data - PTP hardware clock data
 *
 * @ptp_clock: struct ptp_clock pointer returned by the ptp_clock_register()
 *	function.
 * @ptp_clock_info: struct ptp_clock_info that describes a PTP hardware clock
 * @lock: protects the time adjustments data
 * @delta: delta between hardware clock and ptp clock in nanoseconds
 * @scale_update_gp2: GP2 time when the scale was last updated
 * @scale_update_adj_time_ns: adjusted time when the scale was last updated,
 *	in nanoseconds
 * @scaled_freq: clock frequency offset, scaled to 65536000000
 * @last_gp2: the last GP2 reading from the hardware, used for tracking GP2
 *	wraparounds
 * @wrap_counter: number of wraparounds since scale_update_adj_time_ns
 * @dwork: worker scheduled every 1 hour to detect workarounds
 */
struct ptp_data {
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_clock_info;

	spinlock_t lock;
	s64 delta;
	u32 scale_update_gp2;
	u64 scale_update_adj_time_ns;
	u64 scaled_freq;
	u32 last_gp2;
	u32 wrap_counter;
	struct delayed_work dwork;
};

void iwl_mld_ptp_init(struct iwl_mld *mld);
void iwl_mld_ptp_remove(struct iwl_mld *mld);
u64 iwl_mld_ptp_get_adj_time(struct iwl_mld *mld, u64 base_time_ns);

#endif /* __iwl_mld_ptp_h__ */
