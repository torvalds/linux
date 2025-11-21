// SPDX-License-Identifier: GPL-2.0
/*
 * Timer events oriented CPU idle governor
 *
 * Copyright (C) 2018 - 2021 Intel Corporation
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 */

/**
 * DOC: teo-description
 *
 * The idea of this governor is based on the observation that on many systems
 * timer interrupts are two or more orders of magnitude more frequent than any
 * other interrupt types, so they are likely to dominate CPU wakeup patterns.
 * Moreover, in principle, the time when the next timer event is going to occur
 * can be determined at the idle state selection time, although doing that may
 * be costly, so it can be regarded as the most reliable source of information
 * for idle state selection.
 *
 * Of course, non-timer wakeup sources are more important in some use cases,
 * but even then it is generally unnecessary to consider idle duration values
 * greater than the time till the next timer event, referred as the sleep
 * length in what follows, because the closest timer will ultimately wake up the
 * CPU anyway unless it is woken up earlier.
 *
 * However, since obtaining the sleep length may be costly, the governor first
 * checks if it can select a shallow idle state using wakeup pattern information
 * from recent times, in which case it can do without knowing the sleep length
 * at all.  For this purpose, it counts CPU wakeup events and looks for an idle
 * state whose target residency has not exceeded the idle duration (measured
 * after wakeup) in the majority of relevant recent cases.  If the target
 * residency of that state is small enough, it may be used right away and the
 * sleep length need not be determined.
 *
 * The computations carried out by this governor are based on using bins whose
 * boundaries are aligned with the target residency parameter values of the CPU
 * idle states provided by the %CPUIdle driver in the ascending order.  That is,
 * the first bin spans from 0 up to, but not including, the target residency of
 * the second idle state (idle state 1), the second bin spans from the target
 * residency of idle state 1 up to, but not including, the target residency of
 * idle state 2, the third bin spans from the target residency of idle state 2
 * up to, but not including, the target residency of idle state 3 and so on.
 * The last bin spans from the target residency of the deepest idle state
 * supplied by the driver to infinity.
 *
 * Two metrics called "hits" and "intercepts" are associated with each bin.
 * They are updated every time before selecting an idle state for the given CPU
 * in accordance with what happened last time.
 *
 * The "hits" metric reflects the relative frequency of situations in which the
 * sleep length and the idle duration measured after CPU wakeup fall into the
 * same bin (that is, the CPU appears to wake up "on time" relative to the sleep
 * length).  In turn, the "intercepts" metric reflects the relative frequency of
 * non-timer wakeup events for which the measured idle duration falls into a bin
 * that corresponds to an idle state shallower than the one whose bin is fallen
 * into by the sleep length (these events are also referred to as "intercepts"
 * below).
 *
 * The governor also counts "intercepts" with the measured idle duration below
 * the tick period length and uses this information when deciding whether or not
 * to stop the scheduler tick.
 *
 * In order to select an idle state for a CPU, the governor takes the following
 * steps (modulo the possible latency constraint that must be taken into account
 * too):
 *
 * 1. Find the deepest enabled CPU idle state (the candidate idle state) and
 *    compute 2 sums as follows:
 *
 *    - The sum of the "hits" metric for all of the idle states shallower than
 *      the candidate one (it represents the cases in which the CPU was likely
 *      woken up by a timer).
 *
 *    - The sum of the "intercepts" metric for all of the idle states shallower
 *      than the candidate one (it represents the cases in which the CPU was
 *      likely woken up by a non-timer wakeup source).
 *
 * 2. If the second sum computed in step 1 is greater than a half of the sum of
 *    both metrics for the candidate state bin and all subsequent bins (if any),
 *    a shallower idle state is likely to be more suitable, so look for it.
 *
 *    - Traverse the enabled idle states shallower than the candidate one in the
 *      descending order.
 *
 *    - For each of them compute the sum of the "intercepts" metrics over all
 *      of the idle states between it and the candidate one (including the
 *      former and excluding the latter).
 *
 *    - If this sum is greater than a half of the second sum computed in step 1,
 *      use the given idle state as the new candidate one.
 *
 * 3. If the current candidate state is state 0 or its target residency is short
 *    enough, return it and prevent the scheduler tick from being stopped.
 *
 * 4. Obtain the sleep length value and check if it is below the target
 *    residency of the current candidate state, in which case a new shallower
 *    candidate state needs to be found, so look for it.
 */

#include <linux/cpuidle.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/sched/clock.h>
#include <linux/tick.h>

#include "gov.h"

/*
 * Idle state exit latency threshold used for deciding whether or not to check
 * the time till the closest expected timer event.
 */
#define LATENCY_THRESHOLD_NS	(RESIDENCY_THRESHOLD_NS / 2)

/*
 * The PULSE value is added to metrics when they grow and the DECAY_SHIFT value
 * is used for decreasing metrics on a regular basis.
 */
#define PULSE		1024
#define DECAY_SHIFT	3

/**
 * struct teo_bin - Metrics used by the TEO cpuidle governor.
 * @intercepts: The "intercepts" metric.
 * @hits: The "hits" metric.
 */
struct teo_bin {
	unsigned int intercepts;
	unsigned int hits;
};

/**
 * struct teo_cpu - CPU data used by the TEO cpuidle governor.
 * @sleep_length_ns: Time till the closest timer event (at the selection time).
 * @state_bins: Idle state data bins for this CPU.
 * @total: Grand total of the "intercepts" and "hits" metrics for all bins.
 * @total_tick: Wakeups by the scheduler tick.
 * @tick_intercepts: "Intercepts" before TICK_NSEC.
 * @short_idles: Wakeups after short idle periods.
 * @tick_wakeup: Set if the last wakeup was by the scheduler tick.
 */
struct teo_cpu {
	s64 sleep_length_ns;
	struct teo_bin state_bins[CPUIDLE_STATE_MAX];
	unsigned int total;
	unsigned int total_tick;
	unsigned int tick_intercepts;
	unsigned int short_idles;
	bool tick_wakeup;
};

static DEFINE_PER_CPU(struct teo_cpu, teo_cpus);

static void teo_decay(unsigned int *metric)
{
	unsigned int delta = *metric >> DECAY_SHIFT;

	if (delta)
		*metric -= delta;
	else
		*metric = 0;
}

/**
 * teo_update - Update CPU metrics after wakeup.
 * @drv: cpuidle driver containing state data.
 * @dev: Target CPU.
 */
static void teo_update(struct cpuidle_driver *drv, struct cpuidle_device *dev)
{
	struct teo_cpu *cpu_data = this_cpu_ptr(&teo_cpus);
	int i, idx_timer = 0, idx_duration = 0;
	s64 target_residency_ns, measured_ns;
	unsigned int total = 0;

	teo_decay(&cpu_data->short_idles);

	if (dev->poll_time_limit) {
		dev->poll_time_limit = false;
		/*
		 * Polling state timeout has triggered, so assume that this
		 * might have been a long sleep.
		 */
		measured_ns = S64_MAX;
	} else {
		s64 lat_ns = drv->states[dev->last_state_idx].exit_latency_ns;

		measured_ns = dev->last_residency_ns;
		/*
		 * The delay between the wakeup and the first instruction
		 * executed by the CPU is not likely to be worst-case every
		 * time, so take 1/2 of the exit latency as a very rough
		 * approximation of the average of it.
		 */
		if (measured_ns >= lat_ns) {
			measured_ns -= lat_ns / 2;
			if (measured_ns < RESIDENCY_THRESHOLD_NS)
				cpu_data->short_idles += PULSE;
		} else {
			measured_ns /= 2;
			cpu_data->short_idles += PULSE;
		}
	}

	/*
	 * Decay the "hits" and "intercepts" metrics for all of the bins and
	 * find the bins that the sleep length and the measured idle duration
	 * fall into.
	 */
	for (i = 0; i < drv->state_count; i++) {
		struct teo_bin *bin = &cpu_data->state_bins[i];

		teo_decay(&bin->hits);
		total += bin->hits;
		teo_decay(&bin->intercepts);
		total += bin->intercepts;

		target_residency_ns = drv->states[i].target_residency_ns;

		if (target_residency_ns <= cpu_data->sleep_length_ns) {
			idx_timer = i;
			if (target_residency_ns <= measured_ns)
				idx_duration = i;
		}
	}

	cpu_data->total = total + PULSE;

	teo_decay(&cpu_data->tick_intercepts);

	teo_decay(&cpu_data->total_tick);
	if (cpu_data->tick_wakeup) {
		cpu_data->total_tick += PULSE;
		/*
		 * If tick wakeups dominate the wakeup pattern, count this one
		 * as a hit on the deepest available idle state to increase the
		 * likelihood of stopping the tick.
		 */
		if (3 * cpu_data->total_tick > 2 * cpu_data->total) {
			cpu_data->state_bins[drv->state_count-1].hits += PULSE;
			return;
		}
	}

	/*
	 * If the measured idle duration falls into the same bin as the sleep
	 * length, this is a "hit", so update the "hits" metric for that bin.
	 * Otherwise, update the "intercepts" metric for the bin fallen into by
	 * the measured idle duration.
	 */
	if (idx_timer == idx_duration) {
		cpu_data->state_bins[idx_timer].hits += PULSE;
	} else {
		cpu_data->state_bins[idx_duration].intercepts += PULSE;
		if (measured_ns <= TICK_NSEC)
			cpu_data->tick_intercepts += PULSE;
	}
}

/**
 * teo_find_shallower_state - Find shallower idle state matching given duration.
 * @drv: cpuidle driver containing state data.
 * @dev: Target CPU.
 * @state_idx: Index of the capping idle state.
 * @duration_ns: Idle duration value to match.
 */
static int teo_find_shallower_state(struct cpuidle_driver *drv,
				    struct cpuidle_device *dev, int state_idx,
				    s64 duration_ns)
{
	int i;

	for (i = state_idx - 1; i >= 0; i--) {
		if (dev->states_usage[i].disable)
			continue;

		state_idx = i;
		if (drv->states[i].target_residency_ns <= duration_ns)
			break;
	}
	return state_idx;
}

/**
 * teo_select - Selects the next idle state to enter.
 * @drv: cpuidle driver containing state data.
 * @dev: Target CPU.
 * @stop_tick: Indication on whether or not to stop the scheduler tick.
 */
static int teo_select(struct cpuidle_driver *drv, struct cpuidle_device *dev,
		      bool *stop_tick)
{
	struct teo_cpu *cpu_data = this_cpu_ptr(&teo_cpus);
	s64 latency_req = cpuidle_governor_latency_req(dev->cpu);
	ktime_t delta_tick = TICK_NSEC / 2;
	unsigned int idx_intercept_sum = 0;
	unsigned int intercept_sum = 0;
	unsigned int idx_hit_sum = 0;
	unsigned int hit_sum = 0;
	int constraint_idx = 0;
	int idx0 = 0, idx = -1;
	s64 duration_ns;
	int i;

	if (dev->last_state_idx >= 0) {
		teo_update(drv, dev);
		dev->last_state_idx = -1;
	}

	/*
	 * Set the sleep length to infinity in case the invocation of
	 * tick_nohz_get_sleep_length() below is skipped, in which case it won't
	 * be known whether or not the subsequent wakeup is caused by a timer.
	 * It is generally fine to count the wakeup as an intercept then, except
	 * for the cases when the CPU is mostly woken up by timers and there may
	 * be opportunities to ask for a deeper idle state when no imminent
	 * timers are scheduled which may be missed.
	 */
	cpu_data->sleep_length_ns = KTIME_MAX;

	/* Check if there is any choice in the first place. */
	if (drv->state_count < 2) {
		idx = 0;
		goto out_tick;
	}

	if (!dev->states_usage[0].disable)
		idx = 0;

	/* Compute the sums of metrics for early wakeup pattern detection. */
	for (i = 1; i < drv->state_count; i++) {
		struct teo_bin *prev_bin = &cpu_data->state_bins[i-1];
		struct cpuidle_state *s = &drv->states[i];

		/*
		 * Update the sums of idle state metrics for all of the states
		 * shallower than the current one.
		 */
		intercept_sum += prev_bin->intercepts;
		hit_sum += prev_bin->hits;

		if (dev->states_usage[i].disable)
			continue;

		if (idx < 0)
			idx0 = i; /* first enabled state */

		idx = i;

		if (s->exit_latency_ns <= latency_req)
			constraint_idx = i;

		/* Save the sums for the current state. */
		idx_intercept_sum = intercept_sum;
		idx_hit_sum = hit_sum;
	}

	/* Avoid unnecessary overhead. */
	if (idx < 0) {
		idx = 0; /* No states enabled, must use 0. */
		goto out_tick;
	}

	if (idx == idx0) {
		/*
		 * Only one idle state is enabled, so use it, but do not
		 * allow the tick to be stopped it is shallow enough.
		 */
		duration_ns = drv->states[idx].target_residency_ns;
		goto end;
	}

	/*
	 * If the sum of the intercepts metric for all of the idle states
	 * shallower than the current candidate one (idx) is greater than the
	 * sum of the intercepts and hits metrics for the candidate state and
	 * all of the deeper states, a shallower idle state is likely to be a
	 * better choice.
	 */
	if (2 * idx_intercept_sum > cpu_data->total - idx_hit_sum) {
		int min_idx = idx0;

		if (tick_nohz_tick_stopped()) {
			/*
			 * Look for the shallowest idle state below the current
			 * candidate one whose target residency is at least
			 * equal to the tick period length.
			 */
			while (min_idx < idx &&
			       drv->states[min_idx].target_residency_ns < TICK_NSEC)
				min_idx++;
		}

		/*
		 * Look for the deepest idle state whose target residency had
		 * not exceeded the idle duration in over a half of the relevant
		 * cases in the past.
		 *
		 * Take the possible duration limitation present if the tick
		 * has been stopped already into account.
		 */
		for (i = idx - 1, intercept_sum = 0; i >= min_idx; i--) {
			intercept_sum += cpu_data->state_bins[i].intercepts;

			if (dev->states_usage[i].disable)
				continue;

			idx = i;
			if (2 * intercept_sum > idx_intercept_sum)
				break;
		}
	}

	/*
	 * If there is a latency constraint, it may be necessary to select an
	 * idle state shallower than the current candidate one.
	 */
	if (idx > constraint_idx)
		idx = constraint_idx;

	/*
	 * If either the candidate state is state 0 or its target residency is
	 * low enough, there is basically nothing more to do, but if the sleep
	 * length is not updated, the subsequent wakeup will be counted as an
	 * "intercept" which may be problematic in the cases when timer wakeups
	 * are dominant.  Namely, it may effectively prevent deeper idle states
	 * from being selected at one point even if no imminent timers are
	 * scheduled.
	 *
	 * However, frequent timers in the RESIDENCY_THRESHOLD_NS range on one
	 * CPU are unlikely (user space has a default 50 us slack value for
	 * hrtimers and there are relatively few timers with a lower deadline
	 * value in the kernel), and even if they did happen, the potential
	 * benefit from using a deep idle state in that case would be
	 * questionable anyway for latency reasons.  Thus if the measured idle
	 * duration falls into that range in the majority of cases, assume
	 * non-timer wakeups to be dominant and skip updating the sleep length
	 * to reduce latency.
	 *
	 * Also, if the latency constraint is sufficiently low, it will force
	 * shallow idle states regardless of the wakeup type, so the sleep
	 * length need not be known in that case.
	 */
	if ((!idx || drv->states[idx].target_residency_ns < RESIDENCY_THRESHOLD_NS) &&
	    (2 * cpu_data->short_idles >= cpu_data->total ||
	     latency_req < LATENCY_THRESHOLD_NS))
		goto out_tick;

	duration_ns = tick_nohz_get_sleep_length(&delta_tick);
	cpu_data->sleep_length_ns = duration_ns;

	if (!idx)
		goto out_tick;

	/*
	 * If the closest expected timer is before the target residency of the
	 * candidate state, a shallower one needs to be found.
	 */
	if (drv->states[idx].target_residency_ns > duration_ns)
		idx = teo_find_shallower_state(drv, dev, idx, duration_ns);

	/*
	 * If the selected state's target residency is below the tick length
	 * and intercepts occurring before the tick length are the majority of
	 * total wakeup events, do not stop the tick.
	 */
	if (drv->states[idx].target_residency_ns < TICK_NSEC &&
	    cpu_data->tick_intercepts > cpu_data->total / 2 + cpu_data->total / 8)
		duration_ns = TICK_NSEC / 2;

end:
	/*
	 * Allow the tick to be stopped unless the selected state is a polling
	 * one or the expected idle duration is shorter than the tick period
	 * length.
	 */
	if ((!(drv->states[idx].flags & CPUIDLE_FLAG_POLLING) &&
	    duration_ns >= TICK_NSEC) || tick_nohz_tick_stopped())
		return idx;

	/*
	 * The tick is not going to be stopped, so if the target residency of
	 * the state to be returned is not within the time till the closest
	 * timer including the tick, try to correct that.
	 */
	if (idx > idx0 &&
	    drv->states[idx].target_residency_ns > delta_tick)
		idx = teo_find_shallower_state(drv, dev, idx, delta_tick);

out_tick:
	*stop_tick = false;
	return idx;
}

/**
 * teo_reflect - Note that governor data for the CPU need to be updated.
 * @dev: Target CPU.
 * @state: Entered state.
 */
static void teo_reflect(struct cpuidle_device *dev, int state)
{
	struct teo_cpu *cpu_data = this_cpu_ptr(&teo_cpus);

	cpu_data->tick_wakeup = tick_nohz_idle_got_tick();

	dev->last_state_idx = state;
}

/**
 * teo_enable_device - Initialize the governor's data for the target CPU.
 * @drv: cpuidle driver (not used).
 * @dev: Target CPU.
 */
static int teo_enable_device(struct cpuidle_driver *drv,
			     struct cpuidle_device *dev)
{
	struct teo_cpu *cpu_data = per_cpu_ptr(&teo_cpus, dev->cpu);

	memset(cpu_data, 0, sizeof(*cpu_data));

	return 0;
}

static struct cpuidle_governor teo_governor = {
	.name =		"teo",
	.rating =	19,
	.enable =	teo_enable_device,
	.select =	teo_select,
	.reflect =	teo_reflect,
};

static int __init teo_governor_init(void)
{
	return cpuidle_register_governor(&teo_governor);
}

postcore_initcall(teo_governor_init);
