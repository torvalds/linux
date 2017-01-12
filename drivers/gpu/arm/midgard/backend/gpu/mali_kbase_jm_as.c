/*
 *
 * (C) COPYRIGHT 2014-2015 ARM Limited. All rights reserved.
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
 * Register backend context / address space management
 */

#include <mali_kbase.h>
#include <mali_kbase_hwaccess_jm.h>

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
	struct kbasep_js_per_as_data *js_per_as_data;
	int as_nr = current_as->number;

	lockdep_assert_held(&kctx->jctx.sched_info.ctx.jsctx_mutex);
	lockdep_assert_held(&js_devdata->runpool_mutex);
	lockdep_assert_held(&current_as->transaction_mutex);
	lockdep_assert_held(&js_devdata->runpool_irq.lock);

	js_per_as_data = &js_devdata->runpool_irq.per_as_data[as_nr];

	/* Attribute handling */
	kbasep_js_ctx_attr_runpool_retain_ctx(kbdev, kctx);

	/* Assign addr space */
	kctx->as_nr = as_nr;

	/* If the GPU is currently powered, activate this address space on the
	 * MMU */
	if (kbdev->pm.backend.gpu_powered)
		kbase_mmu_update(kctx);
	/* If the GPU was not powered then the MMU will be reprogrammed on the
	 * next pm_context_active() */

	/* Allow it to run jobs */
	kbasep_js_set_submit_allowed(js_devdata, kctx);

	/* Book-keeping */
	js_per_as_data->kctx = kctx;
	js_per_as_data->as_busy_refcount = 0;

	kbase_js_runpool_inc_context_count(kbdev, kctx);
}

/**
 * release_addr_space - Release an address space
 * @kbdev: Kbase device
 * @kctx_as_nr: Address space of context to release
 * @kctx: Context being released
 *
 * Context: kbasep_js_device_data.runpool_mutex must be held
 *
 * Release an address space, making it available for being picked again.
 */
static void release_addr_space(struct kbase_device *kbdev, int kctx_as_nr,
						struct kbase_context *kctx)
{
	struct kbasep_js_device_data *js_devdata;
	u16 as_bit = (1u << kctx_as_nr);

	js_devdata = &kbdev->js_data;
	lockdep_assert_held(&js_devdata->runpool_mutex);

	/* The address space must not already be free */
	KBASE_DEBUG_ASSERT(!(js_devdata->as_free & as_bit));

	js_devdata->as_free |= as_bit;

	kbase_js_runpool_dec_context_count(kbdev, kctx);
}

bool kbase_backend_use_ctx_sched(struct kbase_device *kbdev,
						struct kbase_context *kctx)
{
	int i;

	if (kbdev->hwaccess.active_kctx == kctx) {
		/* Context is already active */
		return true;
	}

	for (i = 0; i < kbdev->nr_hw_address_spaces; i++) {
		struct kbasep_js_per_as_data *js_per_as_data =
				&kbdev->js_data.runpool_irq.per_as_data[i];

		if (js_per_as_data->kctx == kctx) {
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
	struct kbasep_js_per_as_data *js_per_as_data;
	int as_nr = kctx->as_nr;

	if (as_nr == KBASEP_AS_NR_INVALID) {
		WARN(1, "Attempting to release context without ASID\n");
		return;
	}

	lockdep_assert_held(&kbdev->as[as_nr].transaction_mutex);
	lockdep_assert_held(&kbdev->js_data.runpool_irq.lock);

	js_per_as_data = &kbdev->js_data.runpool_irq.per_as_data[kctx->as_nr];
	if (js_per_as_data->as_busy_refcount != 0) {
		WARN(1, "Attempting to release active ASID\n");
		return;
	}

	/* Release context from address space */
	js_per_as_data->kctx = NULL;

	kbasep_js_clear_submit_allowed(&kbdev->js_data, kctx);
	/* If the GPU is currently powered, de-activate this address space on
	 * the MMU */
	if (kbdev->pm.backend.gpu_powered)
		kbase_mmu_disable(kctx);
	/* If the GPU was not powered then the MMU will be reprogrammed on the
	 * next pm_context_active() */

	release_addr_space(kbdev, as_nr, kctx);
	kctx->as_nr = KBASEP_AS_NR_INVALID;
}

void kbase_backend_release_ctx_noirq(struct kbase_device *kbdev,
						struct kbase_context *kctx)
{
}

void kbase_backend_release_free_address_space(struct kbase_device *kbdev,
								int as_nr)
{
	struct kbasep_js_device_data *js_devdata;

	js_devdata = &kbdev->js_data;

	lockdep_assert_held(&js_devdata->runpool_mutex);

	js_devdata->as_free |= (1 << as_nr);
}

/**
 * check_is_runpool_full - check whether the runpool is full for a specified
 * context
 * @kbdev: Kbase device
 * @kctx:  Kbase context
 *
 * If kctx == NULL, then this makes the least restrictive check on the
 * runpool. A specific context that is supplied immediately after could fail
 * the check, even under the same conditions.
 *
 * Therefore, once a context is obtained you \b must re-check it with this
 * function, since the return value could change to false.
 *
 * Context:
 *   In all cases, the caller must hold kbasep_js_device_data.runpool_mutex.
 *   When kctx != NULL the caller must hold the
 *   kbasep_js_kctx_info.ctx.jsctx_mutex.
 *   When kctx == NULL, then the caller need not hold any jsctx_mutex locks (but
 *   it doesn't do any harm to do so).
 *
 * Return: true if the runpool is full
 */
static bool check_is_runpool_full(struct kbase_device *kbdev,
						struct kbase_context *kctx)
{
	struct kbasep_js_device_data *js_devdata;
	bool is_runpool_full;

	js_devdata = &kbdev->js_data;
	lockdep_assert_held(&js_devdata->runpool_mutex);

	/* Regardless of whether a context is submitting or not, can't have more
	 * than there are HW address spaces */
	is_runpool_full = (bool) (js_devdata->nr_all_contexts_running >=
						kbdev->nr_hw_address_spaces);

	if (kctx != NULL && (kctx->jctx.sched_info.ctx.flags &
					KBASE_CTX_FLAG_SUBMIT_DISABLED) == 0) {
		lockdep_assert_held(&kctx->jctx.sched_info.ctx.jsctx_mutex);
		/* Contexts that submit might use less of the address spaces
		 * available, due to HW workarounds.  In which case, the runpool
		 * is also full when the number of submitting contexts exceeds
		 * the number of submittable address spaces.
		 *
		 * Both checks must be made: can have nr_user_address_spaces ==
		 * nr_hw_address spaces, and at the same time can have
		 * nr_user_contexts_running < nr_all_contexts_running. */
		is_runpool_full |= (bool)
					(js_devdata->nr_user_contexts_running >=
						kbdev->nr_user_address_spaces);
	}

	return is_runpool_full;
}

int kbase_backend_find_free_address_space(struct kbase_device *kbdev,
						struct kbase_context *kctx)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;
	unsigned long flags;
	int i;

	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	mutex_lock(&js_devdata->runpool_mutex);

	/* First try to find a free address space */
	if (check_is_runpool_full(kbdev, kctx))
		i = -1;
	else
		i = ffs(js_devdata->as_free) - 1;

	if (i >= 0 && i < kbdev->nr_hw_address_spaces) {
		js_devdata->as_free &= ~(1 << i);

		mutex_unlock(&js_devdata->runpool_mutex);
		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

		return i;
	}

	spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);

	/* No address space currently free, see if we can release one */
	for (i = 0; i < kbdev->nr_hw_address_spaces; i++) {
		struct kbasep_js_per_as_data *js_per_as_data;
		struct kbasep_js_kctx_info *as_js_kctx_info;
		struct kbase_context *as_kctx;

		js_per_as_data = &kbdev->js_data.runpool_irq.per_as_data[i];
		as_kctx = js_per_as_data->kctx;
		as_js_kctx_info = &as_kctx->jctx.sched_info;

		/* Don't release privileged or active contexts, or contexts with
		 * jobs running */
		if (as_kctx && !(as_kctx->jctx.sched_info.ctx.flags &
						KBASE_CTX_FLAG_PRIVILEGED) &&
			js_per_as_data->as_busy_refcount == 0) {
			if (!kbasep_js_runpool_retain_ctx_nolock(kbdev,
								as_kctx)) {
				WARN(1, "Failed to retain active context\n");

				spin_unlock_irqrestore(
						&js_devdata->runpool_irq.lock,
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
			spin_unlock_irqrestore(&js_devdata->runpool_irq.lock,
									flags);
			mutex_unlock(&js_devdata->runpool_mutex);
			mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);


			/* Release context from address space */
			mutex_lock(&as_js_kctx_info->ctx.jsctx_mutex);
			mutex_lock(&js_devdata->runpool_mutex);

			kbasep_js_runpool_release_ctx_nolock(kbdev, as_kctx);

			if (!as_js_kctx_info->ctx.is_scheduled) {
				kbasep_js_runpool_requeue_or_kill_ctx(kbdev,
								as_kctx,
								true);

				js_devdata->as_free &= ~(1 << i);

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
			spin_lock_irqsave(&js_devdata->runpool_irq.lock, flags);
		}
	}

	spin_unlock_irqrestore(&js_devdata->runpool_irq.lock, flags);

	mutex_unlock(&js_devdata->runpool_mutex);
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

	return KBASEP_AS_NR_INVALID;
}

bool kbase_backend_use_ctx(struct kbase_device *kbdev,
				struct kbase_context *kctx,
				int as_nr)
{
	struct kbasep_js_device_data *js_devdata;
	struct kbasep_js_kctx_info *js_kctx_info;
	struct kbase_as *new_address_space = NULL;

	js_devdata = &kbdev->js_data;
	js_kctx_info = &kctx->jctx.sched_info;

	if (kbdev->hwaccess.active_kctx == kctx ||
	    kctx->as_nr != KBASEP_AS_NR_INVALID ||
	    as_nr == KBASEP_AS_NR_INVALID) {
		WARN(1, "Invalid parameters to use_ctx()\n");
		return false;
	}

	new_address_space = &kbdev->as[as_nr];

	lockdep_assert_held(&js_devdata->runpool_mutex);
	lockdep_assert_held(&new_address_space->transaction_mutex);
	lockdep_assert_held(&js_devdata->runpool_irq.lock);

	assign_and_activate_kctx_addr_space(kbdev, kctx, new_address_space);

	if ((js_kctx_info->ctx.flags & KBASE_CTX_FLAG_PRIVILEGED) != 0) {
		/* We need to retain it to keep the corresponding address space
		 */
		kbasep_js_runpool_retain_ctx_nolock(kbdev, kctx);
	}

	return true;
}

