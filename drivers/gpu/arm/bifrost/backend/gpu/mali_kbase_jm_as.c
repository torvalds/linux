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
 * Register backend context / address space management
 */

#include <mali_kbase.h>
#include <mali_kbase_hwaccess_jm.h>
#include <mali_kbase_ctx_sched.h>

/**
 * assign_and_activate_kctx_addr_space - Assign an AS to a context
 * @kbdev: Kbase device
 * @kctx: Kbase context
 * @current_as: Address Space to assign
 *
 * Assign an Address Space (AS) to a context, and add the context to the Policy.
 *
 * This includes
 *   setting up the global runpool_irq structure and the context on the AS,
 *   Activating the MMU on the AS,
 *   Allowing jobs to be submitted on the AS.
 *
 * Context:
 *   kbasep_js_kctx_info.jsctx_mutex held,
 *   kbasep_js_device_data.runpool_mutex held,
 *   AS transaction mutex held,
 *   Runpool IRQ lock held
 */
static void assign_and_activate_kctx_addr_space(struct kbase_device *kbdev,
						struct kbase_context *kctx,
						struct kbase_as *current_as)
{
	struct kbasep_js_device_data *js_devdata = &kbdev->js_data;

	lockdep_assert_held(&kctx->jctx.sched_info.ctx.jsctx_mutex);
	lockdep_assert_held(&js_devdata->runpool_mutex);
	lockdep_assert_held(&kbdev->hwaccess_lock);

#if !MALI_USE_CSF
	/* Attribute handling */
	kbasep_js_ctx_attr_runpool_retain_ctx(kbdev, kctx);
#endif

	/* Allow it to run jobs */
	kbasep_js_set_submit_allowed(js_devdata, kctx);

	kbase_js_runpool_inc_context_count(kbdev, kctx);
}

bool kbase_backend_use_ctx_sched(struct kbase_device *kbdev,
						struct kbase_context *kctx,
						int js)
{
	int i;

	if (kbdev->hwaccess.active_kctx[js] == kctx) {
		/* Context is already active */
		return true;
	}

	for (i = 0; i < kbdev->nr_hw_address_spaces; i++) {
		if (kbdev->as_to_kctx[i] == kctx) {
			/* Context already has ASID - mark as active */
			return true;
		}
	}

	/* Context does not have address space assigned */
	return false;
}

void kbase_backend_release_ctx_irq(struct kbase_device *kbdev,
						struct kbase_context *kctx)
{
	int as_nr = kctx->as_nr;

	if (as_nr == KBASEP_AS_NR_INVALID) {
		WARN(1, "Attempting to release context without ASID\n");
		return;
	}

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (atomic_read(&kctx->refcount) != 1) {
		WARN(1, "Attempting to release active ASID\n");
		return;
	}

	kbasep_js_clear_submit_allowed(&kbdev->js_data, kctx);

	kbase_ctx_sched_release_ctx(kctx);
	kbase_js_runpool_dec_context_count(kbdev, kctx);
}

void kbase_backend_release_ctx_noirq(struct kbase_device *kbdev,
						struct kbase_context *kctx)
{
}

int kbase_backend_find_and_release_free_address_space(
		struct kbase_device *kbdev, struct kbase_context *kctx)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;
	unsigned long flags;
	int i;

	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_lock(&js_devdata->runpool_mutex);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	for (i = 0; i < kbdev->nr_hw_address_spaces; i++) {
		struct kbasep_js_kctx_info *as_js_kctx_info;
		struct kbase_context *as_kctx;

		as_kctx = kbdev->as_to_kctx[i];
		as_js_kctx_info = &as_kctx->jctx.sched_info;

		/* Don't release privileged or active contexts, or contexts with
		 * jobs running.
		 * Note that a context will have at least 1 reference (which
		 * was previously taken by kbasep_js_schedule_ctx()) until
		 * descheduled.
		 */
		if (as_kctx && !kbase_ctx_flag(as_kctx, KCTX_PRIVILEGED) &&
			atomic_read(&as_kctx->refcount) == 1) {
			if (!kbase_ctx_sched_inc_refcount_nolock(as_kctx)) {
				WARN(1, "Failed to retain active context\n");

				spin_unlock_irqrestore(&kbdev->hwaccess_lock,
						flags);
				mutex_unlock(&js_devdata->runpool_mutex);
				mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

				return KBASEP_AS_NR_INVALID;
			}

			kbasep_js_clear_submit_allowed(js_devdata, as_kctx);

			/* Drop and retake locks to take the jsctx_mutex on the
			 * context we're about to release without violating lock
			 * ordering
			 */
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
			mutex_unlock(&js_devdata->runpool_mutex);
			mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);


			/* Release context from address space */
			mutex_lock(&as_js_kctx_info->ctx.jsctx_mutex);
			mutex_lock(&js_devdata->runpool_mutex);

			kbasep_js_runpool_release_ctx_nolock(kbdev, as_kctx);

			if (!kbase_ctx_flag(as_kctx, KCTX_SCHEDULED)) {
				kbasep_js_runpool_requeue_or_kill_ctx(kbdev,
								as_kctx,
								true);

				mutex_unlock(&js_devdata->runpool_mutex);
				mutex_unlock(&as_js_kctx_info->ctx.jsctx_mutex);

				return i;
			}

			/* Context was retained while locks were dropped,
			 * continue looking for free AS */

			mutex_unlock(&js_devdata->runpool_mutex);
			mutex_unlock(&as_js_kctx_info->ctx.jsctx_mutex);

			mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
			mutex_lock(&js_devdata->runpool_mutex);
			spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
		}
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	mutex_unlock(&js_devdata->runpool_mutex);
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

	return KBASEP_AS_NR_INVALID;
}

bool kbase_backend_use_ctx(struct kbase_device *kbdev,
				struct kbase_context *kctx,
				int as_nr)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbase_as *new_address_space = NULL;
	int js;

	js_devdata = &kbdev->js_data;

	for (js = 0; js < BASE_JM_MAX_NR_SLOTS; js++) {
		if (kbdev->hwaccess.active_kctx[js] == kctx) {
			WARN(1, "Context is already scheduled in\n");
			return false;
		}
	}

	new_address_space = &kbdev->as[as_nr];

	lockdep_assert_held(&js_devdata->runpool_mutex);
	lockdep_assert_held(&kbdev->mmu_hw_mutex);
	lockdep_assert_held(&kbdev->hwaccess_lock);

	assign_and_activate_kctx_addr_space(kbdev, kctx, new_address_space);

	if (kbase_ctx_flag(kctx, KCTX_PRIVILEGED)) {
		/* We need to retain it to keep the corresponding address space
		 */
		kbase_ctx_sched_retain_ctx_refcount(kctx);
	}

	return true;
}

