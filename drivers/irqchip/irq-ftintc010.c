// SPDX-License-Identifier: GPL-2.0
/*
 * irqchip for the Faraday Technology FTINTC010 Copyright (C) 2017 Linus
 * Walleij <linus.walleij@linaro.org>
 *
 * Based on arch/arm/mach-gemini/irq.c
 * Copyright (C) 2001-2006 Storlink, Corp.
 * Copyright (C) 2008-2009 Paulius Zaleckas <paulius.zaleckas@gmail.com>
 */
#include <linux/bitops.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqchip/versatile-fpga.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/cpu.h>

#include <asm/exception.h>
#include <asm/mach/irq.h>

#define FT010_NUM_IRQS 32

#define FT010_IRQ_SOURCE(base_addr)	(base_addr + 0x00)
#define FT010_IRQ_MASK(base_addr)	(base_addr + 0x04)
#define FT010_IRQ_CLEAR(base_addr)	(base_addr + 0x08)
/* Selects level- or edge-triggered */
#define FT010_IRQ_MODE(base_addr)	(base_addr + 0x0C)
/* Selects active low/high or falling/rising edge */
#define FT010_IRQ_POLARITY(base_addr)	(base_addr + 0x10)
#define FT010_IRQ_STATUS(base_addr)	(base_addr + 0x14)
#define FT010_FIQ_SOURCE(base_addr)	(base_addr + 0x20)
#define FT010_FIQ_MASK(base_addr)	(base_addr + 0x24)
#define FT010_FIQ_CLEAR(base_addr)	(base_addr + 0x28)
#define FT010_FIQ_MODE(base_addr)	(base_addr + 0x2C)
#define FT010_FIQ_POLARITY(base_addr)	(base_addr + 0x30)
#define FT010_FIQ_STATUS(base_addr)	(base_addr + 0x34)

/**
 * struct ft010_irq_data - irq data container for the Faraday IRQ controller
 * @base: memory offset in virtual memory
 * @chip: chip container for this instance
 * @domain: IRQ domain for this instance
 */
struct ft010_irq_data {
	void __iomem *base;
	struct irq_chip chip;
	struct irq_domain *domain;
};

static void ft010_irq_mask(struct irq_data *d)
{
	struct ft010_irq_data *f = irq_data_get_irq_chip_data(d);
	unsigned int mask;

	mask = readl(FT010_IRQ_MASK(f->base));
	mask &= ~BIT(irqd_to_hwirq(d));
	writel(mask, FT010_IRQ_MASK(f->base));
}

static void ft010_irq_unmask(struct irq_data *d)
{
	struct ft010_irq_data *f = irq_data_get_irq_chip_data(d);
	unsigned int mask;

	mask = readl(FT010_IRQ_MASK(f->base));
	mask |= BIT(irqd_to_hwirq(d));
	writel(mask, FT010_IRQ_MASK(f->base));
}

static void ft010_irq_ack(struct irq_data *d)
{
	struct ft010_irq_data *f = irq_data_get_irq_chip_data(d);

	writel(BIT(irqd_to_hwirq(d)), FT010_IRQ_CLEAR(f->base));
}

static int ft010_irq_set_type(struct irq_data *d, unsigned int trigger)
{
	struct ft010_irq_data *f = irq_data_get_irq_chip_data(d);
	int offset = irqd_to_hwirq(d);
	u32 mode, polarity;

	mode = readl(FT010_IRQ_MODE(f->base));
	polarity = readl(FT010_IRQ_POLARITY(f->base));

	if (trigger & (IRQ_TYPE_LEVEL_LOW)) {
		irq_set_handler_locked(d, handle_level_irq);
		mode &= ~BIT(offset);
		polarity |= BIT(offset);
	} else if (trigger & (IRQ_TYPE_LEVEL_HIGH)) {
		irq_set_handler_locked(d, handle_level_irq);
		mode &= ~BIT(offset);
		polarity &= ~BIT(offset);
	} else if (trigger & IRQ_TYPE_EDGE_FALLING) {
		irq_set_handler_locked(d, handle_edge_irq);
		mode |= BIT(offset);
		polarity |= BIT(offset);
	} else if (trigger & IRQ_TYPE_EDGE_RISING) {
		irq_set_handler_locked(d, handle_edge_irq);
		mode |= BIT(offset);
		polarity &= ~BIT(offset);
	} else {
		irq_set_handler_locked(d, handle_bad_irq);
		pr_warn("Faraday IRQ: no supported trigger selected for line %d\n",
			offset);
	}

	writel(mode, FT010_IRQ_MODE(f->base));
	writel(polarity, FT010_IRQ_POLARITY(f->base));

	return 0;
}

static struct irq_chip ft010_irq_chip = {
	.name		= "FTINTC010",
	.irq_ack	= ft010_irq_ack,
	.irq_mask	= ft010_irq_mask,
	.irq_unmask	= ft010_irq_unmask,
	.irq_set_type	= ft010_irq_set_type,
};

/* Local static for the IRQ entry call */
static struct ft010_irq_data firq;

asmlinkage void __exception_irq_entry ft010_irqchip_handle_irq(struct pt_regs *regs)
{
	struct ft010_irq_data *f = &firq;
	int irq;
	u32 status;

	while ((status = readl(FT010_IRQ_STATUS(f->base)))) {
		irq = ffs(status) - 1;
		handle_domain_irq(f->domain, irq, regs);
	}
}

static int ft010_irqdomain_map(struct irq_domain *d, unsigned int irq,
				irq_hw_number_t hwirq)
{
	struct ft010_irq_data *f = d->host_data;

	irq_set_chip_data(irq, f);
	/* All IRQs should set up their type, flags as bad by default */
	irq_set_chip_and_handler(irq, &ft010_irq_chip, handle_bad_irq);
	irq_set_probe(irq);

	return 0;
}

static void ft010_irqdomain_unmap(struct irq_domain *d, unsigned int irq)
{
	irq_set_chip_and_handler(irq, NULL, NULL);
	irq_set_chip_data(irq, NULL);
}

static const struct irq_domain_ops ft010_irqdomain_ops = {
	.map = ft010_irqdomain_map,
	.unmap = ft010_irqdomain_unmap,
	.xlate = irq_domain_xlate_onetwocell,
};

int __init ft010_of_init_irq(struct device_node *node,
			      struct device_node *parent)
{
	struct ft010_irq_data *f = &firq;

	/*
	 * Disable the idle handler by default since it is buggy
	 * For more info see arch/arm/mach-gemini/idle.c
	 */
	cpu_idle_poll_ctrl(true);

	f->base = of_iomap(node, 0);
	WARN(!f->base, "unable to map gemini irq registers\n");

	/* Disable all interrupts */
	writel(0, FT010_IRQ_MASK(f->base));
	writel(0, FT010_FIQ_MASK(f->base));

	f->domain = irq_domain_add_simple(node, FT010_NUM_IRQS, 0,
					  &ft010_irqdomain_ops, f);
	set_handle_irq(ft010_irqchip_handle_irq);

	return 0;
}
IRQCHIP_DECLARE(faraday, "faraday,ftintc010",
		ft010_of_init_irq);
IRQCHIP_DECLARE(gemini, "cortina,gemini-interrupt-controller",
		ft010_of_init_irq);
IRQCHIP_DECLARE(moxa, "moxa,moxart-ic",
		ft010_of_init_irq);
