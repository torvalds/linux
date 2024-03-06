// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (C) 2016-2018, Allwinner Technology CO., LTD.
// Copyright (C) 2019-2020, Cerno

#include <linux/bitfield.h>
#include <linux/bug.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/ioport.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#define IOMMU_RESET_REG			0x010
#define IOMMU_RESET_RELEASE_ALL			0xffffffff
#define IOMMU_ENABLE_REG		0x020
#define IOMMU_ENABLE_ENABLE			BIT(0)

#define IOMMU_BYPASS_REG		0x030
#define IOMMU_AUTO_GATING_REG		0x040
#define IOMMU_AUTO_GATING_ENABLE		BIT(0)

#define IOMMU_WBUF_CTRL_REG		0x044
#define IOMMU_OOO_CTRL_REG		0x048
#define IOMMU_4KB_BDY_PRT_CTRL_REG	0x04c
#define IOMMU_TTB_REG			0x050
#define IOMMU_TLB_ENABLE_REG		0x060
#define IOMMU_TLB_PREFETCH_REG		0x070
#define IOMMU_TLB_PREFETCH_MASTER_ENABLE(m)	BIT(m)

#define IOMMU_TLB_FLUSH_REG		0x080
#define IOMMU_TLB_FLUSH_PTW_CACHE		BIT(17)
#define IOMMU_TLB_FLUSH_MACRO_TLB		BIT(16)
#define IOMMU_TLB_FLUSH_MICRO_TLB(i)		(BIT(i) & GENMASK(5, 0))

#define IOMMU_TLB_IVLD_ADDR_REG		0x090
#define IOMMU_TLB_IVLD_ADDR_MASK_REG	0x094
#define IOMMU_TLB_IVLD_ENABLE_REG	0x098
#define IOMMU_TLB_IVLD_ENABLE_ENABLE		BIT(0)

#define IOMMU_PC_IVLD_ADDR_REG		0x0a0
#define IOMMU_PC_IVLD_ENABLE_REG	0x0a8
#define IOMMU_PC_IVLD_ENABLE_ENABLE		BIT(0)

#define IOMMU_DM_AUT_CTRL_REG(d)	(0x0b0 + ((d) / 2) * 4)
#define IOMMU_DM_AUT_CTRL_RD_UNAVAIL(d, m)	(1 << (((d & 1) * 16) + ((m) * 2)))
#define IOMMU_DM_AUT_CTRL_WR_UNAVAIL(d, m)	(1 << (((d & 1) * 16) + ((m) * 2) + 1))

#define IOMMU_DM_AUT_OVWT_REG		0x0d0
#define IOMMU_INT_ENABLE_REG		0x100
#define IOMMU_INT_CLR_REG		0x104
#define IOMMU_INT_STA_REG		0x108
#define IOMMU_INT_ERR_ADDR_REG(i)	(0x110 + (i) * 4)
#define IOMMU_INT_ERR_ADDR_L1_REG	0x130
#define IOMMU_INT_ERR_ADDR_L2_REG	0x134
#define IOMMU_INT_ERR_DATA_REG(i)	(0x150 + (i) * 4)
#define IOMMU_L1PG_INT_REG		0x0180
#define IOMMU_L2PG_INT_REG		0x0184

#define IOMMU_INT_INVALID_L2PG			BIT(17)
#define IOMMU_INT_INVALID_L1PG			BIT(16)
#define IOMMU_INT_MASTER_PERMISSION(m)		BIT(m)
#define IOMMU_INT_MASTER_MASK			(IOMMU_INT_MASTER_PERMISSION(0) | \
						 IOMMU_INT_MASTER_PERMISSION(1) | \
						 IOMMU_INT_MASTER_PERMISSION(2) | \
						 IOMMU_INT_MASTER_PERMISSION(3) | \
						 IOMMU_INT_MASTER_PERMISSION(4) | \
						 IOMMU_INT_MASTER_PERMISSION(5))
#define IOMMU_INT_MASK				(IOMMU_INT_INVALID_L1PG | \
						 IOMMU_INT_INVALID_L2PG | \
						 IOMMU_INT_MASTER_MASK)

#define PT_ENTRY_SIZE			sizeof(u32)

#define NUM_DT_ENTRIES			4096
#define DT_SIZE				(NUM_DT_ENTRIES * PT_ENTRY_SIZE)

#define NUM_PT_ENTRIES			256
#define PT_SIZE				(NUM_PT_ENTRIES * PT_ENTRY_SIZE)

#define SPAGE_SIZE			4096

struct sun50i_iommu {
	struct iommu_device iommu;

	/* Lock to modify the IOMMU registers */
	spinlock_t iommu_lock;

	struct device *dev;
	void __iomem *base;
	struct reset_control *reset;
	struct clk *clk;

	struct iommu_domain *domain;
	struct kmem_cache *pt_pool;
};

struct sun50i_iommu_domain {
	struct iommu_domain domain;

	/* Number of devices attached to the domain */
	refcount_t refcnt;

	/* L1 Page Table */
	u32 *dt;
	dma_addr_t dt_dma;

	struct sun50i_iommu *iommu;
};

static struct sun50i_iommu_domain *to_sun50i_domain(struct iommu_domain *domain)
{
	return container_of(domain, struct sun50i_iommu_domain, domain);
}

static struct sun50i_iommu *sun50i_iommu_from_dev(struct device *dev)
{
	return dev_iommu_priv_get(dev);
}

static u32 iommu_read(struct sun50i_iommu *iommu, u32 offset)
{
	return readl(iommu->base + offset);
}

static void iommu_write(struct sun50i_iommu *iommu, u32 offset, u32 value)
{
	writel(value, iommu->base + offset);
}

/*
 * The Allwinner H6 IOMMU uses a 2-level page table.
 *
 * The first level is the usual Directory Table (DT), that consists of
 * 4096 4-bytes Directory Table Entries (DTE), each pointing to a Page
 * Table (PT).
 *
 * Each PT consits of 256 4-bytes Page Table Entries (PTE), each
 * pointing to a 4kB page of physical memory.
 *
 * The IOMMU supports a single DT, pointed by the IOMMU_TTB_REG
 * register that contains its physical address.
 */

#define SUN50I_IOVA_DTE_MASK	GENMASK(31, 20)
#define SUN50I_IOVA_PTE_MASK	GENMASK(19, 12)
#define SUN50I_IOVA_PAGE_MASK	GENMASK(11, 0)

static u32 sun50i_iova_get_dte_index(dma_addr_t iova)
{
	return FIELD_GET(SUN50I_IOVA_DTE_MASK, iova);
}

static u32 sun50i_iova_get_pte_index(dma_addr_t iova)
{
	return FIELD_GET(SUN50I_IOVA_PTE_MASK, iova);
}

static u32 sun50i_iova_get_page_offset(dma_addr_t iova)
{
	return FIELD_GET(SUN50I_IOVA_PAGE_MASK, iova);
}

/*
 * Each Directory Table Entry has a Page Table address and a valid
 * bit:

 * +---------------------+-----------+-+
 * | PT address          | Reserved  |V|
 * +---------------------+-----------+-+
 *  31:10 - Page Table address
 *   9:2  - Reserved
 *   1:0  - 1 if the entry is valid
 */

#define SUN50I_DTE_PT_ADDRESS_MASK	GENMASK(31, 10)
#define SUN50I_DTE_PT_ATTRS		GENMASK(1, 0)
#define SUN50I_DTE_PT_VALID		1

static phys_addr_t sun50i_dte_get_pt_address(u32 dte)
{
	return (phys_addr_t)dte & SUN50I_DTE_PT_ADDRESS_MASK;
}

static bool sun50i_dte_is_pt_valid(u32 dte)
{
	return (dte & SUN50I_DTE_PT_ATTRS) == SUN50I_DTE_PT_VALID;
}

static u32 sun50i_mk_dte(dma_addr_t pt_dma)
{
	return (pt_dma & SUN50I_DTE_PT_ADDRESS_MASK) | SUN50I_DTE_PT_VALID;
}

/*
 * Each PTE has a Page address, an authority index and a valid bit:
 *
 * +----------------+-----+-----+-----+---+-----+
 * | Page address   | Rsv | ACI | Rsv | V | Rsv |
 * +----------------+-----+-----+-----+---+-----+
 *  31:12 - Page address
 *  11:8  - Reserved
 *   7:4  - Authority Control Index
 *   3:2  - Reserved
 *     1  - 1 if the entry is valid
 *     0  - Reserved
 *
 * The way permissions work is that the IOMMU has 16 "domains" that
 * can be configured to give each masters either read or write
 * permissions through the IOMMU_DM_AUT_CTRL_REG registers. The domain
 * 0 seems like the default domain, and its permissions in the
 * IOMMU_DM_AUT_CTRL_REG are only read-only, so it's not really
 * useful to enforce any particular permission.
 *
 * Each page entry will then have a reference to the domain they are
 * affected to, so that we can actually enforce them on a per-page
 * basis.
 *
 * In order to make it work with the IOMMU framework, we will be using
 * 4 different domains, starting at 1: RD_WR, RD, WR and NONE
 * depending on the permission we want to enforce. Each domain will
 * have each master setup in the same way, since the IOMMU framework
 * doesn't seem to restrict page access on a per-device basis. And
 * then we will use the relevant domain index when generating the page
 * table entry depending on the permissions we want to be enforced.
 */

enum sun50i_iommu_aci {
	SUN50I_IOMMU_ACI_DO_NOT_USE = 0,
	SUN50I_IOMMU_ACI_NONE,
	SUN50I_IOMMU_ACI_RD,
	SUN50I_IOMMU_ACI_WR,
	SUN50I_IOMMU_ACI_RD_WR,
};

#define SUN50I_PTE_PAGE_ADDRESS_MASK	GENMASK(31, 12)
#define SUN50I_PTE_ACI_MASK		GENMASK(7, 4)
#define SUN50I_PTE_PAGE_VALID		BIT(1)

static phys_addr_t sun50i_pte_get_page_address(u32 pte)
{
	return (phys_addr_t)pte & SUN50I_PTE_PAGE_ADDRESS_MASK;
}

static enum sun50i_iommu_aci sun50i_get_pte_aci(u32 pte)
{
	return FIELD_GET(SUN50I_PTE_ACI_MASK, pte);
}

static bool sun50i_pte_is_page_valid(u32 pte)
{
	return pte & SUN50I_PTE_PAGE_VALID;
}

static u32 sun50i_mk_pte(phys_addr_t page, int prot)
{
	enum sun50i_iommu_aci aci;
	u32 flags = 0;

	if ((prot & (IOMMU_READ | IOMMU_WRITE)) == (IOMMU_READ | IOMMU_WRITE))
		aci = SUN50I_IOMMU_ACI_RD_WR;
	else if (prot & IOMMU_READ)
		aci = SUN50I_IOMMU_ACI_RD;
	else if (prot & IOMMU_WRITE)
		aci = SUN50I_IOMMU_ACI_WR;
	else
		aci = SUN50I_IOMMU_ACI_NONE;

	flags |= FIELD_PREP(SUN50I_PTE_ACI_MASK, aci);
	page &= SUN50I_PTE_PAGE_ADDRESS_MASK;
	return page | flags | SUN50I_PTE_PAGE_VALID;
}

static void sun50i_table_flush(struct sun50i_iommu_domain *sun50i_domain,
			       void *vaddr, unsigned int count)
{
	struct sun50i_iommu *iommu = sun50i_domain->iommu;
	dma_addr_t dma = virt_to_phys(vaddr);
	size_t size = count * PT_ENTRY_SIZE;

	dma_sync_single_for_device(iommu->dev, dma, size, DMA_TO_DEVICE);
}

static void sun50i_iommu_zap_iova(struct sun50i_iommu *iommu,
				  unsigned long iova)
{
	u32 reg;
	int ret;

	iommu_write(iommu, IOMMU_TLB_IVLD_ADDR_REG, iova);
	iommu_write(iommu, IOMMU_TLB_IVLD_ADDR_MASK_REG, GENMASK(31, 12));
	iommu_write(iommu, IOMMU_TLB_IVLD_ENABLE_REG,
		    IOMMU_TLB_IVLD_ENABLE_ENABLE);

	ret = readl_poll_timeout_atomic(iommu->base + IOMMU_TLB_IVLD_ENABLE_REG,
					reg, !reg, 1, 2000);
	if (ret)
		dev_warn(iommu->dev, "TLB invalidation timed out!\n");
}

static void sun50i_iommu_zap_ptw_cache(struct sun50i_iommu *iommu,
				       unsigned long iova)
{
	u32 reg;
	int ret;

	iommu_write(iommu, IOMMU_PC_IVLD_ADDR_REG, iova);
	iommu_write(iommu, IOMMU_PC_IVLD_ENABLE_REG,
		    IOMMU_PC_IVLD_ENABLE_ENABLE);

	ret = readl_poll_timeout_atomic(iommu->base + IOMMU_PC_IVLD_ENABLE_REG,
					reg, !reg, 1, 2000);
	if (ret)
		dev_warn(iommu->dev, "PTW cache invalidation timed out!\n");
}

static void sun50i_iommu_zap_range(struct sun50i_iommu *iommu,
				   unsigned long iova, size_t size)
{
	assert_spin_locked(&iommu->iommu_lock);

	iommu_write(iommu, IOMMU_AUTO_GATING_REG, 0);

	sun50i_iommu_zap_iova(iommu, iova);
	sun50i_iommu_zap_iova(iommu, iova + SPAGE_SIZE);
	if (size > SPAGE_SIZE) {
		sun50i_iommu_zap_iova(iommu, iova + size);
		sun50i_iommu_zap_iova(iommu, iova + size + SPAGE_SIZE);
	}
	sun50i_iommu_zap_ptw_cache(iommu, iova);
	sun50i_iommu_zap_ptw_cache(iommu, iova + SZ_1M);
	if (size > SZ_1M) {
		sun50i_iommu_zap_ptw_cache(iommu, iova + size);
		sun50i_iommu_zap_ptw_cache(iommu, iova + size + SZ_1M);
	}

	iommu_write(iommu, IOMMU_AUTO_GATING_REG, IOMMU_AUTO_GATING_ENABLE);
}

static int sun50i_iommu_flush_all_tlb(struct sun50i_iommu *iommu)
{
	u32 reg;
	int ret;

	assert_spin_locked(&iommu->iommu_lock);

	iommu_write(iommu,
		    IOMMU_TLB_FLUSH_REG,
		    IOMMU_TLB_FLUSH_PTW_CACHE |
		    IOMMU_TLB_FLUSH_MACRO_TLB |
		    IOMMU_TLB_FLUSH_MICRO_TLB(5) |
		    IOMMU_TLB_FLUSH_MICRO_TLB(4) |
		    IOMMU_TLB_FLUSH_MICRO_TLB(3) |
		    IOMMU_TLB_FLUSH_MICRO_TLB(2) |
		    IOMMU_TLB_FLUSH_MICRO_TLB(1) |
		    IOMMU_TLB_FLUSH_MICRO_TLB(0));

	ret = readl_poll_timeout_atomic(iommu->base + IOMMU_TLB_FLUSH_REG,
					reg, !reg,
					1, 2000);
	if (ret)
		dev_warn(iommu->dev, "TLB Flush timed out!\n");

	return ret;
}

static void sun50i_iommu_flush_iotlb_all(struct iommu_domain *domain)
{
	struct sun50i_iommu_domain *sun50i_domain = to_sun50i_domain(domain);
	struct sun50i_iommu *iommu = sun50i_domain->iommu;
	unsigned long flags;

	/*
	 * At boot, we'll have a first call into .flush_iotlb_all right after
	 * .probe_device, and since we link our (single) domain to our iommu in
	 * the .attach_device callback, we don't have that pointer set.
	 *
	 * It shouldn't really be any trouble to ignore it though since we flush
	 * all caches as part of the device powerup.
	 */
	if (!iommu)
		return;

	spin_lock_irqsave(&iommu->iommu_lock, flags);
	sun50i_iommu_flush_all_tlb(iommu);
	spin_unlock_irqrestore(&iommu->iommu_lock, flags);
}

static int sun50i_iommu_iotlb_sync_map(struct iommu_domain *domain,
				       unsigned long iova, size_t size)
{
	struct sun50i_iommu_domain *sun50i_domain = to_sun50i_domain(domain);
	struct sun50i_iommu *iommu = sun50i_domain->iommu;
	unsigned long flags;

	spin_lock_irqsave(&iommu->iommu_lock, flags);
	sun50i_iommu_zap_range(iommu, iova, size);
	spin_unlock_irqrestore(&iommu->iommu_lock, flags);

	return 0;
}

static void sun50i_iommu_iotlb_sync(struct iommu_domain *domain,
				    struct iommu_iotlb_gather *gather)
{
	sun50i_iommu_flush_iotlb_all(domain);
}

static int sun50i_iommu_enable(struct sun50i_iommu *iommu)
{
	struct sun50i_iommu_domain *sun50i_domain;
	unsigned long flags;
	int ret;

	if (!iommu->domain)
		return 0;

	sun50i_domain = to_sun50i_domain(iommu->domain);

	ret = reset_control_deassert(iommu->reset);
	if (ret)
		return ret;

	ret = clk_prepare_enable(iommu->clk);
	if (ret)
		goto err_reset_assert;

	spin_lock_irqsave(&iommu->iommu_lock, flags);

	iommu_write(iommu, IOMMU_TTB_REG, sun50i_domain->dt_dma);
	iommu_write(iommu, IOMMU_TLB_PREFETCH_REG,
		    IOMMU_TLB_PREFETCH_MASTER_ENABLE(0) |
		    IOMMU_TLB_PREFETCH_MASTER_ENABLE(1) |
		    IOMMU_TLB_PREFETCH_MASTER_ENABLE(2) |
		    IOMMU_TLB_PREFETCH_MASTER_ENABLE(3) |
		    IOMMU_TLB_PREFETCH_MASTER_ENABLE(4) |
		    IOMMU_TLB_PREFETCH_MASTER_ENABLE(5));
	iommu_write(iommu, IOMMU_INT_ENABLE_REG, IOMMU_INT_MASK);
	iommu_write(iommu, IOMMU_DM_AUT_CTRL_REG(SUN50I_IOMMU_ACI_NONE),
		    IOMMU_DM_AUT_CTRL_RD_UNAVAIL(SUN50I_IOMMU_ACI_NONE, 0) |
		    IOMMU_DM_AUT_CTRL_WR_UNAVAIL(SUN50I_IOMMU_ACI_NONE, 0) |
		    IOMMU_DM_AUT_CTRL_RD_UNAVAIL(SUN50I_IOMMU_ACI_NONE, 1) |
		    IOMMU_DM_AUT_CTRL_WR_UNAVAIL(SUN50I_IOMMU_ACI_NONE, 1) |
		    IOMMU_DM_AUT_CTRL_RD_UNAVAIL(SUN50I_IOMMU_ACI_NONE, 2) |
		    IOMMU_DM_AUT_CTRL_WR_UNAVAIL(SUN50I_IOMMU_ACI_NONE, 2) |
		    IOMMU_DM_AUT_CTRL_RD_UNAVAIL(SUN50I_IOMMU_ACI_NONE, 3) |
		    IOMMU_DM_AUT_CTRL_WR_UNAVAIL(SUN50I_IOMMU_ACI_NONE, 3) |
		    IOMMU_DM_AUT_CTRL_RD_UNAVAIL(SUN50I_IOMMU_ACI_NONE, 4) |
		    IOMMU_DM_AUT_CTRL_WR_UNAVAIL(SUN50I_IOMMU_ACI_NONE, 4) |
		    IOMMU_DM_AUT_CTRL_RD_UNAVAIL(SUN50I_IOMMU_ACI_NONE, 5) |
		    IOMMU_DM_AUT_CTRL_WR_UNAVAIL(SUN50I_IOMMU_ACI_NONE, 5));

	iommu_write(iommu, IOMMU_DM_AUT_CTRL_REG(SUN50I_IOMMU_ACI_RD),
		    IOMMU_DM_AUT_CTRL_WR_UNAVAIL(SUN50I_IOMMU_ACI_RD, 0) |
		    IOMMU_DM_AUT_CTRL_WR_UNAVAIL(SUN50I_IOMMU_ACI_RD, 1) |
		    IOMMU_DM_AUT_CTRL_WR_UNAVAIL(SUN50I_IOMMU_ACI_RD, 2) |
		    IOMMU_DM_AUT_CTRL_WR_UNAVAIL(SUN50I_IOMMU_ACI_RD, 3) |
		    IOMMU_DM_AUT_CTRL_WR_UNAVAIL(SUN50I_IOMMU_ACI_RD, 4) |
		    IOMMU_DM_AUT_CTRL_WR_UNAVAIL(SUN50I_IOMMU_ACI_RD, 5));

	iommu_write(iommu, IOMMU_DM_AUT_CTRL_REG(SUN50I_IOMMU_ACI_WR),
		    IOMMU_DM_AUT_CTRL_RD_UNAVAIL(SUN50I_IOMMU_ACI_WR, 0) |
		    IOMMU_DM_AUT_CTRL_RD_UNAVAIL(SUN50I_IOMMU_ACI_WR, 1) |
		    IOMMU_DM_AUT_CTRL_RD_UNAVAIL(SUN50I_IOMMU_ACI_WR, 2) |
		    IOMMU_DM_AUT_CTRL_RD_UNAVAIL(SUN50I_IOMMU_ACI_WR, 3) |
		    IOMMU_DM_AUT_CTRL_RD_UNAVAIL(SUN50I_IOMMU_ACI_WR, 4) |
		    IOMMU_DM_AUT_CTRL_RD_UNAVAIL(SUN50I_IOMMU_ACI_WR, 5));

	ret = sun50i_iommu_flush_all_tlb(iommu);
	if (ret) {
		spin_unlock_irqrestore(&iommu->iommu_lock, flags);
		goto err_clk_disable;
	}

	iommu_write(iommu, IOMMU_AUTO_GATING_REG, IOMMU_AUTO_GATING_ENABLE);
	iommu_write(iommu, IOMMU_ENABLE_REG, IOMMU_ENABLE_ENABLE);

	spin_unlock_irqrestore(&iommu->iommu_lock, flags);

	return 0;

err_clk_disable:
	clk_disable_unprepare(iommu->clk);

err_reset_assert:
	reset_control_assert(iommu->reset);

	return ret;
}

static void sun50i_iommu_disable(struct sun50i_iommu *iommu)
{
	unsigned long flags;

	spin_lock_irqsave(&iommu->iommu_lock, flags);

	iommu_write(iommu, IOMMU_ENABLE_REG, 0);
	iommu_write(iommu, IOMMU_TTB_REG, 0);

	spin_unlock_irqrestore(&iommu->iommu_lock, flags);

	clk_disable_unprepare(iommu->clk);
	reset_control_assert(iommu->reset);
}

static void *sun50i_iommu_alloc_page_table(struct sun50i_iommu *iommu,
					   gfp_t gfp)
{
	dma_addr_t pt_dma;
	u32 *page_table;

	page_table = kmem_cache_zalloc(iommu->pt_pool, gfp);
	if (!page_table)
		return ERR_PTR(-ENOMEM);

	pt_dma = dma_map_single(iommu->dev, page_table, PT_SIZE, DMA_TO_DEVICE);
	if (dma_mapping_error(iommu->dev, pt_dma)) {
		dev_err(iommu->dev, "Couldn't map L2 Page Table\n");
		kmem_cache_free(iommu->pt_pool, page_table);
		return ERR_PTR(-ENOMEM);
	}

	/* We rely on the physical address and DMA address being the same */
	WARN_ON(pt_dma != virt_to_phys(page_table));

	return page_table;
}

static void sun50i_iommu_free_page_table(struct sun50i_iommu *iommu,
					 u32 *page_table)
{
	phys_addr_t pt_phys = virt_to_phys(page_table);

	dma_unmap_single(iommu->dev, pt_phys, PT_SIZE, DMA_TO_DEVICE);
	kmem_cache_free(iommu->pt_pool, page_table);
}

static u32 *sun50i_dte_get_page_table(struct sun50i_iommu_domain *sun50i_domain,
				      dma_addr_t iova, gfp_t gfp)
{
	struct sun50i_iommu *iommu = sun50i_domain->iommu;
	u32 *page_table;
	u32 *dte_addr;
	u32 old_dte;
	u32 dte;

	dte_addr = &sun50i_domain->dt[sun50i_iova_get_dte_index(iova)];
	dte = *dte_addr;
	if (sun50i_dte_is_pt_valid(dte)) {
		phys_addr_t pt_phys = sun50i_dte_get_pt_address(dte);
		return (u32 *)phys_to_virt(pt_phys);
	}

	page_table = sun50i_iommu_alloc_page_table(iommu, gfp);
	if (IS_ERR(page_table))
		return page_table;

	dte = sun50i_mk_dte(virt_to_phys(page_table));
	old_dte = cmpxchg(dte_addr, 0, dte);
	if (old_dte) {
		phys_addr_t installed_pt_phys =
			sun50i_dte_get_pt_address(old_dte);
		u32 *installed_pt = phys_to_virt(installed_pt_phys);
		u32 *drop_pt = page_table;

		page_table = installed_pt;
		dte = old_dte;
		sun50i_iommu_free_page_table(iommu, drop_pt);
	}

	sun50i_table_flush(sun50i_domain, page_table, NUM_PT_ENTRIES);
	sun50i_table_flush(sun50i_domain, dte_addr, 1);

	return page_table;
}

static int sun50i_iommu_map(struct iommu_domain *domain, unsigned long iova,
			    phys_addr_t paddr, size_t size, size_t count,
			    int prot, gfp_t gfp, size_t *mapped)
{
	struct sun50i_iommu_domain *sun50i_domain = to_sun50i_domain(domain);
	struct sun50i_iommu *iommu = sun50i_domain->iommu;
	u32 pte_index;
	u32 *page_table, *pte_addr;
	int ret = 0;

	page_table = sun50i_dte_get_page_table(sun50i_domain, iova, gfp);
	if (IS_ERR(page_table)) {
		ret = PTR_ERR(page_table);
		goto out;
	}

	pte_index = sun50i_iova_get_pte_index(iova);
	pte_addr = &page_table[pte_index];
	if (unlikely(sun50i_pte_is_page_valid(*pte_addr))) {
		phys_addr_t page_phys = sun50i_pte_get_page_address(*pte_addr);
		dev_err(iommu->dev,
			"iova %pad already mapped to %pa cannot remap to %pa prot: %#x\n",
			&iova, &page_phys, &paddr, prot);
		ret = -EBUSY;
		goto out;
	}

	*pte_addr = sun50i_mk_pte(paddr, prot);
	sun50i_table_flush(sun50i_domain, pte_addr, 1);
	*mapped = size;

out:
	return ret;
}

static size_t sun50i_iommu_unmap(struct iommu_domain *domain, unsigned long iova,
				 size_t size, size_t count, struct iommu_iotlb_gather *gather)
{
	struct sun50i_iommu_domain *sun50i_domain = to_sun50i_domain(domain);
	phys_addr_t pt_phys;
	u32 *pte_addr;
	u32 dte;

	dte = sun50i_domain->dt[sun50i_iova_get_dte_index(iova)];
	if (!sun50i_dte_is_pt_valid(dte))
		return 0;

	pt_phys = sun50i_dte_get_pt_address(dte);
	pte_addr = (u32 *)phys_to_virt(pt_phys) + sun50i_iova_get_pte_index(iova);

	if (!sun50i_pte_is_page_valid(*pte_addr))
		return 0;

	memset(pte_addr, 0, sizeof(*pte_addr));
	sun50i_table_flush(sun50i_domain, pte_addr, 1);

	return SZ_4K;
}

static phys_addr_t sun50i_iommu_iova_to_phys(struct iommu_domain *domain,
					     dma_addr_t iova)
{
	struct sun50i_iommu_domain *sun50i_domain = to_sun50i_domain(domain);
	phys_addr_t pt_phys;
	u32 *page_table;
	u32 dte, pte;

	dte = sun50i_domain->dt[sun50i_iova_get_dte_index(iova)];
	if (!sun50i_dte_is_pt_valid(dte))
		return 0;

	pt_phys = sun50i_dte_get_pt_address(dte);
	page_table = (u32 *)phys_to_virt(pt_phys);
	pte = page_table[sun50i_iova_get_pte_index(iova)];
	if (!sun50i_pte_is_page_valid(pte))
		return 0;

	return sun50i_pte_get_page_address(pte) +
		sun50i_iova_get_page_offset(iova);
}

static struct iommu_domain *
sun50i_iommu_domain_alloc_paging(struct device *dev)
{
	struct sun50i_iommu_domain *sun50i_domain;

	sun50i_domain = kzalloc(sizeof(*sun50i_domain), GFP_KERNEL);
	if (!sun50i_domain)
		return NULL;

	sun50i_domain->dt = (u32 *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
						    get_order(DT_SIZE));
	if (!sun50i_domain->dt)
		goto err_free_domain;

	refcount_set(&sun50i_domain->refcnt, 1);

	sun50i_domain->domain.geometry.aperture_start = 0;
	sun50i_domain->domain.geometry.aperture_end = DMA_BIT_MASK(32);
	sun50i_domain->domain.geometry.force_aperture = true;

	return &sun50i_domain->domain;

err_free_domain:
	kfree(sun50i_domain);

	return NULL;
}

static void sun50i_iommu_domain_free(struct iommu_domain *domain)
{
	struct sun50i_iommu_domain *sun50i_domain = to_sun50i_domain(domain);

	free_pages((unsigned long)sun50i_domain->dt, get_order(DT_SIZE));
	sun50i_domain->dt = NULL;

	kfree(sun50i_domain);
}

static int sun50i_iommu_attach_domain(struct sun50i_iommu *iommu,
				      struct sun50i_iommu_domain *sun50i_domain)
{
	iommu->domain = &sun50i_domain->domain;
	sun50i_domain->iommu = iommu;

	sun50i_domain->dt_dma = dma_map_single(iommu->dev, sun50i_domain->dt,
					       DT_SIZE, DMA_TO_DEVICE);
	if (dma_mapping_error(iommu->dev, sun50i_domain->dt_dma)) {
		dev_err(iommu->dev, "Couldn't map L1 Page Table\n");
		return -ENOMEM;
	}

	return sun50i_iommu_enable(iommu);
}

static void sun50i_iommu_detach_domain(struct sun50i_iommu *iommu,
				       struct sun50i_iommu_domain *sun50i_domain)
{
	unsigned int i;

	for (i = 0; i < NUM_DT_ENTRIES; i++) {
		phys_addr_t pt_phys;
		u32 *page_table;
		u32 *dte_addr;
		u32 dte;

		dte_addr = &sun50i_domain->dt[i];
		dte = *dte_addr;
		if (!sun50i_dte_is_pt_valid(dte))
			continue;

		memset(dte_addr, 0, sizeof(*dte_addr));
		sun50i_table_flush(sun50i_domain, dte_addr, 1);

		pt_phys = sun50i_dte_get_pt_address(dte);
		page_table = phys_to_virt(pt_phys);
		sun50i_iommu_free_page_table(iommu, page_table);
	}


	sun50i_iommu_disable(iommu);

	dma_unmap_single(iommu->dev, virt_to_phys(sun50i_domain->dt),
			 DT_SIZE, DMA_TO_DEVICE);

	iommu->domain = NULL;
}

static int sun50i_iommu_identity_attach(struct iommu_domain *identity_domain,
					struct device *dev)
{
	struct sun50i_iommu *iommu = dev_iommu_priv_get(dev);
	struct sun50i_iommu_domain *sun50i_domain;

	dev_dbg(dev, "Detaching from IOMMU domain\n");

	if (iommu->domain == identity_domain)
		return 0;

	sun50i_domain = to_sun50i_domain(iommu->domain);
	if (refcount_dec_and_test(&sun50i_domain->refcnt))
		sun50i_iommu_detach_domain(iommu, sun50i_domain);
	return 0;
}

static struct iommu_domain_ops sun50i_iommu_identity_ops = {
	.attach_dev = sun50i_iommu_identity_attach,
};

static struct iommu_domain sun50i_iommu_identity_domain = {
	.type = IOMMU_DOMAIN_IDENTITY,
	.ops = &sun50i_iommu_identity_ops,
};

static int sun50i_iommu_attach_device(struct iommu_domain *domain,
				      struct device *dev)
{
	struct sun50i_iommu_domain *sun50i_domain = to_sun50i_domain(domain);
	struct sun50i_iommu *iommu;

	iommu = sun50i_iommu_from_dev(dev);
	if (!iommu)
		return -ENODEV;

	dev_dbg(dev, "Attaching to IOMMU domain\n");

	refcount_inc(&sun50i_domain->refcnt);

	if (iommu->domain == domain)
		return 0;

	sun50i_iommu_identity_attach(&sun50i_iommu_identity_domain, dev);

	sun50i_iommu_attach_domain(iommu, sun50i_domain);

	return 0;
}

static struct iommu_device *sun50i_iommu_probe_device(struct device *dev)
{
	struct sun50i_iommu *iommu;

	iommu = sun50i_iommu_from_dev(dev);
	if (!iommu)
		return ERR_PTR(-ENODEV);

	return &iommu->iommu;
}

static int sun50i_iommu_of_xlate(struct device *dev,
				 struct of_phandle_args *args)
{
	struct platform_device *iommu_pdev = of_find_device_by_node(args->np);
	unsigned id = args->args[0];

	dev_iommu_priv_set(dev, platform_get_drvdata(iommu_pdev));

	return iommu_fwspec_add_ids(dev, &id, 1);
}

static const struct iommu_ops sun50i_iommu_ops = {
	.identity_domain = &sun50i_iommu_identity_domain,
	.pgsize_bitmap	= SZ_4K,
	.device_group	= generic_single_device_group,
	.domain_alloc_paging = sun50i_iommu_domain_alloc_paging,
	.of_xlate	= sun50i_iommu_of_xlate,
	.probe_device	= sun50i_iommu_probe_device,
	.default_domain_ops = &(const struct iommu_domain_ops) {
		.attach_dev	= sun50i_iommu_attach_device,
		.flush_iotlb_all = sun50i_iommu_flush_iotlb_all,
		.iotlb_sync_map = sun50i_iommu_iotlb_sync_map,
		.iotlb_sync	= sun50i_iommu_iotlb_sync,
		.iova_to_phys	= sun50i_iommu_iova_to_phys,
		.map_pages	= sun50i_iommu_map,
		.unmap_pages	= sun50i_iommu_unmap,
		.free		= sun50i_iommu_domain_free,
	}
};

static void sun50i_iommu_report_fault(struct sun50i_iommu *iommu,
				      unsigned master, phys_addr_t iova,
				      unsigned prot)
{
	dev_err(iommu->dev, "Page fault for %pad (master %d, dir %s)\n",
		&iova, master, (prot == IOMMU_FAULT_WRITE) ? "wr" : "rd");

	if (iommu->domain)
		report_iommu_fault(iommu->domain, iommu->dev, iova, prot);
	else
		dev_err(iommu->dev, "Page fault while iommu not attached to any domain?\n");

	sun50i_iommu_zap_range(iommu, iova, SPAGE_SIZE);
}

static phys_addr_t sun50i_iommu_handle_pt_irq(struct sun50i_iommu *iommu,
					      unsigned addr_reg,
					      unsigned blame_reg)
{
	phys_addr_t iova;
	unsigned master;
	u32 blame;

	assert_spin_locked(&iommu->iommu_lock);

	iova = iommu_read(iommu, addr_reg);
	blame = iommu_read(iommu, blame_reg);
	master = ilog2(blame & IOMMU_INT_MASTER_MASK);

	/*
	 * If the address is not in the page table, we can't get what
	 * operation triggered the fault. Assume it's a read
	 * operation.
	 */
	sun50i_iommu_report_fault(iommu, master, iova, IOMMU_FAULT_READ);

	return iova;
}

static phys_addr_t sun50i_iommu_handle_perm_irq(struct sun50i_iommu *iommu)
{
	enum sun50i_iommu_aci aci;
	phys_addr_t iova;
	unsigned master;
	unsigned dir;
	u32 blame;

	assert_spin_locked(&iommu->iommu_lock);

	blame = iommu_read(iommu, IOMMU_INT_STA_REG);
	master = ilog2(blame & IOMMU_INT_MASTER_MASK);
	iova = iommu_read(iommu, IOMMU_INT_ERR_ADDR_REG(master));
	aci = sun50i_get_pte_aci(iommu_read(iommu,
					    IOMMU_INT_ERR_DATA_REG(master)));

	switch (aci) {
		/*
		 * If we are in the read-only domain, then it means we
		 * tried to write.
		 */
	case SUN50I_IOMMU_ACI_RD:
		dir = IOMMU_FAULT_WRITE;
		break;

		/*
		 * If we are in the write-only domain, then it means
		 * we tried to read.
		 */
	case SUN50I_IOMMU_ACI_WR:

		/*
		 * If we are in the domain without any permission, we
		 * can't really tell. Let's default to a read
		 * operation.
		 */
	case SUN50I_IOMMU_ACI_NONE:

		/* WTF? */
	case SUN50I_IOMMU_ACI_RD_WR:
	default:
		dir = IOMMU_FAULT_READ;
		break;
	}

	/*
	 * If the address is not in the page table, we can't get what
	 * operation triggered the fault. Assume it's a read
	 * operation.
	 */
	sun50i_iommu_report_fault(iommu, master, iova, dir);

	return iova;
}

static irqreturn_t sun50i_iommu_irq(int irq, void *dev_id)
{
	u32 status, l1_status, l2_status, resets;
	struct sun50i_iommu *iommu = dev_id;

	spin_lock(&iommu->iommu_lock);

	status = iommu_read(iommu, IOMMU_INT_STA_REG);
	if (!(status & IOMMU_INT_MASK)) {
		spin_unlock(&iommu->iommu_lock);
		return IRQ_NONE;
	}

	l1_status = iommu_read(iommu, IOMMU_L1PG_INT_REG);
	l2_status = iommu_read(iommu, IOMMU_L2PG_INT_REG);

	if (status & IOMMU_INT_INVALID_L2PG)
		sun50i_iommu_handle_pt_irq(iommu,
					    IOMMU_INT_ERR_ADDR_L2_REG,
					    IOMMU_L2PG_INT_REG);
	else if (status & IOMMU_INT_INVALID_L1PG)
		sun50i_iommu_handle_pt_irq(iommu,
					   IOMMU_INT_ERR_ADDR_L1_REG,
					   IOMMU_L1PG_INT_REG);
	else
		sun50i_iommu_handle_perm_irq(iommu);

	iommu_write(iommu, IOMMU_INT_CLR_REG, status);

	resets = (status | l1_status | l2_status) & IOMMU_INT_MASTER_MASK;
	iommu_write(iommu, IOMMU_RESET_REG, ~resets);
	iommu_write(iommu, IOMMU_RESET_REG, IOMMU_RESET_RELEASE_ALL);

	spin_unlock(&iommu->iommu_lock);

	return IRQ_HANDLED;
}

static int sun50i_iommu_probe(struct platform_device *pdev)
{
	struct sun50i_iommu *iommu;
	int ret, irq;

	iommu = devm_kzalloc(&pdev->dev, sizeof(*iommu), GFP_KERNEL);
	if (!iommu)
		return -ENOMEM;
	spin_lock_init(&iommu->iommu_lock);
	iommu->domain = &sun50i_iommu_identity_domain;
	platform_set_drvdata(pdev, iommu);
	iommu->dev = &pdev->dev;

	iommu->pt_pool = kmem_cache_create(dev_name(&pdev->dev),
					   PT_SIZE, PT_SIZE,
					   SLAB_HWCACHE_ALIGN,
					   NULL);
	if (!iommu->pt_pool)
		return -ENOMEM;

	iommu->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(iommu->base)) {
		ret = PTR_ERR(iommu->base);
		goto err_free_cache;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err_free_cache;
	}

	iommu->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(iommu->clk)) {
		dev_err(&pdev->dev, "Couldn't get our clock.\n");
		ret = PTR_ERR(iommu->clk);
		goto err_free_cache;
	}

	iommu->reset = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR(iommu->reset)) {
		dev_err(&pdev->dev, "Couldn't get our reset line.\n");
		ret = PTR_ERR(iommu->reset);
		goto err_free_cache;
	}

	ret = iommu_device_sysfs_add(&iommu->iommu, &pdev->dev,
				     NULL, dev_name(&pdev->dev));
	if (ret)
		goto err_free_cache;

	ret = iommu_device_register(&iommu->iommu, &sun50i_iommu_ops, &pdev->dev);
	if (ret)
		goto err_remove_sysfs;

	ret = devm_request_irq(&pdev->dev, irq, sun50i_iommu_irq, 0,
			       dev_name(&pdev->dev), iommu);
	if (ret < 0)
		goto err_unregister;

	return 0;

err_unregister:
	iommu_device_unregister(&iommu->iommu);

err_remove_sysfs:
	iommu_device_sysfs_remove(&iommu->iommu);

err_free_cache:
	kmem_cache_destroy(iommu->pt_pool);

	return ret;
}

static const struct of_device_id sun50i_iommu_dt[] = {
	{ .compatible = "allwinner,sun50i-h6-iommu", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, sun50i_iommu_dt);

static struct platform_driver sun50i_iommu_driver = {
	.driver		= {
		.name			= "sun50i-iommu",
		.of_match_table 	= sun50i_iommu_dt,
		.suppress_bind_attrs	= true,
	}
};
builtin_platform_driver_probe(sun50i_iommu_driver, sun50i_iommu_probe);

MODULE_DESCRIPTION("Allwinner H6 IOMMU driver");
MODULE_AUTHOR("Maxime Ripard <maxime@cerno.tech>");
MODULE_AUTHOR("zhuxianbin <zhuxianbin@allwinnertech.com>");
