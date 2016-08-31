/*
 *
 * (C) COPYRIGHT 2011-2016 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */





/*
 * Affinity Manager internal APIs.
 */

#ifndef _KBASE_JS_AFFINITY_H_
#define _KBASE_JS_AFFINITY_H_

/**
 * kbase_js_can_run_job_on_slot_no_lock - Decide whether it is possible to
 * submit a job to a particular job slot in the current status
 *
 * @kbdev: The kbase device structure of the device
 * @js:    Job slot number to check for allowance
 *
 * Will check if submitting to the given job slot is allowed in the current
 * status.  For example using job slot 2 while in soft-stoppable state and only
 * having 1 coregroup is not allowed by the policy. This function should be
 * called prior to submitting a job to a slot to make sure policy rules are not
 * violated.
 *
 * The following locking conditions are made on the caller
 * - it must hold kbasep_js_device_data.runpool_irq.lock
 */
bool kbase_js_can_run_job_on_slot_no_lock(struct kbase_device *kbdev,
									int js);

/**
 * kbase_js_choose_affinity - Compute affinity for a given job.
 *
 * @affinity: Affinity bitmap computed
 * @kbdev:    The kbase device structure of the device
 * @katom:    Job chain of which affinity is going to be found
 * @js:       Slot the job chain is being submitted
 *
 * Currently assumes an all-on/all-off power management policy.
 * Also assumes there is at least one core with tiler available.
 *
 * Returns true if a valid affinity was chosen, false if
 * no cores were available.
 */
bool kbase_js_choose_affinity(u64 * const affinity,
					struct kbase_device *kbdev,
					struct kbase_jd_atom *katom,
					int js);

/**
 * kbase_js_affinity_would_violate - Determine whether a proposed affinity on
 * job slot @js would cause a violation of affinity restrictions.
 *
 * @kbdev:    Kbase device structure
 * @js:       The job slot to test
 * @affinity: The affinity mask to test
 *
 * The following locks must be held by the caller
 * - kbasep_js_device_data.runpool_irq.lock
 *
 * Return: true if the affinity would violate the restrictions
 */
bool kbase_js_affinity_would_violate(struct kbase_device *kbdev, int js,
								u64 affinity);

/**
 * kbase_js_affinity_retain_slot_cores - Affinity tracking: retain cores used by
 *                                       a slot
 *
 * @kbdev:    Kbase device structure
 * @js:       The job slot retaining the cores
 * @affinity: The cores to retain
 *
 * The following locks must be held by the caller
 * - kbasep_js_device_data.runpool_irq.lock
 */
void kbase_js_affinity_retain_slot_cores(struct kbase_device *kbdev, int js,
								u64 affinity);

/**
 * kbase_js_affinity_release_slot_cores - Affinity tracking: release cores used
 *                                        by a slot
 *
 * @kbdev:    Kbase device structure
 * @js:       Job slot
 * @affinity: Bit mask of core to be released
 *
 * Cores must be released as soon as a job is dequeued from a slot's 'submit
 * slots', and before another job is submitted to those slots. Otherwise, the
 * refcount could exceed the maximum number submittable to a slot,
 * %BASE_JM_SUBMIT_SLOTS.
 *
 * The following locks must be held by the caller
 * - kbasep_js_device_data.runpool_irq.lock
 */
void kbase_js_affinity_release_slot_cores(struct kbase_device *kbdev, int js,
								u64 affinity);

/**
 * kbase_js_debug_log_current_affinities - log the current affinities
 *
 * @kbdev:  Kbase device structure
 *
 * Output to the Trace log the current tracked affinities on all slots
 */
#if KBASE_TRACE_ENABLE
void kbase_js_debug_log_current_affinities(struct kbase_device *kbdev);
#else				/*  KBASE_TRACE_ENABLE  */
static inline void
kbase_js_debug_log_current_affinities(struct kbase_device *kbdev)
{
}
#endif				/*  KBASE_TRACE_ENABLE  */

#endif				/* _KBASE_JS_AFFINITY_H_ */
