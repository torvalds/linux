/*
 *
 * (C) COPYRIGHT 2010-2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/**
 * @file mali_osk_timers.h
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#ifndef _OSK_TIMERS_H_
#define _OSK_TIMERS_H_

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
 * @addtogroup osktimers Timers
 *  
 * A set of functions to control a one-shot timer. Timer expirations are
 * set relative to current time at millisecond resolution. A minimum time
 * period of 1 millisecond needs to be observed -- immediate firing of
 * timers is not supported. A user-supplied function is called with a
 * user-supplied argument when the timer expires. The expiration time
 * of a timer cannot be changed once started - a timer first needs to
 * be stopped before it can be started with another timer expiration.
 *
 * Examples of use: watchdog timeout on job execution duration, 
 * a job progress checker, power management profile timeouts. 
 *
 * @{
 */

/**
 * @brief Initializes a timer
 *
 * Initializes a timer. The timer is not started yet. 
 *
 * Note For timers created on stack @ref osk_timer_on_stack_init() should be used.
 *
 * The timer may be reinitialized but only after having called osk_timer_term()
 * on \a tim.
 *
 * @param[out] tim  an osk timer object to initialize
 * @return OSK_ERR_NONE on success. Any other value indicates failure.
 */
OSK_STATIC_INLINE osk_error osk_timer_init(osk_timer * const tim) CHECK_RESULT;

/**
 * @brief Initializes a timer on stack
 *
 * Initializes a timer created on stack. The timer is not started yet.
 *
 * The timer may be reinitialized but only after having called osk_timer_term()
 * on \a tim.
 *
 * @param[out] tim  an osk timer object to initialize
 * @return OSK_ERR_NONE on success. Any other value indicates failure.
 */
OSK_STATIC_INLINE osk_error osk_timer_on_stack_init(osk_timer * const tim) CHECK_RESULT;

/**
 * @brief Starts a timer
 *
 * Starts a timer. When the timer expires in \a delay milliseconds, the
 * registered callback function will be called with the user supplied
 * argument. 
 *
 * The callback needs to be registered with osk_timer_callback_set()
 * at least once during the lifetime of timer \a tim and before starting
 * the timer.
 *
 * You cannot start a timer that has been started already. A timer needs
 * to be stopped before starting it again.
 *
 * A timer cannot expire immediately. The minimum \a delay value is 1.
 *
 * A timer may fail to start and is important to check the result of this
 * function to prevent waiting for a callback that will never get called.
 *
 * @param[in] tim   an initialized osk timer object
 * @param[in] delay timer expiration in milliseconds, at least 1.
 * @return OSK_ERR_NONE on success. Any other value indicates failure.
 */
OSK_STATIC_INLINE osk_error osk_timer_start(osk_timer *tim, u32 delay) CHECK_RESULT;

/**
 * @brief Starts a timer using a high-resolution parameter
 *
 * This is identical to osk_timer_start(), except that the argument is
 * expressed in nanoseconds.
 *
 * @note whilst the parameter is high-resolution, the actual resolution of the
 * timer may be much more coarse than nanoseconds. In this case, \a delay_ns
 * will be rounded up to the timer resolution.
 *
 * @param[in] tim   an initialized osk timer object
 * @param[in] delay_ns timer expiration in nanoseconds, at least 1.
 * @return OSK_ERR_NONE on success. Any other value indicates failure.
 */
OSK_STATIC_INLINE osk_error osk_timer_start_ns(osk_timer *tim, u64 delay_ns) CHECK_RESULT;

/**
 * @brief Modifies a timer's timeout
 *
 * See \a osk_timer_start for details.
 *
 * The only difference of this function from \a osk_timer_start is:
 * If the timer was already set to expire the timer is modified to expire in \a new_delay milliseconds.
 *
 * @param[in] tim       an initialized osk timer object
 * @param[in] new_delay timer expiration in milliseconds, at least 1.
 * @return OSK_ERR_NONE on success. Any other value indicates failure.
 */
OSK_STATIC_INLINE osk_error osk_timer_modify(osk_timer *tim, u32 new_delay) CHECK_RESULT;

/**
 * @brief Modifies a timer's timeout using a high-resolution parameter
 *
 * This is identical to osk_timer_modify(), except that the argument is
 * expressed in nanoseconds.
 *
 * @note whilst the parameter is high-resolution, the actual resolution of the
 * timer may be much more coarse than nanoseconds. In this case, \a new_delay_ns
 * will be rounded up to the timer resolution.
 *
 * @param[in] tim          an initialized osk timer object
 * @param[in] new_delay_ns timer expiration in nanoseconds, at least 1.
 * @return OSK_ERR_NONE on success. Any other value indicates failure.
 */
OSK_STATIC_INLINE osk_error osk_timer_modify_ns(osk_timer *tim, u64 new_delay_ns) CHECK_RESULT;

/**
 * @brief Stops a timer
 *
 * Stops a timer. If the timer already expired this will have no 
 * effect. If the callback for the timer is currently executing,
 * this function will block on its completion.
 *
 * A non-expired timer will have to be stopped before it can be
 * started again with osk_timer_start().
 *
 * @param[in] tim   an initialized osk timer object
 */
OSK_STATIC_INLINE void osk_timer_stop(osk_timer *tim);

/**
 * @brief Registers a callback function with a timer
 *
 * Registers a callback function to be called when the timer expires.
 * The callback function is called with the provided \a data argument.
 * The timer should be stopped when registering the callback.
 *
 * The code executing within the timer call back is limited as it
 * is assumed it executes in an IRQ context:
 * - Access to user space is not allowed - there is no process context
 * - It is not allowed to call any function that may block
 * - Only spinlocks or atomics may be used to access shared data structures
 *
 * If a timer requires more work to be done than can be achieved in an IRQ
 * context, then it should defer the work to an OSK workqueue.
 *
 * @param[in] tim      an initialized osk timer object
 * @param[in] callback timer callback function
 * @param[in] data     argument to pass to timer callback
 */
OSK_STATIC_INLINE void osk_timer_callback_set(osk_timer *tim, osk_timer_callback callback, void *data);

/**
 * @brief Terminates a timer
 *
 * Frees any resources allocated for a timer. A timer needs to be
 * stopped with osk_timer_stop() before a timer can be terminated. 
 *
 * Note For timers created on stack @ref osk_timer_on_stack_term() should be used.
 *
 * A timer may be reinitialized after it has been terminated.
 *
 * @param[in] tim an initialized osk timer object
 */
OSK_STATIC_INLINE void osk_timer_term(osk_timer *tim);

/**
 * @brief Terminates a timer on stack
 *
 * Frees any resources allocated for a timer created on stack. A timer needs to be
 * stopped with osk_timer_stop() before a timer can be terminated.
 *
 * A timer may be reinitialized after it has been terminated.
 *
 * @param[in] tim an initialized osk timer object
 */
OSK_STATIC_INLINE void osk_timer_on_stack_term(osk_timer *tim);

/* @} */ /* end group osktimers */

/** @} */ /* end group base_osk_api */

/** @} */ /* end group base_api */

/* pull in the arch header with the implementation  */
#include <osk/mali_osk_arch_timers.h>

#ifdef __cplusplus
}
#endif


#endif /* _OSK_TIMERS_H_ */
