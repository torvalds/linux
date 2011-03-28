/*
 * Copyright (C) 2006, 2008 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/sysdev.h>

#include <asm/io.h>

#include "intc.h"

struct intc {
	void __iomem		*regs;
	struct irq_chip		chip;
	struct sys_device	sysdev;
#ifdef CONFIG_PM
	unsigned long		suspend_ipr;
	unsigned long		saved_ipr[64];
#endif
};

extern struct platform_device at32_intc0_device;

/*
 * TODO: We may be able to implement mask/unmask by setting IxM flags
 * in the status register.
 */
static void intc_mask_irq(struct irq_data *d)
{

}

static void intc_unmask_irq(struct irq_data *d)
{

}

static struct intc intc0 = {
	.chip = {
		.name		= "intc",
		.irq_mask	= intc_mask_irq,
		.irq_unmask	= intc_unmask_irq,
	},
};

/*
 * All interrupts go via intc at some point.
 */
asmlinkage void do_IRQ(int level, struct pt_regs *regs)
{
	struct pt_regs *old_regs;
	unsigned int irq;
	unsigned long status_reg;

	local_irq_disable();

	old_regs = set_irq_regs(regs);

	irq_enter();

	irq = intc_readl(&intc0, INTCAUSE0 - 4 * level);
	generic_handle_irq(irq);

	/*
	 * Clear all interrupt level masks so that we may handle
	 * interrupts during softirq processing.  If this is a nested
	 * interrupt, interrupts must stay globally disabled until we
	 * return.
	 */
	status_reg = sysreg_read(SR);
	status_reg &= ~(SYSREG_BIT(I0M) | SYSREG_BIT(I1M)
			| SYSREG_BIT(I2M) | SYSREG_BIT(I3M));
	sysreg_write(SR, status_reg);

	irq_exit();

	set_irq_regs(old_regs);
}

void __init init_IRQ(void)
{
	extern void _evba(void);
	extern void irq_level0(void);
	struct resource *regs;
	struct clk *pclk;
	unsigned int i;
	u32 offset, readback;

	regs = platform_get_resource(&at32_intc0_device, IORESOURCE_MEM, 0);
	if (!regs) {
		printk(KERN_EMERG "intc: no mmio resource defined\n");
		goto fail;
	}
	pclk = clk_get(&at32_intc0_device.dev, "pclk");
	if (IS_ERR(pclk)) {
		printk(KERN_EMERG "intc: no clock defined\n");
		goto fail;
	}

	clk_enable(pclk);

	intc0.regs = ioremap(regs->start, regs->end - regs->start + 1);
	if (!intc0.regs) {
		printk(KERN_EMERG "intc: failed to map registers (0x%08lx)\n",
		       (unsigned long)regs->start);
		goto fail;
	}

	/*
	 * Initialize all interrupts to level 0 (lowest priority). The
	 * priority level may be changed by calling
	 * irq_set_priority().
	 *
	 */
	offset = (unsigned long)&irq_level0 - (unsigned long)&_evba;
	for (i = 0; i < NR_INTERNAL_IRQS; i++) {
		intc_writel(&intc0, INTPR0 + 4 * i, offset);
		readback = intc_readl(&intc0, INTPR0 + 4 * i);
		if (readback == offset)
			irq_set_chip_and_handler(i, &intc0.chip,
						 handle_simple_irq);
	}

	/* Unmask all interrupt levels */
	sysreg_write(SR, (sysreg_read(SR)
			  & ~(SR_I3M | SR_I2M | SR_I1M | SR_I0M)));

	return;

fail:
	panic("Interrupt controller initialization failed!\n");
}

#ifdef CONFIG_PM
void intc_set_suspend_handler(unsigned long offset)
{
	intc0.suspend_ipr = offset;
}

static int intc_suspend(struct sys_device *sdev, pm_message_t state)
{
	struct intc *intc = container_of(sdev, struct intc, sysdev);
	int i;

	if (unlikely(!irqs_disabled())) {
		pr_err("intc_suspend: called with interrupts enabled\n");
		return -EINVAL;
	}

	if (unlikely(!intc->suspend_ipr)) {
		pr_err("intc_suspend: suspend_ipr not initialized\n");
		return -EINVAL;
	}

	for (i = 0; i < 64; i++) {
		intc->saved_ipr[i] = intc_readl(intc, INTPR0 + 4 * i);
		intc_writel(intc, INTPR0 + 4 * i, intc->suspend_ipr);
	}

	return 0;
}

static int intc_resume(struct sys_device *sdev)
{
	struct intc *intc = container_of(sdev, struct intc, sysdev);
	int i;

	WARN_ON(!irqs_disabled());

	for (i = 0; i < 64; i++)
		intc_writel(intc, INTPR0 + 4 * i, intc->saved_ipr[i]);

	return 0;
}
#else
#define intc_suspend	NULL
#define intc_resume	NULL
#endif

static struct sysdev_class intc_class = {
	.name		= "intc",
	.suspend	= intc_suspend,
	.resume		= intc_resume,
};

static int __init intc_init_sysdev(void)
{
	int ret;

	ret = sysdev_class_register(&intc_class);
	if (ret)
		return ret;

	intc0.sysdev.id = 0;
	intc0.sysdev.cls = &intc_class;
	ret = sysdev_register(&intc0.sysdev);

	return ret;
}
device_initcall(intc_init_sysdev);

unsigned long intc_get_pending(unsigned int group)
{
	return intc_readl(&intc0, INTREQ0 + 4 * group);
}
EXPORT_SYMBOL_GPL(intc_get_pending);
