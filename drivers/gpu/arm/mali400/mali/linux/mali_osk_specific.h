/*
 * Copyright (C) 2011-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_osk_specific.h
 * Defines per-OS Kernel level specifics, such as unusual workarounds for
 * certain OSs.
 */

#ifndef __MALI_OSK_SPECIFIC_H__
#define __MALI_OSK_SPECIFIC_H__

#include <asm/uaccess.h>

#include "mali_sync.h"

#define MALI_STATIC_INLINE static inline
#define MALI_NON_STATIC_INLINE inline

#ifdef CONFIG_SYNC
/* MALI_SEC */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,4,0)
typedef struct sync_timeline mali_sync_tl;
typedef struct sync_pt mali_sync_pt;

MALI_STATIC_INLINE mali_sync_pt *_mali_osk_sync_pt_create(mali_sync_tl *parent)
{
	return (mali_sync_pt*)mali_sync_pt_alloc(parent);
}

MALI_STATIC_INLINE void _mali_osk_sync_pt_signal(mali_sync_pt *pt)
{
	mali_sync_signal_pt(pt, 0);
}
#endif
#endif /* CONFIG_SYNC */

MALI_STATIC_INLINE u32 _mali_osk_copy_from_user(void *to, void *from, u32 n)
{
	return (u32)copy_from_user(to, from, (unsigned long)n);
}

/** The list of events supported by the Mali DDK. */
typedef enum
{
    /* Vertex processor activity */
    ACTIVITY_VP = 0,

    /* Fragment processor activity */
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

#define FIRST_ACTIVITY_EVENT    ACTIVITY_VP
#define LAST_ACTIVITY_EVENT     ACTIVITY_FP3

#define FIRST_HW_COUNTER        COUNTER_L2_C0
#define LAST_HW_COUNTER         COUNTER_FP3_C1

#define FIRST_SW_COUNTER        COUNTER_EGL_BLIT_TIME
#define LAST_SW_COUNTER         COUNTER_GLES_LOOP_LINES_COUNT

#define FIRST_SPECIAL_COUNTER   COUNTER_FILMSTRIP
#define LAST_SPECIAL_COUNTER    COUNTER_FILMSTRIP

#endif /* __MALI_OSK_SPECIFIC_H__ */
