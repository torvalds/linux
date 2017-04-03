/*
 * irqchip for the Cortina Systems Gemini Copyright (C) 2017 Linus
 * Walleij <linus.walleij@linaro.org>
 *
 * Based on arch/arm/mach-gemini/irq.c
 * Copyright (C) 2001-2006 Storlink, Corp.
 * Copyright (C) 2008-2009 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
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

#define GEMINI_NUM_IRQS 32

#define GEMINI_IRQ_SOURCE(base_addr)	(base_addr + 0x00)
#define GEMINI_IRQ_MASK(base_addr)	(base_addr + 0x04)
#define GEMINI_IRQ_CLEAR(base_addr)	(base_addr + 0x08)
#define GEMINI_IRQ_MODE(base_addr)	(base_addr + 0x0C)
#define GEMINI_IRQ_POLARITY(base_addr)	(base_addr + 0x10)
#define GEMINI_IRQ_STATUS(base_addr)	(base_addr + 0x14)
#define GEMINI_FIQ_SOURCE(base_addr)	(base_addr + 0x20)
#define GEMINI_FIQ_MASK(base_addr)	(base_addr + 0x24)
#define GEMINI_FIQ_CLEAR(base_addr)	(base_addr + 0x28)
#define GEMINI_FIQ_MODE(base_addr)	(base_addr + 0x2C)
#define GEMINI_FIQ_POLARITY(base_addr)	(base_addr + 0x30)
#define GEMINI_FIQ_STATUS(base_addr)	(base_addr + 0x34)

/**
 * struct gemini_irq_data - irq data container for the Gemini IRQ controller
 * @base: memory offset in virtual memory
 * @chip: chip container for this instance
 * @domain: IRQ domain for this instance
 */
struct gemini_irq_data {
	void __iomem *base;
	struct irq_chip chip;
	struct irq_domain *domain;
};

static void gemini_irq_mask(struct irq_data *d)
{
	struct gemini_irq_data *g = irq_data_get_irq_chip_data(d);
	unsigned int mask;

	mask = readl(GEMINI_IRQ_MASK(g->base));
	mask &= ~BIT(irqd_to_hwirq(d));
	writel(mask, GEMINI_IRQ_MASK(g->base));
}

static void gemini_irq_unmask(struct irq_data *d)
{
	struct gemini_irq_data *g = irq_data_get_irq_chip_data(d);
	unsigned int mask;

	mask = readl(GEMINI_IRQ_MASK(g->base));
	mask |= BIT(irqd_to_hwirq(d));
	writel(mask, GEMINI_IRQ_MASK(g->base));
}

static void gemini_irq_ack(struct irq_data *d)
{
	struct gemini_irq_data *g = irq_data_get_irq_chip_data(d);

	writel(BIT(irqd_to_hwirq(d)), GEMINI_IRQ_CLEAR(g->base));
}

static int gemini_irq_set_type(struct irq_data *d, unsigned int trigger)
{
	struct gemini_irq_data *g = irq_data_get_irq_chip_data(d);
	int offset = irqd_to_hwirq(d);
	u32 mode, polarity;

	mode = readl(GEMINI_IRQ_MODE(g->base));
	polarity = readl(GEMINI_IRQ_POLARITY(g->base));

	if (trigger & (IRQ_TYPE_LEVEL_HIGH)) {
		irq_set_handler_locked(d, handle_level_irq);
		/* Disable edge detection */
		mode &= ~BIT(offset);
		polarity &= ~BIT(offset);
	} else if (trigger & IRQ_TYPE_EDGE_RISING) {
		irq_set_handler_locked(d, handle_edge_irq);
		mode |= BIT(offset);
		polarity |= BIT(offset);
	} else if (trigger & IRQ_TYPE_EDGE_FALLING) {
		irq_set_handler_locked(d, handle_edge_irq);
		mode |= BIT(offset);
		polarity &= ~BIT(offset);
	} else {
		irq_set_handler_locked(d, handle_bad_irq);
		pr_warn("GEMINI IRQ: no supported trigger selected for line %d\n",
			offset);
	}

	writel(mode, GEMINI_IRQ_MODE(g->base));
	writel(polarity, GEMINI_IRQ_POLARITY(g->base));

	return 0;
}

static struct irq_chip gemini_irq_chip = {
	.name		= "GEMINI",
	.irq_ack	= gemini_irq_ack,
	.irq_mask	= gemini_irq_mask,
	.irq_unmask	= gemini_irq_unmask,
	.irq_set_type	= gemini_irq_set_type,
};

/* Local static for the IRQ entry call */
static struct gemini_irq_data girq;

asmlinkage void __exception_irq_entry gemini_irqchip_handle_irq(struct pt_regs *regs)
{
	struct gemini_irq_data *g = &girq;
	int irq;
	u32 status;

	while ((status = readl(GEMINI_IRQ_STATUS(g->base)))) {
		irq = ffs(status) - 1;
		handle_domain_irq(g->domain, irq, regs);
	}
}

static int gemini_irqdomain_map(struct irq_domain *d, unsigned int irq,
				irq_hw_number_t hwirq)
{
	struct gemini_irq_data *g = d->host_data;

	irq_set_chip_data(irq, g);
	/* All IRQs should set up their type, flags as bad by default */
	irq_set_chip_and_handler(irq, &gemini_irq_chip, handle_bad_irq);
	irq_set_probe(irq);

	return 0;
}

static void gemini_irqdomain_unmap(struct irq_domain *d, unsigned int irq)
{
	irq_set_chip_and_handler(irq, NULL, NULL);
	irq_set_chip_data(irq, NULL);
}

static const struct irq_domain_ops gemini_irqdomain_ops = {
	.map = gemini_irqdomain_map,
	.unmap = gemini_irqdomain_unmap,
	.xlate = irq_domain_xlate_onetwocell,
};

int __init gemini_of_init_irq(struct device_node *node,
			      struct device_node *parent)
{
	struct gemini_irq_data *g = &girq;

	/*
	 * Disable the idle handler by default since it is buggy
	 * For more info see arch/arm/mach-gemini/idle.c
	 */
	cpu_idle_poll_ctrl(true);

	g->base = of_iomap(node, 0);
	WARN(!g->base, "unable to map gemini irq registers\n");

	/* Disable all interrupts */
	writel(0, GEMINI_IRQ_MASK(g->base));
	writel(0, GEMINI_FIQ_MASK(g->base));

	g->domain = irq_domain_add_simple(node, GEMINI_NUM_IRQS, 0,
					  &gemini_irqdomain_ops, g);
	set_handle_irq(gemini_irqchip_handle_irq);

	return 0;
}
IRQCHIP_DECLARE(gemini, "cortina,gemini-interrupt-controller",
		gemini_of_init_irq);
