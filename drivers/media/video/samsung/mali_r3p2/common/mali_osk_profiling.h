/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __MALI_OSK_PROFILING_H__
#define __MALI_OSK_PROFILING_H__

#if defined(CONFIG_MALI400_PROFILING) && defined (CONFIG_TRACEPOINTS)

#include "mali_linux_trace.h"
#include "mali_profiling_events.h"

#define MALI_PROFILING_MAX_BUFFER_ENTRIES 1048576

#define MALI_PROFILING_NO_HW_COUNTER = ((u32)-1)

/** @defgroup _mali_osk_profiling External profiling connectivity
 * @{ */

/**
 * Initialize the profiling module.
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t _mali_osk_profiling_init(mali_bool auto_start);

/*
 * Terminate the profiling module.
 */
void _mali_osk_profiling_term(void);

/**
 * Start recording profiling data
 *
 * The specified limit will determine how large the capture buffer is.
 * MALI_PROFILING_MAX_BUFFER_ENTRIES determines the maximum size allowed by the device driver.
 *
 * @param limit The desired maximum number of events to record on input, the actual maximum on output.
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t _mali_osk_profiling_start(u32 * limit);

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
/* Call Linux tracepoint directly */
#define _mali_osk_profiling_add_event(event_id, data0, data1, data2, data3, data4) trace_mali_timeline_event((event_id), (data0), (data1), (data2), (data3), (data4))

/**
 * Report a hardware counter event.
 *
 * @param counter_id The ID of the counter.
 * @param value The value of the counter.
 */

/* Call Linux tracepoint directly */
#define _mali_osk_profiling_report_hw_counter(counter_id, value) trace_mali_hw_counter(counter_id, value)

/**
 * Report SW counters
 *
 * @param counters array of counter values
 */
void _mali_osk_profiling_report_sw_counters(u32 *counters);

/**
 * Stop recording profiling data
 *
 * @param count Returns the number of recorded events.
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t _mali_osk_profiling_stop(u32 * count);

/**
 * Retrieves the number of events that can be retrieved
 *
 * @return The number of recorded events that can be retrieved.
 */
u32 _mali_osk_profiling_get_count(void);

/**
 * Retrieve an event
 *
 * @param index Event index (start with 0 and continue until this function fails to retrieve all events)
 * @param timestamp The timestamp for the retrieved event will be stored here.
 * @param event_id The event ID for the retrieved event will be stored here.
 * @param data The 5 data values for the retrieved event will be stored here.
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t _mali_osk_profiling_get_event(u32 index, u64* timestamp, u32* event_id, u32 data[5]);

/**
 * Clear the recorded buffer.
 *
 * This is needed in order to start another recording.
 *
 * @return _MALI_OSK_ERR_OK on success, otherwise failure.
 */
_mali_osk_errcode_t _mali_osk_profiling_clear(void);

/**
 * Checks if a recording of profiling data is in progress
 *
 * @return MALI_TRUE if recording of profiling data is in progress, MALI_FALSE if not
 */
mali_bool _mali_osk_profiling_is_recording(void);

/**
 * Checks if profiling data is available for retrival
 *
 * @return MALI_TRUE if profiling data is avaiable, MALI_FALSE if not
 */
mali_bool _mali_osk_profiling_have_recording(void);

/** @} */ /* end group _mali_osk_profiling */

#else /* defined(CONFIG_MALI400_PROFILING)  && defined(CONFIG_TRACEPOINTS) */

 /* Dummy add_event, for when profiling is disabled. */

#define _mali_osk_profiling_add_event(event_id, data0, data1, data2, data3, data4)

#endif /* defined(CONFIG_MALI400_PROFILING)  && defined(CONFIG_TRACEPOINTS) */

#endif /* __MALI_OSK_PROFILING_H__ */


