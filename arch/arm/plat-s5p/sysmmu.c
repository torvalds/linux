/* linux/arch/arm/plat-s5p/sysmmu.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/export.h>

#include <asm/pgtable.h>

#include <mach/map.h>
#include <mach/regs-sysmmu.h>
#include <plat/sysmmu.h>

#define CTRL_ENABLE	0x5
#define CTRL_BLOCK	0x7
#define CTRL_DISABLE	0x0

static struct device *dev;

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
	"AW ACCESS PROTECTION FAULT"
};

static int (*fault_handlers[S5P_SYSMMU_TOTAL_IPNUM])(
		enum S5P_SYSMMU_INTERRUPT_TYPE itype,
		unsigned long pgtable_base,
		unsigned long fault_addr);

/*
 * If adjacent 2 bits are true, the system MMU is enabled.
 * The system MMU is disabled, otherwise.
 */
static unsigned long sysmmu_states;

static inline void set_sysmmu_active(sysmmu_ips ips)
{
	sysmmu_states |= 3 << (ips * 2);
}

static inline void set_sysmmu_inactive(sysmmu_ips ips)
{
	sysmmu_states &= ~(3 << (ips * 2));
}

static inline int is_sysmmu_active(sysmmu_ips ips)
{
	return sysmmu_states & (3 << (ips * 2));
}

static void __iomem *sysmmusfrs[S5P_SYSMMU_TOTAL_IPNUM];

static inline void sysmmu_block(sysmmu_ips ips)
{
	__raw_writel(CTRL_BLOCK, sysmmusfrs[ips] + S5P_MMU_CTRL);
	dev_dbg(dev, "%s is blocked.\n", sysmmu_ips_name[ips]);
}

static inline void sysmmu_unblock(sysmmu_ips ips)
{
	__raw_writel(CTRL_ENABLE, sysmmusfrs[ips] + S5P_MMU_CTRL);
	dev_dbg(dev, "%s is unblocked.\n", sysmmu_ips_name[ips]);
}

static inline void __sysmmu_tlb_invalidate(sysmmu_ips ips)
{
	__raw_writel(0x1, sysmmusfrs[ips] + S5P_MMU_FLUSH);
	dev_dbg(dev, "TLB of %s is invalidated.\n", sysmmu_ips_name[ips]);
}

static inline void __sysmmu_set_ptbase(sysmmu_ips ips, unsigned long pgd)
{
	if (unlikely(pgd == 0)) {
		pgd = (unsigned long)ZERO_PAGE(0);
		__raw_writel(0x20, sysmmusfrs[ips] + S5P_MMU_CFG); /* 4KB LV1 */
	} else {
		__raw_writel(0x0, sysmmusfrs[ips] + S5P_MMU_CFG); /* 16KB LV1 */
	}

	__raw_writel(pgd, sysmmusfrs[ips] + S5P_PT_BASE_ADDR);

	dev_dbg(dev, "Page table base of %s is initialized with 0x%08lX.\n",
						sysmmu_ips_name[ips], pgd);
	__sysmmu_tlb_invalidate(ips);
}

void sysmmu_set_fault_handler(sysmmu_ips ips,
			int (*handler)(enum S5P_SYSMMU_INTERRUPT_TYPE itype,
					unsigned long pgtable_base,
					unsigned long fault_addr))
{
	BUG_ON(!((ips >= SYSMMU_MDMA) && (ips < S5P_SYSMMU_TOTAL_IPNUM)));
	fault_handlers[ips] = handler;
}

static irqreturn_t s5p_sysmmu_irq(int irq, void *dev_id)
{
	/* SYSMMU is in blocked when interrupt occurred. */
	unsigned long base = 0;
	sysmmu_ips ips = (sysmmu_ips)dev_id;
	enum S5P_SYSMMU_INTERRUPT_TYPE itype;

	itype = (enum S5P_SYSMMU_INTERRUPT_TYPE)
		__ffs(__raw_readl(sysmmusfrs[ips] + S5P_INT_STATUS));

	BUG_ON(!((itype >= 0) && (itype < 8)));

	dev_alert(dev, "%s occurred by %s.\n", sysmmu_fault_name[itype],
							sysmmu_ips_name[ips]);

	if (fault_handlers[ips]) {
		unsigned long addr;

		base = __raw_readl(sysmmusfrs[ips] + S5P_PT_BASE_ADDR);
		addr = __raw_readl(sysmmusfrs[ips] + fault_reg_offset[itype]);

		if (fault_handlers[ips](itype, base, addr)) {
			__raw_writel(1 << itype,
					sysmmusfrs[ips] + S5P_INT_CLEAR);
			dev_notice(dev, "%s from %s is resolved."
					" Retrying translation.\n",
				sysmmu_fault_name[itype], sysmmu_ips_name[ips]);
		} else {
			base = 0;
		}
	}

	sysmmu_unblock(ips);

	if (!base)
		dev_notice(dev, "%s from %s is not handled.\n",
			sysmmu_fault_name[itype], sysmmu_ips_name[ips]);

	return IRQ_HANDLED;
}

void s5p_sysmmu_set_tablebase_pgd(sysmmu_ips ips, unsigned long pgd)
{
	if (is_sysmmu_active(ips)) {
		sysmmu_block(ips);
		__sysmmu_set_ptbase(ips, pgd);
		sysmmu_unblock(ips);
	} else {
		dev_dbg(dev, "%s is disabled. "
			"Skipping initializing page table base.\n",
						sysmmu_ips_name[ips]);
	}
}

void s5p_sysmmu_enable(sysmmu_ips ips, unsigned long pgd)
{
	if (!is_sysmmu_active(ips)) {
		sysmmu_clk_enable(ips);

		__sysmmu_set_ptbase(ips, pgd);

		__raw_writel(CTRL_ENABLE, sysmmusfrs[ips] + S5P_MMU_CTRL);

		set_sysmmu_active(ips);
		dev_dbg(dev, "%s is enabled.\n", sysmmu_ips_name[ips]);
	} else {
		dev_dbg(dev, "%s is already enabled.\n", sysmmu_ips_name[ips]);
	}
}

void s5p_sysmmu_disable(sysmmu_ips ips)
{
	if (is_sysmmu_active(ips)) {
		__raw_writel(CTRL_DISABLE, sysmmusfrs[ips] + S5P_MMU_CTRL);
		set_sysmmu_inactive(ips);
		sysmmu_clk_disable(ips);
		dev_dbg(dev, "%s is disabled.\n", sysmmu_ips_name[ips]);
	} else {
		dev_dbg(dev, "%s is already disabled.\n", sysmmu_ips_name[ips]);
	}
}

void s5p_sysmmu_tlb_invalidate(sysmmu_ips ips)
{
	if (is_sysmmu_active(ips)) {
		sysmmu_block(ips);
		__sysmmu_tlb_invalidate(ips);
		sysmmu_unblock(ips);
	} else {
		dev_dbg(dev, "%s is disabled. "
			"Skipping invalidating TLB.\n", sysmmu_ips_name[ips]);
	}
}

static int s5p_sysmmu_probe(struct platform_device *pdev)
{
	int i, ret;
	struct resource *res, *mem;

	dev = &pdev->dev;

	for (i = 0; i < S5P_SYSMMU_TOTAL_IPNUM; i++) {
		int irq;

		sysmmu_clk_init(dev, i);
		sysmmu_clk_disable(i);

		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res) {
			dev_err(dev, "Failed to get the resource of %s.\n",
							sysmmu_ips_name[i]);
			ret = -ENODEV;
			goto err_res;
		}

		mem = request_mem_region(res->start, resource_size(res),
					 pdev->name);
		if (!mem) {
			dev_err(dev, "Failed to request the memory region of %s.\n",
							sysmmu_ips_name[i]);
			ret = -EBUSY;
			goto err_res;
		}

		sysmmusfrs[i] = ioremap(res->start, resource_size(res));
		if (!sysmmusfrs[i]) {
			dev_err(dev, "Failed to ioremap() for %s.\n",
							sysmmu_ips_name[i]);
			ret = -ENXIO;
			goto err_reg;
		}

		irq = platform_get_irq(pdev, i);
		if (irq <= 0) {
			dev_err(dev, "Failed to get the IRQ resource of %s.\n",
							sysmmu_ips_name[i]);
			ret = -ENOENT;
			goto err_map;
		}

		if (request_irq(irq, s5p_sysmmu_irq, IRQF_DISABLED,
						pdev->name, (void *)i)) {
			dev_err(dev, "Failed to request IRQ for %s.\n",
							sysmmu_ips_name[i]);
			ret = -ENOENT;
			goto err_map;
		}
	}

	return 0;

err_map:
	iounmap(sysmmusfrs[i]);
err_reg:
	release_mem_region(mem->start, resource_size(mem));
err_res:
	return ret;
}

static int s5p_sysmmu_remove(struct platform_device *pdev)
{
	return 0;
}
int s5p_sysmmu_runtime_suspend(struct device *dev)
{
	return 0;
}

int s5p_sysmmu_runtime_resume(struct device *dev)
{
	return 0;
}

const struct dev_pm_ops s5p_sysmmu_pm_ops = {
	.runtime_suspend	= s5p_sysmmu_runtime_suspend,
	.runtime_resume		= s5p_sysmmu_runtime_resume,
};

static struct platform_driver s5p_sysmmu_driver = {
	.probe		= s5p_sysmmu_probe,
	.remove		= s5p_sysmmu_remove,
	.driver		= {
		.owner		= THIS_MODULE,
		.name		= "s5p-sysmmu",
		.pm		= &s5p_sysmmu_pm_ops,
	}
};

static int __init s5p_sysmmu_init(void)
{
	return platform_driver_register(&s5p_sysmmu_driver);
}
arch_initcall(s5p_sysmmu_init);
