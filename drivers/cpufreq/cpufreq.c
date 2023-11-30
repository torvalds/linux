// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/drivers/cpufreq/cpufreq.c
 *
 *  Copyright (C) 2001 Russell King
 *            (C) 2002 - 2003 Dominik Brodowski <linux@brodo.de>
 *            (C) 2013 Viresh Kumar <viresh.kumar@linaro.org>
 *
 *  Oct 2005 - Ashok Raj <ashok.raj@intel.com>
 *	Added handling for CPU hotplug
 *  Feb 2006 - Jacob Shin <jacob.shin@amd.com>
 *	Fix handling for CPU hotplug -- affected CPUs
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/cpufreq_times.h>
#include <linux/cpu_cooling.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/tick.h>
#include <linux/units.h>
#include <trace/events/power.h>
#include <trace/hooks/cpufreq.h>
#include <trace/hooks/thermal.h>

static LIST_HEAD(cpufreq_policy_list);

/* Macros to iterate over CPU policies */
#define for_each_suitable_policy(__policy, __active)			 \
	list_for_each_entry(__policy, &cpufreq_policy_list, policy_list) \
		if ((__active) == !policy_is_inactive(__policy))

#define for_each_active_policy(__policy)		\
	for_each_suitable_policy(__policy, true)
#define for_each_inactive_policy(__policy)		\
	for_each_suitable_policy(__policy, false)

/* Iterate over governors */
static LIST_HEAD(cpufreq_governor_list);
#define for_each_governor(__governor)				\
	list_for_each_entry(__governor, &cpufreq_governor_list, governor_list)

static char default_governor[CPUFREQ_NAME_LEN];

/*
 * The "cpufreq driver" - the arch- or hardware-dependent low
 * level driver of CPUFreq support, and its spinlock. This lock
 * also protects the cpufreq_cpu_data array.
 */
static struct cpufreq_driver *cpufreq_driver;
static DEFINE_PER_CPU(struct cpufreq_policy *, cpufreq_cpu_data);
static DEFINE_RWLOCK(cpufreq_driver_lock);

static DEFINE_STATIC_KEY_FALSE(cpufreq_freq_invariance);
bool cpufreq_supports_freq_invariance(void)
{
	return static_branch_likely(&cpufreq_freq_invariance);
}

/* Flag to suspend/resume CPUFreq governors */
static bool cpufreq_suspended;

static inline bool has_target(void)
{
	return cpufreq_driver->target_index || cpufreq_driver->target;
}

/* internal prototypes */
static unsigned int __cpufreq_get(struct cpufreq_policy *policy);
static int cpufreq_init_governor(struct cpufreq_policy *policy);
static void cpufreq_exit_governor(struct cpufreq_policy *policy);
static void cpufreq_governor_limits(struct cpufreq_policy *policy);
static int cpufreq_set_policy(struct cpufreq_policy *policy,
			      struct cpufreq_governor *new_gov,
			      unsigned int new_pol);

/*
 * Two notifier lists: the "policy" list is involved in the
 * validation process for a new CPU frequency policy; the
 * "transition" list for kernel code that needs to handle
 * changes to devices when the CPU clock speed changes.
 * The mutex locks both lists.
 */
static BLOCKING_NOTIFIER_HEAD(cpufreq_policy_notifier_list);
SRCU_NOTIFIER_HEAD_STATIC(cpufreq_transition_notifier_list);

static int off __read_mostly;
static int cpufreq_disabled(void)
{
	return off;
}
void disable_cpufreq(void)
{
	off = 1;
}
static DEFINE_MUTEX(cpufreq_governor_mutex);

bool have_governor_per_policy(void)
{
	return !!(cpufreq_driver->flags & CPUFREQ_HAVE_GOVERNOR_PER_POLICY);
}
EXPORT_SYMBOL_GPL(have_governor_per_policy);

static struct kobject *cpufreq_global_kobject;

struct kobject *get_governor_parent_kobj(struct cpufreq_policy *policy)
{
	if (have_governor_per_policy())
		return &policy->kobj;
	else
		return cpufreq_global_kobject;
}
EXPORT_SYMBOL_GPL(get_governor_parent_kobj);

static inline u64 get_cpu_idle_time_jiffy(unsigned int cpu, u64 *wall)
{
	struct kernel_cpustat kcpustat;
	u64 cur_wall_time;
	u64 idle_time;
	u64 busy_time;

	cur_wall_time = jiffies64_to_nsecs(get_jiffies_64());

	kcpustat_cpu_fetch(&kcpustat, cpu);

	busy_time = kcpustat.cpustat[CPUTIME_USER];
	busy_time += kcpustat.cpustat[CPUTIME_SYSTEM];
	busy_time += kcpustat.cpustat[CPUTIME_IRQ];
	busy_time += kcpustat.cpustat[CPUTIME_SOFTIRQ];
	busy_time += kcpustat.cpustat[CPUTIME_STEAL];
	busy_time += kcpustat.cpustat[CPUTIME_NICE];

	idle_time = cur_wall_time - busy_time;
	if (wall)
		*wall = div_u64(cur_wall_time, NSEC_PER_USEC);

	return div_u64(idle_time, NSEC_PER_USEC);
}

u64 get_cpu_idle_time(unsigned int cpu, u64 *wall, int io_busy)
{
	u64 idle_time = get_cpu_idle_time_us(cpu, io_busy ? wall : NULL);

	if (idle_time == -1ULL)
		return get_cpu_idle_time_jiffy(cpu, wall);
	else if (!io_busy)
		idle_time += get_cpu_iowait_time_us(cpu, wall);

	return idle_time;
}
EXPORT_SYMBOL_GPL(get_cpu_idle_time);

/*
 * This is a generic cpufreq init() routine which can be used by cpufreq
 * drivers of SMP systems. It will do following:
 * - validate & show freq table passed
 * - set policies transition latency
 * - policy->cpus with all possible CPUs
 */
void cpufreq_generic_init(struct cpufreq_policy *policy,
		struct cpufreq_frequency_table *table,
		unsigned int transition_latency)
{
	policy->freq_table = table;
	policy->cpuinfo.transition_latency = transition_latency;

	/*
	 * The driver only supports the SMP configuration where all processors
	 * share the clock and voltage and clock.
	 */
	cpumask_setall(policy->cpus);
}
EXPORT_SYMBOL_GPL(cpufreq_generic_init);

struct cpufreq_policy *cpufreq_cpu_get_raw(unsigned int cpu)
{
	struct cpufreq_policy *policy = per_cpu(cpufreq_cpu_data, cpu);

	return policy && cpumask_test_cpu(cpu, policy->cpus) ? policy : NULL;
}
EXPORT_SYMBOL_GPL(cpufreq_cpu_get_raw);

unsigned int cpufreq_generic_get(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get_raw(cpu);

	if (!policy || IS_ERR(policy->clk)) {
		pr_err("%s: No %s associated to cpu: %d\n",
		       __func__, policy ? "clk" : "policy", cpu);
		return 0;
	}

	return clk_get_rate(policy->clk) / 1000;
}
EXPORT_SYMBOL_GPL(cpufreq_generic_get);

/**
 * cpufreq_cpu_get - Return policy for a CPU and mark it as busy.
 * @cpu: CPU to find the policy for.
 *
 * Call cpufreq_cpu_get_raw() to obtain a cpufreq policy for @cpu and increment
 * the kobject reference counter of that policy.  Return a valid policy on
 * success or NULL on failure.
 *
 * The policy returned by this function has to be released with the help of
 * cpufreq_cpu_put() to balance its kobject reference counter properly.
 */
struct cpufreq_policy *cpufreq_cpu_get(unsigned int cpu)
{
	struct cpufreq_policy *policy = NULL;
	unsigned long flags;

	if (WARN_ON(cpu >= nr_cpu_ids))
		return NULL;

	/* get the cpufreq driver */
	read_lock_irqsave(&cpufreq_driver_lock, flags);

	if (cpufreq_driver) {
		/* get the CPU */
		policy = cpufreq_cpu_get_raw(cpu);
		if (policy)
			kobject_get(&policy->kobj);
	}

	read_unlock_irqrestore(&cpufreq_driver_lock, flags);

	return policy;
}
EXPORT_SYMBOL_GPL(cpufreq_cpu_get);

/**
 * cpufreq_cpu_put - Decrement kobject usage counter for cpufreq policy.
 * @policy: cpufreq policy returned by cpufreq_cpu_get().
 */
void cpufreq_cpu_put(struct cpufreq_policy *policy)
{
	kobject_put(&policy->kobj);
}
EXPORT_SYMBOL_GPL(cpufreq_cpu_put);

/**
 * cpufreq_cpu_release - Unlock a policy and decrement its usage counter.
 * @policy: cpufreq policy returned by cpufreq_cpu_acquire().
 */
void cpufreq_cpu_release(struct cpufreq_policy *policy)
{
	if (WARN_ON(!policy))
		return;

	lockdep_assert_held(&policy->rwsem);

	up_write(&policy->rwsem);

	cpufreq_cpu_put(policy);
}

/**
 * cpufreq_cpu_acquire - Find policy for a CPU, mark it as busy and lock it.
 * @cpu: CPU to find the policy for.
 *
 * Call cpufreq_cpu_get() to get a reference on the cpufreq policy for @cpu and
 * if the policy returned by it is not NULL, acquire its rwsem for writing.
 * Return the policy if it is active or release it and return NULL otherwise.
 *
 * The policy returned by this function has to be released with the help of
 * cpufreq_cpu_release() in order to release its rwsem and balance its usage
 * counter properly.
 */
struct cpufreq_policy *cpufreq_cpu_acquire(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);

	if (!policy)
		return NULL;

	down_write(&policy->rwsem);

	if (policy_is_inactive(policy)) {
		cpufreq_cpu_release(policy);
		return NULL;
	}

	return policy;
}

/*********************************************************************
 *            EXTERNALLY AFFECTING FREQUENCY CHANGES                 *
 *********************************************************************/

/**
 * adjust_jiffies - Adjust the system "loops_per_jiffy".
 * @val: CPUFREQ_PRECHANGE or CPUFREQ_POSTCHANGE.
 * @ci: Frequency change information.
 *
 * This function alters the system "loops_per_jiffy" for the clock
 * speed change. Note that loops_per_jiffy cannot be updated on SMP
 * systems as each CPU might be scaled differently. So, use the arch
 * per-CPU loops_per_jiffy value wherever possible.
 */
static void adjust_jiffies(unsigned long val, struct cpufreq_freqs *ci)
{
#ifndef CONFIG_SMP
	static unsigned long l_p_j_ref;
	static unsigned int l_p_j_ref_freq;

	if (ci->flags & CPUFREQ_CONST_LOOPS)
		return;

	if (!l_p_j_ref_freq) {
		l_p_j_ref = loops_per_jiffy;
		l_p_j_ref_freq = ci->old;
		pr_debug("saving %lu as reference value for loops_per_jiffy; freq is %u kHz\n",
			 l_p_j_ref, l_p_j_ref_freq);
	}
	if (val == CPUFREQ_POSTCHANGE && ci->old != ci->new) {
		loops_per_jiffy = cpufreq_scale(l_p_j_ref, l_p_j_ref_freq,
								ci->new);
		pr_debug("scaling loops_per_jiffy to %lu for frequency %u kHz\n",
			 loops_per_jiffy, ci->new);
	}
#endif
}

/**
 * cpufreq_notify_transition - Notify frequency transition and adjust jiffies.
 * @policy: cpufreq policy to enable fast frequency switching for.
 * @freqs: contain details of the frequency update.
 * @state: set to CPUFREQ_PRECHANGE or CPUFREQ_POSTCHANGE.
 *
 * This function calls the transition notifiers and adjust_jiffies().
 *
 * It is called twice on all CPU frequency changes that have external effects.
 */
static void cpufreq_notify_transition(struct cpufreq_policy *policy,
				      struct cpufreq_freqs *freqs,
				      unsigned int state)
{
	int cpu;

	BUG_ON(irqs_disabled());

	if (cpufreq_disabled())
		return;

	freqs->policy = policy;
	freqs->flags = cpufreq_driver->flags;
	pr_debug("notification %u of frequency transition to %u kHz\n",
		 state, freqs->new);

	switch (state) {
	case CPUFREQ_PRECHANGE:
		/*
		 * Detect if the driver reported a value as "old frequency"
		 * which is not equal to what the cpufreq core thinks is
		 * "old frequency".
		 */
		if (policy->cur && policy->cur != freqs->old) {
			pr_debug("Warning: CPU frequency is %u, cpufreq assumed %u kHz\n",
				 freqs->old, policy->cur);
			freqs->old = policy->cur;
		}

		srcu_notifier_call_chain(&cpufreq_transition_notifier_list,
					 CPUFREQ_PRECHANGE, freqs);

		adjust_jiffies(CPUFREQ_PRECHANGE, freqs);
		break;

	case CPUFREQ_POSTCHANGE:
		adjust_jiffies(CPUFREQ_POSTCHANGE, freqs);
		pr_debug("FREQ: %u - CPUs: %*pbl\n", freqs->new,
			 cpumask_pr_args(policy->cpus));

		for_each_cpu(cpu, policy->cpus)
			trace_cpu_frequency(freqs->new, cpu);

		srcu_notifier_call_chain(&cpufreq_transition_notifier_list,
					 CPUFREQ_POSTCHANGE, freqs);

		cpufreq_stats_record_transition(policy, freqs->new);
		cpufreq_times_record_transition(policy, freqs->new);
		policy->cur = freqs->new;
		trace_android_rvh_cpufreq_transition(policy);
	}
}

/* Do post notifications when there are chances that transition has failed */
static void cpufreq_notify_post_transition(struct cpufreq_policy *policy,
		struct cpufreq_freqs *freqs, int transition_failed)
{
	cpufreq_notify_transition(policy, freqs, CPUFREQ_POSTCHANGE);
	if (!transition_failed)
		return;

	swap(freqs->old, freqs->new);
	cpufreq_notify_transition(policy, freqs, CPUFREQ_PRECHANGE);
	cpufreq_notify_transition(policy, freqs, CPUFREQ_POSTCHANGE);
}

void cpufreq_freq_transition_begin(struct cpufreq_policy *policy,
		struct cpufreq_freqs *freqs)
{

	/*
	 * Catch double invocations of _begin() which lead to self-deadlock.
	 * ASYNC_NOTIFICATION drivers are left out because the cpufreq core
	 * doesn't invoke _begin() on their behalf, and hence the chances of
	 * double invocations are very low. Moreover, there are scenarios
	 * where these checks can emit false-positive warnings in these
	 * drivers; so we avoid that by skipping them altogether.
	 */
	WARN_ON(!(cpufreq_driver->flags & CPUFREQ_ASYNC_NOTIFICATION)
				&& current == policy->transition_task);

wait:
	wait_event(policy->transition_wait, !policy->transition_ongoing);

	spin_lock(&policy->transition_lock);

	if (unlikely(policy->transition_ongoing)) {
		spin_unlock(&policy->transition_lock);
		goto wait;
	}

	policy->transition_ongoing = true;
	policy->transition_task = current;

	spin_unlock(&policy->transition_lock);

	cpufreq_notify_transition(policy, freqs, CPUFREQ_PRECHANGE);
}
EXPORT_SYMBOL_GPL(cpufreq_freq_transition_begin);

void cpufreq_freq_transition_end(struct cpufreq_policy *policy,
		struct cpufreq_freqs *freqs, int transition_failed)
{
	if (WARN_ON(!policy->transition_ongoing))
		return;

	cpufreq_notify_post_transition(policy, freqs, transition_failed);

	arch_set_freq_scale(policy->related_cpus,
			    policy->cur,
			    policy->cpuinfo.max_freq);

	spin_lock(&policy->transition_lock);
	policy->transition_ongoing = false;
	policy->transition_task = NULL;
	spin_unlock(&policy->transition_lock);

	wake_up(&policy->transition_wait);
}
EXPORT_SYMBOL_GPL(cpufreq_freq_transition_end);

/*
 * Fast frequency switching status count.  Positive means "enabled", negative
 * means "disabled" and 0 means "not decided yet".
 */
static int cpufreq_fast_switch_count;
static DEFINE_MUTEX(cpufreq_fast_switch_lock);

static void cpufreq_list_transition_notifiers(void)
{
	struct notifier_block *nb;

	pr_info("Registered transition notifiers:\n");

	mutex_lock(&cpufreq_transition_notifier_list.mutex);

	for (nb = cpufreq_transition_notifier_list.head; nb; nb = nb->next)
		pr_info("%pS\n", nb->notifier_call);

	mutex_unlock(&cpufreq_transition_notifier_list.mutex);
}

/**
 * cpufreq_enable_fast_switch - Enable fast frequency switching for policy.
 * @policy: cpufreq policy to enable fast frequency switching for.
 *
 * Try to enable fast frequency switching for @policy.
 *
 * The attempt will fail if there is at least one transition notifier registered
 * at this point, as fast frequency switching is quite fundamentally at odds
 * with transition notifiers.  Thus if successful, it will make registration of
 * transition notifiers fail going forward.
 */
void cpufreq_enable_fast_switch(struct cpufreq_policy *policy)
{
	lockdep_assert_held(&policy->rwsem);

	if (!policy->fast_switch_possible)
		return;

	mutex_lock(&cpufreq_fast_switch_lock);
	if (cpufreq_fast_switch_count >= 0) {
		cpufreq_fast_switch_count++;
		policy->fast_switch_enabled = true;
	} else {
		pr_warn("CPU%u: Fast frequency switching not enabled\n",
			policy->cpu);
		cpufreq_list_transition_notifiers();
	}
	mutex_unlock(&cpufreq_fast_switch_lock);
}
EXPORT_SYMBOL_GPL(cpufreq_enable_fast_switch);

/**
 * cpufreq_disable_fast_switch - Disable fast frequency switching for policy.
 * @policy: cpufreq policy to disable fast frequency switching for.
 */
void cpufreq_disable_fast_switch(struct cpufreq_policy *policy)
{
	mutex_lock(&cpufreq_fast_switch_lock);
	if (policy->fast_switch_enabled) {
		policy->fast_switch_enabled = false;
		if (!WARN_ON(cpufreq_fast_switch_count <= 0))
			cpufreq_fast_switch_count--;
	}
	mutex_unlock(&cpufreq_fast_switch_lock);
}
EXPORT_SYMBOL_GPL(cpufreq_disable_fast_switch);

static unsigned int __resolve_freq(struct cpufreq_policy *policy,
		unsigned int target_freq, unsigned int relation)
{
	unsigned int idx;
	unsigned int old_target_freq = target_freq;

	target_freq = clamp_val(target_freq, policy->min, policy->max);
	trace_android_vh_cpufreq_resolve_freq(policy, &target_freq, old_target_freq);

	if (!policy->freq_table)
		return target_freq;

	idx = cpufreq_frequency_table_target(policy, target_freq, relation);
	policy->cached_resolved_idx = idx;
	policy->cached_target_freq = target_freq;
	return policy->freq_table[idx].frequency;
}

/**
 * cpufreq_driver_resolve_freq - Map a target frequency to a driver-supported
 * one.
 * @policy: associated policy to interrogate
 * @target_freq: target frequency to resolve.
 *
 * The target to driver frequency mapping is cached in the policy.
 *
 * Return: Lowest driver-supported frequency greater than or equal to the
 * given target_freq, subject to policy (min/max) and driver limitations.
 */
unsigned int cpufreq_driver_resolve_freq(struct cpufreq_policy *policy,
					 unsigned int target_freq)
{
	return __resolve_freq(policy, target_freq, CPUFREQ_RELATION_LE);
}
EXPORT_SYMBOL_GPL(cpufreq_driver_resolve_freq);

unsigned int cpufreq_policy_transition_delay_us(struct cpufreq_policy *policy)
{
	unsigned int latency;

	if (policy->transition_delay_us)
		return policy->transition_delay_us;

	latency = policy->cpuinfo.transition_latency / NSEC_PER_USEC;
	if (latency) {
		/*
		 * For platforms that can change the frequency very fast (< 10
		 * us), the above formula gives a decent transition delay. But
		 * for platforms where transition_latency is in milliseconds, it
		 * ends up giving unrealistic values.
		 *
		 * Cap the default transition delay to 10 ms, which seems to be
		 * a reasonable amount of time after which we should reevaluate
		 * the frequency.
		 */
		return min(latency * LATENCY_MULTIPLIER, (unsigned int)10000);
	}

	return LATENCY_MULTIPLIER;
}
EXPORT_SYMBOL_GPL(cpufreq_policy_transition_delay_us);

/*********************************************************************
 *                          SYSFS INTERFACE                          *
 *********************************************************************/
static ssize_t show_boost(struct kobject *kobj,
			  struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", cpufreq_driver->boost_enabled);
}

static ssize_t store_boost(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t count)
{
	int ret, enable;

	ret = sscanf(buf, "%d", &enable);
	if (ret != 1 || enable < 0 || enable > 1)
		return -EINVAL;

	if (cpufreq_boost_trigger_state(enable)) {
		pr_err("%s: Cannot %s BOOST!\n",
		       __func__, enable ? "enable" : "disable");
		return -EINVAL;
	}

	pr_debug("%s: cpufreq BOOST %s\n",
		 __func__, enable ? "enabled" : "disabled");

	return count;
}
define_one_global_rw(boost);

static struct cpufreq_governor *find_governor(const char *str_governor)
{
	struct cpufreq_governor *t;

	for_each_governor(t)
		if (!strncasecmp(str_governor, t->name, CPUFREQ_NAME_LEN))
			return t;

	return NULL;
}

static struct cpufreq_governor *get_governor(const char *str_governor)
{
	struct cpufreq_governor *t;

	mutex_lock(&cpufreq_governor_mutex);
	t = find_governor(str_governor);
	if (!t)
		goto unlock;

	if (!try_module_get(t->owner))
		t = NULL;

unlock:
	mutex_unlock(&cpufreq_governor_mutex);

	return t;
}

static unsigned int cpufreq_parse_policy(char *str_governor)
{
	if (!strncasecmp(str_governor, "performance", CPUFREQ_NAME_LEN))
		return CPUFREQ_POLICY_PERFORMANCE;

	if (!strncasecmp(str_governor, "powersave", CPUFREQ_NAME_LEN))
		return CPUFREQ_POLICY_POWERSAVE;

	return CPUFREQ_POLICY_UNKNOWN;
}

/**
 * cpufreq_parse_governor - parse a governor string only for has_target()
 * @str_governor: Governor name.
 */
static struct cpufreq_governor *cpufreq_parse_governor(char *str_governor)
{
	struct cpufreq_governor *t;

	t = get_governor(str_governor);
	if (t)
		return t;

	if (request_module("cpufreq_%s", str_governor))
		return NULL;

	return get_governor(str_governor);
}

/*
 * cpufreq_per_cpu_attr_read() / show_##file_name() -
 * print out cpufreq information
 *
 * Write out information from cpufreq_driver->policy[cpu]; object must be
 * "unsigned int".
 */

#define show_one(file_name, object)			\
static ssize_t show_##file_name				\
(struct cpufreq_policy *policy, char *buf)		\
{							\
	return sprintf(buf, "%u\n", policy->object);	\
}

static ssize_t show_cpuinfo_max_freq(struct cpufreq_policy *policy, char *buf)
{
	unsigned int max_freq = policy->cpuinfo.max_freq;

	trace_android_rvh_show_max_freq(policy, &max_freq);
	return sprintf(buf, "%u\n", max_freq);
}

show_one(cpuinfo_min_freq, cpuinfo.min_freq);
show_one(cpuinfo_transition_latency, cpuinfo.transition_latency);
show_one(scaling_min_freq, min);
show_one(scaling_max_freq, max);

__weak unsigned int arch_freq_get_on_cpu(int cpu)
{
	return 0;
}

static ssize_t show_scaling_cur_freq(struct cpufreq_policy *policy, char *buf)
{
	ssize_t ret;
	unsigned int freq;

	freq = arch_freq_get_on_cpu(policy->cpu);
	if (freq)
		ret = sprintf(buf, "%u\n", freq);
	else if (cpufreq_driver->setpolicy && cpufreq_driver->get)
		ret = sprintf(buf, "%u\n", cpufreq_driver->get(policy->cpu));
	else
		ret = sprintf(buf, "%u\n", policy->cur);
	return ret;
}

/*
 * cpufreq_per_cpu_attr_write() / store_##file_name() - sysfs write access
 */
#define store_one(file_name, object)			\
static ssize_t store_##file_name					\
(struct cpufreq_policy *policy, const char *buf, size_t count)		\
{									\
	unsigned long val;						\
	int ret;							\
									\
	ret = sscanf(buf, "%lu", &val);					\
	if (ret != 1)							\
		return -EINVAL;						\
									\
	ret = freq_qos_update_request(policy->object##_freq_req, val);\
	return ret >= 0 ? count : ret;					\
}

store_one(scaling_min_freq, min);
store_one(scaling_max_freq, max);

/*
 * show_cpuinfo_cur_freq - current CPU frequency as detected by hardware
 */
static ssize_t show_cpuinfo_cur_freq(struct cpufreq_policy *policy,
					char *buf)
{
	unsigned int cur_freq = __cpufreq_get(policy);

	if (cur_freq)
		return sprintf(buf, "%u\n", cur_freq);

	return sprintf(buf, "<unknown>\n");
}

/*
 * show_scaling_governor - show the current policy for the specified CPU
 */
static ssize_t show_scaling_governor(struct cpufreq_policy *policy, char *buf)
{
	if (policy->policy == CPUFREQ_POLICY_POWERSAVE)
		return sprintf(buf, "powersave\n");
	else if (policy->policy == CPUFREQ_POLICY_PERFORMANCE)
		return sprintf(buf, "performance\n");
	else if (policy->governor)
		return scnprintf(buf, CPUFREQ_NAME_PLEN, "%s\n",
				policy->governor->name);
	return -EINVAL;
}

/*
 * store_scaling_governor - store policy for the specified CPU
 */
static ssize_t store_scaling_governor(struct cpufreq_policy *policy,
					const char *buf, size_t count)
{
	char str_governor[16];
	int ret;

	ret = sscanf(buf, "%15s", str_governor);
	if (ret != 1)
		return -EINVAL;

	if (cpufreq_driver->setpolicy) {
		unsigned int new_pol;

		new_pol = cpufreq_parse_policy(str_governor);
		if (!new_pol)
			return -EINVAL;

		ret = cpufreq_set_policy(policy, NULL, new_pol);
	} else {
		struct cpufreq_governor *new_gov;

		new_gov = cpufreq_parse_governor(str_governor);
		if (!new_gov)
			return -EINVAL;

		ret = cpufreq_set_policy(policy, new_gov,
					 CPUFREQ_POLICY_UNKNOWN);

		module_put(new_gov->owner);
	}

	return ret ? ret : count;
}

/*
 * show_scaling_driver - show the cpufreq driver currently loaded
 */
static ssize_t show_scaling_driver(struct cpufreq_policy *policy, char *buf)
{
	return scnprintf(buf, CPUFREQ_NAME_PLEN, "%s\n", cpufreq_driver->name);
}

/*
 * show_scaling_available_governors - show the available CPUfreq governors
 */
static ssize_t show_scaling_available_governors(struct cpufreq_policy *policy,
						char *buf)
{
	ssize_t i = 0;
	struct cpufreq_governor *t;

	if (!has_target()) {
		i += sprintf(buf, "performance powersave");
		goto out;
	}

	mutex_lock(&cpufreq_governor_mutex);
	for_each_governor(t) {
		if (i >= (ssize_t) ((PAGE_SIZE / sizeof(char))
		    - (CPUFREQ_NAME_LEN + 2)))
			break;
		i += scnprintf(&buf[i], CPUFREQ_NAME_PLEN, "%s ", t->name);
	}
	mutex_unlock(&cpufreq_governor_mutex);
out:
	i += sprintf(&buf[i], "\n");
	return i;
}

ssize_t cpufreq_show_cpus(const struct cpumask *mask, char *buf)
{
	ssize_t i = 0;
	unsigned int cpu;

	for_each_cpu(cpu, mask) {
		i += scnprintf(&buf[i], (PAGE_SIZE - i - 2), "%u ", cpu);
		if (i >= (PAGE_SIZE - 5))
			break;
	}

	/* Remove the extra space at the end */
	i--;

	i += sprintf(&buf[i], "\n");
	return i;
}
EXPORT_SYMBOL_GPL(cpufreq_show_cpus);

/*
 * show_related_cpus - show the CPUs affected by each transition even if
 * hw coordination is in use
 */
static ssize_t show_related_cpus(struct cpufreq_policy *policy, char *buf)
{
	return cpufreq_show_cpus(policy->related_cpus, buf);
}

/*
 * show_affected_cpus - show the CPUs affected by each transition
 */
static ssize_t show_affected_cpus(struct cpufreq_policy *policy, char *buf)
{
	return cpufreq_show_cpus(policy->cpus, buf);
}

static ssize_t store_scaling_setspeed(struct cpufreq_policy *policy,
					const char *buf, size_t count)
{
	unsigned int freq = 0;
	unsigned int ret;

	if (!policy->governor || !policy->governor->store_setspeed)
		return -EINVAL;

	ret = sscanf(buf, "%u", &freq);
	if (ret != 1)
		return -EINVAL;

	policy->governor->store_setspeed(policy, freq);

	return count;
}

static ssize_t show_scaling_setspeed(struct cpufreq_policy *policy, char *buf)
{
	if (!policy->governor || !policy->governor->show_setspeed)
		return sprintf(buf, "<unsupported>\n");

	return policy->governor->show_setspeed(policy, buf);
}

/*
 * show_bios_limit - show the current cpufreq HW/BIOS limitation
 */
static ssize_t show_bios_limit(struct cpufreq_policy *policy, char *buf)
{
	unsigned int limit;
	int ret;
	ret = cpufreq_driver->bios_limit(policy->cpu, &limit);
	if (!ret)
		return sprintf(buf, "%u\n", limit);
	return sprintf(buf, "%u\n", policy->cpuinfo.max_freq);
}

cpufreq_freq_attr_ro_perm(cpuinfo_cur_freq, 0400);
cpufreq_freq_attr_ro(cpuinfo_min_freq);
cpufreq_freq_attr_ro(cpuinfo_max_freq);
cpufreq_freq_attr_ro(cpuinfo_transition_latency);
cpufreq_freq_attr_ro(scaling_available_governors);
cpufreq_freq_attr_ro(scaling_driver);
cpufreq_freq_attr_ro(scaling_cur_freq);
cpufreq_freq_attr_ro(bios_limit);
cpufreq_freq_attr_ro(related_cpus);
cpufreq_freq_attr_ro(affected_cpus);
cpufreq_freq_attr_rw(scaling_min_freq);
cpufreq_freq_attr_rw(scaling_max_freq);
cpufreq_freq_attr_rw(scaling_governor);
cpufreq_freq_attr_rw(scaling_setspeed);

static struct attribute *cpufreq_attrs[] = {
	&cpuinfo_min_freq.attr,
	&cpuinfo_max_freq.attr,
	&cpuinfo_transition_latency.attr,
	&scaling_min_freq.attr,
	&scaling_max_freq.attr,
	&affected_cpus.attr,
	&related_cpus.attr,
	&scaling_governor.attr,
	&scaling_driver.attr,
	&scaling_available_governors.attr,
	&scaling_setspeed.attr,
	NULL
};
ATTRIBUTE_GROUPS(cpufreq);

#define to_policy(k) container_of(k, struct cpufreq_policy, kobj)
#define to_attr(a) container_of(a, struct freq_attr, attr)

static ssize_t show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct cpufreq_policy *policy = to_policy(kobj);
	struct freq_attr *fattr = to_attr(attr);
	ssize_t ret = -EBUSY;

	if (!fattr->show)
		return -EIO;

	down_read(&policy->rwsem);
	if (likely(!policy_is_inactive(policy)))
		ret = fattr->show(policy, buf);
	up_read(&policy->rwsem);

	return ret;
}

static ssize_t store(struct kobject *kobj, struct attribute *attr,
		     const char *buf, size_t count)
{
	struct cpufreq_policy *policy = to_policy(kobj);
	struct freq_attr *fattr = to_attr(attr);
	ssize_t ret = -EBUSY;

	if (!fattr->store)
		return -EIO;

	down_write(&policy->rwsem);
	if (likely(!policy_is_inactive(policy)))
		ret = fattr->store(policy, buf, count);
	up_write(&policy->rwsem);

	return ret;
}

static void cpufreq_sysfs_release(struct kobject *kobj)
{
	struct cpufreq_policy *policy = to_policy(kobj);
	pr_debug("last reference is dropped\n");
	complete(&policy->kobj_unregister);
}

static const struct sysfs_ops sysfs_ops = {
	.show	= show,
	.store	= store,
};

static struct kobj_type ktype_cpufreq = {
	.sysfs_ops	= &sysfs_ops,
	.default_groups	= cpufreq_groups,
	.release	= cpufreq_sysfs_release,
};

static void add_cpu_dev_symlink(struct cpufreq_policy *policy, unsigned int cpu,
				struct device *dev)
{
	if (unlikely(!dev))
		return;

	if (cpumask_test_and_set_cpu(cpu, policy->real_cpus))
		return;

	dev_dbg(dev, "%s: Adding symlink\n", __func__);
	if (sysfs_create_link(&dev->kobj, &policy->kobj, "cpufreq"))
		dev_err(dev, "cpufreq symlink creation failed\n");
}

static void remove_cpu_dev_symlink(struct cpufreq_policy *policy, int cpu,
				   struct device *dev)
{
	dev_dbg(dev, "%s: Removing symlink\n", __func__);
	sysfs_remove_link(&dev->kobj, "cpufreq");
	cpumask_clear_cpu(cpu, policy->real_cpus);
}

static int cpufreq_add_dev_interface(struct cpufreq_policy *policy)
{
	struct freq_attr **drv_attr;
	int ret = 0;

	/* set up files for this cpu device */
	drv_attr = cpufreq_driver->attr;
	while (drv_attr && *drv_attr) {
		ret = sysfs_create_file(&policy->kobj, &((*drv_attr)->attr));
		if (ret)
			return ret;
		drv_attr++;
	}
	if (cpufreq_driver->get) {
		ret = sysfs_create_file(&policy->kobj, &cpuinfo_cur_freq.attr);
		if (ret)
			return ret;
	}

	ret = sysfs_create_file(&policy->kobj, &scaling_cur_freq.attr);
	if (ret)
		return ret;

	if (cpufreq_driver->bios_limit) {
		ret = sysfs_create_file(&policy->kobj, &bios_limit.attr);
		if (ret)
			return ret;
	}

	return 0;
}

static int cpufreq_init_policy(struct cpufreq_policy *policy)
{
	struct cpufreq_governor *gov = NULL;
	unsigned int pol = CPUFREQ_POLICY_UNKNOWN;
	int ret;

	if (has_target()) {
		/* Update policy governor to the one used before hotplug. */
		gov = get_governor(policy->last_governor);
		if (gov) {
			pr_debug("Restoring governor %s for cpu %d\n",
				 gov->name, policy->cpu);
		} else {
			gov = get_governor(default_governor);
		}

		if (!gov) {
			gov = cpufreq_default_governor();
			__module_get(gov->owner);
		}

	} else {

		/* Use the default policy if there is no last_policy. */
		if (policy->last_policy) {
			pol = policy->last_policy;
		} else {
			pol = cpufreq_parse_policy(default_governor);
			/*
			 * In case the default governor is neither "performance"
			 * nor "powersave", fall back to the initial policy
			 * value set by the driver.
			 */
			if (pol == CPUFREQ_POLICY_UNKNOWN)
				pol = policy->policy;
		}
		if (pol != CPUFREQ_POLICY_PERFORMANCE &&
		    pol != CPUFREQ_POLICY_POWERSAVE)
			return -ENODATA;
	}

	ret = cpufreq_set_policy(policy, gov, pol);
	if (gov)
		module_put(gov->owner);

	return ret;
}

static int cpufreq_add_policy_cpu(struct cpufreq_policy *policy, unsigned int cpu)
{
	int ret = 0;

	/* Has this CPU been taken care of already? */
	if (cpumask_test_cpu(cpu, policy->cpus))
		return 0;

	down_write(&policy->rwsem);
	if (has_target())
		cpufreq_stop_governor(policy);

	cpumask_set_cpu(cpu, policy->cpus);

	if (has_target()) {
		ret = cpufreq_start_governor(policy);
		if (ret)
			pr_err("%s: Failed to start governor\n", __func__);
	}
	up_write(&policy->rwsem);
	return ret;
}

void refresh_frequency_limits(struct cpufreq_policy *policy)
{
	if (!policy_is_inactive(policy)) {
		pr_debug("updating policy for CPU %u\n", policy->cpu);

		cpufreq_set_policy(policy, policy->governor, policy->policy);
	}
}
EXPORT_SYMBOL(refresh_frequency_limits);

static void handle_update(struct work_struct *work)
{
	struct cpufreq_policy *policy =
		container_of(work, struct cpufreq_policy, update);

	pr_debug("handle_update for cpu %u called\n", policy->cpu);
	down_write(&policy->rwsem);
	refresh_frequency_limits(policy);
	up_write(&policy->rwsem);
}

static int cpufreq_notifier_min(struct notifier_block *nb, unsigned long freq,
				void *data)
{
	struct cpufreq_policy *policy = container_of(nb, struct cpufreq_policy, nb_min);

	schedule_work(&policy->update);
	return 0;
}

static int cpufreq_notifier_max(struct notifier_block *nb, unsigned long freq,
				void *data)
{
	struct cpufreq_policy *policy = container_of(nb, struct cpufreq_policy, nb_max);

	schedule_work(&policy->update);
	return 0;
}

static void cpufreq_policy_put_kobj(struct cpufreq_policy *policy)
{
	struct kobject *kobj;
	struct completion *cmp;

	down_write(&policy->rwsem);
	cpufreq_stats_free_table(policy);
	kobj = &policy->kobj;
	cmp = &policy->kobj_unregister;
	up_write(&policy->rwsem);
	kobject_put(kobj);

	/*
	 * We need to make sure that the underlying kobj is
	 * actually not referenced anymore by anybody before we
	 * proceed with unloading.
	 */
	pr_debug("waiting for dropping of refcount\n");
	wait_for_completion(cmp);
	pr_debug("wait complete\n");
}

static struct cpufreq_policy *cpufreq_policy_alloc(unsigned int cpu)
{
	struct cpufreq_policy *policy;
	struct device *dev = get_cpu_device(cpu);
	int ret;

	if (!dev)
		return NULL;

	policy = kzalloc(sizeof(*policy), GFP_KERNEL);
	if (!policy)
		return NULL;

	if (!alloc_cpumask_var(&policy->cpus, GFP_KERNEL))
		goto err_free_policy;

	if (!zalloc_cpumask_var(&policy->related_cpus, GFP_KERNEL))
		goto err_free_cpumask;

	if (!zalloc_cpumask_var(&policy->real_cpus, GFP_KERNEL))
		goto err_free_rcpumask;

	init_completion(&policy->kobj_unregister);
	ret = kobject_init_and_add(&policy->kobj, &ktype_cpufreq,
				   cpufreq_global_kobject, "policy%u", cpu);
	if (ret) {
		dev_err(dev, "%s: failed to init policy->kobj: %d\n", __func__, ret);
		/*
		 * The entire policy object will be freed below, but the extra
		 * memory allocated for the kobject name needs to be freed by
		 * releasing the kobject.
		 */
		kobject_put(&policy->kobj);
		goto err_free_real_cpus;
	}

	freq_constraints_init(&policy->constraints);

	policy->nb_min.notifier_call = cpufreq_notifier_min;
	policy->nb_max.notifier_call = cpufreq_notifier_max;

	ret = freq_qos_add_notifier(&policy->constraints, FREQ_QOS_MIN,
				    &policy->nb_min);
	if (ret) {
		dev_err(dev, "Failed to register MIN QoS notifier: %d (%*pbl)\n",
			ret, cpumask_pr_args(policy->cpus));
		goto err_kobj_remove;
	}

	ret = freq_qos_add_notifier(&policy->constraints, FREQ_QOS_MAX,
				    &policy->nb_max);
	if (ret) {
		dev_err(dev, "Failed to register MAX QoS notifier: %d (%*pbl)\n",
			ret, cpumask_pr_args(policy->cpus));
		goto err_min_qos_notifier;
	}

	INIT_LIST_HEAD(&policy->policy_list);
	init_rwsem(&policy->rwsem);
	spin_lock_init(&policy->transition_lock);
	init_waitqueue_head(&policy->transition_wait);
	INIT_WORK(&policy->update, handle_update);

	policy->cpu = cpu;
	return policy;

err_min_qos_notifier:
	freq_qos_remove_notifier(&policy->constraints, FREQ_QOS_MIN,
				 &policy->nb_min);
err_kobj_remove:
	cpufreq_policy_put_kobj(policy);
err_free_real_cpus:
	free_cpumask_var(policy->real_cpus);
err_free_rcpumask:
	free_cpumask_var(policy->related_cpus);
err_free_cpumask:
	free_cpumask_var(policy->cpus);
err_free_policy:
	kfree(policy);

	return NULL;
}

static void cpufreq_policy_free(struct cpufreq_policy *policy)
{
	unsigned long flags;
	int cpu;

	/*
	 * The callers must ensure the policy is inactive by now, to avoid any
	 * races with show()/store() callbacks.
	 */
	if (unlikely(!policy_is_inactive(policy)))
		pr_warn("%s: Freeing active policy\n", __func__);

	/* Remove policy from list */
	write_lock_irqsave(&cpufreq_driver_lock, flags);
	list_del(&policy->policy_list);

	for_each_cpu(cpu, policy->related_cpus)
		per_cpu(cpufreq_cpu_data, cpu) = NULL;
	write_unlock_irqrestore(&cpufreq_driver_lock, flags);

	freq_qos_remove_notifier(&policy->constraints, FREQ_QOS_MAX,
				 &policy->nb_max);
	freq_qos_remove_notifier(&policy->constraints, FREQ_QOS_MIN,
				 &policy->nb_min);

	/* Cancel any pending policy->update work before freeing the policy. */
	cancel_work_sync(&policy->update);

	if (policy->max_freq_req) {
		/*
		 * Remove max_freq_req after sending CPUFREQ_REMOVE_POLICY
		 * notification, since CPUFREQ_CREATE_POLICY notification was
		 * sent after adding max_freq_req earlier.
		 */
		blocking_notifier_call_chain(&cpufreq_policy_notifier_list,
					     CPUFREQ_REMOVE_POLICY, policy);
		freq_qos_remove_request(policy->max_freq_req);
	}

	freq_qos_remove_request(policy->min_freq_req);
	kfree(policy->min_freq_req);

	cpufreq_policy_put_kobj(policy);
	free_cpumask_var(policy->real_cpus);
	free_cpumask_var(policy->related_cpus);
	free_cpumask_var(policy->cpus);
	kfree(policy);
}

static int cpufreq_online(unsigned int cpu)
{
	struct cpufreq_policy *policy;
	bool new_policy;
	unsigned long flags;
	unsigned int j;
	int ret;

	pr_debug("%s: bringing CPU%u online\n", __func__, cpu);

	/* Check if this CPU already has a policy to manage it */
	policy = per_cpu(cpufreq_cpu_data, cpu);
	if (policy) {
		WARN_ON(!cpumask_test_cpu(cpu, policy->related_cpus));
		if (!policy_is_inactive(policy))
			return cpufreq_add_policy_cpu(policy, cpu);

		/* This is the only online CPU for the policy.  Start over. */
		new_policy = false;
		down_write(&policy->rwsem);
		policy->cpu = cpu;
		policy->governor = NULL;
	} else {
		new_policy = true;
		policy = cpufreq_policy_alloc(cpu);
		if (!policy)
			return -ENOMEM;
		down_write(&policy->rwsem);
	}

	if (!new_policy && cpufreq_driver->online) {
		/* Recover policy->cpus using related_cpus */
		cpumask_copy(policy->cpus, policy->related_cpus);

		ret = cpufreq_driver->online(policy);
		if (ret) {
			pr_debug("%s: %d: initialization failed\n", __func__,
				 __LINE__);
			goto out_exit_policy;
		}
	} else {
		cpumask_copy(policy->cpus, cpumask_of(cpu));

		/*
		 * Call driver. From then on the cpufreq must be able
		 * to accept all calls to ->verify and ->setpolicy for this CPU.
		 */
		ret = cpufreq_driver->init(policy);
		if (ret) {
			pr_debug("%s: %d: initialization failed\n", __func__,
				 __LINE__);
			goto out_free_policy;
		}

		/*
		 * The initialization has succeeded and the policy is online.
		 * If there is a problem with its frequency table, take it
		 * offline and drop it.
		 */
		ret = cpufreq_table_validate_and_sort(policy);
		if (ret)
			goto out_offline_policy;

		/* related_cpus should at least include policy->cpus. */
		cpumask_copy(policy->related_cpus, policy->cpus);
	}

	/*
	 * affected cpus must always be the one, which are online. We aren't
	 * managing offline cpus here.
	 */
	cpumask_and(policy->cpus, policy->cpus, cpu_online_mask);

	if (new_policy) {
		for_each_cpu(j, policy->related_cpus) {
			per_cpu(cpufreq_cpu_data, j) = policy;
			add_cpu_dev_symlink(policy, j, get_cpu_device(j));
		}

		policy->min_freq_req = kzalloc(2 * sizeof(*policy->min_freq_req),
					       GFP_KERNEL);
		if (!policy->min_freq_req) {
			ret = -ENOMEM;
			goto out_destroy_policy;
		}

		ret = freq_qos_add_request(&policy->constraints,
					   policy->min_freq_req, FREQ_QOS_MIN,
					   FREQ_QOS_MIN_DEFAULT_VALUE);
		if (ret < 0) {
			/*
			 * So we don't call freq_qos_remove_request() for an
			 * uninitialized request.
			 */
			kfree(policy->min_freq_req);
			policy->min_freq_req = NULL;
			goto out_destroy_policy;
		}

		/*
		 * This must be initialized right here to avoid calling
		 * freq_qos_remove_request() on uninitialized request in case
		 * of errors.
		 */
		policy->max_freq_req = policy->min_freq_req + 1;

		ret = freq_qos_add_request(&policy->constraints,
					   policy->max_freq_req, FREQ_QOS_MAX,
					   FREQ_QOS_MAX_DEFAULT_VALUE);
		if (ret < 0) {
			policy->max_freq_req = NULL;
			goto out_destroy_policy;
		}

		blocking_notifier_call_chain(&cpufreq_policy_notifier_list,
				CPUFREQ_CREATE_POLICY, policy);
	}

	if (cpufreq_driver->get && has_target()) {
		policy->cur = cpufreq_driver->get(policy->cpu);
		if (!policy->cur) {
			ret = -EIO;
			pr_err("%s: ->get() failed\n", __func__);
			goto out_destroy_policy;
		}
	}

	/*
	 * Sometimes boot loaders set CPU frequency to a value outside of
	 * frequency table present with cpufreq core. In such cases CPU might be
	 * unstable if it has to run on that frequency for long duration of time
	 * and so its better to set it to a frequency which is specified in
	 * freq-table. This also makes cpufreq stats inconsistent as
	 * cpufreq-stats would fail to register because current frequency of CPU
	 * isn't found in freq-table.
	 *
	 * Because we don't want this change to effect boot process badly, we go
	 * for the next freq which is >= policy->cur ('cur' must be set by now,
	 * otherwise we will end up setting freq to lowest of the table as 'cur'
	 * is initialized to zero).
	 *
	 * We are passing target-freq as "policy->cur - 1" otherwise
	 * __cpufreq_driver_target() would simply fail, as policy->cur will be
	 * equal to target-freq.
	 */
	if ((cpufreq_driver->flags & CPUFREQ_NEED_INITIAL_FREQ_CHECK)
	    && has_target()) {
		unsigned int old_freq = policy->cur;

		/* Are we running at unknown frequency ? */
		ret = cpufreq_frequency_table_get_index(policy, old_freq);
		if (ret == -EINVAL) {
			ret = __cpufreq_driver_target(policy, old_freq - 1,
						      CPUFREQ_RELATION_L);

			/*
			 * Reaching here after boot in a few seconds may not
			 * mean that system will remain stable at "unknown"
			 * frequency for longer duration. Hence, a BUG_ON().
			 */
			BUG_ON(ret);
			pr_info("%s: CPU%d: Running at unlisted initial frequency: %u KHz, changing to: %u KHz\n",
				__func__, policy->cpu, old_freq, policy->cur);
		}
	}

	if (new_policy) {
		ret = cpufreq_add_dev_interface(policy);
		if (ret)
			goto out_destroy_policy;

		cpufreq_stats_create_table(policy);
		cpufreq_times_create_policy(policy);

		write_lock_irqsave(&cpufreq_driver_lock, flags);
		list_add(&policy->policy_list, &cpufreq_policy_list);
		write_unlock_irqrestore(&cpufreq_driver_lock, flags);

		/*
		 * Register with the energy model before
		 * sched_cpufreq_governor_change() is called, which will result
		 * in rebuilding of the sched domains, which should only be done
		 * once the energy model is properly initialized for the policy
		 * first.
		 *
		 * Also, this should be called before the policy is registered
		 * with cooling framework.
		 */
		if (cpufreq_driver->register_em)
			cpufreq_driver->register_em(policy);
	}

	ret = cpufreq_init_policy(policy);
	if (ret) {
		pr_err("%s: Failed to initialize policy for cpu: %d (%d)\n",
		       __func__, cpu, ret);
		goto out_destroy_policy;
	}

	up_write(&policy->rwsem);

	kobject_uevent(&policy->kobj, KOBJ_ADD);

	/* Callback for handling stuff after policy is ready */
	if (cpufreq_driver->ready)
		cpufreq_driver->ready(policy);

	if (cpufreq_thermal_control_enabled(cpufreq_driver)) {
		policy->cdev = of_cpufreq_cooling_register(policy);
		trace_android_vh_thermal_register(policy);
	}

	pr_debug("initialization complete\n");

	return 0;

out_destroy_policy:
	for_each_cpu(j, policy->real_cpus)
		remove_cpu_dev_symlink(policy, j, get_cpu_device(j));

out_offline_policy:
	if (cpufreq_driver->offline)
		cpufreq_driver->offline(policy);

out_exit_policy:
	if (cpufreq_driver->exit)
		cpufreq_driver->exit(policy);

out_free_policy:
	cpumask_clear(policy->cpus);
	up_write(&policy->rwsem);

	cpufreq_policy_free(policy);
	return ret;
}

/**
 * cpufreq_add_dev - the cpufreq interface for a CPU device.
 * @dev: CPU device.
 * @sif: Subsystem interface structure pointer (not used)
 */
static int cpufreq_add_dev(struct device *dev, struct subsys_interface *sif)
{
	struct cpufreq_policy *policy;
	unsigned cpu = dev->id;
	int ret;

	dev_dbg(dev, "%s: adding CPU%u\n", __func__, cpu);

	if (cpu_online(cpu)) {
		ret = cpufreq_online(cpu);
		if (ret)
			return ret;
	}

	/* Create sysfs link on CPU registration */
	policy = per_cpu(cpufreq_cpu_data, cpu);
	if (policy)
		add_cpu_dev_symlink(policy, cpu, dev);

	return 0;
}

static void __cpufreq_offline(unsigned int cpu, struct cpufreq_policy *policy)
{
	int ret;

	if (has_target())
		cpufreq_stop_governor(policy);

	cpumask_clear_cpu(cpu, policy->cpus);

	if (!policy_is_inactive(policy)) {
		/* Nominate a new CPU if necessary. */
		if (cpu == policy->cpu)
			policy->cpu = cpumask_any(policy->cpus);

		/* Start the governor again for the active policy. */
		if (has_target()) {
			ret = cpufreq_start_governor(policy);
			if (ret)
				pr_err("%s: Failed to start governor\n", __func__);
		}

		return;
	}

	if (has_target())
		strncpy(policy->last_governor, policy->governor->name,
			CPUFREQ_NAME_LEN);
	else
		policy->last_policy = policy->policy;

	if (cpufreq_thermal_control_enabled(cpufreq_driver)) {
		cpufreq_cooling_unregister(policy->cdev);
		trace_android_vh_thermal_unregister(policy);
		policy->cdev = NULL;
	}

	if (has_target())
		cpufreq_exit_governor(policy);

	/*
	 * Perform the ->offline() during light-weight tear-down, as
	 * that allows fast recovery when the CPU comes back.
	 */
	if (cpufreq_driver->offline) {
		cpufreq_driver->offline(policy);
	} else if (cpufreq_driver->exit) {
		cpufreq_driver->exit(policy);
		policy->freq_table = NULL;
	}
}

static int cpufreq_offline(unsigned int cpu)
{
	struct cpufreq_policy *policy;

	pr_debug("%s: unregistering CPU %u\n", __func__, cpu);

	policy = cpufreq_cpu_get_raw(cpu);
	if (!policy) {
		pr_debug("%s: No cpu_data found\n", __func__);
		return 0;
	}

	down_write(&policy->rwsem);

	__cpufreq_offline(cpu, policy);

	up_write(&policy->rwsem);
	return 0;
}

/*
 * cpufreq_remove_dev - remove a CPU device
 *
 * Removes the cpufreq interface for a CPU device.
 */
static void cpufreq_remove_dev(struct device *dev, struct subsys_interface *sif)
{
	unsigned int cpu = dev->id;
	struct cpufreq_policy *policy = per_cpu(cpufreq_cpu_data, cpu);

	if (!policy)
		return;

	down_write(&policy->rwsem);

	if (cpu_online(cpu))
		__cpufreq_offline(cpu, policy);

	remove_cpu_dev_symlink(policy, cpu, dev);

	if (!cpumask_empty(policy->real_cpus)) {
		up_write(&policy->rwsem);
		return;
	}

	/* We did light-weight exit earlier, do full tear down now */
	if (cpufreq_driver->offline)
		cpufreq_driver->exit(policy);

	up_write(&policy->rwsem);

	cpufreq_policy_free(policy);
}

/**
 * cpufreq_out_of_sync - Fix up actual and saved CPU frequency difference.
 * @policy: Policy managing CPUs.
 * @new_freq: New CPU frequency.
 *
 * Adjust to the current frequency first and clean up later by either calling
 * cpufreq_update_policy(), or scheduling handle_update().
 */
static void cpufreq_out_of_sync(struct cpufreq_policy *policy,
				unsigned int new_freq)
{
	struct cpufreq_freqs freqs;

	pr_debug("Warning: CPU frequency out of sync: cpufreq and timing core thinks of %u, is %u kHz\n",
		 policy->cur, new_freq);

	freqs.old = policy->cur;
	freqs.new = new_freq;

	cpufreq_freq_transition_begin(policy, &freqs);
	cpufreq_freq_transition_end(policy, &freqs, 0);
}

static unsigned int cpufreq_verify_current_freq(struct cpufreq_policy *policy, bool update)
{
	unsigned int new_freq;

	new_freq = cpufreq_driver->get(policy->cpu);
	if (!new_freq)
		return 0;

	/*
	 * If fast frequency switching is used with the given policy, the check
	 * against policy->cur is pointless, so skip it in that case.
	 */
	if (policy->fast_switch_enabled || !has_target())
		return new_freq;

	if (policy->cur != new_freq) {
		/*
		 * For some platforms, the frequency returned by hardware may be
		 * slightly different from what is provided in the frequency
		 * table, for example hardware may return 499 MHz instead of 500
		 * MHz. In such cases it is better to avoid getting into
		 * unnecessary frequency updates.
		 */
		if (abs(policy->cur - new_freq) < KHZ_PER_MHZ)
			return policy->cur;

		cpufreq_out_of_sync(policy, new_freq);
		if (update)
			schedule_work(&policy->update);
	}

	return new_freq;
}

/**
 * cpufreq_quick_get - get the CPU frequency (in kHz) from policy->cur
 * @cpu: CPU number
 *
 * This is the last known freq, without actually getting it from the driver.
 * Return value will be same as what is shown in scaling_cur_freq in sysfs.
 */
unsigned int cpufreq_quick_get(unsigned int cpu)
{
	struct cpufreq_policy *policy;
	unsigned int ret_freq = 0;
	unsigned long flags;

	read_lock_irqsave(&cpufreq_driver_lock, flags);

	if (cpufreq_driver && cpufreq_driver->setpolicy && cpufreq_driver->get) {
		ret_freq = cpufreq_driver->get(cpu);
		read_unlock_irqrestore(&cpufreq_driver_lock, flags);
		return ret_freq;
	}

	read_unlock_irqrestore(&cpufreq_driver_lock, flags);

	policy = cpufreq_cpu_get(cpu);
	if (policy) {
		ret_freq = policy->cur;
		cpufreq_cpu_put(policy);
	}

	return ret_freq;
}
EXPORT_SYMBOL(cpufreq_quick_get);

/**
 * cpufreq_quick_get_max - get the max reported CPU frequency for this CPU
 * @cpu: CPU number
 *
 * Just return the max possible frequency for a given CPU.
 */
unsigned int cpufreq_quick_get_max(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);
	unsigned int ret_freq = 0;

	if (policy) {
		ret_freq = policy->max;
		cpufreq_cpu_put(policy);
	}

	return ret_freq;
}
EXPORT_SYMBOL(cpufreq_quick_get_max);

/**
 * cpufreq_get_hw_max_freq - get the max hardware frequency of the CPU
 * @cpu: CPU number
 *
 * The default return value is the max_freq field of cpuinfo.
 */
__weak unsigned int cpufreq_get_hw_max_freq(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);
	unsigned int ret_freq = 0;

	if (policy) {
		ret_freq = policy->cpuinfo.max_freq;
		cpufreq_cpu_put(policy);
	}

	return ret_freq;
}
EXPORT_SYMBOL(cpufreq_get_hw_max_freq);

static unsigned int __cpufreq_get(struct cpufreq_policy *policy)
{
	if (unlikely(policy_is_inactive(policy)))
		return 0;

	return cpufreq_verify_current_freq(policy, true);
}

/**
 * cpufreq_get - get the current CPU frequency (in kHz)
 * @cpu: CPU number
 *
 * Get the CPU current (static) CPU frequency
 */
unsigned int cpufreq_get(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);
	unsigned int ret_freq = 0;

	if (policy) {
		down_read(&policy->rwsem);
		if (cpufreq_driver->get)
			ret_freq = __cpufreq_get(policy);
		up_read(&policy->rwsem);

		cpufreq_cpu_put(policy);
	}

	return ret_freq;
}
EXPORT_SYMBOL(cpufreq_get);

static struct subsys_interface cpufreq_interface = {
	.name		= "cpufreq",
	.subsys		= &cpu_subsys,
	.add_dev	= cpufreq_add_dev,
	.remove_dev	= cpufreq_remove_dev,
};

/*
 * In case platform wants some specific frequency to be configured
 * during suspend..
 */
int cpufreq_generic_suspend(struct cpufreq_policy *policy)
{
	int ret;

	if (!policy->suspend_freq) {
		pr_debug("%s: suspend_freq not defined\n", __func__);
		return 0;
	}

	pr_debug("%s: Setting suspend-freq: %u\n", __func__,
			policy->suspend_freq);

	ret = __cpufreq_driver_target(policy, policy->suspend_freq,
			CPUFREQ_RELATION_H);
	if (ret)
		pr_err("%s: unable to set suspend-freq: %u. err: %d\n",
				__func__, policy->suspend_freq, ret);

	return ret;
}
EXPORT_SYMBOL(cpufreq_generic_suspend);

/**
 * cpufreq_suspend() - Suspend CPUFreq governors.
 *
 * Called during system wide Suspend/Hibernate cycles for suspending governors
 * as some platforms can't change frequency after this point in suspend cycle.
 * Because some of the devices (like: i2c, regulators, etc) they use for
 * changing frequency are suspended quickly after this point.
 */
void cpufreq_suspend(void)
{
	struct cpufreq_policy *policy;

	if (!cpufreq_driver)
		return;

	if (!has_target() && !cpufreq_driver->suspend)
		goto suspend;

	pr_debug("%s: Suspending Governors\n", __func__);

	for_each_active_policy(policy) {
		if (has_target()) {
			down_write(&policy->rwsem);
			cpufreq_stop_governor(policy);
			up_write(&policy->rwsem);
		}

		if (cpufreq_driver->suspend && cpufreq_driver->suspend(policy))
			pr_err("%s: Failed to suspend driver: %s\n", __func__,
				cpufreq_driver->name);
	}

suspend:
	cpufreq_suspended = true;
}

/**
 * cpufreq_resume() - Resume CPUFreq governors.
 *
 * Called during system wide Suspend/Hibernate cycle for resuming governors that
 * are suspended with cpufreq_suspend().
 */
void cpufreq_resume(void)
{
	struct cpufreq_policy *policy;
	int ret;

	if (!cpufreq_driver)
		return;

	if (unlikely(!cpufreq_suspended))
		return;

	cpufreq_suspended = false;

	if (!has_target() && !cpufreq_driver->resume)
		return;

	pr_debug("%s: Resuming Governors\n", __func__);

	for_each_active_policy(policy) {
		if (cpufreq_driver->resume && cpufreq_driver->resume(policy)) {
			pr_err("%s: Failed to resume driver: %p\n", __func__,
				policy);
		} else if (has_target()) {
			down_write(&policy->rwsem);
			ret = cpufreq_start_governor(policy);
			up_write(&policy->rwsem);

			if (ret)
				pr_err("%s: Failed to start governor for policy: %p\n",
				       __func__, policy);
		}
	}
}

/**
 * cpufreq_driver_test_flags - Test cpufreq driver's flags against given ones.
 * @flags: Flags to test against the current cpufreq driver's flags.
 *
 * Assumes that the driver is there, so callers must ensure that this is the
 * case.
 */
bool cpufreq_driver_test_flags(u16 flags)
{
	return !!(cpufreq_driver->flags & flags);
}

/**
 * cpufreq_get_current_driver - Return the current driver's name.
 *
 * Return the name string of the currently registered cpufreq driver or NULL if
 * none.
 */
const char *cpufreq_get_current_driver(void)
{
	if (cpufreq_driver)
		return cpufreq_driver->name;

	return NULL;
}
EXPORT_SYMBOL_GPL(cpufreq_get_current_driver);

/**
 * cpufreq_get_driver_data - Return current driver data.
 *
 * Return the private data of the currently registered cpufreq driver, or NULL
 * if no cpufreq driver has been registered.
 */
void *cpufreq_get_driver_data(void)
{
	if (cpufreq_driver)
		return cpufreq_driver->driver_data;

	return NULL;
}
EXPORT_SYMBOL_GPL(cpufreq_get_driver_data);

/*********************************************************************
 *                     NOTIFIER LISTS INTERFACE                      *
 *********************************************************************/

/**
 * cpufreq_register_notifier - Register a notifier with cpufreq.
 * @nb: notifier function to register.
 * @list: CPUFREQ_TRANSITION_NOTIFIER or CPUFREQ_POLICY_NOTIFIER.
 *
 * Add a notifier to one of two lists: either a list of notifiers that run on
 * clock rate changes (once before and once after every transition), or a list
 * of notifiers that ron on cpufreq policy changes.
 *
 * This function may sleep and it has the same return values as
 * blocking_notifier_chain_register().
 */
int cpufreq_register_notifier(struct notifier_block *nb, unsigned int list)
{
	int ret;

	if (cpufreq_disabled())
		return -EINVAL;

	switch (list) {
	case CPUFREQ_TRANSITION_NOTIFIER:
		mutex_lock(&cpufreq_fast_switch_lock);

		if (cpufreq_fast_switch_count > 0) {
			mutex_unlock(&cpufreq_fast_switch_lock);
			return -EBUSY;
		}
		ret = srcu_notifier_chain_register(
				&cpufreq_transition_notifier_list, nb);
		if (!ret)
			cpufreq_fast_switch_count--;

		mutex_unlock(&cpufreq_fast_switch_lock);
		break;
	case CPUFREQ_POLICY_NOTIFIER:
		ret = blocking_notifier_chain_register(
				&cpufreq_policy_notifier_list, nb);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}
EXPORT_SYMBOL(cpufreq_register_notifier);

/**
 * cpufreq_unregister_notifier - Unregister a notifier from cpufreq.
 * @nb: notifier block to be unregistered.
 * @list: CPUFREQ_TRANSITION_NOTIFIER or CPUFREQ_POLICY_NOTIFIER.
 *
 * Remove a notifier from one of the cpufreq notifier lists.
 *
 * This function may sleep and it has the same return values as
 * blocking_notifier_chain_unregister().
 */
int cpufreq_unregister_notifier(struct notifier_block *nb, unsigned int list)
{
	int ret;

	if (cpufreq_disabled())
		return -EINVAL;

	switch (list) {
	case CPUFREQ_TRANSITION_NOTIFIER:
		mutex_lock(&cpufreq_fast_switch_lock);

		ret = srcu_notifier_chain_unregister(
				&cpufreq_transition_notifier_list, nb);
		if (!ret && !WARN_ON(cpufreq_fast_switch_count >= 0))
			cpufreq_fast_switch_count++;

		mutex_unlock(&cpufreq_fast_switch_lock);
		break;
	case CPUFREQ_POLICY_NOTIFIER:
		ret = blocking_notifier_chain_unregister(
				&cpufreq_policy_notifier_list, nb);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}
EXPORT_SYMBOL(cpufreq_unregister_notifier);


/*********************************************************************
 *                              GOVERNORS                            *
 *********************************************************************/

/**
 * cpufreq_driver_fast_switch - Carry out a fast CPU frequency switch.
 * @policy: cpufreq policy to switch the frequency for.
 * @target_freq: New frequency to set (may be approximate).
 *
 * Carry out a fast frequency switch without sleeping.
 *
 * The driver's ->fast_switch() callback invoked by this function must be
 * suitable for being called from within RCU-sched read-side critical sections
 * and it is expected to select the minimum available frequency greater than or
 * equal to @target_freq (CPUFREQ_RELATION_L).
 *
 * This function must not be called if policy->fast_switch_enabled is unset.
 *
 * Governors calling this function must guarantee that it will never be invoked
 * twice in parallel for the same policy and that it will never be called in
 * parallel with either ->target() or ->target_index() for the same policy.
 *
 * Returns the actual frequency set for the CPU.
 *
 * If 0 is returned by the driver's ->fast_switch() callback to indicate an
 * error condition, the hardware configuration must be preserved.
 */
unsigned int cpufreq_driver_fast_switch(struct cpufreq_policy *policy,
					unsigned int target_freq)
{
	unsigned int freq;
	unsigned int old_target_freq = target_freq;
	int cpu;

	target_freq = clamp_val(target_freq, policy->min, policy->max);
	trace_android_vh_cpufreq_fast_switch(policy, &target_freq, old_target_freq);
	freq = cpufreq_driver->fast_switch(policy, target_freq);

	if (!freq)
		return 0;

	policy->cur = freq;
	arch_set_freq_scale(policy->related_cpus, freq,
			    policy->cpuinfo.max_freq);
	cpufreq_stats_record_transition(policy, freq);
	cpufreq_times_record_transition(policy, freq);
	trace_android_rvh_cpufreq_transition(policy);

	if (trace_cpu_frequency_enabled()) {
		for_each_cpu(cpu, policy->cpus)
			trace_cpu_frequency(freq, cpu);
	}

	return freq;
}
EXPORT_SYMBOL_GPL(cpufreq_driver_fast_switch);

/**
 * cpufreq_driver_adjust_perf - Adjust CPU performance level in one go.
 * @cpu: Target CPU.
 * @min_perf: Minimum (required) performance level (units of @capacity).
 * @target_perf: Target (desired) performance level (units of @capacity).
 * @capacity: Capacity of the target CPU.
 *
 * Carry out a fast performance level switch of @cpu without sleeping.
 *
 * The driver's ->adjust_perf() callback invoked by this function must be
 * suitable for being called from within RCU-sched read-side critical sections
 * and it is expected to select a suitable performance level equal to or above
 * @min_perf and preferably equal to or below @target_perf.
 *
 * This function must not be called if policy->fast_switch_enabled is unset.
 *
 * Governors calling this function must guarantee that it will never be invoked
 * twice in parallel for the same CPU and that it will never be called in
 * parallel with either ->target() or ->target_index() or ->fast_switch() for
 * the same CPU.
 */
void cpufreq_driver_adjust_perf(unsigned int cpu,
				 unsigned long min_perf,
				 unsigned long target_perf,
				 unsigned long capacity)
{
	cpufreq_driver->adjust_perf(cpu, min_perf, target_perf, capacity);
}

/**
 * cpufreq_driver_has_adjust_perf - Check "direct fast switch" callback.
 *
 * Return 'true' if the ->adjust_perf callback is present for the
 * current driver or 'false' otherwise.
 */
bool cpufreq_driver_has_adjust_perf(void)
{
	return !!cpufreq_driver->adjust_perf;
}

/* Must set freqs->new to intermediate frequency */
static int __target_intermediate(struct cpufreq_policy *policy,
				 struct cpufreq_freqs *freqs, int index)
{
	int ret;

	freqs->new = cpufreq_driver->get_intermediate(policy, index);

	/* We don't need to switch to intermediate freq */
	if (!freqs->new)
		return 0;

	pr_debug("%s: cpu: %d, switching to intermediate freq: oldfreq: %u, intermediate freq: %u\n",
		 __func__, policy->cpu, freqs->old, freqs->new);

	cpufreq_freq_transition_begin(policy, freqs);
	ret = cpufreq_driver->target_intermediate(policy, index);
	cpufreq_freq_transition_end(policy, freqs, ret);

	if (ret)
		pr_err("%s: Failed to change to intermediate frequency: %d\n",
		       __func__, ret);

	return ret;
}

static int __target_index(struct cpufreq_policy *policy, int index)
{
	struct cpufreq_freqs freqs = {.old = policy->cur, .flags = 0};
	unsigned int restore_freq, intermediate_freq = 0;
	unsigned int newfreq = policy->freq_table[index].frequency;
	int retval = -EINVAL;
	bool notify;

	if (newfreq == policy->cur)
		return 0;

	/* Save last value to restore later on errors */
	restore_freq = policy->cur;

	notify = !(cpufreq_driver->flags & CPUFREQ_ASYNC_NOTIFICATION);
	if (notify) {
		/* Handle switching to intermediate frequency */
		if (cpufreq_driver->get_intermediate) {
			retval = __target_intermediate(policy, &freqs, index);
			if (retval)
				return retval;

			intermediate_freq = freqs.new;
			/* Set old freq to intermediate */
			if (intermediate_freq)
				freqs.old = freqs.new;
		}

		freqs.new = newfreq;
		pr_debug("%s: cpu: %d, oldfreq: %u, new freq: %u\n",
			 __func__, policy->cpu, freqs.old, freqs.new);

		cpufreq_freq_transition_begin(policy, &freqs);
	}

	retval = cpufreq_driver->target_index(policy, index);
	if (retval)
		pr_err("%s: Failed to change cpu frequency: %d\n", __func__,
		       retval);

	if (notify) {
		cpufreq_freq_transition_end(policy, &freqs, retval);

		/*
		 * Failed after setting to intermediate freq? Driver should have
		 * reverted back to initial frequency and so should we. Check
		 * here for intermediate_freq instead of get_intermediate, in
		 * case we haven't switched to intermediate freq at all.
		 */
		if (unlikely(retval && intermediate_freq)) {
			freqs.old = intermediate_freq;
			freqs.new = restore_freq;
			cpufreq_freq_transition_begin(policy, &freqs);
			cpufreq_freq_transition_end(policy, &freqs, 0);
		}
	}

	return retval;
}

int __cpufreq_driver_target(struct cpufreq_policy *policy,
			    unsigned int target_freq,
			    unsigned int relation)
{
	unsigned int old_target_freq = target_freq;

	if (cpufreq_disabled())
		return -ENODEV;

	target_freq = __resolve_freq(policy, target_freq, relation);

	trace_android_vh_cpufreq_target(policy, &target_freq, old_target_freq);

	pr_debug("target for CPU %u: %u kHz, relation %u, requested %u kHz\n",
		 policy->cpu, target_freq, relation, old_target_freq);

	/*
	 * This might look like a redundant call as we are checking it again
	 * after finding index. But it is left intentionally for cases where
	 * exactly same freq is called again and so we can save on few function
	 * calls.
	 */
	if (target_freq == policy->cur &&
	    !(cpufreq_driver->flags & CPUFREQ_NEED_UPDATE_LIMITS))
		return 0;

	if (cpufreq_driver->target) {
		/*
		 * If the driver hasn't setup a single inefficient frequency,
		 * it's unlikely it knows how to decode CPUFREQ_RELATION_E.
		 */
		if (!policy->efficiencies_available)
			relation &= ~CPUFREQ_RELATION_E;

		return cpufreq_driver->target(policy, target_freq, relation);
	}

	if (!cpufreq_driver->target_index)
		return -EINVAL;

	return __target_index(policy, policy->cached_resolved_idx);
}
EXPORT_SYMBOL_GPL(__cpufreq_driver_target);

int cpufreq_driver_target(struct cpufreq_policy *policy,
			  unsigned int target_freq,
			  unsigned int relation)
{
	int ret;

	down_write(&policy->rwsem);

	ret = __cpufreq_driver_target(policy, target_freq, relation);

	up_write(&policy->rwsem);

	return ret;
}
EXPORT_SYMBOL_GPL(cpufreq_driver_target);

__weak struct cpufreq_governor *cpufreq_fallback_governor(void)
{
	return NULL;
}

static int cpufreq_init_governor(struct cpufreq_policy *policy)
{
	int ret;

	/* Don't start any governor operations if we are entering suspend */
	if (cpufreq_suspended)
		return 0;
	/*
	 * Governor might not be initiated here if ACPI _PPC changed
	 * notification happened, so check it.
	 */
	if (!policy->governor)
		return -EINVAL;

	/* Platform doesn't want dynamic frequency switching ? */
	if (policy->governor->flags & CPUFREQ_GOV_DYNAMIC_SWITCHING &&
	    cpufreq_driver->flags & CPUFREQ_NO_AUTO_DYNAMIC_SWITCHING) {
		struct cpufreq_governor *gov = cpufreq_fallback_governor();

		if (gov) {
			pr_warn("Can't use %s governor as dynamic switching is disallowed. Fallback to %s governor\n",
				policy->governor->name, gov->name);
			policy->governor = gov;
		} else {
			return -EINVAL;
		}
	}

	if (!try_module_get(policy->governor->owner))
		return -EINVAL;

	pr_debug("%s: for CPU %u\n", __func__, policy->cpu);

	if (policy->governor->init) {
		ret = policy->governor->init(policy);
		if (ret) {
			module_put(policy->governor->owner);
			return ret;
		}
	}

	policy->strict_target = !!(policy->governor->flags & CPUFREQ_GOV_STRICT_TARGET);

	return 0;
}

static void cpufreq_exit_governor(struct cpufreq_policy *policy)
{
	if (cpufreq_suspended || !policy->governor)
		return;

	pr_debug("%s: for CPU %u\n", __func__, policy->cpu);

	if (policy->governor->exit)
		policy->governor->exit(policy);

	module_put(policy->governor->owner);
}

int cpufreq_start_governor(struct cpufreq_policy *policy)
{
	int ret;

	if (cpufreq_suspended)
		return 0;

	if (!policy->governor)
		return -EINVAL;

	pr_debug("%s: for CPU %u\n", __func__, policy->cpu);

	if (cpufreq_driver->get)
		cpufreq_verify_current_freq(policy, false);

	if (policy->governor->start) {
		ret = policy->governor->start(policy);
		if (ret)
			return ret;
	}

	if (policy->governor->limits)
		policy->governor->limits(policy);

	return 0;
}

void cpufreq_stop_governor(struct cpufreq_policy *policy)
{
	if (cpufreq_suspended || !policy->governor)
		return;

	pr_debug("%s: for CPU %u\n", __func__, policy->cpu);

	if (policy->governor->stop)
		policy->governor->stop(policy);
}

static void cpufreq_governor_limits(struct cpufreq_policy *policy)
{
	if (cpufreq_suspended || !policy->governor)
		return;

	pr_debug("%s: for CPU %u\n", __func__, policy->cpu);

	if (policy->governor->limits)
		policy->governor->limits(policy);
}

int cpufreq_register_governor(struct cpufreq_governor *governor)
{
	int err;

	if (!governor)
		return -EINVAL;

	if (cpufreq_disabled())
		return -ENODEV;

	mutex_lock(&cpufreq_governor_mutex);

	err = -EBUSY;
	if (!find_governor(governor->name)) {
		err = 0;
		list_add(&governor->governor_list, &cpufreq_governor_list);
	}

	mutex_unlock(&cpufreq_governor_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(cpufreq_register_governor);

void cpufreq_unregister_governor(struct cpufreq_governor *governor)
{
	struct cpufreq_policy *policy;
	unsigned long flags;

	if (!governor)
		return;

	if (cpufreq_disabled())
		return;

	/* clear last_governor for all inactive policies */
	read_lock_irqsave(&cpufreq_driver_lock, flags);
	for_each_inactive_policy(policy) {
		if (!strcmp(policy->last_governor, governor->name)) {
			policy->governor = NULL;
			strcpy(policy->last_governor, "\0");
		}
	}
	read_unlock_irqrestore(&cpufreq_driver_lock, flags);

	mutex_lock(&cpufreq_governor_mutex);
	list_del(&governor->governor_list);
	mutex_unlock(&cpufreq_governor_mutex);
}
EXPORT_SYMBOL_GPL(cpufreq_unregister_governor);


/*********************************************************************
 *                          POLICY INTERFACE                         *
 *********************************************************************/

/**
 * cpufreq_get_policy - get the current cpufreq_policy
 * @policy: struct cpufreq_policy into which the current cpufreq_policy
 *	is written
 * @cpu: CPU to find the policy for
 *
 * Reads the current cpufreq policy.
 */
int cpufreq_get_policy(struct cpufreq_policy *policy, unsigned int cpu)
{
	struct cpufreq_policy *cpu_policy;
	if (!policy)
		return -EINVAL;

	cpu_policy = cpufreq_cpu_get(cpu);
	if (!cpu_policy)
		return -EINVAL;

	memcpy(policy, cpu_policy, sizeof(*policy));

	cpufreq_cpu_put(cpu_policy);
	return 0;
}
EXPORT_SYMBOL(cpufreq_get_policy);

/**
 * cpufreq_set_policy - Modify cpufreq policy parameters.
 * @policy: Policy object to modify.
 * @new_gov: Policy governor pointer.
 * @new_pol: Policy value (for drivers with built-in governors).
 *
 * Invoke the cpufreq driver's ->verify() callback to sanity-check the frequency
 * limits to be set for the policy, update @policy with the verified limits
 * values and either invoke the driver's ->setpolicy() callback (if present) or
 * carry out a governor update for @policy.  That is, run the current governor's
 * ->limits() callback (if @new_gov points to the same object as the one in
 * @policy) or replace the governor for @policy with @new_gov.
 *
 * The cpuinfo part of @policy is not updated by this function.
 */
static int cpufreq_set_policy(struct cpufreq_policy *policy,
			      struct cpufreq_governor *new_gov,
			      unsigned int new_pol)
{
	struct cpufreq_policy_data new_data;
	struct cpufreq_governor *old_gov;
	int ret;

	memcpy(&new_data.cpuinfo, &policy->cpuinfo, sizeof(policy->cpuinfo));
	new_data.freq_table = policy->freq_table;
	new_data.cpu = policy->cpu;
	/*
	 * PM QoS framework collects all the requests from users and provide us
	 * the final aggregated value here.
	 */
	new_data.min = freq_qos_read_value(&policy->constraints, FREQ_QOS_MIN);
	new_data.max = freq_qos_read_value(&policy->constraints, FREQ_QOS_MAX);

	pr_debug("setting new policy for CPU %u: %u - %u kHz\n",
		 new_data.cpu, new_data.min, new_data.max);

	/*
	 * Verify that the CPU speed can be set within these limits and make sure
	 * that min <= max.
	 */
	ret = cpufreq_driver->verify(&new_data);
	if (ret)
		return ret;

	/*
	 * Resolve policy min/max to available frequencies. It ensures
	 * no frequency resolution will neither overshoot the requested maximum
	 * nor undershoot the requested minimum.
	 */
	policy->min = new_data.min;
	policy->max = new_data.max;
	policy->min = __resolve_freq(policy, policy->min, CPUFREQ_RELATION_L);
	policy->max = __resolve_freq(policy, policy->max, CPUFREQ_RELATION_H);
	trace_cpu_frequency_limits(policy);

	policy->cached_target_freq = UINT_MAX;

	pr_debug("new min and max freqs are %u - %u kHz\n",
		 policy->min, policy->max);

	if (cpufreq_driver->setpolicy) {
		policy->policy = new_pol;
		pr_debug("setting range\n");
		return cpufreq_driver->setpolicy(policy);
	}

	if (new_gov == policy->governor) {
		pr_debug("governor limits update\n");
		cpufreq_governor_limits(policy);
		return 0;
	}

	pr_debug("governor switch\n");

	/* save old, working values */
	old_gov = policy->governor;
	/* end old governor */
	if (old_gov) {
		cpufreq_stop_governor(policy);
		cpufreq_exit_governor(policy);
	}

	/* start new governor */
	policy->governor = new_gov;
	ret = cpufreq_init_governor(policy);
	if (!ret) {
		ret = cpufreq_start_governor(policy);
		if (!ret) {
			pr_debug("governor change\n");
			return 0;
		}
		cpufreq_exit_governor(policy);
	}

	/* new governor failed, so re-start old one */
	pr_debug("starting governor %s failed\n", policy->governor->name);
	if (old_gov) {
		policy->governor = old_gov;
		if (cpufreq_init_governor(policy))
			policy->governor = NULL;
		else
			cpufreq_start_governor(policy);
	}

	return ret;
}
EXPORT_TRACEPOINT_SYMBOL_GPL(cpu_frequency_limits);

/**
 * cpufreq_update_policy - Re-evaluate an existing cpufreq policy.
 * @cpu: CPU to re-evaluate the policy for.
 *
 * Update the current frequency for the cpufreq policy of @cpu and use
 * cpufreq_set_policy() to re-apply the min and max limits, which triggers the
 * evaluation of policy notifiers and the cpufreq driver's ->verify() callback
 * for the policy in question, among other things.
 */
void cpufreq_update_policy(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_acquire(cpu);

	if (!policy)
		return;

	/*
	 * BIOS might change freq behind our back
	 * -> ask driver for current freq and notify governors about a change
	 */
	if (cpufreq_driver->get && has_target() &&
	    (cpufreq_suspended || WARN_ON(!cpufreq_verify_current_freq(policy, false))))
		goto unlock;

	refresh_frequency_limits(policy);

unlock:
	cpufreq_cpu_release(policy);
}
EXPORT_SYMBOL(cpufreq_update_policy);

/**
 * cpufreq_update_limits - Update policy limits for a given CPU.
 * @cpu: CPU to update the policy limits for.
 *
 * Invoke the driver's ->update_limits callback if present or call
 * cpufreq_update_policy() for @cpu.
 */
void cpufreq_update_limits(unsigned int cpu)
{
	if (cpufreq_driver->update_limits)
		cpufreq_driver->update_limits(cpu);
	else
		cpufreq_update_policy(cpu);
}
EXPORT_SYMBOL_GPL(cpufreq_update_limits);

/*********************************************************************
 *               BOOST						     *
 *********************************************************************/
static int cpufreq_boost_set_sw(struct cpufreq_policy *policy, int state)
{
	int ret;

	if (!policy->freq_table)
		return -ENXIO;

	ret = cpufreq_frequency_table_cpuinfo(policy, policy->freq_table);
	if (ret) {
		pr_err("%s: Policy frequency update failed\n", __func__);
		return ret;
	}

	ret = freq_qos_update_request(policy->max_freq_req, policy->max);
	if (ret < 0)
		return ret;

	return 0;
}

int cpufreq_boost_trigger_state(int state)
{
	struct cpufreq_policy *policy;
	unsigned long flags;
	int ret = 0;

	if (cpufreq_driver->boost_enabled == state)
		return 0;

	write_lock_irqsave(&cpufreq_driver_lock, flags);
	cpufreq_driver->boost_enabled = state;
	write_unlock_irqrestore(&cpufreq_driver_lock, flags);

	cpus_read_lock();
	for_each_active_policy(policy) {
		ret = cpufreq_driver->set_boost(policy, state);
		if (ret)
			goto err_reset_state;
	}
	cpus_read_unlock();

	return 0;

err_reset_state:
	cpus_read_unlock();

	write_lock_irqsave(&cpufreq_driver_lock, flags);
	cpufreq_driver->boost_enabled = !state;
	write_unlock_irqrestore(&cpufreq_driver_lock, flags);

	pr_err("%s: Cannot %s BOOST\n",
	       __func__, state ? "enable" : "disable");

	return ret;
}

static bool cpufreq_boost_supported(void)
{
	return cpufreq_driver->set_boost;
}

static int create_boost_sysfs_file(void)
{
	int ret;

	ret = sysfs_create_file(cpufreq_global_kobject, &boost.attr);
	if (ret)
		pr_err("%s: cannot register global BOOST sysfs file\n",
		       __func__);

	return ret;
}

static void remove_boost_sysfs_file(void)
{
	if (cpufreq_boost_supported())
		sysfs_remove_file(cpufreq_global_kobject, &boost.attr);
}

int cpufreq_enable_boost_support(void)
{
	if (!cpufreq_driver)
		return -EINVAL;

	if (cpufreq_boost_supported())
		return 0;

	cpufreq_driver->set_boost = cpufreq_boost_set_sw;

	/* This will get removed on driver unregister */
	return create_boost_sysfs_file();
}
EXPORT_SYMBOL_GPL(cpufreq_enable_boost_support);

int cpufreq_boost_enabled(void)
{
	return cpufreq_driver->boost_enabled;
}
EXPORT_SYMBOL_GPL(cpufreq_boost_enabled);

/*********************************************************************
 *               REGISTER / UNREGISTER CPUFREQ DRIVER                *
 *********************************************************************/
static enum cpuhp_state hp_online;

static int cpuhp_cpufreq_online(unsigned int cpu)
{
	cpufreq_online(cpu);

	return 0;
}

static int cpuhp_cpufreq_offline(unsigned int cpu)
{
	cpufreq_offline(cpu);

	return 0;
}

/**
 * cpufreq_register_driver - register a CPU Frequency driver
 * @driver_data: A struct cpufreq_driver containing the values#
 * submitted by the CPU Frequency driver.
 *
 * Registers a CPU Frequency driver to this core code. This code
 * returns zero on success, -EEXIST when another driver got here first
 * (and isn't unregistered in the meantime).
 *
 */
int cpufreq_register_driver(struct cpufreq_driver *driver_data)
{
	unsigned long flags;
	int ret;

	if (cpufreq_disabled())
		return -ENODEV;

	/*
	 * The cpufreq core depends heavily on the availability of device
	 * structure, make sure they are available before proceeding further.
	 */
	if (!get_cpu_device(0))
		return -EPROBE_DEFER;

	if (!driver_data || !driver_data->verify || !driver_data->init ||
	    !(driver_data->setpolicy || driver_data->target_index ||
		    driver_data->target) ||
	     (driver_data->setpolicy && (driver_data->target_index ||
		    driver_data->target)) ||
	     (!driver_data->get_intermediate != !driver_data->target_intermediate) ||
	     (!driver_data->online != !driver_data->offline))
		return -EINVAL;

	pr_debug("trying to register driver %s\n", driver_data->name);

	/* Protect against concurrent CPU online/offline. */
	cpus_read_lock();

	write_lock_irqsave(&cpufreq_driver_lock, flags);
	if (cpufreq_driver) {
		write_unlock_irqrestore(&cpufreq_driver_lock, flags);
		ret = -EEXIST;
		goto out;
	}
	cpufreq_driver = driver_data;
	write_unlock_irqrestore(&cpufreq_driver_lock, flags);

	/*
	 * Mark support for the scheduler's frequency invariance engine for
	 * drivers that implement target(), target_index() or fast_switch().
	 */
	if (!cpufreq_driver->setpolicy) {
		static_branch_enable_cpuslocked(&cpufreq_freq_invariance);
		pr_debug("supports frequency invariance");
	}

	if (driver_data->setpolicy)
		driver_data->flags |= CPUFREQ_CONST_LOOPS;

	if (cpufreq_boost_supported()) {
		ret = create_boost_sysfs_file();
		if (ret)
			goto err_null_driver;
	}

	ret = subsys_interface_register(&cpufreq_interface);
	if (ret)
		goto err_boost_unreg;

	if (unlikely(list_empty(&cpufreq_policy_list))) {
		/* if all ->init() calls failed, unregister */
		ret = -ENODEV;
		pr_debug("%s: No CPU initialized for driver %s\n", __func__,
			 driver_data->name);
		goto err_if_unreg;
	}

	ret = cpuhp_setup_state_nocalls_cpuslocked(CPUHP_AP_ONLINE_DYN,
						   "cpufreq:online",
						   cpuhp_cpufreq_online,
						   cpuhp_cpufreq_offline);
	if (ret < 0)
		goto err_if_unreg;
	hp_online = ret;
	ret = 0;

	pr_debug("driver %s up and running\n", driver_data->name);
	goto out;

err_if_unreg:
	subsys_interface_unregister(&cpufreq_interface);
err_boost_unreg:
	remove_boost_sysfs_file();
err_null_driver:
	write_lock_irqsave(&cpufreq_driver_lock, flags);
	cpufreq_driver = NULL;
	write_unlock_irqrestore(&cpufreq_driver_lock, flags);
out:
	cpus_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(cpufreq_register_driver);

/*
 * cpufreq_unregister_driver - unregister the current CPUFreq driver
 *
 * Unregister the current CPUFreq driver. Only call this if you have
 * the right to do so, i.e. if you have succeeded in initialising before!
 * Returns zero if successful, and -EINVAL if the cpufreq_driver is
 * currently not initialised.
 */
int cpufreq_unregister_driver(struct cpufreq_driver *driver)
{
	unsigned long flags;

	if (!cpufreq_driver || (driver != cpufreq_driver))
		return -EINVAL;

	pr_debug("unregistering driver %s\n", driver->name);

	/* Protect against concurrent cpu hotplug */
	cpus_read_lock();
	subsys_interface_unregister(&cpufreq_interface);
	remove_boost_sysfs_file();
	static_branch_disable_cpuslocked(&cpufreq_freq_invariance);
	cpuhp_remove_state_nocalls_cpuslocked(hp_online);

	write_lock_irqsave(&cpufreq_driver_lock, flags);

	cpufreq_driver = NULL;

	write_unlock_irqrestore(&cpufreq_driver_lock, flags);
	cpus_read_unlock();

	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_unregister_driver);

static int __init cpufreq_core_init(void)
{
	struct cpufreq_governor *gov = cpufreq_default_governor();

	if (cpufreq_disabled())
		return -ENODEV;

	cpufreq_global_kobject = kobject_create_and_add("cpufreq", &cpu_subsys.dev_root->kobj);
	BUG_ON(!cpufreq_global_kobject);

	if (!strlen(default_governor))
		strncpy(default_governor, gov->name, CPUFREQ_NAME_LEN);

	return 0;
}
module_param(off, int, 0444);
module_param_string(default_governor, default_governor, CPUFREQ_NAME_LEN, 0444);
core_initcall(cpufreq_core_init);
