/*
 * Copyright (c) 2011,2016 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifdef CONFIG_EXYNOS_IOMMU_DEBUG
#define DEBUG
#endif

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iommu.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/of_iommu.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/dma-iommu.h>

typedef u32 sysmmu_iova_t;
typedef u32 sysmmu_pte_t;

/* We do not consider super section mapping (16MB) */
#define SECT_ORDER 20
#define LPAGE_ORDER 16
#define SPAGE_ORDER 12

#define SECT_SIZE (1 << SECT_ORDER)
#define LPAGE_SIZE (1 << LPAGE_ORDER)
#define SPAGE_SIZE (1 << SPAGE_ORDER)

#define SECT_MASK (~(SECT_SIZE - 1))
#define LPAGE_MASK (~(LPAGE_SIZE - 1))
#define SPAGE_MASK (~(SPAGE_SIZE - 1))

#define lv1ent_fault(sent) ((*(sent) == ZERO_LV2LINK) || \
			   ((*(sent) & 3) == 0) || ((*(sent) & 3) == 3))
#define lv1ent_zero(sent) (*(sent) == ZERO_LV2LINK)
#define lv1ent_page_zero(sent) ((*(sent) & 3) == 1)
#define lv1ent_page(sent) ((*(sent) != ZERO_LV2LINK) && \
			  ((*(sent) & 3) == 1))
#define lv1ent_section(sent) ((*(sent) & 3) == 2)

#define lv2ent_fault(pent) ((*(pent) & 3) == 0)
#define lv2ent_small(pent) ((*(pent) & 2) == 2)
#define lv2ent_large(pent) ((*(pent) & 3) == 1)

/*
 * v1.x - v3.x SYSMMU supports 32bit physical and 32bit virtual address spaces
 * v5.0 introduced support for 36bit physical address space by shifting
 * all page entry values by 4 bits.
 * All SYSMMU controllers in the system support the address spaces of the same
 * size, so PG_ENT_SHIFT can be initialized on first SYSMMU probe to proper
 * value (0 or 4).
 */
static short PG_ENT_SHIFT = -1;
#define SYSMMU_PG_ENT_SHIFT 0
#define SYSMMU_V5_PG_ENT_SHIFT 4

#define sect_to_phys(ent) (((phys_addr_t) ent) << PG_ENT_SHIFT)
#define section_phys(sent) (sect_to_phys(*(sent)) & SECT_MASK)
#define section_offs(iova) (iova & (SECT_SIZE - 1))
#define lpage_phys(pent) (sect_to_phys(*(pent)) & LPAGE_MASK)
#define lpage_offs(iova) (iova & (LPAGE_SIZE - 1))
#define spage_phys(pent) (sect_to_phys(*(pent)) & SPAGE_MASK)
#define spage_offs(iova) (iova & (SPAGE_SIZE - 1))

#define NUM_LV1ENTRIES 4096
#define NUM_LV2ENTRIES (SECT_SIZE / SPAGE_SIZE)

static u32 lv1ent_offset(sysmmu_iova_t iova)
{
	return iova >> SECT_ORDER;
}

static u32 lv2ent_offset(sysmmu_iova_t iova)
{
	return (iova >> SPAGE_ORDER) & (NUM_LV2ENTRIES - 1);
}

#define LV1TABLE_SIZE (NUM_LV1ENTRIES * sizeof(sysmmu_pte_t))
#define LV2TABLE_SIZE (NUM_LV2ENTRIES * sizeof(sysmmu_pte_t))

#define SPAGES_PER_LPAGE (LPAGE_SIZE / SPAGE_SIZE)
#define lv2table_base(sent) (sect_to_phys(*(sent) & 0xFFFFFFC0))

#define mk_lv1ent_sect(pa) ((pa >> PG_ENT_SHIFT) | 2)
#define mk_lv1ent_page(pa) ((pa >> PG_ENT_SHIFT) | 1)
#define mk_lv2ent_lpage(pa) ((pa >> PG_ENT_SHIFT) | 1)
#define mk_lv2ent_spage(pa) ((pa >> PG_ENT_SHIFT) | 2)

#define CTRL_ENABLE	0x5
#define CTRL_BLOCK	0x7
#define CTRL_DISABLE	0x0

#define CFG_LRU		0x1
#define CFG_QOS(n)	((n & 0xF) << 7)
#define CFG_ACGEN	(1 << 24) /* System MMU 3.3 only */
#define CFG_SYSSEL	(1 << 22) /* System MMU 3.2 only */
#define CFG_FLPDCACHE	(1 << 20) /* System MMU 3.2+ only */

/* common registers */
#define REG_MMU_CTRL		0x000
#define REG_MMU_CFG		0x004
#define REG_MMU_STATUS		0x008
#define REG_MMU_VERSION		0x034

#define MMU_MAJ_VER(val)	((val) >> 7)
#define MMU_MIN_VER(val)	((val) & 0x7F)
#define MMU_RAW_VER(reg)	(((reg) >> 21) & ((1 << 11) - 1)) /* 11 bits */

#define MAKE_MMU_VER(maj, min)	((((maj) & 0xF) << 7) | ((min) & 0x7F))

/* v1.x - v3.x registers */
#define REG_MMU_FLUSH		0x00C
#define REG_MMU_FLUSH_ENTRY	0x010
#define REG_PT_BASE_ADDR	0x014
#define REG_INT_STATUS		0x018
#define REG_INT_CLEAR		0x01C

#define REG_PAGE_FAULT_ADDR	0x024
#define REG_AW_FAULT_ADDR	0x028
#define REG_AR_FAULT_ADDR	0x02C
#define REG_DEFAULT_SLAVE_ADDR	0x030

/* v5.x registers */
#define REG_V5_PT_BASE_PFN	0x00C
#define REG_V5_MMU_FLUSH_ALL	0x010
#define REG_V5_MMU_FLUSH_ENTRY	0x014
#define REG_V5_INT_STATUS	0x060
#define REG_V5_INT_CLEAR	0x064
#define REG_V5_FAULT_AR_VA	0x070
#define REG_V5_FAULT_AW_VA	0x080

#define has_sysmmu(dev)		(dev->archdata.iommu != NULL)

static struct device *dma_dev;
static struct kmem_cache *lv2table_kmem_cache;
static sysmmu_pte_t *zero_lv2_table;
#define ZERO_LV2LINK mk_lv1ent_page(virt_to_phys(zero_lv2_table))

static sysmmu_pte_t *section_entry(sysmmu_pte_t *pgtable, sysmmu_iova_t iova)
{
	return pgtable + lv1ent_offset(iova);
}

static sysmmu_pte_t *page_entry(sysmmu_pte_t *sent, sysmmu_iova_t iova)
{
	return (sysmmu_pte_t *)phys_to_virt(
				lv2table_base(sent)) + lv2ent_offset(iova);
}

/*
 * IOMMU fault information register
 */
struct sysmmu_fault_info {
	unsigned int bit;	/* bit number in STATUS register */
	unsigned short addr_reg; /* register to read VA fault address */
	const char *name;	/* human readable fault name */
	unsigned int type;	/* fault type for report_iommu_fault */
};

static const struct sysmmu_fault_info sysmmu_faults[] = {
	{ 0, REG_PAGE_FAULT_ADDR, "PAGE", IOMMU_FAULT_READ },
	{ 1, REG_AR_FAULT_ADDR, "AR MULTI-HIT", IOMMU_FAULT_READ },
	{ 2, REG_AW_FAULT_ADDR, "AW MULTI-HIT", IOMMU_FAULT_WRITE },
	{ 3, REG_DEFAULT_SLAVE_ADDR, "BUS ERROR", IOMMU_FAULT_READ },
	{ 4, REG_AR_FAULT_ADDR, "AR SECURITY PROTECTION", IOMMU_FAULT_READ },
	{ 5, REG_AR_FAULT_ADDR, "AR ACCESS PROTECTION", IOMMU_FAULT_READ },
	{ 6, REG_AW_FAULT_ADDR, "AW SECURITY PROTECTION", IOMMU_FAULT_WRITE },
	{ 7, REG_AW_FAULT_ADDR, "AW ACCESS PROTECTION", IOMMU_FAULT_WRITE },
};

static const struct sysmmu_fault_info sysmmu_v5_faults[] = {
	{ 0, REG_V5_FAULT_AR_VA, "AR PTW", IOMMU_FAULT_READ },
	{ 1, REG_V5_FAULT_AR_VA, "AR PAGE", IOMMU_FAULT_READ },
	{ 2, REG_V5_FAULT_AR_VA, "AR MULTI-HIT", IOMMU_FAULT_READ },
	{ 3, REG_V5_FAULT_AR_VA, "AR ACCESS PROTECTION", IOMMU_FAULT_READ },
	{ 4, REG_V5_FAULT_AR_VA, "AR SECURITY PROTECTION", IOMMU_FAULT_READ },
	{ 16, REG_V5_FAULT_AW_VA, "AW PTW", IOMMU_FAULT_WRITE },
	{ 17, REG_V5_FAULT_AW_VA, "AW PAGE", IOMMU_FAULT_WRITE },
	{ 18, REG_V5_FAULT_AW_VA, "AW MULTI-HIT", IOMMU_FAULT_WRITE },
	{ 19, REG_V5_FAULT_AW_VA, "AW ACCESS PROTECTION", IOMMU_FAULT_WRITE },
	{ 20, REG_V5_FAULT_AW_VA, "AW SECURITY PROTECTION", IOMMU_FAULT_WRITE },
};

/*
 * This structure is attached to dev.archdata.iommu of the master device
 * on device add, contains a list of SYSMMU controllers defined by device tree,
 * which are bound to given master device. It is usually referenced by 'owner'
 * pointer.
*/
struct exynos_iommu_owner {
	struct list_head controllers;	/* list of sysmmu_drvdata.owner_node */
	struct iommu_domain *domain;	/* domain this device is attached */
};

/*
 * This structure exynos specific generalization of struct iommu_domain.
 * It contains list of SYSMMU controllers from all master devices, which has
 * been attached to this domain and page tables of IO address space defined by
 * it. It is usually referenced by 'domain' pointer.
 */
struct exynos_iommu_domain {
	struct list_head clients; /* list of sysmmu_drvdata.domain_node */
	sysmmu_pte_t *pgtable;	/* lv1 page table, 16KB */
	short *lv2entcnt;	/* free lv2 entry counter for each section */
	spinlock_t lock;	/* lock for modyfying list of clients */
	spinlock_t pgtablelock;	/* lock for modifying page table @ pgtable */
	struct iommu_domain domain; /* generic domain data structure */
};

/*
 * This structure hold all data of a single SYSMMU controller, this includes
 * hw resources like registers and clocks, pointers and list nodes to connect
 * it to all other structures, internal state and parameters read from device
 * tree. It is usually referenced by 'data' pointer.
 */
struct sysmmu_drvdata {
	struct device *sysmmu;		/* SYSMMU controller device */
	struct device *master;		/* master device (owner) */
	void __iomem *sfrbase;		/* our registers */
	struct clk *clk;		/* SYSMMU's clock */
	struct clk *aclk;		/* SYSMMU's aclk clock */
	struct clk *pclk;		/* SYSMMU's pclk clock */
	struct clk *clk_master;		/* master's device clock */
	int activations;		/* number of calls to sysmmu_enable */
	spinlock_t lock;		/* lock for modyfying state */
	struct exynos_iommu_domain *domain; /* domain we belong to */
	struct list_head domain_node;	/* node for domain clients list */
	struct list_head owner_node;	/* node for owner controllers list */
	phys_addr_t pgtable;		/* assigned page table structure */
	unsigned int version;		/* our version */
};

static struct exynos_iommu_domain *to_exynos_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct exynos_iommu_domain, domain);
}

static bool set_sysmmu_active(struct sysmmu_drvdata *data)
{
	/* return true if the System MMU was not active previously
	   and it needs to be initialized */
	return ++data->activations == 1;
}

static bool set_sysmmu_inactive(struct sysmmu_drvdata *data)
{
	/* return true if the System MMU is needed to be disabled */
	BUG_ON(data->activations < 1);
	return --data->activations == 0;
}

static bool is_sysmmu_active(struct sysmmu_drvdata *data)
{
	return data->activations > 0;
}

static void sysmmu_unblock(struct sysmmu_drvdata *data)
{
	writel(CTRL_ENABLE, data->sfrbase + REG_MMU_CTRL);
}

static bool sysmmu_block(struct sysmmu_drvdata *data)
{
	int i = 120;

	writel(CTRL_BLOCK, data->sfrbase + REG_MMU_CTRL);
	while ((i > 0) && !(readl(data->sfrbase + REG_MMU_STATUS) & 1))
		--i;

	if (!(readl(data->sfrbase + REG_MMU_STATUS) & 1)) {
		sysmmu_unblock(data);
		return false;
	}

	return true;
}

static void __sysmmu_tlb_invalidate(struct sysmmu_drvdata *data)
{
	if (MMU_MAJ_VER(data->version) < 5)
		writel(0x1, data->sfrbase + REG_MMU_FLUSH);
	else
		writel(0x1, data->sfrbase + REG_V5_MMU_FLUSH_ALL);
}

static void __sysmmu_tlb_invalidate_entry(struct sysmmu_drvdata *data,
				sysmmu_iova_t iova, unsigned int num_inv)
{
	unsigned int i;

	for (i = 0; i < num_inv; i++) {
		if (MMU_MAJ_VER(data->version) < 5)
			writel((iova & SPAGE_MASK) | 1,
				     data->sfrbase + REG_MMU_FLUSH_ENTRY);
		else
			writel((iova & SPAGE_MASK) | 1,
				     data->sfrbase + REG_V5_MMU_FLUSH_ENTRY);
		iova += SPAGE_SIZE;
	}
}

static void __sysmmu_set_ptbase(struct sysmmu_drvdata *data, phys_addr_t pgd)
{
	if (MMU_MAJ_VER(data->version) < 5)
		writel(pgd, data->sfrbase + REG_PT_BASE_ADDR);
	else
		writel(pgd >> PAGE_SHIFT,
			     data->sfrbase + REG_V5_PT_BASE_PFN);

	__sysmmu_tlb_invalidate(data);
}

static void __sysmmu_get_version(struct sysmmu_drvdata *data)
{
	u32 ver;

	clk_enable(data->clk_master);
	clk_enable(data->clk);
	clk_enable(data->pclk);
	clk_enable(data->aclk);

	ver = readl(data->sfrbase + REG_MMU_VERSION);

	/* controllers on some SoCs don't report proper version */
	if (ver == 0x80000001u)
		data->version = MAKE_MMU_VER(1, 0);
	else
		data->version = MMU_RAW_VER(ver);

	dev_dbg(data->sysmmu, "hardware version: %d.%d\n",
		MMU_MAJ_VER(data->version), MMU_MIN_VER(data->version));

	clk_disable(data->aclk);
	clk_disable(data->pclk);
	clk_disable(data->clk);
	clk_disable(data->clk_master);
}

static void show_fault_information(struct sysmmu_drvdata *data,
				   const struct sysmmu_fault_info *finfo,
				   sysmmu_iova_t fault_addr)
{
	sysmmu_pte_t *ent;

	dev_err(data->sysmmu, "%s FAULT occurred at %#x (page table base: %pa)\n",
		finfo->name, fault_addr, &data->pgtable);
	ent = section_entry(phys_to_virt(data->pgtable), fault_addr);
	dev_err(data->sysmmu, "\tLv1 entry: %#x\n", *ent);
	if (lv1ent_page(ent)) {
		ent = page_entry(ent, fault_addr);
		dev_err(data->sysmmu, "\t Lv2 entry: %#x\n", *ent);
	}
}

static irqreturn_t exynos_sysmmu_irq(int irq, void *dev_id)
{
	/* SYSMMU is in blocked state when interrupt occurred. */
	struct sysmmu_drvdata *data = dev_id;
	const struct sysmmu_fault_info *finfo;
	unsigned int i, n, itype;
	sysmmu_iova_t fault_addr = -1;
	unsigned short reg_status, reg_clear;
	int ret = -ENOSYS;

	WARN_ON(!is_sysmmu_active(data));

	if (MMU_MAJ_VER(data->version) < 5) {
		reg_status = REG_INT_STATUS;
		reg_clear = REG_INT_CLEAR;
		finfo = sysmmu_faults;
		n = ARRAY_SIZE(sysmmu_faults);
	} else {
		reg_status = REG_V5_INT_STATUS;
		reg_clear = REG_V5_INT_CLEAR;
		finfo = sysmmu_v5_faults;
		n = ARRAY_SIZE(sysmmu_v5_faults);
	}

	spin_lock(&data->lock);

	clk_enable(data->clk_master);

	itype = __ffs(readl(data->sfrbase + reg_status));
	for (i = 0; i < n; i++, finfo++)
		if (finfo->bit == itype)
			break;
	/* unknown/unsupported fault */
	BUG_ON(i == n);

	/* print debug message */
	fault_addr = readl(data->sfrbase + finfo->addr_reg);
	show_fault_information(data, finfo, fault_addr);

	if (data->domain)
		ret = report_iommu_fault(&data->domain->domain,
					data->master, fault_addr, finfo->type);
	/* fault is not recovered by fault handler */
	BUG_ON(ret != 0);

	writel(1 << itype, data->sfrbase + reg_clear);

	sysmmu_unblock(data);

	clk_disable(data->clk_master);

	spin_unlock(&data->lock);

	return IRQ_HANDLED;
}

static void __sysmmu_disable_nocount(struct sysmmu_drvdata *data)
{
	clk_enable(data->clk_master);

	writel(CTRL_DISABLE, data->sfrbase + REG_MMU_CTRL);
	writel(0, data->sfrbase + REG_MMU_CFG);

	clk_disable(data->aclk);
	clk_disable(data->pclk);
	clk_disable(data->clk);
	clk_disable(data->clk_master);
}

static bool __sysmmu_disable(struct sysmmu_drvdata *data)
{
	bool disabled;
	unsigned long flags;

	spin_lock_irqsave(&data->lock, flags);

	disabled = set_sysmmu_inactive(data);

	if (disabled) {
		data->pgtable = 0;
		data->domain = NULL;

		__sysmmu_disable_nocount(data);

		dev_dbg(data->sysmmu, "Disabled\n");
	} else  {
		dev_dbg(data->sysmmu, "%d times left to disable\n",
					data->activations);
	}

	spin_unlock_irqrestore(&data->lock, flags);

	return disabled;
}

static void __sysmmu_init_config(struct sysmmu_drvdata *data)
{
	unsigned int cfg;

	if (data->version <= MAKE_MMU_VER(3, 1))
		cfg = CFG_LRU | CFG_QOS(15);
	else if (data->version <= MAKE_MMU_VER(3, 2))
		cfg = CFG_LRU | CFG_QOS(15) | CFG_FLPDCACHE | CFG_SYSSEL;
	else
		cfg = CFG_QOS(15) | CFG_FLPDCACHE | CFG_ACGEN;

	writel(cfg, data->sfrbase + REG_MMU_CFG);
}

static void __sysmmu_enable_nocount(struct sysmmu_drvdata *data)
{
	clk_enable(data->clk_master);
	clk_enable(data->clk);
	clk_enable(data->pclk);
	clk_enable(data->aclk);

	writel(CTRL_BLOCK, data->sfrbase + REG_MMU_CTRL);

	__sysmmu_init_config(data);

	__sysmmu_set_ptbase(data, data->pgtable);

	writel(CTRL_ENABLE, data->sfrbase + REG_MMU_CTRL);

	clk_disable(data->clk_master);
}

static int __sysmmu_enable(struct sysmmu_drvdata *data, phys_addr_t pgtable,
			   struct exynos_iommu_domain *domain)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&data->lock, flags);
	if (set_sysmmu_active(data)) {
		data->pgtable = pgtable;
		data->domain = domain;

		__sysmmu_enable_nocount(data);

		dev_dbg(data->sysmmu, "Enabled\n");
	} else {
		ret = (pgtable == data->pgtable) ? 1 : -EBUSY;

		dev_dbg(data->sysmmu, "already enabled\n");
	}

	if (WARN_ON(ret < 0))
		set_sysmmu_inactive(data); /* decrement count */

	spin_unlock_irqrestore(&data->lock, flags);

	return ret;
}

static void sysmmu_tlb_invalidate_flpdcache(struct sysmmu_drvdata *data,
					    sysmmu_iova_t iova)
{
	unsigned long flags;

	clk_enable(data->clk_master);

	spin_lock_irqsave(&data->lock, flags);
	if (is_sysmmu_active(data)) {
		if (data->version >= MAKE_MMU_VER(3, 3))
			__sysmmu_tlb_invalidate_entry(data, iova, 1);
	}
	spin_unlock_irqrestore(&data->lock, flags);

	clk_disable(data->clk_master);
}

static void sysmmu_tlb_invalidate_entry(struct sysmmu_drvdata *data,
					sysmmu_iova_t iova, size_t size)
{
	unsigned long flags;

	spin_lock_irqsave(&data->lock, flags);
	if (is_sysmmu_active(data)) {
		unsigned int num_inv = 1;

		clk_enable(data->clk_master);

		/*
		 * L2TLB invalidation required
		 * 4KB page: 1 invalidation
		 * 64KB page: 16 invalidations
		 * 1MB page: 64 invalidations
		 * because it is set-associative TLB
		 * with 8-way and 64 sets.
		 * 1MB page can be cached in one of all sets.
		 * 64KB page can be one of 16 consecutive sets.
		 */
		if (MMU_MAJ_VER(data->version) == 2)
			num_inv = min_t(unsigned int, size / PAGE_SIZE, 64);

		if (sysmmu_block(data)) {
			__sysmmu_tlb_invalidate_entry(data, iova, num_inv);
			sysmmu_unblock(data);
		}
		clk_disable(data->clk_master);
	} else {
		dev_dbg(data->master,
			"disabled. Skipping TLB invalidation @ %#x\n", iova);
	}
	spin_unlock_irqrestore(&data->lock, flags);
}

static int __init exynos_sysmmu_probe(struct platform_device *pdev)
{
	int irq, ret;
	struct device *dev = &pdev->dev;
	struct sysmmu_drvdata *data;
	struct resource *res;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->sfrbase = devm_ioremap_resource(dev, res);
	if (IS_ERR(data->sfrbase))
		return PTR_ERR(data->sfrbase);

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(dev, "Unable to find IRQ resource\n");
		return irq;
	}

	ret = devm_request_irq(dev, irq, exynos_sysmmu_irq, 0,
				dev_name(dev), data);
	if (ret) {
		dev_err(dev, "Unabled to register handler of irq %d\n", irq);
		return ret;
	}

	data->clk = devm_clk_get(dev, "sysmmu");
	if (!IS_ERR(data->clk)) {
		ret = clk_prepare(data->clk);
		if (ret) {
			dev_err(dev, "Failed to prepare clk\n");
			return ret;
		}
	} else {
		data->clk = NULL;
	}

	data->aclk = devm_clk_get(dev, "aclk");
	if (!IS_ERR(data->aclk)) {
		ret = clk_prepare(data->aclk);
		if (ret) {
			dev_err(dev, "Failed to prepare aclk\n");
			return ret;
		}
	} else {
		data->aclk = NULL;
	}

	data->pclk = devm_clk_get(dev, "pclk");
	if (!IS_ERR(data->pclk)) {
		ret = clk_prepare(data->pclk);
		if (ret) {
			dev_err(dev, "Failed to prepare pclk\n");
			return ret;
		}
	} else {
		data->pclk = NULL;
	}

	if (!data->clk && (!data->aclk || !data->pclk)) {
		dev_err(dev, "Failed to get device clock(s)!\n");
		return -ENOSYS;
	}

	data->clk_master = devm_clk_get(dev, "master");
	if (!IS_ERR(data->clk_master)) {
		ret = clk_prepare(data->clk_master);
		if (ret) {
			dev_err(dev, "Failed to prepare master's clk\n");
			return ret;
		}
	} else {
		data->clk_master = NULL;
	}

	data->sysmmu = dev;
	spin_lock_init(&data->lock);

	platform_set_drvdata(pdev, data);

	__sysmmu_get_version(data);
	if (PG_ENT_SHIFT < 0) {
		if (MMU_MAJ_VER(data->version) < 5)
			PG_ENT_SHIFT = SYSMMU_PG_ENT_SHIFT;
		else
			PG_ENT_SHIFT = SYSMMU_V5_PG_ENT_SHIFT;
	}

	pm_runtime_enable(dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int exynos_sysmmu_suspend(struct device *dev)
{
	struct sysmmu_drvdata *data = dev_get_drvdata(dev);

	dev_dbg(dev, "suspend\n");
	if (is_sysmmu_active(data)) {
		__sysmmu_disable_nocount(data);
		pm_runtime_put(dev);
	}
	return 0;
}

static int exynos_sysmmu_resume(struct device *dev)
{
	struct sysmmu_drvdata *data = dev_get_drvdata(dev);

	dev_dbg(dev, "resume\n");
	if (is_sysmmu_active(data)) {
		pm_runtime_get_sync(dev);
		__sysmmu_enable_nocount(data);
	}
	return 0;
}
#endif

static const struct dev_pm_ops sysmmu_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(exynos_sysmmu_suspend, exynos_sysmmu_resume)
};

static const struct of_device_id sysmmu_of_match[] __initconst = {
	{ .compatible	= "samsung,exynos-sysmmu", },
	{ },
};

static struct platform_driver exynos_sysmmu_driver __refdata = {
	.probe	= exynos_sysmmu_probe,
	.driver	= {
		.name		= "exynos-sysmmu",
		.of_match_table	= sysmmu_of_match,
		.pm		= &sysmmu_pm_ops,
	}
};

static inline void update_pte(sysmmu_pte_t *ent, sysmmu_pte_t val)
{
	dma_sync_single_for_cpu(dma_dev, virt_to_phys(ent), sizeof(*ent),
				DMA_TO_DEVICE);
	*ent = val;
	dma_sync_single_for_device(dma_dev, virt_to_phys(ent), sizeof(*ent),
				   DMA_TO_DEVICE);
}

static struct iommu_domain *exynos_iommu_domain_alloc(unsigned type)
{
	struct exynos_iommu_domain *domain;
	dma_addr_t handle;
	int i;

	/* Check if correct PTE offsets are initialized */
	BUG_ON(PG_ENT_SHIFT < 0 || !dma_dev);

	domain = kzalloc(sizeof(*domain), GFP_KERNEL);
	if (!domain)
		return NULL;

	if (type == IOMMU_DOMAIN_DMA) {
		if (iommu_get_dma_cookie(&domain->domain) != 0)
			goto err_pgtable;
	} else if (type != IOMMU_DOMAIN_UNMANAGED) {
		goto err_pgtable;
	}

	domain->pgtable = (sysmmu_pte_t *)__get_free_pages(GFP_KERNEL, 2);
	if (!domain->pgtable)
		goto err_dma_cookie;

	domain->lv2entcnt = (short *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, 1);
	if (!domain->lv2entcnt)
		goto err_counter;

	/* Workaround for System MMU v3.3 to prevent caching 1MiB mapping */
	for (i = 0; i < NUM_LV1ENTRIES; i += 8) {
		domain->pgtable[i + 0] = ZERO_LV2LINK;
		domain->pgtable[i + 1] = ZERO_LV2LINK;
		domain->pgtable[i + 2] = ZERO_LV2LINK;
		domain->pgtable[i + 3] = ZERO_LV2LINK;
		domain->pgtable[i + 4] = ZERO_LV2LINK;
		domain->pgtable[i + 5] = ZERO_LV2LINK;
		domain->pgtable[i + 6] = ZERO_LV2LINK;
		domain->pgtable[i + 7] = ZERO_LV2LINK;
	}

	handle = dma_map_single(dma_dev, domain->pgtable, LV1TABLE_SIZE,
				DMA_TO_DEVICE);
	/* For mapping page table entries we rely on dma == phys */
	BUG_ON(handle != virt_to_phys(domain->pgtable));

	spin_lock_init(&domain->lock);
	spin_lock_init(&domain->pgtablelock);
	INIT_LIST_HEAD(&domain->clients);

	domain->domain.geometry.aperture_start = 0;
	domain->domain.geometry.aperture_end   = ~0UL;
	domain->domain.geometry.force_aperture = true;

	return &domain->domain;

err_counter:
	free_pages((unsigned long)domain->pgtable, 2);
err_dma_cookie:
	if (type == IOMMU_DOMAIN_DMA)
		iommu_put_dma_cookie(&domain->domain);
err_pgtable:
	kfree(domain);
	return NULL;
}

static void exynos_iommu_domain_free(struct iommu_domain *iommu_domain)
{
	struct exynos_iommu_domain *domain = to_exynos_domain(iommu_domain);
	struct sysmmu_drvdata *data, *next;
	unsigned long flags;
	int i;

	WARN_ON(!list_empty(&domain->clients));

	spin_lock_irqsave(&domain->lock, flags);

	list_for_each_entry_safe(data, next, &domain->clients, domain_node) {
		if (__sysmmu_disable(data))
			data->master = NULL;
		list_del_init(&data->domain_node);
	}

	spin_unlock_irqrestore(&domain->lock, flags);

	if (iommu_domain->type == IOMMU_DOMAIN_DMA)
		iommu_put_dma_cookie(iommu_domain);

	dma_unmap_single(dma_dev, virt_to_phys(domain->pgtable), LV1TABLE_SIZE,
			 DMA_TO_DEVICE);

	for (i = 0; i < NUM_LV1ENTRIES; i++)
		if (lv1ent_page(domain->pgtable + i)) {
			phys_addr_t base = lv2table_base(domain->pgtable + i);

			dma_unmap_single(dma_dev, base, LV2TABLE_SIZE,
					 DMA_TO_DEVICE);
			kmem_cache_free(lv2table_kmem_cache,
					phys_to_virt(base));
		}

	free_pages((unsigned long)domain->pgtable, 2);
	free_pages((unsigned long)domain->lv2entcnt, 1);
	kfree(domain);
}

static void exynos_iommu_detach_device(struct iommu_domain *iommu_domain,
				    struct device *dev)
{
	struct exynos_iommu_owner *owner = dev->archdata.iommu;
	struct exynos_iommu_domain *domain = to_exynos_domain(iommu_domain);
	phys_addr_t pagetable = virt_to_phys(domain->pgtable);
	struct sysmmu_drvdata *data, *next;
	unsigned long flags;
	bool found = false;

	if (!has_sysmmu(dev) || owner->domain != iommu_domain)
		return;

	spin_lock_irqsave(&domain->lock, flags);
	list_for_each_entry_safe(data, next, &domain->clients, domain_node) {
		if (data->master == dev) {
			if (__sysmmu_disable(data)) {
				data->master = NULL;
				list_del_init(&data->domain_node);
			}
			pm_runtime_put(data->sysmmu);
			found = true;
		}
	}
	spin_unlock_irqrestore(&domain->lock, flags);

	owner->domain = NULL;

	if (found)
		dev_dbg(dev, "%s: Detached IOMMU with pgtable %pa\n",
					__func__, &pagetable);
	else
		dev_err(dev, "%s: No IOMMU is attached\n", __func__);
}

static int exynos_iommu_attach_device(struct iommu_domain *iommu_domain,
				   struct device *dev)
{
	struct exynos_iommu_owner *owner = dev->archdata.iommu;
	struct exynos_iommu_domain *domain = to_exynos_domain(iommu_domain);
	struct sysmmu_drvdata *data;
	phys_addr_t pagetable = virt_to_phys(domain->pgtable);
	unsigned long flags;
	int ret = -ENODEV;

	if (!has_sysmmu(dev))
		return -ENODEV;

	if (owner->domain)
		exynos_iommu_detach_device(owner->domain, dev);

	list_for_each_entry(data, &owner->controllers, owner_node) {
		pm_runtime_get_sync(data->sysmmu);
		ret = __sysmmu_enable(data, pagetable, domain);
		if (ret >= 0) {
			data->master = dev;

			spin_lock_irqsave(&domain->lock, flags);
			list_add_tail(&data->domain_node, &domain->clients);
			spin_unlock_irqrestore(&domain->lock, flags);
		}
	}

	if (ret < 0) {
		dev_err(dev, "%s: Failed to attach IOMMU with pgtable %pa\n",
					__func__, &pagetable);
		return ret;
	}

	owner->domain = iommu_domain;
	dev_dbg(dev, "%s: Attached IOMMU with pgtable %pa %s\n",
		__func__, &pagetable, (ret == 0) ? "" : ", again");

	return ret;
}

static sysmmu_pte_t *alloc_lv2entry(struct exynos_iommu_domain *domain,
		sysmmu_pte_t *sent, sysmmu_iova_t iova, short *pgcounter)
{
	if (lv1ent_section(sent)) {
		WARN(1, "Trying mapping on %#08x mapped with 1MiB page", iova);
		return ERR_PTR(-EADDRINUSE);
	}

	if (lv1ent_fault(sent)) {
		sysmmu_pte_t *pent;
		bool need_flush_flpd_cache = lv1ent_zero(sent);

		pent = kmem_cache_zalloc(lv2table_kmem_cache, GFP_ATOMIC);
		BUG_ON((uintptr_t)pent & (LV2TABLE_SIZE - 1));
		if (!pent)
			return ERR_PTR(-ENOMEM);

		update_pte(sent, mk_lv1ent_page(virt_to_phys(pent)));
		kmemleak_ignore(pent);
		*pgcounter = NUM_LV2ENTRIES;
		dma_map_single(dma_dev, pent, LV2TABLE_SIZE, DMA_TO_DEVICE);

		/*
		 * If pre-fetched SLPD is a faulty SLPD in zero_l2_table,
		 * FLPD cache may cache the address of zero_l2_table. This
		 * function replaces the zero_l2_table with new L2 page table
		 * to write valid mappings.
		 * Accessing the valid area may cause page fault since FLPD
		 * cache may still cache zero_l2_table for the valid area
		 * instead of new L2 page table that has the mapping
		 * information of the valid area.
		 * Thus any replacement of zero_l2_table with other valid L2
		 * page table must involve FLPD cache invalidation for System
		 * MMU v3.3.
		 * FLPD cache invalidation is performed with TLB invalidation
		 * by VPN without blocking. It is safe to invalidate TLB without
		 * blocking because the target address of TLB invalidation is
		 * not currently mapped.
		 */
		if (need_flush_flpd_cache) {
			struct sysmmu_drvdata *data;

			spin_lock(&domain->lock);
			list_for_each_entry(data, &domain->clients, domain_node)
				sysmmu_tlb_invalidate_flpdcache(data, iova);
			spin_unlock(&domain->lock);
		}
	}

	return page_entry(sent, iova);
}

static int lv1set_section(struct exynos_iommu_domain *domain,
			  sysmmu_pte_t *sent, sysmmu_iova_t iova,
			  phys_addr_t paddr, short *pgcnt)
{
	if (lv1ent_section(sent)) {
		WARN(1, "Trying mapping on 1MiB@%#08x that is mapped",
			iova);
		return -EADDRINUSE;
	}

	if (lv1ent_page(sent)) {
		if (*pgcnt != NUM_LV2ENTRIES) {
			WARN(1, "Trying mapping on 1MiB@%#08x that is mapped",
				iova);
			return -EADDRINUSE;
		}

		kmem_cache_free(lv2table_kmem_cache, page_entry(sent, 0));
		*pgcnt = 0;
	}

	update_pte(sent, mk_lv1ent_sect(paddr));

	spin_lock(&domain->lock);
	if (lv1ent_page_zero(sent)) {
		struct sysmmu_drvdata *data;
		/*
		 * Flushing FLPD cache in System MMU v3.3 that may cache a FLPD
		 * entry by speculative prefetch of SLPD which has no mapping.
		 */
		list_for_each_entry(data, &domain->clients, domain_node)
			sysmmu_tlb_invalidate_flpdcache(data, iova);
	}
	spin_unlock(&domain->lock);

	return 0;
}

static int lv2set_page(sysmmu_pte_t *pent, phys_addr_t paddr, size_t size,
								short *pgcnt)
{
	if (size == SPAGE_SIZE) {
		if (WARN_ON(!lv2ent_fault(pent)))
			return -EADDRINUSE;

		update_pte(pent, mk_lv2ent_spage(paddr));
		*pgcnt -= 1;
	} else { /* size == LPAGE_SIZE */
		int i;
		dma_addr_t pent_base = virt_to_phys(pent);

		dma_sync_single_for_cpu(dma_dev, pent_base,
					sizeof(*pent) * SPAGES_PER_LPAGE,
					DMA_TO_DEVICE);
		for (i = 0; i < SPAGES_PER_LPAGE; i++, pent++) {
			if (WARN_ON(!lv2ent_fault(pent))) {
				if (i > 0)
					memset(pent - i, 0, sizeof(*pent) * i);
				return -EADDRINUSE;
			}

			*pent = mk_lv2ent_lpage(paddr);
		}
		dma_sync_single_for_device(dma_dev, pent_base,
					   sizeof(*pent) * SPAGES_PER_LPAGE,
					   DMA_TO_DEVICE);
		*pgcnt -= SPAGES_PER_LPAGE;
	}

	return 0;
}

/*
 * *CAUTION* to the I/O virtual memory managers that support exynos-iommu:
 *
 * System MMU v3.x has advanced logic to improve address translation
 * performance with caching more page table entries by a page table walk.
 * However, the logic has a bug that while caching faulty page table entries,
 * System MMU reports page fault if the cached fault entry is hit even though
 * the fault entry is updated to a valid entry after the entry is cached.
 * To prevent caching faulty page table entries which may be updated to valid
 * entries later, the virtual memory manager should care about the workaround
 * for the problem. The following describes the workaround.
 *
 * Any two consecutive I/O virtual address regions must have a hole of 128KiB
 * at maximum to prevent misbehavior of System MMU 3.x (workaround for h/w bug).
 *
 * Precisely, any start address of I/O virtual region must be aligned with
 * the following sizes for System MMU v3.1 and v3.2.
 * System MMU v3.1: 128KiB
 * System MMU v3.2: 256KiB
 *
 * Because System MMU v3.3 caches page table entries more aggressively, it needs
 * more workarounds.
 * - Any two consecutive I/O virtual regions must have a hole of size larger
 *   than or equal to 128KiB.
 * - Start address of an I/O virtual region must be aligned by 128KiB.
 */
static int exynos_iommu_map(struct iommu_domain *iommu_domain,
			    unsigned long l_iova, phys_addr_t paddr, size_t size,
			    int prot)
{
	struct exynos_iommu_domain *domain = to_exynos_domain(iommu_domain);
	sysmmu_pte_t *entry;
	sysmmu_iova_t iova = (sysmmu_iova_t)l_iova;
	unsigned long flags;
	int ret = -ENOMEM;

	BUG_ON(domain->pgtable == NULL);

	spin_lock_irqsave(&domain->pgtablelock, flags);

	entry = section_entry(domain->pgtable, iova);

	if (size == SECT_SIZE) {
		ret = lv1set_section(domain, entry, iova, paddr,
				     &domain->lv2entcnt[lv1ent_offset(iova)]);
	} else {
		sysmmu_pte_t *pent;

		pent = alloc_lv2entry(domain, entry, iova,
				      &domain->lv2entcnt[lv1ent_offset(iova)]);

		if (IS_ERR(pent))
			ret = PTR_ERR(pent);
		else
			ret = lv2set_page(pent, paddr, size,
				       &domain->lv2entcnt[lv1ent_offset(iova)]);
	}

	if (ret)
		pr_err("%s: Failed(%d) to map %#zx bytes @ %#x\n",
			__func__, ret, size, iova);

	spin_unlock_irqrestore(&domain->pgtablelock, flags);

	return ret;
}

static void exynos_iommu_tlb_invalidate_entry(struct exynos_iommu_domain *domain,
					      sysmmu_iova_t iova, size_t size)
{
	struct sysmmu_drvdata *data;
	unsigned long flags;

	spin_lock_irqsave(&domain->lock, flags);

	list_for_each_entry(data, &domain->clients, domain_node)
		sysmmu_tlb_invalidate_entry(data, iova, size);

	spin_unlock_irqrestore(&domain->lock, flags);
}

static size_t exynos_iommu_unmap(struct iommu_domain *iommu_domain,
				 unsigned long l_iova, size_t size)
{
	struct exynos_iommu_domain *domain = to_exynos_domain(iommu_domain);
	sysmmu_iova_t iova = (sysmmu_iova_t)l_iova;
	sysmmu_pte_t *ent;
	size_t err_pgsize;
	unsigned long flags;

	BUG_ON(domain->pgtable == NULL);

	spin_lock_irqsave(&domain->pgtablelock, flags);

	ent = section_entry(domain->pgtable, iova);

	if (lv1ent_section(ent)) {
		if (WARN_ON(size < SECT_SIZE)) {
			err_pgsize = SECT_SIZE;
			goto err;
		}

		/* workaround for h/w bug in System MMU v3.3 */
		update_pte(ent, ZERO_LV2LINK);
		size = SECT_SIZE;
		goto done;
	}

	if (unlikely(lv1ent_fault(ent))) {
		if (size > SECT_SIZE)
			size = SECT_SIZE;
		goto done;
	}

	/* lv1ent_page(sent) == true here */

	ent = page_entry(ent, iova);

	if (unlikely(lv2ent_fault(ent))) {
		size = SPAGE_SIZE;
		goto done;
	}

	if (lv2ent_small(ent)) {
		update_pte(ent, 0);
		size = SPAGE_SIZE;
		domain->lv2entcnt[lv1ent_offset(iova)] += 1;
		goto done;
	}

	/* lv1ent_large(ent) == true here */
	if (WARN_ON(size < LPAGE_SIZE)) {
		err_pgsize = LPAGE_SIZE;
		goto err;
	}

	dma_sync_single_for_cpu(dma_dev, virt_to_phys(ent),
				sizeof(*ent) * SPAGES_PER_LPAGE,
				DMA_TO_DEVICE);
	memset(ent, 0, sizeof(*ent) * SPAGES_PER_LPAGE);
	dma_sync_single_for_device(dma_dev, virt_to_phys(ent),
				   sizeof(*ent) * SPAGES_PER_LPAGE,
				   DMA_TO_DEVICE);
	size = LPAGE_SIZE;
	domain->lv2entcnt[lv1ent_offset(iova)] += SPAGES_PER_LPAGE;
done:
	spin_unlock_irqrestore(&domain->pgtablelock, flags);

	exynos_iommu_tlb_invalidate_entry(domain, iova, size);

	return size;
err:
	spin_unlock_irqrestore(&domain->pgtablelock, flags);

	pr_err("%s: Failed: size(%#zx) @ %#x is smaller than page size %#zx\n",
		__func__, size, iova, err_pgsize);

	return 0;
}

static phys_addr_t exynos_iommu_iova_to_phys(struct iommu_domain *iommu_domain,
					  dma_addr_t iova)
{
	struct exynos_iommu_domain *domain = to_exynos_domain(iommu_domain);
	sysmmu_pte_t *entry;
	unsigned long flags;
	phys_addr_t phys = 0;

	spin_lock_irqsave(&domain->pgtablelock, flags);

	entry = section_entry(domain->pgtable, iova);

	if (lv1ent_section(entry)) {
		phys = section_phys(entry) + section_offs(iova);
	} else if (lv1ent_page(entry)) {
		entry = page_entry(entry, iova);

		if (lv2ent_large(entry))
			phys = lpage_phys(entry) + lpage_offs(iova);
		else if (lv2ent_small(entry))
			phys = spage_phys(entry) + spage_offs(iova);
	}

	spin_unlock_irqrestore(&domain->pgtablelock, flags);

	return phys;
}

static struct iommu_group *get_device_iommu_group(struct device *dev)
{
	struct iommu_group *group;

	group = iommu_group_get(dev);
	if (!group)
		group = iommu_group_alloc();

	return group;
}

static int exynos_iommu_add_device(struct device *dev)
{
	struct iommu_group *group;

	if (!has_sysmmu(dev))
		return -ENODEV;

	group = iommu_group_get_for_dev(dev);

	if (IS_ERR(group))
		return PTR_ERR(group);

	iommu_group_put(group);

	return 0;
}

static void exynos_iommu_remove_device(struct device *dev)
{
	if (!has_sysmmu(dev))
		return;

	iommu_group_remove_device(dev);
}

static int exynos_iommu_of_xlate(struct device *dev,
				 struct of_phandle_args *spec)
{
	struct exynos_iommu_owner *owner = dev->archdata.iommu;
	struct platform_device *sysmmu = of_find_device_by_node(spec->np);
	struct sysmmu_drvdata *data;

	if (!sysmmu)
		return -ENODEV;

	data = platform_get_drvdata(sysmmu);
	if (!data)
		return -ENODEV;

	if (!owner) {
		owner = kzalloc(sizeof(*owner), GFP_KERNEL);
		if (!owner)
			return -ENOMEM;

		INIT_LIST_HEAD(&owner->controllers);
		dev->archdata.iommu = owner;
	}

	list_add_tail(&data->owner_node, &owner->controllers);
	return 0;
}

static struct iommu_ops exynos_iommu_ops = {
	.domain_alloc = exynos_iommu_domain_alloc,
	.domain_free = exynos_iommu_domain_free,
	.attach_dev = exynos_iommu_attach_device,
	.detach_dev = exynos_iommu_detach_device,
	.map = exynos_iommu_map,
	.unmap = exynos_iommu_unmap,
	.map_sg = default_iommu_map_sg,
	.iova_to_phys = exynos_iommu_iova_to_phys,
	.device_group = get_device_iommu_group,
	.add_device = exynos_iommu_add_device,
	.remove_device = exynos_iommu_remove_device,
	.pgsize_bitmap = SECT_SIZE | LPAGE_SIZE | SPAGE_SIZE,
	.of_xlate = exynos_iommu_of_xlate,
};

static bool init_done;

static int __init exynos_iommu_init(void)
{
	int ret;

	lv2table_kmem_cache = kmem_cache_create("exynos-iommu-lv2table",
				LV2TABLE_SIZE, LV2TABLE_SIZE, 0, NULL);
	if (!lv2table_kmem_cache) {
		pr_err("%s: Failed to create kmem cache\n", __func__);
		return -ENOMEM;
	}

	ret = platform_driver_register(&exynos_sysmmu_driver);
	if (ret) {
		pr_err("%s: Failed to register driver\n", __func__);
		goto err_reg_driver;
	}

	zero_lv2_table = kmem_cache_zalloc(lv2table_kmem_cache, GFP_KERNEL);
	if (zero_lv2_table == NULL) {
		pr_err("%s: Failed to allocate zero level2 page table\n",
			__func__);
		ret = -ENOMEM;
		goto err_zero_lv2;
	}

	ret = bus_set_iommu(&platform_bus_type, &exynos_iommu_ops);
	if (ret) {
		pr_err("%s: Failed to register exynos-iommu driver.\n",
								__func__);
		goto err_set_iommu;
	}

	init_done = true;

	return 0;
err_set_iommu:
	kmem_cache_free(lv2table_kmem_cache, zero_lv2_table);
err_zero_lv2:
	platform_driver_unregister(&exynos_sysmmu_driver);
err_reg_driver:
	kmem_cache_destroy(lv2table_kmem_cache);
	return ret;
}

static int __init exynos_iommu_of_setup(struct device_node *np)
{
	struct platform_device *pdev;

	if (!init_done)
		exynos_iommu_init();

	pdev = of_platform_device_create(np, NULL, platform_bus_type.dev_root);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	/*
	 * use the first registered sysmmu device for performing
	 * dma mapping operations on iommu page tables (cpu cache flush)
	 */
	if (!dma_dev)
		dma_dev = &pdev->dev;

	of_iommu_set_ops(np, &exynos_iommu_ops);
	return 0;
}

IOMMU_OF_DECLARE(exynos_iommu_of, "samsung,exynos-sysmmu",
		 exynos_iommu_of_setup);
