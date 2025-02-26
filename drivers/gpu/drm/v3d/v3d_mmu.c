// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2017-2018 Broadcom */

/**
 * DOC: Broadcom V3D MMU
 *
 * The V3D 3.x hardware (compared to VC4) now includes an MMU. It has
 * a single level of page tables for the V3D's 4GB address space to
 * map to AXI bus addresses, thus it could need up to 4MB of
 * physically contiguous memory to store the PTEs.
 *
 * Because the 4MB of contiguous memory for page tables is precious,
 * and switching between them is expensive, we load all BOs into the
 * same 4GB address space.
 *
 * To protect clients from each other, we should use the GMP to
 * quickly mask out (at 128kb granularity) what pages are available to
 * each client. This is not yet implemented.
 */

#include "v3d_drv.h"
#include "v3d_regs.h"

/* Note: All PTEs for the 64KB bigpage or 1MB superpage must be filled
 * with the bigpage/superpage bit set.
 */
#define V3D_PTE_SUPERPAGE BIT(31)
#define V3D_PTE_BIGPAGE BIT(30)
#define V3D_PTE_WRITEABLE BIT(29)
#define V3D_PTE_VALID BIT(28)

static bool v3d_mmu_is_aligned(u32 page, u32 page_address, size_t alignment)
{
	return IS_ALIGNED(page, alignment >> V3D_MMU_PAGE_SHIFT) &&
		IS_ALIGNED(page_address, alignment >> V3D_MMU_PAGE_SHIFT);
}

int v3d_mmu_flush_all(struct v3d_dev *v3d)
{
	int ret;

	V3D_WRITE(V3D_MMUC_CONTROL, V3D_MMUC_CONTROL_FLUSH |
		  V3D_MMUC_CONTROL_ENABLE);

	ret = wait_for(!(V3D_READ(V3D_MMUC_CONTROL) &
			 V3D_MMUC_CONTROL_FLUSHING), 100);
	if (ret) {
		dev_err(v3d->drm.dev, "MMUC flush wait idle failed\n");
		return ret;
	}

	V3D_WRITE(V3D_MMU_CTL, V3D_READ(V3D_MMU_CTL) |
		  V3D_MMU_CTL_TLB_CLEAR);

	ret = wait_for(!(V3D_READ(V3D_MMU_CTL) &
			 V3D_MMU_CTL_TLB_CLEARING), 100);
	if (ret)
		dev_err(v3d->drm.dev, "MMU TLB clear wait idle failed\n");

	return ret;
}

int v3d_mmu_set_page_table(struct v3d_dev *v3d)
{
	V3D_WRITE(V3D_MMU_PT_PA_BASE, v3d->pt_paddr >> V3D_MMU_PAGE_SHIFT);
	V3D_WRITE(V3D_MMU_CTL,
		  V3D_MMU_CTL_ENABLE |
		  V3D_MMU_CTL_PT_INVALID_ENABLE |
		  V3D_MMU_CTL_PT_INVALID_ABORT |
		  V3D_MMU_CTL_PT_INVALID_INT |
		  V3D_MMU_CTL_WRITE_VIOLATION_ABORT |
		  V3D_MMU_CTL_WRITE_VIOLATION_INT |
		  V3D_MMU_CTL_CAP_EXCEEDED_ABORT |
		  V3D_MMU_CTL_CAP_EXCEEDED_INT);
	V3D_WRITE(V3D_MMU_ILLEGAL_ADDR,
		  (v3d->mmu_scratch_paddr >> V3D_MMU_PAGE_SHIFT) |
		  V3D_MMU_ILLEGAL_ADDR_ENABLE);
	V3D_WRITE(V3D_MMUC_CONTROL, V3D_MMUC_CONTROL_ENABLE);

	return v3d_mmu_flush_all(v3d);
}

void v3d_mmu_insert_ptes(struct v3d_bo *bo)
{
	struct drm_gem_shmem_object *shmem_obj = &bo->base;
	struct v3d_dev *v3d = to_v3d_dev(shmem_obj->base.dev);
	u32 page = bo->node.start;
	struct scatterlist *sgl;
	unsigned int count;

	for_each_sgtable_dma_sg(shmem_obj->sgt, sgl, count) {
		dma_addr_t dma_addr = sg_dma_address(sgl);
		u32 pfn = dma_addr >> V3D_MMU_PAGE_SHIFT;
		unsigned int len = sg_dma_len(sgl);

		while (len > 0) {
			u32 page_prot = V3D_PTE_WRITEABLE | V3D_PTE_VALID;
			u32 page_address = page_prot | pfn;
			unsigned int i, page_size;

			BUG_ON(pfn + V3D_PAGE_FACTOR >= BIT(24));

			if (len >= SZ_1M &&
			    v3d_mmu_is_aligned(page, page_address, SZ_1M)) {
				page_size = SZ_1M;
				page_address |= V3D_PTE_SUPERPAGE;
			} else if (len >= SZ_64K &&
				   v3d_mmu_is_aligned(page, page_address, SZ_64K)) {
				page_size = SZ_64K;
				page_address |= V3D_PTE_BIGPAGE;
			} else {
				page_size = SZ_4K;
			}

			for (i = 0; i < page_size >> V3D_MMU_PAGE_SHIFT; i++) {
				v3d->pt[page++] = page_address + i;
				pfn++;
			}

			len -= page_size;
		}
	}

	WARN_ON_ONCE(page - bo->node.start !=
		     shmem_obj->base.size >> V3D_MMU_PAGE_SHIFT);

	if (v3d_mmu_flush_all(v3d))
		dev_err(v3d->drm.dev, "MMU flush timeout\n");
}

void v3d_mmu_remove_ptes(struct v3d_bo *bo)
{
	struct v3d_dev *v3d = to_v3d_dev(bo->base.base.dev);
	u32 npages = bo->base.base.size >> V3D_MMU_PAGE_SHIFT;
	u32 page;

	for (page = bo->node.start; page < bo->node.start + npages; page++)
		v3d->pt[page] = 0;

	if (v3d_mmu_flush_all(v3d))
		dev_err(v3d->drm.dev, "MMU flush timeout\n");
}
