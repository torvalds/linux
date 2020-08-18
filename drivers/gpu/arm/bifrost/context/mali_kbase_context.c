// SPDX-License-Identifier: GPL-2.0
/*
 *
 * (C) COPYRIGHT 2019-2020 ARM Limited. All rights reserved.
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
 * Base kernel context APIs
 */

#include <mali_kbase.h>
#include <gpu/mali_kbase_gpu_regmap.h>
#include <mali_kbase_mem_linux.h>
#include <mali_kbase_dma_fence.h>
#include <mali_kbase_ctx_sched.h>
#include <mali_kbase_mem_pool_group.h>
#include <tl/mali_kbase_tracepoints.h>
#include <tl/mali_kbase_timeline.h>
#include <mmu/mali_kbase_mmu.h>
#include <context/mali_kbase_context_internal.h>

int kbase_context_common_init(struct kbase_context *kctx)
{
	const unsigned long cookies_mask = KBASE_COOKIE_MASK;

	/* creating a context is considered a disjoint event */
	kbase_disjoint_event(kctx->kbdev);

	kctx->as_nr = KBASEP_AS_NR_INVALID;

	atomic_set(&kctx->refcount, 0);

	spin_lock_init(&kctx->mm_update_lock);
	kctx->process_mm = NULL;
	atomic_set(&kctx->nonmapped_pages, 0);
	atomic_set(&kctx->permanent_mapped_pages, 0);
	kctx->tgid = current->tgid;
	kctx->pid = current->pid;

	atomic_set(&kctx->used_pages, 0);

	mutex_init(&kctx->reg_lock);

	spin_lock_init(&kctx->mem_partials_lock);
	INIT_LIST_HEAD(&kctx->mem_partials);

	spin_lock_init(&kctx->waiting_soft_jobs_lock);
	INIT_LIST_HEAD(&kctx->waiting_soft_jobs);

	init_waitqueue_head(&kctx->event_queue);
	atomic_set(&kctx->event_count, 0);
	atomic_set(&kctx->event_closed, false);

	bitmap_copy(kctx->cookies, &cookies_mask, BITS_PER_LONG);

#ifdef CONFIG_GPU_TRACEPOINTS
	atomic_set(&kctx->jctx.work_id, 0);
#endif

	kctx->id = atomic_add_return(1, &(kctx->kbdev->ctx_num)) - 1;

	mutex_init(&kctx->legacy_hwcnt_lock);

	mutex_lock(&kctx->kbdev->kctx_list_lock);
	list_add(&kctx->kctx_list_link, &kctx->kbdev->kctx_list);

	KBASE_TLSTREAM_TL_KBASE_NEW_CTX(kctx->kbdev, kctx->id,
		kctx->kbdev->gpu_props.props.raw_props.gpu_id);
	KBASE_TLSTREAM_TL_NEW_CTX(kctx->kbdev, kctx, kctx->id,
			(u32)(kctx->tgid));
	mutex_unlock(&kctx->kbdev->kctx_list_lock);

	return 0;
}

void kbase_context_common_term(struct kbase_context *kctx)
{
	unsigned long flags;
	int pages;

	mutex_lock(&kctx->kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kctx->kbdev->hwaccess_lock, flags);
	kbase_ctx_sched_remove_ctx(kctx);
	spin_unlock_irqrestore(&kctx->kbdev->hwaccess_lock, flags);
	mutex_unlock(&kctx->kbdev->mmu_hw_mutex);

	pages = atomic_read(&kctx->used_pages);
	if (pages != 0)
		dev_warn(kctx->kbdev->dev,
			"%s: %d pages in use!\n", __func__, pages);

	WARN_ON(atomic_read(&kctx->nonmapped_pages) != 0);

	mutex_lock(&kctx->kbdev->kctx_list_lock);

	KBASE_TLSTREAM_TL_KBASE_DEL_CTX(kctx->kbdev, kctx->id);

	KBASE_TLSTREAM_TL_DEL_CTX(kctx->kbdev, kctx);
	list_del(&kctx->kctx_list_link);
	mutex_unlock(&kctx->kbdev->kctx_list_lock);

	KBASE_KTRACE_ADD(kctx->kbdev, CORE_CTX_DESTROY, kctx, 0u);

	/* Flush the timeline stream, so the user can see the termination
	 * tracepoints being fired.
	 * The "if" statement below is for optimization. It is safe to call
	 * kbase_timeline_streams_flush when timeline is disabled.
	 */
	if (atomic_read(&kctx->kbdev->timeline_flags) != 0)
		kbase_timeline_streams_flush(kctx->kbdev->timeline);

	vfree(kctx);
}

int kbase_context_mem_pool_group_init(struct kbase_context *kctx)
{
	return kbase_mem_pool_group_init(&kctx->mem_pools,
		kctx->kbdev,
		&kctx->kbdev->mem_pool_defaults,
		&kctx->kbdev->mem_pools);
}

void kbase_context_mem_pool_group_term(struct kbase_context *kctx)
{
	kbase_mem_pool_group_term(&kctx->mem_pools);
}

int kbase_context_mmu_init(struct kbase_context *kctx)
{
	kbase_mmu_init(kctx->kbdev,
		&kctx->mmu, kctx,
		base_context_mmu_group_id_get(kctx->create_flags));

	return 0;
}

void kbase_context_mmu_term(struct kbase_context *kctx)
{
	kbase_mmu_term(kctx->kbdev, &kctx->mmu);
}

int kbase_context_mem_alloc_page(struct kbase_context *kctx)
{
	struct page *p;

	p = kbase_mem_alloc_page(&kctx->mem_pools.small[KBASE_MEM_GROUP_SINK]);
	if (!p)
		return -ENOMEM;

	kctx->aliasing_sink_page = as_tagged(page_to_phys(p));

	return 0;
}

void kbase_context_mem_pool_free(struct kbase_context *kctx)
{
	/* drop the aliasing sink page now that it can't be mapped anymore */
	kbase_mem_pool_free(
		&kctx->mem_pools.small[KBASE_MEM_GROUP_SINK],
		as_page(kctx->aliasing_sink_page),
		false);
}

void kbase_context_sticky_resource_term(struct kbase_context *kctx)
{
	unsigned long pending_regions_to_clean;

	kbase_gpu_vm_lock(kctx);
	kbase_sticky_resource_term(kctx);

	/* free pending region setups */
	pending_regions_to_clean = KBASE_COOKIE_MASK;
	bitmap_andnot(&pending_regions_to_clean, &pending_regions_to_clean,
		      kctx->cookies, BITS_PER_LONG);
	while (pending_regions_to_clean) {
		unsigned int cookie = find_first_bit(&pending_regions_to_clean,
				BITS_PER_LONG);

		if (!WARN_ON(!kctx->pending_regions[cookie])) {
			dev_dbg(kctx->kbdev->dev, "Freeing pending unmapped region\n");
			kbase_mem_phy_alloc_put(
				kctx->pending_regions[cookie]->cpu_alloc);
			kbase_mem_phy_alloc_put(
				kctx->pending_regions[cookie]->gpu_alloc);
			kfree(kctx->pending_regions[cookie]);

			kctx->pending_regions[cookie] = NULL;
		}

		bitmap_clear(&pending_regions_to_clean, cookie, 1);
	}
	kbase_gpu_vm_unlock(kctx);
}
