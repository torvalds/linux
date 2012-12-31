/*
 *
 * (C) COPYRIGHT 2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/**
 * @file mali_kbase_js_affinity.h
 * Affinity Manager internal APIs.
 */

#ifndef _KBASE_JS_AFFINITY_H_
#define _KBASE_JS_AFFINITY_H_



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
 * @brief Compute affinity for a given job.
 *
 * Currently assumes an all-on/all-off power management policy.
 * Also assumes there is at least one core with tiler available.
 * Will try to produce an even distribution of cores for SS and
 * NSS jobs. SS jobs will be given cores starting from core-group
 * 0 forward to n. NSS jobs will be given cores from core-group n
 * backwards to 0. This way for example in a T658 SS jobs will
 * tend to run on cores from core-group 0 and NSS jobs will tend
 * to run on cores from core-group 1.
 * An assertion will be raised if computed affinity is 0
 *
 * @param[out] affinity Affinity bitmap computed
 * @param kbdev The kbase device structure of the device
 * @param katom Job chain of which affinity is going to be found
 * @param js    Slot the job chain is being submitted

 */
void kbase_js_choose_affinity( u64 *affinity, kbase_device *kbdev, kbase_jd_atom *katom, int js );


/**
 * @brief Decide whether it is possible to submit a job to a particular job slot in the current status
 *
 * Will check if submitting to the given job slot is allowed in the current status.
 * For example using job slot 2 while in soft-stoppable state is not allowed by the
 * policy. This function should be called prior to submitting a job to a slot to
 * make sure policy rules are not violated.
 * 
 * The following locking conditions are made on the caller:
 * - it must hold kbasep_js_device_data::runpool_irq::lock
 *
 * @param js_devdata Device info, will be read to obtain current state
 * @param js         Job slot number to check for allowance
 */
static INLINE mali_bool kbase_js_can_run_job_on_slot_no_lock( kbasep_js_device_data *js_devdata, int js )
{
#if BASE_HW_ISSUE_7347
	kbase_device *kbdev = CONTAINER_OF(js_devdata, kbase_device, js_data);

	if (js == 0)
	{
		/* Check there are no jobs running on job slots 1 or 2 */
		if (kbdev->jm_slots[1].submitted_nr > 0 || kbdev->jm_slots[2].submitted_nr > 0)
		{
			return MALI_FALSE;
		}
	}
	else
	{
		/* Check there are no jobs running on job slot 0 */
		if (kbdev->jm_slots[0].submitted_nr > 0)
		{
			return MALI_FALSE;
		}
	}
#endif

	/* Submitting to job slot 2 while in soft-stopable state is not allowed. */
	return ( !((js_devdata->runpool_irq.nr_nss_ctxs_running == 0) && (js == 2)));
}


/** @} */ /* end group kbase_js_affinity */
/** @} */ /* end group base_kbase_api */
/** @} */ /* end group base_api */





#endif /* _KBASE_JS_AFFINITY_H_ */
