/**
 * Copyright (C) ARM Limited 2011-2013. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include "gator.h"

#include <linux/module.h>
#include <linux/time.h>
#include <linux/math64.h>
#include <linux/slab.h>
#include <asm/io.h>

#include "linux/mali_linux_trace.h"

#include "gator_events_mali_common.h"

/*
 * Check that the MALI_SUPPORT define is set to one of the allowable device codes.
 */
#if (MALI_SUPPORT != MALI_T6xx)
#error MALI_SUPPORT set to an invalid device code: expecting MALI_T6xx
#endif

/* Counters for Mali-T6xx:
 *
 *  - Timeline events
 *    They are tracepoints, but instead of reporting a number they report a START/STOP event.
 *    They are reported in Streamline as number of microseconds while that particular counter was active.
 *
 *  - SW counters
 *    They are tracepoints reporting a particular number.
 *    They are accumulated in sw_counter_data array until they are passed to Streamline, then they are zeroed.
 *
 *  - Accumulators
 *    They are the same as software counters but their value is not zeroed.
 */

/* Timeline (start/stop) activity */
static const char *timeline_event_names[] = {
	"PM_SHADER_0",
	"PM_SHADER_1",
	"PM_SHADER_2",
	"PM_SHADER_3",
	"PM_SHADER_4",
	"PM_SHADER_5",
	"PM_SHADER_6",
	"PM_SHADER_7",
	"PM_TILER_0",
	"PM_L2_0",
	"PM_L2_1",
	"MMU_AS_0",
	"MMU_AS_1",
	"MMU_AS_2",
	"MMU_AS_3"
};

enum {
	PM_SHADER_0 = 0,
	PM_SHADER_1,
	PM_SHADER_2,
	PM_SHADER_3,
	PM_SHADER_4,
	PM_SHADER_5,
	PM_SHADER_6,
	PM_SHADER_7,
	PM_TILER_0,
	PM_L2_0,
	PM_L2_1,
	MMU_AS_0,
	MMU_AS_1,
	MMU_AS_2,
	MMU_AS_3
};
/* The number of shader blocks in the enum above */
#define NUM_PM_SHADER (8)

/* Software Counters */
static const char *software_counter_names[] = {
	"MMU_PAGE_FAULT_0",
	"MMU_PAGE_FAULT_1",
	"MMU_PAGE_FAULT_2",
	"MMU_PAGE_FAULT_3"
};

enum {
	MMU_PAGE_FAULT_0 = 0,
	MMU_PAGE_FAULT_1,
	MMU_PAGE_FAULT_2,
	MMU_PAGE_FAULT_3
};

/* Software Counters */
static const char *accumulators_names[] = {
	"TOTAL_ALLOC_PAGES"
};

enum {
	TOTAL_ALLOC_PAGES = 0
};

#define FIRST_TIMELINE_EVENT (0)
#define NUMBER_OF_TIMELINE_EVENTS (sizeof(timeline_event_names) / sizeof(timeline_event_names[0]))
#define FIRST_SOFTWARE_COUNTER (FIRST_TIMELINE_EVENT + NUMBER_OF_TIMELINE_EVENTS)
#define NUMBER_OF_SOFTWARE_COUNTERS (sizeof(software_counter_names) / sizeof(software_counter_names[0]))
#define FIRST_ACCUMULATOR (FIRST_SOFTWARE_COUNTER + NUMBER_OF_SOFTWARE_COUNTERS)
#define NUMBER_OF_ACCUMULATORS (sizeof(accumulators_names) / sizeof(accumulators_names[0]))
#define NUMBER_OF_EVENTS (NUMBER_OF_TIMELINE_EVENTS + NUMBER_OF_SOFTWARE_COUNTERS + NUMBER_OF_ACCUMULATORS)

/*
 * gatorfs variables for counter enable state
 */
static mali_counter counters[NUMBER_OF_EVENTS];

/* An array used to return the data we recorded
 * as key,value pairs hence the *2
 */
static unsigned long counter_dump[NUMBER_OF_EVENTS * 2];

/*
 * Array holding counter start times (in ns) for each counter.  A zero here
 * indicates that the activity monitored by this counter is not running.
 */
static struct timespec timeline_event_starttime[NUMBER_OF_TIMELINE_EVENTS];

/* The data we have recorded */
static unsigned int timeline_data[NUMBER_OF_TIMELINE_EVENTS];
static unsigned int sw_counter_data[NUMBER_OF_SOFTWARE_COUNTERS];
static unsigned int accumulators_data[NUMBER_OF_ACCUMULATORS];

/* Hold the previous timestamp, used to calculate the sample interval. */
static struct timespec prev_timestamp;

/**
 * Returns the timespan (in microseconds) between the two specified timestamps.
 *
 * @param start Ptr to the start timestamp
 * @param end Ptr to the end timestamp
 *
 * @return Number of microseconds between the two timestamps (can be negative if start follows end).
 */
static inline long get_duration_us(const struct timespec *start, const struct timespec *end)
{
	long event_duration_us = (end->tv_nsec - start->tv_nsec) / 1000;
	event_duration_us += (end->tv_sec - start->tv_sec) * 1000000;

	return event_duration_us;
}

static void record_timeline_event(unsigned int timeline_index, unsigned int type)
{
	struct timespec event_timestamp;
	struct timespec *event_start = &timeline_event_starttime[timeline_index];

	switch (type) {
	case ACTIVITY_START:
		/* Get the event time... */
		getnstimeofday(&event_timestamp);

		/* Remember the start time if the activity is not already started */
		if (event_start->tv_sec == 0) {
			*event_start = event_timestamp;	/* Structure copy */
		}
		break;

	case ACTIVITY_STOP:
		/* if the counter was started... */
		if (event_start->tv_sec != 0) {
			/* Get the event time... */
			getnstimeofday(&event_timestamp);

			/* Accumulate the duration in us */
			timeline_data[timeline_index] += get_duration_us(event_start, &event_timestamp);

			/* Reset the start time to indicate the activity is stopped. */
			event_start->tv_sec = 0;
		}
		break;

	default:
		/* Other activity events are ignored. */
		break;
	}
}

/*
 * Documentation about the following tracepoints is in mali_linux_trace.h
 */

GATOR_DEFINE_PROBE(mali_pm_status, TP_PROTO(unsigned int event_id, unsigned long long value))
{
#define SHADER_PRESENT_LO       0x100	/* (RO) Shader core present bitmap, low word */
#define TILER_PRESENT_LO        0x110	/* (RO) Tiler core present bitmap, low word */
#define L2_PRESENT_LO           0x120	/* (RO) Level 2 cache present bitmap, low word */
#define BIT_AT(value, pos) ((value >> pos) & 1)

	static unsigned long long previous_shader_bitmask = 0;
	static unsigned long long previous_tiler_bitmask = 0;
	static unsigned long long previous_l2_bitmask = 0;

	switch (event_id) {
	case SHADER_PRESENT_LO:
		{
			unsigned long long changed_bitmask = previous_shader_bitmask ^ value;
			int pos;

			for (pos = 0; pos < NUM_PM_SHADER; ++pos) {
				if (BIT_AT(changed_bitmask, pos)) {
					record_timeline_event(PM_SHADER_0 + pos, BIT_AT(value, pos) ? ACTIVITY_START : ACTIVITY_STOP);
				}
			}

			previous_shader_bitmask = value;
			break;
		}

	case TILER_PRESENT_LO:
		{
			unsigned long long changed = previous_tiler_bitmask ^ value;

			if (BIT_AT(changed, 0)) {
				record_timeline_event(PM_TILER_0, BIT_AT(value, 0) ? ACTIVITY_START : ACTIVITY_STOP);
			}

			previous_tiler_bitmask = value;
			break;
		}

	case L2_PRESENT_LO:
		{
			unsigned long long changed = previous_l2_bitmask ^ value;

			if (BIT_AT(changed, 0)) {
				record_timeline_event(PM_L2_0, BIT_AT(value, 0) ? ACTIVITY_START : ACTIVITY_STOP);
			}
			if (BIT_AT(changed, 4)) {
				record_timeline_event(PM_L2_1, BIT_AT(value, 4) ? ACTIVITY_START : ACTIVITY_STOP);
			}

			previous_l2_bitmask = value;
			break;
		}

	default:
		/* No other blocks are supported at present */
		break;
	}

#undef SHADER_PRESENT_LO
#undef TILER_PRESENT_LO
#undef L2_PRESENT_LO
#undef BIT_AT
}

GATOR_DEFINE_PROBE(mali_page_fault_insert_pages, TP_PROTO(int event_id, unsigned long value))
{
	/* We add to the previous since we may receive many tracepoints in one sample period */
	sw_counter_data[MMU_PAGE_FAULT_0 + event_id] += value;
}

GATOR_DEFINE_PROBE(mali_mmu_as_in_use, TP_PROTO(int event_id))
{
	record_timeline_event(MMU_AS_0 + event_id, ACTIVITY_START);
}

GATOR_DEFINE_PROBE(mali_mmu_as_released, TP_PROTO(int event_id))
{
	record_timeline_event(MMU_AS_0 + event_id, ACTIVITY_STOP);
}

GATOR_DEFINE_PROBE(mali_total_alloc_pages_change, TP_PROTO(long long int event_id))
{
	accumulators_data[TOTAL_ALLOC_PAGES] = event_id;
}

static int create_files(struct super_block *sb, struct dentry *root)
{
	int event;
	/*
	 * Create the filesystem for all events
	 */
	int counter_index = 0;
	const char *mali_name = gator_mali_get_mali_name();

	for (event = FIRST_TIMELINE_EVENT; event < FIRST_TIMELINE_EVENT + NUMBER_OF_TIMELINE_EVENTS; event++) {
		if (gator_mali_create_file_system(mali_name, timeline_event_names[counter_index], sb, root, &counters[event]) != 0) {
			return -1;
		}
		counter_index++;
	}
	counter_index = 0;
	for (event = FIRST_SOFTWARE_COUNTER; event < FIRST_SOFTWARE_COUNTER + NUMBER_OF_SOFTWARE_COUNTERS; event++) {
		if (gator_mali_create_file_system(mali_name, software_counter_names[counter_index], sb, root, &counters[event]) != 0) {
			return -1;
		}
		counter_index++;
	}
	counter_index = 0;
	for (event = FIRST_ACCUMULATOR; event < FIRST_ACCUMULATOR + NUMBER_OF_ACCUMULATORS; event++) {
		if (gator_mali_create_file_system(mali_name, accumulators_names[counter_index], sb, root, &counters[event]) != 0) {
			return -1;
		}
		counter_index++;
	}

	return 0;
}

static int register_tracepoints(void)
{
	if (GATOR_REGISTER_TRACE(mali_pm_status)) {
		pr_debug("gator: Mali-T6xx: mali_pm_status tracepoint failed to activate\n");
		return 0;
	}

	if (GATOR_REGISTER_TRACE(mali_page_fault_insert_pages)) {
		pr_debug("gator: Mali-T6xx: mali_page_fault_insert_pages tracepoint failed to activate\n");
		return 0;
	}

	if (GATOR_REGISTER_TRACE(mali_mmu_as_in_use)) {
		pr_debug("gator: Mali-T6xx: mali_mmu_as_in_use tracepoint failed to activate\n");
		return 0;
	}

	if (GATOR_REGISTER_TRACE(mali_mmu_as_released)) {
		pr_debug("gator: Mali-T6xx: mali_mmu_as_released tracepoint failed to activate\n");
		return 0;
	}

	if (GATOR_REGISTER_TRACE(mali_total_alloc_pages_change)) {
		pr_debug("gator: Mali-T6xx: mali_total_alloc_pages_change tracepoint failed to activate\n");
		return 0;
	}

	pr_debug("gator: Mali-T6xx: start\n");
	pr_debug("gator: Mali-T6xx: mali_pm_status probe is at %p\n", &probe_mali_pm_status);
	pr_debug("gator: Mali-T6xx: mali_page_fault_insert_pages probe is at %p\n", &probe_mali_page_fault_insert_pages);
	pr_debug("gator: Mali-T6xx: mali_mmu_as_in_use probe is at %p\n", &probe_mali_mmu_as_in_use);
	pr_debug("gator: Mali-T6xx: mali_mmu_as_released probe is at %p\n", &probe_mali_mmu_as_released);
	pr_debug("gator: Mali-T6xx: mali_total_alloc_pages_change probe is at %p\n", &probe_mali_total_alloc_pages_change);

	return 1;
}

static int start(void)
{
	unsigned int cnt;

	/* Clean all data for the next capture */
	for (cnt = 0; cnt < NUMBER_OF_TIMELINE_EVENTS; cnt++) {
		timeline_event_starttime[cnt].tv_sec = timeline_event_starttime[cnt].tv_nsec = 0;
		timeline_data[cnt] = 0;
	}

	for (cnt = 0; cnt < NUMBER_OF_SOFTWARE_COUNTERS; cnt++) {
		sw_counter_data[cnt] = 0;
	}

	for (cnt = 0; cnt < NUMBER_OF_ACCUMULATORS; cnt++) {
		accumulators_data[cnt] = 0;
	}

	/* Register tracepoints */
	if (register_tracepoints() == 0) {
		return -1;
	}

	/*
	 * Set the first timestamp for calculating the sample interval. The first interval could be quite long,
	 * since it will be the time between 'start' and the first 'read'.
	 * This means that timeline values will be divided by a big number for the first sample.
	 */
	getnstimeofday(&prev_timestamp);

	return 0;
}

static void stop(void)
{
	pr_debug("gator: Mali-T6xx: stop\n");

	/*
	 * It is safe to unregister traces even if they were not successfully
	 * registered, so no need to check.
	 */
	GATOR_UNREGISTER_TRACE(mali_pm_status);
	pr_debug("gator: Mali-T6xx: mali_pm_status tracepoint deactivated\n");

	GATOR_UNREGISTER_TRACE(mali_page_fault_insert_pages);
	pr_debug("gator: Mali-T6xx: mali_page_fault_insert_pages tracepoint deactivated\n");

	GATOR_UNREGISTER_TRACE(mali_mmu_as_in_use);
	pr_debug("gator: Mali-T6xx: mali_mmu_as_in_use tracepoint deactivated\n");

	GATOR_UNREGISTER_TRACE(mali_mmu_as_released);
	pr_debug("gator: Mali-T6xx: mali_mmu_as_released tracepoint deactivated\n");

	GATOR_UNREGISTER_TRACE(mali_total_alloc_pages_change);
	pr_debug("gator: Mali-T6xx: mali_total_alloc_pages_change tracepoint deactivated\n");
}

static int read(int **buffer)
{
	int cnt;
	int len = 0;
	long sample_interval_us = 0;
	struct timespec read_timestamp;

	if (!on_primary_core()) {
		return 0;
	}

	/* Get the start of this sample period. */
	getnstimeofday(&read_timestamp);

	/*
	 * Calculate the sample interval if the previous sample time is valid.
	 * We use tv_sec since it will not be 0.
	 */
	if (prev_timestamp.tv_sec != 0) {
		sample_interval_us = get_duration_us(&prev_timestamp, &read_timestamp);
	}

	/* Structure copy. Update the previous timestamp. */
	prev_timestamp = read_timestamp;

	/*
	 * Report the timeline counters (ACTIVITY_START/STOP)
	 */
	for (cnt = FIRST_TIMELINE_EVENT; cnt < (FIRST_TIMELINE_EVENT + NUMBER_OF_TIMELINE_EVENTS); cnt++) {
		mali_counter *counter = &counters[cnt];
		if (counter->enabled) {
			const int index = cnt - FIRST_TIMELINE_EVENT;
			unsigned int value;

			/* If the activity is still running, reset its start time to the start of this sample period
			 * to correct the count.  Add the time up to the end of the sample onto the count. */
			if (timeline_event_starttime[index].tv_sec != 0) {
				const long event_duration = get_duration_us(&timeline_event_starttime[index], &read_timestamp);
				timeline_data[index] += event_duration;
				timeline_event_starttime[index] = read_timestamp;	/* Activity is still running. */
			}

			if (sample_interval_us != 0) {
				/* Convert the counter to a percent-of-sample value */
				value = (timeline_data[index] * 100) / sample_interval_us;
			} else {
				pr_debug("gator: Mali-T6xx: setting value to zero\n");
				value = 0;
			}

			/* Clear the counter value ready for the next sample. */
			timeline_data[index] = 0;

			counter_dump[len++] = counter->key;
			counter_dump[len++] = value;
		}
	}

	/* Report the software counters */
	for (cnt = FIRST_SOFTWARE_COUNTER; cnt < (FIRST_SOFTWARE_COUNTER + NUMBER_OF_SOFTWARE_COUNTERS); cnt++) {
		const mali_counter *counter = &counters[cnt];
		if (counter->enabled) {
			const int index = cnt - FIRST_SOFTWARE_COUNTER;
			counter_dump[len++] = counter->key;
			counter_dump[len++] = sw_counter_data[index];
			/* Set the value to zero for the next time */
			sw_counter_data[index] = 0;
		}
	}

	/* Report the accumulators */
	for (cnt = FIRST_ACCUMULATOR; cnt < (FIRST_ACCUMULATOR + NUMBER_OF_ACCUMULATORS); cnt++) {
		const mali_counter *counter = &counters[cnt];
		if (counter->enabled) {
			const int index = cnt - FIRST_ACCUMULATOR;
			counter_dump[len++] = counter->key;
			counter_dump[len++] = accumulators_data[index];
			/* Do not zero the accumulator */
		}
	}

	/* Update the buffer */
	if (buffer) {
		*buffer = (int *)counter_dump;
	}

	return len;
}

static struct gator_interface gator_events_mali_t6xx_interface = {
	.create_files = create_files,
	.start = start,
	.stop = stop,
	.read = read
};

extern int gator_events_mali_t6xx_init(void)
{
	pr_debug("gator: Mali-T6xx: sw_counters init\n");

	gator_mali_initialise_counters(counters, NUMBER_OF_EVENTS);

	return gator_events_install(&gator_events_mali_t6xx_interface);
}

gator_events_init(gator_events_mali_t6xx_init);
