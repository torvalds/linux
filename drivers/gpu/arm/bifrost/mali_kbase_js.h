/*
 *
 * (C) COPYRIGHT 2011-2017 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */



/**
 * @file mali_kbase_js.h
 * Job Scheduler APIs.
 */

#ifndef _KBASE_JS_H_
#define _KBASE_JS_H_

#include "mali_kbase_js_defs.h"
#include "mali_kbase_context.h"
#include "mali_kbase_defs.h"
#include "mali_kbase_debug.h"

#include "mali_kbase_js_ctx_attr.h"

/**
 * @addtogroup base_api
 * @{
 */

/**
 * @addtogroup base_kbase_api
 * @{
 */

/**
 * @addtogroup kbase_js Job Scheduler Internal APIs
 * @{
 *
 * These APIs are Internal to KBase.
 */

/**
 * @brief Initialize the Job Scheduler
 *
 * The struct kbasep_js_device_data sub-structure of \a kbdev must be zero
 * initialized before passing to the kbasep_js_devdata_init() function. This is
 * to give efficient error path code.
 */
int kbasep_js_devdata_init(struct kbase_device * const kbdev);

/**
 * @brief Halt the Job Scheduler.
 *
 * It is safe to call this on \a kbdev even if it the kbasep_js_device_data
 * sub-structure was never initialized/failed initialization, to give efficient
 * error-path code.
 *
 * For this to work, the struct kbasep_js_device_data sub-structure of \a kbdev must
 * be zero initialized before passing to the kbasep_js_devdata_init()
 * function. This is to give efficient error path code.
 *
 * It is a Programming Error to call this whilst there are still kbase_context
 * structures registered with this scheduler.
 *
 */
void kbasep_js_devdata_halt(struct kbase_device *kbdev);

/**
 * @brief Terminate the Job Scheduler
 *
 * It is safe to call this on \a kbdev even if it the kbasep_js_device_data
 * sub-structure was never initialized/failed initialization, to give efficient
 * error-path code.
 *
 * For this to work, the struct kbasep_js_device_data sub-structure of \a kbdev must
 * be zero initialized before passing to the kbasep_js_devdata_init()
 * function. This is to give efficient error path code.
 *
 * It is a Programming Error to call this whilst there are still kbase_context
 * structures registered with this scheduler.
 */
void kbasep_js_devdata_term(struct kbase_device *kbdev);

/**
 * @brief Initialize the Scheduling Component of a struct kbase_context on the Job Scheduler.
 *
 * This effectively registers a struct kbase_context with a Job Scheduler.
 *
 * It does not register any jobs owned by the struct kbase_context with the scheduler.
 * Those must be separately registered by kbasep_js_add_job().
 *
 * The struct kbase_context must be zero intitialized before passing to the
 * kbase_js_init() function. This is to give efficient error path code.
 */
int kbasep_js_kctx_init(struct kbase_context * const kctx);

/**
 * @brief Terminate the Scheduling Component of a struct kbase_context on the Job Scheduler
 *
 * This effectively de-registers a struct kbase_context from its Job Scheduler
 *
 * It is safe to call this on a struct kbase_context that has never had or failed
 * initialization of its jctx.sched_info member, to give efficient error-path
 * code.
 *
 * For this to work, the struct kbase_context must be zero intitialized before passing
 * to the kbase_js_init() function.
 *
 * It is a Programming Error to call this whilst there are still jobs
 * registered with this context.
 */
void kbasep_js_kctx_term(struct kbase_context *kctx);

/**
 * @brief Add a job chain to the Job Scheduler, and take necessary actions to
 * schedule the context/run the job.
 *
 * This atomically does the following:
 * - Update the numbers of jobs information
 * - Add the job to the run pool if necessary (part of init_job)
 *
 * Once this is done, then an appropriate action is taken:
 * - If the ctx is scheduled, it attempts to start the next job (which might be
 * this added job)
 * - Otherwise, and if this is the first job on the context, it enqueues it on
 * the Policy Queue
 *
 * The Policy's Queue can be updated by this in the following ways:
 * - In the above case that this is the first job on the context
 * - If the context is high priority and the context is not scheduled, then it
 * could cause the Policy to schedule out a low-priority context, allowing
 * this context to be scheduled in.
 *
 * If the context is already scheduled on the RunPool, then adding a job to it
 * is guarenteed not to update the Policy Queue. And so, the caller is
 * guarenteed to not need to try scheduling a context from the Run Pool - it
 * can safely assert that the result is false.
 *
 * It is a programming error to have more than U32_MAX jobs in flight at a time.
 *
 * The following locking conditions are made on the caller:
 * - it must \em not hold kbasep_js_kctx_info::ctx::jsctx_mutex.
 * - it must \em not hold hwaccess_lock (as this will be obtained internally)
 * - it must \em not hold kbasep_js_device_data::runpool_mutex (as this will be
 * obtained internally)
 * - it must \em not hold kbasep_jd_device_data::queue_mutex (again, it's used internally).
 *
 * @return true indicates that the Policy Queue was updated, and so the
 * caller will need to try scheduling a context onto the Run Pool.
 * @return false indicates that no updates were made to the Policy Queue,
 * so no further action is required from the caller. This is \b always returned
 * when the context is currently scheduled.
 */
bool kbasep_js_add_job(struct kbase_context *kctx, struct kbase_jd_atom *atom);

/**
 * @brief Remove a job chain from the Job Scheduler, except for its 'retained state'.
 *
 * Completely removing a job requires several calls:
 * - kbasep_js_copy_atom_retained_state(), to capture the 'retained state' of
 *   the atom
 * - kbasep_js_remove_job(), to partially remove the atom from the Job Scheduler
 * - kbasep_js_runpool_release_ctx_and_katom_retained_state(), to release the
 *   remaining state held as part of the job having been run.
 *
 * In the common case of atoms completing normally, this set of actions is more optimal for spinlock purposes than having kbasep_js_remove_job() handle all of the actions.
 *
 * In the case of cancelling atoms, it is easier to call kbasep_js_remove_cancelled_job(), which handles all the necessary actions.
 *
 * It is a programming error to call this when:
 * - \a atom is not a job belonging to kctx.
 * - \a atom has already been removed from the Job Scheduler.
 * - \a atom is still in the runpool
 *
 * Do not use this for removing jobs being killed by kbase_jd_cancel() - use
 * kbasep_js_remove_cancelled_job() instead.
 *
 * The following locking conditions are made on the caller:
 * - it must hold kbasep_js_kctx_info::ctx::jsctx_mutex.
 *
 */
void kbasep_js_remove_job(struct kbase_device *kbdev, struct kbase_context *kctx, struct kbase_jd_atom *atom);

/**
 * @brief Completely remove a job chain from the Job Scheduler, in the case
 * where the job chain was cancelled.
 *
 * This is a variant of kbasep_js_remove_job() that takes care of removing all
 * of the retained state too. This is generally useful for cancelled atoms,
 * which need not be handled in an optimal way.
 *
 * It is a programming error to call this when:
 * - \a atom is not a job belonging to kctx.
 * - \a atom has already been removed from the Job Scheduler.
 * - \a atom is still in the runpool:
 *  - it is not being killed with kbasep_jd_cancel()
 *
 * The following locking conditions are made on the caller:
 * - it must hold kbasep_js_kctx_info::ctx::jsctx_mutex.
 * - it must \em not hold the hwaccess_lock, (as this will be obtained
 *   internally)
 * - it must \em not hold kbasep_js_device_data::runpool_mutex (as this could be
 * obtained internally)
 *
 * @return true indicates that ctx attributes have changed and the caller
 * should call kbase_js_sched_all() to try to run more jobs
 * @return false otherwise
 */
bool kbasep_js_remove_cancelled_job(struct kbase_device *kbdev,
						struct kbase_context *kctx,
						struct kbase_jd_atom *katom);

/**
 * @brief Refcount a context as being busy, preventing it from being scheduled
 * out.
 *
 * @note This function can safely be called from IRQ context.
 *
 * The following locking conditions are made on the caller:
 * - it must \em not hold mmu_hw_mutex and hwaccess_lock, because they will be
 *   used internally.
 *
 * @return value != false if the retain succeeded, and the context will not be scheduled out.
 * @return false if the retain failed (because the context is being/has been scheduled out).
 */
bool kbasep_js_runpool_retain_ctx(struct kbase_device *kbdev, struct kbase_context *kctx);

/**
 * @brief Refcount a context as being busy, preventing it from being scheduled
 * out.
 *
 * @note This function can safely be called from IRQ context.
 *
 * The following locks must be held by the caller:
 * - mmu_hw_mutex, hwaccess_lock
 *
 * @return value != false if the retain succeeded, and the context will not be scheduled out.
 * @return false if the retain failed (because the context is being/has been scheduled out).
 */
bool kbasep_js_runpool_retain_ctx_nolock(struct kbase_device *kbdev, struct kbase_context *kctx);

/**
 * @brief Lookup a context in the Run Pool based upon its current address space
 * and ensure that is stays scheduled in.
 *
 * The context is refcounted as being busy to prevent it from scheduling
 * out. It must be released with kbasep_js_runpool_release_ctx() when it is no
 * longer required to stay scheduled in.
 *
 * @note This function can safely be called from IRQ context.
 *
 * The following locking conditions are made on the caller:
 * - it must \em not hold the hwaccess_lock, because it will be used internally.
 *   If the hwaccess_lock is already held, then the caller should use
 *   kbasep_js_runpool_lookup_ctx_nolock() instead.
 *
 * @return a valid struct kbase_context on success, which has been refcounted as being busy.
 * @return NULL on failure, indicating that no context was found in \a as_nr
 */
struct kbase_context *kbasep_js_runpool_lookup_ctx(struct kbase_device *kbdev, int as_nr);

/**
 * @brief Handling the requeuing/killing of a context that was evicted from the
 * policy queue or runpool.
 *
 * This should be used whenever handing off a context that has been evicted
 * from the policy queue or the runpool:
 * - If the context is not dying and has jobs, it gets re-added to the policy
 * queue
 * - Otherwise, it is not added
 *
 * In addition, if the context is dying the jobs are killed asynchronously.
 *
 * In all cases, the Power Manager active reference is released
 * (kbase_pm_context_idle()) whenever the has_pm_ref parameter is true.  \a
 * has_pm_ref must be set to false whenever the context was not previously in
 * the runpool and does not hold a Power Manager active refcount. Note that
 * contexts in a rollback of kbasep_js_try_schedule_head_ctx() might have an
 * active refcount even though they weren't in the runpool.
 *
 * The following locking conditions are made on the caller:
 * - it must hold kbasep_js_kctx_info::ctx::jsctx_mutex.
 * - it must \em not hold kbasep_jd_device_data::queue_mutex (as this will be
 * obtained internally)
 */
void kbasep_js_runpool_requeue_or_kill_ctx(struct kbase_device *kbdev, struct kbase_context *kctx, bool has_pm_ref);

/**
 * @brief Release a refcount of a context being busy, allowing it to be
 * scheduled out.
 *
 * When the refcount reaches zero and the context \em might be scheduled out
 * (depending on whether the Scheudling Policy has deemed it so, or if it has run
 * out of jobs).
 *
 * If the context does get scheduled out, then The following actions will be
 * taken as part of deschduling a context:
 * - For the context being descheduled:
 *  - If the context is in the processing of dying (all the jobs are being
 * removed from it), then descheduling also kills off any jobs remaining in the
 * context.
 *  - If the context is not dying, and any jobs remain after descheduling the
 * context then it is re-enqueued to the Policy's Queue.
 *  - Otherwise, the context is still known to the scheduler, but remains absent
 * from the Policy Queue until a job is next added to it.
 *  - In all descheduling cases, the Power Manager active reference (obtained
 * during kbasep_js_try_schedule_head_ctx()) is released (kbase_pm_context_idle()).
 *
 * Whilst the context is being descheduled, this also handles actions that
 * cause more atoms to be run:
 * - Attempt submitting atoms when the Context Attributes on the Runpool have
 * changed. This is because the context being scheduled out could mean that
 * there are more opportunities to run atoms.
 * - Attempt submitting to a slot that was previously blocked due to affinity
 * restrictions. This is usually only necessary when releasing a context
 * happens as part of completing a previous job, but is harmless nonetheless.
 * - Attempt scheduling in a new context (if one is available), and if necessary,
 * running a job from that new context.
 *
 * Unlike retaining a context in the runpool, this function \b cannot be called
 * from IRQ context.
 *
 * It is a programming error to call this on a \a kctx that is not currently
 * scheduled, or that already has a zero refcount.
 *
 * The following locking conditions are made on the caller:
 * - it must \em not hold the hwaccess_lock, because it will be used internally.
 * - it must \em not hold kbasep_js_kctx_info::ctx::jsctx_mutex.
 * - it must \em not hold kbasep_js_device_data::runpool_mutex (as this will be
 * obtained internally)
 * - it must \em not hold the kbase_device::mmu_hw_mutex (as this will be
 * obtained internally)
 * - it must \em not hold kbasep_jd_device_data::queue_mutex (as this will be
 * obtained internally)
 *
 */
void kbasep_js_runpool_release_ctx(struct kbase_device *kbdev, struct kbase_context *kctx);

/**
 * @brief Variant of kbasep_js_runpool_release_ctx() that handles additional
 * actions from completing an atom.
 *
 * This is usually called as part of completing an atom and releasing the
 * refcount on the context held by the atom.
 *
 * Therefore, the extra actions carried out are part of handling actions queued
 * on a completed atom, namely:
 * - Releasing the atom's context attributes
 * - Retrying the submission on a particular slot, because we couldn't submit
 * on that slot from an IRQ handler.
 *
 * The locking conditions of this function are the same as those for
 * kbasep_js_runpool_release_ctx()
 */
void kbasep_js_runpool_release_ctx_and_katom_retained_state(struct kbase_device *kbdev, struct kbase_context *kctx, struct kbasep_js_atom_retained_state *katom_retained_state);

/**
 * @brief Variant of kbase_js_runpool_release_ctx() that assumes that
 * kbasep_js_device_data::runpool_mutex and
 * kbasep_js_kctx_info::ctx::jsctx_mutex are held by the caller, and does not
 * attempt to schedule new contexts.
 */
void kbasep_js_runpool_release_ctx_nolock(struct kbase_device *kbdev,
						struct kbase_context *kctx);

/**
 * @brief Schedule in a privileged context
 *
 * This schedules a context in regardless of the context priority.
 * If the runpool is full, a context will be forced out of the runpool and the function will wait
 * for the new context to be scheduled in.
 * The context will be kept scheduled in (and the corresponding address space reserved) until
 * kbasep_js_release_privileged_ctx is called).
 *
 * The following locking conditions are made on the caller:
 * - it must \em not hold the hwaccess_lock, because it will be used internally.
 * - it must \em not hold kbasep_js_device_data::runpool_mutex (as this will be
 * obtained internally)
 * - it must \em not hold the kbase_device::mmu_hw_mutex (as this will be
 * obtained internally)
 * - it must \em not hold kbasep_jd_device_data::queue_mutex (again, it's used internally).
 * - it must \em not hold kbasep_js_kctx_info::ctx::jsctx_mutex, because it will
 * be used internally.
 *
 */
void kbasep_js_schedule_privileged_ctx(struct kbase_device *kbdev, struct kbase_context *kctx);

/**
 * @brief Release a privileged context, allowing it to be scheduled out.
 *
 * See kbasep_js_runpool_release_ctx for potential side effects.
 *
 * The following locking conditions are made on the caller:
 * - it must \em not hold the hwaccess_lock, because it will be used internally.
 * - it must \em not hold kbasep_js_kctx_info::ctx::jsctx_mutex.
 * - it must \em not hold kbasep_js_device_data::runpool_mutex (as this will be
 * obtained internally)
 * - it must \em not hold the kbase_device::mmu_hw_mutex (as this will be
 * obtained internally)
 *
 */
void kbasep_js_release_privileged_ctx(struct kbase_device *kbdev, struct kbase_context *kctx);

/**
 * @brief Try to submit the next job on each slot
 *
 * The following locks may be used:
 * - kbasep_js_device_data::runpool_mutex
 * - hwaccess_lock
 */
void kbase_js_try_run_jobs(struct kbase_device *kbdev);

/**
 * @brief Suspend the job scheduler during a Power Management Suspend event.
 *
 * Causes all contexts to be removed from the runpool, and prevents any
 * contexts from (re)entering the runpool.
 *
 * This does not handle suspending the one privileged context: the caller must
 * instead do this by by suspending the GPU HW Counter Instrumentation.
 *
 * This will eventually cause all Power Management active references held by
 * contexts on the runpool to be released, without running any more atoms.
 *
 * The caller must then wait for all Power Mangement active refcount to become
 * zero before completing the suspend.
 *
 * The emptying mechanism may take some time to complete, since it can wait for
 * jobs to complete naturally instead of forcing them to end quickly. However,
 * this is bounded by the Job Scheduler's Job Timeouts. Hence, this
 * function is guaranteed to complete in a finite time.
 */
void kbasep_js_suspend(struct kbase_device *kbdev);

/**
 * @brief Resume the Job Scheduler after a Power Management Resume event.
 *
 * This restores the actions from kbasep_js_suspend():
 * - Schedules contexts back into the runpool
 * - Resumes running atoms on the GPU
 */
void kbasep_js_resume(struct kbase_device *kbdev);

/**
 * @brief Submit an atom to the job scheduler.
 *
 * The atom is enqueued on the context's ringbuffer. The caller must have
 * ensured that all dependencies can be represented in the ringbuffer.
 *
 * Caller must hold jctx->lock
 *
 * @param[in] kctx  Context pointer
 * @param[in] atom  Pointer to the atom to submit
 *
 * @return Whether the context requires to be enqueued. */
bool kbase_js_dep_resolved_submit(struct kbase_context *kctx,
					struct kbase_jd_atom *katom);

/**
  * jsctx_ll_flush_to_rb() - Pushes atoms from the linked list to ringbuffer.
  * @kctx:  Context Pointer
  * @prio:  Priority (specifies the queue together with js).
  * @js:    Job slot (specifies the queue together with prio).
  *
  * Pushes all possible atoms from the linked list to the ringbuffer.
  * Number of atoms are limited to free space in the ringbuffer and
  * number of available atoms in the linked list.
  *
  */
void jsctx_ll_flush_to_rb(struct kbase_context *kctx, int prio, int js);
/**
 * @brief Pull an atom from a context in the job scheduler for execution.
 *
 * The atom will not be removed from the ringbuffer at this stage.
 *
 * The HW access lock must be held when calling this function.
 *
 * @param[in] kctx  Context to pull from
 * @param[in] js    Job slot to pull from
 * @return          Pointer to an atom, or NULL if there are no atoms for this
 *                  slot that can be currently run.
 */
struct kbase_jd_atom *kbase_js_pull(struct kbase_context *kctx, int js);

/**
 * @brief Return an atom to the job scheduler ringbuffer.
 *
 * An atom is 'unpulled' if execution is stopped but intended to be returned to
 * later. The most common reason for this is that the atom has been
 * soft-stopped.
 *
 * Note that if multiple atoms are to be 'unpulled', they must be returned in
 * the reverse order to which they were originally pulled. It is a programming
 * error to return atoms in any other order.
 *
 * The HW access lock must be held when calling this function.
 *
 * @param[in] kctx  Context pointer
 * @param[in] atom  Pointer to the atom to unpull
 */
void kbase_js_unpull(struct kbase_context *kctx, struct kbase_jd_atom *katom);

/**
 * @brief Complete an atom from jd_done_worker(), removing it from the job
 * scheduler ringbuffer.
 *
 * If the atom failed then all dependee atoms marked for failure propagation
 * will also fail.
 *
 * @param[in] kctx  Context pointer
 * @param[in] katom Pointer to the atom to complete
 * @return true if the context is now idle (no jobs pulled)
 *         false otherwise
 */
bool kbase_js_complete_atom_wq(struct kbase_context *kctx,
				struct kbase_jd_atom *katom);

/**
 * @brief Complete an atom.
 *
 * Most of the work required to complete an atom will be performed by
 * jd_done_worker().
 *
 * The HW access lock must be held when calling this function.
 *
 * @param[in] katom         Pointer to the atom to complete
 * @param[in] end_timestamp The time that the atom completed (may be NULL)
 *
 * Return: Atom that has now been unblocked and can now be run, or NULL if none
 */
struct kbase_jd_atom *kbase_js_complete_atom(struct kbase_jd_atom *katom,
		ktime_t *end_timestamp);

/**
 * @brief Submit atoms from all available contexts.
 *
 * This will attempt to submit as many jobs as possible to the provided job
 * slots. It will exit when either all job slots are full, or all contexts have
 * been used.
 *
 * @param[in] kbdev    Device pointer
 * @param[in] js_mask  Mask of job slots to submit to
 */
void kbase_js_sched(struct kbase_device *kbdev, int js_mask);

/**
 * kbase_jd_zap_context - Attempt to deschedule a context that is being
 *                        destroyed
 * @kctx: Context pointer
 *
 * This will attempt to remove a context from any internal job scheduler queues
 * and perform any other actions to ensure a context will not be submitted
 * from.
 *
 * If the context is currently scheduled, then the caller must wait for all
 * pending jobs to complete before taking any further action.
 */
void kbase_js_zap_context(struct kbase_context *kctx);

/**
 * @brief Validate an atom
 *
 * This will determine whether the atom can be scheduled onto the GPU. Atoms
 * with invalid combinations of core requirements will be rejected.
 *
 * @param[in] kbdev  Device pointer
 * @param[in] katom  Atom to validate
 * @return           true if atom is valid
 *                   false otherwise
 */
bool kbase_js_is_atom_valid(struct kbase_device *kbdev,
				struct kbase_jd_atom *katom);

/**
 * kbase_js_set_timeouts - update all JS timeouts with user specified data
 * @kbdev: Device pointer
 *
 * Timeouts are specified through the 'js_timeouts' sysfs file. If a timeout is
 * set to a positive number then that becomes the new value used, if a timeout
 * is negative then the default is set.
 */
void kbase_js_set_timeouts(struct kbase_device *kbdev);

/*
 * Helpers follow
 */

/**
 * @brief Check that a context is allowed to submit jobs on this policy
 *
 * The purpose of this abstraction is to hide the underlying data size, and wrap up
 * the long repeated line of code.
 *
 * As with any bool, never test the return value with true.
 *
 * The caller must hold hwaccess_lock.
 */
static inline bool kbasep_js_is_submit_allowed(struct kbasep_js_device_data *js_devdata, struct kbase_context *kctx)
{
	u16 test_bit;

	/* Ensure context really is scheduled in */
	KBASE_DEBUG_ASSERT(kctx->as_nr != KBASEP_AS_NR_INVALID);
	KBASE_DEBUG_ASSERT(kbase_ctx_flag(kctx, KCTX_SCHEDULED));

	test_bit = (u16) (1u << kctx->as_nr);

	return (bool) (js_devdata->runpool_irq.submit_allowed & test_bit);
}

/**
 * @brief Allow a context to submit jobs on this policy
 *
 * The purpose of this abstraction is to hide the underlying data size, and wrap up
 * the long repeated line of code.
 *
 * The caller must hold hwaccess_lock.
 */
static inline void kbasep_js_set_submit_allowed(struct kbasep_js_device_data *js_devdata, struct kbase_context *kctx)
{
	u16 set_bit;

	/* Ensure context really is scheduled in */
	KBASE_DEBUG_ASSERT(kctx->as_nr != KBASEP_AS_NR_INVALID);
	KBASE_DEBUG_ASSERT(kbase_ctx_flag(kctx, KCTX_SCHEDULED));

	set_bit = (u16) (1u << kctx->as_nr);

	dev_dbg(kctx->kbdev->dev, "JS: Setting Submit Allowed on %p (as=%d)", kctx, kctx->as_nr);

	js_devdata->runpool_irq.submit_allowed |= set_bit;
}

/**
 * @brief Prevent a context from submitting more jobs on this policy
 *
 * The purpose of this abstraction is to hide the underlying data size, and wrap up
 * the long repeated line of code.
 *
 * The caller must hold hwaccess_lock.
 */
static inline void kbasep_js_clear_submit_allowed(struct kbasep_js_device_data *js_devdata, struct kbase_context *kctx)
{
	u16 clear_bit;
	u16 clear_mask;

	/* Ensure context really is scheduled in */
	KBASE_DEBUG_ASSERT(kctx->as_nr != KBASEP_AS_NR_INVALID);
	KBASE_DEBUG_ASSERT(kbase_ctx_flag(kctx, KCTX_SCHEDULED));

	clear_bit = (u16) (1u << kctx->as_nr);
	clear_mask = ~clear_bit;

	dev_dbg(kctx->kbdev->dev, "JS: Clearing Submit Allowed on %p (as=%d)", kctx, kctx->as_nr);

	js_devdata->runpool_irq.submit_allowed &= clear_mask;
}

/**
 * Create an initial 'invalid' atom retained state, that requires no
 * atom-related work to be done on releasing with
 * kbasep_js_runpool_release_ctx_and_katom_retained_state()
 */
static inline void kbasep_js_atom_retained_state_init_invalid(struct kbasep_js_atom_retained_state *retained_state)
{
	retained_state->event_code = BASE_JD_EVENT_NOT_STARTED;
	retained_state->core_req = KBASEP_JS_ATOM_RETAINED_STATE_CORE_REQ_INVALID;
}

/**
 * Copy atom state that can be made available after jd_done_nolock() is called
 * on that atom.
 */
static inline void kbasep_js_atom_retained_state_copy(struct kbasep_js_atom_retained_state *retained_state, const struct kbase_jd_atom *katom)
{
	retained_state->event_code = katom->event_code;
	retained_state->core_req = katom->core_req;
	retained_state->sched_priority = katom->sched_priority;
	retained_state->device_nr = katom->device_nr;
}

/**
 * @brief Determine whether an atom has finished (given its retained state),
 * and so should be given back to userspace/removed from the system.
 *
 * Reasons for an atom not finishing include:
 * - Being soft-stopped (and so, the atom should be resubmitted sometime later)
 *
 * @param[in] katom_retained_state the retained state of the atom to check
 * @return    false if the atom has not finished
 * @return    !=false if the atom has finished
 */
static inline bool kbasep_js_has_atom_finished(const struct kbasep_js_atom_retained_state *katom_retained_state)
{
	return (bool) (katom_retained_state->event_code != BASE_JD_EVENT_STOPPED && katom_retained_state->event_code != BASE_JD_EVENT_REMOVED_FROM_NEXT);
}

/**
 * @brief Determine whether a struct kbasep_js_atom_retained_state is valid
 *
 * An invalid struct kbasep_js_atom_retained_state is allowed, and indicates that the
 * code should just ignore it.
 *
 * @param[in] katom_retained_state the atom's retained state to check
 * @return    false if the retained state is invalid, and can be ignored
 * @return    !=false if the retained state is valid
 */
static inline bool kbasep_js_atom_retained_state_is_valid(const struct kbasep_js_atom_retained_state *katom_retained_state)
{
	return (bool) (katom_retained_state->core_req != KBASEP_JS_ATOM_RETAINED_STATE_CORE_REQ_INVALID);
}

/**
 * @brief Variant of kbasep_js_runpool_lookup_ctx() that can be used when the
 * context is guaranteed to be already previously retained.
 *
 * It is a programming error to supply the \a as_nr of a context that has not
 * been previously retained/has a busy refcount of zero. The only exception is
 * when there is no ctx in \a as_nr (NULL returned).
 *
 * The following locking conditions are made on the caller:
 * - it must \em not hold the hwaccess_lock, because it will be used internally.
 *
 * @return a valid struct kbase_context on success, with a refcount that is guaranteed
 * to be non-zero and unmodified by this function.
 * @return NULL on failure, indicating that no context was found in \a as_nr
 */
static inline struct kbase_context *kbasep_js_runpool_lookup_ctx_noretain(struct kbase_device *kbdev, int as_nr)
{
	struct kbase_context *found_kctx;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(0 <= as_nr && as_nr < BASE_MAX_NR_AS);

	found_kctx = kbdev->as_to_kctx[as_nr];
	KBASE_DEBUG_ASSERT(found_kctx == NULL ||
			atomic_read(&found_kctx->refcount) > 0);

	return found_kctx;
}

/*
 * The following locking conditions are made on the caller:
 * - The caller must hold the kbasep_js_kctx_info::ctx::jsctx_mutex.
 * - The caller must hold the kbasep_js_device_data::runpool_mutex
 */
static inline void kbase_js_runpool_inc_context_count(
						struct kbase_device *kbdev,
						struct kbase_context *kctx)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	lockdep_assert_held(&js_kctx_info->ctx.jsctx_mutex);
	lockdep_assert_held(&js_devdata->runpool_mutex);

	/* Track total contexts */
	KBASE_DEBUG_ASSERT(js_devdata->nr_all_contexts_running < S8_MAX);
	++(js_devdata->nr_all_contexts_running);

	if (!kbase_ctx_flag(kctx, KCTX_SUBMIT_DISABLED)) {
		/* Track contexts that can submit jobs */
		KBASE_DEBUG_ASSERT(js_devdata->nr_user_contexts_running <
									S8_MAX);
		++(js_devdata->nr_user_contexts_running);
	}
}

/*
 * The following locking conditions are made on the caller:
 * - The caller must hold the kbasep_js_kctx_info::ctx::jsctx_mutex.
 * - The caller must hold the kbasep_js_device_data::runpool_mutex
 */
static inline void kbase_js_runpool_dec_context_count(
						struct kbase_device *kbdev,
						struct kbase_context *kctx)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(kctx != NULL);

	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	lockdep_assert_held(&js_kctx_info->ctx.jsctx_mutex);
	lockdep_assert_held(&js_devdata->runpool_mutex);

	/* Track total contexts */
	--(js_devdata->nr_all_contexts_running);
	KBASE_DEBUG_ASSERT(js_devdata->nr_all_contexts_running >= 0);

	if (!kbase_ctx_flag(kctx, KCTX_SUBMIT_DISABLED)) {
		/* Track contexts that can submit jobs */
		--(js_devdata->nr_user_contexts_running);
		KBASE_DEBUG_ASSERT(js_devdata->nr_user_contexts_running >= 0);
	}
}


/**
 * @brief Submit atoms from all available contexts to all job slots.
 *
 * This will attempt to submit as many jobs as possible. It will exit when
 * either all job slots are full, or all contexts have been used.
 *
 * @param[in] kbdev    Device pointer
 */
static inline void kbase_js_sched_all(struct kbase_device *kbdev)
{
	kbase_js_sched(kbdev, (1 << kbdev->gpu_props.num_job_slots) - 1);
}

extern const int
kbasep_js_atom_priority_to_relative[BASE_JD_NR_PRIO_LEVELS];

extern const base_jd_prio
kbasep_js_relative_priority_to_atom[KBASE_JS_ATOM_SCHED_PRIO_COUNT];

/**
 * kbasep_js_atom_prio_to_sched_prio(): - Convert atom priority (base_jd_prio)
 *                                        to relative ordering
 * @atom_prio: Priority ID to translate.
 *
 * Atom priority values for @ref base_jd_prio cannot be compared directly to
 * find out which are higher or lower.
 *
 * This function will convert base_jd_prio values for successively lower
 * priorities into a monotonically increasing sequence. That is, the lower the
 * base_jd_prio priority, the higher the value produced by this function. This
 * is in accordance with how the rest of the kernel treates priority.
 *
 * The mapping is 1:1 and the size of the valid input range is the same as the
 * size of the valid output range, i.e.
 * KBASE_JS_ATOM_SCHED_PRIO_COUNT == BASE_JD_NR_PRIO_LEVELS
 *
 * Note This must be kept in sync with BASE_JD_PRIO_<...> definitions
 *
 * Return: On success: a value in the inclusive range
 *         0..KBASE_JS_ATOM_SCHED_PRIO_COUNT-1. On failure:
 *         KBASE_JS_ATOM_SCHED_PRIO_INVALID
 */
static inline int kbasep_js_atom_prio_to_sched_prio(base_jd_prio atom_prio)
{
	if (atom_prio >= BASE_JD_NR_PRIO_LEVELS)
		return KBASE_JS_ATOM_SCHED_PRIO_INVALID;

	return kbasep_js_atom_priority_to_relative[atom_prio];
}

static inline base_jd_prio kbasep_js_sched_prio_to_atom_prio(int sched_prio)
{
	unsigned int prio_idx;

	KBASE_DEBUG_ASSERT(0 <= sched_prio
			&& sched_prio < KBASE_JS_ATOM_SCHED_PRIO_COUNT);

	prio_idx = (unsigned int)sched_prio;

	return kbasep_js_relative_priority_to_atom[prio_idx];
}

	  /** @} *//* end group kbase_js */
	  /** @} *//* end group base_kbase_api */
	  /** @} *//* end group base_api */

#endif				/* _KBASE_JS_H_ */
