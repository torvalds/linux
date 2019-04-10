/*
 * intel_pstate.c: Native P state management for Intel processors
 *
 * (C) Copyright 2012 Intel Corporation
 * Author: Dirk Brandewie <dirk.j.brandewie@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/tick.h>
#include <linux/slab.h>
#include <linux/sched/cpufreq.h>
#include <linux/list.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/acpi.h>
#include <linux/vmalloc.h>
#include <trace/events/power.h>

#include <asm/div64.h>
#include <asm/msr.h>
#include <asm/cpu_device_id.h>
#include <asm/cpufeature.h>
#include <asm/intel-family.h>

#define INTEL_PSTATE_SAMPLING_INTERVAL	(10 * NSEC_PER_MSEC)

#define INTEL_CPUFREQ_TRANSITION_LATENCY	20000
#define INTEL_CPUFREQ_TRANSITION_DELAY		500

#ifdef CONFIG_ACPI
#include <acpi/processor.h>
#include <acpi/cppc_acpi.h>
#endif

#define FRAC_BITS 8
#define int_tofp(X) ((int64_t)(X) << FRAC_BITS)
#define fp_toint(X) ((X) >> FRAC_BITS)

#define ONE_EIGHTH_FP ((int64_t)1 << (FRAC_BITS - 3))

#define EXT_BITS 6
#define EXT_FRAC_BITS (EXT_BITS + FRAC_BITS)
#define fp_ext_toint(X) ((X) >> EXT_FRAC_BITS)
#define int_ext_tofp(X) ((int64_t)(X) << EXT_FRAC_BITS)

static inline int32_t mul_fp(int32_t x, int32_t y)
{
	return ((int64_t)x * (int64_t)y) >> FRAC_BITS;
}

static inline int32_t div_fp(s64 x, s64 y)
{
	return div64_s64((int64_t)x << FRAC_BITS, y);
}

static inline int ceiling_fp(int32_t x)
{
	int mask, ret;

	ret = fp_toint(x);
	mask = (1 << FRAC_BITS) - 1;
	if (x & mask)
		ret += 1;
	return ret;
}

static inline int32_t percent_fp(int percent)
{
	return div_fp(percent, 100);
}

static inline u64 mul_ext_fp(u64 x, u64 y)
{
	return (x * y) >> EXT_FRAC_BITS;
}

static inline u64 div_ext_fp(u64 x, u64 y)
{
	return div64_u64(x << EXT_FRAC_BITS, y);
}

static inline int32_t percent_ext_fp(int percent)
{
	return div_ext_fp(percent, 100);
}

/**
 * struct sample -	Store performance sample
 * @core_avg_perf:	Ratio of APERF/MPERF which is the actual average
 *			performance during last sample period
 * @busy_scaled:	Scaled busy value which is used to calculate next
 *			P state. This can be different than core_avg_perf
 *			to account for cpu idle period
 * @aperf:		Difference of actual performance frequency clock count
 *			read from APERF MSR between last and current sample
 * @mperf:		Difference of maximum performance frequency clock count
 *			read from MPERF MSR between last and current sample
 * @tsc:		Difference of time stamp counter between last and
 *			current sample
 * @time:		Current time from scheduler
 *
 * This structure is used in the cpudata structure to store performance sample
 * data for choosing next P State.
 */
struct sample {
	int32_t core_avg_perf;
	int32_t busy_scaled;
	u64 aperf;
	u64 mperf;
	u64 tsc;
	u64 time;
};

/**
 * struct pstate_data - Store P state data
 * @current_pstate:	Current requested P state
 * @min_pstate:		Min P state possible for this platform
 * @max_pstate:		Max P state possible for this platform
 * @max_pstate_physical:This is physical Max P state for a processor
 *			This can be higher than the max_pstate which can
 *			be limited by platform thermal design power limits
 * @scaling:		Scaling factor to  convert frequency to cpufreq
 *			frequency units
 * @turbo_pstate:	Max Turbo P state possible for this platform
 * @max_freq:		@max_pstate frequency in cpufreq units
 * @turbo_freq:		@turbo_pstate frequency in cpufreq units
 *
 * Stores the per cpu model P state limits and current P state.
 */
struct pstate_data {
	int	current_pstate;
	int	min_pstate;
	int	max_pstate;
	int	max_pstate_physical;
	int	scaling;
	int	turbo_pstate;
	unsigned int max_freq;
	unsigned int turbo_freq;
};

/**
 * struct vid_data -	Stores voltage information data
 * @min:		VID data for this platform corresponding to
 *			the lowest P state
 * @max:		VID data corresponding to the highest P State.
 * @turbo:		VID data for turbo P state
 * @ratio:		Ratio of (vid max - vid min) /
 *			(max P state - Min P State)
 *
 * Stores the voltage data for DVFS (Dynamic Voltage and Frequency Scaling)
 * This data is used in Atom platforms, where in addition to target P state,
 * the voltage data needs to be specified to select next P State.
 */
struct vid_data {
	int min;
	int max;
	int turbo;
	int32_t ratio;
};

/**
 * struct global_params - Global parameters, mostly tunable via sysfs.
 * @no_turbo:		Whether or not to use turbo P-states.
 * @turbo_disabled:	Whethet or not turbo P-states are available at all,
 *			based on the MSR_IA32_MISC_ENABLE value and whether or
 *			not the maximum reported turbo P-state is different from
 *			the maximum reported non-turbo one.
 * @min_perf_pct:	Minimum capacity limit in percent of the maximum turbo
 *			P-state capacity.
 * @max_perf_pct:	Maximum capacity limit in percent of the maximum turbo
 *			P-state capacity.
 */
struct global_params {
	bool no_turbo;
	bool turbo_disabled;
	int max_perf_pct;
	int min_perf_pct;
};

/**
 * struct cpudata -	Per CPU instance data storage
 * @cpu:		CPU number for this instance data
 * @policy:		CPUFreq policy value
 * @update_util:	CPUFreq utility callback information
 * @update_util_set:	CPUFreq utility callback is set
 * @iowait_boost:	iowait-related boost fraction
 * @last_update:	Time of the last update.
 * @pstate:		Stores P state limits for this CPU
 * @vid:		Stores VID limits for this CPU
 * @last_sample_time:	Last Sample time
 * @aperf_mperf_shift:	Number of clock cycles after aperf, merf is incremented
 *			This shift is a multiplier to mperf delta to
 *			calculate CPU busy.
 * @prev_aperf:		Last APERF value read from APERF MSR
 * @prev_mperf:		Last MPERF value read from MPERF MSR
 * @prev_tsc:		Last timestamp counter (TSC) value
 * @prev_cummulative_iowait: IO Wait time difference from last and
 *			current sample
 * @sample:		Storage for storing last Sample data
 * @min_perf_ratio:	Minimum capacity in terms of PERF or HWP ratios
 * @max_perf_ratio:	Maximum capacity in terms of PERF or HWP ratios
 * @acpi_perf_data:	Stores ACPI perf information read from _PSS
 * @valid_pss_table:	Set to true for valid ACPI _PSS entries found
 * @epp_powersave:	Last saved HWP energy performance preference
 *			(EPP) or energy performance bias (EPB),
 *			when policy switched to performance
 * @epp_policy:		Last saved policy used to set EPP/EPB
 * @epp_default:	Power on default HWP energy performance
 *			preference/bias
 * @epp_saved:		Saved EPP/EPB during system suspend or CPU offline
 *			operation
 * @hwp_req_cached:	Cached value of the last HWP Request MSR
 * @hwp_cap_cached:	Cached value of the last HWP Capabilities MSR
 * @last_io_update:	Last time when IO wake flag was set
 * @sched_flags:	Store scheduler flags for possible cross CPU update
 * @hwp_boost_min:	Last HWP boosted min performance
 *
 * This structure stores per CPU instance data for all CPUs.
 */
struct cpudata {
	int cpu;

	unsigned int policy;
	struct update_util_data update_util;
	bool   update_util_set;

	struct pstate_data pstate;
	struct vid_data vid;

	u64	last_update;
	u64	last_sample_time;
	u64	aperf_mperf_shift;
	u64	prev_aperf;
	u64	prev_mperf;
	u64	prev_tsc;
	u64	prev_cummulative_iowait;
	struct sample sample;
	int32_t	min_perf_ratio;
	int32_t	max_perf_ratio;
#ifdef CONFIG_ACPI
	struct acpi_processor_performance acpi_perf_data;
	bool valid_pss_table;
#endif
	unsigned int iowait_boost;
	s16 epp_powersave;
	s16 epp_policy;
	s16 epp_default;
	s16 epp_saved;
	u64 hwp_req_cached;
	u64 hwp_cap_cached;
	u64 last_io_update;
	unsigned int sched_flags;
	u32 hwp_boost_min;
};

static struct cpudata **all_cpu_data;

/**
 * struct pstate_funcs - Per CPU model specific callbacks
 * @get_max:		Callback to get maximum non turbo effective P state
 * @get_max_physical:	Callback to get maximum non turbo physical P state
 * @get_min:		Callback to get minimum P state
 * @get_turbo:		Callback to get turbo P state
 * @get_scaling:	Callback to get frequency scaling factor
 * @get_val:		Callback to convert P state to actual MSR write value
 * @get_vid:		Callback to get VID data for Atom platforms
 *
 * Core and Atom CPU models have different way to get P State limits. This
 * structure is used to store those callbacks.
 */
struct pstate_funcs {
	int (*get_max)(void);
	int (*get_max_physical)(void);
	int (*get_min)(void);
	int (*get_turbo)(void);
	int (*get_scaling)(void);
	int (*get_aperf_mperf_shift)(void);
	u64 (*get_val)(struct cpudata*, int pstate);
	void (*get_vid)(struct cpudata *);
};

static struct pstate_funcs pstate_funcs __read_mostly;

static int hwp_active __read_mostly;
static int hwp_mode_bdw __read_mostly;
static bool per_cpu_limits __read_mostly;
static bool hwp_boost __read_mostly;

static struct cpufreq_driver *intel_pstate_driver __read_mostly;

#ifdef CONFIG_ACPI
static bool acpi_ppc;
#endif

static struct global_params global;

static DEFINE_MUTEX(intel_pstate_driver_lock);
static DEFINE_MUTEX(intel_pstate_limits_lock);

#ifdef CONFIG_ACPI

static bool intel_pstate_acpi_pm_profile_server(void)
{
	if (acpi_gbl_FADT.preferred_profile == PM_ENTERPRISE_SERVER ||
	    acpi_gbl_FADT.preferred_profile == PM_PERFORMANCE_SERVER)
		return true;

	return false;
}

static bool intel_pstate_get_ppc_enable_status(void)
{
	if (intel_pstate_acpi_pm_profile_server())
		return true;

	return acpi_ppc;
}

#ifdef CONFIG_ACPI_CPPC_LIB

/* The work item is needed to avoid CPU hotplug locking issues */
static void intel_pstste_sched_itmt_work_fn(struct work_struct *work)
{
	sched_set_itmt_support();
}

static DECLARE_WORK(sched_itmt_work, intel_pstste_sched_itmt_work_fn);

static void intel_pstate_set_itmt_prio(int cpu)
{
	struct cppc_perf_caps cppc_perf;
	static u32 max_highest_perf = 0, min_highest_perf = U32_MAX;
	int ret;

	ret = cppc_get_perf_caps(cpu, &cppc_perf);
	if (ret)
		return;

	/*
	 * The priorities can be set regardless of whether or not
	 * sched_set_itmt_support(true) has been called and it is valid to
	 * update them at any time after it has been called.
	 */
	sched_set_itmt_core_prio(cppc_perf.highest_perf, cpu);

	if (max_highest_perf <= min_highest_perf) {
		if (cppc_perf.highest_perf > max_highest_perf)
			max_highest_perf = cppc_perf.highest_perf;

		if (cppc_perf.highest_perf < min_highest_perf)
			min_highest_perf = cppc_perf.highest_perf;

		if (max_highest_perf > min_highest_perf) {
			/*
			 * This code can be run during CPU online under the
			 * CPU hotplug locks, so sched_set_itmt_support()
			 * cannot be called from here.  Queue up a work item
			 * to invoke it.
			 */
			schedule_work(&sched_itmt_work);
		}
	}
}

static int intel_pstate_get_cppc_guranteed(int cpu)
{
	struct cppc_perf_caps cppc_perf;
	int ret;

	ret = cppc_get_perf_caps(cpu, &cppc_perf);
	if (ret)
		return ret;

	return cppc_perf.guaranteed_perf;
}

#else /* CONFIG_ACPI_CPPC_LIB */
static void intel_pstate_set_itmt_prio(int cpu)
{
}
#endif /* CONFIG_ACPI_CPPC_LIB */

static void intel_pstate_init_acpi_perf_limits(struct cpufreq_policy *policy)
{
	struct cpudata *cpu;
	int ret;
	int i;

	if (hwp_active) {
		intel_pstate_set_itmt_prio(policy->cpu);
		return;
	}

	if (!intel_pstate_get_ppc_enable_status())
		return;

	cpu = all_cpu_data[policy->cpu];

	ret = acpi_processor_register_performance(&cpu->acpi_perf_data,
						  policy->cpu);
	if (ret)
		return;

	/*
	 * Check if the control value in _PSS is for PERF_CTL MSR, which should
	 * guarantee that the states returned by it map to the states in our
	 * list directly.
	 */
	if (cpu->acpi_perf_data.control_register.space_id !=
						ACPI_ADR_SPACE_FIXED_HARDWARE)
		goto err;

	/*
	 * If there is only one entry _PSS, simply ignore _PSS and continue as
	 * usual without taking _PSS into account
	 */
	if (cpu->acpi_perf_data.state_count < 2)
		goto err;

	pr_debug("CPU%u - ACPI _PSS perf data\n", policy->cpu);
	for (i = 0; i < cpu->acpi_perf_data.state_count; i++) {
		pr_debug("     %cP%d: %u MHz, %u mW, 0x%x\n",
			 (i == cpu->acpi_perf_data.state ? '*' : ' '), i,
			 (u32) cpu->acpi_perf_data.states[i].core_frequency,
			 (u32) cpu->acpi_perf_data.states[i].power,
			 (u32) cpu->acpi_perf_data.states[i].control);
	}

	/*
	 * The _PSS table doesn't contain whole turbo frequency range.
	 * This just contains +1 MHZ above the max non turbo frequency,
	 * with control value corresponding to max turbo ratio. But
	 * when cpufreq set policy is called, it will call with this
	 * max frequency, which will cause a reduced performance as
	 * this driver uses real max turbo frequency as the max
	 * frequency. So correct this frequency in _PSS table to
	 * correct max turbo frequency based on the turbo state.
	 * Also need to convert to MHz as _PSS freq is in MHz.
	 */
	if (!global.turbo_disabled)
		cpu->acpi_perf_data.states[0].core_frequency =
					policy->cpuinfo.max_freq / 1000;
	cpu->valid_pss_table = true;
	pr_debug("_PPC limits will be enforced\n");

	return;

 err:
	cpu->valid_pss_table = false;
	acpi_processor_unregister_performance(policy->cpu);
}

static void intel_pstate_exit_perf_limits(struct cpufreq_policy *policy)
{
	struct cpudata *cpu;

	cpu = all_cpu_data[policy->cpu];
	if (!cpu->valid_pss_table)
		return;

	acpi_processor_unregister_performance(policy->cpu);
}
#else /* CONFIG_ACPI */
static inline void intel_pstate_init_acpi_perf_limits(struct cpufreq_policy *policy)
{
}

static inline void intel_pstate_exit_perf_limits(struct cpufreq_policy *policy)
{
}

static inline bool intel_pstate_acpi_pm_profile_server(void)
{
	return false;
}
#endif /* CONFIG_ACPI */

#ifndef CONFIG_ACPI_CPPC_LIB
static int intel_pstate_get_cppc_guranteed(int cpu)
{
	return -ENOTSUPP;
}
#endif /* CONFIG_ACPI_CPPC_LIB */

static inline void update_turbo_state(void)
{
	u64 misc_en;
	struct cpudata *cpu;

	cpu = all_cpu_data[0];
	rdmsrl(MSR_IA32_MISC_ENABLE, misc_en);
	global.turbo_disabled =
		(misc_en & MSR_IA32_MISC_ENABLE_TURBO_DISABLE ||
		 cpu->pstate.max_pstate == cpu->pstate.turbo_pstate);
}

static int min_perf_pct_min(void)
{
	struct cpudata *cpu = all_cpu_data[0];
	int turbo_pstate = cpu->pstate.turbo_pstate;

	return turbo_pstate ?
		(cpu->pstate.min_pstate * 100 / turbo_pstate) : 0;
}

static s16 intel_pstate_get_epb(struct cpudata *cpu_data)
{
	u64 epb;
	int ret;

	if (!static_cpu_has(X86_FEATURE_EPB))
		return -ENXIO;

	ret = rdmsrl_on_cpu(cpu_data->cpu, MSR_IA32_ENERGY_PERF_BIAS, &epb);
	if (ret)
		return (s16)ret;

	return (s16)(epb & 0x0f);
}

static s16 intel_pstate_get_epp(struct cpudata *cpu_data, u64 hwp_req_data)
{
	s16 epp;

	if (static_cpu_has(X86_FEATURE_HWP_EPP)) {
		/*
		 * When hwp_req_data is 0, means that caller didn't read
		 * MSR_HWP_REQUEST, so need to read and get EPP.
		 */
		if (!hwp_req_data) {
			epp = rdmsrl_on_cpu(cpu_data->cpu, MSR_HWP_REQUEST,
					    &hwp_req_data);
			if (epp)
				return epp;
		}
		epp = (hwp_req_data >> 24) & 0xff;
	} else {
		/* When there is no EPP present, HWP uses EPB settings */
		epp = intel_pstate_get_epb(cpu_data);
	}

	return epp;
}

static int intel_pstate_set_epb(int cpu, s16 pref)
{
	u64 epb;
	int ret;

	if (!static_cpu_has(X86_FEATURE_EPB))
		return -ENXIO;

	ret = rdmsrl_on_cpu(cpu, MSR_IA32_ENERGY_PERF_BIAS, &epb);
	if (ret)
		return ret;

	epb = (epb & ~0x0f) | pref;
	wrmsrl_on_cpu(cpu, MSR_IA32_ENERGY_PERF_BIAS, epb);

	return 0;
}

/*
 * EPP/EPB display strings corresponding to EPP index in the
 * energy_perf_strings[]
 *	index		String
 *-------------------------------------
 *	0		default
 *	1		performance
 *	2		balance_performance
 *	3		balance_power
 *	4		power
 */
static const char * const energy_perf_strings[] = {
	"default",
	"performance",
	"balance_performance",
	"balance_power",
	"power",
	NULL
};
static const unsigned int epp_values[] = {
	HWP_EPP_PERFORMANCE,
	HWP_EPP_BALANCE_PERFORMANCE,
	HWP_EPP_BALANCE_POWERSAVE,
	HWP_EPP_POWERSAVE
};

static int intel_pstate_get_energy_pref_index(struct cpudata *cpu_data)
{
	s16 epp;
	int index = -EINVAL;

	epp = intel_pstate_get_epp(cpu_data, 0);
	if (epp < 0)
		return epp;

	if (static_cpu_has(X86_FEATURE_HWP_EPP)) {
		if (epp == HWP_EPP_PERFORMANCE)
			return 1;
		if (epp <= HWP_EPP_BALANCE_PERFORMANCE)
			return 2;
		if (epp <= HWP_EPP_BALANCE_POWERSAVE)
			return 3;
		else
			return 4;
	} else if (static_cpu_has(X86_FEATURE_EPB)) {
		/*
		 * Range:
		 *	0x00-0x03	:	Performance
		 *	0x04-0x07	:	Balance performance
		 *	0x08-0x0B	:	Balance power
		 *	0x0C-0x0F	:	Power
		 * The EPB is a 4 bit value, but our ranges restrict the
		 * value which can be set. Here only using top two bits
		 * effectively.
		 */
		index = (epp >> 2) + 1;
	}

	return index;
}

static int intel_pstate_set_energy_pref_index(struct cpudata *cpu_data,
					      int pref_index)
{
	int epp = -EINVAL;
	int ret;

	if (!pref_index)
		epp = cpu_data->epp_default;

	mutex_lock(&intel_pstate_limits_lock);

	if (static_cpu_has(X86_FEATURE_HWP_EPP)) {
		u64 value;

		ret = rdmsrl_on_cpu(cpu_data->cpu, MSR_HWP_REQUEST, &value);
		if (ret)
			goto return_pref;

		value &= ~GENMASK_ULL(31, 24);

		if (epp == -EINVAL)
			epp = epp_values[pref_index - 1];

		value |= (u64)epp << 24;
		ret = wrmsrl_on_cpu(cpu_data->cpu, MSR_HWP_REQUEST, value);
	} else {
		if (epp == -EINVAL)
			epp = (pref_index - 1) << 2;
		ret = intel_pstate_set_epb(cpu_data->cpu, epp);
	}
return_pref:
	mutex_unlock(&intel_pstate_limits_lock);

	return ret;
}

static ssize_t show_energy_performance_available_preferences(
				struct cpufreq_policy *policy, char *buf)
{
	int i = 0;
	int ret = 0;

	while (energy_perf_strings[i] != NULL)
		ret += sprintf(&buf[ret], "%s ", energy_perf_strings[i++]);

	ret += sprintf(&buf[ret], "\n");

	return ret;
}

cpufreq_freq_attr_ro(energy_performance_available_preferences);

static ssize_t store_energy_performance_preference(
		struct cpufreq_policy *policy, const char *buf, size_t count)
{
	struct cpudata *cpu_data = all_cpu_data[policy->cpu];
	char str_preference[21];
	int ret;

	ret = sscanf(buf, "%20s", str_preference);
	if (ret != 1)
		return -EINVAL;

	ret = match_string(energy_perf_strings, -1, str_preference);
	if (ret < 0)
		return ret;

	intel_pstate_set_energy_pref_index(cpu_data, ret);
	return count;
}

static ssize_t show_energy_performance_preference(
				struct cpufreq_policy *policy, char *buf)
{
	struct cpudata *cpu_data = all_cpu_data[policy->cpu];
	int preference;

	preference = intel_pstate_get_energy_pref_index(cpu_data);
	if (preference < 0)
		return preference;

	return  sprintf(buf, "%s\n", energy_perf_strings[preference]);
}

cpufreq_freq_attr_rw(energy_performance_preference);

static ssize_t show_base_frequency(struct cpufreq_policy *policy, char *buf)
{
	struct cpudata *cpu;
	u64 cap;
	int ratio;

	ratio = intel_pstate_get_cppc_guranteed(policy->cpu);
	if (ratio <= 0) {
		rdmsrl_on_cpu(policy->cpu, MSR_HWP_CAPABILITIES, &cap);
		ratio = HWP_GUARANTEED_PERF(cap);
	}

	cpu = all_cpu_data[policy->cpu];

	return sprintf(buf, "%d\n", ratio * cpu->pstate.scaling);
}

cpufreq_freq_attr_ro(base_frequency);

static struct freq_attr *hwp_cpufreq_attrs[] = {
	&energy_performance_preference,
	&energy_performance_available_preferences,
	&base_frequency,
	NULL,
};

static void intel_pstate_get_hwp_max(unsigned int cpu, int *phy_max,
				     int *current_max)
{
	u64 cap;

	rdmsrl_on_cpu(cpu, MSR_HWP_CAPABILITIES, &cap);
	WRITE_ONCE(all_cpu_data[cpu]->hwp_cap_cached, cap);
	if (global.no_turbo)
		*current_max = HWP_GUARANTEED_PERF(cap);
	else
		*current_max = HWP_HIGHEST_PERF(cap);

	*phy_max = HWP_HIGHEST_PERF(cap);
}

static void intel_pstate_hwp_set(unsigned int cpu)
{
	struct cpudata *cpu_data = all_cpu_data[cpu];
	int max, min;
	u64 value;
	s16 epp;

	max = cpu_data->max_perf_ratio;
	min = cpu_data->min_perf_ratio;

	if (cpu_data->policy == CPUFREQ_POLICY_PERFORMANCE)
		min = max;

	rdmsrl_on_cpu(cpu, MSR_HWP_REQUEST, &value);

	value &= ~HWP_MIN_PERF(~0L);
	value |= HWP_MIN_PERF(min);

	value &= ~HWP_MAX_PERF(~0L);
	value |= HWP_MAX_PERF(max);

	if (cpu_data->epp_policy == cpu_data->policy)
		goto skip_epp;

	cpu_data->epp_policy = cpu_data->policy;

	if (cpu_data->epp_saved >= 0) {
		epp = cpu_data->epp_saved;
		cpu_data->epp_saved = -EINVAL;
		goto update_epp;
	}

	if (cpu_data->policy == CPUFREQ_POLICY_PERFORMANCE) {
		epp = intel_pstate_get_epp(cpu_data, value);
		cpu_data->epp_powersave = epp;
		/* If EPP read was failed, then don't try to write */
		if (epp < 0)
			goto skip_epp;

		epp = 0;
	} else {
		/* skip setting EPP, when saved value is invalid */
		if (cpu_data->epp_powersave < 0)
			goto skip_epp;

		/*
		 * No need to restore EPP when it is not zero. This
		 * means:
		 *  - Policy is not changed
		 *  - user has manually changed
		 *  - Error reading EPB
		 */
		epp = intel_pstate_get_epp(cpu_data, value);
		if (epp)
			goto skip_epp;

		epp = cpu_data->epp_powersave;
	}
update_epp:
	if (static_cpu_has(X86_FEATURE_HWP_EPP)) {
		value &= ~GENMASK_ULL(31, 24);
		value |= (u64)epp << 24;
	} else {
		intel_pstate_set_epb(cpu, epp);
	}
skip_epp:
	WRITE_ONCE(cpu_data->hwp_req_cached, value);
	wrmsrl_on_cpu(cpu, MSR_HWP_REQUEST, value);
}

static void intel_pstate_hwp_force_min_perf(int cpu)
{
	u64 value;
	int min_perf;

	value = all_cpu_data[cpu]->hwp_req_cached;
	value &= ~GENMASK_ULL(31, 0);
	min_perf = HWP_LOWEST_PERF(all_cpu_data[cpu]->hwp_cap_cached);

	/* Set hwp_max = hwp_min */
	value |= HWP_MAX_PERF(min_perf);
	value |= HWP_MIN_PERF(min_perf);

	/* Set EPP/EPB to min */
	if (static_cpu_has(X86_FEATURE_HWP_EPP))
		value |= HWP_ENERGY_PERF_PREFERENCE(HWP_EPP_POWERSAVE);
	else
		intel_pstate_set_epb(cpu, HWP_EPP_BALANCE_POWERSAVE);

	wrmsrl_on_cpu(cpu, MSR_HWP_REQUEST, value);
}

static int intel_pstate_hwp_save_state(struct cpufreq_policy *policy)
{
	struct cpudata *cpu_data = all_cpu_data[policy->cpu];

	if (!hwp_active)
		return 0;

	cpu_data->epp_saved = intel_pstate_get_epp(cpu_data, 0);

	return 0;
}

static void intel_pstate_hwp_enable(struct cpudata *cpudata);

static int intel_pstate_resume(struct cpufreq_policy *policy)
{
	if (!hwp_active)
		return 0;

	mutex_lock(&intel_pstate_limits_lock);

	if (policy->cpu == 0)
		intel_pstate_hwp_enable(all_cpu_data[policy->cpu]);

	all_cpu_data[policy->cpu]->epp_policy = 0;
	intel_pstate_hwp_set(policy->cpu);

	mutex_unlock(&intel_pstate_limits_lock);

	return 0;
}

static void intel_pstate_update_policies(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		cpufreq_update_policy(cpu);
}

/************************** sysfs begin ************************/
#define show_one(file_name, object)					\
	static ssize_t show_##file_name					\
	(struct kobject *kobj, struct kobj_attribute *attr, char *buf)	\
	{								\
		return sprintf(buf, "%u\n", global.object);		\
	}

static ssize_t intel_pstate_show_status(char *buf);
static int intel_pstate_update_status(const char *buf, size_t size);

static ssize_t show_status(struct kobject *kobj,
			   struct kobj_attribute *attr, char *buf)
{
	ssize_t ret;

	mutex_lock(&intel_pstate_driver_lock);
	ret = intel_pstate_show_status(buf);
	mutex_unlock(&intel_pstate_driver_lock);

	return ret;
}

static ssize_t store_status(struct kobject *a, struct kobj_attribute *b,
			    const char *buf, size_t count)
{
	char *p = memchr(buf, '\n', count);
	int ret;

	mutex_lock(&intel_pstate_driver_lock);
	ret = intel_pstate_update_status(buf, p ? p - buf : count);
	mutex_unlock(&intel_pstate_driver_lock);

	return ret < 0 ? ret : count;
}

static ssize_t show_turbo_pct(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	struct cpudata *cpu;
	int total, no_turbo, turbo_pct;
	uint32_t turbo_fp;

	mutex_lock(&intel_pstate_driver_lock);

	if (!intel_pstate_driver) {
		mutex_unlock(&intel_pstate_driver_lock);
		return -EAGAIN;
	}

	cpu = all_cpu_data[0];

	total = cpu->pstate.turbo_pstate - cpu->pstate.min_pstate + 1;
	no_turbo = cpu->pstate.max_pstate - cpu->pstate.min_pstate + 1;
	turbo_fp = div_fp(no_turbo, total);
	turbo_pct = 100 - fp_toint(mul_fp(turbo_fp, int_tofp(100)));

	mutex_unlock(&intel_pstate_driver_lock);

	return sprintf(buf, "%u\n", turbo_pct);
}

static ssize_t show_num_pstates(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	struct cpudata *cpu;
	int total;

	mutex_lock(&intel_pstate_driver_lock);

	if (!intel_pstate_driver) {
		mutex_unlock(&intel_pstate_driver_lock);
		return -EAGAIN;
	}

	cpu = all_cpu_data[0];
	total = cpu->pstate.turbo_pstate - cpu->pstate.min_pstate + 1;

	mutex_unlock(&intel_pstate_driver_lock);

	return sprintf(buf, "%u\n", total);
}

static ssize_t show_no_turbo(struct kobject *kobj,
			     struct kobj_attribute *attr, char *buf)
{
	ssize_t ret;

	mutex_lock(&intel_pstate_driver_lock);

	if (!intel_pstate_driver) {
		mutex_unlock(&intel_pstate_driver_lock);
		return -EAGAIN;
	}

	update_turbo_state();
	if (global.turbo_disabled)
		ret = sprintf(buf, "%u\n", global.turbo_disabled);
	else
		ret = sprintf(buf, "%u\n", global.no_turbo);

	mutex_unlock(&intel_pstate_driver_lock);

	return ret;
}

static ssize_t store_no_turbo(struct kobject *a, struct kobj_attribute *b,
			      const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	mutex_lock(&intel_pstate_driver_lock);

	if (!intel_pstate_driver) {
		mutex_unlock(&intel_pstate_driver_lock);
		return -EAGAIN;
	}

	mutex_lock(&intel_pstate_limits_lock);

	update_turbo_state();
	if (global.turbo_disabled) {
		pr_warn("Turbo disabled by BIOS or unavailable on processor\n");
		mutex_unlock(&intel_pstate_limits_lock);
		mutex_unlock(&intel_pstate_driver_lock);
		return -EPERM;
	}

	global.no_turbo = clamp_t(int, input, 0, 1);

	if (global.no_turbo) {
		struct cpudata *cpu = all_cpu_data[0];
		int pct = cpu->pstate.max_pstate * 100 / cpu->pstate.turbo_pstate;

		/* Squash the global minimum into the permitted range. */
		if (global.min_perf_pct > pct)
			global.min_perf_pct = pct;
	}

	mutex_unlock(&intel_pstate_limits_lock);

	intel_pstate_update_policies();

	mutex_unlock(&intel_pstate_driver_lock);

	return count;
}

static ssize_t store_max_perf_pct(struct kobject *a, struct kobj_attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	mutex_lock(&intel_pstate_driver_lock);

	if (!intel_pstate_driver) {
		mutex_unlock(&intel_pstate_driver_lock);
		return -EAGAIN;
	}

	mutex_lock(&intel_pstate_limits_lock);

	global.max_perf_pct = clamp_t(int, input, global.min_perf_pct, 100);

	mutex_unlock(&intel_pstate_limits_lock);

	intel_pstate_update_policies();

	mutex_unlock(&intel_pstate_driver_lock);

	return count;
}

static ssize_t store_min_perf_pct(struct kobject *a, struct kobj_attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	mutex_lock(&intel_pstate_driver_lock);

	if (!intel_pstate_driver) {
		mutex_unlock(&intel_pstate_driver_lock);
		return -EAGAIN;
	}

	mutex_lock(&intel_pstate_limits_lock);

	global.min_perf_pct = clamp_t(int, input,
				      min_perf_pct_min(), global.max_perf_pct);

	mutex_unlock(&intel_pstate_limits_lock);

	intel_pstate_update_policies();

	mutex_unlock(&intel_pstate_driver_lock);

	return count;
}

static ssize_t show_hwp_dynamic_boost(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", hwp_boost);
}

static ssize_t store_hwp_dynamic_boost(struct kobject *a,
				       struct kobj_attribute *b,
				       const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = kstrtouint(buf, 10, &input);
	if (ret)
		return ret;

	mutex_lock(&intel_pstate_driver_lock);
	hwp_boost = !!input;
	intel_pstate_update_policies();
	mutex_unlock(&intel_pstate_driver_lock);

	return count;
}

show_one(max_perf_pct, max_perf_pct);
show_one(min_perf_pct, min_perf_pct);

define_one_global_rw(status);
define_one_global_rw(no_turbo);
define_one_global_rw(max_perf_pct);
define_one_global_rw(min_perf_pct);
define_one_global_ro(turbo_pct);
define_one_global_ro(num_pstates);
define_one_global_rw(hwp_dynamic_boost);

static struct attribute *intel_pstate_attributes[] = {
	&status.attr,
	&no_turbo.attr,
	&turbo_pct.attr,
	&num_pstates.attr,
	NULL
};

static const struct attribute_group intel_pstate_attr_group = {
	.attrs = intel_pstate_attributes,
};

static void __init intel_pstate_sysfs_expose_params(void)
{
	struct kobject *intel_pstate_kobject;
	int rc;

	intel_pstate_kobject = kobject_create_and_add("intel_pstate",
						&cpu_subsys.dev_root->kobj);
	if (WARN_ON(!intel_pstate_kobject))
		return;

	rc = sysfs_create_group(intel_pstate_kobject, &intel_pstate_attr_group);
	if (WARN_ON(rc))
		return;

	/*
	 * If per cpu limits are enforced there are no global limits, so
	 * return without creating max/min_perf_pct attributes
	 */
	if (per_cpu_limits)
		return;

	rc = sysfs_create_file(intel_pstate_kobject, &max_perf_pct.attr);
	WARN_ON(rc);

	rc = sysfs_create_file(intel_pstate_kobject, &min_perf_pct.attr);
	WARN_ON(rc);

	if (hwp_active) {
		rc = sysfs_create_file(intel_pstate_kobject,
				       &hwp_dynamic_boost.attr);
		WARN_ON(rc);
	}
}
/************************** sysfs end ************************/

static void intel_pstate_hwp_enable(struct cpudata *cpudata)
{
	/* First disable HWP notification interrupt as we don't process them */
	if (static_cpu_has(X86_FEATURE_HWP_NOTIFY))
		wrmsrl_on_cpu(cpudata->cpu, MSR_HWP_INTERRUPT, 0x00);

	wrmsrl_on_cpu(cpudata->cpu, MSR_PM_ENABLE, 0x1);
	cpudata->epp_policy = 0;
	if (cpudata->epp_default == -EINVAL)
		cpudata->epp_default = intel_pstate_get_epp(cpudata, 0);
}

#define MSR_IA32_POWER_CTL_BIT_EE	19

/* Disable energy efficiency optimization */
static void intel_pstate_disable_ee(int cpu)
{
	u64 power_ctl;
	int ret;

	ret = rdmsrl_on_cpu(cpu, MSR_IA32_POWER_CTL, &power_ctl);
	if (ret)
		return;

	if (!(power_ctl & BIT(MSR_IA32_POWER_CTL_BIT_EE))) {
		pr_info("Disabling energy efficiency optimization\n");
		power_ctl |= BIT(MSR_IA32_POWER_CTL_BIT_EE);
		wrmsrl_on_cpu(cpu, MSR_IA32_POWER_CTL, power_ctl);
	}
}

static int atom_get_min_pstate(void)
{
	u64 value;

	rdmsrl(MSR_ATOM_CORE_RATIOS, value);
	return (value >> 8) & 0x7F;
}

static int atom_get_max_pstate(void)
{
	u64 value;

	rdmsrl(MSR_ATOM_CORE_RATIOS, value);
	return (value >> 16) & 0x7F;
}

static int atom_get_turbo_pstate(void)
{
	u64 value;

	rdmsrl(MSR_ATOM_CORE_TURBO_RATIOS, value);
	return value & 0x7F;
}

static u64 atom_get_val(struct cpudata *cpudata, int pstate)
{
	u64 val;
	int32_t vid_fp;
	u32 vid;

	val = (u64)pstate << 8;
	if (global.no_turbo && !global.turbo_disabled)
		val |= (u64)1 << 32;

	vid_fp = cpudata->vid.min + mul_fp(
		int_tofp(pstate - cpudata->pstate.min_pstate),
		cpudata->vid.ratio);

	vid_fp = clamp_t(int32_t, vid_fp, cpudata->vid.min, cpudata->vid.max);
	vid = ceiling_fp(vid_fp);

	if (pstate > cpudata->pstate.max_pstate)
		vid = cpudata->vid.turbo;

	return val | vid;
}

static int silvermont_get_scaling(void)
{
	u64 value;
	int i;
	/* Defined in Table 35-6 from SDM (Sept 2015) */
	static int silvermont_freq_table[] = {
		83300, 100000, 133300, 116700, 80000};

	rdmsrl(MSR_FSB_FREQ, value);
	i = value & 0x7;
	WARN_ON(i > 4);

	return silvermont_freq_table[i];
}

static int airmont_get_scaling(void)
{
	u64 value;
	int i;
	/* Defined in Table 35-10 from SDM (Sept 2015) */
	static int airmont_freq_table[] = {
		83300, 100000, 133300, 116700, 80000,
		93300, 90000, 88900, 87500};

	rdmsrl(MSR_FSB_FREQ, value);
	i = value & 0xF;
	WARN_ON(i > 8);

	return airmont_freq_table[i];
}

static void atom_get_vid(struct cpudata *cpudata)
{
	u64 value;

	rdmsrl(MSR_ATOM_CORE_VIDS, value);
	cpudata->vid.min = int_tofp((value >> 8) & 0x7f);
	cpudata->vid.max = int_tofp((value >> 16) & 0x7f);
	cpudata->vid.ratio = div_fp(
		cpudata->vid.max - cpudata->vid.min,
		int_tofp(cpudata->pstate.max_pstate -
			cpudata->pstate.min_pstate));

	rdmsrl(MSR_ATOM_CORE_TURBO_VIDS, value);
	cpudata->vid.turbo = value & 0x7f;
}

static int core_get_min_pstate(void)
{
	u64 value;

	rdmsrl(MSR_PLATFORM_INFO, value);
	return (value >> 40) & 0xFF;
}

static int core_get_max_pstate_physical(void)
{
	u64 value;

	rdmsrl(MSR_PLATFORM_INFO, value);
	return (value >> 8) & 0xFF;
}

static int core_get_tdp_ratio(u64 plat_info)
{
	/* Check how many TDP levels present */
	if (plat_info & 0x600000000) {
		u64 tdp_ctrl;
		u64 tdp_ratio;
		int tdp_msr;
		int err;

		/* Get the TDP level (0, 1, 2) to get ratios */
		err = rdmsrl_safe(MSR_CONFIG_TDP_CONTROL, &tdp_ctrl);
		if (err)
			return err;

		/* TDP MSR are continuous starting at 0x648 */
		tdp_msr = MSR_CONFIG_TDP_NOMINAL + (tdp_ctrl & 0x03);
		err = rdmsrl_safe(tdp_msr, &tdp_ratio);
		if (err)
			return err;

		/* For level 1 and 2, bits[23:16] contain the ratio */
		if (tdp_ctrl & 0x03)
			tdp_ratio >>= 16;

		tdp_ratio &= 0xff; /* ratios are only 8 bits long */
		pr_debug("tdp_ratio %x\n", (int)tdp_ratio);

		return (int)tdp_ratio;
	}

	return -ENXIO;
}

static int core_get_max_pstate(void)
{
	u64 tar;
	u64 plat_info;
	int max_pstate;
	int tdp_ratio;
	int err;

	rdmsrl(MSR_PLATFORM_INFO, plat_info);
	max_pstate = (plat_info >> 8) & 0xFF;

	tdp_ratio = core_get_tdp_ratio(plat_info);
	if (tdp_ratio <= 0)
		return max_pstate;

	if (hwp_active) {
		/* Turbo activation ratio is not used on HWP platforms */
		return tdp_ratio;
	}

	err = rdmsrl_safe(MSR_TURBO_ACTIVATION_RATIO, &tar);
	if (!err) {
		int tar_levels;

		/* Do some sanity checking for safety */
		tar_levels = tar & 0xff;
		if (tdp_ratio - 1 == tar_levels) {
			max_pstate = tar_levels;
			pr_debug("max_pstate=TAC %x\n", max_pstate);
		}
	}

	return max_pstate;
}

static int core_get_turbo_pstate(void)
{
	u64 value;
	int nont, ret;

	rdmsrl(MSR_TURBO_RATIO_LIMIT, value);
	nont = core_get_max_pstate();
	ret = (value) & 255;
	if (ret <= nont)
		ret = nont;
	return ret;
}

static inline int core_get_scaling(void)
{
	return 100000;
}

static u64 core_get_val(struct cpudata *cpudata, int pstate)
{
	u64 val;

	val = (u64)pstate << 8;
	if (global.no_turbo && !global.turbo_disabled)
		val |= (u64)1 << 32;

	return val;
}

static int knl_get_aperf_mperf_shift(void)
{
	return 10;
}

static int knl_get_turbo_pstate(void)
{
	u64 value;
	int nont, ret;

	rdmsrl(MSR_TURBO_RATIO_LIMIT, value);
	nont = core_get_max_pstate();
	ret = (((value) >> 8) & 0xFF);
	if (ret <= nont)
		ret = nont;
	return ret;
}

static void intel_pstate_set_pstate(struct cpudata *cpu, int pstate)
{
	trace_cpu_frequency(pstate * cpu->pstate.scaling, cpu->cpu);
	cpu->pstate.current_pstate = pstate;
	/*
	 * Generally, there is no guarantee that this code will always run on
	 * the CPU being updated, so force the register update to run on the
	 * right CPU.
	 */
	wrmsrl_on_cpu(cpu->cpu, MSR_IA32_PERF_CTL,
		      pstate_funcs.get_val(cpu, pstate));
}

static void intel_pstate_set_min_pstate(struct cpudata *cpu)
{
	intel_pstate_set_pstate(cpu, cpu->pstate.min_pstate);
}

static void intel_pstate_max_within_limits(struct cpudata *cpu)
{
	int pstate = max(cpu->pstate.min_pstate, cpu->max_perf_ratio);

	update_turbo_state();
	intel_pstate_set_pstate(cpu, pstate);
}

static void intel_pstate_get_cpu_pstates(struct cpudata *cpu)
{
	cpu->pstate.min_pstate = pstate_funcs.get_min();
	cpu->pstate.max_pstate = pstate_funcs.get_max();
	cpu->pstate.max_pstate_physical = pstate_funcs.get_max_physical();
	cpu->pstate.turbo_pstate = pstate_funcs.get_turbo();
	cpu->pstate.scaling = pstate_funcs.get_scaling();
	cpu->pstate.max_freq = cpu->pstate.max_pstate * cpu->pstate.scaling;

	if (hwp_active && !hwp_mode_bdw) {
		unsigned int phy_max, current_max;

		intel_pstate_get_hwp_max(cpu->cpu, &phy_max, &current_max);
		cpu->pstate.turbo_freq = phy_max * cpu->pstate.scaling;
	} else {
		cpu->pstate.turbo_freq = cpu->pstate.turbo_pstate * cpu->pstate.scaling;
	}

	if (pstate_funcs.get_aperf_mperf_shift)
		cpu->aperf_mperf_shift = pstate_funcs.get_aperf_mperf_shift();

	if (pstate_funcs.get_vid)
		pstate_funcs.get_vid(cpu);

	intel_pstate_set_min_pstate(cpu);
}

/*
 * Long hold time will keep high perf limits for long time,
 * which negatively impacts perf/watt for some workloads,
 * like specpower. 3ms is based on experiements on some
 * workoads.
 */
static int hwp_boost_hold_time_ns = 3 * NSEC_PER_MSEC;

static inline void intel_pstate_hwp_boost_up(struct cpudata *cpu)
{
	u64 hwp_req = READ_ONCE(cpu->hwp_req_cached);
	u32 max_limit = (hwp_req & 0xff00) >> 8;
	u32 min_limit = (hwp_req & 0xff);
	u32 boost_level1;

	/*
	 * Cases to consider (User changes via sysfs or boot time):
	 * If, P0 (Turbo max) = P1 (Guaranteed max) = min:
	 *	No boost, return.
	 * If, P0 (Turbo max) > P1 (Guaranteed max) = min:
	 *     Should result in one level boost only for P0.
	 * If, P0 (Turbo max) = P1 (Guaranteed max) > min:
	 *     Should result in two level boost:
	 *         (min + p1)/2 and P1.
	 * If, P0 (Turbo max) > P1 (Guaranteed max) > min:
	 *     Should result in three level boost:
	 *        (min + p1)/2, P1 and P0.
	 */

	/* If max and min are equal or already at max, nothing to boost */
	if (max_limit == min_limit || cpu->hwp_boost_min >= max_limit)
		return;

	if (!cpu->hwp_boost_min)
		cpu->hwp_boost_min = min_limit;

	/* level at half way mark between min and guranteed */
	boost_level1 = (HWP_GUARANTEED_PERF(cpu->hwp_cap_cached) + min_limit) >> 1;

	if (cpu->hwp_boost_min < boost_level1)
		cpu->hwp_boost_min = boost_level1;
	else if (cpu->hwp_boost_min < HWP_GUARANTEED_PERF(cpu->hwp_cap_cached))
		cpu->hwp_boost_min = HWP_GUARANTEED_PERF(cpu->hwp_cap_cached);
	else if (cpu->hwp_boost_min == HWP_GUARANTEED_PERF(cpu->hwp_cap_cached) &&
		 max_limit != HWP_GUARANTEED_PERF(cpu->hwp_cap_cached))
		cpu->hwp_boost_min = max_limit;
	else
		return;

	hwp_req = (hwp_req & ~GENMASK_ULL(7, 0)) | cpu->hwp_boost_min;
	wrmsrl(MSR_HWP_REQUEST, hwp_req);
	cpu->last_update = cpu->sample.time;
}

static inline void intel_pstate_hwp_boost_down(struct cpudata *cpu)
{
	if (cpu->hwp_boost_min) {
		bool expired;

		/* Check if we are idle for hold time to boost down */
		expired = time_after64(cpu->sample.time, cpu->last_update +
				       hwp_boost_hold_time_ns);
		if (expired) {
			wrmsrl(MSR_HWP_REQUEST, cpu->hwp_req_cached);
			cpu->hwp_boost_min = 0;
		}
	}
	cpu->last_update = cpu->sample.time;
}

static inline void intel_pstate_update_util_hwp_local(struct cpudata *cpu,
						      u64 time)
{
	cpu->sample.time = time;

	if (cpu->sched_flags & SCHED_CPUFREQ_IOWAIT) {
		bool do_io = false;

		cpu->sched_flags = 0;
		/*
		 * Set iowait_boost flag and update time. Since IO WAIT flag
		 * is set all the time, we can't just conclude that there is
		 * some IO bound activity is scheduled on this CPU with just
		 * one occurrence. If we receive at least two in two
		 * consecutive ticks, then we treat as boost candidate.
		 */
		if (time_before64(time, cpu->last_io_update + 2 * TICK_NSEC))
			do_io = true;

		cpu->last_io_update = time;

		if (do_io)
			intel_pstate_hwp_boost_up(cpu);

	} else {
		intel_pstate_hwp_boost_down(cpu);
	}
}

static inline void intel_pstate_update_util_hwp(struct update_util_data *data,
						u64 time, unsigned int flags)
{
	struct cpudata *cpu = container_of(data, struct cpudata, update_util);

	cpu->sched_flags |= flags;

	if (smp_processor_id() == cpu->cpu)
		intel_pstate_update_util_hwp_local(cpu, time);
}

static inline void intel_pstate_calc_avg_perf(struct cpudata *cpu)
{
	struct sample *sample = &cpu->sample;

	sample->core_avg_perf = div_ext_fp(sample->aperf, sample->mperf);
}

static inline bool intel_pstate_sample(struct cpudata *cpu, u64 time)
{
	u64 aperf, mperf;
	unsigned long flags;
	u64 tsc;

	local_irq_save(flags);
	rdmsrl(MSR_IA32_APERF, aperf);
	rdmsrl(MSR_IA32_MPERF, mperf);
	tsc = rdtsc();
	if (cpu->prev_mperf == mperf || cpu->prev_tsc == tsc) {
		local_irq_restore(flags);
		return false;
	}
	local_irq_restore(flags);

	cpu->last_sample_time = cpu->sample.time;
	cpu->sample.time = time;
	cpu->sample.aperf = aperf;
	cpu->sample.mperf = mperf;
	cpu->sample.tsc =  tsc;
	cpu->sample.aperf -= cpu->prev_aperf;
	cpu->sample.mperf -= cpu->prev_mperf;
	cpu->sample.tsc -= cpu->prev_tsc;

	cpu->prev_aperf = aperf;
	cpu->prev_mperf = mperf;
	cpu->prev_tsc = tsc;
	/*
	 * First time this function is invoked in a given cycle, all of the
	 * previous sample data fields are equal to zero or stale and they must
	 * be populated with meaningful numbers for things to work, so assume
	 * that sample.time will always be reset before setting the utilization
	 * update hook and make the caller skip the sample then.
	 */
	if (cpu->last_sample_time) {
		intel_pstate_calc_avg_perf(cpu);
		return true;
	}
	return false;
}

static inline int32_t get_avg_frequency(struct cpudata *cpu)
{
	return mul_ext_fp(cpu->sample.core_avg_perf, cpu_khz);
}

static inline int32_t get_avg_pstate(struct cpudata *cpu)
{
	return mul_ext_fp(cpu->pstate.max_pstate_physical,
			  cpu->sample.core_avg_perf);
}

static inline int32_t get_target_pstate(struct cpudata *cpu)
{
	struct sample *sample = &cpu->sample;
	int32_t busy_frac;
	int target, avg_pstate;

	busy_frac = div_fp(sample->mperf << cpu->aperf_mperf_shift,
			   sample->tsc);

	if (busy_frac < cpu->iowait_boost)
		busy_frac = cpu->iowait_boost;

	sample->busy_scaled = busy_frac * 100;

	target = global.no_turbo || global.turbo_disabled ?
			cpu->pstate.max_pstate : cpu->pstate.turbo_pstate;
	target += target >> 2;
	target = mul_fp(target, busy_frac);
	if (target < cpu->pstate.min_pstate)
		target = cpu->pstate.min_pstate;

	/*
	 * If the average P-state during the previous cycle was higher than the
	 * current target, add 50% of the difference to the target to reduce
	 * possible performance oscillations and offset possible performance
	 * loss related to moving the workload from one CPU to another within
	 * a package/module.
	 */
	avg_pstate = get_avg_pstate(cpu);
	if (avg_pstate > target)
		target += (avg_pstate - target) >> 1;

	return target;
}

static int intel_pstate_prepare_request(struct cpudata *cpu, int pstate)
{
	int min_pstate = max(cpu->pstate.min_pstate, cpu->min_perf_ratio);
	int max_pstate = max(min_pstate, cpu->max_perf_ratio);

	return clamp_t(int, pstate, min_pstate, max_pstate);
}

static void intel_pstate_update_pstate(struct cpudata *cpu, int pstate)
{
	if (pstate == cpu->pstate.current_pstate)
		return;

	cpu->pstate.current_pstate = pstate;
	wrmsrl(MSR_IA32_PERF_CTL, pstate_funcs.get_val(cpu, pstate));
}

static void intel_pstate_adjust_pstate(struct cpudata *cpu)
{
	int from = cpu->pstate.current_pstate;
	struct sample *sample;
	int target_pstate;

	update_turbo_state();

	target_pstate = get_target_pstate(cpu);
	target_pstate = intel_pstate_prepare_request(cpu, target_pstate);
	trace_cpu_frequency(target_pstate * cpu->pstate.scaling, cpu->cpu);
	intel_pstate_update_pstate(cpu, target_pstate);

	sample = &cpu->sample;
	trace_pstate_sample(mul_ext_fp(100, sample->core_avg_perf),
		fp_toint(sample->busy_scaled),
		from,
		cpu->pstate.current_pstate,
		sample->mperf,
		sample->aperf,
		sample->tsc,
		get_avg_frequency(cpu),
		fp_toint(cpu->iowait_boost * 100));
}

static void intel_pstate_update_util(struct update_util_data *data, u64 time,
				     unsigned int flags)
{
	struct cpudata *cpu = container_of(data, struct cpudata, update_util);
	u64 delta_ns;

	/* Don't allow remote callbacks */
	if (smp_processor_id() != cpu->cpu)
		return;

	delta_ns = time - cpu->last_update;
	if (flags & SCHED_CPUFREQ_IOWAIT) {
		/* Start over if the CPU may have been idle. */
		if (delta_ns > TICK_NSEC) {
			cpu->iowait_boost = ONE_EIGHTH_FP;
		} else if (cpu->iowait_boost >= ONE_EIGHTH_FP) {
			cpu->iowait_boost <<= 1;
			if (cpu->iowait_boost > int_tofp(1))
				cpu->iowait_boost = int_tofp(1);
		} else {
			cpu->iowait_boost = ONE_EIGHTH_FP;
		}
	} else if (cpu->iowait_boost) {
		/* Clear iowait_boost if the CPU may have been idle. */
		if (delta_ns > TICK_NSEC)
			cpu->iowait_boost = 0;
		else
			cpu->iowait_boost >>= 1;
	}
	cpu->last_update = time;
	delta_ns = time - cpu->sample.time;
	if ((s64)delta_ns < INTEL_PSTATE_SAMPLING_INTERVAL)
		return;

	if (intel_pstate_sample(cpu, time))
		intel_pstate_adjust_pstate(cpu);
}

static struct pstate_funcs core_funcs = {
	.get_max = core_get_max_pstate,
	.get_max_physical = core_get_max_pstate_physical,
	.get_min = core_get_min_pstate,
	.get_turbo = core_get_turbo_pstate,
	.get_scaling = core_get_scaling,
	.get_val = core_get_val,
};

static const struct pstate_funcs silvermont_funcs = {
	.get_max = atom_get_max_pstate,
	.get_max_physical = atom_get_max_pstate,
	.get_min = atom_get_min_pstate,
	.get_turbo = atom_get_turbo_pstate,
	.get_val = atom_get_val,
	.get_scaling = silvermont_get_scaling,
	.get_vid = atom_get_vid,
};

static const struct pstate_funcs airmont_funcs = {
	.get_max = atom_get_max_pstate,
	.get_max_physical = atom_get_max_pstate,
	.get_min = atom_get_min_pstate,
	.get_turbo = atom_get_turbo_pstate,
	.get_val = atom_get_val,
	.get_scaling = airmont_get_scaling,
	.get_vid = atom_get_vid,
};

static const struct pstate_funcs knl_funcs = {
	.get_max = core_get_max_pstate,
	.get_max_physical = core_get_max_pstate_physical,
	.get_min = core_get_min_pstate,
	.get_turbo = knl_get_turbo_pstate,
	.get_aperf_mperf_shift = knl_get_aperf_mperf_shift,
	.get_scaling = core_get_scaling,
	.get_val = core_get_val,
};

#define ICPU(model, policy) \
	{ X86_VENDOR_INTEL, 6, model, X86_FEATURE_APERFMPERF,\
			(unsigned long)&policy }

static const struct x86_cpu_id intel_pstate_cpu_ids[] = {
	ICPU(INTEL_FAM6_SANDYBRIDGE, 		core_funcs),
	ICPU(INTEL_FAM6_SANDYBRIDGE_X,		core_funcs),
	ICPU(INTEL_FAM6_ATOM_SILVERMONT,	silvermont_funcs),
	ICPU(INTEL_FAM6_IVYBRIDGE,		core_funcs),
	ICPU(INTEL_FAM6_HASWELL_CORE,		core_funcs),
	ICPU(INTEL_FAM6_BROADWELL_CORE,		core_funcs),
	ICPU(INTEL_FAM6_IVYBRIDGE_X,		core_funcs),
	ICPU(INTEL_FAM6_HASWELL_X,		core_funcs),
	ICPU(INTEL_FAM6_HASWELL_ULT,		core_funcs),
	ICPU(INTEL_FAM6_HASWELL_GT3E,		core_funcs),
	ICPU(INTEL_FAM6_BROADWELL_GT3E,		core_funcs),
	ICPU(INTEL_FAM6_ATOM_AIRMONT,		airmont_funcs),
	ICPU(INTEL_FAM6_SKYLAKE_MOBILE,		core_funcs),
	ICPU(INTEL_FAM6_BROADWELL_X,		core_funcs),
	ICPU(INTEL_FAM6_SKYLAKE_DESKTOP,	core_funcs),
	ICPU(INTEL_FAM6_BROADWELL_XEON_D,	core_funcs),
	ICPU(INTEL_FAM6_XEON_PHI_KNL,		knl_funcs),
	ICPU(INTEL_FAM6_XEON_PHI_KNM,		knl_funcs),
	ICPU(INTEL_FAM6_ATOM_GOLDMONT,		core_funcs),
	ICPU(INTEL_FAM6_ATOM_GOLDMONT_PLUS,     core_funcs),
	ICPU(INTEL_FAM6_SKYLAKE_X,		core_funcs),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, intel_pstate_cpu_ids);

static const struct x86_cpu_id intel_pstate_cpu_oob_ids[] __initconst = {
	ICPU(INTEL_FAM6_BROADWELL_XEON_D, core_funcs),
	ICPU(INTEL_FAM6_BROADWELL_X, core_funcs),
	ICPU(INTEL_FAM6_SKYLAKE_X, core_funcs),
	{}
};

static const struct x86_cpu_id intel_pstate_cpu_ee_disable_ids[] = {
	ICPU(INTEL_FAM6_KABYLAKE_DESKTOP, core_funcs),
	{}
};

static const struct x86_cpu_id intel_pstate_hwp_boost_ids[] = {
	ICPU(INTEL_FAM6_SKYLAKE_X, core_funcs),
	ICPU(INTEL_FAM6_SKYLAKE_DESKTOP, core_funcs),
	{}
};

static int intel_pstate_init_cpu(unsigned int cpunum)
{
	struct cpudata *cpu;

	cpu = all_cpu_data[cpunum];

	if (!cpu) {
		cpu = kzalloc(sizeof(*cpu), GFP_KERNEL);
		if (!cpu)
			return -ENOMEM;

		all_cpu_data[cpunum] = cpu;

		cpu->epp_default = -EINVAL;
		cpu->epp_powersave = -EINVAL;
		cpu->epp_saved = -EINVAL;
	}

	cpu = all_cpu_data[cpunum];

	cpu->cpu = cpunum;

	if (hwp_active) {
		const struct x86_cpu_id *id;

		id = x86_match_cpu(intel_pstate_cpu_ee_disable_ids);
		if (id)
			intel_pstate_disable_ee(cpunum);

		intel_pstate_hwp_enable(cpu);

		id = x86_match_cpu(intel_pstate_hwp_boost_ids);
		if (id && intel_pstate_acpi_pm_profile_server())
			hwp_boost = true;
	}

	intel_pstate_get_cpu_pstates(cpu);

	pr_debug("controlling: cpu %d\n", cpunum);

	return 0;
}

static void intel_pstate_set_update_util_hook(unsigned int cpu_num)
{
	struct cpudata *cpu = all_cpu_data[cpu_num];

	if (hwp_active && !hwp_boost)
		return;

	if (cpu->update_util_set)
		return;

	/* Prevent intel_pstate_update_util() from using stale data. */
	cpu->sample.time = 0;
	cpufreq_add_update_util_hook(cpu_num, &cpu->update_util,
				     (hwp_active ?
				      intel_pstate_update_util_hwp :
				      intel_pstate_update_util));
	cpu->update_util_set = true;
}

static void intel_pstate_clear_update_util_hook(unsigned int cpu)
{
	struct cpudata *cpu_data = all_cpu_data[cpu];

	if (!cpu_data->update_util_set)
		return;

	cpufreq_remove_update_util_hook(cpu);
	cpu_data->update_util_set = false;
	synchronize_rcu();
}

static int intel_pstate_get_max_freq(struct cpudata *cpu)
{
	return global.turbo_disabled || global.no_turbo ?
			cpu->pstate.max_freq : cpu->pstate.turbo_freq;
}

static void intel_pstate_update_perf_limits(struct cpufreq_policy *policy,
					    struct cpudata *cpu)
{
	int max_freq = intel_pstate_get_max_freq(cpu);
	int32_t max_policy_perf, min_policy_perf;
	int max_state, turbo_max;

	/*
	 * HWP needs some special consideration, because on BDX the
	 * HWP_REQUEST uses abstract value to represent performance
	 * rather than pure ratios.
	 */
	if (hwp_active) {
		intel_pstate_get_hwp_max(cpu->cpu, &turbo_max, &max_state);
	} else {
		max_state = global.no_turbo || global.turbo_disabled ?
			cpu->pstate.max_pstate : cpu->pstate.turbo_pstate;
		turbo_max = cpu->pstate.turbo_pstate;
	}

	max_policy_perf = max_state * policy->max / max_freq;
	if (policy->max == policy->min) {
		min_policy_perf = max_policy_perf;
	} else {
		min_policy_perf = max_state * policy->min / max_freq;
		min_policy_perf = clamp_t(int32_t, min_policy_perf,
					  0, max_policy_perf);
	}

	pr_debug("cpu:%d max_state %d min_policy_perf:%d max_policy_perf:%d\n",
		 policy->cpu, max_state,
		 min_policy_perf, max_policy_perf);

	/* Normalize user input to [min_perf, max_perf] */
	if (per_cpu_limits) {
		cpu->min_perf_ratio = min_policy_perf;
		cpu->max_perf_ratio = max_policy_perf;
	} else {
		int32_t global_min, global_max;

		/* Global limits are in percent of the maximum turbo P-state. */
		global_max = DIV_ROUND_UP(turbo_max * global.max_perf_pct, 100);
		global_min = DIV_ROUND_UP(turbo_max * global.min_perf_pct, 100);
		global_min = clamp_t(int32_t, global_min, 0, global_max);

		pr_debug("cpu:%d global_min:%d global_max:%d\n", policy->cpu,
			 global_min, global_max);

		cpu->min_perf_ratio = max(min_policy_perf, global_min);
		cpu->min_perf_ratio = min(cpu->min_perf_ratio, max_policy_perf);
		cpu->max_perf_ratio = min(max_policy_perf, global_max);
		cpu->max_perf_ratio = max(min_policy_perf, cpu->max_perf_ratio);

		/* Make sure min_perf <= max_perf */
		cpu->min_perf_ratio = min(cpu->min_perf_ratio,
					  cpu->max_perf_ratio);

	}
	pr_debug("cpu:%d max_perf_ratio:%d min_perf_ratio:%d\n", policy->cpu,
		 cpu->max_perf_ratio,
		 cpu->min_perf_ratio);
}

static int intel_pstate_set_policy(struct cpufreq_policy *policy)
{
	struct cpudata *cpu;

	if (!policy->cpuinfo.max_freq)
		return -ENODEV;

	pr_debug("set_policy cpuinfo.max %u policy->max %u\n",
		 policy->cpuinfo.max_freq, policy->max);

	cpu = all_cpu_data[policy->cpu];
	cpu->policy = policy->policy;

	mutex_lock(&intel_pstate_limits_lock);

	intel_pstate_update_perf_limits(policy, cpu);

	if (cpu->policy == CPUFREQ_POLICY_PERFORMANCE) {
		/*
		 * NOHZ_FULL CPUs need this as the governor callback may not
		 * be invoked on them.
		 */
		intel_pstate_clear_update_util_hook(policy->cpu);
		intel_pstate_max_within_limits(cpu);
	} else {
		intel_pstate_set_update_util_hook(policy->cpu);
	}

	if (hwp_active) {
		/*
		 * When hwp_boost was active before and dynamically it
		 * was turned off, in that case we need to clear the
		 * update util hook.
		 */
		if (!hwp_boost)
			intel_pstate_clear_update_util_hook(policy->cpu);
		intel_pstate_hwp_set(policy->cpu);
	}

	mutex_unlock(&intel_pstate_limits_lock);

	return 0;
}

static void intel_pstate_adjust_policy_max(struct cpufreq_policy *policy,
					 struct cpudata *cpu)
{
	if (!hwp_active &&
	    cpu->pstate.max_pstate_physical > cpu->pstate.max_pstate &&
	    policy->max < policy->cpuinfo.max_freq &&
	    policy->max > cpu->pstate.max_freq) {
		pr_debug("policy->max > max non turbo frequency\n");
		policy->max = policy->cpuinfo.max_freq;
	}
}

static int intel_pstate_verify_policy(struct cpufreq_policy *policy)
{
	struct cpudata *cpu = all_cpu_data[policy->cpu];

	update_turbo_state();
	cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq,
				     intel_pstate_get_max_freq(cpu));

	if (policy->policy != CPUFREQ_POLICY_POWERSAVE &&
	    policy->policy != CPUFREQ_POLICY_PERFORMANCE)
		return -EINVAL;

	intel_pstate_adjust_policy_max(policy, cpu);

	return 0;
}

static void intel_cpufreq_stop_cpu(struct cpufreq_policy *policy)
{
	intel_pstate_set_min_pstate(all_cpu_data[policy->cpu]);
}

static void intel_pstate_stop_cpu(struct cpufreq_policy *policy)
{
	pr_debug("CPU %d exiting\n", policy->cpu);

	intel_pstate_clear_update_util_hook(policy->cpu);
	if (hwp_active) {
		intel_pstate_hwp_save_state(policy);
		intel_pstate_hwp_force_min_perf(policy->cpu);
	} else {
		intel_cpufreq_stop_cpu(policy);
	}
}

static int intel_pstate_cpu_exit(struct cpufreq_policy *policy)
{
	intel_pstate_exit_perf_limits(policy);

	policy->fast_switch_possible = false;

	return 0;
}

static int __intel_pstate_cpu_init(struct cpufreq_policy *policy)
{
	struct cpudata *cpu;
	int rc;

	rc = intel_pstate_init_cpu(policy->cpu);
	if (rc)
		return rc;

	cpu = all_cpu_data[policy->cpu];

	cpu->max_perf_ratio = 0xFF;
	cpu->min_perf_ratio = 0;

	policy->min = cpu->pstate.min_pstate * cpu->pstate.scaling;
	policy->max = cpu->pstate.turbo_pstate * cpu->pstate.scaling;

	/* cpuinfo and default policy values */
	policy->cpuinfo.min_freq = cpu->pstate.min_pstate * cpu->pstate.scaling;
	update_turbo_state();
	policy->cpuinfo.max_freq = global.turbo_disabled ?
			cpu->pstate.max_pstate : cpu->pstate.turbo_pstate;
	policy->cpuinfo.max_freq *= cpu->pstate.scaling;

	if (hwp_active) {
		unsigned int max_freq;

		max_freq = global.turbo_disabled ?
			cpu->pstate.max_freq : cpu->pstate.turbo_freq;
		if (max_freq < policy->cpuinfo.max_freq)
			policy->cpuinfo.max_freq = max_freq;
	}

	intel_pstate_init_acpi_perf_limits(policy);

	policy->fast_switch_possible = true;

	return 0;
}

static int intel_pstate_cpu_init(struct cpufreq_policy *policy)
{
	int ret = __intel_pstate_cpu_init(policy);

	if (ret)
		return ret;

	if (IS_ENABLED(CONFIG_CPU_FREQ_DEFAULT_GOV_PERFORMANCE))
		policy->policy = CPUFREQ_POLICY_PERFORMANCE;
	else
		policy->policy = CPUFREQ_POLICY_POWERSAVE;

	return 0;
}

static struct cpufreq_driver intel_pstate = {
	.flags		= CPUFREQ_CONST_LOOPS,
	.verify		= intel_pstate_verify_policy,
	.setpolicy	= intel_pstate_set_policy,
	.suspend	= intel_pstate_hwp_save_state,
	.resume		= intel_pstate_resume,
	.init		= intel_pstate_cpu_init,
	.exit		= intel_pstate_cpu_exit,
	.stop_cpu	= intel_pstate_stop_cpu,
	.name		= "intel_pstate",
};

static int intel_cpufreq_verify_policy(struct cpufreq_policy *policy)
{
	struct cpudata *cpu = all_cpu_data[policy->cpu];

	update_turbo_state();
	cpufreq_verify_within_limits(policy, policy->cpuinfo.min_freq,
				     intel_pstate_get_max_freq(cpu));

	intel_pstate_adjust_policy_max(policy, cpu);

	intel_pstate_update_perf_limits(policy, cpu);

	return 0;
}

/* Use of trace in passive mode:
 *
 * In passive mode the trace core_busy field (also known as the
 * performance field, and lablelled as such on the graphs; also known as
 * core_avg_perf) is not needed and so is re-assigned to indicate if the
 * driver call was via the normal or fast switch path. Various graphs
 * output from the intel_pstate_tracer.py utility that include core_busy
 * (or performance or core_avg_perf) have a fixed y-axis from 0 to 100%,
 * so we use 10 to indicate the the normal path through the driver, and
 * 90 to indicate the fast switch path through the driver.
 * The scaled_busy field is not used, and is set to 0.
 */

#define	INTEL_PSTATE_TRACE_TARGET 10
#define	INTEL_PSTATE_TRACE_FAST_SWITCH 90

static void intel_cpufreq_trace(struct cpudata *cpu, unsigned int trace_type, int old_pstate)
{
	struct sample *sample;

	if (!trace_pstate_sample_enabled())
		return;

	if (!intel_pstate_sample(cpu, ktime_get()))
		return;

	sample = &cpu->sample;
	trace_pstate_sample(trace_type,
		0,
		old_pstate,
		cpu->pstate.current_pstate,
		sample->mperf,
		sample->aperf,
		sample->tsc,
		get_avg_frequency(cpu),
		fp_toint(cpu->iowait_boost * 100));
}

static int intel_cpufreq_target(struct cpufreq_policy *policy,
				unsigned int target_freq,
				unsigned int relation)
{
	struct cpudata *cpu = all_cpu_data[policy->cpu];
	struct cpufreq_freqs freqs;
	int target_pstate, old_pstate;

	update_turbo_state();

	freqs.old = policy->cur;
	freqs.new = target_freq;

	cpufreq_freq_transition_begin(policy, &freqs);
	switch (relation) {
	case CPUFREQ_RELATION_L:
		target_pstate = DIV_ROUND_UP(freqs.new, cpu->pstate.scaling);
		break;
	case CPUFREQ_RELATION_H:
		target_pstate = freqs.new / cpu->pstate.scaling;
		break;
	default:
		target_pstate = DIV_ROUND_CLOSEST(freqs.new, cpu->pstate.scaling);
		break;
	}
	target_pstate = intel_pstate_prepare_request(cpu, target_pstate);
	old_pstate = cpu->pstate.current_pstate;
	if (target_pstate != cpu->pstate.current_pstate) {
		cpu->pstate.current_pstate = target_pstate;
		wrmsrl_on_cpu(policy->cpu, MSR_IA32_PERF_CTL,
			      pstate_funcs.get_val(cpu, target_pstate));
	}
	freqs.new = target_pstate * cpu->pstate.scaling;
	intel_cpufreq_trace(cpu, INTEL_PSTATE_TRACE_TARGET, old_pstate);
	cpufreq_freq_transition_end(policy, &freqs, false);

	return 0;
}

static unsigned int intel_cpufreq_fast_switch(struct cpufreq_policy *policy,
					      unsigned int target_freq)
{
	struct cpudata *cpu = all_cpu_data[policy->cpu];
	int target_pstate, old_pstate;

	update_turbo_state();

	target_pstate = DIV_ROUND_UP(target_freq, cpu->pstate.scaling);
	target_pstate = intel_pstate_prepare_request(cpu, target_pstate);
	old_pstate = cpu->pstate.current_pstate;
	intel_pstate_update_pstate(cpu, target_pstate);
	intel_cpufreq_trace(cpu, INTEL_PSTATE_TRACE_FAST_SWITCH, old_pstate);
	return target_pstate * cpu->pstate.scaling;
}

static int intel_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	int ret = __intel_pstate_cpu_init(policy);

	if (ret)
		return ret;

	policy->cpuinfo.transition_latency = INTEL_CPUFREQ_TRANSITION_LATENCY;
	policy->transition_delay_us = INTEL_CPUFREQ_TRANSITION_DELAY;
	/* This reflects the intel_pstate_get_cpu_pstates() setting. */
	policy->cur = policy->cpuinfo.min_freq;

	return 0;
}

static struct cpufreq_driver intel_cpufreq = {
	.flags		= CPUFREQ_CONST_LOOPS,
	.verify		= intel_cpufreq_verify_policy,
	.target		= intel_cpufreq_target,
	.fast_switch	= intel_cpufreq_fast_switch,
	.init		= intel_cpufreq_cpu_init,
	.exit		= intel_pstate_cpu_exit,
	.stop_cpu	= intel_cpufreq_stop_cpu,
	.name		= "intel_cpufreq",
};

static struct cpufreq_driver *default_driver = &intel_pstate;

static void intel_pstate_driver_cleanup(void)
{
	unsigned int cpu;

	get_online_cpus();
	for_each_online_cpu(cpu) {
		if (all_cpu_data[cpu]) {
			if (intel_pstate_driver == &intel_pstate)
				intel_pstate_clear_update_util_hook(cpu);

			kfree(all_cpu_data[cpu]);
			all_cpu_data[cpu] = NULL;
		}
	}
	put_online_cpus();
	intel_pstate_driver = NULL;
}

static int intel_pstate_register_driver(struct cpufreq_driver *driver)
{
	int ret;

	memset(&global, 0, sizeof(global));
	global.max_perf_pct = 100;

	intel_pstate_driver = driver;
	ret = cpufreq_register_driver(intel_pstate_driver);
	if (ret) {
		intel_pstate_driver_cleanup();
		return ret;
	}

	global.min_perf_pct = min_perf_pct_min();

	return 0;
}

static int intel_pstate_unregister_driver(void)
{
	if (hwp_active)
		return -EBUSY;

	cpufreq_unregister_driver(intel_pstate_driver);
	intel_pstate_driver_cleanup();

	return 0;
}

static ssize_t intel_pstate_show_status(char *buf)
{
	if (!intel_pstate_driver)
		return sprintf(buf, "off\n");

	return sprintf(buf, "%s\n", intel_pstate_driver == &intel_pstate ?
					"active" : "passive");
}

static int intel_pstate_update_status(const char *buf, size_t size)
{
	int ret;

	if (size == 3 && !strncmp(buf, "off", size))
		return intel_pstate_driver ?
			intel_pstate_unregister_driver() : -EINVAL;

	if (size == 6 && !strncmp(buf, "active", size)) {
		if (intel_pstate_driver) {
			if (intel_pstate_driver == &intel_pstate)
				return 0;

			ret = intel_pstate_unregister_driver();
			if (ret)
				return ret;
		}

		return intel_pstate_register_driver(&intel_pstate);
	}

	if (size == 7 && !strncmp(buf, "passive", size)) {
		if (intel_pstate_driver) {
			if (intel_pstate_driver == &intel_cpufreq)
				return 0;

			ret = intel_pstate_unregister_driver();
			if (ret)
				return ret;
		}

		return intel_pstate_register_driver(&intel_cpufreq);
	}

	return -EINVAL;
}

static int no_load __initdata;
static int no_hwp __initdata;
static int hwp_only __initdata;
static unsigned int force_load __initdata;

static int __init intel_pstate_msrs_not_valid(void)
{
	if (!pstate_funcs.get_max() ||
	    !pstate_funcs.get_min() ||
	    !pstate_funcs.get_turbo())
		return -ENODEV;

	return 0;
}

static void __init copy_cpu_funcs(struct pstate_funcs *funcs)
{
	pstate_funcs.get_max   = funcs->get_max;
	pstate_funcs.get_max_physical = funcs->get_max_physical;
	pstate_funcs.get_min   = funcs->get_min;
	pstate_funcs.get_turbo = funcs->get_turbo;
	pstate_funcs.get_scaling = funcs->get_scaling;
	pstate_funcs.get_val   = funcs->get_val;
	pstate_funcs.get_vid   = funcs->get_vid;
	pstate_funcs.get_aperf_mperf_shift = funcs->get_aperf_mperf_shift;
}

#ifdef CONFIG_ACPI

static bool __init intel_pstate_no_acpi_pss(void)
{
	int i;

	for_each_possible_cpu(i) {
		acpi_status status;
		union acpi_object *pss;
		struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
		struct acpi_processor *pr = per_cpu(processors, i);

		if (!pr)
			continue;

		status = acpi_evaluate_object(pr->handle, "_PSS", NULL, &buffer);
		if (ACPI_FAILURE(status))
			continue;

		pss = buffer.pointer;
		if (pss && pss->type == ACPI_TYPE_PACKAGE) {
			kfree(pss);
			return false;
		}

		kfree(pss);
	}

	pr_debug("ACPI _PSS not found\n");
	return true;
}

static bool __init intel_pstate_no_acpi_pcch(void)
{
	acpi_status status;
	acpi_handle handle;

	status = acpi_get_handle(NULL, "\\_SB", &handle);
	if (ACPI_FAILURE(status))
		goto not_found;

	if (acpi_has_method(handle, "PCCH"))
		return false;

not_found:
	pr_debug("ACPI PCCH not found\n");
	return true;
}

static bool __init intel_pstate_has_acpi_ppc(void)
{
	int i;

	for_each_possible_cpu(i) {
		struct acpi_processor *pr = per_cpu(processors, i);

		if (!pr)
			continue;
		if (acpi_has_method(pr->handle, "_PPC"))
			return true;
	}
	pr_debug("ACPI _PPC not found\n");
	return false;
}

enum {
	PSS,
	PPC,
};

/* Hardware vendor-specific info that has its own power management modes */
static struct acpi_platform_list plat_info[] __initdata = {
	{"HP    ", "ProLiant", 0, ACPI_SIG_FADT, all_versions, 0, PSS},
	{"ORACLE", "X4-2    ", 0, ACPI_SIG_FADT, all_versions, 0, PPC},
	{"ORACLE", "X4-2L   ", 0, ACPI_SIG_FADT, all_versions, 0, PPC},
	{"ORACLE", "X4-2B   ", 0, ACPI_SIG_FADT, all_versions, 0, PPC},
	{"ORACLE", "X3-2    ", 0, ACPI_SIG_FADT, all_versions, 0, PPC},
	{"ORACLE", "X3-2L   ", 0, ACPI_SIG_FADT, all_versions, 0, PPC},
	{"ORACLE", "X3-2B   ", 0, ACPI_SIG_FADT, all_versions, 0, PPC},
	{"ORACLE", "X4470M2 ", 0, ACPI_SIG_FADT, all_versions, 0, PPC},
	{"ORACLE", "X4270M3 ", 0, ACPI_SIG_FADT, all_versions, 0, PPC},
	{"ORACLE", "X4270M2 ", 0, ACPI_SIG_FADT, all_versions, 0, PPC},
	{"ORACLE", "X4170M2 ", 0, ACPI_SIG_FADT, all_versions, 0, PPC},
	{"ORACLE", "X4170 M3", 0, ACPI_SIG_FADT, all_versions, 0, PPC},
	{"ORACLE", "X4275 M3", 0, ACPI_SIG_FADT, all_versions, 0, PPC},
	{"ORACLE", "X6-2    ", 0, ACPI_SIG_FADT, all_versions, 0, PPC},
	{"ORACLE", "Sudbury ", 0, ACPI_SIG_FADT, all_versions, 0, PPC},
	{ } /* End */
};

static bool __init intel_pstate_platform_pwr_mgmt_exists(void)
{
	const struct x86_cpu_id *id;
	u64 misc_pwr;
	int idx;

	id = x86_match_cpu(intel_pstate_cpu_oob_ids);
	if (id) {
		rdmsrl(MSR_MISC_PWR_MGMT, misc_pwr);
		if (misc_pwr & (1 << 8)) {
			pr_debug("Bit 8 in the MISC_PWR_MGMT MSR set\n");
			return true;
		}
	}

	idx = acpi_match_platform_list(plat_info);
	if (idx < 0)
		return false;

	switch (plat_info[idx].data) {
	case PSS:
		if (!intel_pstate_no_acpi_pss())
			return false;

		return intel_pstate_no_acpi_pcch();
	case PPC:
		return intel_pstate_has_acpi_ppc() && !force_load;
	}

	return false;
}

static void intel_pstate_request_control_from_smm(void)
{
	/*
	 * It may be unsafe to request P-states control from SMM if _PPC support
	 * has not been enabled.
	 */
	if (acpi_ppc)
		acpi_processor_pstate_control();
}
#else /* CONFIG_ACPI not enabled */
static inline bool intel_pstate_platform_pwr_mgmt_exists(void) { return false; }
static inline bool intel_pstate_has_acpi_ppc(void) { return false; }
static inline void intel_pstate_request_control_from_smm(void) {}
#endif /* CONFIG_ACPI */

#define INTEL_PSTATE_HWP_BROADWELL	0x01

#define ICPU_HWP(model, hwp_mode) \
	{ X86_VENDOR_INTEL, 6, model, X86_FEATURE_HWP, hwp_mode }

static const struct x86_cpu_id hwp_support_ids[] __initconst = {
	ICPU_HWP(INTEL_FAM6_BROADWELL_X, INTEL_PSTATE_HWP_BROADWELL),
	ICPU_HWP(INTEL_FAM6_BROADWELL_XEON_D, INTEL_PSTATE_HWP_BROADWELL),
	ICPU_HWP(X86_MODEL_ANY, 0),
	{}
};

static int __init intel_pstate_init(void)
{
	const struct x86_cpu_id *id;
	int rc;

	if (no_load)
		return -ENODEV;

	id = x86_match_cpu(hwp_support_ids);
	if (id) {
		copy_cpu_funcs(&core_funcs);
		if (!no_hwp) {
			hwp_active++;
			hwp_mode_bdw = id->driver_data;
			intel_pstate.attr = hwp_cpufreq_attrs;
			goto hwp_cpu_matched;
		}
	} else {
		id = x86_match_cpu(intel_pstate_cpu_ids);
		if (!id) {
			pr_info("CPU ID not supported\n");
			return -ENODEV;
		}

		copy_cpu_funcs((struct pstate_funcs *)id->driver_data);
	}

	if (intel_pstate_msrs_not_valid()) {
		pr_info("Invalid MSRs\n");
		return -ENODEV;
	}

hwp_cpu_matched:
	/*
	 * The Intel pstate driver will be ignored if the platform
	 * firmware has its own power management modes.
	 */
	if (intel_pstate_platform_pwr_mgmt_exists()) {
		pr_info("P-states controlled by the platform\n");
		return -ENODEV;
	}

	if (!hwp_active && hwp_only)
		return -ENOTSUPP;

	pr_info("Intel P-state driver initializing\n");

	all_cpu_data = vzalloc(array_size(sizeof(void *), num_possible_cpus()));
	if (!all_cpu_data)
		return -ENOMEM;

	intel_pstate_request_control_from_smm();

	intel_pstate_sysfs_expose_params();

	mutex_lock(&intel_pstate_driver_lock);
	rc = intel_pstate_register_driver(default_driver);
	mutex_unlock(&intel_pstate_driver_lock);
	if (rc)
		return rc;

	if (hwp_active)
		pr_info("HWP enabled\n");

	return 0;
}
device_initcall(intel_pstate_init);

static int __init intel_pstate_setup(char *str)
{
	if (!str)
		return -EINVAL;

	if (!strcmp(str, "disable")) {
		no_load = 1;
	} else if (!strcmp(str, "passive")) {
		pr_info("Passive mode enabled\n");
		default_driver = &intel_cpufreq;
		no_hwp = 1;
	}
	if (!strcmp(str, "no_hwp")) {
		pr_info("HWP disabled\n");
		no_hwp = 1;
	}
	if (!strcmp(str, "force"))
		force_load = 1;
	if (!strcmp(str, "hwp_only"))
		hwp_only = 1;
	if (!strcmp(str, "per_cpu_perf_limits"))
		per_cpu_limits = true;

#ifdef CONFIG_ACPI
	if (!strcmp(str, "support_acpi_ppc"))
		acpi_ppc = true;
#endif

	return 0;
}
early_param("intel_pstate", intel_pstate_setup);

MODULE_AUTHOR("Dirk Brandewie <dirk.j.brandewie@intel.com>");
MODULE_DESCRIPTION("'intel_pstate' - P state driver Intel Core processors");
MODULE_LICENSE("GPL");
