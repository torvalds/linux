/*
 *
 * (C) COPYRIGHT 2010-2016 ARM Limited. All rights reserved.
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
 * Base kernel affinity manager APIs
 */

#include <mali_kbase.h>
#include "mali_kbase_js_affinity.h"
#include "mali_kbase_hw.h"

#include <backend/gpu/mali_kbase_pm_internal.h>


bool kbase_js_can_run_job_on_slot_no_lock(struct kbase_device *kbdev,
									int js)
{
	/*
	 * Here are the reasons for using job slot 2:
	 * - BASE_HW_ISSUE_8987 (which is entirely used for that purpose)
	 * - In absence of the above, then:
	 *  - Atoms with BASE_JD_REQ_COHERENT_GROUP
	 *  - But, only when there aren't contexts with
	 *  KBASEP_JS_CTX_ATTR_COMPUTE_ALL_CORES, because the atoms that run on
	 *  all cores on slot 1 could be blocked by those using a coherent group
	 *  on slot 2
	 *  - And, only when you actually have 2 or more coregroups - if you
	 *  only have 1 coregroup, then having jobs for slot 2 implies they'd
	 *  also be for slot 1, meaning you'll get interference from them. Jobs
	 *  able to run on slot 2 could also block jobs that can only run on
	 *  slot 1 (tiler jobs)
	 */
	if (kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8987))
		return true;

	if (js != 2)
		return true;

	/* Only deal with js==2 now: */
	if (kbdev->gpu_props.num_core_groups > 1) {
		/* Only use slot 2 in the 2+ coregroup case */
		if (kbasep_js_ctx_attr_is_attr_on_runpool(kbdev,
					KBASEP_JS_CTX_ATTR_COMPUTE_ALL_CORES) ==
								false) {
			/* ...But only when we *don't* have atoms that run on
			 * all cores */

			/* No specific check for BASE_JD_REQ_COHERENT_GROUP
			 * atoms - the policy will sort that out */
			return true;
		}
	}

	/* Above checks failed mean we shouldn't use slot 2 */
	return false;
}

/*
 * As long as it has been decided to have a deeper modification of
 * what job scheduler, power manager and affinity manager will
 * implement, this function is just an intermediate step that
 * assumes:
 * - all working cores will be powered on when this is called.
 * - largest current configuration is 2 core groups.
 * - It has been decided not to have hardcoded values so the low
 *   and high cores in a core split will be evently distributed.
 * - Odd combinations of core requirements have been filtered out
 *   and do not get to this function (e.g. CS+T+NSS is not
 *   supported here).
 * - This function is frequently called and can be optimized,
 *   (see notes in loops), but as the functionallity will likely
 *   be modified, optimization has not been addressed.
*/
bool kbase_js_choose_affinity(u64 * const affinity,
					struct kbase_device *kbdev,
					struct kbase_jd_atom *katom, int js)
{
	base_jd_core_req core_req = katom->core_req;
	unsigned int num_core_groups = kbdev->gpu_props.num_core_groups;
	u64 core_availability_mask;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->pm.power_change_lock, flags);

	core_availability_mask = kbase_pm_ca_get_core_mask(kbdev);

	/*
	 * If no cores are currently available (core availability policy is
	 * transitioning) then fail.
	 */
	if (0 == core_availability_mask) {
		spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);
		*affinity = 0;
		return false;
	}

	KBASE_DEBUG_ASSERT(js >= 0);

	if ((core_req & (BASE_JD_REQ_FS | BASE_JD_REQ_CS | BASE_JD_REQ_T)) ==
								BASE_JD_REQ_T) {
		spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);
		 /* If the hardware supports XAFFINITY then we'll only enable
		  * the tiler (which is the default so this is a no-op),
		  * otherwise enable shader core 0. */
		if (!kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_XAFFINITY))
			*affinity = 1;
		else
			*affinity = 0;

		return true;
	}

	if (1 == kbdev->gpu_props.num_cores) {
		/* trivial case only one core, nothing to do */
		*affinity = core_availability_mask &
				kbdev->pm.debug_core_mask[js];
	} else {
		if ((core_req & (BASE_JD_REQ_COHERENT_GROUP |
					BASE_JD_REQ_SPECIFIC_COHERENT_GROUP))) {
			if (js == 0 || num_core_groups == 1) {
				/* js[0] and single-core-group systems just get
				 * the first core group */
				*affinity =
				kbdev->gpu_props.props.coherency_info.group[0].core_mask
						& core_availability_mask &
						kbdev->pm.debug_core_mask[js];
			} else {
				/* js[1], js[2] use core groups 0, 1 for
				 * dual-core-group systems */
				u32 core_group_idx = ((u32) js) - 1;

				KBASE_DEBUG_ASSERT(core_group_idx <
							num_core_groups);
				*affinity =
				kbdev->gpu_props.props.coherency_info.group[core_group_idx].core_mask
						& core_availability_mask &
						kbdev->pm.debug_core_mask[js];

				/* If the job is specifically targeting core
				 * group 1 and the core availability policy is
				 * keeping that core group off, then fail */
				if (*affinity == 0 && core_group_idx == 1 &&
						kbdev->pm.backend.cg1_disabled
								== true)
					katom->event_code =
							BASE_JD_EVENT_PM_EVENT;
			}
		} else {
			/* All cores are available when no core split is
			 * required */
			*affinity = core_availability_mask &
					kbdev->pm.debug_core_mask[js];
		}
	}

	spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);

	/*
	 * If no cores are currently available in the desired core group(s)
	 * (core availability policy is transitioning) then fail.
	 */
	if (*affinity == 0)
		return false;

	/* Enable core 0 if tiler required for hardware without XAFFINITY
	 * support (notes above) */
	if (core_req & BASE_JD_REQ_T) {
		if (!kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_XAFFINITY))
			*affinity = *affinity | 1;
	}

	return true;
}

static inline bool kbase_js_affinity_is_violating(
						struct kbase_device *kbdev,
								u64 *affinities)
{
	/* This implementation checks whether the two slots involved in Generic
	 * thread creation have intersecting affinity. This is due to micro-
	 * architectural issues where a job in slot A targetting cores used by
	 * slot B could prevent the job in slot B from making progress until the
	 * job in slot A has completed.
	 */
	u64 affinity_set_left;
	u64 affinity_set_right;
	u64 intersection;

	KBASE_DEBUG_ASSERT(affinities != NULL);

	affinity_set_left = affinities[1];

	affinity_set_right = affinities[2];

	/* A violation occurs when any bit in the left_set is also in the
	 * right_set */
	intersection = affinity_set_left & affinity_set_right;

	return (bool) (intersection != (u64) 0u);
}

bool kbase_js_affinity_would_violate(struct kbase_device *kbdev, int js,
								u64 affinity)
{
	struct kbasep_js_device_data *js_devdata;
	u64 new_affinities[BASE_JM_MAX_NR_SLOTS];

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(js < BASE_JM_MAX_NR_SLOTS);
	js_devdata = &kbdev->js_data;

	memcpy(new_affinities, js_devdata->runpool_irq.slot_affinities,
			sizeof(js_devdata->runpool_irq.slot_affinities));

	new_affinities[js] |= affinity;

	return kbase_js_affinity_is_violating(kbdev, new_affinities);
}

void kbase_js_affinity_retain_slot_cores(struct kbase_device *kbdev, int js,
								u64 affinity)
{
	struct kbasep_js_device_data *js_devdata;
	u64 cores;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(js < BASE_JM_MAX_NR_SLOTS);
	js_devdata = &kbdev->js_data;

	KBASE_DEBUG_ASSERT(kbase_js_affinity_would_violate(kbdev, js, affinity)
								== false);

	cores = affinity;
	while (cores) {
		int bitnum = fls64(cores) - 1;
		u64 bit = 1ULL << bitnum;
		s8 cnt;

		cnt =
		++(js_devdata->runpool_irq.slot_affinity_refcount[js][bitnum]);

		if (cnt == 1)
			js_devdata->runpool_irq.slot_affinities[js] |= bit;

		cores &= ~bit;
	}
}

void kbase_js_affinity_release_slot_cores(struct kbase_device *kbdev, int js,
								u64 affinity)
{
	struct kbasep_js_device_data *js_devdata;
	u64 cores;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(js < BASE_JM_MAX_NR_SLOTS);
	js_devdata = &kbdev->js_data;

	cores = affinity;
	while (cores) {
		int bitnum = fls64(cores) - 1;
		u64 bit = 1ULL << bitnum;
		s8 cnt;

		KBASE_DEBUG_ASSERT(
		js_devdata->runpool_irq.slot_affinity_refcount[js][bitnum] > 0);

		cnt =
		--(js_devdata->runpool_irq.slot_affinity_refcount[js][bitnum]);

		if (0 == cnt)
			js_devdata->runpool_irq.slot_affinities[js] &= ~bit;

		cores &= ~bit;
	}
}

#if KBASE_TRACE_ENABLE
void kbase_js_debug_log_current_affinities(struct kbase_device *kbdev)
{
	struct kbasep_js_device_data *js_devdata;
	int slot_nr;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	js_devdata = &kbdev->js_data;

	for (slot_nr = 0; slot_nr < 3; ++slot_nr)
		KBASE_TRACE_ADD_SLOT_INFO(kbdev, JS_AFFINITY_CURRENT, NULL,
							NULL, 0u, slot_nr,
			(u32) js_devdata->runpool_irq.slot_affinities[slot_nr]);
}
#endif				/* KBASE_TRACE_ENABLE  */
