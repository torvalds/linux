/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
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

/*
 * There are (currently) three different variants of the comms between gator and Mali:
 * 1 (deprecated): No software counter support
 * 2 (deprecated): Tracepoint called for each separate s/w counter value as it appears
 * 3 (default): Single tracepoint for all s/w counters in a bundle.
 * Interface style 3 is the default if no other is specified.  1 and 2 will be eliminated when
 * existing Mali DDKs are upgraded.
 */

#if !defined(GATOR_MALI_INTERFACE_STYLE)
#define GATOR_MALI_INTERFACE_STYLE (3)
#endif

#define MALI_200     (0x0a07)
#define MALI_300     (0x0b06) //This is not actually true; Mali-300 is also 0x0b07
#define MALI_400     (0x0b07)
#define MALI_T6xx    (0x0056)

/*
 * List of possible actions allowing DDK to be controlled by Streamline.
 * The following numbers are used by DDK to control the frame buffer dumping.
 */
#define FBDUMP_CONTROL_ENABLE (1)
#define FBDUMP_CONTROL_RATE (2)
#define SW_EVENTS_ENABLE      (3)
#define FBDUMP_CONTROL_RESIZE_FACTOR (4)

/*
 * Check that the MALI_SUPPORT define is set to one of the allowable device codes.
 */
#if !defined(MALI_SUPPORT)
#error MALI_SUPPORT not defined!
#elif (MALI_SUPPORT != MALI_200) && (MALI_SUPPORT != MALI_300) && (MALI_SUPPORT != MALI_400) && (MALI_SUPPORT != MALI_T6xx)
#error MALI_SUPPORT set to an invalid device code
#endif

static const char *mali_name;

enum counters {
    /* Timeline activity */
    ACTIVITY_VP = 0,
    ACTIVITY_FP0,
    ACTIVITY_FP1,
    ACTIVITY_FP2,
    ACTIVITY_FP3,
    
    /* L2 cache counters */
    COUNTER_L2_C0,
    COUNTER_L2_C1,

    /* Vertex processor counters */
    COUNTER_VP_C0,
    COUNTER_VP_C1, 

    /* Fragment processor counters */
    COUNTER_FP0_C0,    
    COUNTER_FP0_C1,     
    COUNTER_FP1_C0,
    COUNTER_FP1_C1,
    COUNTER_FP2_C0,
    COUNTER_FP2_C1,
    COUNTER_FP3_C0,
    COUNTER_FP3_C1,

    /* EGL Software Counters */
    COUNTER_EGL_BLIT_TIME,

    /* GLES Software Counters */
    COUNTER_GLES_DRAW_ELEMENTS_CALLS,
    COUNTER_GLES_DRAW_ELEMENTS_NUM_INDICES,
    COUNTER_GLES_DRAW_ELEMENTS_NUM_TRANSFORMED,
    COUNTER_GLES_DRAW_ARRAYS_CALLS,
    COUNTER_GLES_DRAW_ARRAYS_NUM_TRANSFORMED,
    COUNTER_GLES_DRAW_POINTS,
    COUNTER_GLES_DRAW_LINES,
    COUNTER_GLES_DRAW_LINE_LOOP,
    COUNTER_GLES_DRAW_LINE_STRIP,
    COUNTER_GLES_DRAW_TRIANGLES,
    COUNTER_GLES_DRAW_TRIANGLE_STRIP,
    COUNTER_GLES_DRAW_TRIANGLE_FAN,
    COUNTER_GLES_NON_VBO_DATA_COPY_TIME,
    COUNTER_GLES_UNIFORM_BYTES_COPIED_TO_MALI,
    COUNTER_GLES_UPLOAD_TEXTURE_TIME,
    COUNTER_GLES_UPLOAD_VBO_TIME,
    COUNTER_GLES_NUM_FLUSHES,
    COUNTER_GLES_NUM_VSHADERS_GENERATED,
    COUNTER_GLES_NUM_FSHADERS_GENERATED,
    COUNTER_GLES_VSHADER_GEN_TIME,
    COUNTER_GLES_FSHADER_GEN_TIME,
    COUNTER_GLES_INPUT_TRIANGLES,
    COUNTER_GLES_VXCACHE_HIT,
    COUNTER_GLES_VXCACHE_MISS,
    COUNTER_GLES_VXCACHE_COLLISION,
    COUNTER_GLES_CULLED_TRIANGLES,
    COUNTER_GLES_CULLED_LINES,
    COUNTER_GLES_BACKFACE_TRIANGLES,
    COUNTER_GLES_GBCLIP_TRIANGLES,
    COUNTER_GLES_GBCLIP_LINES,
    COUNTER_GLES_TRIANGLES_DRAWN,
    COUNTER_GLES_DRAWCALL_TIME,
    COUNTER_GLES_TRIANGLES_COUNT,
    COUNTER_GLES_INDEPENDENT_TRIANGLES_COUNT,
    COUNTER_GLES_STRIP_TRIANGLES_COUNT,
    COUNTER_GLES_FAN_TRIANGLES_COUNT,
    COUNTER_GLES_LINES_COUNT,
    COUNTER_GLES_INDEPENDENT_LINES_COUNT,
    COUNTER_GLES_STRIP_LINES_COUNT,
    COUNTER_GLES_LOOP_LINES_COUNT,

	COUNTER_FILMSTRIP,

    NUMBER_OF_EVENTS
};

#define FIRST_ACTIVITY_EVENT    ACTIVITY_VP
#define LAST_ACTIVITY_EVENT     ACTIVITY_FP3

#define FIRST_HW_COUNTER        COUNTER_L2_C0
#define LAST_HW_COUNTER         COUNTER_FP3_C1

#define FIRST_SW_COUNTER        COUNTER_EGL_BLIT_TIME
#define LAST_SW_COUNTER         COUNTER_GLES_LOOP_LINES_COUNT

#define FIRST_SPECIAL_COUNTER   COUNTER_FILMSTRIP
#define LAST_SPECIAL_COUNTER    COUNTER_FILMSTRIP

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
static u32* counter_address[NUMBER_OF_EVENTS];

/* An array used to return the data we recorded
 * as key,value pairs hence the *2
 */
static unsigned long counter_dump[NUMBER_OF_EVENTS * 2];
static unsigned long counter_prev[NUMBER_OF_EVENTS];

/* Note whether tracepoints have been registered */
static int trace_registered;

/**
 * Calculate the difference and handle the overflow.
 */
static u32 get_difference(u32 start, u32 end)
{
	if (start - end >= 0)
	{
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

/**
 * Returns non-zero if the given counter ID is a software counter.
 */
static inline int is_sw_counter(unsigned int event_id)
{
    return (event_id >= FIRST_SW_COUNTER && event_id <= LAST_SW_COUNTER);
}

/*
 * The Mali DDK uses s64 types to contain software counter values, but gator
 * can only use a maximum of 32 bits. This function scales a software counter
 * to an appopriate range.
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

/* Probe for continuously sampled counter */
#if 0 //WE_DONT_CURRENTLY_USE_THIS_SO_SUPPRESS_WARNING
GATOR_DEFINE_PROBE(mali_sample_address, TP_PROTO(unsigned int event_id, u32* addr))
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


#if GATOR_MALI_INTERFACE_STYLE == 3
GATOR_DEFINE_PROBE(mali_sw_counters, TP_PROTO(pid_t pid, pid_t tid, void * surface_id, unsigned int * counters))
{
	u32 i;

	/* Copy over the values for those counters which are enabled. */
	for(i=FIRST_SW_COUNTER; i <= LAST_SW_COUNTER; i++)
	{
		if(counter_enabled[i])
		{
			counter_data[i] = (u32)(counters[i - FIRST_SW_COUNTER]);
		}
	}
}
#endif /* GATOR_MALI_INTERFACE_STYLE == 3 */

//TODO need to work out how many fp units we have
u32 gator_mali_get_n_fp(void) {
    return 4;
}

//TODO need to work out what kind of Mali we are looking at
u32 gator_mali_get_id(void) {
    return MALI_SUPPORT;
}

int gator_events_mali_create_files(struct super_block *sb, struct dentry *root) {
    struct dentry *dir;
    int event;
    int n_fp = gator_mali_get_n_fp();

    /*
     * Create the filesystem entries for vertex processor, fragement processor
     * and L2 cache timeline and hardware counters. Software counters get 
     * special handling after this block.
     */
    for (event = FIRST_ACTIVITY_EVENT; event <= LAST_HW_COUNTER; event++)
    {
        char buf[40];

        /* 
         * We can skip this event if it's for a non-existent fragment
         * processor.
         */
        if (((event - ACTIVITY_FP0 >= n_fp) && (event < COUNTER_L2_C0)) ||
            (((event - COUNTER_FP0_C0)/2 >= n_fp)))
        {
            continue;
        }

        /* Otherwise, set up the filesystem entry for this event. */
        switch (event) {
            case ACTIVITY_VP:
                snprintf(buf, sizeof buf, "ARM_%s_VP_active", mali_name);
                break;
            case ACTIVITY_FP0:
            case ACTIVITY_FP1:
            case ACTIVITY_FP2:
            case ACTIVITY_FP3:
                snprintf(buf, sizeof buf, "ARM_%s_FP%d_active", 
                    mali_name, event - ACTIVITY_FP0);
                break;
            case COUNTER_L2_C0:
            case COUNTER_L2_C1:
                snprintf(buf, sizeof buf, "ARM_%s_L2_cnt%d", 
                    mali_name, event - COUNTER_L2_C0);
                break;
            case COUNTER_VP_C0:
            case COUNTER_VP_C1:
                snprintf(buf, sizeof buf, "ARM_%s_VP_cnt%d", 
                    mali_name, event - COUNTER_VP_C0);
                break;
            case COUNTER_FP0_C0:
            case COUNTER_FP0_C1:
            case COUNTER_FP1_C0:
            case COUNTER_FP1_C1:
            case COUNTER_FP2_C0:
            case COUNTER_FP2_C1:
            case COUNTER_FP3_C0:
            case COUNTER_FP3_C1:
                snprintf(buf, sizeof buf, "ARM_%s_FP%d_cnt%d", mali_name, 
                    (event - COUNTER_FP0_C0) / 2, (event - COUNTER_FP0_C0) % 2);
                break;
            default:
                printk("gator: trying to create file for non-existent counter (%d)\n", event);
                continue;
        }

        dir = gatorfs_mkdir(sb, root, buf);
        
        if (!dir) {
            return -1;
        }
        
        gatorfs_create_ulong(sb, dir, "enabled", &counter_enabled[event]);
        
        /* Only create an event node for counters that can change what they count */
        if (event >= COUNTER_L2_C0) {
            gatorfs_create_ulong(sb, dir, "event", &counter_event[event]);
        }
        
        gatorfs_create_ro_ulong(sb, dir, "key", &counter_key[event]);
    }

    /* Now set up the software counter entries */
    for (event = FIRST_SW_COUNTER; event <= LAST_SW_COUNTER; event++)
    {
        char buf[40];

        snprintf(buf, sizeof(buf), "ARM_%s_SW_%d", mali_name, event);

        dir = gatorfs_mkdir(sb, root, buf);

        if (!dir) {
            return -1;
        }

        gatorfs_create_ulong(sb, dir, "enabled", &counter_enabled[event]);
        gatorfs_create_ro_ulong(sb, dir, "key", &counter_key[event]);
    }

	/* Now set up the special counter entries */
    for (event = FIRST_SPECIAL_COUNTER; event <= LAST_SPECIAL_COUNTER; event++)
    {
        char buf[40];

        snprintf(buf, sizeof(buf), "ARM_%s_Filmstrip", mali_name);

        dir = gatorfs_mkdir(sb, root, buf);

        if (!dir) {
            return -1;
        }

        gatorfs_create_ulong(sb, dir, "event", &counter_event[event]);
        gatorfs_create_ulong(sb, dir, "enabled", &counter_enabled[event]);
        gatorfs_create_ro_ulong(sb, dir, "key", &counter_key[event]);
    }


    return 0;
}

//TODO
void _mali_profiling_set_event(unsigned int, unsigned int);
void _mali_osk_fb_control_set(unsigned int, unsigned int);
void _mali_profiling_control(unsigned int, unsigned int);

void _mali_profiling_get_counters(unsigned int*, unsigned int*, unsigned int*, unsigned int*);
void (*_mali_profiling_get_counters_function_pointer)(unsigned int*, unsigned int*, unsigned int*, unsigned int*);

/*
 * Examine list of software counters and determine if any one is enabled.
 * Returns 1 if any counter is enabled, 0 if none is.
 */
static int is_any_sw_counter_enabled(void)
{
	unsigned int i;

	for (i = FIRST_SW_COUNTER; i <= LAST_SW_COUNTER; i++)
	{
		if (counter_enabled[i])
		{
			return 1;	/* At least one counter is enabled */
		}
	}

	return 0;	/* No s/w counters enabled */
}

static void mali_counter_initialize(void) 
{
    /* If a Mali driver is present and exporting the appropriate symbol
     * then we can request the HW counters (of which there are only 2)
     * be configured to count the desired events
     */
    void (*set_hw_event)(unsigned int, unsigned int);
	void (*set_fb_event)(unsigned int, unsigned int);
	void (*mali_control)(unsigned int, unsigned int);

    set_hw_event = symbol_get(_mali_profiling_set_event);

    if (set_hw_event) {
        int i;

        pr_debug("gator: mali online _mali_profiling_set_event symbol @ %p\n",set_hw_event);

        for (i = FIRST_HW_COUNTER; i <= LAST_HW_COUNTER; i++) {
            if (counter_enabled[i]) {
                set_hw_event(i, counter_event[i]);
            } else {
                set_hw_event(i, 0xFFFFFFFF);
            }
        }

        symbol_put(_mali_profiling_set_event);
    } else {
        printk("gator: mali online _mali_profiling_set_event symbol not found\n");
    }

	set_fb_event = symbol_get(_mali_osk_fb_control_set);

	if (set_fb_event) {
		pr_debug("gator: mali online _mali_osk_fb_control_set symbol @ %p\n", set_fb_event);

        set_fb_event(0,(counter_enabled[COUNTER_FILMSTRIP]?1:0));

		symbol_put(_mali_osk_fb_control_set);
	} else {
		printk("gator: mali online _mali_osk_fb_control_set symbol not found\n");
	}

	/* Generic control interface for Mali DDK. */
	mali_control = symbol_get(_mali_profiling_control);
	if (mali_control) {
		/* The event attribute in the XML file keeps the actual frame rate. */
		unsigned int rate = counter_event[COUNTER_FILMSTRIP] & 0xff;
		unsigned int resize_factor = (counter_event[COUNTER_FILMSTRIP] >> 8) & 0xff;

		pr_debug("gator: mali online _mali_profiling_control symbol @ %p\n", mali_control);

		mali_control(SW_EVENTS_ENABLE, (is_any_sw_counter_enabled()?1:0));
		mali_control(FBDUMP_CONTROL_ENABLE, (counter_enabled[COUNTER_FILMSTRIP]?1:0));
		mali_control(FBDUMP_CONTROL_RATE, rate);
		mali_control(FBDUMP_CONTROL_RESIZE_FACTOR, resize_factor);

		pr_debug("gator: sent mali_control enabled=%d, rate=%d\n", (counter_enabled[COUNTER_FILMSTRIP]?1:0), rate);

		symbol_put(_mali_profiling_control);
	} else {
		printk("gator: mali online _mali_profiling_control symbol not found\n");
	}

	_mali_profiling_get_counters_function_pointer =  symbol_get(_mali_profiling_get_counters);
	if (_mali_profiling_get_counters_function_pointer){
		pr_debug("gator: mali online _mali_profiling_get_counters symbol @ %p\n", _mali_profiling_get_counters_function_pointer);
		counter_prev[COUNTER_L2_C0] = 0;
		counter_prev[COUNTER_L2_C1] = 0;
	}
	else{
		pr_debug("gator WARNING: mali _mali_profiling_get_counters symbol  not defined");
	}
}

static void mali_counter_deinitialize(void) 
{
    void (*set_hw_event)(unsigned int, unsigned int);
	void (*set_fb_event)(unsigned int, unsigned int);
	void (*mali_control)(unsigned int, unsigned int);

    set_hw_event = symbol_get(_mali_profiling_set_event);

    if (set_hw_event) {
        int i;
        
        pr_debug("gator: mali offline _mali_profiling_set_event symbol @ %p\n",set_hw_event);
        for (i = FIRST_HW_COUNTER; i <= LAST_HW_COUNTER; i++) {
            set_hw_event(i, 0xFFFFFFFF);
        }
        
        symbol_put(_mali_profiling_set_event);
    } else {
        printk("gator: mali offline _mali_profiling_set_event symbol not found\n");
    }

	set_fb_event = symbol_get(_mali_osk_fb_control_set);

	if (set_fb_event) {
		pr_debug("gator: mali offline _mali_osk_fb_control_set symbol @ %p\n", set_fb_event);

        set_fb_event(0,0);

		symbol_put(_mali_osk_fb_control_set);
	} else {
		printk("gator: mali offline _mali_osk_fb_control_set symbol not found\n");
	}
	
	/* Generic control interface for Mali DDK. */
	mali_control = symbol_get(_mali_profiling_control);

	if (mali_control) {
		pr_debug("gator: mali offline _mali_profiling_control symbol @ %p\n", set_fb_event);

		/* Reset the DDK state - disable counter collection */
		mali_control(SW_EVENTS_ENABLE, 0);

		mali_control(FBDUMP_CONTROL_ENABLE, 0);

		symbol_put(_mali_profiling_control);
	} else {
		printk("gator: mali offline _mali_profiling_control symbol not found\n");
	}

	if (_mali_profiling_get_counters_function_pointer){
		symbol_put(_mali_profiling_get_counters);
	}

}

static int gator_events_mali_start(void) {
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
#elif GATOR_MALI_INTERFACE_STYLE == 3
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

static void gator_events_mali_stop(void) {
    unsigned int cnt;

    pr_debug("gator: mali stop\n");

    if (trace_registered) {
        GATOR_UNREGISTER_TRACE(mali_hw_counter);

#if GATOR_MALI_INTERFACE_STYLE == 1
    /* None. */
#elif GATOR_MALI_INTERFACE_STYLE == 2
    /* For patched Mali driver. */
    GATOR_UNREGISTER_TRACE(mali_sw_counter);
#elif GATOR_MALI_INTERFACE_STYLE == 3
    /* For Mali drivers with built-in support. */
    GATOR_UNREGISTER_TRACE(mali_sw_counters);
#else
#error Unknown GATOR_MALI_INTERFACE_STYLE option.
#endif

        pr_debug("gator: mali timeline tracepoint deactivated\n");
        
        trace_registered = 0;
    }

    for (cnt = FIRST_ACTIVITY_EVENT; cnt < NUMBER_OF_EVENTS; cnt++) {
        counter_enabled[cnt] = 0;
        counter_event[cnt] = 0;
        counter_address[cnt] = NULL;
    }

    mali_counter_deinitialize();
}

static int gator_events_mali_read(int **buffer) {
    int cnt, len = 0;

    if (smp_processor_id()) return 0;

    // Read the L2 C0 and C1 here.
    if (counter_enabled[COUNTER_L2_C0] || counter_enabled[COUNTER_L2_C1] ) {
        u32 src0 = 0;
        u32 val0 = 0;
        u32 src1 = 0;
        u32 val1 = 0;

        // Poke the driver to get the counter values
        if (_mali_profiling_get_counters_function_pointer){
            _mali_profiling_get_counters_function_pointer(&src0, &val0, &src1, &val1);
        }

        if (counter_enabled[COUNTER_L2_C0])
        {
            // Calculate and save src0's counter val0
            counter_dump[len++] = counter_key[COUNTER_L2_C0];
            counter_dump[len++] = get_difference(val0, counter_prev[COUNTER_L2_C0]);
        }

        if (counter_enabled[COUNTER_L2_C1])
        {
            // Calculate and save src1's counter val1
            counter_dump[len++] = counter_key[COUNTER_L2_C1];
            counter_dump[len++] = get_difference(val1, counter_prev[COUNTER_L2_C1]);
        }

        // Save the previous values for the counters.
        counter_prev[COUNTER_L2_C0] = val0;
        counter_prev[COUNTER_L2_C1] = val1;
    }

    // Process other (non-timeline) counters.
    for (cnt = COUNTER_VP_C0; cnt <= LAST_SW_COUNTER; cnt++) {
        if (counter_enabled[cnt]) {
            counter_dump[len++] = counter_key[cnt];
            counter_dump[len++] = counter_data[cnt];

            counter_data[cnt] = 0;
        }
    }

    if (buffer) {
        *buffer = (int*) counter_dump;
    }

    return len;
}

static struct gator_interface gator_events_mali_interface = {
    .create_files = gator_events_mali_create_files,
    .start = gator_events_mali_start,
    .stop = gator_events_mali_stop,
    .read = gator_events_mali_read,
};

int gator_events_mali_init(void)
{
    unsigned int cnt;
    u32 id = gator_mali_get_id();

    switch (id) {
    case MALI_T6xx:
        mali_name = "Mali-T6xx";
        break;
    case MALI_400:
        mali_name = "Mali-400";
        break;
    case MALI_300:
        mali_name = "Mali-300";
        break;
    case MALI_200:
        mali_name = "Mali-200";
        break;
    default:
        printk("Unknown Mali ID (%d)\n", id);
        return -1;
    }

    pr_debug("gator: mali init\n");

    for (cnt = FIRST_ACTIVITY_EVENT; cnt < NUMBER_OF_EVENTS; cnt++) {
        counter_enabled[cnt] = 0;
        counter_event[cnt] = 0;
        counter_key[cnt] = gator_events_get_key();
        counter_address[cnt] = NULL;
		counter_data[cnt] = 0;
    }

    trace_registered = 0;

    return gator_events_install(&gator_events_mali_interface);
}
gator_events_init(gator_events_mali_init);
