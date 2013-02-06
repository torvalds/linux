/* linux/arch/arm/plat-s5p/sysmmu.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifdef CONFIG_S5P_SYSTEM_MMU_DEBUG
#define DEBUG
#endif

#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/mm.h>

#include <asm/pgtable.h>

#include <mach/map.h>
#include <mach/regs-sysmmu.h>

#include <plat/s5p-sysmmu.h>

#define CTRL_ENABLE	0x5
#define CTRL_BLOCK	0x7
#define CTRL_DISABLE	0x0

static unsigned short fault_reg_offset[SYSMMU_FAULTS_NUM] = {
	S5P_PAGE_FAULT_ADDR,
	S5P_AR_FAULT_ADDR,
	S5P_AW_FAULT_ADDR,
	S5P_DEFAULT_SLAVE_ADDR,
	S5P_AR_FAULT_ADDR,
	S5P_AR_FAULT_ADDR,
	S5P_AW_FAULT_ADDR,
	S5P_AW_FAULT_ADDR
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

static LIST_HEAD(sysmmu_list);

struct sysmmu_drvdata *get_sysmmu_data(struct device *owner,
						struct sysmmu_drvdata *start)
{
	if (start) {
		list_for_each_entry_continue(start, &sysmmu_list, node)
			if (start->owner == owner)
				return start;
	} else  {
		list_for_each_entry(start, &sysmmu_list, node)
			if (start->owner == owner)
				return start;
	}

	return NULL;
}

struct list_head *get_sysmmu_list(void)
{
	return &sysmmu_list;
}

static struct sysmmu_drvdata *get_sysmmu_data_rollback(struct device *owner,
						struct sysmmu_drvdata *start)
{
	if (start) {
		list_for_each_entry_continue_reverse(start, &sysmmu_list, node)
			if (start->owner == owner)
				return start;
	}

	return NULL;
}

static int set_sysmmu_active(struct sysmmu_drvdata *mmudata)
{
#ifndef CONFIG_S5P_SYSTEM_MMU_REFCOUNT
	if (WARN_ON(mmudata->activations == 1))
		return -EBUSY;
#endif
	mmudata->activations++;

	return 0;
}

static bool is_sysmmu_active(struct sysmmu_drvdata *mmudata)
{
	return mmudata->activations != 0;
}

static bool set_sysmmu_inactive(struct sysmmu_drvdata *mmudata)
{
	/* return true if the System MMU is needed to be disabled */
	if (WARN_ON(!is_sysmmu_active(mmudata)))
		return false;

	mmudata->activations--;

	return !is_sysmmu_active(mmudata);
}

#ifdef CONFIG_S5P_SYSTEM_MMU_REFCOUNT
static bool need_sysmmu_initialize(struct sysmmu_drvdata *mmudata)
{
	return mmudata->activations == 1;
}
#else
#define need_sysmmu_initialize is_sysmmu_active
#endif

static void sysmmu_block(void __iomem *sfrbase)
{
	__raw_writel(CTRL_BLOCK, sfrbase + S5P_MMU_CTRL);
}

static void sysmmu_unblock(void __iomem *sfrbase)
{
	__raw_writel(CTRL_ENABLE, sfrbase + S5P_MMU_CTRL);
}

static void __sysmmu_tlb_invalidate(void __iomem *sfrbase)
{
	__raw_writel(0x1, sfrbase + S5P_MMU_FLUSH);
}

static void __sysmmu_tlb_invalidate_entry(void __iomem *sfrbase,
						unsigned long iova)
{
	__raw_writel((iova & PAGE_MASK) | 1, sfrbase + S5P_MMU_FLUSH_ENTRY);
}

static void __sysmmu_set_ptbase(void __iomem *sfrbase,
				       unsigned long pgd)
{
	if (unlikely(pgd == 0)) {
		pgd = page_to_phys(ZERO_PAGE(0));
		__raw_writel(0x20, sfrbase + S5P_MMU_CFG); /* 4KB LV1 */
	} else {
		__raw_writel(0x0, sfrbase + S5P_MMU_CFG); /* 16KB LV1 */
	}

	__raw_writel(pgd, sfrbase + S5P_PT_BASE_ADDR);

	__sysmmu_tlb_invalidate(sfrbase);
}

static void __sysmmu_set_prefbuf(void __iomem *sfrbase, unsigned long base,
						unsigned long size, int idx)
{
	__raw_writel(base, sfrbase + S5P_PB0_SADDR + idx * 8);
	__raw_writel(size - 1 + base,  sfrbase + S5P_PB0_EADDR + idx * 8);
}

void s5p_sysmmu_set_prefbuf(struct device *owner,
				unsigned long base0, unsigned long size0,
				unsigned long base1, unsigned long size1)
{
	struct sysmmu_drvdata *data = NULL;
	unsigned long flags;

	BUG_ON((base0 + (size0 - 1)) <= base0);
	BUG_ON((base1 + (size1 - 1)) <= base1);

	while ((data = get_sysmmu_data(owner, data))) {
		if (WARN_ON(data->version != 3))
			continue;

		read_lock_irqsave(&data->lock, flags);
		if (is_sysmmu_active(data)) {
			sysmmu_block(data->sfrbase);

			if (size1 == 0) {
				if (size0 <= SZ_128K) {
					base1 = base0;
					size1 = size0;
				} else {
					size1 = size0 -
						ALIGN(size0 / 2, SZ_64K);
					size0 = size0 - size1;
					base1 = base0 + size0;
				}
			}

			__sysmmu_set_prefbuf(data->sfrbase, base0, size0, 0);
			__sysmmu_set_prefbuf(data->sfrbase, base1, size1, 1);

			sysmmu_unblock(data->sfrbase);
		}
		read_unlock_irqrestore(&data->lock, flags);
	}
}

static void __set_fault_handler(struct sysmmu_drvdata *mmudata,
					s5p_sysmmu_fault_handler_t handler)
{
	unsigned long flags;

	write_lock_irqsave(&mmudata->lock, flags);
	mmudata->fault_handler = handler;
	write_unlock_irqrestore(&mmudata->lock, flags);
}

void s5p_sysmmu_set_fault_handler(struct device *owner,
					s5p_sysmmu_fault_handler_t handler)
{
	struct sysmmu_drvdata *data = NULL;

	while ((data = get_sysmmu_data(owner, data)))
		__set_fault_handler(data, handler);
}

static int default_fault_handler(enum S5P_SYSMMU_INTERRUPT_TYPE itype,
			     unsigned long pgtable_base,
			     unsigned long fault_addr)
{
	unsigned long *ent;

	if ((itype >= SYSMMU_FAULTS_NUM) || (itype < SYSMMU_PAGEFAULT))
		itype = SYSMMU_FAULT_UNKNOWN;

	pr_err("%s occured at 0x%lx(Page table base: 0x%lx)\n",
			sysmmu_fault_name[itype], fault_addr, pgtable_base);

	pgtable_base += ((fault_addr & 0xFFF00000) >> 20) * 4;

	ent = page_address(phys_to_page(pgtable_base));
	ent += offset_in_page(pgtable_base) / sizeof(unsigned long) ;

	if (likely(ent != NULL)) {
		pr_err("\tLv1 entry: 0x%lx\n", *ent);

		if ((*ent & 0x3) == 0x1) {
			pgtable_base = *ent & ~0x3FF;
			ent = page_address(phys_to_page(pgtable_base));

			if (likely(ent != NULL)) {
				ent += offset_in_page(pgtable_base) /
						sizeof(unsigned long);
				ent += (fault_addr & 0xFF000) >> 12;
				pr_err("\tLv2 entry: 0x%lx\n", *ent);
			}
		}
	}

	pr_err("\t\tGenerating Kernel OOPS... because it is unrecoverable.\n");

	BUG();

	return 0;
}

static irqreturn_t s5p_sysmmu_irq(int irq, void *dev_id)
{
	/* SYSMMU is in blocked when interrupt occurred. */
	unsigned long base = 0;
	struct sysmmu_drvdata *mmudata = dev_id;
	enum S5P_SYSMMU_INTERRUPT_TYPE itype;

	read_lock(&mmudata->lock);

	WARN_ON(!is_sysmmu_active(mmudata));

	itype = (enum S5P_SYSMMU_INTERRUPT_TYPE)
		__ffs(__raw_readl(mmudata->sfrbase + S5P_INT_STATUS));

	BUG_ON(!((itype >= 0) && (itype < 8)));

	if (mmudata->fault_handler) {
		unsigned long addr;

		base = __raw_readl(mmudata->sfrbase + S5P_PT_BASE_ADDR);
		addr = __raw_readl(mmudata->sfrbase + fault_reg_offset[itype]);

		dev_dbg(mmudata->dev, "System MMU %s occurred by %s\n",
			sysmmu_fault_name[itype], dev_name(mmudata->owner));

		if ((mmudata->version == 3) && ((itype == SYSMMU_AR_MULTIHIT) ||
					(itype == SYSMMU_AW_MULTIHIT))) {
			__sysmmu_tlb_invalidate(mmudata->sfrbase);
		} else if (mmudata->fault_handler(itype, base, addr) != 0) {
			dev_dbg(mmudata->dev,
				"%s is resolved. Retrying translation.\n",
				sysmmu_fault_name[itype]);
		} else {
			base = 0;
		}

		__raw_writel(1 << itype, mmudata->sfrbase + S5P_INT_CLEAR);
	}

	sysmmu_unblock(mmudata->sfrbase);

	read_unlock(&mmudata->lock);

	if (!base)
		dev_notice(mmudata->dev, "%s is not handled.\n",
						sysmmu_fault_name[itype]);

	return IRQ_HANDLED;
}

void s5p_sysmmu_set_tablebase_pgd(struct device *owner, unsigned long pgd)
{
	struct sysmmu_drvdata *mmudata = NULL;

	s5p_sysmmu_tlb_invalidate(owner);

	while ((mmudata = get_sysmmu_data(owner, mmudata))) {
		unsigned long flags;

		read_lock_irqsave(&mmudata->lock, flags);

		if (is_sysmmu_active(mmudata)) {
			sysmmu_block(mmudata->sfrbase);
			__sysmmu_set_ptbase(mmudata->sfrbase, pgd);
			sysmmu_unblock(mmudata->sfrbase);
			dev_dbg(mmudata->dev, "New page table base is %p\n",
								(void *)pgd);
		} else {
			dev_dbg(mmudata->dev,
			"Disabled: Skipping setting page table base.\n");
		}

		read_unlock_irqrestore(&mmudata->lock, flags);
	}
}

static bool __sysmmu_disable(struct sysmmu_drvdata *data)
{
	unsigned long flags;
	bool disabled = false;

	write_lock_irqsave(&data->lock, flags);

	if (set_sysmmu_inactive(data)) {
		__raw_writel(CTRL_DISABLE, data->sfrbase + S5P_MMU_CTRL);
		if (data->clk)
		clk_disable(data->clk);
		disabled = true;
	}

	write_unlock_irqrestore(&data->lock, flags);

#ifndef CONFIG_S5P_SYSTEM_MMU_REFCOUNT
	if (disabled)
#endif
		pm_runtime_put_sync(data->dev);

	return disabled;
}

int s5p_sysmmu_enable(struct device *owner, unsigned long pgd)
{
	unsigned long flags;
	struct sysmmu_drvdata *mmudata = NULL;
	int ret = -ENODEV;

	/* There are some devices that control more System MMUs than one such
	 * as MFC.
	 */
	while ((mmudata = get_sysmmu_data(owner, mmudata))) {
		ret = pm_runtime_get_sync(mmudata->dev);
		if (ret < 0)
			break;

		write_lock_irqsave(&mmudata->lock, flags);

		ret = set_sysmmu_active(mmudata);
		if (!ret && need_sysmmu_initialize(mmudata)) {
			if (mmudata->clk)
			clk_enable(mmudata->clk);

			__sysmmu_set_ptbase(mmudata->sfrbase, pgd);

			if (mmudata->version == 0) {

				mmudata->version = readl(
					mmudata->sfrbase + S5P_MMU_VERSION);
				mmudata->version >>= 28;
			}

			if (mmudata->version == 3) {
				__raw_writel((1 << 12) | (2 << 28),
						mmudata->sfrbase + S5P_MMU_CFG);
				__sysmmu_set_prefbuf(mmudata->sfrbase,
								0, -1, 0);
				__sysmmu_set_prefbuf(mmudata->sfrbase,
								0, -1, 1);
			}

			__raw_writel(CTRL_ENABLE,
					mmudata->sfrbase + S5P_MMU_CTRL);

			dev_dbg(mmudata->dev, "Enabled.\n");
		} else {
			dev_dbg(mmudata->dev, "Already enabled.\n");
		}

		write_unlock_irqrestore(&mmudata->lock, flags);

		if (ret) /* already enabled and no refcount */
			pm_runtime_put_sync(mmudata->dev);
	}

	if (ret < 0) {
		while ((mmudata = get_sysmmu_data_rollback(owner, mmudata))) {
			__sysmmu_disable(mmudata);

			dev_dbg(mmudata->dev, "Failed to enable.\n");
		}
	} else {
		ret = 0;
	}

	return ret;
}

void s5p_sysmmu_disable(struct device *owner)
{
	struct sysmmu_drvdata *mmudata = NULL;

	while ((mmudata = get_sysmmu_data(owner, mmudata))) {
		if (__sysmmu_disable(mmudata))
			dev_dbg(mmudata->dev, "Disabled.\n");
		else
			dev_dbg(mmudata->dev,
					"Inactivation request ignorred\n");
	}
}

void s5p_sysmmu_tlb_invalidate(struct device *owner)
{
	struct sysmmu_drvdata *mmudata = NULL;

	while ((mmudata = get_sysmmu_data(owner, mmudata))) {
		unsigned long flags;

		read_lock_irqsave(&mmudata->lock, flags);

		if (is_sysmmu_active(mmudata)) {
			sysmmu_block(mmudata->sfrbase);
			__sysmmu_tlb_invalidate(mmudata->sfrbase);
			sysmmu_unblock(mmudata->sfrbase);
		} else {
			dev_dbg(mmudata->dev,
				"Disabled: Skipping invalidating TLB.\n");
		}

		read_unlock_irqrestore(&mmudata->lock, flags);
	}
}

void s5p_sysmmu_tlb_invalidate_entry(struct device *owner, unsigned long iova,
					unsigned int count,
					unsigned long page_size)
{
	struct sysmmu_drvdata *mmudata = NULL;

	while ((mmudata = get_sysmmu_data(owner, mmudata))) {
		unsigned long flags;

		read_lock_irqsave(&mmudata->lock, flags);

		if (is_sysmmu_active(mmudata)) {
			while (count > 0) {
				sysmmu_block(mmudata->sfrbase);
				__sysmmu_tlb_invalidate_entry(mmudata->sfrbase,
								iova);
				sysmmu_unblock(mmudata->sfrbase);
				count--;
				iova += page_size;
			}
		} else {
			dev_dbg(mmudata->dev,
				"Disabled: Skipping invalidating TLB.\n");
		}

		read_unlock_irqrestore(&mmudata->lock, flags);
	}
}

static int s5p_sysmmu_probe(struct platform_device *pdev)
{
	struct resource *res, *ioarea;
	int ret;
	int irq;
	struct device *dev;
	void *sfr;
	struct sysmmu_drvdata *data;

	dev = &pdev->dev;

	if (!dev_get_platdata(dev)) {
		dev_dbg(dev, "Skipping probing - No owner device.\n");
		return -ENODEV;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(dev, "Not enough memory for private data.\n");
		return -ENOMEM;
	}

	data->owner = dev_get_platdata(dev);

	ret = dev_set_drvdata(dev, data);
	if (ret) {
		dev_err(dev, "Unable to set driver's private data.\n");
		goto err_init;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Failed to get resource.\n");
		goto err_init;
	}

	ioarea = request_mem_region(res->start, resource_size(res),
								dev_name(dev));
	if (ioarea == NULL) {
		dev_err(dev, "Failed to request memory region.\n");
		goto err_init;
	}

	sfr = ioremap(res->start, resource_size(res));
	if (!sfr) {
		dev_err(dev, "Failed to map IO area\n");
		ret = -ENOENT;
		goto err_ioremap;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(dev, "Failed to get irq resource.\n");
		ret = irq;
		goto err_irq;
	}

	ret = request_irq(irq, s5p_sysmmu_irq, 0, dev_name(dev), data);
	if (ret) {
		dev_err(dev, "Failed to request irq.\n");
		goto err_irq;
	}

	data->clk = clk_get(dev, "sysmmu");
	if (IS_ERR(data->clk)) {
		dev_dbg(dev, "Clock descriptor not found:"
				" Skipping clock gating...\n");
		data->clk = NULL;
	}

	data->dev = dev;
	data->sfrbase = sfr;
	rwlock_init(&data->lock);
	INIT_LIST_HEAD(&data->node);

	__set_fault_handler(data, &default_fault_handler);
	list_add(&data->node, &sysmmu_list);

	if (dev->parent)
		pm_runtime_enable(dev);

	if (to_platform_device(data->owner)->id == -1)
		dev_info(dev, "Initialized for %s.\n",
					to_platform_device(data->owner)->name);
	else
		dev_info(dev, "Initialized for %s.%d.\n",
					to_platform_device(data->owner)->name,
					to_platform_device(data->owner)->id);
	return 0;
err_irq:
	iounmap(sfr);
err_ioremap:
	release_resource(ioarea);
	kfree(ioarea);
err_init:
	kfree(data);
	dev_err(dev, "Probing system MMU failed!");
	return ret;
}

static struct platform_driver s5p_sysmmu_driver = {
	.probe		= s5p_sysmmu_probe,
	.driver		= {
		.owner		= THIS_MODULE,
		.name		= "s5p-sysmmu",
	}
};

static int __init s5p_sysmmu_init(void)
{
	return platform_driver_register(&s5p_sysmmu_driver);
}
subsys_initcall(s5p_sysmmu_init);
