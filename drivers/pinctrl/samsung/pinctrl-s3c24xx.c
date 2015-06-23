/*
 * S3C24XX specific support for Samsung pinctrl/gpiolib driver.
 *
 * Copyright (c) 2013 Heiko Stuebner <heiko@sntech.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This file contains the SamsungS3C24XX specific information required by the
 * Samsung pinctrl/gpiolib driver. It also includes the implementation of
 * external gpio and wakeup interrupt support.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>

#include "pinctrl-samsung.h"

#define NUM_EINT	24
#define NUM_EINT_IRQ	6
#define EINT_MAX_PER_GROUP	8

#define EINTPEND_REG	0xa8
#define EINTMASK_REG	0xa4

#define EINT_GROUP(i)		((int)((i) / EINT_MAX_PER_GROUP))
#define EINT_REG(i)		((EINT_GROUP(i) * 4) + 0x88)
#define EINT_OFFS(i)		((i) % EINT_MAX_PER_GROUP * 4)

#define EINT_LEVEL_LOW		0
#define EINT_LEVEL_HIGH		1
#define EINT_EDGE_FALLING	2
#define EINT_EDGE_RISING	4
#define EINT_EDGE_BOTH		6
#define EINT_MASK		0xf

static const struct samsung_pin_bank_type bank_type_1bit = {
	.fld_width = { 1, 1, },
	.reg_offset = { 0x00, 0x04, },
};

static const struct samsung_pin_bank_type bank_type_2bit = {
	.fld_width = { 2, 1, 2, },
	.reg_offset = { 0x00, 0x04, 0x08, },
};

#define PIN_BANK_A(pins, reg, id)		\
	{						\
		.type		= &bank_type_1bit,	\
		.pctl_offset	= reg,			\
		.nr_pins	= pins,			\
		.eint_type	= EINT_TYPE_NONE,	\
		.name		= id			\
	}

#define PIN_BANK_2BIT(pins, reg, id)		\
	{						\
		.type		= &bank_type_2bit,	\
		.pctl_offset	= reg,			\
		.nr_pins	= pins,			\
		.eint_type	= EINT_TYPE_NONE,	\
		.name		= id			\
	}

#define PIN_BANK_2BIT_EINTW(pins, reg, id, eoffs, emask)\
	{						\
		.type		= &bank_type_2bit,	\
		.pctl_offset	= reg,			\
		.nr_pins	= pins,			\
		.eint_type	= EINT_TYPE_WKUP,	\
		.eint_func	= 2,			\
		.eint_mask	= emask,		\
		.eint_offset	= eoffs,		\
		.name		= id			\
	}

/**
 * struct s3c24xx_eint_data: EINT common data
 * @drvdata: pin controller driver data
 * @domains: IRQ domains of particular EINT interrupts
 * @parents: mapped parent irqs in the main interrupt controller
 */
struct s3c24xx_eint_data {
	struct samsung_pinctrl_drv_data *drvdata;
	struct irq_domain *domains[NUM_EINT];
	int parents[NUM_EINT_IRQ];
};

/**
 * struct s3c24xx_eint_domain_data: per irq-domain data
 * @bank: pin bank related to the domain
 * @eint_data: common data
 * eint0_3_parent_only: live eints 0-3 only in the main intc
 */
struct s3c24xx_eint_domain_data {
	struct samsung_pin_bank *bank;
	struct s3c24xx_eint_data *eint_data;
	bool eint0_3_parent_only;
};

static int s3c24xx_eint_get_trigger(unsigned int type)
{
	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		return EINT_EDGE_RISING;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		return EINT_EDGE_FALLING;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		return EINT_EDGE_BOTH;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		return EINT_LEVEL_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		return EINT_LEVEL_LOW;
		break;
	default:
		return -EINVAL;
	}
}

static void s3c24xx_eint_set_handler(struct irq_data *d, unsigned int type)
{
	/* Edge- and level-triggered interrupts need different handlers */
	if (type & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(d, handle_edge_irq);
	else
		irq_set_handler_locked(d, handle_level_irq);
}

static void s3c24xx_eint_set_function(struct samsung_pinctrl_drv_data *d,
					struct samsung_pin_bank *bank, int pin)
{
	const struct samsung_pin_bank_type *bank_type = bank->type;
	unsigned long flags;
	void __iomem *reg;
	u8 shift;
	u32 mask;
	u32 val;

	/* Make sure that pin is configured as interrupt */
	reg = d->virt_base + bank->pctl_offset;
	shift = pin * bank_type->fld_width[PINCFG_TYPE_FUNC];
	mask = (1 << bank_type->fld_width[PINCFG_TYPE_FUNC]) - 1;

	spin_lock_irqsave(&bank->slock, flags);

	val = readl(reg);
	val &= ~(mask << shift);
	val |= bank->eint_func << shift;
	writel(val, reg);

	spin_unlock_irqrestore(&bank->slock, flags);
}

static int s3c24xx_eint_type(struct irq_data *data, unsigned int type)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(data);
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	int index = bank->eint_offset + data->hwirq;
	void __iomem *reg;
	int trigger;
	u8 shift;
	u32 val;

	trigger = s3c24xx_eint_get_trigger(type);
	if (trigger < 0) {
		dev_err(d->dev, "unsupported external interrupt type\n");
		return -EINVAL;
	}

	s3c24xx_eint_set_handler(data, type);

	/* Set up interrupt trigger */
	reg = d->virt_base + EINT_REG(index);
	shift = EINT_OFFS(index);

	val = readl(reg);
	val &= ~(EINT_MASK << shift);
	val |= trigger << shift;
	writel(val, reg);

	s3c24xx_eint_set_function(d, bank, data->hwirq);

	return 0;
}

/* Handling of EINTs 0-3 on all except S3C2412 and S3C2413 */

static void s3c2410_eint0_3_ack(struct irq_data *data)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(data);
	struct s3c24xx_eint_domain_data *ddata = bank->irq_domain->host_data;
	struct s3c24xx_eint_data *eint_data = ddata->eint_data;
	int parent_irq = eint_data->parents[data->hwirq];
	struct irq_chip *parent_chip = irq_get_chip(parent_irq);

	parent_chip->irq_ack(irq_get_irq_data(parent_irq));
}

static void s3c2410_eint0_3_mask(struct irq_data *data)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(data);
	struct s3c24xx_eint_domain_data *ddata = bank->irq_domain->host_data;
	struct s3c24xx_eint_data *eint_data = ddata->eint_data;
	int parent_irq = eint_data->parents[data->hwirq];
	struct irq_chip *parent_chip = irq_get_chip(parent_irq);

	parent_chip->irq_mask(irq_get_irq_data(parent_irq));
}

static void s3c2410_eint0_3_unmask(struct irq_data *data)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(data);
	struct s3c24xx_eint_domain_data *ddata = bank->irq_domain->host_data;
	struct s3c24xx_eint_data *eint_data = ddata->eint_data;
	int parent_irq = eint_data->parents[data->hwirq];
	struct irq_chip *parent_chip = irq_get_chip(parent_irq);

	parent_chip->irq_unmask(irq_get_irq_data(parent_irq));
}

static struct irq_chip s3c2410_eint0_3_chip = {
	.name		= "s3c2410-eint0_3",
	.irq_ack	= s3c2410_eint0_3_ack,
	.irq_mask	= s3c2410_eint0_3_mask,
	.irq_unmask	= s3c2410_eint0_3_unmask,
	.irq_set_type	= s3c24xx_eint_type,
};

static void s3c2410_demux_eint0_3(unsigned int irq, struct irq_desc *desc)
{
	struct irq_data *data = irq_desc_get_irq_data(desc);
	struct s3c24xx_eint_data *eint_data = irq_get_handler_data(irq);
	unsigned int virq;

	/* the first 4 eints have a simple 1 to 1 mapping */
	virq = irq_linear_revmap(eint_data->domains[data->hwirq], data->hwirq);
	/* Something must be really wrong if an unmapped EINT is unmasked */
	BUG_ON(!virq);

	generic_handle_irq(virq);
}

/* Handling of EINTs 0-3 on S3C2412 and S3C2413 */

static void s3c2412_eint0_3_ack(struct irq_data *data)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(data);
	struct samsung_pinctrl_drv_data *d = bank->drvdata;

	unsigned long bitval = 1UL << data->hwirq;
	writel(bitval, d->virt_base + EINTPEND_REG);
}

static void s3c2412_eint0_3_mask(struct irq_data *data)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(data);
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	unsigned long mask;

	mask = readl(d->virt_base + EINTMASK_REG);
	mask |= (1UL << data->hwirq);
	writel(mask, d->virt_base + EINTMASK_REG);
}

static void s3c2412_eint0_3_unmask(struct irq_data *data)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(data);
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	unsigned long mask;

	mask = readl(d->virt_base + EINTMASK_REG);
	mask &= ~(1UL << data->hwirq);
	writel(mask, d->virt_base + EINTMASK_REG);
}

static struct irq_chip s3c2412_eint0_3_chip = {
	.name		= "s3c2412-eint0_3",
	.irq_ack	= s3c2412_eint0_3_ack,
	.irq_mask	= s3c2412_eint0_3_mask,
	.irq_unmask	= s3c2412_eint0_3_unmask,
	.irq_set_type	= s3c24xx_eint_type,
};

static void s3c2412_demux_eint0_3(unsigned int irq, struct irq_desc *desc)
{
	struct irq_chip *chip = irq_get_chip(irq);
	struct irq_data *data = irq_desc_get_irq_data(desc);
	struct s3c24xx_eint_data *eint_data = irq_get_handler_data(irq);
	unsigned int virq;

	chained_irq_enter(chip, desc);

	/* the first 4 eints have a simple 1 to 1 mapping */
	virq = irq_linear_revmap(eint_data->domains[data->hwirq], data->hwirq);
	/* Something must be really wrong if an unmapped EINT is unmasked */
	BUG_ON(!virq);

	generic_handle_irq(virq);

	chained_irq_exit(chip, desc);
}

/* Handling of all other eints */

static void s3c24xx_eint_ack(struct irq_data *data)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(data);
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	unsigned char index = bank->eint_offset + data->hwirq;

	writel(1UL << index, d->virt_base + EINTPEND_REG);
}

static void s3c24xx_eint_mask(struct irq_data *data)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(data);
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	unsigned char index = bank->eint_offset + data->hwirq;
	unsigned long mask;

	mask = readl(d->virt_base + EINTMASK_REG);
	mask |= (1UL << index);
	writel(mask, d->virt_base + EINTMASK_REG);
}

static void s3c24xx_eint_unmask(struct irq_data *data)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(data);
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	unsigned char index = bank->eint_offset + data->hwirq;
	unsigned long mask;

	mask = readl(d->virt_base + EINTMASK_REG);
	mask &= ~(1UL << index);
	writel(mask, d->virt_base + EINTMASK_REG);
}

static struct irq_chip s3c24xx_eint_chip = {
	.name		= "s3c-eint",
	.irq_ack	= s3c24xx_eint_ack,
	.irq_mask	= s3c24xx_eint_mask,
	.irq_unmask	= s3c24xx_eint_unmask,
	.irq_set_type	= s3c24xx_eint_type,
};

static inline void s3c24xx_demux_eint(unsigned int irq, struct irq_desc *desc,
				      u32 offset, u32 range)
{
	struct irq_chip *chip = irq_get_chip(irq);
	struct s3c24xx_eint_data *data = irq_get_handler_data(irq);
	struct samsung_pinctrl_drv_data *d = data->drvdata;
	unsigned int pend, mask;

	chained_irq_enter(chip, desc);

	pend = readl(d->virt_base + EINTPEND_REG);
	mask = readl(d->virt_base + EINTMASK_REG);

	pend &= ~mask;
	pend &= range;

	while (pend) {
		unsigned int virq;

		irq = __ffs(pend);
		pend &= ~(1 << irq);
		virq = irq_linear_revmap(data->domains[irq], irq - offset);
		/* Something is really wrong if an unmapped EINT is unmasked */
		BUG_ON(!virq);

		generic_handle_irq(virq);
	}

	chained_irq_exit(chip, desc);
}

static void s3c24xx_demux_eint4_7(unsigned int irq, struct irq_desc *desc)
{
	s3c24xx_demux_eint(irq, desc, 0, 0xf0);
}

static void s3c24xx_demux_eint8_23(unsigned int irq, struct irq_desc *desc)
{
	s3c24xx_demux_eint(irq, desc, 8, 0xffff00);
}

static irq_flow_handler_t s3c2410_eint_handlers[NUM_EINT_IRQ] = {
	s3c2410_demux_eint0_3,
	s3c2410_demux_eint0_3,
	s3c2410_demux_eint0_3,
	s3c2410_demux_eint0_3,
	s3c24xx_demux_eint4_7,
	s3c24xx_demux_eint8_23,
};

static irq_flow_handler_t s3c2412_eint_handlers[NUM_EINT_IRQ] = {
	s3c2412_demux_eint0_3,
	s3c2412_demux_eint0_3,
	s3c2412_demux_eint0_3,
	s3c2412_demux_eint0_3,
	s3c24xx_demux_eint4_7,
	s3c24xx_demux_eint8_23,
};

static int s3c24xx_gpf_irq_map(struct irq_domain *h, unsigned int virq,
					irq_hw_number_t hw)
{
	struct s3c24xx_eint_domain_data *ddata = h->host_data;
	struct samsung_pin_bank *bank = ddata->bank;

	if (!(bank->eint_mask & (1 << (bank->eint_offset + hw))))
		return -EINVAL;

	if (hw <= 3) {
		if (ddata->eint0_3_parent_only)
			irq_set_chip_and_handler(virq, &s3c2410_eint0_3_chip,
						 handle_edge_irq);
		else
			irq_set_chip_and_handler(virq, &s3c2412_eint0_3_chip,
						 handle_edge_irq);
	} else {
		irq_set_chip_and_handler(virq, &s3c24xx_eint_chip,
					 handle_edge_irq);
	}
	irq_set_chip_data(virq, bank);
	set_irq_flags(virq, IRQF_VALID);
	return 0;
}

static const struct irq_domain_ops s3c24xx_gpf_irq_ops = {
	.map	= s3c24xx_gpf_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

static int s3c24xx_gpg_irq_map(struct irq_domain *h, unsigned int virq,
					irq_hw_number_t hw)
{
	struct s3c24xx_eint_domain_data *ddata = h->host_data;
	struct samsung_pin_bank *bank = ddata->bank;

	if (!(bank->eint_mask & (1 << (bank->eint_offset + hw))))
		return -EINVAL;

	irq_set_chip_and_handler(virq, &s3c24xx_eint_chip, handle_edge_irq);
	irq_set_chip_data(virq, bank);
	set_irq_flags(virq, IRQF_VALID);
	return 0;
}

static const struct irq_domain_ops s3c24xx_gpg_irq_ops = {
	.map	= s3c24xx_gpg_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

static const struct of_device_id s3c24xx_eint_irq_ids[] = {
	{ .compatible = "samsung,s3c2410-wakeup-eint", .data = (void *)1 },
	{ .compatible = "samsung,s3c2412-wakeup-eint", .data = (void *)0 },
	{ }
};

static int s3c24xx_eint_init(struct samsung_pinctrl_drv_data *d)
{
	struct device *dev = d->dev;
	const struct of_device_id *match;
	struct device_node *eint_np = NULL;
	struct device_node *np;
	struct samsung_pin_bank *bank;
	struct s3c24xx_eint_data *eint_data;
	const struct irq_domain_ops *ops;
	unsigned int i;
	bool eint0_3_parent_only;
	irq_flow_handler_t *handlers;

	for_each_child_of_node(dev->of_node, np) {
		match = of_match_node(s3c24xx_eint_irq_ids, np);
		if (match) {
			eint_np = np;
			eint0_3_parent_only = (bool)match->data;
			break;
		}
	}
	if (!eint_np)
		return -ENODEV;

	eint_data = devm_kzalloc(dev, sizeof(*eint_data), GFP_KERNEL);
	if (!eint_data)
		return -ENOMEM;

	eint_data->drvdata = d;

	handlers = eint0_3_parent_only ? s3c2410_eint_handlers
				       : s3c2412_eint_handlers;
	for (i = 0; i < NUM_EINT_IRQ; ++i) {
		unsigned int irq;

		irq = irq_of_parse_and_map(eint_np, i);
		if (!irq) {
			dev_err(dev, "failed to get wakeup EINT IRQ %d\n", i);
			return -ENXIO;
		}

		eint_data->parents[i] = irq;
		irq_set_chained_handler_and_data(irq, handlers[i], eint_data);
	}

	bank = d->pin_banks;
	for (i = 0; i < d->nr_banks; ++i, ++bank) {
		struct s3c24xx_eint_domain_data *ddata;
		unsigned int mask;
		unsigned int irq;
		unsigned int pin;

		if (bank->eint_type != EINT_TYPE_WKUP)
			continue;

		ddata = devm_kzalloc(dev, sizeof(*ddata), GFP_KERNEL);
		if (!ddata)
			return -ENOMEM;

		ddata->bank = bank;
		ddata->eint_data = eint_data;
		ddata->eint0_3_parent_only = eint0_3_parent_only;

		ops = (bank->eint_offset == 0) ? &s3c24xx_gpf_irq_ops
					       : &s3c24xx_gpg_irq_ops;

		bank->irq_domain = irq_domain_add_linear(bank->of_node,
				bank->nr_pins, ops, ddata);
		if (!bank->irq_domain) {
			dev_err(dev, "wkup irq domain add failed\n");
			return -ENXIO;
		}

		irq = bank->eint_offset;
		mask = bank->eint_mask;
		for (pin = 0; mask; ++pin, mask >>= 1) {
			if (irq >= NUM_EINT)
				break;
			if (!(mask & 1))
				continue;
			eint_data->domains[irq] = bank->irq_domain;
			++irq;
		}
	}

	return 0;
}

static const struct samsung_pin_bank_data s3c2412_pin_banks[] __initconst = {
	PIN_BANK_A(23, 0x000, "gpa"),
	PIN_BANK_2BIT(11, 0x010, "gpb"),
	PIN_BANK_2BIT(16, 0x020, "gpc"),
	PIN_BANK_2BIT(16, 0x030, "gpd"),
	PIN_BANK_2BIT(16, 0x040, "gpe"),
	PIN_BANK_2BIT_EINTW(8, 0x050, "gpf", 0, 0xff),
	PIN_BANK_2BIT_EINTW(16, 0x060, "gpg", 8, 0xffff00),
	PIN_BANK_2BIT(11, 0x070, "gph"),
	PIN_BANK_2BIT(13, 0x080, "gpj"),
};

const struct samsung_pin_ctrl s3c2412_pin_ctrl[] __initconst = {
	{
		.pin_banks	= s3c2412_pin_banks,
		.nr_banks	= ARRAY_SIZE(s3c2412_pin_banks),
		.eint_wkup_init = s3c24xx_eint_init,
	},
};

static const struct samsung_pin_bank_data s3c2416_pin_banks[] __initconst = {
	PIN_BANK_A(27, 0x000, "gpa"),
	PIN_BANK_2BIT(11, 0x010, "gpb"),
	PIN_BANK_2BIT(16, 0x020, "gpc"),
	PIN_BANK_2BIT(16, 0x030, "gpd"),
	PIN_BANK_2BIT(16, 0x040, "gpe"),
	PIN_BANK_2BIT_EINTW(8, 0x050, "gpf", 0, 0xff),
	PIN_BANK_2BIT_EINTW(8, 0x060, "gpg", 8, 0xff00),
	PIN_BANK_2BIT(15, 0x070, "gph"),
	PIN_BANK_2BIT(16, 0x0e0, "gpk"),
	PIN_BANK_2BIT(14, 0x0f0, "gpl"),
	PIN_BANK_2BIT(2, 0x100, "gpm"),
};

const struct samsung_pin_ctrl s3c2416_pin_ctrl[] __initconst = {
	{
		.pin_banks	= s3c2416_pin_banks,
		.nr_banks	= ARRAY_SIZE(s3c2416_pin_banks),
		.eint_wkup_init = s3c24xx_eint_init,
	},
};

static const struct samsung_pin_bank_data s3c2440_pin_banks[] __initconst = {
	PIN_BANK_A(25, 0x000, "gpa"),
	PIN_BANK_2BIT(11, 0x010, "gpb"),
	PIN_BANK_2BIT(16, 0x020, "gpc"),
	PIN_BANK_2BIT(16, 0x030, "gpd"),
	PIN_BANK_2BIT(16, 0x040, "gpe"),
	PIN_BANK_2BIT_EINTW(8, 0x050, "gpf", 0, 0xff),
	PIN_BANK_2BIT_EINTW(16, 0x060, "gpg", 8, 0xffff00),
	PIN_BANK_2BIT(11, 0x070, "gph"),
	PIN_BANK_2BIT(13, 0x0d0, "gpj"),
};

const struct samsung_pin_ctrl s3c2440_pin_ctrl[] __initconst = {
	{
		.pin_banks	= s3c2440_pin_banks,
		.nr_banks	= ARRAY_SIZE(s3c2440_pin_banks),
		.eint_wkup_init = s3c24xx_eint_init,
	},
};

static const struct samsung_pin_bank_data s3c2450_pin_banks[] __initconst = {
	PIN_BANK_A(28, 0x000, "gpa"),
	PIN_BANK_2BIT(11, 0x010, "gpb"),
	PIN_BANK_2BIT(16, 0x020, "gpc"),
	PIN_BANK_2BIT(16, 0x030, "gpd"),
	PIN_BANK_2BIT(16, 0x040, "gpe"),
	PIN_BANK_2BIT_EINTW(8, 0x050, "gpf", 0, 0xff),
	PIN_BANK_2BIT_EINTW(16, 0x060, "gpg", 8, 0xffff00),
	PIN_BANK_2BIT(15, 0x070, "gph"),
	PIN_BANK_2BIT(16, 0x0d0, "gpj"),
	PIN_BANK_2BIT(16, 0x0e0, "gpk"),
	PIN_BANK_2BIT(15, 0x0f0, "gpl"),
	PIN_BANK_2BIT(2, 0x100, "gpm"),
};

const struct samsung_pin_ctrl s3c2450_pin_ctrl[] __initconst = {
	{
		.pin_banks	= s3c2450_pin_banks,
		.nr_banks	= ARRAY_SIZE(s3c2450_pin_banks),
		.eint_wkup_init = s3c24xx_eint_init,
	},
};
