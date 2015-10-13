/*
 * S3C64xx specific support for pinctrl-samsung driver.
 *
 * Copyright (c) 2013 Tomasz Figa <tomasz.figa@gmail.com>
 *
 * Based on pinctrl-exynos.c, please see the file for original copyrights.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This file contains the Samsung S3C64xx specific information required by the
 * the Samsung pinctrl/gpiolib driver. It also includes the implementation of
 * external gpio and wakeup interrupt support.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/slab.h>
#include <linux/err.h>

#include "pinctrl-samsung.h"

#define NUM_EINT0		28
#define NUM_EINT0_IRQ		4
#define EINT_MAX_PER_REG	16
#define EINT_MAX_PER_GROUP	16

/* External GPIO and wakeup interrupt related definitions */
#define SVC_GROUP_SHIFT		4
#define SVC_GROUP_MASK		0xf
#define SVC_NUM_MASK		0xf
#define SVC_GROUP(x)		((x >> SVC_GROUP_SHIFT) & \
						SVC_GROUP_MASK)

#define EINT12CON_REG		0x200
#define EINT12MASK_REG		0x240
#define EINT12PEND_REG		0x260

#define EINT_OFFS(i)		((i) % (2 * EINT_MAX_PER_GROUP))
#define EINT_GROUP(i)		((i) / EINT_MAX_PER_GROUP)
#define EINT_REG(g)		(4 * ((g) / 2))

#define EINTCON_REG(i)		(EINT12CON_REG + EINT_REG(EINT_GROUP(i)))
#define EINTMASK_REG(i)		(EINT12MASK_REG + EINT_REG(EINT_GROUP(i)))
#define EINTPEND_REG(i)		(EINT12PEND_REG + EINT_REG(EINT_GROUP(i)))

#define SERVICE_REG		0x284
#define SERVICEPEND_REG		0x288

#define EINT0CON0_REG		0x900
#define EINT0MASK_REG		0x920
#define EINT0PEND_REG		0x924

/* S3C64xx specific external interrupt trigger types */
#define EINT_LEVEL_LOW		0
#define EINT_LEVEL_HIGH		1
#define EINT_EDGE_FALLING	2
#define EINT_EDGE_RISING	4
#define EINT_EDGE_BOTH		6
#define EINT_CON_MASK		0xF
#define EINT_CON_LEN		4

static const struct samsung_pin_bank_type bank_type_4bit_off = {
	.fld_width = { 4, 1, 2, 0, 2, 2, },
	.reg_offset = { 0x00, 0x04, 0x08, 0, 0x0c, 0x10, },
};

static const struct samsung_pin_bank_type bank_type_4bit_alive = {
	.fld_width = { 4, 1, 2, },
	.reg_offset = { 0x00, 0x04, 0x08, },
};

static const struct samsung_pin_bank_type bank_type_4bit2_off = {
	.fld_width = { 4, 1, 2, 0, 2, 2, },
	.reg_offset = { 0x00, 0x08, 0x0c, 0, 0x10, 0x14, },
};

static const struct samsung_pin_bank_type bank_type_4bit2_alive = {
	.fld_width = { 4, 1, 2, },
	.reg_offset = { 0x00, 0x08, 0x0c, },
};

static const struct samsung_pin_bank_type bank_type_2bit_off = {
	.fld_width = { 2, 1, 2, 0, 2, 2, },
	.reg_offset = { 0x00, 0x04, 0x08, 0, 0x0c, 0x10, },
};

static const struct samsung_pin_bank_type bank_type_2bit_alive = {
	.fld_width = { 2, 1, 2, },
	.reg_offset = { 0x00, 0x04, 0x08, },
};

#define PIN_BANK_4BIT(pins, reg, id)			\
	{						\
		.type		= &bank_type_4bit_off,	\
		.pctl_offset	= reg,			\
		.nr_pins	= pins,			\
		.eint_type	= EINT_TYPE_NONE,	\
		.name		= id			\
	}

#define PIN_BANK_4BIT_EINTG(pins, reg, id, eoffs)	\
	{						\
		.type		= &bank_type_4bit_off,	\
		.pctl_offset	= reg,			\
		.nr_pins	= pins,			\
		.eint_type	= EINT_TYPE_GPIO,	\
		.eint_func	= 7,			\
		.eint_mask	= (1 << (pins)) - 1,	\
		.eint_offset	= eoffs,		\
		.name		= id			\
	}

#define PIN_BANK_4BIT_EINTW(pins, reg, id, eoffs, emask) \
	{						\
		.type		= &bank_type_4bit_alive,\
		.pctl_offset	= reg,			\
		.nr_pins	= pins,			\
		.eint_type	= EINT_TYPE_WKUP,	\
		.eint_func	= 3,			\
		.eint_mask	= emask,		\
		.eint_offset	= eoffs,		\
		.name		= id			\
	}

#define PIN_BANK_4BIT2_EINTG(pins, reg, id, eoffs)	\
	{						\
		.type		= &bank_type_4bit2_off,	\
		.pctl_offset	= reg,			\
		.nr_pins	= pins,			\
		.eint_type	= EINT_TYPE_GPIO,	\
		.eint_func	= 7,			\
		.eint_mask	= (1 << (pins)) - 1,	\
		.eint_offset	= eoffs,		\
		.name		= id			\
	}

#define PIN_BANK_4BIT2_EINTW(pins, reg, id, eoffs, emask) \
	{						\
		.type		= &bank_type_4bit2_alive,\
		.pctl_offset	= reg,			\
		.nr_pins	= pins,			\
		.eint_type	= EINT_TYPE_WKUP,	\
		.eint_func	= 3,			\
		.eint_mask	= emask,		\
		.eint_offset	= eoffs,		\
		.name		= id			\
	}

#define PIN_BANK_4BIT2_ALIVE(pins, reg, id)		\
	{						\
		.type		= &bank_type_4bit2_alive,\
		.pctl_offset	= reg,			\
		.nr_pins	= pins,			\
		.eint_type	= EINT_TYPE_NONE,	\
		.name		= id			\
	}

#define PIN_BANK_2BIT(pins, reg, id)			\
	{						\
		.type		= &bank_type_2bit_off,	\
		.pctl_offset	= reg,			\
		.nr_pins	= pins,			\
		.eint_type	= EINT_TYPE_NONE,	\
		.name		= id			\
	}

#define PIN_BANK_2BIT_EINTG(pins, reg, id, eoffs, emask) \
	{						\
		.type		= &bank_type_2bit_off,	\
		.pctl_offset	= reg,			\
		.nr_pins	= pins,			\
		.eint_type	= EINT_TYPE_GPIO,	\
		.eint_func	= 3,			\
		.eint_mask	= emask,		\
		.eint_offset	= eoffs,		\
		.name		= id			\
	}

#define PIN_BANK_2BIT_EINTW(pins, reg, id, eoffs)	\
	{						\
		.type		= &bank_type_2bit_alive,\
		.pctl_offset	= reg,			\
		.nr_pins	= pins,			\
		.eint_type	= EINT_TYPE_WKUP,	\
		.eint_func	= 2,			\
		.eint_mask	= (1 << (pins)) - 1,	\
		.eint_offset	= eoffs,		\
		.name		= id			\
	}

/**
 * struct s3c64xx_eint0_data: EINT0 common data
 * @drvdata: pin controller driver data
 * @domains: IRQ domains of particular EINT0 interrupts
 * @pins: pin offsets inside of banks of particular EINT0 interrupts
 */
struct s3c64xx_eint0_data {
	struct samsung_pinctrl_drv_data *drvdata;
	struct irq_domain *domains[NUM_EINT0];
	u8 pins[NUM_EINT0];
};

/**
 * struct s3c64xx_eint0_domain_data: EINT0 per-domain data
 * @bank: pin bank related to the domain
 * @eints: EINT0 interrupts related to the domain
 */
struct s3c64xx_eint0_domain_data {
	struct samsung_pin_bank *bank;
	u8 eints[];
};

/**
 * struct s3c64xx_eint_gpio_data: GPIO EINT data
 * @drvdata: pin controller driver data
 * @domains: array of domains related to EINT interrupt groups
 */
struct s3c64xx_eint_gpio_data {
	struct samsung_pinctrl_drv_data *drvdata;
	struct irq_domain *domains[];
};

/*
 * Common functions for S3C64xx EINT configuration
 */

static int s3c64xx_irq_get_trigger(unsigned int type)
{
	int trigger;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		trigger = EINT_EDGE_RISING;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		trigger = EINT_EDGE_FALLING;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		trigger = EINT_EDGE_BOTH;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		trigger = EINT_LEVEL_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		trigger = EINT_LEVEL_LOW;
		break;
	default:
		return -EINVAL;
	}

	return trigger;
}

static void s3c64xx_irq_set_handler(struct irq_data *d, unsigned int type)
{
	/* Edge- and level-triggered interrupts need different handlers */
	if (type & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(d, handle_edge_irq);
	else
		irq_set_handler_locked(d, handle_level_irq);
}

static void s3c64xx_irq_set_function(struct samsung_pinctrl_drv_data *d,
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
	shift = pin;
	if (bank_type->fld_width[PINCFG_TYPE_FUNC] * shift >= 32) {
		/* 4-bit bank type with 2 con regs */
		reg += 4;
		shift -= 8;
	}

	shift = shift * bank_type->fld_width[PINCFG_TYPE_FUNC];
	mask = (1 << bank_type->fld_width[PINCFG_TYPE_FUNC]) - 1;

	spin_lock_irqsave(&bank->slock, flags);

	val = readl(reg);
	val &= ~(mask << shift);
	val |= bank->eint_func << shift;
	writel(val, reg);

	spin_unlock_irqrestore(&bank->slock, flags);
}

/*
 * Functions for EINT GPIO configuration (EINT groups 1-9)
 */

static inline void s3c64xx_gpio_irq_set_mask(struct irq_data *irqd, bool mask)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	unsigned char index = EINT_OFFS(bank->eint_offset) + irqd->hwirq;
	void __iomem *reg = d->virt_base + EINTMASK_REG(bank->eint_offset);
	u32 val;

	val = readl(reg);
	if (mask)
		val |= 1 << index;
	else
		val &= ~(1 << index);
	writel(val, reg);
}

static void s3c64xx_gpio_irq_unmask(struct irq_data *irqd)
{
	s3c64xx_gpio_irq_set_mask(irqd, false);
}

static void s3c64xx_gpio_irq_mask(struct irq_data *irqd)
{
	s3c64xx_gpio_irq_set_mask(irqd, true);
}

static void s3c64xx_gpio_irq_ack(struct irq_data *irqd)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	unsigned char index = EINT_OFFS(bank->eint_offset) + irqd->hwirq;
	void __iomem *reg = d->virt_base + EINTPEND_REG(bank->eint_offset);

	writel(1 << index, reg);
}

static int s3c64xx_gpio_irq_set_type(struct irq_data *irqd, unsigned int type)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	void __iomem *reg;
	int trigger;
	u8 shift;
	u32 val;

	trigger = s3c64xx_irq_get_trigger(type);
	if (trigger < 0) {
		pr_err("unsupported external interrupt type\n");
		return -EINVAL;
	}

	s3c64xx_irq_set_handler(irqd, type);

	/* Set up interrupt trigger */
	reg = d->virt_base + EINTCON_REG(bank->eint_offset);
	shift = EINT_OFFS(bank->eint_offset) + irqd->hwirq;
	shift = 4 * (shift / 4); /* 4 EINTs per trigger selector */

	val = readl(reg);
	val &= ~(EINT_CON_MASK << shift);
	val |= trigger << shift;
	writel(val, reg);

	s3c64xx_irq_set_function(d, bank, irqd->hwirq);

	return 0;
}

/*
 * irq_chip for gpio interrupts.
 */
static struct irq_chip s3c64xx_gpio_irq_chip = {
	.name		= "GPIO",
	.irq_unmask	= s3c64xx_gpio_irq_unmask,
	.irq_mask	= s3c64xx_gpio_irq_mask,
	.irq_ack	= s3c64xx_gpio_irq_ack,
	.irq_set_type	= s3c64xx_gpio_irq_set_type,
};

static int s3c64xx_gpio_irq_map(struct irq_domain *h, unsigned int virq,
					irq_hw_number_t hw)
{
	struct samsung_pin_bank *bank = h->host_data;

	if (!(bank->eint_mask & (1 << hw)))
		return -EINVAL;

	irq_set_chip_and_handler(virq,
				&s3c64xx_gpio_irq_chip, handle_level_irq);
	irq_set_chip_data(virq, bank);

	return 0;
}

/*
 * irq domain callbacks for external gpio interrupt controller.
 */
static const struct irq_domain_ops s3c64xx_gpio_irqd_ops = {
	.map	= s3c64xx_gpio_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

static void s3c64xx_eint_gpio_irq(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct s3c64xx_eint_gpio_data *data = irq_desc_get_handler_data(desc);
	struct samsung_pinctrl_drv_data *drvdata = data->drvdata;

	chained_irq_enter(chip, desc);

	do {
		unsigned int svc;
		unsigned int group;
		unsigned int pin;
		unsigned int virq;

		svc = readl(drvdata->virt_base + SERVICE_REG);
		group = SVC_GROUP(svc);
		pin = svc & SVC_NUM_MASK;

		if (!group)
			break;

		/* Group 1 is used for two pin banks */
		if (group == 1) {
			if (pin < 8)
				group = 0;
			else
				pin -= 8;
		}

		virq = irq_linear_revmap(data->domains[group], pin);
		/*
		 * Something must be really wrong if an unmapped EINT
		 * was unmasked...
		 */
		BUG_ON(!virq);

		generic_handle_irq(virq);
	} while (1);

	chained_irq_exit(chip, desc);
}

/**
 * s3c64xx_eint_gpio_init() - setup handling of external gpio interrupts.
 * @d: driver data of samsung pinctrl driver.
 */
static int s3c64xx_eint_gpio_init(struct samsung_pinctrl_drv_data *d)
{
	struct s3c64xx_eint_gpio_data *data;
	struct samsung_pin_bank *bank;
	struct device *dev = d->dev;
	unsigned int nr_domains;
	unsigned int i;

	if (!d->irq) {
		dev_err(dev, "irq number not available\n");
		return -EINVAL;
	}

	nr_domains = 0;
	bank = d->pin_banks;
	for (i = 0; i < d->nr_banks; ++i, ++bank) {
		unsigned int nr_eints;
		unsigned int mask;

		if (bank->eint_type != EINT_TYPE_GPIO)
			continue;

		mask = bank->eint_mask;
		nr_eints = fls(mask);

		bank->irq_domain = irq_domain_add_linear(bank->of_node,
					nr_eints, &s3c64xx_gpio_irqd_ops, bank);
		if (!bank->irq_domain) {
			dev_err(dev, "gpio irq domain add failed\n");
			return -ENXIO;
		}

		++nr_domains;
	}

	data = devm_kzalloc(dev, sizeof(*data)
			+ nr_domains * sizeof(*data->domains), GFP_KERNEL);
	if (!data) {
		dev_err(dev, "failed to allocate handler data\n");
		return -ENOMEM;
	}
	data->drvdata = d;

	bank = d->pin_banks;
	nr_domains = 0;
	for (i = 0; i < d->nr_banks; ++i, ++bank) {
		if (bank->eint_type != EINT_TYPE_GPIO)
			continue;

		data->domains[nr_domains++] = bank->irq_domain;
	}

	irq_set_chained_handler_and_data(d->irq, s3c64xx_eint_gpio_irq, data);

	return 0;
}

/*
 * Functions for configuration of EINT0 wake-up interrupts
 */

static inline void s3c64xx_eint0_irq_set_mask(struct irq_data *irqd, bool mask)
{
	struct s3c64xx_eint0_domain_data *ddata =
					irq_data_get_irq_chip_data(irqd);
	struct samsung_pinctrl_drv_data *d = ddata->bank->drvdata;
	u32 val;

	val = readl(d->virt_base + EINT0MASK_REG);
	if (mask)
		val |= 1 << ddata->eints[irqd->hwirq];
	else
		val &= ~(1 << ddata->eints[irqd->hwirq]);
	writel(val, d->virt_base + EINT0MASK_REG);
}

static void s3c64xx_eint0_irq_unmask(struct irq_data *irqd)
{
	s3c64xx_eint0_irq_set_mask(irqd, false);
}

static void s3c64xx_eint0_irq_mask(struct irq_data *irqd)
{
	s3c64xx_eint0_irq_set_mask(irqd, true);
}

static void s3c64xx_eint0_irq_ack(struct irq_data *irqd)
{
	struct s3c64xx_eint0_domain_data *ddata =
					irq_data_get_irq_chip_data(irqd);
	struct samsung_pinctrl_drv_data *d = ddata->bank->drvdata;

	writel(1 << ddata->eints[irqd->hwirq],
					d->virt_base + EINT0PEND_REG);
}

static int s3c64xx_eint0_irq_set_type(struct irq_data *irqd, unsigned int type)
{
	struct s3c64xx_eint0_domain_data *ddata =
					irq_data_get_irq_chip_data(irqd);
	struct samsung_pin_bank *bank = ddata->bank;
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	void __iomem *reg;
	int trigger;
	u8 shift;
	u32 val;

	trigger = s3c64xx_irq_get_trigger(type);
	if (trigger < 0) {
		pr_err("unsupported external interrupt type\n");
		return -EINVAL;
	}

	s3c64xx_irq_set_handler(irqd, type);

	/* Set up interrupt trigger */
	reg = d->virt_base + EINT0CON0_REG;
	shift = ddata->eints[irqd->hwirq];
	if (shift >= EINT_MAX_PER_REG) {
		reg += 4;
		shift -= EINT_MAX_PER_REG;
	}
	shift = EINT_CON_LEN * (shift / 2);

	val = readl(reg);
	val &= ~(EINT_CON_MASK << shift);
	val |= trigger << shift;
	writel(val, reg);

	s3c64xx_irq_set_function(d, bank, irqd->hwirq);

	return 0;
}

/*
 * irq_chip for wakeup interrupts
 */
static struct irq_chip s3c64xx_eint0_irq_chip = {
	.name		= "EINT0",
	.irq_unmask	= s3c64xx_eint0_irq_unmask,
	.irq_mask	= s3c64xx_eint0_irq_mask,
	.irq_ack	= s3c64xx_eint0_irq_ack,
	.irq_set_type	= s3c64xx_eint0_irq_set_type,
};

static inline void s3c64xx_irq_demux_eint(struct irq_desc *desc, u32 range)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct s3c64xx_eint0_data *data = irq_desc_get_handler_data(desc);
	struct samsung_pinctrl_drv_data *drvdata = data->drvdata;
	unsigned int pend, mask;

	chained_irq_enter(chip, desc);

	pend = readl(drvdata->virt_base + EINT0PEND_REG);
	mask = readl(drvdata->virt_base + EINT0MASK_REG);

	pend = pend & range & ~mask;
	pend &= range;

	while (pend) {
		unsigned int virq, irq;

		irq = fls(pend) - 1;
		pend &= ~(1 << irq);
		virq = irq_linear_revmap(data->domains[irq], data->pins[irq]);
		/*
		 * Something must be really wrong if an unmapped EINT
		 * was unmasked...
		 */
		BUG_ON(!virq);

		generic_handle_irq(virq);
	}

	chained_irq_exit(chip, desc);
}

static void s3c64xx_demux_eint0_3(struct irq_desc *desc)
{
	s3c64xx_irq_demux_eint(desc, 0xf);
}

static void s3c64xx_demux_eint4_11(struct irq_desc *desc)
{
	s3c64xx_irq_demux_eint(desc, 0xff0);
}

static void s3c64xx_demux_eint12_19(struct irq_desc *desc)
{
	s3c64xx_irq_demux_eint(desc, 0xff000);
}

static void s3c64xx_demux_eint20_27(struct irq_desc *desc)
{
	s3c64xx_irq_demux_eint(desc, 0xff00000);
}

static irq_flow_handler_t s3c64xx_eint0_handlers[NUM_EINT0_IRQ] = {
	s3c64xx_demux_eint0_3,
	s3c64xx_demux_eint4_11,
	s3c64xx_demux_eint12_19,
	s3c64xx_demux_eint20_27,
};

static int s3c64xx_eint0_irq_map(struct irq_domain *h, unsigned int virq,
					irq_hw_number_t hw)
{
	struct s3c64xx_eint0_domain_data *ddata = h->host_data;
	struct samsung_pin_bank *bank = ddata->bank;

	if (!(bank->eint_mask & (1 << hw)))
		return -EINVAL;

	irq_set_chip_and_handler(virq,
				&s3c64xx_eint0_irq_chip, handle_level_irq);
	irq_set_chip_data(virq, ddata);

	return 0;
}

/*
 * irq domain callbacks for external wakeup interrupt controller.
 */
static const struct irq_domain_ops s3c64xx_eint0_irqd_ops = {
	.map	= s3c64xx_eint0_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

/* list of external wakeup controllers supported */
static const struct of_device_id s3c64xx_eint0_irq_ids[] = {
	{ .compatible = "samsung,s3c64xx-wakeup-eint", },
	{ }
};

/**
 * s3c64xx_eint_eint0_init() - setup handling of external wakeup interrupts.
 * @d: driver data of samsung pinctrl driver.
 */
static int s3c64xx_eint_eint0_init(struct samsung_pinctrl_drv_data *d)
{
	struct device *dev = d->dev;
	struct device_node *eint0_np = NULL;
	struct device_node *np;
	struct samsung_pin_bank *bank;
	struct s3c64xx_eint0_data *data;
	unsigned int i;

	for_each_child_of_node(dev->of_node, np) {
		if (of_match_node(s3c64xx_eint0_irq_ids, np)) {
			eint0_np = np;
			break;
		}
	}
	if (!eint0_np)
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(dev, "could not allocate memory for wkup eint data\n");
		return -ENOMEM;
	}
	data->drvdata = d;

	for (i = 0; i < NUM_EINT0_IRQ; ++i) {
		unsigned int irq;

		irq = irq_of_parse_and_map(eint0_np, i);
		if (!irq) {
			dev_err(dev, "failed to get wakeup EINT IRQ %d\n", i);
			return -ENXIO;
		}

		irq_set_chained_handler_and_data(irq,
						 s3c64xx_eint0_handlers[i],
						 data);
	}

	bank = d->pin_banks;
	for (i = 0; i < d->nr_banks; ++i, ++bank) {
		struct s3c64xx_eint0_domain_data *ddata;
		unsigned int nr_eints;
		unsigned int mask;
		unsigned int irq;
		unsigned int pin;

		if (bank->eint_type != EINT_TYPE_WKUP)
			continue;

		mask = bank->eint_mask;
		nr_eints = fls(mask);

		ddata = devm_kzalloc(dev,
				sizeof(*ddata) + nr_eints, GFP_KERNEL);
		if (!ddata) {
			dev_err(dev, "failed to allocate domain data\n");
			return -ENOMEM;
		}
		ddata->bank = bank;

		bank->irq_domain = irq_domain_add_linear(bank->of_node,
				nr_eints, &s3c64xx_eint0_irqd_ops, ddata);
		if (!bank->irq_domain) {
			dev_err(dev, "wkup irq domain add failed\n");
			return -ENXIO;
		}

		irq = bank->eint_offset;
		mask = bank->eint_mask;
		for (pin = 0; mask; ++pin, mask >>= 1) {
			if (!(mask & 1))
				continue;
			data->domains[irq] = bank->irq_domain;
			data->pins[irq] = pin;
			ddata->eints[pin] = irq;
			++irq;
		}
	}

	return 0;
}

/* pin banks of s3c64xx pin-controller 0 */
static const struct samsung_pin_bank_data s3c64xx_pin_banks0[] __initconst = {
	PIN_BANK_4BIT_EINTG(8, 0x000, "gpa", 0),
	PIN_BANK_4BIT_EINTG(7, 0x020, "gpb", 8),
	PIN_BANK_4BIT_EINTG(8, 0x040, "gpc", 16),
	PIN_BANK_4BIT_EINTG(5, 0x060, "gpd", 32),
	PIN_BANK_4BIT(5, 0x080, "gpe"),
	PIN_BANK_2BIT_EINTG(16, 0x0a0, "gpf", 48, 0x3fff),
	PIN_BANK_4BIT_EINTG(7, 0x0c0, "gpg", 64),
	PIN_BANK_4BIT2_EINTG(10, 0x0e0, "gph", 80),
	PIN_BANK_2BIT(16, 0x100, "gpi"),
	PIN_BANK_2BIT(12, 0x120, "gpj"),
	PIN_BANK_4BIT2_ALIVE(16, 0x800, "gpk"),
	PIN_BANK_4BIT2_EINTW(15, 0x810, "gpl", 16, 0x7f00),
	PIN_BANK_4BIT_EINTW(6, 0x820, "gpm", 23, 0x1f),
	PIN_BANK_2BIT_EINTW(16, 0x830, "gpn", 0),
	PIN_BANK_2BIT_EINTG(16, 0x140, "gpo", 96, 0xffff),
	PIN_BANK_2BIT_EINTG(15, 0x160, "gpp", 112, 0x7fff),
	PIN_BANK_2BIT_EINTG(9, 0x180, "gpq", 128, 0x1ff),
};

/*
 * Samsung pinctrl driver data for S3C64xx SoC. S3C64xx SoC includes
 * one gpio/pin-mux/pinconfig controller.
 */
const struct samsung_pin_ctrl s3c64xx_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 1 data */
		.pin_banks	= s3c64xx_pin_banks0,
		.nr_banks	= ARRAY_SIZE(s3c64xx_pin_banks0),
		.eint_gpio_init = s3c64xx_eint_gpio_init,
		.eint_wkup_init = s3c64xx_eint_eint0_init,
	},
};
