// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2020-2022 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "../habanalabs.h"
#include "../../include/hw_ip/mmu/mmu_general.h"

#include <linux/slab.h>

static struct pgt_info *hl_mmu_v2_hr_get_pgt_info(struct hl_ctx *ctx, u64 phys_hop_addr)
{
	struct pgt_info *pgt_info = NULL;

	hash_for_each_possible(ctx->hr_mmu_phys_hash, pgt_info, node,
				(unsigned long) phys_hop_addr)
		if (phys_hop_addr == pgt_info->phys_addr)
			break;

	return pgt_info;
}

static void hl_mmu_v2_hr_add_pgt_info(struct hl_ctx *ctx, struct pgt_info *pgt_info,
					dma_addr_t phys_addr)
{
	hash_add(ctx->hr_mmu_phys_hash, &pgt_info->node, phys_addr);
}

static struct pgt_info *hl_mmu_v2_hr_get_hop0_pgt_info(struct hl_ctx *ctx)
{
	return &ctx->hdev->mmu_priv.hr.mmu_asid_hop0[ctx->asid];
}

/**
 * hl_mmu_v2_hr_init() - initialize the MMU module.
 * @hdev: habanalabs device structure.
 *
 * This function does the following:
 * - Create a pool of pages for pgt_infos.
 * - Create a shadow table for pgt
 *
 * Return: 0 for success, non-zero for failure.
 */
static inline int hl_mmu_v2_hr_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;

	return hl_mmu_hr_init(hdev, &hdev->mmu_priv.hr, prop->mmu_hop_table_size,
				prop->mmu_pgt_size);
}

/**
 * hl_mmu_v2_hr_fini() - release the MMU module.
 * @hdev: habanalabs device structure.
 *
 * This function does the following:
 * - Disable MMU in H/W.
 * - Free the pgt_infos pool.
 *
 * All contexts should be freed before calling this function.
 */
static inline void hl_mmu_v2_hr_fini(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;

	hl_mmu_hr_fini(hdev, &hdev->mmu_priv.hr, prop->mmu_hop_table_size);
}

/**
 * hl_mmu_v2_hr_ctx_init() - initialize a context for using the MMU module.
 * @ctx: pointer to the context structure to initialize.
 *
 * Initialize a mutex to protect the concurrent mapping flow, a hash to hold all
 * page tables hops related to this context.
 * Return: 0 on success, non-zero otherwise.
 */
static int hl_mmu_v2_hr_ctx_init(struct hl_ctx *ctx)
{
	hash_init(ctx->hr_mmu_phys_hash);
	return 0;
}

/*
 * hl_mmu_v2_hr_ctx_fini - disable a ctx from using the mmu module
 *
 * @ctx: pointer to the context structure
 *
 * This function does the following:
 * - Free any pgts which were not freed yet
 * - Free the mutex
 * - Free DRAM default page mapping hops
 */
static void hl_mmu_v2_hr_ctx_fini(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct pgt_info *pgt_info;
	struct hlist_node *tmp;
	int i;

	if (!hash_empty(ctx->hr_mmu_phys_hash))
		dev_err(hdev->dev, "ctx %d is freed while it has pgts in use\n",
			ctx->asid);

	hash_for_each_safe(ctx->hr_mmu_phys_hash, i, tmp, pgt_info, node) {
		dev_err_ratelimited(hdev->dev,
			"pgt_info of addr 0x%llx of asid %d was not destroyed, num_ptes: %d\n",
			pgt_info->phys_addr, ctx->asid, pgt_info->num_of_ptes);
		hl_mmu_hr_free_hop_remove_pgt(pgt_info, &ctx->hdev->mmu_priv.hr,
							ctx->hdev->asic_prop.mmu_hop_table_size);
	}
}

static int _hl_mmu_v2_hr_unmap(struct hl_ctx *ctx,
				u64 virt_addr, bool is_dram_addr)
{
	u64 curr_pte, scrambled_virt_addr, hop_pte_phys_addr[MMU_ARCH_6_HOPS] = { 0 };
	struct pgt_info *hops_pgt_info[MMU_ARCH_6_HOPS] = { NULL };
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop;
	struct hl_mmu_properties *mmu_prop;
	bool is_huge = false;
	int i, hop_last;

	prop = &hdev->asic_prop;

	/* shifts and masks are the same in PMMU and HMMU, use one of them */
	mmu_prop = is_dram_addr ? &prop->dmmu : &prop->pmmu;
	hop_last = mmu_prop->num_hops - 1;

	scrambled_virt_addr = hdev->asic_funcs->scramble_addr(hdev, virt_addr);
	curr_pte = 0;

	for (i = 0 ; i < mmu_prop->num_hops ; i++) {
		/* we get HOP0 differently, it doesn't need curr_pte */
		if (i == 0)
			hops_pgt_info[i] = hl_mmu_v2_hr_get_hop0_pgt_info(ctx);
		else
			hops_pgt_info[i] = hl_mmu_hr_get_next_hop_pgt_info(ctx,
					&ctx->hdev->mmu_func[MMU_HR_PGT].hr_funcs, curr_pte);
		if (!hops_pgt_info[i])
			goto not_mapped;

		hop_pte_phys_addr[i] = hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop, i,
									hops_pgt_info[i]->phys_addr,
									scrambled_virt_addr);
		if (hop_pte_phys_addr[i] == U64_MAX)
			return -EFAULT;

		curr_pte = *(u64 *) (uintptr_t) hl_mmu_hr_pte_phys_to_virt(ctx, hops_pgt_info[i],
							hop_pte_phys_addr[i],
							ctx->hdev->asic_prop.mmu_hop_table_size);

		if ((i < hop_last) && (curr_pte & mmu_prop->last_mask)) {
			hop_last = i;
			is_huge = true;
			break;
		}
	}

	if (is_dram_addr && !is_huge) {
		dev_err(hdev->dev, "DRAM unmapping should use huge pages only\n");
		return -EFAULT;
	}

	if (!(curr_pte & PAGE_PRESENT_MASK))
		goto not_mapped;

	for (i = hop_last ; i > 0 ; i--) {
		hl_mmu_hr_clear_pte(ctx, hops_pgt_info[i], hop_pte_phys_addr[i],
						ctx->hdev->asic_prop.mmu_hop_table_size);

		if (hl_mmu_hr_put_pte(ctx, hops_pgt_info[i], &ctx->hdev->mmu_priv.hr,
						ctx->hdev->asic_prop.mmu_hop_table_size))
			goto mapped;
	}
	hl_mmu_hr_clear_pte(ctx, hops_pgt_info[0], hop_pte_phys_addr[0],
						ctx->hdev->asic_prop.mmu_hop_table_size);

mapped:
	return 0;

not_mapped:
	dev_err(hdev->dev, "virt addr 0x%llx is not mapped to phys addr\n", virt_addr);

	return -EINVAL;
}

static int hl_mmu_v2_get_last_hop(struct hl_mmu_properties *mmu_prop, u32 page_size)
{
	int hop;

	for (hop = (mmu_prop->num_hops - 1); hop; hop--) {
		if (mmu_prop->hop_shifts[hop] == 0)
			continue;

		if (page_size <= (1 << mmu_prop->hop_shifts[hop]))
			break;
	}

	return hop;
}

static int _hl_mmu_v2_hr_map(struct hl_ctx *ctx,
			u64 virt_addr, u64 phys_addr,
			u32 page_size, bool is_dram_addr)
{
	u64 hop_pte_phys_addr[MMU_ARCH_6_HOPS] = { 0 },
		curr_pte = 0, scrambled_virt_addr, scrambled_phys_addr;
	struct pgt_info *hops_pgt_info[MMU_ARCH_6_HOPS] = { NULL };
	bool hop_new[MMU_ARCH_6_HOPS] = { false };
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct hl_mmu_properties *mmu_prop;
	int i, hop_last, rc = -ENOMEM;

	/*
	 * This mapping function can map a page or a huge page. For huge page
	 * there are only 4 hops rather than 5. Currently the DRAM allocation
	 * uses huge pages only but user memory could have been allocated with
	 * one of the two page sizes. Since this is a common code for all the
	 * three cases, we need this hugs page check.
	 */
	if (is_dram_addr)
		mmu_prop = &prop->dmmu;
	else if (page_size == prop->pmmu_huge.page_size)
		mmu_prop = &prop->pmmu_huge;
	else
		mmu_prop = &prop->pmmu;

	hop_last = hl_mmu_v2_get_last_hop(mmu_prop, page_size);
	if (hop_last <= 0) {
		dev_err(ctx->hdev->dev, "Invalid last HOP %d\n", hop_last);
		return -EFAULT;
	}

	scrambled_virt_addr = hdev->asic_funcs->scramble_addr(hdev, virt_addr);
	scrambled_phys_addr = hdev->asic_funcs->scramble_addr(hdev, phys_addr);

	for (i = 0 ; i <= hop_last ; i++) {

		if (i == 0)
			hops_pgt_info[i] = hl_mmu_v2_hr_get_hop0_pgt_info(ctx);
		else
			hops_pgt_info[i] = hl_mmu_hr_get_alloc_next_hop(ctx,
							&ctx->hdev->mmu_priv.hr,
							&ctx->hdev->mmu_func[MMU_HR_PGT].hr_funcs,
							mmu_prop, curr_pte, &hop_new[i]);
		if (!hops_pgt_info[i])
			goto err;

		hop_pte_phys_addr[i] = hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop, i,
									hops_pgt_info[i]->phys_addr,
									scrambled_virt_addr);
		curr_pte = *(u64 *) (uintptr_t) hl_mmu_hr_pte_phys_to_virt(ctx, hops_pgt_info[i],
							hop_pte_phys_addr[i],
							ctx->hdev->asic_prop.mmu_hop_table_size);
	}

	if (curr_pte & PAGE_PRESENT_MASK) {
		dev_err(hdev->dev, "mapping already exists for virt_addr 0x%llx\n",
									scrambled_virt_addr);

		for (i = 0 ; i <= hop_last ; i++)
			dev_dbg(hdev->dev, "hop%d pte: 0x%llx (0x%llx)\n",
					i,
					*(u64 *) (uintptr_t)
					hl_mmu_hr_pte_phys_to_virt(ctx, hops_pgt_info[i],
							hop_pte_phys_addr[i],
							ctx->hdev->asic_prop.mmu_hop_table_size),
					hop_pte_phys_addr[i]);
		rc = -EINVAL;
		goto err;
	}

	curr_pte = (scrambled_phys_addr & HOP_PHYS_ADDR_MASK) | mmu_prop->last_mask
			| PAGE_PRESENT_MASK;

	/* Write the PTEs */
	hl_mmu_hr_write_pte(ctx, hops_pgt_info[hop_last], hop_pte_phys_addr[hop_last], curr_pte,
							ctx->hdev->asic_prop.mmu_hop_table_size);

	/* for each new hop, add its address to the table of previous-hop */
	for (i = 1 ; i <= hop_last ; i++) {
		if (hop_new[i]) {
			curr_pte = (hops_pgt_info[i]->phys_addr & HOP_PHYS_ADDR_MASK) |
							PAGE_PRESENT_MASK;
			hl_mmu_hr_write_pte(ctx, hops_pgt_info[i - 1], hop_pte_phys_addr[i - 1],
						curr_pte, ctx->hdev->asic_prop.mmu_hop_table_size);
			if (i - 1)
				hl_mmu_hr_get_pte(ctx, &ctx->hdev->mmu_func[MMU_HR_PGT].hr_funcs,
								hops_pgt_info[i - 1]->phys_addr);
		}
	}

	hl_mmu_hr_get_pte(ctx, &ctx->hdev->mmu_func[MMU_HR_PGT].hr_funcs,
						hops_pgt_info[hop_last]->phys_addr);

	return 0;

err:
	for (i = 1 ; i <= hop_last ; i++)
		if (hop_new[i] && hops_pgt_info[i])
			hl_mmu_hr_free_hop_remove_pgt(hops_pgt_info[i], &ctx->hdev->mmu_priv.hr,
							ctx->hdev->asic_prop.mmu_hop_table_size);

	return rc;
}

/*
 * hl_mmu_v2_swap_out - marks all mapping of the given ctx as swapped out
 *
 * @ctx: pointer to the context structure
 *
 */
static void hl_mmu_v2_hr_swap_out(struct hl_ctx *ctx)
{

}

/*
 * hl_mmu_v2_swap_in - marks all mapping of the given ctx as swapped in
 *
 * @ctx: pointer to the context structure
 *
 */
static void hl_mmu_v2_hr_swap_in(struct hl_ctx *ctx)
{

}

static int hl_mmu_v2_hr_get_tlb_mapping_params(struct hl_device *hdev,
							struct hl_mmu_properties **mmu_prop,
							struct hl_mmu_hop_info *hops,
							u64 virt_addr, bool *is_huge)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	bool is_dram_addr, is_pmmu_addr, is_pmmu_h_addr;

	is_dram_addr = hl_mem_area_inside_range(virt_addr, prop->dmmu.page_size,
						prop->dmmu.start_addr,
						prop->dmmu.end_addr);
	is_pmmu_addr = hl_mem_area_inside_range(virt_addr, prop->pmmu.page_size,
						prop->pmmu.start_addr,
						prop->pmmu.end_addr);
	is_pmmu_h_addr = hl_mem_area_inside_range(virt_addr,
						prop->pmmu_huge.page_size,
						prop->pmmu_huge.start_addr,
						prop->pmmu_huge.end_addr);
	if (is_dram_addr) {
		*mmu_prop = &prop->dmmu;
		*is_huge = true;
		hops->range_type = HL_VA_RANGE_TYPE_DRAM;
	} else if (is_pmmu_addr) {
		*mmu_prop = &prop->pmmu;
		*is_huge = false;
		hops->range_type = HL_VA_RANGE_TYPE_HOST;
	} else if (is_pmmu_h_addr) {
		*mmu_prop = &prop->pmmu_huge;
		*is_huge = true;
		hops->range_type = HL_VA_RANGE_TYPE_HOST_HUGE;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int hl_mmu_v2_hr_get_tlb_info(struct hl_ctx *ctx, u64 virt_addr,
					struct hl_mmu_hop_info *hops)
{
	return hl_mmu_hr_get_tlb_info(ctx, virt_addr, hops,
					&ctx->hdev->mmu_func[MMU_HR_PGT].hr_funcs);
}

/*
 * hl_mmu_v2_prepare - prepare mmu_if for working with mmu v2
 *
 * @hdev: pointer to the device structure
 * @mmu_if: pointer to the mmu interface structure
 */
void hl_mmu_v2_hr_set_funcs(struct hl_device *hdev, struct hl_mmu_funcs *mmu)
{
	mmu->init = hl_mmu_v2_hr_init;
	mmu->fini = hl_mmu_v2_hr_fini;
	mmu->ctx_init = hl_mmu_v2_hr_ctx_init;
	mmu->ctx_fini = hl_mmu_v2_hr_ctx_fini;
	mmu->map = _hl_mmu_v2_hr_map;
	mmu->unmap = _hl_mmu_v2_hr_unmap;
	mmu->flush = hl_mmu_hr_flush;
	mmu->swap_out = hl_mmu_v2_hr_swap_out;
	mmu->swap_in = hl_mmu_v2_hr_swap_in;
	mmu->get_tlb_info = hl_mmu_v2_hr_get_tlb_info;
	mmu->hr_funcs.get_hop0_pgt_info = hl_mmu_v2_hr_get_hop0_pgt_info;
	mmu->hr_funcs.get_pgt_info = hl_mmu_v2_hr_get_pgt_info;
	mmu->hr_funcs.add_pgt_info = hl_mmu_v2_hr_add_pgt_info;
	mmu->hr_funcs.get_tlb_mapping_params = hl_mmu_v2_hr_get_tlb_mapping_params;
}
