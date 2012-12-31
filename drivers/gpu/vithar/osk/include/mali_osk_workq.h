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



#ifndef _OSK_WORKQ_H
#define _OSK_WORKQ_H

#ifndef _OSK_H_
#error "Include mali_osk.h directly"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/* pull in the arch header with the implementation  */
#include <osk/mali_osk_arch_workq.h>

/**
 * @addtogroup base_api
 * @{
 */

/**
 * @addtogroup base_osk_api
 * @{
 */

/**
 * @addtogroup oskworkqueue Work queue
 *  
 * A workqueue is a queue of functions that will be invoked by one or more worker threads
 * at some future time. Functions are invoked in FIFO order by each worker thread. However,
 * overall execution of work is <b>not guaranteed to occur in FIFO order</b>, because two or
 * more worker threads may be processing items concurrently from the same work queue.
 *
 * Each function that is submitted to the workqueue needs to be represented by a work unit
 * (osk_workq_work). When a function is invoked, a pointer to the work unit is passed to the
 * invoked function. A work unit needs to be embedded within the object that the invoked
 * function needs to operate on, so that the invoked function can determine a pointer to the
 * object it needs to operate on.
 *
 * @{
 */

/**
 * @brief Initialize a work queue
 *
 * Initializes an empty work queue. One or more threads within the system will
 * be servicing the work units submitted to the work queue. 
 * 
 * It is a programming error to pass an invalid pointer (including NULL) for the
 * wq parameter.  Passing NULL will assert in debug builds.
 * 
 * It is a programming error to pass an invalid pointer (including NULL) for the
 * name parameter.  Passing NULL will assert in debug builds.
 *
 * It is a programming error to pass a value for flags other than a combination
 * of the OSK_WORK_ constants or 0. Doing so will assert in debug builds.
 *
 * @param[out] wq    workqueue to initialize
 * @param[in] name   The name for the queue (may be visible in the process list)
 * @param[in] flags  flags specifying behavior of work queue, see OSK_WORKQ_ constants.
 * @return OSK_ERR_NONE on success. Any other value indicates failure.
 */
OSK_STATIC_INLINE osk_error osk_workq_init(osk_workq * const wq, const char *name, u32 flags) CHECK_RESULT;

/**
 * @brief Terminate a work queue
 *
 * Stops accepting new work and waits until the work queue is empty and 
 * all work has been completed, then frees any resources allocated for the workqueue.
 *
 * @param[in] wq    intialized workqueue
 */
OSK_STATIC_INLINE void osk_workq_term(osk_workq *wq);

/**
 * @brief (Re)initialize a work object
 *
 * Sets up a work object to call the given function pointer.
 * See \a osk_workq_work_init_on_stack if the work object
 * is a stack object
 * The function \a fn needs to match the prototype: void fn(osk_workq_work *).
 *
 * It is a programming error to pass an invalid pointer (including NULL) for
 * any parameter.  Passing NULL will assert in debug builds.
 *
 * @param[out] wk  work unit to be initialized
 * @param[in]  fn  function to be invoked at some future time
 */
OSK_STATIC_INLINE void osk_workq_work_init(osk_workq_work * const wk, osk_workq_fn fn);

/**
 * @brief (Re)initialize a work object allocated on the stack
 *
 * Sets up a work object to call the given function pointer.
 * Special version needed for work objects on the stack.
 * The function \a fn needs to match the prototype: void fn(osk_workq_work *).
 *
 * It is a programming error to pass an invalid pointer (including NULL) for
 * any parameter.  Passing NULL will assert in debug builds.
 *
 * @param[out] wk  work unit to be initialized
 * @param[in]  fn  function to be invoked at some future time
 */
OSK_STATIC_INLINE void osk_workq_work_init_on_stack(osk_workq_work * const wk, osk_workq_fn fn);


/**
 * @brief Submit work to a work queue
 *
 * Adds work (a work unit) to a work queue. 
 *
 * The work unit (osk_workq_work) represents a function \a fn to be invoked at some
 * future time. The invoked function \a fn is set via \a osk_workq_work_init or
 * \a osk_workq_work_init_on_stack if the work object resides on the stack.
 *
 * The work unit should be embedded within the object that the invoked function needs
 * to operate on, so that the invoked function can determine a pointer to the object
 * it needs to operate on.
 *
 * osk_workq_submit() must be callable from IRQ context (it may not block nor access user space)
 *
 * The work unit memory \a wk needs to remain allocated until the function \a fn has been invoked.
 *
 * It is a programming error to pass an invalid pointer (including NULL) for
 * any parameter.  Passing NULL will assert in debug builds.
 *
 * @param[in] wq   intialized workqueue
 * @param[out] wk  initialized work object to submit
 */
OSK_STATIC_INLINE void osk_workq_submit(osk_workq *wq, osk_workq_work * const wk);

/**
 * @brief Flush a work queue
 *
 * All work units submitted to \a wq before this call will be complete by the
 * time this function returns. The work units are guaranteed to be completed
 * across the pool of worker threads.
 *
 * However, if a thread submits new work units to \a wq during the flush, then
 * this function will not prevent those work units from running, nor will it
 * guarantee to wait until after those work units are complete.
 *
 * Providing that no other thread attempts to submit work units to \a wq during
 * or after this call, then it is guaranteed that no worker thread is executing
 * any work from \a wq.
 *
 * @note The caller must ensure that they hold no locks that are also obtained
 * by any work units on \a wq. Otherwise, a deadlock \b will occur.
 *
 * @note In addition, you must never call osk_workq_flush() from within any
 * work unit, since this would cause a deadlock. Whilst it would normally be
 * possible for a work unit to flush a different work queue, this may still
 * cause a deadlock when the underlying implementation is using a single
 * work queue for all work queues in the system.
 *
 * It is a programming error to pass an invalid pointer (including NULL) for
 * any parameter.  Passing NULL will assert in debug builds.
 *
 * @param[in] wq   intialized workqueue to flush
 */
OSK_STATIC_INLINE void osk_workq_flush(osk_workq *wq);

/** @} */ /* end group oskworkqueue */

/** @} */ /* end group base_osk_api */

/** @} */ /* end group base_api */


#ifdef __cplusplus
}
#endif

#endif /* _OSK_WORKQ_H */
