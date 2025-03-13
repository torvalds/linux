/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2024 Raspberry Pi
 */

#ifndef V3D_PERFORMANCE_COUNTERS_H
#define V3D_PERFORMANCE_COUNTERS_H

/* Holds a description of a given performance counter. The index of
 * performance counter is given by the array on `v3d_performance_counter.c`.
 */
struct v3d_perf_counter_desc {
	/* Category of the counter */
	char category[32];

	/* Name of the counter */
	char name[64];

	/* Description of the counter */
	char description[256];
};

struct v3d_perfmon_info {
	/* Different revisions of V3D have different total number of
	 * performance counters.
	 */
	unsigned int max_counters;

	/* Array of counters valid for the platform. */
	const struct v3d_perf_counter_desc *counters;
};

#endif
