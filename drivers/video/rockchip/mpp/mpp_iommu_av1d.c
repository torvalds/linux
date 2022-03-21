// SPDX-License-Identifier: GPL-2.0-only
/*
 * Compatible with the IOMMU of av1 decode
 *
 * Module Authors:	Yandong Lin <yandong.lin@rock-chips.com>
 *			Simon Xue <xxm@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-iommu.h>
#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_iommu.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include "mpp_debug.h"
#include "mpp_common.h"
#include "mpp_iommu.h"

struct av1_iommu_domain {
	struct list_head iommus;
	u32 *dt; /* page directory table */
	dma_addr_t dt_dma;
	spinlock_t iommus_lock; /* lock for iommus list */
	spinlock_t dt_lock; /* lock for modifying page directory table */
	struct iommu_domain domain;
	/* for av1 iommu */
	u64 *pta; /* page directory table */
	dma_addr_t pta_dma;
};

struct av1_iommu {
	struct device *dev;
	void __iomem **bases;
	int num_mmu;
	int num_irq;
	struct clk_bulk_data *clocks;
	int num_clocks;
	struct iommu_device iommu;
	struct list_head node; /* entry in rk_iommu_domain.iommus */
	struct iommu_domain *domain; /* domain to which iommu is attached */
	struct iommu_group *group;
};

struct av1_iommudata {
	struct device_link *link; /* runtime PM link from IOMMU to master */
	struct av1_iommu *iommu;
	bool defer_attach;
};

#define RK_IOMMU_AV1	0xa
#define NUM_DT_ENTRIES	1024
#define NUM_PT_ENTRIES	1024

#define SPAGE_ORDER	12
#define SPAGE_SIZE	(1 << SPAGE_ORDER)

/* av1 iommu regs address */
#define AV1_CLOCK_CTRL_BASE			0x0
#define AV1_IDLE_ST_BASE			0x4
#define AV1_MMU_CONFIG0_BASE			0x184
#define AV1_MMU_CONFIG1_BASE			0x1ac
#define AV1_MMU_AHB_EXCEPTION_BASE		0x380
#define AV1_MMU_AHB_STATUS_BASE			0x384
#define AV1_MMU_AHB_CONTROL_BASE		0x388
#define AV1_MMU_AHB_TBL_ARRAY_BASE_L_BASE	0x38C
#define AV1_MMU_AHB_TBL_ARRAY_BASE_H_BASE	0x390
#define AV1_MMU_AHB_CTX_PD_BASE			0x3b4
#define AV1_MMU_BUTT_BASE			0xffff

/* MMU register offsets */
#define AV1_MMU_FLUSH_BASE		0x184
#define AV1_MMU_BIT_FLUSH		BIT(4)

#define AV1_MMU_PAGE_FAULT_ADDR		0x380
#define AV1_MMU_STATUS_BASE		0x384	/* IRQ status */

#define AV1_MMU_EN_BASE			0x388
#define AV1_MMU_BIT_ENABLE		BIT(0)

#define AV1_MMU_OUT_OF_BOUND		BIT(28)
/* Irq mask */
#define AV1_MMU_IRQ_MASK		0x7

#define AV1_DTE_PT_ADDRESS_MASK		0xffffffc0
#define AV1_DTE_PT_VALID		BIT(0)

#define AV1_PAGE_DESC_LO_MASK		0xfffff000
#define AV1_PAGE_DESC_HI_MASK		GENMASK_ULL(39, 32)
#define AV1_PAGE_DESC_HI_SHIFT		(32-4)

#define AV1_IOMMU_PGSIZE_BITMAP 0x007ff000

static inline phys_addr_t av1_dte_pt_address(u32 dte)
{
	return (phys_addr_t)dte & AV1_DTE_PT_ADDRESS_MASK;
}

static inline u32 av1_mk_dte(dma_addr_t pt_dma)
{
	return (pt_dma) | AV1_DTE_PT_VALID;
}

#define AV1_PTE_PAGE_ADDRESS_MASK	0xfffffff0
#define AV1_PTE_PAGE_WRITABLE		BIT(2)
#define AV1_PTE_PAGE_VALID		BIT(0)

static struct device *dma_dev;

static inline phys_addr_t av1_pte_page_address(u32 pte)
{
	u64 pte_av1 = pte;

	pte_av1 = ((pte_av1 & AV1_PAGE_DESC_HI_MASK) << AV1_PAGE_DESC_HI_SHIFT) |
		  (pte_av1 & AV1_PAGE_DESC_LO_MASK);

	return (phys_addr_t)pte_av1;
}

static u32 av1_mk_pte(phys_addr_t page, int prot)
{
	u32 flags = 0;

	flags |= (prot & IOMMU_WRITE) ? AV1_PTE_PAGE_WRITABLE : 0;
	page = (page & AV1_PAGE_DESC_LO_MASK) |
	       ((page & AV1_PAGE_DESC_HI_MASK) >> AV1_PAGE_DESC_HI_SHIFT);
	page &= AV1_PTE_PAGE_ADDRESS_MASK;

	return page | flags | AV1_PTE_PAGE_VALID;
}

#define AV1_DTE_PT_VALID	BIT(0)

static inline bool av1_dte_is_pt_valid(u32 dte)
{
	return dte & AV1_DTE_PT_VALID;
}

static inline bool av1_pte_is_page_valid(u32 pte)
{
	return pte & AV1_PTE_PAGE_VALID;
}

static u32 av1_mk_pte_invalid(u32 pte)
{
	return pte & ~AV1_PTE_PAGE_VALID;
}

#define AV1_MASTER_TLB_MASK	GENMASK_ULL(31, 10)
/* mode 0 : 4k */
#define AV1_PTA_4K_MODE	0

static struct av1_iommu *av1_iommu_from_dev(struct device *dev)
{
	struct av1_iommudata *data = dev_iommu_priv_get(dev);

	return data ? data->iommu : NULL;
}

static u64 av1_mk_pta(dma_addr_t dt_dma)
{
	u64 val = (dt_dma & AV1_MASTER_TLB_MASK) | AV1_PTA_4K_MODE;

	return val;
}

static struct av1_iommu_domain *to_av1_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct av1_iommu_domain, domain);
}

static void av1_iommu_disable(struct av1_iommu *iommu)
{
	int i;

	/* Ignore error while disabling, just keep going */
	WARN_ON(clk_bulk_enable(iommu->num_clocks, iommu->clocks));
	for (i = 0; i < iommu->num_mmu; i++)
		writel(0, iommu->bases[i] + AV1_MMU_AHB_CONTROL_BASE);

	clk_bulk_disable(iommu->num_clocks, iommu->clocks);
}

int mpp_av1_iommu_disable(struct device *dev)
{
	struct av1_iommu *iommu = av1_iommu_from_dev(dev);

	if (!iommu->domain)
		return 0;

	av1_iommu_disable(iommu);

	return 0;
}

static int av1_iommu_enable(struct av1_iommu *iommu)
{
	struct iommu_domain *domain = iommu->domain;
	struct av1_iommu_domain *av1_domain = to_av1_domain(domain);
	int ret, i;

	ret = clk_bulk_enable(iommu->num_clocks, iommu->clocks);
	if (ret)
		return ret;

	for (i = 0; i < iommu->num_mmu; i++) {
		u32 val = readl(iommu->bases[i] + AV1_MMU_AHB_CONTROL_BASE);

		if (!(val & AV1_MMU_BIT_ENABLE)) {
			writel(av1_domain->pta_dma,
			       iommu->bases[i] + AV1_MMU_AHB_TBL_ARRAY_BASE_L_BASE);
			writel(AV1_MMU_OUT_OF_BOUND, iommu->bases[i] + AV1_MMU_CONFIG1_BASE);
			writel(AV1_MMU_BIT_ENABLE, iommu->bases[i] + AV1_MMU_AHB_EXCEPTION_BASE);
			writel(AV1_MMU_BIT_ENABLE, iommu->bases[i] + AV1_MMU_AHB_CONTROL_BASE);
		}
	}
	clk_bulk_disable(iommu->num_clocks, iommu->clocks);
	return ret;
}

int mpp_av1_iommu_enable(struct device *dev)
{
	struct av1_iommu *iommu = av1_iommu_from_dev(dev);

	if (!iommu->domain)
		return 0;

	return av1_iommu_enable(iommu);
}

static inline void av1_table_flush(struct av1_iommu_domain *dom, dma_addr_t dma,
				  unsigned int count)
{
	size_t size = count * sizeof(u32); /* count of u32 entry */

	dma_sync_single_for_device(dma_dev, dma, size, DMA_TO_DEVICE);
}

#define AV1_IOVA_DTE_MASK	0xffc00000
#define AV1_IOVA_DTE_SHIFT	22
#define AV1_IOVA_PTE_MASK	0x003ff000
#define AV1_IOVA_PTE_SHIFT	12
#define AV1_IOVA_PAGE_MASK	0x00000fff
#define AV1_IOVA_PAGE_SHIFT	0

static u32 av1_iova_dte_index(dma_addr_t iova)
{
	return (u32)(iova & AV1_IOVA_DTE_MASK) >> AV1_IOVA_DTE_SHIFT;
}

static u32 av1_iova_pte_index(dma_addr_t iova)
{
	return (u32)(iova & AV1_IOVA_PTE_MASK) >> AV1_IOVA_PTE_SHIFT;
}

static u32 av1_iova_page_offset(dma_addr_t iova)
{
	return (u32)(iova & AV1_IOVA_PAGE_MASK) >> AV1_IOVA_PAGE_SHIFT;
}

static u32 av1_iommu_read(void __iomem *base, u32 offset)
{
	return readl(base + offset);
}

static void av1_iommu_write(void __iomem *base, u32 offset, u32 value)
{
	writel(value, base + offset);
}


static void av1_iommu_flush_tlb_all(struct iommu_domain *domain)
{
	struct av1_iommu_domain *av1_domain = to_av1_domain(domain);
	struct list_head *pos;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&av1_domain->iommus_lock, flags);
	list_for_each(pos, &av1_domain->iommus) {
		struct av1_iommu *iommu;
		int ret;

		iommu = list_entry(pos, struct av1_iommu, node);
		ret = pm_runtime_get_if_in_use(iommu->dev);
		if (WARN_ON_ONCE(ret < 0))
			continue;
		if (ret) {
			WARN_ON(clk_bulk_enable(iommu->num_clocks, iommu->clocks));
			for (i = 0; i < iommu->num_mmu; i++) {
				writel(AV1_MMU_BIT_FLUSH,
				       iommu->bases[i] + AV1_MMU_FLUSH_BASE);
				writel(0, iommu->bases[i] + AV1_MMU_FLUSH_BASE);
			}
			clk_bulk_disable(iommu->num_clocks, iommu->clocks);
			pm_runtime_put(iommu->dev);
		}
	}
	spin_unlock_irqrestore(&av1_domain->iommus_lock, flags);
}

static irqreturn_t av1_iommu_irq(int irq, void *dev_id)
{
	struct av1_iommu *iommu = dev_id;
	u32 int_status;
	dma_addr_t iova;
	irqreturn_t ret = IRQ_NONE;
	int i, err;

	err = pm_runtime_get_if_in_use(iommu->dev);
	if (!err || WARN_ON_ONCE(err < 0))
		return ret;

	if (WARN_ON(clk_bulk_enable(iommu->num_clocks, iommu->clocks)))
		goto out;

	for (i = 0; i < iommu->num_mmu; i++) {
		int_status = av1_iommu_read(iommu->bases[i], AV1_MMU_STATUS_BASE);
		if (int_status & AV1_MMU_IRQ_MASK) {
			dev_err(iommu->dev, "unexpected int_status=%08x\n", int_status);
			iova = av1_iommu_read(iommu->bases[i], AV1_MMU_PAGE_FAULT_ADDR);

			if (iommu->domain)
				report_iommu_fault(iommu->domain, iommu->dev, iova, int_status);
			else
				dev_err(iommu->dev,
					"Page fault while iommu not attached to domain?\n");
		}
		av1_iommu_write(iommu->bases[i], AV1_MMU_STATUS_BASE, 0);
		ret = IRQ_HANDLED;
	}

	clk_bulk_disable(iommu->num_clocks, iommu->clocks);

out:
	pm_runtime_put(iommu->dev);
	return ret;
}

static bool av1_iommu_is_attach_deferred(struct iommu_domain *domain,
					struct device *dev)
{
	struct av1_iommudata *data = dev_iommu_priv_get(dev);

	return data->defer_attach;
}

static struct iommu_domain *av1_iommu_domain_alloc(unsigned type)
{
	struct av1_iommu_domain *av1_domain;

	if (type != IOMMU_DOMAIN_UNMANAGED && type != IOMMU_DOMAIN_DMA)
		return NULL;

	if (!dma_dev)
		return NULL;

	av1_domain = kzalloc(sizeof(*av1_domain), GFP_KERNEL);
	if (!av1_domain)
		return NULL;

	if (type == IOMMU_DOMAIN_DMA &&
	    iommu_get_dma_cookie(&av1_domain->domain))
		goto err_free_domain;

	/*
	 * av132xx iommus use a 2 level pagetable.
	 * Each level1 (dt) and level2 (pt) table has 1024 4-byte entries.
	 * Allocate one 4 KiB page for each table.
	 */
	av1_domain->dt = (u32 *)get_zeroed_page(GFP_KERNEL | GFP_DMA32);
	if (!av1_domain->dt)
		goto err_put_cookie;

	av1_domain->dt_dma = dma_map_single(dma_dev, av1_domain->dt,
					   SPAGE_SIZE, DMA_TO_DEVICE);
	if (dma_mapping_error(dma_dev, av1_domain->dt_dma)) {
		dev_err(dma_dev, "DMA map error for DT\n");
		goto err_free_dt;
	}

	av1_domain->pta = (u64 *)get_zeroed_page(GFP_KERNEL | GFP_DMA32);
	if (!av1_domain->pta)
		goto err_unmap_dt;

	av1_domain->pta_dma = dma_map_single(dma_dev, av1_domain->pta,
					SPAGE_SIZE, DMA_TO_DEVICE);
	if (dma_mapping_error(dma_dev, av1_domain->pta_dma)) {
		dev_err(dma_dev, "DMA map error for PTA\n");
		goto err_free_pta;
	}
	av1_domain->pta[0] = av1_mk_pta(av1_domain->dt_dma);

	av1_table_flush(av1_domain, av1_domain->pta_dma, 1024);
	av1_table_flush(av1_domain, av1_domain->dt_dma, NUM_DT_ENTRIES);

	spin_lock_init(&av1_domain->iommus_lock);
	spin_lock_init(&av1_domain->dt_lock);
	INIT_LIST_HEAD(&av1_domain->iommus);

	av1_domain->domain.geometry.aperture_start = 0;
	av1_domain->domain.geometry.aperture_end   = DMA_BIT_MASK(32);
	av1_domain->domain.geometry.force_aperture = true;

	return &av1_domain->domain;
err_free_pta:
	free_page((unsigned long)av1_domain->pta);
err_unmap_dt:
	dma_unmap_single(dma_dev, av1_domain->dt_dma,
			 SPAGE_SIZE, DMA_TO_DEVICE);
err_free_dt:
	free_page((unsigned long)av1_domain->dt);
err_put_cookie:
	if (type == IOMMU_DOMAIN_DMA)
		iommu_put_dma_cookie(&av1_domain->domain);
err_free_domain:
	kfree(av1_domain);

	return NULL;
}

static phys_addr_t av1_iommu_iova_to_phys(struct iommu_domain *domain,
						   dma_addr_t iova)
{
	struct av1_iommu_domain *av1_domain = to_av1_domain(domain);
	unsigned long flags;
	phys_addr_t pt_phys, phys = 0;
	u32 dte, pte;
	u32 *page_table;

	spin_lock_irqsave(&av1_domain->dt_lock, flags);

	dte = av1_domain->dt[av1_iova_dte_index(iova)];
	if (!av1_dte_is_pt_valid(dte))
		goto out;

	pt_phys = av1_dte_pt_address(dte);
	page_table = (u32 *)phys_to_virt(pt_phys);
	pte = page_table[av1_iova_pte_index(iova)];
	if (!av1_pte_is_page_valid(pte))
		goto out;

	phys = av1_pte_page_address(pte) + av1_iova_page_offset(iova);
out:
	spin_unlock_irqrestore(&av1_domain->dt_lock, flags);

	return phys;
}

static u32 *av1_dte_get_page_table(struct av1_iommu_domain *av1_domain, dma_addr_t iova)
{
	u32 *page_table, *dte_addr;
	u32 dte_index, dte;
	phys_addr_t pt_phys;
	dma_addr_t pt_dma;

	assert_spin_locked(&av1_domain->dt_lock);

	dte_index = av1_iova_dte_index(iova);
	dte_addr = &av1_domain->dt[dte_index];
	dte = *dte_addr;
	if (av1_dte_is_pt_valid(dte))
		goto done;

	page_table = (u32 *)get_zeroed_page(GFP_ATOMIC | GFP_DMA32);
	if (!page_table)
		return ERR_PTR(-ENOMEM);

	pt_dma = dma_map_single(dma_dev, page_table, SPAGE_SIZE, DMA_TO_DEVICE);
	if (dma_mapping_error(dma_dev, pt_dma)) {
		dev_err(dma_dev, "DMA mapping error while allocating page table\n");
		free_page((unsigned long)page_table);
		return ERR_PTR(-ENOMEM);
	}

	dte = av1_mk_dte(pt_dma);
	*dte_addr = dte;

	av1_table_flush(av1_domain, pt_dma, NUM_PT_ENTRIES);
	av1_table_flush(av1_domain,
		       av1_domain->dt_dma + dte_index * sizeof(u32), 1);
done:
	pt_phys = av1_dte_pt_address(dte);
	return (u32 *)phys_to_virt(pt_phys);
}

static size_t av1_iommu_unmap_iova(struct av1_iommu_domain *av1_domain,
				   u32 *pte_addr, dma_addr_t pte_dma,
				   size_t size)
{
	unsigned int pte_count;
	unsigned int pte_total = size / SPAGE_SIZE;

	assert_spin_locked(&av1_domain->dt_lock);

	for (pte_count = 0; pte_count < pte_total; pte_count++) {
		u32 pte = pte_addr[pte_count];

		if (!av1_pte_is_page_valid(pte))
			break;

		pte_addr[pte_count] = av1_mk_pte_invalid(pte);
	}

	av1_table_flush(av1_domain, pte_dma, pte_count);

	return pte_count * SPAGE_SIZE;
}

static int av1_iommu_map_iova(struct av1_iommu_domain *av1_domain, u32 *pte_addr,
			      dma_addr_t pte_dma, dma_addr_t iova,
			      phys_addr_t paddr, size_t size, int prot)
{
	unsigned int pte_count;
	unsigned int pte_total = size / SPAGE_SIZE;
	phys_addr_t page_phys;

	assert_spin_locked(&av1_domain->dt_lock);

	for (pte_count = 0; pte_count < pte_total; pte_count++) {
		u32 pte = pte_addr[pte_count];

		if (av1_pte_is_page_valid(pte))
			goto unwind;

		pte_addr[pte_count] = av1_mk_pte(paddr, prot);

		paddr += SPAGE_SIZE;
	}

	av1_table_flush(av1_domain, pte_dma, pte_total);

	return 0;
unwind:
	/* Unmap the range of iovas that we just mapped */
	av1_iommu_unmap_iova(av1_domain, pte_addr, pte_dma,
			    pte_count * SPAGE_SIZE);

	iova += pte_count * SPAGE_SIZE;
	page_phys = av1_pte_page_address(pte_addr[pte_count]);
	pr_err("iova: %pad already mapped to %pa cannot remap to phys: %pa prot: %#x\n",
	       &iova, &page_phys, &paddr, prot);

	return -EADDRINUSE;
}

static size_t av1_iommu_unmap(struct iommu_domain *domain, unsigned long _iova,
			      size_t size, struct iommu_iotlb_gather *gather)
{
	struct av1_iommu_domain *av1_domain = to_av1_domain(domain);
	unsigned long flags;
	dma_addr_t pte_dma, iova = (dma_addr_t)_iova;
	phys_addr_t pt_phys;
	u32 dte;
	u32 *pte_addr;
	size_t unmap_size;

	spin_lock_irqsave(&av1_domain->dt_lock, flags);

	dte = av1_domain->dt[av1_iova_dte_index(iova)];
	/* Just return 0 if iova is unmapped */
	if (!av1_dte_is_pt_valid(dte)) {
		spin_unlock_irqrestore(&av1_domain->dt_lock, flags);
		return 0;
	}

	pt_phys = av1_dte_pt_address(dte);
	pte_addr = (u32 *)phys_to_virt(pt_phys) + av1_iova_pte_index(iova);
	pte_dma = pt_phys + av1_iova_pte_index(iova) * sizeof(u32);
	unmap_size = av1_iommu_unmap_iova(av1_domain, pte_addr, pte_dma, size);

	spin_unlock_irqrestore(&av1_domain->dt_lock, flags);

	return unmap_size;
}

static int av1_iommu_map(struct iommu_domain *domain, unsigned long _iova,
			    phys_addr_t paddr, size_t size, int prot, gfp_t gfp)
{
	struct av1_iommu_domain *av1_domain = to_av1_domain(domain);
	unsigned long flags;
	dma_addr_t pte_dma, iova = (dma_addr_t)_iova;
	u32 *page_table, *pte_addr;
	u32 dte, pte_index;
	int ret;

	spin_lock_irqsave(&av1_domain->dt_lock, flags);

	page_table = av1_dte_get_page_table(av1_domain, iova);
	if (IS_ERR(page_table)) {
		spin_unlock_irqrestore(&av1_domain->dt_lock, flags);
		return PTR_ERR(page_table);
	}

	dte = av1_domain->dt[av1_iova_dte_index(iova)];
	pte_index = av1_iova_pte_index(iova);
	pte_addr = &page_table[pte_index];
	pte_dma = av1_dte_pt_address(dte) + pte_index * sizeof(u32);
	ret = av1_iommu_map_iova(av1_domain, pte_addr, pte_dma, iova,
				   paddr, size, prot);

	spin_unlock_irqrestore(&av1_domain->dt_lock, flags);

	return ret;
}

static void av1_iommu_detach_device(struct iommu_domain *domain,
						  struct device *dev)
{
	struct av1_iommu *iommu;
	struct av1_iommu_domain *av1_domain = to_av1_domain(domain);
	unsigned long flags;
	int ret;

	/* Allow 'virtual devices' (eg drm) to detach from domain */
	iommu = av1_iommu_from_dev(dev);
	if (WARN_ON(!iommu))
		return;

	dev_dbg(dev, "Detaching from iommu domain\n");

	if (!iommu->domain)
		return;

	spin_lock_irqsave(&av1_domain->iommus_lock, flags);
	list_del_init(&iommu->node);
	spin_unlock_irqrestore(&av1_domain->iommus_lock, flags);

	ret = pm_runtime_get_if_in_use(iommu->dev);
	WARN_ON_ONCE(ret < 0);
	if (ret > 0) {
		av1_iommu_disable(iommu);
		pm_runtime_put(iommu->dev);
	}
	iommu->domain = NULL;
}

static int av1_iommu_attach_device(struct iommu_domain *domain,
						  struct device *dev)
{
	struct av1_iommu *iommu;
	struct av1_iommu_domain *av1_domain = to_av1_domain(domain);
	unsigned long flags;
	int ret;

	iommu = av1_iommu_from_dev(dev);
	if (WARN_ON(!iommu))
		return -ENODEV;

	if (iommu->domain)
		av1_iommu_detach_device(iommu->domain, dev);

	iommu->domain = domain;

	/* Attach NULL for disable iommu */
	if (!domain)
		return 0;

	spin_lock_irqsave(&av1_domain->iommus_lock, flags);
	list_add_tail(&iommu->node, &av1_domain->iommus);
	spin_unlock_irqrestore(&av1_domain->iommus_lock, flags);

	ret = pm_runtime_get_if_in_use(iommu->dev);
	if (!ret || WARN_ON_ONCE(ret < 0))
		return 0;

	ret = av1_iommu_enable(iommu);
	if (ret)
		av1_iommu_detach_device(iommu->domain, dev);

	pm_runtime_put(iommu->dev);

	return ret;
}

static void av1_iommu_domain_free(struct iommu_domain *domain)
{
	struct av1_iommu_domain *av1_domain = to_av1_domain(domain);
	int i;

	WARN_ON(!list_empty(&av1_domain->iommus));

	for (i = 0; i < NUM_DT_ENTRIES; i++) {
		u32 dte = av1_domain->dt[i];

		if (av1_dte_is_pt_valid(dte)) {
			phys_addr_t pt_phys = av1_dte_pt_address(dte);
			u32 *page_table = phys_to_virt(pt_phys);

			dma_unmap_single(dma_dev, pt_phys,
					 SPAGE_SIZE, DMA_TO_DEVICE);
			free_page((unsigned long)page_table);
		}
	}

	dma_unmap_single(dma_dev, av1_domain->dt_dma,
			 SPAGE_SIZE, DMA_TO_DEVICE);
	free_page((unsigned long)av1_domain->dt);

	dma_unmap_single(dma_dev, av1_domain->pta_dma,
			 SPAGE_SIZE, DMA_TO_DEVICE);
	free_page((unsigned long)av1_domain->pta);

	if (domain->type == IOMMU_DOMAIN_DMA)
		iommu_put_dma_cookie(&av1_domain->domain);
	kfree(av1_domain);
}

static struct iommu_device *av1_iommu_probe_device(struct device *dev)
{
	struct av1_iommudata *data;
	struct av1_iommu *iommu;

	data = dev_iommu_priv_get(dev);
	if (!data)
		return ERR_PTR(-ENODEV);

	iommu = av1_iommu_from_dev(dev);

	pr_info("%s,%d, consumer : %s, supplier : %s\n",
		__func__, __LINE__, dev_name(dev), dev_name(iommu->dev));

	/*
	 * link will free by platform_device_del(master) via
	 * BUS_NOTIFY_REMOVED_DEVICE
	 */
	data->link = device_link_add(dev, iommu->dev,
				     DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME);

	/* set max segment size for dev, needed for single chunk map */
	if (!dev->dma_parms)
		dev->dma_parms = kzalloc(sizeof(*dev->dma_parms), GFP_KERNEL);
	if (!dev->dma_parms)
		return ERR_PTR(-ENOMEM);

	dma_set_max_seg_size(dev, DMA_BIT_MASK(32));

	return &iommu->iommu;
}

static void av1_iommu_release_device(struct device *dev)
{
	const struct iommu_ops *ops = dev->bus->iommu_ops;

	/* hack for rmmod */
	__module_get(ops->owner);
}

static struct iommu_group *av1_iommu_device_group(struct device *dev)
{
	struct av1_iommu *iommu;

	iommu = av1_iommu_from_dev(dev);

	return iommu_group_ref_get(iommu->group);
}

static int av1_iommu_of_xlate(struct device *dev,
			     struct of_phandle_args *args)
{
	struct platform_device *iommu_dev;
	struct av1_iommudata *data;

	data = devm_kzalloc(dma_dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	dev_info(dev, "%s,%d\n", __func__, __LINE__);
	iommu_dev = of_find_device_by_node(args->np);

	data->iommu = platform_get_drvdata(iommu_dev);

	dev_iommu_priv_set(dev, data);

	platform_device_put(iommu_dev);

	return 0;
}

static void av1_iommu_probe_finalize(struct device *dev)
{
	const struct iommu_ops *ops = dev->bus->iommu_ops;

	/* hack for rmmod */
	module_put(ops->owner);
}

static struct iommu_ops av1_iommu_ops = {
	.domain_alloc = av1_iommu_domain_alloc,
	.domain_free = av1_iommu_domain_free,
	.attach_dev = av1_iommu_attach_device,
	.detach_dev = av1_iommu_detach_device,
	.map = av1_iommu_map,
	.unmap = av1_iommu_unmap,
	.flush_iotlb_all = av1_iommu_flush_tlb_all,
	.probe_device = av1_iommu_probe_device,
	.release_device = av1_iommu_release_device,
	.iova_to_phys = av1_iommu_iova_to_phys,
	.is_attach_deferred = av1_iommu_is_attach_deferred,
	.device_group = av1_iommu_device_group,
	.pgsize_bitmap = AV1_IOMMU_PGSIZE_BITMAP,
	.of_xlate = av1_iommu_of_xlate,
	.probe_finalize = av1_iommu_probe_finalize,
};

static const struct of_device_id av1_iommu_dt_ids[] = {
	{
		.compatible = "rockchip,iommu-av1",
	},
	{ /* sentinel */ }
};

static int av1_iommu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct av1_iommu *iommu;
	struct resource *res;
	int num_res = pdev->num_resources;
	int err, i;
	const struct of_device_id *match;

	iommu = devm_kzalloc(dev, sizeof(*iommu), GFP_KERNEL);
	if (!iommu)
		return -ENOMEM;

	match = of_match_device(av1_iommu_dt_ids, dev);
	if (!match)
		return -EINVAL;

	platform_set_drvdata(pdev, iommu);
	iommu->dev = dev;
	iommu->num_mmu = 0;

	iommu->bases = devm_kcalloc(dev, num_res, sizeof(*iommu->bases),
				    GFP_KERNEL);
	if (!iommu->bases)
		return -ENOMEM;

	for (i = 0; i < num_res; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res)
			continue;
		iommu->bases[i] = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(iommu->bases[i]))
			continue;
		iommu->num_mmu++;
	}
	if (iommu->num_mmu == 0)
		return PTR_ERR(iommu->bases[0]);

	iommu->num_irq = platform_irq_count(pdev);
	if (iommu->num_irq < 0)
		return iommu->num_irq;

	err = devm_clk_bulk_get_all(dev, &iommu->clocks);
	if (err >= 0)
		iommu->num_clocks = err;
	else if (err == -ENOENT)
		iommu->num_clocks = 0;
	else
		return err;

	err = clk_bulk_prepare(iommu->num_clocks, iommu->clocks);
	if (err)
		return err;

	iommu->group = iommu_group_alloc();
	if (IS_ERR(iommu->group)) {
		err = PTR_ERR(iommu->group);
		goto err_unprepare_clocks;
	}

	err = iommu_device_sysfs_add(&iommu->iommu, dev, NULL, dev_name(dev));
	if (err)
		goto err_put_group;

	iommu_device_set_ops(&iommu->iommu, &av1_iommu_ops);
	iommu_device_set_fwnode(&iommu->iommu, &dev->of_node->fwnode);

	err = iommu_device_register(&iommu->iommu);
	if (err)
		goto err_remove_sysfs;

	if (!dma_dev)
		dma_dev = &pdev->dev;

	bus_set_iommu(&av1dec_bus, &av1_iommu_ops);

	pm_runtime_enable(dev);

	for (i = 0; i < iommu->num_irq; i++) {
		int irq = platform_get_irq(pdev, i);

		if (irq < 0)
			return irq;

		err = devm_request_irq(iommu->dev, irq, av1_iommu_irq,
				       IRQF_SHARED, dev_name(dev), iommu);
		if (err) {
			pm_runtime_disable(dev);
			goto err_remove_sysfs;
		}
	}

	return 0;
err_remove_sysfs:
	iommu_device_sysfs_remove(&iommu->iommu);
err_put_group:
	iommu_group_put(iommu->group);
err_unprepare_clocks:
	clk_bulk_unprepare(iommu->num_clocks, iommu->clocks);
	return err;
}

static int av1_iommu_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct av1_iommu *iommu = platform_get_drvdata(pdev);

	iommu_device_unregister(&iommu->iommu);
	iommu_device_sysfs_remove(&iommu->iommu);
	pm_runtime_disable(dev);
	return 0;
}

static void av1_iommu_shutdown(struct platform_device *pdev)
{
	struct av1_iommu *iommu = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < iommu->num_irq; i++) {
		int irq = platform_get_irq(pdev, i);

		devm_free_irq(iommu->dev, irq, iommu);
	}

	pm_runtime_force_suspend(&pdev->dev);
}

static int __maybe_unused av1_iommu_suspend(struct device *dev)
{
	struct av1_iommu *iommu = dev_get_drvdata(dev);

	if (!iommu->domain)
		return 0;

	av1_iommu_disable(iommu);
	return 0;
}

static int __maybe_unused av1_iommu_resume(struct device *dev)
{
	struct av1_iommu *iommu = dev_get_drvdata(dev);

	if (!iommu->domain)
		return 0;

	return av1_iommu_enable(iommu);
}

static const struct dev_pm_ops av1_iommu_pm_ops = {
	SET_RUNTIME_PM_OPS(av1_iommu_suspend, av1_iommu_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

struct platform_driver rockchip_av1_iommu_driver = {
	.probe = av1_iommu_probe,
	.remove = av1_iommu_remove,
	.shutdown = av1_iommu_shutdown,
	.driver = {
		   .name = "av1_iommu",
		   .of_match_table = av1_iommu_dt_ids,
		   .pm = &av1_iommu_pm_ops,
		   .suppress_bind_attrs = true,
	},
};
