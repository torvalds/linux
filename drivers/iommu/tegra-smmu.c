/*
 * IOMMU API for SMMU in Tegra30
 *
 * Copyright (c) 2011-2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define pr_fmt(fmt)	"%s(): " fmt, __func__

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/iommu.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_iommu.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <asm/page.h>
#include <asm/cacheflush.h>

#include <mach/iomap.h>
#include <mach/tegra-ahb.h>

enum smmu_hwgrp {
	HWGRP_AFI,
	HWGRP_AVPC,
	HWGRP_DC,
	HWGRP_DCB,
	HWGRP_EPP,
	HWGRP_G2,
	HWGRP_HC,
	HWGRP_HDA,
	HWGRP_ISP,
	HWGRP_MPE,
	HWGRP_NV,
	HWGRP_NV2,
	HWGRP_PPCS,
	HWGRP_SATA,
	HWGRP_VDE,
	HWGRP_VI,

	HWGRP_COUNT,

	HWGRP_END = ~0,
};

#define HWG_AFI		(1 << HWGRP_AFI)
#define HWG_AVPC	(1 << HWGRP_AVPC)
#define HWG_DC		(1 << HWGRP_DC)
#define HWG_DCB		(1 << HWGRP_DCB)
#define HWG_EPP		(1 << HWGRP_EPP)
#define HWG_G2		(1 << HWGRP_G2)
#define HWG_HC		(1 << HWGRP_HC)
#define HWG_HDA		(1 << HWGRP_HDA)
#define HWG_ISP		(1 << HWGRP_ISP)
#define HWG_MPE		(1 << HWGRP_MPE)
#define HWG_NV		(1 << HWGRP_NV)
#define HWG_NV2		(1 << HWGRP_NV2)
#define HWG_PPCS	(1 << HWGRP_PPCS)
#define HWG_SATA	(1 << HWGRP_SATA)
#define HWG_VDE		(1 << HWGRP_VDE)
#define HWG_VI		(1 << HWGRP_VI)

/* bitmap of the page sizes currently supported */
#define SMMU_IOMMU_PGSIZES	(SZ_4K)

#define SMMU_CONFIG				0x10
#define SMMU_CONFIG_DISABLE			0
#define SMMU_CONFIG_ENABLE			1

/* REVISIT: To support multiple MCs */
enum {
	_MC = 0,
};

enum {
	_TLB = 0,
	_PTC,
};

#define SMMU_CACHE_CONFIG_BASE			0x14
#define __SMMU_CACHE_CONFIG(mc, cache)		(SMMU_CACHE_CONFIG_BASE + 4 * cache)
#define SMMU_CACHE_CONFIG(cache)		__SMMU_CACHE_CONFIG(_MC, cache)

#define SMMU_CACHE_CONFIG_STATS_SHIFT		31
#define SMMU_CACHE_CONFIG_STATS_ENABLE		(1 << SMMU_CACHE_CONFIG_STATS_SHIFT)
#define SMMU_CACHE_CONFIG_STATS_TEST_SHIFT	30
#define SMMU_CACHE_CONFIG_STATS_TEST		(1 << SMMU_CACHE_CONFIG_STATS_TEST_SHIFT)

#define SMMU_TLB_CONFIG_HIT_UNDER_MISS__ENABLE	(1 << 29)
#define SMMU_TLB_CONFIG_ACTIVE_LINES__VALUE	0x10
#define SMMU_TLB_CONFIG_RESET_VAL		0x20000010

#define SMMU_PTC_CONFIG_CACHE__ENABLE		(1 << 29)
#define SMMU_PTC_CONFIG_INDEX_MAP__PATTERN	0x3f
#define SMMU_PTC_CONFIG_RESET_VAL		0x2000003f

#define SMMU_PTB_ASID				0x1c
#define SMMU_PTB_ASID_CURRENT_SHIFT		0

#define SMMU_PTB_DATA				0x20
#define SMMU_PTB_DATA_RESET_VAL			0
#define SMMU_PTB_DATA_ASID_NONSECURE_SHIFT	29
#define SMMU_PTB_DATA_ASID_WRITABLE_SHIFT	30
#define SMMU_PTB_DATA_ASID_READABLE_SHIFT	31

#define SMMU_TLB_FLUSH				0x30
#define SMMU_TLB_FLUSH_VA_MATCH_ALL		0
#define SMMU_TLB_FLUSH_VA_MATCH_SECTION		2
#define SMMU_TLB_FLUSH_VA_MATCH_GROUP		3
#define SMMU_TLB_FLUSH_ASID_SHIFT		29
#define SMMU_TLB_FLUSH_ASID_MATCH_DISABLE	0
#define SMMU_TLB_FLUSH_ASID_MATCH_ENABLE	1
#define SMMU_TLB_FLUSH_ASID_MATCH_SHIFT		31

#define SMMU_PTC_FLUSH				0x34
#define SMMU_PTC_FLUSH_TYPE_ALL			0
#define SMMU_PTC_FLUSH_TYPE_ADR			1
#define SMMU_PTC_FLUSH_ADR_SHIFT		4

#define SMMU_ASID_SECURITY			0x38

#define SMMU_STATS_CACHE_COUNT_BASE		0x1f0

#define SMMU_STATS_CACHE_COUNT(mc, cache, hitmiss)		\
	(SMMU_STATS_CACHE_COUNT_BASE + 8 * cache + 4 * hitmiss)

#define SMMU_TRANSLATION_ENABLE_0		0x228
#define SMMU_TRANSLATION_ENABLE_1		0x22c
#define SMMU_TRANSLATION_ENABLE_2		0x230

#define SMMU_AFI_ASID	0x238   /* PCIE */
#define SMMU_AVPC_ASID	0x23c   /* AVP */
#define SMMU_DC_ASID	0x240   /* Display controller */
#define SMMU_DCB_ASID	0x244   /* Display controller B */
#define SMMU_EPP_ASID	0x248   /* Encoder pre-processor */
#define SMMU_G2_ASID	0x24c   /* 2D engine */
#define SMMU_HC_ASID	0x250   /* Host1x */
#define SMMU_HDA_ASID	0x254   /* High-def audio */
#define SMMU_ISP_ASID	0x258   /* Image signal processor */
#define SMMU_MPE_ASID	0x264   /* MPEG encoder */
#define SMMU_NV_ASID	0x268   /* (3D) */
#define SMMU_NV2_ASID	0x26c   /* (3D) */
#define SMMU_PPCS_ASID	0x270   /* AHB */
#define SMMU_SATA_ASID	0x278   /* SATA */
#define SMMU_VDE_ASID	0x27c   /* Video decoder */
#define SMMU_VI_ASID	0x280   /* Video input */

#define SMMU_PDE_NEXT_SHIFT		28

#define SMMU_TLB_FLUSH_VA_SECTION__MASK		0xffc00000
#define SMMU_TLB_FLUSH_VA_SECTION__SHIFT	12 /* right shift */
#define SMMU_TLB_FLUSH_VA_GROUP__MASK		0xffffc000
#define SMMU_TLB_FLUSH_VA_GROUP__SHIFT		12 /* right shift */
#define SMMU_TLB_FLUSH_VA(iova, which)	\
	((((iova) & SMMU_TLB_FLUSH_VA_##which##__MASK) >> \
		SMMU_TLB_FLUSH_VA_##which##__SHIFT) |	\
	SMMU_TLB_FLUSH_VA_MATCH_##which)
#define SMMU_PTB_ASID_CUR(n)	\
		((n) << SMMU_PTB_ASID_CURRENT_SHIFT)
#define SMMU_TLB_FLUSH_ASID_MATCH_disable		\
		(SMMU_TLB_FLUSH_ASID_MATCH_DISABLE <<	\
			SMMU_TLB_FLUSH_ASID_MATCH_SHIFT)
#define SMMU_TLB_FLUSH_ASID_MATCH__ENABLE		\
		(SMMU_TLB_FLUSH_ASID_MATCH_ENABLE <<	\
			SMMU_TLB_FLUSH_ASID_MATCH_SHIFT)

#define SMMU_PAGE_SHIFT 12
#define SMMU_PAGE_SIZE	(1 << SMMU_PAGE_SHIFT)
#define SMMU_PAGE_MASK	((1 << SMMU_PAGE_SHIFT) - 1)

#define SMMU_PDIR_COUNT	1024
#define SMMU_PDIR_SIZE	(sizeof(unsigned long) * SMMU_PDIR_COUNT)
#define SMMU_PTBL_COUNT	1024
#define SMMU_PTBL_SIZE	(sizeof(unsigned long) * SMMU_PTBL_COUNT)
#define SMMU_PDIR_SHIFT	12
#define SMMU_PDE_SHIFT	12
#define SMMU_PTE_SHIFT	12
#define SMMU_PFN_MASK	0x000fffff

#define SMMU_ADDR_TO_PFN(addr)	((addr) >> 12)
#define SMMU_ADDR_TO_PDN(addr)	((addr) >> 22)
#define SMMU_PDN_TO_ADDR(pdn)	((pdn) << 22)

#define _READABLE	(1 << SMMU_PTB_DATA_ASID_READABLE_SHIFT)
#define _WRITABLE	(1 << SMMU_PTB_DATA_ASID_WRITABLE_SHIFT)
#define _NONSECURE	(1 << SMMU_PTB_DATA_ASID_NONSECURE_SHIFT)
#define _PDE_NEXT	(1 << SMMU_PDE_NEXT_SHIFT)
#define _MASK_ATTR	(_READABLE | _WRITABLE | _NONSECURE)

#define _PDIR_ATTR	(_READABLE | _WRITABLE | _NONSECURE)

#define _PDE_ATTR	(_READABLE | _WRITABLE | _NONSECURE)
#define _PDE_ATTR_N	(_PDE_ATTR | _PDE_NEXT)
#define _PDE_VACANT(pdn)	(((pdn) << 10) | _PDE_ATTR)

#define _PTE_ATTR	(_READABLE | _WRITABLE | _NONSECURE)
#define _PTE_VACANT(addr)	(((addr) >> SMMU_PAGE_SHIFT) | _PTE_ATTR)

#define SMMU_MK_PDIR(page, attr)	\
		((page_to_phys(page) >> SMMU_PDIR_SHIFT) | (attr))
#define SMMU_MK_PDE(page, attr)		\
		(unsigned long)((page_to_phys(page) >> SMMU_PDE_SHIFT) | (attr))
#define SMMU_EX_PTBL_PAGE(pde)		\
		pfn_to_page((unsigned long)(pde) & SMMU_PFN_MASK)
#define SMMU_PFN_TO_PTE(pfn, attr)	(unsigned long)((pfn) | (attr))

#define SMMU_ASID_ENABLE(asid)	((asid) | (1 << 31))
#define SMMU_ASID_DISABLE	0
#define SMMU_ASID_ASID(n)	((n) & ~SMMU_ASID_ENABLE(0))

#define NUM_SMMU_REG_BANKS	3

#define smmu_client_enable_hwgrp(c, m)	smmu_client_set_hwgrp(c, m, 1)
#define smmu_client_disable_hwgrp(c)	smmu_client_set_hwgrp(c, 0, 0)
#define __smmu_client_enable_hwgrp(c, m) __smmu_client_set_hwgrp(c, m, 1)
#define __smmu_client_disable_hwgrp(c)	__smmu_client_set_hwgrp(c, 0, 0)

#define HWGRP_INIT(client) [HWGRP_##client] = SMMU_##client##_ASID

static const u32 smmu_hwgrp_asid_reg[] = {
	HWGRP_INIT(AFI),
	HWGRP_INIT(AVPC),
	HWGRP_INIT(DC),
	HWGRP_INIT(DCB),
	HWGRP_INIT(EPP),
	HWGRP_INIT(G2),
	HWGRP_INIT(HC),
	HWGRP_INIT(HDA),
	HWGRP_INIT(ISP),
	HWGRP_INIT(MPE),
	HWGRP_INIT(NV),
	HWGRP_INIT(NV2),
	HWGRP_INIT(PPCS),
	HWGRP_INIT(SATA),
	HWGRP_INIT(VDE),
	HWGRP_INIT(VI),
};
#define HWGRP_ASID_REG(x) (smmu_hwgrp_asid_reg[x])

/*
 * Per client for address space
 */
struct smmu_client {
	struct device		*dev;
	struct list_head	list;
	struct smmu_as		*as;
	u32			hwgrp;
};

/*
 * Per address space
 */
struct smmu_as {
	struct smmu_device	*smmu;	/* back pointer to container */
	unsigned int		asid;
	spinlock_t		lock;	/* for pagetable */
	struct page		*pdir_page;
	unsigned long		pdir_attr;
	unsigned long		pde_attr;
	unsigned long		pte_attr;
	unsigned int		*pte_count;

	struct list_head	client;
	spinlock_t		client_lock; /* for client list */
};

struct smmu_debugfs_info {
	struct smmu_device *smmu;
	int mc;
	int cache;
};

/*
 * Per SMMU device - IOMMU device
 */
struct smmu_device {
	void __iomem	*regs[NUM_SMMU_REG_BANKS];
	unsigned long	iovmm_base;	/* remappable base address */
	unsigned long	page_count;	/* total remappable size */
	spinlock_t	lock;
	char		*name;
	struct device	*dev;
	struct page *avp_vector_page;	/* dummy page shared by all AS's */

	/*
	 * Register image savers for suspend/resume
	 */
	unsigned long translation_enable_0;
	unsigned long translation_enable_1;
	unsigned long translation_enable_2;
	unsigned long asid_security;

	struct dentry *debugfs_root;
	struct smmu_debugfs_info *debugfs_info;

	struct device_node *ahb;

	int		num_as;
	struct smmu_as	as[0];		/* Run-time allocated array */
};

static struct smmu_device *smmu_handle; /* unique for a system */

/*
 *	SMMU register accessors
 */
static inline u32 smmu_read(struct smmu_device *smmu, size_t offs)
{
	BUG_ON(offs < 0x10);
	if (offs < 0x3c)
		return readl(smmu->regs[0] + offs - 0x10);
	BUG_ON(offs < 0x1f0);
	if (offs < 0x200)
		return readl(smmu->regs[1] + offs - 0x1f0);
	BUG_ON(offs < 0x228);
	if (offs < 0x284)
		return readl(smmu->regs[2] + offs - 0x228);
	BUG();
}

static inline void smmu_write(struct smmu_device *smmu, u32 val, size_t offs)
{
	BUG_ON(offs < 0x10);
	if (offs < 0x3c) {
		writel(val, smmu->regs[0] + offs - 0x10);
		return;
	}
	BUG_ON(offs < 0x1f0);
	if (offs < 0x200) {
		writel(val, smmu->regs[1] + offs - 0x1f0);
		return;
	}
	BUG_ON(offs < 0x228);
	if (offs < 0x284) {
		writel(val, smmu->regs[2] + offs - 0x228);
		return;
	}
	BUG();
}

#define VA_PAGE_TO_PA(va, page)	\
	(page_to_phys(page) + ((unsigned long)(va) & ~PAGE_MASK))

#define FLUSH_CPU_DCACHE(va, page, size)	\
	do {	\
		unsigned long _pa_ = VA_PAGE_TO_PA(va, page);		\
		__cpuc_flush_dcache_area((void *)(va), (size_t)(size));	\
		outer_flush_range(_pa_, _pa_+(size_t)(size));		\
	} while (0)

/*
 * Any interaction between any block on PPSB and a block on APB or AHB
 * must have these read-back barriers to ensure the APB/AHB bus
 * transaction is complete before initiating activity on the PPSB
 * block.
 */
#define FLUSH_SMMU_REGS(smmu)	smmu_read(smmu, SMMU_CONFIG)

#define smmu_client_hwgrp(c) (u32)((c)->dev->platform_data)

static int __smmu_client_set_hwgrp(struct smmu_client *c,
				   unsigned long map, int on)
{
	int i;
	struct smmu_as *as = c->as;
	u32 val, offs, mask = SMMU_ASID_ENABLE(as->asid);
	struct smmu_device *smmu = as->smmu;

	WARN_ON(!on && map);
	if (on && !map)
		return -EINVAL;
	if (!on)
		map = smmu_client_hwgrp(c);

	for_each_set_bit(i, &map, HWGRP_COUNT) {
		offs = HWGRP_ASID_REG(i);
		val = smmu_read(smmu, offs);
		if (on) {
			if (WARN_ON(val & mask))
				goto err_hw_busy;
			val |= mask;
		} else {
			WARN_ON((val & mask) == mask);
			val &= ~mask;
		}
		smmu_write(smmu, val, offs);
	}
	FLUSH_SMMU_REGS(smmu);
	c->hwgrp = map;
	return 0;

err_hw_busy:
	for_each_set_bit(i, &map, HWGRP_COUNT) {
		offs = HWGRP_ASID_REG(i);
		val = smmu_read(smmu, offs);
		val &= ~mask;
		smmu_write(smmu, val, offs);
	}
	return -EBUSY;
}

static int smmu_client_set_hwgrp(struct smmu_client *c, u32 map, int on)
{
	u32 val;
	unsigned long flags;
	struct smmu_as *as = c->as;
	struct smmu_device *smmu = as->smmu;

	spin_lock_irqsave(&smmu->lock, flags);
	val = __smmu_client_set_hwgrp(c, map, on);
	spin_unlock_irqrestore(&smmu->lock, flags);
	return val;
}

/*
 * Flush all TLB entries and all PTC entries
 * Caller must lock smmu
 */
static void smmu_flush_regs(struct smmu_device *smmu, int enable)
{
	u32 val;

	smmu_write(smmu, SMMU_PTC_FLUSH_TYPE_ALL, SMMU_PTC_FLUSH);
	FLUSH_SMMU_REGS(smmu);
	val = SMMU_TLB_FLUSH_VA_MATCH_ALL |
		SMMU_TLB_FLUSH_ASID_MATCH_disable;
	smmu_write(smmu, val, SMMU_TLB_FLUSH);

	if (enable)
		smmu_write(smmu, SMMU_CONFIG_ENABLE, SMMU_CONFIG);
	FLUSH_SMMU_REGS(smmu);
}

static int smmu_setup_regs(struct smmu_device *smmu)
{
	int i;
	u32 val;

	for (i = 0; i < smmu->num_as; i++) {
		struct smmu_as *as = &smmu->as[i];
		struct smmu_client *c;

		smmu_write(smmu, SMMU_PTB_ASID_CUR(as->asid), SMMU_PTB_ASID);
		val = as->pdir_page ?
			SMMU_MK_PDIR(as->pdir_page, as->pdir_attr) :
			SMMU_PTB_DATA_RESET_VAL;
		smmu_write(smmu, val, SMMU_PTB_DATA);

		list_for_each_entry(c, &as->client, list)
			__smmu_client_set_hwgrp(c, c->hwgrp, 1);
	}

	smmu_write(smmu, smmu->translation_enable_0, SMMU_TRANSLATION_ENABLE_0);
	smmu_write(smmu, smmu->translation_enable_1, SMMU_TRANSLATION_ENABLE_1);
	smmu_write(smmu, smmu->translation_enable_2, SMMU_TRANSLATION_ENABLE_2);
	smmu_write(smmu, smmu->asid_security, SMMU_ASID_SECURITY);
	smmu_write(smmu, SMMU_TLB_CONFIG_RESET_VAL, SMMU_CACHE_CONFIG(_TLB));
	smmu_write(smmu, SMMU_PTC_CONFIG_RESET_VAL, SMMU_CACHE_CONFIG(_PTC));

	smmu_flush_regs(smmu, 1);

	return tegra_ahb_enable_smmu(smmu->ahb);
}

static void flush_ptc_and_tlb(struct smmu_device *smmu,
		      struct smmu_as *as, dma_addr_t iova,
		      unsigned long *pte, struct page *page, int is_pde)
{
	u32 val;
	unsigned long tlb_flush_va = is_pde
		?  SMMU_TLB_FLUSH_VA(iova, SECTION)
		:  SMMU_TLB_FLUSH_VA(iova, GROUP);

	val = SMMU_PTC_FLUSH_TYPE_ADR | VA_PAGE_TO_PA(pte, page);
	smmu_write(smmu, val, SMMU_PTC_FLUSH);
	FLUSH_SMMU_REGS(smmu);
	val = tlb_flush_va |
		SMMU_TLB_FLUSH_ASID_MATCH__ENABLE |
		(as->asid << SMMU_TLB_FLUSH_ASID_SHIFT);
	smmu_write(smmu, val, SMMU_TLB_FLUSH);
	FLUSH_SMMU_REGS(smmu);
}

static void free_ptbl(struct smmu_as *as, dma_addr_t iova)
{
	unsigned long pdn = SMMU_ADDR_TO_PDN(iova);
	unsigned long *pdir = (unsigned long *)page_address(as->pdir_page);

	if (pdir[pdn] != _PDE_VACANT(pdn)) {
		dev_dbg(as->smmu->dev, "pdn: %lx\n", pdn);

		ClearPageReserved(SMMU_EX_PTBL_PAGE(pdir[pdn]));
		__free_page(SMMU_EX_PTBL_PAGE(pdir[pdn]));
		pdir[pdn] = _PDE_VACANT(pdn);
		FLUSH_CPU_DCACHE(&pdir[pdn], as->pdir_page, sizeof pdir[pdn]);
		flush_ptc_and_tlb(as->smmu, as, iova, &pdir[pdn],
				  as->pdir_page, 1);
	}
}

static void free_pdir(struct smmu_as *as)
{
	unsigned addr;
	int count;
	struct device *dev = as->smmu->dev;

	if (!as->pdir_page)
		return;

	addr = as->smmu->iovmm_base;
	count = as->smmu->page_count;
	while (count-- > 0) {
		free_ptbl(as, addr);
		addr += SMMU_PAGE_SIZE * SMMU_PTBL_COUNT;
	}
	ClearPageReserved(as->pdir_page);
	__free_page(as->pdir_page);
	as->pdir_page = NULL;
	devm_kfree(dev, as->pte_count);
	as->pte_count = NULL;
}

/*
 * Maps PTBL for given iova and returns the PTE address
 * Caller must unmap the mapped PTBL returned in *ptbl_page_p
 */
static unsigned long *locate_pte(struct smmu_as *as,
				 dma_addr_t iova, bool allocate,
				 struct page **ptbl_page_p,
				 unsigned int **count)
{
	unsigned long ptn = SMMU_ADDR_TO_PFN(iova);
	unsigned long pdn = SMMU_ADDR_TO_PDN(iova);
	unsigned long *pdir = page_address(as->pdir_page);
	unsigned long *ptbl;

	if (pdir[pdn] != _PDE_VACANT(pdn)) {
		/* Mapped entry table already exists */
		*ptbl_page_p = SMMU_EX_PTBL_PAGE(pdir[pdn]);
		ptbl = page_address(*ptbl_page_p);
	} else if (!allocate) {
		return NULL;
	} else {
		int pn;
		unsigned long addr = SMMU_PDN_TO_ADDR(pdn);

		/* Vacant - allocate a new page table */
		dev_dbg(as->smmu->dev, "New PTBL pdn: %lx\n", pdn);

		*ptbl_page_p = alloc_page(GFP_ATOMIC);
		if (!*ptbl_page_p) {
			dev_err(as->smmu->dev,
				"failed to allocate smmu_device page table\n");
			return NULL;
		}
		SetPageReserved(*ptbl_page_p);
		ptbl = (unsigned long *)page_address(*ptbl_page_p);
		for (pn = 0; pn < SMMU_PTBL_COUNT;
		     pn++, addr += SMMU_PAGE_SIZE) {
			ptbl[pn] = _PTE_VACANT(addr);
		}
		FLUSH_CPU_DCACHE(ptbl, *ptbl_page_p, SMMU_PTBL_SIZE);
		pdir[pdn] = SMMU_MK_PDE(*ptbl_page_p,
					as->pde_attr | _PDE_NEXT);
		FLUSH_CPU_DCACHE(&pdir[pdn], as->pdir_page, sizeof pdir[pdn]);
		flush_ptc_and_tlb(as->smmu, as, iova, &pdir[pdn],
				  as->pdir_page, 1);
	}
	*count = &as->pte_count[pdn];

	return &ptbl[ptn % SMMU_PTBL_COUNT];
}

#ifdef CONFIG_SMMU_SIG_DEBUG
static void put_signature(struct smmu_as *as,
			  dma_addr_t iova, unsigned long pfn)
{
	struct page *page;
	unsigned long *vaddr;

	page = pfn_to_page(pfn);
	vaddr = page_address(page);
	if (!vaddr)
		return;

	vaddr[0] = iova;
	vaddr[1] = pfn << PAGE_SHIFT;
	FLUSH_CPU_DCACHE(vaddr, page, sizeof(vaddr[0]) * 2);
}
#else
static inline void put_signature(struct smmu_as *as,
				 unsigned long addr, unsigned long pfn)
{
}
#endif

/*
 * Caller must not hold as->lock
 */
static int alloc_pdir(struct smmu_as *as)
{
	unsigned long *pdir, flags;
	int pdn, err = 0;
	u32 val;
	struct smmu_device *smmu = as->smmu;
	struct page *page;
	unsigned int *cnt;

	/*
	 * do the allocation, then grab as->lock
	 */
	cnt = devm_kzalloc(smmu->dev,
			   sizeof(cnt[0]) * SMMU_PDIR_COUNT,
			   GFP_KERNEL);
	page = alloc_page(GFP_KERNEL | __GFP_DMA);

	spin_lock_irqsave(&as->lock, flags);

	if (as->pdir_page) {
		/* We raced, free the redundant */
		err = -EAGAIN;
		goto err_out;
	}

	if (!page || !cnt) {
		dev_err(smmu->dev, "failed to allocate at %s\n", __func__);
		err = -ENOMEM;
		goto err_out;
	}

	as->pdir_page = page;
	as->pte_count = cnt;

	SetPageReserved(as->pdir_page);
	pdir = page_address(as->pdir_page);

	for (pdn = 0; pdn < SMMU_PDIR_COUNT; pdn++)
		pdir[pdn] = _PDE_VACANT(pdn);
	FLUSH_CPU_DCACHE(pdir, as->pdir_page, SMMU_PDIR_SIZE);
	val = SMMU_PTC_FLUSH_TYPE_ADR | VA_PAGE_TO_PA(pdir, as->pdir_page);
	smmu_write(smmu, val, SMMU_PTC_FLUSH);
	FLUSH_SMMU_REGS(as->smmu);
	val = SMMU_TLB_FLUSH_VA_MATCH_ALL |
		SMMU_TLB_FLUSH_ASID_MATCH__ENABLE |
		(as->asid << SMMU_TLB_FLUSH_ASID_SHIFT);
	smmu_write(smmu, val, SMMU_TLB_FLUSH);
	FLUSH_SMMU_REGS(as->smmu);

	spin_unlock_irqrestore(&as->lock, flags);

	return 0;

err_out:
	spin_unlock_irqrestore(&as->lock, flags);

	devm_kfree(smmu->dev, cnt);
	if (page)
		__free_page(page);
	return err;
}

static void __smmu_iommu_unmap(struct smmu_as *as, dma_addr_t iova)
{
	unsigned long *pte;
	struct page *page;
	unsigned int *count;

	pte = locate_pte(as, iova, false, &page, &count);
	if (WARN_ON(!pte))
		return;

	if (WARN_ON(*pte == _PTE_VACANT(iova)))
		return;

	*pte = _PTE_VACANT(iova);
	FLUSH_CPU_DCACHE(pte, page, sizeof(*pte));
	flush_ptc_and_tlb(as->smmu, as, iova, pte, page, 0);
	if (!--(*count))
		free_ptbl(as, iova);
}

static void __smmu_iommu_map_pfn(struct smmu_as *as, dma_addr_t iova,
				 unsigned long pfn)
{
	struct smmu_device *smmu = as->smmu;
	unsigned long *pte;
	unsigned int *count;
	struct page *page;

	pte = locate_pte(as, iova, true, &page, &count);
	if (WARN_ON(!pte))
		return;

	if (*pte == _PTE_VACANT(iova))
		(*count)++;
	*pte = SMMU_PFN_TO_PTE(pfn, as->pte_attr);
	if (unlikely((*pte == _PTE_VACANT(iova))))
		(*count)--;
	FLUSH_CPU_DCACHE(pte, page, sizeof(*pte));
	flush_ptc_and_tlb(smmu, as, iova, pte, page, 0);
	put_signature(as, iova, pfn);
}

static int smmu_iommu_map(struct iommu_domain *domain, unsigned long iova,
			  phys_addr_t pa, size_t bytes, int prot)
{
	struct smmu_as *as = domain->priv;
	unsigned long pfn = __phys_to_pfn(pa);
	unsigned long flags;

	dev_dbg(as->smmu->dev, "[%d] %08lx:%08x\n", as->asid, iova, pa);

	if (!pfn_valid(pfn))
		return -ENOMEM;

	spin_lock_irqsave(&as->lock, flags);
	__smmu_iommu_map_pfn(as, iova, pfn);
	spin_unlock_irqrestore(&as->lock, flags);
	return 0;
}

static size_t smmu_iommu_unmap(struct iommu_domain *domain, unsigned long iova,
			       size_t bytes)
{
	struct smmu_as *as = domain->priv;
	unsigned long flags;

	dev_dbg(as->smmu->dev, "[%d] %08lx\n", as->asid, iova);

	spin_lock_irqsave(&as->lock, flags);
	__smmu_iommu_unmap(as, iova);
	spin_unlock_irqrestore(&as->lock, flags);
	return SMMU_PAGE_SIZE;
}

static phys_addr_t smmu_iommu_iova_to_phys(struct iommu_domain *domain,
					   unsigned long iova)
{
	struct smmu_as *as = domain->priv;
	unsigned long *pte;
	unsigned int *count;
	struct page *page;
	unsigned long pfn;
	unsigned long flags;

	spin_lock_irqsave(&as->lock, flags);

	pte = locate_pte(as, iova, true, &page, &count);
	pfn = *pte & SMMU_PFN_MASK;
	WARN_ON(!pfn_valid(pfn));
	dev_dbg(as->smmu->dev,
		"iova:%08lx pfn:%08lx asid:%d\n", iova, pfn, as->asid);

	spin_unlock_irqrestore(&as->lock, flags);
	return PFN_PHYS(pfn);
}

static int smmu_iommu_domain_has_cap(struct iommu_domain *domain,
				     unsigned long cap)
{
	return 0;
}

static int smmu_iommu_attach_dev(struct iommu_domain *domain,
				 struct device *dev)
{
	struct smmu_as *as = domain->priv;
	struct smmu_device *smmu = as->smmu;
	struct smmu_client *client, *c;
	u32 map;
	int err;

	client = devm_kzalloc(smmu->dev, sizeof(*c), GFP_KERNEL);
	if (!client)
		return -ENOMEM;
	client->dev = dev;
	client->as = as;
	map = (unsigned long)dev->platform_data;
	if (!map)
		return -EINVAL;

	err = smmu_client_enable_hwgrp(client, map);
	if (err)
		goto err_hwgrp;

	spin_lock(&as->client_lock);
	list_for_each_entry(c, &as->client, list) {
		if (c->dev == dev) {
			dev_err(smmu->dev,
				"%s is already attached\n", dev_name(c->dev));
			err = -EINVAL;
			goto err_client;
		}
	}
	list_add(&client->list, &as->client);
	spin_unlock(&as->client_lock);

	/*
	 * Reserve "page zero" for AVP vectors using a common dummy
	 * page.
	 */
	if (map & HWG_AVPC) {
		struct page *page;

		page = as->smmu->avp_vector_page;
		__smmu_iommu_map_pfn(as, 0, page_to_pfn(page));

		pr_info("Reserve \"page zero\" for AVP vectors using a common dummy\n");
	}

	dev_dbg(smmu->dev, "%s is attached\n", dev_name(dev));
	return 0;

err_client:
	smmu_client_disable_hwgrp(client);
	spin_unlock(&as->client_lock);
err_hwgrp:
	devm_kfree(smmu->dev, client);
	return err;
}

static void smmu_iommu_detach_dev(struct iommu_domain *domain,
				  struct device *dev)
{
	struct smmu_as *as = domain->priv;
	struct smmu_device *smmu = as->smmu;
	struct smmu_client *c;

	spin_lock(&as->client_lock);

	list_for_each_entry(c, &as->client, list) {
		if (c->dev == dev) {
			smmu_client_disable_hwgrp(c);
			list_del(&c->list);
			devm_kfree(smmu->dev, c);
			c->as = NULL;
			dev_dbg(smmu->dev,
				"%s is detached\n", dev_name(c->dev));
			goto out;
		}
	}
	dev_err(smmu->dev, "Couldn't find %s\n", dev_name(dev));
out:
	spin_unlock(&as->client_lock);
}

static int smmu_iommu_domain_init(struct iommu_domain *domain)
{
	int i, err = -EAGAIN;
	unsigned long flags;
	struct smmu_as *as;
	struct smmu_device *smmu = smmu_handle;

	/* Look for a free AS with lock held */
	for  (i = 0; i < smmu->num_as; i++) {
		as = &smmu->as[i];

		if (as->pdir_page)
			continue;

		err = alloc_pdir(as);
		if (!err)
			goto found;

		if (err != -EAGAIN)
			break;
	}
	if (i == smmu->num_as)
		dev_err(smmu->dev,  "no free AS\n");
	return err;

found:
	spin_lock_irqsave(&smmu->lock, flags);

	/* Update PDIR register */
	smmu_write(smmu, SMMU_PTB_ASID_CUR(as->asid), SMMU_PTB_ASID);
	smmu_write(smmu,
		   SMMU_MK_PDIR(as->pdir_page, as->pdir_attr), SMMU_PTB_DATA);
	FLUSH_SMMU_REGS(smmu);

	spin_unlock_irqrestore(&smmu->lock, flags);

	domain->priv = as;

	domain->geometry.aperture_start = smmu->iovmm_base;
	domain->geometry.aperture_end   = smmu->iovmm_base +
		smmu->page_count * SMMU_PAGE_SIZE - 1;
	domain->geometry.force_aperture = true;

	dev_dbg(smmu->dev, "smmu_as@%p\n", as);

	return 0;
}

static void smmu_iommu_domain_destroy(struct iommu_domain *domain)
{
	struct smmu_as *as = domain->priv;
	struct smmu_device *smmu = as->smmu;
	unsigned long flags;

	spin_lock_irqsave(&as->lock, flags);

	if (as->pdir_page) {
		spin_lock(&smmu->lock);
		smmu_write(smmu, SMMU_PTB_ASID_CUR(as->asid), SMMU_PTB_ASID);
		smmu_write(smmu, SMMU_PTB_DATA_RESET_VAL, SMMU_PTB_DATA);
		FLUSH_SMMU_REGS(smmu);
		spin_unlock(&smmu->lock);

		free_pdir(as);
	}

	if (!list_empty(&as->client)) {
		struct smmu_client *c;

		list_for_each_entry(c, &as->client, list)
			smmu_iommu_detach_dev(domain, c->dev);
	}

	spin_unlock_irqrestore(&as->lock, flags);

	domain->priv = NULL;
	dev_dbg(smmu->dev, "smmu_as@%p\n", as);
}

static struct iommu_ops smmu_iommu_ops = {
	.domain_init	= smmu_iommu_domain_init,
	.domain_destroy	= smmu_iommu_domain_destroy,
	.attach_dev	= smmu_iommu_attach_dev,
	.detach_dev	= smmu_iommu_detach_dev,
	.map		= smmu_iommu_map,
	.unmap		= smmu_iommu_unmap,
	.iova_to_phys	= smmu_iommu_iova_to_phys,
	.domain_has_cap	= smmu_iommu_domain_has_cap,
	.pgsize_bitmap	= SMMU_IOMMU_PGSIZES,
};

/* Should be in the order of enum */
static const char * const smmu_debugfs_mc[] = { "mc", };
static const char * const smmu_debugfs_cache[] = {  "tlb", "ptc", };

static ssize_t smmu_debugfs_stats_write(struct file *file,
					const char __user *buffer,
					size_t count, loff_t *pos)
{
	struct smmu_debugfs_info *info;
	struct smmu_device *smmu;
	struct dentry *dent;
	int i;
	enum {
		_OFF = 0,
		_ON,
		_RESET,
	};
	const char * const command[] = {
		[_OFF]		= "off",
		[_ON]		= "on",
		[_RESET]	= "reset",
	};
	char str[] = "reset";
	u32 val;
	size_t offs;

	count = min_t(size_t, count, sizeof(str));
	if (copy_from_user(str, buffer, count))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(command); i++)
		if (strncmp(str, command[i],
			    strlen(command[i])) == 0)
			break;

	if (i == ARRAY_SIZE(command))
		return -EINVAL;

	dent = file->f_dentry;
	info = dent->d_inode->i_private;
	smmu = info->smmu;

	offs = SMMU_CACHE_CONFIG(info->cache);
	val = smmu_read(smmu, offs);
	switch (i) {
	case _OFF:
		val &= ~SMMU_CACHE_CONFIG_STATS_ENABLE;
		val &= ~SMMU_CACHE_CONFIG_STATS_TEST;
		smmu_write(smmu, val, offs);
		break;
	case _ON:
		val |= SMMU_CACHE_CONFIG_STATS_ENABLE;
		val &= ~SMMU_CACHE_CONFIG_STATS_TEST;
		smmu_write(smmu, val, offs);
		break;
	case _RESET:
		val |= SMMU_CACHE_CONFIG_STATS_TEST;
		smmu_write(smmu, val, offs);
		val &= ~SMMU_CACHE_CONFIG_STATS_TEST;
		smmu_write(smmu, val, offs);
		break;
	default:
		BUG();
		break;
	}

	dev_dbg(smmu->dev, "%s() %08x, %08x @%08x\n", __func__,
		val, smmu_read(smmu, offs), offs);

	return count;
}

static int smmu_debugfs_stats_show(struct seq_file *s, void *v)
{
	struct smmu_debugfs_info *info;
	struct smmu_device *smmu;
	struct dentry *dent;
	int i;
	const char * const stats[] = { "hit", "miss", };

	dent = d_find_alias(s->private);
	info = dent->d_inode->i_private;
	smmu = info->smmu;

	for (i = 0; i < ARRAY_SIZE(stats); i++) {
		u32 val;
		size_t offs;

		offs = SMMU_STATS_CACHE_COUNT(info->mc, info->cache, i);
		val = smmu_read(smmu, offs);
		seq_printf(s, "%s:%08x ", stats[i], val);

		dev_dbg(smmu->dev, "%s() %s %08x @%08x\n", __func__,
			stats[i], val, offs);
	}
	seq_printf(s, "\n");
	dput(dent);

	return 0;
}

static int smmu_debugfs_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, smmu_debugfs_stats_show, inode);
}

static const struct file_operations smmu_debugfs_stats_fops = {
	.open		= smmu_debugfs_stats_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
	.write		= smmu_debugfs_stats_write,
};

static void smmu_debugfs_delete(struct smmu_device *smmu)
{
	debugfs_remove_recursive(smmu->debugfs_root);
	kfree(smmu->debugfs_info);
}

static void smmu_debugfs_create(struct smmu_device *smmu)
{
	int i;
	size_t bytes;
	struct dentry *root;

	bytes = ARRAY_SIZE(smmu_debugfs_mc) * ARRAY_SIZE(smmu_debugfs_cache) *
		sizeof(*smmu->debugfs_info);
	smmu->debugfs_info = kmalloc(bytes, GFP_KERNEL);
	if (!smmu->debugfs_info)
		return;

	root = debugfs_create_dir(dev_name(smmu->dev), NULL);
	if (!root)
		goto err_out;
	smmu->debugfs_root = root;

	for (i = 0; i < ARRAY_SIZE(smmu_debugfs_mc); i++) {
		int j;
		struct dentry *mc;

		mc = debugfs_create_dir(smmu_debugfs_mc[i], root);
		if (!mc)
			goto err_out;

		for (j = 0; j < ARRAY_SIZE(smmu_debugfs_cache); j++) {
			struct dentry *cache;
			struct smmu_debugfs_info *info;

			info = smmu->debugfs_info;
			info += i * ARRAY_SIZE(smmu_debugfs_mc) + j;
			info->smmu = smmu;
			info->mc = i;
			info->cache = j;

			cache = debugfs_create_file(smmu_debugfs_cache[j],
						    S_IWUGO | S_IRUGO, mc,
						    (void *)info,
						    &smmu_debugfs_stats_fops);
			if (!cache)
				goto err_out;
		}
	}

	return;

err_out:
	smmu_debugfs_delete(smmu);
}

static int tegra_smmu_suspend(struct device *dev)
{
	struct smmu_device *smmu = dev_get_drvdata(dev);

	smmu->translation_enable_0 = smmu_read(smmu, SMMU_TRANSLATION_ENABLE_0);
	smmu->translation_enable_1 = smmu_read(smmu, SMMU_TRANSLATION_ENABLE_1);
	smmu->translation_enable_2 = smmu_read(smmu, SMMU_TRANSLATION_ENABLE_2);
	smmu->asid_security = smmu_read(smmu, SMMU_ASID_SECURITY);
	return 0;
}

static int tegra_smmu_resume(struct device *dev)
{
	struct smmu_device *smmu = dev_get_drvdata(dev);
	unsigned long flags;
	int err;

	spin_lock_irqsave(&smmu->lock, flags);
	err = smmu_setup_regs(smmu);
	spin_unlock_irqrestore(&smmu->lock, flags);
	return err;
}

static int tegra_smmu_probe(struct platform_device *pdev)
{
	struct smmu_device *smmu;
	struct device *dev = &pdev->dev;
	int i, asids, err = 0;
	dma_addr_t uninitialized_var(base);
	size_t bytes, uninitialized_var(size);

	if (smmu_handle)
		return -EIO;

	BUILD_BUG_ON(PAGE_SHIFT != SMMU_PAGE_SHIFT);

	if (of_property_read_u32(dev->of_node, "nvidia,#asids", &asids))
		return -ENODEV;

	bytes = sizeof(*smmu) + asids * sizeof(*smmu->as);
	smmu = devm_kzalloc(dev, bytes, GFP_KERNEL);
	if (!smmu) {
		dev_err(dev, "failed to allocate smmu_device\n");
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(smmu->regs); i++) {
		struct resource *res;

		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res)
			return -ENODEV;
		smmu->regs[i] = devm_request_and_ioremap(&pdev->dev, res);
		if (!smmu->regs[i])
			return -EBUSY;
	}

	err = of_get_dma_window(dev->of_node, NULL, 0, NULL, &base, &size);
	if (err)
		return -ENODEV;

	if (size & SMMU_PAGE_MASK)
		return -EINVAL;

	size >>= SMMU_PAGE_SHIFT;
	if (!size)
		return -EINVAL;

	smmu->ahb = of_parse_phandle(dev->of_node, "nvidia,ahb", 0);
	if (!smmu->ahb)
		return -ENODEV;

	smmu->dev = dev;
	smmu->num_as = asids;
	smmu->iovmm_base = base;
	smmu->page_count = size;

	smmu->translation_enable_0 = ~0;
	smmu->translation_enable_1 = ~0;
	smmu->translation_enable_2 = ~0;
	smmu->asid_security = 0;

	for (i = 0; i < smmu->num_as; i++) {
		struct smmu_as *as = &smmu->as[i];

		as->smmu = smmu;
		as->asid = i;
		as->pdir_attr = _PDIR_ATTR;
		as->pde_attr = _PDE_ATTR;
		as->pte_attr = _PTE_ATTR;

		spin_lock_init(&as->lock);
		INIT_LIST_HEAD(&as->client);
	}
	spin_lock_init(&smmu->lock);
	err = smmu_setup_regs(smmu);
	if (err)
		return err;
	platform_set_drvdata(pdev, smmu);

	smmu->avp_vector_page = alloc_page(GFP_KERNEL);
	if (!smmu->avp_vector_page)
		return -ENOMEM;

	smmu_debugfs_create(smmu);
	smmu_handle = smmu;
	return 0;
}

static int tegra_smmu_remove(struct platform_device *pdev)
{
	struct smmu_device *smmu = platform_get_drvdata(pdev);
	int i;

	smmu_debugfs_delete(smmu);

	smmu_write(smmu, SMMU_CONFIG_DISABLE, SMMU_CONFIG);
	for (i = 0; i < smmu->num_as; i++)
		free_pdir(&smmu->as[i]);
	__free_page(smmu->avp_vector_page);
	smmu_handle = NULL;
	return 0;
}

const struct dev_pm_ops tegra_smmu_pm_ops = {
	.suspend	= tegra_smmu_suspend,
	.resume		= tegra_smmu_resume,
};

#ifdef CONFIG_OF
static struct of_device_id tegra_smmu_of_match[] __devinitdata = {
	{ .compatible = "nvidia,tegra30-smmu", },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_smmu_of_match);
#endif

static struct platform_driver tegra_smmu_driver = {
	.probe		= tegra_smmu_probe,
	.remove		= tegra_smmu_remove,
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "tegra-smmu",
		.pm	= &tegra_smmu_pm_ops,
		.of_match_table = of_match_ptr(tegra_smmu_of_match),
	},
};

static int __devinit tegra_smmu_init(void)
{
	bus_set_iommu(&platform_bus_type, &smmu_iommu_ops);
	return platform_driver_register(&tegra_smmu_driver);
}

static void __exit tegra_smmu_exit(void)
{
	platform_driver_unregister(&tegra_smmu_driver);
}

subsys_initcall(tegra_smmu_init);
module_exit(tegra_smmu_exit);

MODULE_DESCRIPTION("IOMMU API for SMMU in Tegra30");
MODULE_AUTHOR("Hiroshi DOYU <hdoyu@nvidia.com>");
MODULE_ALIAS("platform:tegra-smmu");
MODULE_LICENSE("GPL v2");
