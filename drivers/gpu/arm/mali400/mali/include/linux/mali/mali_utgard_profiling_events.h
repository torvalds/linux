/*
 * Copyright (C) 2010-2016 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

 * Class Path Exception
 * Linking this library statically or dynamically with other modules is making a combined work based on this library. 
 * Thus, the terms and conditions of the GNU General Public License cover the whole combination.
 * As a special exception, the copyright holders of this library give you permission to link this library with independent modules 
 * to produce an executable, regardless of the license terms of these independent modules, and to copy and distribute the resulting 
 * executable under terms of your choice, provided that you also meet, for each linked independent module, the terms and conditions 
 * of the license of that module. An independent module is a module which is not derived from or based on this library. If you modify 
 * this library, you may extend this exception to your version of the library, but you are not obligated to do so. 
 * If you do not wish to do so, delete this exception statement from your version.
 */

#ifndef _MALI_UTGARD_PROFILING_EVENTS_H_
#define _MALI_UTGARD_PROFILING_EVENTS_H_

/*
 * The event ID is a 32 bit value consisting of different fields
 * reserved, 4 bits, for future use
 * event type, 4 bits, cinstr_profiling_event_type_t
 * event channel, 8 bits, the source of the event.
 * event data, 16 bit field, data depending on event type
 */

/**
 * Specifies what kind of event this is
 */
typedef enum {
	MALI_PROFILING_EVENT_TYPE_SINGLE  = 0 << 24,
	MALI_PROFILING_EVENT_TYPE_START   = 1 << 24,
	MALI_PROFILING_EVENT_TYPE_STOP    = 2 << 24,
	MALI_PROFILING_EVENT_TYPE_SUSPEND = 3 << 24,
	MALI_PROFILING_EVENT_TYPE_RESUME  = 4 << 24,
} cinstr_profiling_event_type_t;


/**
 * Secifies the channel/source of the event
 */
typedef enum {
	MALI_PROFILING_EVENT_CHANNEL_SOFTWARE =  0 << 16,
	MALI_PROFILING_EVENT_CHANNEL_GP0      =  1 << 16,
	MALI_PROFILING_EVENT_CHANNEL_PP0      =  5 << 16,
	MALI_PROFILING_EVENT_CHANNEL_PP1      =  6 << 16,
	MALI_PROFILING_EVENT_CHANNEL_PP2      =  7 << 16,
	MALI_PROFILING_EVENT_CHANNEL_PP3      =  8 << 16,
	MALI_PROFILING_EVENT_CHANNEL_PP4      =  9 << 16,
	MALI_PROFILING_EVENT_CHANNEL_PP5      = 10 << 16,
	MALI_PROFILING_EVENT_CHANNEL_PP6      = 11 << 16,
	MALI_PROFILING_EVENT_CHANNEL_PP7      = 12 << 16,
	MALI_PROFILING_EVENT_CHANNEL_GPU      = 21 << 16,
} cinstr_profiling_event_channel_t;


#define MALI_PROFILING_MAKE_EVENT_CHANNEL_GP(num) (((MALI_PROFILING_EVENT_CHANNEL_GP0 >> 16) + (num)) << 16)
#define MALI_PROFILING_MAKE_EVENT_CHANNEL_PP(num) (((MALI_PROFILING_EVENT_CHANNEL_PP0 >> 16) + (num)) << 16)

/**
 * These events are applicable when the type MALI_PROFILING_EVENT_TYPE_SINGLE is used from software channel
 */
typedef enum {
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_NONE                  = 0,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_EGL_NEW_FRAME         = 1,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_FLUSH                 = 2,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_EGL_SWAP_BUFFERS      = 3,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_FB_EVENT              = 4,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_GP_ENQUEUE            = 5,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_PP_ENQUEUE            = 6,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_READBACK              = 7,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_WRITEBACK             = 8,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_ENTER_API_FUNC        = 10,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_LEAVE_API_FUNC        = 11,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_DISCARD_ATTACHMENTS   = 13,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_UMP_TRY_LOCK          = 53,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_UMP_LOCK              = 54,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_UMP_UNLOCK            = 55,
	MALI_PROFILING_EVENT_REASON_SINGLE_LOCK_CONTENDED           = 56,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_EGL_MALI_FENCE_DUP    = 57,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_EGL_SET_PP_JOB_FENCE  = 58,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_EGL_WAIT_SYNC         = 59,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_EGL_CREATE_FENCE_SYNC = 60,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_EGL_CREATE_NATIVE_FENCE_SYNC = 61,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_EGL_FENCE_FLUSH       = 62,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_EGL_FLUSH_SERVER_WAITS = 63,
} cinstr_profiling_event_reason_single_sw_t;

/**
 * These events are applicable when the type MALI_PROFILING_EVENT_TYPE_START/STOP is used from software channel
 * to inform whether the core is physical or virtual
 */
typedef enum {
	MALI_PROFILING_EVENT_REASON_START_STOP_HW_PHYSICAL  = 0,
	MALI_PROFILING_EVENT_REASON_START_STOP_HW_VIRTUAL   = 1,
} cinstr_profiling_event_reason_start_stop_hw_t;

/**
 * These events are applicable when the type MALI_PROFILING_EVENT_TYPE_START/STOP is used from software channel
 */
typedef enum {
	/*MALI_PROFILING_EVENT_REASON_START_STOP_SW_NONE            = 0,*/
	MALI_PROFILING_EVENT_REASON_START_STOP_SW_MALI            = 1,
	MALI_PROFILING_EVENT_REASON_START_STOP_SW_CALLBACK_THREAD = 2,
	MALI_PROFILING_EVENT_REASON_START_STOP_SW_WORKER_THREAD   = 3,
	MALI_PROFILING_EVENT_REASON_START_STOP_SW_BOTTOM_HALF     = 4,
	MALI_PROFILING_EVENT_REASON_START_STOP_SW_UPPER_HALF      = 5,
} cinstr_profiling_event_reason_start_stop_sw_t;

/**
 * These events are applicable when the type MALI_PROFILING_EVENT_TYPE_SUSPEND/RESUME is used from software channel
 */
typedef enum {
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_NONE                     =  0, /* used */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_PIPELINE_FULL            =  1, /* NOT used */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_VSYNC                    = 26, /* used in some build configurations */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_FB_IFRAME_WAIT           = 27, /* USED */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_FB_IFRAME_SYNC           = 28, /* USED */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_VG_WAIT_FILTER_CLEANUP   = 29, /* used */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_VG_WAIT_TEXTURE          = 30, /* used */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_GLES_WAIT_MIPLEVEL       = 31, /* used */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_GLES_WAIT_READPIXELS     = 32, /* used */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_EGL_WAIT_SWAP_IMMEDIATE  = 33, /* NOT used */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_ICS_QUEUE_BUFFER         = 34, /* USED */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_ICS_DEQUEUE_BUFFER       = 35, /* USED */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_UMP_LOCK                 = 36, /* Not currently used */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_X11_GLOBAL_LOCK          = 37, /* Not currently used */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_X11_SWAP                 = 38, /* Not currently used */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_MALI_EGL_IMAGE_SYNC_WAIT = 39, /* USED */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_GP_JOB_HANDLING          = 40, /* USED */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_PP_JOB_HANDLING          = 41, /* USED */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_EGL_MALI_FENCE_MERGE     = 42, /* USED */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_EGL_MALI_FENCE_DUP       = 43,
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_EGL_FLUSH_SERVER_WAITS   = 44,
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_EGL_WAIT_SYNC            = 45, /* USED */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_FB_JOBS_WAIT             = 46, /* USED */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_FB_NOFRAMES_WAIT         = 47, /* USED */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_FB_NOJOBS_WAIT           = 48, /* USED */
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_SUBMIT_LIMITER_WAIT      = 49, /* USED */
} cinstr_profiling_event_reason_suspend_resume_sw_t;

/**
 * These events are applicable when the type MALI_PROFILING_EVENT_TYPE_SINGLE is used from a HW channel (GPx+PPx)
 */
typedef enum {
	MALI_PROFILING_EVENT_REASON_SINGLE_HW_NONE          = 0,
	MALI_PROFILING_EVENT_REASON_SINGLE_HW_INTERRUPT     = 1,
	MALI_PROFILING_EVENT_REASON_SINGLE_HW_FLUSH         = 2,
} cinstr_profiling_event_reason_single_hw_t;

/**
 * These events are applicable when the type MALI_PROFILING_EVENT_TYPE_SINGLE is used from the GPU channel
 */
typedef enum {
	MALI_PROFILING_EVENT_REASON_SINGLE_GPU_NONE              = 0,
	MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE  = 1,
	MALI_PROFILING_EVENT_REASON_SINGLE_GPU_L20_COUNTERS      = 2,
	MALI_PROFILING_EVENT_REASON_SINGLE_GPU_L21_COUNTERS      = 3,
	MALI_PROFILING_EVENT_REASON_SINGLE_GPU_L22_COUNTERS      = 4,
} cinstr_profiling_event_reason_single_gpu_t;

/**
 * These values are applicable for the 3rd data parameter when
 * the type MALI_PROFILING_EVENT_TYPE_START is used from the software channel
 * with the MALI_PROFILING_EVENT_REASON_START_STOP_BOTTOM_HALF reason.
 */
typedef enum {
	MALI_PROFILING_EVENT_DATA_CORE_GP0             =  1,
	MALI_PROFILING_EVENT_DATA_CORE_PP0             =  5,
	MALI_PROFILING_EVENT_DATA_CORE_PP1             =  6,
	MALI_PROFILING_EVENT_DATA_CORE_PP2             =  7,
	MALI_PROFILING_EVENT_DATA_CORE_PP3             =  8,
	MALI_PROFILING_EVENT_DATA_CORE_PP4             =  9,
	MALI_PROFILING_EVENT_DATA_CORE_PP5             = 10,
	MALI_PROFILING_EVENT_DATA_CORE_PP6             = 11,
	MALI_PROFILING_EVENT_DATA_CORE_PP7             = 12,
	MALI_PROFILING_EVENT_DATA_CORE_GP0_MMU         = 22, /* GP0 + 21 */
	MALI_PROFILING_EVENT_DATA_CORE_PP0_MMU         = 26, /* PP0 + 21 */
	MALI_PROFILING_EVENT_DATA_CORE_PP1_MMU         = 27, /* PP1 + 21 */
	MALI_PROFILING_EVENT_DATA_CORE_PP2_MMU         = 28, /* PP2 + 21 */
	MALI_PROFILING_EVENT_DATA_CORE_PP3_MMU         = 29, /* PP3 + 21 */
	MALI_PROFILING_EVENT_DATA_CORE_PP4_MMU         = 30, /* PP4 + 21 */
	MALI_PROFILING_EVENT_DATA_CORE_PP5_MMU         = 31, /* PP5 + 21 */
	MALI_PROFILING_EVENT_DATA_CORE_PP6_MMU         = 32, /* PP6 + 21 */
	MALI_PROFILING_EVENT_DATA_CORE_PP7_MMU         = 33, /* PP7 + 21 */

} cinstr_profiling_event_data_core_t;

#define MALI_PROFILING_MAKE_EVENT_DATA_CORE_GP(num) (MALI_PROFILING_EVENT_DATA_CORE_GP0 + (num))
#define MALI_PROFILING_MAKE_EVENT_DATA_CORE_GP_MMU(num) (MALI_PROFILING_EVENT_DATA_CORE_GP0_MMU + (num))
#define MALI_PROFILING_MAKE_EVENT_DATA_CORE_PP(num) (MALI_PROFILING_EVENT_DATA_CORE_PP0 + (num))
#define MALI_PROFILING_MAKE_EVENT_DATA_CORE_PP_MMU(num) (MALI_PROFILING_EVENT_DATA_CORE_PP0_MMU + (num))


#endif /*_MALI_UTGARD_PROFILING_EVENTS_H_*/
