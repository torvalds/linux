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
#include "amdgpu_sync.h"
#include "amdgpu_object.h"
#include "amdgpu_vm.h"
#include "amdgpu_mn.h"
#include "amdgpu_res_cursor.h"
#include "kfd_priv.h"
#include "kfd_svm.h"
#include "kfd_migrate.h"

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

	r = amdgpu_job_alloc_with_ib(adev, num_dw * 4 + num_bytes,
				     AMDGPU_IB_POOL_DELAYED, &job);
	if (r)
		return r;

	src_addr = num_dw * 4;
	src_addr += job->ibs[0].gpu_addr;

	dst_addr = amdgpu_bo_gpu_offset(adev->gart.bo);
	amdgpu_emit_copy_buffer(adev, &job->ibs[0], src_addr,
				dst_addr, num_bytes, false);

	amdgpu_ring_pad_ib(ring, &job->ibs[0]);
	WARN_ON(job->ibs[0].length_dw > num_dw);

	pte_flags = AMDGPU_PTE_VALID | AMDGPU_PTE_READABLE;
	pte_flags |= AMDGPU_PTE_SYSTEM | AMDGPU_PTE_SNOOPED;
	if (!(flags & KFD_IOCTL_SVM_FLAG_GPU_RO))
		pte_flags |= AMDGPU_PTE_WRITEABLE;
	pte_flags |= adev->gart.gart_pte_flags;

	cpu_addr = &job->ibs[0].ptr[num_dw];

	r = amdgpu_gart_map(adev, 0, npages, addr, pte_flags, cpu_addr);
	if (r)
		goto error_free;

	r = amdgpu_job_submit(job, &adev->mman.entity,
			      AMDGPU_FENCE_OWNER_UNDEFINED, &fence);
	if (r)
		goto error_free;

	dma_fence_put(fence);

	return r;

error_free:
	amdgpu_job_free(job);
	return r;
}

/**
 * svm_migrate_copy_memory_gart - sdma copy data between ram and vram
 *
 * @adev: amdgpu device the sdma ring running
 * @src: source page address array
 * @dst: destination page address array
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
			pr_debug("failed %d to create gart mapping\n", r);
			goto out_unlock;
		}

		r = amdgpu_copy_buffer(ring, gart_s, gart_d, size * PAGE_SIZE,
				       NULL, &next, false, true, false);
		if (r) {
			pr_debug("failed %d to copy memory\n", r);
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
	return (addr + adev->kfd.dev->pgmap.range.start) >> PAGE_SHIFT;
}

static void
svm_migrate_get_vram_page(struct svm_range *prange, unsigned long pfn)
{
	struct page *page;

	page = pfn_to_page(pfn);
	svm_range_bo_ref(prange->svm_bo);
	page->zone_device_data = prange->svm_bo;
	get_page(page);
	lock_page(page);
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
	return (addr - adev->kfd.dev->pgmap.range.start);
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

static int
svm_migrate_copy_to_vram(struct amdgpu_device *adev, struct svm_range *prange,
			 struct migrate_vma *migrate, struct dma_fence **mfence,
			 dma_addr_t *scratch)
{
	uint64_t npages = migrate->cpages;
	struct device *dev = adev->dev;
	struct amdgpu_res_cursor cursor;
	dma_addr_t *src;
	uint64_t *dst;
	uint64_t i, j;
	int r;

	pr_debug("svms 0x%p [0x%lx 0x%lx]\n", prange->svms, prange->start,
		 prange->last);

	src = scratch;
	dst = (uint64_t *)(scratch + npages);

	r = svm_range_vram_node_new(adev, prange, true);
	if (r) {
		pr_debug("failed %d get 0x%llx pages from vram\n", r, npages);
		goto out;
	}

	amdgpu_res_first(prange->ttm_res, prange->offset << PAGE_SHIFT,
			 npages << PAGE_SHIFT, &cursor);
	for (i = j = 0; i < npages; i++) {
		struct page *spage;

		spage = migrate_pfn_to_page(migrate->src[i]);
		if (spage && !is_zone_device_page(spage)) {
			dst[i] = cursor.start + (j << PAGE_SHIFT);
			migrate->dst[i] = svm_migrate_addr_to_pfn(adev, dst[i]);
			svm_migrate_get_vram_page(prange, migrate->dst[i]);
			migrate->dst[i] = migrate_pfn(migrate->dst[i]);
			migrate->dst[i] |= MIGRATE_PFN_LOCKED;
			src[i] = dma_map_page(dev, spage, 0, PAGE_SIZE,
					      DMA_TO_DEVICE);
			r = dma_mapping_error(dev, src[i]);
			if (r) {
				pr_debug("failed %d dma_map_page\n", r);
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
				amdgpu_res_next(&cursor, j << PAGE_SHIFT);
				j = 0;
			} else {
				amdgpu_res_next(&cursor, PAGE_SIZE);
			}
			continue;
		}

		pr_debug("dma mapping src to 0x%llx, page_to_pfn 0x%lx\n",
			 src[i] >> PAGE_SHIFT, page_to_pfn(spage));

		if (j >= (cursor.size >> PAGE_SHIFT) - 1 && i < npages - 1) {
			r = svm_migrate_copy_memory_gart(adev, src + i - j,
							 dst + i - j, j + 1,
							 FROM_RAM_TO_VRAM,
							 mfence);
			if (r)
				goto out_free_vram_pages;
			amdgpu_res_next(&cursor, (j + 1) * PAGE_SIZE);
			j= 0;
		} else {
			j++;
		}
	}

	r = svm_migrate_copy_memory_gart(adev, src + i - j, dst + i - j, j,
					 FROM_RAM_TO_VRAM, mfence);

out_free_vram_pages:
	if (r) {
		pr_debug("failed %d to copy memory to vram\n", r);
		while (i--) {
			svm_migrate_put_vram_page(adev, dst[i]);
			migrate->dst[i] = 0;
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
out:
	return r;
}

static int
svm_migrate_vma_to_vram(struct amdgpu_device *adev, struct svm_range *prange,
			struct vm_area_struct *vma, uint64_t start,
			uint64_t end)
{
	uint64_t npages = (end - start) >> PAGE_SHIFT;
	struct kfd_process_device *pdd;
	struct dma_fence *mfence = NULL;
	struct migrate_vma migrate;
	dma_addr_t *scratch;
	size_t size;
	void *buf;
	int r = -ENOMEM;

	memset(&migrate, 0, sizeof(migrate));
	migrate.vma = vma;
	migrate.start = start;
	migrate.end = end;
	migrate.flags = MIGRATE_VMA_SELECT_SYSTEM;
	migrate.pgmap_owner = SVM_ADEV_PGMAP_OWNER(adev);

	size = 2 * sizeof(*migrate.src) + sizeof(uint64_t) + sizeof(dma_addr_t);
	size *= npages;
	buf = kvmalloc(size, GFP_KERNEL | __GFP_ZERO);
	if (!buf)
		goto out;

	migrate.src = buf;
	migrate.dst = migrate.src + npages;
	scratch = (dma_addr_t *)(migrate.dst + npages);

	r = migrate_vma_setup(&migrate);
	if (r) {
		pr_debug("failed %d prepare migrate svms 0x%p [0x%lx 0x%lx]\n",
			 r, prange->svms, prange->start, prange->last);
		goto out_free;
	}
	if (migrate.cpages != npages) {
		pr_debug("Partial migration. 0x%lx/0x%llx pages can be migrated\n",
			 migrate.cpages,
			 npages);
	}

	if (migrate.cpages) {
		r = svm_migrate_copy_to_vram(adev, prange, &migrate, &mfence,
					     scratch);
		migrate_vma_pages(&migrate);
		svm_migrate_copy_done(adev, mfence);
		migrate_vma_finalize(&migrate);
	}

	svm_range_dma_unmap(adev->dev, scratch, 0, npages);
	svm_range_free_dma_mappings(prange);

out_free:
	kvfree(buf);
out:
	if (!r) {
		pdd = svm_range_get_pdd_by_adev(prange, adev);
		if (pdd)
			WRITE_ONCE(pdd->page_in, pdd->page_in + migrate.cpages);
	}

	return r;
}

/**
 * svm_migrate_ram_to_vram - migrate svm range from system to device
 * @prange: range structure
 * @best_loc: the device to migrate to
 * @mm: the process mm structure
 *
 * Context: Process context, caller hold mmap read lock, svms lock, prange lock
 *
 * Return:
 * 0 - OK, otherwise error code
 */
static int
svm_migrate_ram_to_vram(struct svm_range *prange, uint32_t best_loc,
			struct mm_struct *mm)
{
	unsigned long addr, start, end;
	struct vm_area_struct *vma;
	struct amdgpu_device *adev;
	int r = 0;

	if (prange->actual_loc == best_loc) {
		pr_debug("svms 0x%p [0x%lx 0x%lx] already on best_loc 0x%x\n",
			 prange->svms, prange->start, prange->last, best_loc);
		return 0;
	}

	adev = svm_range_get_adev_by_id(prange, best_loc);
	if (!adev) {
		pr_debug("failed to get device by id 0x%x\n", best_loc);
		return -ENODEV;
	}

	pr_debug("svms 0x%p [0x%lx 0x%lx] to gpu 0x%x\n", prange->svms,
		 prange->start, prange->last, best_loc);

	/* FIXME: workaround for page locking bug with invalid pages */
	svm_range_prefault(prange, mm, SVM_ADEV_PGMAP_OWNER(adev));

	start = prange->start << PAGE_SHIFT;
	end = (prange->last + 1) << PAGE_SHIFT;

	for (addr = start; addr < end;) {
		unsigned long next;

		vma = find_vma(mm, addr);
		if (!vma || addr < vma->vm_start)
			break;

		next = min(vma->vm_end, end);
		r = svm_migrate_vma_to_vram(adev, prange, vma, addr, next);
		if (r) {
			pr_debug("failed to migrate\n");
			break;
		}
		addr = next;
	}

	if (!r)
		prange->actual_loc = best_loc;

	return r;
}

static void svm_migrate_page_free(struct page *page)
{
	struct svm_range_bo *svm_bo = page->zone_device_data;

	if (svm_bo) {
		pr_debug("svm_bo ref left: %d\n", kref_read(&svm_bo->kref));
		svm_range_bo_unref(svm_bo);
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

	addr = prange->start << PAGE_SHIFT;

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
		if (i > 0 && src[i] != src[i - 1] + PAGE_SIZE) {
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

		dst[i] = dma_map_page(dev, dpage, 0, PAGE_SIZE, DMA_FROM_DEVICE);
		r = dma_mapping_error(dev, dst[i]);
		if (r) {
			pr_debug("failed %d dma_map_page\n", r);
			goto out_oom;
		}

		pr_debug("dma mapping dst to 0x%llx, page_to_pfn 0x%lx\n",
			      dst[i] >> PAGE_SHIFT, page_to_pfn(dpage));

		migrate->dst[i] = migrate_pfn(page_to_pfn(dpage));
		migrate->dst[i] |= MIGRATE_PFN_LOCKED;
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

static int
svm_migrate_vma_to_ram(struct amdgpu_device *adev, struct svm_range *prange,
		       struct vm_area_struct *vma, uint64_t start, uint64_t end)
{
	uint64_t npages = (end - start) >> PAGE_SHIFT;
	struct kfd_process_device *pdd;
	struct dma_fence *mfence = NULL;
	struct migrate_vma migrate;
	dma_addr_t *scratch;
	size_t size;
	void *buf;
	int r = -ENOMEM;

	memset(&migrate, 0, sizeof(migrate));
	migrate.vma = vma;
	migrate.start = start;
	migrate.end = end;
	migrate.flags = MIGRATE_VMA_SELECT_DEVICE_PRIVATE;
	migrate.pgmap_owner = SVM_ADEV_PGMAP_OWNER(adev);

	size = 2 * sizeof(*migrate.src) + sizeof(uint64_t) + sizeof(dma_addr_t);
	size *= npages;
	buf = kvmalloc(size, GFP_KERNEL | __GFP_ZERO);
	if (!buf)
		goto out;

	migrate.src = buf;
	migrate.dst = migrate.src + npages;
	scratch = (dma_addr_t *)(migrate.dst + npages);

	r = migrate_vma_setup(&migrate);
	if (r) {
		pr_debug("failed %d prepare migrate svms 0x%p [0x%lx 0x%lx]\n",
			 r, prange->svms, prange->start, prange->last);
		goto out_free;
	}

	pr_debug("cpages %ld\n", migrate.cpages);

	if (migrate.cpages) {
		r = svm_migrate_copy_to_ram(adev, prange, &migrate, &mfence,
					    scratch, npages);
		migrate_vma_pages(&migrate);
		svm_migrate_copy_done(adev, mfence);
		migrate_vma_finalize(&migrate);
	} else {
		pr_debug("failed collect migrate device pages [0x%lx 0x%lx]\n",
			 prange->start, prange->last);
	}

	svm_range_dma_unmap(adev->dev, scratch, 0, npages);

out_free:
	kvfree(buf);
out:
	if (!r) {
		pdd = svm_range_get_pdd_by_adev(prange, adev);
		if (pdd)
			WRITE_ONCE(pdd->page_out,
				   pdd->page_out + migrate.cpages);
	}
	return r;
}

/**
 * svm_migrate_vram_to_ram - migrate svm range from device to system
 * @prange: range structure
 * @mm: process mm, use current->mm if NULL
 *
 * Context: Process context, caller hold mmap read lock, svms lock, prange lock
 *
 * Return:
 * 0 - OK, otherwise error code
 */
int svm_migrate_vram_to_ram(struct svm_range *prange, struct mm_struct *mm)
{
	struct amdgpu_device *adev;
	struct vm_area_struct *vma;
	unsigned long addr;
	unsigned long start;
	unsigned long end;
	int r = 0;

	if (!prange->actual_loc) {
		pr_debug("[0x%lx 0x%lx] already migrated to ram\n",
			 prange->start, prange->last);
		return 0;
	}

	adev = svm_range_get_adev_by_id(prange, prange->actual_loc);
	if (!adev) {
		pr_debug("failed to get device by id 0x%x\n",
			 prange->actual_loc);
		return -ENODEV;
	}

	pr_debug("svms 0x%p prange 0x%p [0x%lx 0x%lx] from gpu 0x%x to ram\n",
		 prange->svms, prange, prange->start, prange->last,
		 prange->actual_loc);

	start = prange->start << PAGE_SHIFT;
	end = (prange->last + 1) << PAGE_SHIFT;

	for (addr = start; addr < end;) {
		unsigned long next;

		vma = find_vma(mm, addr);
		if (!vma || addr < vma->vm_start)
			break;

		next = min(vma->vm_end, end);
		r = svm_migrate_vma_to_ram(adev, prange, vma, addr, next);
		if (r) {
			pr_debug("failed %d to migrate\n", r);
			break;
		}
		addr = next;
	}

	if (!r) {
		svm_range_vram_node_free(prange);
		prange->actual_loc = 0;
	}
	return r;
}

/**
 * svm_migrate_vram_to_vram - migrate svm range from device to device
 * @prange: range structure
 * @best_loc: the device to migrate to
 * @mm: process mm, use current->mm if NULL
 *
 * Context: Process context, caller hold mmap read lock, svms lock, prange lock
 *
 * Return:
 * 0 - OK, otherwise error code
 */
static int
svm_migrate_vram_to_vram(struct svm_range *prange, uint32_t best_loc,
			 struct mm_struct *mm)
{
	int r;

	/*
	 * TODO: for both devices with PCIe large bar or on same xgmi hive, skip
	 * system memory as migration bridge
	 */

	pr_debug("from gpu 0x%x to gpu 0x%x\n", prange->actual_loc, best_loc);

	r = svm_migrate_vram_to_ram(prange, mm);
	if (r)
		return r;

	return svm_migrate_ram_to_vram(prange, best_loc, mm);
}

int
svm_migrate_to_vram(struct svm_range *prange, uint32_t best_loc,
		    struct mm_struct *mm)
{
	if  (!prange->actual_loc)
		return svm_migrate_ram_to_vram(prange, best_loc, mm);
	else
		return svm_migrate_vram_to_vram(prange, best_loc, mm);

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
	unsigned long addr = vmf->address;
	struct vm_area_struct *vma;
	enum svm_work_list_ops op;
	struct svm_range *parent;
	struct svm_range *prange;
	struct kfd_process *p;
	struct mm_struct *mm;
	int r = 0;

	vma = vmf->vma;
	mm = vma->vm_mm;

	p = kfd_lookup_process_by_mm(vma->vm_mm);
	if (!p) {
		pr_debug("failed find process at fault address 0x%lx\n", addr);
		return VM_FAULT_SIGBUS;
	}
	addr >>= PAGE_SHIFT;
	pr_debug("CPU page fault svms 0x%p address 0x%lx\n", &p->svms, addr);

	mutex_lock(&p->svms.lock);

	prange = svm_range_from_addr(&p->svms, addr, &parent);
	if (!prange) {
		pr_debug("cannot find svm range at 0x%lx\n", addr);
		r = -EFAULT;
		goto out;
	}

	mutex_lock(&parent->migrate_mutex);
	if (prange != parent)
		mutex_lock_nested(&prange->migrate_mutex, 1);

	if (!prange->actual_loc)
		goto out_unlock_prange;

	svm_range_lock(parent);
	if (prange != parent)
		mutex_lock_nested(&prange->lock, 1);
	r = svm_range_split_by_granularity(p, mm, addr, parent, prange);
	if (prange != parent)
		mutex_unlock(&prange->lock);
	svm_range_unlock(parent);
	if (r) {
		pr_debug("failed %d to split range by granularity\n", r);
		goto out_unlock_prange;
	}

	r = svm_migrate_vram_to_ram(prange, mm);
	if (r)
		pr_debug("failed %d migrate 0x%p [0x%lx 0x%lx] to ram\n", r,
			 prange, prange->start, prange->last);

	/* xnack on, update mapping on GPUs with ACCESS_IN_PLACE */
	if (p->xnack_enabled && parent == prange)
		op = SVM_OP_UPDATE_RANGE_NOTIFIER_AND_MAP;
	else
		op = SVM_OP_UPDATE_RANGE_NOTIFIER;
	svm_range_add_list_work(&p->svms, parent, mm, op);
	schedule_deferred_list_work(&p->svms);

out_unlock_prange:
	if (prange != parent)
		mutex_unlock(&prange->migrate_mutex);
	mutex_unlock(&parent->migrate_mutex);
out:
	mutex_unlock(&p->svms.lock);
	kfd_unref_process(p);

	pr_debug("CPU fault svms 0x%p address 0x%lx done\n", &p->svms, addr);

	return r ? VM_FAULT_SIGBUS : 0;
}

static const struct dev_pagemap_ops svm_migrate_pgmap_ops = {
	.page_free		= svm_migrate_page_free,
	.migrate_to_ram		= svm_migrate_to_ram,
};

/* Each VRAM page uses sizeof(struct page) on system memory */
#define SVM_HMM_PAGE_STRUCT_SIZE(size) ((size)/PAGE_SIZE * sizeof(struct page))

int svm_migrate_init(struct amdgpu_device *adev)
{
	struct kfd_dev *kfddev = adev->kfd.dev;
	struct dev_pagemap *pgmap;
	struct resource *res;
	unsigned long size;
	void *r;

	/* Page migration works on Vega10 or newer */
	if (kfddev->device_info->asic_family < CHIP_VEGA10)
		return -EINVAL;

	pgmap = &kfddev->pgmap;
	memset(pgmap, 0, sizeof(*pgmap));

	/* TODO: register all vram to HMM for now.
	 * should remove reserved size
	 */
	size = ALIGN(adev->gmc.real_vram_size, 2ULL << 20);
	res = devm_request_free_mem_region(adev->dev, &iomem_resource, size);
	if (IS_ERR(res))
		return -ENOMEM;

	pgmap->type = MEMORY_DEVICE_PRIVATE;
	pgmap->nr_range = 1;
	pgmap->range.start = res->start;
	pgmap->range.end = res->end;
	pgmap->ops = &svm_migrate_pgmap_ops;
	pgmap->owner = SVM_ADEV_PGMAP_OWNER(adev);
	pgmap->flags = MIGRATE_VMA_SELECT_DEVICE_PRIVATE;
	r = devm_memremap_pages(adev->dev, pgmap);
	if (IS_ERR(r)) {
		pr_err("failed to register HMM device memory\n");
		devm_release_mem_region(adev->dev, res->start,
					res->end - res->start + 1);
		return PTR_ERR(r);
	}

	pr_debug("reserve %ldMB system memory for VRAM pages struct\n",
		 SVM_HMM_PAGE_STRUCT_SIZE(size) >> 20);

	amdgpu_amdkfd_reserve_system_mem(SVM_HMM_PAGE_STRUCT_SIZE(size));

	pr_info("HMM registered %ldMB device memory\n", size >> 20);

	return 0;
}

void svm_migrate_fini(struct amdgpu_device *adev)
{
	struct dev_pagemap *pgmap = &adev->kfd.dev->pgmap;

	devm_memunmap_pages(adev->dev, pgmap);
	devm_release_mem_region(adev->dev, pgmap->range.start,
				pgmap->range.end - pgmap->range.start + 1);
}
