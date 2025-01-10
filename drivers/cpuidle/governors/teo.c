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
 * greater than the time time till the next timer event, referred as the sleep
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
 * supplied by the driver to the scheduler tick period length or to infinity if
 * the tick period length is less than the target residency of that state.  In
 * the latter case, the governor also counts events with the measured idle
 * duration between the tick period length and the target residency of the
 * deepest idle state.
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
 *    both metrics for the candidate state bin and all subsequent bins(if any),
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
 * @time_span_ns: Time between idle state selection and post-wakeup update.
 * @sleep_length_ns: Time till the closest timer event (at the selection time).
 * @state_bins: Idle state data bins for this CPU.
 * @total: Grand total of the "intercepts" and "hits" metrics for all bins.
 * @tick_hits: Number of "hits" after TICK_NSEC.
 */
struct teo_cpu {
	s64 time_span_ns;
	s64 sleep_length_ns;
	struct teo_bin state_bins[CPUIDLE_STATE_MAX];
	unsigned int total;
	unsigned int tick_hits;
};

static DEFINE_PER_CPU(struct teo_cpu, teo_cpus);

/**
 * teo_update - Update CPU metrics after wakeup.
 * @drv: cpuidle driver containing state data.
 * @dev: Target CPU.
 */
static void teo_update(struct cpuidle_driver *drv, struct cpuidle_device *dev)
{
	struct teo_cpu *cpu_data = per_cpu_ptr(&teo_cpus, dev->cpu);
	int i, idx_timer = 0, idx_duration = 0;
	s64 target_residency_ns;
	u64 measured_ns;

	if (cpu_data->time_span_ns >= cpu_data->sleep_length_ns) {
		/*
		 * One of the safety nets has triggered or the wakeup was close
		 * enough to the closest timer event expected at the idle state
		 * selection time to be discarded.
		 */
		measured_ns = U64_MAX;
	} else {
		u64 lat_ns = drv->states[dev->last_state_idx].exit_latency_ns;

		/*
		 * The computations below are to determine whether or not the
		 * (saved) time till the next timer event and the measured idle
		 * duration fall into the same "bin", so use last_residency_ns
		 * for that instead of time_span_ns which includes the cpuidle
		 * overhead.
		 */
		measured_ns = dev->last_residency_ns;
		/*
		 * The delay between the wakeup and the first instruction
		 * executed by the CPU is not likely to be worst-case every
		 * time, so take 1/2 of the exit latency as a very rough
		 * approximation of the average of it.
		 */
		if (measured_ns >= lat_ns)
			measured_ns -= lat_ns / 2;
		else
			measured_ns /= 2;
	}

	cpu_data->total = 0;

	/*
	 * Decay the "hits" and "intercepts" metrics for all of the bins and
	 * find the bins that the sleep length and the measured idle duration
	 * fall into.
	 */
	for (i = 0; i < drv->state_count; i++) {
		struct teo_bin *bin = &cpu_data->state_bins[i];

		bin->hits -= bin->hits >> DECAY_SHIFT;
		bin->intercepts -= bin->intercepts >> DECAY_SHIFT;

		cpu_data->total += bin->hits + bin->intercepts;

		target_residency_ns = drv->states[i].target_residency_ns;

		if (target_residency_ns <= cpu_data->sleep_length_ns) {
			idx_timer = i;
			if (target_residency_ns <= measured_ns)
				idx_duration = i;
		}
	}

	/*
	 * If the deepest state's target residency is below the tick length,
	 * make a record of it to help teo_select() decide whether or not
	 * to stop the tick.  This effectively adds an extra hits-only bin
	 * beyond the last state-related one.
	 */
	if (target_residency_ns < TICK_NSEC) {
		cpu_data->tick_hits -= cpu_data->tick_hits >> DECAY_SHIFT;

		cpu_data->total += cpu_data->tick_hits;

		if (TICK_NSEC <= cpu_data->sleep_length_ns) {
			idx_timer = drv->state_count;
			if (TICK_NSEC <= measured_ns) {
				cpu_data->tick_hits += PULSE;
				goto end;
			}
		}
	}

	/*
	 * If the measured idle duration falls into the same bin as the sleep
	 * length, this is a "hit", so update the "hits" metric for that bin.
	 * Otherwise, update the "intercepts" metric for the bin fallen into by
	 * the measured idle duration.
	 */
	if (idx_timer == idx_duration)
		cpu_data->state_bins[idx_timer].hits += PULSE;
	else
		cpu_data->state_bins[idx_duration].intercepts += PULSE;

end:
	cpu_data->total += PULSE;
}

static bool teo_state_ok(int i, struct cpuidle_driver *drv)
{
	return !tick_nohz_tick_stopped() ||
		drv->states[i].target_residency_ns >= TICK_NSEC;
}

/**
 * teo_find_shallower_state - Find shallower idle state matching given duration.
 * @drv: cpuidle driver containing state data.
 * @dev: Target CPU.
 * @state_idx: Index of the capping idle state.
 * @duration_ns: Idle duration value to match.
 * @no_poll: Don't consider polling states.
 */
static int teo_find_shallower_state(struct cpuidle_driver *drv,
				    struct cpuidle_device *dev, int state_idx,
				    s64 duration_ns, bool no_poll)
{
	int i;

	for (i = state_idx - 1; i >= 0; i--) {
		if (dev->states_usage[i].disable ||
				(no_poll && drv->states[i].flags & CPUIDLE_FLAG_POLLING))
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
	struct teo_cpu *cpu_data = per_cpu_ptr(&teo_cpus, dev->cpu);
	s64 latency_req = cpuidle_governor_latency_req(dev->cpu);
	ktime_t delta_tick = TICK_NSEC / 2;
	unsigned int tick_intercept_sum = 0;
	unsigned int idx_intercept_sum = 0;
	unsigned int intercept_sum = 0;
	unsigned int idx_hit_sum = 0;
	unsigned int hit_sum = 0;
	int constraint_idx = 0;
	int idx0 = 0, idx = -1;
	int prev_intercept_idx;
	s64 duration_ns;
	int i;

	if (dev->last_state_idx >= 0) {
		teo_update(drv, dev);
		dev->last_state_idx = -1;
	}

	cpu_data->time_span_ns = local_clock();
	/*
	 * Set the expected sleep length to infinity in case of an early
	 * return.
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
		 * Update the sums of idle state mertics for all of the states
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

	tick_intercept_sum = intercept_sum +
			cpu_data->state_bins[drv->state_count-1].intercepts;

	/*
	 * If the sum of the intercepts metric for all of the idle states
	 * shallower than the current candidate one (idx) is greater than the
	 * sum of the intercepts and hits metrics for the candidate state and
	 * all of the deeper states a shallower idle state is likely to be a
	 * better choice.
	 */
	prev_intercept_idx = idx;
	if (2 * idx_intercept_sum > cpu_data->total - idx_hit_sum) {
		int first_suitable_idx = idx;

		/*
		 * Look for the deepest idle state whose target residency had
		 * not exceeded the idle duration in over a half of the relevant
		 * cases in the past.
		 *
		 * Take the possible duration limitation present if the tick
		 * has been stopped already into account.
		 */
		intercept_sum = 0;

		for (i = idx - 1; i >= 0; i--) {
			struct teo_bin *bin = &cpu_data->state_bins[i];

			intercept_sum += bin->intercepts;

			if (2 * intercept_sum > idx_intercept_sum) {
				/*
				 * Use the current state unless it is too
				 * shallow or disabled, in which case take the
				 * first enabled state that is deep enough.
				 */
				if (teo_state_ok(i, drv) &&
				    !dev->states_usage[i].disable)
					idx = i;
				else
					idx = first_suitable_idx;

				break;
			}

			if (dev->states_usage[i].disable)
				continue;

			if (!teo_state_ok(i, drv)) {
				/*
				 * The current state is too shallow, but if an
				 * alternative candidate state has been found,
				 * it may still turn out to be a better choice.
				 */
				if (first_suitable_idx != idx)
					continue;

				break;
			}

			first_suitable_idx = i;
		}
	}
	if (!idx && prev_intercept_idx) {
		/*
		 * We have to query the sleep length here otherwise we don't
		 * know after wakeup if our guess was correct.
		 */
		duration_ns = tick_nohz_get_sleep_length(&delta_tick);
		cpu_data->sleep_length_ns = duration_ns;
		goto out_tick;
	}

	/*
	 * If there is a latency constraint, it may be necessary to select an
	 * idle state shallower than the current candidate one.
	 */
	if (idx > constraint_idx)
		idx = constraint_idx;

	/*
	 * Skip the timers check if state 0 is the current candidate one,
	 * because an immediate non-timer wakeup is expected in that case.
	 */
	if (!idx)
		goto out_tick;

	/*
	 * If state 0 is a polling one, check if the target residency of
	 * the current candidate state is low enough and skip the timers
	 * check in that case too.
	 */
	if ((drv->states[0].flags & CPUIDLE_FLAG_POLLING) &&
	    drv->states[idx].target_residency_ns < RESIDENCY_THRESHOLD_NS)
		goto out_tick;

	duration_ns = tick_nohz_get_sleep_length(&delta_tick);
	cpu_data->sleep_length_ns = duration_ns;

	/*
	 * If the closest expected timer is before the target residency of the
	 * candidate state, a shallower one needs to be found.
	 */
	if (drv->states[idx].target_residency_ns > duration_ns) {
		i = teo_find_shallower_state(drv, dev, idx, duration_ns, false);
		if (teo_state_ok(i, drv))
			idx = i;
	}

	/*
	 * If the selected state's target residency is below the tick length
	 * and intercepts occurring before the tick length are the majority of
	 * total wakeup events, do not stop the tick.
	 */
	if (drv->states[idx].target_residency_ns < TICK_NSEC &&
	    tick_intercept_sum > cpu_data->total / 2 + cpu_data->total / 8)
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
		idx = teo_find_shallower_state(drv, dev, idx, delta_tick, false);

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
	struct teo_cpu *cpu_data = per_cpu_ptr(&teo_cpus, dev->cpu);

	dev->last_state_idx = state;
	/*
	 * If the wakeup was not "natural", but triggered by one of the safety
	 * nets, assume that the CPU might have been idle for the entire sleep
	 * length time.
	 */
	if (dev->poll_time_limit ||
	    (tick_nohz_idle_got_tick() && cpu_data->sleep_length_ns > TICK_NSEC)) {
		dev->poll_time_limit = false;
		cpu_data->time_span_ns = cpu_data->sleep_length_ns;
	} else {
		cpu_data->time_span_ns = local_clock() - cpu_data->time_span_ns;
	}
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
