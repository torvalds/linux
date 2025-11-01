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

#ifdef CONFIG_X86
#include <asm/cpu_device_id.h>

/*
 * Not all CPUs want IO time to be accounted as busy; this depends on
 * how efficient idling at a higher frequency/voltage is.
 *
 * Pavel Machek says this is not so for various generations of AMD and
 * old Intel systems. Mike Chan (android.com) claims this is also not
 * true for ARM.
 *
 * Because of this, select a known series of Intel CPUs (Family 6 and
 * later) by default, and leave all others up to the user.
 */
static inline bool od_should_io_be_busy(void)
{
	return (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL &&
		boot_cpu_data.x86_vfm >= INTEL_PENTIUM_PRO);
}
#else
static inline bool od_should_io_be_busy(void) { return false; }
#endif
