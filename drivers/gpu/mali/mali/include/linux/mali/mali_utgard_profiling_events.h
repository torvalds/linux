/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
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
typedef enum
{
	MALI_PROFILING_EVENT_TYPE_SINGLE  = 0 << 24,
	MALI_PROFILING_EVENT_TYPE_START   = 1 << 24,
	MALI_PROFILING_EVENT_TYPE_STOP    = 2 << 24,
	MALI_PROFILING_EVENT_TYPE_SUSPEND = 3 << 24,
	MALI_PROFILING_EVENT_TYPE_RESUME  = 4 << 24,
} cinstr_profiling_event_type_t;


/**
 * Secifies the channel/source of the event
 */
typedef enum
{
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
typedef enum
{
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_NONE             = 0,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_EGL_NEW_FRAME    = 1,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_FLUSH            = 2,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_EGL_SWAP_BUFFERS = 3,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_FB_EVENT         = 4,
    MALI_PROFILING_EVENT_REASON_SINGLE_SW_ENTER_API_FUNC   = 10,
    MALI_PROFILING_EVENT_REASON_SINGLE_SW_LEAVE_API_FUNC   = 11,
   MALI_PROFILING_EVENT_REASON_SINGLE_SW_UMP_TRY_LOCK    = 53,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_UMP_LOCK        = 54,
	MALI_PROFILING_EVENT_REASON_SINGLE_SW_UMP_UNLOCK      = 55,
} cinstr_profiling_event_reason_single_sw_t;

/**
 * These events are applicable when the type MALI_PROFILING_EVENT_TYPE_START/STOP is used from software channel
 */
typedef enum
{
	MALI_PROFILING_EVENT_REASON_START_STOP_SW_NONE      = 0,
	MALI_PROFILING_EVENT_REASON_START_STOP_MALI         = 1,
} cinstr_profiling_event_reason_start_stop_sw_t;

/**
 * These events are applicable when the type MALI_PROFILING_EVENT_TYPE_SUSPEND/RESUME is used from software channel
 */
typedef enum
{
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_NONE                   =  0,
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_PIPELINE_FULL          =  1,
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_VSYNC                  = 26,
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_FB_IFRAME_WAIT         = 27,
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_FB_IFRAME_SYNC         = 28,
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_VG_WAIT_FILTER_CLEANUP = 29,
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_VG_WAIT_TEXTURE        = 30,
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_GLES_WAIT_MIPLEVEL     = 31,
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_GLES_WAIT_READPIXELS   = 32,
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_EGL_WAIT_SWAP_IMMEDIATE= 33,
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_ICS_QUEUE_BUFFER       = 34,
	MALI_PROFILING_EVENT_REASON_SUSPEND_RESUME_SW_ICS_DEQUEUE_BUFFER     = 35,
} cinstr_profiling_event_reason_suspend_resume_sw_t;

/**
 * These events are applicable when the type MALI_PROFILING_EVENT_TYPE_SINGLE is used from a HW channel (GPx+PPx)
 */
typedef enum
{
	MALI_PROFILING_EVENT_REASON_SINGLE_HW_NONE          = 0,
	MALI_PROFILING_EVENT_REASON_SINGLE_HW_INTERRUPT     = 1,
	MALI_PROFILING_EVENT_REASON_SINGLE_HW_FLUSH         = 2,
} cinstr_profiling_event_reason_single_hw_t;

/**
 * These events are applicable when the type MALI_PROFILING_EVENT_TYPE_SINGLE is used from the GPU channel
 */
typedef enum
{
	MALI_PROFILING_EVENT_REASON_SINGLE_GPU_NONE              = 0,
	MALI_PROFILING_EVENT_REASON_SINGLE_GPU_FREQ_VOLT_CHANGE  = 1,
} cinstr_profiling_event_reason_single_gpu_t;

#endif /*_MALI_UTGARD_PROFILING_EVENTS_H_*/
