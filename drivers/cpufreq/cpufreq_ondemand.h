/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Header file for CPUFreq ondemand governor and related code.
 *
 * Copyright (C) 2016, Intel Corporation
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 */

#include "cpufreq_governor.h"

struct od_policy_dbs_info {
	struct policy_dbs_info policy_dbs;
	unsigned int freq_lo;
	unsigned int freq_lo_delay_us;
	unsigned int freq_hi_delay_us;
	unsigned int sample_type:1;
};

static inline struct od_policy_dbs_info *to_dbs_info(struct policy_dbs_info *policy_dbs)
{
	return container_of(policy_dbs, struct od_policy_dbs_info, policy_dbs);
}

struct od_dbs_tuners {
	unsigned int powersave_bias;
};
