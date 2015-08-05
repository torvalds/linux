/*
 *
 * (C) COPYRIGHT 2011-2015 ARM Limited. All rights reserved.
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





/**
 * @file mali_kbase_js_affinity.h
 * Affinity Manager internal APIs.
 */

#ifndef _KBASE_JS_AFFINITY_H_
#define _KBASE_JS_AFFINITY_H_

#ifdef CONFIG_MALI_DEBUG_SHADER_SPLIT_FS
/* Import the external affinity mask variables */
extern u64 mali_js0_affinity_mask;
extern u64 mali_js1_affinity_mask;
extern u64 mali_js2_affinity_mask;
#endif /* CONFIG_MALI_DEBUG_SHADER_SPLIT_FS */


/**
 * @addtogroup base_api
 * @{
 */

/**
 * @addtogroup base_kbase_api
 * @{
 */

/**
 * @addtogroup kbase_js_affinity Affinity Manager internal APIs.
 * @{
 *
 */

/**
 * @brief Decide whether it is possible to submit a job to a particular job slot
 * in the current status
 *
 * Will check if submitting to the given job slot is allowed in the current
 * status.  For example using job slot 2 while in soft-stoppable state and only
 * having 1 coregroup is not allowed by the policy. This function should be
 * called prior to submitting a job to a slot to make sure policy rules are not
 * violated.
 *
 * The following locking conditions are made on the caller:
 * - it must hold kbasep_js_device_data::runpool_irq::lock
 *
 * @param kbdev The kbase device structure of the device
 * @param js    Job slot number to check for allowance
 */
bool kbase_js_can_run_job_on_slot_no_lock(struct kbase_device *kbdev,
									int js);

/**
 * @brief Compute affinity for a given job.
 *
 * Currently assumes an all-on/all-off power management policy.
 * Also assumes there is at least one core with tiler available.
 *
 * Returns true if a valid affinity was chosen, false if
 * no cores were available.
 *
 * @param[out] affinity       Affinity bitmap computed
 * @param kbdev The kbase device structure of the device
 * @param katom Job chain of which affinity is going to be found
 * @param js    Slot the job chain is being submitted
 */
bool kbase_js_choose_affinity(u64 * const affinity,
					struct kbase_device *kbdev,
					struct kbase_jd_atom *katom,
					int js);

/**
 * @brief Determine whether a proposed \a affinity on job slot \a js would
 * cause a violation of affinity restrictions.
 *
 * The following locks must be held by the caller:
 * - kbasep_js_device_data::runpool_irq::lock
 */
bool kbase_js_affinity_would_violate(struct kbase_device *kbdev, int js,
								u64 affinity);

/**
 * @brief Affinity tracking: retain cores used by a slot
 *
 * The following locks must be held by the caller:
 * - kbasep_js_device_data::runpool_irq::lock
 */
void kbase_js_affinity_retain_slot_cores(struct kbase_device *kbdev, int js,
								u64 affinity);

/**
 * @brief Affinity tracking: release cores used by a slot
 *
 * Cores \b must be released as soon as a job is dequeued from a slot's 'submit
 * slots', and before another job is submitted to those slots. Otherwise, the
 * refcount could exceed the maximum number submittable to a slot,
 * BASE_JM_SUBMIT_SLOTS.
 *
 * The following locks must be held by the caller:
 * - kbasep_js_device_data::runpool_irq::lock
 */
void kbase_js_affinity_release_slot_cores(struct kbase_device *kbdev, int js,
								u64 affinity);

/**
 * @brief Output to the Trace log the current tracked affinities on all slots
 */
#if KBASE_TRACE_ENABLE
void kbase_js_debug_log_current_affinities(struct kbase_device *kbdev);
#else				/*  KBASE_TRACE_ENABLE  */
static inline void
kbase_js_debug_log_current_affinities(struct kbase_device *kbdev)
{
}
#endif				/*  KBASE_TRACE_ENABLE  */

	  /** @} *//* end group kbase_js_affinity */
	  /** @} *//* end group base_kbase_api */
	  /** @} *//* end group base_api */


#endif				/* _KBASE_JS_AFFINITY_H_ */
