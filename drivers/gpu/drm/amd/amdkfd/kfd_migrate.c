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
int
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

static void svm_migrate_page_free(struct page *page)
{
}

/**
 * svm_migrate_to_ram - CPU page fault handler
 * @vmf: CPU vm fault vma, address
 *
 * Context: vm fault handler, mm->mmap_sem is taken
 *
 * Return:
 * 0 - OK
 * VM_FAULT_SIGBUS - notice application to have SIGBUS page fault
 */
static vm_fault_t svm_migrate_to_ram(struct vm_fault *vmf)
{
	return VM_FAULT_SIGBUS;
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
	pgmap->owner = adev;
	pgmap->flags = MIGRATE_VMA_SELECT_DEVICE_PRIVATE;
	r = devm_memremap_pages(adev->dev, pgmap);
	if (IS_ERR(r)) {
		pr_err("failed to register HMM device memory\n");
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
	memunmap_pages(&adev->kfd.dev->pgmap);
}
