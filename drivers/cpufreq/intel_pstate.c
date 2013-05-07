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

#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/tick.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <trace/events/power.h>

#include <asm/div64.h>
#include <asm/msr.h>
#include <asm/cpu_device_id.h>

#define SAMPLE_COUNT		3

#define FRAC_BITS 8
#define int_tofp(X) ((int64_t)(X) << FRAC_BITS)
#define fp_toint(X) ((X) >> FRAC_BITS)

static inline int32_t mul_fp(int32_t x, int32_t y)
{
	return ((int64_t)x * (int64_t)y) >> FRAC_BITS;
}

static inline int32_t div_fp(int32_t x, int32_t y)
{
	return div_s64((int64_t)x << FRAC_BITS, (int64_t)y);
}

struct sample {
	int core_pct_busy;
	u64 aperf;
	u64 mperf;
	int freq;
};

struct pstate_data {
	int	current_pstate;
	int	min_pstate;
	int	max_pstate;
	int	turbo_pstate;
};

struct _pid {
	int setpoint;
	int32_t integral;
	int32_t p_gain;
	int32_t i_gain;
	int32_t d_gain;
	int deadband;
	int last_err;
};

struct cpudata {
	int cpu;

	char name[64];

	struct timer_list timer;

	struct pstate_adjust_policy *pstate_policy;
	struct pstate_data pstate;
	struct _pid pid;
	struct _pid idle_pid;

	int min_pstate_count;
	int idle_mode;

	u64	prev_aperf;
	u64	prev_mperf;
	int	sample_ptr;
	struct sample samples[SAMPLE_COUNT];
};

static struct cpudata **all_cpu_data;
struct pstate_adjust_policy {
	int sample_rate_ms;
	int deadband;
	int setpoint;
	int p_gain_pct;
	int d_gain_pct;
	int i_gain_pct;
};

static struct pstate_adjust_policy default_policy = {
	.sample_rate_ms = 10,
	.deadband = 0,
	.setpoint = 109,
	.p_gain_pct = 17,
	.d_gain_pct = 0,
	.i_gain_pct = 4,
};

struct perf_limits {
	int no_turbo;
	int max_perf_pct;
	int min_perf_pct;
	int32_t max_perf;
	int32_t min_perf;
};

static struct perf_limits limits = {
	.no_turbo = 0,
	.max_perf_pct = 100,
	.max_perf = int_tofp(1),
	.min_perf_pct = 0,
	.min_perf = 0,
};

static inline void pid_reset(struct _pid *pid, int setpoint, int busy,
			int deadband, int integral) {
	pid->setpoint = setpoint;
	pid->deadband  = deadband;
	pid->integral  = int_tofp(integral);
	pid->last_err  = setpoint - busy;
}

static inline void pid_p_gain_set(struct _pid *pid, int percent)
{
	pid->p_gain = div_fp(int_tofp(percent), int_tofp(100));
}

static inline void pid_i_gain_set(struct _pid *pid, int percent)
{
	pid->i_gain = div_fp(int_tofp(percent), int_tofp(100));
}

static inline void pid_d_gain_set(struct _pid *pid, int percent)
{

	pid->d_gain = div_fp(int_tofp(percent), int_tofp(100));
}

static signed int pid_calc(struct _pid *pid, int busy)
{
	signed int err, result;
	int32_t pterm, dterm, fp_error;
	int32_t integral_limit;

	err = pid->setpoint - busy;
	fp_error = int_tofp(err);

	if (abs(err) <= pid->deadband)
		return 0;

	pterm = mul_fp(pid->p_gain, fp_error);

	pid->integral += fp_error;

	/* limit the integral term */
	integral_limit = int_tofp(30);
	if (pid->integral > integral_limit)
		pid->integral = integral_limit;
	if (pid->integral < -integral_limit)
		pid->integral = -integral_limit;

	dterm = mul_fp(pid->d_gain, (err - pid->last_err));
	pid->last_err = err;

	result = pterm + mul_fp(pid->integral, pid->i_gain) + dterm;

	return (signed int)fp_toint(result);
}

static inline void intel_pstate_busy_pid_reset(struct cpudata *cpu)
{
	pid_p_gain_set(&cpu->pid, cpu->pstate_policy->p_gain_pct);
	pid_d_gain_set(&cpu->pid, cpu->pstate_policy->d_gain_pct);
	pid_i_gain_set(&cpu->pid, cpu->pstate_policy->i_gain_pct);

	pid_reset(&cpu->pid,
		cpu->pstate_policy->setpoint,
		100,
		cpu->pstate_policy->deadband,
		0);
}

static inline void intel_pstate_idle_pid_reset(struct cpudata *cpu)
{
	pid_p_gain_set(&cpu->idle_pid, cpu->pstate_policy->p_gain_pct);
	pid_d_gain_set(&cpu->idle_pid, cpu->pstate_policy->d_gain_pct);
	pid_i_gain_set(&cpu->idle_pid, cpu->pstate_policy->i_gain_pct);

	pid_reset(&cpu->idle_pid,
		75,
		50,
		cpu->pstate_policy->deadband,
		0);
}

static inline void intel_pstate_reset_all_pid(void)
{
	unsigned int cpu;
	for_each_online_cpu(cpu) {
		if (all_cpu_data[cpu])
			intel_pstate_busy_pid_reset(all_cpu_data[cpu]);
	}
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
DEFINE_SIMPLE_ATTRIBUTE(fops_pid_param, pid_param_get,
			pid_param_set, "%llu\n");

struct pid_param {
	char *name;
	void *value;
};

static struct pid_param pid_files[] = {
	{"sample_rate_ms", &default_policy.sample_rate_ms},
	{"d_gain_pct", &default_policy.d_gain_pct},
	{"i_gain_pct", &default_policy.i_gain_pct},
	{"deadband", &default_policy.deadband},
	{"setpoint", &default_policy.setpoint},
	{"p_gain_pct", &default_policy.p_gain_pct},
	{NULL, NULL}
};

static struct dentry *debugfs_parent;
static void intel_pstate_debug_expose_params(void)
{
	int i = 0;

	debugfs_parent = debugfs_create_dir("pstate_snb", NULL);
	if (IS_ERR_OR_NULL(debugfs_parent))
		return;
	while (pid_files[i].name) {
		debugfs_create_file(pid_files[i].name, 0660,
				debugfs_parent, pid_files[i].value,
				&fops_pid_param);
		i++;
	}
}

/************************** debugfs end ************************/

/************************** sysfs begin ************************/
#define show_one(file_name, object)					\
	static ssize_t show_##file_name					\
	(struct kobject *kobj, struct attribute *attr, char *buf)	\
	{								\
		return sprintf(buf, "%u\n", limits.object);		\
	}

static ssize_t store_no_turbo(struct kobject *a, struct attribute *b,
				const char *buf, size_t count)
{
	unsigned int input;
	int ret;
	ret = sscanf(buf, "%u", &input);
	if (ret != 1)
		return -EINVAL;
	limits.no_turbo = clamp_t(int, input, 0 , 1);

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

	limits.max_perf_pct = clamp_t(int, input, 0 , 100);
	limits.max_perf = div_fp(int_tofp(limits.max_perf_pct), int_tofp(100));
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
	limits.min_perf_pct = clamp_t(int, input, 0 , 100);
	limits.min_perf = div_fp(int_tofp(limits.min_perf_pct), int_tofp(100));

	return count;
}

show_one(no_turbo, no_turbo);
show_one(max_perf_pct, max_perf_pct);
show_one(min_perf_pct, min_perf_pct);

define_one_global_rw(no_turbo);
define_one_global_rw(max_perf_pct);
define_one_global_rw(min_perf_pct);

static struct attribute *intel_pstate_attributes[] = {
	&no_turbo.attr,
	&max_perf_pct.attr,
	&min_perf_pct.attr,
	NULL
};

static struct attribute_group intel_pstate_attr_group = {
	.attrs = intel_pstate_attributes,
};
static struct kobject *intel_pstate_kobject;

static void intel_pstate_sysfs_expose_params(void)
{
	int rc;

	intel_pstate_kobject = kobject_create_and_add("intel_pstate",
						&cpu_subsys.dev_root->kobj);
	BUG_ON(!intel_pstate_kobject);
	rc = sysfs_create_group(intel_pstate_kobject,
				&intel_pstate_attr_group);
	BUG_ON(rc);
}

/************************** sysfs end ************************/

static int intel_pstate_min_pstate(void)
{
	u64 value;
	rdmsrl(MSR_PLATFORM_INFO, value);
	return (value >> 40) & 0xFF;
}

static int intel_pstate_max_pstate(void)
{
	u64 value;
	rdmsrl(MSR_PLATFORM_INFO, value);
	return (value >> 8) & 0xFF;
}

static int intel_pstate_turbo_pstate(void)
{
	u64 value;
	int nont, ret;
	rdmsrl(MSR_NHM_TURBO_RATIO_LIMIT, value);
	nont = intel_pstate_max_pstate();
	ret = ((value) & 255);
	if (ret <= nont)
		ret = nont;
	return ret;
}

static void intel_pstate_get_min_max(struct cpudata *cpu, int *min, int *max)
{
	int max_perf = cpu->pstate.turbo_pstate;
	int min_perf;
	if (limits.no_turbo)
		max_perf = cpu->pstate.max_pstate;

	max_perf = fp_toint(mul_fp(int_tofp(max_perf), limits.max_perf));
	*max = clamp_t(int, max_perf,
			cpu->pstate.min_pstate, cpu->pstate.turbo_pstate);

	min_perf = fp_toint(mul_fp(int_tofp(max_perf), limits.min_perf));
	*min = clamp_t(int, min_perf,
			cpu->pstate.min_pstate, max_perf);
}

static void intel_pstate_set_pstate(struct cpudata *cpu, int pstate)
{
	int max_perf, min_perf;

	intel_pstate_get_min_max(cpu, &min_perf, &max_perf);

	pstate = clamp_t(int, pstate, min_perf, max_perf);

	if (pstate == cpu->pstate.current_pstate)
		return;

#ifndef MODULE
	trace_cpu_frequency(pstate * 100000, cpu->cpu);
#endif
	cpu->pstate.current_pstate = pstate;
	wrmsrl(MSR_IA32_PERF_CTL, pstate << 8);

}

static inline void intel_pstate_pstate_increase(struct cpudata *cpu, int steps)
{
	int target;
	target = cpu->pstate.current_pstate + steps;

	intel_pstate_set_pstate(cpu, target);
}

static inline void intel_pstate_pstate_decrease(struct cpudata *cpu, int steps)
{
	int target;
	target = cpu->pstate.current_pstate - steps;
	intel_pstate_set_pstate(cpu, target);
}

static void intel_pstate_get_cpu_pstates(struct cpudata *cpu)
{
	sprintf(cpu->name, "Intel 2nd generation core");

	cpu->pstate.min_pstate = intel_pstate_min_pstate();
	cpu->pstate.max_pstate = intel_pstate_max_pstate();
	cpu->pstate.turbo_pstate = intel_pstate_turbo_pstate();

	/*
	 * goto max pstate so we don't slow up boot if we are built-in if we are
	 * a module we will take care of it during normal operation
	 */
	intel_pstate_set_pstate(cpu, cpu->pstate.max_pstate);
}

static inline void intel_pstate_calc_busy(struct cpudata *cpu,
					struct sample *sample)
{
	u64 core_pct;
	core_pct = div64_u64(sample->aperf * 100, sample->mperf);
	sample->freq = cpu->pstate.max_pstate * core_pct * 1000;

	sample->core_pct_busy = core_pct;
}

static inline void intel_pstate_sample(struct cpudata *cpu)
{
	u64 aperf, mperf;

	rdmsrl(MSR_IA32_APERF, aperf);
	rdmsrl(MSR_IA32_MPERF, mperf);
	cpu->sample_ptr = (cpu->sample_ptr + 1) % SAMPLE_COUNT;
	cpu->samples[cpu->sample_ptr].aperf = aperf;
	cpu->samples[cpu->sample_ptr].mperf = mperf;
	cpu->samples[cpu->sample_ptr].aperf -= cpu->prev_aperf;
	cpu->samples[cpu->sample_ptr].mperf -= cpu->prev_mperf;

	intel_pstate_calc_busy(cpu, &cpu->samples[cpu->sample_ptr]);

	cpu->prev_aperf = aperf;
	cpu->prev_mperf = mperf;
}

static inline void intel_pstate_set_sample_time(struct cpudata *cpu)
{
	int sample_time, delay;

	sample_time = cpu->pstate_policy->sample_rate_ms;
	delay = msecs_to_jiffies(sample_time);
	mod_timer_pinned(&cpu->timer, jiffies + delay);
}

static inline void intel_pstate_idle_mode(struct cpudata *cpu)
{
	cpu->idle_mode = 1;
}

static inline void intel_pstate_normal_mode(struct cpudata *cpu)
{
	cpu->idle_mode = 0;
}

static inline int intel_pstate_get_scaled_busy(struct cpudata *cpu)
{
	int32_t busy_scaled;
	int32_t core_busy, turbo_pstate, current_pstate;

	core_busy = int_tofp(cpu->samples[cpu->sample_ptr].core_pct_busy);
	turbo_pstate = int_tofp(cpu->pstate.turbo_pstate);
	current_pstate = int_tofp(cpu->pstate.current_pstate);
	busy_scaled = mul_fp(core_busy, div_fp(turbo_pstate, current_pstate));

	return fp_toint(busy_scaled);
}

static inline void intel_pstate_adjust_busy_pstate(struct cpudata *cpu)
{
	int busy_scaled;
	struct _pid *pid;
	signed int ctl = 0;
	int steps;

	pid = &cpu->pid;
	busy_scaled = intel_pstate_get_scaled_busy(cpu);

	ctl = pid_calc(pid, busy_scaled);

	steps = abs(ctl);
	if (ctl < 0)
		intel_pstate_pstate_increase(cpu, steps);
	else
		intel_pstate_pstate_decrease(cpu, steps);
}

static inline void intel_pstate_adjust_idle_pstate(struct cpudata *cpu)
{
	int busy_scaled;
	struct _pid *pid;
	int ctl = 0;
	int steps;

	pid = &cpu->idle_pid;

	busy_scaled = intel_pstate_get_scaled_busy(cpu);

	ctl = pid_calc(pid, 100 - busy_scaled);

	steps = abs(ctl);
	if (ctl < 0)
		intel_pstate_pstate_decrease(cpu, steps);
	else
		intel_pstate_pstate_increase(cpu, steps);

	if (cpu->pstate.current_pstate == cpu->pstate.min_pstate)
		intel_pstate_normal_mode(cpu);
}

static void intel_pstate_timer_func(unsigned long __data)
{
	struct cpudata *cpu = (struct cpudata *) __data;

	intel_pstate_sample(cpu);

	if (!cpu->idle_mode)
		intel_pstate_adjust_busy_pstate(cpu);
	else
		intel_pstate_adjust_idle_pstate(cpu);

#if defined(XPERF_FIX)
	if (cpu->pstate.current_pstate == cpu->pstate.min_pstate) {
		cpu->min_pstate_count++;
		if (!(cpu->min_pstate_count % 5)) {
			intel_pstate_set_pstate(cpu, cpu->pstate.max_pstate);
			intel_pstate_idle_mode(cpu);
		}
	} else
		cpu->min_pstate_count = 0;
#endif
	intel_pstate_set_sample_time(cpu);
}

#define ICPU(model, policy) \
	{ X86_VENDOR_INTEL, 6, model, X86_FEATURE_ANY, (unsigned long)&policy }

static const struct x86_cpu_id intel_pstate_cpu_ids[] = {
	ICPU(0x2a, default_policy),
	ICPU(0x2d, default_policy),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, intel_pstate_cpu_ids);

static int intel_pstate_init_cpu(unsigned int cpunum)
{

	const struct x86_cpu_id *id;
	struct cpudata *cpu;

	id = x86_match_cpu(intel_pstate_cpu_ids);
	if (!id)
		return -ENODEV;

	all_cpu_data[cpunum] = kzalloc(sizeof(struct cpudata), GFP_KERNEL);
	if (!all_cpu_data[cpunum])
		return -ENOMEM;

	cpu = all_cpu_data[cpunum];

	intel_pstate_get_cpu_pstates(cpu);

	cpu->cpu = cpunum;
	cpu->pstate_policy =
		(struct pstate_adjust_policy *)id->driver_data;
	init_timer_deferrable(&cpu->timer);
	cpu->timer.function = intel_pstate_timer_func;
	cpu->timer.data =
		(unsigned long)cpu;
	cpu->timer.expires = jiffies + HZ/100;
	intel_pstate_busy_pid_reset(cpu);
	intel_pstate_idle_pid_reset(cpu);
	intel_pstate_sample(cpu);
	intel_pstate_set_pstate(cpu, cpu->pstate.max_pstate);

	add_timer_on(&cpu->timer, cpunum);

	pr_info("Intel pstate controlling: cpu %d\n", cpunum);

	return 0;
}

static unsigned int intel_pstate_get(unsigned int cpu_num)
{
	struct sample *sample;
	struct cpudata *cpu;

	cpu = all_cpu_data[cpu_num];
	if (!cpu)
		return 0;
	sample = &cpu->samples[cpu->sample_ptr];
	return sample->freq;
}

static int intel_pstate_set_policy(struct cpufreq_policy *policy)
{
	struct cpudata *cpu;

	cpu = all_cpu_data[policy->cpu];

	if (!policy->cpuinfo.max_freq)
		return -ENODEV;

	if (policy->policy == CPUFREQ_POLICY_PERFORMANCE) {
		limits.min_perf_pct = 100;
		limits.min_perf = int_tofp(1);
		limits.max_perf_pct = 100;
		limits.max_perf = int_tofp(1);
		limits.no_turbo = 0;
		return 0;
	}
	limits.min_perf_pct = (policy->min * 100) / policy->cpuinfo.max_freq;
	limits.min_perf_pct = clamp_t(int, limits.min_perf_pct, 0 , 100);
	limits.min_perf = div_fp(int_tofp(limits.min_perf_pct), int_tofp(100));

	limits.max_perf_pct = policy->max * 100 / policy->cpuinfo.max_freq;
	limits.max_perf_pct = clamp_t(int, limits.max_perf_pct, 0 , 100);
	limits.max_perf = div_fp(int_tofp(limits.max_perf_pct), int_tofp(100));

	return 0;
}

static int intel_pstate_verify_policy(struct cpufreq_policy *policy)
{
	cpufreq_verify_within_limits(policy,
				policy->cpuinfo.min_freq,
				policy->cpuinfo.max_freq);

	if ((policy->policy != CPUFREQ_POLICY_POWERSAVE) &&
		(policy->policy != CPUFREQ_POLICY_PERFORMANCE))
		return -EINVAL;

	return 0;
}

static int __cpuinit intel_pstate_cpu_exit(struct cpufreq_policy *policy)
{
	int cpu = policy->cpu;

	del_timer(&all_cpu_data[cpu]->timer);
	kfree(all_cpu_data[cpu]);
	all_cpu_data[cpu] = NULL;
	return 0;
}

static int __cpuinit intel_pstate_cpu_init(struct cpufreq_policy *policy)
{
	int rc, min_pstate, max_pstate;
	struct cpudata *cpu;

	rc = intel_pstate_init_cpu(policy->cpu);
	if (rc)
		return rc;

	cpu = all_cpu_data[policy->cpu];

	if (!limits.no_turbo &&
		limits.min_perf_pct == 100 && limits.max_perf_pct == 100)
		policy->policy = CPUFREQ_POLICY_PERFORMANCE;
	else
		policy->policy = CPUFREQ_POLICY_POWERSAVE;

	intel_pstate_get_min_max(cpu, &min_pstate, &max_pstate);
	policy->min = min_pstate * 100000;
	policy->max = max_pstate * 100000;

	/* cpuinfo and default policy values */
	policy->cpuinfo.min_freq = cpu->pstate.min_pstate * 100000;
	policy->cpuinfo.max_freq = cpu->pstate.turbo_pstate * 100000;
	policy->cpuinfo.transition_latency = CPUFREQ_ETERNAL;
	cpumask_set_cpu(policy->cpu, policy->cpus);

	return 0;
}

static struct cpufreq_driver intel_pstate_driver = {
	.flags		= CPUFREQ_CONST_LOOPS,
	.verify		= intel_pstate_verify_policy,
	.setpolicy	= intel_pstate_set_policy,
	.get		= intel_pstate_get,
	.init		= intel_pstate_cpu_init,
	.exit		= intel_pstate_cpu_exit,
	.name		= "intel_pstate",
	.owner		= THIS_MODULE,
};

static int __initdata no_load;

static int intel_pstate_msrs_not_valid(void)
{
	/* Check that all the msr's we are using are valid. */
	u64 aperf, mperf, tmp;

	rdmsrl(MSR_IA32_APERF, aperf);
	rdmsrl(MSR_IA32_MPERF, mperf);

	if (!intel_pstate_min_pstate() ||
		!intel_pstate_max_pstate() ||
		!intel_pstate_turbo_pstate())
		return -ENODEV;

	rdmsrl(MSR_IA32_APERF, tmp);
	if (!(tmp - aperf))
		return -ENODEV;

	rdmsrl(MSR_IA32_MPERF, tmp);
	if (!(tmp - mperf))
		return -ENODEV;

	return 0;
}
static int __init intel_pstate_init(void)
{
	int cpu, rc = 0;
	const struct x86_cpu_id *id;

	if (no_load)
		return -ENODEV;

	id = x86_match_cpu(intel_pstate_cpu_ids);
	if (!id)
		return -ENODEV;

	if (intel_pstate_msrs_not_valid())
		return -ENODEV;

	pr_info("Intel P-state driver initializing.\n");

	all_cpu_data = vmalloc(sizeof(void *) * num_possible_cpus());
	if (!all_cpu_data)
		return -ENOMEM;
	memset(all_cpu_data, 0, sizeof(void *) * num_possible_cpus());

	rc = cpufreq_register_driver(&intel_pstate_driver);
	if (rc)
		goto out;

	intel_pstate_debug_expose_params();
	intel_pstate_sysfs_expose_params();
	return rc;
out:
	get_online_cpus();
	for_each_online_cpu(cpu) {
		if (all_cpu_data[cpu]) {
			del_timer_sync(&all_cpu_data[cpu]->timer);
			kfree(all_cpu_data[cpu]);
		}
	}

	put_online_cpus();
	vfree(all_cpu_data);
	return -ENODEV;
}
device_initcall(intel_pstate_init);

static int __init intel_pstate_setup(char *str)
{
	if (!str)
		return -EINVAL;

	if (!strcmp(str, "disable"))
		no_load = 1;
	return 0;
}
early_param("intel_pstate", intel_pstate_setup);

MODULE_AUTHOR("Dirk Brandewie <dirk.j.brandewie@intel.com>");
MODULE_DESCRIPTION("'intel_pstate' - P state driver Intel Core processors");
MODULE_LICENSE("GPL");
