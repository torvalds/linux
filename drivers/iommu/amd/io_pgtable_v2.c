// SPDX-License-Identifier: GPL-2.0-only
/*
 * CPU-agnostic AMD IO page table v2 allocator.
 *
 * Copyright (C) 2022 Advanced Micro Devices, Inc.
 * Author: Suravee Suthikulpanit <suravee.suthikulpanit@amd.com>
 * Author: Vasant Hegde <vasant.hegde@amd.com>
 */

#define pr_fmt(fmt)	"AMD-Vi: " fmt
#define dev_fmt(fmt)	pr_fmt(fmt)

#include <linux/bitops.h>
#include <linux/io-pgtable.h>
#include <linux/kernel.h>

#include <asm/barrier.h>

#include "amd_iommu_types.h"
#include "amd_iommu.h"

#define IOMMU_PAGE_PRESENT	BIT_ULL(0)	/* Is present */
#define IOMMU_PAGE_RW		BIT_ULL(1)	/* Writeable */
#define IOMMU_PAGE_USER		BIT_ULL(2)	/* Userspace addressable */
#define IOMMU_PAGE_PWT		BIT_ULL(3)	/* Page write through */
#define IOMMU_PAGE_PCD		BIT_ULL(4)	/* Page cache disabled */
#define IOMMU_PAGE_ACCESS	BIT_ULL(5)	/* Was accessed (updated by IOMMU) */
#define IOMMU_PAGE_DIRTY	BIT_ULL(6)	/* Was written to (updated by IOMMU) */
#define IOMMU_PAGE_PSE		BIT_ULL(7)	/* Page Size Extensions */
#define IOMMU_PAGE_NX		BIT_ULL(63)	/* No execute */

#define MAX_PTRS_PER_PAGE	512

#define IOMMU_PAGE_SIZE_2M	BIT_ULL(21)
#define IOMMU_PAGE_SIZE_1G	BIT_ULL(30)


static inline int get_pgtable_level(void)
{
	/* 5 level page table is not supported */
	return PAGE_MODE_4_LEVEL;
}

static inline bool is_large_pte(u64 pte)
{
	return (pte & IOMMU_PAGE_PSE);
}

static inline void *alloc_pgtable_page(void)
{
	return (void *)get_zeroed_page(GFP_KERNEL);
}

static inline u64 set_pgtable_attr(u64 *page)
{
	u64 prot;

	prot = IOMMU_PAGE_PRESENT | IOMMU_PAGE_RW | IOMMU_PAGE_USER;
	prot |= IOMMU_PAGE_ACCESS | IOMMU_PAGE_DIRTY;

	return (iommu_virt_to_phys(page) | prot);
}

static inline void *get_pgtable_pte(u64 pte)
{
	return iommu_phys_to_virt(pte & PM_ADDR_MASK);
}

static u64 set_pte_attr(u64 paddr, u64 pg_size, int prot)
{
	u64 pte;

	pte = __sme_set(paddr & PM_ADDR_MASK);
	pte |= IOMMU_PAGE_PRESENT | IOMMU_PAGE_USER;
	pte |= IOMMU_PAGE_ACCESS | IOMMU_PAGE_DIRTY;

	if (prot & IOMMU_PROT_IW)
		pte |= IOMMU_PAGE_RW;

	/* Large page */
	if (pg_size == IOMMU_PAGE_SIZE_1G || pg_size == IOMMU_PAGE_SIZE_2M)
		pte |= IOMMU_PAGE_PSE;

	return pte;
}

static inline u64 get_alloc_page_size(u64 size)
{
	if (size >= IOMMU_PAGE_SIZE_1G)
		return IOMMU_PAGE_SIZE_1G;

	if (size >= IOMMU_PAGE_SIZE_2M)
		return IOMMU_PAGE_SIZE_2M;

	return PAGE_SIZE;
}

static inline int page_size_to_level(u64 pg_size)
{
	if (pg_size == IOMMU_PAGE_SIZE_1G)
		return PAGE_MODE_3_LEVEL;
	if (pg_size == IOMMU_PAGE_SIZE_2M)
		return PAGE_MODE_2_LEVEL;

	return PAGE_MODE_1_LEVEL;
}

static inline void free_pgtable_page(u64 *pt)
{
	free_page((unsigned long)pt);
}

static void free_pgtable(u64 *pt, int level)
{
	u64 *p;
	int i;

	for (i = 0; i < MAX_PTRS_PER_PAGE; i++) {
		/* PTE present? */
		if (!IOMMU_PTE_PRESENT(pt[i]))
			continue;

		if (is_large_pte(pt[i]))
			continue;

		/*
		 * Free the next level. No need to look at l1 tables here since
		 * they can only contain leaf PTEs; just free them directly.
		 */
		p = get_pgtable_pte(pt[i]);
		if (level > 2)
			free_pgtable(p, level - 1);
		else
			free_pgtable_page(p);
	}

	free_pgtable_page(pt);
}

/* Allocate page table */
static u64 *v2_alloc_pte(u64 *pgd, unsigned long iova,
			 unsigned long pg_size, bool *updated)
{
	u64 *pte, *page;
	int level, end_level;

	level = get_pgtable_level() - 1;
	end_level = page_size_to_level(pg_size);
	pte = &pgd[PM_LEVEL_INDEX(level, iova)];
	iova = PAGE_SIZE_ALIGN(iova, PAGE_SIZE);

	while (level >= end_level) {
		u64 __pte, __npte;

		__pte = *pte;

		if (IOMMU_PTE_PRESENT(__pte) && is_large_pte(__pte)) {
			/* Unmap large pte */
			cmpxchg64(pte, *pte, 0ULL);
			*updated = true;
			continue;
		}

		if (!IOMMU_PTE_PRESENT(__pte)) {
			page = alloc_pgtable_page();
			if (!page)
				return NULL;

			__npte = set_pgtable_attr(page);
			/* pte could have been changed somewhere. */
			if (cmpxchg64(pte, __pte, __npte) != __pte)
				free_pgtable_page(page);
			else if (IOMMU_PTE_PRESENT(__pte))
				*updated = true;

			continue;
		}

		level -= 1;
		pte = get_pgtable_pte(__pte);
		pte = &pte[PM_LEVEL_INDEX(level, iova)];
	}

	/* Tear down existing pte entries */
	if (IOMMU_PTE_PRESENT(*pte)) {
		u64 *__pte;

		*updated = true;
		__pte = get_pgtable_pte(*pte);
		cmpxchg64(pte, *pte, 0ULL);
		if (pg_size == IOMMU_PAGE_SIZE_1G)
			free_pgtable(__pte, end_level - 1);
		else if (pg_size == IOMMU_PAGE_SIZE_2M)
			free_pgtable_page(__pte);
	}

	return pte;
}

/*
 * This function checks if there is a PTE for a given dma address.
 * If there is one, it returns the pointer to it.
 */
static u64 *fetch_pte(struct amd_io_pgtable *pgtable,
		      unsigned long iova, unsigned long *page_size)
{
	u64 *pte;
	int level;

	level = get_pgtable_level() - 1;
	pte = &pgtable->pgd[PM_LEVEL_INDEX(level, iova)];
	/* Default page size is 4K */
	*page_size = PAGE_SIZE;

	while (level) {
		/* Not present */
		if (!IOMMU_PTE_PRESENT(*pte))
			return NULL;

		/* Walk to the next level */
		pte = get_pgtable_pte(*pte);
		pte = &pte[PM_LEVEL_INDEX(level - 1, iova)];

		/* Large page */
		if (is_large_pte(*pte)) {
			if (level == PAGE_MODE_3_LEVEL)
				*page_size = IOMMU_PAGE_SIZE_1G;
			else if (level == PAGE_MODE_2_LEVEL)
				*page_size = IOMMU_PAGE_SIZE_2M;
			else
				return NULL;	/* Wrongly set PSE bit in PTE */

			break;
		}

		level -= 1;
	}

	return pte;
}

static int iommu_v2_map_pages(struct io_pgtable_ops *ops, unsigned long iova,
			      phys_addr_t paddr, size_t pgsize, size_t pgcount,
			      int prot, gfp_t gfp, size_t *mapped)
{
	struct protection_domain *pdom = io_pgtable_ops_to_domain(ops);
	struct io_pgtable_cfg *cfg = &pdom->iop.iop.cfg;
	u64 *pte;
	unsigned long map_size;
	unsigned long mapped_size = 0;
	unsigned long o_iova = iova;
	size_t size = pgcount << __ffs(pgsize);
	int count = 0;
	int ret = 0;
	bool updated = false;

	if (WARN_ON(!pgsize || (pgsize & cfg->pgsize_bitmap) != pgsize) || !pgcount)
		return -EINVAL;

	if (!(prot & IOMMU_PROT_MASK))
		return -EINVAL;

	while (mapped_size < size) {
		map_size = get_alloc_page_size(pgsize);
		pte = v2_alloc_pte(pdom->iop.pgd, iova, map_size, &updated);
		if (!pte) {
			ret = -EINVAL;
			goto out;
		}

		*pte = set_pte_attr(paddr, map_size, prot);

		count++;
		iova += map_size;
		paddr += map_size;
		mapped_size += map_size;
	}

out:
	if (updated) {
		if (count > 1)
			amd_iommu_flush_tlb(&pdom->domain, 0);
		else
			amd_iommu_flush_page(&pdom->domain, 0, o_iova);
	}

	if (mapped)
		*mapped += mapped_size;

	return ret;
}

static unsigned long iommu_v2_unmap_pages(struct io_pgtable_ops *ops,
					  unsigned long iova,
					  size_t pgsize, size_t pgcount,
					  struct iommu_iotlb_gather *gather)
{
	struct amd_io_pgtable *pgtable = io_pgtable_ops_to_data(ops);
	struct io_pgtable_cfg *cfg = &pgtable->iop.cfg;
	unsigned long unmap_size;
	unsigned long unmapped = 0;
	size_t size = pgcount << __ffs(pgsize);
	u64 *pte;

	if (WARN_ON(!pgsize || (pgsize & cfg->pgsize_bitmap) != pgsize || !pgcount))
		return 0;

	while (unmapped < size) {
		pte = fetch_pte(pgtable, iova, &unmap_size);
		if (!pte)
			return unmapped;

		*pte = 0ULL;

		iova = (iova & ~(unmap_size - 1)) + unmap_size;
		unmapped += unmap_size;
	}

	return unmapped;
}

static phys_addr_t iommu_v2_iova_to_phys(struct io_pgtable_ops *ops, unsigned long iova)
{
	struct amd_io_pgtable *pgtable = io_pgtable_ops_to_data(ops);
	unsigned long offset_mask, pte_pgsize;
	u64 *pte, __pte;

	pte = fetch_pte(pgtable, iova, &pte_pgsize);
	if (!pte || !IOMMU_PTE_PRESENT(*pte))
		return 0;

	offset_mask = pte_pgsize - 1;
	__pte = __sme_clr(*pte & PM_ADDR_MASK);

	return (__pte & ~offset_mask) | (iova & offset_mask);
}

/*
 * ----------------------------------------------------
 */
static void v2_tlb_flush_all(void *cookie)
{
}

static void v2_tlb_flush_walk(unsigned long iova, size_t size,
			      size_t granule, void *cookie)
{
}

static void v2_tlb_add_page(struct iommu_iotlb_gather *gather,
			    unsigned long iova, size_t granule,
			    void *cookie)
{
}

static const struct iommu_flush_ops v2_flush_ops = {
	.tlb_flush_all	= v2_tlb_flush_all,
	.tlb_flush_walk = v2_tlb_flush_walk,
	.tlb_add_page	= v2_tlb_add_page,
};

static void v2_free_pgtable(struct io_pgtable *iop)
{
	struct protection_domain *pdom;
	struct amd_io_pgtable *pgtable = container_of(iop, struct amd_io_pgtable, iop);

	pdom = container_of(pgtable, struct protection_domain, iop);
	if (!(pdom->flags & PD_IOMMUV2_MASK))
		return;

	/*
	 * Make changes visible to IOMMUs. No need to clear gcr3 entry
	 * as gcr3 table is already freed.
	 */
	amd_iommu_domain_update(pdom);

	/* Free page table */
	free_pgtable(pgtable->pgd, get_pgtable_level());
}

static struct io_pgtable *v2_alloc_pgtable(struct io_pgtable_cfg *cfg, void *cookie)
{
	struct amd_io_pgtable *pgtable = io_pgtable_cfg_to_data(cfg);
	struct protection_domain *pdom = (struct protection_domain *)cookie;
	int ret;

	pgtable->pgd = alloc_pgtable_page();
	if (!pgtable->pgd)
		return NULL;

	ret = amd_iommu_domain_set_gcr3(&pdom->domain, 0, iommu_virt_to_phys(pgtable->pgd));
	if (ret)
		goto err_free_pgd;

	pgtable->iop.ops.map_pages    = iommu_v2_map_pages;
	pgtable->iop.ops.unmap_pages  = iommu_v2_unmap_pages;
	pgtable->iop.ops.iova_to_phys = iommu_v2_iova_to_phys;

	cfg->pgsize_bitmap = AMD_IOMMU_PGSIZES_V2,
	cfg->ias           = IOMMU_IN_ADDR_BIT_SIZE,
	cfg->oas           = IOMMU_OUT_ADDR_BIT_SIZE,
	cfg->tlb           = &v2_flush_ops;

	return &pgtable->iop;

err_free_pgd:
	free_pgtable_page(pgtable->pgd);

	return NULL;
}

struct io_pgtable_init_fns io_pgtable_amd_iommu_v2_init_fns = {
	.alloc	= v2_alloc_pgtable,
	.free	= v2_free_pgtable,
};
