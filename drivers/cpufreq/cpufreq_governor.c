/*
 * drivers/cpufreq/cpufreq_governor.c
 *
 * CPUFREQ governors common code
 *
 * Copyright	(C) 2001 Russell King
 *		(C) 2003 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>.
 *		(C) 2003 Jun Nakajima <jun.nakajima@intel.com>
 *		(C) 2009 Alexander Clouter <alex@digriz.org.uk>
 *		(c) 2012 Viresh Kumar <viresh.kumar@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/export.h>
#include <linux/kernel_stat.h>
#include <linux/slab.h>

#include "cpufreq_governor.h"

#define CPUFREQ_DBS_MIN_SAMPLING_INTERVAL	(2 * TICK_NSEC / NSEC_PER_USEC)

static DEFINE_PER_CPU(struct cpu_dbs_info, cpu_dbs);

static DEFINE_MUTEX(gov_dbs_data_mutex);

/* Common sysfs tunables */
/**
 * store_sampling_rate - update sampling rate effective immediately if needed.
 *
 * If new rate is smaller than the old, simply updating
 * dbs.sampling_rate might not be appropriate. For example, if the
 * original sampling_rate was 1 second and the requested new sampling rate is 10
 * ms because the user needs immediate reaction from ondemand governor, but not
 * sure if higher frequency will be required or not, then, the governor may
 * change the sampling rate too late; up to 1 second later. Thus, if we are
 * reducing the sampling rate, we need to make the new value effective
 * immediately.
 *
 * This must be called with dbs_data->mutex held, otherwise traversing
 * policy_dbs_list isn't safe.
 */
ssize_t store_sampling_rate(struct gov_attr_set *attr_set, const char *buf,
			    size_t count)
{
	struct dbs_data *dbs_data = to_dbs_data(attr_set);
	struct policy_dbs_info *policy_dbs;
	unsigned int sampling_interval;
	int ret;

	ret = sscanf(buf, "%u", &sampling_interval);
	if (ret != 1 || sampling_interval < CPUFREQ_DBS_MIN_SAMPLING_INTERVAL)
		return -EINVAL;

	dbs_data->sampling_rate = sampling_interval;

	/*
	 * We are operating under dbs_data->mutex and so the list and its
	 * entries can't be freed concurrently.
	 */
	list_for_each_entry(policy_dbs, &attr_set->policy_list, list) {
		mutex_lock(&policy_dbs->update_mutex);
		/*
		 * On 32-bit architectures this may race with the
		 * sample_delay_ns read in dbs_update_util_handler(), but that
		 * really doesn't matter.  If the read returns a value that's
		 * too big, the sample will be skipped, but the next invocation
		 * of dbs_update_util_handler() (when the update has been
		 * completed) will take a sample.
		 *
		 * If this runs in parallel with dbs_work_handler(), we may end
		 * up overwriting the sample_delay_ns value that it has just
		 * written, but it will be corrected next time a sample is
		 * taken, so it shouldn't be significant.
		 */
		gov_update_sample_delay(policy_dbs, 0);
		mutex_unlock(&policy_dbs->update_mutex);
	}

	return count;
}
EXPORT_SYMBOL_GPL(store_sampling_rate);

/**
 * gov_update_cpu_data - Update CPU load data.
 * @dbs_data: Top-level governor data pointer.
 *
 * Update CPU load data for all CPUs in the domain governed by @dbs_data
 * (that may be a single policy or a bunch of them if governor tunables are
 * system-wide).
 *
 * Call under the @dbs_data mutex.
 */
void gov_update_cpu_data(struct dbs_data *dbs_data)
{
	struct policy_dbs_info *policy_dbs;

	list_for_each_entry(policy_dbs, &dbs_data->attr_set.policy_list, list) {
		unsigned int j;

		for_each_cpu(j, policy_dbs->policy->cpus) {
			struct cpu_dbs_info *j_cdbs = &per_cpu(cpu_dbs, j);

			j_cdbs->prev_cpu_idle = get_cpu_idle_time(j, &j_cdbs->prev_update_time,
								  dbs_data->io_is_busy);
			if (dbs_data->ignore_nice_load)
				j_cdbs->prev_cpu_nice = kcpustat_cpu(j).cpustat[CPUTIME_NICE];
		}
	}
}
EXPORT_SYMBOL_GPL(gov_update_cpu_data);

unsigned int dbs_update(struct cpufreq_policy *policy)
{
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	unsigned int ignore_nice = dbs_data->ignore_nice_load;
	unsigned int max_load = 0, idle_periods = UINT_MAX;
	unsigned int sampling_rate, io_busy, j;

	/*
	 * Sometimes governors may use an additional multiplier to increase
	 * sample delays temporarily.  Apply that multiplier to sampling_rate
	 * so as to keep the wake-up-from-idle detection logic a bit
	 * conservative.
	 */
	sampling_rate = dbs_data->sampling_rate * policy_dbs->rate_mult;
	/*
	 * For the purpose of ondemand, waiting for disk IO is an indication
	 * that you're performance critical, and not that the system is actually
	 * idle, so do not add the iowait time to the CPU idle time then.
	 */
	io_busy = dbs_data->io_is_busy;

	/* Get Absolute Load */
	for_each_cpu(j, policy->cpus) {
		struct cpu_dbs_info *j_cdbs = &per_cpu(cpu_dbs, j);
		u64 update_time, cur_idle_time;
		unsigned int idle_time, time_elapsed;
		unsigned int load;

		cur_idle_time = get_cpu_idle_time(j, &update_time, io_busy);

		time_elapsed = update_time - j_cdbs->prev_update_time;
		j_cdbs->prev_update_time = update_time;

		idle_time = cur_idle_time - j_cdbs->prev_cpu_idle;
		j_cdbs->prev_cpu_idle = cur_idle_time;

		if (ignore_nice) {
			u64 cur_nice = kcpustat_cpu(j).cpustat[CPUTIME_NICE];

			idle_time += div_u64(cur_nice - j_cdbs->prev_cpu_nice, NSEC_PER_USEC);
			j_cdbs->prev_cpu_nice = cur_nice;
		}

		if (unlikely(!time_elapsed)) {
			/*
			 * That can only happen when this function is called
			 * twice in a row with a very short interval between the
			 * calls, so the previous load value can be used then.
			 */
			load = j_cdbs->prev_load;
		} else if (unlikely((int)idle_time > 2 * sampling_rate &&
				    j_cdbs->prev_load)) {
			/*
			 * If the CPU had gone completely idle and a task has
			 * just woken up on this CPU now, it would be unfair to
			 * calculate 'load' the usual way for this elapsed
			 * time-window, because it would show near-zero load,
			 * irrespective of how CPU intensive that task actually
			 * was. This is undesirable for latency-sensitive bursty
			 * workloads.
			 *
			 * To avoid this, reuse the 'load' from the previous
			 * time-window and give this task a chance to start with
			 * a reasonably high CPU frequency. However, that
			 * shouldn't be over-done, lest we get stuck at a high
			 * load (high frequency) for too long, even when the
			 * current system load has actually dropped down, so
			 * clear prev_load to guarantee that the load will be
			 * computed again next time.
			 *
			 * Detecting this situation is easy: an unusually large
			 * 'idle_time' (as compared to the sampling rate)
			 * indicates this scenario.
			 */
			load = j_cdbs->prev_load;
			j_cdbs->prev_load = 0;
		} else {
			if (time_elapsed >= idle_time) {
				load = 100 * (time_elapsed - idle_time) / time_elapsed;
			} else {
				/*
				 * That can happen if idle_time is returned by
				 * get_cpu_idle_time_jiffy().  In that case
				 * idle_time is roughly equal to the difference
				 * between time_elapsed and "busy time" obtained
				 * from CPU statistics.  Then, the "busy time"
				 * can end up being greater than time_elapsed
				 * (for example, if jiffies_64 and the CPU
				 * statistics are updated by different CPUs),
				 * so idle_time may in fact be negative.  That
				 * means, though, that the CPU was busy all
				 * the time (on the rough average) during the
				 * last sampling interval and 100 can be
				 * returned as the load.
				 */
				load = (int)idle_time < 0 ? 100 : 0;
			}
			j_cdbs->prev_load = load;
		}

		if (unlikely((int)idle_time > 2 * sampling_rate)) {
			unsigned int periods = idle_time / sampling_rate;

			if (periods < idle_periods)
				idle_periods = periods;
		}

		if (load > max_load)
			max_load = load;
	}

	policy_dbs->idle_periods = idle_periods;

	return max_load;
}
EXPORT_SYMBOL_GPL(dbs_update);

static void dbs_work_handler(struct work_struct *work)
{
	struct policy_dbs_info *policy_dbs;
	struct cpufreq_policy *policy;
	struct dbs_governor *gov;

	policy_dbs = container_of(work, struct policy_dbs_info, work);
	policy = policy_dbs->policy;
	gov = dbs_governor_of(policy);

	/*
	 * Make sure cpufreq_governor_limits() isn't evaluating load or the
	 * ondemand governor isn't updating the sampling rate in parallel.
	 */
	mutex_lock(&policy_dbs->update_mutex);
	gov_update_sample_delay(policy_dbs, gov->gov_dbs_update(policy));
	mutex_unlock(&policy_dbs->update_mutex);

	/* Allow the utilization update handler to queue up more work. */
	atomic_set(&policy_dbs->work_count, 0);
	/*
	 * If the update below is reordered with respect to the sample delay
	 * modification, the utilization update handler may end up using a stale
	 * sample delay value.
	 */
	smp_wmb();
	policy_dbs->work_in_progress = false;
}

static void dbs_irq_work(struct irq_work *irq_work)
{
	struct policy_dbs_info *policy_dbs;

	policy_dbs = container_of(irq_work, struct policy_dbs_info, irq_work);
	schedule_work_on(smp_processor_id(), &policy_dbs->work);
}

static void dbs_update_util_handler(struct update_util_data *data, u64 time,
				    unsigned int flags)
{
	struct cpu_dbs_info *cdbs = container_of(data, struct cpu_dbs_info, update_util);
	struct policy_dbs_info *policy_dbs = cdbs->policy_dbs;
	u64 delta_ns, lst;

	if (!cpufreq_this_cpu_can_update(policy_dbs->policy))
		return;

	/*
	 * The work may not be allowed to be queued up right now.
	 * Possible reasons:
	 * - Work has already been queued up or is in progress.
	 * - It is too early (too little time from the previous sample).
	 */
	if (policy_dbs->work_in_progress)
		return;

	/*
	 * If the reads below are reordered before the check above, the value
	 * of sample_delay_ns used in the computation may be stale.
	 */
	smp_rmb();
	lst = READ_ONCE(policy_dbs->last_sample_time);
	delta_ns = time - lst;
	if ((s64)delta_ns < policy_dbs->sample_delay_ns)
		return;

	/*
	 * If the policy is not shared, the irq_work may be queued up right away
	 * at this point.  Otherwise, we need to ensure that only one of the
	 * CPUs sharing the policy will do that.
	 */
	if (policy_dbs->is_shared) {
		if (!atomic_add_unless(&policy_dbs->work_count, 1, 1))
			return;

		/*
		 * If another CPU updated last_sample_time in the meantime, we
		 * shouldn't be here, so clear the work counter and bail out.
		 */
		if (unlikely(lst != READ_ONCE(policy_dbs->last_sample_time))) {
			atomic_set(&policy_dbs->work_count, 0);
			return;
		}
	}

	policy_dbs->last_sample_time = time;
	policy_dbs->work_in_progress = true;
	irq_work_queue(&policy_dbs->irq_work);
}

static void gov_set_update_util(struct policy_dbs_info *policy_dbs,
				unsigned int delay_us)
{
	struct cpufreq_policy *policy = policy_dbs->policy;
	int cpu;

	gov_update_sample_delay(policy_dbs, delay_us);
	policy_dbs->last_sample_time = 0;

	for_each_cpu(cpu, policy->cpus) {
		struct cpu_dbs_info *cdbs = &per_cpu(cpu_dbs, cpu);

		cpufreq_add_update_util_hook(cpu, &cdbs->update_util,
					     dbs_update_util_handler);
	}
}

static inline void gov_clear_update_util(struct cpufreq_policy *policy)
{
	int i;

	for_each_cpu(i, policy->cpus)
		cpufreq_remove_update_util_hook(i);

	synchronize_sched();
}

static struct policy_dbs_info *alloc_policy_dbs_info(struct cpufreq_policy *policy,
						     struct dbs_governor *gov)
{
	struct policy_dbs_info *policy_dbs;
	int j;

	/* Allocate memory for per-policy governor data. */
	policy_dbs = gov->alloc();
	if (!policy_dbs)
		return NULL;

	policy_dbs->policy = policy;
	mutex_init(&policy_dbs->update_mutex);
	atomic_set(&policy_dbs->work_count, 0);
	init_irq_work(&policy_dbs->irq_work, dbs_irq_work);
	INIT_WORK(&policy_dbs->work, dbs_work_handler);

	/* Set policy_dbs for all CPUs, online+offline */
	for_each_cpu(j, policy->related_cpus) {
		struct cpu_dbs_info *j_cdbs = &per_cpu(cpu_dbs, j);

		j_cdbs->policy_dbs = policy_dbs;
	}
	return policy_dbs;
}

static void free_policy_dbs_info(struct policy_dbs_info *policy_dbs,
				 struct dbs_governor *gov)
{
	int j;

	mutex_destroy(&policy_dbs->update_mutex);

	for_each_cpu(j, policy_dbs->policy->related_cpus) {
		struct cpu_dbs_info *j_cdbs = &per_cpu(cpu_dbs, j);

		j_cdbs->policy_dbs = NULL;
		j_cdbs->update_util.func = NULL;
	}
	gov->free(policy_dbs);
}

int cpufreq_dbs_governor_init(struct cpufreq_policy *policy)
{
	struct dbs_governor *gov = dbs_governor_of(policy);
	struct dbs_data *dbs_data;
	struct policy_dbs_info *policy_dbs;
	int ret = 0;

	/* State should be equivalent to EXIT */
	if (policy->governor_data)
		return -EBUSY;

	policy_dbs = alloc_policy_dbs_info(policy, gov);
	if (!policy_dbs)
		return -ENOMEM;

	/* Protect gov->gdbs_data against concurrent updates. */
	mutex_lock(&gov_dbs_data_mutex);

	dbs_data = gov->gdbs_data;
	if (dbs_data) {
		if (WARN_ON(have_governor_per_policy())) {
			ret = -EINVAL;
			goto free_policy_dbs_info;
		}
		policy_dbs->dbs_data = dbs_data;
		policy->governor_data = policy_dbs;

		gov_attr_set_get(&dbs_data->attr_set, &policy_dbs->list);
		goto out;
	}

	dbs_data = kzalloc(sizeof(*dbs_data), GFP_KERNEL);
	if (!dbs_data) {
		ret = -ENOMEM;
		goto free_policy_dbs_info;
	}

	gov_attr_set_init(&dbs_data->attr_set, &policy_dbs->list);

	ret = gov->init(dbs_data);
	if (ret)
		goto free_policy_dbs_info;

	/*
	 * The sampling interval should not be less than the transition latency
	 * of the CPU and it also cannot be too small for dbs_update() to work
	 * correctly.
	 */
	dbs_data->sampling_rate = max_t(unsigned int,
					CPUFREQ_DBS_MIN_SAMPLING_INTERVAL,
					cpufreq_policy_transition_delay_us(policy));

	if (!have_governor_per_policy())
		gov->gdbs_data = dbs_data;

	policy_dbs->dbs_data = dbs_data;
	policy->governor_data = policy_dbs;

	gov->kobj_type.sysfs_ops = &governor_sysfs_ops;
	ret = kobject_init_and_add(&dbs_data->attr_set.kobj, &gov->kobj_type,
				   get_governor_parent_kobj(policy),
				   "%s", gov->gov.name);
	if (!ret)
		goto out;

	/* Failure, so roll back. */
	pr_err("initialization failed (dbs_data kobject init error %d)\n", ret);

	policy->governor_data = NULL;

	if (!have_governor_per_policy())
		gov->gdbs_data = NULL;
	gov->exit(dbs_data);
	kfree(dbs_data);

free_policy_dbs_info:
	free_policy_dbs_info(policy_dbs, gov);

out:
	mutex_unlock(&gov_dbs_data_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(cpufreq_dbs_governor_init);

void cpufreq_dbs_governor_exit(struct cpufreq_policy *policy)
{
	struct dbs_governor *gov = dbs_governor_of(policy);
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	unsigned int count;

	/* Protect gov->gdbs_data against concurrent updates. */
	mutex_lock(&gov_dbs_data_mutex);

	count = gov_attr_set_put(&dbs_data->attr_set, &policy_dbs->list);

	policy->governor_data = NULL;

	if (!count) {
		if (!have_governor_per_policy())
			gov->gdbs_data = NULL;

		gov->exit(dbs_data);
		kfree(dbs_data);
	}

	free_policy_dbs_info(policy_dbs, gov);

	mutex_unlock(&gov_dbs_data_mutex);
}
EXPORT_SYMBOL_GPL(cpufreq_dbs_governor_exit);

int cpufreq_dbs_governor_start(struct cpufreq_policy *policy)
{
	struct dbs_governor *gov = dbs_governor_of(policy);
	struct policy_dbs_info *policy_dbs = policy->governor_data;
	struct dbs_data *dbs_data = policy_dbs->dbs_data;
	unsigned int sampling_rate, ignore_nice, j;
	unsigned int io_busy;

	if (!policy->cur)
		return -EINVAL;

	policy_dbs->is_shared = policy_is_shared(policy);
	policy_dbs->rate_mult = 1;

	sampling_rate = dbs_data->sampling_rate;
	ignore_nice = dbs_data->ignore_nice_load;
	io_busy = dbs_data->io_is_busy;

	for_each_cpu(j, policy->cpus) {
		struct cpu_dbs_info *j_cdbs = &per_cpu(cpu_dbs, j);

		j_cdbs->prev_cpu_idle = get_cpu_idle_time(j, &j_cdbs->prev_update_time, io_busy);
		/*
		 * Make the first invocation of dbs_update() compute the load.
		 */
		j_cdbs->prev_load = 0;

		if (ignore_nice)
			j_cdbs->prev_cpu_nice = kcpustat_cpu(j).cpustat[CPUTIME_NICE];
	}

	gov->start(policy);

	gov_set_update_util(policy_dbs, sampling_rate);
	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_dbs_governor_start);

void cpufreq_dbs_governor_stop(struct cpufreq_policy *policy)
{
	struct policy_dbs_info *policy_dbs = policy->governor_data;

	gov_clear_update_util(policy_dbs->policy);
	irq_work_sync(&policy_dbs->irq_work);
	cancel_work_sync(&policy_dbs->work);
	atomic_set(&policy_dbs->work_count, 0);
	policy_dbs->work_in_progress = false;
}
EXPORT_SYMBOL_GPL(cpufreq_dbs_governor_stop);

void cpufreq_dbs_governor_limits(struct cpufreq_policy *policy)
{
	struct policy_dbs_info *policy_dbs;

	/* Protect gov->gdbs_data against cpufreq_dbs_governor_exit() */
	mutex_lock(&gov_dbs_data_mutex);
	policy_dbs = policy->governor_data;
	if (!policy_dbs)
		goto out;

	mutex_lock(&policy_dbs->update_mutex);
	cpufreq_policy_apply_limits(policy);
	gov_update_sample_delay(policy_dbs, 0);
	mutex_unlock(&policy_dbs->update_mutex);

out:
	mutex_unlock(&gov_dbs_data_mutex);
}
EXPORT_SYMBOL_GPL(cpufreq_dbs_governor_limits);
