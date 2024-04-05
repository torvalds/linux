// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "../habanalabs.h"
#include "../../include/hw_ip/mmu/mmu_general.h"
#include "../../include/hw_ip/mmu/mmu_v2_0.h"

#include <linux/slab.h>

/**
 * hl_mmu_v2_ctx_init() - initialize a context for using the MMU module.
 * @ctx: pointer to the context structure to initialize.
 *
 * Initialize a mutex to protect the concurrent mapping flow, a hash to hold all
 * page tables hops related to this context.
 * Return: 0 on success, non-zero otherwise.
 */
static int hl_mmu_v2_ctx_init(struct hl_ctx *ctx)
{
	hash_init(ctx->mmu_shadow_hash);

	return 0;
}

/*
 * hl_mmu_v2_ctx_fini - disable a ctx from using the mmu module
 *
 * @ctx: pointer to the context structure
 *
 * This function does the following:
 * - Free any pgts which were not freed yet
 * - Free the mutex
 * - Free DRAM default page mapping hops
 */
static void hl_mmu_v2_ctx_fini(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct pgt_info *pgt_info;
	struct hlist_node *tmp;
	int i;

	if (!hash_empty(ctx->mmu_shadow_hash))
		dev_err(hdev->dev, "ctx %d is freed while it has pgts in use\n",
			ctx->asid);

	hash_for_each_safe(ctx->mmu_shadow_hash, i, tmp, pgt_info, node) {
		dev_err_ratelimited(hdev->dev,
			"pgt_info of addr 0x%llx of asid %d was not destroyed, num_ptes: %d\n",
			pgt_info->phys_addr, ctx->asid, pgt_info->num_of_ptes);
		hl_mmu_dr_free_pgt_node(ctx, pgt_info);
	}
}

static int hl_mmu_v2_unmap(struct hl_ctx *ctx,	u64 virt_addr, bool is_dram_addr)
{
	u64 hop_addr[MMU_ARCH_6_HOPS] = { 0 }, hop_pte_addr[MMU_ARCH_6_HOPS] = { 0 }, curr_pte,
							scrambled_virt_addr;
	struct asic_fixed_properties *prop = &ctx->hdev->asic_prop;
	struct hl_device *hdev = ctx->hdev;
	struct hl_mmu_properties *mmu_prop;
	bool is_huge = false;
	int i, hop_last;

	/* device resident in V2 are allowed only for HMMU */
	if (!is_dram_addr)
		return -EINVAL;

	mmu_prop = &prop->dmmu;

	hop_last = mmu_prop->num_hops - 1;

	scrambled_virt_addr = hdev->asic_funcs->scramble_addr(hdev, virt_addr);

	hop_addr[0] = hl_mmu_dr_get_hop0_addr(ctx);
	hop_pte_addr[0] = hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop, 0,
					hop_addr[0], scrambled_virt_addr);
	if (hop_pte_addr[0] == U64_MAX)
		return -EFAULT;

	curr_pte = *(u64 *) (uintptr_t) hop_pte_addr[0];

	for (i = 1 ; i < mmu_prop->num_hops ; i++) {
		hop_addr[i] = hl_mmu_get_next_hop_addr(ctx, curr_pte);
		if (hop_addr[i] == ULLONG_MAX)
			goto not_mapped;

		hop_pte_addr[i] = hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop, i,
					hop_addr[i], scrambled_virt_addr);
		if (hop_pte_addr[i] == U64_MAX)
			return -EFAULT;

		curr_pte = *(u64 *) (uintptr_t) hop_pte_addr[i];

		if ((i <= hop_last) && (curr_pte & mmu_prop->last_mask)) {
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
		hl_mmu_dr_clear_pte(ctx, hop_pte_addr[i]);
		if (hl_mmu_dr_put_pte(ctx, hop_addr[i]))
			goto mapped;
	}
	hl_mmu_dr_clear_pte(ctx, hop_pte_addr[0]);

mapped:
	return 0;

not_mapped:
	dev_err(hdev->dev, "virt addr 0x%llx is not mapped to phys addr\n",
		virt_addr);

	return -EINVAL;
}

static int hl_mmu_v2_map(struct hl_ctx *ctx, u64 virt_addr, u64 phys_addr,
							u32 page_size, bool is_dram_addr)
{
	u64 hop_addr[MMU_ARCH_6_HOPS] = { 0 }, hop_pte_addr[MMU_ARCH_6_HOPS] = { 0 },
			curr_pte = 0, scrambled_virt_addr, scrambled_phys_addr;
	struct asic_fixed_properties *prop = &ctx->hdev->asic_prop;
	bool hop_new[MMU_ARCH_6_HOPS] = { false };
	struct hl_device *hdev = ctx->hdev;
	struct hl_mmu_properties *mmu_prop;
	int rc, i, hop_last;

	/* device resident in V2 are allowed only for HMMU */
	if (!is_dram_addr)
		return -EINVAL;

	mmu_prop = &prop->dmmu;

	hop_last = mmu_prop->num_hops - 1;

	scrambled_virt_addr = hdev->asic_funcs->scramble_addr(hdev, virt_addr);
	scrambled_phys_addr = hdev->asic_funcs->scramble_addr(hdev, phys_addr);

	/* First hop is preallocated therefore it is treated differently  */
	hop_addr[0] = hl_mmu_dr_get_hop0_addr(ctx);
	hop_pte_addr[0] = hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop, 0,
						hop_addr[0], scrambled_virt_addr);
	curr_pte = *(u64 *) (uintptr_t) hop_pte_addr[0];

	/* Handle hop1 to hop_last */
	for (i = 1 ; i <= hop_last ; i++) {
		hop_addr[i] = hl_mmu_dr_get_alloc_next_hop_addr(ctx, curr_pte, &hop_new[i]);
		if (hop_addr[i] == ULLONG_MAX) {
			rc = -ENOMEM;
			goto err;
		}

		hop_pte_addr[i] = hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop, i,
					hop_addr[i], scrambled_virt_addr);
		if (hop_pte_addr[i] == U64_MAX) {
			rc = -EINVAL;
			goto err;
		}

		if (!hop_pte_addr[i]) {
			rc = -EINVAL;
			goto err;
		}

		curr_pte = *(u64 *) (uintptr_t) hop_pte_addr[i];
	}

	if (curr_pte & PAGE_PRESENT_MASK) {
		dev_err(hdev->dev,
			"mapping already exists for virt_addr 0x%llx\n",
				virt_addr);

		for (i = 0 ; i <= hop_last ; i++)
			dev_dbg(hdev->dev, "hop%d pte: 0x%llx (0x%llx)\n",
				i, *(u64 *) (uintptr_t) hop_pte_addr[i],
				hop_pte_addr[i]);

		rc = -EINVAL;
		goto err;
	}

	curr_pte = (scrambled_phys_addr & HOP_PHYS_ADDR_MASK)
					| mmu_prop->last_mask | PAGE_PRESENT_MASK;

	/* Write the PTEs */
	hl_mmu_dr_write_final_pte(ctx, hop_pte_addr[hop_last], curr_pte);

	/* for each new hop, add its address to the table of previous-hop */
	for (i = 1 ; i <= hop_last ; i++) {
		if (hop_new[i]) {
			curr_pte = (hop_addr[i] & HOP_PHYS_ADDR_MASK) | PAGE_PRESENT_MASK;
			hl_mmu_dr_write_pte(ctx, hop_pte_addr[i - 1], curr_pte);

			if (i - 1)
				hl_mmu_dr_get_pte(ctx, hop_addr[i - 1]);
		}
	}
	hl_mmu_dr_get_pte(ctx, hop_addr[hop_last]);

	return 0;

err:
	for (i = 1 ; i <= hop_last ; i++)
		if (hop_new[i] && (hop_addr[i] != U64_MAX))
			hl_mmu_dr_free_hop(ctx, hop_addr[i]);

	return rc;
}

/*
 * hl_mmu_v2_swap_out - marks all mapping of the given ctx as swapped out
 *
 * @ctx: pointer to the context structure
 *
 */
static void hl_mmu_v2_swap_out(struct hl_ctx *ctx)
{

}

/*
 * hl_mmu_v2_swap_in - marks all mapping of the given ctx as swapped in
 *
 * @ctx: pointer to the context structure
 *
 */
static void hl_mmu_v2_swap_in(struct hl_ctx *ctx)
{

}

static int hl_mmu_v2_get_tlb_info(struct hl_ctx *ctx, u64 virt_addr, struct hl_mmu_hop_info *hops)
{
	struct asic_fixed_properties *prop = &ctx->hdev->asic_prop;
	struct hl_device *hdev = ctx->hdev;
	struct hl_mmu_properties *mmu_prop;
	bool is_dram_addr;
	int i;

	is_dram_addr = hl_mem_area_inside_range(virt_addr, prop->dmmu.page_size,
						prop->dmmu.start_addr,
						prop->dmmu.end_addr);

	/* device resident in V2 are allowed only for HMMU */
	if (!is_dram_addr)
		return -EINVAL;

	mmu_prop = &prop->dmmu;
	hops->range_type = HL_VA_RANGE_TYPE_DRAM;

	hops->scrambled_vaddr = hdev->asic_funcs->scramble_addr(hdev, virt_addr);

	hops->hop_info[0].hop_addr = hl_mmu_dr_get_phys_hop0_addr(ctx);
	hops->hop_info[0].hop_pte_addr = hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop, 0,
						hops->hop_info[0].hop_addr,
							hops->scrambled_vaddr);
	if (hops->hop_info[0].hop_pte_addr == U64_MAX)
		return -EFAULT;

	hops->hop_info[0].hop_pte_val = hdev->asic_funcs->read_pte(hdev,
						hops->hop_info[0].hop_pte_addr);
	if (hops->hop_info[0].hop_pte_val == U64_MAX)
		return -EFAULT;

	for (i = 1 ; i < mmu_prop->num_hops ; i++) {
		hops->hop_info[i].hop_addr =
			hl_mmu_get_next_hop_addr(ctx, hops->hop_info[i - 1].hop_pte_val);
		if (hops->hop_info[i].hop_addr == ULLONG_MAX)
			return -EFAULT;

		hops->hop_info[i].hop_pte_addr =
				hl_mmu_get_hop_pte_phys_addr(ctx, mmu_prop, i,
						hops->hop_info[i].hop_addr,
						hops->scrambled_vaddr);
		if (hops->hop_info[i].hop_pte_addr == U64_MAX)
			return -EFAULT;

		hops->hop_info[i].hop_pte_val =
				hdev->asic_funcs->read_pte(hdev,
					hops->hop_info[i].hop_pte_addr);

		if (!(hops->hop_info[i].hop_pte_val & PAGE_PRESENT_MASK))
			return -EFAULT;

		if (hops->hop_info[i].hop_pte_val & mmu_prop->last_mask)
			break;
	}

	/* if passed over all hops then no last hop was found */
	if (i == mmu_prop->num_hops)
		return -EFAULT;

	if (!(hops->hop_info[i].hop_pte_val & PAGE_PRESENT_MASK))
		return -EFAULT;

	if (hops->scrambled_vaddr != virt_addr)
		hops->unscrambled_paddr = hdev->asic_funcs->descramble_addr
				(hdev, hops->hop_info[i].hop_pte_val);
	else
		hops->unscrambled_paddr = hops->hop_info[i].hop_pte_val;

	hops->used_hops = i + 1;

	return 0;
}

/*
 * hl_mmu_v2_prepare - prepare mmu_if for working with mmu v2
 *
 * @hdev: pointer to the device structure
 * @mmu_if: pointer to the mmu interface structure
 */
void hl_mmu_v2_set_funcs(struct hl_device *hdev, struct hl_mmu_funcs *mmu)
{
	mmu->init = hl_mmu_dr_init;
	mmu->fini = hl_mmu_dr_fini;
	mmu->ctx_init = hl_mmu_v2_ctx_init;
	mmu->ctx_fini = hl_mmu_v2_ctx_fini;
	mmu->map = hl_mmu_v2_map;
	mmu->unmap = hl_mmu_v2_unmap;
	mmu->flush = hl_mmu_dr_flush;
	mmu->swap_out = hl_mmu_v2_swap_out;
	mmu->swap_in = hl_mmu_v2_swap_in;
	mmu->get_tlb_info = hl_mmu_v2_get_tlb_info;
}
