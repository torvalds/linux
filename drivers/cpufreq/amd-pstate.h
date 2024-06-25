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
 * @highest_perf: the maximum performance an individual processor may reach,
 *		  assuming ideal conditions
 *		  For platforms that do not support the preferred core feature, the
 *		  highest_pef may be configured with 166 or 255, to avoid max frequency
 *		  calculated wrongly. we take the fixed value as the highest_perf.
 * @nominal_perf: the maximum sustained performance level of the processor,
 *		  assuming ideal operating conditions
 * @lowest_nonlinear_perf: the lowest performance level at which nonlinear power
 *			   savings are achieved
 * @lowest_perf: the absolute lowest performance level of the processor
 * @prefcore_ranking: the preferred core ranking, the higher value indicates a higher
 * 		  priority.
 * @min_limit_perf: Cached value of the performance corresponding to policy->min
 * @max_limit_perf: Cached value of the performance corresponding to policy->max
 * @min_limit_freq: Cached value of policy->min (in khz)
 * @max_limit_freq: Cached value of policy->max (in khz)
 * @max_freq: the frequency (in khz) that mapped to highest_perf
 * @min_freq: the frequency (in khz) that mapped to lowest_perf
 * @nominal_freq: the frequency (in khz) that mapped to nominal_perf
 * @lowest_nonlinear_freq: the frequency (in khz) that mapped to lowest_nonlinear_perf
 * @cur: Difference of Aperf/Mperf/tsc count between last and current sample
 * @prev: Last Aperf/Mperf/tsc count value read from register
 * @freq: current cpu frequency value (in khz)
 * @boost_supported: check whether the Processor or SBIOS supports boost mode
 * @hw_prefcore: check whether HW supports preferred core featue.
 * 		  Only when hw_prefcore and early prefcore param are true,
 * 		  AMD P-State driver supports preferred core featue.
 * @epp_policy: Last saved policy used to set energy-performance preference
 * @epp_cached: Cached CPPC energy-performance preference value
 * @policy: Cpufreq policy value
 * @cppc_cap1_cached Cached MSR_AMD_CPPC_CAP1 register value
 *
 * The amd_cpudata is key private data for each CPU thread in AMD P-State, and
 * represents all the attributes and goals that AMD P-State requests at runtime.
 */
struct amd_cpudata {
	int	cpu;

	struct	freq_qos_request req[2];
	u64	cppc_req_cached;

	u32	highest_perf;
	u32	nominal_perf;
	u32	lowest_nonlinear_perf;
	u32	lowest_perf;
	u32     prefcore_ranking;
	u32     min_limit_perf;
	u32     max_limit_perf;
	u32     min_limit_freq;
	u32     max_limit_freq;

	u32	max_freq;
	u32	min_freq;
	u32	nominal_freq;
	u32	lowest_nonlinear_freq;

	struct amd_aperf_mperf cur;
	struct amd_aperf_mperf prev;

	u64	freq;
	bool	boost_supported;
	bool	hw_prefcore;

	/* EPP feature related attributes*/
	s16	epp_policy;
	s16	epp_cached;
	u32	policy;
	u64	cppc_cap1_cached;
	bool	suspended;
	s16	epp_default;
};

#endif /* _LINUX_AMD_PSTATE_H */
