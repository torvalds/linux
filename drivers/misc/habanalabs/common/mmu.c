// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include <linux/slab.h>

#include "habanalabs.h"

static bool is_dram_va(struct hl_device *hdev, u64 virt_addr)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;

	return hl_mem_area_inside_range(virt_addr, prop->dmmu.page_size,
					prop->dmmu.start_addr,
					prop->dmmu.end_addr);
}

/**
 * hl_mmu_init() - initialize the MMU module.
 * @hdev: habanalabs device structure.
 *
 * This function does the following:
 * - Create a pool of pages for pgt_infos.
 * - Create a shadow table for pgt
 *
 * Return: 0 for success, non-zero for failure.
 */
int hl_mmu_init(struct hl_device *hdev)
{
	if (hdev->mmu_enable)
		return hdev->mmu_func.init(hdev);

	return 0;
}

/**
 * hl_mmu_fini() - release the MMU module.
 * @hdev: habanalabs device structure.
 *
 * This function does the following:
 * - Disable MMU in H/W.
 * - Free the pgt_infos pool.
 *
 * All contexts should be freed before calling this function.
 */
void hl_mmu_fini(struct hl_device *hdev)
{
	if (hdev->mmu_enable)
		hdev->mmu_func.fini(hdev);
}

/**
 * hl_mmu_ctx_init() - initialize a context for using the MMU module.
 * @ctx: pointer to the context structure to initialize.
 *
 * Initialize a mutex to protect the concurrent mapping flow, a hash to hold all
 * page tables hops related to this context.
 * Return: 0 on success, non-zero otherwise.
 */
int hl_mmu_ctx_init(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;

	if (hdev->mmu_enable)
		return hdev->mmu_func.ctx_init(ctx);

	return 0;
}

/*
 * hl_mmu_ctx_fini - disable a ctx from using the mmu module
 *
 * @ctx: pointer to the context structure
 *
 * This function does the following:
 * - Free any pgts which were not freed yet
 * - Free the mutex
 * - Free DRAM default page mapping hops
 */
void hl_mmu_ctx_fini(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;

	if (hdev->mmu_enable)
		hdev->mmu_func.ctx_fini(ctx);
}

/*
 * hl_mmu_unmap - unmaps a virtual addr
 *
 * @ctx: pointer to the context structure
 * @virt_addr: virt addr to map from
 * @page_size: size of the page to unmap
 * @flush_pte: whether to do a PCI flush
 *
 * This function does the following:
 * - Check that the virt addr is mapped
 * - Unmap the virt addr and frees pgts if possible
 * - Returns 0 on success, -EINVAL if the given addr is not mapped
 *
 * Because this function changes the page tables in the device and because it
 * changes the MMU hash, it must be protected by a lock.
 * However, because it maps only a single page, the lock should be implemented
 * in a higher level in order to protect the entire mapping of the memory area
 *
 * For optimization reasons PCI flush may be requested once after unmapping of
 * large area.
 */
int hl_mmu_unmap(struct hl_ctx *ctx, u64 virt_addr, u32 page_size,
		bool flush_pte)
{
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_mmu_properties *mmu_prop;
	u64 real_virt_addr;
	u32 real_page_size, npages;
	int i, rc = 0;
	bool is_dram_addr;

	if (!hdev->mmu_enable)
		return 0;

	is_dram_addr = is_dram_va(hdev, virt_addr);

	if (is_dram_addr)
		mmu_prop = &prop->dmmu;
	else if ((page_size % prop->pmmu_huge.page_size) == 0)
		mmu_prop = &prop->pmmu_huge;
	else
		mmu_prop = &prop->pmmu;

	/*
	 * The H/W handles mapping of specific page sizes. Hence if the page
	 * size is bigger, we break it to sub-pages and unmap them separately.
	 */
	if ((page_size % mmu_prop->page_size) == 0) {
		real_page_size = mmu_prop->page_size;
	} else {
		dev_err(hdev->dev,
			"page size of %u is not %uKB aligned, can't unmap\n",
			page_size, mmu_prop->page_size >> 10);

		return -EFAULT;
	}

	npages = page_size / real_page_size;
	real_virt_addr = virt_addr;

	for (i = 0 ; i < npages ; i++) {
		rc = hdev->mmu_func.unmap(ctx, real_virt_addr, is_dram_addr);
		if (rc)
			break;

		real_virt_addr += real_page_size;
	}

	if (flush_pte)
		hdev->mmu_func.flush(ctx);

	return rc;
}

/*
 * hl_mmu_map - maps a virtual addr to physical addr
 *
 * @ctx: pointer to the context structure
 * @virt_addr: virt addr to map from
 * @phys_addr: phys addr to map to
 * @page_size: physical page size
 * @flush_pte: whether to do a PCI flush
 *
 * This function does the following:
 * - Check that the virt addr is not mapped
 * - Allocate pgts as necessary in order to map the virt addr to the phys
 * - Returns 0 on success, -EINVAL if addr is already mapped, or -ENOMEM.
 *
 * Because this function changes the page tables in the device and because it
 * changes the MMU hash, it must be protected by a lock.
 * However, because it maps only a single page, the lock should be implemented
 * in a higher level in order to protect the entire mapping of the memory area
 *
 * For optimization reasons PCI flush may be requested once after mapping of
 * large area.
 */
int hl_mmu_map(struct hl_ctx *ctx, u64 virt_addr, u64 phys_addr, u32 page_size,
		bool flush_pte)
{
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_mmu_properties *mmu_prop;
	u64 real_virt_addr, real_phys_addr;
	u32 real_page_size, npages;
	int i, rc, mapped_cnt = 0;
	bool is_dram_addr;

	if (!hdev->mmu_enable)
		return 0;

	is_dram_addr = is_dram_va(hdev, virt_addr);

	if (is_dram_addr)
		mmu_prop = &prop->dmmu;
	else if ((page_size % prop->pmmu_huge.page_size) == 0)
		mmu_prop = &prop->pmmu_huge;
	else
		mmu_prop = &prop->pmmu;

	/*
	 * The H/W handles mapping of specific page sizes. Hence if the page
	 * size is bigger, we break it to sub-pages and map them separately.
	 */
	if ((page_size % mmu_prop->page_size) == 0) {
		real_page_size = mmu_prop->page_size;
	} else {
		dev_err(hdev->dev,
			"page size of %u is not %uKB aligned, can't unmap\n",
			page_size, mmu_prop->page_size >> 10);

		return -EFAULT;
	}

	WARN_ONCE((phys_addr & (real_page_size - 1)),
		"Mapping 0x%llx with page size of 0x%x is erroneous! Address must be divisible by page size",
		phys_addr, real_page_size);

	npages = page_size / real_page_size;
	real_virt_addr = virt_addr;
	real_phys_addr = phys_addr;

	for (i = 0 ; i < npages ; i++) {
		rc = hdev->mmu_func.map(ctx, real_virt_addr, real_phys_addr,
				real_page_size, is_dram_addr);
		if (rc)
			goto err;

		real_virt_addr += real_page_size;
		real_phys_addr += real_page_size;
		mapped_cnt++;
	}

	if (flush_pte)
		hdev->mmu_func.flush(ctx);

	return 0;

err:
	real_virt_addr = virt_addr;
	for (i = 0 ; i < mapped_cnt ; i++) {
		if (hdev->mmu_func.unmap(ctx, real_virt_addr, is_dram_addr))
			dev_warn_ratelimited(hdev->dev,
				"failed to unmap va: 0x%llx\n", real_virt_addr);

		real_virt_addr += real_page_size;
	}

	hdev->mmu_func.flush(ctx);

	return rc;
}

/*
 * hl_mmu_swap_out - marks all mapping of the given ctx as swapped out
 *
 * @ctx: pointer to the context structure
 *
 */
void hl_mmu_swap_out(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;

	if (hdev->mmu_enable)
		hdev->mmu_func.swap_out(ctx);
}

/*
 * hl_mmu_swap_in - marks all mapping of the given ctx as swapped in
 *
 * @ctx: pointer to the context structure
 *
 */
void hl_mmu_swap_in(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;

	if (hdev->mmu_enable)
		hdev->mmu_func.swap_in(ctx);
}

int hl_mmu_if_set_funcs(struct hl_device *hdev)
{
	if (!hdev->mmu_enable)
		return 0;

	switch (hdev->asic_type) {
	case ASIC_GOYA:
	case ASIC_GAUDI:
		hl_mmu_v1_set_funcs(hdev);
		break;
	default:
		dev_err(hdev->dev, "Unrecognized ASIC type %d\n",
			hdev->asic_type);
		return -EOPNOTSUPP;
	}

	return 0;
}
