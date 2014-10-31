/**
 * Copyright (C) ARM Limited 2013-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef __MALI_MJOLLNIR_PROFILING_GATOR_API_H__
#define __MALI_MJOLLNIR_PROFILING_GATOR_API_H__

#ifdef __cplusplus
extern "C"
{
#endif


/*
 * The number of processor cores.  Update to suit your hardware implementation.
 */
#define MAX_NUM_FP_CORES            (4)
#define MAX_NUM_VP_CORES            (1)
#define MAX_NUM_L2_CACHE_CORES      (1)

enum counters {
	/* Timeline activity */
	ACTIVITY_VP_0 = 0,
	ACTIVITY_FP_0,
	ACTIVITY_FP_1,
	ACTIVITY_FP_2,
	ACTIVITY_FP_3,

	/* L2 cache counters */
	COUNTER_L2_0_C0,
	COUNTER_L2_0_C1,

	/* Vertex processor counters */
	COUNTER_VP_0_C0,
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
	COUNTER_FREQUENCY,
	COUNTER_VOLTAGE,

	NUMBER_OF_EVENTS
};

#define FIRST_ACTIVITY_EVENT    ACTIVITY_VP_0
#define LAST_ACTIVITY_EVENT     ACTIVITY_FP_3

#define FIRST_HW_COUNTER        COUNTER_L2_0_C0
#define LAST_HW_COUNTER         COUNTER_FP_3_C1

#define FIRST_SW_COUNTER        COUNTER_EGL_BLIT_TIME
#define LAST_SW_COUNTER         COUNTER_GLES_LOOP_LINES_COUNT

/* Signifies that the system is able to report voltage and frequency numbers. */
#define DVFS_REPORTED_BY_DDK 1

/**
 * Structure to pass performance counter data of a Mali core
 */
struct _mali_profiling_core_counters {
	u32 source0;
	u32 value0;
	u32 source1;
	u32 value1;
};

/*
 * For compatibility with utgard.
 */
struct _mali_profiling_l2_counter_values {
	struct _mali_profiling_core_counters cores[MAX_NUM_L2_CACHE_CORES];
};

struct _mali_profiling_mali_version {
	u32 mali_product_id;
	u32 mali_version_major;
	u32 mali_version_minor;
	u32 num_of_l2_cores;
	u32 num_of_fp_cores;
	u32 num_of_vp_cores;
};

extern void _mali_profiling_get_mali_version(struct _mali_profiling_mali_version *values);
extern u32 _mali_profiling_get_l2_counters(struct _mali_profiling_l2_counter_values *values);

/*
 * List of possible actions allowing DDK to be controlled by Streamline.
 * The following numbers are used by DDK to control the frame buffer dumping.
 */
#define FBDUMP_CONTROL_ENABLE (1)
#define FBDUMP_CONTROL_RATE (2)
#define SW_COUNTER_ENABLE      (3)
#define FBDUMP_CONTROL_RESIZE_FACTOR (4)


#ifdef __cplusplus
}
#endif

#endif /* __MALI_MJOLLNIR_PROFILING_GATOR_API_H__ */
