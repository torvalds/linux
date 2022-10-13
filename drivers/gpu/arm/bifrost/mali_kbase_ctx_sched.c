// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2017-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
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
 */

#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include "mali_kbase_ctx_sched.h"
#include "tl/mali_kbase_tracepoints.h"
#if MALI_USE_CSF
#include "mali_kbase_reset_gpu.h"
#else
#include <mali_kbase_hwaccess_jm.h>
#endif

/* Helper for ktrace */
#if KBASE_KTRACE_ENABLE
static int kbase_ktrace_get_ctx_refcnt(struct kbase_context *kctx)
{
	return atomic_read(&kctx->refcount);
}
#else /* KBASE_KTRACE_ENABLE  */
static int kbase_ktrace_get_ctx_refcnt(struct kbase_context *kctx)
{
	CSTD_UNUSED(kctx);
	return 0;
}
#endif /* KBASE_KTRACE_ENABLE  */

int kbase_ctx_sched_init(struct kbase_device *kbdev)
{
	int as_present = (1U << kbdev->nr_hw_address_spaces) - 1;

	/* These two must be recalculated if nr_hw_address_spaces changes
	 * (e.g. for HW workarounds)
	 */
	kbdev->nr_user_address_spaces = kbdev->nr_hw_address_spaces;
	kbdev->as_free = as_present; /* All ASs initially free */

	memset(kbdev->as_to_kctx, 0, sizeof(kbdev->as_to_kctx));

	return 0;
}

void kbase_ctx_sched_term(struct kbase_device *kbdev)
{
	s8 i;

	/* Sanity checks */
	for (i = 0; i != kbdev->nr_hw_address_spaces; ++i) {
		WARN_ON(kbdev->as_to_kctx[i] != NULL);
		WARN_ON(!(kbdev->as_free & (1u << i)));
	}
}

/* kbasep_ctx_sched_find_as_for_ctx - Find a free address space
 *
 * @kbdev: The context for which to find a free address space
 *
 * Return: A valid AS if successful, otherwise KBASEP_AS_NR_INVALID
 *
 * This function returns an address space available for use. It would prefer
 * returning an AS that has been previously assigned to the context to
 * avoid having to reprogram the MMU.
 */
static int kbasep_ctx_sched_find_as_for_ctx(struct kbase_context *kctx)
{
	struct kbase_device *const kbdev = kctx->kbdev;
	int free_as;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	/* First check if the previously assigned AS is available */
	if ((kctx->as_nr != KBASEP_AS_NR_INVALID) &&
			(kbdev->as_free & (1u << kctx->as_nr)))
		return kctx->as_nr;

	/* The previously assigned AS was taken, we'll be returning any free
	 * AS at this point.
	 */
	free_as = ffs(kbdev->as_free) - 1;
	if (free_as >= 0 && free_as < kbdev->nr_hw_address_spaces)
		return free_as;

	return KBASEP_AS_NR_INVALID;
}

int kbase_ctx_sched_retain_ctx(struct kbase_context *kctx)
{
	struct kbase_device *const kbdev = kctx->kbdev;

	lockdep_assert_held(&kbdev->mmu_hw_mutex);
	lockdep_assert_held(&kbdev->hwaccess_lock);

	WARN_ON(!kbdev->pm.backend.gpu_powered);

	if (atomic_inc_return(&kctx->refcount) == 1) {
		int const free_as = kbasep_ctx_sched_find_as_for_ctx(kctx);

		if (free_as != KBASEP_AS_NR_INVALID) {
			kbdev->as_free &= ~(1u << free_as);
			/* Only program the MMU if the context has not been
			 * assigned the same address space before.
			 */
			if (free_as != kctx->as_nr) {
				struct kbase_context *const prev_kctx =
					kbdev->as_to_kctx[free_as];

				if (prev_kctx) {
					WARN_ON(atomic_read(&prev_kctx->refcount) != 0);
					kbase_mmu_disable(prev_kctx);
					KBASE_TLSTREAM_TL_KBASE_CTX_UNASSIGN_AS(
						kbdev, prev_kctx->id);
					prev_kctx->as_nr = KBASEP_AS_NR_INVALID;
				}
				kctx->as_nr = free_as;
				kbdev->as_to_kctx[free_as] = kctx;
				KBASE_TLSTREAM_TL_KBASE_CTX_ASSIGN_AS(
					kbdev, kctx->id, free_as);
				kbase_mmu_update(kbdev, &kctx->mmu,
					kctx->as_nr);
			}
		} else {
			atomic_dec(&kctx->refcount);

			/* Failed to find an available address space, we must
			 * be returning an error at this point.
			 */
			WARN_ON(kctx->as_nr != KBASEP_AS_NR_INVALID);
		}
	}

	return kctx->as_nr;
}

void kbase_ctx_sched_retain_ctx_refcount(struct kbase_context *kctx)
{
	struct kbase_device *const kbdev = kctx->kbdev;

	lockdep_assert_held(&kbdev->hwaccess_lock);
#if MALI_USE_CSF
	/* We expect the context to be active when this function is called,
	 * except for the case where a page fault is reported for it during
	 * the GPU reset sequence, in which case we can expect the refcount
	 * to be 0.
	 */
	WARN_ON(!atomic_read(&kctx->refcount) && !kbase_reset_gpu_is_active(kbdev));
#else
	/* We expect the context to be active (and thus refcount should be non-zero)
         * when this function is called
         */
	WARN_ON(!atomic_read(&kctx->refcount));
#endif
	WARN_ON(kctx->as_nr == KBASEP_AS_NR_INVALID);
	WARN_ON(kbdev->as_to_kctx[kctx->as_nr] != kctx);

	atomic_inc(&kctx->refcount);
}

void kbase_ctx_sched_release_ctx(struct kbase_context *kctx)
{
	struct kbase_device *const kbdev = kctx->kbdev;
	int new_ref_count;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	new_ref_count = atomic_dec_return(&kctx->refcount);
	if (new_ref_count == 0) {
		kbdev->as_free |= (1u << kctx->as_nr);
		if (kbase_ctx_flag(kctx, KCTX_AS_DISABLED_ON_FAULT)) {
			KBASE_TLSTREAM_TL_KBASE_CTX_UNASSIGN_AS(
				kbdev, kctx->id);
			kbdev->as_to_kctx[kctx->as_nr] = NULL;
			kctx->as_nr = KBASEP_AS_NR_INVALID;
			kbase_ctx_flag_clear(kctx, KCTX_AS_DISABLED_ON_FAULT);
#if !MALI_USE_CSF
			kbase_backend_slot_kctx_purge_locked(kbdev, kctx);
#endif
		}
	}

	KBASE_KTRACE_ADD(kbdev, SCHED_RELEASE_CTX, kctx, new_ref_count);
}

void kbase_ctx_sched_remove_ctx(struct kbase_context *kctx)
{
	struct kbase_device *const kbdev = kctx->kbdev;

	lockdep_assert_held(&kbdev->mmu_hw_mutex);
	lockdep_assert_held(&kbdev->hwaccess_lock);

	WARN_ON(atomic_read(&kctx->refcount) != 0);

	if (kctx->as_nr != KBASEP_AS_NR_INVALID) {
		if (kbdev->pm.backend.gpu_powered)
			kbase_mmu_disable(kctx);

		KBASE_TLSTREAM_TL_KBASE_CTX_UNASSIGN_AS(kbdev, kctx->id);
		kbdev->as_to_kctx[kctx->as_nr] = NULL;
		kctx->as_nr = KBASEP_AS_NR_INVALID;
	}
}

void kbase_ctx_sched_restore_all_as(struct kbase_device *kbdev)
{
	s8 i;

	lockdep_assert_held(&kbdev->mmu_hw_mutex);
	lockdep_assert_held(&kbdev->hwaccess_lock);

	WARN_ON(!kbdev->pm.backend.gpu_powered);

	for (i = 0; i != kbdev->nr_hw_address_spaces; ++i) {
		struct kbase_context *kctx;

#if MALI_USE_CSF
		if ((i == MCU_AS_NR) && kbdev->csf.firmware_inited) {
			kbase_mmu_update(kbdev, &kbdev->csf.mcu_mmu,
					 MCU_AS_NR);
			continue;
		}
#endif
		kctx = kbdev->as_to_kctx[i];
		if (kctx) {
			if (atomic_read(&kctx->refcount)) {
				WARN_ON(kctx->as_nr != i);

				kbase_mmu_update(kbdev, &kctx->mmu,
					kctx->as_nr);
				kbase_ctx_flag_clear(kctx,
					KCTX_AS_DISABLED_ON_FAULT);
			} else {
				/* This context might have been assigned an
				 * AS before, clear it.
				 */
				if (kctx->as_nr != KBASEP_AS_NR_INVALID) {
					KBASE_TLSTREAM_TL_KBASE_CTX_UNASSIGN_AS(
						kbdev, kctx->id);
					kbdev->as_to_kctx[kctx->as_nr] = NULL;
					kctx->as_nr = KBASEP_AS_NR_INVALID;
				}
			}
		} else {
			kbase_mmu_disable_as(kbdev, i);
		}
	}
}

struct kbase_context *kbase_ctx_sched_as_to_ctx_refcount(
		struct kbase_device *kbdev, size_t as_nr)
{
	unsigned long flags;
	struct kbase_context *found_kctx = NULL;

	if (WARN_ON(kbdev == NULL))
		return NULL;

	if (WARN_ON(as_nr >= BASE_MAX_NR_AS))
		return NULL;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	found_kctx = kbdev->as_to_kctx[as_nr];

	if (!WARN_ON(found_kctx == NULL))
		kbase_ctx_sched_retain_ctx_refcount(found_kctx);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return found_kctx;
}

struct kbase_context *kbase_ctx_sched_as_to_ctx(struct kbase_device *kbdev,
		size_t as_nr)
{
	unsigned long flags;
	struct kbase_context *found_kctx;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	found_kctx = kbase_ctx_sched_as_to_ctx_nolock(kbdev, as_nr);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return found_kctx;
}

struct kbase_context *kbase_ctx_sched_as_to_ctx_nolock(
		struct kbase_device *kbdev, size_t as_nr)
{
	struct kbase_context *found_kctx;

	if (WARN_ON(kbdev == NULL))
		return NULL;

	if (WARN_ON(as_nr >= BASE_MAX_NR_AS))
		return NULL;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	found_kctx = kbdev->as_to_kctx[as_nr];

	if (found_kctx) {
		if (atomic_read(&found_kctx->refcount) <= 0)
			found_kctx = NULL;
	}

	return found_kctx;
}

bool kbase_ctx_sched_inc_refcount_nolock(struct kbase_context *kctx)
{
	bool result = false;

	if (WARN_ON(kctx == NULL))
		return result;

	lockdep_assert_held(&kctx->kbdev->hwaccess_lock);

	if (atomic_read(&kctx->refcount) > 0) {
		KBASE_DEBUG_ASSERT(kctx->as_nr >= 0);

		kbase_ctx_sched_retain_ctx_refcount(kctx);
		KBASE_KTRACE_ADD(kctx->kbdev, SCHED_RETAIN_CTX_NOLOCK, kctx,
				kbase_ktrace_get_ctx_refcnt(kctx));
		result = true;
	}

	return result;
}

bool kbase_ctx_sched_inc_refcount(struct kbase_context *kctx)
{
	unsigned long flags;
	bool result = false;

	if (WARN_ON(kctx == NULL))
		return result;

	if (WARN_ON(kctx->kbdev == NULL))
		return result;

	mutex_lock(&kctx->kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kctx->kbdev->hwaccess_lock, flags);
	result = kbase_ctx_sched_inc_refcount_nolock(kctx);
	spin_unlock_irqrestore(&kctx->kbdev->hwaccess_lock, flags);
	mutex_unlock(&kctx->kbdev->mmu_hw_mutex);

	return result;
}

void kbase_ctx_sched_release_ctx_lock(struct kbase_context *kctx)
{
	unsigned long flags;

	if (WARN_ON(!kctx))
		return;

	spin_lock_irqsave(&kctx->kbdev->hwaccess_lock, flags);

	if (!WARN_ON(kctx->as_nr == KBASEP_AS_NR_INVALID) &&
			!WARN_ON(atomic_read(&kctx->refcount) <= 0))
		kbase_ctx_sched_release_ctx(kctx);

	spin_unlock_irqrestore(&kctx->kbdev->hwaccess_lock, flags);
}

#if MALI_USE_CSF
bool kbase_ctx_sched_inc_refcount_if_as_valid(struct kbase_context *kctx)
{
	struct kbase_device *kbdev;
	bool added_ref = false;
	unsigned long flags;

	if (WARN_ON(kctx == NULL))
		return added_ref;

	kbdev = kctx->kbdev;

	if (WARN_ON(kbdev == NULL))
		return added_ref;

	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	if ((kctx->as_nr != KBASEP_AS_NR_INVALID) &&
	    (kctx == kbdev->as_to_kctx[kctx->as_nr])) {
		atomic_inc(&kctx->refcount);

		if (kbdev->as_free & (1u << kctx->as_nr))
			kbdev->as_free &= ~(1u << kctx->as_nr);

		KBASE_KTRACE_ADD(kbdev, SCHED_RETAIN_CTX_NOLOCK, kctx,
				 kbase_ktrace_get_ctx_refcnt(kctx));
		added_ref = true;
	}

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->mmu_hw_mutex);

	return added_ref;
}
#endif
