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
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>
#include <linux/tick.h>
#include <trace/events/power.h>

/**
 * The "cpufreq driver" - the arch- or hardware-dependent low
 * level driver of CPUFreq support, and its spinlock. This lock
 * also protects the cpufreq_cpu_data array.
 */
static struct cpufreq_driver *cpufreq_driver;
static DEFINE_PER_CPU(struct cpufreq_policy *, cpufreq_cpu_data);
static DEFINE_PER_CPU(struct cpufreq_policy *, cpufreq_cpu_data_fallback);
static DEFINE_RWLOCK(cpufreq_driver_lock);
static DEFINE_MUTEX(cpufreq_governor_lock);
static LIST_HEAD(cpufreq_policy_list);

#ifdef CONFIG_HOTPLUG_CPU
/* This one keeps track of the previously set governor of a removed CPU */
static DEFINE_PER_CPU(char[CPUFREQ_NAME_LEN], cpufreq_cpu_governor);
#endif

/*
 * cpu_policy_rwsem is a per CPU reader-writer semaphore designed to cure
 * all cpufreq/hotplug/workqueue/etc related lock issues.
 *
 * The rules for this semaphore:
 * - Any routine that wants to read from the policy structure will
 *   do a down_read on this semaphore.
 * - Any routine that will write to the policy structure and/or may take away
 *   the policy altogether (eg. CPU hotplug), will hold this lock in write
 *   mode before doing so.
 *
 * Additional rules:
 * - Governor routines that can be called in cpufreq hotplug path should not
 *   take this sem as top level hotplug notifier handler takes this.
 * - Lock should not be held across
 *     __cpufreq_governor(data, CPUFREQ_GOV_STOP);
 */
static DEFINE_PER_CPU(struct rw_semaphore, cpu_policy_rwsem);

#define lock_policy_rwsem(mode, cpu)					\
static int lock_policy_rwsem_##mode(int cpu)				\
{									\
	struct cpufreq_policy *policy = per_cpu(cpufreq_cpu_data, cpu);	\
	BUG_ON(!policy);						\
	down_##mode(&per_cpu(cpu_policy_rwsem, policy->cpu));		\
									\
	return 0;							\
}

lock_policy_rwsem(read, cpu);
lock_policy_rwsem(write, cpu);

#define unlock_policy_rwsem(mode, cpu)					\
static void unlock_policy_rwsem_##mode(int cpu)				\
{									\
	struct cpufreq_policy *policy = per_cpu(cpufreq_cpu_data, cpu);	\
	BUG_ON(!policy);						\
	up_##mode(&per_cpu(cpu_policy_rwsem, policy->cpu));		\
}

unlock_policy_rwsem(read, cpu);
unlock_policy_rwsem(write, cpu);

/*
 * rwsem to guarantee that cpufreq driver module doesn't unload during critical
 * sections
 */
static DECLARE_RWSEM(cpufreq_rwsem);

/* internal prototypes */
static int __cpufreq_governor(struct cpufreq_policy *policy,
		unsigned int event);
static unsigned int __cpufreq_get(unsigned int cpu);
static void handle_update(struct work_struct *work);

/**
 * Two notifier lists: the "policy" list is involved in the
 * validation process for a new CPU frequency policy; the
 * "transition" list for kernel code that needs to handle
 * changes to devices when the CPU clock speed changes.
 * The mutex locks both lists.
 */
static BLOCKING_NOTIFIER_HEAD(cpufreq_policy_notifier_list);
static struct srcu_notifier_head cpufreq_transition_notifier_list;

static bool init_cpufreq_transition_notifier_list_called;
static int __init init_cpufreq_transition_notifier_list(void)
{
	srcu_init_notifier_head(&cpufreq_transition_notifier_list);
	init_cpufreq_transition_notifier_list_called = true;
	return 0;
}
pure_initcall(init_cpufreq_transition_notifier_list);

static int off __read_mostly;
static int cpufreq_disabled(void)
{
	return off;
}
void disable_cpufreq(void)
{
	off = 1;
}
static LIST_HEAD(cpufreq_governor_list);
static DEFINE_MUTEX(cpufreq_governor_mutex);

bool have_governor_per_policy(void)
{
	return cpufreq_driver->have_governor_per_policy;
}
EXPORT_SYMBOL_GPL(have_governor_per_policy);

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
	u64 idle_time;
	u64 cur_wall_time;
	u64 busy_time;

	cur_wall_time = jiffies64_to_cputime64(get_jiffies_64());

	busy_time = kcpustat_cpu(cpu).cpustat[CPUTIME_USER];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_SYSTEM];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_IRQ];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_SOFTIRQ];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_STEAL];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_NICE];

	idle_time = cur_wall_time - busy_time;
	if (wall)
		*wall = cputime_to_usecs(cur_wall_time);

	return cputime_to_usecs(idle_time);
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

struct cpufreq_policy *cpufreq_cpu_get(unsigned int cpu)
{
	struct cpufreq_policy *policy = NULL;
	unsigned long flags;

	if (cpufreq_disabled() || (cpu >= nr_cpu_ids))
		return NULL;

	if (!down_read_trylock(&cpufreq_rwsem))
		return NULL;

	/* get the cpufreq driver */
	read_lock_irqsave(&cpufreq_driver_lock, flags);

	if (cpufreq_driver) {
		/* get the CPU */
		policy = per_cpu(cpufreq_cpu_data, cpu);
		if (policy)
			kobject_get(&policy->kobj);
	}

	read_unlock_irqrestore(&cpufreq_driver_lock, flags);

	if (!policy)
		up_read(&cpufreq_rwsem);

	return policy;
}
EXPORT_SYMBOL_GPL(cpufreq_cpu_get);

void cpufreq_cpu_put(struct cpufreq_policy *policy)
{
	if (cpufreq_disabled())
		return;

	kobject_put(&policy->kobj);
	up_read(&cpufreq_rwsem);
}
EXPORT_SYMBOL_GPL(cpufreq_cpu_put);

/*********************************************************************
 *            EXTERNALLY AFFECTING FREQUENCY CHANGES                 *
 *********************************************************************/

/**
 * adjust_jiffies - adjust the system "loops_per_jiffy"
 *
 * This function alters the system "loops_per_jiffy" for the clock
 * speed change. Note that loops_per_jiffy cannot be updated on SMP
 * systems as each CPU might be scaled differently. So, use the arch
 * per-CPU loops_per_jiffy value wherever possible.
 */
#ifndef CONFIG_SMP
static unsigned long l_p_j_ref;
static unsigned int l_p_j_ref_freq;

static void adjust_jiffies(unsigned long val, struct cpufreq_freqs *ci)
{
	if (ci->flags & CPUFREQ_CONST_LOOPS)
		return;

	if (!l_p_j_ref_freq) {
		l_p_j_ref = loops_per_jiffy;
		l_p_j_ref_freq = ci->old;
		pr_debug("saving %lu as reference value for loops_per_jiffy; "
			"freq is %u kHz\n", l_p_j_ref, l_p_j_ref_freq);
	}
	if ((val == CPUFREQ_POSTCHANGE && ci->old != ci->new) ||
	    (val == CPUFREQ_RESUMECHANGE || val == CPUFREQ_SUSPENDCHANGE)) {
		loops_per_jiffy = cpufreq_scale(l_p_j_ref, l_p_j_ref_freq,
								ci->new);
		pr_debug("scaling loops_per_jiffy to %lu "
			"for frequency %u kHz\n", loops_per_jiffy, ci->new);
	}
}
#else
static inline void adjust_jiffies(unsigned long val, struct cpufreq_freqs *ci)
{
	return;
}
#endif

static void __cpufreq_notify_transition(struct cpufreq_policy *policy,
		struct cpufreq_freqs *freqs, unsigned int state)
{
	BUG_ON(irqs_disabled());

	if (cpufreq_disabled())
		return;

	freqs->flags = cpufreq_driver->flags;
	pr_debug("notification %u of frequency transition to %u kHz\n",
		state, freqs->new);

	switch (state) {

	case CPUFREQ_PRECHANGE:
		if (WARN(policy->transition_ongoing ==
					cpumask_weight(policy->cpus),
				"In middle of another frequency transition\n"))
			return;

		policy->transition_ongoing++;

		/* detect if the driver reported a value as "old frequency"
		 * which is not equal to what the cpufreq core thinks is
		 * "old frequency".
		 */
		if (!(cpufreq_driver->flags & CPUFREQ_CONST_LOOPS)) {
			if ((policy) && (policy->cpu == freqs->cpu) &&
			    (policy->cur) && (policy->cur != freqs->old)) {
				pr_debug("Warning: CPU frequency is"
					" %u, cpufreq assumed %u kHz.\n",
					freqs->old, policy->cur);
				freqs->old = policy->cur;
			}
		}
		srcu_notifier_call_chain(&cpufreq_transition_notifier_list,
				CPUFREQ_PRECHANGE, freqs);
		adjust_jiffies(CPUFREQ_PRECHANGE, freqs);
		break;

	case CPUFREQ_POSTCHANGE:
		if (WARN(!policy->transition_ongoing,
				"No frequency transition in progress\n"))
			return;

		policy->transition_ongoing--;

		adjust_jiffies(CPUFREQ_POSTCHANGE, freqs);
		pr_debug("FREQ: %lu - CPU: %lu", (unsigned long)freqs->new,
			(unsigned long)freqs->cpu);
		trace_cpu_frequency(freqs->new, freqs->cpu);
		srcu_notifier_call_chain(&cpufreq_transition_notifier_list,
				CPUFREQ_POSTCHANGE, freqs);
		if (likely(policy) && likely(policy->cpu == freqs->cpu))
			policy->cur = freqs->new;
		break;
	}
}

/**
 * cpufreq_notify_transition - call notifier chain and adjust_jiffies
 * on frequency transition.
 *
 * This function calls the transition notifiers and the "adjust_jiffies"
 * function. It is called twice on all CPU frequency changes that have
 * external effects.
 */
void cpufreq_notify_transition(struct cpufreq_policy *policy,
		struct cpufreq_freqs *freqs, unsigned int state)
{
	for_each_cpu(freqs->cpu, policy->cpus)
		__cpufreq_notify_transition(policy, freqs, state);
}
EXPORT_SYMBOL_GPL(cpufreq_notify_transition);


/*********************************************************************
 *                          SYSFS INTERFACE                          *
 *********************************************************************/

static struct cpufreq_governor *__find_governor(const char *str_governor)
{
	struct cpufreq_governor *t;

	list_for_each_entry(t, &cpufreq_governor_list, governor_list)
		if (!strnicmp(str_governor, t->name, CPUFREQ_NAME_LEN))
			return t;

	return NULL;
}

/**
 * cpufreq_parse_governor - parse a governor string
 */
static int cpufreq_parse_governor(char *str_governor, unsigned int *policy,
				struct cpufreq_governor **governor)
{
	int err = -EINVAL;

	if (!cpufreq_driver)
		goto out;

	if (cpufreq_driver->setpolicy) {
		if (!strnicmp(str_governor, "performance", CPUFREQ_NAME_LEN)) {
			*policy = CPUFREQ_POLICY_PERFORMANCE;
			err = 0;
		} else if (!strnicmp(str_governor, "powersave",
						CPUFREQ_NAME_LEN)) {
			*policy = CPUFREQ_POLICY_POWERSAVE;
			err = 0;
		}
	} else if (cpufreq_driver->target) {
		struct cpufreq_governor *t;

		mutex_lock(&cpufreq_governor_mutex);

		t = __find_governor(str_governor);

		if (t == NULL) {
			int ret;

			mutex_unlock(&cpufreq_governor_mutex);
			ret = request_module("cpufreq_%s", str_governor);
			mutex_lock(&cpufreq_governor_mutex);

			if (ret == 0)
				t = __find_governor(str_governor);
		}

		if (t != NULL) {
			*governor = t;
			err = 0;
		}

		mutex_unlock(&cpufreq_governor_mutex);
	}
out:
	return err;
}

/**
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

show_one(cpuinfo_min_freq, cpuinfo.min_freq);
show_one(cpuinfo_max_freq, cpuinfo.max_freq);
show_one(cpuinfo_transition_latency, cpuinfo.transition_latency);
show_one(scaling_min_freq, min);
show_one(scaling_max_freq, max);
show_one(scaling_cur_freq, cur);

static int __cpufreq_set_policy(struct cpufreq_policy *policy,
				struct cpufreq_policy *new_policy);

/**
 * cpufreq_per_cpu_attr_write() / store_##file_name() - sysfs write access
 */
#define store_one(file_name, object)			\
static ssize_t store_##file_name					\
(struct cpufreq_policy *policy, const char *buf, size_t count)		\
{									\
	unsigned int ret;						\
	struct cpufreq_policy new_policy;				\
									\
	ret = cpufreq_get_policy(&new_policy, policy->cpu);		\
	if (ret)							\
		return -EINVAL;						\
									\
	ret = sscanf(buf, "%u", &new_policy.object);			\
	if (ret != 1)							\
		return -EINVAL;						\
									\
	ret = __cpufreq_set_policy(policy, &new_policy);		\
	policy->user_policy.object = policy->object;			\
									\
	return ret ? ret : count;					\
}

store_one(scaling_min_freq, min);
store_one(scaling_max_freq, max);

/**
 * show_cpuinfo_cur_freq - current CPU frequency as detected by hardware
 */
static ssize_t show_cpuinfo_cur_freq(struct cpufreq_policy *policy,
					char *buf)
{
	unsigned int cur_freq = __cpufreq_get(policy->cpu);
	if (!cur_freq)
		return sprintf(buf, "<unknown>");
	return sprintf(buf, "%u\n", cur_freq);
}

/**
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

/**
 * store_scaling_governor - store policy for the specified CPU
 */
static ssize_t store_scaling_governor(struct cpufreq_policy *policy,
					const char *buf, size_t count)
{
	unsigned int ret;
	char	str_governor[16];
	struct cpufreq_policy new_policy;

	ret = cpufreq_get_policy(&new_policy, policy->cpu);
	if (ret)
		return ret;

	ret = sscanf(buf, "%15s", str_governor);
	if (ret != 1)
		return -EINVAL;

	if (cpufreq_parse_governor(str_governor, &new_policy.policy,
						&new_policy.governor))
		return -EINVAL;

	/*
	 * Do not use cpufreq_set_policy here or the user_policy.max
	 * will be wrongly overridden
	 */
	ret = __cpufreq_set_policy(policy, &new_policy);

	policy->user_policy.policy = policy->policy;
	policy->user_policy.governor = policy->governor;

	if (ret)
		return ret;
	else
		return count;
}

/**
 * show_scaling_driver - show the cpufreq driver currently loaded
 */
static ssize_t show_scaling_driver(struct cpufreq_policy *policy, char *buf)
{
	return scnprintf(buf, CPUFREQ_NAME_PLEN, "%s\n", cpufreq_driver->name);
}

/**
 * show_scaling_available_governors - show the available CPUfreq governors
 */
static ssize_t show_scaling_available_governors(struct cpufreq_policy *policy,
						char *buf)
{
	ssize_t i = 0;
	struct cpufreq_governor *t;

	if (!cpufreq_driver->target) {
		i += sprintf(buf, "performance powersave");
		goto out;
	}

	list_for_each_entry(t, &cpufreq_governor_list, governor_list) {
		if (i >= (ssize_t) ((PAGE_SIZE / sizeof(char))
		    - (CPUFREQ_NAME_LEN + 2)))
			goto out;
		i += scnprintf(&buf[i], CPUFREQ_NAME_PLEN, "%s ", t->name);
	}
out:
	i += sprintf(&buf[i], "\n");
	return i;
}

ssize_t cpufreq_show_cpus(const struct cpumask *mask, char *buf)
{
	ssize_t i = 0;
	unsigned int cpu;

	for_each_cpu(cpu, mask) {
		if (i)
			i += scnprintf(&buf[i], (PAGE_SIZE - i - 2), " ");
		i += scnprintf(&buf[i], (PAGE_SIZE - i - 2), "%u", cpu);
		if (i >= (PAGE_SIZE - 5))
			break;
	}
	i += sprintf(&buf[i], "\n");
	return i;
}
EXPORT_SYMBOL_GPL(cpufreq_show_cpus);

/**
 * show_related_cpus - show the CPUs affected by each transition even if
 * hw coordination is in use
 */
static ssize_t show_related_cpus(struct cpufreq_policy *policy, char *buf)
{
	return cpufreq_show_cpus(policy->related_cpus, buf);
}

/**
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

/**
 * show_bios_limit - show the current cpufreq HW/BIOS limitation
 */
static ssize_t show_bios_limit(struct cpufreq_policy *policy, char *buf)
{
	unsigned int limit;
	int ret;
	if (cpufreq_driver->bios_limit) {
		ret = cpufreq_driver->bios_limit(policy->cpu, &limit);
		if (!ret)
			return sprintf(buf, "%u\n", limit);
	}
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

static struct attribute *default_attrs[] = {
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

#define to_policy(k) container_of(k, struct cpufreq_policy, kobj)
#define to_attr(a) container_of(a, struct freq_attr, attr)

static ssize_t show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct cpufreq_policy *policy = to_policy(kobj);
	struct freq_attr *fattr = to_attr(attr);
	ssize_t ret = -EINVAL;

	if (!down_read_trylock(&cpufreq_rwsem))
		goto exit;

	if (lock_policy_rwsem_read(policy->cpu) < 0)
		goto up_read;

	if (fattr->show)
		ret = fattr->show(policy, buf);
	else
		ret = -EIO;

	unlock_policy_rwsem_read(policy->cpu);

up_read:
	up_read(&cpufreq_rwsem);
exit:
	return ret;
}

static ssize_t store(struct kobject *kobj, struct attribute *attr,
		     const char *buf, size_t count)
{
	struct cpufreq_policy *policy = to_policy(kobj);
	struct freq_attr *fattr = to_attr(attr);
	ssize_t ret = -EINVAL;

	if (!down_read_trylock(&cpufreq_rwsem))
		goto exit;

	if (lock_policy_rwsem_write(policy->cpu) < 0)
		goto up_read;

	if (fattr->store)
		ret = fattr->store(policy, buf, count);
	else
		ret = -EIO;

	unlock_policy_rwsem_write(policy->cpu);

up_read:
	up_read(&cpufreq_rwsem);
exit:
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
	.default_attrs	= default_attrs,
	.release	= cpufreq_sysfs_release,
};

struct kobject *cpufreq_global_kobject;
EXPORT_SYMBOL(cpufreq_global_kobject);

static int cpufreq_global_kobject_usage;

int cpufreq_get_global_kobject(void)
{
	if (!cpufreq_global_kobject_usage++)
		return kobject_add(cpufreq_global_kobject,
				&cpu_subsys.dev_root->kobj, "%s", "cpufreq");

	return 0;
}
EXPORT_SYMBOL(cpufreq_get_global_kobject);

void cpufreq_put_global_kobject(void)
{
	if (!--cpufreq_global_kobject_usage)
		kobject_del(cpufreq_global_kobject);
}
EXPORT_SYMBOL(cpufreq_put_global_kobject);

int cpufreq_sysfs_create_file(const struct attribute *attr)
{
	int ret = cpufreq_get_global_kobject();

	if (!ret) {
		ret = sysfs_create_file(cpufreq_global_kobject, attr);
		if (ret)
			cpufreq_put_global_kobject();
	}

	return ret;
}
EXPORT_SYMBOL(cpufreq_sysfs_create_file);

void cpufreq_sysfs_remove_file(const struct attribute *attr)
{
	sysfs_remove_file(cpufreq_global_kobject, attr);
	cpufreq_put_global_kobject();
}
EXPORT_SYMBOL(cpufreq_sysfs_remove_file);

/* symlink affected CPUs */
static int cpufreq_add_dev_symlink(struct cpufreq_policy *policy)
{
	unsigned int j;
	int ret = 0;

	for_each_cpu(j, policy->cpus) {
		struct device *cpu_dev;

		if (j == policy->cpu)
			continue;

		pr_debug("Adding link for CPU: %u\n", j);
		cpu_dev = get_cpu_device(j);
		ret = sysfs_create_link(&cpu_dev->kobj, &policy->kobj,
					"cpufreq");
		if (ret)
			break;
	}
	return ret;
}

static int cpufreq_add_dev_interface(struct cpufreq_policy *policy,
				     struct device *dev)
{
	struct freq_attr **drv_attr;
	int ret = 0;

	/* prepare interface data */
	ret = kobject_init_and_add(&policy->kobj, &ktype_cpufreq,
				   &dev->kobj, "cpufreq");
	if (ret)
		return ret;

	/* set up files for this cpu device */
	drv_attr = cpufreq_driver->attr;
	while ((drv_attr) && (*drv_attr)) {
		ret = sysfs_create_file(&policy->kobj, &((*drv_attr)->attr));
		if (ret)
			goto err_out_kobj_put;
		drv_attr++;
	}
	if (cpufreq_driver->get) {
		ret = sysfs_create_file(&policy->kobj, &cpuinfo_cur_freq.attr);
		if (ret)
			goto err_out_kobj_put;
	}
	if (cpufreq_driver->target) {
		ret = sysfs_create_file(&policy->kobj, &scaling_cur_freq.attr);
		if (ret)
			goto err_out_kobj_put;
	}
	if (cpufreq_driver->bios_limit) {
		ret = sysfs_create_file(&policy->kobj, &bios_limit.attr);
		if (ret)
			goto err_out_kobj_put;
	}

	ret = cpufreq_add_dev_symlink(policy);
	if (ret)
		goto err_out_kobj_put;

	return ret;

err_out_kobj_put:
	kobject_put(&policy->kobj);
	wait_for_completion(&policy->kobj_unregister);
	return ret;
}

static void cpufreq_init_policy(struct cpufreq_policy *policy)
{
	struct cpufreq_policy new_policy;
	int ret = 0;

	memcpy(&new_policy, policy, sizeof(*policy));
	/* assure that the starting sequence is run in __cpufreq_set_policy */
	policy->governor = NULL;

	/* set default policy */
	ret = __cpufreq_set_policy(policy, &new_policy);
	policy->user_policy.policy = policy->policy;
	policy->user_policy.governor = policy->governor;

	if (ret) {
		pr_debug("setting policy failed\n");
		if (cpufreq_driver->exit)
			cpufreq_driver->exit(policy);
	}
}

#ifdef CONFIG_HOTPLUG_CPU
static int cpufreq_add_policy_cpu(struct cpufreq_policy *policy,
				  unsigned int cpu, struct device *dev,
				  bool frozen)
{
	int ret = 0, has_target = !!cpufreq_driver->target;
	unsigned long flags;

	if (has_target) {
		ret = __cpufreq_governor(policy, CPUFREQ_GOV_STOP);
		if (ret) {
			pr_err("%s: Failed to stop governor\n", __func__);
			return ret;
		}
	}

	lock_policy_rwsem_write(policy->cpu);

	write_lock_irqsave(&cpufreq_driver_lock, flags);

	cpumask_set_cpu(cpu, policy->cpus);
	per_cpu(cpufreq_cpu_data, cpu) = policy;
	write_unlock_irqrestore(&cpufreq_driver_lock, flags);

	unlock_policy_rwsem_write(policy->cpu);

	if (has_target) {
		if ((ret = __cpufreq_governor(policy, CPUFREQ_GOV_START)) ||
			(ret = __cpufreq_governor(policy, CPUFREQ_GOV_LIMITS))) {
			pr_err("%s: Failed to start governor\n", __func__);
			return ret;
		}
	}

	/* Don't touch sysfs links during light-weight init */
	if (!frozen)
		ret = sysfs_create_link(&dev->kobj, &policy->kobj, "cpufreq");

	return ret;
}
#endif

static struct cpufreq_policy *cpufreq_policy_restore(unsigned int cpu)
{
	struct cpufreq_policy *policy;
	unsigned long flags;

	write_lock_irqsave(&cpufreq_driver_lock, flags);

	policy = per_cpu(cpufreq_cpu_data_fallback, cpu);

	write_unlock_irqrestore(&cpufreq_driver_lock, flags);

	return policy;
}

static struct cpufreq_policy *cpufreq_policy_alloc(void)
{
	struct cpufreq_policy *policy;

	policy = kzalloc(sizeof(*policy), GFP_KERNEL);
	if (!policy)
		return NULL;

	if (!alloc_cpumask_var(&policy->cpus, GFP_KERNEL))
		goto err_free_policy;

	if (!zalloc_cpumask_var(&policy->related_cpus, GFP_KERNEL))
		goto err_free_cpumask;

	INIT_LIST_HEAD(&policy->policy_list);
	return policy;

err_free_cpumask:
	free_cpumask_var(policy->cpus);
err_free_policy:
	kfree(policy);

	return NULL;
}

static void cpufreq_policy_free(struct cpufreq_policy *policy)
{
	free_cpumask_var(policy->related_cpus);
	free_cpumask_var(policy->cpus);
	kfree(policy);
}

static int __cpufreq_add_dev(struct device *dev, struct subsys_interface *sif,
			     bool frozen)
{
	unsigned int j, cpu = dev->id;
	int ret = -ENOMEM;
	struct cpufreq_policy *policy;
	unsigned long flags;
#ifdef CONFIG_HOTPLUG_CPU
	struct cpufreq_policy *tpolicy;
	struct cpufreq_governor *gov;
#endif

	if (cpu_is_offline(cpu))
		return 0;

	pr_debug("adding CPU %u\n", cpu);

#ifdef CONFIG_SMP
	/* check whether a different CPU already registered this
	 * CPU because it is in the same boat. */
	policy = cpufreq_cpu_get(cpu);
	if (unlikely(policy)) {
		cpufreq_cpu_put(policy);
		return 0;
	}
#endif

	if (!down_read_trylock(&cpufreq_rwsem))
		return 0;

#ifdef CONFIG_HOTPLUG_CPU
	/* Check if this cpu was hot-unplugged earlier and has siblings */
	read_lock_irqsave(&cpufreq_driver_lock, flags);
	list_for_each_entry(tpolicy, &cpufreq_policy_list, policy_list) {
		if (cpumask_test_cpu(cpu, tpolicy->related_cpus)) {
			read_unlock_irqrestore(&cpufreq_driver_lock, flags);
			ret = cpufreq_add_policy_cpu(tpolicy, cpu, dev, frozen);
			up_read(&cpufreq_rwsem);
			return ret;
		}
	}
	read_unlock_irqrestore(&cpufreq_driver_lock, flags);
#endif

	if (frozen)
		/* Restore the saved policy when doing light-weight init */
		policy = cpufreq_policy_restore(cpu);
	else
		policy = cpufreq_policy_alloc();

	if (!policy)
		goto nomem_out;

	policy->cpu = cpu;
	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;
	cpumask_copy(policy->cpus, cpumask_of(cpu));

	init_completion(&policy->kobj_unregister);
	INIT_WORK(&policy->update, handle_update);

	/* call driver. From then on the cpufreq must be able
	 * to accept all calls to ->verify and ->setpolicy for this CPU
	 */
	ret = cpufreq_driver->init(policy);
	if (ret) {
		pr_debug("initialization failed\n");
		goto err_set_policy_cpu;
	}

	/* related cpus should atleast have policy->cpus */
	cpumask_or(policy->related_cpus, policy->related_cpus, policy->cpus);

	/*
	 * affected cpus must always be the one, which are online. We aren't
	 * managing offline cpus here.
	 */
	cpumask_and(policy->cpus, policy->cpus, cpu_online_mask);

	policy->user_policy.min = policy->min;
	policy->user_policy.max = policy->max;

	blocking_notifier_call_chain(&cpufreq_policy_notifier_list,
				     CPUFREQ_START, policy);

#ifdef CONFIG_HOTPLUG_CPU
	gov = __find_governor(per_cpu(cpufreq_cpu_governor, cpu));
	if (gov) {
		policy->governor = gov;
		pr_debug("Restoring governor %s for cpu %d\n",
		       policy->governor->name, cpu);
	}
#endif

	write_lock_irqsave(&cpufreq_driver_lock, flags);
	for_each_cpu(j, policy->cpus)
		per_cpu(cpufreq_cpu_data, j) = policy;
	write_unlock_irqrestore(&cpufreq_driver_lock, flags);

	if (!frozen) {
		ret = cpufreq_add_dev_interface(policy, dev);
		if (ret)
			goto err_out_unregister;
	}

	write_lock_irqsave(&cpufreq_driver_lock, flags);
	list_add(&policy->policy_list, &cpufreq_policy_list);
	write_unlock_irqrestore(&cpufreq_driver_lock, flags);

	cpufreq_init_policy(policy);

	kobject_uevent(&policy->kobj, KOBJ_ADD);
	up_read(&cpufreq_rwsem);

	pr_debug("initialization complete\n");

	return 0;

err_out_unregister:
	write_lock_irqsave(&cpufreq_driver_lock, flags);
	for_each_cpu(j, policy->cpus)
		per_cpu(cpufreq_cpu_data, j) = NULL;
	write_unlock_irqrestore(&cpufreq_driver_lock, flags);

err_set_policy_cpu:
	cpufreq_policy_free(policy);
nomem_out:
	up_read(&cpufreq_rwsem);

	return ret;
}

/**
 * cpufreq_add_dev - add a CPU device
 *
 * Adds the cpufreq interface for a CPU device.
 *
 * The Oracle says: try running cpufreq registration/unregistration concurrently
 * with with cpu hotplugging and all hell will break loose. Tried to clean this
 * mess up, but more thorough testing is needed. - Mathieu
 */
static int cpufreq_add_dev(struct device *dev, struct subsys_interface *sif)
{
	return __cpufreq_add_dev(dev, sif, false);
}

static void update_policy_cpu(struct cpufreq_policy *policy, unsigned int cpu)
{
	policy->last_cpu = policy->cpu;
	policy->cpu = cpu;

#ifdef CONFIG_CPU_FREQ_TABLE
	cpufreq_frequency_table_update_policy_cpu(policy);
#endif
	blocking_notifier_call_chain(&cpufreq_policy_notifier_list,
			CPUFREQ_UPDATE_POLICY_CPU, policy);
}

static int cpufreq_nominate_new_policy_cpu(struct cpufreq_policy *policy,
					   unsigned int old_cpu, bool frozen)
{
	struct device *cpu_dev;
	int ret;

	/* first sibling now owns the new sysfs dir */
	cpu_dev = get_cpu_device(cpumask_first(policy->cpus));

	/* Don't touch sysfs files during light-weight tear-down */
	if (frozen)
		return cpu_dev->id;

	sysfs_remove_link(&cpu_dev->kobj, "cpufreq");
	ret = kobject_move(&policy->kobj, &cpu_dev->kobj);
	if (ret) {
		pr_err("%s: Failed to move kobj: %d", __func__, ret);

		WARN_ON(lock_policy_rwsem_write(old_cpu));
		cpumask_set_cpu(old_cpu, policy->cpus);
		unlock_policy_rwsem_write(old_cpu);

		ret = sysfs_create_link(&cpu_dev->kobj, &policy->kobj,
					"cpufreq");

		return -EINVAL;
	}

	return cpu_dev->id;
}

static int __cpufreq_remove_dev_prepare(struct device *dev,
					struct subsys_interface *sif,
					bool frozen)
{
	unsigned int cpu = dev->id, cpus;
	int new_cpu, ret;
	unsigned long flags;
	struct cpufreq_policy *policy;

	pr_debug("%s: unregistering CPU %u\n", __func__, cpu);

	write_lock_irqsave(&cpufreq_driver_lock, flags);

	policy = per_cpu(cpufreq_cpu_data, cpu);

	/* Save the policy somewhere when doing a light-weight tear-down */
	if (frozen)
		per_cpu(cpufreq_cpu_data_fallback, cpu) = policy;

	write_unlock_irqrestore(&cpufreq_driver_lock, flags);

	if (!policy) {
		pr_debug("%s: No cpu_data found\n", __func__);
		return -EINVAL;
	}

	if (cpufreq_driver->target) {
		ret = __cpufreq_governor(policy, CPUFREQ_GOV_STOP);
		if (ret) {
			pr_err("%s: Failed to stop governor\n", __func__);
			return ret;
		}
	}

#ifdef CONFIG_HOTPLUG_CPU
	if (!cpufreq_driver->setpolicy)
		strncpy(per_cpu(cpufreq_cpu_governor, cpu),
			policy->governor->name, CPUFREQ_NAME_LEN);
#endif

	WARN_ON(lock_policy_rwsem_write(cpu));
	cpus = cpumask_weight(policy->cpus);

	if (cpus > 1)
		cpumask_clear_cpu(cpu, policy->cpus);
	unlock_policy_rwsem_write(cpu);

	if (cpu != policy->cpu && !frozen) {
		sysfs_remove_link(&dev->kobj, "cpufreq");
	} else if (cpus > 1) {

		new_cpu = cpufreq_nominate_new_policy_cpu(policy, cpu, frozen);
		if (new_cpu >= 0) {
			WARN_ON(lock_policy_rwsem_write(cpu));
			update_policy_cpu(policy, new_cpu);
			unlock_policy_rwsem_write(cpu);

			if (!frozen) {
				pr_debug("%s: policy Kobject moved to cpu: %d "
					 "from: %d\n",__func__, new_cpu, cpu);
			}
		}
	}

	return 0;
}

static int __cpufreq_remove_dev_finish(struct device *dev,
				       struct subsys_interface *sif,
				       bool frozen)
{
	unsigned int cpu = dev->id, cpus;
	int ret;
	unsigned long flags;
	struct cpufreq_policy *policy;
	struct kobject *kobj;
	struct completion *cmp;

	read_lock_irqsave(&cpufreq_driver_lock, flags);
	policy = per_cpu(cpufreq_cpu_data, cpu);
	read_unlock_irqrestore(&cpufreq_driver_lock, flags);

	if (!policy) {
		pr_debug("%s: No cpu_data found\n", __func__);
		return -EINVAL;
	}

	lock_policy_rwsem_read(cpu);
	cpus = cpumask_weight(policy->cpus);
	unlock_policy_rwsem_read(cpu);

	/* If cpu is last user of policy, free policy */
	if (cpus == 1) {
		if (cpufreq_driver->target) {
			ret = __cpufreq_governor(policy,
					CPUFREQ_GOV_POLICY_EXIT);
			if (ret) {
				pr_err("%s: Failed to exit governor\n",
						__func__);
				return ret;
			}
		}

		if (!frozen) {
			lock_policy_rwsem_read(cpu);
			kobj = &policy->kobj;
			cmp = &policy->kobj_unregister;
			unlock_policy_rwsem_read(cpu);
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

		/*
		 * Perform the ->exit() even during light-weight tear-down,
		 * since this is a core component, and is essential for the
		 * subsequent light-weight ->init() to succeed.
		 */
		if (cpufreq_driver->exit)
			cpufreq_driver->exit(policy);

		/* Remove policy from list of active policies */
		write_lock_irqsave(&cpufreq_driver_lock, flags);
		list_del(&policy->policy_list);
		write_unlock_irqrestore(&cpufreq_driver_lock, flags);

		if (!frozen)
			cpufreq_policy_free(policy);
	} else {
		if (cpufreq_driver->target) {
			if ((ret = __cpufreq_governor(policy, CPUFREQ_GOV_START)) ||
					(ret = __cpufreq_governor(policy, CPUFREQ_GOV_LIMITS))) {
				pr_err("%s: Failed to start governor\n",
						__func__);
				return ret;
			}
		}
	}

	per_cpu(cpufreq_cpu_data, cpu) = NULL;
	return 0;
}

/**
 * __cpufreq_remove_dev - remove a CPU device
 *
 * Removes the cpufreq interface for a CPU device.
 * Caller should already have policy_rwsem in write mode for this CPU.
 * This routine frees the rwsem before returning.
 */
static inline int __cpufreq_remove_dev(struct device *dev,
				       struct subsys_interface *sif,
				       bool frozen)
{
	int ret;

	ret = __cpufreq_remove_dev_prepare(dev, sif, frozen);

	if (!ret)
		ret = __cpufreq_remove_dev_finish(dev, sif, frozen);

	return ret;
}

static int cpufreq_remove_dev(struct device *dev, struct subsys_interface *sif)
{
	unsigned int cpu = dev->id;
	int retval;

	if (cpu_is_offline(cpu))
		return 0;

	retval = __cpufreq_remove_dev(dev, sif, false);
	return retval;
}

static void handle_update(struct work_struct *work)
{
	struct cpufreq_policy *policy =
		container_of(work, struct cpufreq_policy, update);
	unsigned int cpu = policy->cpu;
	pr_debug("handle_update for cpu %u called\n", cpu);
	cpufreq_update_policy(cpu);
}

/**
 *	cpufreq_out_of_sync - If actual and saved CPU frequency differs, we're
 *	in deep trouble.
 *	@cpu: cpu number
 *	@old_freq: CPU frequency the kernel thinks the CPU runs at
 *	@new_freq: CPU frequency the CPU actually runs at
 *
 *	We adjust to current frequency first, and need to clean up later.
 *	So either call to cpufreq_update_policy() or schedule handle_update()).
 */
static void cpufreq_out_of_sync(unsigned int cpu, unsigned int old_freq,
				unsigned int new_freq)
{
	struct cpufreq_policy *policy;
	struct cpufreq_freqs freqs;
	unsigned long flags;

	pr_debug("Warning: CPU frequency out of sync: cpufreq and timing "
	       "core thinks of %u, is %u kHz.\n", old_freq, new_freq);

	freqs.old = old_freq;
	freqs.new = new_freq;

	read_lock_irqsave(&cpufreq_driver_lock, flags);
	policy = per_cpu(cpufreq_cpu_data, cpu);
	read_unlock_irqrestore(&cpufreq_driver_lock, flags);

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);
	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);
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

	if (cpufreq_driver && cpufreq_driver->setpolicy && cpufreq_driver->get)
		return cpufreq_driver->get(cpu);

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

static unsigned int __cpufreq_get(unsigned int cpu)
{
	struct cpufreq_policy *policy = per_cpu(cpufreq_cpu_data, cpu);
	unsigned int ret_freq = 0;

	if (!cpufreq_driver->get)
		return ret_freq;

	ret_freq = cpufreq_driver->get(cpu);

	if (ret_freq && policy->cur &&
		!(cpufreq_driver->flags & CPUFREQ_CONST_LOOPS)) {
		/* verify no discrepancy between actual and
					saved value exists */
		if (unlikely(ret_freq != policy->cur)) {
			cpufreq_out_of_sync(cpu, policy->cur, ret_freq);
			schedule_work(&policy->update);
		}
	}

	return ret_freq;
}

/**
 * cpufreq_get - get the current CPU frequency (in kHz)
 * @cpu: CPU number
 *
 * Get the CPU current (static) CPU frequency
 */
unsigned int cpufreq_get(unsigned int cpu)
{
	unsigned int ret_freq = 0;

	if (!down_read_trylock(&cpufreq_rwsem))
		return 0;

	if (unlikely(lock_policy_rwsem_read(cpu)))
		goto out_policy;

	ret_freq = __cpufreq_get(cpu);

	unlock_policy_rwsem_read(cpu);

out_policy:
	up_read(&cpufreq_rwsem);

	return ret_freq;
}
EXPORT_SYMBOL(cpufreq_get);

static struct subsys_interface cpufreq_interface = {
	.name		= "cpufreq",
	.subsys		= &cpu_subsys,
	.add_dev	= cpufreq_add_dev,
	.remove_dev	= cpufreq_remove_dev,
};

/**
 * cpufreq_bp_suspend - Prepare the boot CPU for system suspend.
 *
 * This function is only executed for the boot processor.  The other CPUs
 * have been put offline by means of CPU hotplug.
 */
static int cpufreq_bp_suspend(void)
{
	int ret = 0;

	int cpu = smp_processor_id();
	struct cpufreq_policy *policy;

	pr_debug("suspending cpu %u\n", cpu);

	/* If there's no policy for the boot CPU, we have nothing to do. */
	policy = cpufreq_cpu_get(cpu);
	if (!policy)
		return 0;

	if (cpufreq_driver->suspend) {
		ret = cpufreq_driver->suspend(policy);
		if (ret)
			printk(KERN_ERR "cpufreq: suspend failed in ->suspend "
					"step on CPU %u\n", policy->cpu);
	}

	cpufreq_cpu_put(policy);
	return ret;
}

/**
 * cpufreq_bp_resume - Restore proper frequency handling of the boot CPU.
 *
 *	1.) resume CPUfreq hardware support (cpufreq_driver->resume())
 *	2.) schedule call cpufreq_update_policy() ASAP as interrupts are
 *	    restored. It will verify that the current freq is in sync with
 *	    what we believe it to be. This is a bit later than when it
 *	    should be, but nonethteless it's better than calling
 *	    cpufreq_driver->get() here which might re-enable interrupts...
 *
 * This function is only executed for the boot CPU.  The other CPUs have not
 * been turned on yet.
 */
static void cpufreq_bp_resume(void)
{
	int ret = 0;

	int cpu = smp_processor_id();
	struct cpufreq_policy *policy;

	pr_debug("resuming cpu %u\n", cpu);

	/* If there's no policy for the boot CPU, we have nothing to do. */
	policy = cpufreq_cpu_get(cpu);
	if (!policy)
		return;

	if (cpufreq_driver->resume) {
		ret = cpufreq_driver->resume(policy);
		if (ret) {
			printk(KERN_ERR "cpufreq: resume failed in ->resume "
					"step on CPU %u\n", policy->cpu);
			goto fail;
		}
	}

	schedule_work(&policy->update);

fail:
	cpufreq_cpu_put(policy);
}

static struct syscore_ops cpufreq_syscore_ops = {
	.suspend	= cpufreq_bp_suspend,
	.resume		= cpufreq_bp_resume,
};

/**
 *	cpufreq_get_current_driver - return current driver's name
 *
 *	Return the name string of the currently loaded cpufreq driver
 *	or NULL, if none.
 */
const char *cpufreq_get_current_driver(void)
{
	if (cpufreq_driver)
		return cpufreq_driver->name;

	return NULL;
}
EXPORT_SYMBOL_GPL(cpufreq_get_current_driver);

/*********************************************************************
 *                     NOTIFIER LISTS INTERFACE                      *
 *********************************************************************/

/**
 *	cpufreq_register_notifier - register a driver with cpufreq
 *	@nb: notifier function to register
 *      @list: CPUFREQ_TRANSITION_NOTIFIER or CPUFREQ_POLICY_NOTIFIER
 *
 *	Add a driver to one of two lists: either a list of drivers that
 *      are notified about clock rate changes (once before and once after
 *      the transition), or a list of drivers that are notified about
 *      changes in cpufreq policy.
 *
 *	This function may sleep, and has the same return conditions as
 *	blocking_notifier_chain_register.
 */
int cpufreq_register_notifier(struct notifier_block *nb, unsigned int list)
{
	int ret;

	if (cpufreq_disabled())
		return -EINVAL;

	WARN_ON(!init_cpufreq_transition_notifier_list_called);

	switch (list) {
	case CPUFREQ_TRANSITION_NOTIFIER:
		ret = srcu_notifier_chain_register(
				&cpufreq_transition_notifier_list, nb);
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
 *	cpufreq_unregister_notifier - unregister a driver with cpufreq
 *	@nb: notifier block to be unregistered
 *	@list: CPUFREQ_TRANSITION_NOTIFIER or CPUFREQ_POLICY_NOTIFIER
 *
 *	Remove a driver from the CPU frequency notifier list.
 *
 *	This function may sleep, and has the same return conditions as
 *	blocking_notifier_chain_unregister.
 */
int cpufreq_unregister_notifier(struct notifier_block *nb, unsigned int list)
{
	int ret;

	if (cpufreq_disabled())
		return -EINVAL;

	switch (list) {
	case CPUFREQ_TRANSITION_NOTIFIER:
		ret = srcu_notifier_chain_unregister(
				&cpufreq_transition_notifier_list, nb);
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

int __cpufreq_driver_target(struct cpufreq_policy *policy,
			    unsigned int target_freq,
			    unsigned int relation)
{
	int retval = -EINVAL;
	unsigned int old_target_freq = target_freq;

	if (cpufreq_disabled())
		return -ENODEV;
	if (policy->transition_ongoing)
		return -EBUSY;

	/* Make sure that target_freq is within supported range */
	if (target_freq > policy->max)
		target_freq = policy->max;
	if (target_freq < policy->min)
		target_freq = policy->min;

	pr_debug("target for CPU %u: %u kHz, relation %u, requested %u kHz\n",
			policy->cpu, target_freq, relation, old_target_freq);

	if (target_freq == policy->cur)
		return 0;

	if (cpufreq_driver->target)
		retval = cpufreq_driver->target(policy, target_freq, relation);

	return retval;
}
EXPORT_SYMBOL_GPL(__cpufreq_driver_target);

int cpufreq_driver_target(struct cpufreq_policy *policy,
			  unsigned int target_freq,
			  unsigned int relation)
{
	int ret = -EINVAL;

	if (unlikely(lock_policy_rwsem_write(policy->cpu)))
		goto fail;

	ret = __cpufreq_driver_target(policy, target_freq, relation);

	unlock_policy_rwsem_write(policy->cpu);

fail:
	return ret;
}
EXPORT_SYMBOL_GPL(cpufreq_driver_target);

/*
 * when "event" is CPUFREQ_GOV_LIMITS
 */

static int __cpufreq_governor(struct cpufreq_policy *policy,
					unsigned int event)
{
	int ret;

	/* Only must be defined when default governor is known to have latency
	   restrictions, like e.g. conservative or ondemand.
	   That this is the case is already ensured in Kconfig
	*/
#ifdef CONFIG_CPU_FREQ_GOV_PERFORMANCE
	struct cpufreq_governor *gov = &cpufreq_gov_performance;
#else
	struct cpufreq_governor *gov = NULL;
#endif

	if (policy->governor->max_transition_latency &&
	    policy->cpuinfo.transition_latency >
	    policy->governor->max_transition_latency) {
		if (!gov)
			return -EINVAL;
		else {
			printk(KERN_WARNING "%s governor failed, too long"
			       " transition latency of HW, fallback"
			       " to %s governor\n",
			       policy->governor->name,
			       gov->name);
			policy->governor = gov;
		}
	}

	if (event == CPUFREQ_GOV_POLICY_INIT)
		if (!try_module_get(policy->governor->owner))
			return -EINVAL;

	pr_debug("__cpufreq_governor for CPU %u, event %u\n",
						policy->cpu, event);

	mutex_lock(&cpufreq_governor_lock);
	if (policy->governor_busy
	    || (policy->governor_enabled && event == CPUFREQ_GOV_START)
	    || (!policy->governor_enabled
	    && (event == CPUFREQ_GOV_LIMITS || event == CPUFREQ_GOV_STOP))) {
		mutex_unlock(&cpufreq_governor_lock);
		return -EBUSY;
	}

	policy->governor_busy = true;
	if (event == CPUFREQ_GOV_STOP)
		policy->governor_enabled = false;
	else if (event == CPUFREQ_GOV_START)
		policy->governor_enabled = true;

	mutex_unlock(&cpufreq_governor_lock);

	ret = policy->governor->governor(policy, event);

	if (!ret) {
		if (event == CPUFREQ_GOV_POLICY_INIT)
			policy->governor->initialized++;
		else if (event == CPUFREQ_GOV_POLICY_EXIT)
			policy->governor->initialized--;
	} else {
		/* Restore original values */
		mutex_lock(&cpufreq_governor_lock);
		if (event == CPUFREQ_GOV_STOP)
			policy->governor_enabled = true;
		else if (event == CPUFREQ_GOV_START)
			policy->governor_enabled = false;
		mutex_unlock(&cpufreq_governor_lock);
	}

	if (((event == CPUFREQ_GOV_POLICY_INIT) && ret) ||
			((event == CPUFREQ_GOV_POLICY_EXIT) && !ret))
		module_put(policy->governor->owner);

	mutex_lock(&cpufreq_governor_lock);
	policy->governor_busy = false;
	mutex_unlock(&cpufreq_governor_lock);
	return ret;
}

int cpufreq_register_governor(struct cpufreq_governor *governor)
{
	int err;

	if (!governor)
		return -EINVAL;

	if (cpufreq_disabled())
		return -ENODEV;

	mutex_lock(&cpufreq_governor_mutex);

	governor->initialized = 0;
	err = -EBUSY;
	if (__find_governor(governor->name) == NULL) {
		err = 0;
		list_add(&governor->governor_list, &cpufreq_governor_list);
	}

	mutex_unlock(&cpufreq_governor_mutex);
	return err;
}
EXPORT_SYMBOL_GPL(cpufreq_register_governor);

void cpufreq_unregister_governor(struct cpufreq_governor *governor)
{
#ifdef CONFIG_HOTPLUG_CPU
	int cpu;
#endif

	if (!governor)
		return;

	if (cpufreq_disabled())
		return;

#ifdef CONFIG_HOTPLUG_CPU
	for_each_present_cpu(cpu) {
		if (cpu_online(cpu))
			continue;
		if (!strcmp(per_cpu(cpufreq_cpu_governor, cpu), governor->name))
			strcpy(per_cpu(cpufreq_cpu_governor, cpu), "\0");
	}
#endif

	mutex_lock(&cpufreq_governor_mutex);
	list_del(&governor->governor_list);
	mutex_unlock(&cpufreq_governor_mutex);
	return;
}
EXPORT_SYMBOL_GPL(cpufreq_unregister_governor);


/*********************************************************************
 *                          POLICY INTERFACE                         *
 *********************************************************************/

/**
 * cpufreq_get_policy - get the current cpufreq_policy
 * @policy: struct cpufreq_policy into which the current cpufreq_policy
 *	is written
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

/*
 * data   : current policy.
 * policy : policy to be set.
 */
static int __cpufreq_set_policy(struct cpufreq_policy *policy,
				struct cpufreq_policy *new_policy)
{
	int ret = 0, failed = 1;

	pr_debug("setting new policy for CPU %u: %u - %u kHz\n", new_policy->cpu,
		new_policy->min, new_policy->max);

	memcpy(&new_policy->cpuinfo, &policy->cpuinfo, sizeof(policy->cpuinfo));

	if (new_policy->min > policy->max || new_policy->max < policy->min) {
		ret = -EINVAL;
		goto error_out;
	}

	/* verify the cpu speed can be set within this limit */
	ret = cpufreq_driver->verify(new_policy);
	if (ret)
		goto error_out;

	/* adjust if necessary - all reasons */
	blocking_notifier_call_chain(&cpufreq_policy_notifier_list,
			CPUFREQ_ADJUST, new_policy);

	/* adjust if necessary - hardware incompatibility*/
	blocking_notifier_call_chain(&cpufreq_policy_notifier_list,
			CPUFREQ_INCOMPATIBLE, new_policy);

	/*
	 * verify the cpu speed can be set within this limit, which might be
	 * different to the first one
	 */
	ret = cpufreq_driver->verify(new_policy);
	if (ret)
		goto error_out;

	/* notification of the new policy */
	blocking_notifier_call_chain(&cpufreq_policy_notifier_list,
			CPUFREQ_NOTIFY, new_policy);

	policy->min = new_policy->min;
	policy->max = new_policy->max;

	pr_debug("new min and max freqs are %u - %u kHz\n",
					policy->min, policy->max);

	if (cpufreq_driver->setpolicy) {
		policy->policy = new_policy->policy;
		pr_debug("setting range\n");
		ret = cpufreq_driver->setpolicy(new_policy);
	} else {
		if (new_policy->governor != policy->governor) {
			/* save old, working values */
			struct cpufreq_governor *old_gov = policy->governor;

			pr_debug("governor switch\n");

			/* end old governor */
			if (policy->governor) {
				__cpufreq_governor(policy, CPUFREQ_GOV_STOP);
				unlock_policy_rwsem_write(new_policy->cpu);
				__cpufreq_governor(policy,
						CPUFREQ_GOV_POLICY_EXIT);
				lock_policy_rwsem_write(new_policy->cpu);
			}

			/* start new governor */
			policy->governor = new_policy->governor;
			if (!__cpufreq_governor(policy, CPUFREQ_GOV_POLICY_INIT)) {
				if (!__cpufreq_governor(policy, CPUFREQ_GOV_START)) {
					failed = 0;
				} else {
					unlock_policy_rwsem_write(new_policy->cpu);
					__cpufreq_governor(policy,
							CPUFREQ_GOV_POLICY_EXIT);
					lock_policy_rwsem_write(new_policy->cpu);
				}
			}

			if (failed) {
				/* new governor failed, so re-start old one */
				pr_debug("starting governor %s failed\n",
							policy->governor->name);
				if (old_gov) {
					policy->governor = old_gov;
					__cpufreq_governor(policy,
							CPUFREQ_GOV_POLICY_INIT);
					__cpufreq_governor(policy,
							   CPUFREQ_GOV_START);
				}
				ret = -EINVAL;
				goto error_out;
			}
			/* might be a policy change, too, so fall through */
		}
		pr_debug("governor: change or update limits\n");
		ret = __cpufreq_governor(policy, CPUFREQ_GOV_LIMITS);
	}

error_out:
	return ret;
}

/**
 *	cpufreq_update_policy - re-evaluate an existing cpufreq policy
 *	@cpu: CPU which shall be re-evaluated
 *
 *	Useful for policy notifiers which have different necessities
 *	at different times.
 */
int cpufreq_update_policy(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);
	struct cpufreq_policy new_policy;
	int ret;

	if (!policy) {
		ret = -ENODEV;
		goto no_policy;
	}

	if (unlikely(lock_policy_rwsem_write(cpu))) {
		ret = -EINVAL;
		goto fail;
	}

	pr_debug("updating policy for CPU %u\n", cpu);
	memcpy(&new_policy, policy, sizeof(*policy));
	new_policy.min = policy->user_policy.min;
	new_policy.max = policy->user_policy.max;
	new_policy.policy = policy->user_policy.policy;
	new_policy.governor = policy->user_policy.governor;

	/*
	 * BIOS might change freq behind our back
	 * -> ask driver for current freq and notify governors about a change
	 */
	if (cpufreq_driver->get) {
		new_policy.cur = cpufreq_driver->get(cpu);
		if (!policy->cur) {
			pr_debug("Driver did not initialize current freq");
			policy->cur = new_policy.cur;
		} else {
			if (policy->cur != new_policy.cur && cpufreq_driver->target)
				cpufreq_out_of_sync(cpu, policy->cur,
								new_policy.cur);
		}
	}

	ret = __cpufreq_set_policy(policy, &new_policy);

	unlock_policy_rwsem_write(cpu);

fail:
	cpufreq_cpu_put(policy);
no_policy:
	return ret;
}
EXPORT_SYMBOL(cpufreq_update_policy);

static int cpufreq_cpu_callback(struct notifier_block *nfb,
					unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	struct device *dev;
	bool frozen = false;

	dev = get_cpu_device(cpu);
	if (dev) {

		if (action & CPU_TASKS_FROZEN)
			frozen = true;

		switch (action & ~CPU_TASKS_FROZEN) {
		case CPU_ONLINE:
			__cpufreq_add_dev(dev, NULL, frozen);
			cpufreq_update_policy(cpu);
			break;

		case CPU_DOWN_PREPARE:
			__cpufreq_remove_dev_prepare(dev, NULL, frozen);
			break;

		case CPU_POST_DEAD:
			__cpufreq_remove_dev_finish(dev, NULL, frozen);
			break;

		case CPU_DOWN_FAILED:
			__cpufreq_add_dev(dev, NULL, frozen);
			break;
		}
	}
	return NOTIFY_OK;
}

static struct notifier_block __refdata cpufreq_cpu_notifier = {
	.notifier_call = cpufreq_cpu_callback,
};

/*********************************************************************
 *               REGISTER / UNREGISTER CPUFREQ DRIVER                *
 *********************************************************************/

/**
 * cpufreq_register_driver - register a CPU Frequency driver
 * @driver_data: A struct cpufreq_driver containing the values#
 * submitted by the CPU Frequency driver.
 *
 * Registers a CPU Frequency driver to this core code. This code
 * returns zero on success, -EBUSY when another driver got here first
 * (and isn't unregistered in the meantime).
 *
 */
int cpufreq_register_driver(struct cpufreq_driver *driver_data)
{
	unsigned long flags;
	int ret;

	if (cpufreq_disabled())
		return -ENODEV;

	if (!driver_data || !driver_data->verify || !driver_data->init ||
	    ((!driver_data->setpolicy) && (!driver_data->target)))
		return -EINVAL;

	pr_debug("trying to register driver %s\n", driver_data->name);

	if (driver_data->setpolicy)
		driver_data->flags |= CPUFREQ_CONST_LOOPS;

	write_lock_irqsave(&cpufreq_driver_lock, flags);
	if (cpufreq_driver) {
		write_unlock_irqrestore(&cpufreq_driver_lock, flags);
		return -EBUSY;
	}
	cpufreq_driver = driver_data;
	write_unlock_irqrestore(&cpufreq_driver_lock, flags);

	ret = subsys_interface_register(&cpufreq_interface);
	if (ret)
		goto err_null_driver;

	if (!(cpufreq_driver->flags & CPUFREQ_STICKY)) {
		int i;
		ret = -ENODEV;

		/* check for at least one working CPU */
		for (i = 0; i < nr_cpu_ids; i++)
			if (cpu_possible(i) && per_cpu(cpufreq_cpu_data, i)) {
				ret = 0;
				break;
			}

		/* if all ->init() calls failed, unregister */
		if (ret) {
			pr_debug("no CPU initialized for driver %s\n",
							driver_data->name);
			goto err_if_unreg;
		}
	}

	register_hotcpu_notifier(&cpufreq_cpu_notifier);
	pr_debug("driver %s up and running\n", driver_data->name);

	return 0;
err_if_unreg:
	subsys_interface_unregister(&cpufreq_interface);
err_null_driver:
	write_lock_irqsave(&cpufreq_driver_lock, flags);
	cpufreq_driver = NULL;
	write_unlock_irqrestore(&cpufreq_driver_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(cpufreq_register_driver);

/**
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

	subsys_interface_unregister(&cpufreq_interface);
	unregister_hotcpu_notifier(&cpufreq_cpu_notifier);

	down_write(&cpufreq_rwsem);
	write_lock_irqsave(&cpufreq_driver_lock, flags);

	cpufreq_driver = NULL;

	write_unlock_irqrestore(&cpufreq_driver_lock, flags);
	up_write(&cpufreq_rwsem);

	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_unregister_driver);

static int __init cpufreq_core_init(void)
{
	int cpu;

	if (cpufreq_disabled())
		return -ENODEV;

	for_each_possible_cpu(cpu)
		init_rwsem(&per_cpu(cpu_policy_rwsem, cpu));

	cpufreq_global_kobject = kobject_create();
	BUG_ON(!cpufreq_global_kobject);
	register_syscore_ops(&cpufreq_syscore_ops);

	return 0;
}
core_initcall(cpufreq_core_init);
