// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * amd-pstate.c - AMD Processor P-state Frequency Driver
 *
 * Copyright (C) 2021 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 * Author: Huang Rui <ray.huang@amd.com>
 *
 * AMD P-State introduces a new CPU performance scaling design for AMD
 * processors using the ACPI Collaborative Performance and Power Control (CPPC)
 * feature which works with the AMD SMU firmware providing a finer grained
 * frequency control range. It is to replace the legacy ACPI P-States control,
 * allows a flexible, low-latency interface for the Linux kernel to directly
 * communicate the performance hints to hardware.
 *
 * AMD P-State is supported on recent AMD Zen base CPU series include some of
 * Zen2 and Zen3 processors. _CPC needs to be present in the ACPI tables of AMD
 * P-State supported system. And there are two types of hardware implementations
 * for AMD P-State: 1) Full MSR Solution and 2) Shared Memory Solution.
 * X86_FEATURE_CPPC CPU feature flag is used to distinguish the different types.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitfield.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/compiler.h>
#include <linux/dmi.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/static_call.h>
#include <linux/topology.h>

#include <acpi/processor.h>
#include <acpi/cppc_acpi.h>

#include <asm/msr.h>
#include <asm/processor.h>
#include <asm/cpufeature.h>
#include <asm/cpu_device_id.h>

#include "amd-pstate.h"
#include "amd-pstate-trace.h"

#define AMD_PSTATE_TRANSITION_LATENCY	20000
#define AMD_PSTATE_TRANSITION_DELAY	1000
#define AMD_PSTATE_FAST_CPPC_TRANSITION_DELAY 600

#define AMD_CPPC_EPP_PERFORMANCE		0x00
#define AMD_CPPC_EPP_BALANCE_PERFORMANCE	0x80
#define AMD_CPPC_EPP_BALANCE_POWERSAVE		0xBF
#define AMD_CPPC_EPP_POWERSAVE			0xFF

static const char * const amd_pstate_mode_string[] = {
	[AMD_PSTATE_UNDEFINED]   = "undefined",
	[AMD_PSTATE_DISABLE]     = "disable",
	[AMD_PSTATE_PASSIVE]     = "passive",
	[AMD_PSTATE_ACTIVE]      = "active",
	[AMD_PSTATE_GUIDED]      = "guided",
	NULL,
};

const char *amd_pstate_get_mode_string(enum amd_pstate_mode mode)
{
	if (mode < 0 || mode >= AMD_PSTATE_MAX)
		return NULL;
	return amd_pstate_mode_string[mode];
}
EXPORT_SYMBOL_GPL(amd_pstate_get_mode_string);

struct quirk_entry {
	u32 nominal_freq;
	u32 lowest_freq;
};

static struct cpufreq_driver *current_pstate_driver;
static struct cpufreq_driver amd_pstate_driver;
static struct cpufreq_driver amd_pstate_epp_driver;
static int cppc_state = AMD_PSTATE_UNDEFINED;
static bool amd_pstate_prefcore = true;
static struct quirk_entry *quirks;

/*
 * AMD Energy Preference Performance (EPP)
 * The EPP is used in the CCLK DPM controller to drive
 * the frequency that a core is going to operate during
 * short periods of activity. EPP values will be utilized for
 * different OS profiles (balanced, performance, power savings)
 * display strings corresponding to EPP index in the
 * energy_perf_strings[]
 *	index		String
 *-------------------------------------
 *	0		default
 *	1		performance
 *	2		balance_performance
 *	3		balance_power
 *	4		power
 */
enum energy_perf_value_index {
	EPP_INDEX_DEFAULT = 0,
	EPP_INDEX_PERFORMANCE,
	EPP_INDEX_BALANCE_PERFORMANCE,
	EPP_INDEX_BALANCE_POWERSAVE,
	EPP_INDEX_POWERSAVE,
};

static const char * const energy_perf_strings[] = {
	[EPP_INDEX_DEFAULT] = "default",
	[EPP_INDEX_PERFORMANCE] = "performance",
	[EPP_INDEX_BALANCE_PERFORMANCE] = "balance_performance",
	[EPP_INDEX_BALANCE_POWERSAVE] = "balance_power",
	[EPP_INDEX_POWERSAVE] = "power",
	NULL
};

static unsigned int epp_values[] = {
	[EPP_INDEX_DEFAULT] = 0,
	[EPP_INDEX_PERFORMANCE] = AMD_CPPC_EPP_PERFORMANCE,
	[EPP_INDEX_BALANCE_PERFORMANCE] = AMD_CPPC_EPP_BALANCE_PERFORMANCE,
	[EPP_INDEX_BALANCE_POWERSAVE] = AMD_CPPC_EPP_BALANCE_POWERSAVE,
	[EPP_INDEX_POWERSAVE] = AMD_CPPC_EPP_POWERSAVE,
 };

typedef int (*cppc_mode_transition_fn)(int);

static struct quirk_entry quirk_amd_7k62 = {
	.nominal_freq = 2600,
	.lowest_freq = 550,
};

static inline u8 freq_to_perf(union perf_cached perf, u32 nominal_freq, unsigned int freq_val)
{
	u32 perf_val = DIV_ROUND_UP_ULL((u64)freq_val * perf.nominal_perf, nominal_freq);

	return (u8)clamp(perf_val, perf.lowest_perf, perf.highest_perf);
}

static inline u32 perf_to_freq(union perf_cached perf, u32 nominal_freq, u8 perf_val)
{
	return DIV_ROUND_UP_ULL((u64)nominal_freq * perf_val,
				perf.nominal_perf);
}

static int __init dmi_matched_7k62_bios_bug(const struct dmi_system_id *dmi)
{
	/**
	 * match the broken bios for family 17h processor support CPPC V2
	 * broken BIOS lack of nominal_freq and lowest_freq capabilities
	 * definition in ACPI tables
	 */
	if (cpu_feature_enabled(X86_FEATURE_ZEN2)) {
		quirks = dmi->driver_data;
		pr_info("Overriding nominal and lowest frequencies for %s\n", dmi->ident);
		return 1;
	}

	return 0;
}

static const struct dmi_system_id amd_pstate_quirks_table[] __initconst = {
	{
		.callback = dmi_matched_7k62_bios_bug,
		.ident = "AMD EPYC 7K62",
		.matches = {
			DMI_MATCH(DMI_BIOS_VERSION, "5.14"),
			DMI_MATCH(DMI_BIOS_RELEASE, "12/12/2019"),
		},
		.driver_data = &quirk_amd_7k62,
	},
	{}
};
MODULE_DEVICE_TABLE(dmi, amd_pstate_quirks_table);

static inline int get_mode_idx_from_str(const char *str, size_t size)
{
	int i;

	for (i=0; i < AMD_PSTATE_MAX; i++) {
		if (!strncmp(str, amd_pstate_mode_string[i], size))
			return i;
	}
	return -EINVAL;
}

static DEFINE_MUTEX(amd_pstate_driver_lock);

static u8 msr_get_epp(struct amd_cpudata *cpudata)
{
	u64 value;
	int ret;

	ret = rdmsrq_on_cpu(cpudata->cpu, MSR_AMD_CPPC_REQ, &value);
	if (ret < 0) {
		pr_debug("Could not retrieve energy perf value (%d)\n", ret);
		return ret;
	}

	return FIELD_GET(AMD_CPPC_EPP_PERF_MASK, value);
}

DEFINE_STATIC_CALL(amd_pstate_get_epp, msr_get_epp);

static inline s16 amd_pstate_get_epp(struct amd_cpudata *cpudata)
{
	return static_call(amd_pstate_get_epp)(cpudata);
}

static u8 shmem_get_epp(struct amd_cpudata *cpudata)
{
	u64 epp;
	int ret;

	ret = cppc_get_epp_perf(cpudata->cpu, &epp);
	if (ret < 0) {
		pr_debug("Could not retrieve energy perf value (%d)\n", ret);
		return ret;
	}

	return FIELD_GET(AMD_CPPC_EPP_PERF_MASK, epp);
}

static int msr_update_perf(struct cpufreq_policy *policy, u8 min_perf,
			   u8 des_perf, u8 max_perf, u8 epp, bool fast_switch)
{
	struct amd_cpudata *cpudata = policy->driver_data;
	u64 value, prev;

	value = prev = READ_ONCE(cpudata->cppc_req_cached);

	value &= ~(AMD_CPPC_MAX_PERF_MASK | AMD_CPPC_MIN_PERF_MASK |
		   AMD_CPPC_DES_PERF_MASK | AMD_CPPC_EPP_PERF_MASK);
	value |= FIELD_PREP(AMD_CPPC_MAX_PERF_MASK, max_perf);
	value |= FIELD_PREP(AMD_CPPC_DES_PERF_MASK, des_perf);
	value |= FIELD_PREP(AMD_CPPC_MIN_PERF_MASK, min_perf);
	value |= FIELD_PREP(AMD_CPPC_EPP_PERF_MASK, epp);

	if (trace_amd_pstate_epp_perf_enabled()) {
		union perf_cached perf = READ_ONCE(cpudata->perf);

		trace_amd_pstate_epp_perf(cpudata->cpu,
					  perf.highest_perf,
					  epp,
					  min_perf,
					  max_perf,
					  policy->boost_enabled,
					  value != prev);
	}

	if (value == prev)
		return 0;

	if (fast_switch) {
		wrmsrq(MSR_AMD_CPPC_REQ, value);
		return 0;
	} else {
		int ret = wrmsrq_on_cpu(cpudata->cpu, MSR_AMD_CPPC_REQ, value);

		if (ret)
			return ret;
	}

	WRITE_ONCE(cpudata->cppc_req_cached, value);

	return 0;
}

DEFINE_STATIC_CALL(amd_pstate_update_perf, msr_update_perf);

static inline int amd_pstate_update_perf(struct cpufreq_policy *policy,
					  u8 min_perf, u8 des_perf,
					  u8 max_perf, u8 epp,
					  bool fast_switch)
{
	return static_call(amd_pstate_update_perf)(policy, min_perf, des_perf,
						   max_perf, epp, fast_switch);
}

static int msr_set_epp(struct cpufreq_policy *policy, u8 epp)
{
	struct amd_cpudata *cpudata = policy->driver_data;
	u64 value, prev;
	int ret;

	value = prev = READ_ONCE(cpudata->cppc_req_cached);
	value &= ~AMD_CPPC_EPP_PERF_MASK;
	value |= FIELD_PREP(AMD_CPPC_EPP_PERF_MASK, epp);

	if (trace_amd_pstate_epp_perf_enabled()) {
		union perf_cached perf = cpudata->perf;

		trace_amd_pstate_epp_perf(cpudata->cpu, perf.highest_perf,
					  epp,
					  FIELD_GET(AMD_CPPC_MIN_PERF_MASK,
						    cpudata->cppc_req_cached),
					  FIELD_GET(AMD_CPPC_MAX_PERF_MASK,
						    cpudata->cppc_req_cached),
					  policy->boost_enabled,
					  value != prev);
	}

	if (value == prev)
		return 0;

	ret = wrmsrq_on_cpu(cpudata->cpu, MSR_AMD_CPPC_REQ, value);
	if (ret) {
		pr_err("failed to set energy perf value (%d)\n", ret);
		return ret;
	}

	/* update both so that msr_update_perf() can effectively check */
	WRITE_ONCE(cpudata->cppc_req_cached, value);

	return ret;
}

DEFINE_STATIC_CALL(amd_pstate_set_epp, msr_set_epp);

static inline int amd_pstate_set_epp(struct cpufreq_policy *policy, u8 epp)
{
	return static_call(amd_pstate_set_epp)(policy, epp);
}

static int shmem_set_epp(struct cpufreq_policy *policy, u8 epp)
{
	struct amd_cpudata *cpudata = policy->driver_data;
	struct cppc_perf_ctrls perf_ctrls;
	u8 epp_cached;
	u64 value;
	int ret;


	epp_cached = FIELD_GET(AMD_CPPC_EPP_PERF_MASK, cpudata->cppc_req_cached);
	if (trace_amd_pstate_epp_perf_enabled()) {
		union perf_cached perf = cpudata->perf;

		trace_amd_pstate_epp_perf(cpudata->cpu, perf.highest_perf,
					  epp,
					  FIELD_GET(AMD_CPPC_MIN_PERF_MASK,
						    cpudata->cppc_req_cached),
					  FIELD_GET(AMD_CPPC_MAX_PERF_MASK,
						    cpudata->cppc_req_cached),
					  policy->boost_enabled,
					  epp != epp_cached);
	}

	if (epp == epp_cached)
		return 0;

	perf_ctrls.energy_perf = epp;
	ret = cppc_set_epp_perf(cpudata->cpu, &perf_ctrls, 1);
	if (ret) {
		pr_debug("failed to set energy perf value (%d)\n", ret);
		return ret;
	}

	value = READ_ONCE(cpudata->cppc_req_cached);
	value &= ~AMD_CPPC_EPP_PERF_MASK;
	value |= FIELD_PREP(AMD_CPPC_EPP_PERF_MASK, epp);
	WRITE_ONCE(cpudata->cppc_req_cached, value);

	return ret;
}

static inline int msr_cppc_enable(struct cpufreq_policy *policy)
{
	return wrmsrq_safe_on_cpu(policy->cpu, MSR_AMD_CPPC_ENABLE, 1);
}

static int shmem_cppc_enable(struct cpufreq_policy *policy)
{
	return cppc_set_enable(policy->cpu, 1);
}

DEFINE_STATIC_CALL(amd_pstate_cppc_enable, msr_cppc_enable);

static inline int amd_pstate_cppc_enable(struct cpufreq_policy *policy)
{
	return static_call(amd_pstate_cppc_enable)(policy);
}

static int msr_init_perf(struct amd_cpudata *cpudata)
{
	union perf_cached perf = READ_ONCE(cpudata->perf);
	u64 cap1, numerator, cppc_req;
	u8 min_perf;

	int ret = rdmsrq_safe_on_cpu(cpudata->cpu, MSR_AMD_CPPC_CAP1,
				     &cap1);
	if (ret)
		return ret;

	ret = amd_get_boost_ratio_numerator(cpudata->cpu, &numerator);
	if (ret)
		return ret;

	ret = rdmsrl_on_cpu(cpudata->cpu, MSR_AMD_CPPC_REQ, &cppc_req);
	if (ret)
		return ret;

	WRITE_ONCE(cpudata->cppc_req_cached, cppc_req);
	min_perf = FIELD_GET(AMD_CPPC_MIN_PERF_MASK, cppc_req);

	/*
	 * Clear out the min_perf part to check if the rest of the MSR is 0, if yes, this is an
	 * indication that the min_perf value is the one specified through the BIOS option
	 */
	cppc_req &= ~(AMD_CPPC_MIN_PERF_MASK);

	if (!cppc_req)
		perf.bios_min_perf = min_perf;

	perf.highest_perf = numerator;
	perf.max_limit_perf = numerator;
	perf.min_limit_perf = FIELD_GET(AMD_CPPC_LOWEST_PERF_MASK, cap1);
	perf.nominal_perf = FIELD_GET(AMD_CPPC_NOMINAL_PERF_MASK, cap1);
	perf.lowest_nonlinear_perf = FIELD_GET(AMD_CPPC_LOWNONLIN_PERF_MASK, cap1);
	perf.lowest_perf = FIELD_GET(AMD_CPPC_LOWEST_PERF_MASK, cap1);
	WRITE_ONCE(cpudata->perf, perf);
	WRITE_ONCE(cpudata->prefcore_ranking, FIELD_GET(AMD_CPPC_HIGHEST_PERF_MASK, cap1));

	return 0;
}

static int shmem_init_perf(struct amd_cpudata *cpudata)
{
	struct cppc_perf_caps cppc_perf;
	union perf_cached perf = READ_ONCE(cpudata->perf);
	u64 numerator;
	bool auto_sel;

	int ret = cppc_get_perf_caps(cpudata->cpu, &cppc_perf);
	if (ret)
		return ret;

	ret = amd_get_boost_ratio_numerator(cpudata->cpu, &numerator);
	if (ret)
		return ret;

	perf.highest_perf = numerator;
	perf.max_limit_perf = numerator;
	perf.min_limit_perf = cppc_perf.lowest_perf;
	perf.nominal_perf = cppc_perf.nominal_perf;
	perf.lowest_nonlinear_perf = cppc_perf.lowest_nonlinear_perf;
	perf.lowest_perf = cppc_perf.lowest_perf;
	WRITE_ONCE(cpudata->perf, perf);
	WRITE_ONCE(cpudata->prefcore_ranking, cppc_perf.highest_perf);

	if (cppc_state == AMD_PSTATE_ACTIVE)
		return 0;

	ret = cppc_get_auto_sel(cpudata->cpu, &auto_sel);
	if (ret) {
		pr_warn("failed to get auto_sel, ret: %d\n", ret);
		return 0;
	}

	ret = cppc_set_auto_sel(cpudata->cpu,
			(cppc_state == AMD_PSTATE_PASSIVE) ? 0 : 1);

	if (ret)
		pr_warn("failed to set auto_sel, ret: %d\n", ret);

	return ret;
}

DEFINE_STATIC_CALL(amd_pstate_init_perf, msr_init_perf);

static inline int amd_pstate_init_perf(struct amd_cpudata *cpudata)
{
	return static_call(amd_pstate_init_perf)(cpudata);
}

static int shmem_update_perf(struct cpufreq_policy *policy, u8 min_perf,
			     u8 des_perf, u8 max_perf, u8 epp, bool fast_switch)
{
	struct amd_cpudata *cpudata = policy->driver_data;
	struct cppc_perf_ctrls perf_ctrls;
	u64 value, prev;
	int ret;

	if (cppc_state == AMD_PSTATE_ACTIVE) {
		int ret = shmem_set_epp(policy, epp);

		if (ret)
			return ret;
	}

	value = prev = READ_ONCE(cpudata->cppc_req_cached);

	value &= ~(AMD_CPPC_MAX_PERF_MASK | AMD_CPPC_MIN_PERF_MASK |
		   AMD_CPPC_DES_PERF_MASK | AMD_CPPC_EPP_PERF_MASK);
	value |= FIELD_PREP(AMD_CPPC_MAX_PERF_MASK, max_perf);
	value |= FIELD_PREP(AMD_CPPC_DES_PERF_MASK, des_perf);
	value |= FIELD_PREP(AMD_CPPC_MIN_PERF_MASK, min_perf);
	value |= FIELD_PREP(AMD_CPPC_EPP_PERF_MASK, epp);

	if (trace_amd_pstate_epp_perf_enabled()) {
		union perf_cached perf = READ_ONCE(cpudata->perf);

		trace_amd_pstate_epp_perf(cpudata->cpu,
					  perf.highest_perf,
					  epp,
					  min_perf,
					  max_perf,
					  policy->boost_enabled,
					  value != prev);
	}

	if (value == prev)
		return 0;

	perf_ctrls.max_perf = max_perf;
	perf_ctrls.min_perf = min_perf;
	perf_ctrls.desired_perf = des_perf;

	ret = cppc_set_perf(cpudata->cpu, &perf_ctrls);
	if (ret)
		return ret;

	WRITE_ONCE(cpudata->cppc_req_cached, value);

	return 0;
}

static inline bool amd_pstate_sample(struct amd_cpudata *cpudata)
{
	u64 aperf, mperf, tsc;
	unsigned long flags;

	local_irq_save(flags);
	rdmsrq(MSR_IA32_APERF, aperf);
	rdmsrq(MSR_IA32_MPERF, mperf);
	tsc = rdtsc();

	if (cpudata->prev.mperf == mperf || cpudata->prev.tsc == tsc) {
		local_irq_restore(flags);
		return false;
	}

	local_irq_restore(flags);

	cpudata->cur.aperf = aperf;
	cpudata->cur.mperf = mperf;
	cpudata->cur.tsc =  tsc;
	cpudata->cur.aperf -= cpudata->prev.aperf;
	cpudata->cur.mperf -= cpudata->prev.mperf;
	cpudata->cur.tsc -= cpudata->prev.tsc;

	cpudata->prev.aperf = aperf;
	cpudata->prev.mperf = mperf;
	cpudata->prev.tsc = tsc;

	cpudata->freq = div64_u64((cpudata->cur.aperf * cpu_khz), cpudata->cur.mperf);

	return true;
}

static void amd_pstate_update(struct amd_cpudata *cpudata, u8 min_perf,
			      u8 des_perf, u8 max_perf, bool fast_switch, int gov_flags)
{
	struct cpufreq_policy *policy __free(put_cpufreq_policy) = cpufreq_cpu_get(cpudata->cpu);
	union perf_cached perf = READ_ONCE(cpudata->perf);

	if (!policy)
		return;

	/* limit the max perf when core performance boost feature is disabled */
	if (!cpudata->boost_supported)
		max_perf = min_t(u8, perf.nominal_perf, max_perf);

	des_perf = clamp_t(u8, des_perf, min_perf, max_perf);

	policy->cur = perf_to_freq(perf, cpudata->nominal_freq, des_perf);

	if ((cppc_state == AMD_PSTATE_GUIDED) && (gov_flags & CPUFREQ_GOV_DYNAMIC_SWITCHING)) {
		min_perf = des_perf;
		des_perf = 0;
	}

	if (trace_amd_pstate_perf_enabled() && amd_pstate_sample(cpudata)) {
		trace_amd_pstate_perf(min_perf, des_perf, max_perf, cpudata->freq,
			cpudata->cur.mperf, cpudata->cur.aperf, cpudata->cur.tsc,
				cpudata->cpu, fast_switch);
	}

	amd_pstate_update_perf(policy, min_perf, des_perf, max_perf, 0, fast_switch);
}

static int amd_pstate_verify(struct cpufreq_policy_data *policy_data)
{
	/*
	 * Initialize lower frequency limit (i.e.policy->min) with
	 * lowest_nonlinear_frequency or the min frequency (if) specified in BIOS,
	 * Override the initial value set by cpufreq core and amd-pstate qos_requests.
	 */
	if (policy_data->min == FREQ_QOS_MIN_DEFAULT_VALUE) {
		struct cpufreq_policy *policy __free(put_cpufreq_policy) =
					      cpufreq_cpu_get(policy_data->cpu);
		struct amd_cpudata *cpudata;
		union perf_cached perf;

		if (!policy)
			return -EINVAL;

		cpudata = policy->driver_data;
		perf = READ_ONCE(cpudata->perf);

		if (perf.bios_min_perf)
			policy_data->min = perf_to_freq(perf, cpudata->nominal_freq,
							perf.bios_min_perf);
		else
			policy_data->min = cpudata->lowest_nonlinear_freq;
	}

	cpufreq_verify_within_cpu_limits(policy_data);

	return 0;
}

static void amd_pstate_update_min_max_limit(struct cpufreq_policy *policy)
{
	struct amd_cpudata *cpudata = policy->driver_data;
	union perf_cached perf = READ_ONCE(cpudata->perf);

	perf.max_limit_perf = freq_to_perf(perf, cpudata->nominal_freq, policy->max);
	WRITE_ONCE(cpudata->max_limit_freq, policy->max);

	if (cpudata->policy == CPUFREQ_POLICY_PERFORMANCE) {
		perf.min_limit_perf = min(perf.nominal_perf, perf.max_limit_perf);
		WRITE_ONCE(cpudata->min_limit_freq, min(cpudata->nominal_freq, cpudata->max_limit_freq));
	} else {
		perf.min_limit_perf = freq_to_perf(perf, cpudata->nominal_freq, policy->min);
		WRITE_ONCE(cpudata->min_limit_freq, policy->min);
	}

	WRITE_ONCE(cpudata->perf, perf);
}

static int amd_pstate_update_freq(struct cpufreq_policy *policy,
				  unsigned int target_freq, bool fast_switch)
{
	struct cpufreq_freqs freqs;
	struct amd_cpudata *cpudata;
	union perf_cached perf;
	u8 des_perf;

	cpudata = policy->driver_data;

	if (policy->min != cpudata->min_limit_freq || policy->max != cpudata->max_limit_freq)
		amd_pstate_update_min_max_limit(policy);

	perf = READ_ONCE(cpudata->perf);

	freqs.old = policy->cur;
	freqs.new = target_freq;

	des_perf = freq_to_perf(perf, cpudata->nominal_freq, target_freq);

	WARN_ON(fast_switch && !policy->fast_switch_enabled);
	/*
	 * If fast_switch is desired, then there aren't any registered
	 * transition notifiers. See comment for
	 * cpufreq_enable_fast_switch().
	 */
	if (!fast_switch)
		cpufreq_freq_transition_begin(policy, &freqs);

	amd_pstate_update(cpudata, perf.min_limit_perf, des_perf,
			  perf.max_limit_perf, fast_switch,
			  policy->governor->flags);

	if (!fast_switch)
		cpufreq_freq_transition_end(policy, &freqs, false);

	return 0;
}

static int amd_pstate_target(struct cpufreq_policy *policy,
			     unsigned int target_freq,
			     unsigned int relation)
{
	return amd_pstate_update_freq(policy, target_freq, false);
}

static unsigned int amd_pstate_fast_switch(struct cpufreq_policy *policy,
				  unsigned int target_freq)
{
	if (!amd_pstate_update_freq(policy, target_freq, true))
		return target_freq;
	return policy->cur;
}

static void amd_pstate_adjust_perf(unsigned int cpu,
				   unsigned long _min_perf,
				   unsigned long target_perf,
				   unsigned long capacity)
{
	u8 max_perf, min_perf, des_perf, cap_perf;
	struct cpufreq_policy *policy __free(put_cpufreq_policy) = cpufreq_cpu_get(cpu);
	struct amd_cpudata *cpudata;
	union perf_cached perf;

	if (!policy)
		return;

	cpudata = policy->driver_data;

	if (policy->min != cpudata->min_limit_freq || policy->max != cpudata->max_limit_freq)
		amd_pstate_update_min_max_limit(policy);

	perf = READ_ONCE(cpudata->perf);
	cap_perf = perf.highest_perf;

	des_perf = cap_perf;
	if (target_perf < capacity)
		des_perf = DIV_ROUND_UP(cap_perf * target_perf, capacity);

	if (_min_perf < capacity)
		min_perf = DIV_ROUND_UP(cap_perf * _min_perf, capacity);
	else
		min_perf = cap_perf;

	if (min_perf < perf.min_limit_perf)
		min_perf = perf.min_limit_perf;

	max_perf = perf.max_limit_perf;
	if (max_perf < min_perf)
		max_perf = min_perf;

	amd_pstate_update(cpudata, min_perf, des_perf, max_perf, true,
			policy->governor->flags);
}

static int amd_pstate_cpu_boost_update(struct cpufreq_policy *policy, bool on)
{
	struct amd_cpudata *cpudata = policy->driver_data;
	union perf_cached perf = READ_ONCE(cpudata->perf);
	u32 nominal_freq, max_freq;
	int ret = 0;

	nominal_freq = READ_ONCE(cpudata->nominal_freq);
	max_freq = perf_to_freq(perf, cpudata->nominal_freq, perf.highest_perf);

	if (on)
		policy->cpuinfo.max_freq = max_freq;
	else if (policy->cpuinfo.max_freq > nominal_freq)
		policy->cpuinfo.max_freq = nominal_freq;

	policy->max = policy->cpuinfo.max_freq;

	if (cppc_state == AMD_PSTATE_PASSIVE) {
		ret = freq_qos_update_request(&cpudata->req[1], policy->cpuinfo.max_freq);
		if (ret < 0)
			pr_debug("Failed to update freq constraint: CPU%d\n", cpudata->cpu);
	}

	return ret < 0 ? ret : 0;
}

static int amd_pstate_set_boost(struct cpufreq_policy *policy, int state)
{
	struct amd_cpudata *cpudata = policy->driver_data;
	int ret;

	if (!cpudata->boost_supported) {
		pr_err("Boost mode is not supported by this processor or SBIOS\n");
		return -EOPNOTSUPP;
	}

	ret = amd_pstate_cpu_boost_update(policy, state);
	refresh_frequency_limits(policy);

	return ret;
}

static int amd_pstate_init_boost_support(struct amd_cpudata *cpudata)
{
	u64 boost_val;
	int ret = -1;

	/*
	 * If platform has no CPB support or disable it, initialize current driver
	 * boost_enabled state to be false, it is not an error for cpufreq core to handle.
	 */
	if (!cpu_feature_enabled(X86_FEATURE_CPB)) {
		pr_debug_once("Boost CPB capabilities not present in the processor\n");
		ret = 0;
		goto exit_err;
	}

	ret = rdmsrq_on_cpu(cpudata->cpu, MSR_K7_HWCR, &boost_val);
	if (ret) {
		pr_err_once("failed to read initial CPU boost state!\n");
		ret = -EIO;
		goto exit_err;
	}

	if (!(boost_val & MSR_K7_HWCR_CPB_DIS))
		cpudata->boost_supported = true;

	return 0;

exit_err:
	cpudata->boost_supported = false;
	return ret;
}

static void amd_perf_ctl_reset(unsigned int cpu)
{
	wrmsrq_on_cpu(cpu, MSR_AMD_PERF_CTL, 0);
}

#define CPPC_MAX_PERF	U8_MAX

static void amd_pstate_init_prefcore(struct amd_cpudata *cpudata)
{
	/* user disabled or not detected */
	if (!amd_pstate_prefcore)
		return;

	/* should use amd-hfi instead */
	if (cpu_feature_enabled(X86_FEATURE_AMD_WORKLOAD_CLASS) &&
	    IS_ENABLED(CONFIG_AMD_HFI)) {
		amd_pstate_prefcore = false;
		return;
	}

	cpudata->hw_prefcore = true;

	/* Priorities must be initialized before ITMT support can be toggled on. */
	sched_set_itmt_core_prio((int)READ_ONCE(cpudata->prefcore_ranking), cpudata->cpu);
}

static void amd_pstate_update_limits(struct cpufreq_policy *policy)
{
	struct amd_cpudata *cpudata;
	u32 prev_high = 0, cur_high = 0;
	bool highest_perf_changed = false;
	unsigned int cpu = policy->cpu;

	if (!amd_pstate_prefcore)
		return;

	if (amd_get_highest_perf(cpu, &cur_high))
		return;

	cpudata = policy->driver_data;

	prev_high = READ_ONCE(cpudata->prefcore_ranking);
	highest_perf_changed = (prev_high != cur_high);
	if (highest_perf_changed) {
		WRITE_ONCE(cpudata->prefcore_ranking, cur_high);

		if (cur_high < CPPC_MAX_PERF) {
			sched_set_itmt_core_prio((int)cur_high, cpu);
			sched_update_asym_prefer_cpu(cpu, prev_high, cur_high);
		}
	}
}

/*
 * Get pstate transition delay time from ACPI tables that firmware set
 * instead of using hardcode value directly.
 */
static u32 amd_pstate_get_transition_delay_us(unsigned int cpu)
{
	int transition_delay_ns;

	transition_delay_ns = cppc_get_transition_latency(cpu);
	if (transition_delay_ns < 0) {
		if (cpu_feature_enabled(X86_FEATURE_AMD_FAST_CPPC))
			return AMD_PSTATE_FAST_CPPC_TRANSITION_DELAY;
		else
			return AMD_PSTATE_TRANSITION_DELAY;
	}

	return transition_delay_ns / NSEC_PER_USEC;
}

/*
 * Get pstate transition latency value from ACPI tables that firmware
 * set instead of using hardcode value directly.
 */
static u32 amd_pstate_get_transition_latency(unsigned int cpu)
{
	int transition_latency;

	transition_latency = cppc_get_transition_latency(cpu);
	if (transition_latency < 0)
		return AMD_PSTATE_TRANSITION_LATENCY;

	return transition_latency;
}

/*
 * amd_pstate_init_freq: Initialize the nominal_freq and lowest_nonlinear_freq
 *			 for the @cpudata object.
 *
 * Requires: all perf members of @cpudata to be initialized.
 *
 * Returns 0 on success, non-zero value on failure.
 */
static int amd_pstate_init_freq(struct amd_cpudata *cpudata)
{
	u32 min_freq, max_freq, nominal_freq, lowest_nonlinear_freq;
	struct cppc_perf_caps cppc_perf;
	union perf_cached perf;
	int ret;

	ret = cppc_get_perf_caps(cpudata->cpu, &cppc_perf);
	if (ret)
		return ret;
	perf = READ_ONCE(cpudata->perf);

	if (quirks && quirks->nominal_freq)
		nominal_freq = quirks->nominal_freq;
	else
		nominal_freq = cppc_perf.nominal_freq;
	nominal_freq *= 1000;

	if (quirks && quirks->lowest_freq) {
		min_freq = quirks->lowest_freq;
		perf.lowest_perf = freq_to_perf(perf, nominal_freq, min_freq);
		WRITE_ONCE(cpudata->perf, perf);
	} else
		min_freq = cppc_perf.lowest_freq;

	min_freq *= 1000;

	WRITE_ONCE(cpudata->nominal_freq, nominal_freq);

	max_freq = perf_to_freq(perf, nominal_freq, perf.highest_perf);
	lowest_nonlinear_freq = perf_to_freq(perf, nominal_freq, perf.lowest_nonlinear_perf);
	WRITE_ONCE(cpudata->lowest_nonlinear_freq, lowest_nonlinear_freq);

	/**
	 * Below values need to be initialized correctly, otherwise driver will fail to load
	 * max_freq is calculated according to (nominal_freq * highest_perf)/nominal_perf
	 * lowest_nonlinear_freq is a value between [min_freq, nominal_freq]
	 * Check _CPC in ACPI table objects if any values are incorrect
	 */
	if (min_freq <= 0 || max_freq <= 0 || nominal_freq <= 0 || min_freq > max_freq) {
		pr_err("min_freq(%d) or max_freq(%d) or nominal_freq(%d) value is incorrect\n",
			min_freq, max_freq, nominal_freq);
		return -EINVAL;
	}

	if (lowest_nonlinear_freq <= min_freq || lowest_nonlinear_freq > nominal_freq) {
		pr_err("lowest_nonlinear_freq(%d) value is out of range [min_freq(%d), nominal_freq(%d)]\n",
			lowest_nonlinear_freq, min_freq, nominal_freq);
		return -EINVAL;
	}

	return 0;
}

static int amd_pstate_cpu_init(struct cpufreq_policy *policy)
{
	struct amd_cpudata *cpudata;
	union perf_cached perf;
	struct device *dev;
	int ret;

	/*
	 * Resetting PERF_CTL_MSR will put the CPU in P0 frequency,
	 * which is ideal for initialization process.
	 */
	amd_perf_ctl_reset(policy->cpu);
	dev = get_cpu_device(policy->cpu);
	if (!dev)
		return -ENODEV;

	cpudata = kzalloc(sizeof(*cpudata), GFP_KERNEL);
	if (!cpudata)
		return -ENOMEM;

	cpudata->cpu = policy->cpu;

	ret = amd_pstate_init_perf(cpudata);
	if (ret)
		goto free_cpudata1;

	amd_pstate_init_prefcore(cpudata);

	ret = amd_pstate_init_freq(cpudata);
	if (ret)
		goto free_cpudata1;

	ret = amd_pstate_init_boost_support(cpudata);
	if (ret)
		goto free_cpudata1;

	policy->cpuinfo.transition_latency = amd_pstate_get_transition_latency(policy->cpu);
	policy->transition_delay_us = amd_pstate_get_transition_delay_us(policy->cpu);

	perf = READ_ONCE(cpudata->perf);

	policy->cpuinfo.min_freq = policy->min = perf_to_freq(perf,
							      cpudata->nominal_freq,
							      perf.lowest_perf);
	policy->cpuinfo.max_freq = policy->max = perf_to_freq(perf,
							      cpudata->nominal_freq,
							      perf.highest_perf);

	ret = amd_pstate_cppc_enable(policy);
	if (ret)
		goto free_cpudata1;

	policy->boost_supported = READ_ONCE(cpudata->boost_supported);

	/* It will be updated by governor */
	policy->cur = policy->cpuinfo.min_freq;

	if (cpu_feature_enabled(X86_FEATURE_CPPC))
		policy->fast_switch_possible = true;

	ret = freq_qos_add_request(&policy->constraints, &cpudata->req[0],
				   FREQ_QOS_MIN, FREQ_QOS_MIN_DEFAULT_VALUE);
	if (ret < 0) {
		dev_err(dev, "Failed to add min-freq constraint (%d)\n", ret);
		goto free_cpudata1;
	}

	ret = freq_qos_add_request(&policy->constraints, &cpudata->req[1],
				   FREQ_QOS_MAX, policy->cpuinfo.max_freq);
	if (ret < 0) {
		dev_err(dev, "Failed to add max-freq constraint (%d)\n", ret);
		goto free_cpudata2;
	}

	policy->driver_data = cpudata;

	if (!current_pstate_driver->adjust_perf)
		current_pstate_driver->adjust_perf = amd_pstate_adjust_perf;

	return 0;

free_cpudata2:
	freq_qos_remove_request(&cpudata->req[0]);
free_cpudata1:
	pr_warn("Failed to initialize CPU %d: %d\n", policy->cpu, ret);
	kfree(cpudata);
	return ret;
}

static void amd_pstate_cpu_exit(struct cpufreq_policy *policy)
{
	struct amd_cpudata *cpudata = policy->driver_data;
	union perf_cached perf = READ_ONCE(cpudata->perf);

	/* Reset CPPC_REQ MSR to the BIOS value */
	amd_pstate_update_perf(policy, perf.bios_min_perf, 0U, 0U, 0U, false);

	freq_qos_remove_request(&cpudata->req[1]);
	freq_qos_remove_request(&cpudata->req[0]);
	policy->fast_switch_possible = false;
	kfree(cpudata);
}

/* Sysfs attributes */

/*
 * This frequency is to indicate the maximum hardware frequency.
 * If boost is not active but supported, the frequency will be larger than the
 * one in cpuinfo.
 */
static ssize_t show_amd_pstate_max_freq(struct cpufreq_policy *policy,
					char *buf)
{
	struct amd_cpudata *cpudata;
	union perf_cached perf;

	cpudata = policy->driver_data;
	perf = READ_ONCE(cpudata->perf);

	return sysfs_emit(buf, "%u\n",
			  perf_to_freq(perf, cpudata->nominal_freq, perf.highest_perf));
}

static ssize_t show_amd_pstate_lowest_nonlinear_freq(struct cpufreq_policy *policy,
						     char *buf)
{
	struct amd_cpudata *cpudata;
	union perf_cached perf;

	cpudata = policy->driver_data;
	perf = READ_ONCE(cpudata->perf);

	return sysfs_emit(buf, "%u\n",
			  perf_to_freq(perf, cpudata->nominal_freq, perf.lowest_nonlinear_perf));
}

/*
 * In some of ASICs, the highest_perf is not the one in the _CPC table, so we
 * need to expose it to sysfs.
 */
static ssize_t show_amd_pstate_highest_perf(struct cpufreq_policy *policy,
					    char *buf)
{
	struct amd_cpudata *cpudata;

	cpudata = policy->driver_data;

	return sysfs_emit(buf, "%u\n", cpudata->perf.highest_perf);
}

static ssize_t show_amd_pstate_prefcore_ranking(struct cpufreq_policy *policy,
						char *buf)
{
	u8 perf;
	struct amd_cpudata *cpudata = policy->driver_data;

	perf = READ_ONCE(cpudata->prefcore_ranking);

	return sysfs_emit(buf, "%u\n", perf);
}

static ssize_t show_amd_pstate_hw_prefcore(struct cpufreq_policy *policy,
					   char *buf)
{
	bool hw_prefcore;
	struct amd_cpudata *cpudata = policy->driver_data;

	hw_prefcore = READ_ONCE(cpudata->hw_prefcore);

	return sysfs_emit(buf, "%s\n", str_enabled_disabled(hw_prefcore));
}

static ssize_t show_energy_performance_available_preferences(
				struct cpufreq_policy *policy, char *buf)
{
	int i = 0;
	int offset = 0;
	struct amd_cpudata *cpudata = policy->driver_data;

	if (cpudata->policy == CPUFREQ_POLICY_PERFORMANCE)
		return sysfs_emit_at(buf, offset, "%s\n",
				energy_perf_strings[EPP_INDEX_PERFORMANCE]);

	while (energy_perf_strings[i] != NULL)
		offset += sysfs_emit_at(buf, offset, "%s ", energy_perf_strings[i++]);

	offset += sysfs_emit_at(buf, offset, "\n");

	return offset;
}

static ssize_t store_energy_performance_preference(
		struct cpufreq_policy *policy, const char *buf, size_t count)
{
	struct amd_cpudata *cpudata = policy->driver_data;
	char str_preference[21];
	ssize_t ret;
	u8 epp;

	ret = sscanf(buf, "%20s", str_preference);
	if (ret != 1)
		return -EINVAL;

	ret = match_string(energy_perf_strings, -1, str_preference);
	if (ret < 0)
		return -EINVAL;

	if (!ret)
		epp = cpudata->epp_default;
	else
		epp = epp_values[ret];

	if (epp > 0 && policy->policy == CPUFREQ_POLICY_PERFORMANCE) {
		pr_debug("EPP cannot be set under performance policy\n");
		return -EBUSY;
	}

	ret = amd_pstate_set_epp(policy, epp);

	return ret ? ret : count;
}

static ssize_t show_energy_performance_preference(
				struct cpufreq_policy *policy, char *buf)
{
	struct amd_cpudata *cpudata = policy->driver_data;
	u8 preference, epp;

	epp = FIELD_GET(AMD_CPPC_EPP_PERF_MASK, cpudata->cppc_req_cached);

	switch (epp) {
	case AMD_CPPC_EPP_PERFORMANCE:
		preference = EPP_INDEX_PERFORMANCE;
		break;
	case AMD_CPPC_EPP_BALANCE_PERFORMANCE:
		preference = EPP_INDEX_BALANCE_PERFORMANCE;
		break;
	case AMD_CPPC_EPP_BALANCE_POWERSAVE:
		preference = EPP_INDEX_BALANCE_POWERSAVE;
		break;
	case AMD_CPPC_EPP_POWERSAVE:
		preference = EPP_INDEX_POWERSAVE;
		break;
	default:
		return -EINVAL;
	}

	return sysfs_emit(buf, "%s\n", energy_perf_strings[preference]);
}

static void amd_pstate_driver_cleanup(void)
{
	if (amd_pstate_prefcore)
		sched_clear_itmt_support();

	cppc_state = AMD_PSTATE_DISABLE;
	current_pstate_driver = NULL;
}

static int amd_pstate_set_driver(int mode_idx)
{
	if (mode_idx >= AMD_PSTATE_DISABLE && mode_idx < AMD_PSTATE_MAX) {
		cppc_state = mode_idx;
		if (cppc_state == AMD_PSTATE_DISABLE)
			pr_info("driver is explicitly disabled\n");

		if (cppc_state == AMD_PSTATE_ACTIVE)
			current_pstate_driver = &amd_pstate_epp_driver;

		if (cppc_state == AMD_PSTATE_PASSIVE || cppc_state == AMD_PSTATE_GUIDED)
			current_pstate_driver = &amd_pstate_driver;

		return 0;
	}

	return -EINVAL;
}

static int amd_pstate_register_driver(int mode)
{
	int ret;

	ret = amd_pstate_set_driver(mode);
	if (ret)
		return ret;

	cppc_state = mode;

	/* at least one CPU supports CPB */
	current_pstate_driver->boost_enabled = cpu_feature_enabled(X86_FEATURE_CPB);

	ret = cpufreq_register_driver(current_pstate_driver);
	if (ret) {
		amd_pstate_driver_cleanup();
		return ret;
	}

	/* Enable ITMT support once all CPUs have initialized their asym priorities. */
	if (amd_pstate_prefcore)
		sched_set_itmt_support();

	return 0;
}

static int amd_pstate_unregister_driver(int dummy)
{
	cpufreq_unregister_driver(current_pstate_driver);
	amd_pstate_driver_cleanup();
	return 0;
}

static int amd_pstate_change_mode_without_dvr_change(int mode)
{
	int cpu = 0;

	cppc_state = mode;

	if (cpu_feature_enabled(X86_FEATURE_CPPC) || cppc_state == AMD_PSTATE_ACTIVE)
		return 0;

	for_each_present_cpu(cpu) {
		cppc_set_auto_sel(cpu, (cppc_state == AMD_PSTATE_PASSIVE) ? 0 : 1);
	}

	return 0;
}

static int amd_pstate_change_driver_mode(int mode)
{
	int ret;

	ret = amd_pstate_unregister_driver(0);
	if (ret)
		return ret;

	ret = amd_pstate_register_driver(mode);
	if (ret)
		return ret;

	return 0;
}

static cppc_mode_transition_fn mode_state_machine[AMD_PSTATE_MAX][AMD_PSTATE_MAX] = {
	[AMD_PSTATE_DISABLE]         = {
		[AMD_PSTATE_DISABLE]     = NULL,
		[AMD_PSTATE_PASSIVE]     = amd_pstate_register_driver,
		[AMD_PSTATE_ACTIVE]      = amd_pstate_register_driver,
		[AMD_PSTATE_GUIDED]      = amd_pstate_register_driver,
	},
	[AMD_PSTATE_PASSIVE]         = {
		[AMD_PSTATE_DISABLE]     = amd_pstate_unregister_driver,
		[AMD_PSTATE_PASSIVE]     = NULL,
		[AMD_PSTATE_ACTIVE]      = amd_pstate_change_driver_mode,
		[AMD_PSTATE_GUIDED]      = amd_pstate_change_mode_without_dvr_change,
	},
	[AMD_PSTATE_ACTIVE]          = {
		[AMD_PSTATE_DISABLE]     = amd_pstate_unregister_driver,
		[AMD_PSTATE_PASSIVE]     = amd_pstate_change_driver_mode,
		[AMD_PSTATE_ACTIVE]      = NULL,
		[AMD_PSTATE_GUIDED]      = amd_pstate_change_driver_mode,
	},
	[AMD_PSTATE_GUIDED]          = {
		[AMD_PSTATE_DISABLE]     = amd_pstate_unregister_driver,
		[AMD_PSTATE_PASSIVE]     = amd_pstate_change_mode_without_dvr_change,
		[AMD_PSTATE_ACTIVE]      = amd_pstate_change_driver_mode,
		[AMD_PSTATE_GUIDED]      = NULL,
	},
};

static ssize_t amd_pstate_show_status(char *buf)
{
	if (!current_pstate_driver)
		return sysfs_emit(buf, "disable\n");

	return sysfs_emit(buf, "%s\n", amd_pstate_mode_string[cppc_state]);
}

int amd_pstate_get_status(void)
{
	return cppc_state;
}
EXPORT_SYMBOL_GPL(amd_pstate_get_status);

int amd_pstate_update_status(const char *buf, size_t size)
{
	int mode_idx;

	if (size > strlen("passive") || size < strlen("active"))
		return -EINVAL;

	mode_idx = get_mode_idx_from_str(buf, size);

	if (mode_idx < 0 || mode_idx >= AMD_PSTATE_MAX)
		return -EINVAL;

	if (mode_state_machine[cppc_state][mode_idx]) {
		guard(mutex)(&amd_pstate_driver_lock);
		return mode_state_machine[cppc_state][mode_idx](mode_idx);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(amd_pstate_update_status);

static ssize_t status_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{

	guard(mutex)(&amd_pstate_driver_lock);

	return amd_pstate_show_status(buf);
}

static ssize_t status_store(struct device *a, struct device_attribute *b,
			    const char *buf, size_t count)
{
	char *p = memchr(buf, '\n', count);
	int ret;

	ret = amd_pstate_update_status(buf, p ? p - buf : count);

	return ret < 0 ? ret : count;
}

static ssize_t prefcore_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n", str_enabled_disabled(amd_pstate_prefcore));
}

cpufreq_freq_attr_ro(amd_pstate_max_freq);
cpufreq_freq_attr_ro(amd_pstate_lowest_nonlinear_freq);

cpufreq_freq_attr_ro(amd_pstate_highest_perf);
cpufreq_freq_attr_ro(amd_pstate_prefcore_ranking);
cpufreq_freq_attr_ro(amd_pstate_hw_prefcore);
cpufreq_freq_attr_rw(energy_performance_preference);
cpufreq_freq_attr_ro(energy_performance_available_preferences);
static DEVICE_ATTR_RW(status);
static DEVICE_ATTR_RO(prefcore);

static struct freq_attr *amd_pstate_attr[] = {
	&amd_pstate_max_freq,
	&amd_pstate_lowest_nonlinear_freq,
	&amd_pstate_highest_perf,
	&amd_pstate_prefcore_ranking,
	&amd_pstate_hw_prefcore,
	NULL,
};

static struct freq_attr *amd_pstate_epp_attr[] = {
	&amd_pstate_max_freq,
	&amd_pstate_lowest_nonlinear_freq,
	&amd_pstate_highest_perf,
	&amd_pstate_prefcore_ranking,
	&amd_pstate_hw_prefcore,
	&energy_performance_preference,
	&energy_performance_available_preferences,
	NULL,
};

static struct attribute *pstate_global_attributes[] = {
	&dev_attr_status.attr,
	&dev_attr_prefcore.attr,
	NULL
};

static const struct attribute_group amd_pstate_global_attr_group = {
	.name = "amd_pstate",
	.attrs = pstate_global_attributes,
};

static bool amd_pstate_acpi_pm_profile_server(void)
{
	switch (acpi_gbl_FADT.preferred_profile) {
	case PM_ENTERPRISE_SERVER:
	case PM_SOHO_SERVER:
	case PM_PERFORMANCE_SERVER:
		return true;
	}
	return false;
}

static bool amd_pstate_acpi_pm_profile_undefined(void)
{
	if (acpi_gbl_FADT.preferred_profile == PM_UNSPECIFIED)
		return true;
	if (acpi_gbl_FADT.preferred_profile >= NR_PM_PROFILES)
		return true;
	return false;
}

static int amd_pstate_epp_cpu_init(struct cpufreq_policy *policy)
{
	struct amd_cpudata *cpudata;
	union perf_cached perf;
	struct device *dev;
	int ret;

	/*
	 * Resetting PERF_CTL_MSR will put the CPU in P0 frequency,
	 * which is ideal for initialization process.
	 */
	amd_perf_ctl_reset(policy->cpu);
	dev = get_cpu_device(policy->cpu);
	if (!dev)
		return -ENODEV;

	cpudata = kzalloc(sizeof(*cpudata), GFP_KERNEL);
	if (!cpudata)
		return -ENOMEM;

	cpudata->cpu = policy->cpu;

	ret = amd_pstate_init_perf(cpudata);
	if (ret)
		goto free_cpudata1;

	amd_pstate_init_prefcore(cpudata);

	ret = amd_pstate_init_freq(cpudata);
	if (ret)
		goto free_cpudata1;

	ret = amd_pstate_init_boost_support(cpudata);
	if (ret)
		goto free_cpudata1;

	perf = READ_ONCE(cpudata->perf);

	policy->cpuinfo.min_freq = policy->min = perf_to_freq(perf,
							      cpudata->nominal_freq,
							      perf.lowest_perf);
	policy->cpuinfo.max_freq = policy->max = perf_to_freq(perf,
							      cpudata->nominal_freq,
							      perf.highest_perf);
	policy->driver_data = cpudata;

	ret = amd_pstate_cppc_enable(policy);
	if (ret)
		goto free_cpudata1;

	/* It will be updated by governor */
	policy->cur = policy->cpuinfo.min_freq;


	policy->boost_supported = READ_ONCE(cpudata->boost_supported);

	/*
	 * Set the policy to provide a valid fallback value in case
	 * the default cpufreq governor is neither powersave nor performance.
	 */
	if (amd_pstate_acpi_pm_profile_server() ||
	    amd_pstate_acpi_pm_profile_undefined()) {
		policy->policy = CPUFREQ_POLICY_PERFORMANCE;
		cpudata->epp_default = amd_pstate_get_epp(cpudata);
	} else {
		policy->policy = CPUFREQ_POLICY_POWERSAVE;
		cpudata->epp_default = AMD_CPPC_EPP_BALANCE_PERFORMANCE;
	}

	ret = amd_pstate_set_epp(policy, cpudata->epp_default);
	if (ret)
		return ret;

	current_pstate_driver->adjust_perf = NULL;

	return 0;

free_cpudata1:
	pr_warn("Failed to initialize CPU %d: %d\n", policy->cpu, ret);
	kfree(cpudata);
	return ret;
}

static void amd_pstate_epp_cpu_exit(struct cpufreq_policy *policy)
{
	struct amd_cpudata *cpudata = policy->driver_data;

	if (cpudata) {
		union perf_cached perf = READ_ONCE(cpudata->perf);

		/* Reset CPPC_REQ MSR to the BIOS value */
		amd_pstate_update_perf(policy, perf.bios_min_perf, 0U, 0U, 0U, false);

		kfree(cpudata);
		policy->driver_data = NULL;
	}

	pr_debug("CPU %d exiting\n", policy->cpu);
}

static int amd_pstate_epp_update_limit(struct cpufreq_policy *policy, bool policy_change)
{
	struct amd_cpudata *cpudata = policy->driver_data;
	union perf_cached perf;
	u8 epp;

	if (policy_change ||
	    policy->min != cpudata->min_limit_freq ||
	    policy->max != cpudata->max_limit_freq)
		amd_pstate_update_min_max_limit(policy);

	if (cpudata->policy == CPUFREQ_POLICY_PERFORMANCE)
		epp = 0;
	else
		epp = FIELD_GET(AMD_CPPC_EPP_PERF_MASK, cpudata->cppc_req_cached);

	perf = READ_ONCE(cpudata->perf);

	return amd_pstate_update_perf(policy, perf.min_limit_perf, 0U,
				      perf.max_limit_perf, epp, false);
}

static int amd_pstate_epp_set_policy(struct cpufreq_policy *policy)
{
	struct amd_cpudata *cpudata = policy->driver_data;
	int ret;

	if (!policy->cpuinfo.max_freq)
		return -ENODEV;

	cpudata->policy = policy->policy;

	ret = amd_pstate_epp_update_limit(policy, true);
	if (ret)
		return ret;

	/*
	 * policy->cur is never updated with the amd_pstate_epp driver, but it
	 * is used as a stale frequency value. So, keep it within limits.
	 */
	policy->cur = policy->min;

	return 0;
}

static int amd_pstate_cpu_online(struct cpufreq_policy *policy)
{
	return amd_pstate_cppc_enable(policy);
}

static int amd_pstate_cpu_offline(struct cpufreq_policy *policy)
{
	struct amd_cpudata *cpudata = policy->driver_data;
	union perf_cached perf = READ_ONCE(cpudata->perf);

	/*
	 * Reset CPPC_REQ MSR to the BIOS value, this will allow us to retain the BIOS specified
	 * min_perf value across kexec reboots. If this CPU is just onlined normally after this, the
	 * limits, epp and desired perf will get reset to the cached values in cpudata struct
	 */
	return amd_pstate_update_perf(policy, perf.bios_min_perf,
				     FIELD_GET(AMD_CPPC_DES_PERF_MASK, cpudata->cppc_req_cached),
				     FIELD_GET(AMD_CPPC_MAX_PERF_MASK, cpudata->cppc_req_cached),
				     FIELD_GET(AMD_CPPC_EPP_PERF_MASK, cpudata->cppc_req_cached),
				     false);
}

static int amd_pstate_suspend(struct cpufreq_policy *policy)
{
	struct amd_cpudata *cpudata = policy->driver_data;
	union perf_cached perf = READ_ONCE(cpudata->perf);
	int ret;

	/*
	 * Reset CPPC_REQ MSR to the BIOS value, this will allow us to retain the BIOS specified
	 * min_perf value across kexec reboots. If this CPU is just resumed back without kexec,
	 * the limits, epp and desired perf will get reset to the cached values in cpudata struct
	 */
	ret = amd_pstate_update_perf(policy, perf.bios_min_perf,
				     FIELD_GET(AMD_CPPC_DES_PERF_MASK, cpudata->cppc_req_cached),
				     FIELD_GET(AMD_CPPC_MAX_PERF_MASK, cpudata->cppc_req_cached),
				     FIELD_GET(AMD_CPPC_EPP_PERF_MASK, cpudata->cppc_req_cached),
				     false);
	if (ret)
		return ret;

	/* set this flag to avoid setting core offline*/
	cpudata->suspended = true;

	return 0;
}

static int amd_pstate_resume(struct cpufreq_policy *policy)
{
	struct amd_cpudata *cpudata = policy->driver_data;
	union perf_cached perf = READ_ONCE(cpudata->perf);
	int cur_perf = freq_to_perf(perf, cpudata->nominal_freq, policy->cur);

	/* Set CPPC_REQ to last sane value until the governor updates it */
	return amd_pstate_update_perf(policy, perf.min_limit_perf, cur_perf, perf.max_limit_perf,
				      0U, false);
}

static int amd_pstate_epp_resume(struct cpufreq_policy *policy)
{
	struct amd_cpudata *cpudata = policy->driver_data;

	if (cpudata->suspended) {
		int ret;

		/* enable amd pstate from suspend state*/
		ret = amd_pstate_epp_update_limit(policy, false);
		if (ret)
			return ret;

		cpudata->suspended = false;
	}

	return 0;
}

static struct cpufreq_driver amd_pstate_driver = {
	.flags		= CPUFREQ_CONST_LOOPS | CPUFREQ_NEED_UPDATE_LIMITS,
	.verify		= amd_pstate_verify,
	.target		= amd_pstate_target,
	.fast_switch    = amd_pstate_fast_switch,
	.init		= amd_pstate_cpu_init,
	.exit		= amd_pstate_cpu_exit,
	.online		= amd_pstate_cpu_online,
	.offline	= amd_pstate_cpu_offline,
	.suspend	= amd_pstate_suspend,
	.resume		= amd_pstate_resume,
	.set_boost	= amd_pstate_set_boost,
	.update_limits	= amd_pstate_update_limits,
	.name		= "amd-pstate",
	.attr		= amd_pstate_attr,
};

static struct cpufreq_driver amd_pstate_epp_driver = {
	.flags		= CPUFREQ_CONST_LOOPS,
	.verify		= amd_pstate_verify,
	.setpolicy	= amd_pstate_epp_set_policy,
	.init		= amd_pstate_epp_cpu_init,
	.exit		= amd_pstate_epp_cpu_exit,
	.offline	= amd_pstate_cpu_offline,
	.online		= amd_pstate_cpu_online,
	.suspend	= amd_pstate_suspend,
	.resume		= amd_pstate_epp_resume,
	.update_limits	= amd_pstate_update_limits,
	.set_boost	= amd_pstate_set_boost,
	.name		= "amd-pstate-epp",
	.attr		= amd_pstate_epp_attr,
};

/*
 * CPPC function is not supported for family ID 17H with model_ID ranging from 0x10 to 0x2F.
 * show the debug message that helps to check if the CPU has CPPC support for loading issue.
 */
static bool amd_cppc_supported(void)
{
	struct cpuinfo_x86 *c = &cpu_data(0);
	bool warn = false;

	if ((boot_cpu_data.x86 == 0x17) && (boot_cpu_data.x86_model < 0x30)) {
		pr_debug_once("CPPC feature is not supported by the processor\n");
		return false;
	}

	/*
	 * If the CPPC feature is disabled in the BIOS for processors
	 * that support MSR-based CPPC, the AMD Pstate driver may not
	 * function correctly.
	 *
	 * For such processors, check the CPPC flag and display a
	 * warning message if the platform supports CPPC.
	 *
	 * Note: The code check below will not abort the driver
	 * registration process because of the code is added for
	 * debugging purposes. Besides, it may still be possible for
	 * the driver to work using the shared-memory mechanism.
	 */
	if (!cpu_feature_enabled(X86_FEATURE_CPPC)) {
		if (cpu_feature_enabled(X86_FEATURE_ZEN2)) {
			switch (c->x86_model) {
			case 0x60 ... 0x6F:
			case 0x80 ... 0xAF:
				warn = true;
				break;
			}
		} else if (cpu_feature_enabled(X86_FEATURE_ZEN3) ||
			   cpu_feature_enabled(X86_FEATURE_ZEN4)) {
			switch (c->x86_model) {
			case 0x10 ... 0x1F:
			case 0x40 ... 0xAF:
				warn = true;
				break;
			}
		} else if (cpu_feature_enabled(X86_FEATURE_ZEN5)) {
			warn = true;
		}
	}

	if (warn)
		pr_warn_once("The CPPC feature is supported but currently disabled by the BIOS.\n"
					"Please enable it if your BIOS has the CPPC option.\n");
	return true;
}

static int __init amd_pstate_init(void)
{
	struct device *dev_root;
	int ret;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD)
		return -ENODEV;

	/* show debug message only if CPPC is not supported */
	if (!amd_cppc_supported())
		return -EOPNOTSUPP;

	/* show warning message when BIOS broken or ACPI disabled */
	if (!acpi_cpc_valid()) {
		pr_warn_once("the _CPC object is not present in SBIOS or ACPI disabled\n");
		return -ENODEV;
	}

	/* don't keep reloading if cpufreq_driver exists */
	if (cpufreq_get_current_driver())
		return -EEXIST;

	quirks = NULL;

	/* check if this machine need CPPC quirks */
	dmi_check_system(amd_pstate_quirks_table);

	/*
	* determine the driver mode from the command line or kernel config.
	* If no command line input is provided, cppc_state will be AMD_PSTATE_UNDEFINED.
	* command line options will override the kernel config settings.
	*/

	if (cppc_state == AMD_PSTATE_UNDEFINED) {
		/* Disable on the following configs by default:
		 * 1. Undefined platforms
		 * 2. Server platforms with CPUs older than Family 0x1A.
		 */
		if (amd_pstate_acpi_pm_profile_undefined() ||
		    (amd_pstate_acpi_pm_profile_server() && boot_cpu_data.x86 < 0x1A)) {
			pr_info("driver load is disabled, boot with specific mode to enable this\n");
			return -ENODEV;
		}
		/* get driver mode from kernel config option [1:4] */
		cppc_state = CONFIG_X86_AMD_PSTATE_DEFAULT_MODE;
	}

	if (cppc_state == AMD_PSTATE_DISABLE) {
		pr_info("driver load is disabled, boot with specific mode to enable this\n");
		return -ENODEV;
	}

	/* capability check */
	if (cpu_feature_enabled(X86_FEATURE_CPPC)) {
		pr_debug("AMD CPPC MSR based functionality is supported\n");
	} else {
		pr_debug("AMD CPPC shared memory based functionality is supported\n");
		static_call_update(amd_pstate_cppc_enable, shmem_cppc_enable);
		static_call_update(amd_pstate_init_perf, shmem_init_perf);
		static_call_update(amd_pstate_update_perf, shmem_update_perf);
		static_call_update(amd_pstate_get_epp, shmem_get_epp);
		static_call_update(amd_pstate_set_epp, shmem_set_epp);
	}

	if (amd_pstate_prefcore) {
		ret = amd_detect_prefcore(&amd_pstate_prefcore);
		if (ret)
			return ret;
	}

	ret = amd_pstate_register_driver(cppc_state);
	if (ret) {
		pr_err("failed to register with return %d\n", ret);
		return ret;
	}

	dev_root = bus_get_dev_root(&cpu_subsys);
	if (dev_root) {
		ret = sysfs_create_group(&dev_root->kobj, &amd_pstate_global_attr_group);
		put_device(dev_root);
		if (ret) {
			pr_err("sysfs attribute export failed with error %d.\n", ret);
			goto global_attr_free;
		}
	}

	return ret;

global_attr_free:
	cpufreq_unregister_driver(current_pstate_driver);
	return ret;
}
device_initcall(amd_pstate_init);

static int __init amd_pstate_param(char *str)
{
	size_t size;
	int mode_idx;

	if (!str)
		return -EINVAL;

	size = strlen(str);
	mode_idx = get_mode_idx_from_str(str, size);

	return amd_pstate_set_driver(mode_idx);
}

static int __init amd_prefcore_param(char *str)
{
	if (!strcmp(str, "disable"))
		amd_pstate_prefcore = false;

	return 0;
}

early_param("amd_pstate", amd_pstate_param);
early_param("amd_prefcore", amd_prefcore_param);

MODULE_AUTHOR("Huang Rui <ray.huang@amd.com>");
MODULE_DESCRIPTION("AMD Processor P-state Frequency Driver");
