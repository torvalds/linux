// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Maxime Coquelin 2015
 * Copyright (C) STMicroelectronics 2017
 * Author:  Maxime Coquelin <mcoquelin.stm32@gmail.com>
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

#define IRQS_PER_BANK 32

struct stm32_exti_bank {
	u32 imr_ofst;
	u32 emr_ofst;
	u32 rtsr_ofst;
	u32 ftsr_ofst;
	u32 swier_ofst;
	u32 pr_ofst;
};

static const struct stm32_exti_bank stm32f4xx_exti_b1 = {
	.imr_ofst	= 0x00,
	.emr_ofst	= 0x04,
	.rtsr_ofst	= 0x08,
	.ftsr_ofst	= 0x0C,
	.swier_ofst	= 0x10,
	.pr_ofst	= 0x14,
};

static const struct stm32_exti_bank *stm32f4xx_exti_banks[] = {
	&stm32f4xx_exti_b1,
};

static const struct stm32_exti_bank stm32h7xx_exti_b1 = {
	.imr_ofst	= 0x80,
	.emr_ofst	= 0x84,
	.rtsr_ofst	= 0x00,
	.ftsr_ofst	= 0x04,
	.swier_ofst	= 0x08,
	.pr_ofst	= 0x88,
};

static const struct stm32_exti_bank stm32h7xx_exti_b2 = {
	.imr_ofst	= 0x90,
	.emr_ofst	= 0x94,
	.rtsr_ofst	= 0x20,
	.ftsr_ofst	= 0x24,
	.swier_ofst	= 0x28,
	.pr_ofst	= 0x98,
};

static const struct stm32_exti_bank stm32h7xx_exti_b3 = {
	.imr_ofst	= 0xA0,
	.emr_ofst	= 0xA4,
	.rtsr_ofst	= 0x40,
	.ftsr_ofst	= 0x44,
	.swier_ofst	= 0x48,
	.pr_ofst	= 0xA8,
};

static const struct stm32_exti_bank *stm32h7xx_exti_banks[] = {
	&stm32h7xx_exti_b1,
	&stm32h7xx_exti_b2,
	&stm32h7xx_exti_b3,
};

static unsigned long stm32_exti_pending(struct irq_chip_generic *gc)
{
	const struct stm32_exti_bank *stm32_bank = gc->private;

	return irq_reg_readl(gc, stm32_bank->pr_ofst);
}

static void stm32_exti_irq_ack(struct irq_chip_generic *gc, u32 mask)
{
	const struct stm32_exti_bank *stm32_bank = gc->private;

	irq_reg_writel(gc, mask, stm32_bank->pr_ofst);
}

static void stm32_irq_handler(struct irq_desc *desc)
{
	struct irq_domain *domain = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int virq, nbanks = domain->gc->num_chips;
	struct irq_chip_generic *gc;
	const struct stm32_exti_bank *stm32_bank;
	unsigned long pending;
	int n, i, irq_base = 0;

	chained_irq_enter(chip, desc);

	for (i = 0; i < nbanks; i++, irq_base += IRQS_PER_BANK) {
		gc = irq_get_domain_generic_chip(domain, irq_base);
		stm32_bank = gc->private;

		while ((pending = stm32_exti_pending(gc))) {
			for_each_set_bit(n, &pending, IRQS_PER_BANK) {
				virq = irq_find_mapping(domain, irq_base + n);
				generic_handle_irq(virq);
				stm32_exti_irq_ack(gc, BIT(n));
			}
		}
	}

	chained_irq_exit(chip, desc);
}

static int stm32_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);
	const struct stm32_exti_bank *stm32_bank = gc->private;
	int pin = data->hwirq % IRQS_PER_BANK;
	u32 rtsr, ftsr;

	irq_gc_lock(gc);

	rtsr = irq_reg_readl(gc, stm32_bank->rtsr_ofst);
	ftsr = irq_reg_readl(gc, stm32_bank->ftsr_ofst);

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

	irq_reg_writel(gc, rtsr, stm32_bank->rtsr_ofst);
	irq_reg_writel(gc, ftsr, stm32_bank->ftsr_ofst);

	irq_gc_unlock(gc);

	return 0;
}

static int stm32_irq_set_wake(struct irq_data *data, unsigned int on)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(data);
	const struct stm32_exti_bank *stm32_bank = gc->private;
	int pin = data->hwirq % IRQS_PER_BANK;
	u32 imr;

	irq_gc_lock(gc);

	imr = irq_reg_readl(gc, stm32_bank->imr_ofst);
	if (on)
		imr |= BIT(pin);
	else
		imr &= ~BIT(pin);
	irq_reg_writel(gc, imr, stm32_bank->imr_ofst);

	irq_gc_unlock(gc);

	return 0;
}

static int stm32_exti_alloc(struct irq_domain *d, unsigned int virq,
			    unsigned int nr_irqs, void *data)
{
	struct irq_chip_generic *gc;
	struct irq_fwspec *fwspec = data;
	irq_hw_number_t hwirq;

	hwirq = fwspec->param[0];
	gc = irq_get_domain_generic_chip(d, hwirq);

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

static int
__init stm32_exti_init(const struct stm32_exti_bank **stm32_exti_banks,
		       int bank_nr, struct device_node *node)
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

	domain = irq_domain_add_linear(node, bank_nr * IRQS_PER_BANK,
				       &irq_exti_domain_ops, NULL);
	if (!domain) {
		pr_err("%s: Could not register interrupt domain.\n",
		       node->name);
		ret = -ENOMEM;
		goto out_unmap;
	}

	ret = irq_alloc_domain_generic_chips(domain, IRQS_PER_BANK, 1, "exti",
					     handle_edge_irq, clr, 0, 0);
	if (ret) {
		pr_err("%pOF: Could not allocate generic interrupt chip.\n",
			node);
		goto out_free_domain;
	}

	for (i = 0; i < bank_nr; i++) {
		const struct stm32_exti_bank *stm32_bank = stm32_exti_banks[i];
		u32 irqs_mask;

		gc = irq_get_domain_generic_chip(domain, i * IRQS_PER_BANK);

		gc->reg_base = base;
		gc->chip_types->type = IRQ_TYPE_EDGE_BOTH;
		gc->chip_types->chip.irq_ack = irq_gc_ack_set_bit;
		gc->chip_types->chip.irq_mask = irq_gc_mask_clr_bit;
		gc->chip_types->chip.irq_unmask = irq_gc_mask_set_bit;
		gc->chip_types->chip.irq_set_type = stm32_irq_set_type;
		gc->chip_types->chip.irq_set_wake = stm32_irq_set_wake;
		gc->chip_types->regs.ack = stm32_bank->pr_ofst;
		gc->chip_types->regs.mask = stm32_bank->imr_ofst;
		gc->private = (void *)stm32_bank;

		/* Determine number of irqs supported */
		writel_relaxed(~0UL, base + stm32_bank->rtsr_ofst);
		irqs_mask = readl_relaxed(base + stm32_bank->rtsr_ofst);
		nr_exti = fls(readl_relaxed(base + stm32_bank->rtsr_ofst));

		/*
		 * This IP has no reset, so after hot reboot we should
		 * clear registers to avoid residue
		 */
		writel_relaxed(0, base + stm32_bank->imr_ofst);
		writel_relaxed(0, base + stm32_bank->emr_ofst);
		writel_relaxed(0, base + stm32_bank->rtsr_ofst);
		writel_relaxed(0, base + stm32_bank->ftsr_ofst);
		writel_relaxed(~0UL, base + stm32_bank->pr_ofst);

		pr_info("%s: bank%d, External IRQs available:%#x\n",
			node->full_name, i, irqs_mask);
	}

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

static int __init stm32f4_exti_of_init(struct device_node *np,
				       struct device_node *parent)
{
	return stm32_exti_init(stm32f4xx_exti_banks,
			ARRAY_SIZE(stm32f4xx_exti_banks), np);
}

IRQCHIP_DECLARE(stm32f4_exti, "st,stm32-exti", stm32f4_exti_of_init);

static int __init stm32h7_exti_of_init(struct device_node *np,
				       struct device_node *parent)
{
	return stm32_exti_init(stm32h7xx_exti_banks,
			ARRAY_SIZE(stm32h7xx_exti_banks), np);
}

IRQCHIP_DECLARE(stm32h7_exti, "st,stm32h7-exti", stm32h7_exti_of_init);
