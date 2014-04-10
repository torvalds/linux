/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
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
 * There are (currently) four different variants of the comms between gator and Mali:
 * 1 (deprecated): No software counter support
 * 2 (deprecated): Tracepoint called for each separate s/w counter value as it appears
 * 3 (default): Single tracepoint for all s/w counters in a bundle.
 * Interface style 3 is the default if no other is specified.  1 and 2 will be eliminated when
 * existing Mali DDKs are upgraded.
 * 4. As above, but for the Utgard (Mali-450) driver.
 */

#if !defined(GATOR_MALI_INTERFACE_STYLE)
#define GATOR_MALI_INTERFACE_STYLE (3)
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
static unsigned long counter_dump[NUMBER_OF_EVENTS * 2];
static unsigned long counter_prev[NUMBER_OF_EVENTS];

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

/**
 * Calculate the difference and handle the overflow.
 */
static u32 get_difference(u32 start, u32 end)
{
	if (start - end >= 0) {
		return start - end;
	}

	// Mali counters are unsigned 32 bit values that wrap.
	return (4294967295u - end) + start;
}

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

/*
 * These are provided for utgard compatibility.
 */
typedef void _mali_profiling_get_mali_version_type(struct _mali_profiling_mali_version *values);
typedef u32 _mali_profiling_get_l2_counters_type(_mali_profiling_l2_counter_values *values);

#if GATOR_MALI_INTERFACE_STYLE == 2
/**
 * Returns non-zero if the given counter ID is a software counter.
 */
static inline int is_sw_counter(unsigned int event_id)
{
	return (event_id >= FIRST_SW_COUNTER && event_id <= LAST_SW_COUNTER);
}
#endif

#if GATOR_MALI_INTERFACE_STYLE == 2
/*
 * The Mali DDK uses s64 types to contain software counter values, but gator
 * can only use a maximum of 32 bits. This function scales a software counter
 * to an appropriate range.
 */
static u32 scale_sw_counter_value(unsigned int event_id, signed long long value)
{
	u32 scaled_value;

	switch (event_id) {
	case COUNTER_GLES_UPLOAD_TEXTURE_TIME:
	case COUNTER_GLES_UPLOAD_VBO_TIME:
		scaled_value = (u32)div_s64(value, 1000000);
		break;
	default:
		scaled_value = (u32)value;
		break;
	}

	return scaled_value;
}
#endif

/* Probe for continuously sampled counter */
#if 0				//WE_DONT_CURRENTLY_USE_THIS_SO_SUPPRESS_WARNING
GATOR_DEFINE_PROBE(mali_sample_address, TP_PROTO(unsigned int event_id, u32 *addr))
{
	/* Turning on too many pr_debug statements in frequently called functions
	 * can cause stability and/or performance problems
	 */
	//pr_debug("gator: mali_sample_address %d %d\n", event_id, addr);
	if (event_id >= ACTIVITY_VP && event_id <= COUNTER_FP3_C1) {
		counter_address[event_id] = addr;
	}
}
#endif

/* Probe for hardware counter events */
GATOR_DEFINE_PROBE(mali_hw_counter, TP_PROTO(unsigned int event_id, unsigned int value))
{
	/* Turning on too many pr_debug statements in frequently called functions
	 * can cause stability and/or performance problems
	 */
	//pr_debug("gator: mali_hw_counter %d %d\n", event_id, value);
	if (is_hw_counter(event_id)) {
		counter_data[event_id] = value;
	}
}

#if GATOR_MALI_INTERFACE_STYLE == 2
GATOR_DEFINE_PROBE(mali_sw_counter, TP_PROTO(unsigned int event_id, signed long long value))
{
	if (is_sw_counter(event_id)) {
		counter_data[event_id] = scale_sw_counter_value(event_id, value);
	}
}
#endif /* GATOR_MALI_INTERFACE_STYLE == 2 */

#if GATOR_MALI_INTERFACE_STYLE >= 3
GATOR_DEFINE_PROBE(mali_sw_counters, TP_PROTO(pid_t pid, pid_t tid, void *surface_id, unsigned int *counters))
{
	u32 i;

	/* Copy over the values for those counters which are enabled. */
	for (i = FIRST_SW_COUNTER; i <= LAST_SW_COUNTER; i++) {
		if (counter_enabled[i]) {
			counter_data[i] = (u32)(counters[i - FIRST_SW_COUNTER]);
		}
	}
}
#endif /* GATOR_MALI_INTERFACE_STYLE >= 3 */

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

	if (!dir) {
		return -1;
	}

	if (create_event_item) {
		gatorfs_create_ulong(sb, dir, "event", &counter_event[event]);
	}

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
	_mali_profiling_get_mali_version_type *mali_profiling_get_mali_version_symbol;

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
		printk("gator: mali online _mali_profiling_get_mali_version symbol not found\n");
	}
}
#endif

static int create_files(struct super_block *sb, struct dentry *root)
{
	int event;
	const char *mali_name = gator_mali_get_mali_name();

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

	/* Vertex processor counters */
	for (core_id = 0; core_id < n_vp_cores; core_id++) {
		int activity_counter_id = ACTIVITY_VP_0;
		snprintf(buf, sizeof buf, "ARM_%s_VP_%d_active", mali_name, core_id);
		if (create_fs_entry(sb, root, buf, activity_counter_id, 0) != 0) {
			return -1;
		}

		for (counter_number = 0; counter_number < 2; counter_number++) {
			int counter_id = COUNTER_VP_0_C0 + (2 * core_id) + counter_number;

			snprintf(buf, sizeof buf, "ARM_%s_VP_%d_cnt%d", mali_name, core_id, counter_number);
			if (create_fs_entry(sb, root, buf, counter_id, 1) != 0) {
				return -1;
			}
		}
	}

	/* Fragment processors' counters */
	for (core_id = 0; core_id < n_fp_cores; core_id++) {
		int activity_counter_id = ACTIVITY_FP_0 + core_id;

		snprintf(buf, sizeof buf, "ARM_%s_FP_%d_active", mali_name, core_id);
		if (create_fs_entry(sb, root, buf, activity_counter_id, 0) != 0) {
			return -1;
		}

		for (counter_number = 0; counter_number < 2; counter_number++) {
			int counter_id = COUNTER_FP_0_C0 + (2 * core_id) + counter_number;

			snprintf(buf, sizeof buf, "ARM_%s_FP_%d_cnt%d", mali_name, core_id, counter_number);
			if (create_fs_entry(sb, root, buf, counter_id, 1) != 0) {
				return -1;
			}
		}
	}

	/* L2 Cache counters */
	for (core_id = 0; core_id < n_l2_cores; core_id++) {
		for (counter_number = 0; counter_number < 2; counter_number++) {
			int counter_id = COUNTER_L2_0_C0 + (2 * core_id) + counter_number;

			snprintf(buf, sizeof buf, "ARM_%s_L2_%d_cnt%d", mali_name, core_id, counter_number);
			if (create_fs_entry(sb, root, buf, counter_id, 1) != 0) {
				return -1;
			}
		}
	}

	/* Now set up the software counter entries */
	for (event = FIRST_SW_COUNTER; event <= LAST_SW_COUNTER; event++) {
		snprintf(buf, sizeof(buf), "ARM_%s_SW_%d", mali_name, event - FIRST_SW_COUNTER);

		if (create_fs_entry(sb, root, buf, event, 0) != 0) {
			return -1;
		}
	}

	/* Now set up the special counter entries */
	snprintf(buf, sizeof(buf), "ARM_%s_Filmstrip_cnt0", mali_name);
	if (create_fs_entry(sb, root, buf, COUNTER_FILMSTRIP, 1) != 0) {
		return -1;
	}

#ifdef DVFS_REPORTED_BY_DDK
	snprintf(buf, sizeof(buf), "ARM_%s_Frequency", mali_name);
	if (create_fs_entry(sb, root, buf, COUNTER_FREQUENCY, 1) != 0) {
		return -1;
	}

	snprintf(buf, sizeof(buf), "ARM_%s_Voltage", mali_name);
	if (create_fs_entry(sb, root, buf, COUNTER_VOLTAGE, 1) != 0) {
		return -1;
	}
#endif

	return 0;
}

/*
 * Local store for the get_counters entry point into the DDK.
 * This is stored here since it is used very regularly.
 */
static mali_profiling_get_counters_type *mali_get_counters = NULL;
static _mali_profiling_get_l2_counters_type *mali_get_l2_counters = NULL;

/*
 * Examine list of counters between two index limits and determine if any one is enabled.
 * Returns 1 if any counter is enabled, 0 if none is.
 */
static int is_any_counter_enabled(unsigned int first_counter, unsigned int last_counter)
{
	unsigned int i;

	for (i = first_counter; i <= last_counter; i++) {
		if (counter_enabled[i]) {
			return 1;	/* At least one counter is enabled */
		}
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
			if (counter_enabled[counter_id]) {
				mali_set_hw_event(counter_id, counter_event[counter_id]);
			} else {
				mali_set_hw_event(counter_id, 0xFFFFFFFF);
			}
		}

		symbol_put(_mali_profiling_set_event);
	} else {
		printk("gator: mali online _mali_profiling_set_event symbol not found\n");
	}
}

static void mali_counter_initialize(void)
{
	int i;
	int core_id;

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
		printk("gator: mali online _mali_profiling_control symbol not found\n");
	}

	mali_get_counters = symbol_get(_mali_profiling_get_counters);
	if (mali_get_counters) {
		pr_debug("gator: mali online _mali_profiling_get_counters symbol @ %p\n", mali_get_counters);

	} else {
		pr_debug("gator WARNING: mali _mali_profiling_get_counters symbol not defined");
	}

	mali_get_l2_counters = symbol_get(_mali_profiling_get_l2_counters);
	if (mali_get_l2_counters) {
		pr_debug("gator: mali online _mali_profiling_get_l2_counters symbol @ %p\n", mali_get_l2_counters);

	} else {
		pr_debug("gator WARNING: mali _mali_profiling_get_l2_counters symbol not defined");
	}

	if (!mali_get_counters && !mali_get_l2_counters) {
		pr_debug("gator: WARNING: no L2 counters available");
		n_l2_cores = 0;
	}

	for (core_id = 0; core_id < n_l2_cores; core_id++) {
		int counter_id = COUNTER_L2_0_C0 + (2 * core_id);
		counter_prev[counter_id] = 0;
		counter_prev[counter_id + 1] = 0;
	}

	/* Clear counters in the start */
	for (i = 0; i < NUMBER_OF_EVENTS; i++) {
		counter_data[i] = 0;
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
		for (i = FIRST_HW_COUNTER; i <= LAST_HW_COUNTER; i++) {
			mali_set_hw_event(i, 0xFFFFFFFF);
		}

		symbol_put(_mali_profiling_set_event);
	} else {
		printk("gator: mali offline _mali_profiling_set_event symbol not found\n");
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
		printk("gator: mali offline _mali_profiling_control symbol not found\n");
	}

	if (mali_get_counters) {
		symbol_put(_mali_profiling_get_counters);
	}

	if (mali_get_l2_counters) {
		symbol_put(_mali_profiling_get_l2_counters);
	}
}

static int start(void)
{
	// register tracepoints
	if (GATOR_REGISTER_TRACE(mali_hw_counter)) {
		printk("gator: mali_hw_counter tracepoint failed to activate\n");
		return -1;
	}

#if GATOR_MALI_INTERFACE_STYLE == 1
	/* None. */
#elif GATOR_MALI_INTERFACE_STYLE == 2
	/* For patched Mali driver. */
	if (GATOR_REGISTER_TRACE(mali_sw_counter)) {
		printk("gator: mali_sw_counter tracepoint failed to activate\n");
		return -1;
	}
#elif GATOR_MALI_INTERFACE_STYLE >= 3
	/* For Mali drivers with built-in support. */
	if (GATOR_REGISTER_TRACE(mali_sw_counters)) {
		printk("gator: mali_sw_counters tracepoint failed to activate\n");
		return -1;
	}
#else
#error Unknown GATOR_MALI_INTERFACE_STYLE option.
#endif

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

#if GATOR_MALI_INTERFACE_STYLE == 1
		/* None. */
#elif GATOR_MALI_INTERFACE_STYLE == 2
		/* For patched Mali driver. */
		GATOR_UNREGISTER_TRACE(mali_sw_counter);
#elif GATOR_MALI_INTERFACE_STYLE >= 3
		/* For Mali drivers with built-in support. */
		GATOR_UNREGISTER_TRACE(mali_sw_counters);
#else
#error Unknown GATOR_MALI_INTERFACE_STYLE option.
#endif

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

static int read(int **buffer)
{
	int len = 0;

	if (!on_primary_core())
		return 0;

	// Read the L2 C0 and C1 here.
	if (n_l2_cores > 0 && is_any_counter_enabled(COUNTER_L2_0_C0, COUNTER_L2_0_C0 + (2 * n_l2_cores))) {
		unsigned int unavailable_l2_caches = 0;
		_mali_profiling_l2_counter_values cache_values;
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

			if ((1 << cache_id) & unavailable_l2_caches) {
				continue; /* This cache is unavailable (powered-off, possibly). */
			}

			per_core = &cache_values.cores[cache_id];

			if (counter_enabled[counter_id_0]) {
				// Calculate and save src0's counter val0
				counter_dump[len++] = counter_key[counter_id_0];
				counter_dump[len++] = get_difference(per_core->value0, counter_prev[counter_id_0]);
			}

			if (counter_enabled[counter_id_1]) {
				// Calculate and save src1's counter val1
				counter_dump[len++] = counter_key[counter_id_1];
				counter_dump[len++] = get_difference(per_core->value1, counter_prev[counter_id_1]);
			}

			// Save the previous values for the counters.
			counter_prev[counter_id_0] = per_core->value0;
			counter_prev[counter_id_1] = per_core->value1;
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
		 * Add in the voltage and frequency counters if enabled.  Note that, since these are
		 * actually passed as events, the counter value should not be cleared.
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

	if (buffer) {
		*buffer = (int *)counter_dump;
	}

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
