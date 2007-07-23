/*
 * (c) Copyright 2007 Hewlett-Packard Development Company, L.P.
 *        Contributed by Peter Keilty <peter.keilty@hp.com>
 *
 * fsyscall gettimeofday data
 */

struct fsyscall_gtod_data_t {
	seqlock_t	lock;
	struct timespec	wall_time;
	struct timespec monotonic_time;
	cycle_t		clk_mask;
	u32		clk_mult;
	u32		clk_shift;
	void		*clk_fsys_mmio;
	cycle_t		clk_cycle_last;
} __attribute__ ((aligned (L1_CACHE_BYTES)));

struct itc_jitter_data_t {
	int		itc_jitter;
	cycle_t		itc_lastcycle;
} __attribute__ ((aligned (L1_CACHE_BYTES)));

