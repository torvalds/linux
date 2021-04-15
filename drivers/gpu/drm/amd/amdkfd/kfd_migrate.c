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
