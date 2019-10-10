/*
 *
 * (C) COPYRIGHT 2010-2019 ARM Limited. All rights reserved.
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
#include <mali_midg_regmap.h>
#include <mali_kbase_mem_linux.h>
#include <mali_kbase_dma_fence.h>
#include <mali_kbase_ctx_sched.h>
#include <mali_kbase_mem_pool_group.h>
#include <mali_kbase_tracepoints.h>

struct kbase_context *
kbase_create_context(struct kbase_device *kbdev, bool is_compat,
	base_context_create_flags const flags,
	unsigned long const api_version,
	struct file *const filp)
{
	struct kbase_context *kctx;
	int err;
	struct page *p;
	struct kbasep_js_kctx_info *js_kctx_info = NULL;
	unsigned long irq_flags = 0;

	if (WARN_ON(!kbdev))
		goto out;

	/* Validate flags */
	if (WARN_ON(flags != (flags & BASEP_CONTEXT_CREATE_KERNEL_FLAGS)))
		goto out;

	/* zero-inited as lot of code assume it's zero'ed out on create */
	kctx = vzalloc(sizeof(*kctx));

	if (!kctx)
		goto out;

	/* creating a context is considered a disjoint event */
	kbase_disjoint_event(kbdev);

	kctx->kbdev = kbdev;
	kctx->as_nr = KBASEP_AS_NR_INVALID;
	atomic_set(&kctx->refcount, 0);
	if (is_compat)
		kbase_ctx_flag_set(kctx, KCTX_COMPAT);
#if defined(CONFIG_64BIT)
	else
		kbase_ctx_flag_set(kctx, KCTX_FORCE_SAME_VA);
#endif /* !defined(CONFIG_64BIT) */

	spin_lock_init(&kctx->mm_update_lock);
	kctx->process_mm = NULL;
	atomic_set(&kctx->nonmapped_pages, 0);
	atomic_set(&kctx->permanent_mapped_pages, 0);
	kctx->slots_pullable = 0;
	kctx->tgid = current->tgid;
	kctx->pid = current->pid;

	err = kbase_mem_pool_group_init(&kctx->mem_pools, kbdev,
		&kbdev->mem_pool_defaults, &kbdev->mem_pools);
	if (err)
		goto free_kctx;

	err = kbase_mem_evictable_init(kctx);
	if (err)
		goto free_both_pools;

	atomic_set(&kctx->used_pages, 0);

	err = kbase_jd_init(kctx);
	if (err)
		goto deinit_evictable;

	err = kbasep_js_kctx_init(kctx);
	if (err)
		goto free_jd;	/* safe to call kbasep_js_kctx_term  in this case */

	err = kbase_event_init(kctx);
	if (err)
		goto free_jd;

	atomic_set(&kctx->drain_pending, 0);

	mutex_init(&kctx->reg_lock);

	spin_lock_init(&kctx->mem_partials_lock);
	INIT_LIST_HEAD(&kctx->mem_partials);

	INIT_LIST_HEAD(&kctx->waiting_soft_jobs);
	spin_lock_init(&kctx->waiting_soft_jobs_lock);
	err = kbase_dma_fence_init(kctx);
	if (err)
		goto free_event;

	err = kbase_mmu_init(kbdev, &kctx->mmu, kctx,
		base_context_mmu_group_id_get(flags));
	if (err)
		goto term_dma_fence;

	p = kbase_mem_alloc_page(
		&kctx->mem_pools.small[KBASE_MEM_GROUP_SINK]);
	if (!p)
		goto no_sink_page;
	kctx->aliasing_sink_page = as_tagged(page_to_phys(p));

	init_waitqueue_head(&kctx->event_queue);

	kctx->cookies = KBASE_COOKIE_MASK;

	/* Make sure page 0 is not used... */
	err = kbase_region_tracker_init(kctx);
	if (err)
		goto no_region_tracker;

	err = kbase_sticky_resource_init(kctx);
	if (err)
		goto no_sticky;

	err = kbase_jit_init(kctx);
	if (err)
		goto no_jit;


#ifdef CONFIG_GPU_TRACEPOINTS
	atomic_set(&kctx->jctx.work_id, 0);
#endif

	kctx->id = atomic_add_return(1, &(kbdev->ctx_num)) - 1;

	mutex_init(&kctx->legacy_hwcnt_lock);

	kbase_timer_setup(&kctx->soft_job_timeout,
			  kbasep_soft_job_timeout_worker);

	mutex_lock(&kbdev->kctx_list_lock);
	list_add(&kctx->kctx_list_link, &kbdev->kctx_list);
	KBASE_TLSTREAM_TL_NEW_CTX(kbdev, kctx, kctx->id, (u32)(kctx->tgid));
	mutex_unlock(&kbdev->kctx_list_lock);

	kctx->api_version = api_version;
	kctx->filp = filp;

	js_kctx_info = &kctx->jctx.sched_info;

	mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
	spin_lock_irqsave(&kctx->kbdev->hwaccess_lock, irq_flags);

	/* Translate the flags */
	if ((flags & BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED) == 0)
		kbase_ctx_flag_clear(kctx, KCTX_SUBMIT_DISABLED);

	spin_unlock_irqrestore(&kctx->kbdev->hwaccess_lock, irq_flags);
	mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);

	return kctx;

no_jit:
	kbase_gpu_vm_lock(kctx);
	kbase_sticky_resource_term(kctx);
	kbase_gpu_vm_unlock(kctx);
no_sticky:
	kbase_region_tracker_term(kctx);
no_region_tracker:
	kbase_mem_pool_free(
		&kctx->mem_pools.small[KBASE_MEM_GROUP_SINK], p, false);
no_sink_page:
	kbase_mmu_term(kbdev, &kctx->mmu);
term_dma_fence:
	kbase_dma_fence_term(kctx);
free_event:
	kbase_event_cleanup(kctx);
free_jd:
	/* Safe to call this one even when didn't initialize (assuming kctx was sufficiently zeroed) */
	kbasep_js_kctx_term(kctx);
	kbase_jd_exit(kctx);
deinit_evictable:
	kbase_mem_evictable_deinit(kctx);
free_both_pools:
	kbase_mem_pool_group_term(&kctx->mem_pools);
free_kctx:
	vfree(kctx);
out:
	return NULL;
}
KBASE_EXPORT_SYMBOL(kbase_create_context);

static void kbase_reg_pending_dtor(struct kbase_device *kbdev,
		struct kbase_va_region *reg)
{
	dev_dbg(kbdev->dev, "Freeing pending unmapped region\n");
	kbase_mem_phy_alloc_put(reg->cpu_alloc);
	kbase_mem_phy_alloc_put(reg->gpu_alloc);
	kfree(reg);
}

void kbase_destroy_context(struct kbase_context *kctx)
{
	struct kbase_device *kbdev;
	int pages;
	unsigned long pending_regions_to_clean;
	unsigned long flags;
	struct page *p;

	if (WARN_ON(!kctx))
		return;

	kbdev = kctx->kbdev;
	if (WARN_ON(!kbdev))
		return;

	mutex_lock(&kbdev->kctx_list_lock);
	KBASE_TLSTREAM_TL_DEL_CTX(kbdev, kctx);
	list_del(&kctx->kctx_list_link);
	mutex_unlock(&kbdev->kctx_list_lock);

	KBASE_TRACE_ADD(kbdev, CORE_CTX_DESTROY, kctx, NULL, 0u, 0u);

	/* Ensure the core is powered up for the destroy process */
	/* A suspend won't happen here, because we're in a syscall from a userspace
	 * thread. */
	kbase_pm_context_active(kbdev);

	kbase_mem_pool_group_mark_dying(&kctx->mem_pools);

	kbase_jd_zap_context(kctx);

	/* We have already waited for the jobs to complete (and hereafter there
	 * can be no more submissions for the context). However the wait could
	 * have timedout and there could still be work items in flight that
	 * would do the completion processing of jobs.
	 * kbase_jd_exit() will destroy the 'job_done_wq'. And destroying the wq
	 * will cause it do drain and implicitly wait for those work items to
	 * complete.
	 */
	kbase_jd_exit(kctx);

#ifdef CONFIG_DEBUG_FS
	/* Removing the rest of the debugfs entries here as we want to keep the
	 * atom debugfs interface alive until all atoms have completed. This
	 * is useful for debugging hung contexts. */
	debugfs_remove_recursive(kctx->kctx_dentry);
	kbase_debug_job_fault_context_term(kctx);
#endif

	kbase_event_cleanup(kctx);


	/*
	 * JIT must be terminated before the code below as it must be called
	 * without the region lock being held.
	 * The code above ensures no new JIT allocations can be made by
	 * by the time we get to this point of context tear down.
	 */
	kbase_jit_term(kctx);

	kbase_gpu_vm_lock(kctx);

	kbase_sticky_resource_term(kctx);

	/* drop the aliasing sink page now that it can't be mapped anymore */
	p = as_page(kctx->aliasing_sink_page);
	kbase_mem_pool_free(&kctx->mem_pools.small[KBASE_MEM_GROUP_SINK],
		p, false);

	/* free pending region setups */
	pending_regions_to_clean = (~kctx->cookies) & KBASE_COOKIE_MASK;
	while (pending_regions_to_clean) {
		unsigned int cookie = __ffs(pending_regions_to_clean);

		BUG_ON(!kctx->pending_regions[cookie]);

		kbase_reg_pending_dtor(kbdev, kctx->pending_regions[cookie]);

		kctx->pending_regions[cookie] = NULL;
		pending_regions_to_clean &= ~(1UL << cookie);
	}

	kbase_region_tracker_term(kctx);
	kbase_gpu_vm_unlock(kctx);

	/* Safe to call this one even when didn't initialize (assuming kctx was sufficiently zeroed) */
	kbasep_js_kctx_term(kctx);

	kbase_dma_fence_term(kctx);

	mutex_lock(&kbdev->mmu_hw_mutex);
	spin_lock_irqsave(&kctx->kbdev->hwaccess_lock, flags);
	kbase_ctx_sched_remove_ctx(kctx);
	spin_unlock_irqrestore(&kctx->kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->mmu_hw_mutex);

	kbase_mmu_term(kbdev, &kctx->mmu);

	pages = atomic_read(&kctx->used_pages);
	if (pages != 0)
		dev_warn(kbdev->dev, "%s: %d pages in use!\n", __func__, pages);

	kbase_mem_evictable_deinit(kctx);

	kbase_mem_pool_group_term(&kctx->mem_pools);

	WARN_ON(atomic_read(&kctx->nonmapped_pages) != 0);

	vfree(kctx);

	kbase_pm_context_idle(kbdev);
}
KBASE_EXPORT_SYMBOL(kbase_destroy_context);
