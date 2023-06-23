// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/sched/idle.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/tick.h>
#include <linux/time64.h>

#if defined(_TRACE_HOOK_PM_DOMAIN_H)
#include <trace/hooks/pm_domain.h>
#endif

#define CREATE_TRACE_POINTS
#include "trace-cluster-lpm.h"
#include "qcom-lpm.h"

LIST_HEAD(cluster_dev_list);

static struct lpm_cluster *to_cluster(struct generic_pm_domain *genpd)
{
	struct lpm_cluster *cluster_gov;

	list_for_each_entry(cluster_gov, &cluster_dev_list, list)
		if (cluster_gov->genpd == genpd)
			return cluster_gov;

	return NULL;
}

/**
 * clusttimer_fn() - Will be executed when cluster prediction timer expires
 * @h:      Cluster prediction timer
 */
static enum hrtimer_restart clusttimer_fn(struct hrtimer *h)
{
	struct lpm_cluster *cluster_gov = container_of(h,
						struct lpm_cluster, histtimer);

	cluster_gov->history_invalid = true;

	return HRTIMER_NORESTART;
}

/**
 * clusttimer_start()  - Programs the hrtimer with given timer value
 * @time_ns:      Value to be program
 */
static void clusttimer_start(struct lpm_cluster *cluster_gov, uint32_t time_us)
{
	struct hrtimer *timer = &cluster_gov->histtimer;
	uint64_t time_ns = time_us * NSEC_PER_USEC;
	ktime_t clust_ktime = ns_to_ktime(time_ns);

	timer->function = clusttimer_fn;
	hrtimer_start(timer, clust_ktime, HRTIMER_MODE_REL_PINNED);
}

/**
 * clusttimer_cancel()  - Cancel the hrtimr after cluster wakeup from sleep
 * @cluster_gov: Targeted cluster's lpm data structure
 */
static void clusttimer_cancel(struct lpm_cluster *cluster_gov)
{
	ktime_t time_rem;

	time_rem = hrtimer_get_remaining(&cluster_gov->histtimer);
	if (ktime_to_us(time_rem) > 0)
		hrtimer_try_to_cancel(&cluster_gov->histtimer);
}

/**
 * cluster_predict() - Predict the cluster's next wakeup.
 * @cluster_gov:  Targeted cluster's lpm data structure
 */
static void cluster_predict(struct lpm_cluster *cluster_gov)
{
	struct generic_pm_domain *genpd = cluster_gov->genpd;
	int i, j, idx = genpd->state_idx;
	int64_t cur_time = ktime_to_us(cluster_gov->now);
	uint64_t avg_residency = 0;

	cluster_gov->pred_wakeup = KTIME_MAX;
	cluster_gov->predicted = false;

	if (prediction_disabled)
		return;

	/*
	 * Samples are marked invalid when woken-up due to timer,
	 * so do not predict.
	 */
	if (cluster_gov->history_invalid) {
		cluster_gov->history_invalid = false;
		cluster_gov->htmr_wkup = true;
		return;
	}

	/*
	 * Cluster wakes up whenever any core of the cluster wakes up.
	 * Since for the last cluster LPM exit, there could be multiple core(s)
	 * LPMs. So, consider only recent history for the cluster.
	 */
	if (cluster_gov->nsamp == MAXSAMPLES) {
		for (i = 0; i < MAXSAMPLES; i++) {
			if ((cur_time - cluster_gov->history[i].entry_time)
					> CLUST_SMPL_INVLD_TIME)
				cluster_gov->nsamp--;
		}
	}

	/* Predict only when all the samples are collected. */
	if (cluster_gov->nsamp < MAXSAMPLES)
		return;

	/*
	 * If cluster's last entered mode is shallower state then calculate
	 * the next predicted wakeup as avg of previous samples
	 */
	if (idx < genpd->state_count - 1) {
		for (i = 0; i < MAXSAMPLES; i++)
			avg_residency += cluster_gov->history[i].residency;
		do_div(avg_residency, MAXSAMPLES);
		cluster_gov->pred_wakeup = ktime_add_us(avg_residency,
							cluster_gov->now);
		cluster_gov->predicted = true;
		return;
	}

	/*
	 * Find the number of premature exits for each of the mode,
	 * excluding clockgating mode, and they are more than fifty
	 * percent restrict that and deeper modes.
	 */
	for (j = 1; j < genpd->state_count; j++) {
		uint32_t count = 0;
		u32 residency = genpd->states[j].residency_ns;

		avg_residency = 0;
		for (i = 0; i < MAXSAMPLES; i++) {

			if ((cluster_gov->history[i].mode == j) &&
			    (cluster_gov->history[i].residency <
					do_div(residency, NSEC_PER_USEC))) {
				count++;
				avg_residency +=
					cluster_gov->history[i].residency;
			}
		}

		if (count > PRED_PREMATURE_CNT) {
			do_div(avg_residency, count);
			cluster_gov->pred_wakeup = ktime_add_us(cluster_gov->now,
								avg_residency);
			cluster_gov->predicted = true;
			return;
		}
	}
}

/**
 * clear_cluster_history() - Clears the stored previous samples data.
 *			   It will be called when APSS going to deep sleep.
 * @cluster_gov: Targeted cluster's lpm data structure
 */
static void clear_cluster_history(struct lpm_cluster *cluster_gov)
{
	int i;

	for (i = 0; i < MAXSAMPLES; i++) {
		cluster_gov->history[i].residency  = 0;
		cluster_gov->history[i].mode = -1;
		cluster_gov->history[i].entry_time = 0;
	}

	cluster_gov->samples_idx = 0;
	cluster_gov->nsamp = 0;
	cluster_gov->history_invalid = false;
	cluster_gov->htmr_wkup = false;
}

/**
 * update_cluster_history() - Update the smaples history data every time when
 *			    cluster exit from sleep.
 * @cluster_gov: Targeted cluster's lpm data structure
 */
static void update_cluster_history(struct lpm_cluster *cluster_gov)
{
	bool tmr = false;
	uint32_t residency = 0;
	struct generic_pm_domain *genpd = cluster_gov->genpd;
	int idx = genpd->state_idx, samples_idx = cluster_gov->samples_idx;

	if (prediction_disabled)
		return;

	if ((cluster_gov->entry_idx == -1) || (cluster_gov->entry_idx == idx)) {
		residency = ktime_sub(cluster_gov->now, cluster_gov->entry_time);
		residency = ktime_to_us(residency);
		cluster_gov->history[samples_idx].entry_time =
					ktime_to_us(cluster_gov->entry_time);
	} else
		return;

	if (cluster_gov->htmr_wkup) {
		if (!samples_idx)
			samples_idx = MAXSAMPLES - 1;
		else
			samples_idx--;
		cluster_gov->history[samples_idx].residency += residency;
		cluster_gov->htmr_wkup = false;
		tmr = true;
	} else
		cluster_gov->history[samples_idx].residency = residency;

	cluster_gov->history[samples_idx].mode = idx;
	cluster_gov->entry_idx = INT_MIN;
	cluster_gov->entry_time = 0;
	if (cluster_gov->nsamp < MAXSAMPLES)
		cluster_gov->nsamp++;
	trace_cluster_pred_hist(cluster_gov->history[samples_idx].mode,
				cluster_gov->history[samples_idx].residency,
				samples_idx, tmr);
	samples_idx++;
	if (samples_idx >= MAXSAMPLES)
		samples_idx = 0;

	cluster_gov->samples_idx = samples_idx;
}

/**
 * cluster_power_down() - Will be called when cluster domain going to power off.
 *			If this entry's next wakeup was predicted it programs
 *			the cluster prediction timer and stores the idx entering
 *			and entry time of this lpm into clusters private data
 *			structure.
 * @cluster_gov:  cluster's lpm data structure
 */
static void cluster_power_down(struct lpm_cluster *cluster_gov)
{
	struct generic_pm_domain *genpd = cluster_gov->genpd;
	struct genpd_governor_data *gd = genpd->gd;
	int idx = genpd->state_idx;
	uint32_t residency;

	if (idx < 0)
		return;

	cluster_gov->entry_time = cluster_gov->now;
	cluster_gov->entry_idx = idx;
	trace_cluster_pred_select(genpd->state_idx, gd->next_wakeup,
				  0, cluster_gov->predicted, cluster_gov->next_wakeup);
	if (idx >= genpd->state_count - 1) {
		clear_cpu_predict_history();
		clear_cluster_history(cluster_gov);
		return;
	}
	if (ktime_compare(cluster_gov->next_wakeup, cluster_gov->pred_wakeup))
		return;

	residency = genpd->states[idx + 1].residency_ns;
	do_div(residency, NSEC_PER_USEC);
	clusttimer_start(cluster_gov, residency + PRED_TIMER_ADD);
}

/**
 * cluster_power_cb() - It will be called when cluster domain power_off/power_on
 * @nb:   notifier block of the cluster
 * @action:  action i.e power_off/power_on
 * @data:  pointer to private data structure
 *
 * It returns the NOTIFY_OK/NOTIFY_BAD to notify the notifier call chain
 */
static int cluster_power_cb(struct notifier_block *nb,
			    unsigned long action, void *data)
{
	struct lpm_cluster *cluster_gov = container_of(nb, struct lpm_cluster, genpd_nb);
	struct generic_pm_domain *pd = cluster_gov->genpd;
	struct genpd_power_state *state = &pd->states[pd->state_idx];
	struct lpm_cpu *cpu_gov;
	int cpu;
	u32 *suspend_param = state->data;

	switch (action) {
	case GENPD_NOTIFY_ON:
		trace_cluster_exit(raw_smp_processor_id(), pd->state_idx, *suspend_param);
		if (cluster_gov->genpd->suspended_count != 0)
			break;

		cluster_gov->now = ktime_get();
		clusttimer_cancel(cluster_gov);
		update_cluster_history(cluster_gov);
		cluster_predict(cluster_gov);
		break;
	case GENPD_NOTIFY_PRE_OFF:
		if (!pd->gd)
			return NOTIFY_BAD;

		if (!cluster_gov->state_allowed[pd->state_idx])
			return NOTIFY_BAD;

		if (cluster_gov->genpd->suspended_count != 0) {
			clear_cpu_predict_history();
			clear_cluster_history(cluster_gov);
			break;
		}

		for_each_cpu(cpu, cluster_gov->genpd->cpus) {
			if (cpu_online(cpu)) {
				cpu_gov = per_cpu_ptr(&lpm_cpu_data, cpu);
				if (cpu_gov->ipi_pending)
					return NOTIFY_BAD;
			}
		}

		cluster_gov->now = ktime_get();
		cluster_power_down(cluster_gov);
		break;
	case GENPD_NOTIFY_OFF:
		trace_cluster_enter(raw_smp_processor_id(), pd->state_idx, *suspend_param);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

/**
 * get_cluster_sleep_time() - It returns the aggregated next_wakeup of all cpus
 *			    which are in online for this cluster domain.
 * @cluster_gov: Targeted cluster's lpm data structure
 */
ktime_t get_cluster_sleep_time(struct lpm_cluster *cluster_gov)
{
	int cpu;
	ktime_t next_wakeup, next_cpu_wakeup;
	struct generic_pm_domain *genpd = cluster_gov->genpd;

	next_wakeup = KTIME_MAX;
	for_each_cpu_and(cpu, genpd->cpus, cpu_online_mask) {
		next_cpu_wakeup = cluster_gov->cpu_next_wakeup[cpu];
		if (ktime_before(next_cpu_wakeup, next_wakeup))
			next_wakeup = next_cpu_wakeup;
	}

	return next_wakeup;
}

/**
 * update_cluster_next_wakeup() - Update the this cluster device next wakeup with
 *				aggregated next_wakeup of all cpus which are in
 *				lpm for this cluster or this clusters predicted
 *				next wakeup whichever is earlier.
 * @cluster_gov: Targeted cluster's lpm data structure
 */
static void update_cluster_next_wakeup(struct lpm_cluster *cluster_gov)
{
	cluster_gov->next_wakeup = get_cluster_sleep_time(cluster_gov);
	if (cluster_gov->pred_wakeup) {
		if (ktime_before(cluster_gov->pred_wakeup,
				cluster_gov->next_wakeup))
			cluster_gov->next_wakeup = cluster_gov->pred_wakeup;
	}

	dev_pm_genpd_set_next_wakeup(cluster_gov->dev,
				     cluster_gov->next_wakeup);
}

/**
 * update_cluster_select() - This will be called when cpu is going to lpm to update
 *			   its next wakeup value to corresponding cluster domain device.
 * @cpu_gov: CPU's lpm data structure.
 */
void update_cluster_select(struct lpm_cpu *cpu_gov)
{
	struct generic_pm_domain *genpd;
	struct lpm_cluster *cluster_gov;
	int cpu = cpu_gov->cpu;

	list_for_each_entry(cluster_gov, &cluster_dev_list, list) {
		if (!cluster_gov->initialized)
			continue;

		genpd = cluster_gov->genpd;
		if (cpumask_test_cpu(cpu, genpd->cpus)) {
			spin_lock(&cluster_gov->lock);
			cluster_gov->now = cpu_gov->now;
			cluster_gov->cpu_next_wakeup[cpu] = cpu_gov->next_wakeup;
			update_cluster_next_wakeup(cluster_gov);
			spin_unlock(&cluster_gov->lock);
		}
	}
}

#if defined(_TRACE_HOOK_PM_DOMAIN_H)
static void android_vh_allow_domain_state(void *unused,
					  struct generic_pm_domain *genpd,
					  uint32_t idx, bool *allow)
{
	struct lpm_cluster *cluster_gov = to_cluster(genpd);

	if (!cluster_gov)
		return;

	*allow = cluster_gov->state_allowed[idx];
}
#endif

static void cluster_gov_disable(void)
{
#if defined(_TRACE_HOOK_PM_DOMAIN_H)
	unregister_trace_android_vh_allow_domain_state(android_vh_allow_domain_state, NULL);
#endif
}

static void cluster_gov_enable(void)
{
#if defined(_TRACE_HOOK_PM_DOMAIN_H)
	register_trace_android_vh_allow_domain_state(android_vh_allow_domain_state, NULL);
#endif
}

struct cluster_governor gov_ops = {
	.select = update_cluster_select,
	.enable = cluster_gov_enable,
	.disable = cluster_gov_disable,
};

static int lpm_cluster_gov_remove(struct platform_device *pdev)
{
	struct generic_pm_domain *genpd = pd_to_genpd(pdev->dev.pm_domain);
	struct lpm_cluster *cluster_gov = to_cluster(genpd);

	if (!cluster_gov)
		return -ENODEV;

	pm_runtime_disable(&pdev->dev);
	cluster_gov->genpd->flags &= ~GENPD_FLAG_MIN_RESIDENCY;
	remove_cluster_sysfs_nodes(cluster_gov);
	dev_pm_genpd_remove_notifier(cluster_gov->dev);

	return 0;
}

static int lpm_cluster_gov_probe(struct platform_device *pdev)
{
	int ret;
	int i;
	struct lpm_cluster *cluster_gov;

	cluster_gov = devm_kzalloc(&pdev->dev,
				   sizeof(struct lpm_cluster),
				   GFP_KERNEL);
	if (!cluster_gov)
		return -ENOMEM;

	spin_lock_init(&cluster_gov->lock);
	cluster_gov->dev = &pdev->dev;
	cluster_gov->pred_wakeup = KTIME_MAX;
	pm_runtime_enable(&pdev->dev);
	hrtimer_init(&cluster_gov->histtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	cluster_gov->genpd = pd_to_genpd(cluster_gov->dev->pm_domain);
	cluster_gov->genpd_nb.notifier_call = cluster_power_cb;
	cluster_gov->genpd->flags |= GENPD_FLAG_MIN_RESIDENCY;
	ret = dev_pm_genpd_add_notifier(cluster_gov->dev,
					&cluster_gov->genpd_nb);
	if (ret) {
		pm_runtime_disable(&pdev->dev);
		return ret;
	}

	if (create_cluster_sysfs_nodes(cluster_gov)) {
		pm_runtime_disable(&pdev->dev);
		return ret;
	}

	list_add_tail(&cluster_gov->list, &cluster_dev_list);
	cluster_gov->initialized = true;

	for (i = 0; i < cluster_gov->genpd->state_count; i++)
		cluster_gov->state_allowed[i] = true;

	register_cluster_governor_ops(&gov_ops);

	return 0;
}

static const struct of_device_id qcom_cluster_lpm[] = {
	{ .compatible = "qcom,lpm-cluster-dev" },
	{ }
};

static struct platform_driver qcom_cluster_lpm_driver = {
	.probe = lpm_cluster_gov_probe,
	.remove = lpm_cluster_gov_remove,
	.driver = {
		.name = "qcom-cluster-lpm-gov",
		.of_match_table = qcom_cluster_lpm,
		.suppress_bind_attrs = true,
	},
};

void qcom_cluster_lpm_governor_deinit(void)
{
	 platform_driver_unregister(&qcom_cluster_lpm_driver);
}

int qcom_cluster_lpm_governor_init(void)
{
	return platform_driver_register(&qcom_cluster_lpm_driver);
}
