// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include <linux/slab.h>

#include "../habanalabs.h"

bool hl_is_dram_va(struct hl_device *hdev, u64 virt_addr)
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
 * Return: 0 for success, non-zero for failure.
 */
int hl_mmu_init(struct hl_device *hdev)
{
	int rc = -EOPNOTSUPP;

	if (!hdev->mmu_enable)
		return 0;

	if (hdev->mmu_func[MMU_DR_PGT].init != NULL) {
		rc = hdev->mmu_func[MMU_DR_PGT].init(hdev);
		if (rc)
			return rc;
	}

	if (hdev->mmu_func[MMU_HR_PGT].init != NULL)
		rc = hdev->mmu_func[MMU_HR_PGT].init(hdev);

	return rc;
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
	if (!hdev->mmu_enable)
		return;

	if (hdev->mmu_func[MMU_DR_PGT].fini != NULL)
		hdev->mmu_func[MMU_DR_PGT].fini(hdev);

	if (hdev->mmu_func[MMU_HR_PGT].fini != NULL)
		hdev->mmu_func[MMU_HR_PGT].fini(hdev);
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
	int rc = -EOPNOTSUPP;

	if (!hdev->mmu_enable)
		return 0;

	mutex_init(&ctx->mmu_lock);

	if (hdev->mmu_func[MMU_DR_PGT].ctx_init != NULL) {
		rc = hdev->mmu_func[MMU_DR_PGT].ctx_init(ctx);
		if (rc)
			return rc;
	}

	if (hdev->mmu_func[MMU_HR_PGT].ctx_init != NULL)
		rc = hdev->mmu_func[MMU_HR_PGT].ctx_init(ctx);

	return rc;
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

	if (!hdev->mmu_enable)
		return;

	if (hdev->mmu_func[MMU_DR_PGT].ctx_fini != NULL)
		hdev->mmu_func[MMU_DR_PGT].ctx_fini(ctx);

	if (hdev->mmu_func[MMU_HR_PGT].ctx_fini != NULL)
		hdev->mmu_func[MMU_HR_PGT].ctx_fini(ctx);

	mutex_destroy(&ctx->mmu_lock);
}

/*
 * hl_mmu_unmap_page - unmaps a virtual addr
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
int hl_mmu_unmap_page(struct hl_ctx *ctx, u64 virt_addr, u32 page_size,
		bool flush_pte)
{
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_mmu_properties *mmu_prop;
	u64 real_virt_addr;
	u32 real_page_size, npages;
	int i, rc = 0, pgt_residency;
	bool is_dram_addr;

	if (!hdev->mmu_enable)
		return 0;

	is_dram_addr = hl_is_dram_va(hdev, virt_addr);

	if (is_dram_addr)
		mmu_prop = &prop->dmmu;
	else if ((page_size % prop->pmmu_huge.page_size) == 0)
		mmu_prop = &prop->pmmu_huge;
	else
		mmu_prop = &prop->pmmu;

	pgt_residency = mmu_prop->host_resident ? MMU_HR_PGT : MMU_DR_PGT;
	/*
	 * The H/W handles mapping of specific page sizes. Hence if the page
	 * size is bigger, we break it to sub-pages and unmap them separately.
	 */
	if ((page_size % mmu_prop->page_size) == 0) {
		real_page_size = mmu_prop->page_size;
	} else {
		/*
		 * MMU page size may differ from DRAM page size.
		 * In such case work with the DRAM page size and let the MMU
		 * scrambling routine to handle this mismatch when
		 * calculating the address to remove from the MMU page table
		 */
		if (is_dram_addr && ((page_size % prop->dram_page_size) == 0)) {
			real_page_size = prop->dram_page_size;
		} else {
			dev_err(hdev->dev,
				"page size of %u is not %uKB aligned, can't unmap\n",
				page_size, mmu_prop->page_size >> 10);

			return -EFAULT;
		}
	}

	npages = page_size / real_page_size;
	real_virt_addr = virt_addr;

	for (i = 0 ; i < npages ; i++) {
		rc = hdev->mmu_func[pgt_residency].unmap(ctx,
						real_virt_addr, is_dram_addr);
		if (rc)
			break;

		real_virt_addr += real_page_size;
	}

	if (flush_pte)
		hdev->mmu_func[pgt_residency].flush(ctx);

	return rc;
}

/*
 * hl_mmu_map_page - maps a virtual addr to physical addr
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
int hl_mmu_map_page(struct hl_ctx *ctx, u64 virt_addr, u64 phys_addr,
		u32 page_size, bool flush_pte)
{
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_mmu_properties *mmu_prop;
	u64 real_virt_addr, real_phys_addr;
	u32 real_page_size, npages;
	int i, rc, pgt_residency, mapped_cnt = 0;
	bool is_dram_addr;


	if (!hdev->mmu_enable)
		return 0;

	is_dram_addr = hl_is_dram_va(hdev, virt_addr);

	if (is_dram_addr)
		mmu_prop = &prop->dmmu;
	else if ((page_size % prop->pmmu_huge.page_size) == 0)
		mmu_prop = &prop->pmmu_huge;
	else
		mmu_prop = &prop->pmmu;

	pgt_residency = mmu_prop->host_resident ? MMU_HR_PGT : MMU_DR_PGT;

	/*
	 * The H/W handles mapping of specific page sizes. Hence if the page
	 * size is bigger, we break it to sub-pages and map them separately.
	 */
	if ((page_size % mmu_prop->page_size) == 0) {
		real_page_size = mmu_prop->page_size;
	} else if (is_dram_addr && ((page_size % prop->dram_page_size) == 0) &&
			(prop->dram_page_size < mmu_prop->page_size)) {
		/*
		 * MMU page size may differ from DRAM page size.
		 * In such case work with the DRAM page size and let the MMU
		 * scrambling routine handle this mismatch when calculating
		 * the address to place in the MMU page table. (in that case
		 * also make sure that the dram_page_size smaller than the
		 * mmu page size)
		 */
		real_page_size = prop->dram_page_size;
	} else {
		dev_err(hdev->dev,
			"page size of %u is not %uKB aligned, can't map\n",
			page_size, mmu_prop->page_size >> 10);

		return -EFAULT;
	}

	/*
	 * Verify that the phys and virt addresses are aligned with the
	 * MMU page size (in dram this means checking the address and MMU
	 * after scrambling)
	 */
	if ((is_dram_addr &&
			((hdev->asic_funcs->scramble_addr(hdev, phys_addr) &
				(mmu_prop->page_size - 1)) ||
			(hdev->asic_funcs->scramble_addr(hdev, virt_addr) &
				(mmu_prop->page_size - 1)))) ||
		(!is_dram_addr && ((phys_addr & (real_page_size - 1)) ||
				(virt_addr & (real_page_size - 1)))))
		dev_crit(hdev->dev,
			"Mapping address 0x%llx with virtual address 0x%llx and page size of 0x%x is erroneous! Addresses must be divisible by page size",
			phys_addr, virt_addr, real_page_size);

	npages = page_size / real_page_size;
	real_virt_addr = virt_addr;
	real_phys_addr = phys_addr;

	for (i = 0 ; i < npages ; i++) {
		rc = hdev->mmu_func[pgt_residency].map(ctx,
						real_virt_addr, real_phys_addr,
						real_page_size, is_dram_addr);
		if (rc)
			goto err;

		real_virt_addr += real_page_size;
		real_phys_addr += real_page_size;
		mapped_cnt++;
	}

	if (flush_pte)
		hdev->mmu_func[pgt_residency].flush(ctx);

	return 0;

err:
	real_virt_addr = virt_addr;
	for (i = 0 ; i < mapped_cnt ; i++) {
		if (hdev->mmu_func[pgt_residency].unmap(ctx,
						real_virt_addr, is_dram_addr))
			dev_warn_ratelimited(hdev->dev,
				"failed to unmap va: 0x%llx\n", real_virt_addr);

		real_virt_addr += real_page_size;
	}

	hdev->mmu_func[pgt_residency].flush(ctx);

	return rc;
}

/*
 * hl_mmu_map_contiguous - implements a wrapper for hl_mmu_map_page
 *                         for mapping contiguous physical memory
 *
 * @ctx: pointer to the context structure
 * @virt_addr: virt addr to map from
 * @phys_addr: phys addr to map to
 * @size: size to map
 *
 */
int hl_mmu_map_contiguous(struct hl_ctx *ctx, u64 virt_addr,
					u64 phys_addr, u32 size)
{
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 curr_va, curr_pa;
	u32 page_size;
	bool flush_pte;
	int rc = 0, off;

	if (hl_mem_area_inside_range(virt_addr, size,
			prop->dmmu.start_addr, prop->dmmu.end_addr))
		page_size = prop->dmmu.page_size;
	else if (hl_mem_area_inside_range(virt_addr, size,
			prop->pmmu.start_addr, prop->pmmu.end_addr))
		page_size = prop->pmmu.page_size;
	else if (hl_mem_area_inside_range(virt_addr, size,
			prop->pmmu_huge.start_addr, prop->pmmu_huge.end_addr))
		page_size = prop->pmmu_huge.page_size;
	else
		return -EINVAL;

	for (off = 0 ; off < size ; off += page_size) {
		curr_va = virt_addr + off;
		curr_pa = phys_addr + off;
		flush_pte = (off + page_size) >= size;
		rc = hl_mmu_map_page(ctx, curr_va, curr_pa, page_size,
								flush_pte);
		if (rc) {
			dev_err(hdev->dev,
				"Map failed for va 0x%llx to pa 0x%llx\n",
				curr_va, curr_pa);
			goto unmap;
		}
	}

	return rc;

unmap:
	for (; off >= 0 ; off -= page_size) {
		curr_va = virt_addr + off;
		flush_pte = (off - (s32) page_size) < 0;
		if (hl_mmu_unmap_page(ctx, curr_va, page_size, flush_pte))
			dev_warn_ratelimited(hdev->dev,
				"failed to unmap va 0x%llx\n", curr_va);
	}

	return rc;
}

/*
 * hl_mmu_unmap_contiguous - implements a wrapper for hl_mmu_unmap_page
 *                           for unmapping contiguous physical memory
 *
 * @ctx: pointer to the context structure
 * @virt_addr: virt addr to unmap
 * @size: size to unmap
 *
 */
int hl_mmu_unmap_contiguous(struct hl_ctx *ctx, u64 virt_addr, u32 size)
{
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 curr_va;
	u32 page_size;
	bool flush_pte;
	int rc = 0, off;

	if (hl_mem_area_inside_range(virt_addr, size,
			prop->dmmu.start_addr, prop->dmmu.end_addr))
		page_size = prop->dmmu.page_size;
	else if (hl_mem_area_inside_range(virt_addr, size,
			prop->pmmu.start_addr, prop->pmmu.end_addr))
		page_size = prop->pmmu.page_size;
	else if (hl_mem_area_inside_range(virt_addr, size,
			prop->pmmu_huge.start_addr, prop->pmmu_huge.end_addr))
		page_size = prop->pmmu_huge.page_size;
	else
		return -EINVAL;

	for (off = 0 ; off < size ; off += page_size) {
		curr_va = virt_addr + off;
		flush_pte = (off + page_size) >= size;
		rc = hl_mmu_unmap_page(ctx, curr_va, page_size, flush_pte);
		if (rc)
			dev_warn_ratelimited(hdev->dev,
				"Unmap failed for va 0x%llx\n", curr_va);
	}

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

	if (!hdev->mmu_enable)
		return;

	if (hdev->mmu_func[MMU_DR_PGT].swap_out != NULL)
		hdev->mmu_func[MMU_DR_PGT].swap_out(ctx);

	if (hdev->mmu_func[MMU_HR_PGT].swap_out != NULL)
		hdev->mmu_func[MMU_HR_PGT].swap_out(ctx);
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

	if (!hdev->mmu_enable)
		return;

	if (hdev->mmu_func[MMU_DR_PGT].swap_in != NULL)
		hdev->mmu_func[MMU_DR_PGT].swap_in(ctx);

	if (hdev->mmu_func[MMU_HR_PGT].swap_in != NULL)
		hdev->mmu_func[MMU_HR_PGT].swap_in(ctx);
}

static void hl_mmu_pa_page_with_offset(struct hl_ctx *ctx, u64 virt_addr,
						struct hl_mmu_hop_info *hops,
						u64 *phys_addr)
{
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 offset_mask, addr_mask, hop_shift, tmp_phys_addr;
	u32 hop0_shift_off;
	void *p;

	/* last hop holds the phys address and flags */
	if (hops->unscrambled_paddr)
		tmp_phys_addr = hops->unscrambled_paddr;
	else
		tmp_phys_addr = hops->hop_info[hops->used_hops - 1].hop_pte_val;

	if (hops->range_type == HL_VA_RANGE_TYPE_HOST_HUGE)
		p = &prop->pmmu_huge;
	else if (hops->range_type == HL_VA_RANGE_TYPE_HOST)
		p = &prop->pmmu;
	else /* HL_VA_RANGE_TYPE_DRAM */
		p = &prop->dmmu;

	if ((hops->range_type == HL_VA_RANGE_TYPE_DRAM) &&
			!is_power_of_2(prop->dram_page_size)) {
		u64 dram_page_size, dram_base, abs_phys_addr, abs_virt_addr,
			page_id, page_start;
		u32 page_off;

		/*
		 * Bit arithmetics cannot be used for non power of two page
		 * sizes. In addition, since bit arithmetics is not used,
		 * we cannot ignore dram base. All that shall be considerd.
		 */

		dram_page_size = prop->dram_page_size;
		dram_base = prop->dram_base_address;
		abs_phys_addr = tmp_phys_addr - dram_base;
		abs_virt_addr = virt_addr - dram_base;
		page_id = DIV_ROUND_DOWN_ULL(abs_phys_addr, dram_page_size);
		page_start = page_id * dram_page_size;
		div_u64_rem(abs_virt_addr, dram_page_size, &page_off);

		*phys_addr = page_start + page_off + dram_base;
	} else {
		/*
		 * find the correct hop shift field in hl_mmu_properties
		 * structure in order to determine the right masks
		 * for the page offset.
		 */
		hop0_shift_off = offsetof(struct hl_mmu_properties, hop0_shift);
		p = (char *)p + hop0_shift_off;
		p = (char *)p + ((hops->used_hops - 1) * sizeof(u64));
		hop_shift = *(u64 *)p;
		offset_mask = (1ull << hop_shift) - 1;
		addr_mask = ~(offset_mask);
		*phys_addr = (tmp_phys_addr & addr_mask) |
				(virt_addr & offset_mask);
	}
}

int hl_mmu_va_to_pa(struct hl_ctx *ctx, u64 virt_addr, u64 *phys_addr)
{
	struct hl_mmu_hop_info hops;
	int rc;

	memset(&hops, 0, sizeof(hops));

	rc = hl_mmu_get_tlb_info(ctx, virt_addr, &hops);
	if (rc)
		return rc;

	hl_mmu_pa_page_with_offset(ctx, virt_addr, &hops,  phys_addr);

	return 0;
}

int hl_mmu_get_tlb_info(struct hl_ctx *ctx, u64 virt_addr,
			struct hl_mmu_hop_info *hops)
{
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_mmu_properties *mmu_prop;
	int rc;
	bool is_dram_addr;

	if (!hdev->mmu_enable)
		return -EOPNOTSUPP;

	hops->scrambled_vaddr = virt_addr;      /* assume no scrambling */

	is_dram_addr = hl_mem_area_inside_range(virt_addr, prop->dmmu.page_size,
						prop->dmmu.start_addr,
						prop->dmmu.end_addr);

	/* host-residency is the same in PMMU and HPMMU, use one of them */
	mmu_prop = is_dram_addr ? &prop->dmmu : &prop->pmmu;

	mutex_lock(&ctx->mmu_lock);

	if (mmu_prop->host_resident)
		rc = hdev->mmu_func[MMU_HR_PGT].get_tlb_info(ctx,
							virt_addr, hops);
	else
		rc = hdev->mmu_func[MMU_DR_PGT].get_tlb_info(ctx,
							virt_addr, hops);

	mutex_unlock(&ctx->mmu_lock);

	/* add page offset to physical address */
	if (hops->unscrambled_paddr)
		hl_mmu_pa_page_with_offset(ctx, virt_addr, hops,
					&hops->unscrambled_paddr);

	return rc;
}

int hl_mmu_if_set_funcs(struct hl_device *hdev)
{
	if (!hdev->mmu_enable)
		return 0;

	switch (hdev->asic_type) {
	case ASIC_GOYA:
	case ASIC_GAUDI:
	case ASIC_GAUDI_SEC:
		hl_mmu_v1_set_funcs(hdev, &hdev->mmu_func[MMU_DR_PGT]);
		break;
	default:
		dev_err(hdev->dev, "Unrecognized ASIC type %d\n",
			hdev->asic_type);
		return -EOPNOTSUPP;
	}

	return 0;
}

/**
 * hl_mmu_scramble_addr() - The generic mmu address scrambling routine.
 * @hdev: pointer to device data.
 * @addr: The address to scramble.
 *
 * Return: The scrambled address.
 */
u64 hl_mmu_scramble_addr(struct hl_device *hdev, u64 addr)
{
	return addr;
}

/**
 * hl_mmu_descramble_addr() - The generic mmu address descrambling
 * routine.
 * @hdev: pointer to device data.
 * @addr: The address to descramble.
 *
 * Return: The un-scrambled address.
 */
u64 hl_mmu_descramble_addr(struct hl_device *hdev, u64 addr)
{
	return addr;
}
