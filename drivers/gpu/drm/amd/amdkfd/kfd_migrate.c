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
#include <linux/hmm.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/migrate.h>
#include "amdgpu_sync.h"
#include "amdgpu_object.h"
#include "amdgpu_vm.h"
#include "amdgpu_res_cursor.h"
#include "kfd_priv.h"
#include "kfd_svm.h"
#include "kfd_migrate.h"
#include "kfd_smi_events.h"

#ifdef dev_fmt
#undef dev_fmt
#endif
#define dev_fmt(fmt) "kfd_migrate: " fmt

static uint64_t
svm_migrate_direct_mapping_addr(struct amdgpu_device *adev, uint64_t addr)
{
	return addr + amdgpu_ttm_domain_start(adev, TTM_PL_VRAM);
}

static int
svm_migrate_gart_map(struct amdgpu_ring *ring, uint64_t npages,
		     dma_addr_t *addr, uint64_t *gart_addr, uint64_t flags)
{
	struct amdgpu_device *adev = ring->adev;
	struct amdgpu_job *job;
	unsigned int num_dw, num_bytes;
	struct dma_fence *fence;
	uint64_t src_addr, dst_addr;
	uint64_t pte_flags;
	void *cpu_addr;
	int r;

	/* use gart window 0 */
	*gart_addr = adev->gmc.gart_start;

	num_dw = ALIGN(adev->mman.buffer_funcs->copy_num_dw, 8);
	num_bytes = npages * 8;

	r = amdgpu_job_alloc_with_ib(adev, &adev->mman.high_pr,
				     AMDGPU_FENCE_OWNER_UNDEFINED,
				     num_dw * 4 + num_bytes,
				     AMDGPU_IB_POOL_DELAYED,
				     &job);
	if (r)
		return r;

	src_addr = num_dw * 4;
	src_addr += job->ibs[0].gpu_addr;

	dst_addr = amdgpu_bo_gpu_offset(adev->gart.bo);
	amdgpu_emit_copy_buffer(adev, &job->ibs[0], src_addr,
				dst_addr, num_bytes, 0);

	amdgpu_ring_pad_ib(ring, &job->ibs[0]);
	WARN_ON(job->ibs[0].length_dw > num_dw);

	pte_flags = AMDGPU_PTE_VALID | AMDGPU_PTE_READABLE;
	pte_flags |= AMDGPU_PTE_SYSTEM | AMDGPU_PTE_SNOOPED;
	if (!(flags & KFD_IOCTL_SVM_FLAG_GPU_RO))
		pte_flags |= AMDGPU_PTE_WRITEABLE;
	pte_flags |= adev->gart.gart_pte_flags;

	cpu_addr = &job->ibs[0].ptr[num_dw];

	amdgpu_gart_map(adev, 0, npages, addr, pte_flags, cpu_addr);
	fence = amdgpu_job_submit(job);
	dma_fence_put(fence);

	return r;
}

/**
 * svm_migrate_copy_memory_gart - sdma copy data between ram and vram
 *
 * @adev: amdgpu device the sdma ring running
 * @sys: system DMA pointer to be copied
 * @vram: vram destination DMA pointer
 * @npages: number of pages to copy
 * @direction: enum MIGRATION_COPY_DIR
 * @mfence: output, sdma fence to signal after sdma is done
 *
 * ram address uses GART table continuous entries mapping to ram pages,
 * vram address uses direct mapping of vram pages, which must have npages
 * number of continuous pages.
 * GART update and sdma uses same buf copy function ring, sdma is splited to
 * multiple GTT_MAX_PAGES transfer, all sdma operations are serialized, wait for
 * the last sdma finish fence which is returned to check copy memory is done.
 *
 * Context: Process context, takes and releases gtt_window_lock
 *
 * Return:
 * 0 - OK, otherwise error code
 */

static int
svm_migrate_copy_memory_gart(struct amdgpu_device *adev, dma_addr_t *sys,
			     uint64_t *vram, uint64_t npages,
			     enum MIGRATION_COPY_DIR direction,
			     struct dma_fence **mfence)
{
	const uint64_t GTT_MAX_PAGES = AMDGPU_GTT_MAX_TRANSFER_SIZE;
	struct amdgpu_ring *ring = adev->mman.buffer_funcs_ring;
	uint64_t gart_s, gart_d;
	struct dma_fence *next;
	uint64_t size;
	int r;

	mutex_lock(&adev->mman.gtt_window_lock);

	while (npages) {
		size = min(GTT_MAX_PAGES, npages);

		if (direction == FROM_VRAM_TO_RAM) {
			gart_s = svm_migrate_direct_mapping_addr(adev, *vram);
			r = svm_migrate_gart_map(ring, size, sys, &gart_d, 0);

		} else if (direction == FROM_RAM_TO_VRAM) {
			r = svm_migrate_gart_map(ring, size, sys, &gart_s,
						 KFD_IOCTL_SVM_FLAG_GPU_RO);
			gart_d = svm_migrate_direct_mapping_addr(adev, *vram);
		}
		if (r) {
			dev_err(adev->dev, "fail %d create gart mapping\n", r);
			goto out_unlock;
		}

		r = amdgpu_copy_buffer(ring, gart_s, gart_d, size * PAGE_SIZE,
				       NULL, &next, false, true, 0);
		if (r) {
			dev_err(adev->dev, "fail %d to copy memory\n", r);
			goto out_unlock;
		}

		dma_fence_put(*mfence);
		*mfence = next;
		npages -= size;
		if (npages) {
			sys += size;
			vram += size;
		}
	}

out_unlock:
	mutex_unlock(&adev->mman.gtt_window_lock);

	return r;
}

/**
 * svm_migrate_copy_done - wait for memory copy sdma is done
 *
 * @adev: amdgpu device the sdma memory copy is executing on
 * @mfence: migrate fence
 *
 * Wait for dma fence is signaled, if the copy ssplit into multiple sdma
 * operations, this is the last sdma operation fence.
 *
 * Context: called after svm_migrate_copy_memory
 *
 * Return:
 * 0		- success
 * otherwise	- error code from dma fence signal
 */
static int
svm_migrate_copy_done(struct amdgpu_device *adev, struct dma_fence *mfence)
{
	int r = 0;

	if (mfence) {
		r = dma_fence_wait(mfence, false);
		dma_fence_put(mfence);
		pr_debug("sdma copy memory fence done\n");
	}

	return r;
}

unsigned long
svm_migrate_addr_to_pfn(struct amdgpu_device *adev, unsigned long addr)
{
	return (addr + adev->kfd.pgmap.range.start) >> PAGE_SHIFT;
}

static void
svm_migrate_get_vram_page(struct svm_range *prange, unsigned long pfn)
{
	struct page *page;

	page = pfn_to_page(pfn);
	svm_range_bo_ref(prange->svm_bo);
	page->zone_device_data = prange->svm_bo;
	zone_device_page_init(page);
}

static void
svm_migrate_put_vram_page(struct amdgpu_device *adev, unsigned long addr)
{
	struct page *page;

	page = pfn_to_page(svm_migrate_addr_to_pfn(adev, addr));
	unlock_page(page);
	put_page(page);
}

static unsigned long
svm_migrate_addr(struct amdgpu_device *adev, struct page *page)
{
	unsigned long addr;

	addr = page_to_pfn(page) << PAGE_SHIFT;
	return (addr - adev->kfd.pgmap.range.start);
}

static struct page *
svm_migrate_get_sys_page(struct vm_area_struct *vma, unsigned long addr)
{
	struct page *page;

	page = alloc_page_vma(GFP_HIGHUSER, vma, addr);
	if (page)
		lock_page(page);

	return page;
}

static void svm_migrate_put_sys_page(unsigned long addr)
{
	struct page *page;

	page = pfn_to_page(addr >> PAGE_SHIFT);
	unlock_page(page);
	put_page(page);
}

static unsigned long svm_migrate_unsuccessful_pages(struct migrate_vma *migrate)
{
	unsigned long upages = 0;
	unsigned long i;

	for (i = 0; i < migrate->npages; i++) {
		if (migrate->src[i] & MIGRATE_PFN_VALID &&
		    !(migrate->src[i] & MIGRATE_PFN_MIGRATE))
			upages++;
	}
	return upages;
}

static int
svm_migrate_copy_to_vram(struct kfd_node *node, struct svm_range *prange,
			 struct migrate_vma *migrate, struct dma_fence **mfence,
			 dma_addr_t *scratch, uint64_t ttm_res_offset)
{
	uint64_t npages = migrate->npages;
	struct amdgpu_device *adev = node->adev;
	struct device *dev = adev->dev;
	struct amdgpu_res_cursor cursor;
	uint64_t mpages = 0;
	dma_addr_t *src;
	uint64_t *dst;
	uint64_t i, j;
	int r;

	pr_debug("svms 0x%p [0x%lx 0x%lx 0x%llx]\n", prange->svms, prange->start,
		 prange->last, ttm_res_offset);

	src = scratch;
	dst = (uint64_t *)(scratch + npages);

	amdgpu_res_first(prange->ttm_res, ttm_res_offset,
			 npages << PAGE_SHIFT, &cursor);
	for (i = j = 0; (i < npages) && (mpages < migrate->cpages); i++) {
		struct page *spage;

		if (migrate->src[i] & MIGRATE_PFN_MIGRATE) {
			dst[i] = cursor.start + (j << PAGE_SHIFT);
			migrate->dst[i] = svm_migrate_addr_to_pfn(adev, dst[i]);
			svm_migrate_get_vram_page(prange, migrate->dst[i]);
			migrate->dst[i] = migrate_pfn(migrate->dst[i]);
			mpages++;
		}
		spage = migrate_pfn_to_page(migrate->src[i]);
		if (spage && !is_zone_device_page(spage)) {
			src[i] = dma_map_page(dev, spage, 0, PAGE_SIZE,
					      DMA_BIDIRECTIONAL);
			r = dma_mapping_error(dev, src[i]);
			if (r) {
				dev_err(dev, "%s: fail %d dma_map_page\n",
					__func__, r);
				goto out_free_vram_pages;
			}
		} else {
			if (j) {
				r = svm_migrate_copy_memory_gart(
						adev, src + i - j,
						dst + i - j, j,
						FROM_RAM_TO_VRAM,
						mfence);
				if (r)
					goto out_free_vram_pages;
				amdgpu_res_next(&cursor, (j + 1) << PAGE_SHIFT);
				j = 0;
			} else {
				amdgpu_res_next(&cursor, PAGE_SIZE);
			}
			continue;
		}

		pr_debug_ratelimited("dma mapping src to 0x%llx, pfn 0x%lx\n",
				     src[i] >> PAGE_SHIFT, page_to_pfn(spage));

		if (j >= (cursor.size >> PAGE_SHIFT) - 1 && i < npages - 1) {
			r = svm_migrate_copy_memory_gart(adev, src + i - j,
							 dst + i - j, j + 1,
							 FROM_RAM_TO_VRAM,
							 mfence);
			if (r)
				goto out_free_vram_pages;
			amdgpu_res_next(&cursor, (j + 1) * PAGE_SIZE);
			j = 0;
		} else {
			j++;
		}
	}

	r = svm_migrate_copy_memory_gart(adev, src + i - j, dst + i - j, j,
					 FROM_RAM_TO_VRAM, mfence);

out_free_vram_pages:
	if (r) {
		pr_debug("failed %d to copy memory to vram\n", r);
		for (i = 0; i < npages && mpages; i++) {
			if (!dst[i])
				continue;
			svm_migrate_put_vram_page(adev, dst[i]);
			migrate->dst[i] = 0;
			mpages--;
		}
	}

#ifdef DEBUG_FORCE_MIXED_DOMAINS
	for (i = 0, j = 0; i < npages; i += 4, j++) {
		if (j & 1)
			continue;
		svm_migrate_put_vram_page(adev, dst[i]);
		migrate->dst[i] = 0;
		svm_migrate_put_vram_page(adev, dst[i + 1]);
		migrate->dst[i + 1] = 0;
		svm_migrate_put_vram_page(adev, dst[i + 2]);
		migrate->dst[i + 2] = 0;
		svm_migrate_put_vram_page(adev, dst[i + 3]);
		migrate->dst[i + 3] = 0;
	}
#endif

	return r;
}

static long
svm_migrate_vma_to_vram(struct kfd_node *node, struct svm_range *prange,
			struct vm_area_struct *vma, uint64_t start,
			uint64_t end, uint32_t trigger, uint64_t ttm_res_offset)
{
	struct kfd_process *p = container_of(prange->svms, struct kfd_process, svms);
	uint64_t npages = (end - start) >> PAGE_SHIFT;
	struct amdgpu_device *adev = node->adev;
	struct kfd_process_device *pdd;
	struct dma_fence *mfence = NULL;
	struct migrate_vma migrate = { 0 };
	unsigned long cpages = 0;
	unsigned long mpages = 0;
	dma_addr_t *scratch;
	void *buf;
	int r = -ENOMEM;

	memset(&migrate, 0, sizeof(migrate));
	migrate.vma = vma;
	migrate.start = start;
	migrate.end = end;
	migrate.flags = MIGRATE_VMA_SELECT_SYSTEM;
	migrate.pgmap_owner = SVM_ADEV_PGMAP_OWNER(adev);

	buf = kvcalloc(npages,
		       2 * sizeof(*migrate.src) + sizeof(uint64_t) + sizeof(dma_addr_t),
		       GFP_KERNEL);
	if (!buf)
		goto out;

	migrate.src = buf;
	migrate.dst = migrate.src + npages;
	scratch = (dma_addr_t *)(migrate.dst + npages);

	kfd_smi_event_migration_start(node, p->lead_thread->pid,
				      start >> PAGE_SHIFT, end >> PAGE_SHIFT,
				      0, node->id, prange->prefetch_loc,
				      prange->preferred_loc, trigger);

	r = migrate_vma_setup(&migrate);
	if (r) {
		dev_err(adev->dev, "%s: vma setup fail %d range [0x%lx 0x%lx]\n",
			__func__, r, prange->start, prange->last);
		goto out_free;
	}

	cpages = migrate.cpages;
	if (!cpages) {
		pr_debug("failed collect migrate sys pages [0x%lx 0x%lx]\n",
			 prange->start, prange->last);
		goto out_free;
	}
	if (cpages != npages)
		pr_debug("partial migration, 0x%lx/0x%llx pages collected\n",
			 cpages, npages);
	else
		pr_debug("0x%lx pages collected\n", cpages);

	r = svm_migrate_copy_to_vram(node, prange, &migrate, &mfence, scratch, ttm_res_offset);
	migrate_vma_pages(&migrate);

	svm_migrate_copy_done(adev, mfence);
	migrate_vma_finalize(&migrate);

	mpages = cpages - svm_migrate_unsuccessful_pages(&migrate);
	pr_debug("successful/cpages/npages 0x%lx/0x%lx/0x%lx\n",
			 mpages, cpages, migrate.npages);

	svm_range_dma_unmap_dev(adev->dev, scratch, 0, npages);

out_free:
	kvfree(buf);
	kfd_smi_event_migration_end(node, p->lead_thread->pid,
				    start >> PAGE_SHIFT, end >> PAGE_SHIFT,
				    0, node->id, trigger, r);
out:
	if (!r && mpages) {
		pdd = svm_range_get_pdd_by_node(prange, node);
		if (pdd)
			WRITE_ONCE(pdd->page_in, pdd->page_in + mpages);

		return mpages;
	}
	return r;
}

/**
 * svm_migrate_ram_to_vram - migrate svm range from system to device
 * @prange: range structure
 * @best_loc: the device to migrate to
 * @start_mgr: start page to migrate
 * @last_mgr: last page to migrate
 * @mm: the process mm structure
 * @trigger: reason of migration
 *
 * Context: Process context, caller hold mmap read lock, svms lock, prange lock
 *
 * Return:
 * 0 - OK, otherwise error code
 */
static int
svm_migrate_ram_to_vram(struct svm_range *prange, uint32_t best_loc,
			unsigned long start_mgr, unsigned long last_mgr,
			struct mm_struct *mm, uint32_t trigger)
{
	unsigned long addr, start, end;
	struct vm_area_struct *vma;
	uint64_t ttm_res_offset;
	struct kfd_node *node;
	unsigned long mpages = 0;
	long r = 0;

	if (start_mgr < prange->start || last_mgr > prange->last) {
		pr_debug("range [0x%lx 0x%lx] out prange [0x%lx 0x%lx]\n",
			 start_mgr, last_mgr, prange->start, prange->last);
		return -EFAULT;
	}

	node = svm_range_get_node_by_id(prange, best_loc);
	if (!node) {
		pr_debug("failed to get kfd node by id 0x%x\n", best_loc);
		return -ENODEV;
	}

	pr_debug("svms 0x%p [0x%lx 0x%lx] in [0x%lx 0x%lx] to gpu 0x%x\n",
		prange->svms, start_mgr, last_mgr, prange->start, prange->last,
		best_loc);

	start = start_mgr << PAGE_SHIFT;
	end = (last_mgr + 1) << PAGE_SHIFT;

	r = amdgpu_amdkfd_reserve_mem_limit(node->adev,
					prange->npages * PAGE_SIZE,
					KFD_IOC_ALLOC_MEM_FLAGS_VRAM,
					node->xcp ? node->xcp->id : 0);
	if (r) {
		dev_dbg(node->adev->dev, "failed to reserve VRAM, r: %ld\n", r);
		return -ENOSPC;
	}

	r = svm_range_vram_node_new(node, prange, true);
	if (r) {
		dev_dbg(node->adev->dev, "fail %ld to alloc vram\n", r);
		goto out;
	}
	ttm_res_offset = (start_mgr - prange->start + prange->offset) << PAGE_SHIFT;

	for (addr = start; addr < end;) {
		unsigned long next;

		vma = vma_lookup(mm, addr);
		if (!vma)
			break;

		next = min(vma->vm_end, end);
		r = svm_migrate_vma_to_vram(node, prange, vma, addr, next, trigger, ttm_res_offset);
		if (r < 0) {
			pr_debug("failed %ld to migrate\n", r);
			break;
		} else {
			mpages += r;
		}
		ttm_res_offset += next - addr;
		addr = next;
	}

	if (mpages) {
		prange->actual_loc = best_loc;
		prange->vram_pages += mpages;
	} else if (!prange->actual_loc) {
		/* if no page migrated and all pages from prange are at
		 * sys ram drop svm_bo got from svm_range_vram_node_new
		 */
		svm_range_vram_node_free(prange);
	}

out:
	amdgpu_amdkfd_unreserve_mem_limit(node->adev,
					prange->npages * PAGE_SIZE,
					KFD_IOC_ALLOC_MEM_FLAGS_VRAM,
					node->xcp ? node->xcp->id : 0);
	return r < 0 ? r : 0;
}

static void svm_migrate_page_free(struct page *page)
{
	struct svm_range_bo *svm_bo = page->zone_device_data;

	if (svm_bo) {
		pr_debug_ratelimited("ref: %d\n", kref_read(&svm_bo->kref));
		svm_range_bo_unref_async(svm_bo);
	}
}

static int
svm_migrate_copy_to_ram(struct amdgpu_device *adev, struct svm_range *prange,
			struct migrate_vma *migrate, struct dma_fence **mfence,
			dma_addr_t *scratch, uint64_t npages)
{
	struct device *dev = adev->dev;
	uint64_t *src;
	dma_addr_t *dst;
	struct page *dpage;
	uint64_t i = 0, j;
	uint64_t addr;
	int r = 0;

	pr_debug("svms 0x%p [0x%lx 0x%lx]\n", prange->svms, prange->start,
		 prange->last);

	addr = migrate->start;

	src = (uint64_t *)(scratch + npages);
	dst = scratch;

	for (i = 0, j = 0; i < npages; i++, addr += PAGE_SIZE) {
		struct page *spage;

		spage = migrate_pfn_to_page(migrate->src[i]);
		if (!spage || !is_zone_device_page(spage)) {
			pr_debug("invalid page. Could be in CPU already svms 0x%p [0x%lx 0x%lx]\n",
				 prange->svms, prange->start, prange->last);
			if (j) {
				r = svm_migrate_copy_memory_gart(adev, dst + i - j,
								 src + i - j, j,
								 FROM_VRAM_TO_RAM,
								 mfence);
				if (r)
					goto out_oom;
				j = 0;
			}
			continue;
		}
		src[i] = svm_migrate_addr(adev, spage);
		if (j > 0 && src[i] != src[i - 1] + PAGE_SIZE) {
			r = svm_migrate_copy_memory_gart(adev, dst + i - j,
							 src + i - j, j,
							 FROM_VRAM_TO_RAM,
							 mfence);
			if (r)
				goto out_oom;
			j = 0;
		}

		dpage = svm_migrate_get_sys_page(migrate->vma, addr);
		if (!dpage) {
			pr_debug("failed get page svms 0x%p [0x%lx 0x%lx]\n",
				 prange->svms, prange->start, prange->last);
			r = -ENOMEM;
			goto out_oom;
		}

		dst[i] = dma_map_page(dev, dpage, 0, PAGE_SIZE, DMA_BIDIRECTIONAL);
		r = dma_mapping_error(dev, dst[i]);
		if (r) {
			dev_err(adev->dev, "%s: fail %d dma_map_page\n", __func__, r);
			goto out_oom;
		}

		pr_debug_ratelimited("dma mapping dst to 0x%llx, pfn 0x%lx\n",
				     dst[i] >> PAGE_SHIFT, page_to_pfn(dpage));

		migrate->dst[i] = migrate_pfn(page_to_pfn(dpage));
		j++;
	}

	r = svm_migrate_copy_memory_gart(adev, dst + i - j, src + i - j, j,
					 FROM_VRAM_TO_RAM, mfence);

out_oom:
	if (r) {
		pr_debug("failed %d copy to ram\n", r);
		while (i--) {
			svm_migrate_put_sys_page(dst[i]);
			migrate->dst[i] = 0;
		}
	}

	return r;
}

/**
 * svm_migrate_vma_to_ram - migrate range inside one vma from device to system
 *
 * @prange: svm range structure
 * @vma: vm_area_struct that range [start, end] belongs to
 * @start: range start virtual address in pages
 * @end: range end virtual address in pages
 * @node: kfd node device to migrate from
 * @trigger: reason of migration
 * @fault_page: is from vmf->page, svm_migrate_to_ram(), this is CPU page fault callback
 *
 * Context: Process context, caller hold mmap read lock, prange->migrate_mutex
 *
 * Return:
 *   negative values - indicate error
 *   positive values or zero - number of pages got migrated
 */
static long
svm_migrate_vma_to_ram(struct kfd_node *node, struct svm_range *prange,
		       struct vm_area_struct *vma, uint64_t start, uint64_t end,
		       uint32_t trigger, struct page *fault_page)
{
	struct kfd_process *p = container_of(prange->svms, struct kfd_process, svms);
	uint64_t npages = (end - start) >> PAGE_SHIFT;
	unsigned long upages = npages;
	unsigned long cpages = 0;
	unsigned long mpages = 0;
	struct amdgpu_device *adev = node->adev;
	struct kfd_process_device *pdd;
	struct dma_fence *mfence = NULL;
	struct migrate_vma migrate = { 0 };
	dma_addr_t *scratch;
	void *buf;
	int r = -ENOMEM;

	memset(&migrate, 0, sizeof(migrate));
	migrate.vma = vma;
	migrate.start = start;
	migrate.end = end;
	migrate.pgmap_owner = SVM_ADEV_PGMAP_OWNER(adev);
	if (adev->gmc.xgmi.connected_to_cpu)
		migrate.flags = MIGRATE_VMA_SELECT_DEVICE_COHERENT;
	else
		migrate.flags = MIGRATE_VMA_SELECT_DEVICE_PRIVATE;

	buf = kvcalloc(npages,
		       2 * sizeof(*migrate.src) + sizeof(uint64_t) + sizeof(dma_addr_t),
		       GFP_KERNEL);
	if (!buf)
		goto out;

	migrate.src = buf;
	migrate.dst = migrate.src + npages;
	migrate.fault_page = fault_page;
	scratch = (dma_addr_t *)(migrate.dst + npages);

	kfd_smi_event_migration_start(node, p->lead_thread->pid,
				      start >> PAGE_SHIFT, end >> PAGE_SHIFT,
				      node->id, 0, prange->prefetch_loc,
				      prange->preferred_loc, trigger);

	r = migrate_vma_setup(&migrate);
	if (r) {
		dev_err(adev->dev, "%s: vma setup fail %d range [0x%lx 0x%lx]\n",
			__func__, r, prange->start, prange->last);
		goto out_free;
	}

	cpages = migrate.cpages;
	if (!cpages) {
		pr_debug("failed collect migrate device pages [0x%lx 0x%lx]\n",
			 prange->start, prange->last);
		upages = svm_migrate_unsuccessful_pages(&migrate);
		goto out_free;
	}
	if (cpages != npages)
		pr_debug("partial migration, 0x%lx/0x%llx pages collected\n",
			 cpages, npages);
	else
		pr_debug("0x%lx pages collected\n", cpages);

	r = svm_migrate_copy_to_ram(adev, prange, &migrate, &mfence,
				    scratch, npages);
	migrate_vma_pages(&migrate);

	upages = svm_migrate_unsuccessful_pages(&migrate);
	pr_debug("unsuccessful/cpages/npages 0x%lx/0x%lx/0x%lx\n",
		 upages, cpages, migrate.npages);

	svm_migrate_copy_done(adev, mfence);
	migrate_vma_finalize(&migrate);

	svm_range_dma_unmap_dev(adev->dev, scratch, 0, npages);

out_free:
	kvfree(buf);
	kfd_smi_event_migration_end(node, p->lead_thread->pid,
				    start >> PAGE_SHIFT, end >> PAGE_SHIFT,
				    node->id, 0, trigger, r);
out:
	if (!r && cpages) {
		mpages = cpages - upages;
		pdd = svm_range_get_pdd_by_node(prange, node);
		if (pdd)
			WRITE_ONCE(pdd->page_out, pdd->page_out + mpages);
	}

	return r ? r : mpages;
}

/**
 * svm_migrate_vram_to_ram - migrate svm range from device to system
 * @prange: range structure
 * @mm: process mm, use current->mm if NULL
 * @start_mgr: start page need be migrated to sys ram
 * @last_mgr: last page need be migrated to sys ram
 * @trigger: reason of migration
 * @fault_page: is from vmf->page, svm_migrate_to_ram(), this is CPU page fault callback
 *
 * Context: Process context, caller hold mmap read lock, prange->migrate_mutex
 *
 * Return:
 * 0 - OK, otherwise error code
 */
int svm_migrate_vram_to_ram(struct svm_range *prange, struct mm_struct *mm,
			    unsigned long start_mgr, unsigned long last_mgr,
			    uint32_t trigger, struct page *fault_page)
{
	struct kfd_node *node;
	struct vm_area_struct *vma;
	unsigned long addr;
	unsigned long start;
	unsigned long end;
	unsigned long mpages = 0;
	long r = 0;

	/* this pragne has no any vram page to migrate to sys ram */
	if (!prange->actual_loc) {
		pr_debug("[0x%lx 0x%lx] already migrated to ram\n",
			 prange->start, prange->last);
		return 0;
	}

	if (start_mgr < prange->start || last_mgr > prange->last) {
		pr_debug("range [0x%lx 0x%lx] out prange [0x%lx 0x%lx]\n",
			 start_mgr, last_mgr, prange->start, prange->last);
		return -EFAULT;
	}

	node = svm_range_get_node_by_id(prange, prange->actual_loc);
	if (!node) {
		pr_debug("failed to get kfd node by id 0x%x\n", prange->actual_loc);
		return -ENODEV;
	}
	pr_debug("svms 0x%p prange 0x%p [0x%lx 0x%lx] from gpu 0x%x to ram\n",
		 prange->svms, prange, start_mgr, last_mgr,
		 prange->actual_loc);

	start = start_mgr << PAGE_SHIFT;
	end = (last_mgr + 1) << PAGE_SHIFT;

	for (addr = start; addr < end;) {
		unsigned long next;

		vma = vma_lookup(mm, addr);
		if (!vma) {
			pr_debug("failed to find vma for prange %p\n", prange);
			r = -EFAULT;
			break;
		}

		next = min(vma->vm_end, end);
		r = svm_migrate_vma_to_ram(node, prange, vma, addr, next, trigger,
			fault_page);
		if (r < 0) {
			pr_debug("failed %ld to migrate prange %p\n", r, prange);
			break;
		} else {
			mpages += r;
		}
		addr = next;
	}

	if (r >= 0) {
		prange->vram_pages -= mpages;

		/* prange does not have vram page set its actual_loc to system
		 * and drop its svm_bo ref
		 */
		if (prange->vram_pages == 0 && prange->ttm_res) {
			prange->actual_loc = 0;
			svm_range_vram_node_free(prange);
		}
	}

	return r < 0 ? r : 0;
}

/**
 * svm_migrate_vram_to_vram - migrate svm range from device to device
 * @prange: range structure
 * @best_loc: the device to migrate to
 * @start: start page need be migrated to sys ram
 * @last: last page need be migrated to sys ram
 * @mm: process mm, use current->mm if NULL
 * @trigger: reason of migration
 *
 * Context: Process context, caller hold mmap read lock, svms lock, prange lock
 *
 * migrate all vram pages in prange to sys ram, then migrate
 * [start, last] pages from sys ram to gpu node best_loc.
 *
 * Return:
 * 0 - OK, otherwise error code
 */
static int
svm_migrate_vram_to_vram(struct svm_range *prange, uint32_t best_loc,
			unsigned long start, unsigned long last,
			struct mm_struct *mm, uint32_t trigger)
{
	int r, retries = 3;

	/*
	 * TODO: for both devices with PCIe large bar or on same xgmi hive, skip
	 * system memory as migration bridge
	 */

	pr_debug("from gpu 0x%x to gpu 0x%x\n", prange->actual_loc, best_loc);

	do {
		r = svm_migrate_vram_to_ram(prange, mm, prange->start, prange->last,
					    trigger, NULL);
		if (r)
			return r;
	} while (prange->actual_loc && --retries);

	if (prange->actual_loc)
		return -EDEADLK;

	return svm_migrate_ram_to_vram(prange, best_loc, start, last, mm, trigger);
}

int
svm_migrate_to_vram(struct svm_range *prange, uint32_t best_loc,
		    unsigned long start, unsigned long last,
		    struct mm_struct *mm, uint32_t trigger)
{
	if  (!prange->actual_loc || prange->actual_loc == best_loc)
		return svm_migrate_ram_to_vram(prange, best_loc, start, last,
					       mm, trigger);

	else
		return svm_migrate_vram_to_vram(prange, best_loc, start, last,
						mm, trigger);

}

/**
 * svm_migrate_to_ram - CPU page fault handler
 * @vmf: CPU vm fault vma, address
 *
 * Context: vm fault handler, caller holds the mmap read lock
 *
 * Return:
 * 0 - OK
 * VM_FAULT_SIGBUS - notice application to have SIGBUS page fault
 */
static vm_fault_t svm_migrate_to_ram(struct vm_fault *vmf)
{
	unsigned long start, last, size;
	unsigned long addr = vmf->address;
	struct svm_range_bo *svm_bo;
	struct svm_range *prange;
	struct kfd_process *p;
	struct mm_struct *mm;
	int r = 0;

	svm_bo = vmf->page->zone_device_data;
	if (!svm_bo) {
		pr_debug("failed get device page at addr 0x%lx\n", addr);
		return VM_FAULT_SIGBUS;
	}
	if (!mmget_not_zero(svm_bo->eviction_fence->mm)) {
		pr_debug("addr 0x%lx of process mm is destroyed\n", addr);
		return VM_FAULT_SIGBUS;
	}

	mm = svm_bo->eviction_fence->mm;
	if (mm != vmf->vma->vm_mm)
		pr_debug("addr 0x%lx is COW mapping in child process\n", addr);

	p = kfd_lookup_process_by_mm(mm);
	if (!p) {
		pr_debug("failed find process at fault address 0x%lx\n", addr);
		r = VM_FAULT_SIGBUS;
		goto out_mmput;
	}
	if (READ_ONCE(p->svms.faulting_task) == current) {
		pr_debug("skipping ram migration\n");
		r = 0;
		goto out_unref_process;
	}

	pr_debug("CPU page fault svms 0x%p address 0x%lx\n", &p->svms, addr);
	addr >>= PAGE_SHIFT;

	mutex_lock(&p->svms.lock);

	prange = svm_range_from_addr(&p->svms, addr, NULL);
	if (!prange) {
		pr_debug("failed get range svms 0x%p addr 0x%lx\n", &p->svms, addr);
		r = -EFAULT;
		goto out_unlock_svms;
	}

	mutex_lock(&prange->migrate_mutex);

	if (!prange->actual_loc)
		goto out_unlock_prange;

	/* Align migration range start and size to granularity size */
	size = 1UL << prange->granularity;
	start = max(ALIGN_DOWN(addr, size), prange->start);
	last = min(ALIGN(addr + 1, size) - 1, prange->last);

	r = svm_migrate_vram_to_ram(prange, vmf->vma->vm_mm, start, last,
				    KFD_MIGRATE_TRIGGER_PAGEFAULT_CPU, vmf->page);
	if (r)
		pr_debug("failed %d migrate svms 0x%p range 0x%p [0x%lx 0x%lx]\n",
			r, prange->svms, prange, start, last);

out_unlock_prange:
	mutex_unlock(&prange->migrate_mutex);
out_unlock_svms:
	mutex_unlock(&p->svms.lock);
out_unref_process:
	pr_debug("CPU fault svms 0x%p address 0x%lx done\n", &p->svms, addr);
	kfd_unref_process(p);
out_mmput:
	mmput(mm);
	return r ? VM_FAULT_SIGBUS : 0;
}

static const struct dev_pagemap_ops svm_migrate_pgmap_ops = {
	.page_free		= svm_migrate_page_free,
	.migrate_to_ram		= svm_migrate_to_ram,
};

/* Each VRAM page uses sizeof(struct page) on system memory */
#define SVM_HMM_PAGE_STRUCT_SIZE(size) ((size)/PAGE_SIZE * sizeof(struct page))

int kgd2kfd_init_zone_device(struct amdgpu_device *adev)
{
	struct amdgpu_kfd_dev *kfddev = &adev->kfd;
	struct dev_pagemap *pgmap;
	struct resource *res = NULL;
	unsigned long size;
	void *r;

	/* Page migration works on gfx9 or newer */
	if (amdgpu_ip_version(adev, GC_HWIP, 0) < IP_VERSION(9, 0, 1))
		return -EINVAL;

	if (adev->apu_prefer_gtt)
		return 0;

	pgmap = &kfddev->pgmap;
	memset(pgmap, 0, sizeof(*pgmap));

	/* TODO: register all vram to HMM for now.
	 * should remove reserved size
	 */
	size = ALIGN(adev->gmc.real_vram_size, 2ULL << 20);
	if (adev->gmc.xgmi.connected_to_cpu) {
		pgmap->range.start = adev->gmc.aper_base;
		pgmap->range.end = adev->gmc.aper_base + adev->gmc.aper_size - 1;
		pgmap->type = MEMORY_DEVICE_COHERENT;
	} else {
		res = devm_request_free_mem_region(adev->dev, &iomem_resource, size);
		if (IS_ERR(res))
			return PTR_ERR(res);
		pgmap->range.start = res->start;
		pgmap->range.end = res->end;
		pgmap->type = MEMORY_DEVICE_PRIVATE;
	}

	pgmap->nr_range = 1;
	pgmap->ops = &svm_migrate_pgmap_ops;
	pgmap->owner = SVM_ADEV_PGMAP_OWNER(adev);
	pgmap->flags = 0;
	/* Device manager releases device-specific resources, memory region and
	 * pgmap when driver disconnects from device.
	 */
	r = devm_memremap_pages(adev->dev, pgmap);
	if (IS_ERR(r)) {
		pr_err("failed to register HMM device memory\n");
		if (pgmap->type == MEMORY_DEVICE_PRIVATE)
			devm_release_mem_region(adev->dev, res->start, resource_size(res));
		/* Disable SVM support capability */
		pgmap->type = 0;
		return PTR_ERR(r);
	}

	pr_debug("reserve %ldMB system memory for VRAM pages struct\n",
		 SVM_HMM_PAGE_STRUCT_SIZE(size) >> 20);

	amdgpu_amdkfd_reserve_system_mem(SVM_HMM_PAGE_STRUCT_SIZE(size));

	pr_info("HMM registered %ldMB device memory\n", size >> 20);

	return 0;
}
