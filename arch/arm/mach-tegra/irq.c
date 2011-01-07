/*
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *
 * Copyright (C) 2010, NVIDIA Corporation
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/hardware/gic.h>

#include <mach/iomap.h>

#include "board.h"

#define INT_SYS_NR	(INT_GPIO_BASE - INT_PRI_BASE)
#define INT_SYS_SZ	(INT_SEC_BASE - INT_PRI_BASE)
#define PPI_NR		((INT_SYS_NR+INT_SYS_SZ-1)/INT_SYS_SZ)

#define APBDMA_IRQ_STA_CPU  0x14
#define APBDMA_IRQ_MASK_SET 0x20
#define APBDMA_IRQ_MASK_CLR 0x24

#define ICTLR_CPU_IER		0x20
#define ICTLR_CPU_IER_SET	0x24
#define ICTLR_CPU_IER_CLR	0x28
#define ICTLR_CPU_IEP_CLASS	0x2c
#define ICTLR_COP_IER		0x30
#define ICTLR_COP_IER_SET	0x34
#define ICTLR_COP_IER_CLR	0x38
#define ICTLR_COP_IEP_CLASS	0x3c

static void (*gic_mask_irq)(unsigned int irq);
static void (*gic_unmask_irq)(unsigned int irq);

#define irq_to_ictlr(irq) (((irq)-32) >> 5)
static void __iomem *tegra_ictlr_base = IO_ADDRESS(TEGRA_PRIMARY_ICTLR_BASE);
#define ictlr_to_virt(ictlr) (tegra_ictlr_base + (ictlr)*0x100)

static void tegra_mask(unsigned int irq)
{
	void __iomem *addr = ictlr_to_virt(irq_to_ictlr(irq));
	gic_mask_irq(irq);
	writel(1<<(irq&31), addr+ICTLR_CPU_IER_CLR);
}

static void tegra_unmask(unsigned int irq)
{
	void __iomem *addr = ictlr_to_virt(irq_to_ictlr(irq));
	gic_unmask_irq(irq);
	writel(1<<(irq&31), addr+ICTLR_CPU_IER_SET);
}

#ifdef CONFIG_PM

static int tegra_set_wake(unsigned int irq, unsigned int on)
{
	return 0;
}
#endif

static struct irq_chip tegra_irq = {
	.name		= "PPI",
	.mask		= tegra_mask,
	.unmask		= tegra_unmask,
#ifdef CONFIG_PM
	.set_wake	= tegra_set_wake,
#endif
};

void __init tegra_init_irq(void)
{
	struct irq_chip *gic;
	unsigned int i;

	for (i = 0; i < PPI_NR; i++) {
		writel(~0, ictlr_to_virt(i) + ICTLR_CPU_IER_CLR);
		writel(0, ictlr_to_virt(i) + ICTLR_CPU_IEP_CLASS);
	}

	gic_init(0, 29, IO_ADDRESS(TEGRA_ARM_INT_DIST_BASE),
		 IO_ADDRESS(TEGRA_ARM_PERIF_BASE + 0x100));

	gic = get_irq_chip(29);
	gic_unmask_irq = gic->unmask;
	gic_mask_irq = gic->mask;
	tegra_irq.ack = gic->ack;
#ifdef CONFIG_SMP
	tegra_irq.set_affinity = gic->set_affinity;
#endif

	for (i = INT_PRI_BASE; i < INT_GPIO_BASE; i++) {
		set_irq_chip(i, &tegra_irq);
		set_irq_handler(i, handle_level_irq);
		set_irq_flags(i, IRQF_VALID);
	}
}

#ifdef CONFIG_PM
static u32 cop_ier[PPI_NR];
static u32 cpu_ier[PPI_NR];
static u32 cpu_iep[PPI_NR];

void tegra_irq_suspend(void)
{
	unsigned long flags;
	int i;

	for (i = INT_PRI_BASE; i < INT_GPIO_BASE; i++) {
		struct irq_desc *desc = irq_to_desc(i);
		if (!desc)
			continue;
		if (desc->status & IRQ_WAKEUP) {
			pr_debug("irq %d is wakeup\n", i);
			continue;
		}
		disable_irq(i);
	}

	local_irq_save(flags);
	for (i = 0; i < PPI_NR; i++) {
		void __iomem *ictlr = ictlr_to_virt(i);
		cpu_ier[i] = readl(ictlr + ICTLR_CPU_IER);
		cpu_iep[i] = readl(ictlr + ICTLR_CPU_IEP_CLASS);
		cop_ier[i] = readl(ictlr + ICTLR_COP_IER);
		writel(~0, ictlr + ICTLR_COP_IER_CLR);
	}
	local_irq_restore(flags);
}

void tegra_irq_resume(void)
{
	unsigned long flags;
	int i;

	local_irq_save(flags);
	for (i = 0; i < PPI_NR; i++) {
		void __iomem *ictlr = ictlr_to_virt(i);
		writel(cpu_iep[i], ictlr + ICTLR_CPU_IEP_CLASS);
		writel(~0ul, ictlr + ICTLR_CPU_IER_CLR);
		writel(cpu_ier[i], ictlr + ICTLR_CPU_IER_SET);
		writel(0, ictlr + ICTLR_COP_IEP_CLASS);
		writel(~0ul, ictlr + ICTLR_COP_IER_CLR);
		writel(cop_ier[i], ictlr + ICTLR_COP_IER_SET);
	}
	local_irq_restore(flags);

	for (i = INT_PRI_BASE; i < INT_GPIO_BASE; i++) {
		struct irq_desc *desc = irq_to_desc(i);
		if (!desc || (desc->status & IRQ_WAKEUP))
			continue;
		enable_irq(i);
	}
}
#endif
