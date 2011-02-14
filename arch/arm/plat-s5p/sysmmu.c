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

#include <mach/map.h>
#include <mach/regs-sysmmu.h>
#include <mach/sysmmu.h>

struct sysmmu_controller s5p_sysmmu_cntlrs[S5P_SYSMMU_TOTAL_IPNUM];

void s5p_sysmmu_register(struct sysmmu_controller *sysmmuconp)
{
	unsigned int reg_mmu_ctrl;
	unsigned int reg_mmu_status;
	unsigned int reg_pt_base_addr;
	unsigned int reg_int_status;
	unsigned int reg_page_ft_addr;

	reg_int_status = __raw_readl(sysmmuconp->regs + S5P_INT_STATUS);
	reg_mmu_ctrl = __raw_readl(sysmmuconp->regs + S5P_MMU_CTRL);
	reg_mmu_status = __raw_readl(sysmmuconp->regs + S5P_MMU_STATUS);
	reg_pt_base_addr = __raw_readl(sysmmuconp->regs + S5P_PT_BASE_ADDR);
	reg_page_ft_addr = __raw_readl(sysmmuconp->regs + S5P_PAGE_FAULT_ADDR);

	printk(KERN_INFO "%s: ips:%s\n", __func__, sysmmuconp->name);
	printk(KERN_INFO "%s: MMU_CTRL:0x%X, ", __func__, reg_mmu_ctrl);
	printk(KERN_INFO "MMU_STATUS:0x%X, PT_BASE_ADDR:0x%X\n", reg_mmu_status, reg_pt_base_addr);
	printk(KERN_INFO "%s: INT_STATUS:0x%X, PAGE_FAULT_ADDR:0x%X\n", __func__, reg_int_status, reg_page_ft_addr);

	switch (reg_int_status & 0xFF) {
	case 0x1:
		printk(KERN_INFO "%s: Page fault\n", __func__);
		printk(KERN_INFO "%s: Virtual address causing last page fault or bus error : 0x%x\n", __func__ , reg_page_ft_addr);
		break;
	case 0x2:
		printk(KERN_INFO "%s: AR multi-hit fault\n", __func__);
		break;
	case 0x4:
		printk(KERN_INFO "%s: AW multi-hit fault\n", __func__);
		break;
	case 0x8:
		printk(KERN_INFO "%s: Bus error\n", __func__);
		break;
	case 0x10:
		printk(KERN_INFO "%s: AR Security protection fault\n", __func__);
		break;
	case 0x20:
		printk(KERN_INFO "%s: AR Access protection fault\n", __func__);
		break;
	case 0x40:
		printk(KERN_INFO "%s: AW Security protection fault\n", __func__);
		break;
	case 0x80:
		printk(KERN_INFO "%s: AW Access protection fault\n", __func__);
		break;
	}
}

static irqreturn_t s5p_sysmmu_irq(int irq, void *dev_id)
{
	unsigned int i;
	unsigned int reg_int_status;
	struct sysmmu_controller *sysmmuconp;

	for (i = 0; i < S5P_SYSMMU_TOTAL_IPNUM; i++) {
		sysmmuconp = &s5p_sysmmu_cntlrs[i];

		if (sysmmuconp->enable == true) {
			reg_int_status = __raw_readl(sysmmuconp->regs + S5P_INT_STATUS);

			if (reg_int_status & 0xFF)
				s5p_sysmmu_register(sysmmuconp);
		}
	}
	return IRQ_HANDLED;
}

int s5p_sysmmu_set_tablebase_pgd(sysmmu_ips ips, unsigned long pgd)
{
	struct sysmmu_controller *sysmmuconp = NULL;

	sysmmuconp = &s5p_sysmmu_cntlrs[ips];

	if (sysmmuconp == NULL) {
		printk(KERN_ERR "failed to get ip's sysmmu info\n");
		return 1;
	}

	/* Set sysmmu page table base address */
	__raw_writel(pgd, sysmmuconp->regs + S5P_PT_BASE_ADDR);

	if (s5p_sysmmu_tlb_invalidate(ips) != 0)
		printk(KERN_ERR "failed s5p_sysmmu_tlb_invalidate\n");

	return 0;
}

static int s5p_sysmmu_set_tablebase(sysmmu_ips ips)
{
	unsigned int pg;
	struct sysmmu_controller *sysmmuconp;

	sysmmuconp = &s5p_sysmmu_cntlrs[ips];

	if (sysmmuconp == NULL) {
		printk(KERN_ERR "failed to get ip's sysmmu info\n");
		return 1;
	}

	__asm__("mrc	p15, 0, %0, c2, c0, 0"	\
		: "=r" (pg) : : "cc");		\
		pg &= ~0x3fff;

	printk(KERN_INFO "%s: CP15 TTBR0 : 0x%x\n", __func__, pg);

	/* Set sysmmu page table base address */
	__raw_writel(pg, sysmmuconp->regs + S5P_PT_BASE_ADDR);

	return 0;
}

int s5p_sysmmu_enable(sysmmu_ips ips)
{
	unsigned int reg;

	struct sysmmu_controller *sysmmuconp;

	sysmmuconp = &s5p_sysmmu_cntlrs[ips];

	if (sysmmuconp == NULL) {
		printk(KERN_ERR "failed to get ip's sysmmu info\n");
		return 1;
	}

	s5p_sysmmu_set_tablebase(ips);

	/* replacement policy : LRU */
	reg = __raw_readl(sysmmuconp->regs + S5P_MMU_CFG);
	reg |= 0x1;
	__raw_writel(reg, sysmmuconp->regs + S5P_MMU_CFG);

	/* Enable interrupt, Enable MMU */
	reg = __raw_readl(sysmmuconp->regs + S5P_MMU_CTRL);
	reg |= (0x1 << 2) | (0x1 << 0);

	__raw_writel(reg, sysmmuconp->regs + S5P_MMU_CTRL);

	sysmmuconp->enable = true;

	return 0;
}

int s5p_sysmmu_disable(sysmmu_ips ips)
{
	unsigned int reg;

	struct sysmmu_controller *sysmmuconp = NULL;

	if (ips > S5P_SYSMMU_TOTAL_IPNUM)
		printk(KERN_ERR "failed to get ips parameter\n");

	sysmmuconp = &s5p_sysmmu_cntlrs[ips];

	if (sysmmuconp == NULL) {
		printk(KERN_ERR "failed to get ip's sysmmu info\n");
		return 1;
	}

	reg = __raw_readl(sysmmuconp->regs + S5P_MMU_CFG);

	/* replacement policy : LRU */
	reg |= 0x1;
	__raw_writel(reg, sysmmuconp->regs + S5P_MMU_CFG);

	reg = __raw_readl(sysmmuconp->regs + S5P_MMU_CTRL);

	/* Disable MMU */
	reg &= ~0x1;
	__raw_writel(reg, sysmmuconp->regs + S5P_MMU_CTRL);

	sysmmuconp->enable = false;

	return 0;
}

int s5p_sysmmu_tlb_invalidate(sysmmu_ips ips)
{
	unsigned int reg;
	struct sysmmu_controller *sysmmuconp = NULL;

	sysmmuconp = &s5p_sysmmu_cntlrs[ips];

	if (sysmmuconp == NULL) {
		printk(KERN_ERR "failed to get ip's sysmmu info\n");
		return 1;
	}

	/* set Block MMU for flush TLB */
	reg = __raw_readl(sysmmuconp->regs + S5P_MMU_CTRL);
	reg |= 0x1 << 1;
	__raw_writel(reg, sysmmuconp->regs + S5P_MMU_CTRL);

	/* flush all TLB entry */
	__raw_writel(0x1, sysmmuconp->regs + S5P_MMU_FLUSH);

	/* set Un-block MMU after flush TLB */
	reg = __raw_readl(sysmmuconp->regs + S5P_MMU_CTRL);
	reg &= ~(0x1 << 1);
	__raw_writel(reg, sysmmuconp->regs + S5P_MMU_CTRL);

	return 0;
}

static int s5p_sysmmu_probe(struct platform_device *pdev)
{
	int i;
	int ret;
	struct resource *res;
	struct sysmmu_controller *sysmmuconp;
	sysmmu_ips ips;

	for (i = 0; i < S5P_SYSMMU_TOTAL_IPNUM; i++) {
		sysmmuconp = &s5p_sysmmu_cntlrs[i];
		if (sysmmuconp == NULL) {
			printk(KERN_ERR "failed to get ip's sysmmu info\n");
			ret = -ENOENT;
			goto err_res;
		}

		sysmmuconp->name = sysmmu_ips_name[i];

		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res) {
			printk(KERN_ERR "failed to get sysmmu resource\n");
			ret = -ENODEV;
			goto err_res;
		}

		sysmmuconp->mem = request_mem_region(res->start,
				((res->end) - (res->start)) + 1, pdev->name);
		if (!sysmmuconp->mem) {
			pr_err("failed to request sysmmu memory region\n");
			ret = -EBUSY;
			goto err_res;
		}

		sysmmuconp->regs = ioremap(res->start, res->end - res->start + 1);
		if (!sysmmuconp->regs) {
			pr_err("failed to sysmmu ioremap\n");
			ret = -ENXIO;
			goto err_reg;
		}

		sysmmuconp->irq = platform_get_irq(pdev, i);
		if (sysmmuconp->irq <= 0) {
			pr_err("failed to get sysmmu irq resource\n");
			ret = -ENOENT;
			goto err_map;
		}

		ret = request_irq(sysmmuconp->irq, s5p_sysmmu_irq, IRQF_DISABLED, pdev->name, sysmmuconp);
		if (ret) {
			pr_err("failed to request irq\n");
			ret = -ENOENT;
			goto err_map;
		}

		ips = (sysmmu_ips)i;

		sysmmuconp->ips = ips;
	}

	return 0;

err_reg:
	release_mem_region((resource_size_t)sysmmuconp->mem, (resource_size_t)((res->end) - (res->start) + 1));
err_map:
	iounmap(sysmmuconp->regs);
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
