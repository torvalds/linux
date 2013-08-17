/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		htt://www.samsung.com
 *
 * Copyright (c) 2011 Gisecke & Devrient
 *
 * Based on linux/arch/arm/plat-s5p/irq-eint.c
 *
 * EXYNOS - Software Generated Interrupts Dummy chip support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <mach/regs-irq.h>

static inline void exynos_irq_sgi_mask(struct irq_data *d)
{
	/* Do nothing, because SGIs are always enabled. */
}

static void exynos_irq_sgi_unmask(struct irq_data *d)
{
	/* Do nothing, because SGIs are always enabled. */
}

static inline void exynos_irq_sgi_eoi(struct irq_data *d)
{
	unsigned int irq = (d->irq - S5P_IRQ_OFFSET);

	writel(irq, S5P_VA_GIC_CPU + GIC_CPU_EOI);
}

static int exynos_irq_sgi_set_type(struct irq_data *d, unsigned int type)
{
	return 0;
}

static struct irq_chip exynos_irq_sgi = {
	.name		= "exynos_sgi",
	.irq_mask	= exynos_irq_sgi_mask,
	.irq_unmask	= exynos_irq_sgi_unmask,
	.irq_eoi	= exynos_irq_sgi_eoi,
	.irq_set_type	= exynos_irq_sgi_set_type,
};

/*
 * exynos_init_irq_sgi
 *
 * Setup the SGI IRQ to a dummy GIC. The interrupts use the same id
 * as provided by get_irqnr_and_base, no demuxing is necesarry but
 * we are handling the last 8 SGIs as normal interrupts in
 * get_irqnr_and_base.
 *
 * NOTE: SGIs are bound to the CPU for which they have been generated
 * so use with care.
 */
int __init exynos_init_irq_sgi(void)
{
	int irq;

	for (irq = 8; irq <= 15; irq++) {
		irq_set_chip_and_handler(IRQ_SGI(irq), &exynos_irq_sgi, handle_fasteoi_irq);
		set_irq_flags(IRQ_SGI(irq), IRQF_VALID);
	}

	return 0;
}
arch_initcall(exynos_init_irq_sgi);
