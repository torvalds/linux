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
#include <linux/debugfs.h>
#include <linux/acpi.h>
#include <linux/vmalloc.h>
#include <trace/events/power.h>

#include <asm/div64.h>
#include <asm/msr.h>
#include <asm/cpu_device_id.h>
#include <asm/cpufeature.h>
#include <asm/intel-family.h>

#define INTEL_CPUFREQ_TRANSITION_LATENCY	20000

#define ATOM_RATIOS		0x66a
#define ATOM_VIDS		0x66b
#define ATOM_TURBO_RATIOS	0x66c
#define ATOM_TURBO_VIDS		0x66d

#ifdef CONFIG_ACPI
#include <acpi/processor.h>
#include <acpi/cppc_acpi.h>
#endif

#define FRAC_BITS 8
#define int_tofp(X) ((int64_t)(X) << FRAC_BITS)
#define fp_toint(X) ((X) >> FRAC_BITS)

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

static inline u64 mul_ext_fp(u64 x, u64 y)
{
	return (x * y) >> EXT_FRAC_BITS;
}

static inline u64 div_ext_fp(u64 x, u64 y)
{
	return div64_u64(x << EXT_FRAC_BITS, y);
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
 * struct _pid -	Stores PID data
 * @setpoint:		Target set point for busyness or performance
 * @integral:		Storage for accumulated error values
 * @p_gain:		PID proportional gain
 * @i_gain:		PID integral gain
 * @d_gain:		PID derivative gain
 * @deadband:		PID deadband
 * @last_err:		Last error storage for integral part of PID calculation
 *
 * Stores PID coefficients and last error for PID controller.
 */
struct _pid {
	int setpoint;
	int32_t integral;
	int32_t p_gain;
	int32_t i_gain;
	int32_t d_gain;
	int deadband;
	int32_t last_err;
};

/**
 * struct perf_limits - Store user and policy limits
 * @no_turbo:		User requested turbo state from intel_pstate sysfs
 * @turbo_disabled:	Platform turbo status either from msr
 *			MSR_IA32_MISC_ENABLE or when maximum available pstate
 *			matches the maximum turbo pstate
 * @max_perf_pct:	Effective maximum performance limit in percentage, this
 *			is minimum of either limits enforced by cpufreq policy
 *			or limits from user set limits via intel_pstate sysfs
 * @min_perf_pct:	Effective minimum performance limit in percentage, this
 *			is maximum of either limits enforced by cpufreq policy
 *			or limits from user set limits via intel_pstate sysfs
 * @max_perf:		This is a scaled value between 0 to 255 for max_perf_pct
 *			This value is used to limit max pstate
 * @min_perf:		This is a scaled value between 0 to 255 for min_perf_pct
 *			This value is used to limit min pstate
 * @max_policy_pct:	The maximum performance in percentage enforced by
 *			cpufreq setpolicy interface
 * @max_sysfs_pct:	The maximum performance in percentage enforced by
 *			intel pstate sysfs interface, unused when per cpu
 *			controls are enforced
 * @min_policy_pct:	The minimum performance in percentage enforced by
 *			cpufreq setpolicy interface
 * @min_sysfs_pct:	The minimum performance in percentage enforced by
 *			intel pstate sysfs interface, unused when per cpu
 *			controls are enforced
 *
 * Storage for user and policy defined limits.
 */
struct perf_limits {
	int no_turbo;
	int turbo_disabled;
	int max_perf_pct;
	int min_perf_pct;
	int32_t max_perf;
	int32_t min_perf;
	int max_policy_pct;
	int max_sysfs_pct;
	int min_policy_pct;
	int min_sysfs_pct;
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
 * @pid:		Stores PID parameters for this CPU
 * @last_sample_time:	Last Sample time
 * @prev_aperf:		Last APERF value read from APERF MSR
 * @prev_mperf:		Last MPERF value read from MPERF MSR
 * @prev_tsc:		Last timestamp counter (TSC) value
 * @prev_cummulative_iowait: IO Wait time difference from last and
 *			current sample
 * @sample:		Storage for storing last Sample data
 * @perf_limits:	Pointer to perf_limit unique to this CPU
 *			Not all field in the structure are applicable
 *			when per cpu controls are enforced
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
	struct _pid pid;

	u64	last_update;
	u64	last_sample_time;
	u64	prev_aperf;
	u64	prev_mperf;
	u64	prev_tsc;
	u64	prev_cummulative_iowait;
	struct sample sample;
	struct perf_limits *perf_limits;
#ifdef CONFIG_ACPI
	struct acpi_processor_performance acpi_perf_data;
	bool valid_pss_table;
#endif
	unsigned int iowait_boost;
	s16 epp_powersave;
	s16 epp_policy;
	s16 epp_default;
	s16 epp_saved;
};

static struct cpudata **all_cpu_data;

/**
 * struct pstate_adjust_policy - Stores static PID configuration data
 * @sample_rate_ms:	PID calculation sample rate in ms
 * @sample_rate_ns:	Sample rate calculation in ns
 * @deadband:		PID deadband
 * @setpoint:		PID Setpoint
 * @p_gain_pct:		PID proportional gain
 * @i_gain_pct:		PID integral gain
 * @d_gain_pct:		PID derivative gain
 *
 * Stores per CPU model static PID configuration data.
 */
struct pstate_adjust_policy {
	int sample_rate_ms;
	s64 sample_rate_ns;
	int deadband;
	int setpoint;
	int p_gain_pct;
	int d_gain_pct;
	int i_gain_pct;
};

/**
 * struct pstate_funcs - Per CPU model specific callbacks
 * @get_max:		Callback to get maximum non turbo effective P state
 * @get_max_physical:	Callback to get maximum non turbo physical P state
 * @get_min:		Callback to get minimum P state
 * @get_turbo:		Callback to get turbo P state
 * @get_scaling:	Callback to get frequency scaling factor
 * @get_val:		Callback to convert P state to actual MSR write value
 * @get_vid:		Callback to get VID data for Atom platforms
 * @get_target_pstate:	Callback to a function to calculate next P state to use
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
	u64 (*get_val)(struct cpudata*, int pstate);
	void (*get_vid)(struct cpudata *);
	int32_t (*get_target_pstate)(struct cpudata *);
};

/**
 * struct cpu_defaults- Per CPU model default config data
 * @pid_policy:	PID config data
 * @funcs:		Callback function data
 */
struct cpu_defaults {
	struct pstate_adjust_policy pid_policy;
	struct pstate_funcs funcs;
};

static inline int32_t get_target_pstate_use_performance(struct cpudata *cpu);
static inline int32_t get_target_pstate_use_cpu_load(struct cpudata *cpu);

static struct pstate_adjust_policy pid_params __read_mostly;
static struct pstate_funcs pstate_funcs __read_mostly;
static int hwp_active __read_mostly;
static bool per_cpu_limits __read_mostly;

static bool driver_registered __read_mostly;

#ifdef CONFIG_ACPI
static bool acpi_ppc;
#endif

static struct perf_limits performance_limits = {
	.no_turbo = 0,
	.turbo_disabled = 0,
	.max_perf_pct = 100,
	.max_perf = int_ext_tofp(1),
	.min_perf_pct = 100,
	.min_perf = int_ext_tofp(1),
	.max_policy_pct = 100,
	.max_sysfs_pct = 100,
	.min_policy_pct = 0,
	.min_sysfs_pct = 0,
};

static struct perf_limits powersave_limits = {
	.no_turbo = 0,
	.turbo_disabled = 0,
	.max_perf_pct = 100,
	.max_perf = int_ext_tofp(1),
	.min_perf_pct = 0,
	.min_perf = 0,
	.max_policy_pct = 100,
	.max_sysfs_pct = 100,
	.min_policy_pct = 0,
	.min_sysfs_pct = 0,
};

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_PERFORMANCE
static struct perf_limits *limits = &performance_limits;
#else
static struct perf_limits *limits = &powersave_limits;
#endif

static DEFINE_MUTEX(intel_pstate_driver_lock);
static DEFINE_MUTEX(intel_pstate_limits_lock);

#ifdef CONFIG_ACPI

static bool intel_pstate_get_ppc_enable_status(void)
{
	if (acpi_gbl_FADT.preferred_profile == PM_ENTERPRISE_SERVER ||
	    acpi_gbl_FADT.preferred_profile == PM_PERFORMANCE_SERVER)
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
#else
static void intel_pstate_set_itmt_prio(int cpu)
{
}
#endif

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
	if (!limits->turbo_disabled)
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
#else
static inline void intel_pstate_init_acpi_perf_limits(struct cpufreq_policy *policy)
{
}

static inline void intel_pstate_exit_perf_limits(struct cpufreq_policy *policy)
{
}
#endif

static inline void pid_reset(struct _pid *pid, int setpoint, int busy,
			     int deadband, int integral) {
	pid->setpoint = int_tofp(setpoint);
	pid->deadband  = int_tofp(deadband);
	pid->integral  = int_tofp(integral);
	pid->last_err  = int_tofp(setpoint) - int_tofp(busy);
}

static inline void pid_p_gain_set(struct _pid *pid, int percent)
{
	pid->p_gain = div_fp(percent, 100);
}

static inline void pid_i_gain_set(struct _pid *pid, int percent)
{
	pid->i_gain = div_fp(percent, 100);
}

static inline void pid_d_gain_set(struct _pid *pid, int percent)
{
	pid->d_gain = div_fp(percent, 100);
}

static signed int pid_calc(struct _pid *pid, int32_t busy)
{
	signed int result;
	int32_t pterm, dterm, fp_error;
	int32_t integral_limit;

	fp_error = pid->setpoint - busy;

	if (abs(fp_error) <= pid->deadband)
		return 0;

	pterm = mul_fp(pid->p_gain, fp_error);

	pid->integral += fp_error;

	/*
	 * We limit the integral here so that it will never
	 * get higher than 30.  This prevents it from becoming
	 * too large an input over long periods of time and allows
	 * it to get factored out sooner.
	 *
	 * The value of 30 was chosen through experimentation.
	 */
	integral_limit = int_tofp(30);
	if (pid->integral > integral_limit)
		pid->integral = integral_limit;
	if (pid->integral < -integral_limit)
		pid->integral = -integral_limit;

	dterm = mul_fp(pid->d_gain, fp_error - pid->last_err);
	pid->last_err = fp_error;

	result = pterm + mul_fp(pid->integral, pid->i_gain) + dterm;
	result = result + (1 << (FRAC_BITS-1));
	return (signed int)fp_toint(result);
}

static inline void intel_pstate_busy_pid_reset(struct cpudata *cpu)
{
	pid_p_gain_set(&cpu->pid, pid_params.p_gain_pct);
	pid_d_gain_set(&cpu->pid, pid_params.d_gain_pct);
	pid_i_gain_set(&cpu->pid, pid_params.i_gain_pct);

	pid_reset(&cpu->pid, pid_params.setpoint, 100, pid_params.deadband, 0);
}

static inline void intel_pstate_reset_all_pid(void)
{
	unsigned int cpu;

	for_each_online_cpu(cpu) {
		if (all_cpu_data[cpu])
			intel_pstate_busy_pid_reset(all_cpu_data[cpu]);
	}
}

static inline void update_turbo_state(void)
{
	u64 misc_en;
	struct cpudata *cpu;

	cpu = all_cpu_data[0];
	rdmsrl(MSR_IA32_MISC_ENABLE, misc_en);
	limits->turbo_disabled =
		(misc_en & MSR_IA32_MISC_ENABLE_TURBO_DISABLE ||
		 cpu->pstate.max_pstate == cpu->pstate.turbo_pstate);
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

static int intel_pstate_get_energy_pref_index(struct cpudata *cpu_data)
{
	s16 epp;
	int index = -EINVAL;

	epp = intel_pstate_get_epp(cpu_data, 0);
	if (epp < 0)
		return epp;

	if (static_cpu_has(X86_FEATURE_HWP_EPP)) {
		/*
		 * Range:
		 *	0x00-0x3F	:	Performance
		 *	0x40-0x7F	:	Balance performance
		 *	0x80-0xBF	:	Balance power
		 *	0xC0-0xFF	:	Power
		 * The EPP is a 8 bit value, but our ranges restrict the
		 * value which can be set. Here only using top two bits
		 * effectively.
		 */
		index = (epp >> 6) + 1;
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

		/*
		 * If epp is not default, convert from index into
		 * energy_perf_strings to epp value, by shifting 6
		 * bits left to use only top two bits in epp.
		 * The resultant epp need to shifted by 24 bits to
		 * epp position in MSR_HWP_REQUEST.
		 */
		if (epp == -EINVAL)
			epp = (pref_index - 1) << 6;

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
	int ret, i = 0;

	ret = sscanf(buf, "%20s", str_preference);
	if (ret != 1)
		return -EINVAL;

	while (energy_perf_strings[i] != NULL) {
		if (!strcmp(str_preference, energy_perf_strings[i])) {
			intel_pstate_set_energy_pref_index(cpu_data, i);
			return count;
		}
		++i;
	}

	return -EINVAL;
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

static struct freq_attr *hwp_cpufreq_attrs[] = {
	&energy_performance_preference,
	&energy_performance_available_preferences,
	NULL,
};

static void intel_pstate_hwp_set(struct cpufreq_policy *policy)
{
	int min, hw_min, max, hw_max, cpu, range, adj_range;
	struct perf_limits *perf_limits = limits;
	u64 value, cap;

	for_each_cpu(cpu, policy->cpus) {
		int max_perf_pct, min_perf_pct;
		struct cpudata *cpu_data = all_cpu_data[cpu];
		s16 epp;

		if (per_cpu_limits)
			perf_limits = all_cpu_data[cpu]->perf_limits;

		rdmsrl_on_cpu(cpu, MSR_HWP_CAPABILITIES, &cap);
		hw_min = HWP_LOWEST_PERF(cap);
		if (limits->no_turbo)
			hw_max = HWP_GUARANTEED_PERF(cap);
		else
			hw_max = HWP_HIGHEST_PERF(cap);
		range = hw_max - hw_min;

		max_perf_pct = perf_limits->max_perf_pct;
		min_perf_pct = perf_limits->min_perf_pct;

		rdmsrl_on_cpu(cpu, MSR_HWP_REQUEST, &value);
		adj_range = min_perf_pct * range / 100;
		min = hw_min + adj_range;
		value &= ~HWP_MIN_PERF(~0L);
		value |= HWP_MIN_PERF(min);

		adj_range = max_perf_pct * range / 100;
		max = hw_min + adj_range;

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
		wrmsrl_on_cpu(cpu, MSR_HWP_REQUEST, value);
	}
}

static int intel_pstate_hwp_set_policy(struct cpufreq_policy *policy)
{
	if (hwp_active)
		intel_pstate_hwp_set(policy);

	return 0;
}

static int intel_pstate_hwp_save_state(struct cpufreq_policy *policy)
{
	struct cpudata *cpu_data = all_cpu_data[policy->cpu];

	if (!hwp_active)
		return 0;

	cpu_data->epp_saved = intel_pstate_get_epp(cpu_data, 0);

	return 0;
}

static int intel_pstate_resume(struct cpufreq_policy *policy)
{
	int ret;

	if (!hwp_active)
		return 0;

	mutex_lock(&intel_pstate_limits_lock);

	all_cpu_data[policy->cpu]->epp_policy = 0;

	ret = intel_pstate_hwp_set_policy(policy);

	mutex_unlock(&intel_pstate_limits_lock);

	return ret;
}

static void intel_pstate_update_policies(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		cpufreq_update_policy(cpu);
}

/************************** debugfs begin ************************/
static int pid_param_set(void *data, u64 val)
{
	*(u32 *)data = val;
	intel_pstate_reset_all_pid();
	return 0;
}

static int pid_param_get(void *data, u64 *val)
{
	*val = *(u32 *)data;
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(fops_pid_param, pid_param_get, pid_param_set, "%llu\n");

static struct dentry *debugfs_parent;

struct pid_param {
	char *name;
	void *value;
	struct dentry *dentry;
};

static struct pid_param pid_files[] = {
	{"sample_rate_ms", &pid_params.sample_rate_ms, },
	{"d_gain_pct", &pid_params.d_gain_pct, },
	{"i_gain_pct", &pid_params.i_gain_pct, },
	{"deadband", &pid_params.deadband, },
	{"setpoint", &pid_params.setpoint, },
	{"p_gain_pct", &pid_params.p_gain_pct, },
	{NULL, NULL, }
};

static void intel_pstate_debug_expose_params(void)
{
	int i;

	debugfs_parent = debugfs_create_dir("pstate_snb", NULL);
	if (IS_ERR_OR_NULL(debugfs_parent))
		return;

	for (i = 0; pid_files[i].name; i++) {
		struct dentry *dentry;

		dentry = debugfs_create_file(pid_files[i].name, 0660,
					     debugfs_parent, pid_files[i].value,
					     &fops_pid_param);
		if (!IS_ERR(dentry))
			pid_files[i].dentry = dentry;
	}
}

static void intel_pstate_debug_hide_params(void)
{
	int i;

	if (IS_ERR_OR_NULL(debugfs_parent))
		return;

	for (i = 0; pid_files[i].name; i++) {
		debugfs_remove(pid_files[i].dentry);
		pid_files[i].dentry = NULL;
	}

	debugfs_remove(debugfs_parent);
	debugfs_parent = NULL;
}

/************************** debugfs end ************************/

/************************** sysfs begin ************************/
#define show_one(file_name, object)					\
	static ssize_t show_##file_name					\
	(struct kobject *kobj, struct attribute *attr, char *buf)	\
	{								\
		return sprintf(buf, "%u\n", limits->object);		\
	}

static ssize_t intel_pstate_show_status(char *buf);
static int intel_pstate_update_status(const char *buf, size_t size);

static ssize_t show_status(struct kobject *kobj,
			   struct attribute *attr, char *buf)
{
	ssize_t ret;

	mutex_lock(&intel_pstate_driver_lock);
	ret = intel_pstate_show_status(buf);
	mutex_unlock(&intel_pstate_driver_lock);

	return ret;
}

static ssize_t store_status(struct kobject *a, struct attribute *b,
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
				struct attribute *attr, char *buf)
{
	struct cpudata *cpu;
	int total, no_turbo, turbo_pct;
	uint32_t turbo_fp;

	mutex_lock(&intel_pstate_driver_lock);

	if (!driver_registered) {
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
				struct attribute *attr, char *buf)
{
	struct cpudata *cpu;
	int total;

	mutex_lock(&intel_pstate_driver_lock);

	if (!driver_registered) {
		mutex_unlock(&intel_pstate_driver_lock);
		return -EAGAIN;
	}

	cpu = all_cpu_data[0];
	total = cpu->pstate.turbo_pstate - cpu->pstate.min_pstate + 1;

	mutex_unlock(&intel_pstate_driver_lock);

	return sprintf(buf, "%u\n", total);
}

static ssize_t show_no_turbo(struct kobject *kobj,
			     struct attribute *attr, char *buf)
{
	ssize_t ret;

	mutex_lock(&intel_pstate_driver_lock);

	if (!driver_registered) {
		mutex_unlock(&intel_pstate_driver_lock);
		return -EAGAIN;
	}

	update_turbo_state();
	if (limits->turbo_disabled)
		ret = sprintf(buf, "%u\n", limits->turbo_disabled);
	else
		ret = sprintf(buf, "%u\n", limits->no_turbo);

	mutex_unlock(&intel_pstate_driver_lock);

	return ret;
}

static ssize_t store_no_turbo(struct kobject *a, struct attribute *b,
			      const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	mutex_lock(&intel_pstate_driver_lock);

	if (!driver_registered) {
		mutex_unlock(&intel_pstate_driver_lock);
		return -EAGAIN;
	}

	mutex_lock(&intel_pstate_limits_lock);

	update_turbo_state();
	if (limits->turbo_disabled) {
		pr_warn("Turbo disabled by BIOS or unavailable on processor\n");
		mutex_unlock(&intel_pstate_limits_lock);
		mutex_unlock(&intel_pstate_driver_lock);
		return -EPERM;
	}

	limits->no_turbo = clamp_t(int, input, 0, 1);

	mutex_unlock(&intel_pstate_limits_lock);

	intel_pstate_update_policies();

	mutex_unlock(&intel_pstate_driver_lock);

	return count;
}

static ssize_t store_max_perf_pct(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	mutex_lock(&intel_pstate_driver_lock);

	if (!driver_registered) {
		mutex_unlock(&intel_pstate_driver_lock);
		return -EAGAIN;
	}

	mutex_lock(&intel_pstate_limits_lock);

	limits->max_sysfs_pct = clamp_t(int, input, 0 , 100);
	limits->max_perf_pct = min(limits->max_policy_pct,
				   limits->max_sysfs_pct);
	limits->max_perf_pct = max(limits->min_policy_pct,
				   limits->max_perf_pct);
	limits->max_perf_pct = max(limits->min_perf_pct,
				   limits->max_perf_pct);
	limits->max_perf = div_ext_fp(limits->max_perf_pct, 100);

	mutex_unlock(&intel_pstate_limits_lock);

	intel_pstate_update_policies();

	mutex_unlock(&intel_pstate_driver_lock);

	return count;
}

static ssize_t store_min_perf_pct(struct kobject *a, struct attribute *b,
				  const char *buf, size_t count)
{
	unsigned int input;
	int ret;

	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;

	mutex_lock(&intel_pstate_driver_lock);

	if (!driver_registered) {
		mutex_unlock(&intel_pstate_driver_lock);
		return -EAGAIN;
	}

	mutex_lock(&intel_pstate_limits_lock);

	limits->min_sysfs_pct = clamp_t(int, input, 0 , 100);
	limits->min_perf_pct = max(limits->min_policy_pct,
				   limits->min_sysfs_pct);
	limits->min_perf_pct = min(limits->max_policy_pct,
				   limits->min_perf_pct);
	limits->min_perf_pct = min(limits->max_perf_pct,
				   limits->min_perf_pct);
	limits->min_perf = div_ext_fp(limits->min_perf_pct, 100);

	mutex_unlock(&intel_pstate_limits_lock);

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

static struct attribute *intel_pstate_attributes[] = {
	&status.attr,
	&no_turbo.attr,
	&turbo_pct.attr,
	&num_pstates.attr,
	NULL
};

static struct attribute_group intel_pstate_attr_group = {
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

	rdmsrl(ATOM_RATIOS, value);
	return (value >> 8) & 0x7F;
}

static int atom_get_max_pstate(void)
{
	u64 value;

	rdmsrl(ATOM_RATIOS, value);
	return (value >> 16) & 0x7F;
}

static int atom_get_turbo_pstate(void)
{
	u64 value;

	rdmsrl(ATOM_TURBO_RATIOS, value);
	return value & 0x7F;
}

static u64 atom_get_val(struct cpudata *cpudata, int pstate)
{
	u64 val;
	int32_t vid_fp;
	u32 vid;

	val = (u64)pstate << 8;
	if (limits->no_turbo && !limits->turbo_disabled)
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

	rdmsrl(ATOM_VIDS, value);
	cpudata->vid.min = int_tofp((value >> 8) & 0x7f);
	cpudata->vid.max = int_tofp((value >> 16) & 0x7f);
	cpudata->vid.ratio = div_fp(
		cpudata->vid.max - cpudata->vid.min,
		int_tofp(cpudata->pstate.max_pstate -
			cpudata->pstate.min_pstate));

	rdmsrl(ATOM_TURBO_VIDS, value);
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
	if (limits->no_turbo && !limits->turbo_disabled)
		val |= (u64)1 << 32;

	return val;
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

static struct cpu_defaults core_params = {
	.pid_policy = {
		.sample_rate_ms = 10,
		.deadband = 0,
		.setpoint = 97,
		.p_gain_pct = 20,
		.d_gain_pct = 0,
		.i_gain_pct = 0,
	},
	.funcs = {
		.get_max = core_get_max_pstate,
		.get_max_physical = core_get_max_pstate_physical,
		.get_min = core_get_min_pstate,
		.get_turbo = core_get_turbo_pstate,
		.get_scaling = core_get_scaling,
		.get_val = core_get_val,
		.get_target_pstate = get_target_pstate_use_performance,
	},
};

static const struct cpu_defaults silvermont_params = {
	.pid_policy = {
		.sample_rate_ms = 10,
		.deadband = 0,
		.setpoint = 60,
		.p_gain_pct = 14,
		.d_gain_pct = 0,
		.i_gain_pct = 4,
	},
	.funcs = {
		.get_max = atom_get_max_pstate,
		.get_max_physical = atom_get_max_pstate,
		.get_min = atom_get_min_pstate,
		.get_turbo = atom_get_turbo_pstate,
		.get_val = atom_get_val,
		.get_scaling = silvermont_get_scaling,
		.get_vid = atom_get_vid,
		.get_target_pstate = get_target_pstate_use_cpu_load,
	},
};

static const struct cpu_defaults airmont_params = {
	.pid_policy = {
		.sample_rate_ms = 10,
		.deadband = 0,
		.setpoint = 60,
		.p_gain_pct = 14,
		.d_gain_pct = 0,
		.i_gain_pct = 4,
	},
	.funcs = {
		.get_max = atom_get_max_pstate,
		.get_max_physical = atom_get_max_pstate,
		.get_min = atom_get_min_pstate,
		.get_turbo = atom_get_turbo_pstate,
		.get_val = atom_get_val,
		.get_scaling = airmont_get_scaling,
		.get_vid = atom_get_vid,
		.get_target_pstate = get_target_pstate_use_cpu_load,
	},
};

static const struct cpu_defaults knl_params = {
	.pid_policy = {
		.sample_rate_ms = 10,
		.deadband = 0,
		.setpoint = 97,
		.p_gain_pct = 20,
		.d_gain_pct = 0,
		.i_gain_pct = 0,
	},
	.funcs = {
		.get_max = core_get_max_pstate,
		.get_max_physical = core_get_max_pstate_physical,
		.get_min = core_get_min_pstate,
		.get_turbo = knl_get_turbo_pstate,
		.get_scaling = core_get_scaling,
		.get_val = core_get_val,
		.get_target_pstate = get_target_pstate_use_performance,
	},
};

static const struct cpu_defaults bxt_params = {
	.pid_policy = {
		.sample_rate_ms = 10,
		.deadband = 0,
		.setpoint = 60,
		.p_gain_pct = 14,
		.d_gain_pct = 0,
		.i_gain_pct = 4,
	},
	.funcs = {
		.get_max = core_get_max_pstate,
		.get_max_physical = core_get_max_pstate_physical,
		.get_min = core_get_min_pstate,
		.get_turbo = core_get_turbo_pstate,
		.get_scaling = core_get_scaling,
		.get_val = core_get_val,
		.get_target_pstate = get_target_pstate_use_cpu_load,
	},
};

static void intel_pstate_get_min_max(struct cpudata *cpu, int *min, int *max)
{
	int max_perf = cpu->pstate.turbo_pstate;
	int max_perf_adj;
	int min_perf;
	struct perf_limits *perf_limits = limits;

	if (limits->no_turbo || limits->turbo_disabled)
		max_perf = cpu->pstate.max_pstate;

	if (per_cpu_limits)
		perf_limits = cpu->perf_limits;

	/*
	 * performance can be limited by user through sysfs, by cpufreq
	 * policy, or by cpu specific default values determined through
	 * experimentation.
	 */
	max_perf_adj = fp_ext_toint(max_perf * perf_limits->max_perf);
	*max = clamp_t(int, max_perf_adj,
			cpu->pstate.min_pstate, cpu->pstate.turbo_pstate);

	min_perf = fp_ext_toint(max_perf * perf_limits->min_perf);
	*min = clamp_t(int, min_perf, cpu->pstate.min_pstate, max_perf);
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
	int min_pstate, max_pstate;

	update_turbo_state();
	intel_pstate_get_min_max(cpu, &min_pstate, &max_pstate);
	intel_pstate_set_pstate(cpu, max_pstate);
}

static void intel_pstate_get_cpu_pstates(struct cpudata *cpu)
{
	cpu->pstate.min_pstate = pstate_funcs.get_min();
	cpu->pstate.max_pstate = pstate_funcs.get_max();
	cpu->pstate.max_pstate_physical = pstate_funcs.get_max_physical();
	cpu->pstate.turbo_pstate = pstate_funcs.get_turbo();
	cpu->pstate.scaling = pstate_funcs.get_scaling();
	cpu->pstate.max_freq = cpu->pstate.max_pstate * cpu->pstate.scaling;
	cpu->pstate.turbo_freq = cpu->pstate.turbo_pstate * cpu->pstate.scaling;

	if (pstate_funcs.get_vid)
		pstate_funcs.get_vid(cpu);

	intel_pstate_set_min_pstate(cpu);
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
	return !!cpu->last_sample_time;
}

static inline int32_t get_avg_frequency(struct cpudata *cpu)
{
	return mul_ext_fp(cpu->sample.core_avg_perf,
			  cpu->pstate.max_pstate_physical * cpu->pstate.scaling);
}

static inline int32_t get_avg_pstate(struct cpudata *cpu)
{
	return mul_ext_fp(cpu->pstate.max_pstate_physical,
			  cpu->sample.core_avg_perf);
}

static inline int32_t get_target_pstate_use_cpu_load(struct cpudata *cpu)
{
	struct sample *sample = &cpu->sample;
	int32_t busy_frac, boost;
	int target, avg_pstate;

	busy_frac = div_fp(sample->mperf, sample->tsc);

	boost = cpu->iowait_boost;
	cpu->iowait_boost >>= 1;

	if (busy_frac < boost)
		busy_frac = boost;

	sample->busy_scaled = busy_frac * 100;

	target = limits->no_turbo || limits->turbo_disabled ?
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

static inline int32_t get_target_pstate_use_performance(struct cpudata *cpu)
{
	int32_t perf_scaled, max_pstate, current_pstate, sample_ratio;
	u64 duration_ns;

	/*
	 * perf_scaled is the ratio of the average P-state during the last
	 * sampling period to the P-state requested last time (in percent).
	 *
	 * That measures the system's response to the previous P-state
	 * selection.
	 */
	max_pstate = cpu->pstate.max_pstate_physical;
	current_pstate = cpu->pstate.current_pstate;
	perf_scaled = mul_ext_fp(cpu->sample.core_avg_perf,
			       div_fp(100 * max_pstate, current_pstate));

	/*
	 * Since our utilization update callback will not run unless we are
	 * in C0, check if the actual elapsed time is significantly greater (3x)
	 * than our sample interval.  If it is, then we were idle for a long
	 * enough period of time to adjust our performance metric.
	 */
	duration_ns = cpu->sample.time - cpu->last_sample_time;
	if ((s64)duration_ns > pid_params.sample_rate_ns * 3) {
		sample_ratio = div_fp(pid_params.sample_rate_ns, duration_ns);
		perf_scaled = mul_fp(perf_scaled, sample_ratio);
	} else {
		sample_ratio = div_fp(100 * cpu->sample.mperf, cpu->sample.tsc);
		if (sample_ratio < int_tofp(1))
			perf_scaled = 0;
	}

	cpu->sample.busy_scaled = perf_scaled;
	return cpu->pstate.current_pstate - pid_calc(&cpu->pid, perf_scaled);
}

static int intel_pstate_prepare_request(struct cpudata *cpu, int pstate)
{
	int max_perf, min_perf;

	intel_pstate_get_min_max(cpu, &min_perf, &max_perf);
	pstate = clamp_t(int, pstate, min_perf, max_perf);
	trace_cpu_frequency(pstate * cpu->pstate.scaling, cpu->cpu);
	return pstate;
}

static void intel_pstate_update_pstate(struct cpudata *cpu, int pstate)
{
	pstate = intel_pstate_prepare_request(cpu, pstate);
	if (pstate == cpu->pstate.current_pstate)
		return;

	cpu->pstate.current_pstate = pstate;
	wrmsrl(MSR_IA32_PERF_CTL, pstate_funcs.get_val(cpu, pstate));
}

static inline void intel_pstate_adjust_busy_pstate(struct cpudata *cpu)
{
	int from, target_pstate;
	struct sample *sample;

	from = cpu->pstate.current_pstate;

	target_pstate = cpu->policy == CPUFREQ_POLICY_PERFORMANCE ?
		cpu->pstate.turbo_pstate : pstate_funcs.get_target_pstate(cpu);

	update_turbo_state();

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

	if (pstate_funcs.get_target_pstate == get_target_pstate_use_cpu_load) {
		if (flags & SCHED_CPUFREQ_IOWAIT) {
			cpu->iowait_boost = int_tofp(1);
		} else if (cpu->iowait_boost) {
			/* Clear iowait_boost if the CPU may have been idle. */
			delta_ns = time - cpu->last_update;
			if (delta_ns > TICK_NSEC)
				cpu->iowait_boost = 0;
		}
		cpu->last_update = time;
	}

	delta_ns = time - cpu->sample.time;
	if ((s64)delta_ns >= pid_params.sample_rate_ns) {
		bool sample_taken = intel_pstate_sample(cpu, time);

		if (sample_taken) {
			intel_pstate_calc_avg_perf(cpu);
			if (!hwp_active)
				intel_pstate_adjust_busy_pstate(cpu);
		}
	}
}

#define ICPU(model, policy) \
	{ X86_VENDOR_INTEL, 6, model, X86_FEATURE_APERFMPERF,\
			(unsigned long)&policy }

static const struct x86_cpu_id intel_pstate_cpu_ids[] = {
	ICPU(INTEL_FAM6_SANDYBRIDGE, 		core_params),
	ICPU(INTEL_FAM6_SANDYBRIDGE_X,		core_params),
	ICPU(INTEL_FAM6_ATOM_SILVERMONT1,	silvermont_params),
	ICPU(INTEL_FAM6_IVYBRIDGE,		core_params),
	ICPU(INTEL_FAM6_HASWELL_CORE,		core_params),
	ICPU(INTEL_FAM6_BROADWELL_CORE,		core_params),
	ICPU(INTEL_FAM6_IVYBRIDGE_X,		core_params),
	ICPU(INTEL_FAM6_HASWELL_X,		core_params),
	ICPU(INTEL_FAM6_HASWELL_ULT,		core_params),
	ICPU(INTEL_FAM6_HASWELL_GT3E,		core_params),
	ICPU(INTEL_FAM6_BROADWELL_GT3E,		core_params),
	ICPU(INTEL_FAM6_ATOM_AIRMONT,		airmont_params),
	ICPU(INTEL_FAM6_SKYLAKE_MOBILE,		core_params),
	ICPU(INTEL_FAM6_BROADWELL_X,		core_params),
	ICPU(INTEL_FAM6_SKYLAKE_DESKTOP,	core_params),
	ICPU(INTEL_FAM6_BROADWELL_XEON_D,	core_params),
	ICPU(INTEL_FAM6_XEON_PHI_KNL,		knl_params),
	ICPU(INTEL_FAM6_XEON_PHI_KNM,		knl_params),
	ICPU(INTEL_FAM6_ATOM_GOLDMONT,		bxt_params),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, intel_pstate_cpu_ids);

static const struct x86_cpu_id intel_pstate_cpu_oob_ids[] __initconst = {
	ICPU(INTEL_FAM6_BROADWELL_XEON_D, core_params),
	ICPU(INTEL_FAM6_BROADWELL_X, core_params),
	ICPU(INTEL_FAM6_SKYLAKE_X, core_params),
	{}
};

static const struct x86_cpu_id intel_pstate_cpu_ee_disable_ids[] = {
	ICPU(INTEL_FAM6_KABYLAKE_DESKTOP, core_params),
	{}
};

static int intel_pstate_init_cpu(unsigned int cpunum)
{
	struct cpudata *cpu;

	cpu = all_cpu_data[cpunum];

	if (!cpu) {
		unsigned int size = sizeof(struct cpudata);

		if (per_cpu_limits)
			size += sizeof(struct perf_limits);

		cpu = kzalloc(size, GFP_KERNEL);
		if (!cpu)
			return -ENOMEM;

		all_cpu_data[cpunum] = cpu;
		if (per_cpu_limits)
			cpu->perf_limits = (struct perf_limits *)(cpu + 1);

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
		pid_params.sample_rate_ms = 50;
		pid_params.sample_rate_ns = 50 * NSEC_PER_MSEC;
	}

	intel_pstate_get_cpu_pstates(cpu);

	intel_pstate_busy_pid_reset(cpu);

	pr_debug("controlling: cpu %d\n", cpunum);

	return 0;
}

static unsigned int intel_pstate_get(unsigned int cpu_num)
{
	struct cpudata *cpu = all_cpu_data[cpu_num];

	return cpu ? get_avg_frequency(cpu) : 0;
}

static void intel_pstate_set_update_util_hook(unsigned int cpu_num)
{
	struct cpudata *cpu = all_cpu_data[cpu_num];

	if (cpu->update_util_set)
		return;

	/* Prevent intel_pstate_update_util() from using stale data. */
	cpu->sample.time = 0;
	cpufreq_add_update_util_hook(cpu_num, &cpu->update_util,
				     intel_pstate_update_util);
	cpu->update_util_set = true;
}

static void intel_pstate_clear_update_util_hook(unsigned int cpu)
{
	struct cpudata *cpu_data = all_cpu_data[cpu];

	if (!cpu_data->update_util_set)
		return;

	cpufreq_remove_update_util_hook(cpu);
	cpu_data->update_util_set = false;
	synchronize_sched();
}

static void intel_pstate_set_performance_limits(struct perf_limits *limits)
{
	limits->no_turbo = 0;
	limits->turbo_disabled = 0;
	limits->max_perf_pct = 100;
	limits->max_perf = int_ext_tofp(1);
	limits->min_perf_pct = 100;
	limits->min_perf = int_ext_tofp(1);
	limits->max_policy_pct = 100;
	limits->max_sysfs_pct = 100;
	limits->min_policy_pct = 0;
	limits->min_sysfs_pct = 0;
}

static void intel_pstate_update_perf_limits(struct cpufreq_policy *policy,
					    struct perf_limits *limits)
{

	limits->max_policy_pct = DIV_ROUND_UP(policy->max * 100,
					      policy->cpuinfo.max_freq);
	limits->max_policy_pct = clamp_t(int, limits->max_policy_pct, 0, 100);
	if (policy->max == policy->min) {
		limits->min_policy_pct = limits->max_policy_pct;
	} else {
		limits->min_policy_pct = DIV_ROUND_UP(policy->min * 100,
						      policy->cpuinfo.max_freq);
		limits->min_policy_pct = clamp_t(int, limits->min_policy_pct,
						 0, 100);
	}

	/* Normalize user input to [min_policy_pct, max_policy_pct] */
	limits->min_perf_pct = max(limits->min_policy_pct,
				   limits->min_sysfs_pct);
	limits->min_perf_pct = min(limits->max_policy_pct,
				   limits->min_perf_pct);
	limits->max_perf_pct = min(limits->max_policy_pct,
				   limits->max_sysfs_pct);
	limits->max_perf_pct = max(limits->min_policy_pct,
				   limits->max_perf_pct);

	/* Make sure min_perf_pct <= max_perf_pct */
	limits->min_perf_pct = min(limits->max_perf_pct, limits->min_perf_pct);

	limits->min_perf = div_ext_fp(limits->min_perf_pct, 100);
	limits->max_perf = div_ext_fp(limits->max_perf_pct, 100);
	limits->max_perf = round_up(limits->max_perf, EXT_FRAC_BITS);
	limits->min_perf = round_up(limits->min_perf, EXT_FRAC_BITS);

	pr_debug("cpu:%d max_perf_pct:%d min_perf_pct:%d\n", policy->cpu,
		 limits->max_perf_pct, limits->min_perf_pct);
}

static int intel_pstate_set_policy(struct cpufreq_policy *policy)
{
	struct cpudata *cpu;
	struct perf_limits *perf_limits = NULL;

	if (!policy->cpuinfo.max_freq)
		return -ENODEV;

	pr_debug("set_policy cpuinfo.max %u policy->max %u\n",
		 policy->cpuinfo.max_freq, policy->max);

	cpu = all_cpu_data[policy->cpu];
	cpu->policy = policy->policy;

	if (cpu->pstate.max_pstate_physical > cpu->pstate.max_pstate &&
	    policy->max < policy->cpuinfo.max_freq &&
	    policy->max > cpu->pstate.max_pstate * cpu->pstate.scaling) {
		pr_debug("policy->max > max non turbo frequency\n");
		policy->max = policy->cpuinfo.max_freq;
	}

	if (per_cpu_limits)
		perf_limits = cpu->perf_limits;

	mutex_lock(&intel_pstate_limits_lock);

	if (policy->policy == CPUFREQ_POLICY_PERFORMANCE) {
		if (!perf_limits) {
			limits = &performance_limits;
			perf_limits = limits;
		}
		if (policy->max >= policy->cpuinfo.max_freq &&
		    !limits->no_turbo) {
			pr_debug("set performance\n");
			intel_pstate_set_performance_limits(perf_limits);
			goto out;
		}
	} else {
		pr_debug("set powersave\n");
		if (!perf_limits) {
			limits = &powersave_limits;
			perf_limits = limits;
		}

	}

	intel_pstate_update_perf_limits(policy, perf_limits);
 out:
	if (cpu->policy == CPUFREQ_POLICY_PERFORMANCE) {
		/*
		 * NOHZ_FULL CPUs need this as the governor callback may not
		 * be invoked on them.
		 */
		intel_pstate_clear_update_util_hook(policy->cpu);
		intel_pstate_max_within_limits(cpu);
	}

	intel_pstate_set_update_util_hook(policy->cpu);

	intel_pstate_hwp_set_policy(policy);

	mutex_unlock(&intel_pstate_limits_lock);

	return 0;
}

static int intel_pstate_verify_policy(struct cpufreq_policy *policy)
{
	struct cpudata *cpu = all_cpu_data[policy->cpu];
	struct perf_limits *perf_limits;

	if (policy->policy == CPUFREQ_POLICY_PERFORMANCE)
		perf_limits = &performance_limits;
	else
		perf_limits = &powersave_limits;

	update_turbo_state();
	policy->cpuinfo.max_freq = perf_limits->turbo_disabled ||
					perf_limits->no_turbo ?
					cpu->pstate.max_freq :
					cpu->pstate.turbo_freq;

	cpufreq_verify_within_cpu_limits(policy);

	if (policy->policy != CPUFREQ_POLICY_POWERSAVE &&
	    policy->policy != CPUFREQ_POLICY_PERFORMANCE)
		return -EINVAL;

	/* When per-CPU limits are used, sysfs limits are not used */
	if (!per_cpu_limits) {
		unsigned int max_freq, min_freq;

		max_freq = policy->cpuinfo.max_freq *
						limits->max_sysfs_pct / 100;
		min_freq = policy->cpuinfo.max_freq *
						limits->min_sysfs_pct / 100;
		cpufreq_verify_within_limits(policy, min_freq, max_freq);
	}

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
	if (hwp_active)
		intel_pstate_hwp_save_state(policy);
	else
		intel_cpufreq_stop_cpu(policy);
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

	/*
	 * We need sane value in the cpu->perf_limits, so inherit from global
	 * perf_limits limits, which are seeded with values based on the
	 * CONFIG_CPU_FREQ_DEFAULT_GOV_*, during boot up.
	 */
	if (per_cpu_limits)
		memcpy(cpu->perf_limits, limits, sizeof(struct perf_limits));

	policy->min = cpu->pstate.min_pstate * cpu->pstate.scaling;
	policy->max = cpu->pstate.turbo_pstate * cpu->pstate.scaling;

	/* cpuinfo and default policy values */
	policy->cpuinfo.min_freq = cpu->pstate.min_pstate * cpu->pstate.scaling;
	update_turbo_state();
	policy->cpuinfo.max_freq = limits->turbo_disabled ?
			cpu->pstate.max_pstate : cpu->pstate.turbo_pstate;
	policy->cpuinfo.max_freq *= cpu->pstate.scaling;

	intel_pstate_init_acpi_perf_limits(policy);
	cpumask_set_cpu(policy->cpu, policy->cpus);

	policy->fast_switch_possible = true;

	return 0;
}

static int intel_pstate_cpu_init(struct cpufreq_policy *policy)
{
	int ret = __intel_pstate_cpu_init(policy);

	if (ret)
		return ret;

	policy->cpuinfo.transition_latency = CPUFREQ_ETERNAL;
	if (limits->min_perf_pct == 100 && limits->max_perf_pct == 100)
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
	.get		= intel_pstate_get,
	.init		= intel_pstate_cpu_init,
	.exit		= intel_pstate_cpu_exit,
	.stop_cpu	= intel_pstate_stop_cpu,
	.name		= "intel_pstate",
};

static int intel_cpufreq_verify_policy(struct cpufreq_policy *policy)
{
	struct cpudata *cpu = all_cpu_data[policy->cpu];
	struct perf_limits *perf_limits = limits;

	update_turbo_state();
	policy->cpuinfo.max_freq = limits->turbo_disabled ?
			cpu->pstate.max_freq : cpu->pstate.turbo_freq;

	cpufreq_verify_within_cpu_limits(policy);

	if (per_cpu_limits)
		perf_limits = cpu->perf_limits;

	mutex_lock(&intel_pstate_limits_lock);

	intel_pstate_update_perf_limits(policy, perf_limits);

	mutex_unlock(&intel_pstate_limits_lock);

	return 0;
}

static unsigned int intel_cpufreq_turbo_update(struct cpudata *cpu,
					       struct cpufreq_policy *policy,
					       unsigned int target_freq)
{
	unsigned int max_freq;

	update_turbo_state();

	max_freq = limits->no_turbo || limits->turbo_disabled ?
			cpu->pstate.max_freq : cpu->pstate.turbo_freq;
	policy->cpuinfo.max_freq = max_freq;
	if (policy->max > max_freq)
		policy->max = max_freq;

	if (target_freq > max_freq)
		target_freq = max_freq;

	return target_freq;
}

static int intel_cpufreq_target(struct cpufreq_policy *policy,
				unsigned int target_freq,
				unsigned int relation)
{
	struct cpudata *cpu = all_cpu_data[policy->cpu];
	struct cpufreq_freqs freqs;
	int target_pstate;

	freqs.old = policy->cur;
	freqs.new = intel_cpufreq_turbo_update(cpu, policy, target_freq);

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
	if (target_pstate != cpu->pstate.current_pstate) {
		cpu->pstate.current_pstate = target_pstate;
		wrmsrl_on_cpu(policy->cpu, MSR_IA32_PERF_CTL,
			      pstate_funcs.get_val(cpu, target_pstate));
	}
	cpufreq_freq_transition_end(policy, &freqs, false);

	return 0;
}

static unsigned int intel_cpufreq_fast_switch(struct cpufreq_policy *policy,
					      unsigned int target_freq)
{
	struct cpudata *cpu = all_cpu_data[policy->cpu];
	int target_pstate;

	target_freq = intel_cpufreq_turbo_update(cpu, policy, target_freq);
	target_pstate = DIV_ROUND_UP(target_freq, cpu->pstate.scaling);
	intel_pstate_update_pstate(cpu, target_pstate);
	return target_freq;
}

static int intel_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	int ret = __intel_pstate_cpu_init(policy);

	if (ret)
		return ret;

	policy->cpuinfo.transition_latency = INTEL_CPUFREQ_TRANSITION_LATENCY;
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

static struct cpufreq_driver *intel_pstate_driver = &intel_pstate;

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
}

static int intel_pstate_register_driver(void)
{
	int ret;

	ret = cpufreq_register_driver(intel_pstate_driver);
	if (ret) {
		intel_pstate_driver_cleanup();
		return ret;
	}

	mutex_lock(&intel_pstate_limits_lock);
	driver_registered = true;
	mutex_unlock(&intel_pstate_limits_lock);

	if (intel_pstate_driver == &intel_pstate && !hwp_active &&
	    pstate_funcs.get_target_pstate != get_target_pstate_use_cpu_load)
		intel_pstate_debug_expose_params();

	return 0;
}

static int intel_pstate_unregister_driver(void)
{
	if (hwp_active)
		return -EBUSY;

	if (intel_pstate_driver == &intel_pstate && !hwp_active &&
	    pstate_funcs.get_target_pstate != get_target_pstate_use_cpu_load)
		intel_pstate_debug_hide_params();

	mutex_lock(&intel_pstate_limits_lock);
	driver_registered = false;
	mutex_unlock(&intel_pstate_limits_lock);

	cpufreq_unregister_driver(intel_pstate_driver);
	intel_pstate_driver_cleanup();

	return 0;
}

static ssize_t intel_pstate_show_status(char *buf)
{
	if (!driver_registered)
		return sprintf(buf, "off\n");

	return sprintf(buf, "%s\n", intel_pstate_driver == &intel_pstate ?
					"active" : "passive");
}

static int intel_pstate_update_status(const char *buf, size_t size)
{
	int ret;

	if (size == 3 && !strncmp(buf, "off", size))
		return driver_registered ?
			intel_pstate_unregister_driver() : -EINVAL;

	if (size == 6 && !strncmp(buf, "active", size)) {
		if (driver_registered) {
			if (intel_pstate_driver == &intel_pstate)
				return 0;

			ret = intel_pstate_unregister_driver();
			if (ret)
				return ret;
		}

		intel_pstate_driver = &intel_pstate;
		return intel_pstate_register_driver();
	}

	if (size == 7 && !strncmp(buf, "passive", size)) {
		if (driver_registered) {
			if (intel_pstate_driver != &intel_pstate)
				return 0;

			ret = intel_pstate_unregister_driver();
			if (ret)
				return ret;
		}

		intel_pstate_driver = &intel_cpufreq;
		return intel_pstate_register_driver();
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

static void __init copy_pid_params(struct pstate_adjust_policy *policy)
{
	pid_params.sample_rate_ms = policy->sample_rate_ms;
	pid_params.sample_rate_ns = pid_params.sample_rate_ms * NSEC_PER_MSEC;
	pid_params.p_gain_pct = policy->p_gain_pct;
	pid_params.i_gain_pct = policy->i_gain_pct;
	pid_params.d_gain_pct = policy->d_gain_pct;
	pid_params.deadband = policy->deadband;
	pid_params.setpoint = policy->setpoint;
}

#ifdef CONFIG_ACPI
static void intel_pstate_use_acpi_profile(void)
{
	if (acpi_gbl_FADT.preferred_profile == PM_MOBILE)
		pstate_funcs.get_target_pstate =
				get_target_pstate_use_cpu_load;
}
#else
static void intel_pstate_use_acpi_profile(void)
{
}
#endif

static void __init copy_cpu_funcs(struct pstate_funcs *funcs)
{
	pstate_funcs.get_max   = funcs->get_max;
	pstate_funcs.get_max_physical = funcs->get_max_physical;
	pstate_funcs.get_min   = funcs->get_min;
	pstate_funcs.get_turbo = funcs->get_turbo;
	pstate_funcs.get_scaling = funcs->get_scaling;
	pstate_funcs.get_val   = funcs->get_val;
	pstate_funcs.get_vid   = funcs->get_vid;
	pstate_funcs.get_target_pstate = funcs->get_target_pstate;

	intel_pstate_use_acpi_profile();
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
	return false;
}

enum {
	PSS,
	PPC,
};

struct hw_vendor_info {
	u16  valid;
	char oem_id[ACPI_OEM_ID_SIZE];
	char oem_table_id[ACPI_OEM_TABLE_ID_SIZE];
	int  oem_pwr_table;
};

/* Hardware vendor-specific info that has its own power management modes */
static struct hw_vendor_info vendor_info[] __initdata = {
	{1, "HP    ", "ProLiant", PSS},
	{1, "ORACLE", "X4-2    ", PPC},
	{1, "ORACLE", "X4-2L   ", PPC},
	{1, "ORACLE", "X4-2B   ", PPC},
	{1, "ORACLE", "X3-2    ", PPC},
	{1, "ORACLE", "X3-2L   ", PPC},
	{1, "ORACLE", "X3-2B   ", PPC},
	{1, "ORACLE", "X4470M2 ", PPC},
	{1, "ORACLE", "X4270M3 ", PPC},
	{1, "ORACLE", "X4270M2 ", PPC},
	{1, "ORACLE", "X4170M2 ", PPC},
	{1, "ORACLE", "X4170 M3", PPC},
	{1, "ORACLE", "X4275 M3", PPC},
	{1, "ORACLE", "X6-2    ", PPC},
	{1, "ORACLE", "Sudbury ", PPC},
	{0, "", ""},
};

static bool __init intel_pstate_platform_pwr_mgmt_exists(void)
{
	struct acpi_table_header hdr;
	struct hw_vendor_info *v_info;
	const struct x86_cpu_id *id;
	u64 misc_pwr;

	id = x86_match_cpu(intel_pstate_cpu_oob_ids);
	if (id) {
		rdmsrl(MSR_MISC_PWR_MGMT, misc_pwr);
		if ( misc_pwr & (1 << 8))
			return true;
	}

	if (acpi_disabled ||
	    ACPI_FAILURE(acpi_get_table_header(ACPI_SIG_FADT, 0, &hdr)))
		return false;

	for (v_info = vendor_info; v_info->valid; v_info++) {
		if (!strncmp(hdr.oem_id, v_info->oem_id, ACPI_OEM_ID_SIZE) &&
			!strncmp(hdr.oem_table_id, v_info->oem_table_id,
						ACPI_OEM_TABLE_ID_SIZE))
			switch (v_info->oem_pwr_table) {
			case PSS:
				return intel_pstate_no_acpi_pss();
			case PPC:
				return intel_pstate_has_acpi_ppc() &&
					(!force_load);
			}
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

static const struct x86_cpu_id hwp_support_ids[] __initconst = {
	{ X86_VENDOR_INTEL, 6, X86_MODEL_ANY, X86_FEATURE_HWP },
	{}
};

static int __init intel_pstate_init(void)
{
	const struct x86_cpu_id *id;
	struct cpu_defaults *cpu_def;
	int rc = 0;

	if (no_load)
		return -ENODEV;

	if (x86_match_cpu(hwp_support_ids) && !no_hwp) {
		copy_cpu_funcs(&core_params.funcs);
		hwp_active++;
		intel_pstate.attr = hwp_cpufreq_attrs;
		goto hwp_cpu_matched;
	}

	id = x86_match_cpu(intel_pstate_cpu_ids);
	if (!id)
		return -ENODEV;

	cpu_def = (struct cpu_defaults *)id->driver_data;

	copy_pid_params(&cpu_def->pid_policy);
	copy_cpu_funcs(&cpu_def->funcs);

	if (intel_pstate_msrs_not_valid())
		return -ENODEV;

hwp_cpu_matched:
	/*
	 * The Intel pstate driver will be ignored if the platform
	 * firmware has its own power management modes.
	 */
	if (intel_pstate_platform_pwr_mgmt_exists())
		return -ENODEV;

	if (!hwp_active && hwp_only)
		return -ENOTSUPP;

	pr_info("Intel P-state driver initializing\n");

	all_cpu_data = vzalloc(sizeof(void *) * num_possible_cpus());
	if (!all_cpu_data)
		return -ENOMEM;

	intel_pstate_request_control_from_smm();

	intel_pstate_sysfs_expose_params();

	mutex_lock(&intel_pstate_driver_lock);
	rc = intel_pstate_register_driver();
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
		intel_pstate_driver = &intel_cpufreq;
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
