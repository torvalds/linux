// SPDX-License-Identifier: GPL-2.0
/*
 * Timer events oriented CPU idle governor
 *
 * Copyright (C) 2018 Intel Corporation
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * The idea of this governor is based on the observation that on many systems
 * timer events are two or more orders of magnitude more frequent than any
 * other interrupts, so they are likely to be the most significant source of CPU
 * wakeups from idle states.  Moreover, information about what happened in the
 * (relatively recent) past can be used to estimate whether or not the deepest
 * idle state with target residency within the time to the closest timer is
 * likely to be suitable for the upcoming idle time of the CPU and, if not, then
 * which of the shallower idle states to choose.
 *
 * Of course, non-timer wakeup sources are more important in some use cases and
 * they can be covered by taking a few most recent idle time intervals of the
 * CPU into account.  However, even in that case it is not necessary to consider
 * idle duration values greater than the time till the closest timer, as the
 * patterns that they may belong to produce average values close enough to
 * the time till the closest timer (sleep length) anyway.
 *
 * Thus this governor estimates whether or not the upcoming idle time of the CPU
 * is likely to be significantly shorter than the sleep length and selects an
 * idle state for it in accordance with that, as follows:
 *
 * - Find an idle state on the basis of the sleep length and state statistics
 *   collected over time:
 *
 *   o Find the deepest idle state whose target residency is less than or equal
 *     to the sleep length.
 *
 *   o Select it if it matched both the sleep length and the observed idle
 *     duration in the past more often than it matched the sleep length alone
 *     (i.e. the observed idle duration was significantly shorter than the sleep
 *     length matched by it).
 *
 *   o Otherwise, select the shallower state with the greatest matched "early"
 *     wakeups metric.
 *
 * - If the majority of the most recent idle duration values are below the
 *   target residency of the idle state selected so far, use those values to
 *   compute the new expected idle duration and find an idle state matching it
 *   (which has to be shallower than the one selected so far).
 */

#include <linux/cpuidle.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/sched/clock.h>
#include <linux/tick.h>

/*
 * The PULSE value is added to metrics when they grow and the DECAY_SHIFT value
 * is used for decreasing metrics on a regular basis.
 */
#define PULSE		1024
#define DECAY_SHIFT	3

/*
 * Number of the most recent idle duration values to take into consideration for
 * the detection of wakeup patterns.
 */
#define INTERVALS	8

/**
 * struct teo_idle_state - Idle state data used by the TEO cpuidle governor.
 * @early_hits: "Early" CPU wakeups "matching" this state.
 * @hits: "On time" CPU wakeups "matching" this state.
 * @misses: CPU wakeups "missing" this state.
 *
 * A CPU wakeup is "matched" by a given idle state if the idle duration measured
 * after the wakeup is between the target residency of that state and the target
 * residency of the next one (or if this is the deepest available idle state, it
 * "matches" a CPU wakeup when the measured idle duration is at least equal to
 * its target residency).
 *
 * Also, from the TEO governor perspective, a CPU wakeup from idle is "early" if
 * it occurs significantly earlier than the closest expected timer event (that
 * is, early enough to match an idle state shallower than the one matching the
 * time till the closest timer event).  Otherwise, the wakeup is "on time", or
 * it is a "hit".
 *
 * A "miss" occurs when the given state doesn't match the wakeup, but it matches
 * the time till the closest timer event used for idle state selection.
 */
struct teo_idle_state {
	unsigned int early_hits;
	unsigned int hits;
	unsigned int misses;
};

/**
 * struct teo_cpu - CPU data used by the TEO cpuidle governor.
 * @time_span_ns: Time between idle state selection and post-wakeup update.
 * @sleep_length_ns: Time till the closest timer event (at the selection time).
 * @states: Idle states data corresponding to this CPU.
 * @interval_idx: Index of the most recent saved idle interval.
 * @intervals: Saved idle duration values.
 */
struct teo_cpu {
	u64 time_span_ns;
	u64 sleep_length_ns;
	struct teo_idle_state states[CPUIDLE_STATE_MAX];
	int interval_idx;
	u64 intervals[INTERVALS];
};

static DEFINE_PER_CPU(struct teo_cpu, teo_cpus);

/**
 * teo_update - Update CPU data after wakeup.
 * @drv: cpuidle driver containing state data.
 * @dev: Target CPU.
 */
static void teo_update(struct cpuidle_driver *drv, struct cpuidle_device *dev)
{
	struct teo_cpu *cpu_data = per_cpu_ptr(&teo_cpus, dev->cpu);
	int i, idx_hit = -1, idx_timer = -1;
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

	/*
	 * Decay the "early hits" metric for all of the states and find the
	 * states matching the sleep length and the measured idle duration.
	 */
	for (i = 0; i < drv->state_count; i++) {
		unsigned int early_hits = cpu_data->states[i].early_hits;

		cpu_data->states[i].early_hits -= early_hits >> DECAY_SHIFT;

		if (drv->states[i].target_residency_ns <= cpu_data->sleep_length_ns) {
			idx_timer = i;
			if (drv->states[i].target_residency_ns <= measured_ns)
				idx_hit = i;
		}
	}

	/*
	 * Update the "hits" and "misses" data for the state matching the sleep
	 * length.  If it matches the measured idle duration too, this is a hit,
	 * so increase the "hits" metric for it then.  Otherwise, this is a
	 * miss, so increase the "misses" metric for it.  In the latter case
	 * also increase the "early hits" metric for the state that actually
	 * matches the measured idle duration.
	 */
	if (idx_timer >= 0) {
		unsigned int hits = cpu_data->states[idx_timer].hits;
		unsigned int misses = cpu_data->states[idx_timer].misses;

		hits -= hits >> DECAY_SHIFT;
		misses -= misses >> DECAY_SHIFT;

		if (idx_timer > idx_hit) {
			misses += PULSE;
			if (idx_hit >= 0)
				cpu_data->states[idx_hit].early_hits += PULSE;
		} else {
			hits += PULSE;
		}

		cpu_data->states[idx_timer].misses = misses;
		cpu_data->states[idx_timer].hits = hits;
	}

	/*
	 * Save idle duration values corresponding to non-timer wakeups for
	 * pattern detection.
	 */
	cpu_data->intervals[cpu_data->interval_idx++] = measured_ns;
	if (cpu_data->interval_idx >= INTERVALS)
		cpu_data->interval_idx = 0;
}

static bool teo_time_ok(u64 interval_ns)
{
	return !tick_nohz_tick_stopped() || interval_ns >= TICK_NSEC;
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
				    u64 duration_ns)
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
	struct teo_cpu *cpu_data = per_cpu_ptr(&teo_cpus, dev->cpu);
	s64 latency_req = cpuidle_governor_latency_req(dev->cpu);
	u64 duration_ns;
	unsigned int hits, misses, early_hits;
	int max_early_idx, prev_max_early_idx, constraint_idx, idx, i;
	ktime_t delta_tick;

	if (dev->last_state_idx >= 0) {
		teo_update(drv, dev);
		dev->last_state_idx = -1;
	}

	cpu_data->time_span_ns = local_clock();

	duration_ns = tick_nohz_get_sleep_length(&delta_tick);
	cpu_data->sleep_length_ns = duration_ns;

	hits = 0;
	misses = 0;
	early_hits = 0;
	max_early_idx = -1;
	prev_max_early_idx = -1;
	constraint_idx = drv->state_count;
	idx = -1;

	for (i = 0; i < drv->state_count; i++) {
		struct cpuidle_state *s = &drv->states[i];

		if (dev->states_usage[i].disable) {
			/*
			 * Ignore disabled states with target residencies beyond
			 * the anticipated idle duration.
			 */
			if (s->target_residency_ns > duration_ns)
				continue;

			/*
			 * This state is disabled, so the range of idle duration
			 * values corresponding to it is covered by the current
			 * candidate state, but still the "hits" and "misses"
			 * metrics of the disabled state need to be used to
			 * decide whether or not the state covering the range in
			 * question is good enough.
			 */
			hits = cpu_data->states[i].hits;
			misses = cpu_data->states[i].misses;

			if (early_hits >= cpu_data->states[i].early_hits ||
			    idx < 0)
				continue;

			/*
			 * If the current candidate state has been the one with
			 * the maximum "early hits" metric so far, the "early
			 * hits" metric of the disabled state replaces the
			 * current "early hits" count to avoid selecting a
			 * deeper state with lower "early hits" metric.
			 */
			if (max_early_idx == idx) {
				early_hits = cpu_data->states[i].early_hits;
				continue;
			}

			/*
			 * The current candidate state is closer to the disabled
			 * one than the current maximum "early hits" state, so
			 * replace the latter with it, but in case the maximum
			 * "early hits" state index has not been set so far,
			 * check if the current candidate state is not too
			 * shallow for that role.
			 */
			if (teo_time_ok(drv->states[idx].target_residency_ns)) {
				prev_max_early_idx = max_early_idx;
				early_hits = cpu_data->states[i].early_hits;
				max_early_idx = idx;
			}

			continue;
		}

		if (idx < 0) {
			idx = i; /* first enabled state */
			hits = cpu_data->states[i].hits;
			misses = cpu_data->states[i].misses;
		}

		if (s->target_residency_ns > duration_ns)
			break;

		if (s->exit_latency_ns > latency_req && constraint_idx > i)
			constraint_idx = i;

		idx = i;
		hits = cpu_data->states[i].hits;
		misses = cpu_data->states[i].misses;

		if (early_hits < cpu_data->states[i].early_hits &&
		    teo_time_ok(drv->states[i].target_residency_ns)) {
			prev_max_early_idx = max_early_idx;
			early_hits = cpu_data->states[i].early_hits;
			max_early_idx = i;
		}
	}

	/*
	 * If the "hits" metric of the idle state matching the sleep length is
	 * greater than its "misses" metric, that is the one to use.  Otherwise,
	 * it is more likely that one of the shallower states will match the
	 * idle duration observed after wakeup, so take the one with the maximum
	 * "early hits" metric, but if that cannot be determined, just use the
	 * state selected so far.
	 */
	if (hits <= misses) {
		/*
		 * The current candidate state is not suitable, so take the one
		 * whose "early hits" metric is the maximum for the range of
		 * shallower states.
		 */
		if (idx == max_early_idx)
			max_early_idx = prev_max_early_idx;

		if (max_early_idx >= 0) {
			idx = max_early_idx;
			duration_ns = drv->states[idx].target_residency_ns;
		}
	}

	/*
	 * If there is a latency constraint, it may be necessary to use a
	 * shallower idle state than the one selected so far.
	 */
	if (constraint_idx < idx)
		idx = constraint_idx;

	if (idx < 0) {
		idx = 0; /* No states enabled. Must use 0. */
	} else if (idx > 0) {
		unsigned int count = 0;
		u64 sum = 0;

		/*
		 * Count and sum the most recent idle duration values less than
		 * the current expected idle duration value.
		 */
		for (i = 0; i < INTERVALS; i++) {
			u64 val = cpu_data->intervals[i];

			if (val >= duration_ns)
				continue;

			count++;
			sum += val;
		}

		/*
		 * Give up unless the majority of the most recent idle duration
		 * values are in the interesting range.
		 */
		if (count > INTERVALS / 2) {
			u64 avg_ns = div64_u64(sum, count);

			/*
			 * Avoid spending too much time in an idle state that
			 * would be too shallow.
			 */
			if (teo_time_ok(avg_ns)) {
				duration_ns = avg_ns;
				if (drv->states[idx].target_residency_ns > avg_ns)
					idx = teo_find_shallower_state(drv, dev,
								       idx, avg_ns);
			}
		}
	}

	/*
	 * Don't stop the tick if the selected state is a polling one or if the
	 * expected idle duration is shorter than the tick period length.
	 */
	if (((drv->states[idx].flags & CPUIDLE_FLAG_POLLING) ||
	    duration_ns < TICK_NSEC) && !tick_nohz_tick_stopped()) {
		*stop_tick = false;

		/*
		 * The tick is not going to be stopped, so if the target
		 * residency of the state to be returned is not within the time
		 * till the closest timer including the tick, try to correct
		 * that.
		 */
		if (idx > 0 && drv->states[idx].target_residency_ns > delta_tick)
			idx = teo_find_shallower_state(drv, dev, idx, delta_tick);
	}

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
	int i;

	memset(cpu_data, 0, sizeof(*cpu_data));

	for (i = 0; i < INTERVALS; i++)
		cpu_data->intervals[i] = U64_MAX;

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
