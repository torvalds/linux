// SPDX-License-Identifier: GPL-2.0-only
/*
 * CPU-agnostic AMD IO page table allocator.
 *
 * Copyright (C) 2020 Advanced Micro Devices, Inc.
 * Author: Suravee Suthikulpanit <suravee.suthikulpanit@amd.com>
 */

#define pr_fmt(fmt)     "AMD-Vi: " fmt
#define dev_fmt(fmt)    pr_fmt(fmt)

#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/io-pgtable.h>
#include <linux/kernel.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>

#include <asm/barrier.h>

#include "amd_iommu_types.h"
#include "amd_iommu.h"
#include "../iommu-pages.h"

/*
 * Helper function to get the first pte of a large mapping
 */
static u64 *first_pte_l7(u64 *pte, unsigned long *page_size,
			 unsigned long *count)
{
	unsigned long pte_mask, pg_size, cnt;
	u64 *fpte;

	pg_size  = PTE_PAGE_SIZE(*pte);
	cnt      = PAGE_SIZE_PTE_COUNT(pg_size);
	pte_mask = ~((cnt << 3) - 1);
	fpte     = (u64 *)(((unsigned long)pte) & pte_mask);

	if (page_size)
		*page_size = pg_size;

	if (count)
		*count = cnt;

	return fpte;
}

/****************************************************************************
 *
 * The functions below are used the create the page table mappings for
 * unity mapped regions.
 *
 ****************************************************************************/

static void free_pt_page(u64 *pt, struct list_head *freelist)
{
	struct page *p = virt_to_page(pt);

	list_add_tail(&p->lru, freelist);
}

static void free_pt_lvl(u64 *pt, struct list_head *freelist, int lvl)
{
	u64 *p;
	int i;

	for (i = 0; i < 512; ++i) {
		/* PTE present? */
		if (!IOMMU_PTE_PRESENT(pt[i]))
			continue;

		/* Large PTE? */
		if (PM_PTE_LEVEL(pt[i]) == 0 ||
		    PM_PTE_LEVEL(pt[i]) == 7)
			continue;

		/*
		 * Free the next level. No need to look at l1 tables here since
		 * they can only contain leaf PTEs; just free them directly.
		 */
		p = IOMMU_PTE_PAGE(pt[i]);
		if (lvl > 2)
			free_pt_lvl(p, freelist, lvl - 1);
		else
			free_pt_page(p, freelist);
	}

	free_pt_page(pt, freelist);
}

static void free_sub_pt(u64 *root, int mode, struct list_head *freelist)
{
	switch (mode) {
	case PAGE_MODE_NONE:
	case PAGE_MODE_7_LEVEL:
		break;
	case PAGE_MODE_1_LEVEL:
		free_pt_page(root, freelist);
		break;
	case PAGE_MODE_2_LEVEL:
	case PAGE_MODE_3_LEVEL:
	case PAGE_MODE_4_LEVEL:
	case PAGE_MODE_5_LEVEL:
	case PAGE_MODE_6_LEVEL:
		free_pt_lvl(root, freelist, mode);
		break;
	default:
		BUG();
	}
}

/*
 * This function is used to add another level to an IO page table. Adding
 * another level increases the size of the address space by 9 bits to a size up
 * to 64 bits.
 */
static bool increase_address_space(struct amd_io_pgtable *pgtable,
				   unsigned long address,
				   gfp_t gfp)
{
	struct io_pgtable_cfg *cfg = &pgtable->pgtbl.cfg;
	struct protection_domain *domain =
		container_of(pgtable, struct protection_domain, iop);
	unsigned long flags;
	bool ret = true;
	u64 *pte;

	pte = iommu_alloc_page_node(cfg->amd.nid, gfp);
	if (!pte)
		return false;

	spin_lock_irqsave(&domain->lock, flags);

	if (address <= PM_LEVEL_SIZE(pgtable->mode))
		goto out;

	ret = false;
	if (WARN_ON_ONCE(pgtable->mode == PAGE_MODE_6_LEVEL))
		goto out;

	*pte = PM_LEVEL_PDE(pgtable->mode, iommu_virt_to_phys(pgtable->root));

	pgtable->root  = pte;
	pgtable->mode += 1;
	amd_iommu_update_and_flush_device_table(domain);

	pte = NULL;
	ret = true;

out:
	spin_unlock_irqrestore(&domain->lock, flags);
	iommu_free_page(pte);

	return ret;
}

static u64 *alloc_pte(struct amd_io_pgtable *pgtable,
		      unsigned long address,
		      unsigned long page_size,
		      u64 **pte_page,
		      gfp_t gfp,
		      bool *updated)
{
	struct io_pgtable_cfg *cfg = &pgtable->pgtbl.cfg;
	int level, end_lvl;
	u64 *pte, *page;

	BUG_ON(!is_power_of_2(page_size));

	while (address > PM_LEVEL_SIZE(pgtable->mode)) {
		/*
		 * Return an error if there is no memory to update the
		 * page-table.
		 */
		if (!increase_address_space(pgtable, address, gfp))
			return NULL;
	}


	level   = pgtable->mode - 1;
	pte     = &pgtable->root[PM_LEVEL_INDEX(level, address)];
	address = PAGE_SIZE_ALIGN(address, page_size);
	end_lvl = PAGE_SIZE_LEVEL(page_size);

	while (level > end_lvl) {
		u64 __pte, __npte;
		int pte_level;

		__pte     = *pte;
		pte_level = PM_PTE_LEVEL(__pte);

		/*
		 * If we replace a series of large PTEs, we need
		 * to tear down all of them.
		 */
		if (IOMMU_PTE_PRESENT(__pte) &&
		    pte_level == PAGE_MODE_7_LEVEL) {
			unsigned long count, i;
			u64 *lpte;

			lpte = first_pte_l7(pte, NULL, &count);

			/*
			 * Unmap the replicated PTEs that still match the
			 * original large mapping
			 */
			for (i = 0; i < count; ++i)
				cmpxchg64(&lpte[i], __pte, 0ULL);

			*updated = true;
			continue;
		}

		if (!IOMMU_PTE_PRESENT(__pte) ||
		    pte_level == PAGE_MODE_NONE) {
			page = iommu_alloc_page_node(cfg->amd.nid, gfp);

			if (!page)
				return NULL;

			__npte = PM_LEVEL_PDE(level, iommu_virt_to_phys(page));

			/* pte could have been changed somewhere. */
			if (!try_cmpxchg64(pte, &__pte, __npte))
				iommu_free_page(page);
			else if (IOMMU_PTE_PRESENT(__pte))
				*updated = true;

			continue;
		}

		/* No level skipping support yet */
		if (pte_level != level)
			return NULL;

		level -= 1;

		pte = IOMMU_PTE_PAGE(__pte);

		if (pte_page && level == end_lvl)
			*pte_page = pte;

		pte = &pte[PM_LEVEL_INDEX(level, address)];
	}

	return pte;
}

/*
 * This function checks if there is a PTE for a given dma address. If
 * there is one, it returns the pointer to it.
 */
static u64 *fetch_pte(struct amd_io_pgtable *pgtable,
		      unsigned long address,
		      unsigned long *page_size)
{
	int level;
	u64 *pte;

	*page_size = 0;

	if (address > PM_LEVEL_SIZE(pgtable->mode))
		return NULL;

	level	   =  pgtable->mode - 1;
	pte	   = &pgtable->root[PM_LEVEL_INDEX(level, address)];
	*page_size =  PTE_LEVEL_PAGE_SIZE(level);

	while (level > 0) {

		/* Not Present */
		if (!IOMMU_PTE_PRESENT(*pte))
			return NULL;

		/* Large PTE */
		if (PM_PTE_LEVEL(*pte) == PAGE_MODE_7_LEVEL ||
		    PM_PTE_LEVEL(*pte) == PAGE_MODE_NONE)
			break;

		/* No level skipping support yet */
		if (PM_PTE_LEVEL(*pte) != level)
			return NULL;

		level -= 1;

		/* Walk to the next level */
		pte	   = IOMMU_PTE_PAGE(*pte);
		pte	   = &pte[PM_LEVEL_INDEX(level, address)];
		*page_size = PTE_LEVEL_PAGE_SIZE(level);
	}

	/*
	 * If we have a series of large PTEs, make
	 * sure to return a pointer to the first one.
	 */
	if (PM_PTE_LEVEL(*pte) == PAGE_MODE_7_LEVEL)
		pte = first_pte_l7(pte, page_size, NULL);

	return pte;
}

static void free_clear_pte(u64 *pte, u64 pteval, struct list_head *freelist)
{
	u64 *pt;
	int mode;

	while (!try_cmpxchg64(pte, &pteval, 0))
		pr_warn("AMD-Vi: IOMMU pte changed since we read it\n");

	if (!IOMMU_PTE_PRESENT(pteval))
		return;

	pt   = IOMMU_PTE_PAGE(pteval);
	mode = IOMMU_PTE_MODE(pteval);

	free_sub_pt(pt, mode, freelist);
}

/*
 * Generic mapping functions. It maps a physical address into a DMA
 * address space. It allocates the page table pages if necessary.
 * In the future it can be extended to a generic mapping function
 * supporting all features of AMD IOMMU page tables like level skipping
 * and full 64 bit address spaces.
 */
static int iommu_v1_map_pages(struct io_pgtable_ops *ops, unsigned long iova,
			      phys_addr_t paddr, size_t pgsize, size_t pgcount,
			      int prot, gfp_t gfp, size_t *mapped)
{
	struct amd_io_pgtable *pgtable = io_pgtable_ops_to_data(ops);
	LIST_HEAD(freelist);
	bool updated = false;
	u64 __pte, *pte;
	int ret, i, count;
	size_t size = pgcount << __ffs(pgsize);
	unsigned long o_iova = iova;

	BUG_ON(!IS_ALIGNED(iova, pgsize));
	BUG_ON(!IS_ALIGNED(paddr, pgsize));

	ret = -EINVAL;
	if (!(prot & IOMMU_PROT_MASK))
		goto out;

	while (pgcount > 0) {
		count = PAGE_SIZE_PTE_COUNT(pgsize);
		pte   = alloc_pte(pgtable, iova, pgsize, NULL, gfp, &updated);

		ret = -ENOMEM;
		if (!pte)
			goto out;

		for (i = 0; i < count; ++i)
			free_clear_pte(&pte[i], pte[i], &freelist);

		if (!list_empty(&freelist))
			updated = true;

		if (count > 1) {
			__pte = PAGE_SIZE_PTE(__sme_set(paddr), pgsize);
			__pte |= PM_LEVEL_ENC(7) | IOMMU_PTE_PR | IOMMU_PTE_FC;
		} else
			__pte = __sme_set(paddr) | IOMMU_PTE_PR | IOMMU_PTE_FC;

		if (prot & IOMMU_PROT_IR)
			__pte |= IOMMU_PTE_IR;
		if (prot & IOMMU_PROT_IW)
			__pte |= IOMMU_PTE_IW;

		for (i = 0; i < count; ++i)
			pte[i] = __pte;

		iova  += pgsize;
		paddr += pgsize;
		pgcount--;
		if (mapped)
			*mapped += pgsize;
	}

	ret = 0;

out:
	if (updated) {
		struct protection_domain *dom = io_pgtable_ops_to_domain(ops);
		unsigned long flags;

		spin_lock_irqsave(&dom->lock, flags);
		/*
		 * Flush domain TLB(s) and wait for completion. Any Device-Table
		 * Updates and flushing already happened in
		 * increase_address_space().
		 */
		amd_iommu_domain_flush_pages(dom, o_iova, size);
		spin_unlock_irqrestore(&dom->lock, flags);
	}

	/* Everything flushed out, free pages now */
	iommu_put_pages_list(&freelist);

	return ret;
}

static unsigned long iommu_v1_unmap_pages(struct io_pgtable_ops *ops,
					  unsigned long iova,
					  size_t pgsize, size_t pgcount,
					  struct iommu_iotlb_gather *gather)
{
	struct amd_io_pgtable *pgtable = io_pgtable_ops_to_data(ops);
	unsigned long long unmapped;
	unsigned long unmap_size;
	u64 *pte;
	size_t size = pgcount << __ffs(pgsize);

	BUG_ON(!is_power_of_2(pgsize));

	unmapped = 0;

	while (unmapped < size) {
		pte = fetch_pte(pgtable, iova, &unmap_size);
		if (pte) {
			int i, count;

			count = PAGE_SIZE_PTE_COUNT(unmap_size);
			for (i = 0; i < count; i++)
				pte[i] = 0ULL;
		} else {
			return unmapped;
		}

		iova = (iova & ~(unmap_size - 1)) + unmap_size;
		unmapped += unmap_size;
	}

	return unmapped;
}

static phys_addr_t iommu_v1_iova_to_phys(struct io_pgtable_ops *ops, unsigned long iova)
{
	struct amd_io_pgtable *pgtable = io_pgtable_ops_to_data(ops);
	unsigned long offset_mask, pte_pgsize;
	u64 *pte, __pte;

	pte = fetch_pte(pgtable, iova, &pte_pgsize);

	if (!pte || !IOMMU_PTE_PRESENT(*pte))
		return 0;

	offset_mask = pte_pgsize - 1;
	__pte	    = __sme_clr(*pte & PM_ADDR_MASK);

	return (__pte & ~offset_mask) | (iova & offset_mask);
}

static bool pte_test_and_clear_dirty(u64 *ptep, unsigned long size,
				     unsigned long flags)
{
	bool test_only = flags & IOMMU_DIRTY_NO_CLEAR;
	bool dirty = false;
	int i, count;

	/*
	 * 2.2.3.2 Host Dirty Support
	 * When a non-default page size is used , software must OR the
	 * Dirty bits in all of the replicated host PTEs used to map
	 * the page. The IOMMU does not guarantee the Dirty bits are
	 * set in all of the replicated PTEs. Any portion of the page
	 * may have been written even if the Dirty bit is set in only
	 * one of the replicated PTEs.
	 */
	count = PAGE_SIZE_PTE_COUNT(size);
	for (i = 0; i < count && test_only; i++) {
		if (test_bit(IOMMU_PTE_HD_BIT, (unsigned long *)&ptep[i])) {
			dirty = true;
			break;
		}
	}

	for (i = 0; i < count && !test_only; i++) {
		if (test_and_clear_bit(IOMMU_PTE_HD_BIT,
				       (unsigned long *)&ptep[i])) {
			dirty = true;
		}
	}

	return dirty;
}

static int iommu_v1_read_and_clear_dirty(struct io_pgtable_ops *ops,
					 unsigned long iova, size_t size,
					 unsigned long flags,
					 struct iommu_dirty_bitmap *dirty)
{
	struct amd_io_pgtable *pgtable = io_pgtable_ops_to_data(ops);
	unsigned long end = iova + size - 1;

	do {
		unsigned long pgsize = 0;
		u64 *ptep, pte;

		ptep = fetch_pte(pgtable, iova, &pgsize);
		if (ptep)
			pte = READ_ONCE(*ptep);
		if (!ptep || !IOMMU_PTE_PRESENT(pte)) {
			pgsize = pgsize ?: PTE_LEVEL_PAGE_SIZE(0);
			iova += pgsize;
			continue;
		}

		/*
		 * Mark the whole IOVA range as dirty even if only one of
		 * the replicated PTEs were marked dirty.
		 */
		if (pte_test_and_clear_dirty(ptep, pgsize, flags))
			iommu_dirty_bitmap_record(dirty, iova, pgsize);
		iova += pgsize;
	} while (iova < end);

	return 0;
}

/*
 * ----------------------------------------------------
 */
static void v1_free_pgtable(struct io_pgtable *iop)
{
	struct amd_io_pgtable *pgtable = container_of(iop, struct amd_io_pgtable, pgtbl);
	LIST_HEAD(freelist);

	if (pgtable->mode == PAGE_MODE_NONE)
		return;

	/* Page-table is not visible to IOMMU anymore, so free it */
	BUG_ON(pgtable->mode < PAGE_MODE_NONE ||
	       pgtable->mode > PAGE_MODE_6_LEVEL);

	free_sub_pt(pgtable->root, pgtable->mode, &freelist);
	iommu_put_pages_list(&freelist);
}

static struct io_pgtable *v1_alloc_pgtable(struct io_pgtable_cfg *cfg, void *cookie)
{
	struct amd_io_pgtable *pgtable = io_pgtable_cfg_to_data(cfg);

	pgtable->root = iommu_alloc_page_node(cfg->amd.nid, GFP_KERNEL);
	if (!pgtable->root)
		return NULL;
	pgtable->mode = PAGE_MODE_3_LEVEL;

	cfg->pgsize_bitmap  = amd_iommu_pgsize_bitmap;
	cfg->ias            = IOMMU_IN_ADDR_BIT_SIZE;
	cfg->oas            = IOMMU_OUT_ADDR_BIT_SIZE;

	pgtable->pgtbl.ops.map_pages    = iommu_v1_map_pages;
	pgtable->pgtbl.ops.unmap_pages  = iommu_v1_unmap_pages;
	pgtable->pgtbl.ops.iova_to_phys = iommu_v1_iova_to_phys;
	pgtable->pgtbl.ops.read_and_clear_dirty = iommu_v1_read_and_clear_dirty;

	return &pgtable->pgtbl;
}

struct io_pgtable_init_fns io_pgtable_amd_iommu_v1_init_fns = {
	.alloc	= v1_alloc_pgtable,
	.free	= v1_free_pgtable,
};
