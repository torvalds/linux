/**
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "gator.h"

#include <linux/module.h>
#include <linux/time.h>
#include <linux/math64.h>

#include "linux/mali_linux_trace.h"

#include "gator_events_mali_common.h"
#include "gator_events_mali_4xx.h"

/*
* There have been four different variants of the comms between gator and Mali depending on driver version:
* # | DDK vsn range             | Support                                                             | Notes
*
* 1 | (obsolete)                | No software counter support                                         | Obsolete patches
* 2 | (obsolete)                | Tracepoint called for each separate s/w counter value as it appears | Obsolete patches
* 3 | r3p0-04rel0 - r3p2-01rel2 | Single tracepoint for all s/w counters in a bundle.                 |
* 4 | r3p2-01rel3 - date        | As above but with extensions for MP devices (Mali-450)              | At least r4p0-00rel1
*/

#if !defined(GATOR_MALI_INTERFACE_STYLE)
#define GATOR_MALI_INTERFACE_STYLE (4)
#endif

#if GATOR_MALI_INTERFACE_STYLE == 1
#error GATOR_MALI_INTERFACE_STYLE 1 is obsolete
#elif GATOR_MALI_INTERFACE_STYLE == 2
#error GATOR_MALI_INTERFACE_STYLE 2 is obsolete
#elif GATOR_MALI_INTERFACE_STYLE >= 3
/* Valid GATOR_MALI_INTERFACE_STYLE */
#else
#error Unknown GATOR_MALI_INTERFACE_STYLE option.
#endif

#if GATOR_MALI_INTERFACE_STYLE < 4
#include "mali/mali_mjollnir_profiling_gator_api.h"
#else
#include "mali/mali_utgard_profiling_gator_api.h"
#endif

/*
 * Check that the MALI_SUPPORT define is set to one of the allowable device codes.
 */
#if (MALI_SUPPORT != MALI_4xx)
#error MALI_SUPPORT set to an invalid device code: expecting MALI_4xx
#endif

static const char mali_name[] = "4xx";

/* gatorfs variables for counter enable state,
 * the event the counter should count and the
 * 'key' (a unique id set by gatord and returned
 * by gator.ko)
 */
static unsigned long counter_enabled[NUMBER_OF_EVENTS];
static unsigned long counter_event[NUMBER_OF_EVENTS];
static unsigned long counter_key[NUMBER_OF_EVENTS];

/* The data we have recorded */
static u32 counter_data[NUMBER_OF_EVENTS];
/* The address to sample (or 0 if samples are sent to us) */
static u32 *counter_address[NUMBER_OF_EVENTS];

/* An array used to return the data we recorded
 * as key,value pairs hence the *2
 */
static int counter_dump[NUMBER_OF_EVENTS * 2];
static int counter_prev[NUMBER_OF_EVENTS];
static bool prev_set[NUMBER_OF_EVENTS];

/* Note whether tracepoints have been registered */
static int trace_registered;

/*
 * These numbers define the actual numbers of each block type that exist in the system. Initially
 * these are set to the maxima defined above; if the driver is capable of being queried (newer
 * drivers only) then the values may be revised.
 */
static unsigned int n_vp_cores = MAX_NUM_VP_CORES;
static unsigned int n_l2_cores = MAX_NUM_L2_CACHE_CORES;
static unsigned int n_fp_cores = MAX_NUM_FP_CORES;

extern struct mali_counter mali_activity[2];
static const char *const mali_activity_names[] = {
	"fragment",
	"vertex",
};

/**
 * Returns non-zero if the given counter ID is an activity counter.
 */
static inline int is_activity_counter(unsigned int event_id)
{
	return (event_id >= FIRST_ACTIVITY_EVENT &&
		event_id <= LAST_ACTIVITY_EVENT);
}

/**
 * Returns non-zero if the given counter ID is a hardware counter.
 */
static inline int is_hw_counter(unsigned int event_id)
{
	return (event_id >= FIRST_HW_COUNTER && event_id <= LAST_HW_COUNTER);
}

/* Probe for hardware counter events */
GATOR_DEFINE_PROBE(mali_hw_counter, TP_PROTO(unsigned int event_id, unsigned int value))
{
	if (is_hw_counter(event_id))
		counter_data[event_id] = value;
}

GATOR_DEFINE_PROBE(mali_sw_counters, TP_PROTO(pid_t pid, pid_t tid, void *surface_id, unsigned int *counters))
{
	u32 i;

	/* Copy over the values for those counters which are enabled. */
	for (i = FIRST_SW_COUNTER; i <= LAST_SW_COUNTER; i++) {
		if (counter_enabled[i])
			counter_data[i] = (u32)(counters[i - FIRST_SW_COUNTER]);
	}
}

/**
 * Create a single filesystem entry for a specified event.
 * @param sb the superblock
 * @param root Filesystem root
 * @param name The name of the entry to create
 * @param event The ID of the event
 * @param create_event_item boolean indicating whether to create an 'event' filesystem entry. True to create.
 *
 * @return 0 if ok, non-zero if the create failed.
 */
static int create_fs_entry(struct super_block *sb, struct dentry *root, const char *name, int event, int create_event_item)
{
	struct dentry *dir;

	dir = gatorfs_mkdir(sb, root, name);

	if (!dir)
		return -1;

	if (create_event_item)
		gatorfs_create_ulong(sb, dir, "event", &counter_event[event]);

	gatorfs_create_ulong(sb, dir, "enabled", &counter_enabled[event]);
	gatorfs_create_ro_ulong(sb, dir, "key", &counter_key[event]);

	return 0;
}

#if GATOR_MALI_INTERFACE_STYLE > 3
/*
 * Read the version info structure if available
 */
static void initialise_version_info(void)
{
	void (*mali_profiling_get_mali_version_symbol)(struct _mali_profiling_mali_version *values);

	mali_profiling_get_mali_version_symbol = symbol_get(_mali_profiling_get_mali_version);

	if (mali_profiling_get_mali_version_symbol) {
		struct _mali_profiling_mali_version version_info;

		pr_debug("gator: mali online _mali_profiling_get_mali_version symbol @ %p\n",
				mali_profiling_get_mali_version_symbol);

		/*
		 * Revise the number of each different core type using information derived from the DDK.
		 */
		mali_profiling_get_mali_version_symbol(&version_info);

		n_fp_cores = version_info.num_of_fp_cores;
		n_vp_cores = version_info.num_of_vp_cores;
		n_l2_cores = version_info.num_of_l2_cores;

		/* Release the function - we're done with it. */
		symbol_put(_mali_profiling_get_mali_version);
	} else {
		pr_err("gator: mali online _mali_profiling_get_mali_version symbol not found\n");
		pr_err("gator:  check your Mali DDK version versus the GATOR_MALI_INTERFACE_STYLE setting\n");
	}
}
#endif

static int create_files(struct super_block *sb, struct dentry *root)
{
	int event;

	char buf[40];
	int core_id;
	int counter_number;

	pr_debug("gator: Initialising counters with style = %d\n", GATOR_MALI_INTERFACE_STYLE);

#if GATOR_MALI_INTERFACE_STYLE > 3
	/*
	 * Initialise first: this sets up the number of cores available (on compatible DDK versions).
	 * Ideally this would not need guarding but other parts of the code depend on the interface style being set
	 * correctly; if it is not then the system can enter an inconsistent state.
	 */
	initialise_version_info();
#endif

	mali_activity[0].cores = n_fp_cores;
	mali_activity[1].cores = n_vp_cores;
	for (event = 0; event < ARRAY_SIZE(mali_activity); event++) {
		if (gator_mali_create_file_system(mali_name, mali_activity_names[event], sb, root, &mali_activity[event], NULL) != 0)
			return -1;
	}

	/* Vertex processor counters */
	for (core_id = 0; core_id < n_vp_cores; core_id++) {
		int activity_counter_id = ACTIVITY_VP_0;

		snprintf(buf, sizeof(buf), "ARM_Mali-%s_VP_%d_active", mali_name, core_id);
		if (create_fs_entry(sb, root, buf, activity_counter_id, 0) != 0)
			return -1;

		for (counter_number = 0; counter_number < 2; counter_number++) {
			int counter_id = COUNTER_VP_0_C0 + (2 * core_id) + counter_number;

			snprintf(buf, sizeof(buf), "ARM_Mali-%s_VP_%d_cnt%d", mali_name, core_id, counter_number);
			if (create_fs_entry(sb, root, buf, counter_id, 1) != 0)
				return -1;
		}
	}

	/* Fragment processors' counters */
	for (core_id = 0; core_id < n_fp_cores; core_id++) {
		int activity_counter_id = ACTIVITY_FP_0 + core_id;

		snprintf(buf, sizeof(buf), "ARM_Mali-%s_FP_%d_active", mali_name, core_id);
		if (create_fs_entry(sb, root, buf, activity_counter_id, 0) != 0)
			return -1;

		for (counter_number = 0; counter_number < 2; counter_number++) {
			int counter_id = COUNTER_FP_0_C0 + (2 * core_id) + counter_number;

			snprintf(buf, sizeof(buf), "ARM_Mali-%s_FP_%d_cnt%d", mali_name, core_id, counter_number);
			if (create_fs_entry(sb, root, buf, counter_id, 1) != 0)
				return -1;
		}
	}

	/* L2 Cache counters */
	for (core_id = 0; core_id < n_l2_cores; core_id++) {
		for (counter_number = 0; counter_number < 2; counter_number++) {
			int counter_id = COUNTER_L2_0_C0 + (2 * core_id) + counter_number;

			snprintf(buf, sizeof(buf), "ARM_Mali-%s_L2_%d_cnt%d", mali_name, core_id, counter_number);
			if (create_fs_entry(sb, root, buf, counter_id, 1) != 0)
				return -1;
		}
	}

	/* Now set up the software counter entries */
	for (event = FIRST_SW_COUNTER; event <= LAST_SW_COUNTER; event++) {
		snprintf(buf, sizeof(buf), "ARM_Mali-%s_SW_%d", mali_name, event - FIRST_SW_COUNTER);

		if (create_fs_entry(sb, root, buf, event, 0) != 0)
			return -1;
	}

	/* Now set up the special counter entries */
	snprintf(buf, sizeof(buf), "ARM_Mali-%s_Filmstrip_cnt0", mali_name);
	if (create_fs_entry(sb, root, buf, COUNTER_FILMSTRIP, 1) != 0)
		return -1;

#ifdef DVFS_REPORTED_BY_DDK
	snprintf(buf, sizeof(buf), "ARM_Mali-%s_Frequency", mali_name);
	if (create_fs_entry(sb, root, buf, COUNTER_FREQUENCY, 1) != 0)
		return -1;

	snprintf(buf, sizeof(buf), "ARM_Mali-%s_Voltage", mali_name);
	if (create_fs_entry(sb, root, buf, COUNTER_VOLTAGE, 1) != 0)
		return -1;
#endif

	return 0;
}

/*
 * Local store for the get_counters entry point into the DDK.
 * This is stored here since it is used very regularly.
 */
static void (*mali_get_counters)(unsigned int *, unsigned int *, unsigned int *, unsigned int *);
static u32 (*mali_get_l2_counters)(struct _mali_profiling_l2_counter_values *values);

/*
 * Examine list of counters between two index limits and determine if any one is enabled.
 * Returns 1 if any counter is enabled, 0 if none is.
 */
static int is_any_counter_enabled(unsigned int first_counter, unsigned int last_counter)
{
	unsigned int i;

	for (i = first_counter; i <= last_counter; i++) {
		if (counter_enabled[i])
			return 1;	/* At least one counter is enabled */
	}

	return 0;		/* No s/w counters enabled */
}

static void init_counters(unsigned int from_counter, unsigned int to_counter)
{
	unsigned int counter_id;

	/* If a Mali driver is present and exporting the appropriate symbol
	 * then we can request the HW counters (of which there are only 2)
	 * be configured to count the desired events
	 */
	mali_profiling_set_event_type *mali_set_hw_event;

	mali_set_hw_event = symbol_get(_mali_profiling_set_event);

	if (mali_set_hw_event) {
		pr_debug("gator: mali online _mali_profiling_set_event symbol @ %p\n", mali_set_hw_event);

		for (counter_id = from_counter; counter_id <= to_counter; counter_id++) {
			if (counter_enabled[counter_id])
				mali_set_hw_event(counter_id, counter_event[counter_id]);
			else
				mali_set_hw_event(counter_id, 0xFFFFFFFF);
		}

		symbol_put(_mali_profiling_set_event);
	} else {
		pr_err("gator: mali online _mali_profiling_set_event symbol not found\n");
	}
}

static void mali_counter_initialize(void)
{
	int i;

	mali_profiling_control_type *mali_control;

	init_counters(COUNTER_L2_0_C0, COUNTER_L2_0_C0 + (2 * n_l2_cores) - 1);
	init_counters(COUNTER_VP_0_C0, COUNTER_VP_0_C0 + (2 * n_vp_cores) - 1);
	init_counters(COUNTER_FP_0_C0, COUNTER_FP_0_C0 + (2 * n_fp_cores) - 1);

	/* Generic control interface for Mali DDK. */
	mali_control = symbol_get(_mali_profiling_control);
	if (mali_control) {
		/* The event attribute in the XML file keeps the actual frame rate. */
		unsigned int rate = counter_event[COUNTER_FILMSTRIP] & 0xff;
		unsigned int resize_factor = (counter_event[COUNTER_FILMSTRIP] >> 8) & 0xff;

		pr_debug("gator: mali online _mali_profiling_control symbol @ %p\n", mali_control);

		mali_control(SW_COUNTER_ENABLE, (is_any_counter_enabled(FIRST_SW_COUNTER, LAST_SW_COUNTER) ? 1 : 0));
		mali_control(FBDUMP_CONTROL_ENABLE, (counter_enabled[COUNTER_FILMSTRIP] ? 1 : 0));
		mali_control(FBDUMP_CONTROL_RATE, rate);
		mali_control(FBDUMP_CONTROL_RESIZE_FACTOR, resize_factor);

		pr_debug("gator: sent mali_control enabled=%d, rate=%d\n", (counter_enabled[COUNTER_FILMSTRIP] ? 1 : 0), rate);

		symbol_put(_mali_profiling_control);
	} else {
		pr_err("gator: mali online _mali_profiling_control symbol not found\n");
	}

	mali_get_counters = symbol_get(_mali_profiling_get_counters);
	if (mali_get_counters)
		pr_debug("gator: mali online _mali_profiling_get_counters symbol @ %p\n", mali_get_counters);
	else
		pr_debug("gator WARNING: mali _mali_profiling_get_counters symbol not defined\n");

	mali_get_l2_counters = symbol_get(_mali_profiling_get_l2_counters);
	if (mali_get_l2_counters)
		pr_debug("gator: mali online _mali_profiling_get_l2_counters symbol @ %p\n", mali_get_l2_counters);
	else
		pr_debug("gator WARNING: mali _mali_profiling_get_l2_counters symbol not defined\n");

	if (!mali_get_counters && !mali_get_l2_counters) {
		pr_debug("gator: WARNING: no L2 counters available\n");
		n_l2_cores = 0;
	}

	/* Clear counters in the start */
	for (i = 0; i < NUMBER_OF_EVENTS; i++) {
		counter_data[i] = 0;
		prev_set[i] = false;
	}
}

static void mali_counter_deinitialize(void)
{
	mali_profiling_set_event_type *mali_set_hw_event;
	mali_profiling_control_type *mali_control;

	mali_set_hw_event = symbol_get(_mali_profiling_set_event);

	if (mali_set_hw_event) {
		int i;

		pr_debug("gator: mali offline _mali_profiling_set_event symbol @ %p\n", mali_set_hw_event);
		for (i = FIRST_HW_COUNTER; i <= LAST_HW_COUNTER; i++)
			mali_set_hw_event(i, 0xFFFFFFFF);

		symbol_put(_mali_profiling_set_event);
	} else {
		pr_err("gator: mali offline _mali_profiling_set_event symbol not found\n");
	}

	/* Generic control interface for Mali DDK. */
	mali_control = symbol_get(_mali_profiling_control);

	if (mali_control) {
		pr_debug("gator: mali offline _mali_profiling_control symbol @ %p\n", mali_control);

		/* Reset the DDK state - disable counter collection */
		mali_control(SW_COUNTER_ENABLE, 0);

		mali_control(FBDUMP_CONTROL_ENABLE, 0);

		symbol_put(_mali_profiling_control);
	} else {
		pr_err("gator: mali offline _mali_profiling_control symbol not found\n");
	}

	if (mali_get_counters)
		symbol_put(_mali_profiling_get_counters);

	if (mali_get_l2_counters)
		symbol_put(_mali_profiling_get_l2_counters);
}

static int start(void)
{
	/* register tracepoints */
	if (GATOR_REGISTER_TRACE(mali_hw_counter)) {
		pr_err("gator: mali_hw_counter tracepoint failed to activate\n");
		return -1;
	}

	/* For Mali drivers with built-in support. */
	if (GATOR_REGISTER_TRACE(mali_sw_counters)) {
		pr_err("gator: mali_sw_counters tracepoint failed to activate\n");
		return -1;
	}

	trace_registered = 1;

	mali_counter_initialize();
	return 0;
}

static void stop(void)
{
	unsigned int cnt;

	pr_debug("gator: mali stop\n");

	if (trace_registered) {
		GATOR_UNREGISTER_TRACE(mali_hw_counter);

		/* For Mali drivers with built-in support. */
		GATOR_UNREGISTER_TRACE(mali_sw_counters);

		pr_debug("gator: mali timeline tracepoint deactivated\n");

		trace_registered = 0;
	}

	for (cnt = 0; cnt < NUMBER_OF_EVENTS; cnt++) {
		counter_enabled[cnt] = 0;
		counter_event[cnt] = 0;
		counter_address[cnt] = NULL;
	}

	mali_counter_deinitialize();
}

static void dump_counters(unsigned int from_counter, unsigned int to_counter, unsigned int *len)
{
	unsigned int counter_id;

	for (counter_id = from_counter; counter_id <= to_counter; counter_id++) {
		if (counter_enabled[counter_id]) {
			counter_dump[(*len)++] = counter_key[counter_id];
			counter_dump[(*len)++] = counter_data[counter_id];

			counter_data[counter_id] = 0;
		}
	}
}

static int read(int **buffer, bool sched_switch)
{
	int len = 0;

	if (!on_primary_core())
		return 0;

	/* Read the L2 C0 and C1 here. */
	if (n_l2_cores > 0 && is_any_counter_enabled(COUNTER_L2_0_C0, COUNTER_L2_0_C0 + (2 * n_l2_cores))) {
		unsigned int unavailable_l2_caches = 0;
		struct _mali_profiling_l2_counter_values cache_values;
		unsigned int cache_id;
		struct _mali_profiling_core_counters *per_core;

		/* Poke the driver to get the counter values - older style; only one L2 cache */
		if (mali_get_l2_counters) {
			unavailable_l2_caches = mali_get_l2_counters(&cache_values);
		} else if (mali_get_counters) {
			per_core = &cache_values.cores[0];
			mali_get_counters(&per_core->source0, &per_core->value0, &per_core->source1, &per_core->value1);
		} else {
			/* This should never happen, as n_l2_caches is only set > 0 if one of the above functions is found. */
		}

		/* Fill in the two cache counter values for each cache block. */
		for (cache_id = 0; cache_id < n_l2_cores; cache_id++) {
			unsigned int counter_id_0 = COUNTER_L2_0_C0 + (2 * cache_id);
			unsigned int counter_id_1 = counter_id_0 + 1;

			if ((1 << cache_id) & unavailable_l2_caches)
				continue; /* This cache is unavailable (powered-off, possibly). */

			per_core = &cache_values.cores[cache_id];

			if (counter_enabled[counter_id_0] && prev_set[counter_id_0]) {
				/* Calculate and save src0's counter val0 */
				counter_dump[len++] = counter_key[counter_id_0];
				counter_dump[len++] = per_core->value0 - counter_prev[counter_id_0];
			}

			if (counter_enabled[counter_id_1] && prev_set[counter_id_1]) {
				/* Calculate and save src1's counter val1 */
				counter_dump[len++] = counter_key[counter_id_1];
				counter_dump[len++] = per_core->value1 - counter_prev[counter_id_1];
			}

			/* Save the previous values for the counters. */
			counter_prev[counter_id_0] = per_core->value0;
			prev_set[counter_id_0] = true;
			counter_prev[counter_id_1] = per_core->value1;
			prev_set[counter_id_1] = true;
		}
	}

	/* Process other (non-timeline) counters. */
	dump_counters(COUNTER_VP_0_C0, COUNTER_VP_0_C0 + (2 * n_vp_cores) - 1, &len);
	dump_counters(COUNTER_FP_0_C0, COUNTER_FP_0_C0 + (2 * n_fp_cores) - 1, &len);

	dump_counters(FIRST_SW_COUNTER, LAST_SW_COUNTER, &len);

#ifdef DVFS_REPORTED_BY_DDK
	{
		int cnt;
		/*
		 * Add in the voltage and frequency counters if enabled. Note
		 * that, since these are actually passed as events, the counter
		 * value should not be cleared.
		 */
		cnt = COUNTER_FREQUENCY;
		if (counter_enabled[cnt]) {
			counter_dump[len++] = counter_key[cnt];
			counter_dump[len++] = counter_data[cnt];
		}

		cnt = COUNTER_VOLTAGE;
		if (counter_enabled[cnt]) {
			counter_dump[len++] = counter_key[cnt];
			counter_dump[len++] = counter_data[cnt];
		}
	}
#endif

	if (buffer)
		*buffer = counter_dump;

	return len;
}

static struct gator_interface gator_events_mali_interface = {
	.create_files = create_files,
	.start = start,
	.stop = stop,
	.read = read,
};

extern void gator_events_mali_log_dvfs_event(unsigned int frequency_mhz, unsigned int voltage_mv)
{
#ifdef DVFS_REPORTED_BY_DDK
	counter_data[COUNTER_FREQUENCY] = frequency_mhz;
	counter_data[COUNTER_VOLTAGE] = voltage_mv;
#endif
}

int gator_events_mali_init(void)
{
	unsigned int cnt;

	pr_debug("gator: mali init\n");

	gator_mali_initialise_counters(mali_activity, ARRAY_SIZE(mali_activity));

	for (cnt = 0; cnt < NUMBER_OF_EVENTS; cnt++) {
		counter_enabled[cnt] = 0;
		counter_event[cnt] = 0;
		counter_key[cnt] = gator_events_get_key();
		counter_address[cnt] = NULL;
		counter_data[cnt] = 0;
	}

	trace_registered = 0;

	return gator_events_install(&gator_events_mali_interface);
}
