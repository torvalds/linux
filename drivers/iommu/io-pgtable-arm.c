/*
 * CPU-agnostic ARM page table allocator.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2014 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#define pr_fmt(fmt)	"arm-lpae io-pgtable: " fmt

#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/io-pgtable.h>
#include <linux/iommu.h>
#include <linux/kernel.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>

#include <asm/barrier.h>

#define ARM_LPAE_MAX_ADDR_BITS		52
#define ARM_LPAE_S2_MAX_CONCAT_PAGES	16
#define ARM_LPAE_MAX_LEVELS		4

/* Struct accessors */
#define io_pgtable_to_data(x)						\
	container_of((x), struct arm_lpae_io_pgtable, iop)

#define io_pgtable_ops_to_data(x)					\
	io_pgtable_to_data(io_pgtable_ops_to_pgtable(x))

/*
 * For consistency with the architecture, we always consider
 * ARM_LPAE_MAX_LEVELS levels, with the walk starting at level n >=0
 */
#define ARM_LPAE_START_LVL(d)		(ARM_LPAE_MAX_LEVELS - (d)->levels)

/*
 * Calculate the right shift amount to get to the portion describing level l
 * in a virtual address mapped by the pagetable in d.
 */
#define ARM_LPAE_LVL_SHIFT(l,d)						\
	((((d)->levels - ((l) - ARM_LPAE_START_LVL(d) + 1))		\
	  * (d)->bits_per_level) + (d)->pg_shift)

#define ARM_LPAE_GRANULE(d)		(1UL << (d)->pg_shift)

#define ARM_LPAE_PAGES_PER_PGD(d)					\
	DIV_ROUND_UP((d)->pgd_size, ARM_LPAE_GRANULE(d))

/*
 * Calculate the index at level l used to map virtual address a using the
 * pagetable in d.
 */
#define ARM_LPAE_PGD_IDX(l,d)						\
	((l) == ARM_LPAE_START_LVL(d) ? ilog2(ARM_LPAE_PAGES_PER_PGD(d)) : 0)

#define ARM_LPAE_LVL_IDX(a,l,d)						\
	(((u64)(a) >> ARM_LPAE_LVL_SHIFT(l,d)) &			\
	 ((1 << ((d)->bits_per_level + ARM_LPAE_PGD_IDX(l,d))) - 1))

/* Calculate the block/page mapping size at level l for pagetable in d. */
#define ARM_LPAE_BLOCK_SIZE(l,d)					\
	(1ULL << (ilog2(sizeof(arm_lpae_iopte)) +			\
		((ARM_LPAE_MAX_LEVELS - (l)) * (d)->bits_per_level)))

/* Page table bits */
#define ARM_LPAE_PTE_TYPE_SHIFT		0
#define ARM_LPAE_PTE_TYPE_MASK		0x3

#define ARM_LPAE_PTE_TYPE_BLOCK		1
#define ARM_LPAE_PTE_TYPE_TABLE		3
#define ARM_LPAE_PTE_TYPE_PAGE		3

#define ARM_LPAE_PTE_ADDR_MASK		GENMASK_ULL(47,12)

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
#define ARM_32_LPAE_TCR_EAE		(1 << 31)
#define ARM_64_LPAE_S2_TCR_RES1		(1 << 31)

#define ARM_LPAE_TCR_EPD1		(1 << 23)

#define ARM_LPAE_TCR_TG0_4K		(0 << 14)
#define ARM_LPAE_TCR_TG0_64K		(1 << 14)
#define ARM_LPAE_TCR_TG0_16K		(2 << 14)

#define ARM_LPAE_TCR_SH0_SHIFT		12
#define ARM_LPAE_TCR_SH0_MASK		0x3
#define ARM_LPAE_TCR_SH_NS		0
#define ARM_LPAE_TCR_SH_OS		2
#define ARM_LPAE_TCR_SH_IS		3

#define ARM_LPAE_TCR_ORGN0_SHIFT	10
#define ARM_LPAE_TCR_IRGN0_SHIFT	8
#define ARM_LPAE_TCR_RGN_MASK		0x3
#define ARM_LPAE_TCR_RGN_NC		0
#define ARM_LPAE_TCR_RGN_WBWA		1
#define ARM_LPAE_TCR_RGN_WT		2
#define ARM_LPAE_TCR_RGN_WB		3

#define ARM_LPAE_TCR_SL0_SHIFT		6
#define ARM_LPAE_TCR_SL0_MASK		0x3

#define ARM_LPAE_TCR_T0SZ_SHIFT		0
#define ARM_LPAE_TCR_SZ_MASK		0xf

#define ARM_LPAE_TCR_PS_SHIFT		16
#define ARM_LPAE_TCR_PS_MASK		0x7

#define ARM_LPAE_TCR_IPS_SHIFT		32
#define ARM_LPAE_TCR_IPS_MASK		0x7

#define ARM_LPAE_TCR_PS_32_BIT		0x0ULL
#define ARM_LPAE_TCR_PS_36_BIT		0x1ULL
#define ARM_LPAE_TCR_PS_40_BIT		0x2ULL
#define ARM_LPAE_TCR_PS_42_BIT		0x3ULL
#define ARM_LPAE_TCR_PS_44_BIT		0x4ULL
#define ARM_LPAE_TCR_PS_48_BIT		0x5ULL
#define ARM_LPAE_TCR_PS_52_BIT		0x6ULL

#define ARM_LPAE_MAIR_ATTR_SHIFT(n)	((n) << 3)
#define ARM_LPAE_MAIR_ATTR_MASK		0xff
#define ARM_LPAE_MAIR_ATTR_DEVICE	0x04
#define ARM_LPAE_MAIR_ATTR_NC		0x44
#define ARM_LPAE_MAIR_ATTR_WBRWA	0xff
#define ARM_LPAE_MAIR_ATTR_IDX_NC	0
#define ARM_LPAE_MAIR_ATTR_IDX_CACHE	1
#define ARM_LPAE_MAIR_ATTR_IDX_DEV	2

/* IOPTE accessors */
#define iopte_deref(pte,d) __va(iopte_to_paddr(pte, d))

#define iopte_type(pte,l)					\
	(((pte) >> ARM_LPAE_PTE_TYPE_SHIFT) & ARM_LPAE_PTE_TYPE_MASK)

#define iopte_prot(pte)	((pte) & ARM_LPAE_PTE_ATTR_MASK)

#define iopte_leaf(pte,l)					\
	(l == (ARM_LPAE_MAX_LEVELS - 1) ?			\
		(iopte_type(pte,l) == ARM_LPAE_PTE_TYPE_PAGE) :	\
		(iopte_type(pte,l) == ARM_LPAE_PTE_TYPE_BLOCK))

struct arm_lpae_io_pgtable {
	struct io_pgtable	iop;

	int			levels;
	size_t			pgd_size;
	unsigned long		pg_shift;
	unsigned long		bits_per_level;

	void			*pgd;
};

typedef u64 arm_lpae_iopte;

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
	u64 paddr = pte & ARM_LPAE_PTE_ADDR_MASK;

	if (data->pg_shift < 16)
		return paddr;

	/* Rotate the packed high-order bits back to the top */
	return (paddr | (paddr << (48 - 12))) & (ARM_LPAE_PTE_ADDR_MASK << 4);
}

static bool selftest_running = false;

static dma_addr_t __arm_lpae_dma_addr(void *pages)
{
	return (dma_addr_t)virt_to_phys(pages);
}

static void *__arm_lpae_alloc_pages(size_t size, gfp_t gfp,
				    struct io_pgtable_cfg *cfg)
{
	struct device *dev = cfg->iommu_dev;
	int order = get_order(size);
	struct page *p;
	dma_addr_t dma;
	void *pages;

	VM_BUG_ON((gfp & __GFP_HIGHMEM));
	p = alloc_pages_node(dev ? dev_to_node(dev) : NUMA_NO_NODE,
			     gfp | __GFP_ZERO, order);
	if (!p)
		return NULL;

	pages = page_address(p);
	if (!(cfg->quirks & IO_PGTABLE_QUIRK_NO_DMA)) {
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
	__free_pages(p, order);
	return NULL;
}

static void __arm_lpae_free_pages(void *pages, size_t size,
				  struct io_pgtable_cfg *cfg)
{
	if (!(cfg->quirks & IO_PGTABLE_QUIRK_NO_DMA))
		dma_unmap_single(cfg->iommu_dev, __arm_lpae_dma_addr(pages),
				 size, DMA_TO_DEVICE);
	free_pages((unsigned long)pages, get_order(size));
}

static void __arm_lpae_sync_pte(arm_lpae_iopte *ptep,
				struct io_pgtable_cfg *cfg)
{
	dma_sync_single_for_device(cfg->iommu_dev, __arm_lpae_dma_addr(ptep),
				   sizeof(*ptep), DMA_TO_DEVICE);
}

static void __arm_lpae_set_pte(arm_lpae_iopte *ptep, arm_lpae_iopte pte,
			       struct io_pgtable_cfg *cfg)
{
	*ptep = pte;

	if (!(cfg->quirks & IO_PGTABLE_QUIRK_NO_DMA))
		__arm_lpae_sync_pte(ptep, cfg);
}

static size_t __arm_lpae_unmap(struct arm_lpae_io_pgtable *data,
			       unsigned long iova, size_t size, int lvl,
			       arm_lpae_iopte *ptep);

static void __arm_lpae_init_pte(struct arm_lpae_io_pgtable *data,
				phys_addr_t paddr, arm_lpae_iopte prot,
				int lvl, arm_lpae_iopte *ptep)
{
	arm_lpae_iopte pte = prot;

	if (data->iop.cfg.quirks & IO_PGTABLE_QUIRK_ARM_NS)
		pte |= ARM_LPAE_PTE_NS;

	if (lvl == ARM_LPAE_MAX_LEVELS - 1)
		pte |= ARM_LPAE_PTE_TYPE_PAGE;
	else
		pte |= ARM_LPAE_PTE_TYPE_BLOCK;

	pte |= ARM_LPAE_PTE_AF | ARM_LPAE_PTE_SH_IS;
	pte |= paddr_to_iopte(paddr, data);

	__arm_lpae_set_pte(ptep, pte, &data->iop.cfg);
}

static int arm_lpae_init_pte(struct arm_lpae_io_pgtable *data,
			     unsigned long iova, phys_addr_t paddr,
			     arm_lpae_iopte prot, int lvl,
			     arm_lpae_iopte *ptep)
{
	arm_lpae_iopte pte = *ptep;

	if (iopte_leaf(pte, lvl)) {
		/* We require an unmap first */
		WARN_ON(!selftest_running);
		return -EEXIST;
	} else if (iopte_type(pte, lvl) == ARM_LPAE_PTE_TYPE_TABLE) {
		/*
		 * We need to unmap and free the old table before
		 * overwriting it with a block entry.
		 */
		arm_lpae_iopte *tblp;
		size_t sz = ARM_LPAE_BLOCK_SIZE(lvl, data);

		tblp = ptep - ARM_LPAE_LVL_IDX(iova, lvl, data);
		if (WARN_ON(__arm_lpae_unmap(data, iova, sz, lvl, tblp) != sz))
			return -EINVAL;
	}

	__arm_lpae_init_pte(data, paddr, prot, lvl, ptep);
	return 0;
}

static arm_lpae_iopte arm_lpae_install_table(arm_lpae_iopte *table,
					     arm_lpae_iopte *ptep,
					     arm_lpae_iopte curr,
					     struct io_pgtable_cfg *cfg)
{
	arm_lpae_iopte old, new;

	new = __pa(table) | ARM_LPAE_PTE_TYPE_TABLE;
	if (cfg->quirks & IO_PGTABLE_QUIRK_ARM_NS)
		new |= ARM_LPAE_PTE_NSTABLE;

	/*
	 * Ensure the table itself is visible before its PTE can be.
	 * Whilst we could get away with cmpxchg64_release below, this
	 * doesn't have any ordering semantics when !CONFIG_SMP.
	 */
	dma_wmb();

	old = cmpxchg64_relaxed(ptep, curr, new);

	if ((cfg->quirks & IO_PGTABLE_QUIRK_NO_DMA) ||
	    (old & ARM_LPAE_PTE_SW_SYNC))
		return old;

	/* Even if it's not ours, there's no point waiting; just kick it */
	__arm_lpae_sync_pte(ptep, cfg);
	if (old == curr)
		WRITE_ONCE(*ptep, new | ARM_LPAE_PTE_SW_SYNC);

	return old;
}

static int __arm_lpae_map(struct arm_lpae_io_pgtable *data, unsigned long iova,
			  phys_addr_t paddr, size_t size, arm_lpae_iopte prot,
			  int lvl, arm_lpae_iopte *ptep)
{
	arm_lpae_iopte *cptep, pte;
	size_t block_size = ARM_LPAE_BLOCK_SIZE(lvl, data);
	size_t tblsz = ARM_LPAE_GRANULE(data);
	struct io_pgtable_cfg *cfg = &data->iop.cfg;

	/* Find our entry at the current level */
	ptep += ARM_LPAE_LVL_IDX(iova, lvl, data);

	/* If we can install a leaf entry at this level, then do so */
	if (size == block_size && (size & cfg->pgsize_bitmap))
		return arm_lpae_init_pte(data, iova, paddr, prot, lvl, ptep);

	/* We can't allocate tables at the final level */
	if (WARN_ON(lvl >= ARM_LPAE_MAX_LEVELS - 1))
		return -EINVAL;

	/* Grab a pointer to the next level */
	pte = READ_ONCE(*ptep);
	if (!pte) {
		cptep = __arm_lpae_alloc_pages(tblsz, GFP_ATOMIC, cfg);
		if (!cptep)
			return -ENOMEM;

		pte = arm_lpae_install_table(cptep, ptep, 0, cfg);
		if (pte)
			__arm_lpae_free_pages(cptep, tblsz, cfg);
	} else if (!(cfg->quirks & IO_PGTABLE_QUIRK_NO_DMA) &&
		   !(pte & ARM_LPAE_PTE_SW_SYNC)) {
		__arm_lpae_sync_pte(ptep, cfg);
	}

	if (pte && !iopte_leaf(pte, lvl)) {
		cptep = iopte_deref(pte, data);
	} else if (pte) {
		/* We require an unmap first */
		WARN_ON(!selftest_running);
		return -EEXIST;
	}

	/* Rinse, repeat */
	return __arm_lpae_map(data, iova, paddr, size, prot, lvl + 1, cptep);
}

static arm_lpae_iopte arm_lpae_prot_to_pte(struct arm_lpae_io_pgtable *data,
					   int prot)
{
	arm_lpae_iopte pte;

	if (data->iop.fmt == ARM_64_LPAE_S1 ||
	    data->iop.fmt == ARM_32_LPAE_S1) {
		pte = ARM_LPAE_PTE_nG;

		if (!(prot & IOMMU_WRITE) && (prot & IOMMU_READ))
			pte |= ARM_LPAE_PTE_AP_RDONLY;

		if (!(prot & IOMMU_PRIV))
			pte |= ARM_LPAE_PTE_AP_UNPRIV;

		if (prot & IOMMU_MMIO)
			pte |= (ARM_LPAE_MAIR_ATTR_IDX_DEV
				<< ARM_LPAE_PTE_ATTRINDX_SHIFT);
		else if (prot & IOMMU_CACHE)
			pte |= (ARM_LPAE_MAIR_ATTR_IDX_CACHE
				<< ARM_LPAE_PTE_ATTRINDX_SHIFT);
	} else {
		pte = ARM_LPAE_PTE_HAP_FAULT;
		if (prot & IOMMU_READ)
			pte |= ARM_LPAE_PTE_HAP_READ;
		if (prot & IOMMU_WRITE)
			pte |= ARM_LPAE_PTE_HAP_WRITE;
		if (prot & IOMMU_MMIO)
			pte |= ARM_LPAE_PTE_MEMATTR_DEV;
		else if (prot & IOMMU_CACHE)
			pte |= ARM_LPAE_PTE_MEMATTR_OIWB;
		else
			pte |= ARM_LPAE_PTE_MEMATTR_NC;
	}

	if (prot & IOMMU_NOEXEC)
		pte |= ARM_LPAE_PTE_XN;

	return pte;
}

static int arm_lpae_map(struct io_pgtable_ops *ops, unsigned long iova,
			phys_addr_t paddr, size_t size, int iommu_prot)
{
	struct arm_lpae_io_pgtable *data = io_pgtable_ops_to_data(ops);
	arm_lpae_iopte *ptep = data->pgd;
	int ret, lvl = ARM_LPAE_START_LVL(data);
	arm_lpae_iopte prot;

	/* If no access, then nothing to do */
	if (!(iommu_prot & (IOMMU_READ | IOMMU_WRITE)))
		return 0;

	if (WARN_ON(iova >= (1ULL << data->iop.cfg.ias) ||
		    paddr >= (1ULL << data->iop.cfg.oas)))
		return -ERANGE;

	prot = arm_lpae_prot_to_pte(data, iommu_prot);
	ret = __arm_lpae_map(data, iova, paddr, size, prot, lvl, ptep);
	/*
	 * Synchronise all PTE updates for the new mapping before there's
	 * a chance for anything to kick off a table walk for the new iova.
	 */
	wmb();

	return ret;
}

static void __arm_lpae_free_pgtable(struct arm_lpae_io_pgtable *data, int lvl,
				    arm_lpae_iopte *ptep)
{
	arm_lpae_iopte *start, *end;
	unsigned long table_size;

	if (lvl == ARM_LPAE_START_LVL(data))
		table_size = data->pgd_size;
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

		if (!pte || iopte_leaf(pte, lvl))
			continue;

		__arm_lpae_free_pgtable(data, lvl + 1, iopte_deref(pte, data));
	}

	__arm_lpae_free_pages(start, table_size, &data->iop.cfg);
}

static void arm_lpae_free_pgtable(struct io_pgtable *iop)
{
	struct arm_lpae_io_pgtable *data = io_pgtable_to_data(iop);

	__arm_lpae_free_pgtable(data, ARM_LPAE_START_LVL(data), data->pgd);
	kfree(data);
}

static size_t arm_lpae_split_blk_unmap(struct arm_lpae_io_pgtable *data,
				       unsigned long iova, size_t size,
				       arm_lpae_iopte blk_pte, int lvl,
				       arm_lpae_iopte *ptep)
{
	struct io_pgtable_cfg *cfg = &data->iop.cfg;
	arm_lpae_iopte pte, *tablep;
	phys_addr_t blk_paddr;
	size_t tablesz = ARM_LPAE_GRANULE(data);
	size_t split_sz = ARM_LPAE_BLOCK_SIZE(lvl, data);
	int i, unmap_idx = -1;

	if (WARN_ON(lvl == ARM_LPAE_MAX_LEVELS))
		return 0;

	tablep = __arm_lpae_alloc_pages(tablesz, GFP_ATOMIC, cfg);
	if (!tablep)
		return 0; /* Bytes unmapped */

	if (size == split_sz)
		unmap_idx = ARM_LPAE_LVL_IDX(iova, lvl, data);

	blk_paddr = iopte_to_paddr(blk_pte, data);
	pte = iopte_prot(blk_pte);

	for (i = 0; i < tablesz / sizeof(pte); i++, blk_paddr += split_sz) {
		/* Unmap! */
		if (i == unmap_idx)
			continue;

		__arm_lpae_init_pte(data, blk_paddr, pte, lvl, &tablep[i]);
	}

	pte = arm_lpae_install_table(tablep, ptep, blk_pte, cfg);
	if (pte != blk_pte) {
		__arm_lpae_free_pages(tablep, tablesz, cfg);
		/*
		 * We may race against someone unmapping another part of this
		 * block, but anything else is invalid. We can't misinterpret
		 * a page entry here since we're never at the last level.
		 */
		if (iopte_type(pte, lvl - 1) != ARM_LPAE_PTE_TYPE_TABLE)
			return 0;

		tablep = iopte_deref(pte, data);
	} else if (unmap_idx >= 0) {
		io_pgtable_tlb_add_flush(&data->iop, iova, size, size, true);
		io_pgtable_tlb_sync(&data->iop);
		return size;
	}

	return __arm_lpae_unmap(data, iova, size, lvl, tablep);
}

static size_t __arm_lpae_unmap(struct arm_lpae_io_pgtable *data,
			       unsigned long iova, size_t size, int lvl,
			       arm_lpae_iopte *ptep)
{
	arm_lpae_iopte pte;
	struct io_pgtable *iop = &data->iop;

	/* Something went horribly wrong and we ran out of page table */
	if (WARN_ON(lvl == ARM_LPAE_MAX_LEVELS))
		return 0;

	ptep += ARM_LPAE_LVL_IDX(iova, lvl, data);
	pte = READ_ONCE(*ptep);
	if (WARN_ON(!pte))
		return 0;

	/* If the size matches this level, we're in the right place */
	if (size == ARM_LPAE_BLOCK_SIZE(lvl, data)) {
		__arm_lpae_set_pte(ptep, 0, &iop->cfg);

		if (!iopte_leaf(pte, lvl)) {
			/* Also flush any partial walks */
			io_pgtable_tlb_add_flush(iop, iova, size,
						ARM_LPAE_GRANULE(data), false);
			io_pgtable_tlb_sync(iop);
			ptep = iopte_deref(pte, data);
			__arm_lpae_free_pgtable(data, lvl + 1, ptep);
		} else if (iop->cfg.quirks & IO_PGTABLE_QUIRK_NON_STRICT) {
			/*
			 * Order the PTE update against queueing the IOVA, to
			 * guarantee that a flush callback from a different CPU
			 * has observed it before the TLBIALL can be issued.
			 */
			smp_wmb();
		} else {
			io_pgtable_tlb_add_flush(iop, iova, size, size, true);
		}

		return size;
	} else if (iopte_leaf(pte, lvl)) {
		/*
		 * Insert a table at the next level to map the old region,
		 * minus the part we want to unmap
		 */
		return arm_lpae_split_blk_unmap(data, iova, size, pte,
						lvl + 1, ptep);
	}

	/* Keep on walkin' */
	ptep = iopte_deref(pte, data);
	return __arm_lpae_unmap(data, iova, size, lvl + 1, ptep);
}

static size_t arm_lpae_unmap(struct io_pgtable_ops *ops, unsigned long iova,
			     size_t size)
{
	struct arm_lpae_io_pgtable *data = io_pgtable_ops_to_data(ops);
	arm_lpae_iopte *ptep = data->pgd;
	int lvl = ARM_LPAE_START_LVL(data);

	if (WARN_ON(iova >= (1ULL << data->iop.cfg.ias)))
		return 0;

	return __arm_lpae_unmap(data, iova, size, lvl, ptep);
}

static phys_addr_t arm_lpae_iova_to_phys(struct io_pgtable_ops *ops,
					 unsigned long iova)
{
	struct arm_lpae_io_pgtable *data = io_pgtable_ops_to_data(ops);
	arm_lpae_iopte pte, *ptep = data->pgd;
	int lvl = ARM_LPAE_START_LVL(data);

	do {
		/* Valid IOPTE pointer? */
		if (!ptep)
			return 0;

		/* Grab the IOPTE we're interested in */
		ptep += ARM_LPAE_LVL_IDX(iova, lvl, data);
		pte = READ_ONCE(*ptep);

		/* Valid entry? */
		if (!pte)
			return 0;

		/* Leaf entry? */
		if (iopte_leaf(pte,lvl))
			goto found_translation;

		/* Take it to the next level */
		ptep = iopte_deref(pte, data);
	} while (++lvl < ARM_LPAE_MAX_LEVELS);

	/* Ran out of page tables to walk */
	return 0;

found_translation:
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
	unsigned long va_bits, pgd_bits;
	struct arm_lpae_io_pgtable *data;

	arm_lpae_restrict_pgsizes(cfg);

	if (!(cfg->pgsize_bitmap & (SZ_4K | SZ_16K | SZ_64K)))
		return NULL;

	if (cfg->ias > ARM_LPAE_MAX_ADDR_BITS)
		return NULL;

	if (cfg->oas > ARM_LPAE_MAX_ADDR_BITS)
		return NULL;

	if (!selftest_running && cfg->iommu_dev->dma_pfn_offset) {
		dev_err(cfg->iommu_dev, "Cannot accommodate DMA offset for IOMMU page tables\n");
		return NULL;
	}

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;

	data->pg_shift = __ffs(cfg->pgsize_bitmap);
	data->bits_per_level = data->pg_shift - ilog2(sizeof(arm_lpae_iopte));

	va_bits = cfg->ias - data->pg_shift;
	data->levels = DIV_ROUND_UP(va_bits, data->bits_per_level);

	/* Calculate the actual size of our pgd (without concatenation) */
	pgd_bits = va_bits - (data->bits_per_level * (data->levels - 1));
	data->pgd_size = 1UL << (pgd_bits + ilog2(sizeof(arm_lpae_iopte)));

	data->iop.ops = (struct io_pgtable_ops) {
		.map		= arm_lpae_map,
		.unmap		= arm_lpae_unmap,
		.iova_to_phys	= arm_lpae_iova_to_phys,
	};

	return data;
}

static struct io_pgtable *
arm_64_lpae_alloc_pgtable_s1(struct io_pgtable_cfg *cfg, void *cookie)
{
	u64 reg;
	struct arm_lpae_io_pgtable *data;

	if (cfg->quirks & ~(IO_PGTABLE_QUIRK_ARM_NS | IO_PGTABLE_QUIRK_NO_DMA |
			    IO_PGTABLE_QUIRK_NON_STRICT))
		return NULL;

	data = arm_lpae_alloc_pgtable(cfg);
	if (!data)
		return NULL;

	/* TCR */
	reg = (ARM_LPAE_TCR_SH_IS << ARM_LPAE_TCR_SH0_SHIFT) |
	      (ARM_LPAE_TCR_RGN_WBWA << ARM_LPAE_TCR_IRGN0_SHIFT) |
	      (ARM_LPAE_TCR_RGN_WBWA << ARM_LPAE_TCR_ORGN0_SHIFT);

	switch (ARM_LPAE_GRANULE(data)) {
	case SZ_4K:
		reg |= ARM_LPAE_TCR_TG0_4K;
		break;
	case SZ_16K:
		reg |= ARM_LPAE_TCR_TG0_16K;
		break;
	case SZ_64K:
		reg |= ARM_LPAE_TCR_TG0_64K;
		break;
	}

	switch (cfg->oas) {
	case 32:
		reg |= (ARM_LPAE_TCR_PS_32_BIT << ARM_LPAE_TCR_IPS_SHIFT);
		break;
	case 36:
		reg |= (ARM_LPAE_TCR_PS_36_BIT << ARM_LPAE_TCR_IPS_SHIFT);
		break;
	case 40:
		reg |= (ARM_LPAE_TCR_PS_40_BIT << ARM_LPAE_TCR_IPS_SHIFT);
		break;
	case 42:
		reg |= (ARM_LPAE_TCR_PS_42_BIT << ARM_LPAE_TCR_IPS_SHIFT);
		break;
	case 44:
		reg |= (ARM_LPAE_TCR_PS_44_BIT << ARM_LPAE_TCR_IPS_SHIFT);
		break;
	case 48:
		reg |= (ARM_LPAE_TCR_PS_48_BIT << ARM_LPAE_TCR_IPS_SHIFT);
		break;
	case 52:
		reg |= (ARM_LPAE_TCR_PS_52_BIT << ARM_LPAE_TCR_IPS_SHIFT);
		break;
	default:
		goto out_free_data;
	}

	reg |= (64ULL - cfg->ias) << ARM_LPAE_TCR_T0SZ_SHIFT;

	/* Disable speculative walks through TTBR1 */
	reg |= ARM_LPAE_TCR_EPD1;
	cfg->arm_lpae_s1_cfg.tcr = reg;

	/* MAIRs */
	reg = (ARM_LPAE_MAIR_ATTR_NC
	       << ARM_LPAE_MAIR_ATTR_SHIFT(ARM_LPAE_MAIR_ATTR_IDX_NC)) |
	      (ARM_LPAE_MAIR_ATTR_WBRWA
	       << ARM_LPAE_MAIR_ATTR_SHIFT(ARM_LPAE_MAIR_ATTR_IDX_CACHE)) |
	      (ARM_LPAE_MAIR_ATTR_DEVICE
	       << ARM_LPAE_MAIR_ATTR_SHIFT(ARM_LPAE_MAIR_ATTR_IDX_DEV));

	cfg->arm_lpae_s1_cfg.mair[0] = reg;
	cfg->arm_lpae_s1_cfg.mair[1] = 0;

	/* Looking good; allocate a pgd */
	data->pgd = __arm_lpae_alloc_pages(data->pgd_size, GFP_KERNEL, cfg);
	if (!data->pgd)
		goto out_free_data;

	/* Ensure the empty pgd is visible before any actual TTBR write */
	wmb();

	/* TTBRs */
	cfg->arm_lpae_s1_cfg.ttbr[0] = virt_to_phys(data->pgd);
	cfg->arm_lpae_s1_cfg.ttbr[1] = 0;
	return &data->iop;

out_free_data:
	kfree(data);
	return NULL;
}

static struct io_pgtable *
arm_64_lpae_alloc_pgtable_s2(struct io_pgtable_cfg *cfg, void *cookie)
{
	u64 reg, sl;
	struct arm_lpae_io_pgtable *data;

	/* The NS quirk doesn't apply at stage 2 */
	if (cfg->quirks & ~(IO_PGTABLE_QUIRK_NO_DMA |
			    IO_PGTABLE_QUIRK_NON_STRICT))
		return NULL;

	data = arm_lpae_alloc_pgtable(cfg);
	if (!data)
		return NULL;

	/*
	 * Concatenate PGDs at level 1 if possible in order to reduce
	 * the depth of the stage-2 walk.
	 */
	if (data->levels == ARM_LPAE_MAX_LEVELS) {
		unsigned long pgd_pages;

		pgd_pages = data->pgd_size >> ilog2(sizeof(arm_lpae_iopte));
		if (pgd_pages <= ARM_LPAE_S2_MAX_CONCAT_PAGES) {
			data->pgd_size = pgd_pages << data->pg_shift;
			data->levels--;
		}
	}

	/* VTCR */
	reg = ARM_64_LPAE_S2_TCR_RES1 |
	     (ARM_LPAE_TCR_SH_IS << ARM_LPAE_TCR_SH0_SHIFT) |
	     (ARM_LPAE_TCR_RGN_WBWA << ARM_LPAE_TCR_IRGN0_SHIFT) |
	     (ARM_LPAE_TCR_RGN_WBWA << ARM_LPAE_TCR_ORGN0_SHIFT);

	sl = ARM_LPAE_START_LVL(data);

	switch (ARM_LPAE_GRANULE(data)) {
	case SZ_4K:
		reg |= ARM_LPAE_TCR_TG0_4K;
		sl++; /* SL0 format is different for 4K granule size */
		break;
	case SZ_16K:
		reg |= ARM_LPAE_TCR_TG0_16K;
		break;
	case SZ_64K:
		reg |= ARM_LPAE_TCR_TG0_64K;
		break;
	}

	switch (cfg->oas) {
	case 32:
		reg |= (ARM_LPAE_TCR_PS_32_BIT << ARM_LPAE_TCR_PS_SHIFT);
		break;
	case 36:
		reg |= (ARM_LPAE_TCR_PS_36_BIT << ARM_LPAE_TCR_PS_SHIFT);
		break;
	case 40:
		reg |= (ARM_LPAE_TCR_PS_40_BIT << ARM_LPAE_TCR_PS_SHIFT);
		break;
	case 42:
		reg |= (ARM_LPAE_TCR_PS_42_BIT << ARM_LPAE_TCR_PS_SHIFT);
		break;
	case 44:
		reg |= (ARM_LPAE_TCR_PS_44_BIT << ARM_LPAE_TCR_PS_SHIFT);
		break;
	case 48:
		reg |= (ARM_LPAE_TCR_PS_48_BIT << ARM_LPAE_TCR_PS_SHIFT);
		break;
	case 52:
		reg |= (ARM_LPAE_TCR_PS_52_BIT << ARM_LPAE_TCR_PS_SHIFT);
		break;
	default:
		goto out_free_data;
	}

	reg |= (64ULL - cfg->ias) << ARM_LPAE_TCR_T0SZ_SHIFT;
	reg |= (~sl & ARM_LPAE_TCR_SL0_MASK) << ARM_LPAE_TCR_SL0_SHIFT;
	cfg->arm_lpae_s2_cfg.vtcr = reg;

	/* Allocate pgd pages */
	data->pgd = __arm_lpae_alloc_pages(data->pgd_size, GFP_KERNEL, cfg);
	if (!data->pgd)
		goto out_free_data;

	/* Ensure the empty pgd is visible before any actual TTBR write */
	wmb();

	/* VTTBR */
	cfg->arm_lpae_s2_cfg.vttbr = virt_to_phys(data->pgd);
	return &data->iop;

out_free_data:
	kfree(data);
	return NULL;
}

static struct io_pgtable *
arm_32_lpae_alloc_pgtable_s1(struct io_pgtable_cfg *cfg, void *cookie)
{
	struct io_pgtable *iop;

	if (cfg->ias > 32 || cfg->oas > 40)
		return NULL;

	cfg->pgsize_bitmap &= (SZ_4K | SZ_2M | SZ_1G);
	iop = arm_64_lpae_alloc_pgtable_s1(cfg, cookie);
	if (iop) {
		cfg->arm_lpae_s1_cfg.tcr |= ARM_32_LPAE_TCR_EAE;
		cfg->arm_lpae_s1_cfg.tcr &= 0xffffffff;
	}

	return iop;
}

static struct io_pgtable *
arm_32_lpae_alloc_pgtable_s2(struct io_pgtable_cfg *cfg, void *cookie)
{
	struct io_pgtable *iop;

	if (cfg->ias > 40 || cfg->oas > 40)
		return NULL;

	cfg->pgsize_bitmap &= (SZ_4K | SZ_2M | SZ_1G);
	iop = arm_64_lpae_alloc_pgtable_s2(cfg, cookie);
	if (iop)
		cfg->arm_lpae_s2_cfg.vtcr &= 0xffffffff;

	return iop;
}

struct io_pgtable_init_fns io_pgtable_arm_64_lpae_s1_init_fns = {
	.alloc	= arm_64_lpae_alloc_pgtable_s1,
	.free	= arm_lpae_free_pgtable,
};

struct io_pgtable_init_fns io_pgtable_arm_64_lpae_s2_init_fns = {
	.alloc	= arm_64_lpae_alloc_pgtable_s2,
	.free	= arm_lpae_free_pgtable,
};

struct io_pgtable_init_fns io_pgtable_arm_32_lpae_s1_init_fns = {
	.alloc	= arm_32_lpae_alloc_pgtable_s1,
	.free	= arm_lpae_free_pgtable,
};

struct io_pgtable_init_fns io_pgtable_arm_32_lpae_s2_init_fns = {
	.alloc	= arm_32_lpae_alloc_pgtable_s2,
	.free	= arm_lpae_free_pgtable,
};

#ifdef CONFIG_IOMMU_IO_PGTABLE_LPAE_SELFTEST

static struct io_pgtable_cfg *cfg_cookie;

static void dummy_tlb_flush_all(void *cookie)
{
	WARN_ON(cookie != cfg_cookie);
}

static void dummy_tlb_add_flush(unsigned long iova, size_t size,
				size_t granule, bool leaf, void *cookie)
{
	WARN_ON(cookie != cfg_cookie);
	WARN_ON(!(size & cfg_cookie->pgsize_bitmap));
}

static void dummy_tlb_sync(void *cookie)
{
	WARN_ON(cookie != cfg_cookie);
}

static const struct iommu_gather_ops dummy_tlb_ops __initconst = {
	.tlb_flush_all	= dummy_tlb_flush_all,
	.tlb_add_flush	= dummy_tlb_add_flush,
	.tlb_sync	= dummy_tlb_sync,
};

static void __init arm_lpae_dump_ops(struct io_pgtable_ops *ops)
{
	struct arm_lpae_io_pgtable *data = io_pgtable_ops_to_data(ops);
	struct io_pgtable_cfg *cfg = &data->iop.cfg;

	pr_err("cfg: pgsize_bitmap 0x%lx, ias %u-bit\n",
		cfg->pgsize_bitmap, cfg->ias);
	pr_err("data: %d levels, 0x%zx pgd_size, %lu pg_shift, %lu bits_per_level, pgd @ %p\n",
		data->levels, data->pgd_size, data->pg_shift,
		data->bits_per_level, data->pgd);
}

#define __FAIL(ops, i)	({						\
		WARN(1, "selftest: test failed for fmt idx %d\n", (i));	\
		arm_lpae_dump_ops(ops);					\
		selftest_running = false;				\
		-EFAULT;						\
})

static int __init arm_lpae_run_tests(struct io_pgtable_cfg *cfg)
{
	static const enum io_pgtable_fmt fmts[] = {
		ARM_64_LPAE_S1,
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
							    IOMMU_CACHE))
				return __FAIL(ops, i);

			/* Overlapping mappings */
			if (!ops->map(ops, iova, iova + size, size,
				      IOMMU_READ | IOMMU_NOEXEC))
				return __FAIL(ops, i);

			if (ops->iova_to_phys(ops, iova + 42) != (iova + 42))
				return __FAIL(ops, i);

			iova += SZ_1G;
		}

		/* Partial unmap */
		size = 1UL << __ffs(cfg->pgsize_bitmap);
		if (ops->unmap(ops, SZ_1G + size, size) != size)
			return __FAIL(ops, i);

		/* Remap of partial unmap */
		if (ops->map(ops, SZ_1G + size, size, size, IOMMU_READ))
			return __FAIL(ops, i);

		if (ops->iova_to_phys(ops, SZ_1G + size + 42) != (size + 42))
			return __FAIL(ops, i);

		/* Full unmap */
		iova = 0;
		for_each_set_bit(j, &cfg->pgsize_bitmap, BITS_PER_LONG) {
			size = 1UL << j;

			if (ops->unmap(ops, iova, size) != size)
				return __FAIL(ops, i);

			if (ops->iova_to_phys(ops, iova + 42))
				return __FAIL(ops, i);

			/* Remap full block */
			if (ops->map(ops, iova, iova, size, IOMMU_WRITE))
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

static int __init arm_lpae_do_selftests(void)
{
	static const unsigned long pgsize[] = {
		SZ_4K | SZ_2M | SZ_1G,
		SZ_16K | SZ_32M,
		SZ_64K | SZ_512M,
	};

	static const unsigned int ias[] = {
		32, 36, 40, 42, 44, 48,
	};

	int i, j, pass = 0, fail = 0;
	struct io_pgtable_cfg cfg = {
		.tlb = &dummy_tlb_ops,
		.oas = 48,
		.quirks = IO_PGTABLE_QUIRK_NO_DMA,
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
subsys_initcall(arm_lpae_do_selftests);
#endif
