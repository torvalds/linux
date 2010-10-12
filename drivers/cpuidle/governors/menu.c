/*
 * menu.c - the menu idle governor
 *
 * Copyright (C) 2006-2007 Adam Belay <abelay@novell.com>
 * Copyright (C) 2009 Intel Corporation
 * Author:
 *        Arjan van de Ven <arjan@linux.intel.com>
 *
 * This code is licenced under the GPL version 2 as described
 * in the COPYING file that acompanies the Linux Kernel.
 */

#include <linux/kernel.h>
#include <linux/cpuidle.h>
#include <linux/pm_qos_params.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/tick.h>
#include <linux/sched.h>
#include <linux/math64.h>

#define BUCKETS 12
#define INTERVALS 8
#define RESOLUTION 1024
#define DECAY 8
#define MAX_INTERESTING 50000
#define STDDEV_THRESH 400


/*
 * Concepts and ideas behind the menu governor
 *
 * For the menu governor, there are 3 decision factors for picking a C
 * state:
 * 1) Energy break even point
 * 2) Performance impact
 * 3) Latency tolerance (from pmqos infrastructure)
 * These these three factors are treated independently.
 *
 * Energy break even point
 * -----------------------
 * C state entry and exit have an energy cost, and a certain amount of time in
 * the  C state is required to actually break even on this cost. CPUIDLE
 * provides us this duration in the "target_residency" field. So all that we
 * need is a good prediction of how long we'll be idle. Like the traditional
 * menu governor, we start with the actual known "next timer event" time.
 *
 * Since there are other source of wakeups (interrupts for example) than
 * the next timer event, this estimation is rather optimistic. To get a
 * more realistic estimate, a correction factor is applied to the estimate,
 * that is based on historic behavior. For example, if in the past the actual
 * duration always was 50% of the next timer tick, the correction factor will
 * be 0.5.
 *
 * menu uses a running average for this correction factor, however it uses a
 * set of factors, not just a single factor. This stems from the realization
 * that the ratio is dependent on the order of magnitude of the expected
 * duration; if we expect 500 milliseconds of idle time the likelihood of
 * getting an interrupt very early is much higher than if we expect 50 micro
 * seconds of idle time. A second independent factor that has big impact on
 * the actual factor is if there is (disk) IO outstanding or not.
 * (as a special twist, we consider every sleep longer than 50 milliseconds
 * as perfect; there are no power gains for sleeping longer than this)
 *
 * For these two reasons we keep an array of 12 independent factors, that gets
 * indexed based on the magnitude of the expected duration as well as the
 * "is IO outstanding" property.
 *
 * Repeatable-interval-detector
 * ----------------------------
 * There are some cases where "next timer" is a completely unusable predictor:
 * Those cases where the interval is fixed, for example due to hardware
 * interrupt mitigation, but also due to fixed transfer rate devices such as
 * mice.
 * For this, we use a different predictor: We track the duration of the last 8
 * intervals and if the stand deviation of these 8 intervals is below a
 * threshold value, we use the average of these intervals as prediction.
 *
 * Limiting Performance Impact
 * ---------------------------
 * C states, especially those with large exit latencies, can have a real
 * noticable impact on workloads, which is not acceptable for most sysadmins,
 * and in addition, less performance has a power price of its own.
 *
 * As a general rule of thumb, menu assumes that the following heuristic
 * holds:
 *     The busier the system, the less impact of C states is acceptable
 *
 * This rule-of-thumb is implemented using a performance-multiplier:
 * If the exit latency times the performance multiplier is longer than
 * the predicted duration, the C state is not considered a candidate
 * for selection due to a too high performance impact. So the higher
 * this multiplier is, the longer we need to be idle to pick a deep C
 * state, and thus the less likely a busy CPU will hit such a deep
 * C state.
 *
 * Two factors are used in determing this multiplier:
 * a value of 10 is added for each point of "per cpu load average" we have.
 * a value of 5 points is added for each process that is waiting for
 * IO on this CPU.
 * (these values are experimentally determined)
 *
 * The load average factor gives a longer term (few seconds) input to the
 * decision, while the iowait value gives a cpu local instantanious input.
 * The iowait factor may look low, but realize that this is also already
 * represented in the system load average.
 *
 */

struct menu_device {
	int		last_state_idx;
	int             needs_update;

	unsigned int	expected_us;
	u64		predicted_us;
	unsigned int	exit_us;
	unsigned int	bucket;
	u64		correction_factor[BUCKETS];
	u32		intervals[INTERVALS];
	int		interval_ptr;
};


#define LOAD_INT(x) ((x) >> FSHIFT)
#define LOAD_FRAC(x) LOAD_INT(((x) & (FIXED_1-1)) * 100)

static int get_loadavg(void)
{
	unsigned long this = this_cpu_load();


	return LOAD_INT(this) * 10 + LOAD_FRAC(this) / 10;
}

static inline int which_bucket(unsigned int duration)
{
	int bucket = 0;

	/*
	 * We keep two groups of stats; one with no
	 * IO pending, one without.
	 * This allows us to calculate
	 * E(duration)|iowait
	 */
	if (nr_iowait_cpu(smp_processor_id()))
		bucket = BUCKETS/2;

	if (duration < 10)
		return bucket;
	if (duration < 100)
		return bucket + 1;
	if (duration < 1000)
		return bucket + 2;
	if (duration < 10000)
		return bucket + 3;
	if (duration < 100000)
		return bucket + 4;
	return bucket + 5;
}

/*
 * Return a multiplier for the exit latency that is intended
 * to take performance requirements into account.
 * The more performance critical we estimate the system
 * to be, the higher this multiplier, and thus the higher
 * the barrier to go to an expensive C state.
 */
static inline int performance_multiplier(void)
{
	int mult = 1;

	/* for higher loadavg, we are more reluctant */

	mult += 2 * get_loadavg();

	/* for IO wait tasks (per cpu!) we add 5x each */
	mult += 10 * nr_iowait_cpu(smp_processor_id());

	return mult;
}

static DEFINE_PER_CPU(struct menu_device, menu_devices);

static void menu_update(struct cpuidle_device *dev);

/* This implements DIV_ROUND_CLOSEST but avoids 64 bit division */
static u64 div_round64(u64 dividend, u32 divisor)
{
	return div_u64(dividend + (divisor / 2), divisor);
}

/*
 * Try detecting repeating patterns by keeping track of the last 8
 * intervals, and checking if the standard deviation of that set
 * of points is below a threshold. If it is... then use the
 * average of these 8 points as the estimated value.
 */
static void detect_repeating_patterns(struct menu_device *data)
{
	int i;
	uint64_t avg = 0;
	uint64_t stddev = 0; /* contains the square of the std deviation */

	/* first calculate average and standard deviation of the past */
	for (i = 0; i < INTERVALS; i++)
		avg += data->intervals[i];
	avg = avg / INTERVALS;

	/* if the avg is beyond the known next tick, it's worthless */
	if (avg > data->expected_us)
		return;

	for (i = 0; i < INTERVALS; i++)
		stddev += (data->intervals[i] - avg) *
			  (data->intervals[i] - avg);

	stddev = stddev / INTERVALS;

	/*
	 * now.. if stddev is small.. then assume we have a
	 * repeating pattern and predict we keep doing this.
	 */

	if (avg && stddev < STDDEV_THRESH)
		data->predicted_us = avg;
}

/**
 * menu_select - selects the next idle state to enter
 * @dev: the CPU
 */
static int menu_select(struct cpuidle_device *dev)
{
	struct menu_device *data = &__get_cpu_var(menu_devices);
	int latency_req = pm_qos_request(PM_QOS_CPU_DMA_LATENCY);
	unsigned int power_usage = -1;
	int i;
	int multiplier;

	if (data->needs_update) {
		menu_update(dev);
		data->needs_update = 0;
	}

	data->last_state_idx = 0;
	data->exit_us = 0;

	/* Special case when user has set very strict latency requirement */
	if (unlikely(latency_req == 0))
		return 0;

	/* determine the expected residency time, round up */
	data->expected_us =
	    DIV_ROUND_UP((u32)ktime_to_ns(tick_nohz_get_sleep_length()), 1000);


	data->bucket = which_bucket(data->expected_us);

	multiplier = performance_multiplier();

	/*
	 * if the correction factor is 0 (eg first time init or cpu hotplug
	 * etc), we actually want to start out with a unity factor.
	 */
	if (data->correction_factor[data->bucket] == 0)
		data->correction_factor[data->bucket] = RESOLUTION * DECAY;

	/* Make sure to round up for half microseconds */
	data->predicted_us = div_round64(data->expected_us * data->correction_factor[data->bucket],
					 RESOLUTION * DECAY);

	detect_repeating_patterns(data);

	/*
	 * We want to default to C1 (hlt), not to busy polling
	 * unless the timer is happening really really soon.
	 */
	if (data->expected_us > 5)
		data->last_state_idx = CPUIDLE_DRIVER_STATE_START;

	/*
	 * Find the idle state with the lowest power while satisfying
	 * our constraints.
	 */
	for (i = CPUIDLE_DRIVER_STATE_START; i < dev->state_count; i++) {
		struct cpuidle_state *s = &dev->states[i];

		if (s->flags & CPUIDLE_FLAG_IGNORE)
			continue;
		if (s->target_residency > data->predicted_us)
			continue;
		if (s->exit_latency > latency_req)
			continue;
		if (s->exit_latency * multiplier > data->predicted_us)
			continue;

		if (s->power_usage < power_usage) {
			power_usage = s->power_usage;
			data->last_state_idx = i;
			data->exit_us = s->exit_latency;
		}
	}

	return data->last_state_idx;
}

/**
 * menu_reflect - records that data structures need update
 * @dev: the CPU
 *
 * NOTE: it's important to be fast here because this operation will add to
 *       the overall exit latency.
 */
static void menu_reflect(struct cpuidle_device *dev)
{
	struct menu_device *data = &__get_cpu_var(menu_devices);
	data->needs_update = 1;
}

/**
 * menu_update - attempts to guess what happened after entry
 * @dev: the CPU
 */
static void menu_update(struct cpuidle_device *dev)
{
	struct menu_device *data = &__get_cpu_var(menu_devices);
	int last_idx = data->last_state_idx;
	unsigned int last_idle_us = cpuidle_get_last_residency(dev);
	struct cpuidle_state *target = &dev->states[last_idx];
	unsigned int measured_us;
	u64 new_factor;

	/*
	 * Ugh, this idle state doesn't support residency measurements, so we
	 * are basically lost in the dark.  As a compromise, assume we slept
	 * for the whole expected time.
	 */
	if (unlikely(!(target->flags & CPUIDLE_FLAG_TIME_VALID)))
		last_idle_us = data->expected_us;


	measured_us = last_idle_us;

	/*
	 * We correct for the exit latency; we are assuming here that the
	 * exit latency happens after the event that we're interested in.
	 */
	if (measured_us > data->exit_us)
		measured_us -= data->exit_us;


	/* update our correction ratio */

	new_factor = data->correction_factor[data->bucket]
			* (DECAY - 1) / DECAY;

	if (data->expected_us > 0 && measured_us < MAX_INTERESTING)
		new_factor += RESOLUTION * measured_us / data->expected_us;
	else
		/*
		 * we were idle so long that we count it as a perfect
		 * prediction
		 */
		new_factor += RESOLUTION;

	/*
	 * We don't want 0 as factor; we always want at least
	 * a tiny bit of estimated time.
	 */
	if (new_factor == 0)
		new_factor = 1;

	data->correction_factor[data->bucket] = new_factor;

	/* update the repeating-pattern data */
	data->intervals[data->interval_ptr++] = last_idle_us;
	if (data->interval_ptr >= INTERVALS)
		data->interval_ptr = 0;
}

/**
 * menu_enable_device - scans a CPU's states and does setup
 * @dev: the CPU
 */
static int menu_enable_device(struct cpuidle_device *dev)
{
	struct menu_device *data = &per_cpu(menu_devices, dev->cpu);

	memset(data, 0, sizeof(struct menu_device));

	return 0;
}

static struct cpuidle_governor menu_governor = {
	.name =		"menu",
	.rating =	20,
	.enable =	menu_enable_device,
	.select =	menu_select,
	.reflect =	menu_reflect,
	.owner =	THIS_MODULE,
};

/**
 * init_menu - initializes the governor
 */
static int __init init_menu(void)
{
	return cpuidle_register_governor(&menu_governor);
}

/**
 * exit_menu - exits the governor
 */
static void __exit exit_menu(void)
{
	cpuidle_unregister_governor(&menu_governor);
}

MODULE_LICENSE("GPL");
module_init(init_menu);
module_exit(exit_menu);
