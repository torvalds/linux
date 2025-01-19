/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Advanced Micro Devices, Inc.
 *
 * Author: Meng Li <li.meng@amd.com>
 */

#ifndef _LINUX_AMD_PSTATE_H
#define _LINUX_AMD_PSTATE_H

#include <linux/pm_qos.h>

/*********************************************************************
 *                        AMD P-state INTERFACE                       *
 *********************************************************************/

/**
 * union perf_cached - A union to cache performance-related data.
 * @highest_perf: the maximum performance an individual processor may reach,
 *		  assuming ideal conditions
 *		  For platforms that support the preferred core feature, the highest_perf value maybe
 * 		  configured to any value in the range 166-255 by the firmware (because the preferred
 * 		  core ranking is encoded in the highest_perf value). To maintain consistency across
 * 		  all platforms, we split the highest_perf and preferred core ranking values into
 * 		  cpudata->perf.highest_perf and cpudata->prefcore_ranking.
 * @nominal_perf: the maximum sustained performance level of the processor,
 *		  assuming ideal operating conditions
 * @lowest_nonlinear_perf: the lowest performance level at which nonlinear power
 *			   savings are achieved
 * @lowest_perf: the absolute lowest performance level of the processor
 * @min_limit_perf: Cached value of the performance corresponding to policy->min
 * @max_limit_perf: Cached value of the performance corresponding to policy->max
 */
union perf_cached {
	struct {
		u8	highest_perf;
		u8	nominal_perf;
		u8	lowest_nonlinear_perf;
		u8	lowest_perf;
		u8	min_limit_perf;
		u8	max_limit_perf;
	};
	u64	val;
};

/**
 * struct  amd_aperf_mperf
 * @aperf: actual performance frequency clock count
 * @mperf: maximum performance frequency clock count
 * @tsc:   time stamp counter
 */
struct amd_aperf_mperf {
	u64 aperf;
	u64 mperf;
	u64 tsc;
};

/**
 * struct amd_cpudata - private CPU data for AMD P-State
 * @cpu: CPU number
 * @req: constraint request to apply
 * @cppc_req_cached: cached performance request hints
 * @perf: cached performance-related data
 * @prefcore_ranking: the preferred core ranking, the higher value indicates a higher
 * 		  priority.
 * @min_limit_freq: Cached value of policy->min (in khz)
 * @max_limit_freq: Cached value of policy->max (in khz)
 * @nominal_freq: the frequency (in khz) that mapped to nominal_perf
 * @lowest_nonlinear_freq: the frequency (in khz) that mapped to lowest_nonlinear_perf
 * @cur: Difference of Aperf/Mperf/tsc count between last and current sample
 * @prev: Last Aperf/Mperf/tsc count value read from register
 * @freq: current cpu frequency value (in khz)
 * @boost_supported: check whether the Processor or SBIOS supports boost mode
 * @hw_prefcore: check whether HW supports preferred core featue.
 * 		  Only when hw_prefcore and early prefcore param are true,
 * 		  AMD P-State driver supports preferred core featue.
 * @epp_cached: Cached CPPC energy-performance preference value
 * @policy: Cpufreq policy value
 *
 * The amd_cpudata is key private data for each CPU thread in AMD P-State, and
 * represents all the attributes and goals that AMD P-State requests at runtime.
 */
struct amd_cpudata {
	int	cpu;

	struct	freq_qos_request req[2];
	u64	cppc_req_cached;

	union perf_cached perf;

	u8	prefcore_ranking;
	u32	min_limit_freq;
	u32	max_limit_freq;
	u32	nominal_freq;
	u32	lowest_nonlinear_freq;

	struct amd_aperf_mperf cur;
	struct amd_aperf_mperf prev;

	u64	freq;
	bool	boost_supported;
	bool	hw_prefcore;

	/* EPP feature related attributes*/
	u32	policy;
	bool	suspended;
	u8	epp_default;
};

/*
 * enum amd_pstate_mode - driver working mode of amd pstate
 */
enum amd_pstate_mode {
	AMD_PSTATE_UNDEFINED = 0,
	AMD_PSTATE_DISABLE,
	AMD_PSTATE_PASSIVE,
	AMD_PSTATE_ACTIVE,
	AMD_PSTATE_GUIDED,
	AMD_PSTATE_MAX,
};
const char *amd_pstate_get_mode_string(enum amd_pstate_mode mode);
int amd_pstate_update_status(const char *buf, size_t size);

#endif /* _LINUX_AMD_PSTATE_H */
