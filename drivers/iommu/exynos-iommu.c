/* linux/drivers/iommu/exynos_iommu.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
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

#include <asm/cacheflush.h>
#include <asm/dma-iommu.h>
#include <asm/pgtable.h>

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

static u32 sysmmu_page_offset(sysmmu_iova_t iova, u32 size)
{
	return iova & (size - 1);
}

#define section_phys(sent) (*(sent) & SECT_MASK)
#define section_offs(iova) sysmmu_page_offset((iova), SECT_SIZE)
#define lpage_phys(pent) (*(pent) & LPAGE_MASK)
#define lpage_offs(iova) sysmmu_page_offset((iova), LPAGE_SIZE)
#define spage_phys(pent) (*(pent) & SPAGE_MASK)
#define spage_offs(iova) sysmmu_page_offset((iova), SPAGE_SIZE)

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

#define LV2TABLE_SIZE (NUM_LV2ENTRIES * sizeof(sysmmu_pte_t))

#define SPAGES_PER_LPAGE (LPAGE_SIZE / SPAGE_SIZE)

#define lv2table_base(sent) (*(sent) & 0xFFFFFC00)

#define mk_lv1ent_sect(pa) ((pa) | 2)
#define mk_lv1ent_page(pa) ((pa) | 1)
#define mk_lv2ent_lpage(pa) ((pa) | 1)
#define mk_lv2ent_spage(pa) ((pa) | 2)

#define CTRL_ENABLE	0x5
#define CTRL_BLOCK	0x7
#define CTRL_DISABLE	0x0

#define CFG_LRU		0x1
#define CFG_QOS(n)	((n & 0xF) << 7)
#define CFG_MASK	0x0150FFFF /* Selecting bit 0-15, 20, 22 and 24 */
#define CFG_ACGEN	(1 << 24) /* System MMU 3.3 only */
#define CFG_SYSSEL	(1 << 22) /* System MMU 3.2 only */
#define CFG_FLPDCACHE	(1 << 20) /* System MMU 3.2+ only */

#define REG_MMU_CTRL		0x000
#define REG_MMU_CFG		0x004
#define REG_MMU_STATUS		0x008
#define REG_MMU_FLUSH		0x00C
#define REG_MMU_FLUSH_ENTRY	0x010
#define REG_PT_BASE_ADDR	0x014
#define REG_INT_STATUS		0x018
#define REG_INT_CLEAR		0x01C

#define REG_PAGE_FAULT_ADDR	0x024
#define REG_AW_FAULT_ADDR	0x028
#define REG_AR_FAULT_ADDR	0x02C
#define REG_DEFAULT_SLAVE_ADDR	0x030

#define REG_MMU_VERSION		0x034

#define MMU_MAJ_VER(val)	((val) >> 7)
#define MMU_MIN_VER(val)	((val) & 0x7F)
#define MMU_RAW_VER(reg)	(((reg) >> 21) & ((1 << 11) - 1)) /* 11 bits */

#define MAKE_MMU_VER(maj, min)	((((maj) & 0xF) << 7) | ((min) & 0x7F))

#define REG_PB0_SADDR		0x04C
#define REG_PB0_EADDR		0x050
#define REG_PB1_SADDR		0x054
#define REG_PB1_EADDR		0x058

#define has_sysmmu(dev)		(dev->archdata.iommu != NULL)

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

enum exynos_sysmmu_inttype {
	SYSMMU_PAGEFAULT,
	SYSMMU_AR_MULTIHIT,
	SYSMMU_AW_MULTIHIT,
	SYSMMU_BUSERROR,
	SYSMMU_AR_SECURITY,
	SYSMMU_AR_ACCESS,
	SYSMMU_AW_SECURITY,
	SYSMMU_AW_PROTECTION, /* 7 */
	SYSMMU_FAULT_UNKNOWN,
	SYSMMU_FAULTS_NUM
};

static unsigned short fault_reg_offset[SYSMMU_FAULTS_NUM] = {
	REG_PAGE_FAULT_ADDR,
	REG_AR_FAULT_ADDR,
	REG_AW_FAULT_ADDR,
	REG_DEFAULT_SLAVE_ADDR,
	REG_AR_FAULT_ADDR,
	REG_AR_FAULT_ADDR,
	REG_AW_FAULT_ADDR,
	REG_AW_FAULT_ADDR
};

static char *sysmmu_fault_name[SYSMMU_FAULTS_NUM] = {
	"PAGE FAULT",
	"AR MULTI-HIT FAULT",
	"AW MULTI-HIT FAULT",
	"BUS ERROR",
	"AR SECURITY PROTECTION FAULT",
	"AR ACCESS PROTECTION FAULT",
	"AW SECURITY PROTECTION FAULT",
	"AW ACCESS PROTECTION FAULT",
	"UNKNOWN FAULT"
};

/*
 * This structure is attached to dev.archdata.iommu of the master device
 * on device add, contains a list of SYSMMU controllers defined by device tree,
 * which are bound to given master device. It is usually referenced by 'owner'
 * pointer.
*/
struct exynos_iommu_owner {
	struct list_head controllers;	/* list of sysmmu_drvdata.owner_node */
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

static void sysmmu_unblock(void __iomem *sfrbase)
{
	__raw_writel(CTRL_ENABLE, sfrbase + REG_MMU_CTRL);
}

static bool sysmmu_block(void __iomem *sfrbase)
{
	int i = 120;

	__raw_writel(CTRL_BLOCK, sfrbase + REG_MMU_CTRL);
	while ((i > 0) && !(__raw_readl(sfrbase + REG_MMU_STATUS) & 1))
		--i;

	if (!(__raw_readl(sfrbase + REG_MMU_STATUS) & 1)) {
		sysmmu_unblock(sfrbase);
		return false;
	}

	return true;
}

static void __sysmmu_tlb_invalidate(void __iomem *sfrbase)
{
	__raw_writel(0x1, sfrbase + REG_MMU_FLUSH);
}

static void __sysmmu_tlb_invalidate_entry(void __iomem *sfrbase,
				sysmmu_iova_t iova, unsigned int num_inv)
{
	unsigned int i;

	for (i = 0; i < num_inv; i++) {
		__raw_writel((iova & SPAGE_MASK) | 1,
				sfrbase + REG_MMU_FLUSH_ENTRY);
		iova += SPAGE_SIZE;
	}
}

static void __sysmmu_set_ptbase(void __iomem *sfrbase,
				       phys_addr_t pgd)
{
	__raw_writel(pgd, sfrbase + REG_PT_BASE_ADDR);

	__sysmmu_tlb_invalidate(sfrbase);
}

static void show_fault_information(const char *name,
		enum exynos_sysmmu_inttype itype,
		phys_addr_t pgtable_base, sysmmu_iova_t fault_addr)
{
	sysmmu_pte_t *ent;

	if ((itype >= SYSMMU_FAULTS_NUM) || (itype < SYSMMU_PAGEFAULT))
		itype = SYSMMU_FAULT_UNKNOWN;

	pr_err("%s occurred at %#x by %s(Page table base: %pa)\n",
		sysmmu_fault_name[itype], fault_addr, name, &pgtable_base);

	ent = section_entry(phys_to_virt(pgtable_base), fault_addr);
	pr_err("\tLv1 entry: %#x\n", *ent);

	if (lv1ent_page(ent)) {
		ent = page_entry(ent, fault_addr);
		pr_err("\t Lv2 entry: %#x\n", *ent);
	}
}

static irqreturn_t exynos_sysmmu_irq(int irq, void *dev_id)
{
	/* SYSMMU is in blocked state when interrupt occurred. */
	struct sysmmu_drvdata *data = dev_id;
	enum exynos_sysmmu_inttype itype;
	sysmmu_iova_t addr = -1;
	int ret = -ENOSYS;

	WARN_ON(!is_sysmmu_active(data));

	spin_lock(&data->lock);

	if (!IS_ERR(data->clk_master))
		clk_enable(data->clk_master);

	itype = (enum exynos_sysmmu_inttype)
		__ffs(__raw_readl(data->sfrbase + REG_INT_STATUS));
	if (WARN_ON(!((itype >= 0) && (itype < SYSMMU_FAULT_UNKNOWN))))
		itype = SYSMMU_FAULT_UNKNOWN;
	else
		addr = __raw_readl(data->sfrbase + fault_reg_offset[itype]);

	if (itype == SYSMMU_FAULT_UNKNOWN) {
		pr_err("%s: Fault is not occurred by System MMU '%s'!\n",
			__func__, dev_name(data->sysmmu));
		pr_err("%s: Please check if IRQ is correctly configured.\n",
			__func__);
		BUG();
	} else {
		unsigned int base =
				__raw_readl(data->sfrbase + REG_PT_BASE_ADDR);
		show_fault_information(dev_name(data->sysmmu),
					itype, base, addr);
		if (data->domain)
			ret = report_iommu_fault(&data->domain->domain,
					data->master, addr, itype);
	}

	/* fault is not recovered by fault handler */
	BUG_ON(ret != 0);

	__raw_writel(1 << itype, data->sfrbase + REG_INT_CLEAR);

	sysmmu_unblock(data->sfrbase);

	if (!IS_ERR(data->clk_master))
		clk_disable(data->clk_master);

	spin_unlock(&data->lock);

	return IRQ_HANDLED;
}

static void __sysmmu_disable_nocount(struct sysmmu_drvdata *data)
{
	if (!IS_ERR(data->clk_master))
		clk_enable(data->clk_master);

	__raw_writel(CTRL_DISABLE, data->sfrbase + REG_MMU_CTRL);
	__raw_writel(0, data->sfrbase + REG_MMU_CFG);

	clk_disable(data->clk);
	if (!IS_ERR(data->clk_master))
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
	unsigned int cfg = CFG_LRU | CFG_QOS(15);
	unsigned int ver;

	ver = MMU_RAW_VER(__raw_readl(data->sfrbase + REG_MMU_VERSION));
	if (MMU_MAJ_VER(ver) == 3) {
		if (MMU_MIN_VER(ver) >= 2) {
			cfg |= CFG_FLPDCACHE;
			if (MMU_MIN_VER(ver) == 3) {
				cfg |= CFG_ACGEN;
				cfg &= ~CFG_LRU;
			} else {
				cfg |= CFG_SYSSEL;
			}
		}
	}

	__raw_writel(cfg, data->sfrbase + REG_MMU_CFG);
	data->version = ver;
}

static void __sysmmu_enable_nocount(struct sysmmu_drvdata *data)
{
	if (!IS_ERR(data->clk_master))
		clk_enable(data->clk_master);
	clk_enable(data->clk);

	__raw_writel(CTRL_BLOCK, data->sfrbase + REG_MMU_CTRL);

	__sysmmu_init_config(data);

	__sysmmu_set_ptbase(data->sfrbase, data->pgtable);

	__raw_writel(CTRL_ENABLE, data->sfrbase + REG_MMU_CTRL);

	if (!IS_ERR(data->clk_master))
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

static void __sysmmu_tlb_invalidate_flpdcache(struct sysmmu_drvdata *data,
					      sysmmu_iova_t iova)
{
	if (data->version == MAKE_MMU_VER(3, 3))
		__raw_writel(iova | 0x1, data->sfrbase + REG_MMU_FLUSH_ENTRY);
}

static void sysmmu_tlb_invalidate_flpdcache(struct sysmmu_drvdata *data,
					    sysmmu_iova_t iova)
{
	unsigned long flags;

	if (!IS_ERR(data->clk_master))
		clk_enable(data->clk_master);

	spin_lock_irqsave(&data->lock, flags);
	if (is_sysmmu_active(data))
		__sysmmu_tlb_invalidate_flpdcache(data, iova);
	spin_unlock_irqrestore(&data->lock, flags);

	if (!IS_ERR(data->clk_master))
		clk_disable(data->clk_master);
}

static void sysmmu_tlb_invalidate_entry(struct sysmmu_drvdata *data,
					sysmmu_iova_t iova, size_t size)
{
	unsigned long flags;

	spin_lock_irqsave(&data->lock, flags);
	if (is_sysmmu_active(data)) {
		unsigned int num_inv = 1;

		if (!IS_ERR(data->clk_master))
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

		if (sysmmu_block(data->sfrbase)) {
			__sysmmu_tlb_invalidate_entry(
				data->sfrbase, iova, num_inv);
			sysmmu_unblock(data->sfrbase);
		}
		if (!IS_ERR(data->clk_master))
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
	if (IS_ERR(data->clk)) {
		dev_err(dev, "Failed to get clock!\n");
		return PTR_ERR(data->clk);
	} else  {
		ret = clk_prepare(data->clk);
		if (ret) {
			dev_err(dev, "Failed to prepare clk\n");
			return ret;
		}
	}

	data->clk_master = devm_clk_get(dev, "master");
	if (!IS_ERR(data->clk_master)) {
		ret = clk_prepare(data->clk_master);
		if (ret) {
			clk_unprepare(data->clk);
			dev_err(dev, "Failed to prepare master's clk\n");
			return ret;
		}
	}

	data->sysmmu = dev;
	spin_lock_init(&data->lock);

	platform_set_drvdata(pdev, data);

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
		.suppress_bind_attrs = true,
	}
};

static inline void pgtable_flush(void *vastart, void *vaend)
{
	dmac_flush_range(vastart, vaend);
	outer_flush_range(virt_to_phys(vastart),
				virt_to_phys(vaend));
}

static struct iommu_domain *exynos_iommu_domain_alloc(unsigned type)
{
	struct exynos_iommu_domain *domain;
	int i;

	if (type != IOMMU_DOMAIN_UNMANAGED)
		return NULL;

	domain = kzalloc(sizeof(*domain), GFP_KERNEL);
	if (!domain)
		return NULL;

	domain->pgtable = (sysmmu_pte_t *)__get_free_pages(GFP_KERNEL, 2);
	if (!domain->pgtable)
		goto err_pgtable;

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

	pgtable_flush(domain->pgtable, domain->pgtable + NUM_LV1ENTRIES);

	spin_lock_init(&domain->lock);
	spin_lock_init(&domain->pgtablelock);
	INIT_LIST_HEAD(&domain->clients);

	domain->domain.geometry.aperture_start = 0;
	domain->domain.geometry.aperture_end   = ~0UL;
	domain->domain.geometry.force_aperture = true;

	return &domain->domain;

err_counter:
	free_pages((unsigned long)domain->pgtable, 2);
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

	for (i = 0; i < NUM_LV1ENTRIES; i++)
		if (lv1ent_page(domain->pgtable + i))
			kmem_cache_free(lv2table_kmem_cache,
				phys_to_virt(lv2table_base(domain->pgtable + i)));

	free_pages((unsigned long)domain->pgtable, 2);
	free_pages((unsigned long)domain->lv2entcnt, 1);
	kfree(domain);
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

	dev_dbg(dev, "%s: Attached IOMMU with pgtable %pa %s\n",
		__func__, &pagetable, (ret == 0) ? "" : ", again");

	return ret;
}

static void exynos_iommu_detach_device(struct iommu_domain *iommu_domain,
				    struct device *dev)
{
	struct exynos_iommu_domain *domain = to_exynos_domain(iommu_domain);
	phys_addr_t pagetable = virt_to_phys(domain->pgtable);
	struct sysmmu_drvdata *data, *next;
	unsigned long flags;
	bool found = false;

	if (!has_sysmmu(dev))
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

	if (found)
		dev_dbg(dev, "%s: Detached IOMMU with pgtable %pa\n",
					__func__, &pagetable);
	else
		dev_err(dev, "%s: No IOMMU is attached\n", __func__);
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
		BUG_ON((unsigned int)pent & (LV2TABLE_SIZE - 1));
		if (!pent)
			return ERR_PTR(-ENOMEM);

		*sent = mk_lv1ent_page(virt_to_phys(pent));
		kmemleak_ignore(pent);
		*pgcounter = NUM_LV2ENTRIES;
		pgtable_flush(pent, pent + NUM_LV2ENTRIES);
		pgtable_flush(sent, sent + 1);

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

	*sent = mk_lv1ent_sect(paddr);

	pgtable_flush(sent, sent + 1);

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

		*pent = mk_lv2ent_spage(paddr);
		pgtable_flush(pent, pent + 1);
		*pgcnt -= 1;
	} else { /* size == LPAGE_SIZE */
		int i;

		for (i = 0; i < SPAGES_PER_LPAGE; i++, pent++) {
			if (WARN_ON(!lv2ent_fault(pent))) {
				if (i > 0)
					memset(pent - i, 0, sizeof(*pent) * i);
				return -EADDRINUSE;
			}

			*pent = mk_lv2ent_lpage(paddr);
		}
		pgtable_flush(pent - SPAGES_PER_LPAGE, pent);
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
		*ent = ZERO_LV2LINK;
		pgtable_flush(ent, ent + 1);
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
		*ent = 0;
		size = SPAGE_SIZE;
		pgtable_flush(ent, ent + 1);
		domain->lv2entcnt[lv1ent_offset(iova)] += 1;
		goto done;
	}

	/* lv1ent_large(ent) == true here */
	if (WARN_ON(size < LPAGE_SIZE)) {
		err_pgsize = LPAGE_SIZE;
		goto err;
	}

	memset(ent, 0, sizeof(*ent) * SPAGES_PER_LPAGE);
	pgtable_flush(ent, ent + SPAGES_PER_LPAGE);

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

static int exynos_iommu_add_device(struct device *dev)
{
	struct iommu_group *group;
	int ret;

	if (!has_sysmmu(dev))
		return -ENODEV;

	group = iommu_group_get(dev);

	if (!group) {
		group = iommu_group_alloc();
		if (IS_ERR(group)) {
			dev_err(dev, "Failed to allocate IOMMU group\n");
			return PTR_ERR(group);
		}
	}

	ret = iommu_group_add_device(group, dev);
	iommu_group_put(group);

	return ret;
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

	of_iommu_set_ops(np, &exynos_iommu_ops);
	return 0;
}

IOMMU_OF_DECLARE(exynos_iommu_of, "samsung,exynos-sysmmu",
		 exynos_iommu_of_setup);
