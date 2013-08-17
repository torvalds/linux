/*
 *
 * (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */



/**
 * @file mali_osk_time.h
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#ifndef _OSK_TIME_H_
#define _OSK_TIME_H_

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @addtogroup base_api
 * @{
 */

/**
 * @addtogroup base_osk_api
 * @{
 */

/**
 * @addtogroup osktime Time
 *
 * A set of time functions based on the OS tick timer which allow
 * - retrieving the current tick count
 * - determining if a deadline has passed
 * - calculating the elapsed time between two tick counts
 * - converting a time interval to a tick count
 *
 * Used in combination with scheduling timers, or determining duration of certain actions (timeouts, profiling).
 * @{
 */

/**
 * @brief Get current tick count
 *
 * Retrieves the current tick count in the OS native resolution. Use in
 * combination with osk_time_after() to determine if current time has
 * passed a deadline, or to determine the elapsed time with osk_time_tickstoms()
 * and osk_time_after().
 *
 * The tick count resolution is 32-bit or higher, supporting a 1000Hz tick count
 * of 2^32/1000*60*60*24 ~= 49.7 days before the counter wraps.
 *
 * @return current tick count
 */
OSK_STATIC_INLINE osk_ticks osk_time_now(void) CHECK_RESULT;

/**
 * @brief Convert milliseconds to ticks
 *
 * Converts \a ms milliseconds to ticks.
 *
 * Intended use of this function is to convert small timeout periods or
 * calculate nearby deadlines (e.g. osk_ticks_now() + osk_time_mstoticks(50)).
 *
 * Supports converting a period of up to 2^32/1000*60*60*24 ~= 49.7 days
 * in the case of a 1000Hz OS tick timer.
 *
 * @param[in] ms number of millisecons to convert to ticks
 * @return number of ticks in \a ms milliseconds
 */
OSK_STATIC_INLINE u32 osk_time_mstoticks(u32 ms) CHECK_RESULT;

/**
 * @brief Calculate elapsed time
 *
 * Calculates elapsed time in milliseconds between tick \a ticka and \a tickb,
 * taking into account that the tick counter may have wrapped. Note that
 * \a tickb must be later than \a ticka in time.
 *
 * @param[in] ticka  a tick count value (as returned from osk_time_now())
 * @param[in] tickb  a tick count value (as returned from osk_time_now())
 * @return elapsed time in milliseconds
 */
OSK_STATIC_INLINE u32 osk_time_elapsed(osk_ticks ticka, osk_ticks tickb) CHECK_RESULT;

/**
 * @brief Determines which tick count is later in time
 *
 * Determines if \a ticka comes after \a tickb in time. Handles the case where
 * the tick counter may have wrapped.
 *
 * Intended use of this function is to determine if a deadline has passed
 * or to determine how the difference between two tick count values should
 * be calculated.
 *
 * @param[in] ticka  a tick count value (as returned from osk_time_now())
 * @param[in] tickb  a tick count value (as returned from osk_time_now())
 * @return MALI_TRUE when \a ticka is after \a tickb in time.
 * @return MALI_FALSE when \a tickb is after \a ticka in time.
 */
OSK_STATIC_INLINE mali_bool osk_time_after(osk_ticks ticka, osk_ticks tickb) CHECK_RESULT;

/**
 * @brief Retrieve current "wall clock" time
 *
 * This function returns the current time in a format that userspace can also
 * produce and allows direct comparison of events in the kernel with events
 * that userspace controls.
 *
 * @param[in] ts    An osk_timespec structure
 */
OSK_STATIC_INLINE void osk_gettimeofday(osk_timeval *ts);

/* @} */ /* end group osktime */

/** @} */ /* end group base_osk_api */

/** @} */ /* end group base_api */

/* pull in the arch header with the implementation  */
#include <osk/mali_osk_arch_time.h>

#ifdef __cplusplus
}
#endif


#endif /* _OSK_TIME_H_ */
