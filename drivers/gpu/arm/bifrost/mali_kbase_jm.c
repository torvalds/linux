/*
 *
 * (C) COPYRIGHT 2014-2020 ARM Limited. All rights reserved.
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


/*
 * HW access job manager common APIs
 */

#include <mali_kbase.h>
#include "mali_kbase_hwaccess_jm.h"
#include "mali_kbase_jm.h"

/**
 * kbase_jm_next_job() - Attempt to run the next @nr_jobs_to_submit jobs on slot
 *			 @js on the active context.
 * @kbdev:		Device pointer
 * @js:			Job slot to run on
 * @nr_jobs_to_submit:	Number of jobs to attempt to submit
 *
 * Return: true if slot can still be submitted on, false if slot is now full.
 */
static bool kbase_jm_next_job(struct kbase_device *kbdev, int js,
				int nr_jobs_to_submit)
{
	struct kbase_context *kctx;
	int i;

	kctx = kbdev->hwaccess.active_kctx[js];
	dev_dbg(kbdev->dev,
		"Trying to run the next %d jobs in kctx %p (s:%d)\n",
		nr_jobs_to_submit, (void *)kctx, js);

	if (!kctx)
		return true;

	for (i = 0; i < nr_jobs_to_submit; i++) {
		struct kbase_jd_atom *katom = kbase_js_pull(kctx, js);

		if (!katom)
			return true; /* Context has no jobs on this slot */

		kbase_backend_run_atom(kbdev, katom);
	}

	dev_dbg(kbdev->dev, "Slot ringbuffer should now be full (s:%d)\n", js);
	return false;
}

u32 kbase_jm_kick(struct kbase_device *kbdev, u32 js_mask)
{
	u32 ret_mask = 0;

	lockdep_assert_held(&kbdev->hwaccess_lock);
	dev_dbg(kbdev->dev, "JM kick slot mask 0x%x\n", js_mask);

	while (js_mask) {
		int js = ffs(js_mask) - 1;
		int nr_jobs_to_submit = kbase_backend_slot_free(kbdev, js);

		if (kbase_jm_next_job(kbdev, js, nr_jobs_to_submit))
			ret_mask |= (1 << js);

		js_mask &= ~(1 << js);
	}

	dev_dbg(kbdev->dev, "Can still submit to mask 0x%x\n", ret_mask);
	return ret_mask;
}

void kbase_jm_try_kick(struct kbase_device *kbdev, u32 js_mask)
{
	struct kbasep_js_device_data *js_devdata = &kbdev->js_data;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (!down_trylock(&js_devdata->schedule_sem)) {
		kbase_jm_kick(kbdev, js_mask);
		up(&js_devdata->schedule_sem);
	}
}

void kbase_jm_try_kick_all(struct kbase_device *kbdev)
{
	struct kbasep_js_device_data *js_devdata = &kbdev->js_data;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (!down_trylock(&js_devdata->schedule_sem)) {
		kbase_jm_kick_all(kbdev);
		up(&js_devdata->schedule_sem);
	}
}

void kbase_jm_idle_ctx(struct kbase_device *kbdev, struct kbase_context *kctx)
{
	int js;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	for (js = 0; js < BASE_JM_MAX_NR_SLOTS; js++) {
		if (kbdev->hwaccess.active_kctx[js] == kctx) {
			dev_dbg(kbdev->dev, "Marking kctx %p as inactive (s:%d)\n",
					(void *)kctx, js);
			kbdev->hwaccess.active_kctx[js] = NULL;
		}
	}
}

struct kbase_jd_atom *kbase_jm_return_atom_to_js(struct kbase_device *kbdev,
				struct kbase_jd_atom *katom)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	dev_dbg(kbdev->dev, "Atom %p is returning with event code 0x%x\n",
		(void *)katom, katom->event_code);

	if (katom->event_code != BASE_JD_EVENT_STOPPED &&
			katom->event_code != BASE_JD_EVENT_REMOVED_FROM_NEXT) {
		return kbase_js_complete_atom(katom, NULL);
	} else {
		kbase_js_unpull(katom->kctx, katom);
		return NULL;
	}
}

struct kbase_jd_atom *kbase_jm_complete(struct kbase_device *kbdev,
		struct kbase_jd_atom *katom, ktime_t *end_timestamp)
{
	lockdep_assert_held(&kbdev->hwaccess_lock);

	return kbase_js_complete_atom(katom, end_timestamp);
}
