/* SPDX-License-Identifier: GPL-2.0 */
/*
 * (c) Copyright 2007 Hewlett-Packard Development Company, L.P.
 *        Contributed by Peter Keilty <peter.keilty@hp.com>
 *
 * fsyscall gettimeofday data
 */

/* like timespec, but includes "shifted nanoseconds" */
struct time_sn_spec {
	u64	sec;
	u64	snsec;
};

struct fsyscall_gtod_data_t {
	seqcount_t	seq;
	struct time_sn_spec wall_time;
	struct time_sn_spec monotonic_time;
	u64		clk_mask;
	u32		clk_mult;
	u32		clk_shift;
	void		*clk_fsys_mmio;
	u64		clk_cycle_last;
} ____cacheline_aligned;

struct itc_jitter_data_t {
	int		itc_jitter;
	u64		itc_lastcycle;
} ____cacheline_aligned;

