// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include <linux/slab.h>

#include "../habanalabs.h"

#include <trace/events/habanalabs.h>

/**
 * hl_mmu_get_funcs() - get MMU functions structure
 * @hdev: habanalabs device structure.
 * @pgt_residency: page table residency.
 * @is_dram_addr: true if we need HMMU functions
 *
 * @return appropriate MMU functions structure
 */
static struct hl_mmu_funcs *hl_mmu_get_funcs(struct hl_device *hdev, int pgt_residency,
									bool is_dram_addr)
{
	return &hdev->mmu_func[pgt_residency];
}

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

	if (hdev->mmu_disable)
		return 0;

	mutex_init(&hdev->mmu_lock);

	if (hdev->mmu_func[MMU_DR_PGT].init != NULL) {
		rc = hdev->mmu_func[MMU_DR_PGT].init(hdev);
		if (rc)
			return rc;
	}

	if (hdev->mmu_func[MMU_HR_PGT].init != NULL) {
		rc = hdev->mmu_func[MMU_HR_PGT].init(hdev);
		if (rc)
			goto fini_dr_mmu;
	}

	return 0;

fini_dr_mmu:
	if (hdev->mmu_func[MMU_DR_PGT].fini != NULL)
		hdev->mmu_func[MMU_DR_PGT].fini(hdev);

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
	if (hdev->mmu_disable)
		return;

	if (hdev->mmu_func[MMU_DR_PGT].fini != NULL)
		hdev->mmu_func[MMU_DR_PGT].fini(hdev);

	if (hdev->mmu_func[MMU_HR_PGT].fini != NULL)
		hdev->mmu_func[MMU_HR_PGT].fini(hdev);

	mutex_destroy(&hdev->mmu_lock);
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

	if (hdev->mmu_disable)
		return 0;

	if (hdev->mmu_func[MMU_DR_PGT].ctx_init != NULL) {
		rc = hdev->mmu_func[MMU_DR_PGT].ctx_init(ctx);
		if (rc)
			return rc;
	}

	if (hdev->mmu_func[MMU_HR_PGT].ctx_init != NULL) {
		rc = hdev->mmu_func[MMU_HR_PGT].ctx_init(ctx);
		if (rc)
			goto fini_dr_ctx;
	}

	return 0;

fini_dr_ctx:
	if (hdev->mmu_func[MMU_DR_PGT].fini != NULL)
		hdev->mmu_func[MMU_DR_PGT].fini(hdev);

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

	if (hdev->mmu_disable)
		return;

	if (hdev->mmu_func[MMU_DR_PGT].ctx_fini != NULL)
		hdev->mmu_func[MMU_DR_PGT].ctx_fini(ctx);

	if (hdev->mmu_func[MMU_HR_PGT].ctx_fini != NULL)
		hdev->mmu_func[MMU_HR_PGT].ctx_fini(ctx);
}

/*
 * hl_mmu_get_real_page_size - get real page size to use in map/unmap operation
 *
 * @hdev: pointer to device data.
 * @mmu_prop: MMU properties.
 * @page_size: page size
 * @real_page_size: set here the actual page size to use for the operation
 * @is_dram_addr: true if DRAM address, otherwise false.
 *
 * @return 0 on success, otherwise non 0 error code
 *
 * note that this is general implementation that can fit most MMU arch. but as this is used as an
 * MMU function:
 * 1. it shall not be called directly- only from mmu_func structure instance
 * 2. each MMU may modify the implementation internally
 */
int hl_mmu_get_real_page_size(struct hl_device *hdev, struct hl_mmu_properties *mmu_prop,
				u32 page_size, u32 *real_page_size, bool is_dram_addr)
{
	/*
	 * The H/W handles mapping of specific page sizes. Hence if the page
	 * size is bigger, we break it to sub-pages and map them separately.
	 */
	if ((page_size % mmu_prop->page_size) == 0) {
		*real_page_size = mmu_prop->page_size;
		return 0;
	}

	dev_err(hdev->dev, "page size of %u is not %uKB aligned, can't map\n",
						page_size, mmu_prop->page_size >> 10);

	return -EFAULT;
}

static struct hl_mmu_properties *hl_mmu_get_prop(struct hl_device *hdev, u32 page_size,
							bool is_dram_addr)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;

	if (is_dram_addr)
		return &prop->dmmu;
	else if ((page_size % prop->pmmu_huge.page_size) == 0)
		return &prop->pmmu_huge;

	return &prop->pmmu;
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
int hl_mmu_unmap_page(struct hl_ctx *ctx, u64 virt_addr, u32 page_size, bool flush_pte)
{
	struct hl_device *hdev = ctx->hdev;
	struct hl_mmu_properties *mmu_prop;
	struct hl_mmu_funcs *mmu_funcs;
	int i, pgt_residency, rc = 0;
	u32 real_page_size, npages;
	u64 real_virt_addr;
	bool is_dram_addr;

	if (hdev->mmu_disable)
		return 0;

	is_dram_addr = hl_is_dram_va(hdev, virt_addr);
	mmu_prop = hl_mmu_get_prop(hdev, page_size, is_dram_addr);

	pgt_residency = mmu_prop->host_resident ? MMU_HR_PGT : MMU_DR_PGT;
	mmu_funcs = hl_mmu_get_funcs(hdev, pgt_residency, is_dram_addr);

	rc = hdev->asic_funcs->mmu_get_real_page_size(hdev, mmu_prop, page_size, &real_page_size,
							is_dram_addr);
	if (rc)
		return rc;

	npages = page_size / real_page_size;
	real_virt_addr = virt_addr;

	for (i = 0 ; i < npages ; i++) {
		rc = mmu_funcs->unmap(ctx, real_virt_addr, is_dram_addr);
		if (rc)
			break;

		real_virt_addr += real_page_size;
	}

	if (flush_pte)
		mmu_funcs->flush(ctx);

	if (trace_habanalabs_mmu_unmap_enabled() && !rc)
		trace_habanalabs_mmu_unmap(hdev->dev, virt_addr, 0, page_size, flush_pte);

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
int hl_mmu_map_page(struct hl_ctx *ctx, u64 virt_addr, u64 phys_addr, u32 page_size,
			bool flush_pte)
{
	int i, rc, pgt_residency, mapped_cnt = 0;
	struct hl_device *hdev = ctx->hdev;
	struct hl_mmu_properties *mmu_prop;
	u64 real_virt_addr, real_phys_addr;
	struct hl_mmu_funcs *mmu_funcs;
	u32 real_page_size, npages;
	bool is_dram_addr;


	if (hdev->mmu_disable)
		return 0;

	is_dram_addr = hl_is_dram_va(hdev, virt_addr);
	mmu_prop = hl_mmu_get_prop(hdev, page_size, is_dram_addr);

	pgt_residency = mmu_prop->host_resident ? MMU_HR_PGT : MMU_DR_PGT;
	mmu_funcs = hl_mmu_get_funcs(hdev, pgt_residency, is_dram_addr);

	rc = hdev->asic_funcs->mmu_get_real_page_size(hdev, mmu_prop, page_size, &real_page_size,
							is_dram_addr);
	if (rc)
		return rc;

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
		rc = mmu_funcs->map(ctx, real_virt_addr, real_phys_addr, real_page_size,
										is_dram_addr);
		if (rc)
			goto err;

		real_virt_addr += real_page_size;
		real_phys_addr += real_page_size;
		mapped_cnt++;
	}

	if (flush_pte)
		mmu_funcs->flush(ctx);

	trace_habanalabs_mmu_map(hdev->dev, virt_addr, phys_addr, page_size, flush_pte);

	return 0;

err:
	real_virt_addr = virt_addr;
	for (i = 0 ; i < mapped_cnt ; i++) {
		if (mmu_funcs->unmap(ctx, real_virt_addr, is_dram_addr))
			dev_warn_ratelimited(hdev->dev,
				"failed to unmap va: 0x%llx\n", real_virt_addr);

		real_virt_addr += real_page_size;
	}

	mmu_funcs->flush(ctx);

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
			/* last mapping failed so don't try to unmap it - reduce off by page_size */
			off -= page_size;
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

static void hl_mmu_pa_page_with_offset(struct hl_ctx *ctx, u64 virt_addr,
						struct hl_mmu_hop_info *hops,
						u64 *phys_addr)
{
	struct asic_fixed_properties *prop = &ctx->hdev->asic_prop;
	u64 offset_mask, addr_mask, hop_shift, tmp_phys_addr;
	struct hl_mmu_properties *mmu_prop;

	/* last hop holds the phys address and flags */
	if (hops->unscrambled_paddr)
		tmp_phys_addr = hops->unscrambled_paddr;
	else
		tmp_phys_addr = hops->hop_info[hops->used_hops - 1].hop_pte_val;

	if (hops->range_type == HL_VA_RANGE_TYPE_HOST_HUGE)
		mmu_prop = &prop->pmmu_huge;
	else if (hops->range_type == HL_VA_RANGE_TYPE_HOST)
		mmu_prop = &prop->pmmu;
	else /* HL_VA_RANGE_TYPE_DRAM */
		mmu_prop = &prop->dmmu;

	if ((hops->range_type == HL_VA_RANGE_TYPE_DRAM) &&
			!is_power_of_2(prop->dram_page_size)) {
		u64 dram_page_size, dram_base, abs_phys_addr, abs_virt_addr,
			page_id, page_start;
		u32 page_off;

		/*
		 * Bit arithmetic cannot be used for non power of two page
		 * sizes. In addition, since bit arithmetic is not used,
		 * we cannot ignore dram base. All that shall be considered.
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
		hop_shift = mmu_prop->hop_shifts[hops->used_hops - 1];
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
	struct asic_fixed_properties *prop;
	struct hl_mmu_properties *mmu_prop;
	struct hl_mmu_funcs *mmu_funcs;
	int pgt_residency, rc;
	bool is_dram_addr;

	if (hdev->mmu_disable)
		return -EOPNOTSUPP;

	prop = &hdev->asic_prop;
	hops->scrambled_vaddr = virt_addr;      /* assume no scrambling */

	is_dram_addr = hl_mem_area_inside_range(virt_addr, prop->dmmu.page_size,
								prop->dmmu.start_addr,
								prop->dmmu.end_addr);

	/* host-residency is the same in PMMU and PMMU huge, no need to distinguish here */
	mmu_prop = is_dram_addr ? &prop->dmmu : &prop->pmmu;
	pgt_residency = mmu_prop->host_resident ? MMU_HR_PGT : MMU_DR_PGT;
	mmu_funcs = hl_mmu_get_funcs(hdev, pgt_residency, is_dram_addr);

	mutex_lock(&hdev->mmu_lock);
	rc = mmu_funcs->get_tlb_info(ctx, virt_addr, hops);
	mutex_unlock(&hdev->mmu_lock);

	if (rc)
		return rc;

	/* add page offset to physical address */
	if (hops->unscrambled_paddr)
		hl_mmu_pa_page_with_offset(ctx, virt_addr, hops, &hops->unscrambled_paddr);

	return 0;
}

int hl_mmu_if_set_funcs(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;

	if (hdev->mmu_disable)
		return 0;

	switch (hdev->asic_type) {
	case ASIC_GOYA:
	case ASIC_GAUDI:
	case ASIC_GAUDI_SEC:
		hl_mmu_v1_set_funcs(hdev, &hdev->mmu_func[MMU_DR_PGT]);
		break;
	case ASIC_GAUDI2:
	case ASIC_GAUDI2B:
	case ASIC_GAUDI2C:
		hl_mmu_v2_set_funcs(hdev, &hdev->mmu_func[MMU_DR_PGT]);
		if (prop->pmmu.host_resident)
			hl_mmu_v2_hr_set_funcs(hdev, &hdev->mmu_func[MMU_HR_PGT]);
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

int hl_mmu_invalidate_cache(struct hl_device *hdev, bool is_hard, u32 flags)
{
	int rc;

	rc = hdev->asic_funcs->mmu_invalidate_cache(hdev, is_hard, flags);
	if (rc)
		dev_err_ratelimited(hdev->dev,
				"%s cache invalidation failed, rc=%d\n",
				flags == VM_TYPE_USERPTR ? "PMMU" : "HMMU", rc);

	return rc;
}

int hl_mmu_invalidate_cache_range(struct hl_device *hdev, bool is_hard,
					u32 flags, u32 asid, u64 va, u64 size)
{
	int rc;

	rc = hdev->asic_funcs->mmu_invalidate_cache_range(hdev, is_hard, flags,
								asid, va, size);
	if (rc)
		dev_err_ratelimited(hdev->dev,
				"%s cache range invalidation failed: va=%#llx, size=%llu, rc=%d",
				flags == VM_TYPE_USERPTR ? "PMMU" : "HMMU", va, size, rc);

	return rc;
}

static void hl_mmu_prefetch_work_function(struct work_struct *work)
{
	struct hl_prefetch_work *pfw = container_of(work, struct hl_prefetch_work, prefetch_work);
	struct hl_ctx *ctx = pfw->ctx;
	struct hl_device *hdev = ctx->hdev;

	if (!hl_device_operational(hdev, NULL))
		goto put_ctx;

	mutex_lock(&hdev->mmu_lock);

	hdev->asic_funcs->mmu_prefetch_cache_range(ctx, pfw->flags, pfw->asid, pfw->va, pfw->size);

	mutex_unlock(&hdev->mmu_lock);

put_ctx:
	/*
	 * context was taken in the common mmu prefetch function- see comment there about
	 * context handling.
	 */
	hl_ctx_put(ctx);
	kfree(pfw);
}

int hl_mmu_prefetch_cache_range(struct hl_ctx *ctx, u32 flags, u32 asid, u64 va, u64 size)
{
	struct hl_prefetch_work *handle_prefetch_work;

	handle_prefetch_work = kmalloc(sizeof(*handle_prefetch_work), GFP_KERNEL);
	if (!handle_prefetch_work)
		return -ENOMEM;

	INIT_WORK(&handle_prefetch_work->prefetch_work, hl_mmu_prefetch_work_function);
	handle_prefetch_work->ctx = ctx;
	handle_prefetch_work->va = va;
	handle_prefetch_work->size = size;
	handle_prefetch_work->flags = flags;
	handle_prefetch_work->asid = asid;

	/*
	 * as actual prefetch is done in a WQ we must get the context (and put it
	 * at the end of the work function)
	 */
	hl_ctx_get(ctx);
	queue_work(ctx->hdev->prefetch_wq, &handle_prefetch_work->prefetch_work);

	return 0;
}

u64 hl_mmu_get_next_hop_addr(struct hl_ctx *ctx, u64 curr_pte)
{
	return (curr_pte & PAGE_PRESENT_MASK) ? (curr_pte & HOP_PHYS_ADDR_MASK) : ULLONG_MAX;
}

/**
 * hl_mmu_get_hop_pte_phys_addr() - extract PTE address from HOP
 * @ctx: pointer to the context structure to initialize.
 * @mmu_prop: MMU properties.
 * @hop_idx: HOP index.
 * @hop_addr: HOP address.
 * @virt_addr: virtual address for the translation.
 *
 * @return the matching PTE value on success, otherwise U64_MAX.
 */
u64 hl_mmu_get_hop_pte_phys_addr(struct hl_ctx *ctx, struct hl_mmu_properties *mmu_prop,
					u8 hop_idx, u64 hop_addr, u64 virt_addr)
{
	u64 mask, shift;

	if (hop_idx >= mmu_prop->num_hops) {
		dev_err_ratelimited(ctx->hdev->dev, "Invalid hop index %d\n", hop_idx);
		return U64_MAX;
	}

	shift = mmu_prop->hop_shifts[hop_idx];
	mask = mmu_prop->hop_masks[hop_idx];

	return hop_addr + ctx->hdev->asic_prop.mmu_pte_size * ((virt_addr & mask) >> shift);
}

static void mmu_dma_mem_free_from_chunk(struct gen_pool *pool,
					struct gen_pool_chunk *chunk,
					void *data)
{
	struct hl_device *hdev = data;

	hl_asic_dma_free_coherent(hdev, (chunk->end_addr - chunk->start_addr) + 1,
					(void *)chunk->start_addr, chunk->phys_addr);
}

void hl_mmu_hr_flush(struct hl_ctx *ctx)
{
	/* a flush operation requires memory barrier */
	mb();
}

/**
 * hl_mmu_hr_pool_destroy() - destroy genpool
 * @hdev: habanalabs device structure.
 * @hr_priv: MMU HR private data.
 * @hop_table_size: HOP table size.
 *
 * This function does the following:
 * - free entries allocated for shadow HOP0
 * - free pool chunks
 * - free pool
 */
static void hl_mmu_hr_pool_destroy(struct hl_device *hdev, struct hl_mmu_hr_priv *hr_priv,
					u32 hop_table_size)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct gen_pool **pool = &hr_priv->mmu_pgt_pool;
	struct pgt_info *hop0_pgt;
	int asid;

	if (ZERO_OR_NULL_PTR(*pool))
		return;

	/* Free the Fixed allocation of HOPs0 */
	if (hr_priv->mmu_asid_hop0) {
		for (asid = 0 ; asid < prop->max_asid ; asid++) {
			hop0_pgt = &hr_priv->mmu_asid_hop0[asid];
			if (ZERO_OR_NULL_PTR(hop0_pgt->virt_addr))
				continue;

			gen_pool_free(*pool, (uintptr_t) hop0_pgt->virt_addr, hop_table_size);
		}
	}

	gen_pool_for_each_chunk(*pool, mmu_dma_mem_free_from_chunk, hdev);
	gen_pool_destroy(*pool);

	/* Make sure that if we arrive here again without init was called we
	 * won't cause kernel panic. This can happen for example if we fail
	 * during hard reset code at certain points
	 */
	*pool = NULL;
}

/**
 * hl_mmu_hr_init() - initialize the MMU module.
 * @hdev: habanalabs device structure.
 * @hr_priv: MMU HR private data.
 * @hop_table_size: HOP table size.
 * @pgt_size: memory size allocated for the page table
 *
 * @return 0 on success otherwise non-zero error code
 *
 * This function does the following:
 * - Create a pool of pages for pgt_infos.
 * - Create a shadow table for pgt
 */
int hl_mmu_hr_init(struct hl_device *hdev, struct hl_mmu_hr_priv *hr_priv, u32 hop_table_size,
			u64 pgt_size)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	size_t pool_chunk_size = SZ_4M;
	struct pgt_info *hop0_pgt;
	dma_addr_t dma_addr;
	u64 virt_addr;
	int i, rc;

	/*
	 * we set alloc size as PAGE_SIZE (sine dma_alloc_coherent allocation order/size is
	 * PAGE_SHIFT/PAGE_SIZE) in order to be able to control the allocations alignment.
	 * This way we can call "DMA alloc align" according to dma_alloc granularity and supply
	 * allocations with higher-order alignment restrictions
	 */
	hr_priv->mmu_pgt_pool = gen_pool_create(PAGE_SHIFT, -1);
	if (ZERO_OR_NULL_PTR(hr_priv->mmu_pgt_pool)) {
		dev_err(hdev->dev, "Failed to create hr page pool\n");
		return -ENOMEM;
	}

	hr_priv->mmu_asid_hop0 = kvcalloc(prop->max_asid, sizeof(struct pgt_info), GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(hr_priv->mmu_asid_hop0)) {
		dev_err(hdev->dev, "Failed to allocate hr-mmu hop0 table\n");
		rc = -ENOMEM;
		goto destroy_mmu_pgt_pool;
	}

	for (i = 0 ; i < pgt_size ; i += pool_chunk_size) {
		virt_addr = (uintptr_t) hl_asic_dma_alloc_coherent(hdev, pool_chunk_size,
									&dma_addr,
									GFP_KERNEL | __GFP_ZERO);
		if (ZERO_OR_NULL_PTR(virt_addr)) {
			dev_err(hdev->dev,
				"Failed to allocate memory for host-resident page pool\n");
			rc = -ENOMEM;
			goto destroy_mmu_pgt_pool;
		}

		rc = gen_pool_add_virt(hr_priv->mmu_pgt_pool, virt_addr, (phys_addr_t) dma_addr,
						pool_chunk_size, -1);
		if (rc) {
			dev_err(hdev->dev, "Failed to fill host-resident page pool\n");
			goto destroy_mmu_pgt_pool;
		}
	}

	for (i = 0 ; i < prop->max_asid ; i++) {
		hop0_pgt = &hr_priv->mmu_asid_hop0[i];
		hop0_pgt->virt_addr = (uintptr_t)
					gen_pool_dma_zalloc_align(hr_priv->mmu_pgt_pool,
								hop_table_size,
								(dma_addr_t *) &hop0_pgt->phys_addr,
								hop_table_size);
		if (!hop0_pgt->virt_addr) {
			dev_err(hdev->dev, "Failed to allocate HOP from pgt pool\n");
			rc = -ENOMEM;
			goto destroy_mmu_pgt_pool;
		}
	}

	/* MMU H/W init will be done in device hw_init() */

	return 0;

destroy_mmu_pgt_pool:
	hl_mmu_hr_pool_destroy(hdev, hr_priv, hop_table_size);
	if (!ZERO_OR_NULL_PTR(hr_priv->mmu_asid_hop0))
		kvfree(hr_priv->mmu_asid_hop0);

	return rc;
}

/**
 * hl_mmu_hr_fini() - release the MMU module.
 * @hdev: habanalabs device structure.
 * @hr_priv: MMU host resident private info.
 * @hop_table_size: HOP table size
 *
 * This function does the following:
 * - Disable MMU in H/W.
 * - Free the pgt_infos pool.
 *
 * All contexts should be freed before calling this function.
 */
void hl_mmu_hr_fini(struct hl_device *hdev, struct hl_mmu_hr_priv *hr_priv, u32 hop_table_size)
{
	/* MMU H/W fini was already done in device hw_fini() */

	hl_mmu_hr_pool_destroy(hdev, hr_priv, hop_table_size);

	if (!ZERO_OR_NULL_PTR(hr_priv->mmu_asid_hop0)) {
		kvfree(hr_priv->mmu_asid_hop0);

		/* Make sure that if we arrive here again without init was
		 * called we won't cause kernel panic. This can happen for
		 * example if we fail during hard reset code at certain points
		 */
		hr_priv->mmu_asid_hop0 = NULL;
	}
}

/**
 * hl_mmu_hr_free_hop_remove_pgt() - free HOP and remove PGT from hash
 * @pgt_info: page table info structure.
 * @hr_priv: MMU HR private data.
 * @hop_table_size: HOP table size.
 */
void hl_mmu_hr_free_hop_remove_pgt(struct pgt_info *pgt_info, struct hl_mmu_hr_priv *hr_priv,
					u32 hop_table_size)
{
	gen_pool_free(hr_priv->mmu_pgt_pool, pgt_info->virt_addr, hop_table_size);
	hash_del(&pgt_info->node);
	kfree(pgt_info);
}

/**
 * hl_mmu_hr_pte_phys_to_virt() - translate PTE phys addr to virt addr
 * @ctx: pointer to the context structure
 * @pgt: pgt_info for the HOP hosting the PTE
 * @phys_pte_addr: phys address of the PTE
 * @hop_table_size: HOP table size
 *
 * @return PTE virtual address
 *
 * The function use the pgt_info to get HOP base virt addr and obtain the PTE's virt addr
 * by adding the PTE offset.
 */
u64 hl_mmu_hr_pte_phys_to_virt(struct hl_ctx *ctx, struct pgt_info *pgt,
							u64 phys_pte_addr, u32 hop_table_size)
{
	u64 page_mask = (hop_table_size - 1);
	u64 pte_offset = phys_pte_addr & page_mask;

	return pgt->virt_addr + pte_offset;
}

/**
 * hl_mmu_hr_write_pte() - write HR PTE
 * @ctx: pointer to the context structure
 * @pgt_info: HOP's page table info structure
 * @phys_pte_addr: phys PTE address
 * @val: raw PTE data
 * @hop_table_size: HOP table size
 */
void hl_mmu_hr_write_pte(struct hl_ctx *ctx, struct pgt_info *pgt_info, u64 phys_pte_addr,
								u64 val, u32 hop_table_size)
{
	/*
	 * The value to write is the phys address of the next hop +
	 * flags at the 12 LSBs.
	 */
	u64 virt_addr = hl_mmu_hr_pte_phys_to_virt(ctx, pgt_info, phys_pte_addr, hop_table_size);

	*((u64 *) (uintptr_t) virt_addr) = val;
}

/**
 * hl_mmu_hr_clear_pte() - clear HR PTE
 * @ctx: pointer to the context structure
 * @pgt_info: HOP's page table info structure
 * @phys_pte_addr: phys PTE address
 * @hop_table_size: HOP table size
 */
void hl_mmu_hr_clear_pte(struct hl_ctx *ctx, struct pgt_info *pgt_info, u64 phys_pte_addr,
						u32 hop_table_size)
{
	/* no need to transform the value to physical address */
	hl_mmu_hr_write_pte(ctx, pgt_info, phys_pte_addr, 0, hop_table_size);
}

/**
 * hl_mmu_hr_put_pte() - put HR PTE and remove it if necessary (no more PTEs)
 * @ctx: pointer to the context structure
 * @pgt_info: HOP's page table info structure
 * @hr_priv: HR MMU private info
 * @hop_table_size: HOP table size
 *
 * @return number of PTEs still in the HOP
 */
int hl_mmu_hr_put_pte(struct hl_ctx *ctx, struct pgt_info *pgt_info,
						struct hl_mmu_hr_priv *hr_priv,
						u32 hop_table_size)
{
	int num_of_ptes_left;

	pgt_info->num_of_ptes--;

	/*
	 * Need to save the number of ptes left because free_hop might free
	 * the pgt_info
	 */
	num_of_ptes_left = pgt_info->num_of_ptes;
	if (!num_of_ptes_left)
		hl_mmu_hr_free_hop_remove_pgt(pgt_info, hr_priv, hop_table_size);

	return num_of_ptes_left;
}

/**
 * hl_mmu_hr_get_pte() - increase PGT PTE count
 * @ctx: pointer to the context structure
 * @hr_func: host resident functions
 * @phys_hop_addr: HOP phys address
 */
void hl_mmu_hr_get_pte(struct hl_ctx *ctx, struct hl_hr_mmu_funcs *hr_func, u64 phys_hop_addr)
{
	hr_func->get_pgt_info(ctx, phys_hop_addr)->num_of_ptes++;
}

/**
 * hl_mmu_hr_get_next_hop_pgt_info() - get pgt_info structure for the next HOP
 * @ctx: pointer to the context structure.
 * @hr_func: host resident functions.
 * @curr_pte: current PTE value.
 *
 * @return pgt_info structure on success, otherwise NULL.
 */
struct pgt_info *hl_mmu_hr_get_next_hop_pgt_info(struct hl_ctx *ctx,
							struct hl_hr_mmu_funcs *hr_func,
							u64 curr_pte)
{
	u64 next_hop_phys_addr = hl_mmu_get_next_hop_addr(ctx, curr_pte);

	if (next_hop_phys_addr == ULLONG_MAX)
		return NULL;

	return hr_func->get_pgt_info(ctx, next_hop_phys_addr);
}

/**
 * hl_mmu_hr_alloc_hop() - allocate HOP
 * @ctx: pointer to the context structure.
 * @hr_priv: host resident private info structure.
 * @hr_func: host resident functions.
 * @mmu_prop: MMU properties.
 *
 * @return pgt_info structure associated with the allocated HOP on success, otherwise NULL.
 */
struct pgt_info *hl_mmu_hr_alloc_hop(struct hl_ctx *ctx, struct hl_mmu_hr_priv *hr_priv,
							struct hl_hr_mmu_funcs *hr_func,
							struct hl_mmu_properties *mmu_prop)
{
	struct hl_device *hdev = ctx->hdev;
	struct pgt_info *pgt_info;
	dma_addr_t phys_addr;
	void *virt_addr;
	int i, retry = 1;

	pgt_info = kmalloc(sizeof(*pgt_info), GFP_KERNEL);
	if (!pgt_info)
		return NULL;

	for (i = 0; i <= retry; i++) {
		virt_addr = gen_pool_dma_zalloc_align(hr_priv->mmu_pgt_pool,
							mmu_prop->hop_table_size,
							&phys_addr,
							mmu_prop->hop_table_size);
		if (virt_addr)
			break;

		/* No memory in pool - get some and try again */
		virt_addr = hl_asic_dma_alloc_coherent(hdev, SZ_2M, &phys_addr,
							GFP_KERNEL | __GFP_ZERO);
		if (ZERO_OR_NULL_PTR(virt_addr))
			break;

		if (gen_pool_add_virt(hr_priv->mmu_pgt_pool, (unsigned long)virt_addr,
								phys_addr, SZ_2M, -1)) {
			hl_asic_dma_free_coherent(hdev, SZ_2M, virt_addr, phys_addr);
			virt_addr = NULL;
			break;
		}
	}

	if (ZERO_OR_NULL_PTR(virt_addr)) {
		dev_err(hdev->dev, "failed to allocate page\n");
		goto pool_alloc_err;
	}

	pgt_info->phys_addr = phys_addr;
	pgt_info->shadow_addr = (unsigned long) NULL;
	pgt_info->virt_addr = (unsigned long)virt_addr;
	pgt_info->ctx = ctx;
	pgt_info->num_of_ptes = 0;
	hr_func->add_pgt_info(ctx, pgt_info, phys_addr);

	return pgt_info;

pool_alloc_err:
	kfree(pgt_info);

	return NULL;
}

/**
 * hl_mmu_hr_get_alloc_next_hop() - get the next HOP, allocate it if it does not exist
 * @ctx: pointer to the context structure.
 * @hr_priv: host resident private info structure.
 * @hr_func: host resident functions.
 * @mmu_prop: MMU properties.
 * @curr_pte: current PTE value.
 * @is_new_hop: set to true if HOP is new (caller responsibility to set it to false).
 *
 * @return pgt_info structure associated with the allocated HOP on success, otherwise NULL.
 */
struct pgt_info *hl_mmu_hr_get_alloc_next_hop(struct hl_ctx *ctx,
							struct hl_mmu_hr_priv *hr_priv,
							struct hl_hr_mmu_funcs *hr_func,
							struct hl_mmu_properties *mmu_prop,
							u64 curr_pte, bool *is_new_hop)
{
	u64 hop_addr = hl_mmu_get_next_hop_addr(ctx, curr_pte);

	if (hop_addr != ULLONG_MAX)
		return hr_func->get_pgt_info(ctx, hop_addr);

	*is_new_hop = true;
	return hl_mmu_hr_alloc_hop(ctx, hr_priv, hr_func, mmu_prop);
}

/**
 * hl_mmu_hr_get_tlb_info() - get the TLB info (info for a specific mapping)
 * @ctx: pointer to the context structure.
 * @virt_addr: the virt address for which to get info.
 * @hops: HOPs info structure.
 * @hr_func: host resident functions.
 *
 * @return 0 on success, otherwise non 0 error code..
 */
int hl_mmu_hr_get_tlb_info(struct hl_ctx *ctx, u64 virt_addr, struct hl_mmu_hop_info *hops,
								struct hl_hr_mmu_funcs *hr_func)
{
	/* using 6 HOPs as this is the maximum number of HOPs */
	struct pgt_info *hops_pgt_info[MMU_ARCH_6_HOPS] = { NULL };
	struct hl_device *hdev = ctx->hdev;
	struct hl_mmu_properties *mmu_prop;
	int rc, i, used_hops;
	bool is_huge;

	rc = hr_func->get_tlb_mapping_params(hdev, &mmu_prop, hops, virt_addr, &is_huge);
	if (rc)
		return rc;

	used_hops = mmu_prop->num_hops;

	/* huge pages use one less hop */
	if (is_huge)
		used_hops--;

	hops->scrambled_vaddr = hdev->asic_funcs->scramble_addr(hdev, virt_addr);

	for (i = 0 ; i < used_hops ; i++) {
		if (i == 0)
			hops_pgt_info[i] = hr_func->get_hop0_pgt_info(ctx);
		else
			hops_pgt_info[i] = hl_mmu_hr_get_next_hop_pgt_info(ctx, hr_func,
								hops->hop_info[i - 1].hop_pte_val);

		if (!hops_pgt_info[i])
			return -EFAULT;

		hops->hop_info[i].hop_addr = hops_pgt_info[i]->phys_addr;
		hops->hop_info[i].hop_pte_addr =
				hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop, i,
								hops->hop_info[i].hop_addr,
								hops->scrambled_vaddr);
		hops->hop_info[i].hop_pte_val = *(u64 *) (uintptr_t)
						hl_mmu_hr_pte_phys_to_virt(ctx, hops_pgt_info[i],
								hops->hop_info[i].hop_pte_addr,
								mmu_prop->hop_table_size);

		if (!(hops->hop_info[i].hop_pte_val & PAGE_PRESENT_MASK))
			return -EFAULT;

		if (hops->hop_info[i].hop_pte_val & mmu_prop->last_mask)
			break;
	}

	/* if passed over all hops then no last hop was found */
	if (i == mmu_prop->num_hops)
		return -EFAULT;

	if (hops->scrambled_vaddr != virt_addr)
		hops->unscrambled_paddr = hdev->asic_funcs->descramble_addr
				(hdev, hops->hop_info[i].hop_pte_val);
	else
		hops->unscrambled_paddr = hops->hop_info[i].hop_pte_val;

	hops->used_hops = i + 1;

	return 0;
}

struct pgt_info *hl_mmu_dr_get_pgt_info(struct hl_ctx *ctx, u64 hop_addr)
{
	struct pgt_info *pgt_info = NULL;

	hash_for_each_possible(ctx->mmu_shadow_hash, pgt_info, node,
			(unsigned long) hop_addr)
		if (hop_addr == pgt_info->shadow_addr)
			break;

	return pgt_info;
}

void hl_mmu_dr_free_hop(struct hl_ctx *ctx, u64 hop_addr)
{
	struct pgt_info *pgt_info = hl_mmu_dr_get_pgt_info(ctx, hop_addr);

	hl_mmu_dr_free_pgt_node(ctx, pgt_info);
}

void hl_mmu_dr_free_pgt_node(struct hl_ctx *ctx, struct pgt_info *pgt_info)
{
	struct hl_device *hdev = ctx->hdev;

	gen_pool_free(hdev->mmu_priv.dr.mmu_pgt_pool, pgt_info->phys_addr,
			hdev->asic_prop.dmmu.hop_table_size);
	hash_del(&pgt_info->node);
	kfree((u64 *) (uintptr_t) pgt_info->shadow_addr);
	kfree(pgt_info);
}

u64 hl_mmu_dr_get_phys_hop0_addr(struct hl_ctx *ctx)
{
	return ctx->hdev->asic_prop.mmu_pgt_addr +
			(ctx->asid * ctx->hdev->asic_prop.dmmu.hop_table_size);
}

u64 hl_mmu_dr_get_hop0_addr(struct hl_ctx *ctx)
{
	return (u64) (uintptr_t) ctx->hdev->mmu_priv.dr.mmu_shadow_hop0 +
			(ctx->asid * ctx->hdev->asic_prop.dmmu.hop_table_size);
}

u64 hl_mmu_dr_get_phys_addr(struct hl_ctx *ctx, u64 shadow_addr)
{
	u64 page_mask = ctx->hdev->asic_prop.dmmu.hop_table_size - 1;
	u64 shadow_hop_addr = shadow_addr & (~page_mask);
	u64 pte_offset = shadow_addr & page_mask;
	u64 phys_hop_addr;

	if (shadow_hop_addr != hl_mmu_dr_get_hop0_addr(ctx))
		phys_hop_addr = hl_mmu_dr_get_pgt_info(ctx, shadow_hop_addr)->phys_addr;
	else
		phys_hop_addr = hl_mmu_dr_get_phys_hop0_addr(ctx);

	return phys_hop_addr + pte_offset;
}

void hl_mmu_dr_write_pte(struct hl_ctx *ctx, u64 shadow_pte_addr, u64 val)
{
	u64 phys_val = hl_mmu_dr_get_phys_addr(ctx, val);

	ctx->hdev->asic_funcs->write_pte(ctx->hdev, hl_mmu_dr_get_phys_addr(ctx, shadow_pte_addr),
					phys_val);

	*(u64 *) (uintptr_t) shadow_pte_addr = val;
}

void hl_mmu_dr_write_final_pte(struct hl_ctx *ctx, u64 shadow_pte_addr, u64 val)
{
	ctx->hdev->asic_funcs->write_pte(ctx->hdev,
				hl_mmu_dr_get_phys_addr(ctx, shadow_pte_addr), val);
	*(u64 *) (uintptr_t) shadow_pte_addr = val;
}

void hl_mmu_dr_clear_pte(struct hl_ctx *ctx, u64 pte_addr)
{
	hl_mmu_dr_write_final_pte(ctx, pte_addr, 0);
}

void hl_mmu_dr_get_pte(struct hl_ctx *ctx, u64 hop_addr)
{
	hl_mmu_dr_get_pgt_info(ctx, hop_addr)->num_of_ptes++;
}

int hl_mmu_dr_put_pte(struct hl_ctx *ctx, u64 hop_addr)
{
	struct pgt_info *pgt_info = hl_mmu_dr_get_pgt_info(ctx, hop_addr);
	int num_of_ptes_left;

	pgt_info->num_of_ptes--;

	/*
	 * Need to save the number of ptes left because hl_mmu_free_hop might free
	 * the pgt_info
	 */
	num_of_ptes_left = pgt_info->num_of_ptes;
	if (!num_of_ptes_left)
		hl_mmu_dr_free_pgt_node(ctx, pgt_info);

	return num_of_ptes_left;
}

u64 hl_mmu_dr_alloc_hop(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct pgt_info *pgt_info;
	u64 phys_addr, shadow_addr;

	pgt_info = kmalloc(sizeof(*pgt_info), GFP_KERNEL);
	if (!pgt_info)
		return ULLONG_MAX;

	phys_addr = (u64) gen_pool_alloc(hdev->mmu_priv.dr.mmu_pgt_pool,
					prop->dmmu.hop_table_size);
	if (!phys_addr) {
		dev_err(hdev->dev, "failed to allocate page\n");
		goto pool_add_err;
	}

	shadow_addr = (u64) (uintptr_t) kzalloc(prop->dmmu.hop_table_size,
						GFP_KERNEL);
	if (!shadow_addr)
		goto shadow_err;

	pgt_info->phys_addr = phys_addr;
	pgt_info->shadow_addr = shadow_addr;
	pgt_info->ctx = ctx;
	pgt_info->num_of_ptes = 0;
	hash_add(ctx->mmu_shadow_hash, &pgt_info->node, shadow_addr);

	return shadow_addr;

shadow_err:
	gen_pool_free(hdev->mmu_priv.dr.mmu_pgt_pool,
			phys_addr, prop->dmmu.hop_table_size);
pool_add_err:
	kfree(pgt_info);

	return ULLONG_MAX;
}

u64 hl_mmu_dr_get_alloc_next_hop_addr(struct hl_ctx *ctx, u64 curr_pte, bool *is_new_hop)
{
	u64 hop_addr = hl_mmu_get_next_hop_addr(ctx, curr_pte);

	if (hop_addr == ULLONG_MAX) {
		hop_addr = hl_mmu_dr_alloc_hop(ctx);
		*is_new_hop = (hop_addr != ULLONG_MAX);
	}

	return hop_addr;
}

void hl_mmu_dr_flush(struct hl_ctx *ctx)
{
	/* flush all writes from all cores to reach PCI */
	mb();
	ctx->hdev->asic_funcs->read_pte(ctx->hdev, hl_mmu_dr_get_phys_hop0_addr(ctx));
}

int hl_mmu_dr_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int rc;

	hdev->mmu_priv.dr.mmu_pgt_pool =
			gen_pool_create(__ffs(prop->dmmu.hop_table_size), -1);

	if (!hdev->mmu_priv.dr.mmu_pgt_pool) {
		dev_err(hdev->dev, "Failed to create page gen pool\n");
		return -ENOMEM;
	}

	rc = gen_pool_add(hdev->mmu_priv.dr.mmu_pgt_pool, prop->mmu_pgt_addr +
			prop->dmmu.hop0_tables_total_size,
			prop->dmmu.pgt_size - prop->dmmu.hop0_tables_total_size,
			-1);
	if (rc) {
		dev_err(hdev->dev, "Failed to add memory to page gen pool\n");
		goto err_pool_add;
	}

	hdev->mmu_priv.dr.mmu_shadow_hop0 = kvcalloc(prop->max_asid,
						prop->dmmu.hop_table_size, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(hdev->mmu_priv.dr.mmu_shadow_hop0)) {
		rc = -ENOMEM;
		goto err_pool_add;
	}

	/* MMU H/W init will be done in device hw_init() */

	return 0;

err_pool_add:
	gen_pool_destroy(hdev->mmu_priv.dr.mmu_pgt_pool);

	return rc;
}

void hl_mmu_dr_fini(struct hl_device *hdev)
{
	/* MMU H/W fini was already done in device hw_fini() */

	if (ZERO_OR_NULL_PTR(hdev->mmu_priv.dr.mmu_shadow_hop0))
		return;

	kvfree(hdev->mmu_priv.dr.mmu_shadow_hop0);
	gen_pool_destroy(hdev->mmu_priv.dr.mmu_pgt_pool);

	/* Make sure that if we arrive here again without init was
	 * called we won't cause kernel panic. This can happen for
	 * example if we fail during hard reset code at certain points
	 */
	hdev->mmu_priv.dr.mmu_shadow_hop0 = NULL;
}
