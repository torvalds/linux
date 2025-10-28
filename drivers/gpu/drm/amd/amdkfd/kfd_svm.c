// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2020-2021 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/types.h>
#include <linux/sched/task.h>
#include <linux/dynamic_debug.h>
#include <drm/ttm/ttm_tt.h>
#include <drm/drm_exec.h>

#include "amdgpu_sync.h"
#include "amdgpu_object.h"
#include "amdgpu_vm.h"
#include "amdgpu_hmm.h"
#include "amdgpu.h"
#include "amdgpu_xgmi.h"
#include "kfd_priv.h"
#include "kfd_svm.h"
#include "kfd_migrate.h"
#include "kfd_smi_events.h"

#ifdef dev_fmt
#undef dev_fmt
#endif
#define dev_fmt(fmt) "kfd_svm: %s: " fmt, __func__

#define AMDGPU_SVM_RANGE_RESTORE_DELAY_MS 1

/* Long enough to ensure no retry fault comes after svm range is restored and
 * page table is updated.
 */
#define AMDGPU_SVM_RANGE_RETRY_FAULT_PENDING	(2UL * NSEC_PER_MSEC)
#if IS_ENABLED(CONFIG_DYNAMIC_DEBUG)
#define dynamic_svm_range_dump(svms) \
	_dynamic_func_call_no_desc("svm_range_dump", svm_range_debug_dump, svms)
#else
#define dynamic_svm_range_dump(svms) \
	do { if (0) svm_range_debug_dump(svms); } while (0)
#endif

/* Giant svm range split into smaller ranges based on this, it is decided using
 * minimum of all dGPU/APU 1/32 VRAM size, between 2MB to 1GB and alignment to
 * power of 2MB.
 */
static uint64_t max_svm_range_pages;

struct criu_svm_metadata {
	struct list_head list;
	struct kfd_criu_svm_range_priv_data data;
};

static void svm_range_evict_svm_bo_worker(struct work_struct *work);
static bool
svm_range_cpu_invalidate_pagetables(struct mmu_interval_notifier *mni,
				    const struct mmu_notifier_range *range,
				    unsigned long cur_seq);
static int
svm_range_check_vm(struct kfd_process *p, uint64_t start, uint64_t last,
		   uint64_t *bo_s, uint64_t *bo_l);
static const struct mmu_interval_notifier_ops svm_range_mn_ops = {
	.invalidate = svm_range_cpu_invalidate_pagetables,
};

/**
 * svm_range_unlink - unlink svm_range from lists and interval tree
 * @prange: svm range structure to be removed
 *
 * Remove the svm_range from the svms and svm_bo lists and the svms
 * interval tree.
 *
 * Context: The caller must hold svms->lock
 */
static void svm_range_unlink(struct svm_range *prange)
{
	pr_debug("svms 0x%p prange 0x%p [0x%lx 0x%lx]\n", prange->svms,
		 prange, prange->start, prange->last);

	if (prange->svm_bo) {
		spin_lock(&prange->svm_bo->list_lock);
		list_del(&prange->svm_bo_list);
		spin_unlock(&prange->svm_bo->list_lock);
	}

	list_del(&prange->list);
	if (prange->it_node.start != 0 && prange->it_node.last != 0)
		interval_tree_remove(&prange->it_node, &prange->svms->objects);
}

static void
svm_range_add_notifier_locked(struct mm_struct *mm, struct svm_range *prange)
{
	pr_debug("svms 0x%p prange 0x%p [0x%lx 0x%lx]\n", prange->svms,
		 prange, prange->start, prange->last);

	mmu_interval_notifier_insert_locked(&prange->notifier, mm,
				     prange->start << PAGE_SHIFT,
				     prange->npages << PAGE_SHIFT,
				     &svm_range_mn_ops);
}

/**
 * svm_range_add_to_svms - add svm range to svms
 * @prange: svm range structure to be added
 *
 * Add the svm range to svms interval tree and link list
 *
 * Context: The caller must hold svms->lock
 */
static void svm_range_add_to_svms(struct svm_range *prange)
{
	pr_debug("svms 0x%p prange 0x%p [0x%lx 0x%lx]\n", prange->svms,
		 prange, prange->start, prange->last);

	list_move_tail(&prange->list, &prange->svms->list);
	prange->it_node.start = prange->start;
	prange->it_node.last = prange->last;
	interval_tree_insert(&prange->it_node, &prange->svms->objects);
}

static void svm_range_remove_notifier(struct svm_range *prange)
{
	pr_debug("remove notifier svms 0x%p prange 0x%p [0x%lx 0x%lx]\n",
		 prange->svms, prange,
		 prange->notifier.interval_tree.start >> PAGE_SHIFT,
		 prange->notifier.interval_tree.last >> PAGE_SHIFT);

	if (prange->notifier.interval_tree.start != 0 &&
	    prange->notifier.interval_tree.last != 0)
		mmu_interval_notifier_remove(&prange->notifier);
}

static bool
svm_is_valid_dma_mapping_addr(struct device *dev, dma_addr_t dma_addr)
{
	return dma_addr && !dma_mapping_error(dev, dma_addr) &&
	       !(dma_addr & SVM_RANGE_VRAM_DOMAIN);
}

static int
svm_range_dma_map_dev(struct amdgpu_device *adev, struct svm_range *prange,
		      unsigned long offset, unsigned long npages,
		      unsigned long *hmm_pfns, uint32_t gpuidx)
{
	enum dma_data_direction dir = DMA_BIDIRECTIONAL;
	dma_addr_t *addr = prange->dma_addr[gpuidx];
	struct device *dev = adev->dev;
	struct page *page;
	int i, r;

	if (!addr) {
		addr = kvcalloc(prange->npages, sizeof(*addr), GFP_KERNEL);
		if (!addr)
			return -ENOMEM;
		prange->dma_addr[gpuidx] = addr;
	}

	addr += offset;
	for (i = 0; i < npages; i++) {
		if (svm_is_valid_dma_mapping_addr(dev, addr[i]))
			dma_unmap_page(dev, addr[i], PAGE_SIZE, dir);

		page = hmm_pfn_to_page(hmm_pfns[i]);
		if (is_zone_device_page(page)) {
			struct amdgpu_device *bo_adev = prange->svm_bo->node->adev;

			addr[i] = (hmm_pfns[i] << PAGE_SHIFT) +
				   bo_adev->vm_manager.vram_base_offset -
				   bo_adev->kfd.pgmap.range.start;
			addr[i] |= SVM_RANGE_VRAM_DOMAIN;
			pr_debug_ratelimited("vram address: 0x%llx\n", addr[i]);
			continue;
		}
		addr[i] = dma_map_page(dev, page, 0, PAGE_SIZE, dir);
		r = dma_mapping_error(dev, addr[i]);
		if (r) {
			dev_err(dev, "failed %d dma_map_page\n", r);
			return r;
		}
		pr_debug_ratelimited("dma mapping 0x%llx for page addr 0x%lx\n",
				     addr[i] >> PAGE_SHIFT, page_to_pfn(page));
	}

	return 0;
}

static int
svm_range_dma_map(struct svm_range *prange, unsigned long *bitmap,
		  unsigned long offset, unsigned long npages,
		  unsigned long *hmm_pfns)
{
	struct kfd_process *p;
	uint32_t gpuidx;
	int r;

	p = container_of(prange->svms, struct kfd_process, svms);

	for_each_set_bit(gpuidx, bitmap, MAX_GPU_INSTANCE) {
		struct kfd_process_device *pdd;

		pr_debug("mapping to gpu idx 0x%x\n", gpuidx);
		pdd = kfd_process_device_from_gpuidx(p, gpuidx);
		if (!pdd) {
			pr_debug("failed to find device idx %d\n", gpuidx);
			return -EINVAL;
		}

		r = svm_range_dma_map_dev(pdd->dev->adev, prange, offset, npages,
					  hmm_pfns, gpuidx);
		if (r)
			break;
	}

	return r;
}

void svm_range_dma_unmap_dev(struct device *dev, dma_addr_t *dma_addr,
			 unsigned long offset, unsigned long npages)
{
	enum dma_data_direction dir = DMA_BIDIRECTIONAL;
	int i;

	if (!dma_addr)
		return;

	for (i = offset; i < offset + npages; i++) {
		if (!svm_is_valid_dma_mapping_addr(dev, dma_addr[i]))
			continue;
		pr_debug_ratelimited("unmap 0x%llx\n", dma_addr[i] >> PAGE_SHIFT);
		dma_unmap_page(dev, dma_addr[i], PAGE_SIZE, dir);
		dma_addr[i] = 0;
	}
}

void svm_range_dma_unmap(struct svm_range *prange)
{
	struct kfd_process_device *pdd;
	dma_addr_t *dma_addr;
	struct device *dev;
	struct kfd_process *p;
	uint32_t gpuidx;

	p = container_of(prange->svms, struct kfd_process, svms);

	for (gpuidx = 0; gpuidx < MAX_GPU_INSTANCE; gpuidx++) {
		dma_addr = prange->dma_addr[gpuidx];
		if (!dma_addr)
			continue;

		pdd = kfd_process_device_from_gpuidx(p, gpuidx);
		if (!pdd) {
			pr_debug("failed to find device idx %d\n", gpuidx);
			continue;
		}
		dev = &pdd->dev->adev->pdev->dev;

		svm_range_dma_unmap_dev(dev, dma_addr, 0, prange->npages);
	}
}

static void svm_range_free(struct svm_range *prange, bool do_unmap)
{
	uint64_t size = (prange->last - prange->start + 1) << PAGE_SHIFT;
	struct kfd_process *p = container_of(prange->svms, struct kfd_process, svms);
	uint32_t gpuidx;

	pr_debug("svms 0x%p prange 0x%p [0x%lx 0x%lx]\n", prange->svms, prange,
		 prange->start, prange->last);

	svm_range_vram_node_free(prange);
	if (do_unmap)
		svm_range_dma_unmap(prange);

	if (do_unmap && !p->xnack_enabled) {
		pr_debug("unreserve prange 0x%p size: 0x%llx\n", prange, size);
		amdgpu_amdkfd_unreserve_mem_limit(NULL, size,
					KFD_IOC_ALLOC_MEM_FLAGS_USERPTR, 0);
	}

	/* free dma_addr array for each gpu */
	for (gpuidx = 0; gpuidx < MAX_GPU_INSTANCE; gpuidx++) {
		if (prange->dma_addr[gpuidx]) {
			kvfree(prange->dma_addr[gpuidx]);
			prange->dma_addr[gpuidx] = NULL;
		}
	}

	mutex_destroy(&prange->lock);
	mutex_destroy(&prange->migrate_mutex);
	kfree(prange);
}

static void
svm_range_set_default_attributes(struct svm_range_list *svms, int32_t *location,
				 int32_t *prefetch_loc, uint8_t *granularity,
				 uint32_t *flags)
{
	*location = KFD_IOCTL_SVM_LOCATION_UNDEFINED;
	*prefetch_loc = KFD_IOCTL_SVM_LOCATION_UNDEFINED;
	*granularity = svms->default_granularity;
	*flags =
		KFD_IOCTL_SVM_FLAG_HOST_ACCESS | KFD_IOCTL_SVM_FLAG_COHERENT;
}

static struct
svm_range *svm_range_new(struct svm_range_list *svms, uint64_t start,
			 uint64_t last, bool update_mem_usage)
{
	uint64_t size = last - start + 1;
	struct svm_range *prange;
	struct kfd_process *p;

	prange = kzalloc(sizeof(*prange), GFP_KERNEL);
	if (!prange)
		return NULL;

	p = container_of(svms, struct kfd_process, svms);
	if (!p->xnack_enabled && update_mem_usage &&
	    amdgpu_amdkfd_reserve_mem_limit(NULL, size << PAGE_SHIFT,
				    KFD_IOC_ALLOC_MEM_FLAGS_USERPTR, 0)) {
		pr_info("SVM mapping failed, exceeds resident system memory limit\n");
		kfree(prange);
		return NULL;
	}
	prange->npages = size;
	prange->svms = svms;
	prange->start = start;
	prange->last = last;
	INIT_LIST_HEAD(&prange->list);
	INIT_LIST_HEAD(&prange->update_list);
	INIT_LIST_HEAD(&prange->svm_bo_list);
	INIT_LIST_HEAD(&prange->deferred_list);
	INIT_LIST_HEAD(&prange->child_list);
	atomic_set(&prange->invalid, 0);
	prange->validate_timestamp = 0;
	prange->vram_pages = 0;
	mutex_init(&prange->migrate_mutex);
	mutex_init(&prange->lock);

	if (p->xnack_enabled)
		bitmap_copy(prange->bitmap_access, svms->bitmap_supported,
			    MAX_GPU_INSTANCE);

	svm_range_set_default_attributes(svms, &prange->preferred_loc,
					 &prange->prefetch_loc,
					 &prange->granularity, &prange->flags);

	pr_debug("svms 0x%p [0x%llx 0x%llx]\n", svms, start, last);

	return prange;
}

static bool svm_bo_ref_unless_zero(struct svm_range_bo *svm_bo)
{
	if (!svm_bo || !kref_get_unless_zero(&svm_bo->kref))
		return false;

	return true;
}

static void svm_range_bo_release(struct kref *kref)
{
	struct svm_range_bo *svm_bo;

	svm_bo = container_of(kref, struct svm_range_bo, kref);
	pr_debug("svm_bo 0x%p\n", svm_bo);

	spin_lock(&svm_bo->list_lock);
	while (!list_empty(&svm_bo->range_list)) {
		struct svm_range *prange =
				list_first_entry(&svm_bo->range_list,
						struct svm_range, svm_bo_list);
		/* list_del_init tells a concurrent svm_range_vram_node_new when
		 * it's safe to reuse the svm_bo pointer and svm_bo_list head.
		 */
		list_del_init(&prange->svm_bo_list);
		spin_unlock(&svm_bo->list_lock);

		pr_debug("svms 0x%p [0x%lx 0x%lx]\n", prange->svms,
			 prange->start, prange->last);
		mutex_lock(&prange->lock);
		prange->svm_bo = NULL;
		/* prange should not hold vram page now */
		WARN_ONCE(prange->actual_loc, "prange should not hold vram page");
		mutex_unlock(&prange->lock);

		spin_lock(&svm_bo->list_lock);
	}
	spin_unlock(&svm_bo->list_lock);

	if (mmget_not_zero(svm_bo->eviction_fence->mm)) {
		struct kfd_process_device *pdd;
		struct kfd_process *p;
		struct mm_struct *mm;

		mm = svm_bo->eviction_fence->mm;
		/*
		 * The forked child process takes svm_bo device pages ref, svm_bo could be
		 * released after parent process is gone.
		 */
		p = kfd_lookup_process_by_mm(mm);
		if (p) {
			pdd = kfd_get_process_device_data(svm_bo->node, p);
			if (pdd)
				atomic64_sub(amdgpu_bo_size(svm_bo->bo), &pdd->vram_usage);
			kfd_unref_process(p);
		}
		mmput(mm);
	}

	if (!dma_fence_is_signaled(&svm_bo->eviction_fence->base))
		/* We're not in the eviction worker. Signal the fence. */
		dma_fence_signal(&svm_bo->eviction_fence->base);
	dma_fence_put(&svm_bo->eviction_fence->base);
	amdgpu_bo_unref(&svm_bo->bo);
	kfree(svm_bo);
}

static void svm_range_bo_wq_release(struct work_struct *work)
{
	struct svm_range_bo *svm_bo;

	svm_bo = container_of(work, struct svm_range_bo, release_work);
	svm_range_bo_release(&svm_bo->kref);
}

static void svm_range_bo_release_async(struct kref *kref)
{
	struct svm_range_bo *svm_bo;

	svm_bo = container_of(kref, struct svm_range_bo, kref);
	pr_debug("svm_bo 0x%p\n", svm_bo);
	INIT_WORK(&svm_bo->release_work, svm_range_bo_wq_release);
	schedule_work(&svm_bo->release_work);
}

void svm_range_bo_unref_async(struct svm_range_bo *svm_bo)
{
	kref_put(&svm_bo->kref, svm_range_bo_release_async);
}

static void svm_range_bo_unref(struct svm_range_bo *svm_bo)
{
	if (svm_bo)
		kref_put(&svm_bo->kref, svm_range_bo_release);
}

static bool
svm_range_validate_svm_bo(struct kfd_node *node, struct svm_range *prange)
{
	mutex_lock(&prange->lock);
	if (!prange->svm_bo) {
		mutex_unlock(&prange->lock);
		return false;
	}
	if (prange->ttm_res) {
		/* We still have a reference, all is well */
		mutex_unlock(&prange->lock);
		return true;
	}
	if (svm_bo_ref_unless_zero(prange->svm_bo)) {
		/*
		 * Migrate from GPU to GPU, remove range from source svm_bo->node
		 * range list, and return false to allocate svm_bo from destination
		 * node.
		 */
		if (prange->svm_bo->node != node) {
			mutex_unlock(&prange->lock);

			spin_lock(&prange->svm_bo->list_lock);
			list_del_init(&prange->svm_bo_list);
			spin_unlock(&prange->svm_bo->list_lock);

			svm_range_bo_unref(prange->svm_bo);
			return false;
		}
		if (READ_ONCE(prange->svm_bo->evicting)) {
			struct dma_fence *f;
			struct svm_range_bo *svm_bo;
			/* The BO is getting evicted,
			 * we need to get a new one
			 */
			mutex_unlock(&prange->lock);
			svm_bo = prange->svm_bo;
			f = dma_fence_get(&svm_bo->eviction_fence->base);
			svm_range_bo_unref(prange->svm_bo);
			/* wait for the fence to avoid long spin-loop
			 * at list_empty_careful
			 */
			dma_fence_wait(f, false);
			dma_fence_put(f);
		} else {
			/* The BO was still around and we got
			 * a new reference to it
			 */
			mutex_unlock(&prange->lock);
			pr_debug("reuse old bo svms 0x%p [0x%lx 0x%lx]\n",
				 prange->svms, prange->start, prange->last);

			prange->ttm_res = prange->svm_bo->bo->tbo.resource;
			return true;
		}

	} else {
		mutex_unlock(&prange->lock);
	}

	/* We need a new svm_bo. Spin-loop to wait for concurrent
	 * svm_range_bo_release to finish removing this range from
	 * its range list and set prange->svm_bo to null. After this,
	 * it is safe to reuse the svm_bo pointer and svm_bo_list head.
	 */
	while (!list_empty_careful(&prange->svm_bo_list) || prange->svm_bo)
		cond_resched();

	return false;
}

static struct svm_range_bo *svm_range_bo_new(void)
{
	struct svm_range_bo *svm_bo;

	svm_bo = kzalloc(sizeof(*svm_bo), GFP_KERNEL);
	if (!svm_bo)
		return NULL;

	kref_init(&svm_bo->kref);
	INIT_LIST_HEAD(&svm_bo->range_list);
	spin_lock_init(&svm_bo->list_lock);

	return svm_bo;
}

int
svm_range_vram_node_new(struct kfd_node *node, struct svm_range *prange,
			bool clear)
{
	struct kfd_process_device *pdd;
	struct amdgpu_bo_param bp;
	struct svm_range_bo *svm_bo;
	struct amdgpu_bo_user *ubo;
	struct amdgpu_bo *bo;
	struct kfd_process *p;
	struct mm_struct *mm;
	int r;

	p = container_of(prange->svms, struct kfd_process, svms);
	pr_debug("process pid: %d svms 0x%p [0x%lx 0x%lx]\n",
		 p->lead_thread->pid, prange->svms,
		 prange->start, prange->last);

	if (svm_range_validate_svm_bo(node, prange))
		return 0;

	svm_bo = svm_range_bo_new();
	if (!svm_bo) {
		pr_debug("failed to alloc svm bo\n");
		return -ENOMEM;
	}
	mm = get_task_mm(p->lead_thread);
	if (!mm) {
		pr_debug("failed to get mm\n");
		kfree(svm_bo);
		return -ESRCH;
	}
	svm_bo->node = node;
	svm_bo->eviction_fence =
		amdgpu_amdkfd_fence_create(dma_fence_context_alloc(1),
					   mm,
					   svm_bo);
	mmput(mm);
	INIT_WORK(&svm_bo->eviction_work, svm_range_evict_svm_bo_worker);
	svm_bo->evicting = 0;
	memset(&bp, 0, sizeof(bp));
	bp.size = prange->npages * PAGE_SIZE;
	bp.byte_align = PAGE_SIZE;
	bp.domain = AMDGPU_GEM_DOMAIN_VRAM;
	bp.flags = AMDGPU_GEM_CREATE_NO_CPU_ACCESS;
	bp.flags |= clear ? AMDGPU_GEM_CREATE_VRAM_CLEARED : 0;
	bp.flags |= AMDGPU_GEM_CREATE_DISCARDABLE;
	bp.type = ttm_bo_type_device;
	bp.resv = NULL;
	if (node->xcp)
		bp.xcp_id_plus1 = node->xcp->id + 1;

	r = amdgpu_bo_create_user(node->adev, &bp, &ubo);
	if (r) {
		pr_debug("failed %d to create bo\n", r);
		goto create_bo_failed;
	}
	bo = &ubo->bo;

	pr_debug("alloc bo at offset 0x%lx size 0x%lx on partition %d\n",
		 bo->tbo.resource->start << PAGE_SHIFT, bp.size,
		 bp.xcp_id_plus1 - 1);

	r = amdgpu_bo_reserve(bo, true);
	if (r) {
		pr_debug("failed %d to reserve bo\n", r);
		goto reserve_bo_failed;
	}

	if (clear) {
		r = amdgpu_bo_sync_wait(bo, AMDGPU_FENCE_OWNER_KFD, false);
		if (r) {
			pr_debug("failed %d to sync bo\n", r);
			amdgpu_bo_unreserve(bo);
			goto reserve_bo_failed;
		}
	}

	r = dma_resv_reserve_fences(bo->tbo.base.resv, 1);
	if (r) {
		pr_debug("failed %d to reserve bo\n", r);
		amdgpu_bo_unreserve(bo);
		goto reserve_bo_failed;
	}
	amdgpu_bo_fence(bo, &svm_bo->eviction_fence->base, true);

	amdgpu_bo_unreserve(bo);

	svm_bo->bo = bo;
	prange->svm_bo = svm_bo;
	prange->ttm_res = bo->tbo.resource;
	prange->offset = 0;

	spin_lock(&svm_bo->list_lock);
	list_add(&prange->svm_bo_list, &svm_bo->range_list);
	spin_unlock(&svm_bo->list_lock);

	pdd = svm_range_get_pdd_by_node(prange, node);
	if (pdd)
		atomic64_add(amdgpu_bo_size(bo), &pdd->vram_usage);

	return 0;

reserve_bo_failed:
	amdgpu_bo_unref(&bo);
create_bo_failed:
	dma_fence_put(&svm_bo->eviction_fence->base);
	kfree(svm_bo);
	prange->ttm_res = NULL;

	return r;
}

void svm_range_vram_node_free(struct svm_range *prange)
{
	/* serialize prange->svm_bo unref */
	mutex_lock(&prange->lock);
	/* prange->svm_bo has not been unref */
	if (prange->ttm_res) {
		prange->ttm_res = NULL;
		mutex_unlock(&prange->lock);
		svm_range_bo_unref(prange->svm_bo);
	} else
		mutex_unlock(&prange->lock);
}

struct kfd_node *
svm_range_get_node_by_id(struct svm_range *prange, uint32_t gpu_id)
{
	struct kfd_process *p;
	struct kfd_process_device *pdd;

	p = container_of(prange->svms, struct kfd_process, svms);
	pdd = kfd_process_device_data_by_id(p, gpu_id);
	if (!pdd) {
		pr_debug("failed to get kfd process device by id 0x%x\n", gpu_id);
		return NULL;
	}

	return pdd->dev;
}

struct kfd_process_device *
svm_range_get_pdd_by_node(struct svm_range *prange, struct kfd_node *node)
{
	struct kfd_process *p;

	p = container_of(prange->svms, struct kfd_process, svms);

	return kfd_get_process_device_data(node, p);
}

static int svm_range_bo_validate(void *param, struct amdgpu_bo *bo)
{
	struct ttm_operation_ctx ctx = { false, false };

	amdgpu_bo_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_VRAM);

	return ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
}

static int
svm_range_check_attr(struct kfd_process *p,
		     uint32_t nattr, struct kfd_ioctl_svm_attribute *attrs)
{
	uint32_t i;

	for (i = 0; i < nattr; i++) {
		uint32_t val = attrs[i].value;
		int gpuidx = MAX_GPU_INSTANCE;

		switch (attrs[i].type) {
		case KFD_IOCTL_SVM_ATTR_PREFERRED_LOC:
			if (val != KFD_IOCTL_SVM_LOCATION_SYSMEM &&
			    val != KFD_IOCTL_SVM_LOCATION_UNDEFINED)
				gpuidx = kfd_process_gpuidx_from_gpuid(p, val);
			break;
		case KFD_IOCTL_SVM_ATTR_PREFETCH_LOC:
			if (val != KFD_IOCTL_SVM_LOCATION_SYSMEM)
				gpuidx = kfd_process_gpuidx_from_gpuid(p, val);
			break;
		case KFD_IOCTL_SVM_ATTR_ACCESS:
		case KFD_IOCTL_SVM_ATTR_ACCESS_IN_PLACE:
		case KFD_IOCTL_SVM_ATTR_NO_ACCESS:
			gpuidx = kfd_process_gpuidx_from_gpuid(p, val);
			break;
		case KFD_IOCTL_SVM_ATTR_SET_FLAGS:
			break;
		case KFD_IOCTL_SVM_ATTR_CLR_FLAGS:
			break;
		case KFD_IOCTL_SVM_ATTR_GRANULARITY:
			break;
		default:
			pr_debug("unknown attr type 0x%x\n", attrs[i].type);
			return -EINVAL;
		}

		if (gpuidx < 0) {
			pr_debug("no GPU 0x%x found\n", val);
			return -EINVAL;
		} else if (gpuidx < MAX_GPU_INSTANCE &&
			   !test_bit(gpuidx, p->svms.bitmap_supported)) {
			pr_debug("GPU 0x%x not supported\n", val);
			return -EINVAL;
		}
	}

	return 0;
}

static void
svm_range_apply_attrs(struct kfd_process *p, struct svm_range *prange,
		      uint32_t nattr, struct kfd_ioctl_svm_attribute *attrs,
		      bool *update_mapping)
{
	uint32_t i;
	int gpuidx;

	for (i = 0; i < nattr; i++) {
		switch (attrs[i].type) {
		case KFD_IOCTL_SVM_ATTR_PREFERRED_LOC:
			prange->preferred_loc = attrs[i].value;
			break;
		case KFD_IOCTL_SVM_ATTR_PREFETCH_LOC:
			prange->prefetch_loc = attrs[i].value;
			break;
		case KFD_IOCTL_SVM_ATTR_ACCESS:
		case KFD_IOCTL_SVM_ATTR_ACCESS_IN_PLACE:
		case KFD_IOCTL_SVM_ATTR_NO_ACCESS:
			if (!p->xnack_enabled)
				*update_mapping = true;

			gpuidx = kfd_process_gpuidx_from_gpuid(p,
							       attrs[i].value);
			if (attrs[i].type == KFD_IOCTL_SVM_ATTR_NO_ACCESS) {
				bitmap_clear(prange->bitmap_access, gpuidx, 1);
				bitmap_clear(prange->bitmap_aip, gpuidx, 1);
			} else if (attrs[i].type == KFD_IOCTL_SVM_ATTR_ACCESS) {
				bitmap_set(prange->bitmap_access, gpuidx, 1);
				bitmap_clear(prange->bitmap_aip, gpuidx, 1);
			} else {
				bitmap_clear(prange->bitmap_access, gpuidx, 1);
				bitmap_set(prange->bitmap_aip, gpuidx, 1);
			}
			break;
		case KFD_IOCTL_SVM_ATTR_SET_FLAGS:
			*update_mapping = true;
			prange->flags |= attrs[i].value;
			break;
		case KFD_IOCTL_SVM_ATTR_CLR_FLAGS:
			*update_mapping = true;
			prange->flags &= ~attrs[i].value;
			break;
		case KFD_IOCTL_SVM_ATTR_GRANULARITY:
			prange->granularity = min_t(uint32_t, attrs[i].value, 0x3F);
			break;
		default:
			WARN_ONCE(1, "svm_range_check_attrs wasn't called?");
		}
	}
}

static bool
svm_range_is_same_attrs(struct kfd_process *p, struct svm_range *prange,
			uint32_t nattr, struct kfd_ioctl_svm_attribute *attrs)
{
	uint32_t i;
	int gpuidx;

	for (i = 0; i < nattr; i++) {
		switch (attrs[i].type) {
		case KFD_IOCTL_SVM_ATTR_PREFERRED_LOC:
			if (prange->preferred_loc != attrs[i].value)
				return false;
			break;
		case KFD_IOCTL_SVM_ATTR_PREFETCH_LOC:
			/* Prefetch should always trigger a migration even
			 * if the value of the attribute didn't change.
			 */
			return false;
		case KFD_IOCTL_SVM_ATTR_ACCESS:
		case KFD_IOCTL_SVM_ATTR_ACCESS_IN_PLACE:
		case KFD_IOCTL_SVM_ATTR_NO_ACCESS:
			gpuidx = kfd_process_gpuidx_from_gpuid(p,
							       attrs[i].value);
			if (attrs[i].type == KFD_IOCTL_SVM_ATTR_NO_ACCESS) {
				if (test_bit(gpuidx, prange->bitmap_access) ||
				    test_bit(gpuidx, prange->bitmap_aip))
					return false;
			} else if (attrs[i].type == KFD_IOCTL_SVM_ATTR_ACCESS) {
				if (!test_bit(gpuidx, prange->bitmap_access))
					return false;
			} else {
				if (!test_bit(gpuidx, prange->bitmap_aip))
					return false;
			}
			break;
		case KFD_IOCTL_SVM_ATTR_SET_FLAGS:
			if ((prange->flags & attrs[i].value) != attrs[i].value)
				return false;
			break;
		case KFD_IOCTL_SVM_ATTR_CLR_FLAGS:
			if ((prange->flags & attrs[i].value) != 0)
				return false;
			break;
		case KFD_IOCTL_SVM_ATTR_GRANULARITY:
			if (prange->granularity != attrs[i].value)
				return false;
			break;
		default:
			WARN_ONCE(1, "svm_range_check_attrs wasn't called?");
		}
	}

	return true;
}

/**
 * svm_range_debug_dump - print all range information from svms
 * @svms: svm range list header
 *
 * debug output svm range start, end, prefetch location from svms
 * interval tree and link list
 *
 * Context: The caller must hold svms->lock
 */
static void svm_range_debug_dump(struct svm_range_list *svms)
{
	struct interval_tree_node *node;
	struct svm_range *prange;

	pr_debug("dump svms 0x%p list\n", svms);
	pr_debug("range\tstart\tpage\tend\t\tlocation\n");

	list_for_each_entry(prange, &svms->list, list) {
		pr_debug("0x%p 0x%lx\t0x%llx\t0x%llx\t0x%x\n",
			 prange, prange->start, prange->npages,
			 prange->start + prange->npages - 1,
			 prange->actual_loc);
	}

	pr_debug("dump svms 0x%p interval tree\n", svms);
	pr_debug("range\tstart\tpage\tend\t\tlocation\n");
	node = interval_tree_iter_first(&svms->objects, 0, ~0ULL);
	while (node) {
		prange = container_of(node, struct svm_range, it_node);
		pr_debug("0x%p 0x%lx\t0x%llx\t0x%llx\t0x%x\n",
			 prange, prange->start, prange->npages,
			 prange->start + prange->npages - 1,
			 prange->actual_loc);
		node = interval_tree_iter_next(node, 0, ~0ULL);
	}
}

static void *
svm_range_copy_array(void *psrc, size_t size, uint64_t num_elements,
		     uint64_t offset, uint64_t *vram_pages)
{
	unsigned char *src = (unsigned char *)psrc + offset;
	unsigned char *dst;
	uint64_t i;

	dst = kvmalloc_array(num_elements, size, GFP_KERNEL);
	if (!dst)
		return NULL;

	if (!vram_pages) {
		memcpy(dst, src, num_elements * size);
		return (void *)dst;
	}

	*vram_pages = 0;
	for (i = 0; i < num_elements; i++) {
		dma_addr_t *temp;
		temp = (dma_addr_t *)dst + i;
		*temp = *((dma_addr_t *)src + i);
		if (*temp&SVM_RANGE_VRAM_DOMAIN)
			(*vram_pages)++;
	}

	return (void *)dst;
}

static int
svm_range_copy_dma_addrs(struct svm_range *dst, struct svm_range *src)
{
	int i;

	for (i = 0; i < MAX_GPU_INSTANCE; i++) {
		if (!src->dma_addr[i])
			continue;
		dst->dma_addr[i] = svm_range_copy_array(src->dma_addr[i],
					sizeof(*src->dma_addr[i]), src->npages, 0, NULL);
		if (!dst->dma_addr[i])
			return -ENOMEM;
	}

	return 0;
}

static int
svm_range_split_array(void *ppnew, void *ppold, size_t size,
		      uint64_t old_start, uint64_t old_n,
		      uint64_t new_start, uint64_t new_n, uint64_t *new_vram_pages)
{
	unsigned char *new, *old, *pold;
	uint64_t d;

	if (!ppold)
		return 0;
	pold = *(unsigned char **)ppold;
	if (!pold)
		return 0;

	d = (new_start - old_start) * size;
	/* get dma addr array for new range and calculte its vram page number */
	new = svm_range_copy_array(pold, size, new_n, d, new_vram_pages);
	if (!new)
		return -ENOMEM;
	d = (new_start == old_start) ? new_n * size : 0;
	old = svm_range_copy_array(pold, size, old_n, d, NULL);
	if (!old) {
		kvfree(new);
		return -ENOMEM;
	}
	kvfree(pold);
	*(void **)ppold = old;
	*(void **)ppnew = new;

	return 0;
}

static int
svm_range_split_pages(struct svm_range *new, struct svm_range *old,
		      uint64_t start, uint64_t last)
{
	uint64_t npages = last - start + 1;
	int i, r;

	for (i = 0; i < MAX_GPU_INSTANCE; i++) {
		r = svm_range_split_array(&new->dma_addr[i], &old->dma_addr[i],
					  sizeof(*old->dma_addr[i]), old->start,
					  npages, new->start, new->npages,
					  old->actual_loc ? &new->vram_pages : NULL);
		if (r)
			return r;
	}
	if (old->actual_loc)
		old->vram_pages -= new->vram_pages;

	return 0;
}

static int
svm_range_split_nodes(struct svm_range *new, struct svm_range *old,
		      uint64_t start, uint64_t last)
{
	uint64_t npages = last - start + 1;

	pr_debug("svms 0x%p new prange 0x%p start 0x%lx [0x%llx 0x%llx]\n",
		 new->svms, new, new->start, start, last);

	if (new->start == old->start) {
		new->offset = old->offset;
		old->offset += new->npages;
	} else {
		new->offset = old->offset + npages;
	}

	new->svm_bo = svm_range_bo_ref(old->svm_bo);
	new->ttm_res = old->ttm_res;

	spin_lock(&new->svm_bo->list_lock);
	list_add(&new->svm_bo_list, &new->svm_bo->range_list);
	spin_unlock(&new->svm_bo->list_lock);

	return 0;
}

/**
 * svm_range_split_adjust - split range and adjust
 *
 * @new: new range
 * @old: the old range
 * @start: the old range adjust to start address in pages
 * @last: the old range adjust to last address in pages
 *
 * Copy system memory dma_addr or vram ttm_res in old range to new
 * range from new_start up to size new->npages, the remaining old range is from
 * start to last
 *
 * Return:
 * 0 - OK, -ENOMEM - out of memory
 */
static int
svm_range_split_adjust(struct svm_range *new, struct svm_range *old,
		      uint64_t start, uint64_t last)
{
	int r;

	pr_debug("svms 0x%p new 0x%lx old [0x%lx 0x%lx] => [0x%llx 0x%llx]\n",
		 new->svms, new->start, old->start, old->last, start, last);

	if (new->start < old->start ||
	    new->last > old->last) {
		WARN_ONCE(1, "invalid new range start or last\n");
		return -EINVAL;
	}

	r = svm_range_split_pages(new, old, start, last);
	if (r)
		return r;

	if (old->actual_loc && old->ttm_res) {
		r = svm_range_split_nodes(new, old, start, last);
		if (r)
			return r;
	}

	old->npages = last - start + 1;
	old->start = start;
	old->last = last;
	new->flags = old->flags;
	new->preferred_loc = old->preferred_loc;
	new->prefetch_loc = old->prefetch_loc;
	new->actual_loc = old->actual_loc;
	new->granularity = old->granularity;
	new->mapped_to_gpu = old->mapped_to_gpu;
	bitmap_copy(new->bitmap_access, old->bitmap_access, MAX_GPU_INSTANCE);
	bitmap_copy(new->bitmap_aip, old->bitmap_aip, MAX_GPU_INSTANCE);
	atomic_set(&new->queue_refcount, atomic_read(&old->queue_refcount));

	return 0;
}

/**
 * svm_range_split - split a range in 2 ranges
 *
 * @prange: the svm range to split
 * @start: the remaining range start address in pages
 * @last: the remaining range last address in pages
 * @new: the result new range generated
 *
 * Two cases only:
 * case 1: if start == prange->start
 *         prange ==> prange[start, last]
 *         new range [last + 1, prange->last]
 *
 * case 2: if last == prange->last
 *         prange ==> prange[start, last]
 *         new range [prange->start, start - 1]
 *
 * Return:
 * 0 - OK, -ENOMEM - out of memory, -EINVAL - invalid start, last
 */
static int
svm_range_split(struct svm_range *prange, uint64_t start, uint64_t last,
		struct svm_range **new)
{
	uint64_t old_start = prange->start;
	uint64_t old_last = prange->last;
	struct svm_range_list *svms;
	int r = 0;

	pr_debug("svms 0x%p [0x%llx 0x%llx] to [0x%llx 0x%llx]\n", prange->svms,
		 old_start, old_last, start, last);

	if (old_start != start && old_last != last)
		return -EINVAL;
	if (start < old_start || last > old_last)
		return -EINVAL;

	svms = prange->svms;
	if (old_start == start)
		*new = svm_range_new(svms, last + 1, old_last, false);
	else
		*new = svm_range_new(svms, old_start, start - 1, false);
	if (!*new)
		return -ENOMEM;

	r = svm_range_split_adjust(*new, prange, start, last);
	if (r) {
		pr_debug("failed %d split [0x%llx 0x%llx] to [0x%llx 0x%llx]\n",
			 r, old_start, old_last, start, last);
		svm_range_free(*new, false);
		*new = NULL;
	}

	return r;
}

static int
svm_range_split_tail(struct svm_range *prange, uint64_t new_last,
		     struct list_head *insert_list, struct list_head *remap_list)
{
	struct svm_range *tail = NULL;
	int r = svm_range_split(prange, prange->start, new_last, &tail);

	if (!r) {
		list_add(&tail->list, insert_list);
		if (!IS_ALIGNED(new_last + 1, 1UL << prange->granularity))
			list_add(&tail->update_list, remap_list);
	}
	return r;
}

static int
svm_range_split_head(struct svm_range *prange, uint64_t new_start,
		     struct list_head *insert_list, struct list_head *remap_list)
{
	struct svm_range *head = NULL;
	int r = svm_range_split(prange, new_start, prange->last, &head);

	if (!r) {
		list_add(&head->list, insert_list);
		if (!IS_ALIGNED(new_start, 1UL << prange->granularity))
			list_add(&head->update_list, remap_list);
	}
	return r;
}

static void
svm_range_add_child(struct svm_range *prange, struct svm_range *pchild, enum svm_work_list_ops op)
{
	pr_debug("add child 0x%p [0x%lx 0x%lx] to prange 0x%p child list %d\n",
		 pchild, pchild->start, pchild->last, prange, op);

	pchild->work_item.mm = NULL;
	pchild->work_item.op = op;
	list_add_tail(&pchild->child_list, &prange->child_list);
}

static bool
svm_nodes_in_same_hive(struct kfd_node *node_a, struct kfd_node *node_b)
{
	return (node_a->adev == node_b->adev ||
		amdgpu_xgmi_same_hive(node_a->adev, node_b->adev));
}

static uint64_t
svm_range_get_pte_flags(struct kfd_node *node, struct amdgpu_vm *vm,
			struct svm_range *prange, int domain)
{
	struct kfd_node *bo_node;
	uint32_t flags = prange->flags;
	uint32_t mapping_flags = 0;
	uint32_t gc_ip_version = KFD_GC_VERSION(node);
	uint64_t pte_flags;
	bool snoop = (domain != SVM_RANGE_VRAM_DOMAIN);
	bool coherent = flags & (KFD_IOCTL_SVM_FLAG_COHERENT | KFD_IOCTL_SVM_FLAG_EXT_COHERENT);
	bool ext_coherent = flags & KFD_IOCTL_SVM_FLAG_EXT_COHERENT;
	unsigned int mtype_local;

	if (domain == SVM_RANGE_VRAM_DOMAIN)
		bo_node = prange->svm_bo->node;

	switch (gc_ip_version) {
	case IP_VERSION(9, 4, 1):
		if (domain == SVM_RANGE_VRAM_DOMAIN) {
			if (bo_node == node) {
				mapping_flags |= coherent ?
					AMDGPU_VM_MTYPE_CC : AMDGPU_VM_MTYPE_RW;
			} else {
				mapping_flags |= coherent ?
					AMDGPU_VM_MTYPE_UC : AMDGPU_VM_MTYPE_NC;
				if (svm_nodes_in_same_hive(node, bo_node))
					snoop = true;
			}
		} else {
			mapping_flags |= coherent ?
				AMDGPU_VM_MTYPE_UC : AMDGPU_VM_MTYPE_NC;
		}
		break;
	case IP_VERSION(9, 4, 2):
		if (domain == SVM_RANGE_VRAM_DOMAIN) {
			if (bo_node == node) {
				mapping_flags |= coherent ?
					AMDGPU_VM_MTYPE_CC : AMDGPU_VM_MTYPE_RW;
				if (node->adev->gmc.xgmi.connected_to_cpu)
					snoop = true;
			} else {
				mapping_flags |= coherent ?
					AMDGPU_VM_MTYPE_UC : AMDGPU_VM_MTYPE_NC;
				if (svm_nodes_in_same_hive(node, bo_node))
					snoop = true;
			}
		} else {
			mapping_flags |= coherent ?
				AMDGPU_VM_MTYPE_UC : AMDGPU_VM_MTYPE_NC;
		}
		break;
	case IP_VERSION(9, 4, 3):
	case IP_VERSION(9, 4, 4):
	case IP_VERSION(9, 5, 0):
		if (ext_coherent)
			mtype_local = AMDGPU_VM_MTYPE_CC;
		else
			mtype_local = amdgpu_mtype_local == 1 ? AMDGPU_VM_MTYPE_NC :
				amdgpu_mtype_local == 2 ? AMDGPU_VM_MTYPE_CC : AMDGPU_VM_MTYPE_RW;
		snoop = true;
		if (domain == SVM_RANGE_VRAM_DOMAIN) {
			/* local HBM region close to partition */
			if (bo_node->adev == node->adev &&
			    (!bo_node->xcp || !node->xcp || bo_node->xcp->mem_id == node->xcp->mem_id))
				mapping_flags |= mtype_local;
			/* local HBM region far from partition or remote XGMI GPU
			 * with regular system scope coherence
			 */
			else if (svm_nodes_in_same_hive(bo_node, node) && !ext_coherent)
				mapping_flags |= AMDGPU_VM_MTYPE_NC;
			/* PCIe P2P on GPUs pre-9.5.0 */
			else if (gc_ip_version < IP_VERSION(9, 5, 0) &&
				 !svm_nodes_in_same_hive(bo_node, node))
				mapping_flags |= AMDGPU_VM_MTYPE_UC;
			/* Other remote memory */
			else
				mapping_flags |= ext_coherent ? AMDGPU_VM_MTYPE_UC : AMDGPU_VM_MTYPE_NC;
		/* system memory accessed by the APU */
		} else if (node->adev->flags & AMD_IS_APU) {
			/* On NUMA systems, locality is determined per-page
			 * in amdgpu_gmc_override_vm_pte_flags
			 */
			if (num_possible_nodes() <= 1)
				mapping_flags |= mtype_local;
			else
				mapping_flags |= ext_coherent ? AMDGPU_VM_MTYPE_UC : AMDGPU_VM_MTYPE_NC;
		/* system memory accessed by the dGPU */
		} else {
			if (gc_ip_version < IP_VERSION(9, 5, 0) || ext_coherent)
				mapping_flags |= AMDGPU_VM_MTYPE_UC;
			else
				mapping_flags |= AMDGPU_VM_MTYPE_NC;
		}
		break;
	case IP_VERSION(12, 0, 0):
	case IP_VERSION(12, 0, 1):
		mapping_flags |= AMDGPU_VM_MTYPE_NC;
		break;
	default:
		mapping_flags |= coherent ?
			AMDGPU_VM_MTYPE_UC : AMDGPU_VM_MTYPE_NC;
	}

	if (flags & KFD_IOCTL_SVM_FLAG_GPU_EXEC)
		mapping_flags |= AMDGPU_VM_PAGE_EXECUTABLE;

	pte_flags = AMDGPU_PTE_VALID;
	pte_flags |= (domain == SVM_RANGE_VRAM_DOMAIN) ? 0 : AMDGPU_PTE_SYSTEM;
	pte_flags |= snoop ? AMDGPU_PTE_SNOOPED : 0;
	if (gc_ip_version >= IP_VERSION(12, 0, 0))
		pte_flags |= AMDGPU_PTE_IS_PTE;

	amdgpu_gmc_get_vm_pte(node->adev, vm, NULL, mapping_flags, &pte_flags);
	pte_flags |= AMDGPU_PTE_READABLE;
	if (!(flags & KFD_IOCTL_SVM_FLAG_GPU_RO))
		pte_flags |= AMDGPU_PTE_WRITEABLE;
	return pte_flags;
}

static int
svm_range_unmap_from_gpu(struct amdgpu_device *adev, struct amdgpu_vm *vm,
			 uint64_t start, uint64_t last,
			 struct dma_fence **fence)
{
	uint64_t init_pte_value = 0;

	pr_debug("[0x%llx 0x%llx]\n", start, last);

	return amdgpu_vm_update_range(adev, vm, false, true, true, false, NULL, start,
				      last, init_pte_value, 0, 0, NULL, NULL,
				      fence);
}

static int
svm_range_unmap_from_gpus(struct svm_range *prange, unsigned long start,
			  unsigned long last, uint32_t trigger)
{
	DECLARE_BITMAP(bitmap, MAX_GPU_INSTANCE);
	struct kfd_process_device *pdd;
	struct dma_fence *fence = NULL;
	struct kfd_process *p;
	uint32_t gpuidx;
	int r = 0;

	if (!prange->mapped_to_gpu) {
		pr_debug("prange 0x%p [0x%lx 0x%lx] not mapped to GPU\n",
			 prange, prange->start, prange->last);
		return 0;
	}

	if (prange->start == start && prange->last == last) {
		pr_debug("unmap svms 0x%p prange 0x%p\n", prange->svms, prange);
		prange->mapped_to_gpu = false;
	}

	bitmap_or(bitmap, prange->bitmap_access, prange->bitmap_aip,
		  MAX_GPU_INSTANCE);
	p = container_of(prange->svms, struct kfd_process, svms);

	for_each_set_bit(gpuidx, bitmap, MAX_GPU_INSTANCE) {
		pr_debug("unmap from gpu idx 0x%x\n", gpuidx);
		pdd = kfd_process_device_from_gpuidx(p, gpuidx);
		if (!pdd) {
			pr_debug("failed to find device idx %d\n", gpuidx);
			return -EINVAL;
		}

		kfd_smi_event_unmap_from_gpu(pdd->dev, p->lead_thread->pid,
					     start, last, trigger);

		r = svm_range_unmap_from_gpu(pdd->dev->adev,
					     drm_priv_to_vm(pdd->drm_priv),
					     start, last, &fence);
		if (r)
			break;

		if (fence) {
			r = dma_fence_wait(fence, false);
			dma_fence_put(fence);
			fence = NULL;
			if (r)
				break;
		}
		kfd_flush_tlb(pdd, TLB_FLUSH_HEAVYWEIGHT);
	}

	return r;
}

static int
svm_range_map_to_gpu(struct kfd_process_device *pdd, struct svm_range *prange,
		     unsigned long offset, unsigned long npages, bool readonly,
		     dma_addr_t *dma_addr, struct amdgpu_device *bo_adev,
		     struct dma_fence **fence, bool flush_tlb)
{
	struct amdgpu_device *adev = pdd->dev->adev;
	struct amdgpu_vm *vm = drm_priv_to_vm(pdd->drm_priv);
	uint64_t pte_flags;
	unsigned long last_start;
	int last_domain;
	int r = 0;
	int64_t i, j;

	last_start = prange->start + offset;

	pr_debug("svms 0x%p [0x%lx 0x%lx] readonly %d\n", prange->svms,
		 last_start, last_start + npages - 1, readonly);

	for (i = offset; i < offset + npages; i++) {
		last_domain = dma_addr[i] & SVM_RANGE_VRAM_DOMAIN;
		dma_addr[i] &= ~SVM_RANGE_VRAM_DOMAIN;

		/* Collect all pages in the same address range and memory domain
		 * that can be mapped with a single call to update mapping.
		 */
		if (i < offset + npages - 1 &&
		    last_domain == (dma_addr[i + 1] & SVM_RANGE_VRAM_DOMAIN))
			continue;

		pr_debug("Mapping range [0x%lx 0x%llx] on domain: %s\n",
			 last_start, prange->start + i, last_domain ? "GPU" : "CPU");

		pte_flags = svm_range_get_pte_flags(pdd->dev, vm, prange, last_domain);
		if (readonly)
			pte_flags &= ~AMDGPU_PTE_WRITEABLE;

		pr_debug("svms 0x%p map [0x%lx 0x%llx] vram %d PTE 0x%llx\n",
			 prange->svms, last_start, prange->start + i,
			 (last_domain == SVM_RANGE_VRAM_DOMAIN) ? 1 : 0,
			 pte_flags);

		/* For dGPU mode, we use same vm_manager to allocate VRAM for
		 * different memory partition based on fpfn/lpfn, we should use
		 * same vm_manager.vram_base_offset regardless memory partition.
		 */
		r = amdgpu_vm_update_range(adev, vm, false, false, flush_tlb, true,
					   NULL, last_start, prange->start + i,
					   pte_flags,
					   (last_start - prange->start) << PAGE_SHIFT,
					   bo_adev ? bo_adev->vm_manager.vram_base_offset : 0,
					   NULL, dma_addr, &vm->last_update);

		for (j = last_start - prange->start; j <= i; j++)
			dma_addr[j] |= last_domain;

		if (r) {
			pr_debug("failed %d to map to gpu 0x%lx\n", r, prange->start);
			goto out;
		}
		last_start = prange->start + i + 1;
	}

	r = amdgpu_vm_update_pdes(adev, vm, false);
	if (r) {
		pr_debug("failed %d to update directories 0x%lx\n", r,
			 prange->start);
		goto out;
	}

	if (fence)
		*fence = dma_fence_get(vm->last_update);

out:
	return r;
}

static int
svm_range_map_to_gpus(struct svm_range *prange, unsigned long offset,
		      unsigned long npages, bool readonly,
		      unsigned long *bitmap, bool wait, bool flush_tlb)
{
	struct kfd_process_device *pdd;
	struct amdgpu_device *bo_adev = NULL;
	struct kfd_process *p;
	struct dma_fence *fence = NULL;
	uint32_t gpuidx;
	int r = 0;

	if (prange->svm_bo && prange->ttm_res)
		bo_adev = prange->svm_bo->node->adev;

	p = container_of(prange->svms, struct kfd_process, svms);
	for_each_set_bit(gpuidx, bitmap, MAX_GPU_INSTANCE) {
		pr_debug("mapping to gpu idx 0x%x\n", gpuidx);
		pdd = kfd_process_device_from_gpuidx(p, gpuidx);
		if (!pdd) {
			pr_debug("failed to find device idx %d\n", gpuidx);
			return -EINVAL;
		}

		pdd = kfd_bind_process_to_device(pdd->dev, p);
		if (IS_ERR(pdd))
			return -EINVAL;

		if (bo_adev && pdd->dev->adev != bo_adev &&
		    !amdgpu_xgmi_same_hive(pdd->dev->adev, bo_adev)) {
			pr_debug("cannot map to device idx %d\n", gpuidx);
			continue;
		}

		r = svm_range_map_to_gpu(pdd, prange, offset, npages, readonly,
					 prange->dma_addr[gpuidx],
					 bo_adev, wait ? &fence : NULL,
					 flush_tlb);
		if (r)
			break;

		if (fence) {
			r = dma_fence_wait(fence, false);
			dma_fence_put(fence);
			fence = NULL;
			if (r) {
				pr_debug("failed %d to dma fence wait\n", r);
				break;
			}
		}

		kfd_flush_tlb(pdd, TLB_FLUSH_LEGACY);
	}

	return r;
}

struct svm_validate_context {
	struct kfd_process *process;
	struct svm_range *prange;
	bool intr;
	DECLARE_BITMAP(bitmap, MAX_GPU_INSTANCE);
	struct drm_exec exec;
};

static int svm_range_reserve_bos(struct svm_validate_context *ctx, bool intr)
{
	struct kfd_process_device *pdd;
	struct amdgpu_vm *vm;
	uint32_t gpuidx;
	int r;

	drm_exec_init(&ctx->exec, intr ? DRM_EXEC_INTERRUPTIBLE_WAIT: 0, 0);
	drm_exec_until_all_locked(&ctx->exec) {
		for_each_set_bit(gpuidx, ctx->bitmap, MAX_GPU_INSTANCE) {
			pdd = kfd_process_device_from_gpuidx(ctx->process, gpuidx);
			if (!pdd) {
				pr_debug("failed to find device idx %d\n", gpuidx);
				r = -EINVAL;
				goto unreserve_out;
			}
			vm = drm_priv_to_vm(pdd->drm_priv);

			r = amdgpu_vm_lock_pd(vm, &ctx->exec, 2);
			drm_exec_retry_on_contention(&ctx->exec);
			if (unlikely(r)) {
				pr_debug("failed %d to reserve bo\n", r);
				goto unreserve_out;
			}
		}
	}

	for_each_set_bit(gpuidx, ctx->bitmap, MAX_GPU_INSTANCE) {
		pdd = kfd_process_device_from_gpuidx(ctx->process, gpuidx);
		if (!pdd) {
			pr_debug("failed to find device idx %d\n", gpuidx);
			r = -EINVAL;
			goto unreserve_out;
		}

		r = amdgpu_vm_validate(pdd->dev->adev,
				       drm_priv_to_vm(pdd->drm_priv), NULL,
				       svm_range_bo_validate, NULL);
		if (r) {
			pr_debug("failed %d validate pt bos\n", r);
			goto unreserve_out;
		}
	}

	return 0;

unreserve_out:
	drm_exec_fini(&ctx->exec);
	return r;
}

static void svm_range_unreserve_bos(struct svm_validate_context *ctx)
{
	drm_exec_fini(&ctx->exec);
}

static void *kfd_svm_page_owner(struct kfd_process *p, int32_t gpuidx)
{
	struct kfd_process_device *pdd;

	pdd = kfd_process_device_from_gpuidx(p, gpuidx);
	if (!pdd)
		return NULL;

	return SVM_ADEV_PGMAP_OWNER(pdd->dev->adev);
}

/*
 * Validation+GPU mapping with concurrent invalidation (MMU notifiers)
 *
 * To prevent concurrent destruction or change of range attributes, the
 * svm_read_lock must be held. The caller must not hold the svm_write_lock
 * because that would block concurrent evictions and lead to deadlocks. To
 * serialize concurrent migrations or validations of the same range, the
 * prange->migrate_mutex must be held.
 *
 * For VRAM ranges, the SVM BO must be allocated and valid (protected by its
 * eviction fence.
 *
 * The following sequence ensures race-free validation and GPU mapping:
 *
 * 1. Reserve page table (and SVM BO if range is in VRAM)
 * 2. hmm_range_fault to get page addresses (if system memory)
 * 3. DMA-map pages (if system memory)
 * 4-a. Take notifier lock
 * 4-b. Check that pages still valid (mmu_interval_read_retry)
 * 4-c. Check that the range was not split or otherwise invalidated
 * 4-d. Update GPU page table
 * 4.e. Release notifier lock
 * 5. Release page table (and SVM BO) reservation
 */
static int svm_range_validate_and_map(struct mm_struct *mm,
				      unsigned long map_start, unsigned long map_last,
				      struct svm_range *prange, int32_t gpuidx,
				      bool intr, bool wait, bool flush_tlb)
{
	struct svm_validate_context *ctx;
	unsigned long start, end, addr;
	struct kfd_process *p;
	void *owner;
	int32_t idx;
	int r = 0;

	ctx = kzalloc(sizeof(struct svm_validate_context), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx->process = container_of(prange->svms, struct kfd_process, svms);
	ctx->prange = prange;
	ctx->intr = intr;

	if (gpuidx < MAX_GPU_INSTANCE) {
		bitmap_zero(ctx->bitmap, MAX_GPU_INSTANCE);
		bitmap_set(ctx->bitmap, gpuidx, 1);
	} else if (ctx->process->xnack_enabled) {
		bitmap_copy(ctx->bitmap, prange->bitmap_aip, MAX_GPU_INSTANCE);

		/* If prefetch range to GPU, or GPU retry fault migrate range to
		 * GPU, which has ACCESS attribute to the range, create mapping
		 * on that GPU.
		 */
		if (prange->actual_loc) {
			gpuidx = kfd_process_gpuidx_from_gpuid(ctx->process,
							prange->actual_loc);
			if (gpuidx < 0) {
				WARN_ONCE(1, "failed get device by id 0x%x\n",
					 prange->actual_loc);
				r = -EINVAL;
				goto free_ctx;
			}
			if (test_bit(gpuidx, prange->bitmap_access))
				bitmap_set(ctx->bitmap, gpuidx, 1);
		}

		/*
		 * If prange is already mapped or with always mapped flag,
		 * update mapping on GPUs with ACCESS attribute
		 */
		if (bitmap_empty(ctx->bitmap, MAX_GPU_INSTANCE)) {
			if (prange->mapped_to_gpu ||
			    prange->flags & KFD_IOCTL_SVM_FLAG_GPU_ALWAYS_MAPPED)
				bitmap_copy(ctx->bitmap, prange->bitmap_access, MAX_GPU_INSTANCE);
		}
	} else {
		bitmap_or(ctx->bitmap, prange->bitmap_access,
			  prange->bitmap_aip, MAX_GPU_INSTANCE);
	}

	if (bitmap_empty(ctx->bitmap, MAX_GPU_INSTANCE)) {
		r = 0;
		goto free_ctx;
	}

	if (prange->actual_loc && !prange->ttm_res) {
		/* This should never happen. actual_loc gets set by
		 * svm_migrate_ram_to_vram after allocating a BO.
		 */
		WARN_ONCE(1, "VRAM BO missing during validation\n");
		r = -EINVAL;
		goto free_ctx;
	}

	r = svm_range_reserve_bos(ctx, intr);
	if (r)
		goto free_ctx;

	p = container_of(prange->svms, struct kfd_process, svms);
	owner = kfd_svm_page_owner(p, find_first_bit(ctx->bitmap,
						MAX_GPU_INSTANCE));
	for_each_set_bit(idx, ctx->bitmap, MAX_GPU_INSTANCE) {
		if (kfd_svm_page_owner(p, idx) != owner) {
			owner = NULL;
			break;
		}
	}

	start = map_start << PAGE_SHIFT;
	end = (map_last + 1) << PAGE_SHIFT;
	for (addr = start; !r && addr < end; ) {
		struct hmm_range *hmm_range = NULL;
		unsigned long map_start_vma;
		unsigned long map_last_vma;
		struct vm_area_struct *vma;
		unsigned long next = 0;
		unsigned long offset;
		unsigned long npages;
		bool readonly;

		vma = vma_lookup(mm, addr);
		if (vma) {
			readonly = !(vma->vm_flags & VM_WRITE);

			next = min(vma->vm_end, end);
			npages = (next - addr) >> PAGE_SHIFT;
			/* HMM requires at least READ permissions. If provided with PROT_NONE,
			 * unmap the memory. If it's not already mapped, this is a no-op
			 * If PROT_WRITE is provided without READ, warn first then unmap
			 */
			if (!(vma->vm_flags & VM_READ)) {
				unsigned long e, s;

				svm_range_lock(prange);
				if (vma->vm_flags & VM_WRITE)
					pr_debug("VM_WRITE without VM_READ is not supported");
				s = max(start, prange->start);
				e = min(end, prange->last);
				if (e >= s)
					r = svm_range_unmap_from_gpus(prange, s, e,
						       KFD_SVM_UNMAP_TRIGGER_UNMAP_FROM_CPU);
				svm_range_unlock(prange);
				/* If unmap returns non-zero, we'll bail on the next for loop
				 * iteration, so just leave r and continue
				 */
				addr = next;
				continue;
			}

			WRITE_ONCE(p->svms.faulting_task, current);
			r = amdgpu_hmm_range_get_pages(&prange->notifier, addr, npages,
						       readonly, owner,
						       &hmm_range);
			WRITE_ONCE(p->svms.faulting_task, NULL);
			if (r)
				pr_debug("failed %d to get svm range pages\n", r);
		} else {
			r = -EFAULT;
		}

		if (!r) {
			offset = (addr >> PAGE_SHIFT) - prange->start;
			r = svm_range_dma_map(prange, ctx->bitmap, offset, npages,
					      hmm_range->hmm_pfns);
			if (r)
				pr_debug("failed %d to dma map range\n", r);
		}

		svm_range_lock(prange);

		/* Free backing memory of hmm_range if it was initialized
		 * Overrride return value to TRY AGAIN only if prior returns
		 * were successful
		 */
		if (hmm_range && amdgpu_hmm_range_get_pages_done(hmm_range) && !r) {
			pr_debug("hmm update the range, need validate again\n");
			r = -EAGAIN;
		}

		if (!r && !list_empty(&prange->child_list)) {
			pr_debug("range split by unmap in parallel, validate again\n");
			r = -EAGAIN;
		}

		if (!r) {
			map_start_vma = max(map_start, prange->start + offset);
			map_last_vma = min(map_last, prange->start + offset + npages - 1);
			if (map_start_vma <= map_last_vma) {
				offset = map_start_vma - prange->start;
				npages = map_last_vma - map_start_vma + 1;
				r = svm_range_map_to_gpus(prange, offset, npages, readonly,
							  ctx->bitmap, wait, flush_tlb);
			}
		}

		if (!r && next == end)
			prange->mapped_to_gpu = true;

		svm_range_unlock(prange);

		addr = next;
	}

	svm_range_unreserve_bos(ctx);
	if (!r)
		prange->validate_timestamp = ktime_get_boottime();

free_ctx:
	kfree(ctx);

	return r;
}

/**
 * svm_range_list_lock_and_flush_work - flush pending deferred work
 *
 * @svms: the svm range list
 * @mm: the mm structure
 *
 * Context: Returns with mmap write lock held, pending deferred work flushed
 *
 */
void
svm_range_list_lock_and_flush_work(struct svm_range_list *svms,
				   struct mm_struct *mm)
{
retry_flush_work:
	flush_work(&svms->deferred_list_work);
	mmap_write_lock(mm);

	if (list_empty(&svms->deferred_range_list))
		return;
	mmap_write_unlock(mm);
	pr_debug("retry flush\n");
	goto retry_flush_work;
}

static void svm_range_restore_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct amdkfd_process_info *process_info;
	struct svm_range_list *svms;
	struct svm_range *prange;
	struct kfd_process *p;
	struct mm_struct *mm;
	int evicted_ranges;
	int invalid;
	int r;

	svms = container_of(dwork, struct svm_range_list, restore_work);
	evicted_ranges = atomic_read(&svms->evicted_ranges);
	if (!evicted_ranges)
		return;

	pr_debug("restore svm ranges\n");

	p = container_of(svms, struct kfd_process, svms);
	process_info = p->kgd_process_info;

	/* Keep mm reference when svm_range_validate_and_map ranges */
	mm = get_task_mm(p->lead_thread);
	if (!mm) {
		pr_debug("svms 0x%p process mm gone\n", svms);
		return;
	}

	mutex_lock(&process_info->lock);
	svm_range_list_lock_and_flush_work(svms, mm);
	mutex_lock(&svms->lock);

	evicted_ranges = atomic_read(&svms->evicted_ranges);

	list_for_each_entry(prange, &svms->list, list) {
		invalid = atomic_read(&prange->invalid);
		if (!invalid)
			continue;

		pr_debug("restoring svms 0x%p prange 0x%p [0x%lx %lx] inv %d\n",
			 prange->svms, prange, prange->start, prange->last,
			 invalid);

		/*
		 * If range is migrating, wait for migration is done.
		 */
		mutex_lock(&prange->migrate_mutex);

		r = svm_range_validate_and_map(mm, prange->start, prange->last, prange,
					       MAX_GPU_INSTANCE, false, true, false);
		if (r)
			pr_debug("failed %d to map 0x%lx to gpus\n", r,
				 prange->start);

		mutex_unlock(&prange->migrate_mutex);
		if (r)
			goto out_reschedule;

		if (atomic_cmpxchg(&prange->invalid, invalid, 0) != invalid)
			goto out_reschedule;
	}

	if (atomic_cmpxchg(&svms->evicted_ranges, evicted_ranges, 0) !=
	    evicted_ranges)
		goto out_reschedule;

	evicted_ranges = 0;

	r = kgd2kfd_resume_mm(mm);
	if (r) {
		/* No recovery from this failure. Probably the CP is
		 * hanging. No point trying again.
		 */
		pr_debug("failed %d to resume KFD\n", r);
	}

	pr_debug("restore svm ranges successfully\n");

out_reschedule:
	mutex_unlock(&svms->lock);
	mmap_write_unlock(mm);
	mutex_unlock(&process_info->lock);

	/* If validation failed, reschedule another attempt */
	if (evicted_ranges) {
		pr_debug("reschedule to restore svm range\n");
		queue_delayed_work(system_freezable_wq, &svms->restore_work,
			msecs_to_jiffies(AMDGPU_SVM_RANGE_RESTORE_DELAY_MS));

		kfd_smi_event_queue_restore_rescheduled(mm);
	}
	mmput(mm);
}

/**
 * svm_range_evict - evict svm range
 * @prange: svm range structure
 * @mm: current process mm_struct
 * @start: starting process queue number
 * @last: last process queue number
 * @event: mmu notifier event when range is evicted or migrated
 *
 * Stop all queues of the process to ensure GPU doesn't access the memory, then
 * return to let CPU evict the buffer and proceed CPU pagetable update.
 *
 * Don't need use lock to sync cpu pagetable invalidation with GPU execution.
 * If invalidation happens while restore work is running, restore work will
 * restart to ensure to get the latest CPU pages mapping to GPU, then start
 * the queues.
 */
static int
svm_range_evict(struct svm_range *prange, struct mm_struct *mm,
		unsigned long start, unsigned long last,
		enum mmu_notifier_event event)
{
	struct svm_range_list *svms = prange->svms;
	struct svm_range *pchild;
	struct kfd_process *p;
	int r = 0;

	p = container_of(svms, struct kfd_process, svms);

	pr_debug("invalidate svms 0x%p prange [0x%lx 0x%lx] [0x%lx 0x%lx]\n",
		 svms, prange->start, prange->last, start, last);

	if (!p->xnack_enabled ||
	    (prange->flags & KFD_IOCTL_SVM_FLAG_GPU_ALWAYS_MAPPED)) {
		int evicted_ranges;
		bool mapped = prange->mapped_to_gpu;

		list_for_each_entry(pchild, &prange->child_list, child_list) {
			if (!pchild->mapped_to_gpu)
				continue;
			mapped = true;
			mutex_lock_nested(&pchild->lock, 1);
			if (pchild->start <= last && pchild->last >= start) {
				pr_debug("increment pchild invalid [0x%lx 0x%lx]\n",
					 pchild->start, pchild->last);
				atomic_inc(&pchild->invalid);
			}
			mutex_unlock(&pchild->lock);
		}

		if (!mapped)
			return r;

		if (prange->start <= last && prange->last >= start)
			atomic_inc(&prange->invalid);

		evicted_ranges = atomic_inc_return(&svms->evicted_ranges);
		if (evicted_ranges != 1)
			return r;

		pr_debug("evicting svms 0x%p range [0x%lx 0x%lx]\n",
			 prange->svms, prange->start, prange->last);

		/* First eviction, stop the queues */
		r = kgd2kfd_quiesce_mm(mm, KFD_QUEUE_EVICTION_TRIGGER_SVM);
		if (r)
			pr_debug("failed to quiesce KFD\n");

		pr_debug("schedule to restore svm %p ranges\n", svms);
		queue_delayed_work(system_freezable_wq, &svms->restore_work,
			msecs_to_jiffies(AMDGPU_SVM_RANGE_RESTORE_DELAY_MS));
	} else {
		unsigned long s, l;
		uint32_t trigger;

		if (event == MMU_NOTIFY_MIGRATE)
			trigger = KFD_SVM_UNMAP_TRIGGER_MMU_NOTIFY_MIGRATE;
		else
			trigger = KFD_SVM_UNMAP_TRIGGER_MMU_NOTIFY;

		pr_debug("invalidate unmap svms 0x%p [0x%lx 0x%lx] from GPUs\n",
			 prange->svms, start, last);
		list_for_each_entry(pchild, &prange->child_list, child_list) {
			mutex_lock_nested(&pchild->lock, 1);
			s = max(start, pchild->start);
			l = min(last, pchild->last);
			if (l >= s)
				svm_range_unmap_from_gpus(pchild, s, l, trigger);
			mutex_unlock(&pchild->lock);
		}
		s = max(start, prange->start);
		l = min(last, prange->last);
		if (l >= s)
			svm_range_unmap_from_gpus(prange, s, l, trigger);
	}

	return r;
}

static struct svm_range *svm_range_clone(struct svm_range *old)
{
	struct svm_range *new;

	new = svm_range_new(old->svms, old->start, old->last, false);
	if (!new)
		return NULL;
	if (svm_range_copy_dma_addrs(new, old)) {
		svm_range_free(new, false);
		return NULL;
	}
	if (old->svm_bo) {
		new->ttm_res = old->ttm_res;
		new->offset = old->offset;
		new->svm_bo = svm_range_bo_ref(old->svm_bo);
		spin_lock(&new->svm_bo->list_lock);
		list_add(&new->svm_bo_list, &new->svm_bo->range_list);
		spin_unlock(&new->svm_bo->list_lock);
	}
	new->flags = old->flags;
	new->preferred_loc = old->preferred_loc;
	new->prefetch_loc = old->prefetch_loc;
	new->actual_loc = old->actual_loc;
	new->granularity = old->granularity;
	new->mapped_to_gpu = old->mapped_to_gpu;
	new->vram_pages = old->vram_pages;
	bitmap_copy(new->bitmap_access, old->bitmap_access, MAX_GPU_INSTANCE);
	bitmap_copy(new->bitmap_aip, old->bitmap_aip, MAX_GPU_INSTANCE);
	atomic_set(&new->queue_refcount, atomic_read(&old->queue_refcount));

	return new;
}

void svm_range_set_max_pages(struct amdgpu_device *adev)
{
	uint64_t max_pages;
	uint64_t pages, _pages;
	uint64_t min_pages = 0;
	int i, id;

	for (i = 0; i < adev->kfd.dev->num_nodes; i++) {
		if (adev->kfd.dev->nodes[i]->xcp)
			id = adev->kfd.dev->nodes[i]->xcp->id;
		else
			id = -1;
		pages = KFD_XCP_MEMORY_SIZE(adev, id) >> 17;
		pages = clamp(pages, 1ULL << 9, 1ULL << 18);
		pages = rounddown_pow_of_two(pages);
		min_pages = min_not_zero(min_pages, pages);
	}

	do {
		max_pages = READ_ONCE(max_svm_range_pages);
		_pages = min_not_zero(max_pages, min_pages);
	} while (cmpxchg(&max_svm_range_pages, max_pages, _pages) != max_pages);
}

static int
svm_range_split_new(struct svm_range_list *svms, uint64_t start, uint64_t last,
		    uint64_t max_pages, struct list_head *insert_list,
		    struct list_head *update_list)
{
	struct svm_range *prange;
	uint64_t l;

	pr_debug("max_svm_range_pages 0x%llx adding [0x%llx 0x%llx]\n",
		 max_pages, start, last);

	while (last >= start) {
		l = min(last, ALIGN_DOWN(start + max_pages, max_pages) - 1);

		prange = svm_range_new(svms, start, l, true);
		if (!prange)
			return -ENOMEM;
		list_add(&prange->list, insert_list);
		list_add(&prange->update_list, update_list);

		start = l + 1;
	}
	return 0;
}

/**
 * svm_range_add - add svm range and handle overlap
 * @p: the range add to this process svms
 * @start: page size aligned
 * @size: page size aligned
 * @nattr: number of attributes
 * @attrs: array of attributes
 * @update_list: output, the ranges need validate and update GPU mapping
 * @insert_list: output, the ranges need insert to svms
 * @remove_list: output, the ranges are replaced and need remove from svms
 * @remap_list: output, remap unaligned svm ranges
 *
 * Check if the virtual address range has overlap with any existing ranges,
 * split partly overlapping ranges and add new ranges in the gaps. All changes
 * should be applied to the range_list and interval tree transactionally. If
 * any range split or allocation fails, the entire update fails. Therefore any
 * existing overlapping svm_ranges are cloned and the original svm_ranges left
 * unchanged.
 *
 * If the transaction succeeds, the caller can update and insert clones and
 * new ranges, then free the originals.
 *
 * Otherwise the caller can free the clones and new ranges, while the old
 * svm_ranges remain unchanged.
 *
 * Context: Process context, caller must hold svms->lock
 *
 * Return:
 * 0 - OK, otherwise error code
 */
static int
svm_range_add(struct kfd_process *p, uint64_t start, uint64_t size,
	      uint32_t nattr, struct kfd_ioctl_svm_attribute *attrs,
	      struct list_head *update_list, struct list_head *insert_list,
	      struct list_head *remove_list, struct list_head *remap_list)
{
	unsigned long last = start + size - 1UL;
	struct svm_range_list *svms = &p->svms;
	struct interval_tree_node *node;
	struct svm_range *prange;
	struct svm_range *tmp;
	struct list_head new_list;
	int r = 0;

	pr_debug("svms 0x%p [0x%llx 0x%lx]\n", &p->svms, start, last);

	INIT_LIST_HEAD(update_list);
	INIT_LIST_HEAD(insert_list);
	INIT_LIST_HEAD(remove_list);
	INIT_LIST_HEAD(&new_list);
	INIT_LIST_HEAD(remap_list);

	node = interval_tree_iter_first(&svms->objects, start, last);
	while (node) {
		struct interval_tree_node *next;
		unsigned long next_start;

		pr_debug("found overlap node [0x%lx 0x%lx]\n", node->start,
			 node->last);

		prange = container_of(node, struct svm_range, it_node);
		next = interval_tree_iter_next(node, start, last);
		next_start = min(node->last, last) + 1;

		if (svm_range_is_same_attrs(p, prange, nattr, attrs) &&
		    prange->mapped_to_gpu) {
			/* nothing to do */
		} else if (node->start < start || node->last > last) {
			/* node intersects the update range and its attributes
			 * will change. Clone and split it, apply updates only
			 * to the overlapping part
			 */
			struct svm_range *old = prange;

			prange = svm_range_clone(old);
			if (!prange) {
				r = -ENOMEM;
				goto out;
			}

			list_add(&old->update_list, remove_list);
			list_add(&prange->list, insert_list);
			list_add(&prange->update_list, update_list);

			if (node->start < start) {
				pr_debug("change old range start\n");
				r = svm_range_split_head(prange, start,
							 insert_list, remap_list);
				if (r)
					goto out;
			}
			if (node->last > last) {
				pr_debug("change old range last\n");
				r = svm_range_split_tail(prange, last,
							 insert_list, remap_list);
				if (r)
					goto out;
			}
		} else {
			/* The node is contained within start..last,
			 * just update it
			 */
			list_add(&prange->update_list, update_list);
		}

		/* insert a new node if needed */
		if (node->start > start) {
			r = svm_range_split_new(svms, start, node->start - 1,
						READ_ONCE(max_svm_range_pages),
						&new_list, update_list);
			if (r)
				goto out;
		}

		node = next;
		start = next_start;
	}

	/* add a final range at the end if needed */
	if (start <= last)
		r = svm_range_split_new(svms, start, last,
					READ_ONCE(max_svm_range_pages),
					&new_list, update_list);

out:
	if (r) {
		list_for_each_entry_safe(prange, tmp, insert_list, list)
			svm_range_free(prange, false);
		list_for_each_entry_safe(prange, tmp, &new_list, list)
			svm_range_free(prange, true);
	} else {
		list_splice(&new_list, insert_list);
	}

	return r;
}

static void
svm_range_update_notifier_and_interval_tree(struct mm_struct *mm,
					    struct svm_range *prange)
{
	unsigned long start;
	unsigned long last;

	start = prange->notifier.interval_tree.start >> PAGE_SHIFT;
	last = prange->notifier.interval_tree.last >> PAGE_SHIFT;

	if (prange->start == start && prange->last == last)
		return;

	pr_debug("up notifier 0x%p prange 0x%p [0x%lx 0x%lx] [0x%lx 0x%lx]\n",
		  prange->svms, prange, start, last, prange->start,
		  prange->last);

	if (start != 0 && last != 0) {
		interval_tree_remove(&prange->it_node, &prange->svms->objects);
		svm_range_remove_notifier(prange);
	}
	prange->it_node.start = prange->start;
	prange->it_node.last = prange->last;

	interval_tree_insert(&prange->it_node, &prange->svms->objects);
	svm_range_add_notifier_locked(mm, prange);
}

static void
svm_range_handle_list_op(struct svm_range_list *svms, struct svm_range *prange,
			 struct mm_struct *mm)
{
	switch (prange->work_item.op) {
	case SVM_OP_NULL:
		pr_debug("NULL OP 0x%p prange 0x%p [0x%lx 0x%lx]\n",
			 svms, prange, prange->start, prange->last);
		break;
	case SVM_OP_UNMAP_RANGE:
		pr_debug("remove 0x%p prange 0x%p [0x%lx 0x%lx]\n",
			 svms, prange, prange->start, prange->last);
		svm_range_unlink(prange);
		svm_range_remove_notifier(prange);
		svm_range_free(prange, true);
		break;
	case SVM_OP_UPDATE_RANGE_NOTIFIER:
		pr_debug("update notifier 0x%p prange 0x%p [0x%lx 0x%lx]\n",
			 svms, prange, prange->start, prange->last);
		svm_range_update_notifier_and_interval_tree(mm, prange);
		break;
	case SVM_OP_UPDATE_RANGE_NOTIFIER_AND_MAP:
		pr_debug("update and map 0x%p prange 0x%p [0x%lx 0x%lx]\n",
			 svms, prange, prange->start, prange->last);
		svm_range_update_notifier_and_interval_tree(mm, prange);
		/* TODO: implement deferred validation and mapping */
		break;
	case SVM_OP_ADD_RANGE:
		pr_debug("add 0x%p prange 0x%p [0x%lx 0x%lx]\n", svms, prange,
			 prange->start, prange->last);
		svm_range_add_to_svms(prange);
		svm_range_add_notifier_locked(mm, prange);
		break;
	case SVM_OP_ADD_RANGE_AND_MAP:
		pr_debug("add and map 0x%p prange 0x%p [0x%lx 0x%lx]\n", svms,
			 prange, prange->start, prange->last);
		svm_range_add_to_svms(prange);
		svm_range_add_notifier_locked(mm, prange);
		/* TODO: implement deferred validation and mapping */
		break;
	default:
		WARN_ONCE(1, "Unknown prange 0x%p work op %d\n", prange,
			 prange->work_item.op);
	}
}

static void svm_range_drain_retry_fault(struct svm_range_list *svms)
{
	struct kfd_process_device *pdd;
	struct kfd_process *p;
	uint32_t i;

	p = container_of(svms, struct kfd_process, svms);

	for_each_set_bit(i, svms->bitmap_supported, p->n_pdds) {
		pdd = p->pdds[i];
		if (!pdd)
			continue;

		pr_debug("drain retry fault gpu %d svms %p\n", i, svms);

		amdgpu_ih_wait_on_checkpoint_process_ts(pdd->dev->adev,
				pdd->dev->adev->irq.retry_cam_enabled ?
				&pdd->dev->adev->irq.ih :
				&pdd->dev->adev->irq.ih1);

		if (pdd->dev->adev->irq.retry_cam_enabled)
			amdgpu_ih_wait_on_checkpoint_process_ts(pdd->dev->adev,
				&pdd->dev->adev->irq.ih_soft);


		pr_debug("drain retry fault gpu %d svms 0x%p done\n", i, svms);
	}
}

static void svm_range_deferred_list_work(struct work_struct *work)
{
	struct svm_range_list *svms;
	struct svm_range *prange;
	struct mm_struct *mm;

	svms = container_of(work, struct svm_range_list, deferred_list_work);
	pr_debug("enter svms 0x%p\n", svms);

	spin_lock(&svms->deferred_list_lock);
	while (!list_empty(&svms->deferred_range_list)) {
		prange = list_first_entry(&svms->deferred_range_list,
					  struct svm_range, deferred_list);
		spin_unlock(&svms->deferred_list_lock);

		pr_debug("prange 0x%p [0x%lx 0x%lx] op %d\n", prange,
			 prange->start, prange->last, prange->work_item.op);

		mm = prange->work_item.mm;

		mmap_write_lock(mm);

		/* Remove from deferred_list must be inside mmap write lock, for
		 * two race cases:
		 * 1. unmap_from_cpu may change work_item.op and add the range
		 *    to deferred_list again, cause use after free bug.
		 * 2. svm_range_list_lock_and_flush_work may hold mmap write
		 *    lock and continue because deferred_list is empty, but
		 *    deferred_list work is actually waiting for mmap lock.
		 */
		spin_lock(&svms->deferred_list_lock);
		list_del_init(&prange->deferred_list);
		spin_unlock(&svms->deferred_list_lock);

		mutex_lock(&svms->lock);
		mutex_lock(&prange->migrate_mutex);
		while (!list_empty(&prange->child_list)) {
			struct svm_range *pchild;

			pchild = list_first_entry(&prange->child_list,
						struct svm_range, child_list);
			pr_debug("child prange 0x%p op %d\n", pchild,
				 pchild->work_item.op);
			list_del_init(&pchild->child_list);
			svm_range_handle_list_op(svms, pchild, mm);
		}
		mutex_unlock(&prange->migrate_mutex);

		svm_range_handle_list_op(svms, prange, mm);
		mutex_unlock(&svms->lock);
		mmap_write_unlock(mm);

		/* Pairs with mmget in svm_range_add_list_work. If dropping the
		 * last mm refcount, schedule release work to avoid circular locking
		 */
		mmput_async(mm);

		spin_lock(&svms->deferred_list_lock);
	}
	spin_unlock(&svms->deferred_list_lock);
	pr_debug("exit svms 0x%p\n", svms);
}

void
svm_range_add_list_work(struct svm_range_list *svms, struct svm_range *prange,
			struct mm_struct *mm, enum svm_work_list_ops op)
{
	spin_lock(&svms->deferred_list_lock);
	/* if prange is on the deferred list */
	if (!list_empty(&prange->deferred_list)) {
		pr_debug("update exist prange 0x%p work op %d\n", prange, op);
		WARN_ONCE(prange->work_item.mm != mm, "unmatch mm\n");
		if (op != SVM_OP_NULL &&
		    prange->work_item.op != SVM_OP_UNMAP_RANGE)
			prange->work_item.op = op;
	} else {
		/* Pairs with mmput in deferred_list_work.
		 * If process is exiting and mm is gone, don't update mmu notifier.
		 */
		if (mmget_not_zero(mm)) {
			prange->work_item.mm = mm;
			prange->work_item.op = op;
			list_add_tail(&prange->deferred_list,
				      &prange->svms->deferred_range_list);
			pr_debug("add prange 0x%p [0x%lx 0x%lx] to work list op %d\n",
				 prange, prange->start, prange->last, op);
		}
	}
	spin_unlock(&svms->deferred_list_lock);
}

void schedule_deferred_list_work(struct svm_range_list *svms)
{
	spin_lock(&svms->deferred_list_lock);
	if (!list_empty(&svms->deferred_range_list))
		schedule_work(&svms->deferred_list_work);
	spin_unlock(&svms->deferred_list_lock);
}

static void
svm_range_unmap_split(struct svm_range *parent, struct svm_range *prange, unsigned long start,
		      unsigned long last)
{
	struct svm_range *head;
	struct svm_range *tail;

	if (prange->work_item.op == SVM_OP_UNMAP_RANGE) {
		pr_debug("prange 0x%p [0x%lx 0x%lx] is already freed\n", prange,
			 prange->start, prange->last);
		return;
	}
	if (start > prange->last || last < prange->start)
		return;

	head = tail = prange;
	if (start > prange->start)
		svm_range_split(prange, prange->start, start - 1, &tail);
	if (last < tail->last)
		svm_range_split(tail, last + 1, tail->last, &head);

	if (head != prange && tail != prange) {
		svm_range_add_child(parent, head, SVM_OP_UNMAP_RANGE);
		svm_range_add_child(parent, tail, SVM_OP_ADD_RANGE);
	} else if (tail != prange) {
		svm_range_add_child(parent, tail, SVM_OP_UNMAP_RANGE);
	} else if (head != prange) {
		svm_range_add_child(parent, head, SVM_OP_UNMAP_RANGE);
	} else if (parent != prange) {
		prange->work_item.op = SVM_OP_UNMAP_RANGE;
	}
}

static void
svm_range_unmap_from_cpu(struct mm_struct *mm, struct svm_range *prange,
			 unsigned long start, unsigned long last)
{
	uint32_t trigger = KFD_SVM_UNMAP_TRIGGER_UNMAP_FROM_CPU;
	struct svm_range_list *svms;
	struct svm_range *pchild;
	struct kfd_process *p;
	unsigned long s, l;
	bool unmap_parent;
	uint32_t i;

	if (atomic_read(&prange->queue_refcount)) {
		int r;

		pr_warn("Freeing queue vital buffer 0x%lx, queue evicted\n",
			prange->start << PAGE_SHIFT);
		r = kgd2kfd_quiesce_mm(mm, KFD_QUEUE_EVICTION_TRIGGER_SVM);
		if (r)
			pr_debug("failed %d to quiesce KFD queues\n", r);
	}

	p = kfd_lookup_process_by_mm(mm);
	if (!p)
		return;
	svms = &p->svms;

	pr_debug("svms 0x%p prange 0x%p [0x%lx 0x%lx] [0x%lx 0x%lx]\n", svms,
		 prange, prange->start, prange->last, start, last);

	/* calculate time stamps that are used to decide which page faults need be
	 * dropped or handled before unmap pages from gpu vm
	 */
	for_each_set_bit(i, svms->bitmap_supported, p->n_pdds) {
		struct kfd_process_device *pdd;
		struct amdgpu_device *adev;
		struct amdgpu_ih_ring *ih;
		uint32_t checkpoint_wptr;

		pdd = p->pdds[i];
		if (!pdd)
			continue;

		adev = pdd->dev->adev;

		/* Check and drain ih1 ring if cam not available */
		if (adev->irq.ih1.ring_size) {
			ih = &adev->irq.ih1;
			checkpoint_wptr = amdgpu_ih_get_wptr(adev, ih);
			if (ih->rptr != checkpoint_wptr) {
				svms->checkpoint_ts[i] =
					amdgpu_ih_decode_iv_ts(adev, ih, checkpoint_wptr, -1);
				continue;
			}
		}

		/* check if dev->irq.ih_soft is not empty */
		ih = &adev->irq.ih_soft;
		checkpoint_wptr = amdgpu_ih_get_wptr(adev, ih);
		if (ih->rptr != checkpoint_wptr)
			svms->checkpoint_ts[i] = amdgpu_ih_decode_iv_ts(adev, ih, checkpoint_wptr, -1);
	}

	unmap_parent = start <= prange->start && last >= prange->last;

	list_for_each_entry(pchild, &prange->child_list, child_list) {
		mutex_lock_nested(&pchild->lock, 1);
		s = max(start, pchild->start);
		l = min(last, pchild->last);
		if (l >= s)
			svm_range_unmap_from_gpus(pchild, s, l, trigger);
		svm_range_unmap_split(prange, pchild, start, last);
		mutex_unlock(&pchild->lock);
	}
	s = max(start, prange->start);
	l = min(last, prange->last);
	if (l >= s)
		svm_range_unmap_from_gpus(prange, s, l, trigger);
	svm_range_unmap_split(prange, prange, start, last);

	if (unmap_parent)
		svm_range_add_list_work(svms, prange, mm, SVM_OP_UNMAP_RANGE);
	else
		svm_range_add_list_work(svms, prange, mm,
					SVM_OP_UPDATE_RANGE_NOTIFIER);
	schedule_deferred_list_work(svms);

	kfd_unref_process(p);
}

/**
 * svm_range_cpu_invalidate_pagetables - interval notifier callback
 * @mni: mmu_interval_notifier struct
 * @range: mmu_notifier_range struct
 * @cur_seq: value to pass to mmu_interval_set_seq()
 *
 * If event is MMU_NOTIFY_UNMAP, this is from CPU unmap range, otherwise, it
 * is from migration, or CPU page invalidation callback.
 *
 * For unmap event, unmap range from GPUs, remove prange from svms in a delayed
 * work thread, and split prange if only part of prange is unmapped.
 *
 * For invalidation event, if GPU retry fault is not enabled, evict the queues,
 * then schedule svm_range_restore_work to update GPU mapping and resume queues.
 * If GPU retry fault is enabled, unmap the svm range from GPU, retry fault will
 * update GPU mapping to recover.
 *
 * Context: mmap lock, notifier_invalidate_start lock are held
 *          for invalidate event, prange lock is held if this is from migration
 */
static bool
svm_range_cpu_invalidate_pagetables(struct mmu_interval_notifier *mni,
				    const struct mmu_notifier_range *range,
				    unsigned long cur_seq)
{
	struct svm_range *prange;
	unsigned long start;
	unsigned long last;

	if (range->event == MMU_NOTIFY_RELEASE)
		return true;

	start = mni->interval_tree.start;
	last = mni->interval_tree.last;
	start = max(start, range->start) >> PAGE_SHIFT;
	last = min(last, range->end - 1) >> PAGE_SHIFT;
	pr_debug("[0x%lx 0x%lx] range[0x%lx 0x%lx] notifier[0x%lx 0x%lx] %d\n",
		 start, last, range->start >> PAGE_SHIFT,
		 (range->end - 1) >> PAGE_SHIFT,
		 mni->interval_tree.start >> PAGE_SHIFT,
		 mni->interval_tree.last >> PAGE_SHIFT, range->event);

	prange = container_of(mni, struct svm_range, notifier);

	svm_range_lock(prange);
	mmu_interval_set_seq(mni, cur_seq);

	switch (range->event) {
	case MMU_NOTIFY_UNMAP:
		svm_range_unmap_from_cpu(mni->mm, prange, start, last);
		break;
	default:
		svm_range_evict(prange, mni->mm, start, last, range->event);
		break;
	}

	svm_range_unlock(prange);

	return true;
}

/**
 * svm_range_from_addr - find svm range from fault address
 * @svms: svm range list header
 * @addr: address to search range interval tree, in pages
 * @parent: parent range if range is on child list
 *
 * Context: The caller must hold svms->lock
 *
 * Return: the svm_range found or NULL
 */
struct svm_range *
svm_range_from_addr(struct svm_range_list *svms, unsigned long addr,
		    struct svm_range **parent)
{
	struct interval_tree_node *node;
	struct svm_range *prange;
	struct svm_range *pchild;

	node = interval_tree_iter_first(&svms->objects, addr, addr);
	if (!node)
		return NULL;

	prange = container_of(node, struct svm_range, it_node);
	pr_debug("address 0x%lx prange [0x%lx 0x%lx] node [0x%lx 0x%lx]\n",
		 addr, prange->start, prange->last, node->start, node->last);

	if (addr >= prange->start && addr <= prange->last) {
		if (parent)
			*parent = prange;
		return prange;
	}
	list_for_each_entry(pchild, &prange->child_list, child_list)
		if (addr >= pchild->start && addr <= pchild->last) {
			pr_debug("found address 0x%lx pchild [0x%lx 0x%lx]\n",
				 addr, pchild->start, pchild->last);
			if (parent)
				*parent = prange;
			return pchild;
		}

	return NULL;
}

/* svm_range_best_restore_location - decide the best fault restore location
 * @prange: svm range structure
 * @adev: the GPU on which vm fault happened
 *
 * This is only called when xnack is on, to decide the best location to restore
 * the range mapping after GPU vm fault. Caller uses the best location to do
 * migration if actual loc is not best location, then update GPU page table
 * mapping to the best location.
 *
 * If the preferred loc is accessible by faulting GPU, use preferred loc.
 * If vm fault gpu idx is on range ACCESSIBLE bitmap, best_loc is vm fault gpu
 * If vm fault gpu idx is on range ACCESSIBLE_IN_PLACE bitmap, then
 *    if range actual loc is cpu, best_loc is cpu
 *    if vm fault gpu is on xgmi same hive of range actual loc gpu, best_loc is
 *    range actual loc.
 * Otherwise, GPU no access, best_loc is -1.
 *
 * Return:
 * -1 means vm fault GPU no access
 * 0 for CPU or GPU id
 */
static int32_t
svm_range_best_restore_location(struct svm_range *prange,
				struct kfd_node *node,
				int32_t *gpuidx)
{
	struct kfd_node *bo_node, *preferred_node;
	struct kfd_process *p;
	uint32_t gpuid;
	int r;

	p = container_of(prange->svms, struct kfd_process, svms);

	r = kfd_process_gpuid_from_node(p, node, &gpuid, gpuidx);
	if (r < 0) {
		pr_debug("failed to get gpuid from kgd\n");
		return -1;
	}

	if (node->adev->apu_prefer_gtt)
		return 0;

	if (prange->preferred_loc == gpuid ||
	    prange->preferred_loc == KFD_IOCTL_SVM_LOCATION_SYSMEM) {
		return prange->preferred_loc;
	} else if (prange->preferred_loc != KFD_IOCTL_SVM_LOCATION_UNDEFINED) {
		preferred_node = svm_range_get_node_by_id(prange, prange->preferred_loc);
		if (preferred_node && svm_nodes_in_same_hive(node, preferred_node))
			return prange->preferred_loc;
		/* fall through */
	}

	if (test_bit(*gpuidx, prange->bitmap_access))
		return gpuid;

	if (test_bit(*gpuidx, prange->bitmap_aip)) {
		if (!prange->actual_loc)
			return 0;

		bo_node = svm_range_get_node_by_id(prange, prange->actual_loc);
		if (bo_node && svm_nodes_in_same_hive(node, bo_node))
			return prange->actual_loc;
		else
			return 0;
	}

	return -1;
}

static int
svm_range_get_range_boundaries(struct kfd_process *p, int64_t addr,
			       unsigned long *start, unsigned long *last,
			       bool *is_heap_stack)
{
	struct vm_area_struct *vma;
	struct interval_tree_node *node;
	struct rb_node *rb_node;
	unsigned long start_limit, end_limit;

	vma = vma_lookup(p->mm, addr << PAGE_SHIFT);
	if (!vma) {
		pr_debug("VMA does not exist in address [0x%llx]\n", addr);
		return -EFAULT;
	}

	*is_heap_stack = vma_is_initial_heap(vma) || vma_is_initial_stack(vma);

	start_limit = max(vma->vm_start >> PAGE_SHIFT,
		      (unsigned long)ALIGN_DOWN(addr, 1UL << p->svms.default_granularity));
	end_limit = min(vma->vm_end >> PAGE_SHIFT,
		    (unsigned long)ALIGN(addr + 1, 1UL << p->svms.default_granularity));

	/* First range that starts after the fault address */
	node = interval_tree_iter_first(&p->svms.objects, addr + 1, ULONG_MAX);
	if (node) {
		end_limit = min(end_limit, node->start);
		/* Last range that ends before the fault address */
		rb_node = rb_prev(&node->rb);
	} else {
		/* Last range must end before addr because
		 * there was no range after addr
		 */
		rb_node = rb_last(&p->svms.objects.rb_root);
	}
	if (rb_node) {
		node = container_of(rb_node, struct interval_tree_node, rb);
		if (node->last >= addr) {
			WARN(1, "Overlap with prev node and page fault addr\n");
			return -EFAULT;
		}
		start_limit = max(start_limit, node->last + 1);
	}

	*start = start_limit;
	*last = end_limit - 1;

	pr_debug("vma [0x%lx 0x%lx] range [0x%lx 0x%lx] is_heap_stack %d\n",
		 vma->vm_start >> PAGE_SHIFT, vma->vm_end >> PAGE_SHIFT,
		 *start, *last, *is_heap_stack);

	return 0;
}

static int
svm_range_check_vm_userptr(struct kfd_process *p, uint64_t start, uint64_t last,
			   uint64_t *bo_s, uint64_t *bo_l)
{
	struct amdgpu_bo_va_mapping *mapping;
	struct interval_tree_node *node;
	struct amdgpu_bo *bo = NULL;
	unsigned long userptr;
	uint32_t i;
	int r;

	for (i = 0; i < p->n_pdds; i++) {
		struct amdgpu_vm *vm;

		if (!p->pdds[i]->drm_priv)
			continue;

		vm = drm_priv_to_vm(p->pdds[i]->drm_priv);
		r = amdgpu_bo_reserve(vm->root.bo, false);
		if (r)
			return r;

		/* Check userptr by searching entire vm->va interval tree */
		node = interval_tree_iter_first(&vm->va, 0, ~0ULL);
		while (node) {
			mapping = container_of((struct rb_node *)node,
					       struct amdgpu_bo_va_mapping, rb);
			bo = mapping->bo_va->base.bo;

			if (!amdgpu_ttm_tt_affect_userptr(bo->tbo.ttm,
							 start << PAGE_SHIFT,
							 last << PAGE_SHIFT,
							 &userptr)) {
				node = interval_tree_iter_next(node, 0, ~0ULL);
				continue;
			}

			pr_debug("[0x%llx 0x%llx] already userptr mapped\n",
				 start, last);
			if (bo_s && bo_l) {
				*bo_s = userptr >> PAGE_SHIFT;
				*bo_l = *bo_s + bo->tbo.ttm->num_pages - 1;
			}
			amdgpu_bo_unreserve(vm->root.bo);
			return -EADDRINUSE;
		}
		amdgpu_bo_unreserve(vm->root.bo);
	}
	return 0;
}

static struct
svm_range *svm_range_create_unregistered_range(struct kfd_node *node,
						struct kfd_process *p,
						struct mm_struct *mm,
						int64_t addr)
{
	struct svm_range *prange = NULL;
	unsigned long start, last;
	uint32_t gpuid, gpuidx;
	bool is_heap_stack;
	uint64_t bo_s = 0;
	uint64_t bo_l = 0;
	int r;

	if (svm_range_get_range_boundaries(p, addr, &start, &last,
					   &is_heap_stack))
		return NULL;

	r = svm_range_check_vm(p, start, last, &bo_s, &bo_l);
	if (r != -EADDRINUSE)
		r = svm_range_check_vm_userptr(p, start, last, &bo_s, &bo_l);

	if (r == -EADDRINUSE) {
		if (addr >= bo_s && addr <= bo_l)
			return NULL;

		/* Create one page svm range if 2MB range overlapping */
		start = addr;
		last = addr;
	}

	prange = svm_range_new(&p->svms, start, last, true);
	if (!prange) {
		pr_debug("Failed to create prange in address [0x%llx]\n", addr);
		return NULL;
	}
	if (kfd_process_gpuid_from_node(p, node, &gpuid, &gpuidx)) {
		pr_debug("failed to get gpuid from kgd\n");
		svm_range_free(prange, true);
		return NULL;
	}

	if (is_heap_stack)
		prange->preferred_loc = KFD_IOCTL_SVM_LOCATION_SYSMEM;

	svm_range_add_to_svms(prange);
	svm_range_add_notifier_locked(mm, prange);

	return prange;
}

/* svm_range_skip_recover - decide if prange can be recovered
 * @prange: svm range structure
 *
 * GPU vm retry fault handle skip recover the range for cases:
 * 1. prange is on deferred list to be removed after unmap, it is stale fault,
 *    deferred list work will drain the stale fault before free the prange.
 * 2. prange is on deferred list to add interval notifier after split, or
 * 3. prange is child range, it is split from parent prange, recover later
 *    after interval notifier is added.
 *
 * Return: true to skip recover, false to recover
 */
static bool svm_range_skip_recover(struct svm_range *prange)
{
	struct svm_range_list *svms = prange->svms;

	spin_lock(&svms->deferred_list_lock);
	if (list_empty(&prange->deferred_list) &&
	    list_empty(&prange->child_list)) {
		spin_unlock(&svms->deferred_list_lock);
		return false;
	}
	spin_unlock(&svms->deferred_list_lock);

	if (prange->work_item.op == SVM_OP_UNMAP_RANGE) {
		pr_debug("svms 0x%p prange 0x%p [0x%lx 0x%lx] unmapped\n",
			 svms, prange, prange->start, prange->last);
		return true;
	}
	if (prange->work_item.op == SVM_OP_ADD_RANGE_AND_MAP ||
	    prange->work_item.op == SVM_OP_ADD_RANGE) {
		pr_debug("svms 0x%p prange 0x%p [0x%lx 0x%lx] not added yet\n",
			 svms, prange, prange->start, prange->last);
		return true;
	}
	return false;
}

static void
svm_range_count_fault(struct kfd_node *node, struct kfd_process *p,
		      int32_t gpuidx)
{
	struct kfd_process_device *pdd;

	/* fault is on different page of same range
	 * or fault is skipped to recover later
	 * or fault is on invalid virtual address
	 */
	if (gpuidx == MAX_GPU_INSTANCE) {
		uint32_t gpuid;
		int r;

		r = kfd_process_gpuid_from_node(p, node, &gpuid, &gpuidx);
		if (r < 0)
			return;
	}

	/* fault is recovered
	 * or fault cannot recover because GPU no access on the range
	 */
	pdd = kfd_process_device_from_gpuidx(p, gpuidx);
	if (pdd)
		WRITE_ONCE(pdd->faults, pdd->faults + 1);
}

static bool
svm_fault_allowed(struct vm_area_struct *vma, bool write_fault)
{
	unsigned long requested = VM_READ;

	if (write_fault)
		requested |= VM_WRITE;

	pr_debug("requested 0x%lx, vma permission flags 0x%lx\n", requested,
		vma->vm_flags);
	return (vma->vm_flags & requested) == requested;
}

int
svm_range_restore_pages(struct amdgpu_device *adev, unsigned int pasid,
			uint32_t vmid, uint32_t node_id,
			uint64_t addr, uint64_t ts, bool write_fault)
{
	unsigned long start, last, size;
	struct mm_struct *mm = NULL;
	struct svm_range_list *svms;
	struct svm_range *prange;
	struct kfd_process *p;
	ktime_t timestamp = ktime_get_boottime();
	struct kfd_node *node;
	int32_t best_loc;
	int32_t gpuid, gpuidx = MAX_GPU_INSTANCE;
	bool write_locked = false;
	struct vm_area_struct *vma;
	bool migration = false;
	int r = 0;

	if (!KFD_IS_SVM_API_SUPPORTED(adev)) {
		pr_debug("device does not support SVM\n");
		return -EFAULT;
	}

	p = kfd_lookup_process_by_pasid(pasid, NULL);
	if (!p) {
		pr_debug("kfd process not founded pasid 0x%x\n", pasid);
		return 0;
	}
	svms = &p->svms;

	pr_debug("restoring svms 0x%p fault address 0x%llx\n", svms, addr);

	if (atomic_read(&svms->drain_pagefaults)) {
		pr_debug("page fault handling disabled, drop fault 0x%llx\n", addr);
		r = 0;
		goto out;
	}

	node = kfd_node_by_irq_ids(adev, node_id, vmid);
	if (!node) {
		pr_debug("kfd node does not exist node_id: %d, vmid: %d\n", node_id,
			 vmid);
		r = -EFAULT;
		goto out;
	}

	if (kfd_process_gpuid_from_node(p, node, &gpuid, &gpuidx)) {
		pr_debug("failed to get gpuid/gpuidex for node_id: %d\n", node_id);
		r = -EFAULT;
		goto out;
	}

	if (!p->xnack_enabled) {
		pr_debug("XNACK not enabled for pasid 0x%x\n", pasid);
		r = -EFAULT;
		goto out;
	}

	/* p->lead_thread is available as kfd_process_wq_release flush the work
	 * before releasing task ref.
	 */
	mm = get_task_mm(p->lead_thread);
	if (!mm) {
		pr_debug("svms 0x%p failed to get mm\n", svms);
		r = 0;
		goto out;
	}

	mmap_read_lock(mm);
retry_write_locked:
	mutex_lock(&svms->lock);

	/* check if this page fault time stamp is before svms->checkpoint_ts */
	if (svms->checkpoint_ts[gpuidx] != 0) {
		if (amdgpu_ih_ts_after_or_equal(ts,  svms->checkpoint_ts[gpuidx])) {
			pr_debug("draining retry fault, drop fault 0x%llx\n", addr);
			if (write_locked)
				mmap_write_downgrade(mm);
			r = -EAGAIN;
			goto out_unlock_svms;
		} else {
			/* ts is after svms->checkpoint_ts now, reset svms->checkpoint_ts
			 * to zero to avoid following ts wrap around give wrong comparing
			 */
			svms->checkpoint_ts[gpuidx] = 0;
		}
	}

	prange = svm_range_from_addr(svms, addr, NULL);
	if (!prange) {
		pr_debug("failed to find prange svms 0x%p address [0x%llx]\n",
			 svms, addr);
		if (!write_locked) {
			/* Need the write lock to create new range with MMU notifier.
			 * Also flush pending deferred work to make sure the interval
			 * tree is up to date before we add a new range
			 */
			mutex_unlock(&svms->lock);
			mmap_read_unlock(mm);
			mmap_write_lock(mm);
			write_locked = true;
			goto retry_write_locked;
		}
		prange = svm_range_create_unregistered_range(node, p, mm, addr);
		if (!prange) {
			pr_debug("failed to create unregistered range svms 0x%p address [0x%llx]\n",
				 svms, addr);
			mmap_write_downgrade(mm);
			r = -EFAULT;
			goto out_unlock_svms;
		}
	}
	if (write_locked)
		mmap_write_downgrade(mm);

	mutex_lock(&prange->migrate_mutex);

	if (svm_range_skip_recover(prange)) {
		amdgpu_gmc_filter_faults_remove(node->adev, addr, pasid);
		r = 0;
		goto out_unlock_range;
	}

	/* skip duplicate vm fault on different pages of same range */
	if (ktime_before(timestamp, ktime_add_ns(prange->validate_timestamp,
				AMDGPU_SVM_RANGE_RETRY_FAULT_PENDING))) {
		pr_debug("svms 0x%p [0x%lx %lx] already restored\n",
			 svms, prange->start, prange->last);
		r = 0;
		goto out_unlock_range;
	}

	/* __do_munmap removed VMA, return success as we are handling stale
	 * retry fault.
	 */
	vma = vma_lookup(mm, addr << PAGE_SHIFT);
	if (!vma) {
		pr_debug("address 0x%llx VMA is removed\n", addr);
		r = 0;
		goto out_unlock_range;
	}

	if (!svm_fault_allowed(vma, write_fault)) {
		pr_debug("fault addr 0x%llx no %s permission\n", addr,
			write_fault ? "write" : "read");
		r = -EPERM;
		goto out_unlock_range;
	}

	best_loc = svm_range_best_restore_location(prange, node, &gpuidx);
	if (best_loc == -1) {
		pr_debug("svms %p failed get best restore loc [0x%lx 0x%lx]\n",
			 svms, prange->start, prange->last);
		r = -EACCES;
		goto out_unlock_range;
	}

	pr_debug("svms %p [0x%lx 0x%lx] best restore 0x%x, actual loc 0x%x\n",
		 svms, prange->start, prange->last, best_loc,
		 prange->actual_loc);

	kfd_smi_event_page_fault_start(node, p->lead_thread->pid, addr,
				       write_fault, timestamp);

	/* Align migration range start and size to granularity size */
	size = 1UL << prange->granularity;
	start = max_t(unsigned long, ALIGN_DOWN(addr, size), prange->start);
	last = min_t(unsigned long, ALIGN(addr + 1, size) - 1, prange->last);
	if (prange->actual_loc != 0 || best_loc != 0) {
		if (best_loc) {
			r = svm_migrate_to_vram(prange, best_loc, start, last,
					mm, KFD_MIGRATE_TRIGGER_PAGEFAULT_GPU);
			if (r) {
				pr_debug("svm_migrate_to_vram failed (%d) at %llx, falling back to system memory\n",
					 r, addr);
				/* Fallback to system memory if migration to
				 * VRAM failed
				 */
				if (prange->actual_loc && prange->actual_loc != best_loc)
					r = svm_migrate_vram_to_ram(prange, mm, start, last,
						KFD_MIGRATE_TRIGGER_PAGEFAULT_GPU, NULL);
				else
					r = 0;
			}
		} else {
			r = svm_migrate_vram_to_ram(prange, mm, start, last,
					KFD_MIGRATE_TRIGGER_PAGEFAULT_GPU, NULL);
		}
		if (r) {
			pr_debug("failed %d to migrate svms %p [0x%lx 0x%lx]\n",
				 r, svms, start, last);
			goto out_migrate_fail;
		} else {
			migration = true;
		}
	}

	r = svm_range_validate_and_map(mm, start, last, prange, gpuidx, false,
				       false, false);
	if (r)
		pr_debug("failed %d to map svms 0x%p [0x%lx 0x%lx] to gpus\n",
			 r, svms, start, last);

out_migrate_fail:
	kfd_smi_event_page_fault_end(node, p->lead_thread->pid, addr,
				     migration);

out_unlock_range:
	mutex_unlock(&prange->migrate_mutex);
out_unlock_svms:
	mutex_unlock(&svms->lock);
	mmap_read_unlock(mm);

	if (r != -EAGAIN)
		svm_range_count_fault(node, p, gpuidx);

	mmput(mm);
out:
	kfd_unref_process(p);

	if (r == -EAGAIN) {
		pr_debug("recover vm fault later\n");
		amdgpu_gmc_filter_faults_remove(node->adev, addr, pasid);
		r = 0;
	}
	return r;
}

int
svm_range_switch_xnack_reserve_mem(struct kfd_process *p, bool xnack_enabled)
{
	struct svm_range *prange, *pchild;
	uint64_t reserved_size = 0;
	uint64_t size;
	int r = 0;

	pr_debug("switching xnack from %d to %d\n", p->xnack_enabled, xnack_enabled);

	mutex_lock(&p->svms.lock);

	list_for_each_entry(prange, &p->svms.list, list) {
		svm_range_lock(prange);
		list_for_each_entry(pchild, &prange->child_list, child_list) {
			size = (pchild->last - pchild->start + 1) << PAGE_SHIFT;
			if (xnack_enabled) {
				amdgpu_amdkfd_unreserve_mem_limit(NULL, size,
					KFD_IOC_ALLOC_MEM_FLAGS_USERPTR, 0);
			} else {
				r = amdgpu_amdkfd_reserve_mem_limit(NULL, size,
					KFD_IOC_ALLOC_MEM_FLAGS_USERPTR, 0);
				if (r)
					goto out_unlock;
				reserved_size += size;
			}
		}

		size = (prange->last - prange->start + 1) << PAGE_SHIFT;
		if (xnack_enabled) {
			amdgpu_amdkfd_unreserve_mem_limit(NULL, size,
					KFD_IOC_ALLOC_MEM_FLAGS_USERPTR, 0);
		} else {
			r = amdgpu_amdkfd_reserve_mem_limit(NULL, size,
					KFD_IOC_ALLOC_MEM_FLAGS_USERPTR, 0);
			if (r)
				goto out_unlock;
			reserved_size += size;
		}
out_unlock:
		svm_range_unlock(prange);
		if (r)
			break;
	}

	if (r)
		amdgpu_amdkfd_unreserve_mem_limit(NULL, reserved_size,
					KFD_IOC_ALLOC_MEM_FLAGS_USERPTR, 0);
	else
		/* Change xnack mode must be inside svms lock, to avoid race with
		 * svm_range_deferred_list_work unreserve memory in parallel.
		 */
		p->xnack_enabled = xnack_enabled;

	mutex_unlock(&p->svms.lock);
	return r;
}

void svm_range_list_fini(struct kfd_process *p)
{
	struct svm_range *prange;
	struct svm_range *next;

	pr_debug("process pid %d svms 0x%p\n", p->lead_thread->pid,
		 &p->svms);

	cancel_delayed_work_sync(&p->svms.restore_work);

	/* Ensure list work is finished before process is destroyed */
	flush_work(&p->svms.deferred_list_work);

	/*
	 * Ensure no retry fault comes in afterwards, as page fault handler will
	 * not find kfd process and take mm lock to recover fault.
	 * stop kfd page fault handing, then wait pending page faults got drained
	 */
	atomic_set(&p->svms.drain_pagefaults, 1);
	svm_range_drain_retry_fault(&p->svms);

	list_for_each_entry_safe(prange, next, &p->svms.list, list) {
		svm_range_unlink(prange);
		svm_range_remove_notifier(prange);
		svm_range_free(prange, true);
	}

	mutex_destroy(&p->svms.lock);

	pr_debug("process pid %d svms 0x%p done\n",
		p->lead_thread->pid, &p->svms);
}

int svm_range_list_init(struct kfd_process *p)
{
	struct svm_range_list *svms = &p->svms;
	int i;

	svms->objects = RB_ROOT_CACHED;
	mutex_init(&svms->lock);
	INIT_LIST_HEAD(&svms->list);
	atomic_set(&svms->evicted_ranges, 0);
	atomic_set(&svms->drain_pagefaults, 0);
	INIT_DELAYED_WORK(&svms->restore_work, svm_range_restore_work);
	INIT_WORK(&svms->deferred_list_work, svm_range_deferred_list_work);
	INIT_LIST_HEAD(&svms->deferred_range_list);
	INIT_LIST_HEAD(&svms->criu_svm_metadata_list);
	spin_lock_init(&svms->deferred_list_lock);

	for (i = 0; i < p->n_pdds; i++)
		if (KFD_IS_SVM_API_SUPPORTED(p->pdds[i]->dev->adev))
			bitmap_set(svms->bitmap_supported, i, 1);

	 /* Value of default granularity cannot exceed 0x1B, the
	  * number of pages supported by a 4-level paging table
	  */
	svms->default_granularity = min_t(u8, amdgpu_svm_default_granularity, 0x1B);
	pr_debug("Default SVM Granularity to use: %d\n", svms->default_granularity);

	return 0;
}

/**
 * svm_range_check_vm - check if virtual address range mapped already
 * @p: current kfd_process
 * @start: range start address, in pages
 * @last: range last address, in pages
 * @bo_s: mapping start address in pages if address range already mapped
 * @bo_l: mapping last address in pages if address range already mapped
 *
 * The purpose is to avoid virtual address ranges already allocated by
 * kfd_ioctl_alloc_memory_of_gpu ioctl.
 * It looks for each pdd in the kfd_process.
 *
 * Context: Process context
 *
 * Return 0 - OK, if the range is not mapped.
 * Otherwise error code:
 * -EADDRINUSE - if address is mapped already by kfd_ioctl_alloc_memory_of_gpu
 * -ERESTARTSYS - A wait for the buffer to become unreserved was interrupted by
 * a signal. Release all buffer reservations and return to user-space.
 */
static int
svm_range_check_vm(struct kfd_process *p, uint64_t start, uint64_t last,
		   uint64_t *bo_s, uint64_t *bo_l)
{
	struct amdgpu_bo_va_mapping *mapping;
	struct interval_tree_node *node;
	uint32_t i;
	int r;

	for (i = 0; i < p->n_pdds; i++) {
		struct amdgpu_vm *vm;

		if (!p->pdds[i]->drm_priv)
			continue;

		vm = drm_priv_to_vm(p->pdds[i]->drm_priv);
		r = amdgpu_bo_reserve(vm->root.bo, false);
		if (r)
			return r;

		node = interval_tree_iter_first(&vm->va, start, last);
		if (node) {
			pr_debug("range [0x%llx 0x%llx] already TTM mapped\n",
				 start, last);
			mapping = container_of((struct rb_node *)node,
					       struct amdgpu_bo_va_mapping, rb);
			if (bo_s && bo_l) {
				*bo_s = mapping->start;
				*bo_l = mapping->last;
			}
			amdgpu_bo_unreserve(vm->root.bo);
			return -EADDRINUSE;
		}
		amdgpu_bo_unreserve(vm->root.bo);
	}

	return 0;
}

/**
 * svm_range_is_valid - check if virtual address range is valid
 * @p: current kfd_process
 * @start: range start address, in pages
 * @size: range size, in pages
 *
 * Valid virtual address range means it belongs to one or more VMAs
 *
 * Context: Process context
 *
 * Return:
 *  0 - OK, otherwise error code
 */
static int
svm_range_is_valid(struct kfd_process *p, uint64_t start, uint64_t size)
{
	const unsigned long device_vma = VM_IO | VM_PFNMAP | VM_MIXEDMAP;
	struct vm_area_struct *vma;
	unsigned long end;
	unsigned long start_unchg = start;

	start <<= PAGE_SHIFT;
	end = start + (size << PAGE_SHIFT);
	do {
		vma = vma_lookup(p->mm, start);
		if (!vma || (vma->vm_flags & device_vma))
			return -EFAULT;
		start = min(end, vma->vm_end);
	} while (start < end);

	return svm_range_check_vm(p, start_unchg, (end - 1) >> PAGE_SHIFT, NULL,
				  NULL);
}

/**
 * svm_range_best_prefetch_location - decide the best prefetch location
 * @prange: svm range structure
 *
 * For xnack off:
 * If range map to single GPU, the best prefetch location is prefetch_loc, which
 * can be CPU or GPU.
 *
 * If range is ACCESS or ACCESS_IN_PLACE by mGPUs, only if mGPU connection on
 * XGMI same hive, the best prefetch location is prefetch_loc GPU, othervise
 * the best prefetch location is always CPU, because GPU can not have coherent
 * mapping VRAM of other GPUs even with large-BAR PCIe connection.
 *
 * For xnack on:
 * If range is not ACCESS_IN_PLACE by mGPUs, the best prefetch location is
 * prefetch_loc, other GPU access will generate vm fault and trigger migration.
 *
 * If range is ACCESS_IN_PLACE by mGPUs, only if mGPU connection on XGMI same
 * hive, the best prefetch location is prefetch_loc GPU, otherwise the best
 * prefetch location is always CPU.
 *
 * Context: Process context
 *
 * Return:
 * 0 for CPU or GPU id
 */
static uint32_t
svm_range_best_prefetch_location(struct svm_range *prange)
{
	DECLARE_BITMAP(bitmap, MAX_GPU_INSTANCE);
	uint32_t best_loc = prange->prefetch_loc;
	struct kfd_process_device *pdd;
	struct kfd_node *bo_node;
	struct kfd_process *p;
	uint32_t gpuidx;

	p = container_of(prange->svms, struct kfd_process, svms);

	if (!best_loc || best_loc == KFD_IOCTL_SVM_LOCATION_UNDEFINED)
		goto out;

	bo_node = svm_range_get_node_by_id(prange, best_loc);
	if (!bo_node) {
		WARN_ONCE(1, "failed to get valid kfd node at id%x\n", best_loc);
		best_loc = 0;
		goto out;
	}

	if (bo_node->adev->apu_prefer_gtt) {
		best_loc = 0;
		goto out;
	}

	if (p->xnack_enabled)
		bitmap_copy(bitmap, prange->bitmap_aip, MAX_GPU_INSTANCE);
	else
		bitmap_or(bitmap, prange->bitmap_access, prange->bitmap_aip,
			  MAX_GPU_INSTANCE);

	for_each_set_bit(gpuidx, bitmap, MAX_GPU_INSTANCE) {
		pdd = kfd_process_device_from_gpuidx(p, gpuidx);
		if (!pdd) {
			pr_debug("failed to get device by idx 0x%x\n", gpuidx);
			continue;
		}

		if (pdd->dev->adev == bo_node->adev)
			continue;

		if (!svm_nodes_in_same_hive(pdd->dev, bo_node)) {
			best_loc = 0;
			break;
		}
	}

out:
	pr_debug("xnack %d svms 0x%p [0x%lx 0x%lx] best loc 0x%x\n",
		 p->xnack_enabled, &p->svms, prange->start, prange->last,
		 best_loc);

	return best_loc;
}

/* svm_range_trigger_migration - start page migration if prefetch loc changed
 * @mm: current process mm_struct
 * @prange: svm range structure
 * @migrated: output, true if migration is triggered
 *
 * If range perfetch_loc is GPU, actual loc is cpu 0, then migrate the range
 * from ram to vram.
 * If range prefetch_loc is cpu 0, actual loc is GPU, then migrate the range
 * from vram to ram.
 *
 * If GPU vm fault retry is not enabled, migration interact with MMU notifier
 * and restore work:
 * 1. migrate_vma_setup invalidate pages, MMU notifier callback svm_range_evict
 *    stops all queues, schedule restore work
 * 2. svm_range_restore_work wait for migration is done by
 *    a. svm_range_validate_vram takes prange->migrate_mutex
 *    b. svm_range_validate_ram HMM get pages wait for CPU fault handle returns
 * 3. restore work update mappings of GPU, resume all queues.
 *
 * Context: Process context
 *
 * Return:
 * 0 - OK, otherwise - error code of migration
 */
static int
svm_range_trigger_migration(struct mm_struct *mm, struct svm_range *prange,
			    bool *migrated)
{
	uint32_t best_loc;
	int r = 0;

	*migrated = false;
	best_loc = svm_range_best_prefetch_location(prange);

	/* when best_loc is a gpu node and same as prange->actual_loc
	 * we still need do migration as prange->actual_loc !=0 does
	 * not mean all pages in prange are vram. hmm migrate will pick
	 * up right pages during migration.
	 */
	if ((best_loc == KFD_IOCTL_SVM_LOCATION_UNDEFINED) ||
	    (best_loc == 0 && prange->actual_loc == 0))
		return 0;

	if (!best_loc) {
		r = svm_migrate_vram_to_ram(prange, mm, prange->start, prange->last,
					KFD_MIGRATE_TRIGGER_PREFETCH, NULL);
		*migrated = !r;
		return r;
	}

	r = svm_migrate_to_vram(prange, best_loc, prange->start, prange->last,
				mm, KFD_MIGRATE_TRIGGER_PREFETCH);
	*migrated = !r;

	return 0;
}

int svm_range_schedule_evict_svm_bo(struct amdgpu_amdkfd_fence *fence)
{
	/* Dereferencing fence->svm_bo is safe here because the fence hasn't
	 * signaled yet and we're under the protection of the fence->lock.
	 * After the fence is signaled in svm_range_bo_release, we cannot get
	 * here any more.
	 *
	 * Reference is dropped in svm_range_evict_svm_bo_worker.
	 */
	if (svm_bo_ref_unless_zero(fence->svm_bo)) {
		WRITE_ONCE(fence->svm_bo->evicting, 1);
		schedule_work(&fence->svm_bo->eviction_work);
	}

	return 0;
}

static void svm_range_evict_svm_bo_worker(struct work_struct *work)
{
	struct svm_range_bo *svm_bo;
	struct mm_struct *mm;
	int r = 0;

	svm_bo = container_of(work, struct svm_range_bo, eviction_work);

	if (mmget_not_zero(svm_bo->eviction_fence->mm)) {
		mm = svm_bo->eviction_fence->mm;
	} else {
		svm_range_bo_unref(svm_bo);
		return;
	}

	mmap_read_lock(mm);
	spin_lock(&svm_bo->list_lock);
	while (!list_empty(&svm_bo->range_list) && !r) {
		struct svm_range *prange =
				list_first_entry(&svm_bo->range_list,
						struct svm_range, svm_bo_list);
		int retries = 3;

		list_del_init(&prange->svm_bo_list);
		spin_unlock(&svm_bo->list_lock);

		pr_debug("svms 0x%p [0x%lx 0x%lx]\n", prange->svms,
			 prange->start, prange->last);

		mutex_lock(&prange->migrate_mutex);
		do {
			/* migrate all vram pages in this prange to sys ram
			 * after that prange->actual_loc should be zero
			 */
			r = svm_migrate_vram_to_ram(prange, mm,
					prange->start, prange->last,
					KFD_MIGRATE_TRIGGER_TTM_EVICTION, NULL);
		} while (!r && prange->actual_loc && --retries);

		if (!r && prange->actual_loc)
			pr_info_once("Migration failed during eviction");

		if (!prange->actual_loc) {
			mutex_lock(&prange->lock);
			prange->svm_bo = NULL;
			mutex_unlock(&prange->lock);
		}
		mutex_unlock(&prange->migrate_mutex);

		spin_lock(&svm_bo->list_lock);
	}
	spin_unlock(&svm_bo->list_lock);
	mmap_read_unlock(mm);
	mmput(mm);

	dma_fence_signal(&svm_bo->eviction_fence->base);

	/* This is the last reference to svm_bo, after svm_range_vram_node_free
	 * has been called in svm_migrate_vram_to_ram
	 */
	WARN_ONCE(!r && kref_read(&svm_bo->kref) != 1, "This was not the last reference\n");
	svm_range_bo_unref(svm_bo);
}

static int
svm_range_set_attr(struct kfd_process *p, struct mm_struct *mm,
		   uint64_t start, uint64_t size, uint32_t nattr,
		   struct kfd_ioctl_svm_attribute *attrs)
{
	struct amdkfd_process_info *process_info = p->kgd_process_info;
	struct list_head update_list;
	struct list_head insert_list;
	struct list_head remove_list;
	struct list_head remap_list;
	struct svm_range_list *svms;
	struct svm_range *prange;
	struct svm_range *next;
	bool update_mapping = false;
	bool flush_tlb;
	int r, ret = 0;

	pr_debug("process pid %d svms 0x%p [0x%llx 0x%llx] pages 0x%llx\n",
		 p->lead_thread->pid, &p->svms, start, start + size - 1, size);

	r = svm_range_check_attr(p, nattr, attrs);
	if (r)
		return r;

	svms = &p->svms;

	mutex_lock(&process_info->lock);

	svm_range_list_lock_and_flush_work(svms, mm);

	r = svm_range_is_valid(p, start, size);
	if (r) {
		pr_debug("invalid range r=%d\n", r);
		mmap_write_unlock(mm);
		goto out;
	}

	mutex_lock(&svms->lock);

	/* Add new range and split existing ranges as needed */
	r = svm_range_add(p, start, size, nattr, attrs, &update_list,
			  &insert_list, &remove_list, &remap_list);
	if (r) {
		mutex_unlock(&svms->lock);
		mmap_write_unlock(mm);
		goto out;
	}
	/* Apply changes as a transaction */
	list_for_each_entry_safe(prange, next, &insert_list, list) {
		svm_range_add_to_svms(prange);
		svm_range_add_notifier_locked(mm, prange);
	}
	list_for_each_entry(prange, &update_list, update_list) {
		svm_range_apply_attrs(p, prange, nattr, attrs, &update_mapping);
		/* TODO: unmap ranges from GPU that lost access */
	}
	update_mapping |= !p->xnack_enabled && !list_empty(&remap_list);

	list_for_each_entry_safe(prange, next, &remove_list, update_list) {
		pr_debug("unlink old 0x%p prange 0x%p [0x%lx 0x%lx]\n",
			 prange->svms, prange, prange->start,
			 prange->last);
		svm_range_unlink(prange);
		svm_range_remove_notifier(prange);
		svm_range_free(prange, false);
	}

	mmap_write_downgrade(mm);
	/* Trigger migrations and revalidate and map to GPUs as needed. If
	 * this fails we may be left with partially completed actions. There
	 * is no clean way of rolling back to the previous state in such a
	 * case because the rollback wouldn't be guaranteed to work either.
	 */
	list_for_each_entry(prange, &update_list, update_list) {
		bool migrated;

		mutex_lock(&prange->migrate_mutex);

		r = svm_range_trigger_migration(mm, prange, &migrated);
		if (r)
			goto out_unlock_range;

		if (migrated && (!p->xnack_enabled ||
		    (prange->flags & KFD_IOCTL_SVM_FLAG_GPU_ALWAYS_MAPPED)) &&
		    prange->mapped_to_gpu) {
			pr_debug("restore_work will update mappings of GPUs\n");
			mutex_unlock(&prange->migrate_mutex);
			continue;
		}

		if (!migrated && !update_mapping) {
			mutex_unlock(&prange->migrate_mutex);
			continue;
		}

		flush_tlb = !migrated && update_mapping && prange->mapped_to_gpu;

		r = svm_range_validate_and_map(mm, prange->start, prange->last, prange,
					       MAX_GPU_INSTANCE, true, true, flush_tlb);
		if (r)
			pr_debug("failed %d to map svm range\n", r);

out_unlock_range:
		mutex_unlock(&prange->migrate_mutex);
		if (r)
			ret = r;
	}

	list_for_each_entry(prange, &remap_list, update_list) {
		pr_debug("Remapping prange 0x%p [0x%lx 0x%lx]\n",
			 prange, prange->start, prange->last);
		mutex_lock(&prange->migrate_mutex);
		r = svm_range_validate_and_map(mm,  prange->start, prange->last, prange,
					       MAX_GPU_INSTANCE, true, true, prange->mapped_to_gpu);
		if (r)
			pr_debug("failed %d on remap svm range\n", r);
		mutex_unlock(&prange->migrate_mutex);
		if (r)
			ret = r;
	}

	dynamic_svm_range_dump(svms);

	mutex_unlock(&svms->lock);
	mmap_read_unlock(mm);
out:
	mutex_unlock(&process_info->lock);

	pr_debug("process pid %d svms 0x%p [0x%llx 0x%llx] done, r=%d\n",
		 p->lead_thread->pid, &p->svms, start, start + size - 1, r);

	return ret ? ret : r;
}

static int
svm_range_get_attr(struct kfd_process *p, struct mm_struct *mm,
		   uint64_t start, uint64_t size, uint32_t nattr,
		   struct kfd_ioctl_svm_attribute *attrs)
{
	DECLARE_BITMAP(bitmap_access, MAX_GPU_INSTANCE);
	DECLARE_BITMAP(bitmap_aip, MAX_GPU_INSTANCE);
	bool get_preferred_loc = false;
	bool get_prefetch_loc = false;
	bool get_granularity = false;
	bool get_accessible = false;
	bool get_flags = false;
	uint64_t last = start + size - 1UL;
	uint8_t granularity = 0xff;
	struct interval_tree_node *node;
	struct svm_range_list *svms;
	struct svm_range *prange;
	uint32_t prefetch_loc = KFD_IOCTL_SVM_LOCATION_UNDEFINED;
	uint32_t location = KFD_IOCTL_SVM_LOCATION_UNDEFINED;
	uint32_t flags_and = 0xffffffff;
	uint32_t flags_or = 0;
	int gpuidx;
	uint32_t i;
	int r = 0;

	pr_debug("svms 0x%p [0x%llx 0x%llx] nattr 0x%x\n", &p->svms, start,
		 start + size - 1, nattr);

	/* Flush pending deferred work to avoid racing with deferred actions from
	 * previous memory map changes (e.g. munmap). Concurrent memory map changes
	 * can still race with get_attr because we don't hold the mmap lock. But that
	 * would be a race condition in the application anyway, and undefined
	 * behaviour is acceptable in that case.
	 */
	flush_work(&p->svms.deferred_list_work);

	mmap_read_lock(mm);
	r = svm_range_is_valid(p, start, size);
	mmap_read_unlock(mm);
	if (r) {
		pr_debug("invalid range r=%d\n", r);
		return r;
	}

	for (i = 0; i < nattr; i++) {
		switch (attrs[i].type) {
		case KFD_IOCTL_SVM_ATTR_PREFERRED_LOC:
			get_preferred_loc = true;
			break;
		case KFD_IOCTL_SVM_ATTR_PREFETCH_LOC:
			get_prefetch_loc = true;
			break;
		case KFD_IOCTL_SVM_ATTR_ACCESS:
			get_accessible = true;
			break;
		case KFD_IOCTL_SVM_ATTR_SET_FLAGS:
		case KFD_IOCTL_SVM_ATTR_CLR_FLAGS:
			get_flags = true;
			break;
		case KFD_IOCTL_SVM_ATTR_GRANULARITY:
			get_granularity = true;
			break;
		case KFD_IOCTL_SVM_ATTR_ACCESS_IN_PLACE:
		case KFD_IOCTL_SVM_ATTR_NO_ACCESS:
			fallthrough;
		default:
			pr_debug("get invalid attr type 0x%x\n", attrs[i].type);
			return -EINVAL;
		}
	}

	svms = &p->svms;

	mutex_lock(&svms->lock);

	node = interval_tree_iter_first(&svms->objects, start, last);
	if (!node) {
		pr_debug("range attrs not found return default values\n");
		svm_range_set_default_attributes(svms, &location, &prefetch_loc,
						 &granularity, &flags_and);
		flags_or = flags_and;
		if (p->xnack_enabled)
			bitmap_copy(bitmap_access, svms->bitmap_supported,
				    MAX_GPU_INSTANCE);
		else
			bitmap_zero(bitmap_access, MAX_GPU_INSTANCE);
		bitmap_zero(bitmap_aip, MAX_GPU_INSTANCE);
		goto fill_values;
	}
	bitmap_copy(bitmap_access, svms->bitmap_supported, MAX_GPU_INSTANCE);
	bitmap_copy(bitmap_aip, svms->bitmap_supported, MAX_GPU_INSTANCE);

	while (node) {
		struct interval_tree_node *next;

		prange = container_of(node, struct svm_range, it_node);
		next = interval_tree_iter_next(node, start, last);

		if (get_preferred_loc) {
			if (prange->preferred_loc ==
					KFD_IOCTL_SVM_LOCATION_UNDEFINED ||
			    (location != KFD_IOCTL_SVM_LOCATION_UNDEFINED &&
			     location != prange->preferred_loc)) {
				location = KFD_IOCTL_SVM_LOCATION_UNDEFINED;
				get_preferred_loc = false;
			} else {
				location = prange->preferred_loc;
			}
		}
		if (get_prefetch_loc) {
			if (prange->prefetch_loc ==
					KFD_IOCTL_SVM_LOCATION_UNDEFINED ||
			    (prefetch_loc != KFD_IOCTL_SVM_LOCATION_UNDEFINED &&
			     prefetch_loc != prange->prefetch_loc)) {
				prefetch_loc = KFD_IOCTL_SVM_LOCATION_UNDEFINED;
				get_prefetch_loc = false;
			} else {
				prefetch_loc = prange->prefetch_loc;
			}
		}
		if (get_accessible) {
			bitmap_and(bitmap_access, bitmap_access,
				   prange->bitmap_access, MAX_GPU_INSTANCE);
			bitmap_and(bitmap_aip, bitmap_aip,
				   prange->bitmap_aip, MAX_GPU_INSTANCE);
		}
		if (get_flags) {
			flags_and &= prange->flags;
			flags_or |= prange->flags;
		}

		if (get_granularity && prange->granularity < granularity)
			granularity = prange->granularity;

		node = next;
	}
fill_values:
	mutex_unlock(&svms->lock);

	for (i = 0; i < nattr; i++) {
		switch (attrs[i].type) {
		case KFD_IOCTL_SVM_ATTR_PREFERRED_LOC:
			attrs[i].value = location;
			break;
		case KFD_IOCTL_SVM_ATTR_PREFETCH_LOC:
			attrs[i].value = prefetch_loc;
			break;
		case KFD_IOCTL_SVM_ATTR_ACCESS:
			gpuidx = kfd_process_gpuidx_from_gpuid(p,
							       attrs[i].value);
			if (gpuidx < 0) {
				pr_debug("invalid gpuid %x\n", attrs[i].value);
				return -EINVAL;
			}
			if (test_bit(gpuidx, bitmap_access))
				attrs[i].type = KFD_IOCTL_SVM_ATTR_ACCESS;
			else if (test_bit(gpuidx, bitmap_aip))
				attrs[i].type =
					KFD_IOCTL_SVM_ATTR_ACCESS_IN_PLACE;
			else
				attrs[i].type = KFD_IOCTL_SVM_ATTR_NO_ACCESS;
			break;
		case KFD_IOCTL_SVM_ATTR_SET_FLAGS:
			attrs[i].value = flags_and;
			break;
		case KFD_IOCTL_SVM_ATTR_CLR_FLAGS:
			attrs[i].value = ~flags_or;
			break;
		case KFD_IOCTL_SVM_ATTR_GRANULARITY:
			attrs[i].value = (uint32_t)granularity;
			break;
		}
	}

	return 0;
}

int kfd_criu_resume_svm(struct kfd_process *p)
{
	struct kfd_ioctl_svm_attribute *set_attr_new, *set_attr = NULL;
	int nattr_common = 4, nattr_accessibility = 1;
	struct criu_svm_metadata *criu_svm_md = NULL;
	struct svm_range_list *svms = &p->svms;
	struct criu_svm_metadata *next = NULL;
	uint32_t set_flags = 0xffffffff;
	int i, j, num_attrs, ret = 0;
	uint64_t set_attr_size;
	struct mm_struct *mm;

	if (list_empty(&svms->criu_svm_metadata_list)) {
		pr_debug("No SVM data from CRIU restore stage 2\n");
		return ret;
	}

	mm = get_task_mm(p->lead_thread);
	if (!mm) {
		pr_err("failed to get mm for the target process\n");
		return -ESRCH;
	}

	num_attrs = nattr_common + (nattr_accessibility * p->n_pdds);

	i = j = 0;
	list_for_each_entry(criu_svm_md, &svms->criu_svm_metadata_list, list) {
		pr_debug("criu_svm_md[%d]\n\tstart: 0x%llx size: 0x%llx (npages)\n",
			 i, criu_svm_md->data.start_addr, criu_svm_md->data.size);

		for (j = 0; j < num_attrs; j++) {
			pr_debug("\ncriu_svm_md[%d]->attrs[%d].type : 0x%x\ncriu_svm_md[%d]->attrs[%d].value : 0x%x\n",
				 i, j, criu_svm_md->data.attrs[j].type,
				 i, j, criu_svm_md->data.attrs[j].value);
			switch (criu_svm_md->data.attrs[j].type) {
			/* During Checkpoint operation, the query for
			 * KFD_IOCTL_SVM_ATTR_PREFETCH_LOC attribute might
			 * return KFD_IOCTL_SVM_LOCATION_UNDEFINED if they were
			 * not used by the range which was checkpointed. Care
			 * must be taken to not restore with an invalid value
			 * otherwise the gpuidx value will be invalid and
			 * set_attr would eventually fail so just replace those
			 * with another dummy attribute such as
			 * KFD_IOCTL_SVM_ATTR_SET_FLAGS.
			 */
			case KFD_IOCTL_SVM_ATTR_PREFETCH_LOC:
				if (criu_svm_md->data.attrs[j].value ==
				    KFD_IOCTL_SVM_LOCATION_UNDEFINED) {
					criu_svm_md->data.attrs[j].type =
						KFD_IOCTL_SVM_ATTR_SET_FLAGS;
					criu_svm_md->data.attrs[j].value = 0;
				}
				break;
			case KFD_IOCTL_SVM_ATTR_SET_FLAGS:
				set_flags = criu_svm_md->data.attrs[j].value;
				break;
			default:
				break;
			}
		}

		/* CLR_FLAGS is not available via get_attr during checkpoint but
		 * it needs to be inserted before restoring the ranges so
		 * allocate extra space for it before calling set_attr
		 */
		set_attr_size = sizeof(struct kfd_ioctl_svm_attribute) *
						(num_attrs + 1);
		set_attr_new = krealloc(set_attr, set_attr_size,
					    GFP_KERNEL);
		if (!set_attr_new) {
			ret = -ENOMEM;
			goto exit;
		}
		set_attr = set_attr_new;

		memcpy(set_attr, criu_svm_md->data.attrs, num_attrs *
					sizeof(struct kfd_ioctl_svm_attribute));
		set_attr[num_attrs].type = KFD_IOCTL_SVM_ATTR_CLR_FLAGS;
		set_attr[num_attrs].value = ~set_flags;

		ret = svm_range_set_attr(p, mm, criu_svm_md->data.start_addr,
					 criu_svm_md->data.size, num_attrs + 1,
					 set_attr);
		if (ret) {
			pr_err("CRIU: failed to set range attributes\n");
			goto exit;
		}

		i++;
	}
exit:
	kfree(set_attr);
	list_for_each_entry_safe(criu_svm_md, next, &svms->criu_svm_metadata_list, list) {
		pr_debug("freeing criu_svm_md[]\n\tstart: 0x%llx\n",
						criu_svm_md->data.start_addr);
		kfree(criu_svm_md);
	}

	mmput(mm);
	return ret;

}

int kfd_criu_restore_svm(struct kfd_process *p,
			 uint8_t __user *user_priv_ptr,
			 uint64_t *priv_data_offset,
			 uint64_t max_priv_data_size)
{
	uint64_t svm_priv_data_size, svm_object_md_size, svm_attrs_size;
	int nattr_common = 4, nattr_accessibility = 1;
	struct criu_svm_metadata *criu_svm_md = NULL;
	struct svm_range_list *svms = &p->svms;
	uint32_t num_devices;
	int ret = 0;

	num_devices = p->n_pdds;
	/* Handle one SVM range object at a time, also the number of gpus are
	 * assumed to be same on the restore node, checking must be done while
	 * evaluating the topology earlier
	 */

	svm_attrs_size = sizeof(struct kfd_ioctl_svm_attribute) *
		(nattr_common + nattr_accessibility * num_devices);
	svm_object_md_size = sizeof(struct criu_svm_metadata) + svm_attrs_size;

	svm_priv_data_size = sizeof(struct kfd_criu_svm_range_priv_data) +
								svm_attrs_size;

	criu_svm_md = kzalloc(svm_object_md_size, GFP_KERNEL);
	if (!criu_svm_md) {
		pr_err("failed to allocate memory to store svm metadata\n");
		return -ENOMEM;
	}
	if (*priv_data_offset + svm_priv_data_size > max_priv_data_size) {
		ret = -EINVAL;
		goto exit;
	}

	ret = copy_from_user(&criu_svm_md->data, user_priv_ptr + *priv_data_offset,
			     svm_priv_data_size);
	if (ret) {
		ret = -EFAULT;
		goto exit;
	}
	*priv_data_offset += svm_priv_data_size;

	list_add_tail(&criu_svm_md->list, &svms->criu_svm_metadata_list);

	return 0;


exit:
	kfree(criu_svm_md);
	return ret;
}

void svm_range_get_info(struct kfd_process *p, uint32_t *num_svm_ranges,
			uint64_t *svm_priv_data_size)
{
	uint64_t total_size, accessibility_size, common_attr_size;
	int nattr_common = 4, nattr_accessibility = 1;
	int num_devices = p->n_pdds;
	struct svm_range_list *svms;
	struct svm_range *prange;
	uint32_t count = 0;

	*svm_priv_data_size = 0;

	svms = &p->svms;

	mutex_lock(&svms->lock);
	list_for_each_entry(prange, &svms->list, list) {
		pr_debug("prange: 0x%p start: 0x%lx\t npages: 0x%llx\t end: 0x%llx\n",
			 prange, prange->start, prange->npages,
			 prange->start + prange->npages - 1);
		count++;
	}
	mutex_unlock(&svms->lock);

	*num_svm_ranges = count;
	/* Only the accessbility attributes need to be queried for all the gpus
	 * individually, remaining ones are spanned across the entire process
	 * regardless of the various gpu nodes. Of the remaining attributes,
	 * KFD_IOCTL_SVM_ATTR_CLR_FLAGS need not be saved.
	 *
	 * KFD_IOCTL_SVM_ATTR_PREFERRED_LOC
	 * KFD_IOCTL_SVM_ATTR_PREFETCH_LOC
	 * KFD_IOCTL_SVM_ATTR_SET_FLAGS
	 * KFD_IOCTL_SVM_ATTR_GRANULARITY
	 *
	 * ** ACCESSBILITY ATTRIBUTES **
	 * (Considered as one, type is altered during query, value is gpuid)
	 * KFD_IOCTL_SVM_ATTR_ACCESS
	 * KFD_IOCTL_SVM_ATTR_ACCESS_IN_PLACE
	 * KFD_IOCTL_SVM_ATTR_NO_ACCESS
	 */
	if (*num_svm_ranges > 0) {
		common_attr_size = sizeof(struct kfd_ioctl_svm_attribute) *
			nattr_common;
		accessibility_size = sizeof(struct kfd_ioctl_svm_attribute) *
			nattr_accessibility * num_devices;

		total_size = sizeof(struct kfd_criu_svm_range_priv_data) +
			common_attr_size + accessibility_size;

		*svm_priv_data_size = *num_svm_ranges * total_size;
	}

	pr_debug("num_svm_ranges %u total_priv_size %llu\n", *num_svm_ranges,
		 *svm_priv_data_size);
}

int kfd_criu_checkpoint_svm(struct kfd_process *p,
			    uint8_t __user *user_priv_data,
			    uint64_t *priv_data_offset)
{
	struct kfd_criu_svm_range_priv_data *svm_priv = NULL;
	struct kfd_ioctl_svm_attribute *query_attr = NULL;
	uint64_t svm_priv_data_size, query_attr_size = 0;
	int index, nattr_common = 4, ret = 0;
	struct svm_range_list *svms;
	int num_devices = p->n_pdds;
	struct svm_range *prange;
	struct mm_struct *mm;

	svms = &p->svms;

	mm = get_task_mm(p->lead_thread);
	if (!mm) {
		pr_err("failed to get mm for the target process\n");
		return -ESRCH;
	}

	query_attr_size = sizeof(struct kfd_ioctl_svm_attribute) *
				(nattr_common + num_devices);

	query_attr = kzalloc(query_attr_size, GFP_KERNEL);
	if (!query_attr) {
		ret = -ENOMEM;
		goto exit;
	}

	query_attr[0].type = KFD_IOCTL_SVM_ATTR_PREFERRED_LOC;
	query_attr[1].type = KFD_IOCTL_SVM_ATTR_PREFETCH_LOC;
	query_attr[2].type = KFD_IOCTL_SVM_ATTR_SET_FLAGS;
	query_attr[3].type = KFD_IOCTL_SVM_ATTR_GRANULARITY;

	for (index = 0; index < num_devices; index++) {
		struct kfd_process_device *pdd = p->pdds[index];

		query_attr[index + nattr_common].type =
			KFD_IOCTL_SVM_ATTR_ACCESS;
		query_attr[index + nattr_common].value = pdd->user_gpu_id;
	}

	svm_priv_data_size = sizeof(*svm_priv) + query_attr_size;

	svm_priv = kzalloc(svm_priv_data_size, GFP_KERNEL);
	if (!svm_priv) {
		ret = -ENOMEM;
		goto exit_query;
	}

	index = 0;
	list_for_each_entry(prange, &svms->list, list) {

		svm_priv->object_type = KFD_CRIU_OBJECT_TYPE_SVM_RANGE;
		svm_priv->start_addr = prange->start;
		svm_priv->size = prange->npages;
		memcpy(&svm_priv->attrs, query_attr, query_attr_size);
		pr_debug("CRIU: prange: 0x%p start: 0x%lx\t npages: 0x%llx end: 0x%llx\t size: 0x%llx\n",
			 prange, prange->start, prange->npages,
			 prange->start + prange->npages - 1,
			 prange->npages * PAGE_SIZE);

		ret = svm_range_get_attr(p, mm, svm_priv->start_addr,
					 svm_priv->size,
					 (nattr_common + num_devices),
					 svm_priv->attrs);
		if (ret) {
			pr_err("CRIU: failed to obtain range attributes\n");
			goto exit_priv;
		}

		if (copy_to_user(user_priv_data + *priv_data_offset, svm_priv,
				 svm_priv_data_size)) {
			pr_err("Failed to copy svm priv to user\n");
			ret = -EFAULT;
			goto exit_priv;
		}

		*priv_data_offset += svm_priv_data_size;

	}


exit_priv:
	kfree(svm_priv);
exit_query:
	kfree(query_attr);
exit:
	mmput(mm);
	return ret;
}

int
svm_ioctl(struct kfd_process *p, enum kfd_ioctl_svm_op op, uint64_t start,
	  uint64_t size, uint32_t nattrs, struct kfd_ioctl_svm_attribute *attrs)
{
	struct mm_struct *mm = current->mm;
	int r;

	start >>= PAGE_SHIFT;
	size >>= PAGE_SHIFT;

	switch (op) {
	case KFD_IOCTL_SVM_OP_SET_ATTR:
		r = svm_range_set_attr(p, mm, start, size, nattrs, attrs);
		break;
	case KFD_IOCTL_SVM_OP_GET_ATTR:
		r = svm_range_get_attr(p, mm, start, size, nattrs, attrs);
		break;
	default:
		r = -EINVAL;
		break;
	}

	return r;
}
