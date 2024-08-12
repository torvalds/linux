// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013--2024 Intel Corporation
 */
#include <asm/barrier.h>

#include <linux/align.h>
#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/bug.h>
#include <linux/cacheflush.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/iova.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/mm.h>
#include <linux/pfn.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#include "ipu6.h"
#include "ipu6-dma.h"
#include "ipu6-mmu.h"
#include "ipu6-platform-regs.h"

#define ISP_PAGE_SHIFT		12
#define ISP_PAGE_SIZE		BIT(ISP_PAGE_SHIFT)
#define ISP_PAGE_MASK		(~(ISP_PAGE_SIZE - 1))

#define ISP_L1PT_SHIFT		22
#define ISP_L1PT_MASK		(~((1U << ISP_L1PT_SHIFT) - 1))

#define ISP_L2PT_SHIFT		12
#define ISP_L2PT_MASK		(~(ISP_L1PT_MASK | (~(ISP_PAGE_MASK))))

#define ISP_L1PT_PTES           1024
#define ISP_L2PT_PTES           1024

#define ISP_PADDR_SHIFT		12

#define REG_TLB_INVALIDATE	0x0000

#define REG_L1_PHYS		0x0004	/* 27-bit pfn */
#define REG_INFO		0x0008

#define TBL_PHYS_ADDR(a)	((phys_addr_t)(a) << ISP_PADDR_SHIFT)

static void tlb_invalidate(struct ipu6_mmu *mmu)
{
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&mmu->ready_lock, flags);
	if (!mmu->ready) {
		spin_unlock_irqrestore(&mmu->ready_lock, flags);
		return;
	}

	for (i = 0; i < mmu->nr_mmus; i++) {
		/*
		 * To avoid the HW bug induced dead lock in some of the IPU6
		 * MMUs on successive invalidate calls, we need to first do a
		 * read to the page table base before writing the invalidate
		 * register. MMUs which need to implement this WA, will have
		 * the insert_read_before_invalidate flags set as true.
		 * Disregard the return value of the read.
		 */
		if (mmu->mmu_hw[i].insert_read_before_invalidate)
			readl(mmu->mmu_hw[i].base + REG_L1_PHYS);

		writel(0xffffffff, mmu->mmu_hw[i].base +
		       REG_TLB_INVALIDATE);
		/*
		 * The TLB invalidation is a "single cycle" (IOMMU clock cycles)
		 * When the actual MMIO write reaches the IPU6 TLB Invalidate
		 * register, wmb() will force the TLB invalidate out if the CPU
		 * attempts to update the IOMMU page table (or sooner).
		 */
		wmb();
	}
	spin_unlock_irqrestore(&mmu->ready_lock, flags);
}

#ifdef DEBUG
static void page_table_dump(struct ipu6_mmu_info *mmu_info)
{
	u32 l1_idx;

	dev_dbg(mmu_info->dev, "begin IOMMU page table dump\n");

	for (l1_idx = 0; l1_idx < ISP_L1PT_PTES; l1_idx++) {
		u32 l2_idx;
		u32 iova = (phys_addr_t)l1_idx << ISP_L1PT_SHIFT;

		if (mmu_info->l1_pt[l1_idx] == mmu_info->dummy_l2_pteval)
			continue;
		dev_dbg(mmu_info->dev,
			"l1 entry %u; iovas 0x%8.8x-0x%8.8x, at %pa\n",
			l1_idx, iova, iova + ISP_PAGE_SIZE,
			TBL_PHYS_ADDR(mmu_info->l1_pt[l1_idx]));

		for (l2_idx = 0; l2_idx < ISP_L2PT_PTES; l2_idx++) {
			u32 *l2_pt = mmu_info->l2_pts[l1_idx];
			u32 iova2 = iova + (l2_idx << ISP_L2PT_SHIFT);

			if (l2_pt[l2_idx] == mmu_info->dummy_page_pteval)
				continue;

			dev_dbg(mmu_info->dev,
				"\tl2 entry %u; iova 0x%8.8x, phys %pa\n",
				l2_idx, iova2,
				TBL_PHYS_ADDR(l2_pt[l2_idx]));
		}
	}

	dev_dbg(mmu_info->dev, "end IOMMU page table dump\n");
}
#endif /* DEBUG */

static dma_addr_t map_single(struct ipu6_mmu_info *mmu_info, void *ptr)
{
	dma_addr_t dma;

	dma = dma_map_single(mmu_info->dev, ptr, PAGE_SIZE, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(mmu_info->dev, dma))
		return 0;

	return dma;
}

static int get_dummy_page(struct ipu6_mmu_info *mmu_info)
{
	void *pt = (void *)get_zeroed_page(GFP_ATOMIC | GFP_DMA32);
	dma_addr_t dma;

	if (!pt)
		return -ENOMEM;

	dev_dbg(mmu_info->dev, "dummy_page: get_zeroed_page() == %p\n", pt);

	dma = map_single(mmu_info, pt);
	if (!dma) {
		dev_err(mmu_info->dev, "Failed to map dummy page\n");
		goto err_free_page;
	}

	mmu_info->dummy_page = pt;
	mmu_info->dummy_page_pteval = dma >> ISP_PAGE_SHIFT;

	return 0;

err_free_page:
	free_page((unsigned long)pt);
	return -ENOMEM;
}

static void free_dummy_page(struct ipu6_mmu_info *mmu_info)
{
	dma_unmap_single(mmu_info->dev,
			 TBL_PHYS_ADDR(mmu_info->dummy_page_pteval),
			 PAGE_SIZE, DMA_BIDIRECTIONAL);
	free_page((unsigned long)mmu_info->dummy_page);
}

static int alloc_dummy_l2_pt(struct ipu6_mmu_info *mmu_info)
{
	u32 *pt = (u32 *)get_zeroed_page(GFP_ATOMIC | GFP_DMA32);
	dma_addr_t dma;
	unsigned int i;

	if (!pt)
		return -ENOMEM;

	dev_dbg(mmu_info->dev, "dummy_l2: get_zeroed_page() = %p\n", pt);

	dma = map_single(mmu_info, pt);
	if (!dma) {
		dev_err(mmu_info->dev, "Failed to map l2pt page\n");
		goto err_free_page;
	}

	for (i = 0; i < ISP_L2PT_PTES; i++)
		pt[i] = mmu_info->dummy_page_pteval;

	mmu_info->dummy_l2_pt = pt;
	mmu_info->dummy_l2_pteval = dma >> ISP_PAGE_SHIFT;

	return 0;

err_free_page:
	free_page((unsigned long)pt);
	return -ENOMEM;
}

static void free_dummy_l2_pt(struct ipu6_mmu_info *mmu_info)
{
	dma_unmap_single(mmu_info->dev,
			 TBL_PHYS_ADDR(mmu_info->dummy_l2_pteval),
			 PAGE_SIZE, DMA_BIDIRECTIONAL);
	free_page((unsigned long)mmu_info->dummy_l2_pt);
}

static u32 *alloc_l1_pt(struct ipu6_mmu_info *mmu_info)
{
	u32 *pt = (u32 *)get_zeroed_page(GFP_ATOMIC | GFP_DMA32);
	dma_addr_t dma;
	unsigned int i;

	if (!pt)
		return NULL;

	dev_dbg(mmu_info->dev, "alloc_l1: get_zeroed_page() = %p\n", pt);

	for (i = 0; i < ISP_L1PT_PTES; i++)
		pt[i] = mmu_info->dummy_l2_pteval;

	dma = map_single(mmu_info, pt);
	if (!dma) {
		dev_err(mmu_info->dev, "Failed to map l1pt page\n");
		goto err_free_page;
	}

	mmu_info->l1_pt_dma = dma >> ISP_PADDR_SHIFT;
	dev_dbg(mmu_info->dev, "l1 pt %p mapped at %llx\n", pt, dma);

	return pt;

err_free_page:
	free_page((unsigned long)pt);
	return NULL;
}

static u32 *alloc_l2_pt(struct ipu6_mmu_info *mmu_info)
{
	u32 *pt = (u32 *)get_zeroed_page(GFP_ATOMIC | GFP_DMA32);
	unsigned int i;

	if (!pt)
		return NULL;

	dev_dbg(mmu_info->dev, "alloc_l2: get_zeroed_page() = %p\n", pt);

	for (i = 0; i < ISP_L1PT_PTES; i++)
		pt[i] = mmu_info->dummy_page_pteval;

	return pt;
}

static int l2_map(struct ipu6_mmu_info *mmu_info, unsigned long iova,
		  phys_addr_t paddr, size_t size)
{
	u32 l1_idx = iova >> ISP_L1PT_SHIFT;
	u32 iova_start = iova;
	u32 *l2_pt, *l2_virt;
	unsigned int l2_idx;
	unsigned long flags;
	dma_addr_t dma;
	u32 l1_entry;

	dev_dbg(mmu_info->dev,
		"mapping l2 page table for l1 index %u (iova %8.8x)\n",
		l1_idx, (u32)iova);

	spin_lock_irqsave(&mmu_info->lock, flags);
	l1_entry = mmu_info->l1_pt[l1_idx];
	if (l1_entry == mmu_info->dummy_l2_pteval) {
		l2_virt = mmu_info->l2_pts[l1_idx];
		if (likely(!l2_virt)) {
			l2_virt = alloc_l2_pt(mmu_info);
			if (!l2_virt) {
				spin_unlock_irqrestore(&mmu_info->lock, flags);
				return -ENOMEM;
			}
		}

		dma = map_single(mmu_info, l2_virt);
		if (!dma) {
			dev_err(mmu_info->dev, "Failed to map l2pt page\n");
			free_page((unsigned long)l2_virt);
			spin_unlock_irqrestore(&mmu_info->lock, flags);
			return -EINVAL;
		}

		l1_entry = dma >> ISP_PADDR_SHIFT;

		dev_dbg(mmu_info->dev, "page for l1_idx %u %p allocated\n",
			l1_idx, l2_virt);
		mmu_info->l1_pt[l1_idx] = l1_entry;
		mmu_info->l2_pts[l1_idx] = l2_virt;
		clflush_cache_range((void *)&mmu_info->l1_pt[l1_idx],
				    sizeof(mmu_info->l1_pt[l1_idx]));
	}

	l2_pt = mmu_info->l2_pts[l1_idx];

	dev_dbg(mmu_info->dev, "l2_pt at %p with dma 0x%x\n", l2_pt, l1_entry);

	paddr = ALIGN(paddr, ISP_PAGE_SIZE);

	l2_idx = (iova_start & ISP_L2PT_MASK) >> ISP_L2PT_SHIFT;

	dev_dbg(mmu_info->dev, "l2_idx %u, phys 0x%8.8x\n", l2_idx,
		l2_pt[l2_idx]);
	if (l2_pt[l2_idx] != mmu_info->dummy_page_pteval) {
		spin_unlock_irqrestore(&mmu_info->lock, flags);
		return -EINVAL;
	}

	l2_pt[l2_idx] = paddr >> ISP_PADDR_SHIFT;

	clflush_cache_range((void *)&l2_pt[l2_idx], sizeof(l2_pt[l2_idx]));
	spin_unlock_irqrestore(&mmu_info->lock, flags);

	dev_dbg(mmu_info->dev, "l2 index %u mapped as 0x%8.8x\n", l2_idx,
		l2_pt[l2_idx]);

	return 0;
}

static int __ipu6_mmu_map(struct ipu6_mmu_info *mmu_info, unsigned long iova,
			  phys_addr_t paddr, size_t size)
{
	u32 iova_start = round_down(iova, ISP_PAGE_SIZE);
	u32 iova_end = ALIGN(iova + size, ISP_PAGE_SIZE);

	dev_dbg(mmu_info->dev,
		"mapping iova 0x%8.8x--0x%8.8x, size %zu at paddr 0x%10.10llx\n",
		iova_start, iova_end, size, paddr);

	return l2_map(mmu_info, iova_start, paddr, size);
}

static size_t l2_unmap(struct ipu6_mmu_info *mmu_info, unsigned long iova,
		       phys_addr_t dummy, size_t size)
{
	u32 l1_idx = iova >> ISP_L1PT_SHIFT;
	u32 iova_start = iova;
	unsigned int l2_idx;
	size_t unmapped = 0;
	unsigned long flags;
	u32 *l2_pt;

	dev_dbg(mmu_info->dev, "unmapping l2 page table for l1 index %u (iova 0x%8.8lx)\n",
		l1_idx, iova);

	spin_lock_irqsave(&mmu_info->lock, flags);
	if (mmu_info->l1_pt[l1_idx] == mmu_info->dummy_l2_pteval) {
		spin_unlock_irqrestore(&mmu_info->lock, flags);
		dev_err(mmu_info->dev,
			"unmap iova 0x%8.8lx l1 idx %u which was not mapped\n",
			iova, l1_idx);
		return 0;
	}

	for (l2_idx = (iova_start & ISP_L2PT_MASK) >> ISP_L2PT_SHIFT;
	     (iova_start & ISP_L1PT_MASK) + (l2_idx << ISP_PAGE_SHIFT)
		     < iova_start + size && l2_idx < ISP_L2PT_PTES; l2_idx++) {
		l2_pt = mmu_info->l2_pts[l1_idx];
		dev_dbg(mmu_info->dev,
			"unmap l2 index %u with pteval 0x%10.10llx\n",
			l2_idx, TBL_PHYS_ADDR(l2_pt[l2_idx]));
		l2_pt[l2_idx] = mmu_info->dummy_page_pteval;

		clflush_cache_range((void *)&l2_pt[l2_idx],
				    sizeof(l2_pt[l2_idx]));
		unmapped++;
	}
	spin_unlock_irqrestore(&mmu_info->lock, flags);

	return unmapped << ISP_PAGE_SHIFT;
}

static size_t __ipu6_mmu_unmap(struct ipu6_mmu_info *mmu_info,
			       unsigned long iova, size_t size)
{
	return l2_unmap(mmu_info, iova, 0, size);
}

static int allocate_trash_buffer(struct ipu6_mmu *mmu)
{
	unsigned int n_pages = PHYS_PFN(PAGE_ALIGN(IPU6_MMUV2_TRASH_RANGE));
	struct iova *iova;
	unsigned int i;
	dma_addr_t dma;
	unsigned long iova_addr;
	int ret;

	/* Allocate 8MB in iova range */
	iova = alloc_iova(&mmu->dmap->iovad, n_pages,
			  PHYS_PFN(mmu->dmap->mmu_info->aperture_end), 0);
	if (!iova) {
		dev_err(mmu->dev, "cannot allocate iova range for trash\n");
		return -ENOMEM;
	}

	dma = dma_map_page(mmu->dmap->mmu_info->dev, mmu->trash_page, 0,
			   PAGE_SIZE, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(mmu->dmap->mmu_info->dev, dma)) {
		dev_err(mmu->dmap->mmu_info->dev, "Failed to map trash page\n");
		ret = -ENOMEM;
		goto out_free_iova;
	}

	mmu->pci_trash_page = dma;

	/*
	 * Map the 8MB iova address range to the same physical trash page
	 * mmu->trash_page which is already reserved at the probe
	 */
	iova_addr = iova->pfn_lo;
	for (i = 0; i < n_pages; i++) {
		ret = ipu6_mmu_map(mmu->dmap->mmu_info, PFN_PHYS(iova_addr),
				   mmu->pci_trash_page, PAGE_SIZE);
		if (ret) {
			dev_err(mmu->dev,
				"mapping trash buffer range failed\n");
			goto out_unmap;
		}

		iova_addr++;
	}

	mmu->iova_trash_page = PFN_PHYS(iova->pfn_lo);
	dev_dbg(mmu->dev, "iova trash buffer for MMUID: %d is %u\n",
		mmu->mmid, (unsigned int)mmu->iova_trash_page);
	return 0;

out_unmap:
	ipu6_mmu_unmap(mmu->dmap->mmu_info, PFN_PHYS(iova->pfn_lo),
		       PFN_PHYS(iova_size(iova)));
	dma_unmap_page(mmu->dmap->mmu_info->dev, mmu->pci_trash_page,
		       PAGE_SIZE, DMA_BIDIRECTIONAL);
out_free_iova:
	__free_iova(&mmu->dmap->iovad, iova);
	return ret;
}

int ipu6_mmu_hw_init(struct ipu6_mmu *mmu)
{
	struct ipu6_mmu_info *mmu_info;
	unsigned long flags;
	unsigned int i;

	mmu_info = mmu->dmap->mmu_info;

	/* Initialise the each MMU HW block */
	for (i = 0; i < mmu->nr_mmus; i++) {
		struct ipu6_mmu_hw *mmu_hw = &mmu->mmu_hw[i];
		unsigned int j;
		u16 block_addr;

		/* Write page table address per MMU */
		writel((phys_addr_t)mmu_info->l1_pt_dma,
		       mmu->mmu_hw[i].base + REG_L1_PHYS);

		/* Set info bits per MMU */
		writel(mmu->mmu_hw[i].info_bits,
		       mmu->mmu_hw[i].base + REG_INFO);

		/* Configure MMU TLB stream configuration for L1 */
		for (j = 0, block_addr = 0; j < mmu_hw->nr_l1streams;
		     block_addr += mmu->mmu_hw[i].l1_block_sz[j], j++) {
			if (block_addr > IPU6_MAX_LI_BLOCK_ADDR) {
				dev_err(mmu->dev, "invalid L1 configuration\n");
				return -EINVAL;
			}

			/* Write block start address for each streams */
			writel(block_addr, mmu_hw->base +
			       mmu_hw->l1_stream_id_reg_offset + 4 * j);
		}

		/* Configure MMU TLB stream configuration for L2 */
		for (j = 0, block_addr = 0; j < mmu_hw->nr_l2streams;
		     block_addr += mmu->mmu_hw[i].l2_block_sz[j], j++) {
			if (block_addr > IPU6_MAX_L2_BLOCK_ADDR) {
				dev_err(mmu->dev, "invalid L2 configuration\n");
				return -EINVAL;
			}

			writel(block_addr, mmu_hw->base +
			       mmu_hw->l2_stream_id_reg_offset + 4 * j);
		}
	}

	if (!mmu->trash_page) {
		int ret;

		mmu->trash_page = alloc_page(GFP_KERNEL);
		if (!mmu->trash_page) {
			dev_err(mmu->dev, "insufficient memory for trash buffer\n");
			return -ENOMEM;
		}

		ret = allocate_trash_buffer(mmu);
		if (ret) {
			__free_page(mmu->trash_page);
			mmu->trash_page = NULL;
			dev_err(mmu->dev, "trash buffer allocation failed\n");
			return ret;
		}
	}

	spin_lock_irqsave(&mmu->ready_lock, flags);
	mmu->ready = true;
	spin_unlock_irqrestore(&mmu->ready_lock, flags);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ipu6_mmu_hw_init, INTEL_IPU6);

static struct ipu6_mmu_info *ipu6_mmu_alloc(struct ipu6_device *isp)
{
	struct ipu6_mmu_info *mmu_info;
	int ret;

	mmu_info = kzalloc(sizeof(*mmu_info), GFP_KERNEL);
	if (!mmu_info)
		return NULL;

	mmu_info->aperture_start = 0;
	mmu_info->aperture_end = DMA_BIT_MASK(isp->secure_mode ?
					      IPU6_MMU_ADDR_BITS :
					      IPU6_MMU_ADDR_BITS_NON_SECURE);
	mmu_info->pgsize_bitmap = SZ_4K;
	mmu_info->dev = &isp->pdev->dev;

	ret = get_dummy_page(mmu_info);
	if (ret)
		goto err_free_info;

	ret = alloc_dummy_l2_pt(mmu_info);
	if (ret)
		goto err_free_dummy_page;

	mmu_info->l2_pts = vzalloc(ISP_L2PT_PTES * sizeof(*mmu_info->l2_pts));
	if (!mmu_info->l2_pts)
		goto err_free_dummy_l2_pt;

	/*
	 * We always map the L1 page table (a single page as well as
	 * the L2 page tables).
	 */
	mmu_info->l1_pt = alloc_l1_pt(mmu_info);
	if (!mmu_info->l1_pt)
		goto err_free_l2_pts;

	spin_lock_init(&mmu_info->lock);

	dev_dbg(mmu_info->dev, "domain initialised\n");

	return mmu_info;

err_free_l2_pts:
	vfree(mmu_info->l2_pts);
err_free_dummy_l2_pt:
	free_dummy_l2_pt(mmu_info);
err_free_dummy_page:
	free_dummy_page(mmu_info);
err_free_info:
	kfree(mmu_info);

	return NULL;
}

void ipu6_mmu_hw_cleanup(struct ipu6_mmu *mmu)
{
	unsigned long flags;

	spin_lock_irqsave(&mmu->ready_lock, flags);
	mmu->ready = false;
	spin_unlock_irqrestore(&mmu->ready_lock, flags);
}
EXPORT_SYMBOL_NS_GPL(ipu6_mmu_hw_cleanup, INTEL_IPU6);

static struct ipu6_dma_mapping *alloc_dma_mapping(struct ipu6_device *isp)
{
	struct ipu6_dma_mapping *dmap;

	dmap = kzalloc(sizeof(*dmap), GFP_KERNEL);
	if (!dmap)
		return NULL;

	dmap->mmu_info = ipu6_mmu_alloc(isp);
	if (!dmap->mmu_info) {
		kfree(dmap);
		return NULL;
	}

	init_iova_domain(&dmap->iovad, SZ_4K, 1);
	dmap->mmu_info->dmap = dmap;

	dev_dbg(&isp->pdev->dev, "alloc mapping\n");

	iova_cache_get();

	return dmap;
}

phys_addr_t ipu6_mmu_iova_to_phys(struct ipu6_mmu_info *mmu_info,
				  dma_addr_t iova)
{
	phys_addr_t phy_addr;
	unsigned long flags;
	u32 *l2_pt;

	spin_lock_irqsave(&mmu_info->lock, flags);
	l2_pt = mmu_info->l2_pts[iova >> ISP_L1PT_SHIFT];
	phy_addr = (phys_addr_t)l2_pt[(iova & ISP_L2PT_MASK) >> ISP_L2PT_SHIFT];
	phy_addr <<= ISP_PAGE_SHIFT;
	spin_unlock_irqrestore(&mmu_info->lock, flags);

	return phy_addr;
}

static size_t ipu6_mmu_pgsize(unsigned long pgsize_bitmap,
			      unsigned long addr_merge, size_t size)
{
	unsigned int pgsize_idx;
	size_t pgsize;

	/* Max page size that still fits into 'size' */
	pgsize_idx = __fls(size);

	if (likely(addr_merge)) {
		/* Max page size allowed by address */
		unsigned int align_pgsize_idx = __ffs(addr_merge);

		pgsize_idx = min(pgsize_idx, align_pgsize_idx);
	}

	pgsize = (1UL << (pgsize_idx + 1)) - 1;
	pgsize &= pgsize_bitmap;

	WARN_ON(!pgsize);

	/* pick the biggest page */
	pgsize_idx = __fls(pgsize);
	pgsize = 1UL << pgsize_idx;

	return pgsize;
}

size_t ipu6_mmu_unmap(struct ipu6_mmu_info *mmu_info, unsigned long iova,
		      size_t size)
{
	size_t unmapped_page, unmapped = 0;
	unsigned int min_pagesz;

	/* find out the minimum page size supported */
	min_pagesz = 1 << __ffs(mmu_info->pgsize_bitmap);

	/*
	 * The virtual address and the size of the mapping must be
	 * aligned (at least) to the size of the smallest page supported
	 * by the hardware
	 */
	if (!IS_ALIGNED(iova | size, min_pagesz)) {
		dev_err(NULL, "unaligned: iova 0x%lx size 0x%zx min_pagesz 0x%x\n",
			iova, size, min_pagesz);
		return -EINVAL;
	}

	/*
	 * Keep iterating until we either unmap 'size' bytes (or more)
	 * or we hit an area that isn't mapped.
	 */
	while (unmapped < size) {
		size_t pgsize = ipu6_mmu_pgsize(mmu_info->pgsize_bitmap,
						iova, size - unmapped);

		unmapped_page = __ipu6_mmu_unmap(mmu_info, iova, pgsize);
		if (!unmapped_page)
			break;

		dev_dbg(mmu_info->dev, "unmapped: iova 0x%lx size 0x%zx\n",
			iova, unmapped_page);

		iova += unmapped_page;
		unmapped += unmapped_page;
	}

	return unmapped;
}

int ipu6_mmu_map(struct ipu6_mmu_info *mmu_info, unsigned long iova,
		 phys_addr_t paddr, size_t size)
{
	unsigned long orig_iova = iova;
	unsigned int min_pagesz;
	size_t orig_size = size;
	int ret = 0;

	if (mmu_info->pgsize_bitmap == 0UL)
		return -ENODEV;

	/* find out the minimum page size supported */
	min_pagesz = 1 << __ffs(mmu_info->pgsize_bitmap);

	/*
	 * both the virtual address and the physical one, as well as
	 * the size of the mapping, must be aligned (at least) to the
	 * size of the smallest page supported by the hardware
	 */
	if (!IS_ALIGNED(iova | paddr | size, min_pagesz)) {
		dev_err(mmu_info->dev,
			"unaligned: iova %lx pa %pa size %zx min_pagesz %x\n",
			iova, &paddr, size, min_pagesz);
		return -EINVAL;
	}

	dev_dbg(mmu_info->dev, "map: iova 0x%lx pa %pa size 0x%zx\n",
		iova, &paddr, size);

	while (size) {
		size_t pgsize = ipu6_mmu_pgsize(mmu_info->pgsize_bitmap,
						iova | paddr, size);

		dev_dbg(mmu_info->dev,
			"mapping: iova 0x%lx pa %pa pgsize 0x%zx\n",
			iova, &paddr, pgsize);

		ret = __ipu6_mmu_map(mmu_info, iova, paddr, pgsize);
		if (ret)
			break;

		iova += pgsize;
		paddr += pgsize;
		size -= pgsize;
	}

	/* unroll mapping in case something went wrong */
	if (ret)
		ipu6_mmu_unmap(mmu_info, orig_iova, orig_size - size);

	return ret;
}

static void ipu6_mmu_destroy(struct ipu6_mmu *mmu)
{
	struct ipu6_dma_mapping *dmap = mmu->dmap;
	struct ipu6_mmu_info *mmu_info = dmap->mmu_info;
	struct iova *iova;
	u32 l1_idx;

	if (mmu->iova_trash_page) {
		iova = find_iova(&dmap->iovad, PHYS_PFN(mmu->iova_trash_page));
		if (iova) {
			/* unmap and free the trash buffer iova */
			ipu6_mmu_unmap(mmu_info, PFN_PHYS(iova->pfn_lo),
				       PFN_PHYS(iova_size(iova)));
			__free_iova(&dmap->iovad, iova);
		} else {
			dev_err(mmu->dev, "trash buffer iova not found.\n");
		}

		mmu->iova_trash_page = 0;
		dma_unmap_page(mmu_info->dev, mmu->pci_trash_page,
			       PAGE_SIZE, DMA_BIDIRECTIONAL);
		mmu->pci_trash_page = 0;
		__free_page(mmu->trash_page);
	}

	for (l1_idx = 0; l1_idx < ISP_L1PT_PTES; l1_idx++) {
		if (mmu_info->l1_pt[l1_idx] != mmu_info->dummy_l2_pteval) {
			dma_unmap_single(mmu_info->dev,
					 TBL_PHYS_ADDR(mmu_info->l1_pt[l1_idx]),
					 PAGE_SIZE, DMA_BIDIRECTIONAL);
			free_page((unsigned long)mmu_info->l2_pts[l1_idx]);
		}
	}

	vfree(mmu_info->l2_pts);
	free_dummy_page(mmu_info);
	dma_unmap_single(mmu_info->dev, TBL_PHYS_ADDR(mmu_info->l1_pt_dma),
			 PAGE_SIZE, DMA_BIDIRECTIONAL);
	free_page((unsigned long)mmu_info->dummy_l2_pt);
	free_page((unsigned long)mmu_info->l1_pt);
	kfree(mmu_info);
}

struct ipu6_mmu *ipu6_mmu_init(struct device *dev,
			       void __iomem *base, int mmid,
			       const struct ipu6_hw_variants *hw)
{
	struct ipu6_device *isp = pci_get_drvdata(to_pci_dev(dev));
	struct ipu6_mmu_pdata *pdata;
	struct ipu6_mmu *mmu;
	unsigned int i;

	if (hw->nr_mmus > IPU6_MMU_MAX_DEVICES)
		return ERR_PTR(-EINVAL);

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < hw->nr_mmus; i++) {
		struct ipu6_mmu_hw *pdata_mmu = &pdata->mmu_hw[i];
		const struct ipu6_mmu_hw *src_mmu = &hw->mmu_hw[i];

		if (src_mmu->nr_l1streams > IPU6_MMU_MAX_TLB_L1_STREAMS ||
		    src_mmu->nr_l2streams > IPU6_MMU_MAX_TLB_L2_STREAMS)
			return ERR_PTR(-EINVAL);

		*pdata_mmu = *src_mmu;
		pdata_mmu->base = base + src_mmu->offset;
	}

	mmu = devm_kzalloc(dev, sizeof(*mmu), GFP_KERNEL);
	if (!mmu)
		return ERR_PTR(-ENOMEM);

	mmu->mmid = mmid;
	mmu->mmu_hw = pdata->mmu_hw;
	mmu->nr_mmus = hw->nr_mmus;
	mmu->tlb_invalidate = tlb_invalidate;
	mmu->ready = false;
	INIT_LIST_HEAD(&mmu->vma_list);
	spin_lock_init(&mmu->ready_lock);

	mmu->dmap = alloc_dma_mapping(isp);
	if (!mmu->dmap) {
		dev_err(dev, "can't alloc dma mapping\n");
		return ERR_PTR(-ENOMEM);
	}

	return mmu;
}

void ipu6_mmu_cleanup(struct ipu6_mmu *mmu)
{
	struct ipu6_dma_mapping *dmap = mmu->dmap;

	ipu6_mmu_destroy(mmu);
	mmu->dmap = NULL;
	iova_cache_put();
	put_iova_domain(&dmap->iovad);
	kfree(dmap);
}
