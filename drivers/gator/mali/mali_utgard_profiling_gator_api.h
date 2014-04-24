/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __MALI_UTGARD_PROFILING_GATOR_API_H__
#define __MALI_UTGARD_PROFILING_GATOR_API_H__

#ifdef __cplusplus
extern "C"
{
#endif

#define MALI_PROFILING_API_VERSION 4

#define MAX_NUM_L2_CACHE_CORES 3
#define MAX_NUM_FP_CORES 8
#define MAX_NUM_VP_CORES 1

/** The list of events supported by the Mali DDK. */
typedef enum
{
    /* Vertex processor activity */
    ACTIVITY_VP_0 = 0,

    /* Fragment processor activity */
    ACTIVITY_FP_0, /* 1 */
    ACTIVITY_FP_1,
    ACTIVITY_FP_2,
    ACTIVITY_FP_3,
    ACTIVITY_FP_4,
    ACTIVITY_FP_5,
    ACTIVITY_FP_6,
    ACTIVITY_FP_7,

    /* L2 cache counters */
    COUNTER_L2_0_C0,
    COUNTER_L2_0_C1,
    COUNTER_L2_1_C0,
    COUNTER_L2_1_C1,
    COUNTER_L2_2_C0,
    COUNTER_L2_2_C1,

    /* Vertex processor counters */
    COUNTER_VP_0_C0, /*15*/
    COUNTER_VP_0_C1,

    /* Fragment processor counters */
    COUNTER_FP_0_C0,
    COUNTER_FP_0_C1,
    COUNTER_FP_1_C0,
    COUNTER_FP_1_C1,
    COUNTER_FP_2_C0,
    COUNTER_FP_2_C1,
    COUNTER_FP_3_C0,
    COUNTER_FP_3_C1,
    COUNTER_FP_4_C0,
    COUNTER_FP_4_C1,
    COUNTER_FP_5_C0,
    COUNTER_FP_5_C1,
    COUNTER_FP_6_C0,
    COUNTER_FP_6_C1,
    COUNTER_FP_7_C0,
    COUNTER_FP_7_C1, /* 32 */

    /*
     * If more hardware counters are added, the _mali_osk_hw_counter_table
     * below should also be updated.
     */

    /* EGL software counters */
    COUNTER_EGL_BLIT_TIME,

    /* GLES software counters */
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

    /* Framebuffer capture pseudo-counter */
    COUNTER_FILMSTRIP,

    NUMBER_OF_EVENTS
} _mali_osk_counter_id;

#define FIRST_ACTIVITY_EVENT    ACTIVITY_VP_0
#define LAST_ACTIVITY_EVENT     ACTIVITY_FP_7

#define FIRST_HW_COUNTER        COUNTER_L2_0_C0
#define LAST_HW_COUNTER         COUNTER_FP_7_C1

#define FIRST_SW_COUNTER        COUNTER_EGL_BLIT_TIME
#define LAST_SW_COUNTER         COUNTER_GLES_LOOP_LINES_COUNT

#define FIRST_SPECIAL_COUNTER   COUNTER_FILMSTRIP
#define LAST_SPECIAL_COUNTER    COUNTER_FILMSTRIP

/**
 * Structure to pass performance counter data of a Mali core
 */
typedef struct _mali_profiling_core_counters
{
	u32 source0;
	u32 value0;
	u32 source1;
	u32 value1;
} _mali_profiling_core_counters;

/**
 * Structure to pass performance counter data of Mali L2 cache cores
 */
typedef struct _mali_profiling_l2_counter_values
{
	struct _mali_profiling_core_counters cores[MAX_NUM_L2_CACHE_CORES];
} _mali_profiling_l2_counter_values;

/**
 * Structure to pass data defining Mali instance in use:
 *
 * mali_product_id - Mali product id
 * mali_version_major - Mali version major number
 * mali_version_minor - Mali version minor number
 * num_of_l2_cores - number of L2 cache cores
 * num_of_fp_cores - number of fragment processor cores
 * num_of_vp_cores - number of vertex processor cores
 */
typedef struct _mali_profiling_mali_version
{
	u32 mali_product_id;
	u32 mali_version_major;
	u32 mali_version_minor;
	u32 num_of_l2_cores;
	u32 num_of_fp_cores;
	u32 num_of_vp_cores;
} _mali_profiling_mali_version;

/*
 * List of possible actions to be controlled by Streamline.
 * The following numbers are used by gator to control the frame buffer dumping and s/w counter reporting.
 * We cannot use the enums in mali_uk_types.h because they are unknown inside gator.
 */
#define FBDUMP_CONTROL_ENABLE (1)
#define FBDUMP_CONTROL_RATE (2)
#define SW_COUNTER_ENABLE (3)
#define FBDUMP_CONTROL_RESIZE_FACTOR (4)

void _mali_profiling_control(u32 action, u32 value);

u32 _mali_profiling_get_l2_counters(_mali_profiling_l2_counter_values *values);

int _mali_profiling_set_event(u32 counter_id, s32 event_id);

u32 _mali_profiling_get_api_version(void);

void _mali_profiling_get_mali_version(struct _mali_profiling_mali_version *values);

#ifdef __cplusplus
}
#endif

#endif /* __MALI_UTGARD_PROFILING_GATOR_API_H__ */
