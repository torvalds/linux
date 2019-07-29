// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Intel Corporation.
 * Copyright 2018 Google LLC.
 *
 * Author: Tuukka Toivonen <tuukka.toivonen@intel.com>
 * Author: Sakari Ailus <sakari.ailus@linux.intel.com>
 * Author: Samu Onkalo <samu.onkalo@intel.com>
 * Author: Tomasz Figa <tfiga@chromium.org>
 *
 */

#include <linux/dma-mapping.h>
#include <linux/iopoll.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <asm/set_memory.h>

#include "ipu3-mmu.h"

#define IPU3_PAGE_SHIFT		12
#define IPU3_PAGE_SIZE		(1UL << IPU3_PAGE_SHIFT)

#define IPU3_PT_BITS		10
#define IPU3_PT_PTES		(1UL << IPU3_PT_BITS)
#define IPU3_PT_SIZE		(IPU3_PT_PTES << 2)
#define IPU3_PT_ORDER		(IPU3_PT_SIZE >> PAGE_SHIFT)

#define IPU3_ADDR2PTE(addr)	((addr) >> IPU3_PAGE_SHIFT)
#define IPU3_PTE2ADDR(pte)	((phys_addr_t)(pte) << IPU3_PAGE_SHIFT)

#define IPU3_L2PT_SHIFT		IPU3_PT_BITS
#define IPU3_L2PT_MASK		((1UL << IPU3_L2PT_SHIFT) - 1)

#define IPU3_L1PT_SHIFT		IPU3_PT_BITS
#define IPU3_L1PT_MASK		((1UL << IPU3_L1PT_SHIFT) - 1)

#define IPU3_MMU_ADDRESS_BITS	(IPU3_PAGE_SHIFT + \
				 IPU3_L2PT_SHIFT + \
				 IPU3_L1PT_SHIFT)

#define IMGU_REG_BASE		0x4000
#define REG_TLB_INVALIDATE	(IMGU_REG_BASE + 0x300)
#define TLB_INVALIDATE		1
#define REG_L1_PHYS		(IMGU_REG_BASE + 0x304) /* 27-bit pfn */
#define REG_GP_HALT		(IMGU_REG_BASE + 0x5dc)
#define REG_GP_HALTED		(IMGU_REG_BASE + 0x5e0)

struct ipu3_mmu {
	struct device *dev;
	void __iomem *base;
	/* protect access to l2pts, l1pt */
	spinlock_t lock;

	void *dummy_page;
	u32 dummy_page_pteval;

	u32 *dummy_l2pt;
	u32 dummy_l2pt_pteval;

	u32 **l2pts;
	u32 *l1pt;

	struct ipu3_mmu_info geometry;
};

static inline struct ipu3_mmu *to_ipu3_mmu(struct ipu3_mmu_info *info)
{
	return container_of(info, struct ipu3_mmu, geometry);
}

/**
 * ipu3_mmu_tlb_invalidate - invalidate translation look-aside buffer
 * @mmu: MMU to perform the invalidate operation on
 *
 * This function invalidates the whole TLB. Must be called when the hardware
 * is powered on.
 */
static void ipu3_mmu_tlb_invalidate(struct ipu3_mmu *mmu)
{
	writel(TLB_INVALIDATE, mmu->base + REG_TLB_INVALIDATE);
}

static void call_if_ipu3_is_powered(struct ipu3_mmu *mmu,
				    void (*func)(struct ipu3_mmu *mmu))
{
	if (!pm_runtime_get_if_in_use(mmu->dev))
		return;

	func(mmu);
	pm_runtime_put(mmu->dev);
}

/**
 * ipu3_mmu_set_halt - set CIO gate halt bit
 * @mmu: MMU to set the CIO gate bit in.
 * @halt: Desired state of the gate bit.
 *
 * This function sets the CIO gate bit that controls whether external memory
 * accesses are allowed. Must be called when the hardware is powered on.
 */
static void ipu3_mmu_set_halt(struct ipu3_mmu *mmu, bool halt)
{
	int ret;
	u32 val;

	writel(halt, mmu->base + REG_GP_HALT);
	ret = readl_poll_timeout(mmu->base + REG_GP_HALTED,
				 val, (val & 1) == halt, 1000, 100000);

	if (ret)
		dev_err(mmu->dev, "failed to %s CIO gate halt\n",
			halt ? "set" : "clear");
}

/**
 * ipu3_mmu_alloc_page_table - allocate a pre-filled page table
 * @pteval: Value to initialize for page table entries with.
 *
 * Return: Pointer to allocated page table or NULL on failure.
 */
static u32 *ipu3_mmu_alloc_page_table(u32 pteval)
{
	u32 *pt;
	int pte;

	pt = (u32 *)__get_free_page(GFP_KERNEL);
	if (!pt)
		return NULL;

	for (pte = 0; pte < IPU3_PT_PTES; pte++)
		pt[pte] = pteval;

	set_memory_uc((unsigned long int)pt, IPU3_PT_ORDER);

	return pt;
}

/**
 * ipu3_mmu_free_page_table - free page table
 * @pt: Page table to free.
 */
static void ipu3_mmu_free_page_table(u32 *pt)
{
	set_memory_wb((unsigned long int)pt, IPU3_PT_ORDER);
	free_page((unsigned long)pt);
}

/**
 * address_to_pte_idx - split IOVA into L1 and L2 page table indices
 * @iova: IOVA to split.
 * @l1pt_idx: Output for the L1 page table index.
 * @l2pt_idx: Output for the L2 page index.
 */
static inline void address_to_pte_idx(unsigned long iova, u32 *l1pt_idx,
				      u32 *l2pt_idx)
{
	iova >>= IPU3_PAGE_SHIFT;

	if (l2pt_idx)
		*l2pt_idx = iova & IPU3_L2PT_MASK;

	iova >>= IPU3_L2PT_SHIFT;

	if (l1pt_idx)
		*l1pt_idx = iova & IPU3_L1PT_MASK;
}

static u32 *ipu3_mmu_get_l2pt(struct ipu3_mmu *mmu, u32 l1pt_idx)
{
	unsigned long flags;
	u32 *l2pt, *new_l2pt;
	u32 pteval;

	spin_lock_irqsave(&mmu->lock, flags);

	l2pt = mmu->l2pts[l1pt_idx];
	if (l2pt)
		goto done;

	spin_unlock_irqrestore(&mmu->lock, flags);

	new_l2pt = ipu3_mmu_alloc_page_table(mmu->dummy_page_pteval);
	if (!new_l2pt)
		return NULL;

	spin_lock_irqsave(&mmu->lock, flags);

	dev_dbg(mmu->dev, "allocated page table %p for l1pt_idx %u\n",
		new_l2pt, l1pt_idx);

	l2pt = mmu->l2pts[l1pt_idx];
	if (l2pt) {
		ipu3_mmu_free_page_table(new_l2pt);
		goto done;
	}

	l2pt = new_l2pt;
	mmu->l2pts[l1pt_idx] = new_l2pt;

	pteval = IPU3_ADDR2PTE(virt_to_phys(new_l2pt));
	mmu->l1pt[l1pt_idx] = pteval;

done:
	spin_unlock_irqrestore(&mmu->lock, flags);
	return l2pt;
}

static int __ipu3_mmu_map(struct ipu3_mmu *mmu, unsigned long iova,
			  phys_addr_t paddr)
{
	u32 l1pt_idx, l2pt_idx;
	unsigned long flags;
	u32 *l2pt;

	if (!mmu)
		return -ENODEV;

	address_to_pte_idx(iova, &l1pt_idx, &l2pt_idx);

	l2pt = ipu3_mmu_get_l2pt(mmu, l1pt_idx);
	if (!l2pt)
		return -ENOMEM;

	spin_lock_irqsave(&mmu->lock, flags);

	if (l2pt[l2pt_idx] != mmu->dummy_page_pteval) {
		spin_unlock_irqrestore(&mmu->lock, flags);
		return -EBUSY;
	}

	l2pt[l2pt_idx] = IPU3_ADDR2PTE(paddr);

	spin_unlock_irqrestore(&mmu->lock, flags);

	return 0;
}

/**
 * The following four functions are implemented based on iommu.c
 * drivers/iommu/iommu.c/iommu_pgsize().
 */
static size_t ipu3_mmu_pgsize(unsigned long pgsize_bitmap,
			      unsigned long addr_merge, size_t size)
{
	unsigned int pgsize_idx;
	size_t pgsize;

	/* Max page size that still fits into 'size' */
	pgsize_idx = __fls(size);

	/* need to consider alignment requirements ? */
	if (likely(addr_merge)) {
		/* Max page size allowed by address */
		unsigned int align_pgsize_idx = __ffs(addr_merge);

		pgsize_idx = min(pgsize_idx, align_pgsize_idx);
	}

	/* build a mask of acceptable page sizes */
	pgsize = (1UL << (pgsize_idx + 1)) - 1;

	/* throw away page sizes not supported by the hardware */
	pgsize &= pgsize_bitmap;

	/* make sure we're still sane */
	WARN_ON(!pgsize);

	/* pick the biggest page */
	pgsize_idx = __fls(pgsize);
	pgsize = 1UL << pgsize_idx;

	return pgsize;
}

/* drivers/iommu/iommu.c/iommu_map() */
int ipu3_mmu_map(struct ipu3_mmu_info *info, unsigned long iova,
		 phys_addr_t paddr, size_t size)
{
	struct ipu3_mmu *mmu = to_ipu3_mmu(info);
	unsigned int min_pagesz;
	int ret = 0;

	/* find out the minimum page size supported */
	min_pagesz = 1 << __ffs(mmu->geometry.pgsize_bitmap);

	/*
	 * both the virtual address and the physical one, as well as
	 * the size of the mapping, must be aligned (at least) to the
	 * size of the smallest page supported by the hardware
	 */
	if (!IS_ALIGNED(iova | paddr | size, min_pagesz)) {
		dev_err(mmu->dev, "unaligned: iova 0x%lx pa %pa size 0x%zx min_pagesz 0x%x\n",
			iova, &paddr, size, min_pagesz);
		return -EINVAL;
	}

	dev_dbg(mmu->dev, "map: iova 0x%lx pa %pa size 0x%zx\n",
		iova, &paddr, size);

	while (size) {
		size_t pgsize = ipu3_mmu_pgsize(mmu->geometry.pgsize_bitmap,
						iova | paddr, size);

		dev_dbg(mmu->dev, "mapping: iova 0x%lx pa %pa pgsize 0x%zx\n",
			iova, &paddr, pgsize);

		ret = __ipu3_mmu_map(mmu, iova, paddr);
		if (ret)
			break;

		iova += pgsize;
		paddr += pgsize;
		size -= pgsize;
	}

	call_if_ipu3_is_powered(mmu, ipu3_mmu_tlb_invalidate);

	return ret;
}

/* drivers/iommu/iommu.c/default_iommu_map_sg() */
size_t ipu3_mmu_map_sg(struct ipu3_mmu_info *info, unsigned long iova,
		       struct scatterlist *sg, unsigned int nents)
{
	struct ipu3_mmu *mmu = to_ipu3_mmu(info);
	struct scatterlist *s;
	size_t s_length, mapped = 0;
	unsigned int i, min_pagesz;
	int ret;

	min_pagesz = 1 << __ffs(mmu->geometry.pgsize_bitmap);

	for_each_sg(sg, s, nents, i) {
		phys_addr_t phys = page_to_phys(sg_page(s)) + s->offset;

		s_length = s->length;

		if (!IS_ALIGNED(s->offset, min_pagesz))
			goto out_err;

		/* must be min_pagesz aligned to be mapped singlely */
		if (i == nents - 1 && !IS_ALIGNED(s->length, min_pagesz))
			s_length = PAGE_ALIGN(s->length);

		ret = ipu3_mmu_map(info, iova + mapped, phys, s_length);
		if (ret)
			goto out_err;

		mapped += s_length;
	}

	call_if_ipu3_is_powered(mmu, ipu3_mmu_tlb_invalidate);

	return mapped;

out_err:
	/* undo mappings already done */
	ipu3_mmu_unmap(info, iova, mapped);

	return 0;
}

static size_t __ipu3_mmu_unmap(struct ipu3_mmu *mmu,
			       unsigned long iova, size_t size)
{
	u32 l1pt_idx, l2pt_idx;
	unsigned long flags;
	size_t unmap = size;
	u32 *l2pt;

	if (!mmu)
		return 0;

	address_to_pte_idx(iova, &l1pt_idx, &l2pt_idx);

	spin_lock_irqsave(&mmu->lock, flags);

	l2pt = mmu->l2pts[l1pt_idx];
	if (!l2pt) {
		spin_unlock_irqrestore(&mmu->lock, flags);
		return 0;
	}

	if (l2pt[l2pt_idx] == mmu->dummy_page_pteval)
		unmap = 0;

	l2pt[l2pt_idx] = mmu->dummy_page_pteval;

	spin_unlock_irqrestore(&mmu->lock, flags);

	return unmap;
}

/* drivers/iommu/iommu.c/iommu_unmap() */
size_t ipu3_mmu_unmap(struct ipu3_mmu_info *info, unsigned long iova,
		      size_t size)
{
	struct ipu3_mmu *mmu = to_ipu3_mmu(info);
	size_t unmapped_page, unmapped = 0;
	unsigned int min_pagesz;

	/* find out the minimum page size supported */
	min_pagesz = 1 << __ffs(mmu->geometry.pgsize_bitmap);

	/*
	 * The virtual address, as well as the size of the mapping, must be
	 * aligned (at least) to the size of the smallest page supported
	 * by the hardware
	 */
	if (!IS_ALIGNED(iova | size, min_pagesz)) {
		dev_err(mmu->dev, "unaligned: iova 0x%lx size 0x%zx min_pagesz 0x%x\n",
			iova, size, min_pagesz);
		return -EINVAL;
	}

	dev_dbg(mmu->dev, "unmap this: iova 0x%lx size 0x%zx\n", iova, size);

	/*
	 * Keep iterating until we either unmap 'size' bytes (or more)
	 * or we hit an area that isn't mapped.
	 */
	while (unmapped < size) {
		size_t pgsize = ipu3_mmu_pgsize(mmu->geometry.pgsize_bitmap,
						iova, size - unmapped);

		unmapped_page = __ipu3_mmu_unmap(mmu, iova, pgsize);
		if (!unmapped_page)
			break;

		dev_dbg(mmu->dev, "unmapped: iova 0x%lx size 0x%zx\n",
			iova, unmapped_page);

		iova += unmapped_page;
		unmapped += unmapped_page;
	}

	call_if_ipu3_is_powered(mmu, ipu3_mmu_tlb_invalidate);

	return unmapped;
}

/**
 * ipu3_mmu_init() - initialize IPU3 MMU block
 * @base:	IOMEM base of hardware registers.
 *
 * Return: Pointer to IPU3 MMU private data pointer or ERR_PTR() on error.
 */
struct ipu3_mmu_info *ipu3_mmu_init(struct device *parent, void __iomem *base)
{
	struct ipu3_mmu *mmu;
	u32 pteval;

	mmu = kzalloc(sizeof(*mmu), GFP_KERNEL);
	if (!mmu)
		return ERR_PTR(-ENOMEM);

	mmu->dev = parent;
	mmu->base = base;
	spin_lock_init(&mmu->lock);

	/* Disallow external memory access when having no valid page tables. */
	ipu3_mmu_set_halt(mmu, true);

	/*
	 * The MMU does not have a "valid" bit, so we have to use a dummy
	 * page for invalid entries.
	 */
	mmu->dummy_page = (void *)__get_free_page(GFP_KERNEL);
	if (!mmu->dummy_page)
		goto fail_group;
	pteval = IPU3_ADDR2PTE(virt_to_phys(mmu->dummy_page));
	mmu->dummy_page_pteval = pteval;

	/*
	 * Allocate a dummy L2 page table with all entries pointing to
	 * the dummy page.
	 */
	mmu->dummy_l2pt = ipu3_mmu_alloc_page_table(pteval);
	if (!mmu->dummy_l2pt)
		goto fail_dummy_page;
	pteval = IPU3_ADDR2PTE(virt_to_phys(mmu->dummy_l2pt));
	mmu->dummy_l2pt_pteval = pteval;

	/*
	 * Allocate the array of L2PT CPU pointers, initialized to zero,
	 * which means the dummy L2PT allocated above.
	 */
	mmu->l2pts = vzalloc(IPU3_PT_PTES * sizeof(*mmu->l2pts));
	if (!mmu->l2pts)
		goto fail_l2pt;

	/* Allocate the L1 page table. */
	mmu->l1pt = ipu3_mmu_alloc_page_table(mmu->dummy_l2pt_pteval);
	if (!mmu->l1pt)
		goto fail_l2pts;

	pteval = IPU3_ADDR2PTE(virt_to_phys(mmu->l1pt));
	writel(pteval, mmu->base + REG_L1_PHYS);
	ipu3_mmu_tlb_invalidate(mmu);
	ipu3_mmu_set_halt(mmu, false);

	mmu->geometry.aperture_start = 0;
	mmu->geometry.aperture_end = DMA_BIT_MASK(IPU3_MMU_ADDRESS_BITS);
	mmu->geometry.pgsize_bitmap = IPU3_PAGE_SIZE;

	return &mmu->geometry;

fail_l2pts:
	vfree(mmu->l2pts);
fail_l2pt:
	ipu3_mmu_free_page_table(mmu->dummy_l2pt);
fail_dummy_page:
	free_page((unsigned long)mmu->dummy_page);
fail_group:
	kfree(mmu);

	return ERR_PTR(-ENOMEM);
}

/**
 * ipu3_mmu_exit() - clean up IPU3 MMU block
 * @mmu: IPU3 MMU private data
 */
void ipu3_mmu_exit(struct ipu3_mmu_info *info)
{
	struct ipu3_mmu *mmu = to_ipu3_mmu(info);

	/* We are going to free our page tables, no more memory access. */
	ipu3_mmu_set_halt(mmu, true);
	ipu3_mmu_tlb_invalidate(mmu);

	ipu3_mmu_free_page_table(mmu->l1pt);
	vfree(mmu->l2pts);
	ipu3_mmu_free_page_table(mmu->dummy_l2pt);
	free_page((unsigned long)mmu->dummy_page);
	kfree(mmu);
}

void ipu3_mmu_suspend(struct ipu3_mmu_info *info)
{
	struct ipu3_mmu *mmu = to_ipu3_mmu(info);

	ipu3_mmu_set_halt(mmu, true);
}

void ipu3_mmu_resume(struct ipu3_mmu_info *info)
{
	struct ipu3_mmu *mmu = to_ipu3_mmu(info);
	u32 pteval;

	ipu3_mmu_set_halt(mmu, true);

	pteval = IPU3_ADDR2PTE(virt_to_phys(mmu->l1pt));
	writel(pteval, mmu->base + REG_L1_PHYS);

	ipu3_mmu_tlb_invalidate(mmu);
	ipu3_mmu_set_halt(mmu, false);
}
