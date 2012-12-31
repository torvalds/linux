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
 * @file mali_osk_waitq.h
 * Implementation of the OS abstraction layer for the kernel device driver
 */

#ifndef _OSK_WAITQ_H_
#define _OSK_WAITQ_H_

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
 * @addtogroup oskwaitq Wait queue
 *  
 * A waitqueue is used to wait for a specific condition to become true.
 * The waitqueue has a flag that needs to be set when the condition
 * becomes true and cleared when the condition becomes false.
 *
 * Threads wait for the specific condition to become true by calling
 * osk_waitq_wait(). If the condition is already true osk_waitq_wait() 
 * will return immediately. 
 *
 * When a thread causes the specific condition to become true, it needs
 * to set the waitqueue flag with osk_waitq_set(), which will wakeup 
 * all threads waiting on the waitqueue.
 *
 * When a thread causes the specific condition to become false, it needs
 * to clear the waitqueue flag with osk_waitq_clear().
 *
 * @{
 */

/**
 * @brief Initialize a wait queue
 *
 * Initializes a waitqueue. The waitqueue flag is cleared assuming the
 * specific condition associated with the waitqueue is false.
 *
 * @param[out] wq  wait queue to initialize
 * @return OSK_ERROR_NONE on success. Any other value indicates failure.
 */
OSK_STATIC_INLINE osk_error osk_waitq_init(osk_waitq * const wq) CHECK_RESULT;

/**
 * @brief Wait until waitqueue flag is set
 *
 * Blocks until a thread signals the waitqueue that the condition has
 * become true. Use osk_waitq_set() to set the waitqueue flag to signal
 * the condition has become true. If the condition is already true,
 * this function will return immediately.
 *
 * @param[in] wq  initialized waitqueue
 */
OSK_STATIC_INLINE void osk_waitq_wait(osk_waitq *wq);

/**
 * @brief Set the waitqueue flag
 *
 * Signals the waitqueue that the condition associated with the waitqueue
 * has become true. All threads on the waitqueue will be woken up. The
 * waitqueue flag is set.
 *
 * @param[in] wq  initialized waitqueue
 */
OSK_STATIC_INLINE void osk_waitq_set(osk_waitq *wq);

/**
 * @brief Clear the waitqueue flag
 *
 * Signals the waitqueue that the condition associated with the waitqueue
 * has become false. The waitqueue flag is reset (cleared).
 *
 * @param[in] wq  initialized waitqueue
 */
OSK_STATIC_INLINE void osk_waitq_clear(osk_waitq *wq);

/**
 * @brief Terminate a wait queue
 *
 * Frees any resources allocated for a waitqueue.
 *
 * No threads are allowed to be waiting on the waitqueue when terminating
 * the waitqueue. If there are waiting threads, they should be woken up 
 * first by setting the waitqueue flag with osk_waitq_set() after which
 * they must cease using the waitqueue.
 *
 * @param[in] wq  initialized waitqueue
 */
OSK_STATIC_INLINE void osk_waitq_term(osk_waitq *wq);

/* @} */ /* end group oskwaitq */

/** @} */ /* end group base_osk_api */

/** @} */ /* end group base_api */

/* pull in the arch header with the implementation  */
#include <osk/mali_osk_arch_waitq.h>

#ifdef __cplusplus
}
#endif

#endif /* _OSK_WAITQ_H_ */
