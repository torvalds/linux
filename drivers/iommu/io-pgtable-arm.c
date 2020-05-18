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
#include <linux/io-pgtable.h>
#include <linux/iommu.h>
#include <linux/kernel.h>
#include <linux/scatterlist.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>

#include <asm/barrier.h>

#define ARM_LPAE_MAX_ADDR_BITS		48
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

#define ARM_LPAE_LVL_MASK(l, d)						\
	((l) == ARM_LPAE_START_LVL(d) ?	(1 << (d)->pgd_bits) - 1 :	\
					(1 << (d)->bits_per_level) - 1)
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

#define ARM_LPAE_PTE_SH_MASK		(((arm_lpae_iopte)0x3) << 8)
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
#define ARM_LPAE_PTE_AP_PRIV_RW		(((arm_lpae_iopte)0) << 6)
#define ARM_LPAE_PTE_AP_UNPRIV		(((arm_lpae_iopte)1) << 6)
#define ARM_LPAE_PTE_AP_PRIV_RO		(((arm_lpae_iopte)2) << 6)
#define ARM_LPAE_PTE_AP_RO		(((arm_lpae_iopte)3) << 6)
#define ARM_LPAE_PTE_ATTRINDX_MASK	0x7
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

#define ARM_LPAE_MAIR_ATTR_SHIFT(n)	((n) << 3)
#define ARM_LPAE_MAIR1_ATTR_SHIFT(n)	((n-4) << 3)
#define ARM_LPAE_MAIR_ATTR_MASK		0xff
#define ARM_LPAE_MAIR_ATTR_DEVICE	0x04
#define ARM_LPAE_MAIR_ATTR_NC		0x44
#define ARM_LPAE_MAIR_ATTR_WBRWA	0xff
#define ARM_LPAE_MAIR_ATTR_UPSTREAM	0xf4
#define ARM_LPAE_MAIR_ATTR_LLC_NWA	0xe4
#define ARM_LPAE_MAIR_ATTR_IDX_NC	0
#define ARM_LPAE_MAIR_ATTR_IDX_CACHE	1
#define ARM_LPAE_MAIR_ATTR_IDX_DEV	2
#define ARM_LPAE_MAIR_ATTR_IDX_UPSTREAM	3
#define ARM_LPAE_MAIR_ATTR_IDX_LLC_NWA	0x4ULL

/* IOPTE accessors */
#define iopte_deref(pte, d)						\
	(__va(iopte_val(pte) & ((1ULL << ARM_LPAE_MAX_ADDR_BITS) - 1)	\
	& ~(ARM_LPAE_GRANULE(d) - 1ULL)))

#define iopte_type(pte,l)					\
	(((pte) >> ARM_LPAE_PTE_TYPE_SHIFT) & ARM_LPAE_PTE_TYPE_MASK)

#define iopte_prot(pte)	((pte) & ARM_LPAE_PTE_ATTR_MASK)

#define iopte_leaf(pte,l)					\
	(l == (ARM_LPAE_MAX_LEVELS - 1) ?			\
		(iopte_type(pte,l) == ARM_LPAE_PTE_TYPE_PAGE) :	\
		(iopte_type(pte,l) == ARM_LPAE_PTE_TYPE_BLOCK))

#define iopte_to_pfn(pte, d)					\
	(((pte) & ((1ULL << ARM_LPAE_MAX_ADDR_BITS) - 1)) >> (d)->pg_shift)

#define pfn_to_iopte(pfn, d)					\
	(((pfn) << (d)->pg_shift) & ((1ULL << ARM_LPAE_MAX_ADDR_BITS) - 1))

struct arm_lpae_io_pgtable {
	struct io_pgtable	iop;

	int			levels;
	unsigned int		pgd_bits;
	size_t			pgd_size;
	unsigned long		pg_shift;
	unsigned long		bits_per_level;

	void			*pgd;
	void			*pgd_ttbr1;
};

typedef u64 arm_lpae_iopte;

/*
 * We'll use some ignored bits in table entries to keep track of the number
 * of page mappings beneath the table.  The maximum number of entries
 * beneath any table mapping in armv8 is 8192 (which is possible at the
 * 2nd- and 3rd-level when using a 64K granule size).  The bits at our
 * disposal are:
 *
 *     4k granule: [54..52], [11..2]
 *    64k granule: [54..52], [15..2]
 *
 * [54..52], [11..2] is enough bits for tracking table mappings at any
 * level for any granule, so we'll use those.
 */
#define BOTTOM_IGNORED_MASK 0x3ff
#define BOTTOM_IGNORED_SHIFT 2
#define BOTTOM_IGNORED_NUM_BITS 10
#define TOP_IGNORED_MASK 0x7ULL
#define TOP_IGNORED_SHIFT 52
#define IOPTE_RESERVED_MASK ((BOTTOM_IGNORED_MASK << BOTTOM_IGNORED_SHIFT) | \
			     (TOP_IGNORED_MASK << TOP_IGNORED_SHIFT))

static arm_lpae_iopte iopte_val(arm_lpae_iopte table_pte)
{
	return table_pte & ~IOPTE_RESERVED_MASK;
}

static arm_lpae_iopte _iopte_bottom_ignored_val(arm_lpae_iopte table_pte)
{
	return (table_pte & (BOTTOM_IGNORED_MASK << BOTTOM_IGNORED_SHIFT))
		>> BOTTOM_IGNORED_SHIFT;
}

static arm_lpae_iopte _iopte_top_ignored_val(arm_lpae_iopte table_pte)
{
	return (table_pte & (TOP_IGNORED_MASK << TOP_IGNORED_SHIFT))
		>> TOP_IGNORED_SHIFT;
}

static int iopte_tblcnt(arm_lpae_iopte table_pte)
{
	return (_iopte_bottom_ignored_val(table_pte) |
		(_iopte_top_ignored_val(table_pte) << BOTTOM_IGNORED_NUM_BITS));
}

static void iopte_tblcnt_set(arm_lpae_iopte *table_pte, int val)
{
	arm_lpae_iopte pte = iopte_val(*table_pte);

	pte |= ((val & BOTTOM_IGNORED_MASK) << BOTTOM_IGNORED_SHIFT) |
		 (((val & (TOP_IGNORED_MASK << BOTTOM_IGNORED_NUM_BITS))
		   >> BOTTOM_IGNORED_NUM_BITS) << TOP_IGNORED_SHIFT);
	*table_pte = pte;
}

static void iopte_tblcnt_sub(arm_lpae_iopte *table_ptep, int cnt)
{
	arm_lpae_iopte current_cnt = iopte_tblcnt(*table_ptep);

	current_cnt -= cnt;
	iopte_tblcnt_set(table_ptep, current_cnt);
}

static void iopte_tblcnt_add(arm_lpae_iopte *table_ptep, int cnt)
{
	arm_lpae_iopte current_cnt = iopte_tblcnt(*table_ptep);

	current_cnt += cnt;
	iopte_tblcnt_set(table_ptep, current_cnt);
}

static bool selftest_running = false;

static dma_addr_t __arm_lpae_dma_addr(void *pages)
{
	return (dma_addr_t)virt_to_phys(pages);
}

static inline void pgtable_dma_sync_single_for_device(
				struct io_pgtable_cfg *cfg,
				dma_addr_t addr, size_t size,
				enum dma_data_direction dir)
{
	if (!(cfg->quirks & IO_PGTABLE_QUIRK_NO_DMA))
		dma_sync_single_for_device(cfg->iommu_dev, addr, size,
								dir);
}

static void *__arm_lpae_alloc_pages(size_t size, gfp_t gfp,
				    struct io_pgtable_cfg *cfg, void *cookie)
{
	struct device *dev = cfg->iommu_dev;
	dma_addr_t dma;
	void *pages = io_pgtable_alloc_pages_exact(cfg, cookie, size,
						   gfp | __GFP_ZERO);

	if (!pages)
		return NULL;

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
	io_pgtable_free_pages_exact(cfg, cookie, pages, size);
	return NULL;
}

static void __arm_lpae_free_pages(void *pages, size_t size,
				  struct io_pgtable_cfg *cfg, void *cookie)
{
	if (!(cfg->quirks & IO_PGTABLE_QUIRK_NO_DMA))
		dma_unmap_single(cfg->iommu_dev, __arm_lpae_dma_addr(pages),
				 size, DMA_TO_DEVICE);
	io_pgtable_free_pages_exact(cfg, cookie, pages, size);
}

static void __arm_lpae_sync_pte(arm_lpae_iopte *ptep,
				struct io_pgtable_cfg *cfg)
{
	pgtable_dma_sync_single_for_device(cfg, __arm_lpae_dma_addr(ptep),
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
				int lvl, arm_lpae_iopte *ptep,
				bool flush)
{
	arm_lpae_iopte pte = prot;

	if (data->iop.cfg.quirks & IO_PGTABLE_QUIRK_ARM_NS)
		pte |= ARM_LPAE_PTE_NS;

	if (lvl == ARM_LPAE_MAX_LEVELS - 1)
		pte |= ARM_LPAE_PTE_TYPE_PAGE;
	else
		pte |= ARM_LPAE_PTE_TYPE_BLOCK;

	pte |= ARM_LPAE_PTE_AF | ARM_LPAE_PTE_SH_OS;
	pte |= pfn_to_iopte(paddr >> data->pg_shift, data);

	if (flush)
		__arm_lpae_set_pte(ptep, pte, &data->iop.cfg);
	else
		*ptep = pte;
}

static int arm_lpae_init_pte(struct arm_lpae_io_pgtable *data,
			     unsigned long iova, phys_addr_t paddr,
			     arm_lpae_iopte prot, int lvl,
			     arm_lpae_iopte *ptep, arm_lpae_iopte *prev_ptep,
			     bool flush)
{
	arm_lpae_iopte pte = *ptep;

	/* We require an unmap first */
	if (pte & ARM_LPAE_PTE_VALID) {
		WARN_RATELIMIT(1, "map without unmap\n");
		return -EEXIST;
	}

	__arm_lpae_init_pte(data, paddr, prot, lvl, ptep, flush);

	if (prev_ptep)
		iopte_tblcnt_add(prev_ptep, 1);
	return 0;
}

static arm_lpae_iopte arm_lpae_install_table(arm_lpae_iopte *table,
					     arm_lpae_iopte *ptep,
					     arm_lpae_iopte curr,
					     struct io_pgtable_cfg *cfg,
					     int ref_count)
{
	arm_lpae_iopte old, new;

	new = __pa(table) | ARM_LPAE_PTE_TYPE_TABLE;
	if (cfg->quirks & IO_PGTABLE_QUIRK_ARM_NS)
		new |= ARM_LPAE_PTE_NSTABLE;
	iopte_tblcnt_set(&new, ref_count);

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

struct map_state {
	unsigned long iova_end;
	unsigned int pgsize;
	arm_lpae_iopte *pgtable;
	arm_lpae_iopte *prev_pgtable;
	arm_lpae_iopte *pte_start;
	unsigned int num_pte;
};
/* map state optimization works at level 3 (the 2nd-to-last level) */
#define MAP_STATE_LVL 3

static int __arm_lpae_map(struct arm_lpae_io_pgtable *data, unsigned long iova,
			  phys_addr_t paddr, size_t size, arm_lpae_iopte prot,
			  int lvl, arm_lpae_iopte *ptep,
			  arm_lpae_iopte *prev_ptep, struct map_state *ms)
{
	arm_lpae_iopte *cptep, pte;
	size_t block_size = ARM_LPAE_BLOCK_SIZE(lvl, data);
	size_t tblsz = ARM_LPAE_GRANULE(data);
	struct io_pgtable_cfg *cfg = &data->iop.cfg;
	void *cookie = data->iop.cookie;
	arm_lpae_iopte *pgtable = ptep;

	/* Find our entry at the current level */
	ptep += ARM_LPAE_LVL_IDX(iova, lvl, data);

	/* If we can install a leaf entry at this level, then do so */
	if (size == block_size && (size & cfg->pgsize_bitmap)) {
		if (!ms)
			return arm_lpae_init_pte(data, iova, paddr, prot, lvl,
						ptep, prev_ptep, true);

		if (lvl == MAP_STATE_LVL) {
			if (ms->pgtable)
				pgtable_dma_sync_single_for_device(cfg,
					__arm_lpae_dma_addr(ms->pte_start),
					ms->num_pte * sizeof(*ptep),
					DMA_TO_DEVICE);

			ms->iova_end = round_down(iova, SZ_2M) + SZ_2M;
			ms->pgtable = pgtable;
			ms->prev_pgtable = prev_ptep;
			ms->pgsize = size;
			ms->pte_start = ptep;
			ms->num_pte = 1;
		} else {
			/*
			 * We have some map state from previous page
			 * mappings, but we're about to set up a block
			 * mapping.  Flush out the previous page mappings.
			 */
			if (ms->pgtable)
				pgtable_dma_sync_single_for_device(cfg,
					__arm_lpae_dma_addr(ms->pte_start),
					ms->num_pte * sizeof(*ptep),
					DMA_TO_DEVICE);
			memset(ms, 0, sizeof(*ms));
			ms = NULL;
		}

		return arm_lpae_init_pte(data, iova, paddr, prot, lvl,
					ptep, prev_ptep, ms == NULL);
	}

	/* We can't allocate tables at the final level */
	if (WARN_ON(lvl >= ARM_LPAE_MAX_LEVELS - 1))
		return -EINVAL;

	/* Grab a pointer to the next level */
	pte = READ_ONCE(*ptep);
	if (!pte) {
		cptep = __arm_lpae_alloc_pages(tblsz, GFP_ATOMIC, cfg, cookie);
		if (!cptep)
			return -ENOMEM;

		pte = arm_lpae_install_table(cptep, ptep, 0, cfg, 0);
		if (pte)
			__arm_lpae_free_pages(cptep, tblsz, cfg, cookie);

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
	return __arm_lpae_map(data, iova, paddr, size, prot, lvl + 1, cptep,
				ptep, ms);
}

static arm_lpae_iopte arm_lpae_prot_to_pte(struct arm_lpae_io_pgtable *data,
					   int prot)
{
	arm_lpae_iopte pte;

	if (data->iop.fmt == ARM_64_LPAE_S1 ||
	    data->iop.fmt == ARM_32_LPAE_S1) {
		pte = ARM_LPAE_PTE_nG;

		if (prot & IOMMU_WRITE)
			pte |= (prot & IOMMU_PRIV) ? ARM_LPAE_PTE_AP_PRIV_RW
					: ARM_LPAE_PTE_AP_UNPRIV;
		else
			pte |= (prot & IOMMU_PRIV) ? ARM_LPAE_PTE_AP_PRIV_RO
					: ARM_LPAE_PTE_AP_RO;

		if (!(prot & IOMMU_PRIV))
			pte |= ARM_LPAE_PTE_AP_UNPRIV;

		if (prot & IOMMU_MMIO)
			pte |= (ARM_LPAE_MAIR_ATTR_IDX_DEV
				<< ARM_LPAE_PTE_ATTRINDX_SHIFT);
		else if (prot & IOMMU_CACHE)
			pte |= (ARM_LPAE_MAIR_ATTR_IDX_CACHE
				<< ARM_LPAE_PTE_ATTRINDX_SHIFT);
		else if (prot & IOMMU_USE_UPSTREAM_HINT)
			pte |= (ARM_LPAE_MAIR_ATTR_IDX_UPSTREAM
				<< ARM_LPAE_PTE_ATTRINDX_SHIFT);
		else if (prot & IOMMU_USE_LLC_NWA)
			pte |= (ARM_LPAE_MAIR_ATTR_IDX_LLC_NWA
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
	ret = __arm_lpae_map(data, iova, paddr, size, prot, lvl, ptep, NULL,
				NULL);
	/*
	 * Synchronise all PTE updates for the new mapping before there's
	 * a chance for anything to kick off a table walk for the new iova.
	 */
	wmb();

	return ret;
}

static int arm_lpae_map_sg(struct io_pgtable_ops *ops, unsigned long iova,
			   struct scatterlist *sg, unsigned int nents,
			   int iommu_prot, size_t *size)
{
	struct arm_lpae_io_pgtable *data = io_pgtable_ops_to_data(ops);
	arm_lpae_iopte *ptep = data->pgd;
	int lvl = ARM_LPAE_START_LVL(data);
	arm_lpae_iopte prot;
	struct scatterlist *s;
	size_t mapped = 0;
	int i;
	int ret = -EINVAL;
	unsigned int min_pagesz;
	struct io_pgtable_cfg *cfg = &data->iop.cfg;
	struct map_state ms;

	/* If no access, then nothing to do */
	if (!(iommu_prot & (IOMMU_READ | IOMMU_WRITE)))
		goto out_err;

	prot = arm_lpae_prot_to_pte(data, iommu_prot);

	min_pagesz = 1 << __ffs(cfg->pgsize_bitmap);

	memset(&ms, 0, sizeof(ms));

	for_each_sg(sg, s, nents, i) {
		phys_addr_t phys = page_to_phys(sg_page(s)) + s->offset;
		size_t size = s->length;

		/*
		 * We are mapping on IOMMU page boundaries, so offset within
		 * the page must be 0. However, the IOMMU may support pages
		 * smaller than PAGE_SIZE, so s->offset may still represent
		 * an offset of that boundary within the CPU page.
		 */
		if (!IS_ALIGNED(s->offset, min_pagesz))
			goto out_err;

		while (size) {
			size_t pgsize = iommu_pgsize(
				cfg->pgsize_bitmap, iova | phys, size);

			if (ms.pgtable && (iova < ms.iova_end)) {
				arm_lpae_iopte *ptep = ms.pgtable +
					ARM_LPAE_LVL_IDX(iova, MAP_STATE_LVL,
							 data);
				arm_lpae_init_pte(
					data, iova, phys, prot, MAP_STATE_LVL,
					ptep, ms.prev_pgtable, false);
				ms.num_pte++;
			} else {
				ret = __arm_lpae_map(data, iova, phys, pgsize,
						prot, lvl, ptep, NULL, &ms);
				if (ret)
					goto out_err;
			}

			iova += pgsize;
			mapped += pgsize;
			phys += pgsize;
			size -= pgsize;
		}
	}

	if (ms.pgtable)
		pgtable_dma_sync_single_for_device(cfg,
			__arm_lpae_dma_addr(ms.pte_start),
			ms.num_pte * sizeof(*ms.pte_start),
			DMA_TO_DEVICE);

	/*
	 * Synchronise all PTE updates for the new mapping before there's
	 * a chance for anything to kick off a table walk for the new iova.
	 */
	wmb();

	return mapped;

out_err:
	/* Return the size of the partial mapping so that they can be undone */
	*size = mapped;
	return ret;
}

static void __arm_lpae_free_pgtable(struct arm_lpae_io_pgtable *data, int lvl,
				    arm_lpae_iopte *ptep)
{
	arm_lpae_iopte *start, *end;
	unsigned long table_size;
	void *cookie = data->iop.cookie;

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

	__arm_lpae_free_pages(start, table_size, &data->iop.cfg, cookie);
}

static void arm_lpae_free_pgtable(struct io_pgtable *iop)
{
	struct arm_lpae_io_pgtable *data = io_pgtable_to_data(iop);

	__arm_lpae_free_pgtable(data, ARM_LPAE_START_LVL(data), data->pgd);
	__arm_lpae_free_pgtable(data, ARM_LPAE_START_LVL(data),
				data->pgd_ttbr1);
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
	void *cookie = data->iop.cookie;
	int child_cnt = 0;

	size = iommu_pgsize(data->iop.cfg.pgsize_bitmap, iova, size);

	if (WARN_ON(lvl == ARM_LPAE_MAX_LEVELS))
		return 0;

	tablep = __arm_lpae_alloc_pages(tablesz, GFP_ATOMIC, cfg, cookie);
	if (!tablep)
		return 0; /* Bytes unmapped */

	if (size == split_sz)
		unmap_idx = ARM_LPAE_LVL_IDX(iova, lvl, data);

	blk_paddr = iopte_to_pfn(blk_pte, data) << data->pg_shift;
	pte = iopte_prot(blk_pte);

	for (i = 0; i < tablesz / sizeof(pte); i++, blk_paddr += split_sz) {
		/* Unmap! */
		if (i == unmap_idx)
			continue;

		__arm_lpae_init_pte(data, blk_paddr, pte, lvl, &tablep[i],
				    true);
		child_cnt++;
	}

	pte = arm_lpae_install_table(tablep, ptep, blk_pte, cfg, child_cnt);
	if (pte != blk_pte) {
		__arm_lpae_free_pages(tablep, tablesz, cfg, cookie);
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
			ptep = iopte_deref(pte, data);
			__arm_lpae_free_pgtable(data, lvl + 1, ptep);
		}

		return size;
	} else if ((lvl == ARM_LPAE_MAX_LEVELS - 2) && !iopte_leaf(pte, lvl)) {
		arm_lpae_iopte *table = iopte_deref(pte, data);
		arm_lpae_iopte *table_base = table;
		int tl_offset = ARM_LPAE_LVL_IDX(iova, lvl + 1, data);
		int entry_size = ARM_LPAE_GRANULE(data);
		int max_entries = ARM_LPAE_BLOCK_SIZE(lvl, data) >>
				data->pg_shift;
		int entries = min_t(int, size / entry_size,
			max_entries - tl_offset);
		int table_len = entries * sizeof(*table);

		/*
		 * This isn't a block mapping so it must be a table mapping
		 * and since it's the 2nd-to-last level the next level has
		 * to be all page mappings.  Zero them all out in one fell
		 * swoop.
		 */

		table += tl_offset;

		memset(table, 0, table_len);
		pgtable_dma_sync_single_for_device(&iop->cfg,
					   __arm_lpae_dma_addr(table),
					   table_len, DMA_TO_DEVICE);

		iopte_tblcnt_sub(ptep, entries);
		if (!iopte_tblcnt(*ptep)) {
			/* no valid mappings left under this table. free it. */
			__arm_lpae_set_pte(ptep, 0, &iop->cfg);
			__arm_lpae_free_pgtable(data, lvl + 1, table_base);
		}

		return entries * entry_size;
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
	size_t unmapped = 0;
	struct arm_lpae_io_pgtable *data = io_pgtable_ops_to_data(ops);
	arm_lpae_iopte *ptep = data->pgd;
	int lvl = ARM_LPAE_START_LVL(data);

	if (WARN_ON(iova >= (1ULL << data->iop.cfg.ias)))
		return 0;

	while (unmapped < size) {
		size_t ret, size_to_unmap, remaining;

		remaining = (size - unmapped);
		size_to_unmap = iommu_pgsize(data->iop.cfg.pgsize_bitmap, iova,
						remaining);
		size_to_unmap = size_to_unmap >= SZ_2M ?
				size_to_unmap :
				min_t(unsigned long, remaining,
					(ALIGN(iova + 1, SZ_2M) - iova));
		ret = __arm_lpae_unmap(data, iova, size_to_unmap, lvl, ptep);
		if (ret == 0)
			break;
		unmapped += ret;
		iova += ret;
	}

	if (unmapped)
		io_pgtable_tlb_flush_all(&data->iop);

	return unmapped;
}

static int arm_lpae_iova_to_pte(struct arm_lpae_io_pgtable *data,
				unsigned long iova, int *plvl_ret,
				arm_lpae_iopte *ptep_ret)
{
	arm_lpae_iopte pte, *ptep = data->pgd;
	*plvl_ret = ARM_LPAE_START_LVL(data);
	*ptep_ret = 0;

	do {
		/* Valid IOPTE pointer? */
		if (!ptep)
			return -EINVAL;

		/* Grab the IOPTE we're interested in */
		pte = *(ptep + ARM_LPAE_LVL_IDX(iova, *plvl_ret, data));

		/* Valid entry? */
		if (!pte)
			return -EINVAL;

		/* Leaf entry? */
		if (iopte_leaf(pte, *plvl_ret))
			goto found_translation;

		/* Take it to the next level */
		ptep = iopte_deref(pte, data);
	} while (++(*plvl_ret) < ARM_LPAE_MAX_LEVELS);

	/* Ran out of page tables to walk */
	return -EINVAL;

found_translation:
	*ptep_ret = pte;
	return 0;
}

static uint64_t arm_lpae_iova_get_pte(struct io_pgtable_ops *ops,
					 unsigned long iova)
{
	struct arm_lpae_io_pgtable *data = io_pgtable_ops_to_data(ops);
	arm_lpae_iopte pte;
	int lvl;

	if (!arm_lpae_iova_to_pte(data, iova, &lvl, &pte))
		return pte;

	return 0;
}

static phys_addr_t arm_lpae_iova_to_phys(struct io_pgtable_ops *ops,
					 unsigned long iova)
{
	struct arm_lpae_io_pgtable *data = io_pgtable_ops_to_data(ops);
	arm_lpae_iopte pte;
	int lvl;
	phys_addr_t phys = 0;

	if (!arm_lpae_iova_to_pte(data, iova, &lvl, &pte)) {
		iova &= ((1 << ARM_LPAE_LVL_SHIFT(lvl, data)) - 1);
		phys = ((phys_addr_t)iopte_to_pfn(pte, data)
				<< data->pg_shift) | iova;
	}

	return phys;
}

static bool __arm_lpae_is_iova_coherent(struct arm_lpae_io_pgtable *data,
				    arm_lpae_iopte *ptep)
{
	if (data->iop.fmt == ARM_64_LPAE_S1 ||
	    data->iop.fmt == ARM_32_LPAE_S1) {
		int attr_idx = (*ptep & (ARM_LPAE_PTE_ATTRINDX_MASK <<
					ARM_LPAE_PTE_ATTRINDX_SHIFT)) >>
					ARM_LPAE_PTE_ATTRINDX_SHIFT;
		if ((attr_idx == ARM_LPAE_MAIR_ATTR_IDX_CACHE) &&
		   (((*ptep & ARM_LPAE_PTE_SH_MASK) == ARM_LPAE_PTE_SH_IS)
		     ||
		     (*ptep & ARM_LPAE_PTE_SH_MASK) == ARM_LPAE_PTE_SH_OS))
			return true;
	} else {
		if (*ptep & ARM_LPAE_PTE_MEMATTR_OIWB)
			return true;
	}

	return false;
}

static bool arm_lpae_is_iova_coherent(struct io_pgtable_ops *ops,
					 unsigned long iova)
{
	struct arm_lpae_io_pgtable *data = io_pgtable_ops_to_data(ops);
	arm_lpae_iopte pte;
	int lvl;
	bool ret = false;

	if (!arm_lpae_iova_to_pte(data, iova, &lvl, &pte))
		ret = __arm_lpae_is_iova_coherent(data, &pte);

	return ret;
}

static void arm_lpae_restrict_pgsizes(struct io_pgtable_cfg *cfg)
{
	unsigned long granule;

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
		cfg->pgsize_bitmap &= (SZ_4K | SZ_2M | SZ_1G);
		break;
	case SZ_16K:
		cfg->pgsize_bitmap &= (SZ_16K | SZ_32M);
		break;
	case SZ_64K:
		cfg->pgsize_bitmap &= (SZ_64K | SZ_512M);
		break;
	default:
		cfg->pgsize_bitmap = 0;
	}
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
	data->pgd_bits = pgd_bits;
	data->pgd_size = 1UL << (pgd_bits + ilog2(sizeof(arm_lpae_iopte)));

	data->iop.ops = (struct io_pgtable_ops) {
		.map		= arm_lpae_map,
		.map_sg		= arm_lpae_map_sg,
		.unmap		= arm_lpae_unmap,
		.iova_to_phys	= arm_lpae_iova_to_phys,
		.is_iova_coherent = arm_lpae_is_iova_coherent,
		.iova_to_pte	= arm_lpae_iova_get_pte,
	};

	return data;
}

static struct io_pgtable *
arm_64_lpae_alloc_pgtable_s1(struct io_pgtable_cfg *cfg, void *cookie)
{
	u64 reg;
	struct arm_lpae_io_pgtable *data;

	if (cfg->quirks & ~(IO_PGTABLE_QUIRK_ARM_NS
			  | IO_PGTABLE_QUIRK_NO_DMA
			  | IO_PGTABLE_QUIRK_QCOM_USE_UPSTREAM_HINT
			  | IO_PGTABLE_QUIRK_QSMMUV500_NON_SHAREABLE
			  | IO_PGTABLE_QUIRK_QCOM_USE_LLC_NWA))
		return NULL;

	data = arm_lpae_alloc_pgtable(cfg);
	if (!data)
		return NULL;

	/* TCR */
	if (cfg->quirks & IO_PGTABLE_QUIRK_NO_DMA)
		reg = (ARM_LPAE_TCR_SH_OS << ARM_LPAE_TCR_SH0_SHIFT) |
			(ARM_LPAE_TCR_RGN_WBWA << ARM_LPAE_TCR_IRGN0_SHIFT) |
			(ARM_LPAE_TCR_RGN_WBWA << ARM_LPAE_TCR_ORGN0_SHIFT);
	else if ((cfg->quirks & IO_PGTABLE_QUIRK_QCOM_USE_UPSTREAM_HINT) &&
		(cfg->quirks & IO_PGTABLE_QUIRK_QSMMUV500_NON_SHAREABLE))
		reg = (ARM_LPAE_TCR_SH_NS << ARM_LPAE_TCR_SH0_SHIFT) |
			(ARM_LPAE_TCR_RGN_NC << ARM_LPAE_TCR_IRGN0_SHIFT) |
			(ARM_LPAE_TCR_RGN_WBWA << ARM_LPAE_TCR_ORGN0_SHIFT);
	else if (cfg->quirks & IO_PGTABLE_QUIRK_QCOM_USE_UPSTREAM_HINT)
		reg = (ARM_LPAE_TCR_SH_OS << ARM_LPAE_TCR_SH0_SHIFT) |
			(ARM_LPAE_TCR_RGN_NC << ARM_LPAE_TCR_IRGN0_SHIFT) |
			(ARM_LPAE_TCR_RGN_WBWA << ARM_LPAE_TCR_ORGN0_SHIFT);
	else if ((cfg->quirks & IO_PGTABLE_QUIRK_QCOM_USE_LLC_NWA) &&
		(cfg->quirks & IO_PGTABLE_QUIRK_QSMMUV500_NON_SHAREABLE))
		reg = (ARM_LPAE_TCR_SH_NS << ARM_LPAE_TCR_SH0_SHIFT) |
			(ARM_LPAE_TCR_RGN_NC << ARM_LPAE_TCR_IRGN0_SHIFT) |
			(ARM_LPAE_TCR_RGN_WB << ARM_LPAE_TCR_ORGN0_SHIFT);
	else if (cfg->quirks & IO_PGTABLE_QUIRK_QCOM_USE_LLC_NWA)
		reg = (ARM_LPAE_TCR_SH_OS << ARM_LPAE_TCR_SH0_SHIFT) |
			(ARM_LPAE_TCR_RGN_NC << ARM_LPAE_TCR_IRGN0_SHIFT) |
			(ARM_LPAE_TCR_RGN_WB << ARM_LPAE_TCR_ORGN0_SHIFT);
	else
		reg = (ARM_LPAE_TCR_SH_OS << ARM_LPAE_TCR_SH0_SHIFT) |
			(ARM_LPAE_TCR_RGN_NC << ARM_LPAE_TCR_IRGN0_SHIFT) |
			(ARM_LPAE_TCR_RGN_NC << ARM_LPAE_TCR_ORGN0_SHIFT);

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
	       << ARM_LPAE_MAIR_ATTR_SHIFT(ARM_LPAE_MAIR_ATTR_IDX_DEV)) |
	      (ARM_LPAE_MAIR_ATTR_UPSTREAM
	       << ARM_LPAE_MAIR_ATTR_SHIFT(ARM_LPAE_MAIR_ATTR_IDX_UPSTREAM));

	cfg->arm_lpae_s1_cfg.mair[0] = reg;

	reg = ARM_LPAE_MAIR_ATTR_LLC_NWA
	      << ARM_LPAE_MAIR1_ATTR_SHIFT(ARM_LPAE_MAIR_ATTR_IDX_LLC_NWA);

	cfg->arm_lpae_s1_cfg.mair[1] = reg;

	/* Looking good; allocate a pgd */
	data->pgd = __arm_lpae_alloc_pages(data->pgd_size, GFP_KERNEL,
					   cfg, cookie);
	if (!data->pgd)
		goto out_free_data;

	data->pgd_ttbr1 = __arm_lpae_alloc_pages(data->pgd_size, GFP_KERNEL,
					   cfg, cookie);
	if (!data->pgd_ttbr1)
		goto out_free_pgd;

	/* Ensure the empty pgd is visible before any actual TTBR write */
	wmb();

	/* TTBRs */
	cfg->arm_lpae_s1_cfg.ttbr[0] = virt_to_phys(data->pgd);
	cfg->arm_lpae_s1_cfg.ttbr[1] = virt_to_phys(data->pgd_ttbr1);
	return &data->iop;

out_free_pgd:
	__arm_lpae_free_pages(data->pgd, data->pgd_size, cfg, cookie);

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
	if (!(cfg->quirks & IO_PGTABLE_QUIRK_ARM_NS))
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
	default:
		goto out_free_data;
	}

	reg |= (64ULL - cfg->ias) << ARM_LPAE_TCR_T0SZ_SHIFT;
	reg |= (~sl & ARM_LPAE_TCR_SL0_MASK) << ARM_LPAE_TCR_SL0_SHIFT;
	cfg->arm_lpae_s2_cfg.vtcr = reg;

	/* Allocate pgd pages */
	data->pgd = __arm_lpae_alloc_pages(data->pgd_size, GFP_KERNEL,
					   cfg, cookie);
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

/*
 * Returns true if there's any mapping in the given iova range in ops.
 */
static bool arm_lpae_range_has_mapping(struct io_pgtable_ops *ops,
				       unsigned long iova_start, size_t size)
{
	unsigned long iova = iova_start;

	while (iova < (iova_start + size)) {
		if (ops->iova_to_phys(ops, iova + 42))
			return true;
		iova += SZ_4K;
	}
	return false;
}

/*
 * Returns true if the iova range is successfully mapped to the contiguous
 * phys range in ops.
 */
static bool arm_lpae_range_has_specific_mapping(struct io_pgtable_ops *ops,
						const unsigned long iova_start,
						const phys_addr_t phys_start,
						const size_t size)
{
	unsigned long iova = iova_start;
	phys_addr_t phys = phys_start;

	while (iova < (iova_start + size)) {
		if (ops->iova_to_phys(ops, iova + 42) != (phys + 42))
			return false;
		iova += SZ_4K;
		phys += SZ_4K;
	}
	return true;
}

static int __init arm_lpae_run_tests(struct io_pgtable_cfg *cfg)
{
	static const enum io_pgtable_fmt fmts[] = {
		ARM_64_LPAE_S1,
		ARM_64_LPAE_S2,
	};

	int i, j, k;
	unsigned long iova;
	size_t size;
	struct io_pgtable_ops *ops;
	selftest_running = true;

	for (i = 0; i < ARRAY_SIZE(fmts); ++i) {
		unsigned long test_sg_sizes[] = { SZ_4K, SZ_64K, SZ_2M,
						  SZ_1M * 12, SZ_1M * 20 };

		cfg_cookie = cfg;
		ops = alloc_io_pgtable_ops(fmts[i], cfg, cfg);
		if (!ops) {
			pr_err("selftest: failed to allocate io pgtable ops\n");
			return -ENOMEM;
		}

		/*
		 * Initial sanity checks.  Empty page tables shouldn't
		 * provide any translations.  TODO: check entire supported
		 * range for these ops rather than first 2G
		 */
		if (arm_lpae_range_has_mapping(ops, 0, SZ_2G))
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

			if (!arm_lpae_range_has_specific_mapping(ops, iova,
								 iova, size))
				return __FAIL(ops, i);

			iova += SZ_1G;
		}

		/* Partial unmap */
		size = 1UL << __ffs(cfg->pgsize_bitmap);
		if (ops->unmap(ops, SZ_1G + size, size) != size)
			return __FAIL(ops, i);

		if (arm_lpae_range_has_mapping(ops, SZ_1G + size, size))
			return __FAIL(ops, i);

		/* Remap of partial unmap */
		if (ops->map(ops, SZ_1G + size, size, size, IOMMU_READ))
			return __FAIL(ops, i);

		if (!arm_lpae_range_has_specific_mapping(ops, SZ_1G + size,
							 size, size))
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

			if (ops->unmap(ops, iova, size) != size)
				return __FAIL(ops, i);

			iova += SZ_1G;
		}

		if (arm_lpae_range_has_mapping(ops, 0, SZ_2G))
			return __FAIL(ops, i);

		if ((cfg->pgsize_bitmap & SZ_2M) &&
		    (cfg->pgsize_bitmap & SZ_4K)) {
			/* mixed block + page mappings */
			iova = 0;
			if (ops->map(ops, iova, iova, SZ_2M, IOMMU_READ))
				return __FAIL(ops, i);

			if (ops->map(ops, iova + SZ_2M, iova + SZ_2M, SZ_4K,
				     IOMMU_READ))
				return __FAIL(ops, i);

			if (ops->iova_to_phys(ops, iova + 42) != (iova + 42))
				return __FAIL(ops, i);

			if (ops->iova_to_phys(ops, iova + SZ_2M + 42) !=
			    (iova + SZ_2M + 42))
				return __FAIL(ops, i);

			/* unmap both mappings at once */
			if (ops->unmap(ops, iova, SZ_2M + SZ_4K) !=
			    (SZ_2M + SZ_4K))
				return __FAIL(ops, i);

			if (arm_lpae_range_has_mapping(ops, 0, SZ_2G))
				return __FAIL(ops, i);
		}

		/* map_sg */
		for (j = 0; j < ARRAY_SIZE(test_sg_sizes); ++j) {
			size_t mapped;
			size_t unused;
			struct page *page;
			phys_addr_t page_phys;
			struct sg_table table;
			struct scatterlist *sg;
			unsigned long total_size = test_sg_sizes[j];
			int chunk_size = 1UL << find_first_bit(
				&cfg->pgsize_bitmap, BITS_PER_LONG);
			int nents = total_size / chunk_size;
			int ret;

			if (total_size < chunk_size)
				continue;

			page = alloc_pages(GFP_KERNEL, get_order(chunk_size));
			if (!page)
				return -ENOMEM;
			page_phys = page_to_phys(page);

			iova = 0;
			ret = sg_alloc_table(&table, nents, GFP_KERNEL);
			if (ret)
				return ret;
			for_each_sg(table.sgl, sg, table.nents, k)
				sg_set_page(sg, page, chunk_size, 0);

			mapped = ops->map_sg(ops, iova, table.sgl, table.nents,
					     IOMMU_READ | IOMMU_WRITE, &unused);

			if (mapped != total_size)
				return __FAIL(ops, i);

			if (!arm_lpae_range_has_mapping(ops, iova, total_size))
				return __FAIL(ops, i);

			if (arm_lpae_range_has_mapping(ops, iova + total_size,
					      SZ_2G - (iova + total_size)))
				return __FAIL(ops, i);

			for_each_sg(table.sgl, sg, table.nents, k) {
				dma_addr_t newphys =
					ops->iova_to_phys(ops, iova + 42);
				if (newphys != (page_phys + 42))
					return __FAIL(ops, i);
				iova += chunk_size;
			}

			if (ops->unmap(ops, 0, total_size) != total_size)
				return __FAIL(ops, i);

			if (arm_lpae_range_has_mapping(ops, 0, SZ_2G))
				return __FAIL(ops, i);

			sg_free_table(&table);
			__free_pages(page, get_order(chunk_size));
		}

		if (arm_lpae_range_has_mapping(ops, 0, SZ_2G))
			return __FAIL(ops, i);

		free_io_pgtable_ops(ops);
	}

	selftest_running = false;
	return 0;
}

static int __init arm_lpae_do_selftests(void)
{
	static const unsigned long pgsize[] = {
		SZ_4K | SZ_2M | SZ_1G,
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
