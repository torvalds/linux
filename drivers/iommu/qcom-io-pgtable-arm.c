// SPDX-License-Identifier: GPL-2.0-only
/*
 * CPU-agnostic ARM page table allocator.
 *
 * Copyright (C) 2014 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 *
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"arm-lpae io-pgtable: " fmt

#include <linux/atomic.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/io-pgtable.h>
#include <linux/kernel.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/qcom-iommu-util.h>
#include <linux/qcom-io-pgtable.h>

#include <asm/barrier.h>
#include "qcom-io-pgtable-alloc.h"

#include "io-pgtable-arm.h"

#define ARM_LPAE_MAX_ADDR_BITS		52
#define ARM_LPAE_S2_MAX_CONCAT_PAGES	16
#define ARM_LPAE_MAX_LEVELS		4

/* Struct accessors */
#define io_pgtable_to_data(x)						\
	container_of((x), struct arm_lpae_io_pgtable, iop)

#define io_pgtable_ops_to_data(x)					\
	io_pgtable_to_data(io_pgtable_ops_to_pgtable(x))

/*
 * Calculate the right shift amount to get to the portion describing level l
 * in a virtual address mapped by the pagetable in d.
 */
#define ARM_LPAE_LVL_SHIFT(l, d)						\
	(((ARM_LPAE_MAX_LEVELS - (l)) * (d)->bits_per_level) +		\
	ilog2(sizeof(arm_lpae_iopte)))

#define ARM_LPAE_GRANULE(d)						\
	(sizeof(arm_lpae_iopte) << (d)->bits_per_level)
#define ARM_LPAE_PGD_SIZE(d)						\
	(sizeof(arm_lpae_iopte) << (d)->pgd_bits)

#define ARM_LPAE_PTES_PER_TABLE(d)					\
	(ARM_LPAE_GRANULE(d) >> ilog2(sizeof(arm_lpae_iopte)))

/*
 * Calculate the index at level l used to map virtual address a using the
 * pagetable in d.
 */
#define ARM_LPAE_PGD_IDX(l, d)						\
	((l) == (d)->start_level ? (d)->pgd_bits - (d)->bits_per_level : 0)

#define ARM_LPAE_LVL_IDX(a, l, d)						\
	(((u64)(a) >> ARM_LPAE_LVL_SHIFT(l, d)) &			\
	 ((1 << ((d)->bits_per_level + ARM_LPAE_PGD_IDX(l, d))) - 1))

/* Calculate the block/page mapping size at level l for pagetable in d. */
#define ARM_LPAE_BLOCK_SIZE(l, d)	(1ULL << ARM_LPAE_LVL_SHIFT(l, d))

/* Page table bits */
#define ARM_LPAE_PTE_TYPE_SHIFT		0
#define ARM_LPAE_PTE_TYPE_MASK		0x3

#define ARM_LPAE_PTE_TYPE_BLOCK		1
#define ARM_LPAE_PTE_TYPE_TABLE		3
#define ARM_LPAE_PTE_TYPE_PAGE		3

#define ARM_LPAE_PTE_ADDR_MASK		GENMASK_ULL(47, 12)

#define ARM_LPAE_PTE_NSTABLE		(((arm_lpae_iopte)1) << 63)
#define ARM_LPAE_PTE_XN			(((arm_lpae_iopte)3) << 53)
#define ARM_LPAE_PTE_AF			(((arm_lpae_iopte)1) << 10)
#define ARM_LPAE_PTE_SH_NS		(((arm_lpae_iopte)0) << 8)
#define ARM_LPAE_PTE_SH_OS		(((arm_lpae_iopte)2) << 8)
#define ARM_LPAE_PTE_SH_IS		(((arm_lpae_iopte)3) << 8)
#define ARM_LPAE_PTE_NS			(((arm_lpae_iopte)1) << 5)
#define ARM_LPAE_PTE_VALID		(((arm_lpae_iopte)1) << 0)

#define ARM_LPAE_PTE_ATTR_LO_MASK	(((arm_lpae_iopte)0x3ff) << 2)
/* Ignore the contiguous bit for block splitting */
#define ARM_LPAE_PTE_ATTR_HI_MASK	(((arm_lpae_iopte)6) << 52)
#define ARM_LPAE_PTE_ATTR_MASK		(ARM_LPAE_PTE_ATTR_LO_MASK |	\
					 ARM_LPAE_PTE_ATTR_HI_MASK)
/* Software bit for solving coherency races */
#define ARM_LPAE_PTE_SW_SYNC		(((arm_lpae_iopte)1) << 55)

/* Stage-1 PTE */
#define ARM_LPAE_PTE_AP_UNPRIV		(((arm_lpae_iopte)1) << 6)
#define ARM_LPAE_PTE_AP_RDONLY		(((arm_lpae_iopte)2) << 6)
#define ARM_LPAE_PTE_ATTRINDX_SHIFT	2
#define ARM_LPAE_PTE_nG			(((arm_lpae_iopte)1) << 11)

/* Stage-2 PTE */
#define ARM_LPAE_PTE_HAP_FAULT		(((arm_lpae_iopte)0) << 6)
#define ARM_LPAE_PTE_HAP_READ		(((arm_lpae_iopte)1) << 6)
#define ARM_LPAE_PTE_HAP_WRITE		(((arm_lpae_iopte)2) << 6)
#define ARM_LPAE_PTE_MEMATTR_OIWB	(((arm_lpae_iopte)0xf) << 2)
#define ARM_LPAE_PTE_MEMATTR_NC		(((arm_lpae_iopte)0x5) << 2)
#define ARM_LPAE_PTE_MEMATTR_DEV	(((arm_lpae_iopte)0x1) << 2)

/* Register bits */
#define ARM_LPAE_VTCR_SL0_MASK		0x3

#define ARM_LPAE_TCR_T0SZ_SHIFT		0

#define ARM_LPAE_VTCR_PS_SHIFT		16
#define ARM_LPAE_VTCR_PS_MASK		0x7

#define ARM_LPAE_MAIR_ATTR_SHIFT(n)			((n) << 3)
#define ARM_LPAE_MAIR_ATTR_MASK				0xff
#define ARM_LPAE_MAIR_ATTR_DEVICE			0x04ULL
#define ARM_LPAE_MAIR_ATTR_NC				0x44ULL
#define ARM_LPAE_MAIR_ATTR_INC_OWBRANWA			0xe4ULL
#define ARM_LPAE_MAIR_ATTR_IWBRWA_OWBRANWA		0xefULL
#define ARM_LPAE_MAIR_ATTR_INC_OWBRWA			0xf4ULL
#define ARM_LPAE_MAIR_ATTR_WBRWA			0xffULL
#define ARM_LPAE_MAIR_ATTR_IDX_NC			0
#define ARM_LPAE_MAIR_ATTR_IDX_CACHE			1
#define ARM_LPAE_MAIR_ATTR_IDX_DEV			2
#define ARM_LPAE_MAIR_ATTR_IDX_INC_OCACHE		3
#define ARM_LPAE_MAIR_ATTR_IDX_INC_OCACHE_NWA		4
#define ARM_LPAE_MAIR_ATTR_IDX_ICACHE_OCACHE_NWA	5

#define ARM_MALI_LPAE_TTBR_ADRMODE_TABLE (3u << 0)
#define ARM_MALI_LPAE_TTBR_READ_INNER	BIT(2)
#define ARM_MALI_LPAE_TTBR_SHARE_OUTER	BIT(4)

#define ARM_MALI_LPAE_MEMATTR_IMP_DEF	0x88ULL
#define ARM_MALI_LPAE_MEMATTR_WRITE_ALLOC 0x8DULL

/* IOPTE accessors */
#define iopte_deref(pte, d) __va(iopte_to_paddr(pte, d))

#define iopte_type(pte)					\
	(((pte) >> ARM_LPAE_PTE_TYPE_SHIFT) & ARM_LPAE_PTE_TYPE_MASK)

#define iopte_prot(pte)	((pte) & ARM_LPAE_PTE_ATTR_MASK)

struct arm_lpae_io_pgtable {
	struct io_pgtable	iop;

	int			pgd_bits;
	int			start_level;
	int			bits_per_level;

	void			*pgd;
	u32			vmid;
	const struct qcom_iommu_flush_ops *iommu_tlb_ops;
	const struct qcom_iommu_pgtable_log_ops *pgtable_log_ops;
	/* Protects table refcounts */
	spinlock_t		lock;
};

typedef u64 arm_lpae_iopte;

/*
 * We'll use some ignored bits in table entries to keep track of the number
 * of page mappings beneath the table. The maximum number of entries
 * beneath any table mapping in armv8 is 8192 (which is possible at the
 * 2nd and 3rd level when using a 64K granule size). The bits at our
 * disposal are:
 *
 *     4k granule: [54..52], [11..2]
 *    64k granule: [54..52], [15..2]
 *
 * [54..52], [11..2] is enough bits for tracking table mappings at any
 * level for any granule, so we'll use those.
 *
 * If iopte_tblcnt reaches zero for a last-level pagetable, the pagetable
 * will be freed.
 */
#define BOTTOM_IGNORED_MASK GENMASK(11, 2)
#define TOP_IGNORED_MASK GENMASK_ULL(54, 52)
#define IOPTE_RESERVED_MASK (TOP_IGNORED_MASK | BOTTOM_IGNORED_MASK)
#define BOTTOM_VAL_BITS GENMASK(9, 0)
#define TOP_VAL_BITS GENMASK(12, 10)

static arm_lpae_iopte iopte_val(arm_lpae_iopte table_pte)
{
	return table_pte & ~IOPTE_RESERVED_MASK;
}

static int iopte_tblcnt(arm_lpae_iopte table_pte)
{
	int top_cnt = FIELD_GET(TOP_IGNORED_MASK, table_pte);
	int bottom_cnt = FIELD_GET(BOTTOM_IGNORED_MASK, table_pte);

	return FIELD_PREP(TOP_VAL_BITS, top_cnt) |
	       FIELD_PREP(BOTTOM_VAL_BITS, bottom_cnt);
}

static void iopte_tblcnt_set(arm_lpae_iopte *table_pte, int val)
{
	arm_lpae_iopte pte = iopte_val(*table_pte);
	int top_val = FIELD_GET(TOP_VAL_BITS, val);
	int bottom_val = FIELD_GET(BOTTOM_VAL_BITS, val);

	pte |= FIELD_PREP(TOP_IGNORED_MASK, top_val) |
	       FIELD_PREP(BOTTOM_IGNORED_MASK, bottom_val);
	*table_pte = pte;
}

static void iopte_tblcnt_sub(arm_lpae_iopte *table_ptep, int cnt)
{
	int current_cnt = iopte_tblcnt(*table_ptep);

	current_cnt -= cnt;
	iopte_tblcnt_set(table_ptep, current_cnt);
}

static void iopte_tblcnt_add(arm_lpae_iopte *table_ptep, int cnt)
{
	int current_cnt = iopte_tblcnt(*table_ptep);

	current_cnt += cnt;
	iopte_tblcnt_set(table_ptep, current_cnt);
}

static inline bool iopte_leaf(arm_lpae_iopte pte, int lvl,
			      enum io_pgtable_fmt fmt)
{
	if (lvl == (ARM_LPAE_MAX_LEVELS - 1) && fmt != ARM_MALI_LPAE)
		return iopte_type(pte) == ARM_LPAE_PTE_TYPE_PAGE;

	return iopte_type(pte) == ARM_LPAE_PTE_TYPE_BLOCK;
}

static arm_lpae_iopte paddr_to_iopte(phys_addr_t paddr,
				     struct arm_lpae_io_pgtable *data)
{
	arm_lpae_iopte pte = paddr;

	/* Of the bits which overlap, either 51:48 or 15:12 are always RES0 */
	return (pte | (pte >> (48 - 12))) & ARM_LPAE_PTE_ADDR_MASK;
}

static phys_addr_t iopte_to_paddr(arm_lpae_iopte pte,
				  struct arm_lpae_io_pgtable *data)
{
	u64 paddr = iopte_val(pte) & ARM_LPAE_PTE_ADDR_MASK;

	if (ARM_LPAE_GRANULE(data) < SZ_64K)
		return paddr;

	/* Rotate the packed high-order bits back to the top */
	return (paddr | (paddr << (48 - 12))) & (ARM_LPAE_PTE_ADDR_MASK << 4);
}

static bool selftest_running;

static dma_addr_t __arm_lpae_dma_addr(void *pages)
{
	return (dma_addr_t)virt_to_phys(pages);
}

static void *__arm_lpae_alloc_pages(struct arm_lpae_io_pgtable *data,
				    size_t size, gfp_t gfp,
				    struct io_pgtable_cfg *cfg, void *cookie)
{
	struct device *dev = cfg->iommu_dev;
	dma_addr_t dma;
	struct page *p;
	void *pages;

	VM_BUG_ON((gfp & __GFP_HIGHMEM));
	p = qcom_io_pgtable_alloc_page(data->vmid, gfp | __GFP_ZERO);
	if (!p)
		return NULL;

	pages = page_address(p);
	if (!cfg->coherent_walk) {
		dma = dma_map_single(dev, pages, size, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, dma))
			goto out_free;
		/*
		 * We depend on the IOMMU being able to work with any physical
		 * address directly, so if the DMA layer suggests otherwise by
		 * translating or truncating them, that bodes very badly...
		 */
		if (dma != virt_to_phys(pages))
			goto out_unmap;
	}

	return pages;

out_unmap:
	dev_err(dev, "Cannot accommodate DMA translation for IOMMU page tables\n");
	dma_unmap_single(dev, dma, size, DMA_TO_DEVICE);
out_free:
	qcom_io_pgtable_free_page(p);
	return NULL;
}

static void __arm_lpae_free_pages(struct arm_lpae_io_pgtable *data,
				  void *pages, size_t size,
				  struct io_pgtable_cfg *cfg, void *cookie,
				  bool deferred_free)
{
	if (!cfg->coherent_walk)
		dma_unmap_single(cfg->iommu_dev, __arm_lpae_dma_addr(pages),
				 size, DMA_TO_DEVICE);

	if (deferred_free)
		qcom_io_pgtable_tlb_add_walk_page(data->iommu_tlb_ops, cookie, pages);
	else
		qcom_io_pgtable_free_page(virt_to_page(pages));
}

static void __arm_lpae_sync_pte(arm_lpae_iopte *ptep, int num_entries,
				struct io_pgtable_cfg *cfg)
{
	dma_sync_single_for_device(cfg->iommu_dev, __arm_lpae_dma_addr(ptep),
				   sizeof(*ptep) * num_entries, DMA_TO_DEVICE);
}

static void __arm_lpae_set_pte(arm_lpae_iopte *ptep, arm_lpae_iopte pte,
			       int num_entries, struct io_pgtable_cfg *cfg)
{
	int i;

	for (i = 0; i < num_entries; i++)
		ptep[i] = pte;

	if (!cfg->coherent_walk)
		__arm_lpae_sync_pte(ptep, num_entries, cfg);
}

static size_t __arm_lpae_unmap(struct arm_lpae_io_pgtable *data,
			       struct iommu_iotlb_gather *gather,
			       unsigned long iova, size_t size, size_t pgcount,
			       int lvl, arm_lpae_iopte *ptep, unsigned long *flags);

static void __arm_lpae_init_pte(struct arm_lpae_io_pgtable *data,
				phys_addr_t paddr, arm_lpae_iopte prot,
				int lvl, int num_entries, arm_lpae_iopte *ptep,
				bool flush)
{
	arm_lpae_iopte pte = prot;
	size_t sz = ARM_LPAE_BLOCK_SIZE(lvl, data);
	struct io_pgtable_cfg *cfg = &data->iop.cfg;
	int i;

	if (data->iop.fmt != ARM_MALI_LPAE && lvl == ARM_LPAE_MAX_LEVELS - 1)
		pte |= ARM_LPAE_PTE_TYPE_PAGE;
	else
		pte |= ARM_LPAE_PTE_TYPE_BLOCK;

	for (i = 0; i < num_entries; i++)
		ptep[i] = pte | paddr_to_iopte(paddr + i * sz, data);

	if (flush && !cfg->coherent_walk)
		__arm_lpae_sync_pte(ptep, num_entries, cfg);
}

static int arm_lpae_init_pte(struct arm_lpae_io_pgtable *data,
			     unsigned long iova, phys_addr_t paddr,
			     arm_lpae_iopte prot, int lvl, int num_entries,
			     arm_lpae_iopte *ptep, arm_lpae_iopte *prev_ptep,
			     bool flush)
{
	int i;

	for (i = 0; i < num_entries; i++)
		if (ptep[i] & ARM_LPAE_PTE_VALID) {
			/* We require an unmap first */
			WARN_ON(!selftest_running);
			return -EEXIST;
		}

	__arm_lpae_init_pte(data, paddr, prot, lvl, num_entries, ptep, flush);

	if (prev_ptep)
		iopte_tblcnt_add(prev_ptep, num_entries);

	return 0;
}

static arm_lpae_iopte arm_lpae_install_table(arm_lpae_iopte *table,
					     arm_lpae_iopte *ptep,
					     arm_lpae_iopte curr,
					     struct io_pgtable_cfg *cfg,
					     int refcount,
					     struct arm_lpae_io_pgtable *data,
					     unsigned long *flags)
{
	arm_lpae_iopte old, new;

	/*
	 * Drop the lock for TLB SYNC operation in order to
	 * enable clock & regulator through rpm hooks and
	 * acquire after it.
	 */

	spin_unlock_irqrestore(&data->lock, *flags);
	/* Due to tlb maintenance in unmap being deferred */
	qcom_io_pgtable_tlb_sync(data->iommu_tlb_ops, data->iop.cookie);
	spin_lock_irqsave(&data->lock, *flags);

	new = __pa(table) | ARM_LPAE_PTE_TYPE_TABLE;
	if (cfg->quirks & IO_PGTABLE_QUIRK_ARM_NS)
		new |= ARM_LPAE_PTE_NSTABLE;
	iopte_tblcnt_set(&new, refcount);

	/*
	 * Ensure the table itself is visible before its PTE can be.
	 * Whilst we could get away with cmpxchg64_release below, this
	 * doesn't have any ordering semantics when !CONFIG_SMP.
	 */
	dma_wmb();

	old = cmpxchg64_relaxed(ptep, curr, new);

	if (cfg->coherent_walk || (old & ARM_LPAE_PTE_SW_SYNC))
		return old;

	/* Even if it's not ours, there's no point waiting; just kick it */
	__arm_lpae_sync_pte(ptep, 1, cfg);
	if (old == curr)
		WRITE_ONCE(*ptep, new | ARM_LPAE_PTE_SW_SYNC);

	return old;
}

struct map_state {
	unsigned long iova_end;
	unsigned int pgsize;
	arm_lpae_iopte *pgtable;
	arm_lpae_iopte *prev_pgtable;
	arm_lpae_iopte *pte_start;
	unsigned int num_pte;
};
/* map state optimization works at level 3 */
#define MAP_STATE_LVL 3

static int __arm_lpae_map(struct arm_lpae_io_pgtable *data, unsigned long iova,
			  phys_addr_t paddr, size_t size, size_t pgcount,
			  arm_lpae_iopte prot, int lvl, arm_lpae_iopte *ptep,
			  arm_lpae_iopte *prev_ptep, struct map_state *ms,
			  gfp_t gfp, unsigned long *flags, size_t *mapped)
{
	arm_lpae_iopte *cptep, pte;
	size_t block_size = ARM_LPAE_BLOCK_SIZE(lvl, data);
	size_t prev_block_size;
	size_t tblsz = ARM_LPAE_GRANULE(data);
	struct io_pgtable_cfg *cfg = &data->iop.cfg;
	void *cookie = data->iop.cookie;
	arm_lpae_iopte *pgtable = ptep;
	int ret = 0, num_entries, max_entries, map_idx_start;

	/* Find our entry at the current level */
	map_idx_start = ARM_LPAE_LVL_IDX(iova, lvl, data);
	ptep += map_idx_start;

	/* If we can install a leaf entry at this level, then do so */
	if (size == block_size) {
		if (!ms) {
			max_entries = ARM_LPAE_PTES_PER_TABLE(data) - map_idx_start;
			num_entries = min_t(int, pgcount, max_entries);
			ret = arm_lpae_init_pte(data, iova, paddr, prot, lvl,
						num_entries, ptep, prev_ptep, true);
			if (!ret && mapped)
				*mapped += size * num_entries;

			return ret;
		}

		if (lvl == MAP_STATE_LVL) {
			prev_block_size = ARM_LPAE_BLOCK_SIZE(lvl - 1, data);

			if (ms->pgtable && !cfg->coherent_walk)
				dma_sync_single_for_device(cfg->iommu_dev,
					__arm_lpae_dma_addr(ms->pte_start),
					ms->num_pte * sizeof(*ptep),
					DMA_TO_DEVICE);

			ms->iova_end = round_down(iova, prev_block_size) + prev_block_size;
			ms->pgtable = pgtable;
			ms->prev_pgtable = prev_ptep;
			ms->pgsize = size;
			ms->pte_start = ptep;
			ms->num_pte = 1;
		} else {
			/*
			 * We have some map state from previous page mappings,
			 * but we're about to set up a block mapping. Flush
			 * out the previous page mappings.
			 */
			if (ms->pgtable && !cfg->coherent_walk)
				dma_sync_single_for_device(cfg->iommu_dev,
					__arm_lpae_dma_addr(ms->pte_start),
					ms->num_pte * sizeof(*ptep),
					DMA_TO_DEVICE);
			memset(ms, 0, sizeof(*ms));
			ms = NULL;
		}

		ret = arm_lpae_init_pte(data, iova, paddr, prot, lvl, 1, ptep, prev_ptep,
					ms == NULL);
		if (!ret && mapped)
			*mapped += size;

		return ret;
	}

	/* We can't allocate tables at the final level */
	if (WARN_ON(lvl >= ARM_LPAE_MAX_LEVELS - 1))
		return -EINVAL;

	/* Grab a pointer to the next level */
	pte = READ_ONCE(*ptep);
	if (!pte) {
		/*
		 * Drop the lock in order to support GFP_KERNEL.
		 *
		 * Only last level pagetables will be freed if iotlb_tblcnt reaches 0.
		 * Since we are installing a new pagetable, the current pagetable cannot
		 * be the last level. So we can assume ptep is still a valid memory location
		 * after reaquiring the lock.
		 */
		spin_unlock_irqrestore(&data->lock, *flags);
		cptep = __arm_lpae_alloc_pages(data, tblsz, gfp, cfg, cookie);
		spin_lock_irqsave(&data->lock, *flags);
		if (!cptep)
			return -ENOMEM;

		pte = arm_lpae_install_table(cptep, ptep, 0, cfg, 0, data, flags);
		if (pte)
			__arm_lpae_free_pages(data, cptep, tblsz, cfg, cookie, false);
		else
			qcom_io_pgtable_log_new_table(data->pgtable_log_ops,
					data->iop.cookie, cptep,
					iova & ~(block_size - 1),
					block_size);
	} else if (!cfg->coherent_walk && !(pte & ARM_LPAE_PTE_SW_SYNC)) {
		__arm_lpae_sync_pte(ptep, 1, cfg);
	}

	if (pte && !iopte_leaf(pte, lvl, data->iop.fmt)) {
		cptep = iopte_deref(pte, data);
	} else if (pte) {
		/* We require an unmap first */
		WARN_ON(!selftest_running);
		return -EEXIST;
	}

	/* Rinse, repeat */
	return __arm_lpae_map(data, iova, paddr, size, pgcount, prot, lvl + 1,
			      cptep, ptep, ms, gfp, flags, mapped);
}

static arm_lpae_iopte arm_lpae_prot_to_pte(struct arm_lpae_io_pgtable *data,
					   int prot)
{
	arm_lpae_iopte pte;

	if (data->iop.fmt == QCOM_ARM_64_LPAE_S1 ||
	    data->iop.fmt == ARM_32_LPAE_S1) {
		pte = ARM_LPAE_PTE_nG;
		if (!(prot & IOMMU_WRITE) && (prot & IOMMU_READ))
			pte |= ARM_LPAE_PTE_AP_RDONLY;
		if (!(prot & IOMMU_PRIV))
			pte |= ARM_LPAE_PTE_AP_UNPRIV;
	} else {
		pte = ARM_LPAE_PTE_HAP_FAULT;
		if (prot & IOMMU_READ)
			pte |= ARM_LPAE_PTE_HAP_READ;
		if (prot & IOMMU_WRITE)
			pte |= ARM_LPAE_PTE_HAP_WRITE;
	}

	/*
	 * Note that this logic is structured to accommodate Mali LPAE
	 * having stage-1-like attributes but stage-2-like permissions.
	 */
	if (data->iop.fmt == ARM_64_LPAE_S2 ||
	    data->iop.fmt == ARM_32_LPAE_S2) {
		if (prot & IOMMU_MMIO)
			pte |= ARM_LPAE_PTE_MEMATTR_DEV;
		else if (prot & IOMMU_CACHE)
			pte |= ARM_LPAE_PTE_MEMATTR_OIWB;
		else
			pte |= ARM_LPAE_PTE_MEMATTR_NC;
	} else {
		if (prot & IOMMU_MMIO)
			pte |= (ARM_LPAE_MAIR_ATTR_IDX_DEV
				<< ARM_LPAE_PTE_ATTRINDX_SHIFT);
		else if ((prot & IOMMU_CACHE) && (prot & IOMMU_SYS_CACHE_NWA))
			pte |= (ARM_LPAE_MAIR_ATTR_IDX_ICACHE_OCACHE_NWA
				<< ARM_LPAE_PTE_ATTRINDX_SHIFT);
		/* IOMMU_CACHE + IOMMU_SYS_CACHE equivalent to IOMMU_CACHE */
		else if (prot & IOMMU_CACHE)
			pte |= (ARM_LPAE_MAIR_ATTR_IDX_CACHE
				<< ARM_LPAE_PTE_ATTRINDX_SHIFT);
		else if (prot & IOMMU_SYS_CACHE)
			pte |= (ARM_LPAE_MAIR_ATTR_IDX_INC_OCACHE
				<< ARM_LPAE_PTE_ATTRINDX_SHIFT);
		else if (prot & IOMMU_SYS_CACHE_NWA)
			pte |= (ARM_LPAE_MAIR_ATTR_IDX_INC_OCACHE_NWA
				<< ARM_LPAE_PTE_ATTRINDX_SHIFT);
	}

	if (prot & IOMMU_CACHE)
		pte |= ARM_LPAE_PTE_SH_IS;
	else
		pte |= ARM_LPAE_PTE_SH_OS;

	if (prot & IOMMU_NOEXEC)
		pte |= ARM_LPAE_PTE_XN;

	if (data->iop.cfg.quirks & IO_PGTABLE_QUIRK_ARM_NS)
		pte |= ARM_LPAE_PTE_NS;

	if (data->iop.fmt != ARM_MALI_LPAE)
		pte |= ARM_LPAE_PTE_AF;

	return pte;
}

int qcom_arm_lpae_map_pages(struct io_pgtable_ops *ops, unsigned long iova,
			      phys_addr_t paddr, size_t pgsize, size_t pgcount,
			      int iommu_prot, gfp_t gfp, size_t *mapped)
{
	struct arm_lpae_io_pgtable *data = io_pgtable_ops_to_data(ops);
	struct io_pgtable_cfg *cfg = &data->iop.cfg;
	arm_lpae_iopte *ptep = data->pgd;
	int ret, lvl = data->start_level;
	arm_lpae_iopte prot;
	long iaext = (s64)iova >> cfg->ias;
	unsigned long flags;

	if (WARN_ON(!pgsize || (pgsize & cfg->pgsize_bitmap) != pgsize || !pgcount))
		return -EINVAL;

	if (cfg->quirks & IO_PGTABLE_QUIRK_ARM_TTBR1)
		iaext = ~iaext;
	if (WARN_ON(iaext || paddr >> cfg->oas))
		return -ERANGE;

	/* If no access, then nothing to do */
	if (!(iommu_prot & (IOMMU_READ | IOMMU_WRITE)))
		return 0;

	prot = arm_lpae_prot_to_pte(data, iommu_prot);

	spin_lock_irqsave(&data->lock, flags);
	ret = __arm_lpae_map(data, iova, paddr, pgsize, pgcount, prot, lvl,
			     ptep, NULL, NULL, gfp, &flags, mapped);
	spin_unlock_irqrestore(&data->lock, flags);
	/*
	 * Synchronise all PTE updates for the new mapping before there's
	 * a chance for anything to kick off a table walk for the new iova.
	 */
	wmb();

	return ret;
}
EXPORT_SYMBOL(qcom_arm_lpae_map_pages);

int qcom_arm_lpae_map(struct io_pgtable_ops *ops, unsigned long iova,
			phys_addr_t paddr, size_t size, int iommu_prot, gfp_t gfp)
{
	return qcom_arm_lpae_map_pages(ops, iova, paddr, size, 1, iommu_prot, gfp,
				  NULL);
}
EXPORT_SYMBOL(qcom_arm_lpae_map);

static size_t arm_lpae_pgsize(unsigned long pgsize_bitmap, unsigned long addr_merge,
			      size_t size)
{
	unsigned int pgsize_idx, align_pgsize_idx;
	size_t pgsize;

	/* Max page size that still fits into 'size' */
	pgsize_idx = __fls(size);

	/* need to consider alignment requirements ? */
	if (likely(addr_merge)) {
		/* Max page size allowed by address */
		align_pgsize_idx = __ffs(addr_merge);
		pgsize_idx = min(pgsize_idx, align_pgsize_idx);
	}

	/* build a mask of acceptable page sizes */
	pgsize = (1UL << (pgsize_idx + 1)) - 1;

	/* throw away page sizes not supported by the hardware */
	pgsize &= pgsize_bitmap;

	/* make sure we're still sane */
	BUG_ON(!pgsize);

	/* pick the biggest page */
	pgsize_idx = __fls(pgsize);
	pgsize = 1UL << pgsize_idx;

	return pgsize;
}

static int arm_lpae_map_by_pgsize(struct io_pgtable_ops *ops,
				  unsigned long iova, phys_addr_t paddr,
				  size_t size, int iommu_prot, gfp_t gfp,
				  size_t *mapped, struct map_state *ms,
				  unsigned long *flags)
{
	struct arm_lpae_io_pgtable *data = io_pgtable_ops_to_data(ops);
	struct io_pgtable_cfg *cfg = &data->iop.cfg;
	arm_lpae_iopte *ptep = data->pgd, *ms_ptep;
	int ret, lvl = data->start_level;
	arm_lpae_iopte prot = arm_lpae_prot_to_pte(data, iommu_prot);
	unsigned int min_pagesz = 1 << __ffs(cfg->pgsize_bitmap);
	long iaext = (s64)(iova + size - 1) >> cfg->ias;
	size_t pgsize;

	if (!IS_ALIGNED(iova | paddr | size, min_pagesz)) {
		pr_err("unaligned: iova 0x%lx pa %pa size 0x%zx min_pagesz 0x%x\n",
		       iova, &paddr, size, min_pagesz);
		return -EINVAL;
	}

	if (cfg->quirks & IO_PGTABLE_QUIRK_ARM_TTBR1)
		iaext = ~iaext;
	if (WARN_ON(iaext || (paddr + size - 1) >> cfg->oas))
		return -ERANGE;

	/* If no access, then nothing to do */
	if (!(iommu_prot & (IOMMU_READ | IOMMU_WRITE))) {
		/* Increment 'mapped' so that the IOVA can be incremented accordingly. */
		*mapped += size;
		return 0;
	}

	while (size) {
		pgsize = arm_lpae_pgsize(cfg->pgsize_bitmap, iova | paddr, size);

		if (ms->pgtable && (iova < ms->iova_end)) {
			ms_ptep = ms->pgtable + ARM_LPAE_LVL_IDX(iova, MAP_STATE_LVL, data);
			ret = arm_lpae_init_pte(data, iova, paddr, prot, MAP_STATE_LVL,
					  1, ms_ptep, ms->prev_pgtable, false);
			if (ret)
				return ret;
			ms->num_pte++;
		} else {
			ret = __arm_lpae_map(data, iova, paddr, pgsize, 1,
					     prot, lvl, ptep, NULL, ms, gfp,
					     flags, NULL);
			if (ret)
				return ret;
		}

		iova += pgsize;
		paddr += pgsize;
		*mapped += pgsize;
		size -= pgsize;
	}

	return 0;
}

int qcom_arm_lpae_map_sg(struct io_pgtable_ops *ops, unsigned long iova,
			   struct scatterlist *sg, unsigned int nents, int prot,
			   gfp_t gfp, size_t *mapped)
{
	size_t len = 0;
	unsigned int i = 0;
	int ret;
	phys_addr_t start;
	struct map_state ms = {};
	struct arm_lpae_io_pgtable *data = io_pgtable_ops_to_data(ops);
	struct io_pgtable_cfg *cfg = &data->iop.cfg;
	unsigned long flags;

	*mapped = 0;

	spin_lock_irqsave(&data->lock, flags);
	while (i <= nents) {
		phys_addr_t s_phys = sg_phys(sg);

		if (len && s_phys != start + len) {
			ret = arm_lpae_map_by_pgsize(ops, iova + *mapped, start,
						     len, prot, gfp, mapped, &ms, &flags);

			if (ret) {
				spin_unlock_irqrestore(&data->lock, flags);
				return ret;
			}

			len = 0;
		}

		if (len) {
			len += sg->length;
		} else {
			len = sg->length;
			start = s_phys;
		}

		if (++i < nents)
			sg = sg_next(sg);
	}
	spin_unlock_irqrestore(&data->lock, flags);

	if (ms.pgtable && !cfg->coherent_walk)
		dma_sync_single_for_device(cfg->iommu_dev,
					   __arm_lpae_dma_addr(ms.pte_start),
					   ms.num_pte * sizeof(*ms.pte_start),
					   DMA_TO_DEVICE);

	/*
	 * Synchronise all PTE updates for the new mapping before there's
	 * a chance for anything to kick off a table walk for the new iova.
	 */
	wmb();

	return 0;
}
EXPORT_SYMBOL(qcom_arm_lpae_map_sg);

static void __arm_lpae_free_pgtable(struct arm_lpae_io_pgtable *data, int lvl,
				    arm_lpae_iopte *ptep, bool deferred_free)
{
	arm_lpae_iopte *start, *end;
	unsigned long table_size;
	void *cookie = data->iop.cookie;

	if (lvl == data->start_level)
		table_size = ARM_LPAE_PGD_SIZE(data);
	else
		table_size = ARM_LPAE_GRANULE(data);

	start = ptep;

	/* Only leaf entries at the last level */
	if (lvl == ARM_LPAE_MAX_LEVELS - 1)
		end = ptep;
	else
		end = (void *)ptep + table_size;

	while (ptep != end) {
		arm_lpae_iopte pte = *ptep++;

		if (!pte || iopte_leaf(pte, lvl, data->iop.fmt))
			continue;

		__arm_lpae_free_pgtable(data, lvl + 1, iopte_deref(pte, data),
					deferred_free);
	}

	__arm_lpae_free_pages(data, start, table_size, &data->iop.cfg, cookie,
			      deferred_free);

	qcom_io_pgtable_log_remove_table(data->pgtable_log_ops,
					data->iop.cookie, start,
					0, /* iova unknown */
					ARM_LPAE_BLOCK_SIZE(lvl - 1, data));
}

static void arm_lpae_free_pgtable(struct io_pgtable *iop)
{
	struct arm_lpae_io_pgtable *data = io_pgtable_to_data(iop);

	__arm_lpae_free_pgtable(data, data->start_level, data->pgd, false);
	qcom_io_pgtable_allocator_unregister(data->vmid);
	kfree(data);
}

static size_t arm_lpae_split_blk_unmap(struct arm_lpae_io_pgtable *data,
				       struct iommu_iotlb_gather *gather,
				       unsigned long iova, size_t size,
				       arm_lpae_iopte blk_pte, int lvl,
				       arm_lpae_iopte *ptep, size_t pgcount,
				       unsigned long *flags)
{
	struct io_pgtable_cfg *cfg = &data->iop.cfg;
	arm_lpae_iopte pte, *tablep;
	phys_addr_t blk_paddr;
	size_t tablesz = ARM_LPAE_GRANULE(data);
	size_t split_sz = ARM_LPAE_BLOCK_SIZE(lvl, data);
	int ptes_per_table = ARM_LPAE_PTES_PER_TABLE(data);
	int i, unmap_idx_start = -1, num_entries = 0, max_entries;
	void *cookie = data->iop.cookie;
	int child_cnt = 0;

	if (WARN_ON(lvl == ARM_LPAE_MAX_LEVELS))
		return 0;

	tablep = __arm_lpae_alloc_pages(data, tablesz, GFP_ATOMIC, cfg, cookie);
	if (!tablep)
		return 0; /* Bytes unmapped */

	if (size == split_sz) {
		unmap_idx_start = ARM_LPAE_LVL_IDX(iova, lvl, data);
		max_entries = ptes_per_table - unmap_idx_start;
		num_entries = min_t(int, pgcount, max_entries);
	}

	blk_paddr = iopte_to_paddr(blk_pte, data);
	pte = iopte_prot(blk_pte);

	for (i = 0; i < ptes_per_table; i++, blk_paddr += split_sz) {
		/* Unmap! */
		if (i >= unmap_idx_start && i < (unmap_idx_start + num_entries))
			continue;

		__arm_lpae_init_pte(data, blk_paddr, pte, lvl, 1, &tablep[i], true);
		child_cnt++;
	}

	pte = arm_lpae_install_table(tablep, ptep, blk_pte, cfg, child_cnt, data, flags);
	if (pte != blk_pte) {
		__arm_lpae_free_pages(data, tablep, tablesz, cfg, cookie, false);
		/*
		 * We may race against someone unmapping another part of this
		 * block, but anything else is invalid. We can't misinterpret
		 * a page entry here since we're never at the last level.
		 */
		if (iopte_type(pte) != ARM_LPAE_PTE_TYPE_TABLE)
			return 0;

		tablep = iopte_deref(pte, data);
	} else if (unmap_idx_start >= 0) {
		/*
		 * note lvl + 1 due to split_sz above.
		 * add 0xDEA as flag since split_block_unmap shouldn't ever be called
		 */
		size_t prev_block_size = ARM_LPAE_BLOCK_SIZE(lvl + 1, data);

		qcom_io_pgtable_log_new_table(data->pgtable_log_ops,
					data->iop.cookie, tablep,
					iova & ~(prev_block_size - 1) + 0xDEA,
					prev_block_size);
		return num_entries * size;
	}

	return __arm_lpae_unmap(data, gather, iova, size, pgcount, lvl, tablep, flags);
}

static size_t __arm_lpae_unmap(struct arm_lpae_io_pgtable *data,
			       struct iommu_iotlb_gather *gather,
			       unsigned long iova, size_t size, size_t pgcount,
			       int lvl, arm_lpae_iopte *ptep, unsigned long *flags)
{
	arm_lpae_iopte pte;
	struct io_pgtable *iop = &data->iop;
	int ptes_per_table = ARM_LPAE_PTES_PER_TABLE(data);
	int i = 0, num_entries, max_entries, unmap_idx_start;

	/* Something went horribly wrong and we ran out of page table */
	if (WARN_ON(lvl == ARM_LPAE_MAX_LEVELS))
		return 0;

	unmap_idx_start = ARM_LPAE_LVL_IDX(iova, lvl, data);
	ptep += unmap_idx_start;
	pte = READ_ONCE(*ptep);
	if (WARN_ON(!pte))
		return 0;

	/* If the size matches this level, we're in the right place */
	if (size == ARM_LPAE_BLOCK_SIZE(lvl, data)) {
		max_entries = ptes_per_table - unmap_idx_start;
		num_entries = min_t(int, pgcount, max_entries);

		while (i < num_entries) {
			pte = READ_ONCE(*ptep);
			if (WARN_ON(!pte))
				break;

			__arm_lpae_set_pte(ptep, 0, 1, &iop->cfg);

			if (!iopte_leaf(pte, lvl, iop->fmt)) {
				__arm_lpae_free_pgtable(data, lvl + 1, iopte_deref(pte, data),
							true);
			} else if (!iommu_iotlb_gather_queued(gather)) {
				/*
				 * Order the PTE update against queueing the IOVA, to
				 * guarantee that a flush callback from a different CPU
				 * has observed it before the TLBIALL can be issued.
				 */
				smp_wmb();
			}

			ptep++;
			i++;
		}

		return i * size;
	} else if ((lvl == ARM_LPAE_MAX_LEVELS - 2) && !iopte_leaf(pte, lvl,
								   iop->fmt)) {
		arm_lpae_iopte *table;
		arm_lpae_iopte *entry;

		table = iopte_deref(pte, data);
		unmap_idx_start = ARM_LPAE_LVL_IDX(iova, lvl + 1, data);
		entry = table + unmap_idx_start;

		max_entries = ptes_per_table - unmap_idx_start;
		num_entries = min_t(int, pgcount, max_entries);
		__arm_lpae_set_pte(entry, 0, num_entries, &iop->cfg);

		iopte_tblcnt_sub(ptep, num_entries);
		if (!iopte_tblcnt(*ptep)) {
			size_t block_size = ARM_LPAE_BLOCK_SIZE(lvl, data);
			/*
			 * no valid mappings left under this table.
			 * Defer table free until after iommu_iotlb_sync, or
			 * qcom_io_pgtable_tlb_sync, whichever occurs first.
			 */
			__arm_lpae_set_pte(ptep, 0, 1, &iop->cfg);
			__arm_lpae_free_pgtable(data, lvl + 1, table, true);

			qcom_io_pgtable_log_remove_table(data->pgtable_log_ops,
				data->iop.cookie, table,
				iova & ~(block_size - 1),
				block_size);
		}

		return num_entries * size;
	} else if (iopte_leaf(pte, lvl, iop->fmt)) {
		/*
		 * Insert a table at the next level to map the old region,
		 * minus the part we want to unmap
		 */
		return arm_lpae_split_blk_unmap(data, gather, iova, size, pte,
						lvl + 1, ptep, pgcount, flags);
	}

	/* Keep on walkin' */
	ptep = iopte_deref(pte, data);
	return __arm_lpae_unmap(data, gather, iova, size, pgcount, lvl + 1, ptep, flags);
}

size_t qcom_arm_lpae_unmap_pages(struct io_pgtable_ops *ops, unsigned long iova,
				   size_t pgsize, size_t pgcount,
				   struct iommu_iotlb_gather *gather)
{
	struct arm_lpae_io_pgtable *data = io_pgtable_ops_to_data(ops);
	struct io_pgtable_cfg *cfg = &data->iop.cfg;
	arm_lpae_iopte *ptep = data->pgd;
	long iaext = (s64)iova >> cfg->ias;
	size_t unmapped;
	unsigned long flags;

	if (WARN_ON(!pgsize || (pgsize & cfg->pgsize_bitmap) != pgsize || !pgcount))
		return 0;

	if (cfg->quirks & IO_PGTABLE_QUIRK_ARM_TTBR1)
		iaext = ~iaext;
	if (WARN_ON(iaext))
		return 0;

	spin_lock_irqsave(&data->lock, flags);
	unmapped = __arm_lpae_unmap(data, gather, iova, pgsize, pgcount,
				    data->start_level, ptep, &flags);
	qcom_io_pgtable_tlb_add_inv(data->iommu_tlb_ops, data->iop.cookie);
	spin_unlock_irqrestore(&data->lock, flags);

	return unmapped;
}
EXPORT_SYMBOL(qcom_arm_lpae_unmap_pages);

size_t qcom_arm_lpae_unmap(struct io_pgtable_ops *ops, unsigned long iova,
			     size_t size, struct iommu_iotlb_gather *gather)
{
	return qcom_arm_lpae_unmap_pages(ops, iova, size, 1, gather);
}
EXPORT_SYMBOL(qcom_arm_lpae_unmap);

static phys_addr_t arm_lpae_iova_to_phys(struct io_pgtable_ops *ops,
					 unsigned long iova)
{
	struct arm_lpae_io_pgtable *data = io_pgtable_ops_to_data(ops);
	arm_lpae_iopte pte, *ptep = data->pgd;
	int lvl = data->start_level;
	unsigned long flags;

	spin_lock_irqsave(&data->lock, flags);
	do {
		/* Valid IOPTE pointer? */
		if (!ptep)
			goto err;

		/* Grab the IOPTE we're interested in */
		ptep += ARM_LPAE_LVL_IDX(iova, lvl, data);
		pte = READ_ONCE(*ptep);

		/* Valid entry? */
		if (!pte)
			goto err;

		/* Leaf entry? */
		if (iopte_leaf(pte, lvl, data->iop.fmt))
			goto found_translation;

		/* Take it to the next level */
		ptep = iopte_deref(pte, data);
	} while (++lvl < ARM_LPAE_MAX_LEVELS);

	/* Ran out of page tables to walk */
err:
	spin_unlock_irqrestore(&data->lock, flags);
	return 0;

found_translation:
	spin_unlock_irqrestore(&data->lock, flags);
	iova &= (ARM_LPAE_BLOCK_SIZE(lvl, data) - 1);
	return iopte_to_paddr(pte, data) | iova;
}

static void arm_lpae_restrict_pgsizes(struct io_pgtable_cfg *cfg)
{
	unsigned long granule, page_sizes;
	unsigned int max_addr_bits = 48;

	/*
	 * We need to restrict the supported page sizes to match the
	 * translation regime for a particular granule. Aim to match
	 * the CPU page size if possible, otherwise prefer smaller sizes.
	 * While we're at it, restrict the block sizes to match the
	 * chosen granule.
	 */
	if (cfg->pgsize_bitmap & PAGE_SIZE)
		granule = PAGE_SIZE;
	else if (cfg->pgsize_bitmap & ~PAGE_MASK)
		granule = 1UL << __fls(cfg->pgsize_bitmap & ~PAGE_MASK);
	else if (cfg->pgsize_bitmap & PAGE_MASK)
		granule = 1UL << __ffs(cfg->pgsize_bitmap & PAGE_MASK);
	else
		granule = 0;

	switch (granule) {
	case SZ_4K:
		page_sizes = (SZ_4K | SZ_2M | SZ_1G);
		break;
	case SZ_16K:
		page_sizes = (SZ_16K | SZ_32M);
		break;
	case SZ_64K:
		max_addr_bits = 52;
		page_sizes = (SZ_64K | SZ_512M);
		if (cfg->oas > 48)
			page_sizes |= 1ULL << 42; /* 4TB */
		break;
	default:
		page_sizes = 0;
	}

	cfg->pgsize_bitmap &= page_sizes;
	cfg->ias = min(cfg->ias, max_addr_bits);
	cfg->oas = min(cfg->oas, max_addr_bits);
}

static struct arm_lpae_io_pgtable *
arm_lpae_alloc_pgtable(struct io_pgtable_cfg *cfg)
{
	struct arm_lpae_io_pgtable *data;
	struct qcom_io_pgtable_info *pgtbl_info = to_qcom_io_pgtable_info(cfg);
	int levels, va_bits, pg_shift;

	arm_lpae_restrict_pgsizes(cfg);

	if (!(cfg->pgsize_bitmap & (SZ_4K | SZ_16K | SZ_64K)))
		return NULL;

	if (cfg->ias > ARM_LPAE_MAX_ADDR_BITS)
		return NULL;

	if (cfg->oas > ARM_LPAE_MAX_ADDR_BITS)
		return NULL;

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;

	pg_shift = __ffs(cfg->pgsize_bitmap);
	data->bits_per_level = pg_shift - ilog2(sizeof(arm_lpae_iopte));

	va_bits = cfg->ias - pg_shift;
	levels = DIV_ROUND_UP(va_bits, data->bits_per_level);
	data->start_level = ARM_LPAE_MAX_LEVELS - levels;

	/* Calculate the actual size of our pgd (without concatenation) */
	data->pgd_bits = va_bits - (data->bits_per_level * (levels - 1));

	data->iop.ops = (struct io_pgtable_ops) {
		.map		= qcom_arm_lpae_map,
		.map_pages	= qcom_arm_lpae_map_pages,
		.unmap		= qcom_arm_lpae_unmap,
		.unmap_pages	= qcom_arm_lpae_unmap_pages,
		.iova_to_phys	= arm_lpae_iova_to_phys,
	};

	spin_lock_init(&data->lock);
	data->iommu_tlb_ops = pgtbl_info->iommu_tlb_ops;
	data->pgtable_log_ops = pgtbl_info->pgtable_log_ops;
	data->vmid = pgtbl_info->vmid;
	if (qcom_io_pgtable_allocator_register(data->vmid)) {
		kfree(data);
		return NULL;
	}

	return data;
}

static struct io_pgtable *
arm_64_lpae_alloc_pgtable_s1(struct io_pgtable_cfg *cfg, void *cookie)
{
	u64 reg;
	struct arm_lpae_io_pgtable *data;
	typeof(&cfg->arm_lpae_s1_cfg.tcr) tcr = &cfg->arm_lpae_s1_cfg.tcr;
	bool tg1;

	if (cfg->quirks & ~(IO_PGTABLE_QUIRK_ARM_NS |
			    IO_PGTABLE_QUIRK_ARM_TTBR1 |
			    IO_PGTABLE_QUIRK_ARM_OUTER_WBWA |
			    IO_PGTABLE_QUIRK_QCOM_USE_LLC_NWA))
		return NULL;

	data = arm_lpae_alloc_pgtable(cfg);
	if (!data)
		return NULL;

	/* TCR */
	if (cfg->coherent_walk) {
		tcr->sh = ARM_LPAE_TCR_SH_IS;
		tcr->irgn = ARM_LPAE_TCR_RGN_WBWA;
		tcr->orgn = ARM_LPAE_TCR_RGN_WBWA;
		if (WARN_ON(cfg->quirks & (IO_PGTABLE_QUIRK_ARM_OUTER_WBWA |
					   IO_PGTABLE_QUIRK_QCOM_USE_LLC_NWA)))
			goto out_free_data;
	} else {
		tcr->sh = ARM_LPAE_TCR_SH_OS;
		tcr->irgn = ARM_LPAE_TCR_RGN_NC;
		if (cfg->quirks & IO_PGTABLE_QUIRK_ARM_OUTER_WBWA)
			tcr->orgn = ARM_LPAE_TCR_RGN_WBWA;
		else if (cfg->quirks & IO_PGTABLE_QUIRK_QCOM_USE_LLC_NWA)
			tcr->orgn = ARM_LPAE_TCR_RGN_WB;
		else
			tcr->orgn = ARM_LPAE_TCR_RGN_NC;
	}

	tg1 = cfg->quirks & IO_PGTABLE_QUIRK_ARM_TTBR1;
	switch (ARM_LPAE_GRANULE(data)) {
	case SZ_4K:
		tcr->tg = tg1 ? ARM_LPAE_TCR_TG1_4K : ARM_LPAE_TCR_TG0_4K;
		break;
	case SZ_16K:
		tcr->tg = tg1 ? ARM_LPAE_TCR_TG1_16K : ARM_LPAE_TCR_TG0_16K;
		break;
	case SZ_64K:
		tcr->tg = tg1 ? ARM_LPAE_TCR_TG1_64K : ARM_LPAE_TCR_TG0_64K;
		break;
	}

	switch (cfg->oas) {
	case 32:
		tcr->ips = ARM_LPAE_TCR_PS_32_BIT;
		break;
	case 36:
		tcr->ips = ARM_LPAE_TCR_PS_36_BIT;
		break;
	case 40:
		tcr->ips = ARM_LPAE_TCR_PS_40_BIT;
		break;
	case 42:
		tcr->ips = ARM_LPAE_TCR_PS_42_BIT;
		break;
	case 44:
		tcr->ips = ARM_LPAE_TCR_PS_44_BIT;
		break;
	case 48:
		tcr->ips = ARM_LPAE_TCR_PS_48_BIT;
		break;
	case 52:
		tcr->ips = ARM_LPAE_TCR_PS_52_BIT;
		break;
	default:
		goto out_free_data;
	}

	tcr->tsz = 64ULL - cfg->ias;

	/* MAIRs */
	reg = (ARM_LPAE_MAIR_ATTR_NC
	       << ARM_LPAE_MAIR_ATTR_SHIFT(ARM_LPAE_MAIR_ATTR_IDX_NC)) |
	      (ARM_LPAE_MAIR_ATTR_WBRWA
	       << ARM_LPAE_MAIR_ATTR_SHIFT(ARM_LPAE_MAIR_ATTR_IDX_CACHE)) |
	      (ARM_LPAE_MAIR_ATTR_DEVICE
	       << ARM_LPAE_MAIR_ATTR_SHIFT(ARM_LPAE_MAIR_ATTR_IDX_DEV)) |
	      (ARM_LPAE_MAIR_ATTR_INC_OWBRWA
	       << ARM_LPAE_MAIR_ATTR_SHIFT(ARM_LPAE_MAIR_ATTR_IDX_INC_OCACHE)) |
	      (ARM_LPAE_MAIR_ATTR_INC_OWBRANWA
	       << ARM_LPAE_MAIR_ATTR_SHIFT(ARM_LPAE_MAIR_ATTR_IDX_INC_OCACHE_NWA)) |
	      (ARM_LPAE_MAIR_ATTR_IWBRWA_OWBRANWA
	       << ARM_LPAE_MAIR_ATTR_SHIFT(ARM_LPAE_MAIR_ATTR_IDX_ICACHE_OCACHE_NWA));

	cfg->arm_lpae_s1_cfg.mair = reg;

	/* Looking good; allocate a pgd */
	data->pgd = __arm_lpae_alloc_pages(data, ARM_LPAE_PGD_SIZE(data),
					   GFP_KERNEL, cfg, cookie);
	if (!data->pgd)
		goto out_free_data;

	/* Ensure the empty pgd is visible before any actual TTBR write */
	wmb();

	/* TTBR */
	cfg->arm_lpae_s1_cfg.ttbr = virt_to_phys(data->pgd);
	return &data->iop;

out_free_data:
	qcom_io_pgtable_allocator_unregister(data->vmid);
	kfree(data);
	return NULL;
}

static struct io_pgtable *
arm_64_lpae_alloc_pgtable_s2(struct io_pgtable_cfg *cfg, void *cookie)
{
	u64 sl;
	struct arm_lpae_io_pgtable *data;
	typeof(&cfg->arm_lpae_s2_cfg.vtcr) vtcr = &cfg->arm_lpae_s2_cfg.vtcr;

	/* The NS quirk doesn't apply at stage 2 */
	if (cfg->quirks)
		return NULL;

	data = arm_lpae_alloc_pgtable(cfg);
	if (!data)
		return NULL;

	/*
	 * Concatenate PGDs at level 1 if possible in order to reduce
	 * the depth of the stage-2 walk.
	 */
	if (data->start_level == 0) {
		unsigned long pgd_pages;

		pgd_pages = ARM_LPAE_PGD_SIZE(data) / sizeof(arm_lpae_iopte);
		if (pgd_pages <= ARM_LPAE_S2_MAX_CONCAT_PAGES) {
			data->pgd_bits += data->bits_per_level;
			data->start_level++;
		}
	}

	/* VTCR */
	if (cfg->coherent_walk) {
		vtcr->sh = ARM_LPAE_TCR_SH_IS;
		vtcr->irgn = ARM_LPAE_TCR_RGN_WBWA;
		vtcr->orgn = ARM_LPAE_TCR_RGN_WBWA;
	} else {
		vtcr->sh = ARM_LPAE_TCR_SH_OS;
		vtcr->irgn = ARM_LPAE_TCR_RGN_NC;
		vtcr->orgn = ARM_LPAE_TCR_RGN_NC;
	}

	sl = data->start_level;

	switch (ARM_LPAE_GRANULE(data)) {
	case SZ_4K:
		vtcr->tg = ARM_LPAE_TCR_TG0_4K;
		sl++; /* SL0 format is different for 4K granule size */
		break;
	case SZ_16K:
		vtcr->tg = ARM_LPAE_TCR_TG0_16K;
		break;
	case SZ_64K:
		vtcr->tg = ARM_LPAE_TCR_TG0_64K;
		break;
	}

	switch (cfg->oas) {
	case 32:
		vtcr->ps = ARM_LPAE_TCR_PS_32_BIT;
		break;
	case 36:
		vtcr->ps = ARM_LPAE_TCR_PS_36_BIT;
		break;
	case 40:
		vtcr->ps = ARM_LPAE_TCR_PS_40_BIT;
		break;
	case 42:
		vtcr->ps = ARM_LPAE_TCR_PS_42_BIT;
		break;
	case 44:
		vtcr->ps = ARM_LPAE_TCR_PS_44_BIT;
		break;
	case 48:
		vtcr->ps = ARM_LPAE_TCR_PS_48_BIT;
		break;
	case 52:
		vtcr->ps = ARM_LPAE_TCR_PS_52_BIT;
		break;
	default:
		goto out_free_data;
	}

	vtcr->tsz = 64ULL - cfg->ias;
	vtcr->sl = ~sl & ARM_LPAE_VTCR_SL0_MASK;

	/* Allocate pgd pages */
	data->pgd = __arm_lpae_alloc_pages(data, ARM_LPAE_PGD_SIZE(data),
					   GFP_KERNEL, cfg, cookie);
	if (!data->pgd)
		goto out_free_data;

	/* Ensure the empty pgd is visible before any actual TTBR write */
	wmb();

	/* VTTBR */
	cfg->arm_lpae_s2_cfg.vttbr = virt_to_phys(data->pgd);
	return &data->iop;

out_free_data:
	qcom_io_pgtable_allocator_unregister(data->vmid);
	kfree(data);
	return NULL;
}

static struct io_pgtable *
arm_32_lpae_alloc_pgtable_s1(struct io_pgtable_cfg *cfg, void *cookie)
{
	if (cfg->ias > 32 || cfg->oas > 40)
		return NULL;

	cfg->pgsize_bitmap &= (SZ_4K | SZ_2M | SZ_1G);
	return arm_64_lpae_alloc_pgtable_s1(cfg, cookie);
}

static struct io_pgtable *
arm_32_lpae_alloc_pgtable_s2(struct io_pgtable_cfg *cfg, void *cookie)
{
	if (cfg->ias > 40 || cfg->oas > 40)
		return NULL;

	cfg->pgsize_bitmap &= (SZ_4K | SZ_2M | SZ_1G);
	return arm_64_lpae_alloc_pgtable_s2(cfg, cookie);
}

static struct io_pgtable *
arm_mali_lpae_alloc_pgtable(struct io_pgtable_cfg *cfg, void *cookie)
{
	struct arm_lpae_io_pgtable *data;

	/* No quirks for Mali (hopefully) */
	if (cfg->quirks)
		return NULL;

	if (cfg->ias > 48 || cfg->oas > 40)
		return NULL;

	cfg->pgsize_bitmap &= (SZ_4K | SZ_2M | SZ_1G);

	data = arm_lpae_alloc_pgtable(cfg);
	if (!data)
		return NULL;

	/* Mali seems to need a full 4-level table regardless of IAS */
	if (data->start_level > 0) {
		data->start_level = 0;
		data->pgd_bits = 0;
	}
	/*
	 * MEMATTR: Mali has no actual notion of a non-cacheable type, so the
	 * best we can do is mimic the out-of-tree driver and hope that the
	 * "implementation-defined caching policy" is good enough. Similarly,
	 * we'll use it for the sake of a valid attribute for our 'device'
	 * index, although callers should never request that in practice.
	 */
	cfg->arm_mali_lpae_cfg.memattr =
		(ARM_MALI_LPAE_MEMATTR_IMP_DEF
		 << ARM_LPAE_MAIR_ATTR_SHIFT(ARM_LPAE_MAIR_ATTR_IDX_NC)) |
		(ARM_MALI_LPAE_MEMATTR_WRITE_ALLOC
		 << ARM_LPAE_MAIR_ATTR_SHIFT(ARM_LPAE_MAIR_ATTR_IDX_CACHE)) |
		(ARM_MALI_LPAE_MEMATTR_IMP_DEF
		 << ARM_LPAE_MAIR_ATTR_SHIFT(ARM_LPAE_MAIR_ATTR_IDX_DEV));

	data->pgd = __arm_lpae_alloc_pages(data, ARM_LPAE_PGD_SIZE(data), GFP_KERNEL,
					   cfg, cookie);
	if (!data->pgd)
		goto out_free_data;

	/* Ensure the empty pgd is visible before TRANSTAB can be written */
	wmb();

	cfg->arm_mali_lpae_cfg.transtab = virt_to_phys(data->pgd) |
					  ARM_MALI_LPAE_TTBR_READ_INNER |
					  ARM_MALI_LPAE_TTBR_ADRMODE_TABLE;
	return &data->iop;

out_free_data:
	qcom_io_pgtable_allocator_unregister(data->vmid);
	kfree(data);
	return NULL;
}

struct io_pgtable_init_fns qcom_io_pgtable_arm_64_lpae_s1_init_fns = {
	.alloc	= arm_64_lpae_alloc_pgtable_s1,
	.free	= arm_lpae_free_pgtable,
};

struct io_pgtable_init_fns qcom_io_pgtable_arm_64_lpae_s2_init_fns = {
	.alloc	= arm_64_lpae_alloc_pgtable_s2,
	.free	= arm_lpae_free_pgtable,
};

struct io_pgtable_init_fns qcom_io_pgtable_arm_32_lpae_s1_init_fns = {
	.alloc	= arm_32_lpae_alloc_pgtable_s1,
	.free	= arm_lpae_free_pgtable,
};

struct io_pgtable_init_fns qcom_io_pgtable_arm_32_lpae_s2_init_fns = {
	.alloc	= arm_32_lpae_alloc_pgtable_s2,
	.free	= arm_lpae_free_pgtable,
};

struct io_pgtable_init_fns qcom_io_pgtable_arm_mali_lpae_init_fns = {
	.alloc	= arm_mali_lpae_alloc_pgtable,
	.free	= arm_lpae_free_pgtable,
};

#ifdef CONFIG_IOMMU_IO_PGTABLE_LPAE_SELFTEST

static struct io_pgtable_cfg *cfg_cookie __initdata;

static void __init dummy_tlb_flush_all(void *cookie)
{
	WARN_ON(cookie != cfg_cookie);
}

static void __init dummy_tlb_flush(unsigned long iova, size_t size,
				   size_t granule, void *cookie)
{
	WARN_ON(cookie != cfg_cookie);
	WARN_ON(!(size & cfg_cookie->pgsize_bitmap));
}

static void __init dummy_tlb_add_page(struct iommu_iotlb_gather *gather,
				      unsigned long iova, size_t granule,
				      void *cookie)
{
	dummy_tlb_flush(iova, granule, granule, cookie);
}

static const struct iommu_flush_ops dummy_tlb_ops __initconst = {
	.tlb_flush_all	= dummy_tlb_flush_all,
	.tlb_flush_walk	= dummy_tlb_flush,
	.tlb_add_page	= dummy_tlb_add_page,
};

static void __init arm_lpae_dump_ops(struct io_pgtable_ops *ops)
{
	struct arm_lpae_io_pgtable *data = io_pgtable_ops_to_data(ops);
	struct io_pgtable_cfg *cfg = &data->iop.cfg;

	pr_err("cfg: pgsize_bitmap 0x%lx, ias %u-bit\n",
		cfg->pgsize_bitmap, cfg->ias);
	pr_err("data: %d levels, 0x%zx pgd_size, %u pg_shift, %u bits_per_level, pgd @ %p\n",
		ARM_LPAE_MAX_LEVELS - data->start_level, ARM_LPAE_PGD_SIZE(data),
		ilog2(ARM_LPAE_GRANULE(data)), data->bits_per_level, data->pgd);
}

#define __FAIL(ops, i)	({						\
		WARN(1, "selftest: test failed for fmt idx %d\n", (i));	\
		arm_lpae_dump_ops(ops);					\
		selftest_running = false;				\
		-EFAULT;						\
})

static int __init arm_lpae_run_tests(struct io_pgtable_cfg *cfg)
{
	static const enum io_pgtable_fmt fmts[] __initconst = {
		QCOM_ARM_64_LPAE_S1,
		ARM_64_LPAE_S2,
	};

	int i, j;
	unsigned long iova;
	size_t size;
	struct io_pgtable_ops *ops;

	selftest_running = true;

	for (i = 0; i < ARRAY_SIZE(fmts); ++i) {
		cfg_cookie = cfg;
		ops = alloc_io_pgtable_ops(fmts[i], cfg, cfg);
		if (!ops) {
			pr_err("selftest: failed to allocate io pgtable ops\n");
			return -ENOMEM;
		}

		/*
		 * Initial sanity checks.
		 * Empty page tables shouldn't provide any translations.
		 */
		if (ops->iova_to_phys(ops, 42))
			return __FAIL(ops, i);

		if (ops->iova_to_phys(ops, SZ_1G + 42))
			return __FAIL(ops, i);

		if (ops->iova_to_phys(ops, SZ_2G + 42))
			return __FAIL(ops, i);

		/*
		 * Distinct mappings of different granule sizes.
		 */
		iova = 0;
		for_each_set_bit(j, &cfg->pgsize_bitmap, BITS_PER_LONG) {
			size = 1UL << j;

			if (ops->map(ops, iova, iova, size, IOMMU_READ |
							    IOMMU_WRITE |
							    IOMMU_NOEXEC |
							    IOMMU_CACHE, GFP_KERNEL))
				return __FAIL(ops, i);

			/* Overlapping mappings */
			if (!ops->map(ops, iova, iova + size, size,
				      IOMMU_READ | IOMMU_NOEXEC, GFP_KERNEL))
				return __FAIL(ops, i);

			if (ops->iova_to_phys(ops, iova + 42) != (iova + 42))
				return __FAIL(ops, i);

			iova += SZ_1G;
		}

		/* Partial unmap */
		size = 1UL << __ffs(cfg->pgsize_bitmap);
		if (ops->unmap(ops, SZ_1G + size, size, NULL) != size)
			return __FAIL(ops, i);

		/* Remap of partial unmap */
		if (ops->map(ops, SZ_1G + size, size, size, IOMMU_READ, GFP_KERNEL))
			return __FAIL(ops, i);

		if (ops->iova_to_phys(ops, SZ_1G + size + 42) != (size + 42))
			return __FAIL(ops, i);

		/* Full unmap */
		iova = 0;
		for_each_set_bit(j, &cfg->pgsize_bitmap, BITS_PER_LONG) {
			size = 1UL << j;

			if (ops->unmap(ops, iova, size, NULL) != size)
				return __FAIL(ops, i);

			if (ops->iova_to_phys(ops, iova + 42))
				return __FAIL(ops, i);

			/* Remap full block */
			if (ops->map(ops, iova, iova, size, IOMMU_WRITE, GFP_KERNEL))
				return __FAIL(ops, i);

			if (ops->iova_to_phys(ops, iova + 42) != (iova + 42))
				return __FAIL(ops, i);

			iova += SZ_1G;
		}

		free_io_pgtable_ops(ops);
	}

	selftest_running = false;
	return 0;
}

int __init qcom_arm_lpae_do_selftests(void)
{
	static const unsigned long pgsize[] __initconst = {
		SZ_4K | SZ_2M | SZ_1G,
		SZ_16K | SZ_32M,
		SZ_64K | SZ_512M,
	};

	static const unsigned int ias[] __initconst = {
		32, 36, 40, 42, 44, 48,
	};

	int i, j, pass = 0, fail = 0;
	struct io_pgtable_cfg cfg = {
		.tlb = &dummy_tlb_ops,
		.oas = 48,
		.coherent_walk = true,
	};

	for (i = 0; i < ARRAY_SIZE(pgsize); ++i) {
		for (j = 0; j < ARRAY_SIZE(ias); ++j) {
			cfg.pgsize_bitmap = pgsize[i];
			cfg.ias = ias[j];
			pr_info("selftest: pgsize_bitmap 0x%08lx, IAS %u\n",
				pgsize[i], ias[j]);
			if (arm_lpae_run_tests(&cfg))
				fail++;
			else
				pass++;
		}
	}

	pr_info("selftest: completed with %d PASS %d FAIL\n", pass, fail);
	return fail ? -EFAULT : 0;
}
#else
int __init qcom_arm_lpae_do_selftests(void)
{
	return 0;
}
#endif
