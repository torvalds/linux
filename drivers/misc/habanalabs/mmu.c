// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 */

#include "habanalabs.h"
#include "include/hw_ip/mmu/mmu_general.h"

#include <linux/genalloc.h>
#include <linux/slab.h>

static struct pgt_info *get_pgt_info(struct hl_ctx *ctx, u64 addr)
{
	struct pgt_info *pgt_info = NULL;

	hash_for_each_possible(ctx->mmu_hash, pgt_info, node,
				(unsigned long) addr)
		if (addr == pgt_info->addr)
			break;

	return pgt_info;
}

static void free_hop(struct hl_ctx *ctx, u64 hop_addr)
{
	struct pgt_info *pgt_info = get_pgt_info(ctx, hop_addr);

	gen_pool_free(pgt_info->ctx->hdev->mmu_pgt_pool, pgt_info->addr,
			ctx->hdev->asic_prop.mmu_hop_table_size);
	hash_del(&pgt_info->node);

	kfree(pgt_info);
}

static u64 alloc_hop(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct pgt_info *pgt_info;
	u64 addr;

	pgt_info = kmalloc(sizeof(*pgt_info), GFP_KERNEL);
	if (!pgt_info)
		return ULLONG_MAX;

	addr = (u64) gen_pool_alloc(hdev->mmu_pgt_pool,
			hdev->asic_prop.mmu_hop_table_size);
	if (!addr) {
		dev_err(hdev->dev, "failed to allocate page\n");
		kfree(pgt_info);
		return ULLONG_MAX;
	}

	pgt_info->addr = addr;
	pgt_info->ctx = ctx;
	pgt_info->num_of_ptes = 0;
	hash_add(ctx->mmu_hash, &pgt_info->node, addr);

	return addr;
}

static inline void clear_pte(struct hl_device *hdev, u64 pte_addr)
{
	/* clear the last and present bits */
	hdev->asic_funcs->write_pte(hdev, pte_addr, 0);
}

static inline void get_pte(struct hl_ctx *ctx, u64 hop_addr)
{
	get_pgt_info(ctx, hop_addr)->num_of_ptes++;
}

/*
 * put_pte - decrement the num of ptes and free the hop if possible
 *
 * @ctx: pointer to the context structure
 * @hop_addr: addr of the hop
 *
 * This function returns the number of ptes left on this hop. If the number is
 * 0, it means the pte was freed.
 */
static inline int put_pte(struct hl_ctx *ctx, u64 hop_addr)
{
	struct pgt_info *pgt_info = get_pgt_info(ctx, hop_addr);
	int num_of_ptes_left;

	pgt_info->num_of_ptes--;

	/*
	 * Need to save the number of ptes left because free_hop might free
	 * the pgt_info
	 */
	num_of_ptes_left = pgt_info->num_of_ptes;
	if (!num_of_ptes_left)
		free_hop(ctx, hop_addr);

	return num_of_ptes_left;
}

static inline u64 get_hop0_addr(struct hl_ctx *ctx)
{
	return ctx->hdev->asic_prop.mmu_pgt_addr +
			(ctx->asid * ctx->hdev->asic_prop.mmu_hop_table_size);
}

static inline u64 get_hopN_pte_addr(struct hl_ctx *ctx, u64 hop_addr,
					u64 virt_addr, u64 mask, u64 shift)
{
	return hop_addr + ctx->hdev->asic_prop.mmu_pte_size *
			((virt_addr & mask) >> shift);
}

static inline u64 get_hop0_pte_addr(struct hl_ctx *ctx, u64 hop_addr, u64 vaddr)
{
	return get_hopN_pte_addr(ctx, hop_addr, vaddr, HOP0_MASK, HOP0_SHIFT);
}

static inline u64 get_hop1_pte_addr(struct hl_ctx *ctx, u64 hop_addr, u64 vaddr)
{
	return get_hopN_pte_addr(ctx, hop_addr, vaddr, HOP1_MASK, HOP1_SHIFT);
}

static inline u64 get_hop2_pte_addr(struct hl_ctx *ctx, u64 hop_addr, u64 vaddr)
{
	return get_hopN_pte_addr(ctx, hop_addr, vaddr, HOP2_MASK, HOP2_SHIFT);
}

static inline u64 get_hop3_pte_addr(struct hl_ctx *ctx, u64 hop_addr, u64 vaddr)
{
	return get_hopN_pte_addr(ctx, hop_addr, vaddr, HOP3_MASK, HOP3_SHIFT);
}

static inline u64 get_hop4_pte_addr(struct hl_ctx *ctx, u64 hop_addr, u64 vaddr)
{
	return get_hopN_pte_addr(ctx, hop_addr, vaddr, HOP4_MASK, HOP4_SHIFT);
}

static inline u64 get_next_hop_addr(u64 curr_pte)
{
	if (curr_pte & PAGE_PRESENT_MASK)
		return curr_pte & PHYS_ADDR_MASK;
	else
		return ULLONG_MAX;
}

static inline u64 get_alloc_next_hop_addr(struct hl_ctx *ctx, u64 curr_pte,
						bool *is_new_hop)
{
	u64 hop_addr = get_next_hop_addr(curr_pte);

	if (hop_addr == ULLONG_MAX) {
		hop_addr = alloc_hop(ctx);
		*is_new_hop = (hop_addr != ULLONG_MAX);
	}

	return hop_addr;
}

/*
 * hl_mmu_init - init the mmu module
 *
 * @hdev: pointer to the habanalabs device structure
 *
 * This function does the following:
 * - Allocate max_asid zeroed hop0 pgts so no mapping is available
 * - Enable mmu in hw
 * - Invalidate the mmu cache
 * - Create a pool of pages for pgts
 * - Returns 0 on success
 *
 * This function depends on DMA QMAN to be working!
 */
int hl_mmu_init(struct hl_device *hdev)
{
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	int rc;

	if (!hdev->mmu_enable)
		return 0;

	/* MMU HW init was already done in device hw_init() */

	mutex_init(&hdev->mmu_cache_lock);

	hdev->mmu_pgt_pool =
			gen_pool_create(__ffs(prop->mmu_hop_table_size), -1);

	if (!hdev->mmu_pgt_pool) {
		dev_err(hdev->dev, "Failed to create page gen pool\n");
		rc = -ENOMEM;
		goto err_pool_create;
	}

	rc = gen_pool_add(hdev->mmu_pgt_pool, prop->mmu_pgt_addr +
			prop->mmu_hop0_tables_total_size,
			prop->mmu_pgt_size - prop->mmu_hop0_tables_total_size,
			-1);
	if (rc) {
		dev_err(hdev->dev, "Failed to add memory to page gen pool\n");
		goto err_pool_add;
	}

	return 0;

err_pool_add:
	gen_pool_destroy(hdev->mmu_pgt_pool);
err_pool_create:
	mutex_destroy(&hdev->mmu_cache_lock);

	return rc;
}

/*
 * hl_mmu_fini - release the mmu module.
 *
 * @hdev: pointer to the habanalabs device structure
 *
 * This function does the following:
 * - Disable mmu in hw
 * - free the pgts pool
 *
 * All ctxs should be freed before calling this func
 */
void hl_mmu_fini(struct hl_device *hdev)
{
	if (!hdev->mmu_enable)
		return;

	gen_pool_destroy(hdev->mmu_pgt_pool);

	mutex_destroy(&hdev->mmu_cache_lock);

	/* MMU HW fini will be done in device hw_fini() */
}

/**
 * hl_mmu_ctx_init() - initialize a context for using the MMU module.
 * @ctx: pointer to the context structure to initialize.
 *
 * Initialize a mutex to protect the concurrent mapping flow, a hash to hold all
 * page tables hops related to this context and an optional DRAM default page
 * mapping.
 * Return: 0 on success, non-zero otherwise.
 */
int hl_mmu_ctx_init(struct hl_ctx *ctx)
{
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 num_of_hop3, total_hops, hop1_addr, hop2_addr, hop2_pte_addr,
		hop3_pte_addr, pte_val;
	int rc, i, j, hop3_allocated = 0;

	if (!hdev->mmu_enable)
		return 0;

	mutex_init(&ctx->mmu_lock);
	hash_init(ctx->mmu_hash);

	if (!hdev->dram_supports_virtual_memory ||
			!hdev->dram_default_page_mapping)
		return 0;

	num_of_hop3 = prop->dram_size_for_default_page_mapping;
	do_div(num_of_hop3, prop->dram_page_size);
	do_div(num_of_hop3, PTE_ENTRIES_IN_HOP);

	/* add hop1 and hop2 */
	total_hops = num_of_hop3 + 2;

	ctx->dram_default_hops = kzalloc(HL_PTE_SIZE * total_hops,  GFP_KERNEL);
	if (!ctx->dram_default_hops) {
		rc = -ENOMEM;
		goto alloc_err;
	}

	hop1_addr = alloc_hop(ctx);
	if (hop1_addr == ULLONG_MAX) {
		dev_err(hdev->dev, "failed to alloc hop 1\n");
		rc = -ENOMEM;
		goto hop1_err;
	}

	ctx->dram_default_hops[total_hops - 1] = hop1_addr;

	hop2_addr = alloc_hop(ctx);
	if (hop2_addr == ULLONG_MAX) {
		dev_err(hdev->dev, "failed to alloc hop 2\n");
		rc = -ENOMEM;
		goto hop2_err;
	}

	ctx->dram_default_hops[total_hops - 2] = hop2_addr;

	for (i = 0 ; i < num_of_hop3 ; i++) {
		ctx->dram_default_hops[i] = alloc_hop(ctx);
		if (ctx->dram_default_hops[i] == ULLONG_MAX) {
			dev_err(hdev->dev, "failed to alloc hop 3, i: %d\n", i);
			rc = -ENOMEM;
			goto hop3_err;
		}
		hop3_allocated++;
	}

	/* need only pte 0 in hops 0 and 1 */
	pte_val = (hop1_addr & PTE_PHYS_ADDR_MASK) | PAGE_PRESENT_MASK;
	hdev->asic_funcs->write_pte(hdev, get_hop0_addr(ctx), pte_val);

	pte_val = (hop2_addr & PTE_PHYS_ADDR_MASK) | PAGE_PRESENT_MASK;
	hdev->asic_funcs->write_pte(hdev, hop1_addr, pte_val);
	get_pte(ctx, hop1_addr);

	hop2_pte_addr = hop2_addr;
	for (i = 0 ; i < num_of_hop3 ; i++) {
		pte_val = (ctx->dram_default_hops[i] & PTE_PHYS_ADDR_MASK) |
				PAGE_PRESENT_MASK;
		hdev->asic_funcs->write_pte(hdev, hop2_pte_addr, pte_val);
		get_pte(ctx, hop2_addr);
		hop2_pte_addr += HL_PTE_SIZE;
	}

	pte_val = (prop->mmu_dram_default_page_addr & PTE_PHYS_ADDR_MASK) |
			LAST_MASK | PAGE_PRESENT_MASK;

	for (i = 0 ; i < num_of_hop3 ; i++) {
		hop3_pte_addr = ctx->dram_default_hops[i];
		for (j = 0 ; j < PTE_ENTRIES_IN_HOP ; j++) {
			hdev->asic_funcs->write_pte(hdev, hop3_pte_addr,
					pte_val);
			get_pte(ctx, ctx->dram_default_hops[i]);
			hop3_pte_addr += HL_PTE_SIZE;
		}
	}

	/* flush all writes to reach PCI */
	mb();
	hdev->asic_funcs->read_pte(hdev, hop2_addr);

	return 0;

hop3_err:
	for (i = 0 ; i < hop3_allocated ; i++)
		free_hop(ctx, ctx->dram_default_hops[i]);
	free_hop(ctx, hop2_addr);
hop2_err:
	free_hop(ctx, hop1_addr);
hop1_err:
	kfree(ctx->dram_default_hops);
alloc_err:
	mutex_destroy(&ctx->mmu_lock);

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
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	struct pgt_info *pgt_info;
	struct hlist_node *tmp;
	u64 num_of_hop3, total_hops, hop1_addr, hop2_addr, hop2_pte_addr,
		hop3_pte_addr;
	int i, j;

	if (!ctx->hdev->mmu_enable)
		return;

	if (hdev->dram_supports_virtual_memory &&
			hdev->dram_default_page_mapping) {

		num_of_hop3 = prop->dram_size_for_default_page_mapping;
		do_div(num_of_hop3, prop->dram_page_size);
		do_div(num_of_hop3, PTE_ENTRIES_IN_HOP);

		/* add hop1 and hop2 */
		total_hops = num_of_hop3 + 2;
		hop1_addr = ctx->dram_default_hops[total_hops - 1];
		hop2_addr = ctx->dram_default_hops[total_hops - 2];

		for (i = 0 ; i < num_of_hop3 ; i++) {
			hop3_pte_addr = ctx->dram_default_hops[i];
			for (j = 0 ; j < PTE_ENTRIES_IN_HOP ; j++) {
				clear_pte(hdev, hop3_pte_addr);
				put_pte(ctx, ctx->dram_default_hops[i]);
				hop3_pte_addr += HL_PTE_SIZE;
			}
		}

		hop2_pte_addr = hop2_addr;
		for (i = 0 ; i < num_of_hop3 ; i++) {
			clear_pte(hdev, hop2_pte_addr);
			put_pte(ctx, hop2_addr);
			hop2_pte_addr += HL_PTE_SIZE;
		}

		clear_pte(hdev, hop1_addr);
		put_pte(ctx, hop1_addr);
		clear_pte(hdev, get_hop0_addr(ctx));

		kfree(ctx->dram_default_hops);

		/* flush all writes to reach PCI */
		mb();
		hdev->asic_funcs->read_pte(hdev, hop2_addr);
	}

	if (!hash_empty(ctx->mmu_hash))
		dev_err(hdev->dev, "ctx is freed while it has pgts in use\n");

	hash_for_each_safe(ctx->mmu_hash, i, tmp, pgt_info, node) {
		dev_err(hdev->dev,
			"pgt_info of addr 0x%llx of asid %d was not destroyed, num_ptes: %d\n",
			pgt_info->addr, ctx->asid, pgt_info->num_of_ptes);
		free_hop(ctx, pgt_info->addr);
	}

	mutex_destroy(&ctx->mmu_lock);
}

static int _hl_mmu_unmap(struct hl_ctx *ctx, u64 virt_addr)
{
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 hop0_addr = 0, hop0_pte_addr = 0,
		hop1_addr = 0, hop1_pte_addr = 0,
		hop2_addr = 0, hop2_pte_addr = 0,
		hop3_addr = 0, hop3_pte_addr = 0,
		hop4_addr = 0, hop4_pte_addr = 0,
		curr_pte;
	int clear_hop3 = 1;
	bool is_dram_addr, is_huge, is_dram_default_page_mapping;

	is_dram_addr = hl_mem_area_inside_range(virt_addr, PAGE_SIZE_2MB,
				prop->va_space_dram_start_address,
				prop->va_space_dram_end_address);

	hop0_addr = get_hop0_addr(ctx);

	hop0_pte_addr = get_hop0_pte_addr(ctx, hop0_addr, virt_addr);

	curr_pte = hdev->asic_funcs->read_pte(hdev, hop0_pte_addr);

	hop1_addr = get_next_hop_addr(curr_pte);

	if (hop1_addr == ULLONG_MAX)
		goto not_mapped;

	hop1_pte_addr = get_hop1_pte_addr(ctx, hop1_addr, virt_addr);

	curr_pte = hdev->asic_funcs->read_pte(hdev, hop1_pte_addr);

	hop2_addr = get_next_hop_addr(curr_pte);

	if (hop2_addr == ULLONG_MAX)
		goto not_mapped;

	hop2_pte_addr = get_hop2_pte_addr(ctx, hop2_addr, virt_addr);

	curr_pte = hdev->asic_funcs->read_pte(hdev, hop2_pte_addr);

	hop3_addr = get_next_hop_addr(curr_pte);

	if (hop3_addr == ULLONG_MAX)
		goto not_mapped;

	hop3_pte_addr = get_hop3_pte_addr(ctx, hop3_addr, virt_addr);

	curr_pte = hdev->asic_funcs->read_pte(hdev, hop3_pte_addr);

	is_huge = curr_pte & LAST_MASK;

	if (is_dram_addr && !is_huge) {
		dev_err(hdev->dev,
				"DRAM unmapping should use huge pages only\n");
		return -EFAULT;
	}

	is_dram_default_page_mapping =
			hdev->dram_default_page_mapping && is_dram_addr;

	if (!is_huge) {
		hop4_addr = get_next_hop_addr(curr_pte);

		if (hop4_addr == ULLONG_MAX)
			goto not_mapped;

		hop4_pte_addr = get_hop4_pte_addr(ctx, hop4_addr, virt_addr);

		curr_pte = hdev->asic_funcs->read_pte(hdev, hop4_pte_addr);

		clear_hop3 = 0;
	}

	if (is_dram_default_page_mapping) {
		u64 zero_pte = (prop->mmu_dram_default_page_addr &
				PTE_PHYS_ADDR_MASK) | LAST_MASK |
					PAGE_PRESENT_MASK;
		if (curr_pte == zero_pte) {
			dev_err(hdev->dev,
				"DRAM: hop3 PTE points to zero page, can't unmap, va: 0x%llx\n",
					virt_addr);
			goto not_mapped;
		}

		if (!(curr_pte & PAGE_PRESENT_MASK)) {
			dev_err(hdev->dev,
				"DRAM: hop3 PTE is cleared! can't unmap, va: 0x%llx\n",
					virt_addr);
			goto not_mapped;
		}

		hdev->asic_funcs->write_pte(hdev, hop3_pte_addr, zero_pte);
		put_pte(ctx, hop3_addr);
	} else {
		if (!(curr_pte & PAGE_PRESENT_MASK))
			goto not_mapped;

		clear_pte(hdev, hop4_addr ? hop4_pte_addr : hop3_pte_addr);

		if (hop4_addr && !put_pte(ctx, hop4_addr))
			clear_hop3 = 1;

		if (!clear_hop3)
			goto flush;
		clear_pte(hdev, hop3_pte_addr);

		if (put_pte(ctx, hop3_addr))
			goto flush;
		clear_pte(hdev, hop2_pte_addr);

		if (put_pte(ctx, hop2_addr))
			goto flush;
		clear_pte(hdev, hop1_pte_addr);

		if (put_pte(ctx, hop1_addr))
			goto flush;
		clear_pte(hdev, hop0_pte_addr);
	}

flush:
	/* flush all writes from all cores to reach PCI */
	mb();

	hdev->asic_funcs->read_pte(hdev,
				hop4_addr ? hop4_pte_addr : hop3_pte_addr);

	return 0;

not_mapped:
	dev_err(hdev->dev, "virt addr 0x%llx is not mapped to phys addr\n",
		virt_addr);

	return -EINVAL;
}

/*
 * hl_mmu_unmap - unmaps a virtual addr
 *
 * @ctx: pointer to the context structure
 * @virt_addr: virt addr to map from
 * @page_size: size of the page to unmap
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
 */
int hl_mmu_unmap(struct hl_ctx *ctx, u64 virt_addr, u32 page_size)
{
	struct hl_device *hdev = ctx->hdev;
	u64 real_virt_addr;
	u32 real_page_size, npages;
	int i, rc;

	if (!hdev->mmu_enable)
		return 0;

	/*
	 * The H/W handles mapping of 4KB/2MB page. Hence if the host page size
	 * is bigger, we break it to sub-pages and unmap them separately.
	 */
	if ((page_size % PAGE_SIZE_2MB) == 0) {
		real_page_size = PAGE_SIZE_2MB;
	} else if ((page_size % PAGE_SIZE_4KB) == 0) {
		real_page_size = PAGE_SIZE_4KB;
	} else {
		dev_err(hdev->dev,
			"page size of %u is not 4KB nor 2MB aligned, can't unmap\n",
				page_size);

		return -EFAULT;
	}

	npages = page_size / real_page_size;
	real_virt_addr = virt_addr;

	for (i = 0 ; i < npages ; i++) {
		rc = _hl_mmu_unmap(ctx, real_virt_addr);
		if (rc)
			return rc;

		real_virt_addr += real_page_size;
	}

	return 0;
}

static int _hl_mmu_map(struct hl_ctx *ctx, u64 virt_addr, u64 phys_addr,
		u32 page_size)
{
	struct hl_device *hdev = ctx->hdev;
	struct asic_fixed_properties *prop = &hdev->asic_prop;
	u64 hop0_addr = 0, hop0_pte_addr = 0,
		hop1_addr = 0, hop1_pte_addr = 0,
		hop2_addr = 0, hop2_pte_addr = 0,
		hop3_addr = 0, hop3_pte_addr = 0,
		hop4_addr = 0, hop4_pte_addr = 0,
		curr_pte = 0;
	bool hop1_new = false, hop2_new = false, hop3_new = false,
		hop4_new = false, is_huge, is_dram_addr,
		is_dram_default_page_mapping;
	int rc = -ENOMEM;

	/*
	 * This mapping function can map a 4KB/2MB page. For 2MB page there are
	 * only 3 hops rather than 4. Currently the DRAM allocation uses 2MB
	 * pages only but user memory could have been allocated with one of the
	 * two page sizes. Since this is a common code for all the three cases,
	 * we need this hugs page check.
	 */
	is_huge = page_size == PAGE_SIZE_2MB;

	is_dram_addr = hl_mem_area_inside_range(virt_addr, page_size,
				prop->va_space_dram_start_address,
				prop->va_space_dram_end_address);

	if (is_dram_addr && !is_huge) {
		dev_err(hdev->dev, "DRAM mapping should use huge pages only\n");
		return -EFAULT;
	}

	is_dram_default_page_mapping =
			hdev->dram_default_page_mapping && is_dram_addr;

	hop0_addr = get_hop0_addr(ctx);

	hop0_pte_addr = get_hop0_pte_addr(ctx, hop0_addr, virt_addr);

	curr_pte = hdev->asic_funcs->read_pte(hdev, hop0_pte_addr);

	hop1_addr = get_alloc_next_hop_addr(ctx, curr_pte, &hop1_new);

	if (hop1_addr == ULLONG_MAX)
		goto err;

	hop1_pte_addr = get_hop1_pte_addr(ctx, hop1_addr, virt_addr);

	curr_pte = hdev->asic_funcs->read_pte(hdev, hop1_pte_addr);

	hop2_addr = get_alloc_next_hop_addr(ctx, curr_pte, &hop2_new);

	if (hop2_addr == ULLONG_MAX)
		goto err;

	hop2_pte_addr = get_hop2_pte_addr(ctx, hop2_addr, virt_addr);

	curr_pte = hdev->asic_funcs->read_pte(hdev, hop2_pte_addr);

	hop3_addr = get_alloc_next_hop_addr(ctx, curr_pte, &hop3_new);

	if (hop3_addr == ULLONG_MAX)
		goto err;

	hop3_pte_addr = get_hop3_pte_addr(ctx, hop3_addr, virt_addr);

	curr_pte = hdev->asic_funcs->read_pte(hdev, hop3_pte_addr);

	if (!is_huge) {
		hop4_addr = get_alloc_next_hop_addr(ctx, curr_pte, &hop4_new);

		if (hop4_addr == ULLONG_MAX)
			goto err;

		hop4_pte_addr = get_hop4_pte_addr(ctx, hop4_addr, virt_addr);

		curr_pte = hdev->asic_funcs->read_pte(hdev, hop4_pte_addr);
	}

	if (is_dram_default_page_mapping) {
		u64 zero_pte = (prop->mmu_dram_default_page_addr &
					PTE_PHYS_ADDR_MASK) | LAST_MASK |
						PAGE_PRESENT_MASK;

		if (curr_pte != zero_pte) {
			dev_err(hdev->dev,
				"DRAM: mapping already exists for virt_addr 0x%llx\n",
					virt_addr);
			rc = -EINVAL;
			goto err;
		}

		if (hop1_new || hop2_new || hop3_new || hop4_new) {
			dev_err(hdev->dev,
				"DRAM mapping should not allocate more hops\n");
			rc = -EFAULT;
			goto err;
		}
	} else if (curr_pte & PAGE_PRESENT_MASK) {
		dev_err(hdev->dev,
				"mapping already exists for virt_addr 0x%llx\n",
					virt_addr);

		dev_dbg(hdev->dev, "hop0 pte: 0x%llx (0x%llx)\n",
				hdev->asic_funcs->read_pte(hdev, hop0_pte_addr),
				hop0_pte_addr);
		dev_dbg(hdev->dev, "hop1 pte: 0x%llx (0x%llx)\n",
				hdev->asic_funcs->read_pte(hdev, hop1_pte_addr),
				hop1_pte_addr);
		dev_dbg(hdev->dev, "hop2 pte: 0x%llx (0x%llx)\n",
				hdev->asic_funcs->read_pte(hdev, hop2_pte_addr),
				hop2_pte_addr);
		dev_dbg(hdev->dev, "hop3 pte: 0x%llx (0x%llx)\n",
				hdev->asic_funcs->read_pte(hdev, hop3_pte_addr),
				hop3_pte_addr);

		if (!is_huge)
			dev_dbg(hdev->dev, "hop4 pte: 0x%llx (0x%llx)\n",
				hdev->asic_funcs->read_pte(hdev,
							hop4_pte_addr),
							hop4_pte_addr);

		rc = -EINVAL;
		goto err;
	}

	curr_pte = (phys_addr & PTE_PHYS_ADDR_MASK) | LAST_MASK
			| PAGE_PRESENT_MASK;

	hdev->asic_funcs->write_pte(hdev,
				is_huge ? hop3_pte_addr : hop4_pte_addr,
				curr_pte);

	if (hop1_new) {
		curr_pte = (hop1_addr & PTE_PHYS_ADDR_MASK) |
				PAGE_PRESENT_MASK;
		ctx->hdev->asic_funcs->write_pte(ctx->hdev, hop0_pte_addr,
				curr_pte);
	}
	if (hop2_new) {
		curr_pte = (hop2_addr & PTE_PHYS_ADDR_MASK) |
				PAGE_PRESENT_MASK;
		ctx->hdev->asic_funcs->write_pte(ctx->hdev, hop1_pte_addr,
				curr_pte);
		get_pte(ctx, hop1_addr);
	}
	if (hop3_new) {
		curr_pte = (hop3_addr & PTE_PHYS_ADDR_MASK) |
				PAGE_PRESENT_MASK;
		ctx->hdev->asic_funcs->write_pte(ctx->hdev, hop2_pte_addr,
				curr_pte);
		get_pte(ctx, hop2_addr);
	}

	if (!is_huge) {
		if (hop4_new) {
			curr_pte = (hop4_addr & PTE_PHYS_ADDR_MASK) |
					PAGE_PRESENT_MASK;
			ctx->hdev->asic_funcs->write_pte(ctx->hdev,
					hop3_pte_addr, curr_pte);
			get_pte(ctx, hop3_addr);
		}

		get_pte(ctx, hop4_addr);
	} else {
		get_pte(ctx, hop3_addr);
	}

	/* flush all writes from all cores to reach PCI */
	mb();

	hdev->asic_funcs->read_pte(hdev,
				is_huge ? hop3_pte_addr : hop4_pte_addr);

	return 0;

err:
	if (hop4_new)
		free_hop(ctx, hop4_addr);
	if (hop3_new)
		free_hop(ctx, hop3_addr);
	if (hop2_new)
		free_hop(ctx, hop2_addr);
	if (hop1_new)
		free_hop(ctx, hop1_addr);

	return rc;
}

/*
 * hl_mmu_map - maps a virtual addr to physical addr
 *
 * @ctx: pointer to the context structure
 * @virt_addr: virt addr to map from
 * @phys_addr: phys addr to map to
 * @page_size: physical page size
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
 */
int hl_mmu_map(struct hl_ctx *ctx, u64 virt_addr, u64 phys_addr, u32 page_size)
{
	struct hl_device *hdev = ctx->hdev;
	u64 real_virt_addr, real_phys_addr;
	u32 real_page_size, npages;
	int i, rc, mapped_cnt = 0;

	if (!hdev->mmu_enable)
		return 0;

	/*
	 * The H/W handles mapping of 4KB/2MB page. Hence if the host page size
	 * is bigger, we break it to sub-pages and map them separately.
	 */
	if ((page_size % PAGE_SIZE_2MB) == 0) {
		real_page_size = PAGE_SIZE_2MB;
	} else if ((page_size % PAGE_SIZE_4KB) == 0) {
		real_page_size = PAGE_SIZE_4KB;
	} else {
		dev_err(hdev->dev,
			"page size of %u is not 4KB nor 2MB aligned, can't map\n",
				page_size);

		return -EFAULT;
	}

	npages = page_size / real_page_size;
	real_virt_addr = virt_addr;
	real_phys_addr = phys_addr;

	for (i = 0 ; i < npages ; i++) {
		rc = _hl_mmu_map(ctx, real_virt_addr, real_phys_addr,
				real_page_size);
		if (rc)
			goto err;

		real_virt_addr += real_page_size;
		real_phys_addr += real_page_size;
		mapped_cnt++;
	}

	return 0;

err:
	real_virt_addr = virt_addr;
	for (i = 0 ; i < mapped_cnt ; i++) {
		if (_hl_mmu_unmap(ctx, real_virt_addr))
			dev_warn_ratelimited(hdev->dev,
				"failed to unmap va: 0x%llx\n", real_virt_addr);

		real_virt_addr += real_page_size;
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

}

/*
 * hl_mmu_swap_in - marks all mapping of the given ctx as swapped in
 *
 * @ctx: pointer to the context structure
 *
 */
void hl_mmu_swap_in(struct hl_ctx *ctx)
{

}
