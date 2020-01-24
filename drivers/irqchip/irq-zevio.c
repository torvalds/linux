// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/drivers/irqchip/irq-zevio.c
 *
 *  Copyright (C) 2013 Daniel Tang <tangrs@tangrs.id.au>
 */

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/mach/irq.h>
#include <asm/exception.h>

#define IO_STATUS	0x000
#define IO_RAW_STATUS	0x004
#define IO_ENABLE	0x008
#define IO_DISABLE	0x00C
#define IO_CURRENT	0x020
#define IO_RESET	0x028
#define IO_MAX_PRIOTY	0x02C

#define IO_IRQ_BASE	0x000
#define IO_FIQ_BASE	0x100

#define IO_INVERT_SEL	0x200
#define IO_STICKY_SEL	0x204
#define IO_PRIORITY_SEL	0x300

#define MAX_INTRS	32
#define FIQ_START	MAX_INTRS

static struct irq_domain *zevio_irq_domain;
static void __iomem *zevio_irq_io;

static void zevio_irq_ack(struct irq_data *irqd)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(irqd);
	struct irq_chip_regs *regs = &irq_data_get_chip_type(irqd)->regs;

	readl(gc->reg_base + regs->ack);
}

static void __exception_irq_entry zevio_handle_irq(struct pt_regs *regs)
{
	int irqnr;

	while (readl(zevio_irq_io + IO_STATUS)) {
		irqnr = readl(zevio_irq_io + IO_CURRENT);
		handle_domain_irq(zevio_irq_domain, irqnr, regs);
	}
}

static void __init zevio_init_irq_base(void __iomem *base)
{
	/* Disable all interrupts */
	writel(~0, base + IO_DISABLE);

	/* Accept interrupts of all priorities */
	writel(0xF, base + IO_MAX_PRIOTY);

	/* Reset existing interrupts */
	readl(base + IO_RESET);
}

static int __init zevio_of_init(struct device_node *node,
				struct device_node *parent)
{
	unsigned int clr = IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN;
	struct irq_chip_generic *gc;
	int ret;

	if (WARN_ON(zevio_irq_io || zevio_irq_domain))
		return -EBUSY;

	zevio_irq_io = of_iomap(node, 0);
	BUG_ON(!zevio_irq_io);

	/* Do not invert interrupt status bits */
	writel(~0, zevio_irq_io + IO_INVERT_SEL);

	/* Disable sticky interrupts */
	writel(0, zevio_irq_io + IO_STICKY_SEL);

	/* We don't use IRQ priorities. Set each IRQ to highest priority. */
	memset_io(zevio_irq_io + IO_PRIORITY_SEL, 0, MAX_INTRS * sizeof(u32));

	/* Init IRQ and FIQ */
	zevio_init_irq_base(zevio_irq_io + IO_IRQ_BASE);
	zevio_init_irq_base(zevio_irq_io + IO_FIQ_BASE);

	zevio_irq_domain = irq_domain_add_linear(node, MAX_INTRS,
						 &irq_generic_chip_ops, NULL);
	BUG_ON(!zevio_irq_domain);

	ret = irq_alloc_domain_generic_chips(zevio_irq_domain, MAX_INTRS, 1,
					     "zevio_intc", handle_level_irq,
					     clr, 0, IRQ_GC_INIT_MASK_CACHE);
	BUG_ON(ret);

	gc = irq_get_domain_generic_chip(zevio_irq_domain, 0);
	gc->reg_base				= zevio_irq_io;
	gc->chip_types[0].chip.irq_ack		= zevio_irq_ack;
	gc->chip_types[0].chip.irq_mask		= irq_gc_mask_disable_reg;
	gc->chip_types[0].chip.irq_unmask	= irq_gc_unmask_enable_reg;
	gc->chip_types[0].regs.mask		= IO_IRQ_BASE + IO_ENABLE;
	gc->chip_types[0].regs.enable		= IO_IRQ_BASE + IO_ENABLE;
	gc->chip_types[0].regs.disable		= IO_IRQ_BASE + IO_DISABLE;
	gc->chip_types[0].regs.ack		= IO_IRQ_BASE + IO_RESET;

	set_handle_irq(zevio_handle_irq);

	pr_info("TI-NSPIRE classic IRQ controller\n");
	return 0;
}

IRQCHIP_DECLARE(zevio_irq, "lsi,zevio-intc", zevio_of_init);
