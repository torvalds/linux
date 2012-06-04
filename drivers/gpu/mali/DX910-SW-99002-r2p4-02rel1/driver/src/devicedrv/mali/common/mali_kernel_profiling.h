/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_KERNEL_PROFILING_H__
#define __MALI_KERNEL_PROFILING_H__

#if MALI_TIMELINE_PROFILING_ENABLED

#include <../../../include/cinstr/mali_cinstr_profiling_events_m200.h>

#define MALI_PROFILING_MAX_BUFFER_ENTRIES 1048576

/**
 * Initialize the profiling module.
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t _mali_profiling_init(mali_bool auto_start);

/*
 * Terminate the profiling module.
 */
void _mali_profiling_term(void);

/**
 * Start recording profiling data
 *
 * The specified limit will determine how large the capture buffer is.
 * MALI_PROFILING_MAX_BUFFER_ENTRIES determines the maximum size allowed by the device driver.
 *
 * @param limit The desired maximum number of events to record on input, the actual maximum on output.
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t _mali_profiling_start(u32 * limit);

/**
 * Add an profiling event
 *
 * @param event_id The event identificator.
 * @param data0 First data parameter, depending on event_id specified.
 * @param data1 Second data parameter, depending on event_id specified.
 * @param data2 Third data parameter, depending on event_id specified.
 * @param data3 Fourth data parameter, depending on event_id specified.
 * @param data4 Fifth data parameter, depending on event_id specified.
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t _mali_profiling_add_event(u32 event_id, u32 data0, u32 data1, u32 data2, u32 data3, u32 data4);

/**
 * Stop recording profiling data
 *
 * @param count Returns the number of recorded events.
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t _mali_profiling_stop(u32 * count);

/**
 * Retrieves the number of events that can be retrieved
 *
 * @return The number of recorded events that can be retrieved.
 */
u32 _mali_profiling_get_count(void);

/**
 * Retrieve an event
 *
 * @param index Event index (start with 0 and continue until this function fails to retrieve all events)
 * @param timestamp The timestamp for the retrieved event will be stored here.
 * @param event_id The event ID for the retrieved event will be stored here.
 * @param data The 5 data values for the retrieved event will be stored here.
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */_mali_osk_errcode_t _mali_profiling_get_event(u32 index, u64* timestamp, u32* event_id, u32 data[5]);

/**
 * Clear the recorded buffer.
 *
 * This is needed in order to start another recording.
 *
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t _mali_profiling_clear(void);

/**
 * Checks if a recording of profiling data is in progress
 *
 * @return MALI_TRUE if recording of profiling data is in progress, MALI_FALSE if not
 */
mali_bool _mali_profiling_is_recording(void);

/**
 * Checks if profiling data is available for retrival
 *
 * @return MALI_TRUE if profiling data is avaiable, MALI_FALSE if not
 */
mali_bool _mali_profiling_have_recording(void);

/**
 * Enable or disable profiling events as default for new sessions (applications)
 *
 * @param enable MALI_TRUE if profiling events should be turned on, otherwise MALI_FALSE
 */
void _mali_profiling_set_default_enable_state(mali_bool enable);

/**
 * Get current default enable state for new sessions (applications)
 *
 * @return MALI_TRUE if profiling events should be turned on, otherwise MALI_FALSE
 */
mali_bool _mali_profiling_get_default_enable_state(void);

#endif /* MALI_TIMELINE_PROFILING_ENABLED */

#endif /* __MALI_KERNEL_PROFILING_H__ */


