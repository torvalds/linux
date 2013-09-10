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

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/iommu.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/memblock.h>
#include <linux/export.h>
#include <linux/of.h>

#include <asm/cacheflush.h>
#include <asm/pgtable.h>

/* We does not consider super section mapping (16MB) */
#define SECT_ORDER 20
#define LPAGE_ORDER 16
#define SPAGE_ORDER 12

#define SECT_SIZE (1 << SECT_ORDER)
#define LPAGE_SIZE (1 << LPAGE_ORDER)
#define SPAGE_SIZE (1 << SPAGE_ORDER)

#define SECT_MASK (~(SECT_SIZE - 1))
#define LPAGE_MASK (~(LPAGE_SIZE - 1))
#define SPAGE_MASK (~(SPAGE_SIZE - 1))

#define lv1ent_fault(sent) (((*(sent) & 3) == 0) || ((*(sent) & 3) == 3))
#define lv1ent_page(sent) ((*(sent) & 3) == 1)
#define lv1ent_section(sent) ((*(sent) & 3) == 2)

#define lv2ent_fault(pent) ((*(pent) & 3) == 0)
#define lv2ent_small(pent) ((*(pent) & 2) == 2)
#define lv2ent_large(pent) ((*(pent) & 3) == 1)

#define section_phys(sent) (*(sent) & SECT_MASK)
#define section_offs(iova) ((iova) & 0xFFFFF)
#define lpage_phys(pent) (*(pent) & LPAGE_MASK)
#define lpage_offs(iova) ((iova) & 0xFFFF)
#define spage_phys(pent) (*(pent) & SPAGE_MASK)
#define spage_offs(iova) ((iova) & 0xFFF)

#define lv1ent_offset(iova) ((iova) >> SECT_ORDER)
#define lv2ent_offset(iova) (((iova) & 0xFF000) >> SPAGE_ORDER)

#define NUM_LV1ENTRIES 4096
#define NUM_LV2ENTRIES 256

#define LV2TABLE_SIZE (NUM_LV2ENTRIES * sizeof(long))

#define SPAGES_PER_LPAGE (LPAGE_SIZE / SPAGE_SIZE)

#define lv2table_base(sent) (*(sent) & 0xFFFFFC00)

#define mk_lv1ent_sect(pa) ((pa) | 2)
#define mk_lv1ent_page(pa) ((pa) | 1)
#define mk_lv2ent_lpage(pa) ((pa) | 1)
#define mk_lv2ent_spage(pa) ((pa) | 2)

#define CTRL_ENABLE	0x5
#define CTRL_BLOCK	0x7
#define CTRL_DISABLE	0x0

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

#define REG_PB0_SADDR		0x04C
#define REG_PB0_EADDR		0x050
#define REG_PB1_SADDR		0x054
#define REG_PB1_EADDR		0x058

#define __clk_gate_ctrl(data, clk, en)	do {		\
		if (data->clk)				\
			clk_##en##able(data->clk);	\
	} while (0)

#define __sysmmu_clk_enable(data)	__clk_gate_ctrl(data, clk, en)
#define __sysmmu_clk_disable(data)	__clk_gate_ctrl(data, clk, dis)
#define __master_clk_enable(data)	__clk_gate_ctrl(data, clk_master, en)
#define __master_clk_disable(data)	__clk_gate_ctrl(data, clk_master, dis)

static struct kmem_cache *lv2table_kmem_cache;

static unsigned long *section_entry(unsigned long *pgtable, unsigned long iova)
{
	return pgtable + lv1ent_offset(iova);
}

static unsigned long *page_entry(unsigned long *sent, unsigned long iova)
{
	return (unsigned long *)__va(lv2table_base(sent)) + lv2ent_offset(iova);
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

struct exynos_iommu_domain {
	struct list_head clients; /* list of sysmmu_drvdata.node */
	unsigned long *pgtable; /* lv1 page table, 16KB */
	short *lv2entcnt; /* free lv2 entry counter for each section */
	spinlock_t lock; /* lock for this structure */
	spinlock_t pgtablelock; /* lock for modifying page table @ pgtable */
};

struct sysmmu_drvdata {
	struct list_head node; /* entry of exynos_iommu_domain.clients */
	struct device *sysmmu;	/* System MMU's device descriptor */
	struct device *dev;	/* Owner of system MMU */
	void __iomem *sfrbase;
	struct clk *clk;
	struct clk *clk_master;
	int activations;
	rwlock_t lock;
	struct iommu_domain *domain;
	unsigned long pgtable;
};

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
				unsigned long iova, unsigned int num_inv)
{
	unsigned int i;
	for (i = 0; i < num_inv; i++) {
		__raw_writel((iova & SPAGE_MASK) | 1,
				sfrbase + REG_MMU_FLUSH_ENTRY);
		iova += SPAGE_SIZE;
	}
}

static void __sysmmu_set_ptbase(void __iomem *sfrbase,
				       unsigned long pgd)
{
	__raw_writel(0x1, sfrbase + REG_MMU_CFG); /* 16KB LV1, LRU */
	__raw_writel(pgd, sfrbase + REG_PT_BASE_ADDR);

	__sysmmu_tlb_invalidate(sfrbase);
}

static void show_fault_information(const char *name,
		enum exynos_sysmmu_inttype itype,
		unsigned long pgtable_base, unsigned long fault_addr)
{
	unsigned long *ent;

	if ((itype >= SYSMMU_FAULTS_NUM) || (itype < SYSMMU_PAGEFAULT))
		itype = SYSMMU_FAULT_UNKNOWN;

	pr_err("%s occurred at 0x%lx by %s(Page table base: 0x%lx)\n",
		sysmmu_fault_name[itype], fault_addr, name, pgtable_base);

	ent = section_entry(__va(pgtable_base), fault_addr);
	pr_err("\tLv1 entry: 0x%lx\n", *ent);

	if (lv1ent_page(ent)) {
		ent = page_entry(ent, fault_addr);
		pr_err("\t Lv2 entry: 0x%lx\n", *ent);
	}
}

static irqreturn_t exynos_sysmmu_irq(int irq, void *dev_id)
{
	/* SYSMMU is in blocked when interrupt occurred. */
	struct sysmmu_drvdata *data = dev_id;
	enum exynos_sysmmu_inttype itype;
	unsigned long addr = -1;
	int ret = -ENOSYS;

	read_lock(&data->lock);

	WARN_ON(!is_sysmmu_active(data));

	__master_clk_enable(data);
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
		unsigned long base =
				__raw_readl(data->sfrbase + REG_PT_BASE_ADDR);
		show_fault_information(dev_name(data->sysmmu),
					itype, base, addr);
		if (data->domain)
			ret = report_iommu_fault(data->domain,
					data->dev, addr, itype);
	}

	/* fault is not recovered by fault handler */
	BUG_ON(ret != 0);

	__raw_writel(1 << itype, data->sfrbase + REG_INT_CLEAR);

	sysmmu_unblock(data->sfrbase);

	__master_clk_disable(data);

	read_unlock(&data->lock);

	return IRQ_HANDLED;
}

static bool __exynos_sysmmu_disable(struct sysmmu_drvdata *data)
{
	unsigned long flags;
	bool disabled = false;

	write_lock_irqsave(&data->lock, flags);

	if (!set_sysmmu_inactive(data))
		goto finish;

	__master_clk_enable(data);

	__raw_writel(CTRL_DISABLE, data->sfrbase + REG_MMU_CTRL);

	__sysmmu_clk_disable(data);
	__master_clk_disable(data);

	disabled = true;
	data->pgtable = 0;
	data->domain = NULL;
finish:
	write_unlock_irqrestore(&data->lock, flags);

	if (disabled)
		dev_dbg(data->sysmmu, "Disabled\n");
	else
		dev_dbg(data->sysmmu, "%d times left to be disabled\n",
					data->activations);

	return disabled;
}

/* __exynos_sysmmu_enable: Enables System MMU
 *
 * returns -error if an error occurred and System MMU is not enabled,
 * 0 if the System MMU has been just enabled and 1 if System MMU was already
 * enabled before.
 */
static int __exynos_sysmmu_enable(struct sysmmu_drvdata *data,
			unsigned long pgtable, struct iommu_domain *domain)
{
	int ret = 0;
	unsigned long flags;

	write_lock_irqsave(&data->lock, flags);

	if (!set_sysmmu_active(data)) {
		if (WARN_ON(pgtable != data->pgtable)) {
			ret = -EBUSY;
			set_sysmmu_inactive(data);
		} else {
			ret = 1;
		}

		dev_dbg(data->sysmmu, "Already enabled\n");
		goto finish;
	}

	data->pgtable = pgtable;

	__master_clk_enable(data);
	__sysmmu_clk_enable(data);

	__sysmmu_set_ptbase(data->sfrbase, pgtable);

	__raw_writel(CTRL_ENABLE, data->sfrbase + REG_MMU_CTRL);

	__master_clk_disable(data);

	data->domain = domain;

	dev_dbg(data->sysmmu, "Enabled\n");
finish:
	write_unlock_irqrestore(&data->lock, flags);

	return ret;
}

int exynos_sysmmu_enable(struct device *dev, unsigned long pgtable)
{
	struct sysmmu_drvdata *data = dev_get_drvdata(dev->archdata.iommu);
	int ret;

	BUG_ON(!memblock_is_memory(pgtable));

	ret = pm_runtime_get_sync(data->sysmmu);
	if (ret < 0) {
		dev_dbg(data->sysmmu, "Failed to enable\n");
		return ret;
	}

	ret = __exynos_sysmmu_enable(data, pgtable, NULL);
	if (WARN_ON(ret < 0)) {
		pm_runtime_put(data->sysmmu);
		dev_err(data->sysmmu, "Already enabled with page table %#lx\n",
			data->pgtable);
	} else {
		data->dev = dev;
	}

	return ret;
}

static bool exynos_sysmmu_disable(struct device *dev)
{
	struct sysmmu_drvdata *data = dev_get_drvdata(dev->archdata.iommu);
	bool disabled;

	disabled = __exynos_sysmmu_disable(data);
	pm_runtime_put(data->sysmmu);

	return disabled;
}

static void sysmmu_tlb_invalidate_entry(struct device *dev, unsigned long iova,
					size_t size)
{
	unsigned long flags;
	struct sysmmu_drvdata *data = dev_get_drvdata(dev->archdata.iommu);

	read_lock_irqsave(&data->lock, flags);

	if (is_sysmmu_active(data)) {
		unsigned int maj;
		unsigned int num_inv = 1;

		__master_clk_enable(data);

		maj = __raw_readl(data->sfrbase + REG_MMU_VERSION);
		/*
		 * L2TLB invalidation required
		 * 4KB page: 1 invalidation
		 * 64KB page: 16 invalidation
		 * 1MB page: 64 invalidation
		 * because it is set-associative TLB
		 * with 8-way and 64 sets.
		 * 1MB page can be cached in one of all sets.
		 * 64KB page can be one of 16 consecutive sets.
		 */
		if ((maj >> 28) == 2) /* major version number */
			num_inv = min_t(unsigned int, size / PAGE_SIZE, 64);

		if (sysmmu_block(data->sfrbase)) {
			__sysmmu_tlb_invalidate_entry(data->sfrbase, iova,
							num_inv);
			sysmmu_unblock(data->sfrbase);
		}
		__master_clk_disable(data);
	} else {
		dev_dbg(data->sysmmu, "Disabled. Skipping invalidating TLB.\n");
	}

	read_unlock_irqrestore(&data->lock, flags);
}

void exynos_sysmmu_tlb_invalidate(struct device *dev)
{
	unsigned long flags;
	struct sysmmu_drvdata *data = dev_get_drvdata(dev->archdata.iommu);

	read_lock_irqsave(&data->lock, flags);

	if (is_sysmmu_active(data)) {
		__master_clk_enable(data);
		if (sysmmu_block(data->sfrbase)) {
			__sysmmu_tlb_invalidate(data->sfrbase);
			sysmmu_unblock(data->sfrbase);
		}
		__master_clk_disable(data);
	} else {
		dev_dbg(data->sysmmu, "Disabled. Skipping invalidating TLB.\n");
	}

	read_unlock_irqrestore(&data->lock, flags);
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
	if (!res) {
		dev_err(dev, "Unable to find IOMEM region\n");
		return -ENOENT;
	}

	data->sfrbase = devm_ioremap_resource(dev, res);
	if (IS_ERR(data->sfrbase))
		return PTR_ERR(data->sfrbase);

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_dbg(dev, "Unable to find IRQ resource\n");
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
		dev_info(dev, "No gate clock found!\n");
		data->clk = NULL;
	}

	ret = clk_prepare(data->clk);
	if (ret) {
		dev_err(dev, "Failed to prepare clk\n");
		return ret;
	}

	data->clk_master = devm_clk_get(dev, "master");
	if (IS_ERR(data->clk_master))
		data->clk_master = NULL;

	ret = clk_prepare(data->clk_master);
	if (ret) {
		clk_unprepare(data->clk);
		dev_err(dev, "Failed to prepare master's clk\n");
		return ret;
	}

	data->sysmmu = dev;
	rwlock_init(&data->lock);
	INIT_LIST_HEAD(&data->node);

	platform_set_drvdata(pdev, data);

	pm_runtime_enable(dev);

	dev_dbg(dev, "Probed and initialized\n");
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id sysmmu_of_match[] __initconst = {
	{ .compatible	= "samsung,sysmmu-v1", },
	{ .compatible	= "samsung,sysmmu-v2", },
	{ .compatible	= "samsung,sysmmu-v3.1", },
	{ .compatible	= "samsung,sysmmu-v3.2", },
	{ .compatible	= "samsung,sysmmu-v3.3", },
	{ },
};
#endif

static struct platform_driver exynos_sysmmu_driver __refdata = {
	.probe	= exynos_sysmmu_probe,
	.driver	= {
		.owner		= THIS_MODULE,
		.name		= "exynos-sysmmu",
		.of_match_table	= of_match_ptr(sysmmu_of_match),
	}
};

static inline void pgtable_flush(void *vastart, void *vaend)
{
	dmac_flush_range(vastart, vaend);
	outer_flush_range(virt_to_phys(vastart),
				virt_to_phys(vaend));
}

static int exynos_iommu_domain_init(struct iommu_domain *domain)
{
	struct exynos_iommu_domain *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->pgtable = (unsigned long *)__get_free_pages(
						GFP_KERNEL | __GFP_ZERO, 2);
	if (!priv->pgtable)
		goto err_pgtable;

	priv->lv2entcnt = (short *)__get_free_pages(
						GFP_KERNEL | __GFP_ZERO, 1);
	if (!priv->lv2entcnt)
		goto err_counter;

	pgtable_flush(priv->pgtable, priv->pgtable + NUM_LV1ENTRIES);

	spin_lock_init(&priv->lock);
	spin_lock_init(&priv->pgtablelock);
	INIT_LIST_HEAD(&priv->clients);

	domain->geometry.aperture_start = 0;
	domain->geometry.aperture_end   = ~0UL;
	domain->geometry.force_aperture = true;

	domain->priv = priv;
	return 0;

err_counter:
	free_pages((unsigned long)priv->pgtable, 2);
err_pgtable:
	kfree(priv);
	return -ENOMEM;
}

static void exynos_iommu_domain_destroy(struct iommu_domain *domain)
{
	struct exynos_iommu_domain *priv = domain->priv;
	struct sysmmu_drvdata *data;
	unsigned long flags;
	int i;

	WARN_ON(!list_empty(&priv->clients));

	spin_lock_irqsave(&priv->lock, flags);

	list_for_each_entry(data, &priv->clients, node) {
		while (!exynos_sysmmu_disable(data->dev))
			; /* until System MMU is actually disabled */
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	for (i = 0; i < NUM_LV1ENTRIES; i++)
		if (lv1ent_page(priv->pgtable + i))
			kmem_cache_free(lv2table_kmem_cache,
					__va(lv2table_base(priv->pgtable + i)));

	free_pages((unsigned long)priv->pgtable, 2);
	free_pages((unsigned long)priv->lv2entcnt, 1);
	kfree(domain->priv);
	domain->priv = NULL;
}

static int exynos_iommu_attach_device(struct iommu_domain *domain,
				   struct device *dev)
{
	struct sysmmu_drvdata *data = dev_get_drvdata(dev->archdata.iommu);
	struct exynos_iommu_domain *priv = domain->priv;
	unsigned long flags;
	int ret;

	ret = pm_runtime_get_sync(data->sysmmu);
	if (ret < 0)
		return ret;

	ret = 0;

	spin_lock_irqsave(&priv->lock, flags);

	ret = __exynos_sysmmu_enable(data, __pa(priv->pgtable), domain);

	if (ret == 0) {
		/* 'data->node' must not be appeared in priv->clients */
		BUG_ON(!list_empty(&data->node));
		data->dev = dev;
		list_add_tail(&data->node, &priv->clients);
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	if (ret < 0) {
		dev_err(dev, "%s: Failed to attach IOMMU with pgtable %#lx\n",
				__func__, __pa(priv->pgtable));
		pm_runtime_put(data->sysmmu);
	} else if (ret > 0) {
		dev_dbg(dev, "%s: IOMMU with pgtable 0x%lx already attached\n",
					__func__, __pa(priv->pgtable));
	} else {
		dev_dbg(dev, "%s: Attached new IOMMU with pgtable 0x%lx\n",
					__func__, __pa(priv->pgtable));
	}

	return ret;
}

static void exynos_iommu_detach_device(struct iommu_domain *domain,
				    struct device *dev)
{
	struct sysmmu_drvdata *data = dev_get_drvdata(dev->archdata.iommu);
	struct exynos_iommu_domain *priv = domain->priv;
	struct list_head *pos;
	unsigned long flags;
	bool found = false;

	spin_lock_irqsave(&priv->lock, flags);

	list_for_each(pos, &priv->clients) {
		if (list_entry(pos, struct sysmmu_drvdata, node) == data) {
			found = true;
			break;
		}
	}

	if (!found)
		goto finish;

	if (__exynos_sysmmu_disable(data)) {
		dev_dbg(dev, "%s: Detached IOMMU with pgtable %#lx\n",
					__func__, __pa(priv->pgtable));
		list_del_init(&data->node);

	} else {
		dev_dbg(dev, "%s: Detaching IOMMU with pgtable %#lx delayed",
					__func__, __pa(priv->pgtable));
	}

finish:
	spin_unlock_irqrestore(&priv->lock, flags);

	if (found)
		pm_runtime_put(data->sysmmu);
}

static unsigned long *alloc_lv2entry(unsigned long *sent, unsigned long iova,
					short *pgcounter)
{
	if (lv1ent_section(sent)) {
		WARN(1, "Trying mapping on %#08lx mapped with 1MiB page", iova);
		return ERR_PTR(-EADDRINUSE);
	}

	if (lv1ent_fault(sent)) {
		unsigned long *pent;

		pent = kmem_cache_zalloc(lv2table_kmem_cache, GFP_ATOMIC);
		BUG_ON((unsigned long)pent & (LV2TABLE_SIZE - 1));
		if (!pent)
			return ERR_PTR(-ENOMEM);

		*sent = mk_lv1ent_page(__pa(pent));
		*pgcounter = NUM_LV2ENTRIES;
		pgtable_flush(pent, pent + NUM_LV2ENTRIES);
		pgtable_flush(sent, sent + 1);
	}

	return page_entry(sent, iova);
}

static int lv1set_section(unsigned long *sent, unsigned long iova,
			  phys_addr_t paddr, short *pgcnt)
{
	if (lv1ent_section(sent)) {
		WARN(1, "Trying mapping on 1MiB@%#08lx that is mapped",
			iova);
		return -EADDRINUSE;
	}

	if (lv1ent_page(sent)) {
		if (*pgcnt != NUM_LV2ENTRIES) {
			WARN(1, "Trying mapping on 1MiB@%#08lx that is mapped",
				iova);
			return -EADDRINUSE;
		}

		kmem_cache_free(lv2table_kmem_cache, page_entry(sent, 0));
		*pgcnt = 0;
	}

	*sent = mk_lv1ent_sect(paddr);

	pgtable_flush(sent, sent + 1);

	return 0;
}

static int lv2set_page(unsigned long *pent, phys_addr_t paddr, size_t size,
								short *pgcnt)
{
	if (size == SPAGE_SIZE) {
		if (!lv2ent_fault(pent)) {
			WARN(1, "Trying mapping on 4KiB where mapping exists");
			return -EADDRINUSE;
		}

		*pent = mk_lv2ent_spage(paddr);
		pgtable_flush(pent, pent + 1);
		*pgcnt -= 1;
	} else { /* size == LPAGE_SIZE */
		int i;
		for (i = 0; i < SPAGES_PER_LPAGE; i++, pent++) {
			if (!lv2ent_fault(pent)) {
				WARN(1,
				"Trying mapping on 64KiB where mapping exists");
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

static int exynos_iommu_map(struct iommu_domain *domain, unsigned long iova,
			 phys_addr_t paddr, size_t size, int prot)
{
	struct exynos_iommu_domain *priv = domain->priv;
	unsigned long *entry;
	unsigned long flags;
	int ret = -ENOMEM;

	BUG_ON(priv->pgtable == NULL);

	spin_lock_irqsave(&priv->pgtablelock, flags);

	entry = section_entry(priv->pgtable, iova);

	if (size == SECT_SIZE) {
		ret = lv1set_section(entry, iova, paddr,
					&priv->lv2entcnt[lv1ent_offset(iova)]);
	} else {
		unsigned long *pent;

		pent = alloc_lv2entry(entry, iova,
					&priv->lv2entcnt[lv1ent_offset(iova)]);

		if (IS_ERR(pent))
			ret = PTR_ERR(pent);
		else
			ret = lv2set_page(pent, paddr, size,
					&priv->lv2entcnt[lv1ent_offset(iova)]);
	}

	if (ret)
		pr_debug("%s: Failed to map iova 0x%lx/0x%x bytes\n",
							__func__, iova, size);

	spin_unlock_irqrestore(&priv->pgtablelock, flags);

	return ret;
}

static size_t exynos_iommu_unmap(struct iommu_domain *domain,
					       unsigned long iova, size_t size)
{
	struct exynos_iommu_domain *priv = domain->priv;
	struct sysmmu_drvdata *data;
	unsigned long flags;
	unsigned long *ent;
	size_t err_pgsize;

	BUG_ON(priv->pgtable == NULL);

	spin_lock_irqsave(&priv->pgtablelock, flags);

	ent = section_entry(priv->pgtable, iova);

	if (lv1ent_section(ent)) {
		if (size < SECT_SIZE) {
			err_pgsize = SECT_SIZE;
			goto err;
		}

		*ent = 0;
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
		priv->lv2entcnt[lv1ent_offset(iova)] += 1;
		goto done;
	}

	/* lv1ent_large(ent) == true here */
	if (size < LPAGE_SIZE) {
		err_pgsize = LPAGE_SIZE;
		goto err;
	}

	memset(ent, 0, sizeof(*ent) * SPAGES_PER_LPAGE);
	pgtable_flush(ent, ent + SPAGES_PER_LPAGE);

	size = LPAGE_SIZE;
	priv->lv2entcnt[lv1ent_offset(iova)] += SPAGES_PER_LPAGE;
done:
	spin_unlock_irqrestore(&priv->pgtablelock, flags);

	spin_lock_irqsave(&priv->lock, flags);
	list_for_each_entry(data, &priv->clients, node)
		sysmmu_tlb_invalidate_entry(data->dev, iova, size);
	spin_unlock_irqrestore(&priv->lock, flags);

	return size;
err:
	spin_unlock_irqrestore(&priv->pgtablelock, flags);

	WARN(1,
	"%s: Failed due to size(%#x) @ %#08lx is smaller than page size %#x\n",
	__func__, size, iova, err_pgsize);

	return 0;
}

static phys_addr_t exynos_iommu_iova_to_phys(struct iommu_domain *domain,
					  dma_addr_t iova)
{
	struct exynos_iommu_domain *priv = domain->priv;
	unsigned long *entry;
	unsigned long flags;
	phys_addr_t phys = 0;

	spin_lock_irqsave(&priv->pgtablelock, flags);

	entry = section_entry(priv->pgtable, iova);

	if (lv1ent_section(entry)) {
		phys = section_phys(entry) + section_offs(iova);
	} else if (lv1ent_page(entry)) {
		entry = page_entry(entry, iova);

		if (lv2ent_large(entry))
			phys = lpage_phys(entry) + lpage_offs(iova);
		else if (lv2ent_small(entry))
			phys = spage_phys(entry) + spage_offs(iova);
	}

	spin_unlock_irqrestore(&priv->pgtablelock, flags);

	return phys;
}

static struct iommu_ops exynos_iommu_ops = {
	.domain_init = &exynos_iommu_domain_init,
	.domain_destroy = &exynos_iommu_domain_destroy,
	.attach_dev = &exynos_iommu_attach_device,
	.detach_dev = &exynos_iommu_detach_device,
	.map = &exynos_iommu_map,
	.unmap = &exynos_iommu_unmap,
	.iova_to_phys = &exynos_iommu_iova_to_phys,
	.pgsize_bitmap = SECT_SIZE | LPAGE_SIZE | SPAGE_SIZE,
};

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

	ret = bus_set_iommu(&platform_bus_type, &exynos_iommu_ops);
	if (ret) {
		pr_err("%s: Failed to register exynos-iommu driver.\n",
								__func__);
		goto err_set_iommu;
	}

	return 0;
err_set_iommu:
	platform_driver_unregister(&exynos_sysmmu_driver);
err_reg_driver:
	kmem_cache_destroy(lv2table_kmem_cache);
	return ret;
}
subsys_initcall(exynos_iommu_init);
