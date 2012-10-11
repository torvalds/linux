/*
 * Exynos specific support for Samsung pinctrl/gpiolib driver with eint support.
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 * Copyright (c) 2012 Linaro Ltd
 *		http://www.linaro.org
 *
 * Author: Thomas Abraham <thomas.ab@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This file contains the Samsung Exynos specific information required by the
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
#include <linux/slab.h>
#include <linux/err.h>

#include <asm/mach/irq.h>

#include "pinctrl-samsung.h"
#include "pinctrl-exynos.h"

/* list of external wakeup controllers supported */
static const struct of_device_id exynos_wkup_irq_ids[] = {
	{ .compatible = "samsung,exynos4210-wakeup-eint", },
};

static void exynos_gpio_irq_unmask(struct irq_data *irqd)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	unsigned long reg_mask = d->ctrl->geint_mask + bank->eint_offset;
	unsigned long mask;

	mask = readl(d->virt_base + reg_mask);
	mask &= ~(1 << irqd->hwirq);
	writel(mask, d->virt_base + reg_mask);
}

static void exynos_gpio_irq_mask(struct irq_data *irqd)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	unsigned long reg_mask = d->ctrl->geint_mask + bank->eint_offset;
	unsigned long mask;

	mask = readl(d->virt_base + reg_mask);
	mask |= 1 << irqd->hwirq;
	writel(mask, d->virt_base + reg_mask);
}

static void exynos_gpio_irq_ack(struct irq_data *irqd)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	unsigned long reg_pend = d->ctrl->geint_pend + bank->eint_offset;

	writel(1 << irqd->hwirq, d->virt_base + reg_pend);
}

static int exynos_gpio_irq_set_type(struct irq_data *irqd, unsigned int type)
{
	struct samsung_pin_bank *bank = irq_data_get_irq_chip_data(irqd);
	struct samsung_pinctrl_drv_data *d = bank->drvdata;
	struct samsung_pin_ctrl *ctrl = d->ctrl;
	unsigned int pin = irqd->hwirq;
	unsigned int shift = EXYNOS_EINT_CON_LEN * pin;
	unsigned int con, trig_type;
	unsigned long reg_con = ctrl->geint_con + bank->eint_offset;
	unsigned int mask;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		trig_type = EXYNOS_EINT_EDGE_RISING;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		trig_type = EXYNOS_EINT_EDGE_FALLING;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		trig_type = EXYNOS_EINT_EDGE_BOTH;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		trig_type = EXYNOS_EINT_LEVEL_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		trig_type = EXYNOS_EINT_LEVEL_LOW;
		break;
	default:
		pr_err("unsupported external interrupt type\n");
		return -EINVAL;
	}

	if (type & IRQ_TYPE_EDGE_BOTH)
		__irq_set_handler_locked(irqd->irq, handle_edge_irq);
	else
		__irq_set_handler_locked(irqd->irq, handle_level_irq);

	con = readl(d->virt_base + reg_con);
	con &= ~(EXYNOS_EINT_CON_MASK << shift);
	con |= trig_type << shift;
	writel(con, d->virt_base + reg_con);

	reg_con = bank->pctl_offset;
	shift = pin * bank->func_width;
	mask = (1 << bank->func_width) - 1;

	con = readl(d->virt_base + reg_con);
	con &= ~(mask << shift);
	con |= EXYNOS_EINT_FUNC << shift;
	writel(con, d->virt_base + reg_con);

	return 0;
}

/*
 * irq_chip for gpio interrupts.
 */
static struct irq_chip exynos_gpio_irq_chip = {
	.name		= "exynos_gpio_irq_chip",
	.irq_unmask	= exynos_gpio_irq_unmask,
	.irq_mask	= exynos_gpio_irq_mask,
	.irq_ack		= exynos_gpio_irq_ack,
	.irq_set_type	= exynos_gpio_irq_set_type,
};

static int exynos_gpio_irq_map(struct irq_domain *h, unsigned int virq,
					irq_hw_number_t hw)
{
	struct samsung_pin_bank *b = h->host_data;

	irq_set_chip_data(virq, b);
	irq_set_chip_and_handler(virq, &exynos_gpio_irq_chip,
					handle_level_irq);
	set_irq_flags(virq, IRQF_VALID);
	return 0;
}

/*
 * irq domain callbacks for external gpio interrupt controller.
 */
static const struct irq_domain_ops exynos_gpio_irqd_ops = {
	.map	= exynos_gpio_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

static irqreturn_t exynos_eint_gpio_irq(int irq, void *data)
{
	struct samsung_pinctrl_drv_data *d = data;
	struct samsung_pin_ctrl *ctrl = d->ctrl;
	struct samsung_pin_bank *bank = ctrl->pin_banks;
	unsigned int svc, group, pin, virq;

	svc = readl(d->virt_base + ctrl->svc);
	group = EXYNOS_SVC_GROUP(svc);
	pin = svc & EXYNOS_SVC_NUM_MASK;

	if (!group)
		return IRQ_HANDLED;
	bank += (group - 1);

	virq = irq_linear_revmap(bank->irq_domain, pin);
	if (!virq)
		return IRQ_NONE;
	generic_handle_irq(virq);
	return IRQ_HANDLED;
}

/*
 * exynos_eint_gpio_init() - setup handling of external gpio interrupts.
 * @d: driver data of samsung pinctrl driver.
 */
static int exynos_eint_gpio_init(struct samsung_pinctrl_drv_data *d)
{
	struct samsung_pin_bank *bank;
	struct device *dev = d->dev;
	unsigned int ret;
	unsigned int i;

	if (!d->irq) {
		dev_err(dev, "irq number not available\n");
		return -EINVAL;
	}

	ret = devm_request_irq(dev, d->irq, exynos_eint_gpio_irq,
					0, dev_name(dev), d);
	if (ret) {
		dev_err(dev, "irq request failed\n");
		return -ENXIO;
	}

	bank = d->ctrl->pin_banks;
	for (i = 0; i < d->ctrl->nr_banks; ++i, ++bank) {
		if (bank->eint_type != EINT_TYPE_GPIO)
			continue;
		bank->irq_domain = irq_domain_add_linear(bank->of_node,
				bank->nr_pins, &exynos_gpio_irqd_ops, bank);
		if (!bank->irq_domain) {
			dev_err(dev, "gpio irq domain add failed\n");
			return -ENXIO;
		}
	}

	return 0;
}

static void exynos_wkup_irq_unmask(struct irq_data *irqd)
{
	struct samsung_pinctrl_drv_data *d = irq_data_get_irq_chip_data(irqd);
	unsigned int bank = irqd->hwirq / EXYNOS_EINT_MAX_PER_BANK;
	unsigned int pin = irqd->hwirq & (EXYNOS_EINT_MAX_PER_BANK - 1);
	unsigned long reg_mask = d->ctrl->weint_mask + (bank << 2);
	unsigned long mask;

	mask = readl(d->virt_base + reg_mask);
	mask &= ~(1 << pin);
	writel(mask, d->virt_base + reg_mask);
}

static void exynos_wkup_irq_mask(struct irq_data *irqd)
{
	struct samsung_pinctrl_drv_data *d = irq_data_get_irq_chip_data(irqd);
	unsigned int bank = irqd->hwirq / EXYNOS_EINT_MAX_PER_BANK;
	unsigned int pin = irqd->hwirq & (EXYNOS_EINT_MAX_PER_BANK - 1);
	unsigned long reg_mask = d->ctrl->weint_mask + (bank << 2);
	unsigned long mask;

	mask = readl(d->virt_base + reg_mask);
	mask |= 1 << pin;
	writel(mask, d->virt_base + reg_mask);
}

static void exynos_wkup_irq_ack(struct irq_data *irqd)
{
	struct samsung_pinctrl_drv_data *d = irq_data_get_irq_chip_data(irqd);
	unsigned int bank = irqd->hwirq / EXYNOS_EINT_MAX_PER_BANK;
	unsigned int pin = irqd->hwirq & (EXYNOS_EINT_MAX_PER_BANK - 1);
	unsigned long pend = d->ctrl->weint_pend + (bank << 2);

	writel(1 << pin, d->virt_base + pend);
}

static int exynos_wkup_irq_set_type(struct irq_data *irqd, unsigned int type)
{
	struct samsung_pinctrl_drv_data *d = irq_data_get_irq_chip_data(irqd);
	unsigned int bank = irqd->hwirq / EXYNOS_EINT_MAX_PER_BANK;
	unsigned int pin = irqd->hwirq & (EXYNOS_EINT_MAX_PER_BANK - 1);
	unsigned long reg_con = d->ctrl->weint_con + (bank << 2);
	unsigned long shift = EXYNOS_EINT_CON_LEN * pin;
	unsigned long con, trig_type;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		trig_type = EXYNOS_EINT_EDGE_RISING;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		trig_type = EXYNOS_EINT_EDGE_FALLING;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		trig_type = EXYNOS_EINT_EDGE_BOTH;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		trig_type = EXYNOS_EINT_LEVEL_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		trig_type = EXYNOS_EINT_LEVEL_LOW;
		break;
	default:
		pr_err("unsupported external interrupt type\n");
		return -EINVAL;
	}

	if (type & IRQ_TYPE_EDGE_BOTH)
		__irq_set_handler_locked(irqd->irq, handle_edge_irq);
	else
		__irq_set_handler_locked(irqd->irq, handle_level_irq);

	con = readl(d->virt_base + reg_con);
	con &= ~(EXYNOS_EINT_CON_MASK << shift);
	con |= trig_type << shift;
	writel(con, d->virt_base + reg_con);
	return 0;
}

/*
 * irq_chip for wakeup interrupts
 */
static struct irq_chip exynos_wkup_irq_chip = {
	.name	= "exynos_wkup_irq_chip",
	.irq_unmask	= exynos_wkup_irq_unmask,
	.irq_mask	= exynos_wkup_irq_mask,
	.irq_ack	= exynos_wkup_irq_ack,
	.irq_set_type	= exynos_wkup_irq_set_type,
};

/* interrupt handler for wakeup interrupts 0..15 */
static void exynos_irq_eint0_15(unsigned int irq, struct irq_desc *desc)
{
	struct exynos_weint_data *eintd = irq_get_handler_data(irq);
	struct irq_chip *chip = irq_get_chip(irq);
	int eint_irq;

	chained_irq_enter(chip, desc);
	chip->irq_mask(&desc->irq_data);

	if (chip->irq_ack)
		chip->irq_ack(&desc->irq_data);

	eint_irq = irq_linear_revmap(eintd->domain, eintd->irq);
	generic_handle_irq(eint_irq);
	chip->irq_unmask(&desc->irq_data);
	chained_irq_exit(chip, desc);
}

static inline void exynos_irq_demux_eint(int irq_base, unsigned long pend,
					struct irq_domain *domain)
{
	unsigned int irq;

	while (pend) {
		irq = fls(pend) - 1;
		generic_handle_irq(irq_find_mapping(domain, irq_base + irq));
		pend &= ~(1 << irq);
	}
}

/* interrupt handler for wakeup interrupt 16 */
static void exynos_irq_demux_eint16_31(unsigned int irq, struct irq_desc *desc)
{
	struct irq_chip *chip = irq_get_chip(irq);
	struct exynos_weint_data *eintd = irq_get_handler_data(irq);
	struct samsung_pinctrl_drv_data *d = eintd->domain->host_data;
	unsigned long pend;
	unsigned long mask;

	chained_irq_enter(chip, desc);
	pend = readl(d->virt_base + d->ctrl->weint_pend + 0x8);
	mask = readl(d->virt_base + d->ctrl->weint_mask + 0x8);
	exynos_irq_demux_eint(16, pend & ~mask, eintd->domain);
	pend = readl(d->virt_base + d->ctrl->weint_pend + 0xC);
	mask = readl(d->virt_base + d->ctrl->weint_mask + 0xC);
	exynos_irq_demux_eint(24, pend & ~mask, eintd->domain);
	chained_irq_exit(chip, desc);
}

static int exynos_wkup_irq_map(struct irq_domain *h, unsigned int virq,
					irq_hw_number_t hw)
{
	irq_set_chip_and_handler(virq, &exynos_wkup_irq_chip, handle_level_irq);
	irq_set_chip_data(virq, h->host_data);
	set_irq_flags(virq, IRQF_VALID);
	return 0;
}

/*
 * irq domain callbacks for external wakeup interrupt controller.
 */
static const struct irq_domain_ops exynos_wkup_irqd_ops = {
	.map	= exynos_wkup_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

/*
 * exynos_eint_wkup_init() - setup handling of external wakeup interrupts.
 * @d: driver data of samsung pinctrl driver.
 */
static int exynos_eint_wkup_init(struct samsung_pinctrl_drv_data *d)
{
	struct device *dev = d->dev;
	struct device_node *wkup_np = NULL;
	struct device_node *np;
	struct exynos_weint_data *weint_data;
	int idx, irq;

	for_each_child_of_node(dev->of_node, np) {
		if (of_match_node(exynos_wkup_irq_ids, np)) {
			wkup_np = np;
			break;
		}
	}
	if (!wkup_np)
		return -ENODEV;

	d->wkup_irqd = irq_domain_add_linear(wkup_np, d->ctrl->nr_wint,
				&exynos_wkup_irqd_ops, d);
	if (!d->wkup_irqd) {
		dev_err(dev, "wakeup irq domain allocation failed\n");
		return -ENXIO;
	}

	weint_data = devm_kzalloc(dev, sizeof(*weint_data) * 17, GFP_KERNEL);
	if (!weint_data) {
		dev_err(dev, "could not allocate memory for weint_data\n");
		return -ENOMEM;
	}

	irq = irq_of_parse_and_map(wkup_np, 16);
	if (irq) {
		weint_data[16].domain = d->wkup_irqd;
		irq_set_chained_handler(irq, exynos_irq_demux_eint16_31);
		irq_set_handler_data(irq, &weint_data[16]);
	} else {
		dev_err(dev, "irq number for EINT16-32 not found\n");
	}

	for (idx = 0; idx < 16; idx++) {
		weint_data[idx].domain = d->wkup_irqd;
		weint_data[idx].irq = idx;

		irq = irq_of_parse_and_map(wkup_np, idx);
		if (irq) {
			irq_set_handler_data(irq, &weint_data[idx]);
			irq_set_chained_handler(irq, exynos_irq_eint0_15);
		} else {
			dev_err(dev, "irq number for eint-%x not found\n", idx);
		}
	}
	return 0;
}

/* pin banks of exynos4210 pin-controller 0 */
static struct samsung_pin_bank exynos4210_pin_banks0[] = {
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpa0", 0x00),
	EXYNOS_PIN_BANK_EINTG(6, 0x020, "gpa1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpb", 0x08),
	EXYNOS_PIN_BANK_EINTG(5, 0x060, "gpc0", 0x0c),
	EXYNOS_PIN_BANK_EINTG(5, 0x080, "gpc1", 0x10),
	EXYNOS_PIN_BANK_EINTG(4, 0x0A0, "gpd0", 0x14),
	EXYNOS_PIN_BANK_EINTG(4, 0x0C0, "gpd1", 0x18),
	EXYNOS_PIN_BANK_EINTG(5, 0x0E0, "gpe0", 0x1c),
	EXYNOS_PIN_BANK_EINTG(8, 0x100, "gpe1", 0x20),
	EXYNOS_PIN_BANK_EINTG(6, 0x120, "gpe2", 0x24),
	EXYNOS_PIN_BANK_EINTG(8, 0x140, "gpe3", 0x28),
	EXYNOS_PIN_BANK_EINTG(8, 0x160, "gpe4", 0x2c),
	EXYNOS_PIN_BANK_EINTG(8, 0x180, "gpf0", 0x30),
	EXYNOS_PIN_BANK_EINTG(8, 0x1A0, "gpf1", 0x34),
	EXYNOS_PIN_BANK_EINTG(8, 0x1C0, "gpf2", 0x38),
	EXYNOS_PIN_BANK_EINTG(6, 0x1E0, "gpf3", 0x3c),
};

/* pin banks of exynos4210 pin-controller 1 */
static struct samsung_pin_bank exynos4210_pin_banks1[] = {
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpj0", 0x00),
	EXYNOS_PIN_BANK_EINTG(5, 0x020, "gpj1", 0x04),
	EXYNOS_PIN_BANK_EINTG(7, 0x040, "gpk0", 0x08),
	EXYNOS_PIN_BANK_EINTG(7, 0x060, "gpk1", 0x0c),
	EXYNOS_PIN_BANK_EINTG(7, 0x080, "gpk2", 0x10),
	EXYNOS_PIN_BANK_EINTG(7, 0x0A0, "gpk3", 0x14),
	EXYNOS_PIN_BANK_EINTG(8, 0x0C0, "gpl0", 0x18),
	EXYNOS_PIN_BANK_EINTG(3, 0x0E0, "gpl1", 0x1c),
	EXYNOS_PIN_BANK_EINTG(8, 0x100, "gpl2", 0x20),
	EXYNOS_PIN_BANK_EINTN(6, 0x120, "gpy0"),
	EXYNOS_PIN_BANK_EINTN(4, 0x140, "gpy1"),
	EXYNOS_PIN_BANK_EINTN(6, 0x160, "gpy2"),
	EXYNOS_PIN_BANK_EINTN(8, 0x180, "gpy3"),
	EXYNOS_PIN_BANK_EINTN(8, 0x1A0, "gpy4"),
	EXYNOS_PIN_BANK_EINTN(8, 0x1C0, "gpy5"),
	EXYNOS_PIN_BANK_EINTN(8, 0x1E0, "gpy6"),
	EXYNOS_PIN_BANK_EINTN(8, 0xC00, "gpx0"),
	EXYNOS_PIN_BANK_EINTN(8, 0xC20, "gpx1"),
	EXYNOS_PIN_BANK_EINTN(8, 0xC40, "gpx2"),
	EXYNOS_PIN_BANK_EINTN(8, 0xC60, "gpx3"),
};

/* pin banks of exynos4210 pin-controller 2 */
static struct samsung_pin_bank exynos4210_pin_banks2[] = {
	EXYNOS_PIN_BANK_EINTN(7, 0x000, "gpz"),
};

/*
 * Samsung pinctrl driver data for Exynos4210 SoC. Exynos4210 SoC includes
 * three gpio/pin-mux/pinconfig controllers.
 */
struct samsung_pin_ctrl exynos4210_pin_ctrl[] = {
	{
		/* pin-controller instance 0 data */
		.pin_banks	= exynos4210_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos4210_pin_banks0),
		.geint_con	= EXYNOS_GPIO_ECON_OFFSET,
		.geint_mask	= EXYNOS_GPIO_EMASK_OFFSET,
		.geint_pend	= EXYNOS_GPIO_EPEND_OFFSET,
		.svc		= EXYNOS_SVC_OFFSET,
		.eint_gpio_init = exynos_eint_gpio_init,
		.label		= "exynos4210-gpio-ctrl0",
	}, {
		/* pin-controller instance 1 data */
		.pin_banks	= exynos4210_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos4210_pin_banks1),
		.nr_wint	= 32,
		.geint_con	= EXYNOS_GPIO_ECON_OFFSET,
		.geint_mask	= EXYNOS_GPIO_EMASK_OFFSET,
		.geint_pend	= EXYNOS_GPIO_EPEND_OFFSET,
		.weint_con	= EXYNOS_WKUP_ECON_OFFSET,
		.weint_mask	= EXYNOS_WKUP_EMASK_OFFSET,
		.weint_pend	= EXYNOS_WKUP_EPEND_OFFSET,
		.svc		= EXYNOS_SVC_OFFSET,
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
		.label		= "exynos4210-gpio-ctrl1",
	}, {
		/* pin-controller instance 2 data */
		.pin_banks	= exynos4210_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos4210_pin_banks2),
		.label		= "exynos4210-gpio-ctrl2",
	},
};
