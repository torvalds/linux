// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2006-2007 Adam Belay <abelay@novell.com>
 * Copyright (C) 2009 Intel Corporation
 * Copyright (c) 2012-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/cpu.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/sched/idle.h>
#if IS_ENABLED(CONFIG_SCHED_WALT)
#include <linux/sched/walt.h>
#endif
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/suspend.h>
#include <linux/tick.h>
#include <linux/time64.h>
#include <trace/events/ipi.h>
#include <trace/events/power.h>
#include <trace/hooks/cpuidle.h>

#include "qcom-lpm.h"
#define CREATE_TRACE_POINTS
#include "trace-qcom-lpm.h"

#define LPM_PRED_RESET				0
#define LPM_PRED_RESIDENCY_PATTERN		1
#define LPM_PRED_PREMATURE_EXITS		2
#define LPM_PRED_IPI_PATTERN			3

#define LPM_SELECT_STATE_DISABLED		0
#define LPM_SELECT_STATE_QOS_UNMET		1
#define LPM_SELECT_STATE_RESIDENCY_UNMET	2
#define LPM_SELECT_STATE_PRED			3
#define LPM_SELECT_STATE_IPI_PENDING		4
#define LPM_SELECT_STATE_SCHED_BIAS		5
#define LPM_SELECT_STATE_MAX			7

#define UPDATE_REASON(i, u)			(BIT(u) << (MAX_LPM_CPUS * i))

bool prediction_disabled;
bool sleep_disabled = true;
static bool suspend_in_progress;
static bool traces_registered;
static struct cluster_governor *cluster_gov_ops;

DEFINE_PER_CPU(struct lpm_cpu, lpm_cpu_data);

static inline bool check_cpu_isactive(int cpu)
{
	return cpu_active(cpu);
}

static bool lpm_disallowed(s64 sleep_ns, int cpu)
{
#if IS_ENABLED(CONFIG_SCHED_WALT)
	struct lpm_cpu *cpu_gov = per_cpu_ptr(&lpm_cpu_data, cpu);
	uint64_t bias_time = 0;
#endif

	if (!check_cpu_isactive(cpu))
		return false;

	if ((sleep_disabled || sleep_ns < 0))
		return true;

#if IS_ENABLED(CONFIG_SCHED_WALT)
	if (!sched_lpm_disallowed_time(cpu, &bias_time)) {
		cpu_gov->last_idx = 0;
		cpu_gov->bias = bias_time;
		return true;
	}
#endif
	return false;
}

/**
 * histtimer_fn() - Will be executed when per cpu prediction timer expires
 * @h:      cpu prediction timer
 */
static enum hrtimer_restart histtimer_fn(struct hrtimer *h)
{
	struct lpm_cpu *cpu_gov = this_cpu_ptr(&lpm_cpu_data);

	cpu_gov->history_invalid = 1;

	return HRTIMER_NORESTART;
}

/**
 * histtimer_start()  - Program the hrtimer with given timer value
 * @time_ns:      Value to be program
 */
static void histtimer_start(uint32_t time_ns)
{
	ktime_t hist_ktime = ns_to_ktime(time_ns * NSEC_PER_USEC);
	struct lpm_cpu *cpu_gov = this_cpu_ptr(&lpm_cpu_data);
	struct hrtimer *cpu_histtimer = &cpu_gov->histtimer;

	cpu_histtimer->function = histtimer_fn;
	hrtimer_start(cpu_histtimer, hist_ktime, HRTIMER_MODE_REL_PINNED);
}

/**
 * histtimer_cancel()  - Cancel the histtimer after cpu wakes up from lpm
 */
static void histtimer_cancel(void)
{
	struct lpm_cpu *cpu_gov = this_cpu_ptr(&lpm_cpu_data);
	struct hrtimer *cpu_histtimer = &cpu_gov->histtimer;
	ktime_t time_rem;

	if (!hrtimer_active(cpu_histtimer))
		return;

	time_rem = hrtimer_get_remaining(cpu_histtimer);
	if (ktime_to_us(time_rem) <= 0)
		return;

	hrtimer_try_to_cancel(cpu_histtimer);
}

static void biastimer_cancel(void)
{
	struct lpm_cpu *cpu_gov = this_cpu_ptr(&lpm_cpu_data);
	struct hrtimer *cpu_biastimer = &cpu_gov->biastimer;
	ktime_t time_rem;

	if (!cpu_gov->bias)
		return;

	cpu_gov->bias = 0;
	time_rem = hrtimer_get_remaining(cpu_biastimer);
	if (ktime_to_us(time_rem) <= 0)
		return;

	hrtimer_try_to_cancel(cpu_biastimer);
}

static enum hrtimer_restart biastimer_fn(struct hrtimer *h)
{
	return HRTIMER_NORESTART;
}

static void biastimer_start(uint32_t time_ns)
{
	ktime_t bias_ktime = ns_to_ktime(time_ns);
	struct lpm_cpu *cpu_gov = this_cpu_ptr(&lpm_cpu_data);
	struct hrtimer *cpu_biastimer = &cpu_gov->biastimer;

	cpu_biastimer->function = biastimer_fn;
	hrtimer_start(cpu_biastimer, bias_ktime, HRTIMER_MODE_REL_PINNED);
}

/**
 * find_deviation() - Try to detect repeat patterns by keeping track of past
 *		    samples and check if the standard deviation of that set
 *		    of previous sample is below a threshold. If it is below
 *		    threshold then use average of these past samples as
 *		    predicted value.
 * @cpu_gov:  targeted cpu's lpm data structure
 * @duration_ns:  cpu's scheduler sleep length
 */
static uint64_t find_deviation(struct lpm_cpu *cpu_gov, int *samples_history,
			       u64 duration_ns)
{
	uint64_t max, avg, stddev;
	uint64_t thresh = LLONG_MAX;
	struct cpuidle_driver *drv = cpu_gov->drv;
	int divisor, i, last_level = drv->state_count - 1;
	struct cpuidle_state *max_state = &drv->states[last_level];

	do {
		max = avg = divisor = stddev = 0;
		for (i = 0; i < MAXSAMPLES; i++) {
			int64_t value = samples_history[i];

			if (value <= thresh) {
				avg += value;
				divisor++;
				if (value > max)
					max = value;
			}
		}
		do_div(avg, divisor);

		for (i = 0; i < MAXSAMPLES; i++) {
			int64_t value = samples_history[i];

			if (value <= thresh) {
				int64_t diff = value - avg;

				stddev += diff * diff;
			}
		}
		do_div(stddev, divisor);
		stddev = int_sqrt(stddev);

	/*
	 * If the deviation is less, return the average, else
	 * ignore one maximum sample and retry
	 */
		if (((avg > stddev * 6) && (divisor >= (MAXSAMPLES - 1)))
					|| stddev <= PRED_REF_STDDEV) {
			do_div(duration_ns, NSEC_PER_USEC);
			if (avg >= duration_ns ||
				avg > max_state->target_residency)
				return 0;

			cpu_gov->next_pred_time = ktime_to_us(cpu_gov->now) + avg;
			return avg;
		}
		thresh = max - 1;

	} while (divisor > (MAXSAMPLES - 1));

	return 0;
}

/**
 * cpu_predict() - Predict the cpus next wakeup.
 * @cpu_gov:  targeted cpu's lpm data structure
 * @duration_ns:  cpu's scheduler sleep length
 */
static void cpu_predict(struct lpm_cpu *cpu_gov, u64 duration_ns)
{
	int i, j;
	struct cpuidle_driver *drv = cpu_gov->drv;
	struct cpuidle_state *min_state = &drv->states[0];
	struct history_lpm *lpm_history = &cpu_gov->lpm_history;
	struct history_ipi *ipi_history = &cpu_gov->ipi_history;

	if (prediction_disabled)
		return;

	/*
	 * Samples are marked invalid when woken-up due to timer,
	 * so do not predict.
	 */
	if (cpu_gov->history_invalid) {
		cpu_gov->history_invalid = false;
		cpu_gov->htmr_wkup = true;
		cpu_gov->next_pred_time = 0;
		return;
	}

	/*
	 * If the duration_ns itself is not sufficient for deeper
	 * low power modes than clock gating do not predict
	 */
	if (min_state->target_residency_ns > duration_ns)
		return;

	/* Predict only when all the samples are collected */
	if (lpm_history->nsamp < MAXSAMPLES) {
		cpu_gov->next_pred_time = 0;
		return;
	}

	/*
	 * Check if the samples are not much deviated, if so use the
	 * average of those as predicted sleep time. Else if any
	 * specific mode has more premature exits return the index of
	 * that mode.
	 */
	cpu_gov->predicted = find_deviation(cpu_gov, lpm_history->resi, duration_ns);
	if (cpu_gov->predicted) {
		cpu_gov->pred_type = LPM_PRED_RESIDENCY_PATTERN;
		return;
	}

	/*
	 * Find the number of premature exits for each of the mode,
	 * excluding clockgating mode, and they are more than fifty
	 * percent restrict that and deeper modes.
	 */
	for (j = 1; j < drv->state_count; j++) {
		struct cpuidle_state *s = &drv->states[j];
		uint32_t min_residency = s->target_residency;
		uint32_t count = 0;
		uint64_t avg_residency = 0;

		for (i = 0; i < MAXSAMPLES; i++) {
			if ((lpm_history->mode[i] == j) &&
				(lpm_history->resi[i] < min_residency)) {
				count++;
				avg_residency += lpm_history->resi[i];
			}
		}

		if (count >= PRED_PREMATURE_CNT) {
			do_div(avg_residency, count);
			cpu_gov->predicted = avg_residency;
			cpu_gov->next_pred_time = ktime_to_us(cpu_gov->now)
								+ cpu_gov->predicted;
			cpu_gov->pred_type = LPM_PRED_PREMATURE_EXITS;
			break;
		}
	}

	if (cpu_gov->predicted)
		return;

	cpu_gov->predicted = find_deviation(cpu_gov, ipi_history->interval,
					    duration_ns);
	if (cpu_gov->predicted)
		cpu_gov->pred_type = LPM_PRED_IPI_PATTERN;
}

/**
 * clear_cpu_predict_history() - Clears the stored previous samples data.
 *			       It will be called when APSS going to deep sleep.
 */
void clear_cpu_predict_history(void)
{
	struct lpm_cpu *cpu_gov;
	struct history_lpm *lpm_history;
	int i, cpu;

	if (prediction_disabled)
		return;

	for_each_possible_cpu(cpu) {
		cpu_gov = this_cpu_ptr(&lpm_cpu_data);
		lpm_history = &cpu_gov->lpm_history;
		for (i = 0; i < MAXSAMPLES; i++) {
			lpm_history->resi[i]  = 0;
			lpm_history->mode[i] = -1;
			lpm_history->samples_idx = 0;
			lpm_history->nsamp = 0;
			cpu_gov->next_pred_time = 0;
			cpu_gov->pred_type = LPM_PRED_RESET;
		}
	}
}

/**
 * update_cpu_history() - Update the samples history data every time when
 *			cpu comes from sleep.
 * @cpu_gov:  targeted cpu's lpm data structure
 */
static void update_cpu_history(struct lpm_cpu *cpu_gov)
{
	bool tmr = false;
	int idx = cpu_gov->last_idx;
	struct history_lpm *lpm_history = &cpu_gov->lpm_history;
	u64 measured_us = ktime_to_us(cpu_gov->dev->last_residency_ns);
	struct cpuidle_state *target;

	if (sleep_disabled || prediction_disabled || idx < 0 ||
	    idx > cpu_gov->drv->state_count - 1)
		return;

	target = &cpu_gov->drv->states[idx];

	if (measured_us > target->exit_latency)
		measured_us -= target->exit_latency;

	if (cpu_gov->htmr_wkup) {
		if (!lpm_history->samples_idx)
			lpm_history->samples_idx = MAXSAMPLES - 1;
		else
			lpm_history->samples_idx--;

		lpm_history->resi[lpm_history->samples_idx] += measured_us;
		cpu_gov->htmr_wkup = false;
		tmr = true;
	} else
		lpm_history->resi[lpm_history->samples_idx] = measured_us;

	lpm_history->mode[lpm_history->samples_idx] = idx;
	cpu_gov->pred_type = LPM_PRED_RESET;

	trace_gov_pred_hist(idx, lpm_history->resi[lpm_history->samples_idx],
			    tmr);

	if (lpm_history->nsamp < MAXSAMPLES)
		lpm_history->nsamp++;

	lpm_history->samples_idx++;
	if (lpm_history->samples_idx >= MAXSAMPLES)
		lpm_history->samples_idx = 0;
}

void update_ipi_history(int cpu)
{
	struct lpm_cpu *cpu_gov = per_cpu_ptr(&lpm_cpu_data, cpu);
	struct history_ipi *history = &cpu_gov->ipi_history;
	ktime_t now = ktime_get();

	history->interval[history->current_ptr] =
			ktime_to_us(ktime_sub(now,
			history->cpu_idle_resched_ts));
	(history->current_ptr)++;
	if (history->current_ptr >= MAXSAMPLES)
		history->current_ptr = 0;

	history->cpu_idle_resched_ts = now;
}

/**
 * lpm_cpu_qos_notify() - It will be called when any new request came on PM QoS.
 *			It wakes up the cpu if it is in idle sleep to honour
 *			the new PM QoS request.
 * @nfb:  notifier block of the CPU
 * @val:  notification value
 * @ptr:  pointer to private data structure
 */
static int lpm_cpu_qos_notify(struct notifier_block *nfb,
			      unsigned long val, void *ptr)
{
	struct lpm_cpu *cpu_gov = container_of(nfb, struct lpm_cpu, nb);
	int cpu = cpu_gov->cpu;

	if (!cpu_gov->enable)
		return NOTIFY_OK;

	preempt_disable();
	if (cpu != smp_processor_id() && cpu_online(cpu) &&
	    check_cpu_isactive(cpu))
		wake_up_if_idle(cpu);
	preempt_enable();

	return NOTIFY_OK;
}

static int lpm_offline_cpu(unsigned int cpu)
{
	struct lpm_cpu *cpu_gov = per_cpu_ptr(&lpm_cpu_data, cpu);
	struct device *dev = get_cpu_device(cpu);

	if (!dev || !cpu_gov)
		return 0;

	dev_pm_qos_remove_notifier(dev, &cpu_gov->nb,
				   DEV_PM_QOS_RESUME_LATENCY);

	return 0;
}

static int lpm_online_cpu(unsigned int cpu)
{
	struct lpm_cpu *cpu_gov = per_cpu_ptr(&lpm_cpu_data, cpu);
	struct device *dev = get_cpu_device(cpu);

	if (!dev || !cpu_gov)
		return 0;

	cpu_gov->nb.notifier_call = lpm_cpu_qos_notify;
	dev_pm_qos_add_notifier(dev, &cpu_gov->nb,
				DEV_PM_QOS_RESUME_LATENCY);

	return 0;
}

static void ipi_raise(void *ignore, const struct cpumask *mask, const char *unused)
{
	int cpu;
	struct lpm_cpu *cpu_gov;
	unsigned long flags;

	if (suspend_in_progress)
		return;

	for_each_cpu(cpu, mask) {
		cpu_gov = &(per_cpu(lpm_cpu_data, cpu));
		if (!cpu_gov->enable)
			return;

		spin_lock_irqsave(&cpu_gov->lock, flags);
		cpu_gov->ipi_pending = true;
		spin_unlock_irqrestore(&cpu_gov->lock, flags);
		update_ipi_history(cpu);
	}
}

static void ipi_entry(void *ignore, const char *unused)
{
	int cpu;
	struct lpm_cpu *cpu_gov;
	unsigned long flags;

	if (suspend_in_progress)
		return;

	cpu = raw_smp_processor_id();
	cpu_gov = &(per_cpu(lpm_cpu_data, cpu));
	if (!cpu_gov->enable)
		return;

	spin_lock_irqsave(&cpu_gov->lock, flags);
	cpu_gov->ipi_pending = false;
	spin_unlock_irqrestore(&cpu_gov->lock, flags);
}

/**
 * get_cpus_qos() - Returns the aggrigated PM QoS request.
 * @mask: cpumask of the cpus
 */
static inline s64 get_cpus_qos(const struct cpumask *mask)
{
	int cpu;
	s64 n, latency = PM_QOS_CPU_LATENCY_DEFAULT_VALUE * NSEC_PER_USEC;

	for_each_cpu(cpu, mask) {
		if (!check_cpu_isactive(cpu))
			continue;
		n = cpuidle_governor_latency_req(cpu);
		if (n < latency)
			latency = n;
	}

	return latency;
}

/**
 * start_prediction_timer() - Programs the prediction hrtimer and make the timer
 *			    to run. It wakes up the cpus from shallower state in
 *			    misprediction case and saves the power by not letting
 *			    the cpu remains in sollower state.
 * @cpu_gov:  cpu's lpm data structure
 * @duration_us:  cpu's scheduled sleep length
 */
static int start_prediction_timer(struct lpm_cpu *cpu_gov, int duration_us)
{
	struct cpuidle_state *s;
	uint32_t htime = 0, max_residency;
	uint32_t last_level = cpu_gov->drv->state_count - 1;

	if (!cpu_gov->predicted || cpu_gov->last_idx >= last_level)
		return 0;

	if (cpu_gov->next_wakeup > cpu_gov->next_pred_time)
		cpu_gov->next_wakeup = cpu_gov->next_pred_time;

	s = &cpu_gov->drv->states[cpu_gov->last_idx];
	max_residency  = s[cpu_gov->last_idx + 1].target_residency - 1;
	htime = cpu_gov->predicted + PRED_TIMER_ADD;

	if (htime > max_residency)
		htime = max_residency;

	if ((duration_us > htime) && ((duration_us - htime) > max_residency))
		histtimer_start(htime);

	return htime;
}

void register_cluster_governor_ops(struct cluster_governor *ops)
{
	if (!ops)
		return;

	cluster_gov_ops = ops;
}

/**
 * lpm_select() - Find the best idle state for the cpu device
 * @dev:       Target cpu
 * @state:     Entered state
 * @stop_tick: Is the tick device stopped
 *
 * Return: Best cpu LPM mode to enter
 */
static int lpm_select(struct cpuidle_driver *drv, struct cpuidle_device *dev,
		      bool *stop_tick)
{
	struct lpm_cpu *cpu_gov = this_cpu_ptr(&lpm_cpu_data);
	s64 latency_req = get_cpus_qos(cpumask_of(dev->cpu));
	ktime_t delta_tick;
	u64 reason = 0;
	uint64_t duration_ns, htime = 0;
	int i = 0;

	if (!cpu_gov)
		return 0;

	do_div(latency_req, NSEC_PER_USEC);
	cpu_gov->predicted = 0;
	cpu_gov->predict_started = false;
	cpu_gov->now = ktime_get();
	duration_ns = tick_nohz_get_sleep_length(&delta_tick);
	update_cpu_history(cpu_gov);

	if (lpm_disallowed(duration_ns, dev->cpu))
		goto done;

	for (i = drv->state_count - 1; i > 0; i--) {
		struct cpuidle_state *s = &drv->states[i];

		if (dev->states_usage[i].disable) {
			reason |= UPDATE_REASON(i, LPM_SELECT_STATE_DISABLED);
			continue;
		}

		if (latency_req < s->exit_latency) {
			reason |= UPDATE_REASON(i, LPM_SELECT_STATE_QOS_UNMET);
			continue;
		}

		if (s->target_residency_ns > duration_ns) {
			reason |= UPDATE_REASON(i,
					LPM_SELECT_STATE_RESIDENCY_UNMET);
			continue;
		}

		if (check_cpu_isactive(dev->cpu) && !cpu_gov->predict_started) {
			cpu_predict(cpu_gov, duration_ns);
			cpu_gov->predict_started = true;
		}

		if (cpu_gov->predicted)
			if (s->target_residency > cpu_gov->predicted) {
				reason |= UPDATE_REASON(i,
						LPM_SELECT_STATE_PRED);
				continue;
		}
		break;
	}

	do_div(duration_ns, NSEC_PER_USEC);
	cpu_gov->last_idx = i;
	cpu_gov->next_wakeup = ktime_add_us(cpu_gov->now, duration_ns);
	htime = start_prediction_timer(cpu_gov, duration_ns);

	/* update this cpu next_wakeup into its parent power domain device */
	if (cpu_gov->last_idx == drv->state_count - 1) {
		if (cluster_gov_ops && cluster_gov_ops->select)
			cluster_gov_ops->select(cpu_gov);
	}

done:
	if ((!cpu_gov->last_idx) && cpu_gov->bias) {
		biastimer_start(cpu_gov->bias);
		reason |= UPDATE_REASON(i, LPM_SELECT_STATE_SCHED_BIAS);
	}

	trace_lpm_gov_select(i, latency_req, duration_ns, reason);
	trace_gov_pred_select(cpu_gov->pred_type, cpu_gov->predicted, htime);

	return i;
}

/**
 * lpm_reflect() - Update the state entered by the cpu device
 * @dev:       Target CPU
 * @state:     Entered state
 */
static void lpm_reflect(struct cpuidle_device *dev, int state)
{

}

/**
 * lpm_idle_enter() - Notification with cpuidle state during idle entry
 * @unused:   unused
 * @state:    selected state by governor's .select
 * @dev:      cpuidle_device
 */
static void lpm_idle_enter(void *unused, int *state, struct cpuidle_device *dev)
{
	struct lpm_cpu *cpu_gov = this_cpu_ptr(&lpm_cpu_data);
	u64 reason = 0;
	unsigned long flags;

	if (*state == 0)
		return;

	if (!cpu_gov->enable)
		return;

	/* Restrict to WFI state if there is an IPI pending on current CPU */
	spin_lock_irqsave(&cpu_gov->lock, flags);
	if (cpu_gov->ipi_pending) {
		reason = UPDATE_REASON(*state, LPM_SELECT_STATE_IPI_PENDING);
		*state = 0;
		trace_lpm_gov_select(*state, 0xdeaffeed, 0xdeaffeed, reason);
	}
	spin_unlock_irqrestore(&cpu_gov->lock, flags);
}

/**
 * lpm_idle_exit() - Notification with cpuidle state during idle exit
 * @unused:   unused
 * @state:    actual entered state by cpuidle
 * @dev:      cpuidle_device
 */
static void lpm_idle_exit(void *unused, int state, struct cpuidle_device *dev)
{
	struct lpm_cpu *cpu_gov = per_cpu_ptr(&lpm_cpu_data, dev->cpu);

	if (cpu_gov->enable) {
		histtimer_cancel();
		biastimer_cancel();
	}
}

/**
 * lpm_enable_device() - Initialize the governor's data for the CPU
 * @drv:      cpuidle driver
 * @dev:      Target CPU
 */
static int lpm_enable_device(struct cpuidle_driver *drv,
			     struct cpuidle_device *dev)
{
	struct lpm_cpu *cpu_gov = per_cpu_ptr(&lpm_cpu_data, dev->cpu);
	struct hrtimer *cpu_histtimer = &cpu_gov->histtimer;
	struct hrtimer *cpu_biastimer = &cpu_gov->biastimer;
	int ret;

	spin_lock_init(&cpu_gov->lock);
	hrtimer_init(cpu_histtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer_init(cpu_biastimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	if (!traces_registered) {
		ret = register_trace_ipi_raise(ipi_raise, NULL);
		if (ret)
			return ret;

		ret = register_trace_ipi_entry(ipi_entry, NULL);
		if (ret) {
			unregister_trace_ipi_raise(ipi_raise, NULL);
			return ret;
		}

		ret = register_trace_prio_android_vh_cpu_idle_enter(
					lpm_idle_enter, NULL, INT_MIN);
		if (ret) {
			unregister_trace_ipi_raise(ipi_raise, NULL);
			unregister_trace_ipi_entry(ipi_entry, NULL);
			return ret;
		}

		ret = register_trace_prio_android_vh_cpu_idle_exit(
					lpm_idle_exit, NULL, INT_MIN);
		if (ret) {
			unregister_trace_ipi_raise(ipi_raise, NULL);
			unregister_trace_ipi_entry(ipi_entry, NULL);
			unregister_trace_android_vh_cpu_idle_enter(
					lpm_idle_enter, NULL);
			return ret;
		}

		if (cluster_gov_ops && cluster_gov_ops->enable)
			cluster_gov_ops->enable();

		traces_registered = true;
	}

	cpu_gov->cpu = dev->cpu;
	cpu_gov->enable = true;
	cpu_gov->drv = drv;
	cpu_gov->dev = dev;
	cpu_gov->last_idx = -1;

	return 0;
}

/**
 * lpm_disable_device() - Clean up the governor's data for the CPU
 * @drv:      cpuidle driver
 * @dev:      Target CPU
 */
static void lpm_disable_device(struct cpuidle_driver *drv,
			       struct cpuidle_device *dev)
{
	struct lpm_cpu *cpu_gov = per_cpu_ptr(&lpm_cpu_data, dev->cpu);
	int cpu;

	cpu_gov->enable = false;
	cpu_gov->last_idx = -1;
	for_each_possible_cpu(cpu) {
		struct lpm_cpu *cpu_gov = per_cpu_ptr(&lpm_cpu_data, cpu);

		if (cpu_gov->enable)
			return;
	}

	if (traces_registered) {
		unregister_trace_ipi_raise(ipi_raise, NULL);
		unregister_trace_ipi_entry(ipi_entry, NULL);
		unregister_trace_android_vh_cpu_idle_enter(
					lpm_idle_enter, NULL);
		unregister_trace_android_vh_cpu_idle_exit(
					lpm_idle_exit, NULL);
		if (cluster_gov_ops && cluster_gov_ops->disable)
			cluster_gov_ops->disable();

		traces_registered = false;
	}
}

static void qcom_lpm_suspend_trace(void *unused, const char *action,
				   int event, bool start)
{
	if (start && !strcmp("dpm_suspend_late", action)) {
		suspend_in_progress = true;

		return;
	}

	if (!start && !strcmp("dpm_resume_early", action))
		suspend_in_progress = false;
}

static struct cpuidle_governor lpm_governor = {
	.name =		"qcom-cpu-lpm",
	.rating =	50,
	.enable =	lpm_enable_device,
	.disable =	lpm_disable_device,
	.select =	lpm_select,
	.reflect =	lpm_reflect,
};

static int __init qcom_lpm_governor_init(void)
{
	int ret;

	ret = create_global_sysfs_nodes();
	if (ret)
		goto sysfs_fail;

	ret = qcom_cluster_lpm_governor_init();
	if (ret)
		goto cluster_init_fail;

	ret = cpuidle_register_governor(&lpm_governor);
	if (ret)
		goto cpuidle_reg_fail;

	ret = register_trace_suspend_resume(qcom_lpm_suspend_trace, NULL);
	if (ret)
		goto cpuidle_reg_fail;

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "qcom-cpu-lpm",
				lpm_online_cpu, lpm_offline_cpu);
	if (ret < 0)
		goto cpuhp_setup_fail;

	return 0;

cpuhp_setup_fail:
	unregister_trace_suspend_resume(qcom_lpm_suspend_trace, NULL);
cpuidle_reg_fail:
	qcom_cluster_lpm_governor_deinit();
cluster_init_fail:
	remove_global_sysfs_nodes();
sysfs_fail:
	return ret;
}
module_init(qcom_lpm_governor_init);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. cpuidle LPM governor");
MODULE_LICENSE("GPL v2");
