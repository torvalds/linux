/*
 * Copyright (C) Maxime Coquelin 2015
 * Author:  Maxime Coquelin <mcoquelin.stm32@gmail.com>
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#define EXTI_IMR	0x0
#define EXTI_EMR	0x4
#define EXTI_RTSR	0x8
#define EXTI_FTSR	0xc
#define EXTI_SWIER	0x10
#define EXTI_PR		0x14

static void stm32_irq_handler(struct irq_desc *desc)
{
	struct irq_domain *domain = irq_desc_get_handler_data(desc);
	struct irq_chip_generic *gc = domain->gc->gc[0];
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned long pending;
	int n;

	chained_irq_enter(chip, desc);

	while ((pending = irq_reg_readl(gc, EXTI_PR))) {
		for_each_set_bit(n, &pending, BITS_PER_LONG) {
			generic_handle_irq(irq_find_mapping(domain, n));
			irq_reg_writel(gc, BIT(n), EXTI_PR);
		}
	}

	chained_irq_exit(chip, desc);
}

static int stm32_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);
	int pin = data->hwirq;
	u32 rtsr, ftsr;

	irq_gc_lock(gc);

	rtsr = irq_reg_readl(gc, EXTI_RTSR);
	ftsr = irq_reg_readl(gc, EXTI_FTSR);

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		rtsr |= BIT(pin);
		ftsr &= ~BIT(pin);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		rtsr &= ~BIT(pin);
		ftsr |= BIT(pin);
		break;
	case IRQ_TYPE_EDGE_BOTH:
		rtsr |= BIT(pin);
		ftsr |= BIT(pin);
		break;
	default:
		irq_gc_unlock(gc);
		return -EINVAL;
	}

	irq_reg_writel(gc, rtsr, EXTI_RTSR);
	irq_reg_writel(gc, ftsr, EXTI_FTSR);

	irq_gc_unlock(gc);

	return 0;
}

static int stm32_irq_set_wake(struct irq_data *data, unsigned int on)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);
	int pin = data->hwirq;
	u32 emr;

	irq_gc_lock(gc);

	emr = irq_reg_readl(gc, EXTI_EMR);
	if (on)
		emr |= BIT(pin);
	else
		emr &= ~BIT(pin);
	irq_reg_writel(gc, emr, EXTI_EMR);

	irq_gc_unlock(gc);

	return 0;
}

static int stm32_exti_alloc(struct irq_domain *d, unsigned int virq,
			    unsigned int nr_irqs, void *data)
{
	struct irq_chip_generic *gc = d->gc->gc[0];
	struct irq_fwspec *fwspec = data;
	irq_hw_number_t hwirq;

	hwirq = fwspec->param[0];

	irq_map_generic_chip(d, virq, hwirq);
	irq_domain_set_info(d, virq, hwirq, &gc->chip_types->chip, gc,
			    handle_simple_irq, NULL, NULL);

	return 0;
}

static void stm32_exti_free(struct irq_domain *d, unsigned int virq,
			    unsigned int nr_irqs)
{
	struct irq_data *data = irq_domain_get_irq_data(d, virq);

	irq_domain_reset_irq_data(data);
}

struct irq_domain_ops irq_exti_domain_ops = {
	.map	= irq_map_generic_chip,
	.xlate	= irq_domain_xlate_onetwocell,
	.alloc  = stm32_exti_alloc,
	.free	= stm32_exti_free,
};

static int __init stm32_exti_init(struct device_node *node,
				  struct device_node *parent)
{
	unsigned int clr = IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN;
	int nr_irqs, nr_exti, ret, i;
	struct irq_chip_generic *gc;
	struct irq_domain *domain;
	void *base;

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("%pOF: Unable to map registers\n", node);
		return -ENOMEM;
	}

	/* Determine number of irqs supported */
	writel_relaxed(~0UL, base + EXTI_RTSR);
	nr_exti = fls(readl_relaxed(base + EXTI_RTSR));
	writel_relaxed(0, base + EXTI_RTSR);

	pr_info("%pOF: %d External IRQs detected\n", node, nr_exti);

	domain = irq_domain_add_linear(node, nr_exti,
				       &irq_exti_domain_ops, NULL);
	if (!domain) {
		pr_err("%s: Could not register interrupt domain.\n",
				node->name);
		ret = -ENOMEM;
		goto out_unmap;
	}

	ret = irq_alloc_domain_generic_chips(domain, nr_exti, 1, "exti",
					     handle_edge_irq, clr, 0, 0);
	if (ret) {
		pr_err("%pOF: Could not allocate generic interrupt chip.\n",
			node);
		goto out_free_domain;
	}

	gc = domain->gc->gc[0];
	gc->reg_base                         = base;
	gc->chip_types->type               = IRQ_TYPE_EDGE_BOTH;
	gc->chip_types->chip.name          = gc->chip_types[0].chip.name;
	gc->chip_types->chip.irq_ack       = irq_gc_ack_set_bit;
	gc->chip_types->chip.irq_mask      = irq_gc_mask_clr_bit;
	gc->chip_types->chip.irq_unmask    = irq_gc_mask_set_bit;
	gc->chip_types->chip.irq_set_type  = stm32_irq_set_type;
	gc->chip_types->chip.irq_set_wake  = stm32_irq_set_wake;
	gc->chip_types->regs.ack           = EXTI_PR;
	gc->chip_types->regs.mask          = EXTI_IMR;
	gc->chip_types->handler            = handle_edge_irq;

	nr_irqs = of_irq_count(node);
	for (i = 0; i < nr_irqs; i++) {
		unsigned int irq = irq_of_parse_and_map(node, i);

		irq_set_handler_data(irq, domain);
		irq_set_chained_handler(irq, stm32_irq_handler);
	}

	return 0;

out_free_domain:
	irq_domain_remove(domain);
out_unmap:
	iounmap(base);
	return ret;
}

IRQCHIP_DECLARE(stm32_exti, "st,stm32-exti", stm32_exti_init);
