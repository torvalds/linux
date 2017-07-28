/*
 * CPU-agnostic ARM page table allocator.
 *
 * ARMv7 Short-descriptor format, supporting
 * - Basic memory attributes
 * - Simplified access permissions (AP[2:1] model)
 * - Backwards-compatible TEX remap
 * - Large pages/supersections (if indicated by the caller)
 *
 * Not supporting:
 * - Legacy access permissions (AP[2:0] model)
 *
 * Almost certainly never supporting:
 * - PXN
 * - Domains
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
 * Copyright (C) 2014-2015 ARM Limited
 * Copyright (c) 2014-2015 MediaTek Inc.
 */

#define pr_fmt(fmt)	"arm-v7s io-pgtable: " fmt

#include <linux/atomic.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>
#include <linux/iommu.h>
#include <linux/kernel.h>
#include <linux/kmemleak.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <asm/barrier.h>

#include "io-pgtable.h"

/* Struct accessors */
#define io_pgtable_to_data(x)						\
	container_of((x), struct arm_v7s_io_pgtable, iop)

#define io_pgtable_ops_to_data(x)					\
	io_pgtable_to_data(io_pgtable_ops_to_pgtable(x))

/*
 * We have 32 bits total; 12 bits resolved at level 1, 8 bits at level 2,
 * and 12 bits in a page. With some carefully-chosen coefficients we can
 * hide the ugly inconsistencies behind these macros and at least let the
 * rest of the code pretend to be somewhat sane.
 */
#define ARM_V7S_ADDR_BITS		32
#define _ARM_V7S_LVL_BITS(lvl)		(16 - (lvl) * 4)
#define ARM_V7S_LVL_SHIFT(lvl)		(ARM_V7S_ADDR_BITS - (4 + 8 * (lvl)))
#define ARM_V7S_TABLE_SHIFT		10

#define ARM_V7S_PTES_PER_LVL(lvl)	(1 << _ARM_V7S_LVL_BITS(lvl))
#define ARM_V7S_TABLE_SIZE(lvl)						\
	(ARM_V7S_PTES_PER_LVL(lvl) * sizeof(arm_v7s_iopte))

#define ARM_V7S_BLOCK_SIZE(lvl)		(1UL << ARM_V7S_LVL_SHIFT(lvl))
#define ARM_V7S_LVL_MASK(lvl)		((u32)(~0U << ARM_V7S_LVL_SHIFT(lvl)))
#define ARM_V7S_TABLE_MASK		((u32)(~0U << ARM_V7S_TABLE_SHIFT))
#define _ARM_V7S_IDX_MASK(lvl)		(ARM_V7S_PTES_PER_LVL(lvl) - 1)
#define ARM_V7S_LVL_IDX(addr, lvl)	({				\
	int _l = lvl;							\
	((u32)(addr) >> ARM_V7S_LVL_SHIFT(_l)) & _ARM_V7S_IDX_MASK(_l); \
})

/*
 * Large page/supersection entries are effectively a block of 16 page/section
 * entries, along the lines of the LPAE contiguous hint, but all with the
 * same output address. For want of a better common name we'll call them
 * "contiguous" versions of their respective page/section entries here, but
 * noting the distinction (WRT to TLB maintenance) that they represent *one*
 * entry repeated 16 times, not 16 separate entries (as in the LPAE case).
 */
#define ARM_V7S_CONT_PAGES		16

/* PTE type bits: these are all mixed up with XN/PXN bits in most cases */
#define ARM_V7S_PTE_TYPE_TABLE		0x1
#define ARM_V7S_PTE_TYPE_PAGE		0x2
#define ARM_V7S_PTE_TYPE_CONT_PAGE	0x1

#define ARM_V7S_PTE_IS_VALID(pte)	(((pte) & 0x3) != 0)
#define ARM_V7S_PTE_IS_TABLE(pte, lvl) \
	((lvl) == 1 && (((pte) & 0x3) == ARM_V7S_PTE_TYPE_TABLE))

/* Page table bits */
#define ARM_V7S_ATTR_XN(lvl)		BIT(4 * (2 - (lvl)))
#define ARM_V7S_ATTR_B			BIT(2)
#define ARM_V7S_ATTR_C			BIT(3)
#define ARM_V7S_ATTR_NS_TABLE		BIT(3)
#define ARM_V7S_ATTR_NS_SECTION		BIT(19)

#define ARM_V7S_CONT_SECTION		BIT(18)
#define ARM_V7S_CONT_PAGE_XN_SHIFT	15

/*
 * The attribute bits are consistently ordered*, but occupy bits [17:10] of
 * a level 1 PTE vs. bits [11:4] at level 2. Thus we define the individual
 * fields relative to that 8-bit block, plus a total shift relative to the PTE.
 */
#define ARM_V7S_ATTR_SHIFT(lvl)		(16 - (lvl) * 6)

#define ARM_V7S_ATTR_MASK		0xff
#define ARM_V7S_ATTR_AP0		BIT(0)
#define ARM_V7S_ATTR_AP1		BIT(1)
#define ARM_V7S_ATTR_AP2		BIT(5)
#define ARM_V7S_ATTR_S			BIT(6)
#define ARM_V7S_ATTR_NG			BIT(7)
#define ARM_V7S_TEX_SHIFT		2
#define ARM_V7S_TEX_MASK		0x7
#define ARM_V7S_ATTR_TEX(val)		(((val) & ARM_V7S_TEX_MASK) << ARM_V7S_TEX_SHIFT)

#define ARM_V7S_ATTR_MTK_4GB		BIT(9) /* MTK extend it for 4GB mode */

/* *well, except for TEX on level 2 large pages, of course :( */
#define ARM_V7S_CONT_PAGE_TEX_SHIFT	6
#define ARM_V7S_CONT_PAGE_TEX_MASK	(ARM_V7S_TEX_MASK << ARM_V7S_CONT_PAGE_TEX_SHIFT)

/* Simplified access permissions */
#define ARM_V7S_PTE_AF			ARM_V7S_ATTR_AP0
#define ARM_V7S_PTE_AP_UNPRIV		ARM_V7S_ATTR_AP1
#define ARM_V7S_PTE_AP_RDONLY		ARM_V7S_ATTR_AP2

/* Register bits */
#define ARM_V7S_RGN_NC			0
#define ARM_V7S_RGN_WBWA		1
#define ARM_V7S_RGN_WT			2
#define ARM_V7S_RGN_WB			3

#define ARM_V7S_PRRR_TYPE_DEVICE	1
#define ARM_V7S_PRRR_TYPE_NORMAL	2
#define ARM_V7S_PRRR_TR(n, type)	(((type) & 0x3) << ((n) * 2))
#define ARM_V7S_PRRR_DS0		BIT(16)
#define ARM_V7S_PRRR_DS1		BIT(17)
#define ARM_V7S_PRRR_NS0		BIT(18)
#define ARM_V7S_PRRR_NS1		BIT(19)
#define ARM_V7S_PRRR_NOS(n)		BIT((n) + 24)

#define ARM_V7S_NMRR_IR(n, attr)	(((attr) & 0x3) << ((n) * 2))
#define ARM_V7S_NMRR_OR(n, attr)	(((attr) & 0x3) << ((n) * 2 + 16))

#define ARM_V7S_TTBR_S			BIT(1)
#define ARM_V7S_TTBR_NOS		BIT(5)
#define ARM_V7S_TTBR_ORGN_ATTR(attr)	(((attr) & 0x3) << 3)
#define ARM_V7S_TTBR_IRGN_ATTR(attr)					\
	((((attr) & 0x1) << 6) | (((attr) & 0x2) >> 1))

#define ARM_V7S_TCR_PD1			BIT(5)

typedef u32 arm_v7s_iopte;

static bool selftest_running;

struct arm_v7s_io_pgtable {
	struct io_pgtable	iop;

	arm_v7s_iopte		*pgd;
	struct kmem_cache	*l2_tables;
	spinlock_t		split_lock;
};

static dma_addr_t __arm_v7s_dma_addr(void *pages)
{
	return (dma_addr_t)virt_to_phys(pages);
}

static arm_v7s_iopte *iopte_deref(arm_v7s_iopte pte, int lvl)
{
	if (ARM_V7S_PTE_IS_TABLE(pte, lvl))
		pte &= ARM_V7S_TABLE_MASK;
	else
		pte &= ARM_V7S_LVL_MASK(lvl);
	return phys_to_virt(pte);
}

static void *__arm_v7s_alloc_table(int lvl, gfp_t gfp,
				   struct arm_v7s_io_pgtable *data)
{
	struct io_pgtable_cfg *cfg = &data->iop.cfg;
	struct device *dev = cfg->iommu_dev;
	dma_addr_t dma;
	size_t size = ARM_V7S_TABLE_SIZE(lvl);
	void *table = NULL;

	if (lvl == 1)
		table = (void *)__get_dma_pages(__GFP_ZERO, get_order(size));
	else if (lvl == 2)
		table = kmem_cache_zalloc(data->l2_tables, gfp | GFP_DMA);
	if (table && !(cfg->quirks & IO_PGTABLE_QUIRK_NO_DMA)) {
		dma = dma_map_single(dev, table, size, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, dma))
			goto out_free;
		/*
		 * We depend on the IOMMU being able to work with any physical
		 * address directly, so if the DMA layer suggests otherwise by
		 * translating or truncating them, that bodes very badly...
		 */
		if (dma != virt_to_phys(table))
			goto out_unmap;
	}
	kmemleak_ignore(table);
	return table;

out_unmap:
	dev_err(dev, "Cannot accommodate DMA translation for IOMMU page tables\n");
	dma_unmap_single(dev, dma, size, DMA_TO_DEVICE);
out_free:
	if (lvl == 1)
		free_pages((unsigned long)table, get_order(size));
	else
		kmem_cache_free(data->l2_tables, table);
	return NULL;
}

static void __arm_v7s_free_table(void *table, int lvl,
				 struct arm_v7s_io_pgtable *data)
{
	struct io_pgtable_cfg *cfg = &data->iop.cfg;
	struct device *dev = cfg->iommu_dev;
	size_t size = ARM_V7S_TABLE_SIZE(lvl);

	if (!(cfg->quirks & IO_PGTABLE_QUIRK_NO_DMA))
		dma_unmap_single(dev, __arm_v7s_dma_addr(table), size,
				 DMA_TO_DEVICE);
	if (lvl == 1)
		free_pages((unsigned long)table, get_order(size));
	else
		kmem_cache_free(data->l2_tables, table);
}

static void __arm_v7s_pte_sync(arm_v7s_iopte *ptep, int num_entries,
			       struct io_pgtable_cfg *cfg)
{
	if (!(cfg->quirks & IO_PGTABLE_QUIRK_NO_DMA))
		return;

	dma_sync_single_for_device(cfg->iommu_dev, __arm_v7s_dma_addr(ptep),
				   num_entries * sizeof(*ptep), DMA_TO_DEVICE);
}
static void __arm_v7s_set_pte(arm_v7s_iopte *ptep, arm_v7s_iopte pte,
			      int num_entries, struct io_pgtable_cfg *cfg)
{
	int i;

	for (i = 0; i < num_entries; i++)
		ptep[i] = pte;

	__arm_v7s_pte_sync(ptep, num_entries, cfg);
}

static arm_v7s_iopte arm_v7s_prot_to_pte(int prot, int lvl,
					 struct io_pgtable_cfg *cfg)
{
	bool ap = !(cfg->quirks & IO_PGTABLE_QUIRK_NO_PERMS);
	arm_v7s_iopte pte = ARM_V7S_ATTR_NG | ARM_V7S_ATTR_S;

	if (!(prot & IOMMU_MMIO))
		pte |= ARM_V7S_ATTR_TEX(1);
	if (ap) {
		pte |= ARM_V7S_PTE_AF;
		if (!(prot & IOMMU_PRIV))
			pte |= ARM_V7S_PTE_AP_UNPRIV;
		if (!(prot & IOMMU_WRITE))
			pte |= ARM_V7S_PTE_AP_RDONLY;
	}
	pte <<= ARM_V7S_ATTR_SHIFT(lvl);

	if ((prot & IOMMU_NOEXEC) && ap)
		pte |= ARM_V7S_ATTR_XN(lvl);
	if (prot & IOMMU_MMIO)
		pte |= ARM_V7S_ATTR_B;
	else if (prot & IOMMU_CACHE)
		pte |= ARM_V7S_ATTR_B | ARM_V7S_ATTR_C;

	pte |= ARM_V7S_PTE_TYPE_PAGE;
	if (lvl == 1 && (cfg->quirks & IO_PGTABLE_QUIRK_ARM_NS))
		pte |= ARM_V7S_ATTR_NS_SECTION;

	if (cfg->quirks & IO_PGTABLE_QUIRK_ARM_MTK_4GB)
		pte |= ARM_V7S_ATTR_MTK_4GB;

	return pte;
}

static int arm_v7s_pte_to_prot(arm_v7s_iopte pte, int lvl)
{
	int prot = IOMMU_READ;
	arm_v7s_iopte attr = pte >> ARM_V7S_ATTR_SHIFT(lvl);

	if (!(attr & ARM_V7S_PTE_AP_RDONLY))
		prot |= IOMMU_WRITE;
	if (!(attr & ARM_V7S_PTE_AP_UNPRIV))
		prot |= IOMMU_PRIV;
	if ((attr & (ARM_V7S_TEX_MASK << ARM_V7S_TEX_SHIFT)) == 0)
		prot |= IOMMU_MMIO;
	else if (pte & ARM_V7S_ATTR_C)
		prot |= IOMMU_CACHE;
	if (pte & ARM_V7S_ATTR_XN(lvl))
		prot |= IOMMU_NOEXEC;

	return prot;
}

static arm_v7s_iopte arm_v7s_pte_to_cont(arm_v7s_iopte pte, int lvl)
{
	if (lvl == 1) {
		pte |= ARM_V7S_CONT_SECTION;
	} else if (lvl == 2) {
		arm_v7s_iopte xn = pte & ARM_V7S_ATTR_XN(lvl);
		arm_v7s_iopte tex = pte & ARM_V7S_CONT_PAGE_TEX_MASK;

		pte ^= xn | tex | ARM_V7S_PTE_TYPE_PAGE;
		pte |= (xn << ARM_V7S_CONT_PAGE_XN_SHIFT) |
		       (tex << ARM_V7S_CONT_PAGE_TEX_SHIFT) |
		       ARM_V7S_PTE_TYPE_CONT_PAGE;
	}
	return pte;
}

static arm_v7s_iopte arm_v7s_cont_to_pte(arm_v7s_iopte pte, int lvl)
{
	if (lvl == 1) {
		pte &= ~ARM_V7S_CONT_SECTION;
	} else if (lvl == 2) {
		arm_v7s_iopte xn = pte & BIT(ARM_V7S_CONT_PAGE_XN_SHIFT);
		arm_v7s_iopte tex = pte & (ARM_V7S_CONT_PAGE_TEX_MASK <<
					   ARM_V7S_CONT_PAGE_TEX_SHIFT);

		pte ^= xn | tex | ARM_V7S_PTE_TYPE_CONT_PAGE;
		pte |= (xn >> ARM_V7S_CONT_PAGE_XN_SHIFT) |
		       (tex >> ARM_V7S_CONT_PAGE_TEX_SHIFT) |
		       ARM_V7S_PTE_TYPE_PAGE;
	}
	return pte;
}

static bool arm_v7s_pte_is_cont(arm_v7s_iopte pte, int lvl)
{
	if (lvl == 1 && !ARM_V7S_PTE_IS_TABLE(pte, lvl))
		return pte & ARM_V7S_CONT_SECTION;
	else if (lvl == 2)
		return !(pte & ARM_V7S_PTE_TYPE_PAGE);
	return false;
}

static int __arm_v7s_unmap(struct arm_v7s_io_pgtable *, unsigned long,
			   size_t, int, arm_v7s_iopte *);

static int arm_v7s_init_pte(struct arm_v7s_io_pgtable *data,
			    unsigned long iova, phys_addr_t paddr, int prot,
			    int lvl, int num_entries, arm_v7s_iopte *ptep)
{
	struct io_pgtable_cfg *cfg = &data->iop.cfg;
	arm_v7s_iopte pte;
	int i;

	for (i = 0; i < num_entries; i++)
		if (ARM_V7S_PTE_IS_TABLE(ptep[i], lvl)) {
			/*
			 * We need to unmap and free the old table before
			 * overwriting it with a block entry.
			 */
			arm_v7s_iopte *tblp;
			size_t sz = ARM_V7S_BLOCK_SIZE(lvl);

			tblp = ptep - ARM_V7S_LVL_IDX(iova, lvl);
			if (WARN_ON(__arm_v7s_unmap(data, iova + i * sz,
						    sz, lvl, tblp) != sz))
				return -EINVAL;
		} else if (ptep[i]) {
			/* We require an unmap first */
			WARN_ON(!selftest_running);
			return -EEXIST;
		}

	pte = arm_v7s_prot_to_pte(prot, lvl, cfg);
	if (num_entries > 1)
		pte = arm_v7s_pte_to_cont(pte, lvl);

	pte |= paddr & ARM_V7S_LVL_MASK(lvl);

	__arm_v7s_set_pte(ptep, pte, num_entries, cfg);
	return 0;
}

static arm_v7s_iopte arm_v7s_install_table(arm_v7s_iopte *table,
					   arm_v7s_iopte *ptep,
					   arm_v7s_iopte curr,
					   struct io_pgtable_cfg *cfg)
{
	arm_v7s_iopte old, new;

	new = virt_to_phys(table) | ARM_V7S_PTE_TYPE_TABLE;
	if (cfg->quirks & IO_PGTABLE_QUIRK_ARM_NS)
		new |= ARM_V7S_ATTR_NS_TABLE;

	/*
	 * Ensure the table itself is visible before its PTE can be.
	 * Whilst we could get away with cmpxchg64_release below, this
	 * doesn't have any ordering semantics when !CONFIG_SMP.
	 */
	dma_wmb();

	old = cmpxchg_relaxed(ptep, curr, new);
	__arm_v7s_pte_sync(ptep, 1, cfg);

	return old;
}

static int __arm_v7s_map(struct arm_v7s_io_pgtable *data, unsigned long iova,
			 phys_addr_t paddr, size_t size, int prot,
			 int lvl, arm_v7s_iopte *ptep)
{
	struct io_pgtable_cfg *cfg = &data->iop.cfg;
	arm_v7s_iopte pte, *cptep;
	int num_entries = size >> ARM_V7S_LVL_SHIFT(lvl);

	/* Find our entry at the current level */
	ptep += ARM_V7S_LVL_IDX(iova, lvl);

	/* If we can install a leaf entry at this level, then do so */
	if (num_entries)
		return arm_v7s_init_pte(data, iova, paddr, prot,
					lvl, num_entries, ptep);

	/* We can't allocate tables at the final level */
	if (WARN_ON(lvl == 2))
		return -EINVAL;

	/* Grab a pointer to the next level */
	pte = READ_ONCE(*ptep);
	if (!pte) {
		cptep = __arm_v7s_alloc_table(lvl + 1, GFP_ATOMIC, data);
		if (!cptep)
			return -ENOMEM;

		pte = arm_v7s_install_table(cptep, ptep, 0, cfg);
		if (pte)
			__arm_v7s_free_table(cptep, lvl + 1, data);
	} else {
		/* We've no easy way of knowing if it's synced yet, so... */
		__arm_v7s_pte_sync(ptep, 1, cfg);
	}

	if (ARM_V7S_PTE_IS_TABLE(pte, lvl)) {
		cptep = iopte_deref(pte, lvl);
	} else if (pte) {
		/* We require an unmap first */
		WARN_ON(!selftest_running);
		return -EEXIST;
	}

	/* Rinse, repeat */
	return __arm_v7s_map(data, iova, paddr, size, prot, lvl + 1, cptep);
}

static int arm_v7s_map(struct io_pgtable_ops *ops, unsigned long iova,
			phys_addr_t paddr, size_t size, int prot)
{
	struct arm_v7s_io_pgtable *data = io_pgtable_ops_to_data(ops);
	struct io_pgtable *iop = &data->iop;
	int ret;

	/* If no access, then nothing to do */
	if (!(prot & (IOMMU_READ | IOMMU_WRITE)))
		return 0;

	ret = __arm_v7s_map(data, iova, paddr, size, prot, 1, data->pgd);
	/*
	 * Synchronise all PTE updates for the new mapping before there's
	 * a chance for anything to kick off a table walk for the new iova.
	 */
	if (iop->cfg.quirks & IO_PGTABLE_QUIRK_TLBI_ON_MAP) {
		io_pgtable_tlb_add_flush(iop, iova, size,
					 ARM_V7S_BLOCK_SIZE(2), false);
		io_pgtable_tlb_sync(iop);
	} else {
		wmb();
	}

	return ret;
}

static void arm_v7s_free_pgtable(struct io_pgtable *iop)
{
	struct arm_v7s_io_pgtable *data = io_pgtable_to_data(iop);
	int i;

	for (i = 0; i < ARM_V7S_PTES_PER_LVL(1); i++) {
		arm_v7s_iopte pte = data->pgd[i];

		if (ARM_V7S_PTE_IS_TABLE(pte, 1))
			__arm_v7s_free_table(iopte_deref(pte, 1), 2, data);
	}
	__arm_v7s_free_table(data->pgd, 1, data);
	kmem_cache_destroy(data->l2_tables);
	kfree(data);
}

static arm_v7s_iopte arm_v7s_split_cont(struct arm_v7s_io_pgtable *data,
					unsigned long iova, int idx, int lvl,
					arm_v7s_iopte *ptep)
{
	struct io_pgtable *iop = &data->iop;
	arm_v7s_iopte pte;
	size_t size = ARM_V7S_BLOCK_SIZE(lvl);
	int i;

	/* Check that we didn't lose a race to get the lock */
	pte = *ptep;
	if (!arm_v7s_pte_is_cont(pte, lvl))
		return pte;

	ptep -= idx & (ARM_V7S_CONT_PAGES - 1);
	pte = arm_v7s_cont_to_pte(pte, lvl);
	for (i = 0; i < ARM_V7S_CONT_PAGES; i++)
		ptep[i] = pte + i * size;

	__arm_v7s_pte_sync(ptep, ARM_V7S_CONT_PAGES, &iop->cfg);

	size *= ARM_V7S_CONT_PAGES;
	io_pgtable_tlb_add_flush(iop, iova, size, size, true);
	io_pgtable_tlb_sync(iop);
	return pte;
}

static int arm_v7s_split_blk_unmap(struct arm_v7s_io_pgtable *data,
				   unsigned long iova, size_t size,
				   arm_v7s_iopte blk_pte, arm_v7s_iopte *ptep)
{
	struct io_pgtable_cfg *cfg = &data->iop.cfg;
	arm_v7s_iopte pte, *tablep;
	int i, unmap_idx, num_entries, num_ptes;

	tablep = __arm_v7s_alloc_table(2, GFP_ATOMIC, data);
	if (!tablep)
		return 0; /* Bytes unmapped */

	num_ptes = ARM_V7S_PTES_PER_LVL(2);
	num_entries = size >> ARM_V7S_LVL_SHIFT(2);
	unmap_idx = ARM_V7S_LVL_IDX(iova, 2);

	pte = arm_v7s_prot_to_pte(arm_v7s_pte_to_prot(blk_pte, 1), 2, cfg);
	if (num_entries > 1)
		pte = arm_v7s_pte_to_cont(pte, 2);

	for (i = 0; i < num_ptes; i += num_entries, pte += size) {
		/* Unmap! */
		if (i == unmap_idx)
			continue;

		__arm_v7s_set_pte(&tablep[i], pte, num_entries, cfg);
	}

	pte = arm_v7s_install_table(tablep, ptep, blk_pte, cfg);
	if (pte != blk_pte) {
		__arm_v7s_free_table(tablep, 2, data);

		if (!ARM_V7S_PTE_IS_TABLE(pte, 1))
			return 0;

		tablep = iopte_deref(pte, 1);
		return __arm_v7s_unmap(data, iova, size, 2, tablep);
	}

	io_pgtable_tlb_add_flush(&data->iop, iova, size, size, true);
	return size;
}

static int __arm_v7s_unmap(struct arm_v7s_io_pgtable *data,
			    unsigned long iova, size_t size, int lvl,
			    arm_v7s_iopte *ptep)
{
	arm_v7s_iopte pte[ARM_V7S_CONT_PAGES];
	struct io_pgtable *iop = &data->iop;
	int idx, i = 0, num_entries = size >> ARM_V7S_LVL_SHIFT(lvl);

	/* Something went horribly wrong and we ran out of page table */
	if (WARN_ON(lvl > 2))
		return 0;

	idx = ARM_V7S_LVL_IDX(iova, lvl);
	ptep += idx;
	do {
		pte[i] = READ_ONCE(ptep[i]);
		if (WARN_ON(!ARM_V7S_PTE_IS_VALID(pte[i])))
			return 0;
	} while (++i < num_entries);

	/*
	 * If we've hit a contiguous 'large page' entry at this level, it
	 * needs splitting first, unless we're unmapping the whole lot.
	 *
	 * For splitting, we can't rewrite 16 PTEs atomically, and since we
	 * can't necessarily assume TEX remap we don't have a software bit to
	 * mark live entries being split. In practice (i.e. DMA API code), we
	 * will never be splitting large pages anyway, so just wrap this edge
	 * case in a lock for the sake of correctness and be done with it.
	 */
	if (num_entries <= 1 && arm_v7s_pte_is_cont(pte[0], lvl)) {
		unsigned long flags;

		spin_lock_irqsave(&data->split_lock, flags);
		pte[0] = arm_v7s_split_cont(data, iova, idx, lvl, ptep);
		spin_unlock_irqrestore(&data->split_lock, flags);
	}

	/* If the size matches this level, we're in the right place */
	if (num_entries) {
		size_t blk_size = ARM_V7S_BLOCK_SIZE(lvl);

		__arm_v7s_set_pte(ptep, 0, num_entries, &iop->cfg);

		for (i = 0; i < num_entries; i++) {
			if (ARM_V7S_PTE_IS_TABLE(pte[i], lvl)) {
				/* Also flush any partial walks */
				io_pgtable_tlb_add_flush(iop, iova, blk_size,
					ARM_V7S_BLOCK_SIZE(lvl + 1), false);
				io_pgtable_tlb_sync(iop);
				ptep = iopte_deref(pte[i], lvl);
				__arm_v7s_free_table(ptep, lvl + 1, data);
			} else {
				io_pgtable_tlb_add_flush(iop, iova, blk_size,
							 blk_size, true);
			}
			iova += blk_size;
		}
		return size;
	} else if (lvl == 1 && !ARM_V7S_PTE_IS_TABLE(pte[0], lvl)) {
		/*
		 * Insert a table at the next level to map the old region,
		 * minus the part we want to unmap
		 */
		return arm_v7s_split_blk_unmap(data, iova, size, pte[0], ptep);
	}

	/* Keep on walkin' */
	ptep = iopte_deref(pte[0], lvl);
	return __arm_v7s_unmap(data, iova, size, lvl + 1, ptep);
}

static int arm_v7s_unmap(struct io_pgtable_ops *ops, unsigned long iova,
			 size_t size)
{
	struct arm_v7s_io_pgtable *data = io_pgtable_ops_to_data(ops);
	size_t unmapped;

	unmapped = __arm_v7s_unmap(data, iova, size, 1, data->pgd);
	if (unmapped)
		io_pgtable_tlb_sync(&data->iop);

	return unmapped;
}

static phys_addr_t arm_v7s_iova_to_phys(struct io_pgtable_ops *ops,
					unsigned long iova)
{
	struct arm_v7s_io_pgtable *data = io_pgtable_ops_to_data(ops);
	arm_v7s_iopte *ptep = data->pgd, pte;
	int lvl = 0;
	u32 mask;

	do {
		ptep += ARM_V7S_LVL_IDX(iova, ++lvl);
		pte = READ_ONCE(*ptep);
		ptep = iopte_deref(pte, lvl);
	} while (ARM_V7S_PTE_IS_TABLE(pte, lvl));

	if (!ARM_V7S_PTE_IS_VALID(pte))
		return 0;

	mask = ARM_V7S_LVL_MASK(lvl);
	if (arm_v7s_pte_is_cont(pte, lvl))
		mask *= ARM_V7S_CONT_PAGES;
	return (pte & mask) | (iova & ~mask);
}

static struct io_pgtable *arm_v7s_alloc_pgtable(struct io_pgtable_cfg *cfg,
						void *cookie)
{
	struct arm_v7s_io_pgtable *data;

#ifdef PHYS_OFFSET
	if (upper_32_bits(PHYS_OFFSET))
		return NULL;
#endif
	if (cfg->ias > ARM_V7S_ADDR_BITS || cfg->oas > ARM_V7S_ADDR_BITS)
		return NULL;

	if (cfg->quirks & ~(IO_PGTABLE_QUIRK_ARM_NS |
			    IO_PGTABLE_QUIRK_NO_PERMS |
			    IO_PGTABLE_QUIRK_TLBI_ON_MAP |
			    IO_PGTABLE_QUIRK_ARM_MTK_4GB |
			    IO_PGTABLE_QUIRK_NO_DMA))
		return NULL;

	/* If ARM_MTK_4GB is enabled, the NO_PERMS is also expected. */
	if (cfg->quirks & IO_PGTABLE_QUIRK_ARM_MTK_4GB &&
	    !(cfg->quirks & IO_PGTABLE_QUIRK_NO_PERMS))
			return NULL;

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;

	spin_lock_init(&data->split_lock);
	data->l2_tables = kmem_cache_create("io-pgtable_armv7s_l2",
					    ARM_V7S_TABLE_SIZE(2),
					    ARM_V7S_TABLE_SIZE(2),
					    SLAB_CACHE_DMA, NULL);
	if (!data->l2_tables)
		goto out_free_data;

	data->iop.ops = (struct io_pgtable_ops) {
		.map		= arm_v7s_map,
		.unmap		= arm_v7s_unmap,
		.iova_to_phys	= arm_v7s_iova_to_phys,
	};

	/* We have to do this early for __arm_v7s_alloc_table to work... */
	data->iop.cfg = *cfg;

	/*
	 * Unless the IOMMU driver indicates supersection support by
	 * having SZ_16M set in the initial bitmap, they won't be used.
	 */
	cfg->pgsize_bitmap &= SZ_4K | SZ_64K | SZ_1M | SZ_16M;

	/* TCR: T0SZ=0, disable TTBR1 */
	cfg->arm_v7s_cfg.tcr = ARM_V7S_TCR_PD1;

	/*
	 * TEX remap: the indices used map to the closest equivalent types
	 * under the non-TEX-remap interpretation of those attribute bits,
	 * excepting various implementation-defined aspects of shareability.
	 */
	cfg->arm_v7s_cfg.prrr = ARM_V7S_PRRR_TR(1, ARM_V7S_PRRR_TYPE_DEVICE) |
				ARM_V7S_PRRR_TR(4, ARM_V7S_PRRR_TYPE_NORMAL) |
				ARM_V7S_PRRR_TR(7, ARM_V7S_PRRR_TYPE_NORMAL) |
				ARM_V7S_PRRR_DS0 | ARM_V7S_PRRR_DS1 |
				ARM_V7S_PRRR_NS1 | ARM_V7S_PRRR_NOS(7);
	cfg->arm_v7s_cfg.nmrr = ARM_V7S_NMRR_IR(7, ARM_V7S_RGN_WBWA) |
				ARM_V7S_NMRR_OR(7, ARM_V7S_RGN_WBWA);

	/* Looking good; allocate a pgd */
	data->pgd = __arm_v7s_alloc_table(1, GFP_KERNEL, data);
	if (!data->pgd)
		goto out_free_data;

	/* Ensure the empty pgd is visible before any actual TTBR write */
	wmb();

	/* TTBRs */
	cfg->arm_v7s_cfg.ttbr[0] = virt_to_phys(data->pgd) |
				   ARM_V7S_TTBR_S | ARM_V7S_TTBR_NOS |
				   ARM_V7S_TTBR_IRGN_ATTR(ARM_V7S_RGN_WBWA) |
				   ARM_V7S_TTBR_ORGN_ATTR(ARM_V7S_RGN_WBWA);
	cfg->arm_v7s_cfg.ttbr[1] = 0;
	return &data->iop;

out_free_data:
	kmem_cache_destroy(data->l2_tables);
	kfree(data);
	return NULL;
}

struct io_pgtable_init_fns io_pgtable_arm_v7s_init_fns = {
	.alloc	= arm_v7s_alloc_pgtable,
	.free	= arm_v7s_free_pgtable,
};

#ifdef CONFIG_IOMMU_IO_PGTABLE_ARMV7S_SELFTEST

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

static const struct iommu_gather_ops dummy_tlb_ops = {
	.tlb_flush_all	= dummy_tlb_flush_all,
	.tlb_add_flush	= dummy_tlb_add_flush,
	.tlb_sync	= dummy_tlb_sync,
};

#define __FAIL(ops)	({				\
		WARN(1, "selftest: test failed\n");	\
		selftest_running = false;		\
		-EFAULT;				\
})

static int __init arm_v7s_do_selftests(void)
{
	struct io_pgtable_ops *ops;
	struct io_pgtable_cfg cfg = {
		.tlb = &dummy_tlb_ops,
		.oas = 32,
		.ias = 32,
		.quirks = IO_PGTABLE_QUIRK_ARM_NS | IO_PGTABLE_QUIRK_NO_DMA,
		.pgsize_bitmap = SZ_4K | SZ_64K | SZ_1M | SZ_16M,
	};
	unsigned int iova, size, iova_start;
	unsigned int i, loopnr = 0;

	selftest_running = true;

	cfg_cookie = &cfg;

	ops = alloc_io_pgtable_ops(ARM_V7S, &cfg, &cfg);
	if (!ops) {
		pr_err("selftest: failed to allocate io pgtable ops\n");
		return -EINVAL;
	}

	/*
	 * Initial sanity checks.
	 * Empty page tables shouldn't provide any translations.
	 */
	if (ops->iova_to_phys(ops, 42))
		return __FAIL(ops);

	if (ops->iova_to_phys(ops, SZ_1G + 42))
		return __FAIL(ops);

	if (ops->iova_to_phys(ops, SZ_2G + 42))
		return __FAIL(ops);

	/*
	 * Distinct mappings of different granule sizes.
	 */
	iova = 0;
	for_each_set_bit(i, &cfg.pgsize_bitmap, BITS_PER_LONG) {
		size = 1UL << i;
		if (ops->map(ops, iova, iova, size, IOMMU_READ |
						    IOMMU_WRITE |
						    IOMMU_NOEXEC |
						    IOMMU_CACHE))
			return __FAIL(ops);

		/* Overlapping mappings */
		if (!ops->map(ops, iova, iova + size, size,
			      IOMMU_READ | IOMMU_NOEXEC))
			return __FAIL(ops);

		if (ops->iova_to_phys(ops, iova + 42) != (iova + 42))
			return __FAIL(ops);

		iova += SZ_16M;
		loopnr++;
	}

	/* Partial unmap */
	i = 1;
	size = 1UL << __ffs(cfg.pgsize_bitmap);
	while (i < loopnr) {
		iova_start = i * SZ_16M;
		if (ops->unmap(ops, iova_start + size, size) != size)
			return __FAIL(ops);

		/* Remap of partial unmap */
		if (ops->map(ops, iova_start + size, size, size, IOMMU_READ))
			return __FAIL(ops);

		if (ops->iova_to_phys(ops, iova_start + size + 42)
		    != (size + 42))
			return __FAIL(ops);
		i++;
	}

	/* Full unmap */
	iova = 0;
	i = find_first_bit(&cfg.pgsize_bitmap, BITS_PER_LONG);
	while (i != BITS_PER_LONG) {
		size = 1UL << i;

		if (ops->unmap(ops, iova, size) != size)
			return __FAIL(ops);

		if (ops->iova_to_phys(ops, iova + 42))
			return __FAIL(ops);

		/* Remap full block */
		if (ops->map(ops, iova, iova, size, IOMMU_WRITE))
			return __FAIL(ops);

		if (ops->iova_to_phys(ops, iova + 42) != (iova + 42))
			return __FAIL(ops);

		iova += SZ_16M;
		i++;
		i = find_next_bit(&cfg.pgsize_bitmap, BITS_PER_LONG, i);
	}

	free_io_pgtable_ops(ops);

	selftest_running = false;

	pr_info("self test ok\n");
	return 0;
}
subsys_initcall(arm_v7s_do_selftests);
#endif
