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
#include <linux/pm_qos.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/tick.h>
#include <linux/sched.h>
#include <linux/math64.h>
#include <linux/module.h>

/*
 * Please note when changing the tuning values:
 * If (MAX_INTERESTING-1) * RESOLUTION > UINT_MAX, the result of
 * a scaling operation multiplication may overflow on 32 bit platforms.
 * In that case, #define RESOLUTION as ULL to get 64 bit result:
 * #define RESOLUTION 1024ULL
 *
 * The default values do not overflow.
 */
#define BUCKETS 12
#define INTERVAL_SHIFT 3
#define INTERVALS (1UL << INTERVAL_SHIFT)
#define RESOLUTION 1024
#define DECAY 8
#define MAX_INTERESTING 50000


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
 * noticeable impact on workloads, which is not acceptable for most sysadmins,
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

	unsigned int	next_timer_us;
	unsigned int	predicted_us;
	unsigned int	bucket;
	unsigned int	correction_factor[BUCKETS];
	unsigned int	intervals[INTERVALS];
	int		interval_ptr;
};


#define LOAD_INT(x) ((x) >> FSHIFT)
#define LOAD_FRAC(x) LOAD_INT(((x) & (FIXED_1-1)) * 100)

static inline int get_loadavg(unsigned long load)
{
	return LOAD_INT(load) * 10 + LOAD_FRAC(load) / 10;
}

static inline int which_bucket(unsigned int duration, unsigned long nr_iowaiters)
{
	int bucket = 0;

	/*
	 * We keep two groups of stats; one with no
	 * IO pending, one without.
	 * This allows us to calculate
	 * E(duration)|iowait
	 */
	if (nr_iowaiters)
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
static inline int performance_multiplier(unsigned long nr_iowaiters, unsigned long load)
{
	int mult = 1;

	/* for higher loadavg, we are more reluctant */

	mult += 2 * get_loadavg(load);

	/* for IO wait tasks (per cpu!) we add 5x each */
	mult += 10 * nr_iowaiters;

	return mult;
}

static DEFINE_PER_CPU(struct menu_device, menu_devices);

static void menu_update(struct cpuidle_driver *drv, struct cpuidle_device *dev);

/*
 * Try detecting repeating patterns by keeping track of the last 8
 * intervals, and checking if the standard deviation of that set
 * of points is below a threshold. If it is... then use the
 * average of these 8 points as the estimated value.
 */
static void get_typical_interval(struct menu_device *data)
{
	int i, divisor;
	unsigned int max, thresh;
	uint64_t avg, stddev;

	thresh = UINT_MAX; /* Discard outliers above this value */

again:

	/* First calculate the average of past intervals */
	max = 0;
	avg = 0;
	divisor = 0;
	for (i = 0; i < INTERVALS; i++) {
		unsigned int value = data->intervals[i];
		if (value <= thresh) {
			avg += value;
			divisor++;
			if (value > max)
				max = value;
		}
	}
	if (divisor == INTERVALS)
		avg >>= INTERVAL_SHIFT;
	else
		do_div(avg, divisor);

	/* Then try to determine standard deviation */
	stddev = 0;
	for (i = 0; i < INTERVALS; i++) {
		unsigned int value = data->intervals[i];
		if (value <= thresh) {
			int64_t diff = value - avg;
			stddev += diff * diff;
		}
	}
	if (divisor == INTERVALS)
		stddev >>= INTERVAL_SHIFT;
	else
		do_div(stddev, divisor);

	/*
	 * The typical interval is obtained when standard deviation is small
	 * or standard deviation is small compared to the average interval.
	 *
	 * int_sqrt() formal parameter type is unsigned long. When the
	 * greatest difference to an outlier exceeds ~65 ms * sqrt(divisor)
	 * the resulting squared standard deviation exceeds the input domain
	 * of int_sqrt on platforms where unsigned long is 32 bits in size.
	 * In such case reject the candidate average.
	 *
	 * Use this result only if there is no timer to wake us up sooner.
	 */
	if (likely(stddev <= ULONG_MAX)) {
		stddev = int_sqrt(stddev);
		if (((avg > stddev * 6) && (divisor * 4 >= INTERVALS * 3))
							|| stddev <= 20) {
			if (data->next_timer_us > avg)
				data->predicted_us = avg;
			return;
		}
	}

	/*
	 * If we have outliers to the upside in our distribution, discard
	 * those by setting the threshold to exclude these outliers, then
	 * calculate the average and standard deviation again. Once we get
	 * down to the bottom 3/4 of our samples, stop excluding samples.
	 *
	 * This can deal with workloads that have long pauses interspersed
	 * with sporadic activity with a bunch of short pauses.
	 */
	if ((divisor * 4) <= INTERVALS * 3)
		return;

	thresh = max - 1;
	goto again;
}

/**
 * menu_select - selects the next idle state to enter
 * @drv: cpuidle driver containing state data
 * @dev: the CPU
 */
static int menu_select(struct cpuidle_driver *drv, struct cpuidle_device *dev)
{
	struct menu_device *data = this_cpu_ptr(&menu_devices);
	int latency_req = pm_qos_request(PM_QOS_CPU_DMA_LATENCY);
	int i;
	unsigned int interactivity_req;
	unsigned long nr_iowaiters, cpu_load;

	if (data->needs_update) {
		menu_update(drv, dev);
		data->needs_update = 0;
	}

	data->last_state_idx = CPUIDLE_DRIVER_STATE_START - 1;

	/* Special case when user has set very strict latency requirement */
	if (unlikely(latency_req == 0))
		return 0;

	/* determine the expected residency time, round up */
	data->next_timer_us = ktime_to_us(tick_nohz_get_sleep_length());

	get_iowait_load(&nr_iowaiters, &cpu_load);
	data->bucket = which_bucket(data->next_timer_us, nr_iowaiters);

	/*
	 * Force the result of multiplication to be 64 bits even if both
	 * operands are 32 bits.
	 * Make sure to round up for half microseconds.
	 */
	data->predicted_us = DIV_ROUND_CLOSEST_ULL((uint64_t)data->next_timer_us *
					 data->correction_factor[data->bucket],
					 RESOLUTION * DECAY);

	get_typical_interval(data);

	/*
	 * Performance multiplier defines a minimum predicted idle
	 * duration / latency ratio. Adjust the latency limit if
	 * necessary.
	 */
	interactivity_req = data->predicted_us / performance_multiplier(nr_iowaiters, cpu_load);
	if (latency_req > interactivity_req)
		latency_req = interactivity_req;

	/*
	 * We want to default to C1 (hlt), not to busy polling
	 * unless the timer is happening really really soon.
	 */
	if (data->next_timer_us > 5 &&
	    !drv->states[CPUIDLE_DRIVER_STATE_START].disabled &&
		dev->states_usage[CPUIDLE_DRIVER_STATE_START].disable == 0)
		data->last_state_idx = CPUIDLE_DRIVER_STATE_START;

	/*
	 * Find the idle state with the lowest power while satisfying
	 * our constraints.
	 */
	for (i = CPUIDLE_DRIVER_STATE_START; i < drv->state_count; i++) {
		struct cpuidle_state *s = &drv->states[i];
		struct cpuidle_state_usage *su = &dev->states_usage[i];

		if (s->disabled || su->disable)
			continue;
		if (s->target_residency > data->predicted_us)
			continue;
		if (s->exit_latency > latency_req)
			continue;

		data->last_state_idx = i;
	}

	return data->last_state_idx;
}

/**
 * menu_reflect - records that data structures need update
 * @dev: the CPU
 * @index: the index of actual entered state
 *
 * NOTE: it's important to be fast here because this operation will add to
 *       the overall exit latency.
 */
static void menu_reflect(struct cpuidle_device *dev, int index)
{
	struct menu_device *data = this_cpu_ptr(&menu_devices);

	data->last_state_idx = index;
	data->needs_update = 1;
}

/**
 * menu_update - attempts to guess what happened after entry
 * @drv: cpuidle driver containing state data
 * @dev: the CPU
 */
static void menu_update(struct cpuidle_driver *drv, struct cpuidle_device *dev)
{
	struct menu_device *data = this_cpu_ptr(&menu_devices);
	int last_idx = data->last_state_idx;
	struct cpuidle_state *target = &drv->states[last_idx];
	unsigned int measured_us;
	unsigned int new_factor;

	/*
	 * Try to figure out how much time passed between entry to low
	 * power state and occurrence of the wakeup event.
	 *
	 * If the entered idle state didn't support residency measurements,
	 * we use them anyway if they are short, and if long,
	 * truncate to the whole expected time.
	 *
	 * Any measured amount of time will include the exit latency.
	 * Since we are interested in when the wakeup begun, not when it
	 * was completed, we must subtract the exit latency. However, if
	 * the measured amount of time is less than the exit latency,
	 * assume the state was never reached and the exit latency is 0.
	 */

	/* measured value */
	measured_us = cpuidle_get_last_residency(dev);

	/* Deduct exit latency */
	if (measured_us > target->exit_latency)
		measured_us -= target->exit_latency;

	/* Make sure our coefficients do not exceed unity */
	if (measured_us > data->next_timer_us)
		measured_us = data->next_timer_us;

	/* Update our correction ratio */
	new_factor = data->correction_factor[data->bucket];
	new_factor -= new_factor / DECAY;

	if (data->next_timer_us > 0 && measured_us < MAX_INTERESTING)
		new_factor += RESOLUTION * measured_us / data->next_timer_us;
	else
		/*
		 * we were idle so long that we count it as a perfect
		 * prediction
		 */
		new_factor += RESOLUTION;

	/*
	 * We don't want 0 as factor; we always want at least
	 * a tiny bit of estimated time. Fortunately, due to rounding,
	 * new_factor will stay nonzero regardless of measured_us values
	 * and the compiler can eliminate this test as long as DECAY > 1.
	 */
	if (DECAY == 1 && unlikely(new_factor == 0))
		new_factor = 1;

	data->correction_factor[data->bucket] = new_factor;

	/* update the repeating-pattern data */
	data->intervals[data->interval_ptr++] = measured_us;
	if (data->interval_ptr >= INTERVALS)
		data->interval_ptr = 0;
}

/**
 * menu_enable_device - scans a CPU's states and does setup
 * @drv: cpuidle driver
 * @dev: the CPU
 */
static int menu_enable_device(struct cpuidle_driver *drv,
				struct cpuidle_device *dev)
{
	struct menu_device *data = &per_cpu(menu_devices, dev->cpu);
	int i;

	memset(data, 0, sizeof(struct menu_device));

	/*
	 * if the correction factor is 0 (eg first time init or cpu hotplug
	 * etc), we actually want to start out with a unity factor.
	 */
	for(i = 0; i < BUCKETS; i++)
		data->correction_factor[i] = RESOLUTION * DECAY;

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

postcore_initcall(init_menu);
